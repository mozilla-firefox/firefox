/*
 *  Copyright (c) 2025 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/flags/flag.h"
#include "api/audio_options.h"
#include "api/rtp_parameters.h"
#include "api/scoped_refptr.h"
#include "api/stats/rtc_stats_report.h"
#include "api/test/network_emulation/dual_pi2_network_queue.h"
#include "api/test/network_emulation/leaky_bucket_network_queue.h"
#include "api/test/network_emulation/network_config_schedule.pb.h"
#include "api/test/network_emulation/network_emulation_interfaces.h"
#include "api/test/network_emulation/network_queue.h"
#include "api/test/network_emulation/schedulable_network_node_builder.h"
#include "api/test/network_emulation/token_bucket_network_behavior_builder.h"
#include "api/test/network_emulation_manager.h"
#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "rtc_base/logging.h"
#include "test/create_frame_generator_capturer.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/network/network_emulation.h"
#include "test/peer_scenario/bwe_integration_tests/stats_utilities.h"
#include "test/peer_scenario/peer_scenario.h"
#include "test/peer_scenario/peer_scenario_client.h"

// Tests for SCReAM.
// Per default, the tests currently does not set expectations on BWE. The
// reason is due to that the tests are not deterministic enough on all
// platforms.
// TODO: bugs.webrtc.org/447037083 - Attempt to make tests stable enough for
// the none deterministic behaviour to not matter.
// To enable BWE expectations, set the flag --enable-scream-bwe-expectations.
// To write RTC eventlogs to file, set the flag --peer_logs=true and
// --peer_logs_root=<path>.

ABSL_FLAG(bool,
          enable_scream_bwe_expectations,
          false,
          "Enable BWE expectations in Scream tests.");

namespace webrtc {
namespace {

using test::GetAvailableSendBitrate;
using test::GetAveragePacingDelay;
using test::GetCurrentRoundTripTime;
using test::GetFirstReportAtOrAfter;
using test::GetPacketsLost;
using test::GetPacketsReceived;
using test::GetPacketsReceivedWithEct1;
using test::GetPacketsSent;
using test::GetStatsAndProcess;
using test::PeerScenario;
using test::PeerScenarioClient;
using ::testing::AllOf;
using ::testing::Each;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::Lt;
using ::testing::TestWithParam;

#if defined(ADDRESS_SANITIZER)
// These tests are too slow to run with ASAN.
#define MaybeTest(t) DISABLED_##t
#else
#define MaybeTest(t) t
#endif

MATCHER_P2(AvailableSendBitrateIsBetween, low, high, "") {
  DataRate available_bwe = GetAvailableSendBitrate(arg);
  if (available_bwe > low && available_bwe < high) {
    return true;
  }
  *result_listener << "the available send bitrate is " << available_bwe.kbps()
                   << "kbps, which is not between " << low.kbps() << "kbps and "
                   << high.kbps() << " kbps";
  if (absl::GetFlag(FLAGS_enable_scream_bwe_expectations)) {
    // TODO: bugs.webrtc.org/447037083 - Attempt to make tests stable enough for
    // the none deterministic behaviour to not matter.
    return false;
  }
  RTC_LOG(LS_ERROR) << "!!! the available send bitrate is "
                    << available_bwe.kbps() << "kbps, which is not between "
                    << low.kbps() << "kbps and " << high.kbps() << " kbps";
  return true;
}

MATCHER_P2(AvailableSendBitrateIsZeroOrBetween, low, high, "") {
  DataRate available_bwe = GetAvailableSendBitrate(arg);
  if (available_bwe == DataRate::Zero() ||
      (available_bwe > low && available_bwe < high)) {
    return true;
  }
  *result_listener << "the available send bitrate is " << available_bwe.kbps()
                   << "kbps, which is neither 0 nor between " << low.kbps()
                   << "kbps and " << high.kbps() << " kbps";
  if (absl::GetFlag(FLAGS_enable_scream_bwe_expectations)) {
    // TODO: bugs.webrtc.org/447037083 - Attempt to make tests stable enough for
    // the none deterministic behaviour to not matter.
    return false;
  }
  RTC_LOG(LS_ERROR) << "!!! the available send bitrate is "
                    << available_bwe.kbps()
                    << "kbps, which is neither 0 nor between " << low.kbps()
                    << "kbps and " << high.kbps() << " kbps";
  return true;
}

MATCHER_P2(AveragePacingDelayIsBelow, max_delay, previous_report, "") {
  TimeDelta pacing_delay = GetAveragePacingDelay(arg, previous_report);
  if (pacing_delay < max_delay) {
    return true;
  }
  *result_listener << "the pacing delay is " << pacing_delay.ms()
                   << "ms, which is not below " << max_delay.ms() << "ms";
  return false;
}

MATCHER_P2(CurrentRoundTripTimeIsBetween, low, high, "") {
  TimeDelta rtt = GetCurrentRoundTripTime(arg);
  if (rtt > low && rtt < high) {
    return true;
  }
  *result_listener << "the current round trip time is " << rtt.ms()
                   << "ms, which is not between " << low.ms() << "ms and "
                   << high.ms() << " ms";

  return false;
}

std::vector<EmulatedNetworkNode*> CreateNetworkPath(
    NetworkEmulationManager::SimulatedNetworkNode::Builder& network_builder,
    bool use_dual_pi) {
  std::unique_ptr<NetworkQueueFactory> queue_factory;
  if (use_dual_pi) {
    queue_factory = std::make_unique<DualPi2NetworkQueueFactory>();
    network_builder.queue_factory(*queue_factory);
  }
  return {network_builder.Build().node};
}

std::vector<EmulatedNetworkNode*> CreateNetworkPath(PeerScenario& s,
                                                    bool use_dual_pi,
                                                    DataRate link_capacity,
                                                    TimeDelta one_way_delay) {
  NetworkEmulationManager::SimulatedNetworkNode::Builder network_builder =
      s.net()
          ->NodeBuilder()
          .capacity(link_capacity)
          .delay_ms(one_way_delay.ms());
  return CreateNetworkPath(network_builder, use_dual_pi);
}

std::vector<EmulatedNetworkNode*> CreateNetworkPathWithPauseBetween3sAnd6s(
    PeerScenario& s) {
  network_behaviour::NetworkConfigSchedule schedule;
  auto initial_config = schedule.add_item();
  initial_config->set_link_capacity_kbps(1000);
  auto updated_capacity = schedule.add_item();
  updated_capacity->set_time_since_first_sent_packet_ms(3000);
  updated_capacity->set_link_capacity_kbps(0);
  updated_capacity = schedule.add_item();
  updated_capacity->set_time_since_first_sent_packet_ms(6000);
  updated_capacity->set_link_capacity_kbps(1000);
  SchedulableNetworkNodeBuilder schedulable_builder(*s.net(),
                                                    std::move(schedule));
  return {schedulable_builder.Build()};
}

std::vector<EmulatedNetworkNode*> CreateNetworkPathWithRepeatedPause(
    PeerScenario& s,
    DataRate link_capacity,
    TimeDelta pause_duration,
    TimeDelta repeat_pause_interval) {
  network_behaviour::NetworkConfigSchedule schedule;
  auto initial_config = schedule.add_item();
  initial_config->set_link_capacity_kbps(link_capacity.kbps());
  initial_config->set_queue_delay_ms(10);
  auto updated_capacity = schedule.add_item();
  updated_capacity->set_time_since_first_sent_packet_ms(
      repeat_pause_interval.ms());
  updated_capacity->set_link_capacity_kbps(0);
  schedule.set_repeat_schedule_after_last_ms(pause_duration.ms());
  SchedulableNetworkNodeBuilder schedulable_builder(*s.net(),
                                                    std::move(schedule));
  return {schedulable_builder.Build()};
}

std::vector<EmulatedNetworkNode*> CreateNetworkPathWithPause(
    PeerScenario& s,
    DataRate link_capacity,
    TimeDelta pause_start,
    TimeDelta pause_duration) {
  network_behaviour::NetworkConfigSchedule schedule;
  auto initial_config = schedule.add_item();
  initial_config->set_link_capacity_kbps(link_capacity.kbps());
  initial_config->set_queue_delay_ms(10);

  auto updated_capacity = schedule.add_item();
  updated_capacity->set_time_since_first_sent_packet_ms(pause_start.ms());
  updated_capacity->set_link_capacity_kbps(0);

  updated_capacity = schedule.add_item();
  updated_capacity->set_time_since_first_sent_packet_ms(
      (pause_start + pause_duration).ms());
  updated_capacity->set_link_capacity_kbps(link_capacity.kbps());

  SchedulableNetworkNodeBuilder schedulable_builder(*s.net(),
                                                    std::move(schedule));
  return {schedulable_builder.Build()};
}

std::vector<EmulatedNetworkNode*> CreateNetworkPath1MbitDelayIncreaseAfter3S(
    PeerScenario& s) {
  network_behaviour::NetworkConfigSchedule schedule;
  auto initial_config = schedule.add_item();
  initial_config->set_link_capacity_kbps(1000);
  initial_config->set_queue_delay_ms(10);
  auto updated_latency = schedule.add_item();
  updated_latency->set_time_since_first_sent_packet_ms(3000);
  updated_latency->set_queue_delay_ms(80);

  SchedulableNetworkNodeBuilder schedulable_builder(*s.net(),
                                                    std::move(schedule));
  return {schedulable_builder.Build()};
}

std::vector<EmulatedNetworkNode*> CreateNetworkPathWithChangedCapacityAfter5s(
    PeerScenario& s,
    DataRate link_capacity_1,
    DataRate link_capacity_2) {
  network_behaviour::NetworkConfigSchedule schedule;
  auto initial_config = schedule.add_item();
  initial_config->set_link_capacity_kbps(link_capacity_1.kbps());
  initial_config->set_queue_delay_ms(15);
  auto updated_capacity = schedule.add_item();
  updated_capacity->set_time_since_first_sent_packet_ms(5000);
  updated_capacity->set_link_capacity_kbps(link_capacity_2.kbps());

  SchedulableNetworkNodeBuilder schedulable_builder(*s.net(),
                                                    std::move(schedule));
  return {schedulable_builder.Build()};
}

std::vector<EmulatedNetworkNode*> CreateNetworkPathWithVaryingCapacity(
    PeerScenario& s,
    DataRate link_capacity_1 = DataRate::KilobitsPerSec(40),
    DataRate link_capacity_2 = DataRate::KilobitsPerSec(90),
    TimeDelta interval = TimeDelta::Millis(800),
    TimeDelta one_way_delay = TimeDelta::Millis(100),
    bool use_ecn = false) {
  network_behaviour::NetworkConfigSchedule schedule;
  auto initial_config = schedule.add_item();
  initial_config->set_link_capacity_kbps(link_capacity_1.kbps());
  initial_config->set_queue_delay_ms(one_way_delay.ms());
  auto updated_capacity = schedule.add_item();
  updated_capacity->set_time_since_first_sent_packet_ms(interval.ms());
  updated_capacity->set_link_capacity_kbps(link_capacity_2.kbps());
  updated_capacity->set_queue_delay_ms(one_way_delay.ms());
  schedule.set_repeat_schedule_after_last_ms(interval.ms());

  SchedulableNetworkNodeBuilder schedulable_builder(*s.net(),
                                                    std::move(schedule));
  if (use_ecn) {
    LeakyBucketNetworkQueueFactory queue_factory(
        LeakyBucketNetworkQueue::Config{
            .max_ect1_sojourn_time = TimeDelta::Millis(40),
            .target_ect1_sojourn_time = TimeDelta::Millis(20)});
    schedulable_builder.set_queue_factory(queue_factory);
  }
  return {schedulable_builder.Build()};
}

struct SendMediaTestResult {
  // Stats gathered every second during the call.
  std::vector<scoped_refptr<const RTCStatsReport>> caller_stats;
  std::vector<scoped_refptr<const RTCStatsReport>> callee_stats;

  std::span<const scoped_refptr<const RTCStatsReport>> caller() const {
    return std::span<const scoped_refptr<const RTCStatsReport>>(caller_stats);
  }

  std::span<const scoped_refptr<const RTCStatsReport>> callee() const {
    return std::span<const scoped_refptr<const RTCStatsReport>>(callee_stats);
  }
};

struct SendMediaTestParams {
  std::vector<EmulatedNetworkNode*> caller_to_callee_path;
  std::vector<EmulatedNetworkNode*> callee_to_caller_path;
  std::map</*trial*/ std::string, /*group*/ std::string> field_trials = {
      {"WebRTC-RFC8888CongestionControlFeedback",
       "Enabled,offer:true,feedback_fraction:0.10"},
      {"WebRTC-Bwe-ScreamV2", "Enabled"}};

  // Audio is negotiated and sent.
  bool send_audio = true;
  // Video is negotiated and sent.
  bool send_video = true;
  // Video capturer is producing frames.
  bool video_capture_enabled = true;
  // Media is sent in both directions (caller and callee).
  bool send_in_two_directions = false;
  // Adaptive frame length allow audio to adapt to BWE.
  bool adaptive_ptime = false;

  // Configure and set the max video encoding bitrate. If not set, the max
  // default per codec and resolution is used.
  std::optional<DataRate> max_video_bitrate;

  PeerScenarioClient::VideoSendTrackConfig caller_video_conf = {
      .generator = {.squares_video =
                        test::FrameGeneratorCapturerConfig::SquaresVideo{
                            .framerate = 30,
                            .width = 1280,
                            .height = 720,
                        }}};

  TimeDelta test_duration = TimeDelta::Seconds(10);
  TimeDelta stats_interval = TimeDelta::Seconds(1);
};

