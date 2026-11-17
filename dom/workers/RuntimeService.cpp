/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RuntimeService.h"

#include <algorithm>

#include "GeckoProfiler.h"
#include "XPCSelfHostedShmem.h"
#include "js/ColumnNumber.h"  // JS::ColumnNumberOneOrigin
#include "js/ContextOptions.h"
#include "js/GCVector.h"
#include "js/Initialization.h"
#include "js/LocaleSensitive.h"
#include "js/Value.h"
#include "js/WasmFeatures.h"
#include "js/experimental/CTypes.h"  // JS::CTypesActivityType, JS::SetCTypesActivityCallback
#include "js/friend/ErrorMessages.h"  // js::GetErrorMessage, JSMSG_*
#include "jsfriendapi.h"
#include "mozilla/AbstractThread.h"
#include "mozilla/Atomics.h"
#include "mozilla/Attributes.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/CycleCollectedJSRuntime.h"
#include "mozilla/FlowMarkers.h"
#include "mozilla/Monitor.h"
#include "mozilla/Preferences.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_javascript.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/AtomList.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/ErrorEventBinding.h"
#include "mozilla/dom/EventTargetBinding.h"
#include "mozilla/dom/FetchUtil.h"
#include "mozilla/dom/IndexedDatabaseManager.h"
#include "mozilla/dom/MessageChannel.h"
#include "mozilla/dom/MessageEventBinding.h"
#include "mozilla/dom/Navigator.h"
#include "mozilla/dom/PerformanceService.h"
#include "mozilla/dom/RemoteWorkerChild.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/TimeoutHandler.h"
#include "mozilla/dom/TrustedTypeUtils.h"
#include "mozilla/dom/WorkerBinding.h"
#include "mozilla/extensions/WebExtensionPolicy.h"
#include "mozilla/glean/DomServiceworkersMetrics.h"
#include "mozilla/glean/DomWorkersMetrics.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "nsContentSecurityUtils.h"
#include "nsContentUtils.h"
#include "nsCycleCollector.h"
#include "nsDOMJSUtils.h"
#include "nsGlobalWindowInner.h"
#include "nsIContentSecurityPolicy.h"
#include "nsIObserverService.h"
#include "nsIScriptContext.h"
#include "nsISerialEventTarget.h"
#include "nsIStreamTransportService.h"
#include "nsISupportsImpl.h"
#include "nsISupportsPriority.h"
#include "nsITimer.h"
#include "nsIURI.h"
#include "nsIXULRuntime.h"
#include "nsLayoutStatics.h"
#include "nsNetUtil.h"
#include "nsPIDOMWindow.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"
#include "nsXPCOM.h"
#include "nsXPCOMPrivate.h"
#include "xpcpublic.h"

#if defined(XP_MACOSX)
#  include "nsMacUtilsImpl.h"
#endif

#include "WorkerDebuggerManager.h"
#include "WorkerError.h"
#include "WorkerLoadInfo.h"
#include "WorkerRunnable.h"
#include "WorkerScope.h"
#include "WorkerThread.h"
#include "prsystem.h"

#ifdef DEBUG
#  include "nsICookieJarSettings.h"
#endif

#define WORKERS_SHUTDOWN_TOPIC "web-workers-shutdown"

static mozilla::LazyLogModule gWorkerShutdownDumpLog("WorkerShutdownDump");

#ifdef SHUTDOWN_LOG
#  undef SHUTDOWN_LOG
#endif
#define SHUTDOWN_LOG(msg) MOZ_LOG(gWorkerShutdownDumpLog, LogLevel::Debug, msg);

namespace mozilla {

using namespace ipc;

namespace dom {

using namespace workerinternals;

namespace workerinternals {

// The size of the worker runtime heaps in bytes. May be changed via pref.
#define WORKER_DEFAULT_RUNTIME_HEAPSIZE 32 * 1024 * 1024

// The size of the worker JS allocation threshold in MB. May be changed via
// pref.
#define WORKER_DEFAULT_ALLOCATION_THRESHOLD 30

// Half the size of the actual C stack, to be safe.
#define WORKER_CONTEXT_NATIVE_STACK_LIMIT 128 * sizeof(size_t) * 1024

// The maximum number of threads to use for workers, overridable via pref.
#define MAX_WORKERS_PER_DOMAIN 512

static_assert(MAX_WORKERS_PER_DOMAIN >= 1,
              "We should allow at least one worker per domain.");

#define PREF_WORKERS_PREFIX "dom.workers."
#define PREF_WORKERS_MAX_PER_DOMAIN PREF_WORKERS_PREFIX "maxPerDomain"

#define GC_REQUEST_OBSERVER_TOPIC "child-gc-request"
#define CC_REQUEST_OBSERVER_TOPIC "child-cc-request"
#define MEMORY_PRESSURE_OBSERVER_TOPIC "memory-pressure"
#define LOW_MEMORY_DATA "low-memory"
#define LOW_MEMORY_ONGOING_DATA "low-memory-ongoing"
#define MEMORY_PRESSURE_STOP_OBSERVER_TOPIC "memory-pressure-stop"

// Prefixes for observing preference changes.
#define PREF_JS_OPTIONS_PREFIX "javascript.options."
#define PREF_MEM_OPTIONS_PREFIX "mem."
#define PREF_GCZEAL_OPTIONS_PREFIX "mem.gc_zeal."
#define PREF_MODE "mode"
#define PREF_FREQUENCY "frequency"

static NS_DEFINE_CID(kStreamTransportServiceCID, NS_STREAMTRANSPORTSERVICE_CID);

namespace {

const uint32_t kNoIndex = uint32_t(-1);

uint32_t gMaxWorkersPerDomain = MAX_WORKERS_PER_DOMAIN;

// Does not hold an owning reference.
Atomic<RuntimeService*> gRuntimeService(nullptr);

// Retries queued-worker scheduling on the event target where the first queued
// worker is allowed to be scheduled.
class ScheduleQueuedWorkerRunnable final : public Runnable {
 public:
  explicit ScheduleQueuedWorkerRunnable(const nsACString& aDomain)
      : Runnable("workerinternals::ScheduleQueuedWorkerRunnable"),
        mDomain(aDomain) {}

  NS_IMETHOD Run() override {
    RuntimeService* runtime = RuntimeService::GetService();
    if (!runtime) {
      return NS_OK;
    }

    WorkerPrivate* parent = nullptr;
    if (!NS_IsMainThread()) {
      parent = GetCurrentThreadWorkerPrivate();
      if (!parent) {
        return NS_OK;
      }
      parent->AssertIsOnWorkerThread();
    }

    runtime->MaybeScheduleQueuedWorker(mDomain, parent);
    return NS_OK;
  }

 private:
  ~ScheduleQueuedWorkerRunnable() = default;

  const nsCString mDomain;
};

// Only true during the call to Init.
bool gRuntimeServiceDuringInit = false;

template <typename T>
struct PrefTraits;

template <>
struct PrefTraits<bool> {
  using PrefValueType = bool;

  static inline PrefValueType Get(const char* aPref) {
    AssertIsOnMainThread();
    return Preferences::GetBool(aPref);
  }

  static inline bool Exists(const char* aPref) {
    AssertIsOnMainThread();
    return Preferences::GetType(aPref) == nsIPrefBranch::PREF_BOOL;
  }
};

template <>
struct PrefTraits<int32_t> {
  using PrefValueType = int32_t;

  static inline PrefValueType Get(const char* aPref) {
    AssertIsOnMainThread();
    return Preferences::GetInt(aPref);
  }

