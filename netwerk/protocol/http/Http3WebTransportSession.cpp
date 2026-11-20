/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "Http3WebTransportSession.h"

#include "Http3Session.h"
#include "Http3Stream.h"
#include "Http3WebTransportStream.h"
#include "HttpLog.h"
#include "nsHttpHandler.h"
#include "nsHttpRequestHead.h"
#include "nsHttpTransaction.h"
#include "nsIClassOfService.h"
#include "nsIOService.h"
#include "nsISocketTransport.h"
#include "nsSocketTransportService2.h"

namespace mozilla::net {

//-----------------------------------------------------------------------------
// Http3TunnelStreamBase
//-----------------------------------------------------------------------------

Http3TunnelStreamBase::Http3TunnelStreamBase(nsAHttpTransaction* trans,
                                             Http3SessionBase* aHttp3Session)
    : Http3StreamBase(trans, aHttp3Session) {}

bool Http3TunnelStreamBase::ConsumeHeaders(const char* buf, uint32_t avail,
                                           uint32_t* countUsed) {
  LOG3(("Http3TunnelStreamBase::ConsumeHeaders %p avail=%u.", this, avail));

  mFlatHttpRequestHeaders.Append(buf, avail);
  // We can use the simple double crlf because firefox is the
  // only client we are parsing
  int32_t endHeader = mFlatHttpRequestHeaders.Find("\r\n\r\n");

  if (endHeader == kNotFound) {
    // We don't have all the headers yet
    LOG3(
        ("Http3TunnelStreamBase::ConsumeHeaders %p "
         "Need more header bytes. Len = %zu",
         this, mFlatHttpRequestHeaders.Length()));
    *countUsed = avail;
    return false;
  }

  uint32_t oldLen = mFlatHttpRequestHeaders.Length();
  mFlatHttpRequestHeaders.SetLength(endHeader + 2);
  *countUsed = avail - (oldLen - endHeader) + 4;

  return true;
}

nsresult Http3TunnelStreamBase::TryActivating() {
  LOG(("Http3TunnelStreamBase::TryActivating [this=%p]", this));
  const nsHttpRequestHead* head = mTransaction->RequestHead();

  nsAutoCString host;
  nsresult rv = head->GetHeader(nsHttp::Host, host);
  if (NS_FAILED(rv)) {
    MOZ_ASSERT(false);
    return rv;
  }
  nsAutoCString path;
  head->Path(path);

  return mSession->TryActivating(""_ns, ""_ns, host, path,
                                 mFlatHttpRequestHeaders, &mStreamId, this);
}

nsresult Http3TunnelStreamBase::ReadSegments() {
  LOG(("Http3TunnelStreamBase::ReadSegments %p mSendState=%d mRecvState=%d",
       this, mSendState, mRecvState));
  if (mSendState == PROCESSING_DATAGRAM) {
    return OnProcessDatagram();
  }

  if ((mRecvState == RECV_DONE) || (mRecvState == ACTIVE) ||
      (mRecvState == CLOSE_PENDING)) {
    // Don't transmit any request frames if the peer cannot respond or respone
    // is already done.
    LOG3(
        ("Http3TunnelStreamBase %p ReadSegments request stream aborted due to"
         " response side closure\n",
         this));
    return NS_ERROR_ABORT;
  }

  nsresult rv = NS_OK;
  uint32_t transactionBytes;
  bool again = true;
  do {
    transactionBytes = 0;
    rv = mSocketOutCondition = NS_OK;
    LOG(("Http3TunnelStreamBase::ReadSegments state=%d [this=%p]", mSendState,
         this));
    switch (mSendState) {
      case PREPARING_HEADERS: {
        rv = mTransaction->ReadSegmentsAgain(
            this, nsIOService::gDefaultSegmentSize, &transactionBytes, &again);
      } break;
      case WAITING_TO_ACTIVATE: {
        // A transaction that had already generated its headers before it was
        // queued at the session level (due to concurrency concerns) may not
        // call onReadSegment off the ReadSegments() stack above.
        LOG3(
            ("Http3TunnelStreamBase %p ReadSegments forcing OnReadSegment "
             "call\n",
             this));
        uint32_t wasted = 0;
        nsresult rv2 = OnReadSegment("", 0, &wasted);
        LOG3(("  OnReadSegment returned 0x%08" PRIx32,
              static_cast<uint32_t>(rv2)));
      } break;
      default:
        transactionBytes = 0;
        rv = NS_OK;
        break;
    }

    LOG(("Http3TunnelStreamBase::ReadSegments rv=0x%" PRIx32
         " read=%u sock-cond=%" PRIx32 " again=%d [this=%p]",
         static_cast<uint32_t>(rv), transactionBytes,
         static_cast<uint32_t>(mSocketOutCondition), again, this));

    // XXX some streams return NS_BASE_STREAM_CLOSED to indicate EOF.
    if (rv == NS_BASE_STREAM_CLOSED && !mTransaction->IsDone()) {
      rv = NS_OK;
      transactionBytes = 0;
    }

    if (NS_FAILED(rv)) {
      // if the transaction didn't want to write any more data, then
      // wait for the transaction to call ResumeSend.
      if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
        rv = NS_OK;
      }
      again = false;
    } else if (NS_FAILED(mSocketOutCondition)) {
      if (mSocketOutCondition != NS_BASE_STREAM_WOULD_BLOCK) {
        rv = mSocketOutCondition;
      }
      again = false;
    } else if (!transactionBytes) {
      mTransaction->OnTransportStatus(nullptr, NS_NET_STATUS_WAITING_FOR, 0);

      rv = NS_OK;
      again = false;
    }
    // write more to the socket until error or end-of-request...
  } while (again && gHttpHandler->Active());
  return rv;
}

