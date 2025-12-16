/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:set ts=4 sw=4 sts=4 et cin: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "HttpLog.h"

#include "HttpTransactionParent.h"

#include "HttpTrafficAnalyzer.h"
#include "mozilla/ipc/IPCStreamUtils.h"
#include "mozilla/net/ChannelEventQueue.h"
#include "mozilla/net/InputChannelThrottleQueueParent.h"
#include "mozilla/net/SocketProcessParent.h"
#include "nsHttpHandler.h"
#include "nsIThreadRetargetableStreamListener.h"
#include "nsITransportSecurityInfo.h"
#include "nsNetUtil.h"
#include "nsQueryObject.h"
#include "nsSerializationHelper.h"
#include "nsStreamUtils.h"
#include "nsStringStream.h"
#include "nsIRequestContext.h"

namespace mozilla::net {

NS_IMPL_ADDREF(HttpTransactionParent)
NS_INTERFACE_MAP_BEGIN(HttpTransactionParent)
  NS_INTERFACE_MAP_ENTRY(nsIRequest)
  NS_INTERFACE_MAP_ENTRY(nsIThreadRetargetableRequest)
  NS_INTERFACE_MAP_ENTRY_CONCRETE(HttpTransactionParent)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIRequest)
NS_INTERFACE_MAP_END

NS_IMETHODIMP_(MozExternalRefCountType) HttpTransactionParent::Release(void) {
  MOZ_ASSERT(int32_t(mRefCnt) > 0, "dup release");
  nsrefcnt count = --mRefCnt;
  NS_LOG_RELEASE(this, count, "HttpTransactionParent");
  if (count == 0) {
    mRefCnt = 1; /* stabilize */
    delete (this);
    return 0;
  }

  // When ref count goes down to 1 (held internally by IPDL), it means that
  // we are done with this transaction. We should send a delete message
  // to delete the transaction child in socket process.
  if (count == 1 && CanSend()) {
    if (!NS_IsMainThread()) {
      RefPtr<HttpTransactionParent> self = this;
      MOZ_ALWAYS_SUCCEEDS(NS_DispatchToMainThread(
          NS_NewRunnableFunction("HttpTransactionParent::Release", [self]() {
            (void)self->Send__delete__(self);
            // Make sure we can not send IPC after Send__delete__().
            MOZ_ASSERT(!self->CanSend());
          })));
    } else {
      (void)Send__delete__(this);
    }
    return 1;
  }
  return count;
}

//-----------------------------------------------------------------------------
// HttpTransactionParent <public>
//-----------------------------------------------------------------------------

HttpTransactionParent::HttpTransactionParent(bool aIsDocumentLoad)
    : mIsDocumentLoad(aIsDocumentLoad) {
  LOG(("Creating HttpTransactionParent @%p\n", this));
  mEventQ = new ChannelEventQueue(static_cast<nsIRequest*>(this));
}

HttpTransactionParent::~HttpTransactionParent() {
  LOG(("Destroying HttpTransactionParent @%p\n", this));
  mEventQ->NotifyReleasingOwner();
}

//-----------------------------------------------------------------------------
// HttpTransactionParent <nsAHttpTransactionShell>
//-----------------------------------------------------------------------------

