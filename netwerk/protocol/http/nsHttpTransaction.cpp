/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "nsHttpTransaction.h"

#include <algorithm>
#include <utility>

#include "HttpLog.h"
#include "HTTPSRecordResolver.h"
#include "NSSErrorsService.h"
#include "base/basictypes.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/Components.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "mozilla/glean/NetwerkProtocolHttpMetrics.h"
#include "mozilla/net/SSLTokensCache.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/Tokenizer.h"
#include "mozilla/StaticPrefs_network.h"
#include "MockHttpAuth.h"
#include "nsCRT.h"
#include "nsComponentManagerUtils.h"  // do_CreateInstance
#include "nsHttpBasicAuth.h"
#include "nsHttpChannel.h"
#include "nsHttpChunkedDecoder.h"
#include "nsHttpDigestAuth.h"
#include "nsHttpHandler.h"
#include "nsHttpConnectionMgr.h"
#include "nsHttpNTLMAuth.h"
#ifdef MOZ_AUTH_EXTENSION
#  include "nsHttpNegotiateAuth.h"
#endif
#include "nsHttpRequestHead.h"
#include "nsHttpResponseHead.h"
#include "nsICancelable.h"
#include "nsIClassOfService.h"
#include "nsIDNSByTypeRecord.h"
#include "nsIDNSRecord.h"
#include "nsIDNSService.h"
#include "nsIEventTarget.h"
#include "nsIHttpActivityObserver.h"
#include "nsIHttpAuthenticator.h"
#include "nsIInputStream.h"
#include "nsIInputStreamPriority.h"
#include "nsIMultiplexInputStream.h"
#include "nsIOService.h"
#include "nsIPipe.h"
#include "nsIRequestContext.h"
#include "nsISeekableStream.h"
#include "nsITLSSocketControl.h"
#include "nsIThrottledInputChannel.h"
#include "nsITransport.h"
#include "nsMultiplexInputStream.h"
#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsQueryObject.h"
#include "nsSocketTransportService2.h"
#include "nsStringStream.h"
#include "nsThreadUtils.h"
#include "nsTransportUtils.h"
#include "sslerr.h"
#include "SpeculativeTransaction.h"
#include "mozilla/Preferences.h"

//-----------------------------------------------------------------------------

// Place a limit on how much non-compliant HTTP can be skipped while
// looking for a response header
#define MAX_INVALID_RESPONSE_BODY_SIZE (1024 * 128)

using namespace mozilla::net;

namespace mozilla::net {

//-----------------------------------------------------------------------------
// nsHttpTransaction::UpdateSecurityCallbacks
//-----------------------------------------------------------------------------

NS_IMPL_ISUPPORTS_INHERITED(nsHttpTransaction::UpdateSecurityCallbacks,
                            Runnable, nsIRunnablePriority)

NS_IMETHODIMP
nsHttpTransaction::UpdateSecurityCallbacks::GetPriority(uint32_t* aPriority) {
  *aPriority = mPriority;
  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpTransaction <public>
//-----------------------------------------------------------------------------

nsHttpTransaction::nsHttpTransaction() {
  LOG(("Creating nsHttpTransaction @%p\n", this));

#ifdef MOZ_VALGRIND
  memset(&mSelfAddr, 0, sizeof(NetAddr));
  memset(&mPeerAddr, 0, sizeof(NetAddr));
#endif
  mSelfAddr.raw.family = PR_AF_UNSPEC;
  mPeerAddr.raw.family = PR_AF_UNSPEC;
}

void nsHttpTransaction::ResumeReading() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  if (!mReadingStopped) {
    return;
  }

  LOG(("nsHttpTransaction::ResumeReading %p", this));

  mReadingStopped = false;

  // This with either reengage the limit when still throttled in WriteSegments
  // or simply reset to allow unlimeted reading again.
  mThrottlingReadAllowance = THROTTLE_NO_LIMIT;

  if (mConnection) {
    mConnection->TransactionHasDataToRecv(this);
    nsresult rv = mConnection->ResumeRecv();
    if (NS_FAILED(rv)) {
      LOG(("  resume failed with rv=%" PRIx32, static_cast<uint32_t>(rv)));
    }
  }
}

bool nsHttpTransaction::EligibleForThrottling() const {
  return (mClassOfServiceFlags &
          (nsIClassOfService::Throttleable | nsIClassOfService::DontThrottle |
           nsIClassOfService::Leader | nsIClassOfService::Unblocked)) ==
         nsIClassOfService::Throttleable;
}

void nsHttpTransaction::SetClassOfService(ClassOfService cos) {
  if (mClosed) {
    return;
  }

  bool wasThrottling = EligibleForThrottling();
  mClassOfServiceFlags = cos.Flags();
  mClassOfServiceIncremental = cos.Incremental();
  bool isThrottling = EligibleForThrottling();

  if (mConnection && wasThrottling != isThrottling) {
    // Do nothing until we are actually activated.  For now
    // only remember the throttle flag.  Call to UpdateActiveTransaction
    // would add this transaction to the list too early.
    gHttpHandler->ConnMgr()->UpdateActiveTransaction(this);

    if (mReadingStopped && !isThrottling) {
      ResumeReading();
    }
  }
}

nsHttpTransaction::~nsHttpTransaction() {
  LOG(("Destroying nsHttpTransaction @%p\n", this));

  if (mTokenBucketCancel) {
    if (OnSocketThread()) {
      mTokenBucketCancel->Cancel(NS_ERROR_ABORT);
    } else {
      MOZ_DIAGNOSTIC_ASSERT(false,
                            "Token bucket not canceled before off-thread "
                            "destruction");
    }
    mTokenBucketCancel = nullptr;
  }

  // Force the callbacks and connection to be released right now
  {
    MutexAutoLock lock(mLock);
    mCallbacks = nullptr;
  }

  mEarlyHintObserver = nullptr;

  delete mResponseHead;
  delete mChunkedDecoder;
  ReleaseBlockingTransaction();
}

nsresult nsHttpTransaction::Init(
    uint32_t caps, nsHttpConnectionInfo* cinfo, nsHttpRequestHead* requestHead,
    nsIInputStream* requestBody, uint64_t requestContentLength,
    bool requestBodyHasHeaders, nsIEventTarget* target,
    nsIInterfaceRequestor* callbacks, nsITransportEventSink* eventsink,
    uint64_t browserId, HttpTrafficCategory trafficCategory,
    nsIRequestContext* requestContext, ClassOfService classOfService,
    uint32_t initialRwin, bool responseTimeoutEnabled, uint64_t channelId,
    TransactionObserverFunc&& transactionObserver,
    nsILoadInfo::IPAddressSpace aParentIpAddressSpace,
    const struct LNAPerms& aLnaPermissionStatus) {
  nsresult rv;

  LOG1(("nsHttpTransaction::Init [this=%p caps=%x]\n", this, caps));

  bool isBeacon = false;
  RefPtr<nsHttpChannel> httpChannel = do_QueryObject(eventsink);
  if (httpChannel) {
    // Beacons are sometimes sent in pagehide during browser shutdown.
    // When that happens, we should allow the beacon to maybe succeed
    // even though it's going to be racy (shutdown may finish first).
    // See bug 1931956.
    nsCOMPtr<nsILoadInfo> loadInfo = httpChannel->LoadInfo();
    if (loadInfo->InternalContentPolicyType() ==
        nsIContentPolicy::TYPE_BEACON) {
      isBeacon = true;
    }
  }

  if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed) &&
      !isBeacon) {
    LOG(
        ("nsHttpTransaction aborting init because of app"
         "shutdown"));
    return NS_ERROR_ILLEGAL_DURING_SHUTDOWN;
  }

  MOZ_ASSERT(cinfo);
  MOZ_ASSERT(requestHead);
  MOZ_ASSERT(target);
  MOZ_ASSERT(target->IsOnCurrentThread());

  mChannelId = channelId;
  mTransactionObserver = std::move(transactionObserver);
  mBrowserId = browserId;

  mTrafficCategory = trafficCategory;

  LOG1(("nsHttpTransaction %p SetRequestContext %p\n", this, requestContext));
  mRequestContext = requestContext;

  SetClassOfService(classOfService);
  mResponseTimeoutEnabled = responseTimeoutEnabled;
  mInitialRwin = initialRwin;

  // create transport event sink proxy. it coalesces consecutive
  // events of the same status type.
  rv = net_NewTransportEventSinkProxy(getter_AddRefs(mTransportSink), eventsink,
                                      target);

  if (NS_FAILED(rv)) return rv;

  mConnInfo = cinfo->Clone();
  // No lock needed: the transaction is initialized on the main thread only,
  // so there is no possibility of a race at this point.
  MOZ_PUSH_IGNORE_THREAD_SAFETY
  mFinalizedConnInfo = mConnInfo;
  mCallbacks = callbacks;
  mEarlyHintObserver = do_QueryInterface(eventsink);
  MOZ_POP_THREAD_SAFETY
  mConsumerTarget = target;
  mCaps = caps;

  mParentIPAddressSpace = aParentIpAddressSpace;
  mLnaPermissionStatus = aLnaPermissionStatus;

  if (requestHead->IsHead()) {
    mNoContent = true;
  }

  // grab a weak reference to the request head
  mRequestHead = requestHead;

  mReqHeaderBuf = nsHttp::ConvertRequestHeadToString(
      *requestHead, !!requestBody, requestBodyHasHeaders,
      cinfo->UsingConnect());

  if (LOG1_ENABLED()) {
    LOG1(("http request [\n"));
    LogHeaders(mReqHeaderBuf.get());
    LOG1(("]\n"));
  }

  // report the request header
  if (gHttpHandler->HttpActivityDistributorActivated()) {
    nsCString requestBuf(mReqHeaderBuf);
    NS_DispatchToMainThread(NS_NewRunnableFunction(
        "ObserveHttpActivityWithArgs", [channelId(mChannelId), requestBuf]() {
          if (!gHttpHandler) {
            return;
          }
          gHttpHandler->ObserveHttpActivityWithArgs(
              HttpActivityArgs(channelId),
              NS_HTTP_ACTIVITY_TYPE_HTTP_TRANSACTION,
              NS_HTTP_ACTIVITY_SUBTYPE_REQUEST_HEADER, PR_Now(), 0, requestBuf);
        }));
  }

  // Create a string stream for the request header buf (the stream holds
  // a non-owning reference to the request header data, so we MUST keep
  // mReqHeaderBuf around).
  nsCOMPtr<nsIInputStream> headers;
  rv = NS_NewByteInputStream(getter_AddRefs(headers), mReqHeaderBuf,
                             NS_ASSIGNMENT_DEPEND);
  if (NS_FAILED(rv)) return rv;

  mHasRequestBody = !!requestBody;
  if (mHasRequestBody && !requestContentLength) {
    mHasRequestBody = false;
  }

  requestContentLength += mReqHeaderBuf.Length();

  if (mHasRequestBody) {
    // wrap the headers and request body in a multiplexed input stream.
    RefPtr<nsMultiplexInputStream> multi = new nsMultiplexInputStream();

    rv = multi->AppendStream(headers);
    if (NS_FAILED(rv)) return rv;

    rv = multi->AppendStream(requestBody);
    if (NS_FAILED(rv)) return rv;

    // wrap the multiplexed input stream with a buffered input stream, so
    // that we write data in the largest chunks possible.  this is actually
    // necessary to workaround some common server bugs (see bug 137155).
    rv = NS_NewBufferedInputStream(getter_AddRefs(mRequestStream),
                                   multi.forget(),
                                   nsIOService::gDefaultSegmentSize);
    if (NS_FAILED(rv)) return rv;
  } else {
    mRequestStream = headers;
  }

  nsCOMPtr<nsIThrottledInputChannel> throttled = do_QueryInterface(eventsink);
  if (throttled) {
    nsCOMPtr<nsIInputChannelThrottleQueue> queue;
    rv = throttled->GetThrottleQueue(getter_AddRefs(queue));
    // In case of failure, just carry on without throttling.
    if (NS_SUCCEEDED(rv) && queue) {
      nsCOMPtr<nsIAsyncInputStream> wrappedStream;
      rv = queue->WrapStream(mRequestStream, getter_AddRefs(wrappedStream));
      // Failure to throttle isn't sufficient reason to fail
      // initialization
      if (NS_SUCCEEDED(rv)) {
        MOZ_ASSERT(wrappedStream != nullptr);
        LOG(
            ("nsHttpTransaction::Init %p wrapping input stream using throttle "
             "queue %p\n",
             this, queue.get()));
        mRequestStream = wrappedStream;
      }
    }
  }

  // make sure request content-length fits within js MAX_SAFE_INTEGER
  mRequestSize = InScriptableRange(requestContentLength)
                     ? static_cast<int64_t>(requestContentLength)
                     : -1;

  // create pipe for response stream
  NS_NewPipe2(getter_AddRefs(mPipeIn), getter_AddRefs(mPipeOut), true, true,
              nsIOService::gDefaultSegmentSize,
              nsIOService::gDefaultSegmentCount);

  // HTTPS RR is handled by HappyEyeballsConnectionAttempt.
  bool forceUseHTTPSRR = StaticPrefs::network_dns_force_use_https_rr() &&
                         !(mCaps & NS_HTTP_USE_HAPPY_EYEBALLS);
  if ((StaticPrefs::network_dns_use_https_rr_as_altsvc() &&
       !(mCaps & NS_HTTP_DISALLOW_HTTPS_RR) &&
       !(mCaps & NS_HTTP_USE_HAPPY_EYEBALLS)) ||
      forceUseHTTPSRR) {
    nsCOMPtr<nsIEventTarget> target;
    (void)gHttpHandler->GetSocketThreadTarget(getter_AddRefs(target));
    if (target) {
      if (forceUseHTTPSRR) {
        mCaps |= NS_HTTP_FORCE_WAIT_HTTP_RR;
      }

      mResolver = new HTTPSRecordResolver(this);
      nsCOMPtr<nsICancelable> dnsRequest;
      rv = mResolver->FetchHTTPSRRInternal(target, getter_AddRefs(dnsRequest));
      if (NS_SUCCEEDED(rv)) {
        mHTTPSSVCReceivedStage = HTTPSSVC_NOT_PRESENT;
      }

      {
        MutexAutoLock lock(mLock);
        mDNSRequest.swap(dnsRequest);
        if (NS_FAILED(rv)) {
          MakeDontWaitHTTPSRR();
        }
      }
    }
  }

  if (httpChannel) {
    RefPtr<WebTransportSessionEventListener> listener =
        httpChannel->GetWebTransportSessionEventListener();
    if (listener) {
      // No lock needed: initialized on the main thread only.
      MOZ_PUSH_IGNORE_THREAD_SAFETY
      mWebTransportSessionEventListener = std::move(listener);
      MOZ_POP_THREAD_SAFETY
    }
    nsCOMPtr<nsIURI> uri;
    if (NS_SUCCEEDED(httpChannel->GetURI(getter_AddRefs(uri)))) {
      mUrl = uri->GetSpecOrDefault();
    }
  }

  return NS_OK;
}

static inline void CreateAndStartTimer(nsCOMPtr<nsITimer>& aTimer,
                                       nsITimerCallback* aCallback,
                                       uint32_t aTimeout) {
  MOZ_DIAGNOSTIC_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(!aTimer);

  if (!aTimeout) {
    return;
  }

  NS_NewTimerWithCallback(getter_AddRefs(aTimer), aCallback, aTimeout,
                          nsITimer::TYPE_ONE_SHOT);
}

void nsHttpTransaction::OnPendingQueueInserted(
    const nsACString& aConnectionHashKey) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  {
    MutexAutoLock lock(mLock);
    mHashKeyOfConnectionEntry.Assign(aConnectionHashKey);
  }

  // Don't create mHttp3BackupTimer if HTTPS RR is used or Happy Eyeballs is
  // enabled. The Happy Eyeballs state machine will handle fallback to h2.
  if ((mConnInfo->IsHttp3() || mConnInfo->IsHttp3ProxyConnection()) &&
      !mOrigConnInfo && !mConnInfo->GetWebTransport() &&
      !(mCaps & NS_HTTP_USE_HAPPY_EYEBALLS)) {
    // Backup timer should only be created once.
    if (!mHttp3BackupTimerCreated) {
      CreateAndStartTimer(mHttp3BackupTimer, this,
                          StaticPrefs::network_http_http3_backup_timer_delay());
      mHttp3BackupTimerCreated = true;
    }
  }
}

nsresult nsHttpTransaction::AsyncRead(nsIStreamListener* listener,
                                      nsIRequest** pump) {
  RefPtr<nsInputStreamPump> transactionPump;
  nsresult rv =
      nsInputStreamPump::Create(getter_AddRefs(transactionPump), mPipeIn);
  NS_ENSURE_SUCCESS(rv, rv);

  // If this is for a TRR request, we increase the priority of the
  // OnInputStreamReady runnables
  if (mIsTRRTransaction) {
    transactionPump->SetHighPriority(true);
  }

  rv = transactionPump->AsyncRead(listener);
  NS_ENSURE_SUCCESS(rv, rv);

  transactionPump.forget(pump);
  return NS_OK;
}

// This method should only be used on the socket thread
nsAHttpConnection* nsHttpTransaction::Connection() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  return mConnection.get();
}

void nsHttpTransaction::SetH2WSConnRefTaken() {
  if (!OnSocketThread()) {
    nsCOMPtr<nsIRunnable> event =
        NewRunnableMethod("nsHttpTransaction::SetH2WSConnRefTaken", this,
                          &nsHttpTransaction::SetH2WSConnRefTaken);
    if (mIsTRRTransaction) {
      event = new PrioritizableRunnable(
          event.forget(), nsIRunnablePriority::PRIORITY_MEDIUMHIGH);
    }
    gSocketTransportService->Dispatch(event, NS_DISPATCH_NORMAL);
    return;
  }
}

UniquePtr<nsHttpResponseHead> nsHttpTransaction::TakeResponseHeadAndConnInfo(
    nsHttpConnectionInfo** aOut) {
  MOZ_ASSERT(!mResponseHeadTaken, "TakeResponseHead called 2x");

  // Lock TakeResponseHead() against main thread
  MutexAutoLock lock(mLock);

  if (aOut) {
    RefPtr<nsHttpConnectionInfo> connInfo = mFinalizedConnInfo;
    connInfo.forget(aOut);
  }

  mResponseHeadTaken = true;

  // Even in OnStartRequest() the headers won't be available if we were
  // canceled
  if (!mHaveAllHeaders) {
    NS_WARNING("response headers not available or incomplete");
    return nullptr;
  }

  return WrapUnique(std::exchange(mResponseHead, nullptr));
}

UniquePtr<nsHttpHeaderArray> nsHttpTransaction::TakeResponseTrailers() {
  MOZ_ASSERT(!mResponseTrailersTaken, "TakeResponseTrailers called 2x");

  // Lock TakeResponseTrailers() against main thread
  MutexAutoLock lock(mLock);

  mResponseTrailersTaken = true;
  return std::move(mForTakeResponseTrailers);
}

void nsHttpTransaction::SetProxyConnectFailed() { mProxyConnectFailed = true; }

nsHttpRequestHead* nsHttpTransaction::RequestHead() { return mRequestHead; }

uint32_t nsHttpTransaction::Http1xTransactionCount() { return 1; }

nsresult nsHttpTransaction::TakeSubTransactions(
    nsTArray<RefPtr<nsAHttpTransaction>>& outTransactions) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

