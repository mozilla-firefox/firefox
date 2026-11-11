/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/*
 * Implementation of GC sweeping.
 *
 * In the SpiderMonkey GC, 'sweeping' is used to mean two things:
 *  - updating data structures to remove pointers to dead GC things and updating
 *    pointers to moved GC things
 *  - finalizing dead GC things
 *
 * Furthermore, the GC carries out gray and weak marking after the start of the
 * sweep phase. This is also implemented in this file.
 */

#include "mozilla/DebugOnly.h"
#include "mozilla/glue/Debug.h"
#include "mozilla/Maybe.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/TimeStamp.h"

#include "builtin/FinalizationRegistryObject.h"
#include "builtin/WeakRefObject.h"
#include "debugger/DebugAPI.h"
#include "gc/AllocKind.h"
#include "gc/BufferAllocator.h"
#include "gc/FinalizationObservers.h"
#include "gc/GCInternals.h"
#include "gc/GCLock.h"
#include "gc/GCProbes.h"
#include "gc/GCRuntime.h"
#include "gc/ParallelWork.h"
#include "gc/Statistics.h"
#include "gc/TraceKind.h"
#include "gc/WeakMap.h"
#include "gc/Zone.h"
#include "jit/CacheIRHealth.h"
#include "jit/JitFrames.h"
#include "jit/JitRuntime.h"
#include "jit/JitScript.h"
#include "jit/JitZone.h"
#include "proxy/DeadObjectProxy.h"
#include "vm/BigIntType.h"
#include "vm/CodeCoverage.h"
#include "vm/HelperThreads.h"
#include "vm/JSContext.h"
#include "vm/Probes.h"
#include "vm/Time.h"
#include "vm/WrapperObject.h"

#include "gc/AtomMarking-inl.h"
#include "gc/PrivateIterators-inl.h"
#include "vm/GeckoProfiler-inl.h"
#include "vm/JSObject-inl.h"
#include "vm/JSScript-inl.h"
#include "vm/PropMap-inl.h"
#include "vm/Shape-inl.h"
#include "vm/StringType-inl.h"

using namespace js;
using namespace js::gc;

using mozilla::DebugOnly;
using mozilla::TimeStamp;

using JS::SliceBudget;

/*
 * Sweeping of arenas and possible finalization of dead cells proceeds in a
 * sequence of phases.
 *
 *  1. ForegroundObjectFinalizePhase
 *     JSObjects with finalizers. Swept incrementally on the main thread.
 *
 *  2. ForegroundNonObjectFinalizePhase
 *     Non-JSObjects with finalizers. Swept incrementally on the main thread.
 *
 *  3. BackgroundObjectFinalizePhase
 *     JSObjects with finalizers that can run off main thread. Swept
 *     non-incrementally on a helper thread.
 *
 *  4. BackgroundTrivialFinalizePhase
 *     Everything else. These may or may not have finalizers. Any finalizers
 *     must not delete HeapPtrs. Swept non-incrementally on a helper thread.
 */

static constexpr AllocKinds ForegroundObjectFinalizePhase = {
    AllocKind::OBJECT0_FOREGROUND, AllocKind::OBJECT2_FOREGROUND,
    AllocKind::OBJECT4_FOREGROUND, AllocKind::OBJECT6_FOREGROUND,
    AllocKind::OBJECT8_FOREGROUND, AllocKind::OBJECT12_FOREGROUND,
    AllocKind::OBJECT16_FOREGROUND};

static constexpr AllocKinds ForegroundNonObjectFinalizePhase = {
    AllocKind::SCRIPT, AllocKind::JITCODE};

static constexpr AllocKinds BackgroundObjectFinalizePhase = {
    AllocKind::OBJECT0_BACKGROUND, AllocKind::OBJECT2_BACKGROUND,
    AllocKind::ARRAYBUFFER4,       AllocKind::OBJECT4_BACKGROUND,
    AllocKind::ARRAYBUFFER6,       AllocKind::OBJECT6_BACKGROUND,
    AllocKind::ARRAYBUFFER8,       AllocKind::OBJECT8_BACKGROUND,
    AllocKind::ARRAYBUFFER12,      AllocKind::OBJECT12_BACKGROUND,
    AllocKind::ARRAYBUFFER16,      AllocKind::OBJECT16_BACKGROUND};

static constexpr AllocKinds BackgroundTrivialFinalizePhase = {
    AllocKind::FUNCTION,        AllocKind::FUNCTION_EXTENDED,
    AllocKind::OBJECT0,         AllocKind::OBJECT2,
    AllocKind::OBJECT4,         AllocKind::OBJECT6,
    AllocKind::OBJECT8,         AllocKind::OBJECT12,
    AllocKind::OBJECT16,        AllocKind::SCOPE,
    AllocKind::REGEXP_SHARED,   AllocKind::FAT_INLINE_STRING,
    AllocKind::STRING,          AllocKind::EXTERNAL_STRING,
    AllocKind::FAT_INLINE_ATOM, AllocKind::ATOM,
    AllocKind::SYMBOL,          AllocKind::BIGINT,
    AllocKind::SHAPE,           AllocKind::BASE_SHAPE,
    AllocKind::GETTER_SETTER,   AllocKind::COMPACT_PROP_MAP,
    AllocKind::NORMAL_PROP_MAP, AllocKind::DICT_PROP_MAP};

static constexpr AllocKinds AllBackgroundSweptKinds =
    BackgroundObjectFinalizePhase + BackgroundTrivialFinalizePhase;

static constexpr size_t ArenaReleaseBatchSize = 32;

template <typename T, FinalizeKind finalizeKind>
inline size_t Arena::finalize(JS::GCContext* gcx, AllocKind thingKind,
                              size_t thingSize) {
  /* Enforce requirements on size of T. */
  MOZ_ASSERT(thingSize % CellAlignBytes == 0);
  MOZ_ASSERT(thingSize >= MinCellSize);
  MOZ_ASSERT(thingSize <= 255);

  MOZ_ASSERT(allocated());
  MOZ_ASSERT(thingKind == getAllocKind());
  MOZ_ASSERT(thingSize == getThingSize());
  MOZ_ASSERT(!onDelayedMarkingList_);

  MOZ_ASSERT(finalizeKind == GetFinalizeKind(thingKind));

  uint_fast16_t freeStart = firstThingOffset(thingKind);

  // Update the free list as we go along. The cell iterator will always be ahead
  // of this pointer when it is written through, so the write will not interfere
  // with the iteration.
  FreeSpan* newListTail = &firstFreeSpan;

  size_t nmarked = 0;
  size_t nfinalized = 0;

  for (ArenaCellIterUnderFinalize cell(this); !cell.done(); cell.next()) {
    T* t = cell.as<T>();
    if (TenuredThingIsMarkedAny(t)) {
      uint_fast16_t thing = uintptr_t(t) & ArenaMask;
      if (thing != freeStart) {
        // We just finished passing over one or more free things,
        // so record a new FreeSpan.
        newListTail->initBounds(freeStart, thing - thingSize, this);
        newListTail = newListTail->nextSpanUnchecked(this);
      }
      freeStart = thing + thingSize;
      nmarked++;
    } else {
      if constexpr (std::is_same_v<T, JSObject>) {
        js::probes::FinalizeObject(t);
      }
      if constexpr (finalizeKind != FinalizeKind::None) {
        t->finalize(gcx);
      }
      AlwaysPoison(t, JS_SWEPT_TENURED_PATTERN, thingSize,
                   MemCheckKind::MakeUndefined);
      gcprobes::TenuredFinalize(t);
      nfinalized++;
    }
  }

  if constexpr (std::is_same_v<T, JSObject> || std::is_same_v<T, JSString> ||
                std::is_same_v<T, JS::BigInt>) {
    if (isNewlyCreated_) {
      auto& pretenuring = zone()->pretenuring;
      JS::TraceKind kind = JS::MapTypeToTraceKind<T>::kind;
      size_t nalloced = nmarked + nfinalized;
      pretenuring.updateCellCountsInNewlyCreatedArenas(kind, nalloced, nmarked);
    }
  }
  isNewlyCreated_ = 0;

  if (freeStart == ArenaSize) {
    // If the last thing was marked, we will have already set the bounds of
    // the final span, and we just need to terminate the list.
    newListTail->initAsEmpty();
  } else {
    // Otherwise, end the list with a span that covers the final stretch of free
    // things.
    newListTail->initFinal(freeStart, ArenaSize - thingSize, this);
  }

#ifdef DEBUG
  size_t nfree = numFreeThings(thingSize);
  MOZ_ASSERT(nfree + nmarked == thingsPerArena(thingKind));
#endif

  return nmarked;
}

// Finalize arenas from src list, releasing empty arenas if keepArenas wasn't
// specified and inserting the others into the appropriate destination size
// bins.
template <typename T, FinalizeKind finalizeKind, ReleaseEmpty releaseEmpty>
static inline bool FinalizeTypedArenas(JS::GCContext* gcx, ArenaList& src,
                                       SortedArenaList& dest,
                                       AllocKind thingKind,
                                       SliceBudget& budget) {
  MOZ_ASSERT(gcx->isFinalizing());

  size_t thingSize = Arena::thingSize(thingKind);
  size_t thingsPerArena = Arena::thingsPerArena(thingKind);
  size_t markCount = 0;
  size_t emptyCount = 0;

  GCRuntime* gc = gcx->gcRuntimeFromAnyThread();
  auto updateMarkCount = mozilla::MakeScopeExit(
      [&] { gc->stats().addCount(gcstats::COUNT_CELLS_MARKED, markCount); });

  while (!src.isEmpty()) {
    Arena* arena = src.popFront();
    size_t nmarked =
        arena->finalize<T, finalizeKind>(gcx, thingKind, thingSize);
    size_t nfree = thingsPerArena - nmarked;

    markCount += nmarked;

    dest.insertAt(arena, nfree);

    if constexpr (bool(releaseEmpty)) {
      if (nmarked == 0) {
        emptyCount++;
        MOZ_ASSERT(emptyCount <= ArenaReleaseBatchSize);
        if (emptyCount == ArenaReleaseBatchSize) {
          Arena* emptyArenas = nullptr;
          dest.extractEmptyTo(&emptyArenas);
          emptyArenas =
              gc->releaseSomeEmptyArenas(emptyArenas->zone(), emptyArenas);
          MOZ_ASSERT(!emptyArenas);
          emptyCount = 0;
        }
      }
    }

    budget.step(thingsPerArena);
    if (budget.isOverBudget()) {
      return false;
    }
  }

  if constexpr (bool(releaseEmpty)) {
    if (emptyCount) {
      Arena* emptyArenas = nullptr;
      dest.extractEmptyTo(&emptyArenas);
      emptyArenas =
          gc->releaseSomeEmptyArenas(emptyArenas->zone(), emptyArenas);
      MOZ_ASSERT(!emptyArenas);
    }
  }

  return true;
}

/*
 * Finalize the list of areans.
 */
template <ReleaseEmpty releaseEmpty>
static bool FinalizeArenas(JS::GCContext* gcx, ArenaList& src,
                           SortedArenaList& dest, AllocKind thingKind,
                           SliceBudget& budget) {
  switch (thingKind) {
#define EXPAND_CASE(allocKind, _1, type, _2, finalizeKind, _3, _4)      \
  case AllocKind::allocKind:                                            \
    return FinalizeTypedArenas<type, FinalizeKind::finalizeKind,        \
                               releaseEmpty>(gcx, src, dest, thingKind, \
                                             budget);
    FOR_EACH_ALLOCKIND(EXPAND_CASE)
#undef EXPAND_CASE

    default:
      MOZ_CRASH("Invalid alloc kind");
  }
}

void GCRuntime::initBackgroundSweep(Zone* zone, JS::GCContext* gcx,
                                    const AllocKinds& kinds) {
  for (AllocKind kind : kinds) {
    zone->arenas.initBackgroundSweep(kind);
  }
}

void ArenaLists::initBackgroundSweep(AllocKind thingKind) {
  MOZ_ASSERT(IsBackgroundSwept(thingKind));
  MOZ_ASSERT(concurrentUse(thingKind) == ConcurrentUse::None);

  if (!collectingArenaList(thingKind).isEmpty()) {
    concurrentUse(thingKind) = ConcurrentUse::BackgroundFinalize;
  }
}

