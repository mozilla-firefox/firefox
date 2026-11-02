/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "HttpLog.h"

// Log on level :5, instead of default :4.
#undef LOG
#define LOG(args) LOG5(args)
#undef LOG_ENABLED
#define LOG_ENABLED() LOG5_ENABLED()

#include "ASpdySession.h"
#include "NSSErrorsService.h"
#include "TLSTransportLayer.h"
#include "mozilla/ChaosMode.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "mozilla/glean/NetwerkProtocolHttpMetrics.h"
#include "mozpkix/pkixnss.h"
#include "nsCRT.h"
#include "nsHttpConnection.h"
#include "nsHttpConnectionMgr.h"
#include "nsHttpHandler.h"
#include "nsHttpRequestHead.h"
#include "nsHttpResponseHead.h"
#include "nsIClassOfService.h"
#include "nsIOService.h"
#include "nsISocketTransport.h"
#include "nsISupportsPriority.h"
#include "nsITLSSocketControl.h"
#include "nsITransportSecurityInfo.h"
#include "nsPreloadedStream.h"
#include "nsProxyRelease.h"
#include "nsQueryObject.h"
#include "nsSocketTransport2.h"
#include "nsSocketTransportService2.h"
#include "nsStringStream.h"
#include "nsThreadUtils.h"
#include "sslerr.h"
#include "sslt.h"

namespace mozilla::net {
extern const nsCString& TRRProviderKey();
}

namespace mozilla::net {

//-----------------------------------------------------------------------------
// nsHttpConnection <public>
//-----------------------------------------------------------------------------

nsHttpConnection::nsHttpConnection() : mHttpHandler(gHttpHandler) {
  LOG(("Creating nsHttpConnection @%p\n", this));

  // the default timeout is for when this connection has not yet processed a
  // transaction
  static const PRIntervalTime k5Sec = PR_SecondsToInterval(5);
  mIdleTimeout = (k5Sec < gHttpHandler->IdleTimeout())
                     ? k5Sec
                     : gHttpHandler->IdleTimeout();
}

nsHttpConnection::~nsHttpConnection() {
  LOG(("Destroying nsHttpConnection @%p\n", this));

  if (!mEverUsedSpdy) {
    LOG(("nsHttpConnection %p performed %d HTTP/1.x transactions\n", this,
         mHttp1xTransactionCount));
    glean::http::request_per_conn.AccumulateSingleSample(
        mHttp1xTransactionCount);
    nsHttpConnectionInfo* ci = nullptr;
    if (mTransaction) {
      ci = mTransaction->ConnectionInfo();
    }
    if (!ci) {
      ci = mConnInfo;
    }

    MOZ_ASSERT(ci);
    if (ci->GetIsTrrServiceChannel() && mHttp1xTransactionCount) {
      mozilla::glean::networking::trr_request_count_per_conn
          .Get(nsPrintfCString("%s_h1", ci->Origin()))
          .Add(static_cast<int32_t>(mHttp1xTransactionCount));
    }
  }

  if (mTotalBytesRead) {
    uint32_t totalKBRead = static_cast<uint32_t>(mTotalBytesRead >> 10);
    LOG(("nsHttpConnection %p read %dkb on connection spdy=%d\n", this,
         totalKBRead, mEverUsedSpdy));
    if (mEverUsedSpdy) {
      glean::spdy::kbread_per_conn.Accumulate(totalKBRead);
    } else {
      glean::http::kbread_per_conn2.Accumulate(totalKBRead);
    }
  }

  if (mForceSendTimer) {
    mForceSendTimer->Cancel();
    mForceSendTimer = nullptr;
  }

  auto ReleaseSocketTransport =
      [socketTransport(std::move(mSocketTransport))]() mutable {
        socketTransport = nullptr;
      };
  if (OnSocketThread()) {
    ReleaseSocketTransport();
  } else {
    gSocketTransportService->Dispatch(NS_NewRunnableFunction(
        "nsHttpConnection::~nsHttpConnection", ReleaseSocketTransport));
  }
}

nsresult nsHttpConnection::Init(
    nsHttpConnectionInfo* info, uint16_t maxHangTime,
    nsISocketTransport* transport, nsIAsyncInputStream* instream,
    nsIAsyncOutputStream* outstream, bool connectedTransport, nsresult status,
    nsIInterfaceRequestor* callbacks, PRIntervalTime rtt, bool forWebSocket) {
  LOG1(("nsHttpConnection::Init this=%p sockettransport=%p forWebSocket=%d",
        this, transport, forWebSocket));
  NS_ENSURE_ARG_POINTER(info);
  NS_ENSURE_TRUE(!mConnInfo, NS_ERROR_ALREADY_INITIALIZED);
  MOZ_ASSERT(NS_SUCCEEDED(status) || !connectedTransport);

  mConnectedTransport = connectedTransport;
  mConnInfo = info;
  MOZ_ASSERT(mConnInfo);

  mLastWriteTime = mLastReadTime = PR_IntervalNow();
  mRtt = rtt;
  mMaxHangTime = PR_SecondsToInterval(maxHangTime);

  mSocketTransport = transport;
  mSocketIn = instream;
  mSocketOut = outstream;
  mForWebSocket = forWebSocket;

  InitCallbacks(callbacks, "nsHttpConnection::mCallbacks");

  mErrorBeforeConnect = status;
  if (NS_SUCCEEDED(mErrorBeforeConnect)) {
    mSocketTransport->SetEventSink(this, nullptr);
    mSocketTransport->SetSecurityCallbacks(this);
    ChangeConnectionState(ConnectionState::INITED);
  } else {
    SetCloseReason(ToCloseReason(mErrorBeforeConnect));
  }

  mTlsHandshaker = new TlsHandshaker(mConnInfo, this);
  return NS_OK;
}

nsresult nsHttpConnection::TryTakeSubTransactions(
    nsTArray<RefPtr<nsAHttpTransaction> >& list) {
  nsresult rv = mTransaction->TakeSubTransactions(list);

  if (rv == NS_ERROR_ALREADY_OPENED) {
    // Has the interface for TakeSubTransactions() changed?
    LOG(
        ("TakeSubTransactions somehow called after "
         "nsAHttpTransaction began processing\n"));
    MOZ_ASSERT(false,
               "TakeSubTransactions somehow called after "
               "nsAHttpTransaction began processing");
    mTransaction->Close(NS_ERROR_ABORT);
    return rv;
  }

  if (NS_FAILED(rv) && rv != NS_ERROR_NOT_IMPLEMENTED) {
    // Has the interface for TakeSubTransactions() changed?
    LOG(("unexpected rv from nnsAHttpTransaction::TakeSubTransactions()"));
    MOZ_ASSERT(false,
               "unexpected result from "
               "nsAHttpTransaction::TakeSubTransactions()");
    mTransaction->Close(NS_ERROR_ABORT);
    return rv;
  }

  return rv;
}

void nsHttpConnection::ResetTransaction(RefPtr<nsAHttpTransaction>&& trans,
                                        bool aForH2Proxy) {
  MOZ_ASSERT(trans);
  mSpdySession->SetConnection(trans->Connection());
  trans->SetConnection(nullptr);
  trans->DoNotRemoveAltSvc();
  if (!aForH2Proxy) {
    // Only do this when this is for websocket or webtransport over HTTP/2.
    trans->SetResettingForTunnelConn(true);
  }
  if (trans->IsForFallback()) {
    trans->InvokeCallback();
    trans->Close(NS_OK);
  } else {
    trans->Close(NS_ERROR_NET_RESET);
  }
}

nsresult nsHttpConnection::MoveTransactionsToSpdy(
    nsresult status, nsTArray<RefPtr<nsAHttpTransaction> >& list) {
  if (NS_FAILED(status)) {  // includes NS_ERROR_NOT_IMPLEMENTED
    MOZ_ASSERT(list.IsEmpty(), "sub transaction list not empty");

    // If this transaction is used to drive websocket or webtransport, we reset
    // it to put it in the pending queue. Once we know if the server supports
    // websocket or not, the pending queue will be processed.
    nsHttpTransaction* trans = mTransaction->QueryHttpTransaction();
    if (trans && (trans->IsWebsocketUpgrade() || trans->IsForWebTransport())) {
      LOG(
          ("nsHttpConnection resetting transaction for websocket or "
           "webtransport upgrade"));
      // websocket upgrade needs NonSticky for transaction reset
      mTransaction->MakeNonSticky();
      ResetTransaction(std::move(mTransaction));
      mTransaction = nullptr;
      return NS_OK;
    }

    // This is ok - treat mTransaction as a single real request.
    // Wrap the old http transaction into the new spdy session
    // as the first stream.
    LOG(
        ("nsHttpConnection::MoveTransactionsToSpdy moves single transaction %p "
         "into SpdySession %p\n",
         mTransaction.get(), mSpdySession.get()));
    nsresult rv = AddTransaction(mTransaction, mPriority);
    if (NS_FAILED(rv)) {
      return rv;
    }
  } else {
    int32_t count = list.Length();

    LOG(
        ("nsHttpConnection::MoveTransactionsToSpdy moving transaction list "
         "len=%d "
         "into SpdySession %p\n",
         count, mSpdySession.get()));

    if (!count) {
      mTransaction->Close(NS_ERROR_ABORT);
      return NS_ERROR_ABORT;
    }

    for (int32_t index = 0; index < count; ++index) {
      RefPtr<nsAHttpTransaction> transaction = list[index];
      nsHttpTransaction* trans = transaction->QueryHttpTransaction();
      if (trans &&
          (trans->IsWebsocketUpgrade() || trans->IsForWebTransport())) {
        LOG(
            ("nsHttpConnection resetting a transaction for websocket or "
             "webtransport upgrade"));
        // websocket upgrade needs NonSticky for transaction reset
        transaction->MakeNonSticky();
        ResetTransaction(std::move(transaction));
        transaction = nullptr;
        continue;
      }
      nsresult rv = AddTransaction(list[index], mPriority);
      if (NS_FAILED(rv)) {
        return rv;
      }
    }
  }

  return NS_OK;
}

void nsHttpConnection::Start0RTTSpdy(SpdyVersion spdyVersion) {
  LOG(("nsHttpConnection::Start0RTTSpdy [this=%p]", this));

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  mDid0RTTSpdy = true;
  mUsingSpdyVersion = spdyVersion;
  mEverUsedSpdy = true;
  mSpdySession =
      ASpdySession::NewSpdySession(spdyVersion, mSocketTransport, true);

  if (mTransaction) {
    nsTArray<RefPtr<nsAHttpTransaction> > list;
    nsresult rv = TryTakeSubTransactions(list);
    if (NS_FAILED(rv) && rv != NS_ERROR_NOT_IMPLEMENTED) {
      LOG(
          ("nsHttpConnection::Start0RTTSpdy [this=%p] failed taking "
           "subtransactions rv=%" PRIx32,
           this, static_cast<uint32_t>(rv)));
      return;
    }

    rv = MoveTransactionsToSpdy(rv, list);
    if (NS_FAILED(rv)) {
      LOG(
          ("nsHttpConnection::Start0RTTSpdy [this=%p] failed moving "
           "transactions rv=%" PRIx32,
           this, static_cast<uint32_t>(rv)));
      return;
    }
  }

  mTransaction = mSpdySession;
}

void nsHttpConnection::StartSpdy(nsITLSSocketControl* sslControl,
                                 SpdyVersion spdyVersion) {
  LOG(("nsHttpConnection::StartSpdy [this=%p, mDid0RTTSpdy=%d]\n", this,
       mDid0RTTSpdy));

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(!mSpdySession || mDid0RTTSpdy);

  mUsingSpdyVersion = spdyVersion;
  mEverUsedSpdy = true;
  if (sslControl) {
    sslControl->SetDenyClientCert(true);
  }

  if (!mDid0RTTSpdy) {
    mSpdySession =
        ASpdySession::NewSpdySession(spdyVersion, mSocketTransport, false);
  }

  if (!mReportedSpdy) {
    mReportedSpdy = true;
    // See bug 1797729.
    // It's possible that we already have a HTTP/3 connection that can be
    // coleased with this connection. We should avoid coalescing with the
    // existing HTTP/3 connection if the transaction doesn't allow to use
    // HTTP/3.
    gHttpHandler->ConnMgr()->ReportSpdyConnection(this, true,
                                                  mTransactionDisallowHttp3);
  }

  // Setting the connection as reused allows some transactions that fail
  // with NS_ERROR_NET_RESET to be restarted and SPDY uses that code
  // to handle clean rejections (such as those that arrived after
  // a server goaway was generated).
  mIsReused = true;

  // If mTransaction is a muxed object it might represent
  // several requests. If so, we need to unpack that and
  // pack them all into a new spdy session.

  nsTArray<RefPtr<nsAHttpTransaction> > list;
  nsresult status = NS_OK;
  if (!mDid0RTTSpdy && mTransaction) {
    status = TryTakeSubTransactions(list);

    if (NS_FAILED(status) && status != NS_ERROR_NOT_IMPLEMENTED) {
      return;
    }
  }

  if (NeedSpdyTunnel()) {
    LOG3(
        ("nsHttpConnection::StartSpdy %p Connecting To a HTTP/2 "
         "Proxy and Need Connect",
         this));
    SetTunnelSetupDone();
  }

  nsresult rv = NS_OK;
  bool spdyProxy = mConnInfo->UsingHttpsProxy() && mConnInfo->UsingConnect() &&
                   !mHasTLSTransportLayer;
  if (spdyProxy) {
    RefPtr<nsHttpConnectionInfo> wildCardProxyCi;
    rv = mConnInfo->CreateWildCard(getter_AddRefs(wildCardProxyCi));
    MOZ_ASSERT(NS_SUCCEEDED(rv));
    gHttpHandler->ConnMgr()->MoveToWildCardConnEntry(mConnInfo, wildCardProxyCi,
                                                     this);
    mConnInfo = wildCardProxyCi;
    MOZ_ASSERT(mConnInfo);
  }

  if (!mDid0RTTSpdy && mTransaction) {
    if (spdyProxy) {
      if (NS_FAILED(status)) {
        // proxy upgrade needs Restartable for transaction reset
        // note that using NonSticky here won't work because it breaks
        // netwerk/test/unit/test_websocket_server.js - h1 ws with h2 proxy
        mTransaction->MakeRestartable();
        ResetTransaction(std::move(mTransaction), true);
        mTransaction = nullptr;
      } else {
        for (const auto& trans : list) {
          if (!mSpdySession->Connection()) {
            mSpdySession->SetConnection(trans->Connection());
          }
          trans->SetConnection(nullptr);
          trans->DoNotRemoveAltSvc();
          trans->Close(NS_ERROR_NET_RESET);
        }
      }
    } else {
      rv = MoveTransactionsToSpdy(status, list);
      if (NS_FAILED(rv)) {
        return;
      }
    }
  }

  // Disable TCP Keepalives - use SPDY ping instead.
  rv = DisableTCPKeepalives();
  if (NS_FAILED(rv)) {
    LOG(
        ("nsHttpConnection::StartSpdy [%p] DisableTCPKeepalives failed "
         "rv[0x%" PRIx32 "]",
         this, static_cast<uint32_t>(rv)));
  }

  mIdleTimeout = gHttpHandler->SpdyTimeout() * mDefaultTimeoutFactor;

  mTransaction = mSpdySession;

  if (mDontReuse) {
    mSpdySession->DontReuse();
  }
}

void nsHttpConnection::OnClientAuthCertificateRequested() {
  if (mTransaction) {
    mTransaction->OnClientAuthCertificateRequested();
  }
}

void nsHttpConnection::OnClientAuthCertificateSelected() {
  if (mTransaction) {
    mTransaction->OnClientAuthCertificateSelected();
  }
}

void nsHttpConnection::PostProcessNPNSetup(bool handshakeSucceeded,
                                           bool hasSecurityInfo,
                                           bool earlyDataUsed) {
  if (mTransaction) {
    mTransaction->OnTransportStatus(mSocketTransport,
                                    NS_NET_STATUS_TLS_HANDSHAKE_ENDED, 0);
    if (handshakeSucceeded) {
      mTransaction->OnPSKResumptionAccepted();
    }
  }

  // this is happening after the bootstrap was originally written to. so update
  // it.
  if (mTransaction && mTransaction->QueryNullTransaction()) {
    if (mBootstrappedTimings.secureConnectionStart.IsNull()) {
      mBootstrappedTimings.secureConnectionStart =
          mTransaction->QueryNullTransaction()->GetSecureConnectionStart();
    }
    if (mBootstrappedTimings.tcpConnectEnd.IsNull()) {
      mBootstrappedTimings.tcpConnectEnd =
          mTransaction->QueryNullTransaction()->GetTcpConnectEnd();
    }
  }

  if (hasSecurityInfo) {
    mBootstrappedTimings.connectEnd = TimeStamp::Now();
  }

  if (earlyDataUsed) {
    // Didn't get 0RTT OK, back out of the "attempting 0RTT" state
    LOG(("nsHttpConnection::PostProcessNPNSetup [this=%p] 0rtt failed", this));
    if (mTransaction && NS_FAILED(mTransaction->Finish0RTT(true, true))) {
      mTransaction->Close(NS_ERROR_NET_RESET);
    }
    mContentBytesWritten0RTT = 0;
    if (mDid0RTTSpdy) {
      Reset0RttForSpdy();
    }
  }

  if (hasSecurityInfo) {
    // Telemetry for tls failure rate with and without esni;
    bool echConfigUsed = false;
    mSocketTransport->GetEchConfigUsed(&echConfigUsed);
    glean::http::EchconfigSuccessRateLabel label =
        echConfigUsed
            ? (handshakeSucceeded
                   ? glean::http::EchconfigSuccessRateLabel::eEchconfigsucceeded
                   : glean::http::EchconfigSuccessRateLabel::eEchconfigfailed)
            : (handshakeSucceeded ? glean::http::EchconfigSuccessRateLabel::
                                        eNoechconfigsucceeded
                                  : glean::http::EchconfigSuccessRateLabel::
                                        eNoechconfigfailed);
    glean::http::echconfig_success_rate.EnumGet(label).Add();
  }

  // Capture the NSS handshake error from the security info so that
  // Speculative-driven HE racers (HappyEyeballsTransaction::ReadSegments)
  // can return the real cert/protocol error from their otherwise-empty
  // ReadSegments.
  if (!handshakeSucceeded && hasSecurityInfo && mSocketTransport) {
    nsCOMPtr<nsITLSSocketControl> tlsCtrl;
    if (NS_SUCCEEDED(
            mSocketTransport->GetTlsSocketControl(getter_AddRefs(tlsCtrl))) &&
        tlsCtrl) {
      nsCOMPtr<nsITransportSecurityInfo> secInfo;
      if (NS_SUCCEEDED(tlsCtrl->GetSecurityInfo(getter_AddRefs(secInfo))) &&
          secInfo) {
        int32_t prErrorCode = 0;
        if (NS_SUCCEEDED(secInfo->GetErrorCode(&prErrorCode)) && prErrorCode) {
          mHandshakeError = mozilla::psm::GetXPCOMFromNSSError(prErrorCode);
          LOG(
              ("nsHttpConnection::PostProcessNPNSetup [this=%p] captured "
               "TLS handshake error rv=%" PRIx32 " (PRErrorCode=%d)",
               this, static_cast<uint32_t>(mHandshakeError), prErrorCode));
          if (prErrorCode == SSL_ERROR_ECH_RETRY_WITH_ECH) {
            if (NS_FAILED(tlsCtrl->GetRetryEchConfig(mRetryEchConfig))) {
              mRetryEchConfig.Truncate();
            }
            LOG(
                ("nsHttpConnection::PostProcessNPNSetup [this=%p] cached "
                 "retry ECH config len=%zu",
                 this, mRetryEchConfig.Length()));
          } else if (prErrorCode == SSL_ERROR_ECH_RETRY_WITHOUT_ECH) {
            mRetryEchConfig.Truncate();
          }
        }
      }
    }
  }
}

void nsHttpConnection::Reset0RttForSpdy() {
  // Reset the work done by Start0RTTSpdy
  mUsingSpdyVersion = SpdyVersion::NONE;
  mTransaction = nullptr;
  mSpdySession = nullptr;
  // We have to reset this here, just in case we end up starting spdy again,
  // so it can actually do everything it needs to do.
  mDid0RTTSpdy = false;
}

// called on the socket thread
nsresult nsHttpConnection::Activate(nsAHttpTransaction* trans, uint32_t caps,
                                    int32_t pri) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG1(("nsHttpConnection::Activate [this=%p trans=%p caps=%x]\n", this, trans,
        caps));

