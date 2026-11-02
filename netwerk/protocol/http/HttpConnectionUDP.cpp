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

#define TLS_EARLY_DATA_NOT_AVAILABLE 0
#define TLS_EARLY_DATA_AVAILABLE_BUT_NOT_USED 1
#define TLS_EARLY_DATA_AVAILABLE_AND_USED 2

#include "ASpdySession.h"
#include "ConnectionHandle.h"
#include "Http3Session.h"
#include "HttpConnectionUDP.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "nsComponentManagerUtils.h"
#include "nsHttpConnectionMgr.h"
#include "nsHttpHandler.h"
#include "nsHttpTransaction.h"
#include "nsIDNSRecord.h"
#include "nsIHttpChannelInternal.h"
#include "nsINetAddr.h"
#include "nsISocketProvider.h"
#include "nsNetAddr.h"
#include "nsSocketTransportService2.h"
#include "nsStringStream.h"
#include "nsThreadUtils.h"

namespace mozilla {
namespace net {

//-----------------------------------------------------------------------------
// ConnectUDPTransaction
//-----------------------------------------------------------------------------

class ConnectUDPTransaction : public nsAHttpTransaction {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS

  explicit ConnectUDPTransaction(nsAHttpTransaction* aTrans,
                                 nsIInputStream* aStream)
      : mTransaction(aTrans), mProxyConnectStream(aStream) {
    LOG(("ConnectUDPTransaction ctor: %p", this));
  }

  void SetConnection(nsAHttpConnection* aConn) override { mConnection = aConn; }
  nsAHttpConnection* Connection() override { return mConnection; }
  void GetSecurityCallbacks(nsIInterfaceRequestor** aCallbacks) override {
    mTransaction->GetSecurityCallbacks(aCallbacks);
  }
  void OnTransportStatus(nsITransport* transport, nsresult status,
                         int64_t progress) override {
    mTransaction->OnTransportStatus(transport, status, progress);
  }
  bool IsDone() override { return mIsDone; }
  nsresult Status() override { return mTransaction->Status(); }
  uint32_t Caps() override { return mTransaction->Caps(); }

  static nsresult ReadRequestSegment(nsIInputStream* stream, void* closure,
                                     const char* buf, uint32_t offset,
                                     uint32_t count, uint32_t* countRead) {
    ConnectUDPTransaction* trans = (ConnectUDPTransaction*)closure;
    nsresult rv = trans->mReader->OnReadSegment(buf, count, countRead);
    return rv;
  }

  [[nodiscard]] nsresult ReadSegments(nsAHttpSegmentReader* reader,
                                      uint32_t count,
                                      uint32_t* countRead) override {
    mReader = reader;
    (void)mProxyConnectStream->ReadSegments(ReadRequestSegment, this, count,
                                            countRead);
    mReader = nullptr;
    uint64_t avil = 0;
    (void)mProxyConnectStream->Available(&avil);
    if (!avil) {
      mIsDone = true;
    }
    return NS_OK;
  }
  [[nodiscard]] nsresult WriteSegments(nsAHttpSegmentWriter* writer,
                                       uint32_t count,
                                       uint32_t* countWritten) override {
    // This is hacky. We should set Connection properly in
    // nsHttpConnectionMgr::DispatchTransaction.
    if (!mTransaction->Connection()) {
      mTransaction->SetConnection(mConnection);
    }
    nsresult rv = mTransaction->WriteSegments(writer, count, countWritten);
    mTransaction = nullptr;
    return rv;
  }
  void Close(nsresult reason) override {
    LOG(("ConnectUDPTransaction close mTransaction=%p", mTransaction.get()));
    if (mTransaction) {
      mTransaction->Close(reason);
    }
  }
  nsHttpConnectionInfo* ConnectionInfo() override {
    return mTransaction->ConnectionInfo();
  }
  void SetProxyConnectFailed() override {
    mTransaction->SetProxyConnectFailed();
  }
  const nsHttpRequestHead* RequestHead() override {
    return mTransaction->RequestHead();
  }
  uint32_t Http1xTransactionCount() override { return 0; }
  [[nodiscard]] nsresult TakeSubTransactions(
      nsTArray<RefPtr<nsAHttpTransaction>>& outTransactions) override {
    return NS_OK;
  }

 protected:
  virtual ~ConnectUDPTransaction() {
    LOG(("ConnectUDPTransaction dtor: %p", this));
  }

  RefPtr<nsAHttpTransaction> mTransaction;
  nsCOMPtr<nsIInputStream> mProxyConnectStream;
  RefPtr<nsAHttpConnection> mConnection;
  bool mIsDone = false;
  nsAHttpSegmentReader* mReader = nullptr;
};

NS_IMPL_ISUPPORTS(ConnectUDPTransaction, nsISupportsWeakReference)

//-----------------------------------------------------------------------------
// Http3ConnectTransaction
//-----------------------------------------------------------------------------

// We need a dummy transaction to keep the Http3StreamTunnel alive.
// Http3StreamTunnel instances are owned via the transaction-to-stream map
// in Http3Session::mStreamTransactionHash.
class Http3ConnectTransaction : public ConnectUDPTransaction {
 public:
  explicit Http3ConnectTransaction(uint32_t aCaps, nsHttpConnectionInfo* aInfo)
      : ConnectUDPTransaction(nullptr, nullptr),
        mCaps(aCaps),
        mConnInfo(aInfo) {
    LOG(("Http3ConnectTransaction ctor: %p", this));
  }

  uint32_t Caps() override { return mCaps; }
  nsHttpConnectionInfo* ConnectionInfo() override { return mConnInfo; }

  void OnTransportStatus(nsITransport* transport, nsresult status,
                         int64_t progress) override {}

