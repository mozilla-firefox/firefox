/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Fetch.h"

#include "BodyExtractor.h"
#include "FetchChild.h"
#include "FetchObserver.h"
#include "FetchUtil.h"
#include "InternalRequest.h"
#include "InternalResponse.h"
#include "ThirdPartyUtil.h"
#include "js/RootingAPI.h"
#include "js/Value.h"
#include "mozilla/CycleCollectedJSContext.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/BodyConsumer.h"
#include "mozilla/dom/DOMException.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Exceptions.h"
#include "mozilla/dom/FetchDriver.h"
#include "mozilla/dom/File.h"
#include "mozilla/dom/FormData.h"
#include "mozilla/dom/Headers.h"
#include "mozilla/dom/MimeType.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/PromiseWorkerProxy.h"
#include "mozilla/dom/ReadableStreamDefaultReader.h"
#include "mozilla/dom/RemoteWorkerChild.h"
#include "mozilla/dom/Request.h"
#include "mozilla/dom/Response.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/URLSearchParams.h"
#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/dom/WorkerRef.h"
#include "mozilla/dom/WorkerRunnable.h"
#include "mozilla/dom/WorkerScope.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/IPCStreamUtils.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "mozilla/net/CookieJarSettings.h"
#include "nsDOMString.h"
#include "nsIClassifiedChannel.h"
#include "nsIGlobalObject.h"
#include "nsJSUtils.h"
#include "nsNetUtil.h"
#include "nsPIDOMWindowInlines.h"
#include "nsProxyRelease.h"
#include "nsReadableUtils.h"
#include "nsStreamUtils.h"
#include "nsStringStream.h"

namespace mozilla::dom {

namespace {

// Step 17.2.1.2 and 17.2.2 of
// https://fetch.spec.whatwg.org/#concept-http-network-fetch
// If stream is readable, then error stream with ...
void AbortStream(JSContext* aCx, ReadableStream* aReadableStream,
                 AbortSignalImpl* aSignal, ErrorResult& aRv) {
  MOZ_ASSERT(aSignal->Aborted());

  if (aReadableStream->State() != ReadableStream::ReaderState::Readable) {
    return;
  }

  JS::Rooted<JS::Value> reason(aCx);
  aSignal->GetReason(aCx, &reason);

  aReadableStream->ErrorNative(aCx, reason, aRv);
}

}  // namespace

class AbortSignalMainThread final : public AbortSignalImpl {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS_FINAL
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(AbortSignalMainThread)

  explicit AbortSignalMainThread(SignalAborted aAborted)
      : AbortSignalImpl(aAborted, JS::UndefinedHandleValue) {
    mozilla::HoldJSObjects(this);
  }

 private:
  ~AbortSignalMainThread() { mozilla::DropJSObjects(this); };
};

NS_IMPL_CYCLE_COLLECTION_CLASS(AbortSignalMainThread)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(AbortSignalMainThread)
  AbortSignalImpl::Unlink(static_cast<AbortSignalImpl*>(tmp));
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(AbortSignalMainThread)
  AbortSignalImpl::Traverse(static_cast<AbortSignalImpl*>(tmp), cb);
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN(AbortSignalMainThread)
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mReason)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(AbortSignalMainThread)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(AbortSignalMainThread)
NS_IMPL_CYCLE_COLLECTING_RELEASE(AbortSignalMainThread)

class AbortSignalProxy;

// This runnable propagates changes from the AbortSignalImpl on workers to the
// AbortSignalImpl on main-thread.
class AbortSignalProxyRunnable final : public Runnable {
  RefPtr<AbortSignalProxy> mProxy;

 public:
  explicit AbortSignalProxyRunnable(AbortSignalProxy* aProxy)
      : Runnable("dom::AbortSignalProxyRunnable"), mProxy(aProxy) {}

  NS_IMETHOD Run() override;
};

// This class orchestrates the proxying of AbortSignal operations between the
// main thread and a worker thread.
class AbortSignalProxy final : public AbortFollower {
  // This is created and released on the main-thread.
  RefPtr<AbortSignalImpl> mSignalImplMainThread;

  // The main-thread event target for runnable dispatching.
  nsCOMPtr<nsIEventTarget> mMainThreadEventTarget;

  // This value is used only when creating mSignalImplMainThread on the main
  // thread, to create it in already-aborted state if necessary.  It does *not*
  // reflect the instantaneous is-aborted status of the worker thread's
  // AbortSignal.
  const SignalAborted mAborted;

 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  AbortSignalProxy(AbortSignalImpl* aSignalImpl,
                   nsIEventTarget* aMainThreadEventTarget)
      : mMainThreadEventTarget(aMainThreadEventTarget),
        mAborted(aSignalImpl->Aborted() ? SignalAborted::Yes
                                        : SignalAborted::No) {
    MOZ_ASSERT(!NS_IsMainThread());
    MOZ_ASSERT(mMainThreadEventTarget);
    Follow(aSignalImpl);
  }

  // AbortFollower
  void RunAbortAlgorithm() override;

  AbortSignalImpl* GetOrCreateSignalImplForMainThread() {
    MOZ_ASSERT(NS_IsMainThread());
    if (!mSignalImplMainThread) {
      mSignalImplMainThread = new AbortSignalMainThread(mAborted);
    }
    return mSignalImplMainThread;
  }

  AbortSignalImpl* GetSignalImplForTargetThread() {
    MOZ_ASSERT(!NS_IsMainThread());
    return Signal();
  }

  nsIEventTarget* MainThreadEventTarget() { return mMainThreadEventTarget; }

  void Shutdown() {
    MOZ_ASSERT(!NS_IsMainThread());
    Unfollow();
  }

 private:
  ~AbortSignalProxy() {
    NS_ProxyRelease("AbortSignalProxy::mSignalImplMainThread",
                    mMainThreadEventTarget, mSignalImplMainThread.forget());
  }
};

NS_IMPL_ISUPPORTS0(AbortSignalProxy)

NS_IMETHODIMP AbortSignalProxyRunnable::Run() {
  MOZ_ASSERT(NS_IsMainThread());
  AbortSignalImpl* signalImpl = mProxy->GetOrCreateSignalImplForMainThread();
  signalImpl->SignalAbort(JS::UndefinedHandleValue);
  return NS_OK;
}

void AbortSignalProxy::RunAbortAlgorithm() {
  MOZ_ASSERT(!NS_IsMainThread());
  RefPtr<AbortSignalProxyRunnable> runnable =
      new AbortSignalProxyRunnable(this);
  MainThreadEventTarget()->Dispatch(runnable.forget(), NS_DISPATCH_NORMAL);
}

class WorkerFetchResolver final : public FetchDriverObserver {
  // Thread-safe:
  RefPtr<PromiseWorkerProxy> mPromiseProxy;
  RefPtr<AbortSignalProxy> mSignalProxy;

  // Touched only on the worker thread.
  RefPtr<FetchObserver> mFetchObserver;
  RefPtr<WeakWorkerRef> mWorkerRef;
  bool mIsShutdown;

  Atomic<bool> mNeedOnDataAvailable;

 public:
  // Returns null if worker is shutting down.
  static already_AddRefed<WorkerFetchResolver> Create(
      WorkerPrivate* aWorkerPrivate, Promise* aPromise,
      AbortSignalImpl* aSignalImpl, FetchObserver* aObserver) {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();
    RefPtr<PromiseWorkerProxy> proxy =
        PromiseWorkerProxy::Create(aWorkerPrivate, aPromise);
    if (!proxy) {
      return nullptr;
    }

    RefPtr<AbortSignalProxy> signalProxy;
    if (aSignalImpl) {
      signalProxy = new AbortSignalProxy(
          aSignalImpl, aWorkerPrivate->MainThreadEventTarget());
    }

    RefPtr<WorkerFetchResolver> r =
        new WorkerFetchResolver(proxy, signalProxy, aObserver);

    RefPtr<WeakWorkerRef> workerRef = WeakWorkerRef::Create(
        aWorkerPrivate, [r]() { r->Shutdown(r->mWorkerRef->GetPrivate()); });
    if (NS_WARN_IF(!workerRef)) {
      return nullptr;
    }

    r->mWorkerRef = std::move(workerRef);

    return r.forget();
  }

  AbortSignalImpl* GetAbortSignalForMainThread() {
    MOZ_ASSERT(NS_IsMainThread());

    if (!mSignalProxy) {
      return nullptr;
    }

    return mSignalProxy->GetOrCreateSignalImplForMainThread();
  }

  AbortSignalImpl* GetAbortSignalForTargetThread() {
    mPromiseProxy->GetWorkerPrivate()->AssertIsOnWorkerThread();

    if (!mSignalProxy) {
      return nullptr;
    }

    return mSignalProxy->GetSignalImplForTargetThread();
  }

