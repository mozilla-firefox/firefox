/*
 *  Copyright 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// Integration tests for PeerConnection.
// These tests exercise a full stack over a simulated network.
//
// NOTE: If your test takes a while (guideline: more than 5 seconds),
// do NOT add it here, but instead add it to the file
// slow_peer_connection_integrationtest.cc

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "api/candidate.h"
#include "api/crypto/crypto_options.h"
#include "api/data_channel_interface.h"
#include "api/dtmf_sender_interface.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/media_types.h"
#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "api/rtc_event_log/rtc_event.h"
#include "api/rtc_event_log/rtc_event_log.h"
#include "api/rtp_parameters.h"
#include "api/rtp_receiver_interface.h"
#include "api/rtp_sender_interface.h"
#include "api/rtp_transceiver_direction.h"
#include "api/rtp_transceiver_interface.h"
#include "api/scoped_refptr.h"
#include "api/stats/rtc_stats_report.h"
#include "api/stats/rtcstats_objects.h"
#include "api/test/mock_async_dns_resolver.h"
#include "api/test/mock_encoder_selector.h"
#include "api/test/rtc_error_matchers.h"
#include "api/transport/bitrate_settings.h"
#include "api/transport/rtp/rtp_source.h"
#include "api/uma_metrics.h"
#include "api/units/data_rate.h"
#include "api/units/time_delta.h"
#include "api/video/video_rotation.h"
#include "api/video_codecs/sdp_video_format.h"
#include "logging/rtc_event_log/fake_rtc_event_log.h"
#include "logging/rtc_event_log/fake_rtc_event_log_factory.h"
#include "media/base/codec.h"
#include "media/base/media_constants.h"
#include "media/base/stream_params.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/port.h"
#include "p2p/base/port_allocator.h"
#include "p2p/base/transport_description.h"
#include "p2p/base/transport_info.h"
#include "p2p/dtls/dtls_transport_internal.h"
#include "p2p/test/test_turn_server.h"
#include "pc/media_session.h"
#include "pc/peer_connection.h"
#include "pc/peer_connection_factory.h"
#include "pc/sctp_transport.h"
#include "pc/session_description.h"
#include "pc/test/fake_periodic_video_source.h"
#include "pc/test/integration_test_helpers.h"
#include "pc/test/mock_peer_connection_observers.h"
#include "rtc_base/fake_mdns_responder.h"
#include "rtc_base/firewall_socket_server.h"
#include "rtc_base/logging.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/random.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/ssl_fingerprint.h"
#include "rtc_base/ssl_identity.h"
#include "rtc_base/ssl_stream_adapter.h"
#include "rtc_base/string_encode.h"
#include "rtc_base/system/plan_b_only.h"
#include "rtc_base/task_queue_for_test.h"
#include "rtc_base/test_certificate_verifier.h"
#include "rtc_base/thread.h"
#include "rtc_base/virtual_socket_server.h"
#include "system_wrappers/include/metrics.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/wait_until.h"

namespace webrtc {

namespace {

using ::testing::AtLeast;
using ::testing::ByMove;
using ::testing::Contains;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Gt;
using ::testing::InSequence;
using ::testing::IsTrue;
using ::testing::MockFunction;
using ::testing::NiceMock;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::SizeIs;
using ::testing::WithParamInterface;

class PeerConnectionIntegrationTest : public PeerConnectionIntegrationBaseTest,
                                      public WithParamInterface<SdpSemantics> {
 protected:
  PeerConnectionIntegrationTest()
      : PeerConnectionIntegrationBaseTest(GetParam()) {}
};

class PeerConnectionIntegrationTestWithFakeClock
    : public PeerConnectionIntegrationTestWithSimulatedTime,
      public WithParamInterface<SdpSemantics> {
 protected:
  PeerConnectionIntegrationTestWithFakeClock()
      : PeerConnectionIntegrationTestWithSimulatedTime(GetParam()) {}
};

class PeerConnectionIntegrationTestPlanB
    : public PeerConnectionIntegrationBaseTest {
 protected:
  PeerConnectionIntegrationTestPlanB()
      : PeerConnectionIntegrationBaseTest(SdpSemantics::kPlanB_DEPRECATED) {}
};

class PeerConnectionIntegrationTestUnifiedPlan
    : public PeerConnectionIntegrationBaseTest {
 protected:
  PeerConnectionIntegrationTestUnifiedPlan()
      : PeerConnectionIntegrationBaseTest(SdpSemantics::kUnifiedPlan) {}
};

// Test the OnFirstPacketReceived callback from audio/video RtpReceivers.  This
// includes testing that the callback is invoked if an observer is connected
// after the first packet has already been received.
TEST_P(PeerConnectionIntegrationTest,
       RtpReceiverObserverOnFirstPacketReceived) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  // Start offer/answer exchange and wait for it to complete.
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  // Should be one receiver each for audio/video.
  EXPECT_EQ(2U, caller()->rtp_receiver_observers().size());
  EXPECT_EQ(2U, callee()->rtp_receiver_observers().size());
  // Wait for all "first packet received" callbacks to be fired.
  EXPECT_TRUE(WaitUntil(
      [&] {
        return absl::c_all_of(
            caller()->rtp_receiver_observers(),
            [](const std::unique_ptr<MockRtpReceiverObserver>& o) {
              return o->first_packet_received();
            });
      },
      {.timeout = kMaxWaitForFrames}));
  EXPECT_TRUE(WaitUntil(
      [&] {
        return absl::c_all_of(
            callee()->rtp_receiver_observers(),
            [](const std::unique_ptr<MockRtpReceiverObserver>& o) {
              return o->first_packet_received();
            });
      },
      {.timeout = kMaxWaitForFrames}));
  // If new observers are set after the first packet was already received, the
  // callback should still be invoked.
  caller()->ResetRtpReceiverObservers();
  callee()->ResetRtpReceiverObservers();
  EXPECT_EQ(2U, caller()->rtp_receiver_observers().size());
  EXPECT_EQ(2U, callee()->rtp_receiver_observers().size());
  EXPECT_TRUE(
      absl::c_all_of(caller()->rtp_receiver_observers(),
                     [](const std::unique_ptr<MockRtpReceiverObserver>& o) {
                       return o->first_packet_received();
                     }));
  EXPECT_TRUE(
      absl::c_all_of(callee()->rtp_receiver_observers(),
                     [](const std::unique_ptr<MockRtpReceiverObserver>& o) {
                       return o->first_packet_received();
                     }));
}

// Test the OnSourceChanged callback from audio/video RtpReceivers.
TEST_P(PeerConnectionIntegrationTest, RtpReceiverObserverOnSourceChanged) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  // Start offer/answer exchange and wait for it to complete.
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  EXPECT_EQ(2U, caller()->rtp_receiver_observers().size());
  EXPECT_EQ(2U, callee()->rtp_receiver_observers().size());
  // Wait for all "source changed" callbacks to be fired.
  EXPECT_TRUE(WaitUntil(
      [&] {
        return absl::c_all_of(
            caller()->rtp_receiver_observers(),
            [](const std::unique_ptr<MockRtpReceiverObserver>& o) {
              return o->source_changed() && o->last_ssrc_changed();
            });
      },
      {.timeout = kMaxWaitForFrames}));
  EXPECT_TRUE(WaitUntil(
      [&] {
        return absl::c_all_of(
            callee()->rtp_receiver_observers(),
            [](const std::unique_ptr<MockRtpReceiverObserver>& o) {
              return o->source_changed() && o->last_ssrc_changed();
            });
      },
      {.timeout = kMaxWaitForFrames}));
  // If new observers are set after sources have already arrived, the
  // callback should still be invoked.
  caller()->ResetRtpReceiverObservers();
  callee()->ResetRtpReceiverObservers();
  EXPECT_EQ(2U, caller()->rtp_receiver_observers().size());
  EXPECT_EQ(2U, callee()->rtp_receiver_observers().size());
  EXPECT_TRUE(
      absl::c_all_of(caller()->rtp_receiver_observers(),
                     [](const std::unique_ptr<MockRtpReceiverObserver>& o) {
                       return o->source_changed() && o->last_ssrc_changed();
                     }));
  EXPECT_TRUE(
      absl::c_all_of(callee()->rtp_receiver_observers(),
                     [](const std::unique_ptr<MockRtpReceiverObserver>& o) {
                       return o->source_changed() && o->last_ssrc_changed();
                     }));
}

TEST_P(PeerConnectionIntegrationTest,
       RtpReceiverObserverOnFirstPacketReceivedAfterInactive) {
  if (sdp_semantics_ != SdpSemantics::kUnifiedPlan) {
    GTEST_SKIP() << "Only supported in unified plan.";
  }
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  // Start offer/answer exchange and wait for it to complete.
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  // Should be one receiver each for audio/video.
  EXPECT_EQ(2U, caller()->rtp_receiver_observers().size());
  // Wait for all "first packet received" callbacks to be fired.
  EXPECT_TRUE(WaitUntil(
      [&] {
        return absl::c_all_of(
            caller()->rtp_receiver_observers(),
            [](const std::unique_ptr<MockRtpReceiverObserver>& o) {
              return o->first_packet_received();
            });
      },
      {.timeout = kMaxWaitForFrames}));

  // Renegotiate, going inactive and back to sendrecv.
  for (auto& transceiver : caller()->pc()->GetTransceivers()) {
    transceiver->SetDirectionWithError(RtpTransceiverDirection::kInactive);
  }
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  callee()->ResetRtpReceiverObservers();
  for (auto& transceiver : caller()->pc()->GetTransceivers()) {
    transceiver->SetDirectionWithError(RtpTransceiverDirection::kSendRecv);
  }
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  EXPECT_TRUE(
      WaitUntil(
          [&] {
            return absl::c_all_of(
                caller()->rtp_receiver_observers(),
                [](const std::unique_ptr<MockRtpReceiverObserver>& o) {
                  return o->first_packet_received_after_receptive_change();
                });
          },
          {.timeout = kMaxWaitForFrames}));
}

TEST_P(PeerConnectionIntegrationTest, RtpSenderObserverOnFirstPacketSent) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  // Start offer/answer exchange and wait for it to complete.
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  // Should be one sender each for audio/video.
  EXPECT_EQ(2U, caller()->rtp_sender_observers().size());
  EXPECT_EQ(2U, callee()->rtp_sender_observers().size());
  // Wait for all "first packet sent" callbacks to be fired.
  EXPECT_TRUE(WaitUntil(
      [&] {
        return absl::c_all_of(
            caller()->rtp_sender_observers(),
            [](const std::unique_ptr<MockRtpSenderObserver>& o) {
              return o->first_packet_sent();
            });
      },
      {.timeout = kMaxWaitForFrames}));
  EXPECT_TRUE(WaitUntil(
      [&] {
        return absl::c_all_of(
            callee()->rtp_sender_observers(),
            [](const std::unique_ptr<MockRtpSenderObserver>& o) {
              return o->first_packet_sent();
            });
      },
      {.timeout = kMaxWaitForFrames}));
  // If new observers are set after the first packet was already sent, the
  // callback should still be invoked.
  caller()->ResetRtpSenderObservers();
  callee()->ResetRtpSenderObservers();
  EXPECT_EQ(2U, caller()->rtp_sender_observers().size());
  EXPECT_EQ(2U, callee()->rtp_sender_observers().size());
  EXPECT_TRUE(
      absl::c_all_of(caller()->rtp_sender_observers(),
                     [](const std::unique_ptr<MockRtpSenderObserver>& o) {
                       return o->first_packet_sent();
                     }));
  EXPECT_TRUE(
      absl::c_all_of(callee()->rtp_sender_observers(),
                     [](const std::unique_ptr<MockRtpSenderObserver>& o) {
                       return o->first_packet_sent();
                     }));
}

class DummyDtmfObserver : public DtmfSenderObserverInterface {
 public:
  DummyDtmfObserver() : completed_(false) {}

  // Implements DtmfSenderObserverInterface.
  void OnToneChange(const std::string& tone,
                    const std::string& /* tone_buffer */) override {
    tones_.push_back(tone);
    if (tone.empty()) {
      completed_ = true;
    }
  }

  const std::vector<std::string>& tones() const { return tones_; }
  bool completed() const { return completed_; }

 private:
  bool completed_;
  std::vector<std::string> tones_;
};

// Assumes `sender` already has an audio track added and the offer/answer
// exchange is done.
void TestDtmfFromSenderToReceiver(PeerConnectionIntegrationWrapper* sender,
                                  PeerConnectionIntegrationWrapper* receiver) {
  // We should be able to get a DTMF sender from the local sender.
  scoped_refptr<DtmfSenderInterface> dtmf_sender =
      sender->pc()->GetSenders().at(0)->GetDtmfSender();
  ASSERT_TRUE(dtmf_sender);
  DummyDtmfObserver observer;
  dtmf_sender->RegisterObserver(&observer);

  // Test the DtmfSender object just created.
  EXPECT_TRUE(dtmf_sender->CanInsertDtmf());
  EXPECT_TRUE(dtmf_sender->InsertDtmf("1a", 100, 50));

  EXPECT_TRUE(WaitUntil([&] { return observer.completed(); }));
  std::vector<std::string> tones = {"1", "a", ""};
  EXPECT_EQ(tones, observer.tones());
  dtmf_sender->UnregisterObserver();
  // TODO(deadbeef): Verify the tones were actually received end-to-end.
}

// Verifies the DtmfSenderObserver callbacks for a DtmfSender (one in each
// direction).
TEST_P(PeerConnectionIntegrationTest, DtmfSenderObserver) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  // Only need audio for DTMF.
  caller()->AddAudioTrack();
  callee()->AddAudioTrack();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  // DTLS must finish before the DTMF sender can be used reliably.
  ASSERT_TRUE(WaitUntil([&] { return DtlsConnected(); }));
  TestDtmfFromSenderToReceiver(caller(), callee());
  TestDtmfFromSenderToReceiver(callee(), caller());
}

// Basic end-to-end test, verifying media can be encoded/transmitted/decoded
// between two connections, using DTLS-SRTP.
TEST_P(PeerConnectionIntegrationTest, EndToEndCallWithDtls) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();

  // Do normal offer/answer and wait for some frames to be received in each
  // direction.
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalAudioAndVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

// Basic end-to-end test specifying the `enable_encrypted_rtp_header_extensions`
// option to offer encrypted versions of all header extensions alongside the
// unencrypted versions.
TEST_P(PeerConnectionIntegrationTest,
       EndToEndCallWithEncryptedRtpHeaderExtensions) {
  CryptoOptions crypto_options;
  crypto_options.srtp.enable_encrypted_rtp_header_extensions = true;
  PeerConnectionInterface::RTCConfiguration config;
  config.crypto_options = crypto_options;
  // Note: This allows offering >14 RTP header extensions.
  config.offer_extmap_allow_mixed = true;
  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfig(config, config));
  ConnectFakeSignaling();

  // Do normal offer/answer and wait for some frames to be received in each
  // direction.
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalAudioAndVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

// This test sets up a call between two parties with a source resolution of
// 1280x720 and verifies that a 16:9 aspect ratio is received.
TEST_P(PeerConnectionIntegrationTest,
       Send1280By720ResolutionAndReceive16To9AspectRatio) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();

  // Add video tracks with 16:9 aspect ratio, size 1280 x 720.
  FakePeriodicVideoSource::Config config;
  config.width = 1280;
  config.height = 720;
  config.timestamp_offset = env_.clock().CurrentTime();
  caller()->AddTrack(caller()->CreateLocalVideoTrackWithConfig(config));
  callee()->AddTrack(callee()->CreateLocalVideoTrackWithConfig(config));

  // Do normal offer/answer and wait for at least one frame to be received in
  // each direction.
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil(
      [&] {
        return caller()->min_video_frames_received_per_track() > 0 &&
               callee()->min_video_frames_received_per_track() > 0;
      },
      {.timeout = kMaxWaitForFrames}));

  // Check rendered aspect ratio.
  EXPECT_EQ(16.0 / 9, caller()->local_rendered_aspect_ratio());
  EXPECT_EQ(16.0 / 9, caller()->rendered_aspect_ratio());
  EXPECT_EQ(16.0 / 9, callee()->local_rendered_aspect_ratio());
  EXPECT_EQ(16.0 / 9, callee()->rendered_aspect_ratio());
}

// This test sets up an one-way call, with media only from caller to
// callee.
TEST_P(PeerConnectionIntegrationTest, OneWayMediaCall) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  MediaExpectations media_expectations;
  media_expectations.CalleeExpectsSomeAudioAndVideo();
  media_expectations.CallerExpectsNoAudio();
  media_expectations.CallerExpectsNoVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

// Tests that send only works without the caller having a decoder factory and
// the callee having an encoder factory.
TEST_P(PeerConnectionIntegrationTest, EndToEndCallWithSendOnlyVideo) {
  ASSERT_TRUE(
      CreateOneDirectionalPeerConnectionWrappers(/*caller_to_callee=*/true));
  ConnectFakeSignaling();
  // Add one-directional video, from caller to callee.
  scoped_refptr<VideoTrackInterface> caller_track =
      caller()->CreateLocalVideoTrack();
  caller()->AddTrack(caller_track);
  PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.offer_to_receive_video = 0;
  caller()->SetOfferAnswerOptions(options);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_EQ(callee()->pc()->GetReceivers().size(), 1u);

  // Expect video to be received in one direction.
  MediaExpectations media_expectations;
  media_expectations.CallerExpectsNoVideo();
  media_expectations.CalleeExpectsSomeVideo();

  EXPECT_TRUE(ExpectNewFrames(media_expectations));
}

// Tests that receive only works without the caller having an encoder factory
// and the callee having a decoder factory.
TEST_P(PeerConnectionIntegrationTest, EndToEndCallWithReceiveOnlyVideo) {
  ASSERT_TRUE(
      CreateOneDirectionalPeerConnectionWrappers(/*caller_to_callee=*/false));
  ConnectFakeSignaling();
  // Add one-directional video, from callee to caller.
  scoped_refptr<VideoTrackInterface> callee_track =
      callee()->CreateLocalVideoTrack();
  callee()->AddTrack(callee_track);
  PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.offer_to_receive_video = 1;
  caller()->SetOfferAnswerOptions(options);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_EQ(caller()->pc()->GetReceivers().size(), 1u);

  // Expect video to be received in one direction.
  MediaExpectations media_expectations;
  media_expectations.CallerExpectsSomeVideo();
  media_expectations.CalleeExpectsNoVideo();

  EXPECT_TRUE(ExpectNewFrames(media_expectations));
}

TEST_P(PeerConnectionIntegrationTest,
       EndToEndCallAddReceiveVideoToSendOnlyCall) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  // Add one-directional video, from caller to callee.
  scoped_refptr<VideoTrackInterface> caller_track =
      caller()->CreateLocalVideoTrack();
  caller()->AddTrack(caller_track);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  // Add receive video.
  scoped_refptr<VideoTrackInterface> callee_track =
      callee()->CreateLocalVideoTrack();
  callee()->AddTrack(callee_track);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  // Ensure that video frames are received end-to-end.
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

TEST_P(PeerConnectionIntegrationTest,
       EndToEndCallAddSendVideoToReceiveOnlyCall) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  // Add one-directional video, from callee to caller.
  scoped_refptr<VideoTrackInterface> callee_track =
      callee()->CreateLocalVideoTrack();
  callee()->AddTrack(callee_track);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  // Add send video.
  scoped_refptr<VideoTrackInterface> caller_track =
      caller()->CreateLocalVideoTrack();
  caller()->AddTrack(caller_track);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  // Expect video to be received in one direction.
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

TEST_P(PeerConnectionIntegrationTest,
       EndToEndCallRemoveReceiveVideoFromSendReceiveCall) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  // Add send video, from caller to callee.
  scoped_refptr<VideoTrackInterface> caller_track =
      caller()->CreateLocalVideoTrack();
  scoped_refptr<RtpSenderInterface> caller_sender =
      caller()->AddTrack(caller_track);
  // Add receive video, from callee to caller.
  scoped_refptr<VideoTrackInterface> callee_track =
      callee()->CreateLocalVideoTrack();

  scoped_refptr<RtpSenderInterface> callee_sender =
      callee()->AddTrack(callee_track);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  // Remove receive video (i.e., callee sender track).
  callee()->pc()->RemoveTrackOrError(callee_sender);

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  // Expect one-directional video.
  MediaExpectations media_expectations;
  media_expectations.CallerExpectsNoVideo();
  media_expectations.CalleeExpectsSomeVideo();

  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

TEST_P(PeerConnectionIntegrationTest,
       EndToEndCallRemoveSendVideoFromSendReceiveCall) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  // Add send video, from caller to callee.
  scoped_refptr<VideoTrackInterface> caller_track =
      caller()->CreateLocalVideoTrack();
  scoped_refptr<RtpSenderInterface> caller_sender =
      caller()->AddTrack(caller_track);
  // Add receive video, from callee to caller.
  scoped_refptr<VideoTrackInterface> callee_track =
      callee()->CreateLocalVideoTrack();

  scoped_refptr<RtpSenderInterface> callee_sender =
      callee()->AddTrack(callee_track);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  // Remove send video (i.e., caller sender track).
  caller()->pc()->RemoveTrackOrError(caller_sender);

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  // Expect one-directional video.
  MediaExpectations media_expectations;
  media_expectations.CalleeExpectsNoVideo();
  media_expectations.CallerExpectsSomeVideo();

  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

// This test sets up a audio call initially, with the callee rejecting video
// initially. Then later the callee decides to upgrade to audio/video, and
// initiates a new offer/answer exchange.
TEST_P(PeerConnectionIntegrationTest, AudioToVideoUpgrade) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  // Initially, offer an audio/video stream from the caller, but refuse to
  // send/receive video on the callee side.
  caller()->AddAudioVideoTracks();
  callee()->AddAudioTrack();
  if (sdp_semantics_ == SdpSemantics::kPlanB_DEPRECATED) {
    PeerConnectionInterface::RTCOfferAnswerOptions options;
    options.offer_to_receive_video = 0;
    callee()->SetOfferAnswerOptions(options);
  } else {
    callee()->SetRemoteOfferHandler([this] {
      callee()->GetFirstTransceiverOfType(MediaType::VIDEO)->StopInternal();
    });
  }
  // Do offer/answer and make sure audio is still received end-to-end.
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  {
    MediaExpectations media_expectations;
    media_expectations.ExpectBidirectionalAudio();
    media_expectations.ExpectNoVideo();
    ASSERT_TRUE(ExpectNewFrames(media_expectations));
  }
  // Sanity check that the callee's description has a rejected video section.
  ASSERT_NE(nullptr, callee()->pc()->local_description());
  const ContentInfo* callee_video_content =
      GetFirstVideoContent(callee()->pc()->local_description()->description());
  ASSERT_NE(nullptr, callee_video_content);
  EXPECT_TRUE(callee_video_content->rejected);

  // Now negotiate with video and ensure negotiation succeeds, with video
  // frames and additional audio frames being received.
  callee()->AddVideoTrack();
  if (sdp_semantics_ == SdpSemantics::kPlanB_DEPRECATED) {
    PeerConnectionInterface::RTCOfferAnswerOptions options;
    options.offer_to_receive_video = 1;
    callee()->SetOfferAnswerOptions(options);
  } else {
    callee()->SetRemoteOfferHandler(nullptr);
    caller()->SetRemoteOfferHandler([this] {
      // The caller creates a new transceiver to receive video on when receiving
      // the offer, but by default it is send only.
      auto transceivers = caller()->pc()->GetTransceivers();
      ASSERT_EQ(2U, transceivers.size());
      ASSERT_EQ(MediaType::VIDEO, transceivers[1]->receiver()->media_type());
      transceivers[1]->sender()->SetTrack(
          caller()->CreateLocalVideoTrack().get());
      transceivers[1]->SetDirectionWithError(
          RtpTransceiverDirection::kSendRecv);
    });
  }
  callee()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  {
    // Expect additional audio frames to be received after the upgrade.
    MediaExpectations media_expectations;
    media_expectations.ExpectBidirectionalAudioAndVideo();
    ASSERT_TRUE(ExpectNewFrames(media_expectations));
  }
}

// Simpler than the above test; just add an audio track to an established
// video-only connection.
TEST_P(PeerConnectionIntegrationTest, AddAudioToVideoOnlyCall) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  // Do initial offer/answer with just a video track.
  caller()->AddVideoTrack();
  callee()->AddVideoTrack();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  // Now add an audio track and do another offer/answer.
  caller()->AddAudioTrack();
  callee()->AddAudioTrack();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  // Ensure both audio and video frames are received end-to-end.
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalAudioAndVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

// This test sets up a non-bundled call and negotiates bundling at the same
// time as starting an ICE restart. When bundling is in effect in the restart,
// the DTLS-SRTP context should be successfully reset.
TEST_P(PeerConnectionIntegrationTest, BundlingEnabledWhileIceRestartOccurs) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();

  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  // Remove the bundle group from the SDP received by the callee.
  callee()->SetReceivedSdpMunger(
      [](std::unique_ptr<SessionDescriptionInterface>& sdp) {
        sdp->description()->RemoveGroupByName("BUNDLE");
      });
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  {
    MediaExpectations media_expectations;
    media_expectations.ExpectBidirectionalAudioAndVideo();
    ASSERT_TRUE(ExpectNewFrames(media_expectations));
  }
  // Now stop removing the BUNDLE group, and trigger an ICE restart.
  callee()->SetReceivedSdpMunger(nullptr);
  caller()->SetOfferAnswerOptions(IceRestartOfferAnswerOptions());
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  // Expect additional frames to be received after the ICE restart.
  {
    MediaExpectations media_expectations;
    media_expectations.ExpectBidirectionalAudioAndVideo();
    ASSERT_TRUE(ExpectNewFrames(media_expectations));
  }
}

// Test CVO (Coordination of Video Orientation). If a video source is rotated
// and both peers support the CVO RTP header extension, the actual video frames
// don't need to be encoded in different resolutions, since the rotation is
// communicated through the RTP header extension.
TEST_P(PeerConnectionIntegrationTest, RotatedVideoWithCVOExtension) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  // Add rotated video tracks.
  caller()->AddTrack(
      caller()->CreateLocalVideoTrackWithRotation(kVideoRotation_90));
  callee()->AddTrack(
      callee()->CreateLocalVideoTrackWithRotation(kVideoRotation_270));

  // Wait for video frames to be received by both sides.
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_TRUE(WaitUntil(
      [&] {
        return caller()->min_video_frames_received_per_track() > 0 &&
               callee()->min_video_frames_received_per_track() > 0;
      },
      {.timeout = kMaxWaitForFrames}));

  // Ensure that the aspect ratio is unmodified.
  // TODO(deadbeef): Where does 4:3 come from? Should be explicit in the test,
  // not just assumed.
  EXPECT_EQ(4.0 / 3, caller()->local_rendered_aspect_ratio());
  EXPECT_EQ(4.0 / 3, caller()->rendered_aspect_ratio());
  EXPECT_EQ(4.0 / 3, callee()->local_rendered_aspect_ratio());
  EXPECT_EQ(4.0 / 3, callee()->rendered_aspect_ratio());
  // Ensure that the CVO bits were surfaced to the renderer.
  EXPECT_EQ(kVideoRotation_270, caller()->rendered_rotation());
  EXPECT_EQ(kVideoRotation_90, callee()->rendered_rotation());
}

