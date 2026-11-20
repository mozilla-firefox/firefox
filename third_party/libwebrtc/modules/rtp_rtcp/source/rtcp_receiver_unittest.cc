/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/rtp_rtcp/source/rtcp_receiver.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "api/environment/environment_factory.h"
#include "api/transport/network_types.h"
#include "api/units/data_rate.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "modules/rtp_rtcp/include/report_block_data.h"
#include "modules/rtp_rtcp/include/rtcp_statistics.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/mocks/mock_network_link_rtcp_observer.h"
#include "modules/rtp_rtcp/mocks/mock_network_state_estimator_observer.h"
#include "modules/rtp_rtcp/source/byte_io.h"
#include "modules/rtp_rtcp/source/ntp_time_util.h"
#include "modules/rtp_rtcp/source/rtcp_packet/app.h"
#include "modules/rtp_rtcp/source/rtcp_packet/bye.h"
#include "modules/rtp_rtcp/source/rtcp_packet/compound_packet.h"
#include "modules/rtp_rtcp/source/rtcp_packet/congestion_control_feedback.h"
#include "modules/rtp_rtcp/source/rtcp_packet/dlrr.h"
#include "modules/rtp_rtcp/source/rtcp_packet/extended_reports.h"
#include "modules/rtp_rtcp/source/rtcp_packet/fir.h"
#include "modules/rtp_rtcp/source/rtcp_packet/nack.h"
#include "modules/rtp_rtcp/source/rtcp_packet/pli.h"
#include "modules/rtp_rtcp/source/rtcp_packet/rapid_resync_request.h"
#include "modules/rtp_rtcp/source/rtcp_packet/receiver_report.h"
#include "modules/rtp_rtcp/source/rtcp_packet/remb.h"
#include "modules/rtp_rtcp/source/rtcp_packet/remote_estimate.h"
#include "modules/rtp_rtcp/source/rtcp_packet/rtpfb.h"
#include "modules/rtp_rtcp/source/rtcp_packet/sdes.h"
#include "modules/rtp_rtcp/source/rtcp_packet/sender_report.h"
#include "modules/rtp_rtcp/source/rtcp_packet/tmmbr.h"
#include "modules/rtp_rtcp/source/rtcp_packet/transport_feedback.h"
#include "modules/rtp_rtcp/source/rtp_rtcp_interface.h"
#include "rtc_base/buffer.h"
#include "rtc_base/random.h"
#include "system_wrappers/include/clock.h"
#include "system_wrappers/include/ntp_time.h"
#include "test/create_test_field_trials.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/near_matcher.h"

namespace webrtc {
namespace {

using rtcp::ReceiveTimeInfo;
using ::testing::_;
using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Gt;
using ::testing::InSequence;
using ::testing::IsEmpty;
using ::testing::NiceMock;
using ::testing::Property;
using ::testing::SizeIs;
using ::testing::StrEq;
using ::testing::StrictMock;
using ::testing::UnorderedElementsAre;

class MockRtcpPacketTypeCounterObserver : public RtcpPacketTypeCounterObserver {
 public:
  MOCK_METHOD(void,
              RtcpPacketTypesCounterUpdated,
              (uint32_t, const RtcpPacketTypeCounter&),
              (override));
};

class MockRtcpIntraFrameObserver : public RtcpIntraFrameObserver {
 public:
  MOCK_METHOD(void, OnReceivedIntraFrameRequest, (uint32_t), (override));
};

class MockCnameCallbackImpl : public RtcpCnameCallback {
 public:
  MOCK_METHOD(void, OnCname, (uint32_t, absl::string_view), (override));
};

class MockReportBlockDataObserverImpl : public ReportBlockDataObserver {
 public:
  MOCK_METHOD(void, OnReportBlockDataUpdated, (ReportBlockData), (override));
};

class MockModuleRtpRtcp : public RTCPReceiver::ModuleRtpRtcp {
 public:
  MOCK_METHOD(void, SetTmmbn, (std::vector<rtcp::TmmbItem>), (override));
  MOCK_METHOD(void, OnRequestSendReport, (), (override));
  MOCK_METHOD(void, OnReceivedNack, (const std::vector<uint16_t>&), (override));
  MOCK_METHOD(void,
              OnReceivedRtcpReportBlocks,
              (std::span<const ReportBlockData>),
              (override));
};

// SSRC of remote peer, that sends rtcp packet to the rtcp receiver under test.
constexpr uint32_t kSenderSsrc = 0x10203;
// SSRCs of local peer, that rtcp packet addressed to.
constexpr uint32_t kReceiverMainSsrc = 0x123456;
// RtcpReceiver can accept several ssrc, e.g. regular and rtx streams.
constexpr uint32_t kReceiverExtraSsrc = 0x1234567;
// SSRCs to ignore (i.e. not configured in RtcpReceiver).
constexpr uint32_t kNotToUsSsrc = 0x654321;
constexpr uint32_t kUnknownSenderSsrc = 0x54321;

constexpr int64_t kRtcpIntervalMs = 1000;

}  // namespace

struct ReceiverMocks {
  SimulatedClock clock{1335900000};
  std::string field_trials;
  // Callbacks to packet_type_counter_observer are frequent but most of the time
  // are not interesting.
  NiceMock<MockRtcpPacketTypeCounterObserver> packet_type_counter_observer;
  StrictMock<MockRtcpIntraFrameObserver> intra_frame_observer;
  StrictMock<MockModuleRtpRtcp> rtp_rtcp_impl;
  NiceMock<MockNetworkLinkRtcpObserver> network_link_rtcp_observer;
  NiceMock<MockNetworkStateEstimateObserver> network_state_estimate_observer;

  RtpRtcpInterface::Configuration config = {
      .receiver_only = false,
      .intra_frame_callback = &intra_frame_observer,
      .network_link_rtcp_observer = &network_link_rtcp_observer,
      .network_state_estimate_observer = &network_state_estimate_observer,
      .rtcp_packet_type_counter_observer = &packet_type_counter_observer,
      .rtcp_report_interval_ms = kRtcpIntervalMs,
      .local_media_ssrc = kReceiverMainSsrc,
      .rtx_send_ssrc = kReceiverExtraSsrc};
};

RTCPReceiver Create(ReceiverMocks& mocks) {
  return RTCPReceiver(CreateEnvironment(&mocks.clock, CreateTestFieldTrialsPtr(
                                                          mocks.field_trials)),
                      mocks.config, &mocks.rtp_rtcp_impl);
}

TEST(RtcpReceiverTest, BrokenPacketIsIgnored) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);

  const uint8_t bad_packet[] = {0, 0, 0, 0};
  EXPECT_CALL(mocks.packet_type_counter_observer, RtcpPacketTypesCounterUpdated)
      .Times(0);
  receiver.IncomingPacket(bad_packet);
}

TEST(RtcpReceiverTest, InvalidFeedbackPacketIsIgnored) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);

  // Too short feedback packet.
  const uint8_t bad_packet[] = {0x81, rtcp::Rtpfb::kPacketType, 0, 0};

  EXPECT_CALL(mocks.packet_type_counter_observer, RtcpPacketTypesCounterUpdated)
      .Times(0);
  receiver.IncomingPacket(bad_packet);
}

TEST(RtcpReceiverTest, InjectSrPacket) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  EXPECT_FALSE(receiver.GetSenderReportStats());

  rtcp::SenderReport sr;
  sr.SetSenderSsrc(kSenderSsrc);

  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks(IsEmpty()));
  receiver.IncomingPacket(sr.Build());

  EXPECT_TRUE(receiver.GetSenderReportStats());
}

TEST(RtcpReceiverTest, InjectSrPacketFromUnknownSender) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  rtcp::SenderReport sr;
  sr.SetSenderSsrc(kUnknownSenderSsrc);

  // The parser will handle report blocks in Sender Report from other than their
  // expected peer.
  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks);
  receiver.IncomingPacket(sr.Build());

  // But will not flag that he's gotten sender information.
  EXPECT_FALSE(receiver.GetSenderReportStats());
}

TEST(RtcpReceiverTest, InjectSrPacketCalculatesRTT) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  const TimeDelta kRtt = TimeDelta::Millis(123);
  const uint32_t kDelayNtp = 0x4321;
  const TimeDelta kDelay = CompactNtpRttToTimeDelta(kDelayNtp);

  EXPECT_EQ(receiver.LastRtt(), std::nullopt);

  uint32_t sent_ntp = CompactNtp(mocks.clock.CurrentNtpTime());
  mocks.clock.AdvanceTime(kRtt + kDelay);

  rtcp::SenderReport sr;
  sr.SetSenderSsrc(kSenderSsrc);
  rtcp::ReportBlock block;
  block.SetMediaSsrc(kReceiverMainSsrc);
  block.SetLastSr(sent_ntp);
  block.SetDelayLastSr(kDelayNtp);
  sr.AddReportBlock(block);

  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks);
  EXPECT_CALL(mocks.network_link_rtcp_observer, OnRttUpdate);
  receiver.IncomingPacket(sr.Build());

  EXPECT_THAT(receiver.LastRtt(), Near(kRtt));
}

TEST(RtcpReceiverTest, InjectSrPacketCalculatesNegativeRTTAsOneMs) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  const TimeDelta kRtt = TimeDelta::Millis(-13);
  const uint32_t kDelayNtp = 0x4321;
  const TimeDelta kDelay = CompactNtpRttToTimeDelta(kDelayNtp);

  EXPECT_EQ(receiver.LastRtt(), std::nullopt);

  uint32_t sent_ntp = CompactNtp(mocks.clock.CurrentNtpTime());
  mocks.clock.AdvanceTime(kRtt + kDelay);

  rtcp::SenderReport sr;
  sr.SetSenderSsrc(kSenderSsrc);
  rtcp::ReportBlock block;
  block.SetMediaSsrc(kReceiverMainSsrc);
  block.SetLastSr(sent_ntp);
  block.SetDelayLastSr(kDelayNtp);
  sr.AddReportBlock(block);

  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks(SizeIs(1)));
  EXPECT_CALL(mocks.network_link_rtcp_observer,
              OnRttUpdate(_, TimeDelta::Millis(1)));
  receiver.IncomingPacket(sr.Build());

  EXPECT_EQ(receiver.LastRtt(), TimeDelta::Millis(1));
}

