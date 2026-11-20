/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_WebTransportProxy_h
#define mozilla_net_WebTransportProxy_h

#include <functional>

#include "mozilla/Mutex.h"
#include "mozilla/dom/PWebTransport.h"
#include "nsIChannelEventSink.h"
#include "nsIInterfaceRequestor.h"
#include "nsIRedirectResultListener.h"
#include "nsIStreamListener.h"
#include "nsIWebTransport.h"

/*
 * WebTransportSessionProxy is introduced to enable the creation of a
 * Http3WebTransportSession and coordination of actions that are performed on
 * the main thread and on the socket thread.
 *
 * mChannel, mRedirectChannel, and mListener are used only on the main thread.
 *
 * mWebTransportSession is used only on the socket thread.
 *
 * mState and mSessionId are used on both threads, socket and main thread and it
 * is only used with a lock.
 *
 *
 * WebTransportSessionProxyState:
 * - INIT: before AsyncConnect is called.
 *
 * - NEGOTIATING: It is set during AsyncConnect. During this state HttpChannel
 *   is open but OnStartRequest has not been called yet. This state can
 *   transfer into:
 *    - NEGOTIATING_SUCCEEDED: when a Http3WebTransportSession has been
 *      negotiated.
 *    - DONE: when a WebTransport session has been canceled.
 *
 * - NEGOTIATING_SUCCEEDED: It is set during parsing of
 *   Http3WebTransportSession response when the response has been successful.
 *   mWebTransport is set to the Http3WebTransportSession at the same time the
 *   session changes to this state. This state can transfer into:
 *    - ACTIVE: during the OnStopRequest call if the WebTransport has not been
 *      canceled or failed for other reason, e.g. a browser shutdown or content
 *      blocking policies.
 *    - SESSION_CLOSE_PENDING: if the WebTransport has been canceled via an API
 *      call or content blocking policies. (the main thread initiated close).
 *    - CLOSE_CALLBACK_PENDING: if Http3WebTransportSession has been canceled
 *     due to a shutdown or a server closing a session. (the socket thread
 *     initiated close).
 *
 * - ACTIVE: In this state the session is negotiated and ready to use. This
 *   state can transfer into:
 *    - SESSION_CLOSE_PENDING: if the WebTransport has been canceled via an API
 *      call(nsIWebTransport::closeSession) or content blocking policies. (the
 *      main thread initiated close).
 *    - CLOSE_CALLBACK_PENDING: if Http3WebTransportSession has been canceled
 *      due to a shutdown or a server closing a session. (the socket thread
 *      initiated close).
 *
 * - CLOSE_CALLBACK_PENDING: This is the socket thread initiated close. In this
 *   state, the Http3WebTransportSession has been closed and a
 * CallOnSessionClosed call is dispatched to the main thread to call the
 * appropriate listener.
 *
 * - SESSION_CLOSE_PENDING: This is the main thread initiated close. In this
 * state, the WebTransport has been closed via an API call
 *   (nsIWebTransport::closeSession) and a CloseSessionInternal call is
 * dispatched to the socket thread to close the appropriate
 * Http3WebTransportSession.
 *
 * - DONE: everything has been cleaned up on both threads.
 *
 *
 * AsyncConnect creates mChannel on the main thread. Redirect callbacks are also
 * performed on the main thread (mRedirectChannel set and access only on the
 * main thread). Before this point, there are no activities on the socket thread
 * and Http3WebTransportSession is nullptr. mChannel is going to create a
 * nsHttpTransaction. The transaction will be dispatched on a nsAHttpConnection,
 * i.e. currently only the HTTP/3 version is implemented, therefore this will be
 * a HttpConnectionUDP and a Http3Session. The Http3Session creates a
 * Http3WebTransportSession. Until a response is received
 * Http3WebTransportSession is only accessed by Http3Session. During parsing of
 * a successful received from a server on the socket thread,
 * WebTransportSessionProxy::mWebTransportSession will take a reference to
 * Http3WebTransportSession and mState will be set to NEGOTIATING_SUCCEEDED.
 * From now on WebTransportSessionProxy is responsible for closing
 * Http3WebTransportSession if the closing of the session is initiated on the
 * main thread. OnStartRequest and OnStopRequest will be called on the main
 * thread. The session negotiation can have 2 outcomes:
 * - If both calls, i.e. OnStartRequest and OnStopRequest, indicate that the
 * request has succeeded and mState is NEGOTIATING_SUCCEEDED, the
 * mListener->OnSessionReady will be called during OnStopRequest.
 * - Otherwise, mListener->OnSessionClosed will be called, the state transferred
 * into SESSION_CLOSE_PENDING, and CloseSessionInternal will be dispatched to
 * the socket thread.
 *
 * CloseSession is called on the main thread. If the session is already closed
 * it returns an error. If the session is in state NEGOTIATING or
 * NEGOTIATING_SUCCEEDED mChannel will be canceled. If the session is in state
 * NEGOTIATING_SUCCEEDED or ACTIVE the state transferred into
 * SESSION_CLOSE_PENDING, and CloseSessionInternal will be dispatched to the
 * socket thread
 *
 * OnSessionReadyInternal is called on the socket thread. If mState is
 * NEGOTIATING the state will be set to NEGOTIATING_SUCCEEDED and mWebTransport
 * will be set to the newly negotiated Http3WebTransportSession. If mState is
 * DONE, the Http3WebTransportSession will be close.
 *
 * OnSessionClosed is called on the socket thread. mState will be set to
 * CLOSE_CALLBACK_PENDING and CallOnSessionClosed will be dispatched to the main
 * thread.
 *
 * mWebTransport is set during states NEGOTIATING_SUCCEEDED, ACTIVE and
 * SESSION_CLOSE_PENDING.
 */