  if (!mExperienced && !trans->IsNullTransaction()) {
    mHasFirstHttpTransaction = true;
    if (mTlsHandshaker->NPNComplete()) {
      mExperienced = true;
    }
    if (nsHttpTransaction* hTrans = trans->QueryHttpTransaction()) {
      SetUrgentStartPreferred(hTrans->GetClassOfService().Flags() &
                              nsIClassOfService::UrgentStart);
    }
  }
  HandOffConnectPhase(trans);

  if (caps & NS_HTTP_LARGE_KEEPALIVE) {
    mDefaultTimeoutFactor = StaticPrefs::network_http_largeKeepaliveFactor();
  }

  mTransactionCaps = caps;
  mPriority = pri;

  if (mHasFirstHttpTransaction && mExperienced) {
    mHasFirstHttpTransaction = false;
    mExperienceState |= ConnectionExperienceState::Experienced;
  }

  if (mTransaction && (mUsingSpdyVersion != SpdyVersion::NONE)) {
    return AddTransaction(trans, pri);
  }

  NS_ENSURE_ARG_POINTER(trans);
  NS_ENSURE_TRUE(!mTransaction, NS_ERROR_IN_PROGRESS);

  // reset the read timers to wash away any idle time
  mLastWriteTime = mLastReadTime = PR_IntervalNow();

  // Connection failures are Activated() just like regular transacions.
  // If we don't have a confirmation of a connected socket then test it
  // with a write() to get relevant error code.
  if (NS_FAILED(mErrorBeforeConnect)) {
    mSocketOutCondition = mErrorBeforeConnect;
    mTransaction = trans;
    CloseTransaction(mTransaction, mSocketOutCondition);
    return mSocketOutCondition;
  }

  if (!mConnectedTransport) {
    uint32_t count;
    mSocketOutCondition = NS_ERROR_FAILURE;
    if (mSocketOut) {
      mSocketOutCondition = mSocketOut->Write("", 0, &count);
    }
    if (NS_FAILED(mSocketOutCondition) &&
        mSocketOutCondition != NS_BASE_STREAM_WOULD_BLOCK) {
      LOG(("nsHttpConnection::Activate [this=%p] Bad Socket %" PRIx32 "\n",
           this, static_cast<uint32_t>(mSocketOutCondition)));
      mSocketOut->AsyncWait(nullptr, 0, 0, nullptr);
      mTransaction = trans;
      CloseTransaction(mTransaction, mSocketOutCondition);
      return mSocketOutCondition;
    }
  }

  // take ownership of the transaction
  mTransaction = trans;

  // check if this is LNA failure
  // XXX - This check should be made earlier, possibly in the
  // DnsAndConnectSocket code to avoid unnecessary operations
  // See Bug 1968908
  nsHttpTransaction* httpTrans = mTransaction->QueryHttpTransaction();

  if (httpTrans && httpTrans->Connection() && !mConnInfo->UsingProxy()) {
    NetAddr peerAddr;
    httpTrans->Connection()->GetPeerAddr(&peerAddr);
    auto addrSpace = peerAddr.GetIpAddressSpace();
    // Local addresses are always checked here. Private addresses are only
    // checked here for plaintext HTTP (no TLS handshake to defer to), or
    // when network.lna.defer_https_check is disabled.
    bool deferPrivate = mConnInfo->FirstHopSSL() &&
                        StaticPrefs::network_lna_defer_https_check() &&
                        !mTlsHandshaker->NPNComplete();
    bool checkNow =
        addrSpace == nsILoadInfo::IPAddressSpace::Local ||
        (addrSpace == nsILoadInfo::IPAddressSpace::Private && !deferPrivate);
    if (checkNow && !httpTrans->AllowedToConnectToIpAddressSpace(addrSpace)) {
      mSocketOutCondition = NS_ERROR_LOCAL_NETWORK_ACCESS_DENIED;
      CloseTransaction(mTransaction, mSocketOutCondition);
      return mSocketOutCondition;
    }
  }

  // Update security callbacks
  nsCOMPtr<nsIInterfaceRequestor> callbacks;
  trans->GetSecurityCallbacks(getter_AddRefs(callbacks));
  SetSecurityCallbacks(callbacks);
  mTlsHandshaker->SetupSSL(mInSpdyTunnel, mForcePlainText);
  if (mTlsHandshaker->NPNComplete()) {
    // For non-HTTPS connection, change the state to TRANSFERING directly.
    ChangeConnectionState(ConnectionState::TRANSFERING);
  } else {
    ChangeConnectionState(ConnectionState::TLS_HANDSHAKING);
  }

  nsCOMPtr<nsITLSSocketControl> tlsSocketControl;
  if (NS_SUCCEEDED(mSocketTransport->GetTlsSocketControl(
          getter_AddRefs(tlsSocketControl))) &&
      tlsSocketControl) {
    tlsSocketControl->SetBrowserId(mTransaction->BrowserId());
  }

  MOZ_ASSERT(!mIdleMonitoring, "Activating a connection with an Idle Monitor");
  mIdleMonitoring = false;

  // set mKeepAlive according to what will be requested
  mKeepAliveMask = mKeepAlive = (caps & NS_HTTP_ALLOW_KEEPALIVE);

  mTransactionDisallowHttp3 |= (caps & NS_HTTP_DISALLOW_HTTP3);

  // need to handle HTTP CONNECT tunnels if this is the first time if
  // we are tunneling through a proxy
  nsresult rv = NS_OK;
  // If this is an extended connection then we have already performed the proxy
  // connect and don't need to do it a second time.
  if (!mExtendedCONNECTHttp2Session) {
    rv = CheckTunnelIsNeeded(mTransaction);
  }
  if (NS_FAILED(rv)) goto failed_activation;

  // Clear the per activation counter
  mCurrentBytesRead = 0;

  // The overflow state is not needed between activations
  mInputOverflow = nullptr;

  mResponseTimeoutEnabled = gHttpHandler->ResponseTimeoutEnabled() &&
                            mTransaction->ResponseTimeout() > 0 &&
                            mTransaction->ResponseTimeoutEnabled();