// Sends media between a caller and a callee.
SendMediaTestResult SendMedia(SendMediaTestParams params, PeerScenario& s) {
  PeerScenarioClient::Config config;
  for (auto [trial, group] : params.field_trials) {
    config.field_trials.Set(trial, group);
  }
  PeerScenarioClient* caller = s.CreateClient(config);
  PeerScenarioClient* callee = s.CreateClient(config);

  if (params.send_audio) {
    PeerScenarioClient::AudioSendTrack audio =
        caller->CreateAudio("AUDIO_1", {});
    if (params.adaptive_ptime) {
      RtpParameters caller_audio_params = audio.sender->GetParameters();
      for (auto& encoding : caller_audio_params.encodings) {
        // Adaptive frame length allow audio to adapt to BWE.
        encoding.adaptive_ptime = true;
      }
      audio.sender->SetParameters(caller_audio_params);
    }
  }
  if (params.send_video) {
    test::PeerScenarioClient::VideoSendTrack video_track =
        caller->CreateVideo("VIDEO_1", params.caller_video_conf);
    if (!params.video_capture_enabled) {
      // Stop the capturer.
      video_track.source->Stop();
    }
    if (params.max_video_bitrate.has_value()) {
      RtpParameters rtp_parameters = video_track.sender->GetParameters();
      rtp_parameters.encodings[0].max_bitrate_bps =
          params.max_video_bitrate->bps();
      video_track.sender->SetParameters(rtp_parameters);
    }
  }

  if (params.send_in_two_directions) {
    if (params.send_audio) {
      PeerScenarioClient::AudioSendTrack audio =
          callee->CreateAudio("AUDIO_2", {});
      if (params.adaptive_ptime) {
        RtpParameters callee_audio_params = audio.sender->GetParameters();
        for (auto& encoding : callee_audio_params.encodings) {
          // Adaptive frame length allow audio to adapt to BWE.
          encoding.adaptive_ptime = true;
        }
        audio.sender->SetParameters(callee_audio_params);
      }
    }
    if (params.send_video) {
      test::PeerScenarioClient::VideoSendTrack video_track =
          callee->CreateVideo("VIDEO_2", params.caller_video_conf);
      if (!params.video_capture_enabled) {
        // Stop the capturer.
        video_track.source->Stop();
      }
      if (params.max_video_bitrate.has_value()) {
        RtpParameters rtp_parameters = video_track.sender->GetParameters();
        rtp_parameters.encodings[0].max_bitrate_bps =
            params.max_video_bitrate->bps();
        video_track.sender->SetParameters(rtp_parameters);
      }
    }
  }

  s.SimpleConnection(caller, callee, std::move(params.caller_to_callee_path),
                     std::move(params.callee_to_caller_path));
  SendMediaTestResult result;

  Timestamp end_time = s.net()->Now() + params.test_duration;
  while (s.net()->Now() < end_time) {
    s.ProcessMessages(params.stats_interval);
    result.caller_stats.push_back(GetStatsAndProcess(s, caller));
    result.callee_stats.push_back(GetStatsAndProcess(s, callee));
  }
  return result;
}

