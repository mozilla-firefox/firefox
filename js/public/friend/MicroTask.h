/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef js_friend_MicroTask_h
#define js_friend_MicroTask_h

#include "mozilla/Assertions.h"
#include "mozilla/Maybe.h"

#include "jstypes.h"

#include "js/GCPolicyAPI.h"
#include "js/RootingAPI.h"
#include "js/TypeDecls.h"
#include "js/UniquePtr.h"
#include "js/Value.h"

namespace js {
class MicroTaskQueueElement;
}  // namespace js

namespace JS {

// [SMDOC] MicroTasks in SpiderMonkey
//
// To enable higher performance, this header allows an embedding to work with
// a MicroTask queue stored inside the JS engine. This allows optimization of
// tasks by avoiding allocations in important cases.
//
// To make this work, we need some cooperation with the embedding.
//
// The high level thrust of this is that rather than managing the JobQueue
// themselves, embeddings assume that there's a JobQueue available to them
// inside the engine. When 'runJobs' happens, the embedding is responsible
// for pulling jobs of the queue, doing any setup required, then calling
// them.
//
// Embeddings can enqueue their own jobs by wrapping their job in
// a JS::Value (perhaps PrivateValue!) and making an embedder micro task
// using FromEmbedderValue.
//
// The major requirement is that if a MicroTask is a JS MicroTask, the job must
// be run by calling RunJSMicroTask, while in the realm specified by the global
// returned by asJS().executionGlobal(), e.g
//
//    Rooted<mozilla::Maybe<MicroTask>> task(cx);
//    JS::DequeueNextMicroTask(cx, &task);
//    if (task.isSome() && task->isJS()) {
//      JSObject* global = task->asJS().executionGlobal();
//      if (global) {
//        AutoRealm ar(cx, global);
//        if (!JS::RunJSMicroTask(cx, task)) {
//          ...
//        }
//      }
//    }

class JSMicroTaskRef;

// A MicroTask is an opaque blob of data tagged with its kind. Using this
// MicroTask system allows embedders to put whatever pointer they would like
// into the queue via the Embedder kind. The value will be dequeued unchanged.
//
// The kind is tracked so consumers (such as Gecko's CycleCollectedJSContext)
// can dispatch on the task kind without having to inspect the value itself.
// A JS microtask is created by the engine, and its contents are opaque to
// embedders. An Embedder microtask is an arbitrary value enqueued through
// the embedder API.
//
// A MicroTask always holds a task. Where there may be no task (e.g. the result
// of dequeueing from an empty queue) a mozilla::Maybe<MicroTask> is used.
//
// The interface specific to JS MicroTasks is reached through asJS(), which
// returns a JSMicroTaskRef that can only be used as a temporary.
//
// The major requirement here is that if the MicroTask is a JS MicroTask, it
// must be run by calling RunJSMicroTask, while in the realm specified by
// asJS().executionGlobal().
//
// Embedder MicroTasks are opaque to the engine. Their values can be
// retrieved by the embedder by calling embedderValue(), which will assert
// the task is an embedder task.
//
// Embedder microtasks can be traced by the GC while in the queue by
// defining JobQueue::traceNonGCThingMicroTask -- see MicroTask::trace
// for details.
class MicroTask {
 public:
  MicroTask() = delete;

  // Wrap an embedder-provided value as an Embedder MicroTask.
  static MicroTask FromEmbedderValue(const JS::Value& value) {
    return MicroTask(Kind::Embedder, value);
  }

  // Wrap an embedder-provided pointer as an Embedder MicroTask.
  static MicroTask FromEmbedderPtr(void* ptr) {
    return MicroTask(Kind::Embedder, JS::PrivateValue(ptr));
  }

  bool isJS() const {
    if (kind_ >= Kind::FirstJSKind) {
      MOZ_ASSERT(kind_ < Kind::EndJSKind);
      return true;
    }
    return false;
  }
  bool isEmbedder() const { return kind_ == Kind::Embedder; }

