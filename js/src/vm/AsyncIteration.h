/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_AsyncIteration_h
#define vm_AsyncIteration_h

#include "builtin/Promise.h"  // js::PromiseHandler
#include "builtin/SelfHostingDefines.h"
#include "js/Class.h"
#include "vm/GeneratorObject.h"
#include "vm/JSObject.h"
#include "vm/List.h"
#include "vm/PromiseObject.h"

// [SMDOC] Async generators
//
// # Start
//
// When an async generator is called, it synchronously runs until the
// JSOp::InitialYield and then suspends, just like a sync generator, and returns
// an async generator object (js::AsyncGeneratorObject).
//
//
// # Request queue
//
// When next/return/throw is called on the async generator object:
//   * AsyncGeneratorEnqueue creates a new AsyncGeneratorRequest and enqueues
//     it in the generator object's request queue.
//   * AsyncGeneratorResume resumes the generator with the oldest request,
//     if the generator is suspended (see "Resume" section below)
//
// The returned promise is resolved when the resumption for the request
// completes with yield/throw/return, in AsyncGeneratorCompleteStepNormal and
// AsyncGeneratorCompleteStepThrow.
// They correspond to AsyncGeneratorCompleteStep in the spec.
//
//
// # Await
//
// Async generator's `await` is implemented differently than async function's
// `await`.
//
// The bytecode is the following:
// (ignoring CanSkipAwait; see the comment in AsyncFunction.h for more details)
//
// ```
//   (operand here)                  # VALUE
//   GetAliasedVar ".generator"      # VALUE .generator
//   Await 0                         # RVAL GENERATOR RESUMEKIND
//
//   AfterYield                      # RVAL GENERATOR RESUMEKIND
//   CheckResumeKind                 # RVAL
// ```
//
// Async generators don't use JSOp::AsyncAwait, and that part is handled
// in AsyncGeneratorResume, and AsyncGeneratorAwait called there.
//
// Both JSOp::Await and JSOp::Yield behave in the exactly same way,
// and AsyncGeneratorResume checks the last opcode and branches for
// await/yield/return cases.
//
//
// # Reaction jobs and resume after await
//
// This is almost same as for async functions (see AsyncFunction.h).
//
// The reaction record for the job is marked as "this is for async generator"
// (see AsyncGeneratorAwait), and handled specially in
// PromiseReactionJob, which calls js::AsyncGeneratorPromiseReactionJob.
//
//
// # Yield
//
// `yield` is implemented with the following bytecode sequence:
// (Ignoring CanSkipAwait for simplicity)
//
// ```
//   (operand here)                  # VALUE
//   GetAliasedVar ".generator"      # VALUE .generator
//   Await 1                         # RVAL GENERATOR RESUMEKIND
//   AfterYield                      # RVAL GENERATOR RESUMEKIND
//   CheckResumeKind                 # RVAL
//
//   GetAliasedVar ".generator"      # RVAL .generator
//   Yield 2                         # RVAL2 GENERATOR RESUMEKIND
//
//   AfterYield                      # RVAL2 GENERATOR RESUMEKIND
//   CheckResumeKind                 # RVAL2
// ```
//
// The 1st part (JSOp::Await + JSOp::CheckResumeKind) performs an implicit
// `await`, as specified in Yield step 2.
//
//   Yield ( value )
//   https://tc39.es/ecma262/#sec-yield
//
//     2. If generatorKind is async, return
//        ? AsyncGeneratorYield(? Await(value)).
//
// The 2nd part (JSOp::Yield) suspends execution and yields the result of
// `await`, as specified in AsyncGeneratorYield.
//
//   AsyncGeneratorYield ( value )
//   https://tc39.es/ecma262/#sec-asyncgeneratoryield
//
//     1. Let genContext be the running execution context.
//     2. Assert: genContext is the execution context of a generator.
//     3. Let generator be the value of the Generator component of genContext.
//     4. Assert: GetGeneratorKind() is async.
//     5. Let completion be NormalCompletion(value).
//     6. Assert: The execution context stack has at least two elements.
//     7. Let previousContext be the second to top element of the execution
//        context stack.
//     8. Let previousRealm be previousContext's Realm.
//     9. Perform AsyncGeneratorCompleteStep(generator, completion, false,
//        previousRealm).
//     10. Let queue be generator.[[AsyncGeneratorQueue]].
//     11. If queue is not empty, then
//       a. NOTE: Execution continues without suspending the generator.
//       b. Let toYield be the first element of queue.
//       c. Let resumptionValue be Completion(toYield.[[Completion]]).
//       d. Return ? AsyncGeneratorUnwrapYieldResumption(resumptionValue).
//     12. Else,
//       a. Set generator.[[AsyncGeneratorState]] to suspended-yield.
//       b. Remove genContext from the execution context stack and restore the
//          execution context that is at the top of the execution context stack
//          as the running execution context.
//       c. Let callerContext be the running execution context.
//       d. Resume callerContext passing undefined. If genContext is ever
//          resumed again, let resumptionValue be the Completion Record with
//          which it is resumed.
//       e. Assert: If control reaches here, then genContext is the running
//          execution context again.
//       f. Return ? AsyncGeneratorUnwrapYieldResumption(resumptionValue).
//
// The last part (JSOp::CheckResumeKind) checks the resumption type and
// resumes/throws/returns the execution, as specified in
// AsyncGeneratorUnwrapYieldResumption
//
//   AsyncGeneratorUnwrapYieldResumption ( resumptionValue )
//   https://tc39.es/ecma262/#sec-asyncgeneratorunwrapyieldresumption
//
//     1. If resumptionValue is not a return completion,
//        return ? resumptionValue.
//     2. Let awaited be Completion(Await(resumptionValue.[[Value]])).
//     3. If awaited is a throw completion, return ? awaited.
//     4. Assert: awaited is a normal completion.
//     5. Return ReturnCompletion(awaited.[[Value]]).
//
// Resumption with `AsyncGenerator.prototype.return` is handled differently.
// See "Resumption with return" section below.
//
//
// # Return
//
// `return` with operand is implemented with the following bytecode sequence:
// (Ignoring CanSkipAwait for simplicity)
//
// ```
//   (operand here)                  # VALUE
//   GetAliasedVar ".generator"      # VALUE .generator
//   Await 0                         # RVAL GENERATOR RESUMEKIND
//   AfterYield                      # RVAL GENERATOR RESUMEKIND
//   CheckResumeKind                 # RVAL
//
//   SetRval                         #
//   GetAliasedVar ".generator"      # .generator
//   FinalYieldRval                  #
// ```
//
// The 1st part (JSOp::Await + JSOp::CheckResumeKind) performs implicit
// `await`, as specified in ReturnStatement's Evaluation step 3.
//
//   ReturnStatement: return Expression;
//   https://tc39.es/ecma262/#sec-return-statement-runtime-semantics-evaluation
//
//     3. If GetGeneratorKind() is async, set exprValue to ? Await(exprValue).
//
// And the 2nd part corresponds to AsyncGeneratorStart steps 4.g-l.
//
//   AsyncGeneratorStart ( generator, generatorBody )
//   https://tc39.es/ecma262/#sec-asyncgeneratorstart
//
//     4. Let closure be a new Abstract Closure with no parameters that captures
//        generatorBody and performs the following steps when called:
//       ...
//       g. Set acGenerator.[[AsyncGeneratorState]] to draining-queue.
//       h. If result is a normal completion, set result to
//          NormalCompletion(undefined).
//       i. If result is a return completion, set result to
//          NormalCompletion(result.[[Value]]).
//       j. Perform AsyncGeneratorCompleteStep(acGenerator, result, true).
//       k. Perform AsyncGeneratorDrainQueue(acGenerator).
//       l. Return undefined.
//
// `return` without operand or implicit return is implicit with the following
// bytecode sequence:
//
// ```
//   Undefined                       # undefined
//   SetRval                         #
//   GetAliasedVar ".generator"      # .generator
//   FinalYieldRval                  #
// ```
//
// This is also AsyncGeneratorStart steps 4.g-l.
//
//
// # Throw
//
// Unlike async function, async generator doesn't use implicit try-catch,
// but the throw completion is handled by AsyncGeneratorResume,
// and AsyncGeneratorThrown is called there.
//
//   AsyncGeneratorStart ( generator, generatorBody )
//   https://tc39.es/ecma262/#sec-asyncgeneratorstart
//
//     4. Let closure be a new Abstract Closure with no parameters that captures
//        generatorBody and performs the following steps when called:
//       ...
//       g. Set acGenerator.[[AsyncGeneratorState]] to draining-queue.
//       h. If result is a normal completion, set result to
//          NormalCompletion(undefined).
//       i. If result is a return completion, set result to
//          NormalCompletion(result.[[Value]]).
//       j. Perform AsyncGeneratorCompleteStep(acGenerator, result, true).
//       k. Perform AsyncGeneratorDrainQueue(acGenerator).
//       l. Return undefined.
//
//   AsyncGeneratorCompleteStep ( generator, completion, done [ , realm ] )
//   https://tc39.es/ecma262/#sec-asyncgeneratorcompletestep
//
//     1. Assert: generator.[[AsyncGeneratorQueue]] is not empty.
//     2. Let next be the first element of generator.[[AsyncGeneratorQueue]].
//     3. Remove the first element from generator.[[AsyncGeneratorQueue]].
//     4. Let promiseCapability be next.[[Capability]].
//     5. Let value be completion.[[Value]].
//     6. If completion is a throw completion, then
//       a. Perform ! Call(promiseCapability.[[Reject]], undefined, « value »).
//
//
// # Resumption with return
//
// If the generator is in "suspended-yield" state, it doesn't immediately
// resume the generator script itself, but it handles implicit `await` it in
// AsyncGeneratorUnwrapYieldResumption.
// (See PromiseHandler::AsyncGeneratorYieldReturnAwaitedFulfilled and
// PromiseHandler::AsyncGeneratorYieldReturnAwaitedRejected), and resumes the
// generator with the result of await.
//
// The return completion is finally handled in JSOp::CheckResumeKind
// after JSOp::Yield.
//
//   AsyncGeneratorUnwrapYieldResumption ( resumptionValue )
//   https://tc39.es/ecma262/#sec-asyncgeneratorunwrapyieldresumption
//
//     1. If resumptionValue is not a return completion, return ?
//        resumptionValue.
//     2. Let awaited be Completion(Await(resumptionValue.[[Value]])).
//     3. If awaited is a throw completion, return ? awaited.
//     4. Assert: awaited is a normal completion.
//     5. Return ReturnCompletion(awaited.[[Value]]).
//
// If the generator is already completed, it awaits on the return value in
// AsyncGeneratorAwaitReturn.
// (See PromiseHandler::AsyncGeneratorAwaitReturnFulfilled and
//  PromiseHandler::AsyncGeneratorAwaitReturnRejected), and resolves the
// request's promise with the value.

