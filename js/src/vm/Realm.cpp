/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "js/shadow/Realm.h"  // JS::shadow::Realm

#include "mozilla/MemoryReporting.h"

#include <stddef.h>

#include "jsfriendapi.h"

#include "debugger/DebugAPI.h"
#include "debugger/Debugger.h"
#include "gc/GC.h"
#include "jit/JitRuntime.h"
#include "js/CallAndConstruct.h"      // JS::IsCallable
#include "js/friend/ErrorMessages.h"  // js::GetErrorMessage, JSMSG_*
#include "js/GCVariant.h"
#include "js/Proxy.h"
#include "js/RootingAPI.h"
#include "js/Wrapper.h"
#include "util/DefaultLocale.h"
#include "util/RandomSeed.h"
#include "vm/Compartment.h"
#include "vm/DateTime.h"
#include "vm/Iteration.h"
#include "vm/JSContext.h"
#include "wasm/WasmInstance.h"

#include "gc/Marking-inl.h"
#include "gc/StableCellHasher-inl.h"
#include "gc/WeakMap-inl.h"
#include "vm/JSObject-inl.h"
#include "vm/Realm-inl.h"

using namespace js;

Realm::DebuggerVectorEntry::DebuggerVectorEntry(js::Debugger* dbg_,
                                                JSObject* link)
    : dbg(dbg_), debuggerLink(link) {}

ObjectRealm::ObjectRealm(JS::Zone* zone)
    : innerViews(zone, zone),
      moduleScriptSources(zone, zone),
      iteratorCache(zone) {}

Realm::Realm(Compartment* comp, const JS::RealmOptions& options)
    : JS::shadow::Realm(comp),
      zone_(comp->zone()),
      runtime_(comp->runtimeFromMainThread()),
      creationOptions_(options.creationOptions()),
      behaviors_(options.behaviors()),
      objects_(zone_),
      randomKeyGenerator_(runtime_->forkRandomKeyGenerator()),
      debuggers_(zone_),
      allocatedDuringIncrementalGC_(zone_->isGCMarkingOrSweeping() ||
                                    zone_->isGCFinished()),
      wasm(runtime_) {
  runtime_->numRealms++;
}

Realm::~Realm() {
  MOZ_ASSERT(!hasBeenEnteredIgnoringJit());
  MOZ_ASSERT(!localAllocSite);
  MOZ_ASSERT_IF(isDebuggee(), isTracingExecution_);

  // Write the code coverage information in a file.
  if (lcovRealm_) {
    runtime_->lcovOutput().writeLCovResult(*lcovRealm_);
  }

  if (hasAllocationMetadataBuilder_) {
    disableAllocationMetadataBuilder();
  }

  if (isTracingExecution_) {
    disableExecutionTracing();
  }

  MOZ_ASSERT(runtime_->numRealms > 0);
  runtime_->numRealms--;
}

void Realm::init(JSContext* cx, JSPrincipals* principals) {
  /*
   * As a hack, we clear our timezone cache every time we create a new realm.
   * This ensures that the cache is always relatively fresh, but shouldn't
   * interfere with benchmarks that create tons of date objects (unless they
   * also create tons of iframes, which seems unlikely).
   */
  js::ResetTimeZoneInternal(ResetTimeZoneMode::DontResetIfOffsetUnchanged);

  if (principals) {
    // Any realm with the trusted principals -- and there can be
    // multiple -- is a system realm.
    isSystem_ = (principals == cx->runtime()->trustedPrincipals());
    JS_HoldPrincipals(principals);
    principals_ = principals;
  }

  if (!isSystem_ && cx->hasExecutionTracer()) {
    enableExecutionTracing();
  }
}

bool JSRuntime::createJitRuntime(JSContext* cx) {
  using namespace js::jit;

  MOZ_ASSERT(!jitRuntime_);

  if (!CanLikelyAllocateMoreExecutableMemory()) {
    // Try to release memory first instead of potentially reporting OOM below.
    if (OnLargeAllocationFailure) {
      OnLargeAllocationFailure();
    }
  }

  jit::JitRuntime* jrt = cx->new_<jit::JitRuntime>();
  if (!jrt) {
    return false;
  }

  // Unfortunately, initialization depends on jitRuntime_ being non-null, so
  // we can't just wait to assign jitRuntime_.
  jitRuntime_ = jrt;

  if (!jitRuntime_->initialize(cx)) {
    js_delete(jitRuntime_.ref());
    jitRuntime_ = nullptr;
    return false;
  }

  return true;
}

