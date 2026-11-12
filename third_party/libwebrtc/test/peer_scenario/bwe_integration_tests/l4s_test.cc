/*
 *  Copyright (c) 2024 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <atomic>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "api/audio_options.h"
#include "api/jsep.h"
#include "api/media_types.h"
#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "api/rtp_parameters.h"
#include "api/rtp_transceiver_direction.h"
#include "api/rtp_transceiver_interface.h"
#include "api/scoped_refptr.h"
#include "api/stats/rtc_stats_report.h"
#include "api/test/network_emulation/network_config_schedule.pb.h"
#include "api/test/network_emulation/network_emulation_interfaces.h"
#include "api/test/network_emulation/schedulable_network_node_builder.h"
#include "api/test/network_emulation_manager.h"
#include "api/transport/ecn_marking.h"
#include "api/transport/stun.h"
#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtcp_packet/congestion_control_feedback.h"
#include "modules/rtp_rtcp/source/rtcp_packet/rtpfb.h"
#include "modules/rtp_rtcp/source/rtcp_packet/transport_feedback.h"
#include "modules/rtp_rtcp/source/rtp_util.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/network_constants.h"
#include "test/create_frame_generator_capturer.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/network/network_emulation.h"
#include "test/peer_scenario/bwe_integration_tests/stats_utilities.h"
#include "test/peer_scenario/peer_scenario.h"
#include "test/peer_scenario/peer_scenario_client.h"
#include "test/peer_scenario/signaling_route.h"

namespace webrtc {
namespace {

using test::GetAvailableSendBitrate;
using test::GetAverageRoundTripTime;
using test::GetPacketsLost;
using test::GetPacketsReceived;
using test::GetPacketsReceivedWithCe;
using test::GetPacketsReceivedWithEct1;
using test::GetPacketsSent;
using test::GetPacketsSentWithEct1;
using test::GetStatsAndProcess;
using test::PeerScenario;
using test::PeerScenarioClient;
using ::testing::AllOf;
using ::testing::ContainsRegex;
using ::testing::Ge;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::Lt;
using ::testing::TestWithParam;

// RTC event logs can be gathered from these tests.
// Add --peer_logs=true --peer_logs_root=/tmp/l4s/ to write logs to /tmp/l4s

// This regexp matches both wildcard and non-wildcard ccfb lines.
constexpr std::string_view ccfb_regex = "a=rtcp-fb:[0-9*]* ack ccfb\r\n";

// Helper class used for counting RTCP feedback messages.
class RtcpFeedbackCounter {
 public:
  void Count(const EmulatedIpPacket& packet) {
    if (!IsRtcpPacket(packet.data)) {
      return;
    }
    rtcp::CommonHeader header;
    ASSERT_TRUE(header.Parse(packet.data.cdata(), packet.data.size()));
    if (header.type() != rtcp::Rtpfb::kPacketType) {
      return;
    }
    if (header.fmt() == rtcp::CongestionControlFeedback::kFeedbackMessageType) {
      ++congestion_control_feedback_;
      rtcp::CongestionControlFeedback fb;
      ASSERT_TRUE(fb.Parse(header));
      for (const rtcp::CongestionControlFeedback::PacketInfo& info :
           fb.packets()) {
        switch (info.ecn) {
          case EcnMarking::kNotEct:
            ++not_ect_;
            break;
          case EcnMarking::kEct0:
            // Not used.
            RTC_CHECK_NOTREACHED();
            break;
          case EcnMarking::kEct1:
            // ECN-Capable Transport
            ++ect1_;
            break;
          case EcnMarking::kCe:
            ++ce_;
        }
      }
    }
    if (header.fmt() == rtcp::TransportFeedback::kFeedbackMessageType) {
      ++transport_sequence_number_feedback_;
    }
  }

  int FeedbackAccordingToRfc8888() const {
    return congestion_control_feedback_;
  }
  int FeedbackAccordingToTransportCc() const {
    return transport_sequence_number_feedback_;
  }
  int not_ect() const { return not_ect_; }
  int ect1() const { return ect1_; }
  int ce() const { return ce_; }

 private:
  int congestion_control_feedback_ = 0;
  int transport_sequence_number_feedback_ = 0;
  int not_ect_ = 0;
  int ect1_ = 0;
  int ce_ = 0;
};

// Creates a network node that changes its one way delay from
// `initial_one_way_delay` to `final_one_way_delay` `change_after` after the
// first packet is sent on it.
EmulatedNetworkNode* CreateNodeWithDelayChange(PeerScenario& s,
                                               TimeDelta initial_one_way_delay,
                                               TimeDelta final_one_way_delay,
                                               TimeDelta change_after) {
  // A scheduled network node requires a finite link capacity.
  constexpr DataRate kLinkCapacity = DataRate::KilobitsPerSec(3000);
  network_behaviour::NetworkConfigSchedule schedule;
  network_behaviour::NetworkConfigScheduleItem* item = schedule.add_item();
  item->set_link_capacity_kbps(kLinkCapacity.kbps());
  item->set_queue_delay_ms(initial_one_way_delay.ms());
  item = schedule.add_item();
  item->set_time_since_first_sent_packet_ms(change_after.ms());
  item->set_link_capacity_kbps(kLinkCapacity.kbps());
  item->set_queue_delay_ms(final_one_way_delay.ms());
  return SchedulableNetworkNodeBuilder(*s.net(), std::move(schedule)).Build();
}

TEST(L4STest, NegotiateAndUseCcfbIfEnabled) {
  PeerScenario s(*test_info_);

  PeerScenarioClient::Config config;
  config.field_trials.Set("WebRTC-RFC8888CongestionControlFeedback",
                          "Enabled,offer:true");
  config.disable_encryption = true;
  PeerScenarioClient* caller = s.CreateClient(config);
  PeerScenarioClient* callee = s.CreateClient(config);

  // Create network path from caller to callee.
  auto send_node = s.net()->NodeBuilder().Build().node;
  auto ret_node = s.net()->NodeBuilder().Build().node;
  s.net()->CreateRoute(caller->endpoint(), {send_node}, callee->endpoint());
  s.net()->CreateRoute(callee->endpoint(), {ret_node}, caller->endpoint());

  RtcpFeedbackCounter send_node_feedback_counter;
  send_node->router()->SetWatcher([&](const EmulatedIpPacket& packet) {
    send_node_feedback_counter.Count(packet);
  });
  RtcpFeedbackCounter ret_node_feedback_counter;
  ret_node->router()->SetWatcher([&](const EmulatedIpPacket& packet) {
    ret_node_feedback_counter.Count(packet);
  });

  auto signaling = s.ConnectSignaling(caller, callee, {send_node}, {ret_node});
  PeerScenarioClient::VideoSendTrackConfig video_conf;
  video_conf.generator.squares_video->framerate = 15;

  caller->CreateAudio("AUDIO_1", AudioOptions());
  caller->CreateVideo("VIDEO_1", video_conf);
  callee->CreateAudio("AUDIO_2", AudioOptions());
  callee->CreateVideo("VIDEO_2", video_conf);

  signaling.StartIceSignaling();

  std::atomic<bool> offer_exchange_done(false);
  signaling.NegotiateSdp(
      [&](SessionDescriptionInterface* offer) {
        std::string offer_str = absl::StrCat(*offer);
        // Check that the offer contain both congestion control feedback
        // according to RFC 8888, and transport-cc and the header extension
        // http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01
        EXPECT_THAT(offer_str, ContainsRegex(ccfb_regex));
        EXPECT_THAT(offer_str, HasSubstr("transport-cc"));
        EXPECT_THAT(
            offer_str,
            HasSubstr("http://www.ietf.org/id/"
                      "draft-holmer-rmcat-transport-wide-cc-extensions"));
      },
      [&](const SessionDescriptionInterface& answer) {
        std::string answer_str = absl::StrCat(answer);
        EXPECT_THAT(answer_str, ContainsRegex(ccfb_regex));
        // Check that the answer does not contain transport-cc nor the
        // header extension
        // http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01
        EXPECT_THAT(answer_str, Not(HasSubstr("transport-cc")));
        EXPECT_THAT(
            answer_str,
            Not(HasSubstr(" http://www.ietf.org/id/"
                          "draft-holmer-rmcat-transport-wide-cc-extensions-")));
        offer_exchange_done = true;
      });
  // Wait for SDP negotiation and the packet filter to be setup.
  s.WaitAndProcess(&offer_exchange_done);

  s.ProcessMessages(TimeDelta::Seconds(2));
  EXPECT_GT(send_node_feedback_counter.FeedbackAccordingToRfc8888(), 0);
  EXPECT_EQ(send_node_feedback_counter.FeedbackAccordingToTransportCc(), 0);

  EXPECT_GT(ret_node_feedback_counter.FeedbackAccordingToRfc8888(), 0);
  EXPECT_EQ(ret_node_feedback_counter.FeedbackAccordingToTransportCc(), 0);
}

TEST(L4STest, NoCcfbSentAfterRenegotiationAndCallerCachesLocalDescription) {
  // The caller supports CCFB, but the callee does not.
  // This test that the caller does not start sending CCFB after renegotiation
  // even if the local description is cached. The caller's local description
  // will contain CCFB since it was used in the initial offer.
  PeerScenario s(*test_info_);
  PeerScenarioClient::Config caller_config;
  caller_config.disable_encryption = true;
  caller_config.field_trials.Set("WebRTC-RFC8888CongestionControlFeedback",
                                 "Enabled,offer:true");
  PeerScenarioClient* caller = s.CreateClient(caller_config);

  PeerScenarioClient::Config callee_config;
  callee_config.disable_encryption = true;
  callee_config.field_trials.Set("WebRTC-RFC8888CongestionControlFeedback",
                                 "Disabled");
  PeerScenarioClient* callee = s.CreateClient(callee_config);

  auto caller_to_callee = s.net()
                              ->NodeBuilder()
                              .capacity(DataRate::KilobitsPerSec(600))
                              .Build()
                              .node;
  auto callee_to_caller = s.net()
                              ->NodeBuilder()
                              .capacity(DataRate::KilobitsPerSec(600))
                              .Build()
                              .node;
  RtcpFeedbackCounter callee_feedback_counter;
  caller_to_callee->router()->SetWatcher([&](const EmulatedIpPacket& packet) {
    callee_feedback_counter.Count(packet);
  });
  RtcpFeedbackCounter caller_feedback_counter;
  callee_to_caller->router()->SetWatcher([&](const EmulatedIpPacket& packet) {
    caller_feedback_counter.Count(packet);
  });

  s.net()->CreateRoute(caller->endpoint(), {caller_to_callee},
                       callee->endpoint());
  s.net()->CreateRoute(callee->endpoint(), {callee_to_caller},
                       caller->endpoint());

  auto signaling = s.ConnectSignaling(caller, callee, {caller_to_callee},
                                      {callee_to_caller});
  PeerScenarioClient::VideoSendTrackConfig video_conf;
  video_conf.generator.squares_video->framerate = 30;
  video_conf.generator.squares_video->width = 320;
  video_conf.generator.squares_video->height = 180;
  caller->CreateVideo("FROM_CALLER", video_conf);
  callee->CreateVideo("FROM_CALLEE", video_conf);

  signaling.StartIceSignaling();
  std::atomic<bool> offer_exchange_done(false);
  signaling.NegotiateSdp([&](const SessionDescriptionInterface& answer) {
    offer_exchange_done = true;
  });
  ASSERT_TRUE(s.WaitAndProcess(&offer_exchange_done));
  s.ProcessMessages(TimeDelta::Seconds(2));

  EXPECT_EQ(caller_feedback_counter.FeedbackAccordingToRfc8888(), 0);
  EXPECT_EQ(callee_feedback_counter.FeedbackAccordingToRfc8888(), 0);
  int transport_cc_caller =
      caller_feedback_counter.FeedbackAccordingToTransportCc();
  int transport_cc_callee =
      callee_feedback_counter.FeedbackAccordingToTransportCc();
  EXPECT_GT(transport_cc_caller, 0);
  EXPECT_GT(transport_cc_callee, 0);

  offer_exchange_done = false;
  // Save the caller's local description and use it as answer to the next offer
  // from callee.
  std::string answer_str;
  caller->pc()->local_description()->ToString(&answer_str);
  ASSERT_FALSE(answer_str.empty());
  ASSERT_THAT(answer_str, ContainsRegex(ccfb_regex));

  callee->CreateAndSetSdp(
      [&](SessionDescriptionInterface* /*munge_offer*/) {
        // Do not munge the offer.
      },
      [&](std::string offer) {
        // Callee does not support ccfb and does not have it in the offer.
        ASSERT_THAT(offer, Not(ContainsRegex(ccfb_regex)));
        caller->SetRemoteDescription(
            offer, SdpType::kOffer, [&](RTCError error) {
              ASSERT_TRUE(error.ok());
              caller->SetLocalDescription(
                  answer_str, SdpType::kAnswer, [&](RTCError error) {
                    ASSERT_TRUE(error.ok());
                    callee->SetRemoteDescription(answer_str, SdpType::kAnswer,
                                                 [&](RTCError error) {
                                                   ASSERT_TRUE(error.ok());
                                                   offer_exchange_done = true;
                                                 });
                  });
            });
      });
  ASSERT_TRUE(s.WaitAndProcess(&offer_exchange_done));
  s.ProcessMessages(TimeDelta::Seconds(4));

  EXPECT_EQ(caller_feedback_counter.FeedbackAccordingToRfc8888(), 0);
  EXPECT_EQ(callee_feedback_counter.FeedbackAccordingToRfc8888(), 0);
  EXPECT_GT(caller_feedback_counter.FeedbackAccordingToTransportCc(),
            transport_cc_caller);
  EXPECT_GT(callee_feedback_counter.FeedbackAccordingToTransportCc(),
            transport_cc_callee);
}