// Let socket process init the *real* nsHttpTransaction.
nsresult HttpTransactionParent::Init(
    uint32_t caps, nsHttpConnectionInfo* cinfo, nsHttpRequestHead* requestHead,
    nsIInputStream* requestBody, uint64_t requestContentLength,
    bool requestBodyHasHeaders, nsIEventTarget* target,
    nsIInterfaceRequestor* callbacks, nsITransportEventSink* eventsink,
    uint64_t browserId, HttpTrafficCategory trafficCategory,
    nsIRequestContext* requestContext, ClassOfService classOfService,
    uint32_t initialRwin, bool responseTimeoutEnabled, uint64_t channelId,
    TransactionObserverFunc&& transactionObserver,
    nsILoadInfo::IPAddressSpace aParentIpAddressSpace,
    const LNAPerms& aLnaPermissionStatus) {
  LOG(("HttpTransactionParent::Init [this=%p caps=%x]\n", this, caps));

  if (!CanSend()) {
    return NS_ERROR_FAILURE;
  }

  mEventsink = eventsink;
  mTargetThread = GetCurrentSerialEventTarget();
  mChannelId = channelId;
  mTransactionObserver = std::move(transactionObserver);
  mCaps = caps;
  mConnInfo = cinfo->Clone();
  mIsHttp3Used = cinfo->IsHttp3();

  HttpConnectionInfoCloneArgs infoArgs;
  nsHttpConnectionInfo::SerializeHttpConnectionInfo(cinfo, infoArgs);

  Maybe<mozilla::ipc::IPCStream> ipcStream;
  if (!mozilla::ipc::SerializeIPCStream(do_AddRef(requestBody), ipcStream,
                                        /* aAllowLazy */ false)) {
    return NS_ERROR_FAILURE;
  }

  uint64_t requestContextID = requestContext ? requestContext->GetID() : 0;

  nsCOMPtr<nsIThrottledInputChannel> throttled = do_QueryInterface(mEventsink);
  Maybe<NotNull<PInputChannelThrottleQueueParent*>> throttleQueue;
  if (throttled) {
    nsCOMPtr<nsIInputChannelThrottleQueue> queue;
    nsresult rv = throttled->GetThrottleQueue(getter_AddRefs(queue));
    // In case of failure, just carry on without throttling.
    if (NS_SUCCEEDED(rv) && queue) {
      LOG1(("HttpTransactionParent::Init %p using throttle queue %p\n", this,
            queue.get()));
      RefPtr<InputChannelThrottleQueueParent> tqParent = do_QueryObject(queue);
      MOZ_ASSERT(tqParent);
      throttleQueue.emplace(WrapNotNull(tqParent.get()));
    }
  }

  // TODO: Figure out if we have to implement nsIThreadRetargetableRequest in
  // bug 1544378.
  if (!SendInit(caps, infoArgs, *requestHead, ipcStream, requestContentLength,
                requestBodyHasHeaders, browserId,
                static_cast<uint8_t>(trafficCategory), requestContextID,
                classOfService, initialRwin, responseTimeoutEnabled, mChannelId,
                !!mTransactionObserver, throttleQueue, mIsDocumentLoad,
                aParentIpAddressSpace, aLnaPermissionStatus, mRedirectStart,
                mRedirectEnd)) {
    return NS_ERROR_FAILURE;
  }

  nsCString reqHeaderBuf = nsHttp::ConvertRequestHeadToString(
      *requestHead, !!requestBody, requestBodyHasHeaders,
      cinfo->UsingConnect());
  requestContentLength += reqHeaderBuf.Length();

  mRequestSize = InScriptableRange(requestContentLength)
                     ? static_cast<int64_t>(requestContentLength)
                     : -1;

  return NS_OK;
}

nsresult HttpTransactionParent::AsyncRead(nsIStreamListener* listener,
                                          nsIRequest** pump) {
  MOZ_ASSERT(pump);

  *pump = do_AddRef(this).take();
  mChannel = listener;
  return NS_OK;
}

UniquePtr<nsHttpResponseHead>
HttpTransactionParent::TakeResponseHeadAndConnInfo(
    nsHttpConnectionInfo** aOut) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mResponseHeadTaken, "TakeResponseHead called 2x");

  if (aOut) {
    RefPtr<nsHttpConnectionInfo> connInfo = mConnInfo;
    connInfo.forget(aOut);
  }

  mResponseHeadTaken = true;
  return std::move(mResponseHead);
}

UniquePtr<nsHttpHeaderArray> HttpTransactionParent::TakeResponseTrailers() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mResponseTrailersTaken, "TakeResponseTrailers called 2x");

  mResponseTrailersTaken = true;
  return std::move(mResponseTrailers);
}

void HttpTransactionParent::SetSniffedTypeToChannel(
    nsInputStreamPump::PeekSegmentFun aCallTypeSniffers, nsIChannel* aChannel) {
  if (!mDataForSniffer.IsEmpty()) {
    aCallTypeSniffers(aChannel, mDataForSniffer.Elements(),
                      mDataForSniffer.Length());
  }
}

NS_IMETHODIMP
HttpTransactionParent::GetDeliveryTarget(nsISerialEventTarget** aEventTarget) {
  MutexAutoLock lock(mEventTargetMutex);

  nsCOMPtr<nsISerialEventTarget> target = mODATarget;
  if (!mODATarget) {
    target = mTargetThread;
  }
  target.forget(aEventTarget);
  return NS_OK;
}

already_AddRefed<nsISerialEventTarget> HttpTransactionParent::GetODATarget() {
  nsCOMPtr<nsISerialEventTarget> target;
  {
    MutexAutoLock lock(mEventTargetMutex);
    target = mODATarget ? mODATarget : mTargetThread;
  }

  if (!target) {
    target = GetMainThreadSerialEventTarget();
  }
  return target.forget();
}

