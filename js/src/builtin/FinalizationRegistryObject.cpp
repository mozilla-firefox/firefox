/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Implementation of JS FinalizationRegistry objects.

#include "builtin/FinalizationRegistryObject.h"

#include "mozilla/ScopeExit.h"

#include "jsapi.h"

#include "js/friend/ErrorMessages.h"  // js::GetErrorMessage, JSMSG_*
#include "vm/GlobalObject.h"
#include "vm/Interpreter.h"

#include "gc/GCContext-inl.h"
#include "gc/WeakMap-inl.h"
#include "vm/JSObject-inl.h"
#include "vm/NativeObject-inl.h"

using namespace js;

///////////////////////////////////////////////////////////////////////////
// FinalizationRecordObject

const JSClassOps FinalizationRecordObject::classOps_ = {
    .finalize = finalize,
};

const JSClass FinalizationRecordObject::class_ = {
    "FinalizationRecord",
    JSCLASS_HAS_RESERVED_SLOTS(SLOT_COUNT) | JSCLASS_FOREGROUND_FINALIZE,
    &classOps_,
    JS_NULL_CLASS_SPEC,
    &classExtension_,
};

/* static */
FinalizationRecordObject* FinalizationRecordObject::create(
    JSContext* cx, HandleFinalizationQueueObject queue, HandleValue heldValue) {
  MOZ_ASSERT(queue);

  auto record = NewObjectWithGivenProto<FinalizationRecordObject>(cx, nullptr);
  if (!record) {
    return nullptr;
  }

  MOZ_ASSERT(queue->compartment() == record->compartment());

  record->initReservedSlotTyped(QUEUE_SLOT, ObjectValue(*queue));
  record->initReservedSlot(HELD_VALUE_SLOT, heldValue);

  return record;
}

/* static */
void FinalizationRecordObject::finalize(JS::GCContext* gcx, JSObject* obj) {
  auto* record = &obj->as<FinalizationRecordObject>();
  MOZ_ASSERT_IF(!record->isInRecordMap(), !record->isInList());
  record->unlink();
}

FinalizationQueueObject* FinalizationRecordObject::queue() const {
  Value value = getReservedSlotTyped(QUEUE_SLOT);
  if (value.isUndefined()) {
    return nullptr;
  }
  return &value.toObject().as<FinalizationQueueObject>();
}

Value FinalizationRecordObject::heldValue() const {
  return getReservedSlot(HELD_VALUE_SLOT);
}

bool FinalizationRecordObject::isRegistered() const {
  MOZ_ASSERT_IF(!queue(), heldValue().isUndefined());
  return queue();
}

#ifdef DEBUG

void FinalizationRecordObject::setState(State state) {
  Value value;
  if (state != Unknown) {
    value = Int32Value(int32_t(state));
  }
  setReservedSlotTyped(DEBUG_STATE_SLOT, value);
}

FinalizationRecordObject::State FinalizationRecordObject::getState() const {
  Value value = getReservedSlotTyped(DEBUG_STATE_SLOT);
  if (value.isUndefined()) {
    return Unknown;
  }

  State state = State(value.toInt32());
  MOZ_ASSERT(state == InRecordMap || state == InQueue);
  return state;
}

#endif

void FinalizationRecordObject::setInRecordMap(bool newValue) {
#ifdef DEBUG
  State newState = newValue ? InRecordMap : Unknown;
  MOZ_ASSERT(getState() != newState);
  setState(newState);
#endif
}

void FinalizationRecordObject::setInQueue(bool newValue) {
#ifdef DEBUG
  State newState = newValue ? InQueue : Unknown;
  MOZ_ASSERT(getState() != newState);
  setState(newState);
#endif
}

void FinalizationRecordObject::clear() {
  MOZ_ASSERT(queue());
  setReservedSlotTyped(QUEUE_SLOT, UndefinedValue());
  setReservedSlot(HELD_VALUE_SLOT, UndefinedValue());
  MOZ_ASSERT(!isRegistered());
}

///////////////////////////////////////////////////////////////////////////
// FinalizationRegistryObject

// Bug 1600300: FinalizationRegistryObject is foreground finalized so that
// HeapPtr destructors never see referents with released arenas. When this is
// fixed we may be able to make this background finalized again.
const JSClass FinalizationRegistryObject::class_ = {
    "FinalizationRegistry",
    JSCLASS_HAS_CACHED_PROTO(JSProto_FinalizationRegistry) |
        JSCLASS_HAS_RESERVED_SLOTS(SLOT_COUNT) | JSCLASS_FOREGROUND_FINALIZE,
    &classOps_,
    &classSpec_,
};