struct SupportRfc8888Params {
  bool caller_supports_rfc8888 = false;
  bool callee_supports_rfc8888 = false;
  std::string test_suffix;
};

class FeedbackFormatTest : public TestWithParam<SupportRfc8888Params> {};

TEST_P(FeedbackFormatTest, AdaptToLinkCapacityWithoutEcn) {
  const SupportRfc8888Params& params = GetParam();
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());

  PeerScenarioClient::Config caller_config;
  caller_config.disable_encryption = true;
  caller_config.field_trials.Set(
      "WebRTC-RFC8888CongestionControlFeedback",
      params.caller_supports_rfc8888 ? "Enabled,offer:true" : "Disabled");
  PeerScenarioClient* caller = s.CreateClient(caller_config);

  PeerScenarioClient::Config callee_config;
  callee_config.disable_encryption = true;
  callee_config.field_trials.Set(
      "WebRTC-RFC8888CongestionControlFeedback",
      params.callee_supports_rfc8888 ? "Enabled" : "Disabled");
  PeerScenarioClient* callee = s.CreateClient(callee_config);

  auto caller_to_callee = s.net()
                              ->NodeBuilder()
                              .capacity(DataRate::KilobitsPerSec(250))
                              .Build()
                              .node;
  auto callee_to_caller = s.net()
                              ->NodeBuilder()
                              .capacity(DataRate::KilobitsPerSec(250))
                              .Build()
                              .node;
  RtcpFeedbackCounter callee_feedback_counter;
  caller_to_callee->router()->SetWatcher([&](const EmulatedIpPacket& packet) {
    callee_feedback_counter.Count(packet);
  });
  RtcpFeedbackCounter caller_feedback_counter;
  callee_to_caller->router()->SetWatcher([&](const EmulatedIpPacket& packet) {
    caller_feedback_counter.Count(packet);
  });

  s.net()->CreateRoute(caller->endpoint(), {caller_to_callee},
                       callee->endpoint());
  s.net()->CreateRoute(callee->endpoint(), {callee_to_caller},
                       caller->endpoint());

  auto signaling = s.ConnectSignaling(caller, callee, {caller_to_callee},
                                      {callee_to_caller});
  PeerScenarioClient::VideoSendTrackConfig video_conf;
  video_conf.generator.squares_video->framerate = 30;
  video_conf.generator.squares_video->width = 320;
  video_conf.generator.squares_video->height = 180;
  caller->CreateVideo("FROM_CALLER", video_conf);
  callee->CreateVideo("FROM_CALLEE", video_conf);
  caller->CreateAudio("FROM_CALLER", AudioOptions());
  callee->CreateAudio("FROM_CALLEE", AudioOptions());

  signaling.StartIceSignaling();
  std::atomic<bool> offer_exchange_done(false);
  signaling.NegotiateSdp([&](const SessionDescriptionInterface& answer) {
    offer_exchange_done = true;
  });
  s.WaitAndProcess(&offer_exchange_done);
  s.ProcessMessages(TimeDelta::Seconds(5));

  DataRate caller_available_bwe =
      GetAvailableSendBitrate(GetStatsAndProcess(s, caller));
  EXPECT_GT(caller_available_bwe.kbps(), 150);
  EXPECT_LT(caller_available_bwe.kbps(), 300);

  DataRate callee_available_bwe =
      GetAvailableSendBitrate(GetStatsAndProcess(s, callee));
  EXPECT_GT(callee_available_bwe.kbps(), 150);
  EXPECT_LT(callee_available_bwe.kbps(), 300);

  EXPECT_LT(GetAverageRoundTripTime(GetStatsAndProcess(s, caller)),
            TimeDelta::Millis(250));

  if (params.caller_supports_rfc8888 && params.callee_supports_rfc8888) {
    EXPECT_GT(caller_feedback_counter.FeedbackAccordingToRfc8888(), 0);
    EXPECT_GT(callee_feedback_counter.FeedbackAccordingToRfc8888(), 0);
    EXPECT_EQ(caller_feedback_counter.FeedbackAccordingToTransportCc(), 0);
    EXPECT_EQ(callee_feedback_counter.FeedbackAccordingToTransportCc(), 0);
  } else {
    EXPECT_EQ(caller_feedback_counter.FeedbackAccordingToRfc8888(), 0);
    EXPECT_EQ(callee_feedback_counter.FeedbackAccordingToRfc8888(), 0);
    EXPECT_GT(caller_feedback_counter.FeedbackAccordingToTransportCc(), 0);
    EXPECT_GT(callee_feedback_counter.FeedbackAccordingToTransportCc(), 0);
  }
}

