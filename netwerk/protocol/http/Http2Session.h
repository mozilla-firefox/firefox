/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_Http2Session_h
#define mozilla_net_Http2Session_h

// HTTP/2 - RFC 7540
// https://www.rfc-editor.org/rfc/rfc7540.txt

#include "ASpdySession.h"
#include "Http2Compression.h"
#include "mozilla/Queue.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/WeakPtr.h"
#include "nsAHttpConnection.h"
#include "nsCOMArray.h"
#include "nsDeque.h"
#include "nsHashKeys.h"
#include "nsHttpRequestHead.h"
#include "nsICacheEntryOpenCallback.h"
#include "nsRefPtrHashtable.h"
#include "nsTHashMap.h"

class nsISocketTransport;

namespace mozilla {
namespace net {

class Http2PushedStream;
class Http2StreamBase;
class Http2StreamTunnel;
class nsHttpTransaction;
class nsHttpConnection;

enum Http2StreamBaseType { Normal, WebSocket, Tunnel, ServerPush };
enum class ExtendedCONNECTType : uint8_t { Proxy, WebSocket, WebTransport };
enum class Http2StreamQueueType {
  ReadyForWrite = 0,
  QueuedStreams,
  SlowConsumersReadyForRead
};

class Http2StreamQueueManager final {
 public:
  void AddStreamToQueue(Http2StreamQueueType aType, Http2StreamBase* aStream);
  void RemoveStreamFromAllQueue(Http2StreamBase* aStream);
  already_AddRefed<Http2StreamBase> GetNextStreamFromQueue(
      Http2StreamQueueType aType);

  uint32_t GetWriteQueueSize() const { return mReadyForWrite.Count(); }

 private:
  using StreamQueue = mozilla::Queue<WeakPtr<Http2StreamBase>>;

  StreamQueue& GetQueue(Http2StreamQueueType aType);
  bool GetQueueFlag(Http2StreamQueueType aType, Http2StreamBase* aStream);
  void SetQueueFlag(Http2StreamQueueType aType, Http2StreamBase* aStream,
                    bool value);

