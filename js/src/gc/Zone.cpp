/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gc/Zone.h"

#include "mozilla/Sprintf.h"
#include "mozilla/TimeStamp.h"

#include "gc/FinalizationObservers.h"
#include "gc/GCContext.h"
#include "gc/PublicIterators.h"
#include "jit/BaselineIC.h"
#include "jit/BaselineJIT.h"
#include "jit/Invalidation.h"
#include "jit/JitScript.h"
#include "jit/JitZone.h"
#include "js/shadow/Zone.h"  // JS::shadow::Zone
#include "vm/Runtime.h"
#include "vm/Time.h"

#include "debugger/DebugAPI-inl.h"
#include "gc/GC-inl.h"
#include "gc/Marking-inl.h"
#include "gc/Nursery-inl.h"
#include "gc/StableCellHasher-inl.h"
#include "gc/WeakMap-inl.h"
#include "vm/JSScript-inl.h"
#include "vm/Realm-inl.h"

using namespace js;
using namespace js::gc;

Zone* const Zone::NotOnList = reinterpret_cast<Zone*>(1);

ZoneAllocator::ZoneAllocator(JSRuntime* rt, Kind kind)
    : JS::shadow::Zone(rt, rt->gc.marker().tracer(), kind),
      jitHeapThreshold(size_t(jit::MaxCodeBytesPerProcess * 0.8)) {}

ZoneAllocator::~ZoneAllocator() {
#ifdef DEBUG
  mallocTracker.checkEmptyOnDestroy();
  MOZ_ASSERT(gcHeapSize.bytes() == 0);
  MOZ_ASSERT(mallocHeapSize.bytes() == 0);
  MOZ_ASSERT(jitHeapSize.bytes() == 0);
#endif
}

void ZoneAllocator::fixupAfterMovingGC() {
#ifdef DEBUG
  mallocTracker.fixupAfterMovingGC();
#endif
}

void js::ZoneAllocator::updateSchedulingStateOnGCStart() {
  gcHeapSize.updateOnGCStart();
  mallocHeapSize.updateOnGCStart();
  jitHeapSize.updateOnGCStart();
  perZoneGCTime = mozilla::TimeDuration::Zero();
}

void js::ZoneAllocator::updateGCStartThresholds(GCRuntime& gc) {
  bool isAtomsZone = JS::Zone::from(this)->isAtomsZone();
  gcHeapThreshold.updateStartThreshold(
      gcHeapSize.retainedBytes(), smoothedAllocationRate.ref(),
      smoothedCollectionRate.ref(), gc.tunables, gc.schedulingState,
      isAtomsZone);

  mallocHeapThreshold.updateStartThreshold(mallocHeapSize.retainedBytes(),
                                           gc.tunables, gc.schedulingState);
}

void js::ZoneAllocator::setGCSliceThresholds(GCRuntime& gc,
                                             bool waitingOnBGTask) {
  gcHeapThreshold.setSliceThreshold(this, gcHeapSize, gc.tunables,
                                    waitingOnBGTask);
  mallocHeapThreshold.setSliceThreshold(this, mallocHeapSize, gc.tunables,
                                        waitingOnBGTask);
  jitHeapThreshold.setSliceThreshold(this, jitHeapSize, gc.tunables,
                                     waitingOnBGTask);
}

void js::ZoneAllocator::clearGCSliceThresholds() {
  gcHeapThreshold.clearSliceThreshold();
  mallocHeapThreshold.clearSliceThreshold();
  jitHeapThreshold.clearSliceThreshold();
}

bool ZoneAllocator::addSharedMemory(void* mem, size_t nbytes, MemoryUse use) {
  // nbytes can be zero here for SharedArrayBuffers.

  MOZ_ASSERT(CurrentThreadCanAccessRuntime(runtime_));

  auto ptr = sharedMemoryUseCounts.lookupForAdd(mem);
  MOZ_ASSERT_IF(ptr, ptr->value().use == use);

  if (!ptr && !sharedMemoryUseCounts.add(ptr, mem, gc::SharedMemoryUse(use))) {
    return false;
  }

  ptr->value().count++;

  // Allocations can grow, so add any increase over the previous size and record
  // the new size.
  if (nbytes > ptr->value().nbytes) {
    mallocHeapSize.addBytes(nbytes - ptr->value().nbytes);
    ptr->value().nbytes = nbytes;
  }

  maybeTriggerGCOnMalloc();

  return true;
}

