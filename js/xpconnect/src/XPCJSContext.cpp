/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/* Per JSContext object */

#include "mozilla/AppShutdown.h"
#include "mozilla/MemoryTelemetry.h"
#include "mozilla/Preferences.h"
#include "mozilla/Services.h"
#include "mozilla/UniquePtr.h"

#include "ExecutionTracerIntegration.h"
#include "mozJSModuleLoader.h"
#include "nsIDebug2.h"
#include "nsIObserverService.h"
#include "nsNetUtil.h"
#include "nsPIDOMWindow.h"
#include "nsPrintfCString.h"
#include "nsThreadUtils.h"
#include "WrapperFactory.h"
#include "XPCJSMemoryReporter.h"
#include "xpcprivate.h"
#include "xpcpublic.h"
#include "XPCSelfHostedShmem.h"
#include "XPCWrapper.h"
#ifdef FUZZING
#  include "mozilla/StaticPrefs_fuzzing.h"
#endif
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/Atomics.h"
#include "mozilla/Attributes.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Element.h"
#include "mozilla/dom/ScriptLoader.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/WakeLockBinding.h"
#include "mozilla/dom/WindowBinding.h"
#include "mozilla/extensions/WebExtensionPolicy.h"
#include "mozilla/glean/JsXpconnectMetrics.h"
#include "mozilla/ProcessHangMonitor.h"
#include "mozilla/scache/StartupCache.h"
#include "mozilla/Sprintf.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_javascript.h"
#include "mozilla/SystemPrincipal.h"
#include "mozilla/TaskController.h"
#include "mozilla/UniquePtrExtensions.h"

#include "AccessCheck.h"
#include "ExpandedPrincipal.h"
#include "GeckoProfiler.h"
#include "jsapi.h"
#include "nsAboutProtocolUtils.h"
#include "nsCCUncollectableMarker.h"
#include "nsContentUtils.h"
#include "nsCycleCollectionNoteRootCallback.h"
#include "nsCycleCollector.h"
#include "nsGlobalWindowInner.h"
#include "nsINode.h"
#include "nsIXULRuntime.h"
#include "nsJSEnvironment.h"
#include "nsJSPrincipals.h"

#include "fmt/format.h"
#include "js/ArrayBuffer.h"
#include "js/ContextOptions.h"
#include "js/DOMEventDispatch.h"
#include "js/experimental/LoggingInterface.h"
#include "js/HelperThreadAPI.h"
#include "js/Initialization.h"
#include "js/MemoryMetrics.h"
#include "js/Prefs.h"
#include "js/WasmFeatures.h"

#if defined(XP_LINUX) && !defined(ANDROID)
// For getrlimit and min/max.
#  include <algorithm>
#  include <sys/resource.h>
#endif

#ifdef XP_WIN
// For min.
#  include <algorithm>
#  include <windows.h>
#endif

using namespace mozilla;
using namespace mozilla::dom;
using namespace xpc;
using namespace JS;

// Callback for JIT trace events to dispatch DOM events to global target
static void DispatchJitEventToDOM(JSContext* cx, const char* eventType) {
  // Check if test interfaces are enabled
  if (!StaticPrefs::dom_expose_test_interfaces()) {
    return;
  }

  if (!cx) {
    return;
  }

  // Get the global object from the context
  JSObject* globalObj = JS::CurrentGlobalOrNull(cx);
  if (!globalObj) {
    return;
  }

  nsIGlobalObject* global = xpc::NativeGlobal(globalObj);
  if (!global) {
    return;
  }

  nsCOMPtr<nsPIDOMWindowInner> window = do_QueryInterface(global);
  if (!window) {
    return;
  }

  mozilla::dom::Document* doc = window->GetDoc();
  if (!doc) {
    return;
  }

  nsCOMPtr<nsINode> target = doc;
  if (!target) {
    return;
  }

  // Convert event type to nsString and dispatch to document
  NS_ConvertUTF8toUTF16 eventTypeStr(eventType);
  RefPtr<AsyncEventDispatcher> dispatcher = new AsyncEventDispatcher(
      target, eventTypeStr, CanBubble::eYes, ChromeOnlyDispatch::eNo);
  dispatcher->PostDOMEvent();
}

// We will clamp to reasonable values if this isn't set.
#if !defined(PTHREAD_STACK_MIN)
#  define PTHREAD_STACK_MIN 0
#endif

static void WatchdogMain(void* arg);
class Watchdog;
class WatchdogManager;
class MOZ_RAII AutoLockWatchdog final {
  Watchdog* const mWatchdog;

 public:
  explicit AutoLockWatchdog(Watchdog* aWatchdog);
  ~AutoLockWatchdog();
};

class Watchdog {
 public:
  explicit Watchdog(WatchdogManager* aManager)
      : mManager(aManager),
        mLock(nullptr),
        mWakeup(nullptr),
        mThread(nullptr),
        mHibernating(false),
        mInitialized(false),
        mShuttingDown(false),
        mMinScriptRunTimeSeconds(1) {}
  ~Watchdog() { MOZ_ASSERT(!Initialized()); }

  WatchdogManager* Manager() { return mManager; }
  bool Initialized() { return mInitialized; }
  bool ShuttingDown() { return mShuttingDown; }
  PRLock* GetLock() { return mLock; }
  bool Hibernating() { return mHibernating; }
  void WakeUp() {
    MOZ_ASSERT(Initialized());
    MOZ_ASSERT(Hibernating());
    mHibernating = false;
    PR_NotifyCondVar(mWakeup);
  }

  //
  // Invoked by the main thread only.
  //

  void Init() {
    MOZ_ASSERT(NS_IsMainThread());
    mLock = PR_NewLock();
    if (!mLock) {
      MOZ_CRASH("PR_NewLock failed.");
    }

    mWakeup = PR_NewCondVar(mLock);
    if (!mWakeup) {
      MOZ_CRASH("PR_NewCondVar failed.");
    }

    {
      // Make sure the debug service is instantiated before we create the
      // watchdog thread, since we intentionally try to keep the thread's stack
      // segment as small as possible. It isn't always large enough to
      // instantiate a new service, and even when it is, we don't want fault in
      // extra pages if we can avoid it.
      nsCOMPtr<nsIDebug2> dbg = do_GetService("@mozilla.org/xpcom/debug;1");
      (void)dbg;
    }

    {
      AutoLockWatchdog lock(this);

      // The watchdog thread loop is pretty trivial, and should not
      // require much stack space to do its job. So only give it 32KiB
      // or the platform minimum. On modern Linux libc this might resolve to
      // a runtime call.
      size_t watchdogStackSize = PTHREAD_STACK_MIN;
      watchdogStackSize = std::max<size_t>(32 * 1024, watchdogStackSize);

      // Gecko uses thread private for accounting and has to clean up at thread
      // exit. Therefore, even though we don't have a return value from the
      // watchdog, we need to join it on shutdown.
      mThread = PR_CreateThread(PR_USER_THREAD, WatchdogMain, this,
                                PR_PRIORITY_NORMAL, PR_GLOBAL_THREAD,
                                PR_JOINABLE_THREAD, watchdogStackSize);
      if (!mThread) {
        MOZ_CRASH("PR_CreateThread failed!");
      }

      // WatchdogMain acquires the lock and then asserts mInitialized. So
      // make sure to set mInitialized before releasing the lock here so
      // that it's atomic with the creation of the thread.
      mInitialized = true;
    }
  }

  void Shutdown() {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(Initialized());
    {  // Scoped lock.
      AutoLockWatchdog lock(this);

      // Signal to the watchdog thread that it's time to shut down.
      mShuttingDown = true;

      // Wake up the watchdog, and wait for it to call us back.
      PR_NotifyCondVar(mWakeup);
    }

    PR_JoinThread(mThread);

    // The thread sets mShuttingDown to false as it exits.
    MOZ_ASSERT(!mShuttingDown);

    // Destroy state.
    mThread = nullptr;
    PR_DestroyCondVar(mWakeup);
    mWakeup = nullptr;
    PR_DestroyLock(mLock);
    mLock = nullptr;

    // All done.
    mInitialized = false;
  }

