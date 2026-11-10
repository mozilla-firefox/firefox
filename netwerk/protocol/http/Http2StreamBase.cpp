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

#include <algorithm>

#include "Http2Compression.h"
#include "Http2Session.h"
#include "Http2Stream.h"
#include "Http2StreamBase.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/Components.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/glean/NetwerkProtocolHttpMetrics.h"
#include "nsHttp.h"
#include "nsHttpHandler.h"
#include "nsHttpRequestHead.h"
#include "nsHttpTransaction.h"
#include "nsIClassOfService.h"
#include "nsISocketTransport.h"
#include "prnetdb.h"

namespace mozilla::net {

NS_IMPL_ADDREF(Http2StreamBase)
NS_IMETHODIMP_(MozExternalRefCountType)
Http2StreamBase::Release() {
  nsrefcnt count;
  MOZ_ASSERT(0 != mRefCnt, "dup release");
  count = --mRefCnt;
  NS_LOG_RELEASE(this, count, "Http2StreamBase");
  if (0 == count) {
    mRefCnt = 1; /* stablize */
    // it is essential that the stream be destroyed on the socket thread.
    DeleteSelfOnSocketThread();
    return 0;
  }
  return count;
}

NS_IMPL_QUERY_INTERFACE0(Http2StreamBase)

class DeleteHttp2StreamBase : public Runnable {
 public:
  explicit DeleteHttp2StreamBase(Http2StreamBase* aStream)
      : Runnable("net::DeleteHttp2StreamBase"), mStream(aStream) {}

  NS_IMETHOD Run() override {
    delete mStream;
    return NS_OK;
  }

 private:
  Http2StreamBase* mStream;
};

void Http2StreamBase::DeleteSelfOnSocketThread() {
  if (OnSocketThread()) {
    delete this;
    return;
  }

  nsCOMPtr<nsIEventTarget> sts =
      mozilla::components::SocketTransport::Service();
  nsCOMPtr<nsIRunnable> event = new DeleteHttp2StreamBase(this);
  (void)NS_WARN_IF(
      NS_FAILED(sts->Dispatch(event.forget(), NS_DISPATCH_NORMAL)));
}

Http2StreamBase::Http2StreamBase(uint64_t aTransactionBrowserId,
                                 Http2Session* session, int32_t priority,
                                 uint64_t currentBrowserId)
    : mSession(
          do_GetWeakReference(static_cast<nsISupportsWeakReference*>(session))),
      mRequestHeadersDone(0),
      mOpenGenerated(0),
      mAllHeadersReceived(0),
      mQueued(0),
      mInWriteQueue(0),
      mInReadQueue(0),
      mSocketTransport(session->SocketTransport()),
      mCurrentBrowserId(currentBrowserId),
      mTransactionBrowserId(aTransactionBrowserId),
      mTxInlineFrameSize(Http2Session::kDefaultBufferSize),
      mChunkSize(session->SendingChunkSize()),
      mRequestBlockedOnRead(0),
      mRecvdFin(0),
      mReceivedData(0),
      mRecvdReset(0),
      mSentReset(0),
      mCountAsActive(0),
      mSentFin(0),
      mSentWaitingFor(0),
      mSetTCPSocketBuffer(0),
      mBypassInputBuffer(0) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  LOG1(("Http2StreamBase::Http2StreamBase %p", this));

  mServerReceiveWindow = session->GetServerInitialStreamWindow();
  mClientReceiveWindow = session->PushAllowance();

  mTxInlineFrame = std::make_unique<uint8_t[]>(mTxInlineFrameSize);

  static_assert(nsISupportsPriority::PRIORITY_LOWEST <= kNormalPriority,
                "Lowest Priority should be less than kNormalPriority");

  // values of priority closer to 0 are higher priority for the priority
  // argument. This value is used as a group, which maps to a
  // weight that is related to the nsISupportsPriority that we are given.
  int32_t httpPriority;
  if (priority >= nsISupportsPriority::PRIORITY_LOWEST) {
    httpPriority = kWorstPriority;
  } else if (priority <= nsISupportsPriority::PRIORITY_HIGHEST) {
    httpPriority = kBestPriority;
  } else {
    httpPriority = kNormalPriority + priority;
  }
  MOZ_ASSERT(httpPriority >= 0);
  SetPriority(static_cast<uint32_t>(httpPriority));
}

Http2StreamBase::~Http2StreamBase() {
  MOZ_DIAGNOSTIC_ASSERT(OnSocketThread());

  mStreamID = Http2Session::kDeadStreamID;

  LOG3(("Http2StreamBase::~Http2StreamBase %p", this));
}

already_AddRefed<Http2Session> Http2StreamBase::Session() {
  RefPtr<Http2Session> session = do_QueryReferent(mSession);
  return session.forget();
}

// ReadSegments() is used to write data down the socket. Generally, HTTP
// request data is pulled from the approriate transaction and
// converted to HTTP/2 data. Sometimes control data like a window-update is
// generated instead.

