/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/rtp_rtcp/source/rtcp_sender.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "api/call/transport.h"
#include "api/environment/environment.h"
#include "api/rtp_headers.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "modules/rtp_rtcp/include/receive_statistics.h"
#include "modules/rtp_rtcp/include/rtcp_statistics.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtcp_packet.h"
#include "modules/rtp_rtcp/source/rtcp_packet/bye.h"
#include "modules/rtp_rtcp/source/rtcp_packet/common_header.h"
#include "modules/rtp_rtcp/source/rtcp_packet/dlrr.h"
#include "modules/rtp_rtcp/source/rtcp_packet/remote_estimate.h"
#include "modules/rtp_rtcp/source/rtcp_packet/report_block.h"
#include "modules/rtp_rtcp/source/rtcp_packet/tmmb_item.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "modules/rtp_rtcp/source/rtp_rtcp_impl2.h"
#include "modules/rtp_rtcp/source/rtp_rtcp_interface.h"
#include "rtc_base/rate_limiter.h"
#include "system_wrappers/include/clock.h"
#include "system_wrappers/include/ntp_time.h"
#include "test/create_test_environment.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/mock_transport.h"
#include "test/rtcp_packet_parser.h"
#include "test/run_loop.h"

namespace webrtc {
namespace {

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Property;
using ::testing::SizeIs;

class RtcpPacketTypeCounterObserverImpl : public RtcpPacketTypeCounterObserver {
 public:
  RtcpPacketTypeCounterObserverImpl() : ssrc_(0) {}
  ~RtcpPacketTypeCounterObserverImpl() override = default;
  void RtcpPacketTypesCounterUpdated(
      uint32_t ssrc,
      const RtcpPacketTypeCounter& packet_counter) override {
    ssrc_ = ssrc;
    counter_ = packet_counter;
  }
  uint32_t ssrc_;
  RtcpPacketTypeCounter counter_;
};

class TestTransport : public Transport {
 public:
  TestTransport() {}

  bool SendRtp(std::span<const uint8_t> /*data*/,
               const PacketOptions& /* options */) override {
    return false;
  }
  bool SendRtcp(std::span<const uint8_t> data,
                const PacketOptions& options) override {
    EXPECT_FALSE(options.is_media);
    parser_.Parse(data);
    return true;
  }
  test::RtcpPacketParser parser_;
};

constexpr uint32_t kSenderSsrc = 0x11111111;
constexpr uint32_t kRemoteSsrc = 0x22222222;
constexpr uint32_t kStartRtpTimestamp = 0x34567;
constexpr uint32_t kRtpTimestamp = 0x45678;

class RtcpSenderTest : public ::testing::Test {
 protected:
  RtcpSenderTest()
      : clock_(1335900000),
        env_(CreateTestEnvironment({.time = &clock_})),
        receive_statistics_(ReceiveStatistics::Create(&clock_)),
        rtp_rtcp_impl_(
            ModuleRtpRtcpImpl2::CreateSendModule(env_,
                                                 GetDefaultRtpRtcpConfig())) {}

  RTCPSender::Configuration GetDefaultConfig() {
    RTCPSender::Configuration configuration;
    configuration.audio = false;
    configuration.outgoing_transport = &test_transport_;
    configuration.rtcp_report_interval = TimeDelta::Seconds(1);
    configuration.receive_statistics = receive_statistics_.get();
    configuration.local_media_ssrc = kSenderSsrc;
    configuration.schedule_next_rtcp_send_evaluation = [](TimeDelta) {};
    return configuration;
  }

  RtpRtcpInterface::Configuration GetDefaultRtpRtcpConfig() {
    RTCPSender::Configuration config = GetDefaultConfig();
    RtpRtcpInterface::Configuration result;
    result.audio = config.audio;
    result.outgoing_transport = config.outgoing_transport;
    result.rtcp_report_interval_ms = config.rtcp_report_interval.ms();
    result.receive_statistics = config.receive_statistics;
    result.local_media_ssrc = config.local_media_ssrc;
    return result;
  }

  std::unique_ptr<RTCPSender> CreateRtcpSender(RTCPSender::Configuration config,
                                               bool init_timestamps = true) {
    auto rtcp_sender = std::make_unique<RTCPSender>(env_, std::move(config));
    rtcp_sender->SetRemoteSSRC(kRemoteSsrc);
    if (init_timestamps) {
      rtcp_sender->SetTimestampOffset(kStartRtpTimestamp);
      rtcp_sender->SetLastRtpTime(kRtpTimestamp, env_.clock().CurrentTime(),
                                  /*payload_type=*/0);
    }
    return rtcp_sender;
  }

  void InsertIncomingPacket(uint32_t remote_ssrc, uint16_t seq_num) {
    RtpPacketReceived packet;
    packet.SetSsrc(remote_ssrc);
    packet.SetSequenceNumber(seq_num);
    packet.SetTimestamp(12345);
    packet.SetPayloadSize(100 - 12);
    receive_statistics_->OnRtpPacket(packet);
  }

