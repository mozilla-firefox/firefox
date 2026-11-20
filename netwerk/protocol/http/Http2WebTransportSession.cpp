/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// HttpLog.h should generally be included first
#include "Http2WebTransportSession.h"

#include <limits>

#include "Capsule.h"
#include "CapsuleEncoder.h"
#include "Http2Session.h"
#include "Http2WebTransportStream.h"
#include "HttpLog.h"
#include "mozilla/net/NeqoHttp3Conn.h"
#include "nsHttp.h"
#include "nsIOService.h"
#include "nsIWebTransport.h"

namespace mozilla::net {

Http2WebTransportSessionImpl::CapsuleQueue::CapsuleQueue() = default;

mozilla::Queue<UniquePtr<CapsuleEncoder>>&
Http2WebTransportSessionImpl::CapsuleQueue::operator[](
    CapsuleTransmissionPriority aPriority) {
  if (aPriority == CapsuleTransmissionPriority::Critical) {
    return mCritical;
  }
  if (aPriority == CapsuleTransmissionPriority::Important) {
    return mImportant;
  }
  if (aPriority == CapsuleTransmissionPriority::High) {
    return mHigh;
  }
  if (aPriority == CapsuleTransmissionPriority::Normal) {
    return mNormal;
  }

  return mLow;
}

Http2WebTransportSessionImpl::Http2WebTransportSessionImpl(
    CapsuleIOHandler* aHandler, Http2WebTransportInitialSettings aSettings)
    : mSettings(aSettings),
      mRemoteStreamsFlowControl(aSettings.mInitialLocalMaxStreamsBidi,
                                aSettings.mInitialLocalMaxStreamsUnidi),
      mHandler(aHandler),
      mSessionDataFc(aSettings.mInitialMaxData),
      mReceiverFc(aSettings.mInitialLocalMaxData) {
  LOG(("Http2WebTransportSessionImpl ctor:%p", this));
  mLocalStreamsFlowControl[WebTransportStreamType::UniDi].Update(
      mSettings.mInitialMaxStreamsUni);
  mLocalStreamsFlowControl[WebTransportStreamType::BiDi].Update(
      mSettings.mInitialMaxStreamsBidi);
}

Http2WebTransportSessionImpl::~Http2WebTransportSessionImpl() {
  LOG(("Http2WebTransportSessionImpl dtor:%p", this));
}

void Http2WebTransportSessionImpl::CloseSession(uint32_t aStatus,
                                                const nsACString& aReason) {
  LOG(("Http2WebTransportSessionImpl::CloseSession %p aStatus=%x", this,
       aStatus));

  mHandler->SetSentFin();

  Capsule capsule = Capsule::CloseWebTransportSession(aStatus, aReason);
  auto encoder = std::make_unique<CapsuleEncoder>();
  encoder->EncodeCapsule(capsule);
  EnqueueOutCapsule(CapsuleTransmissionPriority::Important, std::move(encoder));
}

uint64_t Http2WebTransportSessionImpl::GetStreamId() const { return mStreamId; }

void Http2WebTransportSessionImpl::GetMaxDatagramSize() {
  // HTTP/2 WebTransport (draft-ietf-webtrans-http2) has no equivalent limit
  // to HTTP/3's max_datagram_size; report back the implementation-defined
  // default (see WebTransportDatagramDuplexStream.h's mOutgoingMaxDataSize)
  // instead of leaving the caller's value at its default-constructed 0,
  // which would make every future writeDatagrams()/sendDatagrams() call
  // silently no-op forever (spec: "If OutgoingMaxDatagramSize is less than
  // data's length, return a promise resolved with undefined").
  if (RefPtr<WebTransportSessionEventListener> listener = GetListener()) {
    listener->OnMaxDatagramSize(1024);
  }
}

nsresult Http2WebTransportSessionImpl::ExportKeyingMaterial(
    const nsTArray<uint8_t>& aLabel, const nsTArray<uint8_t>& aContext,
    uint32_t aOutputLength, nsTArray<uint8_t>& aKeyingMaterial) {
  // TODO: Implement exportKeyingMaterial for HTTP/2 WebTransport
  // HTTP/2 WebTransport over TLS should support this via the underlying TLS
  // connection, but the implementation is not yet available.
  return NS_ERROR_NOT_IMPLEMENTED;
}

void Http2WebTransportSessionImpl::GetStats() {
  // HTTP/2 WebTransport has no equivalent to QUIC's per-connection
  // congestion-control stats. Resolve with an all-zero WebTransportStatsData
  // so getStats()'s Promise settles instead of staying pending forever.
  if (RefPtr<WebTransportSessionEventListener> listener = GetListener()) {
    mozilla::dom::WebTransportStatsData stats;
    nsCOMPtr<nsIWebTransportSessionStats> statsWrapper =
        new WebTransportSessionStatsWrapper(stats);
    listener->OnStatsAvailable(statsWrapper);
  }
}

nsresult Http2WebTransportSessionImpl::RegisterSendGroup(uint64_t aGroupId) {
  // HTTP/2 WebTransport doesn't support send group scheduling.
  return NS_OK;
}

void Http2WebTransportSessionImpl::GetNegotiatedProtocol(
    nsACString& aProtocol) {
  // TODO: Implement protocol negotiation for HTTP/2 WebTransport
  aProtocol.Truncate();
}

// HTTP/2 WebTransport raises stream limits with WT_MAX_STREAMS capsules
// rather than QUIC MAX_STREAMS frames; not wired up yet.
void Http2WebTransportSessionImpl::
    SetAnticipatedConcurrentIncomingUnidirectionalStreams(uint16_t aValue) {}

void Http2WebTransportSessionImpl::
    SetAnticipatedConcurrentIncomingBidirectionalStreams(uint16_t aValue) {}

void Http2WebTransportSessionImpl::SendDatagram(nsTArray<uint8_t>&& aData,
                                                uint64_t aTrackingId,
                                                uint64_t aSendGroupId,
                                                int64_t aSendOrder,
                                                TimeStamp aTimestamp) {
  LOG(("Http2WebTransportSessionImpl::SendDatagram %p, sendGroup=%" PRIu64
       ", sendOrder=%" PRId64,
       this, aSendGroupId, aSendOrder));
  // Note: Http2 WebTransport doesn't support sendGroup/sendOrder prioritization
  // yet. These parameters are accepted for API compatibility but not used.
  // Nor is aTimestamp: the datagram is sent here, so it cannot expire.

  Capsule capsule = Capsule::WebTransportDatagram(std::move(aData));

  auto encoder = std::make_unique<CapsuleEncoder>();
  encoder->EncodeCapsule(capsule);
  EnqueueOutCapsule(CapsuleTransmissionPriority::Normal, std::move(encoder));
}

void Http2WebTransportSessionImpl::SetDatagramMaxBufferedDatagrams(
    uint32_t aMaxBufferedDatagrams) {
  LOG(
      ("Http2WebTransportSessionImpl::SetDatagramMaxBufferedDatagrams %p "
       "max=%" PRIu32,
       this, aMaxBufferedDatagrams));
}

void Http2WebTransportSessionImpl::SetDatagramMaxAge(double aMaxAge) {
  LOG(("Http2WebTransportSessionImpl::SetDatagramMaxAge %p age=%f", this,
       aMaxAge));
}

void Http2WebTransportSessionImpl::CreateOutgoingStreamInternal(
    StreamId aStreamId,
    std::function<void(Result<RefPtr<WebTransportStreamBase>, nsresult>&&)>&&
        aCallback) {
  LOG(
      ("Http2WebTransportSessionImpl::CreateOutgoingStreamInternal %p "
       "id:%" PRIx64,
       this, (uint64_t)aStreamId));

  RefPtr<Http2WebTransportStream> stream = new Http2WebTransportStream(
      this, aStreamId,
      aStreamId.IsBiDi() ? mSettings.mInitialMaxStreamDataBidi
                         : mSettings.mInitialMaxStreamDataUni,
      aStreamId.IsBiDi() ? mSettings.mInitialLocalMaxStreamDataBidi
                         : mSettings.mInitialLocalMaxStreamDataUnidi,
      std::move(aCallback));
  if (NS_FAILED(stream->Init())) {
    return;
  }
  mOutgoingStreams.InsertOrUpdate(aStreamId, std::move(stream));
}

void Http2WebTransportSessionImpl::ProcessPendingStreamCallbacks(
    mozilla::Queue<UniquePtr<PendingStreamCallback>>& aCallbacks,
    WebTransportStreamType aStreamType) {
  size_t size = aCallbacks.Count();
  for (size_t count = 0; count < size; ++count) {
    auto id = mLocalStreamsFlowControl.TakeStreamId(aStreamType);
    if (!id) {
      break;
    }

    UniquePtr<PendingStreamCallback> callback = aCallbacks.Pop();
    auto cb = callback->TakeCallback();
    CreateOutgoingStreamInternal(*id, std::move(cb));
  }
}

void Http2WebTransportSessionImpl::CreateOutgoingBidirectionalStream(
    std::function<void(Result<RefPtr<WebTransportStreamBase>, nsresult>&&)>&&
        aCallback) {
  auto id = mLocalStreamsFlowControl.TakeStreamId(WebTransportStreamType::BiDi);
  if (!id) {
    mBidiPendingStreamCallbacks.Push(
        std::make_unique<PendingStreamCallback>(std::move(aCallback)));
    return;
  }

  CreateOutgoingStreamInternal(*id, std::move(aCallback));
}

void Http2WebTransportSessionImpl::CreateOutgoingUnidirectionalStream(
    std::function<void(Result<RefPtr<WebTransportStreamBase>, nsresult>&&)>&&
        aCallback) {
  auto id =
      mLocalStreamsFlowControl.TakeStreamId(WebTransportStreamType::UniDi);
  if (!id) {
    mUnidiPendingStreamCallbacks.Push(
        std::make_unique<PendingStreamCallback>(std::move(aCallback)));
    return;
  }

  CreateOutgoingStreamInternal(*id, std::move(aCallback));
}

void Http2WebTransportSessionImpl::StartReading() {
  LOG(("Http2WebTransportSessionImpl::StartReading %p", this));
  mHandler->StartReading();
}

void Http2WebTransportSessionImpl::EnqueueOutCapsule(
    CapsuleTransmissionPriority aPriority, UniquePtr<CapsuleEncoder>&& aData) {
  mCapsuleQueue[aPriority].Push(std::move(aData));
  mHandler->HasCapsuleToSend();
}

void Http2WebTransportSessionImpl::SendMaintenanceCapsules(
    CapsuleTransmissionPriority aPriority) {
  auto encoder = mSessionDataFc.CreateSessionDataBlockedCapsule();
  if (encoder) {
    mCapsuleQueue[aPriority].Push(
        std::make_unique<CapsuleEncoder>(encoder.ref()));
  }
  encoder = mReceiverFc.CreateMaxDataCapsule();
  if (encoder) {
    mCapsuleQueue[aPriority].Push(
        std::make_unique<CapsuleEncoder>(encoder.ref()));
  }
  encoder = mLocalStreamsFlowControl[WebTransportStreamType::BiDi]
                .CreateStreamsBlockedCapsule();
  if (encoder) {
    mCapsuleQueue[aPriority].Push(
        std::make_unique<CapsuleEncoder>(encoder.ref()));
  }
  encoder = mLocalStreamsFlowControl[WebTransportStreamType::UniDi]
                .CreateStreamsBlockedCapsule();
  if (encoder) {
    mCapsuleQueue[aPriority].Push(
        std::make_unique<CapsuleEncoder>(encoder.ref()));
  }
  encoder = mRemoteStreamsFlowControl[WebTransportStreamType::BiDi]
                .FlowControl()
                .CreateMaxStreamsCapsule();
  if (encoder) {
    mCapsuleQueue[aPriority].Push(
        std::make_unique<CapsuleEncoder>(encoder.ref()));
  }
  encoder = mRemoteStreamsFlowControl[WebTransportStreamType::UniDi]
                .FlowControl()
                .CreateMaxStreamsCapsule();
  if (encoder) {
    mCapsuleQueue[aPriority].Push(
        std::make_unique<CapsuleEncoder>(encoder.ref()));
  }
  for (const auto& stream : mOutgoingStreams.Values()) {
    stream->WriteMaintenanceCapsules(mCapsuleQueue[aPriority]);
  }
  for (const auto& stream : mIncomingStreams.Values()) {
    stream->WriteMaintenanceCapsules(mCapsuleQueue[aPriority]);
  }
}

void Http2WebTransportSessionImpl::StreamHasCapsuleToSend() {
  mHandler->HasCapsuleToSend();
}

void Http2WebTransportSessionImpl::PrepareCapsulesToSend(
    mozilla::Queue<UniquePtr<CapsuleEncoder>>& aOutput) {
  // Like neqo, flow control capsules are at level
  // CapsuleTransmissionPriority::Important.
  SendMaintenanceCapsules(CapsuleTransmissionPriority::Important);

  for (const auto& stream : mOutgoingStreams.Values()) {
    stream->TakeOutputCapsule(
        mCapsuleQueue[CapsuleTransmissionPriority::Normal]);
  }
  for (const auto& stream : mIncomingStreams.Values()) {
    stream->TakeOutputCapsule(
        mCapsuleQueue[CapsuleTransmissionPriority::Normal]);
  }

  static constexpr CapsuleTransmissionPriority priorities[] = {
      CapsuleTransmissionPriority::Critical,
      CapsuleTransmissionPriority::Important,
      CapsuleTransmissionPriority::High,
      CapsuleTransmissionPriority::Normal,
      CapsuleTransmissionPriority::Low,
  };

  for (CapsuleTransmissionPriority priority : priorities) {
    auto& queue = mCapsuleQueue[priority];
    while (!queue.IsEmpty()) {
      UniquePtr<CapsuleEncoder> entry = queue.Pop();
      aOutput.Push(std::move(entry));
    }
  }
}

void Http2WebTransportSessionImpl::Close(nsresult aReason) {
  for (const auto& stream : mOutgoingStreams.Values()) {
    stream->Close(aReason);
  }
  for (const auto& stream : mIncomingStreams.Values()) {
    stream->Close(aReason);
  }
  mOutgoingStreams.Clear();
  mIncomingStreams.Clear();
}

void Http2WebTransportSessionImpl::OnStreamClosed(
    Http2WebTransportStream* aStream) {
  LOG(("Http2WebTransportSessionImpl::OnStreamClosed %p stream:%p", this,
       aStream));
  RefPtr<Http2WebTransportStream> stream = aStream;
  StreamId id = stream->WebTransportStreamId();
  if (id.IsClientInitiated()) {
    mOutgoingStreams.Remove(id);
  } else {
    mIncomingStreams.Remove(id);
    mRemoteStreamsFlowControl[id.StreamType()].FlowControl().AddRetired(1);
  }
}

bool Http2WebTransportSessionImpl::OnCapsule(Capsule&& aCapsule) {
  switch (aCapsule.Type()) {
    case CapsuleType::CLOSE_WEBTRANSPORT_SESSION:
      LOG(("Handling CLOSE_WEBTRANSPORT_SESSION\n"));
      break;
    case CapsuleType::DRAIN_WEBTRANSPORT_SESSION:
      LOG(("Handling DRAIN_WEBTRANSPORT_SESSION\n"));
      break;
    case CapsuleType::PADDING:
      LOG(("Handling PADDING\n"));
      break;
    case CapsuleType::WT_RESET_STREAM: {
      WebTransportResetStreamCapsule& reset =
          aCapsule.GetWebTransportResetStreamCapsule();
      StreamId id = StreamId(reset.mID);
      if (!HandleStreamResetCapsule(id, std::move(aCapsule))) {
        return false;
      }
    } break;
    case CapsuleType::WT_STOP_SENDING: {
      WebTransportStopSendingCapsule& stopSending =
          aCapsule.GetWebTransportStopSendingCapsule();
      StreamId id = StreamId(stopSending.mID);
      if (!HandleStreamStopSendingCapsule(id, std::move(aCapsule))) {
        return false;
      }
    } break;
    case CapsuleType::WT_STREAM: {
      WebTransportStreamDataCapsule& streamData =
          aCapsule.GetWebTransportStreamDataCapsule();
      StreamId id = StreamId(streamData.mID);
      if (id.IsServerInitiated()) {
        return ProcessIncomingStreamCapsule(std::move(aCapsule), id,
                                            id.StreamType());
      } else {
        RefPtr<Http2WebTransportStream> stream = mOutgoingStreams.Get(id);
        if (!stream) {
          LOG(
              ("Http2WebTransportSessionImpl::OnCapsule - "
               "stream not found "
               "stream_id=0x%" PRIx64 " [this=%p].",
               static_cast<uint64_t>(id), this));
          return false;
        }
        if (NS_FAILED(stream->OnCapsule(std::move(aCapsule)))) {
          return false;
        }
      }
      break;
    }
    case CapsuleType::WT_STREAM_FIN:
      LOG(("Handling WT_STREAM_FIN\n"));
      break;
    case CapsuleType::WT_MAX_DATA: {
      LOG(("Handling WT_MAX_DATA\n"));
      WebTransportMaxDataCapsule& maxData =
          aCapsule.GetWebTransportMaxDataCapsule();
      mSessionDataFc.Update(maxData.mMaxDataSize);
    } break;
    case CapsuleType::WT_MAX_STREAM_DATA: {
      WebTransportMaxStreamDataCapsule& maxStreamData =
          aCapsule.GetWebTransportMaxStreamDataCapsule();
      StreamId id = StreamId(maxStreamData.mID);
      if (!HandleMaxStreamDataCapsule(id, std::move(aCapsule))) {
        return false;
      }
    } break;
    case CapsuleType::WT_MAX_STREAMS_BIDI: {
      LOG(("Handling WT_MAX_STREAMS_BIDI\n"));
      WebTransportMaxStreamsCapsule& maxStreams =
          aCapsule.GetWebTransportMaxStreamsCapsule();
      mLocalStreamsFlowControl[WebTransportStreamType::BiDi].Update(
          maxStreams.mLimit);
      ProcessPendingStreamCallbacks(mBidiPendingStreamCallbacks,
                                    WebTransportStreamType::BiDi);
      break;
    }
    case CapsuleType::WT_MAX_STREAMS_UNIDI: {
      LOG(("Handling WT_MAX_STREAMS_UNIDI\n"));
      WebTransportMaxStreamsCapsule& maxStreams =
          aCapsule.GetWebTransportMaxStreamsCapsule();
      mLocalStreamsFlowControl[WebTransportStreamType::UniDi].Update(
          maxStreams.mLimit);
      ProcessPendingStreamCallbacks(mUnidiPendingStreamCallbacks,
                                    WebTransportStreamType::UniDi);
      break;
    }
    case CapsuleType::WT_DATA_BLOCKED:
      LOG(("Handling WT_DATA_BLOCKED\n"));
      break;
    case CapsuleType::WT_STREAM_DATA_BLOCKED:
      LOG(("Handling WT_STREAM_DATA_BLOCKED\n"));
      break;
    case CapsuleType::WT_STREAMS_BLOCKED_BIDI:
      LOG(("Handling WT_STREAMS_BLOCKED_BIDI\n"));
      break;
    case CapsuleType::WT_STREAMS_BLOCKED_UNIDI:
      LOG(("Handling WT_STREAMS_BLOCKED_UNIDI\n"));
      break;
    case CapsuleType::DATAGRAM: {
      LOG(("Handling DATAGRAM\n"));
      WebTransportDatagramCapsule& datagram =
          aCapsule.GetWebTransportDatagramCapsule();
      if (RefPtr<WebTransportSessionEventListener> baseListener =
              GetListener()) {
        if (nsCOMPtr<WebTransportSessionEventListenerInternal> listener =
                do_QueryInterface(baseListener)) {
          listener->OnDatagramReceivedInternal(std::move(datagram.mPayload));
        }
      }
      break;
    }
    default:
      LOG(("Unhandled capsule type\n"));
      break;
  }
  return true;
}

already_AddRefed<Http2WebTransportStream>
Http2WebTransportSessionImpl::GetStream(StreamId aId) {
  RefPtr<Http2WebTransportStream> stream;
  if (aId.IsClientInitiated()) {
    stream = mOutgoingStreams.Get(aId);
  } else {
    stream = mIncomingStreams.Get(aId);
  }

  if (!stream) {
    LOG(
        ("Http2WebTransportSessionImpl::GetStream - "
         "stream not found "
         "stream_id=0x%" PRIx64 " [this=%p].",
         static_cast<uint64_t>(aId), this));
    return nullptr;
  }

  return stream.forget();
}

bool Http2WebTransportSessionImpl::HandleMaxStreamDataCapsule(
    StreamId aId, Capsule&& aCapsule) {
  RefPtr<Http2WebTransportStream> stream = GetStream(aId);
  if (!stream) {
    return false;
  }

  if (NS_FAILED(stream->OnCapsule(std::move(aCapsule)))) {
    return false;
  }

  return true;
}

bool Http2WebTransportSessionImpl::HandleStreamStopSendingCapsule(
    StreamId aId, Capsule&& aCapsule) {
  WebTransportStopSendingCapsule& stopSending =
      aCapsule.GetWebTransportStopSendingCapsule();

  // https://www.ietf.org/archive/id/draft-ietf-webtrans-http2-15.html#name-wt_stop_sending-capsule
  // An error code above 0xffffffff is a session error of type WT_ERROR.
  if (stopSending.mErrorCode > std::numeric_limits<uint32_t>::max()) {
    return false;
  }

  RefPtr<Http2WebTransportStream> stream = GetStream(aId);
  if (!stream) {
    return false;
  }

  stream->OnStopSending();

  LOG(
      ("Http2WebTransportSessionImpl::HandleStreamStopSendingCapsule %p "
       "aID=%" PRIu64 " error=%" PRIu64,
       this, (uint64_t)aId, stopSending.mErrorCode));

  if (RefPtr<WebTransportSessionEventListener> listener = GetListener()) {
    listener->OnStopSending(aId, stopSending.mErrorCode);
  }
  return true;
}

bool Http2WebTransportSessionImpl::HandleStreamResetCapsule(
    StreamId aId, Capsule&& aCapsule) {
  WebTransportResetStreamCapsule& reset =
      aCapsule.GetWebTransportResetStreamCapsule();

  // https://www.ietf.org/archive/id/draft-ietf-webtrans-http2-15.html#name-wt_reset_stream-capsule
  // An error code above 0xffffffff is a session error of type WT_ERROR.
  if (reset.mErrorCode > std::numeric_limits<uint32_t>::max()) {
    return false;
  }

  RefPtr<Http2WebTransportStream> stream = GetStream(aId);
  if (!stream) {
    return false;
  }

  if (NS_FAILED(stream->OnReset(reset.mReliableSize))) {
    return false;
  }

  if (RefPtr<WebTransportSessionEventListener> listener = GetListener()) {
    listener->OnResetReceived(aId, reset.mErrorCode);
  }

  return true;
}

void Http2WebTransportSessionImpl::OnStreamDataSent(StreamId aId,
                                                    size_t aCount) {
  RefPtr<Http2WebTransportStream> stream = GetStream(aId);
  if (!stream) {
    return;
  }

  stream->OnStreamDataSent(aCount);
}

void Http2WebTransportSessionImpl::OnError(uint64_t aError) {
  LOG(("Http2WebTransportSessionImpl::OnError %p aError=%" PRIu64, this,
       aError));
  // XXX This should also tear down the HTTP/2 tunnel via mHandler.
  Close(NS_ERROR_NET_RESET);
}

bool Http2WebTransportSessionImpl::ProcessIncomingStreamCapsule(
    Capsule&& aCapsule, StreamId aID, WebTransportStreamType aStreamType) {
  LOG(
      ("Http2WebTransportSessionImpl::ProcessIncomingStreamCapsule %p "
       "aID=%" PRIu64 " type:%s",
       this, (uint64_t)aID,
       aStreamType == WebTransportStreamType::BiDi ? "BiDi" : "UniDi"));
  RefPtr<Http2WebTransportStream> stream = mIncomingStreams.Get(aID);
  if (stream) {
    return NS_SUCCEEDED(stream->OnCapsule(std::move(aCapsule)));
  }

  while (true) {
    auto res = mRemoteStreamsFlowControl[aStreamType].IsNewStream(aID);
    if (res.isErr() || !res.unwrap()) {
      break;
    }

    StreamId newStreamID =
        mRemoteStreamsFlowControl[aStreamType].TakeStreamId();
    stream = new Http2WebTransportStream(
        this,
        aStreamType == WebTransportStreamType::BiDi
            ? mSettings.mInitialMaxStreamDataBidi
            : 0,
        aStreamType == WebTransportStreamType::BiDi
            ? mSettings.mInitialLocalMaxStreamDataBidi
            : mSettings.mInitialLocalMaxStreamDataUnidi,
        newStreamID);
    if (NS_FAILED(stream->Init())) {
      return false;
    }
    mIncomingStreams.InsertOrUpdate(newStreamID, stream);
    if (RefPtr<WebTransportSessionEventListener> baseListener = GetListener()) {
      if (nsCOMPtr<WebTransportSessionEventListenerInternal> listener =
              do_QueryInterface(baseListener)) {
        listener->OnIncomingStreamAvailableInternal(stream);
      }
    }
  }

  stream = mIncomingStreams.Get(aID);
  if (stream) {
    return NS_SUCCEEDED(stream->OnCapsule(std::move(aCapsule)));
  }

  return true;
}

void Http2WebTransportSessionImpl::OnCapsuleParseFailure(nsresult aError) {
  mHandler->OnCapsuleParseFailure(aError);
}

NS_IMPL_ISUPPORTS_INHERITED(Http2WebTransportSession, Http2StreamTunnel,
                            nsIOutputStreamCallback, nsIInputStreamCallback)

Http2WebTransportSession::Http2WebTransportSession(
    Http2Session* aSession, int32_t aPriority, uint64_t aBcId,
    nsHttpConnectionInfo* aConnectionInfo,
    Http2WebTransportInitialSettings aSettings)
    : Http2StreamTunnel(aSession, aPriority, aBcId, aConnectionInfo),
      mImpl(MakeRefPtr<Http2WebTransportSessionImpl>(this, aSettings)),
      mCapsuleParser(std::make_unique<CapsuleParser>(mImpl)) {
  LOG(("Http2WebTransportSession ctor:%p", this));
}

Http2WebTransportSession::~Http2WebTransportSession() {
  LOG(("Http2WebTransportSession dtor:%p", this));
}

void Http2WebTransportSession::CloseStream(nsresult aReason) {
  LOG(("Http2WebTransportSession::CloseStream this=%p aReason=%x", this,
       static_cast<uint32_t>(aReason)));
  if (mTransaction) {
    mTransaction->Close(aReason);
    mTransaction = nullptr;
  }

  mInput->AsyncWait(nullptr, 0, 0, nullptr);
  mOutput->AsyncWait(nullptr, 0, 0, nullptr);
  Http2StreamTunnel::CloseStream(aReason);
  mCapsuleParser = nullptr;
  if (mImpl) {
    mImpl->Close(aReason);
    mImpl = nullptr;
  }
}

nsresult Http2WebTransportSession::GenerateHeaders(nsCString& aCompressedData,
                                                   uint8_t& aFirstFrameFlags) {
  const nsHttpRequestHead* head = mTransaction->RequestHead();

  nsAutoCString authorityHeader;
  nsresult rv = head->GetHeader(nsHttp::Host, authorityHeader);
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<Http2Session> session = Session();
  LOG3(("Http2WebTransportSession %p Stream ID 0x%X [session=%p] for %s\n",
        this, mStreamID, session.get(), authorityHeader.get()));

  nsAutoCString path;
  head->Path(path);

  rv = session->Compressor()->EncodeHeaderBlock(
      mFlatHttpRequestHeaders, "CONNECT"_ns, path, authorityHeader, "https"_ns,
      "webtransport"_ns, false, aCompressedData, true);
  NS_ENSURE_SUCCESS(rv, rv);

  mRequestBodyLenRemaining = 0x0fffffffffffffffULL;

  if (mInput) {
    mInput->AsyncWait(this, 0, 0, nullptr);
  }
  return NS_OK;
}

NS_IMETHODIMP
Http2WebTransportSession::OnInputStreamReady(nsIAsyncInputStream* aIn) {
  char buffer[nsIOService::gDefaultSegmentSize];
  uint32_t remainingCapacity = sizeof(buffer);
  uint32_t read = 0;

  while (remainingCapacity > 0) {
    uint32_t count = 0;
    nsresult rv = mInput->Read(buffer + read, remainingCapacity, &count);
    if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
      break;
    }

    if (NS_FAILED(rv)) {
      LOG(("Http2WebTransportSession::OnInputStreamReady %p failed 0x%x\n",
           this, static_cast<uint32_t>(rv)));
      // TODO: close connection
      return rv;
    }

    // base stream closed
    if (count == 0) {
      LOG((
          "Http2WebTransportSession::OnInputStreamReady %p connection closed\n",
          this));
      // Close with NS_BASE_STREAM_CLOSED
      return NS_OK;
    }

    remainingCapacity -= count;
    read += count;
  }