nsresult Http2StreamBase::ReadSegments(nsAHttpSegmentReader* reader,
                                       uint32_t count, uint32_t* countRead) {
  LOG3(("Http2StreamBase %p ReadSegments reader=%p count=%d state=%x", this,
        reader, count, mUpstreamState));
  RefPtr<Http2Session> session = Session();
  // Reader is nullptr when this is a push stream.
  MOZ_DIAGNOSTIC_ASSERT(!reader || (reader == session) ||
                        (IsTunnel() && NS_FAILED(Condition())));

  if (NS_FAILED(Condition())) {
    return Condition();
  }

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");

  nsresult rv = NS_ERROR_UNEXPECTED;
  mRequestBlockedOnRead = 0;

  // Half-closed (remote) may still send (RFC 9113 section 5.1).
  if (mRecvdReset) {
    LOG3(("Http2StreamBase %p ReadSegments stop sending after reset\n", this));
    *countRead = 0;
    return NS_OK;
  }

  // avoid runt chunks if possible by anticipating
  // full data frames
  if (count > (mChunkSize + 8)) {
    uint32_t numchunks = count / (mChunkSize + 8);
    count = numchunks * (mChunkSize + 8);
  }

  switch (mUpstreamState) {
    case GENERATING_HEADERS:
    case GENERATING_BODY:
    case SENDING_BODY:
      // Call into the HTTP Transaction to generate the HTTP request
      // stream. That stream will show up in OnReadSegment().
      mSegmentReader = reader;
      rv = CallToReadData(count, countRead);
      mSegmentReader = nullptr;

      LOG3(("Http2StreamBase::ReadSegments %p trans readsegments rv %" PRIx32
            " read=%d\n",
            this, static_cast<uint32_t>(rv), *countRead));

      // Check to see if the transaction's request could be written out now.
      // If not, mark the stream for callback when writing can proceed.
      if (NS_SUCCEEDED(rv) && mUpstreamState == GENERATING_HEADERS &&
          !mRequestHeadersDone) {
        session->TransactionHasDataToWrite(this);
      }

      // mTxinlineFrameUsed represents any queued un-sent frame. It might
      // be 0 if there is no such frame, which is not a gurantee that we
      // don't have more request body to send - just that any data that was
      // sent comprised a complete HTTP/2 frame. Likewise, a non 0 value is
      // a queued, but complete, http/2 frame length.

      // Mark that we are blocked on read if the http transaction needs to
      // provide more of the request message body and there is nothing queued
      // for writing
      if (rv == NS_BASE_STREAM_WOULD_BLOCK && !mTxInlineFrameUsed) {
        LOG(("Http2StreamBase %p mRequestBlockedOnRead = 1", this));
        mRequestBlockedOnRead = 1;
      }

      // A transaction that had already generated its headers before it was
      // queued at the session level (due to concurrency concerns) may not call
      // onReadSegment off the ReadSegments() stack above.

      // When mTransaction->ReadSegments returns NS_BASE_STREAM_WOULD_BLOCK it
      // means it may have already finished providing all the request data
      // necessary to generate open, calling OnReadSegment will drive sending
      // the request; this may happen after dequeue of the stream.

      if (mUpstreamState == GENERATING_HEADERS &&
          (NS_SUCCEEDED(rv) || rv == NS_BASE_STREAM_WOULD_BLOCK)) {
        LOG3(("Http2StreamBase %p ReadSegments forcing OnReadSegment call\n",
              this));
        uint32_t wasted = 0;
        mSegmentReader = reader;
        nsresult rv2 = OnReadSegment("", 0, &wasted);
        mSegmentReader = nullptr;

        LOG3(("  OnReadSegment returned 0x%08" PRIx32,
              static_cast<uint32_t>(rv2)));
        if (NS_SUCCEEDED(rv2)) {
          mRequestBlockedOnRead = 0;
        }
      }

      // If the sending flow control window is open (!mBlockedOnRwin) then
      // continue sending the request
      if (!mBlockedOnRwin && mOpenGenerated && !mTxInlineFrameUsed &&
          NS_SUCCEEDED(rv) && (!*countRead) && CloseSendStreamWhenDone()) {
        MOZ_ASSERT(!mQueued);
        MOZ_ASSERT(mRequestHeadersDone);
        LOG3(
            ("Http2StreamBase::ReadSegments %p 0x%X: Sending request data "
             "complete, "
             "mUpstreamState=%x\n",
             this, mStreamID, mUpstreamState));
        // Retire the "length unknown" sentinel now the body is over, so that
        // UpdateTransportSendEvents() fires NS_NET_STATUS_WAITING_FOR.
        if (mRequestBodyLenRemaining < 0) {
          mRequestBodyLenRemaining = 0;
        }
        if (mSentFin) {
          ChangeState(UPSTREAM_COMPLETE);
        } else {
          GenerateDataFrameHeader(0, true);
          ChangeState(SENDING_FIN_STREAM);
          session->TransactionHasDataToWrite(this);
          rv = NS_BASE_STREAM_WOULD_BLOCK;
        }
      }
      break;

    case SENDING_FIN_STREAM:
      // We were trying to send the FIN-STREAM but were blocked from
      // sending it out - try again.
      if (!mSentFin) {
        mSegmentReader = reader;
        rv = TransmitFrame(nullptr, nullptr, false);
        mSegmentReader = nullptr;
        MOZ_ASSERT(NS_FAILED(rv) || !mTxInlineFrameUsed,
                   "Transmit Frame should be all or nothing");
        if (NS_SUCCEEDED(rv)) ChangeState(UPSTREAM_COMPLETE);
      } else {
        rv = NS_OK;
        mTxInlineFrameUsed = 0;  // cancel fin data packet
        ChangeState(UPSTREAM_COMPLETE);
      }

      *countRead = 0;

      // don't change OK to WOULD BLOCK. we are really done sending if OK
      break;

    case UPSTREAM_COMPLETE:
      *countRead = 0;
      rv = NS_OK;
      break;

    default:
      MOZ_ASSERT(false, "Http2StreamBase::ReadSegments unknown state");
      break;
  }

  return rv;
}

uint64_t Http2StreamBase::LocalUnAcked() {
  // reduce unacked by the amount of undelivered data
  // to help assert flow control
  uint64_t undelivered = mSimpleBuffer.Available();

  if (undelivered > mLocalUnacked) {
    return 0;
  }
  return mLocalUnacked - undelivered;
}

nsresult Http2StreamBase::BufferInput(uint32_t count, uint32_t* countWritten) {
  char buf[SimpleBufferPage::kSimpleBufferPageSize];
  if (SimpleBufferPage::kSimpleBufferPageSize < count) {
    count = SimpleBufferPage::kSimpleBufferPageSize;
  }

  mBypassInputBuffer = 1;
  nsresult rv = mSegmentWriter->OnWriteSegment(buf, count, countWritten);
  mBypassInputBuffer = 0;

  if (NS_SUCCEEDED(rv)) {
    rv = mSimpleBuffer.Write(buf, *countWritten);
    if (NS_FAILED(rv)) {
      MOZ_ASSERT(rv == NS_ERROR_OUT_OF_MEMORY);
      return NS_ERROR_OUT_OF_MEMORY;
    }
  }
  return rv;
}

bool Http2StreamBase::DeferCleanup(nsresult status) {
  // do not cleanup a stream that has data buffered for the transaction
  return (NS_SUCCEEDED(status) && mSimpleBuffer.Available());
}

// WriteSegments() is used to read data off the socket. Generally this is
// just a call through to the associated nsHttpTransaction for this stream
// for the remaining data bytes indicated by the current DATA frame.

nsresult Http2StreamBase::WriteSegments(nsAHttpSegmentWriter* writer,
                                        uint32_t count,
                                        uint32_t* countWritten) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(!mSegmentWriter, "segment writer in progress");

  LOG3(("Http2StreamBase::WriteSegments %p count=%d state=%x", this, count,
        mUpstreamState));

  mSegmentWriter = writer;
  nsresult rv = CallToWriteData(count, countWritten);

  if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
    // consuming transaction won't take data. but we need to read it into a
    // buffer so that it won't block other streams. but we should not advance
    // the flow control window so that we'll eventually push back on the sender.
    rv = BufferInput(count, countWritten);
    LOG3(("Http2StreamBase::WriteSegments %p Buffered %" PRIX32 " %d\n", this,
          static_cast<uint32_t>(rv), *countWritten));
  }

  LOG3(("Http2StreamBase::WriteSegments %" PRIX32 "",
        static_cast<uint32_t>(rv)));
  mSegmentWriter = nullptr;
  return rv;
}