  rv = StartShortLivedTCPKeepalives();
  if (NS_FAILED(rv)) {
    LOG(
        ("nsHttpConnection::Activate [%p] "
         "StartShortLivedTCPKeepalives failed rv[0x%" PRIx32 "]",
         this, static_cast<uint32_t>(rv)));
  }

  trans->OnActivated();

  rv = OnOutputStreamReady(mSocketOut);

  if (NS_SUCCEEDED(rv) && mContinueHandshakeDone) {
    auto continuation = std::move(mContinueHandshakeDone);
    continuation();
  }
  mContinueHandshakeDone = nullptr;

failed_activation:
  if (NS_FAILED(rv)) {
    mTransaction = nullptr;
  }

  return rv;
}

nsresult nsHttpConnection::AddTransaction(nsAHttpTransaction* httpTransaction,
                                          int32_t priority) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(mSpdySession && (mUsingSpdyVersion != SpdyVersion::NONE),
             "AddTransaction to live http connection without spdy/quic");

  // If this is a wild card nshttpconnection (i.e. a spdy proxy) then
  // it is important to start the stream using the specific connection
  // info of the transaction to ensure it is routed on the right tunnel
  nsHttpTransaction* httpTrans = httpTransaction->QueryHttpTransaction();
  nsHttpConnectionInfo* transCI = httpTransaction->ConnectionInfo();
  if (httpTrans && !transCI->UsingProxy()) {
    // this is a httptransaction object, being dispatched into a H2 session
    // ensure it does not violate local network access.
    NetAddr peerAddr;
    if (NS_SUCCEEDED(GetPeerAddr(&peerAddr)) &&
        !httpTrans->AllowedToConnectToIpAddressSpace(
            peerAddr.GetIpAddressSpace())) {
      mSocketOutCondition = NS_ERROR_LOCAL_NETWORK_ACCESS_DENIED;
      CloseTransaction(httpTransaction, mSocketOutCondition);
      httpTransaction->Close(mSocketOutCondition);
      return mSocketOutCondition;
    }
  }

  bool needTunnel = transCI->UsingHttpsProxy();
  needTunnel = needTunnel && !mHasTLSTransportLayer;
  needTunnel = needTunnel && transCI->UsingConnect();
  needTunnel = needTunnel && httpTransaction->QueryHttpTransaction();

  // Let the transaction know that the tunnel is already established and we
  // don't need to setup the tunnel again. Note that we only have a response
  // head if the CONNECT request was sent on this connection - for example a
  // HTTP/2 proxy connection tunnels each transaction in its own stream, so
  // there is nothing to report here.
  if (transCI->UsingConnect() && mProxyConnectResponseHead) {
    httpTransaction->OnProxyConnectComplete(mProxyConnectResponseHead);
  }

  LOG(("nsHttpConnection::AddTransaction [this=%p] for %s%s", this,
       mSpdySession ? "SPDY" : "QUIC", needTunnel ? " over tunnel" : ""));

  if (mSpdySession) {
    nsCOMPtr<nsIInterfaceRequestor> callbacks = GetCallbacks();
    RefPtr<ASpdySession> spdySession = mSpdySession;
    if (!spdySession->AddStream(httpTransaction, priority, callbacks)) {
      MOZ_ASSERT(false);  // this cannot happen!
      httpTransaction->Close(NS_ERROR_ABORT);
      return NS_ERROR_FAILURE;
    }
  }

  (void)ResumeSend();
  return NS_OK;
}

void nsHttpConnection::SwapTransaction(nsAHttpTransaction* aOld,
                                       nsAHttpTransaction* aNew) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(aOld && aNew);
  LOG(
      ("nsHttpConnection::SwapTransaction [this=%p] aOld=%p -> aNew=%p "
       "mTransaction=%p",
       this, aOld, aNew, mTransaction.get()));
  if (mTransaction != aOld) {
    return;
  }
  mTransaction = aNew;
}

nsresult nsHttpConnection::CreateTunnelStream(
    nsAHttpTransaction* httpTransaction, HttpConnectionBase** aHttpConnection,
    bool aIsExtendedCONNECT) {
  if (!mSpdySession) {
    return NS_ERROR_UNEXPECTED;
  }

  nsCOMPtr<nsIInterfaceRequestor> callbacks = GetCallbacks();
  auto result = mSpdySession->CreateTunnelStream(httpTransaction, callbacks,
                                                 mRtt, aIsExtendedCONNECT);
  if (result.isErr()) {
    return result.unwrapErr();
  }
  RefPtr<nsHttpConnection> conn = result.unwrap();

  // We need to store the refrence of the Http2Session in the tunneled
  // connection, so when nsHttpConnection::DontReuse is called the Http2Session
  // can't be reused.
  if (aIsExtendedCONNECT) {
    LOG(
        ("nsHttpConnection::CreateTunnelStream %p Set h2 session %p to "
         "tunneled conn %p",
         this, mSpdySession.get(), conn.get()));
    conn->mExtendedCONNECTHttp2Session = mSpdySession;
  }
  conn.forget(aHttpConnection);
  return NS_OK;
}

void nsHttpConnection::Close(nsresult reason, bool aIsShutdown) {
  LOG(("nsHttpConnection::Close [this=%p reason=%" PRIx32
       " mExperienceState=%x]\n",
       this, static_cast<uint32_t>(reason),
       static_cast<uint32_t>(mExperienceState)));

  if (mConnectionState != ConnectionState::CLOSED) {
    RecordConnectionCloseTelemetry(reason);
    ChangeConnectionState(ConnectionState::CLOSED);
  }

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  mTlsHandshaker->NotifyClose();
  mContinueHandshakeDone = nullptr;
  mExtendedCONNECTHttp2Session = nullptr;
  // Ensure TCP keepalive timer is stopped.
  if (mTCPKeepaliveTransitionTimer) {
    mTCPKeepaliveTransitionTimer->Cancel();
    mTCPKeepaliveTransitionTimer = nullptr;
  }
  if (mForceSendTimer) {
    mForceSendTimer->Cancel();
    mForceSendTimer = nullptr;
  }

  if (!mTrafficCategory.IsEmpty()) {
    HttpTrafficAnalyzer* hta = gHttpHandler->GetHttpTrafficAnalyzer();
    if (hta) {
      hta->IncrementHttpConnection(std::move(mTrafficCategory));
      MOZ_ASSERT(mTrafficCategory.IsEmpty());
    }
  }

  nsCOMPtr<nsITLSSocketControl> tlsSocketControl;
  GetTLSSocketControl(getter_AddRefs(tlsSocketControl));
  if (tlsSocketControl) {
    tlsSocketControl->SetHandshakeCallbackListener(nullptr);
  }

  if (NS_FAILED(reason)) {
    if (mIdleMonitoring) EndIdleMonitoring();

    // The connection and security errors clear out alt-svc mappings
    // in case any previously validated ones are now invalid
    if (((reason == NS_ERROR_NET_RESET) ||
         (NS_ERROR_GET_MODULE(reason) == NS_ERROR_MODULE_SECURITY)) &&
        mConnInfo && !(mTransactionCaps & NS_HTTP_ERROR_SOFTLY)) {
      gHttpHandler->ClearHostMapping(mConnInfo);
    }
    if (mTlsHandshaker->EarlyDataWasAvailable() &&
        PossibleZeroRTTRetryError(reason)) {
      gHttpHandler->Exclude0RttTcp(mConnInfo);
    }

    if (mSocketTransport) {
      mSocketTransport->SetEventSink(nullptr, nullptr);

      // If there are bytes sitting in the input queue then read them
      // into a junk buffer to avoid generating a tcp rst by closing a
      // socket with data pending. TLS is a classic case of this where
      // a Alert record might be superfulous to a clean HTTP/SPDY shutdown.
      // Never block to do this and limit it to a small amount of data.
      // During shutdown just be fast!
      if (mSocketIn && !aIsShutdown && !mInSpdyTunnel) {
        char buffer[4000];
        uint32_t count, total = 0;
        nsresult rv;
        do {
          rv = mSocketIn->Read(buffer, 4000, &count);
          if (NS_SUCCEEDED(rv)) total += count;
        } while (NS_SUCCEEDED(rv) && count > 0 && total < 64000);
        LOG(("nsHttpConnection::Close drained %d bytes\n", total));
      }

      mSocketTransport->SetSecurityCallbacks(nullptr);
      mSocketTransport->Close(reason);
      if (mSocketOut) mSocketOut->AsyncWait(nullptr, 0, 0, nullptr);
    }
    mKeepAlive = false;
  }

  if (mConnInfo->GetIsTrrServiceChannel() && !mLastTRRResponseTime.IsNull() &&
      NS_SUCCEEDED(reason) && !aIsShutdown) {
    // Record telemetry keyed by TRR provider.
    glean::network::trr_idle_close_time_h1.Get(TRRProviderKey())
        .AccumulateRawDuration(TimeStamp::Now() - mLastTRRResponseTime);
  }
}

void nsHttpConnection::MarkAsDontReuse() {
  LOG(("nsHttpConnection::MarkAsDontReuse %p\n", this));
  mKeepAliveMask = false;
  mKeepAlive = false;
  mDontReuse = true;
  mIdleTimeout = 0;
}

void nsHttpConnection::DontReuse() {
  LOG(("nsHttpConnection::DontReuse %p spdysession=%p\n", this,
       mSpdySession.get()));
  MarkAsDontReuse();
  if (mSpdySession) {
    mSpdySession->DontReuse();
  } else if (mExtendedCONNECTHttp2Session) {
    LOG(("nsHttpConnection::DontReuse %p mExtendedCONNECTHttp2Session=%p\n",
         this, mExtendedCONNECTHttp2Session.get()));
    mExtendedCONNECTHttp2Session->DontReuse();
  }
}

bool nsHttpConnection::TestJoinConnection(const nsACString& hostname,
                                          int32_t port) {
  if (mSpdySession && CanDirectlyActivate()) {
    return mSpdySession->TestJoinConnection(hostname, port);
  }

  return false;
}

bool nsHttpConnection::JoinConnection(const nsACString& hostname,
                                      int32_t port) {
  if (mSpdySession && CanDirectlyActivate()) {
    return mSpdySession->JoinConnection(hostname, port);
  }

  return false;
}

bool nsHttpConnection::CanReuse() {
  if (!CanReuseLikely()) {
    return false;
  }

  if (!IsAlive()) {
    return false;
  }

  // An idle persistent connection should not have data waiting to be read
  // before a request is sent. Data here is likely a 408 timeout response
  // which we would deal with later on through the restart logic, but that
  // path is more expensive than just closing the socket now.

  uint64_t dataSize;
  if (mSocketIn && (mUsingSpdyVersion == SpdyVersion::NONE) &&
      mHttp1xTransactionCount &&
      NS_SUCCEEDED(mSocketIn->Available(&dataSize)) && dataSize) {
    LOG(
        ("nsHttpConnection::CanReuse %p %s"
         "Socket not reusable because read data pending (%" PRIu64 ") on it.\n",
         this, mConnInfo->Origin(), dataSize));
    return false;
  }
  return true;
}

bool nsHttpConnection::CanReuseLikely() {
  if (mDontReuse || !mRemainingConnectionUses) {
    return false;
  }

  if ((mTransaction ? (mTransaction->IsDone() ? 0U : 1U) : 0U) >=
      mRemainingConnectionUses) {
    return false;
  }

  bool canReuse;
  if (mSpdySession) {
    canReuse = mSpdySession->CanReuse();
  } else {
    canReuse = IsKeepAlive();
  }

  return canReuse && (IdleTime() < mIdleTimeout);
}

bool nsHttpConnection::CanDirectlyActivate() {
  // return true if a new transaction can be addded to ths connection at any
  // time through Activate(). In practice this means this is a healthy SPDY
  // connection with room for more concurrent streams.

  return UsingSpdy() && CanReuse() && mSpdySession &&
         mSpdySession->RoomForMoreStreams();
}

const char* nsHttpConnection::CanDirectlyActivateReason() const {
  if (mUsingSpdyVersion == SpdyVersion::NONE) return "not-h2";
  if (mDontReuse) return "dont-reuse";
  if (!mSpdySession) return "no-spdy-session";
  if (!mSpdySession->RoomForMoreStreams()) return "streams-full";
  return "ok";
}

PRIntervalTime nsHttpConnection::IdleTime() {
  return mSpdySession ? mSpdySession->IdleTime()
                      : (PR_IntervalNow() - mLastReadTime);
}

// returns the number of seconds left before the allowable idle period
// expires, or 0 if the period has already expied.
uint32_t nsHttpConnection::TimeToLive() {
  LOG(("nsHttpConnection::TTL: %p %s idle %d timeout %d\n", this,
       mConnInfo->Origin(), IdleTime(), mIdleTimeout));

  if (IdleTime() >= mIdleTimeout) {
    return 0;
  }

  uint32_t timeToLive = PR_IntervalToSeconds(mIdleTimeout - IdleTime());

  // a positive amount of time can be rounded to 0. Because 0 is used
  // as the expiration signal, round all values from 0 to 1 up to 1.
  if (!timeToLive) {
    timeToLive = 1;
  }
  return timeToLive;
}

