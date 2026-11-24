/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef gc_WeakMap_h
#define gc_WeakMap_h

#include "mozilla/Atomics.h"
#include "mozilla/Maybe.h"

#include "ds/SlimLinkedList.h"
#include "gc/AllocKind.h"
#include "gc/Barrier.h"
#include "gc/Cell.h"
#include "gc/Marking.h"
#include "gc/Tracer.h"
#include "gc/ZoneAllocator.h"
#include "js/GCVector.h"
#include "js/HashTable.h"
#include "js/HeapAPI.h"
#include "vm/JSObject.h"

namespace JS {
class Zone;
}

namespace js {

class GCMarker;
class WeakMapBase;
struct WeakMapTracer;

template <typename T>
struct WeakMapKeyHasher;

extern void DumpWeakMapLog(JSRuntime* rt);

namespace gc {

// Ensure a Symbol read out of a weak map is marked black in |zone|'s atom
// reference bitmap before it can escape to script.
void MarkSymbolForWeakMapReadBarrier(JS::Zone* zone, JS::Symbol* sym);

void ShouldWeakMappingEntryBeBlack(JSObject* map, Cell* key, Cell* delegate,
                                   Cell* value, bool* keyShouldBeBlack,
                                   bool* valueShouldBeBlack);

#if defined(JS_GC_ZEAL) || defined(DEBUG)
// Check whether a weak map / entry is marked correctly.
bool CheckWeakMapMapMarking(const WeakMapBase* map);
bool CheckWeakMapEntryMarking(const WeakMapBase* map, Cell* key, Cell* value);
#endif

template <typename PtrT>
struct MightBeInNursery {
  using T = std::remove_pointer_t<PtrT>;
  static_assert(std::is_base_of_v<Cell, T>);
  static_assert(!std::is_same_v<Cell, T> && !std::is_same_v<TenuredCell, T>);

#define CAN_NURSERY_ALLOC_KIND_OR(_1, _2, Type, _3, _4, canNurseryAlloc, _5) \
  std::is_base_of_v<Type, T> ? canNurseryAlloc:

  // FOR_EACH_ALLOCKIND doesn't cover every possible type: make sure
  // to default to `true` for unknown types.
  static constexpr bool value =
      FOR_EACH_ALLOCKIND(CAN_NURSERY_ALLOC_KIND_OR) true;
#undef CAN_NURSERY_ALLOC_KIND_OR
};
template <>
struct MightBeInNursery<JS::Value> {
  static constexpr bool value = true;
};

}  // namespace gc

// A subclass template of js::HashMap whose keys and values may be
// garbage-collected. When a key is collected, the table entry disappears,
// dropping its reference to the value.
//
// More precisely:
//
//     A WeakMap entry is live if and only if both the WeakMap and the entry's
//     key are live. An entry holds a strong reference to its value.
//
// You must call this table's 'trace' method when its owning object is reached
// by the garbage collection tracer. Once a table is known to be live, the
// implementation takes care of the special weak marking (ie, marking through
// the implicit edges stored in the map) and of removing (sweeping) table
// entries when collection is complete.

// WeakMaps are marked with an incremental linear-time algorithm that handles
// all orderings of map and key marking. The basic algorithm is:
//
// At first while marking, do nothing special when marking WeakMap keys (there
// is no straightforward way to know whether a particular object is being used
// as a key in some weakmap.) When a WeakMap is marked, scan through it to mark
// all entries with live keys, and collect all unmarked keys into a "weak keys"
// table.
//
// At some point, everything reachable has been marked. At this point, enter
// "weak marking mode". In this mode, whenever any object is marked, look it up
// in the weak keys table to see if it is the key for any WeakMap entry and if
// so, mark the value. When entering weak marking mode, scan the weak key table
// to find all keys that have been marked since we added them to the table, and
// mark those entries.
//
// In addition, we want weakmap marking to work incrementally. So WeakMap
// mutations are barriered to keep the weak keys table up to date: entries are
// removed if their key is removed from the table, etc.
//
// You can break down various ways that WeakMap values get marked based on the
// order that the map and key are marked. All of these assume the map and key
// get marked at some point:
//
//   key marked, then map marked:
//    - value was marked with map in `markEntries()`
//   map marked, key already in map, key marked before weak marking mode:
//    - key added to gcEphemeronEdges when map marked in `markEntries()`
//    - value marked during `enterWeakMarkingMode`
//   map marked, key already in map, key marked after weak marking mode:
//    - when key is marked, gcEphemeronEdges[key] triggers marking of value in
//      `markImplicitEdges()`
//   map marked, key inserted into map, key marked:
//    - value was live when inserted and must get marked at some point
//

using WeakMapColors = HashMap<WeakMapBase*, js::gc::CellColor,
                              DefaultHasher<WeakMapBase*>, SystemAllocPolicy>;

class WeakMapBase;
using WeakMapList = SlimLinkedList<WeakMapBase>;

// Common base class for all WeakMap specializations, used for calling
// subclasses' GC-related methods.
class WeakMapBase : public SlimLinkedListElement<WeakMapBase> {
  friend class js::GCMarker;

