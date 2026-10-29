/*
 *  Copyright 2025 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/congestion_controller/scream/scream_v2.h"

#include <algorithm>
#include <memory>

#include "api/environment/environment.h"
#include "api/transport/network_types.h"
#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "logging/rtc_event_log/events/rtc_event_bwe_update_scream.h"
#include "modules/congestion_controller/scream/delay_based_congestion_control.h"
#include "modules/congestion_controller/scream/scream_feedback.h"
#include "rtc_base/checks.h"
#include "rtc_base/experiments/field_trial_parser.h"
#include "rtc_base/logging.h"

namespace webrtc {

ScreamV2::ScreamV2(const Environment& env)
    : env_(env),
      params_(env_.field_trials()),
      ref_window_(params_.min_ref_window.Get()),
      loss_estimator_(params_),
      allow_initial_ref_window_clamping_(
          params_.allow_initial_ref_window_clamping.Get()),
      delay_based_congestion_control_(params_) {}

void ScreamV2::SetTargetBitrateConstraints(DataRate min,
                                           DataRate max,
                                           DataRate start) {
  RTC_DCHECK_GE(max, min);
  min_target_bitrate_ = min;
  max_target_bitrate_ = max;
  if (!first_feedback_processed_) {
    target_rate_ = start;
  }
  RTC_LOG_F(LS_VERBOSE) << "min_target_bitrate_=" << min_target_bitrate_
                        << " max_target_bitrate_=" << max_target_bitrate_
                        << " start_bitrate_=" << target_rate_;
}

void ScreamV2::OnPacketSent(DataSize data_in_flight) {
  max_data_in_flight_this_rtt_ =
      std::max(max_data_in_flight_this_rtt_, data_in_flight);
}

void ScreamV2::OnTransportPacketsFeedback(const TransportPacketsFeedback& msg) {
  ScreamFeedback feedback = ParseScreamFeedback(msg, params_);
  if (feedback.num_received_packets == 0) {
    RTC_LOG(LS_INFO) << "No received packets in feedback, ignoring.";
    return;
  }
  max_data_in_flight_this_rtt_ =
      std::max(max_data_in_flight_this_rtt_, feedback.data_in_flight);
  UpdateReceiveRate(feedback);

  if (params_.enable_alr.Get()) {
    is_application_limited_ =
        max_allowed_ref_window() < ref_window_ &&
        received_rate_ < params_.alr_threshold.Get() * target_rate_;
  }

  if (feedback.delay_metrics.has_value()) {
    delay_based_congestion_control_.Update(feedback, is_application_limited_);

    if (!is_application_limited_) {
      UpdateFeedbackHoldTime(feedback.delay_metrics->feedback_hold_time);
    }
  }

  if (!first_feedback_processed_) {
    ref_window_ =
        std::max(params_.min_ref_window.Get(),
                 target_rate_ * (delay_based_congestion_control_.rtt() +
                                 feedback_hold_time_));
    RTC_LOG(LS_INFO) << "Initial RTT: "
                     << delay_based_congestion_control_.rtt().ms()
                     << " feedback_hold_time: " << feedback_hold_time_.ms()
                     << "ms, Start Bitrate: " << target_rate_.kbps() << "kbps"
                     << " ref_window_=" << ref_window_.bytes();
    first_feedback_processed_ = true;
  }

  UpdateFeedbackInterval(feedback.feedback_time);
  UpdateL4SAlpha(feedback);
  UpdateRefWindow(feedback);
  UpdateTargetRate(feedback);
  env_.event_log().Log(std::make_unique<RtcEventBweUpdateScream>(
      ref_window_, feedback.data_in_flight, target_rate_,
      delay_based_congestion_control_.rtt(),
      delay_based_congestion_control_.queue_delay(),
      /*l4s_marked_permille*/ l4s_alpha_ * 1000));

  if (feedback.feedback_time - last_data_in_flight_update_ >=
      std::max(params_.virtual_rtt.Get(),
               delay_based_congestion_control_.rtt())) {
    last_data_in_flight_update_ = feedback.feedback_time;
    max_data_in_flight_prev_rtt_ = max_data_in_flight_this_rtt_;
    max_data_in_flight_this_rtt_ = DataSize::Zero();
  }
}

