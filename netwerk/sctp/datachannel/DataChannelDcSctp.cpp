/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DataChannelDcSctp.h"

#include <algorithm>
#include <memory>

#include "DataChannelLog.h"
#include "mozilla/Components.h"
#include "mozilla/Maybe.h"
#include "mozilla/RandomNum.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/RTCErrorBinding.h"
#include "transport/MediaTransportTypes.h"
#include "transport/runnable_utils.h"

namespace mozilla {

// dcsctp announces the full SCTP stream range up front (see DcSctpOptions'
// announced_maximum_*_streams, which default to this) and does not support
// RFC6525 Add-Streams renegotiation. Streams 0..kDcSctpMaxStreams-1 are usable.
static constexpr uint16_t kDcSctpMaxStreams = 65535;

DataChannelConnectionDcSctp::DataChannelConnectionDcSctp(
    DataConnectionListener* aListener, nsISerialEventTarget* aTarget,
    MediaTransportHandler* aHandler)
    : DataChannelConnection(aListener, aTarget, aHandler) {
  // We do not second-guess dcsctp's limit here. This is tightened to the
  // negotiated value in OnConnected().
  mNegotiatedIdLimit = kDcSctpMaxStreams;
}

uint16_t DataChannelConnectionDcSctp::GetStreamIdCeiling() const {
  return kDcSctpMaxStreams;
}

void DataChannelConnectionDcSctp::Destroy() {
  MOZ_ASSERT(NS_IsMainThread());
  DC_DEBUG(("%s: %p", __func__, this));
  DataChannelConnection::Destroy();
  mSTS->Dispatch(NS_NewRunnableFunction(
      "DataChannelConnectionDcSctp::Destroy",
      [this, self = RefPtr<DataChannelConnectionDcSctp>(this)]() {
        if (mDcSctp) {
          mDcSctp->Close();
          // Do we do this now?
          mDcSctp = nullptr;
        }
      }));
}

bool DataChannelConnectionDcSctp::RaiseStreamLimitTo(uint16_t aNewLimit) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  DC_DEBUG(("%s: %p", __func__, this));
  // dcsctp announces the maximum number of streams up front and does not
  // support RFC6525 Add-Streams renegotiation, so there is never anything to
  // raise. Just smile and nod.
  return true;
}

void DataChannelConnectionDcSctp::OnTransportReady() {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  DC_DEBUG(("%s: %p", __func__, this));
  if (!mDcSctp) {
    auto factory = std::make_unique<dcsctp::DcSctpSocketFactory>();
    DcSctpOptions options;
    options.local_port = mLocalPort;
    options.remote_port = mRemotePort;
    options.max_message_size = 8 * 1024 * 1024;
    options.max_timer_backoff_duration = DurationMs(3000);
    // Don't close the connection automatically on too many retransmissions.
    options.max_retransmissions = std::nullopt;
    options.max_init_retransmits = std::nullopt;
    options.per_stream_send_queue_limit = 1024 * 1024 * 64;
    // This is just set to avoid denial-of-service. Practically unlimited.
    options.max_send_buffer_size =
        std::numeric_limits<decltype(options.max_send_buffer_size)>::max();
    options.max_receiver_window_buffer_size = 16 * 1024 * 1024;
    options.enable_message_interleaving = true;
    // The default value of 200 leads to extremely poor congestion recovery
    // when packet loss has occurred.
    options.delayed_ack_max_timeout = DurationMs(50);

    mDcSctp =
        factory->Create("DataChannelConnectionDcSctp", *this, nullptr, options);
    mDcSctp->Connect();
  }
}

bool DataChannelConnectionDcSctp::Init(const uint16_t aLocalPort,
                                       const uint16_t aNumStreams) {
  return true;
}

