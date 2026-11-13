/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "Http3StreamTunnel.h"

#include "Http3Session.h"
#include "HttpLog.h"
#include "nsHttpConnectionMgr.h"
#include "nsHttpHandler.h"
#include "nsQueryObject.h"

namespace mozilla::net {

//-----------------------------------------------------------------------------
// Http3TransportLayer::InputStreamTunnel impl
//-----------------------------------------------------------------------------

NS_IMPL_QUERY_INTERFACE(Http3TransportLayer::InputStreamTunnel, nsIInputStream,
                        nsIAsyncInputStream)

NS_IMETHODIMP_(MozExternalRefCountType)
Http3TransportLayer::InputStreamTunnel::AddRef() {
  return mTransport->AddRef();
}

NS_IMETHODIMP_(MozExternalRefCountType)
Http3TransportLayer::InputStreamTunnel::Release() {
  return mTransport->Release();
}

Http3TransportLayer::InputStreamTunnel::InputStreamTunnel(
    Http3TransportLayer* aTransport)
    : mTransport(aTransport) {}

NS_IMETHODIMP
Http3TransportLayer::InputStreamTunnel::Close() {
  LOG(("Http3TransportLayer::InputStreamTunnel::Close [this=%p]\n", this));
  return CloseWithStatus(NS_BASE_STREAM_CLOSED);
}

NS_IMETHODIMP Http3TransportLayer::InputStreamTunnel::Available(
    uint64_t* avail) {
  LOG(("Http3TransportLayer::InputStreamTunnel::Available [this=%p]\n", this));

  if (NS_FAILED(mCondition)) {
    return mCondition;
  }

  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP Http3TransportLayer::InputStreamTunnel::StreamStatus() {
  LOG(("Http3TransportLayer::InputStreamTunnel::StreamStatus [this=%p]\n",
       this));
  return mCondition;
}

NS_IMETHODIMP
Http3TransportLayer::InputStreamTunnel::Read(char* buf, uint32_t count,
                                             uint32_t* countRead) {
  LOG(("Http3TransportLayer::InputStreamTunnel::Read [this=%p]\n", this));

  *countRead = 0;

  if (NS_FAILED(mCondition)) {
    return mCondition;
  }

  RefPtr<Http3StreamTunnel> tunnel = mTransport->GetStream();
  if (!tunnel) {
    return NS_ERROR_UNEXPECTED;
  }

  return tunnel->OnWriteSegment(buf, count, countRead);
}

NS_IMETHODIMP
Http3TransportLayer::InputStreamTunnel::ReadSegments(nsWriteSegmentFun writer,
                                                     void* closure,
                                                     uint32_t count,
                                                     uint32_t* countRead) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
Http3TransportLayer::InputStreamTunnel::IsNonBlocking(bool* nonblocking) {
  *nonblocking = true;
  return NS_OK;
}

NS_IMETHODIMP
Http3TransportLayer::InputStreamTunnel::CloseWithStatus(nsresult reason) {
  LOG(
      ("Http3TransportLayer::InputStreamTunnel::CloseWithStatus [this=%p "
       "reason=%" PRIx32 "]\n",
       this, static_cast<uint32_t>(reason)));
  mCondition = reason;

  RefPtr<Http3StreamTunnel> tunnel = mTransport->GetStream();
  if (!tunnel) {
    return NS_OK;
  }

  tunnel->CleanupStream(reason);
  return NS_OK;
}

nsresult Http3TransportLayer::InputStreamTunnel::OnSocketReady(
    nsresult condition) {
  LOG(("InputStreamTunnel::OnSocketReady [this=%p cond=%" PRIx32 "]\n", this,
       static_cast<uint32_t>(condition)));

  nsCOMPtr<nsIInputStreamCallback> callback;

  // update condition, but be careful not to erase an already
  // existing error condition.
  if (NS_SUCCEEDED(mCondition)) {
    mCondition = condition;
  }
  callback = std::move(mCallback);

  return callback ? callback->OnInputStreamReady(this) : NS_OK;
}

NS_IMETHODIMP
Http3TransportLayer::InputStreamTunnel::AsyncWait(
    nsIInputStreamCallback* callback, uint32_t flags, uint32_t amount,
    nsIEventTarget* target) {
  LOG(
      ("Http3TransportLayer::InputStreamTunnel::AsyncWait [this=%p, "
       "callback=%p]\n",
       this, callback));
  // The following parameters are not used:
  (void)flags;
  (void)amount;
  (void)target;

  RefPtr<InputStreamTunnel> self(this);
  if (NS_FAILED(mCondition)) {
    (void)DispatchToCurrent(NS_NewRunnableFunction(
        "InputStreamTunnel::CallOnSocketReady",
        [self{std::move(self)}]() { self->OnSocketReady(self->mCondition); }));
  } else if (callback) {
    RefPtr<Http3StreamTunnel> tunnel = mTransport->GetStream();
    if (!tunnel) {
      return NS_ERROR_UNEXPECTED;
    }
    tunnel->HasDataToRead();
  }

  mCallback = callback;
  return NS_OK;
}

//-----------------------------------------------------------------------------
// Http3TransportLayer::OutputStreamTunnel impl
//-----------------------------------------------------------------------------

NS_IMPL_QUERY_INTERFACE(Http3TransportLayer::OutputStreamTunnel,
                        nsIOutputStream, nsIAsyncOutputStream)

NS_IMETHODIMP_(MozExternalRefCountType)
Http3TransportLayer::OutputStreamTunnel::AddRef() {
  return mTransport->AddRef();
}

NS_IMETHODIMP_(MozExternalRefCountType)
Http3TransportLayer::OutputStreamTunnel::Release() {
  return mTransport->Release();
}

Http3TransportLayer::OutputStreamTunnel::OutputStreamTunnel(
    Http3TransportLayer* aTransport)
    : mTransport(aTransport) {}

NS_IMETHODIMP
Http3TransportLayer::OutputStreamTunnel::Close() {
  LOG(("Http3TransportLayer::OutputStreamTunnel::Close [this=%p]\n", this));
  return CloseWithStatus(NS_BASE_STREAM_CLOSED);
}

NS_IMETHODIMP
Http3TransportLayer::OutputStreamTunnel::Flush() {
  LOG(("Http3TransportLayer::OutputStreamTunnel::Flush [this=%p]\n", this));
  return NS_OK;
}

NS_IMETHODIMP
Http3TransportLayer::OutputStreamTunnel::StreamStatus() {
  LOG(("TLSTransportLayerOutputStream::StreamStatus [this=%p]\n", this));
  return mCondition;
}

NS_IMETHODIMP
Http3TransportLayer::OutputStreamTunnel::Write(const char* buf, uint32_t count,
                                               uint32_t* countWritten) {
  LOG(("Http3TransportLayer::OutputStreamTunnel::Write [this=%p count=%u]\n",
       this, count));
  *countWritten = 0;
  if (NS_FAILED(mCondition)) {
    return mCondition;
  }

  RefPtr<Http3StreamTunnel> tunnel = mTransport->GetStream();
  if (!tunnel) {
    return NS_ERROR_UNEXPECTED;
  }

  tunnel->HasDataToWrite();
  return tunnel->OnReadSegment(buf, count, countWritten);
}

NS_IMETHODIMP
Http3TransportLayer::OutputStreamTunnel::WriteSegments(nsReadSegmentFun reader,
                                                       void* closure,
                                                       uint32_t count,
                                                       uint32_t* countRead) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
Http3TransportLayer::OutputStreamTunnel::WriteFrom(nsIInputStream* stream,
                                                   uint32_t count,
                                                   uint32_t* countRead) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
Http3TransportLayer::OutputStreamTunnel::IsNonBlocking(bool* nonblocking) {
  *nonblocking = true;
  return NS_OK;
}

NS_IMETHODIMP
Http3TransportLayer::OutputStreamTunnel::CloseWithStatus(nsresult reason) {
  LOG(("OutputStreamTunnel::CloseWithStatus [this=%p reason=%" PRIx32 "]\n",
       this, static_cast<uint32_t>(reason)));
  mCondition = reason;

  RefPtr<Http3StreamTunnel> tunnel = mTransport->GetStream();
  if (!tunnel) {
    return NS_OK;
  }

  tunnel->CleanupStream(reason);
  return NS_OK;
}

nsresult Http3TransportLayer::OutputStreamTunnel::OnSocketReady(
    nsresult condition) {
  LOG(("OutputStreamTunnel::OnSocketReady [this=%p cond=%" PRIx32
       " callback=%p]\n",
       this, static_cast<uint32_t>(condition), mCallback.get()));

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

NS_IMETHODIMP
Http3TransportLayer::OutputStreamTunnel::AsyncWait(
    nsIOutputStreamCallback* callback, uint32_t flags, uint32_t amount,
    nsIEventTarget* target) {
  LOG(("OutputStreamTunnel::AsyncWait [this=%p]\n", this));

  // The following parameters are not used:
  (void)flags;
  (void)amount;
  (void)target;

  RefPtr<OutputStreamTunnel> self(this);
  if (NS_FAILED(mCondition)) {
    (void)DispatchToCurrent(NS_NewRunnableFunction(
        "OutputStreamTunnel::CallOnSocketReady",
        [self{std::move(self)}]() { self->OnSocketReady(self->mCondition); }));
  } else if (callback) {
    // Inform the proxy connection that the inner connetion wants to
    // read data.
    RefPtr<Http3StreamTunnel> tunnel = mTransport->GetStream();
    if (!tunnel) {
      return NS_ERROR_UNEXPECTED;
    }
    tunnel->HasDataToWrite();
  }

  mCallback = callback;
  return NS_OK;
}

//-----------------------------------------------------------------------------
// Http3TransportLayer impl
//-----------------------------------------------------------------------------

NS_IMPL_ISUPPORTS(Http3TransportLayer, nsISocketTransport, nsITransport,
                  nsIInputStreamCallback, nsIOutputStreamCallback)

Http3TransportLayer::Http3TransportLayer(Http3StreamTunnel* aStream)
    : mStream(aStream), mInput(this), mOutput(this) {
  LOG(("Http3TransportLayer ctor %p", this));
}

Http3TransportLayer::~Http3TransportLayer() {
  LOG(("Http3TransportLayer dtor %p", this));
}

already_AddRefed<Http3StreamTunnel> Http3TransportLayer::GetStream() {
  RefPtr<Http3StreamTunnel> stream = mStream;
  if (!stream || stream->Closed()) {
    return nullptr;
  }

  return stream.forget();
}

nsIAsyncInputStream* Http3TransportLayer::GetInput() { return &mInput; }

nsIAsyncOutputStream* Http3TransportLayer::GetOutput() { return &mOutput; }

nsresult Http3TransportLayer::CallToReadData() {
  LOG(("Http3TransportLayer::CallToReadData this=%p", this));
  return mOutput.OnSocketReady(NS_OK);
}

nsresult Http3TransportLayer::CallToWriteData() {
  LOG(("Http3TransportLayer::CallToWriteData this=%p", this));
  if (!mInput.HasCallback()) {
    return NS_BASE_STREAM_WOULD_BLOCK;
  }
  return mInput.OnSocketReady(NS_OK);
}

NS_IMETHODIMP
Http3TransportLayer::OnInputStreamReady(nsIAsyncInputStream* in) {
  return NS_OK;
}

NS_IMETHODIMP
Http3TransportLayer::OnOutputStreamReady(nsIAsyncOutputStream* out) {
  return NS_OK;
}

NS_IMETHODIMP
Http3TransportLayer::SetKeepaliveEnabled(bool aKeepaliveEnabled) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
Http3TransportLayer::SetKeepaliveVals(int32_t keepaliveIdleTime,
                                      int32_t keepaliveRetryInterval) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
Http3TransportLayer::GetSecurityCallbacks(
    nsIInterfaceRequestor** aSecurityCallbacks) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
Http3TransportLayer::SetSecurityCallbacks(
    nsIInterfaceRequestor* aSecurityCallbacks) {
  return NS_OK;
}

NS_IMETHODIMP
Http3TransportLayer::OpenInputStream(uint32_t aFlags, uint32_t aSegmentSize,
                                     uint32_t aSegmentCount,
                                     nsIInputStream** _retval) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
Http3TransportLayer::OpenOutputStream(uint32_t aFlags, uint32_t aSegmentSize,
                                      uint32_t aSegmentCount,
                                      nsIOutputStream** _retval) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
Http3TransportLayer::Close(nsresult aReason) {
  LOG(("Http3TransportLayer::Close [this=%p reason=%" PRIx32 "]\n", this,
       static_cast<uint32_t>(aReason)));
  if (NS_SUCCEEDED(mCondition)) {
    if (NS_SUCCEEDED(aReason)) {
      aReason = NS_BASE_STREAM_CLOSED;
    }
    mOutput.CloseWithStatus(aReason);
    mInput.CloseWithStatus(aReason);
    // Let the session pickup that the stream has been closed.
    mCondition = aReason;
  }
  return NS_OK;
}

void Http3TransportLayer::OnStreamClosed(nsresult aReason) {
  LOG(("Http3TransportLayer::OnStreamClosed this=%p", this));
  if (NS_SUCCEEDED(mCondition)) {
    if (NS_SUCCEEDED(aReason)) {
      aReason = NS_BASE_STREAM_CLOSED;
    }
    mOutput.OnSocketReady(aReason);
    mInput.OnSocketReady(aReason);
    mCondition = aReason;
  }
}

NS_IMETHODIMP
Http3TransportLayer::SetEventSink(nsITransportEventSink* aSink,
                                  nsIEventTarget* aEventTarget) {
  return NS_OK;
}

NS_IMETHODIMP
Http3TransportLayer::Bind(NetAddr* aLocalAddr) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
Http3TransportLayer::GetEchConfigUsed(bool* aEchConfigUsed) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
Http3TransportLayer::SetEchConfig(const nsACString& aEchConfig) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
Http3TransportLayer::ResolvedByTRR(bool* aResolvedByTRR) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP Http3TransportLayer::GetEffectiveTRRMode(
    nsIRequest::TRRMode* aEffectiveTRRMode) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP Http3TransportLayer::GetTrrSkipReason(
    nsITRRSkipReason::value* aTrrSkipReason) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

#define FWD_H3ST_PTR(fx, ts) \
  NS_IMETHODIMP              \
  Http3TransportLayer::fx(ts* arg) { return NS_OK; }

#define FWD_H3ST_ADDREF(fx, ts) \
  NS_IMETHODIMP                 \
  Http3TransportLayer::fx(ts** arg) { return NS_OK; }

#define FWD_H3ST(fx, ts) \
  NS_IMETHODIMP          \
  Http3TransportLayer::fx(ts arg) { return NS_OK; }

FWD_H3ST_PTR(GetKeepaliveEnabled, bool);
FWD_H3ST_PTR(GetSendBufferSize, uint32_t);
FWD_H3ST(SetSendBufferSize, uint32_t);
FWD_H3ST_PTR(GetPort, int32_t);
FWD_H3ST_PTR(GetSelfAddr, mozilla::net::NetAddr);
FWD_H3ST_ADDREF(GetScriptablePeerAddr, nsINetAddr);
FWD_H3ST_ADDREF(GetScriptableSelfAddr, nsINetAddr);
FWD_H3ST_PTR(GetConnectionFlags, uint32_t);
FWD_H3ST(SetConnectionFlags, uint32_t);
FWD_H3ST(SetIsTRRConnection, bool);
FWD_H3ST_PTR(GetIsTRRConnection, bool);
FWD_H3ST_PTR(GetTlsFlags, uint32_t);
FWD_H3ST(SetTlsFlags, uint32_t);
FWD_H3ST_PTR(GetRecvBufferSize, uint32_t);
FWD_H3ST(SetRecvBufferSize, uint32_t);
FWD_H3ST_PTR(GetResetIPFamilyPreference, bool);

nsresult Http3TransportLayer::IsAlive(bool* aAlive) {
  *aAlive = true;
  return NS_OK;
}

nsresult Http3TransportLayer::GetPeerAddr(NetAddr* addr) {
  // TODO: what address we should use?
  NetAddr peerAddr;
  peerAddr.InitFromString("127.0.0.1"_ns);
  *addr = peerAddr;
  return NS_OK;
}

nsresult Http3TransportLayer::GetTlsSocketControl(
    nsITLSSocketControl** tlsSocketControl) {
  return NS_OK;
}

nsresult Http3TransportLayer::GetOriginAttributes(
    mozilla::OriginAttributes* aOriginAttributes) {
  return NS_OK;
}

nsresult Http3TransportLayer::SetOriginAttributes(
    const mozilla::OriginAttributes& aOriginAttributes) {
  return NS_OK;
}

NS_IMETHODIMP
Http3TransportLayer::GetScriptableOriginAttributes(
    JSContext* aCx, JS::MutableHandle<JS::Value> aOriginAttributes) {
  return NS_OK;
}

NS_IMETHODIMP
Http3TransportLayer::SetScriptableOriginAttributes(
    JSContext* aCx, JS::Handle<JS::Value> aOriginAttributes) {
  return NS_OK;
}

NS_IMETHODIMP
Http3TransportLayer::GetHost(nsACString& aHost) { return NS_OK; }

NS_IMETHODIMP
Http3TransportLayer::GetTimeout(uint32_t aType, uint32_t* _retval) {
  return NS_OK;
}

NS_IMETHODIMP
Http3TransportLayer::SetTimeout(uint32_t aType, uint32_t aValue) {
  return NS_OK;
}

NS_IMETHODIMP
Http3TransportLayer::SetReuseAddrPort(bool aReuseAddrPort) { return NS_OK; }

NS_IMETHODIMP
Http3TransportLayer::SetLinger(bool aPolarity, int16_t aTimeout) {
  return NS_OK;
}

NS_IMETHODIMP
Http3TransportLayer::GetQoSBits(uint8_t* aQoSBits) { return NS_OK; }

NS_IMETHODIMP
Http3TransportLayer::SetQoSBits(uint8_t aQoSBits) { return NS_OK; }

NS_IMETHODIMP
Http3TransportLayer::GetRetryDnsIfPossible(bool* aRetry) { return NS_OK; }

NS_IMETHODIMP
Http3TransportLayer::GetStatus(nsresult* aStatus) {
  *aStatus = mCondition;
  return NS_OK;
}

//-----------------------------------------------------------------------------
// Http3StreamTunnel impl
//-----------------------------------------------------------------------------

Http3StreamTunnel::Http3StreamTunnel(nsAHttpTransaction* aTrans,
                                     Http3Session* aSession,
                                     uint64_t aCurrentBrowserId)
    : Http3Stream(aTrans, aSession, ClassOfService(), aCurrentBrowserId) {
  LOG(("Http3StreamTunnel ctor %p", this));
}

Http3StreamTunnel::~Http3StreamTunnel() {
  LOG(("Http3StreamTunnel dtor %p", this));
}

nsresult Http3StreamTunnel::TryActivating() {
  nsProxyInfo* info = mTransaction->ConnectionInfo()->ProxyInfo();
  if (!info) {
    return NS_ERROR_UNEXPECTED;
  }

  nsAutoCString host;
  DebugOnly<nsresult> rv{};
  rv = nsHttpHandler::GenerateHostPort(
      nsDependentCString(mTransaction->ConnectionInfo()->Origin()),
      mTransaction->ConnectionInfo()->OriginPort(), host);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  LOG(("Http3StreamTunnel::TryActivating [auth=%s]", host.get()));
  return mSession->TryActivating("CONNECT"_ns, ""_ns, host, ""_ns,
                                 mFlatHttpRequestHeaders, &mStreamId, this);
}

void Http3StreamTunnel::Close(nsresult aResult) {
  LOG(("Http3StreamTunnel::Close %p", this));
  if (mClosed) {
    return;
  }
  mClosed = true;

  mRecvState = RECV_DONE;
  mSendState = SEND_DONE;

  mSession = nullptr;

  if (mTransaction) {
    mTransaction->Close(aResult);
  }

  if (mTransport) {
    mTransport->OnStreamClosed(aResult);
    mTransport = nullptr;
  }
}

nsresult Http3StreamTunnel::ReadSegments() {
  LOG(("Http3StreamTunnel::ReadSegments %p mSendState=%d mRecvState=%d", this,
       mSendState, mRecvState));
  if (mRecvState == RECV_DONE) {
    // Don't transmit any request frames if the peer cannot respond or respone
    // is already done.
    LOG3(
        ("Http3StreamTunnel %p ReadSegments request stream aborted due to"
         " response side closure\n",
         this));
    return NS_ERROR_ABORT;
  }

  if (mSendState == SEND_DONE) {
    return NS_OK;
  }

  if (!mTransport) {
    return NS_ERROR_UNEXPECTED;
  }

  nsresult rv = mTransport->CallToReadData();

  if (mSendState == WAITING_TO_ACTIVATE &&
      (NS_SUCCEEDED(rv) || rv == NS_BASE_STREAM_WOULD_BLOCK)) {
    LOG3(("Http3StreamTunnel %p ReadSegments forcing OnReadSegment call\n",
          this));
    uint32_t wasted = 0;
    nsresult rv2 = OnReadSegment("", 0, &wasted);

    LOG3(("  OnReadSegment returned 0x%08" PRIx32, static_cast<uint32_t>(rv2)));
  }

  return rv;
}

nsresult Http3StreamTunnel::BufferInput() {
  char buf[SimpleBufferPage::kSimpleBufferPageSize];
  uint32_t countWritten;
  nsresult rv = mSession->ReadResponseData(
      mStreamId, buf, SimpleBufferPage::kSimpleBufferPageSize, &countWritten,
      &mFin);
  if (NS_FAILED(rv) && rv != NS_BASE_STREAM_WOULD_BLOCK) {
    return rv;
  }
  LOG(("Http3StreamTunnel::BufferInput %p countWritten=%d mFin=%d", this,
       countWritten, mFin));
  if (countWritten == 0) {
    if (mFin) {
      mRecvState = RECV_DONE;
      rv = NS_BASE_STREAM_CLOSED;
    } else {
      rv = NS_BASE_STREAM_WOULD_BLOCK;
    }
  } else {
    mTotalRead += countWritten;
    if (mFin) {
      mRecvState = RECEIVED_FIN;
    }
  }
  if (NS_SUCCEEDED(rv)) {
    rv = mSimpleBuffer.Write(buf, countWritten);
    if (NS_FAILED(rv)) {
      MOZ_ASSERT(rv == NS_ERROR_OUT_OF_MEMORY);
      return NS_ERROR_OUT_OF_MEMORY;
    }
  }
  return rv;
}

nsresult Http3StreamTunnel::WriteSegments() {
  LOG(("Http3StreamTunnel::WriteSegments [this=%p]", this));
  if (mRecvState == RECV_DONE) {
    return NS_OK;
  }

  if (!mTransport) {
    return NS_ERROR_UNEXPECTED;
  }

  nsresult rv = NS_OK;
  bool again = true;

  do {
    mSocketInCondition = NS_OK;
    rv = mTransport->CallToWriteData();
    if (mRecvState == RECV_DONE) {
      return NS_ERROR_UNEXPECTED;
    }

    // When CallToWriteData() returns NS_BASE_STREAM_WOULD_BLOCK, it means the
    // consumer can't accept data at the moment. We need to read the data into a
    // buffer so it won't block other streams.
    if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
      rv = BufferInput();
    }

    if (mRecvState == RECEIVED_FIN) {
      rv = NS_BASE_STREAM_CLOSED;
      mRecvState = RECV_DONE;
    }

    if (NS_FAILED(rv)) {
      if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
        rv = NS_OK;
      }
      again = false;
    } else if (NS_FAILED(mSocketInCondition)) {
      if (mSocketInCondition != NS_BASE_STREAM_WOULD_BLOCK) {
        rv = mSocketInCondition;
      }
      again = false;
    }
  } while (again && gHttpHandler->Active());

  return rv;
}

nsresult Http3StreamTunnel::OnWriteSegment(char* buf, uint32_t count,
                                           uint32_t* countWritten) {
  LOG(("Http3StreamTunnel::OnWriteSegment [this=%p, state=%d", this,
       mRecvState));
  // Sometimes we have read data from the network and stored it in a pipe
  // so that other streams can proceed when the gecko caller is not processing
  // data events fast enough and flow control hasn't caught up yet. This
  // gets the stored data out of that pipe
  if (mSimpleBuffer.Available()) {
    *countWritten = mSimpleBuffer.Read(buf, count);
    MOZ_ASSERT(*countWritten);
    LOG3(
        ("Http3StreamTunnel::OnWriteSegment read from flow "
         "control buffer %p %d",
         this, *countWritten));
    return NS_OK;
  }

  return Http3Stream::OnWriteSegment(buf, count, countWritten);
}

void Http3StreamTunnel::HasDataToWrite() {
  mSession->StreamHasDataToWrite(this);
}

void Http3StreamTunnel::HasDataToRead() {
  // We can't always call ConnectSlowConsumer(), because it triggers
  // ForceRecv(),
  // which posts a runnable to call WriteSegments() again. When we already have
  // data buffered, this is fine. The consumer can read data from the buffer.
  // However, if no data is buffered, doing this would create a busy loop that
  // continuously waits for data.
  if (mSimpleBuffer.Available()) {
    mSession->ConnectSlowConsumer(this);
  }
}

already_AddRefed<nsHttpConnection> Http3StreamTunnel::CreateHttpConnection(
    nsIInterfaceRequestor* aCallbacks, PRIntervalTime aRtt,
    bool aIsExtendedCONNECT) {
  mTransport = new Http3TransportLayer(this);
  RefPtr<nsHttpConnection> conn = new nsHttpConnection();
  conn->SetTunnelOverHttp3();

  conn->SetTransactionCaps(mTransaction->Caps());
  nsresult rv =
      conn->Init(mTransaction->ConnectionInfo(),
                 gHttpHandler->ConnMgr()->MaxRequestDelay(), mTransport,
                 mTransport->GetInput(), mTransport->GetOutput(), true, NS_OK,
                 aCallbacks, aRtt, aIsExtendedCONNECT);
  MOZ_RELEASE_ASSERT(NS_SUCCEEDED(rv));
  return conn.forget();
}

void Http3StreamTunnel::CleanupStream(nsresult aReason) {
  if (mSession && !mClosed) {
    LOG(("Http3StreamTunnel::CleanupStream %p", this));
    mSession->CloseStream(this, aReason);
  }
}

}  // namespace mozilla::net