  [[nodiscard]] nsresult ReadSegments(nsAHttpSegmentReader* reader,
                                      uint32_t count,
                                      uint32_t* countRead) override {
    MOZ_ASSERT_UNREACHABLE("Shouldn't be called");
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  [[nodiscard]] nsresult WriteSegments(nsAHttpSegmentWriter* writer,
                                       uint32_t count,
                                       uint32_t* countWritten) override {
    MOZ_ASSERT_UNREACHABLE("Shouldn't be called");
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  void Close(nsresult reason) override { mConnection = nullptr; }
  const nsHttpRequestHead* RequestHead() override { return nullptr; }

 private:
  virtual ~Http3ConnectTransaction() {
    LOG(("Http3ConnectTransaction dtor: %p", this));
  }

  uint32_t mCaps = 0;
  RefPtr<nsHttpConnectionInfo> mConnInfo;
};

//-----------------------------------------------------------------------------
// HttpConnectionUDP <public>
//-----------------------------------------------------------------------------

HttpConnectionUDP::HttpConnectionUDP() : mHttpHandler(gHttpHandler) {
  LOG(("Creating HttpConnectionUDP @%p\n", this));
}

HttpConnectionUDP::~HttpConnectionUDP() {
  LOG(("Destroying HttpConnectionUDP @%p\n", this));

  if (mForceSendTimer) {
    mForceSendTimer->Cancel();
    mForceSendTimer = nullptr;
  }

  MOZ_ASSERT(mQueuedHttpConnectTransaction.IsEmpty(),
             "Should not have any queued transactions");
  MOZ_ASSERT(mQueuedConnectUdpTransaction.IsEmpty(),
             "Should not have any queued transactions");
}

void HttpConnectionUDP::RekeyAfterHttp3OnlyHandOff(
    nsHttpConnectionInfo* aConnInfo) {
  MOZ_ASSERT(aConnInfo);
  MOZ_ASSERT(mConnInfo);
  MOZ_ASSERT(mConnInfo->GetHttp3Only(),
             "only an h3-only connection info gets handed off");
  MOZ_ASSERT(!aConnInfo->GetHttp3Only(),
             "hand-off must relax the policy to Allowed");
  MOZ_ASSERT(aConnInfo->GetOrigin().Equals(mConnInfo->GetOrigin()) &&
                 aConnInfo->OriginPort() == mConnInfo->OriginPort(),
             "hand-off must not change the origin");

  LOG(("HttpConnectionUDP::RekeyAfterHttp3OnlyHandOff this=%p %s -> %s", this,
       mConnInfo->HashKey().get(), aConnInfo->HashKey().get()));
  mConnInfo = aConnInfo;

  // The session holds its own clone and later resolves connection entries from
  // it, so it has to be re-keyed too.
  if (mHttp3Session) {
    mHttp3Session->RekeyAfterHttp3OnlyHandOff(aConnInfo);
  }
}

nsresult HttpConnectionUDP::Init(nsHttpConnectionInfo* info,
                                 nsIDNSRecord* dnsRecord, nsresult status,
                                 nsIInterfaceRequestor* callbacks,
                                 uint32_t caps) {
  LOG1(("HttpConnectionUDP::Init this=%p", this));
  NS_ENSURE_ARG_POINTER(info);
  NS_ENSURE_TRUE(!mConnInfo, NS_ERROR_ALREADY_INITIALIZED);

  mConnInfo = info;
  MOZ_ASSERT(mConnInfo);
  MOZ_ASSERT(mConnInfo->IsHttp3() || mConnInfo->IsHttp3ProxyConnection());

  mErrorBeforeConnect = status;
  mAlpnToken = mConnInfo->GetNPNToken();
  if (NS_FAILED(mErrorBeforeConnect)) {
    InitCallbacks(callbacks, "HttpConnectionUDP::mCallbacks");
    SetCloseReason(ToCloseReason(mErrorBeforeConnect));
    return mErrorBeforeConnect;
  }

  nsCOMPtr<nsIDNSAddrRecord> dnsAddrRecord = do_QueryInterface(dnsRecord);
  if (!dnsAddrRecord) {
    return NS_ERROR_FAILURE;
  }
  dnsAddrRecord->IsTRR(&mResolvedByTRR);
  dnsAddrRecord->GetEffectiveTRRMode(&mEffectiveTRRMode);
  dnsAddrRecord->GetTrrSkipReason(&mTRRSkipReason);
  NetAddr peerAddr;
  uint16_t port =
      mConnInfo->IsHttp3ProxyConnection()
          ? mConnInfo->ProxyPort()
          : (!mConnInfo->GetRoutedHost().IsEmpty() ? mConnInfo->RoutedPort()
                                                   : mConnInfo->OriginPort());

  nsresult rv = dnsAddrRecord->GetNextAddr(port, &peerAddr);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // We are disabling 0.0.0.0 for non-test purposes.
  // See https://github.com/whatwg/fetch/pull/1763 for context.
  if (peerAddr.IsIPAddrAny()) {
    if (StaticPrefs::network_socket_ip_addr_any_disabled()) {
      LOG(("Connection refused because of 0.0.0.0 IP address\n"));
      return NS_ERROR_CONNECTION_REFUSED;
    }
  }

  mSocket = do_CreateInstance("@mozilla.org/network/udp-socket;1", &rv);
  if (NS_FAILED(rv)) {
    return rv;
  }

  return InitCommon(mSocket, peerAddr, callbacks, caps, false);
}

nsresult HttpConnectionUDP::InitWithSocket(nsHttpConnectionInfo* info,
                                           nsIUDPSocket* aSocket,
                                           NetAddr aPeerAddr,
                                           nsIInterfaceRequestor* callbacks,
                                           uint32_t caps) {
  LOG1(("HttpConnectionUDP::InitWithSocket this=%p", this));
  NS_ENSURE_ARG_POINTER(info);
  NS_ENSURE_TRUE(!mConnInfo, NS_ERROR_ALREADY_INITIALIZED);

  mConnInfo = info;
  MOZ_ASSERT(mConnInfo->IsHttp3() || mConnInfo->IsHttp3ProxyConnection());

  mErrorBeforeConnect = NS_OK;
  mAlpnToken = mConnInfo->GetNPNToken();

  return InitCommon(aSocket, aPeerAddr, callbacks, caps, true);
}

nsresult HttpConnectionUDP::InitCommon(nsIUDPSocket* aSocket,
                                       const NetAddr& aPeerAddr,
                                       nsIInterfaceRequestor* callbacks,
                                       uint32_t caps, bool isInTunnel) {
  mSocket = aSocket;

  if (mConnInfo && mConnInfo->GetIsTrrServiceChannel()) {
    mSocket->MarkAsTRRServiceChannel();
  }

  NetAddr local;
  local.raw.family = aPeerAddr.raw.family;
  nsresult rv = mSocket->InitWithAddress(&local, nullptr, false, 1);
  if (NS_FAILED(rv)) {
    mSocket = nullptr;
    return rv;
  }

  rv = mSocket->SetRecvBufferSize(
      StaticPrefs::network_http_http3_recvBufferSize());
  if (NS_FAILED(rv)) {
    LOG(("HttpConnectionUDP::InitCommon SetRecvBufferSize failed %d [this=%p]",
         static_cast<uint32_t>(rv), this));
    mSocket->Close();
    mSocket = nullptr;
    return rv;
  }

  if (aPeerAddr.raw.family == AF_INET) {
    rv = mSocket->SetDontFragment(true);
    if (NS_FAILED(rv)) {
      LOG(("HttpConnectionUDP::InitCommon SetDontFragment failed %d [this=%p]",
           static_cast<uint32_t>(rv), this));
    }
  }

  // get the resulting socket address.
  rv = mSocket->GetLocalAddr(getter_AddRefs(mSelfAddr));
  if (NS_FAILED(rv)) {
    mSocket->Close();
    mSocket = nullptr;
    return rv;
  }

  uint32_t providerFlags = 0;
  if (caps & NS_HTTP_LOAD_ANONYMOUS) {
    providerFlags |= nsISocketProvider::ANONYMOUS_CONNECT;
  }
  mSocket->SetOriginAttributes(mConnInfo->GetOriginAttributes());
  if (mConnInfo->GetPrivate()) {
    providerFlags |= nsISocketProvider::NO_PERMANENT_STORAGE;
  }
  if (((caps & NS_HTTP_BE_CONSERVATIVE) || mConnInfo->GetBeConservative()) &&
      gHttpHandler->ConnMgr()->BeConservativeIfProxied(
          mConnInfo->ProxyInfo())) {
    providerFlags |= nsISocketProvider::BE_CONSERVATIVE;
  }
  if ((caps & NS_HTTP_IS_RETRY) ||
      (mConnInfo->GetTlsFlags() &
       nsIHttpChannelInternal::TLS_FLAG_CONFIGURE_AS_RETRY)) {
    providerFlags |= nsISocketProvider::IS_RETRY;
  }

  if (mResolvedByTRR) {
    providerFlags |= nsISocketProvider::USED_PRIVATE_DNS;
  }

  mPeerAddr = new nsNetAddr(&aPeerAddr);
  mHttp3Session = new Http3Session();
  rv = mHttp3Session->Init(mConnInfo, mSelfAddr, mPeerAddr, this, providerFlags,
                           callbacks, mSocket, isInTunnel);
  if (NS_FAILED(rv)) {
    LOG(
        ("HttpConnectionUDP::InitCommon Http3Session::Init failed [this=%p "
         "rv=%x]",
         this, static_cast<uint32_t>(rv)));
    mSocket->Close();
    mSocket = nullptr;
    mHttp3Session = nullptr;
    return rv;
  }

  mIsInTunnel = isInTunnel;
  if (mIsInTunnel) {
    mHttp3Session->SetIsInTunnel();
  }

  ChangeConnectionState(ConnectionState::INITED);
  InitCallbacks(callbacks, "HttpConnectionUDP::mCallbacks");

  // Call SyncListen at the end of this function. This call will actually
  // attach the sockte to SocketTransportService.
  rv = mSocket->SyncListen(this);
  if (NS_FAILED(rv)) {
    mSocket->Close();
    mSocket = nullptr;
    return rv;
  }

  return NS_OK;
}

// called on the socket thread
nsresult HttpConnectionUDP::Activate(nsAHttpTransaction* trans, uint32_t caps,
                                     int32_t pri) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG1(("HttpConnectionUDP::Activate [this=%p trans=%p caps=%x]\n", this, trans,
        caps));

  nsHttpTransaction* hTrans = trans->QueryHttpTransaction();
  nsHttpConnectionInfo* transCI = trans->ConnectionInfo();
  NetAddr peerAddr;
  if (!transCI->UsingProxy() && hTrans &&
      NS_SUCCEEDED(GetPeerAddr(&peerAddr))) {
    // set the targetIpAddressSpace in the transaction object, this might be
    // needed by the channel for determining the kind of LNA permissions and/or
    // LNA telemetry
    auto addrSpace = peerAddr.GetIpAddressSpace();
    // h3 always uses TLS via QUIC. When network.lna.defer_https_check is
    // enabled and the QUIC handshake hasn't yet completed, defer the LNA
    // check for Private targets to OnConnected() so we don't prompt for
    // misdirected public hostnames whose certificate would have failed to
    // validate. Local targets are still checked immediately.
    bool deferPrivate = addrSpace == nsILoadInfo::IPAddressSpace::Private &&
                        StaticPrefs::network_lna_defer_https_check() &&
                        mHttp3Session && !mHttp3Session->IsConnected();
    if (deferPrivate) {
      mDeferredLnaTransactions.AppendElement(hTrans);
    } else if (!hTrans->AllowedToConnectToIpAddressSpace(addrSpace)) {
      // we could probably fail early and avoid recreating the H3 session
      // See Bug 1968908
      CloseTransaction(mHttp3Session, NS_ERROR_LOCAL_NETWORK_ACCESS_DENIED);
      trans->Close(NS_ERROR_LOCAL_NETWORK_ACCESS_DENIED);
      return NS_ERROR_LOCAL_NETWORK_ACCESS_DENIED;
    }
  }

  if (!mExperienced && !trans->IsNullTransaction()) {
    mHasFirstHttpTransaction = true;
    // For QUIC we have HttpConnectionUDP before the actual connection
    // has been establish so wait for TLS handshake to be finished before
    // we mark the connection 'experienced'.
    if (!mExperienced && mHttp3Session && mHttp3Session->IsConnected()) {
      mExperienced = true;
    }
  }
  HandOffConnectPhase(trans);

  mTransactionCaps = caps;
  mPriority = pri;

  NS_ENSURE_ARG_POINTER(trans);

  mErrorBeforeConnect = CheckTunnelIsNeeded(trans);

  // Connection failures are Activated() just like regular transacions.
  // If we don't have a confirmation of a connected socket then test it
  // with a write() to get relevant error code.
  if (NS_FAILED(mErrorBeforeConnect)) {
    CloseTransaction(nullptr, mErrorBeforeConnect);
    trans->Close(mErrorBeforeConnect);
    gHttpHandler->ExcludeHttp3(mConnInfo);
    return mErrorBeforeConnect;
  }

  // When mIsInTunnel is false, this HttpConnectionUDP represents the *outer*
  // connection to the proxy. If a proxy CONNECT is still in progress,
  // we need to queue the transaction until the outer connection is fully
  // established.
  //
  // Important: we must not reset the transaction while the outer connection
  // is still connecting. Resetting here could lead to opening another HTTP/3
  // connection.
  if (IsProxyConnectInProgress() && !mIsInTunnel && hTrans) {
    if (!mConnected) {
      mQueuedHttpConnectTransaction.AppendElement(hTrans);
      (void)ResumeSend();
    } else {
      // Don't call ResetTransaction() directly here.
      // HttpConnectionUDP::Activate() may be invoked from
      // nsHttpConnectionMgr::DispatchSpdyPendingQ(), which could run while
      // enumerating all connection entries. ResetTransaction() can insert a new
      // "wild" entry, and modifying the connection-entry table during iteration
      // is not allowed.
      RefPtr<HttpConnectionUDP> self(this);
      RefPtr<nsHttpTransaction> httpTransaction(hTrans);
      nsCOMPtr<nsIRunnable> event = NS_NewRunnableFunction(
          "HttpConnectionUDP::ResetTransaction",
          [self{std::move(self)},
           httpTransaction{std::move(httpTransaction)}]() {
            self->ResetTransaction(httpTransaction);
          });

      if (StaticPrefs::network_trr_high_priority_events() && mConnInfo &&
          mConnInfo->GetIsTrrServiceChannel()) {
        event = new PrioritizableRunnable(
            event.forget(), nsIRunnablePriority::PRIORITY_MEDIUMHIGH);
      }

      DispatchToCurrent(event.forget());
    }
    return NS_OK;
  }

  // This is the CONNECT-UDP case. When mIsInTunnel is true, this is
  // the *inner* connection from the proxy tunnel to the destination website.
  // If the proxy CONNECT is still in progress, we cannot send any data
  // yet because Http3ConnectUDPStream is not allowed to transmit until the
  // tunnel is established. In this case, we queue the transaction and will
  // add it to the Http3Session once the proxy CONNECT completes.
  if (mIsInTunnel && IsProxyConnectInProgress() && hTrans) {
    LOG(("Queue trans %p due to proxy connct in progress", hTrans));
    mQueuedConnectUdpTransaction.AppendElement(hTrans);
    return NS_OK;
  }

  {
    nsCOMPtr<nsIInterfaceRequestor> callbacks = GetCallbacks();
    if (!mHttp3Session->AddStream(trans, pri, callbacks)) {
      MOZ_ASSERT(false);  // this cannot happen!
      trans->Close(NS_ERROR_ABORT);
      return NS_ERROR_FAILURE;
    }
  }

  if (mHasFirstHttpTransaction && mExperienced) {
    mHasFirstHttpTransaction = false;
    mExperienceState |= ConnectionExperienceState::Experienced;
  }

  (void)ResumeSend();
  return NS_OK;
}

void HttpConnectionUDP::OnConnected() {
  LOG(("HttpConnectionUDP::OnConnected %p", this));
  MOZ_ASSERT(!mConnected, "Called more than once");

  mConnected = true;

  // Deferred LNA check: now that the QUIC handshake has succeeded (and the
  // peer presented a valid certificate for the requested name), check
  // whether any deferred transactions are permitted to reach this address
  // space. If denied, tear down the session and close all of them.
  if (!mDeferredLnaTransactions.IsEmpty()) {
    nsTArray<RefPtr<nsHttpTransaction>> deferred =
        std::move(mDeferredLnaTransactions);
    NetAddr peerAddr;
    if (NS_SUCCEEDED(GetPeerAddr(&peerAddr))) {
      auto addrSpace = peerAddr.GetIpAddressSpace();
      bool denied = false;
      for (const auto& t : deferred) {
        if (!t->AllowedToConnectToIpAddressSpace(addrSpace)) {
          denied = true;
          // We might want to break here, but AllowedToConnectToIpAddressSpace
          // has a side effect of setting mTargetIpAddressSpace. In practice
          // the array will usually just contain one transaction.
        }
      }
      if (denied) {
        DontReuse();
        CloseTransaction(mHttp3Session, NS_ERROR_LOCAL_NETWORK_ACCESS_DENIED);
        for (const auto& t : deferred) {
          t->Close(NS_ERROR_LOCAL_NETWORK_ACCESS_DENIED);
        }
        return;
      }
    }
  }

  if (mIsInTunnel) {
    return;
  }

  for (const auto& trans : mQueuedHttpConnectTransaction) {
    ResetTransaction(trans);
  }
  mQueuedHttpConnectTransaction.Clear();
}

already_AddRefed<nsIInputStream> HttpConnectionUDP::CreateProxyConnectStream(
    nsAHttpTransaction* trans) {
  auto request = std::make_unique<nsHttpRequestHead>();
  nsAutoCString host;
  DebugOnly<nsresult> rv{};
  rv = nsHttpHandler::GenerateHostPort(
      nsDependentCString(trans->ConnectionInfo()->Origin()),
      trans->ConnectionInfo()->OriginPort(), host);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  request->SetMethod("CONNECT"_ns);
  request->SetVersion(gHttpHandler->HttpVersion());

  bool shouldResistFingerprinting = trans->Caps() & NS_HTTP_USE_RFP;
  rv = request->SetHeader(nsHttp::User_Agent,
                          gHttpHandler->UserAgent(shouldResistFingerprinting));
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  rv = request->SetHeader(nsHttp::Host, host);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  nsAutoCString val;
  if (NS_SUCCEEDED(
          trans->RequestHead()->GetHeader(nsHttp::Proxy_Authorization, val))) {
    // TODO: we should use Proxy_Authorization here.
    rv = request->SetHeader(nsHttp::Authorization, val);
    MOZ_ASSERT(NS_SUCCEEDED(rv));
  }

  nsAutoCString result;
  request->Flatten(result, false);
  if (LOG1_ENABLED()) {
    LOG(("HttpConnectionUDP::MakeConnectString for transaction=%p[",
         trans->QueryHttpTransaction()));
    LogHeaders(result.get());
    LOG(("]"));
  }
  result.AppendLiteral("\r\n");

  nsCOMPtr<nsIInputStream> stream;
  NS_NewCStringInputStream(getter_AddRefs(stream), std::move(result));
  return stream.forget();
}

nsresult HttpConnectionUDP::CreateTunnelStream(
    nsAHttpTransaction* httpTransaction, HttpConnectionBase** aHttpConnection,
    bool aIsExtendedCONNECT) {
  LOG(("HttpConnectionUDP::CreateTunnelStream %p", this));
  if (!mHttp3Session) {
    return NS_ERROR_UNEXPECTED;
  }

  bool isHttp3 = httpTransaction->ConnectionInfo()->IsHttp3();

  if (!isHttp3) {
    RefPtr<Http3ConnectTransaction> trans = new Http3ConnectTransaction(
        httpTransaction->Caps(), httpTransaction->ConnectionInfo());
    nsCOMPtr<nsIInterfaceRequestor> callbacks = GetCallbacks();
    RefPtr<nsHttpConnection> conn =
        mHttp3Session->CreateTunnelStream(trans, callbacks, mRtt, false);
    RefPtr<ConnectionHandle> handle = new ConnectionHandle(conn);
    trans->SetConnection(handle);

    conn.forget(aHttpConnection);
    return NS_OK;
  }

  nsCOMPtr<nsIInputStream> proxyConnectStream =
      CreateProxyConnectStream(httpTransaction);
  if (!proxyConnectStream) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  RefPtr<ConnectUDPTransaction> trans =
      new ConnectUDPTransaction(httpTransaction, proxyConnectStream);
  nsCOMPtr<nsIInterfaceRequestor> callbacks2 = GetCallbacks();
  RefPtr<HttpConnectionUDP> conn =
      mHttp3Session->CreateTunnelStream(trans, callbacks2);
  RefPtr<ConnectionHandle> handle = new ConnectionHandle(conn);
  trans->SetConnection(handle);

  conn.forget(aHttpConnection);
  return NS_OK;
}

void HttpConnectionUDP::Close(nsresult reason, bool aIsShutdown) {
  LOG(("HttpConnectionUDP::Close [this=%p reason=%" PRIx32 "]\n", this,
       static_cast<uint32_t>(reason)));

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  if (mConnectionState != ConnectionState::CLOSED) {
    RecordConnectionCloseTelemetry(reason);
    ChangeConnectionState(ConnectionState::CLOSED);
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

  nsCOMPtr<nsIUDPSocket> socket = std::move(mSocket);
  if (socket) {
    socket->Close();
  }
  if (mHttp3Session) {
    mHttp3Session->SetCleanShutdown(true);
    mHttp3Session->Close(reason);
    mHttp3Session = nullptr;
  }

  for (const auto& trans : mQueuedHttpConnectTransaction) {
    trans->Close(reason);
  }
  mQueuedHttpConnectTransaction.Clear();
  for (const auto& trans : mQueuedConnectUdpTransaction) {
    trans->Close(reason);
  }
  mQueuedConnectUdpTransaction.Clear();
}

void HttpConnectionUDP::DontReuse() {
  LOG(("HttpConnectionUDP::DontReuse %p http3session=%p\n", this,
       mHttp3Session.get()));
  mDontReuse = true;
  if (mHttp3Session) {
    mHttp3Session->DontReuse();
  }
}

bool HttpConnectionUDP::TestJoinConnection(const nsACString& hostname,
                                           int32_t port) {
  if (mHttp3Session && CanDirectlyActivate()) {
    return mHttp3Session->TestJoinConnection(hostname, port);
  }

  return false;
}

bool HttpConnectionUDP::JoinConnection(const nsACString& hostname,
                                       int32_t port) {
  if (mHttp3Session && CanDirectlyActivate()) {
    return mHttp3Session->JoinConnection(hostname, port);
  }

  return false;
}

bool HttpConnectionUDP::CanReuse() {
#ifdef DEBUG
  if (StaticPrefs::network_http_http3_force_cannot_reuse_for_testing()) {
    return false;
  }
#endif
  if (NS_FAILED(mErrorBeforeConnect)) {
    return false;
  }
  if (mDontReuse) {
    return false;
  }

  if (mHttp3Session) {
    return mHttp3Session->CanReuse();
  }
  return false;
}

bool HttpConnectionUDP::IsConnectedAndUnusable() {
  // mExperienced is set only after the QUIC/TLS handshake completes and a real
  // transaction is served, so it excludes still-handshaking connections (which
  // report !CanReuse() transiently). Combined with !CanReuse() this identifies
  // a connection that was usable and can no longer serve new transactions
  // (either DontReuse'd or its Http3Session went away, e.g. GOAWAY).
  return mExperienced && !CanReuse();
}

bool HttpConnectionUDP::CanDirectlyActivate() {
  // return true if a new transaction can be addded to ths connection at any
  // time through Activate(). In practice this means this is a healthy SPDY
  // connection with room for more concurrent streams.

  if (mHttp3Session) {
    return CanReuse();
  }
  return false;
}

//----------------------------------------------------------------------------
// HttpConnectionUDP::nsAHttpConnection compatible methods
//----------------------------------------------------------------------------

nsresult HttpConnectionUDP::OnHeadersAvailable(nsAHttpTransaction* trans,
                                               nsHttpRequestHead* requestHead,
                                               nsHttpResponseHead* responseHead,
                                               bool* reset) {
  LOG(
      ("HttpConnectionUDP::OnHeadersAvailable [this=%p trans=%p "
       "response-head=%p]\n",
       this, trans, responseHead));

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  NS_ENSURE_ARG_POINTER(trans);
  MOZ_ASSERT(responseHead, "No response head?");

  DebugOnly<nsresult> rv =
      responseHead->SetHeader(nsHttp::X_Firefox_Http3, mAlpnToken);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  uint16_t responseStatus = responseHead->Status();
  nsHttpTransaction* hTrans = trans->QueryHttpTransaction();
  if (mState == HttpConnectionState::SETTING_UP_TUNNEL) {
    HandleTunnelResponse(hTrans, *responseHead, reset);
    return NS_OK;
  }

  // deal with 408 Server Timeouts
  static const PRIntervalTime k1000ms = PR_MillisecondsToInterval(1000);
  if (responseStatus == 408) {
    // If this error could be due to a persistent connection reuse then
    // we pass an error code of NS_ERROR_NET_RESET to
    // trigger the transaction 'restart' mechanism.  We tell it to reset its
    // response headers so that it will be ready to receive the new response.
    if (mIsReused &&
        ((PR_IntervalNow() - mHttp3Session->LastWriteTime()) < k1000ms)) {
      CloseTransaction(mHttp3Session, NS_ERROR_NET_RESET);
      *reset = true;
      return NS_OK;
    }
  }

  return NS_OK;
}

void HttpConnectionUDP::HandleTunnelResponse(
    nsHttpTransaction* aHttpTransaction, const nsHttpResponseHead& responseHead,
    bool* reset) {
  LOG(("HttpConnectionUDP::HandleTunnelResponse mIsInTunnel=%d", mIsInTunnel));
  MOZ_ASSERT(TunnelSetupInProgress());
  MOZ_ASSERT(mIsInTunnel);

  mProxyConnectResponseHead =
      MakeRefPtr<ProxyConnectResponseHead>(responseHead);
  if (responseHead.Status() == 200) {
    ChangeState(HttpConnectionState::REQUEST);
  }

  bool onlyConnect = mTransactionCaps & NS_HTTP_CONNECT_ONLY;
  aHttpTransaction->OnProxyConnectComplete(mProxyConnectResponseHead);
  if (responseHead.Status() == 200) {
    LOG(("proxy CONNECT succeeded! onlyconnect=%d mIsInTunnel=%d\n",
         onlyConnect, mIsInTunnel));
    // If we're only connecting, we don't need to reset the transaction
    // state. We need to upgrade the socket now without doing the actual
    // http request.
    if (!onlyConnect) {
      *reset = true;
    }

    for (const auto& trans : mQueuedConnectUdpTransaction) {
      LOG(("add trans=%p", trans.get()));
      nsCOMPtr<nsIInterfaceRequestor> callbacks = GetCallbacks();
      if (!mHttp3Session->AddStream(trans, trans->Priority(), callbacks)) {
        MOZ_ASSERT(false);  // this cannot happen!
        trans->Close(NS_ERROR_ABORT);
      }
    }
    mQueuedConnectUdpTransaction.Clear();
    mProxyConnectSucceeded = true;
    (void)ResumeSend();
  } else {
    LOG(("proxy CONNECT failed! onlyconnect=%d\n", onlyConnect));
    aHttpTransaction->SetProxyConnectFailed();
    mQueuedConnectUdpTransaction.Clear();
  }
}

void HttpConnectionUDP::ResetTransaction(nsHttpTransaction* aHttpTransaction) {
  LOG(("HttpConnectionUDP::ResetTransaction [this=%p mState=%d]\n", this,
       static_cast<uint32_t>(mState)));

  if (!mAlreadyWildcard) {
    RefPtr<nsHttpConnectionInfo> wildCardProxyCi;
    nsresult rv = mConnInfo->CreateWildCard(getter_AddRefs(wildCardProxyCi));
    if (NS_FAILED(rv)) {
      CloseTransaction(mHttp3Session, rv);
      aHttpTransaction->Close(rv);
      return;
    }
    gHttpHandler->ConnMgr()->MoveToWildCardConnEntry(mConnInfo, wildCardProxyCi,
                                                     this);
    mConnInfo = wildCardProxyCi;
    mAlreadyWildcard = true;
  }

  // Both Http3Session and nsHttpTransaction keeps a strong reference to a
  // ConnectionHandle, which itself holds a strong ref back to the concrete
  // connection, and when the last reference to the handle drops, the underlying
  // connection is released.
  // Notes:
  // 1) SetConnection() may drop the previous
  //    connection reference, which can immediately destroy that handle.
  // Avoid calling it unless we actually need to create/attach a handle.
  // 2) For speculative connections, the transaction may already have a handle.
  //    Reuse it rather than creating a new one.
  // 3) Only set the session's connection if it doesn't already have one.
  if (!mHttp3Session->Connection()) {
    if (!aHttpTransaction->Connection()) {
      RefPtr<ConnectionHandle> handle = new ConnectionHandle(this);
      aHttpTransaction->SetConnection(handle);
    }
    mHttp3Session->SetConnection(aHttpTransaction->Connection());
  }
  aHttpTransaction->SetConnection(nullptr);

  aHttpTransaction->DoNotRemoveAltSvc();
  // This transaction may have NS_HTTP_STICKY_CONNECTION set, so we must call
  // MakeRestartable() explicitly to ensure it can be restarted.
  aHttpTransaction->MakeRestartable();
  aHttpTransaction->Close(NS_ERROR_NET_RESET);
}

bool HttpConnectionUDP::IsReused() { return mIsReused; }

nsresult HttpConnectionUDP::TakeTransport(
    nsISocketTransport** aTransport, nsIAsyncInputStream** aInputStream,
    nsIAsyncOutputStream** aOutputStream) {
  return NS_ERROR_FAILURE;
}

void HttpConnectionUDP::GetTLSSocketControl(nsITLSSocketControl** secinfo) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG(("HttpConnectionUDP::GetTLSSocketControl http3Session=%p\n",
       mHttp3Session.get()));

  if (mHttp3Session &&
      NS_SUCCEEDED(mHttp3Session->GetTransactionTLSSocketControl(secinfo))) {
    return;
  }

  *secinfo = nullptr;
}

nsresult HttpConnectionUDP::PushBack(const char* data, uint32_t length) {
  LOG(("HttpConnectionUDP::PushBack [this=%p, length=%d]\n", this, length));

  return NS_ERROR_UNEXPECTED;
}

class HttpConnectionUDPForceIO : public Runnable, public nsIRunnablePriority {
 public:
  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSIRUNNABLEPRIORITY

  HttpConnectionUDPForceIO(HttpConnectionUDP* aConn, bool doRecv)
      : Runnable("net::HttpConnectionUDPForceIO"),
        mConn(aConn),
        mDoRecv(doRecv) {}

  NS_IMETHOD Run() override {
    MOZ_ASSERT(OnSocketThread(), "not on socket thread");

    if (mDoRecv) {
      return mConn->RecvData();
    }

    MOZ_ASSERT(mConn->mForceSendPending);
    mConn->mForceSendPending = false;

    return mConn->SendData();
  }

 private:
  ~HttpConnectionUDPForceIO() = default;

  RefPtr<HttpConnectionUDP> mConn;
  bool mDoRecv;
};

NS_IMPL_ISUPPORTS_INHERITED(HttpConnectionUDPForceIO, Runnable,
                            nsIRunnablePriority)

NS_IMETHODIMP
HttpConnectionUDPForceIO::GetPriority(uint32_t* aPriority) {
  if (StaticPrefs::network_trr_high_priority_events() && mConn->mConnInfo &&
      mConn->mConnInfo->GetIsTrrServiceChannel()) {
    *aPriority = nsIRunnablePriority::PRIORITY_MEDIUMHIGH;
  } else {
    *aPriority = nsIRunnablePriority::PRIORITY_NORMAL;
  }
  return NS_OK;
}

nsresult HttpConnectionUDP::ResumeSend() {
  LOG(("HttpConnectionUDP::ResumeSend [this=%p]\n", this));
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  RefPtr<HttpConnectionUDP> self(this);
  nsCOMPtr<nsIRunnable> event =
      NS_NewRunnableFunction("HttpConnectionUDP::CallSendData",
                             [self{std::move(self)}]() { self->SendData(); });

  if (StaticPrefs::network_trr_high_priority_events() && mConnInfo &&
      mConnInfo->GetIsTrrServiceChannel()) {
    event = new PrioritizableRunnable(event.forget(),
                                      nsIRunnablePriority::PRIORITY_MEDIUMHIGH);
  }

  DispatchToCurrent(event.forget());
  return NS_OK;
}

nsresult HttpConnectionUDP::ResumeRecv() { return NS_OK; }

void HttpConnectionUDP::ForceSendIO(nsITimer* aTimer, void* aClosure) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  HttpConnectionUDP* self = static_cast<HttpConnectionUDP*>(aClosure);
  MOZ_ASSERT(aTimer == self->mForceSendTimer);
  self->mForceSendTimer = nullptr;
  DispatchToCurrent(do_AddRef(new HttpConnectionUDPForceIO(self, false)));
}

nsresult HttpConnectionUDP::MaybeForceSendIO() {
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
      getter_AddRefs(mForceSendTimer), HttpConnectionUDP::ForceSendIO, this,
      kForceDelay, nsITimer::TYPE_ONE_SHOT,
      "net::HttpConnectionUDP::MaybeForceSendIO"_ns);
}

// trigger an asynchronous read
nsresult HttpConnectionUDP::ForceRecv() {
  LOG(("HttpConnectionUDP::ForceRecv [this=%p]\n", this));
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  return DispatchToCurrent(do_AddRef(new HttpConnectionUDPForceIO(this, true)));
}

// trigger an asynchronous write
nsresult HttpConnectionUDP::ForceSend() {
  LOG(("HttpConnectionUDP::ForceSend [this=%p]\n", this));
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  return MaybeForceSendIO();
}

HttpVersion HttpConnectionUDP::Version() { return HttpVersion::v3_0; }

PRIntervalTime HttpConnectionUDP::LastWriteTime() {
  return mHttp3Session->LastWriteTime();
}

//-----------------------------------------------------------------------------
// HttpConnectionUDP <private>
//-----------------------------------------------------------------------------

void HttpConnectionUDP::CloseTransaction(nsAHttpTransaction* trans,
                                         nsresult reason, bool aIsShutdown) {
  LOG(("HttpConnectionUDP::CloseTransaction[this=%p trans=%p reason=%" PRIx32
       "]\n",
       this, trans, static_cast<uint32_t>(reason)));

  // CloseTransaction may be called by nsHttpTransaction when a fallback
  // is needed. In this case, the transaction is still in mQueuedTransaction
  // and the proxy connect is still in progress.
  bool transInQueue = mQueuedHttpConnectTransaction.Contains(trans) ||
                      mQueuedConnectUdpTransaction.Contains(trans);
  MOZ_ASSERT(trans == mHttp3Session ||
             (transInQueue && IsProxyConnectInProgress()));
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  if (NS_SUCCEEDED(reason) || (reason == NS_BASE_STREAM_CLOSED)) {
    if (aIsShutdown && mHttp3Session) {
      // The underlying socket has been closed. Cancel the Http3Session timer
      // to prevent it from firing on the now-closed socket.
      mHttp3Session->SetCleanShutdown(true);
      mHttp3Session->Close(reason);
    }
    return;
  }

  // The connection and security errors clear out alt-svc mappings
  // in case any previously validated ones are now invalid
  if (((reason == NS_ERROR_NET_RESET) ||
       (NS_ERROR_GET_MODULE(reason) == NS_ERROR_MODULE_SECURITY)) &&
      mConnInfo && !(mTransactionCaps & NS_HTTP_ERROR_SOFTLY)) {
    gHttpHandler->ClearHostMapping(mConnInfo);
  }

  mDontReuse = true;
  if (mHttp3Session) {
    // When proxy connnect failed, we call Http3Session::SetCleanShutdown to
    // force Http3Session to release this UDP connection.
    mHttp3Session->SetCleanShutdown(aIsShutdown || transInQueue ||
                                    (mIsInTunnel && !mProxyConnectSucceeded));
    mHttp3Session->Close(reason);
    if (!mHttp3Session->IsClosed()) {
      // During closing phase we still keep mHttp3Session session,
      // to resend CLOSE_CONNECTION frames.
      return;
    }
  }

  mHttp3Session = nullptr;

  {
    MutexAutoLock lock(mCallbacksLock);
    mCallbacks = nullptr;
  }

  Close(reason, aIsShutdown);

  // flag the connection as reused here for convenience sake. certainly
  // it might be going away instead ;-)
  mIsReused = true;
}

void HttpConnectionUDP::OnQuicTimeoutExpired() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG(("HttpConnectionUDP::OnQuicTimeoutExpired [this=%p]\n", this));
  // if the transaction was dropped...
  if (!mHttp3Session) {
    LOG(("  no transaction; ignoring event\n"));
    return;
  }

