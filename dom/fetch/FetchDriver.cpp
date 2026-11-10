/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/FetchDriver.h"

#include <memory>

#include "Fetch.h"
#include "FetchLog.h"
#include "FetchUtil.h"
#include "InternalRequest.h"
#include "InternalResponse.h"
#include "js/Value.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/PreloaderBase.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_javascript.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/StaticPrefs_privacy.h"
#include "mozilla/TaskQueue.h"
#include "mozilla/dom/BlobURL.h"
#include "mozilla/dom/BlobURLChannel.h"
#include "mozilla/dom/BlobURLProtocolHandler.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/FetchPriority.h"
#include "mozilla/dom/File.h"
#include "mozilla/dom/PerformanceStorage.h"
#include "mozilla/dom/PerformanceTiming.h"
#include "mozilla/dom/ReferrerInfo.h"
#include "mozilla/dom/ServiceWorkerInterceptController.h"
#include "mozilla/dom/UserActivation.h"
#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "mozilla/net/ChannelClassifierUtils.h"
#include "mozilla/net/ContentRange.h"
#include "mozilla/net/HttpBaseChannel.h"
#include "mozilla/net/InterceptionInfo.h"
#include "mozilla/net/NeckoChannelParams.h"
#include "nsContentPolicyUtils.h"
#include "nsDataChannel.h"
#include "nsDataHandler.h"
#include "nsHttpChannel.h"
#include "nsIAsyncVerifyRedirectCallback.h"
#include "nsIBaseChannel.h"
#include "nsICookieJarSettings.h"
#include "nsIFile.h"
#include "nsIFileChannel.h"
#include "nsIHttpChannel.h"
#include "nsIHttpChannelInternal.h"
#include "nsIInputStream.h"
#include "nsIInterceptionInfo.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIOutputStream.h"
#include "nsIPipe.h"
#include "nsIRedirectHistoryEntry.h"
#include "nsISupportsPriority.h"
#include "nsIThreadRetargetableRequest.h"
#include "nsIUploadChannel2.h"
#include "nsNetUtil.h"
#include "nsPrintfCString.h"
#include "nsProxyRelease.h"
#include "nsQueryObject.h"
#include "nsStreamUtils.h"
#include "nsStringStream.h"

namespace mozilla::dom {

namespace {

bool ShouldCheckSRI(const InternalRequest& aRequest,
                    const InternalResponse& aResponse) {
  return !aRequest.GetIntegrity().IsEmpty() &&
         aResponse.Type() != ResponseType::Error;
}

}  // anonymous namespace

//-----------------------------------------------------------------------------
// AlternativeDataStreamListener
//-----------------------------------------------------------------------------
class AlternativeDataStreamListener final
    : public nsIThreadRetargetableStreamListener {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER
  NS_DECL_NSITHREADRETARGETABLESTREAMLISTENER

  // The status of AlternativeDataStreamListener
  // LOADING: is the initial status, loading the alternative data
  // COMPLETED: Alternative data loading is completed
  // CANCELED: Alternative data loading is canceled, this would make
  //           AlternativeDataStreamListener ignore all channel callbacks
  // FALLBACK: fallback the channel callbacks to FetchDriver
  // Depends on different situaions, the status transition could be followings
  // 1. LOADING->COMPLETED
  //    This is the normal status transition for alternative data loading
  //
  // 2. LOADING->CANCELED
  //    LOADING->COMPLETED->CANCELED
  //    Alternative data loading could be canceled when cacheId from alternative
  //    data channel does not match with from main data channel(The cacheID
  //    checking is in FetchDriver::OnStartRequest).
  //    Notice the alternative data loading could finish before the cacheID
  //    checking, so the statust transition could be
  //    LOADING->COMPLETED->CANCELED
  //
  // 3. LOADING->FALLBACK
  //    For the case that alternative data loading could not be initialized,
  //    i.e. alternative data does not exist or no preferred alternative data
  //    type is requested. Once the status becomes FALLBACK,
  //    AlternativeDataStreamListener transits the channel callback request to
  //    FetchDriver, and the status should not go back to LOADING, COMPLETED, or
  //    CANCELED anymore.
  enum eStatus { LOADING = 0, COMPLETED, CANCELED, FALLBACK };

  AlternativeDataStreamListener(FetchDriver* aFetchDriver, nsIChannel* aChannel,
                                const nsACString& aAlternativeDataType);
  eStatus Status();
  void Cancel();
  uint64_t GetAlternativeDataCacheEntryId();
  const nsACString& GetAlternativeDataType() const;
  already_AddRefed<nsICacheInfoChannel> GetCacheInfoChannel();
  already_AddRefed<nsIInputStream> GetAlternativeInputStream();

 private:
  ~AlternativeDataStreamListener() = default;

  // This creates a strong reference cycle with FetchDriver and its
  // mAltDataListener. We need to clear at least one reference of them once the
  // data loading finishes.
  RefPtr<FetchDriver> mFetchDriver;
  nsCString mAlternativeDataType;
  nsCOMPtr<nsIInputStream> mPipeAlternativeInputStream;
  nsCOMPtr<nsIOutputStream> mPipeAlternativeOutputStream;
  uint64_t mAlternativeDataCacheEntryId;
  nsCOMPtr<nsICacheInfoChannel> mCacheInfoChannel;
  nsCOMPtr<nsIChannel> mChannel;
  Atomic<eStatus> mStatus;
};

NS_IMPL_ISUPPORTS(AlternativeDataStreamListener, nsIStreamListener,
                  nsIThreadRetargetableStreamListener)

AlternativeDataStreamListener::AlternativeDataStreamListener(
    FetchDriver* aFetchDriver, nsIChannel* aChannel,
    const nsACString& aAlternativeDataType)
    : mFetchDriver(aFetchDriver),
      mAlternativeDataType(aAlternativeDataType),
      mAlternativeDataCacheEntryId(0),
      mChannel(aChannel),
      mStatus(AlternativeDataStreamListener::LOADING) {
  MOZ_DIAGNOSTIC_ASSERT(mFetchDriver);
  MOZ_DIAGNOSTIC_ASSERT(mChannel);
}

AlternativeDataStreamListener::eStatus AlternativeDataStreamListener::Status() {
  return mStatus;
}

void AlternativeDataStreamListener::Cancel() {
  mAlternativeDataCacheEntryId = 0;
  mCacheInfoChannel = nullptr;
  mPipeAlternativeOutputStream = nullptr;
  mPipeAlternativeInputStream = nullptr;
  if (mChannel && mStatus != AlternativeDataStreamListener::FALLBACK) {
    // if mStatus is fallback, we need to keep channel to forward request back
    // to FetchDriver
    mChannel->CancelWithReason(NS_BINDING_ABORTED,
                               "AlternativeDataStreamListener::Cancel"_ns);
    mChannel = nullptr;
  }
  mStatus = AlternativeDataStreamListener::CANCELED;
}

uint64_t AlternativeDataStreamListener::GetAlternativeDataCacheEntryId() {
  return mAlternativeDataCacheEntryId;
}

const nsACString& AlternativeDataStreamListener::GetAlternativeDataType()
    const {
  return mAlternativeDataType;
}

already_AddRefed<nsIInputStream>
AlternativeDataStreamListener::GetAlternativeInputStream() {
  nsCOMPtr<nsIInputStream> inputStream = mPipeAlternativeInputStream;
  return inputStream.forget();
}

already_AddRefed<nsICacheInfoChannel>
AlternativeDataStreamListener::GetCacheInfoChannel() {
  nsCOMPtr<nsICacheInfoChannel> channel = mCacheInfoChannel;
  return channel.forget();
}

NS_IMETHODIMP
AlternativeDataStreamListener::OnStartRequest(nsIRequest* aRequest) {
  AssertIsOnMainThread();
  MOZ_ASSERT(!mAlternativeDataType.IsEmpty());
  // Checking the alternative data type is the same between we asked and the
  // saved in the channel.
  nsAutoCString alternativeDataType;
  nsCOMPtr<nsICacheInfoChannel> cic = do_QueryInterface(aRequest);
  mStatus = AlternativeDataStreamListener::LOADING;
  if (cic && NS_SUCCEEDED(cic->GetAlternativeDataType(alternativeDataType)) &&
      mAlternativeDataType.Equals(alternativeDataType) &&
      NS_SUCCEEDED(cic->GetCacheEntryId(&mAlternativeDataCacheEntryId))) {
    MOZ_DIAGNOSTIC_ASSERT(!mPipeAlternativeInputStream);
    MOZ_DIAGNOSTIC_ASSERT(!mPipeAlternativeOutputStream);
    NS_NewPipe(getter_AddRefs(mPipeAlternativeInputStream),
               getter_AddRefs(mPipeAlternativeOutputStream),
               0 /* default segment size */, UINT32_MAX /* infinite pipe */,
               true /* non-blocking input, otherwise you deadlock */,
               false /* blocking output, since the pipe is 'in'finite */);

    MOZ_DIAGNOSTIC_ASSERT(!mCacheInfoChannel);
    mCacheInfoChannel = cic;

    // call FetchDriver::HttpFetch to load main body
    MOZ_ASSERT(mFetchDriver);
    return mFetchDriver->HttpFetch();
  }
  // Needn't load alternative data, since alternative data does not exist.
  // Set status to FALLBACK to reuse the opened channel to load main body,
  // then call FetchDriver::OnStartRequest to continue the work. Unfortunately
  // can't change the stream listener to mFetchDriver, need to keep
  // AlternativeDataStreamListener alive to redirect OnDataAvailable and
  // OnStopRequest to mFetchDriver.
  MOZ_ASSERT(alternativeDataType.IsEmpty());
  mStatus = AlternativeDataStreamListener::FALLBACK;
  mAlternativeDataCacheEntryId = 0;
  MOZ_ASSERT(mFetchDriver);
  RefPtr<FetchDriver> fetchDriver = mFetchDriver;
  return fetchDriver->OnStartRequest(aRequest);
}

NS_IMETHODIMP
AlternativeDataStreamListener::OnDataAvailable(nsIRequest* aRequest,
                                               nsIInputStream* aInputStream,
                                               uint64_t aOffset,
                                               uint32_t aCount) {
  FETCH_LOG(
      ("FetchDriver::OnDataAvailable this=%p, request=%p", this, aRequest));
  if (mStatus == AlternativeDataStreamListener::LOADING) {
    MOZ_ASSERT(mPipeAlternativeOutputStream);
    uint32_t read = 0;
    return aInputStream->ReadSegments(
        NS_CopySegmentToStream, mPipeAlternativeOutputStream, aCount, &read);
  }
  if (mStatus == AlternativeDataStreamListener::FALLBACK) {
    MOZ_ASSERT(mFetchDriver);
    RefPtr<FetchDriver> fetchDriver = mFetchDriver;
    return fetchDriver->OnDataAvailable(aRequest, aInputStream, aOffset,
                                        aCount);
  }
  return NS_OK;
}

NS_IMETHODIMP
AlternativeDataStreamListener::OnStopRequest(
    nsIRequest* aRequest, nsresult aStatusCode) MOZ_CAN_RUN_SCRIPT_BOUNDARY {
  AssertIsOnMainThread();

  // Alternative data loading is going to finish, breaking the reference cycle
  // here by taking the ownership to a loacl variable.
  RefPtr<FetchDriver> fetchDriver = std::move(mFetchDriver);

  if (mStatus == AlternativeDataStreamListener::CANCELED) {
    // do nothing
    return NS_OK;
  }

  if (mStatus == AlternativeDataStreamListener::FALLBACK) {
    MOZ_ASSERT(fetchDriver);
    return fetchDriver->OnStopRequest(aRequest, aStatusCode);
  }

  MOZ_DIAGNOSTIC_ASSERT(mStatus == AlternativeDataStreamListener::LOADING);

  MOZ_ASSERT(!mAlternativeDataType.IsEmpty() && mPipeAlternativeOutputStream &&
             mPipeAlternativeInputStream);

  mPipeAlternativeOutputStream->Close();
  mPipeAlternativeOutputStream = nullptr;

  // Cleanup the states for alternative data if needed.
  if (NS_FAILED(aStatusCode)) {
    mAlternativeDataCacheEntryId = 0;
    mCacheInfoChannel = nullptr;
    mPipeAlternativeInputStream = nullptr;
  }
  mStatus = AlternativeDataStreamListener::COMPLETED;
  // alternative data loading finish, call FetchDriver::FinishOnStopRequest to
  // continue the final step for the case FetchDriver::OnStopRequest is called
  // earlier than AlternativeDataStreamListener::OnStopRequest
  MOZ_ASSERT(fetchDriver);
  fetchDriver->FinishOnStopRequest(this);
  return NS_OK;
}

NS_IMETHODIMP
AlternativeDataStreamListener::CheckListenerChain() { return NS_OK; }

NS_IMETHODIMP
AlternativeDataStreamListener::OnDataFinished(nsresult aStatus) {
  return NS_OK;
}

//-----------------------------------------------------------------------------
// FetchDriver
//-----------------------------------------------------------------------------

NS_IMPL_ISUPPORTS(FetchDriver, nsIStreamListener, nsIChannelEventSink,
                  nsIInterfaceRequestor, nsIThreadRetargetableStreamListener,
                  nsINetworkInterceptController)

FetchDriver::FetchDriver(SafeRefPtr<InternalRequest> aRequest,
                         nsIPrincipal* aPrincipal, nsILoadGroup* aLoadGroup,
                         nsIEventTarget* aMainThreadEventTarget,
                         nsICookieJarSettings* aCookieJarSettings,
                         PerformanceStorage* aPerformanceStorage,
                         net::ClassificationFlags aTrackingFlags)
    : mPrincipal(aPrincipal),
      mLoadGroup(aLoadGroup),
      mRequest(std::move(aRequest)),
      mODAMutex("FetchDriver::mODAMutex"),
      mMainThreadEventTarget(aMainThreadEventTarget),
      mCookieJarSettings(aCookieJarSettings),
      mPerformanceStorage(aPerformanceStorage),
      mNeedToObserveOnDataAvailable(false),
      mTrackingFlags(aTrackingFlags),
      mIsOn3PCBExceptionList(false),
      mOnStopRequestCalled(false)
#ifdef DEBUG
      ,
      mResponseAvailableCalled(false),
      mFetchCalled(false)
#endif
{
  AssertIsOnMainThread();

  MOZ_ASSERT(mRequest);
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aMainThreadEventTarget);

