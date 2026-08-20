/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "mozilla/net/HttpChannelChild.h"

#include <functional>

#include "AltDataOutputStreamChild.h"
#include "CookieServiceChild.h"
#include "HttpBackgroundChannelChild.h"
#include "HttpLog.h"
#include "NetworkMarker.h"
#include "SerializedLoadContext.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/PerfStats.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/StoragePrincipalHelper.h"
#include "mozilla/dom/BrowserChild.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/DocGroup.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/LinkStyle.h"
#include "mozilla/dom/PerformanceStorage.h"
#include "mozilla/dom/ReferrerInfo.h"
#include "mozilla/dom/ServiceWorkerUtils.h"
#include "mozilla/extensions/StreamFilterParent.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "mozilla/glean/NetwerkProtocolHttpMetrics.h"
#include "mozilla/ipc/BackgroundUtils.h"
#include "mozilla/ipc/IPCStreamUtils.h"
#include "mozilla/ipc/InputStreamUtils.h"
#include "mozilla/ipc/URIUtils.h"
#include "mozilla/net/CacheEntryWriteHandleChild.h"
#include "mozilla/net/ChannelClassifierUtils.h"
#include "mozilla/net/DNS.h"
#include "mozilla/net/NeckoChild.h"
#include "mozilla/net/PBackgroundDataBridge.h"
#include "mozilla/net/SocketProcessBridgeChild.h"
#include "nsCOMPtr.h"
#include "nsCORSListenerProxy.h"
#include "nsContentPolicyUtils.h"
#include "nsContentSecurityManager.h"
#include "nsDOMNavigationTiming.h"
#include "nsError.h"
#include "nsHttp.h"
#include "nsHttpChannel.h"
#include "nsHttpHandler.h"
#include "nsICacheEntry.h"
#include "nsICompressConvStats.h"
#include "nsIOService.h"
#include "nsIScriptError.h"
#include "nsISerialEventTarget.h"
#include "nsISocketTransport.h"
#include "nsIStreamTransportService.h"
#include "nsIThreadRetargetableStreamListener.h"
#include "nsInputStreamPump.h"
#include "nsNetUtil.h"
#include "nsQueryObject.h"
#include "nsRedirectHistoryEntry.h"
#include "nsSerializationHelper.h"
#include "nsSocketTransportService2.h"
#include "nsStreamUtils.h"
#include "nsStringStream.h"
#include "nsThreadUtils.h"

using namespace mozilla::dom;
using namespace mozilla::ipc;

namespace mozilla::net {

//-----------------------------------------------------------------------------
// HttpChannelChild
//-----------------------------------------------------------------------------

HttpChannelChild::HttpChannelChild()
    : HttpAsyncAborter<HttpChannelChild>(this),
      NeckoTargetHolder(nullptr),
      mCacheEntryAvailable(false),
      mAltDataCacheEntryAvailable(false),
      mSendResumeAt(false),
      mKeptAlive(false),
      mIPCActorDeleted(false),
      mSuspendSent(false),
      mIsFirstPartOfMultiPart(false),
      mIsLastPartOfMultiPart(false),
      mSuspendForWaitCompleteRedirectSetup(false),
      mRecvOnStartRequestSentCalled(false),
      mSuspendedByWaitingForCookies(false),
      mAlreadyReleased(false) {
  LOG(("Creating HttpChannelChild @%p\n", this));

  mChannelCreationTime = PR_Now();
  mChannelCreationTimestamp = TimeStamp::Now();
  mLastStatusReported =
      mChannelCreationTimestamp;  // in case we enable the profiler after Init()
  mAsyncOpenTime = TimeStamp::Now();
  mEventQ = new ChannelEventQueue(static_cast<nsIHttpChannel*>(this));

  // Ensure that the cookie service is initialized before the first
  // IPC HTTP channel is created.
  // We require that the parent cookie service actor exists while
  // processing HTTP responses.
  RefPtr<CookieServiceChild> cookieService = CookieServiceChild::GetSingleton();
}

HttpChannelChild::~HttpChannelChild() {
  LOG(("Destroying HttpChannelChild @%p\n", this));

  // See HttpChannelChild::Release, HttpChannelChild should be always destroyed
  // on the main thread.
  MOZ_RELEASE_ASSERT(NS_IsMainThread());

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  if (mDoDiagnosticAssertWhenOnStopNotCalledOnDestroy && mAsyncOpenSucceeded &&
      !mSuccesfullyRedirected && !LoadOnStopRequestCalled()) {
    bool emptyBgChildQueue, nullBgChild;
    {
      MutexAutoLock lock(mBgChildMutex);
      nullBgChild = !mBgChild;
      emptyBgChildQueue = !nullBgChild && mBgChild->IsQueueEmpty();
    }

    uint32_t flags =
        (mRedirectChannelChild ? 1 << 0 : 0) |
        (mEventQ->IsEmpty() ? 1 << 1 : 0) | (nullBgChild ? 1 << 2 : 0) |
        (emptyBgChildQueue ? 1 << 3 : 0) |
        (LoadOnStartRequestCalled() ? 1 << 4 : 0) |
        (mBackgroundChildQueueFinalState == BCKCHILD_EMPTY ? 1 << 5 : 0) |
        (mBackgroundChildQueueFinalState == BCKCHILD_NON_EMPTY ? 1 << 6 : 0) |
        (mRemoteChannelExistedAtCancel ? 1 << 7 : 0) |
        (mEverHadBgChildAtAsyncOpen ? 1 << 8 : 0) |
        (mEverHadBgChildAtConnectParent ? 1 << 9 : 0) |
        (mCreateBackgroundChannelFailed ? 1 << 10 : 0) |
        (mBgInitFailCallbackTriggered ? 1 << 11 : 0) |
        (mCanSendAtCancel ? 1 << 12 : 0) | (!!mSuspendCount ? 1 << 13 : 0) |
        (!!mCallOnResume ? 1 << 14 : 0);
    MOZ_CRASH_UNSAFE_PRINTF(
        "~HttpChannelChild, LoadOnStopRequestCalled()=false, mStatus=0x%08x, "
        "mActorDestroyReason=%d, 20200717 flags=%u",
        static_cast<uint32_t>(nsresult(mStatus)),
        static_cast<int32_t>(mActorDestroyReason ? *mActorDestroyReason : -1),
        flags);
  }
#endif

  mEventQ->NotifyReleasingOwner();

  ReleaseMainThreadOnlyReferences();
}

void HttpChannelChild::ReleaseMainThreadOnlyReferences() {
  if (NS_IsMainThread()) {
    // Already on main thread, let dtor to
    // take care of releasing references
    return;
  }

  NS_ReleaseOnMainThread("HttpChannelChild::mRedirectChannelChild",
                         mRedirectChannelChild.forget());
}
//-----------------------------------------------------------------------------
// HttpChannelChild::nsISupports
//-----------------------------------------------------------------------------

NS_IMPL_ADDREF(HttpChannelChild)

NS_IMETHODIMP_(MozExternalRefCountType) HttpChannelChild::Release() {
  // Fast path for off-main-thread releases: decrement atomically when count >
  // 2, staying clear of the mKeptAlive (count==1) and revival (count==0) paths
  // which access non-atomic state and must run on the main thread.
  if (!NS_IsMainThread()) {
    auto [ok, count] = mRefCnt.DecrementWithLimit<2>();
    if (ok) {
      NS_LOG_RELEASE(this, count, "HttpChannelChild");
      return count;
    }
    // count <= 2: proxy to main thread for mKeptAlive/revival handling.
    nsresult rv = NS_DispatchToMainThread(NewNonOwningRunnableMethod(
        "HttpChannelChild::Release", this, &HttpChannelChild::Release));
    if (NS_SUCCEEDED(rv)) {
      return count;
    }
    // Dispatch failed (event loop is shutting down). Crash rather than
    // run main-thread-only logic off-thread.
    MOZ_CRASH("Failed to dispatch Release to main thread");
    return count;
  }

  nsrefcnt count = --mRefCnt;
  MOZ_ASSERT(int32_t(count) >= 0, "dup release");

  // Normally we Send_delete in OnStopRequest, but when we need to retain the
  // remote channel for security info IPDL itself holds 1 reference, so we
  // Send_delete when refCnt==1.  But if !CanSend(), then there's nobody to send
  // to, so we fall through.
  if (mKeptAlive && count == 1 && CanSend()) {
    NS_LOG_RELEASE(this, 1, "HttpChannelChild");
    mKeptAlive = false;
    // We send a message to the parent, which calls SendDelete, and then the
    // child calling Send__delete__() to finally drop the refcount to 0.
    TrySendDeletingChannel();
    return 1;
  }

  if (count == 0) {
    mRefCnt = 1; /* stabilize */

    // We don't have a listener when AsyncOpen has failed or when this channel
    // has been sucessfully redirected.
    if (MOZ_LIKELY(LoadOnStartRequestCalled() && LoadOnStopRequestCalled()) ||
        !mListener || mAlreadyReleased) {
      NS_LOG_RELEASE(this, 0, "HttpChannelChild");
      delete this;
      return 0;
    }

    // This ensures that when the refcount goes to 0 again, we don't dispatch
    // yet another runnable and get in a loop.
    mAlreadyReleased = true;

    // This makes sure we fulfill the stream listener contract all the time.
    if (NS_SUCCEEDED(mStatus)) {
      mStatus = NS_ERROR_ABORT;
    }

    // Turn the stabilization refcount into a regular strong reference.

    // 1) We tell refcount logging about the "stabilization" AddRef, which
    // will become the reference for |channel|. We do this first so that we
    // don't tell refcount logging that the refcount has dropped to zero, which
    // it will interpret as destroying the object.
    NS_LOG_ADDREF(this, 2, "HttpChannelChild", sizeof(*this));

    // 2) We tell refcount logging about the original call to Release().
    NS_LOG_RELEASE(this, 1, "HttpChannelChild");

    // 3) Finally, we turn the reference into a regular smart pointer.
    RefPtr<HttpChannelChild> channel = dont_AddRef(this);
    MOZ_ASSERT(mRefCnt == 1);
    NS_DispatchToCurrentThread(NS_NewRunnableFunction(
        "~HttpChannelChild>DoNotifyListener",
        [chan = std::move(channel)] { chan->DoNotifyListener(false); }));
    // If NS_DispatchToCurrentThread failed then we're going to leak the
    // runnable, and thus the channel, so there's no need to do anything else.
    // this might be released at this point, so we can't access mRefCnt here.
    return 1;
  }

  NS_LOG_RELEASE(this, count, "HttpChannelChild");
  return count;
}

NS_INTERFACE_MAP_BEGIN(HttpChannelChild)
  NS_INTERFACE_MAP_ENTRY(nsIRequest)
  NS_INTERFACE_MAP_ENTRY(nsIChannel)
  NS_INTERFACE_MAP_ENTRY(nsIHttpChannel)
  NS_INTERFACE_MAP_ENTRY(nsIHttpChannelInternal)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsICacheInfoChannel,
                                     !mMultiPartID.isSome())
  NS_INTERFACE_MAP_ENTRY(nsIResumableChannel)
  NS_INTERFACE_MAP_ENTRY(nsISupportsPriority)
  NS_INTERFACE_MAP_ENTRY(nsIClassOfService)
  NS_INTERFACE_MAP_ENTRY(nsIProxiedChannel)
  NS_INTERFACE_MAP_ENTRY(nsITraceableChannel)
  NS_INTERFACE_MAP_ENTRY(nsIAsyncVerifyRedirectCallback)
  NS_INTERFACE_MAP_ENTRY(nsIChildChannel)
  NS_INTERFACE_MAP_ENTRY(nsIHttpChannelChild)
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIMultiPartChannel, mMultiPartID.isSome())
  NS_INTERFACE_MAP_ENTRY_CONDITIONAL(nsIThreadRetargetableRequest,
                                     !mMultiPartID.isSome())
  NS_INTERFACE_MAP_ENTRY_CONCRETE(HttpChannelChild)
NS_INTERFACE_MAP_END_INHERITING(HttpBaseChannel)

//-----------------------------------------------------------------------------
// HttpChannelChild::PHttpChannelChild
//-----------------------------------------------------------------------------

void HttpChannelChild::OnBackgroundChildReady(
    HttpBackgroundChannelChild* aBgChild) {
  LOG(("HttpChannelChild::OnBackgroundChildReady [this=%p, bgChild=%p]\n", this,
       aBgChild));
  MOZ_ASSERT(OnSocketThread());

  {
    MutexAutoLock lock(mBgChildMutex);

    // mBgChild might be removed or replaced while the original background
    // channel is inited on STS thread.
    if (mBgChild != aBgChild) {
      return;
    }

    MOZ_ASSERT(mBgInitFailCallback);
    mBgInitFailCallback = nullptr;
  }
}

void HttpChannelChild::OnBackgroundChildDestroyed(
    HttpBackgroundChannelChild* aBgChild) {
  LOG(("HttpChannelChild::OnBackgroundChildDestroyed [this=%p]\n", this));
  // This function might be called during shutdown phase, so OnSocketThread()
  // might return false even on STS thread. Use IsOnCurrentThreadInfallible()
  // to get correct information.
  MOZ_ASSERT(gSocketTransportService);
  MOZ_ASSERT(gSocketTransportService->IsOnCurrentThreadInfallible());

  nsCOMPtr<nsIRunnable> callback;
  {
    MutexAutoLock lock(mBgChildMutex);

    // mBgChild might be removed or replaced while the original background
    // channel is destroyed on STS thread.
    if (aBgChild != mBgChild) {
      return;
    }

    mBgChild = nullptr;
    callback = std::move(mBgInitFailCallback);
  }

  if (callback) {
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
    mBgInitFailCallbackTriggered = true;
#endif
    nsCOMPtr<nsISerialEventTarget> neckoTarget = GetNeckoTarget();
    neckoTarget->Dispatch(callback, NS_DISPATCH_NORMAL);
  }
}

mozilla::ipc::IPCResult HttpChannelChild::RecvOnStartRequestSent() {
  LOG(("HttpChannelChild::RecvOnStartRequestSent [this=%p]\n", this));
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mRecvOnStartRequestSentCalled);

  mRecvOnStartRequestSentCalled = true;

  if (mSuspendedByWaitingForCookies) {
    mSuspendedByWaitingForCookies = false;
    mEventQ->Resume();
  }
  return IPC_OK();
}

void HttpChannelChild::ProcessOnStartRequest(
    const nsHttpResponseHead& aResponseHead, const bool& aUseResponseHead,
    const nsHttpHeaderArray& aRequestHeaders,
    const HttpChannelOnStartRequestArgs& aArgs,
    const HttpChannelAltDataStream& aAltData,
    const TimeStamp& aOnStartRequestStartTime) {
  LOG(("HttpChannelChild::ProcessOnStartRequest [this=%p]\n", this));
  MOZ_ASSERT(OnSocketThread());

  mAltDataInputStream = DeserializeIPCStream(aAltData.altDataInputStream());

  mEventQ->RunOrEnqueue(MakeUnique<NeckoTargetChannelFunctionEvent>(
      this, [self = UnsafePtr<HttpChannelChild>(this), aResponseHead,
             aUseResponseHead, aRequestHeaders, aArgs]() {
        self->OnStartRequest(aResponseHead, aUseResponseHead, aRequestHeaders,
                             aArgs);
      }));
}

static void ResourceTimingStructArgsToTimingsStruct(
    const ResourceTimingStructArgs& aArgs, TimingStruct& aTimings) {
  aTimings.domainLookupStart = aArgs.domainLookupStart();
  aTimings.domainLookupEnd = aArgs.domainLookupEnd();
  aTimings.connectStart = aArgs.connectStart();
  aTimings.tcpConnectEnd = aArgs.tcpConnectEnd();
  aTimings.secureConnectionStart = aArgs.secureConnectionStart();
  aTimings.connectEnd = aArgs.connectEnd();
  aTimings.requestStart = aArgs.requestStart();
  aTimings.responseStart = aArgs.responseStart();
  aTimings.firstInterimResponseStart = aArgs.firstInterimResponseStart();
  aTimings.finalResponseHeadersStart = aArgs.finalResponseHeadersStart();
  aTimings.responseEnd = aArgs.responseEnd();
  aTimings.transactionPending = aArgs.transactionPending();
}