  nsresult rv = mHttp3Session->ProcessOutputAndEvents(mSocket);
  if (NS_FAILED(rv)) {
    CloseTransaction(mHttp3Session, rv);
  }
}

//-----------------------------------------------------------------------------
// HttpConnectionUDP::nsISupports
//-----------------------------------------------------------------------------

NS_IMPL_ADDREF(HttpConnectionUDP)
NS_IMPL_RELEASE(HttpConnectionUDP)

NS_INTERFACE_MAP_BEGIN(HttpConnectionUDP)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
  NS_INTERFACE_MAP_ENTRY(nsIUDPSocketSyncListener)
  NS_INTERFACE_MAP_ENTRY(nsIInterfaceRequestor)
  NS_INTERFACE_MAP_ENTRY(HttpConnectionBase)
  NS_INTERFACE_MAP_ENTRY_CONCRETE(HttpConnectionUDP)
NS_INTERFACE_MAP_END

void HttpConnectionUDP::NotifyDataRead() {
  mExperienceState |= ConnectionExperienceState::First_Response_Received;
}

void HttpConnectionUDP::NotifyDataWrite() {
  mExperienceState |= ConnectionExperienceState::First_Request_Sent;
}

// called on the socket transport thread
nsresult HttpConnectionUDP::RecvData() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  // if the transaction was dropped...
  if (!mHttp3Session) {
    LOG(("  no Http3Session; ignoring event\n"));
    return NS_OK;
  }

  nsresult rv = mHttp3Session->RecvData(mSocket);
  LOG(("HttpConnectionUDP::OnInputReady %p rv=%" PRIx32, this,
       static_cast<uint32_t>(rv)));

  if (NS_FAILED(rv)) CloseTransaction(mHttp3Session, rv);

  return NS_OK;
}