// This test is not using Scream - it is only here as a reference.
TEST(ScreamTest, MaybeTest(LinkCapacity600KbpsRtt100msNoEcnWithGoogCC)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params{
      .field_trials = {
          {"WebRTC-RFC8888CongestionControlFeedback", "Enabled,offer:true"}}};
  params.callee_to_caller_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(600), TimeDelta::Millis(50));
  params.caller_to_callee_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(600), TimeDelta::Millis(50));
  SendMediaTestResult result = SendMedia(std::move(params), s);
  EXPECT_THAT(result.caller_stats.back(),
              AvailableSendBitrateIsBetween(DataRate::KilobitsPerSec(450),
                                            DataRate::KilobitsPerSec(700)));
}

TEST(ScreamTest, MaybeTest(LinkCapacity600KbpsRtt100msNoEcn)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params;
  params.callee_to_caller_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(600), TimeDelta::Millis(50));
  params.caller_to_callee_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(600), TimeDelta::Millis(50));
  SendMediaTestResult result = SendMedia(std::move(params), s);
  EXPECT_THAT(
      result.caller().subspan(0, 4),
      Each(AvailableSendBitrateIsBetween(DataRate::KilobitsPerSec(200),
                                         DataRate::KilobitsPerSec(700))));
  EXPECT_THAT(result.caller().subspan(4), Each(AvailableSendBitrateIsBetween(
                                              DataRate::KilobitsPerSec(400),
                                              DataRate::KilobitsPerSec(650))));
}

TEST(ScreamTest,
     MaybeTest(LinkCapacityIncreaseFrom500KbitTo5MbpsAfter5sNoEcn)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params;
  params.caller_to_callee_path = CreateNetworkPathWithChangedCapacityAfter5s(
      s, DataRate::KilobitsPerSec(500), DataRate::KilobitsPerSec(5000));
  params.callee_to_caller_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(3000), TimeDelta::Millis(25));
  SendMediaTestResult result = SendMedia(std::move(params), s);
  // Stats 2-5s
  EXPECT_THAT(
      result.caller().subspan(1, 3),
      Each(AvailableSendBitrateIsBetween(DataRate::KilobitsPerSec(200),
                                         DataRate::KilobitsPerSec(600))));
  // Stats after 9s
  EXPECT_THAT(result.caller().subspan(9), Each(AvailableSendBitrateIsBetween(
                                              DataRate::KilobitsPerSec(1200),
                                              DataRate::KilobitsPerSec(5000))));
}

TEST(
    ScreamTest,
    MaybeTest(
        LinkCapacityIncreaseFrom80KbitTo5MbpsAfter5sVideoCaptureStoppedNoEcn)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params;
  params.video_capture_enabled = false;
  params.max_video_bitrate = DataRate::KilobitsPerSec(3000);
  params.caller_to_callee_path = CreateNetworkPathWithChangedCapacityAfter5s(
      s, DataRate::KilobitsPerSec(80), DataRate::KilobitsPerSec(5000));
  params.callee_to_caller_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(3000), TimeDelta::Millis(25));
  SendMediaTestResult result = SendMedia(std::move(params), s);
  // Stats 2-5s
  EXPECT_THAT(
      result.caller().subspan(1, 3),
      Each(AvailableSendBitrateIsBetween(DataRate::KilobitsPerSec(10),
                                         DataRate::KilobitsPerSec(150))));
  EXPECT_THAT(result.caller()[3],
              CurrentRoundTripTimeIsBetween(TimeDelta::Millis(40),
                                            TimeDelta::Millis(200)));
  // Stats after 9s
  // TODO: bugs.webrtc.org/447037083 - Figure out why setting max_video_bitrate
  // does not force available send bitrate to increase despite that the camera
  // is disabled.
  EXPECT_THAT(result.caller_stats.back(),
              AvailableSendBitrateIsBetween(DataRate::KilobitsPerSec(100),
                                            DataRate::KilobitsPerSec(6500)));
}

