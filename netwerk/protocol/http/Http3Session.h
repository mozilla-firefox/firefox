/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef Http3Session_H_
#define Http3Session_H_

#include "HttpTrafficAnalyzer.h"
#include "mozilla/Array.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/dom/PWebTransport.h"
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
class nsHttpConnection;

// IID for the Http3Session interface
#define NS_HTTP3SESSION_IID \
  {0x8fc82aaf, 0xc4ef, 0x46ed, {0x89, 0x41, 0x93, 0x95, 0x8f, 0xac, 0x4f, 0x21}}

enum class EchExtensionStatus {
  kNotPresent,  // No ECH Extension was sent
  kGREASE,      // A GREASE ECH Extension was sent
  kReal         // A 'real' ECH Extension was sent
};

// An abstract layer for testing.
class Http3SessionBase {
 public:
  NS_INLINE_DECL_PURE_VIRTUAL_REFCOUNTING

  virtual nsresult TryActivating(const nsACString& aMethod,
                                 const nsACString& aScheme,
                                 const nsACString& aAuthorityHeader,
                                 const nsACString& aPath,
                                 const nsACString& aHeaders,
                                 uint64_t* aStreamId,
                                 Http3StreamBase* aStream) = 0;
  virtual void CloseSendingSide(uint64_t aStreamId) = 0;
  virtual void SendHTTPDatagram(uint64_t aStreamId, nsTArray<uint8_t>& aData,
                                uint64_t aTrackingId) = 0;
  virtual nsresult SendPriorityUpdateFrame(uint64_t aStreamId,
                                           uint8_t aPriorityUrgency,
                                           bool aPriorityIncremental) = 0;
  virtual void ConnectSlowConsumer(Http3StreamBase* stream) = 0;

  virtual nsresult SendRequestBody(uint64_t aStreamId, const char* buf,
                                   uint32_t count, uint32_t* countRead) = 0;
  virtual nsresult ReadResponseData(uint64_t aStreamId, char* aBuf,
                                    uint32_t aCount, uint32_t* aCountWritten,
                                    bool* aFin) = 0;
  virtual void FinishTunnelSetup(nsAHttpTransaction* aTransaction) = 0;

  virtual void CloseStream(Http3StreamBase* aStream, nsresult aResult) {}

  // For WebTransport
  virtual void CloseWebTransportConn() = 0;
  virtual void StreamHasDataToWrite(Http3StreamBase* aStream) = 0;
  virtual bool CloseWebTransport(
      uint64_t aSessionId, uint32_t aError, const nsACString& aMessage,
      mozilla::dom::WebTransportStatsData& aStats) = 0;
  virtual void SendDatagram(Http3WebTransportSession* aSession,
                            nsTArray<uint8_t>& aData, uint64_t aTrackingId,
                            uint64_t aSendGroupId, int64_t aSendOrder,
                            TimeStamp aTimestamp) = 0;
  virtual uint64_t MaxDatagramSize(uint64_t aSessionId) = 0;
  virtual nsresult ExportWebTransportKeyingMaterial(
      uint64_t aSessionId, const nsTArray<uint8_t>& aLabel,
      const nsTArray<uint8_t>& aContext, uint32_t aOutputLength,
      nsTArray<uint8_t>& aKeyingMaterial) = 0;
  virtual void SetDatagramMaxBufferedDatagrams(
      uint64_t aSessionId, uint32_t aMaxBufferedDatagrams) = 0;
  virtual void SetDatagramMaxAge(uint64_t aSessionId, double aMaxAge) = 0;
  virtual bool GetWebTransportSessionStats(
      uint64_t aSessionId, mozilla::dom::WebTransportStatsData& aStats) = 0;
  virtual nsresult RegisterWebTransportSendGroup(uint64_t aSessionId,
                                                 uint64_t aGroupId) = 0;
  virtual nsresult GetWebTransportSessionProtocol(uint64_t aSessionId,
                                                  nsACString& aProtocol) = 0;
  virtual void
  SetWebTransportAnticipatedConcurrentIncomingUnidirectionalStreams(
      uint64_t aSessionId, uint16_t aValue) = 0;
  virtual void SetWebTransportAnticipatedConcurrentIncomingBidirectionalStreams(
      uint64_t aSessionId, uint16_t aValue) = 0;
  virtual nsresult TryActivatingWebTransportStream(
      uint64_t* aStreamId, Http3StreamBase* aStream) = 0;
  virtual void ResetWebTransportStream(Http3WebTransportStream* aStream,
                                       uint64_t aErrorCode) = 0;
  virtual void StreamStopSending(Http3WebTransportStream* aStream,
                                 uint64_t aErrorCode) = 0;
  virtual void SetSendOrder(Http3StreamBase* aStream, int64_t aSendOrder) = 0;
  virtual void SetSendGroup(Http3StreamBase* aStream,
                            uint64_t aSendGroupId) = 0;
};

