/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "HttpLog.h"

#include "TLSTransportLayer.h"
#include "nsISocketProvider.h"
#include "nsITLSSocketControl.h"
#include "nsQueryObject.h"
#include "nsSocketProviderService.h"
#include "nsSocketTransport2.h"

namespace mozilla::net {

//-----------------------------------------------------------------------------
// TLSTransportLayerInputStream impl
//-----------------------------------------------------------------------------

NS_IMPL_QUERY_INTERFACE(TLSTransportLayer::InputStreamWrapper, nsIInputStream,
                        nsIAsyncInputStream)

NS_IMETHODIMP_(MozExternalRefCountType)
TLSTransportLayer::InputStreamWrapper::AddRef() { return mTransport->AddRef(); }

NS_IMETHODIMP_(MozExternalRefCountType)
TLSTransportLayer::InputStreamWrapper::Release() {
  return mTransport->Release();
}

TLSTransportLayer::InputStreamWrapper::InputStreamWrapper(
    nsIAsyncInputStream* aInputStream, TLSTransportLayer* aTransport)
    : mSocketIn(aInputStream), mTransport(aTransport) {}

NS_IMETHODIMP
TLSTransportLayer::InputStreamWrapper::Close() {
  LOG(("TLSTransportLayer::InputStreamWrapper::Close [this=%p]\n", this));
  return mSocketIn->Close();
}

NS_IMETHODIMP TLSTransportLayer::InputStreamWrapper::Available(
    uint64_t* avail) {
  LOG(("TLSTransportLayer::InputStreamWrapper::Available [this=%p]\n", this));
  return mSocketIn->Available(avail);
}

NS_IMETHODIMP TLSTransportLayer::InputStreamWrapper::StreamStatus() {
  LOG(("TLSTransportLayer::InputStreamWrapper::StreamStatus [this=%p]\n",
       this));
  return mSocketIn->StreamStatus();
}

nsresult TLSTransportLayer::InputStreamWrapper::ReadDirectly(
    char* buf, uint32_t count, uint32_t* countRead) {
  LOG(("TLSTransportLayer::InputStreamWrapper::ReadDirectly [this=%p]\n",
       this));
  return mSocketIn->Read(buf, count, countRead);
}

NS_IMETHODIMP
TLSTransportLayer::InputStreamWrapper::Read(char* buf, uint32_t count,
                                            uint32_t* countRead) {
  LOG(("TLSTransportLayer::InputStreamWrapper::Read [this=%p]\n", this));

  *countRead = 0;

  if (NS_FAILED(mStatus)) {
    return (mStatus == NS_BASE_STREAM_CLOSED) ? NS_OK : mStatus;
  }

  int32_t bytesRead = PR_Read(mTransport->mFD, buf, count);
  if (bytesRead > 0) {
    *countRead = bytesRead;
  } else if (bytesRead < 0) {
    PRErrorCode code = PR_GetError();
    if (code == PR_WOULD_BLOCK_ERROR) {
      LOG((
          "TLSTransportLayer::InputStreamWrapper::Read %p PR_Read would block ",
          this));
      return NS_BASE_STREAM_WOULD_BLOCK;
    }
    // Prefer the transport's status only when the transport really failed
    // Otherwise the error came from NSS.
    if (NS_SUCCEEDED(mStatus)) {
      nsresult transportStatus = mTransport->mInputStatus;
      mStatus = (NS_FAILED(transportStatus) &&
                 transportStatus != NS_BASE_STREAM_WOULD_BLOCK)
                    ? transportStatus
                    : ErrorAccordingToNSPR(code);
      LOG(("TLSTransportLayer::InputStreamWrapper::Read %p error %" PRIx32
           " (transport=%" PRIx32 ", nss code=%d)\n",
           this, static_cast<uint32_t>(mStatus),
           static_cast<uint32_t>(transportStatus), code));
    }
  }

  if (NS_SUCCEEDED(mStatus) && !bytesRead) {
    LOG(
        ("TLSTransportLayer::InputStreamWrapper::Read %p "
         "Second layer of TLS stripping results in STREAM_CLOSED\n",
         this));
    mStatus = NS_BASE_STREAM_CLOSED;
  }

  LOG(("TLSTransportLayer::InputStreamWrapper::Read %p rv=%" PRIx32
       " didread=%d "
       "2 layers of ssl stripped to plaintext\n",
       this, static_cast<uint32_t>(mStatus), bytesRead));
  return mStatus;
}

NS_IMETHODIMP
TLSTransportLayer::InputStreamWrapper::ReadSegments(nsWriteSegmentFun writer,
                                                    void* closure,
                                                    uint32_t count,
                                                    uint32_t* countRead) {
  LOG(("TLSTransportLayer::InputStreamWrapper::ReadSegments [this=%p]\n",
       this));
  return mSocketIn->ReadSegments(writer, closure, count, countRead);
}

NS_IMETHODIMP
TLSTransportLayer::InputStreamWrapper::IsNonBlocking(bool* nonblocking) {
  return mSocketIn->IsNonBlocking(nonblocking);
}

NS_IMETHODIMP
TLSTransportLayer::InputStreamWrapper::CloseWithStatus(nsresult reason) {
  LOG(
      ("TLSTransportLayer::InputStreamWrapper::CloseWithStatus [this=%p "
       "reason=%" PRIx32 "]\n",
       this, static_cast<uint32_t>(reason)));
  mStatus = reason;
  return mSocketIn->CloseWithStatus(reason);
}

NS_IMETHODIMP
TLSTransportLayer::InputStreamWrapper::AsyncWait(
    nsIInputStreamCallback* callback, uint32_t flags, uint32_t amount,
    nsIEventTarget* target) {
  LOG(
      ("TLSTransportLayer::InputStreamWrapper::AsyncWait [this=%p, "
       "callback=%p]\n",
       this, callback));
  mTransport->mInputCallback = callback;
  // Don't bother to call PR_POLL when |callback| is NULL. We call |AsyncWait|
  // directly to null out the underlying callback.
  if (!callback) {
    return mSocketIn->AsyncWait(nullptr, 0, 0, nullptr);
  }

  PRPollDesc pd;
  pd.fd = mTransport->mFD;
  pd.in_flags = PR_POLL_READ | PR_POLL_EXCEPT;
  pd.out_flags = 0;
  // Only run PR_Poll on the socket thread. Also, make sure this lives at least
  // as long as that operation.
  auto DoPoll = [self = RefPtr{this}, pd(pd)]() mutable {
    self->mTransport->mPollCalled = false;
    int32_t rv = PR_Poll(&pd, 1, PR_INTERVAL_NO_TIMEOUT);
    LOG(
        ("TLSTransportLayer::InputStreamWrapper::AsyncWait rv=%d out_flags=%d "
         "pollCalled=%d",
         rv, (int)pd.out_flags, (int)self->mTransport->mPollCalled));
    self->mTransport->MaybeWakeWaiter(rv, pd.out_flags, /* aInput */ true);
  };
  if (OnSocketThread()) {
    DoPoll();
  } else {
    gSocketTransportService->Dispatch(NS_NewRunnableFunction(
        "TLSTransportLayer::InputStreamWrapper::AsyncWait", DoPoll));
  }
  return NS_OK;
}

//-----------------------------------------------------------------------------
// TLSTransportLayerOutputStream impl
//-----------------------------------------------------------------------------

NS_IMPL_QUERY_INTERFACE(TLSTransportLayer::OutputStreamWrapper, nsIOutputStream,
                        nsIAsyncOutputStream)

NS_IMETHODIMP_(MozExternalRefCountType)
TLSTransportLayer::OutputStreamWrapper::AddRef() {
  return mTransport->AddRef();
}

NS_IMETHODIMP_(MozExternalRefCountType)
TLSTransportLayer::OutputStreamWrapper::Release() {
  return mTransport->Release();
}

TLSTransportLayer::OutputStreamWrapper::OutputStreamWrapper(
    nsIAsyncOutputStream* aOutputStream, TLSTransportLayer* aTransport)
    : mSocketOut(aOutputStream), mTransport(aTransport) {}

NS_IMETHODIMP
TLSTransportLayer::OutputStreamWrapper::Close() {
  LOG(("TLSTransportLayer::OutputStreamWrapper::Close [this=%p]\n", this));
  return mSocketOut->Close();
}

NS_IMETHODIMP
TLSTransportLayer::OutputStreamWrapper::Flush() {
  LOG(("TLSTransportLayerOutputStream::Flush [this=%p]\n", this));
  return mSocketOut->Flush();
}

NS_IMETHODIMP
TLSTransportLayer::OutputStreamWrapper::StreamStatus() {
  LOG(("TLSTransportLayerOutputStream::StreamStatus [this=%p]\n", this));
  return mSocketOut->StreamStatus();
}

nsresult TLSTransportLayer::OutputStreamWrapper::WriteDirectly(
    const char* buf, uint32_t count, uint32_t* countWritten) {
  LOG(
      ("TLSTransportLayer::OutputStreamWrapper::WriteDirectly [this=%p "
       "count=%u]\n",
       this, count));
  return mSocketOut->Write(buf, count, countWritten);
}

NS_IMETHODIMP
TLSTransportLayer::OutputStreamWrapper::Write(const char* buf, uint32_t count,
                                              uint32_t* countWritten) {
  LOG(("TLSTransportLayer::OutputStreamWrapper::Write [this=%p count=%u]\n",
       this, count));

  *countWritten = 0;

  if (NS_FAILED(mStatus)) {
    return (mStatus == NS_BASE_STREAM_CLOSED) ? NS_OK : mStatus;
  }

  int32_t written = PR_Write(mTransport->mFD, buf, count);
  LOG(("TLSTransportLayer::OutputStreamWrapper::Write %p PRWrite(%d) = %d",
       this, count, written));

  if (written > 0) {
    *countWritten = written;
  } else if (written < 0) {
    PRErrorCode code = PR_GetError();
    if (code == PR_WOULD_BLOCK_ERROR) {
      LOG(
          ("TLSTransportLayer::OutputStreamWrapper::Write %p PRWrite would "
           "block ",
           this));
      return NS_BASE_STREAM_WOULD_BLOCK;
    }

    // Same reasoning as InputStreamWrapper::Read above.
    if (NS_SUCCEEDED(mStatus)) {
      nsresult transportStatus = mTransport->mOutputStatus;
      mStatus = (NS_FAILED(transportStatus) &&
                 transportStatus != NS_BASE_STREAM_WOULD_BLOCK)
                    ? transportStatus
                    : ErrorAccordingToNSPR(code);
      LOG(("TLSTransportLayer::OutputStreamWrapper::Write %p error %" PRIx32
           " (transport=%" PRIx32 ", nss code=%d)\n",
           this, static_cast<uint32_t>(mStatus),
           static_cast<uint32_t>(transportStatus), code));
    }
  }

  return mStatus;
}

NS_IMETHODIMP
TLSTransportLayer::OutputStreamWrapper::WriteSegments(nsReadSegmentFun reader,
                                                      void* closure,
                                                      uint32_t count,
                                                      uint32_t* countRead) {
  return mSocketOut->WriteSegments(reader, closure, count, countRead);
}

// static
nsresult TLSTransportLayer::OutputStreamWrapper::WriteFromSegments(
    nsIInputStream* input, void* closure, const char* fromSegment,
    uint32_t offset, uint32_t count, uint32_t* countRead) {
  OutputStreamWrapper* self = (OutputStreamWrapper*)closure;
  return self->Write(fromSegment, count, countRead);
}

NS_IMETHODIMP
TLSTransportLayer::OutputStreamWrapper::WriteFrom(nsIInputStream* stream,
                                                  uint32_t count,
                                                  uint32_t* countRead) {
  return stream->ReadSegments(WriteFromSegments, this, count, countRead);
}

NS_IMETHODIMP
TLSTransportLayer::OutputStreamWrapper::IsNonBlocking(bool* nonblocking) {
  return mSocketOut->IsNonBlocking(nonblocking);
}

NS_IMETHODIMP
TLSTransportLayer::OutputStreamWrapper::CloseWithStatus(nsresult reason) {
  LOG(("OutputStreamWrapper::CloseWithStatus [this=%p reason=%" PRIx32 "]\n",
       this, static_cast<uint32_t>(reason)));
  mStatus = reason;
  return mSocketOut->CloseWithStatus(reason);
}

NS_IMETHODIMP
TLSTransportLayer::OutputStreamWrapper::AsyncWait(
    nsIOutputStreamCallback* callback, uint32_t flags, uint32_t amount,
    nsIEventTarget* target) {
  LOG(
      ("TLSTransportLayer::OutputStreamWrapper::AsyncWait [this=%p, "
       "mOutputCallback=%p "
       "callback=%p]\n",
       this, mTransport->mOutputCallback.get(), callback));
  mTransport->mOutputCallback = callback;
  // Don't bother to call PR_POLL when |callback| is NULL. We call |AsyncWait|
  // directly to null out the underlying callback.
  if (!callback) {
    return mSocketOut->AsyncWait(nullptr, 0, 0, nullptr);
  }

  PRPollDesc pd;
  pd.fd = mTransport->mFD;
  pd.in_flags = PR_POLL_WRITE | PR_POLL_EXCEPT;
  pd.out_flags = 0;
  mTransport->mPollCalled = false;
  int32_t rv = PR_Poll(&pd, 1, PR_INTERVAL_NO_TIMEOUT);
  LOG(
      ("TLSTransportLayer::OutputStreamWrapper::AsyncWait rv=%d out_flags=%d "
       "pollCalled=%d",
       rv, (int)pd.out_flags, (int)mTransport->mPollCalled));
  mTransport->MaybeWakeWaiter(rv, pd.out_flags, /* aInput */ false);
  return NS_OK;
}

//-----------------------------------------------------------------------------
// TLSTransportLayer impl
//-----------------------------------------------------------------------------

static PRDescIdentity sTLSTransportLayerIdentity;
static PRIOMethods sTLSTransportLayerMethods;
static PRIOMethods* sTLSTransportLayerMethodsPtr = nullptr;

bool TLSTransportLayer::DispatchRelease() {
  if (OnSocketThread()) {
    return false;
  }

  gSocketTransportService->Dispatch(
      NewNonOwningRunnableMethod("net::TLSTransportLayer::Release", this,
                                 &TLSTransportLayer::Release),
      NS_DISPATCH_NORMAL);

  return true;
}

NS_IMPL_ADDREF(TLSTransportLayer)
NS_IMETHODIMP_(MozExternalRefCountType)
TLSTransportLayer::Release() {
  nsrefcnt count = mRefCnt - 1;
  if (DispatchRelease()) {
    // Redispatched to the socket thread.
    return count;
  }

  MOZ_ASSERT(0 != mRefCnt, "dup release");
  count = --mRefCnt;
  NS_LOG_RELEASE(this, count, "TLSTransportLayer");

  if (0 == count) {
    mRefCnt = 1;
    delete (this);
    return 0;
  }

  return count;
}

NS_INTERFACE_MAP_BEGIN(TLSTransportLayer)
  NS_INTERFACE_MAP_ENTRY(nsISocketTransport)
  NS_INTERFACE_MAP_ENTRY(nsITransport)
  NS_INTERFACE_MAP_ENTRY(nsIInputStreamCallback)
  NS_INTERFACE_MAP_ENTRY(nsIOutputStreamCallback)
  NS_INTERFACE_MAP_ENTRY_CONCRETE(TLSTransportLayer)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsITransport)
