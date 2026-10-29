/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "net/dcsctp/socket/transmission_control_block.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/bind_front.h"
#include "absl/strings/string_view.h"
#include "api/task_queue/task_queue_base.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "net/dcsctp/common/internal_types.h"
#include "net/dcsctp/packet/chunk/data_chunk.h"
#include "net/dcsctp/packet/chunk/forward_tsn_chunk.h"
#include "net/dcsctp/packet/chunk/idata_chunk.h"
#include "net/dcsctp/packet/chunk/iforward_tsn_chunk.h"
#include "net/dcsctp/packet/chunk/reconfig_chunk.h"
#include "net/dcsctp/packet/sctp_packet.h"
#include "net/dcsctp/public/dcsctp_handover_state.h"
#include "net/dcsctp/public/dcsctp_options.h"
#include "net/dcsctp/public/dcsctp_socket.h"
#include "net/dcsctp/public/types.h"
#include "net/dcsctp/rx/data_tracker.h"
#include "net/dcsctp/rx/reassembly_queue.h"
#include "net/dcsctp/socket/capabilities.h"
#include "net/dcsctp/socket/packet_sender.h"
#include "net/dcsctp/socket/stream_reset_handler.h"
#include "net/dcsctp/timer/timer.h"
#include "net/dcsctp/tx/retransmission_queue.h"
#include "net/dcsctp/tx/retransmission_timeout.h"
#include "net/dcsctp/tx/send_queue.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/strings/string_builder.h"

