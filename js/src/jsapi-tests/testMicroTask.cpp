/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <type_traits>

#include "js/friend/MicroTask.h"
#include "js/GCAPI.h"
#include "js/ProfilingStack.h"
#include "js/Promise.h"
#include "js/PropertyAndElement.h"
#include "jsapi-tests/tests.h"

using namespace JS;

// JSMicroTaskRef refers into the MicroTask it came from, so it is only usable
// as a temporary: it can't be copied or moved, and its methods can't be called
// on an lvalue.
static_assert(!std::is_copy_constructible_v<JSMicroTaskRef>);
static_assert(!std::is_move_constructible_v<JSMicroTaskRef>);
static_assert(std::is_invocable_v<decltype(&JSMicroTaskRef::executionGlobal),
                                  JSMicroTaskRef&&>);
static_assert(!std::is_invocable_v<decltype(&JSMicroTaskRef::executionGlobal),
                                   JSMicroTaskRef&>);
static_assert(!std::is_invocable_v<decltype(&JSMicroTaskRef::executionGlobal),
                                   const JSMicroTaskRef&>);

namespace {

// Stands in for a Gecko MicroTaskRunnable: an embedder-side C++ task that is
// put on the queue as a PrivateValue and owned by the queue while it is there.
struct EmbedderTask {
  size_t traceCalls = 0;
};

// A minimal embedder JobQueue whose only job is to observe the microtask
// tracing hooks.
class TestJobQueue : public JS::JobQueue {
 public:
  size_t traceNonGCThingCalls = 0;

  bool getHostDefinedData(
      JSContext* cx, JS::MutableHandle<JSObject*> incumbentGlobal,
      JS::MutableHandle<JSObject*> optionalHostDefinedData) const override {
    incumbentGlobal.set(nullptr);
    optionalHostDefinedData.set(nullptr);
    return true;
  }

  bool getHostDefinedGlobal(JSContext* cx,
                            JS::MutableHandle<JSObject*> data) const override {
    data.set(nullptr);
    return true;
  }

  void runJobs(JSContext* cx) override {}
  bool isDrainingStopped() const override { return false; }

  void traceNonGCThingMicroTask(JSTracer* trc, JS::Value* valuePtr) override {
    traceNonGCThingCalls++;
    // A real embedder decodes the value back into its own task type and traces
    // whatever that task keeps reachable. Nothing here is a GC thing, so just
    // record that the hook reached the right task.
    auto* task = static_cast<EmbedderTask*>(valuePtr->toPrivate());
    task->traceCalls++;
  }

 protected:
  js::UniquePtr<SavedJobQueue> saveJobQueue(JSContext*) override {
    return nullptr;
  }
};

}  // namespace

// An embedder microtask (a non-object Value) must be traced via the
// JobQueue's traceNonGCThingMicroTask hook, not directly. Enqueue a
// PrivateValue task, force a GC, and confirm the hook fired for that task.
BEGIN_TEST(testMicroTask_embedderTracedViaHook) {
  TestJobQueue queue;
  JS::SetJobQueue(cx, &queue);

  EmbedderTask embedderTask;
  CHECK(JS::EnqueueMicroTask(cx, MicroTask::FromEmbedderPtr(&embedderTask)));
  CHECK(JS::HasRegularMicroTasks(cx));

  size_t before = queue.traceNonGCThingCalls;
  JS_GC(cx);
  CHECK(queue.traceNonGCThingCalls > before);
  CHECK(embedderTask.traceCalls > 0);

  // Drain the task so it doesn't outlive the stack pointer we enqueued. A
  // PrivateValue is stored as a double; it round-trips unchanged.
  JS::Rooted<mozilla::Maybe<MicroTask>> task(cx);
  JS::DequeueNextMicroTask(cx, &task);
  CHECK(task.isSome());
  CHECK(!task->isJS());
  CHECK(task->isEmbedder());

  CHECK(task->embedderValue().toPrivate() == &embedderTask);
  CHECK(!JS::HasAnyMicroTasks(cx));

  JS::SetJobQueue(cx, nullptr);
  return true;
}
END_TEST(testMicroTask_embedderTracedViaHook)