INSTANTIATE_TEST_SUITE_P(
    L4STest,
    FeedbackFormatTest,
    testing::Values(
        SupportRfc8888Params{.caller_supports_rfc8888 = true,
                             .test_suffix = "OnlyCallerSupportsRfc8888"},
        SupportRfc8888Params{.callee_supports_rfc8888 = true,
                             .test_suffix = "OnlyCalleeSupportsRfc8888"},
        SupportRfc8888Params{.caller_supports_rfc8888 = true,
                             .callee_supports_rfc8888 = true,
                             .test_suffix = "SupportsRfc8888"}),
    [](const testing::TestParamInfo<SupportRfc8888Params>& info) {
      return info.param.test_suffix;
    });

TEST(L4STest, SendsEct1WithScream) {
  PeerScenario s(*test_info_);
  PeerScenarioClient::Config config;
  config.field_trials.Set("WebRTC-RFC8888CongestionControlFeedback",
                          "Enabled,offer:true");
  config.field_trials.Set("WebRTC-Bwe-ScreamV2", "Enabled");
  config.disable_encryption = true;
  PeerScenarioClient* caller = s.CreateClient(config);
  PeerScenarioClient* callee = s.CreateClient(config);
  EmulatedNetworkNode* caller_to_callee = s.net()->NodeBuilder().Build().node;
  EmulatedNetworkNode* callee_to_caller = s.net()->NodeBuilder().Build().node;
  s.net()->CreateRoute(caller->endpoint(), {caller_to_callee},
                       callee->endpoint());
  s.net()->CreateRoute(callee->endpoint(), {callee_to_caller},
                       caller->endpoint());
  RtcpFeedbackCounter feedback_counter;
  callee_to_caller->router()->SetWatcher(
      [&](const EmulatedIpPacket& packet) { feedback_counter.Count(packet); });

  test::SignalingRoute signaling = s.ConnectSignaling(
      caller, callee, {caller_to_callee}, {callee_to_caller});
  PeerScenarioClient::VideoSendTrackConfig video_conf;
  video_conf.generator.squares_video->framerate = 15;
  caller->CreateVideo("VIDEO_1", video_conf);
  signaling.StartIceSignaling();
  std::atomic<bool> offer_exchange_done(false);
  signaling.NegotiateSdp([&](const SessionDescriptionInterface& answer) {
    offer_exchange_done = true;
  });

  s.WaitAndProcess(&offer_exchange_done);
  s.ProcessMessages(TimeDelta::Seconds(3));
  EXPECT_EQ(GetPacketsSentWithEct1(GetStatsAndProcess(s, caller)),
            feedback_counter.ect1());
  EXPECT_GT(feedback_counter.ect1(), 0);
  EXPECT_EQ(feedback_counter.not_ect(), 0);
}