const JSClass FinalizationRegistryObject::protoClass_ = {
    "FinalizationRegistry.prototype",
    JSCLASS_HAS_CACHED_PROTO(JSProto_FinalizationRegistry),
    JS_NULL_CLASS_OPS,
    &classSpec_,
};

const JSClassOps FinalizationRegistryObject::classOps_ = {
    .finalize = FinalizationRegistryObject::finalize,
    .trace = FinalizationRegistryObject::trace,
};

const ClassSpec FinalizationRegistryObject::classSpec_ = {
    GenericCreateConstructor<construct, 1, gc::AllocKind::FUNCTION>,
    GenericCreatePrototype<FinalizationRegistryObject>,
    nullptr,
    nullptr,
    methods_,
    properties_,
};

const JSFunctionSpec FinalizationRegistryObject::methods_[] = {
    JS_FN("register", register_, 2, 0),
    JS_FN("unregister", unregister, 1, 0),
    JS_FN("cleanupSome", cleanupSome, 0, 0),
    JS_FS_END,
};

const JSPropertySpec FinalizationRegistryObject::properties_[] = {
    JS_STRING_SYM_PS(toStringTag, "FinalizationRegistry", JSPROP_READONLY),
    JS_PS_END,
};

/* static */
bool FinalizationRegistryObject::construct(JSContext* cx, unsigned argc,
                                           Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  if (!ThrowIfNotConstructing(cx, args, "FinalizationRegistry")) {
    return false;
  }

  RootedObject cleanupCallback(
      cx, ValueToCallable(cx, args.get(0), 1, NO_CONSTRUCT));
  if (!cleanupCallback) {
    return false;
  }

  RootedObject proto(cx);
  if (!GetPrototypeFromBuiltinConstructor(
          cx, args, JSProto_FinalizationRegistry, &proto)) {
    return false;
  }

  Rooted<UniquePtr<FinalizationRecordVector>> records(
      cx, cx->make_unique<FinalizationRecordVector>(cx->zone()));
  if (!records) {
    return false;
  }

  Rooted<UniquePtr<RegistrationsMap>> registrations(
      cx, cx->make_unique<RegistrationsMap>(cx));
  if (!registrations) {
    return false;
  }

  RootedFinalizationQueueObject queue(
      cx, FinalizationQueueObject::create(cx, cleanupCallback));
  if (!queue) {
    return false;
  }

  RootedFinalizationRegistryObject registry(
      cx, NewObjectWithClassProto<FinalizationRegistryObject>(cx, proto));
  if (!registry) {
    return false;
  }

  registry->initReservedSlotTyped(QUEUE_SLOT, ObjectValue(*queue));
  InitReservedSlot(registry, RECORDS_WITHOUT_TOKEN_SLOT.index(),
                   records.release(), MemoryUse::FinalizationRecordVector);
  InitReservedSlot(registry, REGISTRATIONS_SLOT.index(),
                   registrations.release(),
                   MemoryUse::FinalizationRegistryRegistrations);

  if (!cx->runtime()->gc.addFinalizationRegistry(cx, registry)) {
    return false;
  }

  queue->setHasRegistry(true);

  args.rval().setObject(*registry);
  return true;
}

/* static */
void FinalizationRegistryObject::trace(JSTracer* trc, JSObject* obj) {
  // Trace finalization records.
  auto* registry = &obj->as<FinalizationRegistryObject>();
  if (FinalizationRecordVector* records = registry->recordsWithoutToken()) {
    records->trace(trc);
  }

  // Trace the records referred to by the registrations map, but not its keys
  // which are weakly held.
  if (RegistrationsMap* registrations = registry->registrations()) {
    for (auto iter = registrations->iter(); !iter.done(); iter.next()) {
      iter.get().value().trace(trc);
    }
  }
}

