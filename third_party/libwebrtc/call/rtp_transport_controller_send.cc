/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "call/rtp_transport_controller_send.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "api/call/transport.h"
#include "api/fec_controller.h"
#include "api/frame_transformer_interface.h"
#include "api/rtc_event_log/rtc_event_log.h"
#include "api/rtp_packet_sender.h"
#include "api/rtp_parameters.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/transport/bandwidth_estimation_settings.h"
#include "api/transport/bitrate_settings.h"
#include "api/transport/ecn_marking.h"
#include "api/transport/goog_cc_factory.h"
#include "api/transport/network_control.h"
#include "api/transport/network_types.h"
#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "call/rtp_config.h"
#include "call/rtp_transport_config.h"
#include "call/rtp_transport_controller_send_interface.h"
#include "call/rtp_video_sender.h"
#include "call/rtp_video_sender_interface.h"
#include "logging/rtc_event_log/events/rtc_event_route_change.h"
#include "modules/congestion_controller/rtp/congestion_controller_feedback_stats.h"
#include "modules/congestion_controller/rtp/control_handler.h"
#include "modules/pacing/packet_router.h"
#include "modules/rtp_rtcp/include/report_block_data.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtcp_packet/congestion_control_feedback.h"
#include "modules/rtp_rtcp/source/rtcp_packet/transport_feedback.h"
#include "modules/rtp_rtcp/source/rtp_rtcp_interface.h"
#include "rtc_base/checks.h"
#include "rtc_base/containers/flat_map.h"
#include "rtc_base/logging.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/network_route.h"
#include "rtc_base/rate_limiter.h"
#include "rtc_base/task_utils/repeating_task.h"