nsresult Http3TunnelStreamBase::WriteSegments() {
  LOG(("Http3TunnelStreamBase::WriteSegments [this=%p]", this));
  nsresult rv = NS_OK;
  uint32_t countWrittenSingle = 0;
  bool again = true;

  if (mRecvState == CLOSE_PENDING) {
    OnClosePending();
    mRecvState = RECV_DONE;
    // This will closed the steam because the stream is Done().
    return NS_OK;
  }

  do {
    mSocketInCondition = NS_OK;
    countWrittenSingle = 0;
    rv = mTransaction->WriteSegmentsAgain(
        this, nsIOService::gDefaultSegmentSize, &countWrittenSingle, &again);
    LOG(("Http3TunnelStreamBase::WriteSegments rv=0x%" PRIx32
         " countWrittenSingle=%" PRIu32 " socketin=%" PRIx32 " [this=%p]",
         static_cast<uint32_t>(rv), countWrittenSingle,
         static_cast<uint32_t>(mSocketInCondition), this));
    if (mTransaction->IsDone()) {
      // An HTTP transaction used for setting up a  WebTransport session will
      // receive only response headers and afterward, it will be marked as
      // done. At this point, the session negotiation has finished and the
      // WebTransport session transfers into the ACTIVE state.
      mRecvState = ACTIVE;
    }

    if (NS_FAILED(rv)) {
      // if the transaction didn't want to take any more data, then
      // wait for the transaction to call ResumeRecv.
      if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
        rv = NS_OK;
      }
      again = false;
    } else if (NS_FAILED(mSocketInCondition)) {
      if (mSocketInCondition != NS_BASE_STREAM_WOULD_BLOCK) {
        rv = mSocketInCondition;
      }
      again = false;
    } else if (mRecvState == ACTIVE) {
      // again = false;
      again = OnActivated();
    }
    // read more from the socket until error...
  } while (again && gHttpHandler->Active());

  return rv;
}

void Http3TunnelStreamBase::SetResponseHeaders(
    nsTArray<uint8_t>& aResponseHeaders, bool fin, bool interim) {
  MOZ_ASSERT(mRecvState == BEFORE_HEADERS ||
             mRecvState == READING_INTERIM_HEADERS);
  mFlatResponseHeaders.AppendElements(aResponseHeaders);
  if (!interim) {
    mSavedFlatResponseHeaders.AppendElements(aResponseHeaders);
  }
  mRecvState = (interim) ? READING_INTERIM_HEADERS : READING_HEADERS;
}