void ZoneAllocator::removeSharedMemory(void* mem, size_t nbytes,
                                       MemoryUse use) {
  // nbytes can be zero here for SharedArrayBuffers.

  MOZ_ASSERT(CurrentThreadCanAccessRuntime(runtime_));
  MOZ_ASSERT(CurrentThreadIsGCFinalizing());

  auto ptr = sharedMemoryUseCounts.lookup(mem);

  MOZ_ASSERT(ptr);
  MOZ_ASSERT(ptr->value().count != 0);
  MOZ_ASSERT(ptr->value().use == use);
  MOZ_ASSERT(ptr->value().nbytes >= nbytes);

  ptr->value().count--;
  if (ptr->value().count == 0) {
    mallocHeapSize.removeBytes(ptr->value().nbytes, true);
    sharedMemoryUseCounts.remove(ptr);
  }
}

template <TrackingKind kind>
void js::TrackedAllocPolicy<kind>::decMemory(size_t nbytes) {
  bool updateRetainedSize = false;
  if constexpr (kind == TrackingKind::Cell) {
    // Only subtract freed cell memory from retained size for cell associations
    // during sweeping.
    JS::GCContext* gcx = TlsGCContext.get();
    updateRetainedSize = gcx->isFinalizing();
  }

  zone_->decNonGCMemory(this, nbytes, MemoryUse::TrackedAllocPolicy,
                        updateRetainedSize);
}

namespace js {
template class TrackedAllocPolicy<TrackingKind::Zone>;
template class TrackedAllocPolicy<TrackingKind::Cell>;
}  // namespace js

MOZ_COLD void BufferAllocPolicy::reportAllocOverflow() const {
  zone->reportAllocOverflow();
}

JS::Zone::Zone(JSRuntime* rt, Kind kind)
    : ZoneAllocator(rt, kind),
      arenas(this),
      bufferAllocator(&rt->gc, this),
      data(nullptr),
      suppressAllocationMetadataBuilder(false),
      allocNurseryObjects_(true),
      allocNurseryStrings_(true),
      allocNurseryBigInts_(true),
      allocNurseryGetterSetters_(true),
      pretenuring(this),
      crossZoneStringWrappers_(this),
      localSymbolMap_(this),
      shapeZone_(this),
      gcScheduled_(false),
      gcScheduledSaved_(false),
      keepPropMapTables_(false),
      wasCollected_(false),
      listNext_(NotOnList),
      keptAliveSet(this),
      objectFuses(this) {
  /* Ensure that there are no vtables to mess us up here. */
  MOZ_ASSERT(reinterpret_cast<JS::shadow::Zone*>(this) ==
             static_cast<JS::shadow::Zone*>(this));
  MOZ_ASSERT_IF(isAtomsZone(), rt->gc.zones().empty());

  updateGCStartThresholds(rt->gc);
  rt->gc.nursery().setAllocFlagsForZone(this);
}

Zone::~Zone() {
  MOZ_ASSERT_IF(regExps_.ref(), regExps().empty());

  MOZ_ASSERT(numRealmsWithAllocMetadataBuilder_ == 0);

  DebugAPI::deleteDebugScriptMap(debugScriptMap);
  js_delete(finalizationObservers_.ref().release());

  js_delete(jitZone_.ref());

  MOZ_ASSERT(gcSystemWeakMaps().isEmpty());
  MOZ_ASSERT(gcUserWeakMaps().isEmpty());
  MOZ_ASSERT(gcMarkedUserWeakMaps().isEmpty());
  MOZ_ASSERT(objectsWithWeakPointers.ref().empty());

  JSRuntime* rt = runtimeFromAnyThread();
  if (this == rt->gc.systemZone) {
    MOZ_ASSERT(isSystemZone());
    rt->gc.systemZone = nullptr;
  }

  if (preservedWrappers_) {
    MOZ_RELEASE_ASSERT(preservedWrappersCount_ == 0);
    js_free(preservedWrappers_);
  }

  scriptCountsMap.reset();
  scriptLCovMap.reset();
#ifdef MOZ_VTUNE
  scriptVTuneIdMap.reset();
#endif
#ifdef JS_CACHEIR_SPEW
  scriptFinalWarmUpCountMap.reset();
#endif
  profilerStrings.reset();
}

bool Zone::init() {
  regExps_.ref() = make_unique<RegExpZone>(this);
  return !!regExps_.ref();
}

void Zone::setNeedsMarkingBarrier(GCRuntime* gc, bool needs) {
  uint32_t newState = 0;
  if (needs) {
    newState = Incremental;
    if (gc->isConcurrentMarkingEnabled()) {
      newState |= Concurrent;
    }
  }

  needsMarkingBarrier_ = newState;
}

