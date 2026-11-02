/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "ConnectionEstablisher.h"

#include "HappyEyeballsConnectionAttempt.h"
#include "HttpConnectionUDP.h"
#include "HttpLog.h"
#include "mozilla/Components.h"
#include "nsHttpConnectionMgr.h"
#include "nsHttpHandler.h"
#include "nsHttpTransaction.h"
#include "nsIDNSRecord.h"
#include "nsSocketTransportService2.h"

// Log on level :5, instead of default :4.
#undef LOG
#define LOG(args) LOG5(args)
#undef LOG_ENABLED
#define LOG_ENABLED() LOG5_ENABLED()

namespace mozilla::net {

// -------------------- SingleDNSAddrRecord --------------------

void DnsMetadata::Fill(nsIDNSAddrRecord* aRecord) {
  if (!aRecord) {
    return;
  }
  aRecord->IsTRR(&mIsTRR);
  aRecord->GetTrrFetchDuration(&mTrrFetchDuration);
  aRecord->GetTrrFetchDurationNetworkOnly(&mTrrFetchDurationNetworkOnly);
  aRecord->GetEffectiveTRRMode(&mEffectiveTRRMode);
  aRecord->GetTrrSkipReason(&mTrrSkipReason);
}

class SingleDNSAddrRecord final : public nsIDNSAddrRecord {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIDNSRECORD
  NS_DECL_NSIDNSADDRRECORD

  SingleDNSAddrRecord(NetAddr aAddr, const DnsMetadata& aMetadata)
      : mAddress(aAddr),
        mIsTRR(aMetadata.mIsTRR),
        mTrrFetchDuration(aMetadata.mTrrFetchDuration),
        mTrrFetchDurationNetworkOnly(aMetadata.mTrrFetchDurationNetworkOnly),
        mEffectiveTRRMode(aMetadata.mEffectiveTRRMode),
        mTrrSkipReason(aMetadata.mTrrSkipReason) {
    LOG(("SingleDNSAddrRecord ctor:%p mIsTRR=%d mEffectiveTRRMode=%d", this,
         mIsTRR, static_cast<uint32_t>(mEffectiveTRRMode)));
  }

 private:
  ~SingleDNSAddrRecord() { LOG(("SingleDNSAddrRecord dtor:%p", this)); }

  nsCString mCanonicalName;
  NetAddr mAddress;