NS_IMETHODIMP HttpTransactionParent::RetargetDeliveryTo(
    nsISerialEventTarget* aEventTarget) {
  LOG(("HttpTransactionParent::RetargetDeliveryTo [this=%p, aTarget=%p]", this,
       aEventTarget));

  MOZ_ASSERT(NS_IsMainThread(), "Should be called on main thread only");
  MOZ_ASSERT(!mODATarget);
  NS_ENSURE_ARG(aEventTarget);

  if (aEventTarget->IsOnCurrentThread()) {
    NS_WARNING("Retargeting delivery to same thread");
    return NS_OK;
  }

  nsresult rv = NS_OK;
  nsCOMPtr<nsIThreadRetargetableStreamListener> retargetableListener =
      do_QueryInterface(mChannel, &rv);
  if (!retargetableListener || NS_FAILED(rv)) {
    NS_WARNING("Listener is not retargetable");
    return NS_ERROR_NO_INTERFACE;
  }

  rv = retargetableListener->CheckListenerChain();
  if (NS_FAILED(rv)) {
    NS_WARNING("Subsequent listeners are not retargetable");
    return rv;
  }

  {
    MutexAutoLock lock(mEventTargetMutex);
    mODATarget = aEventTarget;
  }

  return NS_OK;
}

void HttpTransactionParent::SetDNSWasRefreshed() {
  MOZ_ASSERT(NS_IsMainThread(), "SetDNSWasRefreshed on main thread only!");
  (void)SendSetDNSWasRefreshed();
}

void HttpTransactionParent::GetNetworkAddresses(
    NetAddr& self, NetAddr& peer, bool& aResolvedByTRR,
    nsIRequest::TRRMode& aEffectiveTRRMode, TRRSkippedReason& aSkipReason,
    bool& aEchConfigUsed) {
  self = mSelfAddr;
  peer = mPeerAddr;
  aResolvedByTRR = mResolvedByTRR;
  aEffectiveTRRMode = mEffectiveTRRMode;
  aSkipReason = mTRRSkipReason;
  aEchConfigUsed = mEchConfigUsed;
}

nsILoadInfo::IPAddressSpace HttpTransactionParent::GetTargetIPAddressSpace() {
  return mTargetIPAddressSpace;
}

bool HttpTransactionParent::HasStickyConnection() const {
  return mCaps & NS_HTTP_STICKY_CONNECTION;
}

mozilla::TimeStamp HttpTransactionParent::GetDomainLookupStart() {
  return mTimings.domainLookupStart;
}

mozilla::TimeStamp HttpTransactionParent::GetDomainLookupEnd() {
  return mTimings.domainLookupEnd;
}

mozilla::TimeStamp HttpTransactionParent::GetConnectStart() {
  return mTimings.connectStart;
}

mozilla::TimeStamp HttpTransactionParent::GetTcpConnectEnd() {
  return mTimings.tcpConnectEnd;
}

mozilla::TimeStamp HttpTransactionParent::GetSecureConnectionStart() {
  return mTimings.secureConnectionStart;
}

mozilla::TimeStamp HttpTransactionParent::GetConnectEnd() {
  return mTimings.connectEnd;
}

mozilla::TimeStamp HttpTransactionParent::GetRequestStart() {
  return mTimings.requestStart;
}

mozilla::TimeStamp HttpTransactionParent::GetResponseStart() {
  return mTimings.responseStart;
}

mozilla::TimeStamp HttpTransactionParent::GetFirstInterimResponseStart() {
  return mTimings.firstInterimResponseStart;
}

mozilla::TimeStamp HttpTransactionParent::GetFinalResponseHeadersStart() {
  return mTimings.finalResponseHeadersStart;
}

mozilla::TimeStamp HttpTransactionParent::GetResponseEnd() {
  return mTimings.responseEnd;
}

TimingStruct HttpTransactionParent::Timings() { return mTimings; }

bool HttpTransactionParent::ResponseIsComplete() { return mResponseIsComplete; }

int64_t HttpTransactionParent::GetTransferSize() { return mTransferSize; }

int64_t HttpTransactionParent::GetRequestSize() { return mRequestSize; }

bool HttpTransactionParent::IsHttp3Used() { return mIsHttp3Used; }

bool HttpTransactionParent::DataSentToChildProcess() {
  return mDataSentToChildProcess;
}

already_AddRefed<nsITransportSecurityInfo>
HttpTransactionParent::SecurityInfo() {
  return do_AddRef(mSecurityInfo);
}

bool HttpTransactionParent::ProxyConnectFailed() { return mProxyConnectFailed; }

bool HttpTransactionParent::TakeRestartedState() {
  bool result = mRestarted;
  mRestarted = false;
  return result;
}

uint32_t HttpTransactionParent::HTTPSSVCReceivedStage() {
  return mHTTPSSVCReceivedStage;
}

void HttpTransactionParent::DontReuseConnection() {
  MOZ_ASSERT(NS_IsMainThread());
  (void)SendDontReuseConnection();
}