//----------------------------------------------------------------------------
// nsHttpTransaction::nsAHttpTransaction
//----------------------------------------------------------------------------

void nsHttpTransaction::SetConnection(nsAHttpConnection* conn) {
  {
    MutexAutoLock lock(mLock);
    mConnection = conn;
    if (mConnection) {
      mIsHttp3Used = mConnection->Version() == HttpVersion::v3_0;
      if (mActivated) {
        mConnection->GetSelfAddr(&mSelfAddr);
        mConnection->GetPeerAddr(&mPeerAddr);
        mResolvedByTRR = mConnection->ResolvedByTRR();
        mEffectiveTRRMode = mConnection->EffectiveTRRMode();
        mTRRSkipReason = mConnection->TRRSkipReason();
        mEchConfigUsed = mConnection->GetEchConfigUsed();
      }
    }
  }
}

void nsHttpTransaction::OnActivated() {
  nsresult rv;
  MOZ_ASSERT(OnSocketThread());

  if (mActivated) {
    return;
  }

  if (mTrafficCategory != HttpTrafficCategory::eInvalid) {
    HttpTrafficAnalyzer* hta = gHttpHandler->GetHttpTrafficAnalyzer();
    if (hta) {
      hta->IncrementHttpTransaction(mTrafficCategory);
    }
    if (mConnection) {
      mConnection->SetTrafficCategory(mTrafficCategory);
    }
  }

  if (mConnection && mRequestHead &&
      mConnection->Version() >= HttpVersion::v2_0) {
    // So this is fun. On http/2, we want to send TE: trailers, to be
    // spec-compliant. So we add it to the request head here. The fun part
    // is that adding a header to the request head at this point has no
    // effect on what we send on the wire, as the headers are already
    // flattened (in Init()) by the time we get here. So the *real* adding
    // of the header happens in the h2 compression code. We still have to
    // add the header to the request head here, though, so that devtools can
    // show that we sent the header. FUN!
    nsAutoCString teHeader;
    rv = mRequestHead->GetHeader(nsHttp::TE, teHeader);
    if (NS_FAILED(rv) || !teHeader.Equals("moz_no_te_trailers"_ns)) {
      // If the request already has TE:moz_no_te_trailers then
      // Http2Compressor::EncodeHeaderBlock won't actually add this header.
      (void)mRequestHead->SetHeader(nsHttp::TE, "trailers"_ns);
    }
  }

  // Populate connection metadata here rather than in OnTransportStatus. With
  // happy eyeballs, transport status events may arrive before the winning
  // connection is assigned to the transaction, making OnTransportStatus
  // unreliable for this purpose.
  if (mConnection) {
    MutexAutoLock lock(mLock);
    mConnection->GetSelfAddr(&mSelfAddr);
    mConnection->GetPeerAddr(&mPeerAddr);
    mResolvedByTRR = mConnection->ResolvedByTRR();
    mEffectiveTRRMode = mConnection->EffectiveTRRMode();
    mTRRSkipReason = mConnection->TRRSkipReason();
    mEchConfigUsed = mConnection->GetEchConfigUsed();
  }

  mActivated = true;
  gHttpHandler->ConnMgr()->AddActiveTransaction(this);
  FinalizeConnInfo();
}

void nsHttpTransaction::GetSecurityCallbacks(nsIInterfaceRequestor** cb) {
  MutexAutoLock lock(mLock);
  nsCOMPtr<nsIInterfaceRequestor> tmp(mCallbacks);
  tmp.forget(cb);
}

void nsHttpTransaction::SetSecurityCallbacks(
    nsIInterfaceRequestor* aCallbacks) {
  {
    MutexAutoLock lock(mLock);
    mCallbacks = aCallbacks;
  }

  if (gSocketTransportService) {
    RefPtr<UpdateSecurityCallbacks> event = new UpdateSecurityCallbacks(
        this, aCallbacks,
        mIsTRRTransaction ? nsIRunnablePriority::PRIORITY_MEDIUMHIGH
                          : nsIRunnablePriority::PRIORITY_NORMAL);
    gSocketTransportService->Dispatch(event, nsIEventTarget::DISPATCH_NORMAL);
  }
}

void nsHttpTransaction::OnTransportStatus(nsITransport* transport,
                                          nsresult status, int64_t progress) {
  LOG1(("nsHttpTransaction::OnTransportStatus [this=%p status=%" PRIx32
        " progress=%" PRId64 "]\n",
        this, static_cast<uint32_t>(status), progress));

  // If the timing is enabled, and we are not using a persistent connection
  // then the requestStart timestamp will be null, so we mark the timestamps
  // for domainLookupStart/End and connectStart/End
  // If we are using a persistent connection they will remain null,
  // and the correct value will be returned in Performance.
  if (GetRequestStart().IsNull()) {
    if (status == NS_NET_STATUS_RESOLVING_HOST) {
      SetDomainLookupStart(TimeStamp::Now(), true);
    } else if (status == NS_NET_STATUS_RESOLVED_HOST) {
      SetDomainLookupEnd(TimeStamp::Now());
    } else if (status == NS_NET_STATUS_CONNECTING_TO) {
      TimeStamp tnow = TimeStamp::Now();
      {
        MutexAutoLock lock(mLock);
        mTimings.connectStart = tnow;
        if (mConnInfo && mConnInfo->IsHttp3()) {
          mTimings.secureConnectionStart = tnow;
        }
      }
    } else if (status == NS_NET_STATUS_CONNECTED_TO) {
      TimeStamp tnow = TimeStamp::Now();
      SetConnectEnd(tnow, true);
      {
        MutexAutoLock lock(mLock);
        mTimings.tcpConnectEnd = tnow;
      }
    } else if (status == NS_NET_STATUS_TLS_HANDSHAKE_STARTING) {
      {
        MutexAutoLock lock(mLock);
        mTimings.secureConnectionStart = TimeStamp::Now();
      }
    } else if (status == NS_NET_STATUS_TLS_HANDSHAKE_ENDED) {
      SetConnectEnd(TimeStamp::Now(), false);
    } else if (status == NS_NET_STATUS_SENDING_TO) {
      // Set the timestamp to Now(), only if it null
      SetRequestStart(TimeStamp::Now(), true);
    }
  }

  if (!mTransportSink) return;

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  // upon STATUS_WAITING_FOR; report request body sent
  if ((mHasRequestBody) && (status == NS_NET_STATUS_WAITING_FOR)) {
    gHttpHandler->ObserveHttpActivityWithArgs(
        HttpActivityArgs(mChannelId), NS_HTTP_ACTIVITY_TYPE_HTTP_TRANSACTION,
        NS_HTTP_ACTIVITY_SUBTYPE_REQUEST_BODY_SENT, PR_Now(), 0, ""_ns);
  }

  if (status == NS_NET_STATUS_SENDING_TO && mReader) {
    // A mRequestStream method is on the stack - wait.
    LOG(
        ("nsHttpTransaction::OnSocketStatus [this=%p] "
         "Skipping Re-Entrant NS_NET_STATUS_SENDING_TO\n",
         this));
    // its ok to coalesce several of these into one deferred event
    mDeferredSendProgress = true;
    return;
  }

  // report the status and progress
  gHttpHandler->ObserveHttpActivityWithArgs(
      HttpActivityArgs(mChannelId), NS_HTTP_ACTIVITY_TYPE_SOCKET_TRANSPORT,
      static_cast<uint32_t>(status), PR_Now(), progress, ""_ns);

  // nsHttpChannel synthesizes progress events in OnDataAvailable
  if (status == NS_NET_STATUS_RECEIVING_FROM) return;

  int64_t progressMax;

  if (status == NS_NET_STATUS_SENDING_TO) {
    // suppress progress when only writing request headers
    if (!mHasRequestBody) {
      LOG1(
          ("nsHttpTransaction::OnTransportStatus %p "
           "SENDING_TO without request body\n",
           this));
      return;
    }

    nsCOMPtr<nsITellableStream> tellable = do_QueryInterface(mRequestStream);
    if (!tellable) {
      LOG1(
          ("nsHttpTransaction::OnTransportStatus %p "
           "SENDING_TO without tellable request stream\n",
           this));
      MOZ_ASSERT(
          !mRequestStream,
          "mRequestStream should be tellable as it was wrapped in "
          "nsBufferedInputStream, which provides the tellable interface even "
          "when wrapping non-tellable streams.");
      progress = 0;
    } else {
      int64_t prog = 0;
      tellable->Tell(&prog);
      progress = prog;
    }

    // when uploading, we include the request headers in the progress
    // notifications.
    progressMax = mRequestSize;
  } else {
    progress = 0;
    progressMax = 0;
  }

  mTransportSink->OnTransportStatus(transport, status, progress, progressMax);
}

bool nsHttpTransaction::IsDone() { return mTransactionDone; }

nsresult nsHttpTransaction::Status() { return mStatus; }

uint32_t nsHttpTransaction::Caps() { return mCaps & ~mCapsToClear; }

void nsHttpTransaction::SetDNSWasRefreshed() {
  MOZ_ASSERT(mConsumerTarget->IsOnCurrentThread(),
             "SetDNSWasRefreshed on target thread only!");
  mCapsToClear |= NS_HTTP_REFRESH_DNS;
}

nsresult nsHttpTransaction::ReadRequestSegment(nsIInputStream* stream,
                                               void* closure, const char* buf,
                                               uint32_t offset, uint32_t count,
                                               uint32_t* countRead) {
  // For the tracking of sent bytes that we used to do for the networkstats
  // API, please see bug 1318883 where it was removed.

  nsHttpTransaction* trans = (nsHttpTransaction*)closure;
  nsresult rv = trans->mReader->OnReadSegment(buf, count, countRead);
  if (NS_FAILED(rv)) {
    trans->MaybeRefreshSecurityInfo();
    return rv;
  }

  LOG(("nsHttpTransaction::ReadRequestSegment %p read=%u", trans, *countRead));

  trans->mSentData = true;
  return NS_OK;
}

nsresult nsHttpTransaction::ReadSegments(nsAHttpSegmentReader* reader,
                                         uint32_t count, uint32_t* countRead) {
  LOG(("nsHttpTransaction::ReadSegments %p", this));

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  if (mTransactionDone) {
    *countRead = 0;
    return mStatus;
  }

  if (!m0RTTInProgress) {
    MaybeCancelFallbackTimer();
  }

  if (!mConnected && !m0RTTInProgress) {
    mConnected = true;
    MaybeRefreshSecurityInfo();
  }

  mDeferredSendProgress = false;
  mReader = reader;
  nsresult rv =
      mRequestStream->ReadSegments(ReadRequestSegment, this, count, countRead);
  mReader = nullptr;

  if (m0RTTInProgress && (mEarlyDataDisposition == EARLY_NONE) &&
      NS_SUCCEEDED(rv) && (*countRead > 0)) {
    LOG(("mEarlyDataDisposition = EARLY_SENT"));
    mEarlyDataDisposition = EARLY_SENT;
  }

  if (mDeferredSendProgress && mConnection) {
    // to avoid using mRequestStream concurrently, OnTransportStatus()
    // did not report upload status off the ReadSegments() stack from
    // nsSocketTransport do it now.
    OnTransportStatus(mConnection->Transport(), NS_NET_STATUS_SENDING_TO, 0);
  }
  mDeferredSendProgress = false;

  if (mForceRestart) {
    // The forceRestart condition was dealt with on the stack, but it did not
    // clear the flag because nsPipe in the readsegment stack clears out
    // return codes, so we need to use the flag here as a cue to return
    // ERETARGETED
    if (NS_SUCCEEDED(rv)) {
      rv = NS_BINDING_RETARGETED;
    }
    mForceRestart = false;
  }

  // if read would block then we need to AsyncWait on the request stream.
  // have callback occur on socket thread so we stay synchronized.
  if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
    nsCOMPtr<nsIAsyncInputStream> asyncIn = do_QueryInterface(mRequestStream);
    if (asyncIn) {
      nsCOMPtr<nsIEventTarget> target;
      (void)gHttpHandler->GetSocketThreadTarget(getter_AddRefs(target));
      if (target) {
        asyncIn->AsyncWait(this, 0, 0, target);
      } else {
        NS_ERROR("no socket thread event target");
        rv = NS_ERROR_UNEXPECTED;
      }
    }
  }

  return rv;
}

nsresult nsHttpTransaction::WritePipeSegment(nsIOutputStream* stream,
                                             void* closure, char* buf,
                                             uint32_t offset, uint32_t count,
                                             uint32_t* countWritten) {
  nsHttpTransaction* trans = (nsHttpTransaction*)closure;

  if (trans->mTransactionDone) return NS_BASE_STREAM_CLOSED;  // stop iterating

  // Set the timestamp to Now(), only if it null
  trans->SetResponseStart(TimeStamp::Now(), true);

  // Bug 1153929 - add checks to fix windows crash
  MOZ_ASSERT(trans->mWriter);
  if (!trans->mWriter) {
    return NS_ERROR_UNEXPECTED;
  }

  nsresult rv;
  //
  // OK, now let the caller fill this segment with data.
  //
  rv = trans->mWriter->OnWriteSegment(buf, count, countWritten);
  if (NS_FAILED(rv)) {
    trans->MaybeRefreshSecurityInfo();
    return rv;  // caller didn't want to write anything
  }

  LOG(("nsHttpTransaction::WritePipeSegment %p written=%u", trans,
       *countWritten));

  MOZ_ASSERT(*countWritten > 0, "bad writer");
  trans->mReceivedData = true;
  trans->mTransferSize += *countWritten;

  // Let the transaction "play" with the buffer.  It is free to modify
  // the contents of the buffer and/or modify countWritten.
  // - Bytes in HTTP headers don't count towards countWritten, so the input
  // side of pipe (aka nsHttpChannel's mTransactionPump) won't hit
  // OnInputStreamReady until all headers have been parsed.
  //
  rv = trans->ProcessData(buf, *countWritten, countWritten);
  if (NS_FAILED(rv)) trans->Close(rv);

  return rv;  // failure code only stops WriteSegments; it is not propagated.
}

bool nsHttpTransaction::ShouldThrottle() {
  if (mClassOfServiceFlags & nsIClassOfService::DontThrottle) {
    // We deliberately don't touch the throttling window here since
    // DontThrottle requests are expected to be long-standing media
    // streams and would just unnecessarily block running downloads.
    // If we want to ballance bandwidth for media responses against
    // running downloads, we need to find something smarter like
    // changing the suspend/resume throttling intervals at-runtime.
    return false;
  }

  if (!gHttpHandler->ConnMgr()->ShouldThrottle(this)) {
    // We are not obligated to throttle
    return false;
  }

  if (mContentRead < 16000) {
    // Let the first bytes go, it may also well be all the content we get
    LOG(("nsHttpTransaction::ShouldThrottle too few content (%" PRIi64
         ") this=%p",
         mContentRead, this));
    return false;
  }

  if (!(mClassOfServiceFlags & nsIClassOfService::Throttleable) &&
      gHttpHandler->ConnMgr()->IsConnEntryUnderPressure(mConnInfo)) {
    LOG(("nsHttpTransaction::ShouldThrottle entry pressure this=%p", this));
    // This is expensive to check (two hashtable lookups) but may help
    // freeing connections for active tab transactions.
    // Checking this only for transactions that are not explicitly marked
    // as throttleable because trackers and (specially) downloads should
    // keep throttling even under pressure.
    return false;
  }

  return true;
}

void nsHttpTransaction::DontReuseConnection() {
  LOG(("nsHttpTransaction::DontReuseConnection %p\n", this));
  if (!OnSocketThread()) {
    LOG(("DontReuseConnection %p not on socket thread\n", this));
    nsCOMPtr<nsIRunnable> event =
        NewRunnableMethod("nsHttpTransaction::DontReuseConnection", this,
                          &nsHttpTransaction::DontReuseConnection);
    if (mIsTRRTransaction) {
      event = new PrioritizableRunnable(
          event.forget(), nsIRunnablePriority::PRIORITY_MEDIUMHIGH);
    }
    gSocketTransportService->Dispatch(event, NS_DISPATCH_NORMAL);
    return;
  }

  if (mConnection) {
    mConnection->DontReuse();
  }
}

nsresult nsHttpTransaction::WriteSegments(nsAHttpSegmentWriter* writer,
                                          uint32_t count,
                                          uint32_t* countWritten) {
  LOG(("nsHttpTransaction::WriteSegments %p", this));

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  if (mTransactionDone) {
    return NS_SUCCEEDED(mStatus) ? NS_BASE_STREAM_CLOSED : mStatus;
  }

  if (ShouldThrottle()) {
    if (mThrottlingReadAllowance == THROTTLE_NO_LIMIT) {  // no limit set
      // V1: ThrottlingReadLimit() returns 0
      mThrottlingReadAllowance = gHttpHandler->ThrottlingReadLimit();
    }
  } else {
    mThrottlingReadAllowance = THROTTLE_NO_LIMIT;  // don't limit
  }

  if (mThrottlingReadAllowance == 0) {  // depleted
    if (gHttpHandler->ConnMgr()->CurrentBrowserId() != mBrowserId) {
      nsHttp::NotifyActiveTabLoadOptimization();
    }

    // Must remember that we have to call ResumeRecv() on our connection when
    // called back by the conn manager to resume reading.
    LOG(("nsHttpTransaction::WriteSegments %p response throttled", this));
    mReadingStopped = true;
    // This makes the underlaying connection or stream wait for explicit resume.
    // For h1 this means we stop reading from the socket.
    // For h2 this means we stop updating recv window for the stream.
    return NS_BASE_STREAM_WOULD_BLOCK;
  }

  mWriter = writer;

  if (!mPipeOut) {
    return NS_ERROR_UNEXPECTED;
  }

  if (mThrottlingReadAllowance > 0) {
    LOG(("nsHttpTransaction::WriteSegments %p limiting read from %u to %d",
         this, count, mThrottlingReadAllowance));
    count = std::min(count, static_cast<uint32_t>(mThrottlingReadAllowance));
  }

  nsresult rv =
      mPipeOut->WriteSegments(WritePipeSegment, this, count, countWritten);

  mWriter = nullptr;

  if (mForceRestart) {
    // The forceRestart condition was dealt with on the stack, but it did not
    // clear the flag because nsPipe in the writesegment stack clears out
    // return codes, so we need to use the flag here as a cue to return
    // ERETARGETED
    if (NS_SUCCEEDED(rv)) {
      rv = NS_BINDING_RETARGETED;
    }
    mForceRestart = false;
  }

  // if pipe would block then we need to AsyncWait on it.  have callback
  // occur on socket thread so we stay synchronized.
  if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
    nsCOMPtr<nsIEventTarget> target;
    (void)gHttpHandler->GetSocketThreadTarget(getter_AddRefs(target));
    if (target) {
      mPipeOut->AsyncWait(this, 0, 0, target);
      mWaitingOnPipeOut = true;
    } else {
      NS_ERROR("no socket thread event target");
      rv = NS_ERROR_UNEXPECTED;
    }
  } else if (mThrottlingReadAllowance > 0 && NS_SUCCEEDED(rv)) {
    MOZ_ASSERT(count >= *countWritten);
    mThrottlingReadAllowance -= *countWritten;
  }

  return rv;
}

bool nsHttpTransaction::ProxyConnectFailed() { return mProxyConnectFailed; }

bool nsHttpTransaction::DataSentToChildProcess() { return false; }

already_AddRefed<nsITransportSecurityInfo> nsHttpTransaction::SecurityInfo() {
  MutexAutoLock lock(mLock);
  return do_AddRef(mSecurityInfo);
}

