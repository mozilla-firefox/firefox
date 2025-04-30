/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_workers_workerprivate_h__
#define mozilla_dom_workers_workerprivate_h__

#include <bitset>
#include "MainThreadUtils.h"
#include "ScriptLoader.h"
#include "js/ContextOptions.h"
#include "mozilla/Atomics.h"
#include "mozilla/Attributes.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/CondVar.h"
#include "mozilla/DOMEventTargetHelper.h"
#include "mozilla/Maybe.h"
#include "mozilla/MozPromise.h"
#include "mozilla/OriginTrials.h"
#include "mozilla/RelativeTimeline.h"
#include "mozilla/Result.h"
#include "mozilla/StorageAccess.h"
#include "mozilla/ThreadBound.h"
#include "mozilla/ThreadSafeWeakPtr.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/UseCounter.h"
#include "mozilla/dom/ClientSource.h"
#include "mozilla/dom/FlippedOnce.h"
#include "mozilla/dom/PRemoteWorkerNonLifeCycleOpControllerChild.h"
#include "mozilla/dom/RemoteWorkerTypes.h"
#include "mozilla/dom/Timeout.h"
#include "mozilla/dom/quota/CheckedUnsafePtr.h"
#include "mozilla/dom/Worker.h"
#include "mozilla/dom/WorkerBinding.h"
#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/dom/WorkerLoadInfo.h"
#include "mozilla/dom/WorkerStatus.h"
#include "mozilla/dom/workerinternals/JSSettings.h"
#include "mozilla/dom/workerinternals/Queue.h"
#include "mozilla/dom/JSExecutionManager.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/net/NeckoChannelParams.h"
#include "mozilla/StaticPrefs_extensions.h"
#include "nsContentUtils.h"
#include "nsIChannel.h"
#include "nsIContentPolicy.h"
#include "nsID.h"
#include "nsIEventTarget.h"
#include "nsILoadInfo.h"
#include "nsRFPService.h"
#include "nsTObserverArray.h"
#include "stdint.h"

class nsIContentSecurityPolicy;
class nsIThreadInternal;

namespace JS {
struct RuntimeStats;
}

namespace mozilla {
class ThrottledEventQueue;
namespace dom {

class PRemoteWorkerDebuggerChild;
class PRemoteWorkerDebuggerParent;
class RemoteWorkerChild;
class RemoteWorkerDebuggerChild;
class RemoteWorkerNonLifeCycleOpControllerChild;

// If you change this, the corresponding list in nsIWorkerDebugger.idl needs
// to be updated too. And histograms enum for worker use counters uses the same
// order of worker kind. Please also update dom/base/usecounters.py.
enum WorkerKind : uint8_t {
  WorkerKindDedicated,
  WorkerKindShared,
  WorkerKindService
};

class ClientInfo;
class ClientSource;
class Function;
class JSExecutionManager;
class MessagePort;
class UniqueMessagePortId;
class PerformanceStorage;
class StrongWorkerRef;
class TimeoutHandler;
class WorkerControlRunnable;
class WorkerCSPEventListener;
class WorkerDebugger;
class WorkerDebuggerGlobalScope;
class WorkerErrorReport;
class WorkerEventTarget;
class WorkerGlobalScope;
class WorkerParentRef;
class WorkerRef;
class WorkerRunnable;
class WorkerDebuggeeRunnable;
class WorkerThread;
class WorkerThreadRunnable;

// SharedMutex is a small wrapper around an (internal) reference-counted Mutex
// object. It exists to avoid changing a lot of code to use Mutex* instead of
// Mutex&.
class MOZ_CAPABILITY("mutex") SharedMutex {
  using Mutex = mozilla::Mutex;

  class MOZ_CAPABILITY("mutex") RefCountedMutex final : public Mutex {
   public:
    explicit RefCountedMutex(const char* aName) : Mutex(aName) {}

    NS_INLINE_DECL_THREADSAFE_REFCOUNTING(RefCountedMutex)

   private:
    ~RefCountedMutex() = default;
  };

  const RefPtr<RefCountedMutex> mMutex;

 public:
  explicit SharedMutex(const char* aName)
      : mMutex(new RefCountedMutex(aName)) {}

  SharedMutex(const SharedMutex& aOther) = default;

  operator Mutex&() MOZ_RETURN_CAPABILITY(this) { return *mMutex; }

  operator const Mutex&() const MOZ_RETURN_CAPABILITY(this) { return *mMutex; }

  // We need these to make thread-safety analysis work
  void Lock() MOZ_CAPABILITY_ACQUIRE() { mMutex->Lock(); }
  void Unlock() MOZ_CAPABILITY_RELEASE() { mMutex->Unlock(); }