void Zone::changeGCState(GCRuntime* gc, GCState prev, GCState next) {
  MOZ_ASSERT(RuntimeHeapIsBusy());
  MOZ_ASSERT(gcState() == prev);
  MOZ_ASSERT_IF(isGCMarkingOrVerifyingPreBarriers(), needsMarkingBarrier_);

  gcState_ = next;
  setNeedsMarkingBarrier(gc, isGCMarkingOrVerifyingPreBarriers());
}

void Zone::sweepAfterMinorGC(JSTracer* trc) {
  crossZoneStringWrappers().sweepAfterMinorGC(trc);

  for (CompartmentsInZoneIter comp(this); !comp.done(); comp.next()) {
    comp->sweepAfterMinorGC(trc);
  }
}

void Zone::traceWeakCCWEdges(JSTracer* trc) {
  crossZoneStringWrappers().traceWeak(trc);
  for (CompartmentsInZoneIter comp(this); !comp.done(); comp.next()) {
    comp->traceCrossCompartmentObjectWrapperEdges(trc);
  }
  traceWeakLocalSymbolEdges(trc);
}

/* static */
void Zone::fixupAllCrossCompartmentWrappersAfterMovingGC(JSTracer* trc) {
  MOZ_ASSERT(trc->runtime()->gc.isHeapCompacting());

  for (ZonesIter zone(trc->runtime(), WithAtoms); !zone.done(); zone.next()) {
    // Trace the wrapper map to update keys (wrapped values) in other
    // compartments that may have been moved.
    zone->crossZoneStringWrappers().traceWeak(trc);

    for (CompartmentsInZoneIter comp(zone); !comp.done(); comp.next()) {
      comp->fixupCrossCompartmentObjectWrappersAfterMovingGC(trc);
    }
  }
}

void Zone::dropStringWrappersOnGC() {
  MOZ_ASSERT(JS::RuntimeHeapIsCollecting());
  crossZoneStringWrappers().clear();
}

#ifdef JSGC_HASH_TABLE_CHECKS

void Zone::checkAllCrossCompartmentWrappersAfterMovingGC() {
  checkStringWrappersAfterMovingGC();
  for (CompartmentsInZoneIter comp(this); !comp.done(); comp.next()) {
    comp->checkObjectWrappersAfterMovingGC();
  }
}

void Zone::checkStringWrappersAfterMovingGC() {
  CheckTableAfterMovingGC(crossZoneStringWrappers(), [this](const auto& entry) {
    JSString* key = entry.key().get();
    CheckGCThingAfterMovingGC(key);  // Keys may be in a different zone.
    CheckGCThingAfterMovingGC(entry.value().unbarrieredGet(), this);
    return key;
  });
}
#endif

#ifdef DEBUG
bool Zone::isAnyRealmPreservingCode() {
  for (RealmsInZoneIter r(this); !r.done(); r.next()) {
    if (r->jitRealm().isPreservingCode()) {
      return true;
    }
  }
  return false;
}
#endif

void Zone::discardJitCodeForAllRealms(JS::GCContext* gcx) {
  MOZ_ASSERT(!isAnyRealmPreservingCode());
  discardJitCode(gcx);
}

