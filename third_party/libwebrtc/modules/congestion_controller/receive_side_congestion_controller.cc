/*
 *  Copyright (c) 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/congestion_controller/include/receive_side_congestion_controller.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include "api/environment/environment.h"
#include "api/media_types.h"
#include "api/rtp_parameters.h"
#include "api/sequence_checker.h"
#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "modules/congestion_controller/remb_throttler.h"
#include "modules/congestion_controller/rtp/congestion_controller_feedback_stats.h"
#include "modules/remote_bitrate_estimator/congestion_control_feedback_generator.h"
#include "modules/remote_bitrate_estimator/remote_bitrate_estimator_abs_send_time.h"
#include "modules/remote_bitrate_estimator/remote_bitrate_estimator_single_stream.h"
#include "modules/remote_bitrate_estimator/transport_sequence_number_feedback_generator.h"
#include "modules/rtp_rtcp/source/rtp_header_extensions.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "rtc_base/containers/flat_map.h"
#include "rtc_base/experiments/field_trial_parser.h"
#include "rtc_base/experiments/rfc8888_congestion_control_feedback_experiment.h"
#include "rtc_base/logging.h"
#include "rtc_base/synchronization/mutex.h"

namespace webrtc {

namespace {
const uint32_t kTimeOffsetSwitchThreshold = 30;
}  // namespace

void ReceiveSideCongestionController::OnRttUpdate(int64_t avg_rtt_ms,
                                                  int64_t max_rtt_ms) {
  MutexLock lock(&mutex_);
  rbe_->OnRttUpdate(avg_rtt_ms, max_rtt_ms);
}

void ReceiveSideCongestionController::RemoveStream(uint32_t ssrc) {
  MutexLock lock(&mutex_);
  rbe_->RemoveStream(ssrc);
}

DataRate ReceiveSideCongestionController::LatestReceiveSideEstimate() const {
  MutexLock lock(&mutex_);
  return rbe_->LatestEstimate();
}

void ReceiveSideCongestionController::PickEstimator(
    bool has_absolute_send_time) {
  if (has_absolute_send_time) {
    // If we see AST in header, switch RBE strategy immediately.
    if (!using_absolute_send_time_) {
      RTC_LOG(LS_INFO)
          << "WrappingBitrateEstimator: Switching to absolute send time RBE.";
      using_absolute_send_time_ = true;
      rbe_ = std::make_unique<RemoteBitrateEstimatorAbsSendTime>(
          env_, &remb_throttler_);
    }
    packets_since_absolute_send_time_ = 0;
  } else {
    // When we don't see AST, wait for a few packets before going back to TOF.
    if (using_absolute_send_time_) {
      ++packets_since_absolute_send_time_;
      if (packets_since_absolute_send_time_ >= kTimeOffsetSwitchThreshold) {
        RTC_LOG(LS_INFO)
            << "WrappingBitrateEstimator: Switching to transmission "
               "time offset RBE.";
        using_absolute_send_time_ = false;
        rbe_ = std::make_unique<RemoteBitrateEstimatorSingleStream>(
            env_, &remb_throttler_);
      }
    }
  }
}

ReceiveSideCongestionController::ReceiveSideCongestionController(
    const Environment& env,
    TransportSequenceNumberFeedbackGenenerator::RtcpSender feedback_sender,
    RembThrottler::RembSender remb_sender)
    : env_(env),
      remb_throttler_(std::move(remb_sender), &env_.clock()),
      transport_sequence_number_feedback_generator_(feedback_sender),
      congestion_control_feedback_generator_(env, feedback_sender),
      rbe_(std::make_unique<RemoteBitrateEstimatorSingleStream>(
          env_,
          &remb_throttler_)),
      using_absolute_send_time_(false),
      packets_since_absolute_send_time_(0) {
  FieldTrialParameter<bool> force_send_rfc8888_feedback("force_send", false);
  ParseFieldTrial(
      {&force_send_rfc8888_feedback},
      Rfc8888CongestionControlFeedbackFieldTrialString(env.field_trials()));
  if (force_send_rfc8888_feedback) {
    SetPreferredRtcpCcAckType(RtcpFeedbackType::CCFB);
  }
}

void ReceiveSideCongestionController::SetPreferredRtcpCcAckType(
    RtcpFeedbackType preferred_rtcp_cc_ack_type) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  send_rfc8888_congestion_feedback_ =
      (preferred_rtcp_cc_ack_type == RtcpFeedbackType::CCFB);
  RTC_LOG_F(LS_INFO) << " Sending "
                     << (send_rfc8888_congestion_feedback_ ? " RFC8888"
                                                           : " TWCC")
                     << " RTCP feedback.";
}

void ReceiveSideCongestionController::OnReceivedPacket(
    const RtpPacketReceived& packet,
    MediaType media_type) {
  bool has_transport_sequence_number =
      packet.HasExtension<TransportSequenceNumber>() ||
      packet.HasExtension<TransportSequenceNumberV2>();
  if (send_rfc8888_congestion_feedback_) {
    RTC_DCHECK_RUN_ON(&sequence_checker_);
    congestion_control_feedback_generator_.OnReceivedPacket(packet);
    return;
  }
  if (media_type == MediaType::AUDIO && !has_transport_sequence_number) {
    // For audio, we only support send side BWE.
    return;
  }

  if (has_transport_sequence_number) {
    // Send-side BWE.
    transport_sequence_number_feedback_generator_.OnReceivedPacket(packet);
  } else {
    // Receive-side BWE.
    MutexLock lock(&mutex_);
    PickEstimator(packet.HasExtension<AbsoluteSendTime>());
    rbe_->IncomingPacket(packet);
  }
}

flat_map<uint32_t, SentCongestionControllerFeedbackStats>
ReceiveSideCongestionController::GetCongestionControllerStatsPerSsrc() const {
  if (!send_rfc8888_congestion_feedback_) {
    return {};
  }
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  return congestion_control_feedback_generator_.GetStatsPerSsrc();
}

void ReceiveSideCongestionController::OnBitrateChanged(int bitrate_bps) {
  OnBitrateChanged(DataRate::BitsPerSec(bitrate_bps),
                   /*is_bandwidth_limited=*/true,
                   /*transport_overhead=*/std::nullopt);
}