template <ReleaseEmpty releaseEmpty>
void ArenaLists::backgroundFinalize(JS::GCContext* gcx, AllocKind kind,
                                    Arena** empty) {
  MOZ_ASSERT(IsBackgroundSwept(kind));
  MOZ_ASSERT(bool(empty) != bool(releaseEmpty));

  ArenaList& arenas = collectingArenaList(kind);
  if (arenas.isEmpty()) {
    MOZ_ASSERT(concurrentUse(kind) == ConcurrentUse::None);
    return;
  }
  MOZ_ASSERT(concurrentUse(kind) == ConcurrentUse::BackgroundFinalize);

  SortedArenaList finalizedSorted(kind);

  auto unlimited = SliceBudget::unlimited();
  FinalizeArenas<releaseEmpty>(gcx, arenas, finalizedSorted, kind, unlimited);
  MOZ_ASSERT(arenas.isEmpty());

  if constexpr (!bool(releaseEmpty)) {
    finalizedSorted.extractEmptyTo(empty);
  }
  MOZ_ASSERT(!finalizedSorted.hasEmptyArenas());

  // Set the collectingArenaList to the possibly empty list of swept arenas
  // while holding the GC lock. Set concurrentUse to indicate to the main thread
  // that sweeping has finished.
  ArenaList sweptArenas = finalizedSorted.convertToArenaList();

  AutoLockGC lock(gcx->gcRuntimeFromAnyThread());
  collectingArenaList(kind) = std::move(sweptArenas);
  concurrentUse(kind) = ConcurrentUse::BackgroundFinalizeFinished;
}

void ArenaLists::mergeBackgroundSweptArenas() {
  // Merge swept arenas into main arena lists on the main thread.
  MOZ_ASSERT(CurrentThreadCanAccessRuntime(runtime()));

  for (AllocKind kind : AllBackgroundSweptKinds) {
    maybeMergeSweptArenas(kind);
  }
}

void ArenaLists::maybeMergeSweptArenas(AllocKind kind) {
  MOZ_ASSERT(zone_->isGCFinished());
  MOZ_ASSERT(concurrentUse(kind) != ConcurrentUse::BackgroundFinalize);

  if (concurrentUse(kind) == ConcurrentUse::BackgroundFinalizeFinished) {
    concurrentUse(kind) = ConcurrentUse::None;
    mergeSweptArenas(kind, collectingArenaList(kind));
  }

  MOZ_ASSERT(collectingArenaList(kind).isEmpty());
}

// This methods merges the following to get the final state of an arena
// list:
//  - swept arenas
//  - arenas allocated during marking
//  - arenas allocated during sweeping
void ArenaLists::mergeSweptArenas(AllocKind kind, ArenaList& sweptArenas) {
  MOZ_ASSERT(CurrentThreadCanAccessRuntime(runtime()));
  MOZ_ASSERT(concurrentUse(kind) == ConcurrentUse::None);

  arenaList(kind).prepend(std::move(sweptArenas));
}

void ArenaLists::queueForegroundThingsForSweep() {
  gcCompactPropMapArenasToUpdate =
      collectingArenaList(AllocKind::COMPACT_PROP_MAP).getFirst();
  gcNormalPropMapArenasToUpdate =
      collectingArenaList(AllocKind::NORMAL_PROP_MAP).getFirst();
}

void GCRuntime::sweepBackgroundThings(ZoneList& zones) {
  if (zones.isEmpty()) {
    return;
  }

  JS::GCContext* gcx = TlsGCContext.get();
  MOZ_ASSERT(gcx->isFinalizing());

  // Sweep zones in order. The atoms zone must be finalized last as other
  // zones may have direct pointers into it.
  while (!zones.isEmpty()) {
    Zone* zone = zones.removeFront();
    MOZ_ASSERT(zone->isGCFinished());

    TimeStamp startTime = TimeStamp::Now();

    ArenaLists& arenaLists = zone->arenas;
    Arena* emptyArenas = arenaLists.takeSweptEmptyArenas();

    // We must finalize kinds in the order specified at the top of this file.

    for (auto kind : BackgroundObjectFinalizePhase) {
      MOZ_ASSERT(IsBackgroundFinalized(kind));
      arenaLists.backgroundFinalize<ReleaseEmpty::No>(gcx, kind, &emptyArenas);
    }

    // Release any arenas that are now empty.
    //
    // Finalizers can still access the zone pointer in now-empty arenas because
    // of the write barrier in the HeapPtr destructor. This means we can't
    // release any empty arenas until all such finalizers have been run.
    //
    // At this point this has happened and we can release empty arenas
    // immediately from now on.

    AutoDisallowPreWriteBarrier disallowBarrier(gcx);

    while (emptyArenas) {
      emptyArenas = releaseSomeEmptyArenas(zone, emptyArenas);
    }

    // Now everything with a non-trivial finalizer has been finalized we can
    // sweep buffer memory.
    //
    // Note we depend on this happening before the BUFFER alloc kinds in
    // BackgroundTrivialFinalizePhase are swept!
    bool decommit = shouldDecommit() && DecommitEnabled();
    zone->bufferAllocator.sweepForMajorCollection(decommit);

    // TODO: The remaining sweeping work can be parallelised between multiple
    // threads.
    for (AllocKind kind : BackgroundTrivialFinalizePhase) {
      MOZ_ASSERT(IsBackgroundSwept(kind));
      arenaLists.backgroundFinalize<ReleaseEmpty::Yes>(gcx, kind);
    }

    // Record time spent sweeping this zone.
    TimeStamp endTime = TimeStamp::Now();
    zone->perZoneGCTime += endTime - startTime;
  }
}

Arena* GCRuntime::releaseSomeEmptyArenas(Zone* zone, Arena* emptyArenas) {
  // Batch releases so as to periodically drop and reaquire the GC lock to
  // avoid blocking the main thread from allocating arenas. This is important
  // for allocation-heavy workloads such as the splay benchmark.
  //
  // This block is equivalent to calling GCRuntime::releaseArena on each arena
  // individually.
  bool isAtomsZone = zone->isAtomsZone();

  Arena* arenasToRelease[ArenaReleaseBatchSize];
  size_t atomsBitmapIndexes[ArenaReleaseBatchSize];
  size_t count = 0;

  size_t gcHeapBytesFreed = 0;

  // Take up to ArenaReleaseBatchSize arenas from emptyArenas list.
  for (size_t i = 0; emptyArenas && i < ArenaReleaseBatchSize; i++) {
    Arena* arena = emptyArenas;
    emptyArenas = arena->next;

    gcHeapBytesFreed += ArenaSize;

    if (isAtomsZone) {
      atomsBitmapIndexes[i] = arena->atomBitmapStart();
#ifdef DEBUG
      arena->atomBitmapStart() = 0;
#endif
    }

    arena->release();
    arenasToRelease[i] = arena;
    count++;
  }

  zone->gcHeapSize.removeBytes(gcHeapBytesFreed, true, heapSize);

  AutoLockGC lock(this);
  for (size_t i = 0; i < count; i++) {
    Arena* arena = arenasToRelease[i];
    if (isAtomsZone) {
      atomReferences.freeIndex(atomsBitmapIndexes[i], lock);
    }
    arena->chunk()->releaseArena(this, arena, lock);
  }

  return emptyArenas;
}

void GCRuntime::assertBackgroundSweepingFinished() {
#ifdef DEBUG
  {
    AutoLockHelperThreadState lock;
    MOZ_ASSERT(backgroundSweepZones.ref().isEmpty());
  }

  for (ZonesIter zone(this, WithAtoms); !zone.done(); zone.next()) {
    for (auto kind : AllAllocKinds()) {
      MOZ_ASSERT_IF(state() == State::NotActive || state() >= State::Compact,
                    zone->arenas.collectingArenaList(kind).isEmpty());
      MOZ_ASSERT(zone->arenas.doneBackgroundFinalize(kind));
    }
  }
#endif
}

void GCRuntime::queueZonesAndStartBackgroundSweep(ZoneList&& zones) {
  {
    AutoLockHelperThreadState lock;
    MOZ_ASSERT(!requestSliceAfterBackgroundTask);
    backgroundSweepZones.ref().appendList(std::move(zones));
    if (useBackgroundThreads) {
      sweepTask.startOrRunIfIdle(lock);
    }
  }
  if (!useBackgroundThreads) {
    sweepTask.join();
    sweepTask.runFromMainThread();
  }
}

BackgroundSweepTask::BackgroundSweepTask(GCRuntime* gc)
    : GCParallelTask(gc, gcstats::PhaseKind::SWEEP, GCUse::Finalizing) {}

void BackgroundSweepTask::run(AutoLockHelperThreadState& lock) {
  gc->sweepFromBackgroundThread(lock);
}

void GCRuntime::sweepFromBackgroundThread(AutoLockHelperThreadState& lock) {
  do {
    ZoneList zones;
    zones.appendList(std::move(backgroundSweepZones.ref()));

    AutoUnlockHelperThreadState unlock(lock);
    sweepBackgroundThings(zones);

    // The main thread may call queueZonesAndStartBackgroundSweep() while this
    // is running so we must check there is no more work after releasing the
    // lock.
  } while (!backgroundSweepZones.ref().isEmpty());

  maybeRequestGCAfterBackgroundTask(lock);
}

void GCRuntime::waitBackgroundSweepEnd() {
  sweepTask.join();
  for (AllZonesIter zone(this); !zone.done(); zone.next()) {
    if (zone->isGCFinished()) {
      zone->arenas.mergeBackgroundSweptArenas();
    }
  }
  if (state() != State::Sweep) {
    assertBackgroundSweepingFinished();
  }
}

void GCRuntime::waitBackgroundDecommitEnd() { decommitTask.join(); }

void GCRuntime::startBackgroundFree() {
  AutoLockHelperThreadState lock;

  if (!hasBuffersForBackgroundFree()) {
    return;
  }

  freeTask.startOrRunIfIdle(lock);
}

BackgroundFreeTask::BackgroundFreeTask(GCRuntime* gc)
    : GCParallelTask(gc, gcstats::PhaseKind::NONE) {
  // This can occur outside GCs so doesn't have a stats phase.
}

void BackgroundFreeTask::run(AutoLockHelperThreadState& lock) {
  gc->freeFromBackgroundThread(lock);
}

void GCRuntime::freeFromBackgroundThread(AutoLockHelperThreadState& lock) {
  do {
    LifoAlloc lifoBlocks(JSContext::TEMP_LIFO_ALLOC_PRIMARY_CHUNK_SIZE,
                         js::BackgroundMallocArena);
    lifoBlocks.transferFrom(&lifoBlocksToFree.ref());

    Nursery::BufferSet buffers;
    std::swap(buffers, buffersToFreeAfterMinorGC.ref());

    Nursery::StringBufferVector stringBuffers;
    std::swap(stringBuffers, stringBuffersToReleaseAfterMinorGC.ref());

    AutoUnlockHelperThreadState unlock(lock);

    lifoBlocks.freeAll();

    JS::GCContext* gcx = TlsGCContext.get();
    for (auto iter = buffers.iter(); !iter.done(); iter.next()) {
      // Malloc memory associated with nursery objects is not tracked as these
      // are assumed to be short lived.
      gcx->freeUntracked(iter.get());
    }

    for (auto* buffer : stringBuffers) {
      buffer->Release();
    }
  } while (hasBuffersForBackgroundFree());
}

void GCRuntime::waitBackgroundFreeEnd() { freeTask.join(); }

template <class ZoneIterT>
IncrementalProgress GCRuntime::markWeakReferences(
    SliceBudget& incrementalBudget) {
  MOZ_ASSERT(!marker().isWeakMarking());

  gcstats::AutoPhase ap1(stats(), gcstats::PhaseKind::MARK_WEAK);

  auto unlimited = SliceBudget::unlimited();
  SliceBudget& budget =
      marker().incrementalWeakMapMarkingEnabled ? incrementalBudget : unlimited;

  // Ensure we don't return to the mutator while we're still in weak marking
  // mode.
  auto leaveOnExit =
      mozilla::MakeScopeExit([&] { marker().leaveWeakMarkingMode(); });

  // If enterWeakMarkingMode takes up at least 80% of a slice, finish marking
  // completely in the next slice before yielding again. This avoids the problem
  // where scanning gcEphemeronEdges (which must be done at the beginning of
  // each slice) takes longer than a slice and therefore no (or little) progress
  // can be made per slice.
  double progressBeforeEnterWMM = budget.progress();
  auto checkSlowEnter = mozilla::MakeScopeExit([&] {
    // Called only when returning NotFinished.
    if (budget.progress() - progressBeforeEnterWMM > 0.8) {
      // Overran the budget. Finish the marking synchronously in the next slice.
      // Repeatedly returning to the mutator would require re-scanning the full
      // edge table in every slice, and we already know that this will take up
      // most or all of a single slice budget.
      finishMarkingDuringSweeping = true;
    }
  });

  // The previous logic is for the first enterWeakMarkingMode slice. This logic
  // then kicks in for the next slice, to update the budget to actually keep
  // going.
  if (!budget.isUnlimited() && finishMarkingDuringSweeping) {
    JS_LOG(gc, Info, "enterWeakMarkingMode finishing marking in next slice");
    budget.keepGoing = true;
  }

  if (marker().enterWeakMarkingMode()) {
    // If there was an 'enter-weak-marking-mode' token in the queue, then it and
    // everything after it will still be in the queue so we can process them
    // now. If there is an 'abort-weak-marking-mode' then we will leave weak
    // marking mode.
    MOZ_ASSERT(marker().isWeakMarking());
    while (processTestMarkQueue() == QueueYielded) {
    };

    // Do not rely on the information about not-yet-marked weak keys that have
    // been collected by barriers. Clear out the gcEphemeronEdges entries and
    // rebuild the full table. Note that this a cross-zone operation; delegate
    // zone entries will be populated by map zone traversals, so everything
    // needs to be cleared first, then populated.
    if (!marker().incrementalWeakMapMarkingEnabled) {
      for (ZoneIterT zone(this); !zone.done(); zone.next()) {
        zone->gcEphemeronEdges().clearAndCompact();
      }
    }

    for (ZoneIterT zone(this); !zone.done(); zone.next()) {
      if (!marker().isWeakMarking()) {
        // Linear weak marking aborted by OOM or abort-weak-marking-mode action.
        break;
      }
      if (zone->enterWeakMarkingMode(&marker(), budget) == NotFinished) {
        return NotFinished;
      }
    }
  }

  bool markedAny = true;
  while (markedAny) {
    if (!marker().markUntilBudgetExhausted(budget)) {
      MOZ_ASSERT(marker().incrementalWeakMapMarkingEnabled);
      return NotFinished;
    }

    markedAny = false;

    if (!marker().isWeakMarking()) {
      for (ZoneIterT zone(this); !zone.done(); zone.next()) {
        markedAny |= WeakMapBase::markZoneIteratively(zone, &marker());
      }
    }
  }

  assertNoMarkingWork();
  checkSlowEnter.release();  // No need to lengthen next slice.

  return Finished;
}

