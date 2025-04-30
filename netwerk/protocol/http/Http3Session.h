/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=4 sw=2 et cindent: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef Http3Session_H__
#define Http3Session_H__

#include "HttpTrafficAnalyzer.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/net/NeqoHttp3Conn.h"
#include "nsAHttpConnection.h"
#include "nsDeque.h"
#include "nsISupportsImpl.h"
#include "nsITimer.h"
#include "nsIUDPSocket.h"
#include "nsRefPtrHashtable.h"
#include "nsTHashMap.h"
#include "nsWeakReference.h"

/*
 * WebTransport
 *
 * Http3Session and the underlying neqo code support multiplexing of multiple
 * WebTransport and multiplexing WebTransport sessions with regular HTTP
 * traffic. Whether WebTransport sessions are polled, will be controlled by the
 * nsHttpConnectionMgr.
 *
 * WebTransport support is negotiated using HTTP/3 setting. Before the settings
 * are available all WebTransport transactions are queued in
 * mWaitingForWebTransportNegotiation. The information on whether WebTransport
 * is supported is received via an HTTP/3 event. The event is
 * Http3Event::Tag::WebTransport  with the value
 * WebTransportEventExternal::Tag::Negotiated  that can be true or false. If
 * the WebTransport feature has been negotiated, queued transactions will be
 * activated otherwise they will be canceled(i.e. WebTransportNegotiationDone).
 *
 * The lifetime of a WebTransport session
 *
 * A WebTransport lifetime consists of 2 parts:
 * - WebTransport session setup
 * - WebTransport session active time
 *
 * WebTransport session setup:
 * A WebTransport session uses a regular HTTP request for negotiation.
 * Therefore when a new WebTransport is started a nsHttpChannel and the
 * corresponding nsHttpTransaction are created. The nsHttpTransaction is
 * dispatched to a Http3Session and after the
 * WebTransportEventExternal::Tag::Negotiated event it behaves almost the same
 * as a regular transaction, e.g. it is added to mStreamTransactionHash and
 * mStreamIdHash. For activating the session NeqoHttp3Conn::CreateWebTransport
 * is  called instead of NeqoHttp3Conn::Fetch(this is called for the regular
 * HTTP requests). In this phase, the WebTransport session is canceled in the
 * same way a regular request is canceled, by canceling the corresponding
 * nsHttpChannel. If HTTP/3 connection is closed in this phase the
 * corresponding nsHttpTransaction is canceled and this may cause the
 * transaction to be restarted (this is the existing restart logic) or the
 * error is propagated to the nsHttpChannel and its listener(via OnStartRequest
 * and OnStopRequest as the regular HTTP request).
 * The phase ends when a connection breaks or when the event
 * Http3Event::Tag::WebTransport with the value
 * WebTransportEventExternal::Tag::Session is received. The parameter
 * aData(from NeqoHttp3Conn::GetEvent) contain the HTTP head of the response.
 * The headers may be:
 * - failed code, i.e. anything except 200. In this case, the nsHttpTransaction
 *   behaves the same as a normal HTTP request and the nsHttpChannel listener
 *   will be informed via OnStartRequest and OnStopRequest calls.
 * - success code, i.e. 200. The code will be propagated to the
 *   nsHttpTransaction. The transaction will parse the header and call
 *   Http3Session::GetWebTransportSession. The function transfers WebTransport
 *   session into the next phase:
 *   - Removes nsHttpTransaction from mStreamTransactionHash.
 *   - Adds the stream to mWebTransportSessions
 *   - The nsHttpTransaction supplies Http3WebTransportSession to the
 *     WebTransportSessionProxy and vice versa.
 *     TODO remove this circular referencing.
 *
 * WebTransport session active time:
 * During this phase the following actions are possible:
 * - Cancelling a WebTransport session by the application:
 *   The application calls Http3WebTransportSession::CloseSession. This
 *   transfers Http3WebTransportSession into the CLOSE_PENDING state and calls
 *   Http3Session::ConnectSlowConsumer to add itself to the “ready for reading
 *   queue”. Consequently, the Http3Session will call
 *   Http3WebTransportSession::WriteSegments and
 *   Http3Session::CloseWebTransport will be called to send the closing signal
 *   to the peer. After this, the Http3WebTransportSession is in the state DONE
 *   and it will be removed from the Http3Session(the CloseStream function
 *   takes care of this).
 * - The peer sending a session closing signal:
 *   Http3Session will receive a Http3Event::Tag::WebTransport event with value
 *   WebTransportEventExternal::Tag::SessionClosed. The
 *   Http3WebTransportSession::OnSessionClosed function for the corresponding
 *   stream wil be called. The function will inform the corresponding
 *   WebTransportSessionProxy by calling OnSessionClosed function. The
 *   Http3WebTransportSession is in the state DONE and will be removed from the
 *   Http3Session(the CloseStream function takes care of this).
 */

