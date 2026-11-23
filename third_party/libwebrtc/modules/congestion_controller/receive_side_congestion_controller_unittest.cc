/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/congestion_controller/include/receive_side_congestion_controller.h"

#include <cstdint>
#include <memory>
#include <vector>

#include "api/field_trials.h"
#include "api/media_types.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_parameters.h"
#include "api/test/network_emulation/create_cross_traffic.h"
#include "api/test/network_emulation/cross_traffic.h"
#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "modules/remote_bitrate_estimator/include/bwe_defines.h"
#include "modules/rtp_rtcp/include/rtp_header_extension_map.h"
#include "modules/rtp_rtcp/source/rtcp_packet.h"
#include "modules/rtp_rtcp/source/rtcp_packet/common_header.h"
#include "modules/rtp_rtcp/source/rtcp_packet/congestion_control_feedback.h"
#include "modules/rtp_rtcp/source/rtcp_packet/transport_feedback.h"
#include "modules/rtp_rtcp/source/rtp_header_extensions.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "rtc_base/buffer.h"
#include "system_wrappers/include/clock.h"
#include "system_wrappers/include/metrics.h"
#include "test/create_test_environment.h"
#include "test/create_test_field_trials.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/scenario/scenario.h"
#include "test/scenario/scenario_config.h"