  static inline bool Exists(const char* aPref) {
    AssertIsOnMainThread();
    return Preferences::GetType(aPref) == nsIPrefBranch::PREF_INT;
  }
};

template <typename T>
T GetPref(const char* aFullPref, const T aDefault, bool* aPresent = nullptr) {
  AssertIsOnMainThread();

  using PrefHelper = PrefTraits<T>;

  T result;
  bool present = true;

  if (PrefHelper::Exists(aFullPref)) {
    result = PrefHelper::Get(aFullPref);
  } else {
    result = aDefault;
    present = false;
  }

  if (aPresent) {
    *aPresent = present;
  }
  return result;
}

void LoadContextOptions(const char* aPrefName, void* /* aClosure */) {
  AssertIsOnMainThread();

  RuntimeService* rts = RuntimeService::GetService();
  if (!rts) {
    // May be shutting down, just bail.
    return;
  }

  const nsDependentCString prefName(aPrefName);

  // Several other pref branches will get included here so bail out if there is
  // another callback that will handle this change.
  if (StringBeginsWith(
          prefName,
          nsLiteralCString(PREF_JS_OPTIONS_PREFIX PREF_MEM_OPTIONS_PREFIX))) {
    return;
  }

  JS::ContextOptions contextOptions;
  xpc::SetPrefableContextOptions(contextOptions);

  nsCOMPtr<nsIXULRuntime> xr = do_GetService("@mozilla.org/xre/runtime;1");
  if (xr) {
    bool safeMode = false;
    xr->GetInSafeMode(&safeMode);
    if (safeMode) {
      contextOptions.disableOptionsForSafeMode();
    }
  }

  RuntimeService::SetDefaultContextOptions(contextOptions);

  if (rts) {
    rts->UpdateAllWorkerContextOptions();
  }
}

#ifdef JS_GC_ZEAL
void LoadGCZealOptions(const char* /* aPrefName */, void* /* aClosure */) {
  AssertIsOnMainThread();

  RuntimeService* rts = RuntimeService::GetService();
  if (!rts) {
    // May be shutting down, just bail.
    return;
  }

  int32_t mode = GetPref<int32_t>(
      PREF_JS_OPTIONS_PREFIX PREF_GCZEAL_OPTIONS_PREFIX PREF_MODE, -1);
  if (mode < 0) {
    mode = 0;
  }

  int32_t frequency = GetPref<int32_t>(
      PREF_JS_OPTIONS_PREFIX PREF_GCZEAL_OPTIONS_PREFIX PREF_FREQUENCY, -1);
  if (frequency < 0) {
    frequency = JS::BrowserDefaultGCZealFrequency;
  }

  RuntimeService::SetDefaultGCZeal(uint8_t(mode), uint32_t(frequency));

  if (rts) {
    rts->UpdateAllWorkerGCZeal();
  }
}
#endif

void UpdateCommonJSGCMemoryOption(RuntimeService* aRuntimeService,
                                  const char* aPrefName, JSGCParamKey aKey) {
  AssertIsOnMainThread();
  NS_ASSERTION(aPrefName, "Null pref name!");

  int32_t prefValue = GetPref(aPrefName, -1);
  Maybe<uint32_t> value = (prefValue < 0 || prefValue >= 10000)
                              ? Nothing()
                              : Some(uint32_t(prefValue));

  RuntimeService::SetDefaultJSGCSettings(aKey, value);

  if (aRuntimeService) {
    aRuntimeService->UpdateAllWorkerMemoryParameter(aKey, value);
  }
}

void UpdateOtherJSGCMemoryOption(RuntimeService* aRuntimeService,
                                 JSGCParamKey aKey, Maybe<uint32_t> aValue) {
  AssertIsOnMainThread();

  RuntimeService::SetDefaultJSGCSettings(aKey, aValue);

  if (aRuntimeService) {
    aRuntimeService->UpdateAllWorkerMemoryParameter(aKey, aValue);
  }
}

void LoadJSGCMemoryOptions(const char* aPrefName, void* /* aClosure */) {
  AssertIsOnMainThread();

  RuntimeService* rts = RuntimeService::GetService();

  if (!rts) {
    // May be shutting down, just bail.
    return;
  }

  constexpr auto memPrefix =
      nsLiteralCString{PREF_JS_OPTIONS_PREFIX PREF_MEM_OPTIONS_PREFIX};
  const nsDependentCString fullPrefName(aPrefName);

  // Pull out the string that actually distinguishes the parameter we need to
  // change.
  nsDependentCSubstring memPrefName;
  if (StringBeginsWith(fullPrefName, memPrefix)) {
    memPrefName.Rebind(fullPrefName, memPrefix.Length());
  } else {
    NS_ERROR("Unknown pref name!");
    return;
  }

  struct WorkerGCPref {
    nsLiteralCString memName;
    const char* fullName;
    JSGCParamKey key;
  };

#define PREF(suffix_, key_)                                          \
  {                                                                  \
    nsLiteralCString(suffix_),                                       \
        PREF_JS_OPTIONS_PREFIX PREF_MEM_OPTIONS_PREFIX suffix_, key_ \
  }
  constexpr WorkerGCPref kWorkerPrefs[] = {
      PREF("max", JSGC_MAX_BYTES),
      PREF("gc_high_frequency_time_limit_ms", JSGC_HIGH_FREQUENCY_TIME_LIMIT),
      PREF("gc_low_frequency_heap_growth", JSGC_LOW_FREQUENCY_HEAP_GROWTH),
      PREF("gc_high_frequency_large_heap_growth",
           JSGC_HIGH_FREQUENCY_LARGE_HEAP_GROWTH),
      PREF("gc_high_frequency_small_heap_growth",
           JSGC_HIGH_FREQUENCY_SMALL_HEAP_GROWTH),
      PREF("gc_small_heap_size_max_mb", JSGC_SMALL_HEAP_SIZE_MAX),
      PREF("gc_large_heap_size_min_mb", JSGC_LARGE_HEAP_SIZE_MIN),
      PREF("gc_balanced_heap_limits", JSGC_BALANCED_HEAP_LIMITS_ENABLED),
      PREF("gc_heap_growth_factor", JSGC_HEAP_GROWTH_FACTOR),
      PREF("gc_allocation_threshold_mb", JSGC_ALLOCATION_THRESHOLD),
      PREF("gc_malloc_threshold_base_mb", JSGC_MALLOC_THRESHOLD_BASE),
      PREF("gc_small_heap_incremental_limit",
           JSGC_SMALL_HEAP_INCREMENTAL_LIMIT),
      PREF("gc_large_heap_incremental_limit",
           JSGC_LARGE_HEAP_INCREMENTAL_LIMIT),
      PREF("gc_urgent_threshold_mb", JSGC_URGENT_THRESHOLD_MB),
      PREF("gc_incremental_slice_ms", JSGC_SLICE_TIME_BUDGET_MS),
      PREF("gc_min_empty_chunk_count", JSGC_MIN_EMPTY_CHUNK_COUNT),
      PREF("gc_compacting", JSGC_COMPACTING_ENABLED),
      PREF("gc_parallel_marking", JSGC_PARALLEL_MARKING_ENABLED),
      PREF("gc_parallel_marking_threshold_mb",
           JSGC_PARALLEL_MARKING_THRESHOLD_MB),
      PREF("gc_max_parallel_marking_threads", JSGC_MAX_MARKING_THREADS),
#ifdef JS_GC_CONCURRENT_MARKING
      PREF("gc_experimental_concurrent_marking",
           JSGC_CONCURRENT_MARKING_ENABLED),
#endif
#ifdef NIGHTLY_BUILD
      PREF("gc_experimental_semispace_nursery", JSGC_SEMISPACE_NURSERY_ENABLED),
#endif
      PREF("nursery_max_time_goal_ms", JSGC_NURSERY_MAX_TIME_GOAL_MS),
      // Note: Workers do not currently trigger eager minor GC, but if that is
      // desired the following parameters should be added:
      // javascript.options.mem.nursery_eager_collection_threshold_kb
      // javascript.options.mem.nursery_eager_collection_threshold_percent
      // javascript.options.mem.nursery_eager_collection_timeout_ms
  };
#undef PREF

  auto pref = kWorkerPrefs;
  auto end = kWorkerPrefs + std::size(kWorkerPrefs);

  if (gRuntimeServiceDuringInit) {
    // During init, we want to update every pref in kWorkerPrefs.
    MOZ_ASSERT(memPrefName.IsEmpty(),
               "Pref branch prefix only expected during init");
  } else {
    // Otherwise, find the single pref that changed.
    while (pref != end) {
      if (pref->memName == memPrefName) {
        end = pref + 1;
        break;
      }
      ++pref;
    }
#ifdef DEBUG
    if (pref == end) {
      nsAutoCString message("Workers don't support the '");
      message.Append(memPrefName);
      message.AppendLiteral("' preference!");
      NS_WARNING(message.get());
    }
#endif
  }

  while (pref != end) {
    switch (pref->key) {
      case JSGC_MAX_BYTES: {
        int32_t prefValue = GetPref(pref->fullName, -1);
        Maybe<uint32_t> value = (prefValue <= 0 || prefValue >= 0x1000)
                                    ? Nothing()
                                    : Some(uint32_t(prefValue) * 1024 * 1024);
        UpdateOtherJSGCMemoryOption(rts, pref->key, value);
        break;
      }
      case JSGC_SLICE_TIME_BUDGET_MS: {
        int32_t prefValue = GetPref(pref->fullName, -1);
        Maybe<uint32_t> value = (prefValue <= 0 || prefValue >= 100000)
                                    ? Nothing()
                                    : Some(uint32_t(prefValue));
        UpdateOtherJSGCMemoryOption(rts, pref->key, value);
        break;
      }
      case JSGC_COMPACTING_ENABLED:
      case JSGC_PARALLEL_MARKING_ENABLED:
#ifdef JS_GC_CONCURRENT_MARKING
      case JSGC_CONCURRENT_MARKING_ENABLED:
#endif
#ifdef NIGHTLY_BUILD
      case JSGC_SEMISPACE_NURSERY_ENABLED:
#endif
      case JSGC_BALANCED_HEAP_LIMITS_ENABLED: {
        bool present;
        bool prefValue = GetPref(pref->fullName, false, &present);
        Maybe<uint32_t> value = present ? Some(prefValue ? 1 : 0) : Nothing();
        UpdateOtherJSGCMemoryOption(rts, pref->key, value);
        break;
      }
      case JSGC_HIGH_FREQUENCY_TIME_LIMIT:
      case JSGC_LOW_FREQUENCY_HEAP_GROWTH:
      case JSGC_HIGH_FREQUENCY_LARGE_HEAP_GROWTH:
      case JSGC_HIGH_FREQUENCY_SMALL_HEAP_GROWTH:
      case JSGC_SMALL_HEAP_SIZE_MAX:
      case JSGC_LARGE_HEAP_SIZE_MIN:
      case JSGC_ALLOCATION_THRESHOLD:
      case JSGC_MALLOC_THRESHOLD_BASE:
      case JSGC_SMALL_HEAP_INCREMENTAL_LIMIT:
      case JSGC_LARGE_HEAP_INCREMENTAL_LIMIT:
      case JSGC_URGENT_THRESHOLD_MB:
      case JSGC_MIN_EMPTY_CHUNK_COUNT:
      case JSGC_HEAP_GROWTH_FACTOR:
      case JSGC_PARALLEL_MARKING_THRESHOLD_MB:
      case JSGC_MAX_MARKING_THREADS:
      case JSGC_NURSERY_MAX_TIME_GOAL_MS:
        UpdateCommonJSGCMemoryOption(rts, pref->fullName, pref->key);
        break;
      default:
        MOZ_ASSERT_UNREACHABLE("Unknown JSGCParamKey value");
        break;
    }
    ++pref;
  }
}

MOZ_CAN_RUN_SCRIPT bool InterruptCallback(JSContext* aCx) {
  WorkerPrivate* worker = GetWorkerPrivateFromContext(aCx);
  MOZ_ASSERT(worker);

  // Now is a good time to turn on profiling if it's pending.
  PROFILER_JS_INTERRUPT_CALLBACK();

  return MOZ_KnownLive(worker)->InterruptCallback(aCx);
}

class LogViolationDetailsRunnable final : public WorkerMainThreadRunnable {
  uint16_t mViolationType;
  nsCString mFileName;
  uint32_t mLineNum;
  uint32_t mColumnNum;
  nsString mScriptSample;

 public:
  LogViolationDetailsRunnable(WorkerPrivate* aWorker, uint16_t aViolationType,
                              const nsCString& aFileName, uint32_t aLineNum,
                              uint32_t aColumnNum,
                              const nsAString& aScriptSample)
      : WorkerMainThreadRunnable(aWorker,
                                 "RuntimeService :: LogViolationDetails"_ns),
        mViolationType(aViolationType),
        mFileName(aFileName),
        mLineNum(aLineNum),
        mColumnNum(aColumnNum),
        mScriptSample(aScriptSample) {
    MOZ_ASSERT(aWorker);
  }

  virtual bool MainThreadRun() override;

 private:
  ~LogViolationDetailsRunnable() = default;
};

MOZ_CAN_RUN_SCRIPT_FOR_DEFINITION bool ContentSecurityPolicyAllows(
    JSContext* aCx, JS::RuntimeCode aKind, JS::Handle<JSString*> aCodeString,
    JS::CompilationType aCompilationType,
    JS::Handle<JS::StackGCVector<JSString*>> aParameterStrings,
    JS::Handle<JSString*> aBodyString,
    JS::Handle<JS::StackGCVector<JS::Value>> aParameterArgs,
    JS::Handle<JS::Value> aBodyArg, bool* aOutCanCompileStrings) {
  WorkerPrivate* worker = GetWorkerPrivateFromContext(aCx);
  worker->AssertIsOnWorkerThread();

  // Allow eval by default without a CSP.
  bool evalOK = true;
  bool reportViolation = false;
  uint16_t violationType;
  nsAutoJSString scriptSample;
  if (aKind == JS::RuntimeCode::JS) {
    ErrorResult error;
    // FIXME(Bug 1990732): Need to pass a principal here to skip TT enforcement
    // when this code is run from a WebExtension content script.
    bool areArgumentsTrusted = TrustedTypeUtils::
        AreArgumentsTrustedForEnsureCSPDoesNotBlockStringCompilation(
            aCx, aCodeString, aCompilationType, aParameterStrings, aBodyString,
            aParameterArgs, aBodyArg, nullptr, error);
    if (error.MaybeSetPendingException(aCx)) {
      return false;
    }
    if (!areArgumentsTrusted) {
      *aOutCanCompileStrings = false;
      return true;
    }

    if (NS_WARN_IF(!scriptSample.init(aCx, aCodeString))) {
      return false;
    }

    if (!nsContentSecurityUtils::IsEvalAllowed(
            aCx, worker->UsesSystemPrincipal(), scriptSample)) {
      *aOutCanCompileStrings = false;
      return true;
    }

    if (OffThreadCSPContext* ctx = worker->GetCSPContext()) {
      evalOK = ctx->IsEvalAllowed(reportViolation);
    }
    violationType = nsIContentSecurityPolicy::VIOLATION_TYPE_EVAL;
  } else {
    if (OffThreadCSPContext* ctx = worker->GetCSPContext()) {
      evalOK = ctx->IsWasmEvalAllowed(reportViolation);
    }

    // As for nsScriptSecurityManager::ContentSecurityPolicyPermitsJSAction,
    // for MV2 extensions we have to allow wasm by default and report violations
    // for historical reasons.
    // TODO bug 1770909: remove this exception.
    auto* principal = BasePrincipal::Cast(worker->GetPrincipal());
    RefPtr<extensions::WebExtensionPolicyCore> policy =
        principal ? principal->AddonPolicyCore() : nullptr;
    if (!evalOK && policy && policy->ManifestVersion() == 2) {
      evalOK = true;
      reportViolation = true;
    }

    violationType = nsIContentSecurityPolicy::VIOLATION_TYPE_WASM_EVAL;
  }

  if (reportViolation) {
    auto caller = JSCallingLocation::Get(aCx);
    RefPtr<LogViolationDetailsRunnable> runnable =
        new LogViolationDetailsRunnable(worker, violationType,
                                        caller.FileName(), caller.mLine,
                                        caller.mColumn, scriptSample);

    ErrorResult rv;
    runnable->Dispatch(worker, Killing, rv);
    if (NS_WARN_IF(rv.Failed())) {
      rv.SuppressException();
    }
  }

  *aOutCanCompileStrings = evalOK;
  return true;
}

void CTypesActivityCallback(JSContext* aCx, JS::CTypesActivityType aType) {
  WorkerPrivate* worker = GetWorkerPrivateFromContext(aCx);
  worker->AssertIsOnWorkerThread();

  switch (aType) {
    case JS::CTypesActivityType::BeginCall:
      worker->BeginCTypesCall();
      break;

    case JS::CTypesActivityType::EndCall:
      worker->EndCTypesCall();
      break;

    case JS::CTypesActivityType::BeginCallback:
      worker->BeginCTypesCallback();
      break;

    case JS::CTypesActivityType::EndCallback:
      worker->EndCTypesCallback();
      break;

    default:
      MOZ_CRASH("Unknown type flag!");
  }
}

// JSDispatchableRunnables are WorkerRunnables used to dispatch JS::Dispatchable
// back to their worker thread. A WorkerRunnable is used for two reasons:
//
// 1. The JS::Dispatchable::run() callback may run JS so we cannot use a control
// runnable since they use async interrupts and break JS run-to-completion.
//
// 2. The DispatchToEventLoopCallback interface is *required* to fail during
// shutdown (see jsapi.h) which is exactly what WorkerRunnable::Dispatch() will
// do. Moreover, JS_DestroyContext() does *not* block on JS::Dispatchable::run
// being called, DispatchToEventLoopCallback failure is expected to happen
// during shutdown.
class JSDispatchableRunnable final : public WorkerThreadRunnable {
  js::UniquePtr<JS::Dispatchable> mDispatchable;

  ~JSDispatchableRunnable() { MOZ_ASSERT(!mDispatchable); }