  // The value the embedder enqueued, returned unchanged.
  const JS::Value& embedderValue() const {
    MOZ_ASSERT(isEmbedder());
    return value_;
  }

  void* embedderPtr() const {
    MOZ_ASSERT(isEmbedder());
    MOZ_ASSERT(value_.isDouble());
    return value_.toPrivate();
  }

  // Access the interface specific to JS MicroTasks. Only valid after checking
  // isJS().
  inline JSMicroTaskRef asJS() const;

  // Trace a MicroTask held across a GC, whether it is rooted on the stack, in
  // a PersistentRooted, or owned by the engine's queue.
  //
  // The JS engine already knows how to trace JS microtasks. However,
  // Embedder MicroTasks may not be a GC thing at all (e.g. a C++ pointer
  // wrapped with PrivateValue). Normally the tracer would stop there, but
  // the embedder microtask may hold onto its own JS objects and may need
  // its own tracing.
  //
  // To support this, embedder micro tasks that are not GC things will be handed
  // to JobQueue::traceNonGCThingMicroTask, which means this has to redirect
  // through the JSContext to find the installed JobQueue, and so
  // cannot be inlined here.
  JS_PUBLIC_API void trace(JSTracer* trc, const char* name);

 private:
  friend class JSMicroTaskRef;
  friend class js::MicroTaskQueueElement;

  // This kind accessor returns the raw uint8_t kind; this is because the
  // engine provides its own semantics (and names) to the kinds from
  // FirstJSKind to EndJSKind
  uint8_t kind() const { return kind_; }

  // Distinguishes microtasks enqueued by the JS engine from those enqueued by
  // the embedder.
  //
  // Private, so that an embedder cannot construct a MicroTask of JS kind; the
  // engine reaches it through the friend declaration of
  // js::MicroTaskQueueElement.
  //
  // There are many kinds of JS kinds, so we allocate a range to the JS engine
  // for its purposes
  static constexpr uint8_t JSKindsAllocated = 16;
  enum Kind : uint8_t {
    Embedder = 0,
    FirstJSKind,
    EndJSKind = FirstJSKind + JSKindsAllocated,
  };

  explicit MicroTask(Kind kind, const JS::Value& value)
      : kind_(kind), value_(value) {
    MOZ_ASSERT(kind_ < Kind::EndJSKind);
  }

  MicroTask(Kind kind, uint32_t jsBits, const JS::Value& value,
            const JS::Value& extra0, const JS::Value& extra1,
            const JS::Value& extra2, const JS::Value& extra3, uint64_t flowId)
      : kind_(kind),
        jsBits_(jsBits),
        value_(value),
        extra_{extra0, extra1, extra2, extra3},
        flowId_(flowId) {
    MOZ_ASSERT(kind_ >= Kind::FirstJSKind && kind_ < Kind::EndJSKind);
  }

  // Keep kind_ and jsBits_ together before the 8-byte fields to minimize
  // padding.
  Kind kind_;
  uint32_t jsBits_ = 0;
  JS::Value value_;
  // Additional payload, used only by JS kinds which carry their job inline
  // rather than in a job object.
  JS::Value extra_[4];
  // Profiler flow id for JS kinds without a job object, or 0 if none.
  uint64_t flowId_ = 0;
};

}  // namespace JS

namespace JS {

// Run a JS MicroTask. Must be called while in the realm of
// asJS().executionGlobal().
//
// Running a task can GC, so this takes a Handle to guarantee the task is
// rooted.
//
// Returns false if an exception is thrown while processing, which includes
// the case of the task having become a dead wrapper.
JS_PUBLIC_API bool RunJSMicroTask(JSContext* cx, Handle<MicroTask> task);

// An overload for Maybe. Obey the same rules above.
//
// Returns false on exception, or if task is Nothing();
JS_PUBLIC_API bool RunJSMicroTask(JSContext* cx,
                                  Handle<mozilla::Maybe<MicroTask>> task);

// The interface to a MicroTask created by the JS engine, obtained from
// MicroTask::asJS().
//
// A JSMicroTaskRef refers to the MicroTask it came from, so it must not outlive
// it. To make that hold without having to reason about it, a JSMicroTaskRef can
// only be used as a temporary: it cannot be copied or moved, and its methods
// can only be called on an rvalue, as in task.asJS().executionGlobal().
class MOZ_TEMPORARY_CLASS JSMicroTaskRef {
 public:
  JSMicroTaskRef(const JSMicroTaskRef&) = delete;
  JSMicroTaskRef(JSMicroTaskRef&&) = delete;
  JSMicroTaskRef& operator=(const JSMicroTaskRef&) = delete;
  JSMicroTaskRef& operator=(JSMicroTaskRef&&) = delete;