void HttpTransactionParent::SetH2WSConnRefTaken() {
  MOZ_ASSERT(NS_IsMainThread());
  (void)SendSetH2WSConnRefTaken();
}

void HttpTransactionParent::SetSecurityCallbacks(
    nsIInterfaceRequestor* aCallbacks) {
  // TODO: we might don't need to implement this.
  // Will figure out in bug 1512479.
}

void HttpTransactionParent::SetDomainLookupStart(mozilla::TimeStamp timeStamp,
                                                 bool onlyIfNull) {
  mDomainLookupStart = timeStamp;
  mTimings.domainLookupStart = mDomainLookupStart;
}
void HttpTransactionParent::SetDomainLookupEnd(mozilla::TimeStamp timeStamp,
                                               bool onlyIfNull) {
  mDomainLookupEnd = timeStamp;
  mTimings.domainLookupEnd = mDomainLookupEnd;
}

nsHttpTransaction* HttpTransactionParent::AsHttpTransaction() {
  return nullptr;
}

HttpTransactionParent* HttpTransactionParent::AsHttpTransactionParent() {
  return this;
}

int32_t HttpTransactionParent::GetProxyConnectResponseCode() {
  return mProxyConnectResponseCode;
}

bool HttpTransactionParent::Http2Disabled() const {
  return mCaps & NS_HTTP_DISALLOW_SPDY;
}

bool HttpTransactionParent::Http3Disabled() const {
  return mCaps & NS_HTTP_DISALLOW_HTTP3;
}

already_AddRefed<nsHttpConnectionInfo> HttpTransactionParent::GetConnInfo()
    const {
  RefPtr<nsHttpConnectionInfo> connInfo = mConnInfo->Clone();
  return connInfo.forget();
}

already_AddRefed<nsIEventTarget> HttpTransactionParent::GetNeckoTarget() {
  nsCOMPtr<nsIEventTarget> target = GetMainThreadSerialEventTarget();
  return target.forget();
}

mozilla::ipc::IPCResult HttpTransactionParent::RecvOnStartRequest(
    const nsresult& aStatus, Maybe<nsHttpResponseHead>&& aResponseHead,
    nsITransportSecurityInfo* aSecurityInfo, const bool& aProxyConnectFailed,
    const TimingStructArgs& aTimings, const int32_t& aProxyConnectResponseCode,
    nsTArray<uint8_t>&& aDataForSniffer, const Maybe<nsCString>& aAltSvcUsed,
    const bool& aDataToChildProcess, const bool& aRestarted,
    const uint32_t& aHTTPSSVCReceivedStage, const bool& aSupportsHttp3,
    const nsIRequest::TRRMode& aMode, const TRRSkippedReason& aTrrSkipReason,
    const uint32_t& aCaps, const TimeStamp& aOnStartRequestStartTime,
    const HttpConnectionInfoCloneArgs& aArgs,
    const nsILoadInfo::IPAddressSpace& aTargetIPAddressSpace) {
  RefPtr<nsHttpConnectionInfo> cinfo =
      nsHttpConnectionInfo::DeserializeHttpConnectionInfoCloneArgs(aArgs);
  mEventQ->RunOrEnqueue(new NeckoTargetChannelFunctionEvent(
      this,
      [self = UnsafePtr<HttpTransactionParent>(this), aStatus,
       aResponseHead = std::move(aResponseHead),
       securityInfo = nsCOMPtr{aSecurityInfo}, aProxyConnectFailed, aTimings,
       aProxyConnectResponseCode,
       aDataForSniffer = CopyableTArray{std::move(aDataForSniffer)},
       aAltSvcUsed, aDataToChildProcess, aRestarted, aHTTPSSVCReceivedStage,
       aSupportsHttp3, aMode, aTrrSkipReason, aCaps, aOnStartRequestStartTime,
       aTargetIPAddressSpace, cinfo{std::move(cinfo)}]() mutable {
        self->DoOnStartRequest(
            aStatus, std::move(aResponseHead), securityInfo,
            aProxyConnectFailed, aTimings, aProxyConnectResponseCode,
            std::move(aDataForSniffer), aAltSvcUsed, aDataToChildProcess,
            aRestarted, aHTTPSSVCReceivedStage, aSupportsHttp3, aMode,
            aTrrSkipReason, aCaps, aOnStartRequestStartTime, cinfo,
            aTargetIPAddressSpace);
      }));
  return IPC_OK();
}