#ifdef JSGC_HASH_TABLE_CHECKS

void js::DtoaCache::checkCacheAfterMovingGC() {
  MOZ_ASSERT(!str || !IsForwarded(str));
}

#endif  // JSGC_HASH_TABLE_CHECKS

NonSyntacticLexicalEnvironmentObject*
ObjectRealm::getOrCreateNonSyntacticLexicalEnvironment(JSContext* cx,
                                                       HandleObject enclosing,
                                                       HandleObject key,
                                                       HandleObject thisv) {
  MOZ_ASSERT(&ObjectRealm::get(enclosing) == this);

  if (!nonSyntacticLexicalEnvironments_) {
    auto map = cx->make_unique<NonSyntacticLexialEnvironmentsMap>(cx->zone());
    if (!map) {
      return nullptr;
    }

    nonSyntacticLexicalEnvironments_ = std::move(map);
  }

  JSObject* obj = nonSyntacticLexicalEnvironments_->get(key);
  if (obj) {
    return &obj->as<NonSyntacticLexicalEnvironmentObject>();
  }

  MOZ_ASSERT(key->is<NonSyntacticVariablesObject>() ||
             !key->is<EnvironmentObject>());

  NonSyntacticLexicalEnvironmentObject* lexicalEnv =
      NonSyntacticLexicalEnvironmentObject::create(cx, enclosing, thisv);
  if (!lexicalEnv) {
    return nullptr;
  }

  if (!nonSyntacticLexicalEnvironments_->put(key, lexicalEnv)) {
    ReportOutOfMemory(cx);
    return nullptr;
  }

  return lexicalEnv;
}

NonSyntacticLexicalEnvironmentObject*
ObjectRealm::getOrCreateNonSyntacticLexicalEnvironment(
    JSContext* cx, Handle<NonSyntacticVariablesObject*> enclosing) {
  HandleObject key = enclosing;

  // NOTE: The default global |this| value is set to key for compatibility
  // with existing users of the lexical environment cache.
  //  - When used by shared-global JSM loader, |this| must be the
  //    NonSyntacticVariablesObject passed as enclosing.
  // See js::GetFunctionThis / js::GetNonSyntacticGlobalThis
  return getOrCreateNonSyntacticLexicalEnvironment(cx, enclosing, key,
                                                   /*thisv = */ key);
}

NonSyntacticLexicalEnvironmentObject*
ObjectRealm::getOrCreateNonSyntacticLexicalEnvironment(
    JSContext* cx, Handle<WithEnvironmentObject*> enclosing) {
  MOZ_ASSERT(!enclosing->isSyntactic());

  // If a wrapped WithEnvironmentObject was passed in, unwrap it, as we may
  // be creating different WithEnvironmentObject wrappers each time.
  RootedObject key(cx, &enclosing->as<WithEnvironmentObject>().object());

  // NOTE: The default global |this| value is set to key for compatibility
  // with existing users of the lexical environment cache.
  //  - When used by SubscriptLoader, |this| must be the target object of
  //    the WithEnvironmentObject wrapper.
  //  - When used by DOM Events, we execute directly as a function and do not
  //    access the |this| value.
  // See js::GetFunctionThis / js::GetNonSyntacticGlobalThis
  return getOrCreateNonSyntacticLexicalEnvironment(cx, enclosing, key,
                                                   /*thisv = */ key);
}

NonSyntacticLexicalEnvironmentObject*
ObjectRealm::getOrCreateNonSyntacticLexicalEnvironment(
    JSContext* cx, Handle<WithEnvironmentObject*> enclosing,
    Handle<NonSyntacticVariablesObject*> key) {
  MOZ_ASSERT(!enclosing->isSyntactic());

  RootedObject thisv(cx, &enclosing->object());
  return getOrCreateNonSyntacticLexicalEnvironment(cx, enclosing, key, thisv);
}

