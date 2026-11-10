/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ServiceWorkerOp.h"

#include <utility>

#include "ServiceWorkerOpPromise.h"
#include "ServiceWorkerShutdownState.h"
#include "js/Exception.h"  // JS::ExceptionStack, JS::StealPendingExceptionStack
#include "jsapi.h"
#include "mozilla/Assertions.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/OwningNonNull.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/Client.h"
#include "mozilla/dom/CookieStore.h"
#include "mozilla/dom/ExtendableCookieChangeEvent.h"
#include "mozilla/dom/ExtendableMessageEventBinding.h"
#include "mozilla/dom/FetchEventBinding.h"
#include "mozilla/dom/FetchEventOpProxyChild.h"
#include "mozilla/dom/InternalHeaders.h"
#include "mozilla/dom/InternalRequest.h"
#include "mozilla/dom/InternalResponse.h"
#include "mozilla/dom/Notification.h"
#include "mozilla/dom/NotificationEvent.h"
#include "mozilla/dom/NotificationEventBinding.h"
#include "mozilla/dom/PerformanceStorage.h"
#include "mozilla/dom/PerformanceTiming.h"
#include "mozilla/dom/PushEventBinding.h"
#include "mozilla/dom/PushSubscriptionChangeEvent.h"
#include "mozilla/dom/PushSubscriptionChangeEventBinding.h"
#include "mozilla/dom/RemoteWorkerChild.h"
#include "mozilla/dom/RemoteWorkerNonLifeCycleOpControllerChild.h"
#include "mozilla/dom/RemoteWorkerService.h"
#include "mozilla/dom/Request.h"
#include "mozilla/dom/Response.h"
#include "mozilla/dom/RootedDictionary.h"
#include "mozilla/dom/SafeRefPtr.h"
#include "mozilla/dom/ServiceWorker.h"
#include "mozilla/dom/ServiceWorkerBinding.h"
#include "mozilla/dom/ServiceWorkerGlobalScopeBinding.h"
#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/dom/WorkerRef.h"
#include "mozilla/dom/WorkerScope.h"
#include "mozilla/extensions/ExtensionBrowser.h"
#include "mozilla/ipc/IPCStreamUtils.h"
#include "nsCOMPtr.h"
#include "nsContentUtils.h"
#include "nsDebug.h"
#include "nsError.h"
#include "nsINamed.h"
#include "nsIPushErrorReporter.h"
#include "nsISupportsImpl.h"
#include "nsITimer.h"
#include "nsIURI.h"
#include "nsServiceManagerUtils.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"

namespace mozilla::dom {

using remoteworker::Canceled;
using remoteworker::Killed;
using remoteworker::Pending;
using remoteworker::Running;

namespace {

class ExtendableEventKeepAliveHandler final
    : public ExtendableEvent::ExtensionsHandler,
      public PromiseNativeHandler {
 public:
  NS_DECL_ISUPPORTS

  static RefPtr<ExtendableEventKeepAliveHandler> Create(
      RefPtr<ExtendableEventCallback> aCallback) {
    MOZ_ASSERT(IsCurrentThreadRunningWorker());

    RefPtr<ExtendableEventKeepAliveHandler> self =
        new ExtendableEventKeepAliveHandler(std::move(aCallback));

    self->mWorkerRef = StrongWorkerRef::Create(
        GetCurrentThreadWorkerPrivate(), "ExtendableEventKeepAliveHandler",
        [self]() { self->Cleanup(); });

    if (NS_WARN_IF(!self->mWorkerRef)) {
      return nullptr;
    }

    return self;
  }

  /**
   * ExtendableEvent::ExtensionsHandler interface
   */
  bool WaitOnPromise(Promise& aPromise) override {
    if (!mAcceptingPromises) {
      MOZ_ASSERT(!GetDispatchFlag());
      MOZ_ASSERT(!mSelfRef, "We shouldn't be holding a self reference!");
      return false;
    }

    if (!mSelfRef) {
      MOZ_ASSERT(!mPendingPromisesCount);
      mSelfRef = this;
    }

    ++mPendingPromisesCount;
    aPromise.AppendNativeHandler(this);

    return true;
  }

  /**
   * PromiseNativeHandler interface
   */
  void ResolvedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override {
    RemovePromise(Resolved);
  }

  void RejectedCallback(JSContext* aCx, JS::Handle<JS::Value> aValue,
                        ErrorResult& aRv) override {
    RemovePromise(Rejected);
  }

  void MaybeDone() {
    MOZ_ASSERT(IsCurrentThreadRunningWorker());
    MOZ_ASSERT(!GetDispatchFlag());

    if (mPendingPromisesCount) {
      return;
    }

    if (mCallback) {
      mCallback->FinishedWithResult(mRejected ? Rejected : Resolved);
      mCallback = nullptr;
    }

    Cleanup();
  }

 private:
  /**
   * This class is useful for the case where pending microtasks will continue
   * extending the event, which means that the event is not "done." For example:
   *
   * // `e` is an ExtendableEvent, `p` is a Promise
   * e.waitUntil(p);
   * p.then(() => e.waitUntil(otherPromise));
   */
  class MaybeDoneRunner : public MicroTaskRunnable {
   public:
    explicit MaybeDoneRunner(RefPtr<ExtendableEventKeepAliveHandler> aHandler)
        : mHandler(std::move(aHandler)) {}

    void Run(AutoSlowOperation& /* unused */) override {
      mHandler->MaybeDone();
    }

   private:
    RefPtr<ExtendableEventKeepAliveHandler> mHandler;
  };

  explicit ExtendableEventKeepAliveHandler(
      RefPtr<ExtendableEventCallback> aCallback)
      : mCallback(std::move(aCallback)) {}

  ~ExtendableEventKeepAliveHandler() { Cleanup(); }

  void Cleanup() {
    MOZ_ASSERT(IsCurrentThreadRunningWorker());

    if (mCallback) {
      mCallback->FinishedWithResult(Rejected);
    }

    mSelfRef = nullptr;
    mWorkerRef = nullptr;
    mCallback = nullptr;
    mAcceptingPromises = false;
  }

  void RemovePromise(ExtendableEventResult aResult) {
    MOZ_ASSERT(IsCurrentThreadRunningWorker());
    MOZ_DIAGNOSTIC_ASSERT(mPendingPromisesCount > 0);

    // NOTE: mSelfRef can be nullptr here if MaybeCleanup() was just called
    // before a promise settled. This can happen, for example, if the worker
    // thread is being terminated for running too long, browser shutdown, etc.

    mRejected |= (aResult == Rejected);

    --mPendingPromisesCount;
    if (mPendingPromisesCount || GetDispatchFlag()) {
      return;
    }

    CycleCollectedJSContext* cx = CycleCollectedJSContext::Get();
    MOZ_ASSERT(cx);

    RefPtr<MaybeDoneRunner> r = new MaybeDoneRunner(this);
    cx->DispatchToMicroTask(r.forget());
  }

  /**
   * We start holding a self reference when the first extension promise is
   * added, and this reference is released when the last promise settles or
   * when the worker is shutting down.
   *
   * This is needed in the case that we're waiting indefinitely on a to-be-GC'ed
   * promise that's no longer reachable and will never be settled.
   */
  RefPtr<ExtendableEventKeepAliveHandler> mSelfRef;

  RefPtr<StrongWorkerRef> mWorkerRef;

  RefPtr<ExtendableEventCallback> mCallback;

  uint32_t mPendingPromisesCount = 0;

  bool mRejected = false;
  bool mAcceptingPromises = true;
};

NS_IMPL_ISUPPORTS0(ExtendableEventKeepAliveHandler)

nsresult DispatchExtendableEventOnWorkerScope(
    JSContext* aCx, WorkerGlobalScope* aWorkerScope, ExtendableEvent* aEvent,
    RefPtr<ExtendableEventCallback> aCallback) {
  MOZ_ASSERT(aCx);
  MOZ_ASSERT(aWorkerScope);
  MOZ_ASSERT(aEvent);

  nsCOMPtr<nsIGlobalObject> globalObject = aWorkerScope;
  WidgetEvent* internalEvent = aEvent->WidgetEventPtr();

  RefPtr<ExtendableEventKeepAliveHandler> keepAliveHandler =
      ExtendableEventKeepAliveHandler::Create(std::move(aCallback));
  if (NS_WARN_IF(!keepAliveHandler)) {
    return NS_ERROR_FAILURE;
  }

  // This must be always set *before* dispatching the event, otherwise
  // waitUntil() calls will fail.
  aEvent->SetKeepAliveHandler(keepAliveHandler);

  ErrorResult result;
  aWorkerScope->DispatchEvent(*aEvent, result);
  if (NS_WARN_IF(result.Failed())) {
    result.SuppressException();
    return NS_ERROR_FAILURE;
  }

  keepAliveHandler->MaybeDone();

  // We don't block the event when getting an exception but still report the
  // error message. NOTE: this will not stop the event.
  if (internalEvent->mFlags.mExceptionWasRaised) {
    return NS_ERROR_XPC_JS_THREW_EXCEPTION;
  }

  return NS_OK;
}

bool DispatchFailed(nsresult aStatus) {
  return NS_FAILED(aStatus) && aStatus != NS_ERROR_XPC_JS_THREW_EXCEPTION;
}

}  // anonymous namespace

class ServiceWorkerOp::ServiceWorkerOpRunnable final
    : public WorkerDebuggeeRunnable {
 public:
  NS_DECL_ISUPPORTS_INHERITED

  ServiceWorkerOpRunnable(RefPtr<ServiceWorkerOp> aOwner,
                          WorkerPrivate* aWorkerPrivate)
      : WorkerDebuggeeRunnable("ServiceWorkerOpRunnable"),
        mOwner(std::move(aOwner)) {
    MOZ_ASSERT(mOwner);
    MOZ_ASSERT(aWorkerPrivate);
  }

 private:
  ~ServiceWorkerOpRunnable() = default;

  bool WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();
    MOZ_ASSERT(aWorkerPrivate->IsServiceWorker());
    MOZ_ASSERT(mOwner);

    // GlobalScope could be nullptr here that OOM issue causes GlobalScope
    // creation fail.
    if (!aWorkerPrivate->GlobalScope() ||
        aWorkerPrivate->GlobalScope()->IsDying()) {
      (void)Cancel();
      return true;
    }

    bool rv = mOwner->Exec(aCx, aWorkerPrivate);
    (void)NS_WARN_IF(!rv);
    mOwner = nullptr;

    return rv;
  }