TEST(ScreamTest, MaybeTest(LinkCapacity600KbpsRtt20msNoEcn)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params;
  params.test_duration = TimeDelta::Seconds(30);
  params.callee_to_caller_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(600), TimeDelta::Millis(10));
  params.caller_to_callee_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(600), TimeDelta::Millis(10));

  SendMediaTestResult result = SendMedia(std::move(params), s);
  EXPECT_THAT(result.caller().subspan(1), Each(AvailableSendBitrateIsBetween(
                                              DataRate::KilobitsPerSec(200),
                                              DataRate::KilobitsPerSec(900))));
}

TEST(ScreamTest, MaybeTest(LinkCapacity600KbpsRtt100msEcn)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params;
  params.callee_to_caller_path =
      CreateNetworkPath(s, /*use_dual_pi= */ true,
                        DataRate::KilobitsPerSec(600), TimeDelta::Millis(50));
  params.caller_to_callee_path =
      CreateNetworkPath(s, /*use_dual_pi= */ true,
                        DataRate::KilobitsPerSec(600), TimeDelta::Millis(50));

  SendMediaTestResult result = SendMedia(std::move(params), s);
  // Allow rampup to take 3s.
  EXPECT_THAT(result.caller().subspan(3), Each(AvailableSendBitrateIsBetween(
                                              DataRate::KilobitsPerSec(370),
                                              DataRate::KilobitsPerSec(620))));
}

TEST(ScreamTest, MaybeTest(LinkCapacity1000KbpsRtt100msEcn)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params;
  params.callee_to_caller_path =
      CreateNetworkPath(s, /*use_dual_pi= */ true,
                        DataRate::KilobitsPerSec(1000), TimeDelta::Millis(50));
  params.caller_to_callee_path =
      CreateNetworkPath(s, /*use_dual_pi= */ true,
                        DataRate::KilobitsPerSec(1000), TimeDelta::Millis(50));

  SendMediaTestResult result = SendMedia(std::move(params), s);
  // Ignore result first 2s since ramp up is rather slow at higher RTT.
  ASSERT_GT(result.caller().subspan(2).size(), 0u);
  EXPECT_THAT(result.caller().subspan(2), Each(AvailableSendBitrateIsBetween(
                                              DataRate::KilobitsPerSec(650),
                                              DataRate::KilobitsPerSec(950))));
}

void TestVaryingCapacity1000To500KbpsRtt100ms(bool use_ecn) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params;
  params.test_duration = TimeDelta::Seconds(12);
  params.callee_to_caller_path = CreateNetworkPathWithVaryingCapacity(
      s, /*link_capacity_1=*/DataRate::KilobitsPerSec(1000),
      /*link_capacity_2=*/DataRate::KilobitsPerSec(500),
      /*interval=*/TimeDelta::Seconds(4),
      /*one_way_delay=*/TimeDelta::Millis(50), use_ecn);
  params.caller_to_callee_path = CreateNetworkPathWithVaryingCapacity(
      s, /*link_capacity_1=*/DataRate::KilobitsPerSec(1000),
      /*link_capacity_2=*/DataRate::KilobitsPerSec(500),
      /*interval=*/TimeDelta::Seconds(4),
      /*one_way_delay=*/TimeDelta::Millis(50), use_ecn);

  SendMediaTestResult result = SendMedia(std::move(params), s);
  ASSERT_GE(result.caller().size(), 12u);
  // Before drop (seconds 2-3): steady at 1000 kbps.
  EXPECT_THAT(
      result.caller().subspan(2, 2),
      Each(AllOf(AvailableSendBitrateIsBetween(DataRate::KilobitsPerSec(600),
                                               DataRate::KilobitsPerSec(1100)),
                 CurrentRoundTripTimeIsBetween(TimeDelta::Millis(90),
                                               TimeDelta::Millis(200)))));
  // During drop (seconds 6-7): adapted down towards 500 kbps, with queue
  // drained by second 7.
  EXPECT_THAT(
      result.caller().subspan(6, 2),
      Each(AvailableSendBitrateIsBetween(DataRate::KilobitsPerSec(300),
                                         DataRate::KilobitsPerSec(700))));
  EXPECT_THAT(result.caller().subspan(7, 1),
              Each(CurrentRoundTripTimeIsBetween(TimeDelta::Millis(90),
                                                 TimeDelta::Millis(200))));
  // After recovery (seconds 10-11): recovered back towards 1000 kbps.
  EXPECT_THAT(
      result.caller().subspan(10, 2),
      Each(AllOf(AvailableSendBitrateIsBetween(DataRate::KilobitsPerSec(600),
                                               DataRate::KilobitsPerSec(1100)),
                 CurrentRoundTripTimeIsBetween(TimeDelta::Millis(90),
                                               TimeDelta::Millis(200)))));
}

TEST(ScreamTest, MaybeTest(LinkCapacity1000To500KbpsRtt100msEcn)) {
  TestVaryingCapacity1000To500KbpsRtt100ms(/*use_ecn=*/true);
}

TEST(ScreamTest, MaybeTest(LinkCapacity1000To500KbpsRtt100msNoEcn)) {
  TestVaryingCapacity1000To500KbpsRtt100ms(/*use_ecn=*/false);
}

TEST(ScreamTest, MaybeTest(LinkCapacity1500KbpsRtt30msNoEcn)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params;
  params.test_duration = TimeDelta::Seconds(30);
  params.callee_to_caller_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(1500), TimeDelta::Millis(15));
  params.caller_to_callee_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(1500), TimeDelta::Millis(15));

  SendMediaTestResult result = SendMedia(std::move(params), s);
  EXPECT_THAT(result.caller().subspan(1), Each(AvailableSendBitrateIsBetween(
                                              DataRate::KilobitsPerSec(1100),
                                              DataRate::KilobitsPerSec(2200))));
}

TEST(ScreamTest, MaybeTest(LinkCapacity2MbpsRtt50msNoEcn)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params;
  params.test_duration = TimeDelta::Seconds(30);
  params.callee_to_caller_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(2000), TimeDelta::Millis(25));
  params.caller_to_callee_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(2000), TimeDelta::Millis(25));

  SendMediaTestResult result = SendMedia(std::move(params), s);
  EXPECT_THAT(result.caller().subspan(1), Each(AvailableSendBitrateIsBetween(
                                              DataRate::KilobitsPerSec(1300),
                                              DataRate::KilobitsPerSec(2500))));
}