nsresult Http3TunnelStreamBase::OnWriteSegment(char* buf, uint32_t count,
                                               uint32_t* countWritten) {
  LOG(("Http3TunnelStreamBase::OnWriteSegment [this=%p, state=%d", this,
       mRecvState));
  nsresult rv = NS_OK;
  switch (mRecvState) {
    case BEFORE_HEADERS: {
      *countWritten = 0;
      rv = NS_BASE_STREAM_WOULD_BLOCK;
    } break;
    case READING_HEADERS:
    case READING_INTERIM_HEADERS: {
      // SetResponseHeaders should have been previously called.
      MOZ_ASSERT(!mFlatResponseHeaders.IsEmpty(), "Headers empty!");
      *countWritten = (mFlatResponseHeaders.Length() > count)
                          ? count
                          : mFlatResponseHeaders.Length();
      memcpy(buf, mFlatResponseHeaders.Elements(), *countWritten);

      mFlatResponseHeaders.RemoveElementsAt(0, *countWritten);
      if (mFlatResponseHeaders.Length() == 0) {
        if (mRecvState == READING_INTERIM_HEADERS) {
          // neqo makes sure that fin cannot be received before the final
          // headers are received.
          mRecvState = BEFORE_HEADERS;
        } else {
          mRecvState = ACTIVE;
        }
      }

      if (*countWritten == 0) {
        rv = NS_BASE_STREAM_WOULD_BLOCK;
      } else {
        mTransaction->OnTransportStatus(nullptr, NS_NET_STATUS_RECEIVING_FROM,
                                        0);
      }
    } break;
    case ACTIVE:
    case CLOSE_PENDING:
    case RECV_DONE:
      rv = NS_ERROR_UNEXPECTED;
  }

  // Remember the error received from lower layers. A stream pipe may overwrite
  // it.
  // If rv == NS_OK this will reset mSocketInCondition.
  mSocketInCondition = rv;

  return rv;
}

nsresult Http3TunnelStreamBase::OnReadSegment(const char* buf, uint32_t count,
                                              uint32_t* countRead) {
  LOG(("Http3TunnelStreamBase::OnReadSegment count=%u state=%d [this=%p]",
       count, mSendState, this));

  nsresult rv = NS_OK;

  switch (mSendState) {
    case PREPARING_HEADERS: {
      if (!ConsumeHeaders(buf, count, countRead)) {
        break;
      }
      mSendState = WAITING_TO_ACTIVATE;
    }
      [[fallthrough]];
    case WAITING_TO_ACTIVATE:
      rv = TryActivating();
      if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
        LOG3(
            ("Http3TunnelStreamBase::OnReadSegment %p cannot activate now. "
             "queued.\n",
             this));
        break;
      }
      if (NS_FAILED(rv)) {
        LOG3(
            ("Http3TunnelStreamBase::OnReadSegment %p cannot activate "
             "error=0x%" PRIx32 ".",
             this, static_cast<uint32_t>(rv)));
        break;
      }

      // Successfully activated.
      mTransaction->OnTransportStatus(nullptr, NS_NET_STATUS_SENDING_TO, 0);

      mSendState = PROCESSING_DATAGRAM;
      break;
    default:
      MOZ_ASSERT(false, "We are done sending this request!");
      rv = NS_ERROR_UNEXPECTED;
      break;
  }

  mSocketOutCondition = rv;

  return mSocketOutCondition;
}

void Http3TunnelStreamBase::TransactionIsDone(nsresult aResult) {
  mTransaction->Close(aResult);
  mTransaction = nullptr;
}

//-----------------------------------------------------------------------------
// Http3WebTransportSession
//-----------------------------------------------------------------------------

Http3WebTransportSession::Http3WebTransportSession(nsAHttpTransaction* trans,
                                                   Http3Session* aHttp3Session)
    : Http3TunnelStreamBase(trans, aHttp3Session) {
  LOG(("Http3WebTransportSession ctor %p", this));
}

Http3WebTransportSession::~Http3WebTransportSession() = default;

uint64_t Http3WebTransportSession::GetStreamId() const {
  return Http3StreamBase::StreamId();
}

void Http3WebTransportSession::Close(nsresult aResult) {
  LOG(("Http3WebTransportSession::Close %p", this));
  if (RefPtr<WebTransportSessionEventListener> listener = TakeListener()) {
    mozilla::dom::WebTransportStatsData emptyStats;
    nsCOMPtr<nsIWebTransportSessionStats> statsWrapper =
        new WebTransportSessionStatsWrapper(emptyStats);
    listener->OnSessionClosed(NS_SUCCEEDED(aResult), 0, ""_ns, statsWrapper);
  }
  if (mTransaction) {
    mTransaction->Close(aResult);
    mTransaction = nullptr;
  }
  mRecvState = RECV_DONE;
  mSendState = SEND_DONE;

  if (mSession) {
    mSession->CloseWebTransportConn();
    mSession = nullptr;
  }
}

