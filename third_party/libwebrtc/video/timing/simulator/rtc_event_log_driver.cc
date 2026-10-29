/*
 *  Copyright (c) 2025 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "video/timing/simulator/rtc_event_log_driver.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <utility>

#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/environment/environment_factory.h"
#include "api/environment/force_test_environment.h"
#include "api/field_trials.h"
#include "api/sequence_checker.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "logging/rtc_event_log/events/logged_rtp_rtcp.h"
#include "logging/rtc_event_log/events/rtc_event_video_receive_stream_config.h"
#include "logging/rtc_event_log/rtc_event_log_parser.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "test/time_controller/simulated_time_task_queue_controller.h"
#include "video/timing/simulator/rtp_packet_simulator.h"
#include "video/timing/simulator/rtt_simulator.h"

namespace webrtc::video_timing_simulator {
namespace {
// Creates FieldTrials that are acceptable both in unittests and in production.
// Note: This will not capture the command line --force_fieldtrials flag,
// as it bypasses the test environment check without merging the flag.
std::unique_ptr<FieldTrials> CreateFieldTrials(absl::string_view s) {
  AutoBypassTestEnvironmentCheck bypass;
  return std::make_unique<FieldTrials>(s, /*is_test=*/true);
}
}  // namespace

RtcEventLogDriver::RtcEventLogDriver(
    const Config& config,
    const ParsedRtcEventLog* absl_nonnull parsed_log,
    absl::string_view field_trials_string,
    RtcEventLogDriver::StreamInterfaceFactory stream_factory)
    : config_(config),
      time_controller_(Timestamp::Zero()),
      env_(CreateEnvironment(CreateFieldTrials(field_trials_string),
                             time_controller_.GetClock(),
                             time_controller_.GetTaskQueueFactory())),
      parsed_log_(*parsed_log),
      stream_factory_(std::move(stream_factory)),
      prev_log_timestamp_(std::nullopt),
      simulator_queue_(time_controller_.GetTaskQueueFactory()->CreateTaskQueue(
          "simulator_queue",
          TaskQueueFactory::Priority::kNormal)),
      packet_simulator_(env_),
      rtt_callback_adapter_(this) {
  RTC_DCHECK(stream_factory_) << "stream_factory must be provided";

  bool done = false;
  simulator_queue_->PostTask([this, &done]() {
    RTC_DCHECK_RUN_ON(simulator_queue_.get());
    rtt_simulator_ = std::make_unique<RttSimulator>(
        env_, simulator_queue_.get(), &rtt_callback_adapter_);
    done = true;
  });
  time_controller_.AdvanceTime(TimeDelta::Zero());
  RTC_DCHECK(done);

  // Config events.
  processor_.AddEvents(
      parsed_log_.video_recv_configs(),
      [this](const auto& config) { OnLoggedVideoRecvConfig(config); });

  // RTP video packet events (media + RTX).
  for (const auto& stream : parsed_log_.incoming_rtp_packets_by_ssrc()) {
    bool is_video = parsed_log_.GetMediaType(
                        stream.ssrc, PacketDirection::kIncomingPacket) ==
                    ParsedRtcEventLog::MediaType::VIDEO;
    if (!is_video) {
      continue;
    }
    processor_.AddEvents(stream.incoming_packets, [this](const auto& packet) {
      OnLoggedRtpPacketIncoming(packet);
    });
  }

  // RTCP packet events (outgoing).
  processor_.AddEvents(
      parsed_log_.sender_reports(PacketDirection::kOutgoingPacket),
      [this](const auto& packet) {
        OnLoggedRtcpPacketSenderReportOutgoing(packet);
      },
      PacketDirection::kOutgoingPacket);
  processor_.AddEvents(
      parsed_log_.extended_reports(PacketDirection::kOutgoingPacket),
      [this](const auto& packet) {
        OnLoggedRtcpPacketExtendedReportsOutgoing(packet);
      },
      PacketDirection::kOutgoingPacket);

  // RTCP packet events (incoming).
  processor_.AddEvents(
      parsed_log_.sender_reports(PacketDirection::kIncomingPacket),
      [this](const auto& packet) {
        OnLoggedRtcpPacketSenderReportIncoming(packet);
      },
      PacketDirection::kIncomingPacket);
  processor_.AddEvents(
      parsed_log_.receiver_reports(PacketDirection::kIncomingPacket),
      [this](const auto& packet) {
        OnLoggedRtcpPacketReceiverReportIncoming(packet);
      },
      PacketDirection::kIncomingPacket);
  processor_.AddEvents(
      parsed_log_.extended_reports(PacketDirection::kIncomingPacket),
      [this](const auto& packet) {
        OnLoggedRtcpPacketExtendedReportsIncoming(packet);
      },
      PacketDirection::kIncomingPacket);
}