int DataChannelConnectionDcSctp::SendMessage(DataChannel& aChannel,
                                             OutgoingMsg&& aMsg) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  DC_DEBUG(("%s: %p (size %u)", __func__, this,
            static_cast<unsigned>(aMsg.GetRemainingData().size())));
  if (!mDcSctp) {
    return EBADF;  // Debatable?
  }

  // I do not see any way to get nsCString to pass ownership of its buffer to
  // anything besides another nsString. Bummer.
  auto remaining = aMsg.GetRemainingData();
  std::vector<uint8_t> data;
  data.assign(remaining.begin(), remaining.end());
  DcSctpMessage msg(StreamID(aMsg.GetMetadata().mStreamId),
                    PPID(aMsg.GetMetadata().mPpid), std::move(data));
  SendOptions options;
  options.unordered = IsUnordered(aMsg.GetMetadata().mUnordered);
  aMsg.GetMetadata().mMaxLifetimeMs.apply([&options](uint16_t lifetime) {
    options.lifetime = DurationMs(lifetime);
  });
  aMsg.GetMetadata().mMaxRetransmissions.apply(
      [&options](uint16_t rtx) { options.max_retransmissions = rtx; });

  if (aMsg.GetMetadata().mPpid == DATA_CHANNEL_PPID_CONTROL) {
    // Make sure we get a callback when this DCEP message is sent, and remember
    // the stream id and the size. This allows us to work around the dcsctp bug
    // that counts DCEP as part of bufferedAmount.
    uint64_t id = mNextLifecycleId++;
    options.lifecycle_id = LifecycleId(id);
    mBufferedDCEPBytes[id] =
        std::make_pair(aMsg.GetMetadata().mStreamId, remaining.size());
  }

  auto result = mDcSctp->Send(std::move(msg), options);

  if (aMsg.GetMetadata().mPpid != DATA_CHANNEL_PPID_DOMSTRING_EMPTY &&
      aMsg.GetMetadata().mPpid != DATA_CHANNEL_PPID_BINARY_EMPTY) {
    mBufferedAmounts[aMsg.GetMetadata().mStreamId] += remaining.size();
  }

  switch (result) {
    case SendStatus::kSuccess:
      break;
    case SendStatus::kErrorMessageEmpty:
      DC_ERROR(("%s: %p send failed (kErrorMessageEmpty)", __func__, this));
      return EINVAL;
    case SendStatus::kErrorMessageTooLarge:
      DC_ERROR(("%s: %p send failed (kErrorMessageTooLarge)", __func__, this));
      return EMSGSIZE;
    case SendStatus::kErrorResourceExhaustion:
      DC_ERROR(
          ("%s: %p send failed (kErrorResourceExhaustion)", __func__, this));
      return ENOBUFS;  // Debatable?
    case SendStatus::kErrorShuttingDown:
      DC_ERROR(("%s: %p send failed (kErrorShuttingDown)", __func__, this));
      return EPIPE;  // Debatable?
  }

  return 0;
}

void DataChannelConnectionDcSctp::OnSctpPacketReceived(
    const MediaPacket& aPacket) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  DC_DEBUG(
      ("%s: %p size=%u", __func__, this, static_cast<unsigned>(aPacket.len())));
  if (!mDcSctp) {
    return;
  }
  std::span<const uint8_t> data(aPacket.data(), aPacket.len());
  mDcSctp->ReceivePacket(data);
}

bool DataChannelConnectionDcSctp::ResetStreams(nsTArray<uint16_t>& aStreams) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  DC_DEBUG(("%s: %p", __func__, this));
  if (!mDcSctp) {
    return false;
  }
  std::vector<StreamID> converted;
  for (auto id : aStreams) {
    DC_DEBUG(("%s: %p Resetting %u", __func__, this, id));
    converted.push_back(StreamID(id));
  }
  auto result = mDcSctp->ResetStreams(std::span<const StreamID>(converted));
  aStreams.Clear();
  return result == ResetStreamsStatus::kPerformed;
}

void DataChannelConnectionDcSctp::OnStreamOpen(uint16_t aStream) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  DC_DEBUG(("%s: %p", __func__, this));
  for (auto it = mPreChannelData.begin(); it != mPreChannelData.end();) {
    if (it->GetStreamId() == aStream) {
      HandleDataMessage(std::move(*it));
      it = mPreChannelData.erase(it);
    } else {
      ++it;
    }
  }
}