NonSyntacticLexicalEnvironmentObject*
ObjectRealm::getNonSyntacticLexicalEnvironment(JSObject* key) const {
  MOZ_ASSERT(&ObjectRealm::get(key) == this);

  if (!nonSyntacticLexicalEnvironments_) {
    return nullptr;
  }
  // If a wrapped WithEnvironmentObject was passed in, unwrap it as in
  // getOrCreateNonSyntacticLexicalEnvironment.
  if (key->is<WithEnvironmentObject>()) {
    MOZ_ASSERT(!key->as<WithEnvironmentObject>().isSyntactic());
    key = &key->as<WithEnvironmentObject>().object();
  }
  JSObject* lexicalEnv = nonSyntacticLexicalEnvironments_->get(key);
  if (!lexicalEnv) {
    return nullptr;
  }
  return &lexicalEnv->as<NonSyntacticLexicalEnvironmentObject>();
}

void Realm::traceGlobalData(JSTracer* trc) {
  // Trace things reachable from the realm's global. Note that these edges
  // must be swept too in case the realm is live but the global is not.

  savedStacks_.trace(trc);

  DebugAPI::traceFromRealm(trc, this);
}

void Realm::traceGlobalRoot(JSTracer* trc, const char* name) {
  if (global_) {
    TraceRoot(trc, global_.unbarrieredAddress(), name);
  }
}

void ObjectRealm::trace(JSTracer* trc) {
  if (objectMetadataTable) {
    objectMetadataTable->trace(trc);
  }

  if (nonSyntacticLexicalEnvironments_) {
    nonSyntacticLexicalEnvironments_->trace(trc);
  }
}

void Realm::traceRoots(JSTracer* trc,
                       js::gc::GCRuntime::TraceOrMarkRuntime traceOrMark) {
  // It's not possible to trigger a GC between allocating the pending object and
  // setting its meta data in ~AutoSetNewObjectMetadata.
  MOZ_RELEASE_ASSERT(!objectPendingMetadata_);

  if (!JS::RuntimeHeapIsMinorCollecting()) {
    // The global is never nursery allocated, so we don't need to
    // trace it when doing a minor collection.
    //
    // If a realm is on-stack, we mark its global so that JSContext::global()
    // remains valid.
    if (shouldTraceGlobal()) {
      traceGlobalRoot(trc, "on-stack realm global");
    }

    // If the realm is still being initialized we set a flag so that it doesn't
    // get deleted, since there may be GC things that contain pointers to it.
    if (shouldTraceGlobal() && initializingGlobal_) {
      allocatedDuringIncrementalGC_ = true;
    }
  }

  // Nothing below here needs to be treated as a root if we aren't marking
  // this zone for a collection.
  if (traceOrMark == js::gc::GCRuntime::MarkRuntime &&
      !zone()->isCollectingFromAnyThread()) {
    return;
  }

  /* Mark debug scopes, if present */
  if (debugEnvs_) {
    debugEnvs_->trace(trc);
  }

  objects_.trace(trc);
  jitRealm_.trace(trc);
}

void ObjectRealm::finishRoots() {
  if (objectMetadataTable) {
    objectMetadataTable->clear();
  }

  if (nonSyntacticLexicalEnvironments_) {
    nonSyntacticLexicalEnvironments_->clear();
  }
}

void Realm::finishRoots() {
  if (debugEnvs_) {
    debugEnvs_->finish();
  }

  objects_.finishRoots();
  jitRealm_.finishRoots();
}

void ObjectRealm::sweepAfterMinorGC(JSTracer* trc) {
  InnerViewTable& table = innerViews.get();
  if (table.needsSweepAfterMinorGC()) {
    table.sweepAfterMinorGC(trc);
  }
}

void Realm::sweepAfterMinorGC(JSTracer* trc) {
  globalWriteBarriered = 0;
  dtoaCache.purge();
  objects_.sweepAfterMinorGC(trc);
}

#ifdef JSGC_HASH_TABLE_CHECKS
void Realm::checkModuleScriptSourcesAfterMovingGC() {
  objects_.checkModuleScriptSourcesAfterMovingGC(zone_);
}

void ObjectRealm::checkModuleScriptSourcesAfterMovingGC(JS::Zone* zone) {
  gc::CheckTableAfterMovingGC(moduleScriptSources, [zone](const auto& entry) {
    gc::CheckGCThingAfterMovingGC(entry, zone);
    return entry.unbarrieredGet();
  });
}
#endif