TEST(RtcpReceiverTest, TwoReportBlocksWithLastOneWithoutLastSrCalculatesRtt) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  const TimeDelta kRtt = TimeDelta::Millis(125);
  const uint32_t kDelayNtp = 123000;
  const TimeDelta kDelay = CompactNtpRttToTimeDelta(kDelayNtp);

  uint32_t sent_ntp = CompactNtp(mocks.clock.CurrentNtpTime());
  mocks.clock.AdvanceTime(kRtt + kDelay);

  rtcp::SenderReport sr;
  sr.SetSenderSsrc(kSenderSsrc);
  rtcp::ReportBlock block;
  block.SetMediaSsrc(kReceiverMainSsrc);
  block.SetLastSr(sent_ntp);
  block.SetDelayLastSr(kDelayNtp);
  sr.AddReportBlock(block);
  block.SetMediaSsrc(kReceiverExtraSsrc);
  block.SetLastSr(0);
  sr.AddReportBlock(block);

  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks(SizeIs(2)));
  EXPECT_CALL(mocks.network_link_rtcp_observer, OnRttUpdate(_, kRtt));
  receiver.IncomingPacket(sr.Build());
}

TEST(RtcpReceiverTest, InjectRrPacket) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  rtcp::ReceiverReport rr;
  rr.SetSenderSsrc(kSenderSsrc);

  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks(IsEmpty()));
  receiver.IncomingPacket(rr.Build());

  EXPECT_THAT(receiver.GetLatestReportBlockData(), IsEmpty());
}

TEST(RtcpReceiverTest, InjectRrPacketWithReportBlockNotToUsIgnored) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  rtcp::ReportBlock rb;
  rb.SetMediaSsrc(kNotToUsSsrc);
  rtcp::ReceiverReport rr;
  rr.SetSenderSsrc(kSenderSsrc);
  rr.AddReportBlock(rb);

  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks(IsEmpty()));
  EXPECT_CALL(mocks.network_link_rtcp_observer, OnReport).Times(0);
  receiver.IncomingPacket(rr.Build());

  EXPECT_THAT(receiver.GetLatestReportBlockData(), IsEmpty());
}

TEST(RtcpReceiverTest, InjectRrPacketWithOneReportBlock) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  Timestamp now = mocks.clock.CurrentTime();

  rtcp::ReportBlock rb;
  rb.SetMediaSsrc(kReceiverMainSsrc);
  rtcp::ReceiverReport rr;
  rr.SetSenderSsrc(kSenderSsrc);
  rr.AddReportBlock(rb);

  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks(SizeIs(1)));
  EXPECT_CALL(mocks.network_link_rtcp_observer, OnReport(now, SizeIs(1)));
  receiver.IncomingPacket(rr.Build());

  EXPECT_THAT(receiver.GetLatestReportBlockData(), SizeIs(1));
}

TEST(RtcpReceiverTest, InjectSrPacketWithOneReportBlock) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  Timestamp now = mocks.clock.CurrentTime();

  rtcp::ReportBlock rb;
  rb.SetMediaSsrc(kReceiverMainSsrc);
  rtcp::SenderReport sr;
  sr.SetSenderSsrc(kSenderSsrc);
  sr.AddReportBlock(rb);

  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks(SizeIs(1)));
  EXPECT_CALL(mocks.network_link_rtcp_observer, OnReport(now, SizeIs(1)));
  receiver.IncomingPacket(sr.Build());

  EXPECT_THAT(receiver.GetLatestReportBlockData(), SizeIs(1));
}

TEST(RtcpReceiverTest, InjectRrPacketWithTwoReportBlocks) {
  const uint16_t kSequenceNumbers[] = {10, 12423};
  const uint32_t kCumLost[] = {13, 555};
  const uint8_t kFracLost[] = {20, 11};
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  Timestamp now = mocks.clock.CurrentTime();

  rtcp::ReportBlock rb1;
  rb1.SetMediaSsrc(kReceiverMainSsrc);
  rb1.SetExtHighestSeqNum(kSequenceNumbers[0]);
  rb1.SetFractionLost(10);

  rtcp::ReportBlock rb2;
  rb2.SetMediaSsrc(kReceiverExtraSsrc);
  rb2.SetExtHighestSeqNum(kSequenceNumbers[1]);
  rb2.SetFractionLost(0);

  rtcp::ReceiverReport rr1;
  rr1.SetSenderSsrc(kSenderSsrc);
  rr1.AddReportBlock(rb1);
  rr1.AddReportBlock(rb2);

  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks(SizeIs(2)));
  EXPECT_CALL(mocks.network_link_rtcp_observer, OnReport(now, SizeIs(2)));
  receiver.IncomingPacket(rr1.Build());

  EXPECT_THAT(
      receiver.GetLatestReportBlockData(),
      UnorderedElementsAre(Property(&ReportBlockData::fraction_lost_raw, 0),
                           Property(&ReportBlockData::fraction_lost_raw, 10)));

  // Insert next receiver report with same ssrc but new values.
  rtcp::ReportBlock rb3;
  rb3.SetMediaSsrc(kReceiverMainSsrc);
  rb3.SetExtHighestSeqNum(kSequenceNumbers[0]);
  rb3.SetFractionLost(kFracLost[0]);
  rb3.SetCumulativeLost(kCumLost[0]);

  rtcp::ReportBlock rb4;
  rb4.SetMediaSsrc(kReceiverExtraSsrc);
  rb4.SetExtHighestSeqNum(kSequenceNumbers[1]);
  rb4.SetFractionLost(kFracLost[1]);
  rb4.SetCumulativeLost(kCumLost[1]);

  rtcp::ReceiverReport rr2;
  rr2.SetSenderSsrc(kSenderSsrc);
  rr2.AddReportBlock(rb3);
  rr2.AddReportBlock(rb4);

  // Advance time to make 1st sent time and 2nd sent time different.
  mocks.clock.AdvanceTimeMilliseconds(500);
  now = mocks.clock.CurrentTime();

  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks(SizeIs(2)));
  EXPECT_CALL(mocks.network_link_rtcp_observer, OnReport(now, SizeIs(2)));
  receiver.IncomingPacket(rr2.Build());

  EXPECT_THAT(
      receiver.GetLatestReportBlockData(),
      UnorderedElementsAre(
          AllOf(Property(&ReportBlockData::source_ssrc, kReceiverMainSsrc),
                Property(&ReportBlockData::fraction_lost_raw, kFracLost[0]),
                Property(&ReportBlockData::cumulative_lost, kCumLost[0]),
                Property(&ReportBlockData::extended_highest_sequence_number,
                         kSequenceNumbers[0])),
          AllOf(Property(&ReportBlockData::source_ssrc, kReceiverExtraSsrc),
                Property(&ReportBlockData::fraction_lost_raw, kFracLost[1]),
                Property(&ReportBlockData::cumulative_lost, kCumLost[1]),
                Property(&ReportBlockData::extended_highest_sequence_number,
                         kSequenceNumbers[1]))));
}

TEST(RtcpReceiverTest,
     InjectRrPacketsFromTwoRemoteSsrcsReturnsLatestReportBlock) {
  const uint32_t kSenderSsrc2 = 0x20304;
  const uint16_t kSequenceNumbers[] = {10, 12423};
  const int32_t kCumLost[] = {13, 555};
  const uint8_t kFracLost[] = {20, 11};
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  rtcp::ReportBlock rb1;
  rb1.SetMediaSsrc(kReceiverMainSsrc);
  rb1.SetExtHighestSeqNum(kSequenceNumbers[0]);
  rb1.SetFractionLost(kFracLost[0]);
  rb1.SetCumulativeLost(kCumLost[0]);
  rtcp::ReceiverReport rr1;
  rr1.SetSenderSsrc(kSenderSsrc);
  rr1.AddReportBlock(rb1);

  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks(SizeIs(1)));
  receiver.IncomingPacket(rr1.Build());

  EXPECT_THAT(receiver.GetLatestReportBlockData(),
              ElementsAre(AllOf(
                  Property(&ReportBlockData::source_ssrc, kReceiverMainSsrc),
                  Property(&ReportBlockData::sender_ssrc, kSenderSsrc),
                  Property(&ReportBlockData::fraction_lost_raw, kFracLost[0]),
                  Property(&ReportBlockData::cumulative_lost, kCumLost[0]),
                  Property(&ReportBlockData::extended_highest_sequence_number,
                           kSequenceNumbers[0]))));

  rtcp::ReportBlock rb2;
  rb2.SetMediaSsrc(kReceiverMainSsrc);
  rb2.SetExtHighestSeqNum(kSequenceNumbers[1]);
  rb2.SetFractionLost(kFracLost[1]);
  rb2.SetCumulativeLost(kCumLost[1]);
  rtcp::ReceiverReport rr2;
  rr2.SetSenderSsrc(kSenderSsrc2);
  rr2.AddReportBlock(rb2);

  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks(SizeIs(1)));
  receiver.IncomingPacket(rr2.Build());

  EXPECT_THAT(receiver.GetLatestReportBlockData(),
              UnorderedElementsAre(AllOf(
                  Property(&ReportBlockData::source_ssrc, kReceiverMainSsrc),
                  Property(&ReportBlockData::sender_ssrc, kSenderSsrc2),
                  Property(&ReportBlockData::fraction_lost_raw, kFracLost[1]),
                  Property(&ReportBlockData::cumulative_lost, kCumLost[1]),
                  Property(&ReportBlockData::extended_highest_sequence_number,
                           kSequenceNumbers[1]))));
}

TEST(RtcpReceiverTest, NotifiesNetworkLinkObserverOnReportBlocks) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  rtcp::ReportBlock rb1;
  rb1.SetMediaSsrc(kReceiverMainSsrc);
  rb1.SetFractionLost(10);

  rtcp::ReportBlock rb2;
  rb2.SetMediaSsrc(kNotToUsSsrc);
  rb2.SetFractionLost(20);

  rtcp::ReportBlock rb3;
  rb3.SetMediaSsrc(kReceiverExtraSsrc);
  rb3.SetFractionLost(0);

  rtcp::ReceiverReport rr;
  rr.SetSenderSsrc(kSenderSsrc);
  rr.AddReportBlock(rb1);
  rr.AddReportBlock(rb2);
  rr.AddReportBlock(rb3);

  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks(SizeIs(2)));
  EXPECT_CALL(mocks.network_link_rtcp_observer,
              OnReport(mocks.clock.CurrentTime(),
                       UnorderedElementsAre(
                           Property(&ReportBlockData::fraction_lost_raw, 0),
                           Property(&ReportBlockData::fraction_lost_raw, 10))));
  receiver.IncomingPacket(rr.Build());
}