namespace js {

class AsyncGeneratorObject;
enum class CompletionKind : uint8_t;

extern const JSClass AsyncGeneratorFunctionClass;

[[nodiscard]] bool AsyncGeneratorPromiseReactionJob(
    JSContext* cx, PromiseHandler handler,
    Handle<AsyncGeneratorObject*> generator, HandleValue argument);

bool AsyncGeneratorNext(JSContext* cx, unsigned argc, Value* vp);
bool AsyncGeneratorReturn(JSContext* cx, unsigned argc, Value* vp);
bool AsyncGeneratorThrow(JSContext* cx, unsigned argc, Value* vp);

// AsyncGeneratorRequest record in the spec.
// Stores the info from AsyncGenerator#{next,return,throw}.
//
// This object is reused across multiple requests as an optimization, and
// stored in the Slot_CachedRequest slot.
class AsyncGeneratorRequest : public NativeObject {
 private:
  // Int32 value with CompletionKind.
  //   Normal: next
  //   Return: return
  //   Throw:  throw
  JS_DEFINE_TYPED_SLOT(0, Slot_CompletionKind, Int32, Undefined);

  // The value passed to AsyncGenerator#{next,return,throw}.
  JS_DEFINE_UNTYPED_SLOT(1, Slot_CompletionValue);