void Realm::traceWeakSavedStacks(JSTracer* trc) { savedStacks_.traceWeak(trc); }

void Realm::traceWeakGlobalEdge(JSTracer* trc) {
  // If the global is dead, free its GlobalObjectData.
  auto result = TraceWeakEdge(trc, &global_, "Realm::global_");
  if (result.isDead()) {
    global_ = nullptr;
    result.initialTarget()->releaseData(runtime_->gcContext());
  }
}

void Realm::traceWeakDebugEnvironmentEdges(JSTracer* trc) {
  if (debugEnvs_) {
    debugEnvs_->traceWeak(trc);
  }
}

void Realm::fixupAfterMovingGC(JSTracer* trc) {
  purge();
  traceWeakGlobalEdge(trc);
}

void Realm::purge() {
  dtoaCache.purge();
  newProxyCache.purge();
  newPlainObjectWithPropsCache.purge();
  plainObjectAssignCache.purge();
  plainObjectSpreadCache.purge();
  objects_.iteratorCache.clearAndCompact();
}

void Realm::enableAllocationMetadataBuilder() {
  if (hasAllocationMetadataBuilder_) {
    return;
  }

  // Clear any jitcode in the runtime, which behaves differently depending on
  // whether there is an allocation metadata builder.
  ReleaseAllJITCode(runtime_->gcContext());
  zone()->incNumRealmsWithAllocMetadataBuilder();

  for (auto iter = wasm.instances().iter(); !iter.done(); iter.next()) {
    iter.get()->setHasAllocationMetadataBuilder(true);
  }
  hasAllocationMetadataBuilder_ = true;
}

void Realm::disableAllocationMetadataBuilder() {
  if (!hasAllocationMetadataBuilder_) {
    return;
  }

  // Unlike enableAllocationMetadataBuilder, we don't have to discard all JIT
  // code here (code is still valid, just a bit slower because it doesn't do
  // inline GC allocations when a metadata builder is present), but we do want
  // to cancel off-thread Ion compilations to avoid races when Ion accesses
  // numRealmsWithAllocMetadataBuilder_ off-thread.
  CancelOffThreadIonCompile(zone());

  zone()->decNumRealmsWithAllocMetadataBuilder();

  for (auto iter = wasm.instances().iter(); !iter.done(); iter.next()) {
    iter.get()->setHasAllocationMetadataBuilder(false);
  }
  hasAllocationMetadataBuilder_ = false;
}

void Realm::setNewObjectMetadata(JSContext* cx, HandleObject obj) {
  MOZ_ASSERT(obj->maybeCCWRealm() == this);
  cx->check(compartment(), obj);

  AutoEnterOOMUnsafeRegion oomUnsafe;
  if (SavedFrame* metadata =
          SavedStacks::buildAllocationMetadata(cx, obj, oomUnsafe)) {
    MOZ_ASSERT(metadata->maybeCCWRealm() == obj->maybeCCWRealm());
    cx->check(metadata);

    if (!objects_.objectMetadataTable) {
      auto table =
          cx->make_unique<ObjectRealm::ObjectMetadataTable>(cx->zone());
      if (!table) {
        oomUnsafe.crash("setNewObjectMetadata");
      }

      objects_.objectMetadataTable = std::move(table);
    }

    if (!objects_.objectMetadataTable->put(obj, metadata)) {
      oomUnsafe.crash("setNewObjectMetadata");
    }
  }
}

SavedFrame* js::GetAllocationMetadata(JSObject* obj) {
  ObjectRealm::ObjectMetadataTable* map =
      ObjectRealm::get(obj).objectMetadataTable.get();
  if (map) {
    auto ptr = map->lookup(obj);
    if (ptr) {
      return ptr->value();
    }
  }
  return nullptr;
}

