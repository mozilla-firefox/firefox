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

#include "Http2Session.h"
#include "Http2StreamTunnel.h"
#include "mozilla/glean/NetwerkProtocolHttpMetrics.h"
#include "nsHttpConnectionInfo.h"
#include "nsHttpConnectionMgr.h"
#include "nsHttpHandler.h"
#include "nsProxyRelease.h"
#include "nsQueryObject.h"

namespace mozilla::net {

NS_IMPL_ADDREF_INHERITED(Http2StreamTunnel, Http2StreamBase)
NS_IMPL_RELEASE_INHERITED(Http2StreamTunnel, Http2StreamBase)

NS_INTERFACE_MAP_BEGIN(Http2StreamTunnel)
  NS_INTERFACE_MAP_ENTRY(nsITransport)
  NS_INTERFACE_MAP_ENTRY_CONCRETE(Http2StreamTunnel)
  NS_INTERFACE_MAP_ENTRY(nsITransport)
  NS_INTERFACE_MAP_ENTRY(nsISocketTransport)
NS_INTERFACE_MAP_END

Http2StreamTunnel::Http2StreamTunnel(Http2Session* session, int32_t priority,
                                     uint64_t bcId,
                                     nsHttpConnectionInfo* aConnectionInfo)
    : Http2StreamBase(0, session, priority, bcId),
      mConnectionInfo(aConnectionInfo) {}

Http2StreamTunnel::~Http2StreamTunnel() { ClearTransactionsBlockedOnTunnel(); }

void Http2StreamTunnel::HandleResponseHeaders(nsACString& aHeadersOut,
                                              int32_t httpResponseCode) {}

// TODO We do not need this. Fix in bug 1772212.
void Http2StreamTunnel::ClearTransactionsBlockedOnTunnel() {
  nsresult rv = gHttpHandler->ConnMgr()->ProcessPendingQ(mConnectionInfo);
  if (NS_FAILED(rv)) {
    LOG3(
        ("Http2StreamTunnel::ClearTransactionsBlockedOnTunnel %p\n"
         "  ProcessPendingQ failed: %" PRIX32,
         this, static_cast<uint32_t>(rv)));
  }
}

NS_IMETHODIMP
Http2StreamTunnel::SetKeepaliveEnabled(bool aKeepaliveEnabled) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
Http2StreamTunnel::SetKeepaliveVals(int32_t keepaliveIdleTime,
                                    int32_t keepaliveRetryInterval) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
Http2StreamTunnel::GetSecurityCallbacks(
    nsIInterfaceRequestor** aSecurityCallbacks) {
  return mSocketTransport->GetSecurityCallbacks(aSecurityCallbacks);
}

NS_IMETHODIMP
Http2StreamTunnel::SetSecurityCallbacks(
    nsIInterfaceRequestor* aSecurityCallbacks) {
  return NS_OK;
}

NS_IMETHODIMP
Http2StreamTunnel::OpenInputStream(uint32_t aFlags, uint32_t aSegmentSize,
                                   uint32_t aSegmentCount,
                                   nsIInputStream** _retval) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
Http2StreamTunnel::OpenOutputStream(uint32_t aFlags, uint32_t aSegmentSize,
                                    uint32_t aSegmentCount,
                                    nsIOutputStream** _retval) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

void Http2StreamTunnel::CloseStream(nsresult aReason) {
  LOG(("Http2StreamTunnel::CloseStream this=%p", this));
  RefPtr<Http2Session> session = Session();
  if (NS_SUCCEEDED(mCondition)) {
    mSession = nullptr;
    // Let the session pickup that the stream has been closed.
    mCondition = aReason;
    if (NS_SUCCEEDED(aReason)) {
      aReason = NS_BASE_STREAM_CLOSED;
    }
    mOutput->OnSocketReady(aReason);
    mInput->OnSocketReady(aReason);
  }
  mClosed = true;
}

NS_IMETHODIMP
Http2StreamTunnel::Close(nsresult aReason) {
  LOG(("Http2StreamTunnel::Close this=%p", this));
  RefPtr<Http2Session> session = Session();
  if (NS_SUCCEEDED(mCondition)) {
    if (NS_SUCCEEDED(aReason)) {
      aReason = NS_BASE_STREAM_CLOSED;
    }
    mOutput->CloseWithStatus(aReason);
    mInput->CloseWithStatus(aReason);
    // Let the session pickup that the stream has been closed.
    mCondition = aReason;
    // Clear the session in the end to make sure that CleanupStream() can be
    // called in CloseWithStatus().
    mSession = nullptr;
  }
  return NS_OK;
}

NS_IMETHODIMP
Http2StreamTunnel::SetEventSink(nsITransportEventSink* aSink,
                                nsIEventTarget* aEventTarget) {
  return NS_OK;
}

NS_IMETHODIMP
Http2StreamTunnel::Bind(NetAddr* aLocalAddr) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
Http2StreamTunnel::GetEchConfigUsed(bool* aEchConfigUsed) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
Http2StreamTunnel::SetEchConfig(const nsACString& aEchConfig) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
Http2StreamTunnel::ResolvedByTRR(bool* aResolvedByTRR) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP Http2StreamTunnel::GetEffectiveTRRMode(
    nsIRequest::TRRMode* aEffectiveTRRMode) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP Http2StreamTunnel::GetTrrSkipReason(
    nsITRRSkipReason::value* aTrrSkipReason) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
Http2StreamTunnel::IsAlive(bool* aAlive) {
  RefPtr<Http2Session> session = Session();
  if (mSocketTransport && session) {
    return mSocketTransport->IsAlive(aAlive);
  }
  *aAlive = false;
  return NS_OK;
}

#define FWD_TS_T_PTR(fx, ts) \
  NS_IMETHODIMP              \
  Http2StreamTunnel::fx(ts* arg) { return mSocketTransport->fx(arg); }

#define FWD_TS_T_ADDREF(fx, ts) \
  NS_IMETHODIMP                 \
  Http2StreamTunnel::fx(ts** arg) { return mSocketTransport->fx(arg); }

#define FWD_TS_T(fx, ts) \
  NS_IMETHODIMP          \
  Http2StreamTunnel::fx(ts arg) { return mSocketTransport->fx(arg); }

FWD_TS_T_PTR(GetKeepaliveEnabled, bool);
FWD_TS_T_PTR(GetSendBufferSize, uint32_t);
FWD_TS_T(SetSendBufferSize, uint32_t);
FWD_TS_T_PTR(GetPort, int32_t);
FWD_TS_T_PTR(GetPeerAddr, mozilla::net::NetAddr);
FWD_TS_T_PTR(GetSelfAddr, mozilla::net::NetAddr);
FWD_TS_T_ADDREF(GetScriptablePeerAddr, nsINetAddr);
FWD_TS_T_ADDREF(GetScriptableSelfAddr, nsINetAddr);
FWD_TS_T_ADDREF(GetTlsSocketControl, nsITLSSocketControl);
FWD_TS_T_PTR(GetConnectionFlags, uint32_t);
FWD_TS_T(SetConnectionFlags, uint32_t);
FWD_TS_T(SetIsTRRConnection, bool);
FWD_TS_T_PTR(GetIsTRRConnection, bool);
FWD_TS_T_PTR(GetTlsFlags, uint32_t);
FWD_TS_T(SetTlsFlags, uint32_t);
FWD_TS_T_PTR(GetRecvBufferSize, uint32_t);
FWD_TS_T(SetRecvBufferSize, uint32_t);
FWD_TS_T_PTR(GetResetIPFamilyPreference, bool);

nsresult Http2StreamTunnel::GetOriginAttributes(
    mozilla::OriginAttributes* aOriginAttributes) {
  return mSocketTransport->GetOriginAttributes(aOriginAttributes);
}

nsresult Http2StreamTunnel::SetOriginAttributes(
    const mozilla::OriginAttributes& aOriginAttributes) {
  return mSocketTransport->SetOriginAttributes(aOriginAttributes);
}

NS_IMETHODIMP
Http2StreamTunnel::GetScriptableOriginAttributes(
    JSContext* aCx, JS::MutableHandle<JS::Value> aOriginAttributes) {
  return mSocketTransport->GetScriptableOriginAttributes(aCx,
                                                         aOriginAttributes);
}

NS_IMETHODIMP
Http2StreamTunnel::SetScriptableOriginAttributes(
    JSContext* aCx, JS::Handle<JS::Value> aOriginAttributes) {
  return mSocketTransport->SetScriptableOriginAttributes(aCx,
                                                         aOriginAttributes);
}

NS_IMETHODIMP
Http2StreamTunnel::GetHost(nsACString& aHost) {
  return mSocketTransport->GetHost(aHost);
}

NS_IMETHODIMP
Http2StreamTunnel::GetTimeout(uint32_t aType, uint32_t* _retval) {
  return mSocketTransport->GetTimeout(aType, _retval);
}

NS_IMETHODIMP
Http2StreamTunnel::SetTimeout(uint32_t aType, uint32_t aValue) {
  return mSocketTransport->SetTimeout(aType, aValue);
}

NS_IMETHODIMP
Http2StreamTunnel::SetReuseAddrPort(bool aReuseAddrPort) {
  return mSocketTransport->SetReuseAddrPort(aReuseAddrPort);
}

NS_IMETHODIMP
Http2StreamTunnel::SetLinger(bool aPolarity, int16_t aTimeout) {
  return mSocketTransport->SetLinger(aPolarity, aTimeout);
}

NS_IMETHODIMP
Http2StreamTunnel::GetQoSBits(uint8_t* aQoSBits) {
  return mSocketTransport->GetQoSBits(aQoSBits);
}

NS_IMETHODIMP
Http2StreamTunnel::SetQoSBits(uint8_t aQoSBits) {
  return mSocketTransport->SetQoSBits(aQoSBits);
}

NS_IMETHODIMP
Http2StreamTunnel::GetRetryDnsIfPossible(bool* aRetry) {
  return mSocketTransport->GetRetryDnsIfPossible(aRetry);
}

NS_IMETHODIMP
Http2StreamTunnel::GetStatus(nsresult* aStatus) {
  return mSocketTransport->GetStatus(aStatus);
}

already_AddRefed<nsHttpConnection> Http2StreamTunnel::CreateHttpConnection(
    nsAHttpTransaction* httpTransaction, nsIInterfaceRequestor* aCallbacks,
    PRIntervalTime aRtt, bool aIsExtendedCONNECT) {
  mInput = new InputStreamTunnel(this);
  mOutput = new OutputStreamTunnel(this);
  RefPtr<nsHttpConnection> conn = new nsHttpConnection();

  conn->SetTransactionCaps(httpTransaction->Caps());
  nsresult rv =
      conn->Init(httpTransaction->ConnectionInfo(),
                 gHttpHandler->ConnMgr()->MaxRequestDelay(), this, mInput,
                 mOutput, true, NS_OK, aCallbacks, aRtt, aIsExtendedCONNECT);
  MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv));
  mTransaction = httpTransaction;
  return conn.forget();
}