// Test that when the CVO extension isn't supported, video is rotated the
// old-fashioned way, by encoding rotated frames.
TEST_P(PeerConnectionIntegrationTest, RotatedVideoWithoutCVOExtension) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  // Add rotated video tracks.
  caller()->AddTrack(
      caller()->CreateLocalVideoTrackWithRotation(kVideoRotation_90));
  callee()->AddTrack(
      callee()->CreateLocalVideoTrackWithRotation(kVideoRotation_270));

  // Remove the CVO extension from the offered SDP.
  callee()->SetReceivedSdpMunger(
      [](std::unique_ptr<SessionDescriptionInterface>& sdp) {
        VideoContentDescription* video =
            GetFirstVideoContentDescription(sdp->description());
        video->set_rtp_header_extensions({});
      });
  // Wait for video frames to be received by both sides.
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_TRUE(WaitUntil(
      [&] {
        return caller()->min_video_frames_received_per_track() > 0 &&
               callee()->min_video_frames_received_per_track() > 0;
      },
      {.timeout = kMaxWaitForFrames}));

  // Expect that the aspect ratio is inversed to account for the 90/270 degree
  // rotation.
  // TODO(deadbeef): Where does 4:3 come from? Should be explicit in the test,
  // not just assumed.
  EXPECT_EQ(3.0 / 4, caller()->local_rendered_aspect_ratio());
  EXPECT_EQ(3.0 / 4, caller()->rendered_aspect_ratio());
  EXPECT_EQ(3.0 / 4, callee()->local_rendered_aspect_ratio());
  EXPECT_EQ(3.0 / 4, callee()->rendered_aspect_ratio());
  // Expect that each endpoint is unaware of the rotation of the other endpoint.
  EXPECT_EQ(kVideoRotation_0, caller()->rendered_rotation());
  EXPECT_EQ(kVideoRotation_0, callee()->rendered_rotation());
}

// Test that if the answerer rejects the audio m= section, no audio is sent or
// received, but video still can be.
TEST_P(PeerConnectionIntegrationTest, AnswererRejectsAudioSection) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddAudioVideoTracks();
  if (sdp_semantics_ == SdpSemantics::kPlanB_DEPRECATED) {
    // Only add video track for callee, and set offer_to_receive_audio to 0, so
    // it will reject the audio m= section completely.
    PeerConnectionInterface::RTCOfferAnswerOptions options;
    options.offer_to_receive_audio = 0;
    callee()->SetOfferAnswerOptions(options);
  } else {
    // Stopping the audio RtpTransceiver will cause the media section to be
    // rejected in the answer.
    callee()->SetRemoteOfferHandler([this] {
      callee()->GetFirstTransceiverOfType(MediaType::AUDIO)->StopInternal();
    });
  }
  callee()->AddTrack(callee()->CreateLocalVideoTrack());
  // Do offer/answer and wait for successful end-to-end video frames.
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalVideo();
  media_expectations.ExpectNoAudio();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));

  // Sanity check that the callee's description has a rejected audio section.
  ASSERT_NE(nullptr, callee()->pc()->local_description());
  const ContentInfo* callee_audio_content =
      GetFirstAudioContent(callee()->pc()->local_description()->description());
  ASSERT_NE(nullptr, callee_audio_content);
  EXPECT_TRUE(callee_audio_content->rejected);
  if (sdp_semantics_ == SdpSemantics::kUnifiedPlan) {
    // The caller's transceiver should have stopped after receiving the answer,
    // and thus no longer listed in transceivers.
    EXPECT_EQ(nullptr, caller()->GetFirstTransceiverOfType(MediaType::AUDIO));
  }
}

// Test that if the answerer rejects the video m= section, no video is sent or
// received, but audio still can be.
TEST_P(PeerConnectionIntegrationTest, AnswererRejectsVideoSection) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddAudioVideoTracks();
  if (sdp_semantics_ == SdpSemantics::kPlanB_DEPRECATED) {
    // Only add audio track for callee, and set offer_to_receive_video to 0, so
    // it will reject the video m= section completely.
    PeerConnectionInterface::RTCOfferAnswerOptions options;
    options.offer_to_receive_video = 0;
    callee()->SetOfferAnswerOptions(options);
  } else {
    // Stopping the video RtpTransceiver will cause the media section to be
    // rejected in the answer.
    callee()->SetRemoteOfferHandler([this] {
      callee()->GetFirstTransceiverOfType(MediaType::VIDEO)->StopInternal();
    });
  }
  callee()->AddTrack(callee()->CreateLocalAudioTrack());
  // Do offer/answer and wait for successful end-to-end audio frames.
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalAudio();
  media_expectations.ExpectNoVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));

  // Sanity check that the callee's description has a rejected video section.
  ASSERT_NE(nullptr, callee()->pc()->local_description());
  const ContentInfo* callee_video_content =
      GetFirstVideoContent(callee()->pc()->local_description()->description());
  ASSERT_NE(nullptr, callee_video_content);
  EXPECT_TRUE(callee_video_content->rejected);
  if (sdp_semantics_ == SdpSemantics::kUnifiedPlan) {
    // The caller's transceiver should have stopped after receiving the answer,
    // and thus is no longer present.
    EXPECT_EQ(nullptr, caller()->GetFirstTransceiverOfType(MediaType::VIDEO));
  }
}

// Test that if the answerer rejects both audio and video m= sections, nothing
// bad happens.
// TODO(deadbeef): Test that a data channel still works. Currently this doesn't
// test anything but the fact that negotiation succeeds, which doesn't mean
// much.
TEST_P(PeerConnectionIntegrationTest, AnswererRejectsAudioAndVideoSections) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddAudioVideoTracks();
  if (sdp_semantics_ == SdpSemantics::kPlanB_DEPRECATED) {
    // Don't give the callee any tracks, and set offer_to_receive_X to 0, so it
    // will reject both audio and video m= sections.
    PeerConnectionInterface::RTCOfferAnswerOptions options;
    options.offer_to_receive_audio = 0;
    options.offer_to_receive_video = 0;
    callee()->SetOfferAnswerOptions(options);
  } else {
    callee()->SetRemoteOfferHandler([this] {
      // Stopping all transceivers will cause all media sections to be rejected.
      for (const auto& transceiver : callee()->pc()->GetTransceivers()) {
        transceiver->StopInternal();
      }
    });
  }
  // Do offer/answer and wait for stable signaling state.
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  // Sanity check that the callee's description has rejected m= sections.
  ASSERT_NE(nullptr, callee()->pc()->local_description());
  const ContentInfo* callee_audio_content =
      GetFirstAudioContent(callee()->pc()->local_description()->description());
  ASSERT_NE(nullptr, callee_audio_content);
  EXPECT_TRUE(callee_audio_content->rejected);
  const ContentInfo* callee_video_content =
      GetFirstVideoContent(callee()->pc()->local_description()->description());
  ASSERT_NE(nullptr, callee_video_content);
  EXPECT_TRUE(callee_video_content->rejected);
}

// This test sets up an audio and video call between two parties. After the
// call runs for a while, the caller sends an updated offer with video being
// rejected. Once the re-negotiation is done, the video flow should stop and
// the audio flow should continue.
TEST_P(PeerConnectionIntegrationTest, VideoRejectedInSubsequentOffer) {
  if (sdp_semantics_ == SdpSemantics::kPlanB_DEPRECATED) {
    // Munging allowed: kRejected (section rejection)
    SetFieldTrials("WebRTC-NoSdpMangleAllowForTesting/Enabled,39/");
  }
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  {
    MediaExpectations media_expectations;
    media_expectations.ExpectBidirectionalAudioAndVideo();
    ASSERT_TRUE(ExpectNewFrames(media_expectations));
  }
  // Renegotiate, rejecting the video m= section.
  if (sdp_semantics_ == SdpSemantics::kPlanB_DEPRECATED) {
    caller()->SetGeneratedSdpMunger(
        [](std::unique_ptr<SessionDescriptionInterface>& sdp) {
          for (ContentInfo& content : sdp->description()->contents()) {
            if (IsVideoContent(&content)) {
              content.rejected = true;
            }
          }
        });
  } else {
    caller()->GetFirstTransceiverOfType(MediaType::VIDEO)->StopInternal();
  }
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); },
                        {.timeout = kMaxWaitForActivation}));

  // Sanity check that the caller's description has a rejected video section.
  ASSERT_NE(nullptr, caller()->pc()->local_description());
  const ContentInfo* caller_video_content =
      GetFirstVideoContent(caller()->pc()->local_description()->description());
  ASSERT_NE(nullptr, caller_video_content);
  EXPECT_TRUE(caller_video_content->rejected);
  // Wait for some additional audio frames to be received.
  {
    MediaExpectations media_expectations;
    media_expectations.ExpectBidirectionalAudio();
    media_expectations.ExpectNoVideo();
    ASSERT_TRUE(ExpectNewFrames(media_expectations));
  }
}

// Do one offer/answer with audio, another that disables it (rejecting the m=
// section), and another that re-enables it. Regression test for:
// bugs.webrtc.org/6023
TEST_F(PeerConnectionIntegrationTestPlanB, EnableAudioAfterRejecting) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();

  // Add audio track, do normal offer/answer.
  scoped_refptr<AudioTrackInterface> track = caller()->CreateLocalAudioTrack();
  scoped_refptr<RtpSenderInterface> sender =
      caller()->pc()->AddTrack(track, {"stream"}).MoveValue();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  // Remove audio track, and set offer_to_receive_audio to false to cause the
  // m= section to be completely disabled, not just "recvonly".
  caller()->pc()->RemoveTrackOrError(sender);
  PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.offer_to_receive_audio = 0;
  caller()->SetOfferAnswerOptions(options);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  // Add the audio track again, expecting negotiation to succeed and frames to
  // flow.
  sender = caller()->pc()->AddTrack(track, {"stream"}).MoveValue();
  options.offer_to_receive_audio = 1;
  caller()->SetOfferAnswerOptions(options);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  MediaExpectations media_expectations;
  media_expectations.CalleeExpectsSomeAudio();
  EXPECT_TRUE(ExpectNewFrames(media_expectations));
}

// Basic end-to-end test, but without SSRC/MSID signaling. This functionality
// is needed to support legacy endpoints.
// TODO(deadbeef): When we support the MID extension and demuxing on MID, also
// add a test for an end-to-end test without MID signaling either (basically,
// the minimum acceptable SDP).
TEST_P(PeerConnectionIntegrationTest, EndToEndCallWithoutSsrcOrMsidSignaling) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  // Add audio and video, testing that packets can be demuxed on payload type.
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  // Remove SSRCs and MSIDs from the received offer SDP.
  callee()->SetReceivedSdpMunger(RemoveSsrcsAndMsids);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalAudioAndVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

// Basic end-to-end test, without SSRC signaling. This means that the track
// was created properly and frames are delivered when the MSIDs are communicated
// with a=msid lines and no a=ssrc lines.
TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       EndToEndCallWithoutSsrcSignaling) {
  const char kStreamId[] = "streamId";
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  // Add just audio tracks.
  caller()->AddTrack(caller()->CreateLocalAudioTrack(), {kStreamId});
  callee()->AddAudioTrack();

  // Remove SSRCs from the received offer SDP.
  callee()->SetReceivedSdpMunger(RemoveSsrcsAndKeepMsids);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalAudio();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       EndToEndCallAddReceiveVideoToSendOnlyCall) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  // Add one-directional video, from caller to callee.
  scoped_refptr<VideoTrackInterface> track = caller()->CreateLocalVideoTrack();

  RtpTransceiverInit video_transceiver_init;
  video_transceiver_init.stream_ids = {"video1"};
  video_transceiver_init.direction = RtpTransceiverDirection::kSendOnly;
  auto video_sender =
      caller()->pc()->AddTransceiver(track, video_transceiver_init).MoveValue();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  // Add receive direction.
  video_sender->SetDirectionWithError(RtpTransceiverDirection::kSendRecv);

  scoped_refptr<VideoTrackInterface> callee_track =
      callee()->CreateLocalVideoTrack();

  callee()->AddTrack(callee_track);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  // Ensure that video frames are received end-to-end.
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

// Tests that video flows between multiple video tracks when SSRCs are not
// signaled. This exercises the MID RTP header extension which is needed to
// demux the incoming video tracks.
TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       EndToEndCallWithTwoVideoTracksAndNoSignaledSsrc) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddVideoTrack();
  caller()->AddVideoTrack();
  callee()->AddVideoTrack();
  callee()->AddVideoTrack();

  caller()->SetReceivedSdpMunger(&RemoveSsrcsAndKeepMsids);
  callee()->SetReceivedSdpMunger(&RemoveSsrcsAndKeepMsids);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_EQ(2u, caller()->pc()->GetReceivers().size());
  ASSERT_EQ(2u, callee()->pc()->GetReceivers().size());

  // Expect video to be received in both directions on both tracks.
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalVideo();
  EXPECT_TRUE(ExpectNewFrames(media_expectations));
}

// Used for the test below.
void RemoveBundleGroupSsrcsAndMidExtension(
    std::unique_ptr<SessionDescriptionInterface>& sdp) {
  RemoveSsrcsAndKeepMsids(sdp);
  sdp->description()->RemoveGroupByName("BUNDLE");
  for (ContentInfo& content : sdp->description()->contents()) {
    MediaContentDescription* media = content.media_description();
    RtpHeaderExtensions extensions = media->rtp_header_extensions();
    std::erase_if(extensions, [](const RtpExtension& extension) {
      return extension.uri == RtpExtension::kMidUri;
    });
    media->set_rtp_header_extensions(extensions);
  }
}

// Tests that video flows between multiple video tracks when BUNDLE is not used,
// SSRCs are not signaled and the MID RTP header extension is not used. This
// relies on demuxing by payload type, which normally doesn't work if you have
// multiple media sections using the same payload type, but which should work as
// long as the media sections aren't bundled.
// Regression test for: http://crbug.com/webrtc/12023
TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       EndToEndCallWithTwoVideoTracksNoBundleNoSignaledSsrcAndNoMid) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddVideoTrack();
  caller()->AddVideoTrack();
  callee()->AddVideoTrack();
  callee()->AddVideoTrack();
  caller()->SetReceivedSdpMunger(&RemoveBundleGroupSsrcsAndMidExtension);
  callee()->SetReceivedSdpMunger(&RemoveBundleGroupSsrcsAndMidExtension);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_EQ(2u, caller()->pc()->GetReceivers().size());
  ASSERT_EQ(2u, callee()->pc()->GetReceivers().size());
  // Make sure we are not bundled.
  ASSERT_NE(caller()->pc()->GetSenders()[0]->dtls_transport(),
            caller()->pc()->GetSenders()[1]->dtls_transport());

  // Expect video to be received in both directions on both tracks.
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalVideo();
  EXPECT_TRUE(ExpectNewFrames(media_expectations));
}

// Used for the test below.
void ModifyPayloadTypesAndRemoveMidExtension(
    std::unique_ptr<SessionDescriptionInterface>& sdp) {
  int pt = 96;
  for (ContentInfo& content : sdp->description()->contents()) {
    MediaContentDescription* media = content.media_description();
    RtpHeaderExtensions extensions = media->rtp_header_extensions();
    std::erase_if(extensions, [](const RtpExtension& extension) {
      return extension.uri == RtpExtension::kMidUri;
    });
    media->set_rtp_header_extensions(extensions);
    media->set_codecs({CreateVideoCodec(pt++, "VP8")});
  }
}

// Tests that two video tracks can be demultiplexed by payload type alone, by
// using different payload types for the same codec in different m= sections.
// This practice is discouraged but historically has been supported.
// Regression test for: http://crbug.com/webrtc/12029
TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       EndToEndCallWithTwoVideoTracksDemultiplexedByPayloadType) {
  SetFieldTrials("WebRTC-NoSdpMangleAllowForTesting/Enabled,26,40,80/");
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddVideoTrack();
  caller()->AddVideoTrack();
  callee()->AddVideoTrack();
  callee()->AddVideoTrack();
  caller()->SetGeneratedSdpMunger(&ModifyPayloadTypesAndRemoveMidExtension);
  callee()->SetGeneratedSdpMunger(&ModifyPayloadTypesAndRemoveMidExtension);
  // We can't remove SSRCs from the generated SDP because then no send streams
  // would be created.
  caller()->SetReceivedSdpMunger(&RemoveSsrcsAndKeepMsids);
  callee()->SetReceivedSdpMunger(&RemoveSsrcsAndKeepMsids);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_EQ(2u, caller()->pc()->GetReceivers().size());
  ASSERT_EQ(2u, callee()->pc()->GetReceivers().size());
  // Make sure we are bundled.
  ASSERT_EQ(caller()->pc()->GetSenders()[0]->dtls_transport(),
            caller()->pc()->GetSenders()[1]->dtls_transport());

  // Expect video to be received in both directions on both tracks.
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalVideo();
  EXPECT_TRUE(ExpectNewFrames(media_expectations));
}

TEST_F(PeerConnectionIntegrationTestUnifiedPlan, NoStreamsMsidLinePresent) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddAudioTrack();
  caller()->AddVideoTrack();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  auto callee_receivers = callee()->pc()->GetReceivers();
  ASSERT_EQ(2u, callee_receivers.size());
  EXPECT_TRUE(callee_receivers[0]->stream_ids().empty());
  EXPECT_TRUE(callee_receivers[1]->stream_ids().empty());
}

TEST_F(PeerConnectionIntegrationTestUnifiedPlan, NoStreamsMsidLineMissing) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddAudioTrack();
  caller()->AddVideoTrack();
  callee()->SetReceivedSdpMunger(RemoveSsrcsAndMsids);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  auto callee_receivers = callee()->pc()->GetReceivers();
  ASSERT_EQ(2u, callee_receivers.size());
  ASSERT_EQ(1u, callee_receivers[0]->stream_ids().size());
  ASSERT_EQ(1u, callee_receivers[1]->stream_ids().size());
  EXPECT_EQ(callee_receivers[0]->stream_ids()[0],
            callee_receivers[1]->stream_ids()[0]);
  EXPECT_EQ(callee_receivers[0]->streams()[0],
            callee_receivers[1]->streams()[0]);
}

// Used by the CSRC tests below.
MATCHER_P(IsCsrcWithId, csrc, "") {
  return arg.source_type() == RtpSourceType::CSRC && arg.source_id() == csrc;
}

// Test that CSRCs can be set upon adding tracks and that they are received by
// the other side.
TEST_F(PeerConnectionIntegrationTestUnifiedPlan, EndToEndCallForwardsCsrcs) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();

  constexpr uint32_t kAudioCsrc = 1234;
  constexpr uint32_t kVideoCsrc = 5678;
  RtpEncodingParameters audio_encoding;
  RtpEncodingParameters video_encoding;
  audio_encoding.csrcs = {kAudioCsrc};
  video_encoding.csrcs = {kVideoCsrc};
  ASSERT_THAT(caller()->pc()->AddTrack(caller()->CreateLocalAudioTrack(), {},
                                       {audio_encoding}),
              IsRtcOk());
  ASSERT_THAT(caller()->pc()->AddTrack(caller()->CreateLocalVideoTrack(), {},
                                       {video_encoding}),
              IsRtcOk());

  caller()->CreateAndSetAndSignalOffer();

  // Wait for some packets to arrive.
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_TRUE(WaitUntil(
      [&] {
        return callee()->audio_frames_received() > 0 &&
               callee()->min_video_frames_received_per_track() > 0;
      },
      {.timeout = kMaxWaitForFrames}));

  std::vector<scoped_refptr<RtpReceiverInterface>> audio_receivers =
      callee()->GetReceiversOfType(webrtc::MediaType::AUDIO);
  ASSERT_EQ(audio_receivers.size(), 1u);
  std::vector<scoped_refptr<RtpReceiverInterface>> video_receivers =
      callee()->GetReceiversOfType(webrtc::MediaType::VIDEO);
  ASSERT_EQ(video_receivers.size(), 1u);

  EXPECT_TRUE(WaitUntil([&] {
    return !audio_receivers[0]->GetSources().empty() &&
           !video_receivers[0]->GetSources().empty();
  }));
  EXPECT_THAT(audio_receivers[0]->GetSources(),
              Contains(IsCsrcWithId(kAudioCsrc)));
  EXPECT_THAT(video_receivers[0]->GetSources(),
              Contains(IsCsrcWithId(kVideoCsrc)));
}

// Test that CSRCs can be updated on active tracks.
TEST_F(PeerConnectionIntegrationTestUnifiedPlan, EndToEndCallCanUpdateCsrcs) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();

  RtpEncodingParameters audio_encoding;
  RtpEncodingParameters video_encoding;
  audio_encoding.csrcs = {1234};
  video_encoding.csrcs = {5678};

  scoped_refptr<RtpSenderInterface> audio_sender =
      caller()
          ->pc()
          ->AddTrack(caller()->CreateLocalAudioTrack(), {}, {audio_encoding})
          .MoveValue();
  scoped_refptr<RtpSenderInterface> video_sender =
      caller()
          ->pc()
          ->AddTrack(caller()->CreateLocalVideoTrack(), {}, {video_encoding})
          .MoveValue();

  caller()->CreateAndSetAndSignalOffer();

  // Wait for some packets to arrive.
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_TRUE(WaitUntil(
      [&] {
        return callee()->audio_frames_received() > 0 &&
               callee()->min_video_frames_received_per_track() > 0;
      },
      {.timeout = kMaxWaitForFrames}));

  // Update the CSRCs.
  constexpr uint32_t kUpdatedAudioCsrc = 4321;
  constexpr uint32_t kUpdatedVideoCsrc = 8765;
  RtpParameters audio_parameters = audio_sender->GetParameters();
  RtpParameters video_parameters = video_sender->GetParameters();
  audio_parameters.encodings[0].csrcs = {kUpdatedAudioCsrc};
  video_parameters.encodings[0].csrcs = {kUpdatedVideoCsrc};
  ASSERT_THAT(audio_sender->SetParameters(audio_parameters), IsRtcOk());
  ASSERT_THAT(video_sender->SetParameters(video_parameters), IsRtcOk());

  std::vector<scoped_refptr<RtpReceiverInterface>> audio_receivers =
      callee()->GetReceiversOfType(webrtc::MediaType::AUDIO);
  ASSERT_EQ(audio_receivers.size(), 1u);
  std::vector<scoped_refptr<RtpReceiverInterface>> video_receivers =
      callee()->GetReceiversOfType(webrtc::MediaType::VIDEO);
  ASSERT_EQ(video_receivers.size(), 1u);

  // Ensure that the new CSRCs are used.
  EXPECT_THAT(WaitUntil([&] { return audio_receivers[0]->GetSources(); },
                        Contains(IsCsrcWithId(kUpdatedAudioCsrc)),
                        {.timeout = kMaxWaitForFrames}),
              IsRtcOk());
  EXPECT_THAT(WaitUntil([&] { return video_receivers[0]->GetSources(); },
                        Contains(IsCsrcWithId(kUpdatedVideoCsrc)),
                        {.timeout = kMaxWaitForFrames}),
              IsRtcOk());
}

// Test that if two video tracks are sent (from caller to callee, in this test),
// they're transmitted correctly end-to-end.
TEST_P(PeerConnectionIntegrationTest, EndToEndCallWithTwoVideoTracks) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  // Add one audio/video stream, and one video-only stream.
  caller()->AddAudioVideoTracks();
  caller()->AddVideoTrack();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_EQ(3u, callee()->pc()->GetReceivers().size());

  MediaExpectations media_expectations;
  media_expectations.CalleeExpectsSomeAudioAndVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

void MakeSpecCompliantMaxBundleOffer(
    std::unique_ptr<SessionDescriptionInterface>& sdp) {
  bool first = true;
  for (ContentInfo& content : sdp->description()->contents()) {
    if (first) {
      first = false;
      continue;
    }
    content.bundle_only = true;
  }
  first = true;
  for (TransportInfo& transport : sdp->description()->transport_infos()) {
    if (first) {
      first = false;
      continue;
    }
    transport.description.ice_ufrag.clear();
    transport.description.ice_pwd.clear();
    transport.description.connection_role = CONNECTIONROLE_NONE;
    transport.description.identity_fingerprint.reset(nullptr);
  }
}

// Test that if applying a true "max bundle" offer, which uses ports of 0,
// "a=bundle-only", omitting "a=fingerprint", "a=setup", "a=ice-ufrag" and
// "a=ice-pwd" for all but the audio "m=" section, negotiation still completes
// successfully and media flows.
// TODO(deadbeef): Update this test to also omit "a=rtcp-mux", once that works.
// TODO(deadbeef): Won't need this test once we start generating actual
// standards-compliant SDP.
TEST_P(PeerConnectionIntegrationTest,
       EndToEndCallWithSpecCompliantMaxBundleOffer) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  // Do the equivalent of setting the port to 0, adding a=bundle-only, and
  // removing a=ice-ufrag, a=ice-pwd, a=fingerprint and a=setup from all
  // but the first m= section.
  callee()->SetReceivedSdpMunger(MakeSpecCompliantMaxBundleOffer);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalAudioAndVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

// Test that we can receive the audio output level from a remote audio track.
// TODO(deadbeef): Use a fake audio source and verify that the output level is
// exactly what the source on the other side was configured with.
TEST_P(PeerConnectionIntegrationTest, GetAudioOutputLevelStatsWithOldStatsApi) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  // Just add an audio track.
  caller()->AddAudioTrack();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  // Get the audio output level stats. Note that the level is not available
  // until an RTCP packet has been received.
  EXPECT_THAT(
      WaitUntil([&] { return callee()->OldGetStats()->AudioOutputLevel(); },
                Gt(0), {.timeout = kMaxWaitForFrames}),
      IsRtcOk());
}

// Test that an audio input level is reported.
// TODO(deadbeef): Use a fake audio source and verify that the input level is
// exactly what the source was configured with.
TEST_P(PeerConnectionIntegrationTest, GetAudioInputLevelStatsWithOldStatsApi) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  // Just add an audio track.
  caller()->AddAudioTrack();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  // Get the audio input level stats. The level should be available very
  // soon after the test starts.
  EXPECT_THAT(
      WaitUntil([&] { return caller()->OldGetStats()->AudioInputLevel(); },
                Gt(0), {.timeout = kMaxWaitForStats}),
      IsRtcOk());
}

// Test that we can get incoming byte counts from both audio and video tracks.
TEST_P(PeerConnectionIntegrationTest, GetBytesReceivedStatsWithOldStatsApi) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddAudioVideoTracks();
  // Do offer/answer, wait for the callee to receive some frames.
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  MediaExpectations media_expectations;
  media_expectations.CalleeExpectsSomeAudioAndVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));

  // Get a handle to the remote tracks created, so they can be used as GetStats
  // filters.
  for (const auto& receiver : callee()->pc()->GetReceivers()) {
    // We received frames, so we definitely should have nonzero "received bytes"
    // stats at this point.
    EXPECT_GT(
        callee()->OldGetStatsForTrack(receiver->track().get())->BytesReceived(),
        0);
  }
}

// Test that we can get outgoing byte counts from both audio and video tracks.
TEST_P(PeerConnectionIntegrationTest, GetBytesSentStatsWithOldStatsApi) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  auto audio_track = caller()->CreateLocalAudioTrack();
  auto video_track = caller()->CreateLocalVideoTrack();
  caller()->AddTrack(audio_track);
  caller()->AddTrack(video_track);
  // Do offer/answer, wait for the callee to receive some frames.
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  MediaExpectations media_expectations;
  media_expectations.CalleeExpectsSomeAudioAndVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));

  // The callee received frames, so we definitely should have nonzero "sent
  // bytes" stats at this point.
  EXPECT_GT(caller()->OldGetStatsForTrack(audio_track.get())->BytesSent(), 0);
  EXPECT_GT(caller()->OldGetStatsForTrack(video_track.get())->BytesSent(), 0);
}