static void TimingStructArgsToTimingsStruct(const TimingStructArgs& aArgs,
                                            TimingStruct& aTimings) {
  // If domainLookupStart/End was set by the channel before, we use these
  // timestamps instead the ones from the transaction.
  if (aTimings.domainLookupStart.IsNull() &&
      aTimings.domainLookupEnd.IsNull()) {
    aTimings.domainLookupStart = aArgs.domainLookupStart();
    aTimings.domainLookupEnd = aArgs.domainLookupEnd();
  }
  aTimings.connectStart = aArgs.connectStart();
  aTimings.tcpConnectEnd = aArgs.tcpConnectEnd();
  aTimings.secureConnectionStart = aArgs.secureConnectionStart();
  aTimings.connectEnd = aArgs.connectEnd();
  aTimings.requestStart = aArgs.requestStart();
  aTimings.responseStart = aArgs.responseStart();
  aTimings.responseEnd = aArgs.responseEnd();
  aTimings.transactionPending = aArgs.transactionPending();
}

void HttpTransactionParent::DoOnStartRequest(
    const nsresult& aStatus, Maybe<nsHttpResponseHead>&& aResponseHead,
    nsITransportSecurityInfo* aSecurityInfo, const bool& aProxyConnectFailed,
    const TimingStructArgs& aTimings, const int32_t& aProxyConnectResponseCode,
    nsTArray<uint8_t>&& aDataForSniffer, const Maybe<nsCString>& aAltSvcUsed,
    const bool& aDataToChildProcess, const bool& aRestarted,
    const uint32_t& aHTTPSSVCReceivedStage, const bool& aSupportsHttp3,
    const nsIRequest::TRRMode& aMode, const TRRSkippedReason& aSkipReason,
    const uint32_t& aCaps, const TimeStamp& aOnStartRequestStartTime,
    nsHttpConnectionInfo* aConnInfo,
    const nsILoadInfo::IPAddressSpace& aTargetIPAddressSpace) {
  LOG(("HttpTransactionParent::DoOnStartRequest [this=%p aStatus=%" PRIx32
       "]\n",
       this, static_cast<uint32_t>(aStatus)));

  if (mCanceled) {
    return;
  }

  MOZ_ASSERT(!mOnStartRequestCalled);

  mStatus = aStatus;
  mDataSentToChildProcess = aDataToChildProcess;
  mHTTPSSVCReceivedStage = aHTTPSSVCReceivedStage;
  mSupportsHTTP3 = aSupportsHttp3;
  mEffectiveTRRMode = aMode;
  mTRRSkipReason = aSkipReason;
  mCaps = aCaps;
  mSecurityInfo = aSecurityInfo;
  mOnStartRequestStartTime = aOnStartRequestStartTime;
  mConnInfo = aConnInfo;
  mTargetIPAddressSpace = aTargetIPAddressSpace;

  if (aResponseHead.isSome()) {
    mResponseHead =
        MakeUnique<nsHttpResponseHead>(std::move(aResponseHead.ref()));
  }
  mProxyConnectFailed = aProxyConnectFailed;
  TimingStructArgsToTimingsStruct(aTimings, mTimings);

  mProxyConnectResponseCode = aProxyConnectResponseCode;
  mDataForSniffer = std::move(aDataForSniffer);
  mRestarted = aRestarted;

  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(mChannel);
  MOZ_ASSERT(httpChannel, "mChannel is expected to implement nsIHttpChannel");
  if (httpChannel) {
    if (aAltSvcUsed.isSome()) {
      (void)httpChannel->SetRequestHeader(nsHttp::Alternate_Service_Used.val(),
                                          aAltSvcUsed.ref(), false);
    }
  }

  AutoEventEnqueuer ensureSerialDispatch(mEventQ);
  nsresult rv = mChannel->OnStartRequest(this);
  mOnStartRequestCalled = true;
  if (NS_FAILED(rv)) {
    Cancel(rv);
  }
}

mozilla::ipc::IPCResult HttpTransactionParent::RecvOnTransportStatus(
    const nsresult& aStatus, const int64_t& aProgress,
    const int64_t& aProgressMax,
    Maybe<NetworkAddressArg>&& aNetworkAddressArg) {
  if (aNetworkAddressArg) {
    mSelfAddr = aNetworkAddressArg->selfAddr();
    mPeerAddr = aNetworkAddressArg->peerAddr();
    mResolvedByTRR = aNetworkAddressArg->resolvedByTRR();
    mEffectiveTRRMode = aNetworkAddressArg->mode();
    mTRRSkipReason = aNetworkAddressArg->trrSkipReason();
    mEchConfigUsed = aNetworkAddressArg->echConfigUsed();
  }
  mEventsink->OnTransportStatus(nullptr, aStatus, aProgress, aProgressMax);
  return IPC_OK();
}

