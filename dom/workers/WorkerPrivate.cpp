/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WorkerPrivate.h"

#include <utility>

#include "js/CallAndConstruct.h"  // JS_CallFunctionValue
#include "js/CompilationAndEvaluation.h"
#include "js/ContextOptions.h"
#include "js/Exception.h"
#include "js/friend/ErrorMessages.h"  // JSMSG_OUT_OF_MEMORY
#include "js/LocaleSensitive.h"
#include "js/MemoryMetrics.h"
#include "js/SourceText.h"
#include "MessageEventRunnable.h"
#include "mozilla/AntiTrackingUtils.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/ExtensionPolicyService.h"
#include "mozilla/Mutex.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/Result.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/BrowsingContextGroup.h"
#include "mozilla/dom/CallbackDebuggerNotification.h"
#include "mozilla/dom/ClientManager.h"
#include "mozilla/dom/ClientState.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/Console.h"
#include "mozilla/dom/DocGroup.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/DOMTypes.h"
#include "mozilla/dom/Event.h"
#include "mozilla/dom/Exceptions.h"
#include "mozilla/dom/FunctionBinding.h"
#include "mozilla/dom/IndexedDatabaseManager.h"
#include "mozilla/dom/MessageEvent.h"
#include "mozilla/dom/MessageEventBinding.h"
#include "mozilla/dom/MessagePort.h"
#include "mozilla/dom/MessagePortBinding.h"
#include "mozilla/dom/nsCSPContext.h"
#include "mozilla/dom/nsCSPUtils.h"
#include "mozilla/dom/Performance.h"
#include "mozilla/dom/PerformanceStorageWorker.h"
#include "mozilla/dom/PromiseDebugging.h"
#include "mozilla/dom/PRemoteWorkerDebuggerParent.h"
#include "mozilla/dom/ReferrerInfo.h"
#include "mozilla/dom/RemoteWorkerChild.h"
#include "mozilla/dom/RemoteWorkerDebuggerChild.h"
#include "mozilla/dom/RemoteWorkerNonLifeCycleOpControllerChild.h"
#include "mozilla/dom/RemoteWorkerService.h"
#include "mozilla/dom/RootedDictionary.h"
#include "mozilla/dom/SimpleGlobalObject.h"
#include "mozilla/dom/TimeoutHandler.h"
#include "mozilla/dom/UseCounterMetrics.h"
#include "mozilla/dom/WorkerBinding.h"
#include "mozilla/dom/WorkerScope.h"
#include "mozilla/dom/WorkerStatus.h"
#include "mozilla/dom/WebTaskScheduler.h"
#include "mozilla/dom/JSExecutionManager.h"
#include "mozilla/dom/WindowContext.h"
#include "mozilla/extensions/ExtensionBrowser.h"  // extensions::Create{AndDispatchInitWorkerContext,WorkerLoaded,WorkerDestroyed}Runnable
#include "mozilla/extensions/WebExtensionPolicy.h"
#include "mozilla/glean/DomUseCounterMetrics.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "mozilla/StorageAccess.h"
#include "mozilla/StoragePrincipalHelper.h"
#include "mozilla/ThreadEventQueue.h"
#include "mozilla/ThreadSafety.h"
#include "mozilla/ThrottledEventQueue.h"
#include "nsCycleCollector.h"
#include "nsGlobalWindowInner.h"
#include "nsIDUtils.h"
#include "nsNetUtil.h"
#include "nsIFile.h"
#include "nsIMemoryReporter.h"
#include "nsIPermissionManager.h"
#include "nsIProtocolHandler.h"
#include "nsIScriptError.h"
#include "nsIURI.h"
#include "nsIURL.h"
#include "nsIUUIDGenerator.h"
#include "nsPrintfCString.h"
#include "nsProxyRelease.h"
#include "nsQueryObject.h"
#include "nsRFPService.h"
#include "nsSandboxFlags.h"
#include "nsThreadUtils.h"
#include "nsUTF8Utils.h"

#include "RuntimeService.h"
#include "ScriptLoader.h"
#include "mozilla/dom/ServiceWorkerEvents.h"
#include "mozilla/dom/ServiceWorkerManager.h"
#include "mozilla/net/CookieJarSettings.h"
#include "WorkerCSPEventListener.h"
#include "WorkerDebugger.h"
#include "WorkerDebuggerManager.h"
#include "WorkerError.h"
#include "WorkerEventTarget.h"
#include "WorkerNavigator.h"
#include "WorkerRef.h"
#include "WorkerRunnable.h"
#include "WorkerThread.h"
#include "nsContentSecurityManager.h"

#include "nsThreadManager.h"

#ifdef XP_WIN
#  undef PostMessage
#endif

// JS_MaybeGC will run once every second during normal execution.
#define PERIODIC_GC_TIMER_DELAY_SEC 1

// A shrinking GC will run five seconds after the last event is processed.
#define IDLE_GC_TIMER_DELAY_SEC 5

// Arbitrary short grace period for the currently running task to finish.
// There isn't an advantage for us to immediately interrupt JS in the middle of
// execution that might yield soon, especially when there is so much async
// variability in the data flow prior to us deciding to trigger the interrupt.
#define DEBUGGER_RUNNABLE_INTERRUPT_AFTER_MS 250

static mozilla::LazyLogModule sWorkerPrivateLog("WorkerPrivate");
static mozilla::LazyLogModule sWorkerTimeoutsLog("WorkerTimeouts");

mozilla::LogModule* WorkerLog() { return sWorkerPrivateLog; }

mozilla::LogModule* TimeoutsLog() { return sWorkerTimeoutsLog; }

#ifdef LOG
#  undef LOG
#endif
#ifdef LOGV
#  undef LOGV
#endif
#define LOG(log, _args) MOZ_LOG(log, LogLevel::Debug, _args);
#define LOGV(args) MOZ_LOG(sWorkerPrivateLog, LogLevel::Verbose, args);

namespace mozilla {

using namespace ipc;

namespace dom {

using namespace workerinternals;

MOZ_DEFINE_MALLOC_SIZE_OF(JsWorkerMallocSizeOf)

namespace {

#ifdef DEBUG

const nsIID kDEBUGWorkerEventTargetIID = {
    0xccaba3fa,
    0x5be2,
    0x4de2,
    {0xba, 0x87, 0x3b, 0x3b, 0x5b, 0x1d, 0x5, 0xfb}};

#endif

template <class T>
class UniquePtrComparator {
  using A = UniquePtr<T>;
  using B = T*;

 public:
  bool Equals(const A& a, const A& b) const {
    return (a && b) ? (*a == *b) : (!a && !b);
  }
  bool LessThan(const A& a, const A& b) const {
    return (a && b) ? (*a < *b) : !!b;
  }
};

template <class T>
inline UniquePtrComparator<T> GetUniquePtrComparator(
    const nsTArray<UniquePtr<T>>&) {
  return UniquePtrComparator<T>();
}

// This class is used to wrap any runnables that the worker receives via the
// nsIEventTarget::Dispatch() method (either from NS_DispatchToCurrentThread or
// from the worker's EventTarget).
class ExternalRunnableWrapper final : public WorkerThreadRunnable {
  nsCOMPtr<nsIRunnable> mWrappedRunnable;

 public:
  ExternalRunnableWrapper(WorkerPrivate* aWorkerPrivate,
                          nsIRunnable* aWrappedRunnable)
      : WorkerThreadRunnable("ExternalRunnableWrapper"),
        mWrappedRunnable(aWrappedRunnable) {
    MOZ_ASSERT(aWorkerPrivate);
    MOZ_ASSERT(aWrappedRunnable);
  }

  NS_INLINE_DECL_REFCOUNTING_INHERITED(ExternalRunnableWrapper,
                                       WorkerThreadRunnable)

 private:
  ~ExternalRunnableWrapper() = default;

  virtual bool PreDispatch(WorkerPrivate* aWorkerPrivate) override {
    // Silence bad assertions.
    return true;
  }

  virtual void PostDispatch(WorkerPrivate* aWorkerPrivate,
                            bool aDispatchResult) override {
    // Silence bad assertions.
  }

  virtual bool WorkerRun(JSContext* aCx,
                         WorkerPrivate* aWorkerPrivate) override {
    nsresult rv = mWrappedRunnable->Run();
    mWrappedRunnable = nullptr;
    if (NS_FAILED(rv)) {
      if (!JS_IsExceptionPending(aCx)) {
        Throw(aCx, rv);
      }
      return false;
    }
    return true;
  }

  nsresult Cancel() override {
    nsCOMPtr<nsIDiscardableRunnable> doomed =
        do_QueryInterface(mWrappedRunnable);
    if (doomed) {
      doomed->OnDiscard();
    }
    mWrappedRunnable = nullptr;
    return NS_OK;
  }

#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
  NS_IMETHOD GetName(nsACString& aName) override {
    aName.AssignLiteral("ExternalRunnableWrapper(");
    if (nsCOMPtr<nsINamed> named = do_QueryInterface(mWrappedRunnable)) {
      nsAutoCString containedName;
      named->GetName(containedName);
      aName.Append(containedName);
    } else {
      aName.AppendLiteral("?");
    }
    aName.AppendLiteral(")");
    return NS_OK;
  }
#endif
};

struct WindowAction {
  nsPIDOMWindowInner* mWindow;
  bool mDefaultAction;

  MOZ_IMPLICIT WindowAction(nsPIDOMWindowInner* aWindow)
      : mWindow(aWindow), mDefaultAction(true) {}

  bool operator==(const WindowAction& aOther) const {
    return mWindow == aOther.mWindow;
  }
};

class WorkerFinishedRunnable final : public WorkerControlRunnable {
  WorkerPrivate* mFinishedWorker;

 public:
  WorkerFinishedRunnable(WorkerPrivate* aWorkerPrivate,
                         WorkerPrivate* aFinishedWorker)
      : WorkerControlRunnable("WorkerFinishedRunnable"),
        mFinishedWorker(aFinishedWorker) {
    aFinishedWorker->IncreaseWorkerFinishedRunnableCount();
  }

 private:
  virtual bool PreDispatch(WorkerPrivate* aWorkerPrivate) override {
    // Silence bad assertions.
    return true;
  }

  virtual void PostDispatch(WorkerPrivate* aWorkerPrivate,
                            bool aDispatchResult) override {
    // Silence bad assertions.
  }

  virtual bool WorkerRun(JSContext* aCx,
                         WorkerPrivate* aWorkerPrivate) override {
    // This may block on the main thread.
    AutoYieldJSThreadExecution yield;

    mFinishedWorker->DecreaseWorkerFinishedRunnableCount();

    if (!mFinishedWorker->ProxyReleaseMainThreadObjects()) {
      NS_WARNING("Failed to dispatch, going to leak!");
    }

    RuntimeService* runtime = RuntimeService::GetService();
    NS_ASSERTION(runtime, "This should never be null!");

    mFinishedWorker->DisableDebugger();

    runtime->UnregisterWorker(*mFinishedWorker);

    mFinishedWorker->ClearSelfAndParentEventTargetRef();
    return true;
  }
};

class TopLevelWorkerFinishedRunnable final : public Runnable {
  WorkerPrivate* mFinishedWorker;

 public:
  explicit TopLevelWorkerFinishedRunnable(WorkerPrivate* aFinishedWorker)
      : mozilla::Runnable("TopLevelWorkerFinishedRunnable"),
        mFinishedWorker(aFinishedWorker) {
    aFinishedWorker->AssertIsOnWorkerThread();
    aFinishedWorker->IncreaseTopLevelWorkerFinishedRunnableCount();
  }

  NS_INLINE_DECL_REFCOUNTING_INHERITED(TopLevelWorkerFinishedRunnable, Runnable)

 private:
  ~TopLevelWorkerFinishedRunnable() = default;

  NS_IMETHOD
  Run() override {
    AssertIsOnMainThread();

    mFinishedWorker->DecreaseTopLevelWorkerFinishedRunnableCount();

    RuntimeService* runtime = RuntimeService::GetService();
    MOZ_ASSERT(runtime);

    mFinishedWorker->DisableDebugger();

    runtime->UnregisterWorker(*mFinishedWorker);

    if (!mFinishedWorker->ProxyReleaseMainThreadObjects()) {
      NS_WARNING("Failed to dispatch, going to leak!");
    }

    mFinishedWorker->ClearSelfAndParentEventTargetRef();
    return NS_OK;
  }
};

class CompileScriptRunnable final : public WorkerDebuggeeRunnable {
  nsString mScriptURL;
  const mozilla::Encoding* mDocumentEncoding;
  UniquePtr<SerializedStackHolder> mOriginStack;

 public:
  explicit CompileScriptRunnable(WorkerPrivate* aWorkerPrivate,
                                 UniquePtr<SerializedStackHolder> aOriginStack,
                                 const nsAString& aScriptURL,
                                 const mozilla::Encoding* aDocumentEncoding)
      : WorkerDebuggeeRunnable("CompileScriptRunnable"),
        mScriptURL(aScriptURL),
        mDocumentEncoding(aDocumentEncoding),
        mOriginStack(aOriginStack.release()) {}

 private:
  // We can't implement PreRun effectively, because at the point when that would
  // run we have not yet done our load so don't know things like our final
  // principal and whatnot.

  virtual bool WorkerRun(JSContext* aCx,
                         WorkerPrivate* aWorkerPrivate) override {
    aWorkerPrivate->AssertIsOnWorkerThread();

    WorkerGlobalScope* globalScope =
        aWorkerPrivate->GetOrCreateGlobalScope(aCx);
    if (NS_WARN_IF(!globalScope)) {
      return false;
    }

    if (NS_WARN_IF(!aWorkerPrivate->EnsureCSPEventListener())) {
      return false;
    }

    ErrorResult rv;
    workerinternals::LoadMainScript(aWorkerPrivate, std::move(mOriginStack),
                                    mScriptURL, WorkerScript, rv,
                                    mDocumentEncoding);

    if (aWorkerPrivate->ExtensionAPIAllowed()) {
      MOZ_ASSERT(aWorkerPrivate->IsServiceWorker());
      RefPtr<Runnable> extWorkerRunnable =
          extensions::CreateWorkerLoadedRunnable(
              aWorkerPrivate->ServiceWorkerID(), aWorkerPrivate->GetBaseURI());
      // Dispatch as a low priority runnable.
      if (NS_FAILED(aWorkerPrivate->DispatchToMainThreadForMessaging(
              extWorkerRunnable.forget()))) {
        NS_WARNING(
            "Failed to dispatch runnable to notify extensions worker loaded");
      }
    }

    rv.WouldReportJSException();
    // Explicitly ignore NS_BINDING_ABORTED on rv.  Or more precisely, still
    // return false and don't SetWorkerScriptExecutedSuccessfully() in that
    // case, but don't throw anything on aCx.  The idea is to not dispatch error
    // events if our load is canceled with that error code.
    if (rv.ErrorCodeIs(NS_BINDING_ABORTED)) {
      rv.SuppressException();
      return false;
    }

    // Make sure to propagate exceptions from rv onto aCx, so that they will get
    // reported after we return.  We want to propagate just JS exceptions,
    // because all the other errors are handled when the script is loaded.
    // See: https://dom.spec.whatwg.org/#concept-event-fire
    if (rv.Failed() && !rv.IsJSException()) {
      WorkerErrorReport::CreateAndDispatchGenericErrorRunnableToParent(
          aWorkerPrivate);
      rv.SuppressException();
      return false;
    }

    // This is a little dumb, but aCx is in the null realm here because we
    // set it up that way in our Run(), since we had not created the global at
    // that point yet.  So we need to enter the realm of our global,
    // because setting a pending exception on aCx involves wrapping into its
    // current compartment.  Luckily we have a global now.
    JSAutoRealm ar(aCx, globalScope->GetGlobalJSObject());
    if (rv.MaybeSetPendingException(aCx)) {
      // In the event of an uncaught exception, the worker should still keep
      // running (return true) but should not be marked as having executed
      // successfully (which will cause ServiceWorker installation to fail).
      // In previous error handling cases in this method, we return false (to
      // trigger CloseInternal) because the global is not in an operable
      // state at all.
      //
      // For ServiceWorkers, this would correspond to the "Run Service Worker"
      // algorithm returning an "abrupt completion" and _not_ failure.
      //
      // For DedicatedWorkers and SharedWorkers, this would correspond to the
      // "run a worker" algorithm disregarding the return value of "run the
      // classic script"/"run the module script" in step 24:
      //
      // "If script is a classic script, then run the classic script script.
      // Otherwise, it is a module script; run the module script script."
      return true;
    }

    aWorkerPrivate->SetWorkerScriptExecutedSuccessfully();
    return true;
  }

  void PostRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate,
               bool aRunResult) override {
    if (!aRunResult) {
      aWorkerPrivate->CloseInternal();
    }
    WorkerThreadRunnable::PostRun(aCx, aWorkerPrivate, aRunResult);
  }
};

class NotifyRunnable final : public WorkerControlRunnable {
  WorkerStatus mStatus;

 public:
  NotifyRunnable(WorkerPrivate* aWorkerPrivate, WorkerStatus aStatus)
      : WorkerControlRunnable("NotifyRunnable"), mStatus(aStatus) {
    MOZ_ASSERT(aStatus == Closing || aStatus == Canceling ||
               aStatus == Killing);
  }

 private:
  virtual bool PreDispatch(WorkerPrivate* aWorkerPrivate) override {
    aWorkerPrivate->AssertIsOnParentThread();
    return true;
  }

  virtual void PostDispatch(WorkerPrivate* aWorkerPrivate,
                            bool aDispatchResult) override {
    aWorkerPrivate->AssertIsOnParentThread();
  }

  virtual bool WorkerRun(JSContext* aCx,
                         WorkerPrivate* aWorkerPrivate) override {
    return aWorkerPrivate->NotifyInternal(mStatus);
  }

  virtual void PostRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate,
                       bool aRunResult) override {}
};

class FreezeRunnable final : public WorkerControlRunnable {
 public:
  explicit FreezeRunnable(WorkerPrivate* aWorkerPrivate)
      : WorkerControlRunnable("FreezeRunnable") {}

 private:
  virtual bool WorkerRun(JSContext* aCx,
                         WorkerPrivate* aWorkerPrivate) override {
    return aWorkerPrivate->FreezeInternal();
  }
};

class ThawRunnable final : public WorkerControlRunnable {
 public:
  explicit ThawRunnable(WorkerPrivate* aWorkerPrivate)
      : WorkerControlRunnable("ThawRunnable") {}

 private:
  virtual bool WorkerRun(JSContext* aCx,
                         WorkerPrivate* aWorkerPrivate) override {
    return aWorkerPrivate->ThawInternal();
  }
};

class ChangeBackgroundStateRunnable final : public WorkerControlRunnable {
 public:
  ChangeBackgroundStateRunnable() = delete;
  explicit ChangeBackgroundStateRunnable(WorkerPrivate* aWorkerPrivate) =
      delete;
  ChangeBackgroundStateRunnable(WorkerPrivate* aWorkerPrivate,
                                bool aIsBackground)
      : WorkerControlRunnable("ChangeBackgroundStateRunnable"),
        mIsBackground(aIsBackground) {}

 private:
  bool mIsBackground = false;
  virtual bool WorkerRun(JSContext* aCx,
                         WorkerPrivate* aWorkerPrivate) override {
    return aWorkerPrivate->ChangeBackgroundStateInternal(mIsBackground);
  }
};

class PropagateStorageAccessPermissionGrantedRunnable final
    : public WorkerControlRunnable {
 public:
  explicit PropagateStorageAccessPermissionGrantedRunnable(
      WorkerPrivate* aWorkerPrivate)
      : WorkerControlRunnable(
            "PropagateStorageAccessPermissionGrantedRunnable") {}

 private:
  bool WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    aWorkerPrivate->PropagateStorageAccessPermissionGrantedInternal();
    return true;
  }
};

class ReportErrorToConsoleRunnable final : public WorkerParentThreadRunnable {
 public:
  // aWorkerPrivate is the worker thread we're on (or the main thread, if null)
  static void Report(WorkerPrivate* aWorkerPrivate, uint32_t aErrorFlags,
                     const nsCString& aCategory,
                     nsContentUtils::PropertiesFile aFile,
                     const nsCString& aMessageName,
                     const nsTArray<nsString>& aParams,
                     const mozilla::SourceLocation& aLocation) {
    if (aWorkerPrivate) {
      aWorkerPrivate->AssertIsOnWorkerThread();
    } else {
      AssertIsOnMainThread();
    }

    // Now fire a runnable to do the same on the parent's thread if we can.
    if (aWorkerPrivate) {
      RefPtr<ReportErrorToConsoleRunnable> runnable =
          new ReportErrorToConsoleRunnable(aWorkerPrivate, aErrorFlags,
                                           aCategory, aFile, aMessageName,
                                           aParams, aLocation);
      runnable->Dispatch(aWorkerPrivate);
      return;
    }

    // Log a warning to the console.
    nsContentUtils::ReportToConsole(aErrorFlags, aCategory, nullptr, aFile,
                                    aMessageName.get(), aParams, aLocation);
  }

 private:
  ReportErrorToConsoleRunnable(WorkerPrivate* aWorkerPrivate,
                               uint32_t aErrorFlags, const nsCString& aCategory,
                               nsContentUtils::PropertiesFile aFile,
                               const nsCString& aMessageName,
                               const nsTArray<nsString>& aParams,
                               const mozilla::SourceLocation& aLocation)
      : WorkerParentThreadRunnable("ReportErrorToConsoleRunnable"),
        mErrorFlags(aErrorFlags),
        mCategory(aCategory),
        mFile(aFile),
        mMessageName(aMessageName),
        mParams(aParams.Clone()),
        mLocation(aLocation) {}

  virtual void PostDispatch(WorkerPrivate* aWorkerPrivate,
                            bool aDispatchResult) override {
    aWorkerPrivate->AssertIsOnWorkerThread();

    // Dispatch may fail if the worker was canceled, no need to report that as
    // an error, so don't call base class PostDispatch.
  }

  virtual bool WorkerRun(JSContext* aCx,
                         WorkerPrivate* aWorkerPrivate) override {
    WorkerPrivate* parent = aWorkerPrivate->GetParent();
    MOZ_ASSERT_IF(!parent, NS_IsMainThread());
    Report(parent, mErrorFlags, mCategory, mFile, mMessageName, mParams,
           mLocation);
    return true;
  }

  const uint32_t mErrorFlags;
  const nsCString mCategory;
  const nsContentUtils::PropertiesFile mFile;
  const nsCString mMessageName;
  const nsTArray<nsString> mParams;
  const mozilla::SourceLocation mLocation;
};

class RunExpiredTimoutsRunnable final : public WorkerThreadRunnable,
                                        public nsITimerCallback {
 public:
  NS_DECL_ISUPPORTS_INHERITED

  explicit RunExpiredTimoutsRunnable(WorkerPrivate* aWorkerPrivate)
      : WorkerThreadRunnable("RunExpiredTimoutsRunnable") {}

 private:
  ~RunExpiredTimoutsRunnable() = default;

  virtual bool PreDispatch(WorkerPrivate* aWorkerPrivate) override {
    // Silence bad assertions.
    return true;
  }

  virtual void PostDispatch(WorkerPrivate* aWorkerPrivate,
                            bool aDispatchResult) override {
    // Silence bad assertions.
  }

  // MOZ_CAN_RUN_SCRIPT_BOUNDARY until worker runnables are generally
  // MOZ_CAN_RUN_SCRIPT.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  virtual bool WorkerRun(JSContext* aCx,
                         WorkerPrivate* aWorkerPrivate) override {
    return aWorkerPrivate->RunExpiredTimeouts(aCx);
  }

  NS_IMETHOD
  Notify(nsITimer* aTimer) override { return Run(); }
};

NS_IMPL_ISUPPORTS_INHERITED(RunExpiredTimoutsRunnable, WorkerThreadRunnable,
                            nsITimerCallback)

class DebuggerImmediateRunnable final : public WorkerThreadRunnable {
  RefPtr<dom::Function> mHandler;

 public:
  explicit DebuggerImmediateRunnable(WorkerPrivate* aWorkerPrivate,
                                     dom::Function& aHandler)
      : WorkerThreadRunnable("DebuggerImmediateRunnable"),
        mHandler(&aHandler) {}

 private:
  virtual bool IsDebuggerRunnable() const override { return true; }

  virtual bool PreDispatch(WorkerPrivate* aWorkerPrivate) override {
    // Silence bad assertions.
    return true;
  }

  virtual void PostDispatch(WorkerPrivate* aWorkerPrivate,
                            bool aDispatchResult) override {
    // Silence bad assertions.
  }

  // Make as MOZ_CAN_RUN_SCRIPT_BOUNDARY for calling mHandler->Call();
  // Since WorkerRunnable::WorkerRun has not to be MOZ_CAN_RUN_SCRIPT, but
  // DebuggerImmediateRunnable is a special case that must to call the function
  // defined in the debugger script.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  virtual bool WorkerRun(JSContext* aCx,
                         WorkerPrivate* aWorkerPrivate) override {
    JS::Rooted<JS::Value> rval(aCx);
    IgnoredErrorResult rv;
    MOZ_KnownLive(mHandler)->Call({}, &rval, rv);

    return !rv.Failed();
  }
};

// GetJSContext() is safe on the worker thread
void PeriodicGCTimerCallback(nsITimer* aTimer,
                             void* aClosure) MOZ_NO_THREAD_SAFETY_ANALYSIS {
  auto* workerPrivate = static_cast<WorkerPrivate*>(aClosure);
  MOZ_DIAGNOSTIC_ASSERT(workerPrivate);
  workerPrivate->AssertIsOnWorkerThread();
  workerPrivate->GarbageCollectInternal(workerPrivate->GetJSContext(),
                                        false /* shrinking */,
                                        false /* collect children */);
  LOG(WorkerLog(), ("Worker %p run periodic GC\n", workerPrivate));
}

void IdleGCTimerCallback(nsITimer* aTimer,
                         void* aClosure) MOZ_NO_THREAD_SAFETY_ANALYSIS {
  auto* workerPrivate = static_cast<WorkerPrivate*>(aClosure);
  MOZ_DIAGNOSTIC_ASSERT(workerPrivate);
  workerPrivate->AssertIsOnWorkerThread();
  workerPrivate->GarbageCollectInternal(workerPrivate->GetJSContext(),
                                        true /* shrinking */,
                                        false /* collect children */);
  LOG(WorkerLog(), ("Worker %p run idle GC\n", workerPrivate));

  // After running idle GC we can cancel the current timers.
  workerPrivate->CancelGCTimers();
}

class UpdateContextOptionsRunnable final : public WorkerControlRunnable {
  JS::ContextOptions mContextOptions;

 public:
  UpdateContextOptionsRunnable(WorkerPrivate* aWorkerPrivate,
                               const JS::ContextOptions& aContextOptions)
      : WorkerControlRunnable("UpdateContextOptionsRunnable"),
        mContextOptions(aContextOptions) {}

 private:
  virtual bool WorkerRun(JSContext* aCx,
                         WorkerPrivate* aWorkerPrivate) override {
    aWorkerPrivate->UpdateContextOptionsInternal(aCx, mContextOptions);
    return true;
  }
};

class UpdateLanguagesRunnable final : public WorkerThreadRunnable {
  nsTArray<nsString> mLanguages;

 public:
  UpdateLanguagesRunnable(WorkerPrivate* aWorkerPrivate,
                          const nsTArray<nsString>& aLanguages)
      : WorkerThreadRunnable("UpdateLanguagesRunnable"),
        mLanguages(aLanguages.Clone()) {}

  virtual bool WorkerRun(JSContext* aCx,
                         WorkerPrivate* aWorkerPrivate) override {
    aWorkerPrivate->UpdateLanguagesInternal(mLanguages);
    return true;
  }
};

class UpdateJSWorkerMemoryParameterRunnable final
    : public WorkerControlRunnable {
  Maybe<uint32_t> mValue;
  JSGCParamKey mKey;

 public:
  UpdateJSWorkerMemoryParameterRunnable(WorkerPrivate* aWorkerPrivate,
                                        JSGCParamKey aKey,
                                        Maybe<uint32_t> aValue)
      : WorkerControlRunnable("UpdateJSWorkerMemoryParameterRunnable"),
        mValue(aValue),
        mKey(aKey) {}

 private:
  virtual bool WorkerRun(JSContext* aCx,
                         WorkerPrivate* aWorkerPrivate) override {
    aWorkerPrivate->UpdateJSWorkerMemoryParameterInternal(aCx, mKey, mValue);
    return true;
  }
};

#ifdef JS_GC_ZEAL
class UpdateGCZealRunnable final : public WorkerControlRunnable {
  uint8_t mGCZeal;
  uint32_t mFrequency;

 public:
  UpdateGCZealRunnable(WorkerPrivate* aWorkerPrivate, uint8_t aGCZeal,
                       uint32_t aFrequency)
      : WorkerControlRunnable("UpdateGCZealRunnable"),
        mGCZeal(aGCZeal),
        mFrequency(aFrequency) {}

 private:
  virtual bool WorkerRun(JSContext* aCx,
                         WorkerPrivate* aWorkerPrivate) override {
    aWorkerPrivate->UpdateGCZealInternal(aCx, mGCZeal, mFrequency);
    return true;
  }
};
#endif

class SetLowMemoryStateRunnable final : public WorkerControlRunnable {
  bool mState;

 public:
  SetLowMemoryStateRunnable(WorkerPrivate* aWorkerPrivate, bool aState)
      : WorkerControlRunnable("SetLowMemoryStateRunnable"), mState(aState) {}

 private:
  virtual bool WorkerRun(JSContext* aCx,
                         WorkerPrivate* aWorkerPrivate) override {
    aWorkerPrivate->SetLowMemoryStateInternal(aCx, mState);
    return true;
  }
};

class GarbageCollectRunnable final : public WorkerControlRunnable {
  bool mShrinking;
  bool mCollectChildren;

 public:
  GarbageCollectRunnable(WorkerPrivate* aWorkerPrivate, bool aShrinking,
                         bool aCollectChildren)
      : WorkerControlRunnable("GarbageCollectRunnable"),
        mShrinking(aShrinking),
        mCollectChildren(aCollectChildren) {}

 private:
  virtual bool PreDispatch(WorkerPrivate* aWorkerPrivate) override {
    // Silence bad assertions, this can be dispatched from either the main
    // thread or the timer thread..
    return true;
  }

  virtual void PostDispatch(WorkerPrivate* aWorkerPrivate,
                            bool aDispatchResult) override {
    // Silence bad assertions, this can be dispatched from either the main
    // thread or the timer thread..
  }

  virtual bool WorkerRun(JSContext* aCx,
                         WorkerPrivate* aWorkerPrivate) override {
    aWorkerPrivate->GarbageCollectInternal(aCx, mShrinking, mCollectChildren);
    if (mShrinking) {
      // Either we've run the idle GC or explicit GC call from the parent,
      // we can cancel the current timers.
      aWorkerPrivate->CancelGCTimers();
    }
    return true;
  }
};

class CycleCollectRunnable final : public WorkerControlRunnable {
  bool mCollectChildren;

 public:
  CycleCollectRunnable(WorkerPrivate* aWorkerPrivate, bool aCollectChildren)
      : WorkerControlRunnable("CycleCollectRunnable"),
        mCollectChildren(aCollectChildren) {}

  bool WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    aWorkerPrivate->CycleCollectInternal(mCollectChildren);
    return true;
  }
};

class OfflineStatusChangeRunnable final : public WorkerThreadRunnable {
 public:
  OfflineStatusChangeRunnable(WorkerPrivate* aWorkerPrivate, bool aIsOffline)
      : WorkerThreadRunnable("OfflineStatusChangeRunnable"),
        mIsOffline(aIsOffline) {}

  bool WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    aWorkerPrivate->OfflineStatusChangeEventInternal(mIsOffline);
    return true;
  }

 private:
  bool mIsOffline;
};

class MemoryPressureRunnable final : public WorkerControlRunnable {
 public:
  explicit MemoryPressureRunnable(WorkerPrivate* aWorkerPrivate)
      : WorkerControlRunnable("MemoryPressureRunnable") {}

  bool WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    aWorkerPrivate->MemoryPressureInternal();
    return true;
  }
};

class DisableRemoteDebuggerRunnable final : public WorkerControlRunnable {
 public:
  explicit DisableRemoteDebuggerRunnable(WorkerPrivate* aWorkerPrivate)
      : WorkerControlRunnable("DisableRemoteDebuggerRunnable") {}

  bool WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    aWorkerPrivate->DisableRemoteDebuggerOnWorkerThread();
    return true;
  }
};

#ifdef DEBUG
static bool StartsWithExplicit(nsACString& s) {
  return StringBeginsWith(s, "explicit/"_ns);
}
#endif

PRThread* PRThreadFromThread(nsIThread* aThread) {
  MOZ_ASSERT(aThread);

  PRThread* result;
  MOZ_ALWAYS_SUCCEEDS(aThread->GetPRThread(&result));
  MOZ_ASSERT(result);

  return result;
}