bool nsHttpTransaction::HasStickyConnection() const {
  return mCaps & NS_HTTP_STICKY_CONNECTION;
}

bool nsHttpTransaction::ResponseIsComplete() { return mResponseIsComplete; }

int64_t nsHttpTransaction::GetTransferSize() { return mTransferSize; }

int64_t nsHttpTransaction::GetRequestSize() { return mRequestSize; }

bool nsHttpTransaction::IsHttp3Used() { return mIsHttp3Used; }

bool nsHttpTransaction::Http2Disabled() const {
  return mCaps & NS_HTTP_DISALLOW_SPDY;
}

bool nsHttpTransaction::Http3Disabled() const {
  return mCaps & NS_HTTP_DISALLOW_HTTP3;
}

already_AddRefed<nsHttpConnectionInfo> nsHttpTransaction::GetConnInfo() const {
  RefPtr<nsHttpConnectionInfo> connInfo = mConnInfo->Clone();
  return connInfo.forget();
}

nsHttpTransaction* nsHttpTransaction::AsHttpTransaction() { return this; }

HttpTransactionParent* nsHttpTransaction::AsHttpTransactionParent() {
  return nullptr;
}

nsHttpTransaction::HTTPSSVC_CONNECTION_FAILED_REASON
nsHttpTransaction::ErrorCodeToFailedReason(nsresult aErrorCode) {
  HTTPSSVC_CONNECTION_FAILED_REASON reason = HTTPSSVC_CONNECTION_OTHERS;
  switch (aErrorCode) {
    case NS_ERROR_UNKNOWN_HOST:
      reason = HTTPSSVC_CONNECTION_UNKNOWN_HOST;
      break;
    case NS_ERROR_CONNECTION_REFUSED:
      reason = HTTPSSVC_CONNECTION_UNREACHABLE;
      break;
    default:
      if (m421Received) {
        reason = HTTPSSVC_CONNECTION_421_RECEIVED;
      } else if (NS_ERROR_GET_MODULE(aErrorCode) == NS_ERROR_MODULE_SECURITY) {
        reason = HTTPSSVC_CONNECTION_SECURITY_ERROR;
      }
      break;
  }
  return reason;
}

bool nsHttpTransaction::PrepareSVCBRecordsForRetry(
    const nsACString& aFailedDomainName, const nsACString& aFailedAlpn,
    bool& aAllRecordsHaveEchConfig) {
  MOZ_ASSERT(mRecordsForRetry.IsEmpty());
  if (!mHTTPSSVCRecord) {
    return false;
  }

  // If we already failed to connect with h3, don't select records that supports
  // h3.
  bool noHttp3 = mCaps & NS_HTTP_DISALLOW_HTTP3;

  bool unused;
  nsTArray<RefPtr<nsISVCBRecord>> records;
  (void)mHTTPSSVCRecord->GetAllRecordsWithEchConfig(
      mCaps & NS_HTTP_DISALLOW_SPDY, noHttp3, mCname, &aAllRecordsHaveEchConfig,
      &unused, records);

  // Note that it's possible that we can't get any usable record here. For
  // example, when http3 connection is failed, we won't select records with
  // http3 alpn.

  // If not all records have echConfig, we'll directly fallback to the origin
  // server.
  if (!aAllRecordsHaveEchConfig) {
    return false;
  }

  // Take the records behind the failed one and put them into mRecordsForRetry.
  for (const auto& record : records) {
    nsAutoCString name;
    record->GetName(name);
    nsAutoCString alpn;
    nsresult rv = record->GetSelectedAlpn(alpn);

    if (name == aFailedDomainName) {
      // If the record has no alpn or the alpn is already tried, we skip this
      // record.
      if (NS_FAILED(rv) || alpn == aFailedAlpn) {
        continue;
      }
    }

    mRecordsForRetry.InsertElementAt(0, record);
  }

  // Set mHTTPSSVCRecord to null to avoid this function being executed twice.
  mHTTPSSVCRecord = nullptr;
  return !mRecordsForRetry.IsEmpty();
}

already_AddRefed<nsHttpConnectionInfo>
nsHttpTransaction::PrepareFastFallbackConnInfo(bool aEchConfigUsed) {
  MOZ_ASSERT(mHTTPSSVCRecord && mOrigConnInfo);

  RefPtr<nsHttpConnectionInfo> fallbackConnInfo;
  nsCOMPtr<nsISVCBRecord> fastFallbackRecord;
  (void)mHTTPSSVCRecord->GetServiceModeRecordWithCname(
      mCaps & NS_HTTP_DISALLOW_SPDY, true, mCname,
      getter_AddRefs(fastFallbackRecord));

  if (fastFallbackRecord && aEchConfigUsed) {
    nsAutoCString echConfig;
    (void)fastFallbackRecord->GetEchConfig(echConfig);
    if (echConfig.IsEmpty()) {
      fastFallbackRecord = nullptr;
    }
  }

  if (!fastFallbackRecord) {
    if (aEchConfigUsed) {
      LOG(
          ("nsHttpTransaction::PrepareFastFallbackConnInfo [this=%p] no record "
           "can be used",
           this));
      return nullptr;
    }

    if (mOrigConnInfo->IsHttp3()) {
      mOrigConnInfo->CloneAsDirectRoute(getter_AddRefs(fallbackConnInfo));
    } else {
      fallbackConnInfo = mOrigConnInfo;
    }
    return fallbackConnInfo.forget();
  }

  fallbackConnInfo =
      mOrigConnInfo->CloneAndAdoptHTTPSSVCRecord(fastFallbackRecord);
  return fallbackConnInfo.forget();
}

void nsHttpTransaction::PrepareConnInfoForRetry(nsresult aReason) {
  LOG(("nsHttpTransaction::PrepareConnInfoForRetry [this=%p reason=%" PRIx32
       "]",
       this, static_cast<uint32_t>(aReason)));
  RefPtr<nsHttpConnectionInfo> failedConnInfo = mConnInfo->Clone();
  mConnInfo = nullptr;
  bool echConfigUsed =
      nsHttpHandler::EchConfigEnabled(failedConnInfo->IsHttp3()) &&
      !failedConnInfo->GetEchConfig().IsEmpty();

  if (mFastFallbackTriggered) {
    mFastFallbackTriggered = false;
    MOZ_ASSERT(mBackupConnInfo);
    mConnInfo.swap(mBackupConnInfo);
    return;
  }

  auto useOrigConnInfoToRetry = [&]() {
    mOrigConnInfo.swap(mConnInfo);
    if (mConnInfo->IsHttp3() &&
        ((mCaps & NS_HTTP_DISALLOW_HTTP3) ||
         gHttpHandler->IsHttp3Excluded(mConnInfo->GetRoutedHost().IsEmpty()
                                           ? mConnInfo->GetOrigin()
                                           : mConnInfo->GetRoutedHost()))) {
      RefPtr<nsHttpConnectionInfo> ci;
      mConnInfo->CloneAsDirectRoute(getter_AddRefs(ci));
      mConnInfo = ci;
    }
  };

  if (!echConfigUsed) {
    LOG((" echConfig is not used, fallback to origin conn info"));
    useOrigConnInfoToRetry();
    return;
  }

  TRANSACTION_ECH_RETRY_COUNT id = TRANSACTION_ECH_RETRY_OTHERS_COUNT;
  auto updateCount = MakeScopeExit([&] {
    auto entry = mEchRetryCounterMap.Lookup(id);
    MOZ_ASSERT(entry, "table not initialized");
    if (entry) {
      *entry += 1;
    }
  });

  if (aReason == psm::GetXPCOMFromNSSError(SSL_ERROR_ECH_RETRY_WITHOUT_ECH)) {
    LOG((" Got SSL_ERROR_ECH_RETRY_WITHOUT_ECH, use empty echConfig to retry"));
    failedConnInfo->SetEchConfig(EmptyCString());
    failedConnInfo.swap(mConnInfo);
    id = TRANSACTION_ECH_RETRY_WITHOUT_ECH_COUNT;
    return;
  }

  if (aReason == psm::GetXPCOMFromNSSError(SSL_ERROR_ECH_RETRY_WITH_ECH)) {
    LOG((" Got SSL_ERROR_ECH_RETRY_WITH_ECH, use retry echConfig"));
    MOZ_ASSERT(mConnection);

    nsCOMPtr<nsITLSSocketControl> socketControl;
    if (mConnection) {
      mConnection->GetTLSSocketControl(getter_AddRefs(socketControl));
    }
    MOZ_ASSERT(socketControl);

    nsAutoCString retryEchConfig;
    if (socketControl &&
        NS_SUCCEEDED(socketControl->GetRetryEchConfig(retryEchConfig))) {
      MOZ_ASSERT(!retryEchConfig.IsEmpty());

      failedConnInfo->SetEchConfig(retryEchConfig);
      failedConnInfo.swap(mConnInfo);
    }
    id = TRANSACTION_ECH_RETRY_WITH_ECH_COUNT;
    return;
  }

  // Note that we retry the connection not only for SSL_ERROR_ECH_FAILED, but
  // also for all failure cases.
  if (aReason == psm::GetXPCOMFromNSSError(SSL_ERROR_ECH_FAILED) ||
      NS_FAILED(aReason)) {
    LOG((" Got SSL_ERROR_ECH_FAILED, try other records"));
    if (aReason == psm::GetXPCOMFromNSSError(SSL_ERROR_ECH_FAILED)) {
      id = TRANSACTION_ECH_RETRY_ECH_FAILED_COUNT;
    }
    if (mRecordsForRetry.IsEmpty()) {
      if (mHTTPSSVCRecord) {
        bool allRecordsHaveEchConfig = true;
        if (!PrepareSVCBRecordsForRetry(failedConnInfo->GetRoutedHost(),
                                        failedConnInfo->GetNPNToken(),
                                        allRecordsHaveEchConfig)) {
          LOG(
              (" Can't find other records with echConfig, "
               "allRecordsHaveEchConfig=%d",
               allRecordsHaveEchConfig));
          if (gHttpHandler->FallbackToOriginIfConfigsAreECHAndAllFailed() ||
              !allRecordsHaveEchConfig) {
            useOrigConnInfoToRetry();
          }
          return;
        }
      } else {
        LOG((" No available records to retry"));
        if (gHttpHandler->FallbackToOriginIfConfigsAreECHAndAllFailed()) {
          useOrigConnInfoToRetry();
        }
        return;
      }
    }

    if (LOG5_ENABLED()) {
      LOG(("SvcDomainName to retry: ["));
      for (const auto& r : mRecordsForRetry) {
        nsAutoCString name;
        r->GetName(name);
        nsAutoCString alpn;
        r->GetSelectedAlpn(alpn);
        LOG((" name=%s alpn=%s", name.get(), alpn.get()));
      }
      LOG(("]"));
    }

    RefPtr<nsISVCBRecord> recordsForRetry =
        mRecordsForRetry.PopLastElement().forget();
    mConnInfo = mOrigConnInfo->CloneAndAdoptHTTPSSVCRecord(recordsForRetry);
  }
}

void nsHttpTransaction::MaybeReportFailedSVCDomain(
    nsresult aReason, nsHttpConnectionInfo* aFailedConnInfo) {
  if (aReason == psm::GetXPCOMFromNSSError(SSL_ERROR_ECH_RETRY_WITHOUT_ECH) ||
      aReason != psm::GetXPCOMFromNSSError(SSL_ERROR_ECH_RETRY_WITH_ECH)) {
    return;
  }

  nsCOMPtr<nsIDNSService> dns = do_GetService(NS_DNSSERVICE_CONTRACTID);
  if (dns) {
    const nsCString& failedHost = aFailedConnInfo->GetRoutedHost().IsEmpty()
                                      ? aFailedConnInfo->GetOrigin()
                                      : aFailedConnInfo->GetRoutedHost();
    LOG(("add failed domain name [%s] -> [%s] to exclusion list",
         aFailedConnInfo->GetOrigin().get(), failedHost.get()));
    (void)dns->ReportFailedSVCDomainName(aFailedConnInfo->GetOrigin(),
                                         failedHost);
  }
}

void nsHttpTransaction::OnPSKResumptionAccepted() {
  LOG(("nsHttpTransaction::OnPSKResumptionAccepted [this=%p]\n", this));
  mResumptionAttempted = false;
}

bool nsHttpTransaction::ShouldRestartOnResumptionError(nsresult reason) {
  LOG(
      ("nsHttpTransaction::ShouldRestartOnResumptionError [this=%p, "
       "mResumptionAttempted=%d error=%" PRIx32 "]\n",
       this, mResumptionAttempted, static_cast<uint32_t>(reason)));
  // Also accept NS_ERROR_NET_RESET: the socket-transport teardown can win
  // the race against NSS alert propagation and surface a PSK rejection as a
  // generic connection reset.
  return StaticPrefs::network_http_early_data_disable_on_error() &&
         mResumptionAttempted &&
         (NS_ERROR_GET_MODULE(reason) == NS_ERROR_MODULE_SECURITY ||
          reason == NS_ERROR_NET_RESET);
}

static void MaybeRemoveSSLToken(nsITransportSecurityInfo* aSecurityInfo) {
  if (!StaticPrefs::
          network_http_remove_resumption_token_when_early_data_failed()) {
    return;
  }
  if (!aSecurityInfo) {
    return;
  }
  nsAutoCString key;
  aSecurityInfo->GetPeerId(key);
  nsresult rv = SSLTokensCache::RemoveAll(key);
  LOG(("RemoveSSLToken [key=%s, rv=%" PRIx32 "]", key.get(),
       static_cast<uint32_t>(rv)));
}

const int64_t TELEMETRY_REQUEST_SIZE_1M = (int64_t)(1 << 20);
const int64_t TELEMETRY_REQUEST_SIZE_10M =
    (int64_t)10 * TELEMETRY_REQUEST_SIZE_1M;
const int64_t TELEMETRY_REQUEST_SIZE_50M =
    (int64_t)50 * TELEMETRY_REQUEST_SIZE_1M;
const int64_t TELEMETRY_REQUEST_SIZE_100M =
    (int64_t)100 * TELEMETRY_REQUEST_SIZE_1M;

