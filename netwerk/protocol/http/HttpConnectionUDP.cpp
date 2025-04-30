/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=4 sw=2 sts=2 et cin: */
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
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "HttpConnectionUDP.h"
#include "nsHttpHandler.h"
#include "Http3Session.h"
#include "nsComponentManagerUtils.h"
#include "nsIHttpChannelInternal.h"
#include "nsISocketProvider.h"
#include "nsNetAddr.h"
#include "nsINetAddr.h"

namespace mozilla {
namespace net {

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
  MOZ_ASSERT(mConnInfo->IsHttp3());

  mErrorBeforeConnect = status;
  mAlpnToken = mConnInfo->GetNPNToken();
  if (NS_FAILED(mErrorBeforeConnect)) {
    // See explanation for non-strictness of this operation in
    // SetSecurityCallbacks.
    mCallbacks = new nsMainThreadPtrHolder<nsIInterfaceRequestor>(
        "HttpConnectionUDP::mCallbacks", callbacks, false);
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
  nsresult rv = dnsAddrRecord->GetNextAddr(mConnInfo->GetRoutedHost().IsEmpty()
                                               ? mConnInfo->OriginPort()
                                               : mConnInfo->RoutedPort(),
                                           &peerAddr);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // We are disabling 0.0.0.0 for non-test purposes.
  // See https://github.com/whatwg/fetch/pull/1763 for context.
  if (peerAddr.IsIPAddrAny()) {
    if (StaticPrefs::network_socket_ip_addr_any_disabled()) {
      mozilla::glean::networking::http_ip_addr_any_count
          .Get("blocked_requests"_ns)
          .Add(1);
      LOG(("Connection refused because of 0.0.0.0 IP address\n"));
      return NS_ERROR_CONNECTION_REFUSED;
    }

    mozilla::glean::networking::http_ip_addr_any_count
        .Get("not_blocked_requests"_ns)
        .Add(1);
  }

  mSocket = do_CreateInstance("@mozilla.org/network/udp-socket;1", &rv);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // We need an address here so that we can convey the IP version of the
  // socket.
  NetAddr local;
  local.raw.family = peerAddr.raw.family;
  rv = mSocket->InitWithAddress(&local, nullptr, false, 1);
  if (NS_FAILED(rv)) {
    mSocket = nullptr;
    return rv;
  }

  rv = mSocket->SetRecvBufferSize(
      StaticPrefs::network_http_http3_recvBufferSize());
  if (NS_FAILED(rv)) {
    LOG(("HttpConnectionUDP::Init SetRecvBufferSize failed %d [this=%p]",
         static_cast<uint32_t>(rv), this));
    mSocket->Close();
    mSocket = nullptr;
    return rv;
  }

  if (peerAddr.raw.family == AF_INET) {
    rv = mSocket->SetDontFragment(true);
    if (NS_FAILED(rv)) {
      LOG(("HttpConnectionUDP::Init SetDontFragment failed %d [this=%p]",
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

  mPeerAddr = new nsNetAddr(&peerAddr);
  mHttp3Session = new Http3Session();
  rv = mHttp3Session->Init(mConnInfo, mSelfAddr, mPeerAddr, this, providerFlags,
                           callbacks, mSocket);
  if (NS_FAILED(rv)) {
    LOG(
        ("HttpConnectionUDP::Init mHttp3Session->Init failed "
         "[this=%p rv=%x]\n",
         this, static_cast<uint32_t>(rv)));
    mSocket->Close();
    mSocket = nullptr;
    mHttp3Session = nullptr;
    return rv;
  }

  ChangeConnectionState(ConnectionState::INITED);
  // See explanation for non-strictness of this operation in
  // SetSecurityCallbacks.
  mCallbacks = new nsMainThreadPtrHolder<nsIInterfaceRequestor>(
      "HttpConnectionUDP::mCallbacks", callbacks, false);

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

  if (!mExperienced && !trans->IsNullTransaction()) {
    mHasFirstHttpTransaction = true;
    // For QUIC we have HttpConnecitonUDP before the actual connection
    // has been establish so wait for TLS handshake to be finished before
    // we mark the connection 'experienced'.
    if (!mExperienced && mHttp3Session && mHttp3Session->IsConnected()) {
      mExperienced = true;
    }
    if (mBootstrappedTimingsSet) {
      mBootstrappedTimingsSet = false;
      nsHttpTransaction* hTrans = trans->QueryHttpTransaction();
      if (hTrans) {
        hTrans->BootstrapTimings(mBootstrappedTimings);
      }
    }
    mBootstrappedTimings = TimingStruct();
  }

  mTransactionCaps = caps;
  mPriority = pri;

  NS_ENSURE_ARG_POINTER(trans);

  // Connection failures are Activated() just like regular transacions.
  // If we don't have a confirmation of a connected socket then test it
  // with a write() to get relevant error code.
  if (NS_FAILED(mErrorBeforeConnect)) {
    CloseTransaction(nullptr, mErrorBeforeConnect);
    trans->Close(mErrorBeforeConnect);
    gHttpHandler->ExcludeHttp3(mConnInfo);
    return mErrorBeforeConnect;
  }

  if (!mHttp3Session->AddStream(trans, pri, mCallbacks)) {
    MOZ_ASSERT(false);  // this cannot happen!
    trans->Close(NS_ERROR_ABORT);
    return NS_ERROR_FAILURE;
  }

  if (mHasFirstHttpTransaction && mExperienced) {
    mHasFirstHttpTransaction = false;
    mExperienceState |= ConnectionExperienceState::Experienced;
  }

  Unused << ResumeSend();
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
  if (mSocket) {
    mSocket->Close();
    mSocket = nullptr;
  }
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

  // deal with 408 Server Timeouts
  uint16_t responseStatus = responseHead->Status();
  static const PRIntervalTime k1000ms = PR_MillisecondsToInterval(1000);
  if (responseStatus == 408) {
    // If this error could be due to a persistent connection reuse then
    // we pass an error code of NS_ERROR_NET_RESET to
    // trigger the transaction 'restart' mechanism.  We tell it to reset its
    // response headers so that it will be ready to receive the new response.
    if (mIsReused &&
        ((PR_IntervalNow() - mHttp3Session->LastWriteTime()) < k1000ms)) {
      Close(NS_ERROR_NET_RESET);
      *reset = true;
      return NS_OK;
    }
  }

  return NS_OK;
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

class HttpConnectionUDPForceIO : public Runnable {
 public:
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
  RefPtr<HttpConnectionUDP> mConn;
  bool mDoRecv;
};

nsresult HttpConnectionUDP::ResumeSend() {
  LOG(("HttpConnectionUDP::ResumeSend [this=%p]\n", this));
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  RefPtr<HttpConnectionUDP> self(this);
  NS_DispatchToCurrentThread(
      NS_NewRunnableFunction("HttpConnectionUDP::CallSendData",
                             [self{std::move(self)}]() { self->SendData(); }));
  return NS_OK;
}

nsresult HttpConnectionUDP::ResumeRecv() { return NS_OK; }

void HttpConnectionUDP::ForceSendIO(nsITimer* aTimer, void* aClosure) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  HttpConnectionUDP* self = static_cast<HttpConnectionUDP*>(aClosure);
  MOZ_ASSERT(aTimer == self->mForceSendTimer);
  self->mForceSendTimer = nullptr;
  NS_DispatchToCurrentThread(new HttpConnectionUDPForceIO(self, false));
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
      "net::HttpConnectionUDP::MaybeForceSendIO");
}

// trigger an asynchronous read
nsresult HttpConnectionUDP::ForceRecv() {
  LOG(("HttpConnectionUDP::ForceRecv [this=%p]\n", this));
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  return NS_DispatchToCurrentThread(new HttpConnectionUDPForceIO(this, true));
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

  MOZ_ASSERT(trans == mHttp3Session);
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  if (NS_SUCCEEDED(reason) || (reason == NS_BASE_STREAM_CLOSED)) {
    MOZ_ASSERT(false);
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
    mHttp3Session->SetCleanShutdown(aIsShutdown);
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

  nsCOMPtr<nsIInterfaceRequestor> callbacks;
  {
    MutexAutoLock lock(mCallbacksLock);
    callbacks = mCallbacks;
  }
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
      break;
    case NS_NET_STATUS_CONNECTED_TO:
      mBootstrappedTimings.connectEnd = TimeStamp::Now();
      break;
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

bool HttpConnectionUDP::IsProxyConnectInProgress() { return false; }

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
  CloseTransaction(mHttp3Session, aStatus);
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

}  // namespace net
}  // namespace mozilla