void Http3WebTransportSession::OnClosePending() {
  // Capture stats at the moment of close
  mozilla::dom::WebTransportStatsData stats;
  if (mSession->CloseWebTransport(mStreamId, mStatus, mReason, stats)) {
    mCachedStats = stats;
  }
}

void Http3WebTransportSession::OnSessionClosedWithStats(
    bool aCleanly, uint32_t aStatus, const nsACString& aReason,
    const mozilla::dom::WebTransportStatsData& aStats) {
  // Cache stats for server-initiated close
  mCachedStats = aStats;

  // Call the existing OnSessionClosed which uses cached stats
  OnSessionClosed(aCleanly, aStatus, aReason);
}

void Http3WebTransportSession::OnSessionClosed(bool aCleanly, uint32_t aStatus,
                                               const nsACString& aReason) {
  if (mTransaction) {
    mTransaction->Close(NS_BASE_STREAM_CLOSED);
    mTransaction = nullptr;
  }
  if (RefPtr<WebTransportSessionEventListener> listener = TakeListener()) {
    // Use cached stats captured at close time
    nsCOMPtr<nsIWebTransportSessionStats> statsWrapper =
        new WebTransportSessionStatsWrapper(mCachedStats);
    listener->OnSessionClosed(aCleanly, aStatus, aReason, statsWrapper);
  }
  mRecvState = RECV_DONE;
  mSendState = SEND_DONE;

  mSession->CloseWebTransportConn();
}

void Http3WebTransportSession::OnSessionDraining() {
  LOG(("Http3WebTransportSession::OnSessionDraining [this=%p]", this));
  RefPtr<WebTransportSessionEventListener> listener = GetListener();
  if (listener) {
    listener->OnDraining();
  }
}

void Http3WebTransportSession::CloseSession(uint32_t aStatus,
                                            const nsACString& aReason) {
  if ((mRecvState != CLOSE_PENDING) && (mRecvState != RECV_DONE)) {
    mStatus = aStatus;
    mReason = aReason;

    // Capture stats before clearing the listener (client-initiated close)
    if (mSession->CloseWebTransport(mStreamId, mStatus, mReason,
                                    mCachedStats)) {
      // Stats captured successfully
    }

    // Notify listener with cached stats before clearing it
    RefPtr<WebTransportSessionEventListener> listener = GetListener();
    if (listener) {
      nsCOMPtr<nsIWebTransportSessionStats> statsWrapper =
          new WebTransportSessionStatsWrapper(mCachedStats);
      listener->OnSessionClosed(true, mStatus, mReason, statsWrapper);
    }

    mSession->ConnectSlowConsumer(this);
    mRecvState = CLOSE_PENDING;
    mSendState = SEND_DONE;
  }
  RefPtr<WebTransportSessionEventListener> listener = TakeListener();
  // let it drop
}

bool Http3WebTransportSession::CloseSessionAndGetStats(
    uint32_t aStatus, const nsACString& aReason,
    mozilla::dom::WebTransportStatsData& aStats) {
  LOG(("Http3WebTransportSession::CloseSessionAndGetStats stream=%llu",
       (unsigned long long)mStreamId));
  if ((mRecvState != CLOSE_PENDING) && (mRecvState != RECV_DONE)) {
    // Match CloseSession()'s state-transition contract: mStatus/mReason must
    // be set before entering CLOSE_PENDING, since OnClosePending() (invoked
    // from WriteSegments() once ConnectSlowConsumer() schedules a revisit)
    // uses them for its own CloseWebTransport() call that drives mRecvState
    // to RECV_DONE. Without this, that call used stale (default) mStatus/
    // mReason, and without ConnectSlowConsumer() the stream was never
    // revisited at all, leaving it stuck at CLOSE_PENDING (Done() never
    // becomes true, so Http3Session::CloseStreamInternal() never removes it).
    mStatus = aStatus;
    mReason = aReason;

    // Get stats from neqo before closing
    if (mSession->CloseWebTransport(mStreamId, mStatus, mReason, aStats)) {
      LOG(("  Got stats: bytesSent=%llu, bytesReceived=%llu",
           (unsigned long long)aStats.bytesSent(),
           (unsigned long long)aStats.bytesReceived()));
      mCachedStats = aStats;
      RefPtr<WebTransportSessionEventListener> listener =
          TakeListener();  // drop it
      mSession->ConnectSlowConsumer(this);
      mRecvState = CLOSE_PENDING;
      mSendState = SEND_DONE;
      return true;
    }
    LOG(("  CloseWebTransport failed"));
  } else {
    LOG(("  Wrong state: mRecvState=%d", mRecvState));
  }
  return false;
}