  // Disable the usual pre/post-dispatch thread assertions since we are
  // dispatching from some random JS engine internal thread:

  bool PreDispatch(WorkerPrivate* aWorkerPrivate) override { return true; }

  void PostDispatch(WorkerPrivate* aWorkerPrivate,
                    bool aDispatchResult) override {
    if (!aDispatchResult) {
      // It is possible (for example in WASM failed compilation) that a
      // worker will not run, and in this case we need to
      // release the task as a failed task for deletion by the JS runtime.
      JS::Dispatchable::ReleaseFailedTask(std::move(mDispatchable));
    }
  }

 public:
  JSDispatchableRunnable(WorkerPrivate* aWorkerPrivate,
                         js::UniquePtr<JS::Dispatchable>&& aDispatchable)
      : WorkerThreadRunnable("JSDispatchableRunnable"),
        mDispatchable(std::move(aDispatchable)) {
    MOZ_ASSERT(mDispatchable);
  }

  bool WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    MOZ_ASSERT(aCx == aWorkerPrivate->GetJSContext());
    MOZ_ASSERT(mDispatchable);

    AutoJSAPI jsapi;
    jsapi.Init();

    JS::Dispatchable::Run(aWorkerPrivate->GetJSContext(),
                          std::move(mDispatchable),
                          JS::Dispatchable::NotShuttingDown);
    // mDispatchable is no longer valid after this point.
    // The delete has been handled on the JS engine side

    return true;
  }

  nsresult Cancel() override {
    MOZ_ASSERT(mDispatchable);

    AutoJSAPI jsapi;
    jsapi.Init();

    // TODO: Make this make more sense
    // Why are we calling Run here? Because the way the API was designed
    // is so that once control is passed to the runnable, then both cancellation
    // and running are handled through `Run` by either passing NotShuttingDown
    // or ShuttingDown (for cancellation).
    JS::Dispatchable::Run(GetCurrentThreadWorkerPrivate()->GetJSContext(),
                          std::move(mDispatchable),
                          JS::Dispatchable::ShuttingDown);
    // mDispatchable is no longer valid after this point.
    // The delete has been handled on the JS engine side

    return NS_OK;
  }
};

static bool DispatchToEventLoop(
    void* aClosure, js::UniquePtr<JS::Dispatchable>&& aDispatchable) {
  // This callback may execute either on the worker thread or a random
  // JS-internal helper thread.

  // See comment at JS::InitDispatchToEventLoop() below for how we know the
  // WorkerPrivate is alive.
  WorkerPrivate* workerPrivate = reinterpret_cast<WorkerPrivate*>(aClosure);

  // Dispatch is expected to fail during shutdown for the reasons outlined in
  // the JSDispatchableRunnable comment above.
  RefPtr<JSDispatchableRunnable> r =
      new JSDispatchableRunnable(workerPrivate, std::move(aDispatchable));
  return r->Dispatch(workerPrivate);
}

static bool DelayedDispatchToEventLoop(
    void* aClosure, js::UniquePtr<JS::Dispatchable>&& aDispatchable,
    uint32_t delay) {
  // See comment at JS::InitDispatchsToEventLoop() below for how we know the
  // WorkerPrivate is alive.
  WorkerPrivate* workerPrivate = reinterpret_cast<WorkerPrivate*>(aClosure);

  workerPrivate->AssertIsOnWorkerThread();

  JSContext* cx = workerPrivate->GetJSContext();
  TimeoutHandler* handler =
      new DelayedJSDispatchableHandler(cx, std::move(aDispatchable));
  workerPrivate->SetTimeout(cx, handler, delay, /* aIsInterval */ false,
                            Timeout::Reason::eJSTimeout, IgnoreErrors());

  return true;
}

static void AsyncTaskStarted(void* aClosure, JS::Dispatchable* aDispatchable) {
  // See comment at JS::InitDispatchsToEventLoop() below for how we know the
  // WorkerPrivate is alive.
  WorkerPrivate* workerPrivate = reinterpret_cast<WorkerPrivate*>(aClosure);
  workerPrivate->AssertIsOnWorkerThread();
  workerPrivate->JSAsyncTaskStarted(aDispatchable);
}

static void AsyncTaskFinished(void* aClosure, JS::Dispatchable* aDispatchable) {
  // See comment at JS::InitDispatchsToEventLoop() below for how we know the
  // WorkerPrivate is alive.
  WorkerPrivate* workerPrivate = reinterpret_cast<WorkerPrivate*>(aClosure);
  workerPrivate->AssertIsOnWorkerThread();
  workerPrivate->JSAsyncTaskFinished(aDispatchable);
}

static bool ConsumeStream(JSContext* aCx, JS::Handle<JSObject*> aObj,
                          JS::MimeType aMimeType,
                          JS::StreamConsumer* aConsumer) {
  WorkerPrivate* worker = GetWorkerPrivateFromContext(aCx);
  if (!worker) {
    JS_ReportErrorNumberASCII(aCx, js::GetErrorMessage, nullptr,
                              JSMSG_WASM_ERROR_CONSUMING_RESPONSE);
    return false;
  }

  return FetchUtil::StreamResponseToJS(aCx, aObj, aMimeType, aConsumer, worker);
}

bool InitJSContextForWorker(WorkerPrivate* aWorkerPrivate,
                            JSContext* aWorkerCx) {
  aWorkerPrivate->AssertIsOnWorkerThread();
  NS_ASSERTION(!aWorkerPrivate->GetJSContext(), "Already has a context!");

  JSSettings settings;
  aWorkerPrivate->CopyJSSettings(settings);

  JS::ContextOptionsRef(aWorkerCx) = settings.contextOptions;

  // This is the real place where we set the max memory for the runtime.
  for (const auto& setting : settings.gcSettings) {
    if (setting.value) {
      JS_SetGCParameter(aWorkerCx, setting.key, *setting.value);
    } else {
      JS_ResetGCParameter(aWorkerCx, setting.key);
    }
  }

  JS_SetNativeStackQuota(aWorkerCx, WORKER_CONTEXT_NATIVE_STACK_LIMIT);

  // Security policy:
  static const JSSecurityCallbacks securityCallbacks = {
      ContentSecurityPolicyAllows, TrustedTypeUtils::HostGetCodeForEval};
  JS_SetSecurityCallbacks(aWorkerCx, &securityCallbacks);

  // A WorkerPrivate lives strictly longer than its JSRuntime so we can safely
  // store a raw pointer as the callback's closure argument on the JSRuntime.
  JS::InitAsyncTaskCallbacks(aWorkerCx, DispatchToEventLoop,
                             DelayedDispatchToEventLoop, AsyncTaskStarted,
                             AsyncTaskFinished, (void*)aWorkerPrivate);

  JS::InitConsumeStreamCallback(aWorkerCx, ConsumeStream,
                                FetchUtil::ReportJSStreamError);

  // When available, set the self-hosted shared memory to be read, so that we
  // can decode the self-hosted content instead of parsing it.
  auto& shm = xpc::SelfHostedShmem::GetSingleton();
  JS::SelfHostedCache selfHostedContent = shm.Content();

  if (!JS::InitSelfHostedCode(aWorkerCx, selfHostedContent)) {
    NS_WARNING("Could not init self-hosted code!");
    return false;
  }

  JS_AddInterruptCallback(aWorkerCx, InterruptCallback);

  JS::SetCTypesActivityCallback(aWorkerCx, CTypesActivityCallback);

#ifdef JS_GC_ZEAL
  JS::SetGCZeal(aWorkerCx, settings.gcZeal, settings.gcZealFrequency);
#endif

  return true;
}

static void PreserveWrapper(JSContext* cx, JS::Handle<JSObject*> obj) {
  MOZ_ASSERT(cx);
  MOZ_ASSERT(obj);
  MOZ_ASSERT(mozilla::dom::IsDOMObject(obj));

  mozilla::dom::TryPreserveWrapper(obj);
}

static bool IsWorkerDebuggerGlobalOrSandbox(JS::Handle<JSObject*> aGlobal) {
  return IsWorkerDebuggerGlobal(aGlobal) || IsWorkerDebuggerSandbox(aGlobal);
}

JSObject* Wrap(JSContext* cx, JS::Handle<JSObject*> existing,
               JS::Handle<JSObject*> obj) {
  JS::Rooted<JSObject*> targetGlobal(cx, JS::CurrentGlobalOrNull(cx));

  // Note: the JS engine unwraps CCWs before calling this callback.
  JS::Rooted<JSObject*> originGlobal(cx, JS::GetNonCCWObjectGlobal(obj));

  const js::Wrapper* wrapper = nullptr;
  if (IsWorkerDebuggerGlobalOrSandbox(targetGlobal) &&
      IsWorkerDebuggerGlobalOrSandbox(originGlobal)) {
    wrapper = &js::CrossCompartmentWrapper::singleton;
  } else {
    wrapper = &js::OpaqueCrossCompartmentWrapper::singleton;
  }

  if (existing) {
    js::Wrapper::Renew(existing, obj, wrapper);
  }
  return js::Wrapper::New(cx, obj, wrapper);
}

static const JSWrapObjectCallbacks WrapObjectCallbacks = {
    Wrap,
    nullptr,
};

class WorkerJSRuntime final : public mozilla::CycleCollectedJSRuntime {
 public:
  // The heap size passed here doesn't matter, we will change it later in the
  // call to JS_SetGCParameter inside InitJSContextForWorker.
  explicit WorkerJSRuntime(JSContext* aCx, WorkerPrivate* aWorkerPrivate)
      : CycleCollectedJSRuntime(aCx), mWorkerPrivate(aWorkerPrivate) {
    MOZ_COUNT_CTOR_INHERITED(WorkerJSRuntime, CycleCollectedJSRuntime);
    MOZ_ASSERT(aWorkerPrivate);

    {
      JS::UniqueChars defaultLocale = aWorkerPrivate->AdoptDefaultLocale();
      MOZ_ASSERT(defaultLocale,
                 "failure of a WorkerPrivate to have a default locale should "
                 "have made the worker fail to spawn");

      if (!JS_SetDefaultLocale(Runtime(), defaultLocale.get())) {
        NS_WARNING("failed to set workerCx's default locale");
      }
    }
  }

  void Shutdown(JSContext* cx) override {
    // The CC is shut down, and the superclass destructor will GC, so make sure
    // we don't try to CC again.
    mWorkerPrivate = nullptr;

    CycleCollectedJSRuntime::Shutdown(cx);
  }

  ~WorkerJSRuntime() {
    MOZ_COUNT_DTOR_INHERITED(WorkerJSRuntime, CycleCollectedJSRuntime);
  }

  virtual void PrepareForForgetSkippable() override {}

  virtual void BeginCycleCollectionCallback(
      mozilla::CCReason aReason) override {}

  virtual void EndCycleCollectionCallback(
      CycleCollectorResults& aResults) override {}

  void DispatchDeferredDeletion(bool aContinuation, bool aPurge) override {
    MOZ_ASSERT(!aContinuation);

    // Do it immediately, no need for asynchronous behavior here.
    nsCycleCollector_doDeferredDeletion();
  }

  virtual void CustomGCCallback(JSGCStatus aStatus) override {
    if (!mWorkerPrivate) {
      // We're shutting down, no need to do anything.
      return;
    }

    mWorkerPrivate->AssertIsOnWorkerThread();

    if (aStatus == JSGC_END) {
      bool collectedAnything =
          nsCycleCollector_collect(CCReason::GC_FINISHED, nullptr);
      mWorkerPrivate->SetCCCollectedAnything(collectedAnything);
    }
  }

  void TraceAdditionalNativeBlackRoots(JSTracer* aTracer) override {
    if (!mWorkerPrivate || !mWorkerPrivate->MayContinueRunning()) {
      return;
    }

    if (WorkerGlobalScope* scope = mWorkerPrivate->GlobalScope()) {
      if (EventListenerManager* elm = scope->GetExistingListenerManager()) {
        elm->TraceListeners(aTracer);
      }
    }

    if (WorkerDebuggerGlobalScope* debuggerScope =
            mWorkerPrivate->DebuggerGlobalScope()) {
      if (EventListenerManager* elm =
              debuggerScope->GetExistingListenerManager()) {
        elm->TraceListeners(aTracer);
      }
    }
  };

 private:
  WorkerPrivate* mWorkerPrivate;
};

}  // anonymous namespace

}  // namespace workerinternals

class WorkerJSContext final : public mozilla::CycleCollectedJSContext {
 public:
  // The heap size passed here doesn't matter, we will change it later in the
  // call to JS_SetGCParameter inside InitJSContextForWorker.
  explicit WorkerJSContext(WorkerPrivate* aWorkerPrivate)
      : mWorkerPrivate(aWorkerPrivate) {
    MOZ_COUNT_CTOR_INHERITED(WorkerJSContext, CycleCollectedJSContext);
    MOZ_ASSERT(aWorkerPrivate);
    // Magical number 2. Workers have the base recursion depth 1, and normal
    // runnables run at level 2, and we don't want to process microtasks
    // at any other level.
    SetTargetedMicroTaskRecursionDepth(2);
  }

