/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_tools/rtc_event_log_visualizer/analyzer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/functional/bind_front.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "api/dtls_transport_interface.h"
#include "api/function_view.h"
#include "api/media_types.h"
#include "api/network_state_predictor.h"
#include "api/rtc_event_log/rtc_event_log.h"
#include "api/rtp_headers.h"
#include "api/transport/goog_cc_factory.h"
#include "api/transport/network_control.h"
#include "api/transport/network_types.h"
#include "api/units/data_rate.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "logging/rtc_event_log/events/logged_rtp_rtcp.h"
#include "logging/rtc_event_log/events/rtc_event_generic_packet_received.h"
#include "logging/rtc_event_log/events/rtc_event_generic_packet_sent.h"
#include "logging/rtc_event_log/events/rtc_event_ice_candidate_pair.h"
#include "logging/rtc_event_log/events/rtc_event_ice_candidate_pair_config.h"
#include "logging/rtc_event_log/rtc_event_log_parser.h"
#include "logging/rtc_event_log/rtc_event_processor.h"
#include "modules/congestion_controller/goog_cc/acknowledged_bitrate_estimator_interface.h"
#include "modules/congestion_controller/include/receive_side_congestion_controller.h"
#include "modules/congestion_controller/rtp/transport_feedback_adapter.h"
#include "modules/remote_bitrate_estimator/include/remote_bitrate_estimator.h"
#include "modules/rtp_rtcp/include/rtp_header_extension_map.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtcp_packet/receiver_report.h"
#include "modules/rtp_rtcp/source/rtcp_packet/remb.h"
#include "modules/rtp_rtcp/source/rtcp_packet/report_block.h"
#include "modules/rtp_rtcp/source/rtcp_packet/sender_report.h"
#include "modules/rtp_rtcp/source/rtcp_packet/target_bitrate.h"
#include "modules/rtp_rtcp/source/rtcp_packet/transport_feedback.h"
#include "modules/rtp_rtcp/source/rtp_header_extensions.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/numerics/sequence_number_unwrapper.h"
#include "rtc_base/rate_statistics.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_tools/rtc_event_log_visualizer/analyzer_common.h"
#include "rtc_tools/rtc_event_log_visualizer/log_simulation.h"
#include "rtc_tools/rtc_event_log_visualizer/plot_base.h"
#include "system_wrappers/include/clock.h"
#include "test/explicit_key_value_config.h"