  void SetMinScriptRunTimeSeconds(int32_t seconds) {
    // This variable is atomic, and is set from the main thread without
    // locking.
    MOZ_ASSERT(seconds > 0);
    mMinScriptRunTimeSeconds = seconds;
  }

  //
  // Invoked by the watchdog thread only.
  //

  void Hibernate() {
    MOZ_ASSERT(!NS_IsMainThread());
    mHibernating = true;
    Sleep(PR_INTERVAL_NO_TIMEOUT);
  }
  void Sleep(PRIntervalTime timeout) {
    MOZ_ASSERT(!NS_IsMainThread());
    AUTO_PROFILER_THREAD_SLEEP;
    MOZ_ALWAYS_TRUE(PR_WaitCondVar(mWakeup, timeout) == PR_SUCCESS);
  }
  void Finished() {
    MOZ_ASSERT(!NS_IsMainThread());
    mShuttingDown = false;
  }

  int32_t MinScriptRunTimeSeconds() { return mMinScriptRunTimeSeconds; }

 private:
  WatchdogManager* mManager;

  PRLock* mLock;
  PRCondVar* mWakeup;
  PRThread* mThread;
  bool mHibernating;
  bool mInitialized;
  bool mShuttingDown;
  mozilla::Atomic<int32_t> mMinScriptRunTimeSeconds;
};

#define PREF_MAX_SCRIPT_RUN_TIME_CONTENT "dom.max_script_run_time"
#define PREF_MAX_SCRIPT_RUN_TIME_CHROME "dom.max_chrome_script_run_time"
#define PREF_MAX_SCRIPT_RUN_TIME_EXT_CONTENT \
  "dom.max_ext_content_script_run_time"

static const char* gCallbackPrefs[] = {
    "dom.use_watchdog",
    PREF_MAX_SCRIPT_RUN_TIME_CONTENT,
    PREF_MAX_SCRIPT_RUN_TIME_CHROME,
    PREF_MAX_SCRIPT_RUN_TIME_EXT_CONTENT,
    nullptr,
};

class WatchdogManager {
 public:
  explicit WatchdogManager() {
    // All the timestamps start at zero.
    PodArrayZero(mTimestamps);

    // Register ourselves as an observer to get updates on the pref.
    Preferences::RegisterCallbacks(PrefsChanged, gCallbackPrefs, this);
  }

  virtual ~WatchdogManager() {
    // Shutting down the watchdog requires context-switching to the watchdog
    // thread, which isn't great to do in a destructor. So we require
    // consumers to shut it down manually before releasing it.
    MOZ_ASSERT(!mWatchdog);
  }

 private:
  static void PrefsChanged(const char* aPref, void* aSelf) {
    static_cast<WatchdogManager*>(aSelf)->RefreshWatchdog();
  }

 public:
  void Shutdown() {
    Preferences::UnregisterCallbacks(PrefsChanged, gCallbackPrefs, this);
  }

  void RegisterContext(XPCJSContext* aContext) {
    MOZ_ASSERT(NS_IsMainThread());
    AutoLockWatchdog lock(mWatchdog.get());

    if (aContext->mActive == XPCJSContext::CONTEXT_ACTIVE) {
      mActiveContexts.insertBack(aContext);
    } else {
      mInactiveContexts.insertBack(aContext);
    }

    // Enable the watchdog, if appropriate.
    RefreshWatchdog();
  }

  void UnregisterContext(XPCJSContext* aContext) {
    MOZ_ASSERT(NS_IsMainThread());
    AutoLockWatchdog lock(mWatchdog.get());

    // aContext must be in one of our two lists, simply remove it.
    aContext->LinkedListElement<XPCJSContext>::remove();

#ifdef DEBUG
    // If this was the last context, we should have already shut down
    // the watchdog.
    if (mActiveContexts.isEmpty() && mInactiveContexts.isEmpty()) {
      MOZ_ASSERT(!mWatchdog);
    }
#endif
  }

  // Context statistics. These live on the watchdog manager, are written
  // from the main thread, and are read from the watchdog thread (holding
  // the lock in each case).
  void RecordContextActivity(XPCJSContext* aContext, bool active) {
    // The watchdog reads this state, so acquire the lock before writing it.
    MOZ_ASSERT(NS_IsMainThread());
    AutoLockWatchdog lock(mWatchdog.get());

    // Write state.
    aContext->mLastStateChange = PR_Now();
    aContext->mActive =
        active ? XPCJSContext::CONTEXT_ACTIVE : XPCJSContext::CONTEXT_INACTIVE;
    UpdateContextLists(aContext);

    // The watchdog may be hibernating, waiting for the context to go
    // active. Wake it up if necessary.
    if (active && mWatchdog && mWatchdog->Hibernating()) {
      mWatchdog->WakeUp();
    }
  }

  bool IsAnyContextActive() { return !mActiveContexts.isEmpty(); }
  PRTime TimeSinceLastActiveContext() {
    // Must be called on the watchdog thread with the lock held.
    MOZ_ASSERT(!NS_IsMainThread());
    PR_ASSERT_CURRENT_THREAD_OWNS_LOCK(mWatchdog->GetLock());
    MOZ_ASSERT(mActiveContexts.isEmpty());
    MOZ_ASSERT(!mInactiveContexts.isEmpty());

    // We store inactive contexts with the most recently added inactive
    // context at the end of the list.
    return PR_Now() - mInactiveContexts.getLast()->mLastStateChange;
  }

  void RecordTimestamp(WatchdogTimestampCategory aCategory) {
    // Must be called on the watchdog thread with the lock held.
    MOZ_ASSERT(!NS_IsMainThread());
    PR_ASSERT_CURRENT_THREAD_OWNS_LOCK(mWatchdog->GetLock());
    MOZ_ASSERT(aCategory != TimestampContextStateChange,
               "Use RecordContextActivity to update this");

    mTimestamps[aCategory] = PR_Now();
  }

  PRTime GetContextTimestamp(XPCJSContext* aContext,
                             const AutoLockWatchdog& aProofOfLock) {
    return aContext->mLastStateChange;
  }

  PRTime GetTimestamp(WatchdogTimestampCategory aCategory,
                      const AutoLockWatchdog& aProofOfLock) {
    MOZ_ASSERT(aCategory != TimestampContextStateChange,
               "Use GetContextTimestamp to retrieve this");
    return mTimestamps[aCategory];
  }

  Watchdog* GetWatchdog() { return mWatchdog.get(); }

  void RefreshWatchdog() {
    bool wantWatchdog = Preferences::GetBool("dom.use_watchdog", true);
    if (wantWatchdog != !!mWatchdog) {
      if (wantWatchdog) {
        StartWatchdog();
      } else {
        StopWatchdog();
      }
    }

    if (mWatchdog) {
      int32_t contentTime = StaticPrefs::dom_max_script_run_time();
      if (contentTime <= 0) {
        contentTime = INT32_MAX;
      }
      int32_t chromeTime = StaticPrefs::dom_max_chrome_script_run_time();
      if (chromeTime <= 0) {
        chromeTime = INT32_MAX;
      }
      int32_t extTime = StaticPrefs::dom_max_ext_content_script_run_time();
      if (extTime <= 0) {
        extTime = INT32_MAX;
      }
      mWatchdog->SetMinScriptRunTimeSeconds(
          std::min({contentTime, chromeTime, extTime}));
    }
  }

  void StartWatchdog() {
    MOZ_ASSERT(!mWatchdog);
    mWatchdog = std::make_unique<Watchdog>(this);
    mWatchdog->Init();
  }

  void StopWatchdog() {
    MOZ_ASSERT(mWatchdog);
    mWatchdog->Shutdown();
    mWatchdog = nullptr;
  }