NS_INTERFACE_MAP_END

TLSTransportLayer::TLSTransportLayer(nsISocketTransport* aTransport,
                                     nsIAsyncInputStream* aInputStream,
                                     nsIAsyncOutputStream* aOutputStream,
                                     nsIInputStreamCallback* aOwner)
    : mSocketTransport(aTransport),
      mSocketInWrapper(aInputStream, this),
      mSocketOutWrapper(aOutputStream, this),
      mOwner(aOwner) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG(("TLSTransportLayer ctor this=[%p]", this));
}

TLSTransportLayer::~TLSTransportLayer() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG(("TLSTransportLayer dtor this=[%p]", this));
  if (mFD) {
    PR_Close(mFD);
    mFD = nullptr;
  }
  mTLSSocketControl = nullptr;
}

bool TLSTransportLayer::Init(const char* aTLSHost, int32_t aTLSPort) {
  LOG(("TLSTransportLayer::Init this=[%p]", this));
  nsCOMPtr<nsISocketProvider> provider;
  nsCOMPtr<nsISocketProviderService> spserv =
      nsSocketProviderService::GetOrCreate();
  if (!spserv) {
    return false;
  }

  spserv->GetSocketProvider("ssl", getter_AddRefs(provider));
  if (!provider) {
    return false;
  }

  // Install an NSPR layer to handle getpeername() with a failure. This is kind
  // of silly, but the default one used by the pipe asserts when called and the
  // nss code calls it to see if we are connected to a real socket or not.
  if (!sTLSTransportLayerMethodsPtr) {
    // one time initialization
    sTLSTransportLayerIdentity = PR_GetUniqueIdentity("TLSTransportLayer");
    sTLSTransportLayerMethods = *PR_GetDefaultIOMethods();
    sTLSTransportLayerMethods.getpeername = GetPeerName;
    sTLSTransportLayerMethods.getsocketoption = GetSocketOption;
    sTLSTransportLayerMethods.setsocketoption = SetSocketOption;
    sTLSTransportLayerMethods.read = Read;
    sTLSTransportLayerMethods.write = Write;
    sTLSTransportLayerMethods.send = Send;
    sTLSTransportLayerMethods.recv = Recv;
    sTLSTransportLayerMethods.close = Close;
    sTLSTransportLayerMethods.poll = Poll;
    sTLSTransportLayerMethodsPtr = &sTLSTransportLayerMethods;
  }

  mFD = PR_CreateIOLayerStub(sTLSTransportLayerIdentity,
                             &sTLSTransportLayerMethods);
  if (!mFD) {
    return false;
  }

  mFD->secret = reinterpret_cast<PRFilePrivate*>(this);

  return NS_SUCCEEDED(provider->AddToSocket(
      PR_AF_INET, aTLSHost, aTLSPort, nullptr, OriginAttributes(), 0, 0, mFD,
      getter_AddRefs(mTLSSocketControl)));
}

