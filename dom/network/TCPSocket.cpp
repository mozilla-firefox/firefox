/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "TCPSocket.h"

#include "TCPServerSocket.h"
#include "TCPSocketChild.h"
#include "TCPSocketParent.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/SyncRunnable.h"
#include "mozilla/dom/RootedDictionary.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/TCPSocketBinding.h"
#include "mozilla/dom/TCPSocketErrorEvent.h"
#include "mozilla/dom/TCPSocketErrorEventBinding.h"
#include "mozilla/dom/TCPSocketEvent.h"
#include "mozilla/dom/TCPSocketEventBinding.h"
#include "mozilla/dom/ToJSValue.h"
#include "nsComponentManagerUtils.h"
#include "nsContentUtils.h"
#include "nsIArrayBufferInputStream.h"
#include "nsIAsyncInputStream.h"
#include "nsIAsyncStreamCopier.h"
#include "nsIBinaryInputStream.h"
#include "nsICancelable.h"
#include "nsIChannel.h"
#include "nsIInputStream.h"
#include "nsIInputStreamPump.h"
#include "nsIMultiplexInputStream.h"
#include "nsINSSErrorsService.h"
#include "nsIObserverService.h"
#include "nsIOutputStream.h"
#include "nsIProtocolProxyService.h"
#include "nsIScriptableInputStream.h"
#include "nsISocketTransport.h"
#include "nsISocketTransportService.h"
#include "nsISupportsPrimitives.h"
#include "nsITLSSocketControl.h"
#include "nsITlsHandshakeListener.h"
#include "nsITransport.h"
#include "nsIURIMutator.h"
#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsProxyRelease.h"
#include "nsServiceManagerUtils.h"
#include "nsStreamUtils.h"
#include "nsString.h"
#include "nsStringStream.h"
#include "secerr.h"
#include "sslerr.h"

using namespace mozilla::dom;

NS_IMPL_CYCLE_COLLECTION(LegacyMozTCPSocket, mGlobal)

NS_IMPL_CYCLE_COLLECTING_ADDREF(LegacyMozTCPSocket)
NS_IMPL_CYCLE_COLLECTING_RELEASE(LegacyMozTCPSocket)
NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(LegacyMozTCPSocket)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

LegacyMozTCPSocket::LegacyMozTCPSocket(nsPIDOMWindowInner* aWindow)
    : mGlobal(do_QueryInterface(aWindow)) {}

LegacyMozTCPSocket::~LegacyMozTCPSocket() = default;

already_AddRefed<TCPSocket> LegacyMozTCPSocket::Open(
    const nsAString& aHost, uint16_t aPort, const SocketOptions& aOptions,
    mozilla::ErrorResult& aRv) {
  AutoJSAPI api;
  if (NS_WARN_IF(!api.Init(mGlobal))) {
    aRv.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }
  GlobalObject globalObj(api.cx(), mGlobal->GetGlobalJSObject());
  return TCPSocket::Constructor(globalObj, aHost, aPort, aOptions, aRv);
}

already_AddRefed<TCPServerSocket> LegacyMozTCPSocket::Listen(
    uint16_t aPort, const ServerSocketOptions& aOptions, uint16_t aBacklog,
    mozilla::ErrorResult& aRv) {
  AutoJSAPI api;
  if (NS_WARN_IF(!api.Init(mGlobal))) {
    return nullptr;
  }
  GlobalObject globalObj(api.cx(), mGlobal->GetGlobalJSObject());
  return TCPServerSocket::Constructor(globalObj, aPort, aOptions, aBacklog,
                                      aRv);
}

bool LegacyMozTCPSocket::WrapObject(JSContext* aCx,
                                    JS::Handle<JSObject*> aGivenProto,
                                    JS::MutableHandle<JSObject*> aReflector) {
  return LegacyMozTCPSocket_Binding::Wrap(aCx, this, aGivenProto, aReflector);
}

NS_IMPL_CYCLE_COLLECTION_CLASS(TCPSocket)

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(TCPSocket, DOMEventTargetHelper)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(TCPSocket,
                                                  DOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mTransport)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSocketInputStream)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSocketOutputStream)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mInputStreamPump)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mInputStreamScriptable)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mInputStreamBinary)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mPendingDataAfterStartTLS)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSocketBridgeChild)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mSocketBridgeParent)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(TCPSocket, DOMEventTargetHelper)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mTransport)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mSocketInputStream)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mSocketOutputStream)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mInputStreamPump)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mInputStreamScriptable)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mInputStreamBinary)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mPendingDataAfterStartTLS)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mSocketBridgeChild)
  NS_IMPL_CYCLE_COLLECTION_UNLINK(mSocketBridgeParent)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_WEAK_REFERENCE
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_ADDREF_INHERITED(TCPSocket, DOMEventTargetHelper)
NS_IMPL_RELEASE_INHERITED(TCPSocket, DOMEventTargetHelper)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(TCPSocket)
  NS_INTERFACE_MAP_ENTRY(nsITransportEventSink)
  NS_INTERFACE_MAP_ENTRY(nsIRequestObserver)
  NS_INTERFACE_MAP_ENTRY(nsIStreamListener)
  NS_INTERFACE_MAP_ENTRY(nsIInputStreamCallback)
  NS_INTERFACE_MAP_ENTRY(nsIObserver)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
  NS_INTERFACE_MAP_ENTRY(nsITCPSocketCallback)
  NS_INTERFACE_MAP_ENTRY(nsIProtocolProxyCallback)
NS_INTERFACE_MAP_END_INHERITING(DOMEventTargetHelper)