namespace webrtc {

namespace {

std::string SsrcToString(uint32_t ssrc) {
  rtc::StringBuilder ss;
  ss << "SSRC " << ssrc;
  return ss.Release();
}

// Checks whether an SSRC is contained in the list of desired SSRCs.
// Note that an empty SSRC list matches every SSRC.
bool MatchingSsrc(uint32_t ssrc, const std::vector<uint32_t>& desired_ssrc) {
  if (desired_ssrc.empty())
    return true;
  return std::find(desired_ssrc.begin(), desired_ssrc.end(), ssrc) !=
         desired_ssrc.end();
}

double AbsSendTimeToMicroseconds(int64_t abs_send_time) {
  // The timestamp is a fixed point representation with 6 bits for seconds
  // and 18 bits for fractions of a second. Thus, we divide by 2^18 to get the
  // time in seconds and then multiply by kNumMicrosecsPerSec to convert to
  // microseconds.
  static constexpr double kTimestampToMicroSec =
      static_cast<double>(kNumMicrosecsPerSec) / static_cast<double>(1ul << 18);
  return abs_send_time * kTimestampToMicroSec;
}

// Computes the difference `later` - `earlier` where `later` and `earlier`
// are counters that wrap at `modulus`. The difference is chosen to have the
// least absolute value. For example if `modulus` is 8, then the difference will
// be chosen in the range [-3, 4]. If `modulus` is 9, then the difference will
// be in [-4, 4].
int64_t WrappingDifference(uint32_t later, uint32_t earlier, int64_t modulus) {
  RTC_DCHECK_LE(1, modulus);
  RTC_DCHECK_LT(later, modulus);
  RTC_DCHECK_LT(earlier, modulus);
  int64_t difference =
      static_cast<int64_t>(later) - static_cast<int64_t>(earlier);
  int64_t max_difference = modulus / 2;
  int64_t min_difference = max_difference - modulus + 1;
  if (difference > max_difference) {
    difference -= modulus;
  }
  if (difference < min_difference) {
    difference += modulus;
  }
  if (difference > max_difference / 2 || difference < min_difference / 2) {
    RTC_LOG(LS_WARNING) << "Difference between" << later << " and " << earlier
                        << " expected to be in the range ("
                        << min_difference / 2 << "," << max_difference / 2
                        << ") but is " << difference
                        << ". Correct unwrapping is uncertain.";
  }
  return difference;
}

// This is much more reliable for outgoing streams than for incoming streams.
template <typename RtpPacketContainer>
absl::optional<uint32_t> EstimateRtpClockFrequency(
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
    return absl::nullopt;
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
  return absl::nullopt;
}

absl::optional<double> NetworkDelayDiff_AbsSendTime(
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
    return absl::nullopt;
  }
}

absl::optional<double> NetworkDelayDiff_CaptureTime(
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

template <typename T>
TimeSeries CreateRtcpTypeTimeSeries(const std::vector<T>& rtcp_list,
                                    AnalyzerConfig config,
                                    std::string rtcp_name,
                                    int category_id) {
  TimeSeries time_series(rtcp_name, LineStyle::kNone, PointStyle::kHighlight);
  for (const auto& rtcp : rtcp_list) {
    float x = config.GetCallTimeSec(rtcp.timestamp);
    float y = category_id;
    time_series.points.emplace_back(x, y);
  }
  return time_series;
}

const char kUnknownEnumValue[] = "unknown";

// TODO(tommi): This should be "host".
const char kIceCandidateTypeLocal[] = "local";
// TODO(tommi): This should be "srflx".
const char kIceCandidateTypeStun[] = "stun";
const char kIceCandidateTypePrflx[] = "prflx";
const char kIceCandidateTypeRelay[] = "relay";

const char kProtocolUdp[] = "udp";
const char kProtocolTcp[] = "tcp";
const char kProtocolSsltcp[] = "ssltcp";
const char kProtocolTls[] = "tls";

const char kAddressFamilyIpv4[] = "ipv4";
const char kAddressFamilyIpv6[] = "ipv6";

const char kNetworkTypeEthernet[] = "ethernet";
const char kNetworkTypeLoopback[] = "loopback";
const char kNetworkTypeWifi[] = "wifi";
const char kNetworkTypeVpn[] = "vpn";
const char kNetworkTypeCellular[] = "cellular";

absl::string_view GetIceCandidateTypeAsString(IceCandidateType type) {
  switch (type) {
    case IceCandidateType::kHost:
      return kIceCandidateTypeLocal;
    case IceCandidateType::kSrflx:
      return kIceCandidateTypeStun;
    case IceCandidateType::kPrflx:
      return kIceCandidateTypePrflx;
    case IceCandidateType::kRelay:
      return kIceCandidateTypeRelay;
    default:
      RTC_DCHECK_NOTREACHED();
      return kUnknownEnumValue;
  }
}

std::string GetProtocolAsString(webrtc::IceCandidatePairProtocol protocol) {
  switch (protocol) {
    case webrtc::IceCandidatePairProtocol::kUdp:
      return kProtocolUdp;
    case webrtc::IceCandidatePairProtocol::kTcp:
      return kProtocolTcp;
    case webrtc::IceCandidatePairProtocol::kSsltcp:
      return kProtocolSsltcp;
    case webrtc::IceCandidatePairProtocol::kTls:
      return kProtocolTls;
    default:
      return kUnknownEnumValue;
  }
}

std::string GetAddressFamilyAsString(
    webrtc::IceCandidatePairAddressFamily family) {
  switch (family) {
    case webrtc::IceCandidatePairAddressFamily::kIpv4:
      return kAddressFamilyIpv4;
    case webrtc::IceCandidatePairAddressFamily::kIpv6:
      return kAddressFamilyIpv6;
    default:
      return kUnknownEnumValue;
  }
}

std::string GetNetworkTypeAsString(webrtc::IceCandidateNetworkType type) {
  switch (type) {
    case webrtc::IceCandidateNetworkType::kEthernet:
      return kNetworkTypeEthernet;
    case webrtc::IceCandidateNetworkType::kLoopback:
      return kNetworkTypeLoopback;
    case webrtc::IceCandidateNetworkType::kWifi:
      return kNetworkTypeWifi;
    case webrtc::IceCandidateNetworkType::kVpn:
      return kNetworkTypeVpn;
    case webrtc::IceCandidateNetworkType::kCellular:
      return kNetworkTypeCellular;
    default:
      return kUnknownEnumValue;
  }
}

std::string GetCandidatePairLogDescriptionAsString(
    const LoggedIceCandidatePairConfig& config) {
  // Example: stun:wifi->relay(tcp):cellular@udp:ipv4
  // represents a pair of a local server-reflexive candidate on a WiFi network
  // and a remote relay candidate using TCP as the relay protocol on a cell
  // network, when the candidate pair communicates over UDP using IPv4.
  rtc::StringBuilder ss;
  ss << GetIceCandidateTypeAsString(config.local_candidate_type);

  if (config.local_candidate_type == IceCandidateType::kRelay) {
    ss << "(" << GetProtocolAsString(config.local_relay_protocol) << ")";
  }

  ss << ":" << GetNetworkTypeAsString(config.local_network_type) << ":"
     << GetAddressFamilyAsString(config.local_address_family) << "->"
     << GetIceCandidateTypeAsString(config.remote_candidate_type) << ":"
     << GetAddressFamilyAsString(config.remote_address_family) << "@"
     << GetProtocolAsString(config.candidate_pair_protocol);
  return ss.Release();
}

std::string GetDirectionAsString(PacketDirection direction) {
  if (direction == kIncomingPacket) {
    return "Incoming";
  } else {
    return "Outgoing";
  }
}

std::string GetDirectionAsShortString(PacketDirection direction) {
  if (direction == kIncomingPacket) {
    return "In";
  } else {
    return "Out";
  }
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
  rtp_header_extensions.Register<TransmissionOffset>(1);
  rtp_header_extensions.Register<AbsoluteSendTime>(2);
  rtp_header_extensions.Register<TransportSequenceNumber>(3);
  rtp_header_extensions.Register<FakeExtensionSmall>(4);
  // Use id > 14 to force two byte header per rtp header when this one is used.
  rtp_header_extensions.Register<FakeExtensionLarge>(16);

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

struct PacketLossSummary {
  size_t num_packets = 0;
  size_t num_lost_packets = 0;
  Timestamp base_time = Timestamp::MinusInfinity();
};

}  // namespace

EventLogAnalyzer::EventLogAnalyzer(const ParsedRtcEventLog& log,
                                   bool normalize_time)
    : parsed_log_(log) {
  config_.window_duration_ = TimeDelta::Millis(250);
  config_.step_ = TimeDelta::Millis(10);
  if (!log.start_log_events().empty()) {
    config_.rtc_to_utc_offset_ = log.start_log_events()[0].utc_time() -
                                 log.start_log_events()[0].log_time();
  }
  config_.normalize_time_ = normalize_time;
  config_.begin_time_ = parsed_log_.first_timestamp();
  config_.end_time_ = parsed_log_.last_timestamp();
  if (config_.end_time_ < config_.begin_time_) {
    RTC_LOG(LS_WARNING) << "No useful events in the log.";
    config_.begin_time_ = config_.end_time_ = Timestamp::Zero();
  }

  RTC_LOG(LS_INFO) << "Log is "
                   << (parsed_log_.last_timestamp().ms() -
                       parsed_log_.first_timestamp().ms()) /
                          1000
                   << " seconds long.";
}

EventLogAnalyzer::EventLogAnalyzer(const ParsedRtcEventLog& log,
                                   const AnalyzerConfig& config)
    : parsed_log_(log), config_(config) {
  RTC_LOG(LS_INFO) << "Log is "
                   << (parsed_log_.last_timestamp().ms() -
                       parsed_log_.first_timestamp().ms()) /
                          1000
                   << " seconds long.";
}

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

void EventLogAnalyzer::CreatePacketGraph(PacketDirection direction,
                                         Plot* plot) {
  for (const auto& stream : parsed_log_.rtp_packets_by_ssrc(direction)) {
    // Filter on SSRC.
    if (!MatchingSsrc(stream.ssrc, desired_ssrc_)) {
      continue;
    }

    TimeSeries time_series(GetStreamName(parsed_log_, direction, stream.ssrc),
                           LineStyle::kBar);
    auto GetPacketSize = [](const LoggedRtpPacket& packet) {
      return absl::optional<float>(packet.total_length);
    };
    auto ToCallTime = [this](const LoggedRtpPacket& packet) {
      return this->config_.GetCallTimeSec(packet.timestamp);
    };
    ProcessPoints<LoggedRtpPacket>(ToCallTime, GetPacketSize,
                                   stream.packet_view, &time_series);
    plot->AppendTimeSeries(std::move(time_series));
  }

  plot->SetXAxis(config_.CallBeginTimeSec(), config_.CallEndTimeSec(),
                 "Time (s)", kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 1, "Packet size (bytes)", kBottomMargin,
                          kTopMargin);
  plot->SetTitle(GetDirectionAsString(direction) + " RTP packets");
}

void EventLogAnalyzer::CreateRtcpTypeGraph(PacketDirection direction,
                                           Plot* plot) {
  plot->AppendTimeSeries(CreateRtcpTypeTimeSeries(
      parsed_log_.transport_feedbacks(direction), config_, "TWCC", 1));
  plot->AppendTimeSeries(CreateRtcpTypeTimeSeries(
      parsed_log_.receiver_reports(direction), config_, "RR", 2));
  plot->AppendTimeSeries(CreateRtcpTypeTimeSeries(
      parsed_log_.sender_reports(direction), config_, "SR", 3));
  plot->AppendTimeSeries(CreateRtcpTypeTimeSeries(
      parsed_log_.extended_reports(direction), config_, "XR", 4));
  plot->AppendTimeSeries(CreateRtcpTypeTimeSeries(parsed_log_.nacks(direction),
                                                  config_, "NACK", 5));
  plot->AppendTimeSeries(CreateRtcpTypeTimeSeries(parsed_log_.rembs(direction),
                                                  config_, "REMB", 6));
  plot->AppendTimeSeries(
      CreateRtcpTypeTimeSeries(parsed_log_.firs(direction), config_, "FIR", 7));
  plot->AppendTimeSeries(
      CreateRtcpTypeTimeSeries(parsed_log_.plis(direction), config_, "PLI", 8));
  plot->AppendTimeSeries(
      CreateRtcpTypeTimeSeries(parsed_log_.byes(direction), config_, "BYE", 9));
  plot->SetXAxis(config_.CallBeginTimeSec(), config_.CallEndTimeSec(),
                 "Time (s)", kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 1, "RTCP type", kBottomMargin, kTopMargin);
  plot->SetTitle(GetDirectionAsString(direction) + " RTCP packets");
  plot->SetYAxisTickLabels({{1, "TWCC"},
                            {2, "RR"},
                            {3, "SR"},
                            {4, "XR"},
                            {5, "NACK"},
                            {6, "REMB"},
                            {7, "FIR"},
                            {8, "PLI"},
                            {9, "BYE"}});
}

template <typename IterableType>
void EventLogAnalyzer::CreateAccumulatedPacketsTimeSeries(
    Plot* plot,
    const IterableType& packets,
    const std::string& label) {
  TimeSeries time_series(label, LineStyle::kStep);
  for (size_t i = 0; i < packets.size(); i++) {
    float x = config_.GetCallTimeSec(packets[i].log_time());
    time_series.points.emplace_back(x, i + 1);
  }
  plot->AppendTimeSeries(std::move(time_series));
}

void EventLogAnalyzer::CreateAccumulatedPacketsGraph(PacketDirection direction,
                                                     Plot* plot) {
  for (const auto& stream : parsed_log_.rtp_packets_by_ssrc(direction)) {
    if (!MatchingSsrc(stream.ssrc, desired_ssrc_))
      continue;
    std::string label = std::string("RTP ") +
                        GetStreamName(parsed_log_, direction, stream.ssrc);
    CreateAccumulatedPacketsTimeSeries(plot, stream.packet_view, label);
  }
  std::string label =
      std::string("RTCP ") + "(" + GetDirectionAsShortString(direction) + ")";
  if (direction == kIncomingPacket) {
    CreateAccumulatedPacketsTimeSeries(
        plot, parsed_log_.incoming_rtcp_packets(), label);
  } else {
    CreateAccumulatedPacketsTimeSeries(
        plot, parsed_log_.outgoing_rtcp_packets(), label);
  }

  plot->SetXAxis(config_.CallBeginTimeSec(), config_.CallEndTimeSec(),
                 "Time (s)", kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 1, "Received Packets", kBottomMargin, kTopMargin);
  plot->SetTitle(std::string("Accumulated ") + GetDirectionAsString(direction) +
                 " RTP/RTCP packets");
}

void EventLogAnalyzer::CreatePacketRateGraph(PacketDirection direction,
                                             Plot* plot) {
  auto CountPackets = [](auto packet) { return 1.0; };
  for (const auto& stream : parsed_log_.rtp_packets_by_ssrc(direction)) {
    // Filter on SSRC.
    if (!MatchingSsrc(stream.ssrc, desired_ssrc_)) {
      continue;
    }
    TimeSeries time_series(
        std::string("RTP ") +
            GetStreamName(parsed_log_, direction, stream.ssrc),
        LineStyle::kLine);
    MovingAverage<LoggedRtpPacket, double>(CountPackets, stream.packet_view,
                                           config_, &time_series);
    plot->AppendTimeSeries(std::move(time_series));
  }
  TimeSeries time_series(
      std::string("RTCP ") + "(" + GetDirectionAsShortString(direction) + ")",
      LineStyle::kLine);
  if (direction == kIncomingPacket) {
    MovingAverage<LoggedRtcpPacketIncoming, double>(
        CountPackets, parsed_log_.incoming_rtcp_packets(), config_,
        &time_series);
  } else {
    MovingAverage<LoggedRtcpPacketOutgoing, double>(
        CountPackets, parsed_log_.outgoing_rtcp_packets(), config_,
        &time_series);
  }
  plot->AppendTimeSeries(std::move(time_series));

  plot->SetXAxis(config_.CallBeginTimeSec(), config_.CallEndTimeSec(),
                 "Time (s)", kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 1, "Packet Rate (packets/s)", kBottomMargin,
                          kTopMargin);
  plot->SetTitle("Rate of " + GetDirectionAsString(direction) +
                 " RTP/RTCP packets");
}

void EventLogAnalyzer::CreateTotalPacketRateGraph(PacketDirection direction,
                                                  Plot* plot) {
  // Contains a log timestamp to enable counting logged events of different
  // types using MovingAverage().
  class LogTime {
   public:
    explicit LogTime(Timestamp log_time) : log_time_(log_time) {}
    Timestamp log_time() const { return log_time_; }

   private:
    Timestamp log_time_;
  };
  std::vector<LogTime> packet_times;
  auto handle_rtp = [&packet_times](const LoggedRtpPacket& packet) {
    packet_times.emplace_back(packet.log_time());
  };
  RtcEventProcessor process;
  for (const auto& stream : parsed_log_.rtp_packets_by_ssrc(direction)) {
    process.AddEvents(stream.packet_view, handle_rtp, direction);
  }
  if (direction == kIncomingPacket) {
    auto handle_incoming_rtcp =
        [&packet_times](const LoggedRtcpPacketIncoming& packet) {
          packet_times.emplace_back(packet.log_time());
        };
    process.AddEvents(parsed_log_.incoming_rtcp_packets(),
                      handle_incoming_rtcp);
  } else {
    auto handle_outgoing_rtcp =
        [&packet_times](const LoggedRtcpPacketOutgoing& packet) {
          packet_times.emplace_back(packet.log_time());
        };
    process.AddEvents(parsed_log_.outgoing_rtcp_packets(),
                      handle_outgoing_rtcp);
  }
  process.ProcessEventsInOrder();
  TimeSeries time_series(std::string("Total ") + "(" +
                             GetDirectionAsShortString(direction) + ") packets",
                         LineStyle::kLine);
  MovingAverage<LogTime, uint64_t>([](auto packet) { return 1; }, packet_times,
                                   config_, &time_series);
  plot->AppendTimeSeries(std::move(time_series));

  plot->SetXAxis(config_.CallBeginTimeSec(), config_.CallEndTimeSec(),
                 "Time (s)", kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 1, "Packet Rate (packets/s)", kBottomMargin,
                          kTopMargin);
  plot->SetTitle("Rate of all " + GetDirectionAsString(direction) +
                 " RTP/RTCP packets");
}

// For each SSRC, plot the time between the consecutive playouts.
void EventLogAnalyzer::CreatePlayoutGraph(Plot* plot) {
  for (const auto& playout_stream : parsed_log_.audio_playout_events()) {
    uint32_t ssrc = playout_stream.first;
    if (!MatchingSsrc(ssrc, desired_ssrc_))
      continue;
    absl::optional<int64_t> last_playout_ms;
    TimeSeries time_series(SsrcToString(ssrc), LineStyle::kBar);
    for (const auto& playout_event : playout_stream.second) {
      float x = config_.GetCallTimeSec(playout_event.log_time());
      int64_t playout_time_ms = playout_event.log_time_ms();
      // If there were no previous playouts, place the point on the x-axis.
      float y = playout_time_ms - last_playout_ms.value_or(playout_time_ms);
      time_series.points.push_back(TimeSeriesPoint(x, y));
      last_playout_ms.emplace(playout_time_ms);
    }
    plot->AppendTimeSeries(std::move(time_series));
  }

  plot->SetXAxis(config_.CallBeginTimeSec(), config_.CallEndTimeSec(),
                 "Time (s)", kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 1, "Time since last playout (ms)", kBottomMargin,
                          kTopMargin);
  plot->SetTitle("Audio playout");
}

void EventLogAnalyzer::CreateNetEqSetMinimumDelay(Plot* plot) {
  for (const auto& playout_stream :
       parsed_log_.neteq_set_minimum_delay_events()) {
    uint32_t ssrc = playout_stream.first;
    if (!MatchingSsrc(ssrc, desired_ssrc_))
      continue;

    TimeSeries time_series(SsrcToString(ssrc), LineStyle::kStep,
                           PointStyle::kHighlight);
    for (const auto& event : playout_stream.second) {
      float x = config_.GetCallTimeSec(event.log_time());
      float y = event.minimum_delay_ms;
      time_series.points.push_back(TimeSeriesPoint(x, y));
    }
    plot->AppendTimeSeries(std::move(time_series));
  }

  plot->SetXAxis(config_.CallBeginTimeSec(), config_.CallEndTimeSec(),
                 "Time (s)", kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 1000, "Minimum Delay (ms)", kBottomMargin,
                          kTopMargin);
  plot->SetTitle("Set Minimum Delay");
}

// For audio SSRCs, plot the audio level.
void EventLogAnalyzer::CreateAudioLevelGraph(PacketDirection direction,
                                             Plot* plot) {
  for (const auto& stream : parsed_log_.rtp_packets_by_ssrc(direction)) {
    if (!IsAudioSsrc(parsed_log_, direction, stream.ssrc))
      continue;
    TimeSeries time_series(GetStreamName(parsed_log_, direction, stream.ssrc),
                           LineStyle::kLine);
    for (auto& packet : stream.packet_view) {
      if (packet.header.extension.audio_level()) {
        float x = config_.GetCallTimeSec(packet.log_time());
        // The audio level is stored in -dBov (so e.g. -10 dBov is stored as 10)
        // Here we convert it to dBov.
        float y =
            static_cast<float>(-packet.header.extension.audio_level()->level());
        time_series.points.emplace_back(TimeSeriesPoint(x, y));
      }
    }
    plot->AppendTimeSeries(std::move(time_series));
  }

  plot->SetXAxis(config_.CallBeginTimeSec(), config_.CallEndTimeSec(),
                 "Time (s)", kLeftMargin, kRightMargin);
  plot->SetYAxis(-127, 0, "Audio level (dBov)", kBottomMargin, kTopMargin);
  plot->SetTitle(GetDirectionAsString(direction) + " audio level");
}

// For each SSRC, plot the sequence number difference between consecutive
// incoming packets.
void EventLogAnalyzer::CreateSequenceNumberGraph(Plot* plot) {
  for (const auto& stream : parsed_log_.incoming_rtp_packets_by_ssrc()) {
    // Filter on SSRC.
    if (!MatchingSsrc(stream.ssrc, desired_ssrc_)) {
      continue;
    }

    TimeSeries time_series(
        GetStreamName(parsed_log_, kIncomingPacket, stream.ssrc),
        LineStyle::kBar);
    auto GetSequenceNumberDiff = [](const LoggedRtpPacketIncoming& old_packet,
                                    const LoggedRtpPacketIncoming& new_packet) {
      int64_t diff =
          WrappingDifference(new_packet.rtp.header.sequenceNumber,
                             old_packet.rtp.header.sequenceNumber, 1ul << 16);
      return diff;
    };
    auto ToCallTime = [this](const LoggedRtpPacketIncoming& packet) {
      return this->config_.GetCallTimeSec(packet.log_time());
    };
    ProcessPairs<LoggedRtpPacketIncoming, float>(
        ToCallTime, GetSequenceNumberDiff, stream.incoming_packets,
        &time_series);
    plot->AppendTimeSeries(std::move(time_series));
  }

  plot->SetXAxis(config_.CallBeginTimeSec(), config_.CallEndTimeSec(),
                 "Time (s)", kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 1, "Difference since last packet", kBottomMargin,
                          kTopMargin);
  plot->SetTitle("Incoming sequence number delta");
}

void EventLogAnalyzer::CreateIncomingPacketLossGraph(Plot* plot) {
  for (const auto& stream : parsed_log_.incoming_rtp_packets_by_ssrc()) {
    const std::vector<LoggedRtpPacketIncoming>& packets =
        stream.incoming_packets;
    // Filter on SSRC.
    if (!MatchingSsrc(stream.ssrc, desired_ssrc_) || packets.empty()) {
      continue;
    }

    TimeSeries time_series(
        GetStreamName(parsed_log_, kIncomingPacket, stream.ssrc),
        LineStyle::kLine, PointStyle::kHighlight);
    // TODO(terelius): Should the window and step size be read from the class
    // instead?
    const TimeDelta kWindow = TimeDelta::Millis(1000);
    const TimeDelta kStep = TimeDelta::Millis(1000);
    SeqNumUnwrapper<uint16_t> unwrapper_;
    SeqNumUnwrapper<uint16_t> prior_unwrapper_;
    size_t window_index_begin = 0;
    size_t window_index_end = 0;
    uint64_t highest_seq_number =
        unwrapper_.Unwrap(packets[0].rtp.header.sequenceNumber) - 1;
    uint64_t highest_prior_seq_number =
        prior_unwrapper_.Unwrap(packets[0].rtp.header.sequenceNumber) - 1;

    for (Timestamp t = config_.begin_time_; t < config_.end_time_ + kStep;
         t += kStep) {
      while (window_index_end < packets.size() &&
             packets[window_index_end].rtp.log_time() < t) {
        uint64_t sequence_number = unwrapper_.Unwrap(
            packets[window_index_end].rtp.header.sequenceNumber);
        highest_seq_number = std::max(highest_seq_number, sequence_number);
        ++window_index_end;
      }
      while (window_index_begin < packets.size() &&
             packets[window_index_begin].rtp.log_time() < t - kWindow) {
        uint64_t sequence_number = prior_unwrapper_.Unwrap(
            packets[window_index_begin].rtp.header.sequenceNumber);
        highest_prior_seq_number =
            std::max(highest_prior_seq_number, sequence_number);
        ++window_index_begin;
      }
      float x = config_.GetCallTimeSec(t);
      uint64_t expected_packets = highest_seq_number - highest_prior_seq_number;
      if (expected_packets > 0) {
        int64_t received_packets = window_index_end - window_index_begin;
        int64_t lost_packets = expected_packets - received_packets;
        float y = static_cast<float>(lost_packets) / expected_packets * 100;
        time_series.points.emplace_back(x, y);
      }
    }
    plot->AppendTimeSeries(std::move(time_series));
  }

  plot->SetXAxis(config_.CallBeginTimeSec(), config_.CallEndTimeSec(),
                 "Time (s)", kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 1, "Loss rate (in %)", kBottomMargin, kTopMargin);
  plot->SetTitle("Incoming packet loss (derived from incoming packets)");
}

void EventLogAnalyzer::CreateIncomingDelayGraph(Plot* plot) {
  for (const auto& stream : parsed_log_.incoming_rtp_packets_by_ssrc()) {
    // Filter on SSRC.
    if (!MatchingSsrc(stream.ssrc, desired_ssrc_) ||
        IsRtxSsrc(parsed_log_, kIncomingPacket, stream.ssrc)) {
      continue;
    }

    const std::vector<LoggedRtpPacketIncoming>& packets =
        stream.incoming_packets;
    if (packets.size() < 100) {
      RTC_LOG(LS_WARNING) << "Can't estimate the RTP clock frequency with "
                          << packets.size() << " packets in the stream.";
      continue;
    }
    int64_t segment_end_us = parsed_log_.first_log_segment().stop_time_us();
    absl::optional<uint32_t> estimated_frequency =
        EstimateRtpClockFrequency(packets, segment_end_us);
    if (!estimated_frequency)
      continue;
    const double frequency_hz = *estimated_frequency;
    if (IsVideoSsrc(parsed_log_, kIncomingPacket, stream.ssrc) &&
        frequency_hz != 90000) {
      RTC_LOG(LS_WARNING)
          << "Video stream should use a 90 kHz clock but appears to use "
          << frequency_hz / 1000 << ". Discarding.";
      continue;
    }

    auto ToCallTime = [this](const LoggedRtpPacketIncoming& packet) {
      return this->config_.GetCallTimeSec(packet.log_time());
    };
    auto ToNetworkDelay = [frequency_hz](
                              const LoggedRtpPacketIncoming& old_packet,
                              const LoggedRtpPacketIncoming& new_packet) {
      return NetworkDelayDiff_CaptureTime(old_packet, new_packet, frequency_hz);
    };

    TimeSeries capture_time_data(
        GetStreamName(parsed_log_, kIncomingPacket, stream.ssrc) +
            " capture-time",
        LineStyle::kLine);
    AccumulatePairs<LoggedRtpPacketIncoming, double>(
        ToCallTime, ToNetworkDelay, packets, &capture_time_data);
    plot->AppendTimeSeries(std::move(capture_time_data));

    TimeSeries send_time_data(
        GetStreamName(parsed_log_, kIncomingPacket, stream.ssrc) +
            " abs-send-time",
        LineStyle::kLine);
    AccumulatePairs<LoggedRtpPacketIncoming, double>(
        ToCallTime, NetworkDelayDiff_AbsSendTime, packets, &send_time_data);
    plot->AppendTimeSeriesIfNotEmpty(std::move(send_time_data));
  }

  plot->SetXAxis(config_.CallBeginTimeSec(), config_.CallEndTimeSec(),
                 "Time (s)", kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 1, "Delay (ms)", kBottomMargin, kTopMargin);
  plot->SetTitle("Incoming network delay (relative to first packet)");
}

// Plot the fraction of packets lost (as perceived by the loss-based BWE).
void EventLogAnalyzer::CreateFractionLossGraph(Plot* plot) {
  TimeSeries time_series("Fraction lost", LineStyle::kLine,
                         PointStyle::kHighlight);
  for (auto& bwe_update : parsed_log_.bwe_loss_updates()) {
    float x = config_.GetCallTimeSec(bwe_update.log_time());
    float y = static_cast<float>(bwe_update.fraction_lost) / 255 * 100;
    time_series.points.emplace_back(x, y);
  }

  plot->AppendTimeSeries(std::move(time_series));
  plot->SetXAxis(config_.CallBeginTimeSec(), config_.CallEndTimeSec(),
                 "Time (s)", kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 10, "Loss rate (in %)", kBottomMargin, kTopMargin);
  plot->SetTitle("Outgoing packet loss (as reported by BWE)");
}

// Plot the total bandwidth used by all RTP streams.
void EventLogAnalyzer::CreateTotalIncomingBitrateGraph(Plot* plot) {
  // TODO(terelius): This could be provided by the parser.
  std::multimap<Timestamp, size_t> packets_in_order;
  for (const auto& stream : parsed_log_.incoming_rtp_packets_by_ssrc()) {
    for (const LoggedRtpPacketIncoming& packet : stream.incoming_packets)
      packets_in_order.insert(
          std::make_pair(packet.rtp.log_time(), packet.rtp.total_length));
  }

  auto window_begin = packets_in_order.begin();
  auto window_end = packets_in_order.begin();
  size_t bytes_in_window = 0;

  if (!packets_in_order.empty()) {
    // Calculate a moving average of the bitrate and store in a TimeSeries.
    TimeSeries bitrate_series("Bitrate", LineStyle::kLine);
    for (Timestamp time = config_.begin_time_;
         time < config_.end_time_ + config_.step_; time += config_.step_) {
      while (window_end != packets_in_order.end() && window_end->first < time) {
        bytes_in_window += window_end->second;
        ++window_end;
      }
      while (window_begin != packets_in_order.end() &&
             window_begin->first < time - config_.window_duration_) {
        RTC_DCHECK_LE(window_begin->second, bytes_in_window);
        bytes_in_window -= window_begin->second;
        ++window_begin;
      }
      float window_duration_in_seconds =
          static_cast<float>(config_.window_duration_.us()) /
          kNumMicrosecsPerSec;
      float x = config_.GetCallTimeSec(time);
      float y = bytes_in_window * 8 / window_duration_in_seconds / 1000;
      bitrate_series.points.emplace_back(x, y);
    }
    plot->AppendTimeSeries(std::move(bitrate_series));
  }

  // Overlay the outgoing REMB over incoming bitrate.
  TimeSeries remb_series("Remb", LineStyle::kStep);
  for (const auto& rtcp : parsed_log_.rembs(kOutgoingPacket)) {
    float x = config_.GetCallTimeSec(rtcp.log_time());
    float y = static_cast<float>(rtcp.remb.bitrate_bps()) / 1000;
    remb_series.points.emplace_back(x, y);
  }
  plot->AppendTimeSeriesIfNotEmpty(std::move(remb_series));

  if (!parsed_log_.generic_packets_received().empty()) {
    TimeSeries time_series("Incoming generic bitrate", LineStyle::kLine);
    auto GetPacketSizeKilobits = [](const LoggedGenericPacketReceived& packet) {
      return packet.packet_length * 8.0 / 1000.0;
    };
    MovingAverage<LoggedGenericPacketReceived, double>(
        GetPacketSizeKilobits, parsed_log_.generic_packets_received(), config_,
        &time_series);
    plot->AppendTimeSeries(std::move(time_series));
  }

  plot->SetXAxis(config_.CallBeginTimeSec(), config_.CallEndTimeSec(),
                 "Time (s)", kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 1, "Bitrate (kbps)", kBottomMargin, kTopMargin);
  plot->SetTitle("Incoming RTP bitrate");
}

// Plot the total bandwidth used by all RTP streams.
void EventLogAnalyzer::CreateTotalOutgoingBitrateGraph(
    Plot* plot,
    bool show_detector_state,
    bool show_alr_state,
    bool show_link_capacity) {
  // TODO(terelius): This could be provided by the parser.
  std::multimap<Timestamp, size_t> packets_in_order;
  for (const auto& stream : parsed_log_.outgoing_rtp_packets_by_ssrc()) {
    for (const LoggedRtpPacketOutgoing& packet : stream.outgoing_packets)
      packets_in_order.insert(
          std::make_pair(packet.rtp.log_time(), packet.rtp.total_length));
  }

  auto window_begin = packets_in_order.begin();
  auto window_end = packets_in_order.begin();
  size_t bytes_in_window = 0;

  if (!packets_in_order.empty()) {
    // Calculate a moving average of the bitrate and store in a TimeSeries.
    TimeSeries bitrate_series("Bitrate", LineStyle::kLine);
    for (Timestamp time = config_.begin_time_;
         time < config_.end_time_ + config_.step_; time += config_.step_) {
      while (window_end != packets_in_order.end() && window_end->first < time) {
        bytes_in_window += window_end->second;
        ++window_end;
      }
      while (window_begin != packets_in_order.end() &&
             window_begin->first < time - config_.window_duration_) {
        RTC_DCHECK_LE(window_begin->second, bytes_in_window);
        bytes_in_window -= window_begin->second;
        ++window_begin;
      }
      float window_duration_in_seconds =
          static_cast<float>(config_.window_duration_.us()) /
          kNumMicrosecsPerSec;
      float x = config_.GetCallTimeSec(time);
      float y = bytes_in_window * 8 / window_duration_in_seconds / 1000;
      bitrate_series.points.emplace_back(x, y);
    }
    plot->AppendTimeSeries(std::move(bitrate_series));
  }

  // Overlay the send-side bandwidth estimate over the outgoing bitrate.
  TimeSeries loss_series("Loss-based estimate", LineStyle::kStep);
  for (auto& loss_update : parsed_log_.bwe_loss_updates()) {
    float x = config_.GetCallTimeSec(loss_update.log_time());
    float y = static_cast<float>(loss_update.bitrate_bps) / 1000;
    loss_series.points.emplace_back(x, y);
  }

  TimeSeries link_capacity_lower_series("Link-capacity-lower",
                                        LineStyle::kStep);
  TimeSeries link_capacity_upper_series("Link-capacity-upper",
                                        LineStyle::kStep);
  for (auto& remote_estimate_event : parsed_log_.remote_estimate_events()) {
    float x = config_.GetCallTimeSec(remote_estimate_event.log_time());
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

  for (auto& delay_update : parsed_log_.bwe_delay_updates()) {
    float x = config_.GetCallTimeSec(delay_update.log_time());
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
                                      config_.CallEndTimeSec());

  TimeSeries created_series("Probe cluster created.", LineStyle::kNone,
                            PointStyle::kHighlight);
  for (auto& cluster : parsed_log_.bwe_probe_cluster_created_events()) {
    float x = config_.GetCallTimeSec(cluster.log_time());
    float y = static_cast<float>(cluster.bitrate_bps) / 1000;
    created_series.points.emplace_back(x, y);
  }

  TimeSeries result_series("Probing results.", LineStyle::kNone,
                           PointStyle::kHighlight);
  for (auto& result : parsed_log_.bwe_probe_success_events()) {
    float x = config_.GetCallTimeSec(result.log_time());
    float y = static_cast<float>(result.bitrate_bps) / 1000;
    result_series.points.emplace_back(x, y);
  }

  TimeSeries probe_failures_series("Probe failed", LineStyle::kNone,
                                   PointStyle::kHighlight);
  for (auto& failure : parsed_log_.bwe_probe_failure_events()) {
    float x = config_.GetCallTimeSec(failure.log_time());
    probe_failures_series.points.emplace_back(x, 0);
  }

  IntervalSeries alr_state("ALR", "#555555", IntervalSeries::kHorizontal);
  bool previously_in_alr = false;
  Timestamp alr_start = Timestamp::Zero();
  for (auto& alr : parsed_log_.alr_state_events()) {
    float y = config_.GetCallTimeSec(alr.log_time());
    if (!previously_in_alr && alr.in_alr) {
      alr_start = alr.log_time();
      previously_in_alr = true;
    } else if (previously_in_alr && !alr.in_alr) {
      float x = config_.GetCallTimeSec(alr_start);
      alr_state.intervals.emplace_back(x, y);
      previously_in_alr = false;
    }
  }

  if (previously_in_alr) {
    float x = config_.GetCallTimeSec(alr_start);
    float y = config_.GetCallTimeSec(config_.end_time_);
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
  plot->AppendTimeSeries(std::move(created_series));
  plot->AppendTimeSeries(std::move(result_series));

  // Overlay the incoming REMB over the outgoing bitrate.
  TimeSeries remb_series("Remb", LineStyle::kStep);
  for (const auto& rtcp : parsed_log_.rembs(kIncomingPacket)) {
    float x = config_.GetCallTimeSec(rtcp.log_time());
    float y = static_cast<float>(rtcp.remb.bitrate_bps()) / 1000;
    remb_series.points.emplace_back(x, y);
  }
  plot->AppendTimeSeriesIfNotEmpty(std::move(remb_series));

  if (!parsed_log_.generic_packets_sent().empty()) {
    {
      TimeSeries time_series("Outgoing generic total bitrate",
                             LineStyle::kLine);
      auto GetPacketSizeKilobits = [](const LoggedGenericPacketSent& packet) {
        return packet.packet_length() * 8.0 / 1000.0;
      };
      MovingAverage<LoggedGenericPacketSent, double>(
          GetPacketSizeKilobits, parsed_log_.generic_packets_sent(), config_,
          &time_series);
      plot->AppendTimeSeries(std::move(time_series));
    }

    {
      TimeSeries time_series("Outgoing generic payload bitrate",
                             LineStyle::kLine);
      auto GetPacketSizeKilobits = [](const LoggedGenericPacketSent& packet) {
        return packet.payload_length * 8.0 / 1000.0;
      };
      MovingAverage<LoggedGenericPacketSent, double>(
          GetPacketSizeKilobits, parsed_log_.generic_packets_sent(), config_,
          &time_series);
      plot->AppendTimeSeries(std::move(time_series));
    }
  }

  plot->SetXAxis(config_.CallBeginTimeSec(), config_.CallEndTimeSec(),
                 "Time (s)", kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 1, "Bitrate (kbps)", kBottomMargin, kTopMargin);
  plot->SetTitle("Outgoing RTP bitrate");
}

// For each SSRC, plot the bandwidth used by that stream.
void EventLogAnalyzer::CreateStreamBitrateGraph(PacketDirection direction,
                                                Plot* plot) {
  for (const auto& stream : parsed_log_.rtp_packets_by_ssrc(direction)) {
    // Filter on SSRC.
    if (!MatchingSsrc(stream.ssrc, desired_ssrc_)) {
      continue;
    }

    TimeSeries time_series(GetStreamName(parsed_log_, direction, stream.ssrc),
                           LineStyle::kLine);
    auto GetPacketSizeKilobits = [](const LoggedRtpPacket& packet) {
      return packet.total_length * 8.0 / 1000.0;
    };
    MovingAverage<LoggedRtpPacket, double>(
        GetPacketSizeKilobits, stream.packet_view, config_, &time_series);
    plot->AppendTimeSeries(std::move(time_series));
  }

  plot->SetXAxis(config_.CallBeginTimeSec(), config_.CallEndTimeSec(),
                 "Time (s)", kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 1, "Bitrate (kbps)", kBottomMargin, kTopMargin);
  plot->SetTitle(GetDirectionAsString(direction) + " bitrate per stream");
}

// Plot the bitrate allocation for each temporal and spatial layer.
// Computed from RTCP XR target bitrate block, so the graph is only populated if
// those are sent.
void EventLogAnalyzer::CreateBitrateAllocationGraph(PacketDirection direction,
                                                    Plot* plot) {
  std::map<LayerDescription, TimeSeries> time_series;
  const auto& xr_list = parsed_log_.extended_reports(direction);
  for (const auto& rtcp : xr_list) {
    const absl::optional<rtcp::TargetBitrate>& target_bitrate =
        rtcp.xr.target_bitrate();
    if (!target_bitrate.has_value())
      continue;
    for (const auto& bitrate_item : target_bitrate->GetTargetBitrates()) {
      LayerDescription layer(rtcp.xr.sender_ssrc(), bitrate_item.spatial_layer,
                             bitrate_item.temporal_layer);
      auto time_series_it = time_series.find(layer);
      if (time_series_it == time_series.end()) {
        std::string layer_name = GetLayerName(layer);
        bool inserted;
        std::tie(time_series_it, inserted) = time_series.insert(
            std::make_pair(layer, TimeSeries(layer_name, LineStyle::kStep)));
        RTC_DCHECK(inserted);
      }
      float x = config_.GetCallTimeSec(rtcp.log_time());
      float y = bitrate_item.target_bitrate_kbps;
      time_series_it->second.points.emplace_back(x, y);
    }
  }
  for (auto& layer : time_series) {
    plot->AppendTimeSeries(std::move(layer.second));
  }
  plot->SetXAxis(config_.CallBeginTimeSec(), config_.CallEndTimeSec(),
                 "Time (s)", kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 1, "Bitrate (kbps)", kBottomMargin, kTopMargin);
  if (direction == kIncomingPacket)
    plot->SetTitle("Target bitrate per incoming layer");
  else
    plot->SetTitle("Target bitrate per outgoing layer");
}

void EventLogAnalyzer::CreateGoogCcSimulationGraph(Plot* plot) {
  TimeSeries target_rates("Simulated target rate", LineStyle::kStep,
                          PointStyle::kHighlight);
  TimeSeries delay_based("Logged delay-based estimate", LineStyle::kStep,
                         PointStyle::kHighlight);
  TimeSeries loss_based("Logged loss-based estimate", LineStyle::kStep,
                        PointStyle::kHighlight);
  TimeSeries probe_results("Logged probe success", LineStyle::kNone,
                           PointStyle::kHighlight);

  LogBasedNetworkControllerSimulation simulation(
      std::make_unique<GoogCcNetworkControllerFactory>(),
      [&](const NetworkControlUpdate& update, Timestamp at_time) {
        if (update.target_rate) {
          target_rates.points.emplace_back(
              config_.GetCallTimeSec(at_time),
              update.target_rate->target_rate.kbps<float>());
        }
      });

  simulation.ProcessEventsInLog(parsed_log_);
  for (const auto& logged : parsed_log_.bwe_delay_updates())
    delay_based.points.emplace_back(config_.GetCallTimeSec(logged.log_time()),
                                    logged.bitrate_bps / 1000);
  for (const auto& logged : parsed_log_.bwe_probe_success_events())
    probe_results.points.emplace_back(config_.GetCallTimeSec(logged.log_time()),
                                      logged.bitrate_bps / 1000);
  for (const auto& logged : parsed_log_.bwe_loss_updates())
    loss_based.points.emplace_back(config_.GetCallTimeSec(logged.log_time()),
                                   logged.bitrate_bps / 1000);

  plot->AppendTimeSeries(std::move(delay_based));
  plot->AppendTimeSeries(std::move(loss_based));
  plot->AppendTimeSeries(std::move(probe_results));
  plot->AppendTimeSeries(std::move(target_rates));

  plot->SetXAxis(config_.CallBeginTimeSec(), config_.CallEndTimeSec(),
                 "Time (s)", kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 10, "Bitrate (kbps)", kBottomMargin, kTopMargin);
  plot->SetTitle("Simulated BWE behavior");
}

void EventLogAnalyzer::CreateOutgoingTWCCLossRateGraph(Plot* plot) {
  TimeSeries loss_rate_series("Loss rate (from packet feedback)",
                              LineStyle::kLine, PointStyle::kHighlight);
  TimeSeries average_loss_rate_series("Average loss rate last 5s",
                                      LineStyle::kLine, PointStyle::kHighlight);
  TimeSeries missing_feedback_series("Missing feedback", LineStyle::kNone,
                                     PointStyle::kHighlight);
  PacketLossSummary window_summary;
  Timestamp last_observation_receive_time = Timestamp::Zero();

  // Use loss based bwe 2 observation duration and observation window size.
  constexpr TimeDelta kObservationDuration = TimeDelta::Millis(250);
  constexpr uint32_t kObservationWindowSize = 20;
  std::deque<PacketLossSummary> observations;
  SeqNumUnwrapper<uint16_t> unwrapper;
  int64_t last_acked = 1;
  if (!parsed_log_.transport_feedbacks(kIncomingPacket).empty()) {
    last_acked =
        unwrapper.Unwrap(parsed_log_.transport_feedbacks(kIncomingPacket)[0]
                             .transport_feedback.GetBaseSequence());
  }
  for (auto& feedback : parsed_log_.transport_feedbacks(kIncomingPacket)) {
    const rtcp::TransportFeedback& transport_feedback =
        feedback.transport_feedback;
    size_t base_seq_num =
        unwrapper.Unwrap(transport_feedback.GetBaseSequence());
    // Collect packets that do not have feedback, which are from the last acked
    // packet, to the current base packet.
    for (size_t seq_num = last_acked; seq_num < base_seq_num; ++seq_num) {
      missing_feedback_series.points.emplace_back(
          config_.GetCallTimeSec(feedback.timestamp),
          100 + seq_num - last_acked);
    }
    last_acked = base_seq_num + transport_feedback.GetPacketStatusCount();

    // Compute loss rate from the transport feedback.
    auto loss_rate =
        static_cast<float>((transport_feedback.GetPacketStatusCount() -
                            transport_feedback.GetReceivedPackets().size()) *
                           100.0 / transport_feedback.GetPacketStatusCount());
    loss_rate_series.points.emplace_back(
        config_.GetCallTimeSec(feedback.timestamp), loss_rate);

    // Compute loss rate in a window of kObservationWindowSize.
    if (window_summary.num_packets == 0) {
      window_summary.base_time = feedback.log_time();
    }
    window_summary.num_packets += transport_feedback.GetPacketStatusCount();
    window_summary.num_lost_packets +=
        transport_feedback.GetPacketStatusCount() -
        transport_feedback.GetReceivedPackets().size();

    const Timestamp last_received_time = feedback.log_time();
    const TimeDelta observation_duration =
        window_summary.base_time == Timestamp::Zero()
            ? TimeDelta::Zero()
            : last_received_time - window_summary.base_time;
    if (observation_duration > kObservationDuration) {
      last_observation_receive_time = last_received_time;
      observations.push_back(window_summary);
      if (observations.size() > kObservationWindowSize) {
        observations.pop_front();
      }

      // Compute average loss rate in a number of windows.
      int total_packets = 0;
      int total_loss = 0;
      for (const auto& observation : observations) {
        total_loss += observation.num_lost_packets;
        total_packets += observation.num_packets;
      }
      if (total_packets > 0) {
        float average_loss_rate = total_loss * 100.0 / total_packets;
        average_loss_rate_series.points.emplace_back(
            config_.GetCallTimeSec(feedback.timestamp), average_loss_rate);
      } else {
        average_loss_rate_series.points.emplace_back(
            config_.GetCallTimeSec(feedback.timestamp), 0);
      }
      window_summary = PacketLossSummary();
    }
  }
  // Add the data set to the plot.
  plot->AppendTimeSeriesIfNotEmpty(std::move(loss_rate_series));
  plot->AppendTimeSeriesIfNotEmpty(std::move(average_loss_rate_series));
  plot->AppendTimeSeriesIfNotEmpty(std::move(missing_feedback_series));

  plot->SetXAxis(config_.CallBeginTimeSec(), config_.CallEndTimeSec(),
                 "Time (s)", kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 100, "Loss rate (percent)", kBottomMargin,
                          kTopMargin);
  plot->SetTitle("Outgoing loss rate (from TWCC feedback)");
}

void EventLogAnalyzer::CreateSendSideBweSimulationGraph(Plot* plot) {
  using RtpPacketType = LoggedRtpPacketOutgoing;
  using TransportFeedbackType = LoggedRtcpPacketTransportFeedback;

  // TODO(terelius): This could be provided by the parser.
  std::multimap<int64_t, const RtpPacketType*> outgoing_rtp;
  for (const auto& stream : parsed_log_.outgoing_rtp_packets_by_ssrc()) {
    for (const RtpPacketType& rtp_packet : stream.outgoing_packets)
      outgoing_rtp.insert(
          std::make_pair(rtp_packet.rtp.log_time_us(), &rtp_packet));
  }

  const std::vector<TransportFeedbackType>& incoming_rtcp =
      parsed_log_.transport_feedbacks(kIncomingPacket);

  SimulatedClock clock(0);
  BitrateObserver observer;
  RtcEventLogNull null_event_log;
  TransportFeedbackAdapter transport_feedback;
  auto factory = GoogCcNetworkControllerFactory();
  TimeDelta process_interval = factory.GetProcessInterval();
  // TODO(holmer): Log the call config and use that here instead.
  static const uint32_t kDefaultStartBitrateBps = 300000;
  NetworkControllerConfig cc_config;
  cc_config.constraints.at_time = Timestamp::Micros(clock.TimeInMicroseconds());
  cc_config.constraints.starting_rate =
      DataRate::BitsPerSec(kDefaultStartBitrateBps);
  cc_config.event_log = &null_event_log;
  auto goog_cc = factory.Create(cc_config);

  TimeSeries time_series("Delay-based estimate", LineStyle::kStep,
                         PointStyle::kHighlight);
  TimeSeries acked_time_series("Raw acked bitrate", LineStyle::kLine,
                               PointStyle::kHighlight);
  TimeSeries robust_time_series("Robust throughput estimate", LineStyle::kLine,
                                PointStyle::kHighlight);
  TimeSeries acked_estimate_time_series("Ackednowledged bitrate estimate",
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
  test::ExplicitKeyValueConfig throughput_config(
      "WebRTC-Bwe-RobustThroughputEstimatorSettings/enabled:true/");
  std::unique_ptr<AcknowledgedBitrateEstimatorInterface>
      robust_throughput_estimator(
          AcknowledgedBitrateEstimatorInterface::Create(&throughput_config));
  test::ExplicitKeyValueConfig acked_bitrate_config(
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
        RtpPacketSendInfo packet_info;
        packet_info.media_ssrc = rtp_packet.rtp.header.ssrc;
        packet_info.transport_sequence_number =
            rtp_packet.rtp.header.extension.transportSequenceNumber;
        packet_info.rtp_sequence_number = rtp_packet.rtp.header.sequenceNumber;
        packet_info.length = rtp_packet.rtp.total_length;
        if (IsRtxSsrc(parsed_log_, PacketDirection::kOutgoingPacket,
                      rtp_packet.rtp.header.ssrc)) {
          // Don't set the optional media type as we don't know if it is
          // a retransmission, FEC or padding.
        } else if (IsVideoSsrc(parsed_log_, PacketDirection::kOutgoingPacket,
                               rtp_packet.rtp.header.ssrc)) {
          packet_info.packet_type = RtpPacketMediaType::kVideo;
        } else if (IsAudioSsrc(parsed_log_, PacketDirection::kOutgoingPacket,
                               rtp_packet.rtp.header.ssrc)) {
          packet_info.packet_type = RtpPacketMediaType::kAudio;
        }
        transport_feedback.AddPacket(
            packet_info,
            0u,  // Per packet overhead bytes.
            Timestamp::Micros(rtp_packet.rtp.log_time_us()));
      }
      rtc::SentPacket sent_packet;
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
          rtcp_iterator->transport_feedback,
          Timestamp::Millis(clock.TimeInMilliseconds()));
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
          absl::optional<uint32_t> raw_bitrate_bps =
              raw_acked_bitrate.Rate(feedback.back().receive_time.ms());
          float x = config_.GetCallTimeSec(clock.CurrentTime());
          if (raw_bitrate_bps) {
            float y = raw_bitrate_bps.value() / 1000;
            acked_time_series.points.emplace_back(x, y);
          }
          absl::optional<DataRate> robust_estimate =
              robust_throughput_estimator->bitrate();
          if (robust_estimate) {
            float y = robust_estimate.value().kbps();
            robust_time_series.points.emplace_back(x, y);
          }
          absl::optional<DataRate> acked_estimate =
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
      msg.at_time = Timestamp::Micros(clock.TimeInMicroseconds());
      observer.Update(goog_cc->OnProcessInterval(msg));
      next_process_time_us_ += process_interval.us();
    }
    if (observer.GetAndResetBitrateUpdated() ||
        time_us - last_update_us >= 1e6) {
      uint32_t y = observer.last_bitrate_bps() / 1000;
      float x = config_.GetCallTimeSec(clock.CurrentTime());
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

  plot->SetXAxis(config_.CallBeginTimeSec(), config_.CallEndTimeSec(),
                 "Time (s)", kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 10, "Bitrate (kbps)", kBottomMargin, kTopMargin);
  plot->SetTitle("Simulated send-side BWE behavior");
}

void EventLogAnalyzer::CreateReceiveSideBweSimulationGraph(Plot* plot) {
  using RtpPacketType = LoggedRtpPacketIncoming;
  class RembInterceptor {
   public:
    void SendRemb(uint32_t bitrate_bps, std::vector<uint32_t> ssrcs) {
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

  for (const auto& stream : parsed_log_.incoming_rtp_packets_by_ssrc()) {
    if (IsVideoSsrc(parsed_log_, kIncomingPacket, stream.ssrc)) {
      for (const auto& rtp_packet : stream.incoming_packets)
        incoming_rtp.insert(
            std::make_pair(rtp_packet.rtp.log_time_us(), &rtp_packet));
    }
  }

  SimulatedClock clock(0);
  RembInterceptor remb_interceptor;
  ReceiveSideCongestionController rscc(
      &clock, [](auto...) {},
      absl::bind_front(&RembInterceptor::SendRemb, &remb_interceptor), nullptr);
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
    absl::optional<uint32_t> bitrate_bps = acked_bitrate.Rate(arrival_time_ms);
    if (bitrate_bps) {
      uint32_t y = *bitrate_bps / 1000;
      float x = config_.GetCallTimeSec(clock.CurrentTime());
      acked_time_series.points.emplace_back(x, y);
    }
    if (remb_interceptor.GetAndResetBitrateUpdated() ||
        clock.TimeInMicroseconds() - last_update_us >= 1e6) {
      uint32_t y = remb_interceptor.last_bitrate_bps() / 1000;
      float x = config_.GetCallTimeSec(clock.CurrentTime());
      time_series.points.emplace_back(x, y);
      last_update_us = clock.TimeInMicroseconds();
    }
  }
  // Add the data set to the plot.
  plot->AppendTimeSeries(std::move(time_series));
  plot->AppendTimeSeries(std::move(acked_time_series));

  plot->SetXAxis(config_.CallBeginTimeSec(), config_.CallEndTimeSec(),
                 "Time (s)", kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 10, "Bitrate (kbps)", kBottomMargin, kTopMargin);
  plot->SetTitle("Simulated receive-side BWE behavior");
}

void EventLogAnalyzer::CreateNetworkDelayFeedbackGraph(Plot* plot) {
  TimeSeries time_series("Network delay", LineStyle::kLine,
                         PointStyle::kHighlight);
  int64_t min_send_receive_diff_ms = std::numeric_limits<int64_t>::max();
  int64_t min_rtt_ms = std::numeric_limits<int64_t>::max();

  std::vector<MatchedSendArrivalTimes> matched_rtp_rtcp =
      GetNetworkTrace(parsed_log_);
  absl::c_stable_sort(matched_rtp_rtcp, [](const MatchedSendArrivalTimes& a,
                                           const MatchedSendArrivalTimes& b) {
    return a.feedback_arrival_time_ms < b.feedback_arrival_time_ms ||
           (a.feedback_arrival_time_ms == b.feedback_arrival_time_ms &&
            a.arrival_time_ms < b.arrival_time_ms);
  });
  for (const auto& packet : matched_rtp_rtcp) {
    if (packet.arrival_time_ms == MatchedSendArrivalTimes::kNotReceived)
      continue;
    float x = config_.GetCallTimeSecFromMs(packet.feedback_arrival_time_ms);
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

  plot->SetXAxis(config_.CallBeginTimeSec(), config_.CallEndTimeSec(),
                 "Time (s)", kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 10, "Delay (ms)", kBottomMargin, kTopMargin);
  plot->SetTitle("Outgoing network delay (based on per-packet feedback)");
}

void EventLogAnalyzer::CreatePacerDelayGraph(Plot* plot) {
  for (const auto& stream : parsed_log_.outgoing_rtp_packets_by_ssrc()) {
    const std::vector<LoggedRtpPacketOutgoing>& packets =
        stream.outgoing_packets;

    if (IsRtxSsrc(parsed_log_, kOutgoingPacket, stream.ssrc)) {
      continue;
    }

    if (packets.size() < 2) {
      RTC_LOG(LS_WARNING)
          << "Can't estimate a the RTP clock frequency or the "
             "pacer delay with less than 2 packets in the stream";
      continue;
    }
    int64_t segment_end_us = parsed_log_.first_log_segment().stop_time_us();
    absl::optional<uint32_t> estimated_frequency =
        EstimateRtpClockFrequency(packets, segment_end_us);
    if (!estimated_frequency)
      continue;
    if (IsVideoSsrc(parsed_log_, kOutgoingPacket, stream.ssrc) &&
        *estimated_frequency != 90000) {
      RTC_LOG(LS_WARNING)
          << "Video stream should use a 90 kHz clock but appears to use "
          << *estimated_frequency / 1000 << ". Discarding.";
      continue;
    }

    TimeSeries pacer_delay_series(
        GetStreamName(parsed_log_, kOutgoingPacket, stream.ssrc) + "(" +
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
      float x = config_.GetCallTimeSec(packet.rtp.log_time());
      float y = send_time_ms - capture_time_ms;
      pacer_delay_series.points.emplace_back(x, y);
    }
    plot->AppendTimeSeries(std::move(pacer_delay_series));
  }

  plot->SetXAxis(config_.CallBeginTimeSec(), config_.CallEndTimeSec(),
                 "Time (s)", kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 10, "Pacer delay (ms)", kBottomMargin, kTopMargin);
  plot->SetTitle(
      "Delay from capture to send time. (First packet normalized to 0.)");
}

void EventLogAnalyzer::CreateTimestampGraph(PacketDirection direction,
                                            Plot* plot) {
  for (const auto& stream : parsed_log_.rtp_packets_by_ssrc(direction)) {
    TimeSeries rtp_timestamps(
        GetStreamName(parsed_log_, direction, stream.ssrc) + " capture-time",
        LineStyle::kLine, PointStyle::kHighlight);
    for (const auto& packet : stream.packet_view) {
      float x = config_.GetCallTimeSec(packet.log_time());
      float y = packet.header.timestamp;
      rtp_timestamps.points.emplace_back(x, y);
    }
    plot->AppendTimeSeries(std::move(rtp_timestamps));

    TimeSeries rtcp_timestamps(
        GetStreamName(parsed_log_, direction, stream.ssrc) +
            " rtcp capture-time",
        LineStyle::kLine, PointStyle::kHighlight);
    // TODO(terelius): Why only sender reports?
    const auto& sender_reports = parsed_log_.sender_reports(direction);
    for (const auto& rtcp : sender_reports) {
      if (rtcp.sr.sender_ssrc() != stream.ssrc)
        continue;
      float x = config_.GetCallTimeSec(rtcp.log_time());
      float y = rtcp.sr.rtp_timestamp();
      rtcp_timestamps.points.emplace_back(x, y);
    }
    plot->AppendTimeSeriesIfNotEmpty(std::move(rtcp_timestamps));
  }

  plot->SetXAxis(config_.CallBeginTimeSec(), config_.CallEndTimeSec(),
                 "Time (s)", kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 1, "RTP timestamp", kBottomMargin, kTopMargin);
  plot->SetTitle(GetDirectionAsString(direction) + " timestamps");
}

void EventLogAnalyzer::CreateSenderAndReceiverReportPlot(
    PacketDirection direction,
    rtc::FunctionView<float(const rtcp::ReportBlock&)> fy,
    std::string title,
    std::string yaxis_label,
    Plot* plot) {
  std::map<uint32_t, TimeSeries> sr_reports_by_ssrc;
  const auto& sender_reports = parsed_log_.sender_reports(direction);
  for (const auto& rtcp : sender_reports) {
    float x = config_.GetCallTimeSec(rtcp.log_time());
    uint32_t ssrc = rtcp.sr.sender_ssrc();
    for (const auto& block : rtcp.sr.report_blocks()) {
      float y = fy(block);
      auto sr_report_it = sr_reports_by_ssrc.find(ssrc);
      bool inserted;
      if (sr_report_it == sr_reports_by_ssrc.end()) {
        std::tie(sr_report_it, inserted) = sr_reports_by_ssrc.emplace(
            ssrc, TimeSeries(GetStreamName(parsed_log_, direction, ssrc) +
                                 " Sender Reports",
                             LineStyle::kLine, PointStyle::kHighlight));
      }
      sr_report_it->second.points.emplace_back(x, y);
    }
  }
  for (auto& kv : sr_reports_by_ssrc) {
    plot->AppendTimeSeries(std::move(kv.second));
  }

  std::map<uint32_t, TimeSeries> rr_reports_by_ssrc;
  const auto& receiver_reports = parsed_log_.receiver_reports(direction);
  for (const auto& rtcp : receiver_reports) {
    float x = config_.GetCallTimeSec(rtcp.log_time());
    uint32_t ssrc = rtcp.rr.sender_ssrc();
    for (const auto& block : rtcp.rr.report_blocks()) {
      float y = fy(block);
      auto rr_report_it = rr_reports_by_ssrc.find(ssrc);
      bool inserted;
      if (rr_report_it == rr_reports_by_ssrc.end()) {
        std::tie(rr_report_it, inserted) = rr_reports_by_ssrc.emplace(
            ssrc, TimeSeries(GetStreamName(parsed_log_, direction, ssrc) +
                                 " Receiver Reports",
                             LineStyle::kLine, PointStyle::kHighlight));
      }
      rr_report_it->second.points.emplace_back(x, y);
    }
  }
  for (auto& kv : rr_reports_by_ssrc) {
    plot->AppendTimeSeries(std::move(kv.second));
  }

  plot->SetXAxis(config_.CallBeginTimeSec(), config_.CallEndTimeSec(),
                 "Time (s)", kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 1, yaxis_label, kBottomMargin, kTopMargin);
  plot->SetTitle(title);
}

void EventLogAnalyzer::CreateIceCandidatePairConfigGraph(Plot* plot) {
  std::map<uint32_t, TimeSeries> configs_by_cp_id;
  for (const auto& config : parsed_log_.ice_candidate_pair_configs()) {
    if (configs_by_cp_id.find(config.candidate_pair_id) ==
        configs_by_cp_id.end()) {
      const std::string candidate_pair_desc =
          GetCandidatePairLogDescriptionAsString(config);
      configs_by_cp_id[config.candidate_pair_id] =
          TimeSeries("[" + std::to_string(config.candidate_pair_id) + "]" +
                         candidate_pair_desc,
                     LineStyle::kNone, PointStyle::kHighlight);
      candidate_pair_desc_by_id_[config.candidate_pair_id] =
          candidate_pair_desc;
    }
    float x = config_.GetCallTimeSec(config.log_time());
    float y = static_cast<float>(config.type);
    configs_by_cp_id[config.candidate_pair_id].points.emplace_back(x, y);
  }

  // TODO(qingsi): There can be a large number of candidate pairs generated by
  // certain calls and the frontend cannot render the chart in this case due to
  // the failure of generating a palette with the same number of colors.
  for (auto& kv : configs_by_cp_id) {
    plot->AppendTimeSeries(std::move(kv.second));
  }

  plot->SetXAxis(config_.CallBeginTimeSec(), config_.CallEndTimeSec(),
                 "Time (s)", kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 3, "Config Type", kBottomMargin, kTopMargin);
  plot->SetTitle("[IceEventLog] ICE candidate pair configs");
  plot->SetYAxisTickLabels(
      {{static_cast<float>(IceCandidatePairConfigType::kAdded), "ADDED"},
       {static_cast<float>(IceCandidatePairConfigType::kUpdated), "UPDATED"},
       {static_cast<float>(IceCandidatePairConfigType::kDestroyed),
        "DESTROYED"},
       {static_cast<float>(IceCandidatePairConfigType::kSelected),
        "SELECTED"}});
}

std::string EventLogAnalyzer::GetCandidatePairLogDescriptionFromId(
    uint32_t candidate_pair_id) {
  if (candidate_pair_desc_by_id_.find(candidate_pair_id) !=
      candidate_pair_desc_by_id_.end()) {
    return candidate_pair_desc_by_id_[candidate_pair_id];
  }
  for (const auto& config : parsed_log_.ice_candidate_pair_configs()) {
    // TODO(qingsi): Add the handling of the "Updated" config event after the
    // visualization of property change for candidate pairs is introduced.
    if (candidate_pair_desc_by_id_.find(config.candidate_pair_id) ==
        candidate_pair_desc_by_id_.end()) {
      const std::string candidate_pair_desc =
          GetCandidatePairLogDescriptionAsString(config);
      candidate_pair_desc_by_id_[config.candidate_pair_id] =
          candidate_pair_desc;
    }
  }
  return candidate_pair_desc_by_id_[candidate_pair_id];
}

void EventLogAnalyzer::CreateIceConnectivityCheckGraph(Plot* plot) {
  constexpr int kEventTypeOffset =
      static_cast<int>(IceCandidatePairConfigType::kNumValues);
  std::map<uint32_t, TimeSeries> checks_by_cp_id;
  for (const auto& event : parsed_log_.ice_candidate_pair_events()) {
    if (checks_by_cp_id.find(event.candidate_pair_id) ==
        checks_by_cp_id.end()) {
      checks_by_cp_id[event.candidate_pair_id] = TimeSeries(
          "[" + std::to_string(event.candidate_pair_id) + "]" +
              GetCandidatePairLogDescriptionFromId(event.candidate_pair_id),
          LineStyle::kNone, PointStyle::kHighlight);
    }
    float x = config_.GetCallTimeSec(event.log_time());
    float y = static_cast<float>(event.type) + kEventTypeOffset;
    checks_by_cp_id[event.candidate_pair_id].points.emplace_back(x, y);
  }

  // TODO(qingsi): The same issue as in CreateIceCandidatePairConfigGraph.
  for (auto& kv : checks_by_cp_id) {
    plot->AppendTimeSeries(std::move(kv.second));
  }

  plot->SetXAxis(config_.CallBeginTimeSec(), config_.CallEndTimeSec(),
                 "Time (s)", kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 4, "Connectivity State", kBottomMargin,
                          kTopMargin);
  plot->SetTitle("[IceEventLog] ICE connectivity checks");

  plot->SetYAxisTickLabels(
      {{static_cast<float>(IceCandidatePairEventType::kCheckSent) +
            kEventTypeOffset,
        "CHECK SENT"},
       {static_cast<float>(IceCandidatePairEventType::kCheckReceived) +
            kEventTypeOffset,
        "CHECK RECEIVED"},
       {static_cast<float>(IceCandidatePairEventType::kCheckResponseSent) +
            kEventTypeOffset,
        "RESPONSE SENT"},
       {static_cast<float>(IceCandidatePairEventType::kCheckResponseReceived) +
            kEventTypeOffset,
        "RESPONSE RECEIVED"}});
}

void EventLogAnalyzer::CreateDtlsTransportStateGraph(Plot* plot) {
  TimeSeries states("DTLS Transport State", LineStyle::kNone,
                    PointStyle::kHighlight);
  for (const auto& event : parsed_log_.dtls_transport_states()) {
    float x = config_.GetCallTimeSec(event.log_time());
    float y = static_cast<float>(event.dtls_transport_state);
    states.points.emplace_back(x, y);
  }
  plot->AppendTimeSeries(std::move(states));
  plot->SetXAxis(config_.CallBeginTimeSec(), config_.CallEndTimeSec(),
                 "Time (s)", kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, static_cast<float>(DtlsTransportState::kNumValues),
                          "Transport State", kBottomMargin, kTopMargin);
  plot->SetTitle("DTLS Transport State");
  plot->SetYAxisTickLabels(
      {{static_cast<float>(DtlsTransportState::kNew), "NEW"},
       {static_cast<float>(DtlsTransportState::kConnecting), "CONNECTING"},
       {static_cast<float>(DtlsTransportState::kConnected), "CONNECTED"},
       {static_cast<float>(DtlsTransportState::kClosed), "CLOSED"},
       {static_cast<float>(DtlsTransportState::kFailed), "FAILED"}});
}

void EventLogAnalyzer::CreateDtlsWritableStateGraph(Plot* plot) {
  TimeSeries writable("DTLS Writable", LineStyle::kNone,
                      PointStyle::kHighlight);
  for (const auto& event : parsed_log_.dtls_writable_states()) {
    float x = config_.GetCallTimeSec(event.log_time());
    float y = static_cast<float>(event.writable);
    writable.points.emplace_back(x, y);
  }
  plot->AppendTimeSeries(std::move(writable));
  plot->SetXAxis(config_.CallBeginTimeSec(), config_.CallEndTimeSec(),
                 "Time (s)", kLeftMargin, kRightMargin);
  plot->SetSuggestedYAxis(0, 1, "Writable", kBottomMargin, kTopMargin);
  plot->SetTitle("DTLS Writable State");
}

}  // namespace webrtc