  if (read > 0) {
    Http2Session::LogIO(nullptr, this, "Http2WebTransportSession", buffer,
                        read);

    mCapsuleParser->ProcessCapsuleData(reinterpret_cast<uint8_t*>(buffer),
                                       read);
  }

  mInput->AsyncWait(this, 0, 0, nullptr);
  return NS_OK;
}

NS_IMETHODIMP
Http2WebTransportSession::OnOutputStreamReady(nsIAsyncOutputStream* aOut) {
  if (!mCurrentOutCapsule) {
    mImpl->PrepareCapsulesToSend(mOutgoingQueue);
    if (mOutgoingQueue.IsEmpty()) {
      return NS_OK;
    }
    mCurrentOutCapsule = mOutgoingQueue.Pop();
  }

  while (mCurrentOutCapsule && mOutput) {
    auto buffer = mCurrentOutCapsule->GetBuffer();
    const char* writeBuffer =
        reinterpret_cast<const char*>(buffer.Elements()) + mWriteOffset;
    uint32_t toWrite = buffer.Length() - mWriteOffset;

    uint32_t wrote = 0;
    nsresult rv = mOutput->Write(writeBuffer, toWrite, &wrote);
    if (rv == NS_BASE_STREAM_WOULD_BLOCK) {
      mOutput->AsyncWait(this, 0, 0, nullptr);
      return NS_OK;
    }

    if (NS_FAILED(rv)) {
      LOG(("Http2WebTransportSession::OnOutputStreamReady %p failed %u\n", this,
           static_cast<uint32_t>(rv)));
      // TODO: close connection
      return NS_OK;
    }

    mWriteOffset += wrote;

    Maybe<StreamMetadata> metadata = mCurrentOutCapsule->GetStreamMetadata();
    // This is a WT_STREAM_DATA capsule, so we need to track how many bytes of
    // stream data are sent.
    if (metadata) {
      if (mWriteOffset > metadata->mStartOfData) {
        uint64_t dataSent = mWriteOffset - metadata->mStartOfData;
        mImpl->OnStreamDataSent(StreamId(metadata->mID), dataSent);
      }
    }

    if (toWrite == wrote) {
      mWriteOffset = 0;
      mCurrentOutCapsule =
          mOutgoingQueue.IsEmpty() ? nullptr : mOutgoingQueue.Pop();
    }
  }

  return NS_OK;
}

void Http2WebTransportSession::HasCapsuleToSend() {
  LOG(("Http2WebTransportSession::HasCapsuleToSend %p mSendClosed=%d", this,
       mSendClosed));
  if (mSendClosed) {
    return;
  }

  mImpl->PrepareCapsulesToSend(mOutgoingQueue);

  if (mOutput) {
    OnOutputStreamReady(mOutput);
  }
}

void Http2WebTransportSession::SetSentFin() {
  Http2StreamTunnel::SetSentFin(true);
}

void Http2WebTransportSession::StartReading() {
  if (mInput) {
    mInput->AsyncWait(this, 0, 0, nullptr);
  }
}

void Http2WebTransportSession::OnCapsuleParseFailure(nsresult aError) {
  LOG(("Http2WebTransportSession::OnCapsuleParseFailure %p aError=%" PRIX32,
       this, static_cast<uint32_t>(aError)));
}

}  // namespace mozilla::net