// Test that the track ID is associated with all local and remote SSRC stats
// using the old GetStats() and more than 1 audio and more than 1 video track.
// This is a regression test for crbug.com/906988
TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       OldGetStatsAssociatesTrackIdForManyMediaSections) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  auto audio_sender_1 = caller()->AddAudioTrack();
  auto video_sender_1 = caller()->AddVideoTrack();
  auto audio_sender_2 = caller()->AddAudioTrack();
  auto video_sender_2 = caller()->AddVideoTrack();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  MediaExpectations media_expectations;
  media_expectations.CalleeExpectsSomeAudioAndVideo();
  ASSERT_TRUE(WaitUntil([&] { return ExpectNewFrames(media_expectations); }));

  std::vector<std::string> track_ids = {
      audio_sender_1->track()->id(), video_sender_1->track()->id(),
      audio_sender_2->track()->id(), video_sender_2->track()->id()};

  auto caller_stats = caller()->OldGetStats();
  EXPECT_THAT(caller_stats->TrackIds(), UnorderedElementsAreArray(track_ids));
  auto callee_stats = callee()->OldGetStats();
  EXPECT_THAT(callee_stats->TrackIds(), UnorderedElementsAreArray(track_ids));
}

// Test that the new GetStats() returns stats for all outgoing/incoming streams
// with the correct track identifiers if there are more than one audio and more
// than one video senders/receivers.
TEST_P(PeerConnectionIntegrationTest, NewGetStatsManyAudioAndManyVideoStreams) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  auto audio_sender_1 = caller()->AddAudioTrack();
  auto video_sender_1 = caller()->AddVideoTrack();
  auto audio_sender_2 = caller()->AddAudioTrack();
  auto video_sender_2 = caller()->AddVideoTrack();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  MediaExpectations media_expectations;
  media_expectations.CalleeExpectsSomeAudioAndVideo();
  ASSERT_TRUE(WaitUntil([&] { return ExpectNewFrames(media_expectations); }));

  std::vector<std::string> track_ids = {
      audio_sender_1->track()->id(), video_sender_1->track()->id(),
      audio_sender_2->track()->id(), video_sender_2->track()->id()};

  scoped_refptr<const RTCStatsReport> caller_report =
      caller()->NewGetStats(run_loop());
  ASSERT_TRUE(caller_report);
  auto outbound_stream_stats =
      caller_report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  ASSERT_EQ(outbound_stream_stats.size(), 4u);
  std::vector<std::string> outbound_track_ids;
  for (const auto& stat : outbound_stream_stats) {
    ASSERT_TRUE(stat->bytes_sent.has_value());
    EXPECT_LT(0u, *stat->bytes_sent);
    if (*stat->kind == "video") {
      ASSERT_TRUE(stat->key_frames_encoded.has_value());
      EXPECT_GT(*stat->key_frames_encoded, 0u);
      ASSERT_TRUE(stat->frames_encoded.has_value());
      EXPECT_GE(*stat->frames_encoded, *stat->key_frames_encoded);
    }
    ASSERT_TRUE(stat->media_source_id.has_value());
    const RTCMediaSourceStats* media_source =
        static_cast<const RTCMediaSourceStats*>(
            caller_report->Get(*stat->media_source_id));
    ASSERT_TRUE(media_source);
    outbound_track_ids.push_back(*media_source->track_identifier);
  }
  EXPECT_THAT(outbound_track_ids, UnorderedElementsAreArray(track_ids));

  scoped_refptr<const RTCStatsReport> callee_report =
      callee()->NewGetStats(run_loop());
  ASSERT_TRUE(callee_report);
  auto inbound_stream_stats =
      callee_report->GetStatsOfType<RTCInboundRtpStreamStats>();
  ASSERT_EQ(4u, inbound_stream_stats.size());
  std::vector<std::string> inbound_track_ids;
  for (const auto& stat : inbound_stream_stats) {
    ASSERT_TRUE(stat->bytes_received.has_value());
    EXPECT_LT(0u, *stat->bytes_received);
    if (*stat->kind == "video") {
      ASSERT_TRUE(stat->key_frames_decoded.has_value());
      EXPECT_GT(*stat->key_frames_decoded, 0u);
      ASSERT_TRUE(stat->frames_decoded.has_value());
      EXPECT_GE(*stat->frames_decoded, *stat->key_frames_decoded);
    }
    inbound_track_ids.push_back(*stat->track_identifier);
  }
  EXPECT_THAT(inbound_track_ids, UnorderedElementsAreArray(track_ids));
}

// Test that we can get stats (using the new stats implementation) for
// unsignaled streams. Meaning when SSRCs/MSIDs aren't signaled explicitly in
// SDP.
TEST_P(PeerConnectionIntegrationTest,
       GetStatsForUnsignaledStreamWithNewStatsApi) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddAudioTrack();
  // Remove SSRCs and MSIDs from the received offer SDP.
  callee()->SetReceivedSdpMunger(RemoveSsrcsAndMsids);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  MediaExpectations media_expectations;
  media_expectations.CalleeExpectsSomeAudio(1);
  ASSERT_TRUE(ExpectNewFrames(media_expectations));

  // We received a frame, so we should have nonzero "bytes received" stats for
  // the unsignaled stream, if stats are working for it.
  scoped_refptr<const RTCStatsReport> report =
      callee()->NewGetStats(run_loop());
  ASSERT_NE(nullptr, report);
  auto inbound_stream_stats =
      report->GetStatsOfType<RTCInboundRtpStreamStats>();
  ASSERT_EQ(1U, inbound_stream_stats.size());
  ASSERT_TRUE(inbound_stream_stats[0]->bytes_received.has_value());
  ASSERT_GT(*inbound_stream_stats[0]->bytes_received, 0U);
}

// Same as above but for the legacy stats implementation.
TEST_P(PeerConnectionIntegrationTest,
       GetStatsForUnsignaledStreamWithOldStatsApi) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddAudioTrack();
  // Remove SSRCs and MSIDs from the received offer SDP.
  callee()->SetReceivedSdpMunger(RemoveSsrcsAndMsids);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  // Note that, since the old stats implementation associates SSRCs with tracks
  // using SDP, when SSRCs aren't signaled in SDP these stats won't have an
  // associated track ID. So we can't use the track "selector" argument.
  //
  // Also, we use "EXPECT_TRUE_WAIT" because the stats collector may decide to
  // return cached stats if not enough time has passed since the last update.
  EXPECT_THAT(
      WaitUntil([&] { return callee()->OldGetStats()->BytesReceived(); },
                Gt(0)),
      IsRtcOk());
}

// Test that we can successfully get the media related stats (audio level
// etc.) for the unsignaled stream.
TEST_P(PeerConnectionIntegrationTest,
       GetMediaStatsForUnsignaledStreamWithNewStatsApi) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddAudioVideoTracks();
  // Remove SSRCs and MSIDs from the received offer SDP.
  callee()->SetReceivedSdpMunger(RemoveSsrcsAndMsids);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  MediaExpectations media_expectations;
  media_expectations.CalleeExpectsSomeAudio(1);
  media_expectations.CalleeExpectsSomeVideo(1);
  ASSERT_TRUE(ExpectNewFrames(media_expectations));

  scoped_refptr<const RTCStatsReport> report =
      callee()->NewGetStats(run_loop());
  ASSERT_NE(nullptr, report);

  auto inbound_rtps = report->GetStatsOfType<RTCInboundRtpStreamStats>();
  auto index = FindFirstMediaStatsIndexByKind("audio", inbound_rtps);
  ASSERT_GE(index, 0);
  EXPECT_TRUE(inbound_rtps[index]->audio_level.has_value());
}

// Test that DTLS 1.0 is used if both sides only support DTLS 1.0.
TEST_P(PeerConnectionIntegrationTest, EndToEndCallWithDtls10) {
  PeerConnectionFactory::Options dtls_10_options;
  dtls_10_options.ssl_max_version = SSL_PROTOCOL_DTLS_10;
  ASSERT_TRUE(CreatePeerConnectionWrappersWithOptions(dtls_10_options,
                                                      dtls_10_options));
  ConnectFakeSignaling();
  // Do normal offer/answer and wait for some frames to be received in each
  // direction.
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalAudioAndVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

// Test getting cipher stats and UMA metrics when DTLS 1.0 is negotiated.
TEST_P(PeerConnectionIntegrationTest, Dtls10CipherStatsAndUmaMetrics) {
  PeerConnectionFactory::Options dtls_10_options;
  dtls_10_options.ssl_max_version = SSL_PROTOCOL_DTLS_10;
  ASSERT_TRUE(CreatePeerConnectionWrappersWithOptions(dtls_10_options,
                                                      dtls_10_options));
  ConnectFakeSignaling();
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return DtlsConnected(); }));
  EXPECT_TRUE(WaitUntil([&] {
    return SSLStreamAdapter::IsAcceptableCipher(caller()->DtlsCipher(),
                                                KT_DEFAULT);
  }));
  EXPECT_THAT(WaitUntil([&] { return caller()->SrtpCipher(); },
                        Eq(SrtpCryptoSuiteToName(kDefaultSrtpCryptoSuite))),
              IsRtcOk());
}

// Test getting cipher stats and UMA metrics when DTLS 1.2 is negotiated.
TEST_P(PeerConnectionIntegrationTest, Dtls12CipherStatsAndUmaMetrics) {
  PeerConnectionFactory::Options dtls_12_options;
  dtls_12_options.ssl_max_version = SSL_PROTOCOL_DTLS_12;
  ASSERT_TRUE(CreatePeerConnectionWrappersWithOptions(dtls_12_options,
                                                      dtls_12_options));
  ConnectFakeSignaling();
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return DtlsConnected(); }));
  EXPECT_TRUE(WaitUntil([&] {
    return SSLStreamAdapter::IsAcceptableCipher(caller()->DtlsCipher(),
                                                KT_DEFAULT);
  }));
  EXPECT_THAT(WaitUntil([&] { return caller()->SrtpCipher(); },
                        Eq(SrtpCryptoSuiteToName(kDefaultSrtpCryptoSuite))),
              IsRtcOk());
}

// Test that DTLS 1.0 can be used if the caller supports DTLS 1.2 and the
// callee only supports 1.0.
TEST_P(PeerConnectionIntegrationTest, CallerDtls12ToCalleeDtls10) {
  PeerConnectionFactory::Options caller_options;
  caller_options.ssl_max_version = SSL_PROTOCOL_DTLS_12;
  PeerConnectionFactory::Options callee_options;
  callee_options.ssl_max_version = SSL_PROTOCOL_DTLS_10;
  ASSERT_TRUE(
      CreatePeerConnectionWrappersWithOptions(caller_options, callee_options));
  ConnectFakeSignaling();
  // Do normal offer/answer and wait for some frames to be received in each
  // direction.
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalAudioAndVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

// Test that DTLS 1.0 can be used if the caller only supports DTLS 1.0 and the
// callee supports 1.2.
TEST_P(PeerConnectionIntegrationTest, CallerDtls10ToCalleeDtls12) {
  PeerConnectionFactory::Options caller_options;
  caller_options.ssl_max_version = SSL_PROTOCOL_DTLS_10;
  PeerConnectionFactory::Options callee_options;
  callee_options.ssl_max_version = SSL_PROTOCOL_DTLS_12;
  ASSERT_TRUE(
      CreatePeerConnectionWrappersWithOptions(caller_options, callee_options));
  ConnectFakeSignaling();
  // Do normal offer/answer and wait for some frames to be received in each
  // direction.
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalAudioAndVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

// The three tests below verify that "enable_aes128_sha1_32_crypto_cipher"
// works as expected; the cipher should only be used if enabled by both sides.
TEST_P(PeerConnectionIntegrationTest,
       Aes128Sha1_32_CipherNotUsedWhenOnlyCallerSupported) {
  RTCConfiguration caller_config;
  CryptoOptions caller_crypto;
  caller_crypto.srtp.enable_aes128_sha1_32_crypto_cipher = true;
  caller_config.crypto_options = caller_crypto;
  RTCConfiguration callee_config;
  CryptoOptions callee_crypto;
  callee_crypto.srtp.enable_aes128_sha1_32_crypto_cipher = false;
  callee_config.crypto_options = callee_crypto;
  int expected_cipher_suite = kSrtpAes128CmSha1_80;
  TestNegotiatedCipherSuite(caller_config, callee_config,
                            expected_cipher_suite);
}

TEST_P(PeerConnectionIntegrationTest,
       Aes128Sha1_32_CipherNotUsedWhenOnlyCalleeSupported) {
  RTCConfiguration caller_config;
  CryptoOptions caller_crypto;
  caller_crypto.srtp.enable_aes128_sha1_32_crypto_cipher = false;
  caller_config.crypto_options = caller_crypto;
  RTCConfiguration callee_config;
  CryptoOptions callee_crypto;
  callee_crypto.srtp.enable_aes128_sha1_32_crypto_cipher = true;
  callee_config.crypto_options = callee_crypto;
  int expected_cipher_suite = kSrtpAes128CmSha1_80;
  TestNegotiatedCipherSuite(caller_config, callee_config,
                            expected_cipher_suite);
}

TEST_P(PeerConnectionIntegrationTest, Aes128Sha1_32_CipherUsedWhenSupported) {
  RTCConfiguration caller_config;
  CryptoOptions caller_crypto;
  caller_crypto.srtp.enable_aes128_sha1_32_crypto_cipher = true;
  caller_config.crypto_options = caller_crypto;
  RTCConfiguration callee_config;
  CryptoOptions callee_crypto;
  callee_crypto.srtp.enable_aes128_sha1_32_crypto_cipher = true;
  callee_config.crypto_options = callee_crypto;
  int expected_cipher_suite = kSrtpAes128CmSha1_32;
  TestNegotiatedCipherSuite(caller_config, callee_config,
                            expected_cipher_suite);
}

// Test that a non-GCM cipher is used if both sides only support non-GCM.
TEST_P(PeerConnectionIntegrationTest, NonGcmCipherUsedWhenGcmNotSupported) {
  bool local_gcm_enabled = false;
  bool remote_gcm_enabled = false;
  bool aes_ctr_enabled = true;
  int expected_cipher_suite = kDefaultSrtpCryptoSuite;
  TestGcmNegotiationUsesCipherSuite(local_gcm_enabled, remote_gcm_enabled,
                                    aes_ctr_enabled, expected_cipher_suite);
}

// Test that a GCM cipher is used if both ends support it and non-GCM is
// disabled.
TEST_P(PeerConnectionIntegrationTest, GcmCipherUsedWhenOnlyGcmSupported) {
  bool local_gcm_enabled = true;
  bool remote_gcm_enabled = true;
  bool aes_ctr_enabled = false;
  int expected_cipher_suite = kDefaultSrtpCryptoSuiteGcm;
  TestGcmNegotiationUsesCipherSuite(local_gcm_enabled, remote_gcm_enabled,
                                    aes_ctr_enabled, expected_cipher_suite);
}

// Verify that media can be transmitted end-to-end when GCM crypto suites are
// enabled. Note that the above tests, such as GcmCipherUsedWhenGcmSupported,
// only verify that a GCM cipher is negotiated, and not necessarily that SRTP
// works with it.
TEST_P(PeerConnectionIntegrationTest, EndToEndCallWithGcmCipher) {
  RTCConfiguration gcm_config;
  CryptoOptions gcm_options;
  gcm_options.srtp.enable_gcm_crypto_suites = true;
  gcm_options.srtp.enable_aes128_sha1_80_crypto_cipher = false;
  gcm_config.crypto_options = gcm_options;
  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfig(gcm_config, gcm_config));
  ConnectFakeSignaling();
  // Do normal offer/answer and wait for some frames to be received in each
  // direction.
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalAudioAndVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

// Test that the ICE connection and gathering states eventually reach
// "complete".
TEST_P(PeerConnectionIntegrationTest, IceStatesReachCompletion) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  // Do normal offer/answer.
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  EXPECT_THAT(WaitUntil([&] { return caller()->ice_gathering_state(); },
                        Eq(PeerConnectionInterface::kIceGatheringComplete),
                        {.timeout = kMaxWaitForFrames}),
              IsRtcOk());
  EXPECT_THAT(WaitUntil([&] { return callee()->ice_gathering_state(); },
                        Eq(PeerConnectionInterface::kIceGatheringComplete),
                        {.timeout = kMaxWaitForFrames}),
              IsRtcOk());
  // After the best candidate pair is selected and all candidates are signaled,
  // the ICE connection state should reach "complete".
  // TODO(deadbeef): Currently, the ICE "controlled" agent (the
  // answerer/"callee" by default) only reaches "connected". When this is
  // fixed, this test should be updated.
  EXPECT_THAT(WaitUntil([&] { return caller()->ice_connection_state(); },
                        Eq(PeerConnectionInterface::kIceConnectionCompleted)),
              IsRtcOk());
  EXPECT_THAT(WaitUntil([&] { return callee()->ice_connection_state(); },
                        Eq(PeerConnectionInterface::kIceConnectionConnected)),
              IsRtcOk());
}

constexpr int kOnlyLocalPorts = PORTALLOCATOR_DISABLE_STUN |
                                PORTALLOCATOR_DISABLE_RELAY |
                                PORTALLOCATOR_DISABLE_TCP;

// Use a mock resolver to resolve the hostname back to the original IP on both
// sides and check that the ICE connection connects.
TEST_P(PeerConnectionIntegrationTest,
       IceStatesReachCompletionWithRemoteHostname) {
  auto caller_resolver_factory =
      std::make_unique<NiceMock<MockAsyncDnsResolverFactory>>();
  auto callee_resolver_factory =
      std::make_unique<NiceMock<MockAsyncDnsResolverFactory>>();
  auto callee_async_resolver =
      std::make_unique<NiceMock<MockAsyncDnsResolver>>();
  auto caller_async_resolver =
      std::make_unique<NiceMock<MockAsyncDnsResolver>>();
  // Keep raw pointers to the mock resolvers, for use after init,
  // where the std::unique_ptr values have been moved away.
  auto* callee_resolver_ptr = callee_async_resolver.get();
  auto* caller_resolver_ptr = caller_async_resolver.get();

  // This also verifies that the injected AsyncResolverFactory is used by
  // P2PTransportChannel.
  EXPECT_CALL(*caller_resolver_factory, Create())
      .WillOnce(Return(ByMove(std::move(caller_async_resolver))));
  PeerConnectionDependencies caller_deps(nullptr);
  caller_deps.async_dns_resolver_factory = std::move(caller_resolver_factory);

  // Create() will be called twice.
  EXPECT_CALL(*callee_resolver_factory, Create())
      .WillOnce(Return(ByMove(std::move(callee_async_resolver))))
      .WillOnce(
          Return(ByMove(std::make_unique<NiceMock<MockAsyncDnsResolver>>())));
  PeerConnectionDependencies callee_deps(nullptr);
  callee_deps.async_dns_resolver_factory = std::move(callee_resolver_factory);

  PeerConnectionInterface::RTCConfiguration config;
  config.bundle_policy = PeerConnectionInterface::kBundlePolicyMaxBundle;
  config.rtcp_mux_policy = PeerConnectionInterface::kRtcpMuxPolicyRequire;
  config.port_allocator_config.flags = kOnlyLocalPorts;

  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfigAndDeps(
      config, std::move(caller_deps), config, std::move(callee_deps)));

  // TEMP NOTE - this is probably bogus since the resolvers have been
  // moved out of their slots prior to these code lines.
  RTC_LOG(LS_ERROR) << "callee async resolver is "
                    << callee_async_resolver.get();
  caller()->SetRemoteAsyncResolver(callee_resolver_ptr);
  callee()->SetRemoteAsyncResolver(caller_resolver_ptr);

  // Enable hostname candidates with mDNS names.
  caller()->SetMdnsResponder(
      std::make_unique<FakeMdnsResponder>(network_thread()));
  callee()->SetMdnsResponder(
      std::make_unique<FakeMdnsResponder>(network_thread()));

  ConnectFakeSignaling();
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  EXPECT_THAT(WaitUntil([&] { return caller()->ice_connection_state(); },
                        Eq(PeerConnectionInterface::kIceConnectionCompleted)),
              IsRtcOk());
  EXPECT_THAT(WaitUntil([&] { return callee()->ice_connection_state(); },
                        Eq(PeerConnectionInterface::kIceConnectionConnected)),
              IsRtcOk());

  // Part of reporting the stats will occur on the network thread, so flush it
  // before checking NumEvents.
  SendTask(network_thread(), [] {});

  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.PeerConnection.CandidatePairType_UDP",
                            kIceCandidatePairHostNameHostName));
  DestroyPeerConnections();
}

// Test that firewalling the ICE connection causes the clients to identify the
// disconnected state and then removing the firewall causes them to reconnect.

using PeerConnectionIntegrationIceStatesTest =
    PeerConnectionIntegrationIceStatesTestBase<
        PeerConnectionIntegrationBaseTest>;

using PeerConnectionIntegrationIceStatesTestWithFakeClock =
    PeerConnectionIntegrationIceStatesTestBase<
        PeerConnectionIntegrationTestWithSimulatedTime>;

#if !defined(THREAD_SANITIZER)
// This test provokes TSAN errors. bugs.webrtc.org/11282

// Tests that if the connection doesn't get set up properly we eventually reach
// the "failed" iceConnectionState.
TEST_P(PeerConnectionIntegrationIceStatesTestWithFakeClock,
       IceStateSetupFailure) {
  // Block connections to/from the caller and wait for ICE to become
  // disconnected.
  for (const auto& caller_address : CallerAddresses()) {
    firewall()->AddRule(false, FP_ANY, FD_ANY, caller_address);
  }

  PeerConnectionInterface::RTCConfiguration config;
  config.port_allocator_config.flags = port_allocator_flags();
  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfig(config, config));
  ConnectFakeSignaling();
  SetUpNetworkInterfaces();
  caller()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();

  // According to RFC7675, if there is no response within 30 seconds then the
  // peer should consider the other side to have rejected the connection. This
  // is signaled by the state transitioning to "failed".
  constexpr TimeDelta kConsentTimeout = TimeDelta::Millis(30000);
  ASSERT_THAT(
      WaitUntil([&] { return caller()->standardized_ice_connection_state(); },
                Eq(PeerConnectionInterface::kIceConnectionFailed),
                {.timeout = kConsentTimeout,
                 .polling_interval = TimeDelta::Millis(100),
                 .clock = time_controller_.get()}),
      IsRtcOk());
}

#endif  // !defined(THREAD_SANITIZER)

// Tests that the best connection is set to the appropriate IPv4/IPv6 connection
// and that the statistics in the metric observers are updated correctly.
// TODO(bugs.webrtc.org/12591): Flaky on Windows.
#if defined(WEBRTC_WIN)
#define MAYBE_VerifyBestConnection DISABLED_VerifyBestConnection
#else
#define MAYBE_VerifyBestConnection VerifyBestConnection
#endif
TEST_P(PeerConnectionIntegrationIceStatesTest, MAYBE_VerifyBestConnection) {
  PeerConnectionInterface::RTCConfiguration config;
  config.port_allocator_config.flags = port_allocator_flags();
  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfig(config, config));
  ConnectFakeSignaling();
  SetUpNetworkInterfaces();
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();

  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  EXPECT_THAT(WaitUntil([&] { return caller()->ice_connection_state(); },
                        Eq(PeerConnectionInterface::kIceConnectionCompleted)),
              IsRtcOk());
  EXPECT_THAT(WaitUntil([&] { return callee()->ice_connection_state(); },
                        Eq(PeerConnectionInterface::kIceConnectionConnected)),
              IsRtcOk());

  // Part of reporting the stats will occur on the network thread, so flush it
  // before checking NumEvents.
  SendTask(network_thread(), [] {});

  // TODO(bugs.webrtc.org/9456): Fix it.
  const int num_best_ipv4 = metrics::NumEvents(
      "WebRTC.PeerConnection.IPMetrics", kBestConnections_IPv4);
  const int num_best_ipv6 = metrics::NumEvents(
      "WebRTC.PeerConnection.IPMetrics", kBestConnections_IPv6);
  if (TestIPv6()) {
    // When IPv6 is enabled, we should prefer an IPv6 connection over an IPv4
    // connection.
    EXPECT_METRIC_EQ(0, num_best_ipv4);
    EXPECT_METRIC_EQ(1, num_best_ipv6);
  } else {
    EXPECT_METRIC_EQ(1, num_best_ipv4);
    EXPECT_METRIC_EQ(0, num_best_ipv6);
  }

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  EXPECT_METRIC_EQ(
      0, metrics::NumEvents("WebRTC.PeerConnection.CandidatePairType_UDP",
                            kIceCandidatePairHostHost));
#pragma clang diagnostic pop
  EXPECT_METRIC_EQ(
      1, metrics::NumEvents("WebRTC.PeerConnection.CandidatePairType_UDP",
                            kIceCandidatePairHostPublicHostPublic));
}

constexpr uint32_t kFlagsIPv4NoStun = PORTALLOCATOR_DISABLE_TCP |
                                      PORTALLOCATOR_DISABLE_STUN |
                                      PORTALLOCATOR_DISABLE_RELAY;
constexpr uint32_t kFlagsIPv6NoStun =
    PORTALLOCATOR_DISABLE_TCP | PORTALLOCATOR_DISABLE_STUN |
    PORTALLOCATOR_ENABLE_IPV6 | PORTALLOCATOR_DISABLE_RELAY;
constexpr uint32_t kFlagsIPv4Stun =
    PORTALLOCATOR_DISABLE_TCP | PORTALLOCATOR_DISABLE_RELAY;

INSTANTIATE_TEST_SUITE_P(
    PeerConnectionIntegrationTest,
    PeerConnectionIntegrationIceStatesTest,
    Combine(Values(SdpSemantics::kPlanB_DEPRECATED, SdpSemantics::kUnifiedPlan),
            Values(std::make_pair("IPv4 no STUN", kFlagsIPv4NoStun),
                   std::make_pair("IPv6 no STUN", kFlagsIPv6NoStun),
                   std::make_pair("IPv4 with STUN", kFlagsIPv4Stun))));

INSTANTIATE_TEST_SUITE_P(
    PeerConnectionIntegrationTest,
    PeerConnectionIntegrationIceStatesTestWithFakeClock,
    Combine(Values(SdpSemantics::kPlanB_DEPRECATED, SdpSemantics::kUnifiedPlan),
            Values(std::make_pair("IPv4 no STUN", kFlagsIPv4NoStun),
                   std::make_pair("IPv6 no STUN", kFlagsIPv6NoStun),
                   std::make_pair("IPv4 with STUN", kFlagsIPv4Stun))));