RtcEventLogDriver::~RtcEventLogDriver() = default;

void RtcEventLogDriver::Simulate() {
  // Walk through events in timestamp order and call the registered handlers.
  processor_.ProcessEventsInOrder();

  // Attempt to get straggling frames out by advancing time a little bit after
  // the last logged event.
  time_controller_.AdvanceTime(kShutdownAdvanceTimeSlack);

  // Tear down on the queue.
  bool done = false;
  simulator_queue_->PostTask([this, &done]() {
    RTC_DCHECK_RUN_ON(simulator_queue_.get());
    TeardownOnQueue();
    done = true;
  });
  time_controller_.AdvanceTime(TimeDelta::Zero());
  RTC_DCHECK(done);
}

void RtcEventLogDriver::AdvanceTime(Timestamp log_timestamp) {
  if (!prev_log_timestamp_) {
    // For the first event, set the clock in absolute terms.
    prev_log_timestamp_ = log_timestamp;
    time_controller_.AdvanceTime(log_timestamp - env_.clock().CurrentTime());
    RTC_DCHECK_EQ(env_.clock().CurrentTime(), log_timestamp);
    return;
  }
  TimeDelta duration = log_timestamp - *prev_log_timestamp_;
  prev_log_timestamp_ = log_timestamp;
  if (duration < TimeDelta::Zero()) {
    RTC_LOG(LS_ERROR)
        << "Non-monotonic sequence of timestamps. Will not advance time."
        << " (simulated_ts=" << env_.clock().CurrentTime() << ")";
    return;
  }
  time_controller_.AdvanceTime(duration);
}

void RtcEventLogDriver::HandleEvent(Timestamp log_timestamp,
                                    absl::AnyInvocable<void() &&> handler) {
  // Execute all tasks scheduled before the new logged event.
  AdvanceTime(log_timestamp);

  bool done = false;
  simulator_queue_->PostTask([handler = std::move(handler), &done]() mutable {
    std::move(handler)();
    done = true;
  });

  // Execute the logged event itself.
  AdvanceTime(log_timestamp);
  RTC_DCHECK(done) << "Handler was not executed";
}

void RtcEventLogDriver::OnLoggedVideoRecvConfig(
    const webrtc::LoggedVideoRecvConfig& config) {
  uint32_t ssrc = config.config.remote_ssrc;
  uint32_t rtx_ssrc = config.config.rtx_ssrc;
  HandleEvent(config.log_time(), [this, ssrc, rtx_ssrc]() {
    RTC_DCHECK_RUN_ON(simulator_queue_.get());

    all_known_ssrcs_.insert(ssrc);
    all_known_ssrcs_.insert(rtx_ssrc);

    // Skip setting up stream if not included in the filter.
    if (!config_.ssrc_filter.empty() && !config_.ssrc_filter.contains(ssrc)) {
      RTC_LOG(LS_INFO) << "OnLoggedVideoRecvConfig being skipped for "
                       << "ssrc=" << ssrc << ", rtx_ssrc=" << rtx_ssrc
                       << " (simulated_ts=" << env_.clock().CurrentTime()
                       << ")";
      return;
    }

    RTC_LOG(LS_INFO) << "OnLoggedVideoRecvConfig for "
                     << "ssrc=" << ssrc << ", rtx_ssrc=" << rtx_ssrc
                     << " (simulated_ts=" << env_.clock().CurrentTime() << ")";
    if (auto it = streams_.find(ssrc); it != streams_.end()) {
      if (config_.reuse_streams) {
        // TODO: b/384950328 - Remove unused `reuse_streams` option.
        RTC_LOG(LS_WARNING)
            << "Stream for ssrc=" << ssrc << " already existed. Reusing it."
            << " (simulated_ts=" << env_.clock().CurrentTime() << ")";
        return;
      } else {
        RTC_LOG(LS_WARNING)
            << "Stream for ssrc=" << ssrc << " already existed. Overwriting it."
            << " (simulated_ts=" << env_.clock().CurrentTime() << ")";
        it->second->Close();
        absl::erase_if(receiving_streams_,
                       [old_stream = it->second.get()](const auto& kv) {
                         return kv.second == old_stream;
                       });
      }
    }
    std::unique_ptr<StreamInterface> stream =
        stream_factory_(env_, ssrc, rtx_ssrc);
    RTC_DCHECK(stream);
    // Keep track of the stream object for ownership purposes.
    streams_[ssrc] = std::move(stream);
    // Also map it by the two SSRCs, for packet demuxing purposes.
    receiving_streams_[ssrc] = streams_[ssrc].get();
    if (rtx_ssrc != 0) {
      receiving_streams_[rtx_ssrc] = streams_[ssrc].get();
    }
  });
}

