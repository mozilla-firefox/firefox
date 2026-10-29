/*
 *  Copyright 2025 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/congestion_controller/scream/scream_v2_parameters.h"

#include "api/field_trials_view.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "rtc_base/experiments/field_trial_parser.h"

namespace webrtc {

ScreamV2Parameters::ScreamV2Parameters() : ScreamV2Parameters(nullptr) {}

ScreamV2Parameters::ScreamV2Parameters(const FieldTrialsView& trials)
    : ScreamV2Parameters(&trials) {}

ScreamV2Parameters::ScreamV2Parameters(const FieldTrialsView* trials)
    : min_ref_window("MinRefWindow", DataSize::Bytes(1000)),
      l4s_avg_g("L4sAvgG", 1.0 / 16.0),
      scale_l4s_alpha_with_rtt("ScaleL4sAlphaWithRtt", true),
      rtts_with_loss_before_backoff("RttsWithLossBeforeBackoff", 3),
      lossless_rtts_before_clear("LosslessRttsBeforeClear", 2),
      smoothed_rtt_avg_g("SmoothedRttAvgG", 1.0 / 8.0),
      smoothed_rtt_avg_in_alr_g("SmoothedRttAvgInAlrG", 1.0 / 128.0),
      max_segment_size("MaxSegmentSize", DataSize::Bytes(1280)),
      bytes_in_flight_head_room("BytesInFlightHeadRoom", 1.1),
      beta_loss("BetaLoss", 0.7),
      post_congestion_delay_rtts("PostCongestionDelayRtts", 100),
      multiplicative_increase_factor("MultiplicativeIncreaseFactor", 0.02),
      virtual_rtt("VirtualRtt", TimeDelta::Millis(25)),
      // backoff_scale_factor_close_to_ref_window_i is set lower than in the
      // rfc(8.0). This means that increase/decrease around ref_window_i is
      // slower in this implementation.
      backoff_scale_factor_close_to_ref_window_i(
          "BackoffScaleFactorCloseToRefWindowI",
          2.0),
      number_of_rtts_between_reset_ref_window_i_on_congestion(
          "NumberOfRttsBetweenResetRefWindowIOnCongestion",
          100),
      ref_window_overhead_min("RefWinMin", 1.5),
      ref_window_overhead_max("RefWinMax", 3.0),
      min_max_data_in_flight_mss("MinMaxDataInFlightMss", 4),
      queue_delay_avg_g("QDelayAvgG", 1.0 / 4.0),
      delay_min_and_latency_diff_avg_g("DelayMinAndLatencyDiffAvgG",
                                       1.0 / 32.0),
      queue_delay_min_threshold("QDelayMinThreshold", TimeDelta::Millis(10)),
      latency_diff_threshold("LatencyDiffThreshold", TimeDelta::Millis(10)),
      burst_window_min("BurstWindowMin", TimeDelta::Millis(25)),
      burst_window_max("BurstWindowMax", TimeDelta::Millis(50)),
      burst_window_max_gap("BurstWindowMaxGap", TimeDelta::Millis(3)),
      base_delay_window_length("BaseDelayWindowLength", 10),
      base_delay_history_update_interval("BaseDelayHistoryUpdateInterval",
                                         TimeDelta::Minutes(1)),
      queue_delay_target("QDelayTarget", TimeDelta::Millis(60)),
      queue_delay_drain_threshold("QDelayDrainThreshold", TimeDelta::Millis(5)),
      queue_delay_drain_period("QDelayDrainPeriod", TimeDelta::Seconds(20)),
      queue_delay_drain_rtts("QDelayDrainRtts", 5),
      time_between_periodic_padding("PeriodicPadding", TimeDelta::Seconds(3)),
      periodic_padding_duration("PaddingDuration", TimeDelta::Seconds(3)),
      allow_padding_after_last_congestion_time(
          "AllowPaddingAfterLastCongestionTimeout",
          TimeDelta::Seconds(1)),
      initial_probing_duration("InitialProbingDuration", TimeDelta::Seconds(6)),
      pacing_factor("PacingFactor", 1.1),
      pacing_rate_received_factor("PacingRateReceivedFactor", 0.8),
      feedback_hold_time_avg_g("FeedbackHoldTimeAvgG", 1.0 / 8.0),
      feedback_interval_avg_g("FeedbackIntervalAvgG", 0.1),
      use_feedback_interval_for_virtual_rtt("UseFeedbackIntervalForVirtualRtt",
                                            true),
      allow_initial_ref_window_clamping("AllowInitialRefWindowClamping", true),
      use_non_linear_backoff_scale("UseNonLinearBackoffScale", true),
      allow_large_pacing_bursts_after_congestion_time(
          "AllowLargePacingBurstsAfterCongestionTime",
          TimeDelta::Seconds(15)),
      enable_alr("EnableAlr", true),
      alr_threshold("AlrThreshold", 0.9),
      received_rate_window("ReceivedRateWindow", TimeDelta::Millis(100)),
      min_pacing_delay_for_pushback("MinPacingDelayForPushback",
                                    TimeDelta::Millis(100)),
      max_pacing_delay_for_pushback("MaxPacingDelayForPushback",
                                    TimeDelta::Millis(500)),
      safe_reset_on_route_change("SafeResetOnRouteChange", true) {
  ParseFieldTrial(
      {&min_ref_window,
       &l4s_avg_g,
       &scale_l4s_alpha_with_rtt,
       &rtts_with_loss_before_backoff,
       &lossless_rtts_before_clear,

       &smoothed_rtt_avg_g,
       &smoothed_rtt_avg_in_alr_g,
       &max_segment_size,
       &bytes_in_flight_head_room,
       &beta_loss,
       &post_congestion_delay_rtts,
       &multiplicative_increase_factor,
       &virtual_rtt,
       &backoff_scale_factor_close_to_ref_window_i,
       &number_of_rtts_between_reset_ref_window_i_on_congestion,
       &ref_window_overhead_min,
       &ref_window_overhead_max,
       &min_max_data_in_flight_mss,
       &queue_delay_avg_g,
       &delay_min_and_latency_diff_avg_g,
       &queue_delay_min_threshold,
       &latency_diff_threshold,
       &burst_window_min,
       &burst_window_max,
       &burst_window_max_gap,
       &base_delay_window_length,
       &base_delay_history_update_interval,
       &queue_delay_target,
       &queue_delay_drain_threshold,
       &queue_delay_drain_period,
       &queue_delay_drain_rtts,
       &time_between_periodic_padding,
       &periodic_padding_duration,
       &allow_padding_after_last_congestion_time,
       &initial_probing_duration,
       &pacing_factor,
       &pacing_rate_received_factor,
       &feedback_hold_time_avg_g,
       &feedback_interval_avg_g,
       &use_feedback_interval_for_virtual_rtt,
       &allow_initial_ref_window_clamping,
       &use_non_linear_backoff_scale,
       &allow_large_pacing_bursts_after_congestion_time,
       &enable_alr,
       &alr_threshold,
       &received_rate_window,
       &min_pacing_delay_for_pushback,
       &max_pacing_delay_for_pushback,
       &safe_reset_on_route_change},
      trials != nullptr ? trials->Lookup("WebRTC-Bwe-ScreamV2") : "");
}

}  // namespace webrtc