// PSM's poll method cannot report errors: once the handshake has failed it
// stops calling to Poll() and just flags the socket ready, expecting the
// caller to collect the error from PR_Recv/PR_Send.
void TLSTransportLayer::MaybeWakeWaiter(int32_t aPollResult, int16_t aOutFlags,
                                        bool aInput) {
  if (aPollResult <= 0 || mPollCalled || !(aOutFlags & PR_POLL_EXCEPT)) {
    return;
  }

  LOG(("TLSTransportLayer::MaybeWakeWaiter %p waking %s waiter\n", this,
       aInput ? "input" : "output"));

  RefPtr<TLSTransportLayer> self(this);
  gSocketTransportService->Dispatch(NS_NewRunnableFunction(
      "TLSTransportLayer::MaybeWakeWaiter", [self, aInput]() mutable {
        if (aInput) {
          self->OnInputStreamReady(&self->mSocketInWrapper);
        } else {
          self->OnOutputStreamReady(&self->mSocketOutWrapper);
        }
      }));
}

NS_IMETHODIMP
TLSTransportLayer::OnInputStreamReady(nsIAsyncInputStream* in) {
  nsCOMPtr<nsIInputStreamCallback> callback = std::move(mInputCallback);
  if (callback) {
    return callback->OnInputStreamReady(&mSocketInWrapper);
  }
  return NS_OK;
}