SendPacketStatus DataChannelConnectionDcSctp::SendPacketWithStatus(
    std::span<const uint8_t> aData) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  DC_DEBUG(("%s: %p", __func__, this));
  auto packet = std::make_unique<MediaPacket>();
  packet->SetType(MediaPacket::SCTP);
  packet->Copy(aData.data(), aData.size());

  DataChannelConnection::SendPacket(std::move(packet));
  return SendPacketStatus::kSuccess;
}

class DcSctpTimeout : public Timeout {
 public:
  explicit DcSctpTimeout(DataChannelConnectionDcSctp* aConnection)
      : mConnection(aConnection) {}

  // Called to start time timeout, with the duration in milliseconds as
  // `duration` and with the timeout identifier as `timeout_id`, which - if
  // the timeout expires - shall be provided to `DcSctpSocket::HandleTimeout`.
  //
  // `Start` and `Stop` will always be called in pairs. In other words will
  // ´Start` never be called twice, without a call to `Stop` in between.
  void Start(DurationMs duration, TimeoutID timeout_id) override {
    mId = timeout_id.value();
    DC_DEBUG(("%s: %u %ums", __func__, mId,
              static_cast<unsigned>(duration.value())));
    auto result = NS_NewTimerWithCallback(
        [connection = mConnection, timeout_id](nsITimer* timer) {
          DC_DEBUG(("%s: %u fired", __func__,
                    static_cast<unsigned>(timeout_id.value())));
          connection->HandleTimeout(timeout_id);
        },
        duration.value(), nsITimer::TYPE_ONE_SHOT, "DcSctpTimeout::Start"_ns);
    if (result.isOk()) {
      mTimer = result.unwrap();
    }
  }

  // Called to stop the running timeout.
  //
  // `Start` and `Stop` will always be called in pairs. In other words will
  // ´Start` never be called twice, without a call to `Stop` in between.
  //
  // `Stop` will always be called prior to releasing this object.
  void Stop() override {
    DC_DEBUG(("%s: %u", __func__, mId));
    if (mTimer) {
      mTimer->Cancel();
      mTimer = nullptr;
    }
  }

 private:
  RefPtr<DataChannelConnectionDcSctp> mConnection;
  nsCOMPtr<nsITimer> mTimer;
  unsigned mId = 0;
};

std::unique_ptr<Timeout> DataChannelConnectionDcSctp::CreateTimeout(
    webrtc::TaskQueueBase::DelayPrecision aPrecision) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  DC_DEBUG(("%s: %p", __func__, this));
  // There is no such thing as a low precision TYPE_ONE_SHOT
  (void)aPrecision;
  return std::make_unique<DcSctpTimeout>(this);
}

void DataChannelConnectionDcSctp::HandleTimeout(TimeoutID aId) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  DC_DEBUG(("%s: %p", __func__, this));
  if (mDcSctp) {
    mDcSctp->HandleTimeout(aId);
  }
}

webrtc::Timestamp DataChannelConnectionDcSctp::Now() {
  return webrtc::Timestamp::Micros(static_cast<int64_t>(
      (TimeStamp::Now() - TimeStamp::ProcessCreation()).ToMicroseconds()));
}

uint32_t DataChannelConnectionDcSctp::GetRandomInt(uint32_t aLow,
                                                   uint32_t aHigh) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  DC_DEBUG(("%s: %p", __func__, this));
  return aLow + RandomUint64OrDie() % (aHigh - aLow);
}

void DataChannelConnectionDcSctp::OnMessageReceived(DcSctpMessage aMessage) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  DC_DEBUG(("%s: %p", __func__, this));
  RefPtr<DataChannel> channel =
      FindChannelByStream(aMessage.stream_id().value());

  IncomingMsg msg(aMessage.ppid().value(), aMessage.stream_id().value());
  // Sadly, nsCString and std::vector have no way to relinquish their buffers
  // to one another.
  msg.Append(aMessage.payload().data(), aMessage.payload().size());
  if (msg.GetPpid() == DATA_CHANNEL_PPID_CONTROL) {
    HandleDCEPMessage(std::move(msg));
  } else if (channel && !HasPreChannelData(msg.GetStreamId())) {
    HandleDataMessage(std::move(msg));
  } else {
    mPreChannelData.push_back(std::move(msg));
  }
}