void nsHttpTransaction::Close(nsresult reason) {
  LOG(("nsHttpTransaction::Close [this=%p reason=%" PRIx32 "]\n", this,
       static_cast<uint32_t>(reason)));

  if (!mClosed) {
    gHttpHandler->ConnMgr()->RemoveActiveTransaction(this);
    mActivated = false;
  }

  if (mDNSRequest) {
    mDNSRequest->Cancel(NS_ERROR_ABORT);
    mDNSRequest = nullptr;
  }

  // If an HTTP/3 backup timer is active and this transaction ends in error,
  // treat it as NS_ERROR_NET_RESET so the transaction will retry once.
  // NOTE: This is a temporary workaround; the proper fix belongs in
  // the Happy Eyeballs project.
  if (NS_FAILED(reason) && AllowedErrorForTransactionRetry(reason) &&
      mHttp3BackupTimerCreated && mHttp3BackupTimer) {
    reason = NS_ERROR_NET_RESET;
  }

  MaybeCancelFallbackTimer();

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  if (reason == NS_BINDING_RETARGETED) {
    LOG(("  close %p skipped due to ERETARGETED\n", this));
    return;
  }

  if (mClosed) {
    LOG(("  already closed\n"));
    return;
  }

  NotifyTransactionObserver(reason);

  if (mTokenBucketCancel) {
    mTokenBucketCancel->Cancel(reason);
    mTokenBucketCancel = nullptr;
  }

  // report the reponse is complete if not already reported
  if (!mResponseIsComplete) {
    gHttpHandler->ObserveHttpActivityWithArgs(
        HttpActivityArgs(mChannelId), NS_HTTP_ACTIVITY_TYPE_HTTP_TRANSACTION,
        NS_HTTP_ACTIVITY_SUBTYPE_RESPONSE_COMPLETE, PR_Now(),
        static_cast<uint64_t>(mContentRead), ""_ns);
  }

  // report that this transaction is closing
  gHttpHandler->ObserveHttpActivityWithArgs(
      HttpActivityArgs(mChannelId), NS_HTTP_ACTIVITY_TYPE_HTTP_TRANSACTION,
      NS_HTTP_ACTIVITY_SUBTYPE_TRANSACTION_CLOSE, PR_Now(), 0, ""_ns);

  // we must no longer reference the connection!  find out if the
  // connection was being reused before letting it go.
  bool connReused = false;
  bool isHttp2or3 = false;
  if (mConnection) {
    connReused = mConnection->IsReused();
    isHttp2or3 = mConnection->Version() >= HttpVersion::v2_0;
    if (!mConnected) {
      MaybeRefreshSecurityInfo();
    }
  }
  mConnected = false;

  // When mDoNotRemoveAltSvc is true, this means we want to keep the AltSvc in
  // in the conncetion info. In this case, let's not apply HTTPS RR retry logic
  // to make sure this transaction can be restarted with the same conncetion
  // info.
  bool shouldRestartTransactionForHTTPSRR =
      mOrigConnInfo && AllowedErrorForTransactionRetry(reason) &&
      !mDoNotRemoveAltSvc;

  // When a PSK resumption attempt fails over a non-ECH HTTPS-RR-routed
  // connection, only the cached resumption material is bad — the route is
  // fine. Stay on the alt-route: MaybeRemoveSSLToken has already evicted the
  // PSK, so a fresh handshake on the same route should succeed. Setting
  // mDontRetryWithDirectRoute keeps Restart() from stripping the route. If
  // that retry also fails, mResumptionAttempted will have been reset and
  // the standard HTTPS-RR retry path takes over.
  //
  // Skip this for ECH connections: ECH manages its own retry chain (record
  // rotation, fallback to origin) through PrepareConnInfoForRetry, and must
  // not be blocked.
  const bool echConfigUsed =
      nsHttpHandler::EchConfigEnabled(mConnInfo->IsHttp3()) &&
      !mConnInfo->GetEchConfig().IsEmpty();
  if (shouldRestartTransactionForHTTPSRR && !echConfigUsed &&
      ShouldRestartOnResumptionError(reason)) {
    shouldRestartTransactionForHTTPSRR = false;
    mDontRetryWithDirectRoute = true;
  }

  //
  // if the connection was reset or closed before we wrote any part of the
  // request or if we wrote the request but didn't receive any part of the
  // response and the connection was being reused, then we can (and really
  // should) assume that we wrote to a stale connection and we must therefore
  // repeat the request over a new connection.
  //
  // We have decided to retry not only in case of the reused connections, but
  // all safe methods(bug 1236277).
  //
  // NOTE: the conditions under which we will automatically retry the HTTP
  // request have to be carefully selected to avoid duplication of the
  // request from the point-of-view of the server.  such duplication could
  // have dire consequences including repeated purchases, etc.
  //
  // NOTE: because of the way SSL proxy CONNECT is implemented, it is
  // possible that the transaction may have received data without having
  // sent any data.  for this reason, mSendData == FALSE does not imply
  // mReceivedData == FALSE.  (see bug 203057 for more info.)
  //
  // Never restart transactions that are marked as sticky to their conenction.
  // We use that capability to identify transactions bound to connection based
  // authentication.  Reissuing them on a different connections will break
  // this bondage.  Major issue may arise when there is an NTLM message auth
  // header on the transaction and we send it to a different NTLM authenticated
  // connection.  It will break that connection and also confuse the channel's
  // auth provider, beliving the cached credentials are wrong and asking for
  // the password mistakenly again from the user.
  if ((reason == NS_ERROR_NET_RESET || reason == NS_OK ||
       reason ==
           psm::GetXPCOMFromNSSError(SSL_ERROR_DOWNGRADE_WITH_EARLY_DATA) ||
       reason == NS_ERROR_HTTP2_FALLBACK_TO_HTTP1 ||
       ShouldRestartOnResumptionError(reason) ||
       shouldRestartTransactionForHTTPSRR) &&
      (!(mCaps & NS_HTTP_STICKY_CONNECTION) ||
       (mCaps & NS_HTTP_CONNECTION_RESTARTABLE) ||
       (mEarlyDataDisposition == EARLY_425))) {
    if (mForceRestart) {
      SetRestartReason(TRANSACTION_RESTART_FORCED);
      if (NS_SUCCEEDED(Restart())) {
        if (mResponseHead) {
          mResponseHead->Reset();
        }
        mContentRead = 0;
        mContentLength = -1;
        delete mChunkedDecoder;
        mChunkedDecoder = nullptr;
        mHaveStatusLine = false;
        mHaveAllHeaders = false;
        mHttpResponseMatched = false;
        mResponseIsComplete = false;
        mDidContentStart = false;
        mNoContent = false;
        mSentData = false;
        mReceivedData = false;
        mSupportsHTTP3 = false;
        LOG(("transaction force restarted\n"));
        return;
      }
    }

    mDoNotTryEarlyData = true;

    // reallySentData is meant to separate the instances where data has
    // been sent by this transaction but buffered at a higher level while
    // a TLS session (perhaps via a tunnel) is setup.
    bool reallySentData =
        mSentData && (!mConnection || mConnection->BytesWritten());

    // If this is true, it means we failed to use the HTTPSSVC connection info
    // to connect to the server. We need to retry with the original connection
    // info.
    shouldRestartTransactionForHTTPSRR &= !reallySentData;

    if (reason ==
            psm::GetXPCOMFromNSSError(SSL_ERROR_DOWNGRADE_WITH_EARLY_DATA) ||
        (!mReceivedData && ((mRequestHead && mRequestHead->IsSafeMethod()) ||
                            !reallySentData || connReused)) ||
        shouldRestartTransactionForHTTPSRR) {
      if (shouldRestartTransactionForHTTPSRR) {
        MaybeReportFailedSVCDomain(reason, mConnInfo);
        PrepareConnInfoForRetry(reason);
        mDontRetryWithDirectRoute = true;
        LOG(
            ("transaction will be restarted with the fallback connection info "
             "key=%s",
             mConnInfo ? mConnInfo->HashKey().get() : "None"));
      }

      if (shouldRestartTransactionForHTTPSRR) {
        auto toRestartReason =
            [](nsresult aStatus) -> TRANSACTION_RESTART_REASON {
          if (aStatus == NS_ERROR_NET_RESET) {
            return TRANSACTION_RESTART_HTTPS_RR_NET_RESET;
          }
          if (aStatus == NS_ERROR_CONNECTION_REFUSED) {
            return TRANSACTION_RESTART_HTTPS_RR_CONNECTION_REFUSED;
          }
          if (aStatus == NS_ERROR_UNKNOWN_HOST) {
            return TRANSACTION_RESTART_HTTPS_RR_UNKNOWN_HOST;
          }
          if (aStatus == NS_ERROR_NET_TIMEOUT) {
            return TRANSACTION_RESTART_HTTPS_RR_NET_TIMEOUT;
          }
          if (psm::IsNSSErrorCode(-1 * NS_ERROR_GET_CODE(aStatus))) {
            return TRANSACTION_RESTART_HTTPS_RR_SEC_ERROR;
          }
          if (aStatus == NS_ERROR_NOT_CONNECTED ||
              aStatus == NS_ERROR_SOCKET_ADDRESS_IN_USE ||
              aStatus == NS_ERROR_FILE_ALREADY_EXISTS ||
              aStatus == NS_ERROR_NET_INTERRUPT) {
            return TRANSACTION_RESTART_OTHERS;
          }
          MOZ_ASSERT_UNREACHABLE("Unexpected reason");
          return TRANSACTION_RESTART_OTHERS;
        };
        SetRestartReason(toRestartReason(reason));
      } else if (reason == psm::GetXPCOMFromNSSError(
                               SSL_ERROR_DOWNGRADE_WITH_EARLY_DATA)) {
        SetRestartReason(TRANSACTION_RESTART_DOWNGRADE_WITH_EARLY_DATA);
      } else if (mResumptionAttempted) {
        SetRestartReason(TRANSACTION_RESTART_POSSIBLE_0RTT_ERROR);
      } else if (!reallySentData) {
        SetRestartReason(TRANSACTION_RESTART_NO_DATA_SENT);
      }
      // if restarting fails, then we must proceed to close the pipe,
      // which will notify the channel that the transaction failed.
      // Note that when echConfig is enabled, it's possible that we don't have a
      // usable connection info to retry.
      if (mConnInfo && NS_SUCCEEDED(Restart())) {
        return;
      }
      // mConnInfo could be set to null in PrepareConnInfoForRetry() when we
      // can't find an available https rr to retry. We have to set mConnInfo
      // back to mOrigConnInfo to make sure no crash when mConnInfo being
      // accessed again.
      if (!mConnInfo) {
        mConnInfo.swap(mOrigConnInfo);
        MOZ_ASSERT(mConnInfo);
      }
    }
  }

  if (!mResponseIsComplete && NS_SUCCEEDED(reason) && isHttp2or3) {
    // Responses without content-length header field are still complete if
    // they are transfered over http2 or http3 and the stream is properly
    // closed.
    mResponseIsComplete = true;
  }

  if (reason == NS_ERROR_NET_RESET && mResponseIsComplete && isHttp2or3) {
    // See bug 1940663. When using HTTP/2 or HTTP/3, receiving the
    // NS_ERROR_NET_RESET error code indicates that the connection intends
    // to restart this transaction. However, if the transaction has already
    // completed and we've passed the point of restarting, we should avoid
    // propagating the error code and overwrite it to NS_OK.
    //
    // TODO: Refactor the mechanism by which a connection instructs a
    // transaction to restart. This will allow us to remove this hack.
    LOG(("Transaction is already done, overriding error code to NS_OK"));
    reason = NS_OK;
  }

  if ((mChunkedDecoder || (mContentLength >= int64_t(0))) &&
      (NS_SUCCEEDED(reason) && !mResponseIsComplete)) {
    NS_WARNING("Partial transfer, incomplete HTTP response received");

    if ((mHttpResponseCode / 100 == 2) && (mHttpVersion >= HttpVersion::v1_1)) {
      FrameCheckLevel clevel = gHttpHandler->GetEnforceH1Framing();
      if (clevel >= FRAMECHECK_BARELY) {
        // If clevel == FRAMECHECK_STRICT mark any incomplete response as
        // partial.
        // if clevel == FRAMECHECK_BARELY: 1) mark a chunked-encoded response
        // that do not ends on exactly a chunk boundary as partial; We are not
        // strict about the last 0-size chunk and do not mark as parial
        // responses that do not have the last 0-size chunk but do end on a
        // chunk boundary. (check mChunkedDecoder->GetChunkRemaining() != 0)
        // 2) mark a transfer that is partial and it is not chunk-encoded or
        // gzip-encoded or other content-encoding as partial. (check
        // !mChunkedDecoder && !mContentDecoding && mContentDecodingCheck))
        // if clevel == FRAMECHECK_STRICT_CHUNKED mark a chunked-encoded
        // response that ends on exactly a chunk boundary also as partial.
        // Here a response must have the last 0-size chunk.
        if ((clevel == FRAMECHECK_STRICT) ||
            (mChunkedDecoder && (mChunkedDecoder->GetChunkRemaining() ||
                                 (clevel == FRAMECHECK_STRICT_CHUNKED))) ||
            (!mChunkedDecoder && !mContentDecoding && mContentDecodingCheck)) {
          reason = NS_ERROR_NET_PARTIAL_TRANSFER;
          LOG(("Partial transfer, incomplete HTTP response received: %s",
               mChunkedDecoder ? "broken chunk" : "c-l underrun"));
        }
      }
    }

    if (mConnection) {
      // whether or not we generate an error for the transaction
      // bad framing means we don't want a pconn
      mConnection->DontReuse();
    }
  }

  bool relConn = true;
  if (NS_SUCCEEDED(reason)) {
    // the server has not sent the final \r\n terminating the header
    // section, and there may still be a header line unparsed.  let's make
    // sure we parse the remaining header line, and then hopefully, the
    // response will be usable (see bug 88792).
    if (!mHaveAllHeaders) {
      char data[] = "\n\n";
      uint32_t unused = 0;
      // If we have a partial line already, we actually need two \ns to finish
      // the headers section.
      (void)ParseHead(data, mLineBuf.IsEmpty() ? 1 : 2, &unused);

      if (mResponseHead->Version() == HttpVersion::v0_9) {
        // Reject 0 byte HTTP/0.9 Responses - bug 423506
        LOG(("nsHttpTransaction::Close %p 0 Byte 0.9 Response", this));
        reason = NS_ERROR_NET_RESET;
      }
    }

    // honor the sticky connection flag...
    if (mCaps & NS_HTTP_STICKY_CONNECTION) {
      LOG(("  keeping the connection because of STICKY_CONNECTION flag"));
      relConn = false;
    }

    // if the proxy connection has failed, we want the connection be held
    // to allow the upper layers (think nsHttpChannel) to close it when
    // the failure is unrecoverable.
    // we can't just close it here, because mProxyConnectFailed is to a general
    // flag and is also set for e.g. 407 which doesn't mean to kill the
    // connection, specifically when connection oriented auth may be involved.
    if (mProxyConnectFailed) {
      LOG(("  keeping the connection because of mProxyConnectFailed"));
      relConn = false;
    }

    // Use mOrigConnInfo as an indicator that this transaction is completed
    // successfully with an HTTPSSVC record.
    if (mConnection) {
      RefPtr<HttpConnectionBase> conn = mConnection->HttpConnection();
      if (conn) {
        conn->RecordConnectionAddressType();
      }
    }
  }

  // mTimings.responseEnd is normally recorded based on the end of a
  // HTTP delimiter such as chunked-encodings or content-length. However,
  // EOF or an error still require an end time be recorded.

  const TimingStruct timings = Timings();
  if (timings.responseEnd.IsNull() && !timings.responseStart.IsNull()) {
    SetResponseEnd(TimeStamp::Now());
  }

  if (!timings.requestStart.IsNull() && !timings.responseEnd.IsNull()) {
    TimeDuration elapsed = timings.responseEnd - timings.requestStart;
    double megabits = static_cast<double>(mContentRead) * 8.0 / 1000000.0;
    uint32_t mbps = static_cast<uint32_t>(megabits / elapsed.ToSeconds());
    nsAutoCString serverKey;

    switch (mHttpVersion) {
      case HttpVersion::v1_0:
      case HttpVersion::v1_1: {
        if (NS_SUCCEEDED(reason)) {
          serverKey.Assign(mServerHeader.EqualsLiteral("cloudflare")
                               ? "h1_cloudflare"_ns
                               : "h1_others"_ns);
        }
        if (mContentRead > TELEMETRY_REQUEST_SIZE_10M) {
          glean::networking::http_1_download_throughput.AccumulateSingleSample(
              mbps);
          if (mContentRead <= TELEMETRY_REQUEST_SIZE_50M) {
            glean::networking::http_1_download_throughput_10_50
                .AccumulateSingleSample(mbps);
          } else if (mContentRead <= TELEMETRY_REQUEST_SIZE_100M) {
            glean::networking::http_1_download_throughput_50_100
                .AccumulateSingleSample(mbps);
          } else {
            glean::networking::http_1_download_throughput_100
                .AccumulateSingleSample(mbps);
          }
        }
        break;
      }
      case HttpVersion::v2_0: {
        if (NS_SUCCEEDED(reason)) {
          serverKey.Assign(mServerHeader.EqualsLiteral("cloudflare")
                               ? "h2_cloudflare"_ns
                               : "h2_others"_ns);
        }
        if (mContentRead > TELEMETRY_REQUEST_SIZE_10M) {
          if (mContentRead <= TELEMETRY_REQUEST_SIZE_50M) {
            glean::networking::http_2_download_throughput_10_50
                .AccumulateSingleSample(mbps);
          } else if (mContentRead <= TELEMETRY_REQUEST_SIZE_100M) {
            glean::networking::http_2_download_throughput_50_100
                .AccumulateSingleSample(mbps);
          } else {
            glean::networking::http_2_download_throughput_100
                .AccumulateSingleSample(mbps);
          }
        }
        break;
      }
      case HttpVersion::v3_0: {
        if (NS_SUCCEEDED(reason)) {
          serverKey.Assign(mServerHeader.EqualsLiteral("cloudflare")
                               ? "h3_cloudflare"_ns
                               : "h3_others"_ns);
        }
        if (mContentRead > TELEMETRY_REQUEST_SIZE_10M) {
          if (mContentRead <= TELEMETRY_REQUEST_SIZE_50M) {
            glean::networking::http_3_download_throughput_10_50
                .AccumulateSingleSample(mbps);
          } else if (mContentRead <= TELEMETRY_REQUEST_SIZE_100M) {
            glean::networking::http_3_download_throughput_50_100
                .AccumulateSingleSample(mbps);
          } else {
            glean::networking::http_3_download_throughput_100
                .AccumulateSingleSample(mbps);
          }
        }
        break;
      }
      default:
        break;
    }
  }

  if (mTrafficCategory != HttpTrafficCategory::eInvalid) {
    HttpTrafficAnalyzer* hta = gHttpHandler->GetHttpTrafficAnalyzer();
    if (hta) {
      hta->AccumulateHttpTransferredSize(mTrafficCategory, mTransferSize,
                                         mContentRead);
    }
  }

  if (isHttp2or3 &&
      reason == psm::GetXPCOMFromNSSError(SSL_ERROR_PROTOCOL_VERSION_ALERT)) {
    // Change reason to NS_ERROR_ABORT, so we avoid showing a missleading
    // error page tthat TLS1.0 is disabled. H2 or H3 is used here so the
    // TLS version is not a problem.
    reason = NS_ERROR_ABORT;
  }
  mStatus = reason;
  mTransactionDone = true;  // forcibly flag the transaction as complete
  mClosed = true;
  if (mResolver) {
    mResolver->Close();
    mResolver = nullptr;
  }

  {
    MutexAutoLock lock(mLock);
    mEarlyHintObserver = nullptr;
    mWebTransportSessionEventListener = nullptr;
    if (relConn && mConnection) {
      mConnection = nullptr;
    }
  }

  ReleaseBlockingTransaction();

  // release some resources that we no longer need
  mRequestStream = nullptr;
  mReqHeaderBuf.Truncate();
  mLineBuf.Truncate();
  if (mChunkedDecoder) {
    delete mChunkedDecoder;
    mChunkedDecoder = nullptr;
  }

  for (const auto& entry : mEchRetryCounterMap) {
    switch (entry.GetKey()) {
      case TRANSACTION_ECH_RETRY_OTHERS_COUNT:
        glean::http::transaction_ech_retry_others_count.AccumulateSingleSample(
            entry.GetData());
        break;
      case TRANSACTION_ECH_RETRY_WITH_ECH_COUNT:
        glean::http::transaction_ech_retry_with_ech_count
            .AccumulateSingleSample(entry.GetData());
        break;
      case TRANSACTION_ECH_RETRY_WITHOUT_ECH_COUNT:
        glean::http::transaction_ech_retry_without_ech_count
            .AccumulateSingleSample(entry.GetData());
        break;
      case TRANSACTION_ECH_RETRY_ECH_FAILED_COUNT:
        glean::http::transaction_ech_retry_ech_failed_count
            .AccumulateSingleSample(entry.GetData());
        break;
    }
  }

  // closing this pipe triggers the channel's OnStopRequest method.
  mPipeOut->CloseWithStatus(reason);
}

nsHttpConnectionInfo* nsHttpTransaction::ConnectionInfo() {
  return mConnInfo.get();
}

bool  // NOTE BASE CLASS
nsAHttpTransaction::ResponseTimeoutEnabled() const {
  return false;
}

PRIntervalTime  // NOTE BASE CLASS
nsAHttpTransaction::ResponseTimeout() {
  return gHttpHandler->ResponseTimeout();
}

bool nsHttpTransaction::ResponseTimeoutEnabled() const {
  return mResponseTimeoutEnabled;
}

//-----------------------------------------------------------------------------
// nsHttpTransaction <private>
//-----------------------------------------------------------------------------

static inline void RemoveAlternateServiceUsedHeader(
    nsHttpRequestHead* aRequestHead) {
  if (aRequestHead) {
    DebugOnly<nsresult> rv =
        aRequestHead->SetHeader(nsHttp::Alternate_Service_Used, "0"_ns);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
  }
}

void nsHttpTransaction::FinalizeConnInfo() {
  RefPtr<nsHttpConnectionInfo> cloned = mConnInfo->Clone();
  {
    MutexAutoLock lock(mLock);
    mFinalizedConnInfo.swap(cloned);
  }
}

void nsHttpTransaction::SetRestartReason(TRANSACTION_RESTART_REASON aReason) {
  if (mRestartReason == TRANSACTION_RESTART_NONE) {
    mRestartReason = aReason;
  }
}

nsresult nsHttpTransaction::Restart() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  // limit the number of restart attempts - bug 92224
  if (++mRestartCount >= gHttpHandler->MaxRequestAttempts()) {
    LOG(("reached max request attempts, failing transaction @%p\n", this));
    return NS_ERROR_NET_RESET;
  }

  // Let's not restart during shutdown.
  if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
    return NS_ERROR_ILLEGAL_DURING_SHUTDOWN;
  }

  LOG(("restarting transaction @%p\n", this));

  if (mRequestHead) {
    // Dispatching on a new connection better w/o an ambient connection proxy
    // auth request header to not confuse the proxy authenticator.
    nsAutoCString proxyAuth;
    if (NS_SUCCEEDED(
            mRequestHead->GetHeader(nsHttp::Proxy_Authorization, proxyAuth)) &&
        IsStickyAuthSchemeAt(proxyAuth)) {
      (void)mRequestHead->ClearHeader(nsHttp::Proxy_Authorization);
    }
  }

  // rewind streams in case we already wrote out the request
  nsCOMPtr<nsISeekableStream> seekable = do_QueryInterface(mRequestStream);
  if (seekable) seekable->Seek(nsISeekableStream::NS_SEEK_SET, 0);

  if (mDoNotTryEarlyData) {
    MutexAutoLock lock(mLock);
    MaybeRemoveSSLToken(mSecurityInfo);
  }

  // clear old connection state...
  {
    MutexAutoLock lock(mLock);
    mSecurityInfo = nullptr;
  }

  if (mConnection) {
    if (!mReuseOnRestart) {
      mConnection->DontReuse();
    }
    MutexAutoLock lock(mLock);
    mConnection = nullptr;
  }

  // Reset this to our default state, since this may change from one restart
  // to the next
  mReuseOnRestart = false;

  if (!mDoNotRemoveAltSvc && !mDontRetryWithDirectRoute) {
    if (mConnInfo->IsHttp3ProxyConnection()) {
      RefPtr<nsHttpConnectionInfo> ci =
          mConnInfo->CreateConnectUDPFallbackConnInfo();
      mConnInfo = ci;
      RemoveAlternateServiceUsedHeader(mRequestHead);
    } else if (!mConnInfo->GetRoutedHost().IsEmpty() || mConnInfo->IsHttp3()) {
      RefPtr<nsHttpConnectionInfo> ci;
      mConnInfo->CloneAsDirectRoute(getter_AddRefs(ci));
      mConnInfo = ci;
      RemoveAlternateServiceUsedHeader(mRequestHead);
    }
  }

  // Reset mDoNotRemoveAltSvc for the next try.
  mDoNotRemoveAltSvc = false;
  mResumptionAttempted = false;
  mRestarted = true;

  // If we weren't trying to do 'proper' ECH, disable ECH GREASE when retrying.
  if (mConnInfo->GetEchConfig().IsEmpty() &&
      StaticPrefs::security_tls_ech_disable_grease_on_fallback()) {
    mCaps |= NS_HTTP_DISALLOW_ECH;
  }

  mCaps |= NS_HTTP_IS_RETRY;

  // Use TRANSACTION_RESTART_OTHERS as a catch-all.
  SetRestartReason(TRANSACTION_RESTART_OTHERS);

  if (!mDoNotResetIPFamilyPreference) {
    // Reset the IP family preferences, so the new connection can try to use
    // another IPv4 or IPv6 address.
    gHttpHandler->ConnMgr()->ResetIPFamilyPreference(mConnInfo);
  }

  return gHttpHandler->InitiateTransaction(this, mPriority);
}