TEST(RtcpReceiverTest, GetRtt) {
  const uint32_t kSentCompactNtp = 0x1234;
  const uint32_t kDelayCompactNtp = 0x222;
  ReceiverMocks mocks;
  mocks.config.network_link_rtcp_observer = &mocks.network_link_rtcp_observer;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  // No report block received.
  EXPECT_EQ(receiver.LastRtt(), std::nullopt);
  EXPECT_EQ(receiver.AverageRtt(), std::nullopt);

  rtcp::ReportBlock rb;
  rb.SetMediaSsrc(kReceiverMainSsrc);
  rb.SetLastSr(kSentCompactNtp);
  rb.SetDelayLastSr(kDelayCompactNtp);

  rtcp::ReceiverReport rr;
  rr.SetSenderSsrc(kSenderSsrc);
  rr.AddReportBlock(rb);

  Timestamp now = mocks.clock.CurrentTime();
  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks);
  EXPECT_CALL(mocks.network_link_rtcp_observer,
              OnRttUpdate(now, Gt(TimeDelta::Zero())));
  receiver.IncomingPacket(rr.Build());

  EXPECT_NE(receiver.LastRtt(), std::nullopt);
  EXPECT_NE(receiver.AverageRtt(), std::nullopt);
}

// App packets are ignored.
TEST(RtcpReceiverTest, InjectApp) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  rtcp::App app;
  app.SetSubType(30);
  app.SetName(0x17a177e);
  const uint8_t kData[] = {'t', 'e', 's', 't', 'd', 'a', 't', 'a'};
  app.SetData(kData, sizeof(kData));

  receiver.IncomingPacket(app.Build());
}

TEST(RtcpReceiverTest, InjectSdesWithOneChunk) {
  ReceiverMocks mocks;
  MockCnameCallbackImpl callback;
  mocks.config.rtcp_cname_callback = &callback;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  const char kCname[] = "alice@host";
  rtcp::Sdes sdes;
  sdes.AddCName(kSenderSsrc, kCname);

  EXPECT_CALL(callback, OnCname(kSenderSsrc, StrEq(kCname)));
  receiver.IncomingPacket(sdes.Build());
}

TEST(RtcpReceiverTest, InjectByePacketRemovesReportBlocks) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  rtcp::ReportBlock rb1;
  rb1.SetMediaSsrc(kReceiverMainSsrc);
  rtcp::ReportBlock rb2;
  rb2.SetMediaSsrc(kReceiverExtraSsrc);
  rtcp::ReceiverReport rr;
  rr.SetSenderSsrc(kSenderSsrc);
  rr.AddReportBlock(rb1);
  rr.AddReportBlock(rb2);

  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks);
  receiver.IncomingPacket(rr.Build());

  EXPECT_THAT(receiver.GetLatestReportBlockData(), SizeIs(2));

  // Verify that BYE removes the report blocks.
  rtcp::Bye bye;
  bye.SetSenderSsrc(kSenderSsrc);

  receiver.IncomingPacket(bye.Build());

  EXPECT_THAT(receiver.GetLatestReportBlockData(), IsEmpty());

  // Inject packet again.
  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks);
  receiver.IncomingPacket(rr.Build());

  EXPECT_THAT(receiver.GetLatestReportBlockData(), SizeIs(2));
}

TEST(RtcpReceiverTest, InjectByePacketRemovesReferenceTimeInfo) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  rtcp::ExtendedReports xr;
  xr.SetSenderSsrc(kSenderSsrc);
  rtcp::Rrtr rrtr;
  rrtr.SetNtp(NtpTime(0x10203, 0x40506));
  xr.SetRrtr(rrtr);
  receiver.IncomingPacket(xr.Build());

  rtcp::Bye bye;
  bye.SetSenderSsrc(kSenderSsrc);
  receiver.IncomingPacket(bye.Build());

  EXPECT_THAT(receiver.ConsumeReceivedXrReferenceTimeInfo(), IsEmpty());
}

TEST(RtcpReceiverTest, InjectPliPacket) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  rtcp::Pli pli;
  pli.SetMediaSsrc(kReceiverMainSsrc);

  EXPECT_CALL(
      mocks.packet_type_counter_observer,
      RtcpPacketTypesCounterUpdated(
          kReceiverMainSsrc, Field(&RtcpPacketTypeCounter::pli_packets, 1)));
  EXPECT_CALL(mocks.intra_frame_observer,
              OnReceivedIntraFrameRequest(kReceiverMainSsrc));
  receiver.IncomingPacket(pli.Build());
}

TEST(RtcpReceiverTest, PliPacketNotToUsIgnored) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  rtcp::Pli pli;
  pli.SetMediaSsrc(kNotToUsSsrc);

  EXPECT_CALL(
      mocks.packet_type_counter_observer,
      RtcpPacketTypesCounterUpdated(
          kReceiverMainSsrc, Field(&RtcpPacketTypeCounter::pli_packets, 0)));
  EXPECT_CALL(mocks.intra_frame_observer, OnReceivedIntraFrameRequest).Times(0);
  receiver.IncomingPacket(pli.Build());
}

TEST(RtcpReceiverTest, InjectFirPacket) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  rtcp::Fir fir;
  fir.AddRequestTo(kReceiverMainSsrc, 13);

  EXPECT_CALL(
      mocks.packet_type_counter_observer,
      RtcpPacketTypesCounterUpdated(
          kReceiverMainSsrc, Field(&RtcpPacketTypeCounter::fir_packets, 1)));
  EXPECT_CALL(mocks.intra_frame_observer,
              OnReceivedIntraFrameRequest(kReceiverMainSsrc));
  receiver.IncomingPacket(fir.Build());
}

TEST(RtcpReceiverTest, FirPacketNotToUsIgnored) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  rtcp::Fir fir;
  fir.AddRequestTo(kNotToUsSsrc, 13);

  EXPECT_CALL(mocks.intra_frame_observer, OnReceivedIntraFrameRequest).Times(0);
  receiver.IncomingPacket(fir.Build());
}

TEST(RtcpReceiverTest, ExtendedReportsPacketWithZeroReportBlocksIgnored) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  rtcp::ExtendedReports xr;
  xr.SetSenderSsrc(kSenderSsrc);

  receiver.IncomingPacket(xr.Build());
}

TEST(RtcpReceiverTest, InjectExtendedReportsReceiverReferenceTimePacket) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  const NtpTime kNtp(0x10203, 0x40506);
  rtcp::Rrtr rrtr;
  rrtr.SetNtp(kNtp);
  rtcp::ExtendedReports xr;
  xr.SetSenderSsrc(kSenderSsrc);
  xr.SetRrtr(rrtr);

  std::vector<rtcp::ReceiveTimeInfo> last_xr_rtis =
      receiver.ConsumeReceivedXrReferenceTimeInfo();
  EXPECT_THAT(last_xr_rtis, IsEmpty());

  receiver.IncomingPacket(xr.Build());

  last_xr_rtis = receiver.ConsumeReceivedXrReferenceTimeInfo();
  ASSERT_THAT(last_xr_rtis, SizeIs(1));
  EXPECT_EQ(kSenderSsrc, last_xr_rtis[0].ssrc);
  EXPECT_EQ(CompactNtp(kNtp), last_xr_rtis[0].last_rr);
  EXPECT_EQ(0U, last_xr_rtis[0].delay_since_last_rr);
}

TEST(RtcpReceiverTest, ExtendedReportsDlrrPacketNotToUsIgnored) {
  ReceiverMocks mocks;
  // Allow calculate rtt using dlrr/rrtr, simulating media receiver side.
  mocks.config.non_sender_rtt_measurement = true;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  rtcp::ExtendedReports xr;
  xr.SetSenderSsrc(kSenderSsrc);
  xr.AddDlrrItem(ReceiveTimeInfo(kNotToUsSsrc, 0x12345, 0x67890));

  receiver.IncomingPacket(xr.Build());

  EXPECT_FALSE(receiver.GetAndResetXrRrRtt());
  RTCPReceiver::NonSenderRttStats non_sender_rtt_stats =
      receiver.GetNonSenderRTT();
  EXPECT_FALSE(non_sender_rtt_stats.round_trip_time().has_value());
  EXPECT_TRUE(non_sender_rtt_stats.total_round_trip_time().IsZero());
  EXPECT_EQ(non_sender_rtt_stats.round_trip_time_measurements(), 0);
}

TEST(RtcpReceiverTest, InjectExtendedReportsDlrrPacketWithSubBlock) {
  ReceiverMocks mocks;
  mocks.config.non_sender_rtt_measurement = true;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  const uint32_t kLastRR = 0x12345;
  const uint32_t kDelay = 0x23456;
  EXPECT_FALSE(receiver.GetAndResetXrRrRtt());

  rtcp::ExtendedReports xr;
  xr.SetSenderSsrc(kSenderSsrc);
  xr.AddDlrrItem(ReceiveTimeInfo(kReceiverMainSsrc, kLastRR, kDelay));

  receiver.IncomingPacket(xr.Build());

  uint32_t compact_ntp_now = CompactNtp(mocks.clock.CurrentNtpTime());
  uint32_t rtt_ntp = compact_ntp_now - kDelay - kLastRR;
  EXPECT_THAT(receiver.GetAndResetXrRrRtt(),
              Near(CompactNtpRttToTimeDelta(rtt_ntp)));
  RTCPReceiver::NonSenderRttStats non_sender_rtt_stats =
      receiver.GetNonSenderRTT();
  EXPECT_GT(non_sender_rtt_stats.round_trip_time(), TimeDelta::Zero());
  EXPECT_FALSE(non_sender_rtt_stats.total_round_trip_time().IsZero());
  EXPECT_GT(non_sender_rtt_stats.round_trip_time_measurements(), 0);
}

TEST(RtcpReceiverTest, InjectExtendedReportsDlrrPacketWithMultipleSubBlocks) {
  ReceiverMocks mocks;
  mocks.config.non_sender_rtt_measurement = true;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  const uint32_t kLastRR = 0x12345;
  const uint32_t kDelay = 0x56789;

  rtcp::ExtendedReports xr;
  xr.SetSenderSsrc(kSenderSsrc);
  xr.AddDlrrItem(ReceiveTimeInfo(kReceiverMainSsrc, kLastRR, kDelay));
  xr.AddDlrrItem(ReceiveTimeInfo(kReceiverMainSsrc + 1, 0x12345, 0x67890));
  xr.AddDlrrItem(ReceiveTimeInfo(kReceiverMainSsrc + 2, 0x12345, 0x67890));

  receiver.IncomingPacket(xr.Build());

  uint32_t compact_ntp_now = CompactNtp(mocks.clock.CurrentNtpTime());
  uint32_t rtt_ntp = compact_ntp_now - kDelay - kLastRR;
  EXPECT_THAT(receiver.GetAndResetXrRrRtt(),
              Near(CompactNtpRttToTimeDelta(rtt_ntp)));
  RTCPReceiver::NonSenderRttStats non_sender_rtt_stats =
      receiver.GetNonSenderRTT();
  EXPECT_GT(non_sender_rtt_stats.round_trip_time(), TimeDelta::Zero());
  EXPECT_FALSE(non_sender_rtt_stats.total_round_trip_time().IsZero());
  EXPECT_GT(non_sender_rtt_stats.round_trip_time_measurements(), 0);
}