nsresult Http2StreamBase::ParseHttpRequestHeaders(const char* buf,
                                                  uint32_t avail,
                                                  uint32_t* countUsed) {
  // Returns NS_OK even if the headers are incomplete
  // set mRequestHeadersDone flag if they are complete

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(mUpstreamState == GENERATING_HEADERS);
  MOZ_ASSERT(!mRequestHeadersDone);

  LOG3(("Http2StreamBase::ParseHttpRequestHeaders %p avail=%d state=%x", this,
        avail, mUpstreamState));

  mFlatHttpRequestHeaders.Append(buf, avail);

  // We can use the simple double crlf because firefox is the
  // only client we are parsing
  int32_t endHeader = mFlatHttpRequestHeaders.Find("\r\n\r\n");

  if (endHeader == kNotFound) {
    // We don't have all the headers yet
    LOG3(
        ("Http2StreamBase::ParseHttpRequestHeaders %p "
         "Need more header bytes. Len = %zd",
         this, mFlatHttpRequestHeaders.Length()));
    *countUsed = avail;
    return NS_OK;
  }

  // We have recvd all the headers, trim the local
  // buffer of the final empty line, and set countUsed to reflect
  // the whole header has been consumed.
  uint32_t oldLen = mFlatHttpRequestHeaders.Length();
  mFlatHttpRequestHeaders.SetLength(endHeader + 2);
  *countUsed = avail - (oldLen - endHeader) + 4;
  mRequestHeadersDone = 1;
  return NS_OK;
}

// This is really a headers frame, but open is pretty clear from a workflow pov
nsresult Http2StreamBase::GenerateOpen() {
  // It is now OK to assign a streamID that we are assured will
  // be monotonically increasing amongst new streams on this
  // session
  RefPtr<Http2Session> session = Session();
  mStreamID = session->RegisterStreamID(this);
  MOZ_ASSERT(mStreamID & 1, "Http2 Stream Channel ID must be odd");
  MOZ_ASSERT(!mOpenGenerated);

  mOpenGenerated = 1;

  LOG3(("Http2StreamBase %p Stream ID 0x%X [session=%p]\n", this, mStreamID,
        session.get()));

  if (mStreamID >= 0x80000000) {
    // streamID must fit in 31 bits. Evading This is theoretically possible
    // because stream ID assignment is asynchronous to stream creation
    // because of the protocol requirement that the new stream ID
    // be monotonically increasing. In reality this is really not possible
    // because new streams stop being added to a session with millions of
    // IDs still available and no race condition is going to bridge that gap;
    // so we can be comfortable on just erroring out for correctness in that
    // case.
    LOG3(("Stream assigned out of range ID: 0x%X", mStreamID));
    return NS_ERROR_UNEXPECTED;
  }

  // Now we need to convert the flat http headers into a set
  // of HTTP/2 headers by writing to mTxInlineFrame{sz}

  nsCString compressedData;
  uint8_t firstFrameFlags = Http2Session::kFlag_PRIORITY;

  nsresult rv = GenerateHeaders(compressedData, firstFrameFlags);
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (firstFrameFlags & Http2Session::kFlag_END_STREAM) {
    SetSentFin(true);
  }

  // split this one HEADERS frame up into N HEADERS + CONTINUATION frames if it
  // exceeds the 2^14-1 limit for 1 frame. Do it by inserting header size gaps
  // in the existing frame for the new headers and for the first one a priority
  // field. There is no question this is ugly, but a 16KB HEADERS frame should
  // be a long tail event, so this is really just for correctness and a nop in
  // the base case.
  //

  MOZ_ASSERT(!mTxInlineFrameUsed);

  uint32_t dataLength = compressedData.Length();
  uint32_t maxFrameData =
      Http2Session::kMaxFrameData - 5;  // 5 bytes for priority
  uint32_t numFrames = 1;

  if (dataLength > maxFrameData) {
    numFrames +=
        ((dataLength - maxFrameData) + Http2Session::kMaxFrameData - 1) /
        Http2Session::kMaxFrameData;
    MOZ_ASSERT(numFrames > 1);
  }

  // note that we could still have 1 frame for 0 bytes of data. that's ok.

  uint32_t messageSize = dataLength;
  messageSize += Http2Session::kFrameHeaderBytes +
                 5;  // frame header + priority overhead in HEADERS frame
  messageSize += (numFrames - 1) *
                 Http2Session::kFrameHeaderBytes;  // frame header overhead in
                                                   // CONTINUATION frames

  EnsureBuffer(mTxInlineFrame, messageSize, mTxInlineFrameUsed,
               mTxInlineFrameSize);

  mTxInlineFrameUsed += messageSize;
  UpdatePriorityDependency();
  LOG1(
      ("Http2StreamBase %p Generating %d bytes of HEADERS for stream 0x%X with "
       "priority weight %u dep 0x%X frames %u\n",
       this, mTxInlineFrameUsed, mStreamID, mPriorityWeight,
       mPriorityDependency, numFrames));

  uint32_t outputOffset = 0;
  uint32_t compressedDataOffset = 0;
  for (uint32_t idx = 0; idx < numFrames; ++idx) {
    uint32_t flags, frameLen;
    bool lastFrame = (idx == numFrames - 1);

    flags = 0;
    frameLen = maxFrameData;
    if (!idx) {
      flags |= firstFrameFlags;
      // Only the first frame needs the 4-byte offset
      maxFrameData = Http2Session::kMaxFrameData;
    }
    if (lastFrame) {
      frameLen = dataLength;
      flags |= Http2Session::kFlag_END_HEADERS;
    }
    dataLength -= frameLen;

    session->CreateFrameHeader(mTxInlineFrame.get() + outputOffset,
                               frameLen + (idx ? 0 : 5),
                               (idx) ? Http2Session::FRAME_TYPE_CONTINUATION
                                     : Http2Session::FRAME_TYPE_HEADERS,
                               flags, mStreamID);
    outputOffset += Http2Session::kFrameHeaderBytes;

    if (!idx) {
      uint32_t wireDep = PR_htonl(mPriorityDependency);
      memcpy(mTxInlineFrame.get() + outputOffset, &wireDep, 4);
      memcpy(mTxInlineFrame.get() + outputOffset + 4, &mPriorityWeight, 1);
      outputOffset += 5;
    }

    memcpy(mTxInlineFrame.get() + outputOffset,
           compressedData.BeginReading() + compressedDataOffset, frameLen);
    compressedDataOffset += frameLen;
    outputOffset += frameLen;
  }

  mFlatHttpRequestHeaders.Truncate();

  return NS_OK;
}