void DataChannelConnectionDcSctp::OnError(ErrorKind aError,
                                          absl::string_view aMessage) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  DC_ERROR(("%s: %p %d %s", __func__, this, static_cast<int>(aError),
            std::string(aMessage).c_str()));
}

void DataChannelConnectionDcSctp::OnAborted(ErrorKind aError,
                                            absl::string_view aMessage) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  DC_ERROR(("%s: %p %d %s", __func__, this, static_cast<int>(aError),
            std::string(aMessage).c_str()));
  // The SCTP association was aborted; surface this to content as an
  // "sctp-failure" RTCError on the data channels. dcSCTP exposes only its own
  // ErrorKind (not a numeric SCTP cause code), so sctpCauseCode is left unset.
  dom::RTCErrorParams params;
  params.errorInit.mErrorDetail = dom::RTCErrorDetailType::Sctp_failure;
  params.message = nsCString(aMessage.data(), aMessage.length());
  CloseAll_s(Some(std::move(params)));
}

void DataChannelConnectionDcSctp::OnConnected() {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  DC_DEBUG(("%s: %p", __func__, this));
  DataChannelConnectionState state = GetState();
  // TODO: Some duplicate code here, refactor
  if (state == DataChannelConnectionState::Connecting) {
    // The negotiated stream counts are now known. maxChannels is the minimum
    // of the negotiated incoming and outgoing streams; this tightens our id
    // limit if the peer announced fewer streams than we did.
    // Must happen before SetState(Open), which dispatches the
    // NotifySctpConnected that carries maxChannels to DOM.
    if (std::optional<dcsctp::Metrics> metrics = mDcSctp->GetMetrics()) {
      mNegotiatedIdLimit =
          std::min(metrics->negotiated_maximum_incoming_streams,
                   metrics->negotiated_maximum_outgoing_streams);
    }

    SetState(DataChannelConnectionState::Open);

    OnConnected();
    DC_DEBUG(("%s: %p DTLS connect() succeeded!  Entering connected mode",
              __func__, this));

    // Open any streams pending...
    // TODO: Do we really need to dispatch here? We're already on STS...
    RUN_ON_THREAD(mSTS,
                  WrapRunnable(RefPtr<DataChannelConnection>(this),
                               &DataChannelConnection::ProcessQueuedOpens),
                  NS_DISPATCH_NORMAL);

  } else if (state == DataChannelConnectionState::Open) {
    DC_DEBUG(("%s: %p DataConnection Already OPEN", __func__, this));
  } else {
    DC_ERROR(("%s: %p Unexpected state: %s", __func__, this, ToString(state)));
  }
}

void DataChannelConnectionDcSctp::OnClosed() {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  DC_DEBUG(("%s: %p", __func__, this));
  CloseAll_s();
}

void DataChannelConnectionDcSctp::OnConnectionRestarted() {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  DC_DEBUG(("%s: %p", __func__, this));
}

void DataChannelConnectionDcSctp::OnStreamsResetFailed(
    std::span<const StreamID> aOutgoingStreams, absl::string_view aReason) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  DC_ERROR(("%s: %p", __func__, this));
  // It probably does not make much sense to retry this here. If dcsctp doesn't
  // want to retry, we probably don't either.
  (void)aReason;
  std::vector<uint16_t> streamsReset;
  for (auto id : aOutgoingStreams) {
    streamsReset.push_back(id.value());
  }
  OnStreamsResetComplete(std::move(streamsReset));
}

void DataChannelConnectionDcSctp::OnStreamsResetPerformed(
    std::span<const StreamID> aOutgoingStreams) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  DC_DEBUG(("%s: %p", __func__, this));
  std::vector<uint16_t> streamsReset;
  for (auto id : aOutgoingStreams) {
    streamsReset.push_back(id.value());
  }
  OnStreamsResetComplete(std::move(streamsReset));
}

void DataChannelConnectionDcSctp::OnIncomingStreamsReset(
    std::span<const StreamID> aIncomingStreams) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  DC_DEBUG(("%s: %p", __func__, this));
  std::vector<uint16_t> streamsReset;
  for (auto id : aIncomingStreams) {
    streamsReset.push_back(id.value());
  }
  OnStreamsReset(std::move(streamsReset));
}

