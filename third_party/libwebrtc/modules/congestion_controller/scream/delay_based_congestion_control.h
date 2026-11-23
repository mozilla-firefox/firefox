/*
 *  Copyright 2025 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef MODULES_CONGESTION_CONTROLLER_SCREAM_DELAY_BASED_CONGESTION_CONTROL_H_
#define MODULES_CONGESTION_CONTROLLER_SCREAM_DELAY_BASED_CONGESTION_CONTROL_H_

#include <algorithm>

#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "modules/congestion_controller/scream/scream_feedback.h"
#include "modules/congestion_controller/scream/scream_v2_parameters.h"
#include "rtc_base/numerics/windowed_min_filter.h"

namespace webrtc {

// Implements logic necessary for backing off if queue delay increase as
// proposed in
// https://datatracker.ietf.org/doc/draft-johansson-ccwg-rfc8298bis-screamv2/.
// The functionality is split out into a separate class since it may be possible
// to replace this logic with the trend line filter from Goog CC or a ML model.
class DelayBasedCongestionControl {
 public:
  explicit DelayBasedCongestionControl(ScreamV2Parameters params);

  void Update(const ScreamFeedback& feedback, bool alr);

  // Returns true if queue delay is detected and above a threshold
  // (`queue_delay_target * queue_delay_detected_factor`).
  bool IsQueueDelayDetected() const {
    return queue_delay_avg_.IsFinite() &&
           queue_delay_avg_ > params_.queue_delay_target.Get() *
                                  params_.queue_delay_detected_factor.Get();
  }

  // Returns false if the minimum queue delay has been above the drain threshold
  // for a prolonged time. This can happen if minimum possible latency has
  // increased, or queues has been filled for a longer period of time without
  // being drained.
  bool IsQueueDrainedInTime(Timestamp now) const {
    return min_queue_delay_above_threshold_start_.IsInfinite() ||
           (now - min_queue_delay_above_threshold_start_ <
            params_.queue_delay_drain_period.Get());
  }

  // Resets queue delay estimates to start values.
  void ResetQueueDelay();

  TimeDelta queue_delay() const { return queue_delay_avg_; }

  TimeDelta queue_delay_min_avg() const { return queue_delay_min_avg_; }
  TimeDelta latency_difference_avg() const { return latency_difference_avg_; }
  TimeDelta qdelay_short_avg() const { return qdelay_short_avg_; }
  TimeDelta qdelay_long_avg() const { return qdelay_long_avg_; }

  // Scale factor used for scaling reference window increase/decrease due to
  // minimum average queue delay per feedback. The scale factor is 0.1
  // if the delay is higher than the threshold, and increases linearly to 1.0 if
  // the delay is lower than the threshold / 4.
  double ref_window_scale_factor_due_to_avg_min_delay(
      bool allow_zero = false) const;

  // Scale factor used for scaling reference window increase if
  // latency differences per feedback is larger than the threshold. If the send
  // rate is close to link capacity, the difference between min and max latency
  // per feedback increases and the scale factor decreases.
  double ref_window_scale_factor_due_to_latency_difference() const;

  TimeDelta rtt() const { return last_smoothed_rtt_; }

  double l4s_alpha_v() const;

 private:
  TimeDelta min_base_delay() const {
    return std::min(next_base_delay_, base_delay_history_.GetMin());
  }
  void UpdateSmoothedRtt(TimeDelta rtt_sample, bool alr);
  void UpdateQueueDelayAverage(TimeDelta one_way_delay);
  void UpdateQueueDelayMinAverage(TimeDelta packet_qdelay);
  void UpdateLatencyDifferenceAverage(TimeDelta packet_latency_diff);
  void CalculateQueueDelayTrend(TimeDelta max_queue_delay);

  const ScreamV2Parameters params_;

  // For computing min one way delay and compensate for clock drift.
  // Based on https://datatracker.ietf.org/doc/html/rfc6817
  Timestamp last_base_delay_update_ = Timestamp::MinusInfinity();
  TimeDelta next_base_delay_ = TimeDelta::PlusInfinity();
  WindowedMinFilter<TimeDelta> base_delay_history_;

  Timestamp min_queue_delay_above_threshold_start_ = Timestamp::MinusInfinity();
  TimeDelta last_smoothed_rtt_ = TimeDelta::Zero();

  TimeDelta last_queue_delay_sample_ = TimeDelta::PlusInfinity();
  TimeDelta queue_delay_avg_ = TimeDelta::PlusInfinity();
  TimeDelta queue_delay_min_avg_ = TimeDelta::Zero();
  TimeDelta latency_difference_avg_ = TimeDelta::Zero();
  TimeDelta qdelay_short_avg_ = TimeDelta::Zero();
  TimeDelta qdelay_long_avg_ = TimeDelta::Zero();
  // Normalized indicator in [0.0, 1.0] of how much the queue delay is
  // increasing (`latency_diff_avg` in the SCReAMv2 draft).
  double queue_delay_trend_ = 0.0;
};

}  // namespace webrtc
#endif  // MODULES_CONGESTION_CONTROLLER_SCREAM_DELAY_BASED_CONGESTION_CONTROL_H_
