/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef CALL_RTP_TRANSPORT_CONTROLLER_SEND_H_
#define CALL_RTP_TRANSPORT_CONTROLLER_SEND_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"
#include "api/environment/environment.h"
#include "api/fec_controller.h"
#include "api/frame_transformer_interface.h"
#include "api/rtp_parameters.h"
#include "api/scoped_refptr.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "api/transport/bandwidth_estimation_settings.h"
#include "api/transport/bitrate_settings.h"
#include "api/transport/network_control.h"
#include "api/transport/network_types.h"
#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "call/rtp_bitrate_configurator.h"
#include "call/rtp_config.h"
#include "call/rtp_transport_config.h"
#include "call/rtp_transport_controller_send_interface.h"
#include "call/rtp_video_sender.h"
#include "modules/congestion_controller/ect1_policy.h"
#include "modules/congestion_controller/rtp/congestion_controller_feedback_stats.h"
#include "modules/congestion_controller/rtp/control_handler.h"
#include "modules/congestion_controller/rtp/transport_feedback_adapter.h"
#include "modules/congestion_controller/rtp/transport_feedback_demuxer.h"
#include "modules/pacing/packet_router.h"
#include "modules/pacing/task_queue_paced_sender.h"
#include "modules/rtp_rtcp/include/report_block_data.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtcp_packet/congestion_control_feedback.h"
#include "rtc_base/containers/flat_map.h"
#include "rtc_base/network_route.h"
#include "rtc_base/task_utils/repeating_task.h"
#include "rtc_base/thread_annotations.h"