NS_IMETHODIMP
TLSTransportLayer::OnOutputStreamReady(nsIAsyncOutputStream* out) {
  nsCOMPtr<nsIOutputStreamCallback> callback = std::move(mOutputCallback);
  nsresult rv = NS_OK;
  if (callback) {
    rv = callback->OnOutputStreamReady(&mSocketOutWrapper);
  }
  return rv;
}

NS_IMETHODIMP
TLSTransportLayer::SetKeepaliveEnabled(bool aKeepaliveEnabled) {
  if (!mSocketTransport) {
    return NS_ERROR_FAILURE;
  }
  return mSocketTransport->SetKeepaliveEnabled(aKeepaliveEnabled);
}

NS_IMETHODIMP
TLSTransportLayer::SetKeepaliveVals(int32_t keepaliveIdleTime,
                                    int32_t keepaliveRetryInterval) {
  if (!mSocketTransport) {
    return NS_ERROR_FAILURE;
  }
  return mSocketTransport->SetKeepaliveVals(keepaliveIdleTime,
                                            keepaliveRetryInterval);
}

NS_IMETHODIMP
TLSTransportLayer::GetSecurityCallbacks(
    nsIInterfaceRequestor** aSecurityCallbacks) {
  if (!mSocketTransport) {
    return NS_ERROR_FAILURE;
  }
  return mSocketTransport->GetSecurityCallbacks(aSecurityCallbacks);
}