void HttpChannelChild::OnStartRequest(
    const nsHttpResponseHead& aResponseHead, const bool& aUseResponseHead,
    const nsHttpHeaderArray& aRequestHeaders,
    const HttpChannelOnStartRequestArgs& aArgs) {
  LOG(("HttpChannelChild::OnStartRequest [this=%p]\n", this));

  // If this channel was aborted by ActorDestroy, then there may be other
  // OnStartRequest/OnStopRequest/OnDataAvailable IPC messages that need to
  // be handled. In that case we just ignore them to avoid calling the listener
  // twice.
  if (LoadOnStartRequestCalled() && mIPCActorDeleted) {
    return;
  }

  // Copy arguments only. It's possible to handle other IPC between
  // OnStartRequest and DoOnStartRequest.
  mComputedCrossOriginOpenerPolicy = aArgs.openerPolicy();

  if (!mCanceled && NS_SUCCEEDED(mStatus)) {
    mStatus = aArgs.channelStatus();
  }

  // Cookies headers should not be visible to the child process
  MOZ_ASSERT(!aRequestHeaders.HasHeader(nsHttp::Cookie));
  MOZ_ASSERT(!nsHttpResponseHead(aResponseHead).HasHeader(nsHttp::Set_Cookie));

  if (aUseResponseHead && !mCanceled) {
    mResponseHead = MakeUnique<nsHttpResponseHead>(aResponseHead);
  }

  mSecurityInfo = aArgs.securityInfo();

  ipc::MergeParentLoadInfoForwarder(aArgs.loadInfoForwarder(), mLoadInfo);

  mIsFromCache = aArgs.isFromCache();
  mCacheEntryAvailable = aArgs.cacheEntryAvailable();
  mCacheEntryId = aArgs.cacheEntryId();
  mCacheDisposition = aArgs.cacheDisposition();
  mCacheFetchCount = aArgs.cacheFetchCount();
  mProtocolVersion = aArgs.protocolVersion();
  mCacheExpirationTime = aArgs.cacheExpirationTime();
  mSelfAddr = aArgs.selfAddr();
  mPeerAddr = aArgs.peerAddr();

  mRedirectCount = aArgs.redirectCount();
  mAvailableCachedAltDataType = aArgs.altDataType();
  StoreDeliveringAltData(aArgs.deliveringAltData());
  mAltDataLength = aArgs.altDataLength();
  StoreResolvedByTRR(aArgs.isResolvedByTRR());
  mEffectiveTRRMode = aArgs.effectiveTRRMode();
  mTRRSkipReason = aArgs.trrSkipReason();

  SetApplyConversion(aArgs.applyConversion());

  StoreAfterOnStartRequestBegun(true);
  StoreHasHTTPSRR(aArgs.hasHTTPSRR());

  AutoEventEnqueuer ensureSerialDispatch(mEventQ);

  mCacheKey = aArgs.cacheKey();

  StoreIsProxyUsed(aArgs.isProxyUsed());

  // replace our request headers with what actually got sent in the parent
  mRequestHead.SetHeaders(aRequestHeaders);

  // Note: this is where we would notify "http-on-examine-response" observers.
  // We have deliberately disabled this for child processes (see bug 806753)
  //
  // gHttpHandler->OnExamineResponse(this);

  ResourceTimingStructArgsToTimingsStruct(aArgs.timing(), mTransactionTimings);

  if (!mAsyncOpenTime.IsNull() &&
      !aArgs.timing().transactionPending().IsNull()) {
    PerfStats::RecordMeasurement(
        PerfStats::Metric::HttpChannelAsyncOpenToTransactionPending,
        aArgs.timing().transactionPending() - mAsyncOpenTime);
  }

  const TimeStamp now = TimeStamp::Now();
  if (!mOnStartRequestStartTime.IsNull()) {
    PerfStats::RecordMeasurement(PerfStats::Metric::OnStartRequestToContent,
                                 now - mOnStartRequestStartTime);
  }

  StoreAllRedirectsSameOrigin(aArgs.allRedirectsSameOrigin());
  StoreAllRedirectsSameOriginIgnoringInternal(
      aArgs.allRedirectsSameOriginIgnoringInternal());

  mMultiPartID = aArgs.multiPartID();
  mIsFirstPartOfMultiPart = aArgs.isFirstPartOfMultiPart();
  mIsLastPartOfMultiPart = aArgs.isLastPartOfMultiPart();

  if (aArgs.overrideReferrerInfo()) {
    // The arguments passed to SetReferrerInfoInternal here should mirror the
    // arguments passed in
    // nsHttpChannel::ReEvaluateReferrerAfterTrackingStatusIsKnown(), except for
    // aRespectBeforeConnect which we pass false here since we're intentionally
    // overriding the referrer after BeginConnect().
    (void)SetReferrerInfoInternal(aArgs.overrideReferrerInfo(), false, true,
                                  false);
  }

  RefPtr<CookieServiceChild> cookieService = CookieServiceChild::GetSingleton();

  for (const CookieChange& cookieChange : aArgs.cookieChanges()) {
    if (cookieChange.added()) {
      (void)cookieService->RecvAddCookie(
          cookieChange.cookie(), cookieChange.originAttributes(), Nothing());
    } else {
      (void)cookieService->RecvRemoveCookie(
          cookieChange.cookie(), cookieChange.originAttributes(), Nothing());
    }
  }

  // Note: this is where we would notify "http-on-after-examine-response"
  // observers.  We have deliberately disabled this for child processes (see bug
  // 806753)
  //
  // gHttpHandler->OnAfterExamineResponse(this);

  if (aArgs.shouldWaitForOnStartRequestSent() &&
      !mRecvOnStartRequestSentCalled) {
    LOG(("  > pending DoOnStartRequest until RecvOnStartRequestSent\n"));
    MOZ_ASSERT(NS_IsMainThread());

    mEventQ->Suspend();
    mSuspendedByWaitingForCookies = true;
    mEventQ->PrependEvent(MakeUnique<NeckoTargetChannelFunctionEvent>(
        this, [self = UnsafePtr<HttpChannelChild>(this)]() {
          self->DoOnStartRequest(self);
        }));
    return;
  }

  // Remember whether HTTP3 is supported
  if (mResponseHead) {
    mSupportsHTTP3 =
        nsHttpHandler::IsHttp3SupportedByServer(mResponseHead.get());
  }

  DoOnStartRequest(this);
}

void HttpChannelChild::ProcessOnAfterLastPart(const nsresult& aStatus) {
  LOG(("HttpChannelChild::ProcessOnAfterLastPart [this=%p]\n", this));
  MOZ_ASSERT(OnSocketThread());
  mEventQ->RunOrEnqueue(MakeUnique<NeckoTargetChannelFunctionEvent>(
      this, [self = UnsafePtr<HttpChannelChild>(this), aStatus]() {
        self->OnAfterLastPart(aStatus);
      }));
}

void HttpChannelChild::OnAfterLastPart(const nsresult& aStatus) {
  if (LoadOnStopRequestCalled()) {
    return;
  }
  StoreOnStopRequestCalled(true);

  // notify "http-on-stop-connect" observers
  gHttpHandler->OnStopRequest(this);

  ReleaseListeners();

  // If a preferred alt-data type was set, the parent would hold a reference to
  // the cache entry in case the child calls openAlternativeOutputStream().
  // (see nsHttpChannel::OnStopRequest)
  if (!mPreferredCachedAltDataTypes.IsEmpty()) {
    mAltDataCacheEntryAvailable = mCacheEntryAvailable;
  }
  mCacheEntryAvailable = false;

  if (mLoadGroup) mLoadGroup->RemoveRequest(this, nullptr, mStatus);
  CleanupBackgroundChannel();

  if (mLoadFlags & LOAD_DOCUMENT_URI) {
    // Keep IPDL channel open, but only for updating security info.
    // If IPDL is already closed, then do nothing.
    if (CanSend()) {
      mKeptAlive = true;
      SendDocumentChannelCleanup(true);
    }
  } else {
    // The parent process will respond by sending a DeleteSelf message and
    // making sure not to send any more messages after that.
    TrySendDeletingChannel();
  }
}

void HttpChannelChild::DoOnStartRequest(nsIRequest* aRequest) {
  nsresult rv;

  LOG(("HttpChannelChild::DoOnStartRequest [this=%p, request=%p]\n", this,
       aRequest));

  // We handle all the listener chaining before OnStartRequest at this moment.
  // Prevent additional listeners being added to the chain after the request
  // as started.
  StoreTracingEnabled(false);

  // mListener could be null if the redirect setup is not completed.
  MOZ_ASSERT(mListener || LoadOnStartRequestCalled());
  if (!mListener) {
    Cancel(NS_ERROR_FAILURE);
    return;
  }

  if (mListener) {
    nsCOMPtr<nsIStreamListener> listener(mListener);
    StoreOnStartRequestCalled(true);
    rv = listener->OnStartRequest(aRequest);
  } else {
    rv = NS_ERROR_UNEXPECTED;
  }
  StoreOnStartRequestCalled(true);

  if (NS_FAILED(rv)) {
    CancelWithReason(rv, "HttpChannelChild listener->OnStartRequest failed"_ns);
    return;
  }

  nsCOMPtr<nsIStreamListener> listener;
  rv = DoApplyContentConversions(mListener, getter_AddRefs(listener), nullptr);
  if (NS_FAILED(rv)) {
    CancelWithReason(rv,
                     "HttpChannelChild DoApplyContentConversions failed"_ns);
  } else if (listener) {
    mListener = listener;
    mCompressListener = listener;

    // We call MaybeRetarget here to allow the stream converter
    // the option to request data on another thread, even if the
    // final listener might not support it
    if (nsCOMPtr<nsIStreamConverter> conv =
            do_QueryInterface((mCompressListener))) {
      conv->MaybeRetarget(this);
    }
  }

#if defined(EARLY_BETA_OR_EARLIER) || defined(DEBUG)
  if (nsCOMPtr<nsIThreadRetargetableRequest> req =
          do_QueryInterface(aRequest)) {
    nsCOMPtr<nsISerialEventTarget> target;
    rv = req->GetDeliveryTarget(getter_AddRefs(target));
    if (NS_SUCCEEDED(rv) && target && !target->IsOnCurrentThread()) {
      if (nsCOMPtr<nsIThreadRetargetableStreamListener> retargetableListener =
              do_QueryInterface(mListener)) {
        MOZ_DIAGNOSTIC_ASSERT(
            NS_SUCCEEDED(retargetableListener->CheckListenerChain()));
      } else {
        MOZ_DIAGNOSTIC_ASSERT(false, "Unexpected listener is not retargetable");
      }
    }
  }
#endif
}

void HttpChannelChild::ProcessOnTransportAndData(
    const nsresult& aChannelStatus, const nsresult& aTransportStatus,
    const uint64_t& aOffset, const nsACString& aData,
    const TimeStamp& aOnDataAvailableStartTime) {
  LOG(("HttpChannelChild::ProcessOnTransportAndData [this=%p]\n", this));
  MOZ_ASSERT(OnSocketThread());
  mEventQ->RunOrEnqueue(MakeUnique<ChannelFunctionEvent>(
      [self = UnsafePtr<HttpChannelChild>(this)]() {
        return self->GetODATarget();
      },
      [self = UnsafePtr<HttpChannelChild>(this), aChannelStatus,
       aTransportStatus, aOffset, aData = nsCString(aData),
       aOnDataAvailableStartTime]() {
        self->mOnDataAvailableStartTime = aOnDataAvailableStartTime;
        self->OnTransportAndData(aChannelStatus, aTransportStatus, aOffset,
                                 aData);
      }));
}

void HttpChannelChild::OnTransportAndData(const nsresult& aChannelStatus,
                                          const nsresult& aTransportStatus,
                                          const uint64_t& aOffset,
                                          const nsACString& aData) {
  LOG(("HttpChannelChild::OnTransportAndData [this=%p]\n", this));

  if (!mCanceled && NS_SUCCEEDED(mStatus)) {
    mStatus = aChannelStatus;
  }

  if (mCanceled || NS_FAILED(mStatus)) {
    return;
  }

  if (!mOnDataAvailableStartTime.IsNull()) {
    PerfStats::RecordMeasurement(PerfStats::Metric::OnDataAvailableToContent,
                                 TimeStamp::Now() - mOnDataAvailableStartTime);
  }

  // Hold queue lock throughout all three calls, else we might process a later
  // necko msg in between them.
  AutoEventEnqueuer ensureSerialDispatch(mEventQ);

  int64_t progressMax;
  if (NS_FAILED(GetContentLength(&progressMax))) {
    progressMax = -1;
  }

  const int64_t progress = aOffset + aData.Length();

  // OnTransportAndData will be run on retargeted thread if applicable, however
  // OnStatus/OnProgress event can only be fired on main thread. We need to
  // dispatch the status/progress event handling back to main thread with the
  // appropriate event target for networking.
  if (NS_IsMainThread()) {
    DoOnStatus(this, aTransportStatus);
    DoOnProgress(this, progress, progressMax);
  } else {
    RefPtr<HttpChannelChild> self = this;
    nsCOMPtr<nsISerialEventTarget> neckoTarget = GetNeckoTarget();
    MOZ_ASSERT(neckoTarget);

    DebugOnly<nsresult> rv = neckoTarget->Dispatch(
        NS_NewRunnableFunction(
            "net::HttpChannelChild::OnTransportAndData",
            [self, aTransportStatus, progress, progressMax]() {
              self->DoOnStatus(self, aTransportStatus);
              self->DoOnProgress(self, progress, progressMax);
            }),
        NS_DISPATCH_NORMAL);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
  }

  // OnDataAvailable
  //
  // NOTE: the OnDataAvailable contract requires the client to read all the data
  // in the inputstream.  This code relies on that ('data' will go away after
  // this function).  Apparently the previous, non-e10s behavior was to actually
  // support only reading part of the data, allowing later calls to read the
  // rest.
  nsCOMPtr<nsIInputStream> stringStream;
  nsresult rv = NS_NewByteInputStream(getter_AddRefs(stringStream), Span(aData),
                                      NS_ASSIGNMENT_DEPEND);
  if (NS_FAILED(rv)) {
    CancelWithReason(rv, "HttpChannelChild NS_NewByteInputStream failed"_ns);
    return;
  }

  DoOnDataAvailable(this, stringStream, aOffset, aData.Length());
  stringStream->Close();

  // TODO: Bug 1523916 backpressure needs to take into account if the data is
  // coming from the main process or from the socket process via PBackground.
  if (NeedToReportBytesRead()) {
    mUnreportBytesRead += aData.Length();
    if (mUnreportBytesRead >= gHttpHandler->SendWindowSize() >> 2) {
      if (NS_IsMainThread()) {
        (void)SendBytesRead(mUnreportBytesRead);
      } else {
        // PHttpChannel connects to the main thread
        RefPtr<HttpChannelChild> self = this;
        int32_t bytesRead = mUnreportBytesRead;
        nsCOMPtr<nsISerialEventTarget> neckoTarget = GetNeckoTarget();
        MOZ_ASSERT(neckoTarget);

        DebugOnly<nsresult> rv = neckoTarget->Dispatch(
            NS_NewRunnableFunction(
                "net::HttpChannelChild::SendBytesRead",
                [self, bytesRead]() { (void)self->SendBytesRead(bytesRead); }),
            NS_DISPATCH_NORMAL);
        MOZ_ASSERT(NS_SUCCEEDED(rv));
      }
      mUnreportBytesRead = 0;
    }
  }
}

bool HttpChannelChild::NeedToReportBytesRead() {
  if (mCacheNeedToReportBytesReadInitialized) {
    return mNeedToReportBytesRead;
  }

  // Might notify parent for partial cache, and the IPC message is ignored by
  // parent.
  int64_t contentLength = -1;
  if (gHttpHandler->SendWindowSize() == 0 || mIsFromCache ||
      NS_FAILED(GetContentLength(&contentLength)) ||
      contentLength < gHttpHandler->SendWindowSize()) {
    mNeedToReportBytesRead = false;
  }

  mCacheNeedToReportBytesReadInitialized = true;
  return mNeedToReportBytesRead;
}

void HttpChannelChild::DoOnStatus(nsIRequest* aRequest, nsresult status) {
  LOG(("HttpChannelChild::DoOnStatus [this=%p]\n", this));
  MOZ_ASSERT(NS_IsMainThread());

  if (mCanceled) return;

  // cache the progress sink so we don't have to query for it each time.
  if (!mProgressSink) GetCallback(mProgressSink);

  // block status/progress after Cancel or OnStopRequest has been called,
  // or if channel has LOAD_BACKGROUND set.
  if (mProgressSink && NS_SUCCEEDED(mStatus) && LoadIsPending() &&
      !(mLoadFlags & LOAD_BACKGROUND)) {
    nsAutoCString host;
    mURI->GetHost(host);
    nsCOMPtr<nsIProgressEventSink> progressSink(mProgressSink);
    progressSink->OnStatus(aRequest, status, NS_ConvertUTF8toUTF16(host).get());
  }
}

void HttpChannelChild::DoOnProgress(nsIRequest* aRequest, int64_t progress,
                                    int64_t progressMax) {
  LOG(("HttpChannelChild::DoOnProgress [this=%p]\n", this));
  MOZ_ASSERT(NS_IsMainThread());

  if (mCanceled) return;

  // cache the progress sink so we don't have to query for it each time.
  if (!mProgressSink) GetCallback(mProgressSink);

  // block status/progress after Cancel or OnStopRequest has been called,
  // or if channel has LOAD_BACKGROUND set.
  if (mProgressSink && NS_SUCCEEDED(mStatus) && LoadIsPending()) {
    // OnProgress
    //
    if (progress > 0) {
      nsCOMPtr<nsIProgressEventSink> progressSink(mProgressSink);
      progressSink->OnProgress(aRequest, progress, progressMax);
    }
  }

  // mOnProgressEventSent indicates we have flushed all the
  // progress events on the main thread. It is needed if
  // we do not want to dispatch OnDataFinished before sending
  // all of the progress updates.
  if (progress == progressMax) {
    mOnProgressEventSent = true;
  }
}

void HttpChannelChild::DoOnDataAvailable(nsIRequest* aRequest,
                                         nsIInputStream* aStream,
                                         uint64_t aOffset, uint32_t aCount) {
  AUTO_PROFILER_LABEL("HttpChannelChild::DoOnDataAvailable", NETWORK);
  LOG(("HttpChannelChild::DoOnDataAvailable [this=%p, request=%p]\n", this,
       aRequest));
  if (mCanceled) return;

  mGotDataAvailable = true;
  if (mListener) {
    nsCOMPtr<nsIStreamListener> listener(mListener);
    nsresult rv = listener->OnDataAvailable(aRequest, aStream, aOffset, aCount);
    if (NS_FAILED(rv)) {
      CancelOnMainThread(rv, "HttpChannelChild OnDataAvailable failed"_ns);
    }
  }
}