  PromiseWorkerProxy* PromiseProxy() const {
    MOZ_ASSERT(NS_IsMainThread());
    return mPromiseProxy;
  }

  Promise* WorkerPromise(WorkerPrivate* aWorkerPrivate) const {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();
    MOZ_ASSERT(!mIsShutdown);

    return mPromiseProxy->GetWorkerPromise();
  }

  FetchObserver* GetFetchObserver(WorkerPrivate* aWorkerPrivate) const {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();

    return mFetchObserver;
  }

  MOZ_CAN_RUN_SCRIPT void OnResponseAvailableInternal(
      SafeRefPtr<InternalResponse> aResponse) override;

  void OnResponseEnd(FetchDriverObserver::EndReason aReason,
                     JS::Handle<JS::Value> aReasonDetails) override;

  bool NeedOnDataAvailable() override;

  void OnDataAvailable() override;

  void Shutdown(WorkerPrivate* aWorkerPrivate) {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();

    mIsShutdown = true;
    mPromiseProxy->CleanUp();

    mNeedOnDataAvailable = false;
    mFetchObserver = nullptr;

    if (mSignalProxy) {
      mSignalProxy->Shutdown();
    }

    mWorkerRef = nullptr;
  }

  bool IsShutdown(WorkerPrivate* aWorkerPrivate) const {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();
    return mIsShutdown;
  }

 private:
  WorkerFetchResolver(PromiseWorkerProxy* aProxy,
                      AbortSignalProxy* aSignalProxy, FetchObserver* aObserver)
      : mPromiseProxy(aProxy),
        mSignalProxy(aSignalProxy),
        mFetchObserver(aObserver),
        mIsShutdown(false),
        mNeedOnDataAvailable(!!aObserver) {
    MOZ_ASSERT(!NS_IsMainThread());
    MOZ_ASSERT(mPromiseProxy);
  }

  ~WorkerFetchResolver() = default;

  virtual void FlushConsoleReport() override;
};

void FetchDriverObserver::OnResponseAvailable(
    SafeRefPtr<InternalResponse> aResponse) {
  MOZ_ASSERT(!mGotResponseAvailable);
  mGotResponseAvailable = true;
  OnResponseAvailableInternal(std::move(aResponse));
}

class MainThreadFetchResolver final : public FetchDriverObserver {
  RefPtr<Promise> mPromise;
  RefPtr<Response> mResponse;
  RefPtr<FetchObserver> mFetchObserver;
  RefPtr<AbortSignalImpl> mSignalImpl;
  const bool mMozErrors;

  nsCOMPtr<nsILoadGroup> mLoadGroup;

  NS_DECL_OWNINGTHREAD
 public:
  MainThreadFetchResolver(Promise* aPromise, FetchObserver* aObserver,
                          AbortSignalImpl* aSignalImpl, bool aMozErrors)
      : mPromise(aPromise),
        mFetchObserver(aObserver),
        mSignalImpl(aSignalImpl),
        mMozErrors(aMozErrors) {}

  MOZ_CAN_RUN_SCRIPT void OnResponseAvailableInternal(
      SafeRefPtr<InternalResponse> aResponse) override;

  void SetLoadGroup(nsILoadGroup* aLoadGroup) { mLoadGroup = aLoadGroup; }

  void OnResponseEnd(FetchDriverObserver::EndReason aReason,
                     JS::Handle<JS::Value> aReasonDetails) override {
    if (aReason == eAborted) {
      if (!aReasonDetails.isUndefined()) {
        mPromise->MaybeReject(aReasonDetails);
      } else {
        mPromise->MaybeReject(NS_ERROR_DOM_ABORT_ERR);
      }
    }

    mFetchObserver = nullptr;

    FlushConsoleReport();
  }

  bool NeedOnDataAvailable() override;

  void OnDataAvailable() override;

 private:
  ~MainThreadFetchResolver();

  void FlushConsoleReport() override {
    mReporter->FlushConsoleReports(mLoadGroup);
  }
};

class MainThreadFetchRunnable : public Runnable {
  RefPtr<WorkerFetchResolver> mResolver;
  const ClientInfo mClientInfo;
  const Maybe<ServiceWorkerDescriptor> mController;
  nsCOMPtr<nsICSPEventListener> mCSPEventListener;
  SafeRefPtr<InternalRequest> mRequest;
  UniquePtr<SerializedStackHolder> mOriginStack;

 public:
  MainThreadFetchRunnable(WorkerFetchResolver* aResolver,
                          const ClientInfo& aClientInfo,
                          const Maybe<ServiceWorkerDescriptor>& aController,
                          nsICSPEventListener* aCSPEventListener,
                          SafeRefPtr<InternalRequest> aRequest,
                          UniquePtr<SerializedStackHolder>&& aOriginStack)
      : Runnable("dom::MainThreadFetchRunnable"),
        mResolver(aResolver),
        mClientInfo(aClientInfo),
        mController(aController),
        mCSPEventListener(aCSPEventListener),
        mRequest(std::move(aRequest)),
        mOriginStack(std::move(aOriginStack)) {
    MOZ_ASSERT(mResolver);
  }

  NS_IMETHOD
  Run() override {
    AssertIsOnMainThread();
    RefPtr<FetchDriver> fetch;
    RefPtr<PromiseWorkerProxy> proxy = mResolver->PromiseProxy();

    {
      // Acquire the proxy mutex while getting data from the WorkerPrivate...
      MutexAutoLock lock(proxy->Lock());
      if (proxy->CleanedUp()) {
        NS_WARNING("Aborting Fetch because worker already shut down");
        return NS_OK;
      }

      WorkerPrivate* workerPrivate = proxy->GetWorkerPrivate();
      MOZ_ASSERT(workerPrivate);
      nsCOMPtr<nsIPrincipal> principal = workerPrivate->GetPrincipal();
      MOZ_ASSERT(principal);
      nsCOMPtr<nsILoadGroup> loadGroup = workerPrivate->GetLoadGroup();
      MOZ_ASSERT(loadGroup);
      // We don't track if a worker is spawned from a tracking script for now,
      // so pass false as the last argument to FetchDriver().
      nsCOMPtr<nsICookieJarSettings> cookieJarSettings;
      if (mRequest->GetCookieJarSettings()) {
        cookieJarSettings = mRequest->GetCookieJarSettings();
      } else {
        cookieJarSettings = workerPrivate->CookieJarSettings();
      }
      fetch = new FetchDriver(mRequest.clonePtr(), principal, loadGroup,
                              workerPrivate->MainThreadEventTarget(),
                              cookieJarSettings,
                              workerPrivate->GetPerformanceStorage(),
                              net::ClassificationFlags({0, 0}));
      nsAutoCString spec;
      if (proxy->GetWorkerPrivate()->GetBaseURI()) {
        proxy->GetWorkerPrivate()->GetBaseURI()->GetAsciiSpec(spec);
      }
      fetch->SetWorkerScript(spec);

      fetch->SetClientInfo(mClientInfo);
      fetch->SetController(mController);
      fetch->SetCSPEventListener(mCSPEventListener);
    }

    fetch->SetOriginStack(std::move(mOriginStack));

    RefPtr<AbortSignalImpl> signalImpl =
        mResolver->GetAbortSignalForMainThread();

    // ...but release it before calling Fetch, because mResolver's callback can
    // be called synchronously and they want the mutex, too.
    return fetch->Fetch(signalImpl, mResolver);
  }
};

