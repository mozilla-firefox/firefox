/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/audio_options.h"
#include "api/make_ref_counted.h"
#include "api/rtc_error.h"
#include "api/stats/rtcstats_objects.h"
#include "api/test/simulated_network.h"
#include "api/units/data_rate.h"
#include "api/units/time_delta.h"
#include "pc/test/mock_peer_connection_observers.h"
#include "test/gtest.h"
#include "test/peer_scenario/peer_scenario.h"
#include "test/peer_scenario/peer_scenario_client.h"

namespace webrtc {
namespace test {

// TODO(terelius): Use fake encoder and enable on Android once
// https://bugs.chromium.org/p/webrtc/issues/detail?id=11408 is fixed.
#if defined(WEBRTC_ANDROID)
#define MAYBE_NoBweChangeFromVideoUnmute DISABLED_NoBweChangeFromVideoUnmute
#else
#define MAYBE_NoBweChangeFromVideoUnmute NoBweChangeFromVideoUnmute
#endif
TEST(GoogCcPeerScenarioTest, MAYBE_NoBweChangeFromVideoUnmute) {
  PeerScenarioClient::Config config;
  // If transport wide sequence numbers are used for audio, and the call
  // switches from audio only to video only, there will be a sharp change in
  // packets sizes. This will create a change in propagation time which might be
  // detected as an overuse. Using separate overuse detectors for audio and
  // video avoids the issue.
  config.field_trials.Set("WebRTC-Audio-AlrProbing", "Disabled");
  config.field_trials.Set(
      "WebRTC-Bwe-SeparateAudioPackets",
      "enabled:true,packet_threshold:15,time_threshold:1000ms");

  PeerScenario s(*test_info_);
  auto* caller = s.CreateClient(config);
  auto* callee = s.CreateClient(config);

  BuiltInNetworkBehaviorConfig net_conf;
  net_conf.link_capacity = DataRate::KilobitsPerSec(350);
  net_conf.queue_delay_ms = 50;
  auto send_node = s.net()->CreateEmulatedNode(net_conf);
  auto ret_node = s.net()->CreateEmulatedNode(net_conf);

  PeerScenarioClient::VideoSendTrackConfig video_conf;
  video_conf.generator.squares_video->framerate = 15;
  auto video = caller->CreateVideo("VIDEO", video_conf);
  auto audio = caller->CreateAudio("AUDIO", AudioOptions());

  // Start ICE and exchange SDP.
  s.SimpleConnection(caller, callee, {send_node}, {ret_node});

  // Limit the encoder bitrate to ensure that there are no actual BWE overuses.
  ASSERT_EQ(caller->pc()->GetSenders().size(), 2u);  // 2 senders.
  int num_video_streams = 0;
  for (auto& rtp_sender : caller->pc()->GetSenders()) {
    auto parameters = rtp_sender->GetParameters();
    ASSERT_EQ(parameters.encodings.size(), 1u);  // 1 stream per sender.
    for (auto& encoding_parameters : parameters.encodings) {
      if (encoding_parameters.ssrc == video.sender->ssrc()) {
        num_video_streams++;
        encoding_parameters.max_bitrate_bps = 220000;
        encoding_parameters.max_framerate = 15;
      }
    }
    rtp_sender->SetParameters(parameters);
  }
  ASSERT_EQ(num_video_streams, 1);  // Exactly 1 video stream.

  auto get_bwe = [&] {
    auto callback = make_ref_counted<MockRTCStatsCollectorCallback>();
    caller->pc()->GetStats(callback.get());
    s.net()->time_controller()->Wait([&] { return callback->called(); });
    auto stats =
        callback->report()->GetStatsOfType<RTCIceCandidatePairStats>()[0];
    return DataRate::BitsPerSec(*stats->available_outgoing_bitrate);
  };

  s.ProcessMessages(TimeDelta::Seconds(15));
  const DataRate initial_bwe = get_bwe();
  EXPECT_GE(initial_bwe, DataRate::KilobitsPerSec(300));

  // 10 seconds audio only. Bandwidth should not drop.
  video.capturer->Stop();
  s.ProcessMessages(TimeDelta::Seconds(10));
  EXPECT_GE(get_bwe(), initial_bwe);

  // Resume video but stop audio. Bandwidth should not drop.
  video.capturer->Start();
  RTCError status = caller->pc()->RemoveTrackOrError(audio.sender);
  ASSERT_TRUE(status.ok());
  audio.track->set_enabled(false);
  for (int i = 0; i < 10; i++) {
    s.ProcessMessages(TimeDelta::Seconds(1));
    EXPECT_GE(get_bwe(), initial_bwe);
  }

  caller->pc()->Close();
  callee->pc()->Close();
}

}  // namespace test
}  // namespace webrtc