 public:
  using CellColor = js::gc::CellColor;

  WeakMapBase(JSObject* memOf, JS::Zone* zone);
  virtual ~WeakMapBase() = default;

  JS::Zone* zone() const { return zone_; }

  // Whether this is a 'system' weakmap as opposed to a 'user' one. System
  // weakmaps are used internally by the engine and |memberOf| is null. User
  // ones are part of a JS WeakMap object pointed to by |memberOf|.
  bool isSystem() const { return !memberOf; }

  // Garbage collector entry points.

  // Unmark all weak maps in a zone.
  static void unmarkZone(JS::Zone* zone);
#ifdef DEBUG
  static void checkZoneUnmarked(JS::Zone* zone);
#else
  static void checkZoneUnmarked(JS::Zone* zone) {}
#endif

  // Check all weak maps in a zone that have been marked as live in this garbage
  // collection, and mark the values of all entries that have become strong
  // references to them. Return true if we marked any new values, indicating
  // that we need to make another pass. In other words, mark my marked maps'
  // marked members' mid-collection.
  static bool markZoneIteratively(JS::Zone* zone, GCMarker* marker);

  // Add zone edges for weakmaps in zone |mapZone| with key delegates in a
  // different zone.
  [[nodiscard]] static bool findSweepGroupEdgesForZone(JS::Zone* atomsZone,
                                                       JS::Zone* mapZone);

  // Trace all weak map bindings. Used by the cycle collector.
  static void traceAllMappings(WeakMapTracer* tracer);

  // Unmark gray weak map keys and values that the cycle collector requires to
  // be black. See js::FixWeakMappingGrayBits.
  static void fixAllGrayMappings(JSRuntime* rt);

#if defined(JS_GC_ZEAL)
  // Save information about which weak maps are marked for a zone.
  static bool saveZoneMarkedWeakMaps(JS::Zone* zone,
                                     WeakMapColors& markedWeakMaps);

  // Restore information about which weak maps are marked for many zones.
  static void restoreMarkedWeakMaps(WeakMapColors& markedWeakMaps);
#endif

#if defined(JS_GC_ZEAL) || defined(DEBUG)
  static bool checkMarkingForZone(JS::Zone* zone);
#endif

#ifdef JSGC_HASH_TABLE_CHECKS
  static void checkWeakMapsAfterMovingGC(JS::Zone* zone);
#endif

 protected:
  // Instance member functions called by the above. Instantiations of WeakMap
  // override these with definitions appropriate for their Key and Value types.
  virtual bool empty() const = 0;
  virtual void trace(JSTracer* tracer) = 0;
  virtual bool findSweepGroupEdges(Zone* atomsZone) = 0;
  virtual void traceWeakEdgesDuringSweeping(JSTracer* trc) = 0;
  virtual void traceMappings(WeakMapTracer* tracer) = 0;
  virtual void clearAndCompact() = 0;

  virtual bool markEntries(GCMarker* marker) = 0;

  // Trace any keys and values that are in the nursery. Return false if any
  // remain in the nursery.
  virtual bool traceNurseryEntriesOnMinorGC(JSTracer* trc) = 0;
  virtual bool sweepAfterMinorGC() = 0;

  // Restore marking invariants after gray unmarking, which ignores weakmaps.
  // Returns whether any marking changed.
  virtual bool fixGrayMappings() = 0;

  // We have a key that, if it or its delegate is marked, may lead to a WeakMap
  // value getting marked. Insert the necessary edges into the appropriate
  // zone's gcEphemeronEdges or gcNurseryEphemeronEdges tables.
  [[nodiscard]] bool addEphemeronEdgesForEntry(gc::MarkColor mapColor,
                                               gc::TenuredCell* key,
                                               gc::Cell* delegate,
                                               gc::TenuredCell* value);
  [[nodiscard]] bool addEphemeronEdge(gc::MarkColor color, gc::TenuredCell* src,
                                      gc::TenuredCell* dst);

  gc::CellColor mapColor() const {
    return static_cast<gc::CellColor>(mapColor_.load());
  }
  void setMapColor(gc::CellColor newColor) { mapColor_ = uint32_t(newColor); }