IncrementalProgress GCRuntime::markWeakReferencesInCurrentGroup(
    SliceBudget& budget) {
  return markWeakReferences<SweepGroupZonesIter>(budget);
}

IncrementalProgress GCRuntime::markGrayRoots(SliceBudget& budget,
                                             gcstats::PhaseKind phase) {
  MOZ_ASSERT(marker().markColor() == MarkColor::Black);

  gcstats::AutoPhase ap(stats(), phase);

  {
    AutoSetMarkColor setColorGray(marker(), MarkColor::Gray);

    AutoUpdateLiveCompartments updateLive(this);
    marker().setRootMarkingMode(true);
    auto guard = mozilla::MakeScopeExit(
        [this]() { marker().setRootMarkingMode(false); });

    IncrementalProgress result =
        traceEmbeddingGrayRoots(marker().tracer(), budget);
    if (result == NotFinished) {
      return NotFinished;
    }

    traceIncomingCrossCompartmentEdgesForZoneGC(marker().tracer(), GrayEdges);
  }

  // Also mark any incoming cross compartment edges that were originally gray
  // but have been marked black by a barrier.
  traceIncomingCrossCompartmentEdgesForZoneGC(marker().tracer(), BlackEdges);

  return Finished;
}

IncrementalProgress GCRuntime::markAllWeakReferences() {
  SliceBudget budget = SliceBudget::unlimited();
  return markWeakReferences<GCZonesIter>(budget);
}

void GCRuntime::markAllGrayReferences(gcstats::PhaseKind phase) {
#ifdef DEBUG
  // Check zones are in the correct state to be marked.
  for (GCZonesIter zone(this); !zone.done(); zone.next()) {
    MOZ_ASSERT(zone->isGCMarkingBlackAndGray());
  }
#endif

  SliceBudget budget = SliceBudget::unlimited();
  markGrayRoots(budget, phase);
  drainMarkStack();
}

void GCRuntime::dropStringWrappers() {
  /*
   * String "wrappers" are dropped on GC because their presence would require
   * us to sweep the wrappers in all compartments every time we sweep a
   * compartment group.
   */
  for (ZonesIter zone(this, WithAtoms); !zone.done(); zone.next()) {
    zone->dropStringWrappersOnGC();
  }
}

/*
 * Group zones that must be swept at the same time.
 *
 * From the point of view of the mutator, groups of zones transition atomically
 * from marking to sweeping. If compartment A has an edge to an unmarked object
 * in compartment B, then we must not start sweeping A in a later slice than we
 * start sweeping B. That's because a write barrier in A could lead to the
 * unmarked object in B becoming marked. However, if we had already swept that
 * object, we would be in trouble.
 *
 * If we consider these dependencies as a graph, then all the compartments in
 * any strongly-connected component of this graph must start sweeping in the
 * same slice.
 *
 * Tarjan's algorithm is used to calculate the components.
 */

bool Compartment::findSweepGroupEdges() {
  Zone* source = zone();
  for (auto targetComp = wrappedObjectCompartments(); !targetComp.done();
       targetComp.next()) {
    Zone* target = targetComp->zone();

    if (!target->isGCMarking() || source->hasSweepGroupEdgeTo(target)) {
      continue;
    }

    for (auto iter = objectWrapperMappingsTo(targetComp); !iter.done();
         iter.next()) {
      JSObject* key = iter.get().key();
      MOZ_ASSERT(key->zone() == target);

      // Add an edge to the wrapped object's zone to ensure that the wrapper
      // zone is not still being marked when we start sweeping the wrapped zone.
      // As an optimization, if the wrapped object is already marked black there
      // is no danger of later marking and we can skip this.
      if (key->isMarkedBlack()) {
        continue;
      }

      if (!source->addSweepGroupEdgeTo(target)) {
        return false;
      }

      // We don't need to consider any more wrappers for this target
      // compartment since we already added an edge.
      break;
    }
  }

  return true;
}

bool Zone::findSweepGroupEdges(Zone* atomsZone) {
  MOZ_ASSERT_IF(this != atomsZone, !isAtomsZone());

  // Any zone may have a pointer to an atom in the atoms zone, and these aren't
  // in the cross compartment map.
  if (atomsZone->wasGCStarted() && !addSweepGroupEdgeTo(atomsZone)) {
    return false;
  }

  for (CompartmentsInZoneIter comp(this); !comp.done(); comp.next()) {
    if (!comp->findSweepGroupEdges()) {
      return false;
    }
  }

  if (atomsZone->wasGCStarted() &&
      gcFinalizationRegistriesMayHaveSymbolRegistrations_ &&
      !atomsZone->addSweepGroupEdgeTo(this)) {
    return false;
  }

  return WeakMapBase::findSweepGroupEdgesForZone(atomsZone, this);
}

bool GCRuntime::addEdgesForMarkQueue() {
#ifdef DEBUG
  // For testing only.
  //
  // Add edges between all objects mentioned in the test mark queue, since
  // otherwise they will get marked in a different order than their sweep
  // groups. Note that this is only done at the beginning of an incremental
  // collection, so it is possible for objects to be added later that do not
  // follow the sweep group ordering. These objects will wait until their sweep
  // group comes up, or will be skipped if their sweep group is already past.
  JS::Zone* prevZone = nullptr;
  for (Value val : testMarkQueue) {
    if (!val.isObject()) {
      continue;
    }
    JSObject* obj = &val.toObject();
    JS::Zone* zone = obj->zone();
    if (!zone->isGCMarking()) {
      continue;
    }
    if (prevZone && prevZone != zone) {
      if (!prevZone->addSweepGroupEdgeTo(zone)) {
        return false;
      }
    }
    prevZone = zone;
  }
#endif
  return true;
}

bool GCRuntime::findSweepGroupEdges() {
  for (GCZonesIter zone(this); !zone.done(); zone.next()) {
    if (!zone->findSweepGroupEdges(atomsZone())) {
      return false;
    }
  }

  if (!addEdgesForMarkQueue()) {
    return false;
  }

  return DebugAPI::findSweepGroupEdges(rt);
}

void GCRuntime::groupZonesForSweeping() {
#ifdef DEBUG
  for (ZonesIter zone(this, WithAtoms); !zone.done(); zone.next()) {
    MOZ_ASSERT(zone->gcSweepGroupEdges().empty());
  }
#endif

  JSContext* cx = rt->mainContextFromOwnThread();
  ZoneComponentFinder finder(cx);
  if (!isIncremental || !findSweepGroupEdges()) {
    finder.useOneComponent();
  }

  // Use one component for zeal modes that yield at specific points.
  if (useZeal && zealModeControlsYieldPoint()) {
    finder.useOneComponent();
  }

  for (GCZonesIter zone(this); !zone.done(); zone.next()) {
    MOZ_ASSERT(zone->isGCMarking());
    finder.addNode(zone);
  }
  sweepGroups = finder.getResultsList();
  currentSweepGroup = sweepGroups;
  sweepGroupIndex = 1;

  for (GCZonesIter zone(this); !zone.done(); zone.next()) {
    zone->clearSweepGroupEdges();
  }

#ifdef DEBUG
  unsigned idx = sweepGroupIndex;
  for (Zone* head = currentSweepGroup; head; head = head->nextGroup()) {
    for (Zone* zone = head; zone; zone = zone->nextNodeInGroup()) {
      MOZ_ASSERT(zone->isGCMarking());
      zone->gcSweepGroupIndex = idx;
    }
    idx++;
  }

  MOZ_ASSERT_IF(!isIncremental, !currentSweepGroup->nextGroup());
  for (ZonesIter zone(this, WithAtoms); !zone.done(); zone.next()) {
    MOZ_ASSERT(zone->gcSweepGroupEdges().empty());
  }
#endif
}

void GCRuntime::moveToNextSweepGroup() {
  currentSweepGroup = currentSweepGroup->nextGroup();
  ++sweepGroupIndex;
  if (!currentSweepGroup) {
    abortSweepAfterCurrentGroup = false;
    return;
  }

  MOZ_ASSERT_IF(abortSweepAfterCurrentGroup, !isIncremental);
  if (!isIncremental) {
    ZoneComponentFinder::mergeGroups(currentSweepGroup);
  }

  for (SweepGroupZonesIter zone(this); !zone.done(); zone.next()) {
    MOZ_ASSERT(zone->gcState() == zone->initialMarkingState());
    MOZ_ASSERT(!zone->isQueuedForBackgroundSweep());
  }

  if (abortSweepAfterCurrentGroup) {
    markTask.join();

    // Abort collection of subsequent sweep groups.
    for (SweepGroupZonesIter zone(this); !zone.done(); zone.next()) {
      MOZ_ASSERT(!zone->gcNextGraphComponent);
      zone->changeGCState(this, zone->initialMarkingState(), Zone::Finished);
      zone->arenas.unmarkPreMarkedFreeCells();
      zone->arenas.mergeArenasFromCollectingLists();
      zone->clearGCSliceThresholds();
      WeakMapBase::unmarkZone(zone);
#ifdef DEBUG
      zone->cellsToAssertNotGray().clearAndFree();
#endif
    }

    for (SweepGroupCompartmentsIter comp(rt); !comp.done(); comp.next()) {
      resetGrayList(comp);
    }

    abortSweepAfterCurrentGroup = false;
    currentSweepGroup = nullptr;
  }
}

/*
 * Gray marking:
 *
 * At the end of collection, anything reachable from a gray root that has not
 * otherwise been marked black must be marked gray.
 *
 * This means that when marking things gray we must not allow marking to leave
 * the current compartment group, as that could result in things being marked
 * gray when they might subsequently be marked black.  To achieve this, when we
 * find a cross compartment pointer we don't mark the referent but add it to a
 * singly-linked list of incoming gray pointers that is stored with each
 * compartment.
 *
 * The list head is stored in Compartment::gcIncomingGrayPointers and contains
 * cross compartment wrapper objects. The next pointer is stored in the second
 * extra slot of the cross compartment wrapper.
 *
 * The list is created during gray marking when one of the
 * MarkCrossCompartmentXXX functions is called for a pointer that leaves the
 * current compartent group.  This calls DelayCrossCompartmentGrayMarking to
 * push the referring object onto the list.
 *
 * The list is traversed and then unlinked in
 * GCRuntime::markIncomingGrayCrossCompartmentPointers.
 */

static bool IsGrayListObject(JSObject* obj) {
  MOZ_ASSERT(obj);
  return obj->is<CrossCompartmentWrapperObject>() && !IsDeadProxyObject(obj);
}

/* static */
unsigned ProxyObject::grayLinkReservedSlot(JSObject* obj) {
  MOZ_ASSERT(IsGrayListObject(obj));
  return CrossCompartmentWrapperObject::GrayLinkReservedSlot;
}

#ifdef DEBUG
static void AssertNotOnGrayList(JSObject* obj) {
  MOZ_ASSERT_IF(
      IsGrayListObject(obj),
      GetProxyReservedSlot(obj, ProxyObject::grayLinkReservedSlot(obj))
          .isUndefined());
}
#endif

static void AssertNoWrappersInGrayList(JSRuntime* rt) {
#ifdef DEBUG
  for (CompartmentsIter c(rt); !c.done(); c.next()) {
    MOZ_ASSERT(!c->gcIncomingGrayPointers);
    for (auto iter = c->objectWrapperMappings(); !iter.done(); iter.next()) {
      AssertNotOnGrayList(iter.get().value().unbarrieredGet());
    }
  }
#endif
}