// This test sets up a call between two parties with audio and video.
// During the call, the caller restarts ICE and a candidate with the same remote
// address is applied on the callee side. This test verifies that there is
// no ICE disconnection for both caller and callee during the ICE restart.
TEST_P(PeerConnectionIntegrationTest, NoDisconnectionDuringIceRestart) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  // Do normal offer/answer and wait for ICE to complete.
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  EXPECT_THAT(WaitUntil([&] { return caller()->ice_connection_state(); },
                        Eq(PeerConnectionInterface::kIceConnectionCompleted),
                        {.timeout = kMaxWaitForFrames}),
              IsRtcOk());
  EXPECT_THAT(WaitUntil([&] { return callee()->ice_connection_state(); },
                        Eq(PeerConnectionInterface::kIceConnectionConnected),
                        {.timeout = kMaxWaitForFrames}),
              IsRtcOk());

  // Save the caller's address for injection into callee's candidate.
  const IceCandidateCollection* audio_candidates_caller =
      caller()->pc()->local_description()->candidates(0);
  const SocketAddress caller_address_pre_restart =
      audio_candidates_caller->at(0)->candidate().address();

  // Have the caller initiate an ICE restart.
  caller()->SetOfferAnswerOptions(IceRestartOfferAnswerOptions());

  // Trigger the code path for the case where a new ICE generation has a
  // candidate with the same address as the old generation for
  // https://issues.webrtc.org/issues/543082385
  // In this simulated network the restart does not naturally reuse the
  // caller's transport address (VirtualSocketServer assigns a new ephemeral
  // port per bind, so re-gathered candidates get a new port), hence the
  // candidate is injected explicitly.
  Candidate same_address_candidate;
  same_address_candidate.set_component(ICE_CANDIDATE_COMPONENT_DEFAULT);
  same_address_candidate.set_protocol(UDP_PROTOCOL_NAME);
  same_address_candidate.set_address(caller_address_pre_restart);
  // The candidate belongs to the new ICE generation, so it replaces the old
  // connection instead of being treated as a duplicate candidate.
  same_address_candidate.set_generation(1);
  std::optional<RTCError> add_candidate_result;
  callee()->SetRemoteOfferHandler([&] {
    callee()->pc()->AddIceCandidate(
        CreateIceCandidate("", 0, same_address_candidate),
        [&add_candidate_result](RTCError r) { add_candidate_result = r; });
  });

  caller()->CreateAndSetAndSignalOffer();

  // Verify that the same-address candidate is applied inside
  // setRemoteDescription(offer) on the callee.
  ASSERT_TRUE(WaitUntil([&] { return add_candidate_result.has_value(); },
                        {.timeout = kMaxWaitForFrames}));
  ASSERT_TRUE(add_candidate_result.value().ok());

  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); },
                        {.timeout = kMaxWaitForFrames}));
  EXPECT_THAT(WaitUntil([&] { return caller()->ice_connection_state(); },
                        Eq(PeerConnectionInterface::kIceConnectionCompleted),
                        {.timeout = kMaxWaitForFrames}),
              IsRtcOk());
  EXPECT_THAT(WaitUntil([&] { return callee()->ice_connection_state(); },
                        Eq(PeerConnectionInterface::kIceConnectionConnected),
                        {.timeout = kMaxWaitForFrames}),
              IsRtcOk());

  // The caller and callee should not have disconnected. Both sides should be
  // able to send data during the ICE restart, per RFC 8445.
  EXPECT_THAT(
      caller()->ice_connection_state_history(),
      Not(Contains(PeerConnectionInterface::kIceConnectionDisconnected)));
  EXPECT_THAT(
      callee()->ice_connection_state_history(),
      Not(Contains(PeerConnectionInterface::kIceConnectionDisconnected)));
}

// This test sets up a call between two parties with audio and video.
// During the call, the caller restarts ICE and the test verifies that
// new ICE candidates are generated and audio and video still can flow, and the
// ICE state reaches completed again.
TEST_P(PeerConnectionIntegrationTest, MediaContinuesFlowingAfterIceRestart) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  // Do normal offer/answer and wait for ICE to complete.
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  EXPECT_THAT(WaitUntil([&] { return caller()->ice_connection_state(); },
                        Eq(PeerConnectionInterface::kIceConnectionCompleted),
                        {.timeout = kMaxWaitForFrames}),
              IsRtcOk());
  EXPECT_THAT(WaitUntil([&] { return callee()->ice_connection_state(); },
                        Eq(PeerConnectionInterface::kIceConnectionConnected),
                        {.timeout = kMaxWaitForFrames}),
              IsRtcOk());
  // To verify that the ICE restart actually occurs, get
  // ufrag/password/candidates before and after restart.
  // Create an SDP string of the first audio candidate for both clients.
  const IceCandidateCollection* audio_candidates_caller =
      caller()->pc()->local_description()->candidates(0);
  const IceCandidateCollection* audio_candidates_callee =
      callee()->pc()->local_description()->candidates(0);
  ASSERT_GT(audio_candidates_caller->count(), 0u);
  ASSERT_GT(audio_candidates_callee->count(), 0u);
  std::string caller_candidate_pre_restart =
      audio_candidates_caller->at(0)->ToString();
  std::string callee_candidate_pre_restart =
      audio_candidates_callee->at(0)->ToString();
  const SessionDescription* desc =
      caller()->pc()->local_description()->description();
  std::string caller_ufrag_pre_restart =
      desc->transport_infos()[0].description.ice_ufrag;
  desc = callee()->pc()->local_description()->description();
  std::string callee_ufrag_pre_restart =
      desc->transport_infos()[0].description.ice_ufrag;
  EXPECT_EQ(caller()->ice_candidate_pair_change_history().size(), 1u);
  // Have the caller initiate an ICE restart.
  caller()->SetOfferAnswerOptions(IceRestartOfferAnswerOptions());
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  EXPECT_THAT(WaitUntil([&] { return caller()->ice_connection_state(); },
                        Eq(PeerConnectionInterface::kIceConnectionCompleted),
                        {.timeout = kMaxWaitForFrames}),
              IsRtcOk());
  EXPECT_THAT(WaitUntil([&] { return callee()->ice_connection_state(); },
                        Eq(PeerConnectionInterface::kIceConnectionConnected),
                        {.timeout = kMaxWaitForFrames}),
              IsRtcOk());
  // Grab the ufrags/candidates again.
  audio_candidates_caller = caller()->pc()->local_description()->candidates(0);
  audio_candidates_callee = callee()->pc()->local_description()->candidates(0);
  ASSERT_GT(audio_candidates_caller->count(), 0u);
  ASSERT_GT(audio_candidates_callee->count(), 0u);
  std::string caller_candidate_post_restart =
      audio_candidates_caller->at(0)->ToString();
  std::string callee_candidate_post_restart =
      audio_candidates_callee->at(0)->ToString();
  desc = caller()->pc()->local_description()->description();
  std::string caller_ufrag_post_restart =
      desc->transport_infos()[0].description.ice_ufrag;
  desc = callee()->pc()->local_description()->description();
  std::string callee_ufrag_post_restart =
      desc->transport_infos()[0].description.ice_ufrag;
  // Sanity check that an ICE restart was actually negotiated in SDP.
  ASSERT_NE(caller_candidate_pre_restart, caller_candidate_post_restart);
  ASSERT_NE(callee_candidate_pre_restart, callee_candidate_post_restart);
  ASSERT_NE(caller_ufrag_pre_restart, caller_ufrag_post_restart);
  ASSERT_NE(callee_ufrag_pre_restart, callee_ufrag_post_restart);
  EXPECT_THAT(
      WaitUntil(
          [&] { return caller()->ice_candidate_pair_change_history().size(); },
          Gt(1U), {.timeout = kMaxWaitForFrames}),
      IsRtcOk());
  // Ensure that additional frames are received after the ICE restart.
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalAudioAndVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

// Verify that audio/video can be received end-to-end when ICE renomination is
// enabled.
TEST_P(PeerConnectionIntegrationTest, EndToEndCallWithIceRenomination) {
  PeerConnectionInterface::RTCConfiguration config;
  config.enable_ice_renomination = true;
  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfig(config, config));
  ConnectFakeSignaling();
  // Do normal offer/answer and wait for some frames to be received in each
  // direction.
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  // Sanity check that ICE renomination was actually negotiated.
  const SessionDescription* desc =
      caller()->pc()->local_description()->description();
  for (const TransportInfo& info : desc->transport_infos()) {
    ASSERT_THAT(info.description.transport_options, Contains("renomination"));
  }
  desc = callee()->pc()->local_description()->description();
  for (const TransportInfo& info : desc->transport_infos()) {
    ASSERT_THAT(info.description.transport_options, Contains("renomination"));
  }
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalAudioAndVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

// With a max bundle policy and RTCP muxing, adding a new media description to
// the connection should not affect ICE at all because the new media will use
// the existing connection.
TEST_P(PeerConnectionIntegrationTest,
       AddMediaToConnectedBundleDoesNotRestartIce) {
  PeerConnectionInterface::RTCConfiguration config;
  config.bundle_policy = PeerConnectionInterface::kBundlePolicyMaxBundle;
  config.rtcp_mux_policy = PeerConnectionInterface::kRtcpMuxPolicyRequire;
  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfig(
      config, PeerConnectionInterface::RTCConfiguration()));
  ConnectFakeSignaling();

  caller()->AddAudioTrack();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_THAT(WaitUntil([&] { return caller()->ice_connection_state(); },
                        Eq(PeerConnectionInterface::kIceConnectionCompleted)),
              IsRtcOk());

  caller()->clear_ice_connection_state_history();

  caller()->AddVideoTrack();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  EXPECT_EQ(0u, caller()->ice_connection_state_history().size());
}

// This test sets up a call between two parties with audio and video. It then
// renegotiates setting the video m-line to "port 0", then later renegotiates
// again, enabling video.
TEST_P(PeerConnectionIntegrationTest,
       VideoFlowsAfterMediaSectionIsRejectedAndRecycled) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();

  // Do initial negotiation, only sending media from the caller. Will result in
  // video and audio recvonly "m=" sections.
  caller()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  // Negotiate again, disabling the video "m=" section (the callee will set the
  // port to 0 due to offer_to_receive_video = 0).
  if (sdp_semantics_ == SdpSemantics::kPlanB_DEPRECATED) {
    PeerConnectionInterface::RTCOfferAnswerOptions options;
    options.offer_to_receive_video = 0;
    callee()->SetOfferAnswerOptions(options);
  } else {
    callee()->SetRemoteOfferHandler([this] {
      callee()->GetFirstTransceiverOfType(MediaType::VIDEO)->StopInternal();
    });
  }
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  // Sanity check that video "m=" section was actually rejected.
  const ContentInfo* answer_video_content =
      GetFirstVideoContent(callee()->pc()->local_description()->description());
  ASSERT_NE(nullptr, answer_video_content);
  ASSERT_TRUE(answer_video_content->rejected);

  // Enable video and do negotiation again, making sure video is received
  // end-to-end, also adding media stream to callee.
  if (sdp_semantics_ == SdpSemantics::kPlanB_DEPRECATED) {
    PeerConnectionInterface::RTCOfferAnswerOptions options;
    options.offer_to_receive_video = 1;
    callee()->SetOfferAnswerOptions(options);
  } else {
    // The caller's transceiver is stopped, so we need to add another track.
    auto caller_transceiver =
        caller()->GetFirstTransceiverOfType(MediaType::VIDEO);
    EXPECT_EQ(nullptr, caller_transceiver.get());
    caller()->AddVideoTrack();
  }
  callee()->AddVideoTrack();
  callee()->SetRemoteOfferHandler(nullptr);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  // Verify the caller receives frames from the newly added stream, and the
  // callee receives additional frames from the re-enabled video m= section.
  MediaExpectations media_expectations;
  media_expectations.CalleeExpectsSomeAudio();
  media_expectations.ExpectBidirectionalVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

// This tests that if we negotiate after calling CreateSender but before we
// have a track, then set a track later, frames from the newly-set track are
// received end-to-end.
TEST_F(PeerConnectionIntegrationTestPlanB,
       MediaFlowsAfterEarlyWarmupWithCreateSender) {
  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN()
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  auto caller_audio_sender =
      caller()->pc()->CreateSender("audio", "caller_stream");
  auto caller_video_sender =
      caller()->pc()->CreateSender("video", "caller_stream");
  auto callee_audio_sender =
      callee()->pc()->CreateSender("audio", "callee_stream");
  auto callee_video_sender =
      callee()->pc()->CreateSender("video", "callee_stream");
  RTC_ALLOW_PLAN_B_DEPRECATION_END()
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); },
                        {.timeout = kMaxWaitForActivation}));
  // Wait for ICE to complete, without any tracks being set.
  EXPECT_THAT(WaitUntil([&] { return caller()->ice_connection_state(); },
                        Eq(PeerConnectionInterface::kIceConnectionCompleted),
                        {.timeout = kMaxWaitForFrames}),
              IsRtcOk());
  EXPECT_THAT(WaitUntil([&] { return callee()->ice_connection_state(); },
                        Eq(PeerConnectionInterface::kIceConnectionConnected),
                        {.timeout = kMaxWaitForFrames}),
              IsRtcOk());
  // Now set the tracks, and expect frames to immediately start flowing.
  EXPECT_TRUE(
      caller_audio_sender->SetTrack(caller()->CreateLocalAudioTrack().get()));
  EXPECT_TRUE(
      caller_video_sender->SetTrack(caller()->CreateLocalVideoTrack().get()));
  EXPECT_TRUE(
      callee_audio_sender->SetTrack(callee()->CreateLocalAudioTrack().get()));
  EXPECT_TRUE(
      callee_video_sender->SetTrack(callee()->CreateLocalVideoTrack().get()));
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalAudioAndVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

// This tests that if we negotiate after calling AddTransceiver but before we
// have a track, then set a track later, frames from the newly-set tracks are
// received end-to-end.
TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       MediaFlowsAfterEarlyWarmupWithAddTransceiver) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  auto audio_result = caller()->pc()->AddTransceiver(MediaType::AUDIO);
  ASSERT_EQ(RTCErrorType::NONE, audio_result.error().type());
  auto caller_audio_sender = audio_result.MoveValue()->sender();
  auto video_result = caller()->pc()->AddTransceiver(MediaType::VIDEO);
  ASSERT_EQ(RTCErrorType::NONE, video_result.error().type());
  auto caller_video_sender = video_result.MoveValue()->sender();
  callee()->SetRemoteOfferHandler([this] {
    ASSERT_EQ(2u, callee()->pc()->GetTransceivers().size());
    callee()->pc()->GetTransceivers()[0]->SetDirectionWithError(
        RtpTransceiverDirection::kSendRecv);
    callee()->pc()->GetTransceivers()[1]->SetDirectionWithError(
        RtpTransceiverDirection::kSendRecv);
  });
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); },
                        {.timeout = kMaxWaitForActivation}));
  // Wait for ICE to complete, without any tracks being set.
  EXPECT_THAT(WaitUntil([&] { return caller()->ice_connection_state(); },
                        Eq(PeerConnectionInterface::kIceConnectionCompleted),
                        {.timeout = kMaxWaitForFrames}),
              IsRtcOk());
  EXPECT_THAT(WaitUntil([&] { return callee()->ice_connection_state(); },
                        Eq(PeerConnectionInterface::kIceConnectionConnected),
                        {.timeout = kMaxWaitForFrames}),
              IsRtcOk());
  // Now set the tracks, and expect frames to immediately start flowing.
  auto callee_audio_sender = callee()->pc()->GetSenders()[0];
  auto callee_video_sender = callee()->pc()->GetSenders()[1];
  ASSERT_TRUE(
      caller_audio_sender->SetTrack(caller()->CreateLocalAudioTrack().get()));
  ASSERT_TRUE(
      caller_video_sender->SetTrack(caller()->CreateLocalVideoTrack().get()));
  ASSERT_TRUE(
      callee_audio_sender->SetTrack(callee()->CreateLocalAudioTrack().get()));
  ASSERT_TRUE(
      callee_video_sender->SetTrack(callee()->CreateLocalVideoTrack().get()));
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalAudioAndVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

// This test verifies that a remote video track can be added via AddStream,
// and sent end-to-end. For this particular test, it's simply echoed back
// from the caller to the callee, rather than being forwarded to a third
// PeerConnection.
TEST_F(PeerConnectionIntegrationTestPlanB, CanSendRemoteVideoTrack) {
  RTC_ALLOW_PLAN_B_DEPRECATION_BEGIN()
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  // Just send a video track from the caller.
  caller()->AddVideoTrack();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); },
                        {.timeout = kMaxWaitForActivation}));
  ASSERT_EQ(1U, callee()->remote_streams()->count());

  // Echo the stream back, and do a new offer/anwer (initiated by callee this
  // time).
  callee()->pc()->AddStream(callee()->remote_streams()->at(0));
  RTC_ALLOW_PLAN_B_DEPRECATION_END()
  callee()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); },
                        {.timeout = kMaxWaitForActivation}));

  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

#if !defined(THREAD_SANITIZER)
// This test provokes TSAN errors. bugs.webrtc.org/11282

// Test that we achieve the expected end-to-end connection time, using a
// fake clock and simulated latency on the media and signaling paths.
// We use a TURN<->TURN connection because this is usually the quickest to
// set up initially, especially when we're confident the connection will work
// and can start sending media before we get a STUN response.
//
// With various optimizations enabled, here are the network delays we expect to
// be on the critical path:
// 1. 2 signaling trips: Signaling offer and offerer's TURN candidate, then
//                       signaling answer (with DTLS fingerprint).
// 2. 9 media hops: Rest of the DTLS handshake. 3 hops in each direction when
//                  using TURN<->TURN pair, and DTLS exchange is 4 packets,
//                  the first of which should have arrived before the answer.
TEST_P(PeerConnectionIntegrationTestWithFakeClock,
       EndToEndConnectionTimeWithTurnTurnPair) {
  static constexpr int media_hop_delay_ms = 50;
  static constexpr int signaling_trip_delay_ms = 500;
  // For explanation of these values, see comment above.
  static constexpr int required_media_hops = 9;
  static constexpr int required_signaling_trips = 2;
  // For internal delays (such as posting an event asynchronously).
  static constexpr int allowed_internal_delay_ms = 20;
  static constexpr int total_connection_time_ms =
      media_hop_delay_ms * required_media_hops +
      signaling_trip_delay_ms * required_signaling_trips +
      allowed_internal_delay_ms;

  static const SocketAddress turn_server_1_internal_address{"88.88.88.0", 3478};
  static const SocketAddress turn_server_1_external_address{"88.88.88.1", 0};
  static const SocketAddress turn_server_2_internal_address{"99.99.99.0", 3478};
  static const SocketAddress turn_server_2_external_address{"99.99.99.1", 0};
  TestTurnServer* turn_server_1 = CreateTurnServer(
      turn_server_1_internal_address, turn_server_1_external_address);

  TestTurnServer* turn_server_2 = CreateTurnServer(
      turn_server_2_internal_address, turn_server_2_external_address);
  // Bypass permission check on received packets so media can be sent before
  // the candidate is signaled.
  SendTask(network_thread(), [turn_server_1] {
    turn_server_1->set_enable_permission_checks(false);
  });
  SendTask(network_thread(), [turn_server_2] {
    turn_server_2->set_enable_permission_checks(false);
  });

  PeerConnectionInterface::RTCConfiguration client_1_config;
  PeerConnectionInterface::IceServer ice_server_1;
  ice_server_1.urls.push_back("turn:88.88.88.0:3478");
  ice_server_1.username = "test";
  ice_server_1.password = "test";
  client_1_config.servers.push_back(ice_server_1);
  client_1_config.type = PeerConnectionInterface::kRelay;
  client_1_config.presume_writable_when_fully_relayed = true;

  PeerConnectionInterface::RTCConfiguration client_2_config;
  PeerConnectionInterface::IceServer ice_server_2;
  ice_server_2.urls.push_back("turn:99.99.99.0:3478");
  ice_server_2.username = "test";
  ice_server_2.password = "test";
  client_2_config.servers.push_back(ice_server_2);
  client_2_config.type = PeerConnectionInterface::kRelay;
  client_2_config.presume_writable_when_fully_relayed = true;

  ASSERT_TRUE(
      CreatePeerConnectionWrappersWithConfig(client_1_config, client_2_config));
  // Set up the simulated delays.
  SetSignalingDelayMs(signaling_trip_delay_ms);
  ConnectFakeSignaling();
  virtual_socket_server()->set_delay_mean(media_hop_delay_ms);
  virtual_socket_server()->UpdateDelayDistribution();

  // Set "offer to receive audio/video" without adding any tracks, so we just
  // set up ICE/DTLS with no media.
  PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.offer_to_receive_audio = 1;
  options.offer_to_receive_video = 1;
  caller()->SetOfferAnswerOptions(options);
  caller()->CreateAndSetAndSignalOffer();
  EXPECT_TRUE(WaitUntil([&] { return DtlsConnected(); },
                        {.timeout = TimeDelta::Millis(total_connection_time_ms),
                         .clock = time_controller_.get()}));
  // Closing the PeerConnections destroys the ports before the ScopedFakeClock.
  // If this is not done a DCHECK can be hit in ports.cc, because a large
  // negative number is calculated for the rtt due to the global clock changing.
  ClosePeerConnections();
}

TEST_P(PeerConnectionIntegrationTestWithFakeClock,
       OnIceCandidateFlushesGetStatsCache) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddAudioTrack();

  // Call getStats, assert there are no candidates.
  scoped_refptr<const RTCStatsReport> first_report =
      caller()->NewGetStats({.clock = time_controller_.get()});
  ASSERT_TRUE(first_report);
  auto first_candidate_stats =
      first_report->GetStatsOfType<RTCLocalIceCandidateStats>();
  ASSERT_EQ(first_candidate_stats.size(), 0u);

  // Create an offer at the caller and set it as remote description on the
  // callee.
  caller()->CreateAndSetAndSignalOffer();
  // Call getStats again, assert there are candidates now.
  scoped_refptr<const RTCStatsReport> second_report =
      caller()->NewGetStats({.clock = time_controller_.get()});
  ASSERT_TRUE(second_report);
  auto second_candidate_stats =
      second_report->GetStatsOfType<RTCLocalIceCandidateStats>();
  ASSERT_NE(second_candidate_stats.size(), 0u);

  // The fake clock ensures that no time has passed so the cache must have been
  // explicitly invalidated.
  EXPECT_EQ(first_report->timestamp(), second_report->timestamp());
}

TEST_P(PeerConnectionIntegrationTestWithFakeClock,
       AddIceCandidateFlushesGetStatsCache) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignalingForSdpOnly();
  caller()->AddAudioTrack();

  // Start candidate gathering and wait for it to complete. Candidates are not
  // signalled.
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return caller()->IceGatheringStateComplete(); },
                        {.clock = time_controller_.get()}));

  // Call getStats, assert there are no candidates.
  scoped_refptr<const RTCStatsReport> first_report =
      caller()->NewGetStats({.clock = time_controller_.get()});
  ASSERT_TRUE(first_report);
  auto first_candidate_stats =
      first_report->GetStatsOfType<RTCRemoteIceCandidateStats>();
  ASSERT_EQ(first_candidate_stats.size(), 0u);

  // Add a "fake" candidate.
  std::optional<RTCError> result;
  caller()->pc()->AddIceCandidate(
      absl::WrapUnique(CreateIceCandidate(
          "", 0,
          "candidate:2214029314 1 udp 2122260223 127.0.0.1 49152 typ host",
          nullptr)),
      [&result](RTCError r) { result = r; });
  ASSERT_TRUE(WaitUntil([&] { return result.has_value(); },
                        {.clock = time_controller_.get()}));
  ASSERT_TRUE(result.value().ok());

  // Call getStats again, assert there is a remote candidate now.
  scoped_refptr<const RTCStatsReport> second_report =
      caller()->NewGetStats({.clock = time_controller_.get()});
  ASSERT_TRUE(second_report);
  auto second_candidate_stats =
      second_report->GetStatsOfType<RTCRemoteIceCandidateStats>();
  ASSERT_EQ(second_candidate_stats.size(), 1u);

  // The fake clock ensures that no time has passed so the cache must have been
  // explicitly invalidated.
  EXPECT_EQ(first_report->timestamp(), second_report->timestamp());
}

#endif  // !defined(THREAD_SANITIZER)

// Verify that a TurnCustomizer passed in through RTCConfiguration
// is actually used by the underlying TURN candidate pair.
// Note that turnport_unittest.cc contains more detailed, lower-level tests.
TEST_P(PeerConnectionIntegrationTest, TurnCustomizerUsedForTurnConnections) {
  static const SocketAddress turn_server_1_internal_address{"88.88.88.0", 3478};
  static const SocketAddress turn_server_1_external_address{"88.88.88.1", 0};
  static const SocketAddress turn_server_2_internal_address{"99.99.99.0", 3478};
  static const SocketAddress turn_server_2_external_address{"99.99.99.1", 0};
  CreateTurnServer(turn_server_1_internal_address,
                   turn_server_1_external_address);
  CreateTurnServer(turn_server_2_internal_address,
                   turn_server_2_external_address);

  PeerConnectionInterface::RTCConfiguration client_1_config;
  PeerConnectionInterface::IceServer ice_server_1;
  ice_server_1.urls.push_back("turn:88.88.88.0:3478");
  ice_server_1.username = "test";
  ice_server_1.password = "test";
  client_1_config.servers.push_back(ice_server_1);
  client_1_config.type = PeerConnectionInterface::kRelay;
  auto* customizer1 = CreateTurnCustomizer();
  client_1_config.turn_customizer = customizer1;

  PeerConnectionInterface::RTCConfiguration client_2_config;
  PeerConnectionInterface::IceServer ice_server_2;
  ice_server_2.urls.push_back("turn:99.99.99.0:3478");
  ice_server_2.username = "test";
  ice_server_2.password = "test";
  client_2_config.servers.push_back(ice_server_2);
  client_2_config.type = PeerConnectionInterface::kRelay;
  auto* customizer2 = CreateTurnCustomizer();
  client_2_config.turn_customizer = customizer2;

  ASSERT_TRUE(
      CreatePeerConnectionWrappersWithConfig(client_1_config, client_2_config));
  ConnectFakeSignaling();

  // Set "offer to receive audio/video" without adding any tracks, so we just
  // set up ICE/DTLS with no media.
  PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.offer_to_receive_audio = 1;
  options.offer_to_receive_video = 1;
  caller()->SetOfferAnswerOptions(options);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return DtlsConnected(); }));

  ExpectTurnCustomizerCountersIncremented(customizer1);
  ExpectTurnCustomizerCountersIncremented(customizer2);
}

// Verifies that you can use TCP instead of UDP to connect to a TURN server and
// send media between the caller and the callee.
TEST_P(PeerConnectionIntegrationTest, TCPUsedForTurnConnections) {
  static const SocketAddress turn_server_internal_address{"88.88.88.0", 3478};
  static const SocketAddress turn_server_external_address{"88.88.88.1", 0};

  // Enable TCP for the fake turn server.
  CreateTurnServer(turn_server_internal_address, turn_server_external_address,
                   PROTO_TCP);

  PeerConnectionInterface::IceServer ice_server;
  ice_server.urls.push_back("turn:88.88.88.0:3478?transport=tcp");
  ice_server.username = "test";
  ice_server.password = "test";

  PeerConnectionInterface::RTCConfiguration client_1_config;
  client_1_config.servers.push_back(ice_server);
  client_1_config.type = PeerConnectionInterface::kRelay;

  PeerConnectionInterface::RTCConfiguration client_2_config;
  client_2_config.servers.push_back(ice_server);
  client_2_config.type = PeerConnectionInterface::kRelay;

  ASSERT_TRUE(
      CreatePeerConnectionWrappersWithConfig(client_1_config, client_2_config));

  // Do normal offer/answer and wait for ICE to complete.
  ConnectFakeSignaling();
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  EXPECT_THAT(WaitUntil([&] { return callee()->ice_connection_state(); },
                        Eq(PeerConnectionInterface::kIceConnectionConnected),
                        {.timeout = kMaxWaitForFrames}),
              IsRtcOk());

  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalAudioAndVideo();
  EXPECT_TRUE(ExpectNewFrames(media_expectations));
}