TimeDelta ScreamV2::ReactionInterval() const {
  return params_.use_feedback_interval_for_virtual_rtt.Get()
             ? std::max(params_.virtual_rtt.Get(), feedback_interval_)
             : params_.virtual_rtt.Get();
}

double ScreamV2::ScaleBackoff(double backoff) const {
  if (params_.use_spec_backoff.Get()) {
    // 4.5.3. Reference window undershoot at congestion.
    // Reduce backoff when target bitrate is lower than the acknowledged rate;
    // this may be an indication that the target rate is already reduced due to
    // an increased RTT.
    if (received_rate_.IsFinite() &&
        target_rate_ < params_.acked_bitrate_margin.Get() * received_rate_) {
      backoff *= params_.backoff_scale_low_target_rate.Get();
    }
  }
  return backoff / std::max(1.0, delay_based_congestion_control_.rtt() /
                                     ReactionInterval());
}

void ScreamV2::UpdateL4SAlpha(const ScreamFeedback& parsed) {
  // 4.2.1.3.
  if (parsed.num_received_packets == 0) {
    return;
  }

  double steps_per_rtt =
      params_.scale_l4s_alpha_with_rtt.Get()
          ? std::max(1.0,
                     delay_based_congestion_control_.rtt() / ReactionInterval())
          : 1.0;
  double fraction_marked = static_cast<double>(parsed.num_ce_marked_packets) /
                           parsed.num_received_packets;
  double g = params_.l4s_avg_g.Get() / steps_per_rtt;
  l4s_alpha_ =
      std::clamp(g * fraction_marked + (1.0 - g) * l4s_alpha_, 0.0, 1.0);
}
void ScreamV2::UpdateRefWindow(const ScreamFeedback& parsed) {
  bool is_ce = parsed.num_ce_marked_packets > 0;
  bool is_loss =
      loss_estimator_.Update(parsed, delay_based_congestion_control_.rtt());
  if (is_loss) {
    if (!loss_estimator_.congested() &&
        !delay_based_congestion_control_.IsQueueDelayDetected()) {
      is_loss = false;
    }
  }

  TimeDelta time_since_last_reaction =
      parsed.feedback_time - last_reaction_to_congestion_time_;
  TimeDelta min_reaction_interval =
      std::min(delay_based_congestion_control_.rtt(), ReactionInterval());

  bool is_virtual_ce = false;
  if (params_.use_spec_backoff.Get()) {
    if (time_since_last_reaction >= min_reaction_interval && !is_loss &&
        !is_ce && delay_based_congestion_control_.IsQueueDelayDetected() &&
        delay_based_congestion_control_.queue_delay() >=
            queue_delay_avg_prev_) {
      is_virtual_ce = true;
    }
    if (parsed.feedback_time - last_queue_delay_avg_check_time_ >=
        min_reaction_interval) {
      queue_delay_avg_prev_ = delay_based_congestion_control_.queue_delay();
      last_queue_delay_avg_check_time_ = parsed.feedback_time;
    }
  } else {
    is_virtual_ce = delay_based_congestion_control_.IsQueueDelayDetected();
  }

  DataSize previous_ref_window = ref_window_;

  if ((is_virtual_ce || is_ce || is_loss) &&
      time_since_last_reaction >= min_reaction_interval) {
    last_reaction_to_congestion_time_ = parsed.feedback_time;
    double backoff = 0.0;
    if (is_loss) {  // Back off due to loss
      backoff = 1.0 - params_.beta_loss.Get();
    } else if (is_ce) {  // Backoff due to ECN-CE marking
      backoff = ScaleBackoff(l4s_alpha_ / 2.0);

      if (!delay_based_congestion_control_.IsQueueDelayDetected()) {
        // Scale down backoff if close to the last known max reference window
        // This is complemented with a scale down of the reference window
        // increase
        backoff *=
            std::max(0.25, ref_window_scale_factor_close_to_ref_window_i());
        // Counterbalance the limitation in reference window increase when the
        // queue delay varies. This helps to avoid starvation in the presence
        // of competing TCP Prague flows.
        backoff *=
            std::max(0.1, delay_based_congestion_control_
                              .ref_window_scale_factor_due_to_avg_min_delay());
      }

      if (time_since_last_reaction.IsFinite() &&
          time_since_last_reaction >
              params_.number_of_rtts_between_reset_ref_window_i_on_congestion
                      .Get() *
                  std::max(params_.virtual_rtt.Get(),
                           delay_based_congestion_control_.rtt()) &&
          max_data_in_flight_prev_rtt_ > DataSize::Zero()) {
        // A long time(> 100 RTTs) since last congested because
        // link throughput exceeds max video bitrate.
        // There is a certain risk that ref_wnd has increased way above
        // bytes in flight, so we reduce it here to get it better on
        // track and thus the congestion episode is shortened.
        ref_window_ = std::clamp(max_data_in_flight_prev_rtt_,
                                 params_.min_ref_window.Get(), ref_window_);
      }
    } else if (is_virtual_ce) {  // Back off due to delay
      backoff =
          ScaleBackoff(delay_based_congestion_control_.l4s_alpha_v() / 2.0);
    }
    if (allow_initial_ref_window_clamping_ && received_rate_.IsFinite()) {
      // Clamp ref_window to received rate on the first congestion event,
      // to adjust if starting rate was much higher than link capacity.
      DataSize clamped_ref_window =
          std::clamp(received_rate_ * (delay_based_congestion_control_.rtt() +
                                       feedback_hold_time_),
                     params_.min_ref_window.Get(), ref_window_);
      backoff = std::max(backoff, 1.0 - (clamped_ref_window / ref_window_));
      allow_initial_ref_window_clamping_ = false;
    }
    ref_window_ = (1.0 - backoff) * ref_window_;
  }

  // Increase ref_window.
  // 4.2.2.2.  Reference Window Increase
  if ((!is_ce && loss_congestion_level() < 0.01 && !is_virtual_ce) ||
      last_reaction_to_congestion_time_ == parsed.feedback_time) {
    // Allow increase if no event has occurred, or we are at the same time
    // backing off.
    // Just because there is a CE event, does not mean we send too much. At
    // rates close to the capacity, it is quite likely that one packet is CE
    // marked in every feedback.
    double increase_scale_factor = ref_window_mss_ratio();

    // Limit increase for small RTTs
    if (delay_based_congestion_control_.rtt() + feedback_hold_time_ <
        params_.virtual_rtt.Get()) {
      double rtt_ratio =
          (delay_based_congestion_control_.rtt() + feedback_hold_time_) /
          params_.virtual_rtt.Get();
      increase_scale_factor = increase_scale_factor * rtt_ratio;
    }

    // Limit increase when close to the last inflection point.
    increase_scale_factor =
        increase_scale_factor *
        std::max(0.25, ref_window_scale_factor_close_to_ref_window_i());

    // Put a additional restriction on reference window growth if rtt varies a
    // lot.
    // Better to enforce a slow increase in reference window and get
    // a more stable bitrate.
    increase_scale_factor = increase_scale_factor *
                            delay_based_congestion_control_
                                .ref_window_scale_factor_due_to_avg_min_delay();
    increase_scale_factor =
        increase_scale_factor *
        delay_based_congestion_control_
            .ref_window_scale_factor_due_to_latency_difference();

    // Use lower multiplicative scale factor if congestion was detected
    // recently.
    const TimeDelta max_of_virtual_and_smothed_rtt = std::max(
        params_.virtual_rtt.Get(), delay_based_congestion_control_.rtt());
    double post_congestion_scale =
        std::clamp((parsed.feedback_time - last_reaction_to_congestion_time_) /
                       (params_.post_congestion_delay_rtts.Get() *
                        max_of_virtual_and_smothed_rtt),
                   0.0, 1.0);
    double multiplicative_scale =
        1.0 + (ref_window_multiplicative_scale_factor() - 1.0) *
                  post_congestion_scale;
    RTC_DCHECK_GE(multiplicative_scale, 1.0);
    increase_scale_factor = increase_scale_factor * multiplicative_scale;

    DataSize increase = parsed.acked_not_marked_size * increase_scale_factor;
    last_ref_window_increase_scale_factor_ = increase_scale_factor;
    DataSize max_ref_window = max_allowed_ref_window();
    if (ref_window_ < max_ref_window) {
      ref_window_ = std::clamp(ref_window_ + increase,
                               params_.min_ref_window.Get(), max_ref_window);
    }
  }

  if (previous_ref_window > ref_window_) {
    last_ref_window_decrease_time_ = parsed.feedback_time;
    // Allow update of `ref_window_i_` only if `ref_window_` before decrease
    // had increased sufficiently (>5%) above `ref_window_lowest_` or the hold
    // time has expired. This prevents `ref_window_i_` from being stepped down
    // during a period of congestion (4.2.2.1).
    const TimeDelta max_of_virtual_and_smoothed_rtt = std::max(
        params_.virtual_rtt.Get(), delay_based_congestion_control_.rtt());
    if (previous_ref_window > ref_window_lowest_ * 1.05 ||
        parsed.feedback_time - last_ref_window_i_update_time_ >
            params_.ref_window_i_update_max_hold_rtts.Get() *
                max_of_virtual_and_smoothed_rtt) {
      ref_window_i_ = previous_ref_window;
      last_ref_window_i_update_time_ = parsed.feedback_time;
    }
    ref_window_lowest_ = ref_window_;
  }

  RTC_LOG_IF(LS_VERBOSE, previous_ref_window != ref_window_)
      << "ScreamV2: "
      << ", ref_window = " << ref_window_ << " ref_window_i_=" << ref_window_i_
      << ", change=" << ref_window_.bytes() - previous_ref_window.bytes()
      << " bytes "
      << ", l4s_alpha=" << l4s_alpha_ << ", is_ce=" << is_ce
      << " is_virtual_ce=" << is_virtual_ce << " is_loss=" << is_loss
      << " smoothed_rtt=" << delay_based_congestion_control_.rtt().ms()
      << ", queue_delay=" << delay_based_congestion_control_.queue_delay().ms()
      << " feedback_hold" << feedback_hold_time_.ms()
      << ", target_rate =" << target_rate_.kbps();
}