namespace mozilla::net {

class WebTransportEventService;

class WebTransportStreamCallbackWrapper;

class WebTransportSessionProxy final
    : public nsIWebTransport,
      public WebTransportSessionEventListener,
      public WebTransportSessionEventListenerInternal,
      public WebTransportConnectionSettings,
      public nsIStreamListener,
      public nsIChannelEventSink,
      public nsIRedirectResultListener,
      public nsIInterfaceRequestor {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIWEBTRANSPORT
  NS_DECL_WEBTRANSPORTSESSIONEVENTLISTENER
  NS_DECL_WEBTRANSPORTSESSIONEVENTLISTENERINTERNAL
  NS_DECL_WEBTRANSPORTCONNECTIONSETTINGS
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER
  NS_DECL_NSICHANNELEVENTSINK
  NS_DECL_NSIREDIRECTRESULTLISTENER
  NS_DECL_NSIINTERFACEREQUESTOR

  WebTransportSessionProxy();

  bool CloseSessionAndGetStats(uint32_t aStatus, const nsACString& aReason,
                               mozilla::dom::WebTransportStatsData& aStats);

 private:
  ~WebTransportSessionProxy();

  void CloseSessionInternal();
  void CloseSessionInternalLocked();
  void CallOnSessionClosed();
  void CallOnSessionClosedLocked();

  enum WebTransportSessionProxyState {
    INIT,
    NEGOTIATING,
    NEGOTIATING_SUCCEEDED,
    ACTIVE,
    CLOSE_CALLBACK_PENDING,
    SESSION_CLOSE_PENDING,
    DONE,
  };
  mozilla::Mutex mMutex;
  WebTransportSessionProxyState mState MOZ_GUARDED_BY(mMutex) =
      WebTransportSessionProxyState::INIT;
  void ChangeState(WebTransportSessionProxyState newState);
  void CreateStreamInternal(nsIWebTransportStreamCallback* callback,
                            bool aBidi);
  void DoCreateStream(WebTransportStreamCallbackWrapper* aCallback,
                      WebTransportSessionBase* aSession, bool aBidi);
  void SendDatagramInternal(const RefPtr<WebTransportSessionBase>& aSession,
                            nsTArray<uint8_t>&& aData, uint64_t aTrackingId,
                            uint64_t aSendGroupId, int64_t aSendOrder,
                            TimeStamp aTimestamp);
  void NotifyDatagramReceived(nsTArray<uint8_t>&& aData);
  void GetMaxDatagramSizeInternal(
      const RefPtr<WebTransportSessionBase>& aSession);
  void OnMaxDatagramSizeInternal(uint64_t aSize);
  void OnDrainingInternal();
  void GetStatsInternal(const RefPtr<WebTransportSessionBase>& aSession);
  void OnStatsAvailableInternal(
      const Maybe<mozilla::dom::WebTransportStatsData>& aStats);
  void OnOutgoingDatagramOutComeInternal(
      uint64_t aId, WebTransportSessionEventListener::DatagramOutcome aOutCome);
  void OnStopSendingInternal(uint64_t aStreamId, uint64_t aErrorCode);
  void OnResetReceivedInternal(uint64_t aStreamId, uint64_t aErrorCode);
  void SetDatagramMaxBufferedDatagramsInternal(
      const RefPtr<WebTransportSessionBase>& aSession,
      uint32_t aMaxBufferedDatagrams);
  void SetDatagramMaxAgeInternal(
      const RefPtr<WebTransportSessionBase>& aSession, double aMaxAge);
  void SetAnticipatedConcurrentIncomingUnidirectionalStreamsInternal(
      const RefPtr<WebTransportSessionBase>& aSession, uint16_t aValue);
  void SetAnticipatedConcurrentIncomingBidirectionalStreamsInternal(
      const RefPtr<WebTransportSessionBase>& aSession, uint16_t aValue);

  nsCOMPtr<nsIChannel> mChannel;
  uint64_t mHttpChannelID = 0;
  nsCOMPtr<nsIChannel> mRedirectChannel;
  RefPtr<WebTransportEventService> mService;
  nsCOMPtr<WebTransportSessionEventListener> mListener MOZ_GUARDED_BY(mMutex);
  RefPtr<WebTransportSessionBase> mWebTransportSession MOZ_GUARDED_BY(mMutex);
  uint64_t mSessionId MOZ_GUARDED_BY(mMutex) = UINT64_MAX;
  uint32_t mCloseStatus MOZ_GUARDED_BY(mMutex) = 0;
  nsCString mReason MOZ_GUARDED_BY(mMutex);
  nsCString mProtocol MOZ_GUARDED_BY(mMutex);
  nsTArray<nsCString> mResponseHeaderNames MOZ_GUARDED_BY(mMutex);
  nsTArray<nsCString> mResponseHeaderValues MOZ_GUARDED_BY(mMutex);
  nsTArray<nsString> mOfferedProtocols MOZ_GUARDED_BY(mMutex);
  bool mCleanly MOZ_GUARDED_BY(mMutex) = false;
  bool mStopRequestCalled MOZ_GUARDED_BY(mMutex) = false;
  // This is used to store events happened before OnSessionReady.
  // Note that these events will be dispatched to the socket thread.
  nsTArray<std::function<void()>> mPendingEvents MOZ_GUARDED_BY(mMutex);
  nsTArray<std::function<void(nsresult)>> mPendingCreateStreamEvents
      MOZ_GUARDED_BY(mMutex);
  nsCOMPtr<nsIEventTarget> mTarget MOZ_GUARDED_BY(mMutex);
  nsTArray<RefPtr<nsIWebTransportHash>> mServerCertHashes
      MOZ_GUARDED_BY(mMutex);
  bool mDedicatedConnection = false;  // for WebTranport
  nsIWebTransport::HTTPVersion mHTTPVersion = nsIWebTransport::HTTPVersion::h3;
  // 0 means "not requested". Indistinguishable from an explicit 0, which is
  // harmless: neqo's limit is raise-only, so a 0 could never lower anything.
  uint16_t mAnticipatedConcurrentIncomingUnidirectionalStreams
      MOZ_GUARDED_BY(mMutex) = 0;
  uint16_t mAnticipatedConcurrentIncomingBidirectionalStreams
      MOZ_GUARDED_BY(mMutex) = 0;

  // Cached stats for when connection is closed (spec requirement)
  bool mHasCachedStats MOZ_GUARDED_BY(mMutex) = false;
  mozilla::dom::WebTransportStatsData mCachedStats MOZ_GUARDED_BY(mMutex);
};

}  // namespace mozilla::net

#endif  // mozilla_net_WebTransportProxy_h
