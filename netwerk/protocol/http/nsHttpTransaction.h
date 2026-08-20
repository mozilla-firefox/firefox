/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef nsHttpTransaction_h_
#define nsHttpTransaction_h_

#include "ARefBase.h"
#include "EventTokenBucket.h"
#include "HttpTransactionShell.h"
#include "TimingStruct.h"
#include "mozilla/StaticPrefs_security.h"
#include "mozilla/net/DNS.h"
#include "mozilla/net/NeckoChannelParams.h"
#include "nsAHttpConnection.h"
#include "nsAHttpTransaction.h"
#include "nsCOMPtr.h"
#include "nsContentPermissionHelper.h"
#include "nsHttp.h"
#include "nsIAsyncOutputStream.h"
#include "nsIClassOfService.h"
#include "nsIEarlyHintObserver.h"
#include "nsIInterfaceRequestor.h"
#include "nsITLSSocketControl.h"
#include "nsITimer.h"
#include "nsIWebTransport.h"
#include "nsTHashMap.h"
#include "nsThreadUtils.h"

//-----------------------------------------------------------------------------

class nsIDNSHTTPSSVCRecord;
class nsIEventTarget;
class nsIInputStream;
class nsIOutputStream;
class nsIRequestContext;
class nsISVCBRecord;