void FinalizationRegistryObject::traceWeak(JSTracer* trc,
                                           bool* hasSymbolRegistrations) {
  // Trace and update the contents of the registrations map's keys, which are
  // weakly held. Remove any old records that have been queued or cleaned up.
  MOZ_ASSERT(recordsWithoutToken());
  MOZ_ASSERT(registrations());
  MOZ_ASSERT(hasSymbolRegistrations);

  recordsWithoutToken()->mutableEraseIf(
      [](FinalizationRecordObject* record) { return !record->isRegistered(); });

  for (auto iter = registrations()->modIter(); !iter.done(); iter.next()) {
    auto result = TraceWeakEdge(trc, &iter.getMutable().mutableKey(),
                                "FinalizationRegistry unregister token");
    if (result.isDead()) {
      // The unregister token has died and can no longer be used to unregister
      // registrations. However those registrations remain valid.
      AutoEnterOOMUnsafeRegion oomUnsafe;
      if (!recordsWithoutToken()->appendAll(std::move(iter.get().value()))) {
        oomUnsafe.crash("FinalizationRegistryObject::traceWeak");
      }
      iter.remove();
    } else {
      if (result.finalTarget().isSymbol()) {
        *hasSymbolRegistrations = true;
      }

      FinalizationRecordVector& records = iter.get().value();
      records.mutableEraseIf([](FinalizationRecordObject* record) {
        return !record->isRegistered();
      });

      if (records.empty()) {
        iter.remove();
      }
    }
  }

  registrations()->compact();
}

/* static */
void FinalizationRegistryObject::finalize(JS::GCContext* gcx, JSObject* obj) {
  auto registry = &obj->as<FinalizationRegistryObject>();

  // The queue's flag should have been updated by
  // GCRuntime::sweepFinalizationRegistries.
  MOZ_ASSERT_IF(registry->queue(), !registry->queue()->hasRegistry());

  gcx->delete_(obj, registry->recordsWithoutToken(),
               MemoryUse::FinalizationRecordVector);
  gcx->delete_(obj, registry->registrations(),
               MemoryUse::FinalizationRegistryRegistrations);
}

FinalizationRecordVector* FinalizationRegistryObject::recordsWithoutToken()
    const {
  Value value = getReservedSlotTyped(RECORDS_WITHOUT_TOKEN_SLOT);
  if (value.isUndefined()) {
    return nullptr;
  }
  return static_cast<FinalizationRecordVector*>(value.toPrivate());
}

FinalizationQueueObject* FinalizationRegistryObject::queue() const {
  Value value = getReservedSlotTyped(QUEUE_SLOT);
  if (value.isUndefined()) {
    return nullptr;
  }
  return &value.toObject().as<FinalizationQueueObject>();
}

FinalizationRegistryObject::RegistrationsMap*
FinalizationRegistryObject::registrations() const {
  Value value = getReservedSlotTyped(REGISTRATIONS_SLOT);
  if (value.isUndefined()) {
    return nullptr;
  }
  return static_cast<RegistrationsMap*>(value.toPrivate());
}

// FinalizationRegistry.prototype.register(target, heldValue [, unregisterToken
// ])
// https://tc39.es/ecma262/#sec-finalization-registry.prototype.register
/* static */
bool FinalizationRegistryObject::register_(JSContext* cx, unsigned argc,
                                           Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // 1. Let finalizationRegistry be the this value.
  // 2. Perform ? RequireInternalSlot(finalizationRegistry, [[Cells]]).
  if (!args.thisv().isObject() ||
      !args.thisv().toObject().is<FinalizationRegistryObject>()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_NOT_A_FINALIZATION_REGISTRY,
                              "Receiver of FinalizationRegistry.register call");
    return false;
  }

  RootedFinalizationRegistryObject registry(
      cx, &args.thisv().toObject().as<FinalizationRegistryObject>());

  // 3. If CanBeHeldWeakly(target) is false, throw a TypeError exception.
  RootedValue target(cx, args.get(0));
  if (!CanBeHeldWeakly(target)) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_BAD_FINALIZATION_REGISTRY_TARGET);
    return false;
  }

  // 4. If SameValue(target, heldValue) is true, throw a TypeError exception.
  HandleValue heldValue = args.get(1);
  if (heldValue == target) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_BAD_HELD_VALUE);
    return false;
  }

  // 5. If CanBeHeldWeakly(unregisterToken) is false, then:
  //    a. If unregisterToken is not undefined, throw a TypeError exception.
  //    b. Set unregisterToken to empty.
  RootedValue unregisterToken(cx, args.get(2));
  if (!CanBeHeldWeakly(unregisterToken) && !unregisterToken.isUndefined()) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_BAD_UNREGISTER_TOKEN,
                              "FinalizationRegistry.register");
    return false;
  }

  bool isPermanent = false;
  if (target.isObject()) {
    // Fully unwrap the target to register it with the GC.
    RootedObject object(cx, CheckedUnwrapDynamic(&target.toObject(), cx));
    if (!object) {
      ReportAccessDenied(cx);
      return false;
    }

    target = ObjectValue(*object);

    // If the target is a DOM wrapper, preserve it.
    MaybePreserveDOMWrapper(cx, object);
  } else {
    JS::Symbol* symbol = target.toSymbol();
    isPermanent = symbol->isPermanentAndMayBeShared();
  }

  // Create the finalization record representing this target and heldValue.
  Rooted<FinalizationQueueObject*> queue(cx, registry->queue());
  Rooted<FinalizationRecordObject*> record(
      cx, FinalizationRecordObject::create(cx, queue, heldValue));
  if (!record) {
    return false;
  }

  // We shouldn't GC after this point because removeRegistrationOnError assumes
  // the record is still the last element.
  JS::AutoAssertNoGC nogc(cx);
  if (!addRegistration(cx, registry, unregisterToken, record)) {
    return false;
  }
  auto registrationGuard = mozilla::MakeScopeExit(
      [&] { removeRegistrationOnError(registry, unregisterToken, record); });

  // Register the record with the target, unless the target is permanent.
  // (See the note following https://tc39.es/ecma262/#sec-canbeheldweakly)
  if (!isPermanent) {
    gc::GCRuntime* gc = &cx->runtime()->gc;
    if (!gc->registerWithFinalizationRegistry(cx, target, record)) {
      return false;
    }
  }

  // 8. Return undefined.
  registrationGuard.release();
  args.rval().setUndefined();
  return true;
}