  bool isMarked() const { return gc::IsMarked(mapColor()); }

  // Attempt to mark the map and return the old color if successful.
  mozilla::Maybe<gc::CellColor> markMap(gc::MarkColor markColor);

  void setHasNurseryEntries();

#ifdef DEBUG
  virtual void checkCachedFlags() const = 0;
#endif

#ifdef JS_GC_ZEAL
  virtual bool checkMarking() const = 0;
  virtual bool allowKeysInOtherZones() const { return false; }
  friend bool gc::CheckWeakMapMapMarking(const WeakMapBase*);
  friend bool gc::CheckWeakMapEntryMarking(const WeakMapBase*, gc::Cell*,
                                           gc::Cell*);
#endif

#ifdef JSGC_HASH_TABLE_CHECKS
  virtual void checkAfterMovingGC() const = 0;
#endif

  // Object that this weak map is part of, if any.
  HeapPtr<JSObject*> memberOf;

  // Zone containing this weak map.
  JS::Zone* zone_;

  // Whether this object has been marked during garbage collection and which
  // color it was marked.
  mozilla::Atomic<uint32_t, mozilla::Relaxed> mapColor_;

  // Cached information about keys to speed up findSweepGroupEdges.
  bool mayHaveObjectKeyDelegates = false;
  bool mayHaveSymbolKeys = false;

  // Whether this map contains entries with nursery keys or values.
  bool hasNurseryEntries = false;

  // Whether the |nurseryKeys| vector contains the keys of all entries with
  // nursery keys or values. This can be false if it gets too large or on OOM.
  bool nurseryKeysValid = true;

  friend class JS::Zone;
  friend class js::Nursery;
};

// Get the hash from a Symbol.
HashNumber GetSymbolHash(JS::Symbol* sym);

// By default weak maps use default hasher for the key type, which hashes
// the pointer itself for pointer types.
template <typename T>
struct WeakMapKeyHasher : public DefaultHasher<T> {};

// We only support JS::Value keys that contain objects or symbols. For objects
// we hash the pointer and for symbols we use its stored hash, which is randomly
// generated on creation.
//
// Equality is based on a bitwise test not on JS Value semantics.
//
// Take care when modifying this code! Previously there have been security
// issues around using pointer hashing for maps (e.g. bug 1312001).
//
// Although this does use pointer hashing for objects, we don't think those
// concerns apply here because:
//
//  1) This uses an open addressed hash table rather than a chained one which
//     makes the attack much more difficult.
//
//  2) The allowed key types are restricted to objects and non-registered
//     symbols, so it's not possible to use int32 keys as were used in the
//     attack.
//
//  3) Symbols use their own random hash codes which can't be predicted.
//
//  4) Registered symbols are not allowed, which means it's not possible to leak
//     information about such symbols used by another zone.
//
//  5) Although sequentially allocated objects will have similar pointers,
//     ScrambleHashCode should work well enough to distribute these keys and
//     make predicting the hash code from the pointer difficult.
template <>
struct WeakMapKeyHasher<JS::Value> {
  using Key = JS::Value;
  using Lookup = JS::Value;

  static HashNumber hash(const Lookup& l) {
    checkValueType(l);
    if (l.isSymbol()) {
      return GetSymbolHash(l.toSymbol());
    }
    return mozilla::HashGeneric(l.asRawBits());
  }

  static bool match(const Key& k, const Lookup& l) {
    checkValueType(k);
    return k == l;
  }

  static void rekey(Key& k, const Key& newKey) { k = newKey; }

 private:
  static void checkValueType(const Value& value);
};

template <>
struct WeakMapKeyHasher<PreBarriered<JS::Value>> {
  using Key = PreBarriered<JS::Value>;
  using Lookup = JS::Value;

  static HashNumber hash(const Lookup& l) {
    return WeakMapKeyHasher<JS::Value>::hash(l);
  }
  static bool match(const Key& k, const Lookup& l) {
    return WeakMapKeyHasher<JS::Value>::match(k, l);
  }
  static void rekey(Key& k, const Lookup& newKey) { k.unbarrieredSet(newKey); }
};

template <class Key, class Value, class AllocPolicy>
class WeakMap : public WeakMapBase {
  using BarrieredKey = PreBarriered<Key>;
  using BarrieredValue = PreBarriered<Value>;

  using Map = HashMap<BarrieredKey, BarrieredValue,
                      WeakMapKeyHasher<BarrieredKey>, AllocPolicy>;
  using UnbarrieredMap =
      HashMap<Key, Value, WeakMapKeyHasher<Key>, AllocPolicy>;