void Realm::updateDebuggerObservesFlag(unsigned flag) {
  MOZ_ASSERT(isDebuggee());
  MOZ_ASSERT(flag == DebuggerObservesAllExecution ||
             flag == DebuggerObservesCoverage || flag == DebuggerObservesWasm ||
             flag == DebuggerObservesNativeCall);

  GlobalObject* global =
      zone()->runtimeFromMainThread()->gc.isForegroundSweeping()
          ? unsafeUnbarrieredMaybeGlobal()
          : maybeGlobal();
  bool observes = false;
  if (flag == DebuggerObservesAllExecution) {
    observes = (global && DebugAPI::debuggerObservesAllExecution(global)) ||
               isTracingExecution_;
  } else if (flag == DebuggerObservesCoverage) {
    observes = DebugAPI::debuggerObservesCoverage(global);
  } else if (flag == DebuggerObservesWasm) {
    observes = DebugAPI::debuggerObservesWasm(global);
  } else if (flag == DebuggerObservesNativeCall) {
    observes = DebugAPI::debuggerObservesNativeCall(global);
  }

  if (observes) {
    debugModeBits_ |= flag;
  } else {
    debugModeBits_ &= ~flag;
  }
}

void Realm::setIsDebuggee() {
  if (!isDebuggee()) {
    debugModeBits_ |= IsDebuggee;
    runtimeFromMainThread()->incrementNumDebuggeeRealms();
  }
}

void Realm::unsetIsDebuggee() {
  if (isDebuggee()) {
    if (debuggerObservesCoverage()) {
      runtime_->decrementNumDebuggeeRealmsObservingCoverage();
    }
    debugModeBits_ = 0;
    DebugEnvironments::onRealmUnsetIsDebuggee(this);
    runtimeFromMainThread()->decrementNumDebuggeeRealms();
  }
}

void Realm::restoreDebugModeBitsOnOOM(uint32_t bits) {
  // This is called from Debugger::addDebuggeeGlobal after calling
  // Realm::setIsDebuggee. If the realm was not a debuggee realm before, we need
  // to call unsetIsDebuggee to update counters on the JSRuntime.

  MOZ_RELEASE_ASSERT(isDebuggee());

  if (!(bits & IsDebuggee)) {
    MOZ_ASSERT(bits == 0);
    unsetIsDebuggee();
    MOZ_ASSERT(debugModeBits_ == 0);
  } else {
    debugModeBits_ = bits;
  }
}

void Realm::updateDebuggerObservesCoverage() {
  bool previousState = debuggerObservesCoverage();
  updateDebuggerObservesFlag(DebuggerObservesCoverage);
  if (previousState == debuggerObservesCoverage()) {
    return;
  }

  if (debuggerObservesCoverage()) {
    // Interrupt any running interpreter frame. The scriptCounts are
    // allocated on demand when a script resumes its execution.
    JSContext* cx = TlsContext.get();
    for (ActivationIterator iter(cx); !iter.done(); ++iter) {
      if (iter->isInterpreter()) {
        iter->asInterpreter()->enableInterruptsUnconditionally();
      }
    }
    runtime_->incrementNumDebuggeeRealmsObservingCoverage();
    return;
  }

  runtime_->decrementNumDebuggeeRealmsObservingCoverage();

  // If code coverage is enabled by any other means, keep it.
  if (collectCoverageForDebug()) {
    return;
  }

  clearScriptCounts();
  clearScriptLCov();
}

coverage::LCovRealm* Realm::lcovRealm() {
  if (!lcovRealm_) {
    lcovRealm_ = js::MakeUnique<coverage::LCovRealm>(this);
  }
  return lcovRealm_.get();
}

bool Realm::collectCoverageForDebug() const {
  return debuggerObservesCoverage() || coverage::IsLCovEnabled();
}

void Realm::clearScriptCounts() { zone()->clearScriptCounts(this); }

void Realm::clearScriptLCov() { zone()->clearScriptLCov(this); }

LanguageId Realm::getLocale() {
  if (RefPtr<LocaleString> locale = behaviors_.localeOverride()) {
    if (localeId_ == LanguageId::und()) {
      localeId_ = DefaultLocaleFrom(locale.get()->chars());

      // Replace "und" with "und-Zzzz-ZZ" to mark the locale as resolved.
      //
      // "und-Zzzz-ZZ" is an undetermined language with unknown script and
      // region.
      if (localeId_ == LanguageId::und()) {
        localeId_ = LanguageId::fromValidBcp49("und-Zzzz-ZZ");
      }
    }
    return localeId_;
  }
  return runtime_->getDefaultLocale();
}