/* static */
bool FinalizationRegistryObject::addRegistration(
    JSContext* cx, HandleFinalizationRegistryObject registry,
    HandleValue unregisterToken, HandleFinalizationRecordObject record) {
  // Add the record to the list of records associated with this unregister
  // token, or add it to the main list.

  MOZ_ASSERT(registry->registrations());
  MOZ_ASSERT(unregisterToken.isUndefined() || CanBeHeldWeakly(unregisterToken));

  if (unregisterToken.isUndefined()) {
    if (!registry->recordsWithoutToken()->append(record)) {
      ReportOutOfMemory(cx);
      return false;
    }
    return true;
  }

  auto& map = *registry->registrations();
  auto ptr = map.lookupForAdd(unregisterToken.get());
  if (!ptr.found() &&
      !map.add(ptr, unregisterToken, FinalizationRecordVector(cx->zone()))) {
    ReportOutOfMemory(cx);
    return false;
  }

  if (!ptr->value().append(record)) {
    ReportOutOfMemory(cx);
    return false;
  }

  if (unregisterToken.isSymbol()) {
    cx->zone()->setGCFinalizationRegistriesMayHaveSymbolRegistrations();
  }

  return true;
}

/* static */
void FinalizationRegistryObject::removeRegistrationOnError(
    HandleFinalizationRegistryObject registry, HandleValue unregisterToken,
    HandleFinalizationRecordObject record) {
  // Remove a registration if something went wrong before we added it to the
  // target zone's map. Note that this can't remove a registration after that
  // point.

  MOZ_ASSERT(registry->registrations());
  MOZ_ASSERT(unregisterToken.isUndefined() || CanBeHeldWeakly(unregisterToken));
  JS::AutoAssertNoGC nogc;

  if (unregisterToken.isUndefined()) {
    MOZ_RELEASE_ASSERT(registry->recordsWithoutToken()->back() == record);
    registry->recordsWithoutToken()->popBack();
    return;
  }

  auto ptr = registry->registrations()->lookup(unregisterToken);
  MOZ_RELEASE_ASSERT(ptr.found());
  FinalizationRecordVector& records = ptr->value();
  MOZ_RELEASE_ASSERT(records.back() == record);
  records.popBack();
  if (records.empty()) {
    registry->registrations()->remove(ptr);
  }
}

