/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "vm/AsyncIteration.h"

#include "builtin/Promise.h"  // js::PromiseHandler, js::CreatePromiseObjectForAsyncGenerator, js::AsyncFromSyncIteratorMethod, js::ResolvePromiseInternal, js::RejectPromiseInternal, js::InternalAsyncGeneratorAwait
#include "js/friend/ErrorMessages.h"  // js::GetErrorMessage, JSMSG_*
#include "js/PropertySpec.h"
#include "vm/CompletionKind.h"
#include "vm/FunctionFlags.h"  // js::FunctionFlags
#include "vm/GeneratorObject.h"
#include "vm/GlobalObject.h"
#include "vm/Interpreter.h"
#include "vm/PlainObject.h"    // js::PlainObject
#include "vm/PromiseObject.h"  // js::PromiseObject
#include "vm/Realm.h"
#include "vm/SelfHosting.h"

#include "vm/JSObject-inl.h"
#include "vm/List-inl.h"

using namespace js;

// ---------------
// Async generator
// ---------------

const JSClass AsyncGeneratorObject::class_ = {
    "AsyncGenerator",
    JSCLASS_HAS_RESERVED_SLOTS(AsyncGeneratorObject::Slots),
    &classOps_,
};

const JSClassOps AsyncGeneratorObject::classOps_ = {
    .trace = CallTraceMethod<AbstractGeneratorObject>,
};

// ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
//
// OrdinaryCreateFromConstructor ( constructor, intrinsicDefaultProto
//                                 [ , internalSlotsList ] )
// https://tc39.es/ecma262/#sec-ordinarycreatefromconstructor
//
// specialized for AsyncGeneratorObjects.
static AsyncGeneratorObject* OrdinaryCreateFromConstructorAsyncGen(
    JSContext* cx, HandleFunction constructor) {
  // Step 1. Assert: intrinsicDefaultProto is this specification's name of an
  //         intrinsic object. The corresponding object must be an intrinsic
  //         that is intended to be used as the [[Prototype]] value of an
  //         object.
  // (implicit)

  // Step 2. Let proto be
  //         ? GetPrototypeFromConstructor(constructor, intrinsicDefaultProto).
  // Note: this step is skipped for async iterator helpers, which are specified
  // as abstract closures, not JS functions, so they always use the default
  // prototype instead of reading it from the constructor:
  // https://arai-a.github.io/ecma262-compare/?rev=5f4be6e41ce9f6e1997f5e1e42af253145c6c885&id=sec-createasynciteratorfromclosure
  RootedValue protoVal(cx);
  if (!constructor->isSelfHostedBuiltin() &&
      !GetProperty(cx, constructor, constructor, cx->names().prototype,
                   &protoVal)) {
    return nullptr;
  }

  RootedObject proto(cx, protoVal.isObject() ? &protoVal.toObject() : nullptr);
  if (!proto) {
    proto = GlobalObject::getOrCreateAsyncGeneratorPrototype(cx, cx->global());
    if (!proto) {
      return nullptr;
    }
  }

  // Step 3. If internalSlotsList is present, let slotsList be
  //         internalSlotsList.
  // Step 4. Else, let slotsList be a new empty List.
  // Step 5. Return OrdinaryObjectCreate(proto, slotsList).
  return NewObjectWithGivenProto<AsyncGeneratorObject>(cx, proto);
}

// ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
//
// EvaluateAsyncGeneratorBody
// https://tc39.es/ecma262/#sec-runtime-semantics-evaluateasyncgeneratorbody
//
// Steps 4-5.
//
// AsyncGeneratorStart ( generator, generatorBody )
// https://tc39.es/ecma262/#sec-asyncgeneratorstart
//
// Steps 1, 7.
/* static */
AsyncGeneratorObject* AsyncGeneratorObject::create(JSContext* cx,
                                                   HandleFunction asyncGen) {
  MOZ_ASSERT(asyncGen->isAsync() && asyncGen->isGenerator());

  AsyncGeneratorObject* generator =
      OrdinaryCreateFromConstructorAsyncGen(cx, asyncGen);
  if (!generator) {
    return nullptr;
  }

  // EvaluateAsyncGeneratorBody
  // Step 4. Set generator.[[AsyncGeneratorState]] to suspended-start.
  generator->setSuspendedStart();

  // Step 5. Perform AsyncGeneratorStart(generator, FunctionBody).

  // AsyncGeneratorStart
  // Step 1. Assert: generator.[[AsyncGeneratorState]] is suspended-start.

  // Step 7. Set generator.[[AsyncGeneratorQueue]] to a new empty List.
  generator->clearSingleQueueRequest();

  generator->clearCachedRequest();

  return generator;
}

/* static */
AsyncGeneratorRequest* AsyncGeneratorObject::createRequest(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator,
    CompletionKind completionKind, HandleValue completionValue,
    Handle<PromiseObject*> promise) {
  if (!generator->hasCachedRequest()) {
    return AsyncGeneratorRequest::create(cx, completionKind, completionValue,
                                         promise);
  }

  AsyncGeneratorRequest* request = generator->takeCachedRequest();
  request->init(completionKind, completionValue, promise);
  return request;
}

/* static */ [[nodiscard]] bool AsyncGeneratorObject::enqueueRequest(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator,
    Handle<AsyncGeneratorRequest*> request) {
  if (generator->isSingleQueue()) {
    if (generator->isSingleQueueEmpty()) {
      generator->setSingleQueueRequest(request);
      return true;
    }

    ListObject* queue = ListObject::create(cx);
    if (!queue) {
      return false;
    }

    if (!queue->append(cx, ObjectValue(*generator->singleQueueRequest()),
                       ObjectValue(*request))) {
      return false;
    }

    generator->setQueue(queue);
    return true;
  }

  return generator->queue()->append(cx, ObjectValue(*request));
}

/* static */
AsyncGeneratorRequest* AsyncGeneratorObject::dequeueRequest(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator) {
  if (generator->isSingleQueue()) {
    AsyncGeneratorRequest* request = generator->singleQueueRequest();
    generator->clearSingleQueueRequest();
    return request;
  }

  Rooted<ListObject*> queue(cx, generator->queue());
  return &queue->popFirstAs<AsyncGeneratorRequest>(cx);
}

/* static */
AsyncGeneratorRequest* AsyncGeneratorObject::peekRequest(
    Handle<AsyncGeneratorObject*> generator) {
  if (generator->isSingleQueue()) {
    return generator->singleQueueRequest();
  }

  return &generator->queue()->getAs<AsyncGeneratorRequest>(0);
}

const JSClass AsyncGeneratorRequest::class_ = {
    "AsyncGeneratorRequest",
    JSCLASS_HAS_RESERVED_SLOTS(AsyncGeneratorRequest::Slots),
};

// ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
//
// AsyncGeneratorRequest Records
// https://tc39.es/ecma262/#sec-asyncgeneratorrequest-records
/* static */
AsyncGeneratorRequest* AsyncGeneratorRequest::create(
    JSContext* cx, CompletionKind completionKind, HandleValue completionValue,
    Handle<PromiseObject*> promise) {
  AsyncGeneratorRequest* request =
      NewObjectWithGivenProto<AsyncGeneratorRequest>(cx, nullptr);
  if (!request) {
    return nullptr;
  }

  request->init(completionKind, completionValue, promise);
  return request;
}

[[nodiscard]] static bool AsyncGeneratorResume(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator,
    CompletionKind completionKind, HandleValue argument);

[[nodiscard]] static bool AsyncGeneratorDrainQueue(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator);

[[nodiscard]] static bool AsyncGeneratorCompleteStepNormal(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator, HandleValue value,
    bool done);

[[nodiscard]] static bool AsyncGeneratorCompleteStepThrow(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator,
    HandleValue exception);

// ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
//
// AsyncGeneratorStart ( generator, generatorBody )
// https://tc39.es/ecma262/#sec-asyncgeneratorstart
//
// Steps 4.g-l. "return" case.
[[nodiscard]] static bool AsyncGeneratorReturned(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator, HandleValue value) {
  // Step 4.g. Set acGenerator.[[AsyncGeneratorState]] to draining-queue.
  generator->setDrainingQueue();

  // Step 4.i. If result is a return completion, set result to
  //           NormalCompletion(result.[[Value]]).
  // (implicit)

  // Step 4.j. Perform AsyncGeneratorCompleteStep(acGenerator, result, true).
  if (!AsyncGeneratorCompleteStepNormal(cx, generator, value, true)) {
    return false;
  }

  MOZ_ASSERT(!generator->isExecuting());
  MOZ_ASSERT(!generator->isExecuting_AwaitingYieldReturn());
  if (generator->isDrainingQueue_AwaitingReturn()) {
    return true;
  }

  // Step 4.k. Perform AsyncGeneratorDrainQueue(acGenerator).
  // Step 4.l. Return undefined.
  return AsyncGeneratorDrainQueue(cx, generator);
}

// ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
//
// AsyncGeneratorStart ( generator, generatorBody )
// https://tc39.es/ecma262/#sec-asyncgeneratorstart
//
// Steps 4.g-l. "throw" case.
[[nodiscard]] static bool AsyncGeneratorThrown(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator) {
  // Step 4.g. Set acGenerator.[[AsyncGeneratorState]] to draining-queue.
  generator->setDrainingQueue();

  // Not much we can do about uncatchable exceptions, so just bail.
  if (!cx->isExceptionPending()) {
    return false;
  }

  // Step 4.j. Perform AsyncGeneratorCompleteStep(acGenerator, result, true).
  RootedValue value(cx);
  if (!GetAndClearException(cx, &value)) {
    return false;
  }
  if (!AsyncGeneratorCompleteStepThrow(cx, generator, value)) {
    return false;
  }

  MOZ_ASSERT(!generator->isExecuting());
  MOZ_ASSERT(!generator->isExecuting_AwaitingYieldReturn());
  if (generator->isDrainingQueue_AwaitingReturn()) {
    return true;
  }

  // Step 4.k. Perform AsyncGeneratorDrainQueue(acGenerator).
  // Step 4.l. Return undefined.
  return AsyncGeneratorDrainQueue(cx, generator);
}

// ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
//
// AsyncGeneratorUnwrapYieldResumption ( resumptionValue )
// https://tc39.es/ecma262/#sec-asyncgeneratorunwrapyieldresumption
//
// Steps 4-5.
[[nodiscard]] static bool AsyncGeneratorYieldReturnAwaitedFulfilled(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator, HandleValue value) {
  MOZ_ASSERT(generator->isExecuting_AwaitingYieldReturn(),
             "YieldReturn-Await fulfilled when not in "
             "'AwaitingYieldReturn' state");

  // Step 4. Assert: awaited is a normal completion.
  // Step 5. Return ReturnCompletion(awaited.[[Value]]).
  return AsyncGeneratorResume(cx, generator, CompletionKind::Return, value);
}

// ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
//
// AsyncGeneratorUnwrapYieldResumption ( resumptionValue )
// https://tc39.es/ecma262/#sec-asyncgeneratorunwrapyieldresumption
//
// Step 3.
[[nodiscard]] static bool AsyncGeneratorYieldReturnAwaitedRejected(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator,
    HandleValue reason) {
  MOZ_ASSERT(
      generator->isExecuting_AwaitingYieldReturn(),
      "YieldReturn-Await rejected when not in 'AwaitingYieldReturn' state");

  // Step 3. If awaited is a throw completion, return ? awaited.
  return AsyncGeneratorResume(cx, generator, CompletionKind::Throw, reason);
}

// ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
//
// AsyncGeneratorUnwrapYieldResumption ( resumptionValue )
// https://tc39.es/ecma262/#sec-asyncgeneratorunwrapyieldresumption
//
// Step 2.
[[nodiscard]] static bool AsyncGeneratorUnwrapYieldResumptionWithReturn(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator,
    JS::Handle<JS::Value> value) {
  // Step 2. Let awaited be Completion(Await(resumptionValue.[[Value]])).
  //
  // NOTE: Given that Await needs to be performed asynchronously,
  //       we use an implementation-defined state "AwaitingYieldReturn"
  //       to wait for the result.
  generator->setExecuting_AwaitingYieldReturn();

  return InternalAsyncGeneratorAwait(
      cx, generator, value,
      PromiseHandler::AsyncGeneratorYieldReturnAwaitedFulfilled,
      PromiseHandler::AsyncGeneratorYieldReturnAwaitedRejected);
}

// ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
//
// AsyncGeneratorYield ( value )
// https://tc39.es/ecma262/#sec-asyncgeneratoryield
//
// Stesp 9-12.
//
// AsyncGeneratorUnwrapYieldResumption ( resumptionValue )
// https://tc39.es/ecma262/#sec-asyncgeneratorunwrapyieldresumption
//
// Step 1.
[[nodiscard]] static bool AsyncGeneratorYield(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator, HandleValue value,
    bool* resumeAgain, CompletionKind* resumeCompletionKind,
    JS::MutableHandle<JS::Value> resumeValue) {
  *resumeAgain = false;

  // Step 9. Perform
  //         ! AsyncGeneratorCompleteStep(generator, completion, false,
  //                                      previousRealm).
  if (!AsyncGeneratorCompleteStepNormal(cx, generator, value, false)) {
    return false;
  }

  MOZ_ASSERT(!generator->isExecuting_AwaitingYieldReturn());
  // NOTE: This transition doesn't basically happen, but could happen if
  //       Debugger API is used, or the job queue is forcibly drained.
  if (generator->isDrainingQueue_AwaitingReturn()) {
    return true;
  }

  // Step 10. Let queue be generator.[[AsyncGeneratorQueue]].
  // Step 11. If queue is not empty, then
  if (!generator->isQueueEmpty()) {
    // Step 11.a. NOTE: Execution continues without suspending the generator.
    // Step 11.b. Let toYield be the first element of queue.
    Rooted<AsyncGeneratorRequest*> toYield(
        cx, AsyncGeneratorObject::peekRequest(generator));
    if (!toYield) {
      return false;
    }

    CompletionKind completionKind = toYield->completionKind();

    // Step 11.c. Let resumptionValue be Completion(toYield.[[Completion]]).
    RootedValue completionValue(cx, toYield->completionValue());

    // Step 11.d. Return ?
    //            AsyncGeneratorUnwrapYieldResumption(resumptionValue).
    //
    // AsyncGeneratorUnwrapYieldResumption
    // Step 1. If resumptionValue is not a return completion, return ?
    //         resumptionValue.
    if (completionKind != CompletionKind::Return) {
      *resumeAgain = true;
      *resumeCompletionKind = completionKind;
      resumeValue.set(completionValue);
      return true;
    }

    // Step 2.
    return AsyncGeneratorUnwrapYieldResumptionWithReturn(cx, generator,
                                                         completionValue);
  }

  // Step 12. Else,
  // Step 12.a. Set generator.[[AsyncGeneratorState]] to suspended-yield.
  generator->setSuspendedYield();

  // Step 12.b. Remove genContext from the execution context stack and
  //            restore the execution context that is at the top of the
  //            execution context stack as the running execution context.
  // Step 12.c. Let callerContext be the running execution context.
  // Step 12.d. Resume callerContext passing undefined. If genContext is ever
  //            resumed again, let resumptionValue be the Completion Record with
  //            which it is resumed.
  // (done as part of bytecode)

  // Step 12.e. Assert: If control reaches here, then genContext is the
  //            running execution context again.
  // Step 12.f. Return ?
  //            AsyncGeneratorUnwrapYieldResumption(resumptionValue).
  // (done in AsyncGeneratorResume on the next resume)

  return true;
}

// Performs AsyncGeneratorYield. If the generator has to be resumed, because the
// queue isn't empty or because AsyncGeneratorYield failed with a catchable
// exception, sets |*resumeAgain| to true and stores the completion to resume
// with in |*completionKind| and |resumeValue|.
[[nodiscard]] static bool AsyncGeneratorYieldAndGetResumption(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator, HandleValue value,
    bool* resumeAgain, CompletionKind* completionKind,
    MutableHandleValue resumeValue) {
  if (AsyncGeneratorYield(cx, generator, value, resumeAgain, completionKind,
                          resumeValue)) {
    MOZ_ASSERT_IF(*resumeAgain, *completionKind != CompletionKind::Return);
    return true;
  }

  // Not much we can do about uncatchable exceptions, so just bail.
  if (!cx->isExceptionPending()) {
    return false;
  }
  // This can happen if PromiseResolve inside Await fails during
  // AsyncGeneratorUnwrapYieldResumption, which is performed only if the queue
  // is not empty.
  //
  // Per spec, the PromiseResolve failure happens before suspending the
  // generator. Given that implementation-wise we've already suspended the
  // generator, return a throw completion to resume the generator with.
  //
  // On the other hand, per spec, if the queue is empty, this generator
  // is getting suspended and there's no fallible operation.
  // However, we can hit OOM during it, for example in
  // AsyncGeneratorCompleteStep while creating the iterator result object.
  //
  // And in this case, there's no spec-compliant way to report it, either to
  // the generator itself or to the consumer of the generator. Treat it as an
  // error that happened as a part of microtask handling, and propagate it to
  // the microtask queue.
  if (generator->isQueueEmpty()) {
    return false;
  }
  if (!GetAndClearException(cx, resumeValue)) {
    return false;
  }
  *resumeAgain = true;
  *completionKind = CompletionKind::Throw;
  return true;
}

// Yield ( value )
// https://tc39.es/ecma262/#sec-yield
//
// Step 2. The Await(value) in AsyncGeneratorYield(? Await(value)) was
// fulfilled.
[[nodiscard]] static bool AsyncGeneratorYieldAwaitedFulfilled(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator, HandleValue value) {
  MOZ_ASSERT(generator->isExecuting(),
             "Yield-Await fulfilled when not in 'Executing' state");

  bool resumeAgain = false;
  CompletionKind completionKind = CompletionKind::Normal;
  RootedValue resumeValue(cx);
  if (!AsyncGeneratorYieldAndGetResumption(cx, generator, value, &resumeAgain,
                                           &completionKind, &resumeValue)) {
    return false;
  }
  if (!resumeAgain) {
    return true;
  }
  return AsyncGeneratorResume(cx, generator, completionKind, resumeValue);
}