TEST(ScreamTest, MaybeTest(LinkCapacity2MbpsRtt50msEcn)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params;
  params.test_duration = TimeDelta::Seconds(30);
  params.callee_to_caller_path =
      CreateNetworkPath(s, /*use_dual_pi= */ true,
                        DataRate::KilobitsPerSec(2000), TimeDelta::Millis(25));
  params.caller_to_callee_path =
      CreateNetworkPath(s, /*use_dual_pi= */ true,
                        DataRate::KilobitsPerSec(2000), TimeDelta::Millis(25));

  SendMediaTestResult result = SendMedia(std::move(params), s);
  EXPECT_THAT(result.caller().subspan(3), Each(AvailableSendBitrateIsBetween(
                                              DataRate::KilobitsPerSec(1400),
                                              DataRate::KilobitsPerSec(2100))));
}

TEST(ScreamTest, MaybeTest(LinkCapacity2MbpsRtt50msNoEcnWithGoogCC)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params;
  params.test_duration = TimeDelta::Seconds(30);
  params.callee_to_caller_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(2000), TimeDelta::Millis(25));
  params.caller_to_callee_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(2000), TimeDelta::Millis(25));
  params.field_trials = {
      {"WebRTC-RFC8888CongestionControlFeedback", "Enabled,offer:true"},
  };

  SendMediaTestResult result = SendMedia(std::move(params), s);
  EXPECT_THAT(result.caller_stats.back(),
              AvailableSendBitrateIsBetween(DataRate::KilobitsPerSec(1000),
                                            DataRate::KilobitsPerSec(2600)));
}

TEST(ScreamTest, MaybeTest(LinkCapacity4MbpsRtt50ms10MsDelayStdDev)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  NetworkEmulationManager::SimulatedNetworkNode::Builder network_builder(
      s.net());
  network_builder.delay_ms(25);
  network_builder.delay_standard_deviation_ms(10);
  network_builder.capacity_Mbps(4);
  SendMediaTestParams params;
  params.test_duration = TimeDelta::Seconds(30);
  params.callee_to_caller_path =
      CreateNetworkPath(network_builder, /*use_dual_pi= */ false);
  params.caller_to_callee_path =
      CreateNetworkPath(network_builder, /*use_dual_pi= */ false);

  SendMediaTestResult result = SendMedia(std::move(params), s);
  EXPECT_THAT(result.caller().subspan(1), Each(AvailableSendBitrateIsBetween(
                                              DataRate::KilobitsPerSec(700),
                                              DataRate::KilobitsPerSec(4100))));
}

TEST(ScreamTest, MaybeTest(LinkCapacity2MbpsRtt50msNoEcnWithTwcc)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params;
  params.test_duration = TimeDelta::Seconds(30);
  params.callee_to_caller_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(2000), TimeDelta::Millis(25));
  params.caller_to_callee_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(2000), TimeDelta::Millis(25));
  params.field_trials = {
      {"WebRTC-RFC8888CongestionControlFeedback", "Disabled"},
      {"WebRTC-Bwe-ScreamV2", "mode:always"},
  };

  SendMediaTestResult result = SendMedia(std::move(params), s);
  // BWE rampup is quite slow since feedback is only sent every 90ms
  // approximately.
  EXPECT_THAT(result.caller().subspan(5), Each(AvailableSendBitrateIsBetween(
                                              DataRate::KilobitsPerSec(700),
                                              DataRate::KilobitsPerSec(2600))));
}

TEST(ScreamTest, MaybeTest(CallerPauseSendingVideoIfFeedbackNotReceived)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params;
  params.callee_to_caller_path = CreateNetworkPathWithPauseBetween3sAnd6s(s);
  params.caller_to_callee_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(600), TimeDelta::Millis(50));

  Timestamp start_time = s.net()->Now();
  SendMediaTestResult result = SendMedia(std::move(params), s);

  std::optional<scoped_refptr<const RTCStatsReport>> report_after_4s =
      GetFirstReportAtOrAfter(start_time + TimeDelta::Seconds(4),
                              result.caller_stats);
  std::optional<scoped_refptr<const RTCStatsReport>> report_after_5s =
      GetFirstReportAtOrAfter(start_time + TimeDelta::Seconds(5),
                              result.caller_stats);
  ASSERT_TRUE(report_after_4s.has_value());
  ASSERT_TRUE(report_after_5s.has_value());
  ASSERT_GT(GetPacketsSent(report_after_4s.value(), "video"), 0);
  // Audio not paused.
  EXPECT_LT(GetPacketsSent(report_after_4s.value()),
            GetPacketsSent(report_after_5s.value()));
  // video paused.
  EXPECT_EQ(GetPacketsSent(report_after_4s.value(), "video"),
            GetPacketsSent(report_after_5s.value(), "video"));
  // video resumed.
  EXPECT_LT(GetPacketsSent(report_after_4s.value(), "video"),
            GetPacketsSent(result.caller_stats.back(), "video"));

  std::optional<scoped_refptr<const RTCStatsReport>> report_after_6s =
      GetFirstReportAtOrAfter(start_time + TimeDelta::Seconds(6),
                              result.caller_stats);
  std::optional<scoped_refptr<const RTCStatsReport>> report_after_7s =
      GetFirstReportAtOrAfter(start_time + TimeDelta::Seconds(7),
                              result.caller_stats);
  std::optional<scoped_refptr<const RTCStatsReport>> report_after_8s =
      GetFirstReportAtOrAfter(start_time + TimeDelta::Seconds(8),
                              result.caller_stats);
  ASSERT_TRUE(report_after_6s.has_value());
  ASSERT_TRUE(report_after_7s.has_value());
  ASSERT_TRUE(report_after_8s.has_value());

  // Video encoder target rate is set to zero while congested and pacer queue
  // delay exceeds max pushback delay.
  EXPECT_EQ(GetAvailableSendBitrate(report_after_6s.value()), DataRate::Zero());

  // Because the encoder was paused, the pacer queue was bounded and quickly
  // drained upon resumption (~6s). By 8s, average pacing delay is back to
  // normal (< 50 ms).
  EXPECT_LT(
      GetAveragePacingDelay(report_after_8s.value(), report_after_7s.value()),
      TimeDelta::Millis(50));

  // Target rate is within reason at the end of the call.
  EXPECT_GT(GetAvailableSendBitrate(result.caller_stats.back()),
            DataRate::KilobitsPerSec(400));
  EXPECT_LT(GetAvailableSendBitrate(result.caller_stats.back()),
            DataRate::KilobitsPerSec(800));
}

TEST(ScreamTest,
     MaybeTest(CallerResetQueueDelayEstimateAfterIncreasedFixedDelay)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params{.test_duration = TimeDelta::Seconds(35)};
  params.caller_to_callee_path = CreateNetworkPath1MbitDelayIncreaseAfter3S(s);
  params.callee_to_caller_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(2000), TimeDelta::Millis(10));
  SendMediaTestResult result = SendMedia(std::move(params), s);

  // After the increased delay, BWE should drop
  EXPECT_LT(GetAvailableSendBitrate(result.caller_stats[5]),
            DataRate::KilobitsPerSec(400));

  // But have recovered by the end of the test.
  EXPECT_THAT(result.caller_stats.back(),
              AvailableSendBitrateIsBetween(DataRate::KilobitsPerSec(600),
                                            DataRate::KilobitsPerSec(1200)));
}