  template <class Callback>
  void ForAllActiveContexts(Callback&& aCallback) {
    // This function must be called on the watchdog thread with the lock held.
    MOZ_ASSERT(!NS_IsMainThread());
    PR_ASSERT_CURRENT_THREAD_OWNS_LOCK(mWatchdog->GetLock());

    for (auto* context = mActiveContexts.getFirst(); context;
         context = context->LinkedListElement<XPCJSContext>::getNext()) {
      if (!aCallback(context)) {
        return;
      }
    }
  }

 private:
  void UpdateContextLists(XPCJSContext* aContext) {
    // Given aContext whose activity state or timestamp has just changed,
    // put it back in the proper position in the proper list.
    aContext->LinkedListElement<XPCJSContext>::remove();
    auto& list = aContext->mActive == XPCJSContext::CONTEXT_ACTIVE
                     ? mActiveContexts
                     : mInactiveContexts;

    // Either the new list is empty or aContext must be more recent than
    // the existing last element.
    MOZ_ASSERT_IF(!list.isEmpty(), list.getLast()->mLastStateChange <
                                       aContext->mLastStateChange);
    list.insertBack(aContext);
  }

  LinkedList<XPCJSContext> mActiveContexts;
  LinkedList<XPCJSContext> mInactiveContexts;
  mozilla::UniquePtr<Watchdog> mWatchdog;

  // We store ContextStateChange on the contexts themselves.
  PRTime mTimestamps[kWatchdogTimestampCategoryCount - 1];
};

AutoLockWatchdog::AutoLockWatchdog(Watchdog* aWatchdog) : mWatchdog(aWatchdog) {
  if (mWatchdog) {
    PR_Lock(mWatchdog->GetLock());
  }
}

AutoLockWatchdog::~AutoLockWatchdog() {
  if (mWatchdog) {
    PR_Unlock(mWatchdog->GetLock());
  }
}

static void WatchdogMain(void* arg) {
  AUTO_PROFILER_REGISTER_THREAD("JS Watchdog");
  // Create an nsThread wrapper for the thread and register it with the thread
  // manager.
  (void)NS_GetCurrentThread();
  NS_SetCurrentThreadName("JS Watchdog");

  Watchdog* self = static_cast<Watchdog*>(arg);
  WatchdogManager* manager = self->Manager();

  // Lock lasts until we return
  AutoLockWatchdog lock(self);

  MOZ_ASSERT(self->Initialized());
  while (!self->ShuttingDown()) {
    // Sleep only 1 second if recently (or currently) active; otherwise,
    // hibernate
    if (manager->IsAnyContextActive() ||
        manager->TimeSinceLastActiveContext() <= PRTime(2 * PR_USEC_PER_SEC)) {
      self->Sleep(PR_TicksPerSecond());
    } else {
      manager->RecordTimestamp(TimestampWatchdogHibernateStart);
      self->Hibernate();
      manager->RecordTimestamp(TimestampWatchdogHibernateStop);
    }

    // Rise and shine.
    manager->RecordTimestamp(TimestampWatchdogWakeup);

    // Don't request an interrupt callback unless the current script has
    // been running long enough that we might show the slow script dialog.
    // Triggering the callback from off the main thread can be expensive.

    // We want to avoid showing the slow script dialog if the user's laptop
    // goes to sleep in the middle of running a script. To ensure this, we
    // invoke the interrupt callback after only half the timeout has
    // elapsed. The callback simply records the fact that it was called in
    // the mSlowScriptSecondHalf flag. Then we wait another (timeout/2)
    // seconds and invoke the callback again. This time around it sees
    // mSlowScriptSecondHalf is set and so it shows the slow script
    // dialog. If the computer is put to sleep during one of the (timeout/2)
    // periods, the script still has the other (timeout/2) seconds to
    // finish.
    if (!self->ShuttingDown() && manager->IsAnyContextActive()) {
      bool debuggerAttached = false;
      nsCOMPtr<nsIDebug2> dbg = do_GetService("@mozilla.org/xpcom/debug;1");
      if (dbg) {
        dbg->GetIsDebuggerAttached(&debuggerAttached);
      }
      if (debuggerAttached) {
        // We won't be interrupting these scripts anyway.
        continue;
      }

      PRTime usecs = self->MinScriptRunTimeSeconds() * PR_USEC_PER_SEC / 2;
      manager->ForAllActiveContexts([usecs, manager,
                                     &lock](XPCJSContext* aContext) -> bool {
        auto timediff = PR_Now() - manager->GetContextTimestamp(aContext, lock);
        if (timediff > usecs) {
          JS_RequestInterruptCallback(aContext->Context());
          return true;
        }
        return false;
      });
    }
  }

  // Tell the manager that we've shut down.
  self->Finished();
}

PRTime XPCJSContext::GetWatchdogTimestamp(WatchdogTimestampCategory aCategory) {
  AutoLockWatchdog lock(mWatchdogManager->GetWatchdog());
  return aCategory == TimestampContextStateChange
             ? mWatchdogManager->GetContextTimestamp(this, lock)
             : mWatchdogManager->GetTimestamp(aCategory, lock);
}

// static
bool XPCJSContext::RecordScriptActivity(bool aActive) {
  MOZ_ASSERT(NS_IsMainThread());

  XPCJSContext* xpccx = XPCJSContext::Get();
  if (!xpccx) {
    // mozilla::SpinEventLoopUntil may use AutoScriptActivity(false) after
    // we destroyed the XPCJSContext.
    MOZ_ASSERT(!aActive);
    return false;
  }

  bool oldValue = xpccx->SetHasScriptActivity(aActive);
  if (aActive == oldValue) {
    // Nothing to do.
    return oldValue;
  }

  if (!aActive) {
    ProcessHangMonitor::ClearHang();
  }
  xpccx->mWatchdogManager->RecordContextActivity(xpccx, aActive);

  return oldValue;
}

AutoScriptActivity::AutoScriptActivity(bool aActive)
    : mActive(aActive),
      mOldValue(XPCJSContext::RecordScriptActivity(aActive)) {}

AutoScriptActivity::~AutoScriptActivity() {
  MOZ_ALWAYS_TRUE(mActive == XPCJSContext::RecordScriptActivity(mOldValue));
}

static const double sChromeSlowScriptTelemetryCutoff(10.0);