  // The promise returned by AsyncGenerator#{next,return,throw}.
  JS_DEFINE_TYPED_SLOT(2, Slot_Promise, Object, Null, Undefined);

  static constexpr uint32_t Slots = 3;

  void init(CompletionKind completionKind, const Value& completionValue,
            PromiseObject* promise) {
    setFixedSlotTyped(Slot_CompletionKind,
                      Int32Value(static_cast<int32_t>(completionKind)));
    setFixedSlot(Slot_CompletionValue, completionValue);
    setFixedSlotTyped(Slot_Promise, ObjectValue(*promise));
  }

  // Clear the request data for reuse.
  void clearData() {
    setFixedSlot(Slot_CompletionValue, NullValue());
    setFixedSlotTyped(Slot_Promise, NullValue());
  }

  friend AsyncGeneratorObject;

 public:
  static const JSClass class_;

  static AsyncGeneratorRequest* create(JSContext* cx,
                                       CompletionKind completionKind,
                                       HandleValue completionValue,
                                       Handle<PromiseObject*> promise);

  CompletionKind completionKind() const {
    return static_cast<CompletionKind>(
        getFixedSlotTyped(Slot_CompletionKind).toInt32());
  }
  JS::Value completionValue() const {
    return getFixedSlot(Slot_CompletionValue);
  }
  PromiseObject* promise() const {
    return &getFixedSlotTyped(Slot_Promise).toObject().as<PromiseObject>();
  }
};

class AsyncGeneratorObject : public AbstractGeneratorObject {
 private:
  // Int32 value containing one of the |State| fields from below.
  JS_DEFINE_TYPED_SLOT(AbstractGeneratorObject::RESERVED_SLOTS, Slot_State,
                       Int32, Undefined);