void HttpChannelChild::SendOnDataFinished(const nsresult& aChannelStatus) {
  LOG(("HttpChannelChild::SendOnDataFinished [this=%p]\n", this));

  if (mCanceled) return;

  // we need to ensure we OnDataFinished only after all the progress
  // updates are dispatched on the main thread
  if (StaticPrefs::network_send_OnDataFinished_after_progress_updates() &&
      !mOnProgressEventSent) {
    return;
  }

  if (mListener) {
    nsCOMPtr<nsIThreadRetargetableStreamListener> omtEventListener =
        do_QueryInterface(mListener);
    if (omtEventListener) {
      LOG(
          ("HttpChannelChild::SendOnDataFinished sending data end "
           "notification[this=%p]\n",
           this));
      // We want to calculate the delta time between this call and
      // ProcessOnStopRequest.  Complicating things is that OnStopRequest
      // could come first, and that it will run on a different thread, so
      // we need to synchronize and lock data.
      omtEventListener->OnDataFinished(aChannelStatus);
    } else {
      LOG(
          ("HttpChannelChild::SendOnDataFinished missing "
           "nsIThreadRetargetableStreamListener "
           "implementation [this=%p]\n",
           this));
    }
  }
}

void HttpChannelChild::ProcessOnStopRequest(
    const nsresult& aChannelStatus, const ResourceTimingStructArgs& aTiming,
    const nsHttpHeaderArray& aResponseTrailers,
    nsTArray<ConsoleReportCollected>&& aConsoleReports, bool aFromSocketProcess,
    const TimeStamp& aOnStopRequestStartTime) {
  LOG(
      ("HttpChannelChild::ProcessOnStopRequest [this=%p, "
       "aFromSocketProcess=%d]\n",
       this, aFromSocketProcess));
  MOZ_ASSERT(OnSocketThread());
  mTransferSize = aTiming.transferSize();
  mEncodedBodySize = aTiming.encodedBodySize();
  mDecodedBodySize = aTiming.decodedBodySize();

  if (StaticPrefs::network_send_OnDataFinished()) {
    mEventQ->RunOrEnqueue(MakeUnique<ChannelFunctionEvent>(
        [self = UnsafePtr<HttpChannelChild>(this)]() {
          return self->GetODATarget();
        },
        [self = UnsafePtr<HttpChannelChild>(this), status = aChannelStatus]() {
          self->SendOnDataFinished(status);
        }));
  }
  mEventQ->RunOrEnqueue(MakeUnique<NeckoTargetChannelFunctionEvent>(
      this, [self = UnsafePtr<HttpChannelChild>(this), aChannelStatus, aTiming,
             aResponseTrailers,
             consoleReports = CopyableTArray{aConsoleReports.Clone()},
             aFromSocketProcess]() mutable {
        self->OnStopRequest(aChannelStatus, aTiming, aResponseTrailers);
        if (!aFromSocketProcess) {
          self->DoOnConsoleReport(std::move(consoleReports));
          self->ContinueOnStopRequest();
        }
      }));
}

void HttpChannelChild::ProcessOnConsoleReport(
    nsTArray<ConsoleReportCollected>&& aConsoleReports) {
  LOG(("HttpChannelChild::ProcessOnConsoleReport [this=%p]\n", this));
  MOZ_ASSERT(OnSocketThread());

  mEventQ->RunOrEnqueue(MakeUnique<NeckoTargetChannelFunctionEvent>(
      this,
      [self = UnsafePtr<HttpChannelChild>(this),
       consoleReports = CopyableTArray{aConsoleReports.Clone()}]() mutable {
        self->DoOnConsoleReport(std::move(consoleReports));
        self->ContinueOnStopRequest();
      }));
}

void HttpChannelChild::DoOnConsoleReport(
    nsTArray<ConsoleReportCollected>&& aConsoleReports) {
  if (aConsoleReports.IsEmpty()) {
    return;
  }

  for (ConsoleReportCollected& report : aConsoleReports) {
    AddConsoleReport(report.errorFlags(), report.category(),
                     report.propertiesFile(), report.sourceFileURI(),
                     report.lineNumber(), report.columnNumber(),
                     report.messageName(), report.stringParams());
  }
  MaybeFlushConsoleReports();
}

void HttpChannelChild::OnStopRequest(
    const nsresult& aChannelStatus, const ResourceTimingStructArgs& aTiming,
    const nsHttpHeaderArray& aResponseTrailers) {
  LOG(("HttpChannelChild::OnStopRequest [this=%p status=%" PRIx32 "]\n", this,
       static_cast<uint32_t>(aChannelStatus)));
  MOZ_ASSERT(NS_IsMainThread());

  // If this channel was aborted by ActorDestroy, then there may be other
  // OnStartRequest/OnStopRequest/OnDataAvailable IPC messages that need to
  // be handled. In that case we just ignore them to avoid calling the listener
  // twice.
  if (LoadOnStopRequestCalled() && mIPCActorDeleted) {
    return;
  }

  nsCOMPtr<nsICompressConvStats> conv = do_QueryInterface(mCompressListener);
  if (conv) {
    uint64_t decodedDataLength = 0;
    conv->GetDecodedDataLength(&decodedDataLength);
    mDecodedBodySize = decodedDataLength;
  }

  ResourceTimingStructArgsToTimingsStruct(aTiming, mTransactionTimings);

  // Do not overwrite or adjust the original mAsyncOpenTime by timing.fetchStart
  // We must use the original child process time in order to account for child
  // side work and IPC transit overhead.
  // XXX: This depends on TimeStamp being equivalent across processes.
  // This is true for modern hardware but for older platforms it is not always
  // true.

  mRedirectStartTimeStamp = aTiming.redirectStart();
  mRedirectEndTimeStamp = aTiming.redirectEnd();
  // mTransferSize and mEncodedBodySize are set in ProcessOnStopRequest
  // TODO: check if we need to move assignments of other members to
  // ProcessOnStopRequest

  mCacheReadStart = aTiming.cacheReadStart();
  mCacheReadEnd = aTiming.cacheReadEnd();

  const TimeStamp now = TimeStamp::Now();

  if (profiler_thread_is_being_profiled_for_markers()) {
    nsAutoCString requestMethod;
    GetRequestMethod(requestMethod);
    nsAutoCString contentType;
    mozilla::Maybe<mozilla::net::HttpVersion> httpVersion = Nothing();
    mozilla::Maybe<uint32_t> responseStatus = Nothing();
    if (mResponseHead) {
      mResponseHead->ContentType(contentType);
      httpVersion = Some(mResponseHead->Version());
      responseStatus = Some(mResponseHead->Status());
    }
    int32_t priority = PRIORITY_NORMAL;
    GetPriority(&priority);
    profiler_add_network_marker(
        mURI, requestMethod, priority, mChannelId, NetworkLoadType::LOAD_STOP,
        mLastStatusReported, now, mTransferSize, kCacheUnknown,
        mLoadInfo->GetInnerWindowID(),
        mLoadInfo->GetOriginAttributes().IsPrivateBrowsing(), this, mStatus,
        GetSecPurpose(), mLoadInfo->GetActivatedFromNavigationalPrefetch(),
        &mTransactionTimings, std::move(mSource),
        // Skip the version for a cached response: it reflects the original
        // fetch, not this request's connection.
        mIsFromCache ? Nothing() : httpVersion, responseStatus,
        Some(nsDependentCString(contentType.get())));
  }

  TimeDuration channelCompletionDuration = now - mAsyncOpenTime;
  if (mIsFromCache) {
    PerfStats::RecordMeasurement(PerfStats::Metric::HttpChannelCompletion_Cache,
                                 channelCompletionDuration);
  } else {
    PerfStats::RecordMeasurement(
        PerfStats::Metric::HttpChannelCompletion_Network,
        channelCompletionDuration);
  }
  PerfStats::RecordMeasurement(PerfStats::Metric::HttpChannelCompletion,
                               channelCompletionDuration);

  if (!mOnStopRequestStartTime.IsNull()) {
    PerfStats::RecordMeasurement(PerfStats::Metric::OnStopRequestToContent,
                                 now - mOnStopRequestStartTime);
  }

  mResponseTrailers = MakeUnique<nsHttpHeaderArray>(aResponseTrailers);

  DoPreOnStopRequest(aChannelStatus);

  {  // We must flush the queue before we Send__delete__
    // (although we really shouldn't receive any msgs after OnStop),
    // so make sure this goes out of scope before then.
    AutoEventEnqueuer ensureSerialDispatch(mEventQ);

    DoOnStopRequest(this, aChannelStatus);
    // DoOnStopRequest() calls ReleaseListeners()
  }
}

void HttpChannelChild::ContinueOnStopRequest() {
  // If we're a multi-part stream, then don't cleanup yet, and we'll do so
  // in OnAfterLastPart.
  if (mMultiPartID) {
    LOG(
        ("HttpChannelChild::OnStopRequest  - Expecting future parts on a "
         "multipart channel postpone cleaning up."));
    return;
  }

  CollectMixedContentTelemetry();

  CleanupBackgroundChannel();

  // If there is a possibility we might want to write alt data to the cache
  // entry, we keep the channel alive. We still send the DocumentChannelCleanup
  // message but request the cache entry to be kept by the parent.
  // If the channel has failed, the cache entry is in a non-writtable state and
  // we want to release it to not block following consumers.
  if (NS_SUCCEEDED(mStatus) && !mPreferredCachedAltDataTypes.IsEmpty()) {
    mKeptAlive = true;
    SendDocumentChannelCleanup(false);  // don't clear cache entry
    return;
  }

  if (mLoadFlags & LOAD_DOCUMENT_URI) {
    // Keep IPDL channel open, but only for updating security info.
    // If IPDL is already closed, then do nothing.
    if (CanSend()) {
      mKeptAlive = true;
      SendDocumentChannelCleanup(true);
    }
  } else {
    // The parent process will respond by sending a DeleteSelf message and
    // making sure not to send any more messages after that.
    TrySendDeletingChannel();
  }
}

void HttpChannelChild::DoPreOnStopRequest(nsresult aStatus) {
  AUTO_PROFILER_LABEL("HttpChannelChild::DoPreOnStopRequest", NETWORK);
  LOG(("HttpChannelChild::DoPreOnStopRequest [this=%p status=%" PRIx32 "]\n",
       this, static_cast<uint32_t>(aStatus)));
  StoreIsPending(false);

  MaybeReportTimingData();

  if (!mCanceled && NS_SUCCEEDED(mStatus)) {
    mStatus = aStatus;
  }
}

// We want to inspect all upgradable mixed content loads
// (i.e., loads point to HTTP from an HTTPS page), for
// resources that stem from audio, video and img elements.
// Of those, we want to measure which succceed and which fail.
// Some double negatives, but we check the following:exempt loads that
// 1) Request was upgraded as mixed passive content
// 2) Request _could_ have been upgraded as mixed passive content if the pref
// had been set and Request wasn't upgraded by any other means (URL isn't https)
void HttpChannelChild::CollectMixedContentTelemetry() {
  MOZ_ASSERT(NS_IsMainThread());

  bool wasUpgraded = mLoadInfo->GetBrowserDidUpgradeInsecureRequests();
  if (!wasUpgraded) {
    // If this wasn't upgraded, let's check if it _could_ have been upgraded as
    // passive mixed content and that it wasn't upgraded with any other method
    if (!mURI->SchemeIs("https") &&
        !mLoadInfo->GetBrowserWouldUpgradeInsecureRequests()) {
      return;
    }
  }

  // UseCounters require a document.
  RefPtr<Document> doc;
  mLoadInfo->GetLoadingDocument(getter_AddRefs(doc));
  if (!doc) {
    return;
  }

  nsContentPolicyType internalLoadType;
  mLoadInfo->GetInternalContentPolicyType(&internalLoadType);
  bool statusIsSuccess = NS_SUCCEEDED(mStatus);

  if (internalLoadType == nsIContentPolicy::TYPE_INTERNAL_IMAGE) {
    if (wasUpgraded) {
      doc->SetUseCounter(
          statusIsSuccess
              ? eUseCounter_custom_MixedContentUpgradedImageSuccess
              : eUseCounter_custom_MixedContentUpgradedImageFailure);
    } else {
      doc->SetUseCounter(
          statusIsSuccess
              ? eUseCounter_custom_MixedContentNotUpgradedImageSuccess
              : eUseCounter_custom_MixedContentNotUpgradedImageFailure);
    }
    return;
  }
  if (internalLoadType == nsIContentPolicy::TYPE_INTERNAL_VIDEO) {
    if (wasUpgraded) {
      doc->SetUseCounter(
          statusIsSuccess
              ? eUseCounter_custom_MixedContentUpgradedVideoSuccess
              : eUseCounter_custom_MixedContentUpgradedVideoFailure);
    } else {
      doc->SetUseCounter(
          statusIsSuccess
              ? eUseCounter_custom_MixedContentNotUpgradedVideoSuccess
              : eUseCounter_custom_MixedContentNotUpgradedVideoFailure);
    }
    return;
  }
  if (internalLoadType == nsIContentPolicy::TYPE_INTERNAL_AUDIO) {
    if (wasUpgraded) {
      doc->SetUseCounter(
          statusIsSuccess
              ? eUseCounter_custom_MixedContentUpgradedAudioSuccess
              : eUseCounter_custom_MixedContentUpgradedAudioFailure);
    } else {
      doc->SetUseCounter(
          statusIsSuccess
              ? eUseCounter_custom_MixedContentNotUpgradedAudioSuccess
              : eUseCounter_custom_MixedContentNotUpgradedAudioFailure);
    }
  }
}

void HttpChannelChild::DoOnStopRequest(nsIRequest* aRequest,
                                       nsresult aChannelStatus) {
  AUTO_PROFILER_LABEL("HttpChannelChild::DoOnStopRequest", NETWORK);
  LOG(("HttpChannelChild::DoOnStopRequest [this=%p, request=%p]\n", this,
       aRequest));
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!LoadIsPending());

  auto checkForBlockedContent = [&]() {
    // NB: We use aChannelStatus here instead of mStatus because if there was an
    // nsCORSListenerProxy on this request, it will override the tracking
    // protection's return value.
    if (ChannelClassifierUtils::IsClassifierBlockingErrorCode(aChannelStatus) ||
        aChannelStatus == NS_ERROR_MALWARE_URI ||
        aChannelStatus == NS_ERROR_UNWANTED_URI ||
        aChannelStatus == NS_ERROR_BLOCKED_URI ||
        aChannelStatus == NS_ERROR_HARMFUL_URI ||
        aChannelStatus == NS_ERROR_HARMFULADDON_URI ||
        aChannelStatus == NS_ERROR_PHISHING_URI) {
      nsCString list, provider, fullhash;

      nsresult rv = GetMatchedList(list);
      NS_ENSURE_SUCCESS_VOID(rv);

      rv = GetMatchedProvider(provider);
      NS_ENSURE_SUCCESS_VOID(rv);

      rv = GetMatchedFullHash(fullhash);
      NS_ENSURE_SUCCESS_VOID(rv);

      ChannelClassifierUtils::SetBlockedContent(this, aChannelStatus, list,
                                                provider, fullhash);
    }
  };
  checkForBlockedContent();

  MaybeLogCOEPError(aChannelStatus);

  // See bug 1587686. If the redirect setup is not completed, the post-redirect
  // channel will be not opened and mListener will be null.
  MOZ_ASSERT(mListener || !LoadWasOpened());
  if (!mListener) {
    return;
  }

  MOZ_ASSERT(!LoadOnStopRequestCalled(),
             "We should not call OnStopRequest twice");

  // notify "http-on-before-stop-request" observers
  gHttpHandler->OnBeforeStopRequest(this);

  if (mListener) {
    nsCOMPtr<nsIStreamListener> listener(mListener);
    StoreOnStopRequestCalled(true);
    listener->OnStopRequest(aRequest, mStatus);
  }
  StoreOnStopRequestCalled(true);

  // If we're a multi-part stream, then don't cleanup yet, and we'll do so
  // in OnAfterLastPart.
  if (mMultiPartID) {
    LOG(
        ("HttpChannelChild::DoOnStopRequest  - Expecting future parts on a "
         "multipart channel not releasing listeners."));
    StoreOnStopRequestCalled(false);
    StoreOnStartRequestCalled(false);
    return;
  }

  // notify "http-on-stop-request" observers
  gHttpHandler->OnStopRequest(this);

  ReleaseListeners();

  // If a preferred alt-data type was set, the parent would hold a reference to
  // the cache entry in case the child calls openAlternativeOutputStream().
  // (see nsHttpChannel::OnStopRequest)
  if (!mPreferredCachedAltDataTypes.IsEmpty()) {
    mAltDataCacheEntryAvailable = mCacheEntryAvailable;
  }
  mCacheEntryAvailable = false;

  if (mLoadGroup) mLoadGroup->RemoveRequest(this, nullptr, mStatus);
}

void HttpChannelChild::ProcessOnProgress(const int64_t& aProgress,
                                         const int64_t& aProgressMax) {
  MOZ_ASSERT(OnSocketThread());
  LOG(("HttpChannelChild::ProcessOnProgress [this=%p]\n", this));
  mEventQ->RunOrEnqueue(MakeUnique<NeckoTargetChannelFunctionEvent>(
      this,
      [self = UnsafePtr<HttpChannelChild>(this), aProgress, aProgressMax]() {
        AutoEventEnqueuer ensureSerialDispatch(self->mEventQ);
        self->DoOnProgress(self, aProgress, aProgressMax);
      }));
}

void HttpChannelChild::ProcessOnStatus(const nsresult& aStatus) {
  MOZ_ASSERT(OnSocketThread());
  LOG(("HttpChannelChild::ProcessOnStatus [this=%p]\n", this));
  mEventQ->RunOrEnqueue(MakeUnique<NeckoTargetChannelFunctionEvent>(
      this, [self = UnsafePtr<HttpChannelChild>(this), aStatus]() {
        AutoEventEnqueuer ensureSerialDispatch(self->mEventQ);
        self->DoOnStatus(self, aStatus);
      }));
}

mozilla::ipc::IPCResult HttpChannelChild::RecvFailedAsyncOpen(
    const nsresult& aStatus) {
  LOG(("HttpChannelChild::RecvFailedAsyncOpen [this=%p]\n", this));
  mEventQ->RunOrEnqueue(MakeUnique<NeckoTargetChannelFunctionEvent>(
      this, [self = UnsafePtr<HttpChannelChild>(this), aStatus]() {
        self->FailedAsyncOpen(aStatus);
      }));
  return IPC_OK();
}