already_AddRefed<Promise> FetchRequest(nsIGlobalObject* aGlobal,
                                       const RequestOrUTF8String& aInput,
                                       const RequestInit& aInit,
                                       CallerType aCallerType,
                                       ErrorResult& aRv) {
  RefPtr<Promise> p = Promise::Create(aGlobal, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  MOZ_ASSERT(aGlobal);

  // Double check that we have chrome privileges if the Request's content
  // policy type has been overridden.
  MOZ_ASSERT_IF(aInput.IsRequest() &&
                    aInput.GetAsRequest().IsContentPolicyTypeOverridden(),
                aCallerType == CallerType::System);

  AutoJSAPI jsapi;
  if (!jsapi.Init(aGlobal)) {
    aRv.Throw(NS_ERROR_NOT_AVAILABLE);
    return nullptr;
  }

  JSContext* cx = jsapi.cx();
  JS::Rooted<JSObject*> jsGlobal(cx, aGlobal->GetGlobalJSObject());
  GlobalObject global(cx, jsGlobal);

  SafeRefPtr<Request> request =
      Request::Constructor(global, aInput, aInit, aRv);
  if (aRv.Failed()) {
    return nullptr;
  }

  SafeRefPtr<InternalRequest> internalRequest = request->GetInternalRequest();

  // Restore information of InterceptedHttpChannel if they are passed with the
  // Request. Since Request::Constructor would not copy these members.
  if (aInput.IsRequest()) {
    RefPtr<Request> inputReq = &aInput.GetAsRequest();
    SafeRefPtr<InternalRequest> inputInReq = inputReq->GetInternalRequest();
    if (inputInReq->GetInterceptionTriggeringPrincipalInfo()) {
      internalRequest->SetInterceptionContentPolicyType(
          inputInReq->InterceptionContentPolicyType());
      internalRequest->SetInterceptionTriggeringPrincipalInfo(
          std::make_unique<mozilla::ipc::PrincipalInfo>(
              *(inputInReq->GetInterceptionTriggeringPrincipalInfo().get())));
      if (!inputInReq->InterceptionRedirectChain().IsEmpty()) {
        internalRequest->SetInterceptionRedirectChain(
            inputInReq->InterceptionRedirectChain());
      }
      internalRequest->SetInterceptionFromThirdParty(
          inputInReq->InterceptionFromThirdParty());
    }
  }

  RefPtr<AbortSignalImpl> signalImpl = request->GetSignalImpl();

  if (signalImpl && signalImpl->Aborted()) {
    // Already aborted signal rejects immediately.
    JS::Rooted<JS::Value> reason(cx);
    signalImpl->GetReason(cx, &reason);
    request->CancelBody(cx, IgnoredErrorResult(), reason);
    p->MaybeReject(reason);
    return p.forget();
  }

  nsCOMPtr<nsIInputStream> requestBody;
  internalRequest->GetBody(getter_AddRefs(requestBody));
  if (requestBody) {
    request->SetBodyUsed(cx, aRv);
    if (aRv.Failed()) {
      return nullptr;
    }
    request->FollowBodySignal();
  }

  JS::Realm* realm = JS::GetCurrentRealmOrNull(cx);
  if (realm && JS::GetDebuggerObservesWasm(realm)) {
    internalRequest->SetSkipWasmCaching();
  }

  RefPtr<FetchObserver> observer;
  if (aInit.mObserve.WasPassed()) {
    observer = new FetchObserver(aGlobal, signalImpl);
    aInit.mObserve.Value().HandleEvent(*observer);
  }

  nsCOMPtr<nsICookieJarSettings> cookieJarSettings;
  if (internalRequest->GetCookieJarSettings()) {
    cookieJarSettings = internalRequest->GetCookieJarSettings();
  }

  if (NS_IsMainThread() && !internalRequest->GetKeepalive()) {
    nsCOMPtr<nsPIDOMWindowInner> window = do_QueryInterface(aGlobal);
    nsCOMPtr<Document> doc;
    nsCOMPtr<nsILoadGroup> loadGroup;
    nsIPrincipal* principal;
    net::ClassificationFlags trackingFlags = {0, 0};
    if (window) {
      doc = window->GetExtantDoc();
      if (!doc) {
        aRv.Throw(NS_ERROR_FAILURE);
        return nullptr;
      }
      principal = doc->NodePrincipal();
      loadGroup = doc->GetDocumentLoadGroup();
      cookieJarSettings = doc->CookieJarSettings();

      trackingFlags = doc->GetScriptTrackingFlags();
    } else {
      principal = aGlobal->PrincipalOrNull();
      if (NS_WARN_IF(!principal)) {
        aRv.Throw(NS_ERROR_FAILURE);
        return nullptr;
      }

      if (!cookieJarSettings) {
        cookieJarSettings = mozilla::net::CookieJarSettings::Create(principal);
      }
    }

    if (!loadGroup) {
      nsresult rv = NS_NewLoadGroup(getter_AddRefs(loadGroup), principal);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        aRv.Throw(rv);
        return nullptr;
      }
    }

    RefPtr<MainThreadFetchResolver> resolver = new MainThreadFetchResolver(
        p, observer, signalImpl, request->MozErrors());
    uint64_t associatedBc = 0;
    if (internalRequest) {
      associatedBc = internalRequest->AssociatedBrowsingContextID();
    }
    RefPtr<FetchDriver> fetch =
        new FetchDriver(std::move(internalRequest), principal, loadGroup,
                        aGlobal->SerialEventTarget(), cookieJarSettings,
                        nullptr,  // PerformanceStorage
                        trackingFlags);
    if (associatedBc > 0) {
      fetch->SetAssociatedBrowsingContextID(associatedBc);
    }
    fetch->SetDocument(doc);
    resolver->SetLoadGroup(loadGroup);
    aRv = fetch->Fetch(signalImpl, resolver);
    if (NS_WARN_IF(aRv.Failed())) {
      return nullptr;
    }
  } else if (NS_IsMainThread() && internalRequest->GetKeepalive()) {
    // keepalive is set to true, route the request through PFetch
    // We plan to route all main-thread fetch request through PFetch.
    // See Bug 1897129.

    uint64_t bodyLength =
        internalRequest->BodyLength() > 0 ? internalRequest->BodyLength() : 0;

    nsCOMPtr<nsILoadGroup> loadGroup =
        FetchUtil::GetLoadGroupFromGlobal(aGlobal);

    if (loadGroup && !FetchUtil::IncrementPendingKeepaliveRequestSize(
                         loadGroup, bodyLength)) {
      p->MaybeRejectWithTypeError<MSG_FETCH_FAILED>();
      return p.forget();
    };

    if (!loadGroup) {
      // if there is no load group for this request ensure that the request
      // size does not exceed FETCH_KEEPALIVE_MAX_SIZE
      if (bodyLength > FETCH_KEEPALIVE_MAX_SIZE) {
        p->MaybeRejectWithTypeError<MSG_FETCH_FAILED>();
        return p.forget();
      }
    }

    RefPtr<FetchChild> actor =
        FetchChild::CreateForMainThread(p, signalImpl, observer);
    if (!actor) {
      NS_WARNING("Could not start keepalive request.");
      aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
      return nullptr;
    }

    Maybe<ClientInfo> clientInfo(aGlobal->GetClientInfo());
    if (clientInfo.isNothing()) {
      aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
      return nullptr;
    }

    auto* backgroundChild =
        mozilla::ipc::BackgroundChild::GetOrCreateForCurrentThread();
    (void)NS_WARN_IF(!backgroundChild->SendPFetchConstructor(actor));

    nsCOMPtr<nsPIDOMWindowInner> window = do_QueryInterface(aGlobal);
    nsCOMPtr<Document> doc;
    nsIPrincipal* principal;
    bool isThirdPartyContext = false;
    // we don't check if we this request is invoked from a tracking script
    // we might add this capability in future.
    // See Bug 1892406
    if (window) {
      doc = window->GetExtantDoc();
      if (!doc) {
        aRv.Throw(NS_ERROR_FAILURE);
        return nullptr;
      }
      principal = doc->NodePrincipal();
      cookieJarSettings = doc->CookieJarSettings();
      // fetch the thirdparty context from the document

      ThirdPartyUtil* thirdPartyUtil = ThirdPartyUtil::GetInstance();
      if (!thirdPartyUtil) {
        aRv.Throw(NS_ERROR_FAILURE);
        return nullptr;
      }
      (void)thirdPartyUtil->IsThirdPartyWindow(window->GetOuterWindow(),
                                               nullptr, &isThirdPartyContext);
    } else {
      principal = aGlobal->PrincipalOrNull();
      if (NS_WARN_IF(!principal)) {
        aRv.Throw(NS_ERROR_FAILURE);
        return nullptr;
      }

      if (!cookieJarSettings) {
        cookieJarSettings = mozilla::net::CookieJarSettings::Create(principal);
      }
    }

    Maybe<net::CookieJarSettingsArgs> cookieJarSettingsArgs;
    if (cookieJarSettings) {
      net::CookieJarSettingsArgs csArgs;
      net::CookieJarSettings::Cast(cookieJarSettings)->Serialize(csArgs);
      cookieJarSettingsArgs = Some(csArgs);
    }

    uint64_t associatedBrowsingContextID = 0;
    if (window && window->GetBrowsingContext()) {
      associatedBrowsingContextID = window->GetBrowsingContext()->Id();
    }

    FetchOpArgs ipcArgs(
        IPCInternalRequest(), WrapNotNull(principal), /* workerScript */ ""_ns,
        clientInfo.ref().ToIPC(), /* controller */ Nothing(),
        std::move(cookieJarSettingsArgs), /* needOnDataAvailable */ false,
        /* hasCSPEventListener */ false, associatedBrowsingContextID,
        isThirdPartyContext, /* isWorkerRequest */ false,
        /* isOn3PCBExceptionList */ false);
    internalRequest->ToIPCInternalRequest(&(ipcArgs.request()),
                                          backgroundChild);

    UniquePtr<SerializedStackHolder> stack = GetCurrentStackForNetMonitor(cx);
    if (stack) {
      actor->SetOriginStack(std::move(stack));
    }

    actor->DoFetchOp(ipcArgs);

    return p.forget();
  } else {
    WorkerPrivate* worker = GetCurrentThreadWorkerPrivate();
    MOZ_ASSERT(worker);

    if (worker->IsServiceWorker()) {
      internalRequest->SetSkipServiceWorker();
    }

    // PFetch gives no benefit for the fetch in the parent process.
    // Dispatch fetch to the parent process main thread directly for that case.
    // For child process, dispatch fetch op to the parent.
    if (StaticPrefs::dom_workers_pFetch_enabled() && !XRE_IsParentProcess()) {
      if (internalRequest->GetKeepalive()) {
        uint64_t bodyLength = internalRequest->BodyLength() > 0
                                  ? internalRequest->BodyLength()
                                  : 0;

        // We differ from the fetch spec and main thread fetch here.
        // We do not limit the keepalive size per loadgroup(but instead per
        // request). This is due to the fact that loadgroup is not accessible on
        // the worker thread and we dont want to introduce async to introduce
        // this check.
        if (bodyLength > FETCH_KEEPALIVE_MAX_SIZE) {
          p->MaybeRejectWithTypeError<MSG_FETCH_FAILED>();
          return p.forget();
        }
      }

      RefPtr<FetchChild> actor =
          FetchChild::CreateForWorker(worker, p, signalImpl, observer);
      if (!actor) {
        NS_WARNING("Could not keep the worker alive.");
        aRv.Throw(NS_ERROR_DOM_ABORT_ERR);
        return nullptr;
      }

      Maybe<ClientInfo> clientInfo(worker->GlobalScope()->GetClientInfo());
      if (clientInfo.isNothing()) {
        aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
        return nullptr;
      }

      auto* backgroundChild =
          mozilla::ipc::BackgroundChild::GetOrCreateForCurrentThread();
      (void)NS_WARN_IF(!backgroundChild->SendPFetchConstructor(actor));

      nsAutoCString workerScript;
      if (worker->GetBaseURI()) {
        worker->GetBaseURI()->GetAsciiSpec(workerScript);
      }
      Maybe<IPCServiceWorkerDescriptor> controller;
      if (worker->GlobalScope()->GetController().isSome()) {
        controller = Some(worker->GlobalScope()->GetController().ref().ToIPC());
      }
      Maybe<net::CookieJarSettingsArgs> cookieJarSettingsArgs;
      if (worker->CookieJarSettings()) {
        cookieJarSettingsArgs = Some(worker->CookieJarSettingsArgs());
      }
      bool hasCSPEventListener = !!worker->CSPEventListener();
      if (hasCSPEventListener) {
        actor->SetCSPEventListener(worker->CSPEventListener());
      }

      FetchOpArgs ipcArgs(
          IPCInternalRequest(), WrapNotNull(worker->GetPrincipal()),
          std::move(workerScript), clientInfo.ref().ToIPC(),
          std::move(controller), std::move(cookieJarSettingsArgs),
          /* needOnDataAvailable */ false, hasCSPEventListener,
          worker->AssociatedBrowsingContextID(), worker->IsThirdPartyContext(),
          /* isWorkerRequest */ true, worker->IsOn3PCBExceptionList());
      internalRequest->ToIPCInternalRequest(&(ipcArgs.request()),
                                            backgroundChild);

      if (worker->IsWatchedByDevTools()) {
        UniquePtr<SerializedStackHolder> stack;
        stack = GetCurrentStackForNetMonitor(cx);
        actor->SetOriginStack(std::move(stack));
      }

      actor->DoFetchOp(ipcArgs);

      return p.forget();
    }
    // Dispatch worker fetch to the main thread
    // We do not check if keepalive flag is set for ChromeWorkers
    // See Bug 1898664
    RefPtr<WorkerFetchResolver> resolver =
        WorkerFetchResolver::Create(worker, p, signalImpl, observer);
    if (!resolver) {
      NS_WARNING("Could not keep the worker alive.");
      aRv.Throw(NS_ERROR_DOM_ABORT_ERR);
      return nullptr;
    }

    Maybe<ClientInfo> clientInfo(worker->GlobalScope()->GetClientInfo());
    if (clientInfo.isNothing()) {
      aRv.Throw(NS_ERROR_DOM_INVALID_STATE_ERR);
      return nullptr;
    }

    UniquePtr<SerializedStackHolder> stack;
    if (worker->IsWatchedByDevTools()) {
      stack = GetCurrentStackForNetMonitor(cx);
    }

    RefPtr<MainThreadFetchRunnable> run = new MainThreadFetchRunnable(
        resolver, clientInfo.ref(), worker->GlobalScope()->GetController(),
        worker->CSPEventListener(), std::move(internalRequest),
        std::move(stack));
    worker->DispatchToMainThread(run.forget());
  }

  return p.forget();
}