mozilla::ipc::IPCResult HttpTransactionParent::RecvOnDataAvailable(
    const nsCString& aData, const uint64_t& aOffset, const uint32_t& aCount,
    const TimeStamp& aOnDataAvailableStartTime) {
  LOG(("HttpTransactionParent::RecvOnDataAvailable [this=%p, aOffset= %" PRIu64
       " aCount=%" PRIu32,
       this, aOffset, aCount));

  // The final transfer size is updated in OnStopRequest ipc message, but in the
  // case that the socket process is crashed or something went wrong, we might
  // not get the OnStopRequest. So, let's update the transfer size here.
  mTransferSize += aCount;

  if (mCanceled) {
    return IPC_OK();
  }

  mEventQ->RunOrEnqueue(new ChannelFunctionEvent(
      [self = UnsafePtr<HttpTransactionParent>(this)]() {
        return self->GetODATarget();
      },
      [self = UnsafePtr<HttpTransactionParent>(this), aData, aOffset, aCount,
       aOnDataAvailableStartTime]() {
        self->DoOnDataAvailable(aData, aOffset, aCount,
                                aOnDataAvailableStartTime);
      }));
  return IPC_OK();
}

void HttpTransactionParent::DoOnDataAvailable(
    const nsCString& aData, const uint64_t& aOffset, const uint32_t& aCount,
    const TimeStamp& aOnDataAvailableStartTime) {
  LOG(("HttpTransactionParent::DoOnDataAvailable [this=%p]\n", this));
  if (mCanceled) {
    return;
  }

  nsCOMPtr<nsIInputStream> stringStream;
  nsresult rv =
      NS_NewByteInputStream(getter_AddRefs(stringStream),
                            Span(aData.get(), aCount), NS_ASSIGNMENT_DEPEND);

  if (NS_FAILED(rv)) {
    CancelOnMainThread(rv);
    return;
  }

  mOnDataAvailableStartTime = aOnDataAvailableStartTime;
  AutoEventEnqueuer ensureSerialDispatch(mEventQ);
  rv = mChannel->OnDataAvailable(this, stringStream, aOffset, aCount);
  if (NS_FAILED(rv)) {
    CancelOnMainThread(rv);
  }
}

// Note: Copied from HttpChannelChild.
void HttpTransactionParent::CancelOnMainThread(nsresult aRv) {
  LOG(("HttpTransactionParent::CancelOnMainThread [this=%p]", this));

  if (NS_IsMainThread()) {
    Cancel(aRv);
    return;
  }

  mEventQ->Suspend();
  // Cancel is expected to preempt any other channel events, thus we put this
  // event in the front of mEventQ to make sure nsIStreamListener not receiving
  // any ODA/OnStopRequest callbacks.
  mEventQ->PrependEvent(MakeUnique<NeckoTargetChannelFunctionEvent>(
      this, [self = UnsafePtr<HttpTransactionParent>(this), aRv]() {
        self->Cancel(aRv);
      }));
  mEventQ->Resume();
}

mozilla::ipc::IPCResult HttpTransactionParent::RecvOnStopRequest(
    const nsresult& aStatus, const bool& aResponseIsComplete,
    const int64_t& aTransferSize, const TimingStructArgs& aTimings,
    const Maybe<nsHttpHeaderArray>& aResponseTrailers,
    Maybe<TransactionObserverResult>&& aTransactionObserverResult,
    const TimeStamp& aLastActiveTabOptHit,
    const TimeStamp& aOnStopRequestStartTime) {
  LOG(("HttpTransactionParent::RecvOnStopRequest [this=%p status=%" PRIx32
       "]\n",
       this, static_cast<uint32_t>(aStatus)));

  nsHttp::SetLastActiveTabLoadOptimizationHit(aLastActiveTabOptHit);

  if (mCanceled) {
    return IPC_OK();
  }

  mEventQ->RunOrEnqueue(new NeckoTargetChannelFunctionEvent(
      this, [self = UnsafePtr<HttpTransactionParent>(this), aStatus,
             aResponseIsComplete, aTransferSize, aTimings, aResponseTrailers,
             aTransactionObserverResult{std::move(aTransactionObserverResult)},
             aOnStopRequestStartTime]() mutable {
        self->DoOnStopRequest(aStatus, aResponseIsComplete, aTransferSize,
                              aTimings, aResponseTrailers,
                              std::move(aTransactionObserverResult),
                              aOnStopRequestStartTime);
      }));
  return IPC_OK();
}