TCPSocket::TCPSocket(nsIGlobalObject* aGlobal, const nsAString& aHost,
                     uint16_t aPort, bool aSsl, bool aUseArrayBuffers,
                     uint32_t aConnectionFlags)
    : DOMEventTargetHelper(aGlobal),
      mReadyState(TCPReadyState::Closed),
      mUseArrayBuffers(aUseArrayBuffers),
      mHost(aHost),
      mPort(aPort),
      mSsl(aSsl),
      mConnectionFlags(aConnectionFlags),
      mAsyncCopierActive(false),
      mWaitingForDrain(false),
      mInnerWindowID(0),
      mBufferedAmount(0),
      mSuspendCount(0),
      mTrackingNumber(0),
      mWaitingForStartTLS(false),
      mObserversActive(false) {
  if (aGlobal) {
    nsCOMPtr<nsPIDOMWindowInner> window = do_QueryInterface(aGlobal);
    if (window) {
      mInnerWindowID = window->WindowID();
    }
  }
}

TCPSocket::~TCPSocket() {
  if (mObserversActive) {
    nsCOMPtr<nsIObserverService> obs =
        do_GetService("@mozilla.org/observer-service;1");
    if (obs) {
      obs->RemoveObserver(this, "inner-window-destroyed");
      obs->RemoveObserver(this, "profile-change-net-teardown");
    }
  }
}