// We (ab)use this callback to detect when _any_ data has been sent on the
// stream id, to drive updates to mainthread.
void DataChannelConnectionDcSctp::OnBufferedAmountLow(StreamID aStreamId) {
  MOZ_ASSERT(mSTS->IsOnCurrentThread());
  UpdateBufferedAmount(aStreamId);
}

void DataChannelConnectionDcSctp::OnLifecycleMessageFullySent(
    LifecycleId aLifecycleId) {
  DC_DEBUG(("%s: %p aLifecycleId=%u", __func__, this,
            static_cast<unsigned>(aLifecycleId.value())));
  OnDCEPMessageDone(aLifecycleId);
}

void DataChannelConnectionDcSctp::OnLifecycleMessageExpired(
    LifecycleId aLifecycleId, bool aMaybeDelivered) {
  DC_DEBUG(("%s: %p aLifecycleId=%u aMaybeDelivered=%d", __func__, this,
            static_cast<unsigned>(aLifecycleId.value()),
            static_cast<int>(aMaybeDelivered)));
  if (!aMaybeDelivered) {
    OnDCEPMessageDone(aLifecycleId);
  }
}

void DataChannelConnectionDcSctp::UpdateBufferedAmount(StreamID aStreamId) {
  DC_DEBUG(("%s: %p id=%u", __func__, this,
            static_cast<unsigned>(aStreamId.value())));
  mSTS->Dispatch(NS_NewRunnableFunction(
      "DataChannelConnectionDcSctp::UpdateBufferedAmount",
      [this, self = RefPtr<DataChannelConnectionDcSctp>(this), aStreamId]() {
        auto channel = mChannels.Get(aStreamId.value());
        if (!channel || !mDcSctp) {
          return;
        }

        size_t oldAmount = mBufferedAmounts[aStreamId.value()];
        size_t newAmount = mDcSctp->buffered_amount(aStreamId);
        int decreaseWithoutDCEP =
            oldAmount - newAmount - mDCEPBytesSent[aStreamId.value()];

        if (decreaseWithoutDCEP > 0) {
          channel->DecrementBufferedAmount(decreaseWithoutDCEP);
        }

        DC_DEBUG(("%s: %p id=%u amount %u -> %u (difference without DCEP %d)",
                  __func__, this, static_cast<unsigned>(aStreamId.value()),
                  static_cast<unsigned>(oldAmount),
                  static_cast<unsigned>(newAmount), decreaseWithoutDCEP));
        mDCEPBytesSent.erase(aStreamId.value());
        mBufferedAmounts[aStreamId.value()] = newAmount;
        mDcSctp->SetBufferedAmountLowThreshold(aStreamId,
                                               newAmount ? newAmount - 1 : 0);
      }));
}

void DataChannelConnectionDcSctp::OnDCEPMessageDone(LifecycleId aLifecycleId) {
  DC_DEBUG(("%s: %p", __func__, this));
  // Find the stream id and the size of this DCEP packet.
  auto it = mBufferedDCEPBytes.find(aLifecycleId.value());
  if (it == mBufferedDCEPBytes.end()) {
    MOZ_ASSERT(false);
    return;
  }

  auto& [stream, size] = it->second;

  // Find the running total of DCEP bytes sent for this stream, and add the
  // number of DCEP bytes we just learned about.
  mDCEPBytesSent[stream] += size;
  DC_DEBUG(("%s: %p id=%u amount=%u", __func__, this,
            static_cast<unsigned>(stream), static_cast<unsigned>(size)));

  // This is mainly to reset the buffered amount low threshold.
  UpdateBufferedAmount(StreamID(stream));

  mBufferedDCEPBytes.erase(it);
}

bool DataChannelConnectionDcSctp::HasPreChannelData(uint16_t aStream) const {
  for (const auto& msg : mPreChannelData) {
    if (msg.GetStreamId() == aStream) {
      return true;
    }
  }
  return false;
}

}  // namespace mozilla