TEST(L4STest, StopsSendingEct1IfEct1MarkedPacketsAreDropped) {
  PeerScenario s(*test_info_);
  PeerScenarioClient::Config config;
  config.field_trials.Set("WebRTC-RFC8888CongestionControlFeedback",
                          "Enabled,offer:true");
  config.field_trials.Set("WebRTC-Bwe-ScreamV2", "Enabled");
  config.disable_encryption = true;
  PeerScenarioClient* caller = s.CreateClient(config);
  PeerScenarioClient* callee = s.CreateClient(config);
  EmulatedNetworkNode* caller_to_callee = s.net()->NodeBuilder().Build().node;
  EmulatedNetworkNode* callee_to_caller = s.net()->NodeBuilder().Build().node;
  // A middlebox that drops ECT(1) marked packets instead of bleaching them.
  // STUN is Not-ECT, so ICE stays writable and nothing else notices.
  caller_to_callee->router()->SetFilter([](const EmulatedIpPacket& packet) {
    return packet.ecn != EcnMarking::kEct1;
  });

  PeerScenarioClient::VideoSendTrackConfig video_conf;
  video_conf.generator.squares_video->framerate = 15;
  caller->CreateVideo("VIDEO_1", video_conf);
  s.SimpleConnection(caller, callee, {caller_to_callee}, {callee_to_caller});
  s.ProcessMessages(TimeDelta::Seconds(3));

  // No ECT(1) marked packet ever arrived, but media recovered as Not-ECT.
  scoped_refptr<const RTCStatsReport> callee_stats =
      GetStatsAndProcess(s, callee);
  EXPECT_EQ(GetPacketsReceivedWithEct1(callee_stats), 0);
  EXPECT_GT(GetPacketsReceived(callee_stats), 0);
}

TEST(L4STest, KeepsSendingEct1AfterTransientOutage) {
  PeerScenario s(*test_info_);
  PeerScenarioClient::Config config;
  config.field_trials.Set("WebRTC-RFC8888CongestionControlFeedback",
                          "Enabled,offer:true");
  config.field_trials.Set("WebRTC-Bwe-ScreamV2", "Enabled");
  config.disable_encryption = true;
  PeerScenarioClient* caller = s.CreateClient(config);
  PeerScenarioClient* callee = s.CreateClient(config);
  EmulatedNetworkNode* caller_to_callee = s.net()->NodeBuilder().Build().node;
  EmulatedNetworkNode* callee_to_caller = s.net()->NodeBuilder().Build().node;
  RtcpFeedbackCounter feedback_counter;
  callee_to_caller->router()->SetWatcher(
      [&](const EmulatedIpPacket& packet) { feedback_counter.Count(packet); });

  PeerScenarioClient::VideoSendTrackConfig video_conf;
  video_conf.generator.squares_video->framerate = 15;
  caller->CreateVideo("VIDEO_1", video_conf);
  s.SimpleConnection(caller, callee, {caller_to_callee}, {callee_to_caller});
  s.ProcessMessages(TimeDelta::Seconds(2));
  ASSERT_GT(feedback_counter.ect1(), 0);

  // A total uplink outage, such as a Wi-Fi scan, stops all feedback. It is
  // shorter than the timeout used once ECT(1) is known to work, so it must not
  // be mistaken for a path that drops the marking.
  caller_to_callee->router()->SetFilter(
      [](const EmulatedIpPacket& /*packet*/) { return false; });
  s.ProcessMessages(TimeDelta::Seconds(1));
  caller_to_callee->router()->SetFilter(nullptr);

  int ect1_before_recovery = feedback_counter.ect1();
  scoped_refptr<const RTCStatsReport> stats_after_outage =
      GetStatsAndProcess(s, caller);
  int64_t packets_sent_after_outage = GetPacketsSent(stats_after_outage);
  int64_t ect1_sent_after_outage = GetPacketsSentWithEct1(stats_after_outage);

  s.ProcessMessages(TimeDelta::Seconds(2));

  // Note that the feedback counter can not be used to verify that no Not-ECT
  // packet was sent, since RFC 8888 reports packets that were lost during the
  // outage with a Not-ECT marking. Compare the sender side counters instead.
  scoped_refptr<const RTCStatsReport> stats = GetStatsAndProcess(s, caller);
  EXPECT_EQ(GetPacketsSent(stats) - packets_sent_after_outage,
            GetPacketsSentWithEct1(stats) - ect1_sent_after_outage);
  EXPECT_GT(feedback_counter.ect1(), ect1_before_recovery);
}

TEST(L4STest, SendsEct1OnHighRttPath) {
  PeerScenario s(*test_info_);
  PeerScenarioClient::Config config;
  config.field_trials.Set("WebRTC-RFC8888CongestionControlFeedback",
                          "Enabled,offer:true");
  config.field_trials.Set("WebRTC-Bwe-ScreamV2", "Enabled");
  config.disable_encryption = true;
  PeerScenarioClient* caller = s.CreateClient(config);
  PeerScenarioClient* callee = s.CreateClient(config);
  // 400ms RTT. Feedback for the first ECT(1) packet must arrive before the
  // path is assumed to drop the marking.
  EmulatedNetworkNode* caller_to_callee =
      s.net()->NodeBuilder().delay_ms(200).Build().node;
  EmulatedNetworkNode* callee_to_caller =
      s.net()->NodeBuilder().delay_ms(200).Build().node;
  PeerScenarioClient::VideoSendTrackConfig video_conf;
  video_conf.generator.squares_video->framerate = 15;
  caller->CreateVideo("VIDEO_1", video_conf);
  s.SimpleConnection(caller, callee, {caller_to_callee}, {callee_to_caller});
  s.ProcessMessages(TimeDelta::Seconds(2));
  scoped_refptr<const RTCStatsReport> first_stats =
      GetStatsAndProcess(s, caller);
  int64_t packets_sent = GetPacketsSent(first_stats);
  int64_t ect1_sent = GetPacketsSentWithEct1(first_stats);

  s.ProcessMessages(TimeDelta::Seconds(2));

  scoped_refptr<const RTCStatsReport> stats = GetStatsAndProcess(s, caller);
  EXPECT_GT(GetPacketsSent(stats) - packets_sent, 0);
  EXPECT_EQ(GetPacketsSent(stats) - packets_sent,
            GetPacketsSentWithEct1(stats) - ect1_sent);
  EXPECT_GT(GetPacketsReceivedWithEct1(GetStatsAndProcess(s, callee)), 0);
}