// Verify that a SSLCertificateVerifier passed in through
// PeerConnectionDependencies is actually used by the underlying SSL
// implementation to determine whether a certificate presented by the TURN
// server is accepted by the client. Note that openssladapter_unittest.cc
// contains more detailed, lower-level tests.
TEST_P(PeerConnectionIntegrationTest,
       SSLCertificateVerifierUsedForTurnConnections) {
  static const SocketAddress turn_server_internal_address{"88.88.88.0", 3478};
  static const SocketAddress turn_server_external_address{"88.88.88.1", 0};

  // Enable TCP-TLS for the fake turn server. We need to pass in 88.88.88.0 so
  // that host name verification passes on the fake certificate.
  CreateTurnServer(turn_server_internal_address, turn_server_external_address,
                   PROTO_TLS, "88.88.88.0");

  PeerConnectionInterface::IceServer ice_server;
  ice_server.urls.push_back("turns:88.88.88.0:3478?transport=tcp");
  ice_server.username = "test";
  ice_server.password = "test";

  PeerConnectionInterface::RTCConfiguration client_1_config;
  client_1_config.servers.push_back(ice_server);
  client_1_config.type = PeerConnectionInterface::kRelay;

  PeerConnectionInterface::RTCConfiguration client_2_config;
  client_2_config.servers.push_back(ice_server);
  // Setting the type to kRelay forces the connection to go through a TURN
  // server.
  client_2_config.type = PeerConnectionInterface::kRelay;

  // Get a copy to the pointer so we can verify calls later.
  TestCertificateVerifier* client_1_cert_verifier =
      new TestCertificateVerifier();
  client_1_cert_verifier->verify_certificate_ = true;
  TestCertificateVerifier* client_2_cert_verifier =
      new TestCertificateVerifier();
  client_2_cert_verifier->verify_certificate_ = true;

  // Create the dependencies with the test certificate verifier.
  PeerConnectionDependencies client_1_deps(nullptr);
  client_1_deps.tls_cert_verifier =
      std::unique_ptr<TestCertificateVerifier>(client_1_cert_verifier);
  PeerConnectionDependencies client_2_deps(nullptr);
  client_2_deps.tls_cert_verifier =
      std::unique_ptr<TestCertificateVerifier>(client_2_cert_verifier);

  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfigAndDeps(
      client_1_config, std::move(client_1_deps), client_2_config,
      std::move(client_2_deps)));
  ConnectFakeSignaling();

  // Set "offer to receive audio/video" without adding any tracks, so we just
  // set up ICE/DTLS with no media.
  PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.offer_to_receive_audio = 1;
  options.offer_to_receive_video = 1;
  caller()->SetOfferAnswerOptions(options);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return DtlsConnected(); }));

  EXPECT_GT(client_1_cert_verifier->call_count_, 0u);
  EXPECT_GT(client_2_cert_verifier->call_count_, 0u);
}

// Test that the injected ICE transport factory is used to create ICE transports
// for WebRTC connections.
TEST_P(PeerConnectionIntegrationTest, IceTransportFactoryUsedForConnections) {
  PeerConnectionInterface::RTCConfiguration default_config;
  PeerConnectionDependencies dependencies(nullptr);
  auto ice_transport_factory = std::make_unique<MockIceTransportFactory>();
  EXPECT_CALL(*ice_transport_factory, RecordIceTransportCreated()).Times(1);
  dependencies.ice_transport_factory = std::move(ice_transport_factory);
  auto wrapper = CreatePeerConnectionWrapper("Caller", nullptr, &default_config,
                                             std::move(dependencies), nullptr,
                                             /*reset_encoder_factory=*/false,
                                             /*reset_decoder_factory=*/false);
  ASSERT_TRUE(wrapper);
  wrapper->CreateDataChannel();
  auto observer = make_ref_counted<MockSetSessionDescriptionObserver>();
  wrapper->pc()->SetLocalDescription(observer.get(),
                                     wrapper->CreateOfferAndWait().release());
}

// Test that audio and video flow end-to-end when codec names don't use the
// expected casing, given that they're supposed to be case insensitive. To test
// this, all but one codec is removed from each media description, and its
// casing is changed.
//
// In the past, this has regressed and caused crashes/black video, due to the
// fact that code at some layers was doing case-insensitive comparisons and
// code at other layers was not.
TEST_P(PeerConnectionIntegrationTest, CodecNamesAreCaseInsensitive) {
  SetFieldTrials("WebRTC-NoSdpMangleAllowForTesting/Enabled,60,80/");
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();

  // Remove all but one audio/video codec (opus and VP8), and change the
  // casing of the caller's generated offer.
  caller()->SetGeneratedSdpMunger(
      [](std::unique_ptr<SessionDescriptionInterface>& sdp) {
        AudioContentDescription* audio =
            GetFirstAudioContentDescription(sdp->description());
        ASSERT_NE(nullptr, audio);
        auto audio_codecs = audio->codecs();
        std::erase_if(audio_codecs,
                      [](const Codec& codec) { return codec.name != "opus"; });
        ASSERT_EQ(1u, audio_codecs.size());
        audio_codecs[0].name = "OpUs";
        audio->set_codecs(audio_codecs);

        VideoContentDescription* video =
            GetFirstVideoContentDescription(sdp->description());
        ASSERT_NE(nullptr, video);
        auto video_codecs = video->codecs();
        std::erase_if(video_codecs,
                      [](const Codec& codec) { return codec.name != "VP8"; });
        ASSERT_EQ(1u, video_codecs.size());
        video_codecs[0].name = "vP8";
        video->set_codecs(video_codecs);
      });

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  // Verify frames are still received end-to-end.
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalAudioAndVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

TEST_P(PeerConnectionIntegrationTest, GetSourcesAudio) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddAudioTrack();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  // Wait for one audio frame to be received by the callee.
  MediaExpectations media_expectations;
  media_expectations.CalleeExpectsSomeAudio(1);
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
  ASSERT_EQ(callee()->pc()->GetReceivers().size(), 1u);
  auto receiver = callee()->pc()->GetReceivers()[0];
  ASSERT_EQ(receiver->media_type(), MediaType::AUDIO);
  std::vector<RtpSource> sources;
  EXPECT_TRUE(WaitUntil([&] {
    sources = receiver->GetSources();
    return !sources.empty();
  }));
  ASSERT_GT(receiver->GetParameters().encodings.size(), 0u);
  EXPECT_EQ(receiver->GetParameters().encodings[0].ssrc,
            sources[0].source_id());
  EXPECT_EQ(RtpSourceType::SSRC, sources[0].source_type());
}

TEST_P(PeerConnectionIntegrationTest, GetSourcesVideo) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddVideoTrack();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  // Wait for two video frames to be received by the callee.
  // TODO: https://issues.webrtc.org/42220900 - wait for only one frame again
  MediaExpectations media_expectations;
  media_expectations.CalleeExpectsSomeVideo(2);
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
  ASSERT_EQ(callee()->pc()->GetReceivers().size(), 1u);
  auto receiver = callee()->pc()->GetReceivers()[0];
  ASSERT_EQ(receiver->media_type(), MediaType::VIDEO);
  std::vector<RtpSource> sources;
  EXPECT_TRUE(WaitUntil([&] {
    sources = receiver->GetSources();
    return !sources.empty();
  }));
  ASSERT_GT(receiver->GetParameters().encodings.size(), 0u);
  ASSERT_GT(sources.size(), 0u);
  EXPECT_EQ(receiver->GetParameters().encodings[0].ssrc,
            sources[0].source_id());
  EXPECT_EQ(RtpSourceType::SSRC, sources[0].source_type());
}

TEST_P(PeerConnectionIntegrationTest, ConcurrentUnsignaledSsrcPackets) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddAudioTrack();
  callee()->SetReceivedSdpMunger(RemoveSsrcsAndMsids);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  // Wait until the connection is established.
  ASSERT_TRUE(WaitUntil([&] { return DtlsConnected(); }));

  // Wait deterministically for the packets to be delivered and processed.
  MediaExpectations media_expectations;
  media_expectations.CalleeExpectsSomeAudio(1);
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

TEST_P(PeerConnectionIntegrationTest, UnsignaledSsrcGetSourcesAudio) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddAudioTrack();
  callee()->SetReceivedSdpMunger(RemoveSsrcsAndMsids);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_EQ(callee()->pc()->GetReceivers().size(), 1u);
  auto receiver = callee()->pc()->GetReceivers()[0];
  std::vector<RtpSource> sources;
  EXPECT_TRUE(WaitUntil([&] {
    return ([&receiver, &sources]() {
      sources = receiver->GetSources();
      return !sources.empty();
    })();
  }));
  ASSERT_GT(sources.size(), 0u);
  EXPECT_EQ(RtpSourceType::SSRC, sources[0].source_type());
}

TEST_P(PeerConnectionIntegrationTest, UnsignaledSsrcGetSourcesVideo) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddVideoTrack();
  callee()->SetReceivedSdpMunger(RemoveSsrcsAndMsids);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_EQ(callee()->pc()->GetReceivers().size(), 1u);
  auto receiver = callee()->pc()->GetReceivers()[0];
  std::vector<RtpSource> sources;
  EXPECT_TRUE(WaitUntil([&] {
    return ([&receiver, &sources]() {
      sources = receiver->GetSources();
      return !sources.empty();
    })();
  }));
  ASSERT_GT(sources.size(), 0u);
  EXPECT_EQ(RtpSourceType::SSRC, sources[0].source_type());
}

// Similar to the above test, except instead of waiting until GetSources() is
// non-empty we wait until media is flowing and then assert that GetSources()
// is not empty. This provides test coverage for https://crbug.com/webrtc/14817
// where a race due to the re-creation of the unsignaled ssrc stream would
// clear the GetSources() history. This test not flaking confirms the bug fix.
// TODO(crbug.com/webrtc/441652589): Figure out why this is flaking and
// re-enable the test.
TEST_P(PeerConnectionIntegrationTest,
       UnsignaledSsrcGetSourcesNonEmptyIfMediaFlowing) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddVideoTrack();
  callee()->SetReceivedSdpMunger(RemoveSsrcsAndMsids);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  // Wait for one video frame to be received by the callee.
  MediaExpectations media_expectations;
  media_expectations.CalleeExpectsSomeVideo(1);
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
  ASSERT_EQ(callee()->pc()->GetReceivers().size(), 1u);
  auto receiver = callee()->pc()->GetReceivers()[0];

  ASSERT_TRUE(WaitUntil([&] { return !receiver->GetSources().empty(); }));

  std::vector<RtpSource> sources = receiver->GetSources();
  // SSRC history must not be cleared since the reception of the first frame.
  ASSERT_GT(sources.size(), 0u);
  EXPECT_EQ(RtpSourceType::SSRC, sources[0].source_type());
}

TEST_P(PeerConnectionIntegrationTest, UnsignaledSsrcGetParametersAudio) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddAudioTrack();
  callee()->SetReceivedSdpMunger(RemoveSsrcsAndMsids);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_EQ(callee()->pc()->GetReceivers().size(), 1u);
  auto receiver = callee()->pc()->GetReceivers()[0];
  RtpParameters parameters;
  EXPECT_TRUE(WaitUntil([&] {
    return ([&receiver, &parameters]() {
      parameters = receiver->GetParameters();
      return !parameters.encodings.empty() &&
             parameters.encodings[0].ssrc.has_value();
    })();
  }));
  ASSERT_EQ(parameters.encodings.size(), 1u);
  EXPECT_TRUE(parameters.encodings[0].ssrc.has_value());
}

TEST_P(PeerConnectionIntegrationTest, UnsignaledSsrcGetParametersVideo) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddVideoTrack();
  callee()->SetReceivedSdpMunger(RemoveSsrcsAndMsids);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_EQ(callee()->pc()->GetReceivers().size(), 1u);
  auto receiver = callee()->pc()->GetReceivers()[0];
  RtpParameters parameters;
  EXPECT_TRUE(WaitUntil([&] {
    return ([&receiver, &parameters]() {
      parameters = receiver->GetParameters();
      return !parameters.encodings.empty() &&
             parameters.encodings[0].ssrc.has_value();
    })();
  }));
  ASSERT_EQ(parameters.encodings.size(), 1u);
  EXPECT_TRUE(parameters.encodings[0].ssrc.has_value());
}

TEST_P(PeerConnectionIntegrationTest,
       GetParametersHasEncodingsBeforeNegotiation) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  auto sender = caller()->AddTrack(caller()->CreateLocalVideoTrack());
  auto parameters = sender->GetParameters();
  EXPECT_EQ(parameters.encodings.size(), 1u);
}

// Test that if a track is removed and added again with a different stream ID,
// the new stream ID is successfully communicated in SDP and media continues to
// flow end-to-end.
// TODO(webrtc.bugs.org/8734): This test does not work for Unified Plan because
// it will not reuse a transceiver that has already been sending. After creating
// a new transceiver it tries to create an offer with two senders of the same
// track ids and it fails.
TEST_F(PeerConnectionIntegrationTestPlanB, RemoveAndAddTrackWithNewStreamId) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();

  // Add track using stream 1, do offer/answer.
  scoped_refptr<AudioTrackInterface> track = caller()->CreateLocalAudioTrack();
  scoped_refptr<RtpSenderInterface> sender =
      caller()->AddTrack(track, {"stream_1"});
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  {
    MediaExpectations media_expectations;
    media_expectations.CalleeExpectsSomeAudio(1);
    ASSERT_TRUE(ExpectNewFrames(media_expectations));
  }
  // Remove the sender, and create a new one with the new stream.
  caller()->pc()->RemoveTrackOrError(sender);
  sender = caller()->AddTrack(track, {"stream_2"});
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  // Wait for additional audio frames to be received by the callee.
  {
    MediaExpectations media_expectations;
    media_expectations.CalleeExpectsSomeAudio();
    ASSERT_TRUE(ExpectNewFrames(media_expectations));
  }
}

TEST_P(PeerConnectionIntegrationTest, RtcEventLogOutputWriteCalled) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();

  auto output = std::make_unique<NiceMock<MockRtcEventLogOutput>>();
  ON_CALL(*output, IsActive).WillByDefault(Return(true));
  ON_CALL(*output, Write).WillByDefault(Return(true));
  EXPECT_CALL(*output, Write).Times(AtLeast(1));
  EXPECT_TRUE(caller()->pc()->StartRtcEventLog(std::move(output),
                                               RtcEventLog::kImmediateOutput));

  caller()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
}

TEST_P(PeerConnectionIntegrationTest, RtcEventLogOutputWriteCalledOnStop) {
  // This test uses check point to ensure log is written before peer connection
  // is destroyed.
  // https://google.github.io/googletest/gmock_cook_book.html#UsingCheckPoints
  MockFunction<void()> test_is_complete;
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();

  auto output = std::make_unique<NiceMock<MockRtcEventLogOutput>>();
  ON_CALL(*output, IsActive).WillByDefault(Return(true));
  ON_CALL(*output, Write).WillByDefault(Return(true));
  InSequence s;
  EXPECT_CALL(*output, Write).Times(AtLeast(1));
  EXPECT_CALL(test_is_complete, Call);

  // Use large output period to prevent this test pass for the wrong reason.
  EXPECT_TRUE(caller()->pc()->StartRtcEventLog(std::move(output),
                                               /*output_period_ms=*/100'000));

  caller()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  caller()->pc()->StopRtcEventLog();
  test_is_complete.Call();
}

TEST_P(PeerConnectionIntegrationTest, RtcEventLogOutputWriteCalledOnClose) {
  // This test uses check point to ensure log is written before peer connection
  // is destroyed.
  // https://google.github.io/googletest/gmock_cook_book.html#UsingCheckPoints
  MockFunction<void()> test_is_complete;
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();

  auto output = std::make_unique<NiceMock<MockRtcEventLogOutput>>();
  ON_CALL(*output, IsActive).WillByDefault(Return(true));
  ON_CALL(*output, Write).WillByDefault(Return(true));
  InSequence s;
  EXPECT_CALL(*output, Write).Times(AtLeast(1));
  EXPECT_CALL(test_is_complete, Call);

  // Use large output period to prevent this test pass for the wrong reason.
  EXPECT_TRUE(caller()->pc()->StartRtcEventLog(std::move(output),
                                               /*output_period_ms=*/100'000));

  caller()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  caller()->pc()->Close();
  test_is_complete.Call();
}

// Test that if candidates are only signaled by applying full session
// descriptions (instead of using AddIceCandidate), the peers can connect to
// each other and exchange media.
TEST_P(PeerConnectionIntegrationTest, MediaFlowsWhenCandidatesSetOnlyInSdp) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  // Each side will signal the session descriptions but not candidates.
  ConnectFakeSignalingForSdpOnly();

  // Add audio video track and exchange the initial offer/answer with media
  // information only. This will start ICE gathering on each side.
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();

  // Wait for all candidates to be gathered on both the caller and callee.
  ASSERT_THAT(WaitUntil([&] { return caller()->ice_gathering_state(); },
                        Eq(PeerConnectionInterface::kIceGatheringComplete)),
              IsRtcOk());
  ASSERT_THAT(WaitUntil([&] { return callee()->ice_gathering_state(); },
                        Eq(PeerConnectionInterface::kIceGatheringComplete)),
              IsRtcOk());

  // The candidates will now be included in the session description, so
  // signaling them will start the ICE connection.
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  // Ensure that media flows in both directions.
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalAudioAndVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

#if !defined(THREAD_SANITIZER)
// These tests provokes TSAN errors. See bugs.webrtc.org/11305.

// Test that SetAudioPlayout can be used to disable audio playout from the
// start, then later enable it. This may be useful, for example, if the caller
// needs to play a local ringtone until some event occurs, after which it
// switches to playing the received audio.
TEST_P(PeerConnectionIntegrationTest, DisableAndEnableAudioPlayout) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();

  // Set up audio-only call where audio playout is disabled on caller's side.
  caller()->pc()->SetAudioPlayout(false);
  caller()->AddAudioTrack();
  callee()->AddAudioTrack();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  // Pump messages for a second.
  run_loop().RunFor(TimeDelta::Seconds(1));

  // Since audio playout is disabled, the caller shouldn't have received
  // anything (at the playout level, at least).
  EXPECT_EQ(0, caller()->audio_frames_received());
  // As a sanity check, make sure the callee (for which playout isn't disabled)
  // did still see frames on its audio level.
  ASSERT_GT(callee()->audio_frames_received(), 0);

  // Enable playout again, and ensure audio starts flowing.
  caller()->pc()->SetAudioPlayout(true);
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalAudio();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

double GetAudioEnergyStat(PeerConnectionIntegrationWrapper* pc) {
  auto report = pc->NewGetStats();
  auto inbound_rtps = report->GetStatsOfType<RTCInboundRtpStreamStats>();
  if (inbound_rtps.empty()) {
    return 0.0;
  }
  auto* inbound_rtp = inbound_rtps[0];
  if (!inbound_rtp->total_audio_energy.has_value()) {
    return 0.0;
  }
  return *inbound_rtp->total_audio_energy;
}

// Test that if audio playout is disabled via the SetAudioPlayout() method, then
// incoming audio is still processed and statistics are generated.
TEST_P(PeerConnectionIntegrationTest,
       DisableAudioPlayoutStillGeneratesAudioStats) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();

  // Set up audio-only call where playout is disabled but audio-processing is
  // still active.
  caller()->AddAudioTrack();
  callee()->AddAudioTrack();
  caller()->pc()->SetAudioPlayout(false);

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  // Wait for the callee to receive audio stats.
  EXPECT_THAT(WaitUntil([&] { return GetAudioEnergyStat(caller()); }, Gt(0),
                        {.timeout = kMaxWaitForFrames}),
              IsRtcOk());
}

#endif  // !defined(THREAD_SANITIZER)

// Test that SetAudioRecording can be used to disable audio recording from the
// start, then later enable it. This may be useful, for example, if the caller
// wants to ensure that no audio resources are active before a certain state
// is reached.
TEST_P(PeerConnectionIntegrationTest, DisableAndEnableAudioRecording) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();

  // Set up audio-only call where audio recording is disabled on caller's side.
  caller()->pc()->SetAudioRecording(false);
  caller()->AddAudioTrack();
  callee()->AddAudioTrack();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  // Pump messages for a second.
  run_loop().RunFor(TimeDelta::Seconds(1));
  // Since caller has disabled audio recording, the callee shouldn't have
  // received anything.
  EXPECT_EQ(0, callee()->audio_frames_received());
  // As a sanity check, make sure the caller did still see frames on its
  // audio level since audio recording is enabled on the calle side.
  ASSERT_GT(caller()->audio_frames_received(), 0);

  // Enable audio recording again, and ensure audio starts flowing.
  caller()->pc()->SetAudioRecording(true);
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalAudio();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

TEST_P(PeerConnectionIntegrationTest,
       IceEventsGeneratedAndLoggedInRtcEventLog) {
  ASSERT_TRUE(CreatePeerConnectionWrappersWithFakeRtcEventLog());
  ConnectFakeSignaling();
  PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.offer_to_receive_audio = 1;
  caller()->SetOfferAnswerOptions(options);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return DtlsConnected(); }));
  ASSERT_NE(nullptr, caller()->event_log_factory());
  ASSERT_NE(nullptr, callee()->event_log_factory());
  FakeRtcEventLog* caller_event_log =
      caller()->event_log_factory()->last_log_created();
  FakeRtcEventLog* callee_event_log =
      callee()->event_log_factory()->last_log_created();
  ASSERT_NE(nullptr, caller_event_log);
  ASSERT_NE(nullptr, callee_event_log);
  int caller_ice_config_count =
      caller_event_log->GetEventCount(RtcEvent::Type::IceCandidatePairConfig);
  int caller_ice_event_count =
      caller_event_log->GetEventCount(RtcEvent::Type::IceCandidatePairEvent);
  int callee_ice_config_count =
      callee_event_log->GetEventCount(RtcEvent::Type::IceCandidatePairConfig);
  int callee_ice_event_count =
      callee_event_log->GetEventCount(RtcEvent::Type::IceCandidatePairEvent);
  EXPECT_LT(0, caller_ice_config_count);
  EXPECT_LT(0, caller_ice_event_count);
  EXPECT_LT(0, callee_ice_config_count);
  EXPECT_LT(0, callee_ice_event_count);
}

TEST_P(PeerConnectionIntegrationTest, RegatherAfterChangingIceTransportType) {
  static const SocketAddress turn_server_internal_address{"88.88.88.0", 3478};
  static const SocketAddress turn_server_external_address{"88.88.88.1", 0};

  CreateTurnServer(turn_server_internal_address, turn_server_external_address);

  PeerConnectionInterface::IceServer ice_server;
  ice_server.urls.push_back("turn:88.88.88.0:3478");
  ice_server.username = "test";
  ice_server.password = "test";

  PeerConnectionInterface::RTCConfiguration caller_config;
  caller_config.servers.push_back(ice_server);
  caller_config.type = PeerConnectionInterface::kRelay;
  caller_config.continual_gathering_policy = PeerConnection::GATHER_CONTINUALLY;
  caller_config.surface_ice_candidates_on_ice_transport_type_changed = true;

  PeerConnectionInterface::RTCConfiguration callee_config;
  callee_config.servers.push_back(ice_server);
  callee_config.type = PeerConnectionInterface::kRelay;
  callee_config.continual_gathering_policy = PeerConnection::GATHER_CONTINUALLY;
  callee_config.surface_ice_candidates_on_ice_transport_type_changed = true;

  ASSERT_TRUE(
      CreatePeerConnectionWrappersWithConfig(caller_config, callee_config));

  // Do normal offer/answer and wait for ICE to complete.
  ConnectFakeSignaling();
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  // Since we are doing continual gathering, the ICE transport does not reach
  // kIceGatheringComplete (see
  // P2PTransportChannel::OnCandidatesAllocationDone), and consequently not
  // kIceConnectionComplete.
  EXPECT_THAT(WaitUntil([&] { return caller()->ice_connection_state(); },
                        Eq(PeerConnectionInterface::kIceConnectionConnected)),
              IsRtcOk());
  EXPECT_THAT(WaitUntil([&] { return callee()->ice_connection_state(); },
                        Eq(PeerConnectionInterface::kIceConnectionConnected)),
              IsRtcOk());
  // Note that we cannot use the metric
  // `WebRTC.PeerConnection.CandidatePairType_UDP` in this test since this
  // metric is only populated when we reach kIceConnectionComplete in the
  // current implementation.
  EXPECT_TRUE(caller()->last_candidate_gathered().is_relay());
  EXPECT_TRUE(callee()->last_candidate_gathered().is_relay());

  // Loosen the caller's candidate filter.
  caller_config = caller()->pc()->GetConfiguration();
  caller_config.type = PeerConnectionInterface::kAll;
  caller()->pc()->SetConfiguration(caller_config);
  // We should have gathered a new host candidate.
  EXPECT_TRUE(WaitUntil(
      [&] { return caller()->last_candidate_gathered().is_local(); }));

  // Loosen the callee's candidate filter.
  callee_config = callee()->pc()->GetConfiguration();
  callee_config.type = PeerConnectionInterface::kAll;
  callee()->pc()->SetConfiguration(callee_config);
  EXPECT_TRUE(WaitUntil(
      [&] { return callee()->last_candidate_gathered().is_local(); }));

  // Create an offer and verify that it does not contain an ICE restart (i.e new
  // ice credentials).
  std::string caller_ufrag_pre_offer = caller()
                                           ->pc()
                                           ->local_description()
                                           ->description()
                                           ->transport_infos()[0]
                                           .description.ice_ufrag;
  caller()->CreateAndSetAndSignalOffer();
  std::string caller_ufrag_post_offer = caller()
                                            ->pc()
                                            ->local_description()
                                            ->description()
                                            ->transport_infos()[0]
                                            .description.ice_ufrag;
  EXPECT_EQ(caller_ufrag_pre_offer, caller_ufrag_post_offer);
}

TEST_P(PeerConnectionIntegrationTest, OnIceCandidateError) {
  static const SocketAddress turn_server_internal_address{"88.88.88.0", 3478};
  static const SocketAddress turn_server_external_address{"88.88.88.1", 0};

  CreateTurnServer(turn_server_internal_address, turn_server_external_address);

  PeerConnectionInterface::IceServer ice_server;
  ice_server.urls.push_back("turn:88.88.88.0:3478");
  ice_server.username = "test";
  ice_server.password = "123";

  PeerConnectionInterface::RTCConfiguration caller_config;
  caller_config.servers.push_back(ice_server);
  caller_config.type = PeerConnectionInterface::kRelay;
  caller_config.continual_gathering_policy = PeerConnection::GATHER_CONTINUALLY;

  PeerConnectionInterface::RTCConfiguration callee_config;
  callee_config.servers.push_back(ice_server);
  callee_config.type = PeerConnectionInterface::kRelay;
  callee_config.continual_gathering_policy = PeerConnection::GATHER_CONTINUALLY;

  ASSERT_TRUE(
      CreatePeerConnectionWrappersWithConfig(caller_config, callee_config));

  // Do normal offer/answer and wait for ICE to complete.
  ConnectFakeSignaling();
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  EXPECT_THAT(
      WaitUntil([&] { return caller()->error_event().error_code; }, Eq(401)),
      IsRtcOk());
  EXPECT_EQ("Unauthorized.", caller()->error_event().error_text);
  EXPECT_EQ("turn:88.88.88.0:3478?transport=udp", caller()->error_event().url);
  EXPECT_NE(caller()->error_event().address, "");
}