bool nsHttpConnection::IsAlive() {
  if (!mSocketTransport || !mConnectedTransport) return false;

  // SocketTransport::IsAlive can run the SSL state machine, so make sure
  // the NPN options are set before that happens.
  mTlsHandshaker->SetupSSL(mInSpdyTunnel, mForcePlainText);

  bool alive;
  nsresult rv = mSocketTransport->IsAlive(&alive);
  if (NS_FAILED(rv)) alive = false;

  return alive;
}

void nsHttpConnection::SetUrgentStartPreferred(bool urgent) {
  if (mExperienced && !mUrgentStartPreferredKnown) {
    // Set only according the first ever dispatched non-null transaction
    mUrgentStartPreferredKnown = true;
    mUrgentStartPreferred = urgent;
    LOG(("nsHttpConnection::SetUrgentStartPreferred [this=%p urgent=%d]", this,
         urgent));
  }
}

//----------------------------------------------------------------------------
// nsHttpConnection::nsAHttpConnection compatible methods
//----------------------------------------------------------------------------

nsresult nsHttpConnection::OnHeadersAvailable(nsAHttpTransaction* trans,
                                              nsHttpRequestHead* requestHead,
                                              nsHttpResponseHead* responseHead,
                                              bool* reset) {
  LOG(
      ("nsHttpConnection::OnHeadersAvailable [this=%p trans=%p "
       "response-head=%p]\n",
       this, trans, responseHead));

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  NS_ENSURE_ARG_POINTER(trans);
  MOZ_ASSERT(responseHead, "No response head?");

  if (mInSpdyTunnel) {
    DebugOnly<nsresult> rv =
        responseHead->SetHeader(nsHttp::X_Firefox_Spdy_Proxy, "true"_ns);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
  }

  // we won't change our keep-alive policy unless the server has explicitly
  // told us to do so.

  // inspect the connection headers for keep-alive info provided the
  // transaction completed successfully. In the case of a non-sensical close
  // and keep-alive favor the close out of conservatism.

  bool explicitKeepAlive = false;
  bool explicitClose =
      responseHead->HasHeaderValue(nsHttp::Connection, "close") ||
      responseHead->HasHeaderValue(nsHttp::Proxy_Connection, "close");
  if (!explicitClose) {
    explicitKeepAlive =
        responseHead->HasHeaderValue(nsHttp::Connection, "keep-alive") ||
        responseHead->HasHeaderValue(nsHttp::Proxy_Connection, "keep-alive");
  }

  // deal with 408 Server Timeouts
  uint16_t responseStatus = responseHead->Status();
  if (responseStatus == 408) {
    // timeouts that are not caused by persistent connection reuse should
    // not be retried for browser compatibility reasons. bug 907800. The
    // server driven close is implicit in the 408.
    explicitClose = true;
    explicitKeepAlive = false;
  }

  if ((responseHead->Version() < HttpVersion::v1_1) ||
      (requestHead->Version() < HttpVersion::v1_1)) {
    // HTTP/1.0 connections are by default NOT persistent
    mKeepAlive = explicitKeepAlive;
  } else {
    // HTTP/1.1 connections are by default persistent
    mKeepAlive = !explicitClose;
  }
  mKeepAliveMask = mKeepAlive;

  // if this connection is persistent, then the server may send a "Keep-Alive"
  // header specifying the maximum number of times the connection can be
  // reused as well as the maximum amount of time the connection can be idle
  // before the server will close it.  we ignore the max reuse count, because
  // a "keep-alive" connection is by definition capable of being reused, and
  // we only care about being able to reuse it once.  if a timeout is not
  // specified then we use our advertized timeout value.
  bool foundKeepAliveMax = false;
  if (mKeepAlive) {
    nsAutoCString keepAlive;
    (void)responseHead->GetHeader(nsHttp::Keep_Alive, keepAlive);

    if (mUsingSpdyVersion == SpdyVersion::NONE) {
      const char* cp = nsCRT::strcasestr(keepAlive.get(), "timeout=");
      if (cp) {
        mIdleTimeout = PR_SecondsToInterval((uint32_t)atoi(cp + 8));
      } else {
        mIdleTimeout = gHttpHandler->IdleTimeout() * mDefaultTimeoutFactor;
      }

      cp = nsCRT::strcasestr(keepAlive.get(), "max=");
      if (cp) {
        int maxUses = atoi(cp + 4);
        if (maxUses > 0) {
          foundKeepAliveMax = true;
          mRemainingConnectionUses = static_cast<uint32_t>(maxUses);
        }
      }
    }

    LOG(("Connection can be reused [this=%p idle-timeout=%usec]\n", this,
         PR_IntervalToSeconds(mIdleTimeout)));
  }

  if (!foundKeepAliveMax && mRemainingConnectionUses &&
      (mUsingSpdyVersion == SpdyVersion::NONE)) {
    --mRemainingConnectionUses;
  }

  switch (mState) {
    case HttpConnectionState::SETTING_UP_TUNNEL: {
      HandleTunnelResponse(*responseHead, reset);
      break;
    }
    default:
      if (requestHead->HasHeader(nsHttp::Upgrade)) {
        HandleWebSocketResponse(requestHead, responseHead, responseStatus);
      } else if (responseStatus == 101) {
        // We got an 101 but we are not asking of a WebSsocket?
        Close(NS_ERROR_ABORT);
      }
  }

  mLastHttpResponseVersion = responseHead->Version();

  return NS_OK;
}

void nsHttpConnection::HandleTunnelResponse(
    const nsHttpResponseHead& responseHead, bool* reset) {
  LOG(("nsHttpConnection::HandleTunnelResponse()"));
  MOZ_ASSERT(TunnelSetupInProgress());
  MOZ_ASSERT(mProxyConnectStream);
  MOZ_ASSERT(mUsingSpdyVersion == SpdyVersion::NONE,
             "SPDY NPN Complete while using proxy connect stream");
  // If we're doing a proxy connect, we need to check whether or not
  // it was successful.  If so, we have to reset the transaction and step-up
  // the socket connection if using SSL. Finally, we have to wake up the
  // socket write request.

  mProxyConnectResponseHead =
      MakeRefPtr<ProxyConnectResponseHead>(responseHead);
  if (responseHead.Status() == 200) {
    ChangeState(HttpConnectionState::REQUEST);
  }
  mProxyConnectStream = nullptr;
  bool isHttps = mTransaction ? mTransaction->ConnectionInfo()->EndToEndSSL()
                              : mConnInfo->EndToEndSSL();
  bool onlyConnect = mTransactionCaps & NS_HTTP_CONNECT_ONLY;

  mTransaction->OnProxyConnectComplete(mProxyConnectResponseHead);
  if (responseHead.Status() == 200) {
    LOG(("proxy CONNECT succeeded! endtoendssl=%d onlyconnect=%d\n", isHttps,
         onlyConnect));
    // If we're only connecting, we don't need to reset the transaction
    // state. We need to upgrade the socket now without doing the actual
    // http request.
    if (!onlyConnect) {
      *reset = true;
    }
    nsresult rv;
    if (isHttps) {
      bool skipSSL = false;
      if (mConnInfo->UsingHttpsProxy() ||
          mTransactionCaps & NS_HTTP_TLS_TUNNEL) {
        LOG(("%p SetupSecondaryTLS %s %d\n", this, mConnInfo->Origin(),
             mConnInfo->OriginPort()));
        SetupSecondaryTLS();
      } else if (onlyConnect) {
        MOZ_ASSERT(mConnInfo->UsingOnlyHttpProxy(), "Must be a HTTP proxy");

        // We have CONNECT only flag and a HTTP proxy is used here, so we can
        // just skip setting up SSL. We have to mark this as complete to finish
        // the transaction and be upgraded.
        mTlsHandshaker->SetNPNComplete();
        skipSSL = true;
      }

      if (!skipSSL) {
        rv = mTlsHandshaker->InitSSLParams(false, true);
        LOG(("InitSSLParams [rv=%" PRIx32 "]\n", static_cast<uint32_t>(rv)));
      }
    }
    rv = mSocketOut->AsyncWait(this, 0, 0, nullptr);
    // XXX what if this fails -- need to handle this error
    MOZ_ASSERT(NS_SUCCEEDED(rv), "mSocketOut->AsyncWait failed");
  } else {
    LOG(("proxy CONNECT failed! endtoendssl=%d onlyconnect=%d\n", isHttps,
         onlyConnect));
    mTransaction->SetProxyConnectFailed();
  }
}

void nsHttpConnection::HandleWebSocketResponse(nsHttpRequestHead* requestHead,
                                               nsHttpResponseHead* responseHead,
                                               uint16_t responseStatus) {
  LOG(("nsHttpConnection::HandleWebSocketResponse()"));

  // Don't use persistent connection for Upgrade unless there's an auth failure:
  // some proxies expect to see auth response on persistent connection.
  // Also allow persistent conn for h2, as we don't want to waste connections
  // for multiplexed upgrades.
  if (responseStatus != 401 && responseStatus != 407 && !mSpdySession) {
    LOG(("HTTP Upgrade in play - disable keepalive for http/1.x\n"));
    MarkAsDontReuse();
  }

  // the new Http2StreamWebSocket breaks wpt on
  // h2 basic authentication 401, due to MakeSticky() work around
  // so we DontReuse() in this circumstance
  if (mInSpdyTunnel && (responseStatus == 401 || responseStatus == 407)) {
    MarkAsDontReuse();
    return;
  }

  if (responseStatus == 101) {
    nsAutoCString upgradeReq;
    bool hasUpgradeReq =
        NS_SUCCEEDED(requestHead->GetHeader(nsHttp::Upgrade, upgradeReq));
    nsAutoCString upgradeResp;
    bool hasUpgradeResp =
        NS_SUCCEEDED(responseHead->GetHeader(nsHttp::Upgrade, upgradeResp));
    if (!hasUpgradeReq || !hasUpgradeResp ||
        !nsHttp::FindToken(upgradeResp.get(), upgradeReq.get(),
                           HTTP_HEADER_VALUE_SEPS)) {
      LOG(("HTTP 101 Upgrade header mismatch req = %s, resp = %s\n",
           upgradeReq.get(),
           !upgradeResp.IsEmpty() ? upgradeResp.get()
                                  : "RESPONSE's nsHttp::Upgrade is empty"));
      Close(NS_ERROR_ABORT);
    } else {
      LOG(("HTTP Upgrade Response to %s\n", upgradeResp.get()));
    }
  }
}

bool nsHttpConnection::IsReused() {
  if (mIsReused) return true;
  if (!mConsiderReusedAfterInterval) return false;

  // ReusedAfter allows a socket to be consider reused only after a certain
  // interval of time has passed
  return (PR_IntervalNow() - mConsiderReusedAfterEpoch) >=
         mConsiderReusedAfterInterval;
}

void nsHttpConnection::SetIsReusedAfter(uint32_t afterMilliseconds) {
  mConsiderReusedAfterEpoch = PR_IntervalNow();
  mConsiderReusedAfterInterval = PR_MillisecondsToInterval(afterMilliseconds);
}

nsresult nsHttpConnection::TakeTransport(nsISocketTransport** aTransport,
                                         nsIAsyncInputStream** aInputStream,
                                         nsIAsyncOutputStream** aOutputStream) {
  if (mUsingSpdyVersion != SpdyVersion::NONE) return NS_ERROR_FAILURE;
  if (mTransaction && !mTransaction->IsDone()) return NS_ERROR_IN_PROGRESS;
  if (!(mSocketTransport && mSocketIn && mSocketOut)) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  if (mInputOverflow) mSocketIn = mInputOverflow.forget();

  // Change TCP Keepalive frequency to long-lived if currently short-lived.
  if (mTCPKeepaliveConfig == kTCPKeepaliveShortLivedConfig) {
    if (mTCPKeepaliveTransitionTimer) {
      mTCPKeepaliveTransitionTimer->Cancel();
      mTCPKeepaliveTransitionTimer = nullptr;
    }
    nsresult rv = StartLongLivedTCPKeepalives();
    LOG(
        ("nsHttpConnection::TakeTransport [%p] calling "
         "StartLongLivedTCPKeepalives",
         this));
    if (NS_FAILED(rv)) {
      LOG(
          ("nsHttpConnection::TakeTransport [%p] "
           "StartLongLivedTCPKeepalives failed rv[0x%" PRIx32 "]",
           this, static_cast<uint32_t>(rv)));
    }
  }

  if (mHasTLSTransportLayer) {
    RefPtr<TLSTransportLayer> tlsTransportLayer =
        do_QueryObject(mSocketTransport);
    if (tlsTransportLayer) {
      // This transport layer is no longer owned by this connection.
      tlsTransportLayer->ReleaseOwner();
    }
  }

  mSocketTransport->SetSecurityCallbacks(nullptr);
  mSocketTransport->SetEventSink(nullptr, nullptr);

  mSocketTransport.forget(aTransport);
  mSocketIn.forget(aInputStream);
  mSocketOut.forget(aOutputStream);

  return NS_OK;
}