  // MOZ_CAN_RUN_SCRIPT_BOUNDARY because otherwise we have to annotate the
  // SpiderMonkey JS::JobQueue's destructor as MOZ_CAN_RUN_SCRIPT, which is a
  // bit of a pain.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY ~WorkerJSContext() {
    MOZ_COUNT_DTOR_INHERITED(WorkerJSContext, CycleCollectedJSContext);
    JSContext* cx = MaybeContext();
    if (!cx) {
      return;  // Initialize() must have failed
    }

    // We expect to come here with the cycle collector already shut down.
    // The superclass destructor will run the GC one final time and finalize any
    // JSObjects that were participating in cycles that were broken during CC
    // shutdown.
    // Make sure we don't try to CC again.
    mWorkerPrivate = nullptr;
  }

  WorkerJSContext* GetAsWorkerJSContext() override { return this; }

  CycleCollectedJSRuntime* CreateRuntime(JSContext* aCx) override {
    return new WorkerJSRuntime(aCx, mWorkerPrivate);
  }

  nsresult Initialize(JSRuntime* aParentRuntime) {
    nsresult rv = CycleCollectedJSContext::Initialize(
        aParentRuntime, WORKER_DEFAULT_RUNTIME_HEAPSIZE);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    JSContext* cx = Context();

    js::SetPreserveWrapperCallbacks(cx, PreserveWrapper, HasReleasedWrapper);
    JS_InitDestroyPrincipalsCallback(cx, nsJSPrincipals::Destroy);
    JS_InitReadPrincipalsCallback(cx, nsJSPrincipals::ReadPrincipals);
    JS_SetWrapObjectCallbacks(cx, &WrapObjectCallbacks);
    if (mWorkerPrivate->IsDedicatedWorker()) {
      JS_SetFutexCanWait(cx);
    }

    return NS_OK;
  }

  virtual bool useDebugQueue(JS::Handle<JSObject*> global) const override {
    MOZ_ASSERT(!NS_IsMainThread());

    return !IsWorkerGlobal(global);
  }

  virtual void DispatchToMicroTask(
      already_AddRefed<MicroTaskRunnable> aRunnable) override {
    RefPtr<MicroTaskRunnable> runnable(aRunnable);

    MOZ_ASSERT(!NS_IsMainThread());
    MOZ_ASSERT(runnable);

    JSContext* cx = Context();
    NS_ASSERTION(cx, "This should never be null!");

    JS::Rooted<JSObject*> global(cx, JS::CurrentGlobalOrNull(cx));
    NS_ASSERTION(global, "This should never be null!");

    JS::JobQueueMayNotBeEmpty(cx);
    PROFILER_MARKER_FLOW_ONLY("WorkerJSContext::DispatchToMicroTask", OTHER, {},
                              FlowMarker, Flow::FromPointer(runnable.get()));

    // On worker threads, if the current global is the worker global, we use
    // the main micro task queue. Otherwise, the current global must be either
    // the debugger global or a debugger sandbox, and we use the debugger micro
    // task queue instead.
    if (IsWorkerGlobal(global)) {
      if (!EnqueueMicroTask(cx, runnable.forget())) {
        // This should never fail, but if it does, we have no choice but to
        // crash. This is always an OOM.
        NS_ABORT_OOM(0);
      }
    } else {
      MOZ_ASSERT(IsWorkerDebuggerGlobal(global) ||
                 IsWorkerDebuggerSandbox(global));
      if (!EnqueueDebugMicroTask(cx, runnable.forget())) {
        // This should never fail, but if it does, we have no choice but to
        // crash. This is always an OOM.
        NS_ABORT_OOM(0);
      }
    }
  }

  bool IsSystemCaller() const override {
    return mWorkerPrivate->UsesSystemPrincipal();
  }

  void ReportError(JSErrorReport* aReport,
                   JS::ConstUTF8CharsZ aToStringResult) override {
    mWorkerPrivate->ReportError(Context(), aToStringResult, aReport);
  }

  WorkerPrivate* GetWorkerPrivate() const { return mWorkerPrivate; }

 private:
  WorkerPrivate* mWorkerPrivate;
};

namespace workerinternals {

namespace {

class WorkerThreadPrimaryRunnable final : public Runnable {
  WorkerPrivate* mWorkerPrivate;
  SafeRefPtr<WorkerThread> mThread;
  JSRuntime* mParentRuntime;

  class FinishedRunnable final : public Runnable {
    SafeRefPtr<WorkerThread> mThread;

   public:
    explicit FinishedRunnable(SafeRefPtr<WorkerThread> aThread)
        : Runnable("WorkerThreadPrimaryRunnable::FinishedRunnable"),
          mThread(std::move(aThread)) {
      MOZ_ASSERT(mThread);
    }

    NS_INLINE_DECL_REFCOUNTING_INHERITED(FinishedRunnable, Runnable)

   private:
    ~FinishedRunnable() = default;

    NS_DECL_NSIRUNNABLE
  };

 public:
  WorkerThreadPrimaryRunnable(WorkerPrivate* aWorkerPrivate,
                              SafeRefPtr<WorkerThread> aThread,
                              JSRuntime* aParentRuntime)
      : mozilla::Runnable("WorkerThreadPrimaryRunnable"),
        mWorkerPrivate(aWorkerPrivate),
        mThread(std::move(aThread)),
        mParentRuntime(aParentRuntime) {
    MOZ_ASSERT(aWorkerPrivate);
    MOZ_ASSERT(mThread);
  }

  NS_INLINE_DECL_REFCOUNTING_INHERITED(WorkerThreadPrimaryRunnable, Runnable)

 private:
  ~WorkerThreadPrimaryRunnable() = default;