TEST(RtcpReceiverTest, InjectExtendedReportsPacketWithMultipleReportBlocks) {
  ReceiverMocks mocks;
  mocks.config.non_sender_rtt_measurement = true;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  rtcp::Rrtr rrtr;
  rtcp::ExtendedReports xr;
  xr.SetSenderSsrc(kSenderSsrc);
  xr.SetRrtr(rrtr);
  xr.AddDlrrItem(ReceiveTimeInfo(kReceiverMainSsrc, 0x12345, 0x67890));

  receiver.IncomingPacket(xr.Build());

  std::vector<rtcp::ReceiveTimeInfo> last_xr_rtis =
      receiver.ConsumeReceivedXrReferenceTimeInfo();
  EXPECT_THAT(last_xr_rtis, SizeIs(1));
  EXPECT_TRUE(receiver.GetAndResetXrRrRtt());
}

TEST(RtcpReceiverTest, InjectExtendedReportsPacketWithUnknownReportBlock) {
  ReceiverMocks mocks;
  mocks.config.non_sender_rtt_measurement = true;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  rtcp::Rrtr rrtr;
  rtcp::ExtendedReports xr;
  xr.SetSenderSsrc(kSenderSsrc);
  xr.SetRrtr(rrtr);
  xr.AddDlrrItem(ReceiveTimeInfo(kReceiverMainSsrc, 0x12345, 0x67890));

  Buffer packet = xr.Build();
  // Modify the DLRR block to have an unsupported block type, from 5 to 6.
  ASSERT_EQ(5, packet.data()[20]);
  packet.data()[20] = 6;
  receiver.IncomingPacket(packet);

  // Validate Rrtr was received and processed.
  std::vector<rtcp::ReceiveTimeInfo> last_xr_rtis =
      receiver.ConsumeReceivedXrReferenceTimeInfo();
  EXPECT_THAT(last_xr_rtis, SizeIs(1));
  // Validate Dlrr report wasn't processed.
  EXPECT_FALSE(receiver.GetAndResetXrRrRtt());
  RTCPReceiver::NonSenderRttStats non_sender_rtt_stats =
      receiver.GetNonSenderRTT();
  EXPECT_FALSE(non_sender_rtt_stats.round_trip_time().has_value());
  EXPECT_TRUE(non_sender_rtt_stats.total_round_trip_time().IsZero());
  EXPECT_EQ(non_sender_rtt_stats.round_trip_time_measurements(), 0);
}

TEST(RtcpReceiverTest, TestExtendedReportsRrRttInitiallyFalse) {
  ReceiverMocks mocks;
  mocks.config.non_sender_rtt_measurement = true;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  EXPECT_FALSE(receiver.GetAndResetXrRrRtt());
  RTCPReceiver::NonSenderRttStats non_sender_rtt_stats =
      receiver.GetNonSenderRTT();
  EXPECT_FALSE(non_sender_rtt_stats.round_trip_time().has_value());
  EXPECT_TRUE(non_sender_rtt_stats.total_round_trip_time().IsZero());
  EXPECT_EQ(non_sender_rtt_stats.round_trip_time_measurements(), 0);
}

TEST(RtcpReceiverTest, RttCalculatedAfterExtendedReportsDlrr) {
  ReceiverMocks mocks;
  mocks.config.non_sender_rtt_measurement = true;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  Random rand(0x0123456789abcdef);
  const TimeDelta kRtt = TimeDelta::Millis(rand.Rand(1, 9 * 3600 * 1000));
  const uint32_t kDelayNtp = rand.Rand(0, 0x7fffffff);
  const TimeDelta kDelay = CompactNtpRttToTimeDelta(kDelayNtp);
  NtpTime now = mocks.clock.CurrentNtpTime();
  uint32_t sent_ntp = CompactNtp(now);
  mocks.clock.AdvanceTime(kRtt + kDelay);

  rtcp::ExtendedReports xr;
  xr.SetSenderSsrc(kSenderSsrc);
  xr.AddDlrrItem(ReceiveTimeInfo(kReceiverMainSsrc, sent_ntp, kDelayNtp));

  receiver.IncomingPacket(xr.Build());

  EXPECT_THAT(receiver.GetAndResetXrRrRtt(), Near(kRtt));
  RTCPReceiver::NonSenderRttStats non_sender_rtt_stats =
      receiver.GetNonSenderRTT();
  EXPECT_TRUE(non_sender_rtt_stats.round_trip_time().has_value());
  EXPECT_FALSE(non_sender_rtt_stats.round_trip_time().value().IsZero());
  EXPECT_FALSE(non_sender_rtt_stats.total_round_trip_time().IsZero());
  EXPECT_GT(non_sender_rtt_stats.round_trip_time_measurements(), 0);
}

// Same test as above but enables receive-side RTT using the setter instead of
// the config struct.
TEST(RtcpReceiverTest, SetterEnablesReceiverRtt) {
  ReceiverMocks mocks;
  mocks.config.non_sender_rtt_measurement = false;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);
  receiver.SetNonSenderRttMeasurement(true);

  Random rand(0x0123456789abcdef);
  const TimeDelta kRtt = TimeDelta::Millis(rand.Rand(1, 9 * 3600 * 1000));
  const uint32_t kDelayNtp = rand.Rand(0, 0x7fffffff);
  const TimeDelta kDelay = CompactNtpRttToTimeDelta(kDelayNtp);
  NtpTime now = mocks.clock.CurrentNtpTime();
  uint32_t sent_ntp = CompactNtp(now);
  mocks.clock.AdvanceTime(kRtt + kDelay);

  rtcp::ExtendedReports xr;
  xr.SetSenderSsrc(kSenderSsrc);
  xr.AddDlrrItem(ReceiveTimeInfo(kReceiverMainSsrc, sent_ntp, kDelayNtp));

  receiver.IncomingPacket(xr.Build());

  EXPECT_THAT(receiver.GetAndResetXrRrRtt(), Near(kRtt));
  RTCPReceiver::NonSenderRttStats non_sender_rtt_stats =
      receiver.GetNonSenderRTT();
  EXPECT_TRUE(non_sender_rtt_stats.round_trip_time().has_value());
  EXPECT_FALSE(non_sender_rtt_stats.round_trip_time().value().IsZero());
  EXPECT_FALSE(non_sender_rtt_stats.total_round_trip_time().IsZero());
  EXPECT_GT(non_sender_rtt_stats.round_trip_time_measurements(), 0);
}

// Same test as above but disables receive-side RTT using the setter instead of
// the config struct.
TEST(RtcpReceiverTest, DoesntCalculateRttOnReceivedDlrr) {
  ReceiverMocks mocks;
  mocks.config.non_sender_rtt_measurement = true;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);
  receiver.SetNonSenderRttMeasurement(false);

  Random rand(0x0123456789abcdef);
  const TimeDelta kRtt = TimeDelta::Millis(rand.Rand(1, 9 * 3600 * 1000));
  const uint32_t kDelayNtp = rand.Rand(0, 0x7fffffff);
  const TimeDelta kDelay = CompactNtpRttToTimeDelta(kDelayNtp);
  NtpTime now = mocks.clock.CurrentNtpTime();
  uint32_t sent_ntp = CompactNtp(now);
  mocks.clock.AdvanceTime(kRtt + kDelay);

  rtcp::ExtendedReports xr;
  xr.SetSenderSsrc(kSenderSsrc);
  xr.AddDlrrItem(ReceiveTimeInfo(kReceiverMainSsrc, sent_ntp, kDelayNtp));

  receiver.IncomingPacket(xr.Build());

  // We expect that no RTT is available (because receive-side RTT was disabled).
  EXPECT_FALSE(receiver.GetAndResetXrRrRtt());
  RTCPReceiver::NonSenderRttStats non_sender_rtt_stats =
      receiver.GetNonSenderRTT();
  EXPECT_FALSE(non_sender_rtt_stats.round_trip_time().has_value());
  EXPECT_TRUE(non_sender_rtt_stats.total_round_trip_time().IsZero());
  EXPECT_EQ(non_sender_rtt_stats.round_trip_time_measurements(), 0);
}

TEST(RtcpReceiverTest, XrDlrrCalculatesNegativeRttAsOneMillisecond) {
  ReceiverMocks mocks;
  mocks.config.non_sender_rtt_measurement = true;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  Random rand(0x0123456789abcdef);
  const TimeDelta kRtt = TimeDelta::Millis(rand.Rand(-3600 * 1000, -1));
  const uint32_t kDelayNtp = rand.Rand(0, 0x7fffffff);
  const TimeDelta kDelay = CompactNtpRttToTimeDelta(kDelayNtp);
  NtpTime now = mocks.clock.CurrentNtpTime();
  uint32_t sent_ntp = CompactNtp(now);
  mocks.clock.AdvanceTime(kRtt + kDelay);

  rtcp::ExtendedReports xr;
  xr.SetSenderSsrc(kSenderSsrc);
  xr.AddDlrrItem(ReceiveTimeInfo(kReceiverMainSsrc, sent_ntp, kDelayNtp));

  receiver.IncomingPacket(xr.Build());

  EXPECT_EQ(receiver.GetAndResetXrRrRtt(), TimeDelta::Millis(1));
  RTCPReceiver::NonSenderRttStats non_sender_rtt_stats =
      receiver.GetNonSenderRTT();
  EXPECT_TRUE(non_sender_rtt_stats.round_trip_time().has_value());
  EXPECT_FALSE(non_sender_rtt_stats.round_trip_time().value().IsZero());
  EXPECT_FALSE(non_sender_rtt_stats.total_round_trip_time().IsZero());
  EXPECT_GT(non_sender_rtt_stats.round_trip_time_measurements(), 0);
}