void Http2StreamBase::AdjustInitialWindow() {
  // The default initial_window is sized for pushed streams. When we
  // generate a client pulled stream we want to disable flow control for
  // the stream with a window update. Do the same for pushed streams
  // when they connect to a pull.

  uint32_t wireStreamId = GetWireStreamId();
  if (wireStreamId == 0) {
    return;
  }

  // right now mClientReceiveWindow is the lower push limit
  // bump it up to the pull limit set by the channel or session
  // don't allow windows less than push
  uint32_t bump = 0;
  RefPtr<Http2Session> session = Session();
  nsHttpTransaction* trans = HttpTransaction();
  if (trans && trans->InitialRwin()) {
    bump = (trans->InitialRwin() > mClientReceiveWindow)
               ? (trans->InitialRwin() - mClientReceiveWindow)
               : 0;
  } else {
    MOZ_ASSERT(session->InitialRwin() >= mClientReceiveWindow);
    bump = session->InitialRwin() - mClientReceiveWindow;
  }

  LOG3(("AdjustInitialwindow increased flow control window %p 0x%X %u\n", this,
        wireStreamId, bump));
  if (!bump) {  // nothing to do
    return;
  }

  EnsureBuffer(mTxInlineFrame,
               mTxInlineFrameUsed + Http2Session::kFrameHeaderBytes + 4,
               mTxInlineFrameUsed, mTxInlineFrameSize);
  uint8_t* packet = mTxInlineFrame.get() + mTxInlineFrameUsed;
  mTxInlineFrameUsed += Http2Session::kFrameHeaderBytes + 4;

  session->CreateFrameHeader(packet, 4, Http2Session::FRAME_TYPE_WINDOW_UPDATE,
                             0, wireStreamId);

  mClientReceiveWindow += bump;
  bump = PR_htonl(bump);
  memcpy(packet + Http2Session::kFrameHeaderBytes, &bump, 4);
}

void Http2StreamBase::UpdateTransportReadEvents(uint32_t count) {
  mTotalRead += count;
  if (!mSocketTransport) {
    return;
  }

  if (Transaction()) {
    Transaction()->OnTransportStatus(mSocketTransport,
                                     NS_NET_STATUS_RECEIVING_FROM, mTotalRead);
  }
}

void Http2StreamBase::UpdateTransportSendEvents(uint32_t count) {
  mTotalSent += count;

  // Setting the TCP send buffer, introduced in
  // https://bugzilla.mozilla.org/show_bug.cgi?id=790184, which the following
  // comment refers to, is being removed once we verify no increases in error
  // rate.
  //
  // normally on non-windows platform we use TCP autotuning for
  // the socket buffers, and this works well (managing enough
  // buffers for BDP while conserving memory) for HTTP even when
  // it creates really deep queues. However this 'buffer bloat' is
  // a problem for http/2 because it ruins the low latency properties
  // necessary for PING and cancel to work meaningfully.

  // If this stream represents a large upload, disable autotuning for
  // the session and cap the send buffers by default at 128KB.
  // (10Mbit/sec @ 100ms)
  //
  uint32_t bufferSize = gHttpHandler->SpdySendBufferSize();
  if (StaticPrefs::network_http_http2_send_buffer_size() > 0 &&
      (mTotalSent > bufferSize) && !mSetTCPSocketBuffer) {
    mSetTCPSocketBuffer = 1;
    mSocketTransport->SetSendBufferSize(bufferSize);
  }

  if ((mUpstreamState != SENDING_FIN_STREAM) && Transaction()) {
    Transaction()->OnTransportStatus(mSocketTransport, NS_NET_STATUS_SENDING_TO,
                                     mTotalSent);
  }

  if (!mSentWaitingFor && !mRequestBodyLenRemaining) {
    mSentWaitingFor = 1;
    if (Transaction()) {
      Transaction()->OnTransportStatus(mSocketTransport,
                                       NS_NET_STATUS_WAITING_FOR, 0);
    }
  }
}