void Zone::discardJitCode(JS::GCContext* gcx,
                          const JitDiscardOptions& options) {
  if (!jitZone()) {
    return;
  }

  // Move IC stub data from realms not preserving JIT code to a separate
  // LifoAlloc. Stubs that must survive are copied back to their realm's stub
  // space below.
  jit::ICStubSpace discardedStubSpace;
  const mozilla::TimeStamp now = mozilla::TimeStamp::Now();
  size_t numRealms = 0;
  size_t numDiscardedRealms = 0;
  for (RealmsInZoneIter r(this); !r.done(); r.next()) {
    numRealms++;
    if (r->jitRealm().isPreservingCode()) {
      continue;
    }
    numDiscardedRealms++;
    if (options.discardJitScripts) {
      r->jitRealm().setLastDiscardedCodeTime(now);
    }
    discardedStubSpace.transferFrom(*r->jitRealm().stubSpace());
  }

  bool resetAllocSites =
      options.resetNurseryAllocSites || options.resetPretenuredAllocSites;
  if (numDiscardedRealms == 0 && !resetAllocSites) {
    return;
  }

  const bool anyRealmPreservingCode = numDiscardedRealms != numRealms;

#ifdef DEBUG
  // Assert no ICScripts are marked as active.
  jitZone()->forEachJitScript([](jit::JitScript* jitScript) {
    MOZ_ASSERT(!jitScript->hasActiveICScript());
  });
#endif

  // Mark ICScripts on the stack as active and copy active Baseline stubs.
  jit::MarkActiveICScriptsAndCopyStubs(this);

  // Invalidate all Ion code in this zone for realms not preserving JIT code.
  jit::InvalidateAll(gcx, this);

  jitZone()->forEachJitScript<jit::IncludeDyingScripts>(
      [&](jit::JitScript* jitScript) {
        // Note: if we're currently sweeping, |script| may be dying and we
        // shouldn't call script->realm() because it goes through the
        // ScriptSourceObject. |anyRealmPreservingCode| is only true at the
        // start of a GC so we check that first.
        JSScript* script = jitScript->owningScript();
        MOZ_ASSERT_IF(anyRealmPreservingCode,
                      !gc::IsAboutToBeFinalizedUnbarriered(script));
        if (anyRealmPreservingCode &&
            script->realm()->jitRealm().isPreservingCode()) {
          // We're not discarding this realm's JIT code, but we may still have
          // to reset allocation sites.
          if (resetAllocSites &&
              jitScript->resetAllocSites(options.resetNurseryAllocSites,
                                         options.resetPretenuredAllocSites)) {
            if (script->hasIonScript()) {
              jit::Invalidate(runtime_->mainContextFromOwnThread(), script,
                              /* resetUses = */ true,
                              /* cancelOffThread = */ false);
            }
          }
          MOZ_ASSERT(!jitScript->icScript()->active());
          return;
        }

        jit::FinishInvalidation(gcx, script);

        // Discard baseline script if it's not marked as active.
        if (jitScript->hasBaselineScript() &&
            !jitScript->icScript()->active()) {
          jit::FinishDiscardBaselineScript(gcx, script);
        }

#ifdef JS_CACHEIR_SPEW
        maybeUpdateWarmUpCount(script);
#endif

        // Warm-up counter for scripts are reset on GC. After discarding code we
        // need to let it warm back up to get information such as which
        // opcodes are setting array holes or accessing getter properties.
        script->resetWarmUpCounterForGC();

        // Try to release the script's JitScript. This should happen after
        // releasing JIT code because we can't do this when the script still has
        // JIT code.
        if (options.discardJitScripts) {
          script->maybeReleaseJitScript(gcx);
          jitScript = script->maybeJitScript();
          if (!jitScript) {
            // If we successfully discarded the JIT script, try to discard the
            // ScriptCounts too.
            if (!script->realm()->collectCoverageForDebug() &&
                !gcx->runtime()->profilingScripts) {
              script->destroyScriptCounts();
            }
            script->realm()->jitRealm().removeFromCompileQueue(script);
            return;  // Continue script loop.
          }
        }

        // If we did not release the JitScript, we need to purge IC stubs
        // because their stub data was moved to discardedStubSpace above.
        // Also purge all trial-inlined ICScripts that are not active on
        // the stack.
        jitScript->purgeInactiveICScripts();
        jitScript->purgeStubs(script);

        if (resetAllocSites) {
          jitScript->resetAllocSites(options.resetNurseryAllocSites,
                                     options.resetPretenuredAllocSites);
        }

        // Reset the active flag of each ICScript.
        jitScript->resetAllActiveFlags();
      });

  // Also clear references to jit code from RegExpShared cells at this point.
  // This avoid holding onto ExecutablePools. RegExpShareds are not associated
  // with a realm, so we only do this when discarding JIT code for all realms.
  // Do not discard if an interrupted regexp is currently on the stack.
  const bool discardRegExpJitCode =
      !anyRealmPreservingCode && !jitZone()->keepRegExpJitCode();
  if (discardRegExpJitCode) {
    for (auto regExp = cellIterUnsafe<RegExpShared>(); !regExp.done();
         regExp.next()) {
      regExp->discardJitCode();
    }
  }

  /*
   * When scripts contain pointers to nursery things, the store buffer
   * can contain entries that point into the discarded stub space. Since
   * this method can be called outside the context of a GC, this situation
   * could result in us trying to mark invalid store buffer entries.
   *
   * Defer freeing any allocated blocks until after the next minor GC.
   */
  discardedStubSpace.freeAllAfterMinorGC(this);
  for (RealmsInZoneIter r(this); !r.done(); r.next()) {
    if (!r->jitRealm().isPreservingCode()) {
      r->jitRealm().purgeIonCacheIRStubInfo();
    }
  }

  // Generate a profile marker
  if (gcx->runtime()->geckoProfiler().enabled()) {
    char discardingJitScript = options.discardJitScripts ? 'Y' : 'N';
    char discardingBaseline = 'Y';
    char discardingIon = 'Y';

    char discardingRegExp = discardRegExpJitCode ? 'Y' : 'N';
    char discardingNurserySites = options.resetNurseryAllocSites ? 'Y' : 'N';
    char discardingPretenuredSites =
        options.resetPretenuredAllocSites ? 'Y' : 'N';

    char buf[160];
    SprintfLiteral(buf,
                   "Realms:%zu/%zu JitScript:%c Baseline:%c Ion:%c "
                   "RegExp:%c NurserySites:%c PretenuredSites:%c",
                   numDiscardedRealms, numRealms, discardingJitScript,
                   discardingBaseline, discardingIon, discardingRegExp,
                   discardingNurserySites, discardingPretenuredSites);
    gcx->runtime()->geckoProfiler().markEvent("DiscardJit", buf);
  }
}