namespace webrtc {
namespace test {
namespace {

using ::testing::_;
using ::testing::AtLeast;
using ::testing::ElementsAre;
using ::testing::MockFunction;
using ::testing::SizeIs;

constexpr DataRate kInitialBitrate = DataRate::BitsPerSec(60'000);

TEST(ReceiveSideCongestionControllerTest, SendsRembWithAbsSendTime) {
  static constexpr DataSize kPayloadSize = DataSize::Bytes(1000);
  MockFunction<void(std::vector<std::unique_ptr<rtcp::RtcpPacket>>)>
      feedback_sender;
  MockFunction<void(uint64_t, std::vector<uint32_t>)> remb_sender;
  SimulatedClock clock(123456);

  ReceiveSideCongestionController controller(
      CreateTestEnvironment({.time = &clock}), feedback_sender.AsStdFunction(),
      remb_sender.AsStdFunction());

  RtpHeaderExtensionMap extensions;
  extensions.Register<AbsoluteSendTime>(RtpHeaderExtensionId(1));
  RtpPacketReceived packet(&extensions);
  packet.SetSsrc(0x11eb21c);
  packet.ReserveExtension<AbsoluteSendTime>();
  packet.SetPayloadSize(kPayloadSize.bytes());

  EXPECT_CALL(remb_sender, Call(_, ElementsAre(packet.Ssrc())))
      .Times(AtLeast(1));

  for (int i = 0; i < 10; ++i) {
    clock.AdvanceTime(kPayloadSize / kInitialBitrate);
    Timestamp now = clock.CurrentTime();
    packet.SetExtension<AbsoluteSendTime>(AbsoluteSendTime::To24Bits(now));
    packet.set_arrival_time(now);
    controller.OnReceivedPacket(packet, MediaType::VIDEO);
  }
}

TEST(ReceiveSideCongestionControllerTest,
     SendsRembAfterSetMaxDesiredReceiveBitrate) {
  MockFunction<void(std::vector<std::unique_ptr<rtcp::RtcpPacket>>)>
      feedback_sender;
  MockFunction<void(uint64_t, std::vector<uint32_t>)> remb_sender;
  SimulatedClock clock(123456);

  ReceiveSideCongestionController controller(
      CreateTestEnvironment({.time = &clock}), feedback_sender.AsStdFunction(),
      remb_sender.AsStdFunction());
  EXPECT_CALL(remb_sender, Call(123, _));
  controller.SetMaxDesiredReceiveBitrate(DataRate::BitsPerSec(123));
}

void CheckRfc8888Feedback(
    const std::vector<std::unique_ptr<rtcp::RtcpPacket>>& rtcp_packets) {
  ASSERT_THAT(rtcp_packets, SizeIs(1));
  Buffer buffer = rtcp_packets[0]->Build();
  rtcp::CommonHeader header;
  EXPECT_TRUE(header.Parse(buffer.data(), buffer.size()));
  // Check for RFC 8888 format message type 11(CCFB)
  EXPECT_EQ(header.fmt(),
            rtcp::CongestionControlFeedback::kFeedbackMessageType);
}

void CheckTwCCFeedback(
    const std::vector<std::unique_ptr<rtcp::RtcpPacket>>& rtcp_packets) {
  ASSERT_THAT(rtcp_packets, SizeIs(1));
  Buffer buffer = rtcp_packets[0]->Build();
  rtcp::CommonHeader header;
  EXPECT_TRUE(header.Parse(buffer.data(), buffer.size()));
  EXPECT_EQ(header.fmt(), rtcp::TransportFeedback::kFeedbackMessageType);
}

TEST(ReceiveSideCongestionControllerTest, SendsRfc8888FeedbackIfForced) {
  FieldTrials field_trials = CreateTestFieldTrials(
      "WebRTC-RFC8888CongestionControlFeedback/force_send:true/");
  MockFunction<void(std::vector<std::unique_ptr<rtcp::RtcpPacket>>)>
      rtcp_sender;
  MockFunction<void(uint64_t, std::vector<uint32_t>)> remb_sender;
  SimulatedClock clock(123456);
  ReceiveSideCongestionController controller(
      CreateTestEnvironment({.field_trials = &field_trials, .time = &clock}),
      rtcp_sender.AsStdFunction(), remb_sender.AsStdFunction());

  // Expect that RTCP feedback is sent.
  EXPECT_CALL(rtcp_sender, Call)
      .WillOnce(
          [&](std::vector<std::unique_ptr<rtcp::RtcpPacket>> rtcp_packets) {
            CheckRfc8888Feedback(rtcp_packets);
          });
  // Expect that REMB is not sent.
  EXPECT_CALL(remb_sender, Call).Times(0);

  RtpPacketReceived packet;
  packet.set_arrival_time(clock.CurrentTime());
  controller.OnReceivedPacket(packet, MediaType::VIDEO);
  TimeDelta next_process = controller.MaybeProcess();
  clock.AdvanceTime(next_process);
  next_process = controller.MaybeProcess();
}

TEST(ReceiveSideCongestionControllerTest, SendsRfc8888FeedbackIfEnabled) {
  MockFunction<void(std::vector<std::unique_ptr<rtcp::RtcpPacket>>)>
      rtcp_sender;
  MockFunction<void(uint64_t, std::vector<uint32_t>)> remb_sender;
  SimulatedClock clock(123456);
  ReceiveSideCongestionController controller(
      CreateTestEnvironment({.time = &clock}), rtcp_sender.AsStdFunction(),
      remb_sender.AsStdFunction());
  controller.SetPreferredRtcpCcAckType(RtcpFeedbackType::CCFB);

  // Expect that RTCP feedback is sent.
  EXPECT_CALL(rtcp_sender, Call)
      .WillOnce(
          [&](std::vector<std::unique_ptr<rtcp::RtcpPacket>> rtcp_packets) {
            CheckRfc8888Feedback(rtcp_packets);
          });
  // Expect that REMB is not sent.
  EXPECT_CALL(remb_sender, Call).Times(0);

  RtpPacketReceived packet;
  packet.set_arrival_time(clock.CurrentTime());
  controller.OnReceivedPacket(packet, MediaType::VIDEO);
  TimeDelta next_process = controller.MaybeProcess();
  clock.AdvanceTime(next_process);
  next_process = controller.MaybeProcess();
}

TEST(ReceiveSideCongestionControllerTest,
     SendsTwccFeedbackIfEnabledAndHaveExtension) {
  MockFunction<void(std::vector<std::unique_ptr<rtcp::RtcpPacket>>)>
      rtcp_sender;
  MockFunction<void(uint64_t, std::vector<uint32_t>)> remb_sender;
  SimulatedClock clock(123456);
  ReceiveSideCongestionController controller(
      CreateTestEnvironment({.time = &clock}), rtcp_sender.AsStdFunction(),
      remb_sender.AsStdFunction());
  controller.SetPreferredRtcpCcAckType(RtcpFeedbackType::TRANSPORT_CC);

  // Expect that RTCP feedback is sent.
  EXPECT_CALL(rtcp_sender, Call)
      .WillOnce(
          [&](std::vector<std::unique_ptr<rtcp::RtcpPacket>> rtcp_packets) {
            CheckTwCCFeedback(rtcp_packets);
          });
  // Expect that REMB is not sent.
  EXPECT_CALL(remb_sender, Call).Times(0);

  RtpHeaderExtensionMap extensions;
  extensions.Register<TransportSequenceNumber>(RtpHeaderExtensionId(1));
  RtpPacketReceived packet(&extensions);
  packet.set_arrival_time(clock.CurrentTime());
  packet.SetExtension<TransportSequenceNumber>(123);
  controller.OnReceivedPacket(packet, MediaType::VIDEO);
  TimeDelta next_process = controller.MaybeProcess();
  clock.AdvanceTime(next_process);
  next_process = controller.MaybeProcess();
}

TEST(ReceiveSideCongestionControllerTest,
     SendsNoFeedbackIfNotRfcRfc8888EnabledAndNoTransportFeedback) {
  MockFunction<void(std::vector<std::unique_ptr<rtcp::RtcpPacket>>)>
      rtcp_sender;
  MockFunction<void(uint64_t, std::vector<uint32_t>)> remb_sender;
  SimulatedClock clock(123456);
  ReceiveSideCongestionController controller(
      CreateTestEnvironment({.time = &clock}), rtcp_sender.AsStdFunction(),
      remb_sender.AsStdFunction());

  // No Transport feedback is sent because received packet does not have
  // transport sequence number rtp header extension.
  EXPECT_CALL(rtcp_sender, Call).Times(0);
  RtpPacketReceived packet;
  packet.set_arrival_time(clock.CurrentTime());
  controller.OnReceivedPacket(packet, MediaType::VIDEO);
  TimeDelta next_process = controller.MaybeProcess();
  clock.AdvanceTime(next_process);
  next_process = controller.MaybeProcess();
}

TEST(ReceiveSideCongestionControllerTest, ConvergesToCapacity) {
  Scenario s("receive_cc_unit/converge");
  NetworkSimulationConfig net_conf;
  net_conf.bandwidth = DataRate::KilobitsPerSec(1000);
  net_conf.delay = TimeDelta::Millis(50);
  auto* client = s.CreateClient("send", [&](CallClientConfig* c) {
    c->transport.rates.start_rate = DataRate::KilobitsPerSec(300);
  });

  auto* route = s.CreateRoutes(client, {s.CreateSimulationNode(net_conf)},
                               s.CreateClient("return", CallClientConfig()),
                               {s.CreateSimulationNode(net_conf)});
  VideoStreamConfig video;
  video.stream.packet_feedback = false;
  s.CreateVideoStream(route->forward(), video);
  s.RunFor(TimeDelta::Seconds(30));
  EXPECT_NEAR(client->send_bandwidth().kbps(), 900, 150);
}

TEST(ReceiveSideCongestionControllerTest, IsFairToTCP) {
  Scenario s("receive_cc_unit/tcp_fairness");
  NetworkSimulationConfig net_conf;
  net_conf.bandwidth = DataRate::KilobitsPerSec(1000);
  net_conf.delay = TimeDelta::Millis(50);
  auto* client = s.CreateClient("send", [&](CallClientConfig* c) {
    c->transport.rates.start_rate = DataRate::KilobitsPerSec(1000);
  });
  auto send_net = {s.CreateSimulationNode(net_conf)};
  auto ret_net = {s.CreateSimulationNode(net_conf)};
  auto* route = s.CreateRoutes(
      client, send_net, s.CreateClient("return", CallClientConfig()), ret_net);
  VideoStreamConfig video;
  video.stream.packet_feedback = false;
  s.CreateVideoStream(route->forward(), video);
  s.net()->StartCrossTraffic(CreateFakeTcpCrossTraffic(
      s.net()->CreateRoute(send_net), s.net()->CreateRoute(ret_net),
      FakeTcpConfig()));
  s.RunFor(TimeDelta::Seconds(30));
  // For some reason we get outcompeted by TCP here, this should probably be
  // fixed and a lower bound should be added to the test.
  EXPECT_LT(client->send_bandwidth().kbps(), 750);
}

TEST(ReceiveSideCongestionControllerTest, OnBitrateChangedDeprecatedAndNew) {
  MockFunction<void(std::vector<std::unique_ptr<rtcp::RtcpPacket>>)>
      feedback_sender;
  MockFunction<void(uint64_t, std::vector<uint32_t>)> remb_sender;
  SimulatedClock clock(123456);

  ReceiveSideCongestionController controller(
      CreateTestEnvironment({.time = &clock}), feedback_sender.AsStdFunction(),
      remb_sender.AsStdFunction());

  // Test new overload.
  controller.OnBitrateChanged(DataRate::BitsPerSec(500'000),
                              /*is_bandwidth_limited=*/true,
                              DataSize::Bytes(42));

  // Test deprecated overload.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  controller.OnBitrateChanged(500'000);
#pragma clang diagnostic pop
}

void SendVideoPacket(ReceiveSideCongestionController& controller,
                     SimulatedClock& clock,
                     const RtpHeaderExtensionMap& extensions,
                     uint32_t ssrc,
                     bool with_abs_send_time) {
  static constexpr DataSize kPayloadSize = DataSize::Bytes(1000);
  clock.AdvanceTime(kPayloadSize / kInitialBitrate);
  Timestamp now = clock.CurrentTime();
  RtpPacketReceived packet(&extensions);
  packet.SetSsrc(ssrc);
  packet.SetTimestamp(static_cast<uint32_t>(now.ms() * 90));
  packet.SetExtension<TransmissionOffset>(0);
  if (with_abs_send_time) {
    packet.SetExtension<AbsoluteSendTime>(AbsoluteSendTime::To24Bits(now));
  }
  packet.SetPayloadSize(kPayloadSize.bytes());
  packet.set_arrival_time(now);
  controller.OnReceivedPacket(packet, MediaType::VIDEO);
}

TEST(ReceiveSideCongestionControllerTest,
     SwitchesBetweenTofAndAbsSendTimeEstimators) {
  metrics::Reset();
  MockFunction<void(std::vector<std::unique_ptr<rtcp::RtcpPacket>>)>
      feedback_sender;
  MockFunction<void(uint64_t, std::vector<uint32_t>)> remb_sender;
  SimulatedClock clock(123456);
  ReceiveSideCongestionController controller(
      CreateTestEnvironment({.time = &clock}), feedback_sender.AsStdFunction(),
      remb_sender.AsStdFunction());

  RtpHeaderExtensionMap extensions;
  extensions.Register<TransmissionOffset>(RtpHeaderExtensionId(1));
  extensions.Register<AbsoluteSendTime>(RtpHeaderExtensionId(2));

  // The SSRC values are arbitrary, but each phase uses its own SSRC so that
  // the SSRC list of a REMB identifies the estimator that sent it. A switch
  // replaces the estimator with a new instance, which only reports the SSRCs
  // it has received packets for. Hence ElementsAre() rather than Contains().
  constexpr uint32_t kInitialTofSsrc = 0x1111;
  constexpr uint32_t kAbsSendTimeSsrc = 0x2222;
  constexpr uint32_t kSecondTofSsrc = 0x3333;

  // Default estimator is transmission time offset (SingleStream).
  EXPECT_CALL(remb_sender, Call(_, ElementsAre(kInitialTofSsrc)))
      .Times(AtLeast(1));
  for (int i = 0; i < 55; ++i) {
    SendVideoPacket(controller, clock, extensions, kInitialTofSsrc,
                    /*with_abs_send_time=*/false);
    controller.MaybeProcess();
  }
  EXPECT_EQ(metrics::NumEvents(kBweTypeHistogram, BweNames::kReceiverTOffset),
            1);
  EXPECT_EQ(
      metrics::NumEvents(kBweTypeHistogram, BweNames::kReceiverAbsSendTime), 0);

  // First packet with AbsoluteSendTime immediately switches to AST RBE.
  EXPECT_CALL(remb_sender, Call(_, ElementsAre(kAbsSendTimeSsrc)))
      .Times(AtLeast(1));
  for (int i = 0; i < 10; ++i) {
    SendVideoPacket(controller, clock, extensions, kAbsSendTimeSsrc,
                    /*with_abs_send_time=*/true);
  }
  EXPECT_EQ(
      metrics::NumEvents(kBweTypeHistogram, BweNames::kReceiverAbsSendTime), 1);

  // Below kTimeOffsetSwitchThreshold (30) non-AST packets, stays on AST RBE.
  for (int i = 0; i < 29; ++i) {
    SendVideoPacket(controller, clock, extensions, kSecondTofSsrc,
                    /*with_abs_send_time=*/false);
  }
  EXPECT_EQ(metrics::NumEvents(kBweTypeHistogram, BweNames::kReceiverTOffset),
            1);

  // The 30th packet without AST switches back to a new TOF estimator.
  EXPECT_CALL(remb_sender, Call(_, ElementsAre(kSecondTofSsrc)))
      .Times(AtLeast(1));
  for (int i = 0; i < 55; ++i) {
    SendVideoPacket(controller, clock, extensions, kSecondTofSsrc,
                    /*with_abs_send_time=*/false);
    controller.MaybeProcess();
  }
  EXPECT_EQ(metrics::NumEvents(kBweTypeHistogram, BweNames::kReceiverTOffset),
            2);
}
}  // namespace
}  // namespace test
}  // namespace webrtc