void ReceiveSideCongestionController::OnBitrateChanged(
    DataRate target_rate,
    bool is_bandwidth_limited,
    std::optional<DataSize> transport_overhead) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  transport_sequence_number_feedback_generator_.OnSendBandwidthEstimateChanged(
      target_rate, is_bandwidth_limited, transport_overhead);
  congestion_control_feedback_generator_.OnSendBandwidthEstimateChanged(
      target_rate, is_bandwidth_limited, transport_overhead);
}

TimeDelta ReceiveSideCongestionController::MaybeProcess() {
  Timestamp now = env_.clock().CurrentTime();
  if (send_rfc8888_congestion_feedback_) {
    RTC_DCHECK_RUN_ON(&sequence_checker_);
    TimeDelta time_until_cc_rep =
        congestion_control_feedback_generator_.Process(now);
    TimeDelta time_until_rep =
        transport_sequence_number_feedback_generator_.Process(now);
    TimeDelta time_until = std::min(time_until_cc_rep, time_until_rep);
    return std::max(time_until, TimeDelta::Zero());
  }
  mutex_.Lock();
  TimeDelta time_until_rbe = rbe_->Process();
  mutex_.Unlock();
  TimeDelta time_until_rep =
      transport_sequence_number_feedback_generator_.Process(now);
  TimeDelta time_until = std::min(time_until_rbe, time_until_rep);
  return std::max(time_until, TimeDelta::Zero());
}

void ReceiveSideCongestionController::SetMaxDesiredReceiveBitrate(
    DataRate bitrate) {
  remb_throttler_.SetMaxDesiredReceiveBitrate(bitrate);
}

}  // namespace webrtc