  StreamQueue mReadyForWrite;
  StreamQueue mQueuedStreams;
  StreamQueue mSlowConsumersReadyForRead;
};

// b23b147c-c4f8-4d6e-841a-09f29a010de7
#define NS_HTTP2SESSION_IID \
  {0xb23b147c, 0xc4f8, 0x4d6e, {0x84, 0x1a, 0x09, 0xf2, 0x9a, 0x01, 0x0d, 0xe7}}

class Http2Session final : public ASpdySession,
                           public nsAHttpConnection,
                           public nsAHttpSegmentReader,
                           public nsAHttpSegmentWriter {
  ~Http2Session();

 public:
  NS_INLINE_DECL_STATIC_IID(NS_HTTP2SESSION_IID)

  NS_DECL_ISUPPORTS_INHERITED
  NS_DECL_NSAHTTPTRANSACTION
  NS_DECL_NSAHTTPCONNECTION(mConnection)
  NS_DECL_NSAHTTPSEGMENTREADER
  NS_DECL_NSAHTTPSEGMENTWRITER

  static Http2Session* CreateSession(nsISocketTransport*,
                                     enum SpdyVersion version,
                                     bool attemptingEarlyData);

  [[nodiscard]] bool AddStream(nsAHttpTransaction*, int32_t,
                               nsIInterfaceRequestor*) override;

  // Same semantics as Http3Session::SwapTransaction. Used by the HE /
  // 0-RTT adopt path to re-key an existing stream from the
  // HappyEyeballsTransaction shim to the real nsHttpTransaction.
  void SwapTransaction(nsAHttpTransaction* aOld, nsAHttpTransaction* aNew);
  bool CanReuse() override { return !mShouldGoAway && !mClosed; }
  bool RoomForMoreStreams() override;
  enum SpdyVersion SpdyVersion() override;
  bool TestJoinConnection(const nsACString& hostname, int32_t port) override;
  bool JoinConnection(const nsACString& hostname, int32_t port) override;

  // When the connection is active this is called up to once every 1 second
  // return the interval (in seconds) that the connection next wants to
  // have this invoked. It might happen sooner depending on the needs of
  // other connections.
  uint32_t ReadTimeoutTick(PRIntervalTime now) override;

  // Idle time represents time since "goodput".. e.g. a data or header frame
  PRIntervalTime IdleTime() override;

  // Registering with a newID of 0 means pick the next available odd ID
  uint32_t RegisterStreamID(Http2StreamBase*, uint32_t aNewID = 0);

  /*
    HTTP/2 framing

    0                   1                   2                   3
    0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
    +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
    |         Length (16)           |   Type (8)    |   Flags (8)   |
    +-+-------------+---------------+-------------------------------+
    |R|                 Stream Identifier (31)                      |
    +-+-------------------------------------------------------------+
    |                     Frame Data (0...)                       ...
    +---------------------------------------------------------------+
  */

  enum FrameType {
    FRAME_TYPE_DATA = 0x0,
    FRAME_TYPE_HEADERS = 0x1,
    FRAME_TYPE_PRIORITY = 0x2,
    FRAME_TYPE_RST_STREAM = 0x3,
    FRAME_TYPE_SETTINGS = 0x4,
    FRAME_TYPE_PUSH_PROMISE = 0x5,
    FRAME_TYPE_PING = 0x6,
    FRAME_TYPE_GOAWAY = 0x7,
    FRAME_TYPE_WINDOW_UPDATE = 0x8,
    FRAME_TYPE_CONTINUATION = 0x9,
    FRAME_TYPE_ALTSVC = 0xA,
    FRAME_TYPE_UNUSED = 0xB,
    FRAME_TYPE_ORIGIN = 0xC,
    FRAME_TYPE_PRIORITY_UPDATE = 0x10,
  };

  // NO_ERROR is a macro defined on windows, so we'll name the HTTP2 goaway
  // code NO_ERROR to be NO_HTTP_ERROR
  enum errorType {
    NO_HTTP_ERROR = 0,
    PROTOCOL_ERROR = 1,
    INTERNAL_ERROR = 2,
    FLOW_CONTROL_ERROR = 3,
    SETTINGS_TIMEOUT_ERROR = 4,
    STREAM_CLOSED_ERROR = 5,
    FRAME_SIZE_ERROR = 6,
    REFUSED_STREAM_ERROR = 7,
    CANCEL_ERROR = 8,
    COMPRESSION_ERROR = 9,
    CONNECT_ERROR = 10,
    ENHANCE_YOUR_CALM = 11,
    INADEQUATE_SECURITY = 12,
    HTTP_1_1_REQUIRED = 13,
    UNASSIGNED = 31
  };

  // These are frame flags. If they, or other undefined flags, are
  // used on frames other than the comments indicate they MUST be ignored.
  const static uint8_t kFlag_END_STREAM = 0x01;        // data, headers
  const static uint8_t kFlag_END_HEADERS = 0x04;       // headers, continuation
  const static uint8_t kFlag_END_PUSH_PROMISE = 0x04;  // push promise
  const static uint8_t kFlag_ACK = 0x01;               // ping and settings
  const static uint8_t kFlag_PADDED =
      0x08;  // data, headers, push promise, continuation
  const static uint8_t kFlag_PRIORITY = 0x20;  // headers

  enum {
    // compression table size
    SETTINGS_TYPE_HEADER_TABLE_SIZE = 1,
    // can be used to disable push
    SETTINGS_TYPE_ENABLE_PUSH = 2,
    // streams recvr allowed to initiate
    SETTINGS_TYPE_MAX_CONCURRENT = 3,
    // bytes for flow control default
    SETTINGS_TYPE_INITIAL_WINDOW = 4,
    // max frame size settings sender allows receipt of
    SETTINGS_TYPE_MAX_FRAME_SIZE = 5,
    // 6 is SETTINGS_TYPE_MAX_HEADER_LIST - advisory, we ignore it
    // 7 is unassigned
    // if sender implements extended CONNECT
    SETTINGS_TYPE_ENABLE_CONNECT_PROTOCOL = 8,
    // see rfc9218. used to disable HTTP/2 priority signals
    SETTINGS_NO_RFC7540_PRIORITIES = 9,
    // Used to indicate support for WebTransport over HTTP/2
    SETTINGS_WEBTRANSPORT_MAX_SESSIONS = 0x2b60,
    // Settings for WebTransport
    // https://www.ietf.org/archive/id/draft-ietf-webtrans-http2-11.html#section-10.1
    SETTINGS_WEBTRANSPORT_INITIAL_MAX_DATA = 0x2b61,
    SETTINGS_WEBTRANSPORT_INITIAL_MAX_STREAM_DATA_UNI = 0x2b62,
    SETTINGS_WEBTRANSPORT_INITIAL_MAX_STREAM_DATA_BIDI = 0x2b63,
    SETTINGS_WEBTRANSPORT_INITIAL_MAX_STREAMS_UNI = 0x2b64,
    SETTINGS_WEBTRANSPORT_INITIAL_MAX_STREAMS_BIDI = 0x2b65,
  };

  // This should be big enough to hold all of your control packets,
  // but if it needs to grow for huge headers it can do so dynamically.
  const static uint32_t kDefaultBufferSize = 2048;

  // kDefaultQueueSize must be >= other queue size constants
  const static uint32_t kDefaultQueueSize = 32768;
  const static uint32_t kQueueMinimumCleanup = 24576;
  const static uint32_t kQueueTailRoom = 4096;
  const static uint32_t kQueueReserved = 1024;

  // This is a sentinel for a deleted stream. It is not a valid
  // 31 bit stream ID.
  const static uint32_t kDeadStreamID = 0xffffdead;

  // below the emergency threshold of local window we ack every received
  // byte. Above that we coalesce bytes into the MinimumToAck size.
  const static int32_t kEmergencyWindowThreshold = 96 * 1024;
  const static uint32_t kMinimumToAck = 4 * 1024 * 1024;

  // The default rwin is 64KB - 1 unless updated by a settings frame
  const static uint32_t kDefaultRwin = 65535;

  // We limit frames to 2^14 bytes of length in order to preserve responsiveness
  // This is the smallest allowed value for SETTINGS_MAX_FRAME_SIZE
  const static uint32_t kMaxFrameData = 0x4000;

  const static uint8_t kFrameLengthBytes = 3;
  const static uint8_t kFrameStreamIDBytes = 4;
  const static uint8_t kFrameFlagBytes = 1;
  const static uint8_t kFrameTypeBytes = 1;
  const static uint8_t kFrameHeaderBytes = kFrameLengthBytes + kFrameFlagBytes +
                                           kFrameTypeBytes +
                                           kFrameStreamIDBytes;

  enum {
    kLeaderGroupID = 0x3,
    kOtherGroupID = 0x5,
    kBackgroundGroupID = 0x7,
    kSpeculativeGroupID = 0x9,
    kFollowerGroupID = 0xB,
    kUrgentStartGroupID = 0xD
    // Hey, you! YES YOU! If you add/remove any groups here, you almost
    // certainly need to change the lookup of the stream/ID hash in
    // Http2Session::OnTransportStatus and |kPriorityGroupCount| below.
    // Yeah, that's right. YOU!
  };
  const static uint8_t kPriorityGroupCount = 6;

  static nsresult RecvHeaders(Http2Session*);
  static nsresult RecvPriority(Http2Session*);
  static nsresult RecvRstStream(Http2Session*);
  static nsresult RecvSettings(Http2Session*);
  static nsresult RecvPushPromise(Http2Session*);
  static nsresult RecvPing(Http2Session*);
  static nsresult RecvGoAway(Http2Session*);
  static nsresult RecvWindowUpdate(Http2Session*);
  static nsresult RecvContinuation(Http2Session*);
  static nsresult RecvAltSvc(Http2Session*);
  static nsresult RecvUnused(Http2Session*);
  static nsresult RecvOrigin(Http2Session*);
  static nsresult RecvPriorityUpdate(Http2Session*);

  char* EnsureOutputBuffer(uint32_t needed);

  template <typename charType>
  void CreateFrameHeader(charType dest, uint16_t frameLength, uint8_t frameType,
                         uint8_t frameFlags, uint32_t streamID);

  // For writing the data stream to LOG4
  static void LogIO(Http2Session*, Http2StreamBase*, const char*, const char*,
                    uint32_t);

  // overload of nsAHttpConnection
  void TransactionHasDataToWrite(nsAHttpTransaction*) override;
  void TransactionHasDataToRecv(nsAHttpTransaction*) override;

  // a similar version for Http2StreamBase
  void TransactionHasDataToWrite(Http2StreamBase*);
  void TransactionHasDataToRecv(Http2StreamBase* caller);

  // an overload of nsAHttpSegementReader
  [[nodiscard]] virtual nsresult CommitToSegmentSize(
      uint32_t count, bool forceCommitment) override;
  [[nodiscard]] nsresult BufferOutput(const char*, uint32_t, uint32_t*);
  void FlushOutputQueue();
  uint32_t AmountOfOutputBuffered() {
    return mOutputQueueUsed - mOutputQueueSent;
  }

  uint32_t GetServerInitialStreamWindow() { return mServerInitialStreamWindow; }

  [[nodiscard]] bool TryToActivate(Http2StreamBase* stream);
  void ConnectPushedStream(Http2StreamBase* stream);
  void ConnectSlowConsumer(Http2StreamBase* stream);

  [[nodiscard]] nsresult ConfirmTLSProfile();
  [[nodiscard]] static bool ALPNCallback(nsITLSSocketControl* tlsSocketControl);

  uint64_t Serial() { return mSerial; }

  void PrintDiagnostics(nsCString& log) override;

  // Streams need access to these
  uint32_t SendingChunkSize() { return mSendingChunkSize; }
  uint32_t PushAllowance() { return mPushAllowance; }
  Http2Compressor* Compressor() { return &mCompressor; }
  nsISocketTransport* SocketTransport() { return mSocketTransport; }
  int64_t ServerSessionWindow() { return mServerSessionWindow; }
  void DecrementServerSessionWindow(uint32_t bytes) {
    mServerSessionWindow -= bytes;
  }
  uint32_t InitialRwin() { return mInitialRwin; }

  void SendPing() override;
  bool UseH2Deps() { return mUseH2Deps; }
  void SetCleanShutdown(bool) override;

  // overload of nsAHttpTransaction
  [[nodiscard]] nsresult ReadSegmentsAgain(nsAHttpSegmentReader*, uint32_t,
                                           uint32_t*, bool*) final;
  [[nodiscard]] nsresult WriteSegmentsAgain(nsAHttpSegmentWriter*, uint32_t,
                                            uint32_t*, bool*) final;
  [[nodiscard]] bool Do0RTT(bool aCanSendEarlyData) final { return true; }
  [[nodiscard]] nsresult Finish0RTT(bool aRestart, bool aAlpnChanged) final;

  // For use by an HTTP2Stream
  void Received421(nsHttpConnectionInfo* ci);

  void SendPriorityFrame(uint32_t streamID, uint32_t dependsOn, uint8_t weight);
  void IncrementTrrCounter() { mTrrStreams++; }

  void SendPriorityUpdateFrame(uint32_t streamID, uint8_t urgency,
                               bool incremental);

  ExtendedCONNECTSupport GetExtendedCONNECTSupport() override;

  Result<already_AddRefed<nsHttpConnection>, nsresult> CreateTunnelStream(
      nsAHttpTransaction* aHttpTransaction, nsIInterfaceRequestor* aCallbacks,
      PRIntervalTime aRtt, bool aIsExtendedCONNECT = false) override;

  void CleanupStream(Http2StreamBase*, nsresult, errorType);

 private:
  Http2Session(nsISocketTransport*, enum SpdyVersion version,
               bool attemptingEarlyData);

  static already_AddRefed<Http2StreamTunnel> CreateTunnelStreamFromConnInfo(
      Http2Session* session, uint64_t bcId, nsHttpConnectionInfo* connInfo,
      ExtendedCONNECTType aType);

  // These internal states do not correspond to the states of the HTTP/2
  // specification
  enum internalStateType {
    BUFFERING_OPENING_SETTINGS,
    BUFFERING_FRAME_HEADER,
    BUFFERING_CONTROL_FRAME,
    PROCESSING_DATA_FRAME_PADDING_CONTROL,
    PROCESSING_DATA_FRAME,
    DISCARDING_DATA_FRAME_PADDING,
    DISCARDING_DATA_FRAME,
    PROCESSING_COMPLETE_HEADERS,
    PROCESSING_CONTROL_RST_STREAM,
    NOT_USING_NETWORK
  };

  static const uint8_t kMagicHello[24];

  void CreateStream(nsAHttpTransaction* aHttpTransaction, int32_t aPriority,
                    Http2StreamBaseType streamType);

  [[nodiscard]] nsresult ResponseHeadersComplete();
  uint32_t GetWriteQueueSize();
  void ChangeDownstreamState(enum internalStateType);
  void ResetDownstreamState();
  [[nodiscard]] nsresult ReadyToProcessDataFrame(enum internalStateType);
  [[nodiscard]] nsresult UncompressAndDiscard(bool);
  void GeneratePing(bool);
  void GenerateSettingsAck();
  void GenerateRstStream(uint32_t, uint32_t);
  void GenerateGoAway(uint32_t);
  void CleanupStream(uint32_t, nsresult, errorType);
  void CloseStream(Http2StreamBase* aStream, nsresult aResult,
                   bool aRemoveFromQueue = true);
  void SendHello();
  void RemoveStreamFromQueues(Http2StreamBase*);
  void RemoveStreamFromTables(Http2StreamBase*);
  [[nodiscard]] nsresult ParsePadding(uint8_t&, uint16_t&);

  void SetWriteCallbacks();
  void RealignOutputQueue();

  void ProcessPending();
  [[nodiscard]] nsresult ProcessConnectedPush(Http2StreamBase*,
                                              nsAHttpSegmentWriter*, uint32_t,
                                              uint32_t*);
  [[nodiscard]] nsresult ProcessSlowConsumer(Http2StreamBase*,
                                             nsAHttpSegmentWriter*, uint32_t,
                                             uint32_t*);

  [[nodiscard]] nsresult SetInputFrameDataStream(uint32_t);
  void CreatePriorityNode(uint32_t, uint32_t, uint8_t, const char*);
  char* CreatePriorityFrame(uint32_t, uint32_t, uint8_t);
  bool VerifyStream(Http2StreamBase*, uint32_t);
  void SetNeedsCleanup();

  char* CreatePriorityUpdateFrame(uint32_t streamID, uint8_t urgency,
                                  bool incremental);

  void UpdateLocalRwin(Http2StreamBase* stream, uint32_t bytes);
  void UpdateLocalStreamWindow(Http2StreamBase* stream, uint32_t bytes);
  void UpdateLocalSessionWindow(uint32_t bytes);

  void MaybeDecrementConcurrent(Http2StreamBase* stream);
  uint32_t RoomForMoreConcurrent();
  void IncrementConcurrent(Http2StreamBase* stream);
  void QueueStream(Http2StreamBase* stream);

  // a wrapper for all calls to the nshttpconnection level segment writer. Used
  // to track network I/O for timeout purposes
  [[nodiscard]] nsresult NetworkRead(nsAHttpSegmentWriter*, char*, uint32_t,
                                     uint32_t*);

  void Shutdown(nsresult aReason);
  void ShutdownStream(Http2StreamBase* aStream, nsresult aResult);

  nsresult SessionError(enum errorType);

  // This is intended to be nsHttpConnectionMgr:nsConnectionHandle taken
  // from the first transaction on this session. That object contains the
  // pointer to the real network-level nsHttpConnection object.
  RefPtr<nsAHttpConnection> mConnection;

  // The underlying socket transport object is needed to propogate some events
  nsISocketTransport* mSocketTransport;

  // These are temporary state variables to hold the argument to
  // Read/WriteSegments so it can be accessed by On(read/write)segment
  // further up the stack.
  RefPtr<nsAHttpSegmentReader> mSegmentReader;
  nsAHttpSegmentWriter* mSegmentWriter;
  const uint32_t kMaxStreamID;
  uint32_t mSendingChunkSize;    /* the transmission chunk size */
  uint32_t mNextStreamID;        /* 24 bits */
  uint32_t mConcurrentHighWater; /* max parallelism on session */
  uint32_t mPushAllowance;       /* rwin for unmatched pushes */

  internalStateType mDownstreamState; /* in frame, between frames, etc..  */

  // Maintain 2 indexes - one by stream ID, one by transaction pointer.
  // There are also several lists of streams: ready to write, queued due to
  // max parallelism, streams that need to force a read for push, and the full
  // set of pushed streams.
  nsTHashMap<nsUint32HashKey, WeakPtr<Http2StreamBase>> mStreamIDHash;
  nsRefPtrHashtable<nsPtrHashKey<nsAHttpTransaction>, Http2StreamBase>
      mStreamTransactionHash;
  nsTArray<RefPtr<Http2StreamTunnel>> mTunnelStreams;

  Http2StreamQueueManager mQueueManager;

  // Compression contexts for header transport.
  // HTTP/2 compresses only HTTP headers and does not reset the context in
  // between frames. Even data that is not associated with a stream (e.g invalid
  // stream ID) is passed through these contexts to keep the compression
  // context correct.
  Http2Compressor mCompressor;
  Http2Decompressor mDecompressor;
  nsCString mDecompressBuffer;

  // mInputFrameBuffer is used to store received control packets and the 8 bytes
  // of header on data packets
  uint32_t mInputFrameBufferSize;  // buffer allocation
  uint32_t mInputFrameBufferUsed;  // amt of allocation used
  UniquePtr<char[]> mInputFrameBuffer;

  // mInputFrameDataSize/Read are used for tracking the amount of data consumed
  // in a frame after the 8 byte header. Control frames are always fully
  // buffered and the fixed 8 byte leading header is at mInputFrameBuffer + 0,
  // the first data byte (i.e. the first settings/goaway/etc.. specific byte) is
  // at mInputFrameBuffer + 8 The frame size is mInputFrameDataSize + the
  // constant 8 byte header
  uint32_t mInputFrameDataSize;
  uint32_t mInputFrameDataRead;
  bool mInputFrameFinal;  // This frame was marked FIN
  uint8_t mInputFrameType;
  uint8_t mInputFrameFlags;
  uint32_t mInputFrameID;
  uint16_t mPaddingLength;

  // When a frame has been received that is addressed to a particular stream
  // (e.g. a data frame after the stream-id has been decoded), this points
  // to the stream.
  WeakPtr<Http2StreamBase> mInputFrameDataStream;

  // mNeedsCleanup is a state variable to defer cleanup of a closed stream
  // If needed, It is set in session::OnWriteSegments() and acted on and
  // cleared when the stack returns to session::WriteSegments(). The stream
  // cannot be destroyed directly out of OnWriteSegments because
  // stream::writeSegments() is on the stack at that time.
  WeakPtr<Http2StreamBase> mNeedsCleanup;

  // This reason code in the last processed RESET frame
  uint32_t mDownstreamRstReason;

  // When HEADERS/PROMISE are chained together, this is the expected ID of the
  // next recvd frame which must be the same type
  uint32_t mExpectedHeaderID;
  uint32_t mExpectedPushPromiseID;

  // END_STREAM of the HEADERS frame that started the current header block. It
  // only takes effect once the block is complete.
  bool mHeaderBlockEndStream = false;

  // for the conversion of downstream http headers into http/2 formatted headers
  // The data here does not persist between frames
  nsCString mFlatHTTPResponseHeaders;
  uint32_t mFlatHTTPResponseHeadersOut;

  // when set, the session will go away when it reaches 0 streams. This flag
  // is set when: the stream IDs are running out (at either the client or the
  // server), when DontReuse() is called, a RST that is not specific to a
  // particular stream is received, a GOAWAY frame has been received from
  // the server.
  bool mShouldGoAway;

  // the session has received a nsAHttpTransaction::Close()  call
  bool mClosed;

  // the session received a GoAway frame with a valid GoAwayID
  bool mCleanShutdown;

  // the session received the opening SETTINGS frame from the server
  bool mReceivedSettings;

  // The TLS comlpiance checks are not done in the ctor beacuse of bad
  // exception handling - so we do them at IO time and cache the result
  bool mTLSProfileConfirmed;

  // A specifc reason code for the eventual GoAway frame. If set to
  // NO_HTTP_ERROR only NO_HTTP_ERROR, PROTOCOL_ERROR, or INTERNAL_ERROR will be
  // sent.
  errorType mGoAwayReason;

  // The error code sent/received on the session goaway frame. UNASSIGNED/31
  // if not transmitted.
  int32_t mClientGoAwayReason;
  int32_t mPeerGoAwayReason;

  // If a GoAway message was received this is the ID of the last valid
  // stream. 0 otherwise. (0 is never a valid stream id.)
  uint32_t mGoAwayID;

  // The last stream processed ID we will send in our GoAway frame.
  uint32_t mOutgoingGoAwayID;

  // The limit on number of concurrent streams for this session. Normally it
  // is basically unlimited, but the SETTINGS control message from the
  // server might bring it down.
  uint32_t mMaxConcurrent;

  // The actual number of concurrent streams at this moment. Generally below
  // mMaxConcurrent, but the max can be lowered in real time to a value
  // below the current value
  uint32_t mConcurrent;

  // The number of server initiated promises, tracked for telemetry
  uint32_t mServerPushedResources;

  // The server rwin for new streams as determined from a SETTINGS frame
  uint32_t mServerInitialStreamWindow;

  // The Local Session window is how much data the server is allowed to send
  // (across all streams) without getting a window update to stream 0. It is
  // signed because asynchronous changes via SETTINGS can drive it negative.
  int64_t mLocalSessionWindow;

  // The Remote Session Window is how much data the client is allowed to send
  // (across all streams) without receiving a window update to stream 0. It is
  // signed because asynchronous changes via SETTINGS can drive it negative.
  int64_t mServerSessionWindow;

  // The initial value of the local stream and session window
  uint32_t mInitialRwin;

  uint32_t mInitialWebTransportMaxData = 0;
  uint32_t mInitialWebTransportMaxStreamDataBidi = 0;
  uint32_t mInitialWebTransportMaxStreamDataUnidi = 0;
  uint32_t mInitialWebTransportMaxStreamsBidi = 0;
  uint32_t mInitialWebTransportMaxStreamsUnidi = 0;

  // This is a output queue of bytes ready to be written to the SSL stream.
  // When that streams returns WOULD_BLOCK on direct write the bytes get
  // coalesced together here. This results in larger writes to the SSL layer.
  // The buffer is not dynamically grown to accomodate stream writes, but
  // does expand to accept infallible session wide frames like GoAway and RST.
  uint32_t mOutputQueueSize;
  uint32_t mOutputQueueUsed;
  uint32_t mOutputQueueSent;
  UniquePtr<char[]> mOutputQueueBuffer;

  PRIntervalTime mPingThreshold;
  PRIntervalTime mLastReadEpoch;      // used for ping timeouts
  PRIntervalTime mLastDataReadEpoch;  // used for IdleTime()
  PRIntervalTime mPingSentEpoch;

  PRIntervalTime mPreviousPingThreshold;  // backup for the former value
  bool mPreviousUsed;                     // true when backup is used

  // used as a temporary buffer while enumerating the stream hash during GoAway
  nsDeque<Http2StreamBase> mGoAwayStreamsToRestart;

  // Each session gets a unique serial number because the push cache is
  // correlated by the load group and the serial number can be used as part of
  // the cache key to make sure streams aren't shared across sessions.
  uint64_t mSerial;

  // Telemetry for continued headers (pushed and pulled) for quic design
  uint32_t mAggregatedHeaderSize;

  // If push is disabled, we want to be able to send PROTOCOL_ERRORs if we
  // receive a PUSH_PROMISE, but we have to wait for the SETTINGS ACK before
  // we can actually tell the other end to go away. These help us keep track
  // of that state so we can behave appropriately.
  bool mWaitingForSettingsAck;
  bool mGoAwayOnPush;

  bool mUseH2Deps;

  bool mAttemptingEarlyData;
  // The ID(s) of the stream(s) that we are getting 0RTT data from.
  nsTArray<WeakPtr<Http2StreamBase>> m0RTTStreams;
  // The ID(s) of the stream(s) that are not able to send 0RTT data. We need to
  // remember them put them into mReadyForWrite queue when 0RTT finishes.
  nsTArray<WeakPtr<Http2StreamBase>> mCannotDo0RTTStreams;

  bool RealJoinConnection(const nsACString& hostname, int32_t port,
                          bool justKidding);
  bool TestOriginFrame(const nsACString& name, int32_t port);
  bool mOriginFrameActivated;
  nsTHashMap<nsCStringHashKey, bool> mOriginFrame;

  nsTHashMap<nsCStringHashKey, bool> mJoinConnectionCache;

  uint64_t mCurrentBrowserId;

  uint32_t mCntActivated;

  // A h2 session will be created before all socket events are trigered,
  // e.g. NS_NET_STATUS_TLS_HANDSHAKE_ENDED.
  // We should propagate this events to the first nsHttpTransaction.
  RefPtr<nsHttpTransaction> mFirstHttpTransaction;
  bool mTlsHandshakeFinished;

  bool mPeerFailedHandshake;

  uint32_t mWebTransportMaxSessions = 0;

  uint32_t mOngoingWebTransportSessions = 0;

 private:
  TimeStamp mLastTRRResponseTime;  // Time of the last successful TRR response
  uint32_t mTrrStreams;
  nsCString mTrrHost;

  // Whether we allow websockets, based on a pref
  bool mEnableWebsockets = false;
  // Whether our peer allows extended CONNECT, based on SETTINGS
  bool mPeerAllowsExtendedCONNECT = false;

  // Setting this to true means there is a transaction waiting for the result of
  // extended CONNECT support. We'll need to process the pending queue once
  // we've received the settings.
  bool mHasTransactionWaitingForExtendedCONNECT = false;
};

}  // namespace net
}  // namespace mozilla

#endif  // mozilla_net_Http2Session_h