class Http3Session final : public Http3SessionBase,
                           public nsAHttpTransaction,
                           public nsAHttpConnection {
 public:
  NS_INLINE_DECL_STATIC_IID(NS_HTTP3SESSION_IID)

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSAHTTPTRANSACTION
  NS_DECL_NSAHTTPCONNECTION(mConnection)

  class OnQuicTimeout final : public nsITimerCallback, public nsINamed {
   public:
    NS_DECL_THREADSAFE_ISUPPORTS
    NS_DECL_NSITIMERCALLBACK
    NS_DECL_NSINAMED

    explicit OnQuicTimeout(HttpConnectionUDP* aConnection);

   private:
    ~OnQuicTimeout() = default;
    RefPtr<HttpConnectionUDP> mConnection;
  };

  Http3Session();
  nsresult Init(nsHttpConnectionInfo* aConnInfo, nsINetAddr* selfAddr,
                nsINetAddr* peerAddr, HttpConnectionUDP* udpConn,
                uint32_t aProviderFlags, nsIInterfaceRequestor* callbacks,
                nsIUDPSocket* socket, bool aIsTunnel = false);

  // Re-key this session's connection info after
  // nsHttpConnectionMgr::HandOffHttp3OnlyConnection has moved the owning
  // connection out of an Http3Policy::Only entry. The session keeps its own
  // clone, and connection-manager lookups made from here later on -- e.g.
  // AllowToRetryDifferentIPFamilyForHttp3 from Shutdown, or GetServerCertHashes
  // -- resolve an entry from its hash key, so it has to follow the connection.
  void RekeyAfterHttp3OnlyHandOff(nsHttpConnectionInfo* aConnInfo);

  bool IsConnected() const { return mState == CONNECTED; }
  bool CanSendData() const {
    return (mState == CONNECTED) || (mState == ZERORTT);
  }
  bool IsClosing() const { return (mState == CLOSING || mState == CLOSED); }
  bool IsClosed() const { return mState == CLOSED; }

  bool AddStream(nsAHttpTransaction* aHttpTransaction, int32_t aPriority,
                 nsIInterfaceRequestor* aCallbacks);

  // Swap the transaction backing an existing stream. Used by the HE /
  // 0-RTT adopt path: the HappyEyeballsTransaction shim was the key
  // under which AddStream registered the stream; after the real
  // nsHttpTransaction adopts it, we need both mStreamTransactionHash
  // and the stream's own mTransaction to point at the real txn so
  // CloseTransaction(real_txn) can find the stream. No-op if aOld
  // isn't in the hash.
  void SwapTransaction(nsAHttpTransaction* aOld, nsAHttpTransaction* aNew);

  bool CanReuse();

  // The following functions are used by Http3Stream and
  // Http3WebTransportSession:
  nsresult TryActivating(const nsACString& aMethod, const nsACString& aScheme,
                         const nsACString& aAuthorityHeader,
                         const nsACString& aPath, const nsACString& aHeaders,
                         uint64_t* aStreamId,
                         Http3StreamBase* aStream) override;
  // The folowing functions are used by Http3Stream:
  void CloseSendingSide(uint64_t aStreamId) override;
  nsresult SendRequestBody(uint64_t aStreamId, const char* buf, uint32_t count,
                           uint32_t* countRead) override;
  nsresult ReadResponseHeaders(uint64_t aStreamId,
                               nsTArray<uint8_t>& aResponseHeaders, bool* aFin);
  nsresult ReadResponseData(uint64_t aStreamId, char* aBuf, uint32_t aCount,
                            uint32_t* aCountWritten, bool* aFin) override;

  // The folowing functions are used by Http3WebTransportSession:
  bool CloseWebTransport(uint64_t aSessionId, uint32_t aError,
                         const nsACString& aMessage,
                         mozilla::dom::WebTransportStatsData& aStats) override;
  nsresult CreateWebTransportStream(uint64_t aSessionId,
                                    WebTransportStreamType aStreamType,
                                    uint64_t* aStreamId);
  void CloseStream(Http3StreamBase* aStream, nsresult aResult) override;
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
                                   bool aPriorityIncremental) override;

  void ConnectSlowConsumer(Http3StreamBase* stream) override;

  nsresult TryActivatingWebTransportStream(uint64_t* aStreamId,
                                           Http3StreamBase* aStream) override;
  void CloseWebTransportStream(Http3WebTransportStream* aStream,
                               nsresult aResult);
  void StreamHasDataToWrite(Http3StreamBase* aStream) override;
  void ResetWebTransportStream(Http3WebTransportStream* aStream,
                               uint64_t aErrorCode) override;
  void StreamStopSending(Http3WebTransportStream* aStream,
                         uint64_t aErrorCode) override;

  void SendDatagram(Http3WebTransportSession* aSession,
                    nsTArray<uint8_t>& aData, uint64_t aTrackingId,
                    uint64_t aSendGroupId, int64_t aSendOrder,
                    TimeStamp aTimestamp) override;
  void SendHTTPDatagram(uint64_t aStreamId, nsTArray<uint8_t>& aData,
                        uint64_t aTrackingId) override;

  uint64_t MaxDatagramSize(uint64_t aSessionId) override;
  nsresult ExportWebTransportKeyingMaterial(
      uint64_t aSessionId, const nsTArray<uint8_t>& aLabel,
      const nsTArray<uint8_t>& aContext, uint32_t aOutputLength,
      nsTArray<uint8_t>& aKeyingMaterial) override;
  nsresult RegisterWebTransportSendGroup(uint64_t aSessionId,
                                         uint64_t aGroupId) override;
  bool GetWebTransportSessionStats(
      uint64_t aSessionId,
      mozilla::dom::WebTransportStatsData& aStats) override;
  void SetDatagramMaxBufferedDatagrams(uint64_t aSessionId,
                                       uint32_t aMaxBufferedDatagrams) override;
  void SetDatagramMaxAge(uint64_t aSessionId, double aMaxAge) override;
  nsresult GetWebTransportSessionProtocol(uint64_t aSessionId,
                                          nsACString& aProtocol) override;
  void SetWebTransportAnticipatedConcurrentIncomingUnidirectionalStreams(
      uint64_t aSessionId, uint16_t aValue) override;
  void SetWebTransportAnticipatedConcurrentIncomingBidirectionalStreams(
      uint64_t aSessionId, uint16_t aValue) override;

  void SetSendOrder(Http3StreamBase* aStream, int64_t aSendOrder) override;
  void SetSendGroup(Http3StreamBase* aStream, uint64_t aSendGroupId) override;

  void CloseWebTransportConn() override;

  void FinishTunnelSetup(nsAHttpTransaction* aTransaction) override;

  Http3Stats GetStats();

  // For connect-udp
  already_AddRefed<HttpConnectionUDP> CreateTunnelStream(
      nsAHttpTransaction* aHttpTransaction, nsIInterfaceRequestor* aCallbacks);
  // For HTTP CONNECT
  already_AddRefed<nsHttpConnection> CreateTunnelStream(
      nsAHttpTransaction* aHttpTransaction, nsIInterfaceRequestor* aCallbacks,
      PRIntervalTime aRtt, bool aIsExtendedCONNECT);
  void SetIsInTunnel() { mIsInTunnel = true; }

  void SetDontExclude() { mDontExclude = true; }

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
  void AddStreamToReadyForWrite(Http3StreamBase*);
  void ProcessPending();
  void ApplyAnticipatedWebTransportStreams(Http3StreamBase* aStream,
                                           uint64_t aSessionId);

  void CallCertVerification(Maybe<nsCString> aEchPublicName);
  void SetSecInfo();

