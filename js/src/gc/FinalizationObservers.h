/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef gc_FinalizationObservers_h
#define gc_FinalizationObservers_h

#include "gc/Barrier.h"
#include "gc/WeakMap.h"  // For GetSymbolHash.
#include "gc/ZoneAllocator.h"
#include "js/friend/CycleCollector.h"
#include "js/friend/Wrapper.h"
#include "js/GCHashTable.h"
#include "js/GCVector.h"
#include "js/Value.h"
#include "vm/NativeObject.h"

namespace js {

class FinalizationRegistryObject;
class FinalizationRecordObject;
class FinalizationQueueObject;
class WeakRefObject;

namespace gc {

// ObserverList
//
// The following classes provide ObserverList, a circular doubly linked list of
// ObserverListObjects with weak, possibly-cross-zone pointers between the
// elements. Despite how bad this sounds this is OK because:
//
//   1) The list is only accessed from the main thread (no races)
//   2) The link pointers are weak (no pre barriers are required on update)
//   3) The elements don't escape
//
// These lists are used to hold the WeakRefObjects and FinalizationRecordObjects
// associated with a WeakRef or FinalizationRegistry target. They live in the
// target's zone although the elements themselves may be in any zone.

class ObserverListObject;
class ObserverList;

// Link pointers in a ObserverList. These are encoded as PrivateValues to allow
// storing them in object slots. They contain a tagged pointer to either an
// ObserverListObject or an ObserverList (the list head).
class ObserverListPtr {
  Value value;

  enum Kind : uintptr_t { ElementKind = 0, ListHeadKind = 1, KindMask = 1 };

  explicit ObserverListPtr(Value value);
  ObserverListPtr(void* ptr, Kind kind);

  Kind kind() const;
  void* ptr() const;

  template <typename F>
  auto map(F&& func) const;

 public:
  MOZ_IMPLICIT ObserverListPtr(ObserverListObject* element);
  MOZ_IMPLICIT ObserverListPtr(ObserverList* list);

  static ObserverListPtr fromValue(Value value);

  bool operator==(const ObserverListPtr& other) const {
    return value == other.value;
  }
  bool operator!=(const ObserverListPtr& other) const {
    return !(*this == other);
  }

  Value asValue() const { return value; }

  bool isElement() const;
  ObserverListObject* asElement() const;
  ObserverList* asList() const;

  ObserverListPtr getNext() const;
  ObserverListPtr getPrev() const;
  void setNext(ObserverListPtr next);
  void setPrev(ObserverListPtr prev);
};

// Base class for the elements of an ObserverList.
class ObserverListObject : public NativeObject {
  using Ptr = ObserverListPtr;

  Ptr getNext() const;
  Ptr getPrev() const;
  void setNext(Ptr next);
  void setPrev(Ptr prev);
  friend class ObserverListPtr;
  friend class ObserverList;

  static size_t objectMoved(JSObject* obj, JSObject* old);
  void objectMovedFrom(ObserverListObject* old);

 protected:
  // These fields are weak and possibly cross-zone pointers. They must not be
  // allowed to escape.
  enum { NextSlot, PrevSlot, SlotCount };

  static const ClassExtension classExtension_;

 public:
  void unlink();
  bool isInList() const;
};

// A circular doubly linked list of ObserverListObjects with weak references
// between them.
class ObserverList {
  using Ptr = ObserverListPtr;

  // These fields are weak and possibly cross-zone pointers. They must not be
  // allowed to escape.
  Ptr next;
  Ptr prev;

  Ptr getNext() const { return next; }
  Ptr getPrev() const { return prev; }
  void setNext(Ptr link);
  void setPrev(Ptr link);
  friend class ObserverListPtr;

 public:
  class Iter;

  ObserverList();
  ~ObserverList();

  // The list must be relinked on move.
  ObserverList(const ObserverList& other) = delete;
  ObserverList& operator=(const ObserverList& other) = delete;
  ObserverList(ObserverList&& other);
  ObserverList& operator=(ObserverList&& other);

  bool isEmpty() const;
  ObserverListObject* getFirst() const;

  Iter iter();

  void insertFront(ObserverListObject* obj);
};

// A hasher for GC things used as WeakRef and FinalizationRegistry targets. Uses
// stable cell hashing, except for symbols where it uses the symbol's stored
// hash.
struct WeakTargetHasher {
  using Key = HeapPtr<Value>;
  using Lookup = Value;