bool nsHttpTransaction::TakeRestartedState() {
  // This return true if the transaction has been restarted internally.  Used to
  // let the consuming nsHttpChannel reset proxy authentication.  The flag is
  // reset to false by this method.
  return mRestarted.exchange(false);
}

char* nsHttpTransaction::LocateHttpStart(char* buf, uint32_t len,
                                         bool aAllowPartialMatch) {
  MOZ_ASSERT(!aAllowPartialMatch || mLineBuf.IsEmpty());

  static const char HTTPHeader[] = "HTTP/1.";
  static const uint32_t HTTPHeaderLen = sizeof(HTTPHeader) - 1;
  static const char HTTP2Header[] = "HTTP/2";
  static const uint32_t HTTP2HeaderLen = sizeof(HTTP2Header) - 1;
  static const char HTTP3Header[] = "HTTP/3";
  static const uint32_t HTTP3HeaderLen = sizeof(HTTP3Header) - 1;
  // ShoutCast ICY is treated as HTTP/1.0
  static const char ICYHeader[] = "ICY ";
  static const uint32_t ICYHeaderLen = sizeof(ICYHeader) - 1;

  if (aAllowPartialMatch && (len < HTTPHeaderLen)) {
    return (nsCRT::strncasecmp(buf, HTTPHeader, len) == 0) ? buf : nullptr;
  }

  // mLineBuf can contain partial match from previous search
  if (!mLineBuf.IsEmpty()) {
    MOZ_ASSERT(mLineBuf.Length() < HTTPHeaderLen);
    int32_t checkChars =
        std::min<uint32_t>(len, HTTPHeaderLen - mLineBuf.Length());
    if (nsCRT::strncasecmp(buf, HTTPHeader + mLineBuf.Length(), checkChars) ==
        0) {
      mLineBuf.Append(buf, checkChars);
      if (mLineBuf.Length() == HTTPHeaderLen) {
        // We've found whole HTTPHeader sequence. Return pointer at the
        // end of matched sequence since it is stored in mLineBuf.
        return (buf + checkChars);
      }
      // Response matches pattern but is still incomplete.
      return nullptr;
    }
    // Previous partial match together with new data doesn't match the
    // pattern. Start the search again.
    mLineBuf.Truncate();
  }

  bool firstByte = true;
  while (len > 0) {
    if (nsCRT::strncasecmp(buf, HTTPHeader,
                           std::min<uint32_t>(len, HTTPHeaderLen)) == 0) {
      if (len < HTTPHeaderLen) {
        // partial HTTPHeader sequence found
        // save partial match to mLineBuf
        mLineBuf.Assign(buf, len);
        return nullptr;
      }

      // whole HTTPHeader sequence found
      return buf;
    }

    // At least "SmarterTools/2.0.3974.16813" generates nonsensical
    // HTTP/2.0 responses to our HTTP/1 requests. Treat the minimal case of
    // it as HTTP/1.1 to be compatible with old versions of ourselves and
    // other browsers

    if (firstByte && !mInvalidResponseBytesRead && len >= HTTP2HeaderLen &&
        (nsCRT::strncasecmp(buf, HTTP2Header, HTTP2HeaderLen) == 0)) {
      LOG(("nsHttpTransaction:: Identified HTTP/2.0 treating as 1.x\n"));
      return buf;
    }

    // HTTP/3.0 responses to our HTTP/1 requests. Treat the minimal case of
    // it as HTTP/1.1 to be compatible with old versions of ourselves and
    // other browsers

    if (firstByte && !mInvalidResponseBytesRead && len >= HTTP3HeaderLen &&
        (nsCRT::strncasecmp(buf, HTTP3Header, HTTP3HeaderLen) == 0)) {
      LOG(("nsHttpTransaction:: Identified HTTP/3.0 treating as 1.x\n"));
      return buf;
    }

    // Treat ICY (AOL/Nullsoft ShoutCast) non-standard header in same fashion
    // as HTTP/2.0 is treated above. This will allow "ICY " to be interpretted
    // as HTTP/1.0 in nsHttpResponseHead::ParseVersion

    if (firstByte && !mInvalidResponseBytesRead && len >= ICYHeaderLen &&
        (nsCRT::strncasecmp(buf, ICYHeader, ICYHeaderLen) == 0)) {
      LOG(("nsHttpTransaction:: Identified ICY treating as HTTP/1.0\n"));
      return buf;
    }

    if (!nsCRT::IsAsciiSpace(*buf)) firstByte = false;
    buf++;
    len--;
  }
  return nullptr;
}

nsresult nsHttpTransaction::ParseLine(nsACString& line) {
  LOG1(("nsHttpTransaction::ParseLine [%s]\n", PromiseFlatCString(line).get()));
  nsresult rv = NS_OK;

  if (!mHaveStatusLine) {
    rv = mResponseHead->ParseStatusLine(line);
    if (NS_SUCCEEDED(rv)) {
      mHaveStatusLine = true;
    }
    // XXX this should probably never happen
    if (mResponseHead->Version() == HttpVersion::v0_9) mHaveAllHeaders = true;
  } else {
    rv = mResponseHead->ParseHeaderLine(line);
  }
  return rv;
}

nsresult nsHttpTransaction::ParseLineSegment(char* segment, uint32_t len) {
  MOZ_ASSERT(!mHaveAllHeaders, "already have all headers");

  if (!mLineBuf.IsEmpty() && mLineBuf.Last() == '\n') {
    // trim off the new line char, and if this segment is
    // not a continuation of the previous or if we haven't
    // parsed the status line yet, then parse the contents
    // of mLineBuf.
    mLineBuf.Truncate(mLineBuf.Length() - 1);
    if (!mHaveStatusLine || (*segment != ' ' && *segment != '\t')) {
      nsresult rv = ParseLine(mLineBuf);
      mLineBuf.Truncate();
      if (NS_FAILED(rv)) {
        return rv;
      }
    }
  }

  // append segment to mLineBuf...
  mLineBuf.Append(segment, len);

  // a line buf with only a new line char signifies the end of headers.
  if (mLineBuf.First() == '\n') {
    mLineBuf.Truncate();
    // discard this response if it is a 100 continue or other 1xx status.
    uint16_t status = mResponseHead->Status();

    // Capture timing for interim (1xx) vs final responses
    if (status / 100 == 1) {
      if (GetFirstInterimResponseStart().IsNull()) {
        TimeStamp responseStart = GetResponseStart();
        if (responseStart.IsNull()) {
          responseStart = TimeStamp::Now();
        }
        SetFirstInterimResponseStart(responseStart, true);
        SetResponseStart(responseStart, false);
      }
    } else {
      TimeStamp firstInterim = GetFirstInterimResponseStart();
      TimeStamp finalStart =
          firstInterim.IsNull() ? GetResponseStart() : TimeStamp::Now();
      if (finalStart.IsNull()) {
        finalStart = TimeStamp::Now();
      }
      SetFinalResponseHeadersStart(finalStart, true);

      // Keep responseStart aligned with spec: first interim if any, otherwise
      // final response header start.
      if (!firstInterim.IsNull()) {
        SetResponseStart(firstInterim, false);
      } else {
        SetResponseStart(finalStart, false);
      }
    }

    if (status == 103 &&
        (StaticPrefs::network_early_hints_over_http_v1_1_enabled() ||
         mResponseHead->Version() != HttpVersion::v1_1)) {
      // Observe Early Hints info for interfacing with Devtools
      ReportResponseHeader(NS_HTTP_ACTIVITY_SUBTYPE_EARLYHINT_RESPONSE_HEADER);

      nsCString linkHeader;
      nsresult rv = mResponseHead->GetHeader(nsHttp::Link, linkHeader);

      nsCString referrerPolicy;
      (void)mResponseHead->GetHeader(nsHttp::Referrer_Policy, referrerPolicy);

      if (NS_SUCCEEDED(rv) && !linkHeader.IsEmpty()) {
        nsCString cspHeader;
        (void)mResponseHead->GetHeader(nsHttp::Content_Security_Policy,
                                       cspHeader);

        nsCOMPtr<nsIEarlyHintObserver> earlyHint;
        {
          MutexAutoLock lock(mLock);
          earlyHint = mEarlyHintObserver;
        }
        if (earlyHint) {
          DebugOnly<nsresult> rv = NS_DispatchToMainThread(
              NS_NewRunnableFunction(
                  "nsIEarlyHintObserver->EarlyHint",
                  [obs{std::move(earlyHint)}, header{std::move(linkHeader)},
                   referrerPolicy{std::move(referrerPolicy)},
                   cspHeader{std::move(cspHeader)}]() {
                    obs->EarlyHint(header, referrerPolicy, cspHeader);
                  }),
              NS_DISPATCH_NORMAL);
          MOZ_ASSERT(NS_SUCCEEDED(rv));
        }
      }
    }
    if ((status != 101) && (status / 100 == 1)) {
      LOG(("ignoring 1xx response except 101 and 103\n"));
      mHaveStatusLine = false;
      mHttpResponseMatched = false;
      mConnection->SetLastTransactionExpectedNoContent(true);
      mResponseHead->Reset();
      return NS_OK;
    }
    if (!mConnection->IsProxyConnectInProgress()) {
      MutexAutoLock lock(mLock);
      mEarlyHintObserver = nullptr;
    }
    mHaveAllHeaders = true;
  }
  return NS_OK;
}

nsresult nsHttpTransaction::ParseHead(char* buf, uint32_t count,
                                      uint32_t* countRead) {
  nsresult rv;
  uint32_t len;
  char* eol;

  LOG(("nsHttpTransaction::ParseHead [count=%u]\n", count));

  *countRead = 0;

  MOZ_ASSERT(!mHaveAllHeaders, "oops");

  // allocate the response head object if necessary
  if (!mResponseHead) {
    mResponseHead = new nsHttpResponseHead();
    if (!mResponseHead) return NS_ERROR_OUT_OF_MEMORY;

    // report that we have a least some of the response
    if (!mReportedStart) {
      mReportedStart = true;
      gHttpHandler->ObserveHttpActivityWithArgs(
          HttpActivityArgs(mChannelId), NS_HTTP_ACTIVITY_TYPE_HTTP_TRANSACTION,
          NS_HTTP_ACTIVITY_SUBTYPE_RESPONSE_START, PR_Now(), 0, ""_ns);
    }
  }

  if (!mHttpResponseMatched) {
    // Normally we insist on seeing HTTP/1.x in the first few bytes,
    // but if we are on a persistent connection and the previous transaction
    // was not supposed to have any content then we need to be prepared
    // to skip over a response body that the server may have sent even
    // though it wasn't allowed.
    if (!mConnection || !mConnection->LastTransactionExpectedNoContent()) {
      // tolerate only minor junk before the status line
      mHttpResponseMatched = true;
      char* p = LocateHttpStart(buf, std::min<uint32_t>(count, 11), true);
      if (!p) {
        // Treat any 0.9 style response of a put as a failure.
        if (mRequestHead->IsPut()) return NS_ERROR_ABORT;

        if (NS_FAILED(mResponseHead->ParseStatusLine(""_ns))) {
          return NS_ERROR_FAILURE;
        }
        mHaveStatusLine = true;
        mHaveAllHeaders = true;
        return NS_OK;
      }
      if (p > buf) {
        // skip over the junk
        mInvalidResponseBytesRead += p - buf;
        *countRead = p - buf;
        buf = p;
      }
    } else {
      char* p = LocateHttpStart(buf, count, false);
      if (p) {
        mInvalidResponseBytesRead += p - buf;
        *countRead = p - buf;
        buf = p;
        mHttpResponseMatched = true;
      } else {
        mInvalidResponseBytesRead += count;
        *countRead = count;
        if (mInvalidResponseBytesRead > MAX_INVALID_RESPONSE_BODY_SIZE) {
          LOG(
              ("nsHttpTransaction::ParseHead() "
               "Cannot find Response Header\n"));
          // cannot go back and call this 0.9 anymore as we
          // have thrown away a lot of the leading junk
          return NS_ERROR_ABORT;
        }
        return NS_OK;
      }
    }
  }
  // otherwise we can assume that we don't have a HTTP/0.9 response.

  MOZ_ASSERT(mHttpResponseMatched);
  while ((eol = static_cast<char*>(memchr(buf, '\n', count - *countRead))) !=
         nullptr) {
    // found line in range [buf:eol]
    len = eol - buf + 1;

    *countRead += len;

    // actually, the line is in the range [buf:eol-1]
    if ((eol > buf) && (*(eol - 1) == '\r')) len--;

    buf[len - 1] = '\n';
    rv = ParseLineSegment(buf, len);
    if (NS_FAILED(rv)) return rv;

    if (mHaveAllHeaders) return NS_OK;

    // skip over line
    buf = eol + 1;

    if (!mHttpResponseMatched) {
      // a 100 class response has caused us to throw away that set of
      // response headers and look for the next response
      return NS_ERROR_NET_INTERRUPT;
    }
  }

  // do something about a partial header line
  if (!mHaveAllHeaders && (len = count - *countRead)) {
    *countRead = count;
    // ignore a trailing carriage return, and don't bother calling
    // ParseLineSegment if buf only contains a carriage return.
    if ((buf[len - 1] == '\r') && (--len == 0)) return NS_OK;
    rv = ParseLineSegment(buf, len);
    if (NS_FAILED(rv)) return rv;
  }
  return NS_OK;
}

bool nsHttpTransaction::HandleWebTransportResponse(uint16_t aStatus) {
  MOZ_ASSERT(mIsForWebTransport);
  if (!(aStatus >= 200 && aStatus < 300)) {
    return false;
  }
  LOG(("HandleWebTransportResponse mConnection=%p", mConnection.get()));
  RefPtr<WebTransportSessionBase> wtSession =
      mConnection->GetWebTransportSession(this);
  if (!wtSession) {
    return false;
  }

  nsCOMPtr<WebTransportSessionEventListener> webTransportListener;
  {
    MutexAutoLock lock(mLock);
    webTransportListener = mWebTransportSessionEventListener;
    mWebTransportSessionEventListener = nullptr;
  }
  if (nsCOMPtr<WebTransportSessionEventListenerInternal> listener =
          do_QueryInterface(webTransportListener)) {
    listener->OnSessionReadyInternal(wtSession);
    wtSession->SetWebTransportSessionEventListener(webTransportListener);
  }

  return true;
}

