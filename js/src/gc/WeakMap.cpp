/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gc/PublicIterators.h"
#include "js/friend/CycleCollector.h"
#include "vm/JSObject.h"

#include "gc/AtomMarking-inl.h"
#include "gc/Marking-inl.h"
#include "gc/StoreBuffer-inl.h"
#include "gc/WeakMap-inl.h"

using namespace js;
using namespace js::gc;

void js::gc::MarkSymbolForWeakMapReadBarrier(JS::Zone* zone, JS::Symbol* sym) {
  MOZ_ASSERT(zone && !zone->isAtomsZone());
  zone->runtimeFromMainThread()->gc.atomReferences.inlinedRecordRefInfallible(
      zone, sym);
}

WeakMapBase::WeakMapBase(JSObject* memOf, Zone* zone)
    : memberOf(memOf), zone_(zone) {
  MOZ_ASSERT_IF(memberOf, memberOf->compartment()->zone() == zone);
  MOZ_ASSERT(!isMarked());

  if (zone->isGCMarking()) {
    setMapColor(CellColor::Black);
  }

  SlimLinkedList<WeakMapBase>* list;
  if (isSystem()) {
    list = &zone->gcSystemWeakMaps();
  } else if (isMarked()) {
    list = &zone->gcMarkedUserWeakMaps();
  } else {
    list = &zone->gcUserWeakMaps();
  }

#ifdef JS_GC_CONCURRENT_MARKING
  // Lock if background thread is marking concurrently.
  mozilla::Maybe<AutoLockGC> lock;
  if (!isSystem() && zone->needsMarkingBarrier(Zone::Concurrent)) {
    lock.emplace(zone->runtimeFromMainThread());
  }
#endif

  list->pushFront(this);
}

void WeakMapBase::unmarkZone(JS::Zone* zone) {
  zone->gcEphemeronEdges().clearAndCompact();
  ForAllWeakMapsInZone(
      zone, [](WeakMapBase* map) { map->setMapColor(CellColor::White); });
  zone->gcUserWeakMaps().append(std::move(zone->gcMarkedUserWeakMaps()));
  MOZ_ASSERT(zone->gcMarkedUserWeakMaps().isEmpty());
}

#ifdef DEBUG
void WeakMapBase::checkZoneUnmarked(JS::Zone* zone) {
  MOZ_ASSERT(zone->gcEphemeronEdges().empty());
  MOZ_ASSERT(zone->gcMarkedUserWeakMaps().isEmpty());
  ForAllWeakMapsInZone(zone, [](WeakMapBase* map) {
    MOZ_ASSERT(map->mapColor() == CellColor::White);
  });
}
#endif

void Zone::traceWeakMaps(JSTracer* trc) {
  MOZ_ASSERT(trc->weakMapAction() != JS::WeakMapTraceAction::Skip);
  ForAllWeakMapsInZone(this, [trc](WeakMapBase* map) { map->trace(trc); });
}

mozilla::Maybe<CellColor> WeakMapBase::markMap(MarkColor markColor) {
  // We may be marking in parallel here so use a compare exchange loop to handle
  // concurrent updates to the map color.
  //
  // The color increases monotonically; we don't downgrade from black to gray.
  //
  // We can attempt to mark gray after marking black when a barrier pushes the
  // map object onto the black mark stack when it's already present on the
  // gray mark stack, since this is marked later.

  uint32_t targetColor = uint32_t(markColor);

  for (;;) {
    uint32_t currentColor = mapColor_;

    if (currentColor >= targetColor) {
      return mozilla::Nothing();
    }

    if (mapColor_.compareExchange(currentColor, targetColor)) {
      return mozilla::Some(CellColor(currentColor));
    }
  }
}