void JS::Zone::resetPretenuredWasmAllocSites() {
  for (RealmsInZoneIter realm(this); !realm.done(); realm.next()) {
    realm->wasm.resetPretenuredAllocSites();
  }
}

void JS::Zone::traceWeakJitScripts(JSTracer* trc) {
  if (jitZone()) {
    jitZone()->forEachJitScript(
        [&](jit::JitScript* jitScript) { jitScript->traceWeak(trc); });
  }
}

void JS::Zone::beforeClearDelegateInternal(JSObject* wrapper,
                                           JSObject* delegate) {
  // 'delegate' is no longer the delegate of 'wrapper'.
  MOZ_ASSERT(js::gc::detail::GetDelegate(wrapper) == delegate);
  MOZ_ASSERT(needsMarkingBarrier());
  MOZ_ASSERT(!RuntimeFromMainThreadIsHeapMajorCollecting(this));

  // |wrapper| might be a key in a weak map, so trigger a barrier to account for
  // the removal of the automatically added edge from delegate to wrapper.
  PreWriteBarrier(wrapper);
}

#ifdef JSGC_HASH_TABLE_CHECKS
void JS::Zone::checkUniqueIdTableAfterMovingGC() {
  CheckTableAfterMovingGC(uniqueIds(), [this](const auto& entry) {
    js::gc::CheckGCThingAfterMovingGC(entry.key(), this);
    return entry.key();
  });
}
#endif

js::jit::JitZone* Zone::createJitZone(JSContext* cx) {
  MOZ_ASSERT(!jitZone_);
#ifndef ENABLE_PORTABLE_BASELINE_INTERP
  MOZ_ASSERT(cx->runtime()->hasJitRuntime());
#endif

  auto jitZone = cx->make_unique<jit::JitZone>(cx, allocNurseryStrings());
  if (!jitZone) {
    return nullptr;
  }

  jitZone_ = jitZone.release();
  return jitZone_;
}

bool Zone::hasMarkedRealms() {
  for (RealmsInZoneIter realm(this); !realm.done(); realm.next()) {
    if (realm->marked()) {
      return true;
    }
  }
  return false;
}

void Zone::notifyObservingDebuggers() {
  AutoAssertNoGC nogc;
  MOZ_ASSERT(JS::RuntimeHeapIsCollecting(),
             "This method should be called during GC.");

  JSRuntime* rt = runtimeFromMainThread();

  for (RealmsInZoneIter realms(this); !realms.done(); realms.next()) {
    GlobalObject* global = realms->unsafeUnbarrieredMaybeGlobal();
    if (!global) {
      continue;
    }

    DebugAPI::notifyParticipatesInGC(global, rt->gc.majorGCCount());
  }
}

bool Zone::isOnList() const { return listNext_ != NotOnList; }

Zone* Zone::nextZone() const {
  MOZ_ASSERT(isOnList());
  return listNext_;
}

void Zone::prepareForMovingGC() {
  JS::GCContext* gcx = runtimeFromMainThread()->gcContext();
  discardJitCodeForAllRealms(gcx);
}

void Zone::fixupAfterMovingGC() {
  ZoneAllocator::fixupAfterMovingGC();
  shapeZone().fixupPropMapShapeTableAfterMovingGC();
}

void Zone::purgeAtomCache() {
  atomCache_.ref().reset();

  // Also purge the dtoa caches so that subsequent lookups populate atom
  // cache too.
  for (RealmsInZoneIter r(this); !r.done(); r.next()) {
    r->dtoaCache.purge();
  }
}