nsresult nsHttpTransaction::HandleContentStart() {
  LOG(("nsHttpTransaction::HandleContentStart [this=%p]\n", this));
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  if (mResponseHead) {
    if (mEarlyDataDisposition == EARLY_ACCEPTED) {
      if (mResponseHead->Status() == 425) {
        // We will report this state when the final responce arrives.
        mEarlyDataDisposition = EARLY_425;
      } else {
        (void)mResponseHead->SetHeader(nsHttp::X_Firefox_Early_Data,
                                       "accepted"_ns);
      }
    } else if (mEarlyDataDisposition == EARLY_SENT) {
      (void)mResponseHead->SetHeader(nsHttp::X_Firefox_Early_Data, "sent"_ns);
    } else if (mEarlyDataDisposition == EARLY_425) {
      (void)mResponseHead->SetHeader(nsHttp::X_Firefox_Early_Data,
                                     "received 425"_ns);
      mEarlyDataDisposition = EARLY_NONE;
    }  // no header on NONE case

    if (LOG3_ENABLED()) {
      LOG3(("http response [\n"));
      nsAutoCString headers;
      mResponseHead->Flatten(headers, false);
      headers.AppendLiteral("  OriginalHeaders");
      headers.AppendLiteral("\r\n");
      mResponseHead->FlattenNetworkOriginalHeaders(headers);
      LogHeaders(headers.get());
      LOG3(("]\n"));
    }

    CheckForStickyAuthScheme();

    // Save http version, mResponseHead isn't available anymore after
    // TakeResponseHead() is called
    mHttpVersion = mResponseHead->Version();
    mHttpResponseCode = mResponseHead->Status();

    // notify the connection, give it a chance to cause a reset.
    bool reset = false;
    nsresult rv = mConnection->OnHeadersAvailable(this, mRequestHead,
                                                  mResponseHead, &reset);
    NS_ENSURE_SUCCESS(rv, rv);

    // looks like we should ignore this response, resetting...
    if (reset) {
      LOG(("resetting transaction's response head\n"));
      mHaveAllHeaders = false;
      mHaveStatusLine = false;
      mReceivedData = false;
      mSentData = false;
      mHttpResponseMatched = false;
      mResponseHead->Reset();
      // wait to be called again...
      return NS_OK;
    }

    (void)mResponseHead->GetHeader(nsHttp::Server, mServerHeader);

    bool responseChecked = false;
    if (mIsForWebTransport) {
      responseChecked = HandleWebTransportResponse(mResponseHead->Status());
      LOG(("HandleWebTransportResponse res=%d", responseChecked));
      if (responseChecked) {
        mNoContent = true;
        mPreserveStream = true;
      }
    }

    if (!responseChecked) {
      // check if this is a no-content response
      switch (mResponseHead->Status()) {
        case 101:
          mPreserveStream = true;
          [[fallthrough]];  // to other no content cases:
        case 204:
        case 205:
        case 304:
          mNoContent = true;
          LOG(("this response should not contain a body.\n"));
          break;
        case 408:
          LOG(("408 Server Timeouts"));

          if (mConnection->Version() >= HttpVersion::v2_0) {
            mForceRestart = true;
            return NS_ERROR_NET_RESET;
          }

          // If this error could be due to a persistent connection
          // reuse then we pass an error code of NS_ERROR_NET_RESET
          // to trigger the transaction 'restart' mechanism.  We
          // tell it to reset its response headers so that it will
          // be ready to receive the new response.
          LOG(("408 Server Timeouts now=%d lastWrite=%d", PR_IntervalNow(),
               mConnection->LastWriteTime()));
          if ((PR_IntervalNow() - mConnection->LastWriteTime()) >=
              PR_MillisecondsToInterval(1000)) {
            mForceRestart = true;
            return NS_ERROR_NET_RESET;
          }
          break;
        case 421:
          LOG(("Misdirected Request.\n"));
          gHttpHandler->ClearHostMapping(mConnInfo);

          m421Received = true;
          mCaps |= NS_HTTP_REFRESH_DNS;

          // retry on a new connection - just in case
          // See bug 1609410, we can't restart the transaction when
          // NS_HTTP_STICKY_CONNECTION is set. In the case that a connection
          // already passed NTLM authentication, restarting the transaction will
          // cause the connection to be closed.
          if (!mRestartCount && !(mCaps & NS_HTTP_STICKY_CONNECTION)) {
            mCaps &= ~NS_HTTP_ALLOW_KEEPALIVE;
            mForceRestart = true;  // force restart has built in loop protection
            return NS_ERROR_NET_RESET;
          }
          break;
        case 425:
          LOG(("Too Early."));
          if ((mEarlyDataDisposition == EARLY_425) && !mDoNotTryEarlyData) {
            mDoNotTryEarlyData = true;
            mForceRestart = true;  // force restart has built in loop protection
            if (mConnection->Version() >= HttpVersion::v2_0) {
              mReuseOnRestart = true;
            }
            return NS_ERROR_NET_RESET;
          }
          break;
      }
    }

    // Remember whether HTTP3 is supported
    mSupportsHTTP3 = nsHttpHandler::IsHttp3SupportedByServer(mResponseHead);

    CollectTelemetryForUploads();

    // If we're only connecting then we're going to be upgrading this
    // connection since we were successful. Any data from now on belongs to
    // the upgrade handler. If we're not successful the content body doesn't
    // matter. Proxy http errors are treated as network errors. This
    // connection won't be reused since it's marked sticky and no
    // keep-alive.
    if (mCaps & NS_HTTP_CONNECT_ONLY) {
      MOZ_ASSERT(!(mCaps & NS_HTTP_ALLOW_KEEPALIVE) &&
                     (mCaps & NS_HTTP_STICKY_CONNECTION),
                 "connection should be sticky and no keep-alive");
      // The transaction will expect the server to close the socket if
      // there's no content length instead of doing the upgrade.
      mNoContent = true;
    }

    // preserve connection for tunnel setup - h2 websocket upgrade only
    if (mIsHttp2Websocket && mResponseHead->Status() == 200) {
      LOG(("nsHttpTransaction::HandleContentStart websocket upgrade resp 200"));
      mNoContent = true;
    }

    if (mResponseHead->Status() == 200 &&
        mConnection->IsProxyConnectInProgress()) {
      // successful CONNECTs do not have response bodies
      mNoContent = true;
    }
    mConnection->SetLastTransactionExpectedNoContent(mNoContent);

    if (mNoContent) {
      mContentLength = 0;
    } else {
      // grab the content-length from the response headers
      mContentLength = mResponseHead->ContentLength();

      // handle chunked encoding here, so we'll know immediately when
      // we're done with the socket.  please note that _all_ other
      // decoding is done when the channel receives the content data
      // so as not to block the socket transport thread too much.
      if (mResponseHead->Version() >= HttpVersion::v1_0 &&
          mResponseHead->HasHeaderValue(nsHttp::Transfer_Encoding, "chunked")) {
        // we only support the "chunked" transfer encoding right now.
        mChunkedDecoder = new nsHttpChunkedDecoder();
        LOG(("nsHttpTransaction %p chunked decoder created\n", this));
        // Ignore server specified Content-Length.
        if (mContentLength != int64_t(-1)) {
          LOG(("nsHttpTransaction %p chunked with C-L ignores C-L\n", this));
          mContentLength = -1;
          if (mConnection) {
            mConnection->DontReuse();
          }
        }
      } else if (mContentLength == int64_t(-1)) {
        LOG(("waiting for the server to close the connection.\n"));
      }
    }
  }

  mDidContentStart = true;
  return NS_OK;
}

// called on the socket thread
nsresult nsHttpTransaction::HandleContent(char* buf, uint32_t count,
                                          uint32_t* contentRead,
                                          uint32_t* contentRemaining) {
  nsresult rv;

  LOG(("nsHttpTransaction::HandleContent [this=%p count=%u]\n", this, count));

  *contentRead = 0;
  *contentRemaining = 0;

  MOZ_ASSERT(mConnection);

  if (!mDidContentStart) {
    rv = HandleContentStart();
    if (NS_FAILED(rv)) return rv;
    // Do not write content to the pipe if we haven't started streaming yet
    if (!mDidContentStart) return NS_OK;
  }

  if (mChunkedDecoder) {
    // give the buf over to the chunked decoder so it can reformat the
    // data and tell us how much is really there.
    rv = mChunkedDecoder->HandleChunkedContent(buf, count, contentRead,
                                               contentRemaining);
    if (NS_FAILED(rv)) return rv;
  } else if (mContentLength >= int64_t(0)) {
    // HTTP/1.0 servers have been known to send erroneous Content-Length
    // headers. So, unless the connection is persistent, we must make
    // allowances for a possibly invalid Content-Length header. Thus, if
    // NOT persistent, we simply accept everything in |buf|.
    if (mConnection->IsPersistent() || mPreserveStream ||
        mHttpVersion >= HttpVersion::v1_1) {
      int64_t remaining = mContentLength - mContentRead;
      *contentRead = uint32_t(std::min<int64_t>(count, remaining));
      *contentRemaining = count - *contentRead;
    } else {
      *contentRead = count;
      // mContentLength might need to be increased...
      int64_t position = mContentRead + int64_t(count);
      if (position > mContentLength) {
        mContentLength = position;
        // mResponseHead->SetContentLength(mContentLength);
      }
    }
  } else {
    // when we are just waiting for the server to close the connection...
    // (no explicit content-length given)
    *contentRead = count;
  }

  if (*contentRead) {
    // update count of content bytes read and report progress...
    mContentRead += *contentRead;
  }

  LOG1(
      ("nsHttpTransaction::HandleContent [this=%p count=%u read=%u "
       "mContentRead=%" PRId64 " mContentLength=%" PRId64 "]\n",
       this, count, *contentRead, mContentRead, mContentLength));

  // check for end-of-file
  if ((mContentRead == mContentLength) ||
      (mChunkedDecoder && mChunkedDecoder->ReachedEOF())) {
    {
      MutexAutoLock lock(mLock);
      if (mChunkedDecoder) {
        mForTakeResponseTrailers = mChunkedDecoder->TakeTrailers();
      }

      // the transaction is done with a complete response.
      mTransactionDone = true;
      mResponseIsComplete = true;
    }
    ReleaseBlockingTransaction();

    SetResponseEnd(TimeStamp::Now());

    // report the entire response has arrived
    gHttpHandler->ObserveHttpActivityWithArgs(
        HttpActivityArgs(mChannelId), NS_HTTP_ACTIVITY_TYPE_HTTP_TRANSACTION,
        NS_HTTP_ACTIVITY_SUBTYPE_RESPONSE_COMPLETE, PR_Now(),
        static_cast<uint64_t>(mContentRead), ""_ns);
  }

  return NS_OK;
}

nsresult nsHttpTransaction::ProcessData(char* buf, uint32_t count,
                                        uint32_t* countRead) {
  nsresult rv;

  LOG1(("nsHttpTransaction::ProcessData [this=%p count=%u]\n", this, count));

  *countRead = 0;

  // we may not have read all of the headers yet...
  if (!mHaveAllHeaders) {
    uint32_t bytesConsumed = 0;

    do {
      uint32_t localBytesConsumed = 0;
      char* localBuf = buf + bytesConsumed;
      uint32_t localCount = count - bytesConsumed;

      rv = ParseHead(localBuf, localCount, &localBytesConsumed);
      if (NS_FAILED(rv) && rv != NS_ERROR_NET_INTERRUPT) return rv;
      bytesConsumed += localBytesConsumed;
    } while (rv == NS_ERROR_NET_INTERRUPT);

    mCurrentHttpResponseHeaderSize += bytesConsumed;
    if (mCurrentHttpResponseHeaderSize >
        StaticPrefs::network_http_max_response_header_size()) {
      LOG(("nsHttpTransaction %p The response header exceeds the limit.\n",
           this));
      return NS_ERROR_FILE_TOO_BIG;
    }
    count -= bytesConsumed;

    // if buf has some content in it, shift bytes to top of buf.
    if (count && bytesConsumed) memmove(buf, buf + bytesConsumed, count);

    if (mResponseHead && mHaveAllHeaders) {
      if (mConnection->IsProxyConnectInProgress()) {
        ReportResponseHeader(NS_HTTP_ACTIVITY_SUBTYPE_PROXY_RESPONSE_HEADER);
      } else if (!mReportedResponseHeader) {
        mReportedResponseHeader = true;
        ReportResponseHeader(NS_HTTP_ACTIVITY_SUBTYPE_RESPONSE_HEADER);
      }
    }
  }

  // even though count may be 0, we still want to call HandleContent
  // so it can complete the transaction if this is a "no-content" response.
  if (mHaveAllHeaders) {
    uint32_t countRemaining = 0;
    //
    // buf layout:
    //
    // +--------------------------------------+----------------+-----+
    // |              countRead               | countRemaining |     |
    // +--------------------------------------+----------------+-----+
    //
    // count          : bytes read from the socket
    // countRead      : bytes corresponding to this transaction
    // countRemaining : bytes corresponding to next transaction on conn
    //
    // NOTE:
    // count > countRead + countRemaining <==> chunked transfer encoding
    //
    rv = HandleContent(buf, count, countRead, &countRemaining);
    if (NS_FAILED(rv)) return rv;
    // we may have read more than our share, in which case we must give
    // the excess bytes back to the connection
    if (mResponseIsComplete && countRemaining &&
        (mConnection->Version() != HttpVersion::v3_0)) {
      MOZ_ASSERT(mConnection);
      rv = mConnection->PushBack(buf + *countRead, countRemaining);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    if (!mContentDecodingCheck && mResponseHead) {
      mContentDecoding = mResponseHead->HasHeader(nsHttp::Content_Encoding);
      mContentDecodingCheck = true;
    }
  }

  return NS_OK;
}

// Used to report response header data to devtools
void nsHttpTransaction::ReportResponseHeader(uint32_t aSubType) {
  nsAutoCString completeResponseHeaders;
  mResponseHead->Flatten(completeResponseHeaders, false);
  completeResponseHeaders.AppendLiteral("\r\n");
  gHttpHandler->ObserveHttpActivityWithArgs(
      HttpActivityArgs(mChannelId), NS_HTTP_ACTIVITY_TYPE_HTTP_TRANSACTION,
      aSubType, PR_Now(), 0, completeResponseHeaders);
};

// Called when the transaction marked for blocking is associated with a
// connection (i.e. added to a new h1 conn, an idle http connection, etc..) It
// is safe to call this multiple times with it only having an effect once.
void nsHttpTransaction::DispatchedAsBlocking() {
  if (mDispatchedAsBlocking) return;

  LOG(("nsHttpTransaction %p dispatched as blocking\n", this));

  if (!mRequestContext) return;

  LOG(
      ("nsHttpTransaction adding blocking transaction %p from "
       "request context %p\n",
       this, mRequestContext.get()));

  mRequestContext->AddBlockingTransaction();
  mDispatchedAsBlocking = true;
}

void nsHttpTransaction::RemoveDispatchedAsBlocking() {
  if (!mRequestContext || !mDispatchedAsBlocking) {
    LOG(("nsHttpTransaction::RemoveDispatchedAsBlocking this=%p not blocking",
         this));
    return;
  }

  uint32_t blockers = 0;
  nsresult rv = mRequestContext->RemoveBlockingTransaction(&blockers);

  LOG(
      ("nsHttpTransaction removing blocking transaction %p from "
       "request context %p. %d blockers remain.\n",
       this, mRequestContext.get(), blockers));

  if (NS_SUCCEEDED(rv) && !blockers) {
    LOG(
        ("nsHttpTransaction %p triggering release of blocked channels "
         " with request context=%p\n",
         this, mRequestContext.get()));
    rv = gHttpHandler->ConnMgr()->ProcessPendingQ();
    if (NS_FAILED(rv)) {
      LOG(
          ("nsHttpTransaction::RemoveDispatchedAsBlocking\n"
           "    failed to process pending queue\n"));
    }
  }

  mDispatchedAsBlocking = false;
}

void nsHttpTransaction::ReleaseBlockingTransaction() {
  RemoveDispatchedAsBlocking();
  LOG(
      ("nsHttpTransaction %p request context set to null "
       "in ReleaseBlockingTransaction() - was %p\n",
       this, mRequestContext.get()));
  mRequestContext = nullptr;
}

void nsHttpTransaction::DisableSpdy() {
  mCaps |= NS_HTTP_DISALLOW_SPDY;
  if (mConnInfo) {
    // This is our clone of the connection info, not the persistent one that
    // is owned by the connection manager, so we're safe to change this here
    mConnInfo->SetNoSpdy(true);
  }
}

void nsHttpTransaction::DisableHttp2ForProxy() {
  mCaps |= NS_HTTP_DISALLOW_HTTP2_PROXY;
}

void nsHttpTransaction::DisableHttp3(bool aAllowRetryHTTPSRR) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  // mOrigConnInfo is an indicator that HTTPS RR is used, so don't mess up the
  // connection info.
  // When HTTPS RR is used, PrepareConnInfoForRetry() could select other h3
  // record to connect.
  if (mOrigConnInfo) {
    LOG(
        ("nsHttpTransaction::DisableHttp3 this=%p mOrigConnInfo=%s "
         "aAllowRetryHTTPSRR=%d",
         this, mOrigConnInfo->HashKey().get(), aAllowRetryHTTPSRR));
    if (!aAllowRetryHTTPSRR) {
      mCaps |= NS_HTTP_DISALLOW_HTTP3;
    }
    return;
  }

  mCaps |= NS_HTTP_DISALLOW_HTTP3;

  MOZ_ASSERT(mConnInfo);
  if (mConnInfo) {
    // After CloneAsDirectRoute(), http3 will be disabled.
    RefPtr<nsHttpConnectionInfo> connInfo;
    mConnInfo->CloneAsDirectRoute(getter_AddRefs(connInfo));
    RemoveAlternateServiceUsedHeader(mRequestHead);
    MOZ_ASSERT(!connInfo->IsHttp3());
    mConnInfo.swap(connInfo);
  }
}

void nsHttpTransaction::RemoveAltSvcUsedHeader() {
  RemoveAlternateServiceUsedHeader(mRequestHead);
}

void nsHttpTransaction::Deactivate() {
  MOZ_ASSERT(OnSocketThread());
  if (mActivated) {
    gHttpHandler->ConnMgr()->RemoveActiveTransaction(this);
    mActivated = false;
  }
}

void nsHttpTransaction::CheckForStickyAuthScheme() {
  LOG(("nsHttpTransaction::CheckForStickyAuthScheme this=%p", this));

  MOZ_ASSERT(mHaveAllHeaders);
  MOZ_ASSERT(mResponseHead);
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  CheckForStickyAuthSchemeAt(nsHttp::WWW_Authenticate);
  CheckForStickyAuthSchemeAt(nsHttp::Proxy_Authenticate);
}

void nsHttpTransaction::CheckForStickyAuthSchemeAt(nsHttpAtom const& header) {
  if (mCaps & NS_HTTP_STICKY_CONNECTION) {
    LOG(("  already sticky"));
    return;
  }

  nsAutoCString auth;
  if (NS_FAILED(mResponseHead->GetHeader(header, auth))) {
    return;
  }

  if (IsStickyAuthSchemeAt(auth)) {
    LOG(("  connection made sticky"));
    // This is enough to make this transaction keep it's current connection,
    // prevents the connection from being released back to the pool.
    mCaps |= NS_HTTP_STICKY_CONNECTION;
  }
}

bool nsHttpTransaction::IsStickyAuthSchemeAt(nsACString const& auth) {
  Tokenizer p(auth);
  nsAutoCString schema;
  while (p.ReadWord(schema)) {
    ToLowerCase(schema);

    // using a new instance because of thread safety of auth modules refcnt
    nsCOMPtr<nsIHttpAuthenticator> authenticator;
    if (schema.EqualsLiteral("negotiate")) {
#ifdef MOZ_AUTH_EXTENSION
      authenticator = new nsHttpNegotiateAuth();
#endif
    } else if (schema.EqualsLiteral("basic")) {
      authenticator = new nsHttpBasicAuth();
    } else if (schema.EqualsLiteral("digest")) {
      authenticator = new nsHttpDigestAuth();
    } else if (schema.EqualsLiteral("ntlm")) {
      authenticator = new nsHttpNTLMAuth();
    } else if (schema.EqualsLiteral("mock_auth") &&
               PR_GetEnv("XPCSHELL_TEST_PROFILE_DIR")) {
      authenticator = new MockHttpAuth();
    }
    if (authenticator) {
      uint32_t flags;
      nsresult rv = authenticator->GetAuthFlags(&flags);
      if (NS_SUCCEEDED(rv) &&
          (flags & nsIHttpAuthenticator::CONNECTION_BASED)) {
        return true;
      }
    }

    // schemes are separated with LFs, nsHttpHeaderArray::MergeHeader
    p.SkipUntil(Tokenizer::Token::NewLine());
    p.SkipWhites(Tokenizer::INCLUDE_NEW_LINE);
  }

  return false;
}

TimingStruct nsHttpTransaction::Timings() {
  mozilla::MutexAutoLock lock(mLock);
  TimingStruct timings = mTimings;
  return timings;
}

void nsHttpTransaction::BootstrapTimings(TimingStruct times) {
  mozilla::MutexAutoLock lock(mLock);
  mTimings = times;
}

void nsHttpTransaction::SetDomainLookupStart(mozilla::TimeStamp timeStamp,
                                             bool onlyIfNull) {
  mozilla::MutexAutoLock lock(mLock);
  if (onlyIfNull && !mTimings.domainLookupStart.IsNull()) {
    return;  // We only set the timestamp if it was previously null
  }
  mTimings.domainLookupStart = timeStamp;
}

void nsHttpTransaction::SetDomainLookupEnd(mozilla::TimeStamp timeStamp,
                                           bool onlyIfNull) {
  mozilla::MutexAutoLock lock(mLock);
  if (onlyIfNull && !mTimings.domainLookupEnd.IsNull()) {
    return;  // We only set the timestamp if it was previously null
  }
  mTimings.domainLookupEnd = timeStamp;
}

void nsHttpTransaction::SetConnectStart(mozilla::TimeStamp timeStamp,
                                        bool onlyIfNull) {
  mozilla::MutexAutoLock lock(mLock);
  if (onlyIfNull && !mTimings.connectStart.IsNull()) {
    return;  // We only set the timestamp if it was previously null
  }
  mTimings.connectStart = timeStamp;
}

void nsHttpTransaction::SetConnectEnd(mozilla::TimeStamp timeStamp,
                                      bool onlyIfNull) {
  mozilla::MutexAutoLock lock(mLock);
  if (onlyIfNull && !mTimings.connectEnd.IsNull()) {
    return;  // We only set the timestamp if it was previously null
  }
  mTimings.connectEnd = timeStamp;
}

void nsHttpTransaction::SetRequestStart(mozilla::TimeStamp timeStamp,
                                        bool onlyIfNull) {
  mozilla::MutexAutoLock lock(mLock);
  if (onlyIfNull && !mTimings.requestStart.IsNull()) {
    return;  // We only set the timestamp if it was previously null
  }
  mTimings.requestStart = timeStamp;
}

