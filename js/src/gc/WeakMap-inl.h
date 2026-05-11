/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef gc_WeakMap_inl_h
#define gc_WeakMap_inl_h

#include "gc/WeakMap.h"

#include "mozilla/Maybe.h"

#include <algorithm>
#include <type_traits>

#include "gc/GCLock.h"
#include "gc/Marking.h"
#include "gc/Zone.h"
#include "js/Prefs.h"
#include "js/TraceKind.h"
#include "vm/JSContext.h"
#include "vm/SymbolType.h"

#include "gc/AtomMarking-inl.h"
#include "gc/Marking-inl.h"
#include "gc/StableCellHasher-inl.h"

namespace js {

template <typename F>
void ForAllWeakMapsInZone(Zone* zone, F&& func) {
  for (auto* list : {&zone->gcSystemWeakMaps(), &zone->gcUserWeakMaps(),
                     &zone->gcMarkedUserWeakMaps()}) {
    for (WeakMapBase* map : *list) {
      MOZ_ASSERT(map->isSystem() == (list == &zone->gcSystemWeakMaps()));
      func(map);
    }
  }
}

namespace gc::detail {

static inline bool IsObject(JSObject* obj) { return true; }
static inline bool IsObject(BaseScript* script) { return false; }
static inline bool IsObject(const JS::Value& value) { return value.isObject(); }

static inline bool IsSymbol(JSObject* obj) { return false; }
static inline bool IsSymbol(BaseScript* script) { return false; }
static inline bool IsSymbol(const JS::Value& value) { return value.isSymbol(); }

// Return the effective cell color given the current marking state.
// This must be kept in sync with ShouldMark in Marking.cpp.
template <typename T>
static CellColor GetEffectiveColor(GCMarker* marker, const T& item) {
  static_assert(!IsBarriered<T>::value, "Don't pass wrapper types");

  Cell* cell = ToMarkable(item);
  if (!cell->isTenured()) {
    return CellColor::Black;
  }

  const TenuredCell& t = cell->asTenured();
  if (!t.zoneFromAnyThread()->shouldMarkInZone(marker->markColor())) {
    return CellColor::Black;
  }
  MOZ_ASSERT(t.runtimeFromAnyThread() == marker->runtime());

  return t.color();
}

// If a wrapper is used as a key in a weakmap, the garbage collector should
// keep that object around longer than it otherwise would. We want to avoid
// collecting the wrapper (and removing the weakmap entry) as long as the
// wrapped object is alive (because the object can be rewrapped and looked up
// again). As long as the wrapper is used as a weakmap key, it will not be
// collected (and remain in the weakmap) until the wrapped object is
// collected.
template <typename T>
static inline JSObject* GetDelegate(const T& key) {
  static_assert(!IsBarriered<T>::value, "Don't pass wrapper types");
  static_assert(!std::is_same_v<T, gc::Cell*>, "Don't pass Cell*");

  // Only objects have delegates.
  if (!IsObject(key)) {
    return nullptr;
  }

  auto* obj = static_cast<JSObject*>(ToMarkable(key));
  JSObject* delegate = UncheckedUnwrapWithoutExpose(obj);
  if (delegate == obj) {
    return nullptr;
  }

  return delegate;
}

}  // namespace gc::detail

// Weakmap entry -> value edges are only visible if the map is traced, which
// only happens if the map zone is being collected. If the map and the value
// were in different zones, then we could have a case where the map zone is not
// collecting but the value zone is, and incorrectly free a value that is
// reachable solely through weakmaps.
template <class K, class V, class AP>
void WeakMap<K, V, AP>::assertMapIsSameZoneWithValue(const BarrieredValue& v) {
#ifdef DEBUG
  gc::Cell* cell = gc::ToMarkable(v);
  if (cell) {
    Zone* cellZone = cell->zoneFromAnyThread();
    MOZ_ASSERT(zone() == cellZone || cellZone->isAtomsZone());
  }
#endif
}

// Initial length chosen to give minimum table capacity on creation.
//
// Using the default initial length instead means we will often reallocate the
// table on sweep because it's too big for the number of entries.
static constexpr size_t InitialWeakMapLength = 0;

template <class K, class V, class AP>
WeakMap<K, V, AP>::WeakMap(JSContext* cx, JSObject* memOf)
    : WeakMapBase(memOf, cx->zone()),
      map_(AP(cx->zone()), InitialWeakMapLength),
      nurseryKeys(AP(cx->zone())) {
  staticAssertions();
  MOZ_ASSERT(memOf);
}

template <class K, class V, class AP>
WeakMap<K, V, AP>::WeakMap(JS::Zone* zone)
    : WeakMapBase(nullptr, zone),
      map_(AP(zone), InitialWeakMapLength),
      nurseryKeys(AP(zone)) {
  mayHaveKeyDelegates = true;  // Assume true for system maps.
  staticAssertions();
}

template <class K, class V, class AP>
/* static */
MOZ_ALWAYS_INLINE void WeakMap<K, V, AP>::staticAssertions() {
  static_assert(!IsBarriered<K>::value, "Don't use barriered types");
  static_assert(!IsBarriered<V>::value, "Don't use barriered types");

  // The object's TraceKind needs to be added to CC graph if this object is
  // used as a WeakMap key, otherwise the key is considered to be pointed from
  // somewhere unknown, and results in leaking the subgraph which contains the
  // key. See the comments in NoteWeakMapsTracer::trace for more details.
  if constexpr (std::is_pointer_v<K>) {
    using NonPtrType = std::remove_pointer_t<K>;
    static_assert(JS::IsCCTraceKind(NonPtrType::TraceKind),
                  "Object's TraceKind should be added to CC graph.");
  }
}

template <class K, class V, class AP>
WeakMap<K, V, AP>::~WeakMap() {
#ifdef DEBUG
  // Weak maps store their data in an unbarriered map (|map_|) meaning that no
  // barriers are run on destruction. This is safe because:

  // 1. Weak maps have GC lifetime except on construction failure, therefore no
  // prebarrier is required.
  MOZ_ASSERT_IF(!empty(),
                CurrentThreadIsGCSweeping() || CurrentThreadIsGCFinalizing());

  // 2. If we're finalizing a weak map due to GC then it cannot contain nursery
  // things, because we evicted the nursery at the start of collection and
  // writing a nursery thing into the table would require the map to be
  // live. Therefore no postbarrier is required.
  size_t i = 0;
  for (auto iter = this->iter(); !iter.done() && i < 1000; iter.next(), i++) {
    K key = iter.get().key();
    MOZ_ASSERT_IF(gc::ToMarkable(key), !IsInsideNursery(gc::ToMarkable(key)));
    V value = iter.get().value();
    MOZ_ASSERT_IF(gc::ToMarkable(value),
                  !IsInsideNursery(gc::ToMarkable(value)));
  }
#endif

  // This is necessary because debugger weak maps can get destroyed before
  // weakmap sweeping proper.
  if (isInList()) {
    MOZ_ASSERT(isSystem());
    zone()->gcSystemWeakMaps().remove(this);
  }
}

// If the entry is live, ensure its key and value are marked. Also make sure the
// key is at least as marked as min(map, delegate), so it cannot get discarded
// and then recreated by rewrapping the delegate.
//
// Optionally adds edges to the ephemeron edges table for any keys (or
// delegates) where future changes to their mark color would require marking the
// value (or the key).
template <class K, class V, class AP>
bool WeakMap<K, V, AP>::markEntry(GCMarker* marker, gc::CellColor mapColor,
                                  ModIterator& iter,
                                  bool populateWeakKeysTable) {
#ifdef DEBUG
  MOZ_ASSERT(isMarked());
  if (marker->isParallelMarking()) {
    marker->runtime()->gc.assertCurrentThreadHasLockedGC();
  }
#endif

  BarrieredKey& key = iter.get().mutableKey();
  BarrieredValue& value = iter.get().value();

  JSTracer* trc = marker->tracer();
  gc::Cell* keyCell = gc::ToMarkable(key);
  MOZ_ASSERT(keyCell);

  bool marked = false;
  CellColor markColor = AsCellColor(marker->markColor());
  CellColor keyColor = gc::detail::GetEffectiveColor(marker, key.get());

  bool keyIsSymbol = gc::detail::IsSymbol(key.get());
  MOZ_ASSERT(keyIsSymbol == (keyCell->getTraceKind() == JS::TraceKind::Symbol));
  if (keyIsSymbol && keyColor < markColor) {
    // For symbols, also check whether it it is referenced by an uncollected
    // zone, and if so mark it now.
    auto* sym = static_cast<JS::Symbol*>(keyCell);
    gc::GCRuntime* gc = &marker->runtime()->gc;
    if (gc->isSymbolReferencedByUncollectedZone(sym, marker->markColor())) {
      TraceEdge(trc, &key, "WeakMap symbol key");
      MOZ_ASSERT(gc::detail::GetEffectiveColor(marker, key.get()) == markColor);
      keyColor = markColor;
      marked = true;
    }
  }

  JSObject* delegate = gc::detail::GetDelegate(key.get());
  if (delegate) {
    CellColor delegateColor = gc::detail::GetEffectiveColor(marker, delegate);
    // The key needs to stay alive while both the delegate and map are live.
    CellColor proxyPreserveColor = std::min(delegateColor, mapColor);
    if (keyColor < proxyPreserveColor) {
      MOZ_ASSERT(markColor >= proxyPreserveColor);
      if (markColor == proxyPreserveColor) {
        traceKey(trc, iter);
        MOZ_ASSERT(keyCell->color() >= proxyPreserveColor);
        marked = true;
        keyColor = proxyPreserveColor;
      }
    }
  }

  gc::Cell* cellValue = gc::ToMarkable(value);
  if (IsMarked(keyColor)) {
    if (cellValue) {
      CellColor targetColor = std::min(mapColor, keyColor);
      CellColor valueColor = gc::detail::GetEffectiveColor(marker, value.get());
      if (valueColor < targetColor) {
        MOZ_ASSERT(markColor >= targetColor);
        if (markColor == targetColor) {
          TraceEdge(trc, &value, "WeakMap entry value");
          MOZ_ASSERT(cellValue->color() >= targetColor);
          marked = true;
        }
      }
    }
  }

  if (populateWeakKeysTable) {
    MOZ_ASSERT(trc->weakMapAction() == JS::WeakMapTraceAction::Expand);

    // Note that delegateColor >= keyColor because marking a key marks its
    // delegate, so we only need to check whether keyColor < mapColor to tell
    // this.
    if (keyColor < mapColor) {
      // The final color of the key is not yet known. Add an edge to the
      // relevant ephemerons table to ensure that the value will be marked if
      // the key is marked. If the key has a delegate, also add an edge to
      // ensure the key is marked if the delegate is marked.

      // Nursery values are added to the store buffer when writing them into
      // the entry (via HeapPtr), so they will always get tenured. There's no
      // need for a key->value ephemeron to keep them alive via the WeakMap.
      gc::TenuredCell* tenuredValue = nullptr;
      if (cellValue && cellValue->isTenured()) {
        tenuredValue = &cellValue->asTenured();
      }

      // Nursery key is treated as black, so cannot be less marked than the map.
      MOZ_ASSERT(keyCell->isTenured());

      if (!this->addEphemeronEdgesForEntry(AsMarkColor(mapColor),
                                           &keyCell->asTenured(), delegate,
                                           tenuredValue)) {
        marker->abortLinearWeakMarking();
      }
    }
  }

  return marked;
}

template <class K, class V, class AP>
void WeakMap<K, V, AP>::trace(JSTracer* trc) {
  MOZ_ASSERT(isInList());

  TraceEdge(trc, &memberOf, "WeakMap owner");

  // Trace memory owned by our containers but not their contents.
  TraceOwnedAllocs(trc, memberOf, map_, "WeakMap storage");
  TraceOwnedAllocs(trc, memberOf, nurseryKeys, "WeakMap nursery keys");

  if (trc->isMarkingTracer()) {
    MOZ_ASSERT(trc->weakMapAction() == JS::WeakMapTraceAction::Expand);
    GCMarker* marker = GCMarker::fromTracer(trc);
    mozilla::Maybe<gc::CellColor> markResult = markMap(marker->markColor());
    if (markResult.isSome()) {
      // Lock during parallel marking to synchronize updates to the weakmap
      // lists and ephemeron edges tables.
      mozilla::Maybe<AutoLockGC> lock;
      if (marker->isParallelMarking()) {
        lock.emplace(marker->runtime());
      }

      // Move marked user weakmaps from the unmarked list back to the main list.
      if (markResult.value() == gc::CellColor::White && !isSystem()) {
        zone()->gcUserWeakMaps().remove(this);
        zone()->gcMarkedUserWeakMaps().pushFront(this);
      }

      (void)markEntries(marker);
    }
    return;
  }

  if (trc->weakMapAction() == JS::WeakMapTraceAction::Skip) {
    return;
  }

  for (auto iter = modIter(); !iter.done(); iter.next()) {
    // Always trace all values (unless weakMapAction() is Skip).
    TraceEdge(trc, &iter.get().value(), "WeakMap entry value");

    // Trace keys only if weakMapAction() says to.
    if (trc->weakMapAction() == JS::WeakMapTraceAction::TraceKeysAndValues) {
      traceKey(trc, iter);
    }
  }
}

template <class K, class V, class AP>
void WeakMap<K, V, AP>::traceKey(JSTracer* trc, ModIterator& iter) {
  K key = iter.get().key();
  TraceWeakMapKeyEdge(trc, zone(), &key, "WeakMap entry key");
  if (key != iter.get().key()) {
    iter.rekey(key);
  }
}

template <class K, class V, class AP>
bool WeakMap<K, V, AP>::markEntries(GCMarker* marker) {
  // This method is called whenever the map's mark color changes. Mark values
  // (and keys with delegates) as required for the new color and populate the
  // ephemeron edges if we're in incremental marking mode.

  MOZ_ASSERT(isMarked());

  bool markedAny = false;

  // If we don't populate the weak keys table now then we do it when we enter
  // weak marking mode.
  bool populateWeakKeysTable =
      marker->incrementalWeakMapMarkingEnabled || marker->isWeakMarking();

#ifdef DEBUG
  if (populateWeakKeysTable && marker->isParallelMarking()) {
    marker->runtime()->gc.assertCurrentThreadHasLockedGC();
  }
#endif

  // Read the atomic color into a local variable so the compiler doesn't load it
  // every time.
  gc::CellColor mapColor = this->mapColor();

  for (auto iter = modIter(); !iter.done(); iter.next()) {
    if (markEntry(marker, mapColor, iter, populateWeakKeysTable)) {
      markedAny = true;
    }
  }

  return markedAny;
}

template <class K, class V, class AP>
void WeakMap<K, V, AP>::traceWeakEdgesDuringSweeping(JSTracer* trc) {
  // This is only used for sweeping but. This cannot move GC things.
  MOZ_ASSERT(trc->kind() == JS::TracerKind::Sweeping);
  MOZ_ASSERT(zone()->isGCSweeping());

  // Scan the map, removing all entries whose keys remain unmarked. Rebuild
  // cached key state at the same time.
  mayHaveSymbolKeys = false;
  if (!isSystem()) {
    mayHaveKeyDelegates = false;
  }

  mozilla::Maybe<ModIterator> iter;
  iter.emplace(modIter());
  bool removedEntries = false;
  for (; !iter->done(); iter->next()) {
#ifdef DEBUG
    K prior = iter->get().key();
#endif
    if (TraceWeakEdge(trc, &iter->get().mutableKey(), "WeakMap key")) {
      MOZ_ASSERT(iter->get().key() == prior);
      keyKindBarrier(iter->get().key());
    } else {
      iter->remove();
      removedEntries = true;
    }
  }

  // TODO: Shrink nurseryKeys storage?

  {
    // Destroy the iterator, taking the lock if we removed any entries as this
    // may result in shrinking the table.
    mozilla::Maybe<gc::AutoLockSweepingLock> lock;
    if (removedEntries) {
      lock.emplace(trc->runtime());
    }
    iter.reset();
  }

#if DEBUG
  // Once we've swept, all remaining edges should stay within the known-live
  // part of the graph.
  assertEntriesNotAboutToBeFinalized();
#endif
}

template <class K, class V, class AP>
void WeakMap<K, V, AP>::addNurseryKey(const K& key) {
  MOZ_ASSERT(hasNurseryEntries);  // Must be set before calling this.

  if (!nurseryKeysValid) {
    return;
  }

  // Don't bother recording every key if there a lot of them. We will scan the
  // map instead.
  bool tooManyKeys = nurseryKeys.length() >= map().count() / 2;

  if (tooManyKeys || !nurseryKeys.append(key)) {
    nurseryKeys.clear();
    nurseryKeysValid = false;
  }
}

template <class K, class V, class AP>
void WeakMap<K, V, AP>::setMayHaveSymbolKeys() {
  MOZ_ASSERT(!mayHaveSymbolKeys);
  mayHaveSymbolKeys = true;
  zone()->setGCWeakMapsMayHaveSymbolKeys();
}

template <class K, class V, class AP>
void WeakMap<K, V, AP>::setMayHaveKeyDelegates() {
  MOZ_ASSERT(!mayHaveKeyDelegates);
  MOZ_ASSERT(!isSystem());  // This flag is always set for system maps.
  mayHaveKeyDelegates = true;
  zone()->setGCWeakMapsMayHaveKeyDelegates();
}

template <class K, class V, class AP>
bool WeakMap<K, V, AP>::traceNurseryEntriesOnMinorGC(JSTracer* trc) {
  // Called on minor GC to trace nursery keys that have delegates and nursery
  // values. Nursery keys without delegates are swept at the end of minor GC if
  // they do not survive.

  MOZ_ASSERT(hasNurseryEntries);

  using Entry = typename Map::Entry;
  auto traceEntry = [trc](K& key,
                          const Entry& entry) -> std::tuple<bool, bool> {
    TraceEdge(trc, &entry.value(), "WeakMap nursery value");
    bool hasNurseryValue = !JS::GCPolicy<V>::isTenured(entry.value());

    MOZ_ASSERT(key == entry.key());
    JSObject* delegate = gc::detail::GetDelegate(gc::MaybeForwarded(key));
    if (delegate) {
      TraceManuallyBarrieredEdge(trc, &key, "WeakMap nursery key");
    }
    bool hasNurseryKey = !JS::GCPolicy<K>::isTenured(key);
    bool keyUpdated = key != entry.key();

    return {keyUpdated, hasNurseryKey || hasNurseryValue};
  };

  if (nurseryKeysValid) {
    nurseryKeys.mutableEraseIf([&](K& key) {
      auto ptr = lookupUnbarriered(key);
      if (!ptr) {
        if (!gc::IsForwarded(key)) {
          return true;
        }

        // WeakMap::trace might have marked the key in the table already so if
        // the key was forwarded try looking up the forwarded key too.
        //
        // TODO: Try to update cached nursery information there instead.
        key = gc::Forwarded(key);
        ptr = lookupUnbarriered(key);
        if (!ptr) {
          return true;
        }
      }

      auto [keyUpdated, hasNurseryKeyOrValue] = traceEntry(key, *ptr);

      if (keyUpdated) {
        map().rekeyAs(ptr->key(), key, key);
      }

      return !hasNurseryKeyOrValue;
    });
  } else {
    MOZ_ASSERT(nurseryKeys.empty());
    nurseryKeysValid = true;

    for (auto iter = modIter(); !iter.done(); iter.next()) {
      Entry& entry = iter.get();

      K key = entry.key();
      auto [keyUpdated, hasNurseryKeyOrValue] = traceEntry(key, entry);

      if (keyUpdated) {
        entry.mutableKey() = key;
        iter.rekey(key);
      }

      if (hasNurseryKeyOrValue) {
        addNurseryKey(key);
      }
    }
  }

  hasNurseryEntries = !nurseryKeysValid || !nurseryKeys.empty();

#ifdef DEBUG
  bool foundNurseryEntries = false;
  for (auto iter = this->iter(); !iter.done(); iter.next()) {
    if (!JS::GCPolicy<K>::isTenured(iter.get().key()) ||
        !JS::GCPolicy<V>::isTenured(iter.get().value())) {
      foundNurseryEntries = true;
    }
  }
  MOZ_ASSERT_IF(foundNurseryEntries, hasNurseryEntries);
#endif

  return !hasNurseryEntries;
}

template <class K, class V, class AP>
bool WeakMap<K, V, AP>::sweepAfterMinorGC() {
#ifdef DEBUG
  MOZ_ASSERT(hasNurseryEntries);
  bool foundNurseryEntries = false;
  for (auto iter = this->iter(); !iter.done(); iter.next()) {
    if (!JS::GCPolicy<K>::isTenured(iter.get().key()) ||
        !JS::GCPolicy<V>::isTenured(iter.get().value())) {
      foundNurseryEntries = true;
    }
  }
  MOZ_ASSERT(foundNurseryEntries);
#endif

  using Entry = typename Map::Entry;
  using Result = std::tuple<bool /* shouldRemove */, bool /* keyUpdated */,
                            bool /* hasNurseryKeyOrValue */>;
  auto sweepEntry = [](K& key, const Entry& entry) -> Result {
    bool hasNurseryValue = !JS::GCPolicy<V>::isTenured(entry.value());
    MOZ_ASSERT(!gc::IsForwarded(entry.value().get()));

    gc::Cell* keyCell = gc::ToMarkable(key);
    if (!gc::InCollectedNurseryRegion(keyCell)) {
      bool hasNurseryKey = !JS::GCPolicy<K>::isTenured(key);
      return {false, false, hasNurseryKey || hasNurseryValue};
    }

    if (!gc::IsForwarded(key)) {
      return {true, false, false};
    }

    key = gc::Forwarded(key);
    MOZ_ASSERT(key != entry.key());

    bool hasNurseryKey = !JS::GCPolicy<K>::isTenured(key);

    return {false, true, hasNurseryKey || hasNurseryValue};
  };

  if (nurseryKeysValid) {
    nurseryKeys.mutableEraseIf([&](K& key) {
      auto ptr = lookupMutableUnbarriered(key);
      if (!ptr) {
        if (!gc::IsForwarded(key)) {
          return true;
        }

        // WeakMap::trace might have marked the key in the table already so if
        // the key was forwarded try looking up the forwarded key too.
        //
        // TODO: Try to update cached nursery information there instead.
        key = gc::Forwarded(key);
        ptr = lookupMutableUnbarriered(key);
        if (!ptr) {
          return true;
        }
      }

      auto [shouldRemove, keyUpdated, hasNurseryKeyOrValue] =
          sweepEntry(key, *ptr);
      if (shouldRemove) {
        map().remove(ptr);
        return true;
      }

      if (keyUpdated) {
        map().rekeyAs(ptr->key(), key, key);
      }

      return !hasNurseryKeyOrValue;
    });
  } else {
    MOZ_ASSERT(nurseryKeys.empty());
    nurseryKeysValid = true;

    for (auto iter = modIter(); !iter.done(); iter.next()) {
      Entry& entry = iter.get();

      K key = entry.key();
      auto [shouldRemove, keyUpdated, hasNurseryKeyOrValue] =
          sweepEntry(key, entry);

      if (shouldRemove) {
        iter.remove();
        continue;
      }

      if (keyUpdated) {
        entry.mutableKey() = key;
        iter.rekey(key);
      }

      if (hasNurseryKeyOrValue) {
        addNurseryKey(key);
      }
    }
  }

  hasNurseryEntries = !nurseryKeysValid || !nurseryKeys.empty();

#ifdef DEBUG
  foundNurseryEntries = false;
  for (auto iter = this->iter(); !iter.done(); iter.next()) {
    if (!JS::GCPolicy<K>::isTenured(iter.get().key()) ||
        !JS::GCPolicy<V>::isTenured(iter.get().value())) {
      foundNurseryEntries = true;
    }
  }
  MOZ_ASSERT_IF(foundNurseryEntries, hasNurseryEntries);
#endif

  return !hasNurseryEntries;
}

// memberOf can be nullptr, which means that the map is not part of a JSObject.
template <class K, class V, class AP>
void WeakMap<K, V, AP>::traceMappings(WeakMapTracer* tracer) {
  for (auto iter = this->iter(); !iter.done(); iter.next()) {
    gc::Cell* key = gc::ToMarkable(iter.get().key());
    gc::Cell* value = gc::ToMarkable(iter.get().value());
    if (key && value) {
      tracer->trace(memberOf, JS::GCCellPtr(iter.get().key().get()),
                    JS::GCCellPtr(iter.get().value().get()));
    }
  }
}

#ifdef DEBUG
template <class K, class V, class AP>
void WeakMap<K, V, AP>::checkCachedFlags() const {
  MOZ_ASSERT_IF(!zone()->gcUserWeakMapsMayHaveKeyDelegates() && !isSystem(),
                !mayHaveKeyDelegates);
  MOZ_ASSERT_IF(!zone()->gcWeakMapsMayHaveSymbolKeys(), !mayHaveSymbolKeys);

  if (!mayHaveSymbolKeys || !mayHaveKeyDelegates) {
    for (auto iter = this->iter(); !iter.done(); iter.next()) {
      const K& key = iter.get().key();
      MOZ_ASSERT_IF(!mayHaveKeyDelegates, !gc::detail::GetDelegate(key));
      MOZ_ASSERT_IF(!mayHaveSymbolKeys, !gc::detail::IsSymbol(key));
    }
  }
}
#endif

template <class K, class V, class AP>
bool WeakMap<K, V, AP>::findSweepGroupEdges(Zone* atomsZone) {
  // For weakmap keys with delegates in a different zone, add a zone edge to
  // ensure that the delegate zone finishes marking before the key zone.

  // We keep this set for system maps.
  MOZ_ASSERT_IF(isSystem(), mayHaveKeyDelegates);

  if (mayHaveKeyDelegates) {
    for (auto iter = this->iter(); !iter.done(); iter.next()) {
      const K& key = iter.get().key();

      JSObject* delegate = gc::detail::GetDelegate(key);
      if (delegate) {
        // Marking a WeakMap key's delegate will mark the key, so process the
        // delegate zone no later than the key zone.
        Zone* delegateZone = delegate->zone();
        gc::Cell* keyCell = gc::ToMarkable(key);
        MOZ_ASSERT(keyCell);
        Zone* keyZone = keyCell->zone();
        if (delegateZone != keyZone && delegateZone->isGCMarking() &&
            keyZone->isGCMarking()) {
          if (!delegateZone->addSweepGroupEdgeTo(keyZone)) {
            return false;
          }
        }
      }
    }
  }

  return true;
}

template <class K, class V, class AP>
size_t WeakMap<K, V, AP>::shallowSizeOfExcludingThis(
    mozilla::MallocSizeOf mallocSizeOf) {
  return SizeOfOwnedAllocs(map(), mallocSizeOf) +
         SizeOfOwnedAllocs(nurseryKeys, mallocSizeOf);
}

#if DEBUG
template <class K, class V, class AP>
void WeakMap<K, V, AP>::assertEntriesNotAboutToBeFinalized() {
  for (auto iter = this->iter(); !iter.done(); iter.next()) {
    K k = iter.get().key();
    MOZ_ASSERT(!gc::IsAboutToBeFinalizedUnbarriered(k));
    JSObject* delegate = gc::detail::GetDelegate(k);
    if (delegate) {
      MOZ_ASSERT(!gc::IsAboutToBeFinalizedUnbarriered(delegate),
                 "weakmap marking depends on a key tracing its delegate");
    }
    MOZ_ASSERT(!gc::IsAboutToBeFinalized(iter.get().value()));
  }
}
#endif

#ifdef JS_GC_ZEAL
template <class K, class V, class AP>
bool WeakMap<K, V, AP>::checkMarking() const {
  bool ok = true;
  for (auto iter = this->iter(); !iter.done(); iter.next()) {
    gc::Cell* key = gc::ToMarkable(iter.get().key());
    MOZ_RELEASE_ASSERT(key);
    gc::Cell* value = gc::ToMarkable(iter.get().value());
    if (!gc::CheckWeakMapEntryMarking(this, key, value)) {
      ok = false;
    }
  }
  return ok;
}
#endif

#ifdef JSGC_HASH_TABLE_CHECKS
template <class K, class V, class AP>
void WeakMap<K, V, AP>::checkAfterMovingGC() const {
  MOZ_RELEASE_ASSERT(!hasNurseryEntries);
  MOZ_RELEASE_ASSERT(nurseryKeysValid);
  MOZ_RELEASE_ASSERT(nurseryKeys.empty());

  for (auto iter = this->iter(); !iter.done(); iter.next()) {
    gc::Cell* key = gc::ToMarkable(iter.get().key());
    gc::Cell* value = gc::ToMarkable(iter.get().value());
    CheckGCThingAfterMovingGC(key);
    if (!allowKeysInOtherZones()) {
      Zone* keyZone = key->zoneFromAnyThread();
      MOZ_RELEASE_ASSERT(keyZone == zone() || keyZone->isAtomsZone());
    }
    CheckGCThingAfterMovingGC(value, zone());
    auto ptr = lookupUnbarriered(iter.get().key());
    MOZ_RELEASE_ASSERT(ptr.found() && &*ptr == &iter.get());
  }
}
#endif  // JSGC_HASH_TABLE_CHECKS

// https://tc39.es/ecma262/#sec-canbeheldweakly
static MOZ_ALWAYS_INLINE bool CanBeHeldWeakly(Value value) {
  // 1. If v is an Object, return true.
  if (value.isObject()) {
    return true;
  }

  bool symbolsAsWeakMapKeysEnabled =
      JS::Prefs::experimental_symbols_as_weakmap_keys();

  // 2. If v is a Symbol and KeyForSymbol(v) is undefined, return true.
  if (symbolsAsWeakMapKeysEnabled && value.isSymbol() &&
      value.toSymbol()->code() != JS::SymbolCode::InSymbolRegistry) {
    return true;
  }

  // 3. Return false.
  return false;
}

inline HashNumber GetSymbolHash(JS::Symbol* sym) { return sym->hash(); }

/* static */
inline void WeakMapKeyHasher<JS::Value>::checkValueType(const Value& value) {
  MOZ_ASSERT(CanBeHeldWeakly(value));
}

}  // namespace js

#endif /* gc_WeakMap_inl_h */