namespace mozilla::net {

class HttpConnectionUDP;
class Http3StreamBase;
class QuicSocketControl;
class Http3WebTransportSession;
class Http3WebTransportStream;

// IID for the Http3Session interface
#define NS_HTTP3SESSION_IID \
  {0x8fc82aaf, 0xc4ef, 0x46ed, {0x89, 0x41, 0x93, 0x95, 0x8f, 0xac, 0x4f, 0x21}}

enum class EchExtensionStatus {
  kNotPresent,  // No ECH Extension was sent
  kGREASE,      // A GREASE ECH Extension was sent
  kReal         // A 'real' ECH Extension was sent
};

class Http3Session final : public nsAHttpTransaction, public nsAHttpConnection {
 public:
  NS_DECLARE_STATIC_IID_ACCESSOR(NS_HTTP3SESSION_IID)

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSAHTTPTRANSACTION
  NS_DECL_NSAHTTPCONNECTION(mConnection)

  Http3Session();
  nsresult Init(const nsHttpConnectionInfo* aConnInfo, nsINetAddr* selfAddr,
                nsINetAddr* peerAddr, HttpConnectionUDP* udpConn,
                uint32_t aProviderFlags, nsIInterfaceRequestor* callbacks,
                nsIUDPSocket* socket);

  bool IsConnected() const { return mState == CONNECTED; }
  bool CanSendData() const {
    return (mState == CONNECTED) || (mState == ZERORTT);
  }
  bool IsClosing() const { return (mState == CLOSING || mState == CLOSED); }
  bool IsClosed() const { return mState == CLOSED; }

  bool AddStream(nsAHttpTransaction* aHttpTransaction, int32_t aPriority,
                 nsIInterfaceRequestor* aCallbacks);

  bool CanReuse();

  // The following functions are used by Http3Stream and
  // Http3WebTransportSession:
  nsresult TryActivating(const nsACString& aMethod, const nsACString& aScheme,
                         const nsACString& aAuthorityHeader,
                         const nsACString& aPath, const nsACString& aHeaders,
                         uint64_t* aStreamId, Http3StreamBase* aStream);
  // The folowing functions are used by Http3Stream:
  void CloseSendingSide(uint64_t aStreamId);
  nsresult SendRequestBody(uint64_t aStreamId, const char* buf, uint32_t count,
                           uint32_t* countRead);
  nsresult ReadResponseHeaders(uint64_t aStreamId,
                               nsTArray<uint8_t>& aResponseHeaders, bool* aFin);
  nsresult ReadResponseData(uint64_t aStreamId, char* aBuf, uint32_t aCount,
                            uint32_t* aCountWritten, bool* aFin);

  // The folowing functions are used by Http3WebTransportSession:
  nsresult CloseWebTransport(uint64_t aSessionId, uint32_t aError,
                             const nsACString& aMessage);
  nsresult CreateWebTransportStream(uint64_t aSessionId,
                                    WebTransportStreamType aStreamType,
                                    uint64_t* aStreamId);
  void CloseStream(Http3StreamBase* aStream, nsresult aResult);
  void CloseStreamInternal(Http3StreamBase* aStream, nsresult aResult);

  void SetCleanShutdown(bool aCleanShutdown) {
    mCleanShutdown = aCleanShutdown;
  }

  bool TestJoinConnection(const nsACString& hostname, int32_t port);
  bool JoinConnection(const nsACString& hostname, int32_t port);

  void TransactionHasDataToWrite(nsAHttpTransaction* caller) override;
  void TransactionHasDataToRecv(nsAHttpTransaction* caller) override;
  [[nodiscard]] nsresult GetTransactionTLSSocketControl(
      nsITLSSocketControl**) override;