nsresult TCPSocket::CreateStream() {
  nsresult rv =
      mTransport->OpenInputStream(0, 0, 0, getter_AddRefs(mSocketInputStream));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = mTransport->OpenOutputStream(nsITransport::OPEN_UNBUFFERED, 0, 0,
                                    getter_AddRefs(mSocketOutputStream));
  NS_ENSURE_SUCCESS(rv, rv);

  // If the other side is not listening, or some other connection error
  // occurred, we will get an onInputStreamReady callback where available raises
  // to indicate the connection was refused.
  nsCOMPtr<nsIAsyncInputStream> asyncStream =
      do_QueryInterface(mSocketInputStream);
  NS_ENSURE_TRUE(asyncStream, NS_ERROR_NOT_AVAILABLE);

  nsCOMPtr<nsISerialEventTarget> mainTarget = GetMainThreadSerialEventTarget();
  rv = asyncStream->AsyncWait(this, nsIAsyncInputStream::WAIT_CLOSURE_ONLY, 0,
                              mainTarget);
  NS_ENSURE_SUCCESS(rv, rv);

  if (mUseArrayBuffers) {
    mInputStreamBinary =
        do_CreateInstance("@mozilla.org/binaryinputstream;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mInputStreamBinary->SetInputStream(mSocketInputStream);
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    mInputStreamScriptable =
        do_CreateInstance("@mozilla.org/scriptableinputstream;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mInputStreamScriptable->Init(mSocketInputStream);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return NS_OK;
}

nsresult TCPSocket::InitWithUnconnectedTransport(
    nsISocketTransport* aTransport) {
  mReadyState = TCPReadyState::Connecting;
  mTransport = aTransport;

  MOZ_ASSERT(XRE_GetProcessType() != GeckoProcessType_Content);

  nsCOMPtr<nsISerialEventTarget> mainTarget = GetMainThreadSerialEventTarget();
  mTransport->SetEventSink(this, mainTarget);

  nsresult rv = CreateStream();
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult TCPSocket::Init(nsIProxyInfo* aProxyInfo) {
  nsCOMPtr<nsIObserverService> obs =
      do_GetService("@mozilla.org/observer-service;1");
  if (obs) {
    mObserversActive = true;
    obs->AddObserver(this, "inner-window-destroyed", true);  // weak reference
    obs->AddObserver(this, "profile-change-net-teardown", true);  // weak ref
  }

  if (XRE_IsContentProcess()) {
    mReadyState = TCPReadyState::Connecting;

    nsCOMPtr<nsISerialEventTarget> target;
    if (nsCOMPtr<nsIGlobalObject> global = GetRelevantGlobal()) {
      target = global->SerialEventTarget();
    }
    mSocketBridgeChild = new TCPSocketChild(mHost, mPort, target);
    mSocketBridgeChild->SendOpen(this, mSsl, mUseArrayBuffers);
    return NS_OK;
  }

  nsCOMPtr<nsISocketTransportService> sts =
      do_GetService("@mozilla.org/network/socket-transport-service;1");

  AutoTArray<nsCString, 1> socketTypes;
  if (mSsl) {
    socketTypes.AppendElement("ssl"_ns);
  } else {
    socketTypes.AppendElement("starttls"_ns);
  }
  nsCOMPtr<nsISocketTransport> transport;
  nsresult rv =
      sts->CreateTransport(socketTypes, NS_ConvertUTF16toUTF8(mHost), mPort,
                           aProxyInfo, nullptr, getter_AddRefs(transport));
  NS_ENSURE_SUCCESS(rv, rv);

  // Must happen before InitWithUnconnectedTransport, which opens the streams
  // and thereby dispatches the connect to the socket thread.
  if (mConnectionFlags) {
    rv = transport->SetConnectionFlags(mConnectionFlags);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return InitWithUnconnectedTransport(transport);
}

void TCPSocket::InitWithSocketChild(TCPSocketChild* aSocketBridge) {
  mSocketBridgeChild = aSocketBridge;
  mReadyState = TCPReadyState::Open;
  mSocketBridgeChild->SetSocket(this);
  mSocketBridgeChild->GetHost(mHost);
  mSocketBridgeChild->GetPort(&mPort);
}

nsresult TCPSocket::InitWithTransport(nsISocketTransport* aTransport) {
  mTransport = aTransport;
  nsresult rv = CreateStream();
  NS_ENSURE_SUCCESS(rv, rv);

  mReadyState = TCPReadyState::Open;
  rv = CreateInputStreamPump();
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString host;
  mTransport->GetHost(host);
  CopyUTF8toUTF16(host, mHost);
  int32_t port;
  mTransport->GetPort(&port);
  mPort = port;

  return NS_OK;
}

void TCPSocket::UpgradeToSecure(mozilla::ErrorResult& aRv) {
  if (mReadyState != TCPReadyState::Open) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  if (mSsl) {
    return;
  }

  mSsl = true;

  if (mSocketBridgeChild) {
    mSocketBridgeChild->SendStartTLS();
    return;
  }

  if (!mAsyncCopierActive) {
    ActivateTLS();
  } else {
    mWaitingForStartTLS = true;
  }
}

namespace {
class CopierCallbacks final : public nsIRequestObserver {
  RefPtr<TCPSocket> mOwner;

 public:
  explicit CopierCallbacks(TCPSocket* aSocket) : mOwner(aSocket) {}

  NS_DECL_ISUPPORTS
  NS_DECL_NSIREQUESTOBSERVER
 private:
  ~CopierCallbacks() = default;
};

NS_IMPL_ISUPPORTS(CopierCallbacks, nsIRequestObserver)

NS_IMETHODIMP
CopierCallbacks::OnStartRequest(nsIRequest* aRequest) { return NS_OK; }

NS_IMETHODIMP
CopierCallbacks::OnStopRequest(nsIRequest* aRequest, nsresult aStatus) {
  mOwner->NotifyCopyComplete(aStatus);
  mOwner = nullptr;
  return NS_OK;
}
}  // unnamed namespace

void TCPSocket::CalculateBufferedAmount() {
  // Let's update the buffered amount of data.
  uint64_t bufferedAmount = 0;
  for (uint32_t i = 0, len = mPendingData.Length(); i < len; ++i) {
    nsCOMPtr<nsIInputStream> stream = mPendingData[i];
    uint64_t available = 0;
    if (NS_SUCCEEDED(stream->Available(&available))) {
      bufferedAmount += available;
    }
  }
  mBufferedAmount = bufferedAmount;
}

nsresult TCPSocket::EnsureCopying() {
  if (mAsyncCopierActive) {
    return NS_OK;
  }

  mAsyncCopierActive = true;

  nsresult rv;

  nsCOMPtr<nsIMultiplexInputStream> multiplexStream =
      do_CreateInstance("@mozilla.org/io/multiplex-input-stream;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIInputStream> stream = do_QueryInterface(multiplexStream);

  while (!mPendingData.IsEmpty()) {
    nsCOMPtr<nsIInputStream> stream = mPendingData[0];
    multiplexStream->AppendStream(stream);
    mPendingData.RemoveElementAt(0);
  }

  nsCOMPtr<nsIAsyncStreamCopier> copier =
      do_CreateInstance("@mozilla.org/network/async-stream-copier;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsISocketTransportService> sts =
      do_GetService("@mozilla.org/network/socket-transport-service;1");

  nsCOMPtr<nsISerialEventTarget> target = do_QueryInterface(sts);
  rv = copier->Init(stream, mSocketOutputStream, target,
                    true,               /* source buffered */
                    false,              /* sink buffered */
                    BUFFER_SIZE, false, /* close source */
                    false);             /* close sink */
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<CopierCallbacks> callbacks = new CopierCallbacks(this);
  rv = copier->AsyncCopy(callbacks, nullptr);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

void TCPSocket::NotifyCopyComplete(nsresult aStatus) {
  mAsyncCopierActive = false;
  CalculateBufferedAmount();

  if (mSocketBridgeParent && mSocketBridgeParent->IPCOpen()) {
    (void)mSocketBridgeParent->SendUpdateBufferedAmount(BufferedAmount(),
                                                        mTrackingNumber);
  }

  if (NS_FAILED(aStatus)) {
    MaybeReportErrorAndCloseIfOpen(aStatus);
    return;
  }

  if (BufferedAmount() != 0) {
    EnsureCopying();
    return;
  }

  // Maybe we have some empty stream. We want to have an empty queue now.
  mPendingData.Clear();

  // If we are waiting for initiating starttls, we can begin to
  // activate tls now.
  if (mWaitingForStartTLS && mReadyState == TCPReadyState::Open) {
    ActivateTLS();
    mWaitingForStartTLS = false;
    // If we have pending data, we should send them, or fire
    // a drain event if we are waiting for it.
    if (!mPendingDataAfterStartTLS.IsEmpty()) {
      mPendingData = std::move(mPendingDataAfterStartTLS);
      EnsureCopying();
      return;
    }
  }

  // If we have a connected child, we let the child decide whether
  // ondrain should be dispatched.
  if (mWaitingForDrain && !mSocketBridgeParent) {
    mWaitingForDrain = false;
    FireEvent(u"drain"_ns);
  }

  if (mReadyState == TCPReadyState::Closing) {
    if (mSocketOutputStream) {
      mSocketOutputStream->Close();
      mSocketOutputStream = nullptr;
    }
    mReadyState = TCPReadyState::Closed;
    FireEvent(u"close"_ns);
  }
}

void TCPSocket::ActivateTLS() {
  nsresult rv;
  nsCOMPtr<nsIEventTarget> socketThread =
      do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID, &rv);
  if (NS_FAILED(rv)) {
    return;
  }

  bool alreadyOnSTST = false;
  if (NS_FAILED(socketThread->IsOnCurrentThread(&alreadyOnSTST))) {
    return;
  }

  if (alreadyOnSTST) {
    ActivateTLSHelper();
  } else {
    // Since we use mozilla::SyncRunnable::DispatchToThread, it is unnecessary
    // to AddRef/Release TCPSocket in CallActivateTLS.
    auto CallActivateTLS = [&self = *this]() mutable {
      self.ActivateTLSHelper();
    };
    mozilla::SyncRunnable::DispatchToThread(
        socketThread,
        NS_NewRunnableFunction("TCPSocket::UpgradeToSecure->ActivateTLSHelper",
                               CallActivateTLS));
  }

  // Data buffered before TLS was enabled was received in plaintext, and must
  // not be delivered as if it had been received over TLS.
  DiscardPendingInput();
}

void TCPSocket::DiscardPendingInput() {
  if (!mSocketInputStream) {
    return;
  }
  uint64_t available = 0;
  while (NS_SUCCEEDED(mSocketInputStream->Available(&available)) &&
         available > 0) {
    uint32_t read = 0;
    nsresult rv = mSocketInputStream->ReadSegments(
        NS_DiscardSegment, nullptr,
        uint32_t(std::min<uint64_t>(available, UINT32_MAX)), &read);
    if (NS_FAILED(rv) || !read) {
      break;
    }
  }
}

void TCPSocket::ActivateTLSHelper() {
  nsCOMPtr<nsITLSSocketControl> tlsSocketControl;
  mTransport->GetTlsSocketControl(getter_AddRefs(tlsSocketControl));
  if (tlsSocketControl) {
    tlsSocketControl->StartTLS();
  }
}

NS_IMETHODIMP
TCPSocket::FireErrorEvent(const nsAString& aName, const nsAString& aType,
                          nsresult aErrorCode) {
  if (mSocketBridgeParent) {
    mSocketBridgeParent->FireErrorEvent(aName, aType, aErrorCode, mReadyState);
    return NS_OK;
  }

  TCPSocketErrorEventInit init;
  init.mBubbles = false;
  init.mCancelable = false;
  init.mName = aName;
  init.mMessage = aType;
  static_assert(std::is_same_v<std::underlying_type_t<nsresult>, uint32_t>);
  init.mErrorCode = uint32_t(aErrorCode);

  RefPtr<TCPSocketErrorEvent> event =
      TCPSocketErrorEvent::Constructor(this, u"error"_ns, init);
  MOZ_ASSERT(event);
  event->SetTrusted(true);
  DispatchEvent(*event);
  return NS_OK;
}

NS_IMETHODIMP
TCPSocket::FireEvent(const nsAString& aType) {
  if (mSocketBridgeParent) {
    mSocketBridgeParent->FireEvent(aType, mReadyState);
    return NS_OK;
  }

  AutoJSAPI api;
  if (NS_WARN_IF(!api.Init(GetRelevantGlobal()))) {
    return NS_ERROR_FAILURE;
  }
  JS::Rooted<JS::Value> val(api.cx());
  return FireDataEvent(api.cx(), aType, val);
}

NS_IMETHODIMP
TCPSocket::FireDataArrayEvent(const nsAString& aType,
                              const nsTArray<uint8_t>& buffer) {
  AutoJSAPI api;
  if (NS_WARN_IF(!api.Init(GetRelevantGlobal()))) {
    return NS_ERROR_FAILURE;
  }
  JSContext* cx = api.cx();
  JS::Rooted<JS::Value> val(cx);

  bool ok = IPC::DeserializeArrayBuffer(cx, buffer, &val);
  if (ok) {
    return FireDataEvent(cx, aType, val);
  }
  return NS_ERROR_FAILURE;
}

NS_IMETHODIMP
TCPSocket::FireDataStringEvent(const nsAString& aType,
                               const nsACString& aString) {
  AutoJSAPI api;
  if (NS_WARN_IF(!api.Init(GetRelevantGlobal()))) {
    return NS_ERROR_FAILURE;
  }
  JSContext* cx = api.cx();
  JS::Rooted<JS::Value> val(cx);

  bool ok = ToJSValue(cx, NS_ConvertASCIItoUTF16(aString), &val);
  if (ok) {
    return FireDataEvent(cx, aType, val);
  }
  return NS_ERROR_FAILURE;
}

nsresult TCPSocket::FireDataEvent(JSContext* aCx, const nsAString& aType,
                                  JS::Handle<JS::Value> aData) {
  MOZ_ASSERT(!mSocketBridgeParent);

  RootedDictionary<TCPSocketEventInit> init(aCx);
  init.mBubbles = false;
  init.mCancelable = false;
  init.mData = aData;

  RefPtr<TCPSocketEvent> event = TCPSocketEvent::Constructor(this, aType, init);
  event->SetTrusted(true);
  DispatchEvent(*event);
  return NS_OK;
}

JSObject* TCPSocket::WrapObject(JSContext* aCx,
                                JS::Handle<JSObject*> aGivenProto) {
  return TCPSocket_Binding::Wrap(aCx, this, aGivenProto);
}

void TCPSocket::GetHost(nsAString& aHost) { aHost.Assign(mHost); }

uint32_t TCPSocket::Port() const { return mPort; }

bool TCPSocket::Ssl() const { return mSsl; }

void TCPSocket::Suspend() {
  if (mSocketBridgeChild) {
    mSocketBridgeChild->SendSuspend();
    return;
  }
  if (mInputStreamPump) {
    mInputStreamPump->Suspend();
  }
  mSuspendCount++;
}

void TCPSocket::Resume(mozilla::ErrorResult& aRv) {
  if (mSocketBridgeChild) {
    mSocketBridgeChild->SendResume();
    return;
  }
  if (!mSuspendCount) {
    aRv.Throw(NS_ERROR_FAILURE);
    return;
  }

  if (mInputStreamPump) {
    mInputStreamPump->Resume();
  }
  mSuspendCount--;
}

nsresult TCPSocket::MaybeReportErrorAndCloseIfOpen(nsresult status) {
  // If we're closed, we've already reported the error or just don't need to
  // report the error.
  if (mReadyState == TCPReadyState::Closed) {
    return NS_OK;
  }

  // go through ::Closing state and then mark ::Closed
  Close();
  mReadyState = TCPReadyState::Closed;

  if (NS_FAILED(status)) {
    // Convert the status code to an appropriate error message.

    nsString errorType, errName;

    // security module? (and this is an error)
    if ((static_cast<uint32_t>(status) & 0xFF0000) == 0x5a0000) {
      nsCOMPtr<nsINSSErrorsService> errSvc =
          do_GetService("@mozilla.org/nss_errors_service;1");
      // getErrorClass will throw a generic NS_ERROR_FAILURE if the error code
      // is somehow not in the set of covered errors.
      uint32_t errorClass;
      nsresult rv = errSvc->GetErrorClass(status, &errorClass);
      if (NS_FAILED(rv)) {
        errorType.AssignLiteral("SecurityProtocol");
      } else {
        switch (errorClass) {
          case nsINSSErrorsService::ERROR_CLASS_BAD_CERT:
            errorType.AssignLiteral("SecurityCertificate");
            break;
          default:
            errorType.AssignLiteral("SecurityProtocol");
            break;
        }
      }

      // NSS_SEC errors (happen below the base value because of negative vals)
      if ((static_cast<int32_t>(status) & 0xFFFF) <
          abs(nsINSSErrorsService::NSS_SEC_ERROR_BASE)) {
        switch (static_cast<SECErrorCodes>(status)) {
          case SEC_ERROR_EXPIRED_CERTIFICATE:
            errName.AssignLiteral("SecurityExpiredCertificateError");
            break;
          case SEC_ERROR_REVOKED_CERTIFICATE:
            errName.AssignLiteral("SecurityRevokedCertificateError");
            break;
            // per bsmith, we will be unable to tell these errors apart very
            // soon, so it makes sense to just folder them all together already.
          case SEC_ERROR_UNKNOWN_ISSUER:
          case SEC_ERROR_UNTRUSTED_ISSUER:
          case SEC_ERROR_UNTRUSTED_CERT:
          case SEC_ERROR_CA_CERT_INVALID:
            errName.AssignLiteral("SecurityUntrustedCertificateIssuerError");
            break;
          case SEC_ERROR_INADEQUATE_KEY_USAGE:
            errName.AssignLiteral("SecurityInadequateKeyUsageError");
            break;
          case SEC_ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED:
            errName.AssignLiteral(
                "SecurityCertificateSignatureAlgorithmDisabledError");
            break;
          default:
            errName.AssignLiteral("SecurityError");
            break;
        }
      } else {
        // NSS_SSL errors
        switch (static_cast<SSLErrorCodes>(status)) {
          case SSL_ERROR_NO_CERTIFICATE:
            errName.AssignLiteral("SecurityNoCertificateError");
            break;
          case SSL_ERROR_BAD_CERTIFICATE:
            errName.AssignLiteral("SecurityBadCertificateError");
            break;
          case SSL_ERROR_UNSUPPORTED_CERTIFICATE_TYPE:
            errName.AssignLiteral("SecurityUnsupportedCertificateTypeError");
            break;
          case SSL_ERROR_UNSUPPORTED_VERSION:
            errName.AssignLiteral("SecurityUnsupportedTLSVersionError");
            break;
          case SSL_ERROR_BAD_CERT_DOMAIN:
            errName.AssignLiteral("SecurityCertificateDomainMismatchError");
            break;
          default:
            errName.AssignLiteral("SecurityError");
            break;
        }
      }
    } else {
      // must be network
      errorType.AssignLiteral("Network");

      switch (status) {
        // connect to host:port failed
        case NS_ERROR_CONNECTION_REFUSED:
          errName.AssignLiteral("ConnectionRefusedError");
          break;
          // network timeout error
        case NS_ERROR_NET_TIMEOUT:
          errName.AssignLiteral("NetworkTimeoutError");
          break;
          // hostname lookup failed
        case NS_ERROR_UNKNOWN_HOST:
          errName.AssignLiteral("DomainNotFoundError");
          break;
        case NS_ERROR_NET_INTERRUPT:
          errName.AssignLiteral("NetworkInterruptError");
          break;
        default:
          errName.AssignLiteral("NetworkError");
          break;
      }
    }

    (void)NS_WARN_IF(NS_FAILED(FireErrorEvent(errName, errorType, status)));
  }

  return FireEvent(u"close"_ns);
}

void TCPSocket::Close() { CloseHelper(true); }

void TCPSocket::CloseImmediately() { CloseHelper(false); }

void TCPSocket::CloseHelper(bool waitForUnsentData) {
  if (mReadyState == TCPReadyState::Closed ||
      mReadyState == TCPReadyState::Closing) {
    return;
  }

  mReadyState = TCPReadyState::Closing;

  if (mProxyRequest) {
    mProxyRequest->Cancel(NS_BINDING_ABORTED);
    mProxyRequest = nullptr;
  }

  if (mSsl) {
    ClearHandshakeCallbackListener();
  }

  if (mSocketBridgeChild) {
    mSocketBridgeChild->SendClose();
    return;
  }

  if (!mAsyncCopierActive || !waitForUnsentData) {
    mPendingData.Clear();
    mPendingDataAfterStartTLS.Clear();

    if (mSocketOutputStream) {
      mSocketOutputStream->Close();
      mSocketOutputStream = nullptr;
    }
  }

  if (mSocketInputStream) {
    mSocketInputStream->Close();
    mSocketInputStream = nullptr;
  }
}

bool TCPSocket::Send(const nsACString& aData, mozilla::ErrorResult& aRv) {
  if (mReadyState != TCPReadyState::Open) {
    aRv.Throw(NS_ERROR_FAILURE);
    return false;
  }

  uint64_t byteLength;
  nsCOMPtr<nsIInputStream> stream;
  if (mSocketBridgeChild) {
    mSocketBridgeChild->SendSend(aData);
    byteLength = aData.Length();
  } else {
    nsresult rv = NS_NewCStringInputStream(getter_AddRefs(stream), aData);
    if (NS_FAILED(rv)) {
      aRv.Throw(rv);
      return false;
    }
    rv = stream->Available(&byteLength);
    if (NS_FAILED(rv)) {
      aRv.Throw(rv);
      return false;
    }
  }
  return Send(stream, byteLength);
}

bool TCPSocket::Send(const ArrayBuffer& aData, uint32_t aByteOffset,
                     const Optional<uint32_t>& aByteLength,
                     mozilla::ErrorResult& aRv) {
  if (mReadyState != TCPReadyState::Open) {
    aRv.Throw(NS_ERROR_FAILURE);
    return false;
  }

  nsCOMPtr<nsIArrayBufferInputStream> stream;

  uint32_t nbytes;
  auto calculateOffsetAndCount = [&](uint32_t aLength) {
    uint32_t offset = std::min(aLength, aByteOffset);
    nbytes = std::min(aLength - aByteOffset,
                      aByteLength.WasPassed() ? aByteLength.Value() : aLength);
    return std::pair(offset, nbytes);
  };

  if (mSocketBridgeChild) {
    nsTArray<uint8_t> arrayBuffer;
    if (!aData.AppendDataTo(arrayBuffer, calculateOffsetAndCount)) {
      aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
      return false;
    }

    mSocketBridgeChild->SendSend(std::move(arrayBuffer));
  } else {
    mozilla::Maybe<mozilla::UniquePtr<uint8_t[]>> arrayBuffer =
        aData.CreateFromData<mozilla::UniquePtr<uint8_t[]>>(
            calculateOffsetAndCount);
    if (arrayBuffer.isNothing()) {
      aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
      return false;
    }

    stream = do_CreateInstance("@mozilla.org/io/arraybuffer-input-stream;1");
    nsresult rv = stream->SetData(arrayBuffer.extract(), nbytes);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      aRv.Throw(rv);
      return false;
    }
  }
  return Send(stream, nbytes);
}

bool TCPSocket::Send(nsIInputStream* aStream, uint32_t aByteLength) {
  uint64_t newBufferedAmount = BufferedAmount() + aByteLength;
  bool bufferFull = newBufferedAmount > BUFFER_SIZE;
  if (bufferFull) {
    // If we buffered more than some arbitrary amount of data,
    // (65535 right now) we should tell the caller so they can
    // wait until ondrain is called if they so desire. Once all the
    // buffered data has been written to the socket, ondrain is
    // called.
    mWaitingForDrain = true;
  }

  if (mSocketBridgeChild) {
    // In the child, we just add the buffer length to our bufferedAmount and let
    // the parent update our bufferedAmount when the data have been sent.
    mBufferedAmount = newBufferedAmount;
    return !bufferFull;
  }

  // This is used to track how many packets we have been told to send. Signaling
  // this back to the user of this API allows the user to know how many packets
  // are currently in flight over IPC.
  ++mTrackingNumber;
  if (mWaitingForStartTLS) {
    // When we are waiting for starttls, newStream is added to pendingData
    // and will be appended to multiplexStream after tls had been set up.
    mPendingDataAfterStartTLS.AppendElement(aStream);
  } else {
    mPendingData.AppendElement(aStream);
  }

  CalculateBufferedAmount();
  EnsureCopying();

  return !bufferFull;
}

TCPReadyState TCPSocket::ReadyState() { return mReadyState; }

TCPSocketBinaryType TCPSocket::BinaryType() const {
  if (mUseArrayBuffers) {
    return TCPSocketBinaryType::Arraybuffer;
  }
  return TCPSocketBinaryType::String;
}

already_AddRefed<TCPSocket> TCPSocket::CreateAcceptedSocket(
    nsIGlobalObject* aGlobal, nsISocketTransport* aTransport,
    bool aUseArrayBuffers) {
  RefPtr<TCPSocket> socket =
      new TCPSocket(aGlobal, u""_ns, 0, false, aUseArrayBuffers);
  nsresult rv = socket->InitWithTransport(aTransport);
  NS_ENSURE_SUCCESS(rv, nullptr);
  return socket.forget();
}

already_AddRefed<TCPSocket> TCPSocket::CreateAcceptedSocket(
    nsIGlobalObject* aGlobal, TCPSocketChild* aBridge, bool aUseArrayBuffers) {
  RefPtr<TCPSocket> socket =
      new TCPSocket(aGlobal, u""_ns, 0, false, aUseArrayBuffers);
  socket->InitWithSocketChild(aBridge);
  return socket.forget();
}

already_AddRefed<TCPSocket> TCPSocket::Constructor(
    const GlobalObject& aGlobal, const nsAString& aHost, uint16_t aPort,
    const SocketOptions& aOptions, mozilla::ErrorResult& aRv) {
  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(aGlobal.GetAsSupports());
  RefPtr<TCPSocket> socket =
      new TCPSocket(global, aHost, aPort, aOptions.mUseSecureTransport,
                    aOptions.mBinaryType == TCPSocketBinaryType::Arraybuffer,
                    aOptions.mConnectionFlags);
  socket->ResolveProxy();

  return socket.forget();
}

nsresult TCPSocket::ResolveProxy() {
  mReadyState = TCPReadyState::Connecting;

  nsresult rv;
  nsCOMPtr<nsIProtocolProxyService> pps =
      do_GetService(NS_PROTOCOLPROXYSERVICE_CONTRACTID, &rv);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsCOMPtr<nsIURI> uri;
  nsCString spec = mSsl ? "https://"_ns : "http://"_ns;
  bool maybeIPv6 = mHost.FindChar(':') != -1;
  if (maybeIPv6) spec.Append('[');
  if (!AppendUTF16toUTF8(mHost, spec, fallible)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  if (maybeIPv6) spec.Append(']');
  rv = NS_NewURI(getter_AddRefs(uri), spec);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }
  rv = NS_MutateURI(uri).SetPort(mPort).Finalize(uri);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsCOMPtr<nsIChannel> channel;
  rv = NS_NewChannel(getter_AddRefs(channel), uri,
                     nsContentUtils::GetSystemPrincipal(),
                     nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
                     nsIContentPolicy::TYPE_OTHER);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  rv = pps->AsyncResolve(channel,
                         nsIProtocolProxyService::RESOLVE_PREFER_SOCKS_PROXY,
                         this, nullptr, getter_AddRefs(mProxyRequest));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

NS_IMETHODIMP
TCPSocket::OnProxyAvailable(nsICancelable* aRequest, nsIChannel* aChannel,
                            nsIProxyInfo* aProxyInfo, nsresult aResult) {
  mProxyRequest = nullptr;
  if (NS_SUCCEEDED(aResult) && aProxyInfo) {
    nsCString proxyType;
    nsresult rv = aProxyInfo->GetType(proxyType);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      Close();
      return rv;
    }
    // Only supports SOCKS proxy for now.
    if (proxyType == "socks" || proxyType == "socks4") {
      return Init(aProxyInfo);
    }
  }
  return Init(nullptr);
}

nsresult TCPSocket::CreateInputStreamPump() {
  if (!mSocketInputStream) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  nsresult rv;
  mInputStreamPump =
      do_CreateInstance("@mozilla.org/network/input-stream-pump;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = mInputStreamPump->Init(mSocketInputStream, 0, 0, false, nullptr);
  NS_ENSURE_SUCCESS(rv, rv);

  uint64_t suspendCount = mSuspendCount;
  while (suspendCount--) {
    mInputStreamPump->Suspend();
  }

  rv = mInputStreamPump->AsyncRead(this);
  NS_ENSURE_SUCCESS(rv, rv);
  return NS_OK;
}

NS_IMETHODIMP
TCPSocket::OnTransportStatus(nsITransport* aTransport, nsresult aStatus,
                             int64_t aProgress, int64_t aProgressMax) {
  if (static_cast<uint32_t>(aStatus) !=
      static_cast<uint32_t>(nsISocketTransport::STATUS_CONNECTED_TO)) {
    return NS_OK;
  }

  if (mSsl) {
    // The TCP handshake is done, but the TLS handshake is still in flight.
    // Wait for HandshakeDone() before reporting "open".
    RegisterHandshakeCallbackListener();
    return NS_OK;
  }

  mReadyState = TCPReadyState::Open;
  nsresult rv = CreateInputStreamPump();
  NS_ENSURE_SUCCESS(rv, rv);
  FireEvent(u"open"_ns);

  return NS_OK;
}

namespace {
// nsITLSSocketControl's handshake-completion listener must live on the
// socket thread, but TCPSocket is cycle-collected and may only be refcounted
// on the main thread. This proxy bridges the two.
class TCPSocketTlsHandshakeListener final
    : public nsITlsHandshakeCallbackListener {
 public:
  explicit TCPSocketTlsHandshakeListener(TCPSocket* aSocket)
      : mSocket(new nsMainThreadPtrHolder<TCPSocket>(
            "TCPSocketTlsHandshakeListener::mSocket", aSocket)) {}

  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSITLSHANDSHAKECALLBACKLISTENER

 private:
  ~TCPSocketTlsHandshakeListener() = default;

  nsMainThreadPtrHandle<TCPSocket> mSocket;
};

NS_IMPL_ISUPPORTS(TCPSocketTlsHandshakeListener,
                  nsITlsHandshakeCallbackListener)

NS_IMETHODIMP
TCPSocketTlsHandshakeListener::HandshakeDone() {
  // Called on the socket thread; dispatch the rest to the main thread.
  nsCOMPtr<nsISerialEventTarget> mainTarget =
      mozilla::GetMainThreadSerialEventTarget();
  mainTarget->Dispatch(NS_NewRunnableFunction(
      "TCPSocketTlsHandshakeListener::HandshakeDone",
      [socket = mSocket]() { socket->HandshakeDoneOnMainThread(); }));
  return NS_OK;
}

NS_IMETHODIMP
TCPSocketTlsHandshakeListener::CertVerificationDone() { return NS_OK; }

NS_IMETHODIMP
TCPSocketTlsHandshakeListener::ClientAuthCertificateRequested() {
  return NS_OK;
}

NS_IMETHODIMP
TCPSocketTlsHandshakeListener::ClientAuthCertificateSelected() { return NS_OK; }
}  // namespace

void TCPSocket::RegisterHandshakeCallbackListener() {
  nsCOMPtr<nsITLSSocketControl> tlsSocketControl;
  mTransport->GetTlsSocketControl(getter_AddRefs(tlsSocketControl));
  if (!tlsSocketControl) {
    return;
  }

  nsresult rv;
  nsCOMPtr<nsIEventTarget> socketThread =
      do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID, &rv);
  if (NS_FAILED(rv)) {
    return;
  }

  RefPtr<TCPSocketTlsHandshakeListener> listener =
      new TCPSocketTlsHandshakeListener(this);

  socketThread->Dispatch(
      NS_NewRunnableFunction("TCPSocket::RegisterHandshakeCallbackListener",
                             [listener, tlsSocketControl]() {
                               tlsSocketControl->SetHandshakeCallbackListener(
                                   listener);
                             }),
      NS_DISPATCH_NORMAL);
}

void TCPSocket::ClearHandshakeCallbackListener() {
  if (!mTransport) {
    return;
  }

  nsCOMPtr<nsITLSSocketControl> tlsSocketControl;
  mTransport->GetTlsSocketControl(getter_AddRefs(tlsSocketControl));
  if (!tlsSocketControl) {
    return;
  }

  nsresult rv;
  nsCOMPtr<nsIEventTarget> socketThread =
      do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID, &rv);
  if (NS_FAILED(rv)) {
    return;
  }

  socketThread->Dispatch(
      NS_NewRunnableFunction("TCPSocket::ClearHandshakeCallbackListener",
                             [tlsSocketControl]() {
                               tlsSocketControl->SetHandshakeCallbackListener(
                                   nullptr);
                             }),
      NS_DISPATCH_NORMAL);
}

void TCPSocket::HandshakeDoneOnMainThread() {
  if (mReadyState != TCPReadyState::Connecting) {
    return;
  }

  mReadyState = TCPReadyState::Open;
  nsresult rv = CreateInputStreamPump();
  NS_ENSURE_SUCCESS_VOID(rv);
  FireEvent(u"open"_ns);
}

NS_IMETHODIMP
TCPSocket::OnInputStreamReady(nsIAsyncInputStream* aStream) {
  uint64_t dummy;
  nsresult rv = aStream->Available(&dummy);
  if (NS_FAILED(rv)) {
    MaybeReportErrorAndCloseIfOpen(rv);
  }
  return NS_OK;
}

NS_IMETHODIMP
TCPSocket::OnStartRequest(nsIRequest* aRequest) { return NS_OK; }

NS_IMETHODIMP
TCPSocket::OnDataAvailable(nsIRequest* aRequest, nsIInputStream* aStream,
                           uint64_t aOffset, uint32_t aCount) {
  if (mWaitingForStartTLS) {
    // Received in plaintext after TLS was requested but before it was enabled.
    uint32_t read;
    return aStream->ReadSegments(NS_DiscardSegment, nullptr, aCount, &read);
  }

  if (mUseArrayBuffers) {
    nsTArray<uint8_t> buffer;
    buffer.SetCapacity(aCount);
    uint32_t actual;
    nsresult rv = aStream->Read(reinterpret_cast<char*>(buffer.Elements()),
                                aCount, &actual);
    NS_ENSURE_SUCCESS(rv, rv);
    MOZ_ASSERT(actual == aCount);
    buffer.SetLength(actual);

    if (mSocketBridgeParent) {
      mSocketBridgeParent->FireArrayBufferDataEvent(buffer, mReadyState);
      return NS_OK;
    }

    AutoJSAPI api;
    if (!api.Init(GetRelevantGlobal())) {
      return NS_ERROR_FAILURE;
    }
    JSContext* cx = api.cx();

    JS::Rooted<JS::Value> value(cx);
    if (!ToJSValue(cx, TypedArrayCreator<ArrayBuffer>(buffer), &value)) {
      return NS_ERROR_FAILURE;
    }
    FireDataEvent(cx, u"data"_ns, value);
    return NS_OK;
  }

  nsCString data;
  nsresult rv = mInputStreamScriptable->ReadBytes(aCount, data);
  NS_ENSURE_SUCCESS(rv, rv);

  if (mSocketBridgeParent) {
    mSocketBridgeParent->FireStringDataEvent(data, mReadyState);
    return NS_OK;
  }

  AutoJSAPI api;
  if (!api.Init(GetRelevantGlobal())) {
    return NS_ERROR_FAILURE;
  }
  JSContext* cx = api.cx();

  JS::Rooted<JS::Value> value(cx);
  if (!ToJSValue(cx, NS_ConvertASCIItoUTF16(data), &value)) {
    return NS_ERROR_FAILURE;
  }
  FireDataEvent(cx, u"data"_ns, value);

  return NS_OK;
}

NS_IMETHODIMP
TCPSocket::OnStopRequest(nsIRequest* aRequest, nsresult aStatus) {
  mInputStreamPump = nullptr;

  if (mAsyncCopierActive && NS_SUCCEEDED(aStatus)) {
    // If we have some buffered output still, and status is not an
    // error, the other side has done a half-close, but we don't
    // want to be in the close state until we are done sending
    // everything that was buffered. We also don't want to call onclose
    // yet.
    return NS_OK;
  }

  // We call this even if there is no error.
  MaybeReportErrorAndCloseIfOpen(aStatus);
  return NS_OK;
}

void TCPSocket::SetSocketBridgeParent(TCPSocketParent* aBridgeParent) {
  MOZ_ASSERT(NS_IsMainThread());

  mSocketBridgeParent = aBridgeParent;
}

NS_IMETHODIMP
TCPSocket::UpdateReadyState(uint32_t aReadyState) {
  MOZ_ASSERT(mSocketBridgeChild);
  mReadyState = static_cast<TCPReadyState>(aReadyState);
  return NS_OK;
}

NS_IMETHODIMP
TCPSocket::UpdateBufferedAmount(uint32_t aBufferedAmount,
                                uint32_t aTrackingNumber) {
  if (aTrackingNumber != mTrackingNumber) {
    return NS_OK;
  }
  mBufferedAmount = aBufferedAmount;
  if (!mBufferedAmount) {
    if (mWaitingForDrain) {
      mWaitingForDrain = false;
      return FireEvent(u"drain"_ns);
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
TCPSocket::Observe(nsISupports* aSubject, const char* aTopic,
                   const char16_t* aData) {
  if (!strcmp(aTopic, "inner-window-destroyed")) {
    nsCOMPtr<nsISupportsPRUint64> wrapper = do_QueryInterface(aSubject);
    NS_ENSURE_TRUE(wrapper, NS_ERROR_FAILURE);
    uint64_t innerID;
    nsresult rv = wrapper->GetData(&innerID);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    if (innerID == mInnerWindowID) {
      Close();
    }
  } else if (!strcmp(aTopic, "profile-change-net-teardown")) {
    Close();
  }

  return NS_OK;
}

/* static */
bool TCPSocket::ShouldTCPSocketExist(JSContext* aCx, JSObject* aGlobal) {
  if (XRE_IsContentProcess() &&
      !StaticPrefs::dom_tcpsocket_in_child_enabled()) {
    return false;
  }
  JS::Rooted<JSObject*> global(aCx, aGlobal);
  return nsContentUtils::ObjectPrincipal(global)->IsSystemPrincipal();
}