  mIsTrackingFetch = net::ChannelClassifierUtils::IsTrackingClassificationFlag(
      mTrackingFlags.thirdPartyFlags, false);
}

FetchDriver::~FetchDriver() {
  AssertIsOnMainThread();

  // We assert this since even on failures, we should call
  // FailWithNetworkError().
  MOZ_ASSERT(mResponseAvailableCalled);
}

already_AddRefed<PreloaderBase> FetchDriver::FindPreload(nsIURI* aURI) {
  // Decide if we allow reuse of an existing <link rel=preload as=fetch>
  // response for this request.  First examine this fetch requets itself if it
  // is 'pure' enough to use the response and then try to find a preload.

  if (!mDocument) {
    // Preloads are mapped on the document, no document, no preload.
    return nullptr;
  }
  CORSMode cors;
  switch (mRequest->Mode()) {
    case RequestMode::No_cors:
      cors = CORSMode::CORS_NONE;
      break;
    case RequestMode::Cors:
      cors = mRequest->GetCredentialsMode() == RequestCredentials::Include
                 ? CORSMode::CORS_USE_CREDENTIALS
                 : CORSMode::CORS_ANONYMOUS;
      break;
    default:
      // Can't be satisfied by a preload because preload cannot define any of
      // remaining modes.
      return nullptr;
  }
  if (!mRequest->Headers()->HasOnlySimpleHeaders()) {
    // Preload can't set any headers.
    return nullptr;
  }
  if (!mRequest->GetIntegrity().IsEmpty()) {
    // There is currently no support for SRI checking in the fetch preloader.
    return nullptr;
  }
  if (mRequest->GetCacheMode() != RequestCache::Default) {
    // Preload can only go with the default caching mode.
    return nullptr;
  }
  if (mRequest->SkipServiceWorker()) {
    // Preload can't be forbidden interception.
    return nullptr;
  }
  if (mRequest->GetRedirectMode() != RequestRedirect::Follow) {
    // Preload always follows redirects.
    return nullptr;
  }
  nsAutoCString method;
  mRequest->GetMethod(method);
  if (!method.EqualsLiteral("GET")) {
    // Preload can only do GET, this also eliminates the case we do upload, so
    // no need to check if the request has any body to send out.
    return nullptr;
  }

  // OK, this request can be satisfied by a preloaded response, try to find one.

  auto preloadKey = PreloadHashKey::CreateAsFetch(aURI, cors);
  return mDocument->Preloads().LookupPreload(preloadKey);
}

void FetchDriver::UpdateReferrerInfoFromNewChannel(nsIChannel* aChannel) {
  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aChannel);
  if (!httpChannel) {
    return;
  }

  nsCOMPtr<nsIReferrerInfo> referrerInfo = httpChannel->GetReferrerInfo();
  if (!referrerInfo) {
    return;
  }

  nsAutoCString computedReferrerSpec;
  mRequest->SetReferrerPolicy(referrerInfo->ReferrerPolicy());
  (void)referrerInfo->GetComputedReferrerSpec(computedReferrerSpec);
  mRequest->SetReferrer(computedReferrerSpec);
}

nsresult FetchDriver::Fetch(AbortSignalImpl* aSignalImpl,
                            FetchDriverObserver* aObserver) {
  AssertIsOnMainThread();
#ifdef DEBUG
  MOZ_ASSERT(!mFetchCalled);
  mFetchCalled = true;
#endif

  mObserver = aObserver;

  // FIXME(nsm): Deal with HSTS.

  MOZ_RELEASE_ASSERT(!mRequest->IsSynchronous(),
                     "Synchronous fetch not supported");

  auto principalInfo = std::make_unique<mozilla::ipc::PrincipalInfo>();
  nsresult rv = PrincipalToPrincipalInfo(mPrincipal, principalInfo.get());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  mRequest->SetPrincipalInfo(std::move(principalInfo));

  // If the signal is aborted, it's time to inform the observer and terminate
  // the operation.
  if (aSignalImpl) {
    if (aSignalImpl->Aborted()) {
      FetchDriverAbortActions(aSignalImpl);
      return NS_OK;
    }

    Follow(aSignalImpl);
  }

  rv = HttpFetch(mRequest->GetPreferredAlternativeDataType());
  if (NS_FAILED(rv)) {
    FailWithNetworkError(rv);
  }

  // Any failure is handled by FailWithNetworkError notifying the aObserver.
  return NS_OK;
}