class ResolveFetchPromise : public Runnable {
 public:
  ResolveFetchPromise(Promise* aPromise, Response* aResponse)
      : Runnable("ResolveFetchPromise"),
        mPromise(aPromise),
        mResponse(aResponse) {}

  NS_IMETHOD Run() override {
    mPromise->MaybeResolve(mResponse);
    return NS_OK;
  }
  RefPtr<Promise> mPromise;
  RefPtr<Response> mResponse;
};

void MainThreadFetchResolver::OnResponseAvailableInternal(
    SafeRefPtr<InternalResponse> aResponse) {
  NS_ASSERT_OWNINGTHREAD(MainThreadFetchResolver);
  AssertIsOnMainThread();

  if (aResponse->Type() != ResponseType::Error) {
    nsCOMPtr<nsIGlobalObject> go = mPromise->GetParentObject();
    nsCOMPtr<nsPIDOMWindowInner> inner = do_QueryInterface(go);

    // Notify the document when a fetch completes successfully. This is
    // used by the password manager as a hint to observe DOM mutations.
    // Call this prior to setting state to Complete so we can set up the
    // observer before mutations occurs.
    const RefPtr<Document> doc = inner ? inner->GetExtantDoc() : nullptr;
    if (doc) {
      doc->NotifyFetchOrXHRSuccess();
    }

    if (mFetchObserver) {
      mFetchObserver->SetState(FetchState::Complete);
    }

    mResponse = new Response(go, std::move(aResponse), mSignalImpl);
    // response headers received from the network should be immutable
    // all response header settings must be done before this point
    // see Bug 1574174
    ErrorResult result;
    mResponse->Headers_()->SetGuard(HeadersGuardEnum::Immutable, result);
    MOZ_ASSERT(!result.Failed());

    BrowsingContext* bc = inner ? inner->GetBrowsingContext() : nullptr;
    bc = bc ? bc->Top() : nullptr;
    if (bc && bc->IsLoading()) {
      bc->AddDeprioritizedLoadRunner(
          new ResolveFetchPromise(mPromise, mResponse));
    } else {
      mPromise->MaybeResolve(mResponse);
    }
  } else {
    if (mFetchObserver) {
      mFetchObserver->SetState(FetchState::Errored);
    }

    if (mMozErrors) {
      mPromise->MaybeReject(aResponse->GetErrorCode());
      return;
    }

    mPromise->MaybeRejectWithTypeError<MSG_FETCH_FAILED>();
  }
}

bool MainThreadFetchResolver::NeedOnDataAvailable() {
  NS_ASSERT_OWNINGTHREAD(MainThreadFetchResolver);
  return !!mFetchObserver;
}

void MainThreadFetchResolver::OnDataAvailable() {
  NS_ASSERT_OWNINGTHREAD(MainThreadFetchResolver);
  AssertIsOnMainThread();

  if (!mFetchObserver) {
    return;
  }

  if (mFetchObserver->State() == FetchState::Requesting) {
    mFetchObserver->SetState(FetchState::Responding);
  }
}

MainThreadFetchResolver::~MainThreadFetchResolver() {
  NS_ASSERT_OWNINGTHREAD(MainThreadFetchResolver);
}

class WorkerFetchResponseRunnable final : public MainThreadWorkerRunnable {
  RefPtr<WorkerFetchResolver> mResolver;
  // Passed from main thread to worker thread after being initialized.
  SafeRefPtr<InternalResponse> mInternalResponse;