nsresult Http2StreamTunnel::CallToReadData(uint32_t count,
                                           uint32_t* countRead) {
  LOG(("Http2StreamTunnel::CallToReadData this=%p", this));
  return mOutput->OnSocketReady(NS_OK);
}

nsresult Http2StreamTunnel::CallToWriteData(uint32_t count,
                                            uint32_t* countWritten) {
  LOG(("Http2StreamTunnel::CallToWriteData this=%p", this));
  if (!mInput->HasCallback()) {
    return NS_BASE_STREAM_WOULD_BLOCK;
  }
  return mInput->OnSocketReady(NS_OK);
}

nsresult Http2StreamTunnel::GenerateHeaders(nsCString& aCompressedData,
                                            uint8_t& firstFrameFlags) {
  nsAutoCString authorityHeader;
  authorityHeader = mConnectionInfo->GetOrigin();
  authorityHeader.Append(':');
  authorityHeader.AppendInt(mConnectionInfo->OriginPort());

  RefPtr<Http2Session> session = Session();

  LOG3(("Http2StreamTunnel %p Stream ID 0x%X [session=%p] for %s\n", this,
        mStreamID, session.get(), authorityHeader.get()));

  mRequestBodyLenRemaining = 0x0fffffffffffffffULL;

  nsresult rv = session->Compressor()->EncodeHeaderBlock(
      mFlatHttpRequestHeaders, "CONNECT"_ns, EmptyCString(), authorityHeader,
      EmptyCString(), EmptyCString(), true, aCompressedData, true);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

OutputStreamTunnel::OutputStreamTunnel(Http2StreamTunnel* aStream)
    : mWeakStream(aStream) {}

OutputStreamTunnel::~OutputStreamTunnel() = default;

nsresult OutputStreamTunnel::OnSocketReady(nsresult condition) {
  LOG(("OutputStreamTunnel::OnSocketReady [this=%p cond=%" PRIx32
       " callback=%p]\n",
       this, static_cast<uint32_t>(condition), mCallback.get()));

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  nsCOMPtr<nsIOutputStreamCallback> callback;

  // update condition, but be careful not to erase an already
  // existing error condition.
  if (NS_SUCCEEDED(mCondition)) {
    mCondition = condition;
  }
  callback = std::move(mCallback);

  nsresult rv = NS_OK;
  if (callback) {
    rv = callback->OnOutputStreamReady(this);
  }

  return rv;
}

NS_IMPL_ISUPPORTS(OutputStreamTunnel, nsIOutputStream, nsIAsyncOutputStream)

NS_IMETHODIMP
OutputStreamTunnel::Close() { return CloseWithStatus(NS_BASE_STREAM_CLOSED); }

NS_IMETHODIMP
OutputStreamTunnel::Flush() { return NS_OK; }

NS_IMETHODIMP
OutputStreamTunnel::StreamStatus() { return mCondition; }

NS_IMETHODIMP
OutputStreamTunnel::Write(const char* buf, uint32_t count,
                          uint32_t* countWritten) {
  LOG(("OutputStreamTunnel::Write [this=%p count=%u]\n", this, count));

  *countWritten = 0;
  if (NS_FAILED(mCondition)) {
    return mCondition;
  }

  RefPtr<Http2StreamTunnel> tunnel;
  nsresult rv = GetStream(getter_AddRefs(tunnel));
  if (NS_FAILED(rv)) {
    return rv;
  }

  return tunnel->OnReadSegment(buf, count, countWritten);
}

NS_IMETHODIMP
OutputStreamTunnel::WriteSegments(nsReadSegmentFun reader, void* closure,
                                  uint32_t count, uint32_t* countRead) {
  // stream is unbuffered
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
OutputStreamTunnel::WriteFrom(nsIInputStream* stream, uint32_t count,
                              uint32_t* countRead) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
OutputStreamTunnel::IsNonBlocking(bool* nonblocking) {
  *nonblocking = true;
  return NS_OK;
}

NS_IMETHODIMP
OutputStreamTunnel::CloseWithStatus(nsresult reason) {
  LOG(("OutputStreamTunnel::CloseWithStatus [this=%p reason=%" PRIx32 "]\n",
       this, static_cast<uint32_t>(reason)));
  mCondition = reason;

  RefPtr<Http2StreamTunnel> tunnel = mWeakStream.get();
  mWeakStream = nullptr;
  if (!tunnel) {
    return NS_OK;
  }
  RefPtr<Http2Session> session = tunnel->Session();
  if (!session) {
    return NS_OK;
  }
  session->CleanupStream(tunnel, reason, Http2Session::CANCEL_ERROR);
  return NS_OK;
}

NS_IMETHODIMP
OutputStreamTunnel::AsyncWait(nsIOutputStreamCallback* callback, uint32_t flags,
                              uint32_t amount, nsIEventTarget* target) {
  LOG(("OutputStreamTunnel::AsyncWait [this=%p]\n", this));

  // The following parametr are not used:
  (void)flags;
  (void)amount;
  (void)target;

  RefPtr<OutputStreamTunnel> self(this);
  if (NS_FAILED(mCondition)) {
    (void)DispatchToCurrent(NS_NewRunnableFunction(
        "OutputStreamTunnel::CallOnSocketReady",
        [self{std::move(self)}]() { self->OnSocketReady(NS_OK); }));
  } else if (callback) {
    // Inform the proxy connection that the inner connetion wants to
    // read data.
    RefPtr<Http2StreamTunnel> tunnel;
    nsresult rv = GetStream(getter_AddRefs(tunnel));
    if (NS_FAILED(rv)) {
      return rv;
    }
    RefPtr<Http2Session> session;
    rv = GetSession(getter_AddRefs(session));
    if (NS_FAILED(rv)) {
      return rv;
    }
    session->TransactionHasDataToWrite(tunnel);
  }

  mCallback = callback;
  return NS_OK;
}

InputStreamTunnel::InputStreamTunnel(Http2StreamTunnel* aStream)
    : mWeakStream(aStream) {}

InputStreamTunnel::~InputStreamTunnel() = default;

nsresult InputStreamTunnel::OnSocketReady(nsresult condition) {
  LOG(("InputStreamTunnel::OnSocketReady [this=%p cond=%" PRIx32 "]\n", this,
       static_cast<uint32_t>(condition)));

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  nsCOMPtr<nsIInputStreamCallback> callback;

  // update condition, but be careful not to erase an already
  // existing error condition.
  if (NS_SUCCEEDED(mCondition)) {
    mCondition = condition;
  }
  callback = std::move(mCallback);

  return callback ? callback->OnInputStreamReady(this) : NS_OK;
}

NS_IMPL_ISUPPORTS(InputStreamTunnel, nsIInputStream, nsIAsyncInputStream)

NS_IMETHODIMP
InputStreamTunnel::Close() { return CloseWithStatus(NS_BASE_STREAM_CLOSED); }

NS_IMETHODIMP
InputStreamTunnel::Available(uint64_t* avail) {
  LOG(("InputStreamTunnel::Available [this=%p]\n", this));

  if (NS_FAILED(mCondition)) {
    return mCondition;
  }

  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
InputStreamTunnel::StreamStatus() {
  LOG(("InputStreamTunnel::StreamStatus [this=%p]\n", this));

  return mCondition;
}

NS_IMETHODIMP
InputStreamTunnel::Read(char* buf, uint32_t count, uint32_t* countRead) {
  LOG(("InputStreamTunnel::Read [this=%p count=%u]\n", this, count));

  *countRead = 0;

  if (NS_FAILED(mCondition)) {
    return mCondition;
  }

  RefPtr<Http2StreamTunnel> tunnel;
  nsresult rv = GetStream(getter_AddRefs(tunnel));
  if (NS_FAILED(rv)) {
    return rv;
  }

  return tunnel->OnWriteSegment(buf, count, countRead);
}

NS_IMETHODIMP
InputStreamTunnel::ReadSegments(nsWriteSegmentFun writer, void* closure,
                                uint32_t count, uint32_t* countRead) {
  // socket stream is unbuffered
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
InputStreamTunnel::IsNonBlocking(bool* nonblocking) {
  *nonblocking = true;
  return NS_OK;
}

NS_IMETHODIMP
InputStreamTunnel::CloseWithStatus(nsresult reason) {
  LOG(("InputStreamTunnel::CloseWithStatus [this=%p reason=%" PRIx32 "]\n",
       this, static_cast<uint32_t>(reason)));
  mCondition = reason;

  RefPtr<Http2StreamTunnel> tunnel = mWeakStream.get();
  mWeakStream = nullptr;
  if (!tunnel) {
    return NS_OK;
  }
  RefPtr<Http2Session> session = tunnel->Session();
  if (!session) {
    return NS_OK;
  }
  session->CleanupStream(tunnel, reason, Http2Session::CANCEL_ERROR);
  return NS_OK;
}

NS_IMETHODIMP
InputStreamTunnel::AsyncWait(nsIInputStreamCallback* callback, uint32_t flags,
                             uint32_t amount, nsIEventTarget* target) {
  LOG(("InputStreamTunnel::AsyncWait [this=%p mCondition=%x]\n", this,
       static_cast<uint32_t>(mCondition)));

  // The following parametr are not used:
  (void)flags;
  (void)amount;
  (void)target;

  RefPtr<InputStreamTunnel> self(this);
  if (NS_FAILED(mCondition)) {
    (void)DispatchToCurrent(NS_NewRunnableFunction(
        "InputStreamTunnel::CallOnSocketReady",
        [self{std::move(self)}]() { self->OnSocketReady(NS_OK); }));
  } else if (callback) {
    // Inform the proxy connection that the inner connetion wants to
    // read data.
    RefPtr<Http2StreamTunnel> tunnel;
    nsresult rv = GetStream(getter_AddRefs(tunnel));
    if (NS_FAILED(rv)) {
      return rv;
    }
    RefPtr<Http2Session> session;
    rv = GetSession(getter_AddRefs(session));
    if (NS_FAILED(rv)) {
      return rv;
    }
    if (tunnel->DataBuffered()) {
      session->TransactionHasDataToRecv(tunnel);
    }
  }

  mCallback = callback;
  return NS_OK;
}

nsresult OutputStreamTunnel::GetStream(Http2StreamTunnel** aStream) {
  RefPtr<Http2StreamTunnel> tunnel = mWeakStream.get();
  MOZ_ASSERT(tunnel);
  if (!tunnel) {
    return NS_ERROR_UNEXPECTED;
  }

  tunnel.forget(aStream);
  return NS_OK;
}

nsresult OutputStreamTunnel::GetSession(Http2Session** aSession) {
  RefPtr<Http2StreamTunnel> tunnel;
  nsresult rv = GetStream(getter_AddRefs(tunnel));
  if (NS_FAILED(rv)) {
    return rv;
  }
  RefPtr<Http2Session> session = tunnel->Session();
  MOZ_ASSERT(session);
  if (!session) {
    return NS_ERROR_UNEXPECTED;
  }

  session.forget(aSession);
  return NS_OK;
}

nsresult InputStreamTunnel::GetStream(Http2StreamTunnel** aStream) {
  RefPtr<Http2StreamTunnel> tunnel = mWeakStream.get();
  MOZ_ASSERT(tunnel);
  if (!tunnel) {
    return NS_ERROR_UNEXPECTED;
  }

  tunnel.forget(aStream);
  return NS_OK;
}

nsresult InputStreamTunnel::GetSession(Http2Session** aSession) {
  RefPtr<Http2StreamTunnel> tunnel;
  nsresult rv = GetStream(getter_AddRefs(tunnel));
  if (NS_FAILED(rv)) {
    return rv;
  }
  RefPtr<Http2Session> session = tunnel->Session();
  if (!session) {
    return NS_ERROR_UNEXPECTED;
  }

  session.forget(aSession);
  return NS_OK;
}

Http2StreamWebSocket::Http2StreamWebSocket(
    Http2Session* session, int32_t priority, uint64_t bcId,
    nsHttpConnectionInfo* aConnectionInfo)
    : Http2StreamTunnel(session, priority, bcId, aConnectionInfo) {
  LOG(("Http2StreamWebSocket ctor:%p", this));
}

Http2StreamWebSocket::~Http2StreamWebSocket() {
  LOG(("Http2StreamWebSocket dtor:%p", this));
}

nsresult Http2StreamWebSocket::GenerateHeaders(nsCString& aCompressedData,
                                               uint8_t& firstFrameFlags) {
  const nsHttpRequestHead* head = mTransaction->RequestHead();

  nsAutoCString authorityHeader;
  nsresult rv = head->GetHeader(nsHttp::Host, authorityHeader);
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<Http2Session> session = Session();
  LOG3(("Http2StreamWebSocket %p Stream ID 0x%X [session=%p] for %s\n", this,
        mStreamID, session.get(), authorityHeader.get()));

  nsDependentCString scheme(head->IsHTTPS() ? "https" : "http");
  nsAutoCString path;
  head->Path(path);

  rv = session->Compressor()->EncodeHeaderBlock(
      mFlatHttpRequestHeaders, "CONNECT"_ns, path, authorityHeader, scheme,
      "websocket"_ns, false, aCompressedData, true);
  NS_ENSURE_SUCCESS(rv, rv);

  mRequestBodyLenRemaining = 0x0fffffffffffffffULL;

  return NS_OK;
}

void Http2StreamWebSocket::CloseStream(nsresult aReason) {
  LOG(("Http2StreamWebSocket::CloseStream this=%p aReason=%x", this,
       static_cast<uint32_t>(aReason)));
  if (mTransaction) {
    mTransaction->Close(aReason);
    mTransaction = nullptr;
  }
  Http2StreamTunnel::CloseStream(aReason);
}

}  // namespace mozilla::net
