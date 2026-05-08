/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * GC support for FinalizationRegistry and WeakRef objects.
 */

#include "gc/FinalizationObservers.h"

#include "builtin/FinalizationRegistryObject.h"
#include "builtin/WeakRefObject.h"
#include "gc/GCRuntime.h"
#include "gc/PublicIterators.h"
#include "gc/Zone.h"
#include "vm/JSContext.h"

#include "gc/WeakMap-inl.h"
#include "vm/JSObject-inl.h"
#include "vm/NativeObject-inl.h"

using namespace js;
using namespace js::gc;

static inline Zone* GetWeakTargetZone(const Value& value) {
  return value.toGCThing()->zone();
}

static inline void CheckTargetValue(const Value& target) {
  MOZ_ASSERT(CanBeHeldWeakly(target));
  MOZ_ASSERT_IF(target.isObject(),
                !IsCrossCompartmentWrapper(&target.toObject()));
}

/* static */
ObserverListPtr ObserverListPtr::fromValue(Value value) {
  MOZ_ASSERT(value.isDouble());  // Stored as PrivateValue.
  return ObserverListPtr(value);
}

ObserverListPtr::ObserverListPtr(ObserverListObject* element)
    : ObserverListPtr(element, ElementKind) {}

ObserverListPtr::ObserverListPtr(ObserverList* list)
    : ObserverListPtr(list, ListHeadKind) {}

ObserverListPtr::ObserverListPtr(void* ptr, Kind kind)
    : value(PrivateValue(uintptr_t(ptr) | kind)) {
  MOZ_ASSERT((uintptr_t(ptr) & KindMask) == 0);
}

ObserverListPtr::ObserverListPtr(Value value) : value(value) {}

template <typename F>
auto ObserverListPtr::map(F&& func) const {
  if (isElement()) {
    return func(asElement());
  }

  return func(asList());
}

bool ObserverListPtr::isElement() const { return kind() == ElementKind; }

ObserverListPtr::Kind ObserverListPtr::kind() const {
  uintptr_t bits = uintptr_t(value.toPrivate());
  return static_cast<Kind>(bits & KindMask);
}

void* ObserverListPtr::ptr() const {
  uintptr_t bits = uintptr_t(value.toPrivate());
  return reinterpret_cast<void*>(bits & ~KindMask);
}

ObserverListObject* ObserverListPtr::asElement() const {
  MOZ_ASSERT(isElement());
  return static_cast<ObserverListObject*>(ptr());
}

ObserverList* ObserverListPtr::asList() const {
  MOZ_ASSERT(!isElement());
  return static_cast<ObserverList*>(ptr());
}

ObserverListPtr ObserverListPtr::getNext() const {
  return map([](auto* element) { return element->getNext(); });
}

ObserverListPtr ObserverListPtr::getPrev() const {
  return map([](auto* element) { return element->getPrev(); });
}

void ObserverListPtr::setNext(ObserverListPtr next) {
  map([next](auto* element) { element->setNext(next); });
}

/* static */
void ObserverListPtr::setPrev(ObserverListPtr prev) {
  map([prev](auto* element) { element->setPrev(prev); });
}

// An iterator for ObserverList that allows removing the current element from
// the list.
class ObserverList::Iter {
  using Ptr = ObserverListPtr;
  const Ptr end;
  Ptr ptr;
  Ptr nextPtr;

 public:
  explicit Iter(ObserverList& list)
      : end(&list), ptr(end.getNext()), nextPtr(ptr.getNext()) {
    MOZ_ASSERT(list.isEmpty() == done());
  }

  bool done() const { return ptr == end; }

  ObserverListObject* get() const {
    MOZ_ASSERT(!done());
    return ptr.asElement();
  }

  void next() {
    MOZ_ASSERT(!done());
    ptr = nextPtr;
    nextPtr = ptr.getNext();
  }

  operator ObserverListObject*() const { return get(); }
  ObserverListObject* operator->() const { return get(); }
};

ObserverList::ObserverList() : next(this), prev(this) { MOZ_ASSERT(isEmpty()); }

ObserverList::~ObserverList() { MOZ_ASSERT(isEmpty()); }