static JSObject* CrossCompartmentPointerReferent(JSObject* obj) {
  MOZ_ASSERT(IsGrayListObject(obj));
  return &obj->as<ProxyObject>().private_().toObject();
}

static JSObject* NextIncomingCrossCompartmentPointer(JSObject* prev,
                                                     bool unlink) {
  unsigned slot = ProxyObject::grayLinkReservedSlot(prev);
  JSObject* next = GetProxyReservedSlot(prev, slot).toObjectOrNull();
  MOZ_ASSERT_IF(next, IsGrayListObject(next));

  if (unlink) {
    SetProxyReservedSlot(prev, slot, UndefinedValue());
  }

  return next;
}

void js::gc::DelayCrossCompartmentGrayMarking(GCMarker* maybeMarker,
                                              JSObject* src) {
  MOZ_ASSERT_IF(!maybeMarker, !JS::RuntimeHeapIsBusy());
  MOZ_ASSERT(IsGrayListObject(src));
  MOZ_ASSERT(src->isMarkedGray());

  AutoTouchingGrayThings tgt;

  mozilla::Maybe<AutoLockGC> lock;
  if (maybeMarker && maybeMarker->isParallelMarking()) {
    // Synchronize access to JSCompartment::gcIncomingGrayPointers.
    //
    // TODO: Instead of building this list we could scan all incoming CCWs and
    // mark through gray ones when marking gray roots for a sweep group.
    lock.emplace(maybeMarker->runtime());
  }

  /* Called from MarkCrossCompartmentXXX functions. */
  unsigned slot = ProxyObject::grayLinkReservedSlot(src);
  JSObject* dest = CrossCompartmentPointerReferent(src);
  Compartment* comp = dest->compartment();

  if (GetProxyReservedSlot(src, slot).isUndefined()) {
    SetProxyReservedSlot(src, slot,
                         ObjectOrNullValue(comp->gcIncomingGrayPointers));
    comp->gcIncomingGrayPointers = src;
  } else {
    MOZ_ASSERT(GetProxyReservedSlot(src, slot).isObjectOrNull());
  }

#ifdef DEBUG
  /*
   * Assert that the object is in our list, also walking the list to check its
   * integrity.
   */
  JSObject* obj = comp->gcIncomingGrayPointers;
  bool found = false;
  while (obj) {
    if (obj == src) {
      found = true;
    }
    obj = NextIncomingCrossCompartmentPointer(obj, false);
  }
  MOZ_ASSERT(found);
#endif
}

void GCRuntime::markIncomingGrayCrossCompartmentPointers() {
  gcstats::AutoPhase ap(stats(), gcstats::PhaseKind::MARK_INCOMING_GRAY);

  for (SweepGroupCompartmentsIter c(rt); !c.done(); c.next()) {
    MOZ_ASSERT(c->zone()->isGCMarkingBlackAndGray());
    MOZ_ASSERT_IF(c->gcIncomingGrayPointers,
                  IsGrayListObject(c->gcIncomingGrayPointers));

    for (JSObject* src = c->gcIncomingGrayPointers; src;
         src = NextIncomingCrossCompartmentPointer(src, true)) {
      JSObject* dst = CrossCompartmentPointerReferent(src);
      MOZ_ASSERT(dst->compartment() == c);
      MOZ_ASSERT_IF(src->asTenured().isMarkedBlack(),
                    dst->asTenured().isMarkedBlack());

      if (src->asTenured().isMarkedGray()) {
        TraceManuallyBarrieredEdge(marker().tracer(), &dst,
                                   "cross-compartment gray pointer");
      }
    }

    c->gcIncomingGrayPointers = nullptr;
  }
}

static bool RemoveFromGrayList(JSObject* wrapper) {
  AutoTouchingGrayThings tgt;

  if (!IsGrayListObject(wrapper)) {
    return false;
  }

  unsigned slot = ProxyObject::grayLinkReservedSlot(wrapper);
  if (GetProxyReservedSlot(wrapper, slot).isUndefined()) {
    return false; /* Not on our list. */
  }

  JSObject* tail = GetProxyReservedSlot(wrapper, slot).toObjectOrNull();
  SetProxyReservedSlot(wrapper, slot, UndefinedValue());

  Compartment* comp = CrossCompartmentPointerReferent(wrapper)->compartment();
  JSObject* obj = comp->gcIncomingGrayPointers;
  if (obj == wrapper) {
    comp->gcIncomingGrayPointers = tail;
    return true;
  }

  while (obj) {
    unsigned slot = ProxyObject::grayLinkReservedSlot(obj);
    JSObject* next = GetProxyReservedSlot(obj, slot).toObjectOrNull();
    if (next == wrapper) {
      js::detail::SetProxyReservedSlotUnchecked(obj, slot,
                                                ObjectOrNullValue(tail));
      return true;
    }
    obj = next;
  }

  MOZ_CRASH("object not found in gray link list");
}

void GCRuntime::resetGrayList(Compartment* comp) {
  JSObject* src = comp->gcIncomingGrayPointers;
  while (src) {
    src = NextIncomingCrossCompartmentPointer(src, true);
  }
  comp->gcIncomingGrayPointers = nullptr;
}

#ifdef DEBUG
static bool HasIncomingCrossCompartmentPointers(JSRuntime* rt) {
  for (SweepGroupCompartmentsIter c(rt); !c.done(); c.next()) {
    if (c->gcIncomingGrayPointers) {
      return true;
    }
  }

  return false;
}
#endif

void js::NotifyGCNukeWrapper(JSContext* cx, JSObject* wrapper) {
  MOZ_ASSERT(IsCrossCompartmentWrapper(wrapper));

  /*
   * References to target of wrapper are being removed, we no longer have to
   * remember to mark it.
   */
  RemoveFromGrayList(wrapper);
}

enum {
  JS_GC_SWAP_OBJECT_A_REMOVED = 1 << 0,
  JS_GC_SWAP_OBJECT_B_REMOVED = 1 << 1
};

unsigned js::NotifyGCPreSwap(JSObject* a, JSObject* b) {
  /*
   * Two objects in the same compartment are about to have had their contents
   * swapped.  If either of them are in our gray pointer list, then we remove
   * them from the lists, returning a bitset indicating what happened.
   */
  return (RemoveFromGrayList(a) ? JS_GC_SWAP_OBJECT_A_REMOVED : 0) |
         (RemoveFromGrayList(b) ? JS_GC_SWAP_OBJECT_B_REMOVED : 0);
}

void js::NotifyGCPostSwap(JSObject* a, JSObject* b, unsigned removedFlags) {
  /*
   * Two objects in the same compartment have had their contents swapped.  If
   * either of them were in our gray pointer list, we re-add them again.
   */
  if (removedFlags & JS_GC_SWAP_OBJECT_A_REMOVED) {
    DelayCrossCompartmentGrayMarking(nullptr, b);
  }
  if (removedFlags & JS_GC_SWAP_OBJECT_B_REMOVED) {
    DelayCrossCompartmentGrayMarking(nullptr, a);
  }
}

static inline void MaybeCheckWeakMapMarking(GCRuntime* gc) {
#if defined(JS_GC_ZEAL) || defined(DEBUG)

  bool shouldCheck;
#  if defined(DEBUG)
  shouldCheck = true;
#  else
  shouldCheck = gc->hasZealMode(ZealMode::CheckWeakMapMarking);
#  endif

  if (shouldCheck) {
    for (SweepGroupZonesIter zone(gc); !zone.done(); zone.next()) {
      MOZ_RELEASE_ASSERT(WeakMapBase::checkMarkingForZone(zone));
    }
  }

#endif
}

IncrementalProgress GCRuntime::beginMarkingSweepGroup(JS::GCContext* gcx,
                                                      SliceBudget& budget) {
#ifdef DEBUG
  MOZ_ASSERT(!markOnBackgroundThreadDuringSweeping);
  assertNoMarkingWork();
  for (auto& marker : markers) {
    MOZ_ASSERT(marker->markColor() == MarkColor::Black);
  }
#endif

  gcstats::AutoPhase ap(stats(), gcstats::PhaseKind::MARK);

  // Change state of current group to MarkBlackAndGray to restrict gray marking
  // to this group. Note that there may be pointers to the atoms zone, and these
  // will be marked through, as they are not marked with
  // TraceCrossCompartmentEdge.
  for (SweepGroupZonesIter zone(this); !zone.done(); zone.next()) {
    MOZ_ASSERT_IF(!zone->isGCMarkingBlackAndGray(),
                  zone->cellsToAssertNotGray().empty());
    zone->changeGCState(this, zone->initialMarkingState(),
                        Zone::MarkBlackAndGray);
  }

  AutoSetMarkColor setColorGray(marker(), MarkColor::Gray);

  // Mark incoming gray pointers from previously swept compartments.
  markIncomingGrayCrossCompartmentPointers();

  return Finished;
}

#ifdef DEBUG
bool GCRuntime::zoneInCurrentSweepGroup(Zone* zone) const {
  MOZ_ASSERT_IF(!zone->wasGCStarted(), !zone->gcNextGraphComponent);
  return zone->wasGCStarted() &&
         zone->gcNextGraphComponent == currentSweepGroup->nextGroup();
}
#endif

IncrementalProgress GCRuntime::markGrayRootsInCurrentGroup(
    JS::GCContext* gcx, SliceBudget& budget) {
  gcstats::AutoPhase ap(stats(), gcstats::PhaseKind::MARK);

  // Check that the zone state is set correctly for the current sweep group as
  // that determines what gets marked.
  MOZ_ASSERT(atomsZone()->wasGCStarted() ==
             atomsZone()->isGCMarkingBlackAndGray());
  for (NonAtomZonesIter zone(this); !zone.done(); zone.next()) {
    MOZ_ASSERT(zone->isGCMarkingBlackAndGray() ==
               zoneInCurrentSweepGroup(zone));
  }

  return markGrayRoots(budget, gcstats::PhaseKind::MARK_GRAY);
}

IncrementalProgress GCRuntime::markGray(JS::GCContext* gcx,
                                        SliceBudget& budget) {
  if (marker().isDrained()) {
    return Finished;
  }

  gcstats::AutoPhase ap(stats(), gcstats::PhaseKind::MARK);

  auto [mainThreadBudget, helperThreadBudget] = budgetConcurrentMarking(budget);

  if (markSynchronously(mainThreadBudget, useParallelMarking) == NotFinished) {
    MOZ_ASSERT(isIncremental);
    MOZ_ASSERT(safeToYield);

    maybeStartConcurrentMarking(helperThreadBudget);
    return NotFinished;
  }

  return Finished;
}

IncrementalProgress GCRuntime::endMarkingSweepGroup(JS::GCContext* gcx,
                                                    SliceBudget& budget) {
#ifdef DEBUG
  MOZ_ASSERT(!markOnBackgroundThreadDuringSweeping);
  assertNoMarkingWork();
  for (auto& marker : markers) {
    MOZ_ASSERT(marker->markColor() == MarkColor::Black);
  }
  MOZ_ASSERT(!HasIncomingCrossCompartmentPointers(rt));
#endif

  gcstats::AutoPhase ap(stats(), gcstats::PhaseKind::MARK);

  if (markWeakReferencesInCurrentGroup(budget) == NotFinished) {
    return NotFinished;
  }

  AutoSetMarkColor setColorGray(marker(), MarkColor::Gray);

  // Mark transitively inside the current compartment group.
  if (markWeakReferencesInCurrentGroup(budget) == NotFinished) {
    return NotFinished;
  }

  MOZ_ASSERT(marker().isDrained());
  MOZ_ASSERT(!hasAnyDeferredWeakMaps());

  // We must not yield after this point before we start sweeping the group.
  safeToYield = false;

  // If we temporarily prevented yielding during marking, release the hold now.
  budget.keepGoing = false;

  MaybeCheckWeakMapMarking(this);

  return Finished;
}

using WeakCacheToSweepVector = Vector<WeakCacheToSweep, 8, SystemAllocPolicy>;

static size_t ImmediateSweepWeakCache(GCRuntime* gc,
                                      const WeakCacheToSweep& item) {
  AutoSetThreadIsSweeping threadIsSweeping(item.zone);
  SweepingTracer trc(gc->rt);
  return item.cache->traceWeak(&trc, JS::detail::WeakCacheBase::Lock);
}

void GCRuntime::updateAtomsBitmap() {
  atomReferences.refineZoneBitmapsForCollectedZones(this);

  // Mark atoms used by uncollected zones after refining the atoms bitmaps.
  auto& atomsToMark = atomsUsedByUncollectedZones.ref();
  if (atomsToMark) {
    atomReferences.markAtomsUsedByUncollectedZones(this,
                                                   std::move(atomsToMark));
  }

  // For convenience sweep these tables non-incrementally as part of bitmap
  // sweeping; they are likely to be much smaller than the main atoms table.
  SweepingTracer trc(rt);
  rt->symbolRegistry().traceWeak(&trc);
}