  // * null value if this async generator has no requests
  // * AsyncGeneratorRequest if this async generator has only one request
  // * list object if this async generator has 2 or more requests
  JS_DEFINE_TYPED_SLOT(AbstractGeneratorObject::RESERVED_SLOTS + 1,
                       Slot_QueueOrRequest, Object, Null, Undefined);

  // Cached AsyncGeneratorRequest for later use.
  // undefined if there's no cache.
  JS_DEFINE_TYPED_SLOT(AbstractGeneratorObject::RESERVED_SLOTS + 2,
                       Slot_CachedRequest, Object, Null, Undefined);

  static constexpr uint32_t Slots = AbstractGeneratorObject::RESERVED_SLOTS + 3;

 public:
  enum State {
    // "suspended-start" in the spec.
    // Suspended after invocation.
    State_SuspendedStart,

    // "suspended-yield" in the spec
    // Suspended with `yield` expression.
    State_SuspendedYield,

    // "executing" in the spec.
    // Resumed from initial suspend or yield, and either running the script
    // or awaiting for `await` expression.
    State_Executing,

    // Part of "executing" in the spec.
    // Awaiting on the value passed by AsyncGenerator#return which is called
    // while executing.
    State_Executing_AwaitingYieldReturn,

    // "draining-queue" in the spec.
    // It's performing AsyncGeneratorDrainQueue.
    State_DrainingQueue,

    // Part of "draining-queue" in the spec.
    // Awaiting on the value passed by AsyncGenerator#return which is called
    // after completed.
    State_DrainingQueue_AwaitingReturn,

    // "completed" in the spec.
    // The generator is completed.
    State_Completed
  };

  State state() const {
    return static_cast<State>(getFixedSlotTyped(Slot_State).toInt32());
  }
  void setState(State state_) {
    setFixedSlotTyped(Slot_State, Int32Value(state_));
  }

 private:
  // Queue is implemented in 2 ways.  If only one request is queued ever,
  // request is stored directly to the slot.  Once 2 requests are queued, a
  // list is created and requests are appended into it, and the list is
  // stored to the slot.

  bool isSingleQueue() const {
    return getFixedSlotTyped(Slot_QueueOrRequest).isNull() ||
           getFixedSlotTyped(Slot_QueueOrRequest)
               .toObject()
               .is<AsyncGeneratorRequest>();
  }
  bool isSingleQueueEmpty() const {
    return getFixedSlotTyped(Slot_QueueOrRequest).isNull();
  }
  void setSingleQueueRequest(AsyncGeneratorRequest* request) {
    setFixedSlotTyped(Slot_QueueOrRequest, ObjectValue(*request));
  }
  void clearSingleQueueRequest() {
    setFixedSlotTyped(Slot_QueueOrRequest, NullValue());
  }
  AsyncGeneratorRequest* singleQueueRequest() const {
    return &getFixedSlotTyped(Slot_QueueOrRequest)
                .toObject()
                .as<AsyncGeneratorRequest>();
  }

  ListObject* queue() const {
    return &getFixedSlotTyped(Slot_QueueOrRequest).toObject().as<ListObject>();
  }
  void setQueue(ListObject* queue_) {
    setFixedSlotTyped(Slot_QueueOrRequest, ObjectValue(*queue_));
  }

 public:
  static const JSClass class_;
  static const JSClassOps classOps_;

  static AsyncGeneratorObject* create(JSContext* cx, HandleFunction asyncGen);

  bool isSuspendedStart() const { return state() == State_SuspendedStart; }
  bool isSuspendedYield() const { return state() == State_SuspendedYield; }
  bool isExecuting() const { return state() == State_Executing; }
  bool isExecuting_AwaitingYieldReturn() const {
    return state() == State_Executing_AwaitingYieldReturn;
  }
  bool isDrainingQueue() const { return state() == State_DrainingQueue; }
  bool isDrainingQueue_AwaitingReturn() const {
    return state() == State_DrainingQueue_AwaitingReturn;
  }
  bool isCompleted() const { return state() == State_Completed; }