ObserverList::ObserverList(ObserverList&& other) : ObserverList() {
  MOZ_ASSERT(&other != this);
  *this = std::move(other);
}
ObserverList& ObserverList::operator=(ObserverList&& other) {
  MOZ_ASSERT(&other != this);
  MOZ_ASSERT(isEmpty());

  AutoTouchingGrayThings atgt;

  if (other.isEmpty()) {
    return *this;
  }

  next = other.next;
  prev = other.prev;

  // Check other's list head is correctly linked to its neighbours.
  MOZ_ASSERT(next.getPrev().asList() == &other);
  MOZ_ASSERT(prev.getNext().asList() == &other);

  // Update those neighbours to point to this object.
  next.setPrev(this);
  prev.setNext(this);

  other.makeEmpty();

  return *this;
}

void ObserverList::makeEmpty() {
  next = this;
  prev = this;
  MOZ_ASSERT(isEmpty());
}

bool ObserverList::isEmpty() const {
  ObserverListPtr thisLink = const_cast<ObserverList*>(this);
  MOZ_ASSERT((getNext() == thisLink) == (getPrev() == thisLink));
  return getNext() == thisLink;
}

ObserverListObject* ObserverList::getFirst() const {
  MOZ_ASSERT(!isEmpty());
  return next.asElement();
}

ObserverList::Iter ObserverList::iter() { return Iter(*this); }

void ObserverList::insertFront(ObserverListObject* obj) {
  MOZ_ASSERT(!obj->isInList());

  // The other things in this list might be gray.
  AutoTouchingGrayThings atgt;

  Ptr oldNext = getNext();

  setNext(obj);
  obj->setNext(oldNext);

  oldNext.setPrev(obj);
  obj->setPrev(this);
}

static inline void LinkElements(ObserverListPtr a, ObserverListPtr b) {
  a.setNext(b);
  b.setPrev(a);
}

void ObserverList::append(ObserverList&& other) {
  // The things in these lists might be gray.
  AutoTouchingGrayThings atgt;

  if (other.isEmpty()) {
    return;
  }

  LinkElements(getPrev(), other.getNext());
  LinkElements(other.getPrev(), this);

  other.makeEmpty();
}

void ObserverList::setNext(Ptr link) { next = link; }

void ObserverList::setPrev(Ptr link) { prev = link; }

/* static */
const ClassExtension ObserverListObject::classExtension_ = {
    ObserverListObject::objectMoved,  // objectMovedOp
};

bool ObserverListObject::isInList() const {
  bool inList = !getReservedSlot(NextSlot).isUndefined();
  MOZ_ASSERT(inList == !getReservedSlot(PrevSlot).isUndefined());
  return inList;
}

/* static */
size_t ObserverListObject::objectMoved(JSObject* obj, JSObject* old) {
  auto* self = static_cast<ObserverListObject*>(obj);
  self->objectMovedFrom(static_cast<ObserverListObject*>(old));
  return 0;
}

void ObserverListObject::objectMovedFrom(ObserverListObject* old) {
  AutoTouchingGrayThings atgt;

  if (!isInList()) {
    return;
  }

#ifdef DEBUG
  Ptr oldPtr = old;
  MOZ_ASSERT(getNext() != oldPtr);
  MOZ_ASSERT(getPrev() != oldPtr);
  MOZ_ASSERT(getNext().getPrev() == oldPtr);
  MOZ_ASSERT(getPrev().getNext() == oldPtr);
#endif

  getNext().setPrev(this);
  getPrev().setNext(this);
}

void ObserverListObject::unlink() {
  AutoTouchingGrayThings atgt;

  if (!isInList()) {
    return;
  }

  Ptr next = getNext();
  Ptr prev = getPrev();

#ifdef DEBUG
  Ptr thisPtr = this;
  MOZ_ASSERT(prev.getNext() == thisPtr);
  MOZ_ASSERT(next.getPrev() == thisPtr);
#endif

  next.setPrev(prev);
  prev.setNext(next);

  setReservedSlot(NextSlot, UndefinedValue());
  setReservedSlot(PrevSlot, UndefinedValue());
  MOZ_ASSERT(!isInList());
}

ObserverListPtr ObserverListObject::getNext() const {
  Value value = getReservedSlot(NextSlot);
  return Ptr::fromValue(value);
}

ObserverListPtr ObserverListObject::getPrev() const {
  Value value = getReservedSlot(PrevSlot);
  return Ptr::fromValue(value);
}

void ObserverListObject::setNext(Ptr next) {
  setReservedSlot(NextSlot, next.asValue());
}

void ObserverListObject::setPrev(Ptr prev) {
  setReservedSlot(PrevSlot, prev.asValue());
}

FinalizationObservers::FinalizationObservers(Zone* zone)
    : registries(zone), recordMap(zone), weakRefMap(zone) {}