TEST(ScreamTest, MaybeTest(ScreencastSlideChange2Mbit50msRttNoEcn)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params{.test_duration = TimeDelta::Seconds(20)};
  params.caller_to_callee_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(2000), TimeDelta::Millis(25));
  params.caller_to_callee_path = params.callee_to_caller_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(2000), TimeDelta::Millis(25));
  params.caller_video_conf = {
      .generator = {.image_slides =
                        test::FrameGeneratorCapturerConfig::ImageSlides{
                            .change_interval = TimeDelta::Seconds(5)}}};

  SendMediaTestResult result = SendMedia(std::move(params), s);

  // Ignore estimate during rampup.
  EXPECT_THAT(result.caller().subspan(1), Each(AvailableSendBitrateIsBetween(
                                              DataRate::KilobitsPerSec(1200),
                                              DataRate::KilobitsPerSec(2500))));
}

TEST(ScreamTest, MaybeTest(ScreencastSlideChange500Kbit30msRttNoEcn)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params{.test_duration = TimeDelta::Seconds(20)};
  params.caller_to_callee_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(500), TimeDelta::Millis(15));
  params.caller_to_callee_path = params.callee_to_caller_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(500), TimeDelta::Millis(15));
  params.caller_video_conf = {
      .generator = {.image_slides =
                        test::FrameGeneratorCapturerConfig::ImageSlides{
                            .change_interval = TimeDelta::Seconds(3)}}};

  SendMediaTestResult result = SendMedia(std::move(params), s);

  // Ignore estimate during rampup.
  EXPECT_THAT(result.caller().subspan(1), Each(AvailableSendBitrateIsBetween(
                                              DataRate::KilobitsPerSec(200),
                                              DataRate::KilobitsPerSec(700))));
}

TEST(ScreamTest, MaybeTest(ScreencastSlideChangeRepeatedDelaySpikes)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params{.test_duration = TimeDelta::Seconds(20)};
  params.caller_to_callee_path = CreateNetworkPathWithRepeatedPause(
      s, DataRate::KilobitsPerSec(4000), TimeDelta::Millis(500),
      TimeDelta::Seconds(4));
  params.callee_to_caller_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(2000), TimeDelta::Millis(25));
  params.caller_video_conf = {
      .generator = {.image_slides =
                        test::FrameGeneratorCapturerConfig::ImageSlides{
                            .change_interval = TimeDelta::Seconds(5)}}};
  SendMediaTestResult result = SendMedia(std::move(params), s);

  EXPECT_THAT(result.caller(), Each(AvailableSendBitrateIsBetween(
                                   DataRate::KilobitsPerSec(500),
                                   DataRate::KilobitsPerSec(4300))));
}

TEST(ScreamTest, MaybeTest(LinkCapacity5MbitRepeatedDelaySpikesNoEcn)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params{.test_duration = TimeDelta::Seconds(20)};
  params.caller_to_callee_path = CreateNetworkPathWithRepeatedPause(
      s, DataRate::KilobitsPerSec(5000), TimeDelta::Millis(100),
      TimeDelta::Millis(200));
  params.callee_to_caller_path = CreateNetworkPathWithRepeatedPause(
      s, DataRate::KilobitsPerSec(5000), TimeDelta::Millis(100),
      TimeDelta::Millis(200));
  SendMediaTestResult result = SendMedia(std::move(params), s);
  EXPECT_THAT(result.caller().subspan(1), Each(AvailableSendBitrateIsBetween(
                                              DataRate::KilobitsPerSec(1200),
                                              DataRate::KilobitsPerSec(5000))));
}

TEST(ScreamTest, MaybeTest(LinkCapacity2MbitRepeatedDelaySpikesNoEcn)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params{.test_duration = TimeDelta::Seconds(20),
                             .stats_interval = TimeDelta::Millis(200)};
  params.caller_to_callee_path = CreateNetworkPathWithRepeatedPause(
      s, DataRate::KilobitsPerSec(2000), TimeDelta::Millis(100),
      TimeDelta::Millis(200));
  params.callee_to_caller_path = CreateNetworkPathWithRepeatedPause(
      s, DataRate::KilobitsPerSec(2000), TimeDelta::Millis(100),
      TimeDelta::Millis(200));
  SendMediaTestResult result = SendMedia(std::move(params), s);
  // Ignore BWE the first second.
  EXPECT_THAT(result.caller().subspan(5), Each(AvailableSendBitrateIsBetween(
                                              DataRate::KilobitsPerSec(550),
                                              DataRate::KilobitsPerSec(2000))));
  // Ensure average pacing delay between two reports is below 70ms.
  for (size_t i = 1; i < result.caller_stats.size(); ++i) {
    EXPECT_THAT(result.caller_stats[i],
                AveragePacingDelayIsBelow(TimeDelta::Millis(70),
                                          result.caller_stats[i - 1]));
  }
}

TEST(ScreamTest, MaybeTest(PacerDelayIsAcceptableAfterSingleDelaySpike)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params{.test_duration = TimeDelta::Seconds(10),
                             .stats_interval = TimeDelta::Millis(200)};
  params.caller_to_callee_path =
      CreateNetworkPathWithPause(s, DataRate::KilobitsPerSec(1000),
                                 TimeDelta::Seconds(5), TimeDelta::Millis(500));
  params.callee_to_caller_path =
      CreateNetworkPathWithPause(s, DataRate::KilobitsPerSec(1000),
                                 TimeDelta::Seconds(5), TimeDelta::Millis(500));
  SendMediaTestResult result = SendMedia(std::move(params), s);
  // Ensure average pacing delay between two reports stays below 1000ms.
  for (size_t i = 1; i < result.caller_stats.size(); ++i) {
    EXPECT_THAT(result.caller_stats[i],
                AveragePacingDelayIsBelow(TimeDelta::Millis(1000),
                                          result.caller_stats[i - 1]));
  }
}

TEST(ScreamTest, MaybeTest(RampupFastOnLinkCapacity50Mbit20MsRttNoEcn)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params{.test_duration = TimeDelta::Seconds(1),
                             .stats_interval = TimeDelta::Millis(100)};
  params.caller_to_callee_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(50000), TimeDelta::Millis(10));
  params.callee_to_caller_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(50000), TimeDelta::Millis(10));
  SendMediaTestResult result = SendMedia(std::move(params), s);
  // After 400ms.
  EXPECT_THAT(result.caller_stats[3],
              AvailableSendBitrateIsBetween(DataRate::KilobitsPerSec(1200),
                                            DataRate::KilobitsPerSec(2500)));
}