void Zone::addSizeOfIncludingThis(
    mozilla::MallocSizeOf mallocSizeOf, size_t* zoneObject, JS::CodeSizes* code,
    size_t* regexpZone, size_t* jitZone, size_t* objectFusesArg,
    size_t* uniqueIdMap, size_t* initialPropMapTable, size_t* shapeTables,
    size_t* atomReferenceBitmaps, size_t* compartmentObjects,
    size_t* crossCompartmentWrappersTables, size_t* compartmentsPrivateData,
    size_t* scriptCountsMapArg) {
  *zoneObject += mallocSizeOf(this);
  *regexpZone += regExps().sizeOfIncludingThis(mallocSizeOf);
  if (jitZone_) {
    jitZone_->addSizeOfIncludingThis(mallocSizeOf, code, jitZone);
  }
  *objectFusesArg += objectFuses.sizeOfExcludingThis(mallocSizeOf);
  *uniqueIdMap += uniqueIds().shallowSizeOfExcludingThis(mallocSizeOf);
  shapeZone().addSizeOfExcludingThis(mallocSizeOf, initialPropMapTable,
                                     shapeTables);
  *atomReferenceBitmaps += referencedAtoms().sizeOfExcludingThis(mallocSizeOf);
  *crossCompartmentWrappersTables +=
      crossZoneStringWrappers().sizeOfExcludingThis(mallocSizeOf);

  for (CompartmentsInZoneIter comp(this); !comp.done(); comp.next()) {
    comp->addSizeOfIncludingThis(mallocSizeOf, compartmentObjects,
                                 crossCompartmentWrappersTables,
                                 compartmentsPrivateData);
  }

  if (scriptCountsMap) {
    ScriptCountsMap& map = scriptCountsMap->get();
    *scriptCountsMapArg += map.shallowSizeOfIncludingThis(mallocSizeOf);
    for (auto iter = map.iter(); !iter.done(); iter.next()) {
      *scriptCountsMapArg +=
          iter.get().value()->sizeOfIncludingThis(mallocSizeOf);
    }
  }
}

void* ZoneAllocator::onOutOfMemory(js::AllocFunction allocFunc,
                                   arena_id_t arena, size_t nbytes,
                                   void* reallocPtr) {
  if (!js::CurrentThreadCanAccessRuntime(runtime_)) {
    return nullptr;
  }
  // The analysis sees that JSRuntime::onOutOfMemory could report an error,
  // which with a JSErrorInterceptor could GC. But we're passing a null cx (to
  // a default parameter) so the error will not be reported.
  JS::AutoSuppressGCAnalysis suppress;
  return runtimeFromMainThread()->onOutOfMemory(allocFunc, arena, nbytes,
                                                reallocPtr);
}

void ZoneAllocator::reportAllocOverflow() const {
  js::ReportAllocationOverflow(static_cast<JSContext*>(nullptr));
}

ZoneList::ZoneList() : head(nullptr), tail(nullptr) {}

ZoneList::ZoneList(Zone* zone) : head(zone), tail(zone) {
  MOZ_RELEASE_ASSERT(!zone->isOnList());
  zone->listNext_ = nullptr;
}

ZoneList::~ZoneList() { MOZ_ASSERT(isEmpty()); }

void ZoneList::check() const {
#ifdef DEBUG
  MOZ_ASSERT((head == nullptr) == (tail == nullptr));
  if (!head) {
    return;
  }

  Zone* zone = head;
  for (;;) {
    MOZ_ASSERT(zone && zone->isOnList());
    if (zone == tail) break;
    zone = zone->listNext_;
  }
  MOZ_ASSERT(!zone->listNext_);
#endif
}

bool ZoneList::isEmpty() const { return head == nullptr; }

Zone* ZoneList::front() const {
  MOZ_ASSERT(!isEmpty());
  MOZ_ASSERT(head->isOnList());
  return head;
}

void ZoneList::prepend(Zone* zone) { prependList(ZoneList(zone)); }

void ZoneList::append(Zone* zone) { appendList(ZoneList(zone)); }

void ZoneList::prependList(ZoneList&& other) {
  check();
  other.check();

  if (other.isEmpty()) {
    return;
  }

  MOZ_ASSERT(tail != other.tail);

  if (!isEmpty()) {
    other.tail->listNext_ = head;
  } else {
    tail = other.tail;
  }
  head = other.head;

  other.head = nullptr;
  other.tail = nullptr;
}

void ZoneList::appendList(ZoneList&& other) {
  check();
  other.check();

  if (other.isEmpty()) {
    return;
  }

  MOZ_ASSERT(tail != other.tail);

  if (!isEmpty()) {
    tail->listNext_ = other.head;
  } else {
    head = other.head;
  }
  tail = other.tail;

  other.head = nullptr;
  other.tail = nullptr;
}