void GCRuntime::sweepCCWrappers() {
  SweepingTracer trc(rt);
  trc.setAllowSweepingSymbolsEarly(true);
  for (SweepGroupZonesIter zone(this); !zone.done(); zone.next()) {
    zone->traceWeakCCWEdges(&trc);
  }
}

void GCRuntime::sweepRealmGlobals() {
  SweepingTracer trc(rt);
  for (SweepGroupRealmsIter r(this); !r.done(); r.next()) {
    AutoSetThreadIsSweeping threadIsSweeping(r->zone());
    r->traceWeakGlobalEdge(&trc);
  }
}

void GCRuntime::sweepWasmInstances() {
  for (SweepGroupRealmsIter r(this); !r.done(); r.next()) {
    AutoSetThreadIsSweeping threadIsSweeping(r->zone());
    r->wasm.traceWeakInstances();
  }
}

void GCRuntime::sweepMisc() {
  SweepingTracer trc(rt);
  for (SweepGroupRealmsIter r(this); !r.done(); r.next()) {
    AutoSetThreadIsSweeping threadIsSweeping(r->zone());
    r->traceWeakSavedStacks(&trc);
  }
  for (SweepGroupCompartmentsIter c(this); !c.done(); c.next()) {
    AutoSetThreadIsSweeping threadIsSweeping(c->zone());
    c->traceWeakNativeIterators(&trc);
  }
}

void GCRuntime::sweepCompressionTasks() {
  // Discard pending compression entries for ScriptSources that have no
  // other references.
  rt->pendingCompressions().eraseIf(
      [&](const auto& entry) { return entry.shouldCancel(); });

  // Attach finished compression tasks.
  AutoLockHelperThreadState lock;
  AttachFinishedCompressions(rt, lock);
}

void GCRuntime::sweepWeakMaps() {
  AutoSetThreadIsSweeping threadIsSweeping;  // Allow access to all zones.

  SweepingTracer trc(rt);
  for (SweepGroupZonesIter zone(this); !zone.done(); zone.next()) {
    /* No need to look up any more weakmap keys from this sweep group. */
    zone->gcEphemeronEdges().clearAndCompact();

    zone->sweepWeakMaps(&trc);
  }
}

void GCRuntime::sweepUniqueIds() {
  for (SweepGroupZonesIter zone(this); !zone.done(); zone.next()) {
    AutoSetThreadIsSweeping threadIsSweeping(zone);
    zone->sweepUniqueIds();
  }
}

void JS::Zone::sweepUniqueIds() {
  SweepingTracer trc(runtimeFromAnyThread());
  uniqueIds().traceWeak(&trc);
}

void GCRuntime::maybeWriteCoverageAndSpew() {
  // Write any code coverage information and JIT spew for dying scripts before
  // the relevant tables are swept.

  gcstats::AutoPhase ap(stats(), gcstats::PhaseKind::SWEEP_SCRIPT_MAPS);
  for (SweepGroupZonesIter zone(this); !zone.done(); zone.next()) {
    AutoSetThreadIsSweeping threadIsSweeping(zone);
    zone->maybeWriteCoverageAndSpew();
  }
}

void JS::Zone::maybeWriteCoverageAndSpew() {
  MOZ_ASSERT_IF(scriptLCovMap, coverage::IsLCovEnabled());
  if (scriptLCovMap) {
    for (auto iter = scriptLCovMap->get().iter(); !iter.done(); iter.next()) {
      if (IsAboutToBeFinalized(iter.get().key())) {
        (void)MaybeWriteScriptCoverage(iter.get().key()->asJSScript(),
                                       iter.get().value());
      }
    }
  }

#ifdef JS_CACHEIR_SPEW
  if (scriptFinalWarmUpCountMap) {
    for (auto iter = scriptFinalWarmUpCountMap->get().iter(); !iter.done();
         iter.next()) {
      if (IsAboutToBeFinalized(iter.get().key())) {
        BaseScript* base = iter.get().key();
        if (base->hasBytecode()) {
          JSScript* jsScript = base->asJSScript();
          if (jsScript->hasJitScript()) {
            maybeUpdateWarmUpCount(jsScript);
          }
          maybeSpewScriptFinalWarmUpCount(jsScript);
        }
      }
    }
  }
#endif
}

/* static */
bool UniqueIdGCPolicy::traceWeak(JSTracer* trc, Cell** keyp, uint64_t* valuep) {
  // Since this is only ever used for sweeping, we can optimize it for that
  // case. (Compacting GC updates this table manually when it moves a cell.)
  MOZ_ASSERT(trc->kind() == JS::TracerKind::Sweeping);
  return (*keyp)->isMarkedAny();
}

void GCRuntime::sweepFinalizationObserversOnMainThread() {
  // This calls back into the browser which expects to be called from the main
  // thread.
  gcstats::AutoPhase ap1(stats(), gcstats::PhaseKind::SWEEP_COMPARTMENTS);
  gcstats::AutoPhase ap2(stats(),
                         gcstats::PhaseKind::SWEEP_FINALIZATION_OBSERVERS);
  SweepingTracer trc(rt);
  AutoLockSweepingLock lock(rt);
  for (SweepGroupZonesIter zone(this); !zone.done(); zone.next()) {
    traceWeakFinalizationObserverEdges(&trc, zone);
  }
}

void GCRuntime::startTask(GCParallelTask& task,
                          AutoLockHelperThreadState& lock) {
  if (!CanUseExtraThreads()) {
    AutoUnlockHelperThreadState unlock(lock);
    task.runFromMainThread();
    stats().recordParallelPhase(task.phaseKind, task.duration());
    return;
  }

  task.startWithLockHeld(lock);
}

void GCRuntime::joinTask(GCParallelTask& task,
                         AutoLockHelperThreadState& lock) {
  gcstats::AutoPhase ap(stats(), gcstats::PhaseKind::JOIN_PARALLEL_TASKS);
  task.joinWithLockHeld(lock);
}

void GCRuntime::sweepDebuggerOnMainThread(JS::GCContext* gcx) {
  SweepingTracer trc(rt);
  AutoLockSweepingLock lock(rt);

  // Detach unreachable debuggers and global objects from each other.
  // This can modify weakmaps and so must happen before weakmap sweeping.
  DebugAPI::sweepAll(gcx);

  gcstats::AutoPhase ap(stats(), gcstats::PhaseKind::SWEEP_COMPARTMENTS);

  // Sweep debug environment information. This performs lookups in the Zone's
  // unique IDs table and so must not happen in parallel with sweeping that
  // table.
  {
    gcstats::AutoPhase ap2(stats(), gcstats::PhaseKind::SWEEP_MISC);
    for (SweepGroupRealmsIter r(rt); !r.done(); r.next()) {
      r->traceWeakDebugEnvironmentEdges(&trc);
    }
  }
}

void GCRuntime::sweepJitDataOnMainThread(JS::GCContext* gcx) {
  SweepingTracer trc(rt);

  // Allow sweeping currently unmarked symbols that might be marked later by
  // following a reference in another zone. We only really care about references
  // in the current zone and anything swept here is not observable.
  trc.setAllowSweepingSymbolsEarly(true);

  {
    gcstats::AutoPhase ap(stats(), gcstats::PhaseKind::SWEEP_JIT_DATA);

    // Bug 1071218: the following method has not yet been refactored to
    // work on a single zone-group at once.

    // Sweep entries containing about-to-be-finalized JitCode in the
    // JitcodeGlobalTable.
    jit::JitRuntime::TraceWeakJitcodeGlobalTable(rt, &trc);
  }

  // Trace weak edges in JitScripts to remove edges to dying GC things.
  {
    gcstats::AutoPhase apdc(stats(), gcstats::PhaseKind::SWEEP_JIT_SCRIPTS);
    for (SweepGroupZonesIter zone(this); !zone.done(); zone.next()) {
      zone->traceWeakJitScripts(&trc);
    }
  }

  // JitZone must be swept *after* sweeping JitScripts, because
  // Zone::traceWeakJitScripts might access CacheIRStubInfos deleted here.
  {
    gcstats::AutoPhase ap(stats(), gcstats::PhaseKind::SWEEP_JIT_DATA);

    for (SweepGroupZonesIter zone(this); !zone.done(); zone.next()) {
      if (jit::JitZone* jitZone = zone->jitZone()) {
        jitZone->traceWeak(&trc, zone);
      }
    }

    JSContext* cx = rt->mainContextFromOwnThread();
    jit::TraceWeakJitActivationsInSweepingZones(cx, &trc);
  }
}

void GCRuntime::sweepObjectsWithWeakPointers() {
  SweepingTracer trc(rt);
  for (SweepGroupZonesIter zone(this); !zone.done(); zone.next()) {
    AutoSetThreadIsSweeping threadIsSweeping(zone);
    zone->sweepObjectsWithWeakPointers(&trc);
  }
}

void JS::Zone::sweepObjectsWithWeakPointers(JSTracer* trc) {
  MOZ_ASSERT(trc->traceWeakEdges());

  objectsWithWeakPointers.ref().mutableEraseIf([&](JSObject*& obj) {
    if (!TraceManuallyBarrieredWeakEdge(trc, &obj, "objectsWithWeakPointers")) {
      // Object itself is dead.
      return true;
    }

    // Call trace hook to sweep weak pointers.
    obj->getClass()->doTrace(trc, obj);
    return false;
  });
}

// Call a functor for all weak caches that need to be swept in the current
// sweep group.
template <typename Functor>
static inline bool IterateWeakCaches(GCRuntime* gc, Functor f) {
  for (SweepGroupZonesIter zone(gc); !zone.done(); zone.next()) {
    for (JS::detail::WeakCacheBase* cache : zone->weakCaches()) {
      if (!f(cache, zone.get())) {
        return false;
      }
    }
  }

  for (JS::detail::WeakCacheBase* cache : gc->weakCaches()) {
    if (!f(cache, nullptr)) {
      return false;
    }
  }

  return true;
}

static bool PrepareWeakCacheSweeping(GCRuntime* gc,
                                     WeakCacheToSweepVector* immediateCaches) {
  // Start incremental sweeping for caches that support it and add remaining
  // caches to a vector to be swept immediately by helper threads.

  MOZ_ASSERT(immediateCaches->empty());

  bool ok =
      IterateWeakCaches(gc, [&](JS::detail::WeakCacheBase* cache, Zone* zone) {
        if (cache->empty()) {
          return true;
        }

        // Caches that support incremental sweeping will be swept later.
        if (zone && cache->setIncrementalBarrierTracer(&gc->sweepingTracer)) {
          return true;
        }

        return immediateCaches->emplaceBack(cache, zone);
      });

  if (!ok) {
    immediateCaches->clearAndFree();
  }

  return ok;
}

static void SweepAllWeakCachesOnMainThread(GCRuntime* gc) {
  // If we ran out of memory, do all the work on the main thread.
  gcstats::AutoPhase ap(gc->stats(), gcstats::PhaseKind::SWEEP_WEAK_CACHES);
  SweepingTracer trc(gc->rt);
  IterateWeakCaches(gc, [&](JS::detail::WeakCacheBase* cache, Zone* zone) {
    if (cache->needsMarkingBarrier()) {
      cache->setIncrementalBarrierTracer(nullptr);
    }
    cache->traceWeak(&trc, JS::detail::WeakCacheBase::Lock);
    return true;
  });
}

void GCRuntime::sweepEmbeddingWeakPointers(JS::GCContext* gcx) {
  using namespace gcstats;

  AutoLockSweepingLock lock(rt);

  AutoPhase ap(stats(), PhaseKind::FINALIZE_START);
  callFinalizeCallbacks(gcx, JSFINALIZE_GROUP_PREPARE);
  {
    AutoPhase ap2(stats(), PhaseKind::WEAK_ZONES_CALLBACK);
    callWeakPointerZonesCallbacks(&sweepingTracer);
  }
  {
    AutoPhase ap2(stats(), PhaseKind::WEAK_COMPARTMENT_CALLBACK);
    for (SweepGroupZonesIter zone(this); !zone.done(); zone.next()) {
      for (CompartmentsInZoneIter comp(zone); !comp.done(); comp.next()) {
        callWeakPointerCompartmentCallbacks(&sweepingTracer, comp);
      }
    }
  }
  callFinalizeCallbacks(gcx, JSFINALIZE_GROUP_START);
}