  // We can assert we own 'this', but we can't assert we hold mMutex
  void AssertCurrentThreadOwns() const
      MOZ_ASSERT_CAPABILITY(this) MOZ_NO_THREAD_SAFETY_ANALYSIS {
    mMutex->AssertCurrentThreadOwns();
  }
};

nsString ComputeWorkerPrivateId();

class WorkerPrivate final
    : public RelativeTimeline,
      public SupportsCheckedUnsafePtr<CheckIf<DiagnosticAssertEnabled>> {
 public:
  // Callback invoked on the parent thread when the worker's cancellation is
  // about to be requested.  This covers both calls to
  // WorkerPrivate::Cancel() by the owner as well as self-initiated cancellation
  // due to top-level script evaluation failing or close() being invoked on the
  // global scope for Dedicated and Shared workers, but not Service Workers as
  // they do not expose a close() method.
  //
  // ### Parent-Initiated Cancellation
  //
  // When WorkerPrivate::Cancel is invoked on the parent thread (by the binding
  // exposed Worker::Terminate), this callback is invoked synchronously inside
  // that call.
  //
  // ### Worker Self-Cancellation
  //
  // When a worker initiates self-cancellation, the worker's notification to the
  // parent thread is a non-blocking, async mechanism triggered by
  // `WorkerPrivate::DispatchCancelingRunnable`.
  //
  // Self-cancellation races a normally scheduled runnable against a timer that
  // is scheduled against the parent.  The 2 paths initiated by
  // DispatchCancelingRunnable are:
  //
  // 1. A CancelingRunnable is dispatched at the worker's normal event target to
  //    wait for the event loop to be clear of runnables.  When the
  //    CancelingRunnable runs it will dispatch a CancelingOnParentRunnable to
  //    its parent which is a normal, non-control WorkerDebuggeeRunnable to
  //    ensure that any postMessages to the parent or similar events get a
  //    chance to be processed prior to cancellation.  The timer scheduled in
  //    the next bullet will not be canceled unless
  //
  // 2. A CancelingWithTimeoutOnParentRunnable control runnable is dispatched
  //    to the parent to schedule a timer which will (also) fire on the parent
  //    thread.  This handles the case where the worker does not yield
  //    control-flow, and so the normal runnable scheduled above does not get to
  //    run in a timely fashion.  Because this is a control runnable, if the
  //    parent is a worker then the runnable will be processed with urgency.
  //    However, if the worker is top-level, then the control-like throttled
  //    WorkerPrivate::mMainThreadEventTarget will end up getting used which is
  //    nsIRunnablePriority::PRIORITY_MEDIUMHIGH and distinct from the
  //    mMainThreadDebuggeeEventTarget which most runnables (like postMessage)
  //    use.
  //
  //    The timer will explicitly use the control event target if the parent is
  //    a worker and the implicit event target (via `NS_NewTimer()`) otherwise.
  //    The callback is CancelingTimerCallback which just calls
  //    WorkerPrivate::Cancel.
  using CancellationCallback = std::function<void(bool aEverRan)>;

  // Callback invoked on the parent just prior to dropping the worker thread's
  // strong reference that keeps the WorkerPrivate alive while the worker thread
  // is running.  This does not provide a guarantee that the underlying thread
  // has fully shutdown, just that the worker logic has fully shutdown.
  //
  // ### Details
  //
  // The last thing the worker thread's WorkerThreadPrimaryRunnable does before
  // initiating the shutdown of the underlying thread is call ScheduleDeletion.
  // ScheduleDeletion dispatches a runnable to the parent to notify it that the
  // worker has completed its work and will never touch the WorkerPrivate again
  // and that the strong self-reference can be dropped.
  //
  // For parents that are themselves workers, this will be done by
  // WorkerFinishedRunnable which is a WorkerControlRunnable, ensuring that this
  // is processed in a timely fashion.  For main-thread parents,
  // TopLevelWorkerFinishedRunnable will be used and sent via
  // mMainThreadEventTargetForMessaging which is a weird ThrottledEventQueue
  // which does not provide any ordering guarantees relative to
  // mMainThreadDebuggeeEventTarget, so if you want those, you need to enhance
  // things.
  using TerminationCallback = std::function<void(void)>;

  struct LocationInfo {
    nsCString mHref;
    nsCString mProtocol;
    nsCString mHost;
    nsCString mHostname;
    nsCString mPort;
    nsCString mPathname;
    nsCString mSearch;
    nsCString mHash;
    nsString mOrigin;
  };

  NS_INLINE_DECL_REFCOUNTING(WorkerPrivate)

  static already_AddRefed<WorkerPrivate> Constructor(
      JSContext* aCx, const nsAString& aScriptURL, bool aIsChromeWorker,
      WorkerKind aWorkerKind, RequestCredentials aRequestCredentials,
      const WorkerType aWorkerType, const nsAString& aWorkerName,
      const nsACString& aServiceWorkerScope, WorkerLoadInfo* aLoadInfo,
      ErrorResult& aRv, nsString aId = u""_ns,
      CancellationCallback&& aCancellationCallback = {},
      TerminationCallback&& aTerminationCallback = {},
      mozilla::ipc::Endpoint<
          PRemoteWorkerNonLifeCycleOpControllerChild>&& aChildEp =
          mozilla::ipc::Endpoint<PRemoteWorkerNonLifeCycleOpControllerChild>());

  enum LoadGroupBehavior { InheritLoadGroup, OverrideLoadGroup };

  static nsresult GetLoadInfo(
      JSContext* aCx, nsPIDOMWindowInner* aWindow, WorkerPrivate* aParent,
      const nsAString& aScriptURL, const enum WorkerType& aWorkerType,
      const RequestCredentials& aCredentials, bool aIsChromeWorker,
      LoadGroupBehavior aLoadGroupBehavior, WorkerKind aWorkerKind,
      WorkerLoadInfo* aLoadInfo);

  void Traverse(nsCycleCollectionTraversalCallback& aCb);

  void ClearSelfAndParentEventTargetRef() {
    AssertIsOnParentThread();
    MOZ_ASSERT(mSelfRef);

    if (mTerminationCallback) {
      mTerminationCallback();
      mTerminationCallback = nullptr;
    }

    mParentEventTargetRef = nullptr;
    mSelfRef = nullptr;
  }

  // May be called on any thread...
  bool Start();

  // Called on the parent thread.
  bool Notify(WorkerStatus aStatus);

  bool Cancel() { return Notify(Canceling); }

  bool Close() MOZ_REQUIRES(mMutex);

  // The passed principal must be the Worker principal in case of a
  // ServiceWorker and the loading principal for any other type.
  static void OverrideLoadInfoLoadGroup(WorkerLoadInfo& aLoadInfo,
                                        nsIPrincipal* aPrincipal);

  bool IsDebuggerRegistered() MOZ_NO_THREAD_SAFETY_ANALYSIS {
    AssertIsOnMainThread();

    // No need to lock here since this is only ever modified by the same thread.
    return mDebuggerRegistered;  // would give a thread-safety warning
  }

  bool ExtensionAPIAllowed() {
    return (
        StaticPrefs::extensions_backgroundServiceWorker_enabled_AtStartup() &&
        mExtensionAPIAllowed);
  }

  void SetIsDebuggerRegistered(bool aDebuggerRegistered) {
    AssertIsOnMainThread();

    MutexAutoLock lock(mMutex);

    MOZ_ASSERT(mDebuggerRegistered != aDebuggerRegistered);
    mDebuggerRegistered = aDebuggerRegistered;

    mCondVar.Notify();
  }

  // Mark worker private as running in the background tab
  // for further throttling
  void SetIsRunningInBackground();

  void SetIsRunningInForeground();

  bool ChangeBackgroundStateInternal(bool aIsBackground);

  // returns true, if worker is running in the background tab
  bool IsRunningInBackground() const { return mIsInBackground; }

  void WaitForIsDebuggerRegistered(bool aDebuggerRegistered) {
    AssertIsOnParentThread();

    // Yield so that the main thread won't be blocked.
    AutoYieldJSThreadExecution yield;

    MOZ_ASSERT(!NS_IsMainThread());

    MutexAutoLock lock(mMutex);

    while (mDebuggerRegistered != aDebuggerRegistered) {
      mCondVar.Wait();
    }
  }

  nsresult SetIsDebuggerReady(bool aReady);

  WorkerDebugger* Debugger() const {
    AssertIsOnMainThread();

    MOZ_ASSERT(mDebugger);
    return mDebugger;
  }

  const OriginTrials& Trials() const { return mLoadInfo.mTrials; }

  void SetDebugger(WorkerDebugger* aDebugger) {
    AssertIsOnMainThread();

    MOZ_ASSERT(mDebugger != aDebugger);
    mDebugger = aDebugger;
  }

  JS::UniqueChars AdoptDefaultLocale() {
    MOZ_ASSERT(mDefaultLocale,
               "the default locale must have been successfully set for anyone "
               "to be trying to adopt it");
    return std::move(mDefaultLocale);
  }

  /**
   * Invoked by WorkerThreadPrimaryRunnable::Run if it already called
   * SetWorkerPrivateInWorkerThread but has to bail out on initialization before
   * calling DoRunLoop because PBackground failed to initialize or something
   * like that.  Note that there's currently no point earlier than this that
   * failure can be reported.
   *
   * When this happens, the worker will need to be deleted, plus the call to
   * SetWorkerPrivateInWorkerThread will have scheduled all the
   * mPreStartRunnables which need to be cleaned up after, as well as any
   * scheduled control runnables.  We're somewhat punting on debugger runnables
   * for now, which may leak, but the intent is to moot this whole scenario via
   * shutdown blockers, so we don't want the extra complexity right now.
   */
  void RunLoopNeverRan();

  MOZ_CAN_RUN_SCRIPT
  void DoRunLoop(JSContext* aCx);

  void UnrootGlobalScopes();

  MOZ_CAN_RUN_SCRIPT bool InterruptCallback(JSContext* aCx);

  bool IsOnCurrentThread();

  void CloseInternal();

  bool FreezeInternal();

  bool ThawInternal();

  void PropagateStorageAccessPermissionGrantedInternal();

  void TraverseTimeouts(nsCycleCollectionTraversalCallback& aCallback);

  void UnlinkTimeouts();

  bool AddChildWorker(WorkerPrivate& aChildWorker);

  void RemoveChildWorker(WorkerPrivate& aChildWorker);

  void PostMessageToParent(JSContext* aCx, JS::Handle<JS::Value> aMessage,
                           const Sequence<JSObject*>& aTransferable,
                           ErrorResult& aRv);

  void PostMessageToParentMessagePort(JSContext* aCx,
                                      JS::Handle<JS::Value> aMessage,
                                      const Sequence<JSObject*>& aTransferable,
                                      ErrorResult& aRv);

  MOZ_CAN_RUN_SCRIPT void EnterDebuggerEventLoop();

  void LeaveDebuggerEventLoop();

  void PostMessageToDebugger(const nsAString& aMessage);

  void SetDebuggerImmediate(Function& aHandler, ErrorResult& aRv);

  void ReportErrorToDebugger(const nsACString& aFilename, uint32_t aLineno,
                             const nsAString& aMessage);

  bool NotifyInternal(WorkerStatus aStatus);

  void ReportError(JSContext* aCx, JS::ConstUTF8CharsZ aToStringResult,
                   JSErrorReport* aReport);

  static void ReportErrorToConsole(
      uint32_t aErrorFlags, const nsCString& aCategory,
      nsContentUtils::PropertiesFile aFile, const nsCString& aMessageName,
      const nsTArray<nsString>& aParams = nsTArray<nsString>(),
      const mozilla::SourceLocation& aLocation =
          mozilla::JSCallingLocation::Get());

  int32_t SetTimeout(JSContext* aCx, TimeoutHandler* aHandler, int32_t aTimeout,
                     bool aIsInterval, Timeout::Reason aReason,
                     ErrorResult& aRv);

  void ClearTimeout(int32_t aId, Timeout::Reason aReason);

  MOZ_CAN_RUN_SCRIPT bool RunExpiredTimeouts(JSContext* aCx);

  bool RescheduleTimeoutTimer(JSContext* aCx);

  void UpdateContextOptionsInternal(JSContext* aCx,
                                    const JS::ContextOptions& aContextOptions);

  void UpdateLanguagesInternal(const nsTArray<nsString>& aLanguages);

  void UpdateJSWorkerMemoryParameterInternal(JSContext* aCx, JSGCParamKey key,
                                             Maybe<uint32_t> aValue);

  enum WorkerRanOrNot { WorkerNeverRan = 0, WorkerRan };

  void ScheduleDeletion(WorkerRanOrNot aRanOrNot);

  bool CollectRuntimeStats(JS::RuntimeStats* aRtStats, bool aAnonymize);

#ifdef JS_GC_ZEAL
  void UpdateGCZealInternal(JSContext* aCx, uint8_t aGCZeal,
                            uint32_t aFrequency);
#endif

  void SetLowMemoryStateInternal(JSContext* aCx, bool aState);

  void GarbageCollectInternal(JSContext* aCx, bool aShrinking,
                              bool aCollectChildren);

  void CycleCollectInternal(bool aCollectChildren);

  void OfflineStatusChangeEventInternal(bool aIsOffline);

  void MemoryPressureInternal();

  typedef MozPromise<uint64_t, nsresult, true> JSMemoryUsagePromise;
  RefPtr<JSMemoryUsagePromise> GetJSMemoryUsage();

  void SetFetchHandlerWasAdded() {
    MOZ_ASSERT(IsServiceWorker());
    AssertIsOnWorkerThread();
    mFetchHandlerWasAdded = true;
  }

  bool FetchHandlerWasAdded() const {
    MOZ_ASSERT(IsServiceWorker());
    AssertIsOnWorkerThread();
    return mFetchHandlerWasAdded;
  }

  JSContext* GetJSContext() const MOZ_NO_THREAD_SAFETY_ANALYSIS {
    // mJSContext is only modified on the worker thread, so workerthread code
    // can safely read it without a lock
    AssertIsOnWorkerThread();
    return mJSContext;
  }

  WorkerGlobalScope* GlobalScope() const {
    auto data = mWorkerThreadAccessible.Access();
    return data->mScope;
  }

  WorkerDebuggerGlobalScope* DebuggerGlobalScope() const {
    auto data = mWorkerThreadAccessible.Access();
    return data->mDebuggerScope;
  }

  // Get the global associated with the current nested event loop.  Will return
  // null if we're not in a nested event loop or that nested event loop does not
  // have an associated global.
  nsIGlobalObject* GetCurrentEventLoopGlobal() const {
    auto data = mWorkerThreadAccessible.Access();
    return data->mCurrentEventLoopGlobal;
  }

  nsICSPEventListener* CSPEventListener() const;

  void SetThread(WorkerThread* aThread);

  void SetWorkerPrivateInWorkerThread(WorkerThread* aThread);

  void ResetWorkerPrivateInWorkerThread();

  bool IsOnWorkerThread() const;

  void AssertIsOnWorkerThread() const
#ifdef DEBUG
      ;
#else
  {
  }
#endif

  // This may block!
  void BeginCTypesCall();

  // This may block!
  void EndCTypesCall();

  void BeginCTypesCallback();

  void EndCTypesCallback();

  bool ConnectMessagePort(JSContext* aCx, UniqueMessagePortId& aIdentifier);

  WorkerGlobalScope* GetOrCreateGlobalScope(JSContext* aCx);

  WorkerDebuggerGlobalScope* CreateDebuggerGlobalScope(JSContext* aCx);

  bool RegisterBindings(JSContext* aCx, JS::Handle<JSObject*> aGlobal);

  bool RegisterDebuggerBindings(JSContext* aCx, JS::Handle<JSObject*> aGlobal);

  bool OnLine() const {
    auto data = mWorkerThreadAccessible.Access();
    return data->mOnLine;
  }

  void StopSyncLoop(nsIEventTarget* aSyncLoopTarget, nsresult aResult);

  bool MaybeStopSyncLoop(nsIEventTarget* aSyncLoopTarget, nsresult aResult);

  void ShutdownModuleLoader();

  void ClearPreStartRunnables();

  MOZ_CAN_RUN_SCRIPT void ProcessSingleDebuggerRunnable();
  void ClearDebuggerEventQueue();

  void OnProcessNextEvent();

  void AfterProcessNextEvent();

  void AssertValidSyncLoop(nsIEventTarget* aSyncLoopTarget)
#ifdef DEBUG
      ;
#else
  {
  }
#endif

  void AssertIsNotPotentiallyLastGCCCRunning() {
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
    auto data = mWorkerThreadAccessible.Access();
    MOZ_DIAGNOSTIC_ASSERT(!data->mIsPotentiallyLastGCCCRunning);
#endif
  }

  void SetWorkerScriptExecutedSuccessfully() {
    AssertIsOnWorkerThread();
    // Should only be called once!
    MOZ_ASSERT(!mWorkerScriptExecutedSuccessfully);
    mWorkerScriptExecutedSuccessfully = true;
  }

  // Only valid after CompileScriptRunnable has finished running!
  bool WorkerScriptExecutedSuccessfully() const {
    AssertIsOnWorkerThread();
    return mWorkerScriptExecutedSuccessfully;
  }

  // Get the event target to use when dispatching to the main thread
  // from this Worker thread.  This may be the main thread itself or
  // a ThrottledEventQueue to the main thread.
  nsISerialEventTarget* MainThreadEventTargetForMessaging();

  nsresult DispatchToMainThreadForMessaging(
      nsIRunnable* aRunnable, uint32_t aFlags = NS_DISPATCH_NORMAL);

  nsresult DispatchToMainThreadForMessaging(
      already_AddRefed<nsIRunnable> aRunnable,
      uint32_t aFlags = NS_DISPATCH_NORMAL);

  nsISerialEventTarget* MainThreadEventTarget();

  nsresult DispatchToMainThread(nsIRunnable* aRunnable,
                                uint32_t aFlags = NS_DISPATCH_NORMAL);

  nsresult DispatchToMainThread(already_AddRefed<nsIRunnable> aRunnable,
                                uint32_t aFlags = NS_DISPATCH_NORMAL);

  nsresult DispatchDebuggeeToMainThread(
      already_AddRefed<WorkerRunnable> aRunnable,
      uint32_t aFlags = NS_DISPATCH_NORMAL);

  // Get an event target that will dispatch runnables as control runnables on
  // the worker thread.  Implement nsICancelableRunnable if you wish to take
  // action on cancelation.
  nsISerialEventTarget* ControlEventTarget();

  // Get an event target that will attempt to dispatch a normal WorkerRunnable,
  // but if that fails will then fall back to a control runnable.
  nsISerialEventTarget* HybridEventTarget();

  void DumpCrashInformation(nsACString& aString);

  ClientType GetClientType() const;

  bool EnsureCSPEventListener();

  void EnsurePerformanceStorage();

  bool GetExecutionGranted() const;
  void SetExecutionGranted(bool aGranted);

  void ScheduleTimeSliceExpiration(uint32_t aDelay);
  void CancelTimeSliceExpiration();

  JSExecutionManager* GetExecutionManager() const;
  void SetExecutionManager(JSExecutionManager* aManager);

  void ExecutionReady();

  PerformanceStorage* GetPerformanceStorage();

  bool IsAcceptingEvents() MOZ_EXCLUDES(mMutex) {
    AssertIsOnParentThread();

    MutexAutoLock lock(mMutex);
    return mParentStatus < Canceling;
  }

  // This method helps know if the Worker is already at Dead status.
  // Note that it is racy. The status may change after the function returns.
  bool IsDead() MOZ_EXCLUDES(mMutex) {
    MutexAutoLock lock(mMutex);
    return mStatus == Dead;
  }

  WorkerStatus ParentStatusProtected() {
    AssertIsOnParentThread();
    MutexAutoLock lock(mMutex);
    return mParentStatus;
  }

  WorkerStatus ParentStatus() const MOZ_REQUIRES(mMutex) {
    mMutex.AssertCurrentThreadOwns();
    return mParentStatus;
  }

  Worker* ParentEventTargetRef() const {
    MOZ_DIAGNOSTIC_ASSERT(mParentEventTargetRef);
    return mParentEventTargetRef;
  }

  void SetParentEventTargetRef(Worker* aParentEventTargetRef) {
    MOZ_DIAGNOSTIC_ASSERT(aParentEventTargetRef);
    MOZ_DIAGNOSTIC_ASSERT(!mParentEventTargetRef);
    mParentEventTargetRef = aParentEventTargetRef;
  }

  // Check whether this worker is a secure context.  For use from the parent
  // thread only; the canonical "is secure context" boolean is stored on the
  // compartment of the worker global.  The only reason we don't
  // AssertIsOnParentThread() here is so we can assert that this value matches
  // the one on the compartment, which has to be done from the worker thread.
  bool IsSecureContext() const { return mIsSecureContext; }

  // Check whether we're running in automation.
  bool IsInAutomation() const { return mIsInAutomation; }

  bool IsPrivilegedAddonGlobal() const { return mIsPrivilegedAddonGlobal; }

  TimeStamp CreationTimeStamp() const { return mCreationTimeStamp; }

  DOMHighResTimeStamp CreationTime() const { return mCreationTimeHighRes; }

  DOMHighResTimeStamp TimeStampToDOMHighRes(const TimeStamp& aTimeStamp) const {
    MOZ_ASSERT(!aTimeStamp.IsNull());
    TimeDuration duration = aTimeStamp - mCreationTimeStamp;
    return duration.ToMilliseconds();
  }

  LocationInfo& GetLocationInfo() { return mLocationInfo; }

  void CopyJSSettings(workerinternals::JSSettings& aSettings) {
    mozilla::MutexAutoLock lock(mMutex);
    aSettings = mJSSettings;
  }

  void CopyJSRealmOptions(JS::RealmOptions& aOptions) {
    mozilla::MutexAutoLock lock(mMutex);
    aOptions = IsChromeWorker() ? mJSSettings.chromeRealmOptions
                                : mJSSettings.contentRealmOptions;
  }

  // The ability to be a chrome worker is orthogonal to the type of
  // worker [Dedicated|Shared|Service].
  bool IsChromeWorker() const { return mIsChromeWorker; }

  // TODO: Invariants require that the parent worker out-live any child
  // worker, so WorkerPrivate* should be safe in the moment of calling.
  // We would like to have stronger type-system annotated/enforced handling.
  WorkerPrivate* GetParent() const { return mParent; }

  // Returns the top level worker. It can be the current worker if it's the top
  // level one.
  WorkerPrivate* GetTopLevelWorker() const {
    WorkerPrivate const* wp = this;
    while (wp->GetParent()) {
      wp = wp->GetParent();
    }
    return const_cast<WorkerPrivate*>(wp);
  }

  bool IsFrozen() const;

  bool IsFrozenForWorkerThread() const;

  bool IsParentWindowPaused() const {
    AssertIsOnParentThread();
    return mParentWindowPaused;
  }

  // When we debug a worker, we want to disconnect the window and the worker
  // communication. This happens calling this method.
  // Note: this method doesn't suspend the worker! Use Freeze/Thaw instead.
  void ParentWindowPaused();

  void ParentWindowResumed();

  const nsString& ScriptURL() const { return mScriptURL; }

  const nsString& WorkerName() const { return mWorkerName; }
  RequestCredentials WorkerCredentials() const { return mCredentialsMode; }
  enum WorkerType WorkerType() const { return mWorkerType; }

  WorkerKind Kind() const { return mWorkerKind; }

  bool IsDedicatedWorker() const { return mWorkerKind == WorkerKindDedicated; }

  bool IsSharedWorker() const { return mWorkerKind == WorkerKindShared; }

  bool IsServiceWorker() const { return mWorkerKind == WorkerKindService; }

  nsContentPolicyType ContentPolicyType() const {
    return ContentPolicyType(mWorkerKind);
  }

  static nsContentPolicyType ContentPolicyType(WorkerKind aWorkerKind) {
    switch (aWorkerKind) {
      case WorkerKindDedicated:
        return nsIContentPolicy::TYPE_INTERNAL_WORKER;
      case WorkerKindShared:
        return nsIContentPolicy::TYPE_INTERNAL_SHARED_WORKER;
      case WorkerKindService:
        return nsIContentPolicy::TYPE_INTERNAL_SERVICE_WORKER;
      default:
        MOZ_ASSERT_UNREACHABLE("Invalid worker type");
        return nsIContentPolicy::TYPE_INVALID;
    }
  }

  nsIScriptContext* GetScriptContext() const {
    AssertIsOnMainThread();
    return mLoadInfo.mScriptContext;
  }

  const nsCString& Domain() const { return mLoadInfo.mDomain; }

  bool IsFromWindow() const { return mLoadInfo.mFromWindow; }

  nsLoadFlags GetLoadFlags() const { return mLoadInfo.mLoadFlags; }

  uint64_t WindowID() const { return mLoadInfo.mWindowID; }

  uint64_t AssociatedBrowsingContextID() const {
    return mLoadInfo.mAssociatedBrowsingContextID;
  }

  uint64_t ServiceWorkerID() const { return GetServiceWorkerDescriptor().Id(); }

  const nsCString& ServiceWorkerScope() const {
    return GetServiceWorkerDescriptor().Scope();
  }

  // This value should never change after the script load completes. Before
  // then, it may only be called on the main thread.
  nsIURI* GetBaseURI() const { return mLoadInfo.mBaseURI; }

  void SetBaseURI(nsIURI* aBaseURI);

  nsIURI* GetResolvedScriptURI() const { return mLoadInfo.mResolvedScriptURI; }

  const nsString& ServiceWorkerCacheName() const {
    MOZ_DIAGNOSTIC_ASSERT(IsServiceWorker());
    AssertIsOnMainThread();
    return mLoadInfo.mServiceWorkerCacheName;
  }

  const ServiceWorkerDescriptor& GetServiceWorkerDescriptor() const {
    MOZ_DIAGNOSTIC_ASSERT(IsServiceWorker());
    MOZ_DIAGNOSTIC_ASSERT(mLoadInfo.mServiceWorkerDescriptor.isSome());
    return mLoadInfo.mServiceWorkerDescriptor.ref();
  }

  const ServiceWorkerRegistrationDescriptor&
  GetServiceWorkerRegistrationDescriptor() const {
    MOZ_DIAGNOSTIC_ASSERT(IsServiceWorker());
    MOZ_DIAGNOSTIC_ASSERT(
        mLoadInfo.mServiceWorkerRegistrationDescriptor.isSome());
    return mLoadInfo.mServiceWorkerRegistrationDescriptor.ref();
  }

  const ClientInfo& GetSourceInfo() const {
    MOZ_DIAGNOSTIC_ASSERT(IsServiceWorker());
    MOZ_DIAGNOSTIC_ASSERT(mLoadInfo.mSourceInfo.isSome());
    return mLoadInfo.mSourceInfo.ref();
  }

  void UpdateServiceWorkerState(ServiceWorkerState aState) {
    MOZ_DIAGNOSTIC_ASSERT(IsServiceWorker());
    MOZ_DIAGNOSTIC_ASSERT(mLoadInfo.mServiceWorkerDescriptor.isSome());
    return mLoadInfo.mServiceWorkerDescriptor.ref().SetState(aState);
  }

  void UpdateIsOnContentBlockingAllowList(bool aOnContentBlockingAllowList);

  const Maybe<ServiceWorkerDescriptor>& GetParentController() const {
    return mLoadInfo.mParentController;
  }

  const ChannelInfo& GetChannelInfo() const { return mLoadInfo.mChannelInfo; }

  void SetChannelInfo(const ChannelInfo& aChannelInfo) {
    AssertIsOnMainThread();
    MOZ_ASSERT(!mLoadInfo.mChannelInfo.IsInitialized());
    MOZ_ASSERT(aChannelInfo.IsInitialized());
    mLoadInfo.mChannelInfo = aChannelInfo;
  }

  void InitChannelInfo(nsIChannel* aChannel) {
    mLoadInfo.mChannelInfo.InitFromChannel(aChannel);
  }

  void InitChannelInfo(const ChannelInfo& aChannelInfo) {
    mLoadInfo.mChannelInfo = aChannelInfo;
  }

  nsIPrincipal* GetPrincipal() const { return mLoadInfo.mPrincipal; }

  nsIPrincipal* GetLoadingPrincipal() const {
    return mLoadInfo.mLoadingPrincipal;
  }

  nsIPrincipal* GetPartitionedPrincipal() const {
    return mLoadInfo.mPartitionedPrincipal;
  }

  nsIPrincipal* GetEffectiveStoragePrincipal() const;

  nsILoadGroup* GetLoadGroup() const {
    AssertIsOnMainThread();
    return mLoadInfo.mLoadGroup;
  }

  bool UsesSystemPrincipal() const {
    return GetPrincipal()->IsSystemPrincipal();
  }
  bool UsesAddonOrExpandedAddonPrincipal() const {
    return GetPrincipal()->GetIsAddonOrExpandedAddonPrincipal();
  }

  const mozilla::ipc::PrincipalInfo& GetPrincipalInfo() const {
    return *mLoadInfo.mPrincipalInfo;
  }

  const mozilla::ipc::PrincipalInfo& GetPartitionedPrincipalInfo() const {
    return *mLoadInfo.mPartitionedPrincipalInfo;
  }

  const mozilla::ipc::PrincipalInfo& GetEffectiveStoragePrincipalInfo() const;

  already_AddRefed<nsIChannel> ForgetWorkerChannel() {
    AssertIsOnMainThread();
    return mLoadInfo.mChannel.forget();
  }

  nsPIDOMWindowInner* GetWindow() const {
    AssertIsOnMainThread();
    return mLoadInfo.mWindow;
  }

  nsPIDOMWindowInner* GetAncestorWindow() const;

  void EvictFromBFCache();

  nsIContentSecurityPolicy* GetCsp() const {
    AssertIsOnMainThread();
    return mLoadInfo.mCSP;
  }

  nsresult SetCsp(nsIContentSecurityPolicy* aCSP);

  nsresult SetCSPFromHeaderValues(const nsACString& aCSPHeaderValue,
                                  const nsACString& aCSPReportOnlyHeaderValue);

  void StoreCSPOnClient();

  const mozilla::ipc::CSPInfo& GetCSPInfo() const {
    return mLoadInfo.mCSPContext->CSPInfo();
  }

  WorkerCSPContext* GetCSPContext() const {
    return mLoadInfo.mCSPContext.get();
  }

  void UpdateReferrerInfoFromHeader(
      const nsACString& aReferrerPolicyHeaderValue);

  nsIReferrerInfo* GetReferrerInfo() const { return mLoadInfo.mReferrerInfo; }

  ReferrerPolicy GetReferrerPolicy() const {
    return mLoadInfo.mReferrerInfo->ReferrerPolicy();
  }

  void SetReferrerInfo(nsIReferrerInfo* aReferrerInfo) {
    mLoadInfo.mReferrerInfo = aReferrerInfo;
  }

  bool XHRParamsAllowed() const { return mLoadInfo.mXHRParamsAllowed; }

  void SetXHRParamsAllowed(bool aAllowed) {
    mLoadInfo.mXHRParamsAllowed = aAllowed;
  }

  mozilla::StorageAccess StorageAccess() const {
    AssertIsOnWorkerThread();
    if (mLoadInfo.mUsingStorageAccess) {
      return mozilla::StorageAccess::eAllow;
    }

    return mLoadInfo.mStorageAccess;
  }

  bool UseRegularPrincipal() const {
    AssertIsOnWorkerThread();
    return mLoadInfo.mUseRegularPrincipal;
  }

  bool UsingStorageAccess() const {
    AssertIsOnWorkerThread();
    return mLoadInfo.mUsingStorageAccess;
  }

  nsICookieJarSettings* CookieJarSettings() const {
    // Any thread.
    MOZ_ASSERT(mLoadInfo.mCookieJarSettings);
    return mLoadInfo.mCookieJarSettings;
  }

  const net::CookieJarSettingsArgs& CookieJarSettingsArgs() const {
    MOZ_ASSERT(mLoadInfo.mCookieJarSettings);
    return mLoadInfo.mCookieJarSettingsArgs;
  }

  const OriginAttributes& GetOriginAttributes() const {
    return mLoadInfo.mOriginAttributes;
  }

  // Determine if the SW testing per-window flag is set by devtools
  bool ServiceWorkersTestingInWindow() const {
    return mLoadInfo.mServiceWorkersTestingInWindow;
  }

  // Determine if the worker was created under a third-party context.
  bool IsThirdPartyContext() const { return mLoadInfo.mIsThirdPartyContext; }

  bool IsWatchedByDevTools() const { return mLoadInfo.mWatchedByDevTools; }

  bool ShouldResistFingerprinting(RFPTarget aTarget) const;

  const Maybe<RFPTargetSet>& GetOverriddenFingerprintingSettings() const {
    return mLoadInfo.mOverriddenFingerprintingSettings;
  }

  bool IsOn3PCBExceptionList() const {
    return mLoadInfo.mIsOn3PCBExceptionList;
  }

  RemoteWorkerChild* GetRemoteWorkerController();

  void SetRemoteWorkerController(RemoteWorkerChild* aController);

  RefPtr<GenericPromise> SetServiceWorkerSkipWaitingFlag();

  // We can assume that an nsPIDOMWindow will be available for Freeze, Thaw
  // as these are only used for globals going in and out of the bfcache.
  bool Freeze(const nsPIDOMWindowInner* aWindow);

  bool Thaw(const nsPIDOMWindowInner* aWindow);

  void PropagateStorageAccessPermissionGranted();

  void NotifyStorageKeyUsed();

  void EnableDebugger();

  void DisableDebugger();

  void BindRemoteWorkerDebuggerChild();

  void CreateRemoteDebuggerEndpoints();

  void SetIsRemoteDebuggerRegistered(const bool& aRegistered);

  void SetIsRemoteDebuggerReady(const bool& aReady);

  void EnableRemoteDebugger();

  void DisableRemoteDebugger();

  void DisableRemoteDebuggerOnWorkerThread(const bool& aForShutdown = false);

  void SetIsQueued(const bool& aQueued);

  bool IsQueued() const;

  void UpdateWindowIDToDebugger(const uint64_t& aWindowID, const bool& aIsAdd);

  already_AddRefed<WorkerRunnable> MaybeWrapAsWorkerRunnable(
      already_AddRefed<nsIRunnable> aRunnable);

  bool ProxyReleaseMainThreadObjects();

  void SetLowMemoryState(bool aState);

  void GarbageCollect(bool aShrinking);

  void CycleCollect();

  nsresult SetPrincipalsAndCSPOnMainThread(nsIPrincipal* aPrincipal,
                                           nsIPrincipal* aPartitionedPrincipal,
                                           nsILoadGroup* aLoadGroup,
                                           nsIContentSecurityPolicy* aCsp);

  nsresult SetPrincipalsAndCSPFromChannel(nsIChannel* aChannel);

  bool FinalChannelPrincipalIsValid(nsIChannel* aChannel);

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  bool PrincipalURIMatchesScriptURL();
#endif

  void UpdateOverridenLoadGroup(nsILoadGroup* aBaseLoadGroup);

  void WorkerScriptLoaded();

  Document* GetDocument() const;

  void MemoryPressure();

  void UpdateContextOptions(const JS::ContextOptions& aContextOptions);

  void UpdateLanguages(const nsTArray<nsString>& aLanguages);

  void UpdateJSWorkerMemoryParameter(JSGCParamKey key, Maybe<uint32_t> value);

#ifdef JS_GC_ZEAL
  void UpdateGCZeal(uint8_t aGCZeal, uint32_t aFrequency);
#endif

  void OfflineStatusChangeEvent(bool aIsOffline);

  nsresult Dispatch(already_AddRefed<WorkerRunnable> aRunnable,
                    nsIEventTarget* aSyncLoopTarget = nullptr);

  nsresult DispatchControlRunnable(
      already_AddRefed<WorkerRunnable> aWorkerRunnable);

  nsresult DispatchDebuggerRunnable(
      already_AddRefed<WorkerRunnable> aDebuggerRunnable);

  nsresult DispatchToParent(already_AddRefed<WorkerRunnable> aRunnable);

  bool IsOnParentThread() const;
  void DebuggerInterruptRequest();

#ifdef DEBUG
  void AssertIsOnParentThread() const;

  void AssertInnerWindowIsCorrect() const;
#else
  void AssertIsOnParentThread() const {}

  void AssertInnerWindowIsCorrect() const {}
#endif

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  bool PrincipalIsValid() const;
#endif

  void StartCancelingTimer();

  const nsString& Id();

  const nsID& AgentClusterId() const { return mAgentClusterId; }

  bool IsSharedMemoryAllowed() const;

  // https://whatpr.org/html/4734/structured-data.html#cross-origin-isolated
  bool CrossOriginIsolated() const;

  void SetUseCounter(UseCounterWorker aUseCounter) {
    MOZ_ASSERT(!mReportedUseCounters);
    MOZ_ASSERT(aUseCounter > UseCounterWorker::Unknown);
    AssertIsOnWorkerThread();
    mUseCounters[static_cast<size_t>(aUseCounter)] = true;
  }

  /**
   * COEP Methods
   *
   * If browser.tabs.remote.useCrossOriginEmbedderPolicy=false, these methods
   * will, depending on the return type, return a value that will avoid
   * assertion failures or a value that won't block loads.
   */
  nsILoadInfo::CrossOriginEmbedderPolicy GetEmbedderPolicy() const;

  // Fails if a policy has already been set or if `aPolicy` violates the owner's
  // policy, if an owner exists.
  mozilla::Result<Ok, nsresult> SetEmbedderPolicy(
      nsILoadInfo::CrossOriginEmbedderPolicy aPolicy);

  // `aRequest` is the request loading the worker and must be QI-able to
  // `nsIChannel*`. It's used to verify that the worker can indeed inherit its
  // owner's COEP (when an owner exists).
  //
  // TODO: remove `aRequest`; currently, it's required because instances may not
  // always know its final, resolved script URL or have access internally to
  // `aRequest`.
  void InheritOwnerEmbedderPolicyOrNull(nsIRequest* aRequest);

  // Requires a policy to already have been set.
  bool MatchEmbedderPolicy(
      nsILoadInfo::CrossOriginEmbedderPolicy aPolicy) const;

  nsILoadInfo::CrossOriginEmbedderPolicy GetOwnerEmbedderPolicy() const;

  void SetCCCollectedAnything(bool collectedAnything);
  bool isLastCCCollectedAnything();

  uint32_t GetCurrentTimerNestingLevel() const;

  void IncreaseTopLevelWorkerFinishedRunnableCount() {
    ++mTopLevelWorkerFinishedRunnableCount;
  }
  void DecreaseTopLevelWorkerFinishedRunnableCount() {
    --mTopLevelWorkerFinishedRunnableCount;
  }
  void IncreaseWorkerFinishedRunnableCount() { ++mWorkerFinishedRunnableCount; }
  void DecreaseWorkerFinishedRunnableCount() { --mWorkerFinishedRunnableCount; }

  void RunShutdownTasks();

  bool CancelBeforeWorkerScopeConstructed() const {
    auto data = mWorkerThreadAccessible.Access();
    return data->mCancelBeforeWorkerScopeConstructed;
  }

  enum class CCFlag : uint8_t {
    EligibleForWorkerRef,
    IneligibleForWorkerRef,
    EligibleForChildWorker,
    IneligibleForChildWorker,
    EligibleForTimeout,
    IneligibleForTimeout,
    CheckBackgroundActors,
  };

  // When create/release a StrongWorkerRef, child worker, and timeout, this
  // method is used to setup if mParentEventTargetRef can get into
  // cycle-collection.
  // When this method is called, it will also checks if any background actor
  // should block the mParentEventTargetRef cycle-collection when there is no
  // StrongWorkerRef/ChildWorker/Timeout.
  // Worker thread only.
  void UpdateCCFlag(const CCFlag);

  // This is used in WorkerPrivate::Traverse() to checking if
  // mParentEventTargetRef should get into cycle-collection.
  // Parent thread only method.
  bool IsEligibleForCC();

  // A method which adjusts the count of background actors which should not
  // block WorkerPrivate::mParentEventTargetRef cycle-collection.
  // Worker thread only.
  void AdjustNonblockingCCBackgroundActorCount(int32_t aCount);

  RefPtr<WorkerParentRef> GetWorkerParentRef() const;

  bool MayContinueRunning() {
    AssertIsOnWorkerThread();

    WorkerStatus status;
    {
      MutexAutoLock lock(mMutex);
      status = mStatus;
    }

    if (status < Canceling) {
      return true;
    }

    return false;
  }

 private:
  WorkerPrivate(
      WorkerPrivate* aParent, const nsAString& aScriptURL, bool aIsChromeWorker,
      WorkerKind aWorkerKind, RequestCredentials aRequestCredentials,
      enum WorkerType aWorkerType, const nsAString& aWorkerName,
      const nsACString& aServiceWorkerScope, WorkerLoadInfo& aLoadInfo,
      nsString&& aId, const nsID& aAgentClusterId,
      const nsILoadInfo::CrossOriginOpenerPolicy aAgentClusterOpenerPolicy,
      CancellationCallback&& aCancellationCallback,
      TerminationCallback&& aTerminationCallback,
      mozilla::ipc::Endpoint<PRemoteWorkerNonLifeCycleOpControllerChild>&&
          aChildEp);

  ~WorkerPrivate();

  struct AgentClusterIdAndCoop {
    nsID mId;
    nsILoadInfo::CrossOriginOpenerPolicy mCoop;
  };

  static AgentClusterIdAndCoop ComputeAgentClusterIdAndCoop(
      WorkerPrivate* aParent, WorkerKind aWorkerKind, WorkerLoadInfo* aLoadInfo,
      bool aIsChromeWorker);

  void CancelAllTimeouts();

  enum class ProcessAllControlRunnablesResult {
    // We did not process anything.
    Nothing,
    // We did process something, states may have changed, but we can keep
    // executing script.
    MayContinue,
    // We did process something, and should not continue executing script.
    Abort
  };

  ProcessAllControlRunnablesResult ProcessAllControlRunnables() {
    MutexAutoLock lock(mMutex);
    return ProcessAllControlRunnablesLocked();
  }

  ProcessAllControlRunnablesResult ProcessAllControlRunnablesLocked()
      MOZ_REQUIRES(mMutex);

  void EnableMemoryReporter();

  void DisableMemoryReporter();

  void WaitForWorkerEvents() MOZ_REQUIRES(mMutex);

  // If the worker shutdown status is equal or greater then aFailStatus, this
  // operation will fail and nullptr will be returned. See WorkerStatus.h for
  // more information about the correct value to use.
  already_AddRefed<nsISerialEventTarget> CreateNewSyncLoop(
      WorkerStatus aFailStatus);

  nsresult RunCurrentSyncLoop();

  nsresult DestroySyncLoop(uint32_t aLoopIndex);

  void InitializeGCTimers();

  enum GCTimerMode { PeriodicTimer = 0, IdleTimer, NoTimer };

  void SetGCTimerMode(GCTimerMode aMode);

 public:
  void CancelGCTimers() { SetGCTimerMode(NoTimer); }

 private:
  void ShutdownGCTimers();

  friend class WorkerRef;

  bool AddWorkerRef(WorkerRef* aWorkerRefer, WorkerStatus aFailStatus);

  void RemoveWorkerRef(WorkerRef* aWorkerRef);

  void NotifyWorkerRefs(WorkerStatus aStatus);

  bool HasActiveWorkerRefs() {
    auto data = mWorkerThreadAccessible.Access();
    return !(data->mChildWorkers.IsEmpty() && data->mTimeouts.IsEmpty() &&
             data->mWorkerRefs.IsEmpty());
  }

  friend class WorkerEventTarget;

  nsresult RegisterShutdownTask(nsITargetShutdownTask* aTask);

  nsresult UnregisterShutdownTask(nsITargetShutdownTask* aTask);

  // Internal logic to dispatch a runnable. This is separate from Dispatch()
  // to allow runnables to be atomically dispatched in bulk.
  nsresult DispatchLockHeld(already_AddRefed<WorkerRunnable> aRunnable,
                            nsIEventTarget* aSyncLoopTarget,
                            const MutexAutoLock& aProofOfLock)
      MOZ_REQUIRES(mMutex);

  // This method dispatches a simple runnable that starts the shutdown procedure
  // after a self.close(). This method is called after a ClearMainEventQueue()
  // to be sure that the canceling runnable is the only one in the queue.  We
  // need this async operation to be sure that all the current JS code is
  // executed.
  void DispatchCancelingRunnable();

  bool GetUseCounter(UseCounterWorker aUseCounter) {
    MOZ_ASSERT(aUseCounter > UseCounterWorker::Unknown);
    AssertIsOnWorkerThread();
    return mUseCounters[static_cast<size_t>(aUseCounter)];
  }

  void ReportUseCounters();

  UniquePtr<ClientSource> CreateClientSource();

  // This method is called when corresponding script loader processes the COEP
  // header for the worker.
  // This method should be called only once in the main thread.
  // After this method is called the COEP value owner(window/parent worker) is
  // cached in mOwnerEmbedderPolicy such that it can be accessed in other
  // threads, i.e. WorkerThread.
  void EnsureOwnerEmbedderPolicy();

  class EventTarget;
  friend class EventTarget;
  friend class AutoSyncLoopHolder;

  struct TimeoutInfo;

  class MemoryReporter;
  friend class MemoryReporter;

  friend class mozilla::dom::WorkerThread;

  SharedMutex mMutex;
  mozilla::CondVar mCondVar MOZ_GUARDED_BY(mMutex);

  // We cannot make this CheckedUnsafePtr<WorkerPrivate> as this would violate
  // our static assert
  MOZ_NON_OWNING_REF WorkerPrivate* const mParent;

  const nsString mScriptURL;

  // This is the worker name for shared workers and dedicated workers.
  const nsString mWorkerName;
  const RequestCredentials mCredentialsMode;
  enum WorkerType mWorkerType;

  const WorkerKind mWorkerKind;

  // The worker is owned by its thread, which is represented here.  This is set
  // in Constructor() and emptied by WorkerFinishedRunnable, and conditionally
  // traversed by the cycle collector if no other things preventing shutdown.
  //
  // There are 4 ways a worker can be terminated:
  // 1. GC/CC - When the worker is in idle state (busycount == 0), it allows to
  //    traverse the 'hidden' mParentEventTargetRef pointer. This is the exposed
  //    Worker webidl object. Doing this, CC will be able to detect a cycle and
  //    Unlink is called. In Unlink, Worker calls Cancel().
  // 2. Worker::Cancel() is called - the shutdown procedure starts immediately.
  // 3. WorkerScope::Close() is called - Similar to point 2.
  // 4. xpcom-shutdown notification - We call Kill().
  RefPtr<Worker> mParentEventTargetRef;
  RefPtr<WorkerPrivate> mSelfRef;

  CancellationCallback mCancellationCallback;

  // The termination callback is passed into the constructor on the parent
  // thread and invoked by `ClearSelfAndParentEventTargetRef` just before it
  // drops its self-ref.
  TerminationCallback mTerminationCallback;

  // The lifetime of these objects within LoadInfo is managed explicitly;
  // they do not need to be cycle collected.
  WorkerLoadInfo mLoadInfo;
  LocationInfo mLocationInfo;

  // Protected by mMutex.
  workerinternals::JSSettings mJSSettings MOZ_GUARDED_BY(mMutex);

  WorkerDebugger* mDebugger;

  workerinternals::Queue<WorkerRunnable*, 4> mControlQueue;
  workerinternals::Queue<WorkerRunnable*, 4> mDebuggerQueue
      MOZ_GUARDED_BY(mMutex);

  // Touched on multiple threads, protected with mMutex. Only modified on the
  // worker thread
  JSContext* mJSContext MOZ_GUARDED_BY(mMutex);
  // mThread is only modified on the Worker thread, before calling DoRunLoop
  RefPtr<WorkerThread> mThread MOZ_GUARDED_BY(mMutex);
  // mPRThread is only modified on another thread in ScheduleWorker(), and is
  // constant for the duration of DoRunLoop.  Static mutex analysis doesn't help
  // here
  PRThread* mPRThread;

  // Accessed from main thread
  RefPtr<ThrottledEventQueue> mMainThreadEventTargetForMessaging;
  RefPtr<ThrottledEventQueue> mMainThreadEventTarget;

  // Accessed from worker thread and destructing thread
  RefPtr<WorkerEventTarget> mWorkerControlEventTarget;
  RefPtr<WorkerEventTarget> mWorkerHybridEventTarget;

  // A pauseable queue for WorkerDebuggeeRunnables directed at the main thread.
  // See WorkerDebuggeeRunnable for details.
  RefPtr<ThrottledEventQueue> mMainThreadDebuggeeEventTarget;

  struct SyncLoopInfo {
    explicit SyncLoopInfo(EventTarget* aEventTarget);

    RefPtr<EventTarget> mEventTarget;
    nsresult mResult;
    bool mCompleted;
#ifdef DEBUG
    bool mHasRun;
#endif
  };

  // This is only modified on the worker thread, but in DEBUG builds
  // AssertValidSyncLoop function iterates it on other threads. Therefore
  // modifications are done with mMutex held *only* in DEBUG builds.
  nsTArray<UniquePtr<SyncLoopInfo>> mSyncLoopStack;

  nsCOMPtr<nsITimer> mCancelingTimer;

  // fired on the main thread if the worker script fails to load
  nsCOMPtr<nsIRunnable> mLoadFailedRunnable;

  RefPtr<PerformanceStorage> mPerformanceStorage;

  RefPtr<WorkerCSPEventListener> mCSPEventListener;

  // Protected by mMutex.
  nsTArray<RefPtr<WorkerThreadRunnable>> mPreStartRunnables
      MOZ_GUARDED_BY(mMutex);

  // Only touched on the parent thread.  Used for both SharedWorker and
  // ServiceWorker RemoteWorkers.
  RefPtr<RemoteWorkerChild> mRemoteWorkerController;

  // Only touched on the worker thread. Used for both SharedWorker and
  // ServiceWorker RemoteWorkers.
  RefPtr<RemoteWorkerNonLifeCycleOpControllerChild>
      mRemoteWorkerNonLifeCycleOpController;

  mozilla::ipc::Endpoint<PRemoteWorkerNonLifeCycleOpControllerChild> mChildEp;

  RefPtr<RemoteWorkerDebuggerChild> mRemoteDebugger;
  mozilla::ipc::Endpoint<PRemoteWorkerDebuggerChild> mDebuggerChildEp;
  mozilla::ipc::Endpoint<PRemoteWorkerDebuggerParent> mDebuggerParentEp;
  bool mRemoteDebuggerRegistered MOZ_GUARDED_BY(mMutex);
  bool mRemoteDebuggerReady MOZ_GUARDED_BY(mMutex);
  bool mIsQueued;  // Should only touched on parent thread.
  mozilla::CondVar mDebuggerBindingCondVar MOZ_GUARDED_BY(mMutex);
  RefPtr<WorkerEventTarget> mWorkerDebuggerEventTarget;

  JS::UniqueChars mDefaultLocale;  // nulled during worker JSContext init
  TimeStamp mKillTime;
  WorkerStatus mParentStatus MOZ_GUARDED_BY(mMutex);
  WorkerStatus mStatus MOZ_GUARDED_BY(mMutex);

  TimeStamp mCreationTimeStamp;
  DOMHighResTimeStamp mCreationTimeHighRes;

  // Flags for use counters used directly by this worker.
  static_assert(sizeof(UseCounterWorker) <= sizeof(size_t),
                "UseCounterWorker is too big");
  static_assert(UseCounterWorker::Count >= static_cast<UseCounterWorker>(0),
                "Should be non-negative value and safe to cast to unsigned");
  std::bitset<static_cast<size_t>(UseCounterWorker::Count)> mUseCounters;
  bool mReportedUseCounters;

  // This is created while creating the WorkerPrivate, so it's safe to be
  // touched on any thread.
  const nsID mAgentClusterId;

  // Things touched on worker thread only.
  struct WorkerThreadAccessible {
    explicit WorkerThreadAccessible(WorkerPrivate* aParent);

    RefPtr<WorkerGlobalScope> mScope;
    RefPtr<WorkerDebuggerGlobalScope> mDebuggerScope;
    // We cannot make this CheckedUnsafePtr<WorkerPrivate> as this would violate
    // our static assert
    nsTArray<WorkerPrivate*> mChildWorkers;
    nsTObserverArray<WorkerRef*> mWorkerRefs;
    nsTArray<UniquePtr<TimeoutInfo>> mTimeouts;

    nsCOMPtr<nsITimer> mTimer;
    nsCOMPtr<nsITimerCallback> mTimerRunnable;

    nsCOMPtr<nsITimer> mPeriodicGCTimer;
    nsCOMPtr<nsITimer> mIdleGCTimer;

    RefPtr<MemoryReporter> mMemoryReporter;

    // While running a nested event loop, whether a sync loop or a debugger
    // event loop we want to keep track of which global is running it, if any,
    // so runnables that run off that event loop can get at that information. In
    // practice this only matters for various worker debugger runnables running
    // against sandboxes, because all other runnables know which globals they
    // belong to already.  We could also address this by threading the relevant
    // global through the chains of runnables involved, but we'd need to thread
    // it through some runnables that run on the main thread, and that would
    // require some care to make sure things get released on the correct thread,
    // which we'd rather avoid.  This member is only accessed on the worker
    // thread.
    nsCOMPtr<nsIGlobalObject> mCurrentEventLoopGlobal;

    // Timer that triggers an interrupt on expiration of the current time slice
    nsCOMPtr<nsITimer> mTSTimer;

    // Execution manager used to regulate execution for this worker.
    RefPtr<JSExecutionManager> mExecutionManager;

    // Used to relinguish clearance for CTypes Callbacks.
    nsTArray<AutoYieldJSThreadExecution> mYieldJSThreadExecution;

    uint32_t mNumWorkerRefsPreventingShutdownStart;
    uint32_t mDebuggerEventLoopLevel;

    // This is the count of background actors that binding with IPCWorkerRefs.
    // This count would be used in WorkerPrivate::UpdateCCFlag for checking if
    // CC should be blocked by background actors.
    uint32_t mNonblockingCCBackgroundActorCount;

    uint32_t mErrorHandlerRecursionCount;
    int32_t mNextTimeoutId;

    // Tracks the current setTimeout/setInterval nesting level.
    // When there isn't a TimeoutHandler on the stack, this will be 0.
    // Whenever setTimeout/setInterval are called, a new TimeoutInfo will be
    // created with a nesting level one more than the current nesting level,
    // saturating at the kClampTimeoutNestingLevel.
    //
    // When RunExpiredTimeouts is run, it sets this value to the
    // TimeoutInfo::mNestingLevel for the duration of
    // the WorkerScriptTimeoutHandler::Call which will explicitly trigger a
    // microtask checkpoint so that any immediately-resolved promises will
    // still see the nesting level.
    uint32_t mCurrentTimerNestingLevel;

    bool mFrozen;

    // This flag is set by the debugger interrupt control runnable to indicate
    // that we want to process debugger runnables as part of control runnable
    // processing, which is something we don't normally want to do. The flag is
    // cleared after processing the debugger runnables.
    bool mDebuggerInterruptRequested;

    bool mTimerRunning;
    bool mRunningExpiredTimeouts;
    bool mPeriodicGCTimerRunning;
    bool mIdleGCTimerRunning;
    bool mOnLine;
    bool mJSThreadExecutionGranted;
    bool mCCCollectedAnything;
    FlippedOnce<false> mDeletionScheduled;
    FlippedOnce<false> mCancelBeforeWorkerScopeConstructed;
    FlippedOnce<false> mPerformedShutdownAfterLastContentTaskExecuted;
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
    bool mIsPotentiallyLastGCCCRunning = false;
#endif
  };
  ThreadBound<WorkerThreadAccessible> mWorkerThreadAccessible;

  class MOZ_RAII AutoPushEventLoopGlobal {
   public:
    AutoPushEventLoopGlobal(WorkerPrivate* aWorkerPrivate, JSContext* aCx);
    ~AutoPushEventLoopGlobal();

   private:
    nsCOMPtr<nsIGlobalObject> mOldEventLoopGlobal;

#ifdef DEBUG
    // This is used to checking if we are on the right stack while push the
    // mOldEventLoopGlobal back.
    nsCOMPtr<nsIGlobalObject> mNewEventLoopGlobal;
#endif
  };
  friend class AutoPushEventLoopGlobal;

  uint32_t mPostSyncLoopOperations;

  // List of operations to do at the end of the last sync event loop.
  enum {
    eDispatchCancelingRunnable = 0x02,
  };

  bool mParentWindowPaused;

  bool mWorkerScriptExecutedSuccessfully;
  bool mFetchHandlerWasAdded;
  bool mMainThreadObjectsForgotten;
  bool mIsChromeWorker;
  bool mParentFrozen;

  // In order to ensure that the debugger can interrupt a busy worker,
  // including when atomics are used, we reuse the worker's existing
  // JS Interrupt facility used by control runnables.
  // Rather that triggering an interrupt immediately when a debugger runnable
  // is dispatched, we instead start a timer that will fire if we haven't
  // already processed the runnable, targeting the timer's callback at the
  // control event target. This allows existing runnables that were going to
  // finish in a timely fashion to finish.
  nsCOMPtr<nsITimer> mDebuggerInterruptTimer MOZ_GUARDED_BY(mMutex);

  // mIsSecureContext is set once in our constructor; after that it can be read
  // from various threads.
  //
  // It's a bit unfortunate that we have to have an out-of-band boolean for
  // this, but we need access to this state from the parent thread, and we can't
  // use our global object's secure state there.
  const bool mIsSecureContext;

  bool mDebuggerRegistered MOZ_GUARDED_BY(mMutex);
  mozilla::Atomic<bool> mIsInBackground;

  // During registration, this worker may be marked as not being ready to
  // execute debuggee runnables or content.
  //
  // Protected by mMutex.
  bool mDebuggerReady;
  nsTArray<RefPtr<WorkerRunnable>> mDelayedDebuggeeRunnables;

  // Whether this worker should have access to the WebExtension API bindings
  // (currently only the Extension Background ServiceWorker declared in the
  // extension manifest is allowed to access any WebExtension API bindings).
  // This default to false, and it is eventually set to true by
  // RemoteWorkerChild::ExecWorkerOnMainThread if the needed conditions
  // are met.
  bool mExtensionAPIAllowed;

  // mIsInAutomation is true when we're running in test automation.
  // We expose some extra testing functions in that case.
  bool mIsInAutomation;

  nsString mId;

  // This is used to check if it's allowed to share the memory across the agent
  // cluster.
  const nsILoadInfo::CrossOriginOpenerPolicy mAgentClusterOpenerPolicy;

  // Member variable of this class rather than the worker global scope because
  // it's received on the main thread, but the global scope is thread-bound
  // to the worker thread, so storing the value in the global scope would
  // involve sacrificing the thread-bound-ness or using a WorkerRunnable, and
  // there isn't a strong reason to store it on the global scope other than
  // better consistency with the COEP spec.
  Maybe<nsILoadInfo::CrossOriginEmbedderPolicy> mEmbedderPolicy;
  Maybe<nsILoadInfo::CrossOriginEmbedderPolicy> mOwnerEmbedderPolicy;

  /* Privileged add-on flag extracted from the AddonPolicy on the nsIPrincipal
   * on the main thread when constructing a top-level worker. The flag is
   * propagated to nested workers. The flag is only allowed to take effect in
   * extension processes and is forbidden in content scripts in content
   * processes. The flag may be read on either the parent/owner thread as well
   * as on the worker thread itself. When bug 1443925 is fixed allowing
   * nsIPrincipal to be used OMT, it may be possible to remove this flag. */
  bool mIsPrivilegedAddonGlobal;

  Atomic<uint32_t> mTopLevelWorkerFinishedRunnableCount;
  Atomic<uint32_t> mWorkerFinishedRunnableCount;

  nsTArray<nsCOMPtr<nsITargetShutdownTask>> mShutdownTasks
      MOZ_GUARDED_BY(mMutex);
  bool mShutdownTasksRun MOZ_GUARDED_BY(mMutex) = false;

  bool mCCFlagSaysEligible MOZ_GUARDED_BY(mMutex){true};

  // The flag indicates if the worke is idle for events in the main event loop.
  bool mWorkerLoopIsIdle MOZ_GUARDED_BY(mMutex){false};

  // This flag is used to ensure we only call NotifyStorageKeyUsed once per
  // global.
  bool hasNotifiedStorageKeyUsed{false};

  RefPtr<WorkerParentRef> mParentRef;
};

class AutoSyncLoopHolder {
  RefPtr<StrongWorkerRef> mWorkerRef;
  nsCOMPtr<nsISerialEventTarget> mTarget;
  uint32_t mIndex;