  // Silent PreDispatch and PostDispatch, since ServiceWorkerOpRunnable can be
  // from the main thread or from the worker thread.
  bool PreDispatch(WorkerPrivate* WorkerPrivate) override { return true; }
  void PostDispatch(WorkerPrivate* WorkerPrivate,
                    bool aDispatchResult) override {}

  nsresult Cancel() override {
    MOZ_ASSERT(mOwner);

    mOwner->RejectAll(NS_ERROR_DOM_ABORT_ERR);
    mOwner = nullptr;

    return NS_OK;
  }

  RefPtr<ServiceWorkerOp> mOwner;
};

NS_IMPL_ISUPPORTS_INHERITED0(ServiceWorkerOp::ServiceWorkerOpRunnable,
                             WorkerThreadRunnable)

bool ServiceWorkerOp::MaybeStart(RemoteWorkerChild* aOwner,
                                 RemoteWorkerState& aState) {
  MOZ_ASSERT(!mStarted);
  MOZ_ASSERT(aOwner);
  MOZ_ASSERT(aOwner->GetActorEventTarget()->IsOnCurrentThread());

  auto launcherData = aOwner->mLauncherData.Access();

  if (NS_WARN_IF(!aOwner->CanSend())) {
    RejectAll(NS_ERROR_DOM_ABORT_ERR);
    mStarted = true;
    return true;
  }

  // Allow termination to happen while the Service Worker is initializing.
  if (aState.is<Pending>() && !IsTerminationOp()) {
    return false;
  }

  if (NS_WARN_IF(aState.is<Canceled>()) || NS_WARN_IF(aState.is<Killed>())) {
    RejectAll(NS_ERROR_DOM_INVALID_STATE_ERR);
    mStarted = true;
    return true;
  }

  MOZ_ASSERT(aState.is<Running>() || IsTerminationOp());

  RefPtr<ServiceWorkerOp> self = this;

  if (IsTerminationOp()) {
    aOwner->GetTerminationPromise()->Then(
        GetCurrentSerialEventTarget(), __func__,
        [self](
            const GenericNonExclusivePromise::ResolveOrRejectValue& aResult) {
          MaybeReportServiceWorkerShutdownProgress(self->mArgs, true);

          MOZ_ASSERT(!self->mPromiseHolder.IsEmpty());

          if (NS_WARN_IF(aResult.IsReject())) {
            self->mPromiseHolder.Reject(aResult.RejectValue(), __func__);
            return;
          }

          self->mPromiseHolder.Resolve(NS_OK, __func__);
        });
  }

  // NewRunnableMethod doesn't work here because the template does not appear to
  // be able to deal with the owner argument having storage as a RefPtr but
  // with the method taking a RefPtr&.
  RefPtr<RemoteWorkerChild> owner = aOwner;
  nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
      __func__, [self = std::move(self), owner = std::move(owner)]() mutable {
        self->StartOnMainThread(owner);
      });

  mStarted = true;

  MOZ_ALWAYS_SUCCEEDS(SchedulerGroup::Dispatch(r.forget()));
  return true;
}

void ServiceWorkerOp::StartOnMainThread(RefPtr<RemoteWorkerChild>& aOwner) {
  AssertIsOnMainThread();
  MaybeReportServiceWorkerShutdownProgress(mArgs);

  {
    auto lock = aOwner->mState.Lock();

    if (NS_WARN_IF(!lock->is<Running>() && !IsTerminationOp())) {
      RejectAll(NS_ERROR_DOM_INVALID_STATE_ERR);
      return;
    }
  }

  if (IsTerminationOp()) {
    aOwner->CloseWorkerOnMainThread();
  } else {
    auto lock = aOwner->mState.Lock();
    MOZ_ASSERT(lock->is<Running>());

    RefPtr<WorkerThreadRunnable> workerRunnable =
        GetRunnable(lock->as<Running>().mWorkerPrivate);

    if (NS_WARN_IF(
            !workerRunnable->Dispatch(lock->as<Running>().mWorkerPrivate))) {
      RejectAll(NS_ERROR_FAILURE);
    }
  }
}

void ServiceWorkerOp::Start(RemoteWorkerNonLifeCycleOpControllerChild* aOwner,
                            RemoteWorkerState& aState) {
  MOZ_ASSERT(!mStarted);
  MOZ_ASSERT(aOwner);

  if (NS_WARN_IF(!aOwner->CanSend())) {
    RejectAll(NS_ERROR_DOM_ABORT_ERR);
    mStarted = true;
    return;
  }

  // NonLifeCycle related operations would never start at Pending state.
  MOZ_ASSERT(!aState.is<Pending>());

  if (NS_WARN_IF(aState.is<Canceled>()) || NS_WARN_IF(aState.is<Killed>())) {
    RejectAll(NS_ERROR_DOM_INVALID_STATE_ERR);
    mStarted = true;
    return;
  }

  MOZ_ASSERT(aState.is<Running>());

  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();

  MOZ_ASSERT_DEBUG_OR_FUZZING(workerPrivate);

  RefPtr<WorkerThreadRunnable> workerRunnable = GetRunnable(workerPrivate);

  if (NS_WARN_IF(!workerRunnable->Dispatch(workerPrivate))) {
    RejectAll(NS_ERROR_FAILURE);
  }

  mStarted = true;
}

void ServiceWorkerOp::Cancel() { RejectAll(NS_ERROR_DOM_ABORT_ERR); }

ServiceWorkerOp::ServiceWorkerOp(
    ServiceWorkerOpArgs&& aArgs,
    std::function<void(const ServiceWorkerOpResult&)>&& aCallback)
    : mArgs(std::move(aArgs)) {
  // Can be on the Worker Launcher thread for Terminate operations or on the
  // Worker thread for other operations
  RefPtr<ServiceWorkerOpPromise> promise = mPromiseHolder.Ensure(__func__);

  promise->Then(
      GetCurrentSerialEventTarget(), __func__,
      [callback = std::move(aCallback)](
          ServiceWorkerOpPromise::ResolveOrRejectValue&& aResult) mutable {
        if (NS_WARN_IF(aResult.IsReject())) {
          MOZ_ASSERT(NS_FAILED(aResult.RejectValue()));
          callback(aResult.RejectValue());
          return;
        }

        callback(aResult.ResolveValue());
      });
}

ServiceWorkerOp::~ServiceWorkerOp() {
  (void)NS_WARN_IF(!mPromiseHolder.IsEmpty());
  mPromiseHolder.RejectIfExists(NS_ERROR_DOM_ABORT_ERR, __func__);
}

bool ServiceWorkerOp::Started() const {
  MOZ_ASSERT(RemoteWorkerService::Thread()->IsOnCurrentThread());
  return mStarted;
}

bool ServiceWorkerOp::IsTerminationOp() const {
  return mArgs.type() ==
         ServiceWorkerOpArgs::TServiceWorkerTerminateWorkerOpArgs;
}

RefPtr<WorkerThreadRunnable> ServiceWorkerOp::GetRunnable(
    WorkerPrivate* aWorkerPrivate) {
  MOZ_ASSERT(aWorkerPrivate);

  return new ServiceWorkerOpRunnable(this, aWorkerPrivate);
}

void ServiceWorkerOp::RejectAll(nsresult aStatus) {
  MOZ_ASSERT(!mPromiseHolder.IsEmpty());
  mPromiseHolder.Reject(aStatus, __func__);
}

class CheckScriptEvaluationOp final : public ServiceWorkerOp {
  using ServiceWorkerOp::ServiceWorkerOp;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(CheckScriptEvaluationOp, override)

 private:
  ~CheckScriptEvaluationOp() = default;

  bool Exec(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();
    MOZ_ASSERT(aWorkerPrivate->IsServiceWorker());
    MOZ_ASSERT(!mPromiseHolder.IsEmpty());

    ServiceWorkerCheckScriptEvaluationOpResult result;
    result.workerScriptExecutedSuccessfully() =
        aWorkerPrivate->WorkerScriptExecutedSuccessfully();
    result.fetchHandlerWasAdded() = aWorkerPrivate->FetchHandlerWasAdded();

    mPromiseHolder.Resolve(result, __func__);

    return true;
  }
};

class TerminateServiceWorkerOp final : public ServiceWorkerOp {
  using ServiceWorkerOp::ServiceWorkerOp;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(TerminateServiceWorkerOp, override)

 private:
  ~TerminateServiceWorkerOp() = default;

  bool Exec(JSContext*, WorkerPrivate*) override {
    MOZ_ASSERT_UNREACHABLE(
        "Worker termination should be handled in "
        "`ServiceWorkerOp::MaybeStart()`");

    return false;
  }
};

class UpdateServiceWorkerStateOp final : public ServiceWorkerOp {
  using ServiceWorkerOp::ServiceWorkerOp;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(UpdateServiceWorkerStateOp, override);

 private:
  class UpdateStateOpRunnable final : public WorkerControlRunnable {
   public:
    NS_DECL_ISUPPORTS_INHERITED

    UpdateStateOpRunnable(RefPtr<UpdateServiceWorkerStateOp> aOwner,
                          WorkerPrivate* aWorkerPrivate)
        : WorkerControlRunnable("UpdateStateOpRunnable"),
          mOwner(std::move(aOwner)) {
      MOZ_ASSERT(mOwner);
      MOZ_ASSERT(aWorkerPrivate);
      aWorkerPrivate->AssertIsOnWorkerThread();
    }

    virtual bool PreDispatch(WorkerPrivate* aWorkerPrivate) override {
      aWorkerPrivate->AssertIsOnWorkerThread();
      return true;
    }