// We need to have an implementation of this function just so that we can keep
// all references to mCallOnResume of type HttpChannelChild:  it's not OK in C++
// to set a member function ptr to a base class function.
void HttpChannelChild::HandleAsyncAbort() {
  HttpAsyncAborter<HttpChannelChild>::HandleAsyncAbort();

  // Ignore all the messages from background channel after channel aborted.
  CleanupBackgroundChannel();
}

void HttpChannelChild::FailedAsyncOpen(const nsresult& status) {
  LOG(("HttpChannelChild::FailedAsyncOpen [this=%p status=%" PRIx32 "]\n", this,
       static_cast<uint32_t>(status)));
  MOZ_ASSERT(NS_IsMainThread());

  // Might be called twice in race condition in theory.
  // (one by RecvFailedAsyncOpen, another by
  // HttpBackgroundChannelChild::ActorFailed)
  if (LoadOnStartRequestCalled()) {
    return;
  }

  if (NS_SUCCEEDED(mStatus)) {
    mStatus = status;
  }

  // We're already being called from IPDL, therefore already "async"
  HandleAsyncAbort();

  if (CanSend()) {
    TrySendDeletingChannel();
  }
}

void HttpChannelChild::CleanupBackgroundChannel() {
  MutexAutoLock lock(mBgChildMutex);

  AUTO_PROFILER_LABEL("HttpChannelChild::CleanupBackgroundChannel", NETWORK);
  LOG(("HttpChannelChild::CleanupBackgroundChannel [this=%p bgChild=%p]\n",
       this, mBgChild.get()));

  mBgInitFailCallback = nullptr;

  if (!mBgChild) {
    return;
  }

  RefPtr<HttpBackgroundChannelChild> bgChild = std::move(mBgChild);

  MOZ_RELEASE_ASSERT(gSocketTransportService);
  if (!OnSocketThread()) {
    gSocketTransportService->Dispatch(
        NewRunnableMethod("HttpBackgroundChannelChild::OnChannelClosed",
                          bgChild,
                          &HttpBackgroundChannelChild::OnChannelClosed),
        NS_DISPATCH_NORMAL);
  } else {
    bgChild->OnChannelClosed();
  }
}

void HttpChannelChild::DoNotifyListenerCleanup() {
  LOG(("HttpChannelChild::DoNotifyListenerCleanup [this=%p]\n", this));
}

void HttpChannelChild::DoAsyncAbort(nsresult aStatus) {
  (void)AsyncAbort(aStatus);
}

mozilla::ipc::IPCResult HttpChannelChild::RecvDeleteSelf() {
  LOG(("HttpChannelChild::RecvDeleteSelf [this=%p]\n", this));
  MOZ_ASSERT(NS_IsMainThread());

  // The redirection is vetoed. No need to suspend the event queue.
  if (mSuspendForWaitCompleteRedirectSetup) {
    mSuspendForWaitCompleteRedirectSetup = false;
    mEventQ->Resume();
  }

  mEventQ->RunOrEnqueue(MakeUnique<NeckoTargetChannelFunctionEvent>(
      this,
      [self = UnsafePtr<HttpChannelChild>(this)]() { self->DeleteSelf(); }));
  return IPC_OK();
}

void HttpChannelChild::DeleteSelf() { Send__delete__(this); }

void HttpChannelChild::NotifyOrReleaseListeners(nsresult rv) {
  MOZ_ASSERT(NS_IsMainThread());

  if (NS_SUCCEEDED(rv) ||
      (LoadOnStartRequestCalled() && LoadOnStopRequestCalled())) {
    ReleaseListeners();
    return;
  }

  if (NS_SUCCEEDED(mStatus)) {
    mStatus = rv;
  }

  // This is enough what we need.  Undelivered notifications will be pushed.
  // DoNotifyListener ensures the call to ReleaseListeners when done.
  DoNotifyListener();
}

void HttpChannelChild::DoNotifyListener(bool aUseEventQueue) {
  LOG(("HttpChannelChild::DoNotifyListener this=%p", this));
  MOZ_ASSERT(NS_IsMainThread());

  // In case nsHttpChannel::OnStartRequest wasn't called (e.g. due to flag
  // LOAD_ONLY_IF_MODIFIED) we want to set LoadAfterOnStartRequestBegun() to
  // true before notifying listener.
  if (!LoadAfterOnStartRequestBegun()) {
    StoreAfterOnStartRequestBegun(true);
  }

  if (mListener && !LoadOnStartRequestCalled()) {
    nsCOMPtr<nsIStreamListener> listener = mListener;
    // avoid reentrancy bugs by setting this now
    StoreOnStartRequestCalled(true);
    listener->OnStartRequest(this);
  }
  StoreOnStartRequestCalled(true);

  if (aUseEventQueue) {
    mEventQ->RunOrEnqueue(MakeUnique<NeckoTargetChannelFunctionEvent>(
        this, [self = UnsafePtr<HttpChannelChild>(this)] {
          self->ContinueDoNotifyListener();
        }));
  } else {
    ContinueDoNotifyListener();
  }
}

void HttpChannelChild::ContinueDoNotifyListener() {
  LOG(("HttpChannelChild::ContinueDoNotifyListener this=%p", this));
  MOZ_ASSERT(NS_IsMainThread());

  // Make sure IsPending is set to false. At this moment we are done from
  // the point of view of our consumer and we have to report our self
  // as not-pending.
  StoreIsPending(false);

  // notify "http-on-before-stop-request" observers
  gHttpHandler->OnBeforeStopRequest(this);

  if (mListener && !LoadOnStopRequestCalled()) {
    nsCOMPtr<nsIStreamListener> listener = mListener;
    StoreOnStopRequestCalled(true);
    listener->OnStopRequest(this, mStatus);
  }
  StoreOnStopRequestCalled(true);

  // notify "http-on-stop-request" observers
  gHttpHandler->OnStopRequest(this);

  // This channel has finished its job, potentially release any tail-blocked
  // requests with this.
  RemoveAsNonTailRequest();

  // We have to make sure to drop the references to listeners and callbacks
  // no longer needed.
  ReleaseListeners();

  DoNotifyListenerCleanup();

  // If this is a navigation, then we must let the docshell flush the reports
  // to the console later.  The LoadDocument() is pointing at the detached
  // document that started the navigation.  We want to show the reports on the
  // new document.  Otherwise the console is wiped and the user never sees
  // the information.
  if (!IsNavigation()) {
    if (mLoadGroup) {
      FlushConsoleReports(mLoadGroup);
    } else {
      RefPtr<dom::Document> doc;
      mLoadInfo->GetLoadingDocument(getter_AddRefs(doc));
      FlushConsoleReports(doc);
    }
  }
}

mozilla::ipc::IPCResult HttpChannelChild::RecvReportSecurityMessage(
    const nsAString& messageTag, const nsAString& messageCategory) {
  DebugOnly<nsresult> rv = AddSecurityMessage(messageTag, messageCategory);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  return IPC_OK();
}