nsresult Http2StreamBase::TransmitFrame(const char* buf, uint32_t* countUsed,
                                        bool forceCommitment) {
  // If TransmitFrame returns SUCCESS than all the data is sent (or at least
  // buffered at the session level), if it returns WOULD_BLOCK then none of
  // the data is sent.

  // You can call this function with no data and no out parameter in order to
  // flush internal buffers that were previously blocked on writing. You can
  // of course feed new data to it as well.

  LOG3(("Http2StreamBase::TransmitFrame %p inline=%d stream=%d", this,
        mTxInlineFrameUsed, mTxStreamFrameSize));
  if (countUsed) *countUsed = 0;

  if (!mTxInlineFrameUsed) {
    MOZ_ASSERT(!buf);
    return NS_OK;
  }

  MOZ_ASSERT(mTxInlineFrameUsed, "empty stream frame in transmit");
  MOZ_ASSERT(mSegmentReader, "TransmitFrame with null mSegmentReader");
  MOZ_ASSERT((buf && countUsed) || (!buf && !countUsed),
             "TransmitFrame arguments inconsistent");

  uint32_t transmittedCount;
  nsresult rv;
  RefPtr<Http2Session> session = Session();

  // In the (relatively common) event that we have a small amount of data
  // split between the inlineframe and the streamframe, then move the stream
  // data into the inlineframe via copy in order to coalesce into one write.
  // Given the interaction with ssl this is worth the small copy cost.
  if (mTxStreamFrameSize && mTxInlineFrameUsed &&
      mTxStreamFrameSize < Http2Session::kDefaultBufferSize &&
      mTxInlineFrameUsed + mTxStreamFrameSize < mTxInlineFrameSize) {
    LOG3(("Coalesce Transmit"));
    memcpy(&mTxInlineFrame[mTxInlineFrameUsed], buf, mTxStreamFrameSize);
    if (countUsed) *countUsed += mTxStreamFrameSize;
    mTxInlineFrameUsed += mTxStreamFrameSize;
    mTxStreamFrameSize = 0;
  }

  rv = mSegmentReader->CommitToSegmentSize(
      mTxStreamFrameSize + mTxInlineFrameUsed, forceCommitment);

  if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
    MOZ_ASSERT(!forceCommitment, "forceCommitment with WOULD_BLOCK");
    session->TransactionHasDataToWrite(this);
  }
  if (NS_FAILED(rv)) {  // this will include WOULD_BLOCK
    return rv;
  }

  // This function calls mSegmentReader->OnReadSegment to report the actual
  // http/2 bytes through to the session object and then the HttpConnection
  // which calls the socket write function. It will accept all of the inline and
  // stream data because of the above 'commitment' even if it has to buffer

  rv = session->BufferOutput(reinterpret_cast<char*>(mTxInlineFrame.get()),
                             mTxInlineFrameUsed, &transmittedCount);
  LOG3(
      ("Http2StreamBase::TransmitFrame for inline BufferOutput session=%p "
       "stream=%p result %" PRIx32 " len=%d",
       session.get(), this, static_cast<uint32_t>(rv), transmittedCount));

  MOZ_ASSERT(rv != NS_BASE_STREAM_WOULD_BLOCK,
             "inconsistent inline commitment result");

  if (NS_FAILED(rv)) return rv;

  MOZ_ASSERT(transmittedCount == mTxInlineFrameUsed,
             "inconsistent inline commitment count");

  Http2Session::LogIO(session, this, "Writing from Inline Buffer",
                      reinterpret_cast<char*>(mTxInlineFrame.get()),
                      transmittedCount);

  if (mTxStreamFrameSize) {
    if (!buf) {
      // this cannot happen
      MOZ_ASSERT(false,
                 "Stream transmit with null buf argument to "
                 "TransmitFrame()");
      LOG3(("Stream transmit with null buf argument to TransmitFrame()\n"));
      return NS_ERROR_UNEXPECTED;
    }

    // If there is already data buffered, just add to that to form
    // a single TLS Application Data Record - otherwise skip the memcpy
    if (session->AmountOfOutputBuffered()) {
      rv = session->BufferOutput(buf, mTxStreamFrameSize, &transmittedCount);
    } else {
      rv = session->OnReadSegment(buf, mTxStreamFrameSize, &transmittedCount);
    }

    LOG3(
        ("Http2StreamBase::TransmitFrame for regular session=%p "
         "stream=%p result %" PRIx32 " len=%d",
         session.get(), this, static_cast<uint32_t>(rv), transmittedCount));

    MOZ_ASSERT(rv != NS_BASE_STREAM_WOULD_BLOCK,
               "inconsistent stream commitment result");

    if (NS_FAILED(rv)) return rv;

    MOZ_ASSERT(transmittedCount == mTxStreamFrameSize,
               "inconsistent stream commitment count");

    Http2Session::LogIO(session, this, "Writing from Transaction Buffer", buf,
                        transmittedCount);

    *countUsed += mTxStreamFrameSize;
  }

  if (!mAttempting0RTT) {
    session->FlushOutputQueue();
  }

  // calling this will trigger waiting_for if mRequestBodyLenRemaining is 0
  UpdateTransportSendEvents(mTxInlineFrameUsed + mTxStreamFrameSize);

  mTxInlineFrameUsed = 0;
  mTxStreamFrameSize = 0;

  return NS_OK;
}

void Http2StreamBase::ChangeState(enum upstreamStateType newState) {
  LOG3(("Http2StreamBase::ChangeState() %p from %X to %X", this, mUpstreamState,
        newState));
  mUpstreamState = newState;
}

void Http2StreamBase::GenerateDataFrameHeader(uint32_t dataLength,
                                              bool lastFrame) {
  LOG3(("Http2StreamBase::GenerateDataFrameHeader %p len=%d last=%d", this,
        dataLength, lastFrame));

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  MOZ_ASSERT(!mTxInlineFrameUsed, "inline frame not empty");
  MOZ_ASSERT(!mTxStreamFrameSize, "stream frame not empty");

  uint8_t frameFlags = 0;
  if (lastFrame) {
    frameFlags |= Http2Session::kFlag_END_STREAM;
    if (dataLength) SetSentFin(true);
  }

  RefPtr<Http2Session> session = Session();
  session->CreateFrameHeader(mTxInlineFrame.get(), dataLength,
                             Http2Session::FRAME_TYPE_DATA, frameFlags,
                             mStreamID);

  mTxInlineFrameUsed = Http2Session::kFrameHeaderBytes;
  mTxStreamFrameSize = dataLength;
}

// ConvertResponseHeaders is used to convert the response headers
// into HTTP/1 format and report some telemetry
nsresult Http2StreamBase::ConvertResponseHeaders(
    Http2Decompressor* decompressor, nsACString& aHeadersIn,
    nsACString& aHeadersOut, int32_t& httpResponseCode) {
  nsresult rv = decompressor->DecodeHeaderBlock(
      reinterpret_cast<const uint8_t*>(aHeadersIn.BeginReading()),
      aHeadersIn.Length(), aHeadersOut, false);
  if (NS_FAILED(rv)) {
    LOG3(("Http2StreamBase::ConvertResponseHeaders %p decode Error\n", this));
    return rv;
  }

  nsAutoCString statusString;
  decompressor->GetStatus(statusString);
  if (statusString.IsEmpty()) {
    LOG3(("Http2StreamBase::ConvertResponseHeaders %p Error - no status\n",
          this));
    return NS_ERROR_ILLEGAL_VALUE;
  }

  nsresult errcode;
  httpResponseCode = statusString.ToInteger(&errcode);

  // Ensure the :status is just an HTTP status code
  // https://tools.ietf.org/html/rfc7540#section-8.1.2.4
  // https://bugzilla.mozilla.org/show_bug.cgi?id=1352146
  nsAutoCString parsedStatusString;
  parsedStatusString.AppendInt(httpResponseCode);
  if (!parsedStatusString.Equals(statusString)) {
    LOG3(
        ("Http2StreamBase::ConvertResposeHeaders %p status %s is not just a "
         "code",
         this, statusString.get()));
    // Results in stream reset with PROTOCOL_ERROR
    return NS_ERROR_ILLEGAL_VALUE;
  }

  LOG3(("Http2StreamBase::ConvertResponseHeaders %p response code %d\n", this,
        httpResponseCode));

  if (httpResponseCode == 421) {
    // Origin Frame requires 421 to remove this origin from the origin set
    RefPtr<Http2Session> session = Session();
    session->Received421(ConnectionInfo());
  }

  // The decoding went ok. Now we can customize and clean up.

  aHeadersIn.Truncate();
  aHeadersOut.AppendLiteral("X-Firefox-Spdy: h2");
  aHeadersOut.AppendLiteral("\r\n\r\n");
  LOG(("decoded response headers are:\n%s",
       PromiseFlatCString(aHeadersOut).get()));
  HandleResponseHeaders(aHeadersOut, httpResponseCode);

  return NS_OK;
}

