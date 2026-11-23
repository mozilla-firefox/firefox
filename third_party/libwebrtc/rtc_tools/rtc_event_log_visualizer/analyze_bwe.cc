/*
 *  Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_tools/rtc_event_log_visualizer/analyze_bwe.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/functional/bind_front.h"
#include "absl/strings/string_view.h"
#include "api/environment/environment.h"
#include "api/environment/environment_factory.h"
#include "api/field_trials.h"
#include "api/function_view.h"
#include "api/media_types.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_headers.h"
#include "api/transport/bandwidth_usage.h"
#include "api/transport/goog_cc_factory.h"
#include "api/transport/network_control.h"
#include "api/transport/network_types.h"
#include "api/units/data_rate.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "logging/rtc_event_log/events/logged_rtp_rtcp.h"
#include "logging/rtc_event_log/events/rtc_event_route_change.h"
#include "logging/rtc_event_log/rtc_event_log_parser.h"
#include "logging/rtc_event_log/rtc_event_processor.h"
#include "modules/congestion_controller/goog_cc/acknowledged_bitrate_estimator_interface.h"
#include "modules/congestion_controller/include/receive_side_congestion_controller.h"
#include "modules/congestion_controller/rtp/transport_feedback_adapter.h"
#include "modules/remote_bitrate_estimator/include/remote_bitrate_estimator.h"
#include "modules/rtp_rtcp/include/rtp_header_extension_map.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtcp_packet/remb.h"
#include "modules/rtp_rtcp/source/rtp_header_extensions.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "modules/rtp_rtcp/source/rtp_packet_to_send.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/numerics/sequence_number_unwrapper.h"
#include "rtc_base/rate_statistics.h"
#include "rtc_tools/rtc_event_log_visualizer/analyzer_common.h"
#include "rtc_tools/rtc_event_log_visualizer/log_scream_simulation.h"
#include "rtc_tools/rtc_event_log_visualizer/log_simulation.h"
#include "rtc_tools/rtc_event_log_visualizer/plot_base.h"
#include "system_wrappers/include/clock.h"

namespace webrtc {

namespace {

double AbsSendTimeToMicroseconds(int64_t abs_send_time) {
  // The timestamp is a fixed point representation with 6 bits for seconds
  // and 18 bits for fractions of a second. Thus, we divide by 2^18 to get the
  // time in seconds and then multiply by kNumMicrosecsPerSec to convert to
  // microseconds.
  static constexpr double kTimestampToMicroSec =
      static_cast<double>(kNumMicrosecsPerSec) / static_cast<double>(1ul << 18);
  return abs_send_time * kTimestampToMicroSec;
}

// This is much more reliable for outgoing streams than for incoming streams.
template <typename RtpPacketContainer>
std::optional<uint32_t> EstimateRtpClockFrequency(
    const RtpPacketContainer& packets,
    int64_t end_time_us) {
  RTC_CHECK(packets.size() >= 2);
  SeqNumUnwrapper<uint32_t> unwrapper;
  int64_t first_rtp_timestamp =
      unwrapper.Unwrap(packets[0].rtp.header.timestamp);
  int64_t first_log_timestamp = packets[0].log_time_us();
  int64_t last_rtp_timestamp = first_rtp_timestamp;
  int64_t last_log_timestamp = first_log_timestamp;
  for (size_t i = 1; i < packets.size(); i++) {
    if (packets[i].log_time_us() > end_time_us)
      break;
    last_rtp_timestamp = unwrapper.Unwrap(packets[i].rtp.header.timestamp);
    last_log_timestamp = packets[i].log_time_us();
  }
  if (last_log_timestamp - first_log_timestamp < kNumMicrosecsPerSec) {
    RTC_LOG(LS_WARNING)
        << "Failed to estimate RTP clock frequency: Stream too short. ("
        << packets.size() << " packets, "
        << last_log_timestamp - first_log_timestamp << " us)";
    return std::nullopt;
  }
  double duration =
      static_cast<double>(last_log_timestamp - first_log_timestamp) /
      kNumMicrosecsPerSec;
  double estimated_frequency =
      (last_rtp_timestamp - first_rtp_timestamp) / duration;
  for (uint32_t f : {8000, 16000, 32000, 48000, 90000}) {
    if (std::fabs(estimated_frequency - f) < 0.15 * f) {
      return f;
    }
  }
  RTC_LOG(LS_WARNING) << "Failed to estimate RTP clock frequency: Estimate "
                      << estimated_frequency
                      << " not close to any standard RTP frequency."
                      << " Last timestamp " << last_rtp_timestamp
                      << " first timestamp " << first_rtp_timestamp;
  return std::nullopt;
}

std::optional<double> NetworkDelayDiff_AbsSendTime(
    const LoggedRtpPacketIncoming& old_packet,
    const LoggedRtpPacketIncoming& new_packet) {
  if (old_packet.rtp.header.extension.hasAbsoluteSendTime &&
      new_packet.rtp.header.extension.hasAbsoluteSendTime) {
    int64_t send_time_diff = WrappingDifference(
        new_packet.rtp.header.extension.absoluteSendTime,
        old_packet.rtp.header.extension.absoluteSendTime, 1ul << 24);
    int64_t recv_time_diff =
        new_packet.log_time_us() - old_packet.log_time_us();
    double delay_change_us =
        recv_time_diff - AbsSendTimeToMicroseconds(send_time_diff);
    return delay_change_us / 1000;
  } else {
    return std::nullopt;
  }
}

std::optional<double> NetworkDelayDiff_CaptureTime(
    const LoggedRtpPacketIncoming& old_packet,
    const LoggedRtpPacketIncoming& new_packet,
    const double sample_rate) {
  int64_t send_time_diff =
      WrappingDifference(new_packet.rtp.header.timestamp,
                         old_packet.rtp.header.timestamp, 1ull << 32);
  int64_t recv_time_diff = new_packet.log_time_us() - old_packet.log_time_us();

  double delay_change =
      static_cast<double>(recv_time_diff) / 1000 -
      static_cast<double>(send_time_diff) / sample_rate * 1000;
  if (delay_change < -10000 || 10000 < delay_change) {
    RTC_LOG(LS_WARNING) << "Very large delay change. Timestamps correct?";
    RTC_LOG(LS_WARNING) << "Old capture time "
                        << old_packet.rtp.header.timestamp << ", received time "
                        << old_packet.log_time_us();
    RTC_LOG(LS_WARNING) << "New capture time "
                        << new_packet.rtp.header.timestamp << ", received time "
                        << new_packet.log_time_us();
    RTC_LOG(LS_WARNING) << "Receive time difference " << recv_time_diff << " = "
                        << static_cast<double>(recv_time_diff) /
                               kNumMicrosecsPerSec
                        << "s";
    RTC_LOG(LS_WARNING) << "Send time difference " << send_time_diff << " = "
                        << static_cast<double>(send_time_diff) / sample_rate
                        << "s";
  }
  return delay_change;
}

struct FakeExtensionSmall {
  static constexpr RTPExtensionType kId = kRtpExtensionMid;
  static constexpr absl::string_view Uri() { return "fake-extension-small"; }
};
struct FakeExtensionLarge {
  static constexpr RTPExtensionType kId = kRtpExtensionRtpStreamId;
  static constexpr absl::string_view Uri() { return "fake-extension-large"; }
};

RtpPacketReceived RtpPacketForBWEFromHeader(const RTPHeader& header) {
  RtpHeaderExtensionMap rtp_header_extensions(/*extmap_allow_mixed=*/true);
  // ReceiveSideCongestionController doesn't need to know extensions ids as
  // long as it able to get extensions by type. So any ids would work here.
  rtp_header_extensions.Register<TransmissionOffset>(RtpHeaderExtensionId(1));
  rtp_header_extensions.Register<AbsoluteSendTime>(RtpHeaderExtensionId(2));
  rtp_header_extensions.Register<TransportSequenceNumber>(
      RtpHeaderExtensionId(3));
  rtp_header_extensions.Register<FakeExtensionSmall>(RtpHeaderExtensionId(4));
  // Use id > 14 to force two byte header per rtp header when this one is used.
  rtp_header_extensions.Register<FakeExtensionLarge>(RtpHeaderExtensionId(16));

  RtpPacketReceived rtp_packet(&rtp_header_extensions);
  // Set only fields that might be relevant for the bandwidth estimatior.
  rtp_packet.SetSsrc(header.ssrc);
  rtp_packet.SetTimestamp(header.timestamp);
  size_t num_bwe_extensions = 0;
  if (header.extension.hasTransmissionTimeOffset) {
    rtp_packet.SetExtension<TransmissionOffset>(
        header.extension.transmissionTimeOffset);
    ++num_bwe_extensions;
  }
  if (header.extension.hasAbsoluteSendTime) {
    rtp_packet.SetExtension<AbsoluteSendTime>(
        header.extension.absoluteSendTime);
    ++num_bwe_extensions;
  }
  if (header.extension.hasTransportSequenceNumber) {
    rtp_packet.SetExtension<TransportSequenceNumber>(
        header.extension.transportSequenceNumber);
    ++num_bwe_extensions;
  }

  // All parts of the RTP header are 32bit aligned.
  RTC_CHECK_EQ(header.headerLength % 4, 0);

  // Original packet could have more extensions, there could be csrcs that are
  // not propagated by the rtc event log, i.e. logged header size might be
  // larger that rtp_packet.header_size(). Increase it by setting an extra fake
  // extension.
  RTC_CHECK_GE(header.headerLength, rtp_packet.headers_size());
  size_t bytes_to_add = header.headerLength - rtp_packet.headers_size();
  if (bytes_to_add > 0) {
    if (bytes_to_add <= 16) {
      // one-byte header rtp header extension allows to add up to 16 bytes.
      rtp_packet.AllocateExtension(FakeExtensionSmall::kId, bytes_to_add - 1);
    } else {
      // two-byte header rtp header extension would also add one byte per
      // already set extension.
      rtp_packet.AllocateExtension(FakeExtensionLarge::kId,
                                   bytes_to_add - 2 - num_bwe_extensions);
    }
  }
  RTC_CHECK_EQ(rtp_packet.headers_size(), header.headerLength);

  return rtp_packet;
}