    virtual void PostDispatch(WorkerPrivate* aWorkerPrivate,
                              bool aDispatchResult) override {
      aWorkerPrivate->AssertIsOnWorkerThread();
    }

   private:
    ~UpdateStateOpRunnable() = default;

    bool WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
      MOZ_ASSERT(aWorkerPrivate);
      aWorkerPrivate->AssertIsOnWorkerThread();
      MOZ_ASSERT(aWorkerPrivate->IsServiceWorker());

      if (mOwner) {
        (void)mOwner->Exec(aCx, aWorkerPrivate);
        mOwner = nullptr;
      }

      return true;
    }

    nsresult Cancel() override {
      MOZ_ASSERT(mOwner);

      mOwner->RejectAll(NS_ERROR_DOM_ABORT_ERR);
      mOwner = nullptr;

      return NS_OK;
    }

    RefPtr<UpdateServiceWorkerStateOp> mOwner;
  };

  ~UpdateServiceWorkerStateOp() = default;

  RefPtr<WorkerThreadRunnable> GetRunnable(
      WorkerPrivate* aWorkerPrivate) override {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->IsOnWorkerThread();
    MOZ_ASSERT(mArgs.type() ==
               ServiceWorkerOpArgs::TServiceWorkerUpdateStateOpArgs);

    return new UpdateStateOpRunnable(this, aWorkerPrivate);
  }

  bool Exec(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();
    MOZ_ASSERT(aWorkerPrivate->IsServiceWorker());
    MOZ_ASSERT(!mPromiseHolder.IsEmpty());

    ServiceWorkerState state =
        mArgs.get_ServiceWorkerUpdateStateOpArgs().state();
    aWorkerPrivate->UpdateServiceWorkerState(state);

    mPromiseHolder.Resolve(NS_OK, __func__);

    return true;
  }
};

NS_IMPL_ISUPPORTS_INHERITED0(UpdateServiceWorkerStateOp::UpdateStateOpRunnable,
                             WorkerControlRunnable)

void ExtendableEventOp::FinishedWithResult(ExtendableEventResult aResult) {
  MOZ_ASSERT(IsCurrentThreadRunningWorker());
  MOZ_ASSERT(!mPromiseHolder.IsEmpty());

  mPromiseHolder.Resolve(aResult == Resolved ? NS_OK : NS_ERROR_FAILURE,
                         __func__);
}

class LifeCycleEventOp final : public ExtendableEventOp {
  using ExtendableEventOp::ExtendableEventOp;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(LifeCycleEventOp, override)

 private:
  ~LifeCycleEventOp() = default;

  bool Exec(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();
    MOZ_ASSERT(aWorkerPrivate->IsServiceWorker());
    MOZ_ASSERT(!mPromiseHolder.IsEmpty());

    RefPtr<ExtendableEvent> event;
    RefPtr<EventTarget> target = aWorkerPrivate->GlobalScope();

    const nsString& eventName =
        mArgs.get_ServiceWorkerLifeCycleEventOpArgs().eventName();

    if (eventName.EqualsASCII("install") || eventName.EqualsASCII("activate")) {
      ExtendableEventInit init;
      init.mBubbles = false;
      init.mCancelable = false;
      event = ExtendableEvent::Constructor(target, eventName, init);
    } else {
      MOZ_CRASH("Unexpected lifecycle event");
    }

    event->SetTrusted(true);

    nsresult rv = DispatchExtendableEventOnWorkerScope(
        aCx, aWorkerPrivate->GlobalScope(), event, this);

    if (NS_WARN_IF(DispatchFailed(rv))) {
      RejectAll(rv);
    }

    return !DispatchFailed(rv);
  }
};

class CookieChangeEventOp final : public ExtendableEventOp {
  using ExtendableEventOp::ExtendableEventOp;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(CookieChangeEventOp, override)

 private:
  ~CookieChangeEventOp() = default;

  bool Exec(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();
    MOZ_ASSERT(aWorkerPrivate->IsServiceWorker());
    MOZ_ASSERT(!mPromiseHolder.IsEmpty());

    const ServiceWorkerCookieChangeEventOpArgs& args =
        mArgs.get_ServiceWorkerCookieChangeEventOpArgs();

    CookieListItem item;
    CookieStore::CookieStructToItem(args.cookie(), &item);

    GlobalObject globalObj(aCx, aWorkerPrivate->GlobalScope()->GetWrapper());
    nsCOMPtr<EventTarget> eventTarget =
        do_QueryInterface(globalObj.GetAsSupports());
    MOZ_ASSERT(eventTarget);

    RefPtr<ExtendableCookieChangeEvent> event;

    if (args.deleted()) {
      item.mValue.Reset();
      event = ExtendableCookieChangeEvent::CreateForDeletedCookie(eventTarget,
                                                                  item);
    } else {
      event = ExtendableCookieChangeEvent::CreateForChangedCookie(eventTarget,
                                                                  item);
    }

    MOZ_ASSERT(event);

    nsresult rv = DispatchExtendableEventOnWorkerScope(
        aCx, aWorkerPrivate->GlobalScope(), event, this);

    if (NS_WARN_IF(NS_FAILED(rv))) {
      return false;
    }

    return true;
  }
};

/**
 * PushEventOp
 */
class PushEventOp final : public ExtendableEventOp {
  using ExtendableEventOp::ExtendableEventOp;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(PushEventOp, override)

 private:
  ~PushEventOp() = default;

  bool Exec(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();
    MOZ_ASSERT(aWorkerPrivate->IsServiceWorker());
    MOZ_ASSERT(!mPromiseHolder.IsEmpty());

    ErrorResult result;

    auto scopeExit = MakeScopeExit([&] {
      MOZ_ASSERT(result.Failed());

      RejectAll(result.StealNSResult());
      ReportError(aWorkerPrivate);
    });

    const ServiceWorkerPushEventOpArgs& args =
        mArgs.get_ServiceWorkerPushEventOpArgs();

    RootedDictionary<PushEventInit> pushEventInit(aCx);

    if (args.data().type() != OptionalPushData::Tvoid_t) {
      const auto& bytes = args.data().get_ArrayOfuint8_t();
      JSObject* data = Uint8Array::Create(aCx, bytes, result);

      // The ScopeExit above will deal with the exceptions (through
      // StealNSResult).
      result.WouldReportJSException();
      if (result.Failed()) {
        return false;
      }

      DebugOnly<bool> inited =
          pushEventInit.mData.Construct().SetAsArrayBufferView().Init(data);
      MOZ_ASSERT(inited);
    }

    pushEventInit.mBubbles = false;
    pushEventInit.mCancelable = false;

    GlobalObject globalObj(aCx, aWorkerPrivate->GlobalScope()->GetWrapper());
    RefPtr<PushEvent> pushEvent =
        PushEvent::Constructor(globalObj, u"push"_ns, pushEventInit, result);

    if (NS_WARN_IF(result.Failed())) {
      return false;
    }

    pushEvent->SetTrusted(true);

    scopeExit.release();

    nsresult rv = DispatchExtendableEventOnWorkerScope(
        aCx, aWorkerPrivate->GlobalScope(), pushEvent, this);

    if (NS_FAILED(rv)) {
      if (NS_WARN_IF(DispatchFailed(rv))) {
        RejectAll(rv);
      }

      // We don't cancel WorkerPrivate when catching an exception.
      ReportError(aWorkerPrivate,
                  nsIPushErrorReporter::DELIVERY_UNCAUGHT_EXCEPTION);
    }

    return !DispatchFailed(rv);
  }

  void FinishedWithResult(ExtendableEventResult aResult) override {
    MOZ_ASSERT(IsCurrentThreadRunningWorker());

    WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();

    if (aResult == Rejected) {
      ReportError(workerPrivate,
                  nsIPushErrorReporter::DELIVERY_UNHANDLED_REJECTION);
    }

    ExtendableEventOp::FinishedWithResult(aResult);
  }

  void ReportError(
      WorkerPrivate* aWorkerPrivate,
      uint16_t aError = nsIPushErrorReporter::DELIVERY_INTERNAL_ERROR) {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();
    MOZ_ASSERT(aWorkerPrivate->IsServiceWorker());

    if (NS_WARN_IF(aError > nsIPushErrorReporter::DELIVERY_INTERNAL_ERROR) ||
        mArgs.get_ServiceWorkerPushEventOpArgs().messageId().IsEmpty()) {
      return;
    }

    nsString messageId = mArgs.get_ServiceWorkerPushEventOpArgs().messageId();
    nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
        __func__, [messageId = std::move(messageId), error = aError] {
          nsCOMPtr<nsIPushErrorReporter> reporter =
              do_GetService("@mozilla.org/push/Service;1");

          if (reporter) {
            nsresult rv = reporter->ReportDeliveryError(messageId, error);
            (void)NS_WARN_IF(NS_FAILED(rv));
          }
        });

    MOZ_ALWAYS_SUCCEEDS(aWorkerPrivate->DispatchToMainThread(r.forget()));
  }
};

class PushSubscriptionChangeEventOp final : public ExtendableEventOp {
  using ExtendableEventOp::ExtendableEventOp;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(PushSubscriptionChangeEventOp, override)

 private:
  ~PushSubscriptionChangeEventOp() = default;

  bool Exec(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();
    MOZ_ASSERT(aWorkerPrivate->IsServiceWorker());
    MOZ_ASSERT(!mPromiseHolder.IsEmpty());

    RefPtr<EventTarget> target = aWorkerPrivate->GlobalScope();

    ServiceWorkerPushSubscriptionChangeEventOpArgs& args =
        mArgs.get_ServiceWorkerPushSubscriptionChangeEventOpArgs();

    PushSubscriptionChangeEventInit init;
    init.mBubbles = false;
    init.mCancelable = false;

    if (args.oldSubscription()) {
      PushSubscriptionData oldSubscriptionData =
          args.oldSubscription().extract();
      RefPtr<PushSubscription> oldSubscription = new PushSubscription(
          target->GetParentObject(), oldSubscriptionData.endpoint(), u""_ns,
          Nullable<EpochTimeStamp>(),
          std::move(oldSubscriptionData.rawP256dhKey()),
          std::move(oldSubscriptionData.authSecret()),
          std::move(oldSubscriptionData.appServerKey()));
      init.mOldSubscription = oldSubscription.forget();
    }

    RefPtr<PushSubscriptionChangeEvent> event =
        PushSubscriptionChangeEvent::Constructor(
            target, u"pushsubscriptionchange"_ns, init);
    event->SetTrusted(true);

    nsresult rv = DispatchExtendableEventOnWorkerScope(
        aCx, aWorkerPrivate->GlobalScope(), event, this);

    if (NS_WARN_IF(DispatchFailed(rv))) {
      RejectAll(rv);
    }

    return !DispatchFailed(rv);
  }
};