NS_IMETHODIMP
TLSTransportLayer::SetSecurityCallbacks(
    nsIInterfaceRequestor* aSecurityCallbacks) {
  if (!mSocketTransport) {
    return NS_ERROR_FAILURE;
  }

  return mSocketTransport->SetSecurityCallbacks(aSecurityCallbacks);
}

NS_IMETHODIMP
TLSTransportLayer::OpenInputStream(uint32_t aFlags, uint32_t aSegmentSize,
                                   uint32_t aSegmentCount,
                                   nsIInputStream** _retval) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
TLSTransportLayer::OpenOutputStream(uint32_t aFlags, uint32_t aSegmentSize,
                                    uint32_t aSegmentCount,
                                    nsIOutputStream** _retval) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
TLSTransportLayer::Close(nsresult aReason) {
  bool onSocketThread = OnSocketThread();
  LOG(("TLSTransportLayer::Close [this=%p reason=%" PRIx32 "] sts=%d", this,
       static_cast<uint32_t>(aReason), onSocketThread));

  if (!onSocketThread) {
    gSocketTransportService->Dispatch(NS_NewRunnableFunction(
        "TLSTransportLayer::Close",
        [self = RefPtr{this}, aReason] { self->Close(aReason); }));
    return NS_OK;
  }

  mInputCallback = nullptr;
  mOutputCallback = nullptr;
  if (mSocketTransport) {
    mSocketTransport->Close(aReason);
    mSocketTransport = nullptr;
  }
  mSocketInWrapper.AsyncWait(nullptr, 0, 0, nullptr);
  mSocketInWrapper.CloseWithStatus(aReason);
  mSocketOutWrapper.AsyncWait(nullptr, 0, 0, nullptr);
  mSocketOutWrapper.CloseWithStatus(aReason);

  if (mOwner) {
    RefPtr<TLSTransportLayer> self = this;
    (void)NS_DispatchToCurrentThread(NS_NewRunnableFunction(
        "TLSTransportLayer::Close", [self{std::move(self)}]() {
          nsCOMPtr<nsIInputStreamCallback> inputCallback =
              std::move(self->mOwner);
          if (inputCallback) {
            // This is hack. We need to make
            // nsHttpConnection::OnInputStreamReady be called, so
            // nsHttpConnection::CloseTransaction can be called to release the
            // transaction.
            (void)inputCallback->OnInputStreamReady(&self->mSocketInWrapper);
          }
        }));
  }

  return NS_OK;
}