nsresult Http2StreamBase::ConvertResponseTrailers(
    Http2Decompressor* decompressor, nsACString& aTrailersIn) {
  LOG3(("Http2StreamBase::ConvertResponseTrailers %p", this));
  nsAutoCString flatTrailers;

  nsresult rv = decompressor->DecodeHeaderBlock(
      reinterpret_cast<const uint8_t*>(aTrailersIn.BeginReading()),
      aTrailersIn.Length(), flatTrailers, false);
  if (NS_FAILED(rv)) {
    LOG3(("Http2StreamBase::ConvertResponseTrailers %p decode Error", this));
    return rv;
  }

  nsHttpTransaction* trans = HttpTransaction();
  if (trans) {
    trans->SetHttpTrailers(flatTrailers);
  } else {
    LOG3(("Http2StreamBase::ConvertResponseTrailers %p no trans", this));
  }

  return NS_OK;
}

void Http2StreamBase::SetResponseIsComplete() {
  nsHttpTransaction* trans = HttpTransaction();
  if (trans) {
    trans->SetResponseIsComplete();
  }
}

void Http2StreamBase::SetAllHeadersReceived() {
  if (mAllHeadersReceived) {
    return;
  }

  if (mState == RESERVED_BY_REMOTE) {
    // pushed streams needs to wait until headers have
    // arrived to open up their window
    LOG3(
        ("Http2StreamBase::SetAllHeadersReceived %p state OPEN from reserved\n",
         this));
    mState = OPEN;
    AdjustInitialWindow();
  }

  mAllHeadersReceived = 1;
}

bool Http2StreamBase::AllowFlowControlledWrite() {
  RefPtr<Http2Session> session = Session();
  return (session->ServerSessionWindow() > 0) && (mServerReceiveWindow > 0);
}

void Http2StreamBase::UpdateServerReceiveWindow(int32_t delta) {
  mServerReceiveWindow += delta;

  if (mBlockedOnRwin && AllowFlowControlledWrite()) {
    LOG3(
        ("Http2StreamBase::UpdateServerReceived UnPause %p 0x%X "
         "Open stream window\n",
         this, mStreamID));
    RefPtr<Http2Session> session = Session();
    session->TransactionHasDataToWrite(this);
  }
}

void Http2StreamBase::SetPriority(uint32_t newPriority) {
  int32_t httpPriority = static_cast<int32_t>(newPriority);
  if (httpPriority > kWorstPriority) {
    httpPriority = kWorstPriority;
  } else if (httpPriority < kBestPriority) {
    httpPriority = kBestPriority;
  }
  mRFC7540Priority = static_cast<uint32_t>(httpPriority);
  mPriorityWeight = (nsISupportsPriority::PRIORITY_LOWEST + 1) -
                    (httpPriority - kNormalPriority);

  mPriorityDependency = 0;  // maybe adjusted later
}

void Http2StreamBase::SetPriorityDependency(uint32_t newPriority,
                                            uint32_t newDependency) {
  SetPriority(newPriority);
  mPriorityDependency = newDependency;
}

static uint32_t GetPriorityDependencyFromTransaction(nsHttpTransaction* trans) {
  MOZ_ASSERT(trans);

  uint32_t classFlags = trans->GetClassOfService().Flags();

  if (classFlags & nsIClassOfService::UrgentStart) {
    return Http2Session::kUrgentStartGroupID;
  }

  if (classFlags & nsIClassOfService::Leader) {
    return Http2Session::kLeaderGroupID;
  }

  if (classFlags & nsIClassOfService::Follower) {
    return Http2Session::kFollowerGroupID;
  }

  if (classFlags & nsIClassOfService::Speculative) {
    return Http2Session::kSpeculativeGroupID;
  }

  if (classFlags & nsIClassOfService::Background) {
    return Http2Session::kBackgroundGroupID;
  }

  if (classFlags & nsIClassOfService::Unblocked) {
    return Http2Session::kOtherGroupID;
  }

  return Http2Session::kFollowerGroupID;  // unmarked followers
}

void Http2StreamBase::UpdatePriorityDependency() {
  RefPtr<Http2Session> session = Session();
  if (!session->UseH2Deps()) {
    return;
  }

  nsHttpTransaction* trans = HttpTransaction();
  if (!trans) {
    return;
  }

  // we create 6 fake dependency streams per session,
  // these streams are never opened with HEADERS. our first opened stream is 0xd
  // 3 depends 0, weight 200, leader class (kLeaderGroupID)
  // 5 depends 0, weight 100, other (kOtherGroupID)
  // 7 depends 0, weight 0, background (kBackgroundGroupID)
  // 9 depends 7, weight 0, speculative (kSpeculativeGroupID)
  // b depends 3, weight 0, follower class (kFollowerGroupID)
  // d depends 0, weight 240, urgent-start class (kUrgentStartGroupID)
  //
  // streams for leaders (html, js, css) depend on 3
  // streams for folowers (images) depend on b
  // default streams (xhr, async js) depend on 5
  // explicit bg streams (beacon, etc..) depend on 7
  // spculative bg streams depend on 9
  // urgent-start streams depend on d

  mPriorityDependency = GetPriorityDependencyFromTransaction(trans);

  if (StaticPrefs::network_http_active_tab_priority() &&
      mTransactionBrowserId != mCurrentBrowserId &&
      mPriorityDependency != Http2Session::kUrgentStartGroupID) {
    LOG3(
        ("Http2StreamBase::UpdatePriorityDependency %p "
         " depends on background group for trans %p\n",
         this, trans));
    mPriorityDependency = Http2Session::kBackgroundGroupID;

    nsHttp::NotifyActiveTabLoadOptimization();
  }

  LOG1(
      ("Http2StreamBase::UpdatePriorityDependency %p "
       "depends on stream 0x%X\n",
       this, mPriorityDependency));
}

void Http2StreamBase::CurrentBrowserIdChanged(uint64_t id) {
  if (!mStreamID) {
    // For pushed streams, we ignore the direct call from the session and
    // instead let it come to the internal function from the pushed stream, so
    // we don't accidentally send two PRIORITY frames for the same stream.
    return;
  }

  CurrentBrowserIdChangedInternal(id);
}

void Http2StreamBase::CurrentBrowserIdChangedInternal(uint64_t id) {
  MOZ_ASSERT(StaticPrefs::network_http_active_tab_priority());
  RefPtr<Http2Session> session = Session();
  LOG3(
      ("Http2StreamBase::CurrentBrowserIdChangedInternal "
       "%p browserId=%" PRIx64 "\n",
       this, id));

  mCurrentBrowserId = id;

  // Urgent start takes an absolute precedence, so don't
  // change mPriorityDependency here.
  if (mPriorityDependency == Http2Session::kUrgentStartGroupID) {
    return;
  }

  if (session->UseH2Deps()) {
    UpdatePriorityRFC7540(session);
  } else {
    UpdatePriority(session);
  }
}