class NotificationEventOp : public ExtendableEventOp,
                            public nsITimerCallback,
                            public nsINamed {
  using ExtendableEventOp::ExtendableEventOp;

 public:
  NS_DECL_THREADSAFE_ISUPPORTS

 private:
  ~NotificationEventOp() {
    MOZ_DIAGNOSTIC_ASSERT(!mTimer);
    MOZ_DIAGNOSTIC_ASSERT(!mWorkerRef);
  }

  void ClearWindowAllowed(WorkerPrivate* aWorkerPrivate) {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();
    MOZ_ASSERT(aWorkerPrivate->IsServiceWorker());

    if (!mTimer) {
      return;
    }

    // This might be executed after the global was unrooted, in which case
    // GlobalScope() will return null. Making the check here just to be safe.
    WorkerGlobalScope* globalScope = aWorkerPrivate->GlobalScope();
    if (!globalScope) {
      return;
    }

    globalScope->ConsumeWindowInteraction();
    mTimer->Cancel();
    mTimer = nullptr;

    mWorkerRef = nullptr;
  }

  void StartClearWindowTimer(WorkerPrivate* aWorkerPrivate) {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();
    MOZ_ASSERT(!mTimer);

    nsresult rv;
    nsCOMPtr<nsITimer> timer =
        NS_NewTimer(aWorkerPrivate->ControlEventTarget());
    if (NS_WARN_IF(!timer)) {
      return;
    }

    MOZ_ASSERT(!mWorkerRef);
    RefPtr<NotificationEventOp> self = this;
    mWorkerRef = StrongWorkerRef::Create(
        aWorkerPrivate, "NotificationEventOp", [self = std::move(self)] {
          // We could try to hold the worker alive until the timer fires, but
          // other APIs are not likely to work in this partially shutdown state.
          // We might as well let the worker thread exit.
          self->ClearWindowAllowed(self->mWorkerRef->Private());
        });

    if (!mWorkerRef) {
      return;
    }

    aWorkerPrivate->GlobalScope()->AllowWindowInteraction();
    timer.swap(mTimer);

    // We swap first and then initialize the timer so that even if initializing
    // fails, we still clean the interaction count correctly.
    uint32_t delay =
        StaticPrefs::dom_webnotifications_disable_open_click_delay();
    rv = mTimer->InitWithCallback(this, delay, nsITimer::TYPE_ONE_SHOT);

    if (NS_WARN_IF(NS_FAILED(rv))) {
      ClearWindowAllowed(aWorkerPrivate);
      return;
    }
  }

  // ExtendableEventOp interface
  bool Exec(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();
    MOZ_ASSERT(aWorkerPrivate->IsServiceWorker());
    MOZ_ASSERT(!mPromiseHolder.IsEmpty());

    RefPtr<EventTarget> target = aWorkerPrivate->GlobalScope();

    ServiceWorkerNotificationEventOpArgs& args =
        mArgs.get_ServiceWorkerNotificationEventOpArgs();
    bool isClick = args.type() ==
                   ServiceWorkerNotificationEventOpArgs::
                       TServiceWorkerNotificationClickEventOpArgs;
    const IPCNotification& notification =
        args.type() == ServiceWorkerNotificationEventOpArgs::
                           TServiceWorkerNotificationClickEventOpArgs
            ? args.get_ServiceWorkerNotificationClickEventOpArgs()
                  .notification()
            : args.get_ServiceWorkerNotificationCloseEventOpArgs()
                  .notification();

    auto result = Notification::ConstructFromIPC(
        aWorkerPrivate->GlobalScope(), notification,
        NS_ConvertUTF8toUTF16(aWorkerPrivate->ServiceWorkerScope()));

    if (NS_WARN_IF(result.isErr())) {
      return false;
    }

    NotificationEventInit init;
    init.mNotification = result.unwrap();
    init.mBubbles = false;
    init.mCancelable = false;
    if (isClick) {
      init.mAction =
          args.get_ServiceWorkerNotificationClickEventOpArgs().action();
    }

    RefPtr<NotificationEvent> notificationEvent =
        NotificationEvent::Constructor(
            target, isClick ? u"notificationclick"_ns : u"notificationclose"_ns,
            init);

    notificationEvent->SetTrusted(true);

    if (isClick) {
      StartClearWindowTimer(aWorkerPrivate);
    }

    nsresult rv = DispatchExtendableEventOnWorkerScope(
        aCx, aWorkerPrivate->GlobalScope(), notificationEvent, this);

    if (NS_WARN_IF(DispatchFailed(rv))) {
      // This will reject mPromiseHolder.
      FinishedWithResult(Rejected);
    }

    return !DispatchFailed(rv);
  }

  void FinishedWithResult(ExtendableEventResult aResult) override {
    MOZ_ASSERT(IsCurrentThreadRunningWorker());

    WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
    MOZ_ASSERT(workerPrivate);

    ClearWindowAllowed(workerPrivate);

    ExtendableEventOp::FinishedWithResult(aResult);
  }

  // nsITimerCallback interface
  NS_IMETHOD Notify(nsITimer* aTimer) override {
    MOZ_DIAGNOSTIC_ASSERT(mTimer == aTimer);
    WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();
    ClearWindowAllowed(workerPrivate);
    return NS_OK;
  }

  // nsINamed interface
  NS_IMETHOD GetName(nsACString& aName) override {
    aName.AssignLiteral("NotificationEventOp");
    return NS_OK;
  }

  nsCOMPtr<nsITimer> mTimer;
  RefPtr<StrongWorkerRef> mWorkerRef;
};

NS_IMPL_ISUPPORTS(NotificationEventOp, nsITimerCallback, nsINamed)

class MessageEventOp final : public ExtendableEventOp {
  using ExtendableEventOp::ExtendableEventOp;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MessageEventOp, override)

  MessageEventOp(ServiceWorkerOpArgs&& aArgs,
                 std::function<void(const ServiceWorkerOpResult&)>&& aCallback)
      : ExtendableEventOp(std::move(aArgs), std::move(aCallback)) {}

 private:
  ~MessageEventOp() = default;

  bool Exec(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();
    MOZ_ASSERT(aWorkerPrivate->IsServiceWorker());
    MOZ_ASSERT(!mPromiseHolder.IsEmpty());

    ServiceWorkerMessageEventOpArgs& args =
        mArgs.get_ServiceWorkerMessageEventOpArgs();

    JS::Rooted<JS::Value> messageData(aCx);
    nsCOMPtr<nsIGlobalObject> sgo = aWorkerPrivate->GlobalScope();
    ErrorResult rv;

    if (args.clonedData()) {
      args.clonedData()->Read(aCx, &messageData, rv);
    }

    // If mData is an error message data, then it means that it failed to
    // serialize on the caller side because it contains a shared memory object.
    // If deserialization fails, we will fire a messageerror event.
    const bool deserializationFailed = rv.Failed() || !args.clonedData();

    Sequence<OwningNonNull<MessagePort>> ports;
    if (args.clonedData() &&
        !args.clonedData()->TakeTransferredPortsAsSequence(ports)) {
      RejectAll(NS_ERROR_FAILURE);
      rv.SuppressException();
      return false;
    }

    RootedDictionary<ExtendableMessageEventInit> init(aCx);

    init.mBubbles = false;
    init.mCancelable = false;

    // On a messageerror event, we disregard ports:
    // https://w3c.github.io/ServiceWorker/#service-worker-postmessage
    if (!deserializationFailed) {
      init.mData = messageData;
      init.mPorts = std::move(ports);
    }

    PostMessageSource& ipcSource = args.source();
    nsCString originSource;
    switch (ipcSource.type()) {
      case PostMessageSource::TClientInfoAndState:
        originSource = ipcSource.get_ClientInfoAndState().info().url();
        init.mSource.SetValue().SetAsClient() =
            new Client(sgo, ipcSource.get_ClientInfoAndState());
        break;
      case PostMessageSource::TIPCServiceWorkerDescriptor:
        originSource = ipcSource.get_IPCServiceWorkerDescriptor().scriptURL();
        init.mSource.SetValue().SetAsServiceWorker() = ServiceWorker::Create(
            sgo, ServiceWorkerDescriptor(
                     ipcSource.get_IPCServiceWorkerDescriptor()));
        break;
      default:
        MOZ_ASSERT_UNREACHABLE("Unexpected source type");
        return false;
    }

    nsCOMPtr<nsIURI> url;
    nsresult result = NS_NewURI(getter_AddRefs(url), originSource);
    if (NS_WARN_IF(NS_FAILED(result))) {
      RejectAll(result);
      rv.SuppressException();
      return false;
    }

    OriginAttributes attrs;
    nsCOMPtr<nsIPrincipal> principal =
        BasePrincipal::CreateContentPrincipal(url, attrs);
    if (!principal) {
      return false;
    }

    nsCString origin;
    result = principal->GetOriginNoSuffix(origin);
    if (NS_WARN_IF(NS_FAILED(result))) {
      RejectAll(result);
      rv.SuppressException();
      return false;
    }

    CopyUTF8toUTF16(origin, init.mOrigin);

    rv.SuppressException();
    RefPtr<EventTarget> target = aWorkerPrivate->GlobalScope();
    RefPtr<ExtendableMessageEvent> extendableEvent =
        ExtendableMessageEvent::Constructor(
            target, deserializationFailed ? u"messageerror"_ns : u"message"_ns,
            init);

    extendableEvent->SetTrusted(true);

    nsresult rv2 = DispatchExtendableEventOnWorkerScope(
        aCx, aWorkerPrivate->GlobalScope(), extendableEvent, this);

    if (NS_WARN_IF(DispatchFailed(rv2))) {
      RejectAll(rv2);
    }

    return !DispatchFailed(rv2);
  }
};