 public:
  WorkerFetchResponseRunnable(WorkerPrivate* aWorkerPrivate,
                              WorkerFetchResolver* aResolver,
                              SafeRefPtr<InternalResponse> aResponse)
      : MainThreadWorkerRunnable("WorkerFetchResponseRunnable"),
        mResolver(aResolver),
        mInternalResponse(std::move(aResponse)) {
    MOZ_ASSERT(mResolver);
  }

  bool WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();
    if (mResolver->IsShutdown(aWorkerPrivate)) {
      return true;
    }

    RefPtr<Promise> promise = mResolver->WorkerPromise(aWorkerPrivate);
    // Once Worker had already started shutdown, workerPromise would be nullptr
    if (!promise) {
      return true;
    }
    RefPtr<FetchObserver> fetchObserver =
        mResolver->GetFetchObserver(aWorkerPrivate);

    if (mInternalResponse->Type() != ResponseType::Error) {
      if (fetchObserver) {
        fetchObserver->SetState(FetchState::Complete);
      }

      RefPtr<nsIGlobalObject> global = aWorkerPrivate->GlobalScope();
      RefPtr<Response> response =
          new Response(global, mInternalResponse.clonePtr(),
                       mResolver->GetAbortSignalForTargetThread());

      // response headers received from the network should be immutable,
      // all response header settings must be done before this point
      // see Bug 1574174
      ErrorResult result;
      response->Headers_()->SetGuard(HeadersGuardEnum::Immutable, result);
      MOZ_ASSERT(!result.Failed());

      promise->MaybeResolve(response);
    } else {
      if (fetchObserver) {
        fetchObserver->SetState(FetchState::Errored);
      }

      promise->MaybeRejectWithTypeError<MSG_FETCH_FAILED>();
    }
    return true;
  }
};

class WorkerDataAvailableRunnable final : public MainThreadWorkerRunnable {
  RefPtr<WorkerFetchResolver> mResolver;

 public:
  WorkerDataAvailableRunnable(WorkerPrivate* aWorkerPrivate,
                              WorkerFetchResolver* aResolver)
      : MainThreadWorkerRunnable("WorkerDataAvailableRunnable"),
        mResolver(aResolver) {}

  bool WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    MOZ_ASSERT(aWorkerPrivate);
    aWorkerPrivate->AssertIsOnWorkerThread();

    RefPtr<FetchObserver> fetchObserver =
        mResolver->GetFetchObserver(aWorkerPrivate);

    if (fetchObserver && fetchObserver->State() == FetchState::Requesting) {
      fetchObserver->SetState(FetchState::Responding);
    }

    return true;
  }
};

class WorkerFetchResponseEndBase {
 protected:
  RefPtr<WorkerFetchResolver> mResolver;

 public:
  explicit WorkerFetchResponseEndBase(WorkerFetchResolver* aResolver)
      : mResolver(aResolver) {
    MOZ_ASSERT(aResolver);
  }

  void WorkerRunInternal(WorkerPrivate* aWorkerPrivate) {
    mResolver->Shutdown(aWorkerPrivate);
  }
};

class WorkerFetchResponseEndRunnable final : public MainThreadWorkerRunnable,
                                             public WorkerFetchResponseEndBase {
  FetchDriverObserver::EndReason mReason;

 public:
  WorkerFetchResponseEndRunnable(WorkerPrivate* aWorkerPrivate,
                                 WorkerFetchResolver* aResolver,
                                 FetchDriverObserver::EndReason aReason)
      : MainThreadWorkerRunnable("WorkerFetchResponseEndRunnable"),
        WorkerFetchResponseEndBase(aResolver),
        mReason(aReason) {}

  bool WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    if (mResolver->IsShutdown(aWorkerPrivate)) {
      return true;
    }

    if (mReason == FetchDriverObserver::eAborted) {
      mResolver->WorkerPromise(aWorkerPrivate)
          ->MaybeReject(NS_ERROR_DOM_ABORT_ERR);
    }

    WorkerRunInternal(aWorkerPrivate);
    return true;
  }

  nsresult Cancel() override { return Run(); }
};

class WorkerFetchResponseEndControlRunnable final
    : public MainThreadWorkerControlRunnable,
      public WorkerFetchResponseEndBase {
 public:
  WorkerFetchResponseEndControlRunnable(WorkerPrivate* aWorkerPrivate,
                                        WorkerFetchResolver* aResolver)
      : MainThreadWorkerControlRunnable(
            "WorkerFetchResponseEndControlRunnable"),
        WorkerFetchResponseEndBase(aResolver) {}

  bool WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    WorkerRunInternal(aWorkerPrivate);
    return true;
  }

  // Control runnable cancel already calls Run().
};

void WorkerFetchResolver::OnResponseAvailableInternal(
    SafeRefPtr<InternalResponse> aResponse) {
  AssertIsOnMainThread();

  MutexAutoLock lock(mPromiseProxy->Lock());
  if (mPromiseProxy->CleanedUp()) {
    return;
  }

  RefPtr<WorkerFetchResponseRunnable> r = new WorkerFetchResponseRunnable(
      mPromiseProxy->GetWorkerPrivate(), this, std::move(aResponse));

  if (!r->Dispatch(mPromiseProxy->GetWorkerPrivate())) {
    NS_WARNING("Could not dispatch fetch response");
  }
}

bool WorkerFetchResolver::NeedOnDataAvailable() {
  AssertIsOnMainThread();
  return mNeedOnDataAvailable;
}

void WorkerFetchResolver::OnDataAvailable() {
  AssertIsOnMainThread();

  MutexAutoLock lock(mPromiseProxy->Lock());
  if (mPromiseProxy->CleanedUp()) {
    return;
  }

  RefPtr<WorkerDataAvailableRunnable> r =
      new WorkerDataAvailableRunnable(mPromiseProxy->GetWorkerPrivate(), this);
  (void)r->Dispatch(mPromiseProxy->GetWorkerPrivate());
}

void WorkerFetchResolver::OnResponseEnd(FetchDriverObserver::EndReason aReason,
                                        JS::Handle<JS::Value> aReasonDetails) {
  AssertIsOnMainThread();
  MutexAutoLock lock(mPromiseProxy->Lock());
  if (mPromiseProxy->CleanedUp()) {
    return;
  }

  FlushConsoleReport();

  (void)aReasonDetails;

  RefPtr<WorkerFetchResponseEndRunnable> r = new WorkerFetchResponseEndRunnable(
      mPromiseProxy->GetWorkerPrivate(), this, aReason);

  if (!r->Dispatch(mPromiseProxy->GetWorkerPrivate())) {
    RefPtr<WorkerFetchResponseEndControlRunnable> cr =
        new WorkerFetchResponseEndControlRunnable(
            mPromiseProxy->GetWorkerPrivate(), this);
    // This can fail if the worker thread is canceled or killed causing
    // the PromiseWorkerProxy to give up its WorkerRef immediately,
    // allowing the worker thread to become Dead.
    if (!cr->Dispatch(mPromiseProxy->GetWorkerPrivate())) {
      NS_WARNING("Failed to dispatch WorkerFetchResponseEndControlRunnable");
    }
  }
}

void WorkerFetchResolver::FlushConsoleReport() {
  AssertIsOnMainThread();
  MOZ_ASSERT(mPromiseProxy);

  if (!mReporter) {
    return;
  }

  WorkerPrivate* worker = mPromiseProxy->GetWorkerPrivate();
  if (!worker) {
    mReporter->FlushReportsToConsole(0);
    return;
  }

  if (worker->IsServiceWorker()) {
    // Flush to service worker
    mReporter->FlushReportsToConsoleForServiceWorkerScope(
        worker->ServiceWorkerScope());
    return;
  }

  if (worker->IsSharedWorker()) {
    // Flush to shared worker
    worker->GetRemoteWorkerController()->FlushReportsOnMainThread(mReporter);
    return;
  }

  // Flush to dedicated worker
  mReporter->FlushConsoleReports(worker->GetLoadGroup());
}