void Http2StreamBase::UpdatePriority(Http2Session* session) {
  MOZ_ASSERT(!session->UseH2Deps());
  bool isInBackground = mTransactionBrowserId != mCurrentBrowserId;

  if (isInBackground) {
    LOG3(
        ("Http2StreamBase::CurrentBrowserIdChangedInternal %p "
         "move into background group.\n",
         this));

    nsHttp::NotifyActiveTabLoadOptimization();
  }

  if (!StaticPrefs::network_http_http2_priority_updates()) {
    return;
  }

  nsHttpTransaction* trans = HttpTransaction();
  if (!trans) {
    return;
  }

  uint8_t urgency = nsHttpHandler::UrgencyFromCoSFlags(
      trans->GetClassOfService().Flags(), trans->Priority());
  bool incremental = trans->GetClassOfService().Incremental();
  uint32_t streamID = GetWireStreamId();

  // If the tab is in the background
  // we can probably lower the priority of this request by 1.
  // (to get lower priority we increase urgency).
  if (isInBackground && urgency < 6) {
    urgency++;
  }

  if (streamID) {
    session->SendPriorityUpdateFrame(streamID, urgency, incremental);
  }
}

void Http2StreamBase::UpdatePriorityRFC7540(Http2Session* session) {
  MOZ_ASSERT(session->UseH2Deps());
  if (mTransactionBrowserId != mCurrentBrowserId) {
    mPriorityDependency = Http2Session::kBackgroundGroupID;
    LOG3(
        ("Http2StreamBase::CurrentBrowserIdChangedInternal %p "
         "move into background group.\n",
         this));

    nsHttp::NotifyActiveTabLoadOptimization();
  } else {
    nsHttpTransaction* trans = HttpTransaction();
    if (!trans) {
      return;
    }

    mPriorityDependency = GetPriorityDependencyFromTransaction(trans);
    LOG3(
        ("Http2StreamBase::CurrentBrowserIdChangedInternal %p "
         "depends on stream 0x%X\n",
         this, mPriorityDependency));
  }

  uint32_t modifyStreamID = GetWireStreamId();

  if (modifyStreamID) {
    session->SendPriorityFrame(modifyStreamID, mPriorityDependency,
                               mPriorityWeight);
  }
}

void Http2StreamBase::SetRecvdFin(bool aStatus) {
  mRecvdFin = aStatus ? 1 : 0;
  if (!aStatus) return;

  if (mState == OPEN || mState == RESERVED_BY_REMOTE) {
    mState = CLOSED_BY_REMOTE;
  } else if (mState == CLOSED_BY_LOCAL) {
    mState = CLOSED;
  }
}

void Http2StreamBase::SetSentFin(bool aStatus) {
  mSentFin = aStatus ? 1 : 0;
  if (!aStatus) return;

  if (mState == OPEN || mState == RESERVED_BY_REMOTE) {
    mState = CLOSED_BY_LOCAL;
  } else if (mState == CLOSED_BY_REMOTE) {
    mState = CLOSED;
  }
}

void Http2StreamBase::SetRecvdReset(bool aStatus) {
  mRecvdReset = aStatus ? 1 : 0;
  if (!aStatus) return;
  mState = CLOSED;
  mBlockedOnRwin = false;
}

void Http2StreamBase::SetSentReset(bool aStatus) {
  mSentReset = aStatus ? 1 : 0;
  if (!aStatus) return;
  mState = CLOSED;
}

//-----------------------------------------------------------------------------
// nsAHttpSegmentReader
//-----------------------------------------------------------------------------