const std::vector<LoggedRtpPacketIncoming>& GetPackets(
    const ParsedRtcEventLog::LoggedRtpStreamIncoming& stream) {
  return stream.incoming_packets;
}

const std::vector<LoggedRtpPacketOutgoing>& GetPackets(
    const ParsedRtcEventLog::LoggedRtpStreamOutgoing& stream) {
  return stream.outgoing_packets;
}

template <typename RtpStreamIterable, typename RtcpIterable>
void CreateTotalBitrateGraph(
    const RtpStreamIterable& rtp_streams,
    const RtcpIterable& rtcp_packets,
    const std::vector<LoggedRouteChangeEvent>& route_change_events,
    const AnalyzerConfig& config,
    bool include_overhead,
    Plot* plot) {
  struct PacketSizeAndType {
    Timestamp timestamp;
    size_t size;
    bool is_rtp;
  };
  std::vector<PacketSizeAndType> packets_in_order;
  uint32_t current_overhead = include_overhead ? 38 : 0;

  auto handle_rtp = [&](auto rtp_packet) {
    size_t packet_size = rtp_packet.rtp.total_length + current_overhead;
    packets_in_order.push_back(PacketSizeAndType{rtp_packet.rtp.log_time(),
                                                 packet_size, /*is_rtp=*/true});
  };

  auto handle_rtcp = [&](auto rtcp_packet) {
    size_t packet_size = rtcp_packet.rtcp.raw_data.size() + current_overhead;
    packets_in_order.push_back(PacketSizeAndType{
        rtcp_packet.log_time(), packet_size, /*is_rtp=*/false});
  };

  auto handle_route_change = [&](const LoggedRouteChangeEvent& route_change) {
    current_overhead = route_change.overhead;
  };

  webrtc::RtcEventProcessor event_processor;
  for (const auto& stream : rtp_streams) {
    event_processor.AddEvents(GetPackets(stream), handle_rtp);
  }
  event_processor.AddEvents(rtcp_packets, handle_rtcp);
  if (include_overhead) {
    event_processor.AddEvents(route_change_events, handle_route_change);
  }

  event_processor.ProcessEventsInOrder();

  auto window_begin = packets_in_order.begin();
  auto window_end = packets_in_order.begin();
  size_t rtp_bytes_in_window = 0;
  size_t rtcp_bytes_in_window = 0;

  if (!packets_in_order.empty()) {
    absl::string_view rtp_label =
        include_overhead ? "RTP Bitrate (including overhead)" : "RTP Bitrate";
    absl::string_view rtcp_label =
        include_overhead ? "RTCP Bitrate (including overhead)" : "RTCP Bitrate";
    TimeSeries bitrate_series(rtp_label, LineStyle::kLine);
    TimeSeries rtcp_bitrate_series(rtcp_label, LineStyle::kLine);
    for (Timestamp time = config.begin_time_;
         time < config.end_time_ + config.step_; time += config.step_) {
      while (window_end != packets_in_order.end() &&
             window_end->timestamp < time) {
        if (window_end->is_rtp) {
          rtp_bytes_in_window += window_end->size;
        } else {
          rtcp_bytes_in_window += window_end->size;
        }
        ++window_end;
      }
      while (window_begin != packets_in_order.end() &&
             window_begin->timestamp < time - config.window_duration_) {
        if (window_begin->is_rtp) {
          RTC_DCHECK_LE(window_begin->size, rtp_bytes_in_window);
          rtp_bytes_in_window -= window_begin->size;
        } else {
          RTC_DCHECK_LE(window_begin->size, rtcp_bytes_in_window);
          rtcp_bytes_in_window -= window_begin->size;
        }
        ++window_begin;
      }
      float window_duration_in_seconds =
          static_cast<float>(config.window_duration_.us()) /
          kNumMicrosecsPerSec;
      float x = config.GetCallTimeSec(time);
      float rtp_y = rtp_bytes_in_window * 8 / window_duration_in_seconds / 1000;
      bitrate_series.points.emplace_back(x, rtp_y);
      float rtcp_y =
          rtcp_bytes_in_window * 8 / window_duration_in_seconds / 1000;
      rtcp_bitrate_series.points.emplace_back(x, rtcp_y);
    }
    plot->AppendTimeSeries(std::move(bitrate_series));
    plot->AppendTimeSeries(std::move(rtcp_bitrate_series));
  }
}

}  // namespace

class BitrateObserver : public RemoteBitrateObserver {
 public:
  BitrateObserver() : last_bitrate_bps_(0), bitrate_updated_(false) {}

  void Update(NetworkControlUpdate update) {
    if (update.target_rate) {
      last_bitrate_bps_ = update.target_rate->target_rate.bps();
      bitrate_updated_ = true;
    }
  }

  void OnReceiveBitrateChanged(const std::vector<uint32_t>& ssrcs,
                               uint32_t bitrate) override {}

  uint32_t last_bitrate_bps() const { return last_bitrate_bps_; }
  bool GetAndResetBitrateUpdated() {
    bool bitrate_updated = bitrate_updated_;
    bitrate_updated_ = false;
    return bitrate_updated;
  }

 private:
  uint32_t last_bitrate_bps_;
  bool bitrate_updated_;
};

void CreateIncomingDelayGraph(const ParsedRtcEventLog& parsed_log,
                              const AnalyzerConfig& config,
                              Plot* plot) {
  for (const auto& stream : parsed_log.incoming_rtp_packets_by_ssrc()) {
    // Filter on SSRC.
    if (!MatchingSsrc(stream.ssrc, config.desired_ssrc_) ||
        IsRtxSsrc(parsed_log, kIncomingPacket, stream.ssrc)) {
      continue;
    }

    const std::vector<LoggedRtpPacketIncoming>& packets =
        stream.incoming_packets;
    if (packets.size() < 100) {
      RTC_LOG(LS_WARNING) << "Can't estimate the RTP clock frequency with "
                          << packets.size() << " packets in the stream.";
      continue;
    }
    int64_t segment_end_us = parsed_log.first_log_segment().stop_time_us();
    std::optional<uint32_t> estimated_frequency =
        EstimateRtpClockFrequency(packets, segment_end_us);
    if (!estimated_frequency)
      continue;
    const double frequency_hz = *estimated_frequency;
    if (IsVideoSsrc(parsed_log, kIncomingPacket, stream.ssrc) &&
        frequency_hz != 90000) {
      RTC_LOG(LS_WARNING)
          << "Video stream should use a 90 kHz clock but appears to use "
          << frequency_hz / 1000 << ". Discarding.";
      continue;
    }

    auto ToCallTime = [&config](const LoggedRtpPacketIncoming& packet) {
      return config.GetCallTimeSec(packet.log_time());
    };
    auto ToNetworkDelay = [frequency_hz](
                              const LoggedRtpPacketIncoming& old_packet,
                              const LoggedRtpPacketIncoming& new_packet) {
      return NetworkDelayDiff_CaptureTime(old_packet, new_packet, frequency_hz);
    };

    TimeSeries capture_time_data(
        GetStreamName(parsed_log, kIncomingPacket, stream.ssrc) +
            " capture-time",
        LineStyle::kLine);
    AccumulatePairs<LoggedRtpPacketIncoming, double>(
        ToCallTime, ToNetworkDelay, packets, &capture_time_data);
    plot->AppendTimeSeries(std::move(capture_time_data));

    TimeSeries send_time_data(
        GetStreamName(parsed_log, kIncomingPacket, stream.ssrc) +
            " abs-send-time",
        LineStyle::kLine);
    AccumulatePairs<LoggedRtpPacketIncoming, double>(
        ToCallTime, NetworkDelayDiff_AbsSendTime, packets, &send_time_data);
    plot->AppendTimeSeriesIfNotEmpty(std::move(send_time_data));
  }

  plot->SetXAxis(config.CallBeginTimeSec(), config.CallEndTimeSec(), "Time (s)",
                 kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 1, "Delay (ms)", kBottomMargin, kTopMargin);
  plot->SetTitle("Incoming network delay (relative to first packet)");
}