TEST_P(PeerConnectionIntegrationTest, OnIceCandidateErrorWithEmptyAddress) {
  PeerConnectionInterface::IceServer ice_server;
  ice_server.urls.push_back("turn:127.0.0.1:3478?transport=tcp");
  ice_server.username = "test";
  ice_server.password = "test";

  PeerConnectionInterface::RTCConfiguration caller_config;
  caller_config.servers.push_back(ice_server);
  caller_config.type = PeerConnectionInterface::kRelay;
  caller_config.continual_gathering_policy = PeerConnection::GATHER_CONTINUALLY;

  PeerConnectionInterface::RTCConfiguration callee_config;
  callee_config.servers.push_back(ice_server);
  callee_config.type = PeerConnectionInterface::kRelay;
  callee_config.continual_gathering_policy = PeerConnection::GATHER_CONTINUALLY;

  ASSERT_TRUE(
      CreatePeerConnectionWrappersWithConfig(caller_config, callee_config));

  // Do normal offer/answer and wait for ICE to complete.
  ConnectFakeSignaling();
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  EXPECT_THAT(
      WaitUntil([&] { return caller()->error_event().error_code; }, Eq(701)),
      IsRtcOk());
  EXPECT_EQ(caller()->error_event().address, "");
}

TEST_P(PeerConnectionIntegrationTest,
       OnIceCandidateErrorWithEmptyAddressForTurns) {
  PeerConnectionInterface::IceServer ice_server;
  ice_server.urls.push_back("turns:127.0.0.1:3478?transport=tcp");
  ice_server.username = "test";
  ice_server.password = "test";

  PeerConnectionInterface::RTCConfiguration caller_config;
  caller_config.servers.push_back(ice_server);
  caller_config.type = PeerConnectionInterface::kRelay;
  caller_config.continual_gathering_policy = PeerConnection::GATHER_CONTINUALLY;

  PeerConnectionInterface::RTCConfiguration callee_config;
  callee_config.servers.push_back(ice_server);
  callee_config.type = PeerConnectionInterface::kRelay;
  callee_config.continual_gathering_policy = PeerConnection::GATHER_CONTINUALLY;

  ASSERT_TRUE(
      CreatePeerConnectionWrappersWithConfig(caller_config, callee_config));

  // Do normal offer/answer and wait for ICE to complete.
  ConnectFakeSignaling();
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  EXPECT_THAT(
      WaitUntil([&] { return caller()->error_event().error_code; }, Eq(701)),
      IsRtcOk());
  EXPECT_EQ(caller()->error_event().address, "");
}

// TODO(https://crbug.com/webrtc/14947): Investigate why this is flaking and
// find a way to re-enable the test.
TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       DISABLED_AudioKeepsFlowingAfterImplicitRollback) {
  PeerConnectionInterface::RTCConfiguration config;
  config.sdp_semantics = SdpSemantics::kUnifiedPlan;
  config.enable_implicit_rollback = true;
  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfig(config, config));
  ConnectFakeSignaling();
  caller()->AddAudioTrack();
  callee()->AddAudioTrack();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalAudio();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
  SetSignalIceCandidates(false);  // Workaround candidate outrace sdp.
  caller()->AddVideoTrack();
  callee()->AddVideoTrack();
  auto observer = make_ref_counted<MockSetSessionDescriptionObserver>();
  callee()->pc()->SetLocalDescription(observer.get(),
                                      callee()->CreateOfferAndWait().release());
  EXPECT_TRUE(WaitUntil([&] { return observer->called(); }));
  caller()->CreateAndSetAndSignalOffer();  // Implicit rollback.
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       ImplicitRollbackVisitsStableState) {
  RTCConfiguration config;
  config.sdp_semantics = SdpSemantics::kUnifiedPlan;
  config.enable_implicit_rollback = true;

  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfig(config, config));

  auto sld_observer = make_ref_counted<MockSetSessionDescriptionObserver>();
  callee()->pc()->SetLocalDescription(sld_observer.get(),
                                      callee()->CreateOfferAndWait().release());
  EXPECT_TRUE(WaitUntil([&] { return sld_observer->called(); }));
  EXPECT_EQ(sld_observer->error(), "");

  auto srd_observer = make_ref_counted<MockSetSessionDescriptionObserver>();
  callee()->pc()->SetRemoteDescription(
      srd_observer.get(), caller()->CreateOfferAndWait().release());
  EXPECT_TRUE(WaitUntil([&] { return srd_observer->called(); }));
  EXPECT_EQ(srd_observer->error(), "");

  EXPECT_THAT(callee()->peer_connection_signaling_state_history(),
              ElementsAre(PeerConnectionInterface::kHaveLocalOffer,
                          PeerConnectionInterface::kStable,
                          PeerConnectionInterface::kHaveRemoteOffer));
}

TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       H264FmtpSpsPpsIdrInKeyframeParameterUsage) {
  // Munging allowed: kVideoCodecsFmtp (85)
  SetFieldTrials("WebRTC-NoSdpMangleAllowForTesting/Enabled,85/");
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddVideoTrack();
  callee()->AddVideoTrack();
  auto munger = [](std::unique_ptr<SessionDescriptionInterface>& sdp) {
    VideoContentDescription* video =
        GetFirstVideoContentDescription(sdp->description());
    auto codecs = video->codecs();
    for (auto&& codec : codecs) {
      if (codec.name == "H264") {
        std::string value;
        // The parameter is not supposed to be present in SDP by default.
        EXPECT_FALSE(codec.GetParam(kH264FmtpSpsPpsIdrInKeyframe, &value));
        codec.SetParam(std::string(kH264FmtpSpsPpsIdrInKeyframe),
                       std::string(""));
      }
    }
    video->set_codecs(codecs);
  };
  // Munge local offer for SLD.
  caller()->SetGeneratedSdpMunger(munger);
  // Munge remote answer for SRD.
  caller()->SetReceivedSdpMunger(munger);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  // Observe that after munging the parameter is present in generated SDP.
  caller()->SetGeneratedSdpMunger(
      [](std::unique_ptr<SessionDescriptionInterface>& sdp) {
        VideoContentDescription* video =
            GetFirstVideoContentDescription(sdp->description());
        for (auto&& codec : video->codecs()) {
          if (codec.name == "H264") {
            std::string value;
            EXPECT_TRUE(codec.GetParam(kH264FmtpSpsPpsIdrInKeyframe, &value));
          }
        }
      });
  caller()->CreateOfferAndWait();
}

TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       RenegotiateManyAudioTransceivers) {
  PeerConnectionInterface::RTCConfiguration config;
  config.sdp_semantics = SdpSemantics::kUnifiedPlan;
  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfig(config, config));
  ConnectFakeSignaling();
  BitrateSettings bitrate;
  bitrate.start_bitrate_bps = 1'000'000;
  caller()->pc()->SetBitrate(bitrate);
  callee()->pc()->SetBitrate(bitrate);
  caller()->pc()->AddTransceiver(MediaType::AUDIO);

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  int current_size = caller()->pc()->GetTransceivers().size();
  // Add more tracks until we get close to having issues.
  // Issues have been seen at:
  // - 32 tracks on android_arm64_rel and android_arm_dbg bots
  // - 16 tracks on android_arm_dbg (flaky)
  while (current_size < 8) {
    // Double the number of tracks
    for (int i = 0; i < current_size; i++) {
      caller()->pc()->AddTransceiver(MediaType::AUDIO);
    }
    current_size = caller()->pc()->GetTransceivers().size();
    RTC_LOG(LS_INFO) << "Renegotiating with " << current_size << " tracks";
    auto start_time_ms = env_.clock().TimeInMilliseconds();
    caller()->CreateAndSetAndSignalOffer();
    // We want to stop when the time exceeds one second.
    ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
    auto elapsed_time_ms = env_.clock().TimeInMilliseconds() - start_time_ms;
    RTC_LOG(LS_INFO) << "Renegotiating took " << elapsed_time_ms << " ms";
    ASSERT_GT(1000, elapsed_time_ms)
        << "Audio transceivers: Negotiation took too long after "
        << current_size << " tracks added";
  }
}

TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       DISABLED_RenegotiateManyVideoTransceivers) {
  PeerConnectionInterface::RTCConfiguration config;
  config.sdp_semantics = SdpSemantics::kUnifiedPlan;
  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfig(config, config));
  ConnectFakeSignaling();
  caller()->pc()->AddTransceiver(MediaType::VIDEO);

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  int current_size = caller()->pc()->GetTransceivers().size();
  // Add more tracks until we get close to having issues.
  // Issues have been seen at:
  // - 96 on a Linux workstation
  // - 64 at win_x86_more_configs and win_x64_msvc_dbg
  // - 32 on android_arm64_rel and linux_dbg bots
  // - 16 on Android 64 (Nexus 5x)
  while (current_size < 8) {
    // Double the number of tracks
    for (int i = 0; i < current_size; i++) {
      caller()->pc()->AddTransceiver(MediaType::VIDEO);
    }
    current_size = caller()->pc()->GetTransceivers().size();
    RTC_LOG(LS_INFO) << "Renegotiating with " << current_size << " tracks";
    auto start_time_ms = env_.clock().TimeInMilliseconds();
    caller()->CreateAndSetAndSignalOffer();
    // We want to stop when the time exceeds one second.
    ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
    auto elapsed_time_ms = env_.clock().TimeInMilliseconds() - start_time_ms;
    RTC_LOG(LS_INFO) << "Renegotiating took " << elapsed_time_ms << " ms";
    ASSERT_GT(1000, elapsed_time_ms)
        << "Video transceivers: Negotiation took too long after "
        << current_size << " tracks added";
  }
}

TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       RenegotiateManyVideoTransceiversAndWatchAudioDelay) {
  PeerConnectionInterface::RTCConfiguration config;
  config.sdp_semantics = SdpSemantics::kUnifiedPlan;
  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfig(config, config));
  ConnectFakeSignaling();
  caller()->AddAudioTrack();
  callee()->AddAudioTrack();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  // Wait until we can see the audio flowing.
  MediaExpectations media_expectations;
  media_expectations.CalleeExpectsSomeAudio();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));

  // Get the baseline numbers for audio_packets and audio_delay
  // in both directions.
  caller()->StartWatchingDelayStats();
  callee()->StartWatchingDelayStats();

  int current_size = caller()->pc()->GetTransceivers().size();
  // Add more tracks until we get close to having issues.
  // Making this number very large makes the test very slow.
  while (current_size < 16) {
    // Double the number of tracks
    for (int i = 0; i < current_size; i++) {
      caller()->pc()->AddTransceiver(MediaType::VIDEO);
    }
    current_size = caller()->pc()->GetTransceivers().size();
    RTC_LOG(LS_INFO) << "Renegotiating with " << current_size << " tracks";
    auto start_time_ms = env_.clock().TimeInMilliseconds();
    caller()->CreateAndSetAndSignalOffer();
    // We want to stop when the time exceeds one second.
    ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
    auto elapsed_time_ms = env_.clock().TimeInMilliseconds() - start_time_ms;
    RTC_LOG(LS_INFO) << "Renegotiating took " << elapsed_time_ms << " ms";
    // This is a guard against the test using excessive amounts of time.
    ASSERT_GT(5000, elapsed_time_ms)
        << "Video transceivers: Negotiation took too long after "
        << current_size << " tracks added";
    caller()->UpdateDelayStats("caller reception", current_size);
    callee()->UpdateDelayStats("callee reception", current_size);
  }
  EXPECT_TRUE(caller()->AudioDelayStatsPercentageChecked());
  EXPECT_TRUE(callee()->AudioDelayStatsPercentageChecked());
}

TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       GetParametersHasEncodingsBeforeNegotiation) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  auto result = caller()->pc()->AddTransceiver(MediaType::VIDEO);
  auto transceiver = result.MoveValue();
  auto parameters = transceiver->sender()->GetParameters();
  EXPECT_EQ(parameters.encodings.size(), 1u);
}

TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       GetParametersHasInitEncodingsBeforeNegotiation) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  RtpTransceiverInit init;
  init.send_encodings.push_back({});
  init.send_encodings[0].max_bitrate_bps = 12345;
  auto result = caller()->pc()->AddTransceiver(MediaType::VIDEO, init);
  auto transceiver = result.MoveValue();
  auto parameters = transceiver->sender()->GetParameters();
  ASSERT_EQ(parameters.encodings.size(), 1u);
  EXPECT_EQ(parameters.encodings[0].max_bitrate_bps, 12345);
}

INSTANTIATE_TEST_SUITE_P(PeerConnectionIntegrationTest,
                         PeerConnectionIntegrationTest,
                         Values(SdpSemantics::kPlanB_DEPRECATED,
                                SdpSemantics::kUnifiedPlan));

INSTANTIATE_TEST_SUITE_P(PeerConnectionIntegrationTest,
                         PeerConnectionIntegrationTestWithFakeClock,
                         Values(SdpSemantics::kPlanB_DEPRECATED,
                                SdpSemantics::kUnifiedPlan));

// Tests that verify interoperability between Plan B and Unified Plan
// PeerConnections.
class PeerConnectionIntegrationInteropTest
    : public PeerConnectionIntegrationBaseTest,
      public WithParamInterface<std::tuple<SdpSemantics, SdpSemantics>> {
 protected:
  // Setting the SdpSemantics for the base test to kDefault does not matter
  // because we specify not to use the test semantics when creating
  // PeerConnectionIntegrationWrappers.
  PeerConnectionIntegrationInteropTest()
      : PeerConnectionIntegrationBaseTest(SdpSemantics::kPlanB_DEPRECATED),
        caller_semantics_(std::get<0>(GetParam())),
        callee_semantics_(std::get<1>(GetParam())) {}

  bool CreatePeerConnectionWrappersWithSemantics() {
    return CreatePeerConnectionWrappersWithSdpSemantics(caller_semantics_,
                                                        callee_semantics_);
  }

  const SdpSemantics caller_semantics_;
  const SdpSemantics callee_semantics_;
};

TEST_P(PeerConnectionIntegrationInteropTest, NoMediaLocalToNoMediaRemote) {
  ASSERT_TRUE(CreatePeerConnectionWrappersWithSemantics());
  ConnectFakeSignaling();

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
}

TEST_P(PeerConnectionIntegrationInteropTest, OneAudioLocalToNoMediaRemote) {
  ASSERT_TRUE(CreatePeerConnectionWrappersWithSemantics());
  ConnectFakeSignaling();
  auto audio_sender = caller()->AddAudioTrack();

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  // Verify that one audio receiver has been created on the remote and that it
  // has the same track ID as the sending track.
  auto receivers = callee()->pc()->GetReceivers();
  ASSERT_EQ(1u, receivers.size());
  EXPECT_EQ(MediaType::AUDIO, receivers[0]->media_type());
  EXPECT_EQ(receivers[0]->track()->id(), audio_sender->track()->id());

  MediaExpectations media_expectations;
  media_expectations.CalleeExpectsSomeAudio();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

TEST_P(PeerConnectionIntegrationInteropTest, OneAudioOneVideoToNoMediaRemote) {
  ASSERT_TRUE(CreatePeerConnectionWrappersWithSemantics());
  ConnectFakeSignaling();
  auto video_sender = caller()->AddVideoTrack();
  auto audio_sender = caller()->AddAudioTrack();

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  // Verify that one audio and one video receiver have been created on the
  // remote and that they have the same track IDs as the sending tracks.
  auto audio_receivers = callee()->GetReceiversOfType(MediaType::AUDIO);
  ASSERT_EQ(1u, audio_receivers.size());
  EXPECT_EQ(audio_receivers[0]->track()->id(), audio_sender->track()->id());
  auto video_receivers = callee()->GetReceiversOfType(MediaType::VIDEO);
  ASSERT_EQ(1u, video_receivers.size());
  EXPECT_EQ(video_receivers[0]->track()->id(), video_sender->track()->id());

  MediaExpectations media_expectations;
  media_expectations.CalleeExpectsSomeAudioAndVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

TEST_P(PeerConnectionIntegrationInteropTest,
       OneAudioOneVideoLocalToOneAudioOneVideoRemote) {
  ASSERT_TRUE(CreatePeerConnectionWrappersWithSemantics());
  ConnectFakeSignaling();
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  MediaExpectations media_expectations;
  media_expectations.ExpectBidirectionalAudioAndVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

TEST_P(PeerConnectionIntegrationInteropTest,
       ReverseRolesOneAudioLocalToOneVideoRemote) {
  ASSERT_TRUE(CreatePeerConnectionWrappersWithSemantics());
  ConnectFakeSignaling();
  caller()->AddAudioTrack();
  callee()->AddVideoTrack();

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  // Verify that only the audio track has been negotiated.
  EXPECT_EQ(0u, caller()->GetReceiversOfType(MediaType::VIDEO).size());
  // Might also check that the callee's NegotiationNeeded flag is set.

  // Reverse roles.
  callee()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  MediaExpectations media_expectations;
  media_expectations.CallerExpectsSomeVideo();
  media_expectations.CalleeExpectsSomeAudio();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

TEST_P(PeerConnectionIntegrationTest, NewTracksDoNotCauseNewCandidates) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_TRUE(WaitUntil([&] { return DtlsConnected(); }));
  caller()->ExpectCandidates(0);
  callee()->ExpectCandidates(0);
  caller()->AddAudioTrack();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
}

TEST_P(PeerConnectionIntegrationTest, MediaCallWithoutMediaEngineFails) {
  ASSERT_TRUE(CreatePeerConnectionWrappersWithoutMediaEngine());
  // AddTrack should fail.
  EXPECT_FALSE(
      caller()->pc()->AddTrack(caller()->CreateLocalAudioTrack(), {}).ok());
}

INSTANTIATE_TEST_SUITE_P(
    PeerConnectionIntegrationTest,
    PeerConnectionIntegrationInteropTest,
    Values(std::make_tuple(SdpSemantics::kPlanB_DEPRECATED,
                           SdpSemantics::kUnifiedPlan),
           std::make_tuple(SdpSemantics::kUnifiedPlan,
                           SdpSemantics::kPlanB_DEPRECATED)));

// Test that if the Unified Plan side offers two video tracks then the Plan B
// side will only see the first one and ignore the second.
TEST_F(PeerConnectionIntegrationTestPlanB, TwoVideoUnifiedPlanToNoMediaPlanB) {
  ASSERT_TRUE(CreatePeerConnectionWrappersWithSdpSemantics(
      SdpSemantics::kUnifiedPlan, SdpSemantics::kPlanB_DEPRECATED));
  ConnectFakeSignaling();
  auto first_sender = caller()->AddVideoTrack();
  caller()->AddVideoTrack();

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));

  // Verify that there is only one receiver and it corresponds to the first
  // added track.
  auto receivers = callee()->pc()->GetReceivers();
  ASSERT_EQ(1u, receivers.size());
  EXPECT_TRUE(receivers[0]->track()->enabled());
  EXPECT_EQ(first_sender->track()->id(), receivers[0]->track()->id());

  MediaExpectations media_expectations;
  media_expectations.CalleeExpectsSomeVideo();
  ASSERT_TRUE(ExpectNewFrames(media_expectations));
}

// Test that if the initial offer tagged BUNDLE section is rejected due to its
// associated RtpTransceiver being stopped and another transceiver is added,
// then renegotiation causes the callee to receive the new video track without
// error.
// This is a regression test for bugs.webrtc.org/9954
TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       ReOfferWithStoppedBundleTaggedTransceiver) {
  RTCConfiguration config;
  config.bundle_policy = PeerConnectionInterface::kBundlePolicyMaxBundle;
  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfig(config, config));
  ConnectFakeSignaling();
  auto audio_transceiver_or_error =
      caller()->pc()->AddTransceiver(caller()->CreateLocalAudioTrack());
  ASSERT_TRUE(audio_transceiver_or_error.ok());
  auto audio_transceiver = audio_transceiver_or_error.MoveValue();

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  {
    MediaExpectations media_expectations;
    media_expectations.CalleeExpectsSomeAudio();
    ASSERT_TRUE(ExpectNewFrames(media_expectations));
  }

  audio_transceiver->StopInternal();
  caller()->pc()->AddTransceiver(caller()->CreateLocalVideoTrack());

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  {
    MediaExpectations media_expectations;
    media_expectations.CalleeExpectsSomeVideo();
    ASSERT_TRUE(ExpectNewFrames(media_expectations));
  }
}

TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       StopTransceiverRemovesDtlsTransports) {
  RTCConfiguration config;
  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfig(config, config));
  ConnectFakeSignaling();
  auto audio_transceiver_or_error =
      caller()->pc()->AddTransceiver(caller()->CreateLocalAudioTrack());
  ASSERT_TRUE(audio_transceiver_or_error.ok());
  auto audio_transceiver = audio_transceiver_or_error.MoveValue();

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_THAT(WaitUntil([&] { return caller()->ice_gathering_state(); },
                        Eq(PeerConnectionInterface::kIceGatheringComplete)),
              IsRtcOk());

  audio_transceiver->StopStandard();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_EQ(0U, caller()->pc()->GetTransceivers().size());
  EXPECT_EQ(PeerConnectionInterface::kIceGatheringNew,
            caller()->pc()->ice_gathering_state());
  EXPECT_THAT(caller()->ice_gathering_state_history(),
              ElementsAre(PeerConnectionInterface::kIceGatheringGathering,
                          PeerConnectionInterface::kIceGatheringComplete,
                          PeerConnectionInterface::kIceGatheringNew));
}

TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       StopTransceiverStopsAndRemovesTransceivers) {
  RTCConfiguration config;
  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfig(config, config));
  ConnectFakeSignaling();
  auto audio_transceiver_or_error =
      caller()->pc()->AddTransceiver(caller()->CreateLocalAudioTrack());
  ASSERT_TRUE(audio_transceiver_or_error.ok());
  auto caller_transceiver = audio_transceiver_or_error.MoveValue();

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  caller_transceiver->StopStandard();

  auto callee_transceiver = callee()->pc()->GetTransceivers()[0];
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  EXPECT_EQ(0U, caller()->pc()->GetTransceivers().size());
  EXPECT_EQ(0U, callee()->pc()->GetTransceivers().size());
  EXPECT_EQ(0U, caller()->pc()->GetSenders().size());
  EXPECT_EQ(0U, callee()->pc()->GetSenders().size());
  EXPECT_EQ(0U, caller()->pc()->GetReceivers().size());
  EXPECT_EQ(0U, callee()->pc()->GetReceivers().size());
  EXPECT_TRUE(caller_transceiver->stopped());
  EXPECT_TRUE(callee_transceiver->stopped());
}

TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       StopTransceiverEndsIncomingAudioTrack) {
  RTCConfiguration config;
  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfig(config, config));
  ConnectFakeSignaling();
  auto audio_transceiver_or_error =
      caller()->pc()->AddTransceiver(caller()->CreateLocalAudioTrack());
  ASSERT_TRUE(audio_transceiver_or_error.ok());
  auto audio_transceiver = audio_transceiver_or_error.MoveValue();

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  auto caller_track = audio_transceiver->receiver()->track();
  auto callee_track = callee()->pc()->GetReceivers()[0]->track();
  audio_transceiver->StopStandard();
  EXPECT_EQ(MediaStreamTrackInterface::TrackState::kEnded,
            caller_track->state());
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  EXPECT_EQ(MediaStreamTrackInterface::TrackState::kEnded,
            callee_track->state());
}

TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       StopTransceiverEndsIncomingVideoTrack) {
  RTCConfiguration config;
  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfig(config, config));
  ConnectFakeSignaling();
  auto audio_transceiver_or_error =
      caller()->pc()->AddTransceiver(caller()->CreateLocalVideoTrack());
  ASSERT_TRUE(audio_transceiver_or_error.ok());
  auto audio_transceiver = audio_transceiver_or_error.MoveValue();

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  auto caller_track = audio_transceiver->receiver()->track();
  auto callee_track = callee()->pc()->GetReceivers()[0]->track();
  audio_transceiver->StopStandard();
  EXPECT_EQ(MediaStreamTrackInterface::TrackState::kEnded,
            caller_track->state());
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  EXPECT_EQ(MediaStreamTrackInterface::TrackState::kEnded,
            callee_track->state());
}

TEST_P(PeerConnectionIntegrationTest, EndToEndRtpSenderVideoEncoderSelector) {
  ASSERT_TRUE(
      CreateOneDirectionalPeerConnectionWrappers(/*caller_to_callee=*/true));
  ConnectFakeSignaling();
  // Add one-directional video, from caller to callee.
  scoped_refptr<VideoTrackInterface> caller_track =
      caller()->CreateLocalVideoTrack();
  auto sender = caller()->AddTrack(caller_track);
  PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.offer_to_receive_video = 0;
  caller()->SetOfferAnswerOptions(options);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_EQ(callee()->pc()->GetReceivers().size(), 1u);

  scoped_refptr<MockEncoderSelector> encoder_selector =
      make_ref_counted<MockEncoderSelector>();
  EXPECT_CALL(*encoder_selector, OnCurrentEncoder);

  sender->SetEncoderSelector(encoder_selector);

  // Expect video to be received in one direction.
  MediaExpectations media_expectations;
  media_expectations.CallerExpectsNoVideo();
  media_expectations.CalleeExpectsSomeVideo();

  EXPECT_TRUE(ExpectNewFrames(media_expectations));
}

TEST_P(PeerConnectionIntegrationTest,
       EndToEndRtpSenderVideoEncoderSelectorSwitchCodec) {
  ASSERT_TRUE(
      CreateOneDirectionalPeerConnectionWrappers(/*caller_to_callee=*/true));
  ConnectFakeSignaling();
  // Add one-directional video, from caller to callee.
  scoped_refptr<VideoTrackInterface> caller_track =
      caller()->CreateLocalVideoTrack();
  auto sender = caller()->AddTrack(caller_track);
  PeerConnectionInterface::RTCOfferAnswerOptions options;
  options.offer_to_receive_video = 0;
  caller()->SetOfferAnswerOptions(options);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_EQ(callee()->pc()->GetReceivers().size(), 1u);

  scoped_refptr<MockEncoderSelector> encoder_selector =
      make_ref_counted<MockEncoderSelector>();
  std::optional<SdpVideoFormat> next_format;
  EXPECT_CALL(*encoder_selector, OnCurrentEncoder)
      .WillOnce([&](const SdpVideoFormat& format) {
        EXPECT_EQ(format.name, "VP8");
        next_format = SdpVideoFormat::VP9Profile0();
      })
      .WillOnce(
          [&](const SdpVideoFormat& format) { EXPECT_EQ(format.name, "VP9"); });
  EXPECT_CALL(*encoder_selector, OnAvailableBitrate)
      .WillRepeatedly([&](const DataRate& rate) { return next_format; });

  sender->SetEncoderSelector(encoder_selector);

  // Expect video to be received in one direction.
  MediaExpectations media_expectations;
  media_expectations.CallerExpectsNoVideo();
  media_expectations.CalleeExpectsSomeVideo();

  EXPECT_TRUE(ExpectNewFrames(media_expectations));

  caller()->pc()->Close();
}

int NacksReceivedCount(PeerConnectionIntegrationWrapper& pc) {
  scoped_refptr<const RTCStatsReport> report = pc.NewGetStats();
  auto sender_stats = report->GetStatsOfType<RTCOutboundRtpStreamStats>();
  if (sender_stats.size() != 1) {
    ADD_FAILURE();
    return 0;
  }
  if (!sender_stats[0]->nack_count.has_value()) {
    return 0;
  }
  return *sender_stats[0]->nack_count;
}

int NacksSentCount(PeerConnectionIntegrationWrapper& pc) {
  scoped_refptr<const RTCStatsReport> report = pc.NewGetStats();
  auto receiver_stats = report->GetStatsOfType<RTCInboundRtpStreamStats>();
  if (receiver_stats.size() != 1) {
    ADD_FAILURE();
    return 0;
  }
  if (!receiver_stats[0]->nack_count.has_value()) {
    return 0;
  }
  return *receiver_stats[0]->nack_count;
}