// FinalizationRegistry.prototype.unregister ( unregisterToken )
// https://tc39.es/proposal-weakrefs/#sec-finalization-registry.prototype.unregister
/* static */
bool FinalizationRegistryObject::unregister(JSContext* cx, unsigned argc,
                                            Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // 1. Let finalizationRegistry be the this value.
  // 2. If Type(finalizationRegistry) is not Object, throw a TypeError
  //    exception.
  // 3. If finalizationRegistry does not have a [[Cells]] internal slot, throw a
  //    TypeError exception.
  if (!args.thisv().isObject() ||
      !args.thisv().toObject().is<FinalizationRegistryObject>()) {
    JS_ReportErrorNumberASCII(
        cx, GetErrorMessage, nullptr, JSMSG_NOT_A_FINALIZATION_REGISTRY,
        "Receiver of FinalizationRegistry.unregister call");
    return false;
  }

  RootedFinalizationRegistryObject registry(
      cx, &args.thisv().toObject().as<FinalizationRegistryObject>());

  // 4. If Type(unregisterToken) is not Object, throw a TypeError exception.
  RootedValue unregisterToken(cx, args.get(0));
  if (!CanBeHeldWeakly(unregisterToken)) {
    JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                              JSMSG_BAD_UNREGISTER_TOKEN,
                              "FinalizationRegistry.unregister");
    return false;
  }

  // 5. Let removed be false.
  bool removed = false;

  // 6. For each Record { [[Target]], [[HeldValue]], [[UnregisterToken]] } cell
  //    that is an element of finalizationRegistry.[[Cells]], do
  //    a. If SameValue(cell.[[UnregisterToken]], unregisterToken) is true, then
  //       i. Remove cell from finalizationRegistry.[[Cells]].
  //       ii. Set removed to true.

  RegistrationsMap* map = registry->registrations();
  auto ptr = map->lookup(unregisterToken);
  if (ptr) {
    FinalizationRecordVector& records = ptr->value();
    MOZ_ASSERT(!records.empty());
    for (FinalizationRecordObject* record : records) {
      if (unregisterRecord(record)) {
        removed = true;
      }
    }
    map->remove(unregisterToken);
  }

  // 7. Return removed.
  args.rval().setBoolean(removed);
  return true;
}

/* static */
bool FinalizationRegistryObject::unregisterRecord(
    FinalizationRecordObject* record) {
  if (!record->isRegistered()) {
    MOZ_ASSERT(!record->isInList());
    return false;
  }

  // Remove record from the target list if present.
  record->unlink();

  // Clear the fields of this record, marking it as unregistered. It will be
  // removed from relevant data structures when they are next swept.
  record->clear();
  MOZ_ASSERT(!record->isRegistered());

  return true;
}

// FinalizationRegistry.prototype.cleanupSome ( [ callback ] )
// https://tc39.es/proposal-weakrefs/#sec-finalization-registry.prototype.cleanupSome
bool FinalizationRegistryObject::cleanupSome(JSContext* cx, unsigned argc,
                                             Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  // 1. Let finalizationRegistry be the this value.
  // 2. Perform ? RequireInternalSlot(finalizationRegistry, [[Cells]]).
  if (!args.thisv().isObject() ||
      !args.thisv().toObject().is<FinalizationRegistryObject>()) {
    JS_ReportErrorNumberASCII(
        cx, GetErrorMessage, nullptr, JSMSG_NOT_A_FINALIZATION_REGISTRY,
        "Receiver of FinalizationRegistry.cleanupSome call");
    return false;
  }

  RootedFinalizationRegistryObject registry(
      cx, &args.thisv().toObject().as<FinalizationRegistryObject>());

  // 3. If callback is not undefined and IsCallable(callback) is false, throw a
  //    TypeError exception.
  RootedObject cleanupCallback(cx);
  if (!args.get(0).isUndefined()) {
    cleanupCallback = ValueToCallable(cx, args.get(0), -1, NO_CONSTRUCT);
    if (!cleanupCallback) {
      return false;
    }
  }

  RootedFinalizationQueueObject queue(cx, registry->queue());
  if (!FinalizationQueueObject::cleanupQueuedRecords(cx, queue,
                                                     cleanupCallback)) {
    return false;
  }

  args.rval().setUndefined();
  return true;
}

///////////////////////////////////////////////////////////////////////////
// FinalizationQueueObject

// Bug 1600300: FinalizationQueueObject is foreground finalized so that
// HeapPtr destructors never see referents with released arenas. When this is
// fixed we may be able to make this background finalized again.
const JSClass FinalizationQueueObject::class_ = {
    "FinalizationQueue",
    JSCLASS_HAS_RESERVED_SLOTS(SLOT_COUNT) | JSCLASS_FOREGROUND_FINALIZE,
    &classOps_,
};