// Plot the fraction of packets lost (as perceived by the loss-based BWE).
void CreateFractionLossGraph(const ParsedRtcEventLog& parsed_log,
                             const AnalyzerConfig& config,
                             Plot* plot) {
  TimeSeries time_series("Fraction lost", LineStyle::kLine,
                         PointStyle::kHighlight);
  for (auto& bwe_update : parsed_log.bwe_loss_updates()) {
    float x = config.GetCallTimeSec(bwe_update.log_time());
    float y = static_cast<float>(bwe_update.fraction_lost) / 255 * 100;
    time_series.points.emplace_back(x, y);
  }

  plot->AppendTimeSeries(std::move(time_series));
  plot->SetXAxis(config.CallBeginTimeSec(), config.CallEndTimeSec(), "Time (s)",
                 kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 10, "Loss rate (in %)", kBottomMargin, kTopMargin);
  plot->SetTitle("Outgoing packet loss (as reported by BWE)");
}

void CreateTotalIncomingBitrateGraph(const ParsedRtcEventLog& parsed_log,
                                     const AnalyzerConfig& config,
                                     Plot* plot,
                                     bool include_overhead) {
  CreateTotalBitrateGraph(parsed_log.incoming_rtp_packets_by_ssrc(),
                          parsed_log.incoming_rtcp_packets(),
                          parsed_log.route_change_events(), config,
                          include_overhead, plot);

  // Overlay the outgoing REMB over incoming bitrate.
  TimeSeries remb_series("Remb", LineStyle::kStep);
  for (const auto& rtcp : parsed_log.rembs(kOutgoingPacket)) {
    float x = config.GetCallTimeSec(rtcp.log_time());
    float y = static_cast<float>(rtcp.remb.bitrate_bps()) / 1000;
    remb_series.points.emplace_back(x, y);
  }
  plot->AppendTimeSeriesIfNotEmpty(std::move(remb_series));

  plot->SetXAxis(config.CallBeginTimeSec(), config.CallEndTimeSec(), "Time (s)",
                 kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 1, "Bitrate (kbps)", kBottomMargin, kTopMargin);
  plot->SetTitle("Incoming RTP/RTCP bitrate");
}

void CreateTotalOutgoingBitrateGraph(const ParsedRtcEventLog& parsed_log,
                                     const AnalyzerConfig& config,
                                     Plot* plot,
                                     bool show_detector_state,
                                     bool show_alr_state,
                                     bool show_link_capacity,
                                     bool include_overhead) {
  CreateTotalBitrateGraph(parsed_log.outgoing_rtp_packets_by_ssrc(),
                          parsed_log.outgoing_rtcp_packets(),
                          parsed_log.route_change_events(), config,
                          include_overhead, plot);

  // Overlay the send-side bandwidth estimate over the outgoing bitrate.
  TimeSeries loss_series("Loss-based estimate", LineStyle::kStep);
  for (auto& loss_update : parsed_log.bwe_loss_updates()) {
    float x = config.GetCallTimeSec(loss_update.log_time());
    float y = static_cast<float>(loss_update.bitrate_bps) / 1000;
    loss_series.points.emplace_back(x, y);
  }

  TimeSeries link_capacity_lower_series("Link-capacity-lower",
                                        LineStyle::kStep);
  TimeSeries link_capacity_upper_series("Link-capacity-upper",
                                        LineStyle::kStep);
  for (auto& remote_estimate_event : parsed_log.remote_estimate_events()) {
    float x = config.GetCallTimeSec(remote_estimate_event.log_time());
    if (remote_estimate_event.link_capacity_lower.has_value()) {
      float link_capacity_lower = static_cast<float>(
          remote_estimate_event.link_capacity_lower.value().kbps());
      link_capacity_lower_series.points.emplace_back(x, link_capacity_lower);
    }
    if (remote_estimate_event.link_capacity_upper.has_value()) {
      float link_capacity_upper = static_cast<float>(
          remote_estimate_event.link_capacity_upper.value().kbps());
      link_capacity_upper_series.points.emplace_back(x, link_capacity_upper);
    }
  }

  TimeSeries delay_series("Delay-based estimate", LineStyle::kStep);
  IntervalSeries overusing_series("Overusing", "#ff8e82",
                                  IntervalSeries::kHorizontal);
  IntervalSeries underusing_series("Underusing", "#5092fc",
                                   IntervalSeries::kHorizontal);
  IntervalSeries normal_series("Normal", "#c4ffc4",
                               IntervalSeries::kHorizontal);
  IntervalSeries* last_series = &normal_series;
  float last_detector_switch = 0.0;

  BandwidthUsage last_detector_state = BandwidthUsage::kBwNormal;

  for (auto& delay_update : parsed_log.bwe_delay_updates()) {
    float x = config.GetCallTimeSec(delay_update.log_time());
    float y = static_cast<float>(delay_update.bitrate_bps) / 1000;

    if (last_detector_state != delay_update.detector_state) {
      last_series->intervals.emplace_back(last_detector_switch, x);
      last_detector_state = delay_update.detector_state;
      last_detector_switch = x;

      switch (delay_update.detector_state) {
        case BandwidthUsage::kBwNormal:
          last_series = &normal_series;
          break;
        case BandwidthUsage::kBwUnderusing:
          last_series = &underusing_series;
          break;
        case BandwidthUsage::kBwOverusing:
          last_series = &overusing_series;
          break;
        case BandwidthUsage::kLast:
          RTC_DCHECK_NOTREACHED();
      }
    }

    delay_series.points.emplace_back(x, y);
  }

  RTC_CHECK(last_series);
  last_series->intervals.emplace_back(last_detector_switch,
                                      config.CallEndTimeSec());

  TimeSeries scream_series("Scream target rate", LineStyle::kStep);
  for (auto& scream_update : parsed_log.bwe_scream_updates()) {
    float x = config.GetCallTimeSec(scream_update.log_time());
    float y = static_cast<float>(scream_update.target_rate.kbps());
    scream_series.points.emplace_back(x, y);
  }

  TimeSeries created_series("Probe cluster created.", LineStyle::kNone,
                            PointStyle::kHighlight);
  for (auto& cluster : parsed_log.bwe_probe_cluster_created_events()) {
    float x = config.GetCallTimeSec(cluster.log_time());
    float y = static_cast<float>(cluster.bitrate_bps) / 1000;
    created_series.points.emplace_back(x, y);
  }

  TimeSeries result_series("Probing results.", LineStyle::kNone,
                           PointStyle::kHighlight);
  for (auto& result : parsed_log.bwe_probe_success_events()) {
    float x = config.GetCallTimeSec(result.log_time());
    float y = static_cast<float>(result.bitrate_bps) / 1000;
    result_series.points.emplace_back(x, y);
  }

  TimeSeries probe_failures_series("Probe failed", LineStyle::kNone,
                                   PointStyle::kHighlight);
  for (auto& failure : parsed_log.bwe_probe_failure_events()) {
    float x = config.GetCallTimeSec(failure.log_time());
    probe_failures_series.points.emplace_back(x, 0);
  }

  IntervalSeries alr_state("ALR", "#555555", IntervalSeries::kHorizontal);
  bool previously_in_alr = false;
  Timestamp alr_start = Timestamp::Zero();
  for (auto& alr : parsed_log.alr_state_events()) {
    float y = config.GetCallTimeSec(alr.log_time());
    if (!previously_in_alr && alr.in_alr) {
      alr_start = alr.log_time();
      previously_in_alr = true;
    } else if (previously_in_alr && !alr.in_alr) {
      float x = config.GetCallTimeSec(alr_start);
      alr_state.intervals.emplace_back(x, y);
      previously_in_alr = false;
    }
  }

  if (previously_in_alr) {
    float x = config.GetCallTimeSec(alr_start);
    float y = config.GetCallTimeSec(config.end_time_);
    alr_state.intervals.emplace_back(x, y);
  }

  if (show_detector_state) {
    plot->AppendIntervalSeries(std::move(overusing_series));
    plot->AppendIntervalSeries(std::move(underusing_series));
    plot->AppendIntervalSeries(std::move(normal_series));
  }

  if (show_alr_state) {
    plot->AppendIntervalSeries(std::move(alr_state));
  }

  if (show_link_capacity) {
    plot->AppendTimeSeriesIfNotEmpty(std::move(link_capacity_lower_series));
    plot->AppendTimeSeriesIfNotEmpty(std::move(link_capacity_upper_series));
  }

  plot->AppendTimeSeries(std::move(loss_series));
  plot->AppendTimeSeriesIfNotEmpty(std::move(probe_failures_series));
  plot->AppendTimeSeries(std::move(delay_series));
  plot->AppendTimeSeriesIfNotEmpty(std::move(scream_series));
  plot->AppendTimeSeries(std::move(created_series));
  plot->AppendTimeSeries(std::move(result_series));

  // Overlay the incoming REMB over the outgoing bitrate.
  TimeSeries remb_series("Remb", LineStyle::kStep);
  for (const auto& rtcp : parsed_log.rembs(kIncomingPacket)) {
    float x = config.GetCallTimeSec(rtcp.log_time());
    float y = static_cast<float>(rtcp.remb.bitrate_bps()) / 1000;
    remb_series.points.emplace_back(x, y);
  }
  plot->AppendTimeSeriesIfNotEmpty(std::move(remb_series));

  plot->SetXAxis(config.CallBeginTimeSec(), config.CallEndTimeSec(), "Time (s)",
                 kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 1, "Bitrate (kbps)", kBottomMargin, kTopMargin);
  plot->SetTitle("Outgoing RTP/RTCP bitrate");
}