TEST_F(PeerConnectionIntegrationTestUnifiedPlan, AudioPacketLossCausesNack) {
  RTCConfiguration config;
  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfig(config, config));
  ConnectFakeSignaling();
  auto audio_transceiver_or_error =
      caller()->pc()->AddTransceiver(caller()->CreateLocalAudioTrack());
  ASSERT_TRUE(audio_transceiver_or_error.ok());
  auto send_transceiver = audio_transceiver_or_error.MoveValue();
  // Munge the SDP to include NACK and RRTR on Opus, and remove all other
  // codecs.
  caller()->SetGeneratedSdpMunger(
      [](std::unique_ptr<SessionDescriptionInterface>& sdp) {
        for (ContentInfo& content : sdp->description()->contents()) {
          MediaContentDescription* media = content.media_description();
          std::vector<Codec> codecs = media->codecs();
          std::vector<Codec> codecs_out;
          for (Codec codec : codecs) {
            if (codec.name == "opus") {
              codec.AddFeedbackParam(
                  FeedbackParam(kRtcpFbParamNack, kParamValueEmpty));
              codec.AddFeedbackParam(
                  FeedbackParam(kRtcpFbParamRrtr, kParamValueEmpty));
              codecs_out.push_back(codec);
            }
          }
          EXPECT_FALSE(codecs_out.empty());
          media->set_codecs(codecs_out);
        }
      });

  caller()->CreateAndSetAndSignalOffer();
  // Check for failure in helpers
  ASSERT_FALSE(HasFailure());
  MediaExpectations media_expectations;
  media_expectations.CalleeExpectsSomeAudio(1);
  ExpectNewFrames(media_expectations);
  ASSERT_FALSE(HasFailure());

  virtual_socket_server()->set_drop_probability(0.2);

  // Wait until callee has sent at least one NACK.
  // Note that due to stats caching, this might only be visible 50 ms
  // after the nack was in fact sent.
  EXPECT_THAT(WaitUntil([&] { return NacksSentCount(*callee()); }, Gt(0)),
              IsRtcOk());
  ASSERT_FALSE(HasFailure());

  virtual_socket_server()->set_drop_probability(0.0);
  // Wait until caller has received at least one NACK
  EXPECT_THAT(WaitUntil([&] { return NacksReceivedCount(*caller()); }, Gt(0)),
              IsRtcOk());
}

TEST_F(PeerConnectionIntegrationTestUnifiedPlan, VideoPacketLossCausesNack) {
  RTCConfiguration config;
  // Munging allowed: kVideoCodecsRemoved (80)
  SetFieldTrials("WebRTC-NoSdpMangleAllowForTesting/Enabled,80/");
  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfig(config, config));
  ConnectFakeSignaling();
  auto video_transceiver_or_error =
      caller()->pc()->AddTransceiver(caller()->CreateLocalVideoTrack());
  ASSERT_TRUE(video_transceiver_or_error.ok());
  auto send_transceiver = video_transceiver_or_error.MoveValue();
  // Munge the SDP to include NACK and RRTR on VP8, and remove all other
  // codecs.
  caller()->SetGeneratedSdpMunger(
      [](std::unique_ptr<SessionDescriptionInterface>& sdp) {
        for (ContentInfo& content : sdp->description()->contents()) {
          MediaContentDescription* media = content.media_description();
          std::vector<Codec> codecs = media->codecs();
          std::vector<Codec> codecs_out;
          for (const Codec& codec : codecs) {
            if (codec.name == "VP8") {
              ASSERT_TRUE(codec.HasFeedbackParam(
                  FeedbackParam(kRtcpFbParamNack, kParamValueEmpty)));
              codecs_out.push_back(codec);
            }
          }
          EXPECT_FALSE(codecs_out.empty());
          media->set_codecs(codecs_out);
        }
      });

  caller()->CreateAndSetAndSignalOffer();
  // Check for failure in helpers
  ASSERT_FALSE(HasFailure());
  MediaExpectations media_expectations;
  media_expectations.CalleeExpectsSomeVideo(1);
  ExpectNewFrames(media_expectations);
  ASSERT_FALSE(HasFailure());

  virtual_socket_server()->set_drop_probability(0.2);

  // Wait until callee has sent at least one NACK.
  // Note that due to stats caching, this might only be visible 50 ms
  // after the nack was in fact sent.
  EXPECT_THAT(WaitUntil([&] { return NacksSentCount(*callee()); }, Gt(0)),
              IsRtcOk());
  ASSERT_FALSE(HasFailure());

  // Wait until caller has received at least one NACK
  EXPECT_THAT(WaitUntil([&] { return NacksReceivedCount(*caller()); }, Gt(0)),
              IsRtcOk());
}

TEST_F(PeerConnectionIntegrationTestUnifiedPlan, PrAnswerStateTransitions) {
  RTCConfiguration config;
  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfig(config, config));
  ConnectFakeSignaling();
  caller()->pc()->AddTransceiver(caller()->CreateLocalAudioTrack());
  caller()->pc()->AddTransceiver(caller()->CreateLocalVideoTrack());

  callee()->SetGeneratedSdpMunger(
      [](std::unique_ptr<SessionDescriptionInterface>& sdp) {
        SetSdpType(sdp, SdpType::kPrAnswer);
      });
  std::unique_ptr<SessionDescriptionInterface> answer;
  caller()->SetReceivedSdpMunger(
      [&](std::unique_ptr<SessionDescriptionInterface>& sdp) {
        answer = sdp->Clone();
      });
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_FALSE(HasFailure());
  EXPECT_EQ(caller()->pc()->signaling_state(),
            PeerConnectionInterface::kHaveRemotePrAnswer);
  EXPECT_EQ(callee()->pc()->signaling_state(),
            PeerConnectionInterface::kHaveLocalPrAnswer);

  // // Apply the pranswer as a definitive one.
  SetSdpType(answer, SdpType::kAnswer);
  EXPECT_TRUE(caller()->SetRemoteDescription(std::move(answer)));
  EXPECT_EQ(caller()->pc()->signaling_state(),
            PeerConnectionInterface::kStable);
}

// Let caller get a prAnswer followed by answer.
TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       PrAnswerStateTransitionsAsymmetric) {
  RTCConfiguration config;
  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfig(config, config));
  ConnectFakeSignaling();
  caller()->pc()->AddTransceiver(caller()->CreateLocalAudioTrack());
  caller()->pc()->AddTransceiver(caller()->CreateLocalVideoTrack());

  std::unique_ptr<SessionDescriptionInterface> answer;
  caller()->SetReceivedSdpMunger(
      [&](std::unique_ptr<SessionDescriptionInterface>& sdp) {
        answer = sdp->Clone();
        SetSdpType(sdp, SdpType::kPrAnswer);
      });
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_FALSE(HasFailure());
  EXPECT_EQ(caller()->pc()->signaling_state(),
            PeerConnectionInterface::kHaveRemotePrAnswer);
  EXPECT_EQ(callee()->pc()->signaling_state(),
            PeerConnectionInterface::kStable);

  // // Apply the pranswer as a definitive one.
  EXPECT_TRUE(caller()->SetRemoteDescription(std::move(answer)));
  EXPECT_EQ(caller()->pc()->signaling_state(),
            PeerConnectionInterface::kStable);
}

int ReassignPayloadIds(std::unique_ptr<SessionDescriptionInterface>& sdp) {
  int swaps = 0;
  for (ContentInfo& content : sdp->description()->contents()) {
    if (!content.media_description()) {
      continue;
    }
    std::vector<Codec> codecs = content.media_description()->codecs();
    int left = 0;
    int right = codecs.size() - 1;
    while (left < right) {
      if (!codecs[left].IsMediaCodec()) {
        left++;
        continue;
      }
      if (!codecs[right].IsMediaCodec()) {
        right--;
        continue;
      }
      auto tmp = codecs[left].id;
      codecs[left].id = codecs[right].id;
      codecs[right].id = tmp;
      left++;
      right--;
      swaps++;
    }
    content.media_description()->set_codecs(codecs);
  }
  return swaps;
}

int SetNewSsrcs(std::unique_ptr<SessionDescriptionInterface>& sdp) {
  int assignments = 0;
  std::unordered_set<uint32_t> already_used_ssrcs;
  for (ContentInfo& content : sdp->description()->contents()) {
    if (!content.media_description()) {
      continue;
    }
    for (const auto& stream : content.media_description()->streams()) {
      for (const auto& ssrc : stream.ssrcs) {
        already_used_ssrcs.insert(ssrc);
      }
    }
  }

  Random random(/* random_seed= */ 77);
  auto ssrc_generator = [&]() -> uint32_t {
    do {
      auto ssrc = random.Rand(1u, 0xFFFFFFF0u);
      if (already_used_ssrcs.find(ssrc) == already_used_ssrcs.end()) {
        already_used_ssrcs.insert(ssrc);
        return ssrc;
      }
    } while (true);
  };

  for (ContentInfo& content : sdp->description()->contents()) {
    if (!content.media_description()) {
      continue;
    }
    for (auto& stream : content.media_description()->mutable_streams()) {
      // Only reassign primary ssrc for now...
      // but we should maybe also reassign ssrcs for ssrc groups?.
      if (stream.ssrcs.size() == 1) {
        assignments++;
        stream.ssrcs[0] = ssrc_generator();
      }
    }
  }
  return assignments;
}

void SetNewFingerprint(std::unique_ptr<SessionDescriptionInterface>& sdp) {
  auto identity = SSLIdentity::Create("NewIdentity", KT_DEFAULT);
  auto new_fingerprint = SSLFingerprint::Create("sha-256", *identity);
  for (auto& transport_info : sdp->description()->transport_infos()) {
    transport_info.description.identity_fingerprint =
        std::make_unique<SSLFingerprint>(*new_fingerprint);
  }
}

TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       PrAnswerStateTransitionsAsymmetricScrambled) {
  RTCConfiguration config;
  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfig(config, config));
  ConnectFakeSignaling();
  RtpEncodingParameters init_send_encodings;
  init_send_encodings.active = false;
  caller()->pc()->AddTrack(caller()->CreateLocalAudioTrack(), {"name"},
                           {init_send_encodings});
  caller()->pc()->AddTrack(caller()->CreateLocalVideoTrack(), {"name"},
                           {init_send_encodings});
  callee()->pc()->AddTrack(callee()->CreateLocalAudioTrack(), {"name"},
                           {init_send_encodings});
  callee()->pc()->AddTrack(callee()->CreateLocalVideoTrack(), {"name"},
                           {init_send_encodings});

  std::unique_ptr<SessionDescriptionInterface> answer;
  caller()->SetReceivedSdpMunger(
      [&](std::unique_ptr<SessionDescriptionInterface>& sdp) {
        answer = sdp->Clone();
        SetSdpType(sdp, SdpType::kPrAnswer);
      });
  caller()->CreateAndSetAndSignalOffer();

  ASSERT_FALSE(HasFailure());
  ASSERT_EQ(caller()->pc()->signaling_state(),
            PeerConnectionInterface::kHaveRemotePrAnswer);
  ASSERT_EQ(callee()->pc()->signaling_state(),
            PeerConnectionInterface::kStable);

  // Now scramble the answer sdp so that it (really!) different from the first
  // prAnswer.
  // Note: this is maybe {possibly...probably?} a spec violation.
  ASSERT_GT(SetNewSsrcs(answer), 0);
  ASSERT_GT(ReassignPayloadIds(answer), 0);
  SetNewFingerprint(answer);

  // Apply the modified answer as a definitive one.
  EXPECT_TRUE(caller()->SetRemoteDescription(std::move(answer)));
  EXPECT_EQ(caller()->pc()->signaling_state(),
            PeerConnectionInterface::kStable);
}

TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       OnlyOnePairWantsCorruptionScorePlumbingShouldFailToGetIt) {
  // In order for corruption score to be logged, encryption of RTP header
  // extensions must be allowed.
  CryptoOptions crypto_options;
  crypto_options.srtp.enable_encrypted_rtp_header_extensions = true;
  PeerConnectionInterface::RTCConfiguration config;
  config.crypto_options = crypto_options;
  config.offer_extmap_allow_mixed = true;
  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfig(config, config));
  ConnectFakeSignaling();

  // Do normal offer/answer and wait for some frames to be received in each
  // direction.
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();

  // Negotiate the corruption detection header extension in SDP only in one
  // direction.
  caller()->NegotiateCorruptionDetectionHeader();

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  std::vector<RtpHeaderExtensionCapability> negotiated_extensions =
      caller()->pc()->GetTransceivers()[0]->GetNegotiatedHeaderExtensions();
  // Even if `caller` wants to collect corruption score, since `callee` does not
  // want it, we should not send/receive any corruption score data.
  ASSERT_THAT(negotiated_extensions,
              Not(Contains(Field("uri", &RtpHeaderExtensionCapability::uri,
                                 RtpExtension::kCorruptionDetectionUri))));
}

TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       BothPairsWantCorruptionScorePlumbing) {
  // In order for corruption score to be logged, encryption of RTP header
  // extensions must be allowed.
  CryptoOptions crypto_options;
  crypto_options.srtp.enable_encrypted_rtp_header_extensions = true;
  PeerConnectionInterface::RTCConfiguration config;
  config.crypto_options = crypto_options;
  config.offer_extmap_allow_mixed = true;
  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfig(config, config));
  ConnectFakeSignaling();

  // Do normal offer/answer and wait for some frames to be received in each
  // direction, and `corruption_score` to be aggregated.
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();

  // Negotiate the corruption detection header extension in SDP.
  // If caller adds corruption detection header extension to its SDP offer, it
  // will receive it from the callee.
  caller()->NegotiateCorruptionDetectionHeader();
  callee()->NegotiateCorruptionDetectionHeader();

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_THAT(WaitUntil([&] { return caller()->GetCorruptionScoreCount(); },
                        Gt(0), {.timeout = kMaxWaitForStats}),
              IsRtcOk());
  ASSERT_THAT(WaitUntil([&] { return callee()->GetCorruptionScoreCount(); },
                        Gt(0), {.timeout = kMaxWaitForStats}),
              IsRtcOk());

  for (const auto& pair : {caller(), callee()}) {
    scoped_refptr<const RTCStatsReport> report = pair->NewGetStats();
    ASSERT_TRUE(report);
    auto inbound_stream_stats =
        report->GetStatsOfType<RTCInboundRtpStreamStats>();
    for (const auto& stat : inbound_stream_stats) {
      if (*stat->kind == "video") {
        EXPECT_TRUE(stat->total_corruption_probability.has_value());
        EXPECT_TRUE(stat->total_squared_corruption_probability.has_value());

        double average_corruption_score =
            (*stat->total_corruption_probability) /
            static_cast<int32_t>(*stat->corruption_measurements);
        EXPECT_GE(average_corruption_score, 0.0);
        EXPECT_LE(average_corruption_score, 1.0);
      }
    }
  }
}

TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       CorruptionScorePlumbingShouldNotWorkWhenEncryptionIsOff) {
  // In order for corruption score to be logged, encryption of RTP header
  // extensions must be allowed.
  CryptoOptions crypto_options;
  crypto_options.srtp.enable_encrypted_rtp_header_extensions = false;
  PeerConnectionInterface::RTCConfiguration config;
  config.crypto_options = crypto_options;
  config.offer_extmap_allow_mixed = true;
  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfig(config, config));
  ConnectFakeSignaling();

  // Negotiate the corruption detection header extension in SDP.
  // If caller adds corruption detection header extension to its SDP offer, it
  // will receive it from the callee.
  caller()->NegotiateCorruptionDetectionHeader();
  callee()->NegotiateCorruptionDetectionHeader();

  // Do normal offer/answer and wait for some frames to be received in each
  // direction, and `corruption_score` to be aggregated.
  caller()->AddAudioVideoTracks();
  callee()->AddAudioVideoTracks();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_THAT(WaitUntil([&] { return caller()->GetCorruptionScoreCount(); },
                        Eq(0), {.timeout = kMaxWaitForStats}),
              IsRtcOk());
  ASSERT_THAT(WaitUntil([&] { return callee()->GetCorruptionScoreCount(); },
                        Eq(0), {.timeout = kMaxWaitForStats}),
              IsRtcOk());

  for (const auto& pair : {caller(), callee()}) {
    scoped_refptr<const RTCStatsReport> report = pair->NewGetStats();
    ASSERT_TRUE(report);
    auto inbound_stream_stats =
        report->GetStatsOfType<RTCInboundRtpStreamStats>();
    for (const auto& stat : inbound_stream_stats) {
      if (*stat->kind == "video") {
        EXPECT_FALSE(stat->total_corruption_probability.has_value());
        EXPECT_FALSE(stat->total_squared_corruption_probability.has_value());
      }
    }
  }
}

TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       AbsCaptureTimestampShouldBeMeteredCorrectly) {
  metrics::Reset();
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddVideoTrack();
  auto transceiver = caller()->pc()->GetTransceivers()[0];
  auto extensions = transceiver->GetHeaderExtensionsToNegotiate();
  bool found = false;
  for (auto& extension : extensions) {
    if (extension.uri == RtpExtension::kAbsoluteCaptureTimeUri) {
      extension.direction = RtpTransceiverDirection::kSendRecv;
      found = true;
      break;
    }
  }
  ASSERT_TRUE(found);
  ASSERT_TRUE(transceiver->SetHeaderExtensionsToNegotiate(extensions).ok());

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  const auto& negotiated_header_extensions = caller()
                                                 ->pc()
                                                 ->remote_description()
                                                 ->description()
                                                 ->contents()[0]
                                                 .media_description()
                                                 ->rtp_header_extensions();
  ASSERT_TRUE(RtpExtension::FindHeaderExtensionByUri(
      negotiated_header_extensions, RtpExtension::kAbsoluteCaptureTimeUri,
      RtpExtension::kDiscardEncryptedExtension));
  ASSERT_THAT(
      WaitUntil(
          [&] {
            return metrics::NumSamples("WebRTC.Call.AbsCapture.ExtensionWait");
          },
          Gt(0)),
      IsRtcOk());
  // Observed deltas are more than 100 msec. Use 1 minute as tolerance;
  // this is a check against wrong timebase.
  EXPECT_LT(metrics::MinSample("WebRTC.Call.AbsCapture.Delta"), 60'000'000);
  ASSERT_THAT(
      WaitUntil(
          [&] {
            return metrics::NumSamples("WebRTC.Call.AbsCapture.OffsetWait");
          },
          Gt(0)),
      IsRtcOk());
  // On a point-to-point call, we expect the offset to be zero.
  EXPECT_LT(metrics::MinSample("WebRTC.Call.AbsCapture.Offset"), 2);
}

// Test that when SDP is munged to use a PT for a different codec,
// the old codec is added to a subsequent offer with a different PT
// Regression test for https://issues.chromium.org/395077824
TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       MungeOfferCodecAndReOfferWorks) {
  // Munging allowed: kUnknownModification (1), kVideoCodecsFmtp (85)
  // Codec replacement VP9 -> AV1 is detected as kVideoCodecsFmtp because
  // it modifies params in-place.
  SetFieldTrials("WebRTC-NoSdpMangleAllowForTesting/Enabled,1,85/");
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddVideoTrack();
  bool munged = false;
  auto munger = [&munged](std::unique_ptr<SessionDescriptionInterface>& sdp) {
    auto video = GetFirstVideoContentDescription(sdp->description());
    auto codecs = video->codecs();
    std::optional<Codec> replacement_codec;
    for (auto&& codec : codecs) {
      if (codec.name == "AV1") {
        replacement_codec = codec;
        break;
      }
    }
    if (replacement_codec) {
      for (auto&& codec : codecs) {
        if (codec.name == "VP9") {
          RTC_LOG(LS_INFO) << "Remapping VP9 codec " << codec << " to AV1";
          codec.name = replacement_codec->name;
          codec.params = replacement_codec->params;
          munged = true;
          break;
        }
      }
      video->set_codecs(codecs);
    } else {
      RTC_LOG(LS_INFO) << "Skipping munge, no AV1 codec found";
    }
  };
  caller()->SetGeneratedSdpMunger(munger);
  caller()->CreateAndSetAndSignalOffer();
  // Skip rest of test if munge didn't work.
  if (!munged) {
    GTEST_SKIP() << "SDP munging did not replace codec, skipping.";
  }
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  caller()->SetGeneratedSdpMunger(nullptr);
  std::unique_ptr<SessionDescriptionInterface> offer =
      caller()->CreateOfferAndWait();
  ASSERT_NE(nullptr, offer);
  // The offer should be acceptable.
  EXPECT_TRUE(caller()->SetLocalDescriptionAndSendSdpMessage(std::move(offer)));
}

TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       MungeOfferCodecAndReOfferWorksWithSetCodecPreferencesIsAFootgun) {
  SetFieldTrials("WebRTC-NoSdpMangleAllowForTesting/Enabled,85/");
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddVideoTrack();
  bool has_munged = false;
  auto munger =
      [&has_munged](std::unique_ptr<SessionDescriptionInterface>& sdp) {
        auto video = GetFirstVideoContentDescription(sdp->description());
        auto codecs = video->codecs();
        std::optional<Codec> replacement_codec;
        for (auto&& codec : codecs) {
          if (codec.name == "VP9") {
            replacement_codec = codec;
            break;
          }
        }
        if (replacement_codec) {
          for (auto&& codec : codecs) {
            if (codec.name == "VP8") {
              RTC_LOG(LS_INFO) << "Remapping VP8 codec " << codec << " to VP9";
              codec.name = replacement_codec->name;
              codec.params = replacement_codec->params;
              has_munged = true;
              break;
            }
          }
          video->set_codecs(codecs);
        } else {
          RTC_LOG(LS_INFO) << "Skipping munge, no VP9 codec found ";
        }
      };
  caller()->SetGeneratedSdpMunger(munger);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  caller()->SetGeneratedSdpMunger(nullptr);

  if (!has_munged) {
    GTEST_SKIP() << "SDP munging did not replace codec, skipping.";
  }

  // Note currently negotiated codecs and count VP8 and VP9.
  auto codecs_after_munge = caller()
                                ->pc()
                                ->local_description()
                                ->description()
                                ->contents()[0]
                                .media_description()
                                ->codecs();
  size_t vp8_munge = 0;
  size_t vp9_munge = 0;
  for (const auto& codec : codecs_after_munge) {
    if (codec.name == "VP8")
      vp8_munge++;
    else if (codec.name == "VP9")
      vp9_munge++;
  }
  // We should have replaced VP8 with VP9.
  EXPECT_EQ(vp8_munge, 0u);
  EXPECT_GE(vp9_munge, 2u);

  // Call setCodecPreferences.
  std::vector<RtpCodecCapability> codecs =
      caller()
          ->pc_factory()
          ->GetRtpReceiverCapabilities(webrtc::MediaType::VIDEO)
          .codecs;
  auto transceivers = caller()->pc()->GetTransceivers();
  ASSERT_EQ(transceivers.size(), 1u);
  transceivers[0]->SetCodecPreferences(codecs);

  std::unique_ptr<SessionDescriptionInterface> offer =
      caller()->CreateOfferAndWait();
  ASSERT_THAT(offer, NotNull());
  // The offer should be acceptable.
  EXPECT_TRUE(caller()->SetLocalDescriptionAndSendSdpMessage(std::move(offer)));

  auto codecs_after_scp = caller()
                              ->pc()
                              ->local_description()
                              ->description()
                              ->contents()[0]
                              .media_description()
                              ->codecs();
  size_t vp8_scp = 0;
  size_t vp9_scp = 0;
  for (const auto& codec : codecs_after_scp) {
    if (codec.name == "VP8")
      vp8_scp++;
    else if (codec.name == "VP9")
      vp9_scp++;
  }
  // The SDP munging modification was reverted by sCP.
  // This is a footgun but please do not mix such munging with
  // setCodecPreferences, munging is on the way out.
  EXPECT_EQ(vp8_scp, 1u);
  EXPECT_GE(vp9_scp, 1u);
  EXPECT_GT(vp9_munge, vp9_scp);
}

TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       SensibleRtxWithDuplicateCodecs) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  caller()->AddVideoTrack();
  // Copied from WPT test webrtc/protocol/rtx-codecs.https.html
  std::string remote_offer_string =
      "v=0\r\n"
      "o=- 1878890426675213188 2 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "a=group:BUNDLE video\r\n"
      "a=msid-semantic: WMS\r\n"
      "m=video 9 UDP/TLS/RTP/SAVPF 96 97 98 99\r\n"
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtcp:9 IN IP4 0.0.0.0\r\n"
      "a=ice-ufrag:RGPK\r\n"
      "a=ice-pwd:rAyHEAKC7ckxQgWaRZXukz+Z\r\n"
      "a=ice-options:trickle\r\n"
      "a=fingerprint:sha-256 "
      "8C:29:0A:8F:11:06:BF:1C:58:B3:CA:E6:F1:F1:DC:99:4C:6C:89:E9:FF:BC:D4:38:"
      "11:18:1F:40:19:C8:49:37\r\n"
      "a=setup:actpass\r\n"
      "a=mid:video\r\n"
      "a=recvonly\r\n"
      "a=rtcp-mux\r\n"
      "a=rtpmap:96 VP8/90000\r\n"
      "a=rtpmap:97 rtx/90000\r\n"
      "a=fmtp:97 apt=98\r\n"
      "a=rtpmap:98 VP8/90000\r\n"
      "a=rtcp-fb:98 ccm fir\r\n"
      "a=rtcp-fb:98 nack\r\n"
      "a=rtcp-fb:98 nack pli\r\n"
      "a=rtcp-fb:98 goog-remb\r\n"
      "a=rtcp-fb:98 transport-cc\r\n"
      "a=rtpmap:99 rtx/90000\r\n"
      "a=fmtp:99 apt=96\r\n";
  auto srd_observer = make_ref_counted<MockSetSessionDescriptionObserver>();
  std::unique_ptr<SessionDescriptionInterface> remote_offer =
      CreateSessionDescription(SdpType::kOffer, remote_offer_string);
  EXPECT_TRUE(caller()->SetRemoteDescription(std::move(remote_offer)));
  // The resulting SDP answer should have one video codec with a correctly
  // associated RTX codec.
  std::unique_ptr<SessionDescriptionInterface> answer =
      caller()->CreateAnswerForTest();
  ASSERT_THAT(answer, NotNull());
  RTC_LOG(LS_ERROR) << "Answer is " << *answer;
  ASSERT_THAT(answer->description()->contents().size(), Eq(1));
  auto codecs =
      answer->description()->contents()[0].media_description()->codecs();
  std::vector<int> apt_values;
  for (const Codec& codec : codecs) {
    if (codec.GetResiliencyType() == Codec::ResiliencyType::kRtx) {
      const auto apt_it = codec.params.find(kCodecParamAssociatedPayloadType);
      int apt_value;
      ASSERT_TRUE(FromString(apt_it->second, &apt_value));
      apt_values.push_back(apt_value);
    }
  }
  for (int apt : apt_values) {
    EXPECT_THAT(codecs, Contains(Field("id", &Codec::id, apt)));
  }
}