  bool mIsTRR = false;
  double mTrrFetchDuration = 0.0;
  double mTrrFetchDurationNetworkOnly = 0.0;
  nsIRequest::TRRMode mEffectiveTRRMode = nsIRequest::TRR_DEFAULT_MODE;
  nsITRRSkipReason::value mTrrSkipReason = nsITRRSkipReason::TRR_UNSET;
  uint32_t mTTL = 60;
  mozilla::TimeStamp mLastUpdate = TimeStamp::Now();
  bool mDone = false;
};

NS_IMPL_ISUPPORTS(SingleDNSAddrRecord, nsIDNSRecord, nsIDNSAddrRecord)

NS_IMETHODIMP
SingleDNSAddrRecord::GetCanonicalName(nsACString& aResult) {
  aResult.Assign(mCanonicalName);
  return NS_OK;
}

NS_IMETHODIMP
SingleDNSAddrRecord::IsTRR(bool* aRetval) {
  *aRetval = mIsTRR;
  return NS_OK;
}

NS_IMETHODIMP
SingleDNSAddrRecord::GetTrrFetchDuration(double* aTime) {
  *aTime = mTrrFetchDuration;
  return NS_OK;
}

NS_IMETHODIMP
SingleDNSAddrRecord::GetTrrFetchDurationNetworkOnly(double* aTime) {
  *aTime = mTrrFetchDurationNetworkOnly;
  return NS_OK;
}

NS_IMETHODIMP
SingleDNSAddrRecord::GetScriptableNextAddr(uint16_t aPort,
                                           nsINetAddr** aResult) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
SingleDNSAddrRecord::GetNextAddrAsString(nsACString& aResult) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
SingleDNSAddrRecord::HasMore(bool* aResult) { return NS_ERROR_NOT_IMPLEMENTED; }

NS_IMETHODIMP
SingleDNSAddrRecord::Rewind() { return NS_ERROR_NOT_IMPLEMENTED; }

NS_IMETHODIMP
SingleDNSAddrRecord::ReportUnusable(uint16_t aPort) {
  // TODO: should we block this address?
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
SingleDNSAddrRecord::GetEffectiveTRRMode(nsIRequest::TRRMode* aMode) {
  *aMode = mEffectiveTRRMode;
  return NS_OK;
}

NS_IMETHODIMP
SingleDNSAddrRecord::GetTrrSkipReason(nsITRRSkipReason::value* aReason) {
  *aReason = mTrrSkipReason;
  return NS_OK;
}

NS_IMETHODIMP
SingleDNSAddrRecord::GetTtl(uint32_t* aTtl) {
  *aTtl = mTTL;
  return NS_OK;
}

NS_IMETHODIMP
SingleDNSAddrRecord::GetLastUpdate(mozilla::TimeStamp* aLastUpdate) {
  *aLastUpdate = mLastUpdate;
  return NS_OK;
}

NS_IMETHODIMP
SingleDNSAddrRecord::GetFromStaleCache(bool* aResult) {
  // Happy Eyeballs reads staleness directly off the resolved DNS record to feed
  // the state machine; the per-address record it hands to the connection does
  // not carry it, and nothing downstream reads it.
  *aResult = false;
  return NS_OK;
}

NS_IMETHODIMP
SingleDNSAddrRecord::GetNextAddr(uint16_t aPort, NetAddr* aAddr) {
  if (mDone) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  *aAddr = mAddress;
  mDone = true;

  uint16_t port = htons(aPort);
  if (aAddr->raw.family == AF_INET) {
    aAddr->inet.port = port;
  } else if (aAddr->raw.family == AF_INET6) {
    aAddr->inet6.port = port;
  }

  return NS_OK;
}

NS_IMETHODIMP
SingleDNSAddrRecord::GetAddresses(nsTArray<NetAddr>& aAddressArray) {
  // Match a regular DNS address record, which stores port-less addresses (the
  // port is applied later via GetNextAddr). Connection coalescing compares the
  // stored addresses against a connection's peer address with the port zeroed
  // (see FindCoalescableConnection), so a non-zero port here would prevent the
  // match.
  NetAddr addr = mAddress;
  if (addr.raw.family == AF_INET) {
    addr.inet.port = 0;
  } else if (addr.raw.family == AF_INET6) {
    addr.inet6.port = 0;
  }
  aAddressArray.AppendElement(addr);
  return NS_OK;
}

// -------------------- ConnectionEstablisher --------------------

NS_IMPL_ISUPPORTS(ConnectionEstablisher, nsITransportEventSink,
                  nsIInterfaceRequestor)

ConnectionEstablisher::ConnectionEstablisher(nsHttpConnectionInfo* aConnInfo,
                                             const NetAddr& aAddr,
                                             uint32_t aCaps, bool aAllow1918)
    : mConnInfo(aConnInfo), mAddr(aAddr), mCaps(aCaps), mAllow1918(aAllow1918) {
  LOG(("ConnectionEstablisher ctor:%p", this));
}

ConnectionEstablisher::~ConnectionEstablisher() {
  LOG(("ConnectionEstablisher dtor:%p", this));
  MaybeSetConnectingDone();

  if (!OnSocketThread() && gSocketTransportService) {
    gSocketTransportService->Dispatch(
        NS_NewRunnableFunction(
            "~ConnectionEstablisher",
            [transaction = std::move(mTransaction), handle = std::move(mHandle),
             resultConn = std::move(mResultConn),
             transportStatusCallback = std::move(mTransportStatusCallback),
             lnaCheckCallback = std::move(mLnaCheckCallback),
             callback = std::move(mCallback)]() {}),
        NS_DISPATCH_NORMAL);
  }
}

void ConnectionEstablisher::SetConnecting() {
  MOZ_ASSERT(!mWaitingForConnect);
  mWaitingForConnect = true;
  gHttpHandler->ConnMgr()->StartedConnect();
}

void ConnectionEstablisher::MaybeSetConnectingDone() {
  if (mWaitingForConnect) {
    mWaitingForConnect = false;
    gHttpHandler->ConnMgr()->RecvdConnect();
  }
}

void ConnectionEstablisher::ClearResultConnection() { mResultConn = nullptr; }

nsresult ConnectionEstablisher::ActivateConnectionWithTransaction(
    RefPtr<HttpConnectionBase> aConn,
    std::function<void(nsresult)> aOnActivated) {
  LOG(
      ("ConnectionEstablisher::ActivateConnectionWithTransaction %p conn=%p "
       "trans=%p",
       this, aConn.get(), mTransaction.get()));

  aConn->SetIsRacing(true);

  mHasConnected = true;
  mResultConn = aConn;
  mHandle = new ConnectionHandle(aConn);

  MOZ_ASSERT(mTransaction,
             "HappyEyeballsConnectionAttempt must hand us a transaction "
             "before we can activate a connection.");

  mTransaction->SetConnectedCallback(
      [self = RefPtr{this},
       onActivated = std::move(aOnActivated)](nsresult aResult) {
        DispatchToCurrent(NS_NewRunnableFunction(
            "ConnectionEstablisher::ActivateCallback",
            [self, aResult, onActivated = std::move(onActivated)]() {
              if (NS_FAILED(aResult)) {
                self->Finish(aResult);
                return;
              }

              onActivated(NS_OK);
            }));
      });

  mTransaction->SetConnection(mHandle);
  nsresult rv = aConn->Activate(mTransaction, mCaps, 0);
  if (NS_FAILED(rv)) {
    Finish(rv);
    return rv;
  }

  return NS_OK;
}

void ConnectionEstablisher::FinishInternal(nsresult aResult) {
  LOG(("ConnectionEstablisher::FinishInternal %p result=%x", this,
       static_cast<uint32_t>(aResult)));

  if (mFinished) {
    return;
  }
  mFinished = true;

  MaybeSetConnectingDone();
  mTransportStatusCallback = nullptr;
  mLnaCheckCallback = nullptr;

  if (mTransaction) {
    // Detach the connected-callback so later Close/cleanup on the
    // transaction can't call us back. Keep the ref itself: the winning
    // HandleTCPConnectionResult / HandleUDPConnectionResult still needs
    // to query establisher->Transaction() afterwards to grab the
    // handshake timings.
    mTransaction->SetConnectedCallback(nullptr);
  }

  if (mCallback) {
    auto cb = std::move(mCallback);
    mCallback = nullptr;
    // HappyEyeballsTransaction is speculative — the real transaction is
    // never driving I/O on this conn — so the H1 fallback path always
    // applies: reset the handle so the conn doesn't cling to it after
    // we're done (H2/H3 manage reuse via their sessions).
    if (mHandle && mHandle->Conn() && !mHandle->Conn()->UsingSpdy() &&
        !mHandle->Conn()->UsingHttp3()) {
      mHandle->Reset();
    }

    // Clear mHandle to break the ref cycle: the establisher holds mHandle
    // (which refs the connection), while the connection holds mCallbacks
    // (which refs the establisher).
    mHandle = nullptr;

    // For H3, check CanReuse() to guard against mHttp3Session being
    // destroyed between the async handshake callback and this point.
    // For H1/H2, skip: H1 needs the connection even if the socket is dead
    // (e.g. cert error) to propagate the proper error code; H2 sessions
    // may be incorrectly marked DontReuse by the proxy transaction's caps.
    bool connUsable =
        mResultConn && (!mResultConn->UsingHttp3() || mResultConn->CanReuse());
    if (NS_SUCCEEDED(aResult) && connUsable) {
      if (!mConnectStart.IsNull()) {
        mResultConn->SetConnectBootstrapTimings(mConnectStart, mTcpConnectEnd);
      }
      cb(std::move(mResultConn));
    } else {
      LOG(
          ("ConnectionEstablisher::FinishInternal %p conn rejected "
           "aResult=%x connUsable=%d UsingHttp3=%d",
           this, static_cast<uint32_t>(aResult), connUsable,
           mResultConn ? mResultConn->UsingHttp3() : 0));
      cb(Err(NS_FAILED(aResult) ? aResult : NS_ERROR_ABORT));
    }
  }

  mAddrRecord = nullptr;
}

already_AddRefed<nsIDNSAddrRecord> ConnectionEstablisher::AddrRecord() const {
  nsCOMPtr<nsIDNSAddrRecord> record = mAddrRecord;
  return record.forget();
}

NS_IMETHODIMP
ConnectionEstablisher::GetInterface(const nsIID& iid, void** result) {
  nsCOMPtr<nsIInterfaceRequestor> callbacks;
  {
    MutexAutoLock lock(mSecurityCallbacksLock);
    callbacks = mSecurityCallbacks;
  }
  if (callbacks) {
    return callbacks->GetInterface(iid, result);
  }
  return NS_ERROR_NO_INTERFACE;
}

NS_IMETHODIMP
ConnectionEstablisher::OnTransportStatus(nsITransport* trans, nsresult status,
                                         int64_t progress,
                                         int64_t progressMax) {
  if (status == NS_NET_STATUS_CONNECTING_TO) {
    mConnectStart = TimeStamp::Now();
  } else if (status == NS_NET_STATUS_CONNECTED_TO) {
    mConnectedOK = true;
    mTcpConnectEnd = TimeStamp::Now();
  }

  if (mTransportStatusCallback) {
    mTransportStatusCallback(trans, status, progress);
  }

  return NS_OK;
}

NS_IMPL_ISUPPORTS_INHERITED(TCPConnectionEstablisher, ConnectionEstablisher,
                            nsIOutputStreamCallback)

TCPConnectionEstablisher::TCPConnectionEstablisher(
    nsHttpConnectionInfo* aConnInfo, NetAddr aAddr, uint32_t aCaps,
    bool aSpeculative, bool aAllow1918)
    : ConnectionEstablisher(aConnInfo, aAddr, aCaps, aAllow1918),
      mSpeculative(aSpeculative) {}

TCPConnectionEstablisher::~TCPConnectionEstablisher() {
  // mSocketTransport / mStreamOut / mStreamIn must be released on the
  // socket thread.
  if (!OnSocketThread() && gSocketTransportService) {
    gSocketTransportService->Dispatch(
        NS_NewRunnableFunction("~TCPConnectionEstablisher",
                               [socketTransport = std::move(mSocketTransport),
                                streamOut = std::move(mStreamOut),
                                streamIn = std::move(mStreamIn)]() {}),
        NS_DISPATCH_NORMAL);
  }
}

bool ConnectionEstablisher::RefuseIfLocalAddress() {
  if (mAllow1918 || !mAddr.IsIPAddrLocal()) {
    return false;
  }
  LOG(
      ("ConnectionEstablisher::RefuseIfLocalAddress %p refusing speculative "
       "connection to local address [%s]",
       this, mAddr.ToString().get()));
  mRefusedForLocalAddress = true;
  return true;
}

bool TCPConnectionEstablisher::Start(DoneCallback&& aCallback) {
  mCallback = std::move(aCallback);
  if (RefuseIfLocalAddress()) {
    return false;
  }
  mAddrRecord = new SingleDNSAddrRecord(mAddr, mDnsMetadata);

  nsresult rv = CreateAndConfigureSocketTransport();
  if (NS_FAILED(rv)) {
    return false;
  }

  return true;
}

void TCPConnectionEstablisher::ResetSpeculativeFlags() {
  uint32_t flags = 0;
  if (!mSocketTransport ||
      NS_FAILED(mSocketTransport->GetConnectionFlags(&flags))) {
    return;
  }

  flags &= ~nsISocketTransport::DISABLE_RFC1918;
  flags &= ~nsISocketTransport::IS_SPECULATIVE_CONNECTION;
  mSocketTransport->SetConnectionFlags(flags);
}

void TCPConnectionEstablisher::Close(nsresult aReason) {
  LOG(("TCPConnectionEstablisher::Close %p aReason=%x", this,
       static_cast<uint32_t>(aReason)));

  mHandle = nullptr;
  if (mResultConn) {
    // Every connection touched by HE is marked non-reusable: adopted conns
    // finish serving the in-flight real txn then close; losing conns are
    // torn down immediately below.  Either way the CM must never hand a new
    // transaction to a connection that participated in an HE race.
    bool adopted = mTransaction && mTransaction->IsAdopted();
    mResultConn->DontReuse();
    if (adopted) {
      LOG(("TCPConnectionEstablisher::Close %p adopted conn %p DontReuse", this,
           mResultConn.get()));
    } else {
      LOG(("TCPConnectionEstablisher::Close closing connection %p",
           mResultConn.get()));
      // Use CloseTransaction rather than Close to properly clean up the
      // SPDY session: if we only called Close, an Http2Session in
      // mSpdySession/mTransaction would never be released and the conn
      // would stay alive indefinitely without a pending socket read to
      // trigger CloseTransaction naturally.
      mResultConn->CloseTransaction(mResultConn->Transaction(), aReason);
    }
    mResultConn = nullptr;
  }

  if (mSocketTransport) {
    mSocketTransport->SetEventSink(nullptr, nullptr);
    mSocketTransport->SetSecurityCallbacks(nullptr);
    mSocketTransport = nullptr;
  }

  // Tell output stream (and backup) to forget the half open socket.
  if (mStreamOut) {
    mStreamOut->AsyncWait(nullptr, 0, 0, nullptr);
    mStreamOut = nullptr;
  }

  // Lose references to input stream (and backup).
  if (mStreamIn) {
    mStreamIn->AsyncWait(nullptr, 0, 0, nullptr);
    mStreamIn = nullptr;
  }

  // Release the DNS address record to avoid leaking SingleDNSAddrRecord
  mAddrRecord = nullptr;

  mConnectedOK = false;
  Finish(aReason);
}

nsresult TCPConnectionEstablisher::CreateAndConfigureSocketTransport() {
  nsresult rv = NS_OK;
  nsTArray<nsCString> socketTypes;
  if (mConnInfo->FirstHopSSL()) {
    socketTypes.AppendElement("ssl"_ns);
  } else {
    const nsCString& defaultType = gHttpHandler->DefaultSocketType();
    if (!defaultType.IsVoid()) {
      socketTypes.AppendElement(defaultType);
    }
  }

  nsCOMPtr<nsISocketTransport> socketTransport;
  nsCOMPtr<nsISocketTransportService> sts =
      components::SocketTransport::Service();
  if (!sts) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  LOG(
      ("TCPConnectionEstablisher::CreateAndConfigureSocketTransport [this=%p "
       "info=%s] "
       "setup routed transport to origin %s:%d via %s:%d\n",
       this, mConnInfo->HashKey().get(), mConnInfo->Origin(),
       mConnInfo->OriginPort(), mConnInfo->RoutedHost(),
       mConnInfo->RoutedPort()));

  nsCOMPtr<nsIRoutedSocketTransportService> routedSTS(do_QueryInterface(sts));
  if (routedSTS) {
    rv = routedSTS->CreateRoutedTransport(
        socketTypes, mConnInfo->GetOrigin(), mConnInfo->OriginPort(),
        mConnInfo->GetRoutedHost(), mConnInfo->RoutedPort(),
        mConnInfo->ProxyInfo(), mAddrRecord, getter_AddRefs(socketTransport));
  } else {
    if (!mConnInfo->GetRoutedHost().IsEmpty()) {
      // There is a route requested, but the legacy nsISocketTransportService
      // can't handle it.
      // Origin should be reachable on origin host name, so this should
      // not be a problem - but log it.
      LOG(
          ("%p using legacy nsISocketTransportService "
           "means explicit route %s:%d will be ignored.\n",
           this, mConnInfo->RoutedHost(), mConnInfo->RoutedPort()));
    }

    rv = sts->CreateTransport(socketTypes, mConnInfo->GetOrigin(),
                              mConnInfo->OriginPort(), mConnInfo->ProxyInfo(),
                              mAddrRecord, getter_AddRefs(socketTransport));
  }

  if (NS_FAILED(rv)) {
    return rv;
  }

  uint32_t tmpFlags = 0;
  if (mCaps & NS_HTTP_REFRESH_DNS) {
    tmpFlags = nsISocketTransport::BYPASS_CACHE;
  }

  tmpFlags |= nsISocketTransport::GetFlagsFromTRRMode(
      NS_HTTP_TRR_MODE_FROM_FLAGS(mCaps));

  if (mCaps & NS_HTTP_LOAD_ANONYMOUS) {
    tmpFlags |= nsISocketTransport::ANONYMOUS_CONNECT;
  }

  // When we are making a speculative connection we do not propagate all flags
  // in mCaps, so we need to query nsHttpConnectionInfo directly as well.
  if ((mCaps & NS_HTTP_LOAD_ANONYMOUS_CONNECT_ALLOW_CLIENT_CERT) ||
      mConnInfo->GetAnonymousAllowClientCert()) {
    tmpFlags |= nsISocketTransport::ANONYMOUS_CONNECT_ALLOW_CLIENT_CERT;
  }

  if (mConnInfo->GetPrivate()) {
    tmpFlags |= nsISocketTransport::NO_PERMANENT_STORAGE;
  }

  if (mCaps & NS_HTTP_DISALLOW_ECH) {
    tmpFlags |= nsISocketTransport::DONT_TRY_ECH;
  }

  if (mCaps & NS_HTTP_IS_RETRY) {
    tmpFlags |= nsISocketTransport::IS_RETRY;
  }

  if (((mCaps & NS_HTTP_BE_CONSERVATIVE) || mConnInfo->GetBeConservative()) &&
      gHttpHandler->ConnMgr()->BeConservativeIfProxied(
          mConnInfo->ProxyInfo())) {
    LOG(("Setting Socket to BE_CONSERVATIVE"));
    tmpFlags |= nsISocketTransport::BE_CONSERVATIVE;
  }

  // IP hint addresses from HTTPS records are handled by the Happy Eyeballs
  // state machine.

  if (!mAllow1918) {
    tmpFlags |= nsISocketTransport::DISABLE_RFC1918;
  }

  if (mSpeculative) {
    tmpFlags |= nsISocketTransport::IS_SPECULATIVE_CONNECTION;
  }

  socketTransport->SetConnectionFlags(tmpFlags);
  socketTransport->SetTlsFlags(mConnInfo->GetTlsFlags());
  socketTransport->SetOriginAttributes(mConnInfo->GetOriginAttributes());
  // Must match DnsAndConnectSocket::TransportSetup::SetupStreams: without this
  // TRR sockets established through Happy Eyeballs are not marked, so neither
  // nsSocketEvent::GetPriority nor the socket thread's TRR-first servicing sees
  // them.
  socketTransport->SetIsTRRConnection(mConnInfo->GetIsTrrServiceChannel());

  socketTransport->SetQoSBits(gHttpHandler->GetQoSBits());

  rv = socketTransport->SetEventSink(this, nullptr);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = socketTransport->SetSecurityCallbacks(this);
  NS_ENSURE_SUCCESS(rv, rv);

  if (nsHttpHandler::EchConfigEnabled() &&
      !mConnInfo->GetEchConfig().IsEmpty()) {
    LOG(("Setting ECH"));
    rv = socketTransport->SetEchConfig(mConnInfo->GetEchConfig());
    NS_ENSURE_SUCCESS(rv, rv);
  }

  mSynStarted = TimeStamp::Now();

  nsCOMPtr<nsIOutputStream> sout;
  rv = socketTransport->OpenOutputStream(nsITransport::OPEN_UNBUFFERED, 0, 0,
                                         getter_AddRefs(sout));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIInputStream> sin;
  rv = socketTransport->OpenInputStream(nsITransport::OPEN_UNBUFFERED, 0, 0,
                                        getter_AddRefs(sin));
  NS_ENSURE_SUCCESS(rv, rv);

  mSocketTransport = socketTransport.forget();
  mStreamIn = do_QueryInterface(sin);
  mStreamOut = do_QueryInterface(sout);

  rv = mStreamOut->AsyncWait(this, 0, 0, nullptr);
  if (NS_SUCCEEDED(rv)) {
    SetConnecting();
  }
  return rv;
}

void TCPConnectionEstablisher::Finish(nsresult aResult) {
  // Release TCP-specific resources first
  mStreamOut = nullptr;
  mStreamIn = nullptr;
  mSocketTransport = nullptr;

  FinishInternal(aResult);
}

NS_IMETHODIMP
TCPConnectionEstablisher::OnOutputStreamReady(nsIAsyncOutputStream* aOut) {
  MOZ_DIAGNOSTIC_ASSERT(mStreamOut == aOut, "stream mismatch");
  LOG(("TCPConnectionEstablisher::OnOutputStreamReady %p mFinished=%d", this,
       mFinished));

  if (mFinished) {
    return NS_OK;
  }

  // Early LNA check: reject the connection before TLS handshake can send SNI.
  if (mLnaCheckCallback && mSocketTransport) {
    nsresult rv = mLnaCheckCallback(mSocketTransport);
    if (NS_FAILED(rv)) {
      mSocketTransport->Close(rv);
      Finish(rv);
      return NS_OK;
    }
  }

  // Create nsHttpConnection when the output stream is ready.
  RefPtr<nsHttpConnection> conn = new nsHttpConnection();
  conn->SetTransactionCaps(mCaps);
  // TODO:
  // 1. BootstrapTimings
  // 2. SetTransactionCaps
  // 3. SetSecurityCallbacks
  // 4. RecordIPFamilyPreference

  nsresult rv = conn->Init(
      mConnInfo, gHttpHandler->ConnMgr()->mMaxRequestDelay, mSocketTransport,
      mStreamIn, mStreamOut, mConnectedOK, NS_OK, this,
      PR_MillisecondsToInterval(static_cast<uint32_t>(
          (TimeStamp::Now() - mSynStarted).ToMilliseconds())),
      mCaps & NS_HTTP_ALLOW_SPDY_WITHOUT_KEEPALIVE);

  if (NS_FAILED(rv)) {
    Finish(rv);
    return NS_OK;
  }

  // Clear TCP-specific resources before activation
  mSocketTransport = nullptr;
  mStreamOut = nullptr;
  mStreamIn = nullptr;

  rv = ActivateConnectionWithTransaction(
      conn, [self = RefPtr{this}](nsresult aResult) { self->Finish(aResult); });

  return rv;
}

// -------------------- UDPConnectionEstablisher --------------------

UDPConnectionEstablisher::UDPConnectionEstablisher(
    nsHttpConnectionInfo* aConnInfo, NetAddr aAddr, uint32_t aCaps,
    bool /* aSpeculative */, bool aAllow1918)
    : ConnectionEstablisher(aConnInfo, aAddr, aCaps, aAllow1918) {
  LOG(("UDPConnectionEstablisher ctor:%p", this));
}

UDPConnectionEstablisher::~UDPConnectionEstablisher() {
  LOG(("UDPConnectionEstablisher dtor:%p", this));
}

bool UDPConnectionEstablisher::Start(DoneCallback&& aCallback) {
  LOG(("UDPConnectionEstablisher::Start %p", this));
  mCallback = std::move(aCallback);
  if (RefuseIfLocalAddress()) {
    return false;
  }
  mAddrRecord = new SingleDNSAddrRecord(mAddr, mDnsMetadata);

  nsresult rv = CreateAndConfigureUDPConn();
  if (NS_FAILED(rv)) {
    return false;
  }

  return true;
}

void UDPConnectionEstablisher::Close(nsresult aReason) {
  LOG(("UDPConnectionEstablisher::Close %p aReason=%x", this,
       static_cast<uint32_t>(aReason)));

  mHandle = nullptr;
  if (mResultConn) {
    // If the HT on this conn has been adopted, the conn is already
    // driving the real nsHttpTransaction — tearing it down here would
    // close the real txn. Just mark the conn non-reusable and let it
    // finish serving the in-flight request naturally.
    bool adopted = mTransaction && mTransaction->IsAdopted();
    if (adopted) {
      LOG(("UDPConnectionEstablisher::Close %p adopted conn %p DontReuse", this,
           mResultConn.get()));
      mResultConn->DontReuse();
    } else {
      LOG(("UDPConnectionEstablisher::Close closing connection %p",
           mResultConn.get()));
      // TODO: for some cases we might want to exclude HTTP/3.
      mResultConn->SetDontExclude();
      mResultConn->Close(aReason);
    }
    mResultConn = nullptr;
  }

  // Release the DNS address record to avoid leaking SingleDNSAddrRecord
  mAddrRecord = nullptr;

  Finish(aReason);
}

nsresult UDPConnectionEstablisher::CreateAndConfigureUDPConn() {
  LOG(
      ("UDPConnectionEstablisher::CreateAndConfigureUDPConn [this=%p "
       "info=%s]",
       this, mConnInfo->HashKey().get()));

  RefPtr<HttpConnectionUDP> connUDP = new HttpConnectionUDP();
  connUDP->SetTransactionCaps(mCaps);

  nsresult rv = connUDP->Init(mConnInfo, mAddrRecord, NS_OK, this, mCaps);
  if (NS_FAILED(rv)) {
    return rv;
  }

  SetConnecting();

  rv = ActivateConnectionWithTransaction(
      connUDP,
      [self = RefPtr{this}](nsresult aResult) { self->Finish(aResult); });

  return rv;
}

void UDPConnectionEstablisher::Finish(nsresult aResult) {
  LOG(("UDPConnectionEstablisher::Finish %p result=%x", this,
       static_cast<uint32_t>(aResult)));

  FinishInternal(aResult);
}

}  // namespace mozilla::net