void Http3WebTransportSession::CreateOutgoingBidirectionalStream(
    std::function<void(Result<RefPtr<WebTransportStreamBase>, nsresult>&&)>&&
        aCallback) {
  return CreateStreamInternal(true, std::move(aCallback));
}

void Http3WebTransportSession::CreateOutgoingUnidirectionalStream(
    std::function<void(Result<RefPtr<WebTransportStreamBase>, nsresult>&&)>&&
        aCallback) {
  return CreateStreamInternal(false, std::move(aCallback));
}

void Http3WebTransportSession::CreateStreamInternal(
    bool aBidi,
    std::function<void(Result<RefPtr<WebTransportStreamBase>, nsresult>&&)>&&
        aCallback) {
  LOG(("Http3WebTransportSession::CreateStreamInternal this=%p aBidi=%d", this,
       aBidi));
  if (mRecvState != ACTIVE) {
    aCallback(Err(NS_ERROR_NOT_AVAILABLE));
    return;
  }

  RefPtr<Http3WebTransportStream> stream =
      aBidi ? new Http3WebTransportStream(mSession, mStreamId,
                                          WebTransportStreamType::BiDi,
                                          std::move(aCallback))
            : new Http3WebTransportStream(mSession, mStreamId,
                                          WebTransportStreamType::UniDi,
                                          std::move(aCallback));
  mSession->StreamHasDataToWrite(stream);
  // Put the newly created stream in to |mStreams| to keep it alive.
  mStreams.AppendElement(std::move(stream));
}

// This is called by Http3Session::TryActivatingWebTransportStream. When called,
// this means a WebTransport stream is successfully activated and the stream
// will be managed by Http3Session.
void Http3WebTransportSession::RemoveWebTransportStream(
    Http3WebTransportStream* aStream) {
  LOG(
      ("Http3WebTransportSession::RemoveWebTransportStream "
       "this=%p aStream=%p",
       this, aStream));
  DebugOnly<bool> existed = mStreams.RemoveElement(aStream);
  MOZ_ASSERT(existed);
}

already_AddRefed<Http3WebTransportStream>
Http3WebTransportSession::OnIncomingWebTransportStream(
    WebTransportStreamType aType, uint64_t aId) {
  LOG(
      ("Http3WebTransportSession::OnIncomingWebTransportStream "
       "this=%p",
       this));

  if (mRecvState != ACTIVE) {
    return nullptr;
  }

  MOZ_ASSERT(!mTransaction);
  RefPtr<Http3WebTransportStream> stream =
      new Http3WebTransportStream(mSession, mStreamId, aType, aId);
  if (NS_FAILED(stream->InitInputPipe())) {
    return nullptr;
  }

  if (aType == WebTransportStreamType::BiDi) {
    if (NS_FAILED(stream->InitOutputPipe())) {
      return nullptr;
    }
  }

  RefPtr<WebTransportSessionEventListener> baseListener = GetListener();
  if (!baseListener) {
    return nullptr;
  }

  if (nsCOMPtr<WebTransportSessionEventListenerInternal> listener =
          do_QueryInterface(baseListener)) {
    listener->OnIncomingStreamAvailableInternal(stream);
  }
  return stream.forget();
}

