/*
 *  Copyright (c) 2025 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "call/rtp_transport_controller_send.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "api/rtp_parameters.h"
#include "api/transport/ecn_marking.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "modules/congestion_controller/rtp/congestion_controller_feedback_stats.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtcp_packet/congestion_control_feedback.h"
#include "rtc_base/containers/flat_map.h"
#include "test/create_test_environment.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/run_loop.h"

namespace webrtc {
namespace {

using ::testing::AllOf;
using ::testing::Eq;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAre;

constexpr uint32_t kSsrc = 0x554c;

class PacketSender {
 public:
  explicit PacketSender(RtpTransportControllerSend& transport)
      : transport_(transport) {}

  struct SentPacketsOptions {
    uint32_t ssrc = kSsrc;
    uint16_t first_sequence_number = 1;
    int num_packets = 1;
    bool send_as_ect1 = true;
  };
  void SimulateSentPackets(SentPacketsOptions options) {
    uint16_t sequence_number = options.first_sequence_number;
    for (int i = 0; i < options.num_packets; ++i, ++sequence_number) {
      RtpPacketToSend rtp_packet(nullptr);
      rtp_packet.SetSsrc(options.ssrc);
      rtp_packet.SetSequenceNumber(sequence_number);
      rtp_packet.set_transport_sequence_number(++transport_sequence_number_);
      rtp_packet.set_packet_type(RtpPacketMediaType::kVideo);
      if (options.send_as_ect1) {
        rtp_packet.set_send_as_ect1();
      }
      rtp_packet.SetPayloadSize(100);
      transport_.NotifyBweOfSentPacketForTesting(rtp_packet);
    }
  }

 private:
  int64_t transport_sequence_number_ = 0;
  RtpTransportControllerSend& transport_;
};

struct FeedbakPacketTemplate {
  EcnMarking ecn = EcnMarking::kNotEct;
  // If absent, the SSRC defaults to the previous SSRC.
  std::optional<uint32_t> ssrc;
  // If absent, the sequence number defaults to the previous sequence number
  // plus one.
  std::optional<uint16_t> sequence_number;
  bool received = true;
};
rtcp::CongestionControlFeedback GenerateFeedback(
    std::vector<FeedbakPacketTemplate> packets) {
  std::vector<rtcp::CongestionControlFeedback::PacketInfo> packet_infos;
  packet_infos.reserve(packets.size());
  uint32_t ssrc = kSsrc;
  uint16_t sequence_number = 1;
  for (const FeedbakPacketTemplate& p : packets) {
    ssrc = p.ssrc.value_or(ssrc);
    sequence_number = p.sequence_number.value_or(sequence_number + 1);
    packet_infos.push_back(
        {.ssrc = ssrc,
         .sequence_number = sequence_number,
         .arrival_time_offset =
             p.received ? TimeDelta::Millis(10) : TimeDelta::MinusInfinity(),
         .ecn = p.ecn});
  }
  return rtcp::CongestionControlFeedback(std::move(packet_infos),
                                         /*report_timestamp_compact_ntp=*/0);
}

TEST(RtpTransportControllerSendTest,
     IgnoresFeedbackForReportedReceivedPacketThatWereNotSent) {
  test::RunLoop main_thread;
  RtpTransportControllerSend transport(
      {.env = CreateTestEnvironment(),
       .network_thread = main_thread.task_queue()});
  transport.SetPreferredRtcpCcAckType(RtcpFeedbackType::CCFB);
  PacketSender sender(transport);
  sender.SimulateSentPackets({.ssrc = 123,
                              .first_sequence_number = 111,
                              .num_packets = 10,
                              .send_as_ect1 = true});
  sender.SimulateSentPackets({.ssrc = 321,
                              .first_sequence_number = 10'111,
                              .num_packets = 10,
                              .send_as_ect1 = true});

  // Generate feedback for packets that weren't sent: reuse sequence number
  // range from 1st batch, but SSRC from the 2nd batch to double check sequence
  // numbers are checked per SSRC.
  transport.OnCongestionControlFeedback(
      /*receive_time=*/Timestamp::Seconds(123),
      GenerateFeedback(
          {{.ecn = EcnMarking::kEct1, .ssrc = 321, .sequence_number = 111},
           {.ecn = EcnMarking::kEct1},
           {.ecn = EcnMarking::kCe}}));
  EXPECT_THAT(transport.GetCongestionControlFeedbackStatsPerSsrc(), IsEmpty());
}