// The hook is not just for tasks sitting in the queue: an embedder that holds a
// dequeued task across a GC (Gecko does this while a task is suppressed, and
// while it is running) roots the MicroTask, and MicroTask::trace must reach the
// hook there too.
BEGIN_TEST(testMicroTask_rootedEmbedderTracedViaHook) {
  TestJobQueue queue;
  JS::SetJobQueue(cx, &queue);

  EmbedderTask embedderTask;
  CHECK(JS::EnqueueMicroTask(cx, MicroTask::FromEmbedderPtr(&embedderTask)));

  JS::Rooted<mozilla::Maybe<MicroTask>> task(cx);

  JS::DequeueNextMicroTask(cx, &task);
  CHECK(task.isSome());
  CHECK(!JS::HasAnyMicroTasks(cx));

  size_t before = queue.traceNonGCThingCalls;
  JS_GC(cx);
  CHECK(queue.traceNonGCThingCalls > before);
  CHECK(embedderTask.traceCalls > 0);

  CHECK(task->isEmbedder());
  CHECK(task->embedderValue().toPrivate() == &embedderTask);

  JS::SetJobQueue(cx, nullptr);
  return true;
}
END_TEST(testMicroTask_rootedEmbedderTracedViaHook)

// Dequeueing from an empty queue yields Nothing, and tracing Nothing must not
// report anything to the embedder; the hook would have no valid task to decode.
BEGIN_TEST(testMicroTask_emptyRootedTaskNotReported) {
  TestJobQueue queue;
  JS::SetJobQueue(cx, &queue);

  CHECK(!JS::HasAnyMicroTasks(cx));
  JS::Rooted<mozilla::Maybe<MicroTask>> task(cx);
  JS::DequeueNextMicroTask(cx, &task);
  CHECK(task.isNothing());

  size_t before = queue.traceNonGCThingCalls;
  JS_GC(cx);
  CHECK(queue.traceNonGCThingCalls == before);

  JS::SetJobQueue(cx, nullptr);
  return true;
}
END_TEST(testMicroTask_emptyRootedTaskNotReported)

static bool NoopHandler(JSContext* cx, unsigned argc, JS::Value* vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
  args.rval().setUndefined();
  return true;
}

// A JS microtask (a Promise reaction job) is enqueued through the internal
// EnqueueJob path with Kind::JS, so it is traced directly rather than via the
// embedder hook. Attaching a reaction and resolving the promise schedules such
// a job; it must survive GC without the embedder's traceNonGCThingMicroTask
// hook being consulted for it.
BEGIN_TEST(testMicroTask_jsReactionJobTracedDirectly) {
  TestJobQueue queue;
  JS::SetJobQueue(cx, &queue);

  RootedObject promise(cx, JS::NewPromiseObject(cx, nullptr));
  CHECK(promise);

  RootedFunction onFulfilled(
      cx, JS_NewFunction(cx, NoopHandler, 1, 0, "onFulfilled"));
  CHECK(onFulfilled);
  RootedObject onFulfilledObj(cx, JS_GetFunctionObject(onFulfilled));
  CHECK(JS::AddPromiseReactions(cx, promise, onFulfilledObj, nullptr));

  // Resolving the promise enqueues the reaction as a JS microtask.
  CHECK(JS::ResolvePromise(cx, promise, JS::UndefinedHandleValue));
  CHECK(JS::HasRegularMicroTasks(cx));

  size_t hookCallsBefore = queue.traceNonGCThingCalls;
  JS_GC(cx);
  // The JS reaction job is an object traced directly; the embedder hook must
  // not have been used to keep it alive.
  CHECK(queue.traceNonGCThingCalls == hookCallsBefore);

  // The surviving task is a live JS microtask.
  JS::Rooted<mozilla::Maybe<MicroTask>> task(cx);
  JS::PeekNextMicroTask(cx, &task);
  CHECK(task.isSome());
  CHECK(task->isJS());
  CHECK(task->asJS().executionGlobal());

  JS::SetJobQueue(cx, nullptr);
  return true;
}
END_TEST(testMicroTask_jsReactionJobTracedDirectly)

static size_t settledReactionCalls;

static bool SettledReactionHandler(JSContext* cx, unsigned argc,
                                   JS::Value* vp) {
  JS::CallArgs args = JS::CallArgsFromVp(argc, vp);
  JS_GC(cx);
  RootedObject argument(cx, &args[0].toObject());
  RootedValue value(cx);
  if (!JS_GetProperty(cx, argument, "value", &value)) {
    return false;
  }
  MOZ_RELEASE_ASSERT(value == Int32Value(42));
  settledReactionCalls++;
  args.rval().setUndefined();
  return true;
}

namespace {

class HostDataJobQueue : public TestJobQueue {
 public:
  bool withHostData = false;