TEST(ScreamTest, MaybeTest(LinkCapacity100Kbit50msRttNoEcn)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params;
  params.caller_to_callee_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(100), TimeDelta::Millis(25));
  params.callee_to_caller_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(100), TimeDelta::Millis(25));
  SendMediaTestResult result = SendMedia(std::move(params), s);
  // In the beginning, the initial video frame burst at 100 kbps link capacity
  // can build up a pacer queue > 500 ms while congested, causing the encoder to
  // temporarily pause (target rate = 0). Once the pacer drains (after ~3.5s),
  // bitrate stabilizes between 10 kbps and 150 kbps.
  EXPECT_THAT(result.caller().subspan(4), Each(AvailableSendBitrateIsBetween(
                                              DataRate::KilobitsPerSec(10),
                                              DataRate::KilobitsPerSec(150))));
}

TEST(ScreamTest, MaybeTest(LinkCapacity1MbitRtt50msWithShortQueuesNoEcn)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params;
  NetworkEmulationManager::SimulatedNetworkNode::Builder network_builder =
      s.net()->NodeBuilder().capacity_Mbps(1).delay_ms(25);
  params.callee_to_caller_path =
      CreateNetworkPath(network_builder, /*use_dual_pi= */ false);
  params.caller_to_callee_path = CreateNetworkPath(
      network_builder.packet_queue_length(3), /*use_dual_pi= */ false);

  SendMediaTestResult result = SendMedia(std::move(params), s);
  EXPECT_THAT(
      GetPacketsLost(result.callee_stats.back()) /
          static_cast<double>(GetPacketsSent(result.caller_stats.back())),
      Lt(0.07));

  EXPECT_THAT(result.caller().subspan(1), Each(AvailableSendBitrateIsBetween(
                                              DataRate::KilobitsPerSec(200),
                                              DataRate::KilobitsPerSec(1100))));
}

TEST(ScreamTest,
     MaybeTest(LinkCapacity1MbitRtt50msWith10PercentRandomLossNoEcn)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params;
  NetworkEmulationManager::SimulatedNetworkNode::Builder network_builder =
      s.net()->NodeBuilder().capacity_Mbps(1).delay_ms(25);
  params.callee_to_caller_path =
      CreateNetworkPath(network_builder, /*use_dual_pi= */ false);
  params.caller_to_callee_path =
      CreateNetworkPath(network_builder.loss(0.1), /*use_dual_pi= */ false);

  SendMediaTestResult result = SendMedia(std::move(params), s);

  ASSERT_GE(GetPacketsLost(result.callee_stats.back()),
            0.05 * GetPacketsSent(result.caller_stats.back()));

  // Ignore estimate during rampup.
  EXPECT_THAT(result.caller().subspan(1), Each(AvailableSendBitrateIsBetween(
                                              DataRate::KilobitsPerSec(100),
                                              DataRate::KilobitsPerSec(1100))));
}

TEST(ScreamTest, MaybeTest(LowBweOnLinkWith5PercentUniformLoss)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params;
  NetworkEmulationManager::SimulatedNetworkNode::Builder network_builder =
      s.net()->NodeBuilder().capacity_Mbps(3).delay_ms(40);
  params.callee_to_caller_path =
      CreateNetworkPath(network_builder, /*use_dual_pi= */ false);
  params.caller_to_callee_path =
      CreateNetworkPath(network_builder.loss(0.05), /*use_dual_pi= */ false);

  SendMediaTestResult result = SendMedia(std::move(params), s);

  ASSERT_GE(GetPacketsLost(result.callee_stats.back()),
            0.02 * GetPacketsSent(result.caller_stats.back()));

  // Ignore estimate during rampup.
  ASSERT_GT(result.caller().subspan(2).size(), 0u);
  EXPECT_THAT(result.caller().subspan(2), Each(AvailableSendBitrateIsBetween(
                                              DataRate::KilobitsPerSec(100),
                                              DataRate::KilobitsPerSec(1800))));
}

TEST(ScreamTest, MaybeTest(Ignores1PercentUniformLossOnLinkWith80msRtt)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params;
  NetworkEmulationManager::SimulatedNetworkNode::Builder network_builder =
      s.net()->NodeBuilder().capacity_Mbps(3).delay_ms(40);
  params.callee_to_caller_path =
      CreateNetworkPath(network_builder, /*use_dual_pi= */ false);
  params.caller_to_callee_path =
      CreateNetworkPath(network_builder.loss(0.01), /*use_dual_pi= */ false);

  SendMediaTestResult result = SendMedia(std::move(params), s);

  ASSERT_GE(GetPacketsLost(result.callee_stats.back()),
            0.005 * GetPacketsSent(result.caller_stats.back()));

  // Ignore estimate during rampup.
  ASSERT_GT(result.caller().subspan(2).size(), 0u);
  EXPECT_THAT(result.caller().subspan(2), Each(AvailableSendBitrateIsBetween(
                                              DataRate::KilobitsPerSec(1000),
                                              DataRate::KilobitsPerSec(3000))));
}

TEST(ScreamTest, MaybeTest(IgnoresPacketReorderingOnLinkWith80msRtt)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params;
  NetworkEmulationManager::SimulatedNetworkNode::Builder network_builder =
      s.net()->NodeBuilder().capacity_Mbps(3).delay_ms(40);
  params.callee_to_caller_path =
      CreateNetworkPath(network_builder, /*use_dual_pi= */ false);
  params.caller_to_callee_path = CreateNetworkPath(
      network_builder.allow_reordering().delay_standard_deviation_ms(5),
      /*use_dual_pi= */ false);

  SendMediaTestResult result = SendMedia(std::move(params), s);

  // Ignore estimate during rampup.
  ASSERT_GT(result.caller().subspan(2).size(), 0u);
  EXPECT_THAT(result.caller().subspan(2), Each(AvailableSendBitrateIsBetween(
                                              DataRate::KilobitsPerSec(1000),
                                              DataRate::KilobitsPerSec(3000))));
}

TEST(ScreamTest, MaybeTest(ReturnLinkWithBurstLoss)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params{.test_duration = TimeDelta::Seconds(20)};
  NetworkEmulationManager::SimulatedNetworkNode::Builder network_builder =
      s.net()->NodeBuilder().capacity_Mbps(1).delay_ms(25);
  params.caller_to_callee_path =
      CreateNetworkPath(network_builder, /*use_dual_pi= */ false);

  params.callee_to_caller_path =
      CreateNetworkPath(network_builder.loss(0.2).avg_burst_loss_length(3),
                        /*use_dual_pi= */ false);

  SendMediaTestResult result = SendMedia(std::move(params), s);

  // Audio packets are sent even if congestion window is full and ensures
  // feedback is eventually received even if feedback packets are lost.
  EXPECT_GT(GetPacketsSent(result.caller_stats.back()),
            GetPacketsSent(result.caller_stats[5]));

  ASSERT_GT(result.caller().subspan(1).size(), 0u);
  EXPECT_THAT(result.caller().subspan(1), Each(AvailableSendBitrateIsBetween(
                                              DataRate::KilobitsPerSec(300),
                                              DataRate::KilobitsPerSec(1300))));
}