// Yield ( value )
// https://tc39.es/ecma262/#sec-yield
//
// Step 2. The Await(value) in AsyncGeneratorYield(? Await(value)) was
// rejected. The generator is resumed with a throw completion at the yield.
[[nodiscard]] static bool AsyncGeneratorYieldAwaitedRejected(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator,
    HandleValue reason) {
  MOZ_ASSERT(generator->isExecuting(),
             "Yield-Await rejected when not in 'Executing' state");
  return AsyncGeneratorResume(cx, generator, CompletionKind::Throw, reason);
}

// Yield ( value )
// https://tc39.es/ecma262/#sec-yield
//
// Step 2. The Await(value) in AsyncGeneratorYield(? Await(value)).
[[nodiscard]] static bool AsyncGeneratorYieldAwait(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator, HandleValue value) {
  return InternalAsyncGeneratorAwait(
      cx, generator, value, PromiseHandler::AsyncGeneratorYieldAwaitedFulfilled,
      PromiseHandler::AsyncGeneratorYieldAwaitedRejected);
}

// ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
//
// Await in async function
// https://tc39.es/ecma262/#await
//
// Steps 3.c-f.
[[nodiscard]] static bool AsyncGeneratorAwaitedFulfilled(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator, HandleValue value) {
  MOZ_ASSERT(generator->isExecuting(),
             "Await fulfilled when not in 'Executing' state");

  // Step 3.c. Push asyncContext onto the execution context stack; asyncContext
  //           is now the running execution context.
  // Step 3.d. Resume the suspended evaluation of asyncContext using
  //           NormalCompletion(v) as the result of the operation that
  //           suspended it.
  // Step 3.f. Return undefined.
  return AsyncGeneratorResume(cx, generator, CompletionKind::Normal, value);
}

// ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
//
// Await in async function
// https://tc39.es/ecma262/#await
//
// Steps 5.c-f.
[[nodiscard]] static bool AsyncGeneratorAwaitedRejected(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator,
    HandleValue reason) {
  MOZ_ASSERT(generator->isExecuting(),
             "Await rejected when not in 'Executing' state");

  // Step 5.c. Push asyncContext onto the execution context stack; asyncContext
  //           is now the running execution context.
  // Step 5.d. Resume the suspended evaluation of asyncContext using
  //           ThrowCompletion(reason) as the result of the operation that
  //           suspended it.
  // Step 5.f. Return undefined.
  return AsyncGeneratorResume(cx, generator, CompletionKind::Throw, reason);
}

// ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
//
// Await in async function
// https://tc39.es/ecma262/#await
[[nodiscard]] static bool AsyncGeneratorAwait(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator, HandleValue value) {
  return InternalAsyncGeneratorAwait(
      cx, generator, value, PromiseHandler::AsyncGeneratorAwaitedFulfilled,
      PromiseHandler::AsyncGeneratorAwaitedRejected);
}

// ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
//
// AsyncGeneratorCompleteStep ( generator, completion, done [ , realm ] )
// https://tc39.es/ecma262/#sec-asyncgeneratorcompletestep
//
// "normal" case.
//
// Per spec, this function should never fail.
// Our implementation can fail due to OOM.
[[nodiscard]] static bool AsyncGeneratorCompleteStepNormal(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator, HandleValue value,
    bool done) {
  // Step 1. Assert: generator.[[AsyncGeneratorQueue]] is not empty.
  MOZ_ASSERT(!generator->isQueueEmpty());

  // Step 2. Let next be the first element of generator.[[AsyncGeneratorQueue]].
  // Step 3. Remove the first element from generator.[[AsyncGeneratorQueue]].
  AsyncGeneratorRequest* next =
      AsyncGeneratorObject::dequeueRequest(cx, generator);
  if (!next) {
    return false;
  }

  // Step 4. Let promiseCapability be next.[[Capability]].
  Rooted<PromiseObject*> resultPromise(cx, next->promise());

  generator->cacheRequest(next);

  // Step 5. Let value be completion.[[Value]].
  // (passed by caller)

  // Step 6. If completion is a throw completion, then
  // (done in AsyncGeneratorCompleteStepThrow)

  // Step 7. Else,
  // Step 7.a. Assert: completion is a normal completion.

  // Step 7.b. If realm is present, then
  // (skipped)

  // Step 7.c. Else,
  // Step 7.c.i. Let iteratorResult be CreateIteratorResultObject(value,
  //             done).
  JSObject* resultObj = CreateIterResultObject(cx, value, done);
  if (!resultObj) {
    return false;
  }

  // Step 7.d. Perform
  //           ! Call(promiseCapability.[[Resolve]], undefined,
  //                  « iteratorResult »).
  // Step 8. Return unused.
  RootedValue resultValue(cx, ObjectValue(*resultObj));
  return ResolvePromiseInternal(cx, resultPromise, resultValue);
}

// ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
//
// AsyncGeneratorCompleteStep ( generator, completion, done [ , realm ] )
// https://tc39.es/ecma262/#sec-asyncgeneratorcompletestep
//
// "throw" case.
//
// Per spec, this function should never fail.
// Our implementation can fail due to OOM.
[[nodiscard]] static bool AsyncGeneratorCompleteStepThrow(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator,
    HandleValue exception) {
  // Step 1. Assert: generator.[[AsyncGeneratorQueue]] is not empty.
  MOZ_ASSERT(!generator->isQueueEmpty());

  // Step 2. Let next be the first element of generator.[[AsyncGeneratorQueue]].
  // Step 3. Remove the first element from generator.[[AsyncGeneratorQueue]].
  AsyncGeneratorRequest* next =
      AsyncGeneratorObject::dequeueRequest(cx, generator);
  if (!next) {
    return false;
  }

  // Step 4. Let promiseCapability be next.[[Capability]].
  Rooted<PromiseObject*> resultPromise(cx, next->promise());

  generator->cacheRequest(next);

  // Step 5. Let value be completion.[[Value]].
  // (passed by caller)

  // Step 6. If completion is a throw completion, then
  // Step 6.a. Perform
  //           ! Call(promiseCapability.[[Reject]], undefined, « value »).
  // Step 8. Return unused.
  return RejectPromiseInternal(cx, resultPromise, exception);
}

// ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
//
// AsyncGeneratorAwaitReturn ( generator )
// https://tc39.es/ecma262/#sec-asyncgeneratorawaitreturn
//
// Steps 11.a-e.
[[nodiscard]] static bool AsyncGeneratorAwaitReturnFulfilled(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator, HandleValue value) {
  // Step 11.a. Assert: generator.[[AsyncGeneratorState]] is draining-queue.
  //
  // NOTE: We use the implementation-defined state DrainingQueue_AwaitingReturn
  //       for the Await during draining-queue, and it's set back to the
  //       original draining-queue when the await operation finishes.
  MOZ_ASSERT(generator->isDrainingQueue_AwaitingReturn());
  generator->setDrainingQueue();

  // Step 11.b. Let result be NormalCompletion(value).
  // Step 11.c. Perform AsyncGeneratorCompleteStep(generator, result, true).
  if (!AsyncGeneratorCompleteStepNormal(cx, generator, value, true)) {
    return false;
  }

  MOZ_ASSERT(!generator->isExecuting());
  MOZ_ASSERT(!generator->isExecuting_AwaitingYieldReturn());
  if (generator->isDrainingQueue_AwaitingReturn()) {
    return true;
  }

  // Step 11.d. Perform AsyncGeneratorDrainQueue(generator).
  // Step 11.e. Return undefined.
  return AsyncGeneratorDrainQueue(cx, generator);
}

// ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
//
// AsyncGeneratorAwaitReturn ( generator )
// https://tc39.es/ecma262/#sec-asyncgeneratorawaitreturn
//
// Steps 13.a-e.
[[nodiscard]] static bool AsyncGeneratorAwaitReturnRejected(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator, HandleValue value) {
  // Step 13.a. Assert: generator.[[AsyncGeneratorState]] is draining-queue.
  //
  // See the comment for AsyncGeneratorAwaitReturnFulfilled.
  MOZ_ASSERT(generator->isDrainingQueue_AwaitingReturn());
  generator->setDrainingQueue();

  // Step 13.b. Let result be ThrowCompletion(reason).
  // Step 13.c. Perform AsyncGeneratorCompleteStep(generator, result, true).
  if (!AsyncGeneratorCompleteStepThrow(cx, generator, value)) {
    return false;
  }

  MOZ_ASSERT(!generator->isExecuting());
  MOZ_ASSERT(!generator->isExecuting_AwaitingYieldReturn());
  if (generator->isDrainingQueue_AwaitingReturn()) {
    return true;
  }

  // Step 13.d. Perform AsyncGeneratorDrainQueue(generator).
  // Step 13.e. Return undefined.
  return AsyncGeneratorDrainQueue(cx, generator);
}

// ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
//
// AsyncGeneratorAwaitReturn ( generator )
// https://tc39.es/ecma262/#sec-asyncgeneratorawaitreturn
[[nodiscard]] static bool AsyncGeneratorAwaitReturn(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator, HandleValue next) {
  // Step 1. Assert: generator.[[AsyncGeneratorState]] is draining-queue.
  MOZ_ASSERT(generator->isDrainingQueue());
  generator->setDrainingQueue_AwaitingReturn();

  // Step 2. Let queue be generator.[[AsyncGeneratorQueue]].
  // Step 3. Assert: queue is not empty.
  MOZ_ASSERT(!generator->isQueueEmpty());

  // Step 4. Let next be the first element of queue.
  // (passed by caller)

  // Step 5. Let completion be Completion(next.[[Completion]]).
  // Step 6. Assert: completion is a return completion.
  // (implicit)

  // Step 7. Let promiseCompletion be Completion(PromiseResolve(%Promise%,
  //         completion.[[Value]])).

  // Step 9. Assert: promiseCompletion is a normal completion.
  // Step 10. Let promise be promiseCompletion.[[Value]].
  // Step 11. Let fulfilledClosure be a new Abstract Closure with parameters
  //          (value) that captures generator and performs the following steps
  //          when called:
  // Step 12. Let onFulfilled be CreateBuiltinFunction(fulfilledClosure, 1,
  //          "", « »).
  // Step 13. Let rejectedClosure be a new Abstract Closure with parameters
  //          (reason) that captures generator and performs the following steps
  //          when called:
  // Step 14. Let onRejected be CreateBuiltinFunction(rejectedClosure, 1, "",
  //          « »).
  // Step 15. Perform PerformPromiseThen(promise, onFulfilled, onRejected).
  // Step 16. Return unused.
  if (!InternalAsyncGeneratorAwait(
          cx, generator, next,
          PromiseHandler::AsyncGeneratorAwaitReturnFulfilled,
          PromiseHandler::AsyncGeneratorAwaitReturnRejected)) {
    // This branch can be taken with one of the following:
    //   * (a) abrupt completion in PromiseResolve at step 7, such as
    //         getting `compeltion.[[Value]].constructor` property throws
    //   * (b) OOM in PromiseResolve
    //   * (c) OOM in PerformPromiseThen
    //
    // (c) happens after step 8, but OOM is an implementation details and
    // we can treat the OOM as if it happened during PromiseResolve,
    // and thus performing the step 8 here is okay.
    //
    // Step 8. If promiseCompletion is an abrupt completion, then

    // Not much we can do about uncatchable exceptions, so just bail.
    if (!cx->isExceptionPending()) {
      return false;
    }

    RootedValue value(cx);
    if (!GetAndClearException(cx, &value)) {
      return false;
    }

    // Step 8.a. Perform AsyncGeneratorCompleteStep(generator,
    //           promiseCompletion, true).
    if (!AsyncGeneratorCompleteStepThrow(cx, generator, value)) {
      return false;
    }

    MOZ_ASSERT(!generator->isExecuting());
    MOZ_ASSERT(!generator->isExecuting_AwaitingYieldReturn());
    if (generator->isDrainingQueue_AwaitingReturn()) {
      return true;
    }

    // Step 8.b. Perform AsyncGeneratorDrainQueue(generator).
    // Step 8.c. Return unused.
    return AsyncGeneratorDrainQueue(cx, generator);
  }

  return true;
}

// ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
//
// AsyncGeneratorDrainQueue ( generator )
// https://tc39.es/ecma262/#sec-asyncgeneratordrainqueue
[[nodiscard]] static bool AsyncGeneratorDrainQueue(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator) {
  // Step 1. Assert: generator.[[AsyncGeneratorState]] is draining-queue.
  //
  // NOTE: DrainingQueue_AwaitingReturn shouldn't reach here.
  MOZ_ASSERT(generator->isDrainingQueue());

  // Step 2. Let queue be generator.[[AsyncGeneratorQueue]].
  // Step 3. Repeat, while queue is not empty,
  Rooted<AsyncGeneratorRequest*> next(cx);
  RootedValue value(cx);
  while (!generator->isQueueEmpty()) {
    // Step 3.a. Let next be the first element of queue.
    next = AsyncGeneratorObject::peekRequest(generator);
    if (!next) {
      return false;
    }

    // Step 3.b. Let completion be Completion(next.[[Completion]]).
    CompletionKind completionKind = next->completionKind();

    // Step 3.c. If completion is a return completion, then
    if (completionKind == CompletionKind::Return) {
      value = next->completionValue();

      // Step 3.c.i. Perform AsyncGeneratorAwaitReturn(generator).
      // Step 3.c.ii. Return unused.
      return AsyncGeneratorAwaitReturn(cx, generator, value);
    }

    // Step 3.d. Else,
    if (completionKind == CompletionKind::Throw) {
      value = next->completionValue();

      // Step 3.d.ii. Perform AsyncGeneratorCompleteStep(generator, completion,
      //              true).
      if (!AsyncGeneratorCompleteStepThrow(cx, generator, value)) {
        return false;
      }
    } else {
      // Step 3.d.i. If completion is a normal completion, then
      // Step 3.d.i.1. Set completion to NormalCompletion(undefined).
      // Step 3.d.ii. Perform AsyncGeneratorCompleteStep(generator, completion,
      //              true).
      if (!AsyncGeneratorCompleteStepNormal(cx, generator, UndefinedHandleValue,
                                            true)) {
        return false;
      }
    }

    MOZ_ASSERT(!generator->isExecuting());
    MOZ_ASSERT(!generator->isExecuting_AwaitingYieldReturn());
    if (generator->isDrainingQueue_AwaitingReturn()) {
      return true;
    }
  }

  // Step 4. Set generator.[[AsyncGeneratorState]] to completed.
  generator->setCompleted();

  // Step 5. Return unused.
  return true;
}

// ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
//
// AsyncGeneratorValidate ( generator, generatorBrand )
// https://tc39.es/ecma262/#sec-asyncgeneratorvalidate
//
// Testing part.
[[nodiscard]] static bool IsAsyncGeneratorValid(HandleValue asyncGenVal) {
  // Step 1. Perform
  //         ? RequireInternalSlot(generator, [[AsyncGeneratorContext]]).
  // Step 2. Perform
  //         ? RequireInternalSlot(generator, [[AsyncGeneratorState]]).
  // Step 3. Perform
  //         ? RequireInternalSlot(generator, [[AsyncGeneratorQueue]]).
  // Step 4. If generator.[[GeneratorBrand]] is not generatorBrand, throw a
  //         TypeError exception.
  return asyncGenVal.isObject() &&
         asyncGenVal.toObject().canUnwrapAs<AsyncGeneratorObject>();
}

// ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
//
// AsyncGeneratorValidate ( generator, generatorBrand )
// https://tc39.es/ecma262/#sec-asyncgeneratorvalidate
//
// Throwing part.
[[nodiscard]] static bool AsyncGeneratorValidateThrow(
    JSContext* cx, MutableHandleValue result) {
  Rooted<PromiseObject*> resultPromise(
      cx, CreatePromiseObjectForAsyncGenerator(cx));
  if (!resultPromise) {
    return false;
  }

  RootedValue badGeneratorError(cx);
  if (!GetTypeError(cx, JSMSG_NOT_AN_ASYNC_GENERATOR, &badGeneratorError)) {
    return false;
  }

  if (!RejectPromiseInternal(cx, resultPromise, badGeneratorError)) {
    return false;
  }

  result.setObject(*resultPromise);
  return true;
}

// ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
//
// AsyncGeneratorEnqueue ( generator, completion, promiseCapability )
// https://tc39.es/ecma262/#sec-asyncgeneratorenqueue
[[nodiscard]] static bool AsyncGeneratorEnqueue(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator,
    CompletionKind completionKind, HandleValue completionValue,
    Handle<PromiseObject*> resultPromise) {
  // Step 1. Let request be
  //         AsyncGeneratorRequest { [[Completion]]: completion,
  //                                 [[Capability]]: promiseCapability }.
  Rooted<AsyncGeneratorRequest*> request(
      cx, AsyncGeneratorObject::createRequest(cx, generator, completionKind,
                                              completionValue, resultPromise));
  if (!request) {
    return false;
  }

  // Step 2. Append request to generator.[[AsyncGeneratorQueue]].
  // Step 3. Return unused.
  return AsyncGeneratorObject::enqueueRequest(cx, generator, request);
}

class MOZ_STACK_CLASS MaybeEnterAsyncGeneratorRealm {
  mozilla::Maybe<AutoRealm> ar_;

 public:
  MaybeEnterAsyncGeneratorRealm() = default;
  ~MaybeEnterAsyncGeneratorRealm() = default;

  // Enter async generator's realm, and wrap the method's argument value if
  // necessary.
  [[nodiscard]] bool maybeEnterAndWrap(JSContext* cx,
                                       Handle<AsyncGeneratorObject*> generator,
                                       MutableHandleValue value) {
    if (generator->compartment() == cx->compartment()) {
      return true;
    }

    ar_.emplace(cx, generator);
    return cx->compartment()->wrap(cx, value);
  }

  // Leave async generator's realm, and wrap the method's result value if
  // necessary.
  [[nodiscard]] bool maybeLeaveAndWrap(JSContext* cx,
                                       MutableHandleValue result) {
    if (!ar_) {
      return true;
    }
    ar_.reset();

    return cx->compartment()->wrap(cx, result);
  }
};

[[nodiscard]] static bool AsyncGeneratorMethodSanityCheck(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator) {
  if (generator->isSuspendedStart() || generator->isSuspendedYield() ||
      generator->isCompleted()) {
    // The spec assumes the queue is empty when async generator methods are
    // called with those state, but our debugger allows calling those methods
    // in unexpected state, such as before suspendedStart.
    if (MOZ_UNLIKELY(!generator->isQueueEmpty())) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_SUSPENDED_QUEUE_NOT_EMPTY);
      return false;
    }
  }

  return true;
}

// ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
//
// %AsyncGeneratorPrototype%.next ( value )
// https://tc39.es/ecma262/#sec-asyncgenerator-prototype-next
bool js::AsyncGeneratorNext(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Step 3. Let result be Completion(AsyncGeneratorValidate(generator, empty)).
  // Step 4. IfAbruptRejectPromise(result, promiseCapability).
  // (reordered)
  if (!IsAsyncGeneratorValid(args.thisv())) {
    return AsyncGeneratorValidateThrow(cx, args.rval());
  }

  // Step 1. Let generator be the this value.
  // (implicit)
  Rooted<AsyncGeneratorObject*> generator(
      cx, &args.thisv().toObject().unwrapAs<AsyncGeneratorObject>());

  MaybeEnterAsyncGeneratorRealm maybeEnterRealm;

  RootedValue completionValue(cx, args.get(0));
  if (!maybeEnterRealm.maybeEnterAndWrap(cx, generator, &completionValue)) {
    return false;
  }

  // Step 2. Let promiseCapability be ! NewPromiseCapability(%Promise%).
  Rooted<PromiseObject*> resultPromise(
      cx, CreatePromiseObjectForAsyncGenerator(cx));
  if (!resultPromise) {
    return false;
  }

  if (!AsyncGeneratorMethodSanityCheck(cx, generator)) {
    return false;
  }

  // Step 5. Let state be generator.[[AsyncGeneratorState]].
  // Step 6. If state is completed, then
  if (generator->isCompleted()) {
    MOZ_ASSERT(generator->isQueueEmpty());

    // Step 6.a. Let iteratorResult be CreateIteratorResultObject(undefined,
    //           true).
    JSObject* resultObj =
        CreateIterResultObject(cx, UndefinedHandleValue, true);
    if (!resultObj) {
      return false;
    }

    // Step 6.b. Perform ! Call(promiseCapability.[[Resolve]], undefined, «
    //           iteratorResult »).
    RootedValue resultValue(cx, ObjectValue(*resultObj));
    if (!ResolvePromiseInternal(cx, resultPromise, resultValue)) {
      return false;
    }
  } else {
    // Step 7. Let completion be NormalCompletion(value).
    // Step 8. Perform AsyncGeneratorEnqueue(generator, completion,
    //         promiseCapability).
    if (!AsyncGeneratorEnqueue(cx, generator, CompletionKind::Normal,
                               completionValue, resultPromise)) {
      return false;
    }

    // Step 9. If state is either suspended-start or suspended-yield, then
    if (generator->isSuspendedStart() || generator->isSuspendedYield()) {
      MOZ_ASSERT(generator->isQueueLengthOne());

      // Step 9.a. Perform AsyncGeneratorResume(generator, completion).
      if (!AsyncGeneratorResume(cx, generator, CompletionKind::Normal,
                                completionValue)) {
        return false;
      }
    } else {
      // Step 10. Else,
      // Step 10.a. Assert: state is either executing or draining-queue.
      MOZ_ASSERT(generator->isExecuting() ||
                 generator->isExecuting_AwaitingYieldReturn() ||
                 generator->isDrainingQueue() ||
                 generator->isDrainingQueue_AwaitingReturn());
    }
  }

  // Step 6.c. Return promiseCapability.[[Promise]].
  // and
  // Step 11. Return promiseCapability.[[Promise]].
  args.rval().setObject(*resultPromise);

  return maybeEnterRealm.maybeLeaveAndWrap(cx, args.rval());
}

// ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
//
// %AsyncGeneratorPrototype%.return ( value )
// https://tc39.es/ecma262/#sec-asyncgenerator-prototype-return
bool js::AsyncGeneratorReturn(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Step 3. Let result be Completion(AsyncGeneratorValidate(generator, empty)).
  // Step 4. IfAbruptRejectPromise(result, promiseCapability).
  // (reordered)
  if (!IsAsyncGeneratorValid(args.thisv())) {
    return AsyncGeneratorValidateThrow(cx, args.rval());
  }

  // Step 1. Let generator be the this value.
  Rooted<AsyncGeneratorObject*> generator(
      cx, &args.thisv().toObject().unwrapAs<AsyncGeneratorObject>());

  MaybeEnterAsyncGeneratorRealm maybeEnterRealm;

  RootedValue completionValue(cx, args.get(0));
  if (!maybeEnterRealm.maybeEnterAndWrap(cx, generator, &completionValue)) {
    return false;
  }

  // Step 2. Let promiseCapability be ! NewPromiseCapability(%Promise%).
  Rooted<PromiseObject*> resultPromise(
      cx, CreatePromiseObjectForAsyncGenerator(cx));
  if (!resultPromise) {
    return false;
  }

  if (!AsyncGeneratorMethodSanityCheck(cx, generator)) {
    return false;
  }

  // Step 5. Let completion be ReturnCompletion(value).
  // Step 6. Perform AsyncGeneratorEnqueue(generator, completion,
  //         promiseCapability).
  if (!AsyncGeneratorEnqueue(cx, generator, CompletionKind::Return,
                             completionValue, resultPromise)) {
    return false;
  }

  // Step 7. Let state be generator.[[AsyncGeneratorState]].
  // Step 8. If state is either suspended-start or completed, then
  if (generator->isSuspendedStart() || generator->isCompleted()) {
    MOZ_ASSERT(generator->isQueueLengthOne());

    // Step 8.a. Set generator.[[AsyncGeneratorState]] to draining-queue.
    generator->setDrainingQueue();

    // Step 8.b. Perform AsyncGeneratorAwaitReturn(generator).
    if (!AsyncGeneratorAwaitReturn(cx, generator, completionValue)) {
      return false;
    }
  } else if (generator->isSuspendedYield()) {
    // Step 9. Else if state is suspended-yield, then
    MOZ_ASSERT(generator->isQueueLengthOne());

    // Step 9.a. Perform AsyncGeneratorResume(generator, completion).
    //
    // https://tc39.es/ecma262/#sec-asyncgeneratorresume
    // AsyncGeneratorResume ( generator, completion )
    //
    // Step 7. Resume the suspended evaluation of genContext using completion as
    //         the result of the operation that suspended it. Let result be the
    //         Completion Record returned by the resumed computation.
    // Step 10. Return unused.
    //
    // AsyncGeneratorYield ( value )
    // https://tc39.es/ecma262/#sec-asyncgeneratoryield
    //
    // Step 12.d. Resume callerContext passing undefined. If genContext is ever
    //            resumed again, let resumptionValue be the Completion Record
    //            with which it is resumed.
    // Step 12.e. Assert: If control reaches here, then genContext is the
    //            running execution context again.
    // Step 12.f. Return ?
    //            AsyncGeneratorUnwrapYieldResumption(resumptionValue).
    //
    if (!AsyncGeneratorUnwrapYieldResumptionWithReturn(cx, generator,
                                                       completionValue)) {
      // The failure path here is for the Await inside
      // AsyncGeneratorUnwrapYieldResumption, where a corrupted Promise is
      // passed and called there.
      //
      // Per spec, the operation should be performed after resuming the
      // generator, but given that we're performing the Await before
      // resuming the generator, we need to handle the special throw completion
      // here.

      // For uncatchable exception, there's nothing we can do.
      if (!cx->isExceptionPending()) {
        return false;
      }

      // Resume the generator with throw completion, so that it behaves in the
      // same way as the Await throws.
      RootedValue exception(cx);
      if (!GetAndClearException(cx, &exception)) {
        return false;
      }
      if (!AsyncGeneratorResume(cx, generator, CompletionKind::Throw,
                                exception)) {
        return false;
      }
    }
  } else {
    // Step 10. Else,
    // Step 10.a. Assert: state is either executing or draining-queue.
    MOZ_ASSERT(generator->isExecuting() ||
               generator->isExecuting_AwaitingYieldReturn() ||
               generator->isDrainingQueue() ||
               generator->isDrainingQueue_AwaitingReturn());
  }

  // Step 11. Return promiseCapability.[[Promise]].
  args.rval().setObject(*resultPromise);

  return maybeEnterRealm.maybeLeaveAndWrap(cx, args.rval());
}

// ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
//
// %AsyncGeneratorPrototype%.throw ( exception )
// https://tc39.es/ecma262/#sec-asyncgenerator-prototype-throw
bool js::AsyncGeneratorThrow(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Step 3. Let result be Completion(AsyncGeneratorValidate(generator, empty)).
  // Step 4. IfAbruptRejectPromise(result, promiseCapability).
  // (reordered)
  if (!IsAsyncGeneratorValid(args.thisv())) {
    return AsyncGeneratorValidateThrow(cx, args.rval());
  }

  // Step 1. Let generator be the this value.
  Rooted<AsyncGeneratorObject*> generator(
      cx, &args.thisv().toObject().unwrapAs<AsyncGeneratorObject>());

  MaybeEnterAsyncGeneratorRealm maybeEnterRealm;

  RootedValue completionValue(cx, args.get(0));
  if (!maybeEnterRealm.maybeEnterAndWrap(cx, generator, &completionValue)) {
    return false;
  }

  // Step 2. Let promiseCapability be ! NewPromiseCapability(%Promise%).
  Rooted<PromiseObject*> resultPromise(
      cx, CreatePromiseObjectForAsyncGenerator(cx));
  if (!resultPromise) {
    return false;
  }

  if (!AsyncGeneratorMethodSanityCheck(cx, generator)) {
    return false;
  }

  // Step 5. Let state be generator.[[AsyncGeneratorState]].
  // Step 6. If state is suspended-start, then
  if (generator->isSuspendedStart()) {
    // Step 6.a. Set generator.[[AsyncGeneratorState]] to completed.
    // Step 6.b. Set state to completed.
    generator->setCompleted();
  }

  // Step 7. If state is completed, then
  if (generator->isCompleted()) {
    MOZ_ASSERT(generator->isQueueEmpty());

    // Step 7.a. Perform ! Call(promiseCapability.[[Reject]], undefined, «
    //           exception »).
    if (!RejectPromiseInternal(cx, resultPromise, completionValue)) {
      return false;
    }
  } else {
    // Step 8. Let completion be ThrowCompletion(exception).
    // Step 9. Perform AsyncGeneratorEnqueue(generator, completion,
    //         promiseCapability).
    if (!AsyncGeneratorEnqueue(cx, generator, CompletionKind::Throw,
                               completionValue, resultPromise)) {
      return false;
    }

    // Step 10. If state is suspended-yield, then
    if (generator->isSuspendedYield()) {
      MOZ_ASSERT(generator->isQueueLengthOne());

      // Step 10.a. Perform AsyncGeneratorResume(generator, completion).
      if (!AsyncGeneratorResume(cx, generator, CompletionKind::Throw,
                                completionValue)) {
        return false;
      }
    } else {
      // Step 11. Else,
      // Step 11.a. Assert: state is either executing or draining-queue.
      MOZ_ASSERT(generator->isExecuting() ||
                 generator->isExecuting_AwaitingYieldReturn() ||
                 generator->isDrainingQueue() ||
                 generator->isDrainingQueue_AwaitingReturn());
    }
  }

  // Step 7.b. Return promiseCapability.[[Promise]].
  // and
  // Step 12. Return promiseCapability.[[Promise]].
  args.rval().setObject(*resultPromise);

  return maybeEnterRealm.maybeLeaveAndWrap(cx, args.rval());
}

// ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
//
// AsyncGeneratorResume ( generator, completion )
// https://tc39.es/ecma262/#sec-asyncgeneratorresume
//
// This returns false only for internal errors.
[[nodiscard]] static bool AsyncGeneratorResume(
    JSContext* cx, Handle<AsyncGeneratorObject*> generator,
    CompletionKind completionKind, HandleValue argument) {
  // Given that yield can resume again, we implement it as a loop.
  JS::Rooted<JS::Value> resumeArgument(cx, argument);
  while (true) {
    MOZ_ASSERT(!generator->isClosed(),
               "closed generator when resuming async generator");
    MOZ_ASSERT(generator->isSuspended(),
               "non-suspended generator when resuming async generator");

    // Step 1. Assert: generator.[[AsyncGeneratorState]] is either
    //         suspended-start or suspended-yield.
    //
    // NOTE: We're using suspend/resume also for await. and the state can be
    //       anything.

    // Step 2. Let genContext be generator.[[AsyncGeneratorContext]].
    // Step 3. Let callerContext be the running execution context.
    // Step 4. Suspend callerContext.
    // (handled in generator)

    // Step 5. Set generator.[[AsyncGeneratorState]] to executing.
    generator->setExecuting();

    // Step 6. Push genContext onto the execution context stack; genContext is
    //         now the running execution context.
    // Step 7. Resume the suspended evaluation of genContext using completion as
    //         the result of the operation that suspended it. Let result be the
    //         Completion Record returned by the resumed computation.
    // Step 8. Assert: result is never an abrupt completion.
    // Step 9. Assert: When we return here, genContext has already been removed
    //         from the execution context stack and callerContext is the
    //         currently running execution context.
    // Step 10. Return unused.
    //
    // NOTE: Once the CallSelfHostedFunction call returns, the generator itslef
    //       is suspended implementation-wise.
    //       On the other hand, spec-wise, the generator may still be running
    //       and some operations (a part of Await or Yield, etc) are supposed
    //       to be handled with the genContext.
    //       when the execution continues, we resume the generator with
    //       the corresponding completion value.
    GeneratorResumeKind resumeKind =
        completionKind == CompletionKind::Normal  ? GeneratorResumeKind::Next
        : completionKind == CompletionKind::Throw ? GeneratorResumeKind::Throw
                                                  : GeneratorResumeKind::Return;
    RootedValue thisOrRval(cx, ObjectValue(*generator));

    bool resumeOk;
    {
      AutoRealm ar(cx, generator);
      resumeOk = ResumeGenerator(cx, generator, resumeArgument, resumeKind,
                                 &thisOrRval);
    }
    if (!resumeOk) {
      if (!generator->isClosed()) {
        generator->setClosed(cx);
      }
      return AsyncGeneratorThrown(cx, generator);
    }

    if (generator->isClosed()) {
      return AsyncGeneratorReturned(cx, generator, thisOrRval);
    }

    JSOp suspendOp = generator->suspendOp();

    bool needsAwait = suspendOp == JSOp::Await;
    if (suspendOp == JSOp::AwaitThenYield) {
      // If the await can be skipped, fall through to the yield below. Otherwise
      // AsyncGeneratorYieldAwaitedFulfilled performs the yield.
      bool canSkip;
      if (!CanSkipAwaitAfterSuspend(cx, thisOrRval, &canSkip)) {
        return false;
      }
      if (canSkip) {
        if (!ExtractAwaitValueAfterSuspend(cx, thisOrRval, &thisOrRval)) {
          return false;
        }
      } else {
        needsAwait = true;
      }
    }

    if (needsAwait) {
      bool awaitOk = suspendOp == JSOp::Await
                         ? AsyncGeneratorAwait(cx, generator, thisOrRval)
                         : AsyncGeneratorYieldAwait(cx, generator, thisOrRval);
      if (!awaitOk) {
        // Not much we can do about uncatchable exceptions, so just bail.
        if (!cx->isExceptionPending()) {
          return false;
        }
        // This can happen if PromiseResolve inside Await fails.
        //
        // Per spec, that happens without suspending the generator.
        // Given that implementation-wise we've already suspended the generator,
        // resume it with a throw completion.
        //
        // Other OOM can also hit this path, but that should also be
        // treated as an error before suspending the generator.
        if (!GetAndClearException(cx, &resumeArgument)) {
          return false;
        }
        completionKind = CompletionKind::Throw;
        continue;
      }
      return true;
    }

    MOZ_ASSERT(suspendOp == JSOp::Yield || suspendOp == JSOp::AwaitThenYield);

    bool resumeAgain = false;
    if (!AsyncGeneratorYieldAndGetResumption(cx, generator, thisOrRval,
                                             &resumeAgain, &completionKind,
                                             &resumeArgument)) {
      return false;
    }
    if (resumeAgain) {
      continue;
    }
    return true;
  }
}

/**
 * Explicit Resource Management Proposal
 * 27.1.3.1 %AsyncIteratorPrototype% [ @@asyncDispose ] ( )
 * https://arai-a.github.io/ecma262-compare/?pr=3000&id=sec-%25asynciteratorprototype%25-%40%40asyncdispose
 */
static bool AsyncIteratorDispose(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Step 1. Let O be the this value.
  JS::Handle<JS::Value> O = args.thisv();

  // Step 2. Let promiseCapability be ! NewPromiseCapability(%Promise%).
  JS::Rooted<PromiseObject*> promise(cx,
                                     PromiseObject::createSkippingExecutor(cx));
  if (!promise) {
    return false;
  }

  // Step 3. Let return be Completion(GetMethod(O, "return")).
  JS::Rooted<JS::Value> returnMethod(cx);
  if (!GetProperty(cx, O, cx->names().return_, &returnMethod)) {
    // Step 4. IfAbruptRejectPromise(return, promiseCapability).
    return AbruptRejectPromise(cx, args, promise, nullptr);
  }

  // Step 5. If return is undefined, then
  // As per the spec GetMethod returns undefined if the property is either null
  // or undefined thus here we check for both.
  if (returnMethod.isNullOrUndefined()) {
    // Step 5.a. Perform ! Call(promiseCapability.[[Resolve]], undefined, «
    // undefined »).
    if (!PromiseObject::resolve(cx, promise, JS::UndefinedHandleValue)) {
      return false;
    }
    args.rval().setObject(*promise);
    return true;
  }

  // GetMethod also throws a TypeError exception if the function is not callable
  // thus we perform that check here.
  if (!IsCallable(returnMethod)) {
    ReportIsNotFunction(cx, returnMethod);
    return AbruptRejectPromise(cx, args, promise, nullptr);
  }

  // Step 6. Else,
  // Step 6.a. Let result be Completion(Call(return, O, « undefined »)).
  JS::Rooted<JS::Value> rval(cx);
  if (!Call(cx, returnMethod, O, JS::UndefinedHandleValue, &rval)) {
    // Step 6.b. IfAbruptRejectPromise(result, promiseCapability).
    return AbruptRejectPromise(cx, args, promise, nullptr);
  }

  // Step 6.c-g.
  if (!InternalAsyncIteratorDisposeAwait(cx, rval, promise)) {
    return AbruptRejectPromise(cx, args, promise, nullptr);
  }

  // Step 7. Return promiseCapability.[[Promise]].
  args.rval().setObject(*promise);
  return true;
}