// A runnable to cancel the worker from the parent thread when self.close() is
// called. This runnable is executed on the parent process in order to cancel
// the current runnable. It uses a normal WorkerDebuggeeRunnable in order to be
// sure that all the pending WorkerDebuggeeRunnables are executed before this.
class CancelingOnParentRunnable final : public WorkerParentDebuggeeRunnable {
 public:
  explicit CancelingOnParentRunnable(WorkerPrivate* aWorkerPrivate)
      : WorkerParentDebuggeeRunnable("CancelingOnParentRunnable") {}

  bool WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    aWorkerPrivate->Cancel();
    return true;
  }
};

// A runnable to cancel the worker from the parent process.
class CancelingWithTimeoutOnParentRunnable final
    : public WorkerParentControlRunnable {
 public:
  explicit CancelingWithTimeoutOnParentRunnable(WorkerPrivate* aWorkerPrivate)
      : WorkerParentControlRunnable("CancelingWithTimeoutOnParentRunnable") {}

  bool WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    aWorkerPrivate->AssertIsOnParentThread();
    aWorkerPrivate->StartCancelingTimer();
    return true;
  }
};

class CancelingTimerCallback final : public nsITimerCallback {
 public:
  NS_DECL_ISUPPORTS

  explicit CancelingTimerCallback(WorkerPrivate* aWorkerPrivate)
      : mWorkerPrivate(aWorkerPrivate) {}

  NS_IMETHOD
  Notify(nsITimer* aTimer) override {
    mWorkerPrivate->AssertIsOnParentThread();
    mWorkerPrivate->Cancel();
    return NS_OK;
  }

 private:
  ~CancelingTimerCallback() = default;

  // Raw pointer here is OK because the timer is canceled during the shutdown
  // steps.
  WorkerPrivate* mWorkerPrivate;
};

NS_IMPL_ISUPPORTS(CancelingTimerCallback, nsITimerCallback)

// This runnable starts the canceling of a worker after a self.close().
class CancelingRunnable final : public Runnable {
 public:
  CancelingRunnable() : Runnable("CancelingRunnable") {}

  NS_IMETHOD
  Run() override {
    LOG(WorkerLog(), ("CancelingRunnable::Run [%p]", this));
    WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
    MOZ_ASSERT(workerPrivate);
    workerPrivate->AssertIsOnWorkerThread();

    // Now we can cancel the this worker from the parent process.
    RefPtr<CancelingOnParentRunnable> r =
        new CancelingOnParentRunnable(workerPrivate);
    r->Dispatch(workerPrivate);

    return NS_OK;
  }
};

} /* anonymous namespace */

nsString ComputeWorkerPrivateId() {
  nsID uuid = nsID::GenerateUUID();
  return NSID_TrimBracketsUTF16(uuid);
}

class WorkerPrivate::EventTarget final : public nsISerialEventTarget {
  // This mutex protects mWorkerPrivate and must be acquired *before* the
  // WorkerPrivate's mutex whenever they must both be held.
  mozilla::Mutex mMutex;
  WorkerPrivate* mWorkerPrivate MOZ_GUARDED_BY(mMutex);
  nsCOMPtr<nsIEventTarget> mNestedEventTarget MOZ_GUARDED_BY(mMutex);
  bool mDisabled MOZ_GUARDED_BY(mMutex);
  bool mShutdown MOZ_GUARDED_BY(mMutex);

 public:
  EventTarget(WorkerPrivate* aWorkerPrivate, nsIEventTarget* aNestedEventTarget)
      : mMutex("WorkerPrivate::EventTarget::mMutex"),
        mWorkerPrivate(aWorkerPrivate),
        mNestedEventTarget(aNestedEventTarget),
        mDisabled(false),
        mShutdown(false) {
    MOZ_ASSERT(aWorkerPrivate);
    MOZ_ASSERT(aNestedEventTarget);
  }

  void Disable() {
    {
      MutexAutoLock lock(mMutex);

      // Note, Disable() can be called more than once safely.
      mDisabled = true;
    }
  }

  void Shutdown() {
    nsCOMPtr<nsIEventTarget> nestedEventTarget;
    {
      MutexAutoLock lock(mMutex);

      mWorkerPrivate = nullptr;
      mNestedEventTarget.swap(nestedEventTarget);
      MOZ_ASSERT(mDisabled);
      mShutdown = true;
    }
  }

  RefPtr<nsIEventTarget> GetNestedEventTarget() {
    RefPtr<nsIEventTarget> nestedEventTarget = nullptr;
    {
      MutexAutoLock lock(mMutex);
      if (mWorkerPrivate) {
        mWorkerPrivate->AssertIsOnWorkerThread();
        nestedEventTarget = mNestedEventTarget.get();
      }
    }
    return nestedEventTarget;
  }

  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIEVENTTARGET_FULL

 private:
  ~EventTarget() = default;
};

struct WorkerPrivate::TimeoutInfo {
  TimeoutInfo()
      : mId(0),
        mNestingLevel(0),
        mReason(Timeout::Reason::eTimeoutOrInterval),
        mIsInterval(false),
        mCanceled(false),
        mOnChromeWorker(false) {
    MOZ_COUNT_CTOR(mozilla::dom::WorkerPrivate::TimeoutInfo);
  }

  ~TimeoutInfo() { MOZ_COUNT_DTOR(mozilla::dom::WorkerPrivate::TimeoutInfo); }

  bool operator==(const TimeoutInfo& aOther) const {
    return mTargetTime == aOther.mTargetTime;
  }

  bool operator<(const TimeoutInfo& aOther) const {
    return mTargetTime < aOther.mTargetTime;
  }

  void AccumulateNestingLevel(const uint32_t& aBaseLevel) {
    if (aBaseLevel < StaticPrefs::dom_clamp_timeout_nesting_level_AtStartup()) {
      mNestingLevel = aBaseLevel + 1;
      return;
    }
    mNestingLevel = StaticPrefs::dom_clamp_timeout_nesting_level_AtStartup();
  }

  void CalculateTargetTime() {
    auto target = mInterval;
    // Don't clamp timeout for chrome workers
    if (mNestingLevel >=
            StaticPrefs::dom_clamp_timeout_nesting_level_AtStartup() &&
        !mOnChromeWorker) {
      target = TimeDuration::Max(
          mInterval,
          TimeDuration::FromMilliseconds(StaticPrefs::dom_min_timeout_value()));
    }
    mTargetTime = TimeStamp::Now() + target;
  }

  RefPtr<TimeoutHandler> mHandler;
  mozilla::TimeStamp mTargetTime;
  mozilla::TimeDuration mInterval;
  int32_t mId;
  uint32_t mNestingLevel;
  Timeout::Reason mReason;
  bool mIsInterval;
  bool mCanceled;
  bool mOnChromeWorker;
};

class WorkerJSContextStats final : public JS::RuntimeStats {
  const nsCString mRtPath;

 public:
  explicit WorkerJSContextStats(const nsACString& aRtPath)
      : JS::RuntimeStats(JsWorkerMallocSizeOf), mRtPath(aRtPath) {}

  ~WorkerJSContextStats() {
    for (JS::ZoneStats& stats : zoneStatsVector) {
      delete static_cast<xpc::ZoneStatsExtras*>(stats.extra);
    }

    for (JS::RealmStats& stats : realmStatsVector) {
      delete static_cast<xpc::RealmStatsExtras*>(stats.extra);
    }
  }

  const nsCString& Path() const { return mRtPath; }

  virtual void initExtraZoneStats(JS::Zone* aZone, JS::ZoneStats* aZoneStats,
                                  const JS::AutoRequireNoGC& nogc) override {
    MOZ_ASSERT(!aZoneStats->extra);

    // ReportJSRuntimeExplicitTreeStats expects that
    // aZoneStats->extra is a xpc::ZoneStatsExtras pointer.
    xpc::ZoneStatsExtras* extras = new xpc::ZoneStatsExtras;
    extras->pathPrefix = mRtPath;
    extras->pathPrefix += nsPrintfCString("zone(0x%p)/", (void*)aZone);

    MOZ_ASSERT(StartsWithExplicit(extras->pathPrefix));

    aZoneStats->extra = extras;
  }

  virtual void initExtraRealmStats(JS::Realm* aRealm,
                                   JS::RealmStats* aRealmStats,
                                   const JS::AutoRequireNoGC& nogc) override {
    MOZ_ASSERT(!aRealmStats->extra);

    // ReportJSRuntimeExplicitTreeStats expects that
    // aRealmStats->extra is a xpc::RealmStatsExtras pointer.
    xpc::RealmStatsExtras* extras = new xpc::RealmStatsExtras;

    // This is the |jsPathPrefix|.  Each worker has exactly one realm.
    extras->jsPathPrefix.Assign(mRtPath);
    extras->jsPathPrefix +=
        nsPrintfCString("zone(0x%p)/", (void*)js::GetRealmZone(aRealm));
    extras->jsPathPrefix += "realm(web-worker)/"_ns;

    // This should never be used when reporting with workers (hence the "?!").
    extras->domPathPrefix.AssignLiteral("explicit/workers/?!/");

    MOZ_ASSERT(StartsWithExplicit(extras->jsPathPrefix));
    MOZ_ASSERT(StartsWithExplicit(extras->domPathPrefix));

    extras->location = nullptr;

    aRealmStats->extra = extras;
  }
};

class WorkerPrivate::MemoryReporter final : public nsIMemoryReporter {
  NS_DECL_THREADSAFE_ISUPPORTS

  friend class WorkerPrivate;

  SharedMutex mMutex;
  WorkerPrivate* mWorkerPrivate;

 public:
  explicit MemoryReporter(WorkerPrivate* aWorkerPrivate)
      : mMutex(aWorkerPrivate->mMutex), mWorkerPrivate(aWorkerPrivate) {
    aWorkerPrivate->AssertIsOnWorkerThread();
  }

  NS_IMETHOD
  CollectReports(nsIHandleReportCallback* aHandleReport, nsISupports* aData,
                 bool aAnonymize) override;

 private:
  class FinishCollectRunnable;

  class CollectReportsRunnable final : public MainThreadWorkerControlRunnable {
    RefPtr<FinishCollectRunnable> mFinishCollectRunnable;
    const bool mAnonymize;

   public:
    CollectReportsRunnable(WorkerPrivate* aWorkerPrivate,
                           nsIHandleReportCallback* aHandleReport,
                           nsISupports* aHandlerData, bool aAnonymize,
                           const nsACString& aPath);

   private:
    bool WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override;

    ~CollectReportsRunnable() {
      if (NS_IsMainThread()) {
        mFinishCollectRunnable->Run();
        return;
      }

      WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
      MOZ_ASSERT(workerPrivate);
      MOZ_ALWAYS_SUCCEEDS(workerPrivate->DispatchToMainThreadForMessaging(
          mFinishCollectRunnable.forget()));
    }
  };

  class FinishCollectRunnable final : public Runnable {
    nsCOMPtr<nsIHandleReportCallback> mHandleReport;
    nsCOMPtr<nsISupports> mHandlerData;
    size_t mPerformanceUserEntries;
    size_t mPerformanceResourceEntries;
    const bool mAnonymize;
    bool mSuccess;

   public:
    WorkerJSContextStats mCxStats;

    explicit FinishCollectRunnable(nsIHandleReportCallback* aHandleReport,
                                   nsISupports* aHandlerData, bool aAnonymize,
                                   const nsACString& aPath);

    NS_IMETHOD Run() override;

    void SetPerformanceSizes(size_t userEntries, size_t resourceEntries) {
      mPerformanceUserEntries = userEntries;
      mPerformanceResourceEntries = resourceEntries;
    }

    void SetSuccess(bool success) { mSuccess = success; }

    FinishCollectRunnable(const FinishCollectRunnable&) = delete;
    FinishCollectRunnable& operator=(const FinishCollectRunnable&) = delete;
    FinishCollectRunnable& operator=(const FinishCollectRunnable&&) = delete;

   private:
    ~FinishCollectRunnable() {
      // mHandleReport and mHandlerData are released on the main thread.
      AssertIsOnMainThread();
    }
  };

  ~MemoryReporter() = default;

  void Disable() {
    // Called from WorkerPrivate::DisableMemoryReporter.
    mMutex.AssertCurrentThreadOwns();

    NS_ASSERTION(mWorkerPrivate, "Disabled more than once!");
    mWorkerPrivate = nullptr;
  }
};

NS_IMPL_ISUPPORTS(WorkerPrivate::MemoryReporter, nsIMemoryReporter)

NS_IMETHODIMP
WorkerPrivate::MemoryReporter::CollectReports(
    nsIHandleReportCallback* aHandleReport, nsISupports* aData,
    bool aAnonymize) {
  AssertIsOnMainThread();

  RefPtr<CollectReportsRunnable> runnable;

  {
    MutexAutoLock lock(mMutex);

    if (!mWorkerPrivate) {
      // This will effectively report 0 memory.
      nsCOMPtr<nsIMemoryReporterManager> manager =
          do_GetService("@mozilla.org/memory-reporter-manager;1");
      if (manager) {
        manager->EndReport();
      }
      return NS_OK;
    }

    nsAutoCString path;
    path.AppendLiteral("explicit/workers/workers(");
    if (aAnonymize && !mWorkerPrivate->Domain().IsEmpty()) {
      path.AppendLiteral("<anonymized-domain>)/worker(<anonymized-url>");
    } else {
      nsAutoCString escapedDomain(mWorkerPrivate->Domain());
      if (escapedDomain.IsEmpty()) {
        escapedDomain += "chrome";
      } else {
        escapedDomain.ReplaceChar('/', '\\');
      }
      path.Append(escapedDomain);
      path.AppendLiteral(")/worker(");
      NS_ConvertUTF16toUTF8 escapedURL(mWorkerPrivate->ScriptURL());
      escapedURL.ReplaceChar('/', '\\');
      path.Append(escapedURL);
    }
    path.AppendPrintf(", 0x%p)/", static_cast<void*>(mWorkerPrivate));

    runnable = new CollectReportsRunnable(mWorkerPrivate, aHandleReport, aData,
                                          aAnonymize, path);
  }

  if (!runnable->Dispatch(mWorkerPrivate)) {
    return NS_ERROR_UNEXPECTED;
  }

  return NS_OK;
}

WorkerPrivate::MemoryReporter::CollectReportsRunnable::CollectReportsRunnable(
    WorkerPrivate* aWorkerPrivate, nsIHandleReportCallback* aHandleReport,
    nsISupports* aHandlerData, bool aAnonymize, const nsACString& aPath)
    : MainThreadWorkerControlRunnable("CollectReportsRunnable"),
      mFinishCollectRunnable(new FinishCollectRunnable(
          aHandleReport, aHandlerData, aAnonymize, aPath)),
      mAnonymize(aAnonymize) {}

bool WorkerPrivate::MemoryReporter::CollectReportsRunnable::WorkerRun(
    JSContext* aCx, WorkerPrivate* aWorkerPrivate) {
  aWorkerPrivate->AssertIsOnWorkerThread();

  RefPtr<WorkerGlobalScope> scope = aWorkerPrivate->GlobalScope();
  RefPtr<Performance> performance =
      scope ? scope->GetPerformanceIfExists() : nullptr;
  if (performance) {
    size_t userEntries = performance->SizeOfUserEntries(JsWorkerMallocSizeOf);
    size_t resourceEntries =
        performance->SizeOfResourceEntries(JsWorkerMallocSizeOf);
    mFinishCollectRunnable->SetPerformanceSizes(userEntries, resourceEntries);
  }

  mFinishCollectRunnable->SetSuccess(aWorkerPrivate->CollectRuntimeStats(
      &mFinishCollectRunnable->mCxStats, mAnonymize));

  return true;
}

WorkerPrivate::MemoryReporter::FinishCollectRunnable::FinishCollectRunnable(
    nsIHandleReportCallback* aHandleReport, nsISupports* aHandlerData,
    bool aAnonymize, const nsACString& aPath)
    : mozilla::Runnable(
          "dom::WorkerPrivate::MemoryReporter::FinishCollectRunnable"),
      mHandleReport(aHandleReport),
      mHandlerData(aHandlerData),
      mPerformanceUserEntries(0),
      mPerformanceResourceEntries(0),
      mAnonymize(aAnonymize),
      mSuccess(false),
      mCxStats(aPath) {}

NS_IMETHODIMP
WorkerPrivate::MemoryReporter::FinishCollectRunnable::Run() {
  AssertIsOnMainThread();

  nsCOMPtr<nsIMemoryReporterManager> manager =
      do_GetService("@mozilla.org/memory-reporter-manager;1");

  if (!manager) return NS_OK;

  if (mSuccess) {
    xpc::ReportJSRuntimeExplicitTreeStats(
        mCxStats, mCxStats.Path(), mHandleReport, mHandlerData, mAnonymize);

    if (mPerformanceUserEntries) {
      nsCString path = mCxStats.Path();
      path.AppendLiteral("dom/performance/user-entries");
      mHandleReport->Callback(""_ns, path, nsIMemoryReporter::KIND_HEAP,
                              nsIMemoryReporter::UNITS_BYTES,
                              static_cast<int64_t>(mPerformanceUserEntries),
                              "Memory used for performance user entries."_ns,
                              mHandlerData);
    }

    if (mPerformanceResourceEntries) {
      nsCString path = mCxStats.Path();
      path.AppendLiteral("dom/performance/resource-entries");
      mHandleReport->Callback(
          ""_ns, path, nsIMemoryReporter::KIND_HEAP,
          nsIMemoryReporter::UNITS_BYTES,
          static_cast<int64_t>(mPerformanceResourceEntries),
          "Memory used for performance resource entries."_ns, mHandlerData);
    }
  }

  manager->EndReport();

  return NS_OK;
}

WorkerPrivate::SyncLoopInfo::SyncLoopInfo(EventTarget* aEventTarget)
    : mEventTarget(aEventTarget),
      mResult(NS_ERROR_FAILURE),
      mCompleted(false)
#ifdef DEBUG
      ,
      mHasRun(false)
#endif
{
}

Document* WorkerPrivate::GetDocument() const {
  AssertIsOnMainThread();
  if (nsPIDOMWindowInner* window = GetAncestorWindow()) {
    return window->GetExtantDoc();
  }
  // couldn't query a document, give up and return nullptr
  return nullptr;
}

nsPIDOMWindowInner* WorkerPrivate::GetAncestorWindow() const {
  AssertIsOnMainThread();

  // We should query the window from the top level worker in case of a nested
  // worker, as only the top level one can have a window.
  WorkerPrivate* top = GetTopLevelWorker();
  return top->GetWindow();
}

class EvictFromBFCacheRunnable final : public WorkerProxyToMainThreadRunnable {
 public:
  void RunOnMainThread(WorkerPrivate* aWorkerPrivate) override {
    MOZ_ASSERT(aWorkerPrivate);
    AssertIsOnMainThread();
    if (nsCOMPtr<nsPIDOMWindowInner> win =
            aWorkerPrivate->GetAncestorWindow()) {
      win->RemoveFromBFCacheSync();
    }
  }

  void RunBackOnWorkerThreadForCleanup(WorkerPrivate* aWorkerPrivate) override {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();
  }
};

void WorkerPrivate::EvictFromBFCache() {
  AssertIsOnWorkerThread();
  RefPtr<EvictFromBFCacheRunnable> runnable = new EvictFromBFCacheRunnable();
  runnable->Dispatch(this);
}

nsresult WorkerPrivate::SetCsp(nsIContentSecurityPolicy* aCSP) {
  AssertIsOnMainThread();
  if (!aCSP) {
    return NS_OK;
  }
  aCSP->EnsureEventTarget(mMainThreadEventTarget);

  mLoadInfo.mCSP = aCSP;
  auto ctx = WorkerCSPContext::CreateFromCSP(aCSP);
  if (NS_WARN_IF(ctx.isErr())) {
    return ctx.unwrapErr();
  }
  mLoadInfo.mCSPContext = ctx.unwrap();
  return NS_OK;
}

nsresult WorkerPrivate::SetCSPFromHeaderValues(
    const nsACString& aCSPHeaderValue,
    const nsACString& aCSPReportOnlyHeaderValue) {
  AssertIsOnMainThread();
  MOZ_DIAGNOSTIC_ASSERT(!mLoadInfo.mCSP);

  NS_ConvertASCIItoUTF16 cspHeaderValue(aCSPHeaderValue);
  NS_ConvertASCIItoUTF16 cspROHeaderValue(aCSPReportOnlyHeaderValue);

  nsresult rv;
  nsCOMPtr<nsIContentSecurityPolicy> csp = new nsCSPContext();

  // First, we try to query the URI from the Principal, but
  // in case selfURI remains empty (e.g in case the Principal
  // is a SystemPrincipal) then we fall back and use the
  // base URI as selfURI for CSP.
  nsCOMPtr<nsIURI> selfURI;
  // Its not recommended to use the BasePrincipal to get the URI
  // but in this case we need to make an exception
  auto* basePrin = BasePrincipal::Cast(mLoadInfo.mPrincipal);
  if (basePrin) {
    basePrin->GetURI(getter_AddRefs(selfURI));
  }
  if (!selfURI) {
    selfURI = mLoadInfo.mBaseURI;
  }
  MOZ_ASSERT(selfURI, "need a self URI for CSP");

  rv = csp->SetRequestContextWithPrincipal(mLoadInfo.mPrincipal, selfURI, ""_ns,
                                           0);
  NS_ENSURE_SUCCESS(rv, rv);

  csp->EnsureEventTarget(mMainThreadEventTarget);

  // If there's a CSP header, apply it.
  if (!cspHeaderValue.IsEmpty()) {
    rv = CSP_AppendCSPFromHeader(csp, cspHeaderValue, false);
    NS_ENSURE_SUCCESS(rv, rv);
  }
  // If there's a report-only CSP header, apply it.
  if (!cspROHeaderValue.IsEmpty()) {
    rv = CSP_AppendCSPFromHeader(csp, cspROHeaderValue, true);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  RefPtr<extensions::WebExtensionPolicy> addonPolicy;

  if (basePrin) {
    addonPolicy = basePrin->AddonPolicy();
  }

  // For extension workers there aren't any csp header values,
  // instead it will inherit the Extension CSP.
  if (addonPolicy) {
    csp->AppendPolicy(addonPolicy->BaseCSP(), false, false);
    csp->AppendPolicy(addonPolicy->ExtensionPageCSP(), false, false);
  }

  mLoadInfo.mCSP = csp;

  auto ctx = WorkerCSPContext::CreateFromCSP(csp);
  if (NS_WARN_IF(ctx.isErr())) {
    return ctx.unwrapErr();
  }
  mLoadInfo.mCSPContext = ctx.unwrap();
  return NS_OK;
}

bool WorkerPrivate::IsFrozenForWorkerThread() const {
  auto data = mWorkerThreadAccessible.Access();
  return data->mFrozen;
}

bool WorkerPrivate::IsFrozen() const {
  AssertIsOnParentThread();
  return mParentFrozen;
}

void WorkerPrivate::StoreCSPOnClient() {
  auto data = mWorkerThreadAccessible.Access();
  MOZ_ASSERT(data->mScope);
  if (mLoadInfo.mCSPContext) {
    data->mScope->MutableClientSourceRef().SetCspInfo(
        mLoadInfo.mCSPContext->CSPInfo());
  }
}

void WorkerPrivate::UpdateReferrerInfoFromHeader(
    const nsACString& aReferrerPolicyHeaderValue) {
  NS_ConvertUTF8toUTF16 headerValue(aReferrerPolicyHeaderValue);

  if (headerValue.IsEmpty()) {
    return;
  }

  ReferrerPolicy policy =
      ReferrerInfo::ReferrerPolicyFromHeaderString(headerValue);
  if (policy == ReferrerPolicy::_empty) {
    return;
  }

  nsCOMPtr<nsIReferrerInfo> referrerInfo =
      static_cast<ReferrerInfo*>(GetReferrerInfo())->CloneWithNewPolicy(policy);
  SetReferrerInfo(referrerInfo);
}

void WorkerPrivate::Traverse(nsCycleCollectionTraversalCallback& aCb) {
  AssertIsOnParentThread();

  // The WorkerPrivate::mParentEventTargetRef has a reference to the exposed
  // Worker object, which is really held by the worker thread.  We traverse this
  // reference if and only if all main thread event queues are empty, no
  // shutdown tasks, no StrongWorkerRefs, no child workers, no timeouts, no
  // blocking background actors, and we have not released the main thread
  // reference.  We do not unlink it. This allows the CC to break cycles
  // involving the Worker and begin shutting it down (which does happen in
  // unlink) but ensures that the WorkerPrivate won't be deleted before we're
  // done shutting down the thread.
  if (IsEligibleForCC() && !mMainThreadObjectsForgotten) {
    nsCycleCollectionTraversalCallback& cb = aCb;
    WorkerPrivate* tmp = this;
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mParentEventTargetRef);
  }
}

nsresult WorkerPrivate::Dispatch(already_AddRefed<WorkerRunnable> aRunnable,
                                 nsIEventTarget* aSyncLoopTarget) {
  // May be called on any thread!
  RefPtr<WorkerRunnable> runnable(aRunnable);

  LOGV(("WorkerPrivate::Dispatch [%p] runnable %p", this, runnable.get()));
  if (!aSyncLoopTarget) {
    // Dispatch control runnable
    if (runnable->IsControlRunnable()) {
      return DispatchControlRunnable(runnable.forget());
    }

    // Dispatch debugger runnable
    if (runnable->IsDebuggerRunnable()) {
      return DispatchDebuggerRunnable(runnable.forget());
    }
  }
  MutexAutoLock lock(mMutex);
  return DispatchLockHeld(runnable.forget(), aSyncLoopTarget, lock);
}

nsresult WorkerPrivate::DispatchToParent(
    already_AddRefed<WorkerRunnable> aRunnable) {
  RefPtr<WorkerRunnable> runnable(aRunnable);

  LOGV(("WorkerPrivate::DispatchToParent [%p] runnable %p", this,
        runnable.get()));

  WorkerPrivate* parent = GetParent();
  // Dispatch to parent worker
  if (parent) {
    if (runnable->IsControlRunnable()) {
      return parent->DispatchControlRunnable(runnable.forget());
    }
    return parent->Dispatch(runnable.forget());
  }

  // Dispatch to main thread
  if (runnable->IsDebuggeeRunnable()) {
    RefPtr<WorkerParentDebuggeeRunnable> debuggeeRunnable =
        runnable.forget().downcast<WorkerParentDebuggeeRunnable>();
    return DispatchDebuggeeToMainThread(debuggeeRunnable.forget(),
                                        NS_DISPATCH_NORMAL);
  }
  return DispatchToMainThread(runnable.forget());
}

nsresult WorkerPrivate::DispatchLockHeld(
    already_AddRefed<WorkerRunnable> aRunnable, nsIEventTarget* aSyncLoopTarget,
    const MutexAutoLock& aProofOfLock) {
  // May be called on any thread!
  RefPtr<WorkerRunnable> runnable(aRunnable);
  LOGV(("WorkerPrivate::DispatchLockHeld [%p] runnable: %p", this,
        runnable.get()));

  MOZ_ASSERT_IF(aSyncLoopTarget, mThread);

  // Dispatch normal worker runnable
  if (mStatus == Dead || (!aSyncLoopTarget && ParentStatus() > Canceling)) {
    NS_WARNING(
        "A runnable was posted to a worker that is already shutting "
        "down!");
    return NS_ERROR_UNEXPECTED;
  }

  // Postpone the debuggee runnable dispatching while remote debugger
  // registration
  if (runnable->IsDebuggeeRunnable() && !mDebuggerReady &&
      !mRemoteDebuggerReady &&
      (!mRemoteDebuggerRegistered && XRE_IsParentProcess())) {
    MOZ_RELEASE_ASSERT(!aSyncLoopTarget);
    mDelayedDebuggeeRunnables.AppendElement(runnable);
    return NS_OK;
  }

  if (!mThread) {
    if (ParentStatus() == Pending || mStatus == Pending) {
      LOGV(
          ("WorkerPrivate::DispatchLockHeld [%p] runnable %p is queued in "
           "mPreStartRunnables",
           this, runnable.get()));
      RefPtr<WorkerThreadRunnable> workerThreadRunnable =
          static_cast<WorkerThreadRunnable*>(runnable.get());
      mPreStartRunnables.AppendElement(workerThreadRunnable);
      return NS_OK;
    }

    NS_WARNING(
        "Using a worker event target after the thread has already"
        "been released!");
    return NS_ERROR_UNEXPECTED;
  }

  nsresult rv;
  if (aSyncLoopTarget) {
    LOGV(
        ("WorkerPrivate::DispatchLockHeld [%p] runnable %p dispatch to a "
         "SyncLoop(%p)",
         this, runnable.get(), aSyncLoopTarget));
    rv = aSyncLoopTarget->Dispatch(runnable.forget(), NS_DISPATCH_NORMAL);
  } else {
    // If mStatus is Pending, the WorkerPrivate initialization still can fail.
    // Append this WorkerThreadRunnable to WorkerPrivate::mPreStartRunnables,
    // such that this WorkerThreadRunnable can get the correct value of
    // mCleanPreStartDispatching in WorkerPrivate::RunLoopNeverRan().
    if (mStatus == Pending) {
      LOGV(
          ("WorkerPrivate::DispatchLockHeld [%p] runnable %p is append in "
           "mPreStartRunnables",
           this, runnable.get()));
      RefPtr<WorkerThreadRunnable> workerThreadRunnable =
          static_cast<WorkerThreadRunnable*>(runnable.get());
      mPreStartRunnables.AppendElement(workerThreadRunnable);
    }

    // WorkerDebuggeeRunnables don't need any special treatment here. True,
    // they should not be delivered to a frozen worker. But frozen workers
    // aren't drawing from the thread's main event queue anyway, only from
    // mControlQueue.
    LOGV(
        ("WorkerPrivate::DispatchLockHeld [%p] runnable %p dispatch to the "
         "main event queue",
         this, runnable.get()));
    rv = mThread->DispatchAnyThread(WorkerThreadFriendKey(), runnable.forget());
  }

  if (NS_WARN_IF(NS_FAILED(rv))) {
    LOGV(("WorkerPrivate::Dispatch Failed [%p]", this));
    return rv;
  }

  mCondVar.Notify();
  return NS_OK;
}

void WorkerPrivate::EnableDebugger() {
  AssertIsOnParentThread();

  if (NS_FAILED(RegisterWorkerDebugger(this))) {
    NS_WARNING("Failed to register worker debugger!");
    return;
  }
}

void WorkerPrivate::DisableDebugger() {
  AssertIsOnParentThread();

  // RegisterDebuggerMainThreadRunnable might be dispatched but not executed.
  // Wait for its execution before unregistraion.
  if (!NS_IsMainThread()) {
    WaitForIsDebuggerRegistered(true);
  }

  if (NS_FAILED(UnregisterWorkerDebugger(this))) {
    NS_WARNING("Failed to unregister worker debugger!");
  }
}

void WorkerPrivate::BindRemoteWorkerDebuggerChild() {
  AssertIsOnWorkerThread();
  MOZ_ASSERT_DEBUG_OR_FUZZING(!mRemoteDebugger);

  if (XRE_IsParentProcess()) {
    return;
  }

  RefPtr<RemoteWorkerDebuggerChild> debugger =
      MakeRefPtr<RemoteWorkerDebuggerChild>(this);
  mDebuggerChildEp.Bind(debugger);
  {
    MutexAutoLock lock(mMutex);
    MOZ_ASSERT_DEBUG_OR_FUZZING(!mRemoteDebugger);
    mRemoteDebugger = std::move(debugger);
    mDebuggerBindingCondVar.Notify();
  }
}

void WorkerPrivate::CreateRemoteDebuggerEndpoints() {
  AssertIsOnParentThread();

  if (XRE_IsParentProcess()) {
    return;
  }

  MutexAutoLock lock(mMutex);
  MOZ_ASSERT_DEBUG_OR_FUZZING(!mRemoteDebugger &&
                              !mDebuggerParentEp.IsValid() &&
                              !mDebuggerChildEp.IsValid());

  Unused << NS_WARN_IF(NS_FAILED(PRemoteWorkerDebugger::CreateEndpoints(
      &mDebuggerParentEp, &mDebuggerChildEp)));
}