// static
bool XPCJSContext::InterruptCallback(JSContext* cx) {
  XPCJSContext* self = XPCJSContext::Get();

  // Now is a good time to turn on profiling if it's pending.
  PROFILER_JS_INTERRUPT_CALLBACK();

  if (profiler_thread_is_being_profiled_for_markers()) {
    nsDependentCString filename("unknown file");
    JS::AutoFilename scriptFilename;
    // Computing the line number can be very expensive (see bug 1330231 for
    // example), so don't request it here.
    if (JS::DescribeScriptedCaller(&scriptFilename, cx)) {
      if (const char* file = scriptFilename.get()) {
        filename.Assign(file, strlen(file));
      }
      PROFILER_MARKER_TEXT("JS::InterruptCallback", JS, {}, filename);
    }
  }

  // Normally we record mSlowScriptCheckpoint when we start to process an
  // event. However, we can run JS outside of event handlers. This code takes
  // care of that case.
  if (self->mSlowScriptCheckpoint.IsNull()) {
    self->mSlowScriptCheckpoint = TimeStamp::NowLoRes();
    self->mSlowScriptSecondHalf = false;
    self->mSlowScriptActualWait = mozilla::TimeDuration();
    self->mTimeoutAccumulated = false;
    self->mExecutedChromeScript = false;
    return true;
  }

  // Sometimes we get called back during XPConnect initialization, before Gecko
  // has finished bootstrapping. Avoid crashing in nsContentUtils below.
  if (!nsContentUtils::IsInitialized()) {
    return true;
  }

  // This is at least the second interrupt callback we've received since
  // returning to the event loop. See how long it's been, and what the limit
  // is.
  TimeStamp now = TimeStamp::NowLoRes();
  TimeDuration duration = now - self->mSlowScriptCheckpoint;
  int32_t limit;

  nsString addonId;
  const char* prefName;
  auto principal = BasePrincipal::Cast(nsContentUtils::SubjectPrincipal(cx));
  bool chrome = principal->Is<SystemPrincipal>();
  if (chrome) {
    prefName = PREF_MAX_SCRIPT_RUN_TIME_CHROME;
    limit = StaticPrefs::dom_max_chrome_script_run_time();
    self->mExecutedChromeScript = true;
  } else if (auto policy = principal->ContentScriptAddonPolicy()) {
    policy->GetId(addonId);
    prefName = PREF_MAX_SCRIPT_RUN_TIME_EXT_CONTENT;
    limit = StaticPrefs::dom_max_ext_content_script_run_time();
  } else {
    prefName = PREF_MAX_SCRIPT_RUN_TIME_CONTENT;
    limit = StaticPrefs::dom_max_script_run_time();
  }

  // When the parent process slow script dialog is disabled, we still want
  // to be able to track things for telemetry, so set `mSlowScriptSecondHalf`
  // to true in that case:
  if (limit == 0 && chrome &&
      duration.ToSeconds() > sChromeSlowScriptTelemetryCutoff / 2.0) {
    self->mSlowScriptSecondHalf = true;
    return true;
  }
  // If there's no limit, or we're within the limit, let it go.
  if (limit == 0 || duration.ToSeconds() < limit / 2.0) {
    return true;
  }

  self->mSlowScriptCheckpoint = now;
  self->mSlowScriptActualWait += duration;

  // In order to guard against time changes or laptops going to sleep, we
  // don't trigger the slow script warning until (limit/2) seconds have
  // elapsed twice.
  if (!self->mSlowScriptSecondHalf) {
    self->mSlowScriptSecondHalf = true;
    return true;
  }

  // For scripts in content processes, we only want to show the slow script
  // dialogue if the user is actually trying to perform an important
  // interaction. In theory this could be a chrome script running in the
  // content process, which we probably don't want to give the user the ability
  // to terminate. However, if this is the case we won't be able to map the
  // script global to a window and we'll bail out below.
  if (XRE_IsContentProcess() &&
      StaticPrefs::dom_max_script_run_time_require_critical_input()) {
    // Call possibly slow PeekMessages after the other common early returns in
    // this method.
    ContentChild* contentChild = ContentChild::GetSingleton();
    mozilla::ipc::MessageChannel* channel =
        contentChild ? contentChild->GetIPCChannel() : nullptr;
    if (channel) {
      bool foundInputEvent = false;
      channel->PeekMessages(
          [&foundInputEvent](const IPC::Message& aMsg) -> bool {
            if (nsContentUtils::IsMessageCriticalInputEvent(aMsg)) {
              foundInputEvent = true;
              return false;
            }
            return true;
          });
      if (!foundInputEvent) {
        return true;
      }
    }
  }

  // We use a fixed value of 2 from browser_parent_process_hang_telemetry.js
  // to check if the telemetry events work. Do not interrupt it with a dialog.
  if (chrome && limit == 2 && xpc::IsInAutomation()) {
    return true;
  }

  //
  // This has gone on long enough! Time to take action. ;-)
  //

  // Get the DOM window associated with the running script. If the script is
  // running in a non-DOM scope, we have to just let it keep running.
  RootedObject global(cx, JS::CurrentGlobalOrNull(cx));
  RefPtr<nsGlobalWindowInner> win = WindowOrNull(global);
  if (!win) {
    // If this is a sandbox associated with a DOMWindow via a
    // sandboxPrototype, use that DOMWindow. This supports WebExtension
    // content scripts.
    win = SandboxWindowOrNull(global, cx);
  }

  if (!win && !chrome) {
    // Explicit associations must not make system-principal scripts stoppable.
    win = SandboxAssociatedWindowOrNull(global);
  }

  if (!win) {
    NS_WARNING("No active window");
    return true;
  }

  if (win->IsDying()) {
    // The window is being torn down. When that happens we try to prevent
    // the dispatch of new runnables, so it also makes sense to kill any
    // long-running script. The user is primarily interested in this page
    // going away.
    return false;
  }

  // Accumulate slow script invokation delay.
  if (!chrome && !self->mTimeoutAccumulated) {
    TimeDuration delay =
        self->mSlowScriptActualWait - TimeDuration::FromSeconds(limit);
    glean::slow_script_warning::notify_delay.AccumulateRawDuration(delay);
    self->mTimeoutAccumulated = true;
  }

  // Show the prompt to the user, and kill if requested.
  nsGlobalWindowInner::SlowScriptResponse response = win->ShowSlowScriptDialog(
      cx, addonId, self->mSlowScriptActualWait.ToMilliseconds());
  if (response == nsGlobalWindowInner::KillSlowScript) {
    if (Preferences::GetBool("dom.global_stop_script", true)) {
      xpc::Scriptability::Get(global).Block();
    }
    if (nsCOMPtr<Document> doc = win->GetExtantDoc()) {
      doc->UnlockAllWakeLocks(WakeLockType::Screen);
    }
    return false;
  }

  // The user chose to continue the script. Reset the timer, and disable this
  // machinery with a pref if the user opted out of future slow-script dialogs.
  if (response != nsGlobalWindowInner::ContinueSlowScriptAndKeepNotifying) {
    self->mSlowScriptCheckpoint = TimeStamp::NowLoRes();
  }

  if (response == nsGlobalWindowInner::AlwaysContinueSlowScript) {
    Preferences::SetInt(prefName, 0);
  }

  return true;
}

#define JS_OPTIONS_DOT_STR "javascript.options."

static mozilla::Atomic<bool> sDiscardSystemSource(false);

bool xpc::ShouldDiscardSystemSource() { return sDiscardSystemSource; }

static mozilla::Atomic<bool> sSharedMemoryEnabled(false);
static mozilla::Atomic<bool> sStreamsEnabled(false);

void xpc::SetPrefableRealmOptions(JS::RealmOptions& options) {
  options.creationOptions()
      .setSharedMemoryAndAtomicsEnabled(sSharedMemoryEnabled)
      .setCoopAndCoepEnabled(
          StaticPrefs::browser_tabs_remote_useCrossOriginOpenerPolicy() &&
          StaticPrefs::browser_tabs_remote_useCrossOriginEmbedderPolicy());
}

void xpc::SetPrefableCompileOptions(JS::PrefableCompileOptions& options) {
  options.setSourcePragmas(StaticPrefs::javascript_options_source_pragmas())
      .setSourcePhaseImports(
          StaticPrefs::javascript_options_experimental_source_phase_imports())
      .setDeferImportEval(
#ifdef NIGHTLY_BUILD
          StaticPrefs::javascript_options_experimental_defer_import_eval()
#else
          false
#endif
      );
}

void xpc::SetPrefableContextOptions(JS::ContextOptions& options) {
  options
#ifdef FUZZING
      .setFuzzing(Preferences::GetBool(JS_OPTIONS_DOT_STR "fuzzing.enabled"))
#endif
      .setWasm(Preferences::GetBool(JS_OPTIONS_DOT_STR "wasm"))
      .setWasmForTrustedPrinciples(
          Preferences::GetBool(JS_OPTIONS_DOT_STR "wasm_trustedprincipals"))
      .setWasmIon(Preferences::GetBool(JS_OPTIONS_DOT_STR "wasm_optimizingjit"))
      .setWasmBaseline(
          Preferences::GetBool(JS_OPTIONS_DOT_STR "wasm_baselinejit"))
      .setAsyncStack(Preferences::GetBool(JS_OPTIONS_DOT_STR "asyncstack"))
      .setAsyncStackCaptureDebuggeeOnly(Preferences::GetBool(
          JS_OPTIONS_DOT_STR "asyncstack_capture_debuggee_only"));

  SetPrefableCompileOptions(options.compileOptions());
}