void HttpTransactionParent::DoOnStopRequest(
    const nsresult& aStatus, const bool& aResponseIsComplete,
    const int64_t& aTransferSize, const TimingStructArgs& aTimings,
    const Maybe<nsHttpHeaderArray>& aResponseTrailers,
    Maybe<TransactionObserverResult>&& aTransactionObserverResult,
    const TimeStamp& aOnStopRequestStartTime) {
  LOG(("HttpTransactionParent::DoOnStopRequest [this=%p]\n", this));
  if (mCanceled) {
    return;
  }

  MOZ_ASSERT(!mOnStopRequestCalled, "We should not call OnStopRequest twice");

  mStatus = aStatus;

  nsCOMPtr<nsIRequest> deathGrip = this;

  mResponseIsComplete = aResponseIsComplete;
  mTransferSize = aTransferSize;
  mOnStopRequestStartTime = aOnStopRequestStartTime;

  TimingStructArgsToTimingsStruct(aTimings, mTimings);

  if (aResponseTrailers.isSome()) {
    mResponseTrailers = MakeUnique<nsHttpHeaderArray>(aResponseTrailers.ref());
  }

  if (aTransactionObserverResult.isSome()) {
    TransactionObserverFunc obs = nullptr;
    std::swap(obs, mTransactionObserver);
    obs(std::move(*aTransactionObserverResult));
  }

  AutoEventEnqueuer ensureSerialDispatch(mEventQ);
  (void)mChannel->OnStopRequest(this, mStatus);
  mOnStopRequestCalled = true;
}

mozilla::ipc::IPCResult HttpTransactionParent::RecvOnInitFailed(
    const nsresult& aStatus) {
  nsCOMPtr<nsIRequest> request = do_QueryInterface(mEventsink);
  if (request) {
    request->Cancel(aStatus);
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult HttpTransactionParent::RecvEarlyHint(
    const nsCString& aValue, const nsACString& aReferrerPolicy,
    const nsACString& aCSPHeader) {
  LOG(
      ("HttpTransactionParent::RecvEarlyHint header=%s aReferrerPolicy=%s "
       "aCSPHeader=%s",
       PromiseFlatCString(aValue).get(),
       PromiseFlatCString(aReferrerPolicy).get(),
       PromiseFlatCString(aCSPHeader).get()));
  nsCOMPtr<nsIEarlyHintObserver> obs = do_QueryInterface(mChannel);
  if (obs) {
    (void)obs->EarlyHint(aValue, aReferrerPolicy, aCSPHeader);
  }

  return IPC_OK();
}

//-----------------------------------------------------------------------------
// HttpTransactionParent <nsIRequest>
//-----------------------------------------------------------------------------

NS_IMETHODIMP
HttpTransactionParent::GetName(nsACString& aResult) {
  aResult.Truncate();
  return NS_OK;
}

NS_IMETHODIMP
HttpTransactionParent::IsPending(bool* aRetval) {
  *aRetval = false;
  return NS_OK;
}

NS_IMETHODIMP
HttpTransactionParent::GetStatus(nsresult* aStatus) {
  *aStatus = mStatus;
  return NS_OK;
}

NS_IMETHODIMP HttpTransactionParent::SetCanceledReason(
    const nsACString& aReason) {
  return SetCanceledReasonImpl(aReason);
}

NS_IMETHODIMP HttpTransactionParent::GetCanceledReason(nsACString& aReason) {
  return GetCanceledReasonImpl(aReason);
}

NS_IMETHODIMP HttpTransactionParent::CancelWithReason(
    nsresult aStatus, const nsACString& aReason) {
  return CancelWithReasonImpl(aStatus, aReason);
}

NS_IMETHODIMP
HttpTransactionParent::Cancel(nsresult aStatus) {
  MOZ_ASSERT(NS_IsMainThread());

  LOG(("HttpTransactionParent::Cancel [this=%p status=%" PRIx32 "]\n", this,
       static_cast<uint32_t>(aStatus)));

  if (mCanceled) {
    LOG(("  already canceled\n"));
    return NS_OK;
  }

  MOZ_ASSERT(NS_FAILED(aStatus), "cancel with non-failure status code");

  mCanceled = true;
  mStatus = aStatus;
  if (CanSend()) {
    (void)SendCancelPump(mStatus);
  }

  // Put DoNotifyListener() in front of the queue to avoid OnDataAvailable
  // being called after cancellation. Note that
  // HttpTransactionParent::OnStart/StopRequest are driven by IPC messages and
  // HttpTransactionChild won't send IPC if already canceled. That's why we have
  // to call DoNotifyListener().
  mEventQ->Suspend();
  mEventQ->PrependEvent(MakeUnique<NeckoTargetChannelFunctionEvent>(
      this, [self = UnsafePtr<HttpTransactionParent>(this)]() {
        self->DoNotifyListener();
      }));
  mEventQ->Resume();
  return NS_OK;
}

void HttpTransactionParent::DoNotifyListener() {
  LOG(("HttpTransactionParent::DoNotifyListener this=%p", this));
  MOZ_ASSERT(NS_IsMainThread());

  if (mChannel && !mOnStartRequestCalled) {
    nsCOMPtr<nsIStreamListener> listener = mChannel;
    mOnStartRequestCalled = true;
    listener->OnStartRequest(this);
  }
  mOnStartRequestCalled = true;

  // This is to make sure that ODA in the event queue can be processed before
  // OnStopRequest.
  mEventQ->RunOrEnqueue(new NeckoTargetChannelFunctionEvent(
      this, [self = UnsafePtr<HttpTransactionParent>(this)] {
        self->ContinueDoNotifyListener();
      }));
}

void HttpTransactionParent::ContinueDoNotifyListener() {
  LOG(("HttpTransactionParent::ContinueDoNotifyListener this=%p", this));
  MOZ_ASSERT(NS_IsMainThread());

  if (mChannel && !mOnStopRequestCalled) {
    nsCOMPtr<nsIStreamListener> listener = mChannel;
    mOnStopRequestCalled = true;  // avoid reentrancy bugs by setting this now
    listener->OnStopRequest(this, mStatus);
  }
  mOnStopRequestCalled = true;

  mChannel = nullptr;
}

NS_IMETHODIMP
HttpTransactionParent::Suspend() {
  MOZ_ASSERT(NS_IsMainThread());

  // SendSuspend only once, when suspend goes from 0 to 1.
  if (!mSuspendCount++ && CanSend()) {
    (void)SendSuspendPump();
  }
  mEventQ->Suspend();
  return NS_OK;
}

NS_IMETHODIMP
HttpTransactionParent::Resume() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mSuspendCount, "Resume called more than Suspend");

  // SendResume only once, when suspend count drops to 0.
  if (mSuspendCount && !--mSuspendCount) {
    if (CanSend()) {
      (void)SendResumePump();
    }

    if (mCallOnResume) {
      nsCOMPtr<nsIEventTarget> neckoTarget = GetNeckoTarget();
      MOZ_ASSERT(neckoTarget);

      RefPtr<HttpTransactionParent> self = this;
      std::function<void()> callOnResume = nullptr;
      std::swap(callOnResume, mCallOnResume);
      neckoTarget->Dispatch(
          NS_NewRunnableFunction("net::HttpTransactionParent::mCallOnResume",
                                 [callOnResume]() { callOnResume(); }),
          NS_DISPATCH_NORMAL);
    }
  }
  mEventQ->Resume();
  return NS_OK;
}