  test::RtcpPacketParser* parser() { return &test_transport_.parser_; }

  RTCPSender::FeedbackState feedback_state() {
    return rtp_rtcp_impl_->GetFeedbackState();
  }

  test::RunLoop main_thread_;
  SimulatedClock clock_;
  const Environment env_;
  TestTransport test_transport_;
  std::unique_ptr<ReceiveStatistics> receive_statistics_;
  const std::unique_ptr<ModuleRtpRtcpImpl2> rtp_rtcp_impl_;
};

TEST_F(RtcpSenderTest, SetRtcpStatus) {
  auto rtcp_sender = CreateRtcpSender(GetDefaultConfig());
  EXPECT_EQ(RtcpMode::kOff, rtcp_sender->Status());
  rtcp_sender->SetRTCPStatus(RtcpMode::kReducedSize);
  EXPECT_EQ(RtcpMode::kReducedSize, rtcp_sender->Status());
}

TEST_F(RtcpSenderTest, SetSendingStatus) {
  auto rtcp_sender = CreateRtcpSender(GetDefaultConfig());
  EXPECT_FALSE(rtcp_sender->Sending());
  rtcp_sender->SetSendingStatus(feedback_state(), true);
  EXPECT_TRUE(rtcp_sender->Sending());
}

TEST_F(RtcpSenderTest, NoPacketSentIfOff) {
  auto rtcp_sender = CreateRtcpSender(GetDefaultConfig());
  rtcp_sender->SetRTCPStatus(RtcpMode::kOff);
  EXPECT_EQ(-1, rtcp_sender->SendRTCP(feedback_state(), kRtcpSr));
}

TEST_F(RtcpSenderTest, SendSr) {
  const uint32_t kPacketCount = 0x12345;
  const uint32_t kOctetCount = 0x23456;
  auto rtcp_sender = CreateRtcpSender(GetDefaultConfig());
  rtcp_sender->SetRTCPStatus(RtcpMode::kReducedSize);
  RTCPSender::FeedbackState feedback_state = rtp_rtcp_impl_->GetFeedbackState();
  rtcp_sender->SetSendingStatus(feedback_state, true);
  feedback_state.packets_sent = kPacketCount;
  feedback_state.media_bytes_sent = kOctetCount;
  NtpTime ntp = clock_.CurrentNtpTime();
  EXPECT_EQ(0, rtcp_sender->SendRTCP(feedback_state, kRtcpSr));
  EXPECT_EQ(1, parser()->sender_report()->num_packets());
  EXPECT_EQ(kSenderSsrc, parser()->sender_report()->sender_ssrc());
  EXPECT_EQ(ntp, parser()->sender_report()->ntp());
  EXPECT_EQ(kPacketCount, parser()->sender_report()->sender_packet_count());
  EXPECT_EQ(kOctetCount, parser()->sender_report()->sender_octet_count());
  EXPECT_EQ(kStartRtpTimestamp + kRtpTimestamp,
            parser()->sender_report()->rtp_timestamp());
  EXPECT_EQ(0U, parser()->sender_report()->report_blocks().size());
}

TEST_F(RtcpSenderTest, SendConsecutiveSrWithExactSlope) {
  const uint32_t kPacketCount = 0x12345;
  const uint32_t kOctetCount = 0x23456;
  const int kTimeBetweenSRsUs = 10043;  // Not exact value in milliseconds.
  const int kExtraPackets = 30;
  auto rtcp_sender = CreateRtcpSender(GetDefaultConfig());
  // Make sure clock is not exactly at some milliseconds point.
  clock_.AdvanceTimeMicroseconds(kTimeBetweenSRsUs);
  rtcp_sender->SetRTCPStatus(RtcpMode::kReducedSize);
  RTCPSender::FeedbackState feedback_state = rtp_rtcp_impl_->GetFeedbackState();
  rtcp_sender->SetSendingStatus(feedback_state, true);
  feedback_state.packets_sent = kPacketCount;
  feedback_state.media_bytes_sent = kOctetCount;

  EXPECT_EQ(0, rtcp_sender->SendRTCP(feedback_state, kRtcpSr));
  EXPECT_EQ(1, parser()->sender_report()->num_packets());
  NtpTime ntp1 = parser()->sender_report()->ntp();
  uint32_t rtp1 = parser()->sender_report()->rtp_timestamp();

  // Send more SRs to ensure slope is always exact for different offsets
  for (int packets = 1; packets <= kExtraPackets; ++packets) {
    clock_.AdvanceTimeMicroseconds(kTimeBetweenSRsUs);
    EXPECT_EQ(0, rtcp_sender->SendRTCP(feedback_state, kRtcpSr));
    EXPECT_EQ(packets + 1, parser()->sender_report()->num_packets());

    NtpTime ntp2 = parser()->sender_report()->ntp();
    uint32_t rtp2 = parser()->sender_report()->rtp_timestamp();

    uint32_t ntp_diff_in_rtp_units =
        (ntp2.ToMs() - ntp1.ToMs()) * (kVideoPayloadTypeFrequency / 1000);
    EXPECT_EQ(rtp2 - rtp1, ntp_diff_in_rtp_units);
  }
}

TEST_F(RtcpSenderTest, DoNotSendSrBeforeRtp) {
  auto rtcp_sender =
      CreateRtcpSender(GetDefaultConfig(), /*init_timestamps=*/false);
  rtcp_sender->SetRTCPStatus(RtcpMode::kReducedSize);
  rtcp_sender->SetSendingStatus(feedback_state(), true);

  // Sender Report shouldn't be send as an SR nor as a Report.
  rtcp_sender->SendRTCP(feedback_state(), kRtcpSr);
  EXPECT_EQ(0, parser()->sender_report()->num_packets());
  rtcp_sender->SendRTCP(feedback_state(), kRtcpReport);
  EXPECT_EQ(0, parser()->sender_report()->num_packets());
  // Other packets (e.g. Pli) are allowed, even if useless.
  EXPECT_EQ(0, rtcp_sender->SendRTCP(feedback_state(), kRtcpPli));
  EXPECT_EQ(1, parser()->pli()->num_packets());
}

TEST_F(RtcpSenderTest, DoNotSendCompundBeforeRtp) {
  auto rtcp_sender =
      CreateRtcpSender(GetDefaultConfig(), /*init_timestamps=*/false);
  rtcp_sender->SetRTCPStatus(RtcpMode::kCompound);
  rtcp_sender->SetSendingStatus(feedback_state(), true);

  // In compound mode no packets are allowed (e.g. Pli) because compound mode
  // should start with Sender Report.
  EXPECT_EQ(-1, rtcp_sender->SendRTCP(feedback_state(), kRtcpPli));
  EXPECT_EQ(0, parser()->pli()->num_packets());
}

TEST_F(RtcpSenderTest, SendRr) {
  auto rtcp_sender = CreateRtcpSender(GetDefaultConfig());
  rtcp_sender->SetRTCPStatus(RtcpMode::kCompound);
  EXPECT_EQ(0, rtcp_sender->SendRTCP(feedback_state(), kRtcpRr));
  EXPECT_EQ(1, parser()->receiver_report()->num_packets());
  EXPECT_EQ(kSenderSsrc, parser()->receiver_report()->sender_ssrc());
  EXPECT_EQ(0U, parser()->receiver_report()->report_blocks().size());
}

TEST_F(RtcpSenderTest, DoesntSendEmptyRrInReducedSizeMode) {
  auto rtcp_sender = CreateRtcpSender(GetDefaultConfig());
  rtcp_sender->SetRTCPStatus(RtcpMode::kReducedSize);
  rtcp_sender->SendRTCP(feedback_state(), kRtcpRr);
  EXPECT_EQ(parser()->receiver_report()->num_packets(), 0);
}

TEST_F(RtcpSenderTest, SendRrWithOneReportBlock) {
  const uint16_t kSeqNum = 11111;
  auto rtcp_sender = CreateRtcpSender(GetDefaultConfig());
  InsertIncomingPacket(kRemoteSsrc, kSeqNum);
  rtcp_sender->SetRTCPStatus(RtcpMode::kCompound);
  EXPECT_EQ(0, rtcp_sender->SendRTCP(feedback_state(), kRtcpRr));
  EXPECT_EQ(1, parser()->receiver_report()->num_packets());
  EXPECT_EQ(kSenderSsrc, parser()->receiver_report()->sender_ssrc());
  ASSERT_EQ(1U, parser()->receiver_report()->report_blocks().size());
  const rtcp::ReportBlock& rb = parser()->receiver_report()->report_blocks()[0];
  EXPECT_EQ(kRemoteSsrc, rb.source_ssrc());
  EXPECT_EQ(0U, rb.fraction_lost());
  EXPECT_EQ(0, rb.cumulative_lost());
  EXPECT_EQ(kSeqNum, rb.extended_high_seq_num());
}

TEST_F(RtcpSenderTest, SendRrWithTwoReportBlocks) {
  const uint16_t kSeqNum = 11111;
  auto rtcp_sender = CreateRtcpSender(GetDefaultConfig());
  InsertIncomingPacket(kRemoteSsrc, kSeqNum);
  InsertIncomingPacket(kRemoteSsrc + 1, kSeqNum + 1);
  rtcp_sender->SetRTCPStatus(RtcpMode::kCompound);
  EXPECT_EQ(0, rtcp_sender->SendRTCP(feedback_state(), kRtcpRr));
  EXPECT_EQ(1, parser()->receiver_report()->num_packets());
  EXPECT_EQ(kSenderSsrc, parser()->receiver_report()->sender_ssrc());
  EXPECT_THAT(
      parser()->receiver_report()->report_blocks(),
      UnorderedElementsAre(
          Property(&rtcp::ReportBlock::source_ssrc, Eq(kRemoteSsrc)),
          Property(&rtcp::ReportBlock::source_ssrc, Eq(kRemoteSsrc + 1))));
}

TEST_F(RtcpSenderTest, SendSdes) {
  auto rtcp_sender = CreateRtcpSender(GetDefaultConfig());
  rtcp_sender->SetRTCPStatus(RtcpMode::kReducedSize);
  EXPECT_EQ(0, rtcp_sender->SetCNAME("alice@host"));
  EXPECT_EQ(0, rtcp_sender->SendRTCP(feedback_state(), kRtcpSdes));
  EXPECT_EQ(1, parser()->sdes()->num_packets());
  EXPECT_EQ(1U, parser()->sdes()->chunks().size());
  EXPECT_EQ(kSenderSsrc, parser()->sdes()->chunks()[0].ssrc);
  EXPECT_EQ("alice@host", parser()->sdes()->chunks()[0].cname);
}

TEST_F(RtcpSenderTest, SdesIncludedInCompoundPacket) {
  auto rtcp_sender = CreateRtcpSender(GetDefaultConfig());
  rtcp_sender->SetRTCPStatus(RtcpMode::kCompound);
  EXPECT_EQ(0, rtcp_sender->SetCNAME("alice@host"));
  EXPECT_EQ(0, rtcp_sender->SendRTCP(feedback_state(), kRtcpReport));
  EXPECT_EQ(1, parser()->receiver_report()->num_packets());
  EXPECT_EQ(1, parser()->sdes()->num_packets());
  EXPECT_EQ(1U, parser()->sdes()->chunks().size());
}

TEST_F(RtcpSenderTest, SendBye) {
  auto rtcp_sender = CreateRtcpSender(GetDefaultConfig());
  rtcp_sender->SetRTCPStatus(RtcpMode::kReducedSize);
  EXPECT_EQ(0, rtcp_sender->SendRTCP(feedback_state(), kRtcpBye));
  EXPECT_EQ(1, parser()->bye()->num_packets());
  EXPECT_EQ(kSenderSsrc, parser()->bye()->sender_ssrc());
}

TEST_F(RtcpSenderTest, StopSendingTriggersBye) {
  auto rtcp_sender = CreateRtcpSender(GetDefaultConfig());
  rtcp_sender->SetRTCPStatus(RtcpMode::kReducedSize);
  rtcp_sender->SetSendingStatus(feedback_state(), true);
  rtcp_sender->SetSendingStatus(feedback_state(), false);
  EXPECT_EQ(1, parser()->bye()->num_packets());
  EXPECT_EQ(kSenderSsrc, parser()->bye()->sender_ssrc());
}

TEST_F(RtcpSenderTest, SendFir) {
  auto rtcp_sender = CreateRtcpSender(GetDefaultConfig());
  rtcp_sender->SetRTCPStatus(RtcpMode::kReducedSize);
  EXPECT_EQ(0, rtcp_sender->SendRTCP(feedback_state(), kRtcpFir));
  EXPECT_EQ(1, parser()->fir()->num_packets());
  EXPECT_EQ(kSenderSsrc, parser()->fir()->sender_ssrc());
  EXPECT_EQ(1U, parser()->fir()->requests().size());
  EXPECT_EQ(kRemoteSsrc, parser()->fir()->requests()[0].ssrc);
  uint8_t seq = parser()->fir()->requests()[0].seq_nr;
  EXPECT_EQ(0, rtcp_sender->SendRTCP(feedback_state(), kRtcpFir));
  EXPECT_EQ(2, parser()->fir()->num_packets());
  EXPECT_EQ(seq + 1, parser()->fir()->requests()[0].seq_nr);
}

TEST_F(RtcpSenderTest, SendPli) {
  auto rtcp_sender = CreateRtcpSender(GetDefaultConfig());
  rtcp_sender->SetRTCPStatus(RtcpMode::kReducedSize);
  EXPECT_EQ(0, rtcp_sender->SendRTCP(feedback_state(), kRtcpPli));
  EXPECT_EQ(1, parser()->pli()->num_packets());
  EXPECT_EQ(kSenderSsrc, parser()->pli()->sender_ssrc());
  EXPECT_EQ(kRemoteSsrc, parser()->pli()->media_ssrc());
}

TEST_F(RtcpSenderTest, SendNack) {
  auto rtcp_sender = CreateRtcpSender(GetDefaultConfig());
  rtcp_sender->SetRTCPStatus(RtcpMode::kReducedSize);
  const uint16_t kList[] = {0, 1, 16};
  EXPECT_EQ(0, rtcp_sender->SendRTCP(feedback_state(), kRtcpNack, kList));
  EXPECT_EQ(1, parser()->nack()->num_packets());
  EXPECT_EQ(kSenderSsrc, parser()->nack()->sender_ssrc());
  EXPECT_EQ(kRemoteSsrc, parser()->nack()->media_ssrc());
  EXPECT_THAT(parser()->nack()->packet_ids(), ElementsAre(0, 1, 16));
}

TEST_F(RtcpSenderTest, RembNotIncludedBeforeSet) {
  auto rtcp_sender = CreateRtcpSender(GetDefaultConfig());
  rtcp_sender->SetRTCPStatus(RtcpMode::kCompound);

  rtcp_sender->SendRTCP(feedback_state(), kRtcpRr);

  ASSERT_EQ(1, parser()->receiver_report()->num_packets());
  EXPECT_EQ(0, parser()->remb()->num_packets());
}

TEST_F(RtcpSenderTest, RembNotIncludedAfterUnset) {
  const int64_t kBitrate = 261011;
  const std::vector<uint32_t> kSsrcs = {kRemoteSsrc, kRemoteSsrc + 1};
  auto rtcp_sender = CreateRtcpSender(GetDefaultConfig());
  rtcp_sender->SetRTCPStatus(RtcpMode::kCompound);
  rtcp_sender->SetRemb(kBitrate, kSsrcs);
  rtcp_sender->SendRTCP(feedback_state(), kRtcpRr);
  ASSERT_EQ(1, parser()->receiver_report()->num_packets());
  EXPECT_EQ(1, parser()->remb()->num_packets());

  // Turn off REMB. rtcp_sender no longer should send it.
  rtcp_sender->UnsetRemb();
  rtcp_sender->SendRTCP(feedback_state(), kRtcpRr);
  ASSERT_EQ(2, parser()->receiver_report()->num_packets());
  EXPECT_EQ(1, parser()->remb()->num_packets());
}

TEST_F(RtcpSenderTest, SendRemb) {
  const int64_t kBitrate = 261011;
  const std::vector<uint32_t> kSsrcs = {kRemoteSsrc, kRemoteSsrc + 1};
  auto rtcp_sender = CreateRtcpSender(GetDefaultConfig());
  rtcp_sender->SetRTCPStatus(RtcpMode::kReducedSize);
  rtcp_sender->SetRemb(kBitrate, kSsrcs);

  rtcp_sender->SendRTCP(feedback_state(), kRtcpRemb);

  EXPECT_EQ(1, parser()->remb()->num_packets());
  EXPECT_EQ(kSenderSsrc, parser()->remb()->sender_ssrc());
  EXPECT_EQ(kBitrate, parser()->remb()->bitrate_bps());
  EXPECT_THAT(parser()->remb()->ssrcs(),
              ElementsAre(kRemoteSsrc, kRemoteSsrc + 1));
}

TEST_F(RtcpSenderTest, RembIncludedInEachCompoundPacketAfterSet) {
  const int kBitrate = 261011;
  const std::vector<uint32_t> kSsrcs = {kRemoteSsrc, kRemoteSsrc + 1};
  auto rtcp_sender = CreateRtcpSender(GetDefaultConfig());
  rtcp_sender->SetRTCPStatus(RtcpMode::kCompound);
  rtcp_sender->SetRemb(kBitrate, kSsrcs);

  rtcp_sender->SendRTCP(feedback_state(), kRtcpReport);
  EXPECT_EQ(1, parser()->remb()->num_packets());
  // REMB should be included in each compound packet.
  rtcp_sender->SendRTCP(feedback_state(), kRtcpReport);
  EXPECT_EQ(2, parser()->remb()->num_packets());
}

TEST_F(RtcpSenderTest, SendXrWithDlrr) {
  auto rtcp_sender = CreateRtcpSender(GetDefaultConfig());
  rtcp_sender->SetRTCPStatus(RtcpMode::kCompound);
  RTCPSender::FeedbackState feedback_state = rtp_rtcp_impl_->GetFeedbackState();
  rtcp::ReceiveTimeInfo last_xr_rr;
  last_xr_rr.ssrc = 0x11111111;
  last_xr_rr.last_rr = 0x22222222;
  last_xr_rr.delay_since_last_rr = 0x33333333;
  feedback_state.last_xr_rtis.push_back(last_xr_rr);
  EXPECT_EQ(0, rtcp_sender->SendRTCP(feedback_state, kRtcpReport));
  EXPECT_EQ(1, parser()->xr()->num_packets());
  EXPECT_EQ(kSenderSsrc, parser()->xr()->sender_ssrc());
  ASSERT_THAT(parser()->xr()->dlrr().sub_blocks(), SizeIs(1));
  EXPECT_EQ(last_xr_rr.ssrc, parser()->xr()->dlrr().sub_blocks()[0].ssrc);
  EXPECT_EQ(last_xr_rr.last_rr, parser()->xr()->dlrr().sub_blocks()[0].last_rr);
  EXPECT_EQ(last_xr_rr.delay_since_last_rr,
            parser()->xr()->dlrr().sub_blocks()[0].delay_since_last_rr);
}

TEST_F(RtcpSenderTest, SendXrWithMultipleDlrrSubBlocks) {
  const size_t kNumReceivers = 2;
  auto rtcp_sender = CreateRtcpSender(GetDefaultConfig());
  rtcp_sender->SetRTCPStatus(RtcpMode::kCompound);
  RTCPSender::FeedbackState feedback_state = rtp_rtcp_impl_->GetFeedbackState();
  for (size_t i = 0; i < kNumReceivers; ++i) {
    rtcp::ReceiveTimeInfo last_xr_rr;
    last_xr_rr.ssrc = i;
    last_xr_rr.last_rr = (i + 1) * 100;
    last_xr_rr.delay_since_last_rr = (i + 2) * 200;
    feedback_state.last_xr_rtis.push_back(last_xr_rr);
  }

  EXPECT_EQ(0, rtcp_sender->SendRTCP(feedback_state, kRtcpReport));
  EXPECT_EQ(1, parser()->xr()->num_packets());
  EXPECT_EQ(kSenderSsrc, parser()->xr()->sender_ssrc());
  ASSERT_THAT(parser()->xr()->dlrr().sub_blocks(), SizeIs(kNumReceivers));
  for (size_t i = 0; i < kNumReceivers; ++i) {
    EXPECT_EQ(feedback_state.last_xr_rtis[i].ssrc,
              parser()->xr()->dlrr().sub_blocks()[i].ssrc);
    EXPECT_EQ(feedback_state.last_xr_rtis[i].last_rr,
              parser()->xr()->dlrr().sub_blocks()[i].last_rr);
    EXPECT_EQ(feedback_state.last_xr_rtis[i].delay_since_last_rr,
              parser()->xr()->dlrr().sub_blocks()[i].delay_since_last_rr);
  }
}

TEST_F(RtcpSenderTest, SendXrWithRrtr) {
  RTCPSender::Configuration config = GetDefaultConfig();
  config.non_sender_rtt_measurement = true;
  auto rtcp_sender = CreateRtcpSender(std::move(config));
  rtcp_sender->SetRTCPStatus(RtcpMode::kCompound);
  rtcp_sender->SetSendingStatus(feedback_state(), false);
  NtpTime ntp = clock_.CurrentNtpTime();
  EXPECT_EQ(0, rtcp_sender->SendRTCP(feedback_state(), kRtcpReport));
  EXPECT_EQ(1, parser()->xr()->num_packets());
  EXPECT_EQ(kSenderSsrc, parser()->xr()->sender_ssrc());
  EXPECT_FALSE(parser()->xr()->dlrr());
  ASSERT_TRUE(parser()->xr()->rrtr());
  EXPECT_EQ(ntp, parser()->xr()->rrtr()->ntp());
}

// Same test as above, but enable Rrtr with the setter.
TEST_F(RtcpSenderTest, SendXrWithRrtrUsingSetter) {
  RTCPSender::Configuration config = GetDefaultConfig();
  config.non_sender_rtt_measurement = false;
  auto rtcp_sender = CreateRtcpSender(std::move(config));
  rtcp_sender->SetNonSenderRttMeasurement(true);
  rtcp_sender->SetRTCPStatus(RtcpMode::kCompound);
  rtcp_sender->SetSendingStatus(feedback_state(), false);
  NtpTime ntp = clock_.CurrentNtpTime();
  EXPECT_EQ(0, rtcp_sender->SendRTCP(feedback_state(), kRtcpReport));
  EXPECT_EQ(1, parser()->xr()->num_packets());
  EXPECT_EQ(kSenderSsrc, parser()->xr()->sender_ssrc());
  EXPECT_FALSE(parser()->xr()->dlrr());
  ASSERT_TRUE(parser()->xr()->rrtr());
  EXPECT_EQ(ntp, parser()->xr()->rrtr()->ntp());
}

// Same test as above, but disable Rrtr with the setter.
TEST_F(RtcpSenderTest, SendsNoRrtrUsingSetter) {
  RTCPSender::Configuration config = GetDefaultConfig();
  config.non_sender_rtt_measurement = true;
  auto rtcp_sender = CreateRtcpSender(std::move(config));
  rtcp_sender->SetNonSenderRttMeasurement(false);
  rtcp_sender->SetRTCPStatus(RtcpMode::kCompound);
  rtcp_sender->SetSendingStatus(feedback_state(), false);
  EXPECT_EQ(0, rtcp_sender->SendRTCP(feedback_state(), kRtcpReport));
  EXPECT_EQ(0, parser()->xr()->num_packets());
}

TEST_F(RtcpSenderTest, TestNoXrRrtrSentIfSending) {
  RTCPSender::Configuration config = GetDefaultConfig();
  config.non_sender_rtt_measurement = true;
  auto rtcp_sender = CreateRtcpSender(std::move(config));
  rtcp_sender->SetRTCPStatus(RtcpMode::kCompound);
  rtcp_sender->SetSendingStatus(feedback_state(), true);
  EXPECT_EQ(0, rtcp_sender->SendRTCP(feedback_state(), kRtcpReport));
  EXPECT_EQ(0, parser()->xr()->num_packets());
}

TEST_F(RtcpSenderTest, TestNoXrRrtrSentIfNotEnabled) {
  RTCPSender::Configuration config = GetDefaultConfig();
  config.non_sender_rtt_measurement = false;
  auto rtcp_sender = CreateRtcpSender(std::move(config));
  rtcp_sender->SetRTCPStatus(RtcpMode::kCompound);
  rtcp_sender->SetSendingStatus(feedback_state(), false);
  EXPECT_EQ(0, rtcp_sender->SendRTCP(feedback_state(), kRtcpReport));
  EXPECT_EQ(0, parser()->xr()->num_packets());
}

TEST_F(RtcpSenderTest, TestRegisterRtcpPacketTypeObserver) {
  RtcpPacketTypeCounterObserverImpl observer;
  RTCPSender::Configuration config = GetDefaultConfig();
  config.rtcp_packet_type_counter_observer = &observer;
  auto rtcp_sender = CreateRtcpSender(std::move(config));
  rtcp_sender->SetRTCPStatus(RtcpMode::kReducedSize);
  EXPECT_EQ(0, rtcp_sender->SendRTCP(feedback_state(), kRtcpPli));
  EXPECT_EQ(1, parser()->pli()->num_packets());
  EXPECT_EQ(kRemoteSsrc, observer.ssrc_);
  EXPECT_EQ(1U, observer.counter_.pli_packets);
}

TEST_F(RtcpSenderTest, SendTmmbr) {
  const unsigned int kBitrateBps = 312000;
  auto rtcp_sender = CreateRtcpSender(GetDefaultConfig());
  rtcp_sender->SetRTCPStatus(RtcpMode::kReducedSize);
  rtcp_sender->SetTargetBitrate(kBitrateBps);
  EXPECT_EQ(0, rtcp_sender->SendRTCP(feedback_state(), kRtcpTmmbr));
  EXPECT_EQ(1, parser()->tmmbr()->num_packets());
  EXPECT_EQ(kSenderSsrc, parser()->tmmbr()->sender_ssrc());
  EXPECT_EQ(1U, parser()->tmmbr()->requests().size());
  EXPECT_EQ(kBitrateBps, parser()->tmmbr()->requests()[0].bitrate_bps());
  // TODO(asapersson): tmmbr_item()->Overhead() looks broken, always zero.
}

TEST_F(RtcpSenderTest, SendTmmbn) {
  auto rtcp_sender = CreateRtcpSender(GetDefaultConfig());
  rtcp_sender->SetRTCPStatus(RtcpMode::kCompound);
  rtcp_sender->SetSendingStatus(feedback_state(), true);
  std::vector<rtcp::TmmbItem> bounding_set;
  const uint32_t kBitrateBps = 32768000;
  const uint32_t kPacketOh = 40;
  const uint32_t kSourceSsrc = 12345;
  const rtcp::TmmbItem tmmbn(kSourceSsrc, kBitrateBps, kPacketOh);
  bounding_set.push_back(tmmbn);
  rtcp_sender->SetTmmbn(bounding_set);

  EXPECT_EQ(0, rtcp_sender->SendRTCP(feedback_state(), kRtcpSr));
  EXPECT_EQ(1, parser()->sender_report()->num_packets());
  EXPECT_EQ(1, parser()->tmmbn()->num_packets());
  EXPECT_EQ(kSenderSsrc, parser()->tmmbn()->sender_ssrc());
  EXPECT_EQ(1U, parser()->tmmbn()->items().size());
  EXPECT_EQ(kBitrateBps, parser()->tmmbn()->items()[0].bitrate_bps());
  EXPECT_EQ(kPacketOh, parser()->tmmbn()->items()[0].packet_overhead());
  EXPECT_EQ(kSourceSsrc, parser()->tmmbn()->items()[0].ssrc());
}

// This test is written to verify actual behaviour. It does not seem
// to make much sense to send an empty TMMBN, since there is no place
// to put an actual limit here. It's just information that no limit
// is set, which is kind of the starting assumption.
// See http://code.google.com/p/webrtc/issues/detail?id=468 for one
// situation where this caused confusion.
TEST_F(RtcpSenderTest, SendsTmmbnIfSetAndEmpty) {
  auto rtcp_sender = CreateRtcpSender(GetDefaultConfig());
  rtcp_sender->SetRTCPStatus(RtcpMode::kCompound);
  rtcp_sender->SetSendingStatus(feedback_state(), true);
  std::vector<rtcp::TmmbItem> bounding_set;
  rtcp_sender->SetTmmbn(bounding_set);
  EXPECT_EQ(0, rtcp_sender->SendRTCP(feedback_state(), kRtcpSr));
  EXPECT_EQ(1, parser()->sender_report()->num_packets());
  EXPECT_EQ(1, parser()->tmmbn()->num_packets());
  EXPECT_EQ(kSenderSsrc, parser()->tmmbn()->sender_ssrc());
  EXPECT_EQ(0U, parser()->tmmbn()->items().size());
}

// This test is written to verify that BYE is always the last packet
// type in a RTCP compoud packet.  The rtcp_sender is recreated with
// mock_transport, which is used to check for whether BYE at the end
// of a RTCP compound packet.
TEST_F(RtcpSenderTest, ByeMustBeLast) {
  MockTransport mock_transport;
  EXPECT_CALL(mock_transport, SendRtcp(_, _))
      .WillOnce([](std::span<const uint8_t> data, ::testing::Unused) {
        const uint8_t* next_packet = data.data();
        const uint8_t* const packet_end = data.data() + data.size();
        rtcp::CommonHeader packet;
        while (next_packet < packet_end) {
          EXPECT_TRUE(packet.Parse(next_packet, packet_end - next_packet));
          next_packet = packet.NextPacket();
          if (packet.type() ==
              rtcp::Bye::kPacketType)  // Main test expectation.
            EXPECT_EQ(0, packet_end - next_packet)
                << "Bye packet should be last in a compound RTCP packet.";
          if (next_packet == packet_end)  // Validate test was set correctly.
            EXPECT_EQ(packet.type(), rtcp::Bye::kPacketType)
                << "Last packet in this test expected to be Bye.";
        }

        return true;
      });

  // Re-configure rtcp_sender with mock_transport_
  RTCPSender::Configuration config = GetDefaultConfig();
  config.outgoing_transport = &mock_transport;
  auto rtcp_sender = CreateRtcpSender(std::move(config));

  rtcp_sender->SetTimestampOffset(kStartRtpTimestamp);
  rtcp_sender->SetLastRtpTime(kRtpTimestamp, clock_.CurrentTime(),
                              /*payload_type=*/0);

  // Set up REMB info to be included with BYE.
  rtcp_sender->SetRTCPStatus(RtcpMode::kCompound);
  rtcp_sender->SetRemb(1234, {});
  EXPECT_EQ(0, rtcp_sender->SendRTCP(feedback_state(), kRtcpBye));
}

TEST_F(RtcpSenderTest, DoesntSchedulesInitialReportWhenSsrcSetOnConstruction) {
  auto rtcp_sender = CreateRtcpSender(GetDefaultConfig());
  rtcp_sender->SetRTCPStatus(RtcpMode::kReducedSize);
  rtcp_sender->SetRemoteSSRC(kRemoteSsrc);
  // New report should not have been scheduled yet.
  clock_.AdvanceTimeMilliseconds(100);
  EXPECT_FALSE(rtcp_sender->TimeToSendRTCPReport(false));
}

TEST_F(RtcpSenderTest, SendsCombinedRtcpPacket) {
  auto rtcp_sender = CreateRtcpSender(GetDefaultConfig());
  rtcp_sender->SetRTCPStatus(RtcpMode::kReducedSize);

  std::vector<std::unique_ptr<rtcp::RtcpPacket>> packets;
  auto transport_feedback = std::make_unique<rtcp::TransportFeedback>();
  transport_feedback->AddReceivedPacket(321, Timestamp::Millis(10));
  packets.push_back(std::move(transport_feedback));
  auto remote_estimate = std::make_unique<rtcp::RemoteEstimate>();
  packets.push_back(std::move(remote_estimate));
  rtcp_sender->SendCombinedRtcpPacket(std::move(packets));

  EXPECT_EQ(parser()->transport_feedback()->num_packets(), 1);
  EXPECT_EQ(parser()->transport_feedback()->sender_ssrc(), kSenderSsrc);
  EXPECT_EQ(parser()->app()->num_packets(), 1);
  EXPECT_EQ(parser()->app()->sender_ssrc(), kSenderSsrc);
}

TEST_F(RtcpSenderTest, DropsPacketLargerThanMaxPacketSizeFromCombinedPacket) {
  constexpr size_t kMaxPacketSize = 1200;
  auto rtcp_sender = CreateRtcpSender(GetDefaultConfig());
  rtcp_sender->SetRTCPStatus(RtcpMode::kReducedSize);
  rtcp_sender->SetMaxRtpPacketSize(kMaxPacketSize);

  auto transport_feedback = std::make_unique<rtcp::TransportFeedback>();
  transport_feedback->SetBase(0, Timestamp::Millis(10));
  for (uint16_t seq = 0; transport_feedback->BlockLength() <= kMaxPacketSize;
       ++seq) {
    ASSERT_TRUE(transport_feedback->AddReceivedPacket(
        seq, Timestamp::Millis(10 + seq)));
  }
  std::vector<std::unique_ptr<rtcp::RtcpPacket>> packets;
  packets.push_back(std::move(transport_feedback));
  packets.push_back(std::make_unique<rtcp::RemoteEstimate>());
  rtcp_sender->SendCombinedRtcpPacket(std::move(packets));

  EXPECT_EQ(parser()->transport_feedback()->num_packets(), 0);
  EXPECT_EQ(parser()->app()->num_packets(), 1);
}

}  // namespace
}  // namespace webrtc