void nsHttpTransaction::SetResponseStart(mozilla::TimeStamp timeStamp,
                                         bool onlyIfNull) {
  mozilla::MutexAutoLock lock(mLock);
  if (onlyIfNull && !mTimings.responseStart.IsNull()) {
    return;  // We only set the timestamp if it was previously null
  }
  mTimings.responseStart = timeStamp;
}

void nsHttpTransaction::SetResponseEnd(mozilla::TimeStamp timeStamp,
                                       bool onlyIfNull) {
  mozilla::MutexAutoLock lock(mLock);
  if (onlyIfNull && !mTimings.responseEnd.IsNull()) {
    return;  // We only set the timestamp if it was previously null
  }
  mTimings.responseEnd = timeStamp;
}

void nsHttpTransaction::SetFirstInterimResponseStart(
    mozilla::TimeStamp timeStamp, bool onlyIfNull) {
  mozilla::MutexAutoLock lock(mLock);
  if (onlyIfNull && !mTimings.firstInterimResponseStart.IsNull()) {
    return;
  }
  mTimings.firstInterimResponseStart = timeStamp;
}

void nsHttpTransaction::SetFinalResponseHeadersStart(
    mozilla::TimeStamp timeStamp, bool onlyIfNull) {
  mozilla::MutexAutoLock lock(mLock);
  if (onlyIfNull && !mTimings.finalResponseHeadersStart.IsNull()) {
    return;
  }
  mTimings.finalResponseHeadersStart = timeStamp;
}

mozilla::TimeStamp nsHttpTransaction::GetDomainLookupStart() {
  mozilla::MutexAutoLock lock(mLock);
  return mTimings.domainLookupStart;
}

mozilla::TimeStamp nsHttpTransaction::GetDomainLookupEnd() {
  mozilla::MutexAutoLock lock(mLock);
  return mTimings.domainLookupEnd;
}

mozilla::TimeStamp nsHttpTransaction::GetConnectStart() {
  mozilla::MutexAutoLock lock(mLock);
  return mTimings.connectStart;
}

mozilla::TimeStamp nsHttpTransaction::GetTcpConnectEnd() {
  mozilla::MutexAutoLock lock(mLock);
  return mTimings.tcpConnectEnd;
}

mozilla::TimeStamp nsHttpTransaction::GetSecureConnectionStart() {
  mozilla::MutexAutoLock lock(mLock);
  return mTimings.secureConnectionStart;
}

mozilla::TimeStamp nsHttpTransaction::GetConnectEnd() {
  mozilla::MutexAutoLock lock(mLock);
  return mTimings.connectEnd;
}

mozilla::TimeStamp nsHttpTransaction::GetRequestStart() {
  mozilla::MutexAutoLock lock(mLock);
  return mTimings.requestStart;
}

mozilla::TimeStamp nsHttpTransaction::GetResponseStart() {
  mozilla::MutexAutoLock lock(mLock);
  return mTimings.responseStart;
}

mozilla::TimeStamp nsHttpTransaction::GetResponseEnd() {
  mozilla::MutexAutoLock lock(mLock);
  return mTimings.responseEnd;
}

mozilla::TimeStamp nsHttpTransaction::GetFirstInterimResponseStart() {
  mozilla::MutexAutoLock lock(mLock);
  return mTimings.firstInterimResponseStart;
}

mozilla::TimeStamp nsHttpTransaction::GetFinalResponseHeadersStart() {
  mozilla::MutexAutoLock lock(mLock);
  return mTimings.finalResponseHeadersStart;
}

//-----------------------------------------------------------------------------
// nsHttpTransaction deletion event
//-----------------------------------------------------------------------------

class DeleteHttpTransaction : public Runnable {
 public:
  explicit DeleteHttpTransaction(nsHttpTransaction* trans)
      : Runnable("net::DeleteHttpTransaction"), mTrans(trans) {}

  NS_IMETHOD Run() override {
    delete mTrans;
    return NS_OK;
  }

 private:
  nsHttpTransaction* mTrans;
};

void nsHttpTransaction::DeleteSelfOnConsumerThread() {
  LOG(("nsHttpTransaction::DeleteSelfOnConsumerThread [this=%p]\n", this));

  if (mConnection && OnSocketThread()) {
    mConnection = nullptr;
  }

  bool val;
  if (!mConsumerTarget ||
      (NS_SUCCEEDED(mConsumerTarget->IsOnCurrentThread(&val)) && val)) {
    delete this;
  } else {
    LOG(("proxying delete to consumer thread...\n"));
    nsCOMPtr<nsIRunnable> event = new DeleteHttpTransaction(this);
    if (NS_FAILED(mConsumerTarget->Dispatch(event, NS_DISPATCH_NORMAL))) {
      NS_WARNING("failed to dispatch nsHttpDeleteTransaction event");
    }
  }
}

bool nsHttpTransaction::TryToRunPacedRequest() {
  if (mSubmittedRatePacing) return mPassedRatePacing;

  mSubmittedRatePacing = true;
  mSynchronousRatePaceRequest = true;
  (void)gHttpHandler->SubmitPacedRequest(this,
                                         getter_AddRefs(mTokenBucketCancel));
  mSynchronousRatePaceRequest = false;
  return mPassedRatePacing;
}

void nsHttpTransaction::OnTokenBucketAdmitted() {
  mPassedRatePacing = true;
  mTokenBucketCancel = nullptr;

  if (!mSynchronousRatePaceRequest) {
    nsresult rv = gHttpHandler->ConnMgr()->ProcessPendingQ(mConnInfo);
    if (NS_FAILED(rv)) {
      LOG(
          ("nsHttpTransaction::OnTokenBucketAdmitted\n"
           "    failed to process pending queue\n"));
    }
  }
}

void nsHttpTransaction::CancelPacing(nsresult reason) {
  if (mTokenBucketCancel) {
    mTokenBucketCancel->Cancel(reason);
    mTokenBucketCancel = nullptr;
  }
}

//-----------------------------------------------------------------------------
// nsHttpTransaction::nsISupports
//-----------------------------------------------------------------------------

NS_IMPL_ADDREF(nsHttpTransaction)

NS_IMETHODIMP_(MozExternalRefCountType)
nsHttpTransaction::Release() {
  nsrefcnt count;
  MOZ_ASSERT(0 != mRefCnt, "dup release");
  count = --mRefCnt;
  NS_LOG_RELEASE(this, count, "nsHttpTransaction");
  if (0 == count) {
    mRefCnt = 1; /* stablize */
    // it is essential that the transaction be destroyed on the consumer
    // thread (we could be holding the last reference to our consumer).
    DeleteSelfOnConsumerThread();
    return 0;
  }
  return count;
}

NS_IMPL_QUERY_INTERFACE(nsHttpTransaction, nsIInputStreamCallback,
                        nsIOutputStreamCallback, nsITimerCallback, nsINamed)

//-----------------------------------------------------------------------------
// nsHttpTransaction::nsIInputStreamCallback
//-----------------------------------------------------------------------------

// called on the socket thread
NS_IMETHODIMP
nsHttpTransaction::OnInputStreamReady(nsIAsyncInputStream* out) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  if (mConnection) {
    mConnection->TransactionHasDataToWrite(this);
    nsresult rv = mConnection->ResumeSend();
    if (NS_FAILED(rv)) NS_ERROR("ResumeSend failed");
  }
  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpTransaction::nsIOutputStreamCallback
//-----------------------------------------------------------------------------

// called on the socket thread
NS_IMETHODIMP
nsHttpTransaction::OnOutputStreamReady(nsIAsyncOutputStream* out) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  mWaitingOnPipeOut = false;
  if (mConnection) {
    mConnection->TransactionHasDataToRecv(this);
    nsresult rv = mConnection->ResumeRecv();
    if (NS_FAILED(rv) && rv != NS_BASE_STREAM_WOULD_BLOCK) {
      NS_ERROR("ResumeRecv failed");
    }
  }
  return NS_OK;
}

void nsHttpTransaction::GetNetworkAddresses(
    NetAddr& self, NetAddr& peer, bool& aResolvedByTRR,
    nsIRequest::TRRMode& aEffectiveTRRMode, TRRSkippedReason& aSkipReason,
    bool& aEchConfigUsed) {
  MutexAutoLock lock(mLock);
  self = mSelfAddr;
  peer = mPeerAddr;
  aResolvedByTRR = mResolvedByTRR;
  aEffectiveTRRMode = mEffectiveTRRMode;
  aSkipReason = mTRRSkipReason;
  aEchConfigUsed = mEchConfigUsed;
}

bool nsHttpTransaction::Do0RTT(bool aCanSendEarlyData) {
  LOG(("nsHttpTransaction::Do0RTT [aCanSendEarlyData=%d]", aCanSendEarlyData));
  mResumptionAttempted = true;
  if (aCanSendEarlyData && mRequestHead->IsSafeMethod() &&
      !mDoNotTryEarlyData &&
      (!mConnection || !mConnection->IsProxyConnectInProgress())) {
    m0RTTInProgress = true;
  }
  return m0RTTInProgress;
}

nsresult nsHttpTransaction::Finish0RTT(bool aRestart,
                                       bool aAlpnChanged /* ignored */) {
  LOG(("nsHttpTransaction::Finish0RTT %p aRestart=%d aAlpnChanged=%d\n", this,
       aRestart, aAlpnChanged));
  MOZ_ASSERT(m0RTTInProgress);
  m0RTTInProgress = false;

  MaybeCancelFallbackTimer();

  if (!aRestart && (mEarlyDataDisposition == EARLY_SENT)) {
    // note that if this is invoked by a 3 param version of finish0rtt this
    // disposition might be reverted
    mEarlyDataDisposition = EARLY_ACCEPTED;
  }
  if (aRestart) {
    // Not to use 0RTT when this transaction is restarted next time.
    mDoNotTryEarlyData = true;

    // Reset request headers to be sent again.
    nsCOMPtr<nsISeekableStream> seekable = do_QueryInterface(mRequestStream);
    if (seekable) {
      seekable->Seek(nsISeekableStream::NS_SEEK_SET, 0);
    } else {
      return NS_ERROR_FAILURE;
    }
  } else if (!mConnected) {
    // this is code that was skipped in ::ReadSegments while in 0RTT
    mConnected = true;
    MaybeRefreshSecurityInfo();
  }
  return NS_OK;
}

void nsHttpTransaction::FinishAdopted0RTT(bool aRestart) {
  LOG(("nsHttpTransaction::FinishAdopted0RTT %p restart=%d\n", this, aRestart));
  mResumptionAttempted = true;
  if (!aRestart) {
    // Mirror nsHttpTransaction::Finish0RTT: only promote when the
    // pre-state is EARLY_SENT (i.e. ZeroRttHandle::ReadSegments
    // already flipped us via MarkEarlyDataSent). If EARLY_SENT was
    // never set, no bytes were sent as early data for this txn and
    // claiming EARLY_ACCEPTED would be a lie.
    if (mEarlyDataDisposition == EARLY_SENT) {
      mEarlyDataDisposition = EARLY_ACCEPTED;
    }
  } else {
    mDoNotTryEarlyData = true;
    // Rewind the request stream so the real txn re-sends from the
    // start on the winning full-handshake conn. Matches
    // nsHttpTransaction::Finish0RTT's restart path.
    nsCOMPtr<nsISeekableStream> seekable = do_QueryInterface(mRequestStream);
    if (seekable) {
      (void)seekable->Seek(nsISeekableStream::NS_SEEK_SET, 0);
    }
  }
}

void nsHttpTransaction::Refused0RTT() {
  LOG(("nsHttpTransaction::Refused0RTT %p\n", this));
  if (mEarlyDataDisposition == EARLY_ACCEPTED) {
    mEarlyDataDisposition = EARLY_SENT;  // undo accepted state
  }
}

void nsHttpTransaction::SetHttpTrailers(nsCString& aTrailers) {
  LOG(("nsHttpTransaction::SetHttpTrailers %p", this));
  LOG(("[\n    %s\n]", aTrailers.get()));

  // Introduce a local variable to minimize the critical section.
  UniquePtr<nsHttpHeaderArray> httpTrailers(new nsHttpHeaderArray());
  // Given it's usually null, use double-check locking for performance.
  if (mForTakeResponseTrailers) {
    MutexAutoLock lock(mLock);
    if (mForTakeResponseTrailers) {
      // Copy the trailer. |TakeResponseTrailers| gets the original trailer
      // until the final swap.
      *httpTrailers = *mForTakeResponseTrailers;
    }
  }

  int32_t cur = 0;
  int32_t len = aTrailers.Length();
  while (cur < len) {
    int32_t newline = aTrailers.FindCharInSet("\n", cur);
    if (newline == -1) {
      newline = len;
    }

    int32_t end =
        (newline && aTrailers[newline - 1] == '\r') ? newline - 1 : newline;
    nsDependentCSubstring line(aTrailers, cur, end);
    nsHttpAtom hdr;
    nsAutoCString hdrNameOriginal;
    nsAutoCString val;
    if (NS_SUCCEEDED(httpTrailers->ParseHeaderLine(line, &hdr, &hdrNameOriginal,
                                                   &val))) {
      if (hdr == nsHttp::Server_Timing) {
        (void)httpTrailers->SetHeaderFromNet(hdr, hdrNameOriginal, val, true);
      }
    }

    cur = newline + 1;
  }

  if (httpTrailers->Count() == 0) {
    // Didn't find a Server-Timing header, so get rid of this.
    httpTrailers = nullptr;
  }

  MutexAutoLock lock(mLock);
  std::swap(mForTakeResponseTrailers, httpTrailers);
}

bool nsHttpTransaction::IsWebsocketUpgrade() {
  if (mRequestHead) {
    nsAutoCString upgradeHeader;
    if (NS_SUCCEEDED(mRequestHead->GetHeader(nsHttp::Upgrade, upgradeHeader)) &&
        upgradeHeader.LowerCaseEqualsLiteral("websocket")) {
      return true;
    }
  }
  return false;
}

void nsHttpTransaction::OnProxyConnectComplete(int32_t aResponseCode) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(mConnInfo->UsingConnect());

  LOG(("nsHttpTransaction::OnProxyConnectComplete %p aResponseCode=%d", this,
       aResponseCode));

  mProxyConnectResponseCode = aResponseCode;

  if (mConnInfo->IsHttp3() && mProxyConnectResponseCode == 200 &&
      !mHttp3TunnelFallbackTimerCreated) {
    mHttp3TunnelFallbackTimerCreated = true;
    CreateAndStartTimer(mHttp3TunnelFallbackTimer, this,
                        StaticPrefs::network_http_http3_inner_fallback_delay());
  }
}

int32_t nsHttpTransaction::GetProxyConnectResponseCode() {
  return mProxyConnectResponseCode;
}

void nsHttpTransaction::SetFlat407Headers(const nsACString& aHeaders) {
  MOZ_ASSERT(mProxyConnectResponseCode == 407);
  MOZ_ASSERT(!mResponseHead);

  LOG(("nsHttpTransaction::SetFlat407Headers %p", this));
  mFlat407Headers = aHeaders;
}

void nsHttpTransaction::NotifyTransactionObserver(nsresult reason) {
  MOZ_ASSERT(OnSocketThread());

  if (!mTransactionObserver) {
    return;
  }

  bool versionOk = false;
  bool authOk = false;

  LOG(("nsHttpTransaction::NotifyTransactionObserver %p reason %" PRIx32
       " conn %p\n",
       this, static_cast<uint32_t>(reason), mConnection.get()));

  if (mConnection) {
    HttpVersion version = mConnection->Version();
    versionOk = (((reason == NS_BASE_STREAM_CLOSED) || (reason == NS_OK)) &&
                 ((mConnection->Version() == HttpVersion::v2_0) ||
                  (mConnection->Version() == HttpVersion::v3_0)));

    nsCOMPtr<nsITLSSocketControl> socketControl;
    mConnection->GetTLSSocketControl(getter_AddRefs(socketControl));
    LOG(
        ("nsHttpTransaction::NotifyTransactionObserver"
         " version %u socketControl %p\n",
         static_cast<int32_t>(version), socketControl.get()));
    if (socketControl) {
      authOk = !socketControl->GetFailedVerification();
    }
  }

  TransactionObserverResult result;
  result.versionOk() = versionOk;
  result.authOk() = authOk;
  result.closeReason() = reason;

  TransactionObserverFunc obs = nullptr;
  std::swap(obs, mTransactionObserver);
  obs(std::move(result));
}

void nsHttpTransaction::UpdateConnectionInfo(nsHttpConnectionInfo* aConnInfo) {
  MOZ_ASSERT(aConnInfo);

  if (mActivated) {
    MOZ_ASSERT(false, "Should not update conn info after activated");
    return;
  }

  mOrigConnInfo = mConnInfo->Clone();
  mConnInfo = aConnInfo;
}