nsresult ExtractByteStreamFromBody(const fetch::OwningBodyInit& aBodyInit,
                                   nsIInputStream** aStream,
                                   nsCString& aContentTypeWithCharset,
                                   uint64_t& aContentLength) {
  MOZ_ASSERT(aStream);
  MOZ_ASSERT(!aBodyInit.IsReadableStream(),
             "ReadableStream bodies should be handled before calling "
             "ExtractByteStreamFromBody");
  if (aBodyInit.IsReadableStream()) {
    return NS_ERROR_INVALID_ARG;
  }
  nsAutoCString charset;
  aContentTypeWithCharset.SetIsVoid(true);

  if (aBodyInit.IsArrayBuffer()) {
    BodyExtractor<const ArrayBuffer> body(&aBodyInit.GetAsArrayBuffer());
    return body.GetAsStream(aStream, &aContentLength, aContentTypeWithCharset,
                            charset);
  }

  if (aBodyInit.IsArrayBufferView()) {
    BodyExtractor<const ArrayBufferView> body(
        &aBodyInit.GetAsArrayBufferView());
    return body.GetAsStream(aStream, &aContentLength, aContentTypeWithCharset,
                            charset);
  }

  if (aBodyInit.IsBlob()) {
    Blob& blob = aBodyInit.GetAsBlob();
    BodyExtractor<const Blob> body(&blob);
    return body.GetAsStream(aStream, &aContentLength, aContentTypeWithCharset,
                            charset);
  }

  if (aBodyInit.IsFormData()) {
    FormData& formData = aBodyInit.GetAsFormData();
    BodyExtractor<const FormData> body(&formData);
    return body.GetAsStream(aStream, &aContentLength, aContentTypeWithCharset,
                            charset);
  }

  if (aBodyInit.IsUSVString()) {
    BodyExtractor<const nsAString> body(&aBodyInit.GetAsUSVString());
    return body.GetAsStream(aStream, &aContentLength, aContentTypeWithCharset,
                            charset);
  }

  if (aBodyInit.IsURLSearchParams()) {
    URLSearchParams& usp = aBodyInit.GetAsURLSearchParams();
    BodyExtractor<const URLSearchParams> body(&usp);
    return body.GetAsStream(aStream, &aContentLength, aContentTypeWithCharset,
                            charset);
  }

  MOZ_ASSERT_UNREACHABLE("Should never reach here");
  return NS_ERROR_FAILURE;
}

nsresult ExtractByteStreamFromBody(const fetch::BodyInit& aBodyInit,
                                   nsIInputStream** aStream,
                                   nsCString& aContentTypeWithCharset,
                                   uint64_t& aContentLength) {
  MOZ_ASSERT(aStream);
  MOZ_ASSERT(!*aStream);
  MOZ_ASSERT(!aBodyInit.IsReadableStream(),
             "ReadableStream bodies should be handled before calling "
             "ExtractByteStreamFromBody");
  if (aBodyInit.IsReadableStream()) {
    return NS_ERROR_INVALID_ARG;
  }

  nsAutoCString charset;
  aContentTypeWithCharset.SetIsVoid(true);

  if (aBodyInit.IsArrayBuffer()) {
    BodyExtractor<const ArrayBuffer> body(&aBodyInit.GetAsArrayBuffer());
    return body.GetAsStream(aStream, &aContentLength, aContentTypeWithCharset,
                            charset);
  }

  if (aBodyInit.IsArrayBufferView()) {
    BodyExtractor<const ArrayBufferView> body(
        &aBodyInit.GetAsArrayBufferView());
    return body.GetAsStream(aStream, &aContentLength, aContentTypeWithCharset,
                            charset);
  }

  if (aBodyInit.IsBlob()) {
    BodyExtractor<const Blob> body(&aBodyInit.GetAsBlob());
    return body.GetAsStream(aStream, &aContentLength, aContentTypeWithCharset,
                            charset);
  }

  if (aBodyInit.IsFormData()) {
    BodyExtractor<const FormData> body(&aBodyInit.GetAsFormData());
    return body.GetAsStream(aStream, &aContentLength, aContentTypeWithCharset,
                            charset);
  }

  if (aBodyInit.IsUSVString()) {
    BodyExtractor<const nsAString> body(&aBodyInit.GetAsUSVString());
    return body.GetAsStream(aStream, &aContentLength, aContentTypeWithCharset,
                            charset);
  }

  if (aBodyInit.IsURLSearchParams()) {
    BodyExtractor<const URLSearchParams> body(
        &aBodyInit.GetAsURLSearchParams());
    return body.GetAsStream(aStream, &aContentLength, aContentTypeWithCharset,
                            charset);
  }

  MOZ_ASSERT_UNREACHABLE("Should never reach here");
  return NS_ERROR_FAILURE;
}

NS_IMPL_CYCLE_COLLECTION(FetchBodyBase, mReadableStreamBody)

NS_IMPL_CYCLE_COLLECTING_ADDREF(FetchBodyBase)
NS_IMPL_CYCLE_COLLECTING_RELEASE(FetchBodyBase)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(FetchBodyBase)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

template <class Derived>
FetchBody<Derived>::FetchBody(nsIGlobalObject* aGlobal)
    : mGlobal(aGlobal), mBodyUsed(false) {
  MOZ_ASSERT(aGlobal);

  if (!NS_IsMainThread()) {
    WorkerPrivate* wp = GetCurrentThreadWorkerPrivate();
    MOZ_ASSERT(wp);
    mMainThreadEventTarget = wp->MainThreadEventTarget();
  } else {
    mMainThreadEventTarget = GetMainThreadSerialEventTarget();
  }

  MOZ_ASSERT(mMainThreadEventTarget);
}

template FetchBody<Request>::FetchBody(nsIGlobalObject* aGlobal);

template FetchBody<Response>::FetchBody(nsIGlobalObject* aGlobal);

template <class Derived>
FetchBody<Derived>::~FetchBody() {
  Unfollow();
}

template FetchBody<Request>::~FetchBody();

template FetchBody<Response>::~FetchBody();

template <class Derived>
bool FetchBody<Derived>::BodyUsed() const {
  if (mBodyUsed) {
    return true;
  }

  // If this stream is disturbed, return true.
  if (mReadableStreamBody) {
    return mReadableStreamBody->Disturbed();
  }

  return false;
}

template bool FetchBody<Request>::BodyUsed() const;

template bool FetchBody<Response>::BodyUsed() const;

template <class Derived>
bool FetchBody<Derived>::IsBodyUnusable() const {
  if (BodyUsed()) {
    return true;
  }
  return mReadableStreamBody && mReadableStreamBody->Locked();
}

template bool FetchBody<Request>::IsBodyUnusable() const;

template bool FetchBody<Response>::IsBodyUnusable() const;

template <class Derived>
void FetchBody<Derived>::CancelBody(JSContext* aCx, ErrorResult& aRv,
                                    JS::Handle<JS::Value> aReason) {
  MOZ_ASSERT(aCx);
  MOZ_ASSERT(mGlobal->SerialEventTarget()->IsOnCurrentThread());

  if (mReadableStreamBody) {
    // Only a locked stream cannot be cancelled; a disturbed one still can.
    if (!mReadableStreamBody->Locked()) {
      RefPtr<ReadableStream> body = mReadableStreamBody;
      // The returned promise only reports the cancel's completion, which
      // nothing here waits on.
      RefPtr<Promise> cancelPromise = body->Cancel(aCx, aReason, aRv);
      if (cancelPromise) {
        MOZ_ALWAYS_TRUE(cancelPromise->SetAnyPromiseIsHandled());
      }
    }
  } else {
    nsCOMPtr<nsIInputStream> stream;
    DerivedClass()->GetBody(getter_AddRefs(stream));
    if (stream) {
      stream->Close();
    }
  }

  // The body is spent even if the underlying source's cancel algorithm threw,
  // so a failure reported through aRv still leaves it unusable.
  mBodyUsed = true;
}

template void FetchBody<Request>::CancelBody(JSContext* aCx, ErrorResult& aRv,
                                             JS::Handle<JS::Value> aReason);

template void FetchBody<Response>::CancelBody(JSContext* aCx, ErrorResult& aRv,
                                              JS::Handle<JS::Value> aReason);

template <class Derived>
void FetchBody<Derived>::SetBodyUsed(JSContext* aCx, ErrorResult& aRv) {
  MOZ_ASSERT(aCx);
  MOZ_ASSERT(mGlobal->SerialEventTarget()->IsOnCurrentThread());

  MOZ_DIAGNOSTIC_ASSERT(!BodyUsed(), "Consuming already used body?");
  if (BodyUsed()) {
    return;
  }

  mBodyUsed = true;

  // If we already have a ReadableStreamBody and it has been created by DOM, we
  // have to lock it now because it can have been shared with other objects.
  if (mReadableStreamBody) {
    if (mFetchStreamReader) {
      // Having FetchStreamReader means there's no nsIInputStream underlying it
      MOZ_ASSERT(!mReadableStreamBody->MaybeGetInputStreamIfUnread());
      if (!mFetchStreamReader->IsConsuming()) {
        mFetchStreamReader->StartConsuming(aCx, mReadableStreamBody, aRv);
      }
      return;
    }
    // We should have nsIInputStream at this point as long as it's still
    // readable
    MOZ_ASSERT_IF(
        mReadableStreamBody->State() == ReadableStream::ReaderState::Readable,
        mReadableStreamBody->MaybeGetInputStreamIfUnread());
    LockStream(aCx, mReadableStreamBody, aRv);
  }
}

