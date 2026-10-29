/*
 *  Copyright 2025 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_CONGESTION_CONTROLLER_SCREAM_SCREAM_V2_H_
#define MODULES_CONGESTION_CONTROLLER_SCREAM_SCREAM_V2_H_

#include <algorithm>

#include "api/environment/environment.h"
#include "api/transport/network_types.h"
#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "modules/congestion_controller/scream/delay_based_congestion_control.h"
#include "modules/congestion_controller/scream/loss_estimator.h"
#include "modules/congestion_controller/scream/scream_feedback.h"
#include "modules/congestion_controller/scream/scream_v2_parameters.h"

namespace webrtc {

// Implements ScreamV2 based on the draft RFC in
// https://datatracker.ietf.org/doc/draft-johansson-ccwg-rfc8298bis-screamv2/

// Note, this class is currently in development and not all features are yet
// implemented.
// TODO: bugs.webrtc.org/447037083 - revisit this comment when implementation is
// done.

class ScreamV2 {
 public:
  explicit ScreamV2(const Environment& env);
  ~ScreamV2() = default;

  void SetTargetBitrateConstraints(DataRate min, DataRate max, DataRate start);

  void OnPacketSent(DataSize data_in_flight);
  void OnTransportPacketsFeedback(const TransportPacketsFeedback& msg);

  DataRate target_rate() const {
    return std::min(max_target_bitrate_, target_rate_);
  }
  DataRate pacing_rate() const {
    if (received_rate_.IsFinite()) {
      return std::max(
          target_rate() * params_.pacing_factor.Get(),
          params_.pacing_rate_received_factor.Get() * received_rate_);
    }
    return target_rate() * params_.pacing_factor.Get();
  }

  TimeDelta rtt() const { return delay_based_congestion_control_.rtt(); }

  // Max data in flight before the send window is full.
  DataSize max_data_in_flight() const;

  // Target for the upper limit of the number of bytes that can be in
  // flight (transmitted but not yet acknowledged)
  DataSize ref_window() const { return ref_window_; }

  // Last inflection point where ref_window started to decrease.
  DataSize ref_window_i() const { return ref_window_i_; }

  // Returns the maximum allowed reference window based on data in flight during
  // the last RTT.
  DataSize max_allowed_ref_window() const;

  DataRate received_rate() const { return received_rate_; }

  // Returns the average fraction of ECN-CE marked data units per RTT.
  double l4s_alpha() const { return l4s_alpha_; }

  double loss_congestion_level() const {
    return loss_estimator_.congestion_level();
  }

  Timestamp last_reference_window_decrease_time() const {
    return last_ref_window_decrease_time_;
  }

  Timestamp last_reaction_to_congestion_time() const {
    return last_reaction_to_congestion_time_;
  }

  // Exposed for easier logging.
  const DelayBasedCongestionControl& delay_based_congestion_control() const {
    return delay_based_congestion_control_;
  }

  // Average time feedback is delayed in the receiver.
  TimeDelta feedback_hold_time() const { return feedback_hold_time_; }

  // Ratio between `max_segment_size` and `ref_window_`.
  double ref_window_mss_ratio() const {
    return std::min(1.0, params_.max_segment_size.Get() / ref_window_);
  }

  double last_ref_window_increase_scale_factor() const {
    return last_ref_window_increase_scale_factor_;
  }

  // Scaling factor for reference window adjustment
  // when close to the last known inflection point.
  // (4.2.2.1)
  double ref_window_scale_factor_close_to_ref_window_i() const {
    const double scale_factor =
        params_.backoff_scale_factor_close_to_ref_window_i.Get();
    double scl =
        ref_window_ > ref_window_i_
            ? scale_factor * (ref_window_ - ref_window_i_) / ref_window_i_
            : scale_factor * (ref_window_i_ - ref_window_) / ref_window_i_;
    return std::clamp(scl * scl, 0.1, 1.0);
  }

  // Scale factor for reference window increase. (4.2.2.2)
  // Always > 1.0.
  double ref_window_multiplicative_scale_factor() const {
    return 1.0 + (params_.multiplicative_increase_factor.Get() * ref_window_) /
                     params_.max_segment_size.Get();
  }

  bool is_application_limited() const { return is_application_limited_; }

 private:
  void UpdateL4SAlpha(const ScreamFeedback& parsed);
  void UpdateRefWindow(const ScreamFeedback& parsed);
  void UpdateFeedbackHoldTime(TimeDelta feedback_hold_time);
  void UpdateFeedbackInterval(Timestamp feedback_time);
  void UpdateTargetRate(const ScreamFeedback& parsed);
  void UpdateReceiveRate(const ScreamFeedback& parsed);
  // Minimum interval between congestion backoff reactions.
  TimeDelta ReactionInterval() const;
  // Scales `backoff` across the estimated number of reactions per RTT, and
  // further attenuates it when `target_rate_` is below `acked_bitrate_margin *
  // received_rate_` (Section 4.5.3).
  double ScaleBackoff(double backoff) const;

  const Environment env_;
  const ScreamV2Parameters params_;

  DataRate max_target_bitrate_ = DataRate::PlusInfinity();
  DataRate min_target_bitrate_ = DataRate::Zero();
  DataRate target_rate_ = DataRate::Zero();

  // Upper limit on the number of bytes that should be in
  // flight (transmitted but not yet acknowledged)
  DataSize ref_window_;
  // Reference window inflection point. I.e, `ref_window_` when congestion was
  // noticed. Increase and decrease of `ref_window_` is scaled down around
  // `ref_window_i_`.
  DataSize ref_window_i_ = DataSize::Bytes(1);
  // Lowest `ref_window_` observed on a reference window decrease
  // (`ref_wnd_lowest` in 4.2.2.1). `ref_window_i_` is only updated when
  // `ref_window_` before decrease has increased by >5% above
  // `ref_window_lowest_` or `ref_window_i_update_max_hold_rtts` has elapsed
  // since `last_ref_window_i_update_time_`.
  DataSize ref_window_lowest_ = DataSize::Zero();
  Timestamp last_ref_window_i_update_time_ = Timestamp::MinusInfinity();

  double last_ref_window_increase_scale_factor_ = 1.0;

  // `l4s_alpha_` tracks the average fraction of ECN-CE marked data units per
  // Round-Trip Time.
  double l4s_alpha_ = 0.0;
  Timestamp last_ce_mark_detected_time_ = Timestamp::MinusInfinity();

  LossEstimator loss_estimator_;

  TimeDelta feedback_hold_time_ = TimeDelta::Zero();
  Timestamp last_feedback_time_ = Timestamp::MinusInfinity();
  TimeDelta feedback_interval_ = TimeDelta::Zero();

  // Per-RTT stats
  Timestamp last_data_in_flight_update_ = Timestamp::MinusInfinity();
  DataSize max_data_in_flight_this_rtt_ = DataSize::Zero();
  DataSize max_data_in_flight_prev_rtt_ = DataSize::Zero();
  bool allow_initial_ref_window_clamping_ = true;
  DataRate received_rate_ = DataRate::Zero();
  DataSize accumulated_received_bytes_ = DataSize::Zero();
  Timestamp last_window_receive_time_ = Timestamp::MinusInfinity();

  // `last_reaction_to_congestion_time_` tracks the last received feedback that
  // contained a congestion event (`is_loss`, `is_ce`, or `is_virtual_ce`) that
  // caused a reaction (corresponding to `last_reaction_to_congestion_time` and
  // `last_congestion_detected_time` in 4.2.2).
  Timestamp last_reaction_to_congestion_time_ = Timestamp::MinusInfinity();
  // Last time the reference window decreased. This is not the same
  // as `last_reaction_to_congestion_time_` since a single CE mark does not
  // necessarily cause a reference window decrease.
  Timestamp last_ref_window_decrease_time_ = Timestamp::MinusInfinity();

  Timestamp drain_queue_start_ = Timestamp::MinusInfinity();
  TimeDelta queue_delay_avg_prev_ = TimeDelta::MinusInfinity();
  Timestamp last_queue_delay_avg_check_time_ = Timestamp::MinusInfinity();

  DelayBasedCongestionControl delay_based_congestion_control_;
  bool first_feedback_processed_ = false;

  // Tracks if the send rate is less than the network path can currently
  // support. This is done by checking if max_allowed_ref_window() <
  // ref_window_.
  bool is_application_limited_ = false;
};

}  // namespace webrtc
#endif  // MODULES_CONGESTION_CONTROLLER_SCREAM_SCREAM_V2_H_