static const JSFunctionSpec async_generator_methods[] = {
    JS_FN("next", js::AsyncGeneratorNext, 1, 0),
    JS_FN("throw", js::AsyncGeneratorThrow, 1, 0),
    JS_FN("return", js::AsyncGeneratorReturn, 1, 0),
    JS_FS_END,
};

static JSObject* CreateAsyncGeneratorFunction(JSContext* cx, JSProtoKey key) {
  RootedObject proto(cx, &cx->global()->getFunctionConstructor());
  Handle<PropertyName*> name = cx->names().AsyncGeneratorFunction;

  // ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
  //
  // The AsyncGeneratorFunction Constructor
  // https://tc39.es/ecma262/#sec-asyncgeneratorfunction-constructor
  return NewFunctionWithProto(cx, AsyncGeneratorConstructor, 1,
                              FunctionFlags::NATIVE_CTOR, nullptr, name, proto,
                              gc::AllocKind::FUNCTION, TenuredObject);
}

static JSObject* CreateAsyncGeneratorFunctionPrototype(JSContext* cx,
                                                       JSProtoKey key) {
  return NewTenuredObjectWithFunctionPrototype(cx, cx->global());
}

static bool AsyncGeneratorFunctionClassFinish(JSContext* cx,
                                              HandleObject asyncGenFunction,
                                              HandleObject asyncGenerator) {
  Handle<GlobalObject*> global = cx->global();

  // Change the "constructor" property to non-writable before adding any other
  // properties, so it's still the last property and can be modified without a
  // dictionary-mode transition.
  MOZ_ASSERT(asyncGenerator->as<NativeObject>().getLastProperty().key() ==
             NameToId(cx->names().constructor));
  MOZ_ASSERT(!asyncGenerator->as<NativeObject>().inDictionaryMode());

  RootedValue asyncGenFunctionVal(cx, ObjectValue(*asyncGenFunction));
  if (!DefineDataProperty(cx, asyncGenerator, cx->names().constructor,
                          asyncGenFunctionVal, JSPROP_READONLY)) {
    return false;
  }
  MOZ_ASSERT(!asyncGenerator->as<NativeObject>().inDictionaryMode());

  RootedObject asyncIterProto(
      cx, GlobalObject::getOrCreateAsyncIteratorPrototype(cx, global));
  if (!asyncIterProto) {
    return false;
  }

  // ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
  //
  // AsyncGenerator Objects
  // https://tc39.es/ecma262/#sec-asyncgenerator-objects
  RootedObject asyncGenProto(cx, GlobalObject::createBlankPrototypeInheriting(
                                     cx, &PlainObject::class_, asyncIterProto));
  if (!asyncGenProto) {
    return false;
  }
  if (!DefinePropertiesAndFunctions(cx, asyncGenProto, nullptr,
                                    async_generator_methods) ||
      !DefineToStringTag(cx, asyncGenProto, cx->names().AsyncGenerator)) {
    return false;
  }

  // ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
  //
  // Properties of the AsyncGeneratorFunction Prototype Object
  // https://tc39.es/ecma262/#sec-properties-of-asyncgeneratorfunction-prototype
  if (!LinkConstructorAndPrototype(cx, asyncGenerator, asyncGenProto,
                                   JSPROP_READONLY, JSPROP_READONLY) ||
      !DefineToStringTag(cx, asyncGenerator,
                         cx->names().AsyncGeneratorFunction)) {
    return false;
  }

  global->setAsyncGeneratorPrototype(asyncGenProto);

  return true;
}

static const ClassSpec AsyncGeneratorFunctionClassSpec = {
    CreateAsyncGeneratorFunction,
    CreateAsyncGeneratorFunctionPrototype,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    AsyncGeneratorFunctionClassFinish,
    ClassSpec::DontDefineConstructor,
};

const JSClass js::AsyncGeneratorFunctionClass = {
    "AsyncGeneratorFunction",
    0,
    JS_NULL_CLASS_OPS,
    &AsyncGeneratorFunctionClassSpec,
};

[[nodiscard]] bool js::AsyncGeneratorPromiseReactionJob(
    JSContext* cx, PromiseHandler handler,
    Handle<AsyncGeneratorObject*> generator, HandleValue argument) {
  // Await's handlers don't return a value, nor throw any exceptions.
  // They fail only on OOM.
  switch (handler) {
    case PromiseHandler::AsyncGeneratorAwaitedFulfilled:
      return AsyncGeneratorAwaitedFulfilled(cx, generator, argument);

    case PromiseHandler::AsyncGeneratorAwaitedRejected:
      return AsyncGeneratorAwaitedRejected(cx, generator, argument);

    case PromiseHandler::AsyncGeneratorAwaitReturnFulfilled:
      return AsyncGeneratorAwaitReturnFulfilled(cx, generator, argument);

    case PromiseHandler::AsyncGeneratorAwaitReturnRejected:
      return AsyncGeneratorAwaitReturnRejected(cx, generator, argument);

    case PromiseHandler::AsyncGeneratorYieldReturnAwaitedFulfilled:
      return AsyncGeneratorYieldReturnAwaitedFulfilled(cx, generator, argument);

    case PromiseHandler::AsyncGeneratorYieldReturnAwaitedRejected:
      return AsyncGeneratorYieldReturnAwaitedRejected(cx, generator, argument);

    case PromiseHandler::AsyncGeneratorYieldAwaitedFulfilled:
      return AsyncGeneratorYieldAwaitedFulfilled(cx, generator, argument);

    case PromiseHandler::AsyncGeneratorYieldAwaitedRejected:
      return AsyncGeneratorYieldAwaitedRejected(cx, generator, argument);

    default:
      MOZ_CRASH("Bad handler in AsyncGeneratorPromiseReactionJob");
  }
}

// ---------------------
// AsyncFromSyncIterator
// ---------------------

const JSClass AsyncFromSyncIteratorObject::class_ = {
    "AsyncFromSyncIteratorObject",
    JSCLASS_HAS_RESERVED_SLOTS(AsyncFromSyncIteratorObject::Slots),
};

/*
 * ES2024 draft rev 53454a9a596d90473d2152ef04656d605162cd4c
 *
 * CreateAsyncFromSyncIterator ( syncIteratorRecord )
 * https://tc39.es/ecma262/#sec-createasyncfromsynciterator
 */
JSObject* js::CreateAsyncFromSyncIterator(JSContext* cx, HandleObject iter,
                                          HandleValue nextMethod) {
  // Steps 1-5.
  return AsyncFromSyncIteratorObject::create(cx, iter, nextMethod);
}

/*
 * ES2024 draft rev 53454a9a596d90473d2152ef04656d605162cd4c
 *
 * CreateAsyncFromSyncIterator ( syncIteratorRecord )
 * https://tc39.es/ecma262/#sec-createasyncfromsynciterator
 */
/* static */
JSObject* AsyncFromSyncIteratorObject::create(JSContext* cx, HandleObject iter,
                                              HandleValue nextMethod) {
  // Step 1. Let asyncIterator be
  //         OrdinaryObjectCreate(%AsyncFromSyncIteratorPrototype%, «
  //         [[SyncIteratorRecord]] »).
  RootedObject proto(cx,
                     GlobalObject::getOrCreateAsyncFromSyncIteratorPrototype(
                         cx, cx->global()));
  if (!proto) {
    return nullptr;
  }

  AsyncFromSyncIteratorObject* asyncIter =
      NewObjectWithGivenProto<AsyncFromSyncIteratorObject>(cx, proto);
  if (!asyncIter) {
    return nullptr;
  }

  // Step 3. Let nextMethod be ! Get(asyncIterator, "next").
  // (done in caller)

  // Step 2. Set asyncIterator.[[SyncIteratorRecord]] to syncIteratorRecord.
  // Step 4. Let iteratorRecord be the Iterator Record { [[Iterator]]:
  //         asyncIterator, [[NextMethod]]: nextMethod, [[Done]]: false }.
  asyncIter->init(iter, nextMethod);

  // Step 5. Return iteratorRecord.
  return asyncIter;
}

/**
 * ES2024 draft rev 53454a9a596d90473d2152ef04656d605162cd4c
 *
 * %AsyncFromSyncIteratorPrototype%.next ( [ value ] )
 * https://tc39.es/ecma262/#sec-%asyncfromsynciteratorprototype%.next
 */
static bool AsyncFromSyncIteratorNext(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  return AsyncFromSyncIteratorMethod(cx, args, CompletionKind::Normal);
}

/**
 * ES2024 draft rev 53454a9a596d90473d2152ef04656d605162cd4c
 *
 * %AsyncFromSyncIteratorPrototype%.return ( [ value ] )
 * https://tc39.es/ecma262/#sec-%asyncfromsynciteratorprototype%.return
 */
static bool AsyncFromSyncIteratorReturn(JSContext* cx, unsigned argc,
                                        Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  return AsyncFromSyncIteratorMethod(cx, args, CompletionKind::Return);
}