void Realm::setLocaleOverride(const char* locale) {
  // Clear any jitcode in the runtime, because compiled code doesn't handle
  // updates to a realm's locale override.
  ReleaseAllJITCode(runtime_->gcContext());

  behaviors_.setLocaleOverride(locale);
  localeId_ = LanguageId::und();
}

js::DateTimeInfo* Realm::getDateTimeInfo() {
#if JS_HAS_INTL_API
  if (RefPtr<TimeZoneString> timeZone = behaviors_.timeZoneOverride()) {
    if (!dateTimeInfo_) {
      AutoEnterOOMUnsafeRegion oomUnsafe;

      // Crash on OOM because we don't have a good way to handle it here.
      dateTimeInfo_ = js::MakeUnique<js::DateTimeInfo>(timeZone);
      if (!dateTimeInfo_) {
        oomUnsafe.crash("getDateTimeInfo");
      }
    } else {
      dateTimeInfo_->updateTimeZoneOverride(timeZone);
    }
    return dateTimeInfo_.get();
  }
#endif
  return nullptr;
}

void Realm::setTimeZoneOverride(const char* timeZone) {
  // Clear any jitcode in the runtime, because compiled code doesn't handle
  // updates to a realm's time zone override.
  ReleaseAllJITCode(runtime_->gcContext());

  behaviors_.setTimeZoneOverride(timeZone);
}

void ObjectRealm::addSizeOfExcludingThis(
    mozilla::MallocSizeOf mallocSizeOf, size_t* innerViewsArg,
    size_t* objectMetadataTablesArg,
    size_t* nonSyntacticLexicalEnvironmentsArg) {
  *innerViewsArg += innerViews.sizeOfExcludingThis(mallocSizeOf);

  if (objectMetadataTable) {
    *objectMetadataTablesArg +=
        mallocSizeOf(objectMetadataTable.get()) +
        objectMetadataTable->shallowSizeOfExcludingThis(mallocSizeOf);
  }

  if (auto& map = nonSyntacticLexicalEnvironments_) {
    *nonSyntacticLexicalEnvironmentsArg +=
        mallocSizeOf(map.get()) + map->shallowSizeOfExcludingThis(mallocSizeOf);
  }
}

void Realm::addSizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf,
                                   size_t* realmObject, size_t* realmTables,
                                   size_t* innerViewsArg,
                                   size_t* objectMetadataTablesArg,
                                   size_t* savedStacksSet,
                                   size_t* nonSyntacticLexicalEnvironmentsArg,
                                   size_t* cacheIRStubs) {
  *realmObject += mallocSizeOf(this);
  wasm.addSizeOfExcludingThis(mallocSizeOf, realmTables);

  objects_.addSizeOfExcludingThis(mallocSizeOf, innerViewsArg,
                                  objectMetadataTablesArg,
                                  nonSyntacticLexicalEnvironmentsArg);

  *savedStacksSet += savedStacks_.sizeOfExcludingThis(mallocSizeOf);

  jitRealm_.addSizeOfExcludingThis(mallocSizeOf, cacheIRStubs);
}

bool Realm::shouldCaptureStackForThrow() {
  // Determine whether a stack trace should be captured for throw-statements (or
  // similar) in JS code in this realm. We don't want to do this unconditionally
  // because capturing stacks is slow and some scripts throw a lot of
  // exceptions.
  //
  // Note: this is unrelated to Error.stack! That property is observable from
  // JS code so we can't use these heuristics there. The code here is mostly
  // relevant for uncaught exceptions that are not Error objects.

  // To match other browsers, we always capture a stack trace if the realm is a
  // debuggee (this includes the devtools console being open) or if unlimited
  // stack traces have been enabled for this realm (used in automation).
  if (isDebuggee() || isUnlimitedStacksCapturingEnabled) {
    return true;
  }

  // Also always capture for chrome code. This is code we control and this helps
  // debugging.
  if (principals() &&
      principals() == runtimeFromMainThread()->trustedPrincipals()) {
    return true;
  }

  // Else, capture the stack only for the first N exceptions so that we can
  // still show stack traces for scripts that don't throw a lot of exceptions
  // (if the console is opened later).
  static constexpr uint16_t MaxStacksCapturedForThrow = 50;
  if (numStacksCapturedForThrow_ > MaxStacksCapturedForThrow) {
    return false;
  }
  numStacksCapturedForThrow_++;
  return true;
}