void WorkerPrivate::SetIsRemoteDebuggerRegistered(const bool& aRegistered) {
  AssertIsOnWorkerThread();

  if (XRE_IsParentProcess()) {
    return;
  }

  if (aRegistered) {
    MutexAutoLock lock(mMutex);
    MOZ_ASSERT(mRemoteDebuggerRegistered != aRegistered);

    mRemoteDebuggerRegistered = aRegistered;
    bool debuggerRegistered = mDebuggerRegistered && mRemoteDebuggerRegistered;
    if (mRemoteDebuggerReady && mDebuggerReady && debuggerRegistered) {
      LOGV(
          ("WorkerPrivate::SetIsRemoteDebuggerRegistered [%p] dispatching "
           "the delayed debuggee runnables",
           this));
      // Dispatch all the delayed runnables without releasing the lock, to
      // ensure that the order in which debuggee runnables execute is the same
      // as the order in which they were originally dispatched.
      auto pending = std::move(mDelayedDebuggeeRunnables);
      for (uint32_t i = 0; i < pending.Length(); i++) {
        RefPtr<WorkerRunnable> runnable = std::move(pending[i]);
        Unused << NS_WARN_IF(
            NS_FAILED(DispatchLockHeld(runnable.forget(), nullptr, lock)));
      }
      MOZ_RELEASE_ASSERT(mDelayedDebuggeeRunnables.IsEmpty());
    }
    mDebuggerBindingCondVar.Notify();
    return;
  }

  RefPtr<RemoteWorkerDebuggerChild> unregisteredDebugger;
  {
    MutexAutoLock lock(mMutex);
    // Can not call RemoteWorkerDebuggerChild::Close() with lock. It causes
    // deadlock between mMutex and MessageChannel::mMonitor.
    unregisteredDebugger = std::move(mRemoteDebugger);
    // Force to set as unregistered, mRemoteDebuggerRegistered could be false
    // here since Worker quickly shutdown or initialization fails in
    // WorkerThreadPrimaryRunnable::Run().
    mRemoteDebuggerRegistered = aRegistered;
  }
  if (unregisteredDebugger) {
    unregisteredDebugger->Close();
    unregisteredDebugger = nullptr;
  }
  {
    MutexAutoLock lock(mMutex);
    mDebuggerBindingCondVar.Notify();
  }
}

void WorkerPrivate::SetIsRemoteDebuggerReady(const bool& aReady) {
  AssertIsOnWorkerThread();
  MutexAutoLock lock(mMutex);

  if (XRE_IsParentProcess()) {
    return;
  }

  if (mRemoteDebuggerReady == aReady) {
    return;
  }

  bool debuggerRegistered = mDebuggerRegistered && mRemoteDebuggerRegistered;

  if (!aReady && debuggerRegistered) {
    // The debugger can only be marked as not ready during registration.
    return;
  }

  mRemoteDebuggerReady = aReady;

  if (mRemoteDebuggerReady && mDebuggerReady && debuggerRegistered) {
    LOGV(
        ("WorkerPrivate::SetIsRemoteDebuggerReady [%p] dispatching "
         "the delayed debuggee runnables",
         this));
    // Dispatch all the delayed runnables without releasing the lock, to ensure
    // that the order in which debuggee runnables execute is the same as the
    // order in which they were originally dispatched.
    auto pending = std::move(mDelayedDebuggeeRunnables);
    for (uint32_t i = 0; i < pending.Length(); i++) {
      RefPtr<WorkerRunnable> runnable = std::move(pending[i]);
      Unused << NS_WARN_IF(
          NS_FAILED(DispatchLockHeld(runnable.forget(), nullptr, lock)));
    }
    MOZ_RELEASE_ASSERT(mDelayedDebuggeeRunnables.IsEmpty());
  }
}

void WorkerPrivate::SetIsQueued(const bool& aQueued) {
  AssertIsOnParentThread();
  mIsQueued = aQueued;
}

bool WorkerPrivate::IsQueued() const {
  AssertIsOnParentThread();
  return mIsQueued;
}

void WorkerPrivate::EnableRemoteDebugger() {
  AssertIsOnParentThread();

  // XXX Skip for ChromeWorker now, this should be removed after Devtool codes
  // adapt to RemoteWorkerDebugger mechanism.
  if (XRE_IsParentProcess()) {
    return;
  }

  // Wait for RemoteWorkerDebuggerChild binding done in the worker thread.
  mozilla::ipc::Endpoint<PRemoteWorkerDebuggerParent> parentEp;
  {
    MutexAutoLock lock(mMutex);
    if (!mRemoteDebugger) {
      mDebuggerBindingCondVar.Wait();
    }
    // If Worker Thread never run the event loop, i.e. JSContext initilaization
    // fails, directly return for the cases. Because mRemoteDebugger is only
    // created after initialization successfully, but mDebuggerBindingCondVar
    // can get notified if the initialization fails.
    if (!mRemoteDebugger) {
      return;
    }
    parentEp = std::move(mDebuggerParentEp);
  }

  // Call IPC for RemoteWorkerDebuggerParent binding and registration.
  RemoteWorkerDebuggerInfo info(
      mIsChromeWorker, mWorkerKind, mScriptURL, WindowID(),
      WrapNotNull(GetPrincipal()), IsServiceWorker() ? ServiceWorkerID() : 0,
      Id(), mWorkerName,
      GetParent() ? nsAutoString(GetParent()->Id()) : EmptyString());

  MOZ_ASSERT_DEBUG_OR_FUZZING(parentEp.IsValid());
  RemoteWorkerService::RegisterRemoteDebugger(std::move(info),
                                              std::move(parentEp));
  // Wait for register done
  {
    MutexAutoLock lock(mMutex);
    if (!mRemoteDebuggerRegistered) {
      mDebuggerBindingCondVar.Wait();
    }
    // Warning the case if the Worker shutdown before remote debugger
    // registration down.
    Unused << NS_WARN_IF(!mRemoteDebuggerRegistered);
  }
}

void WorkerPrivate::DisableRemoteDebugger() {
  AssertIsOnParentThread();

  if (XRE_IsParentProcess()) {
    return;
  }

  RefPtr<DisableRemoteDebuggerRunnable> r =
      new DisableRemoteDebuggerRunnable(this);

  if (r->Dispatch(this)) {
    MutexAutoLock lock(mMutex);
    if (mRemoteDebuggerRegistered) {
      mDebuggerBindingCondVar.Wait();
    }
  }
}

void WorkerPrivate::DisableRemoteDebuggerOnWorkerThread(
    const bool& aForShutdown) {
  AssertIsOnWorkerThread();

  if (XRE_IsParentProcess()) {
    return;
  }
  RefPtr<RemoteWorkerDebuggerChild> remoteDebugger;
  {
    MutexAutoLock lock(mMutex);
    remoteDebugger = mRemoteDebugger;
  }
  if (remoteDebugger) {
    remoteDebugger->SendUnregister();
  }

  // Now notify the parent thread if it is blocked by waiting
  // RemoteWorkerDebugger registration or unregsiteration.
  if (aForShutdown) {
    SetIsRemoteDebuggerRegistered(false);
  }
}

nsresult WorkerPrivate::DispatchControlRunnable(
    already_AddRefed<WorkerRunnable> aWorkerRunnable) {
  // May be called on any thread!
  RefPtr<WorkerRunnable> runnable(aWorkerRunnable);
  MOZ_ASSERT_DEBUG_OR_FUZZING(runnable && runnable->IsControlRunnable());

  LOG(WorkerLog(), ("WorkerPrivate::DispatchControlRunnable [%p] runnable %p",
                    this, runnable.get()));

  {
    MutexAutoLock lock(mMutex);

    if (mStatus == Dead) {
      return NS_ERROR_UNEXPECTED;
    }

    // Transfer ownership to the control queue.
    mControlQueue.Push(runnable.forget().take());

    if (JSContext* cx = mJSContext) {
      MOZ_ASSERT(mThread);
      JS_RequestInterruptCallback(cx);
    }

    mCondVar.Notify();
  }

  return NS_OK;
}

void DebuggerInterruptTimerCallback(nsITimer* aTimer, void* aClosure)
    MOZ_NO_THREAD_SAFETY_ANALYSIS {
  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
  MOZ_DIAGNOSTIC_ASSERT(workerPrivate);
  workerPrivate->DebuggerInterruptRequest();
}

nsresult WorkerPrivate::DispatchDebuggerRunnable(
    already_AddRefed<WorkerRunnable> aDebuggerRunnable) {
  // May be called on any thread!

  RefPtr<WorkerRunnable> runnable(aDebuggerRunnable);

  MOZ_ASSERT(runnable);

  MutexAutoLock lock(mMutex);
  if (!mDebuggerInterruptTimer) {
    // There is no timer, so we need to create one.  For locking discipline
    // purposes we can't manipulate the timer while our mutex is held so
    // drop the mutex while we build and configure the timer.  Only this
    // function here on the main thread will create a timer, so we're not
    // racing anyone to create or assign the timer.
    nsCOMPtr<nsITimer> timer;
    {
      MutexAutoUnlock unlock(mMutex);
      timer = NS_NewTimer();
      MOZ_ALWAYS_SUCCEEDS(timer->SetTarget(mWorkerControlEventTarget));

      // Whenever an event is scheduled on the WorkerControlEventTarget an
      // interrupt is automatically requested which causes us to yield JS
      // execution and the next JS execution in the queue to execute. This
      // allows for simple code reuse of the existing interrupt callback code
      // used for control events.
      MOZ_ALWAYS_SUCCEEDS(timer->InitWithNamedFuncCallback(
          DebuggerInterruptTimerCallback, nullptr,
          DEBUGGER_RUNNABLE_INTERRUPT_AFTER_MS, nsITimer::TYPE_ONE_SHOT,
          "dom:DebuggerInterruptTimer"));
    }

    // okay, we have our mutex back now, put the timer in place.
    mDebuggerInterruptTimer.swap(timer);
  }

  if (mStatus == Dead) {
    NS_WARNING(
        "A debugger runnable was posted to a worker that is already "
        "shutting down!");
    return NS_ERROR_UNEXPECTED;
  }

  // Transfer ownership to the debugger queue.
  mDebuggerQueue.Push(runnable.forget().take());

  mCondVar.Notify();

  return NS_OK;
}

void WorkerPrivate::DebuggerInterruptRequest() {
  AssertIsOnWorkerThread();

  auto data = mWorkerThreadAccessible.Access();
  data->mDebuggerInterruptRequested = true;
}

already_AddRefed<WorkerRunnable> WorkerPrivate::MaybeWrapAsWorkerRunnable(
    already_AddRefed<nsIRunnable> aRunnable) {
  // May be called on any thread!

  nsCOMPtr<nsIRunnable> runnable(aRunnable);
  MOZ_ASSERT(runnable);

  LOGV(("WorkerPrivate::MaybeWrapAsWorkerRunnable [%p] runnable: %p", this,
        runnable.get()));

  RefPtr<WorkerRunnable> workerRunnable =
      WorkerRunnable::FromRunnable(runnable);
  if (workerRunnable) {
    return workerRunnable.forget();
  }

  workerRunnable = new ExternalRunnableWrapper(this, runnable);
  return workerRunnable.forget();
}

bool WorkerPrivate::Start() {
  // May be called on any thread!
  LOG(WorkerLog(), ("WorkerPrivate::Start [%p]", this));
  {
    MutexAutoLock lock(mMutex);
    NS_ASSERTION(mParentStatus != Running, "How can this be?!");

    if (mParentStatus == Pending) {
      mParentStatus = Running;
      return true;
    }
  }

  return false;
}

// aCx is null when called from the finalizer
bool WorkerPrivate::Notify(WorkerStatus aStatus) {
  AssertIsOnParentThread();
  // This method is only called for Canceling or later.
  MOZ_DIAGNOSTIC_ASSERT(aStatus >= Canceling);

  bool pending;
  {
    MutexAutoLock lock(mMutex);

    if (mParentStatus >= aStatus) {
      return true;
    }

    pending = mParentStatus == Pending;
    mParentStatus = aStatus;
  }

  if (mCancellationCallback) {
    mCancellationCallback(!pending);
    mCancellationCallback = nullptr;
  }

  mParentRef->DropWorkerPrivate();

  if (pending) {
#ifdef DEBUG
    {
      // Fake a thread here just so that our assertions don't go off for no
      // reason.
      nsIThread* currentThread = NS_GetCurrentThread();
      MOZ_ASSERT(currentThread);

      MOZ_ASSERT(!mPRThread);
      mPRThread = PRThreadFromThread(currentThread);
      MOZ_ASSERT(mPRThread);
    }
#endif

    // Worker never got a chance to run, go ahead and delete it.
    ScheduleDeletion(WorkerPrivate::WorkerNeverRan);
    return true;
  }

  // No Canceling timeout is needed.
  if (mCancelingTimer) {
    mCancelingTimer->Cancel();
    mCancelingTimer = nullptr;
  }

  // The NotifyRunnable kicks off a series of events that need the
  // CancelingOnParentRunnable to be executed always.
  // Note that we already advanced mParentStatus above and we check that
  // status in all other (asynchronous) call sites of SetIsPaused.
  if (!mParent) {
    MOZ_ALWAYS_SUCCEEDS(mMainThreadDebuggeeEventTarget->SetIsPaused(false));
  }

  RefPtr<NotifyRunnable> runnable = new NotifyRunnable(this, aStatus);
  return runnable->Dispatch(this);
}

bool WorkerPrivate::Freeze(const nsPIDOMWindowInner* aWindow) {
  AssertIsOnParentThread();

  mParentFrozen = true;

  bool isCanceling = false;
  {
    MutexAutoLock lock(mMutex);

    isCanceling = mParentStatus >= Canceling;
  }

  // WorkerDebuggeeRunnables sent from a worker to content must not be
  // delivered while the worker is frozen.
  //
  // Since a top-level worker and all its children share the same
  // mMainThreadDebuggeeEventTarget, it's sufficient to do this only in the
  // top-level worker.
  if (aWindow) {
    // This is called from WorkerPrivate construction, and We may not have
    // allocated mMainThreadDebuggeeEventTarget yet.
    if (mMainThreadDebuggeeEventTarget) {
      // Pausing a ThrottledEventQueue is infallible.
      MOZ_ALWAYS_SUCCEEDS(
          mMainThreadDebuggeeEventTarget->SetIsPaused(!isCanceling));
    }
  }

  if (isCanceling) {
    return true;
  }

  // DisableRemoteDebugger();

  DisableDebugger();

  RefPtr<FreezeRunnable> runnable = new FreezeRunnable(this);
  return runnable->Dispatch(this);
}

bool WorkerPrivate::Thaw(const nsPIDOMWindowInner* aWindow) {
  AssertIsOnParentThread();
  MOZ_ASSERT(mParentFrozen);

  mParentFrozen = false;

  {
    bool isCanceling = false;

    {
      MutexAutoLock lock(mMutex);

      isCanceling = mParentStatus >= Canceling;
    }

    // Delivery of WorkerDebuggeeRunnables to the window may resume.
    //
    // Since a top-level worker and all its children share the same
    // mMainThreadDebuggeeEventTarget, it's sufficient to do this only in the
    // top-level worker.
    if (aWindow) {
      // Since the worker is no longer frozen, only a paused parent window
      // should require the queue to remain paused.
      //
      // This can only fail if the ThrottledEventQueue cannot dispatch its
      // executor to the main thread, in which case the main thread was never
      // going to draw runnables from it anyway, so the failure doesn't matter.
      Unused << mMainThreadDebuggeeEventTarget->SetIsPaused(
          IsParentWindowPaused() && !isCanceling);
    }

    if (isCanceling) {
      return true;
    }
  }

  // Create remote debugger endpoints here for child binding in ThawRunnable;
  // CreateRemoteDebuggerEndpoints();
  RefPtr<ThawRunnable> runnable = new ThawRunnable(this);
  bool rv = runnable->Dispatch(this);
  // EnableRemoteDebugger();

  EnableDebugger();

  return rv;
}

void WorkerPrivate::ParentWindowPaused() {
  AssertIsOnMainThread();
  MOZ_ASSERT(!mParentWindowPaused);
  mParentWindowPaused = true;

  // This is called from WorkerPrivate construction, and we may not have
  // allocated mMainThreadDebuggeeEventTarget yet.
  if (mMainThreadDebuggeeEventTarget) {
    bool isCanceling = false;

    {
      MutexAutoLock lock(mMutex);

      isCanceling = mParentStatus >= Canceling;
    }

    // If we are already canceling we might wait for CancelingOnParentRunnable
    // to be executed, so do not pause.
    MOZ_ALWAYS_SUCCEEDS(
        mMainThreadDebuggeeEventTarget->SetIsPaused(!isCanceling));
  }
}

void WorkerPrivate::ParentWindowResumed() {
  AssertIsOnMainThread();

  MOZ_ASSERT(mParentWindowPaused);
  mParentWindowPaused = false;

  bool isCanceling = false;
  {
    MutexAutoLock lock(mMutex);

    isCanceling = mParentStatus >= Canceling;
  }

  // Since the window is no longer paused, the queue should only remain paused
  // if the worker is frozen.
  //
  // This can only fail if the ThrottledEventQueue cannot dispatch its executor
  // to the main thread, in which case the main thread was never going to draw
  // runnables from it anyway, so the failure doesn't matter.
  Unused << mMainThreadDebuggeeEventTarget->SetIsPaused(IsFrozen() &&
                                                        !isCanceling);
}

void WorkerPrivate::PropagateStorageAccessPermissionGranted() {
  AssertIsOnParentThread();

  {
    MutexAutoLock lock(mMutex);

    if (mParentStatus >= Canceling) {
      return;
    }
  }

  RefPtr<PropagateStorageAccessPermissionGrantedRunnable> runnable =
      new PropagateStorageAccessPermissionGrantedRunnable(this);
  Unused << NS_WARN_IF(!runnable->Dispatch(this));
}

void WorkerPrivate::NotifyStorageKeyUsed() {
  AssertIsOnWorkerThread();

  // Only notify once per global.
  if (hasNotifiedStorageKeyUsed) {
    return;
  }
  hasNotifiedStorageKeyUsed = true;

  // Notify about storage access on the main thread.
  RefPtr<StrongWorkerRef> strongRef =
      StrongWorkerRef::Create(this, "WorkerPrivate::NotifyStorageKeyUsed");
  if (!strongRef) {
    return;
  }
  RefPtr<ThreadSafeWorkerRef> ref = new ThreadSafeWorkerRef(strongRef);
  DispatchToMainThread(NS_NewRunnableFunction(
      "WorkerPrivate::NotifyStorageKeyUsed", [ref = std::move(ref)] {
        nsGlobalWindowInner* window =
            nsGlobalWindowInner::Cast(ref->Private()->GetAncestorWindow());
        if (window) {
          window->MaybeNotifyStorageKeyUsed();
        }
      }));
}

bool WorkerPrivate::Close() {
  mMutex.AssertCurrentThreadOwns();
  if (mParentStatus < Closing) {
    mParentStatus = Closing;
  }

  return true;
}

bool WorkerPrivate::ProxyReleaseMainThreadObjects() {
  AssertIsOnParentThread();
  MOZ_ASSERT(!mMainThreadObjectsForgotten);

  nsCOMPtr<nsILoadGroup> loadGroupToCancel;
  // If we're not overriden, then do nothing here.  Let the load group get
  // handled in ForgetMainThreadObjects().
  if (mLoadInfo.mInterfaceRequestor) {
    mLoadInfo.mLoadGroup.swap(loadGroupToCancel);
  }

  bool result = mLoadInfo.ProxyReleaseMainThreadObjects(
      this, std::move(loadGroupToCancel));

  mMainThreadObjectsForgotten = true;

  return result;
}

void WorkerPrivate::UpdateContextOptions(
    const JS::ContextOptions& aContextOptions) {
  AssertIsOnParentThread();

  {
    MutexAutoLock lock(mMutex);
    mJSSettings.contextOptions = aContextOptions;
  }

  RefPtr<UpdateContextOptionsRunnable> runnable =
      new UpdateContextOptionsRunnable(this, aContextOptions);
  if (!runnable->Dispatch(this)) {
    NS_WARNING("Failed to update worker context options!");
  }
}

void WorkerPrivate::UpdateLanguages(const nsTArray<nsString>& aLanguages) {
  AssertIsOnParentThread();

  RefPtr<UpdateLanguagesRunnable> runnable =
      new UpdateLanguagesRunnable(this, aLanguages);
  if (!runnable->Dispatch(this)) {
    NS_WARNING("Failed to update worker languages!");
  }
}

void WorkerPrivate::UpdateJSWorkerMemoryParameter(JSGCParamKey aKey,
                                                  Maybe<uint32_t> aValue) {
  AssertIsOnParentThread();

  bool changed = false;

  {
    MutexAutoLock lock(mMutex);
    changed = mJSSettings.ApplyGCSetting(aKey, aValue);
  }

  if (changed) {
    RefPtr<UpdateJSWorkerMemoryParameterRunnable> runnable =
        new UpdateJSWorkerMemoryParameterRunnable(this, aKey, aValue);
    if (!runnable->Dispatch(this)) {
      NS_WARNING("Failed to update memory parameter!");
    }
  }
}

#ifdef JS_GC_ZEAL
void WorkerPrivate::UpdateGCZeal(uint8_t aGCZeal, uint32_t aFrequency) {
  AssertIsOnParentThread();

  {
    MutexAutoLock lock(mMutex);
    mJSSettings.gcZeal = aGCZeal;
    mJSSettings.gcZealFrequency = aFrequency;
  }

  RefPtr<UpdateGCZealRunnable> runnable =
      new UpdateGCZealRunnable(this, aGCZeal, aFrequency);
  if (!runnable->Dispatch(this)) {
    NS_WARNING("Failed to update worker gczeal!");
  }
}
#endif

void WorkerPrivate::SetLowMemoryState(bool aState) {
  AssertIsOnParentThread();

  RefPtr<SetLowMemoryStateRunnable> runnable =
      new SetLowMemoryStateRunnable(this, aState);
  if (!runnable->Dispatch(this)) {
    NS_WARNING("Failed to set low memory state!");
  }
}

void WorkerPrivate::GarbageCollect(bool aShrinking) {
  AssertIsOnParentThread();

  RefPtr<GarbageCollectRunnable> runnable = new GarbageCollectRunnable(
      this, aShrinking, /* aCollectChildren = */ true);
  if (!runnable->Dispatch(this)) {
    NS_WARNING("Failed to GC worker!");
  }
}

void WorkerPrivate::CycleCollect() {
  AssertIsOnParentThread();

  RefPtr<CycleCollectRunnable> runnable =
      new CycleCollectRunnable(this, /* aCollectChildren = */ true);
  if (!runnable->Dispatch(this)) {
    NS_WARNING("Failed to CC worker!");
  }
}

void WorkerPrivate::OfflineStatusChangeEvent(bool aIsOffline) {
  AssertIsOnParentThread();

  RefPtr<OfflineStatusChangeRunnable> runnable =
      new OfflineStatusChangeRunnable(this, aIsOffline);
  if (!runnable->Dispatch(this)) {
    NS_WARNING("Failed to dispatch offline status change event!");
  }
}

void WorkerPrivate::OfflineStatusChangeEventInternal(bool aIsOffline) {
  auto data = mWorkerThreadAccessible.Access();

  // The worker is already in this state. No need to dispatch an event.
  if (data->mOnLine == !aIsOffline) {
    return;
  }

  for (uint32_t index = 0; index < data->mChildWorkers.Length(); ++index) {
    data->mChildWorkers[index]->OfflineStatusChangeEvent(aIsOffline);
  }

  data->mOnLine = !aIsOffline;
  WorkerGlobalScope* globalScope = GlobalScope();
  RefPtr<WorkerNavigator> nav = globalScope->GetExistingNavigator();
  if (nav) {
    nav->SetOnLine(data->mOnLine);
  }

  nsString eventType;
  if (aIsOffline) {
    eventType.AssignLiteral("offline");
  } else {
    eventType.AssignLiteral("online");
  }

  RefPtr<Event> event = NS_NewDOMEvent(globalScope, nullptr, nullptr);

  event->InitEvent(eventType, false, false);
  event->SetTrusted(true);

  globalScope->DispatchEvent(*event);
}

void WorkerPrivate::MemoryPressure() {
  AssertIsOnParentThread();

  RefPtr<MemoryPressureRunnable> runnable = new MemoryPressureRunnable(this);
  Unused << NS_WARN_IF(!runnable->Dispatch(this));
}

RefPtr<WorkerPrivate::JSMemoryUsagePromise> WorkerPrivate::GetJSMemoryUsage() {
  AssertIsOnMainThread();

  {
    MutexAutoLock lock(mMutex);
    // If we have started shutting down the worker, do not dispatch a runnable
    // to measure its memory.
    if (ParentStatus() > Running) {
      return nullptr;
    }
  }

  return InvokeAsync(ControlEventTarget(), __func__, []() {
    WorkerPrivate* wp = GetCurrentThreadWorkerPrivate();
    MOZ_ASSERT(wp);
    wp->AssertIsOnWorkerThread();
    MutexAutoLock lock(wp->mMutex);
    return JSMemoryUsagePromise::CreateAndResolve(
        js::GetGCHeapUsage(wp->mJSContext), __func__);
  });
}

void WorkerPrivate::WorkerScriptLoaded() {
  AssertIsOnMainThread();

  if (IsSharedWorker() || IsServiceWorker()) {
    // No longer need to hold references to the window or document we came from.
    mLoadInfo.mWindow = nullptr;
    mLoadInfo.mScriptContext = nullptr;
  }
}

void WorkerPrivate::SetBaseURI(nsIURI* aBaseURI) {
  AssertIsOnMainThread();

  if (!mLoadInfo.mBaseURI) {
    NS_ASSERTION(GetParent(), "Shouldn't happen without a parent!");
    mLoadInfo.mResolvedScriptURI = aBaseURI;
  }

  mLoadInfo.mBaseURI = aBaseURI;

  if (NS_FAILED(aBaseURI->GetSpec(mLocationInfo.mHref))) {
    mLocationInfo.mHref.Truncate();
  }

  mLocationInfo.mHostname.Truncate();
  nsContentUtils::GetHostOrIPv6WithBrackets(aBaseURI, mLocationInfo.mHostname);

  nsCOMPtr<nsIURL> url(do_QueryInterface(aBaseURI));
  if (!url || NS_FAILED(url->GetFilePath(mLocationInfo.mPathname))) {
    mLocationInfo.mPathname.Truncate();
  }

  nsCString temp;

  if (url && NS_SUCCEEDED(url->GetQuery(temp)) && !temp.IsEmpty()) {
    mLocationInfo.mSearch.Assign('?');
    mLocationInfo.mSearch.Append(temp);
  }

  if (NS_SUCCEEDED(aBaseURI->GetRef(temp)) && !temp.IsEmpty()) {
    if (mLocationInfo.mHash.IsEmpty()) {
      mLocationInfo.mHash.Assign('#');
      mLocationInfo.mHash.Append(temp);
    }
  }

  if (NS_SUCCEEDED(aBaseURI->GetScheme(mLocationInfo.mProtocol))) {
    mLocationInfo.mProtocol.Append(':');
  } else {
    mLocationInfo.mProtocol.Truncate();
  }

  int32_t port;
  if (NS_SUCCEEDED(aBaseURI->GetPort(&port)) && port != -1) {
    mLocationInfo.mPort.AppendInt(port);

    nsAutoCString host(mLocationInfo.mHostname);
    host.Append(':');
    host.Append(mLocationInfo.mPort);

    mLocationInfo.mHost.Assign(host);
  } else {
    mLocationInfo.mHost.Assign(mLocationInfo.mHostname);
  }

  nsContentUtils::GetWebExposedOriginSerialization(aBaseURI,
                                                   mLocationInfo.mOrigin);
}

nsresult WorkerPrivate::SetPrincipalsAndCSPOnMainThread(
    nsIPrincipal* aPrincipal, nsIPrincipal* aPartitionedPrincipal,
    nsILoadGroup* aLoadGroup, nsIContentSecurityPolicy* aCsp) {
  return mLoadInfo.SetPrincipalsAndCSPOnMainThread(
      aPrincipal, aPartitionedPrincipal, aLoadGroup, aCsp);
}

nsresult WorkerPrivate::SetPrincipalsAndCSPFromChannel(nsIChannel* aChannel) {
  return mLoadInfo.SetPrincipalsAndCSPFromChannel(aChannel);
}

bool WorkerPrivate::FinalChannelPrincipalIsValid(nsIChannel* aChannel) {
  return mLoadInfo.FinalChannelPrincipalIsValid(aChannel);
}

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
bool WorkerPrivate::PrincipalURIMatchesScriptURL() {
  return mLoadInfo.PrincipalURIMatchesScriptURL();
}
#endif

void WorkerPrivate::UpdateOverridenLoadGroup(nsILoadGroup* aBaseLoadGroup) {
  AssertIsOnMainThread();

  // The load group should have been overriden at init time.
  mLoadInfo.mInterfaceRequestor->MaybeAddBrowserChild(aBaseLoadGroup);
}

void WorkerPrivate::UpdateIsOnContentBlockingAllowList(
    bool aOnContentBlockingAllowList) {
  AssertIsOnWorkerThread();
  MOZ_DIAGNOSTIC_ASSERT(IsServiceWorker());

  RefPtr<StrongWorkerRef> strongRef = StrongWorkerRef::Create(
      this, "WorkerPrivate::UpdateIsOnContentBlockingAllowList");
  if (!strongRef) {
    return;
  }
  RefPtr<ThreadSafeWorkerRef> ref = new ThreadSafeWorkerRef(strongRef);
  DispatchToMainThread(NS_NewRunnableFunction(
      "WorkerPrivate::UpdateIsOnContentBlockingAllowList",
      [ref = std::move(ref), aOnContentBlockingAllowList] {
        ref->Private()
            ->mLoadInfo.mCookieJarSettingsArgs.isOnContentBlockingAllowList() =
            aOnContentBlockingAllowList;

        nsCOMPtr<nsICookieJarSettings> workerCookieJarSettings;
        net::CookieJarSettings::Deserialize(
            ref->Private()->mLoadInfo.mCookieJarSettingsArgs,
            getter_AddRefs(workerCookieJarSettings));
        bool shouldResistFingerprinting =
            nsContentUtils::ShouldResistFingerprinting_dangerous(
                ref->Private()->mLoadInfo.mPrincipal,
                "Service Workers exist outside a Document or Channel; as a "
                "property of the domain (and origin attributes). We don't have "
                "a "
                "CookieJarSettings to perform the *nested check*, but we can "
                "rely "
                "on the FPI/dFPI partition key check. The WorkerPrivate's "
                "ShouldResistFingerprinting function for the ServiceWorker "
                "depends "
                "on this boolean and will also consider an explicit RFPTarget.",
                RFPTarget::IsAlwaysEnabledForPrecompute) &&
            !nsContentUtils::ETPSaysShouldNotResistFingerprinting(
                workerCookieJarSettings, false);

        ref->Private()
            ->mLoadInfo.mCookieJarSettingsArgs.shouldResistFingerprinting() =
            shouldResistFingerprinting;
        ref->Private()->mLoadInfo.mShouldResistFingerprinting =
            shouldResistFingerprinting;
      }));

  /* From:
    https://searchfox.org/mozilla-central/rev/964b8aa226c68bbf83c9ffc38984804734bb0de2/js/public/RealmOptions.h#316-318
    > RealmCreationOptions specify fundamental realm characteristics that must
    be specified when the realm is created, that can't be changed after the
    realm is created.
  */
  /*
  nsCString locale;
  if (aEnabled) {
    locale = nsRFPService::GetSpoofedJSLocale();
  }

  MutexAutoLock lock(mMutex);
  mJSSettings.chromeRealmOptions.creationOptions().setForceUTC(aEnabled);
  mJSSettings.chromeRealmOptions.creationOptions().setAlwaysUseFdlibm(aEnabled);
  if (aEnabled) {
    mJSSettings.chromeRealmOptions.creationOptions().setLocaleCopyZ(
        locale.get());
  }

  mJSSettings.contentRealmOptions.creationOptions().setForceUTC(aEnabled);
  mJSSettings.contentRealmOptions.creationOptions().setAlwaysUseFdlibm(
      aEnabled);
  if (aEnabled) {
    mJSSettings.contentRealmOptions.creationOptions().setLocaleCopyZ(
        locale.get());
  }
  */
}

bool WorkerPrivate::IsOnParentThread() const {
  if (GetParent()) {
    return GetParent()->IsOnWorkerThread();
  }
  return NS_IsMainThread();
}

#ifdef DEBUG

void WorkerPrivate::AssertIsOnParentThread() const {
  if (GetParent()) {
    GetParent()->AssertIsOnWorkerThread();
  } else {
    AssertIsOnMainThread();
  }
}

void WorkerPrivate::AssertInnerWindowIsCorrect() const {
  AssertIsOnParentThread();

  // Only care about top level workers from windows.
  if (mParent || !mLoadInfo.mWindow) {
    return;
  }

  AssertIsOnMainThread();

  nsPIDOMWindowOuter* outer = mLoadInfo.mWindow->GetOuterWindow();
  NS_ASSERTION(outer && outer->GetCurrentInnerWindow() == mLoadInfo.mWindow,
               "Inner window no longer correct!");
}

#endif

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
bool WorkerPrivate::PrincipalIsValid() const {
  return mLoadInfo.PrincipalIsValid();
}
#endif