static void LoadStartupJSPrefs(XPCJSContext* xpccx) {
  // Prefs that require a restart are handled here. This includes the
  // process-wide JIT options because toggling these at runtime can easily cause
  // races or get us into an inconsistent state.
  //
  // 'Live' prefs are handled by ReloadPrefsCallback below.

  // Note: JS::Prefs are set earlier in startup, in InitJSEngine in
  // nsXPConnect.cpp.

  JSContext* cx = xpccx->Context();

  // Some prefs are unlisted in all.js / StaticPrefs (and thus are invisible in
  // about:config). Make sure we use explicit defaults here.
  bool useJitForTrustedPrincipals =
      Preferences::GetBool(JS_OPTIONS_DOT_STR "jit_trustedprincipals", false);

  bool safeMode = false;
  nsCOMPtr<nsIXULRuntime> xr = do_GetService("@mozilla.org/xre/runtime;1");
  if (xr) {
    xr->GetInSafeMode(&safeMode);
  }

  // NOTE: Baseline Interpreter is still used in safe-mode. This gives a big
  //       perf gain and is our simplest JIT so we make a tradeoff.
  JS_SetGlobalJitCompilerOption(
      cx, JSJITCOMPILER_BASELINE_INTERPRETER_ENABLE,
      StaticPrefs::javascript_options_blinterp_DoNotUseDirectly());

  // Disable most JITs in Safe-Mode.
  if (safeMode) {
    JS_SetGlobalJitCompilerOption(cx, JSJITCOMPILER_BASELINE_ENABLE, false);
    JS_SetGlobalJitCompilerOption(cx, JSJITCOMPILER_ION_ENABLE, false);
    JS_SetGlobalJitCompilerOption(
        cx, JSJITCOMPILER_JIT_TRUSTEDPRINCIPALS_ENABLE, false);
    JS_SetGlobalJitCompilerOption(cx, JSJITCOMPILER_NATIVE_REGEXP_ENABLE,
                                  false);
    JS_SetGlobalJitCompilerOption(cx, JSJITCOMPILER_JIT_HINTS_ENABLE, false);
    xpc::SelfHostedShmem::SetSelfHostedUseSharedMemory(false);
  } else {
    JS_SetGlobalJitCompilerOption(
        cx, JSJITCOMPILER_BASELINE_ENABLE,
        StaticPrefs::javascript_options_baselinejit_DoNotUseDirectly());
    JS_SetGlobalJitCompilerOption(
        cx, JSJITCOMPILER_ION_ENABLE,
        StaticPrefs::javascript_options_ion_DoNotUseDirectly());
    JS_SetGlobalJitCompilerOption(cx,
                                  JSJITCOMPILER_JIT_TRUSTEDPRINCIPALS_ENABLE,
                                  useJitForTrustedPrincipals);
    JS_SetGlobalJitCompilerOption(
        cx, JSJITCOMPILER_NATIVE_REGEXP_ENABLE,
        StaticPrefs::javascript_options_native_regexp_DoNotUseDirectly());
    // Only enable the jit hints cache for the content process to avoid
    // any possible jank or delays on the parent process.
    JS_SetGlobalJitCompilerOption(
        cx, JSJITCOMPILER_JIT_HINTS_ENABLE,
        XRE_IsContentProcess()
            ? StaticPrefs::javascript_options_jithints_DoNotUseDirectly()
            : false);
    xpc::SelfHostedShmem::SetSelfHostedUseSharedMemory(
        StaticPrefs::
            javascript_options_self_hosted_use_shared_memory_DoNotUseDirectly());
  }

  uint32_t strategyIndex = StaticPrefs::
      javascript_options_baselinejit_offthread_compilation_strategy();
  bool onDemandOMTBaselineEnabled = strategyIndex == 1 || strategyIndex == 3;
  JS_SetOffthreadBaselineCompilationEnabled(cx, onDemandOMTBaselineEnabled);

  JS_SetOffthreadIonCompilationEnabled(
      cx, StaticPrefs::
              javascript_options_ion_offthread_compilation_DoNotUseDirectly());

  JS_SetGlobalJitCompilerOption(
      cx, JSJITCOMPILER_BASELINE_INTERPRETER_WARMUP_TRIGGER,
      StaticPrefs::javascript_options_blinterp_threshold_DoNotUseDirectly());
  JS_SetGlobalJitCompilerOption(
      cx, JSJITCOMPILER_BASELINE_WARMUP_TRIGGER,
      StaticPrefs::javascript_options_baselinejit_threshold_DoNotUseDirectly());
  JS_SetGlobalJitCompilerOption(
      cx, JSJITCOMPILER_ION_NORMAL_WARMUP_TRIGGER,
      StaticPrefs::javascript_options_ion_threshold_DoNotUseDirectly());
  JS_SetGlobalJitCompilerOption(
      cx, JSJITCOMPILER_ION_FREQUENT_BAILOUT_THRESHOLD,
      StaticPrefs::
          javascript_options_ion_frequent_bailout_threshold_DoNotUseDirectly());
  JS_SetGlobalJitCompilerOption(
      cx, JSJITCOMPILER_INLINING_BYTECODE_MAX_LENGTH,
      StaticPrefs::
          javascript_options_inlining_bytecode_max_length_DoNotUseDirectly());

#ifdef DEBUG
  JS_SetGlobalJitCompilerOption(
      cx, JSJITCOMPILER_FULL_DEBUG_CHECKS,
      StaticPrefs::javascript_options_jit_full_debug_checks_DoNotUseDirectly());
#endif

#if !defined(JS_CODEGEN_MIPS64) && !defined(JS_CODEGEN_RISCV64) && \
    !defined(JS_CODEGEN_LOONG64)
  JS_SetGlobalJitCompilerOption(
      cx, JSJITCOMPILER_SPECTRE_INDEX_MASKING,
      StaticPrefs::javascript_options_spectre_index_masking_DoNotUseDirectly());
  JS_SetGlobalJitCompilerOption(
      cx, JSJITCOMPILER_SPECTRE_OBJECT_MITIGATIONS,
      StaticPrefs::
          javascript_options_spectre_object_mitigations_DoNotUseDirectly());
  JS_SetGlobalJitCompilerOption(
      cx, JSJITCOMPILER_SPECTRE_STRING_MITIGATIONS,
      StaticPrefs::
          javascript_options_spectre_string_mitigations_DoNotUseDirectly());
  JS_SetGlobalJitCompilerOption(
      cx, JSJITCOMPILER_SPECTRE_VALUE_MASKING,
      StaticPrefs::javascript_options_spectre_value_masking_DoNotUseDirectly());
  JS_SetGlobalJitCompilerOption(
      cx, JSJITCOMPILER_SPECTRE_JIT_TO_CXX_CALLS,
      StaticPrefs::
          javascript_options_spectre_jit_to_cxx_calls_DoNotUseDirectly());
#endif

  bool writeProtectCode = true;
  if (XRE_IsContentProcess()) {
    writeProtectCode =
        StaticPrefs::javascript_options_content_process_write_protect_code();
  }
  JS_SetGlobalJitCompilerOption(cx, JSJITCOMPILER_WRITE_PROTECT_CODE,
                                writeProtectCode);
}