NS_IMETHODIMP
TLSTransportLayer::SetEventSink(nsITransportEventSink* aSink,
                                nsIEventTarget* aEventTarget) {
  if (!mSocketTransport) {
    return NS_ERROR_FAILURE;
  }
  return mSocketTransport->SetEventSink(aSink, aEventTarget);
}

NS_IMETHODIMP
TLSTransportLayer::Bind(NetAddr* aLocalAddr) {
  if (!mSocketTransport) {
    return NS_ERROR_FAILURE;
  }
  return mSocketTransport->Bind(aLocalAddr);
}

NS_IMETHODIMP
TLSTransportLayer::GetEchConfigUsed(bool* aEchConfigUsed) {
  if (!mSocketTransport) {
    return NS_ERROR_FAILURE;
  }
  return mSocketTransport->GetEchConfigUsed(aEchConfigUsed);
}

NS_IMETHODIMP
TLSTransportLayer::SetEchConfig(const nsACString& aEchConfig) {
  if (!mSocketTransport) {
    return NS_ERROR_FAILURE;
  }
  return mSocketTransport->SetEchConfig(aEchConfig);
}

NS_IMETHODIMP
TLSTransportLayer::ResolvedByTRR(bool* aResolvedByTRR) {
  if (!mSocketTransport) {
    return NS_ERROR_FAILURE;
  }
  return mSocketTransport->ResolvedByTRR(aResolvedByTRR);
}

NS_IMETHODIMP TLSTransportLayer::GetEffectiveTRRMode(
    nsIRequest::TRRMode* aEffectiveTRRMode) {
  if (!mSocketTransport) {
    return NS_ERROR_FAILURE;
  }
  return mSocketTransport->GetEffectiveTRRMode(aEffectiveTRRMode);
}

NS_IMETHODIMP TLSTransportLayer::GetTrrSkipReason(
    nsITRRSkipReason::value* aTrrSkipReason) {
  if (!mSocketTransport) {
    return NS_ERROR_FAILURE;
  }
  return mSocketTransport->GetTrrSkipReason(aTrrSkipReason);
}

#define FWD_TS_PTR(fx, ts)                          \
  NS_IMETHODIMP                                     \
  TLSTransportLayer::fx(ts* arg) {                  \
    if (!mSocketTransport) return NS_ERROR_FAILURE; \
    return mSocketTransport->fx(arg);               \
  }

#define FWD_TS_ADDREF(fx, ts)                       \
  NS_IMETHODIMP                                     \
  TLSTransportLayer::fx(ts** arg) {                 \
    if (!mSocketTransport) return NS_ERROR_FAILURE; \
    return mSocketTransport->fx(arg);               \
  }

#define FWD_TS(fx, ts)                              \
  NS_IMETHODIMP                                     \
  TLSTransportLayer::fx(ts arg) {                   \
    if (!mSocketTransport) return NS_ERROR_FAILURE; \
    return mSocketTransport->fx(arg);               \
  }

FWD_TS_PTR(GetKeepaliveEnabled, bool);
FWD_TS_PTR(GetSendBufferSize, uint32_t);
FWD_TS(SetSendBufferSize, uint32_t);
FWD_TS_PTR(GetPort, int32_t);
FWD_TS_PTR(GetPeerAddr, mozilla::net::NetAddr);
FWD_TS_PTR(GetSelfAddr, mozilla::net::NetAddr);
FWD_TS_ADDREF(GetScriptablePeerAddr, nsINetAddr);
FWD_TS_ADDREF(GetScriptableSelfAddr, nsINetAddr);
FWD_TS_PTR(IsAlive, bool);
FWD_TS_PTR(GetConnectionFlags, uint32_t);
FWD_TS(SetConnectionFlags, uint32_t);
FWD_TS(SetIsTRRConnection, bool);
FWD_TS_PTR(GetIsTRRConnection, bool);
FWD_TS_PTR(GetTlsFlags, uint32_t);
FWD_TS(SetTlsFlags, uint32_t);
FWD_TS_PTR(GetRecvBufferSize, uint32_t);
FWD_TS(SetRecvBufferSize, uint32_t);
FWD_TS_PTR(GetResetIPFamilyPreference, bool);