WorkerPrivate::WorkerThreadAccessible::WorkerThreadAccessible(
    WorkerPrivate* const aParent)
    : mNumWorkerRefsPreventingShutdownStart(0),
      mDebuggerEventLoopLevel(0),
      mNonblockingCCBackgroundActorCount(0),
      mErrorHandlerRecursionCount(0),
      mNextTimeoutId(1),
      mCurrentTimerNestingLevel(0),
      mFrozen(false),
      mDebuggerInterruptRequested(false),
      mTimerRunning(false),
      mRunningExpiredTimeouts(false),
      mPeriodicGCTimerRunning(false),
      mIdleGCTimerRunning(false),
      mOnLine(aParent ? aParent->OnLine() : !NS_IsOffline()),
      mJSThreadExecutionGranted(false),
      mCCCollectedAnything(false) {}

namespace {

bool IsNewWorkerSecureContext(const WorkerPrivate* const aParent,
                              const WorkerKind aWorkerKind,
                              const WorkerLoadInfo& aLoadInfo) {
  if (aParent) {
    return aParent->IsSecureContext();
  }

  // Our secure context state depends on the kind of worker we have.

  if (aLoadInfo.mPrincipal && aLoadInfo.mPrincipal->IsSystemPrincipal()) {
    return true;
  }

  if (aWorkerKind == WorkerKindService) {
    return true;
  }

  if (aLoadInfo.mSecureContext != WorkerLoadInfo::eNotSet) {
    return aLoadInfo.mSecureContext == WorkerLoadInfo::eSecureContext;
  }

  MOZ_ASSERT_UNREACHABLE(
      "non-chrome worker that is not a service worker "
      "that has no parent and no associated window");

  return false;
}

}  // namespace

WorkerPrivate::WorkerPrivate(
    WorkerPrivate* aParent, const nsAString& aScriptURL, bool aIsChromeWorker,
    WorkerKind aWorkerKind, RequestCredentials aRequestCredentials,
    enum WorkerType aWorkerType, const nsAString& aWorkerName,
    const nsACString& aServiceWorkerScope, WorkerLoadInfo& aLoadInfo,
    nsString&& aId, const nsID& aAgentClusterId,
    const nsILoadInfo::CrossOriginOpenerPolicy aAgentClusterOpenerPolicy,
    CancellationCallback&& aCancellationCallback,
    TerminationCallback&& aTerminationCallback,
    mozilla::ipc::Endpoint<PRemoteWorkerNonLifeCycleOpControllerChild>&&
        aChildEp)
    : mMutex("WorkerPrivate Mutex"),
      mCondVar(mMutex, "WorkerPrivate CondVar"),
      mParent(aParent),
      mScriptURL(aScriptURL),
      mWorkerName(aWorkerName),
      mCredentialsMode(aRequestCredentials),
      mWorkerType(aWorkerType),  // If the worker runs as a script or a module
      mWorkerKind(aWorkerKind),
      mCancellationCallback(std::move(aCancellationCallback)),
      mTerminationCallback(std::move(aTerminationCallback)),
      mLoadInfo(std::move(aLoadInfo)),
      mDebugger(nullptr),
      mJSContext(nullptr),
      mPRThread(nullptr),
      mWorkerControlEventTarget(new WorkerEventTarget(
          this, WorkerEventTarget::Behavior::ControlOnly)),
      mWorkerHybridEventTarget(
          new WorkerEventTarget(this, WorkerEventTarget::Behavior::Hybrid)),
      mChildEp(std::move(aChildEp)),
      mRemoteDebuggerRegistered(false),
      mRemoteDebuggerReady(true),
      mIsQueued(false),
      mDebuggerBindingCondVar(mMutex,
                              "WorkerPrivate RemoteDebuggerBindingCondVar"),
      mWorkerDebuggerEventTarget(new WorkerEventTarget(
          this, WorkerEventTarget::Behavior::DebuggerOnly)),
      mParentStatus(Pending),
      mStatus(Pending),
      mCreationTimeStamp(TimeStamp::Now()),
      mCreationTimeHighRes((double)PR_Now() / PR_USEC_PER_MSEC),
      mReportedUseCounters(false),
      mAgentClusterId(aAgentClusterId),
      mWorkerThreadAccessible(aParent),
      mPostSyncLoopOperations(0),
      mParentWindowPaused(false),
      mWorkerScriptExecutedSuccessfully(false),
      mFetchHandlerWasAdded(false),
      mMainThreadObjectsForgotten(false),
      mIsChromeWorker(aIsChromeWorker),
      mParentFrozen(false),
      mIsSecureContext(
          IsNewWorkerSecureContext(mParent, mWorkerKind, mLoadInfo)),
      mDebuggerRegistered(false),
      mIsInBackground(false),
      mDebuggerReady(true),
      mExtensionAPIAllowed(false),
      mIsInAutomation(false),
      mId(std::move(aId)),
      mAgentClusterOpenerPolicy(aAgentClusterOpenerPolicy),
      mIsPrivilegedAddonGlobal(false),
      mTopLevelWorkerFinishedRunnableCount(0),
      mWorkerFinishedRunnableCount(0) {
  LOG(WorkerLog(), ("WorkerPrivate::WorkerPrivate [%p]", this));
  MOZ_ASSERT_IF(!IsDedicatedWorker(), NS_IsMainThread());

  if (aParent) {
    aParent->AssertIsOnWorkerThread();

    // Note that this copies our parent's secure context state into mJSSettings.
    aParent->CopyJSSettings(mJSSettings);

    MOZ_ASSERT_IF(mIsChromeWorker, mIsSecureContext);

    mIsInAutomation = aParent->IsInAutomation();

    MOZ_ASSERT(IsDedicatedWorker());

    if (aParent->mParentFrozen) {
      Freeze(nullptr);
    }

    if (aParent->IsRunningInBackground()) {
      mIsInBackground = true;
    }

    mIsPrivilegedAddonGlobal = aParent->mIsPrivilegedAddonGlobal;
  } else {
    AssertIsOnMainThread();

    RuntimeService::GetDefaultJSSettings(mJSSettings);

    {
      JS::RealmOptions& chromeRealmOptions = mJSSettings.chromeRealmOptions;
      JS::RealmOptions& contentRealmOptions = mJSSettings.contentRealmOptions;

      xpc::InitGlobalObjectOptions(
          chromeRealmOptions, UsesSystemPrincipal(), mIsSecureContext,
          ShouldResistFingerprinting(RFPTarget::JSDateTimeUTC),
          ShouldResistFingerprinting(RFPTarget::JSMathFdlibm),
          ShouldResistFingerprinting(RFPTarget::JSLocale));
      xpc::InitGlobalObjectOptions(
          contentRealmOptions, UsesSystemPrincipal(), mIsSecureContext,
          ShouldResistFingerprinting(RFPTarget::JSDateTimeUTC),
          ShouldResistFingerprinting(RFPTarget::JSMathFdlibm),
          ShouldResistFingerprinting(RFPTarget::JSLocale));

      // Check if it's a privileged addon executing in order to allow access
      // to SharedArrayBuffer
      if (mLoadInfo.mPrincipal) {
        if (auto* policy =
                BasePrincipal::Cast(mLoadInfo.mPrincipal)->AddonPolicy()) {
          if (policy->IsPrivileged() &&
              ExtensionPolicyService::GetSingleton().IsExtensionProcess()) {
            // Privileged extensions are allowed to use SharedArrayBuffer in
            // their extension process, but never in content scripts in
            // content processes.
            mIsPrivilegedAddonGlobal = true;
          }

          if (StaticPrefs::
                  extensions_backgroundServiceWorker_enabled_AtStartup() &&
              mWorkerKind == WorkerKindService &&
              policy->IsManifestBackgroundWorker(mScriptURL)) {
            // Only allows ExtensionAPI for extension service workers
            // that are declared in the extension manifest json as
            // the background service worker.
            mExtensionAPIAllowed = true;
          }
        }
      }

      // The SharedArrayBuffer global constructor property should not be present
      // in a fresh global object when shared memory objects aren't allowed
      // (because COOP/COEP support isn't enabled, or because COOP/COEP don't
      // act to isolate this worker to a separate process).
      const bool defineSharedArrayBufferConstructor = IsSharedMemoryAllowed();
      chromeRealmOptions.creationOptions()
          .setDefineSharedArrayBufferConstructor(
              defineSharedArrayBufferConstructor);
      contentRealmOptions.creationOptions()
          .setDefineSharedArrayBufferConstructor(
              defineSharedArrayBufferConstructor);
    }

    mIsInAutomation = xpc::IsInAutomation();

    // Our parent can get suspended after it initiates the async creation
    // of a new worker thread.  In this case suspend the new worker as well.
    if (mLoadInfo.mWindow &&
        nsGlobalWindowInner::Cast(mLoadInfo.mWindow)->IsSuspended()) {
      ParentWindowPaused();
    }

    if (mLoadInfo.mWindow &&
        nsGlobalWindowInner::Cast(mLoadInfo.mWindow)->IsFrozen()) {
      Freeze(mLoadInfo.mWindow);
    }

    if (mLoadInfo.mWindow && mLoadInfo.mWindow->GetOuterWindow() &&
        mLoadInfo.mWindow->GetOuterWindow()->IsBackground()) {
      mIsInBackground = true;
    }
  }

  nsCOMPtr<nsISerialEventTarget> target;

  // A child worker just inherits the parent workers ThrottledEventQueue
  // and main thread target for now.  This is mainly due to the restriction
  // that ThrottledEventQueue can only be created on the main thread at the
  // moment.
  if (aParent) {
    mMainThreadEventTargetForMessaging =
        aParent->mMainThreadEventTargetForMessaging;
    mMainThreadEventTarget = aParent->mMainThreadEventTarget;
    mMainThreadDebuggeeEventTarget = aParent->mMainThreadDebuggeeEventTarget;
    return;
  }

  MOZ_ASSERT(NS_IsMainThread());
  target = GetWindow()
               ? GetWindow()->GetBrowsingContextGroup()->GetWorkerEventQueue()
               : nullptr;

  if (!target) {
    target = GetMainThreadSerialEventTarget();
    MOZ_DIAGNOSTIC_ASSERT(target);
  }

  // Throttle events to the main thread using a ThrottledEventQueue specific to
  // this tree of worker threads.
  mMainThreadEventTargetForMessaging =
      ThrottledEventQueue::Create(target, "Worker queue for messaging");
  mMainThreadEventTarget = ThrottledEventQueue::Create(
      GetMainThreadSerialEventTarget(), "Worker queue",
      nsIRunnablePriority::PRIORITY_MEDIUMHIGH);
  mMainThreadDebuggeeEventTarget =
      ThrottledEventQueue::Create(target, "Worker debuggee queue");
  if (IsParentWindowPaused() || IsFrozen()) {
    MOZ_ALWAYS_SUCCEEDS(mMainThreadDebuggeeEventTarget->SetIsPaused(true));
  }
}

WorkerPrivate::~WorkerPrivate() {
  MOZ_DIAGNOSTIC_ASSERT(mTopLevelWorkerFinishedRunnableCount == 0);
  MOZ_DIAGNOSTIC_ASSERT(mWorkerFinishedRunnableCount == 0);

  mWorkerDebuggerEventTarget->ForgetWorkerPrivate(this);

  mWorkerControlEventTarget->ForgetWorkerPrivate(this);

  // We force the hybrid event target to forget the thread when we
  // enter the Killing state, but we do it again here to be safe.
  // Its possible that we may be created and destroyed without progressing
  // to Killing via some obscure code path.
  mWorkerHybridEventTarget->ForgetWorkerPrivate(this);
}

WorkerPrivate::AgentClusterIdAndCoop
WorkerPrivate::ComputeAgentClusterIdAndCoop(WorkerPrivate* aParent,
                                            WorkerKind aWorkerKind,
                                            WorkerLoadInfo* aLoadInfo,
                                            bool aIsChromeWorker) {
  nsILoadInfo::CrossOriginOpenerPolicy agentClusterCoop =
      nsILoadInfo::OPENER_POLICY_UNSAFE_NONE;

  if (aParent) {
    MOZ_ASSERT(aWorkerKind == WorkerKind::WorkerKindDedicated);

    return {aParent->AgentClusterId(), aParent->mAgentClusterOpenerPolicy};
  }

  AssertIsOnMainThread();

  if (aWorkerKind == WorkerKind::WorkerKindService ||
      aWorkerKind == WorkerKind::WorkerKindShared) {
    return {aLoadInfo->mAgentClusterId, agentClusterCoop};
  }

  if (aLoadInfo->mWindow) {
    Document* doc = aLoadInfo->mWindow->GetExtantDoc();
    MOZ_DIAGNOSTIC_ASSERT(doc);
    RefPtr<DocGroup> docGroup = doc->GetDocGroup();

    nsID agentClusterId =
        docGroup ? docGroup->AgentClusterId() : nsID::GenerateUUID();

    BrowsingContext* bc = aLoadInfo->mWindow->GetBrowsingContext();
    MOZ_DIAGNOSTIC_ASSERT(bc);
    return {agentClusterId, bc->Top()->GetOpenerPolicy()};
  }

  // Chrome workers share an AgentCluster with the XPC module global. This
  // allows things like shared memory and WASM modules to be transferred between
  // chrome workers and system JS.
  // Also set COOP+COEP flags to allow access to shared memory.
  if (aIsChromeWorker) {
    if (nsIGlobalObject* systemGlobal =
            xpc::NativeGlobal(xpc::PrivilegedJunkScope())) {
      nsID agentClusterId = systemGlobal->GetAgentClusterId().valueOrFrom(
          [] { return nsID::GenerateUUID(); });
      return {
          agentClusterId,
          nsILoadInfo::OPENER_POLICY_SAME_ORIGIN_EMBEDDER_POLICY_REQUIRE_CORP};
    }
  }

  // If the window object was failed to be set into the WorkerLoadInfo, we
  // make the worker into another agent cluster group instead of failures.
  return {nsID::GenerateUUID(), agentClusterCoop};
}

// static
already_AddRefed<WorkerPrivate> WorkerPrivate::Constructor(
    JSContext* aCx, const nsAString& aScriptURL, bool aIsChromeWorker,
    WorkerKind aWorkerKind, RequestCredentials aRequestCredentials,
    enum WorkerType aWorkerType, const nsAString& aWorkerName,
    const nsACString& aServiceWorkerScope, WorkerLoadInfo* aLoadInfo,
    ErrorResult& aRv, nsString aId,
    CancellationCallback&& aCancellationCallback,
    TerminationCallback&& aTerminationCallback,
    mozilla::ipc::Endpoint<PRemoteWorkerNonLifeCycleOpControllerChild>&&
        aChildEp) {
  WorkerPrivate* parent =
      NS_IsMainThread() ? nullptr : GetCurrentThreadWorkerPrivate();

  // If this is a sub-worker, we need to keep the parent worker alive until this
  // one is registered.
  RefPtr<StrongWorkerRef> workerRef;
  if (parent) {
    parent->AssertIsOnWorkerThread();

    workerRef = StrongWorkerRef::Create(parent, "WorkerPrivate::Constructor");
    if (NS_WARN_IF(!workerRef)) {
      aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
      return nullptr;
    }
  } else {
    AssertIsOnMainThread();
  }

  Maybe<WorkerLoadInfo> stackLoadInfo;
  if (!aLoadInfo) {
    stackLoadInfo.emplace();

    nsresult rv = GetLoadInfo(
        aCx, nullptr, parent, aScriptURL, aWorkerType, aRequestCredentials,
        aIsChromeWorker, InheritLoadGroup, aWorkerKind, stackLoadInfo.ptr());
    aRv.MightThrowJSException();
    if (NS_FAILED(rv)) {
      workerinternals::ReportLoadError(aRv, rv, aScriptURL);
      return nullptr;
    }

    aLoadInfo = stackLoadInfo.ptr();
  }

  // NB: This has to be done before creating the WorkerPrivate, because it will
  // attempt to use static variables that are initialized in the RuntimeService
  // constructor.
  RuntimeService* runtimeService;

  if (!parent) {
    runtimeService = RuntimeService::GetOrCreateService();
    if (!runtimeService) {
      aRv.Throw(NS_ERROR_FAILURE);
      return nullptr;
    }
  } else {
    runtimeService = RuntimeService::GetService();
  }

  MOZ_ASSERT(runtimeService);

  // Don't create a worker with the shutting down RuntimeService.
  if (runtimeService->IsShuttingDown()) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  AgentClusterIdAndCoop idAndCoop = ComputeAgentClusterIdAndCoop(
      parent, aWorkerKind, aLoadInfo, aIsChromeWorker);

  RefPtr<WorkerPrivate> worker = new WorkerPrivate(
      parent, aScriptURL, aIsChromeWorker, aWorkerKind, aRequestCredentials,
      aWorkerType, aWorkerName, aServiceWorkerScope, *aLoadInfo, std::move(aId),
      idAndCoop.mId, idAndCoop.mCoop, std::move(aCancellationCallback),
      std::move(aTerminationCallback), std::move(aChildEp));

  // Gecko contexts always have an explicitly-set default locale (set by
  // XPJSRuntime::Initialize for the main thread, set by
  // WorkerThreadPrimaryRunnable::Run for workers just before running worker
  // code), so this is never SpiderMonkey's builtin default locale.
  JS::UniqueChars defaultLocale = JS_GetDefaultLocale(aCx);
  if (NS_WARN_IF(!defaultLocale)) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  worker->mDefaultLocale = std::move(defaultLocale);

  // Create remote debugger endpoint here for child binding in
  // WorkerThreadPrimaryRunnable
  worker->CreateRemoteDebuggerEndpoints();

  if (!runtimeService->RegisterWorker(*worker)) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  // From this point on (worker thread has been started) we
  // must keep ourself alive. We can now only be cleared by
  // ClearSelfAndParentEventTargetRef().
  worker->mSelfRef = worker;
  worker->mParentRef = MakeRefPtr<WorkerParentRef>(worker);

  // Enable remote worker debugger when the worker is really scheduled.
  /*
  if (!worker->mIsQueued) {
    worker->EnableRemoteDebugger();
  }
  */

  worker->EnableDebugger();

  MOZ_DIAGNOSTIC_ASSERT(worker->PrincipalIsValid());

  UniquePtr<SerializedStackHolder> stack;
  if (worker->IsWatchedByDevTools()) {
    stack = GetCurrentStackForNetMonitor(aCx);
  }

  // This should be non-null for dedicated workers and null for Shared and
  // Service workers. All Encoding values are static and will live as long
  // as the process and the convention is to therefore use raw pointers.
  const mozilla::Encoding* aDocumentEncoding =
      NS_IsMainThread() && !worker->GetParent() && worker->GetDocument()
          ? worker->GetDocument()->GetDocumentCharacterSet().get()
          : nullptr;

  RefPtr<CompileScriptRunnable> compiler = new CompileScriptRunnable(
      worker, std::move(stack), aScriptURL, aDocumentEncoding);
  if (!compiler->Dispatch(worker)) {
    aRv.Throw(NS_ERROR_UNEXPECTED);
    return nullptr;
  }

  return worker.forget();
}

// Mark worker private as running in the background tab
// for further throttling
void WorkerPrivate::SetIsRunningInBackground() {
  AssertIsOnParentThread();

  RefPtr<ChangeBackgroundStateRunnable> runnable =
      new ChangeBackgroundStateRunnable(this, true);
  runnable->Dispatch(this);

  LOG(WorkerLog(), ("SetIsRunningInBackground [%p]", this));
}

void WorkerPrivate::SetIsRunningInForeground() {
  AssertIsOnParentThread();

  RefPtr<ChangeBackgroundStateRunnable> runnable =
      new ChangeBackgroundStateRunnable(this, false);
  runnable->Dispatch(this);

  LOG(WorkerLog(), ("SetIsRunningInForeground [%p]", this));
}