FinalizationObservers::~FinalizationObservers() {
  MOZ_ASSERT(registries.empty());
  MOZ_ASSERT(recordMap.empty());
}

bool GCRuntime::addFinalizationRegistry(
    JSContext* cx, Handle<FinalizationRegistryObject*> registry) {
  if (!cx->zone()->ensureFinalizationObservers() ||
      !cx->zone()->finalizationObservers()->addRegistry(registry)) {
    ReportOutOfMemory(cx);
    return false;
  }

  return true;
}

bool FinalizationObservers::addRegistry(
    Handle<FinalizationRegistryObject*> registry) {
  return registries.put(registry);
}

bool GCRuntime::registerWithFinalizationRegistry(
    JSContext* cx, HandleValue target,
    Handle<FinalizationRecordObject*> record) {
  CheckTargetValue(target);

  Zone* zone = GetWeakTargetZone(target);
  if (!zone->ensureFinalizationObservers() ||
      !zone->finalizationObservers()->addRecord(target, record)) {
    ReportOutOfMemory(cx);
    return false;
  }

  return true;
}

bool FinalizationObservers::addRecord(
    HandleValue target, Handle<FinalizationRecordObject*> record) {
  // Add a record to the record map and clean up on failure.
  //
  // The following must be updated and kept in sync:
  //  - the zone's recordMap (to observe the target)
  //  - the registry's global objects's recordSet (to trace the record)

  auto ptr = recordMap.lookupForAdd(target);
  if (!ptr && !recordMap.add(ptr, target, ObserverList())) {
    return false;
  }

  ptr->value().insertFront(record);

  record->setInRecordMap(true);
  return true;
}

void FinalizationObservers::clearRecords() {
  // Clear table entries related to FinalizationRecordObjects, which are not
  // processed after the start of shutdown.
  //
  // WeakRefs are still updated during shutdown to avoid the possibility of
  // stale or dangling pointers.
  for (auto iter = recordMap.iter(); !iter.done(); iter.next()) {
    ObserverList& records = iter.get().value();
    for (auto listIter = records.iter(); !listIter.done(); listIter.next()) {
      listIter->unlink();
    }
  }
  recordMap.clear();
}

void GCRuntime::traceWeakFinalizationObserverEdges(JSTracer* trc, Zone* zone) {
  MOZ_ASSERT(CurrentThreadCanAccessRuntime(trc->runtime()));
  FinalizationObservers* observers = zone->finalizationObservers();
  if (observers) {
    observers->traceWeakEdges(trc, zone);
  }
}

void FinalizationObservers::traceWeakEdges(JSTracer* trc, JS::Zone* zone) {
  // Removing dead pointers from vectors may reorder live pointers to gray
  // things in the vector. This is OK.
  AutoTouchingGrayThings atgt;

  traceWeakWeakRefEdges(trc);
  traceWeakFinalizationRegistryEdges(trc, zone);
}