NS_IMETHODIMP
HttpTransactionParent::GetLoadGroup(nsILoadGroup** aLoadGroup) {
  MOZ_ASSERT(false, "Should not be called.");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HttpTransactionParent::SetLoadGroup(nsILoadGroup* aLoadGroup) {
  MOZ_ASSERT(false, "Should not be called.");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HttpTransactionParent::GetLoadFlags(nsLoadFlags* aLoadFlags) {
  MOZ_ASSERT(false, "Should not be called.");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HttpTransactionParent::SetLoadFlags(nsLoadFlags aLoadFlags) {
  MOZ_ASSERT(false, "Should not be called.");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HttpTransactionParent::GetTRRMode(nsIRequest::TRRMode* aTRRMode) {
  MOZ_ASSERT(false, "Should not be called.");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
HttpTransactionParent::SetTRRMode(nsIRequest::TRRMode aTRRMode) {
  MOZ_ASSERT(false, "Should not be called.");
  return NS_ERROR_NOT_IMPLEMENTED;
}

void HttpTransactionParent::ActorDestroy(ActorDestroyReason aWhy) {
  LOG(("HttpTransactionParent::ActorDestroy [this=%p]\n", this));
  if (aWhy != Deletion) {
    // Make sure all the messages are processed.
    AutoEventEnqueuer ensureSerialDispatch(mEventQ);

    mStatus = NS_ERROR_FAILURE;
    HandleAsyncAbort();

    mCanceled = true;
  }
}

void HttpTransactionParent::HandleAsyncAbort() {
  MOZ_ASSERT(!mCallOnResume, "How did that happen?");

  if (mSuspendCount) {
    LOG(
        ("HttpTransactionParent Waiting until resume to do async notification "
         "[this=%p]\n",
         this));
    RefPtr<HttpTransactionParent> self = this;
    mCallOnResume = [self]() { self->HandleAsyncAbort(); };
    return;
  }

  DoNotifyListener();
}

bool HttpTransactionParent::GetSupportsHTTP3() { return mSupportsHTTP3; }

void HttpTransactionParent::SetIsForWebTransport(bool SetIsForWebTransport) {
  // TODO: bug 1791727
}

mozilla::TimeStamp HttpTransactionParent::GetPendingTime() {
  return mTimings.transactionPending;
}

}  // namespace mozilla::net