void CreateGoogCcSimulationGraph(const ParsedRtcEventLog& parsed_log,
                                 const AnalyzerConfig& config,
                                 Plot* plot) {
  TimeSeries target_rates("Simulated target rate", LineStyle::kStep,
                          PointStyle::kHighlight);
  TimeSeries delay_based("Logged delay-based estimate", LineStyle::kStep,
                         PointStyle::kHighlight);
  TimeSeries loss_based("Logged loss-based estimate", LineStyle::kStep,
                        PointStyle::kHighlight);
  TimeSeries probe_results("Logged probe success", LineStyle::kNone,
                           PointStyle::kHighlight);

  LogBasedNetworkControllerSimulation simulation(
      config.env_, std::make_unique<GoogCcNetworkControllerFactory>(),
      [&](const NetworkControlUpdate& update, Timestamp at_time) {
        if (update.target_rate) {
          target_rates.points.emplace_back(
              config.GetCallTimeSec(at_time),
              update.target_rate->target_rate.kbps<float>());
        }
      });

  simulation.ProcessEventsInLog(parsed_log);
  for (const auto& logged : parsed_log.bwe_delay_updates())
    delay_based.points.emplace_back(config.GetCallTimeSec(logged.log_time()),
                                    logged.bitrate_bps / 1000);
  for (const auto& logged : parsed_log.bwe_probe_success_events())
    probe_results.points.emplace_back(config.GetCallTimeSec(logged.log_time()),
                                      logged.bitrate_bps / 1000);
  for (const auto& logged : parsed_log.bwe_loss_updates())
    loss_based.points.emplace_back(config.GetCallTimeSec(logged.log_time()),
                                   logged.bitrate_bps / 1000);

  plot->AppendTimeSeries(std::move(delay_based));
  plot->AppendTimeSeries(std::move(loss_based));
  plot->AppendTimeSeries(std::move(probe_results));
  plot->AppendTimeSeries(std::move(target_rates));

  plot->SetXAxis(config.CallBeginTimeSec(), config.CallEndTimeSec(), "Time (s)",
                 kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 10, "Bitrate (kbps)", kBottomMargin, kTopMargin);
  plot->SetTitle("Simulated BWE behavior");
}

void CreateScreamSimulationDelayGraph(const ParsedRtcEventLog& parsed_log,
                                      const AnalyzerConfig& config,
                                      Plot* plot) {
  TimeSeries smoothed_rtt_series("Smoothed RTT", LineStyle::kStep);
  TimeSeries queue_delay_series("Queue delay", LineStyle::kStep);
  TimeSeries queue_delay_min_avg_series("Queue delay min avg",
                                        LineStyle::kStep);
  TimeSeries latency_difference_avg_series("Latency difference avg",
                                           LineStyle::kStep);
  TimeSeries qdelay_short_avg_series("Queue delay short avg", LineStyle::kStep);
  TimeSeries qdelay_long_avg_series("Queue delay long avg", LineStyle::kStep);

  LogScreamSimulation simulation({.rate_window = config.window_duration_},
                                 config.env_);
  simulation.ProcessEventsInLog(parsed_log);

  for (const LogScreamSimulation::State& state : simulation.updates()) {
    if (state.smoothed_rtt.IsFinite()) {
      smoothed_rtt_series.points.emplace_back(config.GetCallTimeSec(state.time),
                                              state.smoothed_rtt.ms());
    }
    if (state.queue_delay.IsFinite()) {
      queue_delay_series.points.emplace_back(config.GetCallTimeSec(state.time),
                                             state.queue_delay.ms());
    }
    if (state.queue_delay_min_avg.IsFinite()) {
      queue_delay_min_avg_series.points.emplace_back(
          config.GetCallTimeSec(state.time), state.queue_delay_min_avg.ms());
    }
    if (state.latency_difference_avg.IsFinite()) {
      latency_difference_avg_series.points.emplace_back(
          config.GetCallTimeSec(state.time), state.latency_difference_avg.ms());
    }
    if (state.qdelay_short_avg.IsFinite()) {
      qdelay_short_avg_series.points.emplace_back(
          config.GetCallTimeSec(state.time), state.qdelay_short_avg.ms());
    }
    if (state.qdelay_long_avg.IsFinite()) {
      qdelay_long_avg_series.points.emplace_back(
          config.GetCallTimeSec(state.time), state.qdelay_long_avg.ms());
    }
  }
  plot->AppendTimeSeries(std::move(smoothed_rtt_series));
  plot->AppendTimeSeries(std::move(queue_delay_series));
  plot->AppendTimeSeriesIfNotEmpty(std::move(queue_delay_min_avg_series));
  plot->AppendTimeSeriesIfNotEmpty(std::move(latency_difference_avg_series));
  plot->AppendTimeSeriesIfNotEmpty(std::move(qdelay_short_avg_series));
  plot->AppendTimeSeriesIfNotEmpty(std::move(qdelay_long_avg_series));

  plot->SetXAxis(config.CallBeginTimeSec(), config.CallEndTimeSec(), "Time (s)",
                 kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 50, "Delay (ms)", kBottomMargin, kTopMargin);
  plot->SetTitle("Simulated Scream delays");
}