void Http3WebTransportSession::SendDatagram(nsTArray<uint8_t>&& aData,
                                            uint64_t aTrackingId,
                                            uint64_t aSendGroupId,
                                            int64_t aSendOrder,
                                            TimeStamp aTimestamp) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG(("Http3WebTransportSession::SendDatagram this=%p, sendGroup=%" PRIu64
       ", sendOrder=%" PRId64,
       this, aSendGroupId, aSendOrder));
  // Any other state means the session has either not activated yet or is
  // tearing down, and discarding the datagram without reporting an outcome is
  // safe in both cases. Content cannot reach the first: it only sends once
  // WebTransport has resolved [[Ready]] and handed the datagram algorithms its
  // actor, which is later than the activation that gets us here. For the
  // second, every transition to SEND_DONE reports the close to the listener,
  // or drops it because content closed the session itself; either way content
  // runs WebTransportDatagramDuplexStream::Cleanup(), which settles the
  // write() promise this datagram belongs to.
  if (mSendState != PROCESSING_DATAGRAM) {
    return;
  }

  // SendDatagram() enqueues the datagram into neqo's per-session queue but
  // does not send it immediately. The datagram is moved to the QUIC send
  // queue during process_output() (called from ProcessOutput() in
  // Http3Session::SendData()).
  //
  // StreamHasDataToWrite() triggers the chain:
  //   StreamReadyToWrite() -> ForceSend() -> HttpConnectionUDP::ForceSend()
  //     -> MaybeForceSendIO() -> [async dispatch] -> SendData()
  //     -> ProcessOutput() -> neqo process_output()
  //
  // This means the datagram is actually transmitted after one event loop
  // cycle (the async dispatch in ForceSend/MaybeForceSendIO).
  mSession->SendDatagram(this, aData, aTrackingId, aSendGroupId, aSendOrder,
                         aTimestamp);
  mSession->StreamHasDataToWrite(this);
}

void Http3WebTransportSession::OnDatagramReceived(nsTArray<uint8_t>&& aData) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG(("Http3WebTransportSession::OnDatagramReceived this=%p", this));
  RefPtr<WebTransportSessionEventListener> baseListener2 = GetListener();
  if (mRecvState != ACTIVE || !baseListener2) {
    return;
  }

  if (nsCOMPtr<WebTransportSessionEventListenerInternal> listener =
          do_QueryInterface(baseListener2)) {
    listener->OnDatagramReceivedInternal(std::move(aData));
  }
}

void Http3WebTransportSession::GetMaxDatagramSize() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  RefPtr<WebTransportSessionEventListener> listener = GetListener();
  if (mRecvState != ACTIVE || !listener) {
    return;
  }

  uint64_t size = mSession->MaxDatagramSize(mStreamId);
  listener->OnMaxDatagramSize(size);
}

nsresult Http3WebTransportSession::ExportKeyingMaterial(
    const nsTArray<uint8_t>& aLabel, const nsTArray<uint8_t>& aContext,
    uint32_t aOutputLength, nsTArray<uint8_t>& aKeyingMaterial) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  if (mRecvState != ACTIVE) {
    return NS_ERROR_NOT_CONNECTED;
  }
  return mSession->ExportWebTransportKeyingMaterial(
      mStreamId, aLabel, aContext, aOutputLength, aKeyingMaterial);
}

void Http3WebTransportSession::SetDatagramMaxBufferedDatagrams(
    uint32_t aMaxBufferedDatagrams) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG(
      ("Http3WebTransportSession::SetDatagramMaxBufferedDatagrams this=%p "
       "max=%" PRIu32,
       this, aMaxBufferedDatagrams));
  if (mRecvState != ACTIVE) {
    return;
  }

  mSession->SetDatagramMaxBufferedDatagrams(mStreamId, aMaxBufferedDatagrams);
}

void Http3WebTransportSession::SetDatagramMaxAge(double aMaxAge) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG(("Http3WebTransportSession::SetDatagramMaxAge this=%p age=%f", this,
       aMaxAge));
  if (mRecvState != ACTIVE) {
    return;
  }

  mSession->SetDatagramMaxAge(mStreamId, aMaxAge);
}

void Http3WebTransportSession::GetStats() {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  RefPtr<WebTransportSessionEventListener> listener = GetListener();
  if (!listener) {
    // No one to report to either way.
    return;
  }

  mozilla::dom::WebTransportStatsData stats;
  if (mRecvState != ACTIVE ||
      !mSession->GetWebTransportSessionStats(mStreamId, stats)) {
    // Report the failure explicitly, so a pending getStats() request doesn't
    // hang forever waiting for a callback that will never come.
    listener->OnStatsAvailable(nullptr);
    return;
  }

  nsCOMPtr<nsIWebTransportSessionStats> statsWrapper =
      new WebTransportSessionStatsWrapper(stats);
  listener->OnStatsAvailable(statsWrapper);
}

void Http3WebTransportSession::GetNegotiatedProtocol(nsACString& aProtocol) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  aProtocol.Truncate();
  if (mRecvState != ACTIVE) {
    return;
  }
  mSession->GetWebTransportSessionProtocol(mStreamId, aProtocol);
}