  NS_DECL_NSIRUNNABLE
};

void PrefLanguagesChanged(const char* /* aPrefName */, void* /* aClosure */) {
  AssertIsOnMainThread();

  nsTArray<nsString> languages;
  Navigator::GetAcceptLanguages(languages, nullptr);

  RuntimeService* runtime = RuntimeService::GetService();
  if (runtime) {
    runtime->UpdateAllWorkerLanguages(languages);
  }
}

void AppVersionOverrideChanged(const char* /* aPrefName */,
                               void* /* aClosure */) {
  AssertIsOnMainThread();

  nsAutoString override;
  Preferences::GetString("general.appversion.override", override);

  RuntimeService* runtime = RuntimeService::GetService();
  if (runtime) {
    runtime->UpdateAppVersionOverridePreference(override);
  }
}

void PlatformOverrideChanged(const char* /* aPrefName */,
                             void* /* aClosure */) {
  AssertIsOnMainThread();

  nsAutoString override;
  Preferences::GetString("general.platform.override", override);

  RuntimeService* runtime = RuntimeService::GetService();
  if (runtime) {
    runtime->UpdatePlatformOverridePreference(override);
  }
}

} /* anonymous namespace */

// This is only touched on the main thread. Initialized in Init() below.
StaticAutoPtr<JSSettings> RuntimeService::sDefaultJSSettings;

RuntimeService::RuntimeService()
    : mMutex("RuntimeService::mMutex"),
      mObserved(false),
      mShuttingDown(false),
      mNavigatorPropertiesLoaded(false) {
  AssertIsOnMainThread();
  MOZ_ASSERT(!GetService(), "More than one service!");
}

RuntimeService::~RuntimeService() {
  AssertIsOnMainThread();

  // gRuntimeService can be null if Init() fails.
  MOZ_ASSERT(!GetService() || GetService() == this, "More than one service!");

  gRuntimeService = nullptr;
}

// static
RuntimeService* RuntimeService::GetOrCreateService() {
  AssertIsOnMainThread();

  if (!gRuntimeService) {
    // The observer service now owns us until shutdown.
    gRuntimeService = new RuntimeService();
    if (NS_FAILED((*gRuntimeService).Init())) {
      NS_WARNING("Failed to initialize!");
      (*gRuntimeService).Cleanup();
      gRuntimeService = nullptr;
      return nullptr;
    }
  }

  return gRuntimeService;
}

// static
RuntimeService* RuntimeService::GetService() { return gRuntimeService; }

bool RuntimeService::RegisterWorker(WorkerPrivate& aWorkerPrivate) {
  aWorkerPrivate.AssertIsOnParentThread();

  WorkerPrivate* parent = aWorkerPrivate.GetParent();
  if (!parent) {
    AssertIsOnMainThread();

    if (mShuttingDown) {
      return false;
    }
  }

  const bool isServiceWorker = aWorkerPrivate.IsServiceWorker();
  const bool isSharedWorker = aWorkerPrivate.IsSharedWorker();
  const bool isDedicatedWorker = aWorkerPrivate.IsDedicatedWorker();
  if (isServiceWorker) {
    AssertIsOnMainThread();
  }

  bool exemptFromPerDomainMax = false;
  if (isServiceWorker) {
    AssertIsOnMainThread();
    exemptFromPerDomainMax = Preferences::GetBool(
        "dom.serviceWorkers.exemptFromPerDomainMax", false);
  }

  const nsCString& domain = aWorkerPrivate.Domain();

  bool queued = false;
  {
    MutexAutoLock lock(mMutex);

    auto* const domainInfo =
        mDomainMap
            .LookupOrInsertWith(
                domain,
                [&domain, parent] {
                  NS_ASSERTION(!parent, "Shouldn't have a parent here!");
                  (void)parent;  // silence clang -Wunused-lambda-capture in
                                 // opt builds
                  auto wdi = std::make_unique<WorkerDomainInfo>();
                  wdi->mDomain = domain;
                  return wdi;
                })
            .get();

    queued = gMaxWorkersPerDomain && !domain.IsEmpty() &&
             !exemptFromPerDomainMax &&
             (domainInfo->ActiveWorkerCount() >= gMaxWorkersPerDomain ||
              !domainInfo->mQueuedWorkers.IsEmpty());

    if (queued) {
      domainInfo->mQueuedWorkers.AppendElement(&aWorkerPrivate);

      // Worker spawn gets queued due to hitting max workers per domain
      // limit so let's log a warning.
      WorkerPrivate::ReportErrorToConsole(nsIScriptError::warningFlag, "DOM"_ns,
                                          PropertiesFile::DOM_PROPERTIES,
                                          "HittingMaxWorkersPerDomain2"_ns);

      if (isServiceWorker) {
        glean::workers::service_worker_spawn_gets_queued.Add(1);
      } else if (isSharedWorker) {
        glean::workers::shared_worker_spawn_gets_queued.Add(1);
      } else if (isDedicatedWorker) {
        glean::workers::dedicated_worker_spawn_gets_queued.Add(1);
      }
    } else if (parent) {
      domainInfo->mChildWorkerCount++;
    } else if (isServiceWorker) {
      domainInfo->mActiveServiceWorkers.AppendElement(&aWorkerPrivate);
    } else {
      domainInfo->mActiveWorkers.AppendElement(&aWorkerPrivate);
    }
  }

  aWorkerPrivate.SetIsQueued(queued);

  // From here on out we must call UnregisterWorker if something fails!
  if (parent) {
    if (!parent->AddChildWorker(aWorkerPrivate)) {
      UnregisterWorker(aWorkerPrivate);
      return false;
    }
  } else {
    if (!mNavigatorPropertiesLoaded) {
      MutexAutoLock lock(mMutex);

      if (NS_FAILED(Navigator::GetAppVersion(
              mNavigatorProperties.mAppVersion, aWorkerPrivate.GetDocument(),
              false /* aUsePrefOverriddenValue */)) ||
          NS_FAILED(Navigator::GetPlatform(
              mNavigatorProperties.mPlatform, aWorkerPrivate.GetDocument(),
              false /* aUsePrefOverriddenValue */))) {
        UnregisterWorker(aWorkerPrivate);
        return false;
      }

      // The navigator overridden properties should have already been read.

      Navigator::GetAcceptLanguages(mNavigatorProperties.mLanguages, nullptr);
      mNavigatorPropertiesLoaded = true;
    }

    nsPIDOMWindowInner* window = aWorkerPrivate.GetWindow();

    if (!isServiceWorker) {
      // Service workers are excluded since their lifetime is separate from
      // that of dom windows.
      if (auto* const windowArray = mWindowMap.GetOrInsertNew(window, 1);
          !windowArray->Contains(&aWorkerPrivate)) {
        windowArray->AppendElement(&aWorkerPrivate);
      } else {
        MOZ_ASSERT(aWorkerPrivate.IsSharedWorker());
      }
    }
  }

  if (!queued && !ScheduleWorker(aWorkerPrivate)) {
    return false;
  }

  if (isServiceWorker) {
    AssertIsOnMainThread();
  }
  return true;
}

void RuntimeService::UnregisterWorker(WorkerPrivate& aWorkerPrivate) {
  aWorkerPrivate.AssertIsOnParentThread();

  WorkerPrivate* parent = aWorkerPrivate.GetParent();
  if (!parent) {
    AssertIsOnMainThread();
  }

  nsCString domain = aWorkerPrivate.Domain();

  bool shouldScheduleQueuedWorker = false;
  {
    MutexAutoLock lock(mMutex);

    WorkerDomainInfo* domainInfo;
    if (!mDomainMap.Get(domain, &domainInfo)) {
      NS_ERROR("Don't have an entry for this domain!");
    }

    // Remove old worker from everywhere.
    uint32_t index = domainInfo->mQueuedWorkers.IndexOf(&aWorkerPrivate);
    if (index != kNoIndex) {
      // Was queued, remove from the list.
      domainInfo->mQueuedWorkers.RemoveElementAt(index);
    } else if (parent) {
      MOZ_ASSERT(domainInfo->mChildWorkerCount, "Must be non-zero!");
      domainInfo->mChildWorkerCount--;
    } else if (aWorkerPrivate.IsServiceWorker()) {
      MOZ_ASSERT(domainInfo->mActiveServiceWorkers.Contains(&aWorkerPrivate),
                 "Don't know about this worker!");
      domainInfo->mActiveServiceWorkers.RemoveElement(&aWorkerPrivate);
    } else {
      MOZ_ASSERT(domainInfo->mActiveWorkers.Contains(&aWorkerPrivate),
                 "Don't know about this worker!");
      domainInfo->mActiveWorkers.RemoveElement(&aWorkerPrivate);
    }

    // See if there is a queued worker to schedule after we release mMutex.
    shouldScheduleQueuedWorker =
        domainInfo->ActiveWorkerCount() < gMaxWorkersPerDomain &&
        !domainInfo->mQueuedWorkers.IsEmpty();

    if (domainInfo->HasNoWorkers()) {
      MOZ_ASSERT(!shouldScheduleQueuedWorker);
      mDomainMap.Remove(domain);
    }
  }

  // NB: For Shared Workers we used to call ShutdownOnMainThread on the
  // RemoteWorkerController; however, that was redundant because
  // RemoteWorkerChild uses a WeakWorkerRef which notifies at about the
  // same time as us calling into the code here and would race with us.

  if (parent) {
    parent->RemoveChildWorker(aWorkerPrivate);
  } else if (aWorkerPrivate.IsSharedWorker()) {
    AssertIsOnMainThread();

    mWindowMap.RemoveIf([&aWorkerPrivate](const auto& iter) {
      const auto& workers = iter.Data();
      MOZ_ASSERT(workers);

      if (workers->RemoveElement(&aWorkerPrivate)) {
        MOZ_ASSERT(!workers->Contains(&aWorkerPrivate),
                   "Added worker more than once!");

        return workers->IsEmpty();
      }

      return false;
    });
  } else if (aWorkerPrivate.IsDedicatedWorker()) {
    // May be null.
    nsPIDOMWindowInner* window = aWorkerPrivate.GetWindow();
    if (auto entry = mWindowMap.Lookup(window)) {
      MOZ_ALWAYS_TRUE(entry.Data()->RemoveElement(&aWorkerPrivate));
      if (entry.Data()->IsEmpty()) {
        entry.Remove();
      }
    } else {
      MOZ_ASSERT_UNREACHABLE("window is not in mWindowMap");
    }
  }

  if (shouldScheduleQueuedWorker) {
    MaybeScheduleQueuedWorker(domain, parent);
  }
}

void RuntimeService::MaybeScheduleQueuedWorker(const nsACString& aDomain,
                                               WorkerPrivate* aParent) {
  MOZ_ASSERT_DEBUG_OR_FUZZING(aParent ? aParent->IsOnCurrentThread()
                                      : NS_IsMainThread());

  WorkerPrivate* queuedWorker = nullptr;
  nsCOMPtr<nsISerialEventTarget> schedulingTarget;

  {
    MutexAutoLock lock(mMutex);

    WorkerDomainInfo* domainInfo;
    if (!mDomainMap.Get(aDomain, &domainInfo)) {
      return;
    }

    const bool shouldScheduleQueuedWorker =
        domainInfo->ActiveWorkerCount() < gMaxWorkersPerDomain &&
        !domainInfo->mQueuedWorkers.IsEmpty();

    if (!shouldScheduleQueuedWorker) {
      return;
    }

    WorkerPrivate* firstQueuedWorker = domainInfo->mQueuedWorkers[0];

    // Preserve queue order. Schedule only if the first queued worker belongs
    // to the current thread; otherwise retry on its scheduling target.
    WorkerPrivate* queuedParent = firstQueuedWorker->GetParent();

    if (queuedParent == aParent) {
      queuedWorker = firstQueuedWorker;
      domainInfo->mQueuedWorkers.RemoveElementAt(0);

      if (aParent) {
        domainInfo->mChildWorkerCount++;
      } else if (queuedWorker->IsServiceWorker()) {
        domainInfo->mActiveServiceWorkers.AppendElement(queuedWorker);
      } else {
        domainInfo->mActiveWorkers.AppendElement(queuedWorker);
      }
      MOZ_ASSERT(domainInfo->ActiveWorkerCount() <= +gMaxWorkersPerDomain);
    } else {
      schedulingTarget = firstQueuedWorker->GetSchedulingEventTarget();
      MOZ_DIAGNOSTIC_ASSERT(schedulingTarget);
    }
  }

  MOZ_ASSERT_DEBUG_OR_FUZZING(queuedWorker || schedulingTarget);

  if (queuedWorker) {
    (void)ScheduleWorker(*queuedWorker);
  }

  if (schedulingTarget) {
    nsCOMPtr<nsIRunnable> runnable = new ScheduleQueuedWorkerRunnable(aDomain);
    nsresult rv =
        schedulingTarget->Dispatch(runnable.forget(), NS_DISPATCH_NORMAL);
    // Leave the worker queued if dispatch fails; normal cancellation or
    // unregistration removes it.
    (void)NS_WARN_IF(NS_FAILED(rv));
  }
}

bool RuntimeService::ScheduleWorker(WorkerPrivate& aWorkerPrivate) {
  if (!aWorkerPrivate.Start()) {
    // This is ok, means that we didn't need to make a thread for this worker.
    return true;
  }

  const WorkerThreadFriendKey friendKey;

  SafeRefPtr<WorkerThread> thread = WorkerThread::Create(friendKey);
  if (!thread) {
    UnregisterWorker(aWorkerPrivate);
    return false;
  }

  if (NS_FAILED(thread->SetPriority(nsISupportsPriority::PRIORITY_NORMAL))) {
    NS_WARNING("Could not set the thread's priority!");
  }

  aWorkerPrivate.SetThread(thread.unsafeGetRawPtr());
  JSContext* cx = CycleCollectedJSContext::Get()->Context();

  nsCOMPtr<nsIRunnable> runnable = new WorkerThreadPrimaryRunnable(
      &aWorkerPrivate, thread.clonePtr(), JS_GetParentRuntime(cx));
  if (NS_FAILED(
          thread->DispatchPrimaryRunnable(friendKey, runnable.forget()))) {
    UnregisterWorker(aWorkerPrivate);
    return false;
  }

  // The worker was queued when creating, so enable remote debugger now.
  if (aWorkerPrivate.IsQueued()) {
    aWorkerPrivate.SetIsQueued(false);
    aWorkerPrivate.EnableRemoteDebugger();
  }

  return true;
}

nsresult RuntimeService::Init() {
  AssertIsOnMainThread();

  nsLayoutStatics::AddRef();

  // Initialize JSSettings.
  sDefaultJSSettings = new JSSettings();
  SetDefaultJSGCSettings(JSGC_MAX_BYTES, Some(WORKER_DEFAULT_RUNTIME_HEAPSIZE));
  SetDefaultJSGCSettings(JSGC_ALLOCATION_THRESHOLD,
                         Some(WORKER_DEFAULT_ALLOCATION_THRESHOLD));

  // nsIStreamTransportService is thread-safe but it must be initialized on the
  // main-thread. FileReader needs it, so, let's initialize it now.
  nsresult rv;
  nsCOMPtr<nsIStreamTransportService> sts =
      do_GetService(kStreamTransportServiceCID, &rv);
  NS_ENSURE_TRUE(sts, NS_ERROR_FAILURE);

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  NS_ENSURE_TRUE(obs, NS_ERROR_FAILURE);

  rv = obs->AddObserver(this, NS_XPCOM_SHUTDOWN_THREADS_OBSERVER_ID, false);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = obs->AddObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID, false);
  NS_ENSURE_SUCCESS(rv, rv);

  mObserved = true;

  if (NS_FAILED(obs->AddObserver(this, GC_REQUEST_OBSERVER_TOPIC, false))) {
    NS_WARNING("Failed to register for GC request notifications!");
  }

  if (NS_FAILED(obs->AddObserver(this, CC_REQUEST_OBSERVER_TOPIC, false))) {
    NS_WARNING("Failed to register for CC request notifications!");
  }

  if (NS_FAILED(
          obs->AddObserver(this, MEMORY_PRESSURE_OBSERVER_TOPIC, false))) {
    NS_WARNING("Failed to register for memory pressure notifications!");
  }

  if (NS_FAILED(
          obs->AddObserver(this, NS_IOSERVICE_OFFLINE_STATUS_TOPIC, false))) {
    NS_WARNING("Failed to register for offline notification event!");
  }

  MOZ_ASSERT(!gRuntimeServiceDuringInit, "This should be false!");
  gRuntimeServiceDuringInit = true;

#define WORKER_PREF(name, callback) \
  NS_FAILED(Preferences::RegisterCallbackAndCall(callback, name))

  if (NS_FAILED(Preferences::RegisterPrefixCallbackAndCall(
          LoadJSGCMemoryOptions,
          PREF_JS_OPTIONS_PREFIX PREF_MEM_OPTIONS_PREFIX)) ||
#ifdef JS_GC_ZEAL
      NS_FAILED(Preferences::RegisterCallback(
          LoadGCZealOptions,
          PREF_JS_OPTIONS_PREFIX PREF_GCZEAL_OPTIONS_PREFIX)) ||
#endif
      WORKER_PREF("intl.accept_languages", PrefLanguagesChanged) ||
      WORKER_PREF("general.appversion.override", AppVersionOverrideChanged) ||
      WORKER_PREF("general.platform.override", PlatformOverrideChanged) ||
      NS_FAILED(Preferences::RegisterPrefixCallbackAndCall(
          LoadContextOptions, PREF_JS_OPTIONS_PREFIX))) {
    NS_WARNING("Failed to register pref callbacks!");
  }

#undef WORKER_PREF

  MOZ_ASSERT(gRuntimeServiceDuringInit, "Should be true!");
  gRuntimeServiceDuringInit = false;

  int32_t maxPerDomain =
      Preferences::GetInt(PREF_WORKERS_MAX_PER_DOMAIN, MAX_WORKERS_PER_DOMAIN);
  gMaxWorkersPerDomain = std::max(0, maxPerDomain);

  IndexedDatabaseManager* idm = IndexedDatabaseManager::GetOrCreate();
  if (NS_WARN_IF(!idm)) {
    return NS_ERROR_UNEXPECTED;
  }

  rv = idm->EnsureLocale();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // PerformanceService must be initialized on the main-thread.
  PerformanceService::GetOrCreate();

  return NS_OK;
}

void RuntimeService::Shutdown() {
  AssertIsOnMainThread();

  MOZ_ASSERT(!mShuttingDown);
  // That's it, no more workers.
  mShuttingDown = true;

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  NS_WARNING_ASSERTION(obs, "Failed to get observer service?!");

  // Tell anyone that cares that they're about to lose worker support.
  if (obs && NS_FAILED(obs->NotifyObservers(nullptr, WORKERS_SHUTDOWN_TOPIC,
                                            nullptr))) {
    NS_WARNING("NotifyObservers failed!");
  }

  {
    AutoTArray<WorkerPrivate*, 100> workers;

    {
      MutexAutoLock lock(mMutex);

      AddAllTopLevelWorkersToArray(workers);
    }

    // Cancel all top-level workers.
    for (const auto& worker : workers) {
      if (!worker->Cancel()) {
        NS_WARNING("Failed to cancel worker!");
      }
    }
  }

  sDefaultJSSettings = nullptr;
}

namespace {

class DumpCrashInfoRunnable final : public WorkerControlRunnable {
 public:
  explicit DumpCrashInfoRunnable(WorkerPrivate* aWorkerPrivate)
      : WorkerControlRunnable("DumpCrashInfoRunnable"),
        mMonitor("DumpCrashInfoRunnable::mMonitor"),
        mWorkerPrivate(aWorkerPrivate) {}