#ifndef ANDROID
  void EchOutcomeTelemetry();
#endif

  void StreamReadyToWrite(Http3StreamBase* aStream);
  void MaybeResumeSend();

  void CloseConnectionTelemetry(CloseError& aError, bool aClosing);
  void Finish0Rtt(bool aRestart);

  enum ZeroRttOutcome {
    NOT_USED,
    USED_SUCCEEDED,
    USED_REJECTED,
    USED_CONN_ERROR,
    USED_CONN_CLOSED_BY_NECKO
  };
  void ZeroRttTelemetry(ZeroRttOutcome aOutcome);

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

  // True if this http3 session uses NSPR for UDP IO.
  bool mUseNSPRForIO{true};

  RefPtr<HttpConnectionUDP> mUdpConn;

  nsCOMPtr<nsITimer> mTimer;
  RefPtr<OnQuicTimeout> mTimerCallback;

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
  // Copy of mConnInfo->GetIsTrrServiceChannel() that DontReuse can read off
  // the socket thread.
  Atomic<bool, Relaxed> mIsTrrServiceChannel{false};

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

  enum class ExtendedConnectKind : uint8_t {
    WebTransport = 0,
    ConnectUDP,
    // add more extended CONNECT protocols here
  };

  enum ExtendedConnectNegotiation { DISABLED, NEGOTIATING, FAILED, SUCCEEDED };

  struct ExtendedConnectState {
    ExtendedConnectNegotiation mStatus = DISABLED;
    nsTArray<WeakPtr<Http3StreamBase>> mWaiters;
  };

  Array<ExtendedConnectState, 2> mExtConnect{ExtendedConnectState{},
                                             ExtendedConnectState{}};

  // convenience accessors
  ExtendedConnectState& ExtState(ExtendedConnectKind aKind) {
    return mExtConnect[static_cast<size_t>(aKind)];
  }

  bool DeferIfNegotiating(ExtendedConnectKind aKind, Http3StreamBase* aStream);
  // 1795854 implement the case when WebTransport is not supported.
  // Also, implement the case when the  HTTP/3 session fails before settings
  // are exchanged.
  void FinishNegotiation(ExtendedConnectKind aKind, bool aSuccess);

  inline bool HasNoActiveStreams() const {
    return mStreamTransactionHash.Count() == 0 &&
           mWebTransportSessions.IsEmpty() && mWebTransportStreams.IsEmpty() &&
           mTunnelStreams.IsEmpty();
  }

  nsTArray<RefPtr<Http3StreamBase>> mWebTransportSessions;
  nsTArray<RefPtr<Http3StreamBase>> mWebTransportStreams;
  nsTArray<RefPtr<Http3StreamBase>> mTunnelStreams;

  bool mHasWebTransportSession = false;
  // When true, we don't add this connection info into the Http/3 excluded list.
  bool mDontExclude = false;
  // True if any stream accepted Do0RTT() during the ZERORTT phase.  When the
  // session closes with mBeforeConnectedError we suppress ExcludeHttp3: the
  // PSK ticket is single-use so the retry does a full handshake and the H3
  // server itself should still be reachable.
  bool mHad0RttStream = false;
  // The lifetime of the UDP socket is managed by the HttpConnectionUDP. This
  // is only used in Http3Session::ProcessOutput. Using raw pointer here to
  // improve performance.
  nsIUDPSocket* mSocket;
  uint32_t mTrrStreams = 0;
  bool mIsInTunnel = false;
};

}  // namespace mozilla::net

#endif  // Http3Session_H_