mozilla::HashCodeScrambler Realm::randomHashCodeScrambler() {
  return mozilla::HashCodeScrambler(randomKeyGenerator_.next(),
                                    randomKeyGenerator_.next());
}

void AutoSetNewObjectMetadata::setPendingMetadata() {
  JSObject* obj = cx_->realm()->getAndClearObjectPendingMetadata();
  if (!obj) {
    return;
  }

  MOZ_ASSERT(obj->getClass()->shouldDelayMetadataBuilder());

  if (cx_->isExceptionPending()) {
    return;
  }

  // This function is called from a destructor that often runs upon exit from
  // a function that is returning an unrooted pointer to a Cell. The
  // allocation metadata callback often allocates; if it causes a GC, then the
  // Cell pointer being returned won't be traced or relocated.
  //
  // The only extant callbacks are those internal to SpiderMonkey that
  // capture the JS stack. In fact, we're considering removing general
  // callbacks altogther in bug 1236748. Since it's not running arbitrary
  // code, it's adequate to simply suppress GC while we run the callback.
  gc::AutoSuppressGC autoSuppressGC(cx_);

  (void)SetNewObjectMetadata(cx_, obj);
}

JS_PUBLIC_API void gc::TraceRealmRoot(JSTracer* trc, JS::Realm* realm,
                                      const char* name) {
  // Trace the realm's global object to keep the realm alive.
  //
  // Note: this is called for Rooted<Realm*>. If a realm has been entered with
  // AutoRealm, the global object is traced in Realm::traceRoots.
  MOZ_RELEASE_ASSERT(realm->hasLiveGlobal(),
                     "we need to have a global to keep the realm alive");
  gc::AssertRootMarkingPhase(trc);
  realm->traceGlobalRoot(trc, "rooted realm");
}

JS_PUBLIC_API JS::Realm* JS::GetCurrentRealmOrNull(JSContext* cx) {
  return cx->realm();
}

JS_PUBLIC_API JS::Realm* JS::GetObjectRealmOrNull(JSObject* obj) {
  return IsCrossCompartmentWrapper(obj) ? nullptr : obj->nonCCWRealm();
}

JS_PUBLIC_API void* JS::GetRealmPrivate(JS::Realm* realm) {
  return realm->realmPrivate();
}

JS_PUBLIC_API bool JS::HasRealmInitializedGlobal(JS::Realm* realm) {
  return realm->hasInitializedGlobal();
}

JS_PUBLIC_API void JS::SetRealmPrivate(JS::Realm* realm, void* data) {
  realm->setRealmPrivate(data);
}

JS_PUBLIC_API void JS::SetDestroyRealmCallback(
    JSContext* cx, JS::DestroyRealmCallback callback) {
  cx->runtime()->gc.setDestroyRealmCallback(callback);
}

JS_PUBLIC_API void JS::SetRealmNameCallback(JSContext* cx,
                                            JS::RealmNameCallback callback) {
  cx->runtime()->realmNameCallback = callback;
}

JS_PUBLIC_API JSObject* JS::GetRealmGlobalOrNull(JS::Realm* realm) {
  return realm->maybeGlobal();
}

JS_PUBLIC_API bool JS::InitRealmStandardClasses(JSContext* cx) {
  MOZ_ASSERT(!cx->zone()->isAtomsZone());
  AssertHeapIsIdle();
  CHECK_THREAD(cx);
  return GlobalObject::initStandardClasses(cx, cx->global());
}

JS_PUBLIC_API bool JS::MaybeFreezeCtorAndPrototype(JSContext* cx,
                                                   HandleObject ctor,
                                                   HandleObject maybeProto) {
  if (MOZ_LIKELY(!cx->realm()->creationOptions().freezeBuiltins())) {
    return true;
  }
  if (!SetIntegrityLevel(cx, ctor, IntegrityLevel::Frozen)) {
    return false;
  }
  if (maybeProto) {
    if (!SetIntegrityLevel(cx, maybeProto, IntegrityLevel::Sealed)) {
      return false;
    }
  }
  return true;
}