/**
 * ES2024 draft rev 53454a9a596d90473d2152ef04656d605162cd4c
 *
 * %AsyncFromSyncIteratorPrototype%.throw ( [ value ] )
 * https://tc39.es/ecma262/#sec-%asyncfromsynciteratorprototype%.throw
 */
static bool AsyncFromSyncIteratorThrow(JSContext* cx, unsigned argc,
                                       Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  return AsyncFromSyncIteratorMethod(cx, args, CompletionKind::Throw);
}

static const JSFunctionSpec async_from_sync_iter_methods[] = {
    JS_FN("next", AsyncFromSyncIteratorNext, 1, 0),
    JS_FN("throw", AsyncFromSyncIteratorThrow, 1, 0),
    JS_FN("return", AsyncFromSyncIteratorReturn, 1, 0),
    JS_FS_END,
};

bool GlobalObject::initAsyncFromSyncIteratorProto(
    JSContext* cx, Handle<GlobalObject*> global) {
  if (global->hasBuiltinProto(ProtoKind::AsyncFromSyncIteratorProto)) {
    return true;
  }

  RootedObject asyncIterProto(
      cx, GlobalObject::getOrCreateAsyncIteratorPrototype(cx, global));
  if (!asyncIterProto) {
    return false;
  }

  // ES2024 draft rev 53454a9a596d90473d2152ef04656d605162cd4c
  //
  // The %AsyncFromSyncIteratorPrototype% Object
  // https://tc39.es/ecma262/#sec-%asyncfromsynciteratorprototype%-object
  RootedObject asyncFromSyncIterProto(
      cx, GlobalObject::createBlankPrototypeInheriting(cx, &PlainObject::class_,
                                                       asyncIterProto));
  if (!asyncFromSyncIterProto) {
    return false;
  }
  if (!DefinePropertiesAndFunctions(cx, asyncFromSyncIterProto, nullptr,
                                    async_from_sync_iter_methods) ||
      !DefineToStringTag(cx, asyncFromSyncIterProto,
                         cx->names().Async_from_Sync_Iterator_)) {
    return false;
  }

  global->initBuiltinProto(ProtoKind::AsyncFromSyncIteratorProto,
                           asyncFromSyncIterProto);
  return true;
}

// -------------
// AsyncIterator
// -------------

static const JSFunctionSpec async_iterator_proto_methods[] = {
    JS_SELF_HOSTED_SYM_FN(asyncIterator, "AsyncIteratorIdentity", 0, 0),
    JS_SYM_FN(asyncDispose, AsyncIteratorDispose, 0, 0),
    JS_FS_END,
};

static const JSFunctionSpec async_iterator_proto_methods_with_helpers[] = {
    JS_SELF_HOSTED_FN("map", "AsyncIteratorMap", 1, 0),
    JS_SELF_HOSTED_FN("filter", "AsyncIteratorFilter", 1, 0),
    JS_SELF_HOSTED_FN("take", "AsyncIteratorTake", 1, 0),
    JS_SELF_HOSTED_FN("drop", "AsyncIteratorDrop", 1, 0),
    JS_SELF_HOSTED_FN("asIndexedPairs", "AsyncIteratorAsIndexedPairs", 0, 0),
    JS_SELF_HOSTED_FN("flatMap", "AsyncIteratorFlatMap", 1, 0),
    JS_SELF_HOSTED_FN("reduce", "AsyncIteratorReduce", 1, 0),
    JS_SELF_HOSTED_FN("toArray", "AsyncIteratorToArray", 0, 0),
    JS_SELF_HOSTED_FN("forEach", "AsyncIteratorForEach", 1, 0),
    JS_SELF_HOSTED_FN("some", "AsyncIteratorSome", 1, 0),
    JS_SELF_HOSTED_FN("every", "AsyncIteratorEvery", 1, 0),
    JS_SELF_HOSTED_FN("find", "AsyncIteratorFind", 1, 0),
    JS_SELF_HOSTED_SYM_FN(asyncIterator, "AsyncIteratorIdentity", 0, 0),
    JS_SYM_FN(asyncDispose, AsyncIteratorDispose, 0, 0),
    JS_FS_END,
};

bool GlobalObject::initAsyncIteratorProto(JSContext* cx,
                                          Handle<GlobalObject*> global) {
  if (global->hasBuiltinProto(ProtoKind::AsyncIteratorProto)) {
    return true;
  }

  // 25.1.3 The %AsyncIteratorPrototype% Object
  RootedObject asyncIterProto(
      cx, GlobalObject::createBlankPrototype<PlainObject>(cx, global));
  if (!asyncIterProto) {
    return false;
  }
  if (!DefinePropertiesAndFunctions(cx, asyncIterProto, nullptr,
                                    async_iterator_proto_methods)) {
    return false;
  }

  global->initBuiltinProto(ProtoKind::AsyncIteratorProto, asyncIterProto);
  return true;
}

// https://tc39.es/proposal-iterator-helpers/#sec-asynciterator as of revision
// 8f10db5.
static bool AsyncIteratorConstructor(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Step 1.
  if (!ThrowIfNotConstructing(cx, args, "AsyncIterator")) {
    return false;
  }
  // Throw TypeError if NewTarget is the active function object, preventing the
  // Iterator constructor from being used directly.
  if (args.callee() == args.newTarget().toObject()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_BOGUS_CONSTRUCTOR, "AsyncIterator");
    return false;
  }

  // Step 2.
  RootedObject proto(cx);
  if (!GetPrototypeFromBuiltinConstructor(cx, args, JSProto_AsyncIterator,
                                          &proto)) {
    return false;
  }

  JSObject* obj = NewObjectWithClassProto<AsyncIteratorObject>(cx, proto);
  if (!obj) {
    return false;
  }

  args.rval().setObject(*obj);
  return true;
}

static const ClassSpec AsyncIteratorObjectClassSpec = {
    GenericCreateConstructor<AsyncIteratorConstructor, 0,
                             gc::AllocKind::FUNCTION>,
    GenericCreatePrototype<AsyncIteratorObject>,
    nullptr,
    nullptr,
    async_iterator_proto_methods_with_helpers,
    nullptr,
    nullptr,
};

const JSClass AsyncIteratorObject::class_ = {
    "AsyncIterator",
    JSCLASS_HAS_CACHED_PROTO(JSProto_AsyncIterator),
    JS_NULL_CLASS_OPS,
    &AsyncIteratorObjectClassSpec,
};

const JSClass AsyncIteratorObject::protoClass_ = {
    "AsyncIterator.prototype",
    JSCLASS_HAS_CACHED_PROTO(JSProto_AsyncIterator),
    JS_NULL_CLASS_OPS,
    &AsyncIteratorObjectClassSpec,
};

// Iterator Helper proposal
static const JSFunctionSpec async_iterator_helper_methods[] = {
    JS_SELF_HOSTED_FN("next", "AsyncIteratorHelperNext", 1, 0),
    JS_SELF_HOSTED_FN("return", "AsyncIteratorHelperReturn", 1, 0),
    JS_SELF_HOSTED_FN("throw", "AsyncIteratorHelperThrow", 1, 0),
    JS_FS_END,
};

static const JSClass AsyncIteratorHelperPrototypeClass = {
    "Async Iterator Helper",
    0,
};

const JSClass AsyncIteratorHelperObject::class_ = {
    "Async Iterator Helper",
    JSCLASS_HAS_RESERVED_SLOTS(AsyncIteratorHelperObject::SlotCount),
};

/* static */
NativeObject* GlobalObject::getOrCreateAsyncIteratorHelperPrototype(
    JSContext* cx, Handle<GlobalObject*> global) {
  return MaybeNativeObject(
      getOrCreateBuiltinProto(cx, global, ProtoKind::AsyncIteratorHelperProto,
                              initAsyncIteratorHelperProto));
}

/* static */
bool GlobalObject::initAsyncIteratorHelperProto(JSContext* cx,
                                                Handle<GlobalObject*> global) {
  if (global->hasBuiltinProto(ProtoKind::AsyncIteratorHelperProto)) {
    return true;
  }

  RootedObject asyncIterProto(
      cx, GlobalObject::getOrCreateAsyncIteratorPrototype(cx, global));
  if (!asyncIterProto) {
    return false;
  }

  RootedObject asyncIteratorHelperProto(
      cx, GlobalObject::createBlankPrototypeInheriting(
              cx, &AsyncIteratorHelperPrototypeClass, asyncIterProto));
  if (!asyncIteratorHelperProto) {
    return false;
  }
  if (!DefinePropertiesAndFunctions(cx, asyncIteratorHelperProto, nullptr,
                                    async_iterator_helper_methods)) {
    return false;
  }

  global->initBuiltinProto(ProtoKind::AsyncIteratorHelperProto,
                           asyncIteratorHelperProto);
  return true;
}

AsyncIteratorHelperObject* js::NewAsyncIteratorHelper(JSContext* cx) {
  RootedObject proto(cx, GlobalObject::getOrCreateAsyncIteratorHelperPrototype(
                             cx, cx->global()));
  if (!proto) {
    return nullptr;
  }
  return NewObjectWithGivenProto<AsyncIteratorHelperObject>(cx, proto);
}