DataSize ScreamV2::max_data_in_flight() const {
  // 4.3.1. Send Window Calculation
  double ref_window_overhead =
      params_.ref_window_overhead_min.Get() +
      (params_.ref_window_overhead_max.Get() -
       params_.ref_window_overhead_min.Get()) *
          delay_based_congestion_control_
              .ref_window_scale_factor_due_to_avg_min_delay(
                  /*allow_zero=*/true);

  return std::max(ref_window_ * ref_window_overhead,
                  params_.max_segment_size.Get() *
                      params_.min_max_data_in_flight_mss.Get());
}

DataSize ScreamV2::max_allowed_ref_window() const {
  // 4.2.2.2.
  // Increase ref_window only if bytes in flight is large enough.
  // Quite a lot of slack is allowed here to avoid that bitrate locks to low
  // values.
  return std::max(
      params_.max_segment_size.Get() +
          std::max(max_data_in_flight_this_rtt_, max_data_in_flight_prev_rtt_) *
              params_.bytes_in_flight_head_room.Get(),
      params_.min_ref_window.Get());
}

void ScreamV2::UpdateFeedbackHoldTime(TimeDelta feedback_hold_time) {
  if (feedback_hold_time_.IsZero() &&
      params_.feedback_hold_time_avg_g.Get() > 0.0) {
    feedback_hold_time_ = feedback_hold_time;
  }
  feedback_hold_time_ =
      feedback_hold_time * params_.feedback_hold_time_avg_g.Get() +
      (1.0 - params_.feedback_hold_time_avg_g.Get()) * feedback_hold_time_;
}