// called on the socket transport thread
nsresult HttpConnectionUDP::SendData() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  // if the transaction was dropped...
  if (!mHttp3Session) {
    LOG(("  no Http3Session; ignoring event\n"));
    return NS_OK;
  }

  nsresult rv = mHttp3Session->SendData(mSocket);
  LOG(("HttpConnectionUDP::OnInputReady %p rv=%" PRIx32, this,
       static_cast<uint32_t>(rv)));

  if (NS_FAILED(rv)) CloseTransaction(mHttp3Session, rv);

  return NS_OK;
}

//-----------------------------------------------------------------------------
// HttpConnectionUDP::nsIInterfaceRequestor
//-----------------------------------------------------------------------------

// not called on the socket transport thread
NS_IMETHODIMP
HttpConnectionUDP::GetInterface(const nsIID& iid, void** result) {
  // NOTE: This function is only called on the UI thread via sync proxy from
  //       the socket transport thread.  If that weren't the case, then we'd
  //       have to worry about the possibility of mHttp3Session going away
  //       part-way through this function call.  See CloseTransaction.

  // NOTE - there is a bug here, the call to getinterface is proxied off the
  // nss thread, not the ui thread as the above comment says. So there is
  // indeed a chance of mSession going away. bug 615342

  MOZ_ASSERT(!OnSocketThread(), "on socket thread");

  nsCOMPtr<nsIInterfaceRequestor> callbacks = GetCallbacks();
  if (callbacks) return callbacks->GetInterface(iid, result);
  return NS_ERROR_NO_INTERFACE;
}