void FinalizationObservers::traceWeakFinalizationRegistryEdges(JSTracer* trc,
                                                               JS::Zone* zone) {
  // Sweep finalization registry data and queue finalization records for cleanup
  // for any entries whose target is dying and remove them from the map.

  // Clear cached state and set it again below if required.
  zone->clearGCFinalizationRegistriesMayHaveSymbolRegistrations();

  GCRuntime* gc = &trc->runtime()->gc;

  for (auto iter = registries.modIter(); !iter.done(); iter.next()) {
    MOZ_ASSERT(MaybeForwarded(iter.get().get())->zone() == zone);

    auto result =
        TraceWeakEdge(trc, &iter.getMutable(), "FinalizationRegistry");
    if (result.isDead()) {
      auto* registry = result.initialTarget();
      registry->queue()->setHasRegistry(false);

      // Remove any queued records. These might be dead since the registry was
      // not marked.
      registry->queue()->clear();

      iter.remove();
    } else {
      FinalizationRegistryObject* registry = result.finalTarget();
      bool hasSymbolRegistrations = false;
      registry->traceWeak(trc, &hasSymbolRegistrations);
      if (hasSymbolRegistrations) {
        zone->setGCFinalizationRegistriesMayHaveSymbolRegistrations();
      }

      // Now we know the registry is alive we can queue any records for cleanup
      // if this didn't happen already. See
      // shouldQueueFinalizationRegistryForCleanup for details.
      FinalizationQueueObject* queue = registry->queue();
      if (queue->hasRecordsToCleanUp()) {
        MOZ_ASSERT(shouldQueueFinalizationRegistryForCleanup(queue));
        gc->queueFinalizationRegistryForCleanup(queue);
      }
    }
  }

  for (auto iter = recordMap.modIter(); !iter.done(); iter.next()) {
    ObserverList& records = iter.get().value();

    // Sweep finalization records, removing any dead ones.
    for (auto iter = records.iter(); !iter.done(); iter.next()) {
      auto* record = &iter->as<FinalizationRecordObject>();
      MOZ_ASSERT(record->isInRecordMap());
      auto result =
          TraceManuallyBarrieredWeakEdge(trc, &record, "FinalizationRecord");
      if (result.isDead()) {
        record = result.initialTarget();
        record->setInRecordMap(false);
        record->unlink();
      }
    }

    // Queue remaining finalization records if the target is dying.
    if (!TraceWeakEdge(trc, &iter.get().mutableKey(),
                       "FinalizationRecord target")) {
      for (auto iter = records.iter(); !iter.done(); iter.next()) {
        auto* record = &iter->as<FinalizationRecordObject>();
        record->setInRecordMap(false);
        record->unlink();

        // Move the record to the queue object. In theory this requires a read
        // barrier since the record pointer is weak. However we have may have
        // finished marking the record's zone at this point so this is not
        // possible. Instead note that the record will be marked if the registry
        // is alive. If not then we clear any queued records when we discover
        // the the registry is dead above.
        FinalizationQueueObject* queue = record->queue();
        queue->queueRecordToBeCleanedUp(record);

        if (shouldQueueFinalizationRegistryForCleanup(queue)) {
          gc->queueFinalizationRegistryForCleanup(queue);
        }
      }
      iter.remove();
    } else {
      CheckTargetValue(iter.get().key());
    }
  }
}

bool FinalizationObservers::shouldQueueFinalizationRegistryForCleanup(
    FinalizationQueueObject* queue) {
  // FinalizationRegistries and their targets may be in different zones and
  // therefore swept at different times during GC. If a target is observed to
  // die but the registry's zone has not yet been swept then we don't whether we
  // need to queue the registry for cleanup callbacks, as the registry itself
  // might be dead.
  //
  // In this case we defer queuing the registry and this happens when the
  // registry is swept.
  MOZ_ASSERT(queue->hasRegistry());
  Zone* zone = queue->zone();
  return !zone->wasGCStarted() || zone->gcState() >= Zone::Sweep;
}

void GCRuntime::queueFinalizationRegistryForCleanup(
    FinalizationQueueObject* queue) {
  // Prod the embedding to call us back later to run the finalization callbacks,
  // if necessary.

  MOZ_ASSERT(!IsAboutToBeFinalizedUnbarriered(queue));
  MOZ_ASSERT(!IsAboutToBeFinalizedUnbarriered(queue->doCleanupFunction()));
  if (queue->isQueuedForCleanup()) {
    return;
  }

  JSObject* incumbentGlobal = nullptr;

  if (JSObject* wrapped = queue->getIncumbentGlobalRepresentative()) {
    JSObject* unwrappedIncumbentGlobalRepresentative =
        UncheckedUnwrapWithoutExpose(wrapped);
    MOZ_ASSERT(unwrappedIncumbentGlobalRepresentative);
    // If the incumbentGlobal object becomes a dead wrapper here, the target
    // global has already gone, and the finalization callback won't do anything
    // to it anyway.
    if (JS_IsDeadWrapper(unwrappedIncumbentGlobalRepresentative)) {
      return;
    }
    incumbentGlobal = &unwrappedIncumbentGlobalRepresentative->nonCCWGlobal();
  }

  callHostCleanupFinalizationRegistryCallback(queue->doCleanupFunction(),
                                              incumbentGlobal);

  // The queue object may be gray, and that's OK.
  AutoTouchingGrayThings atgt;

  queue->setQueuedForCleanup(true);
}

// Register |target| such that when it dies |weakRef| will have its pointer to
// |target| cleared.
bool GCRuntime::registerWeakRef(JSContext* cx, HandleValue target,
                                Handle<WeakRefObject*> weakRef) {
  CheckTargetValue(target);

  Zone* zone = GetWeakTargetZone(target);
  if (!zone->ensureFinalizationObservers() ||
      !zone->finalizationObservers()->addWeakRefTarget(target, weakRef)) {
    ReportOutOfMemory(cx);
    return false;
  }

  return true;
}