namespace webrtc {
namespace {

constexpr TimeDelta kPacerQueueUpdateInterval = TimeDelta::Millis(25);

TargetRateConstraints ConvertConstraints(int min_bitrate_bps,
                                         int max_bitrate_bps,
                                         int start_bitrate_bps,
                                         Clock* clock) {
  TargetRateConstraints msg;
  msg.at_time = clock->CurrentTime();
  msg.min_data_rate = min_bitrate_bps >= 0
                          ? DataRate::BitsPerSec(min_bitrate_bps)
                          : DataRate::Zero();
  msg.max_data_rate = max_bitrate_bps > 0
                          ? DataRate::BitsPerSec(max_bitrate_bps)
                          : DataRate::Infinity();
  if (start_bitrate_bps > 0)
    msg.starting_rate = DataRate::BitsPerSec(start_bitrate_bps);
  return msg;
}

TargetRateConstraints ConvertConstraints(const BitrateConstraints& contraints,
                                         Clock* clock) {
  return ConvertConstraints(contraints.min_bitrate_bps,
                            contraints.max_bitrate_bps,
                            contraints.start_bitrate_bps, clock);
}

}  // namespace

RtpTransportControllerSend::RtpTransportControllerSend(
    const RtpTransportConfig& config)
    : env_(config.env),
      network_thread_(config.network_thread),
      bitrate_configurator_(config.bitrate_config),
      pacer_started_(false),
      pacer_(&env_.clock(),
             &packet_router_,
             env_.field_trials(),
             TimeDelta::Millis(5),
             3,
             network_thread_,
             PacerConfig::Create(
                 config.env.clock().CurrentTime(),
                 ConvertConstraints(config.bitrate_config, &config.env.clock())
                     .starting_rate.value_or(DataRate::Zero()),
                 DataRate::Zero(),
                 config.default_pacing_time_window)),
      observer_(nullptr),
      controller_factory_override_(config.network_controller_factory),
      process_interval_(TimeDelta::PlusInfinity()),
      last_report_block_time_(env_.clock().CurrentTime()),
      initial_config_(env_),
      reset_bwe_on_adapter_id_change_(
          env_.field_trials().IsEnabled("WebRTC-Bwe-ResetOnAdapterIdChange")),
      network_available_(false),
      congestion_window_size_(DataSize::PlusInfinity()),
      is_congested_(false) {
  RTC_DCHECK(network_thread_);
  initial_config_.constraints =
      ConvertConstraints(config.bitrate_config, &env_.clock());
  initial_config_.default_pacing_time_window =
      config.default_pacing_time_window;
  RTC_DCHECK(config.bitrate_config.start_bitrate_bps > 0);
}

RtpTransportControllerSend::~RtpTransportControllerSend() {
  RTC_DCHECK_RUN_ON(network_thread_);
  RTC_DCHECK(video_rtp_senders_.empty());
  pacer_queue_update_task_.Stop();
  controller_task_.Stop();
}

RtpVideoSenderInterface* RtpTransportControllerSend::CreateRtpVideoSender(
    const std::map<uint32_t, RtpState>& suspended_ssrcs,
    const std::map<uint32_t, RtpPayloadState>& states,
    const RtpConfig& rtp_config,
    int rtcp_report_interval_ms,
    Transport* send_transport,
    const RtpSenderObservers& observers,
    std::unique_ptr<FecController> fec_controller,
    const RtpSenderFrameEncryptionConfig& frame_encryption_config,
    scoped_refptr<FrameTransformerInterface> frame_transformer) {
  RTC_DCHECK_RUN_ON(network_thread_);
  video_rtp_senders_.push_back(std::make_unique<RtpVideoSender>(
      env_, network_thread_, suspended_ssrcs, states, rtp_config,
      rtcp_report_interval_ms, send_transport, observers,
      // TODO(holmer): Remove this circular dependency by injecting
      // the parts of RtpTransportControllerSendInterface that are really used.
      this, std::move(fec_controller), frame_encryption_config.frame_encryptor,
      frame_encryption_config.crypto_options, std::move(frame_transformer)));
  return video_rtp_senders_.back().get();
}

void RtpTransportControllerSend::DestroyRtpVideoSender(
    RtpVideoSenderInterface* rtp_video_sender) {
  RTC_DCHECK_RUN_ON(network_thread_);
  std::vector<std::unique_ptr<RtpVideoSenderInterface>>::iterator it =
      video_rtp_senders_.end();
  for (it = video_rtp_senders_.begin(); it != video_rtp_senders_.end(); ++it) {
    if (it->get() == rtp_video_sender) {
      break;
    }
  }
  RTC_DCHECK(it != video_rtp_senders_.end());
  video_rtp_senders_.erase(it);
}

void RtpTransportControllerSend::RegisterSendingRtpStream(
    RtpRtcpInterface& rtp_module) {
  RTC_DCHECK_RUN_ON(network_thread_);
  // Allow pacer to send packets using this module.
  packet_router_.AddSendRtpModule(&rtp_module,
                                  /*remb_candidate=*/true);
  pacer_.SetAllowProbeWithoutMediaPacket(
      bwe_settings_.allow_probe_without_media &&
      packet_router_.SupportsRtxPayloadPadding());
}

void RtpTransportControllerSend::DeRegisterSendingRtpStream(
    RtpRtcpInterface& rtp_module) {
  RTC_DCHECK_RUN_ON(network_thread_);
  // Disabling media, remove from packet router map to reduce size and
  // prevent any stray packets in the pacer from asynchronously arriving
  // to a disabled module.
  packet_router_.RemoveSendRtpModule(&rtp_module);
  // Clear the pacer queue of any packets pertaining to this module.
  pacer_.RemovePacketsForSsrc(rtp_module.SSRC());
  if (rtp_module.RtxSsrc().has_value()) {
    pacer_.RemovePacketsForSsrc(*rtp_module.RtxSsrc());
  }
  if (rtp_module.FlexfecSsrc().has_value()) {
    pacer_.RemovePacketsForSsrc(*rtp_module.FlexfecSsrc());
  }
  pacer_.SetAllowProbeWithoutMediaPacket(
      bwe_settings_.allow_probe_without_media &&
      packet_router_.SupportsRtxPayloadPadding());
}

void RtpTransportControllerSend::UpdateControlState() {
  std::optional<TargetTransferRate> update = control_handler_->GetUpdate();
  if (!update)
    return;
  // We won't create control_handler_ until we have an observers.
  RTC_DCHECK(observer_ != nullptr);
  observer_->OnTargetTransferRate(*update);
}

void RtpTransportControllerSend::UpdateCongestedState() {
  if (auto update = GetCongestedStateUpdate()) {
    is_congested_ = update.value();
    pacer_.SetCongested(update.value());
  }
}

std::optional<bool> RtpTransportControllerSend::GetCongestedStateUpdate()
    const {
  bool congested = transport_feedback_adapter_.GetOutstandingData() >=
                   congestion_window_size_;
  if (congested != is_congested_)
    return congested;
  return std::nullopt;
}

PacketRouter* RtpTransportControllerSend::packet_router() {
  return &packet_router_;
}

NetworkStateEstimateObserver*
RtpTransportControllerSend::network_state_estimate_observer() {
  return this;
}

RtpPacketSender* RtpTransportControllerSend::packet_sender() {
  return &pacer_;
}

void RtpTransportControllerSend::SetAllocatedSendBitrateLimits(
    BitrateAllocationLimits limits) {
  RTC_DCHECK_RUN_ON(network_thread_);
  streams_config_.min_total_allocated_bitrate = limits.min_allocatable_rate;
  streams_config_.max_padding_rate = limits.max_padding_rate;
  streams_config_.max_total_allocated_bitrate = limits.max_allocatable_rate;
  UpdateStreamsConfig();
}
void RtpTransportControllerSend::SetPacingFactor(float pacing_factor) {
  RTC_DCHECK_RUN_ON(network_thread_);
  // TODO: bugs.webrtc.org/447037083 - Remove or update usage of SetPacingFactor
  // if RFC 8888 is enabled. With RFC 8888 feedback, this method is not
  // invoked. Goog CC sets a sensible pacing factor by itself.
  streams_config_.pacing_factor = pacing_factor;
  UpdateStreamsConfig();
}
void RtpTransportControllerSend::SetQueueTimeLimit(int limit_ms) {
  pacer_.SetQueueTimeLimit(TimeDelta::Millis(limit_ms));
}
StreamFeedbackProvider*
RtpTransportControllerSend::GetStreamFeedbackProvider() {
  return &feedback_demuxer_;
}

void RtpTransportControllerSend::ReconfigureBandwidthEstimation(
    const BandwidthEstimationSettings& settings) {
  RTC_DCHECK_RUN_ON(network_thread_);
  bwe_settings_ = settings;

  streams_config_.enable_repeated_initial_probing =
      bwe_settings_.allow_probe_without_media;
  bool allow_probe_without_media = bwe_settings_.allow_probe_without_media &&
                                   packet_router_.SupportsRtxPayloadPadding();
  pacer_.SetAllowProbeWithoutMediaPacket(allow_probe_without_media);

  if (controller_) {
    // Recreate the controller and handler.
    control_handler_ = nullptr;
    controller_ = nullptr;
    // The BWE controller is created when/if the network is available.
    MaybeCreateControllers();
    if (controller_) {
      BitrateConstraints constraints = bitrate_configurator_.GetConfig();
      UpdateBitrateConstraints(constraints);
      UpdateStreamsConfig();
      UpdateNetworkAvailability();
    }
  }
}

void RtpTransportControllerSend::RegisterTargetTransferRateObserver(
    TargetTransferRateObserver* observer) {
  RTC_DCHECK_RUN_ON(network_thread_);
  RTC_DCHECK(observer_ == nullptr);
  observer_ = observer;
  observer_->OnStartRateUpdate(*initial_config_.constraints.starting_rate);
  if (!transport_overhead_per_packet_.IsZero()) {
    observer_->OnTransportOverheadChanged(transport_overhead_per_packet_);
  }
  MaybeCreateControllers();
}

bool RtpTransportControllerSend::IsRelevantRouteChange(
    const NetworkRoute& old_route,
    const NetworkRoute& new_route) const {
  bool connected_changed = old_route.connected != new_route.connected;
  bool route_ids_changed = false;

  if (reset_bwe_on_adapter_id_change_) {
    route_ids_changed =
        old_route.local.adapter_id() != new_route.local.adapter_id() ||
        old_route.remote.adapter_id() != new_route.remote.adapter_id();
  } else {
    route_ids_changed =
        old_route.local.network_id() != new_route.local.network_id() ||
        old_route.remote.network_id() != new_route.remote.network_id();
  }
  return connected_changed || route_ids_changed;
}

void RtpTransportControllerSend::OnNetworkRouteChanged(
    absl::string_view transport_name,
    const NetworkRoute& network_route) {
  RTC_DCHECK_RUN_ON(network_thread_);
  // Check if the network route is connected.
  if (!network_route.connected) {
    // TODO(honghaiz): Perhaps handle this in SignalChannelNetworkState and
    // consider merging these two methods.
    return;
  }

  DataSize packet_overhead = DataSize::Bytes(network_route.packet_overhead);
  if (transport_overhead_per_packet_ != packet_overhead) {
    transport_overhead_per_packet_ = packet_overhead;
    pacer_.SetTransportOverhead(transport_overhead_per_packet_);
    if (observer_) {
      observer_->OnTransportOverheadChanged(transport_overhead_per_packet_);
    }
  }

  // Check whether the network route has changed on each transport.
  auto result = network_routes_.insert(
      // Explicit conversion of transport_name to std::string here is necessary
      // to support some platforms that cannot yet deal with implicit
      // conversion in these types of situations.
      std::make_pair(std::string(transport_name), network_route));
  auto kv = result.first;
  bool inserted = result.second;
  if (inserted || !(kv->second == network_route)) {
    RTC_LOG(LS_INFO) << "Network route changed on transport " << transport_name
                     << ": new_route = " << network_route.DebugString();
    if (!inserted) {
      RTC_LOG(LS_INFO) << "old_route = " << kv->second.DebugString();
    }
  }
  if (inserted) {
    // No need to reset BWE if this is the first time the network connects.
    return;
  }
  const NetworkRoute old_route = kv->second;
  kv->second = network_route;

  // Check if the new/old route is significantly different from the old route.
  // If it is, restart BWE.
  bool restart_bwe = IsRelevantRouteChange(old_route, network_route);

  // If Scream is used, notify the controller of the route change regardless
  // of what changed in order for it to understand that the route has changed,
  // and thus RTT may have changed. Always log the route change and ensure the
  // feedback adapter only reports packets sent on the new route since ECN
  // support may be different.
  bool is_controller_supporting_ecn = rfc_8888_feedback_negotiated_ &&
                                      controller_ &&
                                      controller_->SupportsEcnAdaptation();
  if (!restart_bwe && !is_controller_supporting_ecn) {
    // Nothing changed - no need to notify the controller.
    return;
  }

  if (restart_bwe) {
    BitrateConstraints bitrate_config = bitrate_configurator_.GetConfig();
    RTC_LOG(LS_INFO) << "Reset bitrates to min: "
                     << bitrate_config.min_bitrate_bps
                     << " bps, start: " << bitrate_config.start_bitrate_bps
                     << " bps,  max: " << bitrate_config.max_bitrate_bps
                     << " bps.";
  }

  env_.event_log().Log(std::make_unique<RtcEventRouteChange>(
      network_route.connected, network_route.packet_overhead));
  transport_feedback_adapter_.SetNetworkRoute(network_route);

  // Reset the congested state.
  is_congested_ = false;
  pacer_.SetCongested(false);

  NetworkRouteChange msg;
  msg.at_time = env_.clock().CurrentTime();
  msg.constraints =
      ConvertConstraints(bitrate_configurator_.GetConfig(), &env_.clock());
  msg.restart_bwe = restart_bwe;
  if (controller_) {
    PostUpdates(controller_->OnNetworkRouteChange(msg));
  } else {
    UpdateInitialConstraints(msg.constraints);
  }

  // The new path may preserve the ECT(1) marking even if the old one did not.
  ect1_policy_.OnNetworkRouteChanged();
  packet_router_.SetSendPacketsAsEct1(ect1_policy_.ShouldSendEct1());
}

void RtpTransportControllerSend::OnNetworkAvailability(bool network_available) {
  RTC_DCHECK_RUN_ON(network_thread_);
  RTC_LOG(LS_VERBOSE) << "SignalNetworkState "
                      << (network_available ? "Up" : "Down");
  network_available_ = network_available;
  if (network_available) {
    pacer_.Resume();
  } else {
    pacer_.Pause();
  }
  is_congested_ = false;
  pacer_.SetCongested(false);

  if (!controller_) {
    MaybeCreateControllers();
  }
  UpdateNetworkAvailability();
  for (auto& rtp_sender : video_rtp_senders_) {
    rtp_sender->OnNetworkAvailability(network_available);
  }
}
NetworkLinkRtcpObserver* RtpTransportControllerSend::GetRtcpObserver() {
  return this;
}
int64_t RtpTransportControllerSend::GetPacerQueuingDelayMs() const {
  return pacer_.OldestPacketWaitTime().ms();
}
std::optional<Timestamp> RtpTransportControllerSend::GetFirstPacketTime()
    const {
  return pacer_.FirstSentPacketTime();
}
void RtpTransportControllerSend::EnablePeriodicAlrProbing(bool enable) {
  RTC_DCHECK_RUN_ON(network_thread_);

  streams_config_.requests_alr_probing = enable;
  UpdateStreamsConfig();
}
void RtpTransportControllerSend::OnSentPacket(
    const SentPacketInfo& sent_packet) {
  RTC_DCHECK_RUN_ON(network_thread_);
  ProcessSentPacket(sent_packet);
}

void RtpTransportControllerSend::ProcessSentPacket(
    const SentPacketInfo& sent_packet) {
  RTC_DCHECK_RUN_ON(network_thread_);
  std::optional<SentPacket> packet_msg =
      transport_feedback_adapter_.ProcessSentPacket(sent_packet);
  if (!packet_msg)
    return;

  auto congestion_update = GetCongestedStateUpdate();
  NetworkControlUpdate control_update;
  if (controller_)
    control_update = controller_->OnSentPacket(*packet_msg);
  if (!congestion_update && !control_update.has_updates())
    return;
  ProcessSentPacketUpdates(std::move(control_update));
}

// RTC_RUN_ON(network_thread_)
void RtpTransportControllerSend::ProcessSentPacketUpdates(
    NetworkControlUpdate updates) {
  RTC_DCHECK_RUN_ON(network_thread_);
  // Only update outstanding data if:
  // 1. Packet feedback is used.
  // 2. The packet has not yet received an acknowledgement.
  // 3. It is not a retransmission of an earlier packet.
  UpdateCongestedState();
  if (controller_) {
    PostUpdates(std::move(updates));
  }
}

void RtpTransportControllerSend::OnReceivedPacket(
    const ReceivedPacket& packet_msg) {
  RTC_DCHECK_RUN_ON(network_thread_);
  if (controller_)
    PostUpdates(controller_->OnReceivedPacket(packet_msg));
}

void RtpTransportControllerSend::UpdateBitrateConstraints(
    const BitrateConstraints& updated) {
  RTC_DCHECK_RUN_ON(network_thread_);
  TargetRateConstraints msg = ConvertConstraints(updated, &env_.clock());
  if (controller_) {
    PostUpdates(controller_->OnTargetRateConstraints(msg));
  } else {
    UpdateInitialConstraints(msg);
  }
}

void RtpTransportControllerSend::SetSdpBitrateParameters(
    const BitrateConstraints& constraints) {
  RTC_DCHECK_RUN_ON(network_thread_);
  std::optional<BitrateConstraints> updated =
      bitrate_configurator_.UpdateWithSdpParameters(constraints);
  if (updated.has_value()) {
    UpdateBitrateConstraints(*updated);
  } else {
    RTC_LOG(LS_VERBOSE)
        << "WebRTC.RtpTransportControllerSend.SetSdpBitrateParameters: "
           "nothing to update";
  }
}

void RtpTransportControllerSend::SetClientBitratePreferences(
    const BitrateSettings& preferences) {
  RTC_DCHECK_RUN_ON(network_thread_);
  std::optional<BitrateConstraints> updated =
      bitrate_configurator_.UpdateWithClientPreferences(preferences);
  if (updated.has_value()) {
    UpdateBitrateConstraints(*updated);
  } else {
    RTC_LOG(LS_VERBOSE)
        << "WebRTC.RtpTransportControllerSend.SetClientBitratePreferences: "
           "nothing to update";
  }
}

void RtpTransportControllerSend::AccountForAudioPacketsInPacedSender(
    bool account_for_audio) {
  pacer_.SetAccountForAudioPackets(account_for_audio);
}

void RtpTransportControllerSend::IncludeOverheadInPacedSender() {
  pacer_.SetIncludeOverhead();
}

void RtpTransportControllerSend::EnsureStarted() {
  RTC_DCHECK_RUN_ON(network_thread_);
  if (!pacer_started_) {
    packet_router_.RegisterNotifyBweCallback(
        [this](const RtpPacketToSend& packet,
               const PacedPacketInfo& pacing_info) {
          return NotifyBweOfPacedSentPacket(packet, pacing_info);
        });
    pacer_.EnsureStarted();
    pacer_started_ = true;
  }
}

void RtpTransportControllerSend::OnReceiverEstimatedMaxBitrate(
    Timestamp receive_time,
    DataRate bitrate) {
  RTC_DCHECK_RUN_ON(network_thread_);
  RemoteBitrateReport msg;
  msg.receive_time = receive_time;
  msg.bandwidth = bitrate;
  if (controller_)
    PostUpdates(controller_->OnRemoteBitrateReport(msg));
}

void RtpTransportControllerSend::OnRttUpdate(Timestamp receive_time,
                                             TimeDelta rtt) {
  RTC_DCHECK_RUN_ON(network_thread_);
  RoundTripTimeUpdate report;
  report.receive_time = receive_time;
  report.round_trip_time = rtt.RoundTo(TimeDelta::Millis(1));
  report.smoothed = false;
  if (controller_ && !report.round_trip_time.IsZero())
    PostUpdates(controller_->OnRoundTripTimeUpdate(report));
}

void RtpTransportControllerSend::NotifyBweOfPacedSentPacket(
    const RtpPacketToSend& packet,
    const PacedPacketInfo& pacing_info) {
  RTC_DCHECK_RUN_ON(network_thread_);

  if (!packet.transport_sequence_number()) {
    return;
  }
  if (!packet.packet_type()) {
    RTC_DCHECK_NOTREACHED() << "Unknown packet type";
    return;
  }
  Timestamp creation_time = env_.clock().CurrentTime();
  transport_feedback_adapter_.AddPacket(packet, pacing_info,
                                        transport_overhead_per_packet_.bytes(),
                                        creation_time);

  ect1_policy_.OnPacketSent(creation_time, packet.send_as_ect1());
  packet_router_.SetSendPacketsAsEct1(ect1_policy_.ShouldSendEct1());
}

void RtpTransportControllerSend::SetPreferredRtcpCcAckType(
    RtcpFeedbackType preferred_rtcp_cc_ack_type) {
  RTC_DCHECK_RUN_ON(network_thread_);
  RTC_DCHECK(preferred_rtcp_cc_ack_type == RtcpFeedbackType::CCFB ||
             preferred_rtcp_cc_ack_type == RtcpFeedbackType::TRANSPORT_CC);
  rfc_8888_feedback_negotiated_ =
      preferred_rtcp_cc_ack_type == RtcpFeedbackType::CCFB;
  RTC_LOG_F(LS_INFO) << "Assume "
                     << (rfc_8888_feedback_negotiated_ ? "RFC 8888" : "TWCC")
                     << " feedback.";
  packet_router_.SetGenerateTransportSequenceNumbers(
      rfc_8888_feedback_negotiated_);
  ect1_policy_.SetFeedbackSupportsEcn(rfc_8888_feedback_negotiated_);
  packet_router_.SetSendPacketsAsEct1(ect1_policy_.ShouldSendEct1());
  // TODO: bugs.webrtc.org/447037083 - Remove method
  // IncludeOverheadInPacedSender once once support for
  // RFC8888 is per default enabled. Also remove or update and SetPacingFactor
  // since it is not used with RFC 8888. SetPreferredRtcpCcAckType is only
  // called if field trial "WebRTC-RFC8888CongestionControlFeedback" is enabled.
  pacer_.SetIncludeOverhead();
}

std::optional<int>
RtpTransportControllerSend::ReceivedCongestionControlFeedbackCount() const {
  RTC_DCHECK_RUN_ON(network_thread_);
  if (!rfc_8888_feedback_negotiated_) {
    return std::nullopt;
  }
  return feedback_count_;
}

flat_map<uint32_t, ReceivedCongestionControlFeedbackStats>
RtpTransportControllerSend::GetCongestionControlFeedbackStatsPerSsrc() const {
  RTC_DCHECK_RUN_ON(network_thread_);
  return received_ccfb_stats_;
}

std::optional<int>
RtpTransportControllerSend::ReceivedTransportCcFeedbackCount() const {
  RTC_DCHECK_RUN_ON(network_thread_);
  if (rfc_8888_feedback_negotiated_) {
    return std::nullopt;
  }
  return transport_cc_feedback_count_;
}

DataSize RtpTransportControllerSend::GetTransportOverhead() const {
  RTC_DCHECK_RUN_ON(network_thread_);
  return transport_overhead_per_packet_;
}

void RtpTransportControllerSend::OnTransportFeedback(
    Timestamp receive_time,
    const rtcp::TransportFeedback& feedback) {
  RTC_DCHECK_RUN_ON(network_thread_);
  ++transport_cc_feedback_count_;
  std::optional<TransportPacketsFeedback> feedback_msg =
      transport_feedback_adapter_.ProcessTransportFeedback(feedback,
                                                           receive_time);
  if (feedback_msg) {
    HandleTransportPacketsFeedback(*feedback_msg);
  }
}

void RtpTransportControllerSend::OnCongestionControlFeedback(
    Timestamp receive_time,
    const rtcp::CongestionControlFeedback& feedback) {
  RTC_DCHECK_RUN_ON(network_thread_);
  ++feedback_count_;
  std::optional<TransportPacketsFeedback> feedback_msg =
      transport_feedback_adapter_.ProcessCongestionControlFeedback(
          feedback, receive_time);
  if (feedback_msg) {
    ComputeStatsFromCongestionControlFeedback(*feedback_msg);
    HandleTransportPacketsFeedback(*feedback_msg);
  }
}

void RtpTransportControllerSend::ComputeStatsFromCongestionControlFeedback(
    const TransportPacketsFeedback& feedback) {
  std::optional<uint32_t> last_ssrc;
  ReceivedCongestionControlFeedbackStats* stats = nullptr;
  for (const PacketResult& packet_info : feedback.packet_feedbacks) {
    if (!packet_info.rtp_packet_info.has_value()) {
      continue;
    }

    // Most of the time ssrc doesn't change across packets, so reuse last
    // map lookup when ssrc is the same. Initially last_ssrc is nullopt,
    // so first check would always trigger map lookup, thus `stats` would always
    // be nonnull after this block.
    if (uint32_t ssrc = packet_info.rtp_packet_info->ssrc; ssrc != last_ssrc) {
      last_ssrc = ssrc;
      stats = &received_ccfb_stats_[ssrc];
    }

    if (packet_info.reported_lost_for_the_first_time) {
      RTC_DCHECK(!packet_info.IsReceived());
      ++stats->num_packets_reported_as_lost;
    }

    if (packet_info.reported_recovered_for_the_first_time) {
      RTC_DCHECK(packet_info.IsReceived());
      ++stats->num_packets_reported_as_lost_but_recovered;
    }

    if (packet_info.IsReceived()) {
      switch (packet_info.ecn) {
        case EcnMarking::kEct1:
          ++stats->num_packets_received_with_ect1;
          break;
        case EcnMarking::kCe:
          ++stats->num_packets_received_with_ce;
          break;
        case EcnMarking::kNotEct:
          if (packet_info.sent_with_ect1) {
            ++stats->num_packets_with_bleached_ect1_marking;
          }
          break;
        case EcnMarking::kEct0:
          break;
      }
    }
  }
}

void RtpTransportControllerSend::HandleTransportPacketsFeedback(
    const TransportPacketsFeedback& feedback) {
  feedback_demuxer_.OnTransportFeedback(feedback);
  if (controller_) {
    PostUpdates(controller_->OnTransportPacketsFeedback(feedback));
  }
  ect1_policy_.OnPacketsFeedback(feedback.HasPacketWithBleachedEct1(),
                                 feedback.HasPacketWithEcn());
  packet_router_.SetSendPacketsAsEct1(ect1_policy_.ShouldSendEct1());

  // Only update outstanding data if any packet is first time acked.
  UpdateCongestedState();
}

void RtpTransportControllerSend::OnRemoteNetworkEstimate(
    NetworkStateEstimate estimate) {
  RTC_DCHECK_RUN_ON(network_thread_);
  estimate.update_time = env_.clock().CurrentTime();
  if (controller_)
    PostUpdates(controller_->OnNetworkStateEstimate(estimate));
}

void RtpTransportControllerSend::MaybeCreateControllers() {
  RTC_DCHECK(!controller_);
  RTC_DCHECK(!control_handler_);

  if (!network_available_ || !observer_)
    return;
  control_handler_ = std::make_unique<CongestionControlHandler>();

  initial_config_.constraints.at_time = env_.clock().CurrentTime();
  initial_config_.stream_based_config = streams_config_;

  if (controller_factory_override_) {
    RTC_LOG(LS_INFO) << "Creating overridden congestion controller";
    controller_ = controller_factory_override_->Create(initial_config_);
    process_interval_ = controller_factory_override_->GetProcessInterval();
  } else {
    RTC_LOG(LS_INFO) << "Creating Goog CC Factory.";
    GoogCcNetworkControllerFactory factory(GoogCcFactoryConfig(
        {.rfc_8888_feedback_negotiated = rfc_8888_feedback_negotiated_}));
    controller_ = factory.Create(initial_config_);
    process_interval_ = factory.GetProcessInterval();
  }
  ect1_policy_.SetCongestionControllerSupportsEcn(
      controller_->SupportsEcnAdaptation());
  packet_router_.SetSendPacketsAsEct1(ect1_policy_.ShouldSendEct1());
  UpdateControllerWithTimeInterval();
  StartProcessPeriodicTasks();
}

void RtpTransportControllerSend::UpdateNetworkAvailability() {
  if (!controller_) {
    return;
  }
  NetworkAvailability msg;
  msg.at_time = env_.clock().CurrentTime();
  msg.network_available = network_available_;
  control_handler_->SetNetworkAvailability(network_available_);
  PostUpdates(controller_->OnNetworkAvailability(msg));
  UpdateControlState();
}

void RtpTransportControllerSend::UpdateInitialConstraints(
    TargetRateConstraints new_contraints) {
  if (!new_contraints.starting_rate)
    new_contraints.starting_rate = initial_config_.constraints.starting_rate;
  RTC_DCHECK(new_contraints.starting_rate);
  initial_config_.constraints = new_contraints;
}

void RtpTransportControllerSend::StartProcessPeriodicTasks() {
  RTC_DCHECK_RUN_ON(network_thread_);
  if (!pacer_queue_update_task_.Running()) {
    pacer_queue_update_task_ = RepeatingTaskHandle::DelayedStart(
        network_thread_, kPacerQueueUpdateInterval, [this]() {
          RTC_DCHECK_RUN_ON(network_thread_);
          TimeDelta expected_queue_time = pacer_.ExpectedQueueTime();
          control_handler_->SetPacerQueue(expected_queue_time);
          UpdateControlState();
          return kPacerQueueUpdateInterval;
        });
  }
  controller_task_.Stop();
  if (process_interval_.IsFinite()) {
    controller_task_ = RepeatingTaskHandle::DelayedStart(
        network_thread_, process_interval_, [this]() {
          RTC_DCHECK_RUN_ON(network_thread_);
          UpdateControllerWithTimeInterval();
          return process_interval_;
        });
  }
}

void RtpTransportControllerSend::UpdateControllerWithTimeInterval() {
  RTC_DCHECK(controller_);
  ProcessInterval msg;
  msg.at_time = env_.clock().CurrentTime();
  msg.pacer_queue = pacer_.QueueSizeData();
  PostUpdates(controller_->OnProcessInterval(msg));
}

void RtpTransportControllerSend::UpdateStreamsConfig() {
  streams_config_.at_time = env_.clock().CurrentTime();
  if (controller_)
    PostUpdates(controller_->OnStreamsConfig(streams_config_));
}

void RtpTransportControllerSend::PostUpdates(NetworkControlUpdate update) {
  if (update.congestion_window) {
    congestion_window_size_ = *update.congestion_window;
    UpdateCongestedState();
  }
  if (update.pacer_config) {
    pacer_.SetConfig(*update.pacer_config);
  }
  if (!update.probe_cluster_configs.empty()) {
    pacer_.CreateProbeClusters(std::move(update.probe_cluster_configs));
  }
  if (update.target_rate) {
    control_handler_->SetTargetRate(*update.target_rate);
    UpdateControlState();
  }
}

void RtpTransportControllerSend::OnReport(
    Timestamp receive_time,
    std::span<const ReportBlockData> report_blocks) {
  RTC_DCHECK_RUN_ON(network_thread_);
  if (report_blocks.empty())
    return;

  int total_packets_lost_delta = 0;
  int total_packets_delta = 0;

  // Compute the packet loss from all report blocks.
  for (const ReportBlockData& report_block : report_blocks) {
    auto [it, inserted] =
        last_report_blocks_.try_emplace(report_block.source_ssrc());
    LossReport& last_loss_report = it->second;
    if (!inserted) {
      total_packets_delta += report_block.extended_highest_sequence_number() -
                             last_loss_report.extended_highest_sequence_number;
      total_packets_lost_delta +=
          report_block.cumulative_lost() - last_loss_report.cumulative_lost;
    }
    last_loss_report.extended_highest_sequence_number =
        report_block.extended_highest_sequence_number();
    last_loss_report.cumulative_lost = report_block.cumulative_lost();
  }
  // Can only compute delta if there has been previous blocks to compare to. If
  // not, total_packets_delta will be unchanged and there's nothing more to do.
  if (!total_packets_delta)
    return;
  int packets_received_delta = total_packets_delta - total_packets_lost_delta;
  // To detect lost packets, at least one packet has to be received. This check
  // is needed to avoid bandwith detection update in
  // VideoSendStreamTest.SuspendBelowMinBitrate

  if (packets_received_delta < 1)
    return;
  TransportLossReport msg;
  msg.packets_lost_delta = total_packets_lost_delta;
  msg.packets_received_delta = packets_received_delta;
  msg.receive_time = receive_time;
  msg.start_time = last_report_block_time_;
  msg.end_time = receive_time;
  if (controller_)
    PostUpdates(controller_->OnTransportLossReport(msg));
  last_report_block_time_ = receive_time;
}

void RtpTransportControllerSend::NotifyBweOfSentPacketForTesting(
    const RtpPacketToSend& rtp_packet) {
  NotifyBweOfPacedSentPacket(rtp_packet, /*pacing_info=*/{});
  PacketInfo packet_info;
  packet_info.included_in_allocation = true;
  packet_info.included_in_feedback =
      rtp_packet.transport_sequence_number().has_value();
  OnSentPacket(SentPacketInfo(
      /*packet_id=*/rtp_packet.transport_sequence_number().value_or(-1),
      /*send_time_ms=*/env_.clock().CurrentTime().ms(),
      /*info=*/packet_info));
}

}  // namespace webrtc