// Test receiver RTT stats with multiple measurements.
TEST(RtcpReceiverTest, ReceiverRttWithMultipleMeasurements) {
  ReceiverMocks mocks;
  mocks.config.non_sender_rtt_measurement = true;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  Random rand(0x0123456789abcdef);
  const TimeDelta kRtt = TimeDelta::Millis(rand.Rand(1, 9 * 3600 * 1000));
  const uint32_t kDelayNtp = rand.Rand(0, 0x7fffffff);
  const TimeDelta kDelay = CompactNtpRttToTimeDelta(kDelayNtp);
  NtpTime now = mocks.clock.CurrentNtpTime();
  uint32_t sent_ntp = CompactNtp(now);
  mocks.clock.AdvanceTime(kRtt + kDelay);

  rtcp::ExtendedReports xr;
  xr.SetSenderSsrc(kSenderSsrc);
  xr.AddDlrrItem(ReceiveTimeInfo(kReceiverMainSsrc, sent_ntp, kDelayNtp));

  receiver.IncomingPacket(xr.Build());

  // Check that the non-sender RTT stats are valid and based on a single
  // measurement.
  RTCPReceiver::NonSenderRttStats non_sender_rtt_stats =
      receiver.GetNonSenderRTT();
  EXPECT_TRUE(non_sender_rtt_stats.round_trip_time().has_value());
  EXPECT_NEAR(non_sender_rtt_stats.round_trip_time()->ms(), kRtt.ms(), 1);
  EXPECT_EQ(non_sender_rtt_stats.round_trip_time_measurements(), 1);
  EXPECT_EQ(non_sender_rtt_stats.total_round_trip_time().ms(),
            non_sender_rtt_stats.round_trip_time()->ms());

  // Generate another XR report with the same RTT and delay.
  NtpTime now2 = mocks.clock.CurrentNtpTime();
  uint32_t sent_ntp2 = CompactNtp(now2);
  mocks.clock.AdvanceTime(kRtt + kDelay);

  rtcp::ExtendedReports xr2;
  xr2.SetSenderSsrc(kSenderSsrc);
  xr2.AddDlrrItem(ReceiveTimeInfo(kReceiverMainSsrc, sent_ntp2, kDelayNtp));

  receiver.IncomingPacket(xr2.Build());

  // Check that the non-sender RTT stats are based on 2 measurements, and that
  // the values are as expected.
  non_sender_rtt_stats = receiver.GetNonSenderRTT();
  EXPECT_TRUE(non_sender_rtt_stats.round_trip_time().has_value());
  EXPECT_NEAR(non_sender_rtt_stats.round_trip_time()->ms(), kRtt.ms(), 1);
  EXPECT_EQ(non_sender_rtt_stats.round_trip_time_measurements(), 2);
  EXPECT_NEAR(non_sender_rtt_stats.total_round_trip_time().ms(), 2 * kRtt.ms(),
              2);
}

// Test that the receiver RTT stat resets when receiving a SR without XR. This
// behavior is described in the standard, see
// https://www.w3.org/TR/webrtc-stats/#dom-rtcremoteoutboundrtpstreamstats-roundtriptime.
TEST(RtcpReceiverTest, ReceiverRttResetOnSrWithoutXr) {
  ReceiverMocks mocks;
  mocks.config.non_sender_rtt_measurement = true;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  Random rand(0x0123456789abcdef);
  const TimeDelta kRtt = TimeDelta::Millis(rand.Rand(1, 9 * 3600 * 1000));
  const uint32_t kDelayNtp = rand.Rand(0, 0x7fffffff);
  const TimeDelta kDelay = CompactNtpRttToTimeDelta(kDelayNtp);
  NtpTime now = mocks.clock.CurrentNtpTime();
  uint32_t sent_ntp = CompactNtp(now);
  mocks.clock.AdvanceTime(kRtt + kDelay);

  rtcp::ExtendedReports xr;
  xr.SetSenderSsrc(kSenderSsrc);
  xr.AddDlrrItem(ReceiveTimeInfo(kReceiverMainSsrc, sent_ntp, kDelayNtp));

  receiver.IncomingPacket(xr.Build());

  RTCPReceiver::NonSenderRttStats non_sender_rtt_stats =
      receiver.GetNonSenderRTT();
  EXPECT_TRUE(non_sender_rtt_stats.round_trip_time().has_value());
  EXPECT_NEAR(non_sender_rtt_stats.round_trip_time()->ms(), kRtt.ms(), 1);

  // Generate a SR without XR.
  rtcp::ReportBlock rb;
  rb.SetMediaSsrc(kReceiverMainSsrc);
  rtcp::SenderReport sr;
  sr.SetSenderSsrc(kSenderSsrc);
  sr.AddReportBlock(rb);
  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks);

  receiver.IncomingPacket(sr.Build());

  // Check that the non-sender RTT stat is not set.
  non_sender_rtt_stats = receiver.GetNonSenderRTT();
  EXPECT_FALSE(non_sender_rtt_stats.round_trip_time().has_value());
}

// Test that the receiver RTT stat resets when receiving a DLRR with a timestamp
// of zero. This behavior is described in the standard, see
// https://www.w3.org/TR/webrtc-stats/#dom-rtcremoteoutboundrtpstreamstats-roundtriptime.
TEST(RtcpReceiverTest, ReceiverRttResetOnDlrrWithZeroTimestamp) {
  ReceiverMocks mocks;
  mocks.config.non_sender_rtt_measurement = true;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  Random rand(0x0123456789abcdef);
  const TimeDelta kRtt = TimeDelta::Millis(rand.Rand(1, 9 * 3600 * 1000));
  const uint32_t kDelayNtp = rand.Rand(0, 0x7fffffff);
  const TimeDelta kDelay = CompactNtpRttToTimeDelta(kDelayNtp);
  NtpTime now = mocks.clock.CurrentNtpTime();
  uint32_t sent_ntp = CompactNtp(now);
  mocks.clock.AdvanceTime(kRtt + kDelay);

  rtcp::ExtendedReports xr;
  xr.SetSenderSsrc(kSenderSsrc);
  xr.AddDlrrItem(ReceiveTimeInfo(kReceiverMainSsrc, sent_ntp, kDelayNtp));

  receiver.IncomingPacket(xr.Build());

  RTCPReceiver::NonSenderRttStats non_sender_rtt_stats =
      receiver.GetNonSenderRTT();
  EXPECT_TRUE(non_sender_rtt_stats.round_trip_time().has_value());
  EXPECT_NEAR(non_sender_rtt_stats.round_trip_time()->ms(), kRtt.ms(), 1);

  // Generate an XR+DLRR with zero timestamp.
  rtcp::ExtendedReports xr2;
  xr2.SetSenderSsrc(kSenderSsrc);
  xr2.AddDlrrItem(ReceiveTimeInfo(kReceiverMainSsrc, 0, kDelayNtp));

  receiver.IncomingPacket(xr2.Build());

  // Check that the non-sender RTT stat is not set.
  non_sender_rtt_stats = receiver.GetNonSenderRTT();
  EXPECT_FALSE(non_sender_rtt_stats.round_trip_time().has_value());
}

// Check that the receiver RTT works correctly when the remote SSRC changes.
TEST(RtcpReceiverTest, ReceiverRttWithMultipleRemoteSsrcs) {
  ReceiverMocks mocks;
  mocks.config.non_sender_rtt_measurement = false;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);
  receiver.SetNonSenderRttMeasurement(true);

  Random rand(0x0123456789abcdef);
  const TimeDelta kRtt = TimeDelta::Millis(rand.Rand(1, 9 * 3600 * 1000));
  const uint32_t kDelayNtp = rand.Rand(0, 0x7fffffff);
  const TimeDelta kDelay = CompactNtpRttToTimeDelta(kDelayNtp);
  NtpTime now = mocks.clock.CurrentNtpTime();
  uint32_t sent_ntp = CompactNtp(now);
  mocks.clock.AdvanceTime(kRtt + kDelay);

  rtcp::ExtendedReports xr;
  xr.SetSenderSsrc(kSenderSsrc);
  xr.AddDlrrItem(ReceiveTimeInfo(kReceiverMainSsrc, sent_ntp, kDelayNtp));

  receiver.IncomingPacket(xr.Build());

  // Generate an XR report for another SSRC.
  const TimeDelta kRtt2 = TimeDelta::Millis(rand.Rand(1, 9 * 3600 * 1000));
  const uint32_t kDelayNtp2 = rand.Rand(0, 0x7fffffff);
  const TimeDelta kDelay2 = CompactNtpRttToTimeDelta(kDelayNtp2);
  NtpTime now2 = mocks.clock.CurrentNtpTime();
  uint32_t sent_ntp2 = CompactNtp(now2);
  mocks.clock.AdvanceTime(kRtt2 + kDelay2);

  // Check that the non-sender RTT stats match the first XR.
  RTCPReceiver::NonSenderRttStats non_sender_rtt_stats =
      receiver.GetNonSenderRTT();
  ASSERT_TRUE(non_sender_rtt_stats.round_trip_time().has_value());
  EXPECT_NEAR(non_sender_rtt_stats.round_trip_time()->ms(), kRtt.ms(), 1);
  EXPECT_FALSE(non_sender_rtt_stats.total_round_trip_time().IsZero());
  EXPECT_GT(non_sender_rtt_stats.round_trip_time_measurements(), 0);

  // Change the remote SSRC and check that the stats match the second XR.
  receiver.SetRemoteSSRC(kSenderSsrc + 1);

  rtcp::ExtendedReports xr2;
  xr2.SetSenderSsrc(kSenderSsrc + 1);
  xr2.AddDlrrItem(ReceiveTimeInfo(kReceiverMainSsrc, sent_ntp2, kDelayNtp2));
  receiver.IncomingPacket(xr2.Build());

  RTCPReceiver::NonSenderRttStats non_sender_rtt_stats2 =
      receiver.GetNonSenderRTT();
  ASSERT_TRUE(non_sender_rtt_stats2.round_trip_time().has_value());
  EXPECT_NEAR(non_sender_rtt_stats2.round_trip_time()->ms(), kRtt2.ms(), 1);
  EXPECT_FALSE(non_sender_rtt_stats2.total_round_trip_time().IsZero());
  EXPECT_GT(non_sender_rtt_stats2.round_trip_time_measurements(), 0);
}

TEST(RtcpReceiverTest, ConsumeReceivedXrReferenceTimeInfoInitiallyEmpty) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  EXPECT_THAT(receiver.ConsumeReceivedXrReferenceTimeInfo(), IsEmpty());
}

TEST(RtcpReceiverTest, ConsumeReceivedXrReferenceTimeInfo) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  const NtpTime kNtp(0x10203, 0x40506);
  const uint32_t kNtpMid = CompactNtp(kNtp);

  rtcp::Rrtr rrtr;
  rrtr.SetNtp(kNtp);
  rtcp::ExtendedReports xr;
  xr.SetSenderSsrc(kSenderSsrc);
  xr.SetRrtr(rrtr);

  receiver.IncomingPacket(xr.Build());

  mocks.clock.AdvanceTimeMilliseconds(1000);

  std::vector<rtcp::ReceiveTimeInfo> last_xr_rtis =
      receiver.ConsumeReceivedXrReferenceTimeInfo();
  ASSERT_THAT(last_xr_rtis, SizeIs(1));
  EXPECT_EQ(kSenderSsrc, last_xr_rtis[0].ssrc);
  EXPECT_EQ(kNtpMid, last_xr_rtis[0].last_rr);
  EXPECT_EQ(65536U, last_xr_rtis[0].delay_since_last_rr);
}