nsresult WorkerPrivate::SetIsDebuggerReady(bool aReady) {
  AssertIsOnMainThread();
  MutexAutoLock lock(mMutex);

  if (mDebuggerReady == aReady) {
    return NS_OK;
  }

  if (!aReady && mDebuggerRegistered) {
    // The debugger can only be marked as not ready during registration.
    return NS_ERROR_FAILURE;
  }

  mDebuggerReady = aReady;

  bool debuggerRegistered = mDebuggerRegistered && (mRemoteDebuggerRegistered ||
                                                    XRE_IsParentProcess());

  if (aReady && debuggerRegistered) {
    // Dispatch all the delayed runnables without releasing the lock, to ensure
    // that the order in which debuggee runnables execute is the same as the
    // order in which they were originally dispatched.
    auto pending = std::move(mDelayedDebuggeeRunnables);
    for (uint32_t i = 0; i < pending.Length(); i++) {
      RefPtr<WorkerRunnable> runnable = std::move(pending[i]);
      nsresult rv = DispatchLockHeld(runnable.forget(), nullptr, lock);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    MOZ_RELEASE_ASSERT(mDelayedDebuggeeRunnables.IsEmpty());
  }

  return NS_OK;
}

// static
nsresult WorkerPrivate::GetLoadInfo(
    JSContext* aCx, nsPIDOMWindowInner* aWindow, WorkerPrivate* aParent,
    const nsAString& aScriptURL, const enum WorkerType& aWorkerType,
    const RequestCredentials& aCredentials, bool aIsChromeWorker,
    LoadGroupBehavior aLoadGroupBehavior, WorkerKind aWorkerKind,
    WorkerLoadInfo* aLoadInfo) {
  using namespace mozilla::dom::workerinternals;

  MOZ_ASSERT(aCx);
  MOZ_ASSERT_IF(NS_IsMainThread(),
                aCx == nsContentUtils::GetCurrentJSContext());

  if (aWindow) {
    AssertIsOnMainThread();
  }

  WorkerLoadInfo loadInfo;
  nsresult rv;

  if (aParent) {
    aParent->AssertIsOnWorkerThread();

    // If the parent is going away give up now.
    WorkerStatus parentStatus;
    {
      MutexAutoLock lock(aParent->mMutex);
      parentStatus = aParent->mStatus;
    }

    if (parentStatus > Running) {
      return NS_ERROR_FAILURE;
    }

    // Passing a pointer to our stack loadInfo is safe here because this
    // method uses a sync runnable to get the channel from the main thread.
    rv = ChannelFromScriptURLWorkerThread(aCx, aParent, aScriptURL, aWorkerType,
                                          aCredentials, loadInfo);
    if (NS_FAILED(rv)) {
      MOZ_ALWAYS_TRUE(loadInfo.ProxyReleaseMainThreadObjects(aParent));
      return rv;
    }

    // Now that we've spun the loop there's no guarantee that our parent is
    // still alive.  We may have received control messages initiating shutdown.
    {
      MutexAutoLock lock(aParent->mMutex);
      parentStatus = aParent->mStatus;
    }

    if (parentStatus > Running) {
      MOZ_ALWAYS_TRUE(loadInfo.ProxyReleaseMainThreadObjects(aParent));
      return NS_ERROR_FAILURE;
    }

    loadInfo.mTrials = aParent->Trials();
    loadInfo.mDomain = aParent->Domain();
    loadInfo.mFromWindow = aParent->IsFromWindow();
    loadInfo.mWindowID = aParent->WindowID();
    loadInfo.mAssociatedBrowsingContextID =
        aParent->AssociatedBrowsingContextID();
    loadInfo.mStorageAccess = aParent->StorageAccess();
    loadInfo.mUseRegularPrincipal = aParent->UseRegularPrincipal();
    loadInfo.mUsingStorageAccess = aParent->UsingStorageAccess();
    loadInfo.mCookieJarSettings = aParent->CookieJarSettings();
    if (loadInfo.mCookieJarSettings) {
      loadInfo.mCookieJarSettingsArgs = aParent->CookieJarSettingsArgs();
    }
    loadInfo.mOriginAttributes = aParent->GetOriginAttributes();
    loadInfo.mServiceWorkersTestingInWindow =
        aParent->ServiceWorkersTestingInWindow();
    loadInfo.mIsThirdPartyContext = aParent->IsThirdPartyContext();
    loadInfo.mShouldResistFingerprinting = aParent->ShouldResistFingerprinting(
        RFPTarget::IsAlwaysEnabledForPrecompute);
    loadInfo.mOverriddenFingerprintingSettings =
        aParent->GetOverriddenFingerprintingSettings();
    loadInfo.mParentController = aParent->GlobalScope()->GetController();
    loadInfo.mWatchedByDevTools = aParent->IsWatchedByDevTools();
    loadInfo.mIsOn3PCBExceptionList = aParent->IsOn3PCBExceptionList();
  } else {
    AssertIsOnMainThread();

    // Make sure that the IndexedDatabaseManager is set up
    IndexedDatabaseManager* idm = IndexedDatabaseManager::GetOrCreate();
    if (idm) {
      Unused << NS_WARN_IF(NS_FAILED(idm->EnsureLocale()));
    } else {
      NS_WARNING("Failed to get IndexedDatabaseManager!");
    }

    nsIScriptSecurityManager* ssm = nsContentUtils::GetSecurityManager();
    MOZ_ASSERT(ssm);

    bool isChrome = nsContentUtils::IsSystemCaller(aCx);

    // First check to make sure the caller has permission to make a privileged
    // worker if they called the ChromeWorker/ChromeSharedWorker constructor.
    if (aIsChromeWorker && !isChrome) {
      return NS_ERROR_DOM_SECURITY_ERR;
    }

    // Chrome callers (whether creating a ChromeWorker or Worker) always get the
    // system principal here as they're allowed to load anything. The script
    // loader will refuse to run any script that does not also have the system
    // principal.
    if (isChrome) {
      rv = ssm->GetSystemPrincipal(getter_AddRefs(loadInfo.mLoadingPrincipal));
      NS_ENSURE_SUCCESS(rv, rv);
    }

    // See if we're being called from a window.
    nsCOMPtr<nsPIDOMWindowInner> globalWindow = aWindow;
    if (!globalWindow) {
      globalWindow = xpc::CurrentWindowOrNull(aCx);
    }

    nsCOMPtr<Document> document;
    Maybe<ClientInfo> clientInfo;

    if (globalWindow) {
      // Only use the current inner window, and only use it if the caller can
      // access it.
      if (nsPIDOMWindowOuter* outerWindow = globalWindow->GetOuterWindow()) {
        loadInfo.mWindow = outerWindow->GetCurrentInnerWindow();
      }

      loadInfo.mTrials =
          OriginTrials::FromWindow(nsGlobalWindowInner::Cast(loadInfo.mWindow));

      BrowsingContext* browsingContext = globalWindow->GetBrowsingContext();

      // TODO: fix this for SharedWorkers with multiple documents (bug
      // 1177935)
      loadInfo.mServiceWorkersTestingInWindow =
          browsingContext &&
          browsingContext->Top()->ServiceWorkersTestingEnabled();

      if (!loadInfo.mWindow ||
          (globalWindow != loadInfo.mWindow &&
           !nsContentUtils::CanCallerAccess(loadInfo.mWindow))) {
        return NS_ERROR_DOM_SECURITY_ERR;
      }

      nsCOMPtr<nsIScriptGlobalObject> sgo = do_QueryInterface(loadInfo.mWindow);
      MOZ_ASSERT(sgo);

      loadInfo.mScriptContext = sgo->GetContext();
      NS_ENSURE_TRUE(loadInfo.mScriptContext, NS_ERROR_FAILURE);

      // If we're called from a window then we can dig out the principal and URI
      // from the document.
      document = loadInfo.mWindow->GetExtantDoc();
      NS_ENSURE_TRUE(document, NS_ERROR_FAILURE);

      loadInfo.mBaseURI = document->GetDocBaseURI();
      loadInfo.mLoadGroup = document->GetDocumentLoadGroup();
      NS_ENSURE_TRUE(loadInfo.mLoadGroup, NS_ERROR_FAILURE);

      clientInfo = globalWindow->GetClientInfo();

      // Use the document's NodePrincipal as loading principal if we're not
      // being called from chrome.
      if (!loadInfo.mLoadingPrincipal) {
        loadInfo.mLoadingPrincipal = document->NodePrincipal();
        NS_ENSURE_TRUE(loadInfo.mLoadingPrincipal, NS_ERROR_FAILURE);

        // We use the document's base domain to limit the number of workers
        // each domain can create. For sandboxed documents, we use the domain
        // of their first non-sandboxed document, walking up until we find
        // one. If we can't find one, we fall back to using the GUID of the
        // null principal as the base domain.
        if (document->GetSandboxFlags() & SANDBOXED_ORIGIN) {
          nsCOMPtr<Document> tmpDoc = document;
          do {
            tmpDoc = tmpDoc->GetInProcessParentDocument();
          } while (tmpDoc && tmpDoc->GetSandboxFlags() & SANDBOXED_ORIGIN);

          if (tmpDoc) {
            // There was an unsandboxed ancestor, yay!
            nsCOMPtr<nsIPrincipal> tmpPrincipal = tmpDoc->NodePrincipal();
            rv = tmpPrincipal->GetBaseDomain(loadInfo.mDomain);
            NS_ENSURE_SUCCESS(rv, rv);
          } else {
            // No unsandboxed ancestor, use our GUID.
            rv = loadInfo.mLoadingPrincipal->GetBaseDomain(loadInfo.mDomain);
            NS_ENSURE_SUCCESS(rv, rv);
          }
        } else {
          // Document creating the worker is not sandboxed.
          rv = loadInfo.mLoadingPrincipal->GetBaseDomain(loadInfo.mDomain);
          NS_ENSURE_SUCCESS(rv, rv);
        }
      }

      NS_ENSURE_TRUE(NS_LoadGroupMatchesPrincipal(loadInfo.mLoadGroup,
                                                  loadInfo.mLoadingPrincipal),
                     NS_ERROR_FAILURE);

      nsCOMPtr<nsIPermissionManager> permMgr =
          do_GetService(NS_PERMISSIONMANAGER_CONTRACTID, &rv);
      NS_ENSURE_SUCCESS(rv, rv);

      uint32_t perm;
      rv = permMgr->TestPermissionFromPrincipal(loadInfo.mLoadingPrincipal,
                                                "systemXHR"_ns, &perm);
      NS_ENSURE_SUCCESS(rv, rv);

      loadInfo.mXHRParamsAllowed = perm == nsIPermissionManager::ALLOW_ACTION;

      loadInfo.mWatchedByDevTools =
          browsingContext && browsingContext->WatchedByDevTools();

      loadInfo.mReferrerInfo =
          ReferrerInfo::CreateForFetch(loadInfo.mLoadingPrincipal, document);
      loadInfo.mFromWindow = true;
      loadInfo.mWindowID = globalWindow->WindowID();
      loadInfo.mAssociatedBrowsingContextID =
          globalWindow->GetBrowsingContext()->Id();
      loadInfo.mStorageAccess = StorageAllowedForWindow(globalWindow);
      loadInfo.mUseRegularPrincipal = document->UseRegularPrincipal();
      loadInfo.mUsingStorageAccess = document->UsingStorageAccess();
      loadInfo.mShouldResistFingerprinting =
          document->ShouldResistFingerprinting(
              RFPTarget::IsAlwaysEnabledForPrecompute);
      loadInfo.mOverriddenFingerprintingSettings =
          document->GetOverriddenFingerprintingSettings();
      loadInfo.mIsOn3PCBExceptionList = document->IsOn3PCBExceptionList();

      // This is an hack to deny the storage-access-permission for workers of
      // sub-iframes.
      if (loadInfo.mUsingStorageAccess &&
          StorageAllowedForDocument(document) != StorageAccess::eAllow) {
        loadInfo.mUsingStorageAccess = false;
      }
      loadInfo.mIsThirdPartyContext =
          AntiTrackingUtils::IsThirdPartyWindow(globalWindow, nullptr);
      loadInfo.mCookieJarSettings = document->CookieJarSettings();
      if (loadInfo.mCookieJarSettings) {
        auto* cookieJarSettings =
            net::CookieJarSettings::Cast(loadInfo.mCookieJarSettings);
        cookieJarSettings->Serialize(loadInfo.mCookieJarSettingsArgs);
      }
      StoragePrincipalHelper::GetRegularPrincipalOriginAttributes(
          document, loadInfo.mOriginAttributes);
      loadInfo.mParentController = globalWindow->GetController();
      loadInfo.mSecureContext = loadInfo.mWindow->IsSecureContext()
                                    ? WorkerLoadInfo::eSecureContext
                                    : WorkerLoadInfo::eInsecureContext;
    } else {
      // Not a window
      MOZ_ASSERT(isChrome);

      // We're being created outside of a window. Need to figure out the script
      // that is creating us in order for us to use relative URIs later on.
      JS::AutoFilename fileName;
      if (JS::DescribeScriptedCaller(&fileName, aCx)) {
        // In most cases, fileName is URI. In a few other cases
        // (e.g. xpcshell), fileName is a file path. Ideally, we would
        // prefer testing whether fileName parses as an URI and fallback
        // to file path in case of error, but Windows file paths have
        // the interesting property that they can be parsed as bogus
        // URIs (e.g. C:/Windows/Tmp is interpreted as scheme "C",
        // hostname "Windows", path "Tmp"), which defeats this algorithm.
        // Therefore, we adopt the opposite convention.
        nsCOMPtr<nsIFile> scriptFile;
        rv = NS_NewNativeLocalFile(nsDependentCString(fileName.get()),
                                   getter_AddRefs(scriptFile));
        if (NS_SUCCEEDED(rv)) {
          rv = NS_NewFileURI(getter_AddRefs(loadInfo.mBaseURI), scriptFile);
        }
        if (NS_FAILED(rv)) {
          // As expected, fileName is not a path, so proceed with
          // a uri.
          rv = NS_NewURI(getter_AddRefs(loadInfo.mBaseURI), fileName.get());
        }
        if (NS_FAILED(rv)) {
          return rv;
        }
      }
      loadInfo.mXHRParamsAllowed = true;
      loadInfo.mFromWindow = false;
      loadInfo.mWindowID = UINT64_MAX;
      loadInfo.mStorageAccess = StorageAccess::eAllow;
      loadInfo.mUseRegularPrincipal = true;
      loadInfo.mUsingStorageAccess = false;
      loadInfo.mCookieJarSettings =
          mozilla::net::CookieJarSettings::Create(loadInfo.mLoadingPrincipal);
      loadInfo.mShouldResistFingerprinting =
          nsContentUtils::ShouldResistFingerprinting_dangerous(
              loadInfo.mLoadingPrincipal,
              "Unusual situation - we have no document or CookieJarSettings",
              RFPTarget::IsAlwaysEnabledForPrecompute);
      MOZ_ASSERT(loadInfo.mCookieJarSettings);
      auto* cookieJarSettings =
          net::CookieJarSettings::Cast(loadInfo.mCookieJarSettings);
      cookieJarSettings->Serialize(loadInfo.mCookieJarSettingsArgs);

      loadInfo.mOriginAttributes = OriginAttributes();
      loadInfo.mIsThirdPartyContext = false;
      loadInfo.mIsOn3PCBExceptionList = false;
    }

    MOZ_ASSERT(loadInfo.mLoadingPrincipal);
    MOZ_ASSERT(isChrome || !loadInfo.mDomain.IsEmpty());

    if (!loadInfo.mLoadGroup || aLoadGroupBehavior == OverrideLoadGroup) {
      OverrideLoadInfoLoadGroup(loadInfo, loadInfo.mLoadingPrincipal);
    }
    MOZ_ASSERT(NS_LoadGroupMatchesPrincipal(loadInfo.mLoadGroup,
                                            loadInfo.mLoadingPrincipal));

    // Top level workers' main script use the document charset for the script
    // uri encoding.
    nsCOMPtr<nsIURI> url;
    rv = nsContentUtils::NewURIWithDocumentCharset(
        getter_AddRefs(url), aScriptURL, document, loadInfo.mBaseURI);
    NS_ENSURE_SUCCESS(rv, NS_ERROR_DOM_SYNTAX_ERR);

    rv = ChannelFromScriptURLMainThread(
        loadInfo.mLoadingPrincipal, document, loadInfo.mLoadGroup, url,
        aWorkerType, aCredentials, clientInfo, ContentPolicyType(aWorkerKind),
        loadInfo.mCookieJarSettings, loadInfo.mReferrerInfo,
        getter_AddRefs(loadInfo.mChannel));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = NS_GetFinalChannelURI(loadInfo.mChannel,
                               getter_AddRefs(loadInfo.mResolvedScriptURI));
    NS_ENSURE_SUCCESS(rv, rv);

    // We need the correct hasStoragePermission flag for the channel here since
    // we will do a content blocking check later when we set the storage
    // principal for the worker. The channel here won't be opened when we do the
    // check later, so the hasStoragePermission flag is incorrect. To address
    // this, We copy the hasStoragePermission flag from the document if there is
    // a window. The worker is created as the same origin of the window. So, the
    // worker is supposed to have the same storage permission as the window as
    // well as the hasStoragePermission flag.
    nsCOMPtr<nsILoadInfo> channelLoadInfo = loadInfo.mChannel->LoadInfo();
    rv = channelLoadInfo->SetStoragePermission(
        loadInfo.mUsingStorageAccess ? nsILoadInfo::HasStoragePermission
                                     : nsILoadInfo::NoStoragePermission);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = loadInfo.SetPrincipalsAndCSPFromChannel(loadInfo.mChannel);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  MOZ_DIAGNOSTIC_ASSERT(loadInfo.mLoadingPrincipal);
  MOZ_DIAGNOSTIC_ASSERT(loadInfo.PrincipalIsValid());

  *aLoadInfo = std::move(loadInfo);
  return NS_OK;
}

// static
void WorkerPrivate::OverrideLoadInfoLoadGroup(WorkerLoadInfo& aLoadInfo,
                                              nsIPrincipal* aPrincipal) {
  MOZ_ASSERT(!aLoadInfo.mInterfaceRequestor);
  MOZ_ASSERT(aLoadInfo.mLoadingPrincipal == aPrincipal);

  aLoadInfo.mInterfaceRequestor =
      new WorkerLoadInfo::InterfaceRequestor(aPrincipal, aLoadInfo.mLoadGroup);
  aLoadInfo.mInterfaceRequestor->MaybeAddBrowserChild(aLoadInfo.mLoadGroup);

  // NOTE: this defaults the load context to:
  //  - private browsing = false
  //  - content = true
  //  - use remote tabs = false
  nsCOMPtr<nsILoadGroup> loadGroup = do_CreateInstance(NS_LOADGROUP_CONTRACTID);

  nsresult rv =
      loadGroup->SetNotificationCallbacks(aLoadInfo.mInterfaceRequestor);
  MOZ_ALWAYS_SUCCEEDS(rv);

  aLoadInfo.mLoadGroup = std::move(loadGroup);

  MOZ_ASSERT(NS_LoadGroupMatchesPrincipal(aLoadInfo.mLoadGroup, aPrincipal));
}

void WorkerPrivate::RunLoopNeverRan() {
  LOG(WorkerLog(), ("WorkerPrivate::RunLoopNeverRan [%p]", this));
  // RunLoopNeverRan is only called in WorkerThreadPrimaryRunnable::Run() to
  // handle cases
  //   1. Fail to get BackgroundChild for the worker thread or
  //   2. Fail to initialize the worker's JS context
  // However, mPreStartRunnables had already dispatched in
  // WorkerThread::SetWorkerPrivateInWorkerThread() where beforing above jobs
  // start. So we need to clean up these dispatched runnables for the worker
  // thread.

  auto data = mWorkerThreadAccessible.Access();
  RefPtr<WorkerThread> thread;
  {
    MutexAutoLock lock(mMutex);

    if (!mPreStartRunnables.IsEmpty()) {
      for (const RefPtr<WorkerThreadRunnable>& runnable : mPreStartRunnables) {
        runnable->mCleanPreStartDispatching = true;
      }
      mPreStartRunnables.Clear();
    }

    // Switch State to Dead
    mStatus = Dead;
    thread = mThread;
  }

  // Clear the dispatched mPreStartRunnables.
  if (thread && NS_HasPendingEvents(thread)) {
    NS_ProcessPendingEvents(nullptr);
  }

  // After mStatus is set to Dead there can be no more
  // WorkerControlRunnables so no need to lock here.
  if (!mControlQueue.IsEmpty()) {
    WorkerRunnable* runnable = nullptr;
    while (mControlQueue.Pop(runnable)) {
      runnable->Release();
    }
  }

  // There should be no StrongWorkerRefs, child Workers, and Timeouts, but
  // WeakWorkerRefs could. WorkerThreadPrimaryRunnable could have created a
  // PerformanceStorageWorker which holds a WeakWorkerRef.
  // Notify WeakWorkerRefs with Dead status.
  NotifyWorkerRefs(Dead);
}

void WorkerPrivate::UnrootGlobalScopes() {
  LOG(WorkerLog(), ("WorkerPrivate::UnrootGlobalScopes [%p]", this));
  auto data = mWorkerThreadAccessible.Access();

  RefPtr<WorkerDebuggerGlobalScope> debugScope = data->mDebuggerScope.forget();
  if (debugScope) {
    MOZ_ASSERT(debugScope->mWorkerPrivate == this);
  }
  RefPtr<WorkerGlobalScope> scope = data->mScope.forget();
  if (scope) {
    MOZ_ASSERT(scope->mWorkerPrivate == this);
  }
}

void WorkerPrivate::DoRunLoop(JSContext* aCx) {
  LOG(WorkerLog(), ("WorkerPrivate::DoRunLoop [%p]", this));
  auto data = mWorkerThreadAccessible.Access();
  MOZ_RELEASE_ASSERT(!GetExecutionManager());

  RefPtr<WorkerThread> thread;
  {
    MutexAutoLock lock(mMutex);
    mJSContext = aCx;
    // mThread is set before we enter, and is never changed during DoRunLoop.
    // copy to local so we don't trigger mutex analysis
    MOZ_ASSERT(mThread);
    thread = mThread;

    MOZ_ASSERT(mStatus == Pending);
    mStatus = Running;

    // Now, start to run the event loop, mPreStartRunnables can be cleared,
    // since when get here, Worker initialization has done successfully.
    mPreStartRunnables.Clear();
  }

  // Create IPC between the content process worker thread and the parent
  // process background thread for non-life cycle related operations of
  // SharedWorker/ServiceWorker
  if (mChildEp.IsValid()) {
    mRemoteWorkerNonLifeCycleOpController =
        RemoteWorkerNonLifeCycleOpControllerChild::Create();
    MOZ_ASSERT_DEBUG_OR_FUZZING(mRemoteWorkerNonLifeCycleOpController);
    mChildEp.Bind(mRemoteWorkerNonLifeCycleOpController);
  }

  // Now that we've done that, we can go ahead and set up our AutoJSAPI.  We
  // can't before this point, because it can't find the right JSContext before
  // then, since it gets it from our mJSContext.
  AutoJSAPI jsapi;
  jsapi.Init();
  MOZ_ASSERT(jsapi.cx() == aCx);

  EnableMemoryReporter();

  InitializeGCTimers();

  bool checkFinalGCCC =
      StaticPrefs::dom_workers_GCCC_on_potentially_last_event();

  bool debuggerRunnablesPending = false;
  bool normalRunnablesPending = false;
  auto noRunnablesPendingAndKeepAlive =
      [&debuggerRunnablesPending, &normalRunnablesPending, &thread, this]()
          MOZ_REQUIRES(mMutex) {
            // We want to keep both pending flags always updated while looping.
            debuggerRunnablesPending = !mDebuggerQueue.IsEmpty();
            normalRunnablesPending = NS_HasPendingEvents(thread);

            bool anyRunnablesPending = !mControlQueue.IsEmpty() ||
                                       debuggerRunnablesPending ||
                                       normalRunnablesPending;
            bool keepWorkerAlive = mStatus == Running || HasActiveWorkerRefs();

            return (!anyRunnablesPending && keepWorkerAlive);
          };

  for (;;) {
    WorkerStatus currentStatus;

    if (checkFinalGCCC) {
      // If we get here after the last event ran but someone holds a WorkerRef
      // and there is no other logic to release that WorkerRef than lazily
      // through GC/CC, we might block forever on the next WaitForWorkerEvents.
      // Every object holding a WorkerRef should really have a straight,
      // deterministic line from the WorkerRef's callback being invoked to the
      // WorkerRef being released which is supported by strong-references that
      // can't form a cycle.
      bool mayNeedFinalGCCC = false;
      {
        MutexAutoLock lock(mMutex);

        currentStatus = mStatus;
        mayNeedFinalGCCC =
            (mStatus >= Canceling && HasActiveWorkerRefs() &&
             !debuggerRunnablesPending && !normalRunnablesPending &&
             data->mPerformedShutdownAfterLastContentTaskExecuted);
      }
      if (mayNeedFinalGCCC) {
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
        // WorkerRef::ReleaseWorker will check this flag via
        // AssertIsNotPotentiallyLastGCCCRunning
        data->mIsPotentiallyLastGCCCRunning = true;
#endif
        // GarbageCollectInternal will trigger both GC and CC
        GarbageCollectInternal(aCx, true /* aShrinking */,
                               true /* aCollectChildren */);
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
        data->mIsPotentiallyLastGCCCRunning = false;
#endif
      }
    }

    {
      MutexAutoLock lock(mMutex);
      if (checkFinalGCCC && currentStatus != mStatus) {
        // Something moved our status while we were supposed to check for a
        // potentially needed GC/CC. Just check again.
        continue;
      }

      // Wait for a runnable to arrive that we can execute, or for it to be okay
      // to shutdown this worker once all holders have been removed.
      // Holders may be removed from inside normal runnables,  but we don't
      // check for that after processing normal runnables, so we need to let
      // control flow to the shutdown logic without blocking.
      while (noRunnablesPendingAndKeepAlive()) {
        // We pop out to this loop when there are no pending events.
        // If we don't reset these, we may not re-enter ProcessNextEvent()
        // until we have events to process, and it may seem like we have
        // an event running for a very long time.
        thread->SetRunningEventDelay(TimeDuration(), TimeStamp());

        mWorkerLoopIsIdle = true;

        WaitForWorkerEvents();

        mWorkerLoopIsIdle = false;
      }

      auto result = ProcessAllControlRunnablesLocked();
      if (result != ProcessAllControlRunnablesResult::Nothing) {
        // Update all saved runnable flags for side effect for the
        // loop check about transitioning to Killing below.
        (void)noRunnablesPendingAndKeepAlive();
      }

      currentStatus = mStatus;
    }

    // Status transitions to Closing/Canceling and there are no SyncLoops,
    // set global start dying, disconnect EventTargetObjects and
    // WebTaskScheduler.
    // The Worker might switch to the "Killing" immediately then directly exits
    // DoRunLoop(). Before exiting the DoRunLoop(), explicitly disconnecting the
    // WorkerGlobalScope's EventTargetObject here would help to fail runnable
    // dispatching when the Worker is in the status changing.
    if (currentStatus >= Closing &&
        !data->mPerformedShutdownAfterLastContentTaskExecuted) {
      data->mPerformedShutdownAfterLastContentTaskExecuted.Flip();
      if (data->mScope) {
        data->mScope->NoteTerminating();
        data->mScope->DisconnectGlobalTeardownObservers();
        if (WebTaskScheduler* scheduler =
                data->mScope->GetExistingScheduler()) {
          scheduler->Disconnect();
        }
      }
    }

    // Transition from Canceling to Killing and exit this loop when:
    //  * All (non-weak) WorkerRefs have been released.
    //  * There are no runnables pending. This is intended to let same-thread
    //    dispatches as part of cleanup be able to run to completion, but any
    //    logic that still wants async things to happen should be holding a
    //    StrongWorkerRef.
    if (currentStatus != Running && !HasActiveWorkerRefs() &&
        !normalRunnablesPending && !debuggerRunnablesPending) {
      // Now we are ready to kill the worker thread.
      if (currentStatus == Canceling) {
        NotifyInternal(Killing);

#ifdef DEBUG
        {
          MutexAutoLock lock(mMutex);
          currentStatus = mStatus;
        }
        MOZ_ASSERT(currentStatus == Killing);
#else
        currentStatus = Killing;
#endif
      }

      // If we're supposed to die then we should exit the loop.
      if (currentStatus == Killing) {
        // We are about to destroy worker, report all use counters.
        ReportUseCounters();

        // Flush uncaught rejections immediately, without
        // waiting for a next tick.
        PromiseDebugging::FlushUncaughtRejections();

        // DisableRemoteDebuggerOnWorkerThread(true /*aForShutdown*/);

        ShutdownGCTimers();

        DisableMemoryReporter();

        // Move the timer out with the mutex held but only drop the ref
        // when the mutex is not held.
        nsCOMPtr<nsITimer> timer;
        {
          MutexAutoLock lock(mMutex);

          mStatus = Dead;
          mJSContext = nullptr;
          mDebuggerInterruptTimer.swap(timer);
        }
        timer = nullptr;

        // After mStatus is set to Dead there can be no more
        // WorkerControlRunnables so no need to lock here.
        if (!mControlQueue.IsEmpty()) {
          LOG(WorkerLog(),
              ("WorkerPrivate::DoRunLoop [%p] dropping control runnables in "
               "Dead status",
               this));
          WorkerRunnable* runnable = nullptr;
          while (mControlQueue.Pop(runnable)) {
            runnable->Cancel();
            runnable->Release();
          }
        }

        // We do not need the timeouts any more, they have been canceled
        // by NotifyInternal(Killing) above if they were active.
        UnlinkTimeouts();

        return;
      }
    }

    if (debuggerRunnablesPending || normalRunnablesPending) {
      // Start the periodic GC timer if it is not already running.
      SetGCTimerMode(PeriodicTimer);
    }

    if (debuggerRunnablesPending) {
      ProcessSingleDebuggerRunnable();

      {
        MutexAutoLock lock(mMutex);
        debuggerRunnablesPending = !mDebuggerQueue.IsEmpty();
      }

      if (debuggerRunnablesPending) {
        WorkerDebuggerGlobalScope* globalScope = DebuggerGlobalScope();
        // If the worker was canceled before ever creating its content global
        // then mCancelBeforeWorkerScopeConstructed could have been flipped and
        // all of the WorkerDebuggerRunnables canceled, so the debugger global
        // would never have been created.
        if (globalScope) {
          // Now *might* be a good time to GC. Let the JS engine make the
          // decision.
          JSAutoRealm ar(aCx, globalScope->GetGlobalJSObject());
          JS_MaybeGC(aCx);
        }
      }
    } else if (normalRunnablesPending) {
      // Process a single runnable from the main queue.
      NS_ProcessNextEvent(thread, false);

      normalRunnablesPending = NS_HasPendingEvents(thread);
      if (normalRunnablesPending && GlobalScope()) {
        // Now *might* be a good time to GC. Let the JS engine make the
        // decision.
        JSAutoRealm ar(aCx, GlobalScope()->GetGlobalJSObject());
        JS_MaybeGC(aCx);
      }
    }

    // Checking the background actors if needed, any runnable execution could
    // release background actors which blocks GC/CC on
    // WorkerPrivate::mParentEventTargetRef.
    if (currentStatus < Canceling) {
      UpdateCCFlag(CCFlag::CheckBackgroundActors);
    }

    if (!debuggerRunnablesPending && !normalRunnablesPending) {
      // Both the debugger event queue and the normal event queue has been
      // exhausted, cancel the periodic GC timer and schedule the idle GC timer.
      SetGCTimerMode(IdleTimer);
    }

    // If the worker thread is spamming the main thread faster than it can
    // process the work, then pause the worker thread until the main thread
    // catches up.
    size_t queuedEvents = mMainThreadEventTargetForMessaging->Length() +
                          mMainThreadDebuggeeEventTarget->Length();
    if (queuedEvents > 5000) {
      // Note, postMessage uses mMainThreadDebuggeeEventTarget!
      mMainThreadDebuggeeEventTarget->AwaitIdle();
    }
  }

  MOZ_CRASH("Shouldn't get here!");
}

namespace {
/**
 * If there is a current CycleCollectedJSContext, return its recursion depth,
 * otherwise return 1.
 *
 * In the edge case where a worker is starting up so late that PBackground is
 * already shutting down, the cycle collected context will never be created,
 * but we will need to drain the event loop in ClearMainEventQueue.  This will
 * result in a normal NS_ProcessPendingEvents invocation which will call
 * WorkerPrivate::OnProcessNextEvent and WorkerPrivate::AfterProcessNextEvent
 * which want to handle the need to process control runnables and perform a
 * sanity check assertion, respectively.
 *
 * We claim a depth of 1 when there's no CCJS because this most corresponds to
 * reality, but this doesn't meant that other code might want to drain various
 * runnable queues as part of this cleanup.
 */
uint32_t GetEffectiveEventLoopRecursionDepth() {
  auto* ccjs = CycleCollectedJSContext::Get();
  if (ccjs) {
    return ccjs->RecursionDepth();
  }

  return 1;
}

}  // namespace

void WorkerPrivate::OnProcessNextEvent() {
  AssertIsOnWorkerThread();

  uint32_t recursionDepth = GetEffectiveEventLoopRecursionDepth();
  MOZ_ASSERT(recursionDepth);

  // Normally we process control runnables in DoRunLoop or RunCurrentSyncLoop.
  // However, it's possible that non-worker C++ could spin its own nested event
  // loop, and in that case we must ensure that we continue to process control
  // runnables here.
  if (recursionDepth > 1 && mSyncLoopStack.Length() < recursionDepth - 1) {
    Unused << ProcessAllControlRunnables();
    // There's no running JS, and no state to revalidate, so we can ignore the
    // return value.
  }
}

void WorkerPrivate::AfterProcessNextEvent() {
  AssertIsOnWorkerThread();
  MOZ_ASSERT(GetEffectiveEventLoopRecursionDepth());
}

nsISerialEventTarget* WorkerPrivate::MainThreadEventTargetForMessaging() {
  return mMainThreadEventTargetForMessaging;
}

nsresult WorkerPrivate::DispatchToMainThreadForMessaging(nsIRunnable* aRunnable,
                                                         uint32_t aFlags) {
  nsCOMPtr<nsIRunnable> r = aRunnable;
  return DispatchToMainThreadForMessaging(r.forget(), aFlags);
}

nsresult WorkerPrivate::DispatchToMainThreadForMessaging(
    already_AddRefed<nsIRunnable> aRunnable, uint32_t aFlags) {
  return mMainThreadEventTargetForMessaging->Dispatch(std::move(aRunnable),
                                                      aFlags);
}

nsISerialEventTarget* WorkerPrivate::MainThreadEventTarget() {
  return mMainThreadEventTarget;
}

nsresult WorkerPrivate::DispatchToMainThread(nsIRunnable* aRunnable,
                                             uint32_t aFlags) {
  nsCOMPtr<nsIRunnable> r = aRunnable;
  return DispatchToMainThread(r.forget(), aFlags);
}

nsresult WorkerPrivate::DispatchToMainThread(
    already_AddRefed<nsIRunnable> aRunnable, uint32_t aFlags) {
  return mMainThreadEventTarget->Dispatch(std::move(aRunnable), aFlags);
}

nsresult WorkerPrivate::DispatchDebuggeeToMainThread(
    already_AddRefed<WorkerRunnable> aRunnable, uint32_t aFlags) {
  RefPtr<WorkerRunnable> debuggeeRunnable = std::move(aRunnable);
  MOZ_ASSERT_DEBUG_OR_FUZZING(debuggeeRunnable->IsDebuggeeRunnable());
  return mMainThreadDebuggeeEventTarget->Dispatch(debuggeeRunnable.forget(),
                                                  aFlags);
}

nsISerialEventTarget* WorkerPrivate::ControlEventTarget() {
  return mWorkerControlEventTarget;
}

nsISerialEventTarget* WorkerPrivate::HybridEventTarget() {
  return mWorkerHybridEventTarget;
}

ClientType WorkerPrivate::GetClientType() const {
  switch (Kind()) {
    case WorkerKindDedicated:
      return ClientType::Worker;
    case WorkerKindShared:
      return ClientType::Sharedworker;
    case WorkerKindService:
      return ClientType::Serviceworker;
    default:
      MOZ_CRASH("unknown worker type!");
  }
}

UniquePtr<ClientSource> WorkerPrivate::CreateClientSource() {
  auto data = mWorkerThreadAccessible.Access();
  MOZ_ASSERT(!data->mScope, "Client should be created before the global");

  UniquePtr<ClientSource> clientSource;
  if (IsServiceWorker()) {
    clientSource = ClientManager::CreateSourceFromInfo(
        GetSourceInfo(), mWorkerHybridEventTarget);
  } else {
    clientSource = ClientManager::CreateSource(
        GetClientType(), mWorkerHybridEventTarget,
        StoragePrincipalHelper::ShouldUsePartitionPrincipalForServiceWorker(
            this)
            ? GetPartitionedPrincipalInfo()
            : GetPrincipalInfo());
  }
  MOZ_DIAGNOSTIC_ASSERT(clientSource);

  clientSource->SetAgentClusterId(mAgentClusterId);

  if (data->mFrozen) {
    clientSource->Freeze();
  }

  // Shortly after the client is reserved we will try loading the main script
  // for the worker.  This may get intercepted by the ServiceWorkerManager
  // which will then try to create a ClientHandle.  Its actually possible for
  // the main thread to create this ClientHandle before our IPC message creating
  // the ClientSource completes.  To avoid this race we synchronously ping our
  // parent Client actor here.  This ensure the worker ClientSource is created
  // in the parent before the main thread might try reaching it with a
  // ClientHandle.
  //
  // An alternative solution would have been to handle the out-of-order
  // operations on the parent side.  We could have created a small window where
  // we allow ClientHandle objects to exist without a ClientSource.  We would
  // then time out these handles if they stayed orphaned for too long.  This
  // approach would be much more complex, but also avoid this extra bit of
  // latency when starting workers.
  //
  // Note, we only have to do this for workers that can be controlled by a
  // service worker.  So avoid the sync overhead here if we are starting a
  // service worker or a chrome worker.
  if (Kind() != WorkerKindService && !IsChromeWorker()) {
    clientSource->WorkerSyncPing(this);
  }

  return clientSource;
}

bool WorkerPrivate::EnsureCSPEventListener() {
  if (!mCSPEventListener) {
    mCSPEventListener = WorkerCSPEventListener::Create(this);
    if (NS_WARN_IF(!mCSPEventListener)) {
      return false;
    }
  }
  return true;
}

nsICSPEventListener* WorkerPrivate::CSPEventListener() const {
  MOZ_ASSERT(mCSPEventListener);
  return mCSPEventListener;
}

void WorkerPrivate::EnsurePerformanceStorage() {
  AssertIsOnWorkerThread();

  if (!mPerformanceStorage) {
    mPerformanceStorage = PerformanceStorageWorker::Create(this);
  }
}

bool WorkerPrivate::GetExecutionGranted() const {
  auto data = mWorkerThreadAccessible.Access();
  return data->mJSThreadExecutionGranted;
}

void WorkerPrivate::SetExecutionGranted(bool aGranted) {
  auto data = mWorkerThreadAccessible.Access();
  data->mJSThreadExecutionGranted = aGranted;
}

void WorkerPrivate::ScheduleTimeSliceExpiration(uint32_t aDelay) {
  auto data = mWorkerThreadAccessible.Access();

  if (!data->mTSTimer) {
    data->mTSTimer = NS_NewTimer();
    MOZ_ALWAYS_SUCCEEDS(data->mTSTimer->SetTarget(mWorkerControlEventTarget));
  }

  // Whenever an event is scheduled on the WorkerControlEventTarget an
  // interrupt is automatically requested which causes us to yield JS execution
  // and the next JS execution in the queue to execute.
  // This allows for simple code reuse of the existing interrupt callback code
  // used for control events.
  MOZ_ALWAYS_SUCCEEDS(data->mTSTimer->InitWithNamedFuncCallback(
      [](nsITimer* Timer, void* aClosure) { return; }, nullptr, aDelay,
      nsITimer::TYPE_ONE_SHOT, "TimeSliceExpirationTimer"));
}

void WorkerPrivate::CancelTimeSliceExpiration() {
  auto data = mWorkerThreadAccessible.Access();
  MOZ_ALWAYS_SUCCEEDS(data->mTSTimer->Cancel());
}

JSExecutionManager* WorkerPrivate::GetExecutionManager() const {
  auto data = mWorkerThreadAccessible.Access();
  return data->mExecutionManager.get();
}

void WorkerPrivate::SetExecutionManager(JSExecutionManager* aManager) {
  auto data = mWorkerThreadAccessible.Access();
  data->mExecutionManager = aManager;
}

void WorkerPrivate::ExecutionReady() {
  auto data = mWorkerThreadAccessible.Access();
  {
    MutexAutoLock lock(mMutex);
    if (mStatus >= Canceling) {
      return;
    }
  }

  data->mScope->MutableClientSourceRef().WorkerExecutionReady(this);

  if (ExtensionAPIAllowed()) {
    extensions::CreateAndDispatchInitWorkerContextRunnable();
  }
}

void WorkerPrivate::InitializeGCTimers() {
  auto data = mWorkerThreadAccessible.Access();

  // We need timers for GC. The basic plan is to run a non-shrinking GC
  // periodically (PERIODIC_GC_TIMER_DELAY_SEC) while the worker is running.
  // Once the worker goes idle we set a short (IDLE_GC_TIMER_DELAY_SEC) timer to
  // run a shrinking GC.
  data->mPeriodicGCTimer = NS_NewTimer();
  data->mIdleGCTimer = NS_NewTimer();

  data->mPeriodicGCTimerRunning = false;
  data->mIdleGCTimerRunning = false;
}

void WorkerPrivate::SetGCTimerMode(GCTimerMode aMode) {
  auto data = mWorkerThreadAccessible.Access();

  if (!data->mPeriodicGCTimer || !data->mIdleGCTimer) {
    // GC timers have been cleared already.
    return;
  }

  if (aMode == NoTimer) {
    MOZ_ALWAYS_SUCCEEDS(data->mPeriodicGCTimer->Cancel());
    data->mPeriodicGCTimerRunning = false;
    MOZ_ALWAYS_SUCCEEDS(data->mIdleGCTimer->Cancel());
    data->mIdleGCTimerRunning = false;
    return;
  }

  WorkerStatus status;
  {
    MutexAutoLock lock(mMutex);
    status = mStatus;
  }

  if (status >= Killing) {
    ShutdownGCTimers();
    return;
  }

  // If the idle timer is running, don't cancel it when the periodic timer
  // is scheduled since we do want shrinking GC to be called occasionally.
  if (aMode == PeriodicTimer && data->mPeriodicGCTimerRunning) {
    return;
  }

  if (aMode == IdleTimer) {
    if (!data->mPeriodicGCTimerRunning) {
      // Since running idle GC cancels both GC timers, after that we want
      // first at least periodic GC timer getting activated, since that tells
      // us that there have been some non-control tasks to process. Otherwise
      // idle GC timer would keep running all the time.
      return;
    }

    // Cancel the periodic timer now, since the event loop is (in the common
    // case) empty now.
    MOZ_ALWAYS_SUCCEEDS(data->mPeriodicGCTimer->Cancel());
    data->mPeriodicGCTimerRunning = false;

    if (data->mIdleGCTimerRunning) {
      return;
    }
  }

  MOZ_ASSERT(aMode == PeriodicTimer || aMode == IdleTimer);

  uint32_t delay = 0;
  int16_t type = nsITimer::TYPE_ONE_SHOT;
  nsTimerCallbackFunc callback = nullptr;
  const char* name = nullptr;
  nsITimer* timer = nullptr;

  if (aMode == PeriodicTimer) {
    delay = PERIODIC_GC_TIMER_DELAY_SEC * 1000;
    type = nsITimer::TYPE_REPEATING_SLACK;
    callback = PeriodicGCTimerCallback;
    name = "dom::PeriodicGCTimerCallback";
    timer = data->mPeriodicGCTimer;
    data->mPeriodicGCTimerRunning = true;
    LOG(WorkerLog(), ("Worker %p scheduled periodic GC timer\n", this));
  } else {
    delay = IDLE_GC_TIMER_DELAY_SEC * 1000;
    type = nsITimer::TYPE_ONE_SHOT;
    callback = IdleGCTimerCallback;
    name = "dom::IdleGCTimerCallback";
    timer = data->mIdleGCTimer;
    data->mIdleGCTimerRunning = true;
    LOG(WorkerLog(), ("Worker %p scheduled idle GC timer\n", this));
  }

  MOZ_ALWAYS_SUCCEEDS(timer->SetTarget(mWorkerControlEventTarget));
  MOZ_ALWAYS_SUCCEEDS(
      timer->InitWithNamedFuncCallback(callback, this, delay, type, name));
}

void WorkerPrivate::ShutdownGCTimers() {
  auto data = mWorkerThreadAccessible.Access();

  MOZ_ASSERT(!data->mPeriodicGCTimer == !data->mIdleGCTimer);

  if (!data->mPeriodicGCTimer && !data->mIdleGCTimer) {
    return;
  }

  // Always make sure the timers are canceled.
  MOZ_ALWAYS_SUCCEEDS(data->mPeriodicGCTimer->Cancel());
  MOZ_ALWAYS_SUCCEEDS(data->mIdleGCTimer->Cancel());

  LOG(WorkerLog(), ("Worker %p killed the GC timers\n", this));

  data->mPeriodicGCTimer = nullptr;
  data->mIdleGCTimer = nullptr;
  data->mPeriodicGCTimerRunning = false;
  data->mIdleGCTimerRunning = false;
}

bool WorkerPrivate::InterruptCallback(JSContext* aCx) {
  auto data = mWorkerThreadAccessible.Access();

  AutoYieldJSThreadExecution yield;

  // If we are here it's because a WorkerControlRunnable has been dispatched.
  // The runnable could be processed here or it could have already been
  // processed by a sync event loop.
  // The most important thing this method must do, is to decide if the JS
  // execution should continue or not. If the runnable returns an error or if
  // the worker status is >= Canceling, we should stop the JS execution.

  MOZ_ASSERT(!JS_IsExceptionPending(aCx));

  bool mayContinue = true;
  bool scheduledIdleGC = false;

  for (;;) {
    // Run all control events now.
    auto result = ProcessAllControlRunnables();
    if (result == ProcessAllControlRunnablesResult::Abort) {
      mayContinue = false;
    }

    bool mayFreeze = data->mFrozen;

    {
      MutexAutoLock lock(mMutex);

      if (mayFreeze) {
        mayFreeze = mStatus <= Running;
      }

      if (mStatus >= Canceling) {
        mayContinue = false;
      }
    }

    if (!mayContinue || !mayFreeze) {
      break;
    }

    // Cancel the periodic GC timer here before freezing. The idle GC timer
    // will clean everything up once it runs.
    if (!scheduledIdleGC) {
      SetGCTimerMode(IdleTimer);
      scheduledIdleGC = true;
    }

    while ((mayContinue = MayContinueRunning())) {
      MutexAutoLock lock(mMutex);
      if (!mControlQueue.IsEmpty()) {
        break;
      }

      WaitForWorkerEvents();
    }
  }

  if (!mayContinue) {
    // We want only uncatchable exceptions here.
    NS_ASSERTION(!JS_IsExceptionPending(aCx),
                 "Should not have an exception set here!");
    return false;
  }

  // Make sure the periodic timer gets turned back on here.
  SetGCTimerMode(PeriodicTimer);

  if (data->mDebuggerInterruptRequested) {
    bool debuggerRunnablesPending = false;
    {
      MutexAutoLock lock(mMutex);
      debuggerRunnablesPending = !mDebuggerQueue.IsEmpty();
    }
    if (debuggerRunnablesPending) {
      // Prevents interrupting the debugger's own logic unless it has called
      // back into content
      WorkerGlobalScope* globalScope = GlobalScope();
      if (globalScope) {
        JSObject* global = JS::CurrentGlobalOrNull(aCx);
        if (global && global == globalScope->GetGlobalJSObject()) {
          while (debuggerRunnablesPending) {
            ProcessSingleDebuggerRunnable();
            {
              MutexAutoLock lock(mMutex);
              debuggerRunnablesPending = !mDebuggerQueue.IsEmpty();
            }
          }
        }
      }
    }
    data->mDebuggerInterruptRequested = false;
  }

  return true;
}

void WorkerPrivate::CloseInternal() {
  AssertIsOnWorkerThread();
  NotifyInternal(Closing);
}

bool WorkerPrivate::IsOnCurrentThread() {
  // May be called on any thread!

  MOZ_ASSERT(mPRThread);
  return PR_GetCurrentThread() == mPRThread;
}

void WorkerPrivate::ScheduleDeletion(WorkerRanOrNot aRanOrNot) {
  AssertIsOnWorkerThread();
  {
    // mWorkerThreadAccessible's accessor must be destructed before
    // the scheduled Runnable gets to run.
    auto data = mWorkerThreadAccessible.Access();
    MOZ_ASSERT(data->mChildWorkers.IsEmpty());

    MOZ_RELEASE_ASSERT(!data->mDeletionScheduled);
    data->mDeletionScheduled.Flip();
  }
  MOZ_ASSERT(mSyncLoopStack.IsEmpty());
  MOZ_ASSERT(mPostSyncLoopOperations == 0);

  // If Worker is never ran, clear the mPreStartRunnables. To let the resource
  // hold by the pre-submmited runnables.
  if (WorkerNeverRan == aRanOrNot) {
    ClearPreStartRunnables();
  }

#ifdef DEBUG
  if (WorkerRan == aRanOrNot) {
    nsIThread* currentThread = NS_GetCurrentThread();
    MOZ_ASSERT(currentThread);
    // On the worker thread WorkerRunnable will refuse to run if not nested
    // on top of a WorkerThreadPrimaryRunnable.
    Unused << NS_WARN_IF(NS_HasPendingEvents(currentThread));
  }
#endif

  // Force to set mRemoteDebuggerRegistered as false and notify if the Worker is
  // waiting for the registration done.
  SetIsRemoteDebuggerRegistered(false);

  if (WorkerPrivate* parent = GetParent()) {
    RefPtr<WorkerFinishedRunnable> runnable =
        new WorkerFinishedRunnable(parent, this);
    if (!runnable->Dispatch(parent)) {
      NS_WARNING("Failed to dispatch runnable!");
    }
  } else {
    if (ExtensionAPIAllowed()) {
      MOZ_ASSERT(IsServiceWorker());
      RefPtr<Runnable> extWorkerRunnable =
          extensions::CreateWorkerDestroyedRunnable(ServiceWorkerID(),
                                                    GetBaseURI());
      // Dispatch as a low priority runnable.
      if (NS_FAILED(
              DispatchToMainThreadForMessaging(extWorkerRunnable.forget()))) {
        NS_WARNING(
            "Failed to dispatch runnable to notify extensions worker "
            "destroyed");
      }
    }

    // Note, this uses the lower priority DispatchToMainThreadForMessaging for
    // dispatching TopLevelWorkerFinishedRunnable to the main thread so that
    // other relevant runnables are guaranteed to run before it.
    RefPtr<TopLevelWorkerFinishedRunnable> runnable =
        new TopLevelWorkerFinishedRunnable(this);
    if (NS_FAILED(DispatchToMainThreadForMessaging(runnable.forget()))) {
      NS_WARNING("Failed to dispatch runnable!");
    }

    // NOTE: Calling any WorkerPrivate methods (or accessing member data) after
    // this point is unsafe (the TopLevelWorkerFinishedRunnable just dispatched
    // may be able to call ClearSelfAndParentEventTargetRef on this
    // WorkerPrivate instance and by the time we get here the WorkerPrivate
    // instance destructor may have been already called).
  }
}

bool WorkerPrivate::CollectRuntimeStats(
    JS::RuntimeStats* aRtStats, bool aAnonymize) MOZ_NO_THREAD_SAFETY_ANALYSIS {
  // We don't have a lock to access mJSContext, but it's safe to access on this
  // thread.
  AssertIsOnWorkerThread();
  NS_ASSERTION(aRtStats, "Null RuntimeStats!");
  // We don't really own it, but it's safe to access on this thread
  NS_ASSERTION(mJSContext, "This must never be null!");

  return JS::CollectRuntimeStats(mJSContext, aRtStats, nullptr, aAnonymize);
}

void WorkerPrivate::EnableMemoryReporter() {
  auto data = mWorkerThreadAccessible.Access();
  MOZ_ASSERT(!data->mMemoryReporter);

  // No need to lock here since the main thread can't race until we've
  // successfully registered the reporter.
  data->mMemoryReporter = new MemoryReporter(this);

  if (NS_FAILED(RegisterWeakAsyncMemoryReporter(data->mMemoryReporter))) {
    NS_WARNING("Failed to register memory reporter!");
    // No need to lock here since a failed registration means our memory
    // reporter can't start running. Just clean up.
    data->mMemoryReporter = nullptr;
  }
}

void WorkerPrivate::DisableMemoryReporter() {
  auto data = mWorkerThreadAccessible.Access();

  RefPtr<MemoryReporter> memoryReporter;
  {
    // Mutex protectes MemoryReporter::mWorkerPrivate which is cleared by
    // MemoryReporter::Disable() below.
    MutexAutoLock lock(mMutex);

    // There is nothing to do here if the memory reporter was never successfully
    // registered.
    if (!data->mMemoryReporter) {
      return;
    }

    // We don't need this set any longer. Swap it out so that we can unregister
    // below.
    data->mMemoryReporter.swap(memoryReporter);

    // Next disable the memory reporter so that the main thread stops trying to
    // signal us.
    memoryReporter->Disable();
  }

  // Finally unregister the memory reporter.
  if (NS_FAILED(UnregisterWeakMemoryReporter(memoryReporter))) {
    NS_WARNING("Failed to unregister memory reporter!");
  }
}

void WorkerPrivate::WaitForWorkerEvents() {
  AUTO_PROFILER_LABEL("WorkerPrivate::WaitForWorkerEvents", IDLE);

  AssertIsOnWorkerThread();
  mMutex.AssertCurrentThreadOwns();

  // Wait for a worker event.
  mCondVar.Wait();
}

WorkerPrivate::ProcessAllControlRunnablesResult
WorkerPrivate::ProcessAllControlRunnablesLocked() {
  AssertIsOnWorkerThread();
  mMutex.AssertCurrentThreadOwns();

  AutoYieldJSThreadExecution yield;

  auto result = ProcessAllControlRunnablesResult::Nothing;

  for (;;) {
    WorkerRunnable* event;
    if (!mControlQueue.Pop(event)) {
      break;
    }

    MutexAutoUnlock unlock(mMutex);

    {
      MOZ_ASSERT(event);
      AUTO_PROFILE_FOLLOWING_RUNNABLE(event);
      if (NS_FAILED(static_cast<nsIRunnable*>(event)->Run())) {
        result = ProcessAllControlRunnablesResult::Abort;
      }
    }

    if (result == ProcessAllControlRunnablesResult::Nothing) {
      // We ran at least one thing.
      result = ProcessAllControlRunnablesResult::MayContinue;
    }
    event->Release();
  }

  return result;
}

void WorkerPrivate::ShutdownModuleLoader() {
  AssertIsOnWorkerThread();

  WorkerGlobalScope* globalScope = GlobalScope();
  if (globalScope) {
    if (globalScope->GetModuleLoader(nullptr)) {
      globalScope->GetModuleLoader(nullptr)->Shutdown();
    }
  }
  WorkerDebuggerGlobalScope* debugGlobalScope = DebuggerGlobalScope();
  if (debugGlobalScope) {
    if (debugGlobalScope->GetModuleLoader(nullptr)) {
      debugGlobalScope->GetModuleLoader(nullptr)->Shutdown();
    }
  }
}

void WorkerPrivate::ClearPreStartRunnables() {
  nsTArray<RefPtr<WorkerThreadRunnable>> prestart;
  {
    MutexAutoLock lock(mMutex);
    mPreStartRunnables.SwapElements(prestart);
  }
  for (uint32_t count = prestart.Length(), index = 0; index < count; index++) {
    LOG(WorkerLog(), ("WorkerPrivate::ClearPreStartRunnable [%p]", this));
    RefPtr<WorkerRunnable> runnable = std::move(prestart[index]);
    runnable->Cancel();
  }
}

void WorkerPrivate::ProcessSingleDebuggerRunnable() {
  WorkerRunnable* runnable = nullptr;

  // Move the timer out with the mutex held but only drop the ref
  // when the mutex is not held.
  nsCOMPtr<nsITimer> timer;
  {
    MutexAutoLock lock(mMutex);

    mDebuggerQueue.Pop(runnable);

    mDebuggerInterruptTimer.swap(timer);
  }
  timer = nullptr;

  {
    MOZ_ASSERT(runnable);
    AUTO_PROFILE_FOLLOWING_RUNNABLE(runnable);
    static_cast<nsIRunnable*>(runnable)->Run();
  }
  runnable->Release();

  CycleCollectedJSContext* ccjs = CycleCollectedJSContext::Get();
  ccjs->PerformDebuggerMicroTaskCheckpoint();
}

void WorkerPrivate::ClearDebuggerEventQueue() {
  bool debuggerRunnablesPending = false;
  {
    MutexAutoLock lock(mMutex);
    debuggerRunnablesPending = !mDebuggerQueue.IsEmpty();
  }
  while (debuggerRunnablesPending) {
    WorkerRunnable* runnable = nullptr;
    {
      MutexAutoLock lock(mMutex);
      mDebuggerQueue.Pop(runnable);
      debuggerRunnablesPending = !mDebuggerQueue.IsEmpty();
    }
    // It should be ok to simply release the runnable, without running it.
    runnable->Release();

    // Move the timer out with the mutex held but only drop the ref
    // when the mutex is not held.
    nsCOMPtr<nsITimer> timer;
    {
      MutexAutoLock lock(mMutex);
      mDebuggerInterruptTimer.swap(timer);
    }
    timer = nullptr;
  }
}

bool WorkerPrivate::FreezeInternal() {
  auto data = mWorkerThreadAccessible.Access();
  NS_ASSERTION(!data->mFrozen, "Already frozen!");

  AutoYieldJSThreadExecution yield;

  // The worker can freeze even if it failed to run (and doesn't have a global).
  if (data->mScope) {
    data->mScope->MutableClientSourceRef().Freeze();
  }

  data->mFrozen = true;

  for (uint32_t index = 0; index < data->mChildWorkers.Length(); index++) {
    data->mChildWorkers[index]->Freeze(nullptr);
  }

  if (StaticPrefs::dom_workers_timeoutmanager_AtStartup()) {
    auto* timeoutManager =
        data->mScope ? data->mScope->GetTimeoutManager() : nullptr;
    if (timeoutManager) {
      timeoutManager->Suspend();
    }
  }

  return true;
}

bool WorkerPrivate::ThawInternal() {
  auto data = mWorkerThreadAccessible.Access();
  NS_ASSERTION(data->mFrozen, "Not yet frozen!");

  // BindRemoteWorkerDebuggerChild();

  for (uint32_t index = 0; index < data->mChildWorkers.Length(); index++) {
    data->mChildWorkers[index]->Thaw(nullptr);
  }

  data->mFrozen = false;

  // The worker can thaw even if it failed to run (and doesn't have a global).
  if (data->mScope) {
    data->mScope->MutableClientSourceRef().Thaw();
  }

  if (StaticPrefs::dom_workers_timeoutmanager_AtStartup()) {
    auto* timeoutManager =
        data->mScope ? data->mScope->GetTimeoutManager() : nullptr;
    if (timeoutManager) {
      timeoutManager->Resume();
    }
  }

  return true;
}

bool WorkerPrivate::ChangeBackgroundStateInternal(bool aIsBackground) {
  AssertIsOnWorkerThread();
  mIsInBackground = aIsBackground;
  auto data = mWorkerThreadAccessible.Access();
  if (StaticPrefs::dom_workers_timeoutmanager_AtStartup() &&
      StaticPrefs::dom_workers_throttling_enabled_AtStartup()) {
    auto* timeoutManager =
        data->mScope ? data->mScope->GetTimeoutManager() : nullptr;
    if (timeoutManager) {
      timeoutManager->UpdateBackgroundState();
    }
  }

  for (uint32_t index = 0; index < data->mChildWorkers.Length(); index++) {
    if (aIsBackground) {
      data->mChildWorkers[index]->SetIsRunningInBackground();
    } else {
      data->mChildWorkers[index]->SetIsRunningInForeground();
    }
  }
  return true;
}

void WorkerPrivate::PropagateStorageAccessPermissionGrantedInternal() {
  auto data = mWorkerThreadAccessible.Access();

  mLoadInfo.mUseRegularPrincipal = true;
  mLoadInfo.mUsingStorageAccess = true;

  WorkerGlobalScope* globalScope = GlobalScope();
  if (globalScope) {
    globalScope->StorageAccessPermissionGranted();
  }

  for (uint32_t index = 0; index < data->mChildWorkers.Length(); index++) {
    data->mChildWorkers[index]->PropagateStorageAccessPermissionGranted();
  }
}

void WorkerPrivate::TraverseTimeouts(nsCycleCollectionTraversalCallback& cb) {
  auto data = mWorkerThreadAccessible.Access();
  if (StaticPrefs::dom_workers_timeoutmanager_AtStartup()) {
    auto* timeoutManager =
        data->mScope ? data->mScope->GetTimeoutManager() : nullptr;
    if (timeoutManager) {
      timeoutManager->ForEachUnorderedTimeout([&cb](Timeout* timeout) {
        cb.NoteNativeChild(timeout, NS_CYCLE_COLLECTION_PARTICIPANT(Timeout));
      });
    }
    return;
  }
  for (uint32_t i = 0; i < data->mTimeouts.Length(); ++i) {
    // TODO(erahm): No idea what's going on here.
    TimeoutInfo* tmp = data->mTimeouts[i].get();
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mHandler)
  }
}

void WorkerPrivate::UnlinkTimeouts() {
  auto data = mWorkerThreadAccessible.Access();
  if (StaticPrefs::dom_workers_timeoutmanager_AtStartup()) {
    auto* timeoutManager =
        data->mScope ? data->mScope->GetTimeoutManager() : nullptr;
    if (timeoutManager) {
      timeoutManager->ClearAllTimeouts();
      if (!timeoutManager->HasTimeouts()) {
        UpdateCCFlag(CCFlag::EligibleForTimeout);
      }
    }
    return;
  }
  data->mTimeouts.Clear();
}

bool WorkerPrivate::AddChildWorker(WorkerPrivate& aChildWorker) {
  auto data = mWorkerThreadAccessible.Access();

#ifdef DEBUG
  {
    WorkerStatus currentStatus;
    {
      MutexAutoLock lock(mMutex);
      currentStatus = mStatus;
    }

    MOZ_ASSERT(currentStatus == Running);
  }
#endif

  NS_ASSERTION(!data->mChildWorkers.Contains(&aChildWorker),
               "Already know about this one!");
  data->mChildWorkers.AppendElement(&aChildWorker);

  if (data->mChildWorkers.Length() == 1) {
    UpdateCCFlag(CCFlag::IneligibleForChildWorker);
  }

  return true;
}

void WorkerPrivate::RemoveChildWorker(WorkerPrivate& aChildWorker) {
  auto data = mWorkerThreadAccessible.Access();

  NS_ASSERTION(data->mChildWorkers.Contains(&aChildWorker),
               "Didn't know about this one!");
  data->mChildWorkers.RemoveElement(&aChildWorker);

  if (data->mChildWorkers.IsEmpty()) {
    UpdateCCFlag(CCFlag::EligibleForChildWorker);
  }
}

bool WorkerPrivate::AddWorkerRef(WorkerRef* aWorkerRef,
                                 WorkerStatus aFailStatus) {
  MOZ_ASSERT(aWorkerRef);
  auto data = mWorkerThreadAccessible.Access();

  {
    MutexAutoLock lock(mMutex);

    LOG(WorkerLog(),
        ("WorkerPrivate::AddWorkerRef [%p] mStatus: %u, aFailStatus: (%u)",
         this, static_cast<uint8_t>(mStatus),
         static_cast<uint8_t>(aFailStatus)));

    if (mStatus >= aFailStatus) {
      return false;
    }

    // We shouldn't create strong references to workers before their main loop
    // begins running. Strong references must be disposed of on the worker
    // thread, so strong references from other threads use a control runnable
    // for that purpose. If the worker fails to reach the main loop stage then
    // no control runnables get run and it would be impossible to get rid of the
    // reference properly.
    MOZ_DIAGNOSTIC_ASSERT_IF(aWorkerRef->IsPreventingShutdown(),
                             mStatus >= WorkerStatus::Running);
  }

  MOZ_ASSERT(!data->mWorkerRefs.Contains(aWorkerRef),
             "Already know about this one!");

  if (aWorkerRef->IsPreventingShutdown()) {
    data->mNumWorkerRefsPreventingShutdownStart += 1;
    if (data->mNumWorkerRefsPreventingShutdownStart == 1) {
      UpdateCCFlag(CCFlag::IneligibleForWorkerRef);
    }
  }

  data->mWorkerRefs.AppendElement(aWorkerRef);
  return true;
}

void WorkerPrivate::RemoveWorkerRef(WorkerRef* aWorkerRef) {
  MOZ_ASSERT(aWorkerRef);
  LOG(WorkerLog(),
      ("WorkerPrivate::RemoveWorkerRef [%p] aWorkerRef: %p", this, aWorkerRef));
  auto data = mWorkerThreadAccessible.Access();

  MOZ_ASSERT(data->mWorkerRefs.Contains(aWorkerRef),
             "Didn't know about this one!");
  data->mWorkerRefs.RemoveElement(aWorkerRef);

  if (aWorkerRef->IsPreventingShutdown()) {
    data->mNumWorkerRefsPreventingShutdownStart -= 1;
    if (!data->mNumWorkerRefsPreventingShutdownStart) {
      UpdateCCFlag(CCFlag::EligibleForWorkerRef);
    }
  }
}

void WorkerPrivate::NotifyWorkerRefs(WorkerStatus aStatus) {
  auto data = mWorkerThreadAccessible.Access();

  NS_ASSERTION(aStatus > Closing, "Bad status!");

  LOG(WorkerLog(), ("WorkerPrivate::NotifyWorkerRefs [%p] aStatus: %u", this,
                    static_cast<uint8_t>(aStatus)));

  for (auto* workerRef : data->mWorkerRefs.ForwardRange()) {
    LOG(WorkerLog(), ("WorkerPrivate::NotifyWorkerRefs [%p] WorkerRefs(%s %p)",
                      this, workerRef->mName, workerRef));
    workerRef->Notify();
  }

  AutoTArray<CheckedUnsafePtr<WorkerPrivate>, 10> children;
  children.AppendElements(data->mChildWorkers);

  for (uint32_t index = 0; index < children.Length(); index++) {
    if (!children[index]->Notify(aStatus)) {
      NS_WARNING("Failed to notify child worker!");
    }
  }
}

nsresult WorkerPrivate::RegisterShutdownTask(nsITargetShutdownTask* aTask) {
  NS_ENSURE_ARG(aTask);

  MutexAutoLock lock(mMutex);

  // If we've already started running shutdown tasks, don't allow registering
  // new ones.
  if (mShutdownTasksRun) {
    return NS_ERROR_UNEXPECTED;
  }

  MOZ_ASSERT(!mShutdownTasks.Contains(aTask));
  mShutdownTasks.AppendElement(aTask);
  return NS_OK;
}

nsresult WorkerPrivate::UnregisterShutdownTask(nsITargetShutdownTask* aTask) {
  NS_ENSURE_ARG(aTask);

  MutexAutoLock lock(mMutex);

  // We've already started running shutdown tasks, so can't unregister them
  // anymore.
  if (mShutdownTasksRun) {
    return NS_ERROR_UNEXPECTED;
  }

  return mShutdownTasks.RemoveElement(aTask) ? NS_OK : NS_ERROR_UNEXPECTED;
}

void WorkerPrivate::RunShutdownTasks() {
  nsTArray<nsCOMPtr<nsITargetShutdownTask>> shutdownTasks;

  {
    MutexAutoLock lock(mMutex);
    shutdownTasks = std::move(mShutdownTasks);
    mShutdownTasks.Clear();
    mShutdownTasksRun = true;
  }

  for (auto& task : shutdownTasks) {
    task->TargetShutdown();
  }
  mWorkerHybridEventTarget->ForgetWorkerPrivate(this);
}

RefPtr<WorkerParentRef> WorkerPrivate::GetWorkerParentRef() const {
  RefPtr<WorkerParentRef> ref(mParentRef);
  return ref;
}

void WorkerPrivate::AdjustNonblockingCCBackgroundActorCount(int32_t aCount) {
  AssertIsOnWorkerThread();
  auto data = mWorkerThreadAccessible.Access();
  LOGV(("WorkerPrivate::AdjustNonblockingCCBackgroundActors [%p] (%d/%u)", this,
        aCount, data->mNonblockingCCBackgroundActorCount));

#ifdef DEBUG
  if (aCount < 0) {
    MOZ_ASSERT(data->mNonblockingCCBackgroundActorCount >=
               (uint32_t)abs(aCount));
  }
#endif

  data->mNonblockingCCBackgroundActorCount += aCount;
}

void WorkerPrivate::UpdateCCFlag(const CCFlag aFlag) {
  AssertIsOnWorkerThread();

  auto data = mWorkerThreadAccessible.Access();

#ifdef DEBUG
  switch (aFlag) {
    case CCFlag::EligibleForWorkerRef: {
      MOZ_ASSERT(!data->mNumWorkerRefsPreventingShutdownStart);
      break;
    }
    case CCFlag::IneligibleForWorkerRef: {
      MOZ_ASSERT(data->mNumWorkerRefsPreventingShutdownStart);
      break;
    }
    case CCFlag::EligibleForChildWorker: {
      MOZ_ASSERT(data->mChildWorkers.IsEmpty());
      break;
    }
    case CCFlag::IneligibleForChildWorker: {
      MOZ_ASSERT(!data->mChildWorkers.IsEmpty());
      break;
    }
    case CCFlag::EligibleForTimeout: {
      if (StaticPrefs::dom_workers_timeoutmanager_AtStartup()) {
        auto* timeoutManager =
            data->mScope ? data->mScope->GetTimeoutManager() : nullptr;
        MOZ_ASSERT(timeoutManager && !timeoutManager->HasTimeouts());
        break;
      }
      MOZ_ASSERT(data->mTimeouts.IsEmpty());
      break;
    }
    case CCFlag::IneligibleForTimeout: {
      if (StaticPrefs::dom_workers_timeoutmanager_AtStartup()) {
        auto* timeoutManager =
            data->mScope ? data->mScope->GetTimeoutManager() : nullptr;
        MOZ_ASSERT(!timeoutManager || timeoutManager->HasTimeouts());
        break;
      }
      MOZ_ASSERT(!data->mTimeouts.IsEmpty());
      break;
    }
    case CCFlag::CheckBackgroundActors: {
      break;
    }
  }
#endif

  {
    MutexAutoLock lock(mMutex);
    if (mStatus > Canceling) {
      mCCFlagSaysEligible = true;
      return;
    }
  }
  auto HasBackgroundActors = [nonblockingActorCount =
                                  data->mNonblockingCCBackgroundActorCount]() {
    RefPtr<PBackgroundChild> backgroundChild =
        BackgroundChild::GetForCurrentThread();
    MOZ_ASSERT(backgroundChild);
    auto totalCount = backgroundChild->AllManagedActorsCount();
    LOGV(("WorkerPrivate::UpdateCCFlag HasBackgroundActors: %s(%u/%u)",
          totalCount > nonblockingActorCount ? "true" : "false", totalCount,
          nonblockingActorCount));

    return totalCount > nonblockingActorCount;
  };

  bool noTimeouts{data->mTimeouts.IsEmpty()};

  if (StaticPrefs::dom_workers_timeoutmanager_AtStartup()) {
    auto* timeoutManager =
        data->mScope ? data->mScope->GetTimeoutManager() : nullptr;
    if (timeoutManager) {
      noTimeouts = !timeoutManager->HasTimeouts();
    }
  }

  bool eligibleForCC = data->mChildWorkers.IsEmpty() && noTimeouts &&
                       !data->mNumWorkerRefsPreventingShutdownStart;

  // Only checking BackgroundActors when no strong WorkerRef, ChildWorker, and
  // Timeout since the checking is expensive.
  if (eligibleForCC) {
    eligibleForCC = !HasBackgroundActors();
  }

  {
    MutexAutoLock lock(mMutex);
    mCCFlagSaysEligible = eligibleForCC;
  }
}

bool WorkerPrivate::IsEligibleForCC() {
  LOGV(("WorkerPrivate::IsEligibleForCC [%p]", this));
  MutexAutoLock lock(mMutex);
  if (mStatus > Canceling) {
    return true;
  }

  bool hasShutdownTasks = !mShutdownTasks.IsEmpty();
  bool hasPendingEvents = false;
  if (mThread) {
    hasPendingEvents =
        NS_SUCCEEDED(mThread->HasPendingEvents(&hasPendingEvents)) &&
        hasPendingEvents;
  }

  LOGV(("mMainThreadEventTarget: %s",
        mMainThreadEventTarget->IsEmpty() ? "empty" : "non-empty"));
  LOGV(("mMainThreadEventTargetForMessaging: %s",
        mMainThreadEventTargetForMessaging->IsEmpty() ? "empty" : "non-empty"));
  LOGV(("mMainThreadDebuggerEventTarget: %s",
        mMainThreadDebuggeeEventTarget->IsEmpty() ? "empty" : "non-empty"));
  LOGV(("mCCFlagSaysEligible: %s", mCCFlagSaysEligible ? "true" : "false"));
  LOGV(("hasShutdownTasks: %s", hasShutdownTasks ? "true" : "false"));
  LOGV(("hasPendingEvents: %s", hasPendingEvents ? "true" : "false"));

  return mMainThreadEventTarget->IsEmpty() &&
         mMainThreadEventTargetForMessaging->IsEmpty() &&
         mMainThreadDebuggeeEventTarget->IsEmpty() && mCCFlagSaysEligible &&
         !hasShutdownTasks && !hasPendingEvents && mWorkerLoopIsIdle;
}

void WorkerPrivate::CancelAllTimeouts() {
  auto data = mWorkerThreadAccessible.Access();

  if (StaticPrefs::dom_workers_timeoutmanager_AtStartup()) {
    auto* timeoutManager =
        data->mScope ? data->mScope->GetTimeoutManager() : nullptr;
    if (timeoutManager) {
      timeoutManager->ClearAllTimeouts();
      if (!timeoutManager->HasTimeouts()) {
        UpdateCCFlag(CCFlag::EligibleForTimeout);
      }
    }
    return;
  }

  LOG(TimeoutsLog(), ("Worker %p CancelAllTimeouts.\n", this));

  if (data->mTimerRunning) {
    NS_ASSERTION(data->mTimer && data->mTimerRunnable, "Huh?!");
    NS_ASSERTION(!data->mTimeouts.IsEmpty(), "Huh?!");

    if (NS_FAILED(data->mTimer->Cancel())) {
      NS_WARNING("Failed to cancel timer!");
    }

    for (uint32_t index = 0; index < data->mTimeouts.Length(); index++) {
      data->mTimeouts[index]->mCanceled = true;
    }

    // If mRunningExpiredTimeouts, then the fact that they are all canceled now
    // means that the currently executing RunExpiredTimeouts will deal with
    // them.  Otherwise, we need to clean them up ourselves.
    if (!data->mRunningExpiredTimeouts) {
      data->mTimeouts.Clear();
      UpdateCCFlag(CCFlag::EligibleForTimeout);
    }

    // Set mTimerRunning false even if mRunningExpiredTimeouts is true, so that
    // if we get reentered under this same RunExpiredTimeouts call we don't
    // assert above that !mTimeouts().IsEmpty(), because that's clearly false
    // now.
    data->mTimerRunning = false;
  }
#ifdef DEBUG
  else if (!data->mRunningExpiredTimeouts) {
    NS_ASSERTION(data->mTimeouts.IsEmpty(), "Huh?!");
  }
#endif

  data->mTimer = nullptr;
  data->mTimerRunnable = nullptr;
}

already_AddRefed<nsISerialEventTarget> WorkerPrivate::CreateNewSyncLoop(
    WorkerStatus aFailStatus) {
  AssertIsOnWorkerThread();
  MOZ_ASSERT(
      aFailStatus >= Canceling,
      "Sync loops can be created when the worker is in Running/Closing state!");

  LOG(WorkerLog(), ("WorkerPrivate::CreateNewSyncLoop [%p] failstatus: %u",
                    this, static_cast<uint8_t>(aFailStatus)));

  ThreadEventQueue* queue = nullptr;
  {
    MutexAutoLock lock(mMutex);

    if (mStatus >= aFailStatus) {
      return nullptr;
    }
    queue = static_cast<ThreadEventQueue*>(mThread->EventQueue());
  }

  nsCOMPtr<nsISerialEventTarget> nestedEventTarget = queue->PushEventQueue();
  MOZ_ASSERT(nestedEventTarget);

  RefPtr<EventTarget> workerEventTarget =
      new EventTarget(this, nestedEventTarget);

  {
    // Modifications must be protected by mMutex in DEBUG builds, see comment
    // about mSyncLoopStack in WorkerPrivate.h.
#ifdef DEBUG
    MutexAutoLock lock(mMutex);
#endif

    mSyncLoopStack.AppendElement(new SyncLoopInfo(workerEventTarget));
  }

  return workerEventTarget.forget();
}

nsresult WorkerPrivate::RunCurrentSyncLoop() {
  AssertIsOnWorkerThread();
  LOG(WorkerLog(), ("WorkerPrivate::RunCurrentSyncLoop [%p]", this));
  RefPtr<WorkerThread> thread;
  JSContext* cx = GetJSContext();
  MOZ_ASSERT(cx);
  // mThread is set before we enter, and is never changed during
  // RunCurrentSyncLoop.
  {
    MutexAutoLock lock(mMutex);
    // Copy to local so we don't trigger mutex analysis lower down
    // mThread is set before we enter, and is never changed during
    // RunCurrentSyncLoop copy to local so we don't trigger mutex analysis
    thread = mThread;
  }

  AutoPushEventLoopGlobal eventLoopGlobal(this, cx);

  // This should not change between now and the time we finish running this sync
  // loop.
  uint32_t currentLoopIndex = mSyncLoopStack.Length() - 1;

  SyncLoopInfo* loopInfo = mSyncLoopStack[currentLoopIndex].get();

  AutoYieldJSThreadExecution yield;

  MOZ_ASSERT(loopInfo);
  MOZ_ASSERT(!loopInfo->mHasRun);
  MOZ_ASSERT(!loopInfo->mCompleted);

#ifdef DEBUG
  loopInfo->mHasRun = true;
#endif

  {
    while (!loopInfo->mCompleted) {
      bool normalRunnablesPending = false;

      // Don't block with the periodic GC timer running.
      if (!NS_HasPendingEvents(thread)) {
        SetGCTimerMode(IdleTimer);
      }

      // Wait for something to do.
      {
        MutexAutoLock lock(mMutex);

        for (;;) {
          while (mControlQueue.IsEmpty() && !normalRunnablesPending &&
                 !(normalRunnablesPending = NS_HasPendingEvents(thread))) {
            WaitForWorkerEvents();
          }

          auto result = ProcessAllControlRunnablesLocked();
          if (result != ProcessAllControlRunnablesResult::Nothing) {
            // The state of the world may have changed. Recheck it if we need to
            // continue.
            normalRunnablesPending =
                result == ProcessAllControlRunnablesResult::MayContinue &&
                NS_HasPendingEvents(thread);

            // NB: If we processed a NotifyRunnable, we might have run
            // non-control runnables, one of which may have shut down the
            // sync loop.
            if (loopInfo->mCompleted) {
              break;
            }
          }

          // If we *didn't* run any control runnables, this should be unchanged.
          MOZ_ASSERT(!loopInfo->mCompleted);

          if (normalRunnablesPending) {
            break;
          }
        }
      }

      if (normalRunnablesPending) {
        // Make sure the periodic timer is running before we continue.
        SetGCTimerMode(PeriodicTimer);

        MOZ_ALWAYS_TRUE(NS_ProcessNextEvent(thread, false));

        // Now *might* be a good time to GC. Let the JS engine make the
        // decision.
        if (GetCurrentEventLoopGlobal()) {
          // If GetCurrentEventLoopGlobal() is non-null, our JSContext is in a
          // Realm, so it's safe to try to GC.
          MOZ_ASSERT(JS::CurrentGlobalOrNull(cx));
          JS_MaybeGC(cx);
        }
      }
    }
  }

  // Make sure that the stack didn't change underneath us.
  MOZ_ASSERT(mSyncLoopStack[currentLoopIndex].get() == loopInfo);

  return DestroySyncLoop(currentLoopIndex);
}

nsresult WorkerPrivate::DestroySyncLoop(uint32_t aLoopIndex) {
  MOZ_ASSERT(!mSyncLoopStack.IsEmpty());
  MOZ_ASSERT(mSyncLoopStack.Length() - 1 == aLoopIndex);

  LOG(WorkerLog(),
      ("WorkerPrivate::DestroySyncLoop [%p] aLoopIndex: %u", this, aLoopIndex));

  AutoYieldJSThreadExecution yield;

  // We're about to delete the loop, stash its event target and result.
  const auto& loopInfo = mSyncLoopStack[aLoopIndex];

  nsresult result = loopInfo->mResult;

  {
    RefPtr<nsIEventTarget> nestedEventTarget(
        loopInfo->mEventTarget->GetNestedEventTarget());
    MOZ_ASSERT(nestedEventTarget);

    loopInfo->mEventTarget->Shutdown();

    {
      MutexAutoLock lock(mMutex);
      static_cast<ThreadEventQueue*>(mThread->EventQueue())
          ->PopEventQueue(nestedEventTarget);
    }
  }

  // Are we making a 1 -> 0 transition here?
  if (mSyncLoopStack.Length() == 1) {
    if ((mPostSyncLoopOperations & eDispatchCancelingRunnable)) {
      LOG(WorkerLog(),
          ("WorkerPrivate::DestroySyncLoop [%p] Dispatching CancelingRunnables",
           this));
      DispatchCancelingRunnable();
    }

    mPostSyncLoopOperations = 0;
  }

  {
    // Modifications must be protected by mMutex in DEBUG builds, see comment
    // about mSyncLoopStack in WorkerPrivate.h.
#ifdef DEBUG
    MutexAutoLock lock(mMutex);
#endif

    // This will delete |loopInfo|!
    mSyncLoopStack.RemoveElementAt(aLoopIndex);
  }

  return result;
}

void WorkerPrivate::DispatchCancelingRunnable() {
  // Here we use a normal runnable to know when the current JS chunk of code
  // is finished. We cannot use a WorkerRunnable because they are not
  // accepted any more by the worker, and we do not want to use a
  // WorkerControlRunnable because they are immediately executed.

  LOG(WorkerLog(), ("WorkerPrivate::DispatchCancelingRunnable [%p]", this));
  RefPtr<CancelingRunnable> r = new CancelingRunnable();
  {
    MutexAutoLock lock(mMutex);
    mThread->nsThread::Dispatch(r.forget(), NS_DISPATCH_NORMAL);
  }

  // At the same time, we want to be sure that we interrupt infinite loops.
  // The following runnable starts a timer that cancel the worker, from the
  // parent thread, after CANCELING_TIMEOUT millseconds.
  LOG(WorkerLog(), ("WorkerPrivate::DispatchCancelingRunnable [%p] Setup a "
                    "timeout canceling",
                    this));
  RefPtr<CancelingWithTimeoutOnParentRunnable> rr =
      new CancelingWithTimeoutOnParentRunnable(this);
  rr->Dispatch(this);
}

void WorkerPrivate::ReportUseCounters() {
  AssertIsOnWorkerThread();

  if (mReportedUseCounters) {
    return;
  }
  mReportedUseCounters = true;

  if (IsChromeWorker()) {
    return;
  }

  const size_t kind = Kind();
  switch (kind) {
    case WorkerKindDedicated:
      glean::use_counter::dedicated_workers_destroyed.Add();
      break;
    case WorkerKindShared:
      glean::use_counter::shared_workers_destroyed.Add();
      break;
    case WorkerKindService:
      glean::use_counter::service_workers_destroyed.Add();
      break;
    default:
      MOZ_ASSERT(false, "Unknown worker kind");
      return;
  }

  Maybe<nsCString> workerPathForLogging;
  const bool dumpCounters = StaticPrefs::dom_use_counters_dump_worker();
  if (dumpCounters) {
    nsAutoCString path(Domain());
    path.AppendLiteral("(");
    NS_ConvertUTF16toUTF8 script(ScriptURL());
    path.Append(script);
    path.AppendPrintf(", 0x%p)", this);
    workerPathForLogging.emplace(std::move(path));
  }

  const size_t count = static_cast<size_t>(UseCounterWorker::Count);

  const auto workerKind = Kind();
  for (size_t c = 0; c < count; ++c) {
    if (!GetUseCounter(static_cast<UseCounterWorker>(c))) {
      continue;
    }
    const char* metricName =
        IncrementWorkerUseCounter(static_cast<UseCounterWorker>(c), workerKind);
    if (dumpCounters) {
      printf_stderr("USE_COUNTER_WORKER: %s - %s\n", metricName,
                    workerPathForLogging->get());
    }
  }
}

void WorkerPrivate::StopSyncLoop(nsIEventTarget* aSyncLoopTarget,
                                 nsresult aResult) {
  AssertValidSyncLoop(aSyncLoopTarget);

  if (!MaybeStopSyncLoop(aSyncLoopTarget, aResult)) {
    // TODO: I wonder if we should really ever crash here given the assert.
    MOZ_CRASH("Unknown sync loop!");
  }
}

bool WorkerPrivate::MaybeStopSyncLoop(nsIEventTarget* aSyncLoopTarget,
                                      nsresult aResult) {
  AssertIsOnWorkerThread();

  for (uint32_t index = mSyncLoopStack.Length(); index > 0; index--) {
    const auto& loopInfo = mSyncLoopStack[index - 1];
    MOZ_ASSERT(loopInfo);
    MOZ_ASSERT(loopInfo->mEventTarget);

    if (loopInfo->mEventTarget == aSyncLoopTarget) {
      // Can't assert |loop->mHasRun| here because dispatch failures can cause
      // us to bail out early.
      MOZ_ASSERT(!loopInfo->mCompleted);

      loopInfo->mResult = aResult;
      loopInfo->mCompleted = true;

      loopInfo->mEventTarget->Disable();

      return true;
    }

    MOZ_ASSERT(!SameCOMIdentity(loopInfo->mEventTarget, aSyncLoopTarget));
  }

  return false;
}

#ifdef DEBUG
void WorkerPrivate::AssertValidSyncLoop(nsIEventTarget* aSyncLoopTarget) {
  MOZ_ASSERT(aSyncLoopTarget);

  EventTarget* workerTarget;
  nsresult rv = aSyncLoopTarget->QueryInterface(
      kDEBUGWorkerEventTargetIID, reinterpret_cast<void**>(&workerTarget));
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  MOZ_ASSERT(workerTarget);

  bool valid = false;

  {
    MutexAutoLock lock(mMutex);

    for (uint32_t index = 0; index < mSyncLoopStack.Length(); index++) {
      const auto& loopInfo = mSyncLoopStack[index];
      MOZ_ASSERT(loopInfo);
      MOZ_ASSERT(loopInfo->mEventTarget);

      if (loopInfo->mEventTarget == aSyncLoopTarget) {
        valid = true;
        break;
      }

      MOZ_ASSERT(!SameCOMIdentity(loopInfo->mEventTarget, aSyncLoopTarget));
    }
  }

  MOZ_ASSERT(valid);
}
#endif

void WorkerPrivate::PostMessageToParent(
    JSContext* aCx, JS::Handle<JS::Value> aMessage,
    const Sequence<JSObject*>& aTransferable, ErrorResult& aRv) {
  LOG(WorkerLog(), ("WorkerPrivate::PostMessageToParent [%p]", this));
  AssertIsOnWorkerThread();
  MOZ_DIAGNOSTIC_ASSERT(IsDedicatedWorker());

  JS::Rooted<JS::Value> transferable(aCx, JS::UndefinedValue());

  aRv = nsContentUtils::CreateJSValueFromSequenceOfObject(aCx, aTransferable,
                                                          &transferable);
  if (NS_WARN_IF(aRv.Failed())) {
    return;
  }

  RefPtr<MessageEventToParentRunnable> runnable =
      new MessageEventToParentRunnable(this);

  JS::CloneDataPolicy clonePolicy;

  // Parent and dedicated workers are always part of the same cluster.
  clonePolicy.allowIntraClusterClonableSharedObjects();

  if (IsSharedMemoryAllowed()) {
    clonePolicy.allowSharedMemoryObjects();
  }

  runnable->Write(aCx, aMessage, transferable, clonePolicy, aRv);

  if (NS_WARN_IF(aRv.Failed())) {
    return;
  }

  if (!runnable->Dispatch(this)) {
    aRv = NS_ERROR_FAILURE;
  }
}

void WorkerPrivate::EnterDebuggerEventLoop() {
  auto data = mWorkerThreadAccessible.Access();

  JSContext* cx = GetJSContext();
  MOZ_ASSERT(cx);

  AutoPushEventLoopGlobal eventLoopGlobal(this, cx);
  AutoYieldJSThreadExecution yield;

  CycleCollectedJSContext* ccjscx = CycleCollectedJSContext::Get();

  uint32_t currentEventLoopLevel = ++data->mDebuggerEventLoopLevel;

  while (currentEventLoopLevel <= data->mDebuggerEventLoopLevel) {
    bool debuggerRunnablesPending = false;

    {
      MutexAutoLock lock(mMutex);

      debuggerRunnablesPending = !mDebuggerQueue.IsEmpty();
    }

    // Don't block with the periodic GC timer running.
    if (!debuggerRunnablesPending) {
      SetGCTimerMode(IdleTimer);
    }

    // Wait for something to do
    {
      MutexAutoLock lock(mMutex);

      std::deque<RefPtr<MicroTaskRunnable>>& debuggerMtQueue =
          ccjscx->GetDebuggerMicroTaskQueue();
      while (mControlQueue.IsEmpty() &&
             !(debuggerRunnablesPending = !mDebuggerQueue.IsEmpty()) &&
             debuggerMtQueue.empty()) {
        WaitForWorkerEvents();
      }

      ProcessAllControlRunnablesLocked();

      // XXXkhuey should we abort JS on the stack here if we got Abort above?
    }
    ccjscx->PerformDebuggerMicroTaskCheckpoint();
    if (debuggerRunnablesPending) {
      // Start the periodic GC timer if it is not already running.
      SetGCTimerMode(PeriodicTimer);

      ProcessSingleDebuggerRunnable();

      // Now *might* be a good time to GC. Let the JS engine make the decision.
      if (GetCurrentEventLoopGlobal()) {
        // If GetCurrentEventLoopGlobal() is non-null, our JSContext is in a
        // Realm, so it's safe to try to GC.
        MOZ_ASSERT(JS::CurrentGlobalOrNull(cx));
        JS_MaybeGC(cx);
      }
    }
  }
}

void WorkerPrivate::LeaveDebuggerEventLoop() {
  auto data = mWorkerThreadAccessible.Access();

  // TODO: Why lock the mutex if we're accessing data accessible to one thread
  // only?
  MutexAutoLock lock(mMutex);

  if (data->mDebuggerEventLoopLevel > 0) {
    --data->mDebuggerEventLoopLevel;
  }
}

void WorkerPrivate::PostMessageToDebugger(const nsAString& aMessage) {
  AssertIsOnWorkerThread();

  mDebugger->PostMessageToDebugger(aMessage);
  RefPtr<RemoteWorkerDebuggerChild> remoteDebugger;
  {
    MutexAutoLock lock(mMutex);
    if (!mRemoteDebugger) {
      return;
    }
    remoteDebugger = mRemoteDebugger;
  }
  MOZ_ASSERT_DEBUG_OR_FUZZING(remoteDebugger);
  Unused << remoteDebugger->SendPostMessageToDebugger(nsAutoString(aMessage));
}

void WorkerPrivate::SetDebuggerImmediate(dom::Function& aHandler,
                                         ErrorResult& aRv) {
  AssertIsOnWorkerThread();

  RefPtr<DebuggerImmediateRunnable> runnable =
      new DebuggerImmediateRunnable(this, aHandler);
  if (!runnable->Dispatch(this)) {
    aRv.Throw(NS_ERROR_FAILURE);
  }
}

void WorkerPrivate::ReportErrorToDebugger(const nsACString& aFilename,
                                          uint32_t aLineno,
                                          const nsAString& aMessage) {
  AssertIsOnWorkerThread();
  mDebugger->ReportErrorToDebugger(aFilename, aLineno, aMessage);
  RefPtr<RemoteWorkerDebuggerChild> remoteDebugger;
  {
    MutexAutoLock lock(mMutex);
    if (!mRemoteDebugger) {
      return;
    }
    remoteDebugger = mRemoteDebugger;
  }
  MOZ_ASSERT_DEBUG_OR_FUZZING(remoteDebugger);
  Unused << remoteDebugger->SendReportErrorToDebugger(
      RemoteWorkerDebuggerErrorInfo(nsAutoCString(aFilename), aLineno,
                                    nsAutoString(aMessage)));
}

void WorkerPrivate::UpdateWindowIDToDebugger(const uint64_t& aWindowID,
                                             const bool& aIsAdd) {
  AssertIsOnWorkerThread();
  // only need to update the remote debugger since local debugger grab the
  // windowIDs information from RemoteWorkerChild directly.

  RefPtr<RemoteWorkerDebuggerChild> remoteDebugger;
  {
    MutexAutoLock lock(mMutex);
    if (!mRemoteDebugger) {
      return;
    }
    remoteDebugger = mRemoteDebugger;
  }
  MOZ_ASSERT_DEBUG_OR_FUZZING(remoteDebugger);
  if (aIsAdd) {
    Unused << remoteDebugger->SendAddWindowID(aWindowID);
  } else {
    Unused << remoteDebugger->SendRemoveWindowID(aWindowID);
  }
}

bool WorkerPrivate::NotifyInternal(WorkerStatus aStatus) {
  auto data = mWorkerThreadAccessible.Access();

  // Yield execution while notifying out-of-module WorkerRefs and cancelling
  // runnables.
  AutoYieldJSThreadExecution yield;

  NS_ASSERTION(aStatus > Running && aStatus < Dead, "Bad status!");

  RefPtr<EventTarget> eventTarget;

  // Save the old status and set the new status.
  {
    MutexAutoLock lock(mMutex);

    LOG(WorkerLog(),
        ("WorkerPrivate::NotifyInternal [%p] mStatus: %u, aStatus: %u", this,
         static_cast<uint8_t>(mStatus), static_cast<uint8_t>(aStatus)));

    if (mStatus >= aStatus) {
      return true;
    }

    MOZ_ASSERT_IF(aStatus == Killing,
                  mStatus == Canceling && mParentStatus == Canceling);

    mStatus = aStatus;

    // Mark parent status as closing immediately to avoid new events being
    // dispatched after we clear the queue below.
    if (aStatus == Closing) {
      Close();
    }

    // Synchronize the mParentStatus with mStatus, such that event dispatching
    // will fail in proper after WorkerPrivate gets into Killing status.
    if (aStatus >= Killing) {
      mParentStatus = aStatus;
    }
  }

  // Status transistion to "Canceling"/"Killing", mark the scope as dying when
  // "Canceling," or shutdown the StorageManager when "Killing."
  if (aStatus >= Canceling) {
    if (data->mScope) {
      if (aStatus == Canceling) {
        data->mScope->NoteTerminating();
      } else {
        data->mScope->NoteShuttingDown();
      }
    }
  }

  if (aStatus >= Closing) {
    CancelAllTimeouts();
  }

  if (aStatus == Closing && GlobalScope()) {
    GlobalScope()->SetIsNotEligibleForMessaging();
  }

  // Let all our holders know the new status.
  if (aStatus == Canceling) {
    NotifyWorkerRefs(aStatus);
  }

  if (aStatus == Canceling && mRemoteWorkerNonLifeCycleOpController) {
    mRemoteWorkerNonLifeCycleOpController->TransistionStateToCanceled();
  }

  if (aStatus == Killing && mRemoteWorkerNonLifeCycleOpController) {
    mRemoteWorkerNonLifeCycleOpController->TransistionStateToKilled();
    mRemoteWorkerNonLifeCycleOpController = nullptr;
  }

  // If the worker script never ran, or failed to compile, we don't need to do
  // anything else.
  WorkerGlobalScope* global = GlobalScope();
  if (!global) {
    if (aStatus == Canceling) {
      MOZ_ASSERT(!data->mCancelBeforeWorkerScopeConstructed);
      data->mCancelBeforeWorkerScopeConstructed.Flip();
    }
    return true;
  }

  // Don't abort the script now, but we dispatch a runnable to do it when the
  // current JS frame is executed.
  if (aStatus == Closing) {
    if (!mSyncLoopStack.IsEmpty()) {
      LOG(WorkerLog(), ("WorkerPrivate::NotifyInternal [%p] request to "
                        "dispatch canceling runnables...",
                        this));
      mPostSyncLoopOperations |= eDispatchCancelingRunnable;
    } else {
      DispatchCancelingRunnable();
    }
    return true;
  }

  MOZ_ASSERT(aStatus == Canceling || aStatus == Killing);

  LOG(WorkerLog(), ("WorkerPrivate::NotifyInternal [%p] abort script", this));

  // Always abort the script.
  return false;
}

void WorkerPrivate::ReportError(JSContext* aCx,
                                JS::ConstUTF8CharsZ aToStringResult,
                                JSErrorReport* aReport) {
  auto data = mWorkerThreadAccessible.Access();

  if (!MayContinueRunning() || data->mErrorHandlerRecursionCount == 2) {
    return;
  }

  NS_ASSERTION(data->mErrorHandlerRecursionCount == 0 ||
                   data->mErrorHandlerRecursionCount == 1,
               "Bad recursion logic!");

  UniquePtr<WorkerErrorReport> report = MakeUnique<WorkerErrorReport>();
  if (aReport) {
    report->AssignErrorReport(aReport);
  }

  JS::ExceptionStack exnStack(aCx);
  if (JS_IsExceptionPending(aCx)) {
    if (!JS::StealPendingExceptionStack(aCx, &exnStack)) {
      JS_ClearPendingException(aCx);
      return;
    }

    JS::Rooted<JSObject*> stack(aCx), stackGlobal(aCx);
    xpc::FindExceptionStackForConsoleReport(
        nullptr, exnStack.exception(), exnStack.stack(), &stack, &stackGlobal);

    if (stack) {
      JSAutoRealm ar(aCx, stackGlobal);
      report->SerializeWorkerStack(aCx, this, stack);
    }
  } else {
    // ReportError is also used for reporting warnings,
    // so there won't be a pending exception.
    MOZ_ASSERT(aReport && aReport->isWarning());
  }

  if (report->mMessage.IsEmpty() && aToStringResult) {
    nsDependentCString toStringResult(aToStringResult.c_str());
    if (!AppendUTF8toUTF16(toStringResult, report->mMessage,
                           mozilla::fallible)) {
      // Try again, with only a 1 KB string. Do this infallibly this time.
      // If the user doesn't have 1 KB to spare we're done anyways.
      size_t index = std::min<size_t>(1024, toStringResult.Length());

      // Drop the last code point that may be cropped.
      index = RewindToPriorUTF8Codepoint(toStringResult.BeginReading(), index);

      nsDependentCString truncatedToStringResult(aToStringResult.c_str(),
                                                 index);
      AppendUTF8toUTF16(truncatedToStringResult, report->mMessage);
    }
  }

  data->mErrorHandlerRecursionCount++;

  // Don't want to run the scope's error handler if this is a recursive error or
  // if we ran out of memory.
  bool fireAtScope = data->mErrorHandlerRecursionCount == 1 &&
                     report->mErrorNumber != JSMSG_OUT_OF_MEMORY &&
                     JS::CurrentGlobalOrNull(aCx);

  WorkerErrorReport::ReportError(aCx, this, fireAtScope, nullptr,
                                 std::move(report), 0, exnStack.exception());

  data->mErrorHandlerRecursionCount--;
}

// static
void WorkerPrivate::ReportErrorToConsole(
    uint32_t aErrorFlags, const nsCString& aCategory,
    nsContentUtils::PropertiesFile aFile, const nsCString& aMessageName,
    const nsTArray<nsString>& aParams,
    const mozilla::SourceLocation& aLocation) {
  WorkerPrivate* wp = nullptr;
  if (!NS_IsMainThread()) {
    wp = GetCurrentThreadWorkerPrivate();
  }

  ReportErrorToConsoleRunnable::Report(wp, aErrorFlags, aCategory, aFile,
                                       aMessageName, aParams, aLocation);
}

int32_t WorkerPrivate::SetTimeout(JSContext* aCx, TimeoutHandler* aHandler,
                                  int32_t aTimeout, bool aIsInterval,
                                  Timeout::Reason aReason, ErrorResult& aRv) {
  auto data = mWorkerThreadAccessible.Access();
  MOZ_ASSERT(aHandler);

  if (StaticPrefs::dom_workers_timeoutmanager_AtStartup()) {
    WorkerGlobalScope* globalScope = GlobalScope();
    MOZ_DIAGNOSTIC_ASSERT(globalScope);
    auto* timeoutManager = globalScope->GetTimeoutManager();
    int32_t timerId = -1;
    WorkerStatus status;
    {
      MutexAutoLock lock(mMutex);
      status = mStatus;
    }
    // If the worker is trying to call setTimeout/setInterval and the
    // worker itself which has initiated the close process.
    if (status >= Closing) {
      return timeoutManager->GetTimeoutId(aReason);
    }
    bool hadTimeouts = timeoutManager->HasTimeouts();
    nsresult rv = timeoutManager->SetTimeout(aHandler, aTimeout, aIsInterval,
                                             aReason, &timerId);
    if (NS_FAILED(rv)) {
      aRv.Throw(NS_ERROR_FAILURE);
      return timerId;
    }
    if (!hadTimeouts) {
      UpdateCCFlag(CCFlag::IneligibleForTimeout);
    }
    return timerId;
  }

  // Reasons that doesn't support cancellation will get -1 as their ids.
  int32_t timerId = -1;
  if (aReason == Timeout::Reason::eTimeoutOrInterval) {
    timerId = data->mNextTimeoutId;
    data->mNextTimeoutId += 1;
  }

  WorkerStatus currentStatus;
  {
    MutexAutoLock lock(mMutex);
    currentStatus = mStatus;
  }

  // If the worker is trying to call setTimeout/setInterval and the parent
  // thread has initiated the close process then just silently fail.
  if (currentStatus >= Closing) {
    return timerId;
  }

  auto newInfo = MakeUnique<TimeoutInfo>();
  newInfo->mReason = aReason;
  newInfo->mOnChromeWorker = mIsChromeWorker;
  newInfo->mIsInterval = aIsInterval;
  newInfo->mId = timerId;
  if (newInfo->mReason == Timeout::Reason::eTimeoutOrInterval ||
      newInfo->mReason == Timeout::Reason::eIdleCallbackTimeout) {
    newInfo->AccumulateNestingLevel(data->mCurrentTimerNestingLevel);
  }

  if (MOZ_UNLIKELY(timerId == INT32_MAX)) {
    NS_WARNING("Timeout ids overflowed!");
    if (aReason == Timeout::Reason::eTimeoutOrInterval) {
      data->mNextTimeoutId = 1;
    }
  }

  newInfo->mHandler = aHandler;

  // See if any of the optional arguments were passed.
  aTimeout = std::max(0, aTimeout);
  newInfo->mInterval = TimeDuration::FromMilliseconds(aTimeout);
  newInfo->CalculateTargetTime();

  const auto& insertedInfo = data->mTimeouts.InsertElementSorted(
      std::move(newInfo), GetUniquePtrComparator(data->mTimeouts));

  LOG(TimeoutsLog(), ("Worker %p has new timeout: delay=%d interval=%s\n", this,
                      aTimeout, aIsInterval ? "yes" : "no"));

  // If the timeout we just made is set to fire next then we need to update the
  // timer, unless we're currently running timeouts.
  if (insertedInfo == data->mTimeouts.Elements() &&
      !data->mRunningExpiredTimeouts) {
    if (!data->mTimer) {
      data->mTimer = NS_NewTimer(GlobalScope()->SerialEventTarget());
      if (!data->mTimer) {
        aRv.Throw(NS_ERROR_UNEXPECTED);
        return 0;
      }

      data->mTimerRunnable = new RunExpiredTimoutsRunnable(this);
    }

    if (!data->mTimerRunning) {
      UpdateCCFlag(CCFlag::IneligibleForTimeout);
      data->mTimerRunning = true;
    }

    if (!RescheduleTimeoutTimer(aCx)) {
      aRv.Throw(NS_ERROR_FAILURE);
      return 0;
    }
  }

  return timerId;
}

void WorkerPrivate::ClearTimeout(int32_t aId, Timeout::Reason aReason) {
  MOZ_ASSERT(aReason == Timeout::Reason::eTimeoutOrInterval,
             "This timeout reason doesn't support cancellation.");
  if (StaticPrefs::dom_workers_timeoutmanager_AtStartup()) {
    WorkerGlobalScope* globalScope = GlobalScope();
    MOZ_DIAGNOSTIC_ASSERT(globalScope);
    auto* timeoutManager = globalScope->GetTimeoutManager();
    timeoutManager->ClearTimeout(aId, aReason);
    if (!timeoutManager->HasTimeouts()) {
      UpdateCCFlag(CCFlag::EligibleForTimeout);
    }
    return;
  }

  auto data = mWorkerThreadAccessible.Access();

  if (!data->mTimeouts.IsEmpty()) {
    NS_ASSERTION(data->mTimerRunning, "Huh?!");

    for (uint32_t index = 0; index < data->mTimeouts.Length(); index++) {
      const auto& info = data->mTimeouts[index];
      if (info->mId == aId && info->mReason == aReason) {
        info->mCanceled = true;
        break;
      }
    }
  }
}

bool WorkerPrivate::RunExpiredTimeouts(JSContext* aCx) {
  auto data = mWorkerThreadAccessible.Access();

  // We may be called recursively (e.g. close() inside a timeout) or we could
  // have been canceled while this event was pending, bail out if there is
  // nothing to do.
  if (data->mRunningExpiredTimeouts || !data->mTimerRunning) {
    return true;
  }

  NS_ASSERTION(data->mTimer && data->mTimerRunnable, "Must have a timer!");
  NS_ASSERTION(!data->mTimeouts.IsEmpty(), "Should have some work to do!");

  bool retval = true;

  auto comparator = GetUniquePtrComparator(data->mTimeouts);
  JS::Rooted<JSObject*> global(aCx, JS::CurrentGlobalOrNull(aCx));

  // We want to make sure to run *something*, even if the timer fired a little
  // early. Fudge the value of now to at least include the first timeout.
  const TimeStamp actual_now = TimeStamp::Now();
  const TimeStamp now = std::max(actual_now, data->mTimeouts[0]->mTargetTime);

  if (now != actual_now) {
    LOG(TimeoutsLog(), ("Worker %p fudged timeout by %f ms.\n", this,
                        (now - actual_now).ToMilliseconds()));
#ifdef DEBUG
    double microseconds = (now - actual_now).ToMicroseconds();
    uint32_t allowedEarlyFiringMicroseconds;
    data->mTimer->GetAllowedEarlyFiringMicroseconds(
        &allowedEarlyFiringMicroseconds);
    MOZ_ASSERT(microseconds < allowedEarlyFiringMicroseconds);
#endif
  }

  AutoTArray<TimeoutInfo*, 10> expiredTimeouts;
  for (uint32_t index = 0; index < data->mTimeouts.Length(); index++) {
    TimeoutInfo* info = data->mTimeouts[index].get();
    if (info->mTargetTime > now) {
      break;
    }
    expiredTimeouts.AppendElement(info);
  }

  // Guard against recursion.
  data->mRunningExpiredTimeouts = true;

  MOZ_DIAGNOSTIC_ASSERT(data->mCurrentTimerNestingLevel == 0);

  // Run expired timeouts.
  for (uint32_t index = 0; index < expiredTimeouts.Length(); index++) {
    TimeoutInfo*& info = expiredTimeouts[index];
    AutoRestore<uint32_t> nestingLevel(data->mCurrentTimerNestingLevel);

    if (info->mCanceled) {
      continue;
    }

    // Set current timer nesting level to current running timer handler's
    // nesting level
    data->mCurrentTimerNestingLevel = info->mNestingLevel;

    LOG(TimeoutsLog(),
        ("Worker %p executing timeout with original delay %f ms.\n", this,
         info->mInterval.ToMilliseconds()));

    // Always check JS_IsExceptionPending if something fails, and if
    // JS_IsExceptionPending returns false (i.e. uncatchable exception) then
    // break out of the loop.

    RefPtr<TimeoutHandler> handler(info->mHandler);

    const char* reason;
    switch (info->mReason) {
      case Timeout::Reason::eTimeoutOrInterval:
        if (info->mIsInterval) {
          reason = "setInterval handler";
        } else {
          reason = "setTimeout handler";
        }
        break;
      case Timeout::Reason::eDelayedWebTaskTimeout:
        reason = "delayedWebTask handler";
        break;
      default:
        MOZ_ASSERT(info->mReason == Timeout::Reason::eAbortSignalTimeout);
        reason = "AbortSignal Timeout";
    }
    if (info->mReason == Timeout::Reason::eTimeoutOrInterval ||
        info->mReason == Timeout::Reason::eDelayedWebTaskTimeout) {
      RefPtr<WorkerGlobalScope> scope(this->GlobalScope());
      CallbackDebuggerNotificationGuard guard(
          scope, info->mIsInterval
                     ? DebuggerNotificationType::SetIntervalCallback
                     : DebuggerNotificationType::SetTimeoutCallback);

      if (!handler->Call(reason)) {
        retval = false;
        break;
      }
    } else {
      MOZ_ASSERT(info->mReason == Timeout::Reason::eAbortSignalTimeout);
      MOZ_ALWAYS_TRUE(handler->Call(reason));
    }

    NS_ASSERTION(data->mRunningExpiredTimeouts, "Someone changed this!");
  }

  // No longer possible to be called recursively.
  data->mRunningExpiredTimeouts = false;

  // Now remove canceled and expired timeouts from the main list.
  // NB: The timeouts present in expiredTimeouts must have the same order
  // with respect to each other in mTimeouts.  That is, mTimeouts is just
  // expiredTimeouts with extra elements inserted.  There may be unexpired
  // timeouts that have been inserted between the expired timeouts if the
  // timeout event handler called setTimeout/setInterval.
  for (uint32_t index = 0, expiredTimeoutIndex = 0,
                expiredTimeoutLength = expiredTimeouts.Length();
       index < data->mTimeouts.Length();) {
    const auto& info = data->mTimeouts[index];
    if ((expiredTimeoutIndex < expiredTimeoutLength &&
         info == expiredTimeouts[expiredTimeoutIndex] &&
         ++expiredTimeoutIndex) ||
        info->mCanceled) {
      if (info->mIsInterval && !info->mCanceled) {
        // Reschedule intervals.
        // Reschedule a timeout, if needed, increase the nesting level.
        info->AccumulateNestingLevel(info->mNestingLevel);
        info->CalculateTargetTime();
        // Don't resort the list here, we'll do that at the end.
        ++index;
      } else {
        data->mTimeouts.RemoveElement(info);
      }
    } else {
      // If info did not match the current entry in expiredTimeouts, it
      // shouldn't be there at all.
      NS_ASSERTION(!expiredTimeouts.Contains(info),
                   "Our timeouts are out of order!");
      ++index;
    }
  }

  data->mTimeouts.Sort(comparator);

  // Either signal the parent that we're no longer using timeouts or reschedule
  // the timer.
  if (data->mTimeouts.IsEmpty()) {
    UpdateCCFlag(CCFlag::EligibleForTimeout);
    data->mTimerRunning = false;
  } else if (retval && !RescheduleTimeoutTimer(aCx)) {
    retval = false;
  }

  return retval;
}

bool WorkerPrivate::RescheduleTimeoutTimer(JSContext* aCx) {
  auto data = mWorkerThreadAccessible.Access();
  MOZ_ASSERT(!data->mRunningExpiredTimeouts);
  NS_ASSERTION(!data->mTimeouts.IsEmpty(), "Should have some timeouts!");
  NS_ASSERTION(data->mTimer && data->mTimerRunnable, "Should have a timer!");

  // NB: This is important! The timer may have already fired, e.g. if a timeout
  // callback itself calls setTimeout for a short duration and then takes longer
  // than that to finish executing. If that has happened, it's very important
  // that we don't execute the event that is now pending in our event queue, or
  // our code in RunExpiredTimeouts to "fudge" the timeout value will unleash an
  // early timeout when we execute the event we're about to queue.
  data->mTimer->Cancel();

  double delta =
      (data->mTimeouts[0]->mTargetTime - TimeStamp::Now()).ToMilliseconds();
  uint32_t delay = delta > 0 ? static_cast<uint32_t>(std::ceil(
                                   std::min(delta, double(UINT32_MAX))))
                             : 0;

  LOG(TimeoutsLog(),
      ("Worker %p scheduled timer for %d ms, %zu pending timeouts\n", this,
       delay, data->mTimeouts.Length()));

  nsresult rv = data->mTimer->InitWithCallback(data->mTimerRunnable, delay,
                                               nsITimer::TYPE_ONE_SHOT);
  if (NS_FAILED(rv)) {
    JS_ReportErrorASCII(aCx, "Failed to start timer!");
    return false;
  }

  return true;
}

void WorkerPrivate::StartCancelingTimer() {
  AssertIsOnParentThread();

  // return if mCancelingTimer has already existed.
  if (mCancelingTimer) {
    return;
  }

  auto errorCleanup = MakeScopeExit([&] { mCancelingTimer = nullptr; });

  if (WorkerPrivate* parent = GetParent()) {
    mCancelingTimer = NS_NewTimer(parent->ControlEventTarget());
  } else {
    mCancelingTimer = NS_NewTimer();
  }

  if (NS_WARN_IF(!mCancelingTimer)) {
    return;
  }

  // This is not needed if we are already in an advanced shutdown state.
  {
    MutexAutoLock lock(mMutex);
    if (ParentStatus() >= Canceling) {
      return;
    }
  }

  uint32_t cancelingTimeoutMillis =
      StaticPrefs::dom_worker_canceling_timeoutMilliseconds();

  RefPtr<CancelingTimerCallback> callback = new CancelingTimerCallback(this);
  nsresult rv = mCancelingTimer->InitWithCallback(
      callback, cancelingTimeoutMillis, nsITimer::TYPE_ONE_SHOT);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  errorCleanup.release();
}

void WorkerPrivate::UpdateContextOptionsInternal(
    JSContext* aCx, const JS::ContextOptions& aContextOptions) {
  auto data = mWorkerThreadAccessible.Access();

  JS::ContextOptionsRef(aCx) = aContextOptions;

  for (uint32_t index = 0; index < data->mChildWorkers.Length(); index++) {
    data->mChildWorkers[index]->UpdateContextOptions(aContextOptions);
  }
}

void WorkerPrivate::UpdateLanguagesInternal(
    const nsTArray<nsString>& aLanguages) {
  WorkerGlobalScope* globalScope = GlobalScope();
  RefPtr<WorkerNavigator> nav = globalScope->GetExistingNavigator();
  if (nav) {
    nav->SetLanguages(aLanguages);
  }

  auto data = mWorkerThreadAccessible.Access();
  for (uint32_t index = 0; index < data->mChildWorkers.Length(); index++) {
    data->mChildWorkers[index]->UpdateLanguages(aLanguages);
  }

  RefPtr<Event> event = NS_NewDOMEvent(globalScope, nullptr, nullptr);

  event->InitEvent(u"languagechange"_ns, false, false);
  event->SetTrusted(true);

  globalScope->DispatchEvent(*event);
}

void WorkerPrivate::UpdateJSWorkerMemoryParameterInternal(
    JSContext* aCx, JSGCParamKey aKey, Maybe<uint32_t> aValue) {
  auto data = mWorkerThreadAccessible.Access();

  if (aValue) {
    JS_SetGCParameter(aCx, aKey, *aValue);
  } else {
    JS_ResetGCParameter(aCx, aKey);
  }

  for (uint32_t index = 0; index < data->mChildWorkers.Length(); index++) {
    data->mChildWorkers[index]->UpdateJSWorkerMemoryParameter(aKey, aValue);
  }
}

#ifdef JS_GC_ZEAL
void WorkerPrivate::UpdateGCZealInternal(JSContext* aCx, uint8_t aGCZeal,
                                         uint32_t aFrequency) {
  auto data = mWorkerThreadAccessible.Access();

  JS::SetGCZeal(aCx, aGCZeal, aFrequency);

  for (uint32_t index = 0; index < data->mChildWorkers.Length(); index++) {
    data->mChildWorkers[index]->UpdateGCZeal(aGCZeal, aFrequency);
  }
}
#endif

void WorkerPrivate::SetLowMemoryStateInternal(JSContext* aCx, bool aState) {
  auto data = mWorkerThreadAccessible.Access();

  JS::SetLowMemoryState(aCx, aState);

  for (uint32_t index = 0; index < data->mChildWorkers.Length(); index++) {
    data->mChildWorkers[index]->SetLowMemoryState(aState);
  }
}

void WorkerPrivate::SetCCCollectedAnything(bool collectedAnything) {
  mWorkerThreadAccessible.Access()->mCCCollectedAnything = collectedAnything;
}

uint32_t WorkerPrivate::GetCurrentTimerNestingLevel() const {
  auto data = mWorkerThreadAccessible.Access();

  auto* timeoutManager =
      data->mScope ? data->mScope->GetTimeoutManager() : nullptr;
  if (timeoutManager) {
    return timeoutManager->GetNestingLevelForWorker();
  }

  return data->mCurrentTimerNestingLevel;
}

bool WorkerPrivate::isLastCCCollectedAnything() {
  return mWorkerThreadAccessible.Access()->mCCCollectedAnything;
}

void WorkerPrivate::GarbageCollectInternal(JSContext* aCx, bool aShrinking,
                                           bool aCollectChildren) {
  // Perform GC followed by CC (the CC is triggered by
  // WorkerJSRuntime::CustomGCCallback at the end of the collection).

  auto data = mWorkerThreadAccessible.Access();

  if (!GlobalScope()) {
    // We haven't compiled anything yet. Just bail out.
    return;
  }

  if (aShrinking || aCollectChildren) {
    JS::PrepareForFullGC(aCx);

    if (aShrinking && mSyncLoopStack.IsEmpty()) {
      JS::NonIncrementalGC(aCx, JS::GCOptions::Shrink,
                           JS::GCReason::DOM_WORKER);

      // Check whether the CC collected anything and if so GC again. This is
      // necessary to collect all garbage.
      if (data->mCCCollectedAnything) {
        JS::NonIncrementalGC(aCx, JS::GCOptions::Normal,
                             JS::GCReason::DOM_WORKER);
      }

      if (!aCollectChildren) {
        LOG(WorkerLog(), ("Worker %p collected idle garbage\n", this));
      }
    } else {
      JS::NonIncrementalGC(aCx, JS::GCOptions::Normal,
                           JS::GCReason::DOM_WORKER);
      LOG(WorkerLog(), ("Worker %p collected garbage\n", this));
    }
  } else {
    JS_MaybeGC(aCx);
    LOG(WorkerLog(), ("Worker %p collected periodic garbage\n", this));
  }

  if (aCollectChildren) {
    for (uint32_t index = 0; index < data->mChildWorkers.Length(); index++) {
      data->mChildWorkers[index]->GarbageCollect(aShrinking);
    }
  }
}

void WorkerPrivate::CycleCollectInternal(bool aCollectChildren) {
  auto data = mWorkerThreadAccessible.Access();

  nsCycleCollector_collect(CCReason::WORKER, nullptr);

  if (aCollectChildren) {
    for (uint32_t index = 0; index < data->mChildWorkers.Length(); index++) {
      data->mChildWorkers[index]->CycleCollect();
    }
  }
}

void WorkerPrivate::MemoryPressureInternal() {
  auto data = mWorkerThreadAccessible.Access();

  if (data->mScope) {
    RefPtr<Console> console = data->mScope->GetConsoleIfExists();
    if (console) {
      console->ClearStorage();
    }

    RefPtr<Performance> performance = data->mScope->GetPerformanceIfExists();
    if (performance) {
      performance->MemoryPressure();
    }

    data->mScope->RemoveReportRecords();
  }

  if (data->mDebuggerScope) {
    RefPtr<Console> console = data->mDebuggerScope->GetConsoleIfExists();
    if (console) {
      console->ClearStorage();
    }
  }

  for (uint32_t index = 0; index < data->mChildWorkers.Length(); index++) {
    data->mChildWorkers[index]->MemoryPressure();
  }
}

void WorkerPrivate::SetThread(WorkerThread* aThread) {
  if (aThread) {
#ifdef DEBUG
    {
      bool isOnCurrentThread;
      MOZ_ASSERT(NS_SUCCEEDED(aThread->IsOnCurrentThread(&isOnCurrentThread)));
      MOZ_ASSERT(!isOnCurrentThread);
    }
#endif

    MOZ_ASSERT(!mPRThread);
    mPRThread = PRThreadFromThread(aThread);
    MOZ_ASSERT(mPRThread);

    mWorkerThreadAccessible.Transfer(mPRThread);
  } else {
    MOZ_ASSERT(mPRThread);
  }
}

void WorkerPrivate::SetWorkerPrivateInWorkerThread(
    WorkerThread* const aThread) {
  LOG(WorkerLog(),
      ("WorkerPrivate::SetWorkerPrivateInWorkerThread [%p]", this));
  MutexAutoLock lock(mMutex);

  MOZ_ASSERT(!mThread);
  MOZ_ASSERT(mStatus == Pending);

  mThread = aThread;
  mThread->SetWorker(WorkerThreadFriendKey{}, this);

  if (!mPreStartRunnables.IsEmpty()) {
    for (uint32_t index = 0; index < mPreStartRunnables.Length(); index++) {
      MOZ_ALWAYS_SUCCEEDS(mThread->DispatchAnyThread(
          WorkerThreadFriendKey{}, mPreStartRunnables[index]));
    }
    // Don't clear mPreStartRunnables here, it will be cleared in the beginning
    // of WorkerPrivate::DoRunLoop() or when in WorkerPrivate::RunLoopNeverRan()
  }
}

void WorkerPrivate::ResetWorkerPrivateInWorkerThread() {
  LOG(WorkerLog(),
      ("WorkerPrivate::ResetWorkerPrivateInWorkerThread [%p]", this));
  RefPtr<WorkerThread> doomedThread;

  // Release the mutex before doomedThread.
  MutexAutoLock lock(mMutex);
  MOZ_ASSERT(mStatus == Dead);

  MOZ_ASSERT(mThread);

  mThread->ClearEventQueueAndWorker(WorkerThreadFriendKey{});
  mThread.swap(doomedThread);
}

void WorkerPrivate::BeginCTypesCall() {
  AssertIsOnWorkerThread();
  auto data = mWorkerThreadAccessible.Access();

  // Don't try to GC while we're blocked in a ctypes call.
  SetGCTimerMode(NoTimer);

  data->mYieldJSThreadExecution.EmplaceBack();
}

void WorkerPrivate::EndCTypesCall() {
  AssertIsOnWorkerThread();
  auto data = mWorkerThreadAccessible.Access();

  data->mYieldJSThreadExecution.RemoveLastElement();

  // Make sure the periodic timer is running before we start running JS again.
  SetGCTimerMode(PeriodicTimer);
}

void WorkerPrivate::BeginCTypesCallback() {
  AssertIsOnWorkerThread();

  // Make sure the periodic timer is running before we start running JS again.
  SetGCTimerMode(PeriodicTimer);

  // Re-requesting execution is not needed since the JSRuntime code calling
  // this will do an AutoEntryScript.
}

void WorkerPrivate::EndCTypesCallback() {
  AssertIsOnWorkerThread();

  // Don't try to GC while we're blocked in a ctypes call.
  SetGCTimerMode(NoTimer);
}

bool WorkerPrivate::ConnectMessagePort(JSContext* aCx,
                                       UniqueMessagePortId& aIdentifier) {
  AssertIsOnWorkerThread();

  WorkerGlobalScope* globalScope = GlobalScope();

  JS::Rooted<JSObject*> jsGlobal(aCx, globalScope->GetWrapper());
  MOZ_ASSERT(jsGlobal);

  // This UniqueMessagePortId is used to create a new port, still connected
  // with the other one, but in the worker thread.
  ErrorResult rv;
  RefPtr<MessagePort> port = MessagePort::Create(globalScope, aIdentifier, rv);
  if (NS_WARN_IF(rv.Failed())) {
    rv.SuppressException();
    return false;
  }

  GlobalObject globalObject(aCx, jsGlobal);
  if (globalObject.Failed()) {
    return false;
  }

  RootedDictionary<MessageEventInit> init(aCx);
  init.mData = JS_GetEmptyStringValue(aCx);
  init.mBubbles = false;
  init.mCancelable = false;
  init.mSource.SetValue().SetAsMessagePort() = port;
  if (!init.mPorts.AppendElement(port.forget(), fallible)) {
    return false;
  }

  RefPtr<MessageEvent> event =
      MessageEvent::Constructor(globalObject, u"connect"_ns, init);

  event->SetTrusted(true);

  globalScope->DispatchEvent(*event);

  return true;
}

WorkerGlobalScope* WorkerPrivate::GetOrCreateGlobalScope(JSContext* aCx) {
  auto data = mWorkerThreadAccessible.Access();

  if (data->mScope) {
    return data->mScope;
  }

  if (IsSharedWorker()) {
    data->mScope =
        new SharedWorkerGlobalScope(this, CreateClientSource(), WorkerName());
  } else if (IsServiceWorker()) {
    data->mScope = new ServiceWorkerGlobalScope(
        this, CreateClientSource(), GetServiceWorkerRegistrationDescriptor());
  } else {
    data->mScope = new DedicatedWorkerGlobalScope(this, CreateClientSource(),
                                                  WorkerName());
  }

  JS::Rooted<JSObject*> global(aCx);
  NS_ENSURE_TRUE(data->mScope->WrapGlobalObject(aCx, &global), nullptr);

  JSAutoRealm ar(aCx, global);

  if (!RegisterBindings(aCx, global)) {
    data->mScope = nullptr;
    return nullptr;
  }

  // Worker has already in "Canceling", let the WorkerGlobalScope start dying.
  if (data->mCancelBeforeWorkerScopeConstructed) {
    data->mScope->NoteTerminating();
    data->mScope->DisconnectGlobalTeardownObservers();
  }

  JS_FireOnNewGlobalObject(aCx, global);

  return data->mScope;
}

WorkerDebuggerGlobalScope* WorkerPrivate::CreateDebuggerGlobalScope(
    JSContext* aCx) {
  auto data = mWorkerThreadAccessible.Access();
  MOZ_ASSERT(!data->mDebuggerScope);

  // The debugger global gets a dummy client, not the "real" client used by the
  // debugee worker.
  auto clientSource = ClientManager::CreateSource(
      GetClientType(), HybridEventTarget(), NullPrincipalInfo());

  data->mDebuggerScope =
      new WorkerDebuggerGlobalScope(this, std::move(clientSource));

  JS::Rooted<JSObject*> global(aCx);
  NS_ENSURE_TRUE(data->mDebuggerScope->WrapGlobalObject(aCx, &global), nullptr);

  JSAutoRealm ar(aCx, global);

  if (!RegisterDebuggerBindings(aCx, global)) {
    data->mDebuggerScope = nullptr;
    return nullptr;
  }

  JS_FireOnNewGlobalObject(aCx, global);

  return data->mDebuggerScope;
}

bool WorkerPrivate::IsOnWorkerThread() const {
  // We can't use mThread because it must be protected by mMutex and sometimes
  // this method is called when mMutex is already locked. This method should
  // always work.
  MOZ_ASSERT(mPRThread,
             "AssertIsOnWorkerThread() called before a thread was assigned!");

  return mPRThread == PR_GetCurrentThread();
}

#ifdef DEBUG
void WorkerPrivate::AssertIsOnWorkerThread() const {
  MOZ_ASSERT(IsOnWorkerThread());
}
#endif  // DEBUG

void WorkerPrivate::DumpCrashInformation(nsACString& aString) {
  auto data = mWorkerThreadAccessible.Access();

  aString.Append("IsChromeWorker(");
  if (IsChromeWorker()) {
    aString.Append(NS_ConvertUTF16toUTF8(ScriptURL()));
  } else {
    aString.Append("false");
  }
  aString.Append(")");
  for (const auto* workerRef : data->mWorkerRefs.NonObservingRange()) {
    if (workerRef->IsPreventingShutdown()) {
      aString.Append("|");
      aString.Append(workerRef->Name());
      const nsCString status = GET_WORKERREF_DEBUG_STATUS(workerRef);
      if (!status.IsEmpty()) {
        aString.Append("[");
        aString.Append(status);
        aString.Append("]");
      }
    }
  }
}

PerformanceStorage* WorkerPrivate::GetPerformanceStorage() {
  MOZ_ASSERT(mPerformanceStorage);
  return mPerformanceStorage;
}

bool WorkerPrivate::ShouldResistFingerprinting(RFPTarget aTarget) const {
  return mLoadInfo.mShouldResistFingerprinting &&
         nsRFPService::IsRFPEnabledFor(
             mLoadInfo.mOriginAttributes.IsPrivateBrowsing(), aTarget,
             mLoadInfo.mOverriddenFingerprintingSettings);
}

void WorkerPrivate::SetRemoteWorkerController(RemoteWorkerChild* aController) {
  AssertIsOnMainThread();
  MOZ_ASSERT(aController);
  MOZ_ASSERT(!mRemoteWorkerController);

  mRemoteWorkerController = aController;
}

RemoteWorkerChild* WorkerPrivate::GetRemoteWorkerController() {
  AssertIsOnMainThread();
  MOZ_ASSERT(mRemoteWorkerController);
  return mRemoteWorkerController;
}

RefPtr<GenericPromise> WorkerPrivate::SetServiceWorkerSkipWaitingFlag() {
  AssertIsOnWorkerThread();
  MOZ_ASSERT(IsServiceWorker());

  RefPtr<RemoteWorkerChild> rwc = mRemoteWorkerController;

  if (!rwc) {
    return GenericPromise::CreateAndReject(NS_ERROR_DOM_ABORT_ERR, __func__);
  }

  RefPtr<GenericPromise> promise =
      rwc->MaybeSendSetServiceWorkerSkipWaitingFlag();

  return promise;
}

const nsString& WorkerPrivate::Id() {
  if (mId.IsEmpty()) {
    mId = ComputeWorkerPrivateId();
  }

  MOZ_ASSERT(!mId.IsEmpty());

  return mId;
}

bool WorkerPrivate::IsSharedMemoryAllowed() const {
  if (StaticPrefs::
          dom_postMessage_sharedArrayBuffer_bypassCOOP_COEP_insecure_enabled()) {
    return true;
  }

  // Allow privileged addons to access shared memory.
  if (mIsPrivilegedAddonGlobal) {
    return true;
  }

  return CrossOriginIsolated();
}

bool WorkerPrivate::CrossOriginIsolated() const {
  if (!StaticPrefs::
          dom_postMessage_sharedArrayBuffer_withCOOP_COEP_AtStartup()) {
    return false;
  }

  return mAgentClusterOpenerPolicy ==
         nsILoadInfo::OPENER_POLICY_SAME_ORIGIN_EMBEDDER_POLICY_REQUIRE_CORP;
}

nsILoadInfo::CrossOriginEmbedderPolicy WorkerPrivate::GetEmbedderPolicy()
    const {
  if (!StaticPrefs::browser_tabs_remote_useCrossOriginEmbedderPolicy()) {
    return nsILoadInfo::EMBEDDER_POLICY_NULL;
  }

  return mEmbedderPolicy.valueOr(nsILoadInfo::EMBEDDER_POLICY_NULL);
}

Result<Ok, nsresult> WorkerPrivate::SetEmbedderPolicy(
    nsILoadInfo::CrossOriginEmbedderPolicy aPolicy) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mEmbedderPolicy.isNothing());

  if (!StaticPrefs::browser_tabs_remote_useCrossOriginEmbedderPolicy()) {
    return Ok();
  }

  // https://html.spec.whatwg.org/multipage/browsers.html#check-a-global-object's-embedder-policy
  // If ownerPolicy's value is not compatible with cross-origin isolation or
  // policy's value is compatible with cross-origin isolation, then return true.
  EnsureOwnerEmbedderPolicy();
  nsILoadInfo::CrossOriginEmbedderPolicy ownerPolicy =
      mOwnerEmbedderPolicy.valueOr(nsILoadInfo::EMBEDDER_POLICY_NULL);
  if (nsContentSecurityManager::IsCompatibleWithCrossOriginIsolation(
          ownerPolicy) &&
      !nsContentSecurityManager::IsCompatibleWithCrossOriginIsolation(
          aPolicy)) {
    return Err(NS_ERROR_BLOCKED_BY_POLICY);
  }

  mEmbedderPolicy.emplace(aPolicy);

  return Ok();
}