class UpdateIsOnContentBlockingAllowListOp final : public ExtendableEventOp {
  using ExtendableEventOp::ExtendableEventOp;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(UpdateIsOnContentBlockingAllowListOp,
                                        override);

 private:
  ~UpdateIsOnContentBlockingAllowListOp() = default;

  bool Exec(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();
    MOZ_ASSERT(aWorkerPrivate->IsServiceWorker());
    MOZ_ASSERT(!mPromiseHolder.IsEmpty());

    bool onContentBlockingAllowList =
        mArgs.get_ServiceWorkerUpdateIsOnContentBlockingAllowListOpArgs()
            .onContentBlockingAllowList();
    aWorkerPrivate->UpdateIsOnContentBlockingAllowList(
        onContentBlockingAllowList);

    return true;
  }
};

/**
 * Used for ScopeExit-style network request cancelation in
 * `ResolvedCallback()` (e.g. if `FetchEvent::RespondWith()` is resolved with
 * a non-JS object).
 */
class MOZ_STACK_CLASS FetchEventOp::AutoCancel {
 public:
  explicit AutoCancel(FetchEventOp* aOwner)
      : mOwner(aOwner),
        mLine(0),
        mColumn(0),
        mMessageName("InterceptionFailedWithURL"_ns) {
    MOZ_ASSERT(IsCurrentThreadRunningWorker());
    MOZ_ASSERT(mOwner);

    nsAutoString requestURL;
    mOwner->GetRequestURL(requestURL);
    mParams.AppendElement(requestURL);
  }

  ~AutoCancel() {
    if (mOwner) {
      if (mSourceSpec.IsEmpty()) {
        mOwner->AsyncLog(mMessageName, std::move(mParams));
      } else {
        mOwner->AsyncLog(mSourceSpec, mLine, mColumn, mMessageName,
                         std::move(mParams));
      }

      MOZ_ASSERT(!mOwner->mRespondWithPromiseHolder.IsEmpty());
      mOwner->mHandled->MaybeRejectWithNetworkError("AutoCancel"_ns);
      mOwner->mRespondWithPromiseHolder.Reject(
          CancelInterceptionArgs(
              NS_ERROR_INTERCEPTION_FAILED,
              FetchEventTimeStamps(mOwner->mFetchHandlerStart,
                                   mOwner->mFetchHandlerFinish)),
          __func__);
    }
  }

  // This function steals the error message from a ErrorResult.
  void SetCancelErrorResult(JSContext* aCx, ErrorResult& aRv) {
    MOZ_DIAGNOSTIC_ASSERT(aRv.Failed());
    MOZ_DIAGNOSTIC_ASSERT(!JS_IsExceptionPending(aCx));

    // Storing the error as exception in the JSContext.
    if (!aRv.MaybeSetPendingException(aCx)) {
      return;
    }

    MOZ_ASSERT(!aRv.Failed());

    // Let's take the pending exception.
    JS::ExceptionStack exnStack(aCx);
    if (!JS::StealPendingExceptionStack(aCx, &exnStack)) {
      return;
    }

    // Converting the exception in a JS::ErrorReportBuilder.
    JS::ErrorReportBuilder report(aCx);
    if (!report.init(aCx, exnStack, JS::ErrorReportBuilder::WithSideEffects)) {
      JS_ClearPendingException(aCx);
      return;
    }

    MOZ_ASSERT(mOwner);
    MOZ_ASSERT(mMessageName.EqualsLiteral("InterceptionFailedWithURL"));
    MOZ_ASSERT(mParams.Length() == 1);

    // Let's store the error message here.
    mMessageName.Assign(report.toStringResult().c_str());
    mParams.Clear();
  }

  template <typename... Params>
  void SetCancelMessage(const nsACString& aMessageName, Params&&... aParams) {
    MOZ_ASSERT(mOwner);
    MOZ_ASSERT(mMessageName.EqualsLiteral("InterceptionFailedWithURL"));
    MOZ_ASSERT(mParams.Length() == 1);
    mMessageName = aMessageName;
    mParams.Clear();
    StringArrayAppender::Append(mParams, sizeof...(Params),
                                std::forward<Params>(aParams)...);
  }

  template <typename... Params>
  void SetCancelMessageAndLocation(const nsACString& aSourceSpec,
                                   uint32_t aLine, uint32_t aColumn,
                                   const nsACString& aMessageName,
                                   Params&&... aParams) {
    MOZ_ASSERT(mOwner);
    MOZ_ASSERT(mMessageName.EqualsLiteral("InterceptionFailedWithURL"));
    MOZ_ASSERT(mParams.Length() == 1);

    mSourceSpec = aSourceSpec;
    mLine = aLine;
    mColumn = aColumn;

    mMessageName = aMessageName;
    mParams.Clear();
    StringArrayAppender::Append(mParams, sizeof...(Params),
                                std::forward<Params>(aParams)...);
  }

  void Reset() { mOwner = nullptr; }

 private:
  FetchEventOp* MOZ_NON_OWNING_REF mOwner;
  nsCString mSourceSpec;
  uint32_t mLine;
  uint32_t mColumn;
  nsCString mMessageName;
  nsTArray<nsString> mParams;
};

NS_IMPL_ISUPPORTS0(FetchEventOp)

void FetchEventOp::SetActor(RefPtr<FetchEventOpProxyChild> aActor) {
  MOZ_ASSERT(RemoteWorkerService::Thread()->IsOnCurrentThread());
  MOZ_ASSERT(!Started());
  MOZ_ASSERT(!mActor);

  mActor = std::move(aActor);
}

void FetchEventOp::RevokeActor(FetchEventOpProxyChild* aActor) {
  MOZ_ASSERT(aActor);
  MOZ_ASSERT_IF(mActor, mActor == aActor);

  mActor = nullptr;
}

RefPtr<FetchEventRespondWithPromise> FetchEventOp::GetRespondWithPromise() {
  MOZ_ASSERT(RemoteWorkerService::Thread()->IsOnCurrentThread());
  MOZ_ASSERT(!Started());
  MOZ_ASSERT(mRespondWithPromiseHolder.IsEmpty());

  return mRespondWithPromiseHolder.Ensure(__func__);
}

void FetchEventOp::RespondWithCalledAt(const nsCString& aRespondWithScriptSpec,
                                       uint32_t aRespondWithLineNumber,
                                       uint32_t aRespondWithColumnNumber) {
  MOZ_ASSERT(IsCurrentThreadRunningWorker());
  MOZ_ASSERT(!mRespondWithClosure);

  mRespondWithClosure.emplace(aRespondWithScriptSpec, aRespondWithLineNumber,
                              aRespondWithColumnNumber);
}

void FetchEventOp::ReportCanceled(const nsCString& aPreventDefaultScriptSpec,
                                  uint32_t aPreventDefaultLineNumber,
                                  uint32_t aPreventDefaultColumnNumber) {
  MOZ_ASSERT(IsCurrentThreadRunningWorker());
  MOZ_ASSERT(mActor);
  MOZ_ASSERT(!mPromiseHolder.IsEmpty());

  nsString requestURL;
  GetRequestURL(requestURL);

  AsyncLog(aPreventDefaultScriptSpec, aPreventDefaultLineNumber,
           aPreventDefaultColumnNumber, "InterceptionCanceledWithURL"_ns,
           {std::move(requestURL)});
}

FetchEventOp::~FetchEventOp() {
  mRespondWithPromiseHolder.RejectIfExists(
      CancelInterceptionArgs(
          NS_ERROR_DOM_ABORT_ERR,
          FetchEventTimeStamps(mFetchHandlerStart, mFetchHandlerFinish)),
      __func__);
}

void FetchEventOp::RejectAll(nsresult aStatus) {
  MOZ_ASSERT(!mRespondWithPromiseHolder.IsEmpty());
  MOZ_ASSERT(!mPromiseHolder.IsEmpty());

  if (mFetchHandlerStart.IsNull()) {
    mFetchHandlerStart = TimeStamp::Now();
  }
  if (mFetchHandlerFinish.IsNull()) {
    mFetchHandlerFinish = TimeStamp::Now();
  }

  mRespondWithPromiseHolder.Reject(
      CancelInterceptionArgs(
          aStatus,
          FetchEventTimeStamps(mFetchHandlerStart, mFetchHandlerFinish)),
      __func__);
  mPromiseHolder.Reject(aStatus, __func__);
}

void FetchEventOp::FinishedWithResult(ExtendableEventResult aResult) {
  MOZ_ASSERT(IsCurrentThreadRunningWorker());
  MOZ_ASSERT(!mPromiseHolder.IsEmpty());
  MOZ_ASSERT(!mResult);

  mResult.emplace(aResult);

  /**
   * This should only return early if neither waitUntil() nor respondWith()
   * are called. The early return is so that mRespondWithPromiseHolder has a
   * chance to settle before mPromiseHolder does.
   */
  if (!mPostDispatchChecksDone) {
    return;
  }

  MaybeFinished();
}

void FetchEventOp::MaybeFinished() {
  MOZ_ASSERT(IsCurrentThreadRunningWorker());
  MOZ_ASSERT(!mPromiseHolder.IsEmpty());

  if (mResult) {
    // It's possible that mRespondWithPromiseHolder wasn't settled. That happens
    // if the worker was terminated before the respondWith promise settled.

    mHandled = nullptr;
    mPreloadResponse = nullptr;
    mPreloadResponseAvailablePromiseRequestHolder.DisconnectIfExists();
    mPreloadResponseTimingPromiseRequestHolder.DisconnectIfExists();
    mPreloadResponseEndPromiseRequestHolder.DisconnectIfExists();

    ServiceWorkerFetchEventOpResult result(
        mResult.value() == Resolved ? NS_OK : NS_ERROR_FAILURE);

    mPromiseHolder.Resolve(result, __func__);
  }
}