void CreateScreamSimulationBitrateGraph(const ParsedRtcEventLog& parsed_log,
                                        const AnalyzerConfig& config,
                                        Plot* plot) {
  TimeSeries target_rate_series("Target rate", LineStyle::kStep);
  TimeSeries pacing_rate_series("Pacing rate", LineStyle::kStep);
  TimeSeries send_rate_series("Send rate", LineStyle::kStep);
  TimeSeries received_rate_series("Received rate", LineStyle::kStep);
  IntervalSeries app_limited_series("Application limited", "#5092fc",
                                    IntervalSeries::kHorizontal);

  LogScreamSimulation simulation({.rate_window = config.window_duration_},
                                 config.env_);
  simulation.ProcessEventsInLog(parsed_log);

  bool previously_app_limited = false;
  float app_limited_start_time = 0;

  for (const LogScreamSimulation::State& state : simulation.updates()) {
    target_rate_series.points.emplace_back(config.GetCallTimeSec(state.time),
                                           state.target_rate.bps() / 1000);
    pacing_rate_series.points.emplace_back(config.GetCallTimeSec(state.time),
                                           state.pacing_rate.bps() / 1000);
    send_rate_series.points.emplace_back(config.GetCallTimeSec(state.time),
                                         state.send_rate.bps() / 1000);
    if (state.received_rate.IsFinite()) {
      received_rate_series.points.emplace_back(
          config.GetCallTimeSec(state.time), state.received_rate.bps() / 1000);
    }
    if (state.is_application_limited && !previously_app_limited) {
      app_limited_start_time = config.GetCallTimeSec(state.time);
      previously_app_limited = true;
    } else if (!state.is_application_limited && previously_app_limited) {
      app_limited_series.intervals.emplace_back(
          app_limited_start_time, config.GetCallTimeSec(state.time));
      previously_app_limited = false;
    }
  }

  if (previously_app_limited) {
    app_limited_series.intervals.emplace_back(app_limited_start_time,
                                              config.CallEndTimeSec());
  }

  plot->AppendTimeSeries(std::move(target_rate_series));
  plot->AppendTimeSeries(std::move(pacing_rate_series));
  plot->AppendTimeSeries(std::move(send_rate_series));
  plot->AppendTimeSeries(std::move(received_rate_series));
  plot->AppendIntervalSeries(std::move(app_limited_series));

  plot->SetXAxis(config.CallBeginTimeSec(), config.CallEndTimeSec(), "Time (s)",
                 kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 100, "Kbps", kBottomMargin, kTopMargin);
  plot->SetTitle("Simulated Scream rates");
}

void CreateScreamSimulationRefWindowGraph(const ParsedRtcEventLog& parsed_log,
                                          const AnalyzerConfig& config,
                                          Plot* plot) {
  using SendWindowUsage = LogScreamSimulation::State::SendWindowUsage;
  TimeSeries ref_window_series("RefWindow", LineStyle::kStep);
  TimeSeries ref_window_i_series("RefWindowI", LineStyle::kStep);
  TimeSeries max_data_in_flight("Max allowed data in flight", LineStyle::kStep);
  TimeSeries max_allowed_ref_window_series("Max allowed ref window",
                                           LineStyle::kStep);
  TimeSeries data_in_flight("Data in flight", LineStyle::kStep);
  IntervalSeries send_window_above_max_series(
      "Data in flight > Max allowed", "#ff8e82", IntervalSeries::kHorizontal);
  IntervalSeries send_window_below_ref_window_series(
      "Data in flight < RefWindow", "#c5dff2", IntervalSeries::kHorizontal);
  IntervalSeries send_window_above_ref_window_series(
      "Data in flight >= RefWindow", "#b9fad8", IntervalSeries::kHorizontal);
  IntervalSeries* last_series = &send_window_below_ref_window_series;

  LogScreamSimulation simulation({.rate_window = config.window_duration_},
                                 config.env_);
  simulation.ProcessEventsInLog(parsed_log);
  if (simulation.updates().empty()) {
    RTC_LOG(LS_ERROR) << "Empty simulation.";
    return;
  }

  float send_window_state_switch =
      config.GetCallTimeSec(simulation.updates().front().time);
  SendWindowUsage send_window_usage = SendWindowUsage::kBelowRefWindow;
  float last_time = config.CallBeginTimeSec();
  for (const LogScreamSimulation::State& state : simulation.updates()) {
    ref_window_series.points.emplace_back(config.GetCallTimeSec(state.time),
                                          state.ref_window.bytes());
    if (state.ref_window_i.has_value()) {
      ref_window_i_series.points.emplace_back(config.GetCallTimeSec(state.time),
                                              state.ref_window_i->bytes());
    }
    max_data_in_flight.points.emplace_back(config.GetCallTimeSec(state.time),
                                           state.max_data_in_flight.bytes());
    max_allowed_ref_window_series.points.emplace_back(
        config.GetCallTimeSec(state.time),
        state.max_allowed_ref_window.bytes());
    // Plot the max data in flight before the feedback.
    data_in_flight.points.emplace_back(last_time, state.data_in_flight.bytes());
    if (state.send_window_usage != send_window_usage) {
      last_series->intervals.emplace_back(send_window_state_switch,
                                          config.GetCallTimeSec(state.time));
      send_window_usage = state.send_window_usage;
      send_window_state_switch = config.GetCallTimeSec(state.time);
      switch (send_window_usage) {
        case SendWindowUsage::kAboveRefWindow:
          last_series = &send_window_above_ref_window_series;
          break;
        case SendWindowUsage::kBelowRefWindow:
          last_series = &send_window_below_ref_window_series;
          break;
        case SendWindowUsage::kAboveScreamMax:
          last_series = &send_window_above_max_series;
          break;
      }
    }
    last_time = config.GetCallTimeSec(state.time);
  }
  last_series->intervals.emplace_back(send_window_state_switch,
                                      config.CallEndTimeSec());
  plot->AppendTimeSeries(std::move(ref_window_series));
  plot->AppendTimeSeriesIfNotEmpty(std::move(ref_window_i_series));
  plot->AppendTimeSeries(std::move(max_data_in_flight));
  plot->AppendTimeSeries(std::move(max_allowed_ref_window_series));
  plot->AppendTimeSeries(std::move(data_in_flight));
  plot->AppendIntervalSeries(std::move(send_window_above_max_series));
  plot->AppendIntervalSeries(std::move(send_window_below_ref_window_series));
  plot->AppendIntervalSeries(std::move(send_window_above_ref_window_series));

  plot->SetXAxis(config.CallBeginTimeSec(), config.CallEndTimeSec(), "Time (s)",
                 kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 10, "Bytes", kBottomMargin, kTopMargin);
  plot->SetTitle("Simulated Scream RefWindow");
}

void CreateScreamSimulationRatiosGraph(const ParsedRtcEventLog& parsed_log,
                                       const AnalyzerConfig& config,
                                       Plot* plot) {
  TimeSeries l4s_alpha_series("L4sAlpha", LineStyle::kStep);
  TimeSeries l4s_alpha_v_series("L4sAlphaV", LineStyle::kStep);
  TimeSeries loss_congestion_level_series("LossCongestionLevel",
                                          LineStyle::kStep);
  TimeSeries ref_window_scale_factor_due_to_min_delay_variation(
      "RefWindowScaleFactorDueToAvgMinQueueDelay", LineStyle::kStep);
  TimeSeries ref_window_scale_factor_due_to_latency_difference(
      "RefWindowScaleFactorDueToAvgLatencyDifference", LineStyle::kStep);
  TimeSeries ref_window_scale_factor_close_to_ref_window_i(
      "RefWindowScaleFactorCloseToRefWindowI", LineStyle::kStep);
  TimeSeries ref_window_combined_increase_scale_factor(
      "RefWindowCombinedIncreaseScaleFactor", LineStyle::kStep);

  LogScreamSimulation simulation({.rate_window = config.window_duration_},
                                 config.env_);
  simulation.ProcessEventsInLog(parsed_log);

  for (const LogScreamSimulation::State& state : simulation.updates()) {
    l4s_alpha_series.points.emplace_back(config.GetCallTimeSec(state.time),
                                         state.l4s_alpha);
    l4s_alpha_v_series.points.emplace_back(config.GetCallTimeSec(state.time),
                                           state.l4s_alpha_v);
    loss_congestion_level_series.points.emplace_back(
        config.GetCallTimeSec(state.time), state.loss_congestion_level);
    if (state.ref_window_scale_factor_due_to_avg_min_delay.has_value()) {
      ref_window_scale_factor_due_to_min_delay_variation.points.emplace_back(
          config.GetCallTimeSec(state.time),
          *state.ref_window_scale_factor_due_to_avg_min_delay);
    }
    ref_window_scale_factor_due_to_latency_difference.points.emplace_back(
        config.GetCallTimeSec(state.time),
        state.ref_window_scale_factor_due_to_latency_difference);
    ref_window_scale_factor_close_to_ref_window_i.points.emplace_back(
        config.GetCallTimeSec(state.time),
        state.ref_window_scale_factor_close_to_ref_window_i);
    ref_window_combined_increase_scale_factor.points.emplace_back(
        config.GetCallTimeSec(state.time),
        state.ref_window_combined_increase_scale_factor);
  }
  plot->AppendTimeSeries(std::move(l4s_alpha_series));
  plot->AppendTimeSeries(std::move(l4s_alpha_v_series));
  plot->AppendTimeSeries(std::move(loss_congestion_level_series));
  plot->AppendTimeSeriesIfNotEmpty(
      std::move(ref_window_scale_factor_due_to_min_delay_variation));
  plot->AppendTimeSeries(
      std::move(ref_window_scale_factor_due_to_latency_difference));
  plot->AppendTimeSeries(
      std::move(ref_window_scale_factor_close_to_ref_window_i));
  plot->AppendTimeSeries(std::move(ref_window_combined_increase_scale_factor));

  plot->SetXAxis(config.CallBeginTimeSec(), config.CallEndTimeSec(), "Time (s)",
                 kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 1, "Ratios", kBottomMargin, kTopMargin);
  plot->SetTitle("Simulated Scream Ratios");
}