  bool getHostDefinedData(
      JSContext* cx, MutableHandleObject incumbentGlobal,
      MutableHandleObject optionalHostDefinedData) const override {
    incumbentGlobal.set(JS::CurrentGlobalOrNull(cx));
    optionalHostDefinedData.set(nullptr);
    if (!withHostData) {
      return true;
    }
    optionalHostDefinedData.set(JS_NewPlainObject(cx));
    return optionalHostDefinedData &&
           JS_DefineProperty(cx, optionalHostDefinedData, "value", 23, 0);
  }
};

}  // namespace

BEGIN_TEST(testMicroTask_settledReactionSurvivesGC) {
  HostDataJobQueue queue;
  JS::SetJobQueue(cx, &queue);
  settledReactionCalls = 0;

  for (bool withHostData : {false, true}) {
    queue.withHostData = withHostData;

    for (bool rejected : {false, true}) {
      RootedObject promise(cx, JS::NewPromiseObject(cx, nullptr));
      CHECK(promise);
      RootedObject argument(cx, JS_NewPlainObject(cx));
      CHECK(argument);
      CHECK(JS_DefineProperty(cx, argument, "value", 42, 0));
      RootedValue argumentValue(cx, ObjectValue(*argument));
      if (rejected) {
        CHECK(JS::RejectPromise(cx, promise, argumentValue));
      } else {
        CHECK(JS::ResolvePromise(cx, promise, argumentValue));
      }
      RootedFunction handler(
          cx, JS_NewFunction(cx, SettledReactionHandler, 1, 0, "reaction"));
      CHECK(handler);
      RootedObject handlerObject(cx, JS_GetFunctionObject(handler));
      CHECK(JS::AddPromiseReactions(cx, promise, handlerObject, handlerObject));
      RootedObject thenResult(
          cx, JS::CallOriginalPromiseThen(cx, promise, handlerObject,
                                          handlerObject));
      CHECK(thenResult);
      CHECK(JS::GetRegularMicroTaskCount(cx) == 2);

      promise = nullptr;
      argument = nullptr;
      argumentValue.setUndefined();
      handler = nullptr;
      handlerObject = nullptr;
      JS_GC(cx);

      auto saved = JS::SaveMicroTaskQueue(cx);
      CHECK(saved);
      CHECK(!JS::HasAnyMicroTasks(cx));
      JS_GC(cx);
      JS::RestoreMicroTaskQueue(cx, std::move(saved));

      for (size_t i = 0; i < 2; i++) {
        JS::Rooted<mozilla::Maybe<MicroTask>> task(cx);
        JS::DequeueNextMicroTask(cx, &task);
        CHECK(task.isSome());
        JS_GC(cx);
        CHECK(JS::PrependMicroTask(cx, *task));
        JS::DequeueNextMicroTask(cx, &task);
        CHECK(task.isSome());
        CHECK(task->isJS());
        CHECK(task->asJS().executionGlobal() == global);
        CHECK(task->asJS().maybeGetPromise() ==
              (i == 0 ? nullptr : thenResult.get()));

        // Inline reactions have no flow when the profiler is disabled.
        // Reactions with host defined data are never inline.
        uint64_t flowId;
        CHECK(task->asJS().getFlowId(&flowId) == withHostData);

        RootedObject incumbentGlobal(cx), hostData(cx), stack(cx);
        CHECK(
            task->asJS().maybeGetHostDefinedData(&incumbentGlobal, &hostData));
        CHECK(incumbentGlobal == global);
        CHECK(!!hostData == withHostData);
        if (withHostData) {
          RootedValue value(cx);
          CHECK(JS_GetProperty(cx, hostData, "value", &value));
          CHECK(value == Int32Value(23));
        }
        CHECK(task->asJS().maybeGetAllocationSite(&stack));
        CHECK(!stack);
        JS_GC(cx);
        CHECK(JS::RunJSMicroTask(cx, task));
      }
      CHECK(!JS::HasAnyMicroTasks(cx));
    }
  }
  CHECK(settledReactionCalls == 8);
  CHECK(queue.traceNonGCThingCalls == 0);
  JS::SetJobQueue(cx, nullptr);
  return true;
}
END_TEST(testMicroTask_settledReactionSurvivesGC)

static ProfilingStack flowProfilingStack;
static uint64_t enqueuedFlowIds[2];
static size_t enqueuedFlowCount;

static void RecordFlowMarker(mozilla::MarkerCategory, const char* name,
                             uint64_t flowId) {
  MOZ_RELEASE_ASSERT(strcmp(name, "JS::EnqueueJob") == 0);
  MOZ_RELEASE_ASSERT(enqueuedFlowCount < std::size(enqueuedFlowIds));
  enqueuedFlowIds[enqueuedFlowCount++] = flowId;
}

