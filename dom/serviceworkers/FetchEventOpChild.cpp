/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FetchEventOpChild.h"

#include <utility>

#include "MainThreadUtils.h"
#include "ServiceWorkerPrivate.h"
#include "mozilla/Assertions.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/FetchService.h"
#include "mozilla/dom/InternalHeaders.h"
#include "mozilla/dom/InternalResponse.h"
#include "mozilla/dom/PRemoteWorkerControllerChild.h"
#include "mozilla/dom/RemoteWorkerControllerChild.h"
#include "mozilla/dom/ServiceWorkerRegistrationInfo.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/net/NeckoChannelParams.h"
#include "nsContentPolicyUtils.h"
#include "nsContentUtils.h"
#include "nsDebug.h"
#include "nsError.h"
#include "nsIChannel.h"
#include "nsIConsoleReportCollector.h"
#include "nsIContentPolicy.h"
#include "nsIInputStream.h"
#include "nsILoadInfo.h"
#include "nsINetworkInterceptController.h"
#include "nsIObserverService.h"
#include "nsIScriptError.h"
#include "nsISupportsImpl.h"
#include "nsIURI.h"
#include "nsNetUtil.h"
#include "nsProxyRelease.h"
#include "nsTArray.h"
#include "nsThreadUtils.h"

namespace mozilla::dom {

namespace {

bool CSPPermitsResponse(nsILoadInfo* aLoadInfo,
                        SafeRefPtr<InternalResponse> aResponse,
                        const nsACString& aWorkerScriptSpec) {
  AssertIsOnMainThread();
  MOZ_ASSERT(aLoadInfo);

  nsresult rv;

  nsCOMPtr<nsIURI> uri = aResponse->GetUnfilteredURL();
  if (!uri) {
    // Synthetic response.
    rv = NS_NewURI(getter_AddRefs(uri), aWorkerScriptSpec, nullptr, nullptr);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return false;
    }
  }

  int16_t decision = nsIContentPolicy::ACCEPT;
  rv = NS_CheckContentLoadPolicy(uri, aLoadInfo, &decision);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }

  return decision == nsIContentPolicy::ACCEPT;
}

void AsyncLog(nsIInterceptedChannel* aChannel, const nsACString& aScriptSpec,
              uint32_t aLineNumber, uint32_t aColumnNumber,
              const nsACString& aMessageName, nsTArray<nsString>&& aParams) {
  AssertIsOnMainThread();
  MOZ_ASSERT(aChannel);

  nsCOMPtr<nsIConsoleReportCollector> reporter =
      aChannel->GetConsoleReportCollector();

  if (reporter) {
    // NOTE: is appears that `const nsTArray<nsString>&` is required for
    // nsIConsoleReportCollector::AddConsoleReport to resolve to the correct
    // overload.
    const nsTArray<nsString> params = std::move(aParams);

    reporter->AddConsoleReport(
        nsIScriptError::errorFlag, "Service Worker Interception"_ns,
        PropertiesFile::DOM_PROPERTIES, aScriptSpec, aLineNumber, aColumnNumber,
        aMessageName, params);
  }
}

class SynthesizeResponseWatcher final : public nsIInterceptedBodyCallback {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  SynthesizeResponseWatcher(
      const nsMainThreadPtrHandle<nsIInterceptedChannel>& aInterceptedChannel,
      const nsMainThreadPtrHandle<ServiceWorkerRegistrationInfo>& aRegistration,
      const bool aIsNonSubresourceRequest,
      FetchEventRespondWithClosure&& aClosure, nsAString&& aRequestURL)
      : mInterceptedChannel(aInterceptedChannel),
        mRegistration(aRegistration),
        mIsNonSubresourceRequest(aIsNonSubresourceRequest),
        mClosure(std::move(aClosure)),
        mRequestURL(std::move(aRequestURL)) {
    AssertIsOnMainThread();
    MOZ_ASSERT(mInterceptedChannel);
    MOZ_ASSERT(mRegistration);
  }