  bool WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    MonitorAutoLock lock(mMonitor);
    if (!mHasMsg) {
      aWorkerPrivate->DumpCrashInformation(mMsg);
      mHasMsg.Flip();
    }
    lock.Notify();
    return true;
  }

  nsresult Cancel() override {
    MonitorAutoLock lock(mMonitor);
    if (!mHasMsg) {
      mMsg.Assign("Canceled");
      mHasMsg.Flip();
    }
    lock.Notify();

    return NS_OK;
  }

  bool DispatchAndWait() {
    MonitorAutoLock lock(mMonitor);

    if (!Dispatch(mWorkerPrivate)) {
      // The worker is already dead but the main thread still didn't remove it
      // from RuntimeService's registry.
      return false;
    }

    // To avoid any possibility of process hangs we never receive reports on
    // we give the worker 1sec to react.
    lock.Wait(TimeDuration::FromMilliseconds(1000));
    if (!mHasMsg) {
      mMsg.Append("NoResponse");
      mHasMsg.Flip();
    }
    return true;
  }

  const nsCString& MsgData() const { return mMsg; }

 private:
  bool PreDispatch(WorkerPrivate* aWorkerPrivate) override { return true; }

  void PostDispatch(WorkerPrivate* aWorkerPrivate,
                    bool aDispatchResult) override {}

  Monitor mMonitor MOZ_UNANNOTATED;
  nsCString mMsg;
  FlippedOnce<false> mHasMsg;
  WorkerPrivate* mWorkerPrivate;
};

struct ActiveWorkerStats {
  template <uint32_t ActiveWorkerStats::* Category>
  void Update(const nsTArray<WorkerPrivate*>& aWorkers) {
    for (const auto worker : aWorkers) {
      RefPtr<DumpCrashInfoRunnable> runnable =
          new DumpCrashInfoRunnable(worker);
      if (runnable->DispatchAndWait()) {
        ++(this->*Category);
        mMessage.Append(runnable->MsgData());
      }
    }
  }

  uint32_t mWorkers = 0;
  uint32_t mServiceWorkers = 0;
  nsCString mMessage;
};

}  // namespace

Maybe<nsCString> RuntimeService::GetHangingWorkersInfo() {
  MutexAutoLock lock(mMutex);

  // If we never wanted to shut down we cannot hang.
  if (!mShuttingDown) {
    return Nothing();
  }

  ActiveWorkerStats activeStats;
  uint32_t inactiveWorkers = 0;

  for (const auto& aData : mDomainMap.Values()) {
    activeStats.Update<&ActiveWorkerStats::mWorkers>(aData->mActiveWorkers);
    activeStats.Update<&ActiveWorkerStats::mServiceWorkers>(
        aData->mActiveServiceWorkers);

    // These might not be top-level workers...
    inactiveWorkers += std::count_if(
        aData->mQueuedWorkers.begin(), aData->mQueuedWorkers.end(),
        [](const auto* const worker) { return !worker->GetParent(); });
  }

  if (activeStats.mWorkers + activeStats.mServiceWorkers + inactiveWorkers ==
      0) {
    return Nothing();
  }

  nsCString msg;

  // A: active Workers | S: active ServiceWorkers | Q: queued Workers
  msg.AppendPrintf("Workers Hanging - %d|A:%d|S:%d|Q:%d", mShuttingDown ? 1 : 0,
                   activeStats.mWorkers, activeStats.mServiceWorkers,
                   inactiveWorkers);
  msg.Append(activeStats.mMessage);

  return Some(std::move(msg));
}

// This spins the event loop until all workers are finished and their threads
// have been joined.
void RuntimeService::Cleanup() {
  AssertIsOnMainThread();

  if (mCleanedUp) {
    return;
  }
  mCleanedUp = true;

  if (!mShuttingDown) {
    Shutdown();
  }

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  NS_WARNING_ASSERTION(obs, "Failed to get observer service?!");

  {
    MutexAutoLock lock(mMutex);

    AutoTArray<WorkerPrivate*, 100> workers;
    AddAllTopLevelWorkersToArray(workers);

    if (!workers.IsEmpty()) {
      nsIThread* currentThread = NS_GetCurrentThread();
      NS_ASSERTION(currentThread, "This should never be null!");

      // If the loop below takes too long, we probably have a problematic
      // worker.  MOZ_LOG some info before the parent process forcibly
      // terminates us so that in the event we are a content process, the log
      // output can provide useful context about the workers that did not
      // cleanly shut down.
      nsCOMPtr<nsITimer> timer;
      RefPtr<RuntimeService> self = this;
      nsresult rv = NS_NewTimerWithCallback(
          getter_AddRefs(timer),
          [self](nsITimer*) { self->DumpRunningWorkers(); },
          TimeDuration::FromSeconds(1), nsITimer::TYPE_ONE_SHOT,
          "RuntimeService::WorkerShutdownDump"_ns);
      (void)NS_WARN_IF(NS_FAILED(rv));

      // And make sure all their final messages have run and all their threads
      // have joined.
      while (mDomainMap.Count()) {
        MutexAutoUnlock unlock(mMutex);

        if (!NS_ProcessNextEvent(currentThread)) {
          NS_WARNING("Something bad happened!");
          break;
        }
      }

      if (NS_SUCCEEDED(rv)) {
        timer->Cancel();
      }
    }
  }

  NS_ASSERTION(!mWindowMap.Count(), "All windows should have been released!");

#define WORKER_PREF(name, callback) \
  NS_FAILED(Preferences::UnregisterCallback(callback, name))

  if (mObserved) {
    if (NS_FAILED(Preferences::UnregisterPrefixCallback(
            LoadContextOptions, PREF_JS_OPTIONS_PREFIX)) ||
        WORKER_PREF("intl.accept_languages", PrefLanguagesChanged) ||
        WORKER_PREF("general.appversion.override", AppVersionOverrideChanged) ||
        WORKER_PREF("general.platform.override", PlatformOverrideChanged) ||
#ifdef JS_GC_ZEAL
        NS_FAILED(Preferences::UnregisterCallback(
            LoadGCZealOptions,
            PREF_JS_OPTIONS_PREFIX PREF_GCZEAL_OPTIONS_PREFIX)) ||
#endif
        NS_FAILED(Preferences::UnregisterPrefixCallback(
            LoadJSGCMemoryOptions,
            PREF_JS_OPTIONS_PREFIX PREF_MEM_OPTIONS_PREFIX))) {
      NS_WARNING("Failed to unregister pref callbacks!");
    }

#undef WORKER_PREF

    if (obs) {
      if (NS_FAILED(obs->RemoveObserver(this, GC_REQUEST_OBSERVER_TOPIC))) {
        NS_WARNING("Failed to unregister for GC request notifications!");
      }

      if (NS_FAILED(obs->RemoveObserver(this, CC_REQUEST_OBSERVER_TOPIC))) {
        NS_WARNING("Failed to unregister for CC request notifications!");
      }

      if (NS_FAILED(
              obs->RemoveObserver(this, MEMORY_PRESSURE_OBSERVER_TOPIC))) {
        NS_WARNING("Failed to unregister for memory pressure notifications!");
      }

      if (NS_FAILED(
              obs->RemoveObserver(this, NS_IOSERVICE_OFFLINE_STATUS_TOPIC))) {
        NS_WARNING("Failed to unregister for offline notification event!");
      }
      obs->RemoveObserver(this, NS_XPCOM_SHUTDOWN_THREADS_OBSERVER_ID);
      obs->RemoveObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID);
      mObserved = false;
    }

    nsLayoutStatics::Release();
  }
}

void RuntimeService::AddAllTopLevelWorkersToArray(
    nsTArray<WorkerPrivate*>& aWorkers) {
  for (const auto& aData : mDomainMap.Values()) {
#ifdef DEBUG
    for (const auto& activeWorker : aData->mActiveWorkers) {
      MOZ_ASSERT(!activeWorker->GetParent(),
                 "Shouldn't have a parent in this list!");
    }
    for (const auto& activeServiceWorker : aData->mActiveServiceWorkers) {
      MOZ_ASSERT(!activeServiceWorker->GetParent(),
                 "Shouldn't have a parent in this list!");
    }
#endif

    aWorkers.AppendElements(aData->mActiveWorkers);
    aWorkers.AppendElements(aData->mActiveServiceWorkers);

    // These might not be top-level workers...
    std::copy_if(aData->mQueuedWorkers.begin(), aData->mQueuedWorkers.end(),
                 MakeBackInserter(aWorkers),
                 [](const auto& worker) { return !worker->GetParent(); });
  }
}

nsTArray<WorkerPrivate*> RuntimeService::GetWorkersForWindow(
    const nsPIDOMWindowInner& aWindow) const {
  AssertIsOnMainThread();

  nsTArray<WorkerPrivate*> result;
  if (nsTArray<WorkerPrivate*>* const workers = mWindowMap.Get(&aWindow)) {
    NS_ASSERTION(!workers->IsEmpty(), "Should have been removed!");
    result.AppendElements(*workers);
  }
  return result;
}

void RuntimeService::CancelWorkersForWindow(const nsPIDOMWindowInner& aWindow) {
  AssertIsOnMainThread();

  for (WorkerPrivate* const worker : GetWorkersForWindow(aWindow)) {
    MOZ_ASSERT(!worker->IsSharedWorker());
    worker->Cancel();
  }
}

void RuntimeService::UpdateWorkersBackgroundState(
    const nsPIDOMWindowInner& aWindow, bool aIsBackground) {
  AssertIsOnMainThread();
  for (WorkerPrivate* const worker : GetWorkersForWindow(aWindow)) {
    MOZ_ASSERT(!worker->IsSharedWorker());
    if (aIsBackground) {
      worker->SetIsRunningInBackground();
    } else {
      worker->SetIsRunningInForeground();
    }
  }
}

void RuntimeService::FreezeWorkersForWindow(const nsPIDOMWindowInner& aWindow) {
  AssertIsOnMainThread();

  for (WorkerPrivate* const worker : GetWorkersForWindow(aWindow)) {
    MOZ_ASSERT(!worker->IsSharedWorker());
    worker->Freeze(&aWindow);
  }
}

void RuntimeService::ThawWorkersForWindow(const nsPIDOMWindowInner& aWindow) {
  AssertIsOnMainThread();

  for (WorkerPrivate* const worker : GetWorkersForWindow(aWindow)) {
    MOZ_ASSERT(!worker->IsSharedWorker());
    worker->Thaw(&aWindow);
  }
}

void RuntimeService::SuspendWorkersForWindow(
    const nsPIDOMWindowInner& aWindow) {
  AssertIsOnMainThread();

  for (WorkerPrivate* const worker : GetWorkersForWindow(aWindow)) {
    MOZ_ASSERT(!worker->IsSharedWorker());
    worker->ParentWindowPaused();
  }
}

void RuntimeService::ResumeWorkersForWindow(const nsPIDOMWindowInner& aWindow) {
  AssertIsOnMainThread();

  for (WorkerPrivate* const worker : GetWorkersForWindow(aWindow)) {
    MOZ_ASSERT(!worker->IsSharedWorker());
    worker->ParentWindowResumed();
  }
}

void RuntimeService::PropagateStorageAccessPermissionGranted(
    const nsPIDOMWindowInner& aWindow) {
  AssertIsOnMainThread();
  MOZ_ASSERT_IF(aWindow.GetExtantDoc(), aWindow.GetExtantDoc()
                                            ->CookieJarSettings()
                                            ->GetRejectThirdPartyContexts());

  for (WorkerPrivate* const worker : GetWorkersForWindow(aWindow)) {
    worker->PropagateStorageAccessPermissionGranted();
  }
}

void RuntimeService::UpdateTimezoneOverrideForWorkers(
    const nsPIDOMWindowInner& aWindow, const nsAString& aTimezone) {
  AssertIsOnMainThread();

  for (WorkerPrivate* const worker : GetWorkersForWindow(aWindow)) {
    worker->UpdateTimezoneOverride(aTimezone);
  }
}

template <typename Func>
void RuntimeService::BroadcastAllWorkers(const Func& aFunc) {
  AssertIsOnMainThread();

  AutoTArray<WorkerPrivate*, 100> workers;
  {
    MutexAutoLock lock(mMutex);

    AddAllTopLevelWorkersToArray(workers);
  }

  for (const auto& worker : workers) {
    aFunc(*worker);
  }
}

void RuntimeService::UpdateAllWorkerContextOptions() {
  BroadcastAllWorkers([](auto& worker) {
    worker.UpdateContextOptions(sDefaultJSSettings->contextOptions);
  });
}

void RuntimeService::UpdateAppVersionOverridePreference(
    const nsAString& aValue) {
  AssertIsOnMainThread();
  MutexAutoLock lock(mMutex);
  mNavigatorProperties.mAppVersionOverridden = aValue;
}