TEST(RtcpReceiverTest,
     ReceivedRrtrFromSameSsrcUpdatesReceivedReferenceTimeInfo) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  const NtpTime kNtp1(0x10203, 0x40506);
  const NtpTime kNtp2(0x11223, 0x44556);
  const int64_t kDelayMs = 2000;

  rtcp::ExtendedReports xr;
  xr.SetSenderSsrc(kSenderSsrc);
  rtcp::Rrtr rrtr1;
  rrtr1.SetNtp(kNtp1);
  xr.SetRrtr(rrtr1);
  receiver.IncomingPacket(xr.Build());
  mocks.clock.AdvanceTimeMilliseconds(kDelayMs);
  rtcp::Rrtr rrtr2;
  rrtr2.SetNtp(kNtp2);
  xr.SetRrtr(rrtr2);
  receiver.IncomingPacket(xr.Build());
  mocks.clock.AdvanceTimeMilliseconds(kDelayMs);

  std::vector<rtcp::ReceiveTimeInfo> last_xr_rtis =
      receiver.ConsumeReceivedXrReferenceTimeInfo();
  ASSERT_THAT(last_xr_rtis, SizeIs(1));
  EXPECT_EQ(kSenderSsrc, last_xr_rtis[0].ssrc);
  EXPECT_EQ(CompactNtp(kNtp2), last_xr_rtis[0].last_rr);
  EXPECT_EQ(kDelayMs * 65536 / 1000, last_xr_rtis[0].delay_since_last_rr);
}

TEST(RtcpReceiverTest, StoresLastReceivedRrtrPerSsrc) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  const size_t kNumBufferedReports = 1;
  const size_t kNumReports =
      rtcp::ExtendedReports::kMaxNumberOfDlrrItems + kNumBufferedReports;
  for (size_t i = 0; i < kNumReports; ++i) {
    rtcp::ExtendedReports xr;
    xr.SetSenderSsrc(i * 100);
    rtcp::Rrtr rrtr;
    rrtr.SetNtp(NtpTime(i * 200, i * 300));
    xr.SetRrtr(rrtr);
    receiver.IncomingPacket(xr.Build());
    mocks.clock.AdvanceTimeMilliseconds(1000);
  }

  std::vector<rtcp::ReceiveTimeInfo> last_xr_rtis =
      receiver.ConsumeReceivedXrReferenceTimeInfo();
  ASSERT_THAT(last_xr_rtis,
              SizeIs(rtcp::ExtendedReports::kMaxNumberOfDlrrItems));
  for (size_t i = 0; i < rtcp::ExtendedReports::kMaxNumberOfDlrrItems; ++i) {
    EXPECT_EQ(i * 100, last_xr_rtis[i].ssrc);
    EXPECT_EQ(CompactNtp(NtpTime(i * 200, i * 300)), last_xr_rtis[i].last_rr);
    EXPECT_EQ(65536U * (kNumReports - i), last_xr_rtis[i].delay_since_last_rr);
  }

  last_xr_rtis = receiver.ConsumeReceivedXrReferenceTimeInfo();
  ASSERT_THAT(last_xr_rtis, SizeIs(kNumBufferedReports));
}

TEST(RtcpReceiverTest, TmmbrReceivedWithNoIncomingPacket) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  EXPECT_THAT(receiver.TmmbrReceived(), IsEmpty());
}

TEST(RtcpReceiverTest, TmmbrPacketAccepted) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  const DataRate kBitrate = DataRate::BitsPerSec(30'000);
  auto tmmbr = std::make_unique<rtcp::Tmmbr>();
  tmmbr->SetSenderSsrc(kSenderSsrc);
  tmmbr->AddTmmbr(rtcp::TmmbItem(kReceiverMainSsrc, kBitrate.bps(), 0));
  auto sr = std::make_unique<rtcp::SenderReport>();
  sr->SetSenderSsrc(kSenderSsrc);
  rtcp::CompoundPacket compound;
  compound.Append(std::move(sr));
  compound.Append(std::move(tmmbr));

  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks);
  EXPECT_CALL(mocks.rtp_rtcp_impl, SetTmmbn(SizeIs(1)));
  EXPECT_CALL(mocks.network_link_rtcp_observer,
              OnReceiverEstimatedMaxBitrate(_, kBitrate));
  receiver.IncomingPacket(compound.Build());

  EXPECT_THAT(
      receiver.TmmbrReceived(),
      ElementsAre(AllOf(
          Property(&rtcp::TmmbItem::bitrate_bps, Eq(kBitrate.bps<uint64_t>())),
          Property(&rtcp::TmmbItem::ssrc, Eq(kSenderSsrc)))));
}

TEST(RtcpReceiverTest, TmmbrPacketNotForUsIgnored) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  const uint32_t kBitrateBps = 30000;
  auto tmmbr = std::make_unique<rtcp::Tmmbr>();
  tmmbr->SetSenderSsrc(kSenderSsrc);
  tmmbr->AddTmmbr(rtcp::TmmbItem(kNotToUsSsrc, kBitrateBps, 0));

  auto sr = std::make_unique<rtcp::SenderReport>();
  sr->SetSenderSsrc(kSenderSsrc);
  rtcp::CompoundPacket compound;
  compound.Append(std::move(sr));
  compound.Append(std::move(tmmbr));

  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks);
  EXPECT_CALL(mocks.network_link_rtcp_observer, OnReceiverEstimatedMaxBitrate)
      .Times(0);
  receiver.IncomingPacket(compound.Build());

  EXPECT_EQ(0u, receiver.TmmbrReceived().size());
}

TEST(RtcpReceiverTest, TmmbrPacketZeroRateIgnored) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  auto tmmbr = std::make_unique<rtcp::Tmmbr>();
  tmmbr->SetSenderSsrc(kSenderSsrc);
  tmmbr->AddTmmbr(rtcp::TmmbItem(kReceiverMainSsrc, 0, 0));
  auto sr = std::make_unique<rtcp::SenderReport>();
  sr->SetSenderSsrc(kSenderSsrc);
  rtcp::CompoundPacket compound;
  compound.Append(std::move(sr));
  compound.Append(std::move(tmmbr));

  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks);
  EXPECT_CALL(mocks.network_link_rtcp_observer, OnReceiverEstimatedMaxBitrate)
      .Times(0);
  receiver.IncomingPacket(compound.Build());

  EXPECT_EQ(0u, receiver.TmmbrReceived().size());
}

TEST(RtcpReceiverTest, TmmbrThreeConstraintsTimeOut) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  // Inject 3 packets "from" kSenderSsrc, kSenderSsrc+1, kSenderSsrc+2.
  // The times of arrival are starttime + 0, starttime + 5 and starttime + 10.
  for (uint32_t ssrc = kSenderSsrc; ssrc < kSenderSsrc + 3; ++ssrc) {
    auto tmmbr = std::make_unique<rtcp::Tmmbr>();
    tmmbr->SetSenderSsrc(ssrc);
    tmmbr->AddTmmbr(rtcp::TmmbItem(kReceiverMainSsrc, 30000, 0));
    auto sr = std::make_unique<rtcp::SenderReport>();
    sr->SetSenderSsrc(ssrc);
    rtcp::CompoundPacket compound;
    compound.Append(std::move(sr));
    compound.Append(std::move(tmmbr));

    EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks);
    EXPECT_CALL(mocks.rtp_rtcp_impl, SetTmmbn);
    EXPECT_CALL(mocks.network_link_rtcp_observer,
                OnReceiverEstimatedMaxBitrate);
    receiver.IncomingPacket(compound.Build());

    // 5 seconds between each packet.
    mocks.clock.AdvanceTimeMilliseconds(5000);
  }
  // It is now starttime + 15.
  EXPECT_THAT(receiver.TmmbrReceived(),
              AllOf(SizeIs(3),
                    Each(Property(&rtcp::TmmbItem::bitrate_bps, Eq(30'000U)))));

  // We expect the timeout to be 25 seconds. Advance the clock by 12
  // seconds, timing out the first packet.
  mocks.clock.AdvanceTimeMilliseconds(12000);
  EXPECT_THAT(receiver.TmmbrReceived(),
              UnorderedElementsAre(
                  Property(&rtcp::TmmbItem::ssrc, Eq(kSenderSsrc + 1)),
                  Property(&rtcp::TmmbItem::ssrc, Eq(kSenderSsrc + 2))));
}

TEST(RtcpReceiverTest,
     VerifyBlockAndTimestampObtainedFromReportBlockDataObserver) {
  ReceiverMocks mocks;
  MockReportBlockDataObserverImpl observer;
  mocks.config.report_block_data_observer = &observer;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  const uint8_t kFractionLoss = 3;
  const uint32_t kCumulativeLoss = 7;
  const uint32_t kJitter = 9;
  const uint16_t kSequenceNumber = 1234;
  const Timestamp kUtcNow = Clock::NtpToUtc(mocks.clock.CurrentNtpTime());

  rtcp::ReportBlock rtcp_block;
  rtcp_block.SetMediaSsrc(kReceiverMainSsrc);
  rtcp_block.SetExtHighestSeqNum(kSequenceNumber);
  rtcp_block.SetFractionLost(kFractionLoss);
  rtcp_block.SetCumulativeLost(kCumulativeLoss);
  rtcp_block.SetJitter(kJitter);

  rtcp::ReceiverReport rtcp_report;
  rtcp_report.SetSenderSsrc(kSenderSsrc);
  rtcp_report.AddReportBlock(rtcp_block);
  EXPECT_CALL(observer, OnReportBlockDataUpdated)
      .WillOnce([&](ReportBlockData report_block) {
        EXPECT_EQ(rtcp_block.source_ssrc(), report_block.source_ssrc());
        EXPECT_EQ(kSenderSsrc, report_block.sender_ssrc());
        EXPECT_EQ(rtcp_block.fraction_lost(), report_block.fraction_lost_raw());
        EXPECT_EQ(rtcp_block.cumulative_lost(), report_block.cumulative_lost());
        EXPECT_EQ(rtcp_block.extended_high_seq_num(),
                  report_block.extended_highest_sequence_number());
        EXPECT_EQ(rtcp_block.jitter(), report_block.jitter());
        EXPECT_EQ(report_block.report_block_timestamp_utc(), kUtcNow);
        // No RTT is calculated in this test.
        EXPECT_EQ(0u, report_block.num_rtts());
      });
  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks);
  receiver.IncomingPacket(rtcp_report.Build());
}

TEST(RtcpReceiverTest, VerifyRttObtainedFromReportBlockDataObserver) {
  ReceiverMocks mocks;
  MockReportBlockDataObserverImpl observer;
  mocks.config.report_block_data_observer = &observer;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  // To avoid issues with rounding due to different way to represent time units,
  // use RTT that can be precisly represented both with
  // TimeDelta units (i.e. integer number of microseconds), and
  // ntp units (i.e. integer number of 2^(-32) seconds)
  const TimeDelta kRtt = TimeDelta::Millis(125);
  const uint32_t kDelayNtp = 123'000;
  const TimeDelta kDelay = CompactNtpRttToTimeDelta(kDelayNtp);

  uint32_t sent_ntp = CompactNtp(mocks.clock.CurrentNtpTime());
  mocks.clock.AdvanceTime(kRtt + kDelay);

  rtcp::SenderReport sr;
  sr.SetSenderSsrc(kSenderSsrc);
  rtcp::ReportBlock block;
  block.SetMediaSsrc(kReceiverMainSsrc);
  block.SetLastSr(sent_ntp);
  block.SetDelayLastSr(kDelayNtp);
  sr.AddReportBlock(block);
  block.SetMediaSsrc(kReceiverExtraSsrc);
  block.SetLastSr(0);
  sr.AddReportBlock(block);

  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks);
  InSequence sequence;
  EXPECT_CALL(observer, OnReportBlockDataUpdated)
      .WillOnce([&](ReportBlockData report_block_data) {
        EXPECT_EQ(kReceiverMainSsrc, report_block_data.source_ssrc());
        EXPECT_EQ(1u, report_block_data.num_rtts());
        EXPECT_EQ(kRtt, report_block_data.sum_rtts());
        EXPECT_EQ(kRtt, report_block_data.last_rtt());
      });
  EXPECT_CALL(observer, OnReportBlockDataUpdated)
      .WillOnce([](ReportBlockData report_block_data) {
        EXPECT_EQ(kReceiverExtraSsrc, report_block_data.source_ssrc());
        EXPECT_EQ(0u, report_block_data.num_rtts());
      });
  receiver.IncomingPacket(sr.Build());
}

TEST(RtcpReceiverTest, GetReportBlockDataAfterOneReportBlock) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  const uint16_t kSequenceNumber = 1234;

  rtcp::ReportBlock rtcp_block;
  rtcp_block.SetMediaSsrc(kReceiverMainSsrc);
  rtcp_block.SetExtHighestSeqNum(kSequenceNumber);

  rtcp::ReceiverReport rtcp_report;
  rtcp_report.SetSenderSsrc(kSenderSsrc);
  rtcp_report.AddReportBlock(rtcp_block);
  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks);
  receiver.IncomingPacket(rtcp_report.Build());

  auto report_block_datas = receiver.GetLatestReportBlockData();
  ASSERT_THAT(report_block_datas, SizeIs(1));
  EXPECT_EQ(kReceiverMainSsrc, report_block_datas[0].source_ssrc());
  EXPECT_EQ(kSequenceNumber,
            report_block_datas[0].extended_highest_sequence_number());
}