  NS_IMETHOD
  BodyComplete(nsresult aRv) override {
    AssertIsOnMainThread();
    MOZ_ASSERT(mInterceptedChannel);

    if (NS_WARN_IF(NS_FAILED(aRv))) {
      AsyncLog(mInterceptedChannel, mClosure.respondWithScriptSpec(),
               mClosure.respondWithLineNumber(),
               mClosure.respondWithColumnNumber(),
               "InterceptionFailedWithURL"_ns, {mRequestURL});

      CancelInterception(NS_ERROR_INTERCEPTION_FAILED);

      return NS_OK;
    }

    nsresult rv = mInterceptedChannel->FinishSynthesizedResponse();

    if (NS_WARN_IF(NS_FAILED(rv))) {
      CancelInterception(rv);
    }

    mInterceptedChannel = nullptr;

    return NS_OK;
  }

  // See FetchEventOpChild::MaybeScheduleRegistrationUpdate() for comments.
  void CancelInterception(nsresult aStatus) {
    AssertIsOnMainThread();
    MOZ_ASSERT(mInterceptedChannel);
    MOZ_ASSERT(mRegistration);

    mInterceptedChannel->CancelInterception(aStatus);

    if (mIsNonSubresourceRequest) {
      mRegistration->MaybeScheduleUpdate();
    } else {
      mRegistration->MaybeScheduleTimeCheckAndUpdate();
    }

    mInterceptedChannel = nullptr;
    mRegistration = nullptr;
  }

 private:
  ~SynthesizeResponseWatcher() {
    if (NS_WARN_IF(mInterceptedChannel)) {
      CancelInterception(NS_ERROR_DOM_ABORT_ERR);
    }
  }

  nsMainThreadPtrHandle<nsIInterceptedChannel> mInterceptedChannel;
  nsMainThreadPtrHandle<ServiceWorkerRegistrationInfo> mRegistration;
  const bool mIsNonSubresourceRequest;
  const FetchEventRespondWithClosure mClosure;
  const nsString mRequestURL;
};

NS_IMPL_ISUPPORTS(SynthesizeResponseWatcher, nsIInterceptedBodyCallback)

}  // anonymous namespace

/* static */ RefPtr<GenericPromise> FetchEventOpChild::SendFetchEvent(
    PRemoteWorkerControllerChild* aManager,
    ParentToParentServiceWorkerFetchEventOpArgs&& aArgs,
    nsCOMPtr<nsIInterceptedChannel> aInterceptedChannel,
    RefPtr<ServiceWorkerRegistrationInfo> aRegistration,
    RefPtr<FetchServicePromises>&& aPreloadResponseReadyPromises,
    RefPtr<KeepAliveToken>&& aKeepAliveToken) {
  AssertIsOnMainThread();
  MOZ_ASSERT(aManager);
  MOZ_ASSERT(aInterceptedChannel);
  MOZ_ASSERT(aKeepAliveToken);

  FetchEventOpChild* actor = new FetchEventOpChild(
      std::move(aArgs), std::move(aInterceptedChannel),
      std::move(aRegistration), std::move(aPreloadResponseReadyPromises),
      std::move(aKeepAliveToken));

  actor->mWasSent = true;
  RefPtr<GenericPromise> promise = actor->mPromiseHolder.Ensure(__func__);
  (void)aManager->SendPFetchEventOpConstructor(actor, actor->mArgs);
  // NOTE: actor may have been destroyed
  return promise;
}

FetchEventOpChild::~FetchEventOpChild() {
  AssertIsOnMainThread();
  MOZ_ASSERT(mInterceptedChannelHandled);
  MOZ_DIAGNOSTIC_ASSERT(mPromiseHolder.IsEmpty());
}