const JSClassOps FinalizationQueueObject::classOps_ = {
    .finalize = FinalizationQueueObject::finalize,
    .trace = FinalizationQueueObject::trace,
};

/* static */
FinalizationQueueObject* FinalizationQueueObject::create(
    JSContext* cx, HandleObject cleanupCallback) {
  MOZ_ASSERT(cleanupCallback);

  Rooted<UniquePtr<QueuedRecordVector>> recordsToBeCleanedUp(
      cx, cx->make_unique<QueuedRecordVector>());
  if (!recordsToBeCleanedUp) {
    return nullptr;
  }

  Handle<PropertyName*> funName = cx->names().empty_;
  RootedFunction doCleanupFunction(
      cx, NewNativeFunction(cx, doCleanup, 0, funName,
                            gc::AllocKind::FUNCTION_EXTENDED));
  if (!doCleanupFunction) {
    return nullptr;
  }

  // It's problematic storing a CCW to a global in another compartment because
  // you don't know how far to unwrap it to get the original object
  // back. Instead store a CCW to a plain object in the same compartment as the
  // global (this uses Object.prototype).
  Rooted<JSObject*> incumbentGlobalRepresentative(cx);
  if (!GetIncumbentGlobalRepresentative(cx, &incumbentGlobalRepresentative)) {
    return nullptr;
  }

  FinalizationQueueObject* queue =
      NewObjectWithGivenProto<FinalizationQueueObject>(cx, nullptr);
  if (!queue) {
    return nullptr;
  }

  queue->initReservedSlotTyped(CLEANUP_CALLBACK_SLOT,
                               ObjectValue(*cleanupCallback));
  queue->initReservedSlotTyped(
      INCUMBENT_GLOBAL_REPRESENTATIVE_SLOT,
      JS::ObjectOrNullValue(incumbentGlobalRepresentative));
  InitReservedSlot(queue, RECORDS_TO_BE_CLEANED_UP_SLOT.index(),
                   recordsToBeCleanedUp.release(),
                   MemoryUse::FinalizationRegistryRecordVector);
  queue->initReservedSlotTyped(IS_QUEUED_FOR_CLEANUP_SLOT, BooleanValue(false));
  queue->initReservedSlotTyped(DO_CLEANUP_FUNCTION_SLOT,
                               ObjectValue(*doCleanupFunction));
  queue->initReservedSlotTyped(HAS_REGISTRY_SLOT, BooleanValue(false));

  doCleanupFunction->setExtendedSlot(DoCleanupFunction_QueueSlot,
                                     ObjectValue(*queue));

  return queue;
}

/* static */
void FinalizationQueueObject::trace(JSTracer* trc, JSObject* obj) {
  auto queue = &obj->as<FinalizationQueueObject>();

  if (QueuedRecordVector* records = queue->recordsToBeCleanedUp()) {
    records->trace(trc);
  }
}

/* static */
void FinalizationQueueObject::finalize(JS::GCContext* gcx, JSObject* obj) {
  auto* queue = &obj->as<FinalizationQueueObject>();
  gcx->delete_(obj, queue->recordsToBeCleanedUp(),
               MemoryUse::FinalizationRegistryRecordVector);
}

void FinalizationQueueObject::setHasRegistry(bool newValue) {
  MOZ_ASSERT(hasRegistry() != newValue);

  // Suppress our assertions about touching grey things. It's OK for us to set a
  // boolean slot even if this object is gray.
  AutoTouchingGrayThings atgt;

  setReservedSlotTyped(HAS_REGISTRY_SLOT, BooleanValue(newValue));
}

void FinalizationQueueObject::clear() {
  MOZ_ASSERT(!hasRegistry());
  if (QueuedRecordVector* records = recordsToBeCleanedUp()) {
    records->clear();
  }
}

bool FinalizationQueueObject::hasRegistry() const {
  return getReservedSlotTyped(HAS_REGISTRY_SLOT).toBoolean();
}

inline JSObject* FinalizationQueueObject::cleanupCallback() const {
  Value value = getReservedSlotTyped(CLEANUP_CALLBACK_SLOT);
  if (value.isUndefined()) {
    return nullptr;
  }
  return &value.toObject();
}

JSObject* FinalizationQueueObject::getIncumbentGlobalRepresentative() const {
  Value value = getReservedSlotTyped(INCUMBENT_GLOBAL_REPRESENTATIVE_SLOT);
  if (value.isUndefined()) {
    return nullptr;
  }
  return value.toObjectOrNull();
}

