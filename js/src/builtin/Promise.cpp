/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "builtin/Promise.h"

#include "mozilla/Atomics.h"
#include "mozilla/Maybe.h"
#include "mozilla/TimeStamp.h"

#include "jsapi.h"
#include "jsfriendapi.h"

#include "js/CallAndConstruct.h"      // JS::Construct, JS::IsCallable
#include "js/experimental/JitInfo.h"  // JSJitGetterOp, JSJitInfo
#include "js/ForOfIterator.h"         // JS::ForOfIterator
#include "js/friend/ErrorMessages.h"  // js::GetErrorMessage, JSMSG_*
#include "js/Prefs.h"                 // JS::Prefs
#include "js/PropertySpec.h"
#include "js/Stack.h"
#include "vm/ArrayObject.h"
#include "vm/AsyncFunction.h"
#include "vm/AsyncIteration.h"
#include "vm/CompletionKind.h"
#include "vm/ErrorObject.h"
#include "vm/ErrorReporting.h"
#include "vm/Iteration.h"
#include "vm/JSContext.h"
#include "vm/JSObject.h"
#include "vm/List.h"           // js::ListObject
#include "vm/PlainObject.h"    // js::PlainObject
#include "vm/PromiseObject.h"  // js::PromiseObject
#include "vm/SelfHosting.h"
#include "vm/Warnings.h"  // js::WarnNumberASCII

#include "debugger/DebugAPI-inl.h"
#include "gc/StableCellHasher-inl.h"
#include "vm/Compartment-inl.h"
#include "vm/ErrorObject-inl.h"
#include "vm/JSContext-inl.h"  // JSContext::check
#include "vm/JSObject-inl.h"
#include "vm/List-inl.h"  // js::ListObject
#include "vm/NativeObject-inl.h"

using namespace js;

static double MillisecondsSinceStartup() {
  auto now = mozilla::TimeStamp::Now();
  return (now - mozilla::TimeStamp::FirstTimeStamp()).ToMilliseconds();
}

enum ResolutionMode { ResolveMode, RejectMode };

/**
 * ES2023 draft rev 714fa3dd1e8237ae9c666146270f81880089eca5
 *
 * Promise Resolve Functions
 * https://tc39.es/ecma262/#sec-promise-resolve-functions
 */
enum ResolveFunctionSlots {
  // NOTE: All slot represent [[AlreadyResolved]].[[Value]].
  //
  // The spec creates single record for [[AlreadyResolved]] and shares it
  // between Promise Resolve Function and Promise Reject Function.
  //
  //   Step 1. Let alreadyResolved be the Record { [[Value]]: false }.
  //   ...
  //   Step 6. Set resolve.[[AlreadyResolved]] to alreadyResolved.
  //   ...
  //   Step 11. Set reject.[[AlreadyResolved]] to alreadyResolved.
  //
  // We implement it by clearing all slots, both in
  // Promise Resolve Function and Promise Reject Function at the same time.
  //
  // If none of slots are undefined, [[AlreadyResolved]].[[Value]] is false.
  // If all slot are undefined, [[AlreadyResolved]].[[Value]] is true.

  // [[Promise]] slot.
  // A possibly-wrapped promise.
  ResolveFunctionSlot_Promise = 0,

  // The corresponding Promise Reject Function.
  ResolveFunctionSlot_RejectFunction,
};

/**
 * ES2023 draft rev 714fa3dd1e8237ae9c666146270f81880089eca5
 *
 * Promise Reject Functions
 * https://tc39.es/ecma262/#sec-promise-reject-functions
 */
enum RejectFunctionSlots {
  // [[Promise]] slot.
  // A possibly-wrapped promise.
  RejectFunctionSlot_Promise = 0,

  // The corresponding Promise Resolve Function.
  RejectFunctionSlot_ResolveFunction,
};

// The promise combinator builtins such as Promise.all and Promise.allSettled
// allocate one or two functions for each array element. These functions store
// some state in extended slots.
enum PromiseCombinatorElementFunctionSlots {
  // This slot stores either:
  //
  // - The [[Index]] slot (the array index) as Int32Value.
  //
  // - For the onRejected functions for Promise.allSettled, a pointer to the
  //   corresponding onFulfilled function stored as ObjectValue. In this case
  //   the slots on that function must be used instead because the
  //   [[AlreadyCalled]] flag must be shared by these two functions.
  PromiseCombinatorElementFunctionSlot_ElementIndexOrResolveFunc = 0,

  // This slot stores a pointer to the PromiseCombinatorDataHolder JS object.
  // It's also used to represent the [[AlreadyCalled]] flag: we set this slot to
  // UndefinedValue when [[AlreadyCalled]] is set to true in the spec.
  //
  // The onRejected functions for Promise.allSettled and Promise.allSettledKeyed
  // have a NullValue stored in this slot. In this case the slot shouldn't be
  // used because the [[AlreadyCalled]] state must be shared by the two
  // functions.
  PromiseCombinatorElementFunctionSlot_Data
};

struct PromiseCapability {
  JSObject* promise = nullptr;
  JSObject* resolve = nullptr;
  JSObject* reject = nullptr;

  PromiseCapability() = default;

  void trace(JSTracer* trc);
};

void PromiseCapability::trace(JSTracer* trc) {
  if (promise) {
    TraceRoot(trc, &promise, "PromiseCapability::promise");
  }
  if (resolve) {
    TraceRoot(trc, &resolve, "PromiseCapability::resolve");
  }
  if (reject) {
    TraceRoot(trc, &reject, "PromiseCapability::reject");
  }
}

namespace js {

template <typename Wrapper>
class WrappedPtrOperations<PromiseCapability, Wrapper> {
  const PromiseCapability& capability() const {
    return static_cast<const Wrapper*>(this)->get();
  }

 public:
  HandleObject promise() const {
    return HandleObject::fromMarkedLocation(&capability().promise);
  }
  HandleObject resolve() const {
    return HandleObject::fromMarkedLocation(&capability().resolve);
  }
  HandleObject reject() const {
    return HandleObject::fromMarkedLocation(&capability().reject);
  }
};

template <typename Wrapper>
class MutableWrappedPtrOperations<PromiseCapability, Wrapper>
    : public WrappedPtrOperations<PromiseCapability, Wrapper> {
  PromiseCapability& capability() { return static_cast<Wrapper*>(this)->get(); }

 public:
  MutableHandleObject promise() {
    return MutableHandleObject::fromMarkedLocation(&capability().promise);
  }
  MutableHandleObject resolve() {
    return MutableHandleObject::fromMarkedLocation(&capability().resolve);
  }
  MutableHandleObject reject() {
    return MutableHandleObject::fromMarkedLocation(&capability().reject);
  }
};

}  // namespace js

struct PromiseCombinatorElements;

class PromiseCombinatorDataHolder : public NativeObject {
 protected:
  enum {
    Slot_Promise = 0,
    Slot_RemainingElements,
    Slot_ValuesArray,
    Slot_ResolveOrRejectFunction,
    SlotsCount,
  };

 public:
  static const JSClass class_;
  JSObject* promiseObj() { return &getFixedSlot(Slot_Promise).toObject(); }
  JSObject* resolveOrRejectObj() {
    return &getFixedSlot(Slot_ResolveOrRejectFunction).toObject();
  }
  Value valuesArray() { return getFixedSlot(Slot_ValuesArray); }
  int32_t remainingCount() {
    return getFixedSlot(Slot_RemainingElements).toInt32();
  }
  int32_t increaseRemainingCount() {
    int32_t remainingCount = getFixedSlot(Slot_RemainingElements).toInt32();
    remainingCount++;
    setFixedSlot(Slot_RemainingElements, Int32Value(remainingCount));
    return remainingCount;
  }
  int32_t decreaseRemainingCount() {
    int32_t remainingCount = getFixedSlot(Slot_RemainingElements).toInt32();
    remainingCount--;
    MOZ_ASSERT(remainingCount >= 0, "unpaired calls to decreaseRemainingCount");
    setFixedSlot(Slot_RemainingElements, Int32Value(remainingCount));
    return remainingCount;
  }

  static PromiseCombinatorDataHolder* New(
      JSContext* cx, JS::Handle<JSObject*> resultPromise,
      JS::Handle<PromiseCombinatorElements> elements,
      JS::Handle<JSObject*> resolveOrReject);
};

const JSClass PromiseCombinatorDataHolder::class_ = {
    "PromiseCombinatorDataHolder",
    JSCLASS_HAS_RESERVED_SLOTS(SlotsCount),
};

// Specialized data holder for Promise.allKeyed and Promise.allSettledKeyed
// that includes a slot for storing the keys array.
class PromiseCombinatorKeyedDataHolder : public PromiseCombinatorDataHolder {
  enum {
    // Inherits Slot_Promise, Slot_RemainingElements, Slot_ValuesArray,
    // and Slot_ResolveOrRejectFunction from PromiseCombinatorDataHolder.

    // Additional slot for keyed variant: starts after parent's last slot.
    Slot_KeysList = PromiseCombinatorDataHolder::SlotsCount,
    SlotsCount,
  };

 public:
  static const JSClass class_;

  ListObject* keysList() {
    return &getFixedSlot(Slot_KeysList).toObject().as<ListObject>();
  }

  ListObject* valuesList() {
    return &getFixedSlot(Slot_ValuesArray).toObject().as<ListObject>();
  }

  static PromiseCombinatorKeyedDataHolder* New(
      JSContext* cx, JS::Handle<JSObject*> resultPromise,
      JS::Handle<ListObject*> keys, JS::Handle<ListObject*> values,
      JS::Handle<JSObject*> resolveOrReject);

 private:
  using PromiseCombinatorDataHolder::valuesArray;
};

const JSClass PromiseCombinatorKeyedDataHolder::class_ = {
    "PromiseCombinatorKeyedDataHolder",
    JSCLASS_HAS_RESERVED_SLOTS(SlotsCount),
};

// Smart pointer to the "F.[[Values]]" part of the state of a Promise.all or
// Promise.allSettled invocation, or the "F.[[Errors]]" part of the state of a
// Promise.any invocation. Copes with compartment issues when setting an
// element.
struct MOZ_STACK_CLASS PromiseCombinatorElements final {
  // Object value holding the elements array. The object can be a wrapper.
  Value value;

  // Unwrapped elements array. May not belong to the current compartment!
  ArrayObject* unwrappedArray = nullptr;

  // Set to true if the |setElement| method needs to wrap its input value.
  bool setElementNeedsWrapping = false;

  PromiseCombinatorElements() = default;

  void trace(JSTracer* trc);
};

void PromiseCombinatorElements::trace(JSTracer* trc) {
  TraceRoot(trc, &value, "PromiseCombinatorElements::value");
  if (unwrappedArray) {
    TraceRoot(trc, &unwrappedArray,
              "PromiseCombinatorElements::unwrappedArray");
  }
}

namespace js {

template <typename Wrapper>
class WrappedPtrOperations<PromiseCombinatorElements, Wrapper> {
  const PromiseCombinatorElements& elements() const {
    return static_cast<const Wrapper*>(this)->get();
  }

 public:
  HandleValue value() const {
    return HandleValue::fromMarkedLocation(&elements().value);
  }

  Handle<ArrayObject*> unwrappedArray() const {
    return Handle<ArrayObject*>::fromMarkedLocation(&elements().unwrappedArray);
  }
};

template <typename Wrapper>
class MutableWrappedPtrOperations<PromiseCombinatorElements, Wrapper>
    : public WrappedPtrOperations<PromiseCombinatorElements, Wrapper> {
  PromiseCombinatorElements& elements() {
    return static_cast<Wrapper*>(this)->get();
  }

 public:
  MutableHandleValue value() {
    return MutableHandleValue::fromMarkedLocation(&elements().value);
  }

  MutableHandle<ArrayObject*> unwrappedArray() {
    return MutableHandle<ArrayObject*>::fromMarkedLocation(
        &elements().unwrappedArray);
  }

  void initialize(ArrayObject* arrayObj) {
    unwrappedArray().set(arrayObj);
    value().setObject(*arrayObj);

    // |needsWrapping| isn't tracked here, because all modifications on the
    // initial elements don't require any wrapping.
  }

  void initialize(PromiseCombinatorDataHolder* data, ArrayObject* arrayObj,
                  bool needsWrapping) {
    unwrappedArray().set(arrayObj);
    value().set(data->valuesArray());
    elements().setElementNeedsWrapping = needsWrapping;
  }

  [[nodiscard]] bool pushUndefined(JSContext* cx) {
    // Helper for the AutoRealm we need to work with |array|. We mostly do this
    // for performance; we could go ahead and do the define via a cross-
    // compartment proxy instead...
    AutoRealm ar(cx, unwrappedArray());

    Handle<ArrayObject*> arrayObj = unwrappedArray();
    return js::NewbornArrayPush(cx, arrayObj, UndefinedValue());
  }

  // `Promise.all` Resolve Element Functions
  // Step 9. Set values[index] to x.
  //
  // `Promise.allSettled` Resolve Element Functions
  // `Promise.allSettled` Reject Element Functions
  // Step 12. Set values[index] to obj.
  //
  // `Promise.any` Reject Element Functions
  // Step 9. Set errors[index] to x.
  //
  // These handler functions are always created in the compartment of the
  // Promise.all/allSettled/any function, which isn't necessarily the same
  // compartment as unwrappedArray as explained in NewPromiseCombinatorElements.
  // So before storing |val| we may need to enter unwrappedArray's compartment.
  [[nodiscard]] bool setElement(JSContext* cx, uint32_t index,
                                HandleValue val) {
    // The index is guaranteed to be initialized to `undefined`.
    MOZ_ASSERT(unwrappedArray()->getDenseElement(index).isUndefined());

    if (elements().setElementNeedsWrapping) {
      AutoRealm ar(cx, unwrappedArray());

      RootedValue rootedVal(cx, val);
      if (!cx->compartment()->wrap(cx, &rootedVal)) {
        return false;
      }
      unwrappedArray()->setDenseElement(index, rootedVal);
    } else {
      unwrappedArray()->setDenseElement(index, val);
    }
    return true;
  }
};

}  // namespace js

PromiseCombinatorDataHolder* PromiseCombinatorDataHolder::New(
    JSContext* cx, JS::Handle<JSObject*> resultPromise,
    JS::Handle<PromiseCombinatorElements> elements,
    JS::Handle<JSObject*> resolveOrReject) {
  auto* dataHolder = NewBuiltinClassInstance<PromiseCombinatorDataHolder>(cx);
  if (!dataHolder) {
    return nullptr;
  }

  cx->check(resultPromise, elements.value(), resolveOrReject);

  dataHolder->initFixedSlot(Slot_Promise, ObjectValue(*resultPromise));
  dataHolder->initFixedSlot(Slot_RemainingElements, Int32Value(1));
  dataHolder->initFixedSlot(Slot_ValuesArray, elements.value());
  dataHolder->initFixedSlot(Slot_ResolveOrRejectFunction,
                            ObjectValue(*resolveOrReject));
  return dataHolder;
}

PromiseCombinatorKeyedDataHolder* PromiseCombinatorKeyedDataHolder::New(
    JSContext* cx, JS::Handle<JSObject*> resultPromise,
    JS::Handle<ListObject*> keys, JS::Handle<ListObject*> values,
    JS::Handle<JSObject*> resolveOrReject) {
  auto* dataHolder =
      NewBuiltinClassInstance<PromiseCombinatorKeyedDataHolder>(cx);
  if (!dataHolder) {
    return nullptr;
  }

  cx->check(resultPromise);
  cx->check(keys);
  cx->check(values);
  cx->check(resolveOrReject);

  dataHolder->setFixedSlot(Slot_Promise, ObjectValue(*resultPromise));
  dataHolder->setFixedSlot(Slot_RemainingElements, Int32Value(1));
  dataHolder->setFixedSlot(Slot_ValuesArray, ObjectValue(*values));
  dataHolder->setFixedSlot(Slot_ResolveOrRejectFunction,
                           ObjectValue(*resolveOrReject));
  dataHolder->setFixedSlot(Slot_KeysList, ObjectValue(*keys));
  return dataHolder;
}

namespace {
// Generator used by PromiseObject::getID.
mozilla::Atomic<uint64_t> gIDGenerator(0);
}  // namespace

// Returns true if the following properties haven't been mutated:
// - On the original Promise.prototype object: "constructor" and "then"
// - On the original Promise constructor: "resolve" and @@species
static bool HasDefaultPromiseProperties(JSContext* cx) {
  return cx->realm()->realmFuses.optimizePromiseLookupFuse.intact();
}

static bool IsPromiseWithDefaultProperties(PromiseObject* promise,
                                           JSContext* cx) {
  if (!HasDefaultPromiseProperties(cx)) {
    return false;
  }

  // Ensure the promise's prototype is the original Promise.prototype object.
  JSObject* proto = cx->global()->maybeGetPrototype(JSProto_Promise);
  if (!proto || promise->staticPrototype() != proto) {
    return false;
  }

  // Ensure `promise` doesn't define any own properties. This serves as a
  // quick check to make sure `promise` doesn't define an own "constructor"
  // or "then" property which may shadow Promise.prototype.constructor or
  // Promise.prototype.then.
  return promise->empty();
}

class PromiseDebugInfo : public NativeObject {
 private:
  enum Slots {
    Slot_AllocationSite,
    Slot_ResolutionSite,
    Slot_AllocationTime,
    Slot_ResolutionTime,
    Slot_Id,
    SlotCount
  };

 public:
  static const JSClass class_;
  static PromiseDebugInfo* create(JSContext* cx,
                                  Handle<PromiseObject*> promise) {
    Rooted<PromiseDebugInfo*> debugInfo(
        cx, NewBuiltinClassInstance<PromiseDebugInfo>(cx));
    if (!debugInfo) {
      return nullptr;
    }

    RootedObject stack(cx);
    if (!JS::CaptureCurrentStack(cx, &stack,
                                 JS::StackCapture(JS::AllFrames()))) {
      return nullptr;
    }
    debugInfo->setFixedSlot(Slot_AllocationSite, ObjectOrNullValue(stack));
    debugInfo->setFixedSlot(Slot_ResolutionSite, NullValue());
    debugInfo->setFixedSlot(Slot_AllocationTime,
                            DoubleValue(MillisecondsSinceStartup()));
    debugInfo->setFixedSlot(Slot_ResolutionTime, NumberValue(0));
    promise->setFixedSlotTyped(PromiseObject::DEBUG_INFO_SLOT,
                               ObjectValue(*debugInfo));

    return debugInfo;
  }

  static PromiseDebugInfo* FromPromise(PromiseObject* promise) {
    Value val = promise->getFixedSlotTyped(PromiseObject::DEBUG_INFO_SLOT);
    if (val.isObject()) {
      return &val.toObject().as<PromiseDebugInfo>();
    }
    return nullptr;
  }

  /**
   * Returns the given PromiseObject's process-unique ID.
   * The ID is lazily assigned when first queried, and then either stored
   * in the DebugInfo slot if no debug info was recorded for this Promise,
   * or in the Id slot of the DebugInfo object.
   */
  static uint64_t id(PromiseObject* promise) {
    Value idVal(promise->getFixedSlotTyped(PromiseObject::DEBUG_INFO_SLOT));
    if (idVal.isUndefined()) {
      idVal.setDouble(++gIDGenerator);
      promise->setFixedSlotTyped(PromiseObject::DEBUG_INFO_SLOT, idVal);
    } else if (idVal.isObject()) {
      PromiseDebugInfo* debugInfo = FromPromise(promise);
      idVal = debugInfo->getFixedSlot(Slot_Id);
      if (idVal.isUndefined()) {
        idVal.setDouble(++gIDGenerator);
        debugInfo->setFixedSlot(Slot_Id, idVal);
      }
    }
    return uint64_t(idVal.toNumber());
  }

  double allocationTime() {
    return getFixedSlot(Slot_AllocationTime).toNumber();
  }
  double resolutionTime() {
    return getFixedSlot(Slot_ResolutionTime).toNumber();
  }
  JSObject* allocationSite() {
    return getFixedSlot(Slot_AllocationSite).toObjectOrNull();
  }
  JSObject* resolutionSite() {
    return getFixedSlot(Slot_ResolutionSite).toObjectOrNull();
  }

  // The |unwrappedRejectionStack| parameter should only be set on promise
  // rejections and should be the stack of the exception that caused the promise
  // to be rejected. If the |unwrappedRejectionStack| is null, the current stack
  // will be used instead. This is also the default behavior for fulfilled
  // promises.
  static void setResolutionInfo(JSContext* cx, Handle<PromiseObject*> promise,
                                Handle<SavedFrame*> unwrappedRejectionStack) {
    MOZ_ASSERT_IF(unwrappedRejectionStack,
                  promise->state() == JS::PromiseState::Rejected);

    if (!JS::IsAsyncStackCaptureEnabledForRealm(cx)) {
      return;
    }

    // If async stacks weren't enabled and the Promise's global wasn't a
    // debuggee when the Promise was created, we won't have a debugInfo
    // object. We still want to capture the resolution stack, so we
    // create the object now and change it's slots' values around a bit.
    Rooted<PromiseDebugInfo*> debugInfo(cx, FromPromise(promise));
    if (!debugInfo) {
      RootedValue idVal(
          cx, promise->getFixedSlotTyped(PromiseObject::DEBUG_INFO_SLOT));
      debugInfo = create(cx, promise);
      if (!debugInfo) {
        cx->clearPendingException();
        return;
      }

      // The current stack was stored in the AllocationSite slot, move
      // it to ResolutionSite as that's what it really is.
      debugInfo->setFixedSlot(Slot_ResolutionSite,
                              debugInfo->getFixedSlot(Slot_AllocationSite));
      debugInfo->setFixedSlot(Slot_AllocationSite, NullValue());

      // There's no good default for a missing AllocationTime, so
      // instead of resetting that, ensure that it's the same as
      // ResolutionTime, so that the diff shows as 0, which isn't great,
      // but bearable.
      debugInfo->setFixedSlot(Slot_ResolutionTime,
                              debugInfo->getFixedSlot(Slot_AllocationTime));

      // The Promise's ID might've been queried earlier, in which case
      // it's stored in the DebugInfo slot. We saved that earlier, so
      // now we can store it in the right place (or leave it as
      // undefined if it wasn't ever initialized.)
      debugInfo->setFixedSlot(Slot_Id, idVal);
      return;
    }

    RootedObject stack(cx, unwrappedRejectionStack);
    if (stack) {
      // The exception stack is always unwrapped so it might be in
      // a different compartment.
      if (!cx->compartment()->wrap(cx, &stack)) {
        cx->clearPendingException();
        return;
      }
    } else {
      if (!JS::CaptureCurrentStack(cx, &stack,
                                   JS::StackCapture(JS::AllFrames()))) {
        cx->clearPendingException();
        return;
      }
    }

    debugInfo->setFixedSlot(Slot_ResolutionSite, ObjectOrNullValue(stack));
    debugInfo->setFixedSlot(Slot_ResolutionTime,
                            DoubleValue(MillisecondsSinceStartup()));
  }

#if defined(DEBUG) || defined(JS_JITSPEW)
  void dumpOwnFields(js::JSONPrinter& json) const;
#endif
};

const JSClass PromiseDebugInfo::class_ = {
    "PromiseDebugInfo",
    JSCLASS_HAS_RESERVED_SLOTS(SlotCount),
};

double PromiseObject::allocationTime() {
  auto debugInfo = PromiseDebugInfo::FromPromise(this);
  if (debugInfo) {
    return debugInfo->allocationTime();
  }
  return 0;
}

double PromiseObject::resolutionTime() {
  auto debugInfo = PromiseDebugInfo::FromPromise(this);
  if (debugInfo) {
    return debugInfo->resolutionTime();
  }
  return 0;
}

JSObject* PromiseObject::allocationSite() {
  auto debugInfo = PromiseDebugInfo::FromPromise(this);
  if (debugInfo) {
    return debugInfo->allocationSite();
  }
  return nullptr;
}

JSObject* PromiseObject::resolutionSite() {
  auto debugInfo = PromiseDebugInfo::FromPromise(this);
  if (debugInfo) {
    JSObject* site = debugInfo->resolutionSite();
    if (site && !JS_IsDeadWrapper(site)) {
      MOZ_ASSERT(UncheckedUnwrap(site)->is<SavedFrame>());
      return site;
    }
  }
  return nullptr;
}

/**
 * Wrapper for GetAndClearExceptionAndStack that handles cases where
 * no exception is pending, but an error occurred.
 * This can be the case if an OOM was encountered while throwing the error.
 */
static bool MaybeGetAndClearExceptionAndStack(
    JSContext* cx, MutableHandleValue rval, MutableHandle<SavedFrame*> stack) {
  if (!cx->isExceptionPending()) {
    return false;
  }

  return GetAndClearExceptionAndStack(cx, rval, stack);
}

[[nodiscard]] static bool CallPromiseRejectFunction(
    JSContext* cx, HandleObject rejectFun, HandleValue reason,
    HandleObject promiseObj, Handle<SavedFrame*> unwrappedRejectionStack,
    UnhandledRejectionBehavior behavior);

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * IfAbruptRejectPromise ( value, capability )
 * https://tc39.es/ecma262/#sec-ifabruptrejectpromise
 *
 * Steps 1.a-b.
 *
 * Extracting all of this internal spec algorithm into a helper function would
 * be tedious, so the check in step 1 and the entirety of step 2 aren't
 * included.
 */
bool js::AbruptRejectPromise(JSContext* cx, CallArgs& args,
                             HandleObject promiseObj, HandleObject reject) {
  // Step 1.a. Perform
  //           ? Call(capability.[[Reject]], undefined, « value.[[Value]] »).
  RootedValue reason(cx);
  Rooted<SavedFrame*> stack(cx);
  if (!MaybeGetAndClearExceptionAndStack(cx, &reason, &stack)) {
    return false;
  }

  if (!CallPromiseRejectFunction(cx, reject, reason, promiseObj, stack,
                                 UnhandledRejectionBehavior::Report)) {
    return false;
  }

  // Step 1.b. Return capability.[[Promise]].
  args.rval().setObject(*promiseObj);
  return true;
}

static bool AbruptRejectPromise(JSContext* cx, CallArgs& args,
                                Handle<PromiseCapability> capability) {
  return AbruptRejectPromise(cx, args, capability.promise(),
                             capability.reject());
}

class MicroTaskEntry : public NativeObject {
 protected:
  enum Slots {
    // Shared slots:
    Promise = 0,                    // see comment in PromiseReactionRecord
    IncumbentGlobalRepresentative,  // See comment in PromiseReactionRecord
    OptionalHostDefinedData,

    // Only needed for microtask jobs
    AllocationStack,
    SlotCount,
  };

 public:
  JSObject* promise() const {
    return getFixedSlot(Slots::Promise).toObjectOrNull();
  }

  void initPromise(JSObject* obj) {
    initFixedSlot(Slots::Promise, ObjectOrNullValue(obj));
  }

  Value getIncumbentGlobalRepresentative() const {
    return getFixedSlot(Slots::IncumbentGlobalRepresentative);
  }

  void initIncumbentGlobalRepresentative(const Value& val) {
    initFixedSlot(Slots::IncumbentGlobalRepresentative, val);
  }

  Value getOptionalHostDefinedData() const {
    return getFixedSlot(Slots::OptionalHostDefinedData);
  }

  void initOptionalHostDefinedData(const Value& val) {
    initFixedSlot(Slots::OptionalHostDefinedData, val);
  }

  JSObject* allocationStack() const {
    return getFixedSlot(Slots::AllocationStack).toObjectOrNull();
  }

  void initAllocationStack(JSObject* stack) {
    initFixedSlot(Slots::AllocationStack, ObjectOrNullValue(stack));
  }

  void setAllocationStack(JSObject* stack) {
    setFixedSlot(Slots::AllocationStack, ObjectOrNullValue(stack));
  }
};

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * PromiseReaction Records
 * https://tc39.es/ecma262/#sec-promisereaction-records
 */
class PromiseReactionRecord : public MicroTaskEntry {
  // If this flag is set, this reaction record is already enqueued to the
  // job queue, and the spec's [[Type]] field is represented by
  // REACTION_FLAG_FULFILLED flag.
  //
  // If this flag isn't yet set, [[Type]] field is undefined.
  static constexpr uint32_t REACTION_FLAG_RESOLVED = 0x1;

  // This bit is valid only when REACTION_FLAG_RESOLVED flag is set.
  //
  // If this flag is set, [[Type]] field is Fulfill.
  // If this flag isn't set, [[Type]] field is Reject.
  static constexpr uint32_t REACTION_FLAG_FULFILLED = 0x2;

  // If this flag is set, this reaction record is created for resolving
  // one promise P1 to another promise P2, and
  // Slot::GeneratorOrPromiseToResolveOrAsyncFromSyncIterator slot
  // holds P2.
  static constexpr uint32_t REACTION_FLAG_DEFAULT_RESOLVING_HANDLER = 0x4;

  // If this flag is set, this reaction record is created for async function
  // and Slot::GeneratorOrPromiseToResolveOrAsyncFromSyncIterator
  // slot holds internal generator object of the async function.
  static constexpr uint32_t REACTION_FLAG_ASYNC_FUNCTION = 0x8;

  // If this flag is set, this reaction record is created for async generator
  // and Slot::GeneratorOrPromiseToResolveOrAsyncFromSyncIterator
  // slot holds the async generator object of the async generator.
  static constexpr uint32_t REACTION_FLAG_ASYNC_GENERATOR = 0x10;

  // If this flag is set, this reaction record is created only for providing
  // information to debugger.
  static constexpr uint32_t REACTION_FLAG_DEBUGGER_DUMMY = 0x20;

  // This bit is valid only when the promise object is optimized out
  // for the reaction.
  //
  // If this flag is set, unhandled rejection should be ignored.
  // Otherwise, promise object should be created on-demand for unhandled
  // rejection.
  static constexpr uint32_t REACTION_FLAG_IGNORE_UNHANDLED_REJECTION = 0x40;

  // If this flag is set, this reaction record is created for async-from-sync
  // iterators and
  // Slot::GeneratorOrPromiseToResolveOrAsyncFromSyncIterator slot
  // holds the async-from-sync iterator object.
  static constexpr uint32_t REACTION_FLAG_ASYNC_FROM_SYNC_ITERATOR = 0x80;

 public:
  enum Slots {
    // This is the promise-like object that gets resolved with the result of
    // this reaction, if any. If this reaction record was created with .then or
    // .catch, this is the promise that .then or .catch returned.
    //
    // The spec says that a PromiseReaction record has a [[Capability]] field
    // whose value is either undefined or a PromiseCapability record, but we
    // just store the PromiseCapability's fields directly in this object. This
    // is the
    // capability's [[Promise]] field; its [[Resolve]] and [[Reject]] fields are
    // stored in Slot::Resolve and Slot::Reject.
    //
    // This can be 'null' in reaction records created for a few situations:
    //
    // - When you resolve one promise to another. When you pass a promise P1 to
    //   the 'fulfill' function of a promise P2, so that resolving P1 resolves
    //   P2 in the same way, P1 gets a reaction record with the
    //   REACTION_FLAG_DEFAULT_RESOLVING_HANDLER flag set and whose
    //   Slots::GeneratorOrPromiseToResolveOrAsyncFromSyncIterator
    //   slot holds P2.
    //
    // - When you await a promise. When an async function or generator awaits a
    //   value V, then the await expression generates an internal promise P,
    //   resolves it to V, and then gives P a reaction record with the
    //   REACTION_FLAG_ASYNC_FUNCTION or REACTION_FLAG_ASYNC_GENERATOR flag set
    //   and whose Slot::GeneratorOrPromiseToResolveOrAsyncFromSyncIterator
    //   slot holds the generator object. (Typically V is a promise, so
    //   resolving P to V gives V a REACTION_FLAGS_DEFAULT_RESOLVING_HANDLER
    //   reaction
    //   record as described above.)
    //
    // - When JS::AddPromiseReactions{,IgnoringUnhandledRejection} cause the
    //   reaction to be created. (These functions act as if they had created a
    //   promise to invoke the appropriate provided reaction function, without
    //   actually allocating a promise for them.)
    Promise = MicroTaskEntry::Slots::Promise,

    // The host defined data for this reaction record. Can be null.
    // See step 5 in https://html.spec.whatwg.org/#hostmakejobcallback
    IncumbentGlobalRepresentative =
        MicroTaskEntry::Slots::IncumbentGlobalRepresentative,
    OptionalHostDefinedData = MicroTaskEntry::Slots::OptionalHostDefinedData,

    // < Invisibly here are the microtask job slots from the parent class
    // MicroTask. >

    // A slot holding an object from the realm where we need to execute
    // the reaction job. This may be a CCW. We don't store the global
    // of the realm directly because wrappers to globals can change
    // globals, which breaks code.
    EnqueueGlobalRepresentative = MicroTaskEntry::Slots::SlotCount,

    // The [[Handler]] field(s) of a PromiseReaction record. We create a
    // single reaction record for fulfillment and rejection, therefore our
    // PromiseReaction implementation needs two [[Handler]] fields.
    //
    // The slot value is either a callable object, an integer constant from
    // the |PromiseHandler| enum, or null. If the value is null, either the
    // REACTION_FLAG_DEBUGGER_DUMMY or the
    // REACTION_FLAG_DEFAULT_RESOLVING_HANDLER flag must be set.
    //
    // After setting the target state for a PromiseReaction, the slot of the
    // no longer used handler gets reused to store the argument of the active
    // handler.
    OnFulfilled,
    OnRejectedArg = OnFulfilled,
    OnRejected,
    OnFulfilledArg = OnRejected,

    // The functions to resolve or reject the promise. Matches the
    // [[Capability]].[[Resolve]] and [[Capability]].[[Reject]] fields from
    // the spec.
    //
    // The slot values are either callable objects or null, but the latter
    // case is only allowed if the promise is either a built-in Promise object
    // or null.
    Resolve,
    Reject,

    // Bitmask of the REACTION_FLAG values.
    Flags,

    // Additional slot to store extra data for specific reaction record types.
    //
    // - When the REACTION_FLAG_ASYNC_FUNCTION flag is set, this slot stores
    //   the (internal) generator object for this promise reaction.
    // - When the REACTION_FLAG_ASYNC_GENERATOR flag is set, this slot stores
    //   the async generator object for this promise reaction.
    // - When the REACTION_FLAG_DEFAULT_RESOLVING_HANDLER flag is set, this
    //   slot stores the promise to resolve when conceptually "calling" the
    //   OnFulfilled or OnRejected handlers.
    // - When the REACTION_FLAG_ASYNC_FROM_SYNC_ITERATOR is set, this slot
    // stores
    //   the async-from-sync iterator object.
    GeneratorOrPromiseToResolveOrAsyncFromSyncIterator,

    SlotCount,
  };

 private:
  template <typename KnownF, typename UnknownF>
  static void forEachReactionFlag(uint32_t flags, KnownF known,
                                  UnknownF unknown);

  void setFlagOnInitialState(uint32_t flag) {
    int32_t flags = this->flags();
    MOZ_ASSERT(flags == 0, "Can't modify with non-default flags");
    flags |= flag;
    setFixedSlot(Slots::Flags, Int32Value(flags));
  }

  uint32_t handlerSlot() {
    MOZ_ASSERT(targetState() != JS::PromiseState::Pending);
    return targetState() == JS::PromiseState::Fulfilled ? Slots::OnFulfilled
                                                        : Slots::OnRejected;
  }

  uint32_t handlerArgSlot() {
    MOZ_ASSERT(targetState() != JS::PromiseState::Pending);
    return targetState() == JS::PromiseState::Fulfilled ? Slots::OnFulfilledArg
                                                        : Slots::OnRejectedArg;
  }

 public:
  static const JSClass class_;

  int32_t flags() const { return getFixedSlot(Slots::Flags).toInt32(); }
  JS::PromiseState targetState() const {
    int32_t flags = this->flags();
    if (!(flags & REACTION_FLAG_RESOLVED)) {
      return JS::PromiseState::Pending;
    }
    return flags & REACTION_FLAG_FULFILLED ? JS::PromiseState::Fulfilled
                                           : JS::PromiseState::Rejected;
  }
  void setTargetStateAndHandlerArg(JS::PromiseState state, const Value& arg) {
    MOZ_ASSERT(targetState() == JS::PromiseState::Pending);
    MOZ_ASSERT(state != JS::PromiseState::Pending,
               "Can't revert a reaction to pending.");

    int32_t flags = this->flags();
    flags |= REACTION_FLAG_RESOLVED;
    if (state == JS::PromiseState::Fulfilled) {
      flags |= REACTION_FLAG_FULFILLED;
    }

    setFixedSlot(Slots::Flags, Int32Value(flags));
    setFixedSlot(handlerArgSlot(), arg);
  }

  void setShouldIgnoreUnhandledRejection() {
    setFlagOnInitialState(REACTION_FLAG_IGNORE_UNHANDLED_REJECTION);
  }
  UnhandledRejectionBehavior unhandledRejectionBehavior() const {
    int32_t flags = this->flags();
    return (flags & REACTION_FLAG_IGNORE_UNHANDLED_REJECTION)
               ? UnhandledRejectionBehavior::Ignore
               : UnhandledRejectionBehavior::Report;
  }

  void setIsDefaultResolvingHandler(PromiseObject* promiseToResolve) {
    setFlagOnInitialState(REACTION_FLAG_DEFAULT_RESOLVING_HANDLER);
    setFixedSlot(Slots::GeneratorOrPromiseToResolveOrAsyncFromSyncIterator,
                 ObjectValue(*promiseToResolve));
  }
  bool isDefaultResolvingHandler() const {
    int32_t flags = this->flags();
    return flags & REACTION_FLAG_DEFAULT_RESOLVING_HANDLER;
  }
  PromiseObject* defaultResolvingPromise() {
    MOZ_ASSERT(isDefaultResolvingHandler());
    const Value& promiseToResolve =
        getFixedSlot(Slots::GeneratorOrPromiseToResolveOrAsyncFromSyncIterator);
    return &promiseToResolve.toObject().as<PromiseObject>();
  }

  void setIsAsyncFunction(AsyncFunctionGeneratorObject* genObj) {
    MOZ_ASSERT(realm() == genObj->nonCCWRealm());
    setFlagOnInitialState(REACTION_FLAG_ASYNC_FUNCTION);
    setFixedSlot(Slots::GeneratorOrPromiseToResolveOrAsyncFromSyncIterator,
                 ObjectValue(*genObj));
  }
  bool isAsyncFunction() const {
    int32_t flags = this->flags();
    return flags & REACTION_FLAG_ASYNC_FUNCTION;
  }
  AsyncFunctionGeneratorObject* asyncFunctionGenerator() {
    MOZ_ASSERT(isAsyncFunction());
    const Value& generator =
        getFixedSlot(Slots::GeneratorOrPromiseToResolveOrAsyncFromSyncIterator);
    AsyncFunctionGeneratorObject* res =
        &generator.toObject().as<AsyncFunctionGeneratorObject>();
    MOZ_RELEASE_ASSERT(realm() == res->realm());
    return res;
  }

  void setIsAsyncGenerator(AsyncGeneratorObject* generator) {
    setFlagOnInitialState(REACTION_FLAG_ASYNC_GENERATOR);
    setFixedSlot(Slots::GeneratorOrPromiseToResolveOrAsyncFromSyncIterator,
                 ObjectValue(*generator));
  }
  bool isAsyncGenerator() const {
    int32_t flags = this->flags();
    return flags & REACTION_FLAG_ASYNC_GENERATOR;
  }
  AsyncGeneratorObject* asyncGenerator() {
    MOZ_ASSERT(isAsyncGenerator());
    const Value& generator =
        getFixedSlot(Slots::GeneratorOrPromiseToResolveOrAsyncFromSyncIterator);
    return &generator.toObject().as<AsyncGeneratorObject>();
  }

  void setIsAsyncFromSyncIterator(AsyncFromSyncIteratorObject* iterator) {
    setFlagOnInitialState(REACTION_FLAG_ASYNC_FROM_SYNC_ITERATOR);
    setFixedSlot(Slots::GeneratorOrPromiseToResolveOrAsyncFromSyncIterator,
                 ObjectValue(*iterator));
  }
  bool isAsyncFromSyncIterator() const {
    int32_t flags = this->flags();
    return flags & REACTION_FLAG_ASYNC_FROM_SYNC_ITERATOR;
  }
  AsyncFromSyncIteratorObject* asyncFromSyncIterator() {
    MOZ_ASSERT(isAsyncFromSyncIterator());
    const Value& iterator =
        getFixedSlot(Slots::GeneratorOrPromiseToResolveOrAsyncFromSyncIterator);
    return &iterator.toObject().as<AsyncFromSyncIteratorObject>();
  }

  void setIsDebuggerDummy() {
    setFlagOnInitialState(REACTION_FLAG_DEBUGGER_DUMMY);
  }
  bool isDebuggerDummy() const {
    int32_t flags = this->flags();
    return flags & REACTION_FLAG_DEBUGGER_DUMMY;
  }

  Value handler() {
    MOZ_ASSERT(targetState() != JS::PromiseState::Pending);
    return getFixedSlot(handlerSlot());
  }

  // Get the handler for a target state, before the state
  // transition has happened in setTargetStateAndHandlerArg
  Value targetStateHandler(JS::PromiseState targetState) {
    MOZ_ASSERT(this->targetState() == JS::PromiseState::Pending);
    MOZ_ASSERT(targetState != JS::PromiseState::Pending);

    return getFixedSlot(targetState == JS::PromiseState::Fulfilled
                            ? Slots::OnFulfilled
                            : Slots::OnRejected);
  }
  Value handlerArg() {
    MOZ_ASSERT(targetState() != JS::PromiseState::Pending);
    return getFixedSlot(handlerArgSlot());
  }

  JSObject* enqueueGlobalRepresentative() const {
    return getFixedSlot(Slots::EnqueueGlobalRepresentative).toObjectOrNull();
  }
  void setEnqueueGlobalRepresentative(JSObject* obj) {
    setFixedSlot(Slots::EnqueueGlobalRepresentative, ObjectOrNullValue(obj));
  }

#if defined(DEBUG) || defined(JS_JITSPEW)
  void dumpOwnFields(js::JSONPrinter& json) const;
#endif
};

const JSClass PromiseReactionRecord::class_ = {
    "PromiseReactionRecord",
    JSCLASS_HAS_RESERVED_SLOTS(Slots::SlotCount),
};

class ThenableJob : public MicroTaskEntry {
 protected:
  enum Slots {
    // These slots come directoy after the MicroTaskEntry slots.
    Thenable = MicroTaskEntry::Slots::SlotCount,
    Then,
    Callback,
    SlotCount
  };

 public:
  static const JSClass class_;

  enum TargetFunction : int32_t {
    PromiseResolveThenableJob,
    PromiseResolveBuiltinThenableJob,
#ifdef NIGHTLY_BUILD
    // Job used by SafePromiseResolve (JS::SafeResolve): runs
    // PerformPromiseResolution on `promise` with the resolution value stored
    // in the Thenable slot. The Then slot is unused for this target.
    DeferredResolveJob,
#endif  // NIGHTLY_BUILD
  };

  Value thenable() const { return getFixedSlot(Slots::Thenable); }

  void initThenable(const Value& val) { initFixedSlot(Slots::Thenable, val); }

  JSObject* then() const { return getFixedSlot(Slots::Then).toObjectOrNull(); }

  void initThen(JSObject* obj) {
    initFixedSlot(Slots::Then, ObjectOrNullValue(obj));
  }

  TargetFunction targetFunction() const {
    return static_cast<TargetFunction>(getFixedSlot(Slots::Callback).toInt32());
  }
  void initTargetFunction(TargetFunction target) {
    initFixedSlot(Slots::Callback,
                  JS::Int32Value(static_cast<int32_t>(target)));
  }
};

const JSClass ThenableJob::class_ = {
    "ThenableJob",
    JSCLASS_HAS_RESERVED_SLOTS(ThenableJob::SlotCount),
};

ThenableJob* NewThenableJob(JSContext* cx, ThenableJob::TargetFunction target,
                            HandleObject promise, HandleValue thenable,
                            HandleObject then,
                            HandleObject incumbentGlobalRepresentative,
                            HandleObject optionalHostDefinedData) {
  cx->check(optionalHostDefinedData);
  // MG:XXX: Boy isn't it silly that we have to root here, only to get the
  // allocation site...
  RootedObject stack(
      cx, JS::MaybeGetPromiseAllocationSiteFromPossiblyWrappedPromise(promise));
  if (!cx->compartment()->wrap(cx, &stack)) {
    return nullptr;
  }

  auto* job = NewBuiltinClassInstance<ThenableJob>(cx);
  if (!job) {
    return nullptr;
  }

  job->initPromise(promise);
  job->initThen(then);
  job->initThenable(thenable);
  job->initTargetFunction(target);
  job->initIncumbentGlobalRepresentative(
      ObjectOrNullValue(incumbentGlobalRepresentative));
  job->initOptionalHostDefinedData(ObjectOrNullValue(optionalHostDefinedData));
  job->initAllocationStack(stack);

  return job;
}

static void AddPromiseFlags(PromiseObject& promise, int32_t flag) {
  int32_t flags = promise.flags();
  promise.setFixedSlotTyped(PromiseObject::FLAGS_SLOT,
                            Int32Value(flags | flag));
}

static void RemovePromiseFlags(PromiseObject& promise, int32_t flag) {
  int32_t flags = promise.flags();
  promise.setFixedSlotTyped(PromiseObject::FLAGS_SLOT,
                            Int32Value(flags & ~flag));
}

static bool PromiseHasAnyFlag(PromiseObject& promise, int32_t flag) {
  return promise.flags() & flag;
}

static bool ResolvePromiseFunction(JSContext* cx, unsigned argc, Value* vp);
static bool RejectPromiseFunction(JSContext* cx, unsigned argc, Value* vp);

static JSFunction* GetResolveFunctionFromReject(JSFunction* reject);
static JSFunction* GetRejectFunctionFromResolve(JSFunction* resolve);
static JSFunction* GetResolveFunctionFromPromise(PromiseObject* promise);

#ifdef DEBUG

/**
 * Returns Promise Resolve Function's [[AlreadyResolved]].[[Value]].
 */
static bool IsAlreadyResolvedResolveFunction(JSFunction* resolveFun) {
  MOZ_ASSERT(resolveFun->maybeNative() == ResolvePromiseFunction);

  bool alreadyResolved =
      resolveFun->getExtendedSlot(ResolveFunctionSlot_Promise).isUndefined();

  // Other slots should agree.
  if (alreadyResolved) {
    MOZ_ASSERT(resolveFun->getExtendedSlot(ResolveFunctionSlot_RejectFunction)
                   .isUndefined());
  } else {
    JSFunction* rejectFun = GetRejectFunctionFromResolve(resolveFun);
    MOZ_ASSERT(
        !rejectFun->getExtendedSlot(RejectFunctionSlot_Promise).isUndefined());
    MOZ_ASSERT(!rejectFun->getExtendedSlot(RejectFunctionSlot_ResolveFunction)
                    .isUndefined());
  }

  return alreadyResolved;
}

/**
 * Returns Promise Reject Function's [[AlreadyResolved]].[[Value]].
 */
static bool IsAlreadyResolvedRejectFunction(JSFunction* rejectFun) {
  MOZ_ASSERT(rejectFun->maybeNative() == RejectPromiseFunction);

  bool alreadyResolved =
      rejectFun->getExtendedSlot(RejectFunctionSlot_Promise).isUndefined();

  // Other slots should agree.
  if (alreadyResolved) {
    MOZ_ASSERT(rejectFun->getExtendedSlot(RejectFunctionSlot_ResolveFunction)
                   .isUndefined());
  } else {
    JSFunction* resolveFun = GetResolveFunctionFromReject(rejectFun);
    MOZ_ASSERT(!resolveFun->getExtendedSlot(ResolveFunctionSlot_Promise)
                    .isUndefined());
    MOZ_ASSERT(!resolveFun->getExtendedSlot(ResolveFunctionSlot_RejectFunction)
                    .isUndefined());
  }

  return alreadyResolved;
}

#endif  // DEBUG

/**
 * Set Promise Resolve Function's and Promise Reject Function's
 * [[AlreadyResolved]].[[Value]] to true.
 *
 * `resolutionFun` can be either of them.
 */
static void SetAlreadyResolvedResolutionFunction(JSFunction* resolutionFun) {
  JSFunction* resolve;
  JSFunction* reject;
  if (resolutionFun->maybeNative() == ResolvePromiseFunction) {
    resolve = resolutionFun;
    reject = GetRejectFunctionFromResolve(resolutionFun);
  } else {
    resolve = GetResolveFunctionFromReject(resolutionFun);
    reject = resolutionFun;
  }

  resolve->setExtendedSlot(ResolveFunctionSlot_Promise, UndefinedValue());
  resolve->setExtendedSlot(ResolveFunctionSlot_RejectFunction,
                           UndefinedValue());

  reject->setExtendedSlot(RejectFunctionSlot_Promise, UndefinedValue());
  reject->setExtendedSlot(RejectFunctionSlot_ResolveFunction, UndefinedValue());

  MOZ_ASSERT(IsAlreadyResolvedResolveFunction(resolve));
  MOZ_ASSERT(IsAlreadyResolvedRejectFunction(reject));
}

/**
 * Returns true if given promise is created by
 * CreatePromiseObjectWithoutResolutionFunctions.
 */
bool js::IsPromiseWithDefaultResolvingFunction(PromiseObject* promise) {
  return PromiseHasAnyFlag(*promise, PROMISE_FLAG_DEFAULT_RESOLVING_FUNCTIONS);
}

/**
 * Returns Promise Resolve Function's [[AlreadyResolved]].[[Value]] for
 * a promise created by CreatePromiseObjectWithoutResolutionFunctions.
 */
static bool IsAlreadyResolvedPromiseWithDefaultResolvingFunction(
    PromiseObject* promise) {
  MOZ_ASSERT(IsPromiseWithDefaultResolvingFunction(promise));

  if (promise->as<PromiseObject>().state() != JS::PromiseState::Pending) {
    MOZ_ASSERT(PromiseHasAnyFlag(
        *promise, PROMISE_FLAG_DEFAULT_RESOLVING_FUNCTIONS_ALREADY_RESOLVED));
    return true;
  }

  return PromiseHasAnyFlag(
      *promise, PROMISE_FLAG_DEFAULT_RESOLVING_FUNCTIONS_ALREADY_RESOLVED);
}

/**
 * Set Promise Resolve Function's [[AlreadyResolved]].[[Value]] to true for
 * a promise created by CreatePromiseObjectWithoutResolutionFunctions.
 */
void js::SetAlreadyResolvedPromiseWithDefaultResolvingFunction(
    PromiseObject* promise) {
  MOZ_ASSERT(IsPromiseWithDefaultResolvingFunction(promise));

  promise->setFixedSlotTyped(
      PromiseObject::FLAGS_SLOT,
      JS::Int32Value(
          promise->flags() |
          PROMISE_FLAG_DEFAULT_RESOLVING_FUNCTIONS_ALREADY_RESOLVED));
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * CreateResolvingFunctions ( promise )
 * https://tc39.es/ecma262/#sec-createresolvingfunctions
 */
[[nodiscard]] static MOZ_ALWAYS_INLINE bool CreateResolvingFunctions(
    JSContext* cx, HandleObject promise, MutableHandleObject resolveFn,
    MutableHandleObject rejectFn) {
  // Step 1. Let alreadyResolved be the Record { [[Value]]: false }.
  // (implicit, see steps 5-6, 10-11 below)

  // Step 2. Let stepsResolve be the algorithm steps defined in Promise Resolve
  //         Functions.
  // Step 3. Let lengthResolve be the number of non-optional parameters of the
  //         function definition in Promise Resolve Functions.
  // Step 4. Let resolve be
  //         ! CreateBuiltinFunction(stepsResolve, lengthResolve, "",
  //                                 « [[Promise]], [[AlreadyResolved]] »).
  Handle<PropertyName*> funName = cx->names().empty_;
  resolveFn.set(NewNativeFunction(cx, ResolvePromiseFunction, 1, funName,
                                  gc::AllocKind::FUNCTION_EXTENDED,
                                  GenericObject));
  if (!resolveFn) {
    return false;
  }

  // Step 7. Let stepsReject be the algorithm steps defined in Promise Reject
  //         Functions.
  // Step 8. Let lengthReject be the number of non-optional parameters of the
  //         function definition in Promise Reject Functions.
  // Step 9. Let reject be
  //         ! CreateBuiltinFunction(stepsReject, lengthReject, "",
  //                                 « [[Promise]], [[AlreadyResolved]] »).
  rejectFn.set(NewNativeFunction(cx, RejectPromiseFunction, 1, funName,
                                 gc::AllocKind::FUNCTION_EXTENDED,
                                 GenericObject));
  if (!rejectFn) {
    return false;
  }

  JSFunction* resolveFun = &resolveFn->as<JSFunction>();
  JSFunction* rejectFun = &rejectFn->as<JSFunction>();

  // Step 5. Set resolve.[[Promise]] to promise.
  // Step 6. Set resolve.[[AlreadyResolved]] to alreadyResolved.
  //
  // NOTE: We use these references as [[AlreadyResolved]].[[Value]].
  //       See the comment in ResolveFunctionSlots for more details.
  resolveFun->initExtendedSlot(ResolveFunctionSlot_Promise,
                               ObjectValue(*promise));
  resolveFun->initExtendedSlot(ResolveFunctionSlot_RejectFunction,
                               ObjectValue(*rejectFun));

  // Step 10. Set reject.[[Promise]] to promise.
  // Step 11. Set reject.[[AlreadyResolved]] to alreadyResolved.
  //
  // NOTE: We use these references as [[AlreadyResolved]].[[Value]].
  //       See the comment in ResolveFunctionSlots for more details.
  rejectFun->initExtendedSlot(RejectFunctionSlot_Promise,
                              ObjectValue(*promise));
  rejectFun->initExtendedSlot(RejectFunctionSlot_ResolveFunction,
                              ObjectValue(*resolveFun));

  MOZ_ASSERT(!IsAlreadyResolvedResolveFunction(resolveFun));
  MOZ_ASSERT(!IsAlreadyResolvedRejectFunction(rejectFun));

  // Step 12. Return the Record { [[Resolve]]: resolve, [[Reject]]: reject }.
  return true;
}

static bool IsSettledMaybeWrappedPromise(JSObject* promise) {
  if (IsProxy(promise)) {
    promise = UncheckedUnwrap(promise);

    // Caller needs to handle dead wrappers.
    if (JS_IsDeadWrapper(promise)) {
      return false;
    }
  }

  return promise->as<PromiseObject>().state() != JS::PromiseState::Pending;
}

[[nodiscard]] static bool RejectMaybeWrappedPromise(
    JSContext* cx, HandleObject promiseObj, HandleValue reason,
    Handle<SavedFrame*> unwrappedRejectionStack);

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Promise Reject Functions
 * https://tc39.es/ecma262/#sec-promise-reject-functions
 */
static bool RejectPromiseFunction(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  JSFunction* reject = &args.callee().as<JSFunction>();
  HandleValue reasonVal = args.get(0);

  // Step 1. Let F be the active function object.
  // Step 2. Assert: F has a [[Promise]] internal slot whose value is an Object.
  // (implicit)

  // Step 3. Let promise be F.[[Promise]].
  const Value& promiseVal = reject->getExtendedSlot(RejectFunctionSlot_Promise);

  // Step 4. Let alreadyResolved be F.[[AlreadyResolved]].
  // Step 5. If alreadyResolved.[[Value]] is true, return undefined.
  //
  // If the Promise isn't available anymore, it has been resolved and the
  // reference to it removed to make it eligible for collection.
  bool alreadyResolved = promiseVal.isUndefined();
  MOZ_ASSERT(IsAlreadyResolvedRejectFunction(reject) == alreadyResolved);
  if (alreadyResolved) {
    args.rval().setUndefined();
    return true;
  }

  RootedObject promise(cx, &promiseVal.toObject());

  // Step 6. Set alreadyResolved.[[Value]] to true.
  SetAlreadyResolvedResolutionFunction(reject);

  // In some cases the Promise reference on the resolution function won't
  // have been removed during resolution, so we need to check that here,
  // too.
  if (IsSettledMaybeWrappedPromise(promise)) {
    args.rval().setUndefined();
    return true;
  }

  // Step 7. Return RejectPromise(promise, reason).
  if (!RejectMaybeWrappedPromise(cx, promise, reasonVal, nullptr)) {
    return false;
  }
  args.rval().setUndefined();
  return true;
}

[[nodiscard]] static bool FulfillMaybeWrappedPromise(JSContext* cx,
                                                     HandleObject promiseObj,
                                                     HandleValue value_);

[[nodiscard]] static bool EnqueuePromiseResolveThenableJob(
    JSContext* cx, HandleValue promiseToResolve, HandleValue thenable,
    HandleValue thenVal);

[[nodiscard]] static bool EnqueuePromiseResolveThenableBuiltinJob(
    JSContext* cx, HandleObject promiseToResolve, HandleObject thenable);

static bool Promise_then_impl(JSContext* cx, HandleValue promiseVal,
                              HandleValue onFulfilled, HandleValue onRejected,
                              MutableHandleValue rval, bool rvalExplicitlyUsed);

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Promise Resolve Functions
 * https://tc39.es/ecma262/#sec-promise-resolve-functions
 *
 * Steps 7-15.
 */
[[nodiscard]] bool js::ResolvePromiseInternal(
    JSContext* cx, JS::Handle<JSObject*> promise,
    JS::Handle<JS::Value> resolutionVal) {
  cx->check(promise, resolutionVal);
  MOZ_ASSERT(!IsSettledMaybeWrappedPromise(promise));

  RootedTuple<JSObject*, Value, SavedFrame*, Value, Value> roots(cx);

  // (reordered)
  // Step 8. If Type(resolution) is not Object, then
  if (!resolutionVal.isObject()) {
    // Step 8.a. Return FulfillPromise(promise, resolution).
    return FulfillMaybeWrappedPromise(cx, promise, resolutionVal);
  }

  RootedField<JSObject*, 0> resolution(roots, &resolutionVal.toObject());

  // Step 7. If SameValue(resolution, promise) is true, then
  if (resolution == promise) {
    // Step 7.a. Let selfResolutionError be a newly created TypeError object.
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_CANNOT_RESOLVE_PROMISE_WITH_ITSELF);
    RootedField<Value, 1> selfResolutionError(roots);
    RootedField<SavedFrame*, 2> stack(roots);
    if (!MaybeGetAndClearExceptionAndStack(cx, &selfResolutionError, &stack)) {
      return false;
    }

    // Step 7.b. Return RejectPromise(promise, selfResolutionError).
    return RejectMaybeWrappedPromise(cx, promise, selfResolutionError, stack);
  }

  // Step 9. Let then be Get(resolution, "then").
  RootedField<Value, 1> thenVal(roots);
  bool status =
      GetProperty(cx, resolution, resolutionVal, cx->names().then, &thenVal);

  RootedField<Value, 3> error(roots);
  RootedField<SavedFrame*, 2> errorStack(roots);

  // Step 10. If then is an abrupt completion, then
  if (!status) {
    // Get the `then.[[Value]]` value used in the step 10.a.
    if (!MaybeGetAndClearExceptionAndStack(cx, &error, &errorStack)) {
      return false;
    }
  }

  // Testing functions allow to directly settle a promise without going
  // through the resolving functions. In that case the normal bookkeeping to
  // ensure only pending promises can be resolved doesn't apply and we need
  // to manually check for already settled promises. The exception is simply
  // dropped when this case happens.
  if (IsSettledMaybeWrappedPromise(promise)) {
    return true;
  }

  // Step 10. If then is an abrupt completion, then
  if (!status) {
    // Step 10.a. Return RejectPromise(promise, then.[[Value]]).
    return RejectMaybeWrappedPromise(cx, promise, error, errorStack);
  }

  // Step 11. Let thenAction be then.[[Value]].
  // (implicit)

  // Step 12. If IsCallable(thenAction) is false, then
  if (!IsCallable(thenVal)) {
    // Step 12.a. Return FulfillPromise(promise, resolution).
    return FulfillMaybeWrappedPromise(cx, promise, resolutionVal);
  }

  // Step 13. Let thenJobCallback be HostMakeJobCallback(thenAction).
  // (implicit)

  // Step 14. Let job be
  //          NewPromiseResolveThenableJob(promise, resolution,
  //                                       thenJobCallback).
  // Step 15. Perform HostEnqueuePromiseJob(job.[[Job]], job.[[Realm]]).

  // If the resolution object is a built-in Promise object and the
  // `then` property is the original Promise.prototype.then function
  // from the current realm, we skip storing/calling it.
  // Additionally we require that |promise| itself is also a built-in
  // Promise object, so the fast path doesn't need to cope with wrappers.
  bool isBuiltinThen = false;
  if (resolution->is<PromiseObject>() && promise->is<PromiseObject>() &&
      IsNativeFunction(thenVal, Promise_then) &&
      thenVal.toObject().as<JSFunction>().realm() == cx->realm()) {
    isBuiltinThen = true;
  }

  if (!isBuiltinThen) {
    RootedField<Value, 4> promiseVal(roots, ObjectValue(*promise));
    if (!EnqueuePromiseResolveThenableJob(cx, promiseVal, resolutionVal,
                                          thenVal)) {
      return false;
    }
  } else {
    if (!EnqueuePromiseResolveThenableBuiltinJob(cx, promise, resolution)) {
      return false;
    }
  }

  return true;
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Promise Resolve Functions
 * https://tc39.es/ecma262/#sec-promise-resolve-functions
 */
static bool ResolvePromiseFunction(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Step 1. Let F be the active function object.
  // Step 2. Assert: F has a [[Promise]] internal slot whose value is an Object.
  // (implicit)

  JSFunction* resolve = &args.callee().as<JSFunction>();
  HandleValue resolutionVal = args.get(0);

  // Step 3. Let promise be F.[[Promise]].
  const Value& promiseVal =
      resolve->getExtendedSlot(ResolveFunctionSlot_Promise);

  // Step 4. Let alreadyResolved be F.[[AlreadyResolved]].
  // Step 5. If alreadyResolved.[[Value]] is true, return undefined.
  //
  // NOTE: We use the reference to the reject function as [[AlreadyResolved]].
  bool alreadyResolved = promiseVal.isUndefined();
  MOZ_ASSERT(IsAlreadyResolvedResolveFunction(resolve) == alreadyResolved);
  if (alreadyResolved) {
    args.rval().setUndefined();
    return true;
  }

  RootedObject promise(cx, &promiseVal.toObject());

  // Step 6. Set alreadyResolved.[[Value]] to true.
  SetAlreadyResolvedResolutionFunction(resolve);

  // In some cases the Promise reference on the resolution function won't
  // have been removed during resolution, so we need to check that here,
  // too.
  if (IsSettledMaybeWrappedPromise(promise)) {
    args.rval().setUndefined();
    return true;
  }

  // Steps 7-15.
  if (!ResolvePromiseInternal(cx, promise, resolutionVal)) {
    return false;
  }

  // Step 16. Return undefined.
  args.rval().setUndefined();
  return true;
}

static bool EnqueueJob(JSContext* cx, JS::JSMicroTask* job) {
  MOZ_ASSERT(cx->realm());
  GeckoProfilerRuntime& profiler = cx->runtime()->geckoProfiler();
  if (MOZ_UNLIKELY(profiler.enabled())) {
    // Emit a flow start marker here.
    uint64_t uid = 0;
    if (JS::GetFlowIdFromJSMicroTask(job, &uid)) {
      profiler.markFlow("JS::EnqueueJob", uid,
                        JS::ProfilingCategoryPair::OTHER);
    }
  }

  // Only check if we need to use the debug queue when we're not on main thread.
  if (MOZ_LIKELY(cx->runtime()->isMainRuntime())) {
    return cx->microTaskQueues->enqueueRegularMicroTask(cx, ObjectValue(*job));
  }

  // We need to root this job because useDebugQueue can GC.
  Rooted<JS::JSMicroTask*> rootedJob(cx, job);
  if (MOZ_UNLIKELY(cx->jobQueue->useDebugQueue(cx->global()))) {
    return cx->microTaskQueues->enqueueDebugMicroTask(cx,
                                                      ObjectValue(*rootedJob));
  }

  return cx->microTaskQueues->enqueueRegularMicroTask(cx,
                                                      ObjectValue(*rootedJob));
}

// This traces the paths in EnqueuePromiseReactionJobCrossRealm where you'd
// actually change realms.
static bool CanUseSameRealmEnqueue(JSContext* cx, HandleObject reactionObj,
                                   JS::PromiseState targetState) {
  if (IsProxy(reactionObj)) {
    return false;
  }

  MOZ_RELEASE_ASSERT(reactionObj->is<PromiseReactionRecord>());
  PromiseReactionRecord* reaction = &reactionObj->as<PromiseReactionRecord>();
  if (cx->realm() != reaction->realm()) {
    return false;
  }

  // Handle only real promise objects
  JSObject* reactionPromise = reaction->promise();
  if (reactionPromise && !reactionPromise->is<PromiseObject>()) {
    return false;
  }

  Value targetHandler = reaction->targetStateHandler(targetState);

  // This mimics AutoFunctionOrCurrentRealm on handler, however we
  // don't handle anything but the simplest cases, returning false
  // at any point of complexity.
  if (targetHandler.isObject()) {
    RootedObject handlerObj(cx, &targetHandler.toObject());
    JS::Realm* handlerRealm = JS::GetFunctionRealm(cx, handlerObj);
    if (!handlerRealm) {
      cx->clearPendingException();
      return false;
    }

    if (cx->realm() != handlerRealm) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] static bool EnqueuePromiseReactionJobCrossRealm(
    JSContext* cx, HandleObject reactionObj, HandleValue handlerArg,
    JS::PromiseState targetState);
[[nodiscard]] static bool EnqueuePromiseReactionJobSameRealm(
    JSContext* cx, HandleObject reactionObj, HandleValue handlerArg,
    JS::PromiseState targetState);

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * NewPromiseReactionJob ( reaction, argument )
 * https://tc39.es/ecma262/#sec-newpromisereactionjob
 * HostEnqueuePromiseJob ( job, realm )
 * https://tc39.es/ecma262/#sec-hostenqueuepromisejob
 *
 * Tells the embedding to enqueue a Promise reaction job, based on
 * three parameters:
 * reactionObj - The reaction record.
 * handlerArg_ - The first and only argument to pass to the handler invoked by
 *              the job. This will be stored on the reaction record.
 * targetState - The PromiseState this reaction job targets. This decides
 *               whether the onFulfilled or onRejected handler is called.
 */
[[nodiscard]] static bool EnqueuePromiseReactionJob(
    JSContext* cx, HandleObject reactionObj, HandleValue handlerArg,
    JS::PromiseState targetState) {
  MOZ_ASSERT(targetState == JS::PromiseState::Fulfilled ||
             targetState == JS::PromiseState::Rejected);
  if (CanUseSameRealmEnqueue(cx, reactionObj, targetState)) {
    return EnqueuePromiseReactionJobSameRealm(cx, reactionObj, handlerArg,
                                              targetState);
  }
  return EnqueuePromiseReactionJobCrossRealm(cx, reactionObj, handlerArg,
                                             targetState);
}

// The most general handling of promise enqueue cross realm.
//
// Note: Changes to this will almost certainly require changes to
//       CanUseSameRealmEnqueue and EnqueuePromiseReactionJobSameRealm
[[nodiscard]] static bool EnqueuePromiseReactionJobCrossRealm(
    JSContext* cx, HandleObject reactionObj, HandleValue handlerArg_,
    JS::PromiseState targetState) {
  // The reaction might have been stored on a Promise from another
  // compartment, which means it would've been wrapped in a CCW.
  // To properly handle that case here, unwrap it and enter its
  // compartment, where the job creation should take place anyway.
  RootedTuple<PromiseReactionRecord*, Value, Value, Value, JSObject*,
              JSFunction*, JSObject*, JSObject*, JSObject*>
      roots(cx);
  RootedField<PromiseReactionRecord*, 0> reaction(roots);
  RootedField<Value, 1> handlerArg(roots, handlerArg_);
  mozilla::Maybe<AutoRealm> ar;
  if (!IsProxy(reactionObj)) {
    MOZ_RELEASE_ASSERT(reactionObj->is<PromiseReactionRecord>());
    reaction = &reactionObj->as<PromiseReactionRecord>();
    if (cx->realm() != reaction->realm()) {
      // If the compartment has multiple realms, create the job in the
      // reaction's realm. This is consistent with the code in the else-branch
      // and avoids problems with running jobs against a dying global (Gecko
      // drops such jobs).
      ar.emplace(cx, reaction);
    }
  } else {
    JSObject* unwrappedReactionObj = UncheckedUnwrap(reactionObj);
    if (JS_IsDeadWrapper(unwrappedReactionObj)) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_DEAD_OBJECT);
      return false;
    }
    reaction = &unwrappedReactionObj->as<PromiseReactionRecord>();
    MOZ_RELEASE_ASSERT(reaction->is<PromiseReactionRecord>());
    ar.emplace(cx, reaction);
    if (!cx->compartment()->wrap(cx, &handlerArg)) {
      return false;
    }
  }

  // Must not enqueue a reaction job more than once.
  MOZ_ASSERT(reaction->targetState() == JS::PromiseState::Pending);

  // NOTE: Instead of capturing reaction and arguments separately in the
  //       Job Abstract Closure below, store arguments (= handlerArg) in
  //       reaction object and capture it.
  //       Also, set reaction.[[Type]] is represented by targetState here.
  cx->check(handlerArg);
  reaction->setTargetStateAndHandlerArg(targetState, handlerArg);

  RootedField<Value, 2> reactionVal(roots, ObjectValue(*reaction));
  RootedField<Value, 3> handler(roots, reaction->handler());

  // NewPromiseReactionJob
  // Step 2. Let handlerRealm be null.
  // NOTE: Instead of passing job and realm separately, we use the job's
  //       JSFunction object's realm as the job's realm.
  //       So we should enter the handlerRealm before creating the job function.
  //
  // GetFunctionRealm performed inside AutoFunctionOrCurrentRealm uses checked
  // unwrap and it can hit permission error if there's a security wrapper, and
  // in that case the reaction job is created in the current realm, instead of
  // the target function's realm.
  //
  // If this reaction crosses chrome/content boundary, and the security
  // wrapper would allow "call" operation, it still works inside the
  // reaction job.
  //
  // This behavior is observable only when the job belonging to the content
  // realm stops working (*1, *2), and it won't matter in practice.
  //
  // *1: "we can run script" performed inside HostEnqueuePromiseJob
  //     in HTML spec
  //       https://html.spec.whatwg.org/#hostenqueuepromisejob
  //       https://html.spec.whatwg.org/#check-if-we-can-run-script
  //       https://html.spec.whatwg.org/#fully-active
  // *2: nsIGlobalObject::IsDying performed inside PromiseJobRunnable::Run
  //     in our implementation
  mozilla::Maybe<AutoFunctionOrCurrentRealm> ar2;

  // NewPromiseReactionJob
  // Step 3. If reaction.[[Handler]] is not empty, then
  if (handler.isObject()) {
    // Step 3.a. Let getHandlerRealmResult be
    //           GetFunctionRealm(reaction.[[Handler]].[[Callback]]).
    // Step 3.b. If getHandlerRealmResult is a normal completion,
    //           set handlerRealm to getHandlerRealmResult.[[Value]].
    // Step 3.c. Else, set handlerRealm to the current Realm Record.
    // Step 3.d. NOTE: handlerRealm is never null unless the handler is
    //           undefined. When the handler is a revoked Proxy and no
    //           ECMAScript code runs, handlerRealm is used to create error
    //           objects.
    RootedField<JSObject*, 4> handlerObj(roots, &handler.toObject());
    ar2.emplace(cx, handlerObj);

    // This is wrapped here because it may be a cross comaprtment
    // reference, and so should be wrapped to be stored on the job function.
    // (it's also important because this indicates to PromiseReactionJob
    // that it needs to switch realms).
    if (!cx->compartment()->wrap(cx, &reactionVal)) {
      return false;
    }
  }

  // When using JS::AddPromiseReactions{,IgnoringUnHandledRejection}, no actual
  // promise is created, so we might not have one here.
  //
  // Bug 1977691: This comment needs updating; I don't think
  // JS::AddPromiseReactions happens without a promise anymore, _however_ async
  // functions may not have a promise.
  //
  //
  // Additionally, we might have an object here that isn't an instance of
  // Promise. This can happen if content overrides the value of
  // Promise[@@species] (or invokes Promise#then on a Promise subclass
  // instance with a non-default @@species value on the constructor) with a
  // function that returns objects that're not Promise (subclass) instances.
  // In that case, we just pretend we didn't have an object in the first
  // place.
  // If after all this we do have an object, wrap it in case we entered the
  // handler's compartment above, because we should pass objects from a
  // single compartment to the enqueuePromiseJob callback.
  RootedField<JSObject*, 4> promise(roots, reaction->promise());
  if (promise) {
    if (promise->is<PromiseObject>()) {
      if (!cx->compartment()->wrap(cx, &promise)) {
        return false;
      }
    } else if (IsWrapper(promise)) {
      // `promise` can be already-wrapped promise object at this point.
      JSObject* unwrappedPromise = UncheckedUnwrap(promise);
      if (unwrappedPromise->is<PromiseObject>()) {
        if (!cx->compartment()->wrap(cx, &promise)) {
          return false;
        }
      } else {
        promise = nullptr;
      }
    } else {
      promise = nullptr;
    }
  }

  // NewPromiseReactionJob
  // Step 1 (reordered). Let job be a new Job Abstract Closure with no
  //                     parameters that captures reaction and argument
  //                     and performs the following steps when called:
  MOZ_ASSERT(reactionVal.isObject());

  // Get a representative object for this global: We will use this later
  // to extract the target global for execution. We don't store the global
  // directly because CCWs to globals can change identity.
  //
  // So instead we simply store Object.prototype from the target global,
  // an object which always exists.
  RootedField<JSObject*, 6> globalRepresentative(
      roots, &cx->global()->getObjectPrototype());

  // PromiseReactionJob job will use the existence of a CCW as a signal
  // to change to the reactionVal's realm for execution. I believe
  // this is the right thing to do. As a result however we don't actually
  // need to track the global. We simply allow PromiseReactionJob to
  // do the right thing. We will need to enqueue a CCW however
  {
    AutoRealm ar(cx, reaction);

    RootedField<JSObject*, 7> stack(
        roots,
        JS::MaybeGetPromiseAllocationSiteFromPossiblyWrappedPromise(promise));
    if (!cx->compartment()->wrap(cx, &stack)) {
      return false;
    }
    reaction->setAllocationStack(stack);

    if (!cx->compartment()->wrap(cx, &globalRepresentative)) {
      return false;
    }
    reaction->setEnqueueGlobalRepresentative(globalRepresentative);
  }

  if (!cx->compartment()->wrap(cx, &reactionVal)) {
    return false;
  }

  // HostEnqueuePromiseJob(job.[[Job]], job.[[Realm]]).
  return EnqueueJob(cx, &reactionVal.toObject());
}

// A specialization of EnqueuePromiseReactionJobCrossRealm for very common
// same realm case. Should not be called directly, rather
// EnqueuePromiseReactionJob will dispatch here if it's safe.
[[nodiscard]] static bool EnqueuePromiseReactionJobSameRealm(
    JSContext* cx, HandleObject reactionObj, HandleValue handlerArg_,
    JS::PromiseState targetState) {
  RootedTuple<PromiseReactionRecord*, Value, JSObject*, JSObject*, JSObject*>
      roots(cx);
  RootedField<PromiseReactionRecord*, 0> reaction(roots);
  RootedField<Value, 1> handlerArg(roots, handlerArg_);

  // Checked in CanUseSameRealmEnqueue
  MOZ_ASSERT(!IsProxy(reactionObj));
  MOZ_ASSERT(reactionObj->is<PromiseReactionRecord>());

  reaction = &reactionObj->as<PromiseReactionRecord>();

  // Checked in CanUseSameRealmEnqueue
  MOZ_ASSERT(cx->realm() == reaction->realm());

  // Must not enqueue a reaction job more than once.
  MOZ_ASSERT(reaction->targetState() == JS::PromiseState::Pending);

  // NOTE: Instead of capturing reaction and arguments separately in the
  //       Job Abstract Closure below, store arguments (= handlerArg) in
  //       reaction object and capture it.
  //       Also, set reaction.[[Type]] is represented by targetState here.
  cx->check(handlerArg);
  reaction->setTargetStateAndHandlerArg(targetState, handlerArg);

  // NewPromiseReactionJob
  // Checked in CanUseSameRealmEnqueue: the reaction's promise is either null
  // or a PromiseObject.
  RootedField<JSObject*, 2> promise(roots, reaction->promise());

  // NewPromiseReactionJob
  // Step 1 (reordered). Let job be a new Job Abstract Closure with no
  //                     parameters that captures reaction and argument
  //                     and performs the following steps when called:

  // Get a representative object for this global: We will use this later
  // to extract the target global for execution. We don't store the global
  // directly because CCWs to globals can change identity.
  //
  // So instead we simply store Object.prototype from the target global,
  // an object which always exists.
  RootedField<JSObject*, 3> globalRepresentative(
      roots, &cx->global()->getObjectPrototype());

  cx->check(reaction);

  RootedField<JSObject*, 4> stack(
      roots,
      JS::MaybeGetPromiseAllocationSiteFromPossiblyWrappedPromise(promise));
  cx->check(stack, globalRepresentative);

  reaction->setAllocationStack(stack);
  reaction->setEnqueueGlobalRepresentative(globalRepresentative);

  // HostEnqueuePromiseJob(job.[[Job]], job.[[Realm]]).
  return EnqueueJob(cx, reaction);
}

[[nodiscard]] static bool TriggerPromiseReactions(JSContext* cx,
                                                  HandleValue reactionsVal,
                                                  JS::PromiseState state,
                                                  HandleValue valueOrReason);

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * FulfillPromise ( promise, value )
 * https://tc39.es/ecma262/#sec-fulfillpromise
 * RejectPromise ( promise, reason )
 * https://tc39.es/ecma262/#sec-rejectpromise
 *
 * This method takes an additional optional |unwrappedRejectionStack| parameter,
 * which is only used for debugging purposes.
 * It allows callers to to pass in the stack of some exception which
 * triggered the rejection of the promise.
 */
[[nodiscard]] static bool ResolvePromise(
    JSContext* cx, Handle<PromiseObject*> promise, HandleValue valueOrReason,
    JS::PromiseState state,
    Handle<SavedFrame*> unwrappedRejectionStack = nullptr) {
  // Step 1. Assert: The value of promise.[[PromiseState]] is pending.
  MOZ_ASSERT(promise->state() == JS::PromiseState::Pending);
  MOZ_ASSERT(state == JS::PromiseState::Fulfilled ||
             state == JS::PromiseState::Rejected);
  MOZ_ASSERT_IF(unwrappedRejectionStack, state == JS::PromiseState::Rejected);

  // FulfillPromise
  // Step 2. Let reactions be promise.[[PromiseFulfillReactions]].
  // RejectPromise
  // Step 2. Let reactions be promise.[[PromiseRejectReactions]].
  //
  // We only have one list of reactions for both resolution types. So
  // instead of getting the right list of reactions, we determine the
  // resolution type to retrieve the right information from the
  // reaction records.
  RootedValue reactionsVal(cx, promise->reactions());

  // FulfillPromise
  // Step 3. Set promise.[[PromiseResult]] to value.
  // RejectPromise
  // Step 3. Set promise.[[PromiseResult]] to reason.
  //
  // Step 4. Set promise.[[PromiseFulfillReactions]] to undefined.
  // Step 5. Set promise.[[PromiseRejectReactions]] to undefined.
  //
  // The same slot is used for the reactions list and the result, so setting
  // the result also removes the reactions list.
  promise->setFixedSlot(PromiseObject::REACTIONS_OR_RESULT_SLOT, valueOrReason);

  // FulfillPromise
  // Step 6. Set promise.[[PromiseState]] to fulfilled.
  // RejectPromise
  // Step 6. Set promise.[[PromiseState]] to rejected.
  int32_t flags = promise->flags();
  flags |= PROMISE_FLAG_RESOLVED;
  if (state == JS::PromiseState::Fulfilled) {
    flags |= PROMISE_FLAG_FULFILLED;
  }
  promise->setFixedSlotTyped(PromiseObject::FLAGS_SLOT, Int32Value(flags));

  // Also null out the resolve/reject functions so they can be GC'd.
  promise->setFixedSlotTyped(PromiseObject::REJECT_FUNCTION_SLOT,
                             UndefinedValue());

  // Now that everything else is done, do the things the debugger needs.

  // RejectPromise
  // Step 7. If promise.[[PromiseIsHandled]] is false, perform
  //         HostPromiseRejectionTracker(promise, "reject").
  PromiseObject::onSettled(cx, promise, unwrappedRejectionStack);

  // FulfillPromise
  // Step 7. Return TriggerPromiseReactions(reactions, value).
  // RejectPromise
  // Step 8. Return TriggerPromiseReactions(reactions, reason).
  return TriggerPromiseReactions(cx, reactionsVal, state, valueOrReason);
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * RejectPromise ( promise, reason )
 * https://tc39.es/ecma262/#sec-rejectpromise
 */
[[nodiscard]] bool js::RejectPromiseInternal(
    JSContext* cx, JS::Handle<PromiseObject*> promise,
    JS::Handle<JS::Value> reason,
    JS::Handle<SavedFrame*> unwrappedRejectionStack /* = nullptr */) {
  return ResolvePromise(cx, promise, reason, JS::PromiseState::Rejected,
                        unwrappedRejectionStack);
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * FulfillPromise ( promise, value )
 * https://tc39.es/ecma262/#sec-fulfillpromise
 */
[[nodiscard]] static bool FulfillMaybeWrappedPromise(JSContext* cx,
                                                     HandleObject promiseObj,
                                                     HandleValue value_) {
  RootedTuple<PromiseObject*, Value> roots(cx);
  RootedField<PromiseObject*, 0> promise(roots);
  RootedField<Value, 1> value(roots, value_);

  mozilla::Maybe<AutoRealm> ar;
  if (!IsProxy(promiseObj)) {
    promise = &promiseObj->as<PromiseObject>();
  } else {
    JSObject* unwrappedPromiseObj = UncheckedUnwrap(promiseObj);
    if (JS_IsDeadWrapper(unwrappedPromiseObj)) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_DEAD_OBJECT);
      return false;
    }
    promise = &unwrappedPromiseObj->as<PromiseObject>();
    ar.emplace(cx, promise);
    if (!cx->compartment()->wrap(cx, &value)) {
      return false;
    }
  }

  return ResolvePromise(cx, promise, value, JS::PromiseState::Fulfilled);
}

static bool GetCapabilitiesExecutor(JSContext* cx, unsigned argc, Value* vp);
static bool PromiseConstructor(JSContext* cx, unsigned argc, Value* vp);
[[nodiscard]] static PromiseObject* CreatePromiseObjectInternal(
    JSContext* cx, HandleObject proto = nullptr, bool protoIsWrapped = false);

enum GetCapabilitiesExecutorSlots {
  GetCapabilitiesExecutorSlots_Resolve,
  GetCapabilitiesExecutorSlots_Reject
};

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Promise ( executor )
 * https://tc39.es/ecma262/#sec-promise-executor
 */
[[nodiscard]] PromiseObject* js::CreatePromiseObjectWithoutResolutionFunctions(
    JSContext* cx, int32_t extraFlags) {
  // Steps 3-7.
  JS::Rooted<PromiseObject*> promise(cx, CreatePromiseObjectInternal(cx));
  if (!promise) {
    return nullptr;
  }

  AddPromiseFlags(*promise,
                  PROMISE_FLAG_DEFAULT_RESOLVING_FUNCTIONS | extraFlags);

  // Let the Debugger know about this Promise, after we've set
  // flags and slots.
  if (!DebugAPI::onNewPromise(cx, promise)) {
    return nullptr;
  }

  // Step 11. Return promise.
  return promise;
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Promise ( executor )
 * https://tc39.es/ecma262/#sec-promise-executor
 *
 * As if called with GetCapabilitiesExecutor as the executor argument.
 */
[[nodiscard]] static PromiseObject* CreatePromiseWithDefaultResolutionFunctions(
    JSContext* cx, MutableHandleObject resolve, MutableHandleObject reject) {
  // Steps 3-7.
  Rooted<PromiseObject*> promise(cx, CreatePromiseObjectInternal(cx));
  if (!promise) {
    return nullptr;
  }

  // Step 8. Let resolvingFunctions be CreateResolvingFunctions(promise).
  if (!CreateResolvingFunctions(cx, promise, resolve, reject)) {
    return nullptr;
  }

  promise->setFixedSlotTyped(PromiseObject::REJECT_FUNCTION_SLOT,
                             ObjectValue(*reject));

  // Let the Debugger know about this Promise. Do this after we've set
  // flags and functions
  if (!DebugAPI::onNewPromise(cx, promise)) {
    return nullptr;
  }

  // Step 11. Return promise.
  return promise;
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * NewPromiseCapability ( C )
 * https://tc39.es/ecma262/#sec-newpromisecapability
 */
[[nodiscard]] static bool NewPromiseCapability(
    JSContext* cx, HandleObject C, MutableHandle<PromiseCapability> capability,
    bool canOmitResolutionFunctions) {
  RootedValue cVal(cx, ObjectValue(*C));

  // Step 1. If IsConstructor(C) is false, throw a TypeError exception.
  // Step 2. NOTE: C is assumed to be a constructor function that supports the
  // parameter conventions of the Promise constructor (see 27.2.3.1).
  if (!IsConstructor(C)) {
    ReportValueError(cx, JSMSG_NOT_CONSTRUCTOR, JSDVG_SEARCH_STACK, cVal,
                     nullptr);
    return false;
  }

  // If we'd call the original Promise constructor and know that the
  // resolve/reject functions won't ever escape to content, we can skip
  // creating and calling the executor function and instead return a Promise
  // marked as having default resolve/reject functions.
  //
  // This can't be used in Promise.all and Promise.race because we have to
  // pass the reject (and resolve, in the race case) function to thenables
  // in the list passed to all/race, which (potentially) means exposing them
  // to content.
  //
  // For Promise.all and Promise.race we can only optimize away the creation
  // of the GetCapabilitiesExecutor function, and directly allocate the
  // result promise instead of invoking the Promise constructor.
  if (IsNativeFunction(cVal, PromiseConstructor) &&
      cVal.toObject().nonCCWRealm() == cx->realm()) {
    PromiseObject* promise;
    if (canOmitResolutionFunctions) {
      promise = CreatePromiseObjectWithoutResolutionFunctions(cx);
    } else {
      promise = CreatePromiseWithDefaultResolutionFunctions(
          cx, capability.resolve(), capability.reject());
    }
    if (!promise) {
      return false;
    }

    // Step 3. Let promiseCapability be the PromiseCapability Record
    //         { [[Promise]]: undefined, [[Resolve]]: undefined,
    //           [[Reject]]: undefined }.
    capability.promise().set(promise);

    // Step 10. Return promiseCapability.
    return true;
  }

  // Step 4. Let executorClosure be a new Abstract Closure with parameters
  //         (resolve, reject) that captures promiseCapability and performs the
  //         following steps when called:
  Handle<PropertyName*> funName = cx->names().empty_;
  RootedFunction executor(
      cx, NewNativeFunction(cx, GetCapabilitiesExecutor, 2, funName,
                            gc::AllocKind::FUNCTION_EXTENDED, GenericObject));
  if (!executor) {
    return false;
  }

  // Step 5. Let executor be
  //         ! CreateBuiltinFunction(executorClosure, 2, "", « »).
  // (omitted)

  // Step 6. Let promise be ? Construct(C, « executor »).
  // Step 9. Set promiseCapability.[[Promise]] to promise.
  FixedConstructArgs<1> cargs(cx);
  cargs[0].setObject(*executor);
  if (!Construct(cx, cVal, cargs, cVal, capability.promise())) {
    return false;
  }

  // Step 7. If IsCallable(promiseCapability.[[Resolve]]) is false,
  //         throw a TypeError exception.
  const Value& resolveVal =
      executor->getExtendedSlot(GetCapabilitiesExecutorSlots_Resolve);
  if (!IsCallable(resolveVal)) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_PROMISE_RESOLVE_FUNCTION_NOT_CALLABLE);
    return false;
  }

  // Step 8. If IsCallable(promiseCapability.[[Reject]]) is false,
  //         throw a TypeError exception.
  const Value& rejectVal =
      executor->getExtendedSlot(GetCapabilitiesExecutorSlots_Reject);
  if (!IsCallable(rejectVal)) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_PROMISE_REJECT_FUNCTION_NOT_CALLABLE);
    return false;
  }

  // (reordered)
  // Step 3. Let promiseCapability be the PromiseCapability Record
  //         { [[Promise]]: undefined, [[Resolve]]: undefined,
  //           [[Reject]]: undefined }.
  capability.resolve().set(&resolveVal.toObject());
  capability.reject().set(&rejectVal.toObject());

  // Step 10. Return promiseCapability.
  return true;
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * NewPromiseCapability ( C )
 * https://tc39.es/ecma262/#sec-newpromisecapability
 *
 * Steps 4.a-e.
 */
static bool GetCapabilitiesExecutor(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  JSFunction* F = &args.callee().as<JSFunction>();

  // Step 4.a. If promiseCapability.[[Resolve]] is not undefined,
  //           throw a TypeError exception.
  // Step 4.b. If promiseCapability.[[Reject]] is not undefined,
  //           throw a TypeError exception.
  if (!F->getExtendedSlot(GetCapabilitiesExecutorSlots_Resolve).isUndefined() ||
      !F->getExtendedSlot(GetCapabilitiesExecutorSlots_Reject).isUndefined()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_PROMISE_CAPABILITY_HAS_SOMETHING_ALREADY);
    return false;
  }

  // Step 4.c. Set promiseCapability.[[Resolve]] to resolve.
  F->setExtendedSlot(GetCapabilitiesExecutorSlots_Resolve, args.get(0));

  // Step 4.d. Set promiseCapability.[[Reject]] to reject.
  F->setExtendedSlot(GetCapabilitiesExecutorSlots_Reject, args.get(1));

  // Step 4.e. Return undefined.
  args.rval().setUndefined();
  return true;
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * RejectPromise ( promise, reason )
 * https://tc39.es/ecma262/#sec-rejectpromise
 */
[[nodiscard]] static bool RejectMaybeWrappedPromise(
    JSContext* cx, HandleObject promiseObj, HandleValue reason_,
    Handle<SavedFrame*> unwrappedRejectionStack) {
  Rooted<PromiseObject*> promise(cx);
  RootedValue reason(cx, reason_);

  mozilla::Maybe<AutoRealm> ar;
  if (!IsProxy(promiseObj)) {
    promise = &promiseObj->as<PromiseObject>();
  } else {
    JSObject* unwrappedPromiseObj = UncheckedUnwrap(promiseObj);
    if (JS_IsDeadWrapper(unwrappedPromiseObj)) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_DEAD_OBJECT);
      return false;
    }
    promise = &unwrappedPromiseObj->as<PromiseObject>();
    ar.emplace(cx, promise);

    // The rejection reason might've been created in a compartment with higher
    // privileges than the Promise's. In that case, object-type rejection
    // values might be wrapped into a wrapper that throws whenever the
    // Promise's reaction handler wants to do anything useful with it. To
    // avoid that situation, we synthesize a generic error that doesn't
    // expose any privileged information but can safely be used in the
    // rejection handler.
    if (!cx->compartment()->wrap(cx, &reason)) {
      return false;
    }
    if (reason.isObject() && !CheckedUnwrapStatic(&reason.toObject())) {
      // Report the existing reason, so we don't just drop it on the
      // floor.
      JSObject* realReason = UncheckedUnwrap(&reason.toObject());
      RootedValue realReasonVal(cx, ObjectValue(*realReason));
      Rooted<GlobalObject*> realGlobal(cx, &realReason->nonCCWGlobal());
      ReportErrorToGlobal(cx, realGlobal, realReasonVal);

      // Async stacks are only properly adopted if there's at least one
      // interpreter frame active right now. If a thenable job with a
      // throwing `then` function got us here, that'll not be the case,
      // so we add one by throwing the error from self-hosted code.
      if (!GetInternalError(cx, JSMSG_PROMISE_ERROR_IN_WRAPPED_REJECTION_REASON,
                            &reason)) {
        return false;
      }
    }
  }

  return ResolvePromise(cx, promise, reason, JS::PromiseState::Rejected,
                        unwrappedRejectionStack);
}

// Apply f to a mutable handle on each member of a collection of reactions, like
// that stored in PromiseObject::REACTIONS_OR_RESULT_SLOT on a pending promise.
// When the reaction record is wrapped, we pass the wrapper, without
// dereferencing it. If f returns false, then we stop the iteration
// immediately and return false.
// Otherwise, we return true.
//
// There are several different representations for collections:
//
// - We represent an empty collection of reactions as an 'undefined' value.
//
// - We represent a collection containing a single reaction simply as the given
//   PromiseReactionRecord object, possibly wrapped.
//
// - We represent a collection of two or more reactions as a dense array of
//   possibly-wrapped PromiseReactionRecords.
//
template <typename F>
static bool ForEachReaction(JSContext* cx, HandleValue reactionsVal, F f) {
  if (reactionsVal.isUndefined()) {
    return true;
  }

  RootedObject reactions(cx, &reactionsVal.toObject());
  RootedObject reaction(cx);

  if (reactions->is<PromiseReactionRecord>() || IsWrapper(reactions) ||
      JS_IsDeadWrapper(reactions)) {
    return f(&reactions);
  }

  Handle<NativeObject*> reactionsList = reactions.as<NativeObject>();
  uint32_t reactionsCount = reactionsList->getDenseInitializedLength();
  MOZ_ASSERT(reactionsCount > 1, "Reactions list should be created lazily");

  for (uint32_t i = 0; i < reactionsCount; i++) {
    const Value& reactionVal = reactionsList->getDenseElement(i);
    MOZ_RELEASE_ASSERT(reactionVal.isObject());
    reaction = &reactionVal.toObject();
    if (!f(&reaction)) {
      return false;
    }
  }

  return true;
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * TriggerPromiseReactions ( reactions, argument )
 * https://tc39.es/ecma262/#sec-triggerpromisereactions
 */
[[nodiscard]] static bool TriggerPromiseReactions(JSContext* cx,
                                                  HandleValue reactionsVal,
                                                  JS::PromiseState state,
                                                  HandleValue valueOrReason) {
  MOZ_ASSERT(state == JS::PromiseState::Fulfilled ||
             state == JS::PromiseState::Rejected);

  // Step 1. For each element reaction of reactions, do
  // Step 2. Return undefined.
  return ForEachReaction(cx, reactionsVal, [&](MutableHandleObject reaction) {
    // Step 1.a. Let job be NewPromiseReactionJob(reaction, argument).
    // Step 1.b. Perform HostEnqueuePromiseJob(job.[[Job]], job.[[Realm]]).
    return EnqueuePromiseReactionJob(cx, reaction, valueOrReason, state);
  });
}

[[nodiscard]] static bool CallPromiseResolveFunction(JSContext* cx,
                                                     HandleObject resolveFun,
                                                     HandleValue value,
                                                     HandleObject promiseObj);

/**
 * ES2023 draft rev 714fa3dd1e8237ae9c666146270f81880089eca5
 *
 * NewPromiseReactionJob ( reaction, argument )
 * https://tc39.es/ecma262/#sec-newpromisereactionjob
 *
 * Step 1.
 *
 * Implements PromiseReactionJob optimized for the case when the reaction
 * handler is one of the default resolving functions as created by the
 * CreateResolvingFunctions abstract operation.
 */
[[nodiscard]] static bool DefaultResolvingPromiseReactionJob(
    JSContext* cx, Handle<PromiseReactionRecord*> reaction) {
  MOZ_ASSERT(reaction->targetState() != JS::PromiseState::Pending);

  RootedTuple<PromiseObject*, Value, SavedFrame*, Value, JSObject*, JSObject*>
      roots(cx);
  RootedField<PromiseObject*, 0> promiseToResolve(
      roots, reaction->defaultResolvingPromise());

  // Testing functions allow to directly settle a promise without going
  // through the resolving functions. In that case the normal bookkeeping to
  // ensure only pending promises can be resolved doesn't apply and we need
  // to manually check for already settled promises. We still call
  // Run{Fulfill,Reject}Function for consistency with PromiseReactionJob.
  ResolutionMode resolutionMode = ResolveMode;
  RootedField<Value, 1> handlerResult(roots, UndefinedValue());
  RootedField<SavedFrame*, 2> unwrappedRejectionStack(roots);
  if (promiseToResolve->state() == JS::PromiseState::Pending) {
    RootedField<Value, 3> argument(roots, reaction->handlerArg());

    // Step 1.e. Else, let handlerResult be
    //           Completion(HostCallJobCallback(handler, undefined,
    //                                          « argument »)).
    bool ok;
    if (reaction->targetState() == JS::PromiseState::Fulfilled) {
      ok = ResolvePromiseInternal(cx, promiseToResolve, argument);
    } else {
      ok = RejectPromiseInternal(cx, promiseToResolve, argument);
    }

    if (!ok) {
      resolutionMode = RejectMode;
      if (!MaybeGetAndClearExceptionAndStack(cx, &handlerResult,
                                             &unwrappedRejectionStack)) {
        return false;
      }
    }
  }

  // Steps 1.f-i.
  RootedField<JSObject*, 4> promiseObj(roots, reaction->promise());
  RootedField<JSObject*, 5> callee(roots);
  if (resolutionMode == ResolveMode) {
    callee =
        reaction->getFixedSlot(PromiseReactionRecord::Resolve).toObjectOrNull();

    return CallPromiseResolveFunction(cx, callee, handlerResult, promiseObj);
  }

  callee =
      reaction->getFixedSlot(PromiseReactionRecord::Reject).toObjectOrNull();

  return CallPromiseRejectFunction(cx, callee, handlerResult, promiseObj,
                                   unwrappedRejectionStack,
                                   reaction->unhandledRejectionBehavior());
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Await in async function
 * https://tc39.es/ecma262/#await
 *
 * Step 3. fulfilledClosure Abstract Closure.
 * Step 5. rejectedClosure Abstract Closure.
 */
[[nodiscard]] static bool AsyncFunctionPromiseReactionJob(
    JSContext* cx, Handle<PromiseReactionRecord*> reaction) {
  MOZ_ASSERT(reaction->isAsyncFunction());

  auto handler = static_cast<PromiseHandler>(reaction->handler().toInt32());
  RootedValue argument(cx, reaction->handlerArg());
  Rooted<AsyncFunctionGeneratorObject*> generator(
      cx, reaction->asyncFunctionGenerator());

  // Await's handlers don't return a value, nor throw any exceptions.
  // They fail only on OOM.

  if (handler == PromiseHandler::AsyncFunctionAwaitedFulfilled) {
    // Step 3. fulfilledClosure Abstract Closure.
    return AsyncFunctionAwaitedFulfilled(cx, generator, argument);
  }

  // Step 5. rejectedClosure Abstract Closure.
  MOZ_ASSERT(handler == PromiseHandler::AsyncFunctionAwaitedRejected);
  return AsyncFunctionAwaitedRejected(cx, generator, argument);
}

/**
 * ES2023 draft rev 714fa3dd1e8237ae9c666146270f81880089eca5
 *
 * NewPromiseReactionJob ( reaction, argument )
 * https://tc39.es/ecma262/#sec-newpromisereactionjob
 *
 * Step 1.
 *
 * Callback triggering the fulfill/reject reaction for a resolved Promise,
 * to be invoked by the embedding during its processing of the Promise job
 * queue.
 *
 * A PromiseReactionJob is set as the native function of an extended
 * JSFunction object, with all information required for the job's
 * execution stored in in a reaction record in its first extended slot.
 */
static bool PromiseReactionJob(JSContext* cx, HandleObject reactionObjIn) {
  RootedTuple<JSObject*, PromiseReactionRecord*, Value, AsyncGeneratorObject*,
              Value, Value, SavedFrame*, JSObject*, JSObject*>
      roots(cx);
  RootedField<JSObject*, 0> reactionObj(roots, reactionObjIn);
  // To ensure that the embedding ends up with the right entry global, we're
  // guaranteeing that the reaction job function gets created in the same
  // compartment as the handler function. That's not necessarily the global
  // that the job was triggered from, though.
  // We can find the triggering global via the job's reaction record. To go
  // back, we check if the reaction is a wrapper and if so, unwrap it and
  // enter its compartment.
  //
  // MG:XXX: I think that when we switch over to using the JS microtask
  // queue exclusively there's some cleanup around realm handling possible.
  mozilla::Maybe<AutoRealm> ar;
  if (!IsProxy(reactionObj)) {
    MOZ_RELEASE_ASSERT(reactionObj->is<PromiseReactionRecord>());
  } else {
    reactionObj = UncheckedUnwrap(reactionObj);
    if (JS_IsDeadWrapper(reactionObj)) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_DEAD_OBJECT);
      return false;
    }
    MOZ_RELEASE_ASSERT(reactionObj->is<PromiseReactionRecord>());
    ar.emplace(cx, reactionObj);
  }

  // Optimized/special cases.
  RootedField<PromiseReactionRecord*, 1> reaction(
      roots, &reactionObj.get()->as<PromiseReactionRecord>());
  if (reaction->isDefaultResolvingHandler()) {
    return DefaultResolvingPromiseReactionJob(cx, reaction);
  }
  if (reaction->isAsyncFunction()) {
    MOZ_RELEASE_ASSERT(reaction->asyncFunctionGenerator()->realm() ==
                       cx->realm());
    return AsyncFunctionPromiseReactionJob(cx, reaction);
  }
  if (reaction->isAsyncGenerator()) {
    RootedField<Value, 2> argument(roots, reaction->handlerArg());
    RootedField<AsyncGeneratorObject*, 3> generator(roots,
                                                    reaction->asyncGenerator());
    auto handler = static_cast<PromiseHandler>(reaction->handler().toInt32());
    return AsyncGeneratorPromiseReactionJob(cx, handler, generator, argument);
  }
  if (reaction->isDebuggerDummy()) {
    return true;
  }

  // Step 1.a. Let promiseCapability be reaction.[[Capability]].
  // (implicit)

  // Step 1.c. Let handler be reaction.[[Handler]].
  RootedField<Value, 2> handlerVal(roots, reaction->handler());

  RootedField<Value, 4> argument(roots, reaction->handlerArg());

  RootedField<Value, 5> handlerResult(roots);
  ResolutionMode resolutionMode = ResolveMode;

  RootedField<SavedFrame*, 6> unwrappedRejectionStack(roots);

  // Step 1.d. If handler is empty, then
  if (handlerVal.isInt32()) {
    // Step 1.b. Let type be reaction.[[Type]].
    // (reordered)
    auto handlerNum = static_cast<PromiseHandler>(handlerVal.toInt32());

    // Step 1.d.i. If type is Fulfill, let handlerResult be
    //             NormalCompletion(argument).
    if (handlerNum == PromiseHandler::Identity) {
      handlerResult = argument;
    } else if (handlerNum == PromiseHandler::Thrower) {
      // Step 1.d.ii. Else,
      // Step 1.d.ii.1. Assert: type is Reject.
      // Step 1.d.ii.2. Let handlerResult be ThrowCompletion(argument).
      resolutionMode = RejectMode;
      handlerResult = argument;
    } else if (handlerNum ==
               PromiseHandler::AsyncIteratorDisposeAwaitFulfilled) {
      // Explicit Resource Management Proposal
      // 27.1.3.1 %AsyncIteratorPrototype% [ @@asyncDispose ] ( )
      // https://arai-a.github.io/ecma262-compare/?pr=3000&id=sec-%25asynciteratorprototype%25-%40%40asyncdispose
      //
      // Step 6.e.i. Return undefined.
      handlerResult = JS::UndefinedValue();
    } else if (handlerNum == PromiseHandler::AsyncFromSyncIteratorClose) {
      MOZ_ASSERT(reaction->isAsyncFromSyncIterator());

      // 27.1.6.4 AsyncFromSyncIteratorContinuation
      //
      // Step 13.a.i. Return ? IteratorClose(syncIteratorRecord,
      //              ThrowCompletion(error)).
      //
      // https://tc39.es/ecma262/#sec-asyncfromsynciteratorcontinuation
      RootedField<JSObject*, 8> iter(
          roots, reaction->asyncFromSyncIterator()->iterator());
      MOZ_ALWAYS_TRUE(CloseIterOperation(cx, iter, CompletionKind::Throw));

      resolutionMode = RejectMode;
      handlerResult = argument;
    } else {
      // Special case for Async-from-Sync Iterator.

      MOZ_ASSERT(handlerNum ==
                     PromiseHandler::AsyncFromSyncIteratorValueUnwrapDone ||
                 handlerNum ==
                     PromiseHandler::AsyncFromSyncIteratorValueUnwrapNotDone);

      bool done =
          handlerNum == PromiseHandler::AsyncFromSyncIteratorValueUnwrapDone;

      // 27.1.6.4 AsyncFromSyncIteratorContinuation
      //
      // Step 9.a. Return CreateIteratorResultObject(v, done).
      //
      // https://tc39.es/ecma262/#sec-asyncfromsynciteratorcontinuation
      PlainObject* resultObj = CreateIterResultObject(cx, argument, done);
      if (!resultObj) {
        return false;
      }

      handlerResult = ObjectValue(*resultObj);
    }
  } else {
    MOZ_ASSERT(handlerVal.isObject());
    MOZ_ASSERT(IsCallable(handlerVal));

    // Step 1.e. Else, let handlerResult be
    //           Completion(HostCallJobCallback(handler, undefined,
    //                                          « argument »)).
    if (!Call(cx, handlerVal, UndefinedHandleValue, argument, &handlerResult)) {
      resolutionMode = RejectMode;
      if (!MaybeGetAndClearExceptionAndStack(cx, &handlerResult,
                                             &unwrappedRejectionStack)) {
        return false;
      }
    }
  }

  // Steps 1.f-i.
  RootedField<JSObject*, 8> promiseObj(roots, reaction->promise());
  RootedField<JSObject*, 7> callee(roots);
  if (resolutionMode == ResolveMode) {
    callee =
        reaction->getFixedSlot(PromiseReactionRecord::Resolve).toObjectOrNull();

    return CallPromiseResolveFunction(cx, callee, handlerResult, promiseObj);
  }

  callee =
      reaction->getFixedSlot(PromiseReactionRecord::Reject).toObjectOrNull();

  return CallPromiseRejectFunction(cx, callee, handlerResult, promiseObj,
                                   unwrappedRejectionStack,
                                   reaction->unhandledRejectionBehavior());
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * NewPromiseResolveThenableJob ( promiseToResolve, thenable, then )
 * https://tc39.es/ecma262/#sec-newpromiseresolvethenablejob
 *
 * Steps 1.a-d.
 *
 * With the thenable-curtailment proposal
 * (https://tc39.es/proposal-thenable-curtailment/) these steps are the
 * PerformPromiseResolveThenable abstract operation: the body shared between
 * the enqueued thenable job (the ~sync~ resolution path) and the synchronous
 * thenable invocation done by PerformPromiseResolution in ~deferred~ mode.
 */
static bool PerformPromiseResolveThenable(JSContext* cx, HandleObject promise,
                                          HandleValue thenable,
                                          HandleObject then) {
  // Step 1.a. Let resolvingFunctions be
  //           CreateResolvingFunctions(promiseToResolve).
  RootedTuple<JSObject*, JSObject*, Value, SavedFrame*, Value> roots(cx);
  RootedField<JSObject*, 0> resolveFn(roots);
  RootedField<JSObject*, 1> rejectFn(roots);
  if (!CreateResolvingFunctions(cx, promise, &resolveFn, &rejectFn)) {
    return false;
  }

  // Step 1.b. Let thenCallResult be
  //           HostCallJobCallback(then, thenable,
  //                               « resolvingFunctions.[[Resolve]],
  //                                 resolvingFunctions.[[Reject]] »).
  FixedInvokeArgs<2> args2(cx);
  args2[0].setObject(*resolveFn);
  args2[1].setObject(*rejectFn);

  // In difference to the usual pattern, we return immediately on success.
  RootedField<Value, 2> rval(roots);
  if (Call(cx, thenable, then, args2, &rval)) {
    // Step 1.d. Return Completion(thenCallResult).
    return true;
  }

  // Step 1.c. If thenCallResult is an abrupt completion, then

  RootedField<SavedFrame*, 3> stack(roots);
  if (!MaybeGetAndClearExceptionAndStack(cx, &rval, &stack)) {
    return false;
  }

  // Step 1.c.i. Let status be
  //             Call(resolvingFunctions.[[Reject]], undefined,
  //                  « thenCallResult.[[Value]] »).
  // Step 1.c.ii. Return Completion(status).
  RootedField<Value, 4> rejectVal(roots, ObjectValue(*rejectFn));
  return Call(cx, rejectVal, UndefinedHandleValue, rval, &rval);
}

[[nodiscard]] static bool OriginalPromiseThenWithoutSettleHandlers(
    JSContext* cx, Handle<PromiseObject*> promise,
    Handle<PromiseObject*> promiseToResolve);

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * NewPromiseResolveThenableJob ( promiseToResolve, thenable, then )
 * https://tc39.es/ecma262/#sec-newpromiseresolvethenablejob
 *
 * Step 1.a-d.
 *
 * Specialization of PromiseResolveThenableJob when the `thenable` is a
 * built-in Promise object and the `then` property is the built-in
 * `Promise.prototype.then` function.
 *
 * A PromiseResolveBuiltinThenableJob is set as the native function of an
 * extended JSFunction object, with all information required for the job's
 * execution stored in the function's extended slots.
 *
 * Usage of the function's extended slots is described in the ThenableJobSlots
 * enum.
 */
static bool PromiseResolveBuiltinThenableJob(JSContext* cx,
                                             HandleObject promise,
                                             HandleObject thenable) {
  cx->check(promise, thenable);
  MOZ_ASSERT(promise->is<PromiseObject>());
  MOZ_ASSERT(thenable->is<PromiseObject>());

  // Step 1.a. Let resolvingFunctions be
  //           CreateResolvingFunctions(promiseToResolve).
  // (skipped)

  // Step 1.b. Let thenCallResult be HostCallJobCallback(
  //             then, thenable,
  //             « resolvingFunctions.[[Resolve]],
  //               resolvingFunctions.[[Reject]] »).
  //
  // NOTE: In difference to the usual pattern, we return immediately on success.
  if (OriginalPromiseThenWithoutSettleHandlers(cx, thenable.as<PromiseObject>(),
                                               promise.as<PromiseObject>())) {
    // Step 1.d. Return Completion(thenCallResult).
    return true;
  }

  // Step 1.c. If thenCallResult is an abrupt completion, then
  RootedValue exception(cx);
  Rooted<SavedFrame*> stack(cx);
  if (!MaybeGetAndClearExceptionAndStack(cx, &exception, &stack)) {
    return false;
  }

  // Testing functions allow to directly settle a promise without going
  // through the resolving functions. In that case the normal bookkeeping to
  // ensure only pending promises can be resolved doesn't apply and we need
  // to manually check for already settled promises. The exception is simply
  // dropped when this case happens.
  if (promise->as<PromiseObject>().state() != JS::PromiseState::Pending) {
    return true;
  }

  // Step 1.c.i. Let status be
  //             Call(resolvingFunctions.[[Reject]], undefined,
  //                  « thenCallResult.[[Value]] »).
  // Step 1.c.ii. Return Completion(status).
  return RejectPromiseInternal(cx, promise.as<PromiseObject>(), exception,
                               stack);
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * NewPromiseResolveThenableJob ( promiseToResolve, thenable, then )
 * https://tc39.es/ecma262/#sec-newpromiseresolvethenablejob
 * HostEnqueuePromiseJob ( job, realm )
 * https://tc39.es/ecma262/#sec-hostenqueuepromisejob
 *
 * Tells the embedding to enqueue a Promise resolve thenable job, based on
 * three parameters:
 * promiseToResolve_ - The promise to resolve, obviously.
 * thenable_ - The thenable to resolve the Promise with.
 * thenVal - The `then` function to invoke with the `thenable` as the receiver.
 */
[[nodiscard]] static bool EnqueuePromiseResolveThenableJob(
    JSContext* cx, HandleValue promiseToResolve_, HandleValue thenable_,
    HandleValue thenVal) {
  // Need to re-root these to enable wrapping them below.
  RootedTuple<Value, Value, JSObject*, JSObject*, JSObject*, JSObject*> roots(
      cx);
  RootedField<Value, 0> promiseToResolve(roots, promiseToResolve_);
  RootedField<Value, 1> thenable(roots, thenable_);

  // Step 2. Let getThenRealmResult be GetFunctionRealm(then.[[Callback]]).
  // Step 3. If getThenRealmResult is a normal completion, let thenRealm be
  //         getThenRealmResult.[[Value]].
  // Step 4. Else, let thenRealm be the current Realm Record.
  // Step 5. NOTE: thenRealm is never null. When then.[[Callback]] is a revoked
  //         Proxy and no code runs, thenRealm is used to create error objects.
  //
  // NOTE: Instead of passing job and realm separately, we use the job's
  //       JSFunction object's realm as the job's realm.
  //       So we should enter the thenRealm before creating the job function.
  //
  // GetFunctionRealm performed inside AutoFunctionOrCurrentRealm uses checked
  // unwrap and this is fine given the behavior difference (see the comment
  // around AutoFunctionOrCurrentRealm usage in EnqueuePromiseReactionJob for
  // more details) is observable only when the `thenable` is from content realm
  // and `then` is from chrome realm, that shouldn't happen in practice.
  //
  // NOTE: If `thenable` is also from chrome realm, accessing `then` silently
  //       fails and it returns `undefined`, and that case doesn't reach here.
  RootedField<JSObject*, 2> then(roots, &thenVal.toObject());
  AutoFunctionOrCurrentRealm ar(cx, then);
  if (then->maybeCCWRealm() != cx->realm()) {
    if (!cx->compartment()->wrap(cx, &then)) {
      return false;
    }
  }

  // Wrap the `promiseToResolve` and `thenable` arguments.
  if (!cx->compartment()->wrap(cx, &promiseToResolve)) {
    return false;
  }

  MOZ_ASSERT(thenable.isObject());
  if (!cx->compartment()->wrap(cx, &thenable)) {
    return false;
  }

  // At this point the promise is guaranteed to be wrapped into the job's
  // compartment.
  RootedField<JSObject*, 3> promise(roots, &promiseToResolve.toObject());

  RootedField<JSObject*, 4> hostDefinedGlobalRepresentative(roots);

  if (!GetIncumbentGlobalRepresentative(cx, &hostDefinedGlobalRepresentative)) {
    return false;
  }
  RootedField<JSObject*, 5> optionalHostDefinedDataIsOptimizedOut(roots,
                                                                  nullptr);
  ThenableJob* thenableJob = NewThenableJob(
      cx, ThenableJob::PromiseResolveThenableJob, promise, thenable, then,
      hostDefinedGlobalRepresentative, optionalHostDefinedDataIsOptimizedOut);
  if (!thenableJob) {
    return false;
  }

  return EnqueueJob(cx, thenableJob);
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * NewPromiseResolveThenableJob ( promiseToResolve, thenable, then )
 * https://tc39.es/ecma262/#sec-newpromiseresolvethenablejob
 * HostEnqueuePromiseJob ( job, realm )
 * https://tc39.es/ecma262/#sec-hostenqueuepromisejob
 *
 * Tells the embedding to enqueue a Promise resolve thenable built-in job,
 * based on two parameters:
 * promiseToResolve - The promise to resolve, obviously.
 * thenable - The thenable to resolve the Promise with.
 */
[[nodiscard]] static bool EnqueuePromiseResolveThenableBuiltinJob(
    JSContext* cx, HandleObject promiseToResolve, HandleObject thenable) {
  cx->check(promiseToResolve, thenable);
  MOZ_ASSERT(promiseToResolve->is<PromiseObject>());
  MOZ_ASSERT(thenable->is<PromiseObject>());

  // Step 1. Let job be a new Job Abstract Closure with no parameters that
  //         captures promiseToResolve, thenable, and then and performs the
  //         following steps when called:

  RootedTuple<JSObject*, JSObject*, Value> roots(cx);
  RootedField<JSObject*, 0> incumbentGlobalRepresentative(roots);
  RootedField<JSObject*, 1> optionalHostDefinedData(roots);
  if (!GetObjectFromHostDefinedData(cx, &incumbentGlobalRepresentative,
                                    &optionalHostDefinedData)) {
    return false;
  }

  RootedField<Value, 2> thenableValue(roots, ObjectValue(*thenable));
  ThenableJob* thenableJob =
      NewThenableJob(cx, ThenableJob::PromiseResolveBuiltinThenableJob,
                     promiseToResolve, thenableValue, nullptr,
                     incumbentGlobalRepresentative, optionalHostDefinedData);
  if (!thenableJob) {
    return false;
  }

  return EnqueueJob(cx, thenableJob);
}

#ifdef NIGHTLY_BUILD
/**
 * Thenable-curtailment: https://tc39.es/proposal-thenable-curtailment/
 *
 * RequiresDeferredPromiseResolution (value)
 *
 * Returns true in *needsDeferral if the synchronous steps of resolving a
 * promise with `value` _might_ execute user code: e.g.
 * - a Proxy or other exotic object on the prototype chain, an accessor
 *   for `"then"`, or a callable data-property `"then"`.
 *
 */
[[nodiscard]] static bool RequiresDeferredPromiseResolution(
    JSContext* cx, HandleValue value, bool* needsDeferral) {
  *needsDeferral = false;
  if (!value.isObject()) {
    return true;
  }

  RootedValue thenVal(cx);
  if (!GetPropertyPure(cx, &value.toObject(), NameToId(cx->names().then),
                       thenVal.address())) {
    *needsDeferral = true;
    return true;
  }

  *needsDeferral = IsCallable(thenVal);
  return true;
}

/**
 * Thenable-curtailment: https://tc39.es/proposal-thenable-curtailment/
 *
 * PerformPromiseResolution (promise, resolution, called)
 *
 * This realizes PerformPromiseResolution with `called` fixed to ~deferred~:
 * it is only ever reached from the deferred-resolve job enqueued by
 * SafePromiseResolve. (The ~sync~ resolution path is the ordinary
 * ResolvePromiseInternal used by the resolve function.) Because we are in
 * ~deferred~ mode, a callable `then` is invoked synchronously via
 * PerformPromiseResolveThenable rather than by enqueuing a further job, so
 * the deferred resolution takes the same number of microtask ticks as an
 * ordinary thenable resolution.
 */
[[nodiscard]] static bool PerformPromiseResolution(
    JSContext* cx, Handle<PromiseObject*> promise, HandleValue resolution) {
  MOZ_ASSERT(promise->state() == JS::PromiseState::Pending);

  // Step 1.
  if (!resolution.isObject()) {
    return FulfillMaybeWrappedPromise(cx, promise, resolution);
  }

  // Step 2.
  if (resolution.isObject() && &resolution.toObject() == promise) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_CANNOT_RESOLVE_PROMISE_WITH_ITSELF);
    RootedValue selfResolutionError(cx);
    Rooted<SavedFrame*> stack(cx);
    if (!MaybeGetAndClearExceptionAndStack(cx, &selfResolutionError, &stack)) {
      return false;
    }
    return RejectPromiseInternal(cx, promise, selfResolutionError, stack);
  }

  // Step 3.
  RootedObject resolutionObj(cx, &resolution.toObject());
  RootedValue thenVal(cx);
  if (!GetProperty(cx, resolutionObj, resolution, cx->names().then, &thenVal)) {
    // Step 4.
    RootedValue exn(cx);
    Rooted<SavedFrame*> stack(cx);
    if (!MaybeGetAndClearExceptionAndStack(cx, &exn, &stack)) {
      return false;
    }
    if (IsSettledMaybeWrappedPromise(promise)) {
      return true;
    }
    return RejectPromiseInternal(cx, promise, exn, stack);
  }

  if (IsSettledMaybeWrappedPromise(promise)) {
    return true;
  }

  // Step 6
  if (!IsCallable(thenVal)) {
    return FulfillMaybeWrappedPromise(cx, promise, resolution);
  }

  // Step 7. (called is ~deferred~.) Run the thenable now instead of enqueuing
  // another job: PerformPromiseResolveThenable(promise, resolution, then).
  RootedObject promiseObj(cx, promise);
  RootedObject thenObj(cx, &thenVal.toObject());
  return PerformPromiseResolveThenable(cx, promiseObj, resolution, thenObj);
}

/**
 * Thenable-curtailment: https://tc39.es/proposal-thenable-curtailment/
 *
 * Enqueues a deferred-resolve microtask job that will invoke
 * PerformPromiseResolution on `promise` with `resolution` when run.
 *
 * The job's realm is the caller's current realm, matching
 * HostEnqueuePromiseJob's contract.
 */
[[nodiscard]] static bool EnqueueDeferredResolveJob(
    JSContext* cx, Handle<PromiseObject*> promise, HandleValue resolution) {
  RootedObject incumbentGlobalRepresentative(cx);
  RootedObject optionalHostDefinedData(cx);
  if (!GetObjectFromHostDefinedData(cx, &incumbentGlobalRepresentative,
                                    &optionalHostDefinedData)) {
    return false;
  }

  RootedObject promiseObj(cx, promise);
  ThenableJob* job = NewThenableJob(
      cx, ThenableJob::DeferredResolveJob, promiseObj, resolution, nullptr,
      incumbentGlobalRepresentative, optionalHostDefinedData);
  if (!job) {
    return false;
  }

  return EnqueueJob(cx, job);
}

/**
 * Thenable-curtailment: https://tc39.es/proposal-thenable-curtailment/
 *
 * SafePromiseResolve (promiseCapability, resolution)
 *
 * Resolves `promise` with `resolution`. If the synchronous resolution steps
 * might run user code (Proxy, accessor, or callable "then" data property),
 * mark the promise's resolving functions as no-ops and defer the actual
 * PerformPromiseResolution steps to a freshly-enqueued microtask.
 *
 * The spec routes the deferral through a null-prototype wrapper thenable
 * resolved via the promise's [[Resolve]] function; that wrapper is never
 * exposed to user code, so we skip allocating it and instead latch the
 * resolving functions directly and enqueue the deferred-resolve job here.
 */
bool js::SafeResolvePromise(JSContext* cx, Handle<PromiseObject*> promise,
                            HandleValue resolution) {
  cx->check(promise, resolution);
  MOZ_ASSERT(!PromiseHasAnyFlag(*promise, PROMISE_FLAG_ASYNC));

  if (promise->state() != JS::PromiseState::Pending) {
    return true;
  }

  bool needsDeferral = false;
  if (!RequiresDeferredPromiseResolution(cx, resolution, &needsDeferral)) {
    return false;
  }

  if (!needsDeferral) {
    return PromiseObject::resolve(cx, promise, resolution);
  }

  // Mark resolving functions as no-ops.
  if (IsPromiseWithDefaultResolvingFunction(promise)) {
    if (PromiseHasAnyFlag(
            *promise,
            PROMISE_FLAG_DEFAULT_RESOLVING_FUNCTIONS_ALREADY_RESOLVED)) {
      return true;
    }
    SetAlreadyResolvedPromiseWithDefaultResolvingFunction(promise);
  } else {
    JSFunction* resolveFun = GetResolveFunctionFromPromise(promise);
    if (!resolveFun) {
      // Already latched.
      return true;
    }
    SetAlreadyResolvedResolutionFunction(resolveFun);
  }

  return EnqueueDeferredResolveJob(cx, promise, resolution);
}
#endif  // NIGHTLY_BUILD

[[nodiscard]] static bool AddDummyPromiseReactionForDebugger(
    JSContext* cx, Handle<PromiseObject*> promise,
    HandleObject dependentPromise);

[[nodiscard]] static bool AddPromiseReaction(
    JSContext* cx, Handle<PromiseObject*> promise,
    Handle<PromiseReactionRecord*> reaction);

static JSFunction* GetResolveFunctionFromReject(JSFunction* reject) {
  MOZ_ASSERT(reject->maybeNative() == RejectPromiseFunction);
  Value resolveFunVal =
      reject->getExtendedSlot(RejectFunctionSlot_ResolveFunction);
  MOZ_ASSERT(IsNativeFunction(resolveFunVal, ResolvePromiseFunction));
  return &resolveFunVal.toObject().as<JSFunction>();
}

static JSFunction* GetRejectFunctionFromResolve(JSFunction* resolve) {
  MOZ_ASSERT(resolve->maybeNative() == ResolvePromiseFunction);
  Value rejectFunVal =
      resolve->getExtendedSlot(ResolveFunctionSlot_RejectFunction);
  MOZ_ASSERT(IsNativeFunction(rejectFunVal, RejectPromiseFunction));
  return &rejectFunVal.toObject().as<JSFunction>();
}

static JSFunction* GetResolveFunctionFromPromise(PromiseObject* promise) {
  Value rejectFunVal =
      promise->getFixedSlotTyped(PromiseObject::REJECT_FUNCTION_SLOT);
  if (rejectFunVal.isUndefined()) {
    return nullptr;
  }
  JSObject* rejectFunObj = &rejectFunVal.toObject();

  // We can safely unwrap it because all we want is to get the resolve
  // function.
  if (IsWrapper(rejectFunObj)) {
    rejectFunObj = UncheckedUnwrap(rejectFunObj);
  }

  if (!rejectFunObj->is<JSFunction>()) {
    return nullptr;
  }

  JSFunction* rejectFun = &rejectFunObj->as<JSFunction>();

  // Only the original RejectPromiseFunction has a reference to the resolve
  // function.
  if (rejectFun->maybeNative() != &RejectPromiseFunction) {
    return nullptr;
  }

  // The reject function was already called and cleared its resolve-function
  // extended slot.
  if (rejectFun->getExtendedSlot(RejectFunctionSlot_ResolveFunction)
          .isUndefined()) {
    return nullptr;
  }

  return GetResolveFunctionFromReject(rejectFun);
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Promise ( executor )
 * https://tc39.es/ecma262/#sec-promise-executor
 *
 * Steps 3-7.
 */
[[nodiscard]] static MOZ_ALWAYS_INLINE PromiseObject*
CreatePromiseObjectInternal(JSContext* cx, HandleObject proto /* = nullptr */,
                            bool protoIsWrapped /* = false */) {
  // Enter the unwrapped proto's compartment, if that's different from
  // the current one.
  // All state stored in a Promise's fixed slots must be created in the
  // same compartment, so we get all of that out of the way here.
  // (Except for the resolution functions, which are created below.)
  mozilla::Maybe<AutoRealm> ar;
  if (protoIsWrapped) {
    ar.emplace(cx, proto);
  }

  // Step 3. Let promise be
  //         ? OrdinaryCreateFromConstructor(
  //             NewTarget, "%Promise.prototype%",
  //             « [[PromiseState]], [[PromiseResult]],
  //               [[PromiseFulfillReactions]], [[PromiseRejectReactions]],
  //               [[PromiseIsHandled]] »).
  PromiseObject* promise = NewObjectWithClassProto<PromiseObject>(cx, proto);
  if (!promise) {
    return nullptr;
  }

  // Step 4. Set promise.[[PromiseState]] to pending.
  promise->initFixedSlotTyped(PromiseObject::FLAGS_SLOT, Int32Value(0));

  // Step 5. Set promise.[[PromiseFulfillReactions]] to a new empty List.
  // Step 6. Set promise.[[PromiseRejectReactions]] to a new empty List.
  // (omitted)
  // We allocate our single list of reaction records lazily.

  // Step 7. Set promise.[[PromiseIsHandled]] to false.
  // (implicit)
  // The handled flag is unset by default.

  if (MOZ_LIKELY(!JS::IsAsyncStackCaptureEnabledForRealm(cx))) {
    return promise;
  }

  // Store an allocation stack so we can later figure out what the
  // control flow was for some unexpected results. Frightfully expensive,
  // but oh well.

  Rooted<PromiseObject*> promiseRoot(cx, promise);

  PromiseDebugInfo* debugInfo = PromiseDebugInfo::create(cx, promiseRoot);
  if (!debugInfo) {
    return nullptr;
  }

  return promiseRoot;
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Promise ( executor )
 * https://tc39.es/ecma262/#sec-promise-executor
 */
static bool PromiseConstructor(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Step 1. If NewTarget is undefined, throw a TypeError exception.
  if (!ThrowIfNotConstructing(cx, args, "Promise")) {
    return false;
  }

  // Step 2. If IsCallable(executor) is false, throw a TypeError exception.
  HandleValue executorVal = args.get(0);
  if (!IsCallable(executorVal)) {
    return ReportIsNotFunction(cx, executorVal);
  }
  RootedObject executor(cx, &executorVal.toObject());

  RootedObject newTarget(cx, &args.newTarget().toObject());

  // If the constructor is called via an Xray wrapper, then the newTarget
  // hasn't been unwrapped. We want that because, while the actual instance
  // should be created in the target compartment, the constructor's code
  // should run in the wrapper's compartment.
  //
  // This is so that the resolve and reject callbacks get created in the
  // wrapper's compartment, which is required for code in that compartment
  // to freely interact with it, and, e.g., pass objects as arguments, which
  // it wouldn't be able to if the callbacks were themselves wrapped in Xray
  // wrappers.
  //
  // At the same time, just creating the Promise itself in the wrapper's
  // compartment wouldn't be helpful: if the wrapper forbids interactions
  // with objects except for specific actions, such as calling them, then
  // the code we want to expose it to can't actually treat it as a Promise:
  // calling .then on it would throw, for example.
  //
  // Another scenario where it's important to create the Promise in a
  // different compartment from the resolution functions is when we want to
  // give non-privileged code a Promise resolved with the result of a
  // Promise from privileged code; as a return value of a JS-implemented
  // API, say. If the resolution functions were unprivileged, then resolving
  // with a privileged Promise would cause `resolve` to attempt accessing
  // .then on the passed Promise, which would throw an exception, so we'd
  // just end up with a rejected Promise. Really, we want to chain the two
  // Promises, with the unprivileged one resolved with the resolution of the
  // privileged one.

  bool needsWrapping = false;
  RootedObject proto(cx);
  if (IsWrapper(newTarget)) {
    JSObject* unwrappedNewTarget = CheckedUnwrapStatic(newTarget);
    MOZ_ASSERT(unwrappedNewTarget);
    MOZ_ASSERT(unwrappedNewTarget != newTarget);

    newTarget = unwrappedNewTarget;
    {
      AutoRealm ar(cx, newTarget);
      Handle<GlobalObject*> global = cx->global();
      JSObject* promiseCtor =
          GlobalObject::getOrCreatePromiseConstructor(cx, global);
      if (!promiseCtor) {
        return false;
      }

      // Promise subclasses don't get the special Xray treatment, so
      // we only need to do the complex wrapping and unwrapping scheme
      // described above for instances of Promise itself.
      if (newTarget == promiseCtor) {
        needsWrapping = true;
        proto = GlobalObject::getOrCreatePromisePrototype(cx, cx->global());
        if (!proto) {
          return false;
        }
      }
    }
  }

  if (needsWrapping) {
    if (!cx->compartment()->wrap(cx, &proto)) {
      return false;
    }
  } else {
    if (!GetPrototypeFromBuiltinConstructor(cx, args, JSProto_Promise,
                                            &proto)) {
      return false;
    }
  }
  PromiseObject* promise =
      PromiseObject::create(cx, executor, proto, needsWrapping);
  if (!promise) {
    return false;
  }

  // Step 11.
  args.rval().setObject(*promise);
  if (needsWrapping) {
    return cx->compartment()->wrap(cx, args.rval());
  }
  return true;
}

bool js::IsPromiseConstructor(const JSObject* obj) {
  // Note: this also returns true for cross-realm Promise constructors in the
  // same compartment.
  return IsNativeFunction(obj, PromiseConstructor);
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Promise ( executor )
 * https://tc39.es/ecma262/#sec-promise-executor
 *
 * Steps 3-11.
 */
/* static */
PromiseObject* PromiseObject::create(JSContext* cx, HandleObject executor,
                                     HandleObject proto /* = nullptr */,
                                     bool needsWrapping /* = false */) {
  MOZ_ASSERT(executor->isCallable());

  RootedTuple<JSObject*, PromiseObject*, JSObject*, JSObject*, JSObject*,
              JSObject*, Value, Value, SavedFrame*, Value>
      roots(cx);
  RootedField<JSObject*, 0> usedProto(roots, proto);
  // If the proto is wrapped, that means the current function is running
  // with a different compartment active from the one the Promise instance
  // is to be created in.
  // See the comment in PromiseConstructor for details.
  if (needsWrapping) {
    MOZ_ASSERT(proto);
    usedProto = CheckedUnwrapStatic(proto);
    if (!usedProto) {
      ReportAccessDenied(cx);
      return nullptr;
    }
  }

  // Steps 3-7.
  RootedField<PromiseObject*, 1> promise(
      roots, CreatePromiseObjectInternal(cx, usedProto, needsWrapping));
  if (!promise) {
    return nullptr;
  }

  RootedField<JSObject*, 2> promiseObj(roots, promise);
  if (needsWrapping && !cx->compartment()->wrap(cx, &promiseObj)) {
    return nullptr;
  }

  // Step 8. Let resolvingFunctions be CreateResolvingFunctions(promise).
  //
  // The resolving functions are created in the compartment active when the
  // (maybe wrapped) Promise constructor was called. They contain checks and
  // can unwrap the Promise if required.
  RootedField<JSObject*, 3> resolveFn(roots);
  RootedField<JSObject*, 4> rejectFn(roots);
  if (!CreateResolvingFunctions(cx, promiseObj, &resolveFn, &rejectFn)) {
    return nullptr;
  }

  // Need to wrap the resolution functions before storing them on the Promise.
  MOZ_ASSERT(promise->getFixedSlotTyped(PromiseObject::REJECT_FUNCTION_SLOT)
                 .isUndefined(),
             "Slot must be undefined so initFixedSlotTyped can be used");
  if (needsWrapping) {
    AutoRealm ar(cx, promise);
    RootedField<JSObject*, 5> wrappedRejectFn(roots, rejectFn);
    if (!cx->compartment()->wrap(cx, &wrappedRejectFn)) {
      return nullptr;
    }
    promise->initFixedSlotTyped(PromiseObject::REJECT_FUNCTION_SLOT,
                                ObjectValue(*wrappedRejectFn));
  } else {
    promise->initFixedSlotTyped(PromiseObject::REJECT_FUNCTION_SLOT,
                                ObjectValue(*rejectFn));
  }

  // Step 9. Let completion be
  //         Call(executor, undefined, « resolvingFunctions.[[Resolve]],
  //                                     resolvingFunctions.[[Reject]] »).
  bool success;
  {
    FixedInvokeArgs<2> args(cx);
    args[0].setObject(*resolveFn);
    args[1].setObject(*rejectFn);

    RootedField<Value, 6> calleeOrRval(roots, ObjectValue(*executor));
    success = Call(cx, calleeOrRval, UndefinedHandleValue, args, &calleeOrRval);
  }

  // Step 10. If completion is an abrupt completion, then
  if (!success) {
    RootedField<Value, 7> exceptionVal(roots);
    RootedField<SavedFrame*, 8> stack(roots);
    if (!MaybeGetAndClearExceptionAndStack(cx, &exceptionVal, &stack)) {
      return nullptr;
    }

    // Step 10.a. Perform
    //            ? Call(resolvingFunctions.[[Reject]], undefined,
    //                   « completion.[[Value]] »).
    RootedField<Value, 9> calleeOrRval(roots, ObjectValue(*rejectFn));
    if (!Call(cx, calleeOrRval, UndefinedHandleValue, exceptionVal,
              &calleeOrRval)) {
      return nullptr;
    }
  }

  // Let the Debugger know about this Promise.
  if (!DebugAPI::onNewPromise(cx, promise)) {
    return nullptr;
  }

  // Step 11. Return promise.
  return promise;
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Promise ( executor )
 * https://tc39.es/ecma262/#sec-promise-executor
 *
 * skipping creation of resolution functions and executor function invocation.
 */
/* static */
PromiseObject* PromiseObject::createSkippingExecutor(JSContext* cx) {
  return CreatePromiseObjectWithoutResolutionFunctions(cx);
}

class MOZ_STACK_CLASS PromiseForOfIterator : public JS::ForOfIterator {
 public:
  using JS::ForOfIterator::ForOfIterator;

  bool isOptimizedDenseArrayIteration() {
    MOZ_ASSERT(valueIsIterable());
    return isOptimizedArray_ && IsPackedArray(iteratorOrArray_);
  }
};

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Unified implementation of iterable and property-based variants:
 *
 * Promise.all ( iterable )
 * https://tc39.es/ecma262/#sec-promise.all
 * Promise.allSettled ( iterable )
 * https://tc39.es/ecma262/#sec-promise.allsettled
 * Promise.race ( iterable )
 * https://tc39.es/ecma262/#sec-promise.race
 * Promise.any ( iterable )
 * https://tc39.es/ecma262/#sec-promise.any
 * Promise.allKeyed ( promises )
 * https://tc39.es/proposal-await-dictionary/#sec-promise.allkeyed
 * Promise.allSettledKeyed ( promises )
 * https://tc39.es/proposal-await-dictionary/#sec-promise.allsettledkeyed
 * GetPromiseResolve ( promiseConstructor )
 * https://tc39.es/ecma262/#sec-getpromiseresolve
 */
template <typename IterT, typename PerformFuncT, typename InitIterFuncT,
          typename MaybeCloseIterFuncT>
[[nodiscard]] static bool CommonPromiseCombinator(
    JSContext* cx, CallArgs& args, PerformFuncT performFunc,
    const char* nonObjectThisErrorMessage, const char* nonIterableErrorMessage,
    InitIterFuncT initIter, MaybeCloseIterFuncT maybeCloseIterFunc) {
  // Step 2. Let promiseCapability be ? NewPromiseCapability(C).
  // (moved from NewPromiseCapability, step 1).
  HandleValue CVal = args.thisv();
  if (!CVal.isObject()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_OBJECT_REQUIRED, nonObjectThisErrorMessage);
    return false;
  }

  // Step 1. Let C be the this value.
  RootedObject C(cx, &CVal.toObject());

  // Step 2. Let promiseCapability be ? NewPromiseCapability(C).
  Rooted<PromiseCapability> promiseCapability(cx);
  if (!NewPromiseCapability(cx, C, &promiseCapability, false)) {
    return false;
  }

  RootedValue promiseResolve(cx, UndefinedValue());
  {
    JSObject* promiseCtor =
        GlobalObject::getOrCreatePromiseConstructor(cx, cx->global());
    if (!promiseCtor) {
      return false;
    }

    if (C != promiseCtor || !HasDefaultPromiseProperties(cx)) {
      // Step 3. Let promiseResolve be GetPromiseResolve(C).

      // GetPromiseResolve
      // Step 1. Let promiseResolve be ? Get(promiseConstructor, "resolve").
      if (!GetProperty(cx, C, C, cx->names().resolve, &promiseResolve)) {
        // Step 4. IfAbruptRejectPromise(promiseResolve, promiseCapability).
        return AbruptRejectPromise(cx, args, promiseCapability);
      }

      // GetPromiseResolve
      // Step 2. If IsCallable(promiseResolve) is false,
      //         throw a TypeError exception.
      if (!IsCallable(promiseResolve)) {
        ReportIsNotFunction(cx, promiseResolve);

        // Step 4. IfAbruptRejectPromise(promiseResolve, promiseCapability).
        return AbruptRejectPromise(cx, args, promiseCapability);
      }
    }
  }

  // Step 5.
  IterT iter(cx);
  if (!initIter(iter)) {
    // Note: This applies only to the iterator-based combinators
    // (Promise.all, Promise.allSettled, Promise.race, Promise.any).
    // Step 6. IfAbruptRejectPromise(iteratorRecord, promiseCapability).
    return AbruptRejectPromise(cx, args, promiseCapability);
  }

  // Promise.all
  // Step 7. Let result be
  //         PerformPromiseAll(iteratorRecord, C, promiseCapability,
  //                           promiseResolve).
  // Promise.allSettled
  // Step 7. Let result be
  //         PerformPromiseAllSettled(iteratorRecord, C, promiseCapability,
  //                                  promiseResolve).
  // Promise.race
  // Step 7. Let result be
  //         PerformPromiseRace(iteratorRecord, C, promiseCapability,
  //                            promiseResolve).
  // Promise.any
  // Step 7. Let result be
  //         PerformPromiseAny(iteratorRecord, C, promiseCapability,
  //                           promiseResolve).
  // Promise.allKeyed
  // Step 6. Let result be
  //         PerformPromiseAllKeyed(all, promises, C, promiseCapability,
  //                                promiseResolve).
  // Promise.allSettledKeyed
  // Step 6. Let result be
  //         PerformPromiseAllKeyed(all-settled, promises, C,
  //                                promiseCapability, promiseResolve).
  // Note: Although the spec defines steps 8-9 differently for iterable
  // variants (Promise.all, etc.) vs. property-based variants (Promise.allKeyed,
  // etc.), the control flow is identical: check for error from the perform
  // function, close the iterator if needed, and return the promise. This
  // allows them to share the same code path.
  bool done;
  bool result =
      performFunc(cx, iter, C, promiseCapability, promiseResolve, &done);

  // Step 8. If result is an abrupt completion, then
  if (!result) {
    // Step 8.a.
    maybeCloseIterFunc(iter, done);

    // Step 8.b. IfAbruptRejectPromise(result, promiseCapability).
    return AbruptRejectPromise(cx, args, promiseCapability);
  }

  // Step 9. Return Completion(result).
  args.rval().setObject(*promiseCapability.promise());
  return true;
}

template <typename PerformFuncT>
[[nodiscard]] static bool CommonIterPromiseCombinator(
    JSContext* cx, CallArgs& args, PerformFuncT performFunc,
    const char* nonObjectThisErrorMessage,
    const char* nonIterableErrorMessage) {
  JS::Handle<JS::Value> iterable = args.get(0);

  auto initIter = [&](PromiseForOfIterator& iter) {
    // Step 5. Let iteratorRecord be GetIterator(iterable).
    if (!iter.init(iterable, JS::ForOfIterator::AllowNonIterable)) {
      return false;
    }

    if (!iter.valueIsIterable()) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_NOT_ITERABLE, nonIterableErrorMessage);
      return false;
    }

    return true;
  };

  auto maybeCloseIter = [](PromiseForOfIterator& iter, bool done) {
    // Step 8.a. If iteratorRecord.[[Done]] is false,
    //           set result to IteratorClose(iteratorRecord, result).
    if (!done) {
      iter.closeThrow();
    }
  };

  return CommonPromiseCombinator<PromiseForOfIterator>(
      cx, args, performFunc, nonObjectThisErrorMessage, nonIterableErrorMessage,
      initIter, maybeCloseIter);
}

[[nodiscard]] static bool PerformPromiseAll(
    JSContext* cx, PromiseForOfIterator& iterator, HandleObject C,
    Handle<PromiseCapability> resultCapability, HandleValue promiseResolve,
    bool* done);

/**
 * ES2026 draft rev 00146687f225a64e1b1e2d303acc6139a1adee7d
 *
 * Promise.all ( iterable )
 * https://tc39.es/ecma262/#sec-promise.all
 */
static bool Promise_static_all(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  return CommonIterPromiseCombinator(cx, args, PerformPromiseAll,
                                     "Receiver of Promise.all call",
                                     "Argument of Promise.all");
}

/**
 * Await Dictionary Proposal
 *
 * Unified implementation for property-based promise combinators.
 *
 * Promise.allKeyed ( promises )
 * https://tc39.es/proposal-await-dictionary/#sec-promise.allkeyed
 * Promise.allSettledKeyed ( promises )
 * https://tc39.es/proposal-await-dictionary/#sec-promise.allsettledkeyed
 */
template <typename PerformFuncT>
[[nodiscard]] static bool CommonPromiseCombinatorKeyed(
    JSContext* cx, CallArgs& args, PerformFuncT performFunc,
    const char* nonObjectThisErrorMessage,
    const char* nonObjectArgumentErrorMessage) {
  JS::Handle<JS::Value> promisesVal = args.get(0);

  auto initPromises = [&](JS::Rooted<JSObject*>& promises) {
    // Step 5. If promises is not an Object, then
    if (!promisesVal.isObject()) {
      // Step 5.a. Let error be a newly created TypeError object.
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_OBJECT_REQUIRED,
                                nonObjectArgumentErrorMessage);
      return false;
    }

    promises = &promisesVal.toObject();
    return true;
  };

  auto maybeClosePromises = [](JS::Rooted<JSObject*>& promises, bool done) {};

  auto perform = [&](JSContext* cx, JS::Rooted<JSObject*>& promises,
                     JS::Handle<JSObject*> C,
                     JS::Handle<PromiseCapability> promiseCapability,
                     JS::Handle<JS::Value> promiseResolve, bool* done) {
    *done = true;
    return performFunc(cx, promises, C, promiseCapability, promiseResolve);
  };

  return CommonPromiseCombinator<JS::Rooted<JSObject*>>(
      cx, args, perform, nonObjectThisErrorMessage,
      nonObjectArgumentErrorMessage, initPromises, maybeClosePromises);
}

/**
 * Await Dictionary Proposal
 *
 * Promise.allKeyed ( promises )
 * https://tc39.es/proposal-await-dictionary/#sec-promise.allkeyed
 */
[[nodiscard]] static bool PerformPromiseAllKeyed(
    JSContext* cx, JS::Handle<JSObject*> promises, JS::Handle<JSObject*> C,
    JS::Handle<PromiseCapability> resultCapability,
    JS::Handle<JS::Value> promiseResolve);

static bool Promise_static_allKeyed(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  return CommonPromiseCombinatorKeyed(cx, args, PerformPromiseAllKeyed,
                                      "Receiver of Promise.allKeyed call",
                                      "Argument of Promise.allKeyed");
}

/**
 * Await Dictionary Proposal
 *
 * Promise.allSettledKeyed ( promises )
 * https://tc39.es/proposal-await-dictionary/#sec-promise.allsettledkeyed
 */
[[nodiscard]] static bool PerformPromiseAllSettledKeyed(
    JSContext* cx, JS::Handle<JSObject*> promises, JS::Handle<JSObject*> C,
    JS::Handle<PromiseCapability> resultCapability,
    JS::Handle<JS::Value> promiseResolve);

static bool Promise_static_allSettledKeyed(JSContext* cx, unsigned argc,
                                           Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  return CommonPromiseCombinatorKeyed(
      cx, args, PerformPromiseAllSettledKeyed,
      "Receiver of Promise.allSettledKeyed call",
      "Argument of Promise.allSettledKeyed");
}

[[nodiscard]] static bool PerformPromiseThen(
    JSContext* cx, Handle<PromiseObject*> promise, HandleValue onFulfilled_,
    HandleValue onRejected_, Handle<PromiseCapability> resultCapability);

[[nodiscard]] static bool PerformPromiseThenWithoutSettleHandlers(
    JSContext* cx, Handle<PromiseObject*> promise,
    Handle<PromiseObject*> promiseToResolve,
    Handle<PromiseCapability> resultCapability);

static JSFunction* NewPromiseCombinatorElementFunction(
    JSContext* cx, Native native,
    Handle<PromiseCombinatorDataHolder*> dataHolder, uint32_t index,
    Handle<Value> maybeResolveFunc);

static bool PromiseAllResolveElementFunction(JSContext* cx, unsigned argc,
                                             Value* vp);

/**
 * ES2026 draft rev 00146687f225a64e1b1e2d303acc6139a1adee7d
 *
 * Promise.all ( iterable )
 * https://tc39.es/ecma262/#sec-promise.all
 * PerformPromiseAll ( iteratorRecord, constructor, resultCapability,
 *                     promiseResolve )
 * https://tc39.es/ecma262/#sec-performpromiseall
 *
 * Unforgeable version.
 */
[[nodiscard]] JSObject* js::GetWaitForAllPromise(
    JSContext* cx, JS::HandleObjectVector promises) {
#ifdef DEBUG
  for (size_t i = 0, len = promises.length(); i < len; i++) {
    JSObject* obj = promises[i];
    cx->check(obj);
    JSObject* unwrapped = UncheckedUnwrap(obj);
    MOZ_ASSERT(unwrapped->is<PromiseObject>() || JS_IsDeadWrapper(unwrapped));
  }
#endif

  // Step 1. Let C be the this value.
  RootedObject C(cx,
                 GlobalObject::getOrCreatePromiseConstructor(cx, cx->global()));
  if (!C) {
    return nullptr;
  }

  // Step 2. Let promiseCapability be ? NewPromiseCapability(C).
  Rooted<PromiseCapability> resultCapability(cx);
  if (!NewPromiseCapability(cx, C, &resultCapability, false)) {
    return nullptr;
  }

  // Steps 3-6 for iterator and iteratorRecord.
  // (omitted)

  // Step 7. Let result be
  //         Completion(PerformPromiseAll(iteratorRecord, C, promiseCapability,
  //                           promiseResolve)).
  //
  // Implemented as an inlined, simplied version of PerformPromiseAll.
  {
    uint32_t promiseCount = promises.length();
    // PerformPromiseAll

    // Step 1. Let values be a new empty List.
    Rooted<PromiseCombinatorElements> values(cx);
    {
      auto* valuesArray = NewDenseFullyAllocatedArray(cx, promiseCount);
      if (!valuesArray) {
        return nullptr;
      }
      valuesArray->ensureDenseInitializedLength(0, promiseCount);

      values.initialize(valuesArray);
    }

    // Step 3. Let remainingElementsCount be the Record { [[Value]]: 1 }.
    //
    // Create our data holder that holds all the things shared across
    // every step of the iterator.  In particular, this holds the
    // remainingElementsCount (as an integer reserved slot), the array of
    // values, and the resolve function from our PromiseCapability.
    Rooted<PromiseCombinatorDataHolder*> dataHolder(cx);
    dataHolder = PromiseCombinatorDataHolder::New(
        cx, resultCapability.promise(), values, resultCapability.resolve());
    if (!dataHolder) {
      return nullptr;
    }

    // Call PerformPromiseThen with resolve and reject set to nullptr.
    Rooted<PromiseCapability> resultCapabilityWithoutResolving(cx);
    resultCapabilityWithoutResolving.promise().set(resultCapability.promise());

    // Step 4. Let index be 0.
    // Step 5. Repeat,
    // Step 5.i. Set index to index + 1.
    for (uint32_t index = 0; index < promiseCount; index++) {
      // Steps 5.a-c for IteratorStep.
      // (omitted)

      // Step 5.b.
      // (implemented after the loop).

      // Steps 5.e-g.
      // for IteratorValue
      // (omitted)

      // Step 5.c. Append undefined to values.
      values.unwrappedArray()->setDenseElement(index, UndefinedHandleValue);

      // Step 5.d. Let nextPromise be
      //           ? Call(promiseResolve, constructor, « next »).
      RootedObject nextPromiseObj(cx, promises[index]);

      // Steps 5.e-h.
      JSFunction* resolveFunc = NewPromiseCombinatorElementFunction(
          cx, PromiseAllResolveElementFunction, dataHolder, index,
          UndefinedHandleValue);
      if (!resolveFunc) {
        return nullptr;
      }

      // Step 5.j. Set remainingElementsCount.[[Value]] to
      //           remainingElementsCount.[[Value]] + 1.
      dataHolder->increaseRemainingCount();

      // Step 5.k. Perform
      //           ? Invoke(nextPromise, "then",
      //                    « onFulfilled, resultCapability.[[Reject]] »).
      RootedValue resolveFunVal(cx, ObjectValue(*resolveFunc));
      RootedValue rejectFunVal(cx, ObjectValue(*resultCapability.reject()));
      Rooted<PromiseObject*> nextPromise(cx);

      // GetWaitForAllPromise is used internally only and must not
      // trigger content-observable effects when registering a reaction.
      // It's also meant to work on wrapped Promises, potentially from
      // compartments with principals inaccessible from the current
      // compartment. To make that work, it unwraps promises with
      // UncheckedUnwrap,
      JSObject* unwrapped = UncheckedUnwrap(nextPromiseObj);
      if (JS_IsDeadWrapper(unwrapped)) {
        JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                  JSMSG_DEAD_OBJECT);
        return nullptr;
      }
      nextPromise = &unwrapped->as<PromiseObject>();

      if (!PerformPromiseThen(cx, nextPromise, resolveFunVal, rejectFunVal,
                              resultCapabilityWithoutResolving)) {
        return nullptr;
      }
    }

    // Step 5.b.i. Set remainingElementsCount.[[Value]] to
    //             remainingElementsCount.[[Value]] - 1.
    int32_t remainingCount = dataHolder->decreaseRemainingCount();

    // Step 5.b.ii. If remainingElementsCount.[[Value]] is 0, then
    if (remainingCount == 0) {
      // Step 5.b.ii.1. Let valuesArray be ! CreateArrayFromList(values).
      // (already performed)

      // Step 5.b.ii.2. Perform
      //                ? Call(resultCapability.[[Resolve]], undefined,
      //                       « valuesArray »).
      if (!ResolvePromiseInternal(cx, resultCapability.promise(),
                                  values.value())) {
        return nullptr;
      }
    }
  }

  // Step 5.b.iii. Return resultCapability.[[Promise]].
  return resultCapability.promise();
}

static bool CallDefaultPromiseResolveFunction(JSContext* cx,
                                              Handle<PromiseObject*> promise,
                                              HandleValue resolutionValue);
static bool CallDefaultPromiseRejectFunction(
    JSContext* cx, Handle<PromiseObject*> promise, HandleValue rejectionValue,
    JS::Handle<SavedFrame*> unwrappedRejectionStack = nullptr);

/**
 * Perform Call(promiseCapability.[[Resolve]], undefined ,« value ») given
 * promiseCapability = { promiseObj, resolveFun }.
 *
 * Also,
 *
 * ES2023 draft rev 714fa3dd1e8237ae9c666146270f81880089eca5
 *
 * NewPromiseReactionJob ( reaction, argument )
 * https://tc39.es/ecma262/#sec-newpromisereactionjob
 *
 * Steps 1.f-i. "type is Fulfill" case.
 */
[[nodiscard]] static bool CallPromiseResolveFunction(JSContext* cx,
                                                     HandleObject resolveFun,
                                                     HandleValue value,
                                                     HandleObject promiseObj) {
  cx->check(resolveFun, value, promiseObj);

  // NewPromiseReactionJob
  // Step 1.g. Assert: promiseCapability is a PromiseCapability Record.
  // (implicit)

  if (resolveFun) {
    // NewPromiseReactionJob
    // Step 1.h. If handlerResult is an abrupt completion, then
    //           (handled in CallPromiseRejectFunction)
    // Step 1.i. Else,
    // Step 1.i.i. Return
    //             ? Call(promiseCapability.[[Resolve]], undefined,
    //                    « handlerResult.[[Value]] »).
    RootedValue calleeOrRval(cx, ObjectValue(*resolveFun));
    return Call(cx, calleeOrRval, UndefinedHandleValue, value, &calleeOrRval);
  }

  // `promiseObj` can be optimized away if it's known to be unused.
  //
  // NewPromiseReactionJob
  // Step f. If promiseCapability is undefined, then
  // (reordered)
  //
  // NOTE: "promiseCapability is undefined" case is represented by
  //       `resolveFun == nullptr && promiseObj == nullptr`.
  if (!promiseObj) {
    // NewPromiseReactionJob
    // Step f.i. Assert: handlerResult is not an abrupt completion.
    // (implicit)

    // Step f.ii. Return empty.
    return true;
  }

  // NewPromiseReactionJob
  // Step 1.h. If handlerResult is an abrupt completion, then
  //           (handled in CallPromiseRejectFunction)
  // Step 1.i. Else,
  // Step 1.i.i. Return
  //             ? Call(promiseCapability.[[Resolve]], undefined,
  //                    « handlerResult.[[Value]] »).
  Handle<PromiseObject*> promise = promiseObj.as<PromiseObject>();
  if (IsPromiseWithDefaultResolvingFunction(promise)) {
    return CallDefaultPromiseResolveFunction(cx, promise, value);
  }

  // This case is used by resultCapabilityWithoutResolving in
  // GetWaitForAllPromise, and nothing should be done.

  return true;
}

/**
 * Perform Call(promiseCapability.[[Reject]], undefined ,« reason ») given
 * promiseCapability = { promiseObj, rejectFun }.
 *
 * Also,
 *
 * ES2023 draft rev 714fa3dd1e8237ae9c666146270f81880089eca5
 *
 * NewPromiseReactionJob ( reaction, argument )
 * https://tc39.es/ecma262/#sec-newpromisereactionjob
 *
 * Steps 1.g-i. "type is Reject" case.
 */
[[nodiscard]] static bool CallPromiseRejectFunction(
    JSContext* cx, HandleObject rejectFun, HandleValue reason,
    HandleObject promiseObj, Handle<SavedFrame*> unwrappedRejectionStack,
    UnhandledRejectionBehavior behavior) {
  cx->check(rejectFun, reason, promiseObj);

  // NewPromiseReactionJob
  // Step 1.g. Assert: promiseCapability is a PromiseCapability Record.
  // (implicit)

  if (rejectFun) {
    // NewPromiseReactionJob
    // Step 1.h. If handlerResult is an abrupt completion, then
    // Step 1.h.i. Return
    //             ? Call(promiseCapability.[[Reject]], undefined,
    //                    « handlerResult.[[Value]] »).
    RootedValue calleeOrRval(cx, ObjectValue(*rejectFun));
    return Call(cx, calleeOrRval, UndefinedHandleValue, reason, &calleeOrRval);
  }

  // NewPromiseReactionJob
  // See the comment in CallPromiseResolveFunction for promiseCapability field
  //
  // Step f. If promiseCapability is undefined, then
  // Step f.i. Assert: handlerResult is not an abrupt completion.
  //
  // The spec doesn't allow promiseCapability to be undefined for reject case,
  // but `promiseObj` can be optimized away if it's known to be unused.
  if (!promiseObj) {
    if (behavior == UnhandledRejectionBehavior::Ignore) {
      // Do nothing if unhandled rejections are to be ignored.
      return true;
    }

    // Otherwise create and reject a promise on the fly.  The promise's
    // allocation time will be wrong.  So it goes.
    Rooted<PromiseObject*> temporaryPromise(
        cx, CreatePromiseObjectWithoutResolutionFunctions(cx));
    if (!temporaryPromise) {
      cx->clearPendingException();
      return true;
    }

    // NewPromiseReactionJob
    // Step 1.h. If handlerResult is an abrupt completion, then
    // Step 1.h.i. Return
    //             ? Call(promiseCapability.[[Reject]], undefined,
    //                    « handlerResult.[[Value]] »).
    return RejectPromiseInternal(cx, temporaryPromise, reason,
                                 unwrappedRejectionStack);
  }

  // NewPromiseReactionJob
  // Step 1.h. If handlerResult is an abrupt completion, then
  // Step 1.h.i. Return
  //             ? Call(promiseCapability.[[Reject]], undefined,
  //                    « handlerResult.[[Value]] »).
  Handle<PromiseObject*> promise = promiseObj.as<PromiseObject>();
  if (IsPromiseWithDefaultResolvingFunction(promise)) {
    return CallDefaultPromiseRejectFunction(cx, promise, reason,
                                            unwrappedRejectionStack);
  }

  // This case is used by resultCapabilityWithoutResolving in
  // GetWaitForAllPromise, and nothing should be done.

  return true;
}

[[nodiscard]] static JSObject* CommonStaticResolveImpl(JSContext* cx,
                                                       HandleObject C,
                                                       HandleValue argVal);

static bool IsPromiseSpecies(JSContext* cx, JSFunction* species);

/**
 * ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
 *
 * Unified implementation of
 *
 * PerformPromiseAll ( iteratorRecord, constructor, resultCapability,
 *                     promiseResolve )
 * https://tc39.es/ecma262/#sec-performpromiseall
 * PerformPromiseAllSettled ( iteratorRecord, constructor, resultCapability,
 *                            promiseResolve )
 * https://tc39.es/ecma262/#sec-performpromiseallsettled
 * PerformPromiseRace ( iteratorRecord, constructor, resultCapability,
 *                      promiseResolve )
 * https://tc39.es/ecma262/#sec-performpromiserace
 * PerformPromiseAny ( iteratorRecord, constructor, resultCapability,
 *                     promiseResolve )
 * https://tc39.es/ecma262/#sec-performpromiseany
 *
 * Promise.prototype.then ( onFulfilled, onRejected )
 * https://tc39.es/ecma262/#sec-promise.prototype.then
 */
template <typename GetNextFuncT, typename GetResolveAndRejectFuncT>
[[nodiscard]] static bool CommonPerformPromiseCombinator(
    JSContext* cx, HandleObject C, HandleObject resultPromise,
    HandleValue promiseResolve, bool iterationMayHaveSideEffects, bool* done,
    bool resolveReturnsUndefined, GetNextFuncT getNextFunc,
    GetResolveAndRejectFuncT getResolveAndReject) {
  RootedObject promiseCtor(
      cx, GlobalObject::getOrCreatePromiseConstructor(cx, cx->global()));
  if (!promiseCtor) {
    return false;
  }

  // Try to optimize when the Promise object is in its default state, guarded
  // by |C == promiseCtor| because we can only perform this optimization
  // for the builtin Promise constructor.
  bool isDefaultPromiseState =
      C == promiseCtor && HasDefaultPromiseProperties(cx);
  bool validatePromiseState = iterationMayHaveSideEffects;

  RootedValue CVal(cx, ObjectValue(*C));
  RootedValue resolveFunVal(cx);
  RootedValue rejectFunVal(cx);

  // We're reusing rooted variables in the loop below, so we don't need to
  // declare a gazillion different rooted variables here. Rooted variables
  // which are reused include "Or" in their name.
  RootedValue nextValueOrNextPromise(cx);
  RootedObject nextPromiseObj(cx);
  RootedValue thenVal(cx);
  RootedObject thenSpeciesOrBlockedPromise(cx);
  Rooted<PromiseCapability> thenCapability(cx);

  // PerformPromiseAll, PerformPromiseAllSettled, PerformPromiseAny
  // Step 4.
  // PerformPromiseRace
  // Step 1.
  while (true) {
    // Step a.
    RootedValue& nextValue = nextValueOrNextPromise;
    if (!getNextFunc(&nextValue, done)) {
      return false;
    }

    // Step b. If next is done, then
    if (*done) {
      return true;
    }

    // Set to false when we can skip the [[Get]] for "then" and instead
    // use the built-in Promise.prototype.then function.
    bool getThen = true;

    if (isDefaultPromiseState && validatePromiseState) {
      isDefaultPromiseState = HasDefaultPromiseProperties(cx);
    }

    RootedValue& nextPromise = nextValueOrNextPromise;
    if (isDefaultPromiseState) {
      PromiseObject* nextValuePromise = nullptr;
      if (nextValue.isObject() && nextValue.toObject().is<PromiseObject>()) {
        nextValuePromise = &nextValue.toObject().as<PromiseObject>();
      }

      if (nextValuePromise &&
          IsPromiseWithDefaultProperties(nextValuePromise, cx)) {
        // The below steps don't produce any side-effects, so we can
        // skip the Promise state revalidation in the next iteration
        // when the iterator itself also doesn't produce any
        // side-effects.
        validatePromiseState = iterationMayHaveSideEffects;

        // Step {c, d}. Let nextPromise be
        //              ? Call(promiseResolve, constructor, « next »).
        // Promise.resolve is a no-op for the default case.
        MOZ_ASSERT(&nextPromise.toObject() == nextValuePromise);

        // `nextPromise` uses the built-in `then` function.
        getThen = false;
      } else {
        // Need to revalidate the Promise state in the next iteration,
        // because CommonStaticResolveImpl may have modified it.
        validatePromiseState = true;

        // Step {c, d}. Let nextPromise be
        //              ? Call(promiseResolve, constructor, « next »).
        // Inline the call to Promise.resolve.
        JSObject* res = CommonStaticResolveImpl(cx, C, nextValue);
        if (!res) {
          return false;
        }

        nextPromise.setObject(*res);
      }
    } else if (promiseResolve.isUndefined()) {
      // |promiseResolve| is undefined when the Promise constructor was
      // initially in its default state, i.e. if it had been retrieved, it would
      // have been set to |Promise.resolve|.

      // Step {c, d}. Let nextPromise be
      //              ? Call(promiseResolve, constructor, « next »).
      // Inline the call to Promise.resolve.
      JSObject* res = CommonStaticResolveImpl(cx, C, nextValue);
      if (!res) {
        return false;
      }

      nextPromise.setObject(*res);
    } else {
      // Step {c, d}. Let nextPromise be
      //              ? Call(promiseResolve, constructor, « next »).
      if (!Call(cx, promiseResolve, CVal, nextValue, &nextPromise)) {
        return false;
      }
    }

    // Get the resolving functions for this iteration.
    // PerformPromiseAll
    // Steps c and e-m.
    // PerformPromiseAllSettled
    // Steps c and e-v.
    // PerformPromiseAny
    // Steps c and e-m.
    if (!getResolveAndReject(&resolveFunVal, &rejectFunVal)) {
      return false;
    }

    // Call |nextPromise.then| with the provided hooks and add
    // |resultPromise| to the list of dependent promises.
    //
    // If |nextPromise.then| is the original |Promise.prototype.then|
    // function and the call to |nextPromise.then| would use the original
    // |Promise| constructor to create the resulting promise, we skip the
    // call to |nextPromise.then| and thus creating a new promise that
    // would not be observable by content.

    // PerformPromiseAll
    // Step n. Perform
    //         ? Invoke(nextPromise, "then",
    //                  « onFulfilled, resultCapability.[[Reject]] »).
    // PerformPromiseAllSettled
    // Step w. Perform
    //         ? Invoke(nextPromise, "then", « onFulfilled, onRejected »).
    // PerformPromiseRace
    // Step d. Perform
    //         ? Invoke(nextPromise, "then",
    //                  « resultCapability.[[Resolve]],
    //                    resultCapability.[[Reject]] »).
    // PerformPromiseAny
    // Step n. Perform
    //         ? Invoke(nextPromise, "then",
    //                  « resultCapability.[[Resolve]], onRejected »).
    nextPromiseObj = ToObject(cx, nextPromise);
    if (!nextPromiseObj) {
      return false;
    }

    bool isBuiltinThen;
    if (getThen) {
      // We don't use the Promise lookup cache here, because this code
      // is only called when we had a lookup cache miss, so it's likely
      // we'd get another cache miss when trying to use the cache here.
      if (!GetProperty(cx, nextPromiseObj, nextPromise, cx->names().then,
                       &thenVal)) {
        return false;
      }

      // |nextPromise| is an unwrapped Promise, and |then| is the
      // original |Promise.prototype.then|, inline it here.
      isBuiltinThen = nextPromiseObj->is<PromiseObject>() &&
                      IsNativeFunction(thenVal, Promise_then);
    } else {
      isBuiltinThen = true;
    }

    // By default, the blocked promise is added as an extra entry to the
    // rejected promises list.
    bool addToDependent = true;

    if (isBuiltinThen) {
      MOZ_ASSERT(nextPromise.isObject());
      MOZ_ASSERT(&nextPromise.toObject() == nextPromiseObj);

      // Promise.prototype.then
      // Step 3. Let C be ? SpeciesConstructor(promise, %Promise%).
      RootedObject& thenSpecies = thenSpeciesOrBlockedPromise;
      if (getThen) {
        thenSpecies = SpeciesConstructor(cx, nextPromiseObj, JSProto_Promise,
                                         IsPromiseSpecies);
        if (!thenSpecies) {
          return false;
        }
      } else {
        thenSpecies = promiseCtor;
      }

      // The fast path here and the one in NewPromiseCapability may not
      // set the resolve and reject handlers, so we need to clear the
      // fields in case they were set in the previous iteration.
      thenCapability.resolve().set(nullptr);
      thenCapability.reject().set(nullptr);

      // Skip the creation of a built-in Promise object if:
      // 1. `thenSpecies` is the built-in Promise constructor.
      // 2. `resolveFun` doesn't return an object, which ensures no side effects
      //    occur in ResolvePromiseInternal.
      // 3. The result promise is a built-in Promise object.
      // 4. The result promise doesn't use the default resolving functions,
      //    which in turn means Run{Fulfill,Reject}Function when called from
      //    PromiseReactionJob won't try to resolve the promise.
      if (thenSpecies == promiseCtor && resolveReturnsUndefined &&
          resultPromise->is<PromiseObject>() &&
          !IsPromiseWithDefaultResolvingFunction(
              &resultPromise->as<PromiseObject>())) {
        thenCapability.promise().set(resultPromise);
        addToDependent = false;
      } else {
        // Promise.prototype.then
        // Step 4. Let resultCapability be ? NewPromiseCapability(C).
        if (!NewPromiseCapability(cx, thenSpecies, &thenCapability, true)) {
          return false;
        }
      }

      // Promise.prototype.then
      // Step 5. Return
      //         PerformPromiseThen(promise, onFulfilled, onRejected,
      //                            resultCapability).
      Handle<PromiseObject*> promise = nextPromiseObj.as<PromiseObject>();
      if (!PerformPromiseThen(cx, promise, resolveFunVal, rejectFunVal,
                              thenCapability)) {
        return false;
      }
    } else {
      RootedValue& ignored = thenVal;
      if (!Call(cx, thenVal, nextPromise, resolveFunVal, rejectFunVal,
                &ignored)) {
        return false;
      }

      // In case the value to depend on isn't an object at all, there's
      // nothing more to do here: we can only add reactions to Promise
      // objects (potentially after unwrapping them), and non-object
      // values can't be Promise objects. This can happen if Promise.all
      // is called on an object with a `resolve` method that returns
      // primitives.
      if (!nextPromise.isObject()) {
        addToDependent = false;
      }
    }

    // Adds |resultPromise| to the list of dependent promises.
    if (addToDependent) {
      // The object created by the |promise.then| call or the inlined
      // version of it above is visible to content (either because
      // |promise.then| was overridden by content and could leak it,
      // or because a constructor other than the original value of
      // |Promise| was used to create it). To have both that object and
      // |resultPromise| show up as dependent promises in the debugger,
      // add a dummy reaction to the list of reject reactions that
      // contains |resultPromise|, but otherwise does nothing.
      RootedObject& blockedPromise = thenSpeciesOrBlockedPromise;
      blockedPromise = resultPromise;

      mozilla::Maybe<AutoRealm> ar;
      if (IsProxy(nextPromiseObj)) {
        nextPromiseObj = CheckedUnwrapStatic(nextPromiseObj);
        if (!nextPromiseObj) {
          ReportAccessDenied(cx);
          return false;
        }
        if (JS_IsDeadWrapper(nextPromiseObj)) {
          JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                    JSMSG_DEAD_OBJECT);
          return false;
        }
        ar.emplace(cx, nextPromiseObj);
        if (!cx->compartment()->wrap(cx, &blockedPromise)) {
          return false;
        }
      }

      // If either the object to depend on (`nextPromiseObj`) or the
      // object that gets blocked (`resultPromise`) isn't a,
      // maybe-wrapped, Promise instance, we ignore it. All this does is
      // lose some small amount of debug information in scenarios that
      // are highly unlikely to occur in useful code.
      if (nextPromiseObj->is<PromiseObject>() &&
          resultPromise->is<PromiseObject>()) {
        Handle<PromiseObject*> promise = nextPromiseObj.as<PromiseObject>();
        if (!AddDummyPromiseReactionForDebugger(cx, promise, blockedPromise)) {
          return false;
        }
      }
    }
  }
}

template <typename T>
[[nodiscard]] static bool CommonPerformIterPromiseCombinator(
    JSContext* cx, PromiseForOfIterator& iterator, HandleObject C,
    HandleObject resultPromise, HandleValue promiseResolve, bool* done,
    bool resolveReturnsUndefined, T getResolveAndReject) {
  // Optimized dense array iteration ensures no side-effects take place
  // during the iteration.
  bool iterationMayHaveSideEffects = !iterator.isOptimizedDenseArrayIteration();

  auto getNextFunc = [&](JS::MutableHandle<JS::Value> nextValue, bool* done) {
    // PerformPromiseAll, PerformPromiseAllSettled, PerformPromiseAny
    // Step 4.a. Let next be ? IteratorStepValue(iteratorRecord).
    // PerformPromiseRace
    // Step 1.a. Let next be ? IteratorStepValue(iteratorRecord).
    if (!iterator.next(nextValue, done)) {
      *done = true;
      return false;
    }
    return true;
  };

  return CommonPerformPromiseCombinator(
      cx, C, resultPromise, promiseResolve, iterationMayHaveSideEffects, done,
      resolveReturnsUndefined, getNextFunc, getResolveAndReject);
}

// Create the elements for the Promise combinators Promise.all and
// Promise.allSettled.
// Create the errors list for the Promise combinator Promise.any.
[[nodiscard]] static bool NewPromiseCombinatorElements(
    JSContext* cx, Handle<PromiseCapability> resultCapability,
    MutableHandle<PromiseCombinatorElements> elements) {
  // We have to be very careful about which compartments we create things for
  // the Promise combinators. In particular, we have to maintain the invariant
  // that anything stored in a reserved slot is same-compartment with the object
  // whose reserved slot it's in. But we want to create the values array in the
  // compartment of the result capability's Promise, because that array can get
  // exposed as the Promise's resolution value to code that has access to the
  // Promise (in particular code from that compartment), and that should work,
  // even if the Promise compartment is less-privileged than our caller
  // compartment.
  //
  // So the plan is as follows: Create the values array in the promise
  // compartment. Create the promise resolving functions and the data holder in
  // our current compartment, i.e. the compartment of the Promise combinator
  // function. Store a cross-compartment wrapper to the values array in the
  // holder. This should be OK because the only things we hand the promise
  // resolving functions to are the "then" calls we do and in the case when the
  // Promise's compartment is not the current compartment those are happening
  // over Xrays anyway, which means they get the canonical "then" function and
  // content can't see our promise resolving functions.

  if (IsWrapper(resultCapability.promise())) {
    JSObject* unwrappedPromiseObj =
        CheckedUnwrapStatic(resultCapability.promise());
    MOZ_ASSERT(unwrappedPromiseObj);

    {
      AutoRealm ar(cx, unwrappedPromiseObj);
      auto* array = NewDenseEmptyArray(cx);
      if (!array) {
        return false;
      }
      elements.initialize(array);
    }

    if (!cx->compartment()->wrap(cx, elements.value())) {
      return false;
    }
  } else {
    auto* array = NewDenseEmptyArray(cx);
    if (!array) {
      return false;
    }

    elements.initialize(array);
  }
  return true;
}

// Retrieve the combinator elements from the data holder.
[[nodiscard]] static bool GetPromiseCombinatorElements(
    JSContext* cx, Handle<PromiseCombinatorDataHolder*> data,
    MutableHandle<PromiseCombinatorElements> elements) {
  bool needsWrapping = false;
  JSObject* valuesObj = &data->valuesArray().toObject();
  if (IsProxy(valuesObj)) {
    // See comment for NewPromiseCombinatorElements for why we unwrap here.
    valuesObj = UncheckedUnwrap(valuesObj);

    if (JS_IsDeadWrapper(valuesObj)) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_DEAD_OBJECT);
      return false;
    }

    needsWrapping = true;
  }

  elements.initialize(data, &valuesObj->as<ArrayObject>(), needsWrapping);
  return true;
}

static JSFunction* NewPromiseCombinatorElementFunction(
    JSContext* cx, Native native,
    Handle<PromiseCombinatorDataHolder*> dataHolder, uint32_t index,
    Handle<Value> maybeResolveFunc) {
  JSFunction* fn = NewNativeFunction(
      cx, native, 1, nullptr, gc::AllocKind::FUNCTION_EXTENDED, GenericObject);
  if (!fn) {
    return nullptr;
  }

  // See the PromiseCombinatorElementFunctionSlots comment for an explanation of
  // how these two slots are used.
  if (maybeResolveFunc.isObject()) {
    fn->setExtendedSlot(
        PromiseCombinatorElementFunctionSlot_ElementIndexOrResolveFunc,
        maybeResolveFunc);
    fn->setExtendedSlot(PromiseCombinatorElementFunctionSlot_Data, NullValue());
  } else {
    fn->setExtendedSlot(
        PromiseCombinatorElementFunctionSlot_ElementIndexOrResolveFunc,
        Int32Value(index));
    fn->setExtendedSlot(PromiseCombinatorElementFunctionSlot_Data,
                        ObjectValue(*dataHolder));
  }
  return fn;
}

/**
 * ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
 *
 * Unified implementation of
 *
 * Promise.all Resolve Element Functions
 * https://tc39.es/ecma262/#sec-promise.all-resolve-element-functions
 *
 * Steps 1-4.
 *
 * Promise.allSettled Resolve Element Functions
 * https://tc39.es/ecma262/#sec-promise.allsettled-resolve-element-functions
 *
 * Steps 1-5.
 *
 * Promise.allSettled Reject Element Functions
 * https://tc39.es/ecma262/#sec-promise.allsettled-reject-element-functions
 *
 * Steps 1-5.
 *
 * Promise.any Reject Element Functions
 * https://tc39.es/ecma262/#sec-promise.any-reject-element-functions
 *
 * Steps 1-4.
 *
 * Await Dictionary Proposal
 *
 * PerformPromiseAllKeyed ( promises, constructor, resultCapability,
 *                          promiseResolve )
 * https://tc39.es/proposal-await-dictionary/#sec-performpromiseallkeyed
 *
 * Step 6.b.vi.1-2.
 * Step 6.b.ix.2.a-b.
 *
 * Common implementation for Promise combinator element functions to check if
 * they've already been called.
 */
template <typename DataHolderT>
static bool PromiseCombinatorElementFunctionAlreadyCalled(
    const CallArgs& args, MutableHandle<DataHolderT*> data, uint32_t* index) {
  // Step 1. Let F be the active function object.
  // (implicit for PerformPromiseAllKeyed)
  JSFunction* fn = &args.callee().as<JSFunction>();

  // Promise.{all,any} functions
  // Step 2. If F.[[AlreadyCalled]] is true, return undefined.
  // Promise.allSettled functions
  // Step 2. Let alreadyCalled be F.[[AlreadyCalled]].
  // Step 3. If alreadyCalled.[[Value]] is true, return undefined.
  // PerformPromiseAllKeyed
  // Step 6.b.vi.1 / Step 6.b.ix.2.a.
  //   If alreadyCalled.[[Value]] is true, return undefined.
  //
  // We use the existence of the data holder as a signal for whether the Promise
  // combinator element function was already called. Upon resolution, it's reset
  // to `undefined`.
  //
  // For Promise.allSettled and Promise.allSettledKeyed, the [[AlreadyCalled]]
  // state must be shared by the two functions, so we always use the resolve
  // function's state.

  constexpr size_t indexOrResolveFuncSlot =
      PromiseCombinatorElementFunctionSlot_ElementIndexOrResolveFunc;
  if (fn->getExtendedSlot(indexOrResolveFuncSlot).isObject()) {
    // This is a reject function for Promise.allSettled and
    // Promise.allSettledKeyed. Get the corresponding resolve function.
    Value slotVal = fn->getExtendedSlot(indexOrResolveFuncSlot);
    fn = &slotVal.toObject().as<JSFunction>();
  }
  MOZ_RELEASE_ASSERT(fn->getExtendedSlot(indexOrResolveFuncSlot).isInt32());

  const Value& dataVal =
      fn->getExtendedSlot(PromiseCombinatorElementFunctionSlot_Data);
  if (dataVal.isUndefined()) {
    return true;
  }

  data.set(&dataVal.toObject().as<DataHolderT>());

  // Promise.{all,any} functions
  // Step 3. Set F.[[AlreadyCalled]] to true.
  // Promise.allSettled functions
  // Step 4. Set alreadyCalled.[[Value]] to true.
  // PerformPromiseAllKeyed
  // Step 6.b.vi.2 / Step 6.b.ix.2.b.
  //   Set alreadyCalled.[[Value]] to true.
  fn->setExtendedSlot(PromiseCombinatorElementFunctionSlot_Data,
                      UndefinedValue());

  // Promise.{all,any} functions
  // Step 4. Let index be F.[[Index]].
  // Promise.allSettled functions
  // Step 5. Let index be F.[[Index]].
  // PerformPromiseAllKeyed
  // (implicit)
  int32_t idx = fn->getExtendedSlot(indexOrResolveFuncSlot).toInt32();
  MOZ_ASSERT(idx >= 0);
  *index = uint32_t(idx);

  return false;
}

/**
 * ES2026 draft rev 00146687f225a64e1b1e2d303acc6139a1adee7d
 *
 * PerformPromiseAll ( iteratorRecord, constructor, resultCapability,
 *                     promiseResolve )
 * https://tc39.es/ecma262/#sec-performpromiseall
 */
[[nodiscard]] static bool PerformPromiseAll(
    JSContext* cx, PromiseForOfIterator& iterator, HandleObject C,
    Handle<PromiseCapability> resultCapability, HandleValue promiseResolve,
    bool* done) {
  *done = false;

  MOZ_ASSERT(C->isConstructor());

  // Step 1. Let values be a new empty List.
  Rooted<PromiseCombinatorElements> values(cx);
  if (!NewPromiseCombinatorElements(cx, resultCapability, &values)) {
    return false;
  }

  // Step 3. Let remainingElementsCount be the Record { [[Value]]: 1 }.
  //
  // Create our data holder that holds all the things shared across
  // every step of the iterator.  In particular, this holds the
  // remainingElementsCount (as an integer reserved slot), the array of
  // values, and the resolve function from our PromiseCapability.
  Rooted<PromiseCombinatorDataHolder*> dataHolder(cx);
  dataHolder = PromiseCombinatorDataHolder::New(
      cx, resultCapability.promise(), values, resultCapability.resolve());
  if (!dataHolder) {
    return false;
  }

  // Step 4. Let index be 0.
  uint32_t index = 0;

  auto getResolveAndReject = [cx, &resultCapability, &values, &dataHolder,
                              &index](MutableHandleValue resolveFunVal,
                                      MutableHandleValue rejectFunVal) {
    // Step 5.c. Append undefined to values.
    if (!values.pushUndefined(cx)) {
      return false;
    }

    // Steps 5.e-k.
    JSFunction* resolveFunc = NewPromiseCombinatorElementFunction(
        cx, PromiseAllResolveElementFunction, dataHolder, index,
        UndefinedHandleValue);
    if (!resolveFunc) {
      return false;
    }

    // Step 5.j. Set remainingElementsCount.[[Value]] to
    //           remainingElementsCount.[[Value]] + 1.
    dataHolder->increaseRemainingCount();

    // Step 5.i. Set index to index + 1.
    index++;
    MOZ_ASSERT(index > 0);

    resolveFunVal.setObject(*resolveFunc);
    rejectFunVal.setObject(*resultCapability.reject());
    return true;
  };

  // Step 5. Repeat,
  if (!CommonPerformIterPromiseCombinator(
          cx, iterator, C, resultCapability.promise(), promiseResolve, done,
          true, getResolveAndReject)) {
    return false;
  }

  // Step 5.b.i. Set remainingElementsCount.[[Value]] to
  //             remainingElementsCount.[[Value]] - 1.
  int32_t remainingCount = dataHolder->decreaseRemainingCount();

  // Step 5.b.ii. If remainingElementsCount.[[Value]] is 0, then
  if (remainingCount == 0) {
    // Step 5.b.ii.1. Let valuesArray be CreateArrayFromList(values).
    // (already performed)

    // Step 5.b.ii.2. Perform
    //                ? Call(resultCapability.[[Resolve]], undefined,
    //                       « valuesArray »).
    return CallPromiseResolveFunction(cx, resultCapability.resolve(),
                                      values.value(),
                                      resultCapability.promise());
  }

  // Step 5.b.iii. Return resultCapability.[[Promise]].
  return true;
}

/**
 * ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
 *
 * Promise.all Resolve Element Functions
 * https://tc39.es/ecma262/#sec-promise.all-resolve-element-functions
 */
static bool PromiseAllResolveElementFunction(JSContext* cx, unsigned argc,
                                             Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  HandleValue xVal = args.get(0);

  // Steps 1-4.
  Rooted<PromiseCombinatorDataHolder*> data(cx);
  uint32_t index;
  if (PromiseCombinatorElementFunctionAlreadyCalled<
          PromiseCombinatorDataHolder>(args, &data, &index)) {
    args.rval().setUndefined();
    return true;
  }

  // Step 5. Let values be F.[[Values]].
  Rooted<PromiseCombinatorElements> values(cx);
  if (!GetPromiseCombinatorElements(cx, data, &values)) {
    return false;
  }

  // Step 8. Set values[index] to x.
  if (!values.setElement(cx, index, xVal)) {
    return false;
  }

  // (reordered)
  // Step 7. Let remainingElementsCount be F.[[RemainingElements]].
  //
  // Step 9. Set remainingElementsCount.[[Value]] to
  //         remainingElementsCount.[[Value]] - 1.
  uint32_t remainingCount = data->decreaseRemainingCount();

  // Step 10. If remainingElementsCount.[[Value]] is 0, then
  if (remainingCount == 0) {
    // Step 10.a. Let valuesArray be CreateArrayFromList(values).
    // (already performed)

    // (reordered)
    // Step 6. Let promiseCapability be F.[[Capability]].
    //
    // Step 10.b. Return
    //            ? Call(promiseCapability.[[Resolve]], undefined,
    //                   « valuesArray »).
    RootedObject resolveAllFun(cx, data->resolveOrRejectObj());
    RootedObject promiseObj(cx, data->promiseObj());
    if (!CallPromiseResolveFunction(cx, resolveAllFun, values.value(),
                                    promiseObj)) {
      return false;
    }
  }

  // Step 11. Return undefined.
  args.rval().setUndefined();
  return true;
}

[[nodiscard]] static bool PerformPromiseRace(
    JSContext* cx, PromiseForOfIterator& iterator, HandleObject C,
    Handle<PromiseCapability> resultCapability, HandleValue promiseResolve,
    bool* done);

/**
 * ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
 *
 * Promise.race ( iterable )
 * https://tc39.es/ecma262/#sec-promise.race
 */
static bool Promise_static_race(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  return CommonIterPromiseCombinator(cx, args, PerformPromiseRace,
                                     "Receiver of Promise.race call",
                                     "Argument of Promise.race");
}

/**
 * ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
 *
 * PerformPromiseRace ( iteratorRecord, constructor, resultCapability,
 *                      promiseResolve )
 * https://tc39.es/ecma262/#sec-performpromiserace
 */
[[nodiscard]] static bool PerformPromiseRace(
    JSContext* cx, PromiseForOfIterator& iterator, HandleObject C,
    Handle<PromiseCapability> resultCapability, HandleValue promiseResolve,
    bool* done) {
  *done = false;

  MOZ_ASSERT(C->isConstructor());

  // BlockOnPromise fast path requires the passed onFulfilled function
  // doesn't return an object value, because otherwise the skipped promise
  // creation is detectable due to missing property lookups.
  bool isDefaultResolveFn =
      IsNativeFunction(resultCapability.resolve(), ResolvePromiseFunction);

  auto getResolveAndReject = [&resultCapability](
                                 MutableHandleValue resolveFunVal,
                                 MutableHandleValue rejectFunVal) {
    resolveFunVal.setObject(*resultCapability.resolve());
    rejectFunVal.setObject(*resultCapability.reject());
    return true;
  };

  // Step 1. Repeat,
  return CommonPerformIterPromiseCombinator(
      cx, iterator, C, resultCapability.promise(), promiseResolve, done,
      isDefaultResolveFn, getResolveAndReject);
}

enum class PromiseAllSettledElementFunctionKind { Resolve, Reject };

template <PromiseAllSettledElementFunctionKind Kind>
static bool PromiseAllSettledElementFunction(JSContext* cx, unsigned argc,
                                             Value* vp);

[[nodiscard]] static bool PerformPromiseAllSettled(
    JSContext* cx, PromiseForOfIterator& iterator, HandleObject C,
    Handle<PromiseCapability> resultCapability, HandleValue promiseResolve,
    bool* done);

/**
 * ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
 *
 * Promise.allSettled ( iterable )
 * https://tc39.es/ecma262/#sec-promise.allsettled
 */
static bool Promise_static_allSettled(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  return CommonIterPromiseCombinator(cx, args, PerformPromiseAllSettled,
                                     "Receiver of Promise.allSettled call",
                                     "Argument of Promise.allSettled");
}

/**
 * ES2026 draft rev 00146687f225a64e1b1e2d303acc6139a1adee7d
 *
 * PerformPromiseAllSettled ( iteratorRecord, constructor, resultCapability,
 *                            promiseResolve )
 * https://tc39.es/ecma262/#sec-performpromiseallsettled
 */
[[nodiscard]] static bool PerformPromiseAllSettled(
    JSContext* cx, PromiseForOfIterator& iterator, HandleObject C,
    Handle<PromiseCapability> resultCapability, HandleValue promiseResolve,
    bool* done) {
  *done = false;

  MOZ_ASSERT(C->isConstructor());

  // Step 1. Let values be a new empty List.
  Rooted<PromiseCombinatorElements> values(cx);
  if (!NewPromiseCombinatorElements(cx, resultCapability, &values)) {
    return false;
  }

  // Step 3. Let remainingElementsCount be the Record { [[Value]]: 1 }.
  //
  // Create our data holder that holds all the things shared across every step
  // of the iterator. In particular, this holds the remainingElementsCount
  // (as an integer reserved slot), the array of values, and the resolve
  // function from our PromiseCapability.
  Rooted<PromiseCombinatorDataHolder*> dataHolder(cx);
  dataHolder = PromiseCombinatorDataHolder::New(
      cx, resultCapability.promise(), values, resultCapability.resolve());
  if (!dataHolder) {
    return false;
  }

  // Step 4. Let index be 0.
  uint32_t index = 0;

  auto getResolveAndReject = [cx, &values, &dataHolder, &index](
                                 MutableHandleValue resolveFunVal,
                                 MutableHandleValue rejectFunVal) {
    // Step 5.c. Append undefined to values.
    if (!values.pushUndefined(cx)) {
      return false;
    }

    auto PromiseAllSettledResolveElementFunction =
        PromiseAllSettledElementFunction<
            PromiseAllSettledElementFunctionKind::Resolve>;
    auto PromiseAllSettledRejectElementFunction =
        PromiseAllSettledElementFunction<
            PromiseAllSettledElementFunctionKind::Reject>;

    // Steps 5.f-i.
    JSFunction* resolveFunc = NewPromiseCombinatorElementFunction(
        cx, PromiseAllSettledResolveElementFunction, dataHolder, index,
        UndefinedHandleValue);
    if (!resolveFunc) {
      return false;
    }
    resolveFunVal.setObject(*resolveFunc);

    // Steps 5.j-m.
    JSFunction* rejectFunc = NewPromiseCombinatorElementFunction(
        cx, PromiseAllSettledRejectElementFunction, dataHolder, index,
        resolveFunVal);
    if (!rejectFunc) {
      return false;
    }
    rejectFunVal.setObject(*rejectFunc);

    // Step 5.o. Set remainingElementsCount.[[Value]] to
    //           remainingElementsCount.[[Value]] + 1.
    dataHolder->increaseRemainingCount();

    // Step 5.n. Set index to index + 1.
    index++;
    MOZ_ASSERT(index > 0);

    return true;
  };

  // Step 5. Repeat,
  if (!CommonPerformIterPromiseCombinator(
          cx, iterator, C, resultCapability.promise(), promiseResolve, done,
          true, getResolveAndReject)) {
    return false;
  }

  // Step 5.b.i. Set remainingElementsCount.[[Value]] to
  //              remainingElementsCount.[[Value]] - 1.
  int32_t remainingCount = dataHolder->decreaseRemainingCount();

  // Step 5.b.ii. If remainingElementsCount.[[Value]] is 0, then
  if (remainingCount == 0) {
    // Step 5.b.ii.1. Let valuesArray be CreateArrayFromList(values).
    // (already performed)

    // Step 5.b.ii.2. Perform
    //                ? Call(resultCapability.[[Resolve]], undefined,
    //                       « valuesArray »).
    return CallPromiseResolveFunction(cx, resultCapability.resolve(),
                                      values.value(),
                                      resultCapability.promise());
  }

  return true;
}

/**
 * ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
 *
 * Unified implementation of
 *
 * Promise.allSettled Resolve Element Functions
 * https://tc39.es/ecma262/#sec-promise.allsettled-resolve-element-functions
 * Promise.allSettled Reject Element Functions
 * https://tc39.es/ecma262/#sec-promise.allsettled-reject-element-functions
 */
template <PromiseAllSettledElementFunctionKind Kind>
static bool PromiseAllSettledElementFunction(JSContext* cx, unsigned argc,
                                             Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  HandleValue valueOrReason = args.get(0);

  // Steps 1-5.
  Rooted<PromiseCombinatorDataHolder*> data(cx);
  uint32_t index;
  if (PromiseCombinatorElementFunctionAlreadyCalled<
          PromiseCombinatorDataHolder>(args, &data, &index)) {
    args.rval().setUndefined();
    return true;
  }

  // Step 6. Let values be F.[[Values]].
  Rooted<PromiseCombinatorElements> values(cx);
  if (!GetPromiseCombinatorElements(cx, data, &values)) {
    return false;
  }

  // Step 9. Let obj be OrdinaryObjectCreate(%Object.prototype%).
  Rooted<PlainObject*> obj(cx, NewPlainObject(cx));
  if (!obj) {
    return false;
  }

  // Promise.allSettled Resolve Element Functions
  // Step 10. Perform ! CreateDataPropertyOrThrow(obj, "status", "fulfilled").
  // Promise.allSettled Reject Element Functions
  // Step 10. Perform ! CreateDataPropertyOrThrow(obj, "status", "rejected").
  RootedId id(cx, NameToId(cx->names().status));
  RootedValue statusValue(cx);
  if (Kind == PromiseAllSettledElementFunctionKind::Resolve) {
    statusValue.setString(cx->names().fulfilled);
  } else {
    statusValue.setString(cx->names().rejected);
  }
  if (!NativeDefineDataProperty(cx, obj, id, statusValue, JSPROP_ENUMERATE)) {
    return false;
  }

  // Promise.allSettled Resolve Element Functions
  // Step 11. Perform ! CreateDataPropertyOrThrow(obj, "value", x).
  // Promise.allSettled Reject Element Functions
  // Step 11. Perform ! CreateDataPropertyOrThrow(obj, "reason", x).
  if (Kind == PromiseAllSettledElementFunctionKind::Resolve) {
    id = NameToId(cx->names().value);
  } else {
    id = NameToId(cx->names().reason);
  }
  if (!NativeDefineDataProperty(cx, obj, id, valueOrReason, JSPROP_ENUMERATE)) {
    return false;
  }

  // Step 12. Set values[index] to obj.
  RootedValue objVal(cx, ObjectValue(*obj));
  if (!values.setElement(cx, index, objVal)) {
    return false;
  }

  // (reordered)
  // Step 8. Let remainingElementsCount be F.[[RemainingElements]].
  //
  // Step 13. Set remainingElementsCount.[[Value]] to
  // remainingElementsCount.[[Value]] - 1.
  uint32_t remainingCount = data->decreaseRemainingCount();

  // Step 14. If remainingElementsCount.[[Value]] is 0, then
  if (remainingCount == 0) {
    // Step 14.a. Let valuesArray be ! CreateArrayFromList(values).
    // (already performed)

    // (reordered)
    // Step 7. Let promiseCapability be F.[[Capability]].
    //
    // Step 14.b. Return
    //            ? Call(promiseCapability.[[Resolve]], undefined,
    //                   « valuesArray »).
    RootedObject resolveAllFun(cx, data->resolveOrRejectObj());
    RootedObject promiseObj(cx, data->promiseObj());
    if (!CallPromiseResolveFunction(cx, resolveAllFun, values.value(),
                                    promiseObj)) {
      return false;
    }
  }

  // Step 15. Return undefined.
  args.rval().setUndefined();
  return true;
}

[[nodiscard]] static bool PerformPromiseAny(
    JSContext* cx, PromiseForOfIterator& iterator, HandleObject C,
    Handle<PromiseCapability> resultCapability, HandleValue promiseResolve,
    bool* done);

/**
 * ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
 *
 * Promise.any ( iterable )
 * https://tc39.es/ecma262/#sec-promise.any
 */
static bool Promise_static_any(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  return CommonIterPromiseCombinator(cx, args, PerformPromiseAny,
                                     "Receiver of Promise.any call",
                                     "Argument of Promise.any");
}

static bool PromiseAnyRejectElementFunction(JSContext* cx, unsigned argc,
                                            Value* vp);

static void ThrowAggregateError(JSContext* cx,
                                Handle<PromiseCombinatorElements> errors,
                                HandleObject promise);

/**
 * ES2026 draft rev 00146687f225a64e1b1e2d303acc6139a1adee7d
 *
 * Promise.any ( iterable )
 * https://tc39.es/ecma262/#sec-promise.any
 * PerformPromiseAny ( iteratorRecord, constructor, resultCapability,
 *                     promiseResolve )
 * https://tc39.es/ecma262/#sec-performpromiseany
 */
[[nodiscard]] static bool PerformPromiseAny(
    JSContext* cx, PromiseForOfIterator& iterator, HandleObject C,
    Handle<PromiseCapability> resultCapability, HandleValue promiseResolve,
    bool* done) {
  MOZ_ASSERT(C->isConstructor());

  *done = false;

  // Step 1. Let errors be a new empty List.
  Rooted<PromiseCombinatorElements> errors(cx);
  if (!NewPromiseCombinatorElements(cx, resultCapability, &errors)) {
    return false;
  }

  // Step 3. Let remainingElementsCount be the Record { [[Value]]: 1 }.
  //
  // Create our data holder that holds all the things shared across every step
  // of the iterator. In particular, this holds the remainingElementsCount (as
  // an integer reserved slot), the array of errors, and the reject function
  // from our PromiseCapability.
  Rooted<PromiseCombinatorDataHolder*> dataHolder(cx);
  dataHolder = PromiseCombinatorDataHolder::New(
      cx, resultCapability.promise(), errors, resultCapability.reject());
  if (!dataHolder) {
    return false;
  }

  // Step 4. Let index be 0.
  uint32_t index = 0;

  auto getResolveAndReject = [cx, &resultCapability, &errors, &dataHolder,
                              &index](MutableHandleValue resolveFunVal,
                                      MutableHandleValue rejectFunVal) {
    // Step 5.c. Append undefined to errors.
    if (!errors.pushUndefined(cx)) {
      return false;
    }

    // Steps 5.e-h.
    JSFunction* rejectFunc = NewPromiseCombinatorElementFunction(
        cx, PromiseAnyRejectElementFunction, dataHolder, index,
        UndefinedHandleValue);
    if (!rejectFunc) {
      return false;
    }

    // Step 5.j. Set remainingElementsCount.[[Value]] to
    //           remainingElementsCount.[[Value]] + 1.
    dataHolder->increaseRemainingCount();

    // Step 5.i. Set index to index + 1.
    index++;
    MOZ_ASSERT(index > 0);

    resolveFunVal.setObject(*resultCapability.resolve());
    rejectFunVal.setObject(*rejectFunc);
    return true;
  };

  // BlockOnPromise fast path requires the passed onFulfilled function doesn't
  // return an object value, because otherwise the skipped promise creation is
  // detectable due to missing property lookups.
  bool isDefaultResolveFn =
      IsNativeFunction(resultCapability.resolve(), ResolvePromiseFunction);

  // Step 5. Repeat,
  if (!CommonPerformIterPromiseCombinator(
          cx, iterator, C, resultCapability.promise(), promiseResolve, done,
          isDefaultResolveFn, getResolveAndReject)) {
    return false;
  }

  // Step 5.b.i. Set remainingElementsCount.[[Value]] to
  //             remainingElementsCount.[[Value]] - 1..
  int32_t remainingCount = dataHolder->decreaseRemainingCount();

  // Step 5.b.ii. If remainingElementsCount.[[Value]] = 0, then
  if (remainingCount == 0) {
    ThrowAggregateError(cx, errors, resultCapability.promise());
    return false;
  }

  // Step 5.b.iii. Return resultCapability.[[Promise]].
  return true;
}

/**
 * ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
 *
 * Promise.any Reject Element Functions
 * https://tc39.es/ecma262/#sec-promise.any-reject-element-functions
 */
static bool PromiseAnyRejectElementFunction(JSContext* cx, unsigned argc,
                                            Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  HandleValue xVal = args.get(0);

  // Steps 1-4.
  Rooted<PromiseCombinatorDataHolder*> data(cx);
  uint32_t index;
  if (PromiseCombinatorElementFunctionAlreadyCalled<
          PromiseCombinatorDataHolder>(args, &data, &index)) {
    args.rval().setUndefined();
    return true;
  }

  // Step 5.
  Rooted<PromiseCombinatorElements> errors(cx);
  if (!GetPromiseCombinatorElements(cx, data, &errors)) {
    return false;
  }

  // Step 8.
  if (!errors.setElement(cx, index, xVal)) {
    return false;
  }

  // Steps 7 and 9.
  uint32_t remainingCount = data->decreaseRemainingCount();

  // Step 10.
  if (remainingCount == 0) {
    // Step 6 (Adapted to work with PromiseCombinatorDataHolder's layout).
    RootedObject rejectFun(cx, data->resolveOrRejectObj());
    RootedObject promiseObj(cx, data->promiseObj());

    // Steps 10.a-b.
    ThrowAggregateError(cx, errors, promiseObj);

    RootedValue reason(cx);
    Rooted<SavedFrame*> stack(cx);
    if (!MaybeGetAndClearExceptionAndStack(cx, &reason, &stack)) {
      return false;
    }

    // Step 10.c.
    if (!CallPromiseRejectFunction(cx, rejectFun, reason, promiseObj, stack,
                                   UnhandledRejectionBehavior::Report)) {
      return false;
    }
  }

  // Step 11.
  args.rval().setUndefined();
  return true;
}

/**
 * ES2026 draft rev bdfd596ffad5aeb2957aed4e1db36be3665c69ec
 *
 * PerformPromiseAny ( iteratorRecord, constructor, resultCapability,
 *                     promiseResolve )
 * https://tc39.es/ecma262/#sec-performpromiseany
 *
 * Steps 4.b.ii.1-3
 */
static void ThrowAggregateError(JSContext* cx,
                                Handle<PromiseCombinatorElements> errors,
                                HandleObject promise) {
  MOZ_ASSERT(!cx->isExceptionPending());

  // Create the AggregateError in the same realm as the array object.
  AutoRealm ar(cx, errors.unwrappedArray());

  RootedObject allocationSite(cx);
  mozilla::Maybe<JS::AutoSetAsyncStackForNewCalls> asyncStack;

  // Provide a more useful error stack if possible: This function is typically
  // called from Promise job queue, which doesn't have any JS frames on the
  // stack. So when we create the AggregateError below, its stack property will
  // be set to the empty string, which makes it harder to debug the error cause.
  // To avoid this situation set-up an async stack based on the Promise
  // allocation site, which should point to calling site of |Promise.any|.
  if (promise->is<PromiseObject>()) {
    allocationSite = promise->as<PromiseObject>().allocationSite();
    if (allocationSite) {
      asyncStack.emplace(
          cx, allocationSite, "Promise.any",
          JS::AutoSetAsyncStackForNewCalls::AsyncCallKind::IMPLICIT);
    }
  }

  // Step 4.b.ii.1. Let error be a newly created AggregateError object.
  //
  // AutoSetAsyncStackForNewCalls requires a new activation before it takes
  // effect, so call into the self-hosting helper to set-up new call frames.
  RootedValue error(cx);
  if (!GetAggregateError(cx, JSMSG_PROMISE_ANY_REJECTION, &error)) {
    return;
  }

  // Step 4.b.ii.2. Perform ! DefinePropertyOrThrow(
  //                  error, "errors", PropertyDescriptor {
  //                    [[Configurable]]: true, [[Enumerable]]: false,
  //                    [[Writable]]: true,
  //                    [[Value]]: ! CreateArrayFromList(errors) }).
  //
  // |error| isn't guaranteed to be an AggregateError in case of OOM or stack
  // overflow.
  Rooted<SavedFrame*> stack(cx);
  if (error.isObject() && error.toObject().is<ErrorObject>()) {
    Rooted<ErrorObject*> errorObj(cx, &error.toObject().as<ErrorObject>());
    if (errorObj->type() == JSEXN_AGGREGATEERR) {
      RootedValue errorsVal(cx, JS::ObjectValue(*errors.unwrappedArray()));
      if (!NativeDefineDataProperty(cx, errorObj, cx->names().errors, errorsVal,
                                    0)) {
        return;
      }

      // Adopt the existing saved frames when present.
      if (JSObject* errorStack = errorObj->stack()) {
        stack = &errorStack->as<SavedFrame>();
      }
    }
  }

  // Step 4.b.ii.3. Return ThrowCompletion(error).
  cx->setPendingException(error, stack);
}

/**
 * Await Dictionary Proposal
 *
 * CreateKeyedPromiseCombinatorResultObject ( keys, values )
 * https://tc39.es/proposal-await-dictionary/#sec-createkeyedpromisecombinatorresultobject
 *
 * Takes parallel lists of keys and values and creates an object with those
 * properties.
 */
[[nodiscard]] static JSObject* CreateKeyedPromiseCombinatorResultObject(
    JSContext* cx, JS::Handle<ListObject*> keys,
    JS::Handle<ListObject*> values) {
  // Step 1. Assert: The number of elements in keys is the same as the number
  //         of elements in values.
  MOZ_ASSERT(keys->length() == values->length());

  // Step 2. Let obj be OrdinaryObjectCreate(null).
  JS::Rooted<PlainObject*> obj(cx, NewPlainObjectWithProto(cx, nullptr));
  if (!obj) {
    return nullptr;
  }

  // Step 3. For each integer i such that 0 ≤ i < the number of elements in
  //         keys, in ascending order, do
  uint32_t len = keys->length();
  for (uint32_t i = 0; i < len; i++) {
    JS::Rooted<JS::Value> keyVal(cx, keys->get(i));

    JS::Rooted<JS::PropertyKey> id(cx);
    if (!ToPropertyKey(cx, keyVal, &id)) {
      return nullptr;
    }

    JS::Rooted<JS::Value> val(cx, values->get(i));

    // Step 3.a. Perform ! CreateDataPropertyOrThrow(obj, keys[i], values[i]).
    if (!NativeDefineDataProperty(cx, obj, id, val, JSPROP_ENUMERATE)) {
      return nullptr;
    }
  }

  // Step 4. Return obj.
  return obj;
}

/**
 * Await Dictionary Proposal
 *
 * PerformPromiseAllKeyed ( variant, promises, constructor, resultCapability,
 *                          promiseResolve )
 * https://tc39.es/proposal-await-dictionary/#sec-performpromiseallkeyed
 *
 * Common implementation for both Promise.allKeyed and Promise.allSettledKeyed.
 * The spec defines a single PerformPromiseAllKeyed operation that takes a
 * 'variant' parameter (either "all" or "all-settled"). This implementation
 * parameterizes the variant-specific behavior (creating element functions in
 * steps 6.b.v-ix) via the createElementFunctions callback.
 */
template <typename CreateElementFunctionsCallback>
[[nodiscard]] static bool CommonPerformPromiseKeyedCombinator(
    JSContext* cx, JS::Handle<JSObject*> promises, JS::Handle<JSObject*> C,
    JS::Handle<PromiseCapability> resultCapability,
    JS::Handle<JS::Value> promiseResolve,
    CreateElementFunctionsCallback createElementFunctions) {
  MOZ_ASSERT(C->isConstructor());

  // Step 1. Let allKeys be ? promises.[[OwnPropertyKeys]]().
  JS::RootedVector<JS::PropertyKey> allKeys(cx);
  if (!GetPropertyKeys(cx, promises,
                       JSITER_OWNONLY | JSITER_HIDDEN | JSITER_SYMBOLS,
                       &allKeys)) {
    return false;
  }

  // Step 2. Let keys be a new empty List.
  JS::Rooted<ListObject*> keys(cx, ListObject::create(cx));
  if (!keys) {
    return false;
  }

  // Step 3. Let values be a new empty List.
  JS::Rooted<ListObject*> values(cx, ListObject::create(cx));
  if (!values) {
    return false;
  }

  // Step 4. Let remainingElementsCount be the Record { [[Value]]: 1 }.
  JS::Rooted<PromiseCombinatorKeyedDataHolder*> dataHolder(
      cx, PromiseCombinatorKeyedDataHolder::New(cx, resultCapability.promise(),
                                                keys, values,
                                                resultCapability.resolve()));
  if (!dataHolder) {
    return false;
  }

  JS::Rooted<JS::PropertyKey> key(cx);
  JS::Rooted<mozilla::Maybe<JS::PropertyDescriptor>> desc(cx);
  JS::Rooted<JS::Value> keyVal(cx);

  // Step 5. Let index be 0.
  uint32_t index = 0;
  size_t keyIndex = 0;
  auto getNextFunc = [&](JS::MutableHandle<JS::Value> nextValue, bool* done) {
    // The outer loop in CommonPerformPromiseCombinator corresponds to Step 6.
    // This helper advances through allKeys and skips non-enumerable entries
    // to produce the next key/value pair for one Step 6 iteration.
    while (true) {
      if (keyIndex == allKeys.length()) {
        *done = true;
        return true;
      }
      key = allKeys[keyIndex++];

      // Step 6.a. Let desc be ? promises.[[GetOwnProperty]](key).
      if (!GetOwnPropertyDescriptor(cx, promises, key, &desc)) {
        return false;
      }

      // Step 6.b. If desc is not undefined and desc.[[Enumerable]] is true,
      //           then
      if (desc.isNothing() || !desc->enumerable()) {
        continue;
      }
      break;
    }

    // Step 6.b.i. Let value be ? Get(promises, key).
    if (!GetProperty(cx, promises, promises, key, nextValue)) {
      return false;
    }

    // Step 6.b.ii. Append key to keys.
    keyVal = IdToValue(key);
    if (!keys->append(cx, keyVal)) {
      return false;
    }

    // Step 6.b.iii. Append undefined to values.
    if (!values->append(cx, UndefinedHandleValue)) {
      return false;
    }
    *done = false;
    return true;
  };

  auto getResolveAndReject = [&](JS::MutableHandle<JS::Value> resolveFunVal,
                                 JS::MutableHandle<JS::Value> rejectFunVal) {
    // Steps 6.b.v-ix.
    // Create onFulfilled and onRejected closures.
    if (!createElementFunctions(dataHolder, index, resolveFunVal,
                                rejectFunVal)) {
      return false;
    }

    // Step 6.b.x. Set remainingElementsCount.[[Value]] to
    //             remainingElementsCount.[[Value]] + 1.
    dataHolder->increaseRemainingCount();

    // Step 6.b.xii. Set index to index + 1.
    index++;
    return true;
  };

  // Step 6.
  bool done = false;
  if (!CommonPerformPromiseCombinator(cx, C, resultCapability.promise(),
                                      promiseResolve, true, &done, true,
                                      getNextFunc, getResolveAndReject)) {
    return false;
  }

  // Step 7. Set remainingElementsCount.[[Value]] to
  //         remainingElementsCount.[[Value]] - 1.
  int32_t remainingCount = dataHolder->decreaseRemainingCount();

  // Step 8. If remainingElementsCount.[[Value]] is 0, then
  if (remainingCount == 0) {
    // Step 8.a. NOTE: This can happen even if keys was non-empty if an
    //           ill-behaved thenable synchronously invoked the callback passed
    //           to its "then" method.

    // Step 8.b. Let result be CreateKeyedPromiseCombinatorResultObject(keys,
    //                                                                  values).
    JS::Rooted<JSObject*> resultObj(
        cx, CreateKeyedPromiseCombinatorResultObject(cx, keys, values));
    if (!resultObj) {
      return false;
    }

    // Step 8.c. Perform ? Call(resultCapability.[[Resolve]], undefined, «
    //                          result »).
    JS::Rooted<JS::Value> resultVal(cx, ObjectValue(*resultObj));
    if (!CallPromiseResolveFunction(cx, resultCapability.resolve(), resultVal,
                                    resultCapability.promise())) {
      return false;
    }
  }

  // Step 9. Return resultCapability.[[Promise]].
  // NOTE: The promise is returned by the caller
  //       (PerformPromiseAllKeyed/PerformPromiseAllSettledKeyed).
  return true;
}

static bool PromiseAllKeyedResolveElementFunction(JSContext* cx, unsigned argc,
                                                  Value* vp);

/**
 * Await Dictionary Proposal
 *
 * PerformPromiseAllKeyed ( variant, promises, constructor, resultCapability,
 *                          promiseResolve )
 * https://tc39.es/proposal-await-dictionary/#sec-performpromiseallkeyed
 *
 * Implements PerformPromiseAllKeyed with variant="all".
 */
[[nodiscard]] static bool PerformPromiseAllKeyed(
    JSContext* cx, JS::Handle<JSObject*> promises, JS::Handle<JSObject*> C,
    JS::Handle<PromiseCapability> resultCapability,
    JS::Handle<JS::Value> promiseResolve) {
  auto createElementFunctions =
      [&](JS::Handle<PromiseCombinatorKeyedDataHolder*> dataHolder,
          uint32_t index, JS::MutableHandle<JS::Value> resolveFunVal,
          JS::MutableHandle<JS::Value> rejectFunVal) {
        // Step 6.b.vi. Let onFulfilled be a new Abstract Closure with
        //              parameters (x) that captures variant, alreadyCalled,
        //              index, keys, values, resultCapability, and
        //              remainingElementsCount and performs the following steps
        //              when called:
        // Step 6.b.v. Let alreadyCalled be the Record { [[Value]]: false }.
        JSFunction* resolveFunc = NewPromiseCombinatorElementFunction(
            cx, PromiseAllKeyedResolveElementFunction, dataHolder, index,
            UndefinedHandleValue);
        if (!resolveFunc) {
          return false;
        }

        resolveFunVal.setObject(*resolveFunc);

        // Step 6.b.viii. If variant is all, then
        // Step 6.b.viii.1. Let onRejected be resultCapability.[[Reject]].
        rejectFunVal.setObject(*resultCapability.reject());
        return true;
      };

  // Steps 1-9.
  return CommonPerformPromiseKeyedCombinator(cx, promises, C, resultCapability,
                                             promiseResolve,
                                             createElementFunctions);
}

static bool PromiseAllSettledKeyedResolveElementFunction(JSContext* cx,
                                                         unsigned argc,
                                                         Value* vp);
static bool PromiseAllSettledKeyedRejectElementFunction(JSContext* cx,
                                                        unsigned argc,
                                                        Value* vp);

/**
 * Await Dictionary Proposal
 *
 * PerformPromiseAllKeyed ( variant, promises, constructor, resultCapability,
 *                          promiseResolve )
 * https://tc39.es/proposal-await-dictionary/#sec-performpromiseallkeyed
 *
 * Implements PerformPromiseAllKeyed with variant="all-settled".
 */
[[nodiscard]] static bool PerformPromiseAllSettledKeyed(
    JSContext* cx, JS::Handle<JSObject*> promises, JS::Handle<JSObject*> C,
    JS::Handle<PromiseCapability> resultCapability,
    JS::Handle<JS::Value> promiseResolve) {
  auto createElementFunctions =
      [&](JS::Handle<PromiseCombinatorKeyedDataHolder*> dataHolder,
          uint32_t index, JS::MutableHandle<JS::Value> resolveFunVal,
          JS::MutableHandle<JS::Value> rejectFunVal) {
        // Step 6.b.vi. Let onFulfilled be a new Abstract Closure with
        //              parameters (x) that captures variant, alreadyCalled,
        //              index, keys, values, resultCapability, and
        //              remainingElementsCount and performs the following steps
        //              when called:
        // Step 6.b.v. Let alreadyCalled be the Record { [[Value]]: false }.
        JSFunction* resolveFunc = NewPromiseCombinatorElementFunction(
            cx, PromiseAllSettledKeyedResolveElementFunction, dataHolder, index,
            UndefinedHandleValue);
        if (!resolveFunc) {
          return false;
        }

        resolveFunVal.setObject(*resolveFunc);

        // Step 6.b.ix.2. Let onRejected be a new Abstract Closure with
        //                parameters (x) that captures alreadyCalled, index,
        //                keys, values, resultCapability, and
        //                remainingElementsCount and performs the following
        //                steps when called:
        JSFunction* rejectFunc = NewPromiseCombinatorElementFunction(
            cx, PromiseAllSettledKeyedRejectElementFunction, dataHolder, index,
            resolveFunVal);
        if (!rejectFunc) {
          return false;
        }

        rejectFunVal.setObject(*rejectFunc);
        return true;
      };

  // Steps 1-9.
  return CommonPerformPromiseKeyedCombinator(cx, promises, C, resultCapability,
                                             promiseResolve,
                                             createElementFunctions);
}

/**
 * Await Dictionary Proposal
 *
 * Unified implementation of:
 *
 * PerformPromiseAllKeyed ( promises, constructor, resultCapability,
 *                          promiseResolve )
 * https://tc39.es/proposal-await-dictionary/#sec-performpromiseallkeyed
 *
 * Step 6.b.vi. - Promise.allKeyed Resolve Element Functions, and
 *                Promise.allSettledKeyed Resolve Element Functions
 * Step 6.b.ix.2. - Promise.allSettledKeyed Reject Element Functions
 *
 * Template function that handles common logic for all keyed combinator
 * element functions. The ProcessValueFn lambda handles the variant-specific
 * logic for processing the input value before storing it in the values list.
 */
template <typename ProcessValueFn>
static bool PromiseKeyedElementFunction(JSContext* cx, unsigned argc, Value* vp,
                                        ProcessValueFn&& processValue) {
  CallArgs args = CallArgsFromVp(argc, vp);
  JS::Handle<JS::Value> xVal = args.get(0);

  // Step 6.b.vi.1-2.
  // Step 6.b.ix.2.a-b.
  JS::Rooted<PromiseCombinatorKeyedDataHolder*> data(cx);
  uint32_t index;
  if (PromiseCombinatorElementFunctionAlreadyCalled<
          PromiseCombinatorKeyedDataHolder>(args, &data, &index)) {
    args.rval().setUndefined();
    return true;
  }

  // Variant-specific processing: process the value before storing.
  // For allKeyed: just use the value directly
  // For allSettledKeyed resolve: create {status: "fulfilled", value: x}
  // For allSettledKeyed reject: create {status: "rejected", reason: x}
  JS::Rooted<JS::Value> processedValue(cx);
  if (!processValue(cx, xVal, index, &processedValue)) {
    return false;
  }

  // Step 6.b.vi.3.a / Step 6.b.vi.4.e / Step 6.b.ix.2.f.
  // Set values[index] to the processed value.
  JS::Rooted<ListObject*> values(cx, data->valuesList());
  values->setDenseElement(index, processedValue);

  // Step 6.b.vi.5 / Step 6.b.ix.2.g. Set remainingElementsCount.[[Value]] to
  //                                      remainingElementsCount.[[Value]] - 1.
  uint32_t remainingCount = data->decreaseRemainingCount();

  // Step 6.b.vi.6 / Step 6.b.ix.2.h. If remainingElementsCount.[[Value]] = 0,
  //                                     then
  if (remainingCount == 0) {
    JS::Rooted<ListObject*> keys(cx, data->keysList());
    JS::Rooted<JSObject*> resolveAllFun(cx, data->resolveOrRejectObj());
    JS::Rooted<JSObject*> promiseObj(cx, data->promiseObj());

    // Step 6.b.vi.6.a / Step 6.b.ix.2.h.i. Let result be
    // CreateKeyedPromiseCombinatorResultObject(keys, values).
    JS::Rooted<JSObject*> resultObj(
        cx, CreateKeyedPromiseCombinatorResultObject(cx, keys, values));
    if (!resultObj) {
      return false;
    }

    // Step 6.b.vi.6.b / Step 6.b.ix.2.h.ii. Return ?
    // Call(resultCapability.[[Resolve]], undefined, « result »).
    JS::Rooted<JS::Value> resultVal(cx, ObjectValue(*resultObj));
    if (!CallPromiseResolveFunction(cx, resolveAllFun, resultVal, promiseObj)) {
      return false;
    }
  }

  // Step 6.b.vi.7 / Step 6.b.ix.2.i. Return undefined.
  args.rval().setUndefined();
  return true;
}

/**
 * Await Dictionary Proposal
 *
 * PerformPromiseAllKeyed ( promises, constructor, resultCapability,
 *                          promiseResolve )
 * https://tc39.es/proposal-await-dictionary/#sec-performpromiseallkeyed
 *
 * Step 6.b.vi.
 * onFulfilled callback for variant="all".
 *
 * Promise.allKeyed Resolve Element Functions.
 */
static bool PromiseAllKeyedResolveElementFunction(JSContext* cx, unsigned argc,
                                                  Value* vp) {
  // For allKeyed, just use the value directly
  auto processAllKeyedValue = [](JSContext* cx, JS::Handle<JS::Value> xVal,
                                 uint32_t index,
                                 JS::MutableHandle<JS::Value> outVal) {
    // Step 6.b.vi.3. If variant is all, then
    // Step 6.b.vi.3.a. Set values[index] to x.
    outVal.set(xVal);
    return true;
  };

  return PromiseKeyedElementFunction(cx, argc, vp, processAllKeyedValue);
}

/**
 * Await Dictionary Proposal
 *
 * PerformPromiseAllKeyed ( promises, constructor, resultCapability,
 *                          promiseResolve )
 * https://tc39.es/proposal-await-dictionary/#sec-performpromiseallkeyed
 *
 * Step 6.b.vi.
 * onFulfilled callback for variant="all-settled".
 *
 * Promise.allSettledKeyed Resolve Element Functions.
 */
static bool PromiseAllSettledKeyedResolveElementFunction(JSContext* cx,
                                                         unsigned argc,
                                                         Value* vp) {
  // For allSettledKeyed resolve, create {status: "fulfilled", value: x}
  auto processAllSettledResolveValue =
      [](JSContext* cx, JS::Handle<JS::Value> xVal, uint32_t index,
         JS::MutableHandle<JS::Value> outVal) {
        // Step 6.b.vi.4. Else,
        // Step 6.b.vi.4.a. Assert: variant is all-settled.
        // Step 6.b.vi.4.b. Let obj be OrdinaryObjectCreate(%Object.prototype%).
        JS::Rooted<JSObject*> obj(cx, NewPlainObject(cx));
        if (!obj) {
          return false;
        }

        // Step 6.b.vi.4.c. Perform ! CreateDataPropertyOrThrow(obj, "status",
        //                                                      "fulfilled").
        JS::Rooted<JS::Value> statusVal(cx, StringValue(cx->names().fulfilled));
        if (!DefineDataProperty(cx, obj, cx->names().status, statusVal)) {
          return false;
        }

        // Step 6.b.vi.4.d. Perform ! CreateDataPropertyOrThrow(obj, "value",
        //                                                      x).
        if (!DefineDataProperty(cx, obj, cx->names().value, xVal)) {
          return false;
        }

        // Step 6.b.vi.4.e. Set values[index] to obj.
        outVal.setObject(*obj);
        return true;
      };

  return PromiseKeyedElementFunction(cx, argc, vp,
                                     processAllSettledResolveValue);
}

/**
 * Await Dictionary Proposal
 *
 * PerformPromiseAllKeyed ( promises, constructor, resultCapability,
 *                          promiseResolve )
 * https://tc39.es/proposal-await-dictionary/#sec-performpromiseallkeyed
 *
 * Step 6.b.ix.2.
 * onRejected callback for variant="all-settled".
 *
 * Promise.allSettledKeyed Reject Element Functions.
 */
static bool PromiseAllSettledKeyedRejectElementFunction(JSContext* cx,
                                                        unsigned argc,
                                                        Value* vp) {
  // For allSettledKeyed reject, create {status: "rejected", reason: x}
  auto processAllSettledRejectValue =
      [](JSContext* cx, JS::Handle<JS::Value> xVal, uint32_t index,
         JS::MutableHandle<JS::Value> outVal) {
        // Step 6.b.ix.2.c. Let obj be OrdinaryObjectCreate(%Object.prototype%).
        JS::Rooted<JSObject*> obj(cx, NewPlainObject(cx));
        if (!obj) {
          return false;
        }

        // Step 6.b.ix.2.d. Perform ! CreateDataPropertyOrThrow(obj, "status",
        //                                                      "rejected").
        JS::Rooted<JS::Value> statusVal(cx, StringValue(cx->names().rejected));
        if (!DefineDataProperty(cx, obj, cx->names().status, statusVal)) {
          return false;
        }

        // Step 6.b.ix.2.e. Perform ! CreateDataPropertyOrThrow(obj, "reason",
        //                                                      x).
        if (!DefineDataProperty(cx, obj, cx->names().reason, xVal)) {
          return false;
        }

        // Step 6.b.ix.2.f. Set values[index] to obj.
        outVal.setObject(*obj);
        return true;
      };

  return PromiseKeyedElementFunction(cx, argc, vp,
                                     processAllSettledRejectValue);
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Unified implementation of

 * NewPromiseCapability ( C )
 * https://tc39.es/ecma262/#sec-newpromisecapability
 * Promise.resolve ( x )
 * https://tc39.es/ecma262/#sec-promise.resolve
 * PromiseResolve ( C, x )
 * https://tc39.es/ecma262/#sec-promise-resolve
 */
[[nodiscard]] static JSObject* CommonStaticResolveImpl(JSContext* cx,
                                                       HandleObject C,
                                                       HandleValue argVal) {
  // Promise.resolve
  // Step 3. Return ? PromiseResolve(C, x).
  //
  // PromiseResolve
  // Step 1. Assert: Type(C) is Object.
  // (implicit)
  if (argVal.isObject()) {
    RootedObject xObj(cx, &argVal.toObject());
    bool isPromise = false;
    if (xObj->is<PromiseObject>()) {
      isPromise = true;
    } else if (IsWrapper(xObj)) {
      // Treat instances of Promise from other compartments as Promises
      // here, too.
      // It's important to do the GetProperty for the `constructor`
      // below through the wrapper, because wrappers can change the
      // outcome, so instead of unwrapping and then performing the
      // GetProperty, just check here and then operate on the original
      // object again.
      if (xObj->canUnwrapAs<PromiseObject>()) {
        isPromise = true;
      }
    }

    // PromiseResolve
    // Step 2. If IsPromise(x) is true, then
    if (isPromise) {
      // Step 2.a. Let xConstructor be ? Get(x, "constructor").
      RootedValue ctorVal(cx);
      if (!GetProperty(cx, xObj, xObj, cx->names().constructor, &ctorVal)) {
        return nullptr;
      }

      // Step 2.b. If SameValue(xConstructor, C) is true, return x.
      if (ctorVal == ObjectValue(*C)) {
        return xObj;
      }
    }
  }

  // PromiseResolve
  // Step 3. Let promiseCapability be ? NewPromiseCapability(C).
  Rooted<PromiseCapability> capability(cx);
  if (!NewPromiseCapability(cx, C, &capability, true)) {
    return nullptr;
  }

  HandleObject promise = capability.promise();

  // PromiseResolve
  // Step 4. Perform ? Call(promiseCapability.[[Resolve]], undefined, « x »).
  if (!CallPromiseResolveFunction(cx, capability.resolve(), argVal, promise)) {
    return nullptr;
  }

  // PromiseResolve
  // Step 5. Return promiseCapability.[[Promise]].
  return promise;
}

[[nodiscard]] JSObject* js::PromiseResolve(JSContext* cx,
                                           HandleObject constructor,
                                           HandleValue value) {
  return CommonStaticResolveImpl(cx, constructor, value);
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Promise.reject ( r )
 * https://tc39.es/ecma262/#sec-promise.reject
 */
static bool Promise_reject(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  HandleValue thisVal = args.thisv();
  HandleValue argVal = args.get(0);
  // Promise.reject
  // Step 1. Let C be the this value.
  // Step 2. Let promiseCapability be ? NewPromiseCapability(C).
  //
  // Promise.reject => NewPromiseCapability
  // Step 1. If IsConstructor(C) is false, throw a TypeError exception.
  if (!thisVal.isObject()) {
    const char* msg = "Receiver of Promise.reject call";
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_OBJECT_REQUIRED, msg);
    return false;
  }
  RootedObject C(cx, &thisVal.toObject());

  // Promise.reject
  // Step 2. Let promiseCapability be ? NewPromiseCapability(C).
  Rooted<PromiseCapability> capability(cx);
  if (!NewPromiseCapability(cx, C, &capability, true)) {
    return false;
  }

  HandleObject promise = capability.promise();

  // Promise.reject
  // Step 3. Perform ? Call(promiseCapability.[[Reject]], undefined, « r »).
  if (!CallPromiseRejectFunction(cx, capability.reject(), argVal, promise,
                                 nullptr, UnhandledRejectionBehavior::Report)) {
    return false;
  }

  // Promise.reject
  // Step 4. Return promiseCapability.[[Promise]].
  args.rval().setObject(*promise);
  return true;
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Promise.reject ( r )
 * https://tc39.es/ecma262/#sec-promise.reject
 *
 * Unforgeable version.
 */
/* static */
PromiseObject* PromiseObject::unforgeableReject(JSContext* cx,
                                                HandleValue value) {
  cx->check(value);

  // Step 1. Let C be the this value.
  // Step 2. Let promiseCapability be ? NewPromiseCapability(C).
  Rooted<PromiseObject*> promise(
      cx, CreatePromiseObjectWithoutResolutionFunctions(cx));
  if (!promise) {
    return nullptr;
  }

  MOZ_ASSERT(promise->state() == JS::PromiseState::Pending);
  MOZ_ASSERT(IsPromiseWithDefaultResolvingFunction(promise));

  // Step 3. Perform ? Call(promiseCapability.[[Reject]], undefined, « r »).
  if (!RejectPromiseInternal(cx, promise, value)) {
    return nullptr;
  }

  // Step 4. Return promiseCapability.[[Promise]].
  return promise;
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Promise.resolve ( x )
 * https://tc39.es/ecma262/#sec-promise.resolve
 */
bool js::Promise_static_resolve(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  // Promise.resolve
  // Step 1. Let C be the this value.
  HandleValue Cval = args.thisv();
  HandleValue argVal = args.get(0);

  // Step 2. If Type(C) is not Object, throw a TypeError exception
  if (!Cval.isObject()) {
    const char* msg = "Receiver of Promise.resolve call";
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_OBJECT_REQUIRED, msg);
    return false;
  }

  RootedObject C(cx, &Cval.toObject());
  JSObject* result = CommonStaticResolveImpl(cx, C, argVal);
  if (!result) {
    return false;
  }
  args.rval().setObject(*result);
  return true;
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Promise.resolve ( x )
 * https://tc39.es/ecma262/#sec-promise.resolve
 *
 * Unforgeable version.
 */
/* static */
JSObject* PromiseObject::unforgeableResolve(JSContext* cx, HandleValue value) {
  RootedObject promiseCtor(cx, JS::GetPromiseConstructor(cx));
  if (!promiseCtor) {
    return nullptr;
  }

  return CommonStaticResolveImpl(cx, promiseCtor, value);
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Promise.resolve ( x )
 * https://tc39.es/ecma262/#sec-promise.resolve
 * PromiseResolve ( C, x )
 * https://tc39.es/ecma262/#sec-promise-resolve
 *
 * Unforgeable version, where `x` is guaranteed not to be a promise.
 */
/* static */
PromiseObject* PromiseObject::unforgeableResolveWithNonPromise(
    JSContext* cx, HandleValue value) {
  cx->check(value);

#ifdef DEBUG
  auto IsPromise = [](HandleValue value) {
    if (!value.isObject()) {
      return false;
    }

    JSObject* obj = &value.toObject();
    if (obj->is<PromiseObject>()) {
      return true;
    }

    if (!IsWrapper(obj)) {
      return false;
    }

    return obj->canUnwrapAs<PromiseObject>();
  };
  MOZ_ASSERT(!IsPromise(value), "must use unforgeableResolve with this value");
#endif

  // Promise.resolve
  // Step 3. Return ? PromiseResolve(C, x).

  // PromiseResolve
  // Step 2. Let promiseCapability be ? NewPromiseCapability(C).
  Rooted<PromiseObject*> promise(
      cx, CreatePromiseObjectWithoutResolutionFunctions(cx));
  if (!promise) {
    return nullptr;
  }

  MOZ_ASSERT(promise->state() == JS::PromiseState::Pending);
  MOZ_ASSERT(IsPromiseWithDefaultResolvingFunction(promise));

  // PromiseResolve
  // Step 3. Perform ? Call(promiseCapability.[[Resolve]], undefined, « x »).
  if (!ResolvePromiseInternal(cx, promise, value)) {
    return nullptr;
  }

  // PromiseResolve
  // Step 4. Return promiseCapability.[[Promise]].
  return promise;
}

/**
 * https://tc39.es/proposal-promise-try/
 *
 * Promise.try ( )
 */
static bool Promise_static_try(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // 1. Let C be the this value.
  HandleValue cVal = args.thisv();

  // 2. If C is not an Object, throw a TypeError exception.
  if (!cVal.isObject()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_OBJECT_REQUIRED,
                              "Receiver of Promise.try call");
    return false;
  }

  // 3. Let promiseCapability be ? NewPromiseCapability(C).
  RootedObject c(cx, &cVal.toObject());
  Rooted<PromiseCapability> promiseCapability(cx);
  if (!NewPromiseCapability(cx, c, &promiseCapability, false)) {
    return false;
  }
  HandleObject promiseObject = promiseCapability.promise();

  // 4. Let status be Completion(Call(callbackfn, undefined, args)).
  size_t argCount = args.length();
  if (argCount > 0) {
    argCount--;
  }

  InvokeArgs iargs(cx);
  if (!iargs.init(cx, argCount)) {
    return false;
  }

  for (size_t i = 0; i < argCount; i++) {
    iargs[i].set(args[i + 1]);
  }

  HandleValue callbackfn = args.get(0);
  RootedValue rval(cx);
  bool ok = Call(cx, callbackfn, UndefinedHandleValue, iargs, &rval);

  // 5. If status is an abrupt completion, then
  if (!ok) {
    RootedValue reason(cx);
    Rooted<SavedFrame*> stack(cx);

    if (!MaybeGetAndClearExceptionAndStack(cx, &reason, &stack)) {
      return false;
    }

    // 5.a. Perform ? Call(promiseCapability.[[Reject]], undefined, «
    // status.[[Value]] »).
    if (!CallPromiseRejectFunction(cx, promiseCapability.reject(), reason,
                                   promiseObject, stack,
                                   UnhandledRejectionBehavior::Report)) {
      return false;
    }
  } else {
    // 6. Else,
    // 6.a. Perform ? Call(promiseCapability.[[Resolve]], undefined, «
    // status.[[Value]] »).
    if (!CallPromiseResolveFunction(cx, promiseCapability.resolve(), rval,
                                    promiseObject)) {
      return false;
    }
  }

  // 7. Return promiseCapability.[[Promise]].
  args.rval().setObject(*promiseObject);
  return true;
}

/**
 * https://tc39.es/proposal-promise-with-resolvers/
 *
 * Promise.withResolvers ( )
 */
static bool Promise_static_withResolvers(JSContext* cx, unsigned argc,
                                         Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Step 1. Let C be the this value.
  HandleValue cVal = args.thisv();

  // Step 2. Let promiseCapability be ? NewPromiseCapability(C).
  if (!cVal.isObject()) {
    ReportValueError(cx, JSMSG_NOT_CONSTRUCTOR, JSDVG_SEARCH_STACK, cVal,
                     nullptr);
    return false;
  }
  RootedObject c(cx, &cVal.toObject());
  Rooted<PromiseCapability> promiseCapability(cx);
  if (!NewPromiseCapability(cx, c, &promiseCapability, false)) {
    return false;
  }

  // Step 3. Let obj be OrdinaryObjectCreate(%Object.prototype%).
  Rooted<PlainObject*> obj(cx, NewPlainObject(cx));
  if (!obj) {
    return false;
  }

  // Step 4. Perform ! CreateDataPropertyOrThrow(obj, "promise",
  // promiseCapability.[[Promise]]).
  RootedValue v(cx, ObjectValue(*promiseCapability.promise()));
  if (!NativeDefineDataProperty(cx, obj, cx->names().promise, v,
                                JSPROP_ENUMERATE)) {
    return false;
  }

  // Step 5. Perform ! CreateDataPropertyOrThrow(obj, "resolve",
  // promiseCapability.[[Resolve]]).
  v.setObject(*promiseCapability.resolve());
  if (!NativeDefineDataProperty(cx, obj, cx->names().resolve, v,
                                JSPROP_ENUMERATE)) {
    return false;
  }

  // Step 6. Perform ! CreateDataPropertyOrThrow(obj, "reject",
  // promiseCapability.[[Reject]]).
  v.setObject(*promiseCapability.reject());
  if (!NativeDefineDataProperty(cx, obj, cx->names().reject, v,
                                JSPROP_ENUMERATE)) {
    return false;
  }

  // Step 7. Return obj.
  args.rval().setObject(*obj);
  return true;
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * get Promise [ @@species ]
 * https://tc39.es/ecma262/#sec-get-promise-@@species
 */
bool js::Promise_static_species(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Step 1. Return the this value.
  args.rval().set(args.thisv());
  return true;
}

enum class HostDefinedDataObjectOption {
  // Allocate the host defined data object, this is the normal operation.
  Allocate,

  // Do not allocate the host defined data object because the embeddings can
  // retrieve the same data on its own.
  OptimizeOut,

  // Did not allocate the host defined data object because this is a special
  // case used by the debugger.
  UnusedForDebugger,
};

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * PerformPromiseThen ( promise, onFulfilled, onRejected
 *                      [ , resultCapability ] )
 * https://tc39.es/ecma262/#sec-performpromisethen
 *
 * Steps 7-8 for creating PromiseReaction record.
 * We use single object for both fulfillReaction and rejectReaction.
 */
static PromiseReactionRecord* NewReactionRecord(
    JSContext* cx, Handle<PromiseCapability> resultCapability,
    HandleValue onFulfilled, HandleValue onRejected,
    HostDefinedDataObjectOption hostDefinedDataObjectOption) {
#ifdef DEBUG
  if (resultCapability.promise()) {
    if (hostDefinedDataObjectOption == HostDefinedDataObjectOption::Allocate) {
      if (resultCapability.promise()->is<PromiseObject>()) {
        // If `resultCapability.promise` is a Promise object,
        // `resultCapability.{resolve,reject}` may be optimized out,
        // but if they're not, they should be callable.
        MOZ_ASSERT_IF(resultCapability.resolve(),
                      IsCallable(resultCapability.resolve()));
        MOZ_ASSERT_IF(resultCapability.reject(),
                      IsCallable(resultCapability.reject()));
      } else {
        // If `resultCapability.promise` is a non-Promise object
        // (including wrapped Promise object),
        // `resultCapability.{resolve,reject}` should be callable.
        MOZ_ASSERT(resultCapability.resolve());
        MOZ_ASSERT(IsCallable(resultCapability.resolve()));
        MOZ_ASSERT(resultCapability.reject());
        MOZ_ASSERT(IsCallable(resultCapability.reject()));
      }
    } else if (hostDefinedDataObjectOption ==
               HostDefinedDataObjectOption::UnusedForDebugger) {
      // For debugger usage, `resultCapability.promise` should be a
      // maybe-wrapped Promise object. The other fields are not used.
      //
      // This is the only case where we allow `resolve` and `reject` to
      // be null when the `promise` field is not a PromiseObject.
      JSObject* unwrappedPromise = UncheckedUnwrap(resultCapability.promise());
      MOZ_ASSERT(unwrappedPromise->is<PromiseObject>() ||
                 JS_IsDeadWrapper(unwrappedPromise));
      MOZ_ASSERT(!resultCapability.resolve());
      MOZ_ASSERT(!resultCapability.reject());
    }
  } else {
    // `resultCapability.promise` is null for the following cases:
    //   * resulting Promise is known to be unused
    //   * Async Function
    //   * Async Generator
    // In any case, other fields are also not used.
    MOZ_ASSERT(!resultCapability.resolve());
    MOZ_ASSERT(!resultCapability.reject());
    MOZ_ASSERT(hostDefinedDataObjectOption !=
               HostDefinedDataObjectOption::UnusedForDebugger);
  }
#endif

  // Ensure the onFulfilled handler has the expected type.
  MOZ_ASSERT(onFulfilled.isInt32() || onFulfilled.isObjectOrNull());
  MOZ_ASSERT_IF(onFulfilled.isObject(), IsCallable(onFulfilled));
  MOZ_ASSERT_IF(onFulfilled.isInt32(),
                0 <= onFulfilled.toInt32() &&
                    onFulfilled.toInt32() < int32_t(PromiseHandler::Limit));

  // Ensure the onRejected handler has the expected type.
  MOZ_ASSERT(onRejected.isInt32() || onRejected.isObjectOrNull());
  MOZ_ASSERT_IF(onRejected.isObject(), IsCallable(onRejected));
  MOZ_ASSERT_IF(onRejected.isInt32(),
                0 <= onRejected.toInt32() &&
                    onRejected.toInt32() < int32_t(PromiseHandler::Limit));

  // Handlers must either both be present or both be absent.
  MOZ_ASSERT(onFulfilled.isNull() == onRejected.isNull());

  RootedObject incumbentGlobalRepresentative(cx, nullptr);
  RootedObject optionalHostDefinedData(cx);

  // An incumbent global must always be requested, however some host
  // defined data can be elided in the !Allocate case.
  //
  // Currently the APIs we have are basically "GetBoth" or "GetIncumbent",
  // hence the else branch here. We can potentially clean this up
  // in the future.
  if (hostDefinedDataObjectOption == HostDefinedDataObjectOption::Allocate) {
    // Get incumbent global and optional host defined data
    if (!GetObjectFromHostDefinedData(cx, &incumbentGlobalRepresentative,
                                      &optionalHostDefinedData)) {
      return nullptr;
    }
  } else {
    // Only get incumbent global representative.
    if (!GetIncumbentGlobalRepresentative(cx, &incumbentGlobalRepresentative)) {
      return nullptr;
    }
  }

  PromiseReactionRecord* reaction =
      NewBuiltinClassInstance<PromiseReactionRecord>(cx);
  if (!reaction) {
    return nullptr;
  }
  cx->check(resultCapability.promise(), onFulfilled, onRejected,
            resultCapability.resolve(), resultCapability.reject(),
            incumbentGlobalRepresentative, optionalHostDefinedData);

  // Step 7. Let fulfillReaction be the PromiseReaction
  //         { [[Capability]]: resultCapability, [[Type]]: Fulfill,
  //           [[Handler]]: onFulfilledJobCallback }.
  // Step 8. Let rejectReaction be the PromiseReaction
  //         { [[Capability]]: resultCapability, [[Type]]: Reject,
  //           [[Handler]]: onRejectedJobCallback }.

  // See comments for ReactionRecordSlots for the relation between
  // spec record fields and PromiseReactionRecord slots.
  reaction->initFixedSlot(PromiseReactionRecord::Promise,
                          ObjectOrNullValue(resultCapability.promise()));
  // We set [[Type]] in EnqueuePromiseReactionJob, by calling
  // setTargetStateAndHandlerArg.
  reaction->initFixedSlot(PromiseReactionRecord::Flags, Int32Value(0));
  reaction->initFixedSlot(PromiseReactionRecord::OnFulfilled, onFulfilled);
  reaction->initFixedSlot(PromiseReactionRecord::OnRejected, onRejected);
  reaction->initFixedSlot(PromiseReactionRecord::Resolve,
                          ObjectOrNullValue(resultCapability.resolve()));
  reaction->initFixedSlot(PromiseReactionRecord::Reject,
                          ObjectOrNullValue(resultCapability.reject()));
  reaction->initFixedSlot(PromiseReactionRecord::IncumbentGlobalRepresentative,
                          ObjectOrNullValue(incumbentGlobalRepresentative));
  reaction->initFixedSlot(PromiseReactionRecord::OptionalHostDefinedData,
                          ObjectOrNullValue(optionalHostDefinedData));

  return reaction;
}

static bool IsPromiseSpecies(JSContext* cx, JSFunction* species) {
  return species->maybeNative() == Promise_static_species;
}

// Whether to create a promise as the return value of Promise#{then,catch}.
// If the return value is known to be unused, and if the operation is known
// to be unobservable, we can skip creating the promise.
enum class CreateDependentPromise { Always, SkipIfCtorUnobservable };

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Promise.prototype.then ( onFulfilled, onRejected )
 * https://tc39.es/ecma262/#sec-promise.prototype.then
 *
 * Steps 3-4.
 */
static bool PromiseThenNewPromiseCapability(
    JSContext* cx, HandleObject promiseObj,
    CreateDependentPromise createDependent,
    MutableHandle<PromiseCapability> resultCapability) {
  // Step 3. Let C be ? SpeciesConstructor(promise, %Promise%).
  RootedObject C(cx, SpeciesConstructor(cx, promiseObj, JSProto_Promise,
                                        IsPromiseSpecies));
  if (!C) {
    return false;
  }

  if (createDependent != CreateDependentPromise::Always &&
      IsNativeFunction(C, PromiseConstructor)) {
    return true;
  }

  // Step 4. Let resultCapability be ? NewPromiseCapability(C).
  if (!NewPromiseCapability(cx, C, resultCapability, true)) {
    return false;
  }

  JSObject* unwrappedPromise = promiseObj;
  if (IsWrapper(promiseObj)) {
    unwrappedPromise = UncheckedUnwrap(promiseObj);
  }
  JSObject* unwrappedNewPromise = resultCapability.promise();
  if (IsWrapper(resultCapability.promise())) {
    unwrappedNewPromise = UncheckedUnwrap(resultCapability.promise());
  }
  if (unwrappedPromise->is<PromiseObject>() &&
      unwrappedNewPromise->is<PromiseObject>()) {
    unwrappedNewPromise->as<PromiseObject>().copyUserInteractionFlagsFrom(
        unwrappedPromise->as<PromiseObject>());
  }

  return true;
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Promise.prototype.then ( onFulfilled, onRejected )
 * https://tc39.es/ecma262/#sec-promise.prototype.then
 *
 * Steps 3-5.
 */
[[nodiscard]] PromiseObject* js::OriginalPromiseThen(JSContext* cx,
                                                     HandleObject promiseObj,
                                                     HandleObject onFulfilled,
                                                     HandleObject onRejected) {
  cx->check(promiseObj, onFulfilled, onRejected);

  RootedTuple<Value, PromiseObject*, PromiseObject*, PromiseCapability, Value,
              Value>
      roots(cx);
  RootedField<Value, 0> promiseVal(roots, ObjectValue(*promiseObj));
  RootedField<PromiseObject*, 1> unwrappedPromise(
      roots,
      UnwrapAndTypeCheckValue<PromiseObject>(cx, promiseVal, [cx, promiseObj] {
        JS_ReportErrorNumberLatin1(cx, GetErrorMessage, nullptr,
                                   JSMSG_INCOMPATIBLE_PROTO, "Promise", "then",
                                   promiseObj->getClass()->name);
      }));
  if (!unwrappedPromise) {
    return nullptr;
  }

  // Step 3. Let C be ? SpeciesConstructor(promise, %Promise%).
  // Step 4. Let resultCapability be ? NewPromiseCapability(C).
  RootedField<PromiseObject*, 2> newPromise(
      roots, CreatePromiseObjectWithoutResolutionFunctions(cx));
  if (!newPromise) {
    return nullptr;
  }
  newPromise->copyUserInteractionFlagsFrom(*unwrappedPromise);

  RootedField<PromiseCapability, 3> resultCapability(roots);
  resultCapability.promise().set(newPromise);

  // Step 5. Return PerformPromiseThen(promise, onFulfilled, onRejected,
  //                                   resultCapability).
  {
    RootedField<Value, 4> onFulfilledVal(roots, ObjectOrNullValue(onFulfilled));
    RootedField<Value, 5> onRejectedVal(roots, ObjectOrNullValue(onRejected));
    if (!PerformPromiseThen(cx, unwrappedPromise, onFulfilledVal, onRejectedVal,
                            resultCapability)) {
      return nullptr;
    }
  }

  return newPromise;
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Promise.prototype.then ( onFulfilled, onRejected )
 * https://tc39.es/ecma262/#sec-promise.prototype.then
 *
 * Steps 3-5.
 */
[[nodiscard]] static bool OriginalPromiseThenWithoutSettleHandlers(
    JSContext* cx, Handle<PromiseObject*> promise,
    Handle<PromiseObject*> promiseToResolve) {
  cx->check(promise);

  // Step 3. Let C be ? SpeciesConstructor(promise, %Promise%).
  // Step 4. Let resultCapability be ? NewPromiseCapability(C).
  Rooted<PromiseCapability> resultCapability(cx);
  if (!PromiseThenNewPromiseCapability(
          cx, promise, CreateDependentPromise::SkipIfCtorUnobservable,
          &resultCapability)) {
    return false;
  }

  // Step 5. Return PerformPromiseThen(promise, onFulfilled, onRejected,
  //                                   resultCapability).
  return PerformPromiseThenWithoutSettleHandlers(cx, promise, promiseToResolve,
                                                 resultCapability);
}

[[nodiscard]] static bool PerformPromiseThenWithReaction(
    JSContext* cx, Handle<PromiseObject*> promise,
    Handle<PromiseReactionRecord*> reaction);

[[nodiscard]] bool js::ReactToUnwrappedPromise(
    JSContext* cx, Handle<PromiseObject*> unwrappedPromise,
    HandleObject onFulfilled_, HandleObject onRejected_,
    UnhandledRejectionBehavior behavior) {
  cx->check(onFulfilled_, onRejected_);

  MOZ_ASSERT_IF(onFulfilled_, IsCallable(onFulfilled_));
  MOZ_ASSERT_IF(onRejected_, IsCallable(onRejected_));

  RootedTuple<Value, Value, PromiseCapability, PromiseReactionRecord*> roots(
      cx);
  RootedField<Value, 0> onFulfilled(
      roots, onFulfilled_ ? ObjectValue(*onFulfilled_)
                          : Int32Value(int32_t(PromiseHandler::Identity)));
  RootedField<Value, 1> onRejected(
      roots, onRejected_ ? ObjectValue(*onRejected_)
                         : Int32Value(int32_t(PromiseHandler::Thrower)));
  RootedField<PromiseCapability, 2> resultCapability(roots);
  MOZ_ASSERT(!resultCapability.promise());

  auto hostDefinedDataObjectOption =
      unwrappedPromise->state() == JS::PromiseState::Pending
          ? HostDefinedDataObjectOption::Allocate
          : HostDefinedDataObjectOption::OptimizeOut;

  RootedField<PromiseReactionRecord*, 3> reaction(
      roots, NewReactionRecord(cx, resultCapability, onFulfilled, onRejected,
                               hostDefinedDataObjectOption));
  if (!reaction) {
    return false;
  }

  if (behavior == UnhandledRejectionBehavior::Ignore) {
    reaction->setShouldIgnoreUnhandledRejection();
  }

  return PerformPromiseThenWithReaction(cx, unwrappedPromise, reaction);
}

static bool CanCallOriginalPromiseThenBuiltin(JSContext* cx,
                                              HandleValue promise) {
  return promise.isObject() && promise.toObject().is<PromiseObject>() &&
         IsPromiseWithDefaultProperties(&promise.toObject().as<PromiseObject>(),
                                        cx);
}

static MOZ_ALWAYS_INLINE bool IsPromiseThenOrCatchRetValImplicitlyUsed(
    JSContext* cx, PromiseObject* promise) {
  // Embedding requires the return value of then/catch as
  // `enqueuePromiseJob` parameter, to propaggate the user-interaction.
  // We cannot optimize out the return value if the flag is set by embedding.
  if (promise->requiresUserInteractionHandling()) {
    return true;
  }

  // The returned promise of Promise#then and Promise#catch contains
  // stack info if async stack is enabled.  Even if their return value is not
  // used explicitly in the script, the stack info is observable in devtools
  // and profilers.  We shouldn't apply the optimization not to allocate the
  // returned Promise object if the it's implicitly used by them.
  if (!cx->options().asyncStack()) {
    return false;
  }

  // If devtools is opened, the current realm will become debuggee.
  if (cx->realm()->isDebuggee()) {
    return true;
  }

  // There are 2 profilers, and they can be independently enabled.
  if (cx->runtime()->geckoProfiler().enabled()) {
    return true;
  }
  if (JS::IsProfileTimelineRecordingEnabled()) {
    return true;
  }

  // The stack is also observable from Error#stack, but we don't care since
  // it's nonstandard feature.
  return false;
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Promise.prototype.then ( onFulfilled, onRejected )
 * https://tc39.es/ecma262/#sec-promise.prototype.then
 *
 * Steps 3-5.
 */
static bool OriginalPromiseThenBuiltin(JSContext* cx, HandleValue promiseVal,
                                       HandleValue onFulfilled,
                                       HandleValue onRejected,
                                       MutableHandleValue rval,
                                       bool rvalExplicitlyUsed) {
  cx->check(promiseVal, onFulfilled, onRejected);
  MOZ_ASSERT(CanCallOriginalPromiseThenBuiltin(cx, promiseVal));

  RootedTuple<PromiseObject*, PromiseCapability> roots(cx);
  RootedField<PromiseObject*, 0> promise(
      roots, &promiseVal.toObject().as<PromiseObject>());

  bool rvalUsed = rvalExplicitlyUsed ||
                  IsPromiseThenOrCatchRetValImplicitlyUsed(cx, promise);

  // Step 3. Let C be ? SpeciesConstructor(promise, %Promise%).
  // Step 4. Let resultCapability be ? NewPromiseCapability(C).
  RootedField<PromiseCapability, 1> resultCapability(roots);
  if (rvalUsed) {
    PromiseObject* resultPromise =
        CreatePromiseObjectWithoutResolutionFunctions(cx);
    if (!resultPromise) {
      return false;
    }

    resultPromise->copyUserInteractionFlagsFrom(
        promiseVal.toObject().as<PromiseObject>());
    resultCapability.promise().set(resultPromise);
  }

  // Step 5. Return PerformPromiseThen(promise, onFulfilled, onRejected,
  //                                   resultCapability).
  if (!PerformPromiseThen(cx, promise, onFulfilled, onRejected,
                          resultCapability)) {
    return false;
  }

  if (rvalUsed) {
    rval.setObject(*resultCapability.promise());
  } else {
    rval.setUndefined();
  }
  return true;
}

[[nodiscard]] bool js::RejectPromiseWithPendingError(
    JSContext* cx, Handle<PromiseObject*> promise) {
  cx->check(promise);

  if (!cx->isExceptionPending()) {
    // Reject the promise, but also propagate this uncatchable error.
    (void)PromiseObject::reject(cx, promise, UndefinedHandleValue);
    return false;
  }

  RootedValue exn(cx);
  if (!GetAndClearException(cx, &exn)) {
    return false;
  }
  return PromiseObject::reject(cx, promise, exn);
}

// Some async/await functions are implemented here instead of
// js/src/builtin/AsyncFunction.cpp, to call Promise internal functions.

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Runtime Semantics: EvaluateAsyncFunctionBody
 * AsyncFunctionBody : FunctionBody
 * https://tc39.es/ecma262/#sec-runtime-semantics-evaluateasyncfunctionbody
 *
 * Runtime Semantics: EvaluateAsyncConciseBody
 * AsyncConciseBody : ExpressionBody
 * https://tc39.es/ecma262/#sec-runtime-semantics-evaluateasyncconcisebody
 */
[[nodiscard]] PromiseObject* js::CreatePromiseObjectForAsync(JSContext* cx) {
  // Step 1. Let promiseCapability be ! NewPromiseCapability(%Promise%).
  PromiseObject* promise =
      CreatePromiseObjectWithoutResolutionFunctions(cx, PROMISE_FLAG_ASYNC);
  if (!promise) {
    return nullptr;
  }

  return promise;
}

bool js::IsPromiseForAsyncFunctionOrGenerator(JSObject* promise) {
  return promise->is<PromiseObject>() &&
         PromiseHasAnyFlag(promise->as<PromiseObject>(), PROMISE_FLAG_ASYNC);
}

[[nodiscard]] PromiseObject* js::CreatePromiseObjectForAsyncGenerator(
    JSContext* cx) {
  PromiseObject* promise =
      CreatePromiseObjectWithoutResolutionFunctions(cx, PROMISE_FLAG_ASYNC);
  if (!promise) {
    return nullptr;
  }

  return promise;
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * AsyncFunctionStart ( promiseCapability, asyncFunctionBody )
 * https://tc39.es/ecma262/#sec-async-functions-abstract-operations-async-function-start
 *
 * Steps 4.f-g.
 */
[[nodiscard]] bool js::AsyncFunctionThrown(
    JSContext* cx, Handle<PromiseObject*> resultPromise, HandleValue reason,
    JS::Handle<SavedFrame*> unwrappedRejectionStack) {
  if (resultPromise->state() != JS::PromiseState::Pending) {
    // OOM after resolving promise.
    // Report a warning and ignore the result.
    if (!WarnNumberASCII(cx, JSMSG_UNHANDLABLE_PROMISE_REJECTION_WARNING)) {
      if (cx->isExceptionPending()) {
        cx->clearPendingException();
      }
    }
    return true;
  }

  // Step 4.f. Else,
  // Step 4.f.i. Assert: result.[[Type]] is throw.
  // Step 4.f.ii. Perform
  //              ! Call(promiseCapability.[[Reject]], undefined,
  //                 « result.[[Value]] »).
  // Step 4.g. Return.
  return RejectPromiseInternal(cx, resultPromise, reason,
                               unwrappedRejectionStack);
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * AsyncFunctionStart ( promiseCapability, asyncFunctionBody )
 * https://tc39.es/ecma262/#sec-async-functions-abstract-operations-async-function-start
 *
 * Steps 4.e, 4.g.
 */
[[nodiscard]] bool js::AsyncFunctionReturned(
    JSContext* cx, Handle<PromiseObject*> resultPromise, HandleValue value) {
  if (resultPromise->state() != JS::PromiseState::Pending) {
    if (!WarnNumberASCII(cx, JSMSG_UNHANDLABLE_PROMISE_RESOLUTION_WARNING)) {
      if (cx->isExceptionPending()) {
        cx->clearPendingException();
      }
    }
    return true;
  }

  // Step 4.e. Else if result.[[Type]] is return, then
  // Step 4.e.i. Perform
  //             ! Call(promiseCapability.[[Resolve]], undefined,
  //                    « result.[[Value]] »).
  return ResolvePromiseInternal(cx, resultPromise, value);
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Await
 * https://tc39.github.io/ecma262/#await
 *
 * Helper function that performs Await(promise) steps 2-7.
 * The same steps are also used in a few other places in the spec.
 */
template <typename T>
[[nodiscard]] static bool InternalAwait(JSContext* cx, HandleValue value,
                                        HandleObject resultPromise,
                                        PromiseHandler onFulfilled,
                                        PromiseHandler onRejected,
                                        T extraStep) {
  RootedTuple<JSObject*, PromiseObject*, Value, Value, PromiseCapability,
              PromiseReactionRecord*>
      roots(cx);

  // Step 2. Let promise be ? PromiseResolve(%Promise%, value).
  RootedField<JSObject*, 0> promise(
      roots, PromiseObject::unforgeableResolve(cx, value));
  if (!promise) {
    return false;
  }

  // This downcast is safe because unforgeableResolve either returns `value`
  // (only if it is already a possibly-wrapped promise) or creates a new
  // promise using the Promise constructor.
  RootedField<PromiseObject*, 1> unwrappedPromise(
      roots, UnwrapAndDowncastObject<PromiseObject>(cx, promise));
  if (!unwrappedPromise) {
    return false;
  }

  // Steps 3-6 for creating onFulfilled/onRejected are done by caller.

  // Step 7. Perform ! PerformPromiseThen(promise, onFulfilled, onRejected).
  RootedField<Value, 2> onFulfilledValue(roots,
                                         Int32Value(int32_t(onFulfilled)));
  RootedField<Value, 3> onRejectedValue(roots, Int32Value(int32_t(onRejected)));
  RootedField<PromiseCapability, 4> resultCapability(roots);
  resultCapability.promise().set(resultPromise);

  auto hostDefinedDataObjectOption =
      unwrappedPromise->state() == JS::PromiseState::Pending
          ? HostDefinedDataObjectOption::Allocate
          : HostDefinedDataObjectOption::OptimizeOut;

  RootedField<PromiseReactionRecord*, 5> reaction(
      roots, NewReactionRecord(cx, resultCapability, onFulfilledValue,
                               onRejectedValue, hostDefinedDataObjectOption));
  if (!reaction) {
    return false;
  }
  extraStep(reaction);
  return PerformPromiseThenWithReaction(cx, unwrappedPromise, reaction);
}

// Explicit Resource Management Proposal
// 27.1.3.1 %AsyncIteratorPrototype% [ @@asyncDispose ] ( )
// Steps 6.c-g
// https://arai-a.github.io/ecma262-compare/?pr=3000&id=sec-%25asynciteratorprototype%25-%40%40asyncdispose
// The steps mentioned above are almost identical to the steps 3-7 of
// https://tc39.es/ecma262/#await we have a utility function InternalAwait which
// covers these steps thus this following function wraps around the utility
// and implements the steps of %AsyncIteratorPrototype% [ @@asyncDispose ] ( ).
[[nodiscard]] bool js::InternalAsyncIteratorDisposeAwait(
    JSContext* cx, JS::Handle<JS::Value> value,
    JS::Handle<JSObject*> resultPromise) {
  auto extra = [](JS::Handle<PromiseReactionRecord*> reaction) {};
  return InternalAwait(cx, value, resultPromise,
                       PromiseHandler::AsyncIteratorDisposeAwaitFulfilled,
                       PromiseHandler::Thrower, extra);
}

[[nodiscard]] bool js::InternalAsyncGeneratorAwait(
    JSContext* cx, JS::Handle<AsyncGeneratorObject*> generator,
    JS::Handle<JS::Value> value, PromiseHandler onFulfilled,
    PromiseHandler onRejected) {
  auto extra = [&](Handle<PromiseReactionRecord*> reaction) {
    reaction->setIsAsyncGenerator(generator);
  };
  return InternalAwait(cx, value, nullptr, onFulfilled, onRejected, extra);
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Await
 * https://tc39.es/ecma262/#await
 */
[[nodiscard]] JSObject* js::AsyncFunctionAwait(
    JSContext* cx, Handle<AsyncFunctionGeneratorObject*> genObj,
    HandleValue value) {
  auto extra = [&](Handle<PromiseReactionRecord*> reaction) {
    MOZ_ASSERT(genObj->realm() == reaction->realm());
    MOZ_ASSERT(genObj->realm() == cx->realm());
    reaction->setIsAsyncFunction(genObj);
  };
  if (!InternalAwait(cx, value, nullptr,
                     PromiseHandler::AsyncFunctionAwaitedFulfilled,
                     PromiseHandler::AsyncFunctionAwaitedRejected, extra)) {
    return nullptr;
  }
  return genObj->promise();
}

/**
 * ES2026 draft rev d14670224281909f5bb552e8ebe4a8e958646c16
 *
 * %AsyncFromSyncIteratorPrototype%.next ( [ value ] )
 * https://tc39.es/ecma262/#sec-%asyncfromsynciteratorprototype%.next
 *
 * %AsyncFromSyncIteratorPrototype%.return ( [ value ] )
 * https://tc39.es/ecma262/#sec-%asyncfromsynciteratorprototype%.return
 *
 * %AsyncFromSyncIteratorPrototype%.throw ( [ value ] )
 * https://tc39.es/ecma262/#sec-%asyncfromsynciteratorprototype%.throw
 *
 * AsyncFromSyncIteratorContinuation ( result, promiseCapability,
 *                                     syncIteratorRecord, closeOnRejection )
 * https://tc39.es/ecma262/#sec-asyncfromsynciteratorcontinuation
 */
bool js::AsyncFromSyncIteratorMethod(JSContext* cx, CallArgs& args,
                                     CompletionKind completionKind) {
  // Step 1. Let O be the this value.
  HandleValue thisVal = args.thisv();

  // Step 2. Assert: O is an Object that has a [[SyncIteratorRecord]] internal
  //         slot.
  MOZ_ASSERT(thisVal.isObject());
  MOZ_ASSERT(thisVal.toObject().is<AsyncFromSyncIteratorObject>());

  RootedTuple<PromiseObject*, AsyncFromSyncIteratorObject*, JSObject*, Value,
              Value, Value, JSObject*, Value, Value, Value>
      roots(cx);

  // Step 3. Let promiseCapability be ! NewPromiseCapability(%Promise%).
  RootedField<PromiseObject*, 0> resultPromise(
      roots, CreatePromiseObjectWithoutResolutionFunctions(cx));
  if (!resultPromise) {
    return false;
  }

  RootedField<AsyncFromSyncIteratorObject*, 1> asyncIter(
      roots, &thisVal.toObject().as<AsyncFromSyncIteratorObject>());

  // next():
  // Step 4. Let syncIteratorRecord be O.[[SyncIteratorRecord]].
  //
  // or
  //
  // return() / throw():
  // Step 4. Let syncIteratorRecord be O.[[SyncIteratorRecord]].
  // Step 5. Let syncIterator be syncIteratorRecord.[[Iterator]].
  RootedField<JSObject*, 2> iter(roots, asyncIter->iterator());

  RootedField<Value, 3> func(roots);
  if (completionKind == CompletionKind::Normal) {
    // next() preparing for steps 5-6.
    func.set(asyncIter->nextMethod());
  } else if (completionKind == CompletionKind::Return) {
    // return() steps 6-8.
    // Step 6. Let return be Completion(GetMethod(syncIterator, "return")).
    // Step 7. IfAbruptRejectPromise(return, promiseCapability).
    if (!GetProperty(cx, iter, iter, cx->names().return_, &func)) {
      return AbruptRejectPromise(cx, args, resultPromise, nullptr);
    }

    // Step 8. If return is undefined, then
    // (Note: GetMethod contains a step that changes `null` to `undefined`;
    // we omit that step above, and check for `null` here instead.)
    if (func.isNullOrUndefined()) {
      // Step 8.a. Let iterResult be CreateIterResultObject(value, true).
      PlainObject* resultObj = CreateIterResultObject(cx, args.get(0), true);
      if (!resultObj) {
        return AbruptRejectPromise(cx, args, resultPromise, nullptr);
      }

      RootedField<Value, 4> resultVal(roots, ObjectValue(*resultObj));

      // Step 8.b. Perform ! Call(promiseCapability.[[Resolve]], undefined,
      //                          « iterResult »).
      if (!ResolvePromiseInternal(cx, resultPromise, resultVal)) {
        return AbruptRejectPromise(cx, args, resultPromise, nullptr);
      }

      // Step 8.c. Return promiseCapability.[[Promise]].
      args.rval().setObject(*resultPromise);
      return true;
    }
  } else {
    // throw() steps 6-8.
    MOZ_ASSERT(completionKind == CompletionKind::Throw);

    // Step 6. Let throw be Completion(GetMethod(syncIterator, "throw")).
    // Step 7. IfAbruptRejectPromise(throw, promiseCapability).
    if (!GetProperty(cx, iter, iter, cx->names().throw_, &func)) {
      return AbruptRejectPromise(cx, args, resultPromise, nullptr);
    }

    // Step 8. If throw is undefined, then
    // (Note: GetMethod contains a step that changes `null` to `undefined`;
    // we omit that step above, and check for `null` here instead.)
    if (func.isNullOrUndefined()) {
      // Step 8.a. NOTE: If syncIterator does not have a throw method, close it
      //           to give it a chance to clean up before we reject the
      //           capability.
      // Step 8.b. Let closeCompletion be NormalCompletion(empty).
      // Step 8.c. Let result be Completion(IteratorClose(syncIteratorRecord,
      //           closeCompletion)).
      // Step 8.d. IfAbruptRejectPromise(result, promiseCapability).
      if (!CloseIterOperation(cx, iter, CompletionKind::Normal)) {
        return AbruptRejectPromise(cx, args, resultPromise, nullptr);
      }

      // Step 8.e. NOTE: The next step throws a TypeError to indicate that there
      //           was a protocol violation: syncIterator does not have a throw
      //           method.
      // Step 8.f. NOTE: If closing syncIterator does not throw then the result
      //           of that operation is ignored, even if it yields a rejected
      //           promise.
      // Step 8.g. Perform ! Call(_promiseCapability_.[[Reject]], *undefined*,
      //           « a newly created *TypeError* object »).
      RootedField<Value, 4> noThrowMethodError(roots);
      if (!GetTypeError(cx, JSMSG_ITERATOR_NO_THROW, &noThrowMethodError)) {
        return AbruptRejectPromise(cx, args, resultPromise, nullptr);
      }
      if (!RejectPromiseInternal(cx, resultPromise, noThrowMethodError)) {
        return AbruptRejectPromise(cx, args, resultPromise, nullptr);
      }

      // Step 8.h. Return promiseCapability.[[Promise]].
      args.rval().setObject(*resultPromise);
      return true;
    }
  }

  // next():
  // Step 5. If value is present, then
  // Step 5.a. Let result be Completion(IteratorNext(syncIteratorRecord,
  //                                                 value)).
  // Step 6. Else,
  // Step 6.a. Let result be Completion(IteratorNext(syncIteratorRecord)).
  //
  // or
  //
  // return():
  // Step 9. If value is present, then
  // Step 9.a. Let result be Completion(Call(return, syncIterator,
  //                                         « value »)).
  // Step 10. Else,
  // Step 10.a. Let result be Completion(Call(return, syncIterator)).
  //
  // throw():
  // Step 9. If value is present, then
  // Step 9.a. Let result be Completion(Call(throw, syncIterator,
  //                                         « value »)).
  // Step 10. Else,
  // Step 10.a. Let result be Completion(Call(throw, syncIterator)).
  RootedField<Value, 4> iterVal(roots, ObjectValue(*iter));
  RootedField<Value, 5> resultVal(roots);
  bool ok;
  if (args.length() == 0) {
    ok = Call(cx, func, iterVal, &resultVal);
  } else {
    ok = Call(cx, func, iterVal, args[0], &resultVal);
  }
  if (!ok) {
    // next():
    // Step 7. IfAbruptRejectPromise(result, promiseCapability).
    //
    // return() / throw():
    // Step 11. IfAbruptRejectPromise(result, promiseCapability).
    return AbruptRejectPromise(cx, args, resultPromise, nullptr);
  }

  // next() steps 5-6 -> IteratorNext:
  // Step 5. If result is not an Object, throw a TypeError exception.
  // next():
  // Step 7. IfAbruptRejectPromise(result, promiseCapability).
  //
  // or
  //
  // return() / throw():
  // Step 12. If result is not an Object, then
  // Step 12.a. Perform ! Call(promiseCapability.[[Reject]], undefined,
  //                           « a newly created TypeError object »).
  // Step 12.b. Return promiseCapability.[[Promise]].
  if (!resultVal.isObject()) {
    CheckIsObjectKind kind;
    switch (completionKind) {
      case CompletionKind::Normal:
        kind = CheckIsObjectKind::IteratorNext;
        break;
      case CompletionKind::Throw:
        kind = CheckIsObjectKind::IteratorThrow;
        break;
      case CompletionKind::Return:
        kind = CheckIsObjectKind::IteratorReturn;
        break;
    }
    MOZ_ALWAYS_FALSE(ThrowCheckIsObject(cx, kind));
    return AbruptRejectPromise(cx, args, resultPromise, nullptr);
  }

  RootedField<JSObject*, 6> resultObj(roots, &resultVal.toObject());

  // next():
  // Step 8. Return AsyncFromSyncIteratorContinuation(result,
  //                                                  promiseCapability,
  //                                                  syncIteratorRecord,
  //                                                  true).
  //
  // return():
  // Step 13. Return AsyncFromSyncIteratorContinuation(result,
  //                                                   promiseCapability,
  //                                                   syncIteratorRecord,
  //                                                   false).
  //
  // throw():
  // Step 13. Return AsyncFromSyncIteratorContinuation(result,
  //                                                   promiseCapability,
  //                                                   syncIteratorRecord,
  //                                                   true).

  // AsyncFromSyncIteratorContinuation is passed |closeOnRejection = true| iff
  // completion-kind is not "return".
  bool closeOnRejection = completionKind != CompletionKind::Return;

  // The step numbers below are for AsyncFromSyncIteratorContinuation().
  //
  // Step 1. NOTE: Because promiseCapability is derived from the intrinsic
  //         %Promise%, the calls to promiseCapability.[[Reject]] entailed by
  //         the use IfAbruptRejectPromise below are guaranteed not to throw.
  // Step 2. Let done be Completion(IteratorComplete(result)).
  // Step 3. IfAbruptRejectPromise(done, promiseCapability).
  RootedField<Value, 7> doneVal(roots);
  if (!GetProperty(cx, resultObj, resultObj, cx->names().done, &doneVal)) {
    return AbruptRejectPromise(cx, args, resultPromise, nullptr);
  }
  bool done = ToBoolean(doneVal);

  // Step 4. Let value be Completion(IteratorValue(result)).
  // Step 5. IfAbruptRejectPromise(value, promiseCapability).
  RootedField<Value, 8> value(roots);
  if (!GetProperty(cx, resultObj, resultObj, cx->names().value, &value)) {
    return AbruptRejectPromise(cx, args, resultPromise, nullptr);
  }

  // Step 9. Let unwrap be a new Abstract Closure with parameters (v) that
  //         captures done and performs the following steps when called:
  // Step 9.a. Return CreateIterResultObject(v, done).
  // Step 10. Let onFulfilled be CreateBuiltinFunction(unwrap, 1, "", « »).
  // Step 11. NOTE: onFulfilled is used when processing the "value" property
  //          of an IteratorResult object in order to wait for its value if it
  //          is a promise and re-package the result in a new "unwrapped"
  //          IteratorResult object.
  PromiseHandler onFulfilled =
      done ? PromiseHandler::AsyncFromSyncIteratorValueUnwrapDone
           : PromiseHandler::AsyncFromSyncIteratorValueUnwrapNotDone;

  // Step 12. If done is true, or if closeOnRejection is false, then
  // Step 12.a. Let onRejected be undefined.
  // Step 13. Else,
  // Step 13.a. Let closeIterator be a new Abstract Closure with parameters
  //            (error) that captures syncIteratorRecord and performs the
  //            following steps when called:
  // Step 13.a.i. Return ? IteratorClose(syncIteratorRecord,
  //                                     ThrowCompletion(error)).
  // Step 13.b. Let onRejected be CreateBuiltinFunction(closeIterator, 1, "",
  //            «»).
  // Step 13.c. NOTE: onRejected is used to close the Iterator when the "value"
  //            property of an IteratorResult object it yields is a rejected
  //            promise.
  PromiseHandler onRejected = done || !closeOnRejection
                                  ? PromiseHandler::Thrower
                                  : PromiseHandler::AsyncFromSyncIteratorClose;

  // Steps 6, 8, and 14 are identical to some steps in Await; we have a utility
  // function InternalAwait() that implements the idiom.
  //
  // Step 6. Let valueWrapper be Completion(PromiseResolve(%Promise%, value)).
  // Step 8. IfAbruptRejectPromise(valueWrapper, promiseCapability).
  // Step 14. Perform PerformPromiseThen(valueWrapper, onFulfilled,
  //                                     onRejected, promiseCapability).
  auto extra = [&](Handle<PromiseReactionRecord*> reaction) {
    if (onRejected == PromiseHandler::AsyncFromSyncIteratorClose) {
      reaction->setIsAsyncFromSyncIterator(asyncIter);
    }
  };
  if (!InternalAwait(cx, value, resultPromise, onFulfilled, onRejected,
                     extra)) {
    // Reordering step 7 to this point is fine, because there are no other
    // user-observable operations between steps 7-8 and 14. And |InternalAwait|,
    // apart from the initial PromiseResolve, can only fail due to OOM. Since
    // out-of-memory handling is not defined in the spec, we're also free
    // reorder its effects. So w.r.t. spec-observable steps, calling
    // |IteratorCloseForException| after |InternalAwait| has the same effect as
    // performing IteratorClose directly after PromiseResolve.
    //
    // Step 7. If valueWrapper is an abrupt completion, done is false, and
    //         closeOnRejection is true, then
    // Step 7.a. Set valueWrapper to Completion(IteratorClose(
    //           syncIteratorRecord, valueWrapper)).
    //
    // Check |cx->isExceptionPending()| because we don't want to perform
    // IteratorClose for uncatchable exceptions. (IteratorCloseForException will
    // also assert when there's no pending exception.)
    if (cx->isExceptionPending() && !done && closeOnRejection) {
      (void)IteratorCloseForException(cx, iter);
    }
    return AbruptRejectPromise(cx, args, resultPromise, nullptr);
  }

  // Step 15. Return promiseCapability.[[Promise]].
  args.rval().setObject(*resultPromise);
  return true;
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Promise.prototype.catch ( onRejected )
 * https://tc39.es/ecma262/#sec-promise.prototype.catch
 */
static bool Promise_catch_impl(JSContext* cx, unsigned argc, Value* vp,
                               bool rvalExplicitlyUsed) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // Step 1. Let promise be the this value.
  HandleValue thisVal = args.thisv();
  HandleValue onFulfilled = UndefinedHandleValue;
  HandleValue onRejected = args.get(0);

  // Fast path when the default Promise state is intact.
  if (CanCallOriginalPromiseThenBuiltin(cx, thisVal)) {
    return OriginalPromiseThenBuiltin(cx, thisVal, onFulfilled, onRejected,
                                      args.rval(), rvalExplicitlyUsed);
  }

  // Step 2. Return ? Invoke(promise, "then", « undefined, onRejected »).
  RootedValue thenVal(cx);
  RootedObject thisObj(cx, ToObject(cx, thisVal));
  if (!thisObj) {
    return false;
  }
  if (!GetProperty(cx, thisObj, thisVal, cx->names().then, &thenVal)) {
    return false;
  }

  if (IsNativeFunction(thenVal, &Promise_then) &&
      thenVal.toObject().nonCCWRealm() == cx->realm()) {
    return Promise_then_impl(cx, thisVal, onFulfilled, onRejected, args.rval(),
                             rvalExplicitlyUsed);
  }

  return Call(cx, thenVal, thisVal, UndefinedHandleValue, onRejected,
              args.rval());
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Promise.prototype.catch ( onRejected )
 * https://tc39.es/ecma262/#sec-promise.prototype.catch
 */
static bool Promise_catch_noRetVal(JSContext* cx, unsigned argc, Value* vp) {
  return Promise_catch_impl(cx, argc, vp, false);
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Promise.prototype.catch ( onRejected )
 * https://tc39.es/ecma262/#sec-promise.prototype.catch
 */
static bool Promise_catch(JSContext* cx, unsigned argc, Value* vp) {
  return Promise_catch_impl(cx, argc, vp, true);
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Promise.prototype.then ( onFulfilled, onRejected )
 * https://tc39.es/ecma262/#sec-promise.prototype.then
 */
static bool Promise_then_impl(JSContext* cx, HandleValue promiseVal,
                              HandleValue onFulfilled, HandleValue onRejected,
                              MutableHandleValue rval,
                              bool rvalExplicitlyUsed) {
  // Step 1. Let promise be the this value.
  // (implicit)

  // Step 2. If IsPromise(promise) is false, throw a TypeError exception.
  if (!promiseVal.isObject()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_OBJECT_REQUIRED,
                              "Receiver of Promise.prototype.then call");
    return false;
  }

  // Fast path when the default Promise state is intact.
  if (CanCallOriginalPromiseThenBuiltin(cx, promiseVal)) {
    // Steps 3-5.
    return OriginalPromiseThenBuiltin(cx, promiseVal, onFulfilled, onRejected,
                                      rval, rvalExplicitlyUsed);
  }

  RootedObject promiseObj(cx, &promiseVal.toObject());
  Rooted<PromiseObject*> unwrappedPromise(
      cx,
      UnwrapAndTypeCheckValue<PromiseObject>(cx, promiseVal, [cx, &promiseVal] {
        JS_ReportErrorNumberLatin1(cx, GetErrorMessage, nullptr,
                                   JSMSG_INCOMPATIBLE_PROTO, "Promise", "then",
                                   InformalValueTypeName(promiseVal));
      }));
  if (!unwrappedPromise) {
    return false;
  }

  bool rvalUsed =
      rvalExplicitlyUsed ||
      IsPromiseThenOrCatchRetValImplicitlyUsed(cx, unwrappedPromise);

  // Step 3. Let C be ? SpeciesConstructor(promise, %Promise%).
  // Step 4. Let resultCapability be ? NewPromiseCapability(C).
  CreateDependentPromise createDependent =
      rvalUsed ? CreateDependentPromise::Always
               : CreateDependentPromise::SkipIfCtorUnobservable;
  Rooted<PromiseCapability> resultCapability(cx);
  if (!PromiseThenNewPromiseCapability(cx, promiseObj, createDependent,
                                       &resultCapability)) {
    return false;
  }

  // Step 5. Return PerformPromiseThen(promise, onFulfilled, onRejected,
  //                                   resultCapability).
  if (!PerformPromiseThen(cx, unwrappedPromise, onFulfilled, onRejected,
                          resultCapability)) {
    return false;
  }

  if (rvalUsed) {
    rval.setObject(*resultCapability.promise());
  } else {
    rval.setUndefined();
  }
  return true;
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Promise.prototype.then ( onFulfilled, onRejected )
 * https://tc39.es/ecma262/#sec-promise.prototype.then
 */
bool Promise_then_noRetVal(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  return Promise_then_impl(cx, args.thisv(), args.get(0), args.get(1),
                           args.rval(), false);
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * Promise.prototype.then ( onFulfilled, onRejected )
 * https://tc39.es/ecma262/#sec-promise.prototype.then
 */
bool js::Promise_then(JSContext* cx, unsigned argc, Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);
  return Promise_then_impl(cx, args.thisv(), args.get(0), args.get(1),
                           args.rval(), true);
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * PerformPromiseThen ( promise, onFulfilled, onRejected
 *                      [ , resultCapability ] )
 * https://tc39.es/ecma262/#sec-performpromisethen
 *
 * Steps 1-12.
 */
[[nodiscard]] static bool PerformPromiseThen(
    JSContext* cx, Handle<PromiseObject*> promise, HandleValue onFulfilled_,
    HandleValue onRejected_, Handle<PromiseCapability> resultCapability) {
  // Step 1. Assert: IsPromise(promise) is true.
  // Step 2. If resultCapability is not present, then
  // Step 2. a. Set resultCapability to undefined.
  // (implicit)

  // (reordered)
  // Step 4. Else,
  // Step 4. a. Let onFulfilledJobCallback be HostMakeJobCallback(onFulfilled).
  RootedValue onFulfilled(cx, onFulfilled_);

  // Step 3. If IsCallable(onFulfilled) is false, then
  if (!IsCallable(onFulfilled)) {
    // Step 3. a. Let onFulfilledJobCallback be empty.
    onFulfilled = Int32Value(int32_t(PromiseHandler::Identity));
  }

  // (reordered)
  // Step 6. Else,
  // Step 6. a. Let onRejectedJobCallback be HostMakeJobCallback(onRejected).
  RootedValue onRejected(cx, onRejected_);

  // Step 5. If IsCallable(onRejected) is false, then
  if (!IsCallable(onRejected)) {
    // Step 5. a. Let onRejectedJobCallback be empty.
    onRejected = Int32Value(int32_t(PromiseHandler::Thrower));
  }

  // Step 7. Let fulfillReaction be the PromiseReaction
  //         { [[Capability]]: resultCapability, [[Type]]: Fulfill,
  //           [[Handler]]: onFulfilledJobCallback }.
  // Step 8. Let rejectReaction be the PromiseReaction
  //         { [[Capability]]: resultCapability, [[Type]]: Reject,
  //           [[Handler]]: onRejectedJobCallback }.
  //
  // NOTE: We use single object for both reactions.
  auto hostDefinedDataObjectOption =
      promise->state() == JS::PromiseState::Pending
          ? HostDefinedDataObjectOption::Allocate
          : HostDefinedDataObjectOption::OptimizeOut;
  Rooted<PromiseReactionRecord*> reaction(
      cx, NewReactionRecord(cx, resultCapability, onFulfilled, onRejected,
                            hostDefinedDataObjectOption));
  if (!reaction) {
    return false;
  }

  // Steps 9-14.
  return PerformPromiseThenWithReaction(cx, promise, reaction);
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * PerformPromiseThen ( promise, onFulfilled, onRejected
 *                      [ , resultCapability ] )
 * https://tc39.es/ecma262/#sec-performpromisethen
 */
[[nodiscard]] static bool PerformPromiseThenWithoutSettleHandlers(
    JSContext* cx, Handle<PromiseObject*> promise,
    Handle<PromiseObject*> promiseToResolve,
    Handle<PromiseCapability> resultCapability) {
  // Step 1. Assert: IsPromise(promise) is true.
  // Step 2. If resultCapability is not present, then
  // (implicit)

  // Step 3. If IsCallable(onFulfilled) is false, then
  // Step 3.a. Let onFulfilledJobCallback be empty.
  HandleValue onFulfilled = NullHandleValue;

  // Step 5. If IsCallable(onRejected) is false, then
  // Step 5.a. Let onRejectedJobCallback be empty.
  HandleValue onRejected = NullHandleValue;

  // When the promise's state isn't pending, the embedding
  // should be able to retrieve the host defined object
  // on their own, so here we optimize out from the
  // our side.
  auto hostDefinedDataObjectOption =
      promise->state() == JS::PromiseState::Pending
          ? HostDefinedDataObjectOption::Allocate
          : HostDefinedDataObjectOption::OptimizeOut;

  // Step 7. Let fulfillReaction be the PromiseReaction
  //         { [[Capability]]: resultCapability, [[Type]]: Fulfill,
  //           [[Handler]]: onFulfilledJobCallback }.
  // Step 8. Let rejectReaction be the PromiseReaction
  //         { [[Capability]]: resultCapability, [[Type]]: Reject,
  //           [[Handler]]: onRejectedJobCallback }.
  Rooted<PromiseReactionRecord*> reaction(
      cx, NewReactionRecord(cx, resultCapability, onFulfilled, onRejected,
                            hostDefinedDataObjectOption));
  if (!reaction) {
    return false;
  }

  reaction->setIsDefaultResolvingHandler(promiseToResolve);

  // Steps 9-12.
  return PerformPromiseThenWithReaction(cx, promise, reaction);
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * PerformPromiseThen ( promise, onFulfilled, onRejected
 *                      [ , resultCapability ] )
 * https://tc39.github.io/ecma262/#sec-performpromisethen
 *
 * Steps 9-12.
 */
[[nodiscard]] static bool PerformPromiseThenWithReaction(
    JSContext* cx, Handle<PromiseObject*> unwrappedPromise,
    Handle<PromiseReactionRecord*> reaction) {
  // Step 9. If promise.[[PromiseState]] is pending, then
  JS::PromiseState state = unwrappedPromise->state();
  int32_t flags = unwrappedPromise->flags();
  if (state == JS::PromiseState::Pending) {
    // Step 9.a. Append fulfillReaction as the last element of the List that is
    //           promise.[[PromiseFulfillReactions]].
    // Step 9.b. Append rejectReaction as the last element of the List that is
    //           promise.[[PromiseRejectReactions]].
    //
    // Instead of creating separate reaction records for fulfillment and
    // rejection, we create a combined record. All places we use the record
    // can handle that.
    if (!AddPromiseReaction(cx, unwrappedPromise, reaction)) {
      return false;
    }
  }

  // Steps 10-11.
  else {
    // Step 11.a. Assert: The value of promise.[[PromiseState]] is rejected.
    MOZ_ASSERT_IF(state != JS::PromiseState::Fulfilled,
                  state == JS::PromiseState::Rejected);

    // Step 10.a. Let value be promise.[[PromiseResult]].
    // Step 11.b. Let reason be promise.[[PromiseResult]].
    RootedValue valueOrReason(cx, unwrappedPromise->valueOrReason());

    // We might be operating on a promise from another compartment. In that
    // case, we need to wrap the result/reason value before using it.
    if (!cx->compartment()->wrap(cx, &valueOrReason)) {
      return false;
    }

    // Step 11.c. If promise.[[PromiseIsHandled]] is false,
    //            perform HostPromiseRejectionTracker(promise, "handle").
    if (state == JS::PromiseState::Rejected &&
        !(flags & PROMISE_FLAG_HANDLED)) {
      cx->runtime()->removeUnhandledRejectedPromise(cx, unwrappedPromise);
    }

    // Step 10.b. Let fulfillJob be
    //            NewPromiseReactionJob(fulfillReaction, value).
    // Step 10.c. Perform HostEnqueuePromiseJob(fulfillJob.[[Job]],
    //                                          fulfillJob.[[Realm]]).
    // Step 11.d. Let rejectJob be
    //            NewPromiseReactionJob(rejectReaction, reason).
    // Step 11.e. Perform HostEnqueuePromiseJob(rejectJob.[[Job]],
    //                                          rejectJob.[[Realm]]).
    if (!EnqueuePromiseReactionJob(cx, reaction, valueOrReason, state)) {
      return false;
    }
  }

  // Step 12. Set promise.[[PromiseIsHandled]] to true.
  unwrappedPromise->setHandled();

  return true;
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * PerformPromiseThen ( promise, onFulfilled, onRejected
 *                      [ , resultCapability ] )
 * https://tc39.github.io/ecma262/#sec-performpromisethen
 *
 * Steps 9.a-b.
 */
[[nodiscard]] static bool AddPromiseReaction(
    JSContext* cx, Handle<PromiseObject*> unwrappedPromise,
    Handle<PromiseReactionRecord*> reaction) {
  MOZ_RELEASE_ASSERT(reaction->is<PromiseReactionRecord>());
  RootedValue reactionVal(cx, ObjectValue(*reaction));

  // The code that creates Promise reactions can handle wrapped Promises,
  // unwrapping them as needed. That means that the `promise` and `reaction`
  // objects we have here aren't necessarily from the same compartment. In
  // order to store the reaction on the promise, we have to ensure that it
  // is properly wrapped.
  mozilla::Maybe<AutoRealm> ar;
  if (unwrappedPromise->compartment() != cx->compartment()) {
    ar.emplace(cx, unwrappedPromise);
    if (!cx->compartment()->wrap(cx, &reactionVal)) {
      return false;
    }
  }
  Handle<PromiseObject*> promise = unwrappedPromise;

  // Step 9.a. Append fulfillReaction as the last element of the List that is
  //           promise.[[PromiseFulfillReactions]].
  // Step 9.b. Append rejectReaction as the last element of the List that is
  //           promise.[[PromiseRejectReactions]].
  RootedValue reactionsVal(cx, promise->reactions());

  if (reactionsVal.isUndefined()) {
    // If no reactions existed so far, just store the reaction record directly.
    promise->setFixedSlot(PromiseObject::REACTIONS_OR_RESULT_SLOT, reactionVal);
    return true;
  }

  RootedObject reactionsObj(cx, &reactionsVal.toObject());

  // If only a single reaction exists, it's stored directly instead of in a
  // list. In that case, `reactionsObj` might be a wrapper, which we can
  // always safely unwrap.
  if (IsProxy(reactionsObj)) {
    reactionsObj = UncheckedUnwrap(reactionsObj);
    if (JS_IsDeadWrapper(reactionsObj)) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_DEAD_OBJECT);
      return false;
    }
    MOZ_RELEASE_ASSERT(reactionsObj->is<PromiseReactionRecord>());
  }

  if (reactionsObj->is<PromiseReactionRecord>()) {
    // If a single reaction existed so far, create a list and store the
    // old and the new reaction in it.
    ArrayObject* reactions = NewDenseFullyAllocatedArray(cx, 2);
    if (!reactions) {
      return false;
    }

    reactions->setDenseInitializedLength(2);
    reactions->initDenseElement(0, reactionsVal);
    reactions->initDenseElement(1, reactionVal);

    promise->setFixedSlot(PromiseObject::REACTIONS_OR_RESULT_SLOT,
                          ObjectValue(*reactions));
  } else {
    // Otherwise, just store the new reaction.
    MOZ_RELEASE_ASSERT(reactionsObj->is<NativeObject>());
    Handle<NativeObject*> reactions = reactionsObj.as<NativeObject>();
    uint32_t len = reactions->getDenseInitializedLength();
    DenseElementResult result = reactions->ensureDenseElements(cx, len, 1);
    if (result != DenseElementResult::Success) {
      MOZ_ASSERT(result == DenseElementResult::Failure);
      return false;
    }
    reactions->setDenseElement(len, reactionVal);
  }

  return true;
}

[[nodiscard]] static bool AddDummyPromiseReactionForDebugger(
    JSContext* cx, Handle<PromiseObject*> promise,
    HandleObject dependentPromise) {
  if (promise->state() != JS::PromiseState::Pending) {
    return true;
  }

  if (JS_IsDeadWrapper(UncheckedUnwrap(dependentPromise))) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, JSMSG_DEAD_OBJECT);
    return false;
  }

  // `dependentPromise` should be a maybe-wrapped Promise.
  MOZ_ASSERT(UncheckedUnwrap(dependentPromise)->is<PromiseObject>());

  // Leave resolve and reject as null.
  Rooted<PromiseCapability> capability(cx);
  capability.promise().set(dependentPromise);

  Rooted<PromiseReactionRecord*> reaction(
      cx, NewReactionRecord(cx, capability, NullHandleValue, NullHandleValue,
                            HostDefinedDataObjectOption::UnusedForDebugger));
  if (!reaction) {
    return false;
  }

  reaction->setIsDebuggerDummy();

  return AddPromiseReaction(cx, promise, reaction);
}

uint64_t PromiseObject::getID() { return PromiseDebugInfo::id(this); }

double PromiseObject::lifetime() {
  return MillisecondsSinceStartup() - allocationTime();
}

/**
 * Returns all promises that directly depend on this one. That means those
 * created by calling `then` on this promise, or the promise returned by
 * `Promise.all(iterable)` or `Promise.race(iterable)`, with this promise
 * being a member of the passed-in `iterable`.
 *
 * Per spec, we should have separate lists of reaction records for the
 * fulfill and reject cases. As an optimization, we have only one of those,
 * containing the required data for both cases. So we just walk that list
 * and extract the dependent promises from all reaction records.
 */
bool PromiseObject::dependentPromises(JSContext* cx,
                                      MutableHandle<GCVector<Value>> values) {
  if (state() != JS::PromiseState::Pending) {
    return true;
  }

  uint32_t valuesIndex = 0;
  RootedValue reactionsVal(cx, reactions());

  return ForEachReaction(cx, reactionsVal, [&](MutableHandleObject obj) {
    if (IsProxy(obj)) {
      obj.set(UncheckedUnwrap(obj));
    }

    if (JS_IsDeadWrapper(obj)) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_DEAD_OBJECT);
      return false;
    }

    MOZ_RELEASE_ASSERT(obj->is<PromiseReactionRecord>());
    auto* reaction = &obj->as<PromiseReactionRecord>();

    // Not all reactions have a Promise on them.
    JSObject* promiseObj = reaction->promise();
    if (promiseObj) {
      if (!values.growBy(1)) {
        return false;
      }

      values[valuesIndex++].setObject(*promiseObj);
    }
    return true;
  });
}

bool PromiseObject::forEachReactionRecord(
    JSContext* cx, PromiseReactionRecordBuilder& builder) {
  if (state() != JS::PromiseState::Pending) {
    // Promise was resolved, so no reaction records are present.
    return true;
  }

  RootedTuple<Value, PromiseReactionRecord*, AsyncFunctionGeneratorObject*,
              AsyncGeneratorObject*, PromiseObject*, JSObject*, JSObject*,
              JSObject*>
      roots(cx);
  RootedField<Value, 0> reactionsVal(roots, reactions());
  if (reactionsVal.isNullOrUndefined()) {
    // No reaction records are attached to this promise.
    return true;
  }

  return ForEachReaction(cx, reactionsVal, [&](MutableHandleObject obj) {
    if (IsProxy(obj)) {
      obj.set(UncheckedUnwrap(obj));
    }

    if (JS_IsDeadWrapper(obj)) {
      JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                JSMSG_DEAD_OBJECT);
      return false;
    }

    RootedField<PromiseReactionRecord*, 1> reaction(
        roots, &obj->as<PromiseReactionRecord>());
    MOZ_ASSERT(reaction->targetState() == JS::PromiseState::Pending);

    if (reaction->isAsyncFunction()) {
      RootedField<AsyncFunctionGeneratorObject*, 2> generator(
          roots, reaction->asyncFunctionGenerator());
      if (!builder.asyncFunction(cx, generator)) {
        return false;
      }
    } else if (reaction->isAsyncGenerator()) {
      RootedField<AsyncGeneratorObject*, 3> generator(
          roots, reaction->asyncGenerator());
      if (!builder.asyncGenerator(cx, generator)) {
        return false;
      }
    } else if (reaction->isDefaultResolvingHandler()) {
      RootedField<PromiseObject*, 4> promise(
          roots, reaction->defaultResolvingPromise());
      if (!builder.direct(cx, promise)) {
        return false;
      }
    } else {
      RootedField<JSObject*, 5> resolve(roots);
      RootedField<JSObject*, 6> reject(roots);
      RootedField<JSObject*, 7> result(roots, reaction->promise());

      Value v = reaction->getFixedSlot(PromiseReactionRecord::OnFulfilled);
      if (v.isObject()) {
        resolve = &v.toObject();
      }

      v = reaction->getFixedSlot(PromiseReactionRecord::OnRejected);
      if (v.isObject()) {
        reject = &v.toObject();
      }

      if (!builder.then(cx, resolve, reject, result)) {
        return false;
      }
    }

    return true;
  });
}

/**
 * ES2023 draft rev 714fa3dd1e8237ae9c666146270f81880089eca5
 *
 * Promise Reject Functions
 * https://tc39.es/ecma262/#sec-promise-reject-functions
 */
static bool CallDefaultPromiseResolveFunction(JSContext* cx,
                                              Handle<PromiseObject*> promise,
                                              HandleValue resolutionValue) {
  MOZ_ASSERT(IsPromiseWithDefaultResolvingFunction(promise));

  // Steps 1-3.
  // (implicit)

  // Step 4. Let alreadyResolved be F.[[AlreadyResolved]].
  // Step 5. If alreadyResolved.[[Value]] is true, return undefined.
  if (IsAlreadyResolvedPromiseWithDefaultResolvingFunction(promise)) {
    return true;
  }

  // Step 6. Set alreadyResolved.[[Value]] to true.
  SetAlreadyResolvedPromiseWithDefaultResolvingFunction(promise);

  // Steps 7-15.
  // (implicit) Step 16. Return undefined.
  return ResolvePromiseInternal(cx, promise, resolutionValue);
}

/* static */
bool PromiseObject::resolve(JSContext* cx, Handle<PromiseObject*> promise,
                            HandleValue resolutionValue) {
  MOZ_ASSERT(!PromiseHasAnyFlag(*promise, PROMISE_FLAG_ASYNC));
  if (promise->state() != JS::PromiseState::Pending) {
    return true;
  }

  if (IsPromiseWithDefaultResolvingFunction(promise)) {
    return CallDefaultPromiseResolveFunction(cx, promise, resolutionValue);
  }

  JSFunction* resolveFun = GetResolveFunctionFromPromise(promise);
  if (!resolveFun) {
    return true;
  }

  RootedValue funVal(cx, ObjectValue(*resolveFun));

  // For xray'd Promises, the resolve fun may have been created in another
  // compartment. For the call below to work in that case, wrap the
  // function into the current compartment.
  if (!cx->compartment()->wrap(cx, &funVal)) {
    return false;
  }

  RootedValue dummy(cx);
  return Call(cx, funVal, UndefinedHandleValue, resolutionValue, &dummy);
}

/**
 * ES2023 draft rev 714fa3dd1e8237ae9c666146270f81880089eca5
 *
 * Promise Reject Functions
 * https://tc39.es/ecma262/#sec-promise-reject-functions
 */
static bool CallDefaultPromiseRejectFunction(
    JSContext* cx, Handle<PromiseObject*> promise, HandleValue rejectionValue,
    JS::Handle<SavedFrame*> unwrappedRejectionStack /* = nullptr */) {
  MOZ_ASSERT(IsPromiseWithDefaultResolvingFunction(promise));

  // Steps 1-3.
  // (implicit)

  // Step 4. Let alreadyResolved be F.[[AlreadyResolved]].
  // Step 5. If alreadyResolved.[[Value]] is true, return undefined.
  if (IsAlreadyResolvedPromiseWithDefaultResolvingFunction(promise)) {
    return true;
  }

  // Step 6. Set alreadyResolved.[[Value]] to true.
  SetAlreadyResolvedPromiseWithDefaultResolvingFunction(promise);

  return RejectPromiseInternal(cx, promise, rejectionValue,
                               unwrappedRejectionStack);
}

/* static */
bool PromiseObject::reject(JSContext* cx, Handle<PromiseObject*> promise,
                           HandleValue rejectionValue) {
  MOZ_ASSERT(!PromiseHasAnyFlag(*promise, PROMISE_FLAG_ASYNC));
  if (promise->state() != JS::PromiseState::Pending) {
    return true;
  }

  if (IsPromiseWithDefaultResolvingFunction(promise)) {
    return CallDefaultPromiseRejectFunction(cx, promise, rejectionValue);
  }

  RootedValue funVal(
      cx, promise->getFixedSlotTyped(PromiseObject::REJECT_FUNCTION_SLOT));
  MOZ_ASSERT(IsCallable(funVal));

  RootedValue dummy(cx);
  return Call(cx, funVal, UndefinedHandleValue, rejectionValue, &dummy);
}

/**
 * ES2022 draft rev d03c1ec6e235a5180fa772b6178727c17974cb14
 *
 * RejectPromise ( promise, reason )
 * https://tc39.es/ecma262/#sec-rejectpromise
 *
 * Step 7.
 */
/* static */
void PromiseObject::onSettled(JSContext* cx, Handle<PromiseObject*> promise,
                              Handle<SavedFrame*> unwrappedRejectionStack) {
  PromiseDebugInfo::setResolutionInfo(cx, promise, unwrappedRejectionStack);

  // Step 7. If promise.[[PromiseIsHandled]] is false, perform
  //         HostPromiseRejectionTracker(promise, "reject").
  if (promise->state() == JS::PromiseState::Rejected &&
      promise->isUnhandled()) {
    cx->runtime()->addUnhandledRejectedPromise(cx, promise);
  }
}

void PromiseObject::setRequiresUserInteractionHandling(bool state) {
  if (state) {
    AddPromiseFlags(*this, PROMISE_FLAG_REQUIRES_USER_INTERACTION_HANDLING);
  } else {
    RemovePromiseFlags(*this, PROMISE_FLAG_REQUIRES_USER_INTERACTION_HANDLING);
  }
}

void PromiseObject::setHadUserInteractionUponCreation(bool state) {
  if (state) {
    AddPromiseFlags(*this, PROMISE_FLAG_HAD_USER_INTERACTION_UPON_CREATION);
  } else {
    RemovePromiseFlags(*this, PROMISE_FLAG_HAD_USER_INTERACTION_UPON_CREATION);
  }
}

void PromiseObject::copyUserInteractionFlagsFrom(PromiseObject& rhs) {
  setRequiresUserInteractionHandling(rhs.requiresUserInteractionHandling());
  setHadUserInteractionUponCreation(rhs.hadUserInteractionUponCreation());
}

#if defined(DEBUG) || defined(JS_JITSPEW)
void PromiseDebugInfo::dumpOwnFields(js::JSONPrinter& json) const {
  if (getFixedSlot(Slot_Id).isNumber()) {
    json.formatProperty("id", "%lf", getFixedSlot(Slot_Id).toNumber());
  }

  if (getFixedSlot(Slot_AllocationTime).isNumber()) {
    json.formatProperty("allocationTime", "%lf",
                        getFixedSlot(Slot_AllocationTime).toNumber());
  }

  {
    js::GenericPrinter& out = json.beginStringProperty("allocationSite");
    getFixedSlot(Slot_AllocationSite).dumpStringContent(out);
    json.endStringProperty();
  }

  if (getFixedSlot(Slot_ResolutionTime).isNumber()) {
    json.formatProperty("resolutionTime", "%lf",
                        getFixedSlot(Slot_ResolutionTime).toNumber());
  }

  {
    js::GenericPrinter& out = json.beginStringProperty("resolutionSite");
    getFixedSlot(Slot_ResolutionSite).dumpStringContent(out);
    json.endStringProperty();
  }
}

template <typename KnownF, typename UnknownF>
/* static */
void PromiseReactionRecord::forEachReactionFlag(uint32_t flags, KnownF known,
                                                UnknownF unknown) {
  for (uint32_t i = 1; i; i = i << 1) {
    if (!(flags & i)) {
      continue;
    }
    switch (flags & i) {
      case REACTION_FLAG_RESOLVED:
        known("RESOLVED");
        break;
      case REACTION_FLAG_FULFILLED:
        known("FULFILLED");
        break;
      case REACTION_FLAG_DEFAULT_RESOLVING_HANDLER:
        known("DEFAULT_RESOLVING_HANDLER");
        break;
      case REACTION_FLAG_ASYNC_FUNCTION:
        known("ASYNC_FUNCTION");
        break;
      case REACTION_FLAG_ASYNC_GENERATOR:
        known("ASYNC_GENERATOR");
        break;
      case REACTION_FLAG_DEBUGGER_DUMMY:
        known("DEBUGGER_DUMMY");
        break;
      case REACTION_FLAG_IGNORE_UNHANDLED_REJECTION:
        known("IGNORE_UNHANDLED_REJECTION");
        break;
      default:
        unknown(i);
        break;
    }
  }
}

void PromiseReactionRecord::dumpOwnFields(js::JSONPrinter& json) const {
  if (promise()) {
    js::GenericPrinter& out = json.beginStringProperty("promise");
    promise()->dumpStringContent(out);
    json.endStringProperty();
  }

  if (targetState() == JS::PromiseState::Fulfilled) {
    {
      js::GenericPrinter& out = json.beginStringProperty("onFulfilled");
      getFixedSlot(OnFulfilled).dumpStringContent(out);
      json.endStringProperty();
    }
    {
      js::GenericPrinter& out = json.beginStringProperty("onFulfilledArg");
      getFixedSlot(OnFulfilledArg).dumpStringContent(out);
      json.endStringProperty();
    }
  }

  if (targetState() == JS::PromiseState::Rejected) {
    {
      js::GenericPrinter& out = json.beginStringProperty("onRejected");
      getFixedSlot(OnRejected).dumpStringContent(out);
      json.endStringProperty();
    }
    {
      js::GenericPrinter& out = json.beginStringProperty("onRejectedArg");
      getFixedSlot(OnRejectedArg).dumpStringContent(out);
      json.endStringProperty();
    }
  }

  if (!getFixedSlot(Resolve).isNull()) {
    js::GenericPrinter& out = json.beginStringProperty("resolve");
    getFixedSlot(Resolve).dumpStringContent(out);
    json.endStringProperty();
  }

  if (!getFixedSlot(Reject).isNull()) {
    js::GenericPrinter& out = json.beginStringProperty("reject");
    getFixedSlot(Reject).dumpStringContent(out);
    json.endStringProperty();
  }

  if (!getFixedSlot(IncumbentGlobalRepresentative).isUndefined()) {
    js::GenericPrinter& out =
        json.beginStringProperty("incumbentGlobalRepresentative");
    getFixedSlot(IncumbentGlobalRepresentative).dumpStringContent(out);
    json.endStringProperty();
  }
  if (!getFixedSlot(OptionalHostDefinedData).isUndefined()) {
    js::GenericPrinter& out =
        json.beginStringProperty("optionalHostDefinedData");
    getFixedSlot(OptionalHostDefinedData).dumpStringContent(out);
    json.endStringProperty();
  }

  json.beginInlineListProperty("flags");
  forEachReactionFlag(
      flags(), [&](const char* name) { json.value("%s", name); },
      [&](uint32_t value) { json.value("Unknown(%08x)", value); });
  json.endInlineList();

  if (isDefaultResolvingHandler()) {
    js::GenericPrinter& out = json.beginStringProperty("promiseToResolve");
    getFixedSlot(GeneratorOrPromiseToResolveOrAsyncFromSyncIterator)
        .dumpStringContent(out);
    json.endStringProperty();
  }

  if (isAsyncFunction()) {
    js::GenericPrinter& out = json.beginStringProperty("generator");
    getFixedSlot(GeneratorOrPromiseToResolveOrAsyncFromSyncIterator)
        .dumpStringContent(out);
    json.endStringProperty();
  }

  if (isAsyncGenerator()) {
    js::GenericPrinter& out = json.beginStringProperty("generator");
    getFixedSlot(GeneratorOrPromiseToResolveOrAsyncFromSyncIterator)
        .dumpStringContent(out);
    json.endStringProperty();
  }
}

void DumpReactions(js::JSONPrinter& json, const JS::Value& reactionsVal) {
  if (reactionsVal.isUndefined()) {
    return;
  }

  if (reactionsVal.isObject()) {
    JSObject* reactionsObj = &reactionsVal.toObject();
    if (IsProxy(reactionsObj)) {
      reactionsObj = UncheckedUnwrap(reactionsObj);
    }

    if (reactionsObj->is<PromiseReactionRecord>()) {
      json.beginObject();
      reactionsObj->as<PromiseReactionRecord>().dumpOwnFields(json);
      json.endObject();
      return;
    }

    if (reactionsObj->is<NativeObject>()) {
      NativeObject* reactionsList = &reactionsObj->as<NativeObject>();
      uint32_t len = reactionsList->getDenseInitializedLength();
      for (uint32_t i = 0; i < len; i++) {
        const JS::Value& reactionVal = reactionsList->getDenseElement(i);
        if (reactionVal.isObject()) {
          JSObject* reactionsObj = &reactionVal.toObject();
          if (IsProxy(reactionsObj)) {
            reactionsObj = UncheckedUnwrap(reactionsObj);
          }

          if (reactionsObj->is<PromiseReactionRecord>()) {
            json.beginObject();
            reactionsObj->as<PromiseReactionRecord>().dumpOwnFields(json);
            json.endObject();
            continue;
          }
        }

        js::GenericPrinter& out = json.beginString();
        out.put("Unknown(");
        reactionVal.dumpStringContent(out);
        out.put(")");
        json.endString();
      }
      return;
    }
  }

  js::GenericPrinter& out = json.beginString();
  out.put("Unknown(");
  reactionsVal.dumpStringContent(out);
  out.put(")");
  json.endString();
}

template <typename KnownF, typename UnknownF>
void ForEachPromiseFlag(uint32_t flags, KnownF known, UnknownF unknown) {
  for (uint32_t i = 1; i; i = i << 1) {
    if (!(flags & i)) {
      continue;
    }
    switch (flags & i) {
      case PROMISE_FLAG_RESOLVED:
        known("RESOLVED");
        break;
      case PROMISE_FLAG_FULFILLED:
        known("FULFILLED");
        break;
      case PROMISE_FLAG_HANDLED:
        known("HANDLED");
        break;
      case PROMISE_FLAG_DEFAULT_RESOLVING_FUNCTIONS:
        known("DEFAULT_RESOLVING_FUNCTIONS");
        break;
      case PROMISE_FLAG_DEFAULT_RESOLVING_FUNCTIONS_ALREADY_RESOLVED:
        known("DEFAULT_RESOLVING_FUNCTIONS_ALREADY_RESOLVED");
        break;
      case PROMISE_FLAG_ASYNC:
        known("ASYNC");
        break;
      case PROMISE_FLAG_REQUIRES_USER_INTERACTION_HANDLING:
        known("REQUIRES_USER_INTERACTION_HANDLING");
        break;
      case PROMISE_FLAG_HAD_USER_INTERACTION_UPON_CREATION:
        known("HAD_USER_INTERACTION_UPON_CREATION");
        break;
      default:
        unknown(i);
        break;
    }
  }
}

void PromiseObject::dumpOwnFields(js::JSONPrinter& json) const {
  json.beginInlineListProperty("flags");
  ForEachPromiseFlag(
      flags(), [&](const char* name) { json.value("%s", name); },
      [&](uint32_t value) { json.value("Unknown(%08x)", value); });
  json.endInlineList();

  if (state() == JS::PromiseState::Pending) {
    json.property("state", "pending");

    json.beginListProperty("reactions");
    DumpReactions(json, reactions());
    json.endList();
  } else if (state() == JS::PromiseState::Fulfilled) {
    json.property("state", "fulfilled");

    json.beginObjectProperty("value");
    value().dumpFields(json);
    json.endObject();
  } else if (state() == JS::PromiseState::Rejected) {
    json.property("state", "rejected");

    json.beginObjectProperty("reason");
    reason().dumpFields(json);
    json.endObject();
  }

  JS::Value debugInfo = getFixedSlotTyped(DEBUG_INFO_SLOT);
  if (debugInfo.isNumber()) {
    json.formatProperty("id", "%lf", debugInfo.toNumber());
  } else if (debugInfo.isObject() &&
             debugInfo.toObject().is<PromiseDebugInfo>()) {
    debugInfo.toObject().as<PromiseDebugInfo>().dumpOwnFields(json);
  }
}

void PromiseObject::dumpOwnStringContent(js::GenericPrinter& out) const {}
#endif

// We can skip `await` with an already resolved value only if the current frame
// is the topmost JS frame and the current job is the last job in the job queue.
// This guarantees that any new job enqueued in the current turn will be
// executed immediately after the current job.
//
// The predicate is split across the JSOp::CanSkipAwait op and this function:
//
//  - The op calls js::CanSkipAwait iff the stack frame is the first frame of
//    its activation. Checking this in JIT code is much cheaper than checking it
//    here in C++.
//
//  - js::CanSkipAwait then requires the activation to have been entered to
//    resume a suspended async function and to be the context's only activation.
//
// This lets us avoid an expensive frame iteration in non-debug builds.
[[nodiscard]] bool js::CanSkipAwait(JSContext* cx, HandleValue val,
                                    bool* canSkip) {
  if (!cx->canSkipEnqueuingJobs) {
    *canSkip = false;
    return true;
  }

  // The op already established the stack frame is the first frame of its
  // activation. Ensure the activation is a resume and the sole activation.
  Activation* act = cx->activation();
  if (!act->enteredForGeneratorResume() || act->prev()) {
    *canSkip = false;
    return true;
  }

  // In debug builds, assert our JS caller is the only frame on the stack.
#ifdef DEBUG
  FrameIter iter(cx);
  MOZ_ASSERT(!iter.done());
  MOZ_ASSERT(iter.isFunctionFrame(), "CanSkipAwait is only used for functions");
  MOZ_ASSERT(iter.calleeTemplate()->isAsync());
  ++iter;
  MOZ_ASSERT(iter.done());
#endif

  // Primitive values cannot be 'thenables', so we can trivially skip the
  // await operation.
  if (!val.isObject()) {
    *canSkip = true;
    return true;
  }

  JSObject* obj = &val.toObject();
  if (!obj->is<PromiseObject>()) {
    // If the awaited value is a non-promise object, we can still skip the await
    // if there's no user code called on this path; that means no
    // - getter then property. This is avoided by using GetPropertyPure
    // - callable then property. This is checked by IsCallable.
    Value thenVal;
    if (!GetPropertyPure(cx, obj, NameToId(cx->names().then), &thenVal)) {
      // Couldn't check thenable value!
      *canSkip = false;
      return true;
    }

    // If we got a then value, still can skip if it's
    // not callable.
    *canSkip = !IsCallable(thenVal);
    return true;
  }

  PromiseObject* promise = &obj->as<PromiseObject>();

  if (promise->state() == JS::PromiseState::Pending) {
    *canSkip = false;
    return true;
  }

  if (!IsPromiseWithDefaultProperties(promise, cx)) {
    *canSkip = false;
    return true;
  }

  if (promise->state() == JS::PromiseState::Rejected) {
    // We don't optimize rejected Promises for now.
    *canSkip = false;
    return true;
  }

  *canSkip = true;
  return true;
}

[[nodiscard]] bool js::ExtractAwaitValue(JSContext* cx, HandleValue val,
                                         MutableHandleValue resolved) {
// Ensure all callers of this are jumping past the
// extract if it's not possible to extract.
#ifdef DEBUG
  bool canSkip;
  if (!CanSkipAwait(cx, val, &canSkip)) {
    return false;
  }
  MOZ_ASSERT(canSkip == true);
#endif

  // Primitive values cannot be 'thenables', so we can trivially skip the
  // await operation.
  if (!val.isObject()) {
    resolved.set(val);
    return true;
  }

  JSObject* obj = &val.toObject();
  if (obj->is<PromiseObject>()) {
    PromiseObject* promise = &obj->as<PromiseObject>();
    resolved.set(promise->value());
  } else {
    resolved.setObject(*obj);
  }

  return true;
}

JS_PUBLIC_API bool JS::RunJSMicroTask(JSContext* cx,
                                      Handle<JS::JSMicroTask*> entry) {
#ifdef DEBUG
  JSObject* global = JS::GetExecutionGlobalFromJSMicroTask(entry);
  MOZ_ASSERT_IF(global, global == cx->global());
#endif

  RootedObject task(cx, entry);
  RootedObject unwrappedTask(cx, UncheckedUnwrap(entry));
  if (JS_IsDeadWrapper(unwrappedTask)) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, JSMSG_DEAD_OBJECT);
    return false;
  }

  if (unwrappedTask->is<PromiseReactionRecord>()) {
    // Note: We don't store a callback for promise reaction records because they
    // always call back into PromiseReactionJob.
    //
    // Note: We pass the (maybe)wrapped task here since PromiseReactionJob will
    // decide what realm to be in based on the wrapper if it exists.
    return PromiseReactionJob(cx, task);
  }

  if (unwrappedTask->is<ThenableJob>()) {
    ThenableJob* job = &unwrappedTask->as<ThenableJob>();
    ThenableJob::TargetFunction target = job->targetFunction();

    // MG:XXX: Note: Because we don't care about the result of these values
    // after the call, do these really have to be rooted (I don't think so?)
    RootedTuple<JSObject*, Value, JSObject*, JSObject*> roots(cx);
    RootedField<JSObject*, 0> promise(roots, job->promise());
    RootedField<Value, 1> thenable(roots, job->thenable());

    switch (target) {
      case ThenableJob::PromiseResolveThenableJob: {
        // MG:XXX: Unify naming: is it `then` or `handler` make up your mind.
        RootedField<JSObject*, 3> then(roots, job->then());
        return PerformPromiseResolveThenable(cx, promise, thenable, then);
      }
      case ThenableJob::PromiseResolveBuiltinThenableJob: {
        RootedField<JSObject*, 2> thenableObj(roots,
                                              &job->thenable().toObject());
        return PromiseResolveBuiltinThenableJob(cx, promise, thenableObj);
      }
#ifdef NIGHTLY_BUILD
      case ThenableJob::DeferredResolveJob: {
        MOZ_ASSERT(promise->is<PromiseObject>());
        Rooted<PromiseObject*> promiseRooted(cx, &promise->as<PromiseObject>());
        if (promiseRooted->state() != JS::PromiseState::Pending) {
          return true;
        }
        return PerformPromiseResolution(cx, promiseRooted, thenable);
      }
#endif  // NIGHTLY_BUILD
    }
    MOZ_CRASH("Corrupted Target Function");
    return false;
  }

  MOZ_CRASH("Unknown Job type");
  return false;
}

template <>
inline bool JSObject::is<MicroTaskEntry>() const {
  return is<ThenableJob>() || is<PromiseReactionRecord>();
}

JS_PUBLIC_API bool JS::MaybeGetAllocationSiteFromJSMicroTask(
    JS::JSMicroTask* entry, MutableHandleObject out) {
  JSObject* task = UncheckedUnwrap(entry);
  if (JS_IsDeadWrapper(task)) {
    return false;
  };

  MOZ_ASSERT(task->is<MicroTaskEntry>());
  JSObject* maybeWrappedStack = task->as<MicroTaskEntry>().allocationStack();

  if (!maybeWrappedStack) {
    out.set(nullptr);
    return true;
  }

  if (JS_IsDeadWrapper(maybeWrappedStack)) {
    return false;
  }

  JSObject* unwrapped = UncheckedUnwrap(maybeWrappedStack);
  MOZ_ASSERT(unwrapped->is<SavedFrame>());
  out.set(unwrapped);
  return true;
}

JS_PUBLIC_API bool JS::MaybeGetHostDefinedDataFromJSMicroTask(
    JS::JSMicroTask* entry, MutableHandleObject incumbentGlobal,
    MutableHandleObject optionalHostDefinedData) {
  incumbentGlobal.set(nullptr);
  optionalHostDefinedData.set(nullptr);
  JSObject* task = CheckedUnwrapStatic(entry);
  if (!task) {
    return false;
  }
  if (JS_IsDeadWrapper(task)) {
    return false;
  }

  MOZ_ASSERT(task->is<MicroTaskEntry>());
  JSObject* maybeIncumbentGlobalRepresentative =
      task->as<MicroTaskEntry>()
          .getIncumbentGlobalRepresentative()
          .toObjectOrNull();
  JSObject* maybeOptionalHostDefinedData =
      task->as<MicroTaskEntry>().getOptionalHostDefinedData().toObjectOrNull();

  if (!maybeIncumbentGlobalRepresentative) {
    // If we don't have an incumbent global we don't process host defined data.
    MOZ_RELEASE_ASSERT(!maybeOptionalHostDefinedData);
    return true;
  }

  JSObject* unwrappedIncumbentGlobalRepresentative =
      CheckedUnwrapStatic(maybeIncumbentGlobalRepresentative);
  if (!unwrappedIncumbentGlobalRepresentative) {
    return false;
  }
  if (JS_IsDeadWrapper(unwrappedIncumbentGlobalRepresentative)) {
    return false;
  }

  incumbentGlobal.set(&unwrappedIncumbentGlobalRepresentative->nonCCWGlobal());
  optionalHostDefinedData.set(maybeOptionalHostDefinedData);
  return true;
}

JS_PUBLIC_API JSObject* JS::GetExecutionGlobalFromJSMicroTask(
    JS::JSMicroTask* entry) {
  JSObject* unwrapped = UncheckedUnwrap(entry);
  if (JS_IsDeadWrapper(unwrapped)) {
    return nullptr;
  }

  if (unwrapped->is<PromiseReactionRecord>()) {
    // Use the stored equeue representative (which may need to be unwrapped)
    JSObject* enqueueGlobalRepresentative =
        unwrapped->as<PromiseReactionRecord>().enqueueGlobalRepresentative();
    JSObject* unwrappedRepresentative =
        UncheckedUnwrap(enqueueGlobalRepresentative);

    if (JS_IsDeadWrapper(unwrappedRepresentative)) {
      return nullptr;
    }

    return &unwrappedRepresentative->nonCCWGlobal();
  }

  // Thenable jobs are allocated in the right realm+global and so we
  // can just use nonCCWGlobal;
  if (unwrapped->is<ThenableJob>()) {
    return &unwrapped->nonCCWGlobal();
  }

  MOZ_CRASH("Somehow we lost the execution global");
}

JS_PUBLIC_API JSObject* JS::MaybeGetPromiseFromJSMicroTask(
    JS::JSMicroTask* entry) {
  JSObject* unwrapped = UncheckedUnwrap(entry);
  if (JS_IsDeadWrapper(unwrapped)) {
    return nullptr;
  }

  if (unwrapped->is<MicroTaskEntry>()) {
    return unwrapped->as<MicroTaskEntry>().promise();
  }
  return nullptr;
}

JS_PUBLIC_API bool JS::GetFlowIdFromJSMicroTask(JS::JSMicroTask* entry,
                                                uint64_t* uid) {
  // We want to make sure we get the flow id from the target object
  // not the wrapper.
  JSObject* unwrapped = UncheckedUnwrap(entry);
  if (JS_IsDeadWrapper(unwrapped)) {
    return false;
  }

  MOZ_ASSERT(unwrapped->is<MicroTaskEntry>(), "Only use on JSMicroTasks");

  *uid = js::gc::GetUniqueIdInfallible(unwrapped);
  return true;
}

JS_PUBLIC_API JS::JSMicroTask* JS::ToUnwrappedJSMicroTask(
    const JS::GenericMicroTask& genericMicroTask) {
  if (!genericMicroTask.isObject()) {
    return nullptr;
  }

  JSObject* unwrapped = UncheckedUnwrap(&genericMicroTask.toObject());

  // On the off chance someone hands us a dead wrapper.
  if (JS_IsDeadWrapper(unwrapped)) {
    return nullptr;
  }
  if (!unwrapped->is<MicroTaskEntry>()) {
    return nullptr;
  }

  return unwrapped;
}

JS_PUBLIC_API JS::JSMicroTask* JS::ToMaybeWrappedJSMicroTask(
    const JS::GenericMicroTask& genericMicroTask) {
  if (!genericMicroTask.isObject()) {
    return nullptr;
  }

  return &genericMicroTask.toObject();
}

JS_PUBLIC_API bool JS::IsJSMicroTask(const JS::GenericMicroTask& hv) {
  return JS::ToUnwrappedJSMicroTask(hv) != nullptr;
}

JS::AutoDebuggerJobQueueInterruption::AutoDebuggerJobQueueInterruption()
    : cx(nullptr) {}

JS::AutoDebuggerJobQueueInterruption::~AutoDebuggerJobQueueInterruption() {
#ifdef DEBUG
  if (initialized() && !cx->jobQueue->isDrainingStopped()) {
    MOZ_ASSERT(!JS::HasRegularMicroTasks(cx));
  }
#endif
}

bool JS::AutoDebuggerJobQueueInterruption::init(JSContext* cx) {
  MOZ_ASSERT(cx->jobQueue);
  this->cx = cx;
  saved = cx->jobQueue->saveJobQueue(cx);
  return !!saved;
}

void JS::AutoDebuggerJobQueueInterruption::runJobs() {
  JS::AutoSaveExceptionState ases(cx);
  cx->jobQueue->runJobs(cx);
}

const JSJitInfo promise_then_info = {
    {(JSJitGetterOp)Promise_then_noRetVal},
    {0}, /* unused */
    {0}, /* unused */
    JSJitInfo::IgnoresReturnValueNative,
    JSJitInfo::AliasEverything,
    JSVAL_TYPE_UNDEFINED,
};

const JSJitInfo promise_catch_info = {
    {(JSJitGetterOp)Promise_catch_noRetVal},
    {0}, /* unused */
    {0}, /* unused */
    JSJitInfo::IgnoresReturnValueNative,
    JSJitInfo::AliasEverything,
    JSVAL_TYPE_UNDEFINED,
};

static const JSFunctionSpec promise_methods[] = {
    JS_FNINFO("then", js::Promise_then, &promise_then_info, 2, 0),
    JS_FNINFO("catch", Promise_catch, &promise_catch_info, 1, 0),
    JS_SELF_HOSTED_FN("finally", "Promise_finally", 1, 0),
    JS_FS_END,
};

static const JSPropertySpec promise_properties[] = {
    JS_STRING_SYM_PS(toStringTag, "Promise", JSPROP_READONLY),
    JS_PS_END,
};

static const JSFunctionSpec promise_static_methods[] = {
    JS_FN("all", Promise_static_all, 1, 0),
    JS_FN("allSettled", Promise_static_allSettled, 1, 0),
    JS_FN("allKeyed", Promise_static_allKeyed, 1, 0),
    JS_FN("allSettledKeyed", Promise_static_allSettledKeyed, 1, 0),
    JS_FN("any", Promise_static_any, 1, 0),
    JS_FN("race", Promise_static_race, 1, 0),
    JS_FN("reject", Promise_reject, 1, 0),
    JS_FN("resolve", js::Promise_static_resolve, 1, 0),
    JS_FN("withResolvers", Promise_static_withResolvers, 0, 0),
    JS_FN("try", Promise_static_try, 1, 0),
    JS_FS_END,
};

static const JSPropertySpec promise_static_properties[] = {
    JS_SYM_GET(species, js::Promise_static_species, 0),
    JS_PS_END,
};

static const ClassSpec PromiseObjectClassSpec = {
    GenericCreateConstructor<PromiseConstructor, 1, gc::AllocKind::FUNCTION>,
    GenericCreatePrototype<PromiseObject>,
    promise_static_methods,
    promise_static_properties,
    promise_methods,
    promise_properties,
    GenericFinishInit<WhichHasRealmFuseProperty::ProtoAndCtor>,
};

const JSClass PromiseObject::class_ = {
    "Promise",
    JSCLASS_HAS_RESERVED_SLOTS(RESERVED_SLOTS) |
        JSCLASS_HAS_CACHED_PROTO(JSProto_Promise) |
        JSCLASS_HAS_XRAYED_CONSTRUCTOR,
    JS_NULL_CLASS_OPS,
    &PromiseObjectClassSpec,
};

const JSClass PromiseObject::protoClass_ = {
    "Promise.prototype",
    JSCLASS_HAS_CACHED_PROTO(JSProto_Promise),
    JS_NULL_CLASS_OPS,
    &PromiseObjectClassSpec,
};