Zone* ZoneList::removeFront() {
  MOZ_ASSERT(!isEmpty());
  check();

  Zone* front = head;
  head = head->listNext_;
  if (!head) {
    tail = nullptr;
  }

  front->listNext_ = Zone::NotOnList;

  return front;
}

void ZoneList::clear() {
  while (!isEmpty()) {
    removeFront();
  }
}

JS_PUBLIC_API void JS::shadow::RegisterWeakCache(
    JS::Zone* zone, detail::WeakCacheBase* cachep) {
  zone->registerWeakCache(cachep);
}

void Zone::traceRootsInMajorGC(JSTracer* trc) {
  if (trc->isMarkingTracer() && !isGCMarking()) {
    return;
  }

  // Trace zone script-table roots. See comment below for justification re:
  // calling this only during major (non-nursery) collections.
  traceScriptTableRoots(trc);
}

void Zone::traceScriptTableRoots(JSTracer* trc) {
  static_assert(std::is_convertible_v<BaseScript*, gc::TenuredCell*>,
                "BaseScript must not be nursery-allocated for script-table "
                "tracing to work");

  // Performance optimization: the script-table keys are JSScripts, which
  // cannot be in the nursery, so we can skip this tracing if we are only in a
  // minor collection. We static-assert this fact above.
  MOZ_ASSERT(!JS::RuntimeHeapIsMinorCollecting());

  // N.B.: the script-table keys are weak *except* in an exceptional case: when
  // then --dump-bytecode command line option or the PCCount JSFriend API is
  // used, then the scripts for all counts must remain alive. We only trace
  // when the `trc->runtime()->profilingScripts` flag is set. This flag is
  // cleared in JSRuntime::destroyRuntime() during shutdown to ensure that
  // scripts are collected before the runtime goes away completely.
  if (scriptCountsMap && trc->runtime()->profilingScripts) {
    for (auto iter = scriptCountsMap->get().iter(); !iter.done(); iter.next()) {
      BaseScript* script = iter.get().key();
      MOZ_ASSERT(script->hasScriptCounts());
      TraceRoot(trc, &script, "profilingScripts");
    }
  }

  // Trace the debugger's DebugScript weak map.
  if (debugScriptMap) {
    DebugAPI::traceDebugScriptMap(trc, debugScriptMap);
  }

  // Trace the table used to hold interpreter entry code generated with
  // --emit-interpreter-entry.
  if (jitZone()) {
    jitZone()->traceScriptTableRoots(trc);
  }
}

#ifdef DEBUG
bool Zone::hasLocalSymbol(JS::Symbol* symbol) {
  MOZ_ASSERT(symbol->isLocal());
  auto ptr = localSymbolMap().lookup(symbol->sharedSymbol());
  MOZ_ASSERT_IF(ptr, ptr->value() == symbol);
  return ptr.found();
}
#endif

void Zone::traceIncomingSymbolEdgesForZoneGC(JSTracer* trc,
                                             gc::EdgeSelector whichEdges) {
  MOZ_ASSERT(trc->runtime()->atomsZone()->isGCMarking());

  for (auto iter = localSymbolMap().iter(); !iter.done(); iter.next()) {
    MOZ_ASSERT(iter.get().key()->isShared());
    MOZ_ASSERT(iter.get().value()->isLocal());
    if (ShouldTraceEdge(iter.get().value().get(), whichEdges)) {
      TraceEdge(trc, &iter.get().key(), "shared symbol");
    }
  }
}

void Zone::traceWeakLocalSymbolEdges(JSTracer* trc) {
  for (auto iter = localSymbolMap().modIter(); !iter.done(); iter.next()) {
    MOZ_ASSERT(iter.get().key()->isShared());
    MOZ_ASSERT(iter.get().value()->isLocal());
    auto valueResult = TraceWeakEdge(trc, &iter.get().value(), "local symbol");
    if (valueResult.isDead()) {
      iter.remove();
      continue;
    }
#ifdef DEBUG
    auto keyResult = TraceWeakEdge(trc, &iter.get().key(), "shared symbol");
    MOZ_ASSERT(keyResult.isLive());  // Held live by the local symbol's pointer.
    MOZ_ASSERT(!keyResult.wasMoved());
#endif
  }
  localSymbolMap().compact();
}