bool FetchEventOp::Exec(JSContext* aCx, WorkerPrivate* aWorkerPrivate) {
  aWorkerPrivate->AssertIsOnWorkerThread();
  MOZ_ASSERT(aWorkerPrivate->IsServiceWorker());
  MOZ_ASSERT(!mRespondWithPromiseHolder.IsEmpty());
  MOZ_ASSERT(!mPromiseHolder.IsEmpty());

  nsresult rv = DispatchFetchEvent(aCx, aWorkerPrivate);

  if (NS_WARN_IF(NS_FAILED(rv))) {
    RejectAll(rv);
  }

  return NS_SUCCEEDED(rv);
}

void FetchEventOp::AsyncLog(const nsCString& aMessageName,
                            nsTArray<nsString> aParams) {
  MOZ_ASSERT(mActor);
  MOZ_ASSERT(!mPromiseHolder.IsEmpty());
  MOZ_ASSERT(mRespondWithClosure);

  const FetchEventRespondWithClosure& closure = mRespondWithClosure.ref();

  AsyncLog(closure.respondWithScriptSpec(), closure.respondWithLineNumber(),
           closure.respondWithColumnNumber(), aMessageName, std::move(aParams));
}

void FetchEventOp::AsyncLog(const nsCString& aScriptSpec, uint32_t aLineNumber,
                            uint32_t aColumnNumber,
                            const nsCString& aMessageName,
                            nsTArray<nsString> aParams) {
  MOZ_ASSERT(mActor);
  MOZ_ASSERT(!mPromiseHolder.IsEmpty());

  // Capture `this` because FetchEventOpProxyChild (mActor) is not thread
  // safe, so an AddRef from RefPtr<FetchEventOpProxyChild>'s constructor will
  // assert.
  RefPtr<FetchEventOp> self = this;

  nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
      __func__, [self = std::move(self), spec = aScriptSpec, line = aLineNumber,
                 column = aColumnNumber, messageName = aMessageName,
                 params = std::move(aParams)] {
        if (NS_WARN_IF(!self->mActor)) {
          return;
        }

        (void)self->mActor->SendAsyncLog(spec, line, column, messageName,
                                         params);
      });

  MOZ_ALWAYS_SUCCEEDS(
      RemoteWorkerService::Thread()->Dispatch(r.forget(), NS_DISPATCH_NORMAL));
}

void FetchEventOp::GetRequestURL(nsAString& aOutRequestURL) {
  nsTArray<NotNull<RefPtr<nsIURI>>>& urls =
      mArgs.get_ParentToChildServiceWorkerFetchEventOpArgs()
          .common()
          .internalRequest()
          .urlList();
  MOZ_ASSERT(!urls.IsEmpty());

  CopyUTF8toUTF16(urls.LastElement()->GetSpecOrDefault(), aOutRequestURL);
}

void FetchEventOp::ResolvedCallback(JSContext* aCx,
                                    JS::Handle<JS::Value> aValue,
                                    ErrorResult& aRv) {
  MOZ_ASSERT(IsCurrentThreadRunningWorker());
  MOZ_ASSERT(mRespondWithClosure);
  MOZ_ASSERT(!mRespondWithPromiseHolder.IsEmpty());
  MOZ_ASSERT(!mPromiseHolder.IsEmpty());

  mFetchHandlerFinish = TimeStamp::Now();

  nsAutoString requestURL;
  GetRequestURL(requestURL);

  AutoCancel autoCancel(this);

  if (!aValue.isObject()) {
    NS_WARNING(
        "FetchEvent::RespondWith was passed a promise resolved to a "
        "non-Object "
        "value");

    nsCString sourceSpec;
    uint32_t line = 0;
    uint32_t column = 0;
    nsString valueString;
    nsContentUtils::ExtractErrorValues(aCx, aValue, sourceSpec, &line, &column,
                                       valueString);

    autoCancel.SetCancelMessageAndLocation(sourceSpec, line, column,
                                           "InterceptedNonResponseWithURL"_ns,
                                           requestURL, valueString);
    return;
  }

  RefPtr<Response> response;
  nsresult rv = UNWRAP_OBJECT(Response, &aValue.toObject(), response);
  if (NS_FAILED(rv)) {
    nsCString sourceSpec;
    uint32_t line = 0;
    uint32_t column = 0;
    nsString valueString;
    nsContentUtils::ExtractErrorValues(aCx, aValue, sourceSpec, &line, &column,
                                       valueString);

    autoCancel.SetCancelMessageAndLocation(sourceSpec, line, column,
                                           "InterceptedNonResponseWithURL"_ns,
                                           requestURL, valueString);
    return;
  }

  WorkerPrivate* worker = GetCurrentThreadWorkerPrivate();
  MOZ_ASSERT(worker);
  worker->AssertIsOnWorkerThread();

  // Section "HTTP Fetch", step 3.3:
  //  If one of the following conditions is true, return a network error:
  //    * response's type is "error".
  //    * request's mode is not "no-cors" and response's type is "opaque".
  //    * request's redirect mode is not "manual" and response's type is
  //      "opaqueredirect".
  //    * request's redirect mode is not "follow" and response's url list
  //      has more than one item.

  if (response->Type() == ResponseType::Error) {
    autoCancel.SetCancelMessage("InterceptedErrorResponseWithURL"_ns,
                                requestURL);
    return;
  }

  const ParentToChildServiceWorkerFetchEventOpArgs& args =
      mArgs.get_ParentToChildServiceWorkerFetchEventOpArgs();
  const RequestMode requestMode = args.common().internalRequest().requestMode();

  if (response->Type() == ResponseType::Opaque &&
      requestMode != RequestMode::No_cors) {
    NS_ConvertASCIItoUTF16 modeString(GetEnumString(requestMode));

    nsAutoString requestURL;
    GetRequestURL(requestURL);

    autoCancel.SetCancelMessage("BadOpaqueInterceptionRequestModeWithURL"_ns,
                                requestURL, modeString);
    return;
  }

  const RequestRedirect requestRedirectMode =
      args.common().internalRequest().requestRedirect();

  if (requestRedirectMode != RequestRedirect::Manual &&
      response->Type() == ResponseType::Opaqueredirect) {
    autoCancel.SetCancelMessage("BadOpaqueRedirectInterceptionWithURL"_ns,
                                requestURL);
    return;
  }

  if (requestRedirectMode != RequestRedirect::Follow &&
      response->Redirected()) {
    autoCancel.SetCancelMessage("BadRedirectModeInterceptionWithURL"_ns,
                                requestURL);
    return;
  }

  if (NS_WARN_IF(response->BodyUsed())) {
    autoCancel.SetCancelMessage("InterceptedUsedResponseWithURL"_ns,
                                requestURL);
    return;
  }

  SafeRefPtr<InternalResponse> ir = response->GetInternalResponse();
  if (NS_WARN_IF(!ir)) {
    return;
  }

  // An extra safety check to make sure our invariant that opaque and cors
  // responses always have a URL does not break.
  if (NS_WARN_IF((response->Type() == ResponseType::Opaque ||
                  response->Type() == ResponseType::Cors) &&
                 !ir->GetUnfilteredURL())) {
    MOZ_DIAGNOSTIC_CRASH("Cors or opaque Response without a URL");
    return;
  }

  if (requestMode == RequestMode::Same_origin &&
      response->Type() == ResponseType::Cors) {
    // XXXtt: Will have a pref to enable the quirk response in bug 1419684.
    // The variadic template provided by StringArrayAppender requires exactly
    // an nsString.
    NS_ConvertUTF8toUTF16 responseURL(
        ir->GetUnfilteredURL()->GetSpecOrDefault());
    autoCancel.SetCancelMessage("CorsResponseForSameOriginRequest"_ns,
                                requestURL, responseURL);
    return;
  }

  nsCOMPtr<nsIInputStream> body;
  ir->GetUnfilteredBody(getter_AddRefs(body));
  // Errors and redirects may not have a body.
  if (body) {
    ErrorResult error;
    response->SetBodyUsed(aCx, error);
    error.WouldReportJSException();
    if (NS_WARN_IF(error.Failed())) {
      autoCancel.SetCancelErrorResult(aCx, error);
      return;
    }
  }

  if (!ir->GetChannelInfo().IsInitialized()) {
    // This is a synthetic response (I think and hope so).
    ir->InitChannelInfo(worker->GetChannelInfo());
  }

  autoCancel.Reset();

  SynthesizeResponseArgs synthesizeResponseArgs;
  ChildToParentSynthesizeResponseArgs& ipcArgs =
      synthesizeResponseArgs.mIPCArgs;
  ipcArgs.closure() = mRespondWithClosure.ref();
  ipcArgs.timeStamps() =
      FetchEventTimeStamps(mFetchHandlerStart, mFetchHandlerFinish);
  ir->ToChildToParentInternalResponse(&ipcArgs.internalResponse());

  // Extract the body streams here on the worker thread rather than later on
  // the Worker Launcher thread, since `ir` is still shared with the Response
  // object at this point.
  ir->GetUnfilteredBody(getter_AddRefs(synthesizeResponseArgs.mBody),
                        &ipcArgs.internalResponse().bodySize());
  synthesizeResponseArgs.mAlternativeBody = ir->TakeAlternativeBody();

  // https://w3c.github.io/ServiceWorker/#on-fetch-request-algorithm Step 26: If
  // eventHandled is not null, then resolve eventHandled.
  mHandled->MaybeResolveWithUndefined();

  mRespondWithPromiseHolder.Resolve(
      FetchEventRespondWithResult(std::move(synthesizeResponseArgs)), __func__);
}