void RuntimeService::UpdatePlatformOverridePreference(const nsAString& aValue) {
  AssertIsOnMainThread();
  MutexAutoLock lock(mMutex);
  mNavigatorProperties.mPlatformOverridden = aValue;
}

void RuntimeService::UpdateAllWorkerLanguages(
    const nsTArray<nsString>& aLanguages) {
  MOZ_ASSERT(NS_IsMainThread());

  {
    MutexAutoLock lock(mMutex);
    mNavigatorProperties.mLanguages = aLanguages.Clone();
  }
  BroadcastAllWorkers(
      [&aLanguages](auto& worker) { worker.UpdateLanguages(aLanguages); });
}

void RuntimeService::UpdateAllWorkerMemoryParameter(JSGCParamKey aKey,
                                                    Maybe<uint32_t> aValue) {
  BroadcastAllWorkers([aKey, aValue](auto& worker) {
    worker.UpdateJSWorkerMemoryParameter(aKey, aValue);
  });
}

#ifdef JS_GC_ZEAL
void RuntimeService::UpdateAllWorkerGCZeal() {
  BroadcastAllWorkers([](auto& worker) {
    worker.UpdateGCZeal(sDefaultJSSettings->gcZeal,
                        sDefaultJSSettings->gcZealFrequency);
  });
}
#endif

void RuntimeService::SetLowMemoryStateAllWorkers(bool aState) {
  BroadcastAllWorkers(
      [aState](auto& worker) { worker.SetLowMemoryState(aState); });
}

void RuntimeService::GarbageCollectAllWorkers(bool aShrinking) {
  BroadcastAllWorkers(
      [aShrinking](auto& worker) { worker.GarbageCollect(aShrinking); });
}

void RuntimeService::CycleCollectAllWorkers() {
  BroadcastAllWorkers([](auto& worker) { worker.CycleCollect(); });
}

void RuntimeService::SendOfflineStatusChangeEventToAllWorkers(bool aIsOffline) {
  BroadcastAllWorkers([aIsOffline](auto& worker) {
    worker.OfflineStatusChangeEvent(aIsOffline);
  });
}

void RuntimeService::MemoryPressureAllWorkers() {
  BroadcastAllWorkers([](auto& worker) { worker.MemoryPressure(); });
}

uint32_t RuntimeService::ClampedHardwareConcurrency(bool aRFPHardcoded,
                                                    bool aRFPTiered) const {
  // The Firefox Hardware Report says 34% of Firefox users have exactly 4 cores.
  // When the resistFingerprinting pref is set, we want to blend into the crowd
  // so spoof navigator.hardwareConcurrency = 4 to reduce user uniqueness. On
  // OSX, the majority of Macs have 8 cores.
  if (MOZ_UNLIKELY(aRFPHardcoded)) {
#ifdef XP_MACOSX
    return 8;
#else
    return 4;
#endif
  }

  // This needs to be atomic, because multiple workers, and even mainthread,
  // could race to initialize it at once.
  static Atomic<uint32_t> unclampedHardwareConcurrency;

  // No need to loop here: if compareExchange fails, that just means that some
  // other worker has initialized numberOfProcessors, so we're good to go.
  if (!unclampedHardwareConcurrency) {
    int32_t numberOfProcessors = 0;
#if defined(XP_MACOSX)
    if (nsMacUtilsImpl::IsTCSMAvailable()) {
      // On failure, zero is returned from GetPhysicalCPUCount()
      // and we fallback to PR_GetNumberOfProcessors below.
      numberOfProcessors = nsMacUtilsImpl::GetPhysicalCPUCount();
    }
#endif
    if (numberOfProcessors == 0) {
      numberOfProcessors = PR_GetNumberOfProcessors();
    }
    if (numberOfProcessors <= 0) {
      numberOfProcessors = 1;  // Must be one there somewhere
    }
    (void)unclampedHardwareConcurrency.compareExchange(0, numberOfProcessors);
  }

  if (MOZ_UNLIKELY(aRFPTiered)) {
    if (unclampedHardwareConcurrency >= 8) {
      return 8;
    }
    return 4;
  }

  return std::min(uint32_t(unclampedHardwareConcurrency),
                  StaticPrefs::dom_maxHardwareConcurrency());
}

// nsISupports
NS_IMPL_ISUPPORTS(RuntimeService, nsIObserver)

// nsIObserver
NS_IMETHODIMP
RuntimeService::Observe(nsISupports* aSubject, const char* aTopic,
                        const char16_t* aData) {
  AssertIsOnMainThread();

  if (!strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID)) {
    Shutdown();
    return NS_OK;
  }
  if (!strcmp(aTopic, NS_XPCOM_SHUTDOWN_THREADS_OBSERVER_ID)) {
    Cleanup();
    return NS_OK;
  }
  if (!strcmp(aTopic, GC_REQUEST_OBSERVER_TOPIC)) {
    GarbageCollectAllWorkers(/* shrinking = */ false);
    return NS_OK;
  }
  if (!strcmp(aTopic, CC_REQUEST_OBSERVER_TOPIC)) {
    CycleCollectAllWorkers();
    return NS_OK;
  }
  if (!strcmp(aTopic, MEMORY_PRESSURE_OBSERVER_TOPIC)) {
    nsDependentString data(aData);
    // Don't continue to GC/CC if we are in an ongoing low-memory state since
    // its very slow and it likely won't help us anyway.
    if (data.EqualsLiteral(LOW_MEMORY_ONGOING_DATA)) {
      return NS_OK;
    }
    if (data.EqualsLiteral(LOW_MEMORY_DATA)) {
      SetLowMemoryStateAllWorkers(true);
    }
    GarbageCollectAllWorkers(/* shrinking = */ true);
    CycleCollectAllWorkers();
    MemoryPressureAllWorkers();
    return NS_OK;
  }
  if (!strcmp(aTopic, MEMORY_PRESSURE_STOP_OBSERVER_TOPIC)) {
    SetLowMemoryStateAllWorkers(false);
    return NS_OK;
  }
  if (!strcmp(aTopic, NS_IOSERVICE_OFFLINE_STATUS_TOPIC)) {
    SendOfflineStatusChangeEventToAllWorkers(NS_IsOffline());
    return NS_OK;
  }

  MOZ_ASSERT_UNREACHABLE("Unknown observer topic!");
  return NS_OK;
}

namespace {
const char* WorkerKindToString(WorkerKind kind) {
  switch (kind) {
    case WorkerKindDedicated:
      return "dedicated";
    case WorkerKindShared:
      return "shared";
    case WorkerKindService:
      return "service";
    default:
      NS_WARNING("Unknown worker type");
      return "unknown worker type";
  }
}

void LogWorker(WorkerPrivate* worker, const char* category) {
  AssertIsOnMainThread();

  SHUTDOWN_LOG(("Found %s (%s): %s", category,
                WorkerKindToString(worker->Kind()),
                NS_ConvertUTF16toUTF8(worker->ScriptURL()).get()));

  if (worker->Kind() == WorkerKindService) {
    SHUTDOWN_LOG(("Scope: %s", worker->ServiceWorkerScope().get()));
  }

  nsCString origin;
  worker->GetPrincipal()->GetOrigin(origin);
  SHUTDOWN_LOG(("Principal: %s", origin.get()));

  nsCString loadingOrigin;
  worker->GetLoadingPrincipal()->GetOrigin(loadingOrigin);
  SHUTDOWN_LOG(("LoadingPrincipal: %s", loadingOrigin.get()));

  RefPtr<DumpCrashInfoRunnable> runnable = new DumpCrashInfoRunnable(worker);
  if (runnable->DispatchAndWait()) {
    SHUTDOWN_LOG(("CrashInfo: %s", runnable->MsgData().get()));
  } else {
    SHUTDOWN_LOG(("CrashInfo: dispatch failed"));
  }
}
}  // namespace

void RuntimeService::DumpRunningWorkers() {
  // Temporarily set the LogLevel high enough to be certain the messages are
  // visible.
  LogModule* module = gWorkerShutdownDumpLog;
  LogLevel prevLevel = module->Level();

  const auto cleanup =
      MakeScopeExit([module, prevLevel] { module->SetLevel(prevLevel); });

  if (prevLevel < LogLevel::Debug) {
    module->SetLevel(LogLevel::Debug);
  }

  MutexAutoLock lock(mMutex);

  for (const auto& info : mDomainMap.Values()) {
    for (WorkerPrivate* worker : info->mActiveWorkers) {
      LogWorker(worker, "ActiveWorker");
    }

    for (WorkerPrivate* worker : info->mActiveServiceWorkers) {
      LogWorker(worker, "ActiveServiceWorker");
    }

    for (WorkerPrivate* worker : info->mQueuedWorkers) {
      LogWorker(worker, "QueuedWorker");
    }
  }
}

void RuntimeService::UpdateWorkersPlaybackState(
    const nsPIDOMWindowInner& aWindow, bool aIsPlayingAudio) {
  AssertIsOnMainThread();

  for (WorkerPrivate* const worker : GetWorkersForWindow(aWindow)) {
    MOZ_ASSERT(!worker->IsSharedWorker());
    worker->SetIsPlayingAudio(aIsPlayingAudio);
  }
}

void RuntimeService::UpdateWorkersPeerConnections(
    const nsPIDOMWindowInner& aWindow, bool aHasPeerConnections) {
  AssertIsOnMainThread();

  for (WorkerPrivate* const worker : GetWorkersForWindow(aWindow)) {
    MOZ_ASSERT(!worker->IsSharedWorker());
    worker->SetActivePeerConnections(aHasPeerConnections);
  }
}

void RuntimeService::UpdateWorkersLanguageOverride(
    const nsPIDOMWindowInner& aWindow, const nsCString& aLanguageOverride) {
  AssertIsOnMainThread();

  nsTArray<nsString> resolvedLanguages;
  Navigator::GetAcceptLanguages(resolvedLanguages, aLanguageOverride.IsEmpty()
                                                       ? nullptr
                                                       : &aLanguageOverride);

  for (WorkerPrivate* const worker : GetWorkersForWindow(aWindow)) {
    MOZ_ASSERT(!worker->IsSharedWorker());
    worker->UpdateLanguageOverride(aLanguageOverride, resolvedLanguages);
  }
}

bool LogViolationDetailsRunnable::MainThreadRun() {
  AssertIsOnMainThread();
  MOZ_ASSERT(mWorkerRef);

  nsIContentSecurityPolicy* csp = mWorkerRef->Private()->GetCsp();
  if (csp) {
    csp->LogViolationDetails(mViolationType,
                             nullptr,  // triggering element
                             mWorkerRef->Private()->CSPEventListener(),
                             mFileName, mScriptSample, mLineNum, mColumnNum,
                             u""_ns, u""_ns);
  }

  return true;
}