mozilla::ipc::IPCResult HttpChannelChild::RecvReportLNAToConsole(
    const NetAddr& aPeerAddr, const nsACString& aMessageType,
    const nsACString& aPromptAction, const nsACString& aTopLevelSite) {
  nsCOMPtr<nsILoadInfo> loadInfo = LoadInfo();
  nsCOMPtr<nsIURI> uri;
  GetURI(getter_AddRefs(uri));

  if (!loadInfo || !uri) {
    return IPC_OK();
  }

  // Use top-level site passed from parent process via IPC.
  // This is necessary because in cross-site scenarios with Fission,
  // the content process cannot access the top-level document which
  // exists in a different process.
  nsAutoCString topLevelSite(aTopLevelSite);

  // Get initiator (triggering principal)
  nsAutoCString initiator;
  nsCOMPtr<nsIPrincipal> triggeringPrincipal;
  loadInfo->GetTriggeringPrincipal(getter_AddRefs(triggeringPrincipal));
  if (triggeringPrincipal) {
    nsCOMPtr<nsIURI> triggeringURI = triggeringPrincipal->GetURI();
    if (triggeringURI) {
      initiator = triggeringURI->GetSpecOrDefault();
    }
  }

  // Get target URL (full spec shown to users for clarity)
  nsAutoCString targetURL;
  targetURL = uri->GetSpecOrDefault();

  // Get target IP address from passed NetAddr
  nsCString targetIp = aPeerAddr.ToString();

  // Get port
  uint16_t port = 0;
  (void)aPeerAddr.GetPort(&port);

  // Determine request mechanism from LoadInfo
  nsAutoCString mechanism;
  ExtContentPolicyType contentType = loadInfo->GetExternalContentPolicyType();
  switch (contentType) {
    case ExtContentPolicyType::TYPE_WEBSOCKET:
      mechanism.AssignLiteral("websocket");
      break;
    case ExtContentPolicyType::TYPE_WEB_TRANSPORT:
      mechanism.AssignLiteral("webtransport");
      break;
    case ExtContentPolicyType::TYPE_FETCH:
      mechanism.AssignLiteral("fetch");
      break;
    case ExtContentPolicyType::TYPE_XMLHTTPREQUEST:
      mechanism.AssignLiteral("xhr");
      break;
    default:
      if (uri->SchemeIs("https")) {
        mechanism.AssignLiteral("https");
      } else {
        mechanism.AssignLiteral("http");
      }
      break;
  }

  // Check if the originating context is secure (required by LNA spec)
  bool isSecureContext = false;
  if (triggeringPrincipal) {
    isSecureContext = triggeringPrincipal->GetIsOriginPotentiallyTrustworthy();
  }

  // Build console parameters
  AutoTArray<nsString, 8> consoleParams;
  CopyUTF8toUTF16(
      topLevelSite.IsEmpty() ? nsAutoCString("(empty)") : topLevelSite,
      *consoleParams.AppendElement());
  CopyUTF8toUTF16(initiator.IsEmpty() ? nsAutoCString("(empty)") : initiator,
                  *consoleParams.AppendElement());
  CopyUTF8toUTF16(targetURL.IsEmpty() ? nsAutoCString("(empty)") : targetURL,
                  *consoleParams.AppendElement());
  CopyUTF8toUTF16(targetIp, *consoleParams.AppendElement());
  consoleParams.AppendElement()->AppendInt(port);
  CopyUTF8toUTF16(mechanism, *consoleParams.AppendElement());
  CopyUTF8toUTF16(
      isSecureContext ? nsAutoCString("True") : nsAutoCString("False"),
      *consoleParams.AppendElement());

  // Add prompt action if provided (for LocalNetworkAccessDetected message)
  if (!aPromptAction.IsEmpty()) {
    CopyUTF8toUTF16(aPromptAction, *consoleParams.AppendElement());
  }

  // Build the formatted message with stack trace
  nsAutoString formattedMsg;
  nsContentUtils::FormatLocalizedString(PropertiesFile::NECKO_PROPERTIES,
                                        PromiseFlatCString(aMessageType).get(),
                                        consoleParams, formattedMsg);

  // Append stack trace to the message if available
  const char* callStack = GetCallStack();
  if (callStack && callStack[0] != '\0') {
    formattedMsg.AppendLiteral("\n");
    formattedMsg.Append(NS_ConvertUTF8toUTF16(callStack));
  }

  uint64_t innerWindowID = 0;
  loadInfo->GetInnerWindowID(&innerWindowID);

  nsCOMPtr<nsIURI> sourceURI = uri;  // fallback to target
  if (triggeringPrincipal) {
    nsCOMPtr<nsIURI> principalURI = triggeringPrincipal->GetURI();
    if (principalURI) {
      sourceURI = std::move(principalURI);
    }
  }

  // Report to web console
  if (innerWindowID) {
    nsContentUtils::ReportToConsoleByWindowID(
        formattedMsg, nsIScriptError::infoFlag, "Security"_ns, innerWindowID,
        mozilla::SourceLocation(sourceURI.get()));
  } else {
    RefPtr<dom::Document> doc;
    loadInfo->GetLoadingDocument(getter_AddRefs(doc));
    nsContentUtils::ReportToConsoleNonLocalized(
        formattedMsg, nsIScriptError::infoFlag, "Security"_ns, doc,
        mozilla::SourceLocation(sourceURI.get()));
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult HttpChannelChild::RecvRedirect1Begin(
    const uint32_t& aRegistrarId, nsIURI* aNewUri,
    const uint32_t& aNewLoadFlags, const uint32_t& aRedirectFlags,
    const ParentLoadInfoForwarderArgs& aLoadInfoForwarder,
    nsHttpResponseHead&& aResponseHead, nsITransportSecurityInfo* aSecurityInfo,
    const uint64_t& aChannelId, const NetAddr& aOldPeerAddr,
    const ResourceTimingStructArgs& aTiming) {
  // TODO: handle security info
  LOG(("HttpChannelChild::RecvRedirect1Begin [this=%p]\n", this));
  // We set peer address of child to the old peer,
  // Then it will be updated to new peer in OnStartRequest
  mPeerAddr = aOldPeerAddr;

  // Cookies headers should not be visible to the child process
  MOZ_ASSERT(!nsHttpResponseHead(aResponseHead).HasHeader(nsHttp::Set_Cookie));

  mEventQ->RunOrEnqueue(MakeUnique<NeckoTargetChannelFunctionEvent>(
      this, [self = UnsafePtr<HttpChannelChild>(this), aRegistrarId,
             newUri = RefPtr{aNewUri}, aNewLoadFlags, aRedirectFlags,
             aLoadInfoForwarder, aResponseHead = std::move(aResponseHead),
             aSecurityInfo = nsCOMPtr{aSecurityInfo}, aChannelId, aTiming]() {
        self->Redirect1Begin(aRegistrarId, newUri, aNewLoadFlags,
                             aRedirectFlags, aLoadInfoForwarder, aResponseHead,
                             aSecurityInfo, aChannelId, aTiming);
      }));
  return IPC_OK();
}

nsresult HttpChannelChild::SetupRedirect(nsIURI* uri,
                                         const nsHttpResponseHead* responseHead,
                                         const uint32_t& redirectFlags,
                                         nsIChannel** outChannel) {
  LOG(("HttpChannelChild::SetupRedirect [this=%p]\n", this));

  if (mCanceled) {
    return NS_ERROR_ABORT;
  }

  nsresult rv;
  nsCOMPtr<nsIIOService> ioService;
  rv = gHttpHandler->GetIOService(getter_AddRefs(ioService));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIChannel> newChannel;
  nsCOMPtr<nsILoadInfo> redirectLoadInfo =
      CloneLoadInfoForRedirect(uri, redirectFlags);
  rv = NS_NewChannelInternal(getter_AddRefs(newChannel), uri, redirectLoadInfo,
                             nullptr,  // PerformanceStorage
                             nullptr,  // aLoadGroup
                             nullptr,  // aCallbacks
                             nsIRequest::LOAD_NORMAL, ioService);
  NS_ENSURE_SUCCESS(rv, rv);

  // We won't get OnStartRequest, set cookies here.
  mResponseHead = MakeUnique<nsHttpResponseHead>(*responseHead);

  bool rewriteToGET = HttpBaseChannel::ShouldRewriteRedirectToGET(
      mResponseHead->Status(), mRequestHead.ParsedMethod());

  rv = SetupReplacementChannel(uri, newChannel, !rewriteToGET, redirectFlags);
  NS_ENSURE_SUCCESS(rv, rv);

  mRedirectChannelChild = do_QueryInterface(newChannel);
  newChannel.forget(outChannel);

  return NS_OK;
}

void HttpChannelChild::Redirect1Begin(
    const uint32_t& registrarId, nsIURI* newOriginalURI,
    const uint32_t& newLoadFlags, const uint32_t& redirectFlags,
    const ParentLoadInfoForwarderArgs& loadInfoForwarder,
    const nsHttpResponseHead& responseHead,
    nsITransportSecurityInfo* securityInfo, const uint64_t& channelId,
    const ResourceTimingStructArgs& timing) {
  nsresult rv;

  LOG(("HttpChannelChild::Redirect1Begin [this=%p]\n", this));

  MOZ_ASSERT(newOriginalURI, "newOriginalURI should not be null");

  ipc::MergeParentLoadInfoForwarder(loadInfoForwarder, mLoadInfo);
  ResourceTimingStructArgsToTimingsStruct(timing, mTransactionTimings);

  if (profiler_thread_is_being_profiled_for_markers()) {
    nsAutoCString requestMethod;
    GetRequestMethod(requestMethod);
    nsAutoCString contentType;
    responseHead.ContentType(contentType);

    profiler_add_network_marker(
        mURI, requestMethod, mPriority, mChannelId,
        NetworkLoadType::LOAD_REDIRECT, mLastStatusReported, TimeStamp::Now(),
        0, kCacheUnknown, mLoadInfo->GetInnerWindowID(),
        mLoadInfo->GetOriginAttributes().IsPrivateBrowsing(), this, mStatus,
        GetSecPurpose(), mLoadInfo->GetActivatedFromNavigationalPrefetch(),
        &mTransactionTimings, std::move(mSource), Some(responseHead.Version()),
        Some(responseHead.Status()),
        Some(nsDependentCString(contentType.get())), newOriginalURI,
        redirectFlags, channelId);
  }

  mSecurityInfo = securityInfo;

  nsCOMPtr<nsIChannel> newChannel;
  rv = SetupRedirect(newOriginalURI, &responseHead, redirectFlags,
                     getter_AddRefs(newChannel));

  if (NS_SUCCEEDED(rv)) {
    MOZ_ALWAYS_SUCCEEDS(newChannel->SetLoadFlags(newLoadFlags));

    if (mRedirectChannelChild) {
      // Set the channelId allocated in parent to the child instance
      nsCOMPtr<nsIHttpChannel> httpChannel =
          do_QueryInterface(mRedirectChannelChild);
      if (httpChannel) {
        rv = httpChannel->SetChannelId(channelId);
        MOZ_ASSERT(NS_SUCCEEDED(rv));
      }
      mRedirectChannelChild->ConnectParent(registrarId);
    }

    nsCOMPtr<nsISerialEventTarget> target = GetNeckoTarget();
    MOZ_ASSERT(target);

    rv = gHttpHandler->AsyncOnChannelRedirect(this, newChannel, redirectFlags,
                                              target);
  }

  if (NS_FAILED(rv)) OnRedirectVerifyCallback(rv);
}

mozilla::ipc::IPCResult HttpChannelChild::RecvRedirect3Complete() {
  LOG(("HttpChannelChild::RecvRedirect3Complete [this=%p]\n", this));
  nsCOMPtr<nsIChannel> redirectChannel =
      do_QueryInterface(mRedirectChannelChild);
  MOZ_ASSERT(redirectChannel);
  mEventQ->RunOrEnqueue(MakeUnique<NeckoTargetChannelFunctionEvent>(
      this, [self = UnsafePtr<HttpChannelChild>(this), redirectChannel]() {
        nsresult rv = NS_OK;
        (void)self->GetStatus(&rv);
        if (NS_FAILED(rv)) {
          // Pre-redirect channel was canceled. Call |HandleAsyncAbort|, so
          // mListener's OnStart/StopRequest can be called. Nothing else will
          // trigger these notification after this point.
          // We do this before |CompleteRedirectSetup|, so post-redirect channel
          // stays unopened and we also make sure that OnStart/StopRequest won't
          // be called twice.
          self->HandleAsyncAbort();

          nsCOMPtr<nsIHttpChannelChild> chan =
              do_QueryInterface(redirectChannel);
          RefPtr<HttpChannelChild> httpChannelChild =
              static_cast<HttpChannelChild*>(chan.get());
          if (httpChannelChild) {
            // For sending an IPC message to parent channel so that the loading
            // can be cancelled.
            (void)httpChannelChild->CancelWithReason(
                rv, "HttpChannelChild Redirect3 failed"_ns);

            // The post-redirect channel could still get OnStart/StopRequest IPC
            // messages from parent, but the mListener is still null. So, we
            // call |DoNotifyListener| to pretend that OnStart/StopRequest are
            // already called.
            httpChannelChild->DoNotifyListener();
          }
          return;
        }

        self->Redirect3Complete();
      }));
  return IPC_OK();
}

mozilla::ipc::IPCResult HttpChannelChild::RecvRedirectFailed(
    const nsresult& status) {
  LOG(("HttpChannelChild::RecvRedirectFailed this=%p status=%X\n", this,
       static_cast<uint32_t>(status)));
  mEventQ->RunOrEnqueue(MakeUnique<NeckoTargetChannelFunctionEvent>(
      this, [self = UnsafePtr<HttpChannelChild>(this), status]() {
        nsCOMPtr<nsIRedirectResultListener> vetoHook;
        self->GetCallback(vetoHook);
        if (vetoHook) {
          vetoHook->OnRedirectResult(status);
        }

        if (RefPtr<HttpChannelChild> httpChannelChild =
                do_QueryObject(self->mRedirectChannelChild)) {
          // For sending an IPC message to parent channel so that the loading
          // can be cancelled.
          (void)httpChannelChild->CancelWithReason(
              status, "HttpChannelChild RecvRedirectFailed"_ns);

          // The post-redirect channel could still get OnStart/StopRequest IPC
          // messages from parent, but the mListener is still null. So, we
          // call |DoNotifyListener| to pretend that OnStart/StopRequest are
          // already called.
          httpChannelChild->DoNotifyListener();
        }
      }));

  return IPC_OK();
}

void HttpChannelChild::ProcessNotifyClassificationFlags(
    uint32_t aClassificationFlags, bool aIsThirdParty) {
  LOG(
      ("HttpChannelChild::ProcessNotifyClassificationFlags thirdparty=%d "
       "flags=%" PRIu32 " [this=%p]\n",
       static_cast<int>(aIsThirdParty), aClassificationFlags, this));
  MOZ_ASSERT(OnSocketThread());

  mEventQ->RunOrEnqueue(MakeUnique<NeckoTargetChannelFunctionEvent>(
      this, [self = UnsafePtr<HttpChannelChild>(this), aClassificationFlags,
             aIsThirdParty]() {
        self->AddClassificationFlags(aClassificationFlags, aIsThirdParty);
      }));
}

void HttpChannelChild::ProcessSetClassifierMatchedInfo(
    const nsACString& aList, const nsACString& aProvider,
    const nsACString& aFullHash) {
  LOG(("HttpChannelChild::ProcessSetClassifierMatchedInfo [this=%p]\n", this));
  MOZ_ASSERT(OnSocketThread());

  mEventQ->RunOrEnqueue(MakeUnique<NeckoTargetChannelFunctionEvent>(
      this,
      [self = UnsafePtr<HttpChannelChild>(this), aList = nsCString(aList),
       aProvider = nsCString(aProvider), aFullHash = nsCString(aFullHash)]() {
        self->SetMatchedInfo(aList, aProvider, aFullHash);
      }));
}

void HttpChannelChild::ProcessSetClassifierMatchedTrackingInfo(
    const nsACString& aLists, const nsACString& aFullHashes) {
  LOG(("HttpChannelChild::ProcessSetClassifierMatchedTrackingInfo [this=%p]\n",
       this));
  MOZ_ASSERT(OnSocketThread());

  nsTArray<nsCString> lists, fullhashes;
  for (const nsACString& token : aLists.Split(',')) {
    lists.AppendElement(token);
  }
  for (const nsACString& token : aFullHashes.Split(',')) {
    fullhashes.AppendElement(token);
  }

  mEventQ->RunOrEnqueue(MakeUnique<NeckoTargetChannelFunctionEvent>(
      this, [self = UnsafePtr<HttpChannelChild>(this),
             lists = CopyableTArray{std::move(lists)},
             fullhashes = CopyableTArray{std::move(fullhashes)}]() {
        self->SetMatchedTrackingInfo(lists, fullhashes);
      }));
}

// Completes the redirect and cleans up the old channel.
void HttpChannelChild::Redirect3Complete() {
  LOG(("HttpChannelChild::Redirect3Complete [this=%p]\n", this));
  MOZ_ASSERT(NS_IsMainThread());

  // Using an error as the default so that when we fail to forward this redirect
  // to the target channel, we make sure to notify the current listener from
  // CleanupRedirectingChannel.
  nsresult rv = NS_BINDING_ABORTED;

  nsCOMPtr<nsIRedirectResultListener> vetoHook;
  GetCallback(vetoHook);
  if (vetoHook) {
    vetoHook->OnRedirectResult(NS_OK);
  }

  // Chrome channel has been AsyncOpen'd.  Reflect this in child.
  if (mRedirectChannelChild) {
    rv = mRedirectChannelChild->CompleteRedirectSetup(mListener);
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
    mSuccesfullyRedirected = NS_SUCCEEDED(rv);
#endif
  }

  CleanupRedirectingChannel(rv);
}

void HttpChannelChild::CleanupRedirectingChannel(nsresult rv) {
  // Redirecting to new channel: shut this down and init new channel
  if (mLoadGroup) mLoadGroup->RemoveRequest(this, nullptr, NS_BINDING_ABORTED);

  if (NS_SUCCEEDED(rv)) {
    mLoadInfo->AppendRedirectHistoryEntry(this, false);
  } else {
    NS_WARNING("CompleteRedirectSetup failed, HttpChannelChild already open?");
  }

  // Release ref to new channel.
  mRedirectChannelChild = nullptr;

  NotifyOrReleaseListeners(rv);
  CleanupBackgroundChannel();
}

//-----------------------------------------------------------------------------
// HttpChannelChild::nsIChildChannel
//-----------------------------------------------------------------------------

NS_IMETHODIMP
HttpChannelChild::ConnectParent(uint32_t registrarId) {
  LOG(("HttpChannelChild::ConnectParent [this=%p, id=%" PRIu32 "]\n", this,
       registrarId));
  MOZ_ASSERT(NS_IsMainThread());
  mozilla::dom::BrowserChild* browserChild = nullptr;
  nsCOMPtr<nsIBrowserChild> iBrowserChild;
  GetCallback(iBrowserChild);
  if (iBrowserChild) {
    browserChild =
        static_cast<mozilla::dom::BrowserChild*>(iBrowserChild.get());
  }

  if (browserChild && !browserChild->IPCOpen()) {
    return NS_ERROR_FAILURE;
  }

  ContentChild* cc =
      mozilla::ipc::ActorCast<ContentChild>(gNeckoChild->Manager());
  if (cc->IsShuttingDown()) {
    return NS_ERROR_FAILURE;
  }

  // Defensively ensure the process is marked as untrusted.
  ContentChild::MaybeBecomeUntrusted();

  HttpBaseChannel::SetDocshellUserAgentOverride();

  // This must happen before the constructor message is sent. Otherwise messages
  // from the parent could arrive quickly and be delivered to the wrong event
  // target.
  SetEventTarget();

  if (browserChild) {
    MOZ_ASSERT(browserChild->WebNavigation());
    if (BrowsingContext* bc = browserChild->GetBrowsingContext()) {
      mBrowserId = bc->BrowserId();
    }
  }

  HttpChannelConnectArgs connectArgs(registrarId);
  if (!gNeckoChild->SendPHttpChannelConstructor(
          this, browserChild, IPC::SerializedLoadContext(this), connectArgs)) {
    return NS_ERROR_FAILURE;
  }

  {
    MutexAutoLock lock(mBgChildMutex);

    MOZ_ASSERT(!mBgChild);
    MOZ_ASSERT(!mBgInitFailCallback);

    mBgInitFailCallback = NewRunnableMethod<nsresult>(
        "HttpChannelChild::OnRedirectVerifyCallback", this,
        &HttpChannelChild::OnRedirectVerifyCallback, NS_ERROR_FAILURE);

    RefPtr<HttpBackgroundChannelChild> bgChild =
        new HttpBackgroundChannelChild();

    MOZ_RELEASE_ASSERT(gSocketTransportService);

    RefPtr<HttpChannelChild> self = this;
    nsresult rv = gSocketTransportService->Dispatch(
        NewRunnableMethod<RefPtr<HttpChannelChild>>(
            "HttpBackgroundChannelChild::Init", bgChild,
            &HttpBackgroundChannelChild::Init, std::move(self)),
        NS_DISPATCH_NORMAL);

    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    mBgChild = std::move(bgChild);
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
    mEverHadBgChildAtConnectParent = true;
#endif
  }

  // Should wait for CompleteRedirectSetup to set the listener.
  mEventQ->Suspend();
  MOZ_ASSERT(!mSuspendForWaitCompleteRedirectSetup);
  mSuspendForWaitCompleteRedirectSetup = true;

  // Connect to socket process after mEventQ is suspended.
  MaybeConnectToSocketProcess();

  return NS_OK;
}

NS_IMETHODIMP
HttpChannelChild::CompleteRedirectSetup(nsIStreamListener* aListener) {
  LOG(("HttpChannelChild::CompleteRedirectSetup [this=%p]\n", this));
  MOZ_ASSERT(NS_IsMainThread());

  NS_ENSURE_TRUE(!LoadIsPending(), NS_ERROR_IN_PROGRESS);
  NS_ENSURE_TRUE(!LoadWasOpened(), NS_ERROR_ALREADY_OPENED);

  // Resume the suspension in ConnectParent.
  auto eventQueueResumeGuard = MakeScopeExit([&] {
    MOZ_ASSERT(mSuspendForWaitCompleteRedirectSetup);
    mEventQ->Resume();
    mSuspendForWaitCompleteRedirectSetup = false;
  });

  /*
   * No need to check for cancel: we don't get here if nsHttpChannel canceled
   * before AsyncOpen(); if it's canceled after that, OnStart/Stop will just
   * get called with error code as usual.  So just setup mListener and make the
   * channel reflect AsyncOpen'ed state.
   */

  mLastStatusReported = TimeStamp::Now();
  if (profiler_thread_is_being_profiled_for_markers()) {
    nsAutoCString requestMethod;
    GetRequestMethod(requestMethod);

    profiler_add_network_marker(
        mURI, requestMethod, mPriority, mChannelId, NetworkLoadType::LOAD_START,
        mChannelCreationTimestamp, mLastStatusReported, 0, kCacheUnknown,
        mLoadInfo->GetInnerWindowID(),
        mLoadInfo->GetOriginAttributes().IsPrivateBrowsing(), this, mStatus,
        GetSecPurpose(), mLoadInfo->GetActivatedFromNavigationalPrefetch());
  }
  StoreIsPending(true);
  StoreWasOpened(true);
  mListener = aListener;

  // add ourselves to the load group.
  if (mLoadGroup) mLoadGroup->AddRequest(this, nullptr);

  // We already have an open IPDL connection to the parent. If on-modify-request
  // listeners or load group observers canceled us, let the parent handle it
  // and send it back to us naturally.
  return NS_OK;
}

//-----------------------------------------------------------------------------
// HttpChannelChild::nsIAsyncVerifyRedirectCallback
//-----------------------------------------------------------------------------

NS_IMETHODIMP
HttpChannelChild::OnRedirectVerifyCallback(nsresult aResult) {
  LOG(("HttpChannelChild::OnRedirectVerifyCallback [this=%p]\n", this));
  MOZ_ASSERT(NS_IsMainThread());
  nsCOMPtr<nsIURI> redirectURI;

  DebugOnly<nsresult> rv = NS_OK;

  nsCOMPtr<nsIHttpChannel> newHttpChannel =
      do_QueryInterface(mRedirectChannelChild);

  if (NS_SUCCEEDED(aResult) && !mRedirectChannelChild) {
    // mRedirectChannelChild doesn't exist means we're redirecting to a protocol
    // that doesn't implement nsIChildChannel. The redirect result should be set
    // as failed by veto listeners and shouldn't enter this condition. As the
    // last resort, we synthesize the error result as NS_ERROR_DOM_BAD_URI here
    // to let nsHttpChannel::ContinueProcessResponse2 know it's redirecting to
    // another protocol and throw an error.
    LOG(("  redirecting to a protocol that doesn't implement nsIChildChannel"));
    aResult = NS_ERROR_DOM_BAD_URI;
  }

  nsCOMPtr<nsIReferrerInfo> referrerInfo;
  if (newHttpChannel) {
    // Must not be called until after redirect observers called.
    newHttpChannel->SetOriginalURI(mOriginalURI);
    referrerInfo = newHttpChannel->GetReferrerInfo();
  }

  RequestHeaderTuples emptyHeaders;
  RequestHeaderTuples* headerTuples = &emptyHeaders;
  nsLoadFlags loadFlags = 0;
  Maybe<CorsPreflightArgs> corsPreflightArgs;

  nsCOMPtr<nsIHttpChannelChild> newHttpChannelChild =
      do_QueryInterface(mRedirectChannelChild);
  if (newHttpChannelChild && NS_SUCCEEDED(aResult)) {
    rv = newHttpChannelChild->AddCookiesToRequest();
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    rv = newHttpChannelChild->GetClientSetRequestHeaders(&headerTuples);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    newHttpChannelChild->GetClientSetCorsPreflightParameters(corsPreflightArgs);
  }

  if (NS_SUCCEEDED(aResult)) {
    // Note: this is where we would notify "http-on-modify-response" observers.
    // We have deliberately disabled this for child processes (see bug 806753)
    //
    // After we verify redirect, nsHttpChannel may hit the network: must give
    // "http-on-modify-request" observers the chance to cancel before that.
    // base->CallOnModifyRequestObservers();

    nsCOMPtr<nsIHttpChannelInternal> newHttpChannelInternal =
        do_QueryInterface(mRedirectChannelChild);
    if (newHttpChannelInternal) {
      (void)newHttpChannelInternal->GetApiRedirectToURI(
          getter_AddRefs(redirectURI));
    }

    nsCOMPtr<nsIRequest> request = do_QueryInterface(mRedirectChannelChild);
    if (request) {
      request->GetLoadFlags(&loadFlags);
    }
  }

  uint32_t sourceRequestBlockingReason = 0;
  mLoadInfo->GetRequestBlockingReason(&sourceRequestBlockingReason);

  Maybe<ChildLoadInfoForwarderArgs> targetLoadInfoForwarder;
  nsCOMPtr<nsIChannel> newChannel = do_QueryInterface(mRedirectChannelChild);
  if (newChannel) {
    ChildLoadInfoForwarderArgs args;
    nsCOMPtr<nsILoadInfo> loadInfo = newChannel->LoadInfo();
    LoadInfoToChildLoadInfoForwarder(loadInfo, &args);
    targetLoadInfoForwarder.emplace(args);
  }

  if (CanSend()) {
    SendRedirect2Verify(aResult, *headerTuples, sourceRequestBlockingReason,
                        targetLoadInfoForwarder, loadFlags, referrerInfo,
                        redirectURI, corsPreflightArgs);
  }

  return NS_OK;
}

//-----------------------------------------------------------------------------
// HttpChannelChild::nsIRequest
//-----------------------------------------------------------------------------

NS_IMETHODIMP HttpChannelChild::SetCanceledReason(const nsACString& aReason) {
  return SetCanceledReasonImpl(aReason);
}

NS_IMETHODIMP HttpChannelChild::GetCanceledReason(nsACString& aReason) {
  return GetCanceledReasonImpl(aReason);
}

NS_IMETHODIMP
HttpChannelChild::CancelWithReason(nsresult aStatus,
                                   const nsACString& aReason) {
  return CancelWithReasonImpl(aStatus, aReason);
}

NS_IMETHODIMP
HttpChannelChild::Cancel(nsresult aStatus) {
  LOG(("HttpChannelChild::Cancel [this=%p, status=%" PRIx32 "]\n", this,
       static_cast<uint32_t>(aStatus)));
  // only logging on parent is necessary
  Maybe<nsCString> logStack = CallingScriptLocationString();
  Maybe<nsCString> logOnParent;
  if (logStack.isSome()) {
    logOnParent = Some(""_ns);
    logOnParent->AppendPrintf(
        "[this=%p] cancelled call in child process from script: %s", this,
        logStack->get());
  }
  PROFILER_MARKER("HttpChannelChild::Cancel", NETWORK,
                  {MarkerStack::MaybeCapture(
                      profiler_feature_active(ProfilerFeature::Flows))},
                  Tracing, "Http");

  MOZ_ASSERT(NS_IsMainThread());

  if (!mCanceled) {
    // If this cancel occurs before nsHttpChannel has been set up, AsyncOpen
    // is responsible for cleaning up.
    mCanceled = true;
    mStatus = aStatus;

    bool remoteChannelExists = RemoteChannelExists();
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
    mCanSendAtCancel = CanSend();
    mRemoteChannelExistedAtCancel = remoteChannelExists;
#endif

    if (remoteChannelExists) {
      SendCancel(aStatus, mLoadInfo->GetRequestBlockingReason(),
                 mCanceledReason, logOnParent);
    } else if (MOZ_UNLIKELY(!LoadOnStartRequestCalled() ||
                            !LoadOnStopRequestCalled())) {
      (void)AsyncAbort(mStatus);
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
HttpChannelChild::Suspend() {
  LOG(("HttpChannelChild::Suspend [this=%p, mSuspendCount=%" PRIu32 "\n", this,
       mSuspendCount + 1));
  MOZ_ASSERT(NS_IsMainThread());

  LogCallingScriptLocation(this);

  // SendSuspend only once, when suspend goes from 0 to 1.
  // Don't SendSuspend at all if we're diverting callbacks to the parent;
  // suspend will be called at the correct time in the parent itself.
  if (!mSuspendCount++) {
    if (RemoteChannelExists()) {
      SendSuspend();
      mSuspendSent = true;
    }
  }
  mEventQ->Suspend();

  return NS_OK;
}

NS_IMETHODIMP
HttpChannelChild::Resume() {
  LOG(("HttpChannelChild::Resume [this=%p, mSuspendCount=%" PRIu32 "\n", this,
       mSuspendCount - 1));
  MOZ_ASSERT(NS_IsMainThread());
  NS_ENSURE_TRUE(mSuspendCount > 0, NS_ERROR_UNEXPECTED);

  LogCallingScriptLocation(this);

  nsresult rv = NS_OK;

  // SendResume only once, when suspend count drops to 0.
  // Don't SendResume at all if we're diverting callbacks to the parent (unless
  // suspend was sent earlier); otherwise, resume will be called at the correct
  // time in the parent itself.
  if (!--mSuspendCount) {
    if (RemoteChannelExists() && mSuspendSent) {
      SendResume();
    }
    if (mCallOnResume) {
      nsCOMPtr<nsISerialEventTarget> neckoTarget = GetNeckoTarget();
      MOZ_ASSERT(neckoTarget);

      RefPtr<HttpChannelChild> self = this;
      std::function<nsresult(HttpChannelChild*)> callOnResume = nullptr;
      std::swap(callOnResume, mCallOnResume);
      rv = neckoTarget->Dispatch(
          NS_NewRunnableFunction(
              "net::HttpChannelChild::mCallOnResume",
              [callOnResume = std::move(callOnResume),
               self{std::move(self)}]() { callOnResume(self); }),
          NS_DISPATCH_NORMAL);
    }
  }
  mEventQ->Resume();

  return rv;
}

//-----------------------------------------------------------------------------
// HttpChannelChild::nsIChannel
//-----------------------------------------------------------------------------

NS_IMETHODIMP
HttpChannelChild::GetSecurityInfo(nsITransportSecurityInfo** aSecurityInfo) {
  NS_ENSURE_ARG_POINTER(aSecurityInfo);
  *aSecurityInfo = do_AddRef(mSecurityInfo).take();
  return NS_OK;
}

NS_IMETHODIMP
HttpChannelChild::AsyncOpen(nsIStreamListener* aListener) {
  // Capture JavaScript stack for LNA console logging only if:
  // 1. LNA blocking is enabled
  // 2. This is a cross-origin request (LNA only applies to cross-origin)
  if (StaticPrefs::network_lna_blocking() &&
      ReferrerInfo::IsCrossOriginRequest(this)) {
    JSContext* cx = nsContentUtils::GetCurrentJSContext();
    if (cx) {
      JS::UniqueChars chars = xpc_PrintJSStack(cx,
                                               /*showArgs=*/false,
                                               /*showLocals=*/false,
                                               /*showThisProps=*/false);
      if (chars) {
        size_t len = strlen(chars.get());
        mCallStack = mozilla::MakeUnique<char[]>(len + 1);
        memcpy(mCallStack.get(), chars.get(), len + 1);
      }
    }
  }

  AUTO_PROFILER_LABEL("HttpChannelChild::AsyncOpen", NETWORK);
  LOG(("HttpChannelChild::AsyncOpen [this=%p uri=%s]\n", this, mSpec.get()));

  nsresult rv = AsyncOpenInternal(aListener);
  if (NS_FAILED(rv)) {
    uint32_t blockingReason = 0;
    mLoadInfo->GetRequestBlockingReason(&blockingReason);
    LOG(
        ("HttpChannelChild::AsyncOpen failed [this=%p rv=0x%08x "
         "blocking-reason=%u]\n",
         this, static_cast<uint32_t>(rv), blockingReason));

    gHttpHandler->OnFailedOpeningRequest(this);
  }

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  mAsyncOpenSucceeded = NS_SUCCEEDED(rv);
#endif
  return rv;
}

nsresult HttpChannelChild::AsyncOpenInternal(nsIStreamListener* aListener) {
  nsresult rv;

  nsCOMPtr<nsIStreamListener> listener = aListener;
  rv = nsContentSecurityManager::doContentSecurityCheck(this, listener);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    ReleaseListeners();
    return rv;
  }

  MOZ_ASSERT(
      mLoadInfo->GetSecurityMode() == 0 ||
          mLoadInfo->GetInitialSecurityCheckDone() ||
          (mLoadInfo->GetSecurityMode() ==
               nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL &&
           mLoadInfo->GetLoadingPrincipal() &&
           mLoadInfo->GetLoadingPrincipal()->IsSystemPrincipal()),
      "security flags in loadInfo but doContentSecurityCheck() not called");

  LogCallingScriptLocation(this);

  if (!mLoadGroup && !mCallbacks) {
    // If no one called SetLoadGroup or SetNotificationCallbacks, the private
    // state has not been updated on PrivateBrowsingChannel (which we derive
    // from) Hence, we have to call UpdatePrivateBrowsing() here
    UpdatePrivateBrowsing();
  }

#ifdef DEBUG
  AssertPrivateBrowsingId();
#endif

  if (mCanceled) {
    ReleaseListeners();
    return mStatus;
  }

  NS_ENSURE_TRUE(gNeckoChild != nullptr, NS_ERROR_FAILURE);
  NS_ENSURE_ARG_POINTER(listener);
  NS_ENSURE_TRUE(!LoadIsPending(), NS_ERROR_IN_PROGRESS);
  NS_ENSURE_TRUE(!LoadWasOpened(), NS_ERROR_ALREADY_OPENED);

  if (MaybeWaitForUploadStreamNormalization(listener, nullptr)) {
    return NS_OK;
  }

  if (!LoadAsyncOpenTimeOverriden()) {
    mAsyncOpenTime = TimeStamp::Now();
  }

  // Port checked in parent, but duplicate here so we can return with error
  // immediately
  rv = NS_CheckPortSafety(mURI);
  if (NS_FAILED(rv)) {
    ReleaseListeners();
    return rv;
  }

  nsAutoCString cookie;
  if (NS_SUCCEEDED(mRequestHead.GetHeader(nsHttp::Cookie, cookie))) {
    mUserSetCookieHeader = std::move(cookie);
  }

  DebugOnly<nsresult> check = AddCookiesToRequest();
  MOZ_ASSERT(NS_SUCCEEDED(check));

  //
  // NOTE: From now on we must return NS_OK; all errors must be handled via
  // OnStart/OnStopRequest
  //

  // We notify "http-on-opening-request" observers in the child
  // process so that devtools can capture a stack trace at the
  // appropriate spot.  See bug 806753 for some information about why
  // other http-* notifications are disabled in child processes.
  gHttpHandler->OnOpeningRequest(this);

  mLastStatusReported = TimeStamp::Now();
  if (profiler_thread_is_being_profiled_for_markers()) {
    nsAutoCString requestMethod;
    GetRequestMethod(requestMethod);

    profiler_add_network_marker(
        mURI, requestMethod, mPriority, mChannelId, NetworkLoadType::LOAD_START,
        mChannelCreationTimestamp, mLastStatusReported, 0, kCacheUnknown,
        mLoadInfo->GetInnerWindowID(),
        mLoadInfo->GetOriginAttributes().IsPrivateBrowsing(), this, mStatus,
        GetSecPurpose(), mLoadInfo->GetActivatedFromNavigationalPrefetch());
  }
  StoreIsPending(true);
  StoreWasOpened(true);
  mListener = std::move(listener);

  if (mCanceled) {
    // We may have been canceled already, either by on-modify-request
    // listeners or by load group observers; in that case, don't create IPDL
    // connection. See nsHttpChannel::AsyncOpen().
    ReleaseListeners();
    return mStatus;
  }

  // Set user agent override from docshell
  HttpBaseChannel::SetDocshellUserAgentOverride();

  rv = ContinueAsyncOpen();
  if (NS_FAILED(rv)) {
    ReleaseListeners();
  }
  return rv;
}

// Assigns an nsISerialEventTarget to our IPDL actor so that IPC messages are
// sent to the correct DocGroup/TabGroup.
void HttpChannelChild::SetEventTarget() {
  MutexAutoLock lock(mEventTargetMutex);
  mNeckoTarget = GetMainThreadSerialEventTarget();
}

already_AddRefed<nsISerialEventTarget> HttpChannelChild::GetNeckoTarget() {
  nsCOMPtr<nsISerialEventTarget> target;
  {
    MutexAutoLock lock(mEventTargetMutex);
    target = mNeckoTarget;
  }

  if (!target) {
    target = GetMainThreadSerialEventTarget();
  }
  return target.forget();
}

already_AddRefed<nsIEventTarget> HttpChannelChild::GetODATarget() {
  nsCOMPtr<nsIEventTarget> target;
  {
    MutexAutoLock lock(mEventTargetMutex);
    if (mODATarget) {
      target = mODATarget;
    } else {
      target = mNeckoTarget;
    }
  }

  if (!target) {
    target = GetMainThreadSerialEventTarget();
  }
  return target.forget();
}

nsresult HttpChannelChild::ContinueAsyncOpen() {
  nsresult rv;
  //
  // Send request to the chrome process...
  //

  mozilla::dom::BrowserChild* browserChild = nullptr;
  nsCOMPtr<nsIBrowserChild> iBrowserChild;
  GetCallback(iBrowserChild);
  if (iBrowserChild) {
    browserChild =
        static_cast<mozilla::dom::BrowserChild*>(iBrowserChild.get());
  }

  // This id identifies the inner window's top-level document,
  // which changes on every new load or navigation.
  uint64_t contentWindowId = 0;
  TimeStamp navigationStartTimeStamp;
  if (browserChild) {
    MOZ_ASSERT(browserChild->WebNavigation());
    if (RefPtr<Document> document = browserChild->GetTopLevelDocument()) {
      contentWindowId = document->InnerWindowID();
      nsDOMNavigationTiming* navigationTiming = document->GetNavigationTiming();
      if (navigationTiming) {
        navigationStartTimeStamp =
            navigationTiming->GetNavigationStartTimeStamp();
      }
    }
    if (BrowsingContext* bc = browserChild->GetBrowsingContext()) {
      mBrowserId = bc->BrowserId();
    }
  }
  SetTopLevelContentWindowId(contentWindowId);

  if (browserChild && !browserChild->IPCOpen()) {
    return NS_ERROR_FAILURE;
  }

  ContentChild* cc =
      mozilla::ipc::ActorCast<ContentChild>(gNeckoChild->Manager());
  if (cc->IsShuttingDown()) {
    return NS_ERROR_FAILURE;
  }

  // Defensively ensure the process is marked as untrusted.
  ContentChild::MaybeBecomeUntrusted();

  // add ourselves to the load group.
  if (mLoadGroup) {
    mLoadGroup->AddRequest(this, nullptr);
  }

  HttpChannelOpenArgs openArgs;
  // No access to HttpChannelOpenArgs members, but they each have a
  // function with the struct name that returns a ref.
  openArgs.uri() = mURI;
  openArgs.original() = mOriginalURI;
  openArgs.doc() = mDocumentURI;
  if (mAPIRedirectTo) {
    openArgs.apiRedirectTo() = mAPIRedirectTo->first();
  }
  openArgs.loadFlags() = mLoadFlags;
  openArgs.requestHeaders() = mClientSetRequestHeaders;
  mRequestHead.Method(openArgs.requestMethod());
  openArgs.preferredAlternativeTypes() = mPreferredCachedAltDataTypes.Clone();
  openArgs.referrerInfo() = mReferrerInfo;

  if (mUploadStream) {
    MOZ_ALWAYS_TRUE(SerializeIPCStream(do_AddRef(mUploadStream),
                                       openArgs.uploadStream(),
                                       /* aAllowLazy */ false));
  }

  Maybe<CorsPreflightArgs> optionalCorsPreflightArgs;
  GetClientSetCorsPreflightParameters(optionalCorsPreflightArgs);

  // NB: This call forces us to cache mTopWindowURI if we haven't already.
  nsCOMPtr<nsIURI> uri;
  GetTopWindowURI(mURI, getter_AddRefs(uri));

  openArgs.topWindowURI() = mTopWindowURI;

  openArgs.preflightArgs() = optionalCorsPreflightArgs;

  openArgs.priority() = mPriority;
  openArgs.classOfService() = mClassOfService;
  openArgs.redirectionLimit() = mRedirectionLimit;
  openArgs.allowSTS() = LoadAllowSTS();
  openArgs.thirdPartyFlags() = LoadThirdPartyFlags();
  openArgs.resumeAt() = mSendResumeAt;
  openArgs.startPos() = mStartPos;
  openArgs.entityID() = mEntityID;
  openArgs.allowSpdy() = LoadAllowSpdy();
  openArgs.allowHttp3() = LoadAllowHttp3();
  openArgs.allowAltSvc() = LoadAllowAltSvc();
  openArgs.beConservative() = LoadBeConservative();
  openArgs.bypassProxy() = BypassProxy();
  openArgs.tlsFlags() = mTlsFlags;
  openArgs.initialRwin() = mInitialRwin;

  openArgs.cacheKey() = mCacheKey;

  openArgs.blockAuthPrompt() = LoadBlockAuthPrompt();

  openArgs.allowStaleCacheContent() = LoadAllowStaleCacheContent();
  openArgs.preferCacheLoadOverBypass() = LoadPreferCacheLoadOverBypass();

  openArgs.contentTypeHint() = mContentTypeHint;

  rv = mozilla::ipc::LoadInfoToLoadInfoArgs(mLoadInfo, &openArgs.loadInfo());
  NS_ENSURE_SUCCESS(rv, rv);

  EnsureRequestContextID();
  openArgs.requestContextID() = mRequestContextID;

  openArgs.requestMode() = mRequestMode;
  openArgs.redirectMode() = mRedirectMode;

  openArgs.channelId() = mChannelId;

  openArgs.contentWindowId() = contentWindowId;
  openArgs.browserId() = mBrowserId;

  LOG(("HttpChannelChild::ContinueAsyncOpen this=%p gid=%" PRIu64
       " browser id=%" PRIx64,
       this, mChannelId, mBrowserId));

  openArgs.launchServiceWorkerStart() = mLaunchServiceWorkerStart;
  openArgs.launchServiceWorkerEnd() = mLaunchServiceWorkerEnd;
  openArgs.dispatchFetchEventStart() = mDispatchFetchEventStart;
  openArgs.dispatchFetchEventEnd() = mDispatchFetchEventEnd;
  openArgs.handleFetchEventStart() = mHandleFetchEventStart;
  openArgs.handleFetchEventEnd() = mHandleFetchEventEnd;

  openArgs.forceMainDocumentChannel() = LoadForceMainDocumentChannel();

  openArgs.navigationStartTimeStamp() = navigationStartTimeStamp;
  openArgs.earlyHintPreloaderId() = mEarlyHintPreloaderId;

  openArgs.classicScriptHintCharset() = mClassicScriptHintCharset;

  openArgs.isUserAgentHeaderModified() = LoadIsUserAgentHeaderModified();
  openArgs.initiatorType() = mInitiatorType;

  RefPtr<Document> doc;
  mLoadInfo->GetLoadingDocument(getter_AddRefs(doc));

  if (doc) {
    nsAutoString documentCharacterSet;
    doc->GetCharacterSet(documentCharacterSet);
    openArgs.documentCharacterSet() = documentCharacterSet;
  }

  // This must happen before the constructor message is sent. Otherwise messages
  // from the parent could arrive quickly and be delivered to the wrong event
  // target.
  SetEventTarget();

  if (!gNeckoChild->SendPHttpChannelConstructor(
          this, browserChild, IPC::SerializedLoadContext(this), openArgs)) {
    return NS_ERROR_FAILURE;
  }

  {
    MutexAutoLock lock(mBgChildMutex);

    MOZ_RELEASE_ASSERT(gSocketTransportService);

    // Service worker might use the same HttpChannelChild to do async open
    // twice. Need to disconnect with previous background channel before
    // creating the new one, to prevent receiving further notification
    // from it.
    if (mBgChild) {
      RefPtr<HttpBackgroundChannelChild> prevBgChild = std::move(mBgChild);
      gSocketTransportService->Dispatch(
          NewRunnableMethod("HttpBackgroundChannelChild::OnChannelClosed",
                            prevBgChild,
                            &HttpBackgroundChannelChild::OnChannelClosed),
          NS_DISPATCH_NORMAL);
    }

    MOZ_ASSERT(!mBgInitFailCallback);

    mBgInitFailCallback = NewRunnableMethod<nsresult>(
        "HttpChannelChild::FailedAsyncOpen", this,
        &HttpChannelChild::FailedAsyncOpen, NS_ERROR_FAILURE);

    RefPtr<HttpBackgroundChannelChild> bgChild =
        new HttpBackgroundChannelChild();

    RefPtr<HttpChannelChild> self = this;
    nsresult rv = gSocketTransportService->Dispatch(
        NewRunnableMethod<RefPtr<HttpChannelChild>>(
            "HttpBackgroundChannelChild::Init", bgChild,
            &HttpBackgroundChannelChild::Init, self),
        NS_DISPATCH_NORMAL);

    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    mBgChild = std::move(bgChild);
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
    mEverHadBgChildAtAsyncOpen = true;
#endif
  }

  MaybeConnectToSocketProcess();

  return NS_OK;
}

//-----------------------------------------------------------------------------
// HttpChannelChild::nsIHttpChannel
//-----------------------------------------------------------------------------

NS_IMETHODIMP
HttpChannelChild::SetRequestHeader(const nsACString& aHeader,
                                   const nsACString& aValue, bool aMerge) {
  LOG(("HttpChannelChild::SetRequestHeader [this=%p]\n", this));
  nsresult rv = HttpBaseChannel::SetRequestHeader(aHeader, aValue, aMerge);
  if (NS_FAILED(rv)) return rv;

  RequestHeaderTuple* tuple = mClientSetRequestHeaders.AppendElement();
  if (!tuple) return NS_ERROR_OUT_OF_MEMORY;

  // Mark that the User-Agent header has been modified.
  if (nsHttp::ResolveAtom(aHeader) == nsHttp::User_Agent) {
    StoreIsUserAgentHeaderModified(true);
  }

  tuple->mHeader = aHeader;
  tuple->mValue = aValue;
  tuple->mMerge = aMerge;
  tuple->mEmpty = false;
  return NS_OK;
}

NS_IMETHODIMP
HttpChannelChild::SetEmptyRequestHeader(const nsACString& aHeader) {
  LOG(("HttpChannelChild::SetEmptyRequestHeader [this=%p]\n", this));
  nsresult rv = HttpBaseChannel::SetEmptyRequestHeader(aHeader);
  if (NS_FAILED(rv)) return rv;

  RequestHeaderTuple* tuple = mClientSetRequestHeaders.AppendElement();
  if (!tuple) return NS_ERROR_OUT_OF_MEMORY;

  // Mark that the User-Agent header has been modified.
  if (nsHttp::ResolveAtom(aHeader) == nsHttp::User_Agent) {
    StoreIsUserAgentHeaderModified(true);
  }

  tuple->mHeader = aHeader;
  tuple->mMerge = false;
  tuple->mEmpty = true;
  return NS_OK;
}

NS_IMETHODIMP
HttpChannelChild::RedirectTo(nsIURI* newURI) {
  // disabled until/unless addons run in child or something else needs this
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HttpChannelChild::TransparentRedirectTo(nsIURI* newURI) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HttpChannelChild::UpgradeToSecure() {
  // disabled until/unless addons run in child or something else needs this
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HttpChannelChild::GetProtocolVersion(nsACString& aProtocolVersion) {
  aProtocolVersion = mProtocolVersion;
  return NS_OK;
}

//-----------------------------------------------------------------------------
// HttpChannelChild::nsIHttpChannelInternal
//-----------------------------------------------------------------------------

NS_IMETHODIMP
HttpChannelChild::GetIsAuthChannel(bool* aIsAuthChannel) { DROP_DEAD(); }

//-----------------------------------------------------------------------------
// HttpChannelChild::nsICacheInfoChannel
//-----------------------------------------------------------------------------

NS_IMETHODIMP
HttpChannelChild::GetCacheTokenFetchCount(uint32_t* _retval) {
  NS_ENSURE_ARG_POINTER(_retval);
  MOZ_ASSERT(NS_IsMainThread());

  if (!mCacheEntryAvailable && !mAltDataCacheEntryAvailable) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  *_retval = mCacheFetchCount;
  return NS_OK;
}

NS_IMETHODIMP
HttpChannelChild::GetCacheTokenExpirationTime(uint32_t* _retval) {
  NS_ENSURE_ARG_POINTER(_retval);
  MOZ_ASSERT(NS_IsMainThread());

  if (!mCacheEntryAvailable) return NS_ERROR_NOT_AVAILABLE;

  *_retval = mCacheExpirationTime;
  return NS_OK;
}

NS_IMETHODIMP
HttpChannelChild::IsFromCache(bool* value) {
  if (!LoadIsPending()) return NS_ERROR_NOT_AVAILABLE;

  *value = mIsFromCache;
  return NS_OK;
}

NS_IMETHODIMP
HttpChannelChild::HasCacheEntry(bool* value) {
  *value = mCacheEntryAvailable;
  return NS_OK;
}

NS_IMETHODIMP
HttpChannelChild::GetCacheEntryId(uint64_t* aCacheEntryId) {
  if (!mCacheEntryAvailable) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  *aCacheEntryId = mCacheEntryId;
  return NS_OK;
}

NS_IMETHODIMP
HttpChannelChild::GetCacheKey(uint32_t* cacheKey) {
  MOZ_ASSERT(NS_IsMainThread());

  *cacheKey = mCacheKey;
  return NS_OK;
}
NS_IMETHODIMP
HttpChannelChild::SetCacheKey(uint32_t cacheKey) {
  ENSURE_CALLED_BEFORE_ASYNC_OPEN();

  mCacheKey = cacheKey;
  return NS_OK;
}

NS_IMETHODIMP
HttpChannelChild::SetAllowStaleCacheContent(bool aAllowStaleCacheContent) {
  StoreAllowStaleCacheContent(aAllowStaleCacheContent);
  return NS_OK;
}
NS_IMETHODIMP
HttpChannelChild::GetAllowStaleCacheContent(bool* aAllowStaleCacheContent) {
  NS_ENSURE_ARG(aAllowStaleCacheContent);
  *aAllowStaleCacheContent = LoadAllowStaleCacheContent();
  return NS_OK;
}

NS_IMETHODIMP
HttpChannelChild::SetForceValidateCacheContent(
    bool aForceValidateCacheContent) {
  StoreForceValidateCacheContent(aForceValidateCacheContent);
  return NS_OK;
}
NS_IMETHODIMP
HttpChannelChild::GetForceValidateCacheContent(
    bool* aForceValidateCacheContent) {
  NS_ENSURE_ARG(aForceValidateCacheContent);
  *aForceValidateCacheContent = LoadForceValidateCacheContent();
  return NS_OK;
}

NS_IMETHODIMP
HttpChannelChild::SetPreferCacheLoadOverBypass(
    bool aPreferCacheLoadOverBypass) {
  StorePreferCacheLoadOverBypass(aPreferCacheLoadOverBypass);
  return NS_OK;
}
NS_IMETHODIMP
HttpChannelChild::GetPreferCacheLoadOverBypass(
    bool* aPreferCacheLoadOverBypass) {
  NS_ENSURE_ARG(aPreferCacheLoadOverBypass);
  *aPreferCacheLoadOverBypass = LoadPreferCacheLoadOverBypass();
  return NS_OK;
}

NS_IMETHODIMP
HttpChannelChild::PreferAlternativeDataType(
    const nsACString& aType, const nsACString& aContentType,
    PreferredAlternativeDataDeliveryType aDeliverAltData) {
  ENSURE_CALLED_BEFORE_ASYNC_OPEN();

  mPreferredCachedAltDataTypes.AppendElement(PreferredAlternativeDataTypeParams(
      nsCString(aType), nsCString(aContentType), aDeliverAltData));
  return NS_OK;
}

const nsTArray<PreferredAlternativeDataTypeParams>&
HttpChannelChild::PreferredAlternativeDataTypes() {
  return mPreferredCachedAltDataTypes;
}

NS_IMETHODIMP
HttpChannelChild::GetAlternativeDataType(nsACString& aType) {
  // Must be called during or after OnStartRequest
  if (!LoadAfterOnStartRequestBegun()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  aType = mAvailableCachedAltDataType;
  return NS_OK;
}

NS_IMPL_ADDREF(CacheEntryWriteHandleChild)
NS_IMPL_RELEASE(CacheEntryWriteHandleChild)
NS_INTERFACE_MAP_BEGIN(CacheEntryWriteHandleChild)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_INTERFACE_MAP_ENTRY(nsICacheEntryWriteHandle)
NS_INTERFACE_MAP_END

void CacheEntryWriteHandleChild::AddIPDLReference() { AddRef(); }

void CacheEntryWriteHandleChild::ReleaseIPDLReference() { Release(); }

NS_IMETHODIMP
CacheEntryWriteHandleChild::OpenAlternativeOutputStream(
    const nsACString& aType, int64_t aPredictedSize,
    nsIAsyncOutputStream** _retval) {
  MOZ_ASSERT(NS_IsMainThread(), "Main thread only");

  if (!CanSend()) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  if (mozilla::ipc::ActorCast<ContentChild>(gNeckoChild->Manager())
          ->IsShuttingDown()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  RefPtr<AltDataOutputStreamChild> stream = new AltDataOutputStreamChild();
  stream->AddIPDLReference();

  if (!gNeckoChild->SendPAltDataOutputStreamConstructor(
          stream, nsCString(aType), aPredictedSize, Nothing(),
          Some(WrapNotNull(this)))) {
    return NS_ERROR_FAILURE;
  }

  stream.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
HttpChannelChild::GetCacheEntryWriteHandle(nsICacheEntryWriteHandle** _retval) {
  MOZ_ASSERT(NS_IsMainThread(), "Main thread only");

  if (!CanSend()) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  if (mozilla::ipc::ActorCast<ContentChild>(gNeckoChild->Manager())
          ->IsShuttingDown()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsCOMPtr<nsISerialEventTarget> neckoTarget = GetNeckoTarget();
  MOZ_ASSERT(neckoTarget);

  RefPtr<CacheEntryWriteHandleChild> handle = new CacheEntryWriteHandleChild();
  handle->AddIPDLReference();

  if (!gNeckoChild->SendPCacheEntryWriteHandleConstructor(handle,
                                                          WrapNotNull(this))) {
    return NS_ERROR_FAILURE;
  }

  handle.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
HttpChannelChild::OpenAlternativeOutputStream(const nsACString& aType,
                                              int64_t aPredictedSize,
                                              nsIAsyncOutputStream** _retval) {
  MOZ_ASSERT(NS_IsMainThread(), "Main thread only");

  if (!CanSend()) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  if (mozilla::ipc::ActorCast<ContentChild>(gNeckoChild->Manager())
          ->IsShuttingDown()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsCOMPtr<nsISerialEventTarget> neckoTarget = GetNeckoTarget();
  MOZ_ASSERT(neckoTarget);

  RefPtr<AltDataOutputStreamChild> stream = new AltDataOutputStreamChild();
  stream->AddIPDLReference();

  if (!gNeckoChild->SendPAltDataOutputStreamConstructor(
          stream, nsCString(aType), aPredictedSize, Some(WrapNotNull(this)),
          Nothing())) {
    return NS_ERROR_FAILURE;
  }

  stream.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
HttpChannelChild::GetOriginalInputStream(nsIInputStreamReceiver* aReceiver) {
  if (aReceiver == nullptr) {
    return NS_ERROR_INVALID_ARG;
  }

  if (!CanSend()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  mOriginalInputStreamReceiver = aReceiver;
  (void)SendOpenOriginalCacheInputStream();

  return NS_OK;
}

NS_IMETHODIMP
HttpChannelChild::GetAlternativeDataInputStream(nsIInputStream** aInputStream) {
  NS_ENSURE_ARG_POINTER(aInputStream);

  nsCOMPtr<nsIInputStream> is = mAltDataInputStream;
  is.forget(aInputStream);

  return NS_OK;
}

mozilla::ipc::IPCResult HttpChannelChild::RecvOriginalCacheInputStreamAvailable(
    const Maybe<IPCStream>& aStream) {
  nsCOMPtr<nsIInputStream> stream = DeserializeIPCStream(aStream);
  nsCOMPtr<nsIInputStreamReceiver> receiver;
  receiver.swap(mOriginalInputStreamReceiver);
  if (receiver) {
    receiver->OnInputStreamReady(stream);
  }

  return IPC_OK();
}

//-----------------------------------------------------------------------------
// HttpChannelChild::nsIResumableChannel
//-----------------------------------------------------------------------------

NS_IMETHODIMP
HttpChannelChild::ResumeAt(uint64_t startPos, const nsACString& entityID) {
  LOG(("HttpChannelChild::ResumeAt [this=%p]\n", this));
  ENSURE_CALLED_BEFORE_CONNECT();
  mStartPos = startPos;
  mEntityID = entityID;
  mSendResumeAt = true;
  return NS_OK;
}

// GetEntityID is shared in HttpBaseChannel

//-----------------------------------------------------------------------------
// HttpChannelChild::nsISupportsPriority
//-----------------------------------------------------------------------------

NS_IMETHODIMP
HttpChannelChild::SetPriority(int32_t aPriority) {
  LOG(("HttpChannelChild::SetPriority %p p=%d", this, aPriority));
  int16_t newValue = std::clamp<int32_t>(aPriority, INT16_MIN, INT16_MAX);
  if (mPriority == newValue) return NS_OK;
  mPriority = newValue;
  if (RemoteChannelExists()) SendSetPriority(mPriority);
  return NS_OK;
}

//-----------------------------------------------------------------------------
// HttpChannelChild::nsIClassOfService
//-----------------------------------------------------------------------------
NS_IMETHODIMP
HttpChannelChild::SetClassFlags(uint32_t inFlags) {
  if (mClassOfService.Flags() == inFlags) {
    return NS_OK;
  }

  mClassOfService.SetFlags(inFlags);

  LOG(("HttpChannelChild %p ClassOfService flags=%lu inc=%d", this,
       mClassOfService.Flags(), mClassOfService.Incremental()));

  if (RemoteChannelExists()) {
    SendSetClassOfService(mClassOfService);
  }
  return NS_OK;
}

NS_IMETHODIMP
HttpChannelChild::AddClassFlags(uint32_t inFlags) {
  mClassOfService.SetFlags(inFlags | mClassOfService.Flags());

  LOG(("HttpChannelChild %p ClassOfService flags=%lu inc=%d", this,
       mClassOfService.Flags(), mClassOfService.Incremental()));

  if (RemoteChannelExists()) {
    SendSetClassOfService(mClassOfService);
  }
  return NS_OK;
}

NS_IMETHODIMP
HttpChannelChild::ClearClassFlags(uint32_t inFlags) {
  mClassOfService.SetFlags(~inFlags & mClassOfService.Flags());

  LOG(("HttpChannelChild %p ClassOfService=%lu", this,
       mClassOfService.Flags()));

  if (RemoteChannelExists()) {
    SendSetClassOfService(mClassOfService);
  }
  return NS_OK;
}

NS_IMETHODIMP
HttpChannelChild::SetClassOfService(ClassOfService inCos) {
  mClassOfService = inCos;
  LOG(("HttpChannelChild %p ClassOfService flags=%lu inc=%d", this,
       mClassOfService.Flags(), mClassOfService.Incremental()));
  if (RemoteChannelExists()) {
    SendSetClassOfService(mClassOfService);
  }
  return NS_OK;
}
NS_IMETHODIMP
HttpChannelChild::SetIncremental(bool inIncremental) {
  mClassOfService.SetIncremental(inIncremental);
  LOG(("HttpChannelChild %p ClassOfService flags=%lu inc=%d", this,
       mClassOfService.Flags(), mClassOfService.Incremental()));
  if (RemoteChannelExists()) {
    SendSetClassOfService(mClassOfService);
  }
  return NS_OK;
}

//-----------------------------------------------------------------------------
// HttpChannelChild::nsIProxiedChannel
//-----------------------------------------------------------------------------

NS_IMETHODIMP
HttpChannelChild::GetProxyInfo(nsIProxyInfo** aProxyInfo) { DROP_DEAD(); }

NS_IMETHODIMP HttpChannelChild::GetHttpProxyConnectResponseCode(
    int32_t* aResponseCode) {
  DROP_DEAD();
}

NS_IMETHODIMP HttpChannelChild::GetHttpProxyResponseHeader(
    const nsACString& aHeader, nsACString& aValue) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

//-----------------------------------------------------------------------------
// HttpChannelChild::nsIHttpChannelChild
//-----------------------------------------------------------------------------

NS_IMETHODIMP HttpChannelChild::AddCookiesToRequest() {
  HttpBaseChannel::AddCookiesToRequest();
  return NS_OK;
}

NS_IMETHODIMP HttpChannelChild::GetClientSetRequestHeaders(
    RequestHeaderTuples** aRequestHeaders) {
  *aRequestHeaders = &mClientSetRequestHeaders;
  return NS_OK;
}

void HttpChannelChild::GetClientSetCorsPreflightParameters(
    Maybe<CorsPreflightArgs>& aArgs) {
  if (LoadRequireCORSPreflight()) {
    CorsPreflightArgs args;
    args.unsafeHeaders() = mUnsafeHeaders.Clone();
    aArgs.emplace(args);
  } else {
    aArgs = Nothing();
  }
}

NS_IMETHODIMP
HttpChannelChild::RemoveCorsPreflightCacheEntry(
    nsIURI* aURI, nsIPrincipal* aPrincipal,
    const OriginAttributes& aOriginAttributes) {
  PrincipalInfo principalInfo;
  MOZ_ASSERT(aURI, "aURI should not be null");
  nsresult rv = PrincipalToPrincipalInfo(aPrincipal, &principalInfo);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  bool result = false;
  // Be careful to not attempt to send a message to the parent after the
  // actor has been destroyed.
  if (CanSend()) {
    result = SendRemoveCorsPreflightCacheEntry(aURI, principalInfo,
                                               aOriginAttributes);
  }
  return result ? NS_OK : NS_ERROR_FAILURE;
}

//-----------------------------------------------------------------------------
// HttpChannelChild::nsIMuliPartChannel
//-----------------------------------------------------------------------------

NS_IMETHODIMP
HttpChannelChild::GetBaseChannel(nsIChannel** aBaseChannel) {
  if (!mMultiPartID) {
    MOZ_ASSERT(false, "Not a multipart channel");
    return NS_ERROR_NOT_AVAILABLE;
  }
  nsCOMPtr<nsIChannel> channel = this;
  channel.forget(aBaseChannel);
  return NS_OK;
}

NS_IMETHODIMP
HttpChannelChild::GetPartID(uint32_t* aPartID) {
  if (!mMultiPartID) {
    MOZ_ASSERT(false, "Not a multipart channel");
    return NS_ERROR_NOT_AVAILABLE;
  }
  *aPartID = *mMultiPartID;
  return NS_OK;
}

NS_IMETHODIMP
HttpChannelChild::GetIsFirstPart(bool* aIsFirstPart) {
  if (!mMultiPartID) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  *aIsFirstPart = mIsFirstPartOfMultiPart;
  return NS_OK;
}

NS_IMETHODIMP
HttpChannelChild::GetIsLastPart(bool* aIsLastPart) {
  if (!mMultiPartID) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  *aIsLastPart = mIsLastPartOfMultiPart;
  return NS_OK;
}

//-----------------------------------------------------------------------------
// HttpChannelChild::nsIThreadRetargetableRequest
//-----------------------------------------------------------------------------

NS_IMETHODIMP
HttpChannelChild::RetargetDeliveryTo(nsISerialEventTarget* aNewTarget) {
  LOG(("HttpChannelChild::RetargetDeliveryTo [this=%p, aNewTarget=%p]", this,
       aNewTarget));
  MOZ_ASSERT(NS_IsMainThread(), "Should be called on main thread only");
  MOZ_ASSERT(aNewTarget);

  NS_ENSURE_ARG(aNewTarget);
  if (aNewTarget->IsOnCurrentThread()) {
    NS_WARNING("Retargeting delivery to same thread");
    return NS_OK;
  }

  if (mMultiPartID) {
    return NS_ERROR_NO_INTERFACE;
  }

  // Ensure that |mListener| and any subsequent listeners can be retargeted
  // to another thread.
  nsresult rv = NS_OK;
  nsCOMPtr<nsIThreadRetargetableStreamListener> retargetableListener =
      do_QueryInterface(mListener, &rv);
  if (!retargetableListener || NS_FAILED(rv)) {
    NS_WARNING("Listener is not retargetable");
    return NS_ERROR_NO_INTERFACE;
  }

  rv = retargetableListener->CheckListenerChain();
  if (NS_FAILED(rv)) {
    NS_WARNING("Subsequent listeners are not retargetable");
    return rv;
  }

  MutexAutoLock lock(mEventTargetMutex);
  // Don't assert if the target hasn't changed, or if we haven't gotten
  // OnDataAvailable (backed off on this last bit, see bug 1917901)
  if (mODATarget == aNewTarget) {
    // Same target
    return NS_OK;
  } else if (mODATarget) {
    // We already retargetted (valentin: unclear if this should be allowed)
    NS_WARNING("Retargeting delivery when already retargeted");
    return NS_ERROR_ALREADY_INITIALIZED;
  } else if (mGotDataAvailable) {
    // Too late to retarget now.
    return NS_ERROR_FAILURE;
  }

  RetargetDeliveryToImpl(aNewTarget, lock);
  return NS_OK;
}

void HttpChannelChild::RetargetDeliveryToImpl(nsISerialEventTarget* aNewTarget,
                                              MutexAutoLock& aLockRef) {
  aLockRef.AssertOwns(mEventTargetMutex);

  mODATarget = aNewTarget;
}

NS_IMETHODIMP
HttpChannelChild::GetDeliveryTarget(nsISerialEventTarget** aEventTarget) {
  MutexAutoLock lock(mEventTargetMutex);

  nsCOMPtr<nsISerialEventTarget> target = mODATarget;
  if (!mODATarget) {
    target = GetCurrentSerialEventTarget();
  }
  target.forget(aEventTarget);
  return NS_OK;
}

void HttpChannelChild::TrySendDeletingChannel() {
  AUTO_PROFILER_LABEL("HttpChannelChild::TrySendDeletingChannel", NETWORK);
  MOZ_ASSERT(NS_IsMainThread());

  if (!mDeletingChannelSent.compareExchange(false, true)) {
    // SendDeletingChannel is already sent.
    return;
  }

  if (NS_WARN_IF(!CanSend())) {
    // IPC actor is destroyed already, do not send more messages.
    return;
  }

  (void)PHttpChannelChild::SendDeletingChannel();
}

nsresult HttpChannelChild::AsyncCallImpl(
    void (HttpChannelChild::*funcPtr)(),
    nsRunnableMethod<HttpChannelChild>** retval) {
  nsresult rv;

  RefPtr<nsRunnableMethod<HttpChannelChild>> event =
      NewRunnableMethod("net::HttpChannelChild::AsyncCall", this, funcPtr);
  nsCOMPtr<nsISerialEventTarget> neckoTarget = GetNeckoTarget();
  MOZ_ASSERT(neckoTarget);

  rv = neckoTarget->Dispatch(event, NS_DISPATCH_NORMAL);

  if (NS_SUCCEEDED(rv) && retval) {
    *retval = event;
  }

  return rv;
}

nsresult HttpChannelChild::SetReferrerHeader(const nsACString& aReferrer,
                                             bool aRespectBeforeConnect) {
  // Normally this would be ENSURE_CALLED_BEFORE_CONNECT, but since the
  // "connect" is done in the main process, and LoadRequestObserversCalled() is
  // never set in the ChannelChild, before connect basically means before
  // asyncOpen.
  if (aRespectBeforeConnect) {
    ENSURE_CALLED_BEFORE_ASYNC_OPEN();
  }

  // remove old referrer if any
  mClientSetRequestHeaders.RemoveElementsBy(
      [](const auto& header) { return "Referer"_ns.Equals(header.mHeader); });

  return HttpBaseChannel::SetReferrerHeader(aReferrer, aRespectBeforeConnect);
}

void HttpChannelChild::CancelOnMainThread(nsresult aRv,
                                          const nsACString& aReason) {
  LOG(("HttpChannelChild::CancelOnMainThread [this=%p]", this));

  if (NS_IsMainThread()) {
    CancelWithReason(aRv, aReason);
    return;
  }

  mEventQ->Suspend();
  // Cancel is expected to preempt any other channel events, thus we put this
  // event in the front of mEventQ to make sure nsIStreamListener not receiving
  // any ODA/OnStopRequest callbacks.
  nsCString reason(aReason);
  mEventQ->PrependEvent(MakeUnique<NeckoTargetChannelFunctionEvent>(
      this,
      [self = UnsafePtr<HttpChannelChild>(this), aRv,
       reason = std::move(reason)]() { self->CancelWithReason(aRv, reason); }));
  mEventQ->Resume();
}

mozilla::ipc::IPCResult HttpChannelChild::RecvSetPriority(
    const int16_t& aPriority) {
  mPriority = aPriority;
  return IPC_OK();
}

// We don't have a copyable Endpoint and NeckoTargetChannelFunctionEvent takes
// std::function<void()>.  It's not possible to avoid the copy from the type of
// lambda to std::function, so does the capture list. Hence, we're forced to
// use the old-fashioned channel event inheritance.
class AttachStreamFilterEvent : public ChannelEvent {
 public:
  AttachStreamFilterEvent(HttpChannelChild* aChild,
                          already_AddRefed<nsIEventTarget> aTarget,
                          Endpoint<extensions::PStreamFilterParent>&& aEndpoint)
      : mChild(aChild), mTarget(aTarget), mEndpoint(std::move(aEndpoint)) {}

  already_AddRefed<nsIEventTarget> GetEventTarget() override {
    nsCOMPtr<nsIEventTarget> target = mTarget;
    return target.forget();
  }

  void Run() override {
    extensions::StreamFilterParent::Attach(mChild, std::move(mEndpoint));
  }

 private:
  HttpChannelChild* mChild;
  nsCOMPtr<nsIEventTarget> mTarget;
  Endpoint<extensions::PStreamFilterParent> mEndpoint;
};

void HttpChannelChild::RegisterStreamFilter(
    RefPtr<extensions::StreamFilterParent>& aStreamFilter) {
  MOZ_ASSERT(NS_IsMainThread());
  mStreamFilters.AppendElement(aStreamFilter);
}

void HttpChannelChild::ProcessAttachStreamFilter(
    Endpoint<extensions::PStreamFilterParent>&& aEndpoint) {
  LOG(("HttpChannelChild::ProcessAttachStreamFilter [this=%p]\n", this));
  MOZ_ASSERT(OnSocketThread());

  mEventQ->RunOrEnqueue(MakeUnique<AttachStreamFilterEvent>(
      this, GetNeckoTarget(), std::move(aEndpoint)));
}

void HttpChannelChild::OnDetachStreamFilters() {
  LOG(("HttpChannelChild::OnDetachStreamFilters [this=%p]\n", this));
  MOZ_ASSERT(NS_IsMainThread());
  for (auto& StreamFilter : mStreamFilters) {
    StreamFilter->Disconnect("ServiceWorker fallback redirection"_ns);
  }
  mStreamFilters.Clear();
}

void HttpChannelChild::ProcessDetachStreamFilters() {
  LOG(("HttpChannelChild::ProcessDetachStreamFilter [this=%p]\n", this));
  MOZ_ASSERT(OnSocketThread());

  mEventQ->RunOrEnqueue(MakeUnique<NeckoTargetChannelFunctionEvent>(
      this, [self = UnsafePtr<HttpChannelChild>(this)]() {
        self->OnDetachStreamFilters();
      }));
}

void HttpChannelChild::ActorDestroy(ActorDestroyReason aWhy) {
  MOZ_ASSERT(NS_IsMainThread());

#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  mActorDestroyReason.emplace(aWhy);
#endif

  // OnStartRequest might be dropped if IPDL is destroyed abnormally
  // and BackgroundChild might have pending IPC messages.
  // Clean up BackgroundChild at this time to prevent memleak.
  if (aWhy != Deletion) {
    // Make sure all the messages are processed.
    AutoEventEnqueuer ensureSerialDispatch(mEventQ);

    mStatus = NS_ERROR_DOCSHELL_DYING;
    HandleAsyncAbort();

    // Cleanup the background channel before we resume the eventQ so we don't
    // get any other events.
    CleanupBackgroundChannel();

    mIPCActorDeleted = true;
    mCanceled = true;
  }

  // Any queued OnStartRequest/OnDataAvailable/OnStopRequest events are safe to
  // discard: in the non-Deletion path HandleAsyncAbort() has already called
  // DoNotifyListener() to deliver OnStart+OnStop to the consumer, so the
  // queued events would be no-ops (guarded by mIPCActorDeleted/mCanceled).
  // In the Deletion path the channel completed normally so the queue is empty.
  mEventQ->DiscardQueuedEvents();
}

mozilla::ipc::IPCResult HttpChannelChild::RecvLogBlockedCORSRequest(
    const nsAString& aMessage, const nsACString& aCategory,
    const bool& aIsWarning) {
  (void)LogBlockedCORSRequest(aMessage, aCategory, aIsWarning);
  return IPC_OK();
}

NS_IMETHODIMP
HttpChannelChild::LogBlockedCORSRequest(const nsAString& aMessage,
                                        const nsACString& aCategory,
                                        bool aIsWarning) {
  uint64_t innerWindowID = mLoadInfo->GetInnerWindowID();
  bool privateBrowsing = mLoadInfo->GetOriginAttributes().IsPrivateBrowsing();
  bool fromChromeContext =
      mLoadInfo->TriggeringPrincipal()->IsSystemPrincipal();
  nsCORSListenerProxy::LogBlockedCORSRequest(innerWindowID, privateBrowsing,
                                             fromChromeContext, aMessage,
                                             aCategory, aIsWarning);
  return NS_OK;
}

mozilla::ipc::IPCResult HttpChannelChild::RecvLogMimeTypeMismatch(
    const nsACString& aMessageName, const bool& aWarning, const nsAString& aURL,
    const nsAString& aContentType) {
  (void)LogMimeTypeMismatch(aMessageName, aWarning, aURL, aContentType);
  return IPC_OK();
}

NS_IMETHODIMP
HttpChannelChild::LogMimeTypeMismatch(const nsACString& aMessageName,
                                      bool aWarning, const nsAString& aURL,
                                      const nsAString& aContentType) {
  RefPtr<Document> doc;
  mLoadInfo->GetLoadingDocument(getter_AddRefs(doc));

  AutoTArray<nsString, 2> params;
  params.AppendElement(aURL);
  params.AppendElement(aContentType);
  nsContentUtils::ReportToConsole(
      aWarning ? nsIScriptError::warningFlag : nsIScriptError::errorFlag,
      "MIMEMISMATCH"_ns, doc, PropertiesFile::SECURITY_PROPERTIES,
      nsCString(aMessageName).get(), params);
  return NS_OK;
}

nsresult HttpChannelChild::MaybeLogCOEPError(nsresult aStatus) {
  if (aStatus == NS_ERROR_DOM_CORP_FAILED) {
    RefPtr<Document> doc;
    mLoadInfo->GetLoadingDocument(getter_AddRefs(doc));

    nsAutoCString url;
    mURI->GetSpec(url);

    AutoTArray<nsString, 2> params;
    params.AppendElement(NS_ConvertUTF8toUTF16(url));
    // The MDN URL intentionally ends with a # so the webconsole linkification
    // doesn't ignore the final ) of the URL
    params.AppendElement(
        u"https://developer.mozilla.org/docs/Web/HTTP/Cross-Origin_Resource_Policy_(CORP)#"_ns);
    nsContentUtils::ReportToConsole(nsIScriptError::errorFlag, "COEP"_ns, doc,
                                    PropertiesFile::NECKO_PROPERTIES,
                                    "CORPBlocked", params);
  }

  return NS_OK;
}

nsresult HttpChannelChild::CrossProcessRedirectFinished(nsresult aStatus) {
  if (!CanSend()) {
    return NS_BINDING_FAILED;
  }

  if (!mCanceled && NS_SUCCEEDED(mStatus)) {
    mStatus = aStatus;
  }

  return mStatus;
}

void HttpChannelChild::DoDiagnosticAssertWhenOnStopNotCalledOnDestroy() {
#ifdef MOZ_DIAGNOSTIC_ASSERT_ENABLED
  mDoDiagnosticAssertWhenOnStopNotCalledOnDestroy = true;
#endif
}

void HttpChannelChild::MaybeConnectToSocketProcess() {
  if (!nsIOService::UseSocketProcess()) {
    return;
  }

  if (!StaticPrefs::network_send_ODA_to_content_directly()) {
    return;
  }

  RefPtr<HttpBackgroundChannelChild> bgChild;
  {
    MutexAutoLock lock(mBgChildMutex);
    bgChild = mBgChild;
  }
  SocketProcessBridgeChild::GetSocketProcessBridge()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [bgChild, channelId = ChannelId()](
          const RefPtr<SocketProcessBridgeChild>& aBridge) {
        Endpoint<PBackgroundDataBridgeParent> parentEndpoint;
        Endpoint<PBackgroundDataBridgeChild> childEndpoint;
        PBackgroundDataBridge::CreateEndpoints(&parentEndpoint, &childEndpoint);
        aBridge->SendInitBackgroundDataBridge(std::move(parentEndpoint),
                                              channelId);

        gSocketTransportService->Dispatch(
            NS_NewRunnableFunction(
                "HttpBackgroundChannelChild::CreateDataBridge",
                [bgChild, endpoint = std::move(childEndpoint)]() mutable {
                  bgChild->CreateDataBridge(std::move(endpoint));
                }),
            NS_DISPATCH_NORMAL);
      },
      []() { NS_WARNING("Failed to create SocketProcessBridgeChild"); });
}

NS_IMETHODIMP
HttpChannelChild::SetEarlyHintObserver(nsIEarlyHintObserver* aObserver) {
  return NS_OK;
}

NS_IMETHODIMP HttpChannelChild::SetWebTransportSessionEventListener(
    WebTransportSessionEventListener* aListener) {
  return NS_OK;
}

void HttpChannelChild::ExplicitSetUploadStreamLength(
    uint64_t aContentLength, bool aSetContentLengthHeader) {
  // SetRequestHeader propagates headers to chrome if HttpChannelChild
  MOZ_ASSERT(!LoadWasOpened());
  HttpBaseChannel::ExplicitSetUploadStreamLength(aContentLength,
                                                 aSetContentLengthHeader);
}

NS_IMETHODIMP
HttpChannelChild::GetCacheDisposition(
    nsICacheInfoChannel::CacheDisposition* aDisposition) {
  if (!aDisposition) {
    return NS_ERROR_INVALID_ARG;
  }
  *aDisposition = mCacheDisposition;
  return NS_OK;
}

}  // namespace mozilla::net