void FetchEventOp::RejectedCallback(JSContext* aCx,
                                    JS::Handle<JS::Value> aValue,
                                    ErrorResult& aRv) {
  MOZ_ASSERT(IsCurrentThreadRunningWorker());
  MOZ_ASSERT(mRespondWithClosure);
  MOZ_ASSERT(!mRespondWithPromiseHolder.IsEmpty());
  MOZ_ASSERT(!mPromiseHolder.IsEmpty());

  mFetchHandlerFinish = TimeStamp::Now();

  FetchEventRespondWithClosure& closure = mRespondWithClosure.ref();

  nsCString sourceSpec = closure.respondWithScriptSpec();
  uint32_t line = closure.respondWithLineNumber();
  uint32_t column = closure.respondWithColumnNumber();
  nsString valueString;

  nsContentUtils::ExtractErrorValues(aCx, aValue, sourceSpec, &line, &column,
                                     valueString);

  nsString requestURL;
  GetRequestURL(requestURL);

  AsyncLog(sourceSpec, line, column, "InterceptionRejectedResponseWithURL"_ns,
           {std::move(requestURL), std::move(valueString)});

  // https://w3c.github.io/ServiceWorker/#on-fetch-request-algorithm Step 25.1:
  // If eventHandled is not null, then reject eventHandled with a "NetworkError"
  // DOMException in workerRealm.
  mHandled->MaybeRejectWithNetworkError(
      "FetchEvent.respondWith() Promise rejected"_ns);
  mRespondWithPromiseHolder.Resolve(
      FetchEventRespondWithResult(CancelInterceptionArgs(
          NS_ERROR_INTERCEPTION_FAILED,
          FetchEventTimeStamps(mFetchHandlerStart, mFetchHandlerFinish))),
      __func__);
}

nsresult FetchEventOp::DispatchFetchEvent(JSContext* aCx,
                                          WorkerPrivate* aWorkerPrivate) {
  MOZ_ASSERT(aCx);
  MOZ_ASSERT(aWorkerPrivate);
  aWorkerPrivate->AssertIsOnWorkerThread();
  MOZ_ASSERT(aWorkerPrivate->IsServiceWorker());

  ParentToChildServiceWorkerFetchEventOpArgs& args =
      mArgs.get_ParentToChildServiceWorkerFetchEventOpArgs();

  /**
   * Testing: Failure injection.
   *
   * There are a number of different ways that this fetch event could have
   * failed that would result in cancellation.  This injection point helps
   * simulate them without worrying about shifting implementation details with
   * full fidelity reproductions of current scenarios.
   *
   * Broadly speaking, we expect fetch event scenarios to fail because of:
   * - Script load failure, which results in the CompileScriptRunnable closing
   *   the worker and thereby cancelling all pending operations, including this
   *   fetch.  The `ServiceWorkerOp::Cancel` impl just calls
   *   RejectAll(NS_ERROR_DOM_ABORT_ERR) which we are able to approximate by
   *   returning the same nsresult here, as our caller also calls RejectAll.
   *   (And timing-wise, this rejection will happen in the correct sequence.)
   * - An exception gets thrown in the processing of the promise that was passed
   *   to respondWith and it ends up rejecting.  The rejection will be converted
   *   by `FetchEventOp::RejectedCallback` into a cancellation with
   *   NS_ERROR_INTERCEPTION_FAILED, and by returning that here we approximate
   *   that failure mode.
   */
  if (NS_FAILED(args.common().testingInjectCancellation())) {
    return args.common().testingInjectCancellation();
  }

  /**
   * Step 1: get the InternalRequest. The InternalRequest can't be constructed
   * here from mArgs because the IPCStream has to be deserialized on the
   * thread receiving the ServiceWorkerFetchEventOpArgs.
   * FetchEventOpProxyChild will have already deserialized the stream on the
   * correct thread before creating this op, so we can take its saved
   * InternalRequest.
   */
  SafeRefPtr<InternalRequest> internalRequest =
      mActor->ExtractInternalRequest();

  /**
   * Step 2: get the worker's global object
   */
  GlobalObject globalObject(aCx, aWorkerPrivate->GlobalScope()->GetWrapper());
  nsCOMPtr<nsIGlobalObject> globalObjectAsSupports =
      do_QueryInterface(globalObject.GetAsSupports());
  if (NS_WARN_IF(!globalObjectAsSupports)) {
    return NS_ERROR_DOM_INVALID_STATE_ERR;
  }

  /**
   * Step 3: create the public DOM Request object
   * TODO: this Request object should be created with an AbortSignal object
   * which should be aborted if the loading is aborted. See but 1394102.
   */
  RefPtr<Request> request =
      new Request(globalObjectAsSupports, internalRequest.clonePtr(), nullptr);
  MOZ_ASSERT_IF(internalRequest->IsNavigationRequest(),
                request->Redirect() == RequestRedirect::Manual);

  /**
   * Step 4a: create the FetchEventInit
   */
  RootedDictionary<FetchEventInit> fetchEventInit(aCx);
  fetchEventInit.mRequest = request;
  fetchEventInit.mBubbles = false;
  fetchEventInit.mCancelable = true;

  /**
   * TODO: only expose the FetchEvent.clientId on subresource requests for
   * now. Once we implement .targetClientId we can then start exposing
   * .clientId on non-subresource requests as well.  See bug 1487534.
   */
  if (!args.common().clientId().IsEmpty() &&
      !internalRequest->IsNavigationRequest()) {
    fetchEventInit.mClientId = args.common().clientId();
  }

  /*
   * https://w3c.github.io/ServiceWorker/#on-fetch-request-algorithm
   *
   * "If request is a non-subresource request and request’s
   * destination is not "report", initialize e’s resultingClientId attribute
   * to reservedClient’s [resultingClient's] id, and to the empty string
   * otherwise." (Step 18.8)
   */
  if (!args.common().resultingClientId().IsEmpty() &&
      args.common().isNonSubresourceRequest() &&
      internalRequest->Destination() != RequestDestination::Report) {
    fetchEventInit.mResultingClientId = args.common().resultingClientId();
  }

  /**
   * Step 4b: create the FetchEvent
   */
  RefPtr<FetchEvent> fetchEvent =
      FetchEvent::Constructor(globalObject, u"fetch"_ns, fetchEventInit);
  fetchEvent->SetTrusted(true);
  fetchEvent->PostInit(args.common().workerScriptSpec(), this);
  mHandled = fetchEvent->Handled();
  mPreloadResponse = fetchEvent->PreloadResponse();

  if (args.common().preloadNavigation()) {
    RefPtr<FetchEventPreloadResponseAvailablePromise> preloadResponsePromise =
        mActor->GetPreloadResponseAvailablePromise();
    MOZ_ASSERT(preloadResponsePromise);

    // If preloadResponsePromise has already settled then this callback will get
    // run synchronously here.
    RefPtr<FetchEventOp> self = this;
    preloadResponsePromise
        ->Then(
            GetCurrentSerialEventTarget(), __func__,
            [self, globalObjectAsSupports](
                SafeRefPtr<InternalResponse>&& aPreloadResponse) {
              // let's complete the promise holder before MaybeResolve
              self->mPreloadResponseAvailablePromiseRequestHolder.Complete();
              RefPtr<Promise> preloadResponse = self->mPreloadResponse;
              if (preloadResponse) {
                preloadResponse->MaybeResolve(
                    MakeRefPtr<Response>(globalObjectAsSupports,
                                         std::move(aPreloadResponse), nullptr));
              }
            },
            [self](int) {
              self->mPreloadResponseAvailablePromiseRequestHolder.Complete();
            })
        ->Track(mPreloadResponseAvailablePromiseRequestHolder);

    RefPtr<PerformanceStorage> performanceStorage =
        aWorkerPrivate->GetPerformanceStorage();

    RefPtr<FetchEventPreloadResponseTimingPromise>
        preloadResponseTimingPromise =
            mActor->GetPreloadResponseTimingPromise();
    MOZ_ASSERT(preloadResponseTimingPromise);
    preloadResponseTimingPromise
        ->Then(
            GetCurrentSerialEventTarget(), __func__,
            [self, performanceStorage,
             globalObjectAsSupports](ResponseTiming&& aTiming) {
              if (performanceStorage && !aTiming.entryName().IsEmpty() &&
                  aTiming.initiatorType().Equals(u"navigation"_ns)) {
                performanceStorage->AddEntry(
                    aTiming.entryName(), aTiming.initiatorType(),
                    std::make_unique<PerformanceTimingData>(
                        aTiming.timingData()));
              }
              self->mPreloadResponseTimingPromiseRequestHolder.Complete();
            },
            [self](int) {
              self->mPreloadResponseTimingPromiseRequestHolder.Complete();
            })
        ->Track(mPreloadResponseTimingPromiseRequestHolder);

    RefPtr<FetchEventPreloadResponseEndPromise> preloadResponseEndPromise =
        mActor->GetPreloadResponseEndPromise();
    MOZ_ASSERT(preloadResponseEndPromise);
    preloadResponseEndPromise
        ->Then(
            GetCurrentSerialEventTarget(), __func__,
            [self, globalObjectAsSupports](ResponseEndArgs&& aArgs) {
              // let's complete the promise holder before MaybeReject
              self->mPreloadResponseEndPromiseRequestHolder.Complete();
              if (aArgs.endReason() == FetchDriverObserver::eAborted) {
                RefPtr<Promise> preloadResponse = self->mPreloadResponse;
                if (preloadResponse) {
                  preloadResponse->MaybeReject(NS_ERROR_DOM_ABORT_ERR);
                }
              }
            },
            [self](int) {
              self->mPreloadResponseEndPromiseRequestHolder.Complete();
            })
        ->Track(mPreloadResponseEndPromiseRequestHolder);
  } else {
    // preload navigation is disabled, resolved preload response promise with
    // undefined as default behavior.
    mPreloadResponse->MaybeResolveWithUndefined();
  }

  mFetchHandlerStart = TimeStamp::Now();

  /**
   * Step 5: Dispatch the FetchEvent to the worker's global object
   */
  nsresult rv = DispatchExtendableEventOnWorkerScope(
      aCx, aWorkerPrivate->GlobalScope(), fetchEvent, this);
  bool dispatchFailed = NS_FAILED(rv) && rv != NS_ERROR_XPC_JS_THREW_EXCEPTION;

  if (NS_WARN_IF(dispatchFailed)) {
    mHandled = nullptr;
    mPreloadResponse = nullptr;
    return rv;
  }

  /**
   * At this point, there are 4 (legal) scenarios:
   *
   * 1) If neither waitUntil() nor respondWith() are called,
   * DispatchExtendableEventOnWorkerScope() will have already called
   * FinishedWithResult(), but this call will have recorded the result
   * (mResult) and returned early so that mRespondWithPromiseHolder can be
   * settled first. mRespondWithPromiseHolder will be settled below, followed
   * by a call to MaybeFinished() which settles mPromiseHolder.
   *
   * 2) If waitUntil() is called at least once, and respondWith() is not
   * called, DispatchExtendableEventOnWorkerScope() will NOT have called
   * FinishedWithResult(). We'll settle mRespondWithPromiseHolder first, and
   * at some point in the future when the last waitUntil() promise settles,
   * FinishedWithResult() will be called, settling mPromiseHolder.
   *
   * 3) If waitUntil() is not called, and respondWith() is called,
   * DispatchExtendableEventOnWorkerScope() will NOT have called
   * FinishedWithResult(). We can also guarantee that
   * mRespondWithPromiseHolder will be settled before mPromiseHolder, due to
   * the Promise::AppendNativeHandler() call ordering in
   * FetchEvent::RespondWith().
   *
   * 4) If waitUntil() is called at least once, and respondWith() is also
   * called, the effect is similar to scenario 3), with the most imporant
   * property being mRespondWithPromiseHolder settling before mPromiseHolder.
   *
   * Note that if mPromiseHolder is settled before mRespondWithPromiseHolder,
   * FetchEventOpChild will cancel the interception.
   */
  if (!fetchEvent->WaitToRespond()) {
    MOZ_ASSERT(!mRespondWithPromiseHolder.IsEmpty());
    MOZ_ASSERT(!aWorkerPrivate->UsesSystemPrincipal(),
               "We don't support system-principal serviceworkers");

    mFetchHandlerFinish = TimeStamp::Now();

    // https://w3c.github.io/ServiceWorker/#create-fetch-event-and-dispatch-algorithm
    // Step 17: with no response and a request body whose source is null (a
    // streaming upload), an unusable body means handle fetch fails.
    RefPtr<Request> request = fetchEvent->Request_();
    const bool hasStreamBody = request && request->HasStreamBody();
    const bool streamBodyUnusable = hasStreamBody && request->IsBodyUnusable();

    if (fetchEvent->DefaultPrevented(CallerType::NonSystem)) {
      // https://w3c.github.io/ServiceWorker/#on-fetch-request-algorithm
      // Step 24.1.1: If eventHandled is not null, then reject eventHandled with
      // a "NetworkError" DOMException in workerRealm.
      mHandled->MaybeRejectWithNetworkError(
          "FetchEvent.preventDefault() called"_ns);
      mRespondWithPromiseHolder.Resolve(
          FetchEventRespondWithResult(CancelInterceptionArgs(
              NS_ERROR_INTERCEPTION_FAILED,
              FetchEventTimeStamps(mFetchHandlerStart, mFetchHandlerFinish))),
          __func__);
    } else if (streamBodyUnusable) {
      // Step 22: handleFetchFailed is set, so eventHandled is rejected and the
      // fetch ends in a network error rather than falling back.
      mHandled->MaybeRejectWithNetworkError(
          "Streaming request body was used before network fallback"_ns);
      mRespondWithPromiseHolder.Resolve(
          FetchEventRespondWithResult(CancelInterceptionArgs(
              NS_ERROR_INTERCEPTION_FAILED,
              FetchEventTimeStamps(mFetchHandlerStart, mFetchHandlerFinish))),
          __func__);
    } else {
      // https://w3c.github.io/ServiceWorker/#on-fetch-request-algorithm
      // Step 24.2: If eventHandled is not null, then resolve eventHandled.
      // Only the fallback consumes the body, so the worker gives up its claim
      // on it here and hands the unread stream back to the channel.
      nsCOMPtr<nsIInputStream> fallbackBody;
      if (hasStreamBody) {
        IgnoredErrorResult rv;
        fallbackBody = request->TakeBodyForServiceWorker(aCx, rv);
      }
      mHandled->MaybeResolveWithUndefined();
      mRespondWithPromiseHolder.Resolve(
          FetchEventRespondWithResult(ResetInterceptionArgs(
              FetchEventTimeStamps(mFetchHandlerStart, mFetchHandlerFinish),
              fallbackBody ? Some(mozilla::ipc::EagerIPCStream{
                                 WrapNotNull(fallbackBody)})
                           : Nothing())),
          __func__);
    }
  } else {
    MOZ_ASSERT(mRespondWithClosure);
  }

  mPostDispatchChecksDone = true;
  MaybeFinished();

  return NS_OK;
}