  // This is the global associated with the realm RunJSMicroTask expects to be
  // in. Returns nullptr if a dead wrapper is found, and the task can't be run.
  JS_PUBLIC_API JSObject* executionGlobal() &&;

  // Via the following accessors various host defined data is exposed to the
  // embedder (see JobQueue::getHostDefinedData).
  //
  // These return true on success and false on failure. They return false if
  // there are any unwrapping issues (e.g., dead wrappers), and true with
  // nullptr if there just isn't any data.
  //
  // This disambiguates between no-data and the dead wrapper case
  JS_PUBLIC_API bool maybeGetHostDefinedData(
      MutableHandleObject incumbentGlobal,
      MutableHandleObject optionalHostDefinedData) &&;
  JS_PUBLIC_API bool maybeGetAllocationSite(MutableHandleObject out) &&;

  // Returns nullptr if there is no promise or a dead wrapper is found.
  JS_PUBLIC_API JSObject* maybeGetPromise() &&;

  // Get the flow id from a JS microtask for profiler markers.
  // Returns false if the task has become a dead wrapper, in which case the
  // microtask doesn't run anyhow, or if the task has no flow because it was
  // enqueued while the profiler was disabled.
  JS_PUBLIC_API bool getFlowId(uint64_t* uid) &&;

 private:
  friend class MicroTask;
  friend bool RunJSMicroTask(JSContext* cx, Handle<MicroTask> task);
  friend bool RunJSMicroTask(JSContext* cx,
                             Handle<mozilla::Maybe<MicroTask>> task);

  explicit JSMicroTaskRef(const MicroTask& task) : task_(task) {
    MOZ_ASSERT(task.isJS());
  }

  // Only reachable through RunJSMicroTask, whose Handle parameter guarantees
  // the task is rooted.
  bool run(JSContext* cx) &&;

  JSObject* maybeWrappedObject() const {
    // A JS MicroTask is always an object; only the embedder can enqueue
    // non-object values.
    MOZ_ASSERT(task_.value_.isObject());
    return &task_.value_.toObject();
  }