void WorkerPrivate::InheritOwnerEmbedderPolicyOrNull(nsIRequest* aRequest) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aRequest);

  EnsureOwnerEmbedderPolicy();

  if (mOwnerEmbedderPolicy.isSome()) {
    nsCOMPtr<nsIChannel> channel = do_QueryInterface(aRequest);
    MOZ_ASSERT(channel);

    nsCOMPtr<nsIURI> scriptURI;
    MOZ_ALWAYS_SUCCEEDS(channel->GetURI(getter_AddRefs(scriptURI)));

    bool isLocalScriptURI = false;
    MOZ_ALWAYS_SUCCEEDS(NS_URIChainHasFlags(
        scriptURI, nsIProtocolHandler::URI_IS_LOCAL_RESOURCE,
        &isLocalScriptURI));

    MOZ_RELEASE_ASSERT(isLocalScriptURI);
  }

  mEmbedderPolicy.emplace(
      mOwnerEmbedderPolicy.valueOr(nsILoadInfo::EMBEDDER_POLICY_NULL));
}

bool WorkerPrivate::MatchEmbedderPolicy(
    nsILoadInfo::CrossOriginEmbedderPolicy aPolicy) const {
  MOZ_ASSERT(NS_IsMainThread());

  if (!StaticPrefs::browser_tabs_remote_useCrossOriginEmbedderPolicy()) {
    return true;
  }

  return mEmbedderPolicy.value() == aPolicy;
}