// MOZ_CAN_RUN_SCRIPT_BOUNDARY until Runnable::Run is MOZ_CAN_RUN_SCRIPT.  See
// bug 1535398.
MOZ_CAN_RUN_SCRIPT_BOUNDARY
NS_IMETHODIMP
WorkerThreadPrimaryRunnable::Run() {
  NS_ConvertUTF16toUTF8 url(mWorkerPrivate->ScriptURL());
  AUTO_PROFILER_LABEL_DYNAMIC_CSTR("WorkerThreadPrimaryRunnable::Run", OTHER,
                                   url.get());

  using mozilla::ipc::BackgroundChild;
  {
    bool runLoopRan = false;
    auto failureCleanup = MakeScopeExit([&]() {
      // If Worker initialization fails, call WorkerPrivate::ScheduleDeletion()
      // to release the WorkerPrivate in the parent thread.
      mWorkerPrivate->ScheduleDeletion(WorkerPrivate::WorkerRan);
    });

    mWorkerPrivate->SetWorkerPrivateInWorkerThread(mThread.unsafeGetRawPtr());

    const auto threadCleanup = MakeScopeExit([&] {
      // If Worker initialization fails, such as creating a BackgroundChild or
      // the worker's JSContext initialization failing, call
      // WorkerPrivate::RunLoopNeverRan() to set the Worker to the correct
      // status, which means "Dead," to forbid WorkerThreadRunnable dispatching.
      if (!runLoopRan) {
        mWorkerPrivate->RunLoopNeverRan();
      }
      mWorkerPrivate->ResetWorkerPrivateInWorkerThread();
    });

    mWorkerPrivate->AssertIsOnWorkerThread();

    // This needs to be initialized on the worker thread before being used on
    // the main thread and calling BackgroundChild::GetOrCreateForCurrentThread
    // exposes it to the main thread.
    mWorkerPrivate->EnsurePerformanceStorage();

    if (NS_WARN_IF(!BackgroundChild::GetOrCreateForCurrentThread())) {
      return NS_ERROR_FAILURE;
    }

    nsWeakPtr globalScopeSentinel;
    nsWeakPtr debuggerScopeSentinel;
    // Never use the following pointers without checking their corresponding
    // nsWeakPtr sentinel, defined above and initialized after DoRunLoop ends.
    WorkerGlobalScopeBase* globalScopeRawPtr = nullptr;
    WorkerGlobalScopeBase* debuggerScopeRawPtr = nullptr;
    {
      nsCycleCollector_startup();

      auto context = std::make_unique<WorkerJSContext>(mWorkerPrivate);
      nsresult rv = context->Initialize(mParentRuntime);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }

      JSContext* cx = context->Context();

      if (!InitJSContextForWorker(mWorkerPrivate, cx)) {
        return NS_ERROR_FAILURE;
      }

      failureCleanup.release();

      // Binding the RemoteWorkerDebugger child endpoint after initialization
      // successfully. Self-gates on UseRemoteDebugger().
      mWorkerPrivate->BindRemoteWorkerDebuggerChild();

      runLoopRan = true;

      {
        PROFILER_SET_JS_CONTEXT(context.get());

        {
          // Makes the worker thread an AbstractThread with tail dispatch while
          // the worker runs, so that e.g. state mirroring works on it.
          // Lifecycle matches the main thread, where
          // AbstractThread::ShutdownMainThread() runs before
          // PROFILER_CLEAR_JS_CONTEXT() and the final cycle collection.
          AutoXPCOMThreadWrapper abstractThread(
              TailDispatchPolicy::ConsistentOrdering);

          {
            // We're on the worker thread here, and WorkerPrivate's refcounting
            // is non-threadsafe: you can only do it on the parent thread.  What
            // that means in practice is that we're relying on it being kept
            // alive while we run.  Hopefully.
            MOZ_KnownLive(mWorkerPrivate)->DoRunLoop(cx);
            // The AutoJSAPI in DoRunLoop should have reported any exceptions
            // left on cx.
            MOZ_ASSERT(!JS_IsExceptionPending(cx));
          }

          mWorkerPrivate->ShutdownModuleLoader();

          mWorkerPrivate->RunShutdownTasks();

          BackgroundChild::CloseForCurrentThread();
        }

        PROFILER_CLEAR_JS_CONTEXT();
      }

      // There may still be runnables on the debugger event queue that hold a
      // strong reference to the debugger global scope. These runnables are not
      // visible to the cycle collector, so we need to make sure to clear the
      // debugger event queue before we try to destroy the context. If we don't,
      // the garbage collector will crash.
      // Note that this just releases the runnables and does not execute them.
      mWorkerPrivate->ClearDebuggerEventQueue();

      // Before shutting down the cycle collector we need to do one more pass
      // through the event loop to clean up any C++ objects that need deferred
      // cleanup.
      NS_ProcessPendingEvents(nullptr);

      // At this point we expect the scopes to be alive if they were ever
      // created successfully, keep weak references and set up the sentinels.
      globalScopeRawPtr = mWorkerPrivate->GlobalScope();
      if (globalScopeRawPtr) {
        globalScopeSentinel = do_GetWeakReference(globalScopeRawPtr);
      }
      MOZ_ASSERT(!globalScopeRawPtr || globalScopeSentinel);
      debuggerScopeRawPtr = mWorkerPrivate->DebuggerGlobalScope();
      if (debuggerScopeRawPtr) {
        debuggerScopeSentinel = do_GetWeakReference(debuggerScopeRawPtr);
      }
      MOZ_ASSERT(!debuggerScopeRawPtr || debuggerScopeSentinel);

      // To our best knowledge nobody should need a reference to our globals
      // now (NS_ProcessPendingEvents is the last expected potential usage)
      // and we can unroot them.
      mWorkerPrivate->UnrootGlobalScopes();

      // Perform a full GC until we collect the main worker global and CC,
      // which should break all cycles that touch JS.
      bool repeatGCCC = true;
      while (repeatGCCC) {
        JS::PrepareForFullGC(cx);
        JS::NonIncrementalGC(cx, JS::GCOptions::Shutdown,
                             JS::GCReason::WORKER_SHUTDOWN);

        // If we CCed something or got new events as a side effect, repeat.
        repeatGCCC = mWorkerPrivate->isLastCCCollectedAnything() ||
                     NS_HasPendingEvents(nullptr);
        NS_ProcessPendingEvents(nullptr);
      }

      // The worker global should be unrooted and the shutdown of cycle
      // collection should break all the remaining cycles.
      nsCycleCollector_shutdown();

      // If ever the CC shutdown run caused side effects, process them.
      NS_ProcessPendingEvents(nullptr);

      // Now WorkerJSContext goes out of scope. Do not use any cycle
      // collectable objects nor JS after this point!
    }

    // Check sentinels if we actually removed all global scope references.
    // In case use the earlier set-aside raw pointers to not mess with the
    // ref counting after the cycle collector has gone away.
    if (NS_WARN_IF(globalScopeSentinel && globalScopeSentinel->IsAlive())) {
      MOZ_ASSERT_UNREACHABLE("WorkerGlobalScope alive after worker shutdown");
      globalScopeRawPtr->NoteWorkerTerminated();
      globalScopeRawPtr = nullptr;
    }
    if (NS_WARN_IF(debuggerScopeSentinel && debuggerScopeSentinel->IsAlive())) {
      MOZ_ASSERT_UNREACHABLE("Debugger global alive after worker shutdown");
      debuggerScopeRawPtr->NoteWorkerTerminated();
      debuggerScopeRawPtr = nullptr;
    }
  }

  mWorkerPrivate->ScheduleDeletion(WorkerPrivate::WorkerRan);

  // It is no longer safe to touch mWorkerPrivate.
  mWorkerPrivate = nullptr;

  // Now recycle this thread.
  nsCOMPtr<nsIEventTarget> mainTarget = GetMainThreadSerialEventTarget();
  MOZ_ASSERT(mainTarget);

  RefPtr<FinishedRunnable> finishedRunnable =
      new FinishedRunnable(std::move(mThread));
  MOZ_ALWAYS_SUCCEEDS(
      mainTarget->Dispatch(finishedRunnable, NS_DISPATCH_NORMAL));

  return NS_OK;
}

NS_IMETHODIMP
WorkerThreadPrimaryRunnable::FinishedRunnable::Run() {
  AssertIsOnMainThread();

  SafeRefPtr<WorkerThread> thread = std::move(mThread);
  if (thread->ShutdownRequired()) {
    MOZ_ALWAYS_SUCCEEDS(thread->Shutdown());
  }

  return NS_OK;
}

}  // namespace workerinternals

// This is mostly for invoking within a debugger.
void DumpRunningWorkers() {
  RuntimeService* runtimeService = RuntimeService::GetService();
  if (runtimeService) {
    runtimeService->DumpRunningWorkers();
  } else {
    NS_WARNING("RuntimeService not found");
  }
}

void CancelWorkersForWindow(const nsPIDOMWindowInner& aWindow) {
  AssertIsOnMainThread();
  RuntimeService* runtime = RuntimeService::GetService();
  if (runtime) {
    runtime->CancelWorkersForWindow(aWindow);
  }
}

void UpdateWorkersBackgroundState(const nsPIDOMWindowInner& aWindow,
                                  bool aIsBackground) {
  AssertIsOnMainThread();
  RuntimeService* runtime = RuntimeService::GetService();
  if (runtime) {
    runtime->UpdateWorkersBackgroundState(aWindow, aIsBackground);
  }
}

void FreezeWorkersForWindow(const nsPIDOMWindowInner& aWindow) {
  AssertIsOnMainThread();
  RuntimeService* runtime = RuntimeService::GetService();
  if (runtime) {
    runtime->FreezeWorkersForWindow(aWindow);
  }
}

void ThawWorkersForWindow(const nsPIDOMWindowInner& aWindow) {
  AssertIsOnMainThread();
  RuntimeService* runtime = RuntimeService::GetService();
  if (runtime) {
    runtime->ThawWorkersForWindow(aWindow);
  }
}

void SuspendWorkersForWindow(const nsPIDOMWindowInner& aWindow) {
  AssertIsOnMainThread();
  RuntimeService* runtime = RuntimeService::GetService();
  if (runtime) {
    runtime->SuspendWorkersForWindow(aWindow);
  }
}

void ResumeWorkersForWindow(const nsPIDOMWindowInner& aWindow) {
  AssertIsOnMainThread();
  RuntimeService* runtime = RuntimeService::GetService();
  if (runtime) {
    runtime->ResumeWorkersForWindow(aWindow);
  }
}

void PropagateStorageAccessPermissionGrantedToWorkers(
    const nsPIDOMWindowInner& aWindow) {
  AssertIsOnMainThread();
  MOZ_ASSERT_IF(aWindow.GetExtantDoc(), aWindow.GetExtantDoc()
                                            ->CookieJarSettings()
                                            ->GetRejectThirdPartyContexts());

  RuntimeService* runtime = RuntimeService::GetService();
  if (runtime) {
    runtime->PropagateStorageAccessPermissionGranted(aWindow);
  }
}

void UpdateTimezoneOverrideForWorkers(const nsPIDOMWindowInner& aWindow,
                                      const nsAString& aTimezone) {
  AssertIsOnMainThread();
  RuntimeService* runtime = RuntimeService::GetService();
  if (runtime) {
    runtime->UpdateTimezoneOverrideForWorkers(aWindow, aTimezone);
  }
}

WorkerPrivate* GetWorkerPrivateFromContext(JSContext* aCx) {
  MOZ_ASSERT(!NS_IsMainThread());
  MOZ_ASSERT(aCx);

  CycleCollectedJSContext* ccjscx = CycleCollectedJSContext::GetFor(aCx);
  if (!ccjscx) {
    return nullptr;
  }

  WorkerJSContext* workerjscx = ccjscx->GetAsWorkerJSContext();
  // GetWorkerPrivateFromContext is called only for worker contexts.  The
  // context private is cleared early in ~CycleCollectedJSContext() and so
  // GetFor() returns null above if called after ccjscx is no longer a
  // WorkerJSContext.
  MOZ_ASSERT(workerjscx);
  return workerjscx->GetWorkerPrivate();
}

WorkerPrivate* GetCurrentThreadWorkerPrivate() {
  if (NS_IsMainThread()) {
    return nullptr;
  }

  CycleCollectedJSContext* ccjscx = CycleCollectedJSContext::Get();
  if (!ccjscx) {
    return nullptr;
  }

  WorkerJSContext* workerjscx = ccjscx->GetAsWorkerJSContext();
  // Even when GetCurrentThreadWorkerPrivate() is called on worker
  // threads, the ccjscx will no longer be a WorkerJSContext if called from
  // stable state events during ~CycleCollectedJSContext().
  if (!workerjscx) {
    return nullptr;
  }

  return workerjscx->GetWorkerPrivate();
}

bool IsCurrentThreadRunningWorker() {
  return !NS_IsMainThread() && !!GetCurrentThreadWorkerPrivate();
}

bool IsCurrentThreadRunningChromeWorker() {
  WorkerPrivate* wp = GetCurrentThreadWorkerPrivate();
  return wp && wp->UsesSystemPrincipal();
}

JSContext* GetCurrentWorkerThreadJSContext() {
  WorkerPrivate* wp = GetCurrentThreadWorkerPrivate();
  if (!wp) {
    return nullptr;
  }
  return wp->GetJSContext();
}

JSObject* GetCurrentThreadWorkerGlobal() {
  WorkerPrivate* wp = GetCurrentThreadWorkerPrivate();
  if (!wp) {
    return nullptr;
  }
  WorkerGlobalScope* scope = wp->GlobalScope();
  if (!scope) {
    return nullptr;
  }
  return scope->GetGlobalJSObject();
}

JSObject* GetCurrentThreadWorkerDebuggerGlobal() {
  WorkerPrivate* wp = GetCurrentThreadWorkerPrivate();
  if (!wp) {
    return nullptr;
  }
  WorkerDebuggerGlobalScope* scope = wp->DebuggerGlobalScope();
  if (!scope) {
    return nullptr;
  }
  return scope->GetGlobalJSObject();
}

void UpdateWorkersPlaybackState(const nsPIDOMWindowInner& aWindow,
                                bool aIsPlayingAudio) {
  AssertIsOnMainThread();
  RuntimeService* runtime = RuntimeService::GetService();
  if (runtime) {
    runtime->UpdateWorkersPlaybackState(aWindow, aIsPlayingAudio);
  }
}

void UpdateWorkersPeerConnections(const nsPIDOMWindowInner& aWindow,
                                  bool aHasPeerConnections) {
  AssertIsOnMainThread();
  RuntimeService* runtime = RuntimeService::GetService();
  if (runtime) {
    runtime->UpdateWorkersPeerConnections(aWindow, aHasPeerConnections);
  }
}

}  // namespace dom
}  // namespace mozilla