  const MicroTask& task_;
};

inline JSMicroTaskRef MicroTask::asJS() const { return JSMicroTaskRef(*this); }

// Queue Management. This is done per-JSContext.
//
// Internally we maintain two queues, one for 'debugger' microtasks. These
// are expected in normal operation to either be popped off the queue first,
// or processed separately.
//
// Non-debugger MicroTasks are "regular" microtasks, and go to the regular
// microtask queue.
//
// In general, we highly recommend that most embeddings use only the regular
// microtask queue. The debugger microtask queue mostly exists to support
// patterns used by Gecko.
//
// Embedders can only enqueue Embedder MicroTasks, built with
// MicroTask::FromEmbedderValue. PrependMicroTask additionally accepts a task
// previously obtained from a Dequeue call, which puts it back on the queue with
// its kind preserved.
//
// These methods only fail for OOM.
JS_PUBLIC_API bool EnqueueMicroTask(JSContext* cx, const MicroTask& entry);
JS_PUBLIC_API bool EnqueueDebugMicroTask(JSContext* cx, const MicroTask& entry);
JS_PUBLIC_API bool PrependMicroTask(JSContext* cx, const MicroTask& entry);

// Dequeue the next MicroTask into |out|, which is overwritten with Nothing if
// there are no MicroTasks of the appropriate kind. Writing into the caller's
// rooted storage avoids copying the task through temporaries. These can't GC.
//
// The generic DequeueNext will always pull a debugger microtask first,
// if one exists, then a regular microtask if one exists.
// - DequeueNextDebuggerMicroTask only pulls from the debugger queue.
// - DequeueNextRegularMicroTask only pulls from the regular queue.
//
// Checking for emptiness before calling these is not required, and is very
// slightly less efficient.
JS_PUBLIC_API void DequeueNextMicroTask(
    JSContext* cx, JS::MutableHandle<mozilla::Maybe<MicroTask>> out);
JS_PUBLIC_API void DequeueNextDebuggerMicroTask(
    JSContext* cx, JS::MutableHandle<mozilla::Maybe<MicroTask>> out);
JS_PUBLIC_API void DequeueNextRegularMicroTask(
    JSContext* cx, JS::MutableHandle<mozilla::Maybe<MicroTask>> out);

// Peek at the next MicroTask without removing it from the queue, writing it
// (or Nothing if there are no MicroTasks) into |out|.
// Checks debugger queue first, then regular queue.
JS_PUBLIC_API void PeekNextMicroTask(
    JSContext* cx, JS::MutableHandle<mozilla::Maybe<MicroTask>> out);

// Returns true if there are -any- microtasks pending in the queue.
JS_PUBLIC_API bool HasAnyMicroTasks(JSContext* cx);

// Returns true if there are any debugger microtasks pending in the queue.
JS_PUBLIC_API bool HasDebuggerMicroTasks(JSContext* cx);

// Returns true if there are any regular (non-debugger) microtasks pending in
// the queue.
JS_PUBLIC_API bool HasRegularMicroTasks(JSContext* cx);

// Returns the length of the regular microtask queue.
JS_PUBLIC_API size_t GetRegularMicroTaskCount(JSContext* cx);

// To handle cases where the queue needs to be set aside for some reason
// (mostly the Debugger API), we provide a Save and Restore API.
//
// When restoring the saved queue, the JSContext microtask queue must be
// empty -- you cannot drop items by restoring over a non-empty queue
// (so HasAnyMicroTasks must be false).
class SavedMicroTaskQueue {
 public:
  SavedMicroTaskQueue() = default;
  virtual ~SavedMicroTaskQueue() = default;
  SavedMicroTaskQueue(const SavedMicroTaskQueue&) = delete;
  SavedMicroTaskQueue& operator=(const SavedMicroTaskQueue&) = delete;
};

// This will return nullptr (and set OutOfMemory) if the save operation
// fails.
[[nodiscard]] JS_PUBLIC_API js::UniquePtr<SavedMicroTaskQueue>
SaveMicroTaskQueue(JSContext* cx);
JS_PUBLIC_API void RestoreMicroTaskQueue(
    JSContext* cx, js::UniquePtr<SavedMicroTaskQueue> savedQueue);

template <>
struct GCPolicy<JS::MicroTask> : public StructGCPolicy<JS::MicroTask> {
  static void trace(JSTracer* trc, JS::MicroTask* task, const char* name) {
    task->trace(trc, name);
  }
};

}  // namespace JS

namespace js {

template <typename Wrapper>
class WrappedPtrOperations<JS::MicroTask, Wrapper> {
  const JS::MicroTask& task() const {
    return static_cast<const Wrapper*>(this)->get();
  }

 public:
  bool isJS() const { return task().isJS(); }
  bool isEmbedder() const { return task().isEmbedder(); }
  const JS::Value& embedderValue() const { return task().embedderValue(); }
  inline JS::JSMicroTaskRef asJS() const;
};

template <typename Wrapper>
inline JS::JSMicroTaskRef WrappedPtrOperations<JS::MicroTask, Wrapper>::asJS()
    const {
  return task().asJS();
}

}  // namespace js

#endif /* js_friend_MicroTask_h */