#ifdef JSGC_HASH_TABLE_CHECKS
void Zone::checkScriptMapsAfterMovingGC() {
  // |debugScriptMap| is checked automatically because it is s a WeakMap.

  if (scriptCountsMap) {
    CheckTableAfterMovingGC(scriptCountsMap->get(), [this](const auto& entry) {
      BaseScript* script = entry.key();
      CheckGCThingAfterMovingGC(script, this);
      return script;
    });
  }

  if (scriptLCovMap) {
    CheckTableAfterMovingGC(scriptLCovMap->get(), [this](const auto& entry) {
      BaseScript* script = entry.key();
      CheckGCThingAfterMovingGC(script, this);
      return script;
    });
  }

#  ifdef MOZ_VTUNE
  if (scriptVTuneIdMap) {
    CheckTableAfterMovingGC(scriptVTuneIdMap->get(), [this](const auto& entry) {
      BaseScript* script = entry.key();
      CheckGCThingAfterMovingGC(script, this);
      return script;
    });
  }
#  endif  // MOZ_VTUNE

#  ifdef JS_CACHEIR_SPEW
  if (scriptFinalWarmUpCountMap) {
    CheckTableAfterMovingGC(scriptFinalWarmUpCountMap->get(),
                            [this](const auto& entry) {
                              BaseScript* script = entry.key();
                              CheckGCThingAfterMovingGC(script, this);
                              return script;
                            });
  }
#  endif  // JS_CACHEIR_SPEW

  if (profilerStrings) {
    ProfileStringMap& map = profilerStrings->get();
    CheckTableAfterMovingGC(map, [this](const auto& entry) {
      BaseScript* script = entry.key();
      CheckGCThingAfterMovingGC(script, this);
      return script;
    });
  }
}
#endif

void Zone::clearScriptCounts(Realm* realm) {
  if (!scriptCountsMap) {
    return;
  }

  // Clear all hasScriptCounts_ flags of BaseScript, in order to release all
  // ScriptCounts entries of the given realm.
  for (auto i = scriptCountsMap->get().modIter(); !i.done(); i.next()) {
    const HeapPtr<BaseScript*>& script = i.get().key();
    if (IsAboutToBeFinalized(script)) {
      // Dead scripts may be present during incremental GC until script
      // finalizers have been run.
      continue;
    }

    if (script->realm() != realm) {
      continue;
    }
    // We can't destroy the ScriptCounts yet if the script has Baseline code,
    // because Baseline code bakes in pointers to the counters. The ScriptCounts
    // will be destroyed in Zone::discardJitCode when discarding the JitScript.
    if (script->hasBaselineScript()) {
      continue;
    }
    script->clearHasScriptCounts();
    i.remove();
  }
}

void Zone::clearScriptLCov(Realm* realm) {
  if (!scriptLCovMap) {
    return;
  }

  for (auto i = scriptLCovMap->get().modIter(); !i.done(); i.next()) {
    const HeapPtr<BaseScript*>& script = i.get().key();
    if (IsAboutToBeFinalized(script)) {
      // Dead scripts may be present during incremental GC until script
      // finalizers have been run.
      continue;
    }

    if (script->realm() == realm) {
      i.remove();
    }
  }
}

void Zone::clearRootsForShutdownGC() {
  // Finalization callbacks are not called if we're shutting down.
  if (finalizationObservers()) {
    finalizationObservers()->clearRecords();
  }

  clearKeptObjects();
}

void Zone::finishRoots() {
  if (jitZone()) {
    jitZone()->finishScriptTableRoots();
  }

  for (RealmsInZoneIter r(this); !r.done(); r.next()) {
    r->finishRoots();
  }
}

void Zone::traceKeptObjects(JSTracer* trc) { keptAliveSet.ref().trace(trc); }

bool Zone::addToKeptObjects(HandleValue target) {
  MOZ_ASSERT(CanBeHeldWeakly(target));
  MOZ_ASSERT_IF(target.isSymbol(),
                !target.toSymbol()->isPermanentAndMayBeShared());

  return keptAliveSet.ref().put(target);
}

void Zone::clearKeptObjects() { keptAliveSet.ref().clear(); }

bool Zone::ensureFinalizationObservers() {
  if (finalizationObservers_.ref()) {
    return true;
  }

  finalizationObservers_ = js::MakeUnique<FinalizationObservers>(this);
  return bool(finalizationObservers_.ref());
}

bool Zone::registerObjectWithWeakPointers(JSObject* obj) {
  MOZ_ASSERT(obj->getClass()->hasTrace());
  MOZ_ASSERT(!IsInsideNursery(obj));
  return objectsWithWeakPointers.ref().append(obj);
}