nsresult nsHttpTransaction::OnHTTPSRRAvailable(
    nsIDNSHTTPSSVCRecord* aHTTPSSVCRecord,
    nsISVCBRecord* aHighestPriorityRecord, const nsACString& aCname) {
  LOG(("nsHttpTransaction::OnHTTPSRRAvailable [this=%p] mActivated=%d", this,
       mActivated));
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  {
    MutexAutoLock lock(mLock);
    MakeDontWaitHTTPSRR();
    mDNSRequest = nullptr;
  }

  if (!mResolver) {
    LOG(("The transaction is not interested in HTTPS record anymore."));
    return NS_OK;
  }

  RefPtr<nsHttpTransaction> deleteProtector(this);

  uint32_t receivedStage = HTTPSSVC_NO_USABLE_RECORD;
  // Make sure we set the correct value to |mHTTPSSVCReceivedStage|, since we
  // also use this value to indicate whether HTTPS RR is used or not.
  auto updateHTTPSSVCReceivedStage = MakeScopeExit([&] {
    mHTTPSSVCReceivedStage = receivedStage;

    // In the case that an HTTPS RR is unavailable, we should call
    // ProcessPendingQ to make sure this transition to be processed soon.
    if (!mHTTPSSVCRecord) {
      gHttpHandler->ConnMgr()->ProcessPendingQ(mConnInfo);
    }
  });

  nsCOMPtr<nsIDNSHTTPSSVCRecord> record = aHTTPSSVCRecord;
  if (!record) {
    return NS_ERROR_FAILURE;
  }

  bool hasIPAddress = false;
  (void)record->GetHasIPAddresses(&hasIPAddress);

  if (mActivated) {
    receivedStage = hasIPAddress ? HTTPSSVC_WITH_IPHINT_RECEIVED_STAGE_2
                                 : HTTPSSVC_WITHOUT_IPHINT_RECEIVED_STAGE_2;
    return NS_OK;
  }

  receivedStage = hasIPAddress ? HTTPSSVC_WITH_IPHINT_RECEIVED_STAGE_1
                               : HTTPSSVC_WITHOUT_IPHINT_RECEIVED_STAGE_1;

  nsCOMPtr<nsISVCBRecord> svcbRecord = aHighestPriorityRecord;
  if (!svcbRecord) {
    LOG(("  no usable record!"));
    nsCOMPtr<nsIDNSService> dns = do_GetService(NS_DNSSERVICE_CONTRACTID);
    bool allRecordsExcluded = false;
    (void)record->GetAllRecordsExcluded(&allRecordsExcluded);
    if (allRecordsExcluded &&
        StaticPrefs::network_dns_httpssvc_reset_exclustion_list() && dns) {
      (void)dns->ResetExcludedSVCDomainName(mConnInfo->GetOrigin());
      if (NS_FAILED(record->GetServiceModeRecordWithCname(
              mCaps & NS_HTTP_DISALLOW_SPDY, mCaps & NS_HTTP_DISALLOW_HTTP3,
              aCname, getter_AddRefs(svcbRecord)))) {
        return NS_ERROR_FAILURE;
      }
    } else {
      return NS_ERROR_FAILURE;
    }
  }

  // Remember this RR set. In the case that the connection establishment failed,
  // we will use other records to retry.
  mHTTPSSVCRecord = record;
  mCname = aCname;
  LOG(("has cname:%s", mCname.get()));

  RefPtr<nsHttpConnectionInfo> newInfo =
      mConnInfo->CloneAndAdoptHTTPSSVCRecord(svcbRecord);
  // Don't fallback until we support WebTransport over HTTP/2.
  // TODO: implement fallback in bug 1874102.
  // Note: We don't support HTTPS RR for proxy connection yet, so disable the
  // fallback.
  bool needFastFallback = newInfo->IsHttp3() && !newInfo->GetWebTransport() &&
                          !newInfo->IsHttp3ProxyConnection();
  nsAutoCString hashKey;
  GetHashKeyOfConnectionEntry(hashKey);
  bool foundInPendingQ =
      gHttpHandler->ConnMgr()->RemoveTransFromConnEntry(this, hashKey);

  // Adopt the new connection info, so this transaction will be added into the
  // new connection entry.
  UpdateConnectionInfo(newInfo);

  // If this transaction is sucessfully removed from a connection entry, we call
  // ProcessNewTransaction to process it immediately.
  // If not, this means that nsHttpTransaction::OnHTTPSRRAvailable happens
  // before ProcessNewTransaction and this transaction will be processed later.
  if (foundInPendingQ) {
    if (NS_FAILED(gHttpHandler->ConnMgr()->ProcessNewTransaction(this))) {
      LOG(("Failed to process this transaction."));
      return NS_ERROR_FAILURE;
    }
  }

  // In case we already have mHttp3BackupTimer, cancel it.
  MaybeCancelFallbackTimer();

  if (needFastFallback) {
    CreateAndStartTimer(
        mFastFallbackTimer, this,
        StaticPrefs::network_dns_httpssvc_http3_fast_fallback_timeout());
  }

  // Prefetch the A/AAAA records of the target name.
  nsAutoCString targetName;
  (void)svcbRecord->GetName(targetName);
  if (mResolver) {
    mResolver->PrefetchAddrRecord(targetName, mCaps & NS_HTTP_REFRESH_DNS);
  }

  // echConfig is used, so initialize the retry counters to 0.
  if (!mConnInfo->GetEchConfig().IsEmpty()) {
    mEchRetryCounterMap.InsertOrUpdate(TRANSACTION_ECH_RETRY_WITH_ECH_COUNT, 0);
    mEchRetryCounterMap.InsertOrUpdate(TRANSACTION_ECH_RETRY_WITHOUT_ECH_COUNT,
                                       0);
    mEchRetryCounterMap.InsertOrUpdate(TRANSACTION_ECH_RETRY_ECH_FAILED_COUNT,
                                       0);
    mEchRetryCounterMap.InsertOrUpdate(TRANSACTION_ECH_RETRY_OTHERS_COUNT, 0);
  }

  return NS_OK;
}

uint32_t nsHttpTransaction::HTTPSSVCReceivedStage() {
  return mHTTPSSVCReceivedStage;
}

void nsHttpTransaction::MaybeCancelFallbackTimer() {
  MOZ_DIAGNOSTIC_ASSERT(OnSocketThread(), "not on socket thread");

  if (mFastFallbackTimer) {
    mFastFallbackTimer->Cancel();
    mFastFallbackTimer = nullptr;
  }

  if (mHttp3BackupTimer) {
    mHttp3BackupTimer->Cancel();
    mHttp3BackupTimer = nullptr;
  }

  if (mHttp3TunnelFallbackTimer) {
    mHttp3TunnelFallbackTimer->Cancel();
    mHttp3TunnelFallbackTimer = nullptr;
  }
}

void nsHttpTransaction::OnBackupConnectionReady(bool aTriggeredByHTTPSRR) {
  LOG(
      ("nsHttpTransaction::OnBackupConnectionReady [%p] mConnected=%d "
       "aTriggeredByHTTPSRR=%d",
       this, mConnected, aTriggeredByHTTPSRR));
  if (mConnected || mClosed || mRestarted) {
    return;
  }

  // If HTTPS RR is in play, don't mess up the fallback mechansim of HTTPS RR.
  if (!aTriggeredByHTTPSRR && mOrigConnInfo) {
    return;
  }

  if (mConnection) {
    if (mConnection->Version() != HttpVersion::v3_0) {
      LOG(("Already have non-HTTP/3 conn:%p", mConnection.get()));
      return;
    }
    // The transaction will only be restarted when we already have a connection.
    // When there is no connection, this transaction will be moved to another
    // connection entry.
    SetRestartReason(aTriggeredByHTTPSRR
                         ? TRANSACTION_RESTART_HTTPS_RR_FAST_FALLBACK
                         : TRANSACTION_RESTART_HTTP3_FAST_FALLBACK);
  }

  mCaps |= NS_HTTP_DISALLOW_HTTP3;

  // Need to backup the origin conn info, since UpdateConnectionInfo() will be
  // called in HandleFallback() and mOrigConnInfo will be
  // replaced.
  RefPtr<nsHttpConnectionInfo> backup = mOrigConnInfo;
  HandleFallback(mBackupConnInfo);
  mOrigConnInfo.swap(backup);

  RemoveAlternateServiceUsedHeader(mRequestHead);

  if (mResolver) {
    if (mBackupConnInfo) {
      const nsCString& host = mBackupConnInfo->GetRoutedHost().IsEmpty()
                                  ? mBackupConnInfo->GetOrigin()
                                  : mBackupConnInfo->GetRoutedHost();
      mResolver->PrefetchAddrRecord(host, Caps() & NS_HTTP_REFRESH_DNS);
    }

    if (!aTriggeredByHTTPSRR) {
      // We are about to use this backup connection. We shoud not try to use
      // HTTPS RR at this point.
      mResolver->Close();
      mResolver = nullptr;
    }
  }
}

static void CreateBackupConnection(
    nsHttpConnectionInfo* aBackupConnInfo, nsIInterfaceRequestor* aCallbacks,
    uint32_t aCaps, std::function<void(nsresult)>&& aResultCallback) {
  aBackupConnInfo->SetFallbackConnection(true);
  RefPtr<SpeculativeTransaction> trans = new FallbackTransaction(
      aBackupConnInfo, aCallbacks, aCaps | NS_HTTP_DISALLOW_HTTP3,
      std::move(aResultCallback));
  uint32_t limit =
      StaticPrefs::network_http_http3_parallel_fallback_conn_limit();
  if (limit) {
    trans->SetParallelSpeculativeConnectLimit(limit);
    trans->SetIgnoreIdle(true);
  }
  gHttpHandler->ConnMgr()->DoFallbackConnection(trans, false);
}

void nsHttpTransaction::OnHttp3BackupTimer() {
  LOG(("nsHttpTransaction::OnHttp3BackupTimer [%p]", this));
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(mConnInfo->IsHttp3() || mConnInfo->IsHttp3ProxyConnection());

  mHttp3BackupTimer = nullptr;

  if (mConnInfo->IsHttp3ProxyConnection()) {
    mBackupConnInfo = mConnInfo->CreateConnectUDPFallbackConnInfo();
  } else {
    mConnInfo->CloneAsDirectRoute(getter_AddRefs(mBackupConnInfo));
    MOZ_ASSERT(!mBackupConnInfo->IsHttp3());
  }

  RefPtr<nsHttpTransaction> self = this;
  auto callback = [self](nsresult aResult) {
    if (NS_SUCCEEDED(aResult)) {
      self->OnBackupConnectionReady(false);
    }
  };

  nsCOMPtr<nsIInterfaceRequestor> callbacks;
  {
    MutexAutoLock lock(mLock);
    callbacks = mCallbacks;
  }
  CreateBackupConnection(mBackupConnInfo, callbacks, mCaps,
                         std::move(callback));
}

void nsHttpTransaction::OnHttp3TunnelFallbackTimer() {
  LOG(("nsHttpTransaction::OnHttp3TunnelFallbackTimer [%p]", this));
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  mHttp3TunnelFallbackTimer = nullptr;

  // Don't disturb the HTTPS RR fallback mechanism.
  if (mOrigConnInfo) {
    return;
  }

  RefPtr<nsHttpConnectionInfo> fallbackCI =
      mConnInfo->CreateConnectUDPFallbackConnInfo();
  if (fallbackCI) {
    mCaps |= NS_HTTP_DISALLOW_HTTP3;
    RemoveAlternateServiceUsedHeader(mRequestHead);
    mConnInfo.swap(fallbackCI);
  } else {
    DisableHttp3(false);
  }

  mDontRetryWithDirectRoute = true;
  if (mConnection) {
    mConnection->CloseTransaction(this, NS_ERROR_NET_RESET);
  }
}

void nsHttpTransaction::OnFastFallbackTimer() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG(("nsHttpTransaction::OnFastFallbackTimer [%p] mConnected=%d", this,
       mConnected));

  mFastFallbackTimer = nullptr;

  MOZ_ASSERT(mHTTPSSVCRecord && mOrigConnInfo);
  if (!mHTTPSSVCRecord || !mOrigConnInfo) {
    return;
  }

  bool echConfigUsed = nsHttpHandler::EchConfigEnabled(mConnInfo->IsHttp3()) &&
                       !mConnInfo->GetEchConfig().IsEmpty();
  mBackupConnInfo = PrepareFastFallbackConnInfo(echConfigUsed);
  if (!mBackupConnInfo) {
    return;
  }

  MOZ_ASSERT(!mBackupConnInfo->IsHttp3());

  RefPtr<nsHttpTransaction> self = this;
  auto callback = [self](nsresult aResult) {
    if (NS_FAILED(aResult)) {
      return;
    }

    self->mFastFallbackTriggered = true;
    self->OnBackupConnectionReady(true);
  };

  nsCOMPtr<nsIInterfaceRequestor> callbacks;
  {
    MutexAutoLock lock(mLock);
    callbacks = mCallbacks;
  }
  CreateBackupConnection(mBackupConnInfo, callbacks, mCaps,
                         std::move(callback));
}

void nsHttpTransaction::HandleFallback(
    nsHttpConnectionInfo* aFallbackConnInfo) {
  if (mConnection) {
    // Close the transaction with NS_ERROR_NET_RESET, since we know doing this
    // will make transaction to be restarted.
    mConnection->CloseTransaction(this, NS_ERROR_NET_RESET);
    return;
  }

  if (!aFallbackConnInfo) {
    // Nothing to do here.
    return;
  }

  LOG(("nsHttpTransaction %p HandleFallback to connInfo[%s]", this,
       aFallbackConnInfo->HashKey().get()));

  nsAutoCString hashKey;
  GetHashKeyOfConnectionEntry(hashKey);
  bool foundInPendingQ =
      gHttpHandler->ConnMgr()->RemoveTransFromConnEntry(this, hashKey);
  if (!foundInPendingQ) {
    MOZ_ASSERT(false, "transaction not in entry");
    return;
  }

  // rewind streams in case we already wrote out the request
  nsCOMPtr<nsISeekableStream> seekable = do_QueryInterface(mRequestStream);
  if (seekable) {
    seekable->Seek(nsISeekableStream::NS_SEEK_SET, 0);
  }

  UpdateConnectionInfo(aFallbackConnInfo);
  (void)gHttpHandler->ConnMgr()->ProcessNewTransaction(this);
}

NS_IMETHODIMP
nsHttpTransaction::Notify(nsITimer* aTimer) {
  MOZ_DIAGNOSTIC_ASSERT(OnSocketThread(), "not on socket thread");

  if (!gHttpHandler || !gHttpHandler->ConnMgr()) {
    return NS_OK;
  }

  if (aTimer == mFastFallbackTimer) {
    OnFastFallbackTimer();
  } else if (aTimer == mHttp3BackupTimer) {
    OnHttp3BackupTimer();
  } else if (aTimer == mHttp3TunnelFallbackTimer) {
    OnHttp3TunnelFallbackTimer();
  }

  return NS_OK;
}

NS_IMETHODIMP
nsHttpTransaction::GetName(nsACString& aName) {
  aName.AssignLiteral("nsHttpTransaction");
  return NS_OK;
}

bool nsHttpTransaction::GetSupportsHTTP3() { return mSupportsHTTP3; }

void nsHttpTransaction::CollectTelemetryForUploads() {
  TimingStruct timings;
  {
    MutexAutoLock lock(mLock);
    timings = mTimings;
  }
  if ((mRequestSize < TELEMETRY_REQUEST_SIZE_1M) ||
      timings.requestStart.IsNull() || timings.responseStart.IsNull()) {
    return;
  }

  nsAutoCString protocolVersion(nsHttp::GetProtocolVersion(mHttpVersion));
  TimeDuration sendTime = timings.responseStart - timings.requestStart;
  double megabits = static_cast<double>(mRequestSize) * 8.0 / 1000000.0;
  uint32_t mpbs = static_cast<uint32_t>(megabits / sendTime.ToSeconds());

  if (mRequestSize <= TELEMETRY_REQUEST_SIZE_10M) {
    if (mHttpVersion == HttpVersion::v3_0) {
      glean::networking::http_3_upload_throughput_1_10.AccumulateSingleSample(
          mpbs);
    }
    return;
  }

  switch (mHttpVersion) {
    case HttpVersion::v1_0:
    case HttpVersion::v1_1:
      glean::networking::http_1_upload_throughput.AccumulateSingleSample(mpbs);
      if (mRequestSize <= TELEMETRY_REQUEST_SIZE_50M) {
        glean::networking::http_1_upload_throughput_10_50
            .AccumulateSingleSample(mpbs);
      } else if (mRequestSize <= TELEMETRY_REQUEST_SIZE_100M) {
        glean::networking::http_1_upload_throughput_50_100
            .AccumulateSingleSample(mpbs);
      } else {
        glean::networking::http_1_upload_throughput_100.AccumulateSingleSample(
            mpbs);
      }
      break;
    case HttpVersion::v2_0:
      glean::networking::http_2_upload_throughput.AccumulateSingleSample(mpbs);
      if (mRequestSize <= TELEMETRY_REQUEST_SIZE_50M) {
        glean::networking::http_2_upload_throughput_10_50
            .AccumulateSingleSample(mpbs);
      } else if (mRequestSize <= TELEMETRY_REQUEST_SIZE_100M) {
        glean::networking::http_2_upload_throughput_50_100
            .AccumulateSingleSample(mpbs);
      } else {
        glean::networking::http_2_upload_throughput_100.AccumulateSingleSample(
            mpbs);
      }
      break;
    case HttpVersion::v3_0:
      glean::networking::http_3_upload_throughput.AccumulateSingleSample(mpbs);
      if (mRequestSize <= TELEMETRY_REQUEST_SIZE_50M) {
        glean::networking::http_3_upload_throughput_10_50
            .AccumulateSingleSample(mpbs);
      } else if (mRequestSize <= TELEMETRY_REQUEST_SIZE_100M) {
        glean::networking::http_3_upload_throughput_50_100
            .AccumulateSingleSample(mpbs);
      } else {
        glean::networking::http_3_upload_throughput_100.AccumulateSingleSample(
            mpbs);
      }
      break;
    default:
      break;
  }
}

void nsHttpTransaction::GetHashKeyOfConnectionEntry(nsACString& aResult) {
  MutexAutoLock lock(mLock);
  aResult.Assign(mHashKeyOfConnectionEntry);
}

void nsHttpTransaction::SetIsForWebTransport(bool aIsForWebTransport) {
  mIsForWebTransport = aIsForWebTransport;
}

void nsHttpTransaction::RemoveConnection() {
  MutexAutoLock lock(mLock);
  mConnection = nullptr;
}

nsILoadInfo::IPAddressSpace nsHttpTransaction::GetTargetIPAddressSpace() {
  nsILoadInfo::IPAddressSpace retVal;
  {
    MutexAutoLock lock(mLock);
    retVal = mTargetIpAddressSpace;
  }

  return retVal;
}

bool nsHttpTransaction::AllowedToConnectToIpAddressSpace(
    nsILoadInfo::IPAddressSpace aTargetIpAddressSpace) {
  // skip checks if LNA feature is disabled

  if (!StaticPrefs::network_lna_enabled()) {
    return true;
  }

  if (mConnection) {
    // update peer address required for LNA telemetry and console logging
    MutexAutoLock lock(mLock);
    mConnection->GetPeerAddr(&mPeerAddr);
  }
  // Skip LNA checks if domain is in skip list
  if (mConnInfo && gIOService &&
      gIOService->ShouldSkipDomainForLNA(mConnInfo->GetOrigin())) {
    return true;
  }

  // Skip LNA checks entirely for WebSocket connections if websocket LNA is
  // disabled
  if (!StaticPrefs::network_lna_websocket_enabled() && IsWebsocketUpgrade()) {
    return true;  // Allow all WebSocket connections
  }

  // store targetIpAddress space which is required later by nsHttpChannel for
  // permission prompts
  {
    mozilla::MutexAutoLock lock(mLock);
    if (mTargetIpAddressSpace == nsILoadInfo::Unknown) {
      mTargetIpAddressSpace = aTargetIpAddressSpace;
    }
  }

  // Deny access to a request moving to a more private addresspsace.
  // Specifically,
  // 1. local host resources cannot be accessed from Private or Public
  // network
  // 2. private network resources cannot be accessed from Public
  // network
  // Refer
  // https://wicg.github.io/private-network-access/#private-network-request-heading
  // for private network access
  // XXX add link to LNA spec once it is published

  // Skip LNA checks for private network to localhost if preference is enabled
  if (StaticPrefs::network_lna_local_network_to_localhost_skip_checks() &&
      mParentIPAddressSpace == nsILoadInfo::IPAddressSpace::Private &&
      aTargetIpAddressSpace == nsILoadInfo::IPAddressSpace::Local) {
    return true;  // Allow private->localhost access
  }

  if (mozilla::net::IsLocalOrPrivateNetworkAccess(mParentIPAddressSpace,
                                                  aTargetIpAddressSpace)) {
    if (aTargetIpAddressSpace == nsILoadInfo::IPAddressSpace::Local &&
        mLnaPermissionStatus.mLocalHostPermission == LNAPermission::Denied) {
      return false;
    }

    if (aTargetIpAddressSpace == nsILoadInfo::IPAddressSpace::Private &&
        mLnaPermissionStatus.mLocalNetworkPermission == LNAPermission::Denied) {
      return false;
    }

    if ((StaticPrefs::network_lna_blocking() ||
         StaticPrefs::network_lna_block_trackers()) &&
        ((aTargetIpAddressSpace == nsILoadInfo::IPAddressSpace::Local &&
          mLnaPermissionStatus.mLocalHostPermission ==
              LNAPermission::Pending) ||
         (aTargetIpAddressSpace == nsILoadInfo::IPAddressSpace::Private &&
          mLnaPermissionStatus.mLocalNetworkPermission ==
              LNAPermission::Pending))) {
      // If LNA prompts are enabled or tracker blocking is enabled we disallow
      // requests
      return false;
    }
  }

  return true;
}

}  // namespace mozilla::net