namespace mozilla::net {

class HTTPSRecordResolver;
class nsHttpChunkedDecoder;
class nsHttpHeaderArray;
class nsHttpRequestHead;
class nsHttpResponseHead;
class NullHttpTransaction;
class Http2ConnectTransaction;

//-----------------------------------------------------------------------------
// nsHttpTransaction represents a single HTTP transaction.  It is thread-safe,
// intended to run on the socket thread.
//-----------------------------------------------------------------------------

class nsHttpTransaction final : public nsAHttpTransaction,
                                public HttpTransactionShell,
                                public ATokenBucketEvent,
                                public nsIInputStreamCallback,
                                public nsIOutputStreamCallback,
                                public ARefBase,
                                public nsITimerCallback,
                                public nsINamed {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSAHTTPTRANSACTION
  NS_DECL_HTTPTRANSACTIONSHELL
  NS_DECL_NSIINPUTSTREAMCALLBACK
  NS_DECL_NSIOUTPUTSTREAMCALLBACK
  NS_DECL_NSITIMERCALLBACK
  NS_DECL_NSINAMED

  nsHttpTransaction();

  void OnActivated() override;

  // attributes
  nsHttpResponseHead* ResponseHead() {
    return mHaveAllHeaders ? mResponseHead : nullptr;
  }

  nsIEventTarget* ConsumerTarget() { return mConsumerTarget; }

  // Called to set/find out if the transaction generated a complete response.
  void SetResponseIsComplete() { mResponseIsComplete = true; }

  void EnableKeepAlive() { mCaps |= NS_HTTP_ALLOW_KEEPALIVE; }
  void MakeSticky() { mCaps |= NS_HTTP_STICKY_CONNECTION; }
  void MakeNonSticky() override { mCaps &= ~NS_HTTP_STICKY_CONNECTION; }
  void MakeRestartable() override { mCaps |= NS_HTTP_CONNECTION_RESTARTABLE; }
  void MakeNonRestartable() { mCaps &= ~NS_HTTP_CONNECTION_RESTARTABLE; }
  void RemoveConnection();
  void SetIsHttp2Websocket(bool h2ws) override { mIsHttp2Websocket = h2ws; }
  bool IsHttp2Websocket() override { return mIsHttp2Websocket; }
  bool Closed() { return mClosed; }

  void SetTRRInfo(nsIRequest::TRRMode aMode,
                  TRRSkippedReason aSkipReason) override {
    mEffectiveTRRMode = aMode;
    mTRRSkipReason = aSkipReason;
  }

  bool WaitingForHTTPSRR() const { return mCaps & NS_HTTP_FORCE_WAIT_HTTP_RR; }
  void MakeDontWaitHTTPSRR() { mCaps &= ~NS_HTTP_FORCE_WAIT_HTTP_RR; }

  // SetPriority() may only be used by the connection manager.
  void SetPriority(int32_t priority) { mPriority = priority; }
  int32_t Priority() { return mPriority; }

  void PrintDiagnostics(nsCString& log);

  // Sets mTimings.transactionPending to the current time stamp or to a null
  // time stamp (if now is false)
  void SetPendingTime(bool now = true) {
    mozilla::MutexAutoLock lock(mLock);
    if (!now && !mTimings.transactionPending.IsNull()) {
      // Remember how long it took. We will use this value to record
      // TRANSACTION_WAIT_TIME_HTTP2_SUP_HTTP3 telemetry, but we need to wait
      // for the response headers.
      mPendingDurationTime = TimeStamp::Now() - mTimings.transactionPending;
    }
    // Note that the transaction could be added in to a pending queue multiple
    // times (when the transaction is restarted or moved to a new conn entry due
    // to HTTPS RR), so we should only set the pending time once.
    if (mTimings.transactionPending.IsNull()) {
      mTimings.transactionPending = now ? TimeStamp::Now() : TimeStamp();
    }
  }
  TimeStamp GetPendingTime() override {
    mozilla::MutexAutoLock lock(mLock);
    return mTimings.transactionPending;
  }

  // overload of nsAHttpTransaction::RequestContext()
  nsIRequestContext* RequestContext() override { return mRequestContext.get(); }
  void DispatchedAsBlocking();
  void RemoveDispatchedAsBlocking();

  void DisableSpdy() override;
  void DisableHttp2ForProxy() override;
  void DoNotRemoveAltSvc() override { mDoNotRemoveAltSvc = true; }
  void DoNotResetIPFamilyPreference() override {
    mDoNotResetIPFamilyPreference = true;
  }
  void DisableHttp3(bool aAllowRetryHTTPSRR) override;
  void RemoveAltSvcUsedHeader();
  void Deactivate();

  nsHttpTransaction* QueryHttpTransaction() override { return this; }

  uint32_t InitialRwin() const { return mInitialRwin; };
  bool ChannelPipeFull() { return mWaitingOnPipeOut; }

  // Locked methods to get and set timing info
  void BootstrapTimings(TimingStruct times);
  void SetConnectStart(mozilla::TimeStamp timeStamp, bool onlyIfNull = false);
  void SetConnectEnd(mozilla::TimeStamp timeStamp, bool onlyIfNull = false);
  void SetRequestStart(mozilla::TimeStamp timeStamp, bool onlyIfNull = false);
  void SetResponseStart(mozilla::TimeStamp timeStamp, bool onlyIfNull = false);
  void SetFirstInterimResponseStart(mozilla::TimeStamp timeStamp,
                                    bool onlyIfNull = false);
  void SetFinalResponseHeadersStart(mozilla::TimeStamp timeStamp,
                                    bool onlyIfNull = false);
  void SetResponseEnd(mozilla::TimeStamp timeStamp, bool onlyIfNull = false);

  [[nodiscard]] bool Do0RTT(bool aCanSendEarlyData) override;
  [[nodiscard]] nsresult Finish0RTT(bool aRestart,
                                    bool aAlpnChanged /* ignored */) override;

  // After Finish0RTT early data may have failed but the caller did not request
  // restart - this indicates that state for dev tools
  void Refused0RTT();

  bool Connected() const { return mConnected; }

  // Exposes mRequestStream to ZeroRttHandle so Happy Eyeballs racing
  // attempts can drive 0-RTT by reading directly from it (seeking to
  // per-attempt offsets) without mutating any of the transaction's
  // own 0-RTT flags.
  nsIInputStream* RequestStream() const { return mRequestStream; }

  // Called by ZeroRttHandle::ReadSegments each time a racer HT
  // successfully forwards early-data bytes on this txn's behalf.
  // Mirrors the EARLY_NONE → EARLY_SENT transition the non-HE
  // ReadSegments path does inline when m0RTTInProgress and
  // *countRead > 0. Idempotent: later ReadSegments rounds / racer
  // attempts are no-ops once the disposition has advanced past
  // EARLY_NONE.
  void MarkEarlyDataSent() {
    if (mEarlyDataDisposition == EARLY_NONE) {
      mEarlyDataDisposition = EARLY_SENT;
      mEarlyDataSentTime = TimeStamp::Now();
    }
  }

  // Called by ZeroRttHandle::Finish0RTT once 0-RTT has completed on
  // behalf of the HE path. In the non-HE flow Do0RTT()+Finish0RTT()
  // are driven by nsHttpConnection against the transaction that did
  // its own ReadSegments, so m0RTTInProgress is true when we enter.
  // In the HE path the HappyEyeballsTransaction did all of that on
  // its racer and this txn's own 0-RTT flags were never set — so
  // we can't reuse Finish0RTT() (its m0RTTInProgress assert would
  // fire). Flip the flags that outlive 0-RTT and that downstream
  // code still reads on the real txn:
  //   * both: mEarlyDataWasAvailable = true so
  //     ShouldRestartOn0RttError in Close() can map a later 0-RTT
  //     TLS alert (BAD_MAC / PROTOCOL_VERSION / UNEXPECTED) into a
  //     restart instead of a channel failure.
  //   * accept: mEarlyDataDisposition = EARLY_ACCEPTED so
  //     HandleContentStart tags the response and a 425 is mapped to
  //     EARLY_425 for retry.
  //   * reject: mDoNotTryEarlyData = true so a later restart of this
  //     txn won't re-attempt 0-RTT.
  // mConnected / mSecurityInfo / fallback-timers are intentionally
  // left alone — the real txn's ReadSegments path initializes them
  // once Adopt() attaches the conn and dispatch kicks in, and HE's
  // fast-fallback timers live on the HappyEyeballsConnectionAttempt,
  // not on this transaction.
  void FinishAdopted0RTT(bool aRestart);

  // Remove all SSL session tokens keyed by aSecInfo->GetPeerId() so a 0-RTT
  // restart starts a fresh handshake instead of looping on the rejected ticket.
  void RemoveSSLTokens(nsITransportSecurityInfo* aSecInfo);

  uint64_t BrowserId() override { return mBrowserId; }

  void SetHttpTrailers(nsCString& aTrailers);

  bool IsWebsocketUpgrade();

  void OnProxyConnectComplete(ProxyConnectResponseHead* aResponseHead) override;
  void SetFlat407Headers(const nsACString& aHeaders);

  void UpdateConnectionInfo(nsHttpConnectionInfo* aConnInfo);

  void SetClassOfService(ClassOfService cos);

  virtual nsresult OnHTTPSRRAvailable(nsIDNSHTTPSSVCRecord* aHTTPSSVCRecord,
                                      nsISVCBRecord* aHighestPriorityRecord,
                                      const nsACString& aCname) override;

  void GetHashKeyOfConnectionEntry(nsACString& aResult);

  bool IsForWebTransport() override { return mIsForWebTransport; }
  bool IsResettingForTunnelConn() override { return mIsResettingForTunnelConn; }
  void SetResettingForTunnelConn(bool aValue) override {
    mIsResettingForTunnelConn = aValue;
  }

  nsAutoCString GetUrl() { return mUrl; }

  uint64_t ChannelId() { return mChannelId; }

  void SetIsTRRTransaction() override { mIsTRRTransaction = true; }
  bool IsTRRTransaction() { return mIsTRRTransaction; }

  // Used by the HE speculative path to propagate the failed-handshake
  // security info onto the real transaction whose mConnection was never
  // set (so its own MaybeRefreshSecurityInfo skips). Without this the
  // channel's GetSecurityInfo returns null on TLS handshake failures
  // routed through the speculative racer.
  void SetSecurityInfo(nsITransportSecurityInfo* aSecurityInfo) {
    MutexAutoLock lock(mLock);
    mSecurityInfo = aSecurityInfo;
  }

  void RefreshSecurityInfoAfter0RTTAdopt() { MaybeRefreshSecurityInfo(); }

 private:
  friend class DeleteHttpTransaction;
  virtual ~nsHttpTransaction();

  [[nodiscard]] nsresult Restart();
  // For an accepted 0-RTT request, report connectEnd and requestStart at the
  // early-data send point. Caller must hold mLock.
  void Apply0RTTTimingOverride();
  char* LocateHttpStart(char* buf, uint32_t len, bool aAllowPartialMatch);
  [[nodiscard]] nsresult ParseLine(nsACString& line);
  [[nodiscard]] nsresult ParseLineSegment(char* seg, uint32_t len);
  [[nodiscard]] nsresult ParseHead(char*, uint32_t count, uint32_t* countRead);
  [[nodiscard]] nsresult HandleContentStart();
  [[nodiscard]] nsresult HandleContent(char*, uint32_t count,
                                       uint32_t* contentRead,
                                       uint32_t* contentRemaining);
  [[nodiscard]] nsresult ProcessData(char*, uint32_t, uint32_t*);
  void ReportResponseHeader(uint32_t aSubType);
  void DeleteSelfOnConsumerThread();
  void ReleaseBlockingTransaction();
  [[nodiscard]] static nsresult ReadRequestSegment(nsIInputStream*, void*,
                                                   const char*, uint32_t,
                                                   uint32_t, uint32_t*);
  [[nodiscard]] static nsresult WritePipeSegment(nsIOutputStream*, void*, char*,
                                                 uint32_t, uint32_t, uint32_t*);

  bool ResponseTimeoutEnabled() const final;

  void ReuseConnectionOnRestartOK(bool reuseOk) override {
    mReuseOnRestart = reuseOk;
  }

  // Called right after we parsed the response head.  Checks for connection
  // based authentication schemes in reponse headers for WWW and Proxy
  // authentication. If such is found in any of them, NS_HTTP_STICKY_CONNECTION
  // is set in mCaps. We need the sticky flag be set early to keep the
  // connection from very start of the authentication process.
  void CheckForStickyAuthScheme();
  void CheckForStickyAuthSchemeAt(nsHttpAtom const& header);
  bool IsStickyAuthSchemeAt(nsACString const& auth);

  // Called from WriteSegments.  Checks for conditions whether to throttle
  // reading the content.  When this returns true, WriteSegments returns
  // WOULD_BLOCK.
  bool ShouldThrottle();

  void NotifyTransactionObserver(nsresult reason);

  // When echConfig is enabled, this function put other available records
  // in mRecordsForRetry. Returns true when mRecordsForRetry is not empty,
  // otherwise returns false.
  bool PrepareSVCBRecordsForRetry(const nsACString& aFailedDomainName,
                                  const nsACString& aFailedAlpn,
                                  bool& aAllRecordsHaveEchConfig);
  // This function setups a new connection info for restarting this transaction.
  void PrepareConnInfoForRetry(nsresult aReason);
  // This function is used to select the next non http3 record and is only
  // executed when the fast fallback timer is triggered.
  already_AddRefed<nsHttpConnectionInfo> PrepareFastFallbackConnInfo(
      bool aEchConfigUsed);

  void MaybeReportFailedSVCDomain(nsresult aReason,
                                  nsHttpConnectionInfo* aFailedConnInfo);

  void FinalizeConnInfo();

  // IMPORTANT: when adding new values, always add them to the end, otherwise
  // it will mess up telemetry.
  enum HTTPSSVC_CONNECTION_FAILED_REASON : uint32_t {
    HTTPSSVC_CONNECTION_OK = 0,
    HTTPSSVC_CONNECTION_UNKNOWN_HOST = 1,
    HTTPSSVC_CONNECTION_UNREACHABLE = 2,
    HTTPSSVC_CONNECTION_421_RECEIVED = 3,
    HTTPSSVC_CONNECTION_SECURITY_ERROR = 4,
    HTTPSSVC_CONNECTION_NO_USABLE_RECORD = 5,
    HTTPSSVC_CONNECTION_ALL_RECORDS_EXCLUDED = 6,
    HTTPSSVC_CONNECTION_OTHERS = 7,
  };
  HTTPSSVC_CONNECTION_FAILED_REASON ErrorCodeToFailedReason(
      nsresult aErrorCode);

  void OnHttp3BackupTimer();
  void OnBackupConnectionReady(bool aTriggeredByHTTPSRR);
  void OnFastFallbackTimer();
  void OnHttp3TunnelFallbackTimer();
  void HandleFallback(nsHttpConnectionInfo* aFallbackConnInfo);
  void MaybeCancelFallbackTimer();

  // IMPORTANT: when adding new values, always add them to the end, otherwise
  // it will mess up telemetry.
  enum TRANSACTION_RESTART_REASON : uint32_t {
    TRANSACTION_RESTART_NONE = 0,  // The transacion was not restarted.
    TRANSACTION_RESTART_FORCED,    // The transaction was forced to restart.
    TRANSACTION_RESTART_NO_DATA_SENT,
    TRANSACTION_RESTART_DOWNGRADE_WITH_EARLY_DATA,
    TRANSACTION_RESTART_HTTPS_RR_NET_RESET,
    TRANSACTION_RESTART_HTTPS_RR_CONNECTION_REFUSED,
    TRANSACTION_RESTART_HTTPS_RR_UNKNOWN_HOST,
    TRANSACTION_RESTART_HTTPS_RR_NET_TIMEOUT,
    TRANSACTION_RESTART_HTTPS_RR_SEC_ERROR,
    TRANSACTION_RESTART_HTTPS_RR_FAST_FALLBACK,
    TRANSACTION_RESTART_HTTP3_FAST_FALLBACK,
    TRANSACTION_RESTART_OTHERS,
    TRANSACTION_RESTART_PROTOCOL_VERSION_ALERT,
    TRANSACTION_RESTART_POSSIBLE_0RTT_ERROR
  };
  void SetRestartReason(TRANSACTION_RESTART_REASON aReason);
  bool MaybeForceRestart(const char* aLogMessage);

  bool HandleWebTransportResponse(uint16_t aStatus);

  void MaybeRefreshSecurityInfo() {
    MutexAutoLock lock(mLock);
    if (mConnection) {
      nsCOMPtr<nsITLSSocketControl> tlsSocketControl;
      mConnection->GetTLSSocketControl(getter_AddRefs(tlsSocketControl));
      if (tlsSocketControl) {
        tlsSocketControl->GetSecurityInfo(getter_AddRefs(mSecurityInfo));
      }
    }
  }

 private:
  class UpdateSecurityCallbacks : public Runnable, public nsIRunnablePriority {
   public:
    UpdateSecurityCallbacks(nsHttpTransaction* aTrans,
                            nsIInterfaceRequestor* aCallbacks,
                            uint32_t aPriority)
        : Runnable("net::nsHttpTransaction::UpdateSecurityCallbacks"),
          mTrans(aTrans),
          mCallbacks(aCallbacks),
          mPriority(aPriority) {}

    NS_DECL_ISUPPORTS_INHERITED
    NS_DECL_NSIRUNNABLEPRIORITY

    NS_IMETHOD Run() override {
      if (mTrans->mConnection) {
        mTrans->mConnection->SetSecurityCallbacks(mCallbacks);
      }
      return NS_OK;
    }

   private:
    virtual ~UpdateSecurityCallbacks() = default;

    RefPtr<nsHttpTransaction> mTrans;
    nsCOMPtr<nsIInterfaceRequestor> mCallbacks;
    uint32_t mPriority;
  };

  // Fields annotated MOZ_GUARDED_BY(mLock) are accessed from multiple threads.
  // Fields without annotation are either socket-thread-only, or have complex
  // mixed-access patterns (e.g. mConnection is checked without the lock on the
  // socket thread but modified with it; mChunkedDecoder is accessed without the
  // lock except during trailer extraction).
  Mutex mLock{"transaction lock"};

  nsCOMPtr<nsIInterfaceRequestor> mCallbacks MOZ_GUARDED_BY(mLock);
  nsCOMPtr<nsITransportEventSink> mTransportSink;
  nsCOMPtr<nsIEventTarget> mConsumerTarget;
  nsCOMPtr<nsITransportSecurityInfo> mSecurityInfo MOZ_GUARDED_BY(mLock);
  nsCOMPtr<nsIAsyncInputStream> mPipeIn;
  nsCOMPtr<nsIAsyncOutputStream> mPipeOut;
  nsCOMPtr<nsIRequestContext> mRequestContext;

  uint64_t mChannelId{0};

  nsCString mReqHeaderBuf;  // flattened request headers
  nsCOMPtr<nsIInputStream> mRequestStream;
  int64_t mRequestSize{0};

  RefPtr<nsAHttpConnection> mConnection;
  RefPtr<nsHttpConnectionInfo> mConnInfo;
  // This is only set in UpdateConnectionInfo() when we have received a SVCB RR.
  // When echConfig is not used and the connection is failed, this transaction
  // will be restarted with this origin connection info directly.
  // When echConfig is enabled, there are two cases below.
  // 1. If all records have echConfig, we will retry other records except the
  // failed one. In the case all other records with echConfig are failed and the
  // pref network.dns.echconfig.fallback_to_origin_when_all_failed is true, this
  // origin connection info will be used.
  // 2. If only some records have echConfig and some not, we always fallback to
  // this origin conn info.
  RefPtr<nsHttpConnectionInfo> mOrigConnInfo;
  nsHttpRequestHead* mRequestHead{nullptr};    // weak ref
  nsHttpResponseHead* mResponseHead{nullptr};  // owning pointer

  nsAHttpSegmentReader* mReader{nullptr};
  nsAHttpSegmentWriter* mWriter{nullptr};

  nsCString mLineBuf;  // may contain a partial line

  int64_t mContentLength{-1};  // equals -1 if unknown
  int64_t mContentRead{0};     // count of consumed content bytes
  Atomic<int64_t, ReleaseAcquire> mTransferSize{0};  // count of received bytes

  // After a 304/204 or other "no-content" style response we will skip over
  // up to MAX_INVALID_RESPONSE_BODY_SZ bytes when looking for the next
  // response header to deal with servers that actually sent a response
  // body where they should not have. This member tracks how many bytes have
  // so far been skipped.
  uint32_t mInvalidResponseBytesRead{0};

  uint32_t mInitialRwin{0};

  nsHttpChunkedDecoder* mChunkedDecoder{nullptr};

  TimingStruct mTimings MOZ_GUARDED_BY(mLock);

  nsresult mStatus{NS_OK};

  int16_t mPriority{0};

  // the number of times this transaction has been restarted
  uint16_t mRestartCount{0};
  Atomic<uint32_t, ReleaseAcquire> mCaps{0};

  HttpVersion mHttpVersion{HttpVersion::UNKNOWN};
  uint16_t mHttpResponseCode{0};
  nsCString mFlat407Headers;

  uint32_t mCurrentHttpResponseHeaderSize{0};

  int32_t const THROTTLE_NO_LIMIT = -1;
  // This can have 3 possible values:
  // * THROTTLE_NO_LIMIT - this means the transaction is not in any way limited
  //                       to read the response, this is the default
  // * a positive number - a limit is set because the transaction is obligated
  //                       to throttle the response read, this is decresed with
  //                       every piece of data the transaction receives
  // * zero - when the transaction depletes the limit for reading, this makes it
  //          stop reading and return WOULD_BLOCK from WriteSegments;
  //          transaction then waits for a call of ResumeReading that resets
  //          this member back to THROTTLE_NO_LIMIT
  int32_t mThrottlingReadAllowance{THROTTLE_NO_LIMIT};

  // mCapsToClear holds flags that should be cleared in mCaps, e.g. unset
  // NS_HTTP_REFRESH_DNS when DNS refresh request has completed to avoid
  // redundant requests on the network. The member itself is atomic, but
  // access to it from the networking thread may happen either before or
  // after the main thread modifies it. To deal with raciness, only unsetting
  // bitfields should be allowed: 'lost races' will thus err on the
  // conservative side, e.g. by going ahead with a 2nd DNS refresh.
  Atomic<uint32_t> mCapsToClear{0};
  Atomic<bool, ReleaseAcquire> mResponseIsComplete{false};
  Atomic<bool, ReleaseAcquire> mClosed{false};
  Atomic<bool, Relaxed> mIsHttp3Used{false};

  // True iff WriteSegments was called while this transaction should be
  // throttled (stop reading) Used to resume read on unblock of reading.  Conn
  // manager is responsible for calling back to resume reading.
  bool mReadingStopped{false};

  // state flags, all logically boolean, but not packed together into a
  // bitfield so as to avoid bitfield-induced races.  See bug 560579.
  bool mConnected{false};
  bool mActivated{false};
  bool mHaveStatusLine{false};
  bool mHaveAllHeaders{false};
  bool mTransactionDone{false};
  bool mDidContentStart{false};
  bool mNoContent{false};  // expecting an empty entity body
  bool mSentData{false};
  bool mReceivedData{false};
  bool mStatusEventPending{false};
  bool mHasRequestBody{false};
  bool mProxyConnectFailed{false};
  bool mHttpResponseMatched{false};
  bool mPreserveStream{false};
  bool mDispatchedAsBlocking{false};
  bool mResponseTimeoutEnabled{true};
  bool mForceRestart{false};
  bool mReuseOnRestart{false};
  bool mContentDecoding{false};
  bool mContentDecodingCheck{false};
  bool mDeferredSendProgress{false};
  bool mWaitingOnPipeOut{false};
  bool mDoNotRemoveAltSvc{false};
  bool mDoNotResetIPFamilyPreference{false};
  bool mIsHttp2Websocket{false};
  bool mIsTRRTransaction{false};

  // mClosed           := transaction has been explicitly closed
  // mTransactionDone  := transaction ran to completion or was interrupted
  // mResponseComplete := transaction ran to completion

  // For Restart-In-Progress Functionality
  bool mReportedStart{false};
  bool mReportedResponseHeader{false};

  // protected by nsHttp::GetLock()
  bool mResponseHeadTaken{false};
  UniquePtr<nsHttpHeaderArray> mForTakeResponseTrailers;
  bool mResponseTrailersTaken{false};

  // Set when this transaction was restarted by call to Restart().  Used to tell
  // the http channel to reset proxy authentication.
  Atomic<bool> mRestarted{false};

  // The time when the transaction was submitted to the Connection Manager
  TimeDuration mPendingDurationTime;

  uint64_t mBrowserId{0};

  // IP address space of the browsing context that triggered this request
  nsILoadInfo::IPAddressSpace mParentIPAddressSpace{
      nsILoadInfo::IPAddressSpace::Unknown};
  struct LNAPerms mLnaPermissionStatus{};

  // For Rate Pacing via an EventTokenBucket
 public:
  // called by the connection manager to run this transaction through the
  // token bucket. If the token bucket admits the transaction immediately it
  // returns true. The function is called repeatedly until it returns true.
  bool TryToRunPacedRequest();

  // ATokenBucketEvent pure virtual implementation. Called by the token bucket
  // when the transaction is ready to run. If this happens asynchrounously to
  // token bucket submission the transaction just posts an event that causes
  // the pending transaction queue to be rerun (and TryToRunPacedRequest() to
  // be run again.
  void OnTokenBucketAdmitted() override;  // ATokenBucketEvent

  // CancelPacing() can be used to tell the token bucket to remove this
  // transaction from the list of pending transactions. This is used when a
  // transaction is believed to be HTTP/1 (and thus subject to rate pacing)
  // but later can be dispatched via spdy (not subject to rate pacing).
  void CancelPacing(nsresult reason);

  // Called by the connetion manager on the socket thread when reading for this
  // previously throttled transaction has to be resumed.
  void ResumeReading();

  // This examins classification of this transaction whether the Throttleable
  // class has been set while Leader, Unblocked, DontThrottle has not.
  bool EligibleForThrottling() const;

  bool AllowedToConnectToIpAddressSpace(
      nsILoadInfo::IPAddressSpace aTargetIpAddressSpace) override;

 private:
  bool mSubmittedRatePacing{false};
  bool mPassedRatePacing{false};
  bool mSynchronousRatePaceRequest{false};
  nsCOMPtr<nsICancelable> mTokenBucketCancel;

  void CollectTelemetryForUploads();

 public:
  ClassOfService GetClassOfService() {
    return {mClassOfServiceFlags, mClassOfServiceIncremental};
  }

 private:
  Atomic<uint32_t, Relaxed> mClassOfServiceFlags{0};
  Atomic<bool, Relaxed> mClassOfServiceIncremental{false};

 public:
  nsIInterfaceRequestor* SecurityCallbacks() {
    MutexAutoLock lock(mLock);
    return mCallbacks;
  }
  // Called when this transaction is inserted in the pending queue.
  void OnPendingQueueInserted(const nsACString& aConnectionHashKey);

 private:
  TransactionObserverFunc mTransactionObserver;
  NetAddr mSelfAddr;
  NetAddr mPeerAddr;
  nsILoadInfo::IPAddressSpace mTargetIpAddressSpace{
      nsILoadInfo::IPAddressSpace::Unknown};
  bool mResolvedByTRR{false};
  Atomic<nsIRequest::TRRMode, Relaxed> mEffectiveTRRMode{
      nsIRequest::TRR_DEFAULT_MODE};
  Atomic<TRRSkippedReason, Relaxed> mTRRSkipReason{nsITRRSkipReason::TRR_UNSET};
  bool mEchConfigUsed = false;

  bool m0RTTInProgress{false};
  bool mDoNotTryEarlyData{false};
  enum {
    EARLY_NONE,
    EARLY_SENT,
    EARLY_ACCEPTED,
    EARLY_425
  } mEarlyDataDisposition{EARLY_NONE};
  // The moment the request bytes were first sent as TLS 1.3 early data
  // (0-RTT). For an accepted 0-RTT request this is the point the connection
  // could send the request, i.e. the W3C Resource Timing connectEnd (which,
  // per spec, must exclude the round trip spent receiving the server's
  // ServerHello). See Apply0RTTTimingOverride.
  TimeStamp mEarlyDataSentTime;

  HttpTrafficCategory mTrafficCategory{HttpTrafficCategory::eInvalid};
  // Shared pointer to the full CONNECT response head, owned by the connection.
  // Held here only so it survives the transaction being detached from the
  // connection; copying the RefPtr is just an addref rather than a deep copy
  // of the header array on every request. See bug 2045419.
  RefPtr<ProxyConnectResponseHead> mProxyConnectResponseHead
      MOZ_GUARDED_BY(mLock);

  nsCOMPtr<nsICancelable> mDNSRequest;
  Atomic<uint32_t, Relaxed> mHTTPSSVCReceivedStage{HTTPSSVC_NOT_USED};
  bool m421Received = false;
  nsCOMPtr<nsIDNSHTTPSSVCRecord> mHTTPSSVCRecord;
  nsTArray<RefPtr<nsISVCBRecord>> mRecordsForRetry;
  bool mDontRetryWithDirectRoute = false;
  bool mFastFallbackTriggered = false;
  bool mHttp3BackupTimerCreated = false;
  bool mHttp3TunnelFallbackTimerCreated = false;
  nsCOMPtr<nsITimer> mFastFallbackTimer;
  nsCOMPtr<nsITimer> mHttp3BackupTimer;
  nsCOMPtr<nsITimer> mHttp3TunnelFallbackTimer;
  RefPtr<nsHttpConnectionInfo> mBackupConnInfo;
  // A clone of mConnInfo taken when this transaction is activated.
  // Describes the server that the associated connection is connected to.
  RefPtr<nsHttpConnectionInfo> mFinalizedConnInfo MOZ_GUARDED_BY(mLock);
  RefPtr<HTTPSRecordResolver> mResolver;
  TRANSACTION_RESTART_REASON mRestartReason = TRANSACTION_RESTART_NONE;

  enum TRANSACTION_ECH_RETRY_COUNT : uint32_t {
    TRANSACTION_ECH_RETRY_OTHERS_COUNT = 0,
    TRANSACTION_ECH_RETRY_WITH_ECH_COUNT = 1,
    TRANSACTION_ECH_RETRY_WITHOUT_ECH_COUNT = 2,
    TRANSACTION_ECH_RETRY_ECH_FAILED_COUNT = 3,
  };
  nsTHashMap<nsUint32HashKey, uint32_t> mEchRetryCounterMap;

  bool mSupportsHTTP3 = false;
  Atomic<bool, Relaxed> mIsForWebTransport{false};
  bool mIsResettingForTunnelConn = false;
  Maybe<bool> mIsWebsocketUpgrade;

  bool mResumptionAttempted = false;
  void OnPSKResumptionAccepted() override;
  bool ShouldRestartOnResumptionError(nsresult reason);

  nsCOMPtr<nsIEarlyHintObserver> mEarlyHintObserver MOZ_GUARDED_BY(mLock);
  // This hash key is set when a transaction is inserted into the connection
  // entry's pending queue.
  // See nsHttpConnectionMgr::GetOrCreateConnectionEntry(). A transaction could
  // be associated with the connection entry whose hash key is not the same as
  // this transaction's.
  nsCString mHashKeyOfConnectionEntry MOZ_GUARDED_BY(mLock);
  // The CNAME of the host, or empty if none.
  nsCString mCname;
  nsCString mServerHeader;

  nsCOMPtr<WebTransportSessionEventListener> mWebTransportSessionEventListener
      MOZ_GUARDED_BY(mLock);

  nsAutoCString mUrl;
};

}  // namespace mozilla::net

#endif  // nsHttpTransaction_h_
