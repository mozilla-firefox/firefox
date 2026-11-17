/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_Http2StreamBase_h
#define mozilla_net_Http2StreamBase_h

// HTTP/2 - RFC7540
// https://www.rfc-editor.org/rfc/rfc7540.txt

#include "mozilla/Attributes.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/WeakPtr.h"
#include "nsAHttpTransaction.h"
#include "nsISupportsPriority.h"
#include "SimpleBuffer.h"
#include "nsISupportsImpl.h"
#include "nsIURI.h"

class nsISocketTransport;
class nsIInputStream;
class nsIOutputStream;

namespace mozilla {
class OriginAttributes;
}

namespace mozilla::net {

class nsStandardURL;
class Http2Session;
class Http2Stream;
class Http2PushedStream;
class Http2Decompressor;

class Http2StreamBase : public nsAHttpSegmentReader,
                        public nsAHttpSegmentWriter,
                        public SupportsWeakPtr {
 public:
  NS_DECL_NSAHTTPSEGMENTREADER

  enum stateType {
    IDLE,
    RESERVED_BY_REMOTE,
    OPEN,
    CLOSED_BY_LOCAL,
    CLOSED_BY_REMOTE,
    CLOSED
  };

  const static int32_t kNormalPriority = 0x1000;
  const static int32_t kWorstPriority =
      kNormalPriority + nsISupportsPriority::PRIORITY_LOWEST;
  const static int32_t kBestPriority =
      kNormalPriority + nsISupportsPriority::PRIORITY_HIGHEST;

  Http2StreamBase(uint64_t, Http2Session*, int32_t, uint64_t);

  uint32_t StreamID() { return mStreamID; }

  stateType HTTPState() { return mState; }
  void SetHTTPState(stateType val) { mState = val; }

  [[nodiscard]] virtual nsresult ReadSegments(nsAHttpSegmentReader*, uint32_t,
                                              uint32_t*);
  [[nodiscard]] virtual nsresult WriteSegments(nsAHttpSegmentWriter*, uint32_t,
                                               uint32_t*);
  virtual bool DeferCleanup(nsresult status);

  const nsCString& Origin() const { return mOrigin; }
  const nsCString& Host() const { return mHeaderHost; }
  const nsCString& Path() const { return mHeaderPath; }

  bool RequestBlockedOnRead() {
    return static_cast<bool>(mRequestBlockedOnRead);
  }

  bool HasRegisteredID() { return mStreamID != 0; }

  virtual nsAHttpTransaction* Transaction() { return nullptr; }
  nsHttpTransaction* HttpTransaction();
  virtual nsIRequestContext* RequestContext() { return nullptr; }

  virtual void CloseStream(nsresult reason) = 0;
  void SetResponseIsComplete();

  void SetRecvdFin(bool aStatus);
  bool RecvdFin() { return mRecvdFin; }

  void SetRecvdData(bool aStatus) { mReceivedData = aStatus ? 1 : 0; }
  bool RecvdData() { return mReceivedData; }

  void SetSentFin(bool aStatus);
  bool SentFin() { return mSentFin; }

  void SetRecvdReset(bool aStatus);
  bool RecvdReset() { return mRecvdReset; }

  void SetSentReset(bool aStatus);
  bool SentReset() { return mSentReset; }

  void SetQueued(bool aStatus) { mQueued = aStatus ? 1 : 0; }
  bool Queued() { return mQueued; }

  void SetCountAsActive(bool aStatus) { mCountAsActive = aStatus ? 1 : 0; }
  bool CountAsActive() { return mCountAsActive; }

  void SetAllHeadersReceived();
  void UnsetAllHeadersReceived() { mAllHeadersReceived = 0; }
  bool AllHeadersReceived() { return mAllHeadersReceived; }

  void UpdateTransportSendEvents(uint32_t count);
  void UpdateTransportReadEvents(uint32_t count);

  // NS_ERROR_ABORT terminates stream, other failure terminates session
  [[nodiscard]] nsresult ConvertResponseHeaders(Http2Decompressor*, nsACString&,
                                                nsACString&, int32_t&);
  [[nodiscard]] nsresult ConvertResponseTrailers(Http2Decompressor*,
                                                 nsACString&);

  bool AllowFlowControlledWrite();
  void UpdateServerReceiveWindow(int32_t delta);
  int64_t ServerReceiveWindow() { return mServerReceiveWindow; }

  void DecrementClientReceiveWindow(uint32_t delta) {
    mClientReceiveWindow -= delta;
    mLocalUnacked += delta;
  }

  void IncrementClientReceiveWindow(uint32_t delta) {
    mClientReceiveWindow += delta;
    mLocalUnacked -= delta;
  }

  uint64_t LocalUnAcked();
  int64_t ClientReceiveWindow() { return mClientReceiveWindow; }

  bool BlockedOnRwin() { return mBlockedOnRwin; }

  uint32_t Priority() { return mPriority; }
  uint32_t PriorityDependency() { return mPriorityDependency; }
  uint8_t PriorityWeight() { return mPriorityWeight; }
  void SetPriority(uint32_t);
  void SetPriorityDependency(uint32_t, uint32_t);
  void UpdatePriorityDependency();

  uint64_t TransactionBrowserId() { return mTransactionBrowserId; }

  // A pull stream has an implicit sink, a pushed stream has a sink
  // once it is matched to a pull stream.
  virtual bool HasSink() { return true; }

  already_AddRefed<Http2Session> Session();

  // Mirrors nsAHttpTransaction
  bool Do0RTT();
  nsresult Finish0RTT(bool aRestart, bool aAlpnChanged);

  nsresult GetOriginAttributes(mozilla::OriginAttributes* oa);

  virtual void CurrentBrowserIdChanged(uint64_t id);
  void CurrentBrowserIdChangedInternal(
      uint64_t id);  // For use by pushed streams only

  virtual bool IsTunnel() { return false; }

  virtual uint32_t GetWireStreamId() { return mStreamID; }
  virtual Http2Stream* GetHttp2Stream() { return nullptr; }
  virtual Http2PushedStream* GetHttp2PushedStream() { return nullptr; }

  [[nodiscard]] virtual nsresult OnWriteSegment(char*, uint32_t,
                                                uint32_t*) override;

  virtual nsHttpConnectionInfo* ConnectionInfo();

  bool DataBuffered() { return mSimpleBuffer.Available(); }

  virtual nsresult Condition() { return NS_OK; }

  virtual void DisableSpdy() {
    if (Transaction()) {
      Transaction()->DisableSpdy();
    }
  }
  virtual void ReuseConnectionOnRestartOK(bool aReuse) {
    if (Transaction()) {
      Transaction()->ReuseConnectionOnRestartOK(aReuse);
    }
  }
  virtual void MakeNonSticky() {
    if (Transaction()) {
      Transaction()->MakeNonSticky();
    }
  }

 protected:
  virtual ~Http2StreamBase();

  virtual void HandleResponseHeaders(nsACString& aHeadersOut,
                                     int32_t httpResponseCode) {}
  virtual nsresult CallToWriteData(uint32_t count, uint32_t* countRead) = 0;
  virtual nsresult CallToReadData(uint32_t count, uint32_t* countWritten) = 0;
  virtual bool CloseSendStreamWhenDone() { return true; }

  // These internal states track request generation
  enum upstreamStateType {
    GENERATING_HEADERS,
    GENERATING_BODY,
    SENDING_BODY,
    SENDING_FIN_STREAM,
    UPSTREAM_COMPLETE
  };

  uint32_t mStreamID{0};

  // The session that this stream is a subset of
  nsWeakPtr mSession;

  // These are temporary state variables to hold the argument to
  // Read/WriteSegments so it can be accessed by On(read/write)segment
  // further up the stack.
  RefPtr<nsAHttpSegmentReader> mSegmentReader;
  nsAHttpSegmentWriter* mSegmentWriter{nullptr};

  nsCString mOrigin;
  nsCString mHeaderHost;
  nsCString mHeaderScheme;
  nsCString mHeaderPath;

  // Each stream goes from generating_headers to upstream_complete, perhaps
  // looping on multiple instances of generating_body and
  // sending_body for each frame in the upload.
  enum upstreamStateType mUpstreamState { GENERATING_HEADERS };

  // The HTTP/2 state for the stream from section 5.1
  enum stateType mState { IDLE };

  // Flag is set when all http request headers have been read ID is not stable
  uint32_t mRequestHeadersDone : 1;

  // Flag is set when ID is stable and concurrency limits are met
  uint32_t mOpenGenerated : 1;

  // Flag is set when all http response headers have been read
  uint32_t mAllHeadersReceived : 1;

  // Flag is set when stream is queued inside the session due to
  // concurrency limits being exceeded
  uint32_t mQueued : 1;

  void ChangeState(enum upstreamStateType);

  virtual void AdjustInitialWindow();
  [[nodiscard]] nsresult TransmitFrame(const char*, uint32_t*,
                                       bool forceCommitment);

  // The underlying socket transport object is needed to propogate some events
  nsCOMPtr<nsISocketTransport> mSocketTransport;

  uint8_t mPriorityWeight = 0;       // h2 weight
  uint32_t mPriorityDependency = 0;  // h2 stream id this one depends on
  uint64_t mCurrentBrowserId;
  uint64_t mTransactionBrowserId{0};

  // The InlineFrame and associated data is used for composing control
  // frames and data frame headers.
  UniquePtr<uint8_t[]> mTxInlineFrame;
  uint32_t mTxInlineFrameSize{0};
  uint32_t mTxInlineFrameUsed{0};

  // A failure to queue a frame for writing, see TransmitFrame().
  nsresult mTransmitError{NS_OK};

  uint32_t mPriority = 0;  // geckoish weight

  // Buffer for request header compression.
  nsCString mFlatHttpRequestHeaders;

  // Track the content-length of a request body so that we can
  // place the fin flag on the last data packet instead of waiting
  // for a stream closed indication. Relying on stream close results
  // in an extra 0-length runt packet and seems to have some interop
  // problems with the google servers. Connect does rely on stream
  // close by setting this to the max value.
  int64_t mRequestBodyLenRemaining{0};

 private:
  friend class mozilla::DefaultDelete<Http2StreamBase>;

  [[nodiscard]] nsresult ParseHttpRequestHeaders(const char*, uint32_t,
                                                 uint32_t*);
  [[nodiscard]] nsresult GenerateOpen();

  virtual nsresult GenerateHeaders(nsCString& aCompressedData,
                                   uint8_t& firstFrameFlags) = 0;

  void GenerateDataFrameHeader(uint32_t, bool);

  [[nodiscard]] nsresult BufferInput(uint32_t, uint32_t*);

  // The quanta upstream data frames are chopped into
  uint32_t mChunkSize;

  // Flag is set when the HTTP processor has more data to send
  // but has blocked in doing so.
  uint32_t mRequestBlockedOnRead : 1;

  // Flag is set after the response frame bearing the fin bit has
  // been processed. (i.e. after the server has closed).
  uint32_t mRecvdFin : 1;

  // Flag is set after 1st DATA frame has been passed to stream
  uint32_t mReceivedData : 1;

  // Flag is set after RST_STREAM has been received for this stream
  uint32_t mRecvdReset : 1;

  // Flag is set after RST_STREAM has been generated for this stream
  uint32_t mSentReset : 1;

  // Flag is set when stream is counted towards MAX_CONCURRENT streams in
  // session
  uint32_t mCountAsActive : 1;

  // Flag is set when a FIN has been placed on a data or header frame
  // (i.e after the client has closed)
  uint32_t mSentFin : 1;

  // Flag is set after the WAITING_FOR Transport event has been generated
  uint32_t mSentWaitingFor : 1;

  // Flag is set after TCP send autotuning has been disabled
  uint32_t mSetTCPSocketBuffer : 1;

  // Flag is set when OnWriteSegment is being called directly from stream
  // instead of transaction
  uint32_t mBypassInputBuffer : 1;

  // mTxStreamFrameSize tracks the progress of
  // transmitting a request body data frame. The data frame itself
  // is never copied into the spdy layer.
  uint32_t mTxStreamFrameSize{0};

  // mClientReceiveWindow, mServerReceiveWindow, and mLocalUnacked are for flow
  // control. *window are signed because the race conditions in asynchronous
  // SETTINGS messages can force them temporarily negative.

  // mClientReceiveWindow is how much data the server will send without getting
  // a
  //   window update
  int64_t mClientReceiveWindow;

  // mServerReceiveWindow is how much data the client is allowed to send without
  //   getting a window update
  int64_t mServerReceiveWindow;

  // LocalUnacked is the number of bytes received by the client but not
  //   yet reflected in a window update. Sending that update will increment
  //   ClientReceiveWindow
  uint64_t mLocalUnacked{0};

  // True when sending is suspended becuase the server receive window is
  //   <= 0
  bool mBlockedOnRwin{false};

  // For Progress Events
  uint64_t mTotalSent{0};
  uint64_t mTotalRead{0};

  // Used to store stream data when the transaction channel cannot keep up
  // and flow control has not yet kicked in.
  SimpleBuffer mSimpleBuffer;

  bool mAttempting0RTT{false};
};

}  // namespace mozilla::net

#endif  // mozilla_net_Http2StreamBase_h