nsresult TLSTransportLayer::GetTlsSocketControl(
    nsITLSSocketControl** tlsSocketControl) {
  if (!mTLSSocketControl) {
    return NS_ERROR_ABORT;
  }

  *tlsSocketControl = do_AddRef(mTLSSocketControl).take();
  return NS_OK;
}

nsresult TLSTransportLayer::GetOriginAttributes(
    mozilla::OriginAttributes* aOriginAttributes) {
  if (!mSocketTransport) {
    return NS_ERROR_FAILURE;
  }
  return mSocketTransport->GetOriginAttributes(aOriginAttributes);
}

nsresult TLSTransportLayer::SetOriginAttributes(
    const mozilla::OriginAttributes& aOriginAttributes) {
  if (!mSocketTransport) {
    return NS_ERROR_FAILURE;
  }
  return mSocketTransport->SetOriginAttributes(aOriginAttributes);
}

NS_IMETHODIMP
TLSTransportLayer::GetScriptableOriginAttributes(
    JSContext* aCx, JS::MutableHandle<JS::Value> aOriginAttributes) {
  if (!mSocketTransport) {
    return NS_ERROR_FAILURE;
  }
  return mSocketTransport->GetScriptableOriginAttributes(aCx,
                                                         aOriginAttributes);
}

NS_IMETHODIMP
TLSTransportLayer::SetScriptableOriginAttributes(
    JSContext* aCx, JS::Handle<JS::Value> aOriginAttributes) {
  if (!mSocketTransport) {
    return NS_ERROR_FAILURE;
  }
  return mSocketTransport->SetScriptableOriginAttributes(aCx,
                                                         aOriginAttributes);
}

NS_IMETHODIMP
TLSTransportLayer::GetHost(nsACString& aHost) {
  if (!mSocketTransport) {
    return NS_ERROR_FAILURE;
  }
  return mSocketTransport->GetHost(aHost);
}

NS_IMETHODIMP
TLSTransportLayer::GetTimeout(uint32_t aType, uint32_t* _retval) {
  if (!mSocketTransport) {
    return NS_ERROR_FAILURE;
  }
  return mSocketTransport->GetTimeout(aType, _retval);
}

NS_IMETHODIMP
TLSTransportLayer::SetTimeout(uint32_t aType, uint32_t aValue) {
  if (!mSocketTransport) {
    return NS_ERROR_FAILURE;
  }
  return mSocketTransport->SetTimeout(aType, aValue);
}

NS_IMETHODIMP
TLSTransportLayer::SetReuseAddrPort(bool aReuseAddrPort) {
  if (!mSocketTransport) {
    return NS_ERROR_FAILURE;
  }
  return mSocketTransport->SetReuseAddrPort(aReuseAddrPort);
}

NS_IMETHODIMP
TLSTransportLayer::SetLinger(bool aPolarity, int16_t aTimeout) {
  if (!mSocketTransport) {
    return NS_ERROR_FAILURE;
  }
  return mSocketTransport->SetLinger(aPolarity, aTimeout);
}

NS_IMETHODIMP
TLSTransportLayer::GetQoSBits(uint8_t* aQoSBits) {
  if (!mSocketTransport) {
    return NS_ERROR_FAILURE;
  }
  return mSocketTransport->GetQoSBits(aQoSBits);
}

NS_IMETHODIMP
TLSTransportLayer::SetQoSBits(uint8_t aQoSBits) {
  if (!mSocketTransport) {
    return NS_ERROR_FAILURE;
  }
  return mSocketTransport->SetQoSBits(aQoSBits);
}

NS_IMETHODIMP
TLSTransportLayer::GetRetryDnsIfPossible(bool* aRetry) {
  if (!mSocketTransport) {
    return NS_ERROR_FAILURE;
  }
  return mSocketTransport->GetRetryDnsIfPossible(aRetry);
}

NS_IMETHODIMP
TLSTransportLayer::GetStatus(nsresult* aStatus) {
  if (!mSocketTransport) {
    return NS_ERROR_FAILURE;
  }
  return mSocketTransport->GetStatus(aStatus);
}