TEST(RtcpReceiverTest, GetReportBlockDataAfterTwoReportBlocksOfSameSsrc) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  const uint16_t kSequenceNumber1 = 1234;
  const uint16_t kSequenceNumber2 = 1235;

  rtcp::ReportBlock rtcp_block1;
  rtcp_block1.SetMediaSsrc(kReceiverMainSsrc);
  rtcp_block1.SetExtHighestSeqNum(kSequenceNumber1);

  rtcp::ReceiverReport rtcp_report1;
  rtcp_report1.SetSenderSsrc(kSenderSsrc);
  rtcp_report1.AddReportBlock(rtcp_block1);
  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks);
  receiver.IncomingPacket(rtcp_report1.Build());

  // Inject a report block with an increased the sequence number for the same
  // source SSRC.
  rtcp::ReportBlock rtcp_block2;
  rtcp_block2.SetMediaSsrc(kReceiverMainSsrc);
  rtcp_block2.SetExtHighestSeqNum(kSequenceNumber2);

  rtcp::ReceiverReport rtcp_report2;
  rtcp_report2.SetSenderSsrc(kSenderSsrc);
  rtcp_report2.AddReportBlock(rtcp_block2);
  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks);
  receiver.IncomingPacket(rtcp_report2.Build());

  // Only the latest block should be returned.
  auto report_block_datas = receiver.GetLatestReportBlockData();
  ASSERT_THAT(report_block_datas, SizeIs(1));
  EXPECT_EQ(kReceiverMainSsrc, report_block_datas[0].source_ssrc());
  EXPECT_EQ(kSequenceNumber2,
            report_block_datas[0].extended_highest_sequence_number());
}

TEST(RtcpReceiverTest, GetReportBlockDataAfterTwoReportBlocksOfDifferentSsrcs) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  const uint16_t kSequenceNumber1 = 1234;
  const uint16_t kSequenceNumber2 = 42;

  rtcp::ReportBlock rtcp_block1;
  rtcp_block1.SetMediaSsrc(kReceiverMainSsrc);
  rtcp_block1.SetExtHighestSeqNum(kSequenceNumber1);

  rtcp::ReceiverReport rtcp_report1;
  rtcp_report1.SetSenderSsrc(kSenderSsrc);
  rtcp_report1.AddReportBlock(rtcp_block1);
  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks);
  receiver.IncomingPacket(rtcp_report1.Build());

  // Inject a report block for a different source SSRC.
  rtcp::ReportBlock rtcp_block2;
  rtcp_block2.SetMediaSsrc(kReceiverExtraSsrc);
  rtcp_block2.SetExtHighestSeqNum(kSequenceNumber2);

  rtcp::ReceiverReport rtcp_report2;
  rtcp_report2.SetSenderSsrc(kSenderSsrc);
  rtcp_report2.AddReportBlock(rtcp_block2);
  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedRtcpReportBlocks);
  receiver.IncomingPacket(rtcp_report2.Build());

  // Both report blocks should be returned.
  auto report_block_datas = receiver.GetLatestReportBlockData();
  ASSERT_THAT(report_block_datas, SizeIs(2));
  EXPECT_EQ(kReceiverMainSsrc, report_block_datas[0].source_ssrc());
  EXPECT_EQ(kSequenceNumber1,
            report_block_datas[0].extended_highest_sequence_number());
  EXPECT_EQ(kReceiverExtraSsrc, report_block_datas[1].source_ssrc());
  EXPECT_EQ(kSequenceNumber2,
            report_block_datas[1].extended_highest_sequence_number());
}

TEST(RtcpReceiverTest, NotifiesNetworkLinkObserverOnTransportFeedback) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  rtcp::TransportFeedback packet;
  packet.SetMediaSsrc(mocks.config.local_media_ssrc);
  packet.SetSenderSsrc(kSenderSsrc);
  packet.SetBase(123, Timestamp::Millis(1));
  packet.AddReceivedPacket(123, Timestamp::Millis(1));

  EXPECT_CALL(
      mocks.network_link_rtcp_observer,
      OnTransportFeedback(
          mocks.clock.CurrentTime(),
          AllOf(Property(&rtcp::TransportFeedback::GetBaseSequence, 123),
                Property(&rtcp::TransportFeedback::GetReceivedPackets,
                         SizeIs(1)))));

  receiver.IncomingPacket(packet.Build());
}

TEST(RtcpReceiverTest, NotifiesNetworkLinkObserverOnCongestionControlFeedback) {
  ReceiverMocks mocks;
  mocks.field_trials = "WebRTC-RFC8888CongestionControlFeedback/Enabled/";
  RTCPReceiver receiver = Create(mocks);

  rtcp::CongestionControlFeedback packet(
      {{
          .ssrc = mocks.config.local_media_ssrc,
          .sequence_number = 1,
      }},
      /*report_timestamp_compact_ntp=*/324);
  packet.SetSenderSsrc(kSenderSsrc);

  EXPECT_CALL(
      mocks.network_link_rtcp_observer,
      OnCongestionControlFeedback(
          mocks.clock.CurrentTime(),
          Property(&rtcp::CongestionControlFeedback::packets, SizeIs(1))));
  receiver.IncomingPacket(packet.Build());
}

TEST(RtcpReceiverTest, FiltersCongestionControlFeedbackOnFirstSsrc) {
  ReceiverMocks mocks_1;
  mocks_1.field_trials = "WebRTC-RFC8888CongestionControlFeedback/Enabled/";
  RTCPReceiver receiver_1 = Create(mocks_1);

  ReceiverMocks mocks_2;
  mocks_2.field_trials = "WebRTC-RFC8888CongestionControlFeedback/Enabled/";
  mocks_2.config.local_media_ssrc = 789;
  mocks_2.config.rtx_send_ssrc = 345;
  RTCPReceiver receiver_2 = Create(mocks_2);

  rtcp::CongestionControlFeedback packet(
      {{
          .ssrc = mocks_2.config.local_media_ssrc,
          .sequence_number = 1,
      }},
      /*report_timestamp_compact_ntp=*/324);
  packet.SetSenderSsrc(kSenderSsrc);

  EXPECT_CALL(mocks_1.network_link_rtcp_observer, OnCongestionControlFeedback)
      .Times(0);
  EXPECT_CALL(mocks_2.network_link_rtcp_observer, OnCongestionControlFeedback)
      .Times(1);
  receiver_1.IncomingPacket(packet.Build());
  receiver_2.IncomingPacket(packet.Build());
}

TEST(RtcpReceiverTest,
     NotifiesNetworkStateEstimateObserverOnRemoteNetworkEstimate) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  NetworkStateEstimate estimate;
  estimate.link_capacity_lower = DataRate::BitsPerSec(1000);
  estimate.link_capacity_upper = DataRate::BitsPerSec(10000);
  rtcp::RemoteEstimate remote_estimate;
  remote_estimate.SetEstimate(estimate);

  EXPECT_CALL(mocks.network_state_estimate_observer,
              OnRemoteNetworkEstimate(
                  AllOf(Field(&NetworkStateEstimate::link_capacity_lower,
                              DataRate::BitsPerSec(1000)),
                        Field(&NetworkStateEstimate::link_capacity_upper,
                              DataRate::BitsPerSec(10000)))));

  receiver.IncomingPacket(remote_estimate.Build());
}

