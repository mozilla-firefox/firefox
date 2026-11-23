/*
 *  Copyright 2025 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/congestion_controller/scream/delay_based_congestion_control.h"

#include <algorithm>

#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "modules/congestion_controller/scream/scream_feedback.h"
#include "modules/congestion_controller/scream/scream_v2_parameters.h"
#include "rtc_base/checks.h"

namespace webrtc {

DelayBasedCongestionControl::DelayBasedCongestionControl(
    ScreamV2Parameters params)
    : params_(params),
      base_delay_history_(params_.base_delay_window_length.Get()) {
  ResetQueueDelay();
}

void DelayBasedCongestionControl::Update(const ScreamFeedback& feedback,
                                         bool alr) {
  RTC_DCHECK(feedback.delay_metrics.has_value());
  const ScreamFeedback::DelayMetrics& delay = *feedback.delay_metrics;
  RTC_DCHECK(delay.min_one_way_delay.IsFinite());
  RTC_DCHECK(delay.max_one_way_delay.IsFinite());
  RTC_DCHECK(delay.rtt_sample.IsFinite());
  next_base_delay_ = std::min(next_base_delay_, delay.min_one_way_delay);
  UpdateSmoothedRtt(delay.rtt_sample, alr);

  TimeDelta min_queue_delay = delay.min_one_way_delay - min_base_delay();
  TimeDelta max_queue_delay = delay.max_one_way_delay - min_base_delay();
  if (min_queue_delay > params_.queue_delay_drain_threshold.Get()) {
    if (min_queue_delay_above_threshold_start_.IsInfinite()) {
      min_queue_delay_above_threshold_start_ = feedback.feedback_time;
    }
  } else {
    min_queue_delay_above_threshold_start_ = Timestamp::MinusInfinity();
  }
  UpdateQueueDelayAverage(std::min(min_queue_delay, last_queue_delay_sample_));
  last_queue_delay_sample_ = min_queue_delay;
  if (params_.reduce_jitter.Get()) {
    CalculateQueueDelayTrend(max_queue_delay);
  } else {
    UpdateQueueDelayMinAverage(min_queue_delay);
    UpdateLatencyDifferenceAverage(delay.max_one_way_delay -
                                   delay.min_one_way_delay);
  }

  if (feedback.feedback_time - last_base_delay_update_ >=
      params_.base_delay_history_update_interval.Get()) {
    base_delay_history_.Insert(next_base_delay_);
    last_base_delay_update_ = feedback.feedback_time;
    next_base_delay_ = TimeDelta::PlusInfinity();
  }
}

void DelayBasedCongestionControl::UpdateQueueDelayAverage(
    TimeDelta min_qdelay_in_feedback) {
  // `queue_delay_avg_` is updated with a slow attack,fast decay EWMA
  // filter.
  if (min_qdelay_in_feedback < queue_delay_avg_) {
    queue_delay_avg_ = min_qdelay_in_feedback;
  } else {
    queue_delay_avg_ =
        params_.queue_delay_avg_g.Get() * min_qdelay_in_feedback +
        (1.0 - params_.queue_delay_avg_g.Get()) * queue_delay_avg_;
  }
}

void DelayBasedCongestionControl::UpdateQueueDelayMinAverage(
    TimeDelta packet_qdelay) {
  RTC_DCHECK(packet_qdelay >= TimeDelta::Zero());
  queue_delay_min_avg_ =
      (1.0 - params_.delay_min_and_latency_diff_avg_g.Get()) *
          queue_delay_min_avg_ +
      params_.delay_min_and_latency_diff_avg_g.Get() *
          std::min(packet_qdelay, 2 * params_.queue_delay_min_threshold.Get());
}

void DelayBasedCongestionControl::UpdateLatencyDifferenceAverage(
    TimeDelta packet_latency_diff) {
  RTC_DCHECK(packet_latency_diff >= TimeDelta::Zero());
  latency_difference_avg_ =
      (1.0 - params_.delay_min_and_latency_diff_avg_g.Get()) *
          latency_difference_avg_ +
      params_.delay_min_and_latency_diff_avg_g.Get() *
          std::min(packet_latency_diff,
                   2 * params_.latency_diff_threshold.Get());
}

void DelayBasedCongestionControl::CalculateQueueDelayTrend(
    TimeDelta max_queue_delay) {
  // 4.2.1.4. Detecting Increased Queue Delay (`calculate_latency_diff_avg`).
  // Unlike the draft, `queue_delay_trend_` (`latency_diff_avg` in the draft) is
  // updated even before the first congestion event so that rising queue delay
  // during initial ramp-up is tracked and moderates reference window growth and
  // in-flight headroom before the first congestion event occurs.
  RTC_DCHECK(max_queue_delay >= TimeDelta::Zero());
  qdelay_short_avg_ =
      qdelay_short_avg_ * (1.0 - params_.queue_delay_short_avg_g.Get()) +
      max_queue_delay * params_.queue_delay_short_avg_g.Get();
  qdelay_long_avg_ =
      qdelay_long_avg_ * (1.0 - params_.queue_delay_long_avg_g.Get()) +
      max_queue_delay * params_.queue_delay_long_avg_g.Get();

  TimeDelta diff = std::clamp(qdelay_short_avg_ - qdelay_long_avg_ -
                                  params_.latency_diff_margin_1.Get(),
                              -params_.latency_diff_margin_1.Get(),
                              params_.latency_diff_margin_1.Get());
  if (diff > TimeDelta::Zero()) {
    // Attenuate the increase when `queue_delay_avg_` is below
    // `latency_diff_margin_2` to avoid over-sensitivity to small delay
    // variations when overall queue delay is low.
    double low_qdelay_scale =
        std::min(1.0, queue_delay_avg_ / params_.latency_diff_margin_2.Get());
    queue_delay_trend_ += params_.latency_diff_up_gain.Get() *
                          diff.seconds<double>() * low_qdelay_scale;
  } else {
    queue_delay_trend_ +=
        params_.latency_diff_down_gain.Get() * diff.seconds<double>();
  }
  queue_delay_trend_ = std::clamp(queue_delay_trend_, 0.0, 1.0);
}

void DelayBasedCongestionControl::UpdateSmoothedRtt(TimeDelta rtt_sample,
                                                    bool alr) {
  RTC_DCHECK(rtt_sample >= TimeDelta::Zero());
  if (last_smoothed_rtt_.IsZero()) {
    last_smoothed_rtt_ = rtt_sample;
  } else {
    double g = alr ? params_.smoothed_rtt_avg_in_alr_g.Get()
                   : params_.smoothed_rtt_avg_g.Get();
    last_smoothed_rtt_ = rtt_sample * g + last_smoothed_rtt_ * (1.0 - g);
  }
}

void DelayBasedCongestionControl::ResetQueueDelay() {
  last_base_delay_update_ = Timestamp::MinusInfinity();
  next_base_delay_ = TimeDelta::PlusInfinity();
  base_delay_history_.Reset();
  // Insert a start value to ensure GetMin returns a sensible value when empty.
  base_delay_history_.Insert(TimeDelta::PlusInfinity());

  min_queue_delay_above_threshold_start_ = Timestamp::MinusInfinity();
  queue_delay_avg_ = TimeDelta::PlusInfinity();
  queue_delay_min_avg_ = TimeDelta::Zero();
  latency_difference_avg_ = TimeDelta::Zero();
  qdelay_short_avg_ = TimeDelta::Zero();
  qdelay_long_avg_ = TimeDelta::Zero();
  queue_delay_trend_ = 0.0;
}

double
DelayBasedCongestionControl::ref_window_scale_factor_due_to_avg_min_delay(
    bool allow_zero) const {
  TimeDelta norm = params_.queue_delay_min_threshold.Get();
  // Reaches 0.1 at norm, and 1.0 at norm / 4
  return std::clamp(0.1 + 1.2 * (norm - queue_delay_min_avg_) / norm,
                    allow_zero ? 0.0 : 0.1, 1.0);
}

double
DelayBasedCongestionControl::ref_window_scale_factor_due_to_latency_difference()
    const {
  if (params_.reduce_jitter.Get()) {
    return 1.0 - queue_delay_trend_;
  }
  TimeDelta norm = params_.latency_diff_threshold.Get();
  // Reaches 0.1 at norm, and 1.0 at norm / 4
  return std::clamp(0.1 + 1.2 * (norm - latency_difference_avg_) / norm, 0.1,
                    1.0);
}



double DelayBasedCongestionControl::l4s_alpha_v() const {
  // 4.2.2.1
  double l4s_alpha_v =
      (queue_delay_avg_ - params_.queue_delay_target.Get() / 2) /
      (params_.queue_delay_target.Get() / 2);
  return std::clamp(l4s_alpha_v, 0.0, 1.0);
}

}  // namespace webrtc