namespace dcsctp {
using ::webrtc::TimeDelta;
using ::webrtc::Timestamp;

TransmissionControlBlock::TransmissionControlBlock(
    TimerManager& timer_manager,
    absl::string_view log_prefix,
    const DcSctpOptions& options,
    const Capabilities& capabilities,
    DcSctpSocketCallbacks& callbacks,
    SendQueue& send_queue,
    VerificationTag my_verification_tag,
    TSN my_initial_tsn,
    VerificationTag peer_verification_tag,
    TSN peer_initial_tsn,
    size_t a_rwnd,
    TieTag tie_tag,
    PacketSender& packet_sender,
    std::function<bool()> is_connection_established)
    : log_prefix_(log_prefix),
      options_(options),
      timer_manager_(timer_manager),
      capabilities_(capabilities),
      callbacks_(callbacks),
      t3_rtx_(timer_manager_.CreateTimer(
          "t3-rtx",
          absl::bind_front(&TransmissionControlBlock::OnRtxTimerExpiry, this),
          TimerOptions(options.rto_initial.ToTimeDelta(),
                       TimerBackoffAlgorithm::kExponential,
                       /*max_restarts=*/std::nullopt,
                       options.max_timer_backoff_duration.has_value()
                           ? options.max_timer_backoff_duration->ToTimeDelta()
                           : TimeDelta::PlusInfinity()))),
      delayed_ack_timer_(timer_manager_.CreateTimer(
          "delayed-ack",
          absl::bind_front(&TransmissionControlBlock::OnDelayedAckTimerExpiry,
                           this),
          TimerOptions(options.delayed_ack_max_timeout.ToTimeDelta(),
                       TimerBackoffAlgorithm::kExponential,
                       /*max_restarts=*/0,
                       /*max_backoff_duration=*/TimeDelta::PlusInfinity(),
                       webrtc::TaskQueueBase::DelayPrecision::kHigh))),
      my_verification_tag_(my_verification_tag),
      my_initial_tsn_(my_initial_tsn),
      peer_verification_tag_(peer_verification_tag),
      peer_initial_tsn_(peer_initial_tsn),
      tie_tag_(tie_tag),
      is_connection_established_(std::move(is_connection_established)),
      packet_sender_(packet_sender),
      rto_(options),
      tx_error_counter_(log_prefix, options),
      data_tracker_(log_prefix, delayed_ack_timer_.get(), peer_initial_tsn),
      reassembly_queue_(log_prefix,
                        options.max_receiver_window_buffer_size,
                        capabilities.message_interleaving),
      retransmission_queue_(
          log_prefix,
          &callbacks_,
          my_initial_tsn,
          a_rwnd,
          send_queue,
          absl::bind_front(&TransmissionControlBlock::ObserveRTT, this),
          [this]() { tx_error_counter_.Clear(); },
          *t3_rtx_,
          options,
          capabilities.partial_reliability,
          capabilities.message_interleaving),
      stream_reset_handler_(log_prefix,
                            this,
                            &timer_manager,
                            &data_tracker_,
                            &reassembly_queue_,
                            &retransmission_queue_),
      heartbeat_handler_(log_prefix, options, this, &timer_manager_) {
  send_queue.EnableMessageInterleaving(capabilities.message_interleaving);
}

void TransmissionControlBlock::ObserveRTT(TimeDelta rtt) {
  TimeDelta prev_rto = rto_.rto();
  rto_.ObserveRTT(rtt);
  RTC_DLOG(LS_VERBOSE) << log_prefix_ << "new rtt=" << webrtc::ToString(rtt)
                       << ", srtt=" << webrtc::ToString(rto_.srtt())
                       << ", rto=" << webrtc::ToString(rto_.rto()) << " ("
                       << webrtc::ToString(prev_rto) << ")";
  t3_rtx_->set_duration(rto_.rto());

  TimeDelta delayed_ack_tmo = std::min(
      rto_.rto() * 0.5, options_.delayed_ack_max_timeout.ToTimeDelta());
  delayed_ack_timer_->set_duration(delayed_ack_tmo);
}

TimeDelta TransmissionControlBlock::OnRtxTimerExpiry() {
  Timestamp now = callbacks_.Now();
  RTC_DLOG(LS_INFO) << log_prefix_ << "Timer " << t3_rtx_->name()
                    << " has expired";
  if (cookie_echo_chunk_.has_value()) {
    // In the COOKIE_ECHO state, let the T1-COOKIE timer trigger
    // retransmissions, to avoid having two timers doing that.
    RTC_DLOG(LS_VERBOSE) << "Not retransmitting as T1-cookie is active.";
  } else {
    if (IncrementTxErrorCounter("t3-rtx expired")) {
      retransmission_queue_.HandleT3RtxTimerExpiry(now);
      SendBufferedPackets(now);
    }
  }
  return TimeDelta::Zero();
}

TimeDelta TransmissionControlBlock::OnDelayedAckTimerExpiry() {
  data_tracker_.HandleDelayedAckTimerExpiry();
  MaybeSendSack();
  return TimeDelta::Zero();
}

void TransmissionControlBlock::MaybeSendSack() {
  if (data_tracker_.ShouldSendAck(/*also_if_delayed=*/false)) {
    SctpPacket::Builder builder = PacketBuilder();
    builder.Add(
        data_tracker_.CreateSelectiveAck(reassembly_queue_.remaining_bytes()));
    Send(builder);
  }
}

void TransmissionControlBlock::MaybeSendForwardTsn(SctpPacket::Builder& builder,
                                                   Timestamp now) {
  if (!retransmission_queue_.ShouldSendForwardTsn(now)) {
    return;
  }
  if (capabilities_.message_interleaving) {
    IForwardTsnChunk chunk = retransmission_queue_.CreateIForwardTsn();
    if (ShouldSendForwardTsnChunk(chunk.new_cumulative_tsn(), now)) {
      builder.Add(chunk);
    }
  } else {
    ForwardTsnChunk chunk = retransmission_queue_.CreateForwardTsn();
    if (ShouldSendForwardTsnChunk(chunk.new_cumulative_tsn(), now)) {
      builder.Add(chunk);
    }
  }
}

bool TransmissionControlBlock::ShouldSendForwardTsnChunk(TSN new_cumulative_tsn,
                                                         Timestamp now) {
  // https://datatracker.ietf.org/doc/html/rfc3758#section-3.5
  // "C3) If, after step C1 and C2, the "Advanced.Peer.Ack.Point" is greater
  // than the Cumulative TSN Ack Point, the data sender MUST send the data
  // receiver a FORWARD TSN chunk containing the latest value of the
  // "Advanced.Peer.Ack.Point"."
  //
  // "IMPLEMENTATION NOTE: An implementation may wish to limit the number of
  // duplicate FORWARD TSN chunks it sends by ... waiting a full RTT before
  // sending a duplicate FORWARD TSN."
  // "Any delay applied to the sending of FORWARD TSN chunk SHOULD NOT exceed
  // 200ms and MUST NOT exceed 500ms".
  //
  // Only duplicates are rate limited. A FORWARD-TSN that advances the
  // cumulative TSN is sent immediately, as delaying it would block the
  // receiver from delivering messages on ordered streams.
  if (new_cumulative_tsn == last_sent_forward_tsn_ &&
      now < limit_forward_tsn_until_) {
    return false;
  }
  last_sent_forward_tsn_ = new_cumulative_tsn;
  limit_forward_tsn_until_ =
      now + std::min(TimeDelta::Millis(200), rto_.srtt());
  return true;
}

void TransmissionControlBlock::MaybeSendFastRetransmit(Timestamp now) {
  if (!retransmission_queue_.has_data_to_be_fast_retransmitted()) {
    return;
  }

  // https://datatracker.ietf.org/doc/html/rfc4960#section-7.2.4
  // "Determine how many of the earliest (i.e., lowest TSN) DATA chunks marked
  // for retransmission will fit into a single packet, subject to constraint of
  // the path MTU of the destination transport address to which the packet is
  // being sent.  Call this value K. Retransmit those K DATA chunks in a single
  // packet.  When a Fast Retransmit is being performed, the sender SHOULD
  // ignore the value of cwnd and SHOULD NOT delay retransmission for this
  // single packet."

  SctpPacket::Builder builder(peer_verification_tag_, options_);
  auto chunks = retransmission_queue_.GetChunksForFastRetransmit(
      now, builder.bytes_remaining());
  for (auto& [tsn, data] : chunks) {
    if (capabilities_.message_interleaving) {
      builder.Add(IDataChunk(tsn, std::move(data), false));
    } else {
      builder.Add(DataChunk(tsn, std::move(data), false));
    }
  }
  Send(builder);
}

void TransmissionControlBlock::SendBufferedPackets(SctpPacket::Builder& builder,
                                                   Timestamp now) {
  for (int packet_idx = 0; packet_idx < options_.max_burst; ++packet_idx) {
    // Only add control chunks to the first packet that is sent, if sending
    // multiple packets in one go (as allowed by the congestion window).
    if (packet_idx == 0) {
      if (cookie_echo_chunk_.has_value()) {
        // https://tools.ietf.org/html/rfc4960#section-5.1
        // "The COOKIE ECHO chunk can be bundled with any pending outbound DATA
        // chunks, but it MUST be the first chunk in the packet..."
        RTC_DCHECK(builder.empty());
        builder.Add(*cookie_echo_chunk_);
      }

      // https://tools.ietf.org/html/rfc4960#section-6
      // "Before an endpoint transmits a DATA chunk, if any received DATA
      // chunks have not been acknowledged (e.g., due to delayed ack), the
      // sender should create a SACK and bundle it with the outbound DATA chunk,
      // as long as the size of the final SCTP packet does not exceed the
      // current MTU."
      if (data_tracker_.ShouldSendAck(/*also_if_delayed=*/true)) {
        builder.Add(data_tracker_.CreateSelectiveAck(
            reassembly_queue_.remaining_bytes()));
      }
      MaybeSendForwardTsn(builder, now);
      std::optional<ReConfigChunk> reconfig =
          stream_reset_handler_.MakeStreamResetRequest();
      if (reconfig.has_value()) {
        builder.Add(*reconfig);
      }
    }

    auto chunks =
        retransmission_queue_.GetChunksToSend(now, builder.bytes_remaining());

    if (!chunks.empty()) {
      // https://datatracker.ietf.org/doc/html/rfc9260#section-8.3
      // Sending DATA means that the path is not idle - restart heartbeat timer.
      heartbeat_handler_.RestartTimer();
    }

    bool set_immediate_sack_bit =
        cwnd() < (options_.immediate_sack_under_cwnd_mtus * options_.mtu);
    for (auto& [tsn, data] : chunks) {
      if (capabilities_.message_interleaving) {
        builder.Add(IDataChunk(tsn, std::move(data), set_immediate_sack_bit));
      } else {
        builder.Add(DataChunk(tsn, std::move(data), set_immediate_sack_bit));
      }
    }

    // https://www.ietf.org/archive/id/draft-tuexen-tsvwg-sctp-zero-checksum-02.html#section-4.2
    // "When an end point sends a packet containing a COOKIE ECHO chunk, it MUST
    // include a correct CRC32c checksum in the packet containing the COOKIE
    // ECHO chunk."
    bool write_checksum = !capabilities_.zero_checksum_enabled() ||
                          cookie_echo_chunk_.has_value();
    if (!packet_sender_.Send(builder, write_checksum)) {
      break;
    }

    if (cookie_echo_chunk_.has_value()) {
      // https://tools.ietf.org/html/rfc4960#section-5.1
      // "...  until the COOKIE ACK is returned the sender MUST NOT send any
      // other packets to the peer."
      break;
    }
  }
}

std::string TransmissionControlBlock::ToString() const {
  webrtc::StringBuilder sb;

  sb.AppendFormat(
      "verification_tag=%08x, last_cumulative_ack=%u, capabilities=",
      *peer_verification_tag_, *data_tracker_.last_cumulative_acked_tsn());

  if (capabilities_.partial_reliability) {
    sb << "PR,";
  }
  if (capabilities_.message_interleaving) {
    sb << "IL,";
  }
  if (capabilities_.reconfig) {
    sb << "Reconfig,";
  }
  if (capabilities_.zero_checksum_enabled()) {
    sb << "ZeroChecksum,";
  }
  sb << " max_in=" << capabilities_.negotiated_maximum_incoming_streams;
  sb << " max_out=" << capabilities_.negotiated_maximum_outgoing_streams;

  return sb.Release();
}

HandoverReadinessStatus TransmissionControlBlock::GetHandoverReadiness() const {
  HandoverReadinessStatus status;
  status.Add(data_tracker_.GetHandoverReadiness());
  status.Add(stream_reset_handler_.GetHandoverReadiness());
  status.Add(reassembly_queue_.GetHandoverReadiness());
  status.Add(retransmission_queue_.GetHandoverReadiness());
  return status;
}

void TransmissionControlBlock::AddHandoverState(
    webrtc::Timestamp now,
    DcSctpSocketHandoverState& state) {
  state.capabilities.partial_reliability = capabilities_.partial_reliability;
  state.capabilities.message_interleaving = capabilities_.message_interleaving;
  state.capabilities.reconfig = capabilities_.reconfig;
  state.capabilities.zero_checksum = capabilities_.zero_checksum_enabled();
  state.capabilities.negotiated_maximum_incoming_streams =
      capabilities_.negotiated_maximum_incoming_streams;
  state.capabilities.negotiated_maximum_outgoing_streams =
      capabilities_.negotiated_maximum_outgoing_streams;

  state.my_verification_tag = my_verification_tag().value();
  state.peer_verification_tag = peer_verification_tag().value();
  state.my_initial_tsn = my_initial_tsn().value();
  state.peer_initial_tsn = peer_initial_tsn().value();
  state.tie_tag = tie_tag().value();

  data_tracker_.AddHandoverState(state);
  stream_reset_handler_.AddHandoverState(state);
  reassembly_queue_.AddHandoverState(state);
  retransmission_queue_.AddHandoverState(now, state);
}

void TransmissionControlBlock::RestoreFromState(
    webrtc::Timestamp now,
    const DcSctpSocketHandoverState& state) {
  data_tracker_.RestoreFromState(state);
  retransmission_queue_.RestoreFromState(now, state);
  reassembly_queue_.RestoreFromState(state);
}
}  // namespace dcsctp
