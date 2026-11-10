/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_TCPSocket_h
#define mozilla_dom_TCPSocket_h

#include "js/RootingAPI.h"
#include "mozilla/DOMEventTargetHelper.h"
#include "mozilla/dom/TCPSocketBinding.h"
#include "mozilla/dom/TypedArray.h"
#include "nsIAsyncInputStream.h"
#include "nsIObserver.h"
#include "nsIProtocolProxyCallback.h"
#include "nsIProxyInfo.h"
#include "nsIStreamListener.h"
#include "nsISupportsImpl.h"
#include "nsITCPSocketCallback.h"
#include "nsITransport.h"
#include "nsWeakReference.h"

class nsISocketTransport;
class nsIInputStreamPump;
class nsIScriptableInputStream;
class nsIBinaryInputStream;
class nsIMultiplexInputStream;
class nsIAsyncStreamCopier;
class nsIInputStream;
class nsINetworkInfo;

namespace mozilla {
class ErrorResult;
namespace dom {

struct ServerSocketOptions;
class TCPServerSocket;
class TCPSocketChild;
class TCPSocketParent;

// This interface is only used for legacy navigator.mozTCPSocket API
// compatibility.
class LegacyMozTCPSocket : public nsISupports {
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_CLASS(LegacyMozTCPSocket)

  explicit LegacyMozTCPSocket(nsPIDOMWindowInner* aWindow);

  already_AddRefed<TCPServerSocket> Listen(uint16_t aPort,
                                           const ServerSocketOptions& aOptions,
                                           uint16_t aBacklog, ErrorResult& aRv);

  already_AddRefed<TCPSocket> Open(const nsAString& aHost, uint16_t aPort,
                                   const SocketOptions& aOptions,
                                   ErrorResult& aRv);

  bool WrapObject(JSContext* aCx, JS::Handle<JSObject*> aGivenProto,
                  JS::MutableHandle<JSObject*> aReflector);

 private:
  virtual ~LegacyMozTCPSocket();