  // This function will be called by QuicSocketControl when the certificate
  // verification is done.
  void Authenticated(int32_t aError, bool aServCertHashesSucceeded = false);

  nsresult ProcessOutputAndEvents(nsIUDPSocket* socket);

  void ReportHttp3Connection();

  int64_t GetBytesWritten() { return mTotalBytesWritten; }
  int64_t BytesRead() { return mTotalBytesRead; }

  nsresult SendData(nsIUDPSocket* socket);
  nsresult RecvData(nsIUDPSocket* socket);

  void DoSetEchConfig(const nsACString& aEchConfig);

  nsresult SendPriorityUpdateFrame(uint64_t aStreamId, uint8_t aPriorityUrgency,
                                   bool aPriorityIncremental);

  void ConnectSlowConsumer(Http3StreamBase* stream);

  nsresult TryActivatingWebTransportStream(uint64_t* aStreamId,
                                           Http3StreamBase* aStream);
  void CloseWebTransportStream(Http3WebTransportStream* aStream,
                               nsresult aResult);
  void StreamHasDataToWrite(Http3StreamBase* aStream);
  void ResetWebTransportStream(Http3WebTransportStream* aStream,
                               uint64_t aErrorCode);
  void StreamStopSending(Http3WebTransportStream* aStream, uint8_t aErrorCode);

  void SendDatagram(Http3WebTransportSession* aSession,
                    nsTArray<uint8_t>& aData, uint64_t aTrackingId);

  uint64_t MaxDatagramSize(uint64_t aSessionId);

  void SetSendOrder(Http3StreamBase* aStream, Maybe<int64_t> aSendOrder);

  void CloseWebTransportConn();

  Http3Stats GetStats();

 private:
  ~Http3Session();

  void CloseInternal(bool aCallNeqoClose);
  void Shutdown();

  bool RealJoinConnection(const nsACString& hostname, int32_t port,
                          bool justKidding);

  nsresult ProcessOutput(nsIUDPSocket* socket);
  nsresult ProcessInput(nsIUDPSocket* socket);
  nsresult ProcessEvents();

  nsresult ProcessTransactionRead(uint64_t stream_id);
  nsresult ProcessTransactionRead(Http3StreamBase* stream);
  nsresult ProcessSlowConsumers();

  void SetupTimer(uint64_t aTimeout);

  enum ResetType {
    RESET,
    STOP_SENDING,
  };
  void ResetOrStopSendingRecvd(uint64_t aStreamId, uint64_t aError,
                               ResetType aType);

  void QueueStream(Http3StreamBase* stream);
  void RemoveStreamFromQueues(Http3StreamBase*);
  void ProcessPending();

  void CallCertVerification(Maybe<nsCString> aEchPublicName);
  void SetSecInfo();

#ifndef ANDROID
  void EchOutcomeTelemetry();
#endif

  void StreamReadyToWrite(Http3StreamBase* aStream);
  void MaybeResumeSend();

  void CloseConnectionTelemetry(CloseError& aError, bool aClosing);
  void Finish0Rtt(bool aRestart);

#ifndef ANDROID
  enum ZeroRttOutcome {
    NOT_USED,
    USED_SUCCEEDED,
    USED_REJECTED,
    USED_CONN_ERROR,
    USED_CONN_CLOSED_BY_NECKO
  };
  void ZeroRttTelemetry(ZeroRttOutcome aOutcome);
#endif

  RefPtr<NeqoHttp3Conn> mHttp3Connection;
  RefPtr<nsAHttpConnection> mConnection;
  // We need an extra map to store the mapping of WebTransportSession and
  // WebTransportStreams to handle the case that a stream is already removed
  // from mStreamIdHash and we still need the WebTransportSession.
  nsTHashMap<nsUint64HashKey, uint64_t> mWebTransportStreamToSessionMap;
  nsRefPtrHashtable<nsUint64HashKey, Http3StreamBase> mStreamIdHash;
  nsRefPtrHashtable<nsPtrHashKey<nsAHttpTransaction>, Http3StreamBase>
      mStreamTransactionHash;

  nsRefPtrDeque<Http3StreamBase> mReadyForWrite;

  nsTArray<RefPtr<Http3StreamBase>> mSlowConsumersReadyForRead;
  nsRefPtrDeque<Http3StreamBase> mQueuedStreams;