bool FinalizationObservers::addWeakRefTarget(HandleValue target,
                                             Handle<WeakRefObject*> weakRef) {
  auto ptr = weakRefMap.lookupForAdd(target);
  if (!ptr && !weakRefMap.relookupOrAdd(ptr, target, ObserverList())) {
    return false;
  }

  ptr->value().insertFront(weakRef);
  return true;
}

void FinalizationObservers::removeWeakRefTarget(
    Handle<Value> target, Handle<WeakRefObject*> weakRef) {
  CheckTargetValue(target);
  MOZ_ASSERT(weakRef->target() == target);

  MOZ_ASSERT(weakRef->isInList());
  weakRef->clearTargetAndUnlink();

  auto ptr = weakRefMap.lookup(target);
  MOZ_ASSERT(ptr);
  ObserverList& list = ptr->value();
  if (list.isEmpty()) {
    weakRefMap.remove(ptr);
  }
}

void FinalizationObservers::traceWeakWeakRefEdges(JSTracer* trc) {
  for (auto iter = weakRefMap.modIter(); !iter.done(); iter.next()) {
    ObserverList& weakRefs = iter.get().value();
    auto result =
        TraceWeakEdge(trc, &iter.get().mutableKey(), "WeakRef target");

    if (result.isDead()) {
      // Clear the observer list if the target is dying.
      while (!weakRefs.isEmpty()) {
        auto* weakRef = &weakRefs.getFirst()->as<WeakRefObject>();
        weakRef->clearTargetAndUnlink();
      }
      iter.remove();
    } else {
      Value target = result.finalTarget();
      CheckTargetValue(target);

      // Update WeakRef targets if the target has been moved.
      if (target != result.initialTarget()) {
        traceWeakWeakRefList(trc, weakRefs, target);
      }
    }
  }
}

void FinalizationObservers::traceWeakWeakRefList(JSTracer* trc,
                                                 ObserverList& weakRefs,
                                                 Value target) {
  MOZ_ASSERT(!IsForwarded(target.toGCThing()));

  for (auto iter = weakRefs.iter(); !iter.done(); iter.next()) {
    auto* weakRef = &iter.get()->as<WeakRefObject>();
    MOZ_ASSERT(!IsForwarded(weakRef));
    if (weakRef->target() != target) {
      MOZ_ASSERT(MaybeForwarded(weakRef->target().toGCThing()) ==
                 target.toGCThing());
      weakRef->setTargetUnbarriered(target);
    }
  }
}

JS_PUBLIC_API void JS::MaybeClearWeakRefTargets(
    JSRuntime* runtime, JS::ShouldClearWeakRefTargetCallback callback,
    void* data) {
  MOZ_ASSERT(CurrentThreadCanAccessRuntime(runtime));
  AssertHeapIsIdle();
  runtime->gc.maybeClearWeakRefTargets(callback, data);
}

void GCRuntime::maybeClearWeakRefTargets(
    JS::ShouldClearWeakRefTargetCallback callback, void* data) {
  for (AllZonesIter zone(this); !zone.done(); zone.next()) {
    FinalizationObservers* observers = zone->finalizationObservers();
    if (observers) {
      observers->maybeClearWeakRefTargets(callback, data);
    }
  }
}

void FinalizationObservers::maybeClearWeakRefTargets(
    JS::ShouldClearWeakRefTargetCallback callback, void* data) {
  for (auto iter = weakRefMap.modIter(); !iter.done(); iter.next()) {
    Value target = iter.get().key();
    if (callback(target.toGCCellPtr(), data)) {
      ObserverList& weakRefs = iter.get().value();
      while (!weakRefs.isEmpty()) {
        auto* weakRef = &weakRefs.getFirst()->as<WeakRefObject>();
        weakRef->clearTargetAndUnlink();
      }
      iter.remove();
    }
  }
}

bool FinalizationObservers::isTarget(const Value& target) {
  return weakRefMap.has(target) || recordMap.has(target);
}

/* static */
bool GCRuntime::isFinalizationObserverTarget(const Value& target) {
  Zone* zone = GetWeakTargetZone(target);
  FinalizationObservers* observers = zone->finalizationObservers();
  return observers && observers->isTarget(target);
}