  UnbarrieredMap map_;  // Barriers are added by |map()| accessor.

  // The keys of entries where either the key or value is allocated in the
  // nursery.
  GCVector<Key, 0, AllocPolicy> nurseryKeys;

 public:
  using Lookup = typename Map::Lookup;
  using Entry = typename Map::Entry;
  using Iterator = typename Map::Iterator;
  using ModIterator = typename Map::ModIterator;

  // Restrict the interface of HashMap::Ptr and AddPtr to remove mutable access
  // to the hash table entry which could otherwise bypass our barriers.

  using MutablePtr = typename Map::Ptr;
  class Ptr {
    MutablePtr ptr;
    friend class WeakMap;

   public:
    explicit Ptr(const MutablePtr& ptr) : ptr(ptr) {}
    bool found() const { return ptr.found(); }
    explicit operator bool() const { return found(); }
    const Entry& operator*() const { return *ptr; }
    const Entry* operator->() const { return &*ptr; }
  };

  using MutableAddPtr = typename Map::AddPtr;
  class AddPtr {
    MutableAddPtr ptr;
    friend class WeakMap;

   public:
    explicit AddPtr(const MutableAddPtr& ptr) : ptr(ptr) {}
    bool found() const { return ptr.found(); }
    explicit operator bool() const { return found(); }
    const Entry& operator*() const { return *ptr; }
    const Entry* operator->() const { return &*ptr; }
  };

  // Create a weak map owned by a JS object. Used for script-facing objects.
  explicit WeakMap(JSContext* cx, JSObject* memOf);

  // Create a weak map associated with a zone. For internal use by the engine.
  explicit WeakMap(JS::Zone* zone);

  ~WeakMap() override;

  Iterator iter() const { return map().iter(); }
  ModIterator modIter() { return map().modIter(); }
  uint32_t count() const { return map().count(); }
  bool empty() const override { return map().empty(); }
  bool has(const Lookup& lookup) const { return map().has(lookup); }
  void remove(const Lookup& lookup) { return map().remove(lookup); }
  void remove(Ptr ptr) { return map().remove(ptr.ptr); }

  // Get the value associated with a key, or a default constructed Value if the
  // key is not present in the map.
  Value get(const Lookup& l) const {
    Ptr ptr = lookup(l);
    if (!ptr) {
      return Value();
    }
    return ptr->value();
  }

  // Add a read barrier to prevent a gray value from escaping the weak map. This
  // is necessary because we don't unmark gray through weak maps.
  Ptr lookup(const Lookup& l) const {
    Ptr p = lookupUnbarriered(l);
    if (p) {
      valueReadBarrier(p->value());
    }
    return p;
  }

  Ptr lookupUnbarriered(const Lookup& l) const { return Ptr(map().lookup(l)); }

  AddPtr lookupForAdd(const Lookup& l) {
    AddPtr p(map().lookupForAdd(l));
    if (p) {
      valueReadBarrier(p->value());
    }
    return p;
  }

  [[nodiscard]] bool add(AddPtr& p, const Key& k, const Value& v) {
    MOZ_ASSERT(gc::ToMarkable(k));
    writeBarrier(k, v);
    return map().add(p.ptr, k, v);
  }

  [[nodiscard]] bool relookupOrAdd(AddPtr& p, const Key& k, const Value& v) {
    MOZ_ASSERT(gc::ToMarkable(k));
    writeBarrier(k, v);
    return map().relookupOrAdd(p.ptr, k, v);
  }

  [[nodiscard]] bool put(const Key& k, const Value& v) {
    MOZ_ASSERT(gc::ToMarkable(k));
    writeBarrier(k, v);
    return map().put(k, v);
  }

  [[nodiscard]] bool putNew(const Key& k, const Value& v) {
    MOZ_ASSERT(gc::ToMarkable(k));
    writeBarrier(k, v);
    return map().putNew(k, v);
  }

  void clear() {
    map().clear();
    nurseryKeys.clear();
    nurseryKeysValid = true;
    mayHaveSymbolKeys = false;
    if (!isSystem()) {
      mayHaveObjectKeyDelegates = false;
    }
  }

#ifdef DEBUG
  bool hasEntry(const Key& key, const Value& value) const {
    Ptr p = lookupUnbarriered(key);
    return p && p->value() == value;
  }
#endif

  bool markEntry(GCMarker* marker, gc::CellColor mapColor, ModIterator& iter,
                 bool populateWeakKeysTable);

  void trace(JSTracer* trc) override;

