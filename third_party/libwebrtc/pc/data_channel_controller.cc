/*
 *  Copyright 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/data_channel_controller.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/data_channel_event_observer_interface.h"
#include "api/data_channel_interface.h"
#include "api/peer_connection_tracer_interface.h"
#include "api/priority.h"
#include "api/rtc_error.h"
#include "api/scoped_refptr.h"
#include "api/sctp_transport_interface.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/transport/data_channel_transport_interface.h"
#include "pc/data_channel_utils.h"
#include "pc/peer_connection_internal.h"
#include "pc/sctp_data_channel.h"
#include "pc/sctp_utils.h"
#include "rtc_base/checks.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/logging.h"
#include "rtc_base/ssl_stream_adapter.h"
#include "rtc_base/thread.h"
#include "rtc_base/time_utils.h"

namespace webrtc {

using Message = DataChannelEventObserverInterface::Message;
using Direction = DataChannelEventObserverInterface::Message::Direction;

namespace {

// Returns true if `channel` is in `channels`. Used to check if a raw channel
// pointer, collected before calling out to channels, is still valid. A channel
// that closes is removed from `channels` and its reference is handed over to
// the signaling thread, where the channel may then get destroyed.
bool ContainsChannel(
    const std::vector<scoped_refptr<SctpDataChannel>>& channels,
    const SctpDataChannel* channel) {
  return absl::c_any_of(
      channels, [channel](const auto& c) { return c.get() == channel; });
}

}  // namespace

DataChannelController::~DataChannelController() {
  RTC_DCHECK(sctp_data_channels_n_.empty())
      << "Missing call to TeardownDataChannelTransport_n?";
  RTC_DCHECK(!signaling_safety_.flag()->alive())
      << "Missing call to PrepareForShutdown?";
}

bool DataChannelController::HasDataChannels() const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  return channel_usage_ == DataChannelUsage::kInUse;
}

bool DataChannelController::HasUsedDataChannels() const {
  RTC_DCHECK_RUN_ON(signaling_thread());
  return channel_usage_ != DataChannelUsage::kNeverUsed;
}

void DataChannelController::SetEventObserver(
    std::unique_ptr<DataChannelEventObserverInterface> observer) {
  RTC_DCHECK_RUN_ON(network_thread());
  event_observer_ = std::move(observer);
}

RTCError DataChannelController::SendData(StreamId sid,
                                         const SendDataParams& params,
                                         const CopyOnWriteBuffer& payload) {
  RTC_DCHECK_RUN_ON(network_thread());
  if (!data_channel_transport_) {
    RTC_LOG(LS_ERROR) << "SendData called before transport is ready";
    return RTCError(RTCErrorType::INVALID_STATE);
  }
  RTCError result =
      data_channel_transport_->SendData(sid.stream_id_int(), params, payload);

  if (event_observer_ && result.ok()) {
    if (std::optional<Message> message =
            BuildObserverMessage(sid, params.type, payload, Direction::kSend)) {
      event_observer_->OnMessage(*message);
    }
  }

  return result;
}

RTCError DataChannelController::AddSctpDataStream(StreamId sid,
                                                  PriorityValue priority) {
  RTC_DCHECK_RUN_ON(network_thread());
  if (data_channel_transport_) {
    return data_channel_transport_->OpenChannel(sid.stream_id_int(), priority);
  }
  return RTCError::OK();
}

void DataChannelController::RemoveSctpDataStream(StreamId sid) {
  RTC_DCHECK_RUN_ON(network_thread());
  if (data_channel_transport_) {
    data_channel_transport_->CloseChannel(sid.stream_id_int());
  }
}

void DataChannelController::OnChannelStateChanged(
    SctpDataChannel* channel,
    DataChannelInterface::DataState state) {
  RTC_DCHECK_RUN_ON(network_thread());
  const int channel_id = channel->internal_id();

  scoped_refptr<SctpDataChannel> closed_channel;
  if (state == DataChannelInterface::DataState::kClosed) {
    closed_channel = OnSctpDataChannelClosed(channel);
  }

  DataChannelUsage channel_usage = sctp_data_channels_n_.empty()
                                       ? DataChannelUsage::kHaveBeenUsed
                                       : DataChannelUsage::kInUse;
  // Move `closed_channel` into the signaling_thread() task so that if the
  // controller held the last reference to the channel, its destructor runs on
  // `signaling_thread()`.
  signaling_thread()->PostTask(SafeTask(
      signaling_safety_.flag(), [this, channel_id, state, channel_usage,
                                 closed_channel = std::move(closed_channel)] {
        RTC_DCHECK_RUN_ON(signaling_thread());
        channel_usage_ = channel_usage;
        pc_->OnSctpDataChannelStateChanged(channel_id, state);
      }));
}

size_t DataChannelController::buffered_amount(StreamId sid) const {
  RTC_DCHECK_RUN_ON(network_thread());
  if (!data_channel_transport_) {
    return 0;
  }
  return data_channel_transport_->buffered_amount(sid.stream_id_int());
}

size_t DataChannelController::buffered_amount_low_threshold(
    StreamId sid) const {
  RTC_DCHECK_RUN_ON(network_thread());
  if (!data_channel_transport_) {
    return 0;
  }
  return data_channel_transport_->buffered_amount_low_threshold(
      sid.stream_id_int());
}

void DataChannelController::SetBufferedAmountLowThreshold(StreamId sid,
                                                          size_t bytes) {
  RTC_DCHECK_RUN_ON(network_thread());
  if (!data_channel_transport_) {
    return;
  }
  data_channel_transport_->SetBufferedAmountLowThreshold(sid.stream_id_int(),
                                                         bytes);
}

void DataChannelController::OnTransportConnected() {
  RTC_DCHECK_RUN_ON(network_thread());
  RTC_DCHECK(data_channel_transport_);
  RTC_DCHECK(data_channel_transport_->MaxChannels().has_value());
  sid_allocator_.SetMaxSid(*data_channel_transport_->MaxChannels() - 1);
  RTC_DCHECK(data_channel_transport_->DtlsRole().has_value());
  AllocateSctpSids(*data_channel_transport_->DtlsRole());
}

void DataChannelController::OnDataReceived(int channel_id,
                                           DataMessageType type,
                                           const CopyOnWriteBuffer& buffer) {
  RTC_DCHECK_RUN_ON(network_thread());

  if (HandleOpenMessage_n(channel_id, type, buffer))
    return;

  auto it = absl::c_find_if(sctp_data_channels_n_, [&](const auto& c) {
    return c->sid_n().has_value() && c->sid_n()->stream_id_int() == channel_id;
  });

  if (it != sctp_data_channels_n_.end()) {
    (*it)->OnDataReceived(type, buffer);

    if (event_observer_) {
      if (std::optional<Message> message = BuildObserverMessage(
              StreamId(channel_id), type, buffer, Direction::kReceive)) {
        event_observer_->OnMessage(*message);
      }
    }
  }
}

void DataChannelController::OnChannelClosing(int channel_id) {
  RTC_DCHECK_RUN_ON(network_thread());
  auto it = absl::c_find_if(sctp_data_channels_n_, [&](const auto& c) {
    return c->sid_n().has_value() && c->sid_n()->stream_id_int() == channel_id;
  });

  if (it != sctp_data_channels_n_.end())
    (*it)->OnClosingProcedureStartedRemotely();
}

void DataChannelController::OnChannelClosed(int channel_id) {
  RTC_DCHECK_RUN_ON(network_thread());
  StreamId sid(channel_id);
  auto it = absl::c_find_if(sctp_data_channels_n_,
                            [&](const auto& c) { return c->sid_n() == sid; });

  if (it != sctp_data_channels_n_.end()) {
    (*it)->OnClosingProcedureComplete();
  }
}

void DataChannelController::OnReadyToSend() {
  RTC_DCHECK_RUN_ON(network_thread());
  std::vector<SctpDataChannel*> channels;
  channels.reserve(sctp_data_channels_n_.size());
  for (const auto& channel : sctp_data_channels_n_) {
    channels.push_back(channel.get());
  }
  for (SctpDataChannel* channel : channels) {
    if (ContainsChannel(sctp_data_channels_n_, channel)) {
      // All channels are supposed to either have an ID allocated in
      // OnConnected, or be deleted at this point.
      RTC_DCHECK(channel->sid_n().has_value());
      channel->OnTransportReady();
    }
  }
}

void DataChannelController::OnTransportClosed(RTCError error) {
  RTC_DCHECK_RUN_ON(network_thread());

  // Close all data channels, which will trigger `OnChannelStateChanged` ->
  // `OnSctpDataChannelClosed` and transfer the channel references to the
  // signaling thread.
  while (!sctp_data_channels_n_.empty()) {
    SctpDataChannel* channel = sctp_data_channels_n_.back().get();
    channel->OnTransportChannelClosed(error);
  }
}

void DataChannelController::OnBufferedAmountLow(int channel_id) {
  RTC_DCHECK_RUN_ON(network_thread());
  auto it = absl::c_find_if(sctp_data_channels_n_, [&](const auto& c) {
    return c->sid_n().has_value() && c->sid_n()->stream_id_int() == channel_id;
  });

  if (it != sctp_data_channels_n_.end())
    (*it)->OnBufferedAmountLow();
}

void DataChannelController::OnMaxMessageSize(int max_message_size) {
  RTC_DCHECK_RUN_ON(network_thread());

  max_message_size_ = max_message_size;
  // Tell all the channels about their new max-message-size.
  for (auto& channel : sctp_data_channels_n_) {
    channel->OnMaxMessageSize(max_message_size);
  }
}

void DataChannelController::SetupDataChannelTransport_n(
    DataChannelTransportInterface* transport) {
  RTC_DCHECK_RUN_ON(network_thread());
  RTC_DCHECK(transport);
  set_data_channel_transport(transport);
}

void DataChannelController::PrepareForShutdown() {
  RTC_DCHECK_RUN_ON(signaling_thread());
  signaling_safety_.reset(PendingTaskSafetyFlag::CreateDetachedInactive());
  if (channel_usage_ != DataChannelUsage::kNeverUsed)
    channel_usage_ = DataChannelUsage::kHaveBeenUsed;
}

void DataChannelController::TeardownDataChannelTransport_n(RTCError error) {
  RTC_DCHECK_RUN_ON(network_thread());
  OnTransportClosed(error);
  set_data_channel_transport(nullptr);
  RTC_DCHECK(sctp_data_channels_n_.empty());
  weak_factory_.InvalidateWeakPtrs();
}

void DataChannelController::OnTransportChanged(
    DataChannelTransportInterface* new_data_channel_transport) {
  RTC_DCHECK_RUN_ON(network_thread());
  if (data_channel_transport_ &&
      data_channel_transport_ != new_data_channel_transport) {
    // Changed which data channel transport is used for `sctp_mid_` (eg. now
    // it's bundled).
    set_data_channel_transport(new_data_channel_transport);
  }
}

std::vector<DataChannelStats> DataChannelController::GetDataChannelStats()
    const {
  RTC_DCHECK_RUN_ON(network_thread());
  std::vector<DataChannelStats> stats;
  stats.reserve(sctp_data_channels_n_.size());
  for (const auto& channel : sctp_data_channels_n_)
    stats.push_back(channel->GetStats());
  return stats;
}

bool DataChannelController::HandleOpenMessage_n(
    int channel_id,
    DataMessageType type,
    const CopyOnWriteBuffer& buffer) {
  if (type != DataMessageType::kControl || !IsOpenMessage(buffer))
    return false;

  // Received OPEN message; parse and signal that a new data channel should
  // be created.
  std::string label;
  InternalDataChannelInit config;
  config.id = channel_id;
  if (!ParseDataChannelOpenMessage(buffer, &label, &config)) {
    RTC_LOG(LS_WARNING) << "Failed to parse the OPEN message for sid "
                        << channel_id;
    // Return `true` since the open message must be consumed and discarded.
    return true;
  }
  config.open_handshake_role = InternalDataChannelInit::kAcker;
  auto channel_or_error = CreateDataChannel(label, config);
  if (channel_or_error.ok()) {
    signaling_thread()->PostTask(
        SafeTask(signaling_safety_.flag(),
                 [this, channel = channel_or_error.MoveValue(), channel_id,
                  ready_to_send = data_channel_transport_->IsReadyToSend()] {
                   RTC_DCHECK_RUN_ON(signaling_thread());
                   OnDataChannelOpenMessage(std::move(channel), ready_to_send,
                                            channel_id);
                 }));
  } else {
    RTC_LOG(LS_ERROR) << "Failed to create DataChannel from the OPEN message. "
                      << channel_or_error;
  }
  return true;
}

void DataChannelController::OnDataChannelOpenMessage(
    scoped_refptr<SctpDataChannel> channel,
    bool ready_to_send,
    int id) {
  channel_usage_ = DataChannelUsage::kInUse;
  auto proxy = SctpDataChannel::CreateProxy(channel);

  if (auto* tracer = pc_->tracer()) {
    tracer->OnDataChannel(*proxy, id);
  }
  pc_->RunWithObserver([&](auto observer) { observer->OnDataChannel(proxy); });
  pc_->NoteDataAddedEvent();

  if (ready_to_send) {
    channel->PostOnTransportReady();
  }
}

// RTC_RUN_ON(network_thread())
RTCError DataChannelController::ReserveOrAllocateSid(
    std::optional<StreamId>& sid,
    std::optional<SSLRole> fallback_ssl_role) {
  if (sid.has_value()) {
    if (sid_allocator_.ReserveSid(*sid)) {
      return RTCError::OK();
    }
    // SID is reserved. Check if it is held by a closing channel (race).
    auto it = absl::c_find_if(sctp_data_channels_n_,
                              [&](const auto& c) { return c->sid_n() == sid; });
    if (it != sctp_data_channels_n_.end() &&
        (*it)->state() == DataChannelInterface::DataState::kClosing) {
      RTC_LOG(LS_INFO) << "Forcefully closing closing channel with sid "
                       << sid->stream_id_int()
                       << " due to new reservation conflict (race).";
      (*it)->CloseAbruptlyWithError(
          RTCError::OperationErrorWithData("Closed due to stream reuse."));
      // The old channel is now kClosed, and its SID has been released
      // synchronously. Retry reservation.
      if (sid_allocator_.ReserveSid(*sid)) {
        return RTCError::OK();
      }
    }
    return RTCError::InvalidRange("StreamId reserved.");
  }

  // Attempt to allocate an ID based on the negotiated role.
  std::optional<SSLRole> role;
  if (data_channel_transport_) {
    role = data_channel_transport_->DtlsRole();
    if (!role) {
      role = fallback_ssl_role;
    }
  }
  if (role) {
    sid = sid_allocator_.AllocateSid(*role);
    if (!sid.has_value())
      return RTCError(RTCErrorType::RESOURCE_EXHAUSTED);
  }
  // When we get here, we may still not have an ID, but that's a supported case
  // whereby an id will be assigned later.
  RTC_DCHECK(sid.has_value() || !role);
  return RTCError::OK();
}

// RTC_RUN_ON(network_thread())
RTCErrorOr<scoped_refptr<SctpDataChannel>>
DataChannelController::CreateDataChannel(absl::string_view label,
                                         InternalDataChannelInit& config) {
  std::optional<StreamId> sid = std::nullopt;
  if (config.id != -1) {
    if (config.id < 0 || config.id > kMaxSctpSid) {
      return RTCError(RTCErrorType::INVALID_RANGE, "StreamId out of range.");
    }
    sid = StreamId(config.id);
  }

  size_t total_strings_size = label.size() + config.protocol.size();
  for (const scoped_refptr<SctpDataChannel>& dc : sctp_data_channels_n_) {
    total_strings_size += dc->label().size() + dc->protocol().size();
  }
  if (total_strings_size > 1024 * 1024) {
    return RTCError(RTCErrorType::RESOURCE_EXHAUSTED,
                    "DataChannel labels and protocols use too much memory");
  }

  RTCError err = ReserveOrAllocateSid(sid, config.fallback_ssl_role);
  if (!err.ok())
    return err;

  // In case `sid` has changed. Update `config` accordingly.
  if (sid.has_value()) {
    config.id = sid->stream_id_int();
  }

  // If we have an id already, notify the transport.
  if (sid.has_value()) {
    err = AddSctpDataStream(
        *sid, config.priority.value_or(PriorityValue(Priority::kLow)));
    if (!err.ok()) {
      sid_allocator_.ReleaseSid(*sid);
      return err;
    }
  }

  scoped_refptr<SctpDataChannel> channel = SctpDataChannel::Create(
      weak_factory_.GetWeakPtr(), label, data_channel_transport_ != nullptr,
      config, max_message_size_, signaling_safety_.flag(), signaling_thread(),
      network_thread());

  sctp_data_channels_n_.push_back(channel);
  return channel;
}

RTCErrorOr<scoped_refptr<DataChannelInterface>>
DataChannelController::InternalCreateDataChannelWithProxy(
    absl::string_view label,
    const InternalDataChannelInit& config) {
  RTC_DCHECK_RUN_ON(signaling_thread());
  RTC_DCHECK(!pc_->IsClosed());
  if (!config.IsValid()) {
    return RTC_LOG_ERROR(RTCError::InvalidParameter("Invalid DataChannelInit"));
  }

  bool ready_to_send = false;
  InternalDataChannelInit new_config = config;
  auto ret = network_thread()->BlockingCall(
      [&]() -> RTCErrorOr<scoped_refptr<SctpDataChannel>> {
        RTC_DCHECK_RUN_ON(network_thread());
        auto channel = CreateDataChannel(label, new_config);
        if (!channel.ok())
          return channel;
        ready_to_send =
            data_channel_transport_ && data_channel_transport_->IsReadyToSend();
        if (ready_to_send) {
          // If the transport is ready to send because the initial channel
          // ready signal may have been sent before the DataChannel creation.
          // This has to be done async because the upper layer objects (e.g.
          // Chrome glue and WebKit) are not wired up properly until after
          // `InternalCreateDataChannelWithProxy` returns.
          channel.value()->PostOnTransportReady();
        }

        return channel;
      });

  if (!ret.ok())
    return ret.MoveError();

  channel_usage_ = DataChannelUsage::kInUse;
  return SctpDataChannel::CreateProxy(ret.MoveValue());
}

void DataChannelController::AllocateSctpSids(SSLRole role) {
  RTC_DCHECK_RUN_ON(network_thread());

  const bool ready_to_send =
      data_channel_transport_ && data_channel_transport_->IsReadyToSend();

  std::vector<SctpDataChannel*> channels_to_start;
  std::vector<SctpDataChannel*> channels_to_close;
  for (const auto& channel : sctp_data_channels_n_) {
    if (!channel->sid_n().has_value()) {
      std::optional<StreamId> sid = sid_allocator_.AllocateSid(role);
      if (sid.has_value()) {
        channel->SetSctpSid_n(*sid);
        AddSctpDataStream(*sid, channel->priority());
        channels_to_start.push_back(channel.get());
      } else {
        channels_to_close.push_back(channel.get());
      }
    }
  }
  // Since OnTransportReady can cause sending, and sending may fail and cause
  // channel to close, do this outside the loop. A channel that closes is
  // removed from `sctp_data_channels_n_` and may then get destroyed on the
  // signaling thread, so skip channels that are no longer in the list.
  if (ready_to_send) {
    for (SctpDataChannel* channel : channels_to_start) {
      if (ContainsChannel(sctp_data_channels_n_, channel)) {
        RTC_LOG(LS_INFO) << "AllocateSctpSids: Id assigned, ready to send.";
        channel->OnTransportReady();
      }
    }
  }

  // Since closing modifies the list of channels, we have to do the actual
  // closing outside the loop. As above, skip channels that have already been
  // closed and removed from the list.
  for (SctpDataChannel* channel : channels_to_close) {
    if (ContainsChannel(sctp_data_channels_n_, channel)) {
      channel->CloseAbruptlyWithDataChannelFailure(
          "Failed to allocate SCTP SID");
    }
  }
}

scoped_refptr<SctpDataChannel> DataChannelController::OnSctpDataChannelClosed(
    SctpDataChannel* channel) {
  RTC_DCHECK_RUN_ON(network_thread());
  // After the closing procedure is done, it's safe to use this ID for
  // another data channel.
  if (channel->sid_n().has_value()) {
    sid_allocator_.ReleaseSid(*channel->sid_n());
  }
  auto it = absl::c_find_if(sctp_data_channels_n_,
                            [&](const auto& c) { return c.get() == channel; });
  if (it != sctp_data_channels_n_.end()) {
    scoped_refptr<SctpDataChannel> closed_channel = std::move(*it);
    sctp_data_channels_n_.erase(it);
    return closed_channel;
  }
  return nullptr;
}

void DataChannelController::set_data_channel_transport(
    DataChannelTransportInterface* transport) {
  RTC_DCHECK_RUN_ON(network_thread());

  if (data_channel_transport_)
    data_channel_transport_->SetDataSink(nullptr);

  data_channel_transport_ = transport;

  if (data_channel_transport_) {
    // There's a new data channel transport.  This needs to be signaled to the
    // `sctp_data_channels_n_` so that they can reopen and reconnect.  This is
    // necessary when bundling is applied.
    NotifyDataChannelsOfTransportCreated();
    data_channel_transport_->SetDataSink(this);
  }
}

std::optional<Message> DataChannelController::BuildObserverMessage(
    StreamId sid,
    DataMessageType type,
    std::span<const uint8_t> payload,
    Message::Direction direction) const {
  RTC_DCHECK_RUN_ON(network_thread());

  if (type != DataMessageType::kText && type != DataMessageType::kBinary) {
    return std::nullopt;
  }

  auto it = absl::c_find_if(sctp_data_channels_n_, [sid](const auto& channel) {
    return channel->sid_n() == sid;
  });

  if (it == sctp_data_channels_n_.end()) {
    return std::nullopt;
  }

  Message message;
  Message::DataType data_type = type == DataMessageType::kBinary
                                    ? Message::DataType::kBinary
                                    : Message::DataType::kString;
  message.set_data_type(data_type);
  message.set_unix_timestamp_ms(TimeUTCMillis());
  message.set_datachannel_id(sid.stream_id_int());
  message.set_label((*it)->label());
  message.set_direction(direction);
  message.set_data(payload);

  return message;
}

void DataChannelController::NotifyDataChannelsOfTransportCreated() {
  RTC_DCHECK_RUN_ON(network_thread());
  RTC_DCHECK(data_channel_transport_);

  for (const auto& channel : sctp_data_channels_n_) {
    if (channel->sid_n().has_value())
      AddSctpDataStream(*channel->sid_n(), channel->priority());
    channel->OnTransportChannelCreated();
  }
}

Thread* DataChannelController::network_thread() const {
  return pc_->network_thread();
}

Thread* DataChannelController::signaling_thread() const {
  return pc_->signaling_thread();
}

}  // namespace webrtc