TEST(RtpTransportControllerSendTest,
     AccumulatesNumberOfReportedReceivedPacketsPerSsrcPerEcnMarkingType) {
  constexpr uint32_t kSsrc1 = 1'000;
  constexpr uint32_t kSsrc2 = 2'000;
  test::RunLoop main_thread;
  RtpTransportControllerSend transport(
      {.env = CreateTestEnvironment(),
       .network_thread = main_thread.task_queue()});
  transport.SetPreferredRtcpCcAckType(RtcpFeedbackType::CCFB);

  PacketSender sender(transport);
  sender.SimulateSentPackets(
      {.ssrc = kSsrc1, .first_sequence_number = 1, .num_packets = 10});
  sender.SimulateSentPackets(
      {.ssrc = kSsrc2, .first_sequence_number = 101, .num_packets = 10});

  transport.OnCongestionControlFeedback(
      /*receive_time=*/Timestamp::Seconds(123),
      GenerateFeedback(
          {{.ecn = EcnMarking::kEct1, .ssrc = kSsrc1, .sequence_number = 1},
           {.ecn = EcnMarking::kEct1},
           {.ecn = EcnMarking::kCe},
           {.ecn = EcnMarking::kEct1, .ssrc = kSsrc2, .sequence_number = 101},
           {.ecn = EcnMarking::kEct1},
           {.ecn = EcnMarking::kEct1},
           {.ecn = EcnMarking::kCe},
           {.ecn = EcnMarking::kCe},
           {.ecn = EcnMarking::kCe},
           {.ecn = EcnMarking::kCe}}));

  flat_map<uint32_t, ReceivedCongestionControlFeedbackStats> stats =
      transport.GetCongestionControlFeedbackStatsPerSsrc();
  EXPECT_EQ(stats[kSsrc1].num_packets_received_with_ect1, 2);
  EXPECT_EQ(stats[kSsrc1].num_packets_received_with_ce, 1);
  EXPECT_EQ(stats[kSsrc2].num_packets_received_with_ect1, 3);
  EXPECT_EQ(stats[kSsrc2].num_packets_received_with_ce, 4);

  transport.OnCongestionControlFeedback(
      /*receive_time=*/Timestamp::Seconds(123),
      GenerateFeedback(
          {{.ecn = EcnMarking::kEct1, .ssrc = kSsrc1, .sequence_number = 5},
           {.ecn = EcnMarking::kEct1},
           {.ecn = EcnMarking::kEct1},
           {.ecn = EcnMarking::kCe}}));

  stats = transport.GetCongestionControlFeedbackStatsPerSsrc();
  EXPECT_EQ(stats[kSsrc1].num_packets_received_with_ect1, 2 + 3);
  EXPECT_EQ(stats[kSsrc1].num_packets_received_with_ce, 1 + 1);
}

TEST(RtpTransportControllerSendTest, CalculatesNumberOfBleachedPackets) {
  test::RunLoop main_thread;
  RtpTransportControllerSend transport(
      {.env = CreateTestEnvironment(),
       .network_thread = main_thread.task_queue()});
  transport.SetPreferredRtcpCcAckType(RtcpFeedbackType::CCFB);
  PacketSender sender(transport);

  // Packets send as ect1 and received without ect1 are the bleached packets.
  sender.SimulateSentPackets(
      {.first_sequence_number = 1, .num_packets = 10, .send_as_ect1 = true});
  transport.OnCongestionControlFeedback(
      /*receive_time=*/Timestamp::Seconds(123),
      GenerateFeedback({{.ecn = EcnMarking::kNotEct, .sequence_number = 1},
                        {.ecn = EcnMarking::kNotEct},
                        {.ecn = EcnMarking::kEct1},
                        {.ecn = EcnMarking::kEct1},
                        {.ecn = EcnMarking::kEct1},
                        {.ecn = EcnMarking::kCe}}));
  EXPECT_EQ(transport.GetCongestionControlFeedbackStatsPerSsrc()[kSsrc]
                .num_packets_with_bleached_ect1_marking,
            2);

  // Packets not send as ect1 do not add to number of bleached packets.
  sender.SimulateSentPackets(
      {.first_sequence_number = 11, .num_packets = 10, .send_as_ect1 = false});
  transport.OnCongestionControlFeedback(
      /*receive_time=*/Timestamp::Seconds(123),
      GenerateFeedback({{.ecn = EcnMarking::kNotEct, .sequence_number = 11},
                        {.ecn = EcnMarking::kNotEct},
                        {.ecn = EcnMarking::kNotEct}}));
  EXPECT_EQ(transport.GetCongestionControlFeedbackStatsPerSsrc()[kSsrc]
                .num_packets_with_bleached_ect1_marking,
            2 + 0);
}