TEST(RtcpReceiverTest,
     NotifiesNetworkStateEstimateObserverBeforeNetworkLinkObserver) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  NetworkStateEstimate estimate;
  estimate.link_capacity_lower = DataRate::BitsPerSec(1000);
  estimate.link_capacity_upper = DataRate::BitsPerSec(10000);
  std::unique_ptr<rtcp::RemoteEstimate> remote_estimate =
      std::make_unique<rtcp::RemoteEstimate>();
  remote_estimate->SetEstimate(estimate);
  std::unique_ptr<rtcp::TransportFeedback> feedback_packet =
      std::make_unique<rtcp::TransportFeedback>();
  feedback_packet->SetMediaSsrc(mocks.config.local_media_ssrc);
  feedback_packet->SetSenderSsrc(kSenderSsrc);
  feedback_packet->SetBase(123, Timestamp::Millis(1));
  feedback_packet->AddReceivedPacket(123, Timestamp::Millis(1));
  rtcp::CompoundPacket compound;
  compound.Append(std::move(remote_estimate));
  compound.Append(std::move(feedback_packet));

  InSequence s;
  EXPECT_CALL(mocks.network_state_estimate_observer, OnRemoteNetworkEstimate);
  EXPECT_CALL(mocks.network_link_rtcp_observer, OnTransportFeedback);
  receiver.IncomingPacket(compound.Build());
}

TEST(RtcpReceiverTest, HandlesInvalidCongestionControlFeedback) {
  ReceiverMocks mocks;
  mocks.field_trials = "WebRTC-RFC8888CongestionControlFeedback/Enabled/";
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  rtcp::CongestionControlFeedback packet({{
                                             .ssrc = 123,
                                             .sequence_number = 1,
                                         }},
                                         /*report_timestamp_compact_ntp=*/324);
  packet.SetSenderSsrc(kSenderSsrc);
  Buffer built_packet = packet.Build();
  // Modify the CongestionControlFeedback packet so that it is invalid.
  const size_t kNumReportsOffset = 14;
  ByteWriter<uint16_t>::WriteBigEndian(&built_packet.data()[kNumReportsOffset],
                                       42);

  EXPECT_CALL(mocks.network_link_rtcp_observer, OnCongestionControlFeedback)
      .Times(0);
  receiver.IncomingPacket(built_packet);
}

TEST(RtcpReceiverTest,
     NotifiesNetworkLinkObserverOnTransportFeedbackOnRtxSsrc) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  rtcp::TransportFeedback packet;
  packet.SetMediaSsrc(*mocks.config.rtx_send_ssrc);
  packet.SetSenderSsrc(kSenderSsrc);
  packet.SetBase(1, Timestamp::Millis(1));
  packet.AddReceivedPacket(1, Timestamp::Millis(1));

  EXPECT_CALL(mocks.network_link_rtcp_observer, OnTransportFeedback);
  receiver.IncomingPacket(packet.Build());
}

TEST(RtcpReceiverTest,
     DoesNotNotifyNetworkLinkObserverOnTransportFeedbackForUnregistedSsrc) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  rtcp::TransportFeedback packet;
  packet.SetMediaSsrc(kNotToUsSsrc);
  packet.SetSenderSsrc(kSenderSsrc);
  packet.SetBase(1, Timestamp::Millis(1));
  packet.AddReceivedPacket(1, Timestamp::Millis(1));

  EXPECT_CALL(mocks.network_link_rtcp_observer, OnTransportFeedback).Times(0);
  receiver.IncomingPacket(packet.Build());
}

TEST(RtcpReceiverTest, NotifiesNetworkLinkObserverOnRemb) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  rtcp::Remb remb;
  remb.SetSenderSsrc(kSenderSsrc);
  remb.SetBitrateBps(500'000);

  EXPECT_CALL(mocks.network_link_rtcp_observer,
              OnReceiverEstimatedMaxBitrate(mocks.clock.CurrentTime(),
                                            DataRate::BitsPerSec(500'000)));
  receiver.IncomingPacket(remb.Build());
}

TEST(RtcpReceiverTest, HandlesInvalidTransportFeedback) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  // Send a compound packet with a TransportFeedback followed by something else.
  auto packet = std::make_unique<rtcp::TransportFeedback>();
  packet->SetMediaSsrc(kReceiverMainSsrc);
  packet->SetSenderSsrc(kSenderSsrc);
  packet->SetBase(1, Timestamp::Millis(1));
  packet->AddReceivedPacket(1, Timestamp::Millis(1));

  static constexpr DataRate kBitrate = DataRate::BitsPerSec(50'000);
  auto remb = std::make_unique<rtcp::Remb>();
  remb->SetSenderSsrc(kSenderSsrc);
  remb->SetBitrateBps(kBitrate.bps());
  rtcp::CompoundPacket compound;
  compound.Append(std::move(packet));
  compound.Append(std::move(remb));
  Buffer built_packet = compound.Build();

  // Modify the TransportFeedback packet so that it is invalid.
  const size_t kStatusCountOffset = 14;
  ByteWriter<uint16_t>::WriteBigEndian(&built_packet.data()[kStatusCountOffset],
                                       42);

  // Stress no transport feedback is expected.
  EXPECT_CALL(mocks.network_link_rtcp_observer, OnTransportFeedback).Times(0);
  // But remb should be processed and cause a callback
  EXPECT_CALL(mocks.network_link_rtcp_observer,
              OnReceiverEstimatedMaxBitrate(_, kBitrate));
  receiver.IncomingPacket(built_packet);
}

TEST(RtcpReceiverTest, Nack) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  const uint16_t kNackList1[] = {1, 2, 3, 5};
  const uint16_t kNackList23[] = {5, 7, 30, 40, 41, 58, 59, 61, 63};
  const size_t kNackListLength2 = 4;
  const size_t kNackListLength3 = std::size(kNackList23) - kNackListLength2;
  std::set<uint16_t> nack_set;
  nack_set.insert(std::begin(kNackList1), std::end(kNackList1));
  nack_set.insert(std::begin(kNackList23), std::end(kNackList23));

  auto nack1 = std::make_unique<rtcp::Nack>();
  nack1->SetSenderSsrc(kSenderSsrc);
  nack1->SetMediaSsrc(kReceiverMainSsrc);
  nack1->SetPacketIds(kNackList1, std::size(kNackList1));

  EXPECT_CALL(mocks.rtp_rtcp_impl,
              OnReceivedNack(ElementsAreArray(kNackList1)));
  EXPECT_CALL(mocks.packet_type_counter_observer,
              RtcpPacketTypesCounterUpdated(
                  kReceiverMainSsrc,
                  AllOf(Field(&RtcpPacketTypeCounter::nack_requests,
                              std::size(kNackList1)),
                        Field(&RtcpPacketTypeCounter::unique_nack_requests,
                              std::size(kNackList1)))));
  receiver.IncomingPacket(nack1->Build());

  auto nack2 = std::make_unique<rtcp::Nack>();
  nack2->SetSenderSsrc(kSenderSsrc);
  nack2->SetMediaSsrc(kReceiverMainSsrc);
  nack2->SetPacketIds(kNackList23, kNackListLength2);

  auto nack3 = std::make_unique<rtcp::Nack>();
  nack3->SetSenderSsrc(kSenderSsrc);
  nack3->SetMediaSsrc(kReceiverMainSsrc);
  nack3->SetPacketIds(kNackList23 + kNackListLength2, kNackListLength3);

  rtcp::CompoundPacket two_nacks;
  two_nacks.Append(std::move(nack2));
  two_nacks.Append(std::move(nack3));

  EXPECT_CALL(mocks.rtp_rtcp_impl,
              OnReceivedNack(ElementsAreArray(kNackList23)));
  EXPECT_CALL(mocks.packet_type_counter_observer,
              RtcpPacketTypesCounterUpdated(
                  kReceiverMainSsrc,
                  AllOf(Field(&RtcpPacketTypeCounter::nack_requests,
                              std::size(kNackList1) + std::size(kNackList23)),
                        Field(&RtcpPacketTypeCounter::unique_nack_requests,
                              nack_set.size()))));
  receiver.IncomingPacket(two_nacks.Build());
}

TEST(RtcpReceiverTest, NackNotForUsIgnored) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  const uint16_t kNackList1[] = {1, 2, 3, 5};
  const size_t kNackListLength1 = std::end(kNackList1) - std::begin(kNackList1);

  rtcp::Nack nack;
  nack.SetSenderSsrc(kSenderSsrc);
  nack.SetMediaSsrc(kNotToUsSsrc);
  nack.SetPacketIds(kNackList1, kNackListLength1);

  EXPECT_CALL(mocks.packet_type_counter_observer,
              RtcpPacketTypesCounterUpdated(
                  _, Field(&RtcpPacketTypeCounter::nack_requests, 0)));
  receiver.IncomingPacket(nack.Build());
}

TEST(RtcpReceiverTest, ForceSenderReport) {
  ReceiverMocks mocks;
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  rtcp::RapidResyncRequest rr;
  rr.SetSenderSsrc(kSenderSsrc);
  rr.SetMediaSsrc(kReceiverMainSsrc);

  EXPECT_CALL(mocks.rtp_rtcp_impl, OnRequestSendReport());
  receiver.IncomingPacket(rr.Build());
}

TEST(RtcpReceiverTest, ChangeLocalMediaSsrc) {
  ReceiverMocks mocks;
  // Construct a receiver with `kReceiverMainSsrc` (default) local media ssrc.
  RTCPReceiver receiver = Create(mocks);
  receiver.SetRemoteSSRC(kSenderSsrc);

  constexpr uint32_t kSecondarySsrc = kReceiverMainSsrc + 1;

  // Expect to only get the `OnReceivedNack()` callback once since we'll
  // configure it for the `kReceiverMainSsrc` media ssrc.
  EXPECT_CALL(mocks.rtp_rtcp_impl, OnReceivedNack);

  // We'll get two callbacks to RtcpPacketTypesCounterUpdated, one for each
  // call to `IncomingPacket`, differentiated by the local media ssrc.
  EXPECT_CALL(mocks.packet_type_counter_observer,
              RtcpPacketTypesCounterUpdated(kReceiverMainSsrc, _));
  EXPECT_CALL(mocks.packet_type_counter_observer,
              RtcpPacketTypesCounterUpdated(kSecondarySsrc, _));

  // Construct a test nack packet with media ssrc set to `kReceiverMainSsrc`.
  rtcp::Nack nack;
  nack.SetSenderSsrc(kSenderSsrc);
  nack.SetMediaSsrc(kReceiverMainSsrc);
  const uint16_t kNackList[] = {1, 2, 3, 5};
  nack.SetPacketIds(kNackList, std::size(kNackList));

  // Deliver the first callback.
  receiver.IncomingPacket(nack.Build());

  // Change the set local media ssrc.
  receiver.set_local_media_ssrc(kSecondarySsrc);

  // Deliver another packet - this time there will be no callback to
  // OnReceivedNack due to the ssrc not matching.
  receiver.IncomingPacket(nack.Build());
}

}  // namespace webrtc