JS_PUBLIC_API JSObject* JS::GetRealmObjectPrototype(JSContext* cx) {
  CHECK_THREAD(cx);
  return &cx->global()->getObjectPrototype();
}

JS_PUBLIC_API JS::Handle<JSObject*> JS::GetRealmObjectPrototypeHandle(
    JSContext* cx) {
  return cx->global()->getObjectPrototypeHandle();
}

JS_PUBLIC_API JSObject* JS::GetRealmFunctionPrototype(JSContext* cx) {
  CHECK_THREAD(cx);
  return &cx->global()->getFunctionPrototype();
}

JS_PUBLIC_API JS::Handle<JSObject*> JS::GetRealmFunctionPrototypeHandle(
    JSContext* cx) {
  CHECK_THREAD(cx);
  return cx->global()->getFunctionPrototypeHandle();
}

JS_PUBLIC_API JSObject* JS::GetRealmArrayPrototype(JSContext* cx) {
  CHECK_THREAD(cx);
  return GlobalObject::getOrCreateArrayPrototype(cx, cx->global());
}

JS_PUBLIC_API JSObject* JS::GetRealmErrorPrototype(JSContext* cx) {
  CHECK_THREAD(cx);
  return GlobalObject::getOrCreateCustomErrorPrototype(cx, cx->global(),
                                                       JSEXN_ERR);
}

JS_PUBLIC_API JSObject* JS::GetRealmIteratorPrototype(JSContext* cx) {
  CHECK_THREAD(cx);
  return GlobalObject::getOrCreateIteratorPrototype(cx, cx->global());
}

JS_PUBLIC_API JSObject* JS::GetRealmMapIteratorPrototype(JSContext* cx) {
  CHECK_THREAD(cx);
  return GlobalObject::getOrCreateMapIteratorPrototype(cx, cx->global());
}

JS_PUBLIC_API JSObject* JS::GetRealmSetIteratorPrototype(JSContext* cx) {
  CHECK_THREAD(cx);
  return GlobalObject::getOrCreateSetIteratorPrototype(cx, cx->global());
}

JS_PUBLIC_API JSObject* JS::GetRealmAsyncIteratorPrototype(JSContext* cx) {
  CHECK_THREAD(cx);
  return GlobalObject::getOrCreateAsyncIteratorPrototype(cx, cx->global());
}

JS_PUBLIC_API JSObject* JS::GetRealmKeyObject(JSContext* cx) {
  return GlobalObject::getOrCreateRealmKeyObject(cx, cx->global());
}

JS_PUBLIC_API Realm* JS::GetFunctionRealm(JSContext* cx, HandleObject objArg) {
  // https://tc39.github.io/ecma262/#sec-getfunctionrealm
  // 7.3.22 GetFunctionRealm ( obj )

  CHECK_THREAD(cx);
  cx->check(objArg);

  RootedObject obj(cx, objArg);
  while (true) {
    obj = CheckedUnwrapStatic(obj);
    if (!obj) {
      ReportAccessDenied(cx);
      return nullptr;
    }

    // Step 1.
    MOZ_ASSERT(IsCallable(obj));

    // Steps 2 and 3. We use a loop instead of recursion to unwrap bound
    // functions.
    if (obj->is<JSFunction>()) {
      return obj->as<JSFunction>().realm();
    }
    if (obj->is<BoundFunctionObject>()) {
      obj = obj->as<BoundFunctionObject>().getTarget();
      continue;
    }

    // Step 4.
    if (IsScriptedProxy(obj)) {
      // Steps 4.a-b.
      JSObject* proxyTarget = GetProxyTargetObject(obj);
      if (!proxyTarget) {
        JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr,
                                  JSMSG_PROXY_REVOKED);
        return nullptr;
      }

      // Step 4.c.
      obj = proxyTarget;
      continue;
    }

    // Step 5.
    return cx->realm();
  }
}

JS_PUBLIC_API void JS::ResetRealmMathRandomSeed(JSContext* cx) {
  MOZ_ASSERT(cx->realm());
  auto& rng = cx->realm()->getOrCreateRandomNumberGenerator();
  mozilla::Array<uint64_t, 2> seed;
  GenerateXorShift128PlusSeed(seed);
  rng.setState(seed[0], seed[1]);
}