// This function implements the "HTTP Fetch" algorithm from the Fetch spec.
// Functionality is often split between here, the CORS listener proxy and the
// Necko HTTP implementation.
nsresult FetchDriver::HttpFetch(
    const nsACString& aPreferredAlternativeDataType) {
  MOZ_ASSERT(NS_IsMainThread());

  // Step 1. "Let response be null."
  mResponse = nullptr;
  mOnStopRequestCalled = false;
  nsresult rv;

  nsCOMPtr<nsIIOService> ios = do_GetIOService(&rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIURI> uri = mRequest->GetURL();

  // Unsafe requests aren't allowed with when using no-core mode.
  if (mRequest->Mode() == RequestMode::No_cors && mRequest->UnsafeRequest() &&
      (!mRequest->HasSimpleMethod() ||
       !mRequest->Headers()->HasOnlySimpleHeaders())) {
    MOZ_ASSERT(false, "The API should have caught this");
    return NS_ERROR_DOM_BAD_URI;
  }

  // non-GET requests aren't allowed for blob.
  if (IsBlobURI(uri)) {
    nsAutoCString method;
    mRequest->GetMethod(method);
    if (!method.EqualsLiteral("GET")) {
      return NS_ERROR_DOM_NETWORK_ERR;
    }
  }

  RefPtr<PreloaderBase> fetchPreload = FindPreload(uri);
  if (fetchPreload) {
    fetchPreload->RemoveSelf(mDocument);
    fetchPreload->NotifyUsage(mDocument, PreloaderBase::LoadBackground::Keep);

    rv = fetchPreload->AsyncConsume(this);
    if (NS_SUCCEEDED(rv)) {
      mFromPreload = true;

      mChannel = fetchPreload->Channel();
      MOZ_ASSERT(mChannel);
      mChannel->SetNotificationCallbacks(this);

      // Copied from AsyncOnChannelRedirect.
      for (const auto& redirect : fetchPreload->Redirects()) {
        nsCOMPtr<nsIURI> uriNoFragment = redirect.URINoFragment();
        if (redirect.Flags() & nsIChannelEventSink::REDIRECT_INTERNAL) {
          mRequest->SetURLForInternalRedirect(redirect.Flags(),
                                              WrapNotNull(uriNoFragment.get()),
                                              redirect.Fragment());
        } else {
          mRequest->AddURL(WrapNotNull(uriNoFragment.get()),
                           redirect.Fragment());
        }
      }

      return NS_OK;
    }

    // The preload failed to be consumed.  Behave like there were no preload.
    fetchPreload = nullptr;
  }

  // Step 2 deals with letting ServiceWorkers intercept requests. This is
  // handled by Necko after the channel is opened.
  // FIXME(nsm): Bug 1119026: The channel's skip service worker flag should be
  // set based on the Request's flag.

  // Step 3.1 "If the CORS preflight flag is set and one of these conditions is
  // true..." is handled by the CORS proxy.
  //
  // Step 3.2 "Set request's skip service worker flag." This isn't required
  // since Necko will fall back to the network if the ServiceWorker does not
  // respond with a valid Response.
  //
  // NS_StartCORSPreflight() will automatically kick off the original request
  // if it succeeds, so we need to have everything setup for the original
  // request too.

  // Step 3.3 "Let credentials flag be set if one of
  //  - request's credentials mode is "include"
  //  - request's credentials mode is "same-origin" and either the CORS flag
  //    is unset or response tainting is "opaque"
  // is true, and unset otherwise."

  // Set skip serviceworker flag.
  // While the spec also gates on the client being a ServiceWorker, we can't
  // infer that here. Instead we rely on callers to set the flag correctly.
  const nsLoadFlags bypassFlag = mRequest->SkipServiceWorker()
                                     ? nsIChannel::LOAD_BYPASS_SERVICE_WORKER
                                     : 0;

  nsSecurityFlags secFlags = 0;
  if (mRequest->Mode() == RequestMode::Cors) {
    secFlags |= nsILoadInfo::SEC_REQUIRE_CORS_INHERITS_SEC_CONTEXT;
  } else if (mRequest->Mode() == RequestMode::Same_origin ||
             mRequest->Mode() == RequestMode::Navigate) {
    secFlags |= nsILoadInfo::SEC_REQUIRE_SAME_ORIGIN_INHERITS_SEC_CONTEXT;
  } else if (mRequest->Mode() == RequestMode::No_cors) {
    secFlags |= nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_INHERITS_SEC_CONTEXT;
  } else {
    MOZ_ASSERT_UNREACHABLE("Unexpected request mode!");
    return NS_ERROR_UNEXPECTED;
  }

  if (mRequest->GetRedirectMode() != RequestRedirect::Follow) {
    secFlags |= nsILoadInfo::SEC_DONT_FOLLOW_REDIRECTS;
  }

  // This handles the use credentials flag in "HTTP
  // network or cache fetch" in the spec and decides whether to transmit
  // cookies and other identifying information.
  if (mRequest->GetCredentialsMode() == RequestCredentials::Include) {
    secFlags |= nsILoadInfo::SEC_COOKIES_INCLUDE;
  } else if (mRequest->GetCredentialsMode() == RequestCredentials::Omit) {
    secFlags |= nsILoadInfo::SEC_COOKIES_OMIT;
  } else if (mRequest->GetCredentialsMode() ==
             RequestCredentials::Same_origin) {
    secFlags |= nsILoadInfo::SEC_COOKIES_SAME_ORIGIN;
  } else {
    MOZ_ASSERT_UNREACHABLE("Unexpected credentials mode!");
    return NS_ERROR_UNEXPECTED;
  }

  // From here on we create a channel and set its properties with the
  // information from the InternalRequest. This is an implementation detail.
  MOZ_ASSERT(mLoadGroup);
  nsCOMPtr<nsIChannel> chan;

  nsLoadFlags loadFlags = nsIRequest::LOAD_BACKGROUND | bypassFlag;
  if (mDocument) {
    MOZ_ASSERT(mDocument->NodePrincipal() == mPrincipal);
    MOZ_ASSERT(mDocument->CookieJarSettings() == mCookieJarSettings);
    rv = NS_NewChannel(getter_AddRefs(chan), uri, mDocument, secFlags,
                       mRequest->ContentPolicyType(),
                       nullptr,             /* aPerformanceStorage */
                       mLoadGroup, nullptr, /* aCallbacks */
                       loadFlags, ios);
  } else if (mClientInfo.isSome()) {
    rv = NS_NewChannel(
        getter_AddRefs(chan), uri, mPrincipal, mClientInfo.ref(), mController,
        secFlags, mRequest->ContentPolicyType(), mCookieJarSettings,
        mPerformanceStorage, mLoadGroup, nullptr, /* aCallbacks */
        loadFlags, ios, /* aSandboxFlags */ 0, mAssociatedBrowsingContextID);
  } else {
    nsCOMPtr<nsIPrincipal> principal = mPrincipal;
    if (principal->IsSystemPrincipal() &&
        mRequest->GetTriggeringPrincipalOverride()) {
      rv = NS_NewChannelWithTriggeringPrincipal(
          getter_AddRefs(chan), uri, mPrincipal,
          mRequest->GetTriggeringPrincipalOverride(), secFlags,
          mRequest->ContentPolicyType(), mCookieJarSettings,
          mPerformanceStorage, mLoadGroup, nullptr, /* aCallbacks */
          loadFlags, ios);
    } else {
      rv = NS_NewChannel(getter_AddRefs(chan), uri, mPrincipal, secFlags,
                         mRequest->ContentPolicyType(), mCookieJarSettings,
                         mPerformanceStorage, mLoadGroup,
                         nullptr, /* aCallbacks */
                         loadFlags, ios);
    }
  }
  NS_ENSURE_SUCCESS(rv, rv);

  // https://fetch.spec.whatwg.org/#concept-fetch
  // MIME sniffing does not apply to fetch requests, only to browsing contexts.
  // no-cors requests may need sniffing for Opaque Response Blocking (ORB).
  if (mRequest->Mode() != RequestMode::No_cors) {
    nsCOMPtr<nsILoadInfo> loadInfo = chan->LoadInfo();
    loadInfo->SetSkipContentSniffing(true);
  }

  if (mCSPEventListener) {
    nsCOMPtr<nsILoadInfo> loadInfo = chan->LoadInfo();
    rv = loadInfo->SetCspEventListener(mCSPEventListener);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  {
    nsCOMPtr<nsILoadInfo> loadInfo = chan->LoadInfo();
    rv = loadInfo->SetLoadingEmbedderPolicy(mRequest->GetEmbedderPolicy());
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (mAssociatedBrowsingContextID) {
    nsCOMPtr<nsILoadInfo> loadInfo = chan->LoadInfo();
    rv = loadInfo->SetAssociatedBrowsingContextID(mAssociatedBrowsingContextID);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (mIsThirdPartyContext.isSome()) {
    nsCOMPtr<nsILoadInfo> loadInfo = chan->LoadInfo();
    rv = loadInfo->SetIsInThirdPartyContext(mIsThirdPartyContext.ref());
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (mIsOn3PCBExceptionList) {
    nsCOMPtr<nsILoadInfo> loadInfo = chan->LoadInfo();
    rv = loadInfo->SetIsOn3PCBExceptionList(mIsOn3PCBExceptionList);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  nsCOMPtr<nsILoadInfo> loadInfo = chan->LoadInfo();
  rv = loadInfo->SetTriggeringFirstPartyClassificationFlags(
      mTrackingFlags.firstPartyFlags);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = loadInfo->SetTriggeringThirdPartyClassificationFlags(
      mTrackingFlags.thirdPartyFlags);
  NS_ENSURE_SUCCESS(rv, rv);

  // If the fetch is created by FetchEvent.request or NavigationPreload request,
  // corresponding InterceptedHttpChannel information need to propagate to the
  // channel of the fetch.
  if (mRequest->GetInterceptionTriggeringPrincipalInfo()) {
    auto principalOrErr = mozilla::ipc::PrincipalInfoToPrincipal(
        *(mRequest->GetInterceptionTriggeringPrincipalInfo().get()));
    if (!principalOrErr.isErr()) {
      nsCOMPtr<nsIPrincipal> principal = principalOrErr.unwrap();

      nsTArray<nsCOMPtr<nsIRedirectHistoryEntry>> redirectChain;
      if (!mRequest->InterceptionRedirectChain().IsEmpty()) {
        for (const RedirectHistoryEntryInfo& entryInfo :
             mRequest->InterceptionRedirectChain()) {
          nsCOMPtr<nsIRedirectHistoryEntry> entry =
              mozilla::ipc::RHEntryInfoToRHEntry(entryInfo);
          redirectChain.AppendElement(entry);
        }
      }

      nsCOMPtr<nsILoadInfo> loadInfo = chan->LoadInfo();
      MOZ_ASSERT(loadInfo);
      loadInfo->SetInterceptionInfo(new mozilla::net::InterceptionInfo(
          principal, mRequest->InterceptionContentPolicyType(), redirectChain,
          mRequest->InterceptionFromThirdParty()));
    }
  }

  // Insert ourselves into the notification callbacks chain so we can set
  // headers on redirects.
#ifdef DEBUG
  {
    nsCOMPtr<nsIInterfaceRequestor> notificationCallbacks;
    chan->GetNotificationCallbacks(getter_AddRefs(notificationCallbacks));
    MOZ_ASSERT(!notificationCallbacks);
  }
#endif
  chan->SetNotificationCallbacks(this);

  nsCOMPtr<nsIClassOfService> cos(do_QueryInterface(chan));
  // Mark channel as urgent-start if the Fetch is triggered by user input
  // events.
  if (cos && UserActivation::IsHandlingUserInput()) {
    cos->AddClassFlags(nsIClassOfService::UrgentStart);
  }

  // Step 3.5 begins "HTTP network or cache fetch".
  // HTTP network or cache fetch
  // ---------------------------
  // Step 1 "Let HTTPRequest..." The channel is the HTTPRequest.
  nsCOMPtr<nsIHttpChannel> httpChan = do_QueryInterface(chan);
  if (httpChan) {
    // Copy the method.
    nsAutoCString method;
    mRequest->GetMethod(method);
    rv = httpChan->SetRequestMethod(method);
    NS_ENSURE_SUCCESS(rv, rv);

    // Set the same headers.
    SetRequestHeaders(httpChan, false, false);

    // Step 5 of https://fetch.spec.whatwg.org/#main-fetch
    // If request's referrer policy is the empty string and request's client is
    // non-null, then set request's referrer policy to request's client's
    // associated referrer policy.
    // Basically, "client" is not in our implementation, we use
    // EnvironmentReferrerPolicy of the worker or document context
    ReferrerPolicy referrerPolicy = mRequest->GetEnvironmentReferrerPolicy();
    if (mRequest->ReferrerPolicy_() == ReferrerPolicy::_empty) {
      mRequest->SetReferrerPolicy(referrerPolicy);
    }
    // Step 6 of https://fetch.spec.whatwg.org/#main-fetch
    // If request’s referrer policy is the empty string,
    // then set request’s referrer policy to the user-set default policy.
    if (mRequest->ReferrerPolicy_() == ReferrerPolicy::_empty) {
      nsCOMPtr<nsILoadInfo> loadInfo = httpChan->LoadInfo();
      bool isPrivate = loadInfo->GetOriginAttributes().IsPrivateBrowsing();
      referrerPolicy =
          ReferrerInfo::GetDefaultReferrerPolicy(httpChan, uri, isPrivate);
      mRequest->SetReferrerPolicy(referrerPolicy);
    }

    rv = FetchUtil::SetRequestReferrer(mPrincipal, mDocument, httpChan,
                                       *mRequest);
    NS_ENSURE_SUCCESS(rv, rv);

    // Bug 1120722 - Authorization will be handled later.
    // Auth may require prompting, we don't support it yet.
    // The next patch in this same bug prevents this from aborting the request.
    // Credentials checks for CORS are handled by nsCORSListenerProxy,

    nsCOMPtr<nsIHttpChannelInternal> internalChan = do_QueryInterface(httpChan);

    rv = internalChan->SetRequestMode(mRequest->Mode());
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    // Conversion between enumerations is safe due to static asserts in
    // dom/workers/ServiceWorkerManager.cpp
    rv = internalChan->SetRedirectMode(
        static_cast<uint32_t>(mRequest->GetRedirectMode()));
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    mRequest->MaybeSkipCacheIfPerformingRevalidation();
    rv = internalChan->SetFetchCacheMode(
        static_cast<uint32_t>(mRequest->GetCacheMode()));
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    nsCOMPtr<nsILoadInfo> loadInfo = chan->LoadInfo();
    rv = loadInfo->SetIntegrityMetadata(mRequest->GetIntegrity());
    MOZ_ASSERT(NS_SUCCEEDED(rv));

    // Set the initiator type
    nsCOMPtr<nsITimedChannel> timedChannel(do_QueryInterface(httpChan));
    if (timedChannel) {
      timedChannel->SetInitiatorType(u"fetch"_ns);
    }
  }

  // Step 5. Proxy authentication will be handled by Necko.

  // Continue setting up 'HTTPRequest'. Content-Type and body data.
  nsCOMPtr<nsIUploadChannel2> uploadChan = do_QueryInterface(chan);
  if (uploadChan) {
    nsAutoCString contentType;
    ErrorResult result;
    mRequest->Headers()->GetFirst("content-type"_ns, contentType, result);
    // We don't actually expect "result" to have failed here: that only happens
    // for invalid header names.  But if for some reason it did, just propagate
    // it out.
    if (result.Failed()) {
      return result.StealNSResult();
    }

    // Now contentType is the header that was set in mRequest->Headers(), or a
    // void string if no header was set.
#ifdef DEBUG
    bool hasContentTypeHeader =
        mRequest->Headers()->Has("content-type"_ns, result);
    MOZ_ASSERT(!result.Failed());
    MOZ_ASSERT_IF(!hasContentTypeHeader, contentType.IsVoid());
#endif  // DEBUG

    int64_t bodyLength;
    nsCOMPtr<nsIInputStream> bodyStream;
    mRequest->GetBody(getter_AddRefs(bodyStream), &bodyLength);
    if (bodyStream) {
      nsAutoCString method;
      mRequest->GetMethod(method);

      // Streaming uploads (ReadableStream) require HTTP/2 or HTTP/3.
      // Since browsers only use HTTP/2 over TLS, reject non-https URLs early.
      if (mRequest->HasStreamBody() && !uri->SchemeIs("https")) {
        // Reject streaming upload on non-https (which implies HTTP/1.1)
        FailWithNetworkError(NS_ERROR_DOM_NETWORK_ERR);
        return NS_ERROR_DOM_NETWORK_ERR;
      }

      // Mark the channel as streaming BEFORE calling ExplicitSetUploadStream
      // so InternalSetUploadStream knows to skip normalization.
      if (mRequest->HasStreamBody()) {
        nsCOMPtr<nsIHttpChannel> httpChan = do_QueryInterface(chan);
        RefPtr<mozilla::net::HttpBaseChannel> baseChan =
            do_QueryObject(httpChan);
        if (baseChan) {
          baseChan->SetUploadStreamIsStreaming(true);
        }
      }

      rv = uploadChan->ExplicitSetUploadStream(bodyStream, contentType,
                                               bodyLength, method);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  // If preflight is required, start a "CORS preflight fetch"
  // https://fetch.spec.whatwg.org/#cors-preflight-fetch-0. All the
  // implementation is handled by the http channel calling into
  // nsCORSListenerProxy. We just inform it which unsafe headers are included
  // in the request.
  if (mRequest->Mode() == RequestMode::Cors) {
    AutoTArray<nsCString, 5> unsafeHeaders;
    mRequest->Headers()->GetUnsafeHeaders(unsafeHeaders);
    nsCOMPtr<nsILoadInfo> loadInfo = chan->LoadInfo();
    // Request constructor step 39.3: a body with a null source sets the
    // use-CORS-preflight flag, even with a safelisted method and headers.
    loadInfo->SetCorsPreflightInfo(unsafeHeaders, mRequest->HasStreamBody());
  }

  if (mIsTrackingFetch && StaticPrefs::network_http_tailing_enabled() && cos) {
    cos->AddClassFlags(nsIClassOfService::Throttleable |
                       nsIClassOfService::Tail);
  }

  const auto fetchPriority = ToFetchPriority(mRequest->GetPriorityMode());
  if (cos) {
    cos->SetFetchPriorityDOM(fetchPriority);
  }

  if (nsCOMPtr<nsISupportsPriority> p = do_QueryInterface(chan)) {
    if (mIsTrackingFetch &&
        StaticPrefs::privacy_trackingprotection_lower_network_priority()) {
      p->SetPriority(nsISupportsPriority::PRIORITY_LOWEST);
    } else if (StaticPrefs::network_fetchpriority_enabled()) {
      // According to step 15 of https://fetch.spec.whatwg.org/#concept-fetch
      // request’s priority, initiator, destination, and render-blocking are
      // used in an implementation-defined manner to set the internal priority.
      // See corresponding preferences in StaticPrefList.yaml for more context.
      const int32_t supportsPriorityDelta = [this, &fetchPriority]() {
        auto destination = mRequest->GetInterceptionTriggeringPrincipalInfo()
                               ? mRequest->InterceptionDestination()
                               : mRequest->Destination();
        switch (destination) {
          case RequestDestination::Font:
            return FETCH_PRIORITY_ADJUSTMENT_FOR(link_preload_font,
                                                 fetchPriority);
          case RequestDestination::Style:
            return FETCH_PRIORITY_ADJUSTMENT_FOR(link_preload_style,
                                                 fetchPriority);
          case RequestDestination::Script:
          case RequestDestination::Audioworklet:
          case RequestDestination::Paintworklet:
          case RequestDestination::Sharedworker:
          case RequestDestination::Worker:
          case RequestDestination::Xslt:
          case RequestDestination::Json:
          case RequestDestination::Text:
            return FETCH_PRIORITY_ADJUSTMENT_FOR(link_preload_script,
                                                 fetchPriority);
          case RequestDestination::Image:
            return FETCH_PRIORITY_ADJUSTMENT_FOR(images, fetchPriority);
          case RequestDestination::Audio:
          case RequestDestination::Track:
          case RequestDestination::Video:
            return FETCH_PRIORITY_ADJUSTMENT_FOR(media, fetchPriority);
          case RequestDestination::Document:
          case RequestDestination::Embed:
          case RequestDestination::Frame:
          case RequestDestination::Iframe:
          case RequestDestination::Manifest:
          case RequestDestination::Object:
          case RequestDestination::Report:
          case RequestDestination::_empty:
            return FETCH_PRIORITY_ADJUSTMENT_FOR(global_fetch_api,
                                                 fetchPriority);
        };
        MOZ_ASSERT_UNREACHABLE("Unknown destination");
        return 0;
      }();
      p->SetPriority(mRequest->InternalPriority());
      p->AdjustPriority(supportsPriorityDelta);
    }
  }

  NotifyNetworkMonitorAlternateStack(chan, std::move(mOriginStack));
  if (mObserver && httpChan) {
    mObserver->OnNotifyNetworkMonitorAlternateStack(httpChan->ChannelId());
  }

  // Should set a Content-Range header for blob scheme, and also slice the
  // blob appropriately, so we process the Range header here for later use.
  RefPtr<BlobURLChannel> blobChan = do_QueryObject(chan);
  if (blobChan) {
    ErrorResult result;
    nsAutoCString range;
    mRequest->Headers()->Get("Range"_ns, range, result);
    MOZ_ASSERT(!result.Failed());
    if (!range.IsVoid()) {
      rv = blobChan->SetRequestContentRangeHeader(range);
      if (NS_FAILED(rv)) {
        return rv;
      }
    }
  }

  // if the preferred alternative data type in InternalRequest is not empty, set
  // the data type on the created channel and also create a
  // AlternativeDataStreamListener to be the stream listener of the channel.
  if (!aPreferredAlternativeDataType.IsEmpty()) {
    nsCOMPtr<nsICacheInfoChannel> cic = do_QueryInterface(chan);
    if (cic) {
      cic->PreferAlternativeDataType(
          aPreferredAlternativeDataType, ""_ns,
          nsICacheInfoChannel::PreferredAlternativeDataDeliveryType::ASYNC);
      MOZ_ASSERT(!mAltDataListener);
      mAltDataListener = new AlternativeDataStreamListener(
          this, chan, aPreferredAlternativeDataType);
      rv = chan->AsyncOpen(mAltDataListener);
    } else {
      rv = chan->AsyncOpen(this);
    }
  } else {
    // Integrity check cannot be done on alt-data yet.
    if (mRequest->GetIntegrity().IsEmpty()) {
      nsCOMPtr<nsICacheInfoChannel> cic = do_QueryInterface(chan);
      if (cic && StaticPrefs::javascript_options_wasm_caching() &&
          !mRequest->SkipWasmCaching()) {
        cic->PreferAlternativeDataType(
            FetchUtil::GetWasmAltDataType(),
            nsLiteralCString(WASM_CONTENT_TYPE),
            nsICacheInfoChannel::PreferredAlternativeDataDeliveryType::
                SERIALIZE);
      }
    }

    rv = chan->AsyncOpen(this);
  }

  if (NS_FAILED(rv)) {
    return rv;
  }

  // Step 4 onwards of "HTTP Fetch" is handled internally by Necko.

  mChannel = std::move(chan);
  return NS_OK;
}

SafeRefPtr<InternalResponse> FetchDriver::BeginAndGetFilteredResponse(
    SafeRefPtr<InternalResponse> aResponse, bool aFoundOpaqueRedirect) {
  MOZ_ASSERT(aResponse);
  MOZ_ASSERT(!mRequest->GetURLListWithoutFragment().IsEmpty());
  aResponse->SetURLList(mRequest->GetURLListWithoutFragment());
  SafeRefPtr<InternalResponse> filteredResponse;
  if (aFoundOpaqueRedirect) {
    filteredResponse = aResponse->OpaqueRedirectResponse();
  } else {
    switch (mRequest->GetResponseTainting()) {
      case LoadTainting::Basic:
        filteredResponse = aResponse->BasicResponse();
        break;
      case LoadTainting::CORS:
        filteredResponse = aResponse->CORSResponse();
        break;
      case LoadTainting::Opaque: {
        filteredResponse = aResponse->OpaqueResponse();
        nsresult rv = filteredResponse->GeneratePaddingInfo();
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return nullptr;
        }
        break;
      }
      default:
        MOZ_CRASH("Unexpected case");
    }
  }

  MOZ_ASSERT(filteredResponse);
  MOZ_ASSERT(mObserver);
  MOZ_ASSERT(filteredResponse);
  if (!ShouldCheckSRI(*mRequest, *filteredResponse)) {
    // Need to keep mObserver alive.
    RefPtr<FetchDriverObserver> observer = mObserver;
    observer->OnResponseAvailable(filteredResponse.clonePtr());
#ifdef DEBUG
    mResponseAvailableCalled = true;
#endif
  }

  return filteredResponse;
}

void FetchDriver::FailWithNetworkError(nsresult rv) {
  AssertIsOnMainThread();
  if (mObserver) {
    // Need to keep mObserver alive.
    RefPtr<FetchDriverObserver> observer = mObserver;
    observer->OnResponseAvailable(InternalResponse::NetworkError(rv));
#ifdef DEBUG
    mResponseAvailableCalled = true;
#endif
  }

  // mObserver could be null after OnResponseAvailable().
  if (mObserver) {
    mObserver->OnReportPerformanceTiming();
    mObserver->OnResponseEnd(FetchDriverObserver::eByNetworking,
                             JS::UndefinedHandleValue);
    mObserver = nullptr;
  }

  mChannel = nullptr;
  Unfollow();
}

NS_IMETHODIMP
FetchDriver::OnStartRequest(nsIRequest* aRequest) MOZ_CAN_RUN_SCRIPT_BOUNDARY {
  FETCH_LOG(
      ("FetchDriver::OnStartRequest this=%p, request=%p", this, aRequest));
  AssertIsOnMainThread();

  // Note, this can be called multiple times if we are doing an opaqueredirect.
  // In that case we will get a simulated OnStartRequest() and then the real
  // channel will call in with an errored OnStartRequest().

  if (mFromPreload && mAborted) {
    aRequest->CancelWithReason(NS_BINDING_ABORTED,
                               "FetchDriver::OnStartRequest aborted"_ns);
    return NS_BINDING_ABORTED;
  }

  if (!mChannel) {
    MOZ_ASSERT(!mObserver);
    return NS_BINDING_ABORTED;
  }

  nsresult rv;
  aRequest->GetStatus(&rv);
  if (NS_FAILED(rv)) {
    FailWithNetworkError(rv);
    return rv;
  }

  // We should only get to the following code once.
  MOZ_ASSERT(!mPipeOutputStream);

  if (!mObserver) {
    MOZ_ASSERT(false, "We should have mObserver here.");
    FailWithNetworkError(NS_ERROR_UNEXPECTED);
    return NS_ERROR_UNEXPECTED;
  }

  mNeedToObserveOnDataAvailable = mObserver->NeedOnDataAvailable();

  SafeRefPtr<InternalResponse> response;
  nsCOMPtr<nsIChannel> channel = do_QueryInterface(aRequest);
  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aRequest);

  // On a successful redirect we perform the following substeps of HTTP Fetch,
  // step 5, "redirect status", step 11.

  bool foundOpaqueRedirect = false;

  nsAutoCString contentType(VoidCString());

  int64_t contentLength = InternalResponse::UNKNOWN_BODY_SIZE;
  rv = channel->GetContentLength(&contentLength);
  MOZ_ASSERT_IF(NS_FAILED(rv),
                contentLength == InternalResponse::UNKNOWN_BODY_SIZE);

  if (httpChannel) {
    // Streaming uploads require HTTP/2 or HTTP/3 on the wire. A response
    // synthesized by a service worker never touched the network, so its
    // protocol version carries no information about the upload; only check
    // responses that actually came from the network.
    if (mRequest->HasStreamBody()) {
      nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
      bool synthesizedByServiceWorker =
          loadInfo && loadInfo->GetServiceWorkerTaintingSynthesized();
      if (!synthesizedByServiceWorker) {
        nsAutoCString protocolVersion;
        rv = httpChannel->GetProtocolVersion(protocolVersion);
        if (NS_SUCCEEDED(rv) && !protocolVersion.EqualsLiteral("h2") &&
            !protocolVersion.EqualsLiteral("h3")) {
          FailWithNetworkError(NS_ERROR_DOM_NETWORK_ERR);
          return NS_ERROR_DOM_NETWORK_ERR;
        }
      }
    }

    channel->GetContentType(contentType);

    uint32_t responseStatus = 0;
    rv = httpChannel->GetResponseStatus(&responseStatus);
    if (NS_FAILED(rv)) {
      FailWithNetworkError(rv);
      return rv;
    }

    if (mozilla::net::nsHttpChannel::IsRedirectStatus(responseStatus)) {
      if (mRequest->GetRedirectMode() == RequestRedirect::Error) {
        FailWithNetworkError(NS_BINDING_ABORTED);
        return NS_BINDING_FAILED;
      }
      if (mRequest->GetRedirectMode() == RequestRedirect::Manual) {
        foundOpaqueRedirect = true;
      }
    }

    nsAutoCString statusText;
    rv = httpChannel->GetResponseStatusText(statusText);
    MOZ_ASSERT(NS_SUCCEEDED(rv));

    response = MakeSafeRefPtr<InternalResponse>(responseStatus, statusText,
                                                mRequest->GetCredentialsMode());

    auto principalInfo = std::make_unique<mozilla::ipc::PrincipalInfo>();
    nsresult rv = PrincipalToPrincipalInfo(mPrincipal, principalInfo.get());
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    response->SetPrincipalInfo(std::move(principalInfo));

    response->Headers()->FillResponseHeaders(httpChannel);

    // If Content-Encoding or Transfer-Encoding headers are set, then the actual
    // Content-Length (which refer to the decoded data) is obscured behind the
    // encodings.
    ErrorResult result;
    if (response->Headers()->Has("content-encoding"_ns, result) ||
        response->Headers()->Has("transfer-encoding"_ns, result)) {
      // We cannot trust the content-length when content-encoding or
      // transfer-encoding are set.  There are many servers which just
      // get this wrong.
      contentLength = InternalResponse::UNKNOWN_BODY_SIZE;
    }
    MOZ_ASSERT(!result.Failed());
  } else {
    // Should set a Content-Range header for blob scheme
    // (https://fetch.spec.whatwg.org/#scheme-fetch)
    nsAutoCString contentRange(VoidCString());
    RefPtr<BlobURLChannel> blobChan = do_QueryObject(mChannel);
    if (blobChan && blobChan->GetResponseContentRange()) {
      blobChan->GetResponseContentRange()->AsHeader(contentRange);
    }

    response = MakeSafeRefPtr<InternalResponse>(
        contentRange.IsVoid() ? 200 : 206,
        contentRange.IsVoid() ? "OK"_ns : "Partial Content"_ns,
        mRequest->GetCredentialsMode());

    IgnoredErrorResult result;
    if (!contentRange.IsVoid()) {
      response->Headers()->Append("Content-Range"_ns, contentRange, result);
      MOZ_ASSERT(!result.Failed());
    }

    nsCOMPtr<nsIBaseChannel> baseChan = do_QueryInterface(mChannel);
    if (baseChan) {
      RefPtr<CMimeType> fullMimeType(baseChan->FullMimeType());
      if (fullMimeType) {
        fullMimeType->Serialize(contentType);
      }
    }
    if (contentType.IsVoid()) {
      channel->GetContentType(contentType);
      if (!contentType.IsEmpty()) {
        nsAutoCString contentCharset;
        channel->GetContentCharset(contentCharset);
        if (NS_SUCCEEDED(rv) && !contentCharset.IsEmpty()) {
          contentType += ";charset="_ns + contentCharset;
        }
      }
    }

    response->Headers()->Append("Content-Type"_ns, contentType, result);
    MOZ_ASSERT(!result.Failed());

    if (contentLength >= 0) {
      nsAutoCString contentLenStr;
      contentLenStr.AppendInt(contentLength);

      IgnoredErrorResult result;
      response->Headers()->Append("Content-Length"_ns, contentLenStr, result);
      MOZ_ASSERT(!result.Failed());
    }
  }

  nsCOMPtr<nsICacheInfoChannel> cic = do_QueryInterface(aRequest);
  if (cic) {
    if (mAltDataListener) {
      // Skip the case that mAltDataListener->Status() equals to FALLBACK, that
      // means the opened channel for alternative data loading is reused for
      // loading the main data.
      if (mAltDataListener->Status() !=
          AlternativeDataStreamListener::FALLBACK) {
        // Verify the cache ID is the same with from alternative data cache.
        // If the cache ID is different, droping the alternative data loading,
        // otherwise setup the response's alternative body and cacheInfoChannel.
        uint64_t cacheEntryId = 0;
        if (NS_SUCCEEDED(cic->GetCacheEntryId(&cacheEntryId)) &&
            cacheEntryId !=
                mAltDataListener->GetAlternativeDataCacheEntryId()) {
          mAltDataListener->Cancel();
        } else {
          // AlternativeDataStreamListener::OnStartRequest had already been
          // called, the alternative data input stream and cacheInfo channel
          // must be created.
          nsCOMPtr<nsICacheInfoChannel> cacheInfo =
              mAltDataListener->GetCacheInfoChannel();
          nsCOMPtr<nsIInputStream> altInputStream =
              mAltDataListener->GetAlternativeInputStream();
          MOZ_ASSERT(altInputStream && cacheInfo);
          response->SetAlternativeBody(altInputStream);
          nsMainThreadPtrHandle<nsICacheInfoChannel> handle(
              new nsMainThreadPtrHolder<nsICacheInfoChannel>(
                  "nsICacheInfoChannel", cacheInfo, false));
          response->SetCacheInfoChannel(handle);
        }
      } else if (!mAltDataListener->GetAlternativeDataType().IsEmpty()) {
        // If the status is FALLBACK and the
        // mAltDataListener::mAlternativeDataType is not empty, that means the
        // data need to be saved into cache, setup the response's
        // nsICacheInfoChannel for caching the data after loading.
        nsMainThreadPtrHandle<nsICacheInfoChannel> handle(
            new nsMainThreadPtrHolder<nsICacheInfoChannel>(
                "nsICacheInfoChannel", cic, false));
        response->SetCacheInfoChannel(handle);
      }
    } else if (!cic->PreferredAlternativeDataTypes().IsEmpty()) {
      MOZ_ASSERT(cic->PreferredAlternativeDataTypes().Length() == 1);
      MOZ_ASSERT(cic->PreferredAlternativeDataTypes()[0].type().Equals(
          FetchUtil::GetWasmAltDataType()));
      MOZ_ASSERT(
          cic->PreferredAlternativeDataTypes()[0].contentType().EqualsLiteral(
              WASM_CONTENT_TYPE));

      if (contentType.EqualsLiteral(WASM_CONTENT_TYPE)) {
        // We want to attach the CacheInfoChannel to the response object such
        // that we can track its origin when the Response object is manipulated
        // by JavaScript code. This is important for WebAssembly, which uses
        // fetch to query its sources in JavaScript and transfer the Response
        // object to other function responsible for storing the alternate data
        // using the CacheInfoChannel.
        nsMainThreadPtrHandle<nsICacheInfoChannel> handle(
            new nsMainThreadPtrHolder<nsICacheInfoChannel>(
                "nsICacheInfoChannel", cic, false));
        response->SetCacheInfoChannel(handle);
      }
    }
  }

  // Fetch spec Main Fetch step 21: ignore body for head/connect methods.
  nsAutoCString method;
  mRequest->GetMethod(method);
  if (!(method.EqualsLiteral("HEAD") || method.EqualsLiteral("CONNECT"))) {
    // We open a pipe so that we can immediately set the pipe's read end as the
    // response's body. Setting the segment size to UINT32_MAX means that the
    // pipe has infinite space. The nsIChannel will continue to buffer data in
    // xpcom events even if we block on a fixed size pipe.  It might be possible
    // to suspend the channel and then resume when there is space available, but
    // for now use an infinite pipe to avoid blocking.
    nsCOMPtr<nsIInputStream> pipeInputStream;
    NS_NewPipe(getter_AddRefs(pipeInputStream),
               getter_AddRefs(mPipeOutputStream), 0, /* default segment size */
               UINT32_MAX /* infinite pipe */,
               true /* non-blocking input, otherwise you deadlock */,
               false /* blocking output, since the pipe is 'in'finite */);
    response->SetBody(pipeInputStream, contentLength);
  }

  RefPtr<mozilla::dom::BlobURLChannel> bc = do_QueryObject(aRequest);
  if (bc) {
    RefPtr<mozilla::dom::BlobImpl> blobImpl;
    rv = bc->GetBackingBlob(getter_AddRefs(blobImpl));
    if (!NS_WARN_IF(NS_FAILED(rv))) {
      response->SetBodyBlobImpl(blobImpl);
    }
  }

  // If the request is a file channel, then remember the local path to
  // that file so we can later create File blobs rather than plain ones.
  nsCOMPtr<nsIFileChannel> fc = do_QueryInterface(aRequest);
  if (fc) {
    nsCOMPtr<nsIFile> file;
    rv = fc->GetFile(getter_AddRefs(file));
    if (!NS_WARN_IF(NS_FAILED(rv))) {
      nsAutoString path;
      file->GetPath(path);
      response->SetBodyLocalPath(path);
    }
  }

  response->InitChannelInfo(channel);

  nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
  // Propagate any tainting from the channel back to our response here.  This
  // step is not reflected in the spec because the spec is written such that
  // FetchEvent.respondWith() just passes the already-tainted Response back to
  // the outer fetch().  In gecko, however, we serialize the Response through
  // the channel and must regenerate the tainting from the channel in the
  // interception case.
  mRequest->MaybeIncreaseResponseTainting(loadInfo->GetTainting());

  // Resolves fetch() promise which may trigger code running in a worker.  Make
  // sure the Response is fully initialized before calling this.
  mResponse =
      BeginAndGetFilteredResponse(std::move(response), foundOpaqueRedirect);
  if (NS_WARN_IF(!mResponse)) {
    // Fail to generate a paddingInfo for opaque response.
    MOZ_DIAGNOSTIC_ASSERT(mRequest->GetResponseTainting() ==
                              LoadTainting::Opaque &&
                          !foundOpaqueRedirect);
    FailWithNetworkError(NS_ERROR_UNEXPECTED);
    return NS_ERROR_UNEXPECTED;
  }

  // From "Main Fetch" step 19: SRI-part1.
  if (ShouldCheckSRI(*mRequest, *mResponse) && mSRIMetadata.IsEmpty()) {
    nsIConsoleReportCollector* reporter = nullptr;
    if (mObserver) {
      reporter = mObserver->GetReporter();
    }

    nsAutoCString sourceUri;
    if (mDocument && mDocument->GetDocumentURI()) {
      mDocument->GetDocumentURI()->GetAsciiSpec(sourceUri);
    } else if (!mWorkerScript.IsEmpty()) {
      sourceUri.Assign(mWorkerScript);
    }
    SRICheck::IntegrityMetadata(mRequest->GetIntegrity(), sourceUri, reporter,
                                &mSRIMetadata);
    mSRIDataVerifier =
        std::make_unique<SRICheckDataVerifier>(mSRIMetadata, channel, reporter);

    // Do not retarget off main thread when using SRI API.
    return NS_OK;
  }

  // Only retarget if not already retargeted
  nsCOMPtr<nsISerialEventTarget> target;
  nsCOMPtr<nsIThreadRetargetableRequest> req = do_QueryInterface(aRequest);
  if (req) {
    rv = req->GetDeliveryTarget(getter_AddRefs(target));
    if (NS_SUCCEEDED(rv) && target && !target->IsOnCurrentThread()) {
      FETCH_LOG(
          ("FetchDriver::OnStartRequest this=%p, request=%p already retargeted",
           this, aRequest));
      return NS_OK;
    }
  }

  nsCOMPtr<nsIEventTarget> sts =
      do_GetService(NS_STREAMTRANSPORTSERVICE_CONTRACTID, &rv);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    FailWithNetworkError(rv);
    // Cancel request.
    return rv;
  }

  FETCH_LOG(("FetchDriver retargeting: request %p", aRequest));
  // Try to retarget off main thread.
  if (nsCOMPtr<nsIThreadRetargetableRequest> rr = do_QueryInterface(aRequest)) {
    RefPtr<TaskQueue> queue =
        TaskQueue::Create(sts.forget(), "FetchDriver STS Delivery Queue");
    (void)NS_WARN_IF(NS_FAILED(rr->RetargetDeliveryTo(queue)));
  }
  return NS_OK;
}

namespace {

// Runnable to call the observer OnDataAvailable on the main-thread.
class DataAvailableRunnable final : public Runnable {
  RefPtr<FetchDriverObserver> mObserver;

 public:
  explicit DataAvailableRunnable(FetchDriverObserver* aObserver)
      : Runnable("dom::DataAvailableRunnable"), mObserver(aObserver) {
    MOZ_ASSERT(aObserver);
  }

  NS_IMETHOD
  Run() override {
    RefPtr<FetchDriverObserver> observer = mObserver;
    observer->OnDataAvailable();
    mObserver = nullptr;
    return NS_OK;
  }
};

struct SRIVerifierAndOutputHolder {
  SRIVerifierAndOutputHolder(SRICheckDataVerifier* aVerifier,
                             nsIOutputStream* aOutputStream)
      : mVerifier(aVerifier), mOutputStream(aOutputStream) {}

  SRICheckDataVerifier* mVerifier;
  nsIOutputStream* mOutputStream;

  SRIVerifierAndOutputHolder() = delete;
};

// Just like NS_CopySegmentToStream, but also sends the data into an
// SRICheckDataVerifier.
nsresult CopySegmentToStreamAndSRI(nsIInputStream* aInStr, void* aClosure,
                                   const char* aBuffer, uint32_t aOffset,
                                   uint32_t aCount, uint32_t* aCountWritten) {
  auto holder = static_cast<SRIVerifierAndOutputHolder*>(aClosure);
  MOZ_DIAGNOSTIC_ASSERT(holder && holder->mVerifier && holder->mOutputStream,
                        "Bogus holder");
  nsresult rv = holder->mVerifier->Update(
      aCount, reinterpret_cast<const uint8_t*>(aBuffer));
  NS_ENSURE_SUCCESS(rv, rv);

  // The rest is just like NS_CopySegmentToStream.
  *aCountWritten = 0;
  while (aCount) {
    uint32_t n = 0;
    rv = holder->mOutputStream->Write(aBuffer, aCount, &n);
    if (NS_FAILED(rv)) {
      return rv;
    }
    aBuffer += n;
    aCount -= n;
    *aCountWritten += n;
  }
  return NS_OK;
}

}  // anonymous namespace

NS_IMETHODIMP
FetchDriver::OnDataAvailable(nsIRequest* aRequest, nsIInputStream* aInputStream,
                             uint64_t aOffset, uint32_t aCount) {
  // NB: This can be called on any thread!  But we're guaranteed that it is
  // called between OnStartRequest and OnStopRequest, so we don't need to worry
  // about races for the members accessed in OnStartRequest, OnStopRequest,
  // FailWithNetworkError and member functions accessed before opening the
  // channel. However, we have a possibility of a race from
  // FetchDriverAbortActions

  if (!mPipeOutputStream) {
    // We ignore the body for HEAD/CONNECT requests.
    // nsIStreamListener mandates reading from the stream before returning.
    uint32_t totalRead;
    nsresult rv = aInputStream->ReadSegments(NS_DiscardSegment, nullptr, aCount,
                                             &totalRead);
    NS_ENSURE_SUCCESS(rv, rv);
    return NS_OK;
  }

  if (mNeedToObserveOnDataAvailable) {
    mNeedToObserveOnDataAvailable = false;
    RefPtr<FetchDriverObserver> observer;
    {
      MutexAutoLock lock(mODAMutex);
      // Need to keep mObserver alive.
      observer = mObserver;
    }
    if (observer) {
      if (NS_IsMainThread()) {
        observer->OnDataAvailable();
      } else {
        RefPtr<Runnable> runnable = new DataAvailableRunnable(observer);
        nsresult rv = mMainThreadEventTarget->Dispatch(runnable.forget(),
                                                       NS_DISPATCH_NORMAL);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return rv;
        }
      }
    }
  }

  if (!mResponse) {
    MOZ_ASSERT(false);
    return NS_ERROR_UNEXPECTED;
  }

  // Needs to be initialized to 0 because in some cases nsStringInputStream may
  // not write to aRead.
  uint32_t aRead = 0;
  MOZ_ASSERT(mPipeOutputStream);

  // From "Main Fetch" step 19: SRI-part2.
  // Note: Avoid checking the hidden opaque body.
  nsresult rv;
  if (mResponse->Type() != ResponseType::Opaque &&
      ShouldCheckSRI(*mRequest, *mResponse)) {
    MOZ_ASSERT(mSRIDataVerifier);

    SRIVerifierAndOutputHolder holder(mSRIDataVerifier.get(),
                                      mPipeOutputStream);
    rv = aInputStream->ReadSegments(CopySegmentToStreamAndSRI, &holder, aCount,
                                    &aRead);
  } else {
    rv = aInputStream->ReadSegments(NS_CopySegmentToStream, mPipeOutputStream,
                                    aCount, &aRead);
  }

  // If no data was read, it's possible the output stream is closed but the
  // ReadSegments call followed its contract of returning NS_OK despite write
  // errors.  Unfortunately, nsIOutputStream has an ill-conceived contract when
  // taken together with ReadSegments' contract, because the pipe will just
  // NS_OK if we try and invoke its Write* functions ourselves with a 0 count.
  // So we must just assume the pipe is broken.
  if (aRead == 0 && aCount != 0) {
    return NS_BASE_STREAM_CLOSED;
  }
  return rv;
}

NS_IMETHODIMP
FetchDriver::OnStopRequest(nsIRequest* aRequest,
                           nsresult aStatusCode) MOZ_CAN_RUN_SCRIPT_BOUNDARY {
  FETCH_LOG(("FetchDriver::OnStopRequest this=%p, request=%p", this, aRequest));
  AssertIsOnMainThread();

  MOZ_DIAGNOSTIC_ASSERT(!mOnStopRequestCalled);
  mOnStopRequestCalled = true;

  // main data loading is going to finish, breaking the reference cycle.
  RefPtr<AlternativeDataStreamListener> altDataListener =
      std::move(mAltDataListener);

  // For PFetch and ServiceWorker navigationPreload, resource timing should be
  // reported before the body stream closing.
  if (mObserver) {
    mObserver->OnReportPerformanceTiming();
  }

  // We need to check mObserver, which is nulled by FailWithNetworkError(),
  // because in the case of "error" redirect mode, aStatusCode may be NS_OK but
  // mResponse will definitely be null so we must not take the else branch.
  if (NS_FAILED(aStatusCode) || !mObserver) {
    nsCOMPtr<nsIAsyncOutputStream> outputStream =
        do_QueryInterface(mPipeOutputStream);
    if (outputStream) {
      outputStream->CloseWithStatus(NS_FAILED(aStatusCode) ? aStatusCode
                                                           : NS_BINDING_FAILED);
    }
    if (altDataListener) {
      altDataListener->Cancel();
    }

    // We proceed as usual here, since we've already created a successful
    // response from OnStartRequest.
  } else {
    MOZ_ASSERT(mResponse);
    MOZ_ASSERT(!mResponse->IsError());

    // From "Main Fetch" step 19: SRI-part3.
    if (ShouldCheckSRI(*mRequest, *mResponse)) {
      MOZ_ASSERT(mSRIDataVerifier);

      nsCOMPtr<nsIChannel> channel = do_QueryInterface(aRequest);

      nsIConsoleReportCollector* reporter = nullptr;
      if (mObserver) {
        reporter = mObserver->GetReporter();
      }

      nsresult rv = mSRIDataVerifier->Verify(mSRIMetadata, channel, reporter);
      if (NS_FAILED(rv)) {
        if (altDataListener) {
          altDataListener->Cancel();
        }
        FailWithNetworkError(rv);
        // Cancel request.
        return rv;
      }
    }

    if (mPipeOutputStream) {
      mPipeOutputStream->Close();
    }
  }

  FinishOnStopRequest(altDataListener);
  return NS_OK;
}

void FetchDriver::FinishOnStopRequest(
    AlternativeDataStreamListener* aAltDataListener) {
  AssertIsOnMainThread();
  // OnStopRequest is not called from channel, that means the main data loading
  // does not finish yet. Reaching here since alternative data loading finishes.
  if (!mOnStopRequestCalled) {
    return;
  }

  MOZ_DIAGNOSTIC_ASSERT(!mAltDataListener);
  // Wait for alternative data loading finish if we needed it.
  if (aAltDataListener &&
      aAltDataListener->Status() == AlternativeDataStreamListener::LOADING) {
    // For LOADING case, channel holds the reference of altDataListener, no need
    // to restore it to mAltDataListener.
    return;
  }

  if (mObserver) {
    // From "Main Fetch" step 19.1, 19.2: Process response.
    if (ShouldCheckSRI(*mRequest, *mResponse)) {
      MOZ_ASSERT(mResponse);
      // Need to keep mObserver alive.
      RefPtr<FetchDriverObserver> observer = mObserver;
      observer->OnResponseAvailable(mResponse.clonePtr());
#ifdef DEBUG
      mResponseAvailableCalled = true;
#endif
    }
  }

  if (mObserver) {
    mObserver->OnResponseEnd(FetchDriverObserver::eByNetworking,
                             JS::UndefinedHandleValue);
    mObserver = nullptr;
  }

  mChannel = nullptr;
  Unfollow();
}

NS_IMETHODIMP
FetchDriver::ShouldPrepareForIntercept(nsIURI* aURI, nsIChannel* aChannel,
                                       bool* aShouldIntercept) {
  MOZ_ASSERT(aChannel);

  if (mInterceptController) {
    MOZ_ASSERT(XRE_IsParentProcess());
    return mInterceptController->ShouldPrepareForIntercept(aURI, aChannel,
                                                           aShouldIntercept);
  }

  nsCOMPtr<nsINetworkInterceptController> controller;
  NS_QueryNotificationCallbacks(nullptr, mLoadGroup,
                                NS_GET_IID(nsINetworkInterceptController),
                                getter_AddRefs(controller));
  if (controller) {
    return controller->ShouldPrepareForIntercept(aURI, aChannel,
                                                 aShouldIntercept);
  }

  *aShouldIntercept = false;
  return NS_OK;
}

NS_IMETHODIMP
FetchDriver::ChannelIntercepted(nsIInterceptedChannel* aChannel) {
  if (mInterceptController) {
    MOZ_ASSERT(XRE_IsParentProcess());
    return mInterceptController->ChannelIntercepted(aChannel);
  }

  nsCOMPtr<nsINetworkInterceptController> controller;
  NS_QueryNotificationCallbacks(nullptr, mLoadGroup,
                                NS_GET_IID(nsINetworkInterceptController),
                                getter_AddRefs(controller));
  if (controller) {
    return controller->ChannelIntercepted(aChannel);
  }

  return NS_OK;
}

void FetchDriver::EnableNetworkInterceptControl() {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mInterceptController);
  mInterceptController = new ServiceWorkerInterceptController();
}

NS_IMETHODIMP
FetchDriver::AsyncOnChannelRedirect(nsIChannel* aOldChannel,
                                    nsIChannel* aNewChannel, uint32_t aFlags,
                                    nsIAsyncVerifyRedirectCallback* aCallback) {
  nsCOMPtr<nsIHttpChannel> oldHttpChannel = do_QueryInterface(aOldChannel);
  nsCOMPtr<nsIHttpChannel> newHttpChannel = do_QueryInterface(aNewChannel);

  // Streaming uploads can only follow 303 redirects (which change method to
  // GET) All other redirects fail because the stream is non-rewindable
  if (mRequest->HasStreamBody() && oldHttpChannel) {
    uint32_t responseStatus = 0;
    nsresult rv = oldHttpChannel->GetResponseStatus(&responseStatus);
    if (NS_SUCCEEDED(rv) && responseStatus != 303) {
      // Non-303 redirect with streaming body: reject.
      FailWithNetworkError(NS_ERROR_DOM_NETWORK_ERR);
      aCallback->OnRedirectVerifyCallback(NS_ERROR_DOM_NETWORK_ERR);
      return NS_OK;
    }
  }

  if (oldHttpChannel && newHttpChannel) {
    nsAutoCString method;
    mRequest->GetMethod(method);

    // HTTP-redirect fetch, step 12: rewriting to GET drops the request body.
    bool rewriteToGET = false;
    (void)oldHttpChannel->ShouldStripRequestBodyHeader(method, &rewriteToGET);
    if (rewriteToGET) {
      mRequest->SetHasStreamBody(false);
    }

    // we need to strip Authentication headers for cross-origin requests
    // Ref: https://fetch.spec.whatwg.org/#http-redirect-fetch
    bool skipAuthHeader =
        NS_ShouldRemoveAuthHeaderOnRedirect(aOldChannel, aNewChannel, aFlags);

    SetRequestHeaders(newHttpChannel, rewriteToGET, skipAuthHeader);
  }

  // "HTTP-redirect fetch": step 14 "Append locationURL to request's URL list."
  // However, ignore internal redirects here.  We don't want to flip
  // Response.redirected to true if an internal redirect occurs.  These
  // should be transparent to script.
  nsCOMPtr<nsIURI> uri;
  MOZ_ALWAYS_SUCCEEDS(NS_GetFinalChannelURI(aNewChannel, getter_AddRefs(uri)));

  nsCOMPtr<nsIURI> uriClone;
  nsresult rv = NS_GetURIWithoutRef(uri, getter_AddRefs(uriClone));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  nsCString fragment;
  rv = uri->GetRef(fragment);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  if (!(aFlags & nsIChannelEventSink::REDIRECT_INTERNAL)) {
    mRequest->AddURL(WrapNotNull(uriClone.get()), fragment);
  } else {
    // Overwrite the URL only when the request is redirected by a service
    // worker.
    mRequest->SetURLForInternalRedirect(aFlags, WrapNotNull(uriClone.get()),
                                        fragment);
  }

  // In redirect, httpChannel already took referrer-policy into account, so
  // updates request’s associated referrer policy from channel.
  UpdateReferrerInfoFromNewChannel(aNewChannel);

  aCallback->OnRedirectVerifyCallback(NS_OK);
  return NS_OK;
}

NS_IMETHODIMP
FetchDriver::CheckListenerChain() { return NS_OK; }

NS_IMETHODIMP
FetchDriver::OnDataFinished(nsresult) { return NS_OK; }

NS_IMETHODIMP
FetchDriver::GetInterface(const nsIID& aIID, void** aResult) {
  if (aIID.Equals(NS_GET_IID(nsIChannelEventSink))) {
    *aResult = static_cast<nsIChannelEventSink*>(this);
    NS_ADDREF_THIS();
    return NS_OK;
  }
  if (aIID.Equals(NS_GET_IID(nsIStreamListener))) {
    *aResult = static_cast<nsIStreamListener*>(this);
    NS_ADDREF_THIS();
    return NS_OK;
  }
  if (aIID.Equals(NS_GET_IID(nsIRequestObserver))) {
    *aResult = static_cast<nsIRequestObserver*>(this);
    NS_ADDREF_THIS();
    return NS_OK;
  }

  return QueryInterface(aIID, aResult);
}

void FetchDriver::SetDocument(Document* aDocument) {
  // Cannot set document after Fetch() has been called.
  MOZ_ASSERT(!mFetchCalled);
  mDocument = aDocument;
}

void FetchDriver::SetCSPEventListener(nsICSPEventListener* aCSPEventListener) {
  MOZ_ASSERT(!mFetchCalled);
  mCSPEventListener = aCSPEventListener;
}

void FetchDriver::SetClientInfo(const ClientInfo& aClientInfo) {
  MOZ_ASSERT(!mFetchCalled);
  mClientInfo.emplace(aClientInfo);
}

void FetchDriver::SetController(
    const Maybe<ServiceWorkerDescriptor>& aController) {
  MOZ_ASSERT(!mFetchCalled);
  mController = aController;
}

UniquePtr<PerformanceTimingData> FetchDriver::GetPerformanceTimingData(
    nsAString& aInitiatorType, nsAString& aEntryName) {
  MOZ_ASSERT(XRE_IsParentProcess());
  if (!mChannel) {
    return nullptr;
  }

  nsCOMPtr<nsITimedChannel> timedChannel = do_QueryInterface(mChannel);
  if (!timedChannel) {
    return nullptr;
  }
  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(mChannel);
  if (!httpChannel) {
    return nullptr;
  }
  return dom::PerformanceTimingData::Create(timedChannel, httpChannel, 0,
                                            aInitiatorType, aEntryName);
}

void FetchDriver::SetRequestHeaders(nsIHttpChannel* aChannel,
                                    bool aStripRequestBodyHeader,
                                    bool aStripAuthHeader) const {
  MOZ_ASSERT(aChannel);

  // nsIHttpChannel has a set of pre-configured headers (Accept,
  // Accept-Languages, ...) and we don't want to merge the Request's headers
  // with them. This array is used to know if the current header has been aleady
  // set, if yes, we ask necko to merge it with the previous one, otherwise, we
  // don't want the merge.
  nsTArray<nsCString> headersSet;

  AutoTArray<InternalHeaders::Entry, 5> headers;
  mRequest->Headers()->GetEntries(headers);
  for (uint32_t i = 0; i < headers.Length(); ++i) {
    if (aStripRequestBodyHeader &&
        (headers[i].mName.LowerCaseEqualsASCII("content-type") ||
         headers[i].mName.LowerCaseEqualsASCII("content-encoding") ||
         headers[i].mName.LowerCaseEqualsASCII("content-language") ||
         headers[i].mName.LowerCaseEqualsASCII("content-location"))) {
      continue;
    }

    if (aStripAuthHeader &&
        headers[i].mName.LowerCaseEqualsASCII("authorization")) {
      continue;
    }

    bool alreadySet = headersSet.Contains(headers[i].mName);
    if (!alreadySet) {
      headersSet.AppendElement(headers[i].mName);
    }

    if (headers[i].mValue.IsEmpty()) {
      DebugOnly<nsresult> rv =
          aChannel->SetEmptyRequestHeader(headers[i].mName);
      MOZ_ASSERT(NS_SUCCEEDED(rv));
    } else {
      DebugOnly<nsresult> rv = aChannel->SetRequestHeader(
          headers[i].mName, headers[i].mValue, alreadySet /* merge */);
      MOZ_ASSERT(NS_SUCCEEDED(rv));
    }
  }
}

void FetchDriver::RunAbortAlgorithm() { FetchDriverAbortActions(Signal()); }

void FetchDriver::FetchDriverAbortActions(AbortSignalImpl* aSignalImpl) {
  MOZ_DIAGNOSTIC_ASSERT(NS_IsMainThread());
  RefPtr<FetchDriverObserver> observer;
  {
    MutexAutoLock lock(mODAMutex);
    observer = std::move(mObserver);
  }

  if (observer) {
#ifdef DEBUG
    mResponseAvailableCalled = true;
#endif
    JS::Rooted<JS::Value> reason(RootingCx());
    if (aSignalImpl) {
      reason.set(aSignalImpl->RawReason());
    }
    observer->OnResponseEnd(FetchDriverObserver::eAborted, reason);
  }

  if (mChannel) {
    mChannel->CancelWithReason(NS_BINDING_ABORTED,
                               "FetchDriver::RunAbortAlgorithm"_ns);
    mChannel = nullptr;
  }

  mAborted = true;
}

}  // namespace mozilla::dom
