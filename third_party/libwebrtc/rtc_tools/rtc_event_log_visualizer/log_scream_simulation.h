/*
 *  Copyright 2025 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef RTC_TOOLS_RTC_EVENT_LOG_VISUALIZER_LOG_SCREAM_SIMULATION_H_
#define RTC_TOOLS_RTC_EVENT_LOG_VISUALIZER_LOG_SCREAM_SIMULATION_H_

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

#include "api/candidate.h"
#include "api/environment/environment.h"
#include "api/transport/network_types.h"
#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "logging/rtc_event_log/events/logged_rtp_rtcp.h"
#include "logging/rtc_event_log/events/rtc_event_ice_candidate_pair_config.h"
#include "logging/rtc_event_log/rtc_event_log_parser.h"
#include "modules/congestion_controller/rtp/transport_feedback_adapter.h"
#include "modules/congestion_controller/scream/scream_v2.h"
#include "modules/congestion_controller/scream/scream_v2_parameters.h"
#include "rtc_base/bitrate_tracker.h"

namespace webrtc {

class LogScreamSimulation {
 public:
  // State of Scream at the time when a feedback report has been processed.
  struct State {
    enum SendWindowUsage {
      kBelowRefWindow,
      kAboveRefWindow,
      kAboveScreamMax,
    };
    Timestamp time;

    DataRate target_rate = DataRate::Zero();
    DataRate pacing_rate = DataRate::Zero();
    DataRate send_rate = DataRate::Zero();
    DataRate received_rate = DataRate::Zero();

    DataSize ref_window = DataSize::Zero();
    std::optional<DataSize> ref_window_i;
    DataSize max_allowed_ref_window = DataSize::Zero();
    DataSize max_data_in_flight = DataSize::Zero();
    bool is_application_limited = false;
    // Data in flight after last packet was sent before the state was captured.
    DataSize data_in_flight = DataSize::Zero();
    // How the send window have been utilized. Based on data in flight when the
    // last packet was sent before the state was captured.
    SendWindowUsage send_window_usage = kBelowRefWindow;

    TimeDelta smoothed_rtt = TimeDelta::Zero();
    TimeDelta queue_delay = TimeDelta::Zero();
    TimeDelta queue_delay_min_avg = TimeDelta::PlusInfinity();
    TimeDelta latency_difference_avg = TimeDelta::PlusInfinity();
    TimeDelta qdelay_short_avg = TimeDelta::PlusInfinity();
    TimeDelta qdelay_long_avg = TimeDelta::PlusInfinity();

    std::optional<double> ref_window_scale_factor_due_to_avg_min_delay;
    double ref_window_scale_factor_due_to_latency_difference = 0.0;
    double ref_window_scale_factor_close_to_ref_window_i = 0.0;
    double ref_window_combined_increase_scale_factor = 0.0;
    double l4s_alpha = 0.0;
    double l4s_alpha_v = 0.0;
    double loss_congestion_level = 0.0;
    int packets_lost_per_rtt = 0;
    int packets_recovered_per_rtt = 0;
    int ce_marked_per_rtt = 0;
    int packets_lost_per_feedback = 0;
    int packets_recovered_per_feedback = 0;
    int ce_marked_per_feedback = 0;
  };

  struct Config {
    TimeDelta rate_window = TimeDelta::Millis(100);
  };

  LogScreamSimulation(const Config& config, const Environment& env);
  ~LogScreamSimulation() = default;
  void ProcessEventsInLog(const ParsedRtcEventLog& parsed_log_);

  const std::vector<State>& updates() const { return state_; }

 private:
  void ProcessUntil(Timestamp to_time);
  void OnPacketSent(const LoggedPacketInfo& packet);
  void OnTransportFeedback(const LoggedRtcpPacketTransportFeedback& feedback);
  void OnCongestionControlFeedback(
      const LoggedRtcpCongestionControlFeedback& feedback);
  void OnIceConfig(const LoggedIceCandidatePairConfig& candidate);
  void UpdateFeedbackHistory(const TransportPacketsFeedback& msg);
  void LogState(Timestamp log_time);

  const Environment env_;
  const ScreamV2Parameters params_;
  std::optional<ScreamV2> scream_;

  Timestamp current_time_ = Timestamp::MinusInfinity();
  Timestamp last_process_ = Timestamp::MinusInfinity();
  Timestamp last_log_state_time_ = Timestamp::MinusInfinity();
  TransportFeedbackAdapter transport_feedback_;
  BitrateTracker send_rate_tracker_;
  State::SendWindowUsage send_window_usage_ = State::kBelowRefWindow;
  DataSize data_in_flight_ = DataSize::Zero();

  // With RFC 8888, transport sequence numbers are not stored per packet.
  // Instead, we generate one.
  int64_t next_ccfb_packet_id_ = 0;
  std::optional<IceCandidateType> local_candidate_type_;
  std::optional<IceCandidateType> remote_candidate_type_;

  struct FeedbackEvent {
    Timestamp time;
    int lost_count = 0;
    int recovered_count = 0;
    int ce_marked_count = 0;
  };
  std::deque<FeedbackEvent> feedback_history_;

  std::vector<State> state_;
};
}  // namespace webrtc

#endif  // RTC_TOOLS_RTC_EVENT_LOG_VISUALIZER_LOG_SCREAM_SIMULATION_H_