BEGIN_TEST(testMicroTask_settledReactionFlowIdsWithProfiler) {
  HostDataJobQueue queue;
  JS::SetJobQueue(cx, &queue);
  js::SetContextProfilingStack(cx, &flowProfilingStack);
  js::EnableContextProfilingStack(cx, true);
  js::RegisterContextProfilerMarkers(cx, nullptr, nullptr, RecordFlowMarker,
                                     nullptr);
  settledReactionCalls = 0;
  enqueuedFlowCount = 0;

  RootedObject promise(cx, JS::NewPromiseObject(cx, nullptr));
  CHECK(promise);
  RootedObject argument(cx, JS_NewPlainObject(cx));
  CHECK(argument);
  CHECK(JS_DefineProperty(cx, argument, "value", 42, 0));
  RootedValue argumentValue(cx, ObjectValue(*argument));
  CHECK(JS::ResolvePromise(cx, promise, argumentValue));
  RootedFunction handler(
      cx, JS_NewFunction(cx, SettledReactionHandler, 1, 0, "reaction"));
  CHECK(handler);
  RootedObject handlerObject(cx, JS_GetFunctionObject(handler));
  CHECK(JS::AddPromiseReactions(cx, promise, handlerObject, handlerObject));
  CHECK(JS::CallOriginalPromiseThen(cx, promise, handlerObject, handlerObject));
  CHECK(JS::GetRegularMicroTaskCount(cx) == 2);

  CHECK(enqueuedFlowCount == 2);
  CHECK(enqueuedFlowIds[0] != 0);
  CHECK(enqueuedFlowIds[1] != 0);
  CHECK(enqueuedFlowIds[0] != enqueuedFlowIds[1]);

  JS_GC(cx);
  auto saved = JS::SaveMicroTaskQueue(cx);
  CHECK(saved);
  CHECK(!JS::HasAnyMicroTasks(cx));
  JS_GC(cx);
  JS::RestoreMicroTaskQueue(cx, std::move(saved));

  for (size_t i = 0; i < 2; i++) {
    JS::Rooted<mozilla::Maybe<MicroTask>> task(cx);
    JS::DequeueNextMicroTask(cx, &task);
    CHECK(task.isSome());
    uint64_t flowId;
    CHECK(task->asJS().getFlowId(&flowId));
    CHECK(flowId == enqueuedFlowIds[i]);
    JS_GC(cx);
    CHECK(task->asJS().getFlowId(&flowId));
    CHECK(flowId == enqueuedFlowIds[i]);
    CHECK(JS::RunJSMicroTask(cx, task));
  }
  CHECK(!JS::HasAnyMicroTasks(cx));
  CHECK(settledReactionCalls == 2);

  js::RegisterContextProfilerMarkers(cx, nullptr, nullptr, nullptr, nullptr);
  js::EnableContextProfilingStack(cx, false);
  js::SetContextProfilingStack(cx, nullptr);
  JS::SetJobQueue(cx, nullptr);
  return true;
}
END_TEST(testMicroTask_settledReactionFlowIdsWithProfiler)