static void ReloadPrefsCallback(const char* pref, void* aXpccx) {
  // Note: Prefs that require a restart are handled in LoadStartupJSPrefs above.

  // Update all non-startup JS::Prefs.
  SET_NON_STARTUP_JS_PREFS_FROM_BROWSER_PREFS;

  auto xpccx = static_cast<XPCJSContext*>(aXpccx);
  JSContext* cx = xpccx->Context();

  sDiscardSystemSource =
      Preferences::GetBool(JS_OPTIONS_DOT_STR "discardSystemSource");
  sSharedMemoryEnabled =
      Preferences::GetBool(JS_OPTIONS_DOT_STR "shared_memory");
  sStreamsEnabled = Preferences::GetBool(JS_OPTIONS_DOT_STR "streams");

#ifdef JS_GC_ZEAL
  int32_t zeal = Preferences::GetInt(JS_OPTIONS_DOT_STR "mem.gc_zeal.mode", -1);
  int32_t zeal_frequency =
      Preferences::GetInt(JS_OPTIONS_DOT_STR "mem.gc_zeal.frequency",
                          JS::BrowserDefaultGCZealFrequency);
  if (zeal >= 0) {
    JS::SetGCZeal(cx, (uint8_t)zeal, zeal_frequency);
  }
#endif  // JS_GC_ZEAL

  auto& contextOptions = JS::ContextOptionsRef(cx);
  SetPrefableContextOptions(contextOptions);

  // Set options not shared with workers.
  contextOptions
      .setThrowOnDebuggeeWouldRun(Preferences::GetBool(
          JS_OPTIONS_DOT_STR "throw_on_debuggee_would_run"))
      .setDumpStackOnDebuggeeWouldRun(Preferences::GetBool(
          JS_OPTIONS_DOT_STR "dump_stack_on_debuggee_would_run"));

  nsCOMPtr<nsIXULRuntime> xr = do_GetService("@mozilla.org/xre/runtime;1");
  if (xr) {
    bool safeMode = false;
    xr->GetInSafeMode(&safeMode);
    if (safeMode) {
      contextOptions.disableOptionsForSafeMode();
    }
  }

  // Set up the callback for DOM event dispatch
  if (StaticPrefs::dom_expose_test_interfaces()) {
    JS::SetDispatchDOMEventCallback(cx, DispatchJitEventToDOM);
  } else {
    JS::SetDispatchDOMEventCallback(cx, nullptr);
  }
}

XPCJSContext::~XPCJSContext() {
  MOZ_COUNT_DTOR_INHERITED(XPCJSContext, CycleCollectedJSContext);
  // Elsewhere we abort immediately if XPCJSContext initialization fails.
  // Therefore the context must be non-null.
  MOZ_ASSERT(MaybeContext());

  Preferences::UnregisterPrefixCallback(ReloadPrefsCallback, JS_OPTIONS_DOT_STR,
                                        this);

#ifdef FUZZING
  Preferences::UnregisterCallback(ReloadPrefsCallback, "fuzzing.enabled", this);
#endif

  // Clear any pending exception.  It might be an XPCWrappedJS, and if we try
  // to destroy it later we will crash.
  SetPendingException(nullptr);

  // If we're the last XPCJSContext around, clean up the watchdog manager.
  if (--sInstanceCount == 0) {
    if (mWatchdogManager->GetWatchdog()) {
      mWatchdogManager->StopWatchdog();
    }

    mWatchdogManager->UnregisterContext(this);
    mWatchdogManager->Shutdown();
    sWatchdogInstance = nullptr;
  } else {
    // Otherwise, simply remove ourselves from the list.
    mWatchdogManager->UnregisterContext(this);
  }

  if (mCallContext) {
    mCallContext->SystemIsBeingShutDown();
  }

  PROFILER_CLEAR_JS_CONTEXT();
}

XPCJSContext::XPCJSContext()
    : mCallContext(nullptr),
      mAutoRoots(nullptr),
      mResolveName(JS::PropertyKey::Void()),
      mResolvingWrapper(nullptr),
      mWatchdogManager(GetWatchdogManager()),
      mSlowScriptSecondHalf(false),
      mTimeoutAccumulated(false),
      mExecutedChromeScript(false),
      mHasScriptActivity(false),
      mPendingResult(NS_OK),
      mActive(CONTEXT_INACTIVE),
      mLastStateChange(PR_Now()) {
  MOZ_COUNT_CTOR_INHERITED(XPCJSContext, CycleCollectedJSContext);
  MOZ_ASSERT(mWatchdogManager);
  ++sInstanceCount;
  mWatchdogManager->RegisterContext(this);
}

/* static */
XPCJSContext* XPCJSContext::Get() {
  // Do an explicit null check, because this can get called from a process that
  // does not run JS.
  nsXPConnect* xpc = static_cast<nsXPConnect*>(nsXPConnect::XPConnect());
  return xpc ? xpc->GetContext() : nullptr;
}

#ifdef XP_WIN
static size_t GetWindowsStackSize() {
  // First, get the stack base. Because the stack grows down, this is the top
  // of the stack.
  const uint8_t* stackTop;
#  ifdef _WIN64
  PNT_TIB64 pTib = reinterpret_cast<PNT_TIB64>(NtCurrentTeb());
  stackTop = reinterpret_cast<const uint8_t*>(pTib->StackBase);
#  else
  PNT_TIB pTib = reinterpret_cast<PNT_TIB>(NtCurrentTeb());
  stackTop = reinterpret_cast<const uint8_t*>(pTib->StackBase);
#  endif

  // Now determine the stack bottom. Note that we can't use tib->StackLimit,
  // because that's the size of the committed area and we're also interested
  // in the reserved pages below that.
  MEMORY_BASIC_INFORMATION mbi;
  if (!VirtualQuery(&mbi, &mbi, sizeof(mbi))) {
    MOZ_CRASH("VirtualQuery failed");
  }

  const uint8_t* stackBottom =
      reinterpret_cast<const uint8_t*>(mbi.AllocationBase);

  // Do some sanity checks.
  size_t stackSize = size_t(stackTop - stackBottom);
  MOZ_RELEASE_ASSERT(stackSize >= 1 * 1024 * 1024);
  MOZ_RELEASE_ASSERT(stackSize <= 32 * 1024 * 1024);

  // Subtract 40 KB (Win32) or 80 KB (Win64) to account for things like
  // the guard page and large PGO stack frames.
  return stackSize - 10 * sizeof(uintptr_t) * 1024;
}
#endif

XPCJSRuntime* XPCJSContext::Runtime() const {
  return static_cast<XPCJSRuntime*>(CycleCollectedJSContext::Runtime());
}

CycleCollectedJSRuntime* XPCJSContext::CreateRuntime(JSContext* aCx) {
  return new XPCJSRuntime(aCx);
}

class HelperThreadTaskHandler : public Task {
  JS::HelperThreadTask* mTask;

 public:
  explicit HelperThreadTaskHandler(JS::HelperThreadTask* aTask)
      : Task(Kind::OffMainThreadOnly, EventQueuePriority::Normal),
        mTask(aTask) {
    // Bug 1703185: Currently all tasks are run at the same priority.
  }

  TaskResult Run() override {
    JS::RunHelperThreadTask(mTask);
    return TaskResult::Complete;
  }

#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
  bool GetName(nsACString& aName) override {
    const char* taskName = JS::GetHelperThreadTaskName(mTask);
    aName.AssignLiteral(taskName, strlen(taskName));
    return true;
  }
#endif

 private:
  ~HelperThreadTaskHandler() = default;
};

static void DispatchOffThreadTask(JS::HelperThreadTask* aTask) {
  TaskController::Get()->AddTask(MakeAndAddRef<HelperThreadTaskHandler>(aTask));
}

// Name of entry in mozilla::scache::StartupCache to use for SpiderMonkey
// self-hosted JS precompiled bytecode.
static constexpr char kSelfHostCacheKey[] = "js.self-hosted";

static bool CreateSelfHostedSharedMemory(JSContext* aCx,
                                         JS::SelfHostedCache aBuf) {
  // Record the data to the "StartupCache" for future restarts to use to
  // initialize the shmem with.
  if (auto* sc = scache::StartupCache::GetSingleton()) {
    UniqueFreePtr<char[]> copy(static_cast<char*>(malloc(aBuf.LengthBytes())));
    if (copy) {
      memcpy(copy.get(), aBuf.Elements(), aBuf.LengthBytes());
      sc->PutBuffer(kSelfHostCacheKey, std::move(copy), aBuf.LengthBytes());
    }
  }

  auto& shm = xpc::SelfHostedShmem::GetSingleton();
  MOZ_RELEASE_ASSERT(shm.Content().IsEmpty());
  // Failures within InitFromParent output warnings but do not cause
  // unrecoverable failures.
  shm.InitFromParent(aBuf);
  return true;
}