bool WeakMapBase::addEphemeronEdgesForEntry(MarkColor mapColor,
                                            TenuredCell* key, Cell* delegate,
                                            TenuredCell* value) {
  if (delegate) {
    if (!delegate->isTenured()) {
      MOZ_ASSERT(false);
      // This case is probably not possible, or wasn't at the time of this
      // writing. It requires a tenured wrapper with a nursery wrappee delegate,
      // which is tough to create given that the wrapper has to be created after
      // its target, and in fact appears impossible because the delegate has to
      // be created after the GC begins to avoid being tenured at the beginning
      // of the GC, and adding the key to the weakmap will mark the key via a
      // pre-barrier. But still, handling this case is straightforward:

      // The delegate is already being kept alive in a minor GC since it has an
      // edge from a tenured cell (the key). Make sure the key stays alive too.
      delegate->storeBuffer()->putWholeCell(key);
    } else if (delegate->zone()->isGCMarking()) {
      if (!addEphemeronEdge(mapColor, &delegate->asTenured(), key)) {
        return false;
      }
    }
  }

  if (value && !addEphemeronEdge(mapColor, key, value)) {
    return false;
  }

  return true;
}

bool WeakMapBase::addEphemeronEdge(MarkColor color, gc::TenuredCell* src,
                                   gc::TenuredCell* dst) {
  // Add an implicit edge from |src| to |dst|.

  Zone* zone = src->zone();
  MOZ_ASSERT(zone->isGCMarking());

  auto& edgeTable = zone->gcEphemeronEdges();
  auto p = edgeTable.lookupForAdd(src);
  if (!p) {
    if (!edgeTable.add(p, src, EphemeronEdgeVector())) {
      return false;
    }
  }
  return p->value().emplaceBack(color, dst);
}

#if defined(JS_GC_ZEAL) || defined(DEBUG)
bool WeakMapBase::checkMarkingForZone(JS::Zone* zone) {
  // This is called at the end of marking.
  MOZ_ASSERT(zone->isGCMarking());

  bool ok = true;
  ForAllWeakMapsInZone(zone, [&ok](WeakMapBase* map) {
    if (map->isMarked() && !map->checkMarking()) {
      ok = false;
    }
  });

  return ok;
}
#endif

#ifdef JSGC_HASH_TABLE_CHECKS
/* static */
void WeakMapBase::checkWeakMapsAfterMovingGC(JS::Zone* zone) {
  ForAllWeakMapsInZone(zone,
                       [](WeakMapBase* map) { map->checkAfterMovingGC(); });
}
#endif

bool WeakMapBase::markZoneIteratively(JS::Zone* zone, GCMarker* marker) {
  MOZ_ASSERT(zone->isGCMarking());

  bool markedAny = false;
  ForAllWeakMapsInZone(zone, [&](WeakMapBase* map) {
    if (map->isMarked() && map->markEntries(marker)) {
      markedAny = true;
    }
  });
  return markedAny;
}

bool WeakMapBase::findSweepGroupEdgesForZone(JS::Zone* atomsZone,
                                             JS::Zone* mapZone) {
#ifdef DEBUG
  ForAllWeakMapsInZone(mapZone,
                       [](WeakMapBase* map) { map->checkCachedFlags(); });
#endif

  // Because this might involve iterating over all weakmap edges in the zone we
  // cache some information on the zone to allow us to avoid it if possible.
  //
  //  - mapZone->gcWeakMapsMayHaveSymbolKeys() is set if any weakmap may have
  //    symbol keys
  //
  //  - mapZone->gcUserWeakMapsMayHaveKeyDelegates() is set if any user weakmap
  //    may have key delegates
  //
  //  It's assumed that system weakmaps may have key delegates so these are
  //  always scanned. There are a limited number of these.

  if (mapZone->gcWeakMapsMayHaveSymbolKeys()) {
    MOZ_ASSERT(JS::Prefs::experimental_symbols_as_weakmap_keys());
    if (atomsZone->isGCMarking()) {
      if (!atomsZone->addSweepGroupEdgeTo(mapZone)) {
        return false;
      }
    }
  }

  for (WeakMapBase* map : mapZone->gcSystemWeakMaps()) {
    if (!map->findSweepGroupEdges(atomsZone)) {
      return false;
    }
  }

  if (mapZone->gcUserWeakMapsMayHaveKeyDelegates()) {
    for (WeakMapBase* map : mapZone->gcMarkedUserWeakMaps()) {
      if (!map->findSweepGroupEdges(atomsZone)) {
        return false;
      }
    }
    for (WeakMapBase* map : mapZone->gcUserWeakMaps()) {
      if (!map->findSweepGroupEdges(atomsZone)) {
        return false;
      }
    }
  }

  return true;
}