  nsCOMPtr<nsIGlobalObject> mGlobal;
};

class TCPSocket final : public DOMEventTargetHelper,
                        public nsIStreamListener,
                        public nsITransportEventSink,
                        public nsIInputStreamCallback,
                        public nsIObserver,
                        public nsSupportsWeakReference,
                        public nsITCPSocketCallback,
                        public nsIProtocolProxyCallback {
 public:
  TCPSocket(nsIGlobalObject* aGlobal, const nsAString& aHost, uint16_t aPort,
            bool aSsl, bool aUseArrayBuffers, uint32_t aConnectionFlags = 0);

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS_INHERITED(TCPSocket,
                                                         DOMEventTargetHelper)
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER
  NS_DECL_NSITRANSPORTEVENTSINK
  NS_DECL_NSIINPUTSTREAMCALLBACK
  NS_DECL_NSIOBSERVER
  NS_DECL_NSITCPSOCKETCALLBACK
  NS_DECL_NSIPROTOCOLPROXYCALLBACK

  virtual JSObject* WrapObject(JSContext* aCx,
                               JS::Handle<JSObject*> aGivenProto) override;

  static bool ShouldTCPSocketExist(JSContext* aCx, JSObject* aGlobal);

  nsISocketTransport* GetTransport() const { return mTransport.get(); }

  void GetHost(nsAString& aHost);
  uint32_t Port() const;
  bool Ssl() const;
  uint64_t BufferedAmount() const { return mBufferedAmount; }
  void Suspend();
  void Resume(ErrorResult& aRv);
  void Close();
  void CloseImmediately();
  bool Send(const nsACString& aData, ErrorResult& aRv);
  bool Send(const ArrayBuffer& aData, uint32_t aByteOffset,
            const Optional<uint32_t>& aByteLength, ErrorResult& aRv);
  TCPReadyState ReadyState();
  TCPSocketBinaryType BinaryType() const;
  void UpgradeToSecure(ErrorResult& aRv);

  static already_AddRefed<TCPSocket> Constructor(const GlobalObject& aGlobal,
                                                 const nsAString& aHost,
                                                 uint16_t aPort,
                                                 const SocketOptions& aOptions,
                                                 ErrorResult& aRv);

  // Create a TCPSocket object from an existing low-level socket connection.
  // Used by the TCPServerSocket implementation when a new connection is
  // accepted.
  static already_AddRefed<TCPSocket> CreateAcceptedSocket(
      nsIGlobalObject* aGlobal, nsISocketTransport* aTransport,
      bool aUseArrayBuffers);
  // Create a TCPSocket object from an existing child-side IPC actor.
  // Used by the TCPServerSocketChild implementation when a new connection is
  // accepted.
  static already_AddRefed<TCPSocket> CreateAcceptedSocket(
      nsIGlobalObject* aGlobal, TCPSocketChild* aBridge, bool aUseArrayBuffers);

  // Initialize this socket's associated IPC actor in the parent process.
  void SetSocketBridgeParent(TCPSocketParent* aBridgeParent);

  static bool SocketEnabled();

  IMPL_EVENT_HANDLER(open);
  IMPL_EVENT_HANDLER(drain);
  IMPL_EVENT_HANDLER(data);
  IMPL_EVENT_HANDLER(error);
  IMPL_EVENT_HANDLER(close);

  nsresult Init(nsIProxyInfo* aProxyInfo);

  // Inform this socket that a buffered send() has completed sending.
  void NotifyCopyComplete(nsresult aStatus);

  // Complete the "open" transition once the TLS handshake has finished.
  // Called by TCPSocketTlsHandshakeListener on the main thread.
  void HandshakeDoneOnMainThread();

  // Initialize this socket from a low-level connection that hasn't connected
  // yet (called from RecvOpenBind() in TCPSocketParent).
  nsresult InitWithUnconnectedTransport(nsISocketTransport* aTransport);

 private:
  ~TCPSocket();

  // Initialize this socket with an existing IPC actor.
  void InitWithSocketChild(TCPSocketChild* aSocketBridge);
  // Initialize this socket from an existing low-level connection.
  nsresult InitWithTransport(nsISocketTransport* aTransport);
  // Initialize the input/output streams for this socket object.
  nsresult CreateStream();
  // Initialize the asynchronous read operation from this socket's input stream.
  nsresult CreateInputStreamPump();
  // Send the contents of the provided input stream, which is assumed to be the
  // given length for reporting and buffering purposes.
  bool Send(nsIInputStream* aStream, uint32_t aByteLength);
  // Begin an asynchronous copy operation if one is not already in progress.
  nsresult EnsureCopying();
  // Re-calculate buffered amount.
  void CalculateBufferedAmount();
  // Helper function, should be called by ActivateTLS(), only.
  void ActivateTLSHelper();
  // Enable TLS on this socket, dispatch to STSThread if necessary.
  void ActivateTLS();
  // Discard received data that has not been delivered yet.
  void DiscardPendingInput();
  // Register/clear this socket as the transport's TLS handshake-completion
  // listener. Dispatched to the socket thread, per nsITLSSocketControl's
  // threading contract.
  void RegisterHandshakeCallbackListener();
  void ClearHandshakeCallbackListener();
  // Dispatch an error event if necessary, then dispatch a "close" event.
  nsresult MaybeReportErrorAndCloseIfOpen(nsresult status);

  // Helper for FireDataStringEvent/FireDataArrayEvent.
  nsresult FireDataEvent(JSContext* aCx, const nsAString& aType,
                         JS::Handle<JS::Value> aData);
  // Helper for Close/CloseImmediately
  void CloseHelper(bool waitForUnsentData);

  nsresult ResolveProxy();

  TCPReadyState mReadyState;
  // Whether to use strings or array buffers for the "data" event.
  bool mUseArrayBuffers;
  nsString mHost;
  uint16_t mPort;
  // Whether this socket is using a secure transport.
  bool mSsl;
  // nsISocketTransport connection flags to apply before connecting.
  uint32_t mConnectionFlags;

  // The associated IPC actor in a child process.
  RefPtr<TCPSocketChild> mSocketBridgeChild;
  // The associated IPC actor in a parent process.
  RefPtr<TCPSocketParent> mSocketBridgeParent;

  // Raw socket streams
  nsCOMPtr<nsISocketTransport> mTransport;
  nsCOMPtr<nsIInputStream> mSocketInputStream;
  nsCOMPtr<nsIOutputStream> mSocketOutputStream;

  nsCOMPtr<nsICancelable> mProxyRequest;

  // Input stream machinery
  nsCOMPtr<nsIInputStreamPump> mInputStreamPump;
  nsCOMPtr<nsIScriptableInputStream> mInputStreamScriptable;
  nsCOMPtr<nsIBinaryInputStream> mInputStreamBinary;

  // Is there an async copy operation in progress?
  bool mAsyncCopierActive;
  // True if the buffer is full and a "drain" event is expected by the client.
  bool mWaitingForDrain;

  // The id of the window that created this socket.
  uint64_t mInnerWindowID;

  // The current number of buffered bytes. Only used in content processes when
  // IPC is enabled.
  uint64_t mBufferedAmount;

  // The number of times this socket has had `Suspend` called without a
  // corresponding `Resume`.
  uint32_t mSuspendCount;

  // The current sequence number (ie. number of send operations) that have been
  // processed. This is used in the IPC scenario by the child process to filter
  // out outdated notifications about the amount of buffered data present in the
  // parent process.
  uint32_t mTrackingNumber;

  // True if this socket has been upgraded to secure after the initial
  // connection, but the actual upgrade is waiting for an in-progress copy
  // operation to complete.
  bool mWaitingForStartTLS;
  // The buffered data awaiting the TLS upgrade to finish.
  nsTArray<nsCOMPtr<nsIInputStream>> mPendingDataAfterStartTLS;

  // The data to be sent.
  nsTArray<nsCOMPtr<nsIInputStream>> mPendingData;

  bool mObserversActive;
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_TCPSocket_h