void CreateScreamSimulationFeedbackEventsPerRttGraph(
    const ParsedRtcEventLog& parsed_log,
    const AnalyzerConfig& config,
    Plot* plot) {
  TimeSeries lost_series("Lost per smoothed RTT", LineStyle::kLine);
  TimeSeries recovered_series("Recovered per smoothed RTT", LineStyle::kLine);
  TimeSeries ce_marked_series("CE marked per smoothed RTT", LineStyle::kLine);

  TimeSeries lost_per_feedback_series("Lost per feedback", LineStyle::kNone,
                                      PointStyle::kHighlight);
  TimeSeries recovered_per_feedback_series(
      "Recovered per feedback", LineStyle::kNone, PointStyle::kHighlight);
  TimeSeries ce_marked_per_feedback_series(
      "CE marked per feedback", LineStyle::kNone, PointStyle::kHighlight);

  LogScreamSimulation simulation({.rate_window = config.window_duration_},
                                 config.env_);
  simulation.ProcessEventsInLog(parsed_log);

  for (const LogScreamSimulation::State& state : simulation.updates()) {
    lost_series.points.emplace_back(config.GetCallTimeSec(state.time),
                                    state.packets_lost_per_rtt);
    recovered_series.points.emplace_back(config.GetCallTimeSec(state.time),
                                         state.packets_recovered_per_rtt);
    ce_marked_series.points.emplace_back(config.GetCallTimeSec(state.time),
                                         state.ce_marked_per_rtt);

    if (state.packets_lost_per_feedback > 0) {
      lost_per_feedback_series.points.emplace_back(
          config.GetCallTimeSec(state.time), state.packets_lost_per_feedback);
    }
    if (state.packets_recovered_per_feedback > 0) {
      recovered_per_feedback_series.points.emplace_back(
          config.GetCallTimeSec(state.time),
          state.packets_recovered_per_feedback);
    }
    if (state.ce_marked_per_feedback > 0) {
      ce_marked_per_feedback_series.points.emplace_back(
          config.GetCallTimeSec(state.time), state.ce_marked_per_feedback);
    }
  }
  plot->AppendTimeSeries(std::move(lost_series));
  plot->AppendTimeSeries(std::move(recovered_series));
  plot->AppendTimeSeries(std::move(ce_marked_series));
  plot->AppendTimeSeriesIfNotEmpty(std::move(lost_per_feedback_series));
  plot->AppendTimeSeriesIfNotEmpty(std::move(recovered_per_feedback_series));
  plot->AppendTimeSeriesIfNotEmpty(std::move(ce_marked_per_feedback_series));

  plot->SetXAxis(config.CallBeginTimeSec(), config.CallEndTimeSec(), "Time (s)",
                 kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 10, "Packets", kBottomMargin, kTopMargin);
  plot->SetTitle("Simulated Scream feedback events per smoothed RTT");
}

void CreateScreamRefWindowGraph(const ParsedRtcEventLog& parsed_log,
                                const AnalyzerConfig& config,
                                Plot* plot) {
  TimeSeries ref_window_series("RefWindow", LineStyle::kStep);
  for (auto& scream_update : parsed_log.bwe_scream_updates()) {
    float x = config.GetCallTimeSec(scream_update.log_time());
    float y = static_cast<float>(scream_update.ref_window.bytes());
    ref_window_series.points.emplace_back(x, y);
  }
  plot->AppendTimeSeries(std::move(ref_window_series));

  TimeSeries data_in_flight_series("Data in flight", LineStyle::kLine);
  for (auto& scream_update : parsed_log.bwe_scream_updates()) {
    float x = config.GetCallTimeSec(scream_update.log_time());
    float y = static_cast<float>(scream_update.data_in_flight.bytes());
    data_in_flight_series.points.emplace_back(x, y);
  }
  plot->AppendTimeSeries(std::move(data_in_flight_series));

  plot->SetXAxis(config.CallBeginTimeSec(), config.CallEndTimeSec(), "Time (s)",
                 kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 3000, "Bytes", kBottomMargin, kTopMargin);
  plot->SetTitle("Scream Ref Window");
}

void CreateScreamDelayEstimateGraph(const ParsedRtcEventLog& parsed_log,
                                    const AnalyzerConfig& config,
                                    Plot* plot) {
  TimeSeries smoothed_rtt_series("Smoothed RTT", LineStyle::kStep);
  TimeSeries avg_queue_delay_series("Avg queue delay", LineStyle::kStep);

  for (auto& scream_update : parsed_log.bwe_scream_updates()) {
    float x = config.GetCallTimeSec(scream_update.log_time());
    float smoothed_rtt_ms = static_cast<float>(scream_update.smoothed_rtt.ms());
    smoothed_rtt_series.points.emplace_back(x, smoothed_rtt_ms);
    float avg_queue_delay_ms =
        static_cast<float>(scream_update.avg_queue_delay.ms());
    avg_queue_delay_series.points.emplace_back(x, avg_queue_delay_ms);
  }

  plot->AppendTimeSeries(std::move(smoothed_rtt_series));
  plot->AppendTimeSeries(std::move(avg_queue_delay_series));

  plot->SetXAxis(config.CallBeginTimeSec(), config.CallEndTimeSec(), "Time (s)",
                 kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 50, "Delay (ms)", kBottomMargin, kTopMargin);
  plot->SetTitle("Scream delay estimates");
}