void RtcEventLogDriver::OnLoggedRtpPacketIncoming(
    const webrtc::LoggedRtpPacketIncoming& packet) {
  HandleEvent(packet.log_time(), [this, &packet]() {
    RTC_DCHECK_RUN_ON(simulator_queue_.get());
    uint32_t ssrc = packet.rtp.header.ssrc;
    if (auto it = receiving_streams_.find(ssrc);
        it != receiving_streams_.end()) {
      RtpPacketSimulator::SimulatedPacket simulated_packet =
          packet_simulator_.SimulateRtpPacketReceived(packet.rtp);
      RTC_DCHECK_EQ(simulated_packet.rtp_packet.arrival_time(),
                    packet.log_time());
      RTC_DCHECK_EQ(env_.clock().CurrentTime(), packet.log_time());
      it->second->InsertSimulatedPacket(simulated_packet);
    } else if (!all_known_ssrcs_.contains(ssrc)) {
      RTC_LOG(LS_WARNING) << "Received packet for unknown ssrc=" << ssrc
                          << " (simulated_ts=" << env_.clock().CurrentTime()
                          << ")";
    }
  });
}

void RtcEventLogDriver::OnLoggedRtcpPacketSenderReportOutgoing(
    const LoggedRtcpPacketSenderReport& packet) {
  HandleEvent(packet.log_time(), [this, &packet]() {
    RTC_DCHECK_RUN_ON(simulator_queue_.get());
    rtt_simulator_->OnOutgoingSenderReport(packet);
  });
}

void RtcEventLogDriver::OnLoggedRtcpPacketExtendedReportsOutgoing(
    const LoggedRtcpPacketExtendedReports& packet) {
  HandleEvent(packet.log_time(), [this, &packet]() {
    RTC_DCHECK_RUN_ON(simulator_queue_.get());
    rtt_simulator_->OnOutgoingExtendedReports(packet);
  });
}

void RtcEventLogDriver::OnLoggedRtcpPacketSenderReportIncoming(
    const LoggedRtcpPacketSenderReport& packet) {
  HandleEvent(packet.log_time(), [this, &packet]() {
    RTC_DCHECK_RUN_ON(simulator_queue_.get());
    rtt_simulator_->OnIncomingSenderReport(packet);
  });
}

void RtcEventLogDriver::OnLoggedRtcpPacketReceiverReportIncoming(
    const LoggedRtcpPacketReceiverReport& packet) {
  HandleEvent(packet.log_time(), [this, &packet]() {
    RTC_DCHECK_RUN_ON(simulator_queue_.get());
    rtt_simulator_->OnIncomingReceiverReport(packet);
  });
}

void RtcEventLogDriver::OnLoggedRtcpPacketExtendedReportsIncoming(
    const LoggedRtcpPacketExtendedReports& packet) {
  HandleEvent(packet.log_time(), [this, &packet]() {
    RTC_DCHECK_RUN_ON(simulator_queue_.get());
    rtt_simulator_->OnIncomingExtendedReports(packet);
  });
}

void RtcEventLogDriver::UpdateMaxRtt(TimeDelta max_rtt) {
  RTC_DCHECK_RUN_ON(simulator_queue_.get());
  for (auto& stream : streams_) {
    stream.second->UpdateMaxRtt(max_rtt);
  }
}

void RtcEventLogDriver::TeardownOnQueue() {
  RTC_DCHECK_RUN_ON(simulator_queue_.get());
  for (auto& stream : streams_) {
    stream.second->Close();
  }
  all_known_ssrcs_.clear();
  receiving_streams_.clear();
  streams_.clear();
  rtt_simulator_.reset();
}

}  // namespace webrtc::video_timing_simulator