nsILoadInfo::CrossOriginEmbedderPolicy WorkerPrivate::GetOwnerEmbedderPolicy()
    const {
  if (!StaticPrefs::browser_tabs_remote_useCrossOriginEmbedderPolicy()) {
    return nsILoadInfo::EMBEDDER_POLICY_NULL;
  }

  return mOwnerEmbedderPolicy.valueOr(nsILoadInfo::EMBEDDER_POLICY_NULL);
}

void WorkerPrivate::EnsureOwnerEmbedderPolicy() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mOwnerEmbedderPolicy.isNothing());

  if (GetParent()) {
    mOwnerEmbedderPolicy.emplace(GetParent()->GetEmbedderPolicy());
  } else if (GetWindow() && GetWindow()->GetWindowContext()) {
    mOwnerEmbedderPolicy.emplace(
        GetWindow()->GetWindowContext()->GetEmbedderPolicy());
  }
}

nsIPrincipal* WorkerPrivate::GetEffectiveStoragePrincipal() const {
  AssertIsOnWorkerThread();

  if (mLoadInfo.mUseRegularPrincipal) {
    return mLoadInfo.mPrincipal;
  }

  return mLoadInfo.mPartitionedPrincipal;
}

const mozilla::ipc::PrincipalInfo&
WorkerPrivate::GetEffectiveStoragePrincipalInfo() const {
  AssertIsOnWorkerThread();

  if (mLoadInfo.mUseRegularPrincipal) {
    return *mLoadInfo.mPrincipalInfo;
  }

  return *mLoadInfo.mPartitionedPrincipalInfo;
}