TEST(L4STest, SendsEct1AfterRoundTripTimeDecrease) {
  PeerScenario s(*test_info_);
  PeerScenarioClient::Config config;
  config.field_trials.Set("WebRTC-RFC8888CongestionControlFeedback",
                          "Enabled,offer:true");
  config.field_trials.Set("WebRTC-Bwe-ScreamV2", "Enabled");
  config.disable_encryption = true;
  PeerScenarioClient* caller = s.CreateClient(config);
  PeerScenarioClient* callee = s.CreateClient(config);
  // The RTT starts at 600ms, which is longer than the time the first ECT(1)
  // packet is allowed to go unacknowledged, so ECT(1) is disabled. It then
  // drops to 30ms and the retry done 10s later succeeds.
  EmulatedNetworkNode* caller_to_callee = CreateNodeWithDelayChange(
      s, TimeDelta::Millis(300), TimeDelta::Millis(15), TimeDelta::Seconds(3));
  EmulatedNetworkNode* callee_to_caller = CreateNodeWithDelayChange(
      s, TimeDelta::Millis(300), TimeDelta::Millis(15), TimeDelta::Seconds(3));
  PeerScenarioClient::VideoSendTrackConfig video_conf;
  video_conf.generator.squares_video->framerate = 15;
  caller->CreateVideo("VIDEO_1", video_conf);
  s.SimpleConnection(caller, callee, {caller_to_callee}, {callee_to_caller});
  s.ProcessMessages(TimeDelta::Seconds(13));
  scoped_refptr<const RTCStatsReport> first_stats =
      GetStatsAndProcess(s, caller);
  int64_t packets_sent = GetPacketsSent(first_stats);
  int64_t ect1_sent = GetPacketsSentWithEct1(first_stats);

  s.ProcessMessages(TimeDelta::Seconds(2));

  scoped_refptr<const RTCStatsReport> stats = GetStatsAndProcess(s, caller);
  EXPECT_GT(GetPacketsSent(stats) - packets_sent, 0);
  EXPECT_EQ(GetPacketsSent(stats) - packets_sent,
            GetPacketsSentWithEct1(stats) - ect1_sent);
  EXPECT_GT(GetPacketsReceivedWithEct1(GetStatsAndProcess(s, callee)), 0);
}

TEST(L4STest, SendsEct1AfterRouteChangeEvenIfBleached) {
  PeerScenario s(*test_info_);

  PeerScenarioClient::Config config;
  config.field_trials.Set("WebRTC-RFC8888CongestionControlFeedback",
                          "Enabled,offer:true");
  config.field_trials.Set("WebRTC-Bwe-ScreamV2", "Enabled");
  config.disable_encryption = true;

  // Caller has both wifi and cellular adapters.
  config.endpoints = {{0, {.type = AdapterType::ADAPTER_TYPE_WIFI}},
                      {1, {.type = AdapterType::ADAPTER_TYPE_CELLULAR}}};
  PeerScenarioClient* caller = s.CreateClient(config);

  // Callee has only wifi adapter.
  config.endpoints = {{0, {.type = AdapterType::ADAPTER_TYPE_WIFI}}};
  PeerScenarioClient* callee = s.CreateClient(config);

  auto caller_wifi_node = s.net()->NodeBuilder().Build().node;
  auto caller_cellular_node = s.net()->NodeBuilder().Build().node;
  auto bleaching_node =
      s.net()->NodeBuilder().config({.forward_ecn = false}).Build().node;
  auto callee_to_caller_wifi = s.net()->NodeBuilder().config({}).Build().node;
  auto callee_to_caller_cellular =
      s.net()->NodeBuilder().config({}).Build().node;

  // Routes from caller to callee bleach ECN.
  s.net()->CreateRoute(caller->endpoint(0), {caller_wifi_node, bleaching_node},
                       callee->endpoint(0));
  s.net()->CreateRoute(caller->endpoint(1),
                       {caller_cellular_node, bleaching_node},
                       callee->endpoint(0));
  // Routes from callee to caller do not bleach ECN.
  s.net()->CreateRoute(callee->endpoint(0), {callee_to_caller_wifi},
                       caller->endpoint(0));
  s.net()->CreateRoute(callee->endpoint(0), {callee_to_caller_cellular},
                       caller->endpoint(1));

  int ect1_count_wifi = 0;
  int not_ect_count_wifi = 0;
  int ect1_count_cellular = 0;
  int not_ect_count_cellular = 0;
  std::atomic<bool> seen_ect1_on_wifi = false;
  std::atomic<bool> seen_ect1_on_cellular = false;
  std::atomic<bool> seen_not_ect_on_wifi = false;
  std::atomic<bool> seen_not_ect_on_cellular = false;

  caller_wifi_node->router()->SetWatcher([&](const EmulatedIpPacket& packet) {
    if (!IsRtpPacket(packet.data))
      return;
    if (packet.from.ipaddr() == caller->endpoint(0)->GetPeerLocalAddress()) {
      if (packet.ecn == EcnMarking::kEct1) {
        seen_ect1_on_wifi = true;
        ect1_count_wifi++;
      } else if (packet.ecn == EcnMarking::kNotEct) {
        not_ect_count_wifi++;
        seen_not_ect_on_wifi = true;
      }
    }
  });

  caller_cellular_node->router()->SetWatcher(
      [&](const EmulatedIpPacket& packet) {
        if (!IsRtpPacket(packet.data))
          return;
        if (packet.from.ipaddr() ==
            caller->endpoint(1)->GetPeerLocalAddress()) {
          if (packet.ecn == EcnMarking::kEct1) {
            seen_ect1_on_cellular = true;
            ect1_count_cellular++;
          } else if (packet.ecn == EcnMarking::kNotEct) {
            not_ect_count_cellular++;
            seen_not_ect_on_cellular = true;
          }
        }
      });

  auto signaling = s.ConnectSignaling(caller, callee, {bleaching_node},
                                      {callee_to_caller_wifi});
  PeerScenarioClient::VideoSendTrackConfig video_conf;
  video_conf.generator.squares_video->framerate = 15;

  caller->CreateVideo("VIDEO_1", video_conf);
  signaling.StartIceSignaling();

  std::atomic<bool> offer_exchange_done(false);
  signaling.NegotiateSdp([&](const SessionDescriptionInterface& answer) {
    offer_exchange_done = true;
  });
  s.WaitAndProcess(&offer_exchange_done);

  EXPECT_TRUE(s.WaitAndProcess(&seen_ect1_on_wifi, TimeDelta::Seconds(1)));
  EXPECT_TRUE(s.WaitAndProcess(&seen_not_ect_on_wifi, TimeDelta::Seconds(1)));

  // Disable caller's wifi and expect that the connection switch to cellular.
  s.net()->DisableEndpoint(caller->endpoint(0));
  EXPECT_TRUE(s.WaitAndProcess(&seen_ect1_on_cellular, TimeDelta::Seconds(5)));
  EXPECT_TRUE(
      s.WaitAndProcess(&seen_not_ect_on_cellular, TimeDelta::Seconds(5)));

  // Check statistics.
  auto packets_sent_with_ect1_stats =
      GetPacketsSentWithEct1(GetStatsAndProcess(s, caller));
  EXPECT_GE(packets_sent_with_ect1_stats, 0);

  scoped_refptr<const RTCStatsReport> callee_stats =
      GetStatsAndProcess(s, callee);
  EXPECT_EQ(GetPacketsReceivedWithEct1(callee_stats), 0);
  EXPECT_EQ(GetPacketsReceivedWithCe(callee_stats), 0);

  // Verify that packets were sent with ECT1 and then fell back to not-ECT.
  EXPECT_GT(ect1_count_wifi, 0);
  EXPECT_GT(not_ect_count_wifi, 0);
  EXPECT_GT(ect1_count_cellular, 0);
  EXPECT_GT(not_ect_count_cellular, 0);
}