/* static */
bool GCRuntime::relocateFinalizationObserverTarget(const Value& oldTarget,
                                                   const Value& newTarget) {
  CheckTargetValue(oldTarget);
  CheckTargetValue(newTarget);
  MOZ_ASSERT(oldTarget != newTarget);

  Zone* oldZone = GetWeakTargetZone(oldTarget);
  FinalizationObservers* oldObservers = oldZone->finalizationObservers();
  if (!oldObservers) {
    return true;
  }

  ObserverList weakRefList = oldObservers->extractWeakRefObservers(oldTarget);
  ObserverList recordList = oldObservers->extractRecordObservers(oldTarget);
  if (weakRefList.isEmpty() && recordList.isEmpty()) {
    return true;
  }

  // Update the target stored in each WeakRefObject.
  for (auto iter = weakRefList.iter(); !iter.done(); iter.next()) {
    auto* weakRef = &iter.get()->as<WeakRefObject>();
    MOZ_ASSERT(weakRef->target() == oldTarget);
    weakRef->setTargetUnbarriered(newTarget);
  }

  Zone* newZone = GetWeakTargetZone(newTarget);
  if (!newZone->ensureFinalizationObservers()) {
    return false;
  }

  FinalizationObservers* newObservers = newZone->finalizationObservers();
  if (!weakRefList.isEmpty() &&
      !newObservers->addWeakRefObservers(newTarget, std::move(weakRefList))) {
    return false;
  }

  if (!recordList.isEmpty() &&
      !newObservers->addRecordObservers(newTarget, std::move(recordList))) {
    return false;
  }

  return true;
}

ObserverList FinalizationObservers::extractWeakRefObservers(
    const Value& target) {
  ObserverList list;
  if (auto ptr = weakRefMap.lookup(target)) {
    list = std::move(ptr->value());
    weakRefMap.remove(ptr);
  }

  return list;
}

bool FinalizationObservers::addWeakRefObservers(const Value& target,
                                                ObserverList&& list) {
  auto ptr = weakRefMap.lookupForAdd(target);
  if (!ptr && !weakRefMap.add(ptr, target, ObserverList())) {
    return false;
  }

  ptr->value().append(std::move(list));
  return true;
}

ObserverList FinalizationObservers::extractRecordObservers(
    const Value& target) {
  ObserverList list;
  if (auto ptr = recordMap.lookup(target)) {
    list = std::move(ptr->value());
    recordMap.remove(ptr);
  }

  return list;
}

bool FinalizationObservers::addRecordObservers(const Value& target,
                                               ObserverList&& list) {
  auto ptr = recordMap.lookupForAdd(target);
  if (!ptr && !recordMap.add(ptr, target, ObserverList())) {
    return false;
  }

  ptr->value().append(std::move(list));
  return true;
}

/* static */
void GCRuntime::clearWeakRefTargets(Compartment* source, const Value& target) {
  Zone* zone = target.toGCThing()->zone();
  FinalizationObservers* observers = zone->finalizationObservers();
  if (observers) {
    observers->clearWeakRefTargets(source, target);
  }
}

void FinalizationObservers::clearWeakRefTargets(Compartment* source,
                                                const Value& target) {
  if (auto ptr = weakRefMap.lookup(target)) {
    ObserverList& weakRefs = ptr->value();
    for (auto iter = weakRefs.iter(); !iter.done(); iter.next()) {
      auto* weakRef = &iter->as<WeakRefObject>();
      if (weakRef->compartment() == source) {
        weakRef->clearTargetAndUnlink();
      }
    }
    if (weakRefs.isEmpty()) {
      weakRefMap.remove(ptr);
    }
  }
}

/* static */
void GCRuntime::clearWeakRefTargets(const CompartmentFilter& sourceFilter,
                                    JS::Realm* targetFilter) {
  Zone* zone = targetFilter->zone();
  FinalizationObservers* observers = zone->finalizationObservers();
  if (observers) {
    observers->clearWeakRefTargets(sourceFilter, targetFilter);
  }
}

void FinalizationObservers::clearWeakRefTargets(
    const CompartmentFilter& sourceFilter, JS::Realm* targetFilter) {
  for (auto mapIter = weakRefMap.modIter(); !mapIter.done(); mapIter.next()) {
    Value target = mapIter.get().key();
    if (target.isObject() && target.toObject().nonCCWRealm() == targetFilter) {
      ObserverList& weakRefs = mapIter.get().value();
      for (auto iter = weakRefs.iter(); !iter.done(); iter.next()) {
        auto* weakRef = &iter->as<WeakRefObject>();
        if (sourceFilter.match(weakRef->compartment())) {
          weakRef->clearTargetAndUnlink();
        }
      }
      if (weakRefs.isEmpty()) {
        mapIter.remove();
      }
    }
  }
}