FetchEventOpChild::FetchEventOpChild(
    ParentToParentServiceWorkerFetchEventOpArgs&& aArgs,
    nsCOMPtr<nsIInterceptedChannel>&& aInterceptedChannel,
    RefPtr<ServiceWorkerRegistrationInfo>&& aRegistration,
    RefPtr<FetchServicePromises>&& aPreloadResponseReadyPromises,
    RefPtr<KeepAliveToken>&& aKeepAliveToken)
    : mArgs(std::move(aArgs)),
      mInterceptedChannel(std::move(aInterceptedChannel)),
      mRegistration(std::move(aRegistration)),
      mKeepAliveToken(std::move(aKeepAliveToken)),
      mPreloadResponseReadyPromises(std::move(aPreloadResponseReadyPromises)) {
  if (mPreloadResponseReadyPromises) {
    // This promise should be configured to use synchronous dispatch, so if it's
    // already resolved when we run this code then the callback will be called
    // synchronously and pass the preload response with the constructor message.
    //
    // Note that it's fine to capture the this pointer in the callbacks because
    // we disconnect the request in Recv__delete__().
    mPreloadResponseReadyPromises->GetResponseAvailablePromise()
        ->Then(
            GetCurrentSerialEventTarget(), __func__,
            [this](FetchServiceResponse&& aResponse) {
              if (!mWasSent) {
                // The actor wasn't sent yet, we can still send the preload
                // response with it.
                mArgs.preloadResponse() =
                    Some(aResponse->ToParentToParentInternalResponse());
              } else {
                // It's too late to send the preload response with the actor, we
                // have to send it in a separate message.
                SendPreloadResponse(
                    aResponse->ToParentToParentInternalResponse());
              }
              mPreloadResponseAvailablePromiseRequestHolder.Complete();
            },
            [this](const CopyableErrorResult&) {
              mPreloadResponseAvailablePromiseRequestHolder.Complete();
            })
        ->Track(mPreloadResponseAvailablePromiseRequestHolder);

    mPreloadResponseReadyPromises->GetResponseTimingPromise()
        ->Then(
            GetCurrentSerialEventTarget(), __func__,
            [this](ResponseTiming&& aTiming) {
              if (!mWasSent) {
                // The actor wasn't sent yet, we can still send the preload
                // response timing with it.
                mArgs.preloadResponseTiming() = Some(std::move(aTiming));
              } else {
                SendPreloadResponseTiming(aTiming);
              }
              mPreloadResponseTimingPromiseRequestHolder.Complete();
            },
            [this](const CopyableErrorResult&) {
              mPreloadResponseTimingPromiseRequestHolder.Complete();
            })
        ->Track(mPreloadResponseTimingPromiseRequestHolder);

    mPreloadResponseReadyPromises->GetResponseEndPromise()
        ->Then(
            GetCurrentSerialEventTarget(), __func__,
            [this](ResponseEndArgs&& aResponse) {
              if (!mWasSent) {
                // The actor wasn't sent yet, we can still send the preload
                // response end args with it.
                mArgs.preloadResponseEndArgs() = Some(std::move(aResponse));
              } else {
                // It's too late to send the preload response end with the
                // actor, we have to send it in a separate message.
                SendPreloadResponseEnd(aResponse);
              }
              mPreloadResponseReadyPromises = nullptr;
              mPreloadResponseEndPromiseRequestHolder.Complete();
            },
            [this](const CopyableErrorResult&) {
              mPreloadResponseReadyPromises = nullptr;
              mPreloadResponseEndPromiseRequestHolder.Complete();
            })
        ->Track(mPreloadResponseEndPromiseRequestHolder);
  }
}

mozilla::ipc::IPCResult FetchEventOpChild::RecvAsyncLog(
    const nsCString& aScriptSpec, const uint32_t& aLineNumber,
    const uint32_t& aColumnNumber, const nsCString& aMessageName,
    nsTArray<nsString>&& aParams) {
  AssertIsOnMainThread();
  MOZ_ASSERT(mInterceptedChannel);

  AsyncLog(mInterceptedChannel, aScriptSpec, aLineNumber, aColumnNumber,
           aMessageName, std::move(aParams));

  return IPC_OK();
}