void Zone::sweepWeakMaps(JSTracer* trc) {
  MOZ_ASSERT(isGCSweeping());

  // These flags will be recalculated during sweeping.
  clearGCCachedWeakMapKeyData();

  // Sweep all system weakmaps.
  WeakMapBase* weakmap = gcSystemWeakMaps().getFirst();
  while (weakmap) {
    WeakMapBase* next = weakmap->getNext();
    if (weakmap->isMarked()) {
      // Sweep live map to remove dead entries.
      weakmap->traceWeakEdgesDuringSweeping(trc);
      // Unmark swept weak map.
      weakmap->setMapColor(CellColor::White);
    } else {
      // Clean up system weak maps now. This may remove store buffer entries.
      // TODO: Is this still necessary? There should be no nursery entries.
      AutoLockSweepingLock lock(trc->runtime());
      weakmap->clearAndCompact();
      gcSystemWeakMaps().remove(weakmap);
    }
    weakmap = next;
  }

  // Sweep marked user weakmaps.
  for (WeakMapBase* weakmap : gcMarkedUserWeakMaps()) {
    MOZ_ASSERT(weakmap->isMarked());
    MOZ_ASSERT(weakmap->memberOf->isMarkedAny());
    // Sweep live map to remove dead entries.
    weakmap->traceWeakEdgesDuringSweeping(trc);
    // Unmark swept weak map.
    weakmap->setMapColor(CellColor::White);
  }

  // Remove all dead user weakmaps without iterating the list. The data will be
  // cleaned up when their owning objects are finalized. This assumes user
  // weakmaps are allocated using the buffer allocator which will recover the
  // memory without explicit free.
#ifdef DEBUG
  for (WeakMapBase* weakmap : gcUserWeakMaps()) {
    MOZ_ASSERT(!weakmap->isMarked());
    MOZ_ASSERT(!weakmap->memberOf->isMarkedAny());
  }
#endif
  new (&gcUserWeakMaps()) SlimLinkedList<WeakMapBase>();
  gcUserWeakMaps() = std::move(gcMarkedUserWeakMaps());

  WeakMapBase::checkZoneUnmarked(this);
}

void WeakMapBase::traceAllMappings(WeakMapTracer* tracer) {
  JSRuntime* rt = tracer->runtime;
  for (ZonesIter zone(rt, SkipAtoms); !zone.done(); zone.next()) {
    ForAllWeakMapsInZone(zone, [tracer](WeakMapBase* map) {
      // The WeakMapTracer callback is not allowed to GC.
      JS::AutoSuppressGCAnalysis nogc;
      map->traceMappings(tracer);
    });
  }
}

void js::gc::ShouldWeakMappingEntryBeBlack(JSObject* map, Cell* key,
                                           Cell* delegate, Cell* value,
                                           bool* keyShouldBeBlack,
                                           bool* valueShouldBeBlack) {
  *keyShouldBeBlack = false;
  *valueShouldBeBlack = false;

  // If nothing that could be held alive by this entry is marked gray, return.
  bool keyMightNeedMarking = key && key->isMarkedGray();
  bool valueMightNeedMarking = value && value->isMarkedGray() &&
                               value->getTraceKind() != JS::TraceKind::String;
  if (!keyMightNeedMarking && !valueMightNeedMarking) {
    return;
  }

  if (!JS::IsCCTraceKind(key->getTraceKind())) {
    key = nullptr;
  }

  if (keyMightNeedMarking) {
    if (delegate && !delegate->isMarkedGray() &&
        (!map || !map->isMarkedGray())) {
      *keyShouldBeBlack = true;
    }
  }

  if (value && value->isMarkedGray() && (!key || !key->isMarkedGray()) &&
      (!map || !map->isMarkedGray()) &&
      value->getTraceKind() != JS::TraceKind::Shape) {
    *valueShouldBeBlack = true;
  }
}