void CreateSendSideBweSimulationGraph(const ParsedRtcEventLog& parsed_log,
                                      const AnalyzerConfig& config,
                                      Plot* plot) {
  using RtpPacketType = LoggedRtpPacketOutgoing;
  using TransportFeedbackType = LoggedRtcpPacketTransportFeedback;

  // TODO(terelius): The parser could provide a clearer view of the
  // streams, so that we don't have to recalculate it.
  std::multimap<int64_t, const RtpPacketType*> outgoing_rtp;
  for (const auto& stream : parsed_log.outgoing_rtp_packets_by_ssrc()) {
    for (const RtpPacketType& rtp_packet : stream.outgoing_packets)
      outgoing_rtp.insert(
          std::make_pair(rtp_packet.rtp.log_time_us(), &rtp_packet));
  }

  const std::vector<TransportFeedbackType>& incoming_rtcp =
      parsed_log.transport_feedbacks(kIncomingPacket);

  SimulatedClock clock(0);
  BitrateObserver observer;
  TransportFeedbackAdapter transport_feedback;
  auto factory = GoogCcNetworkControllerFactory();
  TimeDelta process_interval = factory.GetProcessInterval();
  // TODO(holmer): Log the call config and use that here instead.
  static const uint32_t kDefaultStartBitrateBps = 300000;
  NetworkControllerConfig cc_config(config.env_);
  cc_config.constraints.at_time = clock.CurrentTime();
  cc_config.constraints.starting_rate =
      DataRate::BitsPerSec(kDefaultStartBitrateBps);
  auto goog_cc = factory.Create(cc_config);

  TimeSeries time_series("Delay-based estimate", LineStyle::kStep,
                         PointStyle::kHighlight);
  TimeSeries acked_time_series("Raw acked bitrate", LineStyle::kLine,
                               PointStyle::kHighlight);
  TimeSeries robust_time_series("Robust throughput estimate", LineStyle::kLine,
                                PointStyle::kHighlight);
  TimeSeries acked_estimate_time_series("Acknowledged bitrate estimate",
                                        LineStyle::kLine,
                                        PointStyle::kHighlight);

  auto rtp_iterator = outgoing_rtp.begin();
  auto rtcp_iterator = incoming_rtcp.begin();

  auto NextRtpTime = [&]() {
    if (rtp_iterator != outgoing_rtp.end())
      return static_cast<int64_t>(rtp_iterator->first);
    return std::numeric_limits<int64_t>::max();
  };

  auto NextRtcpTime = [&]() {
    if (rtcp_iterator != incoming_rtcp.end())
      return static_cast<int64_t>(rtcp_iterator->log_time_us());
    return std::numeric_limits<int64_t>::max();
  };
  int64_t next_process_time_us_ = std::min({NextRtpTime(), NextRtcpTime()});

  auto NextProcessTime = [&]() {
    if (rtcp_iterator != incoming_rtcp.end() ||
        rtp_iterator != outgoing_rtp.end()) {
      return next_process_time_us_;
    }
    return std::numeric_limits<int64_t>::max();
  };

  RateStatistics raw_acked_bitrate(750, 8000);
  FieldTrials throughput_config(
      "WebRTC-Bwe-RobustThroughputEstimatorSettings/enabled:true/");
  std::unique_ptr<AcknowledgedBitrateEstimatorInterface>
      robust_throughput_estimator(
          AcknowledgedBitrateEstimatorInterface::Create(&throughput_config));
  FieldTrials acked_bitrate_config(
      "WebRTC-Bwe-RobustThroughputEstimatorSettings/enabled:false/");
  std::unique_ptr<AcknowledgedBitrateEstimatorInterface>
      acknowledged_bitrate_estimator(
          AcknowledgedBitrateEstimatorInterface::Create(&acked_bitrate_config));
  int64_t time_us =
      std::min({NextRtpTime(), NextRtcpTime(), NextProcessTime()});
  int64_t last_update_us = 0;
  while (time_us != std::numeric_limits<int64_t>::max()) {
    clock.AdvanceTimeMicroseconds(time_us - clock.TimeInMicroseconds());
    if (clock.TimeInMicroseconds() >= NextRtpTime()) {
      RTC_DCHECK_EQ(clock.TimeInMicroseconds(), NextRtpTime());
      const RtpPacketType& rtp_packet = *rtp_iterator->second;
      if (rtp_packet.rtp.header.extension.hasTransportSequenceNumber) {
        RtpPacketToSend send_packet(/*extensions=*/nullptr);
        send_packet.set_transport_sequence_number(
            rtp_packet.rtp.header.extension.transportSequenceNumber);
        send_packet.SetSsrc(rtp_packet.rtp.header.ssrc);
        send_packet.SetSequenceNumber(rtp_packet.rtp.header.sequenceNumber);
        send_packet.SetPayloadSize(rtp_packet.rtp.total_length -
                                   send_packet.headers_size());
        RTC_DCHECK_EQ(send_packet.size(), rtp_packet.rtp.total_length);
        if (IsRtxSsrc(parsed_log, PacketDirection::kOutgoingPacket,
                      rtp_packet.rtp.header.ssrc)) {
          // Don't set the optional media type as we don't know if it is
          // a retransmission, FEC or padding.
        } else if (IsVideoSsrc(parsed_log, PacketDirection::kOutgoingPacket,
                               rtp_packet.rtp.header.ssrc)) {
          send_packet.set_packet_type(RtpPacketMediaType::kVideo);
        } else if (IsAudioSsrc(parsed_log, PacketDirection::kOutgoingPacket,
                               rtp_packet.rtp.header.ssrc)) {
          send_packet.set_packet_type(RtpPacketMediaType::kAudio);
        }
        transport_feedback.AddPacket(
            send_packet, PacedPacketInfo(),
            0u,  // Per packet overhead bytes.,
            Timestamp::Micros(rtp_packet.rtp.log_time_us()));
      }
      SentPacketInfo sent_packet;
      sent_packet.send_time_ms = rtp_packet.rtp.log_time_ms();
      sent_packet.info.included_in_allocation = true;
      sent_packet.info.packet_size_bytes = rtp_packet.rtp.total_length;
      if (rtp_packet.rtp.header.extension.hasTransportSequenceNumber) {
        sent_packet.packet_id =
            rtp_packet.rtp.header.extension.transportSequenceNumber;
        sent_packet.info.included_in_feedback = true;
      }
      auto sent_msg = transport_feedback.ProcessSentPacket(sent_packet);
      if (sent_msg)
        observer.Update(goog_cc->OnSentPacket(*sent_msg));
      ++rtp_iterator;
    }
    if (clock.TimeInMicroseconds() >= NextRtcpTime()) {
      RTC_DCHECK_EQ(clock.TimeInMicroseconds(), NextRtcpTime());

      auto feedback_msg = transport_feedback.ProcessTransportFeedback(
          rtcp_iterator->transport_feedback, clock.CurrentTime());
      if (feedback_msg) {
        observer.Update(goog_cc->OnTransportPacketsFeedback(*feedback_msg));
        std::vector<PacketResult> feedback =
            feedback_msg->SortedByReceiveTime();
        if (!feedback.empty()) {
          acknowledged_bitrate_estimator->IncomingPacketFeedbackVector(
              feedback);
          robust_throughput_estimator->IncomingPacketFeedbackVector(feedback);
          for (const PacketResult& packet : feedback) {
            raw_acked_bitrate.Update(packet.sent_packet.size.bytes(),
                                     packet.receive_time.ms());
          }
          std::optional<uint32_t> raw_bitrate_bps =
              raw_acked_bitrate.Rate(feedback.back().receive_time.ms());
          float x = config.GetCallTimeSec(clock.CurrentTime());
          if (raw_bitrate_bps) {
            float y = raw_bitrate_bps.value() / 1000;
            acked_time_series.points.emplace_back(x, y);
          }
          std::optional<DataRate> robust_estimate =
              robust_throughput_estimator->bitrate();
          if (robust_estimate) {
            float y = robust_estimate.value().kbps();
            robust_time_series.points.emplace_back(x, y);
          }
          std::optional<DataRate> acked_estimate =
              acknowledged_bitrate_estimator->bitrate();
          if (acked_estimate) {
            float y = acked_estimate.value().kbps();
            acked_estimate_time_series.points.emplace_back(x, y);
          }
        }
      }
      ++rtcp_iterator;
    }
    if (clock.TimeInMicroseconds() >= NextProcessTime()) {
      RTC_DCHECK_EQ(clock.TimeInMicroseconds(), NextProcessTime());
      ProcessInterval msg;
      msg.at_time = clock.CurrentTime();
      observer.Update(goog_cc->OnProcessInterval(msg));
      next_process_time_us_ += process_interval.us();
    }
    if (observer.GetAndResetBitrateUpdated() ||
        time_us - last_update_us >= 1e6) {
      uint32_t y = observer.last_bitrate_bps() / 1000;
      float x = config.GetCallTimeSec(clock.CurrentTime());
      time_series.points.emplace_back(x, y);
      last_update_us = time_us;
    }
    time_us = std::min({NextRtpTime(), NextRtcpTime(), NextProcessTime()});
  }
  // Add the data set to the plot.
  plot->AppendTimeSeries(std::move(time_series));
  plot->AppendTimeSeries(std::move(robust_time_series));
  plot->AppendTimeSeries(std::move(acked_time_series));
  plot->AppendTimeSeriesIfNotEmpty(std::move(acked_estimate_time_series));

  plot->SetXAxis(config.CallBeginTimeSec(), config.CallEndTimeSec(), "Time (s)",
                 kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 10, "Bitrate (kbps)", kBottomMargin, kTopMargin);
  plot->SetTitle("Simulated send-side BWE behavior");
}