bool FinalizationQueueObject::hasRecordsToCleanUp() const {
  QueuedRecordVector* records = recordsToBeCleanedUp();
  return records && !records->empty();
}

FinalizationQueueObject::QueuedRecordVector*
FinalizationQueueObject::recordsToBeCleanedUp() const {
  Value value = getReservedSlotTyped(RECORDS_TO_BE_CLEANED_UP_SLOT);
  if (value.isUndefined()) {
    return nullptr;
  }
  return static_cast<QueuedRecordVector*>(value.toPrivate());
}

bool FinalizationQueueObject::isQueuedForCleanup() const {
  return getReservedSlotTyped(IS_QUEUED_FOR_CLEANUP_SLOT).toBoolean();
}

JSFunction* FinalizationQueueObject::doCleanupFunction() const {
  Value value = getReservedSlotTyped(DO_CLEANUP_FUNCTION_SLOT);
  if (value.isUndefined()) {
    return nullptr;
  }
  return &value.toObject().as<JSFunction>();
}

void FinalizationQueueObject::queueRecordToBeCleanedUp(
    FinalizationRecordObject* record) {
  MOZ_ASSERT(hasRegistry());

  MOZ_ASSERT(!record->isInQueue());
  record->setInQueue(true);

  AutoEnterOOMUnsafeRegion oomUnsafe;
  if (!recordsToBeCleanedUp()->append(record)) {
    oomUnsafe.crash("FinalizationQueueObject::queueRecordsToBeCleanedUp");
  }
}

void FinalizationQueueObject::setQueuedForCleanup(bool value) {
  MOZ_ASSERT(value != isQueuedForCleanup());
  setReservedSlotTyped(IS_QUEUED_FOR_CLEANUP_SLOT, BooleanValue(value));
}

/* static */
bool FinalizationQueueObject::doCleanup(JSContext* cx, unsigned argc,
                                        Value* vp) {
  CallArgs args = CallArgsFromVp(argc, vp);

  RootedFunction callee(cx, &args.callee().as<JSFunction>());

  Value value = callee->getExtendedSlot(DoCleanupFunction_QueueSlot);
  RootedFinalizationQueueObject queue(
      cx, &value.toObject().as<FinalizationQueueObject>());

  queue->setQueuedForCleanup(false);
  return cleanupQueuedRecords(cx, queue);
}

// CleanupFinalizationRegistry ( finalizationRegistry [ , callback ] )
// https://tc39.es/proposal-weakrefs/#sec-cleanup-finalization-registry
/* static */
bool FinalizationQueueObject::cleanupQueuedRecords(
    JSContext* cx, HandleFinalizationQueueObject queue,
    HandleObject callbackArg) {
  MOZ_ASSERT(cx->compartment() == queue->compartment());

  // 2. If callback is undefined, set callback to
  //    finalizationRegistry.[[CleanupCallback]].
  RootedValue callback(cx);
  if (callbackArg) {
    callback.setObject(*callbackArg);
  } else {
    JSObject* cleanupCallback = queue->cleanupCallback();
    MOZ_ASSERT(cleanupCallback);
    callback.setObject(*cleanupCallback);
  }

  // 3. While finalizationRegistry.[[Cells]] contains a Record cell such that
  //    cell.[[WeakRefTarget]] is empty, then an implementation may perform the
  //    following steps,
  //    a. Choose any such cell.
  //    b. Remove cell from finalizationRegistry.[[Cells]].
  //    c. Perform ? Call(callback, undefined, « cell.[[HeldValue]] »).

  QueuedRecordVector* records = queue->recordsToBeCleanedUp();
  MOZ_ASSERT_IF(!queue->hasRegistry(), records->empty());

  RootedValue heldValue(cx);
  RootedValue rval(cx);
  while (!records->empty()) {
    FinalizationRecordObject* record = records->popCopy();
    MOZ_ASSERT(!record->isInRecordMap());

    JS::ExposeObjectToActiveJS(record);

    MOZ_ASSERT(record->isInQueue());
    record->setInQueue(false);

    // Skip over records that have been unregistered.
    if (!record->isRegistered()) {
      continue;
    }

    heldValue.set(record->heldValue());

    record->clear();

    if (!Call(cx, callback, UndefinedHandleValue, heldValue, &rval)) {
      return false;
    }
  }

  return true;
}