class ExtensionAPIEventOp final : public ServiceWorkerOp {
  using ServiceWorkerOp::ServiceWorkerOp;

 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(ExtensionAPIEventOp, override)

 private:
  ~ExtensionAPIEventOp() = default;

  bool Exec(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();
    MOZ_ASSERT(aWorkerPrivate->IsServiceWorker());
    MOZ_ASSERT(aWorkerPrivate->ExtensionAPIAllowed());
    MOZ_ASSERT(!mPromiseHolder.IsEmpty());

    ServiceWorkerExtensionAPIEventOpArgs& args =
        mArgs.get_ServiceWorkerExtensionAPIEventOpArgs();

    ServiceWorkerExtensionAPIEventOpResult result;
    result.extensionAPIEventListenerWasAdded() = false;

    if (aWorkerPrivate->WorkerScriptExecutedSuccessfully()) {
      GlobalObject globalObj(aCx, aWorkerPrivate->GlobalScope()->GetWrapper());
      RefPtr<ServiceWorkerGlobalScope> scope;
      UNWRAP_OBJECT(ServiceWorkerGlobalScope, globalObj.Get(), scope);
      SafeRefPtr<extensions::ExtensionBrowser> extensionAPI =
          scope->AcquireExtensionBrowser();
      if (!extensionAPI) {
        // If the worker script did never access the WebExtension APIs
        // then we can return earlier, no event listener could have been added.
        mPromiseHolder.Resolve(result, __func__);
        return true;
      }
      // Check if a listener has been subscribed on the expected WebExtensions
      // API event.
      bool hasWakeupListener = extensionAPI->HasWakeupEventListener(
          args.apiNamespace(), args.apiEventName());
      result.extensionAPIEventListenerWasAdded() = hasWakeupListener;
      mPromiseHolder.Resolve(result, __func__);
    } else {
      mPromiseHolder.Resolve(result, __func__);
    }

    return true;
  }
};

/* static */ already_AddRefed<ServiceWorkerOp> ServiceWorkerOp::Create(
    ServiceWorkerOpArgs&& aArgs,
    std::function<void(const ServiceWorkerOpResult&)>&& aCallback) {
  // Can be on the Worker Launcher thread for Terminate operations or on the
  // Worker thread for other operations.
  RefPtr<ServiceWorkerOp> op;

  switch (aArgs.type()) {
    case ServiceWorkerOpArgs::TServiceWorkerCheckScriptEvaluationOpArgs:
      op = MakeRefPtr<CheckScriptEvaluationOp>(std::move(aArgs),
                                               std::move(aCallback));
      break;
    case ServiceWorkerOpArgs::TServiceWorkerUpdateStateOpArgs:
      op = MakeRefPtr<UpdateServiceWorkerStateOp>(std::move(aArgs),
                                                  std::move(aCallback));
      break;
    case ServiceWorkerOpArgs::TServiceWorkerTerminateWorkerOpArgs:
      op = MakeRefPtr<TerminateServiceWorkerOp>(std::move(aArgs),
                                                std::move(aCallback));
      break;
    case ServiceWorkerOpArgs::TServiceWorkerLifeCycleEventOpArgs:
      op = MakeRefPtr<LifeCycleEventOp>(std::move(aArgs), std::move(aCallback));
      break;
    case ServiceWorkerOpArgs::TServiceWorkerCookieChangeEventOpArgs:
      op = MakeRefPtr<CookieChangeEventOp>(std::move(aArgs),
                                           std::move(aCallback));
      break;
    case ServiceWorkerOpArgs::TServiceWorkerPushEventOpArgs:
      op = MakeRefPtr<PushEventOp>(std::move(aArgs), std::move(aCallback));
      break;
    case ServiceWorkerOpArgs::TServiceWorkerPushSubscriptionChangeEventOpArgs:
      op = MakeRefPtr<PushSubscriptionChangeEventOp>(std::move(aArgs),
                                                     std::move(aCallback));
      break;
    case ServiceWorkerOpArgs::TServiceWorkerNotificationEventOpArgs:
      op = MakeRefPtr<NotificationEventOp>(std::move(aArgs),
                                           std::move(aCallback));
      break;
    case ServiceWorkerOpArgs::TServiceWorkerMessageEventOpArgs:
      op = MakeRefPtr<MessageEventOp>(std::move(aArgs), std::move(aCallback));
      break;
    case ServiceWorkerOpArgs::TParentToChildServiceWorkerFetchEventOpArgs:
      op = MakeRefPtr<FetchEventOp>(std::move(aArgs), std::move(aCallback));
      break;
    case ServiceWorkerOpArgs::TServiceWorkerExtensionAPIEventOpArgs:
      op = MakeRefPtr<ExtensionAPIEventOp>(std::move(aArgs),
                                           std::move(aCallback));
      break;
    case ServiceWorkerOpArgs::
        TServiceWorkerUpdateIsOnContentBlockingAllowListOpArgs:
      op = MakeRefPtr<UpdateIsOnContentBlockingAllowListOp>(
          std::move(aArgs), std::move(aCallback));
      break;
    default:
      MOZ_CRASH("Unknown Service Worker operation!");
      return nullptr;
  }

  return op.forget();
}

}  // namespace mozilla::dom