int32_t TLSTransportLayer::OutputInternal(const char* aBuf, int32_t aAmount) {
  LOG(("TLSTransportLayer::OutputInternal %p %d", this, aAmount));

  uint32_t outCountWrite = 0;
  mOutputStatus =
      mSocketOutWrapper.WriteDirectly(aBuf, aAmount, &outCountWrite);
  if (NS_FAILED(mOutputStatus)) {
    if (mOutputStatus == NS_BASE_STREAM_WOULD_BLOCK) {
      PR_SetError(PR_WOULD_BLOCK_ERROR, 0);
    } else {
      PR_SetError(PR_UNKNOWN_ERROR, 0);
    }
    return -1;
  }

  return outCountWrite;
}

int32_t TLSTransportLayer::InputInternal(char* aBuf, int32_t aAmount) {
  LOG(("TLSTransportLayer::InputInternal aAmount=%d\n", aAmount));

  uint32_t outCountRead = 0;
  mInputStatus = mSocketInWrapper.ReadDirectly(aBuf, aAmount, &outCountRead);
  if (NS_FAILED(mInputStatus)) {
    if (mInputStatus == NS_BASE_STREAM_WOULD_BLOCK) {
      PR_SetError(PR_WOULD_BLOCK_ERROR, 0);
    } else {
      PR_SetError(PR_UNKNOWN_ERROR, 0);
    }
    return -1;
  }
  return outCountRead;
}

PRStatus TLSTransportLayer::GetPeerName(PRFileDesc* aFD, PRNetAddr* addr) {
  TLSTransportLayer* self = reinterpret_cast<TLSTransportLayer*>(aFD->secret);
  NetAddr peeraddr;
  if (NS_FAILED(self->Transport()->GetPeerAddr(&peeraddr))) {
    return PR_FAILURE;
  }
  NetAddrToPRNetAddr(&peeraddr, addr);
  return PR_SUCCESS;
}

PRStatus TLSTransportLayer::GetSocketOption(PRFileDesc* aFD,
                                            PRSocketOptionData* aOpt) {
  if (aOpt->option == PR_SockOpt_Nonblocking) {
    aOpt->value.non_blocking = PR_TRUE;
    return PR_SUCCESS;
  }
  return PR_FAILURE;
}

PRStatus TLSTransportLayer::SetSocketOption(PRFileDesc* aFD,
                                            const PRSocketOptionData* aOpt) {
  return PR_FAILURE;
}

PRStatus TLSTransportLayer::Close(PRFileDesc* aFD) { return PR_SUCCESS; }

int32_t TLSTransportLayer::Write(PRFileDesc* aFD, const void* aBuf,
                                 int32_t aAmount) {
  TLSTransportLayer* self = reinterpret_cast<TLSTransportLayer*>(aFD->secret);
  return self->OutputInternal(static_cast<const char*>(aBuf), aAmount);
}

int32_t TLSTransportLayer::Send(PRFileDesc* aFD, const void* aBuf,
                                int32_t aAmount, int, PRIntervalTime) {
  return Write(aFD, aBuf, aAmount);
}

int32_t TLSTransportLayer::Read(PRFileDesc* aFD, void* aBuf, int32_t aAmount) {
  TLSTransportLayer* self = reinterpret_cast<TLSTransportLayer*>(aFD->secret);
  return self->InputInternal(static_cast<char*>(aBuf), aAmount);
}

int32_t TLSTransportLayer::Recv(PRFileDesc* aFD, void* aBuf, int32_t aAmount,
                                int, PRIntervalTime) {
  return Read(aFD, aBuf, aAmount);
}

int16_t TLSTransportLayer::Poll(PRFileDesc* fd, int16_t in_flags,
                                int16_t* out_flags) {
  LOG(("TLSTransportLayer::Poll fd=%p inf_flags=%d\n", fd, (int)in_flags));
  *out_flags = in_flags;

  TLSTransportLayer* self = reinterpret_cast<TLSTransportLayer*>(fd->secret);
  if (!self) {
    return 0;
  }
  self->mPollCalled = true;

  if (in_flags & PR_POLL_READ) {
    self->mSocketInWrapper.mSocketIn->AsyncWait(self, 0, 0, nullptr);
  } else if (in_flags & PR_POLL_WRITE) {
    self->mSocketOutWrapper.mSocketOut->AsyncWait(self, 0, 0, nullptr);
  }

  return in_flags;
}

bool TLSTransportLayer::HasDataToRecv() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  if (!mFD) {
    return false;
  }
  int32_t n = 0;
  char c;
  n = PR_Recv(mFD, &c, 1, PR_MSG_PEEK, 0);
  return n > 0;
}

}  // namespace mozilla::net