mozilla::ipc::IPCResult FetchEventOpChild::RecvRespondWith(
    ParentToParentFetchEventRespondWithResult&& aResult) {
  AssertIsOnMainThread();

  RefPtr<RemoteWorkerControllerChild> mgr =
      mozilla::ipc::ActorCast<RemoteWorkerControllerChild>(Manager());

  mInterceptedChannel->SetRemoteWorkerLaunchStart(
      mgr->GetRemoteWorkerLaunchStart());
  mInterceptedChannel->SetRemoteWorkerLaunchEnd(
      mgr->GetRemoteWorkerLaunchEnd());

  switch (aResult.type()) {
    case ParentToParentFetchEventRespondWithResult::
        TParentToParentSynthesizeResponseArgs:
      mInterceptedChannel->SetFetchHandlerStart(
          aResult.get_ParentToParentSynthesizeResponseArgs()
              .timeStamps()
              .fetchHandlerStart());
      mInterceptedChannel->SetFetchHandlerFinish(
          aResult.get_ParentToParentSynthesizeResponseArgs()
              .timeStamps()
              .fetchHandlerFinish());
      SynthesizeResponse(
          std::move(aResult.get_ParentToParentSynthesizeResponseArgs()));
      break;
    case ParentToParentFetchEventRespondWithResult::TResetInterceptionArgs:
      mInterceptedChannel->SetFetchHandlerStart(
          aResult.get_ResetInterceptionArgs().timeStamps().fetchHandlerStart());
      mInterceptedChannel->SetFetchHandlerFinish(
          aResult.get_ResetInterceptionArgs()
              .timeStamps()
              .fetchHandlerFinish());
      ResetInterception(false);
      break;
    case ParentToParentFetchEventRespondWithResult::TCancelInterceptionArgs:
      mInterceptedChannel->SetFetchHandlerStart(
          aResult.get_CancelInterceptionArgs()
              .timeStamps()
              .fetchHandlerStart());
      mInterceptedChannel->SetFetchHandlerFinish(
          aResult.get_CancelInterceptionArgs()
              .timeStamps()
              .fetchHandlerFinish());
      CancelInterception(aResult.get_CancelInterceptionArgs().status());
      break;
    default:
      MOZ_CRASH("Unknown IPCFetchEventRespondWithResult type!");
      break;
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult FetchEventOpChild::Recv__delete__(
    const ServiceWorkerFetchEventOpResult& aResult) {
  AssertIsOnMainThread();
  MOZ_ASSERT(mRegistration);

  if (NS_WARN_IF(!mInterceptedChannelHandled)) {
    MOZ_ASSERT(NS_FAILED(aResult.rv()));
    NS_WARNING(
        "Failed to handle intercepted network request; canceling "
        "interception!");

    CancelInterception(aResult.rv());
  }

  mPromiseHolder.ResolveIfExists(true, __func__);

  // FetchEvent is completed.
  // Disconnect preload response related promises and cancel the preload.
  mPreloadResponseAvailablePromiseRequestHolder.DisconnectIfExists();
  mPreloadResponseTimingPromiseRequestHolder.DisconnectIfExists();
  mPreloadResponseEndPromiseRequestHolder.DisconnectIfExists();
  if (mPreloadResponseReadyPromises) {
    RefPtr<FetchService> fetchService = FetchService::GetInstance();
    fetchService->CancelFetch(std::move(mPreloadResponseReadyPromises), false);
  }

  /**
   * This corresponds to the "Fire Functional Event" algorithm's step 9:
   *
   * "If the time difference in seconds calculated by the current time minus
   * registration's last update check time is greater than 84600, invoke Soft
   * Update algorithm with registration."
   *
   * TODO: this is probably being called later than it should be; it should be
   * called ASAP after dispatching the FetchEvent.
   */
  mRegistration->MaybeScheduleTimeCheckAndUpdate();

  return IPC_OK();
}

void FetchEventOpChild::ActorDestroy(ActorDestroyReason) {
  AssertIsOnMainThread();

  // If `Recv__delete__` was called, it would have resolved the promise already.
  mPromiseHolder.RejectIfExists(NS_ERROR_DOM_ABORT_ERR, __func__);

  if (NS_WARN_IF(!mInterceptedChannelHandled)) {
    (void)Recv__delete__(NS_ERROR_DOM_ABORT_ERR);
  }
}

nsresult FetchEventOpChild::StartSynthesizedResponse(
    ParentToParentSynthesizeResponseArgs&& aArgs) {
  AssertIsOnMainThread();
  MOZ_ASSERT(mInterceptedChannel);
  MOZ_ASSERT(!mInterceptedChannelHandled);
  MOZ_ASSERT(mRegistration);

  /**
   * TODO: moving the IPCInternalResponse won't do anything right now because
   * there isn't a prefect-forwarding or rvalue-ref-parameter overload of
   * `InternalResponse::FromIPC().`
   */
  SafeRefPtr<InternalResponse> response =
      InternalResponse::FromIPC(aArgs.internalResponse());
  if (NS_WARN_IF(!response)) {
    return NS_ERROR_FAILURE;
  }

  // This implements https://fetch.spec.whatwg.org/#main-fetch step 20 for the
  // response from ServiceWorker.
  const IPCInternalRequest& request = mArgs.common().internalRequest();
  if ((response->GetUnfilteredStatus() == 206 ||
       response->GetUnfilteredStatus() == 416) &&
      response->GetTainting() == LoadTainting::Opaque) {
    bool isRangedRequest = false;
    for (const auto& headerEntry : request.headers()) {
      if (headerEntry.name().EqualsIgnoreCase("Range")) {
        isRangedRequest = true;
        break;
      }
    }
    if (!isRangedRequest) {
      return NS_ERROR_FAILURE;
    }
  }

  nsCOMPtr<nsIChannel> underlyingChannel;
  nsresult rv =
      mInterceptedChannel->GetChannel(getter_AddRefs(underlyingChannel));
  if (NS_WARN_IF(NS_FAILED(rv)) || NS_WARN_IF(!underlyingChannel)) {
    return NS_FAILED(rv) ? rv : NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsILoadInfo> loadInfo = underlyingChannel->LoadInfo();
  if (!CSPPermitsResponse(loadInfo, response.clonePtr(),
                          mArgs.common().workerScriptSpec())) {
    return NS_ERROR_CONTENT_BLOCKED;
  }

  MOZ_ASSERT(response->GetChannelInfo().IsInitialized());
  ChannelInfo channelInfo = response->GetChannelInfo();
  rv = mInterceptedChannel->SetChannelInfo(&channelInfo);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return NS_ERROR_INTERCEPTION_FAILED;
  }

  rv = mInterceptedChannel->SynthesizeStatus(
      response->GetUnfilteredStatus(), response->GetUnfilteredStatusText());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  AutoTArray<InternalHeaders::Entry, 5> entries;
  response->UnfilteredHeaders()->GetEntries(entries);
  for (auto& entry : entries) {
    mInterceptedChannel->SynthesizeHeader(entry.mName, entry.mValue);
  }

  auto castLoadInfo = static_cast<mozilla::net::LoadInfo*>(loadInfo.get());
  castLoadInfo->SynthesizeServiceWorkerTainting(response->GetTainting());

  // Get the preferred alternative data type of the outer channel
  nsAutoCString preferredAltDataType(""_ns);
  nsCOMPtr<nsICacheInfoChannel> outerChannel =
      do_QueryInterface(underlyingChannel);
  if (outerChannel &&
      !outerChannel->PreferredAlternativeDataTypes().IsEmpty()) {
    preferredAltDataType.Assign(
        outerChannel->PreferredAlternativeDataTypes()[0].type());
  }

  nsCOMPtr<nsIInputStream> body;
  if (preferredAltDataType.Equals(response->GetAlternativeDataType())) {
    body = response->TakeAlternativeBody();
  }
  if (!body) {
    response->GetUnfilteredBody(getter_AddRefs(body));
  }

  // Propagate the URL to the content if the request mode is not "navigate".
  // Note that, we only reflect the final URL if the response.redirected is
  // false. We propagate all the URLs if the response.redirected is true.
  nsCOMPtr<nsIURI> responseURL;
  if (request.requestMode() != RequestMode::Navigate &&
      response->GetUnfilteredURL()) {
    // Similar to how we apply the request fragment to redirects automatically
    // we also want to apply it automatically when propagating the response
    // URL from a service worker interception.  Currently response.url strips
    // the fragment, so this will never conflict with an existing fragment
    // on the response.  In the future we will have to check for a response
    // fragment and avoid overriding in that case.
    if (request.fragment().IsEmpty()) {
      responseURL = response->GetUnfilteredURL();
    } else {
      rv = NS_GetURIWithNewRef(response->GetUnfilteredURL(),
                               "#"_ns + request.fragment(),
                               getter_AddRefs(responseURL));
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
    }
  }

  nsMainThreadPtrHandle<nsIInterceptedChannel> interceptedChannel(
      new nsMainThreadPtrHolder<nsIInterceptedChannel>(
          "nsIInterceptedChannel", mInterceptedChannel, false));

  nsMainThreadPtrHandle<ServiceWorkerRegistrationInfo> registration(
      new nsMainThreadPtrHolder<ServiceWorkerRegistrationInfo>(
          "ServiceWorkerRegistrationInfo", mRegistration, false));

  nsCOMPtr<nsIURI> requestURL;
  if (request.fragment().IsEmpty()) {
    requestURL = request.urlList().LastElement();
  } else {
    rv = NS_GetURIWithNewRef(request.urlList().LastElement(),
                             "#"_ns + request.fragment(),
                             getter_AddRefs(requestURL));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }

  nsAutoCString responseURLSpec;
  if (responseURL) {
    responseURL->GetSpec(responseURLSpec);
  }

  RefPtr<SynthesizeResponseWatcher> watcher = new SynthesizeResponseWatcher(
      interceptedChannel, registration,
      mArgs.common().isNonSubresourceRequest(), std::move(aArgs.closure()),
      NS_ConvertUTF8toUTF16(responseURLSpec));

  rv = mInterceptedChannel->StartSynthesizedResponse(
      body, watcher, nullptr /* TODO */, responseURLSpec,
      response->IsRedirected());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsCOMPtr<nsIObserverService> obsService = services::GetObserverService();
  if (obsService) {
    obsService->NotifyObservers(underlyingChannel,
                                "service-worker-synthesized-response", nullptr);
  }

  return rv;
}

void FetchEventOpChild::SynthesizeResponse(
    ParentToParentSynthesizeResponseArgs&& aArgs) {
  AssertIsOnMainThread();
  MOZ_ASSERT(mInterceptedChannel);
  MOZ_ASSERT(!mInterceptedChannelHandled);

  nsresult rv = StartSynthesizedResponse(std::move(aArgs));

  if (NS_WARN_IF(NS_FAILED(rv))) {
    NS_WARNING("Failed to synthesize response!");

    mInterceptedChannel->CancelInterception(rv);
  }

  mInterceptedChannelHandled = true;

  MaybeScheduleRegistrationUpdate();
}

void FetchEventOpChild::ResetInterception(bool aBypass) {
  AssertIsOnMainThread();
  MOZ_ASSERT(mInterceptedChannel);
  MOZ_ASSERT(!mInterceptedChannelHandled);

  nsresult rv = mInterceptedChannel->ResetInterception(aBypass);

  if (NS_WARN_IF(NS_FAILED(rv))) {
    NS_WARNING("Failed to resume intercepted network request!");

    mInterceptedChannel->CancelInterception(rv);
  }

  mInterceptedChannelHandled = true;

  MaybeScheduleRegistrationUpdate();
}

void FetchEventOpChild::CancelInterception(nsresult aStatus) {
  AssertIsOnMainThread();
  MOZ_ASSERT(mInterceptedChannel);
  MOZ_ASSERT(!mInterceptedChannelHandled);
  MOZ_ASSERT(NS_FAILED(aStatus));

  // Report a navigation fault if this is a navigation (and we have an active
  // worker, which should be the case in non-shutdown/content-process-crash
  // situations).
  RefPtr<ServiceWorkerInfo> mActive = mRegistration->GetActive();
  if (mActive && mArgs.common().isNonSubresourceRequest()) {
    mActive->ReportNavigationFault();
    // Additional mitigations such as unregistering the registration are handled
    // in ServiceWorkerRegistrationInfo::MaybeScheduleUpdate which will be
    // called by MaybeScheduleRegistrationUpdate which gets called by our call
    // to ResetInterception.
    if (StaticPrefs::dom_serviceWorkers_mitigations_bypass_on_fault()) {
      ResetInterception(true);
      return;
    }
  }

  mInterceptedChannel->CancelInterception(aStatus);
  mInterceptedChannelHandled = true;

  MaybeScheduleRegistrationUpdate();
}

/**
 * This corresponds to the "Handle Fetch" algorithm's steps 20.3, 21.2, and
 * 22.2:
 *
 * "If request is a non-subresource request, or request is a subresource
 * request and the time difference in seconds calculated by the current time
 * minus registration's last update check time is greater than 86400, invoke
 * Soft Update algorithm with registration."
 */
void FetchEventOpChild::MaybeScheduleRegistrationUpdate() const {
  AssertIsOnMainThread();
  MOZ_ASSERT(mRegistration);
  MOZ_ASSERT(mInterceptedChannelHandled);

  if (mArgs.common().isNonSubresourceRequest()) {
    mRegistration->MaybeScheduleUpdate();
  } else {
    mRegistration->MaybeScheduleTimeCheckAndUpdate();
  }
}

}  // namespace mozilla::dom