IncrementalProgress GCRuntime::beginSweepingSweepGroup(JS::GCContext* gcx,
                                                       SliceBudget& budget) {
  /*
   * Begin sweeping the group of zones in currentSweepGroup, performing
   * actions that must be done before yielding to caller.
   */

  using namespace gcstats;

  AutoSCC scc(stats(), sweepGroupIndex);
  finishMarkingDuringSweeping = false;

#ifdef JS_GC_CONCURRENT_MARKING
  concurrentMarkingFinishedCount = 0;
#endif

  bool sweepingAtoms = false;
  for (SweepGroupZonesIter zone(this); !zone.done(); zone.next()) {
    /* Set the GC state to sweeping. */
    zone->changeGCState(this, Zone::MarkBlackAndGray, Zone::Sweep);

    /* Purge the ArenaLists before sweeping. */
    zone->arenas.checkSweepStateNotInUse();
    zone->arenas.unmarkPreMarkedFreeCells();
    zone->arenas.clearFreeLists();

    zone->bufferAllocator.setMultiThreadedUse(&sweepingLock);

    if (zone->isAtomsZone()) {
      sweepingAtoms = true;
    }
  }

  AutoSetThreadIsSweeping threadIsSweeping;

  // Disable incremental barriers for all zones while we are sweeping/finalizing
  // zones in this sweep group. Set the |disableBarriersForSweeping| flag so we
  // enable/disable the barriers on yield/resume.
  MOZ_ASSERT(!disableBarriersForSweeping);
  disableBarriersForSweeping = true;
  disableIncrementalBarriers();

  // This must happen before sweeping realm globals.
  sweepDebuggerOnMainThread(gcx);

  // FinalizationRegistry sweeping touches weak maps and so must not run in
  // parallel with that. This triggers a read barrier and can add marking work
  // for zones that are still marking. Must happen before sweeping realm
  // globals.
  sweepFinalizationObserversOnMainThread();

  // This must happen before updating embedding weak pointers.
  sweepRealmGlobals();

  // Prune dying wasm instances from each realm's weak instance list now, at the
  // start of sweeping, before the mutator can observe them via the (now no-op)
  // instances() read barrier during later incremental slices.
  sweepWasmInstances();

  sweepEmbeddingWeakPointers(gcx);

  maybeWriteCoverageAndSpew();

  {
    AutoLockHelperThreadState lock;

    AutoPhase ap(stats(), PhaseKind::SWEEP_COMPARTMENTS);

    AutoRunParallelTask sweepCCWrappers(this, &GCRuntime::sweepCCWrappers,
                                        PhaseKind::SWEEP_CC_WRAPPER,
                                        GCUse::Sweeping, lock);
    AutoRunParallelTask sweepMisc(this, &GCRuntime::sweepMisc,
                                  PhaseKind::SWEEP_MISC, GCUse::Sweeping, lock);
    AutoRunParallelTask sweepCompTasks(this, &GCRuntime::sweepCompressionTasks,
                                       PhaseKind::SWEEP_COMPRESSION,
                                       GCUse::Sweeping, lock);
    AutoRunParallelTask sweepWeakMaps(this, &GCRuntime::sweepWeakMaps,
                                      PhaseKind::SWEEP_WEAKMAPS,
                                      GCUse::Sweeping, lock);
    AutoRunParallelTask sweepUniqueIds(this, &GCRuntime::sweepUniqueIds,
                                       PhaseKind::SWEEP_UNIQUEIDS,
                                       GCUse::Sweeping, lock);
    AutoRunParallelTask sweepWeakPointers(
        this, &GCRuntime::sweepObjectsWithWeakPointers,
        PhaseKind::SWEEP_WEAK_POINTERS, GCUse::Sweeping, lock);

    WeakCacheToSweepVector immediateCaches;
    bool canSweepWeakCachesOffThread =
        PrepareWeakCacheSweeping(this, &immediateCaches);
    if (canSweepWeakCachesOffThread) {
      weakCachesToSweep.ref().emplace(currentSweepGroup);
    }

    {
      VectorIterator<WeakCacheToSweepVector> work(immediateCaches);
      AutoRunParallelWork sweepImmediate(
          this, ImmediateSweepWeakCache, PhaseKind::SWEEP_WEAK_CACHES,
          GCUse::Sweeping, work, SliceBudget::unlimited(), lock);

      AutoUnlockHelperThreadState unlock(lock);
      sweepJitDataOnMainThread(gcx);

      if (!canSweepWeakCachesOffThread) {
        MOZ_ASSERT(immediateCaches.empty());
        SweepAllWeakCachesOnMainThread(this);
      }
    }
  }

  // Updating the atom reference bitmaps. This marks atoms referenced by
  // uncollected zones so cannot be done in parallel with the other sweeping
  // work below.
  if (sweepingAtoms) {
    AutoPhase ap(stats(), PhaseKind::UPDATE_ATOMS_BITMAP);
    updateAtomsBitmap();
    startSweepingAtomsTable();
  }

#ifdef JS_GC_ZEAL
  validateIncrementalMarking();
#endif

#ifdef DEBUG
  // Now that the final mark state has been computed check any gray marking
  // assertions we delayed until this point.

  for (SweepGroupZonesIter zone(this); !zone.done(); zone.next()) {
    if (areGrayBitsValid()) {
      for (const auto* cell : zone->cellsToAssertNotGray()) {
        if (cell->isMarkedGray()) {
          const char* kind = JS::GCTraceKindToAscii(cell->getTraceKind());
          printf_stderr("AssertCellIsNotGray: Found gray %s %p\n", kind, cell);
          foundUnexpectedGrayCells = true;
          // Finish the GC non-incrementally. We will assert at the end of GC.
          budget = SliceBudget::unlimited();
        }
      }
    }
    zone->cellsToAssertNotGray().clearAndFree();
  }
#endif

  // Queue all GC things in all zones for sweeping, either on the foreground
  // or on the background thread.

  for (SweepGroupZonesIter zone(this); !zone.done(); zone.next()) {
    zone->bufferAllocator.clearMultiThreadedUse();
    zone->arenas.queueForegroundThingsForSweep();
    constexpr AllocKinds backgroundKinds =
        BackgroundObjectFinalizePhase + BackgroundTrivialFinalizePhase;
    initBackgroundSweep(zone, gcx, backgroundKinds);
  }

  MOZ_ASSERT(!sweepZone);

  safeToYield = true;
  markOnBackgroundThreadDuringSweeping = CanUseExtraThreads();

  return Finished;
}

#ifdef JS_GC_ZEAL
bool GCRuntime::shouldYieldForZeal(ZealMode mode) {
  bool yield = useZeal && hasZealMode(mode);

  // Only yield on the first sweep slice for this mode.
  bool firstSweepSlice = initialState != State::Sweep;
  if (mode == ZealMode::IncrementalMultipleSlices && !firstSweepSlice) {
    yield = false;
  }

  return yield;
}
#endif

IncrementalProgress GCRuntime::endSweepingSweepGroup(JS::GCContext* gcx,
                                                     SliceBudget& budget) {
  // This is to prevent a race between markTask checking the zone state and
  // us changing it below.
  if (joinBackgroundMarkTask() == NotFinished) {
    return NotFinished;
  }

  assertNoMarkingWork();

  // Disable background marking during sweeping until we start sweeping the next
  // zone group.
  markOnBackgroundThreadDuringSweeping = false;

  {
    gcstats::AutoPhase ap(stats(), gcstats::PhaseKind::FINALIZE_END);
    AutoLockSweepingLock lock(rt);
    callFinalizeCallbacks(gcx, JSFINALIZE_GROUP_END);
  }

  /* Free LIFO blocks on a background thread if possible. */
  startBackgroundFree();

  /* Update the GC state for zones we have swept. */
  for (SweepGroupZonesIter zone(this); !zone.done(); zone.next()) {
    if (jit::JitZone* jitZone = zone->jitZone()) {
      // Clear out any small pools that we're hanging on to.
      jitZone->execAlloc().purge();
    }
    AutoLockGC lock(this);
    zone->changeGCState(this, Zone::Sweep, Zone::Finished);
    zone->arenas.unmarkPreMarkedFreeCells();
    zone->arenas.checkNoArenasToUpdate();
    zone->pretenuring.clearCellCountsInNewlyCreatedArenas();
  }

  /* Ensure the initial minor GC has finished sweeping. */
  MOZ_ASSERT(minorGCNumber >= initialMinorGCNumber);
  if (minorGCNumber == initialMinorGCNumber) {
    nursery().joinSweepTask();
  }

  /*
   * Start background thread to sweep zones if required, sweeping any atoms
   * zones last if present.
   */
  ZoneList zones;
  {
    BufferAllocator::MaybeLock lock;
    for (SweepGroupZonesIter zone(this); !zone.done(); zone.next()) {
      if (zone->isAtomsZone()) {
        zones.append(zone);
      } else {
        zones.prepend(zone);
      }

      zone->bufferAllocator.startMajorSweeping(lock);
    }
  }
  queueZonesAndStartBackgroundSweep(std::move(zones));

  // Re-enable incremental barriers for all zones now we are we done sweeping
  // zones in this group.
  MOZ_ASSERT(disableBarriersForSweeping);
  disableBarriersForSweeping = false;
  enableIncrementalBarriers();

  return Finished;
}

IncrementalProgress GCRuntime::markDuringSweeping(JS::GCContext* gcx,
                                                  SliceBudget& budget) {
  MOZ_ASSERT(markTask.isIdle());

  // If we can mark in parallel with sweeping on the main thread we do
  // that. Skip this for internally triggered slices not running in idle time.
  if (markOnBackgroundThreadDuringSweeping && !shouldYieldBeforeSweep(budget)) {
    if (!marker().isDrained() || hasDelayedMarking() ||
        hasAnyDeferredWeakMaps()) {
      AutoLockHelperThreadState lock;
      MOZ_ASSERT(markTask.isIdle(lock));
      markTask.initialize(false, budget, lock);
      markTask.startOrRunIfIdle(lock);
    }
    return Finished;  // This means don't yield to the mutator here.
  }

  // Otherwise the marking needs to happen before we do any more sweeping. If
  // concurrent marking is enabled then |mainThreadBudget| may be empty and
  // we will yield and do the marking concurrently with the mutator.
  gcstats::AutoPhase ap(stats(), gcstats::PhaseKind::MARK);

  auto [mainThreadBudget, helperThreadBudget] = budgetConcurrentMarking(budget);

  IncrementalProgress result =
      markSynchronously(mainThreadBudget, useParallelMarking);

  if (!marker().isMarkStackEmpty()) {
    MOZ_ASSERT(result == NotFinished);
    maybeStartConcurrentMarking(helperThreadBudget);
    return NotFinished;
  }

  if (result == NotFinished) {
    return NotFinished;
  }

  // Yield eagerly after marking has finished for internally triggered slices
  // not running in idle time.
  if (shouldYieldBeforeSweep(budget)) {
    return NotFinished;
  }

  return Finished;
}

void GCRuntime::beginSweepPhase(AutoGCSession& session) {
  /*
   * Sweep phase.
   *
   * Finalize as we sweep, outside of lock but with RuntimeHeapIsBusy()
   * true so that any attempt to allocate a GC-thing from a finalizer will
   * fail, rather than nest badly and leave the unmarked newborn to be swept.
   */

  MOZ_ASSERT(!abortSweepAfterCurrentGroup);
  MOZ_ASSERT(!markOnBackgroundThreadDuringSweeping);

#ifdef DEBUG
  releaseHeldRelocatedArenas();
  verifyAllChunks();
#endif

#ifdef JS_GC_ZEAL
  if (hasZealMode(ZealMode::IncrementalMarkingValidator) && isIncremental) {
    computeNonIncrementalMarkingForValidation(session);
  }
#endif

  gcstats::AutoPhase ap(stats(), gcstats::PhaseKind::SWEEP);

  AssertNoWrappersInGrayList(rt);
  dropStringWrappers();

  groupZonesForSweeping();

  markSliceCount = 0;  // Number of marking slices per zone group swept.

  sweepActions->assertFinished();
}

bool ArenaLists::foregroundFinalize(JS::GCContext* gcx, AllocKind thingKind,
                                    SliceBudget& sliceBudget,
                                    SortedArenaList& sweepList) {
  checkNoArenasToUpdateForKind(thingKind);

  // Non-empty arenas are reused for use for new allocations as soon as the
  // finalizers for that allocation kind have run. Empty arenas are only
  // released when everything in the zone has been swept (see
  // GCRuntime::sweepBackgroundThings for more details).
  ArenaList& arenas = collectingArenaList(thingKind);
  if (!FinalizeArenas<ReleaseEmpty::No>(gcx, arenas, sweepList, thingKind,
                                        sliceBudget)) {
    return false;
  }

  sweepList.extractEmptyTo(&savedEmptyArenas.ref());
  ArenaList sweptArenas = sweepList.convertToArenaList();
  mergeSweptArenas(thingKind, sweptArenas);
  return true;
}

BackgroundMarkTask::BackgroundMarkTask(GCRuntime* gc)
    : GCParallelTask(gc, gcstats::PhaseKind::MARK, GCUse::Marking),
      budget(SliceBudget::unlimited()) {}