void CreateReceiveSideBweSimulationGraph(const ParsedRtcEventLog& parsed_log,
                                         const AnalyzerConfig& config,
                                         Plot* plot) {
  using RtpPacketType = LoggedRtpPacketIncoming;
  class RembInterceptor {
   public:
    void SendRemb(uint32_t bitrate_bps, std::span<const uint32_t> ssrcs) {
      last_bitrate_bps_ = bitrate_bps;
      bitrate_updated_ = true;
    }
    uint32_t last_bitrate_bps() const { return last_bitrate_bps_; }
    bool GetAndResetBitrateUpdated() {
      bool bitrate_updated = bitrate_updated_;
      bitrate_updated_ = false;
      return bitrate_updated;
    }

   private:
    // We don't know the start bitrate, but assume that it is the default 300
    // kbps.
    uint32_t last_bitrate_bps_ = 300000;
    bool bitrate_updated_ = false;
  };

  std::multimap<int64_t, const RtpPacketType*> incoming_rtp;

  for (const auto& stream : parsed_log.incoming_rtp_packets_by_ssrc()) {
    if (IsVideoSsrc(parsed_log, kIncomingPacket, stream.ssrc)) {
      for (const auto& rtp_packet : stream.incoming_packets)
        incoming_rtp.insert(
            std::make_pair(rtp_packet.rtp.log_time_us(), &rtp_packet));
    }
  }

  SimulatedClock clock(0);
  EnvironmentFactory env_factory(config.env_);
  env_factory.Set(&clock);
  RembInterceptor remb_interceptor;
  ReceiveSideCongestionController rscc(
      env_factory.Create(), [](auto...) {},
      absl::bind_front(&RembInterceptor::SendRemb, &remb_interceptor));
  // TODO(holmer): Log the call config and use that here instead.
  // static const uint32_t kDefaultStartBitrateBps = 300000;
  // rscc.SetBweBitrates(0, kDefaultStartBitrateBps, -1);

  TimeSeries time_series("Receive side estimate", LineStyle::kLine,
                         PointStyle::kHighlight);
  TimeSeries acked_time_series("Received bitrate", LineStyle::kLine);

  RateStatistics acked_bitrate(250, 8000);
  int64_t last_update_us = 0;
  for (const auto& kv : incoming_rtp) {
    const RtpPacketType& packet = *kv.second;

    RtpPacketReceived rtp_packet = RtpPacketForBWEFromHeader(packet.rtp.header);
    rtp_packet.set_arrival_time(packet.rtp.log_time());
    rtp_packet.SetPayloadSize(packet.rtp.total_length -
                              rtp_packet.headers_size());

    clock.AdvanceTime(rtp_packet.arrival_time() - clock.CurrentTime());
    rscc.OnReceivedPacket(rtp_packet, MediaType::VIDEO);
    int64_t arrival_time_ms = packet.rtp.log_time().ms();
    acked_bitrate.Update(packet.rtp.total_length, arrival_time_ms);
    std::optional<uint32_t> bitrate_bps = acked_bitrate.Rate(arrival_time_ms);
    if (bitrate_bps) {
      uint32_t y = *bitrate_bps / 1000;
      float x = config.GetCallTimeSec(clock.CurrentTime());
      acked_time_series.points.emplace_back(x, y);
    }
    if (remb_interceptor.GetAndResetBitrateUpdated() ||
        clock.TimeInMicroseconds() - last_update_us >= 1e6) {
      uint32_t y = remb_interceptor.last_bitrate_bps() / 1000;
      float x = config.GetCallTimeSec(clock.CurrentTime());
      time_series.points.emplace_back(x, y);
      last_update_us = clock.TimeInMicroseconds();
    }
  }
  // Add the data set to the plot.
  plot->AppendTimeSeries(std::move(time_series));
  plot->AppendTimeSeries(std::move(acked_time_series));

  plot->SetXAxis(config.CallBeginTimeSec(), config.CallEndTimeSec(), "Time (s)",
                 kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 10, "Bitrate (kbps)", kBottomMargin, kTopMargin);
  plot->SetTitle("Simulated receive-side BWE behavior");
}

void CreateNetworkDelayFeedbackGraph(const ParsedRtcEventLog& parsed_log,
                                     const AnalyzerConfig& config,
                                     Plot* plot) {
  TimeSeries time_series("Network delay", LineStyle::kLine,
                         PointStyle::kHighlight);
  int64_t min_send_receive_diff_ms = std::numeric_limits<int64_t>::max();
  int64_t min_rtt_ms = std::numeric_limits<int64_t>::max();

  std::vector<MatchedSendArrivalTimes> matched_rtp_rtcp =
      GetNetworkTrace(parsed_log);
  absl::c_stable_sort(matched_rtp_rtcp, [](const MatchedSendArrivalTimes& a,
                                           const MatchedSendArrivalTimes& b) {
    return a.feedback_arrival_time_ms < b.feedback_arrival_time_ms ||
           (a.feedback_arrival_time_ms == b.feedback_arrival_time_ms &&
            a.arrival_time_ms < b.arrival_time_ms);
  });
  for (const auto& packet : matched_rtp_rtcp) {
    if (packet.arrival_time_ms == MatchedSendArrivalTimes::kNotReceived)
      continue;
    float x = config.GetCallTimeSecFromMs(packet.feedback_arrival_time_ms);
    int64_t y = packet.arrival_time_ms - packet.send_time_ms;
    int64_t rtt_ms = packet.feedback_arrival_time_ms - packet.send_time_ms;
    min_rtt_ms = std::min(rtt_ms, min_rtt_ms);
    min_send_receive_diff_ms = std::min(y, min_send_receive_diff_ms);
    time_series.points.emplace_back(x, y);
  }

  // We assume that the base network delay (w/o queues) is equal to half
  // the minimum RTT. Therefore rescale the delays by subtracting the minimum
  // observed 1-ways delay and add half the minimum RTT.
  const int64_t estimated_clock_offset_ms =
      min_send_receive_diff_ms - min_rtt_ms / 2;
  for (TimeSeriesPoint& point : time_series.points)
    point.y -= estimated_clock_offset_ms;

  // Add the data set to the plot.
  plot->AppendTimeSeriesIfNotEmpty(std::move(time_series));

  plot->SetXAxis(config.CallBeginTimeSec(), config.CallEndTimeSec(), "Time (s)",
                 kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 10, "Delay (ms)", kBottomMargin, kTopMargin);
  plot->SetTitle("Outgoing network delay (based on per-packet feedback)");
}

void CreatePacerDelayGraph(const ParsedRtcEventLog& parsed_log,
                           const AnalyzerConfig& config,
                           Plot* plot) {
  for (const auto& stream : parsed_log.outgoing_rtp_packets_by_ssrc()) {
    const std::vector<LoggedRtpPacketOutgoing>& packets =
        stream.outgoing_packets;

    if (IsRtxSsrc(parsed_log, kOutgoingPacket, stream.ssrc)) {
      continue;
    }

    if (packets.size() < 2) {
      RTC_LOG(LS_WARNING)
          << "Can't estimate the RTP clock frequency or the "
             "pacer delay with less than 2 packets in the stream";
      continue;
    }
    int64_t segment_end_us = parsed_log.first_log_segment().stop_time_us();
    std::optional<uint32_t> estimated_frequency =
        EstimateRtpClockFrequency(packets, segment_end_us);
    if (!estimated_frequency)
      continue;
    if (IsVideoSsrc(parsed_log, kOutgoingPacket, stream.ssrc) &&
        *estimated_frequency != 90000) {
      RTC_LOG(LS_WARNING)
          << "Video stream should use a 90 kHz clock but appears to use "
          << *estimated_frequency / 1000 << ". Discarding.";
      continue;
    }

    TimeSeries pacer_delay_series(
        GetStreamName(parsed_log, kOutgoingPacket, stream.ssrc) + "(" +
            std::to_string(*estimated_frequency / 1000) + " kHz)",
        LineStyle::kLine, PointStyle::kHighlight);
    SeqNumUnwrapper<uint32_t> timestamp_unwrapper;
    uint64_t first_capture_timestamp =
        timestamp_unwrapper.Unwrap(packets.front().rtp.header.timestamp);
    uint64_t first_send_timestamp = packets.front().rtp.log_time_us();
    for (const auto& packet : packets) {
      double capture_time_ms = (static_cast<double>(timestamp_unwrapper.Unwrap(
                                    packet.rtp.header.timestamp)) -
                                first_capture_timestamp) /
                               *estimated_frequency * 1000;
      double send_time_ms =
          static_cast<double>(packet.rtp.log_time_us() - first_send_timestamp) /
          1000;
      float x = config.GetCallTimeSec(packet.rtp.log_time());
      float y = send_time_ms - capture_time_ms;
      pacer_delay_series.points.emplace_back(x, y);
    }
    plot->AppendTimeSeries(std::move(pacer_delay_series));
  }

  plot->SetXAxis(config.CallBeginTimeSec(), config.CallEndTimeSec(), "Time (s)",
                 kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 10, "Pacer delay (ms)", kBottomMargin, kTopMargin);
  plot->SetTitle(
      "Delay from capture to send time. (First packet normalized to 0.)");
}

}  // namespace webrtc