TEST(L4STest, SendsEct1AfterRouteChangeFromTurnWithBleachingToDirect) {
  PeerScenario s(*test_info_);

  EmulatedTURNServerConfig turn_config;
  turn_config.client_config.type = AdapterType::ADAPTER_TYPE_WIFI;
  turn_config.peer_config.type = AdapterType::ADAPTER_TYPE_WIFI;
  EmulatedTURNServerInterface* turn_server =
      s.net()->CreateTURNServer(turn_config);

  auto ice_server_config = turn_server->GetIceServerConfig();
  PeerConnectionInterface::IceServer ice_server;
  ice_server.urls.push_back(ice_server_config.url);
  ice_server.username = ice_server_config.username;
  ice_server.password = ice_server_config.password;

  PeerScenarioClient::Config config;
  config.field_trials.Set("WebRTC-RFC8888CongestionControlFeedback",
                          "Enabled,offer:true");
  config.field_trials.Set("WebRTC-Bwe-ScreamV2", "Enabled");
  config.disable_encryption = true;
  config.endpoints = {{0, {.type = AdapterType::ADAPTER_TYPE_WIFI}}};
  config.rtc_config.servers.push_back(ice_server);
  PeerScenarioClient* caller = s.CreateClient(config);
  PeerScenarioClient* callee = s.CreateClient(config);

  // TURN routes for Caller.
  // Route via a node that will not forward ECN markings to simulate TURN
  // server bleaching. The route also has longer delay to test that packets will
  // be delivered out of order when switching to a direct route.
  s.net()->CreateRoute(
      caller->endpoint(0),
      {s.net()->NodeBuilder().delay_ms(50).Build().node,
       s.net()->NodeBuilder().config({.forward_ecn = false}).Build().node},
      turn_server->GetClientEndpoint());
  s.net()->CreateRoute(turn_server->GetClientEndpoint(),
                       {s.net()->NodeBuilder().Build().node},
                       caller->endpoint(0));

  // TURN routes for Callee.
  s.net()->CreateRoute(callee->endpoint(0),
                       {s.net()->NodeBuilder().Build().node},
                       turn_server->GetClientEndpoint());
  s.net()->CreateRoute(turn_server->GetClientEndpoint(),
                       {s.net()->NodeBuilder().Build().node},
                       callee->endpoint(0));

  auto signaling =
      s.ConnectSignaling(caller, callee, {s.net()->NodeBuilder().Build().node},
                         {s.net()->NodeBuilder().Build().node});

  PeerScenarioClient::VideoSendTrackConfig video_conf;
  video_conf.generator.squares_video->framerate = 15;
  caller->CreateVideo("VIDEO_1", video_conf);
  signaling.StartIceSignaling();

  std::atomic<bool> offer_exchange_done(false);
  signaling.NegotiateSdp([&](const SessionDescriptionInterface& answer) {
    offer_exchange_done = true;
  });
  ASSERT_TRUE(s.WaitAndProcess(&offer_exchange_done));
  ASSERT_TRUE(offer_exchange_done);

  /// Run simulation with the TURN route
  s.ProcessMessages(TimeDelta::Seconds(5));
  scoped_refptr<const RTCStatsReport> callee_stats =
      GetStatsAndProcess(s, callee);
  ASSERT_GT(GetPacketsReceived(callee_stats), 0);
  EXPECT_LT(GetAverageRoundTripTime(callee_stats), TimeDelta::Millis(90));

  // Create a direct route from caller to callee and callee to caller.
  EmulatedNetworkNode* caller_to_direct_node =
      s.net()->NodeBuilder().delay_ms(0).Build().node;
  s.net()->CreateRoute(caller->endpoint(0), {caller_to_direct_node},
                       callee->endpoint(0));
  int ect1_count_direct = 0;
  int not_ect_count_direct = 0;
  caller_to_direct_node->router()->SetWatcher(
      [&](const EmulatedIpPacket& packet) {
        if (!IsRtpPacket(packet.data))
          return;
        if (packet.ecn == EcnMarking::kEct1) {
          ++ect1_count_direct;
        } else if (packet.ecn == EcnMarking::kNotEct) {
          ++not_ect_count_direct;
        }
      });
  s.net()->CreateRoute(callee->endpoint(0),
                       {s.net()->NodeBuilder().Build().node},
                       caller->endpoint(0));

  s.ProcessMessages(TimeDelta::Seconds(10));
  // Expect that eventually, caller switches to sending packets with ect1 on the
  // direct route.
  EXPECT_GT(ect1_count_direct, 0);
  EXPECT_EQ(not_ect_count_direct, 0);
}