static JS::OpaqueLogger GetLoggerByName(const char* name) {
  LogModule* tmp = LogModule::Get(name);
  return static_cast<JS::OpaqueLogger>(tmp);
}

MOZ_FORMAT_PRINTF(3, 0)
static void LogPrintVA(JS::OpaqueLogger aLogger, mozilla::LogLevel level,
                       const char* aFmt, va_list ap) {
  LogModule* logmod = static_cast<LogModule*>(aLogger);

  logmod->Printv(level, aFmt, ap);
}

static void LogPrintFMT(JS::OpaqueLogger aLogger, mozilla::LogLevel level,
                        fmt::string_view fmt, fmt::format_args args) {
  LogModule* logmod = static_cast<LogModule*>(aLogger);

  logmod->PrintvFmt(level, fmt, args);
}

static AtomicLogLevel& GetLevelRef(JS::OpaqueLogger aLogger) {
  LogModule* logmod = static_cast<LogModule*>(aLogger);
  return logmod->LevelRef();
}

static JS::LoggingInterface loggingInterface = {GetLoggerByName, LogPrintVA,
                                                LogPrintFMT, GetLevelRef};

nsresult XPCJSContext::Initialize() {
  if (StaticPrefs::javascript_options_external_thread_pool_DoNotUseDirectly()) {
    size_t threadCount = TaskController::GetPoolThreadCount();
    size_t stackSize = TaskController::GetThreadStackSize();
    SetHelperThreadTaskCallback(&DispatchOffThreadTask, threadCount, stackSize);
  }

  if (!JS::SetLoggingInterface(loggingInterface)) {
    MOZ_CRASH("Failed to install logging interface");
  }

  nsresult rv =
      CycleCollectedJSContext::Initialize(nullptr, JS::DefaultHeapMaxBytes);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  MOZ_ASSERT(Context());
  JSContext* cx = Context();

  // The JS engine permits us to set different stack limits for system code,
  // trusted script, and untrusted script. We have tests that ensure that
  // we can always execute 10 "heavy" (eval+with) stack frames deeper in
  // privileged code. Our stack sizes vary greatly in different configurations,
  // so satisfying those tests requires some care. Manual measurements of the
  // number of heavy stack frames achievable gives us the following rough data,
  // ordered by the effective categories in which they are grouped in the
  // JS_SetNativeStackQuota call (which predates this analysis).
  //
  // The following "Stack Frames" numbers come from `chromeLimit` in
  // js/xpconnect/tests/chrome/test_bug732665.xul
  //
  //  Platform   | Build | Stack Quota | Stack Frames | Stack Frame Size
  // ------------+-------+-------------+--------------+------------------
  //  OSX 64     | Opt   | 7MB         | 1331         | ~5.4k
  //  OSX 64     | Debug | 7MB         | 1202         | ~6.0k
  // ------------+-------+-------------+--------------+------------------
  //  Linux 32   | Opt   | 7.875MB     | 2513         | ~3.2k
  //  Linux 32   | Debug | 7.875MB     | 2146         | ~3.8k
  // ------------+-------+-------------+--------------+------------------
  //  Linux 64   | Opt   | 7.875MB     | 1360         | ~5.9k
  //  Linux 64   | Debug | 7.875MB     | 1180         | ~6.8k
  //  Linux 64   | ASan  | 7.875MB     | 473          | ~17.0k
  // ------------+-------+-------------+--------------+------------------
  //  Windows 32 | Opt   | 984k        | 188          | ~5.2k
  //  Windows 32 | Debug | 984k        | 208          | ~4.7k
  // ------------+-------+-------------+--------------+------------------
  //  Windows 64 | Opt   | 1.922MB     | 189          | ~10.4k
  //  Windows 64 | Debug | 1.922MB     | 175          | ~11.2k
  //
  // We tune the trusted/untrusted quotas for each configuration to achieve our
  // invariants while attempting to minimize overhead. In contrast, our buffer
  // between system code and trusted script is a very unscientific 10k.
  const size_t kSystemCodeBuffer = 10 * 1024;

  // Our "default" stack is what we use in configurations where we don't have
  // a compelling reason to do things differently. This is effectively 512KB
  // on 32-bit platforms and 1MB on 64-bit platforms.
  const size_t kDefaultStackQuota = 128 * sizeof(size_t) * 1024;

  // Set maximum stack size for different configurations. This value is then
  // capped below because huge stacks are not web-compatible.

#if defined(XP_MACOSX) || defined(DARWIN)
  // MacOS has a gargantuan default stack size of 8MB. Go wild with 7MB,
  // and give trusted script 180k extra. The stack is huge on mac anyway.
  const size_t kUncappedStackQuota = 7 * 1024 * 1024;
  const size_t kTrustedScriptBuffer = 180 * 1024;
#elif defined(XP_LINUX) && !defined(ANDROID)
  // Most Linux distributions set default stack size to 8MB.  Use it as the
  // maximum value.
  const size_t kStackQuotaMax = 8 * 1024 * 1024;
#  if defined(MOZ_ASAN) || defined(DEBUG)
  // Bug 803182: account for the 4x difference in the size of js::Interpret
  // between optimized and debug builds.  We use 2x since the JIT part
  // doesn't increase much.
  // See the standalone MOZ_ASAN branch below for the ASan case.
  const size_t kStackQuotaMin = 2 * kDefaultStackQuota;
#  else
  const size_t kStackQuotaMin = kDefaultStackQuota;
#  endif
  // Allocate 128kB margin for the safe space.
  const size_t kStackSafeMargin = 128 * 1024;

  struct rlimit rlim;
  const size_t kUncappedStackQuota =
      getrlimit(RLIMIT_STACK, &rlim) == 0
          ? std::clamp(size_t(rlim.rlim_cur - kStackSafeMargin), kStackQuotaMin,
                       kStackQuotaMax - kStackSafeMargin)
          : kStackQuotaMin;
#  if defined(MOZ_ASAN)
  // See the standalone MOZ_ASAN branch below for the ASan case.
  const size_t kTrustedScriptBuffer = 450 * 1024;
#  else
  const size_t kTrustedScriptBuffer = 180 * 1024;
#  endif
#elif defined(XP_WIN)
  // 1MB is the default stack size on Windows. We use the -STACK linker flag
  // (see WIN32_EXE_DEFAULT_LDFLAGS in build/moz.configure/windows.configure) to
  // request a larger stack, so we determine the stack size at runtime.
  const size_t kUncappedStackQuota = GetWindowsStackSize();
#  if defined(MOZ_ASAN)
  // See the standalone MOZ_ASAN branch below for the ASan case.
  const size_t kTrustedScriptBuffer = 450 * 1024;
#  else
  const size_t kTrustedScriptBuffer = (sizeof(size_t) == 8)
                                          ? 180 * 1024   // win64
                                          : 120 * 1024;  // win32
#  endif
#elif defined(MOZ_ASAN)
  // ASan requires more stack space due to red-zones, so give it double the
  // default (1MB on 32-bit, 2MB on 64-bit). ASAN stack frame measurements
  // were not taken at the time of this writing, so we hazard a guess that
  // ASAN builds have roughly thrice the stack overhead as normal builds.
  // On normal builds, the largest stack frame size we might encounter is
  // 9.0k (see above), so let's use a buffer of 9.0 * 5 * 10 = 450k.
  //
  // FIXME: Does this branch make sense for Windows and Android?
  // (See bug 1415195)
  const size_t kUncappedStackQuota = 2 * kDefaultStackQuota;
  const size_t kTrustedScriptBuffer = 450 * 1024;
#elif defined(ANDROID)
  // Android appears to have 1MB stacks. Allow the use of 3/4 of that size
  // (768KB on 32-bit), since otherwise we can crash with a stack overflow
  // when nearing the 1MB limit.
  const size_t kUncappedStackQuota =
      kDefaultStackQuota + kDefaultStackQuota / 2;
  const size_t kTrustedScriptBuffer = sizeof(size_t) * 12800;
#else
  // Catch-all configuration for other environments.
#  if defined(DEBUG)
  const size_t kUncappedStackQuota = 2 * kDefaultStackQuota;
#  else
  const size_t kUncappedStackQuota = kDefaultStackQuota;
#  endif
  // Given the numbers above, we use 50k and 100k trusted buffers on 32-bit
  // and 64-bit respectively.
  const size_t kTrustedScriptBuffer = sizeof(size_t) * 12800;
#endif

  // Avoid an unused variable warning on platforms where we don't use the
  // default.
  (void)kDefaultStackQuota;

  // Large stacks are not web-compatible so cap to a smaller value.
  // See bug 1537609 and bug 1562700.
  const size_t kStackQuotaCap =
      StaticPrefs::javascript_options_main_thread_stack_quota_cap();
  const size_t kStackQuota = std::min(kUncappedStackQuota, kStackQuotaCap);

  JS_SetNativeStackQuota(
      cx, kStackQuota, kStackQuota - kSystemCodeBuffer,
      kStackQuota - kSystemCodeBuffer - kTrustedScriptBuffer);

  PROFILER_SET_JS_CONTEXT(this);

  JS_AddInterruptCallback(cx, InterruptCallback);

  Runtime()->Initialize(cx);

  LoadStartupJSPrefs(this);

  // Watch for the JS boolean options.
  ReloadPrefsCallback(nullptr, this);
  Preferences::RegisterPrefixCallback(ReloadPrefsCallback, JS_OPTIONS_DOT_STR,
                                      this);

#ifdef FUZZING
  Preferences::RegisterCallback(ReloadPrefsCallback, "fuzzing.enabled", this);
#endif

  // Initialize the MIME type used for the bytecode cache, after calling
  // SetProcessBuildIdOp and loading JS prefs.
  if (!nsContentUtils::InitJSBytecodeMimeType()) {
    NS_ABORT_OOM(0);  // Size is unknown.
  }

  // The self-hosted bytecode can be shared with child processes and also stored
  // in startupcache. Only the parent process may initialize the data.
  auto& shm = xpc::SelfHostedShmem::GetSingleton();
  JS::SelfHostedWriter writer = nullptr;
  if (XRE_IsParentProcess() &&
      xpc::SelfHostedShmem::SelfHostedUseSharedMemory()) {
    // Check the startup cache for a copy of the bytecode.
    if (auto* sc = scache::StartupCache::GetSingleton()) {
      const char* buf = nullptr;
      uint32_t len = 0;
      if (NS_SUCCEEDED(sc->GetBuffer(kSelfHostCacheKey, &buf, &len))) {
        shm.InitFromParent(AsBytes(mozilla::Span(buf, len)));
      }
    }

    // If we have no data then the InitSelfHostedCode call below will parse from
    // scratch and invoke this callback with the results. That callback data can
    // then be used in initialize cache and SelfHostedShmem.
    if (shm.Content().IsEmpty()) {
      writer = CreateSelfHostedSharedMemory;
    }
  }

  if (!JS::InitSelfHostedCode(cx, shm.Content(), writer)) {
    // Note: If no exception is pending, failure is due to OOM.
    if (!JS_IsExceptionPending(cx) || JS_IsThrowingOutOfMemory(cx)) {
      NS_ABORT_OOM(0);  // Size is unknown.
    }

    // Failed to execute self-hosted JavaScript! Uh oh.
    MOZ_CRASH("InitSelfHostedCode failed");
  }

#ifdef MOZ_EXECUTION_TRACING
  JS_SetCustomObjectSummaryCallback(cx, ExecutionTracerIntegration::Callback);
#endif

  MOZ_RELEASE_ASSERT(Runtime()->InitializeStrings(cx),
                     "InitializeStrings failed");

  return NS_OK;
}