BEGIN_TEST(testMicroTask_inlineThenableSurvivesGC) {
  HostDataJobQueue queue;
  JS::SetJobQueue(cx, &queue);

  for (bool withHostData : {false, true}) {
    queue.withHostData = withHostData;
    for (int mode = 0; mode < 3; mode++) {
      RootedObject promise(cx, JS::NewPromiseObject(cx, nullptr));
      CHECK(promise);
      RootedValue thenable(cx);
      if (mode == 0) {
        EVAL("Promise.resolve({value: 42})", &thenable);
      } else if (mode == 1) {
        EVAL("({then(resolve) { resolve({value: 42}); }})", &thenable);
      } else {
        EVAL("({get then() { return resolve => resolve({value: 42}); }})",
             &thenable);
      }
      if (mode == 2) {
        CHECK(JS::SafeResolve(cx, promise, thenable));
        CHECK(JS::GetPromiseState(promise) == JS::PromiseState::Pending);
      } else {
        CHECK(JS::ResolvePromise(cx, promise, thenable));
      }
      thenable.setUndefined();
      CHECK(JS::GetRegularMicroTaskCount(cx) == 1);
      JS_GC(cx);
      auto saved = JS::SaveMicroTaskQueue(cx);
      CHECK(saved);
      JS_GC(cx);
      JS::RestoreMicroTaskQueue(cx, std::move(saved));

      Rooted<mozilla::Maybe<MicroTask>> task(cx);

      JS::DequeueNextMicroTask(cx, &task);
      CHECK(task.isSome());
      JS_GC(cx);
      CHECK(task->asJS().executionGlobal() == global);
      CHECK(task->asJS().maybeGetPromise() == promise);
      uint64_t flowId;
      CHECK(!task->asJS().getFlowId(&flowId));
      RootedObject incumbent(cx), data(cx), stack(cx);
      CHECK(task->asJS().maybeGetHostDefinedData(&incumbent, &data));
      CHECK(incumbent == global);
      CHECK(!!data == withHostData);
      if (withHostData) {
        RootedValue value(cx);
        CHECK(JS_GetProperty(cx, data, "value", &value));
        CHECK(value == Int32Value(23));
      }
      CHECK(task->asJS().maybeGetAllocationSite(&stack));
      CHECK(!stack);
      CHECK(JS::PrependMicroTask(cx, *task));
      task = mozilla::Nothing();
      size_t count = 0;
      while (JS::HasAnyMicroTasks(cx)) {
        CHECK(++count < 10);
        JS::DequeueNextMicroTask(cx, &task);
        JS_GC(cx);
        CHECK(JS::RunJSMicroTask(cx, task));
      }
      CHECK(JS::GetPromiseState(promise) == JS::PromiseState::Fulfilled);
      RootedValue result(cx, JS::GetPromiseResult(promise));
      RootedObject resultObject(cx, &result.toObject());
      CHECK(JS_GetProperty(cx, resultObject, "value", &result));
      CHECK(result == Int32Value(42));
    }
  }
  JS::SetJobQueue(cx, nullptr);
  return true;
}
END_TEST(testMicroTask_inlineThenableSurvivesGC)

// The dequeue and peek functions overwrite whatever |out| held, including with
// Nothing once their queue is empty. The generic forms check the debugger queue
// before the regular one.
BEGIN_TEST(testMicroTask_peekAndDequeueInPlace) {
  TestJobQueue queue;
  JS::SetJobQueue(cx, &queue);

  EmbedderTask first, second, debug;
  CHECK(JS::EnqueueMicroTask(cx, MicroTask::FromEmbedderPtr(&first)));
  CHECK(JS::EnqueueMicroTask(cx, MicroTask::FromEmbedderPtr(&second)));
  CHECK(JS::EnqueueDebugMicroTask(cx, MicroTask::FromEmbedderPtr(&debug)));

  JS::Rooted<mozilla::Maybe<MicroTask>> task(
      cx, mozilla::Some(MicroTask::FromEmbedderPtr(&second)));
  JS::PeekNextMicroTask(cx, &task);
  CHECK(task->embedderPtr() == &debug);
  CHECK(JS::HasDebuggerMicroTasks(cx));

  JS::DequeueNextMicroTask(cx, &task);
  CHECK(task->embedderPtr() == &debug);
  CHECK(!JS::HasDebuggerMicroTasks(cx));

  JS::PeekNextMicroTask(cx, &task);
  CHECK(task->embedderPtr() == &first);
  CHECK(JS::GetRegularMicroTaskCount(cx) == 2);

  JS::DequeueNextMicroTask(cx, &task);
  CHECK(task->embedderPtr() == &first);
  JS::DequeueNextMicroTask(cx, &task);
  CHECK(task->embedderPtr() == &second);
  CHECK(!JS::HasAnyMicroTasks(cx));

  JS::PeekNextMicroTask(cx, &task);
  CHECK(task.isNothing());
  task = mozilla::Some(MicroTask::FromEmbedderPtr(&first));
  JS::DequeueNextMicroTask(cx, &task);
  CHECK(task.isNothing());

  CHECK(JS::EnqueueMicroTask(cx, MicroTask::FromEmbedderPtr(&first)));
  CHECK(JS::EnqueueDebugMicroTask(cx, MicroTask::FromEmbedderPtr(&debug)));
  JS::DequeueNextRegularMicroTask(cx, &task);
  CHECK(task->embedderPtr() == &first);
  JS::DequeueNextRegularMicroTask(cx, &task);
  CHECK(task.isNothing());
  JS::DequeueNextDebuggerMicroTask(cx, &task);
  CHECK(task->embedderPtr() == &debug);
  JS::DequeueNextDebuggerMicroTask(cx, &task);
  CHECK(task.isNothing());

  JS::SetJobQueue(cx, nullptr);
  return true;
}
END_TEST(testMicroTask_peekAndDequeueInPlace)