namespace webrtc {
class FrameEncryptorInterface;

class RtpTransportControllerSend final
    : public RtpTransportControllerSendInterface,
      public NetworkLinkRtcpObserver,
      public NetworkStateEstimateObserver {
 public:
  explicit RtpTransportControllerSend(const RtpTransportConfig& config);
  ~RtpTransportControllerSend() override;

  RtpTransportControllerSend(const RtpTransportControllerSend&) = delete;
  RtpTransportControllerSend& operator=(const RtpTransportControllerSend&) =
      delete;

  // TODO(tommi): Change to std::unique_ptr<>.
  RtpVideoSenderInterface* CreateRtpVideoSender(
      const std::map<uint32_t, RtpState>& suspended_ssrcs,
      const std::map<uint32_t, RtpPayloadState>&
          states,  // move states into RtpTransportControllerSend
      const RtpConfig& rtp_config,
      int rtcp_report_interval_ms,
      Transport* send_transport,
      const RtpSenderObservers& observers,
      std::unique_ptr<FecController> fec_controller,
      const RtpSenderFrameEncryptionConfig& frame_encryption_config,
      scoped_refptr<FrameTransformerInterface> frame_transformer) override;
  void DestroyRtpVideoSender(
      RtpVideoSenderInterface* rtp_video_sender) override;

  // Implements RtpTransportControllerSendInterface
  void RegisterSendingRtpStream(RtpRtcpInterface& rtp_module) override;
  void DeRegisterSendingRtpStream(RtpRtcpInterface& rtp_module) override;
  PacketRouter* packet_router() override;

  NetworkStateEstimateObserver* network_state_estimate_observer() override;
  RtpPacketSender* packet_sender() override;

  void SetAllocatedSendBitrateLimits(BitrateAllocationLimits limits) override;
  void ReconfigureBandwidthEstimation(
      const BandwidthEstimationSettings& settings) override;

  void SetPacingFactor(float pacing_factor) override;
  void SetQueueTimeLimit(int limit_ms) override;
  StreamFeedbackProvider* GetStreamFeedbackProvider() override;
  void RegisterTargetTransferRateObserver(
      TargetTransferRateObserver* observer) override;
  void OnNetworkRouteChanged(absl::string_view transport_name,
                             const NetworkRoute& network_route) override;
  void OnNetworkAvailability(bool network_available) override;
  NetworkLinkRtcpObserver* GetRtcpObserver() override;
  int64_t GetPacerQueuingDelayMs() const override;
  std::optional<Timestamp> GetFirstPacketTime() const override;
  void EnablePeriodicAlrProbing(bool enable) override;
  void OnSentPacket(const SentPacketInfo& sent_packet) override;
  void OnReceivedPacket(const ReceivedPacket& packet_msg) override;

  void SetSdpBitrateParameters(const BitrateConstraints& constraints) override;
  void SetClientBitratePreferences(const BitrateSettings& preferences) override;

  void AccountForAudioPacketsInPacedSender(bool account_for_audio) override;
  void IncludeOverheadInPacedSender() override;
  void EnsureStarted() override;

  // Implements NetworkLinkRtcpObserver interface
  void OnReceiverEstimatedMaxBitrate(Timestamp receive_time,
                                     DataRate bitrate) override;
  void OnReport(Timestamp receive_time,
                std::span<const ReportBlockData> report_blocks) override;
  void OnRttUpdate(Timestamp receive_time, TimeDelta rtt) override;
  void OnTransportFeedback(Timestamp receive_time,
                           const rtcp::TransportFeedback& feedback) override;
  void OnCongestionControlFeedback(
      Timestamp receive_time,
      const rtcp::CongestionControlFeedback& feedback) override;

  // Implements NetworkStateEstimateObserver interface
  void OnRemoteNetworkEstimate(NetworkStateEstimate estimate) override;

  NetworkControllerInterface* GetNetworkController() override {
    RTC_DCHECK_RUN_ON(network_thread_);
    return controller_.get();
  }

  // Determines what type of RTCP feedback that should be used for congestion
  // control.
  void SetPreferredRtcpCcAckType(
      RtcpFeedbackType preferred_rtcp_cc_ack_type) override;

  std::optional<int> ReceivedCongestionControlFeedbackCount() const override;
  flat_map<uint32_t, ReceivedCongestionControlFeedbackStats>
  GetCongestionControlFeedbackStatsPerSsrc() const override;
  std::optional<int> ReceivedTransportCcFeedbackCount() const override;
  DataSize GetTransportOverhead() const override;

  // Mimics callbacks for packets sent through this transport.
  void NotifyBweOfSentPacketForTesting(const RtpPacketToSend& rtp_packet);

 private:
  void MaybeCreateControllers() RTC_RUN_ON(network_thread_);
  void HandleTransportPacketsFeedback(const TransportPacketsFeedback& feedback)
      RTC_RUN_ON(network_thread_);
  void ComputeStatsFromCongestionControlFeedback(
      const TransportPacketsFeedback& feedback) RTC_RUN_ON(network_thread_);
  void UpdateNetworkAvailability() RTC_RUN_ON(network_thread_);
  void UpdateInitialConstraints(TargetRateConstraints new_contraints)
      RTC_RUN_ON(network_thread_);

  void StartProcessPeriodicTasks() RTC_RUN_ON(network_thread_);
  void UpdateControllerWithTimeInterval() RTC_RUN_ON(network_thread_);

  bool IsRelevantRouteChange(const NetworkRoute& old_route,
                             const NetworkRoute& new_route) const;
  void UpdateBitrateConstraints(const BitrateConstraints& updated);
  void UpdateStreamsConfig() RTC_RUN_ON(network_thread_);
  void PostUpdates(NetworkControlUpdate update) RTC_RUN_ON(network_thread_);
  void UpdateControlState() RTC_RUN_ON(network_thread_);
  void UpdateCongestedState() RTC_RUN_ON(network_thread_);
  std::optional<bool> GetCongestedStateUpdate() const
      RTC_RUN_ON(network_thread_);

  // Called by packet router just before packet is sent to the RTP modules.
  void NotifyBweOfPacedSentPacket(const RtpPacketToSend& packet,
                                  const PacedPacketInfo& pacing_info);
  void ProcessSentPacket(const SentPacketInfo& sent_packet)
      RTC_RUN_ON(network_thread_);
  void ProcessSentPacketUpdates(NetworkControlUpdate updates)
      RTC_RUN_ON(network_thread_);

  const Environment env_;
  TaskQueueBase* const network_thread_;
  PacketRouter packet_router_;

  std::vector<std::unique_ptr<RtpVideoSenderInterface>> video_rtp_senders_
      RTC_GUARDED_BY(network_thread_);
  RtpBitrateConfigurator bitrate_configurator_;
  std::map<std::string, NetworkRoute> network_routes_
      RTC_GUARDED_BY(network_thread_);
  BandwidthEstimationSettings bwe_settings_ RTC_GUARDED_BY(network_thread_);
  bool pacer_started_ RTC_GUARDED_BY(network_thread_);
  TaskQueuePacedSender pacer_;

  TargetTransferRateObserver* observer_ RTC_GUARDED_BY(network_thread_);
  TransportFeedbackDemuxer feedback_demuxer_;

  TransportFeedbackAdapter transport_feedback_adapter_
      RTC_GUARDED_BY(network_thread_);

  NetworkControllerFactoryInterface* const controller_factory_override_
      RTC_PT_GUARDED_BY(network_thread_);

  std::unique_ptr<CongestionControlHandler> control_handler_
      RTC_GUARDED_BY(network_thread_) RTC_PT_GUARDED_BY(network_thread_);

  std::unique_ptr<NetworkControllerInterface> controller_
      RTC_GUARDED_BY(network_thread_) RTC_PT_GUARDED_BY(network_thread_);

  TimeDelta process_interval_ RTC_GUARDED_BY(network_thread_);

  struct LossReport {
    uint32_t extended_highest_sequence_number = 0;
    int cumulative_lost = 0;
  };
  std::map<uint32_t, LossReport> last_report_blocks_
      RTC_GUARDED_BY(network_thread_);
  flat_map<uint32_t, ReceivedCongestionControlFeedbackStats>
      received_ccfb_stats_ RTC_GUARDED_BY(network_thread_);
  Timestamp last_report_block_time_ RTC_GUARDED_BY(network_thread_);

  NetworkControllerConfig initial_config_ RTC_GUARDED_BY(network_thread_);
  StreamsConfig streams_config_ RTC_GUARDED_BY(network_thread_);

  const bool reset_bwe_on_adapter_id_change_;

  DataSize transport_overhead_per_packet_ RTC_GUARDED_BY(network_thread_) =
      DataSize::Zero();
  bool network_available_ RTC_GUARDED_BY(network_thread_);
  RepeatingTaskHandle pacer_queue_update_task_ RTC_GUARDED_BY(network_thread_);
  RepeatingTaskHandle controller_task_ RTC_GUARDED_BY(network_thread_);

  DataSize congestion_window_size_ RTC_GUARDED_BY(network_thread_);
  bool is_congested_ RTC_GUARDED_BY(network_thread_);
  bool rfc_8888_feedback_negotiated_ = false;
  Ect1Policy ect1_policy_ RTC_GUARDED_BY(network_thread_);
  // Count of feedback messages received.
  int feedback_count_ RTC_GUARDED_BY(network_thread_) = 0;
  int transport_cc_feedback_count_ RTC_GUARDED_BY(network_thread_) = 0;

  ScopedTaskSafety safety_;
};

}  // namespace webrtc

#endif  // CALL_RTP_TRANSPORT_CONTROLLER_SEND_H_