void js::gc::BackgroundMarkTask::initialize(bool isConcurrent,
                                            const SliceBudget& budget,
                                            AutoLockHelperThreadState& lock) {
  MOZ_ASSERT(isIdle(lock));
  MOZ_ASSERT_IF(isConcurrent && !budget.isWorkBudget(),
                budget.interruptRequestFlag() == &interruptRequest);

  this->isConcurrent = isConcurrent;
  this->budget = budget;
  this->interruptRequest = false;

  // This must happen on the main thread.
  gc->bufferRuntime().incOffThreadCount();
}

void js::gc::BackgroundMarkTask::run(AutoLockHelperThreadState& lock) {
  {
    AutoUnlockHelperThreadState unlock(lock);
    AutoSetThreadIsMarking threadIsMarking;

    GCMarker* marker;
    if (isConcurrent) {
      marker = &gc->concurrentMarker();
      marker->enterConcurrentMarkingMode();
    } else {
      marker = &gc->marker();
    }

    // Time reporting is handled separately for parallel tasks.
    bool finished =
        marker->markUntilBudgetExhausted(budget, DontReportMarkTime);
    gc->sweepMarkResult = finished ? Finished : NotFinished;

    if (isConcurrent) {
      marker->leaveConcurrentMarkingMode();
    }
  }

  gc->maybeRequestGCAfterBackgroundTask(lock);
  gc->bufferRuntime().decOffThreadCount();
}

void js::gc::BackgroundMarkTask::pause() {
  MOZ_ASSERT(!interruptRequest);
  interruptRequest = true;
}

void js::gc::BackgroundMarkTask::unpause() {
  interruptRequest = false;
  budget.clearInterrupted();
}

IncrementalProgress GCRuntime::joinBackgroundMarkTask() {
  AutoLockHelperThreadState lock;
  if (markTask.isIdle(lock)) {
    return Finished;
  }

  joinTask(markTask, lock);

  IncrementalProgress result = sweepMarkResult;
  sweepMarkResult = Finished;
  return result;
}

bool GCRuntime::pauseBackgroundMarking() {
  AutoLockHelperThreadState lock;
  if (markTask.isIdle(lock)) {
    MOZ_ASSERT(!markTask.interruptRequest);
    return false;
  }

  if (markTask.isFinished(lock)) {
    MOZ_ASSERT(!markTask.interruptRequest);
    markTask.joinWithLockHeld(lock);
    markTask.unpause();
    return false;
  }

  gcstats::AutoPhase ap(stats(), gcstats::PhaseKind::WAIT_BACKGROUND_THREAD);

  bool wasSliceRequested = requestSliceAfterBackgroundTask;
  requestSliceAfterBackgroundTask = false;

  markTask.pause();
  markTask.joinWithLockHeld(lock);
  markTask.unpause();

  MOZ_ASSERT(!requestSliceAfterBackgroundTask);
  if (wasSliceRequested) {
    requestSliceAfterBackgroundTask = true;
  }

  return true;
}

void GCRuntime::resumeBackgroundMarking() {
  MOZ_ASSERT(!markTask.interruptRequest);
  if (markTask.isOverBudget()) {
    return;
  }

  bufferRuntime().incOffThreadCount();
  markTask.start();
}

template <typename T>
static void SweepThing(JS::GCContext* gcx, T* thing) {
  if (!TenuredThingIsMarkedAny(thing)) {
    thing->sweep(gcx);
  }
}

template <typename T>
static bool SweepArenaList(JS::GCContext* gcx, ArenaList& arenaList,
                           Arena** arenasToSweep, SliceBudget& sliceBudget) {
  if (!*arenasToSweep) {
    return true;
  }

  DebugOnly<Zone*> zone = (*arenasToSweep)->zone();
  MOZ_ASSERT(zone->isGCSweeping());

  AllocKind kind = MapTypeToAllocKind<T>::kind;
  size_t steps = Arena::thingsPerArena(kind);

  for (auto arena = arenaList.iterFrom(*arenasToSweep); !arena.done();
       arena.next()) {
    MOZ_ASSERT(arena->zone() == zone);
    MOZ_ASSERT(arena->getAllocKind() == kind);

    if (sliceBudget.isOverBudget()) {
      *arenasToSweep = arena.get();
      return false;
    }

    for (ArenaCellIterUnderGC cell(arena.get()); !cell.done(); cell.next()) {
      SweepThing(gcx, cell.as<T>());
    }

    sliceBudget.step(steps);
  }

  *arenasToSweep = nullptr;
  return true;
}

void GCRuntime::startSweepingAtomsTable() {
  auto& maybeAtoms = maybeAtomsToSweep.ref();
  MOZ_ASSERT(maybeAtoms.isNothing());

  AtomsTable* atomsTable = rt->atomsForSweeping();
  if (!atomsTable) {
    return;
  }

  // Create secondary tables to hold new atoms added while we're sweeping the
  // main tables incrementally.
  if (!atomsTable->startIncrementalSweep(maybeAtoms)) {
    SweepingTracer trc(rt);
    atomsTable->traceWeak(&trc);
  }
}

IncrementalProgress GCRuntime::sweepAtomsTable(JS::GCContext* gcx,
                                               SliceBudget& budget) {
  if (!atomsZone()->isGCSweeping()) {
    return Finished;
  }

  gcstats::AutoPhase ap(stats(), gcstats::PhaseKind::SWEEP_ATOMS_TABLE);

  auto& maybeAtoms = maybeAtomsToSweep.ref();
  if (!maybeAtoms) {
    return Finished;
  }

  if (!rt->atomsForSweeping()->sweepIncrementally(maybeAtoms.ref(), budget)) {
    return NotFinished;
  }

  maybeAtoms.reset();

  return Finished;
}

static size_t IncrementalSweepWeakCache(GCRuntime* gc,
                                        const WeakCacheToSweep& item) {
  AutoSetThreadIsSweeping threadIsSweeping(item.zone);

  JS::detail::WeakCacheBase* cache = item.cache;
  MOZ_ASSERT(cache->needsMarkingBarrier());

  SweepingTracer trc(gc->rt);
  size_t steps = cache->traceWeak(&trc, JS::detail::WeakCacheBase::Lock);
  cache->setIncrementalBarrierTracer(nullptr);

  return steps;
}

WeakCacheSweepIterator::WeakCacheSweepIterator(JS::Zone* sweepGroup)
    : sweepZone(sweepGroup), sweepCache(sweepZone->weakCaches().getFirst()) {
  settle();
}

bool WeakCacheSweepIterator::done() const { return !sweepZone; }

WeakCacheToSweep WeakCacheSweepIterator::get() const {
  MOZ_ASSERT(!done());

  return {sweepCache, sweepZone};
}

void WeakCacheSweepIterator::next() {
  MOZ_ASSERT(!done());

  sweepCache = sweepCache->getNext();
  settle();
}

void WeakCacheSweepIterator::settle() {
  while (sweepZone) {
    while (sweepCache && !sweepCache->needsMarkingBarrier()) {
      sweepCache = sweepCache->getNext();
    }

    if (sweepCache) {
      break;
    }

    sweepZone = sweepZone->nextNodeInGroup();
    if (sweepZone) {
      sweepCache = sweepZone->weakCaches().getFirst();
    }
  }

  MOZ_ASSERT((!sweepZone && !sweepCache) ||
             (sweepCache && sweepCache->needsMarkingBarrier()));
}

IncrementalProgress GCRuntime::sweepWeakCaches(JS::GCContext* gcx,
                                               SliceBudget& budget) {
  if (weakCachesToSweep.ref().isNothing()) {
    return Finished;
  }

  gcstats::AutoPhase ap(stats(), gcstats::PhaseKind::SWEEP_COMPARTMENTS);

  WeakCacheSweepIterator& work = weakCachesToSweep.ref().ref();

  AutoLockHelperThreadState lock;

  {
    AutoRunParallelWork runWork(this, IncrementalSweepWeakCache,
                                gcstats::PhaseKind::SWEEP_WEAK_CACHES,
                                GCUse::Sweeping, work, budget, lock);
    AutoUnlockHelperThreadState unlock(lock);
  }

  if (work.done()) {
    weakCachesToSweep.ref().reset();
    return Finished;
  }

  return NotFinished;
}

IncrementalProgress GCRuntime::finalizeAllocKind(JS::GCContext* gcx,
                                                 SliceBudget& budget) {
  MOZ_ASSERT(sweepZone->isGCSweeping());

  auto& finalizedArenas = foregroundFinalizedArenas.ref();
  if (!finalizedArenas) {
    finalizedArenas.emplace(sweepAllocKind);
    foregroundFinalizedZone = sweepZone;
    foregroundFinalizedAllocKind = sweepAllocKind;
  } else {
    MOZ_ASSERT(finalizedArenas->allocKind() == sweepAllocKind);
    MOZ_ASSERT(foregroundFinalizedZone == sweepZone);
    MOZ_ASSERT(foregroundFinalizedAllocKind == sweepAllocKind);
  }

  AutoSetThreadIsFinalizing threadIsFinalizing(gcx);
  ArenaLists& arenaLists = sweepZone->arenas;
  if (!arenaLists.foregroundFinalize(gcx, sweepAllocKind, budget,
                                     finalizedArenas.ref())) {
    return NotFinished;
  }

  finalizedArenas.reset();
  foregroundFinalizedZone = nullptr;
  foregroundFinalizedAllocKind = AllocKind::LIMIT;

  return Finished;
}

SortedArenaList* GCRuntime::maybeGetForegroundFinalizedArenas(Zone* zone,
                                                              AllocKind kind) {
  MOZ_ASSERT(zone);
  MOZ_ASSERT(IsValidAllocKind(kind));

  auto& finalizedArenas = foregroundFinalizedArenas.ref();

  if (finalizedArenas.isNothing() || zone != foregroundFinalizedZone ||
      kind != foregroundFinalizedAllocKind) {
    return nullptr;
  }

  return finalizedArenas.ptr();
}

IncrementalProgress GCRuntime::sweepPropMapTree(JS::GCContext* gcx,
                                                SliceBudget& budget) {
  // Remove dead SharedPropMaps from the tree. This happens incrementally on the
  // main thread. PropMaps are finalized later on the a background thread.

  gcstats::AutoPhase ap(stats(), gcstats::PhaseKind::SWEEP_PROP_MAP);

  ArenaLists& al = sweepZone->arenas;

  if (!SweepArenaList<CompactPropMap>(
          gcx, al.collectingArenaList(AllocKind::COMPACT_PROP_MAP),
          &al.gcCompactPropMapArenasToUpdate.ref(), budget)) {
    return NotFinished;
  }
  if (!SweepArenaList<NormalPropMap>(
          gcx, al.collectingArenaList(AllocKind::NORMAL_PROP_MAP),
          &al.gcNormalPropMapArenasToUpdate.ref(), budget)) {
    return NotFinished;
  }

  return Finished;
}

// An iterator for a standard container that provides an STL-like begin()/end()
// interface. This iterator provides a done()/get()/next() style interface.
template <typename Container>
class ContainerIter {
  using Iter = decltype(std::declval<const Container>().begin());
  using Elem = decltype(*std::declval<Iter>());

  Iter iter;
  const Iter end;

 public:
  explicit ContainerIter(const Container& container)
      : iter(container.begin()), end(container.end()) {}

  bool done() const { return iter == end; }

  Elem get() const { return *iter; }

  void next() {
    MOZ_ASSERT(!done());
    ++iter;
  }
};

// IncrementalIter is a template class that makes a normal iterator into one
// that can be used to perform incremental work by using external state that
// persists between instantiations. The state is only initialised on the first
// use and subsequent uses carry on from the previous state.
template <typename Iter>
struct IncrementalIter {
  using State = mozilla::Maybe<Iter>;
  using Elem = decltype(std::declval<Iter>().get());

 private:
  State& maybeIter;

 public:
  template <typename... Args>
  explicit IncrementalIter(State& maybeIter, Args&&... args)
      : maybeIter(maybeIter) {
    if (maybeIter.isNothing()) {
      maybeIter.emplace(std::forward<Args>(args)...);
    }
  }

  ~IncrementalIter() {
    if (done()) {
      maybeIter.reset();
    }
  }

  bool done() const { return maybeIter.ref().done(); }

  Elem get() const { return maybeIter.ref().get(); }

  void next() { maybeIter.ref().next(); }
};

// Iterate through the sweep groups created by
// GCRuntime::groupZonesForSweeping().
class js::gc::SweepGroupsIter {
  GCRuntime* gc;

 public:
  explicit SweepGroupsIter(JSRuntime* rt) : gc(&rt->gc) {
    MOZ_ASSERT(gc->currentSweepGroup);
  }

  bool done() const { return !gc->currentSweepGroup; }

  Zone* get() const { return gc->currentSweepGroup; }

  void next() {
    MOZ_ASSERT(!done());
    gc->moveToNextSweepGroup();
  }
};