// This test documents the behavior expected in
// https://issues.webrtc.org/412904801. It does not constitute a promise
// that this mechanism will go on working.
TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       MungeRawPacketizationChangesSubsequentSections) {
  // Munging allowed: kVideoCodecsModifiedWithRawPacketization (88)
  SetFieldTrials("WebRTC-NoSdpMangleAllowForTesting/Enabled,88/");
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  // Add first video track.
  caller()->AddVideoTrack();
  std::unique_ptr<SessionDescriptionInterface> offer =
      caller()->CreateOfferAndWait();
  EXPECT_EQ(offer->description()->contents().size(), 1U);
  // Observe that packetization is NOT raw.
  for (const auto& content : offer->description()->contents()) {
    for (const auto& codec : content.media_description()->codecs()) {
      ASSERT_THAT(codec.packetization, Eq(std::nullopt));
    }
  }
  // Mangle packetization to be raw.
  for (auto& content : offer->description()->contents()) {
    std::vector<Codec> codecs = content.media_description()->codecs();
    bool mangled_raw = false;
    for (auto& codec : codecs) {
      if (codec.name == "VP8" && codec.type == Codec::Type::kVideo) {
        codec.packetization = kPacketizationParamRaw;
        mangled_raw = true;
      }
    }
    ASSERT_TRUE(mangled_raw);
    content.media_description()->set_codecs(codecs);
  }
  // Set local description.
  auto observer = make_ref_counted<MockSetSessionDescriptionObserver>();
  caller()->pc()->SetLocalDescription(observer.get(), offer.release());
  // Wait for SLD to complete.
  EXPECT_TRUE(WaitUntil([&] { return observer->called(); }));
  // Add a second video track.
  caller()->AddVideoTrack();
  auto offer2 = caller()->CreateOfferAndWait();
  // Observe that packetization is raw on BOTH media sections.
  ASSERT_NE(offer2, nullptr);
  EXPECT_EQ(offer2->description()->contents().size(), 2U);
  for (const auto& content : offer2->description()->contents()) {
    for (const auto& codec : content.media_description()->codecs()) {
      if (codec.type == Codec::Type::kVideo && codec.name == "VP8") {
        EXPECT_EQ(codec.packetization, kPacketizationParamRaw);
      } else {
        EXPECT_THAT(codec.packetization, Eq(std::nullopt));
      }
    }
  }
}

TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       OfferAnswerWithAddTrackAndTweak) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignalingForSdpOnly();
  scoped_refptr<VideoTrackInterface> caller_track =
      caller()->CreateLocalVideoTrack();
  caller()->AddTrack(caller_track);
  ASSERT_TRUE(caller()
                  ->pc()
                  ->GetTransceivers()[0]
                  ->SetDirectionWithError(RtpTransceiverDirection::kSendOnly)
                  .ok());
  RtpTransceiverInit recvonly_init;
  recvonly_init.direction = RtpTransceiverDirection::kRecvOnly;
  ASSERT_TRUE(
      caller()->pc()->AddTransceiver(MediaType::VIDEO, recvonly_init).ok());

  scoped_refptr<VideoTrackInterface> callee_track =
      callee()->CreateLocalVideoTrack();
  callee()->AddTrack(callee_track);
  auto callee_transceiver = callee()->pc()->GetTransceivers()[0];
  callee_transceiver->SetDirectionWithError(RtpTransceiverDirection::kSendOnly);

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  EXPECT_THAT(caller()->pc()->GetTransceivers().size(), Eq(2));
  EXPECT_THAT(callee()->pc()->GetTransceivers().size(), Eq(2));
  EXPECT_TRUE(callee_transceiver->mid().has_value());
}

TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       OfferAnswerWithAddTransceiver) {
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignalingForSdpOnly();
  scoped_refptr<VideoTrackInterface> caller_track =
      caller()->CreateLocalVideoTrack();
  RtpTransceiverInit sendonly_init;
  sendonly_init.direction = RtpTransceiverDirection::kSendOnly;
  ASSERT_TRUE(caller()->pc()->AddTransceiver(caller_track, sendonly_init).ok());
  RtpTransceiverInit recvonly_init;
  recvonly_init.direction = RtpTransceiverDirection::kRecvOnly;
  ASSERT_TRUE(
      caller()->pc()->AddTransceiver(MediaType::VIDEO, recvonly_init).ok());
  scoped_refptr<VideoTrackInterface> callee_track =
      callee()->CreateLocalVideoTrack();
  auto receiver_video_transceiver =
      callee()->pc()->AddTransceiver(callee_track, sendonly_init).MoveValue();

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_THAT(caller()->pc()->GetTransceivers().size(), Eq(2));
  // Verify that the video-sending track has not gotten associated with
  // a media section.
  // This is likely not what we want. See this spec issue:
  // https://github.com/rtcweb-wg/jsep/issues/1040
  EXPECT_THAT(callee()->pc()->GetTransceivers().size(), Eq(3));
  EXPECT_FALSE(receiver_video_transceiver->mid().has_value());
}

#ifdef WEBRTC_HAVE_SCTP

TEST_P(PeerConnectionIntegrationTest, DtlsPqc) {
  CryptoOptions crypto_options;
  crypto_options.ephemeral_key_exchange_cipher_groups.AddFirst(
      webrtc::CryptoOptions::EphemeralKeyExchangeCipherGroups::
          kX25519_MLKEM768);

  PeerConnectionInterface::RTCConfiguration config;
  config.crypto_options = crypto_options;

  PeerConnectionFactoryInterface::Options options;
  options.ssl_max_version = SSL_PROTOCOL_DTLS_13;

  const bool create_media_engine = false;
  SetCallerPcWrapperAndReturnCurrent(CreatePeerConnectionWrapper(
      "caller", &options, &config, PeerConnectionDependencies(nullptr),
      /* event_log_factory= */ nullptr,
      /* reset_encoder_factory= */ false,
      /* reset_decoder_factory= */ false, create_media_engine));
  SetCalleePcWrapperAndReturnCurrent(CreatePeerConnectionWrapper(
      "callee", &options, &config, PeerConnectionDependencies(nullptr),
      /* event_log_factory= */ nullptr,
      /* reset_encoder_factory= */ false,
      /* reset_decoder_factory= */ false, create_media_engine));

  ConnectFakeSignaling();

  caller()->CreateDataChannel();

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] {
    return PeerConnectionStateIs(
        PeerConnectionInterface::PeerConnectionState::kConnected);
  }));

  uint16_t expected =
      webrtc::CryptoOptions::EphemeralKeyExchangeCipherGroups::kX25519_MLKEM768;
  if (!SSLStreamAdapter::IsBoringSsl()) {
    expected = webrtc::CryptoOptions::EphemeralKeyExchangeCipherGroups::kX25519;
  }
  EXPECT_EQ(caller()->dtls_transport_information().ssl_group_id(), expected);
  EXPECT_EQ(caller()->dtls_transport_information().ssl_group_id(), expected);
}

TEST_P(PeerConnectionIntegrationTest, DtlsPqcFieldTrial) {
  SetFieldTrials("WebRTC-EnableDtlsPqc/Enabled/");
  PeerConnectionInterface::RTCConfiguration config;
  PeerConnectionFactoryInterface::Options options;
  options.ssl_max_version = SSL_PROTOCOL_DTLS_13;

  const bool create_media_engine = false;
  SetCallerPcWrapperAndReturnCurrent(CreatePeerConnectionWrapper(
      "caller", &options, &config, PeerConnectionDependencies(nullptr),
      /* event_log_factory= */ nullptr,
      /* reset_encoder_factory= */ false,
      /* reset_decoder_factory= */ false, create_media_engine));
  SetCalleePcWrapperAndReturnCurrent(CreatePeerConnectionWrapper(
      "callee", &options, &config, PeerConnectionDependencies(nullptr),
      /* event_log_factory= */ nullptr,
      /* reset_encoder_factory= */ false,
      /* reset_decoder_factory= */ false, create_media_engine));

  ConnectFakeSignaling();

  caller()->CreateDataChannel();

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] {
    return PeerConnectionStateIs(
        PeerConnectionInterface::PeerConnectionState::kConnected);
  }));

  uint16_t expected =
      webrtc::CryptoOptions::EphemeralKeyExchangeCipherGroups::kX25519_MLKEM768;
  if (!SSLStreamAdapter::IsBoringSsl()) {
    expected = webrtc::CryptoOptions::EphemeralKeyExchangeCipherGroups::kX25519;
  }
  EXPECT_EQ(caller()->dtls_transport_information().ssl_group_id(), expected);
  EXPECT_EQ(caller()->dtls_transport_information().ssl_group_id(), expected);
}

TEST_P(PeerConnectionIntegrationTest,
       SpedWireTriggerStartsDtlsWithoutRemoteDescription) {
  if (!SSLStreamAdapter::IsBoringSsl()) {
    GTEST_SKIP() << "DTLS-in-STUN requires BoringSSL.";
  }
  SetFieldTrials("WebRTC-IceHandshakeDtls/Enabled/");
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  // Suppress SetRemoteDescription and ICE candidates from callee.
  caller()->SetReceivedSdpMunger(
      [](std::unique_ptr<SessionDescriptionInterface>& desc) { desc.reset(); });
  callee()->set_signal_ice_candidates(false);

  caller()->CreateDataChannel();
  caller()->CreateAndSetAndSignalOffer();

  ASSERT_THAT(
      WaitUntil(
          [&] {
            const auto& history = caller()->peer_connection_state_history();
            return std::find(history.begin(), history.end(),
                             PeerConnectionInterface::PeerConnectionState::
                                 kConnecting) != history.end();
          },
          IsTrue()),
      IsRtcOk());
}

TEST_P(PeerConnectionIntegrationTest, NoEarlyDtlsStartWhenSpedNotInAnswer) {
  if (!SSLStreamAdapter::IsBoringSsl()) {
    GTEST_SKIP() << "DTLS-in-STUN requires BoringSSL.";
  }
  SetFieldTrials(kCallerName, "WebRTC-IceHandshakeDtls/Enabled/");
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignalingForSdpOnly();
  caller()->CreateDataChannel();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_THAT(WaitUntil([&] { return SignalingStateStable(); }, IsTrue()),
              IsRtcOk());
  EXPECT_THAT(caller()->peer_connection_state_history(),
              ::testing::Not(::testing::Contains(
                  PeerConnectionInterface::PeerConnectionState::kConnecting)));
}

#endif  // WEBRTC_HAVE_SCTP

TEST_P(PeerConnectionIntegrationTest, PerPeerConnectionHeaderExtensions) {
  SetFieldTrials("caller", "WebRTC-VideoFrameTrackingIdAdvertised/Enabled/");
  SetFieldTrials("callee", "WebRTC-VideoFrameTrackingIdAdvertised/Disabled/");
  PeerConnectionInterface::RTCConfiguration config;
  PeerConnectionFactoryInterface::Options options;
  options.ssl_max_version = SSL_PROTOCOL_DTLS_13;

  const bool create_media_engine = true;
  SetCallerPcWrapperAndReturnCurrent(CreatePeerConnectionWrapper(
      "caller", &options, &config, PeerConnectionDependencies(nullptr),
      /* event_log_factory= */ nullptr,
      /* reset_encoder_factory= */ false,
      /* reset_decoder_factory= */ false, create_media_engine));
  SetCalleePcWrapperAndReturnCurrent(CreatePeerConnectionWrapper(
      "callee", &options, &config, PeerConnectionDependencies(nullptr),
      /* event_log_factory= */ nullptr,
      /* reset_encoder_factory= */ false,
      /* reset_decoder_factory= */ false, create_media_engine));

  const std::string uri =
      "http://www.webrtc.org/experiments/rtp-hdrext/video-frame-tracking-id";
  {
    scoped_refptr<VideoTrackInterface> caller_track =
        caller()->CreateLocalVideoTrack();
    scoped_refptr<RtpSenderInterface> caller_sender =
        caller()->AddTrack(caller_track);

    auto session_description = caller()->CreateOfferAndWait();
    EXPECT_THAT(session_description->description()
                    ->contents()[0]
                    .media_description()
                    ->rtp_header_extensions(),
                Contains(Field(&RtpExtension::uri, uri)));
  }

  {
    scoped_refptr<VideoTrackInterface> callee_track =
        callee()->CreateLocalVideoTrack();

    scoped_refptr<RtpSenderInterface> callee_sender =
        callee()->AddTrack(callee_track);
    auto session_description = callee()->CreateOfferAndWait();
    EXPECT_THAT(session_description->description()
                    ->contents()[0]
                    .media_description()
                    ->rtp_header_extensions(),
                Not(Contains(Field(&RtpExtension::uri, uri))));
  }
}

TEST_P(PeerConnectionIntegrationTest, CryptexRenegotiation) {
  CryptoOptions crypto_options;
  crypto_options.srtp.cryptex_policy =
      CryptoOptions::Srtp::CryptexPolicy::kNegotiate;
  PeerConnectionInterface::RTCConfiguration config;
  config.crypto_options = crypto_options;

  const bool create_media_engine = true;
  SetCallerPcWrapperAndReturnCurrent(CreatePeerConnectionWrapper(
      "caller", nullptr, &config, PeerConnectionDependencies(nullptr),
      /* event_log_factory= */ nullptr,
      /* reset_encoder_factory= */ false,
      /* reset_decoder_factory= */ false, create_media_engine));
  SetCalleePcWrapperAndReturnCurrent(CreatePeerConnectionWrapper(
      "callee", nullptr, &config, PeerConnectionDependencies(nullptr),
      /* event_log_factory= */ nullptr,
      /* reset_encoder_factory= */ false,
      /* reset_decoder_factory= */ false, create_media_engine));

  ConnectFakeSignaling();

  caller()->AddAudioVideoTracks();
  // Strip cryptex from the answer the caller receives so the caller observes
  // a peer that does not negotiate cryptex.
  caller()->SetReceivedSdpMunger(
      [](std::unique_ptr<SessionDescriptionInterface>& sdp) {
        sdp->description()->set_cryptex(false);
      });

  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] {
    return PeerConnectionStateIs(
        PeerConnectionInterface::PeerConnectionState::kConnected);
  }));
  caller()->SetReceivedSdpMunger(nullptr);
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
}

#ifdef WEBRTC_HAVE_SCTP

class DowngradeLogSink : public LogSink {
 public:
  void OnLogMessage(const std::string& message) override {
    if (message.find("Received unexpected non-DTLS packet") !=
        std::string::npos) {
      found_ = true;
    }
  }
  bool found() const { return found_; }

 private:
  bool found_ = false;
};

// This test reproduces the vulnerability where a kPrAnswer with a BUNDLE group
// but no fingerprint for a media section can cause that media section to use
// an unencrypted transport.
TEST_P(PeerConnectionIntegrationTest,
       DataChannelEncryptionDowngradeViaBundleDesyncInPrAnswer) {
  RTCConfiguration config;
  config.bundle_policy = PeerConnectionInterface::kBundlePolicyBalanced;
  // Munging allowed: kBundle (33)
  SetFieldTrials("Callee", "WebRTC-NoSdpMangleAllowForTesting/Enabled,33/");
  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfig(config, config));
  ConnectFakeSignaling();

  // 1. Initial negotiation without BUNDLE.
  // We munge the answer to remove BUNDLE so that separate transports are
  // created.
  callee()->SetGeneratedSdpMunger(
      [](std::unique_ptr<SessionDescriptionInterface>& desc) {
        desc->description()->RemoveGroupByName(GROUP_TYPE_BUNDLE);
      });

  caller()->AddVideoTrack();
  caller()->CreateDataChannel();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_TRUE(WaitUntil([&] { return DtlsConnected(); }));

  // 2. Renegotiation with BUNDLE in offer, and malicious prAnswer.
  // The malicious prAnswer will have a BUNDLE group containing mid 0 and mid 1,
  // but mid 1 (Data Channel) will have no fingerprint.
  callee()->SetGeneratedSdpMunger(nullptr);

  // Create an offer.
  std::unique_ptr<SessionDescriptionInterface> offer =
      caller()->CreateOfferAndWait();
  ASSERT_THAT(offer, NotNull());

  // Set local offer on caller.
  auto observer = make_ref_counted<FakeSetLocalDescriptionObserver>();
  caller()->pc()->SetLocalDescription(std::move(offer), observer);
  ASSERT_TRUE(WaitUntil([&] { return observer->called(); }));

  // Set remote offer on callee.
  std::unique_ptr<SessionDescriptionInterface> offer_for_callee =
      caller()->CreateOfferAndWait();
  ASSERT_THAT(offer_for_callee, NotNull());
  ASSERT_TRUE(callee()->SetRemoteDescription(std::move(offer_for_callee)));

  // Callee creates a normal answer first.
  auto answer_observer =
      make_ref_counted<MockCreateSessionDescriptionObserver>();
  callee()->pc()->CreateAnswer(
      answer_observer.get(), PeerConnectionInterface::RTCOfferAnswerOptions());
  ASSERT_TRUE(WaitUntil([&] { return answer_observer->called(); }));
  std::unique_ptr<SessionDescriptionInterface> answer =
      answer_observer->MoveDescription();
  ASSERT_THAT(answer, NotNull());

  // Convert the answer to a prAnswer by serializing and re-parsing.
  std::string sdp = answer->ToString();
  ASSERT_FALSE(sdp.empty());
  std::unique_ptr<SessionDescriptionInterface> pr_answer =
      CreateSessionDescription(SdpType::kPrAnswer, sdp);
  ASSERT_THAT(pr_answer, NotNull());

  // Munge the prAnswer:
  // - Ensure BUNDLE group is present.
  // - Remove fingerprint from the second media section (mid 1).
  SessionDescription* desc = pr_answer->description();
  const ContentGroup* bundle_group = desc->GetGroupByName(GROUP_TYPE_BUNDLE);
  ASSERT_THAT(bundle_group, NotNull());
  ASSERT_THAT(bundle_group->content_names(), SizeIs(2));

  // Remove fingerprint from the second content.
  const ContentInfos& contents = desc->contents();
  ASSERT_GE(contents.size(), 2u);
  TransportInfo* transport_info =
      desc->GetTransportInfoByName(contents[1].mid());
  ASSERT_THAT(transport_info, NotNull());
  transport_info->description.identity_fingerprint.reset();

  // Set the remote prAnswer on the caller.
  // This succeeds because VerifyCrypto skips the fingerprint check for
  // bundled sections (except the first).
  ASSERT_TRUE(caller()->SetRemoteDescription(std::move(pr_answer)));

  // With the fix, the Data Channel's transport will now be BUNDLED into mid 0.
  // Since mid 0 has DTLS active, mid 1's data will also be DTLS-wrapped.
  // We can verify this by checking that the SCTP transport's internal DTLS
  // transport is active.
  EXPECT_TRUE(network_thread()->BlockingCall([&] {
    return static_cast<SctpTransport*>(caller()->pc()->GetSctpTransport().get())
        ->internal()
        ->dtls_transport()
        ->IsDtlsActive();
  }));

  // If the bug were present, it would be sent as raw SCTP and trigger an error
  // on the peer.

  DowngradeLogSink downgrade_log_sink;
  LogMessage::AddLogToStream(&downgrade_log_sink, LS_ERROR);

  // Send data from caller.
  ASSERT_EQ(caller()->data_channels().size(), 1u);
  ASSERT_TRUE(WaitUntil([&] {
    return caller()->data_channels()[0]->state() == DataChannelInterface::kOpen;
  }));
  caller()->data_channels()[0]->Send(DataBuffer("SECRET-PLAINTEXT"));

  // If the fix is correct, no non-DTLS packets should be received.
  // We wait a bit to ensure packets have time to arrive.
  Thread::Current()->ProcessMessages(500);

  bool found = downgrade_log_sink.found();
  LogMessage::RemoveLogToStream(&downgrade_log_sink);

  EXPECT_FALSE(found) << "Security downgrade detected: raw SCTP packets sent "
                         "instead of DTLS-wrapped packets.";
}

// This test verifies that establishing BUNDLE in a prAnswer does not cause
// the destruction of transports that might be needed if the final answer
// removes BUNDLE.
TEST_P(PeerConnectionIntegrationTest,
       BundleEstablishedInPrAnswerThenRemovedInFinalAnswer) {
  RTCConfiguration config;
  config.bundle_policy = PeerConnectionInterface::kBundlePolicyBalanced;
  // Munging allowed: kBundle (33)
  SetFieldTrials("Callee", "WebRTC-NoSdpMangleAllowForTesting/Enabled,33/");
  ASSERT_TRUE(CreatePeerConnectionWrappersWithConfig(config, config));
  ConnectFakeSignaling();

  // 1. Initial negotiation without BUNDLE.
  callee()->SetGeneratedSdpMunger(
      [](std::unique_ptr<SessionDescriptionInterface>& desc) {
        desc->description()->RemoveGroupByName(GROUP_TYPE_BUNDLE);
      });

  caller()->AddVideoTrack();
  caller()->CreateDataChannel();
  caller()->CreateAndSetAndSignalOffer();
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  ASSERT_TRUE(WaitUntil([&] { return DtlsConnected(); }));

  // 2. Renegotiation with BUNDLE in offer and prAnswer, then NO BUNDLE in
  // answer.
  std::unique_ptr<SessionDescriptionInterface> offer =
      caller()->CreateOfferAndWait();
  ASSERT_THAT(offer, NotNull());

  // Set local offer on caller.
  auto observer = make_ref_counted<FakeSetLocalDescriptionObserver>();
  caller()->pc()->SetLocalDescription(std::move(offer), observer);
  ASSERT_TRUE(WaitUntil([&] { return observer->called(); }));

  // Set remote offer on callee.
  std::unique_ptr<SessionDescriptionInterface> offer_for_callee =
      caller()->CreateOfferAndWait();
  ASSERT_THAT(offer_for_callee, NotNull());
  ASSERT_TRUE(callee()->SetRemoteDescription(std::move(offer_for_callee)));

  // Callee creates a prAnswer WITH BUNDLE.
  auto answer_observer =
      make_ref_counted<MockCreateSessionDescriptionObserver>();
  callee()->pc()->CreateAnswer(
      answer_observer.get(), PeerConnectionInterface::RTCOfferAnswerOptions());
  ASSERT_TRUE(WaitUntil([&] { return answer_observer->called(); }));
  std::unique_ptr<SessionDescriptionInterface> answer =
      answer_observer->MoveDescription();
  ASSERT_THAT(answer, NotNull());

  std::string sdp = answer->ToString();
  ASSERT_FALSE(sdp.empty());
  std::unique_ptr<SessionDescriptionInterface> pr_answer =
      CreateSessionDescription(SdpType::kPrAnswer, sdp);
  ASSERT_THAT(pr_answer, NotNull());

  // Set the remote prAnswer on the caller.
  // This establishes BUNDLE.
  const ContentGroup* bundle_group =
      pr_answer->description()->GetGroupByName(GROUP_TYPE_BUNDLE);
  ASSERT_THAT(bundle_group, NotNull());
  ASSERT_THAT(bundle_group->content_names(), SizeIs(2));

  ASSERT_TRUE(caller()->SetRemoteDescription(std::move(pr_answer)));

  // 3. Final answer WITHOUT BUNDLE.
  auto final_answer_observer =
      make_ref_counted<MockCreateSessionDescriptionObserver>();
  callee()->pc()->CreateAnswer(
      final_answer_observer.get(),
      PeerConnectionInterface::RTCOfferAnswerOptions());
  ASSERT_TRUE(WaitUntil([&] { return final_answer_observer->called(); }));
  std::unique_ptr<SessionDescriptionInterface> final_answer =
      final_answer_observer->MoveDescription();
  ASSERT_THAT(final_answer, NotNull());

  // Munge it to remove BUNDLE.
  final_answer->description()->RemoveGroupByName(GROUP_TYPE_BUNDLE);

  // Verify it has no bundle.
  ASSERT_FALSE(final_answer->description()->GetGroupByName(GROUP_TYPE_BUNDLE));

  // Set the final remote answer on the caller.
  // This should FAIL because mid 0 was already bundled with mid 1 in the
  // prAnswer, and RFC 8843 says you can't remove an m= section from a BUNDLE
  // group once it's been established.
  ASSERT_FALSE(caller()->SetRemoteDescription(std::move(final_answer)));
}

#endif  // WEBRTC_HAVE_SCTP

TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       MungeOfferCodecAndReOfferCausesNoDuplicateId) {
  // Munging allowed: kUnknownModification (codec replacement VP9 -> AV1)
  SetFieldTrials("WebRTC-NoSdpMangleAllowForTesting/Enabled,1/");
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddVideoTrack();
  caller()->AddAudioTrack();
  bool munged = false;
  auto munger = [&munged](std::unique_ptr<SessionDescriptionInterface>& sdp) {
    VideoContentDescription* video =
        GetFirstVideoContentDescription(sdp->description());
    std::vector<Codec> codecs = video->codecs();
    // Check that AV1 is present. If not, the munged SDP won't be accepted
    // by SetLocalDescription.
    if (!absl::c_any_of(
            codecs, [](const Codec& codec) { return codec.name == "AV1"; })) {
      return;
    }
    for (auto& codec : codecs) {
      if (codec.name == "VP9") {
        RTC_LOG(LS_INFO) << "Remapping VP9 codec " << codec << " to AV1";
        codec.name = "AV1";
        munged = true;
      }
    }
    video->set_codecs(codecs);
  };
  caller()->SetGeneratedSdpMunger(munger);
  caller()->CreateAndSetAndSignalOffer();
  if (!munged) {
    GTEST_SKIP() << "Test skipped, codec remapping did not work";
  }
  ASSERT_TRUE(WaitUntil([&] { return SignalingStateStable(); }));
  EXPECT_TRUE(ValidateBundledPayloadTypes(
                  *caller()->pc()->local_description()->description())
                  .ok());
  EXPECT_TRUE(ValidateBundledPayloadTypes(
                  *caller()->pc()->remote_description()->description())
                  .ok());
  caller()->SetGeneratedSdpMunger(nullptr);
  auto offer = caller()->CreateOfferAndWait();
  ASSERT_THAT(offer, NotNull());
  // The offer should be acceptable.
  EXPECT_TRUE(ValidateBundledPayloadTypes(*offer->description()).ok());
  EXPECT_TRUE(caller()->SetLocalDescriptionAndSendSdpMessage(std::move(offer)));
}

TEST_F(PeerConnectionIntegrationTestUnifiedPlan,
       MungeOfferCodecWithNonsenseFailsAtSetLocalDescription) {
  // Munging allowed: kUnknownModification (codec replacement VP9 -> NONSENSE)
  SetFieldTrials("WebRTC-NoSdpMangleAllowForTesting/Enabled,1/");
  ASSERT_TRUE(CreatePeerConnectionWrappers());
  ConnectFakeSignaling();
  caller()->AddVideoTrack();
  caller()->AddAudioTrack();
  bool munged = false;
  auto munger = [&munged](std::unique_ptr<SessionDescriptionInterface>& sdp) {
    VideoContentDescription* video =
        GetFirstVideoContentDescription(sdp->description());
    std::vector<Codec> codecs = video->codecs();
    for (auto& codec : codecs) {
      if (codec.name == "VP9") {
        RTC_LOG(LS_ERROR) << "Remapping VP9 codec " << codec << " to NONSENSE";
        codec.name = "NONSENSE";
        munged = true;
      }
    }
    video->set_codecs(codecs);
  };
  caller()->SetGeneratedSdpMunger(munger);
  std::unique_ptr<SessionDescriptionInterface> offer =
      caller()->CreateOfferAndWait();
  ASSERT_THAT(offer, NotNull());
  // If the munger failed to find a VP9 codec to munge, don't test.
  if (!munged) {
    GTEST_SKIP() << "Replacement of codec failed, skipping test";
  }
  auto observer = make_ref_counted<MockSetSessionDescriptionObserver>();
  caller()->pc()->SetLocalDescription(observer.get(), offer.release());
  ASSERT_TRUE(WaitUntil([&] { return observer->called(); }));
  // Observe failure.
  EXPECT_FALSE(observer->result());
}

}  // namespace

}  // namespace webrtc