TEST(ScreamTest, MaybeTest(SendVideoOnlyReturnLinkWithBurstLoss)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params;
  params.send_audio = false;
  NetworkEmulationManager::SimulatedNetworkNode::Builder network_builder =
      s.net()->NodeBuilder().capacity_Mbps(1).delay_ms(25);
  params.caller_to_callee_path =
      CreateNetworkPath(network_builder, /*use_dual_pi= */ false);
  params.callee_to_caller_path =
      CreateNetworkPath(network_builder.loss(0.2).avg_burst_loss_length(3),
                        /*use_dual_pi= */ false);

  SendMediaTestResult result = SendMedia(std::move(params), s);

  // Keep alive packets ensure feedback is eventually received even if feedback
  // packets are lost. When feedback is not received, the pacer queue builds up
  // and the video encoder is paused (BWE drops to zero) to prevent queue bloat.
  EXPECT_GT(GetPacketsSent(result.caller_stats.back()),
            GetPacketsSent(result.caller_stats[5]));

  int zero_bwe_count = 0;
  for (const auto& report : result.caller().subspan(1)) {
    if (GetAvailableSendBitrate(report) == DataRate::Zero()) {
      ++zero_bwe_count;
    }
  }
  // BWE can drop to zero when feedback is lost and the encoder is paused, but
  // should not remain zero throughout the call.
  EXPECT_LT(zero_bwe_count,
            static_cast<int>(result.caller().subspan(1).size()));

  ASSERT_GT(result.caller().subspan(1).size(), 0u);
  EXPECT_THAT(
      result.caller().subspan(1),
      Each(AvailableSendBitrateIsZeroOrBetween(
          DataRate::KilobitsPerSec(300), DataRate::KilobitsPerSec(1200))));
}

// Test that Scream adapt to a link with traffic policing on the network path
// from caller to calee.
TEST(ScreamTest, MaybeTest(LinkCapacity5MbitPolicedTo256Kbit)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params;
  NetworkEmulationManager::SimulatedNetworkNode::Builder network_builder =
      s.net()->NodeBuilder().capacity_Mbps(5).delay_ms(25);
  params.caller_to_callee_path = {
      s.net()->NodeBuilder().capacity_Mbps(5).delay_ms(25).Build().node,
      TokenBucketNetworkBehaviorNodeBuilder(s.net())
          .burst(DataSize::Bytes(16384))  // 0.5s at 256kbps.
          .rate(DataRate::KilobitsPerSec(256))
          .Build()};
  params.callee_to_caller_path =
      CreateNetworkPath(network_builder, /*use_dual_pi= */ false);

  SendMediaTestResult result = SendMedia(std::move(params), s);
  EXPECT_THAT(
      GetPacketsLost(result.callee_stats.back()) /
          static_cast<double>(GetPacketsSent(result.caller_stats.back())),
      AllOf(Lt(0.11), Gt(0.01)));

  EXPECT_THAT(result.caller().subspan(1), Each(AvailableSendBitrateIsBetween(
                                              DataRate::KilobitsPerSec(150),
                                              DataRate::KilobitsPerSec(900))));
}

TEST(ScreamTest, MaybeTest(LinkCapacity5MbitWithCrossTrafficNoEcn)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params;
  params.test_duration = TimeDelta::Seconds(30);
  params.caller_to_callee_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(5000), TimeDelta::Millis(25));
  params.callee_to_caller_path =
      CreateNetworkPath(s, /*use_dual_pi= */ false,
                        DataRate::KilobitsPerSec(5000), TimeDelta::Millis(25));

  // Simulate a file upload on the path from caller to calee.
  webrtc::TcpMessageRoute* tcp_route = s.net()->CreateTcpRoute(
      s.net()->CreateRoute({params.caller_to_callee_path}),
      s.net()->CreateRoute({params.callee_to_caller_path}));
  Timestamp start_time = s.net()->Now();
  Timestamp tcp_message_delivered_time = Timestamp::MinusInfinity();
  s.net()->time_controller()->GetMainThread()->PostDelayedTask(
      [&]() {
        tcp_route->SendMessage(
            /*size=*/2'000'000,
            /*on_received=*/[&tcp_message_delivered_time, &s]() {
              tcp_message_delivered_time = s.net()->Now();
            });
      },
      TimeDelta::Seconds(3));

  SendMediaTestResult result = SendMedia(std::move(params), s);
  ASSERT_TRUE(tcp_message_delivered_time.IsFinite());

  int index_where_available_bitrate_should_have_recovered =
      (tcp_message_delivered_time - start_time).seconds<int>() + 10;
  EXPECT_THAT(
      result.caller().subspan(
          index_where_available_bitrate_should_have_recovered),
      Each(AvailableSendBitrateIsBetween(DataRate::KilobitsPerSec(1500),
                                         DataRate::KilobitsPerSec(5000))));
}

TEST(ScreamTest,
     MaybeTest(AudioOnlyBothDirectionsLinkCapacity40To90KbpsRtt200ms)) {
  PeerScenario s(*testing::UnitTest::GetInstance()->current_test_info());
  SendMediaTestParams params;
  params.send_audio = true;
  params.send_video = false;
  params.adaptive_ptime = true;
  params.send_in_two_directions = true;
  params.test_duration = TimeDelta::Seconds(20);
  params.caller_to_callee_path = CreateNetworkPathWithVaryingCapacity(
      s, DataRate::KilobitsPerSec(30), DataRate::KilobitsPerSec(150),
      TimeDelta::Millis(800), TimeDelta::Millis(100));
  params.callee_to_caller_path = CreateNetworkPathWithVaryingCapacity(
      s, DataRate::KilobitsPerSec(30), DataRate::KilobitsPerSec(150),
      TimeDelta::Millis(800), TimeDelta::Millis(100));

  SendMediaTestResult result = SendMedia(std::move(params), s);
  ASSERT_GT(result.caller().subspan(2).size(), 0u);
  EXPECT_THAT(result.caller().subspan(2),
              Each(CurrentRoundTripTimeIsBetween(TimeDelta::Millis(180),
                                                 TimeDelta::Millis(600))));
  ASSERT_GT(result.callee().subspan(2).size(), 0u);
  EXPECT_THAT(result.callee().subspan(2),
              Each(CurrentRoundTripTimeIsBetween(TimeDelta::Millis(180),
                                                 TimeDelta::Millis(600))));
  EXPECT_THAT(result.caller().subspan(2),
              Each(AvailableSendBitrateIsBetween(
                  DataRate::KilobitsPerSec(5), DataRate::KilobitsPerSec(60))));
  EXPECT_THAT(result.callee().subspan(2),
              Each(AvailableSendBitrateIsBetween(
                  DataRate::KilobitsPerSec(5), DataRate::KilobitsPerSec(60))));
}
}  // namespace
}  // namespace webrtc