TEST(L4STest, RtcpSentAsEct1IfRtpWithEct1Received) {
  int ecn_count = 0;
  int not_ect_count = 0;
  PeerScenario s(*test_info_);
  PeerScenarioClient::Config config;
  config.field_trials.Set("WebRTC-RFC8888CongestionControlFeedback",
                          "Enabled,offer:true");
  config.field_trials.Set("WebRTC-Bwe-ScreamV2", "Enabled");
  PeerScenarioClient* caller = s.CreateClient(config);
  PeerScenarioClient* callee = s.CreateClient(config);
  EmulatedNetworkNode* caller_to_callee_node =
      s.net()->NodeBuilder().Build().node;
  EmulatedNetworkNode* callee_to_caller_node =
      s.net()->NodeBuilder().Build().node;
  // Callee is not sending media - Thus if Stun is ignored, most packets should
  // be RTCP. Negotiation is still done using not ECT.
  callee_to_caller_node->router()->SetWatcher(
      [&](const EmulatedIpPacket& packet) {
        if (StunMessage::ValidateFingerprint(packet.data)) {
          return;
        }
        if (packet.ecn == EcnMarking::kEct1 || packet.ecn == EcnMarking::kCe) {
          ecn_count++;
        } else {
          not_ect_count++;
        }
      });

  PeerScenarioClient::VideoSendTrackConfig video_conf;
  video_conf.generator.squares_video->framerate = 15;
  caller->CreateAudio("AUDIO_1", AudioOptions());
  caller->CreateVideo("VIDEO_1", video_conf);

  s.SimpleConnection(caller, callee, {caller_to_callee_node},
                     {callee_to_caller_node});
  s.ProcessMessages(TimeDelta::Seconds(1));

  // Feedback is sent every 25ms. Expect more than 20 feedback packets during
  // 1S.
  EXPECT_GT(ecn_count, 20);
  EXPECT_LT(not_ect_count, 10);
}

TEST(L4STest, RtcpSentAsNotEctIfRtpEcnBleached) {
  int rtcp_ecn_count = 0;
  int rtcp_not_ect_count = 0;
  PeerScenario s(*test_info_);
  PeerScenarioClient::Config config;
  config.field_trials.Set("WebRTC-RFC8888CongestionControlFeedback",
                          "Enabled,offer:true");
  config.field_trials.Set("WebRTC-Bwe-ScreamV2", "Enabled");
  config.disable_encryption = true;
  PeerScenarioClient* caller = s.CreateClient(config);
  PeerScenarioClient* callee = s.CreateClient(config);

  EmulatedNetworkNode* caller_to_callee_node =
      s.net()->NodeBuilder().config({.forward_ecn = false}).Build().node;
  EmulatedNetworkNode* callee_to_caller_node =
      s.net()->NodeBuilder().Build().node;

  callee_to_caller_node->router()->SetWatcher(
      [&](const EmulatedIpPacket& packet) {
        if (!IsRtcpPacket(packet.data)) {
          return;
        }
        if (packet.ecn == EcnMarking::kEct1 || packet.ecn == EcnMarking::kCe) {
          rtcp_ecn_count++;
        } else {
          rtcp_not_ect_count++;
        }
      });

  PeerScenarioClient::VideoSendTrackConfig video_conf;
  video_conf.generator.squares_video->framerate = 30;
  caller->CreateAudio("AUDIO_1", AudioOptions());
  caller->CreateVideo("VIDEO_1", video_conf);
  s.SimpleConnection(caller, callee, {caller_to_callee_node},
                     {callee_to_caller_node});
  s.ProcessMessages(TimeDelta::Seconds(1));

  EXPECT_EQ(rtcp_ecn_count, 0);
  EXPECT_GT(rtcp_not_ect_count, 0);
}

#if !defined(WEBRTC_IOS)
// TODO(bugs.webrtc.org/42225697): investigate why CcFbSendRateAdaptation fails
// on iOS bots.
enum class TestVariant { kCcfbWithGoogCc, kCcfbWithScreamV2 };

// These tests that the bitrate used by Congestion Control feedback is reduced
// at extremely low available bandwidths. If the sender stops utilizing the
// capacity, the bitrate may increase again.
class CcFbSendRateAdaptation : public TestWithParam<TestVariant> {
 protected:
  struct TestParticipants {
    PeerScenarioClient* caller;
    PeerScenarioClient* callee;
    EmulatedNetworkNode* caller_to_callee;
    EmulatedNetworkNode* callee_to_caller;
    std::optional<PeerScenarioClient::AudioSendTrack> caller_audio;
  };