  static bool maybeGetHash(const Lookup& l, HashNumber* hashOut) {
    if (l.isSymbol()) {
      *hashOut = GetSymbolHash(l.toSymbol());
      return true;
    }
    return StableCellHasher<Cell*>::maybeGetHash(l.toGCThing(), hashOut);
  }
  static bool ensureHash(const Lookup& l, HashNumber* hashOut) {
    if (l.isSymbol()) {
      *hashOut = GetSymbolHash(l.toSymbol());
      return true;
    }
    return StableCellHasher<Cell*>::ensureHash(l.toGCThing(), hashOut);
  }
  static HashNumber hash(const Lookup& l) {
    if (l.isSymbol()) {
      return GetSymbolHash(l.toSymbol());
    }
    return StableCellHasher<Cell*>::hash(l.toGCThing());
  }
  static bool match(const Key& k, const Lookup& l) {
    if (k.type() != l.type()) {
      return false;
    }
    if (l.isSymbol()) {
      return k.toSymbol() == l.toSymbol();
    }
    return StableCellHasher<Cell*>::match(k.toGCThing(), l.toGCThing());
  }
};

// Per-zone data structures to support FinalizationRegistry and WeakRef.
class FinalizationObservers {
  // The set of all finalization registries in the associated zone.
  using RegistrySet =
      GCHashSet<HeapPtr<FinalizationRegistryObject*>,
                StableCellHasher<HeapPtr<FinalizationRegistryObject*>>,
                ZoneAllocPolicy>;
  RegistrySet registries;

  // A map from finalization registry targets in the associated zone to a list
  // of finalization records representing registries that the target is
  // registered with and their associated held values. The records may be in
  // other zones. They are direct pointers and are not wrapped.
  using ObserverMap = GCHashMap<HeapPtr<Value>, ObserverList, WeakTargetHasher,
                                ZoneAllocPolicy>;
  ObserverMap recordMap;

  // A map from weak ref targets in the associated zone to a list of weak ref
  // objects that reference that target. The weak refs may be in other zones.
  // They are direct pointers and are not wrapped.
  ObserverMap weakRefMap;

 public:
  explicit FinalizationObservers(Zone* zone);
  ~FinalizationObservers();

  // FinalizationRegistry support:
  bool addRegistry(Handle<FinalizationRegistryObject*> registry);
  bool addRecord(HandleValue target, Handle<FinalizationRecordObject*> record);
  void clearRecords();

  // WeakRef support:
  bool addWeakRefTarget(Handle<Value> target, Handle<WeakRefObject*> weakRef);
  void removeWeakRefTarget(Handle<Value> target,
                           Handle<WeakRefObject*> weakRef);

  // Integration with cycle collector:
  void maybeClearWeakRefTargets(JS::ShouldClearWeakRefTargetCallback callback,
                                void* data);

  // Integration with object transplanting:
  bool isTarget(const Value& target);
  ObserverList extractWeakRefObservers(const Value& target);
  bool addWeakRefObservers(const Value& target, ObserverList&& list);
  ObserverList extractRecordObservers(const Value& target);
  bool addRecordObservers(const Value& target, ObserverList&& list);

  // Integration with nuking CCWs:
  void clearWeakRefTargets(JS::Compartment* source, const Value& target);
  void clearWeakRefTargets(const CompartmentFilter& sourceFilter,
                           JS::Realm* targetFilter);

  void traceWeakEdges(JSTracer* trc, JS::Zone* zone);

 private:
  void traceWeakFinalizationRegistryEdges(JSTracer* trc, JS::Zone* zone);
  void traceWeakWeakRefEdges(JSTracer* trc);
  void traceWeakWeakRefList(JSTracer* trc, ObserverList& weakRefs,
                            Value target);
  bool shouldQueueFinalizationRegistryForCleanup(FinalizationQueueObject*);
};

}  // namespace gc
}  // namespace js

namespace mozilla {
template <>
struct FallibleHashMethods<js::gc::WeakTargetHasher>
    : public js::gc::WeakTargetHasher {};
}  // namespace mozilla

#endif  // gc_FinalizationObservers_h