TEST(RtpTransportControllerSendTest,
     AccumulatesNumberOfReportedLostAndRecoveredPackets) {
  test::RunLoop main_thread;
  RtpTransportControllerSend transport(
      {.env = CreateTestEnvironment(),
       .network_thread = main_thread.task_queue()});
  transport.SetPreferredRtcpCcAckType(RtcpFeedbackType::CCFB);

  PacketSender sender(transport);
  sender.SimulateSentPackets({.first_sequence_number = 1, .num_packets = 30});

  // Produce 1st report with 2 received and 3 lost packets.
  transport.OnCongestionControlFeedback(
      /*receive_time=*/Timestamp::Seconds(123),
      GenerateFeedback({{.sequence_number = 1, .received = true},
                        {.sequence_number = 2, .received = false},
                        {.sequence_number = 3, .received = false},
                        {.sequence_number = 4, .received = false},
                        {.sequence_number = 5, .received = true}}));

  ReceivedCongestionControlFeedbackStats stats =
      transport.GetCongestionControlFeedbackStatsPerSsrc()[kSsrc];
  EXPECT_EQ(stats.num_packets_reported_as_lost, 3);
  EXPECT_EQ(stats.num_packets_reported_as_lost_but_recovered, 0);

  // Produce 2nd report with 1 packet recovered, 1 old packet reported still
  // lost, and 2 new packets lost.
  transport.OnCongestionControlFeedback(
      /*receive_time=*/Timestamp::Seconds(123),
      GenerateFeedback({{.sequence_number = 3, .received = true},
                        {.sequence_number = 4, .received = false},
                        {.sequence_number = 5, .received = true},
                        {.sequence_number = 6, .received = false},
                        {.sequence_number = 7, .received = false},
                        {.sequence_number = 8, .received = true}}));
  stats = transport.GetCongestionControlFeedbackStatsPerSsrc()[kSsrc];
  EXPECT_EQ(stats.num_packets_reported_as_lost, 3 + 2);
  EXPECT_EQ(stats.num_packets_reported_as_lost_but_recovered, 1);

  // Produce 3rd report with 2 more packets recovered.
  transport.OnCongestionControlFeedback(
      /*receive_time=*/Timestamp::Seconds(123),
      GenerateFeedback({{.sequence_number = 6, .received = true},
                        {.sequence_number = 7, .received = true},
                        {.sequence_number = 8, .received = true}}));
  stats = transport.GetCongestionControlFeedbackStatsPerSsrc()[kSsrc];
  EXPECT_EQ(stats.num_packets_reported_as_lost_but_recovered, 1 + 2);
}

TEST(RtpTransportControllerSendTest,
     DoesNotCountGapsInSequenceNumberBetweenReportsAsLoss) {
  test::RunLoop main_thread;
  RtpTransportControllerSend transport(
      {.env = CreateTestEnvironment(),
       .network_thread = main_thread.task_queue()});
  transport.SetPreferredRtcpCcAckType(RtcpFeedbackType::CCFB);

  PacketSender sender(transport);
  sender.SimulateSentPackets({.first_sequence_number = 1, .num_packets = 30});

  // Produce two report with a sequence number gap between them.
  transport.OnCongestionControlFeedback(
      /*receive_time=*/Timestamp::Seconds(123),
      GenerateFeedback({{.sequence_number = 1}}));
  transport.OnCongestionControlFeedback(
      /*receive_time=*/Timestamp::Seconds(123),
      GenerateFeedback({{.sequence_number = 5}}));

  // It is unclear if packets 2-4 weren't received and thus were excluded from
  // the feedback report, or report about these packets was itself lost.
  // Such packets are not counted as loss.
  EXPECT_EQ(transport.GetCongestionControlFeedbackStatsPerSsrc()[kSsrc]
                .num_packets_reported_as_lost,
            0);

  // Only count losses explicitly marked as such in a report to align with
  // the metric definition "report has been sent with a zero R bit"
  transport.OnCongestionControlFeedback(
      /*receive_time=*/Timestamp::Seconds(123),
      GenerateFeedback({{.sequence_number = 3, .received = true},
                        {.sequence_number = 4, .received = false},
                        {.sequence_number = 5, .received = true}}));
  EXPECT_EQ(transport.GetCongestionControlFeedbackStatsPerSsrc()[kSsrc]
                .num_packets_reported_as_lost,
            1);
}

}  // namespace
}  // namespace webrtc