void ScreamV2::UpdateFeedbackInterval(Timestamp feedback_time) {
  if (last_feedback_time_.IsFinite()) {
    TimeDelta delta = feedback_time - last_feedback_time_;
    if (delta > TimeDelta::Zero()) {
      if (feedback_interval_.IsZero()) {
        feedback_interval_ = delta;
      } else {
        const double g = params_.feedback_interval_avg_g.Get();
        feedback_interval_ = delta * g + feedback_interval_ * (1.0 - g);
      }
    }
  }
  last_feedback_time_ = feedback_time;
}

void ScreamV2::UpdateTargetRate(const ScreamFeedback& parsed) {
  // Avoid division by zero.
  const TimeDelta non_zero_smoothed_rtt =
      std::max(delay_based_congestion_control_.rtt(), TimeDelta::Millis(1));
  double scale_target_rate = 1.0;
  // Scale down target rate slightly when the reference window is very small
  // compared to MSS
  scale_target_rate *=
      (1.0 - std::clamp(ref_window_mss_ratio() - 0.1, 0.0, 0.2));

  DataRate target_rate =
      scale_target_rate *
      (ref_window_ / (non_zero_smoothed_rtt + feedback_hold_time_));

  if (!delay_based_congestion_control_.IsQueueDrainedInTime(
          parsed.feedback_time)) {
    // If estimated min queue delay is too high for too long, target rate is
    // reduced for a period of time.
    // If the min queue delay is still too high, the queue delay estimate is
    // reset. This can happen if the one way delay increase for other reasons
    // than self congestion.
    if (drain_queue_start_.IsInfinite()) {
      drain_queue_start_ = parsed.feedback_time;
      RTC_LOG(LS_INFO) << "Reduce target rate to attempt to drain queue.";
    }
    if (parsed.feedback_time - drain_queue_start_ <
        std::max(TimeDelta::Millis(100), params_.queue_delay_drain_rtts.Get() *
                                             non_zero_smoothed_rtt)) {
      target_rate = 0.5 * target_rate;
    } else {
      RTC_LOG(LS_INFO) << "Reset queue delay estimate due to high queue delay.";
      delay_based_congestion_control_.ResetQueueDelay();
    }
  } else {
    drain_queue_start_ = Timestamp::MinusInfinity();
  }

  // TODO: bugs.webrtc.org/447037083 -  Consider implementing 4.4, compensation
  // for increased pacer delay.
  target_rate =
      std::clamp(target_rate, min_target_bitrate_, max_target_bitrate_);

  target_rate_ = target_rate;
}

void ScreamV2::UpdateReceiveRate(const ScreamFeedback& feedback) {
  accumulated_received_bytes_ += feedback.received;
  if (!feedback.delay_metrics.has_value()) {
    return;
  }
  const Timestamp last_packet_receive_time =
      feedback.delay_metrics->last_packet_receive_time;
  RTC_DCHECK(last_packet_receive_time.IsFinite());

  if (last_window_receive_time_.IsInfinite()) {
    // At the first feedback, set the received rate to infinite to ensure ALR
    // can not be entered until a valid receive rate estimate exists.
    last_window_receive_time_ = last_packet_receive_time;
    received_rate_ = DataRate::PlusInfinity();
    accumulated_received_bytes_ = DataSize::Zero();
  } else if (last_packet_receive_time - last_window_receive_time_ >=
             params_.received_rate_window.Get()) {
    TimeDelta duration = last_packet_receive_time - last_window_receive_time_;
    received_rate_ = accumulated_received_bytes_ / duration;
    accumulated_received_bytes_ = DataSize::Zero();
    last_window_receive_time_ = last_packet_receive_time;
  }
}

}  // namespace webrtc