uint32_t nsHttpConnection::ReadTimeoutTick(PRIntervalTime now) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  // make sure timer didn't tick before Activate()
  if (!mTransaction) return UINT32_MAX;

  // Spdy implements some timeout handling using the SPDY ping frame.
  if (mSpdySession) {
    return mSpdySession->ReadTimeoutTick(now);
  }

  uint32_t nextTickAfter = UINT32_MAX;
  // Timeout if the response is taking too long to arrive.
  if (mResponseTimeoutEnabled) {
    NS_WARNING_ASSERTION(
        gHttpHandler->ResponseTimeoutEnabled(),
        "Timing out a response, but response timeout is disabled!");

    PRIntervalTime initialResponseDelta = now - mLastWriteTime;

    if (initialResponseDelta > mTransaction->ResponseTimeout()) {
      LOG(("canceling transaction: no response for %ums: timeout is %dms\n",
           PR_IntervalToMilliseconds(initialResponseDelta),
           PR_IntervalToMilliseconds(mTransaction->ResponseTimeout())));

      mResponseTimeoutEnabled = false;
      SetCloseReason(ConnectionCloseReason::IDLE_TIMEOUT);
      // This will also close the connection
      CloseTransaction(mTransaction, NS_ERROR_NET_TIMEOUT);
      return UINT32_MAX;
    }
    nextTickAfter = PR_IntervalToSeconds(mTransaction->ResponseTimeout()) -
                    PR_IntervalToSeconds(initialResponseDelta);
    nextTickAfter = std::max(nextTickAfter, 1U);
  }

  if (!mTlsHandshaker->NPNComplete()) {
    // We can reuse mLastWriteTime here, because it is set when the
    // connection is activated and only change when a transaction
    // succesfullu write to the socket and this can only happen after
    // the TLS handshake is done.
    PRIntervalTime initialTLSDelta = now - mLastWriteTime;
    if (initialTLSDelta >
        PR_MillisecondsToInterval(gHttpHandler->TLSHandshakeTimeout())) {
      LOG(
          ("canceling transaction: tls handshake takes too long: tls handshake "
           "last %ums, timeout is %dms.",
           PR_IntervalToMilliseconds(initialTLSDelta),
           gHttpHandler->TLSHandshakeTimeout()));

      // This will also close the connection
      SetCloseReason(ConnectionCloseReason::TLS_TIMEOUT);
      CloseTransaction(mTransaction, NS_ERROR_NET_TIMEOUT);
      return UINT32_MAX;
    }
  }

  return nextTickAfter;
}

void nsHttpConnection::UpdateTCPKeepalive(nsITimer* aTimer, void* aClosure) {
  MOZ_ASSERT(aTimer);
  MOZ_ASSERT(aClosure);

  nsHttpConnection* self = static_cast<nsHttpConnection*>(aClosure);

  if (NS_WARN_IF(self->mUsingSpdyVersion != SpdyVersion::NONE)) {
    return;
  }

  // Do not reduce keepalive probe frequency for idle connections.
  if (self->mIdleMonitoring) {
    return;
  }

  nsresult rv = self->StartLongLivedTCPKeepalives();
  if (NS_FAILED(rv)) {
    LOG(
        ("nsHttpConnection::UpdateTCPKeepalive [%p] "
         "StartLongLivedTCPKeepalives failed rv[0x%" PRIx32 "]",
         self, static_cast<uint32_t>(rv)));
  }
}

void nsHttpConnection::GetTLSSocketControl(
    nsITLSSocketControl** tlsSocketControl) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG(("nsHttpConnection::GetTLSSocketControl trans=%p socket=%p\n",
       mTransaction.get(), mSocketTransport.get()));

  *tlsSocketControl = nullptr;

  if (mTransaction && NS_SUCCEEDED(mTransaction->GetTransactionTLSSocketControl(
                          tlsSocketControl))) {
    return;
  }

  if (mSocketTransport &&
      NS_SUCCEEDED(mSocketTransport->GetTlsSocketControl(tlsSocketControl))) {
    return;
  }
}

nsresult nsHttpConnection::PushBack(const char* data, uint32_t length) {
  LOG(("nsHttpConnection::PushBack [this=%p, length=%d]\n", this, length));

  if (mInputOverflow) {
    NS_ERROR("nsHttpConnection::PushBack only one buffer supported");
    return NS_ERROR_UNEXPECTED;
  }

  mInputOverflow = new nsPreloadedStream(mSocketIn, data, length);
  return NS_OK;
}

class HttpConnectionForceIO : public Runnable, public nsIRunnablePriority {
 public:
  HttpConnectionForceIO(nsHttpConnection* aConn, bool doRecv)
      : Runnable("net::HttpConnectionForceIO"), mConn(aConn), mDoRecv(doRecv) {}

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIRUNNABLEPRIORITY

  NS_IMETHOD Run() override {
    MOZ_ASSERT(OnSocketThread(), "not on socket thread");

    if (mDoRecv) {
      if (!mConn->mSocketIn) return NS_OK;
      return mConn->OnInputStreamReady(mConn->mSocketIn);
    }

    MOZ_ASSERT(mConn->mForceSendPending);
    mConn->mForceSendPending = false;

    if (!mConn->mSocketOut) {
      return NS_OK;
    }
    return mConn->OnOutputStreamReady(mConn->mSocketOut);
  }

 private:
  virtual ~HttpConnectionForceIO() = default;

  RefPtr<nsHttpConnection> mConn;
  bool mDoRecv;
};

NS_IMPL_ISUPPORTS_INHERITED(HttpConnectionForceIO, Runnable,
                            nsIRunnablePriority)

NS_IMETHODIMP
HttpConnectionForceIO::GetPriority(uint32_t* aPriority) {
  if (StaticPrefs::network_trr_high_priority_events() &&
      mConn->ConnectionInfo() &&
      mConn->ConnectionInfo()->GetIsTrrServiceChannel()) {
    *aPriority = nsIRunnablePriority::PRIORITY_MEDIUMHIGH;
  } else {
    *aPriority = nsIRunnablePriority::PRIORITY_NORMAL;
  }
  return NS_OK;
}

nsresult nsHttpConnection::ResumeSend() {
  LOG(("nsHttpConnection::ResumeSend [this=%p]\n", this));

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  if (mSocketOut) {
    return mSocketOut->AsyncWait(this, 0, 0, nullptr);
  }

  MOZ_ASSERT_UNREACHABLE("no socket output stream");
  return NS_ERROR_UNEXPECTED;
}

nsresult nsHttpConnection::ResumeRecv() {
  LOG(("nsHttpConnection::ResumeRecv [this=%p]\n", this));

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  // the mLastReadTime timestamp is used for finding slowish readers
  // and can be pretty sensitive. For that reason we actually reset it
  // when we ask to read (resume recv()) so that when we get called back
  // with actual read data in OnSocketReadable() we are only measuring
  // the latency between those two acts and not all the processing that
  // may get done before the ResumeRecv() call
  mLastReadTime = PR_IntervalNow();

  if (mSocketIn) {
    if (mHasTLSTransportLayer) {
      RefPtr<TLSTransportLayer> tlsTransportLayer =
          do_QueryObject(mSocketTransport);
      if (tlsTransportLayer) {
        bool hasDataToRecv = tlsTransportLayer->HasDataToRecv();
        if (hasDataToRecv && NS_SUCCEEDED(ForceRecv())) {
          return NS_OK;
        }
        (void)mSocketIn->AsyncWait(this, 0, 0, nullptr);
        // We have to return an error here to let the underlying layer know this
        // connection doesn't read any data.
        return NS_BASE_STREAM_WOULD_BLOCK;
      }
    }
    return mSocketIn->AsyncWait(this, 0, 0, nullptr);
  }

  MOZ_ASSERT_UNREACHABLE("no socket input stream");
  return NS_ERROR_UNEXPECTED;
}

void nsHttpConnection::ForceSendIO(nsITimer* aTimer, void* aClosure) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  nsHttpConnection* self = static_cast<nsHttpConnection*>(aClosure);
  MOZ_ASSERT(aTimer == self->mForceSendTimer);
  self->mForceSendTimer = nullptr;
  DispatchToCurrent(new HttpConnectionForceIO(self, false));
}

nsresult nsHttpConnection::MaybeForceSendIO() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  // due to bug 1213084 sometimes real I/O events do not get serviced when
  // NSPR derived I/O events are ready and this can cause a deadlock with
  // https over https proxying. Normally we would expect the write callback to
  // be invoked before this timer goes off, but set it at the old windows
  // tick interval (kForceDelay) as a backup for those circumstances.
  static const uint32_t kForceDelay = 17;  // ms

  if (mForceSendPending) {
    return NS_OK;
  }
  MOZ_ASSERT(!mForceSendTimer);
  mForceSendPending = true;
  return NS_NewTimerWithFuncCallback(
      getter_AddRefs(mForceSendTimer), nsHttpConnection::ForceSendIO, this,
      kForceDelay, nsITimer::TYPE_ONE_SHOT,
      "net::nsHttpConnection::MaybeForceSendIO"_ns);
}

// trigger an asynchronous read
nsresult nsHttpConnection::ForceRecv() {
  LOG(("nsHttpConnection::ForceRecv [this=%p]\n", this));
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  return DispatchToCurrent(new HttpConnectionForceIO(this, true));
}

// trigger an asynchronous write
nsresult nsHttpConnection::ForceSend() {
  LOG(("nsHttpConnection::ForceSend [this=%p]\n", this));
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  return MaybeForceSendIO();
}

void nsHttpConnection::BeginIdleMonitoring() {
  LOG(("nsHttpConnection::BeginIdleMonitoring [this=%p]\n", this));
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(!mTransaction, "BeginIdleMonitoring() while active");
  MOZ_ASSERT(mUsingSpdyVersion == SpdyVersion::NONE,
             "Idle monitoring of spdy not allowed");

  LOG(("Entering Idle Monitoring Mode [this=%p]", this));
  mIdleMonitoring = true;
  if (mSocketIn) mSocketIn->AsyncWait(this, 0, 0, nullptr);
}

void nsHttpConnection::EndIdleMonitoring() {
  LOG(("nsHttpConnection::EndIdleMonitoring [this=%p]\n", this));
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(!mTransaction, "EndIdleMonitoring() while active");

  if (mIdleMonitoring) {
    LOG(("Leaving Idle Monitoring Mode [this=%p]", this));
    mIdleMonitoring = false;
    if (mSocketIn) mSocketIn->AsyncWait(nullptr, 0, 0, nullptr);
  }
}

HttpVersion nsHttpConnection::Version() {
  if (mUsingSpdyVersion != SpdyVersion::NONE) {
    return HttpVersion::v2_0;
  }
  return mLastHttpResponseVersion;
}

PRIntervalTime nsHttpConnection::LastWriteTime() { return mLastWriteTime; }

//-----------------------------------------------------------------------------
// nsHttpConnection <private>
//-----------------------------------------------------------------------------

void nsHttpConnection::CloseTransaction(nsAHttpTransaction* trans,
                                        nsresult reason, bool aIsShutdown) {
  LOG(("nsHttpConnection::CloseTransaction[this=%p trans=%p reason=%" PRIx32
       "]\n",
       this, trans, static_cast<uint32_t>(reason)));

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  if (mCurrentBytesRead > mMaxBytesRead) mMaxBytesRead = mCurrentBytesRead;

  // mask this error code because its not a real error.
  if (reason == NS_BASE_STREAM_CLOSED) reason = NS_OK;

  if (mUsingSpdyVersion != SpdyVersion::NONE) {
    DontReuse();
    // if !mSpdySession then mUsingSpdyVersion must be false for canreuse()
    mSpdySession->SetCleanShutdown(aIsShutdown);
    mUsingSpdyVersion = SpdyVersion::NONE;
    mSpdySession = nullptr;
  }

  if ((NS_SUCCEEDED(reason) || NS_BASE_STREAM_CLOSED == reason) &&
      trans->ConnectionInfo() &&
      trans->ConnectionInfo()->GetIsTrrServiceChannel()) {
    // save time of last successful response
    mLastTRRResponseTime = TimeStamp::Now();
  }

  if (mTransaction) {
    LOG(("  closing associated mTransaction"));
    if (NS_SUCCEEDED(reason)) {
      mHttp1xTransactionCount += mTransaction->Http1xTransactionCount();
    }

    mTransaction->Close(reason);
    mTransaction = nullptr;
  }

  {
    MutexAutoLock lock(mCallbacksLock);
    mCallbacks = nullptr;
  }

  if (NS_FAILED(reason) && (reason != NS_BINDING_RETARGETED)) {
    Close(reason, aIsShutdown);
  }

  // flag the connection as reused here for convenience sake.  certainly
  // it might be going away instead ;-)
  mIsReused = true;
}

bool nsHttpConnection::CheckCanWrite0RTTData() {
  MOZ_ASSERT(mTlsHandshaker->EarlyDataAvailable());
  nsCOMPtr<nsITLSSocketControl> tlsSocketControl;
  GetTLSSocketControl(getter_AddRefs(tlsSocketControl));
  if (!tlsSocketControl) {
    return false;
  }
  nsCOMPtr<nsITransportSecurityInfo> securityInfo;
  if (NS_FAILED(
          tlsSocketControl->GetSecurityInfo(getter_AddRefs(securityInfo)))) {
    return false;
  }
  if (!securityInfo) {
    return false;
  }
  nsAutoCString negotiatedNPN;
  // If the following code fails means that the handshake is not done
  // yet, so continue writing 0RTT data.
  nsresult rv = securityInfo->GetNegotiatedNPN(negotiatedNPN);
  if (NS_FAILED(rv)) {
    return true;
  }
  bool earlyDataAccepted = false;
  rv = tlsSocketControl->GetEarlyDataAccepted(&earlyDataAccepted);
  // If 0RTT data is accepted we can continue writing data,
  // if it is reject stop writing more data.
  return NS_SUCCEEDED(rv) && earlyDataAccepted;
}