  enum State {
    INITIALIZING,
    ZERORTT,
    CONNECTED,
    CLOSING,
    CLOSED
  } mState{INITIALIZING};

  bool mAuthenticationStarted{false};
  bool mCleanShutdown{false};
  bool mGoawayReceived{false};
  bool mShouldClose{false};
  bool mIsClosedByNeqo{false};
  bool mHttp3ConnectionReported = false;
  // mError is neqo error (a protocol error) and that may mean that we will
  // send some packets after that.
  nsresult mError{NS_OK};
  // This is a socket error, there is no poioint in sending anything on that
  // socket.
  nsresult mSocketError{NS_OK};
  bool mBeforeConnectedError{false};
  uint64_t mCurrentBrowserId;

  // True if the mTimer is inited and waiting for firing.
  bool mTimerActive{false};

  // True if this http3 session uses NSPR for UDP IO.
  bool mUseNSPRForIO{true};

  RefPtr<HttpConnectionUDP> mUdpConn;

  nsCOMPtr<nsITimer> mTimer;

  nsTHashMap<nsCStringHashKey, bool> mJoinConnectionCache;

  RefPtr<QuicSocketControl> mSocketControl;

  uint64_t mTransactionCount = 0;

  // The stream(s) that we are getting 0RTT data from.
  nsTArray<WeakPtr<Http3StreamBase>> m0RTTStreams;
  // The stream(s) that are not able to send 0RTT data. We need to
  // remember them put them into mReadyForWrite queue when 0RTT finishes.
  nsTArray<WeakPtr<Http3StreamBase>> mCannotDo0RTTStreams;

  // The following variables are needed for telemetry.
  TimeStamp mConnectionIdleStart;
  TimeStamp mConnectionIdleEnd;
  Maybe<uint64_t> mFirstStreamIdReuseIdleConnection;
  TimeStamp mTimerShouldTrigger;
  TimeStamp mZeroRttStarted;
  TimeStamp mLastTRRResponseTime;  // Time of the last successful TRR response
  uint64_t mBlockedByStreamLimitCount = 0;
  uint64_t mTransactionsBlockedByStreamLimitCount = 0;
  uint64_t mTransactionsSenderBlockedByFlowControlCount = 0;

  // NS_NET_STATUS_CONNECTED_TO event will be created by the Http3Session.
  // We want to  propagate it to the first transaction.
  RefPtr<nsHttpTransaction> mFirstHttpTransaction;

  RefPtr<nsHttpConnectionInfo> mConnInfo;

  int64_t mTotalBytesRead = 0;     // total data read
  int64_t mTotalBytesWritten = 0;  // total data read
  PRIntervalTime mLastWriteTime = 0;
  nsCString mServer;

  // Records whether we sent an ECH Extension and whether it was a GREASE Xtn
  EchExtensionStatus mEchExtensionStatus = EchExtensionStatus::kNotPresent;

  // Records whether the handshake finished successfully and we established a
  // a connection.
  bool mHandshakeSucceeded = false;

  nsCOMPtr<nsINetAddr> mNetAddr;

  enum WebTransportNegotiation { DISABLED, NEGOTIATING, FAILED, SUCCEEDED };
  WebTransportNegotiation mWebTransportNegotiationStatus{
      WebTransportNegotiation::DISABLED};

  nsTArray<WeakPtr<Http3StreamBase>> mWaitingForWebTransportNegotiation;
  // 1795854 implement the case when WebTransport is not supported.
  // Also, implement the case when the  HTTP/3 session fails before settings
  // are exchanged.
  void WebTransportNegotiationDone();

  nsTArray<RefPtr<Http3StreamBase>> mWebTransportSessions;
  nsTArray<RefPtr<Http3StreamBase>> mWebTransportStreams;

  bool mHasWebTransportSession = false;
  // When true, we don't add this connection info into the Http/3 excluded list.
  bool mDontExclude = false;
  // The lifetime of the UDP socket is managed by the HttpConnectionUDP. This
  // is only used in Http3Session::ProcessOutput. Using raw pointer here to
  // improve performance.
  nsIUDPSocket* mSocket;
};

NS_DEFINE_STATIC_IID_ACCESSOR(Http3Session, NS_HTTP3SESSION_IID);

}  // namespace mozilla::net

#endif  // Http3Session_H__