nsresult Http2StreamBase::OnReadSegment(const char* buf, uint32_t count,
                                        uint32_t* countRead) {
  LOG3(("Http2StreamBase::OnReadSegment %p count=%d state=%x", this, count,
        mUpstreamState));

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  if (!mSegmentReader) {
    return NS_BASE_STREAM_WOULD_BLOCK;
  }

  nsresult rv = NS_ERROR_UNEXPECTED;
  uint32_t dataLength;
  RefPtr<Http2Session> session = Session();

  switch (mUpstreamState) {
    case GENERATING_HEADERS:
      // The buffer is the HTTP request stream, including at least part of the
      // HTTP request header. This state's job is to build a HEADERS frame
      // from the header information. count is the number of http bytes
      // available (which may include more than the header), and in countRead we
      // return the number of those bytes that we consume (i.e. the portion that
      // are header bytes)

      if (!mRequestHeadersDone) {
        if (NS_FAILED(rv = ParseHttpRequestHeaders(buf, count, countRead))) {
          return rv;
        }
      }

      if (mRequestHeadersDone && !mOpenGenerated) {
        if (!session->TryToActivate(this)) {
          LOG3(
              ("Http2StreamBase::OnReadSegment %p cannot activate now. "
               "queued.\n",
               this));
          return *countRead ? NS_OK : NS_BASE_STREAM_WOULD_BLOCK;
        }
        if (NS_FAILED(rv = GenerateOpen())) {
          return rv;
        }
      }

      LOG3(
          ("ParseHttpRequestHeaders %p used %d of %d. "
           "requestheadersdone = %d mOpenGenerated = %d\n",
           this, *countRead, count, mRequestHeadersDone, mOpenGenerated));
      if (mOpenGenerated) {
        SetHTTPState(OPEN);
        AdjustInitialWindow();
        // This version of TransmitFrame cannot block
        rv = TransmitFrame(nullptr, nullptr, true);
        ChangeState(GENERATING_BODY);
        break;
      }
      MOZ_ASSERT(*countRead == count,
                 "Header parsing not complete but unused data");
      break;

    case GENERATING_BODY:
      // if there is session flow control and either the stream window is active
      // and exhaused or the session window is exhausted then suspend
      if (!AllowFlowControlledWrite()) {
        *countRead = 0;
        LOG3(
            ("Http2StreamBase this=%p, id 0x%X request body suspended because "
             "remote window is stream=%" PRId64 " session=%" PRId64 ".\n",
             this, mStreamID, mServerReceiveWindow,
             session->ServerSessionWindow()));
        mBlockedOnRwin = true;
        return NS_BASE_STREAM_WOULD_BLOCK;
      }
      mBlockedOnRwin = false;

      // The chunk is the smallest of: availableData, configured chunkSize,
      // stream window, session window, or 14 bit framing limit.
      // Its amazing we send anything at all.
      dataLength = std::min(count, mChunkSize);

      if (dataLength > Http2Session::kMaxFrameData) {
        dataLength = Http2Session::kMaxFrameData;
      }

      if (dataLength > session->ServerSessionWindow()) {
        dataLength = static_cast<uint32_t>(session->ServerSessionWindow());
      }

      if (dataLength > mServerReceiveWindow) {
        dataLength = static_cast<uint32_t>(mServerReceiveWindow);
      }

      LOG3(
          ("Http2StreamBase this=%p id 0x%X send calculation "
           "avail=%d chunksize=%d stream window=%" PRId64
           " session window=%" PRId64 " "
           "max frame=%d USING=%u\n",
           this, mStreamID, count, mChunkSize, mServerReceiveWindow,
           session->ServerSessionWindow(), Http2Session::kMaxFrameData,
           dataLength));

      session->DecrementServerSessionWindow(dataLength);
      mServerReceiveWindow -= dataLength;

      LOG3(("Http2StreamBase %p id 0x%x request len remaining %" PRId64 ", "
            "count avail %u, chunk used %u",
            this, mStreamID, mRequestBodyLenRemaining, count, dataLength));
      if (!dataLength && mRequestBodyLenRemaining) {
        return NS_BASE_STREAM_WOULD_BLOCK;
      }
      // mRequestBodyLenRemaining < 0 means streaming upload with unknown
      // length; skip the length-bookkeeping and let END_STREAM be driven by
      // the 0-byte read that ends the body, via the FIN path in ReadSegments().
      if (mRequestBodyLenRemaining >= 0) {
        if (static_cast<int64_t>(dataLength) > mRequestBodyLenRemaining) {
          return NS_ERROR_UNEXPECTED;
        }
        mRequestBodyLenRemaining -= dataLength;
        GenerateDataFrameHeader(dataLength, !mRequestBodyLenRemaining);
      } else {
        GenerateDataFrameHeader(dataLength, false);
      }
      ChangeState(SENDING_BODY);
      [[fallthrough]];

    case SENDING_BODY:
      MOZ_ASSERT(mTxInlineFrameUsed, "OnReadSegment Send Data Header 0b");
      rv = TransmitFrame(buf, countRead, false);
      MOZ_ASSERT(NS_FAILED(rv) || !mTxInlineFrameUsed,
                 "Transmit Frame should be all or nothing");

      LOG3(("TransmitFrame() rv=%" PRIx32 " returning %d data bytes. "
            "Header is %d Body is %d.",
            static_cast<uint32_t>(rv), *countRead, mTxInlineFrameUsed,
            mTxStreamFrameSize));

      // normalize a partial write with a WOULD_BLOCK into just a partial write
      // as some code will take WOULD_BLOCK to mean an error with nothing
      // written (e.g. nsHttpTransaction::ReadRequestSegment()
      if (rv == NS_BASE_STREAM_WOULD_BLOCK && *countRead) rv = NS_OK;

      // If that frame was all sent, look for another one
      if (!mTxInlineFrameUsed) ChangeState(GENERATING_BODY);
      break;

    case SENDING_FIN_STREAM:
      MOZ_ASSERT(false, "resuming partial fin stream out of OnReadSegment");
      break;

    case UPSTREAM_COMPLETE: {
      MOZ_ASSERT(this->GetHttp2Stream());
      rv = TransmitFrame(nullptr, nullptr, true);
      break;
    }
    default:
      MOZ_ASSERT(false, "Http2StreamBase::OnReadSegment non-write state");
      break;
  }

  return rv;
}

//-----------------------------------------------------------------------------
// nsAHttpSegmentWriter
//-----------------------------------------------------------------------------

nsresult Http2StreamBase::OnWriteSegment(char* buf, uint32_t count,
                                         uint32_t* countWritten) {
  LOG3(("Http2StreamBase::OnWriteSegment %p count=%d state=%x 0x%X\n", this,
        count, mUpstreamState, mStreamID));

  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  if (!mSegmentWriter) {
    return NS_BASE_STREAM_WOULD_BLOCK;
  }

  // sometimes we have read data from the network and stored it in a pipe
  // so that other streams can proceed when the gecko caller is not processing
  // data events fast enough and flow control hasn't caught up yet. This
  // gets the stored data out of that pipe
  if (!mBypassInputBuffer && mSimpleBuffer.Available()) {
    *countWritten = mSimpleBuffer.Read(buf, count);
    MOZ_ASSERT(*countWritten);
    LOG3(
        ("Http2StreamBase::OnWriteSegment read from flow control buffer %p %x "
         "%d\n",
         this, mStreamID, *countWritten));
    return NS_OK;
  }

  // read from the network
  return mSegmentWriter->OnWriteSegment(buf, count, countWritten);
}

// -----------------------------------------------------------------------------
// mirror nsAHttpTransaction
// -----------------------------------------------------------------------------

bool Http2StreamBase::Do0RTT() {
  MOZ_ASSERT(Transaction());
  mAttempting0RTT = false;
  nsAHttpTransaction* trans = Transaction();
  if (trans) {
    mAttempting0RTT = trans->Do0RTT();
  }
  return mAttempting0RTT;
}

nsresult Http2StreamBase::Finish0RTT(bool aRestart, bool aAlpnChanged) {
  MOZ_ASSERT(Transaction());
  mAttempting0RTT = false;
  // Instead of passing (aRestart, aAlpnChanged) here, we use aAlpnChanged for
  // both arguments because as long as the alpn token stayed the same, we can
  // just reuse what we have in our buffer to send instead of having to have
  // the transaction rewind and read it all over again. We only need to rewind
  // the transaction if we're switching to a new protocol, because our buffer
  // won't get used in that case.
  // ..
  // however, we send in the aRestart value to indicate that early data failed
  // for devtools purposes
  nsresult rv = NS_OK;
  nsAHttpTransaction* trans = Transaction();
  if (trans) {
    rv = trans->Finish0RTT(aAlpnChanged, aAlpnChanged);
    if (aRestart) {
      nsHttpTransaction* hTrans = trans->QueryHttpTransaction();
      if (hTrans) {
        hTrans->Refused0RTT();
      }
    }
  }
  return rv;
}

nsresult Http2StreamBase::GetOriginAttributes(mozilla::OriginAttributes* oa) {
  if (!mSocketTransport) {
    return NS_ERROR_UNEXPECTED;
  }

  return mSocketTransport->GetOriginAttributes(oa);
}

nsHttpTransaction* Http2StreamBase::HttpTransaction() {
  return (Transaction()) ? Transaction()->QueryHttpTransaction() : nullptr;
}

nsHttpConnectionInfo* Http2StreamBase::ConnectionInfo() {
  if (Transaction()) {
    return Transaction()->ConnectionInfo();
  }
  return nullptr;
}

}  // namespace mozilla::net