void HttpConnectionUDP::SetEvent(nsresult aStatus) {
  switch (aStatus) {
    case NS_NET_STATUS_RESOLVING_HOST:
      mBootstrappedTimings.domainLookupStart = TimeStamp::Now();
      break;
    case NS_NET_STATUS_RESOLVED_HOST:
      mBootstrappedTimings.domainLookupEnd = TimeStamp::Now();
      break;
    case NS_NET_STATUS_CONNECTING_TO:
      mBootstrappedTimings.connectStart = TimeStamp::Now();
      mBootstrappedTimings.secureConnectionStart =
          mBootstrappedTimings.connectStart;
      break;
    case NS_NET_STATUS_CONNECTED_TO:
      mBootstrappedTimings.connectEnd = TimeStamp::Now();
      break;
    default:
      break;
  }
}

bool HttpConnectionUDP::LastTransactionExpectedNoContent() {
  return mLastTransactionExpectedNoContent;
}

void HttpConnectionUDP::SetLastTransactionExpectedNoContent(bool val) {
  mLastTransactionExpectedNoContent = val;
}

bool HttpConnectionUDP::IsPersistent() { return !mDontReuse; }

nsAHttpTransaction* HttpConnectionUDP::Transaction() { return mHttp3Session; }

int64_t HttpConnectionUDP::BytesWritten() {
  if (!mHttp3Session) {
    return 0;
  }
  return mHttp3Session->GetBytesWritten();
}