nsresult nsHttpConnection::OnReadSegment(const char* buf, uint32_t count,
                                         uint32_t* countRead) {
  LOG(("nsHttpConnection::OnReadSegment [this=%p]\n", this));
  if (count == 0) {
    // some ReadSegments implementations will erroneously call the writer
    // to consume 0 bytes worth of data.  we must protect against this case
    // or else we'd end up closing the socket prematurely.
    NS_ERROR("bad ReadSegments implementation");
    return NS_ERROR_FAILURE;  // stop iterating
  }

  // If we are waiting for 0RTT Response, check maybe nss has finished
  // handshake already.
  // IsAlive() calls drive the handshake and that may cause nss and necko
  // to be out of sync.
  if (mTlsHandshaker->EarlyDataAvailable() && !CheckCanWrite0RTTData()) {
    MOZ_DIAGNOSTIC_ASSERT(mTlsHandshaker->TlsHandshakeComplitionPending());
    LOG(
        ("nsHttpConnection::OnReadSegment Do not write any data, wait"
         " for EnsureNPNComplete to be called [this=%p]",
         this));
    *countRead = 0;
    return NS_BASE_STREAM_WOULD_BLOCK;
  }

  nsresult rv = mSocketOut->Write(buf, count, countRead);
  if (NS_FAILED(rv)) {
    mSocketOutCondition = rv;
  } else if (*countRead == 0) {
    mSocketOutCondition = NS_BASE_STREAM_CLOSED;
  } else {
    mLastWriteTime = PR_IntervalNow();
    mSocketOutCondition = NS_OK;  // reset condition
    if (!TunnelSetupInProgress()) {
      mTotalBytesWritten += *countRead;
      mExperienceState |= ConnectionExperienceState::First_Request_Sent;
    }
  }

  return mSocketOutCondition;
}

nsresult nsHttpConnection::OnSocketWritable() {
  LOG(("nsHttpConnection::OnSocketWritable [this=%p] host=%s\n", this,
       mConnInfo->Origin()));

  nsresult rv;
  uint32_t transactionBytes;
  bool again = true;

  // Prevent STS thread from being blocked by single OnOutputStreamReady
  // callback.
  const uint32_t maxWriteAttempts = 128;
  uint32_t writeAttempts = 0;

  if (mTransactionCaps & NS_HTTP_CONNECT_ONLY) {
    if (!mConnInfo->UsingConnect()) {
      // A CONNECT has been requested for this connection but will never
      // be performed. This should never happen.
      MOZ_ASSERT(false, "proxy connect will never happen");
      LOG(("return failure because proxy connect will never happen\n"));
      return NS_ERROR_FAILURE;
    }

    if (mState == HttpConnectionState::REQUEST &&
        mTlsHandshaker->EnsureNPNComplete()) {
      // Don't need to check this each write attempt since it is only
      // updated after OnSocketWritable completes.
      // We've already done primary tls (if needed) and sent our CONNECT.
      // If we're doing a CONNECT only request there's no need to write
      // the http transaction.
      LOG(("return NS_BASE_STREAM_CLOSED to make transaction closed\n"));
      return NS_BASE_STREAM_CLOSED;
    }
  }

  do {
    ++writeAttempts;
    rv = mSocketOutCondition = NS_OK;
    transactionBytes = 0;

    switch (mState) {
      case HttpConnectionState::SETTING_UP_TUNNEL:
        if (mConnInfo->UsingHttpsProxy() &&
            !mTlsHandshaker->EnsureNPNComplete()) {
          MOZ_DIAGNOSTIC_ASSERT(!mTlsHandshaker->EarlyDataAvailable());
          mSocketOutCondition = NS_BASE_STREAM_WOULD_BLOCK;
        } else {
          rv = SendConnectRequest(this, &transactionBytes);
        }
        break;
      default: {
        // The SSL handshake must be completed before the
        // transaction->readsegments() processing can proceed because we need to
        // know how to format the request differently for http/1, http/2, spdy,
        // etc.. and that is negotiated with NPN/ALPN in the SSL handshake.
        if (!mTlsHandshaker->EnsureNPNComplete() &&
            (!mTlsHandshaker->EarlyDataUsed() ||
             mTlsHandshaker->TlsHandshakeComplitionPending())) {
          // The handshake is not done and we cannot write 0RTT data or nss has
          // already finished 0RTT data.
          mSocketOutCondition = NS_BASE_STREAM_WOULD_BLOCK;
        } else if (!mTransaction) {
          rv = NS_ERROR_FAILURE;
          LOG(("  No Transaction In OnSocketWritable\n"));
        } else if (NS_SUCCEEDED(rv)) {
          // for non spdy sessions let the connection manager know
          if (!mReportedSpdy && mTlsHandshaker->NPNComplete()) {
            mReportedSpdy = true;
            MOZ_ASSERT(!mEverUsedSpdy);
            gHttpHandler->ConnMgr()->ReportSpdyConnection(this, false, false);
          }

          LOG(("  writing transaction request stream\n"));
          RefPtr<nsAHttpTransaction> transaction = mTransaction;
          rv = transaction->ReadSegmentsAgain(this,
                                              nsIOService::gDefaultSegmentSize,
                                              &transactionBytes, &again);
          if (mTlsHandshaker->EarlyDataUsed()) {
            mContentBytesWritten0RTT += transactionBytes;
            if (NS_FAILED(rv) && rv != NS_BASE_STREAM_WOULD_BLOCK) {
              // If an error happens while writting 0RTT data, restart
              // the transactiions without 0RTT.
              mTlsHandshaker->FinishNPNSetup(false, true);
            }
          } else {
            mContentBytesWritten += transactionBytes;
          }
        }
      }
    }

    LOG(
        ("nsHttpConnection::OnSocketWritable %p "
         "ReadSegments returned [rv=%" PRIx32 " read=%u "
         "sock-cond=%" PRIx32 " again=%d]\n",
         this, static_cast<uint32_t>(rv), transactionBytes,
         static_cast<uint32_t>(mSocketOutCondition), again));

    // XXX some streams return NS_BASE_STREAM_CLOSED to indicate EOF.
    if (rv == NS_BASE_STREAM_CLOSED &&
        (mTransaction && !mTransaction->IsDone())) {
      rv = NS_OK;
      transactionBytes = 0;
    }

    if (NS_FAILED(rv)) {
      // if the transaction didn't want to write any more data, then
      // wait for the transaction to call ResumeSend.
      if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
        rv = NS_OK;
      }
      again = false;
    } else if (NS_FAILED(mSocketOutCondition)) {
      if (mSocketOutCondition == NS_BASE_STREAM_WOULD_BLOCK) {
        if (!mTlsHandshaker->EarlyDataCanNotBeUsed()) {
          // continue writing
          // We are not going to poll for write if the handshake is in progress,
          // but early data cannot be used.
          rv = mSocketOut->AsyncWait(this, 0, 0, nullptr);
        }
      } else {
        rv = mSocketOutCondition;
      }
      again = false;
    } else if (!transactionBytes) {
      rv = NS_OK;

      if (mTransaction) {  // in case the ReadSegments stack called
                           // CloseTransaction()
        //
        // at this point we've written out the entire transaction, and now we
        // must wait for the server's response.  we manufacture a status message
        // here to reflect the fact that we are waiting.  this message will be
        // trumped (overwritten) if the server responds quickly.
        //
        mTransaction->OnTransportStatus(mSocketTransport,
                                        NS_NET_STATUS_WAITING_FOR, 0);

        rv = ResumeRecv();  // start reading
      }

      again = false;
    } else if (writeAttempts >= maxWriteAttempts) {
      LOG(("  yield for other transactions\n"));
      rv = mSocketOut->AsyncWait(this, 0, 0, nullptr);  // continue writing
      again = false;
    }
    // write more to the socket until error or end-of-request...
  } while (again && gHttpHandler->Active());

  return rv;
}

nsresult nsHttpConnection::OnWriteSegment(char* buf, uint32_t count,
                                          uint32_t* countWritten) {
  if (count == 0) {
    // some WriteSegments implementations will erroneously call the reader
    // to provide 0 bytes worth of data.  we must protect against this case
    // or else we'd end up closing the socket prematurely.
    NS_ERROR("bad WriteSegments implementation");
    return NS_ERROR_FAILURE;  // stop iterating
  }

  if (ChaosMode::isActive(ChaosFeature::IOAmounts) &&
      ChaosMode::randomUint32LessThan(2)) {
    // read 1...count bytes
    count = ChaosMode::randomUint32LessThan(count) + 1;
  }

  nsresult rv = mSocketIn->Read(buf, count, countWritten);
  if (NS_FAILED(rv)) {
    mSocketInCondition = rv;
  } else if (*countWritten == 0) {
    mSocketInCondition = NS_BASE_STREAM_CLOSED;
  } else {
    mSocketInCondition = NS_OK;  // reset condition
    mExperienceState |= ConnectionExperienceState::First_Response_Received;
  }

  return mSocketInCondition;
}

nsresult nsHttpConnection::OnSocketReadable() {
  LOG(("nsHttpConnection::OnSocketReadable [this=%p]\n", this));

  PRIntervalTime now = PR_IntervalNow();
  PRIntervalTime delta = now - mLastReadTime;

  // Reset mResponseTimeoutEnabled to stop response timeout checks.
  mResponseTimeoutEnabled = false;

  if ((mTransactionCaps & NS_HTTP_CONNECT_ONLY) && !mConnInfo->UsingConnect()) {
    // A CONNECT has been requested for this connection but will never
    // be performed. This should never happen.
    MOZ_ASSERT(false, "proxy connect will never happen");
    LOG(("return failure because proxy connect will never happen\n"));
    return NS_ERROR_FAILURE;
  }

  if (mKeepAliveMask && (delta >= mMaxHangTime)) {
    LOG(("max hang time exceeded!\n"));
    // give the handler a chance to create a new persistent connection to
    // this host if we've been busy for too long.
    mKeepAliveMask = false;
    (void)gHttpHandler->ProcessPendingQ(mConnInfo);
  }

  // Reduce the estimate of the time since last read by up to 1 RTT to
  // accommodate exhausted sender TCP congestion windows or minor I/O delays.
  mLastReadTime = now;

  nsresult rv = NS_OK;
  uint32_t n;
  bool again = true;

  do {
    if (!TunnelSetupInProgress() && !mTlsHandshaker->EnsureNPNComplete()) {
      // Unless we are setting up a tunnel via CONNECT, prevent reading
      // from the socket until the results of NPN
      // negotiation are known (which is determined from the write path).
      // If the server speaks SPDY it is likely the readable data here is
      // a spdy settings frame and without NPN it would be misinterpreted
      // as HTTP/*

      LOG(
          ("nsHttpConnection::OnSocketReadable %p return due to inactive "
           "tunnel setup but incomplete NPN state\n",
           this));
      if (mTlsHandshaker->EarlyDataAvailable() || mHasTLSTransportLayer) {
        rv = ResumeRecv();
      }
      break;
    }

    mSocketInCondition = NS_OK;
    if (!mTransaction) {
      rv = NS_ERROR_FAILURE;
      LOG(("  No Transaction In OnSocketWritable\n"));
    } else {
      RefPtr<nsAHttpTransaction> transaction = mTransaction;
      rv = transaction->WriteSegmentsAgain(
          this, nsIOService::gDefaultSegmentSize, &n, &again);
    }
    LOG(("nsHttpConnection::OnSocketReadable %p trans->ws rv=%" PRIx32
         " n=%d socketin=%" PRIx32 "\n",
         this, static_cast<uint32_t>(rv), n,
         static_cast<uint32_t>(mSocketInCondition)));
    if (NS_FAILED(rv)) {
      // if the transaction didn't want to take any more data, then
      // wait for the transaction to call ResumeRecv.
      if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
        rv = NS_OK;
      }
      again = false;
    } else {
      mCurrentBytesRead += n;
      mTotalBytesRead += n;
      if (NS_FAILED(mSocketInCondition)) {
        // continue waiting for the socket if necessary...
        if (mSocketInCondition == NS_BASE_STREAM_WOULD_BLOCK) {
          rv = ResumeRecv();
        } else {
          rv = mSocketInCondition;
        }
        again = false;
      }
    }
    // read more from the socket until error...
  } while (again && gHttpHandler->Active());

  return rv;
}

void nsHttpConnection::SetupSecondaryTLS() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(!mHasTLSTransportLayer);
  LOG(("nsHttpConnection %p SetupSecondaryTLS %s %d\n", this,
       mConnInfo->Origin(), mConnInfo->OriginPort()));

  nsHttpConnectionInfo* ci = nullptr;
  if (mTransaction) {
    ci = mTransaction->ConnectionInfo();
  }
  if (!ci) {
    ci = mConnInfo;
  }
  MOZ_ASSERT(ci);

  RefPtr<TLSTransportLayer> transportLayer =
      new TLSTransportLayer(mSocketTransport, mSocketIn, mSocketOut, this);
  if (transportLayer->Init(ci->Origin(), ci->OriginPort())) {
    mSocketIn = transportLayer->GetInputStreamWrapper();
    mSocketOut = transportLayer->GetOutputStreamWrapper();
    mSocketTransport = transportLayer;
    mHasTLSTransportLayer = true;
    LOG(("Create mTLSTransportLayer %p", this));
  }
}