/* static */
void WeakMapBase::fixAllGrayMappings(JSRuntime* rt) {
  bool anyMarked;
  do {
    anyMarked = false;
    for (ZonesIter zone(rt, SkipAtoms); !zone.done(); zone.next()) {
      ForAllWeakMapsInZone(zone, [&anyMarked](WeakMapBase* map) {
        if (map->fixGrayMappings()) {
          anyMarked = true;
        }
      });
    }
  } while (anyMarked);
}

namespace {

#if defined(JS_GC_ZEAL) || defined(DEBUG)
struct CheckWeakMappingGrayBitsTracer final : public WeakMapTracer {
  explicit CheckWeakMappingGrayBitsTracer(JSRuntime* rt) : WeakMapTracer(rt) {}

  void trace(JSObject* map, JS::GCCellPtr key, JS::GCCellPtr delegate,
             JS::GCCellPtr value) override {
    bool keyShouldBeBlack;
    bool valueShouldBeBlack;
    ShouldWeakMappingEntryBeBlack(map, key.asCell(), delegate.asCell(),
                                  value.asCell(), &keyShouldBeBlack,
                                  &valueShouldBeBlack);

    if (keyShouldBeBlack) {
      fprintf(stderr, "Weak mapping key %p of map %p should be black\n",
              key.asCell(), map);
      failed = true;
    }

    if (valueShouldBeBlack) {
      fprintf(stderr, "Weak mapping value %p of map %p should be black\n",
              value.asCell(), map);
      failed = true;
    }
  }

  bool failed = false;
};
#endif

}  // namespace

JS_PUBLIC_API void js::FixWeakMappingGrayBits(JSRuntime* rt) {
  MOZ_ASSERT(!JS::RuntimeHeapIsBusy());
  MOZ_ASSERT(!rt->gc.isIncrementalGCInProgress());
  MOZ_ASSERT(rt->gc.areGrayBitsValid());

  WeakMapBase::fixAllGrayMappings(rt);
}

#if defined(JS_GC_ZEAL) || defined(DEBUG)
JS_PUBLIC_API bool js::CheckWeakMappingGrayBits(JSRuntime* rt) {
  MOZ_ASSERT(!JS::RuntimeHeapIsBusy());
  MOZ_ASSERT(!rt->gc.isIncrementalGCInProgress());

  CheckWeakMappingGrayBitsTracer tracer(rt);
  WeakMapBase::traceAllMappings(&tracer);
  return !tracer.failed;
}
#endif

#if defined(JS_GC_ZEAL)

bool WeakMapBase::saveZoneMarkedWeakMaps(JS::Zone* zone,
                                         WeakMapColors& markedWeakMaps) {
  bool ok = true;
  ForAllWeakMapsInZone(zone, [&](WeakMapBase* map) {
    if (!markedWeakMaps.put(map, map->mapColor())) {
      ok = false;
    }
  });
  return ok;
}

void WeakMapBase::restoreMarkedWeakMaps(WeakMapColors& markedWeakMaps) {
  for (auto iter = markedWeakMaps.iter(); !iter.done(); iter.next()) {
    WeakMapBase* map = iter.get().key();
    MOZ_ASSERT(!map->isMarked());

    Zone* zone = map->zone();
    MOZ_ASSERT(zone->isGCMarking());

    CellColor color = iter.get().value();
    if (IsMarked(color)) {
      map->setMapColor(color);
      if (!map->isSystem()) {
        zone->gcUserWeakMaps().remove(map);
        zone->gcMarkedUserWeakMaps().pushFront(map);
      }
    }
  }
}

#endif  // JS_GC_ZEAL

void WeakMapBase::setHasNurseryEntries() {
  MOZ_ASSERT(!hasNurseryEntries);

  AutoEnterOOMUnsafeRegion oomUnsafe;

  GCRuntime* gc = &zone()->runtimeFromMainThread()->gc;
  if (!gc->nursery().addWeakMapWithNurseryEntries(this)) {
    oomUnsafe.crash("WeakMapBase::setHasNurseryEntries");
  }

  hasNurseryEntries = true;
}