// static
uint32_t XPCJSContext::sInstanceCount;

// static
StaticAutoPtr<WatchdogManager> XPCJSContext::sWatchdogInstance;

// static
WatchdogManager* XPCJSContext::GetWatchdogManager() {
  if (sWatchdogInstance) {
    return sWatchdogInstance;
  }

  MOZ_ASSERT(sInstanceCount == 0);
  sWatchdogInstance = new WatchdogManager();
  return sWatchdogInstance;
}

// static
XPCJSContext* XPCJSContext::NewXPCJSContext() {
  XPCJSContext* self = new XPCJSContext();
  nsresult rv = self->Initialize();
  if (rv == NS_ERROR_OUT_OF_MEMORY) {
    mozalloc_handle_oom(0);
  } else if (NS_FAILED(rv)) {
    MOZ_CRASH("new XPCJSContext failed to initialize.");
  }

  if (self->Context()) {
    return self;
  }

  MOZ_CRASH("new XPCJSContext failed to initialize.");
}

void XPCJSContext::BeforeProcessTask(bool aMightBlock) {
  MOZ_ASSERT(NS_IsMainThread());

  // Start the slow script timer.
  mSlowScriptCheckpoint = mozilla::TimeStamp::NowLoRes();
  mSlowScriptSecondHalf = false;
  mSlowScriptActualWait = mozilla::TimeDuration();
  mTimeoutAccumulated = false;
  mExecutedChromeScript = false;
  CycleCollectedJSContext::BeforeProcessTask(aMightBlock);
}

void XPCJSContext::AfterProcessTask(uint32_t aNewRecursionDepth) {
  // Record hangs in the parent process for telemetry.
  if (mSlowScriptSecondHalf && XRE_IsE10sParentProcess()) {
    double hangDuration = (mozilla::TimeStamp::NowLoRes() -
                           mSlowScriptCheckpoint + mSlowScriptActualWait)
                              .ToSeconds();
    // We use the pref to test this code.
    double limit = sChromeSlowScriptTelemetryCutoff;
    if (xpc::IsInAutomation()) {
      double prefLimit = StaticPrefs::dom_max_chrome_script_run_time();
      if (prefLimit > 0) {
        limit = std::min(prefLimit, sChromeSlowScriptTelemetryCutoff);
      }
    }
    if (hangDuration > limit) {
      // Use AppendFloat to avoid printf-type APIs using locale-specific
      // decimal separators, when we definitely want a `.`.
      nsCString durationStr;
      durationStr.AppendFloat(hangDuration);

      glean::slow_script_warning::ShownBrowserExtra extra = {
          .hangDuration = Some(durationStr),
          .uriType = Some(mExecutedChromeScript ? "browser"_ns : "content"_ns),
      };
      glean::slow_script_warning::shown_browser.Record(Some(extra));
    }
  }

  // Now that we're back to the event loop, reset the slow script checkpoint.
  mSlowScriptCheckpoint = mozilla::TimeStamp();
  mSlowScriptSecondHalf = false;

  // Call cycle collector occasionally.
  MOZ_ASSERT(NS_IsMainThread());
  nsJSContext::MaybePokeCC();
  CycleCollectedJSContext::AfterProcessTask(aNewRecursionDepth);

  // Poke the memory telemetry reporter
  if (AppShutdown::GetCurrentShutdownPhase() == ShutdownPhase::NotInShutdown) {
    RefPtr<MemoryTelemetry> telemetry = MemoryTelemetry::Get();
    if (telemetry) {
      telemetry->Poke();
    }
  }

  // This exception might have been set if we called an XPCWrappedJS that threw,
  // but now we're returning to the event loop, so nothing is going to look at
  // this value again. Clear it to prevent leaks.
  SetPendingException(nullptr);
}

void XPCJSContext::MaybePokeGC() { nsJSContext::MaybePokeGC(); }

bool XPCJSContext::IsSystemCaller() const {
  return nsContentUtils::IsSystemCaller(Context());
}