void nsHttpConnection::SetInTunnel() {
  mInSpdyTunnel = true;
  mForcePlainText = true;
}

// static
nsresult nsHttpConnection::MakeConnectString(nsAHttpTransaction* trans,
                                             nsHttpRequestHead* request,
                                             nsACString& result,
                                             bool aShouldResistFingerprinting) {
  result.Truncate();
  if (!trans->ConnectionInfo()) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  DebugOnly<nsresult> rv{};

  rv = nsHttpHandler::GenerateHostPort(
      nsDependentCString(trans->ConnectionInfo()->Origin()),
      trans->ConnectionInfo()->OriginPort(), result);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  // CONNECT host:port HTTP/1.1
  request->SetMethod("CONNECT"_ns);
  request->SetVersion(gHttpHandler->HttpVersion());
  request->SetRequestURI(result);
  rv = request->SetHeader(nsHttp::User_Agent,
                          gHttpHandler->UserAgent(aShouldResistFingerprinting));
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  // a CONNECT is always persistent
  rv = request->SetHeader(nsHttp::Proxy_Connection, "keep-alive"_ns);
  MOZ_ASSERT(NS_SUCCEEDED(rv));
  rv = request->SetHeader(nsHttp::Connection, "keep-alive"_ns);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  // all HTTP/1.1 requests must include a Host header (even though it
  // may seem redundant in this case; see bug 82388).
  rv = request->SetHeader(nsHttp::Host, result);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  nsAutoCString val;
  if (NS_SUCCEEDED(
          trans->RequestHead()->GetHeader(nsHttp::Proxy_Authorization, val))) {
    // we don't know for sure if this authorization is intended for the
    // SSL proxy, so we add it just in case.
    rv = request->SetHeader(nsHttp::Proxy_Authorization, val);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
  }
  if ((trans->Caps() & NS_HTTP_CONNECT_ONLY) &&
      NS_SUCCEEDED(trans->RequestHead()->GetHeader(nsHttp::Upgrade, val))) {
    // rfc7639 proposes using the ALPN header to indicate the protocol used
    // in CONNECT when not used for TLS. The protocol is stored in Upgrade.
    // We have to copy this header here since a new HEAD request is created
    // for the CONNECT.
    rv = request->SetHeader("ALPN"_ns, val);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
  }

  result.Truncate();
  request->Flatten(result, false);

  if (LOG1_ENABLED()) {
    LOG(("nsHttpConnection::MakeConnectString for transaction=%p[",
         trans->QueryHttpTransaction()));
    LogHeaders(PromiseFlatCString(result).get());
    LOG(("]"));
  }

  result.AppendLiteral("\r\n");
  return NS_OK;
}

nsresult nsHttpConnection::StartShortLivedTCPKeepalives() {
  if (mUsingSpdyVersion != SpdyVersion::NONE) {
    return NS_OK;
  }
  MOZ_ASSERT(mSocketTransport);
  if (!mSocketTransport) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsresult rv = NS_OK;
  int32_t idleTimeS = -1;
  int32_t retryIntervalS = -1;
  if (gHttpHandler->TCPKeepaliveEnabledForShortLivedConns()) {
    // Set the idle time.
    idleTimeS = gHttpHandler->GetTCPKeepaliveShortLivedIdleTime();
    LOG(
        ("nsHttpConnection::StartShortLivedTCPKeepalives[%p] "
         "idle time[%ds].",
         this, idleTimeS));

    retryIntervalS = std::max<int32_t>((int32_t)PR_IntervalToSeconds(mRtt), 1);
    rv = mSocketTransport->SetKeepaliveVals(idleTimeS, retryIntervalS);
    if (NS_FAILED(rv)) {
      return rv;
    }
    rv = mSocketTransport->SetKeepaliveEnabled(true);
    mTCPKeepaliveConfig = kTCPKeepaliveShortLivedConfig;
  } else {
    rv = mSocketTransport->SetKeepaliveEnabled(false);
    mTCPKeepaliveConfig = kTCPKeepaliveDisabled;
  }
  if (NS_FAILED(rv)) {
    return rv;
  }

  // Start a timer to move to long-lived keepalive config.
  if (!mTCPKeepaliveTransitionTimer) {
    mTCPKeepaliveTransitionTimer = NS_NewTimer();
  }

  if (mTCPKeepaliveTransitionTimer) {
    int32_t time = gHttpHandler->GetTCPKeepaliveShortLivedTime();

    // Adjust |time| to ensure a full set of keepalive probes can be sent
    // at the end of the short-lived phase.
    if (gHttpHandler->TCPKeepaliveEnabledForShortLivedConns()) {
      if (NS_WARN_IF(!gSocketTransportService)) {
        return NS_ERROR_NOT_INITIALIZED;
      }
      int32_t probeCount = -1;
      rv = gSocketTransportService->GetKeepaliveProbeCount(&probeCount);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
      if (NS_WARN_IF(probeCount <= 0)) {
        return NS_ERROR_UNEXPECTED;
      }
      // Add time for final keepalive probes, and 2 seconds for a buffer.
      time += ((probeCount)*retryIntervalS) - (time % idleTimeS) + 2;
    }
    mTCPKeepaliveTransitionTimer->InitWithNamedFuncCallback(
        nsHttpConnection::UpdateTCPKeepalive, this, (uint32_t)time * 1000,
        nsITimer::TYPE_ONE_SHOT,
        "net::nsHttpConnection::StartShortLivedTCPKeepalives"_ns);
  } else {
    NS_WARNING(
        "nsHttpConnection::StartShortLivedTCPKeepalives failed to "
        "create timer.");
  }

  return NS_OK;
}

nsresult nsHttpConnection::StartLongLivedTCPKeepalives() {
  MOZ_ASSERT(mUsingSpdyVersion == SpdyVersion::NONE,
             "Don't use TCP Keepalive with SPDY!");
  if (NS_WARN_IF(mUsingSpdyVersion != SpdyVersion::NONE)) {
    return NS_OK;
  }
  MOZ_ASSERT(mSocketTransport);
  if (!mSocketTransport) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  nsresult rv = NS_OK;
  if (gHttpHandler->TCPKeepaliveEnabledForLongLivedConns()) {
    // Increase the idle time.
    int32_t idleTimeS = gHttpHandler->GetTCPKeepaliveLongLivedIdleTime();
    LOG(("nsHttpConnection::StartLongLivedTCPKeepalives[%p] idle time[%ds]",
         this, idleTimeS));

    int32_t retryIntervalS =
        std::max<int32_t>((int32_t)PR_IntervalToSeconds(mRtt), 1);
    rv = mSocketTransport->SetKeepaliveVals(idleTimeS, retryIntervalS);
    if (NS_FAILED(rv)) {
      return rv;
    }

    // Ensure keepalive is enabled, if current status is disabled.
    if (mTCPKeepaliveConfig == kTCPKeepaliveDisabled) {
      rv = mSocketTransport->SetKeepaliveEnabled(true);
      if (NS_FAILED(rv)) {
        return rv;
      }
    }
    mTCPKeepaliveConfig = kTCPKeepaliveLongLivedConfig;
  } else {
    rv = mSocketTransport->SetKeepaliveEnabled(false);
    mTCPKeepaliveConfig = kTCPKeepaliveDisabled;
  }

  if (NS_FAILED(rv)) {
    return rv;
  }
  return NS_OK;
}

nsresult nsHttpConnection::DisableTCPKeepalives() {
  MOZ_ASSERT(mSocketTransport);
  if (!mSocketTransport) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  LOG(("nsHttpConnection::DisableTCPKeepalives [%p]", this));
  if (mTCPKeepaliveConfig != kTCPKeepaliveDisabled) {
    nsresult rv = mSocketTransport->SetKeepaliveEnabled(false);
    if (NS_FAILED(rv)) {
      return rv;
    }
    mTCPKeepaliveConfig = kTCPKeepaliveDisabled;
  }
  if (mTCPKeepaliveTransitionTimer) {
    mTCPKeepaliveTransitionTimer->Cancel();
    mTCPKeepaliveTransitionTimer = nullptr;
  }
  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpConnection::nsISupports
//-----------------------------------------------------------------------------

NS_IMPL_ADDREF(nsHttpConnection)
NS_IMPL_RELEASE(nsHttpConnection)

NS_INTERFACE_MAP_BEGIN(nsHttpConnection)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
  NS_INTERFACE_MAP_ENTRY(nsIInputStreamCallback)
  NS_INTERFACE_MAP_ENTRY(nsIOutputStreamCallback)
  NS_INTERFACE_MAP_ENTRY(nsITransportEventSink)
  NS_INTERFACE_MAP_ENTRY(nsIInterfaceRequestor)
  NS_INTERFACE_MAP_ENTRY(HttpConnectionBase)
  NS_INTERFACE_MAP_ENTRY_CONCRETE(nsHttpConnection)
NS_INTERFACE_MAP_END

//-----------------------------------------------------------------------------
// nsHttpConnection::nsIInputStreamCallback
//-----------------------------------------------------------------------------

// called on the socket transport thread
NS_IMETHODIMP
nsHttpConnection::OnInputStreamReady(nsIAsyncInputStream* in) {
  MOZ_ASSERT(in == mSocketIn, "unexpected stream");
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  if (mIdleMonitoring) {
    MOZ_ASSERT(!mTransaction, "Idle Input Event While Active");

    // The only read event that is protocol compliant for an idle connection
    // is an EOF, which we check for with CanReuse(). If the data is
    // something else then just ignore it and suspend checking for EOF -
    // our normal timers or protocol stack are the place to deal with
    // any exception logic.

    if (!CanReuse()) {
      LOG(("Server initiated close of idle conn %p\n", this));
      (void)gHttpHandler->ConnMgr()->CloseIdleConnection(this);
      return NS_OK;
    }

    LOG(("Input data on idle conn %p, but not closing yet\n", this));
    return NS_OK;
  }

  // if the transaction was dropped...
  if (!mTransaction) {
    LOG(("  no transaction; ignoring event\n"));
    return NS_OK;
  }

  nsresult rv = OnSocketReadable();
  if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
    return rv;
  }

  if (NS_FAILED(rv)) {
    CloseTransaction(mTransaction, rv);
  }

  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpConnection::nsIOutputStreamCallback
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpConnection::OnOutputStreamReady(nsIAsyncOutputStream* out) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(out == mSocketOut, "unexpected socket");
  // if the transaction was dropped...
  if (!mTransaction) {
    LOG(("  no transaction; ignoring event\n"));
    return NS_OK;
  }

  nsresult rv = OnSocketWritable();
  if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
    return NS_OK;
  }

  if (NS_FAILED(rv)) CloseTransaction(mTransaction, rv);

  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpConnection::nsITransportEventSink
//-----------------------------------------------------------------------------