template void FetchBody<Request>::SetBodyUsed(JSContext* aCx, ErrorResult& aRv);

template void FetchBody<Response>::SetBodyUsed(JSContext* aCx,
                                               ErrorResult& aRv);

template <class Derived>
already_AddRefed<Promise> FetchBody<Derived>::ConsumeBody(
    JSContext* aCx, BodyConsumer::ConsumeType aType, ErrorResult& aRv) {
  aRv.MightThrowJSException();

  RefPtr<AbortSignalImpl> signalImpl =
      DerivedClass()->GetSignalImplToConsumeBody();

  if (signalImpl && signalImpl->Aborted()) {
    JS::Rooted<JS::Value> abortReason(aCx);
    signalImpl->GetReason(aCx, &abortReason);

    nsCOMPtr<nsIGlobalObject> global = DerivedClass()->GetParentObject();
    RefPtr<Promise> promise = Promise::Create(global, aRv);
    if (aRv.Failed()) {
      return nullptr;
    }

    promise->MaybeReject(abortReason);
    return promise.forget();
  }

  if (IsBodyUnusable()) {
    aRv.ThrowTypeError<MSG_FETCH_BODY_CONSUMED_ERROR>();
    return nullptr;
  }

  nsAutoCString mimeType;
  nsAutoCString mixedCaseMimeType;
  DerivedClass()->GetMimeType(mimeType, mixedCaseMimeType);

  // Null bodies are a special-case in the fetch spec.  The Body mix-in can only
  // be "disturbed" or "locked" if its associated "body" is non-null.
  // Additionally, the Body min-in's "consume body" algorithm explicitly creates
  // a fresh empty ReadableStream object in step 2.  This means that `bodyUsed`
  // will never return true for a null body.
  //
  // To this end, we create a fresh (empty) body every time a request is made
  // and consume its body here, without marking this FetchBody consumed via
  // SetBodyUsed.
  nsCOMPtr<nsIInputStream> bodyStream;
  DerivedClass()->GetBody(getter_AddRefs(bodyStream));
  if (!bodyStream) {
    RefPtr<EmptyBody> emptyBody =
        EmptyBody::Create(DerivedClass()->GetParentObject(),
                          DerivedClass()->GetPrincipalInfo().get(), signalImpl,
                          mimeType, mixedCaseMimeType, aRv);
    if (NS_WARN_IF(aRv.Failed())) {
      return nullptr;
    }

    return emptyBody->ConsumeBody(aCx, aType, aRv);
  }

  SetBodyUsed(aCx, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  nsCOMPtr<nsIGlobalObject> global = DerivedClass()->GetParentObject();

  MutableBlobStorage::MutableBlobStorageType blobStorageType =
      MutableBlobStorage::eOnlyInMemory;
  const mozilla::UniquePtr<mozilla::ipc::PrincipalInfo>& principalInfo =
      DerivedClass()->GetPrincipalInfo();
  // We support temporary file for blobs only if the principal is known and
  // it's system or content not in private Browsing.
  if (principalInfo &&
      (principalInfo->type() ==
           mozilla::ipc::PrincipalInfo::TSystemPrincipalInfo ||
       (principalInfo->type() ==
            mozilla::ipc::PrincipalInfo::TContentPrincipalInfo &&
        !principalInfo->get_ContentPrincipalInfo()
             .attrs()
             .IsPrivateBrowsing()))) {
    blobStorageType = MutableBlobStorage::eCouldBeInTemporaryFile;
  }

  RefPtr<Promise> promise =
      BodyConsumer::Create(global, mMainThreadEventTarget, bodyStream,
                           signalImpl, aType, BodyBlobImpl(), BodyLocalPath(),
                           mimeType, mixedCaseMimeType, blobStorageType, aRv);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  return promise.forget();
}

template already_AddRefed<Promise> FetchBody<Request>::ConsumeBody(
    JSContext* aCx, BodyConsumer::ConsumeType aType, ErrorResult& aRv);

template already_AddRefed<Promise> FetchBody<Response>::ConsumeBody(
    JSContext* aCx, BodyConsumer::ConsumeType aType, ErrorResult& aRv);

template already_AddRefed<Promise> FetchBody<EmptyBody>::ConsumeBody(
    JSContext* aCx, BodyConsumer::ConsumeType aType, ErrorResult& aRv);

template <class Derived>
void FetchBody<Derived>::GetMimeType(nsACString& aMimeType,
                                     nsACString& aMixedCaseMimeType) {
  // Implements "extract a MIME type" from
  // https://fetch.spec.whatwg.org/#concept-header-extract-mime-type
  MOZ_ASSERT(DerivedClass()->GetInternalHeaders());

  ErrorResult result;
  nsAutoCString contentTypeValue;
  DerivedClass()->GetInternalHeaders()->Get("Content-Type"_ns, contentTypeValue,
                                            result);
  MOZ_ALWAYS_TRUE(!result.Failed());

  if (contentTypeValue.IsVoid()) {
    return;
  }

  nsTArray<nsTDependentSubstring<char>> values =
      CMimeType::SplitMimetype(contentTypeValue);

  nsAutoCString charset;
  nsAutoCString essence;
  RefPtr<CMimeType> mimeType;

  for (const auto& value : values) {
    RefPtr<CMimeType> temporaryMimeType = CMimeType::Parse(value);

    if (!temporaryMimeType) {
      continue;
    }

    nsAutoCString temporaryEssence;
    temporaryMimeType->GetEssence(temporaryEssence);

    if (temporaryEssence.EqualsLiteral("*/*")) {
      continue;
    }

    mimeType = temporaryMimeType;

    if (!temporaryEssence.Equals(essence)) {
      charset.Truncate();
      mimeType->GetParameterValue("charset"_ns, charset, false, false);

      essence = temporaryEssence;
    } else {
      nsAutoCString newCharset;
      if (!mimeType->GetParameterValue("charset"_ns, newCharset, false,
                                       false) &&
          !charset.IsEmpty()) {
        mimeType->SetParameterValue("charset"_ns, charset);
      } else if (!newCharset.IsEmpty()) {
        charset = newCharset;
      }
    }
  }

  if (mimeType) {
    mimeType->Serialize(aMixedCaseMimeType);
    aMimeType = aMixedCaseMimeType;
    ToLowerCase(aMimeType);
  }
}

template void FetchBody<Request>::GetMimeType(nsACString& aMimeType,
                                              nsACString& aMixedCaseMimeType);
template void FetchBody<Response>::GetMimeType(nsACString& aMimeType,
                                               nsACString& aMixedCaseMimeType);

template <class Derived>
BlobImpl* FetchBody<Derived>::BodyBlobImpl() const {
  return DerivedClass()->BodyBlobImpl();
}

template BlobImpl* FetchBody<Request>::BodyBlobImpl() const;

template BlobImpl* FetchBody<Response>::BodyBlobImpl() const;

template BlobImpl* FetchBody<EmptyBody>::BodyBlobImpl() const;

template <class Derived>
const nsAString& FetchBody<Derived>::BodyLocalPath() const {
  return DerivedClass()->BodyLocalPath();
}

template const nsAString& FetchBody<Request>::BodyLocalPath() const;

template const nsAString& FetchBody<Response>::BodyLocalPath() const;

template const nsAString& FetchBody<EmptyBody>::BodyLocalPath() const;

template <class Derived>
void FetchBody<Derived>::SetReadableStreamBody(JSContext* aCx,
                                               ReadableStream* aBody) {
  MOZ_ASSERT(!mReadableStreamBody);
  MOZ_ASSERT(aBody);
  mReadableStreamBody = aBody;

  RefPtr<AbortSignalImpl> signalImpl =
      DerivedClass()->GetSignalImplToConsumeBody();
  if (!signalImpl) {
    return;
  }

  if (signalImpl->Aborted()) {
    IgnoredErrorResult result;
    AbortStream(aCx, mReadableStreamBody, signalImpl, result);
    if (NS_WARN_IF(result.Failed())) {
      return;
    }
  } else if (!IsFollowing()) {
    Follow(signalImpl);
  }
}

template void FetchBody<Request>::SetReadableStreamBody(JSContext* aCx,
                                                        ReadableStream* aBody);

template void FetchBody<Response>::SetReadableStreamBody(JSContext* aCx,
                                                         ReadableStream* aBody);

template <class Derived>
already_AddRefed<ReadableStream> FetchBody<Derived>::GetBody(JSContext* aCx,
                                                             ErrorResult& aRv) {
  if (mReadableStreamBody) {
    return do_AddRef(mReadableStreamBody);
  }

  nsCOMPtr<nsIInputStream> inputStream;
  DerivedClass()->GetBody(getter_AddRefs(inputStream));

  if (!inputStream) {
    return nullptr;
  }

  // The spec immediately creates ReadableStream on Response/Request constructor
  // via https://fetch.spec.whatwg.org/#concept-bodyinit-extract, but Gecko
  // creates nsIInputStream there instead and creates ReadableStream only when
  // .body is accessed. Thus we only follow step 4 of it here.
  //
  // Step 4: Otherwise, set stream to a new ReadableStream object, and set up
  // stream with byte reading support.
  auto algorithms =
      MakeRefPtr<NonAsyncInputToReadableStreamAlgorithms>(*inputStream);
  RefPtr<ReadableStream> body = ReadableStream::CreateByteNative(
      aCx, DerivedClass()->GetParentObject(), *algorithms, Nothing(), aRv);
  if (aRv.Failed()) {
    return nullptr;
  }
  mReadableStreamBody = body;

  // If the body has been already consumed, we lock the stream.
  if (BodyUsed()) {
    LockStream(aCx, body, aRv);
    if (NS_WARN_IF(aRv.Failed())) {
      return nullptr;
    }
  }

  RefPtr<AbortSignalImpl> signalImpl =
      DerivedClass()->GetSignalImplToConsumeBody();
  if (signalImpl) {
    if (signalImpl->Aborted()) {
      AbortStream(aCx, body, signalImpl, aRv);
      if (NS_WARN_IF(aRv.Failed())) {
        return nullptr;
      }
    } else if (!IsFollowing()) {
      Follow(signalImpl);
    }
  }

  return body.forget();
}

template already_AddRefed<ReadableStream> FetchBody<Request>::GetBody(
    JSContext* aCx, ErrorResult& aRv);

template already_AddRefed<ReadableStream> FetchBody<Response>::GetBody(
    JSContext* aCx, ErrorResult& aRv);

template <class Derived>
void FetchBody<Derived>::LockStream(JSContext* aCx, ReadableStream* aStream,
                                    ErrorResult& aRv) {
  // This is native stream, creating a reader will not execute any JS code.
  RefPtr<ReadableStreamDefaultReader> reader = aStream->GetReader(aRv);
  if (aRv.Failed()) {
    return;
  }
}

template void FetchBody<Request>::LockStream(JSContext* aCx,
                                             ReadableStream* aStream,
                                             ErrorResult& aRv);

template void FetchBody<Response>::LockStream(JSContext* aCx,
                                              ReadableStream* aStream,
                                              ErrorResult& aRv);

template <class Derived>
void FetchBody<Derived>::MaybeTeeReadableStreamBody(
    JSContext* aCx, ReadableStream** aBodyOut,
    FetchStreamReader** aStreamReader, nsIInputStream** aInputStream,
    ErrorResult& aRv) {
  MOZ_DIAGNOSTIC_ASSERT(aStreamReader);
  MOZ_DIAGNOSTIC_ASSERT(aInputStream);
  MOZ_DIAGNOSTIC_ASSERT(!BodyUsed());

  *aBodyOut = nullptr;
  *aStreamReader = nullptr;
  *aInputStream = nullptr;

  if (!mReadableStreamBody) {
    return;
  }

  // If this is a ReadableStream with an native source, this has been
  // generated by a Fetch. In this case, Fetch will be able to recreate it
  // again when GetBody() is called.
  if (mReadableStreamBody->MaybeGetInputStreamIfUnread()) {
    *aBodyOut = nullptr;
    return;
  }

  nsTArray<RefPtr<ReadableStream>> branches;
  MOZ_KnownLive(mReadableStreamBody)->Tee(aCx, branches, aRv);
  if (aRv.Failed()) {
    return;
  }

  mReadableStreamBody = branches[0];
  branches[1].forget(aBodyOut);

  aRv = FetchStreamReader::Create(aCx, mGlobal, aStreamReader, aInputStream);
  if (NS_WARN_IF(aRv.Failed())) {
    return;
  }
}

template void FetchBody<Request>::MaybeTeeReadableStreamBody(
    JSContext* aCx, ReadableStream** aBodyOut,
    FetchStreamReader** aStreamReader, nsIInputStream** aInputStream,
    ErrorResult& aRv);

template void FetchBody<Response>::MaybeTeeReadableStreamBody(
    JSContext* aCx, ReadableStream** aBodyOut,
    FetchStreamReader** aStreamReader, nsIInputStream** aInputStream,
    ErrorResult& aRv);

template <class Derived>
void FetchBody<Derived>::MaybeRebindReadableStreamBody() {
  if (!mReadableStreamBody) {
    return;
  }

  // Only native streams hold an nsIInputStream that clone() may have replaced;
  // SetInputStreamIfUnread() is a no-op for JS-backed streams. clone() rejects
  // used bodies, so the stream is guaranteed to be non-disturbed here.
  nsCOMPtr<nsIInputStream> currentBody;
  DerivedClass()->GetBody(getter_AddRefs(currentBody));
  if (currentBody) {
    mReadableStreamBody->SetInputStreamIfUnread(currentBody);
  }
}

template void FetchBody<Request>::MaybeRebindReadableStreamBody();

template void FetchBody<Response>::MaybeRebindReadableStreamBody();

template <class Derived>
void FetchBody<Derived>::RunAbortAlgorithm() {
  if (!mReadableStreamBody) {
    return;
  }

  AutoJSAPI jsapi;
  if (!jsapi.Init(mGlobal)) {
    return;
  }

  JSContext* cx = jsapi.cx();

  RefPtr<ReadableStream> body(mReadableStreamBody);
  AbortStream(cx, body, Signal(), IgnoredErrorResult());
}

template void FetchBody<Request>::RunAbortAlgorithm();

template void FetchBody<Response>::RunAbortAlgorithm();

NS_IMPL_ADDREF_INHERITED(EmptyBody, FetchBody<EmptyBody>)
NS_IMPL_RELEASE_INHERITED(EmptyBody, FetchBody<EmptyBody>)

NS_IMPL_CYCLE_COLLECTION_CLASS(EmptyBody)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(EmptyBody, FetchBody<EmptyBody>)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mGlobal)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mAbortSignalImpl)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mFetchStreamReader)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(EmptyBody,
                                                  FetchBody<EmptyBody>)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mGlobal)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mAbortSignalImpl)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mFetchStreamReader)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(EmptyBody, FetchBody<EmptyBody>)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(EmptyBody)