  // Used by the debugger to trace cross-compartment edges.
  void traceKeys(JSTracer* trc);
  void traceKey(JSTracer* trc, ModIterator& iter);

  size_t shallowSizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf);

  static size_t offsetOfHashShift() {
    return offsetof(WeakMap, map_) + UnbarrieredMap::offsetOfHashShift();
  }
  static size_t offsetOfTable() {
    return offsetof(WeakMap, map_) + UnbarrieredMap::offsetOfTable();
  }
  static size_t offsetOfEntryCount() {
    return offsetof(WeakMap, map_) + UnbarrieredMap::offsetOfEntryCount();
  }

 protected:
  inline void assertMapIsSameZoneWithValue(const BarrieredValue& v);

  bool markEntries(GCMarker* marker) override;

  // Find sweep group edges for delegates, if the key type has delegates. (If
  // not, the optimizer should make this a nop.)
  bool findSweepGroupEdges(Zone* atomsZone) override;

#if DEBUG
  void assertEntriesNotAboutToBeFinalized();
  void checkCachedFlags() const override;
#endif

#ifdef JS_GC_ZEAL
  bool checkMarking() const override;
#endif

#ifdef JSGC_HASH_TABLE_CHECKS
  void checkAfterMovingGC() const override;
#endif

 private:
  static void staticAssertions();

  // Map accessor uses a cast to add barriers.
  Map& map() { return reinterpret_cast<Map&>(map_); }
  const Map& map() const { return reinterpret_cast<const Map&>(map_); }

  MutablePtr lookupMutableUnbarriered(const Lookup& l) {
    return map().lookup(l);
  }

  void valueReadBarrier(const JS::Value& v) const {
    // js::jit::WeakMapValueReadBarrier is a specialized version of this
    // function designed to be called from jitcode. If this code is changed, it
    // should be kept in sync.
    JS::ExposeValueToActiveJS(v);
    if (MOZ_UNLIKELY(v.isSymbol())) {
      gc::MarkSymbolForWeakMapReadBarrier(zone(), v.toSymbol());
    }
  }
  static void valueReadBarrier(JSObject* obj) {
    JS::ExposeObjectToActiveJS(obj);
  }
  static void valueReadBarrier(jit::JitCode* code) {
    gc::ExposeGCThingToActiveJS(JS::GCCellPtr(code));
  }

  void writeBarrier(const Key& key, const Value& value) {
    keyKindBarrier(key);
    nurseryEntryBarrier(key, value);
  }

  void keyKindBarrier(const JS::Value& key) {
    if (key.isSymbol()) {
      keyKindBarrier(key.toSymbol());
    }
    if (key.isObject()) {
      keyKindBarrier(&key.toObject());
    }
  }
  void keyKindBarrier(JSObject* key) {
    // Fast path for non-proxy objects.
    if (!IsProxy(key)) {
      MOZ_ASSERT(!ObjectMayBeSwapped(key));
      return;
    }
    keyKindBarrierSlow(key);
  }
  void keyKindBarrierSlow(JSObject* key) {
    if (!mayHaveObjectKeyDelegates) {
      JSObject* delegate = UncheckedUnwrapWithoutExpose(key);
      if (delegate != key || ObjectMayBeSwapped(key)) {
        setMayHaveKeyDelegates();
      }
    }
  }
  void keyKindBarrier(JS::Symbol* symbol) {
    if (!mayHaveSymbolKeys) {
      setMayHaveSymbolKeys();
    }
  }
  void keyKindBarrier(BaseScript* key) {}

  void nurseryEntryBarrier(const Key& key, const Value& value) {
    if ((gc::MightBeInNursery<Key>::value &&
         !JS::GCPolicy<Key>::isTenured(key)) ||
        (gc::MightBeInNursery<Value>::value &&
         !JS::GCPolicy<Value>::isTenured(value))) {
      if (!hasNurseryEntries) {
        setHasNurseryEntries();
      }

      addNurseryKey(key);
    }
  }

  void addNurseryKey(const Key& key);
  void setMayHaveSymbolKeys();
  void setMayHaveKeyDelegates();

  void traceWeakEdgesDuringSweeping(JSTracer* trc) override;

  void clearAndCompact() override {
    clear();
    map().compact();
    nurseryKeys.clearAndFree();
  }

  // memberOf can be nullptr, which means that the map is not part of a
  // JSObject.
  void traceMappings(WeakMapTracer* tracer) override;
  bool fixGrayMappings() override;

  bool traceNurseryEntriesOnMinorGC(JSTracer* trc) override;
  bool sweepAfterMinorGC() override;
};

} /* namespace js */

#endif /* gc_WeakMap_h */