NS_IMETHODIMP
nsHttpConnection::OnTransportStatus(nsITransport* trans, nsresult status,
                                    int64_t progress, int64_t progressMax) {
  if (mTransaction) mTransaction->OnTransportStatus(trans, status, progress);
  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsHttpConnection::nsIInterfaceRequestor
//-----------------------------------------------------------------------------

// not called on the socket transport thread
NS_IMETHODIMP
nsHttpConnection::GetInterface(const nsIID& iid, void** result) {
  // NOTE: This function is only called on the UI thread via sync proxy from
  //       the socket transport thread.  If that weren't the case, then we'd
  //       have to worry about the possibility of mTransaction going away
  //       part-way through this function call.  See CloseTransaction.

  // NOTE - there is a bug here, the call to getinterface is proxied off the
  // nss thread, not the ui thread as the above comment says. So there is
  // indeed a chance of mTransaction going away. bug 615342

  MOZ_ASSERT(!OnSocketThread(), "on socket thread");

  nsCOMPtr<nsIInterfaceRequestor> callbacks = GetCallbacks();
  if (callbacks) return callbacks->GetInterface(iid, result);
  return NS_ERROR_NO_INTERFACE;
}

void nsHttpConnection::CheckForTraffic(bool check) {
  if (check) {
    LOG((" CheckForTraffic conn %p\n", this));
    if (mSpdySession) {
      if (PR_IntervalToMilliseconds(IdleTime()) >= 500) {
        // Send a ping to verify it is still alive if it has been idle
        // more than half a second, the network changed events are
        // rate-limited to one per 1000 ms.
        LOG((" SendPing\n"));
        mSpdySession->SendPing();
      } else {
        LOG((" SendPing skipped due to network activity\n"));
      }
    } else {
      // If not SPDY, Store snapshot amount of data right now
      mTrafficCount = mTotalBytesWritten + mTotalBytesRead;
      mTrafficStamp = true;
    }
  } else {
    // mark it as not checked
    mTrafficStamp = false;
  }
}

void nsHttpConnection::SetEvent(nsresult aStatus) {
  LOG(("nsHttpConnection::SetEvent [this=%p status=%" PRIx32 "]\n", this,
       static_cast<uint32_t>(aStatus)));
  switch (aStatus) {
    case NS_NET_STATUS_RESOLVING_HOST:
      mBootstrappedTimings.domainLookupStart = TimeStamp::Now();
      break;
    case NS_NET_STATUS_RESOLVED_HOST:
      mBootstrappedTimings.domainLookupEnd = TimeStamp::Now();
      break;
    case NS_NET_STATUS_CONNECTING_TO:
      mBootstrappedTimings.connectStart = TimeStamp::Now();
      break;
    case NS_NET_STATUS_CONNECTED_TO: {
      TimeStamp tnow = TimeStamp::Now();
      mBootstrappedTimings.tcpConnectEnd = tnow;
      mBootstrappedTimings.connectEnd = tnow;
      mBootstrappedTimings.secureConnectionStart = tnow;
      break;
    }
    case NS_NET_STATUS_TLS_HANDSHAKE_STARTING:
      mBootstrappedTimings.secureConnectionStart = TimeStamp::Now();
      break;
    case NS_NET_STATUS_TLS_HANDSHAKE_ENDED:
      mBootstrappedTimings.connectEnd = TimeStamp::Now();
      break;
    default:
      break;
  }
}

bool nsHttpConnection::NoClientCertAuth() const {
  if (!mSocketTransport) {
    return false;
  }

  nsCOMPtr<nsITLSSocketControl> tlsSocketControl;
  mSocketTransport->GetTlsSocketControl(getter_AddRefs(tlsSocketControl));
  if (!tlsSocketControl) {
    return false;
  }

  return !tlsSocketControl->GetClientCertSent();
}

ExtendedCONNECTSupport nsHttpConnection::GetExtendedCONNECTSupport() {
  LOG3(("nsHttpConnection::GetExtendedCONNECTSupport"));
  if (!UsingSpdy()) {
    return ExtendedCONNECTSupport::SUPPORTED;
  }
  LOG3(("nsHttpConnection::ExtendedCONNECTSupport checking spdy session"));
  if (mSpdySession) {
    return mSpdySession->GetExtendedCONNECTSupport();
  }

  return ExtendedCONNECTSupport::NO_SUPPORT;
}

bool nsHttpConnection::LastTransactionExpectedNoContent() {
  return mLastTransactionExpectedNoContent;
}

void nsHttpConnection::SetLastTransactionExpectedNoContent(bool val) {
  mLastTransactionExpectedNoContent = val;
}

bool nsHttpConnection::IsPersistent() { return IsKeepAlive() && !mDontReuse; }

nsAHttpTransaction* nsHttpConnection::Transaction() { return mTransaction; }

nsresult nsHttpConnection::GetSelfAddr(NetAddr* addr) {
  if (!mSocketTransport) {
    return NS_ERROR_FAILURE;
  }
  return mSocketTransport->GetSelfAddr(addr);
}

nsresult nsHttpConnection::GetPeerAddr(NetAddr* addr) {
  if (!mSocketTransport) {
    return NS_ERROR_FAILURE;
  }
  return mSocketTransport->GetPeerAddr(addr);
}

bool nsHttpConnection::ResolvedByTRR() {
  bool val = false;
  if (mSocketTransport) {
    mSocketTransport->ResolvedByTRR(&val);
  }
  return val;
}

nsIRequest::TRRMode nsHttpConnection::EffectiveTRRMode() {
  nsIRequest::TRRMode mode = nsIRequest::TRR_DEFAULT_MODE;
  if (mSocketTransport) {
    mSocketTransport->GetEffectiveTRRMode(&mode);
  }
  return mode;
}

TRRSkippedReason nsHttpConnection::TRRSkipReason() {
  TRRSkippedReason reason = nsITRRSkipReason::TRR_UNSET;
  if (mSocketTransport) {
    mSocketTransport->GetTrrSkipReason(&reason);
  }
  return reason;
}

bool nsHttpConnection::GetEchConfigUsed() {
  bool val = false;
  if (mSocketTransport) {
    mSocketTransport->GetEchConfigUsed(&val);
  }
  return val;
}

void nsHttpConnection::HandshakeDoneInternal() {
  LOG(("nsHttpConnection::HandshakeDoneInternal [this=%p]\n", this));
  if (mTlsHandshaker->NPNComplete()) {
    return;
  }

  ChangeConnectionState(ConnectionState::TRANSFERING);

  nsCOMPtr<nsITLSSocketControl> tlsSocketControl;
  GetTLSSocketControl(getter_AddRefs(tlsSocketControl));
  if (!tlsSocketControl) {
    mTlsHandshaker->FinishNPNSetup(false, false);
    return;
  }

  nsCOMPtr<nsITransportSecurityInfo> securityInfo;
  if (NS_FAILED(
          tlsSocketControl->GetSecurityInfo(getter_AddRefs(securityInfo)))) {
    mTlsHandshaker->FinishNPNSetup(false, false);
    return;
  }
  if (!securityInfo) {
    mTlsHandshaker->FinishNPNSetup(false, false);
    return;
  }

  nsAutoCString negotiatedNPN;
  DebugOnly<nsresult> rvDebug = securityInfo->GetNegotiatedNPN(negotiatedNPN);
  MOZ_ASSERT(NS_SUCCEEDED(rvDebug));

  nsAutoCString transactionNPN;
  transactionNPN = mConnInfo->GetNPNToken();
  LOG(("negotiatedNPN: %s - transactionNPN: %s", negotiatedNPN.get(),
       transactionNPN.get()));
  if (!transactionNPN.IsEmpty() && negotiatedNPN != transactionNPN) {
    LOG(("Resetting connection due to mismatched NPN token"));
    mozilla::glean::network::alpn_mismatch_count.Get(negotiatedNPN).Add();
    DontReuse();
    if (mTransaction) {
      mTransaction->Close(NS_ERROR_NET_RESET);
    }
    return;
  }

  // Deferred LNA check for HTTPS: when network.lna.defer_https_check is set,
  // we let the TLS handshake proceed for Private targets and only deny here,
  // after the peer has presented a valid, trusted certificate. Bailing out
  // before StartSpdy keeps us from spinning up an Http2Session (and its
  // static HPACK tables) just to tear it down. Local targets are handled
  // pre-TLS; plaintext HTTP is handled in Activate.
  //
  // For the HE path mTransaction is a HappyEyeballsTransaction; its
  // override of AllowedToConnectToIpAddressSpace forwards to the real
  // nsHttpTransaction this race is running for. Closing mTransaction on
  // failure tears down the HE attempt and propagates
  // NS_ERROR_LOCAL_NETWORK_ACCESS_DENIED to the real txn so the channel
  // can run its prompt-and-replay path.
  if (mTransaction && mConnInfo->FirstHopSSL() && !mConnInfo->UsingProxy() &&
      StaticPrefs::network_lna_blocking() &&
      StaticPrefs::network_lna_defer_https_check()) {
    NetAddr peerAddr;
    if (NS_SUCCEEDED(GetPeerAddr(&peerAddr))) {
      auto addrSpace = peerAddr.GetIpAddressSpace();
      if (addrSpace == nsILoadInfo::IPAddressSpace::Private &&
          !mTransaction->AllowedToConnectToIpAddressSpace(addrSpace)) {
        DontReuse();
        mTransaction->Close(NS_ERROR_LOCAL_NETWORK_ACCESS_DENIED);
        // Drop the transaction reference before FinishNPNSetup so the
        // PostProcessNPNSetup path doesn't touch it (e.g. Finish0RTT)
        // after Close.
        mTransaction = nullptr;
        mTlsHandshaker->FinishNPNSetup(true, true);
        return;
      }
    }
  }

  bool earlyDataAccepted = false;
  if (mTlsHandshaker->EarlyDataUsed()) {
    // Check if early data has been accepted.
    nsresult rvEarlyData =
        tlsSocketControl->GetEarlyDataAccepted(&earlyDataAccepted);
    LOG(
        ("nsHttpConnection::HandshakeDone [this=%p] - early data "
         "that was sent during 0RTT %s been accepted [rv=%" PRIx32 "].",
         this, earlyDataAccepted ? "has" : "has not",
         static_cast<uint32_t>(rvEarlyData)));

    if (NS_FAILED(rvEarlyData) ||
        (mTransaction &&
         NS_FAILED(mTransaction->Finish0RTT(
             !earlyDataAccepted,
             negotiatedNPN != mTlsHandshaker->EarlyNegotiatedALPN())))) {
      LOG(
          ("nsHttpConnection::HandshakeDone [this=%p] closing transaction "
           "%p",
           this, mTransaction.get()));
      if (mTransaction) {
        mTransaction->Close(NS_ERROR_NET_RESET);
      }
      mTlsHandshaker->FinishNPNSetup(false, true);
      return;
    }
    if (mDid0RTTSpdy &&
        (negotiatedNPN != mTlsHandshaker->EarlyNegotiatedALPN())) {
      Reset0RttForSpdy();
    }
  }

  if (mTlsHandshaker->EarlyDataAvailable() && !earlyDataAccepted) {
    // When the early-data were used but not accepted, we need to start
    // from the begining here and start writing the request again.
    // The same is true if 0RTT was available but not used.
    if (mSocketIn) {
      mSocketIn->AsyncWait(nullptr, 0, 0, nullptr);
    }
    (void)ResumeSend();
  }

  int16_t tlsVersion;
  tlsSocketControl->GetSSLVersionUsed(&tlsVersion);
  mConnInfo = mConnInfo->Mutate()
                  .SetLessThanTls13(
                      (tlsVersion < nsITLSSocketControl::TLS_VERSION_1_3) &&
                      (tlsVersion != nsITLSSocketControl::SSL_VERSION_UNKNOWN))
                  .Finalize();
#ifndef ANDROID
  mTlsHandshaker->EarlyDataTelemetry(tlsVersion, earlyDataAccepted,
                                     mContentBytesWritten0RTT);
#endif
  mTlsHandshaker->EarlyDataDone();

  if (!earlyDataAccepted) {
    LOG(
        ("nsHttpConnection::HandshakeDone [this=%p] early data not "
         "accepted or early data were not used",
         this));

    const SpdyInformation* info = gHttpHandler->SpdyInfo();
    if (negotiatedNPN.Equals(info->VersionString)) {
      if (mTransaction) {
        StartSpdy(tlsSocketControl, info->Version);
      } else {
        LOG(
            ("nsHttpConnection::HandshakeDone [this=%p] set "
             "mContinueHandshakeDone",
             this));
        RefPtr<nsHttpConnection> self = this;
        mContinueHandshakeDone = [self = RefPtr{this},
                                  tlsSocketControl(tlsSocketControl),
                                  info(info->Version)]() {
          LOG(("nsHttpConnection do mContinueHandshakeDone [this=%p]",
               self.get()));
          self->StartSpdy(tlsSocketControl, info);
          self->mTlsHandshaker->FinishNPNSetup(true, true);
        };
        return;
      }
    }
  } else {
    LOG(("nsHttpConnection::HandshakeDone [this=%p] - %" PRId64 " bytes "
         "has been sent during 0RTT.",
         this, mContentBytesWritten0RTT));
    mContentBytesWritten = mContentBytesWritten0RTT;

    if (mSpdySession) {
      // We had already started 0RTT-spdy, now we need to fully set up
      // spdy, since we know we're sticking with it.
      LOG(
          ("nsHttpConnection::HandshakeDone [this=%p] - finishing "
           "StartSpdy for 0rtt spdy session %p",
           this, mSpdySession.get()));
      StartSpdy(tlsSocketControl, mSpdySession->SpdyVersion());
    }
  }

  mTlsHandshaker->FinishNPNSetup(true, true);
  (void)ResumeSend();
}

void nsHttpConnection::SetTunnelSetupDone() {
  MOZ_ASSERT(mProxyConnectStream);
  MOZ_ASSERT(mState == HttpConnectionState::SETTING_UP_TUNNEL);

  ChangeState(HttpConnectionState::REQUEST);
  mProxyConnectStream = nullptr;
}

nsresult nsHttpConnection::SetupProxyConnectStream() {
  LOG(("nsHttpConnection::SetupStream\n"));
  NS_ENSURE_TRUE(!mProxyConnectStream, NS_ERROR_ALREADY_INITIALIZED);
  MOZ_ASSERT(mState == HttpConnectionState::SETTING_UP_TUNNEL);

  nsAutoCString buf;
  nsHttpRequestHead request;
  nsresult rv = MakeConnectString(mTransaction, &request, buf,
                                  mTransactionCaps & NS_HTTP_USE_RFP);
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = NS_NewCStringInputStream(getter_AddRefs(mProxyConnectStream),
                                std::move(buf));
  return rv;
}

nsresult nsHttpConnection::ReadFromStream(nsIInputStream* input, void* closure,
                                          const char* buf, uint32_t offset,
                                          uint32_t count, uint32_t* countRead) {
  // thunk for nsIInputStream instance
  nsHttpConnection* conn = (nsHttpConnection*)closure;
  return conn->OnReadSegment(buf, count, countRead);
}

nsresult nsHttpConnection::SendConnectRequest(void* closure,
                                              uint32_t* transactionBytes) {
  LOG(("  writing CONNECT request stream\n"));
  return mProxyConnectStream->ReadSegments(ReadFromStream, closure,
                                           nsIOService::gDefaultSegmentSize,
                                           transactionBytes);
}

WebTransportSessionBase* nsHttpConnection::GetWebTransportSession(
    nsAHttpTransaction* aTransaction) {
  LOG(
      ("nsHttpConnection::GetWebTransportSession %p mSpdySession=%p "
       "mExtendedCONNECTHttp2Session=%p",
       this, mSpdySession.get(), mExtendedCONNECTHttp2Session.get()));
  if (!mExtendedCONNECTHttp2Session) {
    return nullptr;
  }

  return mExtendedCONNECTHttp2Session->GetWebTransportSession(aTransaction);
}

}  // namespace mozilla::net