 public:
  // See CreateNewSyncLoop() for more information about the correct value to use
  // for aFailStatus.
  AutoSyncLoopHolder(WorkerPrivate* aWorkerPrivate, WorkerStatus aFailStatus,
                     const char* const aName = "AutoSyncLoopHolder");

  ~AutoSyncLoopHolder();

  nsresult Run();

  nsISerialEventTarget* GetSerialEventTarget() const;
};

/**
 * WorkerParentRef is a RefPtr<WorkerPrivate> wrapper for cross-thread access.
 * WorkerPrivate needs to be accessed in multiple threads; for example,
 * in WorkerParentThreadRunnable, the associated WorkerPrivate must be accessed
 * in the worker thread when creating/dispatching and in the parent thread when
 * executing. Unfortunately, RefPtr can not be used on this WorkerPrivate since
 * it is not a thread-safe ref-counted object.
 *
 * Instead of using a raw pointer and a complicated mechanism to ensure the
 * WorkerPrivate's accessibility. WorkerParentRef is used to resolve the
 * problem. WorkerParentRef has a RefPtr<WorkerPrivate> mWorkerPrivate
 * initialized on the parent thread when WorkerPrivate::Constructor().
 * WorkerParentRef is a thread-safe ref-counted object that can be copied at
 * any thread by WorkerPrivate::GetWorkerParentRef() and propagated to other
 * threads. In the target thread, call WorkerParentRef::Private() to get the
 * reference for WorkerPrivate or get a nullptr if the Worker has shut down.
 *
 * Since currently usage cases, WorkerParentRef::Private() will assert to be on
 * the parent thread.
 */
class WorkerParentRef final {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(WorkerParentRef);

  explicit WorkerParentRef(RefPtr<WorkerPrivate>& aWorkerPrivate);

  const RefPtr<WorkerPrivate>& Private() const;

  void DropWorkerPrivate();

 private:
  ~WorkerParentRef();

  RefPtr<WorkerPrivate> mWorkerPrivate;
};

}  // namespace dom
}  // namespace mozilla

#endif /* mozilla_dom_workers_workerprivate_h__ */