  void setSuspendedStart() { setState(State_SuspendedStart); }
  void setSuspendedYield() { setState(State_SuspendedYield); }
  void setExecuting() { setState(State_Executing); }
  void setExecuting_AwaitingYieldReturn() {
    setState(State_Executing_AwaitingYieldReturn);
  }
  void setDrainingQueue() { setState(State_DrainingQueue); }
  void setDrainingQueue_AwaitingReturn() {
    setState(State_DrainingQueue_AwaitingReturn);
  }
  void setCompleted() { setState(State_Completed); }

  [[nodiscard]] static bool enqueueRequest(
      JSContext* cx, Handle<AsyncGeneratorObject*> generator,
      Handle<AsyncGeneratorRequest*> request);

  static AsyncGeneratorRequest* dequeueRequest(
      JSContext* cx, Handle<AsyncGeneratorObject*> generator);

  static AsyncGeneratorRequest* peekRequest(
      Handle<AsyncGeneratorObject*> generator);

  bool isQueueEmpty() const {
    if (isSingleQueue()) {
      return isSingleQueueEmpty();
    }
    return queue()->getDenseInitializedLength() == 0;
  }

#ifdef DEBUG
  bool isQueueLengthOne() const {
    if (isSingleQueue()) {
      return !isSingleQueueEmpty();
    }
    return queue()->getDenseInitializedLength() == 1;
  }
#endif

  // This function does either of the following:
  //   * return a cached request object with the slots updated
  //   * create a new request object with the slots set
  static AsyncGeneratorRequest* createRequest(
      JSContext* cx, Handle<AsyncGeneratorObject*> generator,
      CompletionKind completionKind, HandleValue completionValue,
      Handle<PromiseObject*> promise);

  // Stores the given request to the generator's cache after clearing its data
  // slots.  The cached request will be reused in the subsequent createRequest
  // call.
  void cacheRequest(AsyncGeneratorRequest* request) {
    if (hasCachedRequest()) {
      return;
    }

    request->clearData();
    setFixedSlotTyped(Slot_CachedRequest, ObjectValue(*request));
  }

 private:
  bool hasCachedRequest() const {
    return getFixedSlotTyped(Slot_CachedRequest).isObject();
  }

  AsyncGeneratorRequest* takeCachedRequest() {
    auto request = &getFixedSlotTyped(Slot_CachedRequest)
                        .toObject()
                        .as<AsyncGeneratorRequest>();
    clearCachedRequest();
    return request;
  }

  void clearCachedRequest() {
    setFixedSlotTyped(Slot_CachedRequest, NullValue());
  }
};

JSObject* CreateAsyncFromSyncIterator(JSContext* cx, HandleObject iter,
                                      HandleValue nextMethod);

class AsyncFromSyncIteratorObject : public NativeObject {
 private:
  // Object that implements the sync iterator protocol.
  JS_DEFINE_TYPED_SLOT(0, Slot_Iterator, Object, Undefined);

  // The `next` property of the iterator object.
  JS_DEFINE_UNTYPED_SLOT(1, Slot_NextMethod);

  static constexpr uint32_t Slots = 2;

  void init(JSObject* iterator, const Value& nextMethod) {
    setFixedSlotTyped(Slot_Iterator, ObjectValue(*iterator));
    setFixedSlot(Slot_NextMethod, nextMethod);
  }

 public:
  static const JSClass class_;

  static JSObject* create(JSContext* cx, HandleObject iter,
                          HandleValue nextMethod);

  JSObject* iterator() const {
    return &getFixedSlotTyped(Slot_Iterator).toObject();
  }

  const Value& nextMethod() const { return getFixedSlot(Slot_NextMethod); }
};

class AsyncIteratorObject : public NativeObject {
 public:
  static const JSClass class_;
  static const JSClass protoClass_;
};

// Iterator Helpers proposal
class AsyncIteratorHelperObject : public NativeObject {
 public:
  static const JSClass class_;

  enum { GeneratorSlot, SlotCount };

  static_assert(GeneratorSlot == ASYNC_ITERATOR_HELPER_GENERATOR_SLOT,
                "GeneratorSlot must match self-hosting define for generator "
                "object slot.");
};

AsyncIteratorHelperObject* NewAsyncIteratorHelper(JSContext* cx);

}  // namespace js

#endif /* vm_AsyncIteration_h */