namespace sweepaction {

// Implementation of the SweepAction interface that calls a method on GCRuntime.
class SweepActionCall final : public SweepAction {
  using Method = IncrementalProgress (GCRuntime::*)(JS::GCContext* gcx,
                                                    SliceBudget& budget);

  Method method;

 public:
  explicit SweepActionCall(Method m) : method(m) {}
  IncrementalProgress run(Args& args) override {
    return (args.gc->*method)(args.gcx, args.budget);
  }
  void assertFinished() const override {}
};

// Implementation of the SweepAction interface that yields in a specified zeal
// mode.
class SweepActionMaybeYield final : public SweepAction {
#ifdef JS_GC_ZEAL
  ZealMode mode;
  bool isYielding;
#endif

 public:
  explicit SweepActionMaybeYield(ZealMode mode)
#ifdef JS_GC_ZEAL
      : mode(mode),
        isYielding(false)
#endif
  {
  }

  IncrementalProgress run(Args& args) override {
#ifdef JS_GC_ZEAL
    if (!isYielding && args.gc->shouldYieldForZeal(mode)) {
      isYielding = true;
      return NotFinished;
    }

    isYielding = false;
#endif
    return Finished;
  }

  void assertFinished() const override { MOZ_ASSERT(!isYielding); }

  // These actions should be skipped if GC zeal is not configured.
#ifndef JS_GC_ZEAL
  bool shouldSkip() override { return true; }
#endif
};

// Implementation of the SweepAction interface that calls a list of actions in
// sequence.
class SweepActionSequence final : public SweepAction {
  using ActionVector = Vector<UniquePtr<SweepAction>, 0, SystemAllocPolicy>;
  using Iter = IncrementalIter<ContainerIter<ActionVector>>;

  ActionVector actions;
  typename Iter::State iterState;

 public:
  bool init(UniquePtr<SweepAction>* acts, size_t count) {
    for (size_t i = 0; i < count; i++) {
      auto& action = acts[i];
      if (!action) {
        return false;
      }
      if (action->shouldSkip()) {
        continue;
      }
      if (!actions.emplaceBack(std::move(action))) {
        return false;
      }
    }
    return true;
  }

  IncrementalProgress run(Args& args) override {
    for (Iter iter(iterState, actions); !iter.done(); iter.next()) {
      if (iter.get()->run(args) == NotFinished) {
        return NotFinished;
      }
    }
    return Finished;
  }

  void assertFinished() const override {
    MOZ_ASSERT(iterState.isNothing());
    for (const auto& action : actions) {
      action->assertFinished();
    }
  }
};

template <typename Iter, typename Init>
class SweepActionForEach final : public SweepAction {
  using Elem = decltype(std::declval<Iter>().get());
  using IncrIter = IncrementalIter<Iter>;

  Init iterInit;
  Elem* elemOut;
  UniquePtr<SweepAction> action;
  typename IncrIter::State iterState;

 public:
  SweepActionForEach(const Init& init, Elem* maybeElemOut,
                     UniquePtr<SweepAction> action)
      : iterInit(init), elemOut(maybeElemOut), action(std::move(action)) {}

  IncrementalProgress run(Args& args) override {
    MOZ_ASSERT_IF(elemOut, *elemOut == Elem());
    auto clearElem = mozilla::MakeScopeExit([&] { setElem(Elem()); });
    for (IncrIter iter(iterState, iterInit); !iter.done(); iter.next()) {
      setElem(iter.get());
      if (action->run(args) == NotFinished) {
        return NotFinished;
      }
    }
    return Finished;
  }

  void assertFinished() const override {
    MOZ_ASSERT(iterState.isNothing());
    MOZ_ASSERT_IF(elemOut, *elemOut == Elem());
    action->assertFinished();
  }

 private:
  void setElem(const Elem& value) {
    if (elemOut) {
      *elemOut = value;
    }
  }
};

static UniquePtr<SweepAction> Call(IncrementalProgress (GCRuntime::*method)(
    JS::GCContext* gcx, SliceBudget& budget)) {
  return MakeUnique<SweepActionCall>(method);
}

static UniquePtr<SweepAction> MaybeYield(ZealMode zealMode) {
  return MakeUnique<SweepActionMaybeYield>(zealMode);
}

template <typename... Rest>
static UniquePtr<SweepAction> Sequence(UniquePtr<SweepAction> first,
                                       Rest... rest) {
  UniquePtr<SweepAction> actions[] = {std::move(first), std::move(rest)...};
  auto seq = MakeUnique<SweepActionSequence>();
  if (!seq || !seq->init(actions, std::size(actions))) {
    return nullptr;
  }

  return UniquePtr<SweepAction>(std::move(seq));
}

static UniquePtr<SweepAction> RepeatForSweepGroup(
    JSRuntime* rt, UniquePtr<SweepAction> action) {
  if (!action) {
    return nullptr;
  }

  using Action = SweepActionForEach<SweepGroupsIter, JSRuntime*>;
  return js::MakeUnique<Action>(rt, nullptr, std::move(action));
}

static UniquePtr<SweepAction> ForEachZoneInSweepGroup(
    JSRuntime* rt, Zone** zoneOut, UniquePtr<SweepAction> action) {
  if (!action) {
    return nullptr;
  }

  using Action = SweepActionForEach<SweepGroupZonesIter, JSRuntime*>;
  return js::MakeUnique<Action>(rt, zoneOut, std::move(action));
}

static UniquePtr<SweepAction> ForEachAllocKind(const AllocKinds& kinds,
                                               AllocKind* kindOut,
                                               UniquePtr<SweepAction> action) {
  if (!action) {
    return nullptr;
  }

  using Action = SweepActionForEach<ContainerIter<AllocKinds>, AllocKinds>;
  return js::MakeUnique<Action>(kinds, kindOut, std::move(action));
}

}  // namespace sweepaction

bool GCRuntime::initSweepActions() {
  using namespace sweepaction;
  using sweepaction::Call;

  sweepActions.ref() = RepeatForSweepGroup(
      rt,
      Sequence(
          Call(&GCRuntime::beginMarkingSweepGroup),
          Call(&GCRuntime::markGrayRootsInCurrentGroup),
          MaybeYield(ZealMode::YieldWhileGrayMarking),
          Call(&GCRuntime::markGray), Call(&GCRuntime::endMarkingSweepGroup),
          Call(&GCRuntime::beginSweepingSweepGroup),
          MaybeYield(ZealMode::IncrementalMultipleSlices),
          MaybeYield(ZealMode::YieldBeforeSweepingAtoms),
          Call(&GCRuntime::sweepAtomsTable),
          MaybeYield(ZealMode::YieldBeforeSweepingCaches),
          Call(&GCRuntime::sweepWeakCaches),
          ForEachZoneInSweepGroup(
              rt, &sweepZone.ref(),
              Sequence(MaybeYield(ZealMode::YieldBeforeSweepingObjects),
                       ForEachAllocKind(ForegroundObjectFinalizePhase,
                                        &sweepAllocKind.ref(),
                                        Call(&GCRuntime::finalizeAllocKind)),
                       MaybeYield(ZealMode::YieldBeforeSweepingNonObjects),
                       ForEachAllocKind(ForegroundNonObjectFinalizePhase,
                                        &sweepAllocKind.ref(),
                                        Call(&GCRuntime::finalizeAllocKind)),
                       MaybeYield(ZealMode::YieldBeforeSweepingPropMapTrees),
                       Call(&GCRuntime::sweepPropMapTree))),
          Call(&GCRuntime::endSweepingSweepGroup)));

  return sweepActions != nullptr;
}

// Ensure barriers are disabled if required when entering a sweep slice and
// re-enabled when yielding to the mutator. |disableBarriersForSweeping| is set
// in beginSweepingSweepGroup and cleared in endSweepingSweepGroup.
class js::gc::AutoUpdateBarriersForSweeping {
 public:
  explicit AutoUpdateBarriersForSweeping(GCRuntime* gc) : gc(gc) {
    MOZ_ASSERT(gc->state() == State::Sweep);
    if (gc->disableBarriersForSweeping) {
      gc->disableIncrementalBarriers();
    }
  }

  ~AutoUpdateBarriersForSweeping() {
    MOZ_ASSERT(gc->state() == State::Sweep);
    if (gc->disableBarriersForSweeping) {
      gc->enableIncrementalBarriers();
    }
  }

 private:
  GCRuntime* gc;
};

IncrementalProgress GCRuntime::sweepPhase(SliceBudget& budget) {
  // The first time we enter the sweep phase we must not yield to the mutator
  // until we've started sweeping a sweep group but in that case the stack must
  // be empty already.
  MOZ_ASSERT(initialState <= State::Sweep);

#ifdef DEBUG
  bool isFirstSweepSlice = initialState < State::Sweep;
  if (isFirstSweepSlice) {
    assertNoMarkingWork();
  }
#endif

  AutoMajorGCProfilerEntry s(this);
  gcstats::AutoPhase ap(stats(), gcstats::PhaseKind::SWEEP);

  markSliceCount++;

  bool finishedMainThreadOnlyMarking = finishAnyConcurrentMarking(budget);
  if (!finishedMainThreadOnlyMarking) {
    auto [_, helperThreadBudget] = budgetConcurrentMarking(budget);
    maybeStartConcurrentMarking(helperThreadBudget);
    return NotFinished;
  }

  // Clear out whole cell store buffer entries with pointers to unreachable
  // cells in case we free them.
  if (storeBuffer().mayHavePointersToDeadCells()) {
    collectNurseryFromMajorGC(JS::GCReason::EVICT_NURSERY);
  }

  JS::GCContext* gcx = rt->gcContext();
  AutoSetThreadIsSweeping threadIsSweeping(gcx);
  AutoPoisonFreedJitCode pjc(gcx);

  // Drain the mark stack, possibly in a parallel task if we're in a part of
  // sweeping that allows it.

  if (markDuringSweeping(gcx, budget) == Finished) {
    // Don't trigger pre-barriers when sweeping or finalizing.
    AutoUpdateBarriersForSweeping updateBarriers(this);

    // Then continue running sweep actions.

    SweepAction::Args args{this, gcx, budget};
    IncrementalProgress sweepProgress = sweepActions->run(args);

    markTask.pause();
    joinBackgroundMarkTask();
    markTask.unpause();

    if (sweepProgress == Finished) {
      MOZ_ASSERT(!hasMarkingWork());
      return Finished;
    }
  }

  MOZ_ASSERT(isIncremental);
  return NotFinished;
}

bool GCRuntime::allCCVisibleZonesWereCollected() {
  // Calculate whether the gray marking state is now valid.
  //
  // The gray bits change from invalid to valid if we finished a full GC from
  // the point of view of the cycle collector. We ignore the following:
  //
  //  - Empty zones.
  //
  // This exception ensures that when the CC requests a full GC the gray mark
  // state ends up valid even if we don't collect all of the zones.

  for (ZonesIter zone(this, WithAtoms); !zone.done(); zone.next()) {
    if (!zone->isCollecting() && !zone->arenas.arenaListsAreEmpty()) {
      return false;
    }
  }

  return true;
}

void GCRuntime::endSweepPhase(bool destroyingRuntime) {
  MOZ_ASSERT(!markOnBackgroundThreadDuringSweeping);

  sweepActions->assertFinished();

  gcstats::AutoPhase ap(stats(), gcstats::PhaseKind::SWEEP);

  MOZ_ASSERT_IF(destroyingRuntime, !useBackgroundThreads);

  // Release parallel marking threads for worker runtimes now we've finished
  // marking. The main thread keeps the reservation as long as parallel marking
  // is enabled.
  if (!rt->isMainRuntime()) {
    MOZ_ASSERT_IF(useParallelMarking, reservedMarkingThreads != 0);
    releaseMarkingThreads();
  }

  {
    gcstats::AutoPhase ap(stats(), gcstats::PhaseKind::DESTROY);

    // Sweep shared script bytecode now all zones have been swept and finalizers
    // for BaseScripts have released their references.
    SweepScriptData(rt);
  }

  {
    gcstats::AutoPhase ap(stats(), gcstats::PhaseKind::FINALIZE_END);
    AutoLockSweepingLock lock(rt);
    callFinalizeCallbacks(rt->gcContext(), JSFINALIZE_COLLECTION_END);

    if (grayBitsInvalid) {
      if (grayBitsInvalid & GrayBitsInvalidatedInCurrentGC) {
        grayBitsInvalid &= ~GrayBitsInvalidatedInCurrentGC;
        MOZ_ASSERT(grayBitsInvalid == GrayBitsInvalid);
      } else if (allCCVisibleZonesWereCollected()) {
        grayBitsInvalid = 0;
      }
    }
  }

  if (isIncremental) {
    findDeadCompartments();
  }

#ifdef JS_GC_ZEAL
  finishMarkingValidation();
#endif

  AssertNoWrappersInGrayList(rt);
}