NS_INTERFACE_MAP_END_INHERITING(FetchBody<EmptyBody>)

EmptyBody::EmptyBody(nsIGlobalObject* aGlobal,
                     mozilla::ipc::PrincipalInfo* aPrincipalInfo,
                     AbortSignalImpl* aAbortSignalImpl,
                     const nsACString& aMimeType,
                     const nsACString& aMixedCaseMimeType,
                     already_AddRefed<nsIInputStream> aBodyStream)
    : FetchBody<EmptyBody>(aGlobal),
      mAbortSignalImpl(aAbortSignalImpl),
      mMimeType(aMimeType),
      mMixedCaseMimeType(aMixedCaseMimeType),
      mBodyStream(std::move(aBodyStream)) {
  if (aPrincipalInfo) {
    mPrincipalInfo =
        std::make_unique<mozilla::ipc::PrincipalInfo>(*aPrincipalInfo);
  }
}

EmptyBody::~EmptyBody() = default;

/* static */
already_AddRefed<EmptyBody> EmptyBody::Create(
    nsIGlobalObject* aGlobal, mozilla::ipc::PrincipalInfo* aPrincipalInfo,
    AbortSignalImpl* aAbortSignalImpl, const nsACString& aMimeType,
    const nsACString& aMixedCaseMimeType, ErrorResult& aRv) {
  nsCOMPtr<nsIInputStream> bodyStream;
  aRv = NS_NewCStringInputStream(getter_AddRefs(bodyStream), ""_ns);
  if (NS_WARN_IF(aRv.Failed())) {
    return nullptr;
  }

  RefPtr<EmptyBody> emptyBody =
      new EmptyBody(aGlobal, aPrincipalInfo, aAbortSignalImpl, aMimeType,
                    aMixedCaseMimeType, bodyStream.forget());
  return emptyBody.forget();
}

void EmptyBody::GetBody(nsIInputStream** aStream, int64_t* aBodyLength) {
  MOZ_ASSERT(aStream);

  if (aBodyLength) {
    *aBodyLength = 0;
  }

  nsCOMPtr<nsIInputStream> bodyStream = mBodyStream;
  bodyStream.forget(aStream);
}

}  // namespace mozilla::dom