NS_IMETHODIMP HttpConnectionUDP::OnPacketReceived(nsIUDPSocket* aSocket) {
  RecvData();
  return NS_OK;
}

NS_IMETHODIMP HttpConnectionUDP::OnStopListening(nsIUDPSocket* aSocket,
                                                 nsresult aStatus) {
  // At this point, the UDP socket has already been closed. Set aIsShutdown to
  // true to ensure that mHttp3Session is also closed.
  CloseTransaction(mHttp3Session, aStatus, true);
  return NS_OK;
}

nsresult HttpConnectionUDP::GetSelfAddr(NetAddr* addr) {
  if (mSelfAddr) {
    return mSelfAddr->GetNetAddr(addr);
  }
  return NS_ERROR_FAILURE;
}

nsresult HttpConnectionUDP::GetPeerAddr(NetAddr* addr) {
  if (mPeerAddr) {
    return mPeerAddr->GetNetAddr(addr);
  }
  return NS_ERROR_FAILURE;
}

bool HttpConnectionUDP::ResolvedByTRR() { return mResolvedByTRR; }

nsIRequest::TRRMode HttpConnectionUDP::EffectiveTRRMode() {
  return mEffectiveTRRMode;
}

TRRSkippedReason HttpConnectionUDP::TRRSkipReason() { return mTRRSkipReason; }

Http3Stats HttpConnectionUDP::GetStats() {
  if (!mHttp3Session) {
    return Http3Stats();
  }
  return mHttp3Session->GetStats();
}

void HttpConnectionUDP::SetDontExclude() {
  if (mHttp3Session) {
    mHttp3Session->SetDontExclude();
  }
}

}  // namespace net
}  // namespace mozilla
