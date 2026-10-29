/*
 *  Copyright 2025 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef MODULES_CONGESTION_CONTROLLER_SCREAM_SCREAM_V2_PARAMETERS_H_
#define MODULES_CONGESTION_CONTROLLER_SCREAM_SCREAM_V2_PARAMETERS_H_

#include "api/field_trials_view.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "rtc_base/experiments/field_trial_parser.h"

namespace webrtc {

struct ScreamV2Parameters {
  ScreamV2Parameters();
  explicit ScreamV2Parameters(const FieldTrialsView& trials);
  explicit ScreamV2Parameters(const FieldTrialsView* trials);
  ScreamV2Parameters(const ScreamV2Parameters& params) = default;

  // Minimum Reference Window
  FieldTrialParameter<DataSize> min_ref_window;

  // Exponentially Weighted Moving Average (EWMA) factor for l4s_alpha.
  FieldTrialParameter<double> l4s_avg_g;
  // If true, scales `l4s_avg_g` by `steps_per_rtt` so `l4s_alpha` is updated
  // relative to `max(reaction_interval, rtt)`.
  FieldTrialParameter<bool> scale_l4s_alpha_with_rtt;

  // The number of consecutive RTTs with loss required to trigger a backoff.
  // Used as the step-up value (1.0 / rtts_with_loss_before_backoff) in the
  // short-term congestion level step filter.
  FieldTrialParameter<int> rtts_with_loss_before_backoff;

  // The number of consecutive lossless RTTs required to fully clear the
  // congestion level from maximum congestion (1.0) back to 0.0.
  // Used as the step-down value (1.0 / lossless_rtts_before_clear) in the
  // short-term congestion level step filter.
  FieldTrialParameter<int> lossless_rtts_before_clear;

  // Exponentially Weighted Moving Average (EWMA) factor for smoothed rtt.
  FieldTrialParameter<double> smoothed_rtt_avg_g;
  FieldTrialParameter<double> smoothed_rtt_avg_in_alr_g;

  // Maximum Segment Size (MSS)
  // Size of the largest data segment that a sender is able to transmit. I.e
  // largest possible IP packet.
  FieldTrialParameter<DataSize> max_segment_size;

  // Headroom for bytes in flight when increasing reference window.
  FieldTrialParameter<double> bytes_in_flight_head_room;

  // Reference window scale factor due to loss event.
  FieldTrialParameter<double> beta_loss;

  // Determines how many RTTs after a congestion event the reference window
  // growth should be cautious.
  FieldTrialParameter<int> post_congestion_delay_rtts;

  // Determines how much (as a fraction of ref_window) that the ref_window can
  // increase per RTT.
  FieldTrialParameter<double> multiplicative_increase_factor;

  // This mimics Prague's RTT fairness such that flows with RTT below
  // virtual_rtt should get a roughly equal share over an L4S path.
  FieldTrialParameter<TimeDelta> virtual_rtt;

  // Increase and decrease of ref window is slower close to the last
  // inflection point. Both increase and decrease is scaled by
  // (backoff_scale_factor_close_to_ref_window_i* (ref_window_i -
  // ref_window)) / ref_window_i) ^2
  FieldTrialParameter<double> backoff_scale_factor_close_to_ref_window_i;

  // If CE is detected and this number of RTTs has passed since last
  // congestion, ref_window_i will be reset.
  FieldTrialParameter<int>
      number_of_rtts_between_reset_ref_window_i_on_congestion;

  // Max/Min used for calculating how much larger the send window is allowed to
  // be than the ref window.
  FieldTrialParameter<double> ref_window_overhead_min;
  FieldTrialParameter<double> ref_window_overhead_max;
  // Minimum send window (max_data_in_flight), expressed in number of MSS.
  FieldTrialParameter<int> min_max_data_in_flight_mss;

  // Exponentially Weighted Moving Average (EWMA) factor for updating queue
  // delay.
  FieldTrialParameter<double> queue_delay_avg_g;
  // Exponentially Weighted Moving Average (EWMA) factor for updating average
  // min queue delay and average latency difference.
  FieldTrialParameter<double> delay_min_and_latency_diff_avg_g;

  // Thresholds for average min queue delay. Used for limiting ref_window
  // increase.
  FieldTrialParameter<TimeDelta> queue_delay_min_threshold;
  // Thresholds for average latency difference. Used for limiting ref_window
  // increase.
  FieldTrialParameter<TimeDelta> latency_diff_threshold;
  // Minimum (base) window from the latest sent packet in feedback to compute
  // min and max one-way delay.
  FieldTrialParameter<TimeDelta> burst_window_min;
  // Maximum window from the latest sent packet in feedback to bound burst
  // continuation when consecutive packets are closely spaced.
  FieldTrialParameter<TimeDelta> burst_window_max;
  // Maximum send time gap between consecutive packets to extend the burst
  // window beyond `burst_window_min` up to `burst_window_max`.
  FieldTrialParameter<TimeDelta> burst_window_max_gap;

  // Determines the length of the base delay history when estimating one way
  // delay (owd)
  FieldTrialParameter<int> base_delay_window_length;
  // Determines how often the base delay history is updated.
  FieldTrialParameter<TimeDelta> base_delay_history_update_interval;

  // Reference window is reduced if average queue delay is above
  // `queue_delay_target/2`
  // TODO: bugs.webrtc.org/447037083 -  Consider implementing 4.2.1.4.1.
  // Competing Flows Compensation.
  FieldTrialParameter<TimeDelta> queue_delay_target;

  // If the minimum queue delay is below this threshold, queues are deamed to be
  // drained.
  FieldTrialParameter<TimeDelta> queue_delay_drain_threshold;
  // If the minimum queue delay has been above `queue_delay_drain_threshold` for
  // longer than `queue_delay_drain_period`, an attempt it made to drain the
  // queues, and if that fails, resets the estimates.
  FieldTrialParameter<TimeDelta> queue_delay_drain_period;
  // Number of RTTs where the target rate is reduced to attempt to drain.
  FieldTrialParameter<int> queue_delay_drain_rtts;

  // Padding is periodically used in order to increase target rate even if a
  // stream does not produce a high enough rate. Time between periodic padding
  // is at least this long.
  FieldTrialParameter<TimeDelta> time_between_periodic_padding;
  // Max duration padding is used when periodic padding start.
  // Padding is stopped if congestion occur.
  FieldTrialParameter<TimeDelta> periodic_padding_duration;
  // Padding is allowed to be used after this duration since the last
  // time reference window was reduced but at least `periodic_padding_interval`
  // must have passed since last time padding was used.
  FieldTrialParameter<TimeDelta> allow_padding_after_last_congestion_time;
  // If initial BWE probing is allowed, allow periodic probing for this duration
  // even of no streams have been configured.
  FieldTrialParameter<TimeDelta> initial_probing_duration;

  // Factor multiplied by the current target rate to decide the pacing rate.
  FieldTrialParameter<double> pacing_factor;
  // Minimum pacing rate relative to received_rate.
  FieldTrialParameter<double> pacing_rate_received_factor;

  // Exponentially Weighted Moving Average (EWMA) factor for calculating average
  // time feedback is delayed by the receiver. I.e the time from a packet is
  // received until feedback is sent. If zero, this delay is ignored.
  FieldTrialParameter<double> feedback_hold_time_avg_g;

  // Exponentially Weighted Moving Average (EWMA) factor for tracking average
  // feedback interval.
  FieldTrialParameter<double> feedback_interval_avg_g;

  // If true, effective virtual_rtt is calculated as
  // std::max(virtual_rtt, feedback_interval). Otherwise, virtual_rtt is used.
  FieldTrialParameter<bool> use_feedback_interval_for_virtual_rtt;

  // If true, reference window is clamped to received rate on the first
  // congestion event after the first RTT.
  FieldTrialParameter<bool> allow_initial_ref_window_clamping;

  // If true, `ref_window_` is reduced immediately according to `alpha` and then
  // waits for one RTT after `data_in_flight` drops below `ref_window_` before
  // starting another full reduction, applying only positive `alpha` deltas in
  // the meantime.
  FieldTrialParameter<bool> use_non_linear_backoff_scale;

  // If the time since last reaction to congestion is larger than this, the
  // pacing window is increased. I.e. packets are allowed to be sent in larger
  // bursts.
  FieldTrialParameter<TimeDelta>
      allow_large_pacing_bursts_after_congestion_time;

  // Enable application-limited (ALR) state tracking.
  // In ALR, reference window can not increase, and RTT is updated slower.
  FieldTrialParameter<bool> enable_alr;
  // An application is deemed application-limited (ALR) if the reference window
  // exceeds the maximum allowed based on data in flight, and the received rate
  // is less than alr_threshold * target_rate.
  FieldTrialParameter<double> alr_threshold;

  // Window over which received rate is calculated.
  FieldTrialParameter<TimeDelta> received_rate_window;

  // Minimum pacing delay before starting cwnd pushback reduction.
  FieldTrialParameter<TimeDelta> min_pacing_delay_for_pushback;
  // Maximum pacing delay for full cwnd pushback reduction.
  FieldTrialParameter<TimeDelta> max_pacing_delay_for_pushback;

  // On route change with restart_bwe = true, cap the starting rate to
  // min(current_target_rate, starting_rate).
  FieldTrialParameter<bool> safe_reset_on_route_change;
};

}  // namespace webrtc
#endif  // MODULES_CONGESTION_CONTROLLER_SCREAM_SCREAM_V2_PARAMETERS_H_