NS_IMPL_ADDREF(WorkerPrivate::EventTarget)
NS_IMPL_RELEASE(WorkerPrivate::EventTarget)

NS_INTERFACE_MAP_BEGIN(WorkerPrivate::EventTarget)
  NS_INTERFACE_MAP_ENTRY(nsISerialEventTarget)
  NS_INTERFACE_MAP_ENTRY(nsIEventTarget)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
#ifdef DEBUG
  // kDEBUGWorkerEventTargetIID is special in that it does not AddRef its
  // result.
  if (aIID.Equals(kDEBUGWorkerEventTargetIID)) {
    *aInstancePtr = this;
    return NS_OK;
  } else
#endif
NS_INTERFACE_MAP_END

NS_IMETHODIMP
WorkerPrivate::EventTarget::DispatchFromScript(nsIRunnable* aRunnable,
                                               uint32_t aFlags) {
  nsCOMPtr<nsIRunnable> event(aRunnable);
  return Dispatch(event.forget(), aFlags);
}

NS_IMETHODIMP
WorkerPrivate::EventTarget::Dispatch(already_AddRefed<nsIRunnable> aRunnable,
                                     uint32_t aFlags) {
  // May be called on any thread!
  nsCOMPtr<nsIRunnable> event(aRunnable);

  // Workers only support asynchronous dispatch for now.
  if (NS_WARN_IF(aFlags != NS_DISPATCH_NORMAL)) {
    return NS_ERROR_UNEXPECTED;
  }

  RefPtr<WorkerRunnable> workerRunnable;

  MutexAutoLock lock(mMutex);

  if (mDisabled) {
    NS_WARNING(
        "A runnable was posted to a worker that is already shutting "
        "down!");
    return NS_ERROR_UNEXPECTED;
  }

  MOZ_ASSERT(mWorkerPrivate);
  MOZ_ASSERT(mNestedEventTarget);

  if (event) {
    workerRunnable = mWorkerPrivate->MaybeWrapAsWorkerRunnable(event.forget());
  }

  nsresult rv =
      mWorkerPrivate->Dispatch(workerRunnable.forget(), mNestedEventTarget);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

NS_IMETHODIMP
WorkerPrivate::EventTarget::DelayedDispatch(already_AddRefed<nsIRunnable>,
                                            uint32_t)

{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WorkerPrivate::EventTarget::RegisterShutdownTask(nsITargetShutdownTask* aTask) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WorkerPrivate::EventTarget::UnregisterShutdownTask(
    nsITargetShutdownTask* aTask) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WorkerPrivate::EventTarget::IsOnCurrentThread(bool* aIsOnCurrentThread) {
  // May be called on any thread!

  MOZ_ASSERT(aIsOnCurrentThread);

  MutexAutoLock lock(mMutex);

  if (mShutdown) {
    NS_WARNING(
        "A worker's event target was used after the worker has shutdown!");
    return NS_ERROR_UNEXPECTED;
  }

  MOZ_ASSERT(mNestedEventTarget);

  *aIsOnCurrentThread = mNestedEventTarget->IsOnCurrentThread();
  return NS_OK;
}

NS_IMETHODIMP_(bool)
WorkerPrivate::EventTarget::IsOnCurrentThreadInfallible() {
  // May be called on any thread!

  MutexAutoLock lock(mMutex);

  if (mShutdown) {
    NS_WARNING(
        "A worker's event target was used after the worker has shutdown!");
    return false;
  }

  MOZ_ASSERT(mNestedEventTarget);

  return mNestedEventTarget->IsOnCurrentThread();
}

WorkerPrivate::AutoPushEventLoopGlobal::AutoPushEventLoopGlobal(
    WorkerPrivate* aWorkerPrivate, JSContext* aCx) {
  auto data = aWorkerPrivate->mWorkerThreadAccessible.Access();
  mOldEventLoopGlobal = std::move(data->mCurrentEventLoopGlobal);
  if (JSObject* global = JS::CurrentGlobalOrNull(aCx)) {
    data->mCurrentEventLoopGlobal = xpc::NativeGlobal(global);
  }
#ifdef DEBUG
  mNewEventLoopGlobal = data->mCurrentEventLoopGlobal;
#endif
}

WorkerPrivate::AutoPushEventLoopGlobal::~AutoPushEventLoopGlobal() {
  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
  // We are popping out the event loop global, WorkerPrivate is supposed to be
  // alive and in a valid status(Running or Canceling)
  MOZ_ASSERT(workerPrivate);
  auto data = workerPrivate->mWorkerThreadAccessible.Access();
#ifdef DEBUG
  // Saved event loop global should be matched.
  MOZ_ASSERT(data->mCurrentEventLoopGlobal == mNewEventLoopGlobal);
  mNewEventLoopGlobal = nullptr;
#endif
  data->mCurrentEventLoopGlobal = std::move(mOldEventLoopGlobal);
}

// -----------------------------------------------------------------------------
// AutoSyncLoopHolder

AutoSyncLoopHolder::AutoSyncLoopHolder(WorkerPrivate* aWorkerPrivate,
                                       WorkerStatus aFailStatus,
                                       const char* const aName)
    : mTarget(aWorkerPrivate->CreateNewSyncLoop(aFailStatus)),
      mIndex(aWorkerPrivate->mSyncLoopStack.Length() - 1) {
  aWorkerPrivate->AssertIsOnWorkerThread();
  LOGV(
      ("AutoSyncLoopHolder::AutoSyncLoopHolder [%p] creator: %s", this, aName));
  if (aFailStatus < Canceling) {
    mWorkerRef = StrongWorkerRef::Create(aWorkerPrivate, aName, [aName]() {
      // Do nothing with the shutdown callback here since we need to wait for
      // the underlying SyncLoop to complete by itself.
      LOGV(
          ("AutoSyncLoopHolder::AutoSyncLoopHolder Worker starts to shutdown "
           "with a AutoSyncLoopHolder(%s).",
           aName));
    });
  } else {
    LOGV(
        ("AutoSyncLoopHolder::AutoSyncLoopHolder [%p] Create "
         "AutoSyncLoopHolder(%s) while Worker is shutting down",
         this, aName));
    mWorkerRef = StrongWorkerRef::CreateForcibly(aWorkerPrivate, aName);
  }
  // mWorkerRef can be nullptr here.
}

AutoSyncLoopHolder::~AutoSyncLoopHolder() {
  if (mWorkerRef && mTarget) {
    mWorkerRef->Private()->AssertIsOnWorkerThread();
    mWorkerRef->Private()->StopSyncLoop(mTarget, NS_ERROR_FAILURE);
    mWorkerRef->Private()->DestroySyncLoop(mIndex);
  }
}

nsresult AutoSyncLoopHolder::Run() {
  if (mWorkerRef) {
    WorkerPrivate* workerPrivate = mWorkerRef->Private();
    MOZ_ASSERT(workerPrivate);

    workerPrivate->AssertIsOnWorkerThread();

    nsresult rv = workerPrivate->RunCurrentSyncLoop();

    // The sync loop is done, sync loop has already destroyed in the end of
    // WorkerPrivate::RunCurrentSyncLoop(). So, release mWorkerRef here to
    // avoid destroying sync loop again in the ~AutoSyncLoopHolder();
    mWorkerRef = nullptr;

    return rv;
  }
  return NS_OK;
}

nsISerialEventTarget* AutoSyncLoopHolder::GetSerialEventTarget() const {
  // This can be null if CreateNewSyncLoop() fails.
  return mTarget;
}

// -----------------------------------------------------------------------------
// WorkerParentRef
WorkerParentRef::WorkerParentRef(RefPtr<WorkerPrivate>& aWorkerPrivate)
    : mWorkerPrivate(aWorkerPrivate) {
  LOGV(("WorkerParentRef::WorkerParentRef [%p] aWorkerPrivate %p", this,
        aWorkerPrivate.get()));
  MOZ_ASSERT(mWorkerPrivate);
  mWorkerPrivate->AssertIsOnParentThread();
}

const RefPtr<WorkerPrivate>& WorkerParentRef::Private() const {
  if (mWorkerPrivate) {
    mWorkerPrivate->AssertIsOnParentThread();
  }
  return mWorkerPrivate;
}

void WorkerParentRef::DropWorkerPrivate() {
  LOGV(("WorkerParentRef::DropWorkerPrivate [%p]", this));
  if (mWorkerPrivate) {
    mWorkerPrivate->AssertIsOnParentThread();
    mWorkerPrivate = nullptr;
  }
}

WorkerParentRef::~WorkerParentRef() = default;

}  // namespace dom
}  // namespace mozilla