void ParseFlatWebTransportResponseHeaders(const nsTArray<uint8_t>& aFlatHeaders,
                                          nsTArray<nsCString>& aNames,
                                          nsTArray<nsCString>& aValues) {
  // Format: "HTTP/3 200\r\nname: value\r\n\r\n"
  const char* data = reinterpret_cast<const char*>(aFlatHeaders.Elements());
  uint32_t len = aFlatHeaders.Length();
  uint32_t pos = 0;

  // Skip status line
  while (pos + 1 < len && !(data[pos] == '\r' && data[pos + 1] == '\n')) {
    ++pos;
  }
  pos += 2;

  // Parse header lines
  while (pos < len) {
    uint32_t lineStart = pos;
    while (pos + 1 < len && !(data[pos] == '\r' && data[pos + 1] == '\n')) {
      ++pos;
    }
    uint32_t lineLen = pos - lineStart;
    pos += 2;

    if (lineLen == 0) break;

    const char* line = data + lineStart;
    uint32_t sep = 0;
    while (sep + 1 < lineLen && !(line[sep] == ':' && line[sep + 1] == ' ')) {
      ++sep;
    }
    if (sep + 1 >= lineLen) continue;

    nsCString name(line, sep);
    // https://w3c.github.io/webtransport/#process-a-webtransport-fetch-response
    // Step 6: Delete "wt-protocol" from responseHeaders before exposing them
    // to script; it is a negotiation-only header (see .protocol instead).
    if (name.LowerCaseEqualsLiteral("wt-protocol")) {
      continue;
    }

    aNames.AppendElement(name);
    aValues.AppendElement(nsCString(line + sep + 2, lineLen - sep - 2));
  }
}

void Http3WebTransportSession::GetResponseHeaders(
    nsTArray<nsCString>& aNames, nsTArray<nsCString>& aValues) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  ParseFlatWebTransportResponseHeaders(mSavedFlatResponseHeaders, aNames,
                                       aValues);
}
nsresult Http3WebTransportSession::RegisterSendGroup(uint64_t aGroupId) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  if (mRecvState != ACTIVE) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  return mSession->RegisterWebTransportSendGroup(mStreamId, aGroupId);
}

void Http3WebTransportSession::OnOutgoingDatagramOutCome(
    uint64_t aId, WebTransportSessionEventListener::DatagramOutcome aOutCome) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  LOG(("Http3WebTransportSession::OnOutgoingDatagramOutCome this=%p id=%" PRIx64
       ", outCome=%d mRecvState=%d",
       this, aId, static_cast<uint32_t>(aOutCome), mRecvState));
  RefPtr<WebTransportSessionEventListener> listener = GetListener();
  if (mRecvState != ACTIVE || !listener || !aId) {
    return;
  }

  listener->OnOutgoingDatagramOutCome(aId, aOutCome);
}

void Http3WebTransportSession::
    SetAnticipatedConcurrentIncomingUnidirectionalStreams(uint16_t aValue) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  mSession->SetWebTransportAnticipatedConcurrentIncomingUnidirectionalStreams(
      mStreamId, aValue);
}

void Http3WebTransportSession::
    SetAnticipatedConcurrentIncomingBidirectionalStreams(uint16_t aValue) {
  MOZ_ASSERT(OnSocketThread(), "not on socket thread");
  mSession->SetWebTransportAnticipatedConcurrentIncomingBidirectionalStreams(
      mStreamId, aValue);
}

void Http3WebTransportSession::OnStreamStopSending(uint64_t aId,
                                                   uint64_t aErrorCode) {
  LOG(("OnStreamStopSending id:%" PRId64 " errorCode:%" PRIu64, aId,
       aErrorCode));
  RefPtr<WebTransportSessionEventListener> listener = GetListener();
  if (!listener) {
    return;
  }

  listener->OnStopSending(aId, aErrorCode);
}

void Http3WebTransportSession::OnStreamReset(uint64_t aId,
                                             uint64_t aErrorCode) {
  LOG(("OnStreamReset id:%" PRId64 " errorCode:%" PRIu64, aId, aErrorCode));
  RefPtr<WebTransportSessionEventListener> listener = GetListener();
  if (!listener) {
    return;
  }

  listener->OnResetReceived(aId, aErrorCode);
}

}  // namespace mozilla::net