  TestParticipants SetupSendAudioOnLowCapacityUplink(
      PeerScenario& s,
      TestVariant variant,
      bool caller_sends_audio = true) {
    PeerScenarioClient::Config caller_config;
    PeerScenarioClient::Config callee_config;

    if (variant == TestVariant::kCcfbWithGoogCc) {
      caller_config.field_trials.Set(
          "WebRTC-RFC8888CongestionControlFeedback",
          "Enabled,offer:true,feedback_fraction:0.1");
      callee_config.field_trials.Set("WebRTC-RFC8888CongestionControlFeedback",
                                     "Enabled,feedback_fraction:0.1");
    } else if (variant == TestVariant::kCcfbWithScreamV2) {
      caller_config.field_trials.Set(
          "WebRTC-RFC8888CongestionControlFeedback",
          "Enabled,offer:true,feedback_fraction:0.1");
      caller_config.field_trials.Set("WebRTC-Bwe-ScreamV2", "Enabled");
      callee_config.field_trials.Set("WebRTC-RFC8888CongestionControlFeedback",
                                     "Enabled,feedback_fraction:0.1");
      callee_config.field_trials.Set("WebRTC-Bwe-ScreamV2", "Enabled");
    }

    PeerScenarioClient* caller = s.CreateClient(caller_config);
    PeerScenarioClient* callee = s.CreateClient(callee_config);

    EmulatedNetworkNode* caller_to_callee = s.net()
                                                ->NodeBuilder()
                                                .capacity_kbps(50)
                                                .delay_ms(50)
                                                .packet_queue_length(20)
                                                .Build()
                                                .node;
    EmulatedNetworkNode* callee_to_caller =
        s.net()->NodeBuilder().delay_ms(50).capacity_kbps(100000).Build().node;

    std::optional<PeerScenarioClient::AudioSendTrack> caller_audio;
    // Caller maybe sends AUDIO only, but wants to receive VIDEO from callee.
    if (caller_sends_audio) {
      caller_audio = caller->CreateAudio("AUDIO", AudioOptions());
      RtpParameters caller_audio_params = caller_audio->sender->GetParameters();
      for (auto& encoding : caller_audio_params.encodings) {
        // Adaptive frame length allow audio to adapt to BWE.
        encoding.adaptive_ptime = true;
      }
      caller_audio->sender->SetParameters(caller_audio_params);
    } else {
      RTCErrorOr<scoped_refptr<RtpTransceiverInterface>> audio_transceiver =
          caller->pc()->AddTransceiver(MediaType::AUDIO);
      RTC_CHECK(audio_transceiver.ok());
      audio_transceiver.value()->SetDirectionWithError(
          RtpTransceiverDirection::kRecvOnly);
    }
    RTCErrorOr<scoped_refptr<RtpTransceiverInterface>> transceiver =
        caller->pc()->AddTransceiver(MediaType::VIDEO);
    RTC_CHECK(transceiver.ok());
    transceiver.value()->SetDirectionWithError(
        RtpTransceiverDirection::kRecvOnly);

    // Callee sends AUDIO and VIDEO.
    PeerScenarioClient::VideoSendTrackConfig video_conf;
    video_conf.generator.squares_video->framerate = 30;
    video_conf.generator.squares_video->width = 320;
    video_conf.generator.squares_video->height = 180;
    callee->CreateVideo("VIDEO", video_conf);
    PeerScenarioClient::AudioSendTrack callee_audio =
        callee->CreateAudio("AUDIO", AudioOptions());
    RtpParameters callee_audio_params = callee_audio.sender->GetParameters();
    for (auto& encoding : callee_audio_params.encodings) {
      encoding.adaptive_ptime = true;
    }
    callee_audio.sender->SetParameters(callee_audio_params);

    s.SimpleConnection(caller, callee, {caller_to_callee}, {callee_to_caller});

    // Allow 10S for BWE convergence and leaving ALR.
    s.ProcessMessages(TimeDelta::Seconds(10));

    if (caller_sends_audio) {
      EXPECT_THAT(GetAvailableSendBitrate(GetStatsAndProcess(s, caller)).kbps(),
                  AllOf(Ge(20), Lt(80)));
    }

    // Callee has unlimited capacity (100 Mbps). BWE should be high, but due to
    // very infrequent feedback, we can not expect it to adapt very fast.
    EXPECT_THAT(GetAvailableSendBitrate(GetStatsAndProcess(s, callee)).kbps(),
                Gt(500));

    return TestParticipants{
        .caller = caller,
        .callee = callee,
        .caller_to_callee = caller_to_callee,
        .callee_to_caller = callee_to_caller,
        .caller_audio = caller_audio,
    };
  }
};

TEST_P(CcFbSendRateAdaptation, RtcpRateReducedWhenLinkIsUtilized) {
  const TestVariant variant = GetParam();
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());

  TestParticipants setup = SetupSendAudioOnLowCapacityUplink(s, variant);

  Timestamp start_time = s.net()->Now();
  std::atomic<int64_t> total_rtcp_bytes = 0;
  setup.caller_to_callee->router()->SetWatcher(
      [&](const EmulatedIpPacket& packet) {
        if (IsRtcpPacket(packet.data)) {
          total_rtcp_bytes += packet.ip_packet_size();
        }
      });
  s.ProcessMessages(TimeDelta::Seconds(10));
  TimeDelta duration = s.net()->Now() - start_time;

  DataRate average_rtcp_bitrate =
      DataSize::Bytes(total_rtcp_bytes.load()) / duration;
  RTC_LOG(LS_INFO) << "Average RTCP traffic bitrate: " << average_rtcp_bitrate;

  // Allow RTCP to take up to 15% of the link capacity. (Feedback, NACK and SR
  // RR)
  EXPECT_LT(average_rtcp_bitrate, DataRate::KilobitsPerSec(50) * 0.15);
}

TEST_P(CcFbSendRateAdaptation, RtcpRateIncreaseWhenAudioStopSending) {
  const TestVariant variant = GetParam();
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());

  TestParticipants setup = SetupSendAudioOnLowCapacityUplink(s, variant);

  // Stop sending audio. Bwe should enter ALR and thus allow more RTCP traffic.
  EXPECT_TRUE(setup.caller_audio->sender->SetTrack(nullptr));

  Timestamp start_time = s.net()->Now();
  std::atomic<int64_t> total_rtcp_bytes = 0;
  setup.caller_to_callee->router()->SetWatcher(
      [&](const EmulatedIpPacket& packet) {
        if (IsRtcpPacket(packet.data)) {
          total_rtcp_bytes += packet.ip_packet_size();
        }
      });

  s.ProcessMessages(TimeDelta::Seconds(15));
  TimeDelta duration = s.net()->Now() - start_time;

  DataRate average_rtcp_bitrate =
      DataSize::Bytes(total_rtcp_bytes.load()) / duration;
  RTC_LOG(LS_INFO) << "Average RTCP traffic bitrate when audio sender stops: "
                   << average_rtcp_bitrate;

  EXPECT_GT(average_rtcp_bitrate, DataRate::KilobitsPerSec(50) * 0.15);
}

TEST_P(CcFbSendRateAdaptation, RtcpRateHigherWhenCallerNeverSendsMedia) {
  const TestVariant variant = GetParam();
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());

  TestParticipants setup = SetupSendAudioOnLowCapacityUplink(
      s, variant, /*caller_sends_audio=*/false);

  Timestamp start_time = s.net()->Now();
  std::atomic<int64_t> total_rtcp_bytes = 0;
  setup.caller_to_callee->router()->SetWatcher(
      [&](const EmulatedIpPacket& packet) {
        if (IsRtcpPacket(packet.data)) {
          total_rtcp_bytes += packet.ip_packet_size();
        }
      });
  s.ProcessMessages(TimeDelta::Seconds(10));
  TimeDelta duration = s.net()->Now() - start_time;

  DataRate average_rtcp_bitrate =
      DataSize::Bytes(total_rtcp_bytes.load()) / duration;
  RTC_LOG(LS_INFO)
      << "Average RTCP traffic bitrate when caller does not send media: "
      << average_rtcp_bitrate;

  EXPECT_GT(average_rtcp_bitrate, DataRate::KilobitsPerSec(50) * 0.15);
}

INSTANTIATE_TEST_SUITE_P(L4STest,
                         CcFbSendRateAdaptation,
                         testing::Values(TestVariant::kCcfbWithGoogCc,
                                         TestVariant::kCcfbWithScreamV2),
                         [](const testing::TestParamInfo<TestVariant>& info) {
                           switch (info.param) {
                             case TestVariant::kCcfbWithGoogCc:
                               return "CcfbWithGoogCc";
                             case TestVariant::kCcfbWithScreamV2:
                               return "CcfbWithScreamV2";
                           }
                           RTC_CHECK_NOTREACHED();
                         });
#endif  // !defined(WEBRTC_IOS)

}  // namespace
}  // namespace webrtc
