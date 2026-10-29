/*
 *  Copyright 2017 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// This file contains tests that check the interaction between the
// PeerConnection and the underlying media engine, as well as tests that check
// the media-related aspects of SDP.

#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/strings/string_view.h"
#include "api/create_modular_peer_connection_factory.h"
#include "api/environment/environment.h"
#include "api/jsep.h"
#include "api/media_types.h"
#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "api/rtc_event_log/rtc_event_log_factory.h"
#include "api/rtp_parameters.h"
#include "api/rtp_sender_interface.h"
#include "api/rtp_transceiver_direction.h"
#include "api/rtp_transceiver_interface.h"
#include "api/scoped_refptr.h"
#include "api/test/rtc_error_matchers.h"
#include "api/video_codecs/sdp_video_format.h"
#include "media/base/codec.h"
#include "media/base/fake_media_engine.h"
#include "media/base/media_channel.h"
#include "media/base/media_constants.h"
#include "media/base/media_engine.h"
#include "media/base/stream_params.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/transport_info.h"
#include "p2p/test/fake_port_allocator.h"
#include "pc/media_session.h"
#include "pc/peer_connection_wrapper.h"
#include "pc/rtp_media_utils.h"
#include "pc/rtp_transceiver.h"
#include "pc/session_description.h"
#include "pc/test/enable_fake_media.h"
#include "pc/test/full_codec_matrix.h"
#include "pc/test/mock_peer_connection_observers.h"
#include "rtc_base/checks.h"
#include "rtc_base/ref_counted_object.h"
#include "rtc_base/thread.h"
#include "rtc_base/virtual_socket_server.h"
#include "test/create_test_environment.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/run_loop.h"

namespace webrtc {

using RTCConfiguration = PeerConnectionInterface::RTCConfiguration;
using RTCOfferAnswerOptions = PeerConnectionInterface::RTCOfferAnswerOptions;
using ::testing::Bool;
using ::testing::Combine;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::Gt;
using ::testing::HasSubstr;
using ::testing::NotNull;
using ::testing::StrCaseEq;
using ::testing::UnorderedElementsAreArray;
using ::testing::Values;

RtpTransceiver* RtpTransceiverInternal(
    scoped_refptr<RtpTransceiverInterface> transceiver) {
  auto transceiver_with_internal = static_cast<
      RefCountedObject<RtpTransceiverProxyWithInternal<RtpTransceiver>>*>(
      transceiver.get());
  auto transceiver_internal =
      static_cast<RtpTransceiver*>(transceiver_with_internal->internal());
  return transceiver_internal;
}

MediaSendChannelInterface* SendChannelInternal(
    scoped_refptr<RtpTransceiverInterface> transceiver) {
  auto transceiver_internal = RtpTransceiverInternal(transceiver);
  return transceiver_internal->media_send_channel();
}

MediaReceiveChannelInterface* ReceiveChannelInternal(
    scoped_refptr<RtpTransceiverInterface> transceiver) {
  auto transceiver_internal = RtpTransceiverInternal(transceiver);
  return transceiver_internal->media_receive_channel();
}

FakeVideoMediaSendChannel* VideoMediaSendChannel(
    scoped_refptr<RtpTransceiverInterface> transceiver) {
  return static_cast<FakeVideoMediaSendChannel*>(
      SendChannelInternal(transceiver));
}
FakeVideoMediaReceiveChannel* VideoMediaReceiveChannel(
    scoped_refptr<RtpTransceiverInterface> transceiver) {
  return static_cast<FakeVideoMediaReceiveChannel*>(
      ReceiveChannelInternal(transceiver));
}
FakeVoiceMediaSendChannel* VoiceMediaSendChannel(
    scoped_refptr<RtpTransceiverInterface> transceiver) {
  return static_cast<FakeVoiceMediaSendChannel*>(
      SendChannelInternal(transceiver));
}
FakeVoiceMediaReceiveChannel* VoiceMediaReceiveChannel(
    scoped_refptr<RtpTransceiverInterface> transceiver) {
  return static_cast<FakeVoiceMediaReceiveChannel*>(
      ReceiveChannelInternal(transceiver));
}

class PeerConnectionWrapperForMediaTest : public PeerConnectionWrapper {
 public:
  using PeerConnectionWrapper::PeerConnectionWrapper;

  FakeMediaEngine* media_engine() { return media_engine_; }
  void set_media_engine(FakeMediaEngine* media_engine) {
    media_engine_ = media_engine;
  }

 private:
  FakeMediaEngine* media_engine_;
};

class PeerConnectionMediaBaseTest : public ::testing::Test {
 protected:
  typedef std::unique_ptr<PeerConnectionWrapperForMediaTest> WrapperPtr;

  explicit PeerConnectionMediaBaseTest(SdpSemantics sdp_semantics)
      : vss_(new VirtualSocketServer()),
        main_(vss_.get()),
        sdp_semantics_(sdp_semantics) {}

  WrapperPtr CreatePeerConnection() {
    return CreatePeerConnection(RTCConfiguration());
  }

  WrapperPtr CreatePeerConnection(const RTCConfiguration& config) {
    return CreatePeerConnection(config, std::make_unique<FakeMediaEngine>());
  }

  WrapperPtr CreatePeerConnection(
      std::unique_ptr<FakeMediaEngine> media_engine) {
    return CreatePeerConnection(RTCConfiguration(), std::move(media_engine));
  }

  // Creates PeerConnectionFactory and PeerConnection for given configuration.
  WrapperPtr CreatePeerConnection(
      const RTCConfiguration& config,
      std::unique_ptr<FakeMediaEngine> media_engine) {
    auto* media_engine_ptr = media_engine.get();

    Environment env = CreateTestEnvironment();

    PeerConnectionFactoryDependencies factory_dependencies;

    factory_dependencies.network_thread = Thread::Current();
    factory_dependencies.signaling_thread = Thread::Current();
    EnableFakeMedia(factory_dependencies, std::move(media_engine));
    factory_dependencies.event_log_factory =
        std::make_unique<RtcEventLogFactory>();
    factory_dependencies.env = env;
    auto pc_factory =
        CreateModularPeerConnectionFactory(std::move(factory_dependencies));

    auto fake_port_allocator =
        std::make_unique<FakePortAllocator>(env, vss_.get());
    auto observer = std::make_unique<MockPeerConnectionObserver>();
    auto modified_config = config;
    modified_config.sdp_semantics = sdp_semantics_;
    PeerConnectionDependencies pc_dependencies(observer.get());
    pc_dependencies.allocator = std::move(fake_port_allocator);

    auto result = pc_factory->CreatePeerConnectionOrError(
        modified_config, std::move(pc_dependencies));
    if (!result.ok()) {
      return nullptr;
    }

    auto pc = result.MoveValue();
    observer->SetPeerConnectionInterface(pc.get());
    auto wrapper = std::make_unique<PeerConnectionWrapperForMediaTest>(
        pc_factory, pc, std::move(observer));
    wrapper->set_media_engine(media_engine_ptr);
    return wrapper;
  }

  // Accepts the same arguments as CreatePeerConnection and adds default audio
  // track (but no video).
  template <typename... Args>
  WrapperPtr CreatePeerConnectionWithAudio(Args&&... args) {
    auto wrapper = CreatePeerConnection(std::forward<Args>(args)...);
    if (!wrapper) {
      return nullptr;
    }
    wrapper->AddAudioTrack("a");
    return wrapper;
  }

  // Accepts the same arguments as CreatePeerConnection and adds default video
  // track (but no audio).
  template <typename... Args>
  WrapperPtr CreatePeerConnectionWithVideo(Args&&... args) {
    auto wrapper = CreatePeerConnection(std::forward<Args>(args)...);
    if (!wrapper) {
      return nullptr;
    }
    wrapper->AddVideoTrack("v");
    return wrapper;
  }

  // Accepts the same arguments as CreatePeerConnection and adds default audio
  // and video tracks.
  template <typename... Args>
  WrapperPtr CreatePeerConnectionWithAudioVideo(Args&&... args) {
    auto wrapper = CreatePeerConnection(std::forward<Args>(args)...);
    if (!wrapper) {
      return nullptr;
    }
    wrapper->AddAudioTrack("a");
    wrapper->AddVideoTrack("v");
    return wrapper;
  }

  RtpTransceiverDirection GetMediaContentDirection(
      const SessionDescriptionInterface* sdesc,
      MediaType media_type) {
    auto* content = GetFirstMediaContent(sdesc->description(), media_type);
    RTC_DCHECK(content);
    return content->media_description()->direction();
  }

  bool IsUnifiedPlan() const {
    return sdp_semantics_ == SdpSemantics::kUnifiedPlan;
  }

  std::unique_ptr<VirtualSocketServer> vss_;
  test::RunLoop main_;
  const SdpSemantics sdp_semantics_;
};

class PeerConnectionMediaTest
    : public PeerConnectionMediaBaseTest,
      public ::testing::WithParamInterface<SdpSemantics> {
 protected:
  PeerConnectionMediaTest() : PeerConnectionMediaBaseTest(GetParam()) {}
};

class PeerConnectionMediaTestUnifiedPlan : public PeerConnectionMediaBaseTest {
 protected:
  PeerConnectionMediaTestUnifiedPlan()
      : PeerConnectionMediaBaseTest(SdpSemantics::kUnifiedPlan) {}
};

class PeerConnectionMediaTestPlanB : public PeerConnectionMediaBaseTest {
 protected:
  PeerConnectionMediaTestPlanB()
      : PeerConnectionMediaBaseTest(SdpSemantics::kPlanB_DEPRECATED) {}
};

std::vector<std::string> GetIds(const std::vector<StreamParams>& streams) {
  std::vector<std::string> ids;
  ids.reserve(streams.size());
  for (const auto& stream : streams) {
    ids.push_back(stream.id);
  }
  return ids;
}

// Test that exchanging an offer and answer with each side having an audio and
// video stream creates the appropriate send/recv streams in the underlying
// media engine on both sides.
TEST_F(PeerConnectionMediaTestUnifiedPlan,
       AudioVideoOfferAnswerCreateSendRecvStreams) {
  const std::string kCallerAudioId = "caller_a";
  const std::string kCallerVideoId = "caller_v";
  const std::string kCalleeAudioId = "callee_a";
  const std::string kCalleeVideoId = "callee_v";

  auto caller = CreatePeerConnection();
  caller->AddAudioTrack(kCallerAudioId);
  caller->AddVideoTrack(kCallerVideoId);

  auto callee = CreatePeerConnection();
  callee->AddAudioTrack(kCalleeAudioId);
  callee->AddVideoTrack(kCalleeVideoId);

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));
  ASSERT_TRUE(
      caller->SetRemoteDescription(callee->CreateAnswerAndSetAsLocal()));

  auto* caller_voice_send =
      VoiceMediaSendChannel(caller->pc()->GetTransceivers()[0]);
  auto* caller_voice_receive =
      VoiceMediaReceiveChannel(caller->pc()->GetTransceivers()[0]);
  EXPECT_THAT(GetIds(caller_voice_receive->recv_streams()),
              ElementsAre(kCalleeAudioId));
  EXPECT_THAT(GetIds(caller_voice_send->send_streams()),
              ElementsAre(kCallerAudioId));

  auto* caller_video_send =
      VideoMediaSendChannel(caller->pc()->GetTransceivers()[1]);
  auto* caller_video_receive =
      VideoMediaReceiveChannel(caller->pc()->GetTransceivers()[1]);
  EXPECT_THAT(GetIds(caller_video_receive->recv_streams()),
              ElementsAre(kCalleeVideoId));
  EXPECT_THAT(GetIds(caller_video_send->send_streams()),
              ElementsAre(kCallerVideoId));

  auto* callee_voice_send =
      VoiceMediaSendChannel(callee->pc()->GetTransceivers()[0]);
  auto* callee_voice_receive =
      VoiceMediaReceiveChannel(callee->pc()->GetTransceivers()[0]);
  EXPECT_THAT(GetIds(callee_voice_receive->recv_streams()),
              ElementsAre(kCallerAudioId));
  EXPECT_THAT(GetIds(callee_voice_send->send_streams()),
              ElementsAre(kCalleeAudioId));

  auto* callee_video_send =
      VideoMediaSendChannel(callee->pc()->GetTransceivers()[1]);
  auto* callee_video_receive =
      VideoMediaReceiveChannel(callee->pc()->GetTransceivers()[1]);
  EXPECT_THAT(GetIds(callee_video_receive->recv_streams()),
              ElementsAre(kCallerVideoId));
  EXPECT_THAT(GetIds(callee_video_send->send_streams()),
              ElementsAre(kCalleeVideoId));
}

// Test that stopping the caller transceivers causes the media channels on the
// callee to be destroyed after calling SetRemoteDescription on the generated
// offer.
// See next test for equivalent behavior with Plan B semantics.
TEST_F(PeerConnectionMediaTestUnifiedPlan,
       StoppedRemoteTransceiversRemovesMediaChannels) {
  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnection();

  ASSERT_TRUE(caller->ExchangeOfferAnswerWith(callee.get()));

  // Stop both audio and video transceivers on the caller.
  auto transceivers = caller->pc()->GetTransceivers();
  ASSERT_EQ(2u, transceivers.size());
  transceivers[0]->StopInternal();
  transceivers[1]->StopInternal();

  ASSERT_TRUE(caller->ExchangeOfferAnswerWith(callee.get()));

  ASSERT_TRUE(callee->pc()->GetTransceivers().empty());
}

// Test that removing streams from a subsequent offer causes the receive streams
// on the callee to be removed.
// See previous test for equivalent behavior with Unified Plan semantics.
TEST_F(PeerConnectionMediaTestPlanB, EmptyRemoteOfferRemovesRecvStreams) {
  auto caller = CreatePeerConnection();
  auto caller_audio_track = caller->AddAudioTrack("a");
  auto caller_video_track = caller->AddVideoTrack("v");
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(caller->ExchangeOfferAnswerWith(callee.get()));

  // Remove both tracks from caller.
  caller->pc()->RemoveTrackOrError(caller_audio_track);
  caller->pc()->RemoveTrackOrError(caller_video_track);

  ASSERT_TRUE(caller->ExchangeOfferAnswerWith(callee.get()));

  EXPECT_TRUE(callee->pc()->GetReceivers().empty());
  EXPECT_EQ(2u, callee->pc()->GetSenders().size());
}

// Test enabling of simulcast with Plan B semantics.
// This test creating an offer.
TEST_F(PeerConnectionMediaTestPlanB, SimulcastOffer) {
  auto caller = CreatePeerConnection();
  auto caller_video_track = caller->AddVideoTrack("v");
  RTCOfferAnswerOptions options;
  options.num_simulcast_layers = 3;
  std::unique_ptr<SessionDescriptionInterface> offer =
      caller->CreateOffer(options);
  auto* description =
      GetFirstMediaContent(offer->description(), MediaType::VIDEO)
          ->media_description();
  ASSERT_EQ(1u, description->streams().size());
  ASSERT_TRUE(description->streams()[0].get_ssrc_group("SIM"));
  EXPECT_EQ(3u, description->streams()[0].get_ssrc_group("SIM")->ssrcs.size());

  // Check that it actually creates simulcast aswell.
  caller->SetLocalDescription(std::move(offer));
  auto senders = caller->pc()->GetSenders();
  ASSERT_EQ(1u, senders.size());
  EXPECT_EQ(MediaType::VIDEO, senders[0]->media_type());
  EXPECT_EQ(3u, senders[0]->GetParameters().encodings.size());
}

// Test enabling of simulcast with Plan B semantics.
// This test creating an answer.
TEST_F(PeerConnectionMediaTestPlanB, SimulcastAnswer) {
  auto caller = CreatePeerConnection();
  caller->AddVideoTrack("v0");
  std::unique_ptr<SessionDescriptionInterface> offer = caller->CreateOffer();
  auto callee = CreatePeerConnection();
  auto callee_video_track = callee->AddVideoTrack("v1");
  ASSERT_TRUE(callee->SetRemoteDescription(std::move(offer)));
  RTCOfferAnswerOptions options;
  options.num_simulcast_layers = 3;
  std::unique_ptr<SessionDescriptionInterface> answer =
      callee->CreateAnswer(options);
  auto* description =
      GetFirstMediaContent(answer->description(), MediaType::VIDEO)
          ->media_description();
  ASSERT_EQ(1u, description->streams().size());
  ASSERT_TRUE(description->streams()[0].get_ssrc_group("SIM"));
  EXPECT_EQ(3u, description->streams()[0].get_ssrc_group("SIM")->ssrcs.size());

  // Check that it actually creates simulcast aswell.
  callee->SetLocalDescription(std::move(answer));
  auto senders = callee->pc()->GetSenders();
  ASSERT_EQ(1u, senders.size());
  EXPECT_EQ(MediaType::VIDEO, senders[0]->media_type());
  EXPECT_EQ(3u, senders[0]->GetParameters().encodings.size());
}

// Test that stopping the callee transceivers causes the media channels to be
// destroyed on the callee after calling SetLocalDescription on the local
// answer.
// See next test for equivalent behavior with Plan B semantics.
TEST_F(PeerConnectionMediaTestUnifiedPlan,
       StoppedLocalTransceiversRemovesMediaChannels) {
  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(caller->ExchangeOfferAnswerWith(callee.get()));

  // Stop both audio and video transceivers on the callee.
  auto transceivers = callee->pc()->GetTransceivers();
  ASSERT_EQ(2u, transceivers.size());
  transceivers[0]->StopInternal();
  transceivers[1]->StopInternal();

  ASSERT_TRUE(caller->ExchangeOfferAnswerWith(callee.get()));

  EXPECT_TRUE(callee->pc()->GetTransceivers().empty());
}

// Test that removing streams from a subsequent answer causes the send streams
// on the callee to be removed when applied locally.
// See previous test for equivalent behavior with Unified Plan semantics.
TEST_F(PeerConnectionMediaTestPlanB, EmptyLocalAnswerRemovesSendStreams) {
  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnection();
  auto callee_audio_track = callee->AddAudioTrack("a");
  auto callee_video_track = callee->AddVideoTrack("v");

  ASSERT_TRUE(caller->ExchangeOfferAnswerWith(callee.get()));

  // Remove both tracks from callee.
  callee->pc()->RemoveTrackOrError(callee_audio_track);
  callee->pc()->RemoveTrackOrError(callee_video_track);

  ASSERT_TRUE(caller->ExchangeOfferAnswerWith(callee.get()));

  EXPECT_TRUE(callee->pc()->GetSenders().empty());
  EXPECT_EQ(2u, callee->pc()->GetReceivers().size());
}

// Test that a new stream in a subsequent offer causes a new receive stream to
// be created on the callee.
TEST_P(PeerConnectionMediaTest, NewStreamInRemoteOfferAddsRecvStreams) {
  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnection();

  ASSERT_TRUE(caller->ExchangeOfferAnswerWith(callee.get()));

  // Add second set of tracks to the caller.
  caller->AddAudioTrack("a2");
  caller->AddVideoTrack("v2");

  ASSERT_TRUE(caller->ExchangeOfferAnswerWith(callee.get()));

  if (IsUnifiedPlan()) {
    auto a1 = VoiceMediaReceiveChannel(callee->pc()->GetTransceivers()[0]);
    auto a2 = VoiceMediaReceiveChannel(callee->pc()->GetTransceivers()[2]);
    auto v1 = VideoMediaReceiveChannel(callee->pc()->GetTransceivers()[1]);
    auto v2 = VideoMediaReceiveChannel(callee->pc()->GetTransceivers()[3]);

    ASSERT_TRUE(a1);
    EXPECT_EQ(1u, a1->recv_streams().size());
    ASSERT_TRUE(a2);
    EXPECT_EQ(1u, a2->recv_streams().size());
    ASSERT_TRUE(v1);
    EXPECT_EQ(1u, v1->recv_streams().size());
    ASSERT_TRUE(v2);
    EXPECT_EQ(1u, v2->recv_streams().size());
  } else {
    EXPECT_EQ(4u, callee->pc()->GetReceivers().size());
  }
}

// Test that a new stream in a subsequent answer causes a new send stream to be
// created on the callee when added locally.
TEST_P(PeerConnectionMediaTest, NewStreamInLocalAnswerAddsSendStreams) {
  auto caller = CreatePeerConnection();
  auto callee = CreatePeerConnectionWithAudioVideo();

  RTCOfferAnswerOptions offer_options;
  offer_options.offer_to_receive_audio =
      RTCOfferAnswerOptions::kOfferToReceiveMediaTrue;
  offer_options.offer_to_receive_video =
      RTCOfferAnswerOptions::kOfferToReceiveMediaTrue;
  RTCOfferAnswerOptions answer_options;

  ASSERT_TRUE(caller->ExchangeOfferAnswerWith(callee.get(), offer_options,
                                              answer_options));

  // Add second set of tracks to the callee.
  callee->AddAudioTrack("a2");
  callee->AddVideoTrack("v2");

  ASSERT_TRUE(caller->ExchangeOfferAnswerWith(callee.get(), offer_options,
                                              answer_options));

  if (IsUnifiedPlan()) {
    auto callee_voice =
        VoiceMediaSendChannel(callee->pc()->GetTransceivers()[0]);
    ASSERT_TRUE(callee_voice);
    auto callee_video =
        VideoMediaSendChannel(callee->pc()->GetTransceivers()[1]);
    ASSERT_TRUE(callee_video);

    EXPECT_EQ(1u, callee_voice->send_streams().size());
    EXPECT_EQ(1u, callee_video->send_streams().size());
  } else {
    EXPECT_EQ(4u, callee->pc()->GetSenders().size());
  }
}

// A PeerConnection with no local streams and no explicit answer constraints
// should not reject any offered media sections.
TEST_P(PeerConnectionMediaTest,
       CreateAnswerWithNoStreamsAndDefaultOptionsDoesNotReject) {
  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnection();
  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));
  std::unique_ptr<SessionDescriptionInterface> answer = callee->CreateAnswer();

  const auto* audio_content = GetFirstAudioContent(answer->description());
  ASSERT_TRUE(audio_content);
  EXPECT_FALSE(audio_content->rejected);

  const auto* video_content = GetFirstVideoContent(answer->description());
  ASSERT_TRUE(video_content);
  EXPECT_FALSE(video_content->rejected);
}

// Test that raw packetization is not set in the offer by default.
TEST_P(PeerConnectionMediaTest, RawPacketizationNotSetInOffer) {
  std::vector<Codec> fake_codecs;
  fake_codecs.push_back(CreateVideoCodec(111, kVp8CodecName));
  fake_codecs.push_back(CreateVideoRtxCodec(112, 111));
  fake_codecs.push_back(CreateVideoCodec(113, kVp9CodecName));
  fake_codecs.push_back(CreateVideoCodec(114, kH264CodecName));
  fake_codecs.push_back(CreateVideoCodec(115, "HEVC"));
  auto caller_fake_engine = std::make_unique<FakeMediaEngine>();
  caller_fake_engine->SetVideoCodecs(fake_codecs);

  auto caller = CreatePeerConnectionWithVideo(std::move(caller_fake_engine));
  std::unique_ptr<SessionDescriptionInterface> offer =
      caller->CreateOfferAndSetAsLocal();
  auto* offer_description =
      GetFirstVideoContentDescription(offer->description());
  for (const auto& codec : offer_description->codecs()) {
    EXPECT_EQ(codec.packetization, std::nullopt);
  }
}

// Test that raw packetization is set in the offer and answer for all
// video payload when raw_packetization_for_video is true.
TEST_P(PeerConnectionMediaTest, RawPacketizationSetInOfferAndAnswer) {
  std::vector<Codec> fake_codecs;
  fake_codecs.push_back(CreateVideoCodec(111, kVp8CodecName));
  fake_codecs.push_back(CreateVideoRtxCodec(112, 111));
  fake_codecs.push_back(CreateVideoCodec(113, kVp9CodecName));
  fake_codecs.push_back(CreateVideoCodec(114, kH264CodecName));
  fake_codecs.push_back(CreateVideoCodec(115, "HEVC"));
  auto caller_fake_engine = std::make_unique<FakeMediaEngine>();
  caller_fake_engine->SetVideoCodecs(fake_codecs);
  auto callee_fake_engine = std::make_unique<FakeMediaEngine>();
  callee_fake_engine->SetVideoCodecs(fake_codecs);

  RTCOfferAnswerOptions options;
  options.raw_packetization_for_video = true;

  auto caller = CreatePeerConnectionWithVideo(std::move(caller_fake_engine));
  std::unique_ptr<SessionDescriptionInterface> offer =
      caller->CreateOfferAndSetAsLocal(options);
  auto* offer_description =
      GetFirstVideoContentDescription(offer->description());
  for (const auto& codec : offer_description->codecs()) {
    if (codec.IsMediaCodec()) {
      EXPECT_EQ(codec.packetization, kPacketizationParamRaw);
    }
  }

  auto callee = CreatePeerConnectionWithVideo(std::move(callee_fake_engine));
  ASSERT_TRUE(callee->SetRemoteDescription(std::move(offer)));
  std::unique_ptr<SessionDescriptionInterface> answer =
      callee->CreateAnswerAndSetAsLocal(options);
  auto* answer_description =
      GetFirstVideoContentDescription(answer->description());
  for (const auto& codec : answer_description->codecs()) {
    if (codec.IsMediaCodec()) {
      EXPECT_EQ(codec.packetization, kPacketizationParamRaw);
    }
  }

  ASSERT_TRUE(caller->SetRemoteDescription(std::move(answer)));
}

// Test that raw packetization is not set in the answer when
// raw_packetization_for_video is true if it was not set in the offer.
TEST_P(PeerConnectionMediaTest,
       RawPacketizationNotSetInAnswerWhenNotSetInOffer) {
  std::vector<Codec> fake_codecs;
  fake_codecs.push_back(CreateVideoCodec(111, kVp8CodecName));
  fake_codecs.push_back(CreateVideoRtxCodec(112, 111));
  fake_codecs.push_back(CreateVideoCodec(113, kVp9CodecName));
  fake_codecs.push_back(CreateVideoCodec(114, kH264CodecName));
  fake_codecs.push_back(CreateVideoCodec(115, "HEVC"));
  auto caller_fake_engine = std::make_unique<FakeMediaEngine>();
  caller_fake_engine->SetVideoCodecs(fake_codecs);
  auto callee_fake_engine = std::make_unique<FakeMediaEngine>();
  callee_fake_engine->SetVideoCodecs(fake_codecs);

  RTCOfferAnswerOptions caller_options;
  caller_options.raw_packetization_for_video = false;
  RTCOfferAnswerOptions callee_options;
  callee_options.raw_packetization_for_video = true;

  auto caller = CreatePeerConnectionWithVideo(std::move(caller_fake_engine));
  std::unique_ptr<SessionDescriptionInterface> offer =
      caller->CreateOfferAndSetAsLocal(caller_options);

  auto callee = CreatePeerConnectionWithVideo(std::move(callee_fake_engine));
  ASSERT_TRUE(callee->SetRemoteDescription(std::move(offer)));
  std::unique_ptr<SessionDescriptionInterface> answer =
      callee->CreateAnswerAndSetAsLocal(callee_options);

  auto* answer_description =
      GetFirstVideoContentDescription(answer->description());
  for (const auto& codec : answer_description->codecs()) {
    EXPECT_EQ(codec.packetization, std::nullopt);
  }

  ASSERT_TRUE(caller->SetRemoteDescription(std::move(answer)));
}

class PeerConnectionMediaOfferDirectionTest
    : public PeerConnectionMediaBaseTest,
      public ::testing::WithParamInterface<
          std::tuple<SdpSemantics,
                     std::tuple<bool, int, RtpTransceiverDirection>>> {
 protected:
  PeerConnectionMediaOfferDirectionTest()
      : PeerConnectionMediaBaseTest(std::get<0>(GetParam())) {
    auto param = std::get<1>(GetParam());
    send_media_ = std::get<0>(param);
    offer_to_receive_ = std::get<1>(param);
    expected_direction_ = std::get<2>(param);
  }

  bool send_media_;
  int offer_to_receive_;
  RtpTransceiverDirection expected_direction_;
};

// Tests that the correct direction is set on the media description according
// to the presence of a local media track and the offer_to_receive setting.
TEST_P(PeerConnectionMediaOfferDirectionTest, VerifyDirection) {
  auto caller = CreatePeerConnection();
  if (send_media_) {
    caller->AddAudioTrack("a");
  }

  RTCOfferAnswerOptions options;
  options.offer_to_receive_audio = offer_to_receive_;
  std::unique_ptr<SessionDescriptionInterface> offer =
      caller->CreateOffer(options);

  auto* content = GetFirstMediaContent(offer->description(), MediaType::AUDIO);
  if (expected_direction_ == RtpTransceiverDirection::kInactive) {
    EXPECT_FALSE(content);
  } else {
    EXPECT_EQ(expected_direction_, content->media_description()->direction());
  }
}

// Note that in these tests, MD_INACTIVE indicates that no media section is
// included in the offer, not that the media direction is inactive.
INSTANTIATE_TEST_SUITE_P(
    PeerConnectionMediaTest,
    PeerConnectionMediaOfferDirectionTest,
    Combine(
        Values(SdpSemantics::kPlanB_DEPRECATED, SdpSemantics::kUnifiedPlan),
        Values(std::make_tuple(false, -1, RtpTransceiverDirection::kInactive),
               std::make_tuple(false, 0, RtpTransceiverDirection::kInactive),
               std::make_tuple(false, 1, RtpTransceiverDirection::kRecvOnly),
               std::make_tuple(true, -1, RtpTransceiverDirection::kSendRecv),
               std::make_tuple(true, 0, RtpTransceiverDirection::kSendOnly),
               std::make_tuple(true, 1, RtpTransceiverDirection::kSendRecv))));

class PeerConnectionMediaAnswerDirectionTest
    : public PeerConnectionMediaBaseTest,
      public ::testing::WithParamInterface<
          std::tuple<SdpSemantics, RtpTransceiverDirection, bool, int>> {
 protected:
  PeerConnectionMediaAnswerDirectionTest()
      : PeerConnectionMediaBaseTest(std::get<0>(GetParam())) {
    offer_direction_ = std::get<1>(GetParam());
    send_media_ = std::get<2>(GetParam());
    offer_to_receive_ = std::get<3>(GetParam());
  }

  RtpTransceiverDirection offer_direction_;
  bool send_media_;
  int offer_to_receive_;
};

// Tests that the direction in an answer is correct according to direction sent
// in the offer, the presence of a local media track on the receive side and the
// offer_to_receive setting.
TEST_P(PeerConnectionMediaAnswerDirectionTest, VerifyDirection) {
  if (IsUnifiedPlan() &&
      offer_to_receive_ != RTCOfferAnswerOptions::kUndefined) {
    // offer_to_receive_ is not implemented when creating answers with Unified
    // Plan semantics specified.
    return;
  }

  auto caller = CreatePeerConnection();
  caller->AddAudioTrack("a");

  // Create the offer with an audio section and set its direction.
  std::unique_ptr<SessionDescriptionInterface> offer = caller->CreateOffer();
  GetFirstAudioContentDescription(offer->description())
      ->set_direction(offer_direction_);

  auto callee = CreatePeerConnection();
  if (send_media_) {
    callee->AddAudioTrack("a");
  }
  ASSERT_TRUE(callee->SetRemoteDescription(std::move(offer)));

  // Create the answer according to the test parameters.
  RTCOfferAnswerOptions options;
  options.offer_to_receive_audio = offer_to_receive_;
  std::unique_ptr<SessionDescriptionInterface> answer =
      callee->CreateAnswer(options);

  // The expected direction in the answer is the intersection of each side's
  // capability to send/recv media.
  // For the offerer, the direction is given in the offer (offer_direction_).
  // For the answerer, the direction has two components:
  // 1. Send if the answerer has a local track to send.
  // 2. Receive if the answerer has explicitly set the offer_to_receive to 1 or
  //    if it has been left as default.
  bool offer_send = RtpTransceiverDirectionHasSend(offer_direction_);
  bool offer_recv = RtpTransceiverDirectionHasRecv(offer_direction_);

  // The negotiated components determine the direction set in the answer.
  bool negotiate_send = (send_media_ && offer_recv);
  bool negotiate_recv = ((offer_to_receive_ != 0) && offer_send);

  auto expected_direction =
      RtpTransceiverDirectionFromSendRecv(negotiate_send, negotiate_recv);
  EXPECT_EQ(expected_direction,
            GetMediaContentDirection(answer.get(), MediaType::AUDIO));
}

// Tests that the media section is rejected if and only if the callee has no
// local media track and has set offer_to_receive to 0, no matter which
// direction the caller indicated in the offer.
TEST_P(PeerConnectionMediaAnswerDirectionTest, VerifyRejected) {
  if (IsUnifiedPlan() &&
      offer_to_receive_ != RTCOfferAnswerOptions::kUndefined) {
    // offer_to_receive_ is not implemented when creating answers with Unified
    // Plan semantics specified.
    return;
  }

  auto caller = CreatePeerConnection();
  caller->AddAudioTrack("a");

  // Create the offer with an audio section and set its direction.
  std::unique_ptr<SessionDescriptionInterface> offer = caller->CreateOffer();
  GetFirstAudioContentDescription(offer->description())
      ->set_direction(offer_direction_);

  auto callee = CreatePeerConnection();
  if (send_media_) {
    callee->AddAudioTrack("a");
  }
  ASSERT_TRUE(callee->SetRemoteDescription(std::move(offer)));

  // Create the answer according to the test parameters.
  RTCOfferAnswerOptions options;
  options.offer_to_receive_audio = offer_to_receive_;
  std::unique_ptr<SessionDescriptionInterface> answer =
      callee->CreateAnswer(options);

  // The media section is rejected if and only if offer_to_receive is explicitly
  // set to 0 and there is no media to send.
  auto* audio_content = GetFirstAudioContent(answer->description());
  ASSERT_TRUE(audio_content);
  EXPECT_EQ((offer_to_receive_ == 0 && !send_media_), audio_content->rejected);
}

INSTANTIATE_TEST_SUITE_P(PeerConnectionMediaTest,
                         PeerConnectionMediaAnswerDirectionTest,
                         Combine(Values(SdpSemantics::kPlanB_DEPRECATED,
                                        SdpSemantics::kUnifiedPlan),
                                 Values(RtpTransceiverDirection::kInactive,
                                        RtpTransceiverDirection::kSendOnly,
                                        RtpTransceiverDirection::kRecvOnly,
                                        RtpTransceiverDirection::kSendRecv),
                                 Bool(),
                                 Values(-1, 0, 1)));

TEST_P(PeerConnectionMediaTest, OfferHasDifferentDirectionForAudioVideo) {
  auto caller = CreatePeerConnection();
  caller->AddVideoTrack("v");

  RTCOfferAnswerOptions options;
  options.offer_to_receive_audio = 1;
  options.offer_to_receive_video = 0;
  std::unique_ptr<SessionDescriptionInterface> offer =
      caller->CreateOffer(options);

  EXPECT_EQ(RtpTransceiverDirection::kRecvOnly,
            GetMediaContentDirection(offer.get(), MediaType::AUDIO));
  EXPECT_EQ(RtpTransceiverDirection::kSendOnly,
            GetMediaContentDirection(offer.get(), MediaType::VIDEO));
}

TEST_P(PeerConnectionMediaTest, AnswerHasDifferentDirectionsForAudioVideo) {
  if (IsUnifiedPlan()) {
    // offer_to_receive_ is not implemented when creating answers with Unified
    // Plan semantics specified.
    return;
  }

  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnection();
  callee->AddVideoTrack("v");

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));

  RTCOfferAnswerOptions options;
  options.offer_to_receive_audio = 1;
  options.offer_to_receive_video = 0;
  std::unique_ptr<SessionDescriptionInterface> answer =
      callee->CreateAnswer(options);

  EXPECT_EQ(RtpTransceiverDirection::kRecvOnly,
            GetMediaContentDirection(answer.get(), MediaType::AUDIO));
  EXPECT_EQ(RtpTransceiverDirection::kSendOnly,
            GetMediaContentDirection(answer.get(), MediaType::VIDEO));
}

void AddComfortNoiseCodecsToSend(FakeMediaEngine* media_engine) {
  const Codec kComfortNoiseCodec8k =
      CreateAudioCodec(102, kCnCodecName, 8000, 1);
  const Codec kComfortNoiseCodec16k =
      CreateAudioCodec(103, kCnCodecName, 16000, 1);

  auto codecs = media_engine->voice().LegacySendCodecs();
  codecs.push_back(kComfortNoiseCodec8k);
  codecs.push_back(kComfortNoiseCodec16k);
  media_engine->SetAudioCodecs(codecs);
}

bool HasAnyComfortNoiseCodecs(const SessionDescription* desc) {
  const auto* audio_desc = GetFirstAudioContentDescription(desc);
  for (const auto& codec : audio_desc->codecs()) {
    if (codec.name == kCnCodecName) {
      return true;
    }
  }
  return false;
}

bool HasPayloadTypeConflict(const SessionDescription* desc) {
  std::set<int> payload_types;
  const auto* audio_desc = GetFirstAudioContentDescription(desc);
  if (audio_desc) {
    for (const auto& codec : audio_desc->codecs()) {
      if (payload_types.count(codec.id) > 0) {
        return true;
      }
      payload_types.insert(codec.id);
    }
  }
  const auto* video_desc = GetFirstVideoContentDescription(desc);
  if (video_desc) {
    for (const auto& codec : video_desc->codecs()) {
      if (payload_types.count(codec.id) > 0) {
        return true;
      }
      payload_types.insert(codec.id);
    }
  }
  return false;
}

TEST_P(PeerConnectionMediaTest,
       CreateOfferWithNoVoiceActivityDetectionIncludesNoComfortNoiseCodecs) {
  auto fake_engine = std::make_unique<FakeMediaEngine>();
  AddComfortNoiseCodecsToSend(fake_engine.get());
  auto caller = CreatePeerConnectionWithAudioVideo(std::move(fake_engine));

  RTCOfferAnswerOptions options;
  options.voice_activity_detection = false;
  std::unique_ptr<SessionDescriptionInterface> offer =
      caller->CreateOffer(options);

  EXPECT_FALSE(HasAnyComfortNoiseCodecs(offer->description()));
}

TEST_P(PeerConnectionMediaTest,
       CreateOfferWithVoiceActivityDetectionIncludesComfortNoiseCodecs) {
  auto fake_engine = std::make_unique<FakeMediaEngine>();
  AddComfortNoiseCodecsToSend(fake_engine.get());
  auto caller = CreatePeerConnectionWithAudioVideo(std::move(fake_engine));

  RTCOfferAnswerOptions options;
  options.voice_activity_detection = true;
  std::unique_ptr<SessionDescriptionInterface> offer =
      caller->CreateOffer(options);

  EXPECT_TRUE(HasAnyComfortNoiseCodecs(offer->description()));
}

TEST_P(PeerConnectionMediaTest,
       CreateAnswerWithVoiceActivityDetectionIncludesNoComfortNoiseCodecs) {
  auto caller = CreatePeerConnectionWithAudioVideo();

  auto callee_fake_engine = std::make_unique<FakeMediaEngine>();
  AddComfortNoiseCodecsToSend(callee_fake_engine.get());
  auto callee =
      CreatePeerConnectionWithAudioVideo(std::move(callee_fake_engine));

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));

  RTCOfferAnswerOptions options;
  options.voice_activity_detection = true;
  std::unique_ptr<SessionDescriptionInterface> answer =
      callee->CreateAnswer(options);

  EXPECT_FALSE(HasAnyComfortNoiseCodecs(answer->description()));
}

TEST_P(PeerConnectionMediaTest,
       CreateAnswerWithNoVoiceActivityDetectionIncludesNoComfortNoiseCodecs) {
  auto caller_fake_engine = std::make_unique<FakeMediaEngine>();
  AddComfortNoiseCodecsToSend(caller_fake_engine.get());
  auto caller =
      CreatePeerConnectionWithAudioVideo(std::move(caller_fake_engine));

  auto callee_fake_engine = std::make_unique<FakeMediaEngine>();
  AddComfortNoiseCodecsToSend(callee_fake_engine.get());
  auto callee =
      CreatePeerConnectionWithAudioVideo(std::move(callee_fake_engine));

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));

  RTCOfferAnswerOptions options;
  options.voice_activity_detection = false;
  std::unique_ptr<SessionDescriptionInterface> answer =
      callee->CreateAnswer(options);

  EXPECT_FALSE(HasAnyComfortNoiseCodecs(answer->description()));
}

// The following test group verifies that we reject answers with invalid media
// sections as per RFC 3264.

class PeerConnectionMediaInvalidMediaTest
    : public PeerConnectionMediaBaseTest,
      public ::testing::WithParamInterface<
          std::tuple<SdpSemantics,
                     std::tuple<std::string,
                                std::function<void(SessionDescription*)>,
                                std::string>>> {
 protected:
  PeerConnectionMediaInvalidMediaTest()
      : PeerConnectionMediaBaseTest(std::get<0>(GetParam())) {
    auto param = std::get<1>(GetParam());
    mutator_ = std::get<1>(param);
    expected_error_ = std::get<2>(param);
  }

  std::function<void(SessionDescription*)> mutator_;
  std::string expected_error_;
};

TEST_P(PeerConnectionMediaInvalidMediaTest, FailToSetRemoteAnswer) {
  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));

  std::unique_ptr<SessionDescriptionInterface> answer = callee->CreateAnswer();
  mutator_(answer->description());

  std::string error;
  ASSERT_FALSE(caller->SetRemoteDescription(std::move(answer), &error));
  EXPECT_EQ("Failed to set remote answer sdp: " + expected_error_, error);
}

TEST_P(PeerConnectionMediaInvalidMediaTest, FailToSetLocalAnswer) {
  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));

  std::unique_ptr<SessionDescriptionInterface> answer = callee->CreateAnswer();
  mutator_(answer->description());

  std::string error;
  ASSERT_FALSE(callee->SetLocalDescription(std::move(answer), &error));
  EXPECT_EQ("Failed to set local answer sdp: " + expected_error_, error);
}

void RemoveVideoContentAndUnbundle(SessionDescription* desc) {
  // Removing BUNDLE is easier than removing the content in there.
  desc->RemoveGroupByName("BUNDLE");
  std::string content_name = GetFirstVideoContent(desc)->mid();
  desc->RemoveContentByName(content_name);
  desc->RemoveTransportInfoByName(content_name);
}

void RenameVideoContentAndUnbundle(SessionDescription* desc) {
  // Removing BUNDLE is easier than renaming the content in there.
  desc->RemoveGroupByName("BUNDLE");
  auto* video_content = GetFirstVideoContent(desc);
  auto* transport_info = desc->GetTransportInfoByName(video_content->mid());
  video_content->set_mid("video_renamed");
  transport_info->content_name = video_content->mid();
}

void ReverseMediaContent(SessionDescription* desc) {
  absl::c_reverse(desc->contents());
  absl::c_reverse(desc->transport_infos());
}

void ChangeMediaTypeAudioToVideo(SessionDescription* desc) {
  std::string audio_mid = GetFirstAudioContent(desc)->mid();
  desc->RemoveContentByName(audio_mid);
  auto* video_content = GetFirstVideoContent(desc);
  desc->AddContent(audio_mid, video_content->type,
                   video_content->media_description()->Clone());
}

constexpr char kMLinesOutOfOrder[] =
    "The order of m-lines in answer doesn't match order in offer. Rejecting "
    "answer.";

INSTANTIATE_TEST_SUITE_P(
    PeerConnectionMediaTest,
    PeerConnectionMediaInvalidMediaTest,
    Combine(Values(SdpSemantics::kPlanB_DEPRECATED, SdpSemantics::kUnifiedPlan),
            Values(std::make_tuple("remove video",
                                   RemoveVideoContentAndUnbundle,
                                   kMLinesOutOfOrder),
                   std::make_tuple("rename video",
                                   RenameVideoContentAndUnbundle,
                                   kMLinesOutOfOrder),
                   std::make_tuple("reverse media sections",
                                   ReverseMediaContent,
                                   kMLinesOutOfOrder),
                   std::make_tuple("change audio type to video type",
                                   ChangeMediaTypeAudioToVideo,
                                   kMLinesOutOfOrder))));

// Tests that if the underlying video encoder fails to be initialized (signaled
// by failing to set send codecs), the PeerConnection signals the error to the
// client.
TEST_F(PeerConnectionMediaTestUnifiedPlan,
       MediaEngineErrorPropagatedToClients) {
  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));

  auto video_channel =
      VideoMediaSendChannel(caller->pc()->GetTransceivers()[1]);
  video_channel->set_fail_set_send_codecs(true);

  std::string error;
  ASSERT_FALSE(caller->SetRemoteDescription(callee->CreateAnswerAndSetAsLocal(),
                                            &error));
  EXPECT_EQ(std::string("Failed to set remote answer sdp: Failed to set remote "
                        "video description "
                        "send parameters for m-section with mid='") +
                (IsUnifiedPlan() ? "1" : "video") + "'.",
            error);
}

// Tests that if the underlying video encoder fails once then subsequent
// attempts at setting the local/remote description will also fail, even if
// SetSendCodecs no longer fails.
TEST_F(PeerConnectionMediaTestUnifiedPlan,
       FailToApplyDescriptionIfVideoEncoderHasEverFailed) {
  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));

  auto video_channel =
      VideoMediaSendChannel(caller->pc()->GetTransceivers()[1]);
  video_channel->set_fail_set_send_codecs(true);

  EXPECT_FALSE(
      caller->SetRemoteDescription(callee->CreateAnswerAndSetAsLocal()));

  video_channel->set_fail_set_send_codecs(false);

  EXPECT_FALSE(caller->SetRemoteDescription(callee->CreateAnswer()));
  EXPECT_FALSE(caller->SetLocalDescription(caller->CreateOffer()));
}

void RenameContent(SessionDescription* desc,
                   MediaType media_type,
                   const std::string& new_name) {
  auto* content = GetFirstMediaContent(desc, media_type);
  RTC_DCHECK(content);
  std::string old_name(content->mid());
  content->set_mid(new_name);
  auto* transport = desc->GetTransportInfoByName(old_name);
  RTC_DCHECK(transport);
  transport->content_name = new_name;

  // Rename the content name in the BUNDLE group.
  ContentGroup new_bundle_group = *desc->GetGroupByName(GROUP_TYPE_BUNDLE);
  new_bundle_group.RemoveContentName(old_name);
  new_bundle_group.AddContentName(new_name);
  desc->RemoveGroupByName(GROUP_TYPE_BUNDLE);
  desc->AddGroup(new_bundle_group);
}

// Tests that an answer responds with the same MIDs as the offer.
TEST_P(PeerConnectionMediaTest, AnswerHasSameMidsAsOffer) {
  const std::string kAudioMid = "notdefault1";
  const std::string kVideoMid = "notdefault2";

  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  std::unique_ptr<SessionDescriptionInterface> offer = caller->CreateOffer();
  RenameContent(offer->description(), MediaType::AUDIO, kAudioMid);
  RenameContent(offer->description(), MediaType::VIDEO, kVideoMid);
  ASSERT_TRUE(callee->SetRemoteDescription(std::move(offer)));

  std::unique_ptr<SessionDescriptionInterface> answer = callee->CreateAnswer();
  EXPECT_EQ(kAudioMid, GetFirstAudioContent(answer->description())->mid());
  EXPECT_EQ(kVideoMid, GetFirstVideoContent(answer->description())->mid());
}

// Test that if the callee creates a re-offer, the MIDs are the same as the
// original offer.
TEST_P(PeerConnectionMediaTest, ReOfferHasSameMidsAsFirstOffer) {
  const std::string kAudioMid = "notdefault1";
  const std::string kVideoMid = "notdefault2";

  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  std::unique_ptr<SessionDescriptionInterface> offer = caller->CreateOffer();
  RenameContent(offer->description(), MediaType::AUDIO, kAudioMid);
  RenameContent(offer->description(), MediaType::VIDEO, kVideoMid);
  ASSERT_TRUE(callee->SetRemoteDescription(std::move(offer)));
  ASSERT_TRUE(callee->SetLocalDescription(callee->CreateAnswer()));

  auto reoffer = callee->CreateOffer();
  EXPECT_EQ(kAudioMid, GetFirstAudioContent(reoffer->description())->mid());
  EXPECT_EQ(kVideoMid, GetFirstVideoContent(reoffer->description())->mid());
}

// Test that SetRemoteDescription returns an error if there are two m= sections
// with the same MID value.
TEST_P(PeerConnectionMediaTest, SetRemoteDescriptionFailsWithDuplicateMids) {
  auto caller = CreatePeerConnectionWithAudioVideo();
  auto callee = CreatePeerConnectionWithAudioVideo();

  std::unique_ptr<SessionDescriptionInterface> offer = caller->CreateOffer();
  RenameContent(offer->description(), MediaType::AUDIO, "same");
  RenameContent(offer->description(), MediaType::VIDEO, "same");

  std::string error;
  EXPECT_FALSE(callee->SetRemoteDescription(std::move(offer), &error));
  EXPECT_EQ(error,
            "Failed to set remote offer sdp: Duplicate a=mid value 'same'.");
}

// Test that if a RED codec refers to another codec in its fmtp line, but that
// codec's payload type was reassigned for some reason (either the remote
// endpoint selected a different payload type or there was a conflict), the RED
// fmtp line is modified to refer to the correct payload type.
TEST_P(PeerConnectionMediaTest, RedFmtpPayloadTypeReassigned) {
  std::vector<Codec> caller_fake_codecs;
  caller_fake_codecs.push_back(
      CreateAudioCodec(100, "foo", kDefaultAudioClockRateHz, 1));
  auto caller_fake_engine = std::make_unique<FakeMediaEngine>();
  caller_fake_engine->SetAudioCodecs(caller_fake_codecs);
  auto caller = CreatePeerConnectionWithAudio(std::move(caller_fake_engine));

  std::vector<Codec> callee_fake_codecs;
  callee_fake_codecs.push_back(
      CreateAudioCodec(120, "foo", kDefaultAudioClockRateHz, 1));
  callee_fake_codecs.push_back(
      CreateAudioCodec(121, kRedCodecName, kDefaultAudioClockRateHz, 1));
  callee_fake_codecs.back().SetParam(kCodecParamNotInNameValueFormat,
                                     "120/120");
  auto callee_fake_engine = std::make_unique<FakeMediaEngine>();
  callee_fake_engine->SetAudioCodecs(callee_fake_codecs);
  auto callee = CreatePeerConnectionWithAudio(std::move(callee_fake_engine));

  // Offer from the caller establishes 100 as the "foo" payload type.
  std::unique_ptr<SessionDescriptionInterface> offer =
      caller->CreateOfferAndSetAsLocal();
  callee->SetRemoteDescription(std::move(offer));
  std::unique_ptr<SessionDescriptionInterface> answer =
      callee->CreateAnswerAndSetAsLocal();
  auto answer_description =
      GetFirstAudioContentDescription(answer->description());
  ASSERT_EQ(1u, answer_description->codecs().size());

  // Offer from the callee should respect the established payload type, and
  // attempt to add RED, which should refer to the correct payload type.
  offer = callee->CreateOfferAndSetAsLocal();
  auto* offer_description =
      GetFirstAudioContentDescription(offer->description());
  ASSERT_EQ(2u, offer_description->codecs().size());
  for (const auto& codec : offer_description->codecs()) {
    if (codec.name == "foo") {
      ASSERT_EQ(100, codec.id);
    } else if (codec.name == kRedCodecName) {
      std::string fmtp;
      ASSERT_TRUE(codec.GetParam("", &fmtp));
      EXPECT_EQ("100/100", fmtp);
    }
  }
}

// Test that RED without fmtp does match RED without fmtp.
TEST_P(PeerConnectionMediaTest, RedFmtpPayloadTypeNoFmtpMatchNoFmtp) {
  std::vector<Codec> caller_fake_codecs;
  caller_fake_codecs.push_back(
      CreateAudioCodec(100, "foo", kDefaultAudioClockRateHz, 1));
  caller_fake_codecs.push_back(
      CreateAudioCodec(101, kRedCodecName, kDefaultAudioClockRateHz, 1));
  auto caller_fake_engine = std::make_unique<FakeMediaEngine>();
  caller_fake_engine->SetAudioCodecs(caller_fake_codecs);
  auto caller = CreatePeerConnectionWithAudio(std::move(caller_fake_engine));

  std::vector<Codec> callee_fake_codecs;
  callee_fake_codecs.push_back(
      CreateAudioCodec(120, "foo", kDefaultAudioClockRateHz, 1));
  callee_fake_codecs.push_back(
      CreateAudioCodec(121, kRedCodecName, kDefaultAudioClockRateHz, 1));
  auto callee_fake_engine = std::make_unique<FakeMediaEngine>();
  callee_fake_engine->SetAudioCodecs(callee_fake_codecs);
  auto callee = CreatePeerConnectionWithAudio(std::move(callee_fake_engine));

  // Offer from the caller establishes 100 as the "foo" payload type.
  // Red (without fmtp) is negotiated.
  std::unique_ptr<SessionDescriptionInterface> offer =
      caller->CreateOfferAndSetAsLocal();
  callee->SetRemoteDescription(std::move(offer));
  std::unique_ptr<SessionDescriptionInterface> answer =
      callee->CreateAnswerAndSetAsLocal();
  auto answer_description =
      GetFirstAudioContentDescription(answer->description());
  ASSERT_EQ(2u, answer_description->codecs().size());

  // Offer from the callee should respect the established payload type, and
  // attempt to add RED.
  offer = callee->CreateOfferAndSetAsLocal();
  auto* offer_description =
      GetFirstAudioContentDescription(offer->description());
  ASSERT_EQ(2u, offer_description->codecs().size());
  for (const auto& codec : offer_description->codecs()) {
    if (codec.name == "foo") {
      ASSERT_EQ(100, codec.id);
    } else if (codec.name == kRedCodecName) {
      ASSERT_EQ(101, codec.id);
    }
  }
}

// Test that RED without fmtp does not match RED with fmtp.
TEST_P(PeerConnectionMediaTest, RedFmtpPayloadTypeNoFmtpNoMatchFmtp) {
  std::vector<Codec> caller_fake_codecs;
  caller_fake_codecs.push_back(
      CreateAudioCodec(100, "foo", kDefaultAudioClockRateHz, 1));
  caller_fake_codecs.push_back(
      CreateAudioCodec(101, kRedCodecName, kDefaultAudioClockRateHz, 1));
  auto caller_fake_engine = std::make_unique<FakeMediaEngine>();
  caller_fake_engine->SetAudioCodecs(caller_fake_codecs);
  auto caller = CreatePeerConnectionWithAudio(std::move(caller_fake_engine));

  std::vector<Codec> callee_fake_codecs;
  callee_fake_codecs.push_back(
      CreateAudioCodec(120, "foo", kDefaultAudioClockRateHz, 1));
  callee_fake_codecs.push_back(
      CreateAudioCodec(121, kRedCodecName, kDefaultAudioClockRateHz, 1));
  callee_fake_codecs.back().SetParam(kCodecParamNotInNameValueFormat,
                                     "120/120");
  auto callee_fake_engine = std::make_unique<FakeMediaEngine>();
  callee_fake_engine->SetAudioCodecs(callee_fake_codecs);
  auto callee = CreatePeerConnectionWithAudio(std::move(callee_fake_engine));

  // Offer from the caller establishes 100 as the "foo" payload type.
  // It should not negotiate RED.
  std::unique_ptr<SessionDescriptionInterface> offer =
      caller->CreateOfferAndSetAsLocal();
  callee->SetRemoteDescription(std::move(offer));
  std::unique_ptr<SessionDescriptionInterface> answer =
      callee->CreateAnswerAndSetAsLocal();
  auto answer_description =
      GetFirstAudioContentDescription(answer->description());
  ASSERT_EQ(1u, answer_description->codecs().size());

  // Offer from the callee should respect the established payload type, and
  // attempt to add RED, which should refer to the correct payload type.
  offer = callee->CreateOfferAndSetAsLocal();
  auto* offer_description =
      GetFirstAudioContentDescription(offer->description());
  ASSERT_EQ(2u, offer_description->codecs().size());
  for (const auto& codec : offer_description->codecs()) {
    if (codec.name == "foo") {
      ASSERT_EQ(100, codec.id);
    } else if (codec.name == kRedCodecName) {
      std::string fmtp;
      ASSERT_TRUE(codec.GetParam(kCodecParamNotInNameValueFormat, &fmtp));
      EXPECT_EQ("100/100", fmtp);
    }
  }
}

// Test that RED with fmtp must match base codecs.
TEST_P(PeerConnectionMediaTest, RedFmtpPayloadTypeMustMatchBaseCodecs) {
  std::vector<Codec> caller_fake_codecs;
  caller_fake_codecs.push_back(
      CreateAudioCodec(100, "foo", kDefaultAudioClockRateHz, 1));
  caller_fake_codecs.push_back(
      CreateAudioCodec(101, kRedCodecName, kDefaultAudioClockRateHz, 1));
  caller_fake_codecs.back().SetParam(kCodecParamNotInNameValueFormat,
                                     "100/100");
  auto caller_fake_engine = std::make_unique<FakeMediaEngine>();
  caller_fake_engine->SetAudioCodecs(caller_fake_codecs);
  auto caller = CreatePeerConnectionWithAudio(std::move(caller_fake_engine));

  std::vector<Codec> callee_fake_codecs;
  callee_fake_codecs.push_back(
      CreateAudioCodec(120, "foo", kDefaultAudioClockRateHz, 1));
  callee_fake_codecs.push_back(
      CreateAudioCodec(121, kRedCodecName, kDefaultAudioClockRateHz, 1));
  callee_fake_codecs.back().SetParam(kCodecParamNotInNameValueFormat,
                                     "122/122");
  callee_fake_codecs.push_back(
      CreateAudioCodec(122, "bar", kDefaultAudioClockRateHz, 1));
  auto callee_fake_engine = std::make_unique<FakeMediaEngine>();
  callee_fake_engine->SetAudioCodecs(callee_fake_codecs);
  auto callee = CreatePeerConnectionWithAudio(std::move(callee_fake_engine));

  // Offer from the caller establishes 100 as the "foo" payload type.
  // It should not negotiate RED since RED is associated with foo, not bar.
  std::unique_ptr<SessionDescriptionInterface> offer =
      caller->CreateOfferAndSetAsLocal();
  callee->SetRemoteDescription(std::move(offer));
  std::unique_ptr<SessionDescriptionInterface> answer =
      callee->CreateAnswerAndSetAsLocal();
  auto answer_description =
      GetFirstAudioContentDescription(answer->description());
  ASSERT_EQ(1u, answer_description->codecs().size());
}

// Test behaviour when the RED fmtp attempts to specify different codecs
// which is not supported.
TEST_P(PeerConnectionMediaTest, RedFmtpPayloadMixed) {
  std::vector<Codec> caller_fake_codecs;
  caller_fake_codecs.push_back(
      CreateAudioCodec(100, "foo", kDefaultAudioClockRateHz, 1));
  caller_fake_codecs.push_back(
      CreateAudioCodec(102, "bar", kDefaultAudioClockRateHz, 1));
  caller_fake_codecs.push_back(
      CreateAudioCodec(101, kRedCodecName, kDefaultAudioClockRateHz, 1));
  caller_fake_codecs.back().SetParam(kCodecParamNotInNameValueFormat,
                                     "100/102");
  auto caller_fake_engine = std::make_unique<FakeMediaEngine>();
  caller_fake_engine->SetAudioCodecs(caller_fake_codecs);
  auto caller = CreatePeerConnectionWithAudio(std::move(caller_fake_engine));

  std::vector<Codec> callee_fake_codecs;
  callee_fake_codecs.push_back(
      CreateAudioCodec(120, "foo", kDefaultAudioClockRateHz, 1));
  callee_fake_codecs.push_back(
      CreateAudioCodec(121, kRedCodecName, kDefaultAudioClockRateHz, 1));
  callee_fake_codecs.back().SetParam(kCodecParamNotInNameValueFormat,
                                     "120/120");
  auto callee_fake_engine = std::make_unique<FakeMediaEngine>();
  callee_fake_engine->SetAudioCodecs(callee_fake_codecs);
  auto callee = CreatePeerConnectionWithAudio(std::move(callee_fake_engine));

  // Offer from the caller establishes 100 as the "foo" payload type.
  std::unique_ptr<SessionDescriptionInterface> offer =
      caller->CreateOfferAndSetAsLocal();
  callee->SetRemoteDescription(std::move(offer));
  std::unique_ptr<SessionDescriptionInterface> answer =
      callee->CreateAnswerAndSetAsLocal();
  auto answer_description =
      GetFirstAudioContentDescription(answer->description());
  // RED is not negotiated.
  ASSERT_EQ(1u, answer_description->codecs().size());
}

// Test behaviour when the RED fmtp attempts to negotiate different levels of
// redundancy.
TEST_P(PeerConnectionMediaTest, RedFmtpPayloadDifferentRedundancy) {
  std::vector<Codec> caller_fake_codecs;
  caller_fake_codecs.push_back(
      CreateAudioCodec(100, "foo", kDefaultAudioClockRateHz, 1));
  caller_fake_codecs.push_back(
      CreateAudioCodec(101, kRedCodecName, kDefaultAudioClockRateHz, 1));
  caller_fake_codecs.back().SetParam(kCodecParamNotInNameValueFormat,
                                     "100/100");
  auto caller_fake_engine = std::make_unique<FakeMediaEngine>();
  caller_fake_engine->SetAudioCodecs(caller_fake_codecs);
  auto caller = CreatePeerConnectionWithAudio(std::move(caller_fake_engine));

  std::vector<Codec> callee_fake_codecs;
  callee_fake_codecs.push_back(
      CreateAudioCodec(120, "foo", kDefaultAudioClockRateHz, 1));
  callee_fake_codecs.push_back(
      CreateAudioCodec(121, kRedCodecName, kDefaultAudioClockRateHz, 1));
  callee_fake_codecs.back().SetParam(kCodecParamNotInNameValueFormat,
                                     "120/120/120");
  auto callee_fake_engine = std::make_unique<FakeMediaEngine>();
  callee_fake_engine->SetAudioCodecs(callee_fake_codecs);
  auto callee = CreatePeerConnectionWithAudio(std::move(callee_fake_engine));

  // Offer from the caller establishes 100 as the "foo" payload type.
  std::unique_ptr<SessionDescriptionInterface> offer =
      caller->CreateOfferAndSetAsLocal();
  callee->SetRemoteDescription(std::move(offer));
  std::unique_ptr<SessionDescriptionInterface> answer =
      callee->CreateAnswerAndSetAsLocal();
  auto answer_description =
      GetFirstAudioContentDescription(answer->description());
  // RED is negotiated.
  ASSERT_EQ(2u, answer_description->codecs().size());

  // Offer from the callee should respect the established payload type, and
  // attempt to add RED, which should refer to the correct payload type.
  offer = callee->CreateOfferAndSetAsLocal();
  auto* offer_description =
      GetFirstAudioContentDescription(offer->description());
  ASSERT_EQ(2u, offer_description->codecs().size());
  for (const auto& codec : offer_description->codecs()) {
    if (codec.name == "foo") {
      ASSERT_EQ(100, codec.id);
    } else if (codec.name == kRedCodecName) {
      std::string fmtp;
      ASSERT_TRUE(codec.GetParam(kCodecParamNotInNameValueFormat, &fmtp));
      EXPECT_EQ("100/100", fmtp);
    }
  }
}

template <typename C>
bool CompareCodecs(const std::vector<RtpCodecCapability>& capabilities,
                   const std::vector<C>& codecs) {
  bool capability_has_rtx =
      absl::c_any_of(capabilities, [](const RtpCodecCapability& codec) {
        return codec.name == kRtxCodecName;
      });
  bool codecs_has_rtx = absl::c_any_of(
      codecs, [](const C& codec) { return codec.name == kRtxCodecName; });

  std::vector<C> codecs_no_rtx;
  absl::c_copy_if(codecs, std::back_inserter(codecs_no_rtx),
                  [](const C& codec) { return codec.name != kRtxCodecName; });

  std::vector<RtpCodecCapability> capabilities_no_rtx;
  absl::c_copy_if(capabilities, std::back_inserter(capabilities_no_rtx),
                  [](const RtpCodecCapability& codec) {
                    return codec.name != kRtxCodecName;
                  });

  return capability_has_rtx == codecs_has_rtx &&
         absl::c_equal(
             capabilities_no_rtx, codecs_no_rtx,
             [](const RtpCodecCapability& capability, const C& codec) {
               return codec.MatchesRtpCodec(capability);
             });
}

TEST_F(PeerConnectionMediaTestUnifiedPlan,
       SetCodecPreferencesAudioMissingRecvCodec) {
  auto fake_engine = std::make_unique<FakeMediaEngine>();
  auto send_codecs = fake_engine->voice().LegacySendCodecs();
  send_codecs.push_back(CreateAudioCodec(send_codecs.back().id + 1,
                                         "send_only_codec",
                                         kDefaultAudioClockRateHz, 1));
  fake_engine->SetAudioSendCodecs(send_codecs);

  auto caller = CreatePeerConnectionWithAudio(std::move(fake_engine));

  auto transceiver = caller->pc()->GetTransceivers().front();
  auto capabilities =
      caller->pc_factory()->GetRtpSenderCapabilities(MediaType::AUDIO);

  std::vector<RtpCodecCapability> codecs;
  absl::c_copy_if(capabilities.codecs, std::back_inserter(codecs),
                  [](const RtpCodecCapability& codec) {
                    return codec.name.find("_only_") != std::string::npos;
                  });

  // This is OK, however because the codec is send-only and the transciever is
  // not send-only, it would get filtered out during negotiation.
  EXPECT_THAT(transceiver->SetCodecPreferences(codecs), IsRtcOk());
}

TEST_F(PeerConnectionMediaTestUnifiedPlan,
       SetCodecPreferencesAudioRejectsVideoCodec) {
  auto caller = CreatePeerConnectionWithAudio();

  auto transceiver = caller->pc()->GetTransceivers().front();
  auto video_codecs =
      caller->pc_factory()->GetRtpSenderCapabilities(MediaType::VIDEO).codecs;
  auto codecs =
      caller->pc_factory()->GetRtpSenderCapabilities(MediaType::AUDIO).codecs;
  codecs.insert(codecs.end(), video_codecs.begin(), video_codecs.end());
  auto result = transceiver->SetCodecPreferences(codecs);
  EXPECT_EQ(RTCErrorType::INVALID_MODIFICATION, result.type());
}

TEST_F(PeerConnectionMediaTestUnifiedPlan,
       SetCodecPreferencesAudioRejectsOnlyRtxRedFec) {
  auto fake_engine = std::make_unique<FakeMediaEngine>();
  auto audio_codecs = fake_engine->voice().LegacySendCodecs();
  audio_codecs.push_back(
      CreateAudioRtxCodec(audio_codecs.back().id + 1, audio_codecs.back().id));
  audio_codecs.push_back(CreateAudioCodec(
      audio_codecs.back().id + 1, kRedCodecName, kDefaultAudioClockRateHz, 1));
  audio_codecs.push_back(CreateAudioCodec(audio_codecs.back().id + 1,
                                          kUlpfecCodecName,
                                          kDefaultAudioClockRateHz, 1));
  fake_engine->SetAudioCodecs(audio_codecs);

  auto caller = CreatePeerConnectionWithAudio(std::move(fake_engine));

  auto transceiver =
      RtpTransceiverInternal(caller->pc()->GetTransceivers().front());
  auto codecs =
      caller->pc_factory()->GetRtpSenderCapabilities(MediaType::AUDIO).codecs;
  auto codecs_only_rtx_red_fec = codecs;
  std::erase_if(codecs_only_rtx_red_fec, [](const RtpCodecCapability& codec) {
    return !(codec.name == kRtxCodecName || codec.name == kRedCodecName ||
             codec.name == kUlpfecCodecName);
  });
  ASSERT_THAT(codecs_only_rtx_red_fec.size(), Gt(0));
  auto result = transceiver->SetCodecPreferences(codecs_only_rtx_red_fec);
  EXPECT_EQ(RTCErrorType::INVALID_MODIFICATION, result.type());
}

TEST_F(PeerConnectionMediaTestUnifiedPlan, SetCodecPreferencesAllAudioCodecs) {
  auto caller = CreatePeerConnectionWithAudio();

  auto sender_audio_codecs =
      caller->pc_factory()->GetRtpSenderCapabilities(MediaType::AUDIO).codecs;

  auto audio_transceiver = caller->pc()->GetTransceivers().front();

  // Normal case, set all capabilities as preferences
  EXPECT_TRUE(audio_transceiver->SetCodecPreferences(sender_audio_codecs).ok());
  std::unique_ptr<SessionDescriptionInterface> offer = caller->CreateOffer();
  auto codecs =
      offer->description()->contents()[0].media_description()->codecs();
  EXPECT_TRUE(CompareCodecs(sender_audio_codecs, codecs));
}

TEST_F(PeerConnectionMediaTestUnifiedPlan,
       SetCodecPreferencesResetAudioCodecs) {
  auto caller = CreatePeerConnectionWithAudio();

  auto sender_audio_codecs =
      caller->pc_factory()->GetRtpSenderCapabilities(MediaType::AUDIO).codecs;
  std::vector<RtpCodecCapability> empty_codecs = {};

  auto audio_transceiver = caller->pc()->GetTransceivers().front();

  // Normal case, reset codec preferences
  EXPECT_TRUE(audio_transceiver->SetCodecPreferences(empty_codecs).ok());
  std::unique_ptr<SessionDescriptionInterface> offer = caller->CreateOffer();
  auto codecs =
      offer->description()->contents()[0].media_description()->codecs();
  EXPECT_TRUE(CompareCodecs(sender_audio_codecs, codecs));
}

TEST_F(PeerConnectionMediaTestUnifiedPlan,
       SetCodecPreferencesAudioRemovingOpusDropsRed) {
  auto fake_engine = std::make_unique<FakeMediaEngine>();
  std::vector<Codec> audio_codecs = {
      CreateAudioCodec(111, kOpusCodecName, 48000, 2),
      CreateAudioCodec(63, kRedCodecName, 48000, 2),
      CreateAudioCodec(0, kPcmuCodecName, 8000, 1),
      CreateAudioCodec(8, kPcmaCodecName, 8000, 1),
  };
  audio_codecs[1].params[std::string(kCodecParamNotInNameValueFormat)] =
      "111/111";
  fake_engine->SetAudioCodecs(audio_codecs);

  auto caller = CreatePeerConnectionWithAudio(std::move(fake_engine));

  auto transceiver = caller->pc()->GetTransceivers().front();
  auto caps =
      caller->pc_factory()->GetRtpSenderCapabilities(MediaType::AUDIO).codecs;

  // Prefer RED, filter opus.
  std::vector<RtpCodecCapability> preferred;
  for (const auto& codec : caps) {
    if (codec.name == kRedCodecName) {
      preferred.push_back(codec);
    }
  }
  for (const auto& codec : caps) {
    if (codec.name != kRedCodecName && codec.name != kOpusCodecName) {
      preferred.push_back(codec);
    }
  }
  EXPECT_THAT(transceiver->SetCodecPreferences(preferred), IsRtcOk());

  auto offer = caller->CreateOffer();
  const std::vector<Codec>& offered_codecs =
      offer->description()->contents()[0].media_description()->codecs();

  auto has_codec = [&](absl::string_view name) {
    return absl::c_any_of(
        offered_codecs, [&](const Codec& codec) { return codec.name == name; });
  };

  EXPECT_TRUE(has_codec(kPcmuCodecName));
  EXPECT_TRUE(has_codec(kPcmaCodecName));
  // RED was not removed via sCP but depends on opus which was removed
  // so neither is part of the generated offer.
  EXPECT_FALSE(has_codec(kOpusCodecName));
  EXPECT_FALSE(has_codec(kRedCodecName));
}

TEST_F(PeerConnectionMediaTestUnifiedPlan,
       SetCodecPreferencesAudioRemovingOpusDropsRedInAnswer) {
  auto make_engine = [] {
    auto fake_engine = std::make_unique<FakeMediaEngine>();
    std::vector<Codec> audio_codecs = {
        CreateAudioCodec(111, kOpusCodecName, 48000, 2),
        CreateAudioCodec(63, kRedCodecName, 48000, 2),
        CreateAudioCodec(0, kPcmuCodecName, 8000, 1),
        CreateAudioCodec(8, kPcmaCodecName, 8000, 1),
    };
    audio_codecs[1].params[std::string(kCodecParamNotInNameValueFormat)] =
        "111/111";
    fake_engine->SetAudioCodecs(audio_codecs);
    return fake_engine;
  };
  auto caller = CreatePeerConnectionWithAudio(make_engine());
  auto callee = CreatePeerConnectionWithAudio(make_engine());

  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));

  auto transceiver = callee->pc()->GetTransceivers().front();
  auto caps =
      callee->pc_factory()->GetRtpSenderCapabilities(MediaType::AUDIO).codecs;

  // Prefer RED, filter opus.
  std::vector<RtpCodecCapability> preferred;
  for (const auto& codec : caps) {
    if (codec.name == kRedCodecName) {
      preferred.push_back(codec);
    }
  }
  for (const auto& codec : caps) {
    if (codec.name != kRedCodecName && codec.name != kOpusCodecName) {
      preferred.push_back(codec);
    }
  }
  EXPECT_THAT(transceiver->SetCodecPreferences(preferred), IsRtcOk());

  auto answer = callee->CreateAnswer();
  const std::vector<Codec>& answer_codecs =
      answer->description()->contents()[0].media_description()->codecs();

  auto has_codec = [&](absl::string_view name) {
    return absl::c_any_of(
        answer_codecs, [&](const Codec& codec) { return codec.name == name; });
  };

  EXPECT_TRUE(has_codec(kPcmuCodecName));
  EXPECT_TRUE(has_codec(kPcmaCodecName));
  // RED was not removed via sCP but depends on opus which was removed
  // so neither is part of the generated answer.
  EXPECT_FALSE(has_codec(kOpusCodecName));
  EXPECT_FALSE(has_codec(kRedCodecName));
}

TEST_F(PeerConnectionMediaTestUnifiedPlan,
       SetCodecPreferencesVideoRejectsAudioCodec) {
  auto caller = CreatePeerConnectionWithVideo();

  auto transceiver = caller->pc()->GetTransceivers().front();
  auto audio_codecs =
      caller->pc_factory()->GetRtpSenderCapabilities(MediaType::AUDIO).codecs;
  auto codecs =
      caller->pc_factory()->GetRtpSenderCapabilities(MediaType::VIDEO).codecs;
  codecs.insert(codecs.end(), audio_codecs.begin(), audio_codecs.end());
  auto result = transceiver->SetCodecPreferences(codecs);
  EXPECT_EQ(RTCErrorType::INVALID_MODIFICATION, result.type());
}

TEST_F(PeerConnectionMediaTestUnifiedPlan,
       SetCodecPreferencesVideoRejectsOnlyRtxRedFec) {
  auto fake_engine = std::make_unique<FakeMediaEngine>();
  auto video_codecs = fake_engine->video().LegacySendCodecs();
  video_codecs.push_back(
      CreateVideoRtxCodec(video_codecs.back().id + 1, video_codecs.back().id));
  video_codecs.push_back(
      CreateVideoCodec(video_codecs.back().id + 1, kRedCodecName));
  video_codecs.push_back(
      CreateVideoCodec(video_codecs.back().id + 1, kUlpfecCodecName));
  fake_engine->SetVideoCodecs(video_codecs);

  auto caller = CreatePeerConnectionWithVideo(std::move(fake_engine));

  auto transceiver = caller->pc()->GetTransceivers().front();
  auto codecs =
      caller->pc_factory()->GetRtpSenderCapabilities(MediaType::VIDEO).codecs;
  auto codecs_only_rtx_red_fec = codecs;
  std::erase_if(codecs_only_rtx_red_fec, [](const RtpCodecCapability& codec) {
    return !(codec.name == kRtxCodecName || codec.name == kRedCodecName ||
             codec.name == kUlpfecCodecName);
  });

  auto result = transceiver->SetCodecPreferences(codecs_only_rtx_red_fec);
  EXPECT_EQ(RTCErrorType::INVALID_MODIFICATION, result.type());
}

TEST_F(PeerConnectionMediaTestUnifiedPlan, SetCodecPreferencesAllVideoCodecs) {
  auto caller = CreatePeerConnectionWithVideo();

  auto sender_video_codecs =
      caller->pc_factory()->GetRtpSenderCapabilities(MediaType::VIDEO).codecs;

  auto video_transceiver = caller->pc()->GetTransceivers().front();

  // Normal case, setting preferences to normal capabilities
  EXPECT_TRUE(video_transceiver->SetCodecPreferences(sender_video_codecs).ok());
  std::unique_ptr<SessionDescriptionInterface> offer = caller->CreateOffer();
  auto codecs =
      offer->description()->contents()[0].media_description()->codecs();
  EXPECT_TRUE(CompareCodecs(sender_video_codecs, codecs));
}

TEST_F(PeerConnectionMediaTestUnifiedPlan,
       SetCodecPreferencesResetVideoCodecs) {
  auto caller = CreatePeerConnectionWithVideo();

  auto sender_video_codecs =
      caller->pc_factory()->GetRtpSenderCapabilities(MediaType::VIDEO).codecs;

  std::vector<RtpCodecCapability> empty_codecs = {};

  auto video_transceiver = caller->pc()->GetTransceivers().front();

  // Normal case, resetting preferences with empty list of codecs
  EXPECT_TRUE(video_transceiver->SetCodecPreferences(empty_codecs).ok());
  std::unique_ptr<SessionDescriptionInterface> offer = caller->CreateOffer();
  auto codecs =
      offer->description()->contents()[0].media_description()->codecs();
  EXPECT_TRUE(CompareCodecs(sender_video_codecs, codecs));
}

TEST_F(PeerConnectionMediaTestUnifiedPlan,
       SetCodecPreferencesVideoCodecDuplicatesRemoved) {
  auto caller = CreatePeerConnectionWithVideo();

  auto sender_video_codecs =
      caller->pc_factory()->GetRtpSenderCapabilities(MediaType::VIDEO).codecs;

  auto video_transceiver = caller->pc()->GetTransceivers().front();

  // Check duplicates are removed
  auto single_codec = sender_video_codecs;
  single_codec.resize(1);
  auto duplicate_codec = single_codec;
  duplicate_codec.push_back(duplicate_codec.front());
  duplicate_codec.push_back(duplicate_codec.front());
  duplicate_codec.push_back(duplicate_codec.front());

  EXPECT_TRUE(video_transceiver->SetCodecPreferences(duplicate_codec).ok());
  std::unique_ptr<SessionDescriptionInterface> offer = caller->CreateOffer();
  auto codecs =
      offer->description()->contents()[0].media_description()->codecs();
  EXPECT_TRUE(CompareCodecs(single_codec, codecs));
}

TEST_F(PeerConnectionMediaTestUnifiedPlan, SetCodecPreferencesVideoWithRtx) {
  auto caller_fake_engine = std::make_unique<FakeMediaEngine>();
  auto caller_video_codecs = caller_fake_engine->video().LegacySendCodecs();
  caller_video_codecs.push_back(
      CreateVideoCodec(caller_video_codecs.back().id + 1, kVp8CodecName));
  caller_video_codecs.push_back(CreateVideoRtxCodec(
      caller_video_codecs.back().id + 1, caller_video_codecs.back().id));
  caller_video_codecs.push_back(
      CreateVideoCodec(caller_video_codecs.back().id + 1, kVp9CodecName));
  caller_video_codecs.push_back(CreateVideoRtxCodec(
      caller_video_codecs.back().id + 1, caller_video_codecs.back().id));
  caller_fake_engine->SetVideoCodecs(caller_video_codecs);

  auto caller = CreatePeerConnectionWithVideo(std::move(caller_fake_engine));

  auto sender_video_codecs =
      caller->pc_factory()->GetRtpSenderCapabilities(MediaType::VIDEO).codecs;

  auto video_transceiver = caller->pc()->GetTransceivers().front();

  // Check that RTX codec is properly added
  auto video_codecs_vpx_rtx = sender_video_codecs;
  std::erase_if(video_codecs_vpx_rtx, [](const RtpCodecCapability& codec) {
    return codec.name != kRtxCodecName && codec.name != kVp8CodecName &&
           codec.name != kVp9CodecName;
  });
  absl::c_reverse(video_codecs_vpx_rtx);
  EXPECT_EQ(video_codecs_vpx_rtx.size(), 3u);  // VP8, VP9, RTX
  EXPECT_TRUE(
      video_transceiver->SetCodecPreferences(video_codecs_vpx_rtx).ok());
  std::unique_ptr<SessionDescriptionInterface> offer = caller->CreateOffer();
  auto codecs =
      offer->description()->contents()[0].media_description()->codecs();

  EXPECT_TRUE(CompareCodecs(video_codecs_vpx_rtx, codecs));
  EXPECT_EQ(codecs.size(), 4u);
}

TEST_F(PeerConnectionMediaTestUnifiedPlan,
       SetCodecPreferencesVideoCodecsNegotiation) {
  auto caller_fake_engine = std::make_unique<FakeMediaEngine>();
  auto caller_video_codecs = caller_fake_engine->video().LegacySendCodecs();
  caller_video_codecs.push_back(
      CreateVideoCodec(caller_video_codecs.back().id + 1, kVp8CodecName));
  caller_video_codecs.push_back(CreateVideoRtxCodec(
      caller_video_codecs.back().id + 1, caller_video_codecs.back().id));
  caller_video_codecs.push_back(
      CreateVideoCodec(caller_video_codecs.back().id + 1, kVp9CodecName));
  caller_video_codecs.push_back(CreateVideoRtxCodec(
      caller_video_codecs.back().id + 1, caller_video_codecs.back().id));
  caller_fake_engine->SetVideoCodecs(caller_video_codecs);

  auto callee_fake_engine = std::make_unique<FakeMediaEngine>();
  callee_fake_engine->SetVideoCodecs(caller_video_codecs);

  auto caller = CreatePeerConnectionWithVideo(std::move(caller_fake_engine));
  auto callee = CreatePeerConnection(std::move(callee_fake_engine));

  auto video_codecs =
      caller->pc_factory()->GetRtpSenderCapabilities(MediaType::VIDEO).codecs;

  auto send_transceiver = caller->pc()->GetTransceivers().front();

  auto video_codecs_vpx = video_codecs;
  std::erase_if(video_codecs_vpx, [](const RtpCodecCapability& codec) {
    return codec.name != kVp8CodecName && codec.name != kVp9CodecName;
  });
  EXPECT_EQ(video_codecs_vpx.size(), 2u);  // VP8, VP9
  EXPECT_TRUE(send_transceiver->SetCodecPreferences(video_codecs_vpx).ok());

  std::unique_ptr<SessionDescriptionInterface> offer =
      caller->CreateOfferAndSetAsLocal();
  auto codecs =
      offer->description()->contents()[0].media_description()->codecs();

  EXPECT_EQ(codecs.size(), 2u);  // VP8, VP9
  EXPECT_TRUE(CompareCodecs(video_codecs_vpx, codecs));

  callee->SetRemoteDescription(std::move(offer));

  auto recv_transceiver = callee->pc()->GetTransceivers().front();
  auto video_codecs_vp8_rtx = video_codecs;
  std::erase_if(video_codecs_vp8_rtx, [](const RtpCodecCapability& codec) {
    bool r = codec.name != kVp8CodecName && codec.name != kRtxCodecName;
    return r;
  });
  EXPECT_EQ(video_codecs_vp8_rtx.size(), 2u);  // VP8, RTX
  recv_transceiver->SetCodecPreferences(video_codecs_vp8_rtx);

  std::unique_ptr<SessionDescriptionInterface> answer =
      callee->CreateAnswerAndSetAsLocal();

  auto recv_codecs =
      answer->description()->contents()[0].media_description()->codecs();
  EXPECT_EQ(recv_codecs.size(), 1u);  // VP8
}

TEST_F(PeerConnectionMediaTestUnifiedPlan,
       SetCodecPreferencesVideoCodecsNegotiationReverseOrder) {
  auto caller_fake_engine = std::make_unique<FakeMediaEngine>();
  auto caller_video_codecs = caller_fake_engine->video().LegacySendCodecs();
  caller_video_codecs.push_back(
      CreateVideoCodec(caller_video_codecs.back().id + 1, kVp8CodecName));
  caller_video_codecs.push_back(CreateVideoRtxCodec(
      caller_video_codecs.back().id + 1, caller_video_codecs.back().id));
  caller_video_codecs.push_back(
      CreateVideoCodec(caller_video_codecs.back().id + 1, kVp9CodecName));
  caller_video_codecs.push_back(CreateVideoRtxCodec(
      caller_video_codecs.back().id + 1, caller_video_codecs.back().id));
  caller_fake_engine->SetVideoCodecs(caller_video_codecs);

  auto callee_fake_engine = std::make_unique<FakeMediaEngine>();
  callee_fake_engine->SetVideoCodecs(caller_video_codecs);

  auto caller = CreatePeerConnectionWithVideo(std::move(caller_fake_engine));
  auto callee = CreatePeerConnection(std::move(callee_fake_engine));

  auto video_codecs =
      caller->pc_factory()->GetRtpSenderCapabilities(MediaType::VIDEO).codecs;

  auto send_transceiver = caller->pc()->GetTransceivers().front();

  auto video_codecs_vpx = video_codecs;
  std::erase_if(video_codecs_vpx, [](const RtpCodecCapability& codec) {
    return codec.name != kVp8CodecName && codec.name != kVp9CodecName;
  });
  EXPECT_EQ(video_codecs_vpx.size(), 2u);  // VP8, VP9
  EXPECT_TRUE(send_transceiver->SetCodecPreferences(video_codecs_vpx).ok());

  auto video_codecs_vpx_reverse = video_codecs_vpx;
  absl::c_reverse(video_codecs_vpx_reverse);

  std::unique_ptr<SessionDescriptionInterface> offer =
      caller->CreateOfferAndSetAsLocal();
  auto codecs =
      offer->description()->contents()[0].media_description()->codecs();
  EXPECT_EQ(codecs.size(), 2u);  // VP9, VP8
  EXPECT_TRUE(CompareCodecs(video_codecs_vpx, codecs));

  callee->SetRemoteDescription(std::move(offer));

  auto recv_transceiver = callee->pc()->GetTransceivers().front();
  recv_transceiver->SetCodecPreferences(video_codecs_vpx_reverse);

  std::unique_ptr<SessionDescriptionInterface> answer =
      callee->CreateAnswerAndSetAsLocal();

  auto recv_codecs =
      answer->description()->contents()[0].media_description()->codecs();

  EXPECT_TRUE(CompareCodecs(video_codecs_vpx_reverse, recv_codecs));
}

TEST_F(PeerConnectionMediaTestUnifiedPlan,
       SetCodecPreferencesVoiceActivityDetection) {
  auto fake_engine = std::make_unique<FakeMediaEngine>();
  AddComfortNoiseCodecsToSend(fake_engine.get());
  auto caller = CreatePeerConnectionWithAudio(std::move(fake_engine));

  RTCOfferAnswerOptions options;
  std::unique_ptr<SessionDescriptionInterface> offer =
      caller->CreateOffer(options);
  EXPECT_TRUE(HasAnyComfortNoiseCodecs(offer->description()));

  auto transceiver = caller->pc()->GetTransceivers().front();
  auto capabilities =
      caller->pc_factory()->GetRtpSenderCapabilities(MediaType::AUDIO);
  EXPECT_TRUE(transceiver->SetCodecPreferences(capabilities.codecs).ok());

  options.voice_activity_detection = false;
  offer = caller->CreateOffer(options);
  EXPECT_FALSE(HasAnyComfortNoiseCodecs(offer->description()));
}

// If the "default" payload types of audio/video codecs are the same, and
// audio/video are bundled (as is the default), payload types should be
// remapped to avoid conflict, as normally happens without using
// SetCodecPreferences.
TEST_F(PeerConnectionMediaTestUnifiedPlan,
       SetCodecPreferencesAvoidsPayloadTypeConflictInOffer) {
  auto fake_engine = std::make_unique<FakeMediaEngine>();

  std::vector<Codec> audio_codecs;
  audio_codecs.emplace_back(
      CreateAudioCodec(100, "foo", kDefaultAudioClockRateHz, 1));
  audio_codecs.emplace_back(CreateAudioRtxCodec(101, 100));
  fake_engine->SetAudioCodecs(audio_codecs);

  std::vector<Codec> video_codecs;
  video_codecs.emplace_back(CreateVideoCodec(100, "bar"));
  video_codecs.emplace_back(CreateVideoRtxCodec(101, 100));
  fake_engine->SetVideoCodecs(video_codecs);

  auto caller = CreatePeerConnectionWithAudioVideo(std::move(fake_engine));
  auto transceivers = caller->pc()->GetTransceivers();
  ASSERT_EQ(2u, transceivers.size());

  auto audio_transceiver = caller->pc()->GetTransceivers()[0];
  auto capabilities =
      caller->pc_factory()->GetRtpSenderCapabilities(MediaType::AUDIO);
  EXPECT_TRUE(audio_transceiver->SetCodecPreferences(capabilities.codecs).ok());

  auto video_transceiver = caller->pc()->GetTransceivers()[1];
  capabilities =
      caller->pc_factory()->GetRtpSenderCapabilities(MediaType::VIDEO);
  EXPECT_TRUE(video_transceiver->SetCodecPreferences(capabilities.codecs).ok());

  RTCOfferAnswerOptions options;
  std::unique_ptr<SessionDescriptionInterface> offer =
      caller->CreateOffer(options);
  EXPECT_FALSE(HasPayloadTypeConflict(offer->description()));
  // Sanity check that we got the primary codec and RTX.
  EXPECT_EQ(
      2u,
      GetFirstAudioContentDescription(offer->description())->codecs().size());
  EXPECT_EQ(
      2u,
      GetFirstVideoContentDescription(offer->description())->codecs().size());
}

// Same as above, but preferences set for the answer.
TEST_F(PeerConnectionMediaTestUnifiedPlan,
       SetCodecPreferencesAvoidsPayloadTypeConflictInAnswer) {
  auto fake_engine = std::make_unique<FakeMediaEngine>();

  std::vector<Codec> audio_codecs;
  audio_codecs.emplace_back(
      CreateAudioCodec(100, "foo", kDefaultAudioClockRateHz, 1));
  audio_codecs.emplace_back(CreateAudioRtxCodec(101, 100));
  fake_engine->SetAudioCodecs(audio_codecs);

  std::vector<Codec> video_codecs;
  video_codecs.emplace_back(CreateVideoCodec(100, "bar"));
  video_codecs.emplace_back(CreateVideoRtxCodec(101, 100));
  fake_engine->SetVideoCodecs(video_codecs);

  auto caller = CreatePeerConnectionWithAudioVideo(std::move(fake_engine));

  RTCOfferAnswerOptions options;
  caller->SetRemoteDescription(caller->CreateOffer(options));

  auto transceivers = caller->pc()->GetTransceivers();
  ASSERT_EQ(2u, transceivers.size());

  auto audio_transceiver = caller->pc()->GetTransceivers()[0];
  auto capabilities =
      caller->pc_factory()->GetRtpSenderCapabilities(MediaType::AUDIO);
  EXPECT_TRUE(audio_transceiver->SetCodecPreferences(capabilities.codecs).ok());

  auto video_transceiver = caller->pc()->GetTransceivers()[1];
  capabilities =
      caller->pc_factory()->GetRtpSenderCapabilities(MediaType::VIDEO);
  EXPECT_TRUE(video_transceiver->SetCodecPreferences(capabilities.codecs).ok());

  std::unique_ptr<SessionDescriptionInterface> answer =
      caller->CreateAnswer(options);

  EXPECT_FALSE(HasPayloadTypeConflict(answer->description()));
  // Sanity check that we got the primary codec and RTX.
  EXPECT_EQ(
      2u,
      GetFirstAudioContentDescription(answer->description())->codecs().size());
  EXPECT_EQ(
      2u,
      GetFirstVideoContentDescription(answer->description())->codecs().size());
}

// Same as above, but preferences set for a subsequent offer.
TEST_F(PeerConnectionMediaTestUnifiedPlan,
       SetCodecPreferencesAvoidsPayloadTypeConflictInSubsequentOffer) {
  auto fake_engine = std::make_unique<FakeMediaEngine>();

  std::vector<Codec> audio_codecs;
  audio_codecs.emplace_back(
      CreateAudioCodec(100, "foo", kDefaultAudioClockRateHz, 1));
  audio_codecs.emplace_back(CreateAudioRtxCodec(101, 100));
  fake_engine->SetAudioCodecs(audio_codecs);

  std::vector<Codec> video_codecs;
  video_codecs.emplace_back(CreateVideoCodec(100, "bar"));
  video_codecs.emplace_back(CreateVideoRtxCodec(101, 100));
  fake_engine->SetVideoCodecs(video_codecs);

  auto caller = CreatePeerConnectionWithAudioVideo(std::move(fake_engine));

  RTCOfferAnswerOptions options;
  caller->SetRemoteDescription(caller->CreateOffer(options));
  caller->SetLocalDescription(caller->CreateAnswer(options));

  auto transceivers = caller->pc()->GetTransceivers();
  ASSERT_EQ(2u, transceivers.size());

  auto audio_transceiver = caller->pc()->GetTransceivers()[0];
  auto capabilities =
      caller->pc_factory()->GetRtpSenderCapabilities(MediaType::AUDIO);
  EXPECT_TRUE(audio_transceiver->SetCodecPreferences(capabilities.codecs).ok());

  auto video_transceiver = caller->pc()->GetTransceivers()[1];
  capabilities =
      caller->pc_factory()->GetRtpSenderCapabilities(MediaType::VIDEO);
  EXPECT_TRUE(video_transceiver->SetCodecPreferences(capabilities.codecs).ok());

  auto reoffer = caller->CreateOffer(options);
  ASSERT_THAT(reoffer, NotNull());

  EXPECT_FALSE(HasPayloadTypeConflict(reoffer->description()));
  // Sanity check that we got the primary codec and RTX.
  EXPECT_EQ(
      2u,
      GetFirstAudioContentDescription(reoffer->description())->codecs().size());
  EXPECT_EQ(
      2u,
      GetFirstVideoContentDescription(reoffer->description())->codecs().size());
}

TEST_F(PeerConnectionMediaTestUnifiedPlan,
       SetCodecPreferencesRecvOnlyCodecOnSendOnlyTransceiver) {
  auto fake_engine = std::make_unique<FakeMediaEngine>();

  std::vector<Codec> audio_codecs;
  audio_codecs.emplace_back(
      CreateAudioCodec(100, "foo", kDefaultAudioClockRateHz, 1));
  fake_engine->SetAudioRecvCodecs(audio_codecs);

  auto caller = CreatePeerConnectionWithAudio(std::move(fake_engine));

  auto transceivers = caller->pc()->GetTransceivers();
  ASSERT_EQ(1u, transceivers.size());

  auto audio_transceiver = caller->pc()->GetTransceivers()[0];
  auto error = audio_transceiver->SetDirectionWithError(
      RtpTransceiverDirection::kSendOnly);
  ASSERT_TRUE(error.ok());
  auto capabilities =
      caller->pc_factory()->GetRtpReceiverCapabilities(MediaType::AUDIO);
  EXPECT_TRUE(audio_transceiver->SetCodecPreferences(capabilities.codecs).ok());
  RTCOfferAnswerOptions options;
  EXPECT_TRUE(caller->SetLocalDescription(caller->CreateOffer(options)));
  // The transceiver is still sendonly (not stopped) because preferring a codec
  // that is not applicable to the sendonly use case is the same as not having
  // any codec preferences.
  EXPECT_EQ(audio_transceiver->direction(), RtpTransceiverDirection::kSendOnly);
}

TEST_F(PeerConnectionMediaTestUnifiedPlan, SetCodecPreferencesVideoNoRtx) {
  auto fake_engine = std::make_unique<FakeMediaEngine>();

  std::vector<Codec> video_codecs;
  video_codecs.emplace_back(CreateVideoCodec(100, "bar"));
  video_codecs.emplace_back(CreateVideoRtxCodec(101, 100));
  video_codecs.emplace_back(CreateVideoCodec(102, kRedCodecName));
  fake_engine->SetVideoCodecs(video_codecs);

  auto caller = CreatePeerConnectionWithVideo(std::move(fake_engine));

  auto transceivers = caller->pc()->GetTransceivers();
  ASSERT_EQ(1u, transceivers.size());

  auto video_transceiver = caller->pc()->GetTransceivers()[0];
  EXPECT_TRUE(video_transceiver
                  ->SetDirectionWithError(RtpTransceiverDirection::kRecvOnly)
                  .ok());
  auto capabilities =
      caller->pc_factory()->GetRtpSenderCapabilities(MediaType::VIDEO);

  std::erase_if(capabilities.codecs, [](const RtpCodecCapability& codec) {
    return codec.name == kRtxCodecName;
  });
  EXPECT_EQ(capabilities.codecs.size(), 2u);
  EXPECT_TRUE(video_transceiver->SetCodecPreferences(capabilities.codecs).ok());

  RTCOfferAnswerOptions options;
  std::unique_ptr<SessionDescriptionInterface> offer =
      caller->CreateOffer(options);
  const auto& content = offer->description()->contents()[0];
  auto& codecs = content.media_description()->codecs();
  ASSERT_EQ(codecs.size(), 2u);
  EXPECT_EQ(codecs[0].name, "bar");
  EXPECT_EQ(codecs[1].name, kRedCodecName);
}

// Returns the set of payload types used by all media sections of `description`.
std::set<int> PayloadTypesIn(const SessionDescriptionInterface* description) {
  std::set<int> payload_types;
  for (const ContentInfo& content : description->description()->contents()) {
    for (const Codec& codec : content.media_description()->codecs()) {
      payload_types.insert(codec.id.value());
    }
  }
  return payload_types;
}

// A peer that bundles many media sections and renegotiates repeatedly must
// keep reusing the payload types it has already assigned. Allocating a new
// payload type on every negotiation exhausts the 61 dynamic payload types,
// after which createOffer fails with "All available dynamic PTs have been
// assigned".
TEST_F(PeerConnectionMediaTestUnifiedPlan,
       RepeatedRenegotiationDoesNotExhaustPayloadTypes) {
  constexpr int kSectionsPerMediaType = 7;
  constexpr int kNegotiationRounds = 10;

  auto make_engine = [] {
    auto engine = std::make_unique<FakeMediaEngine>();
    engine->SetAudioCodecs({CreateAudioCodec(111, kOpusCodecName, 48000, 2),
                            CreateAudioCodec(103, "isac", 16000, 1),
                            CreateAudioCodec(0, kPcmuCodecName, 8000, 1)});
    engine->SetVideoCodecs(
        {CreateVideoCodec(96, "vp8"), CreateVideoRtxCodec(97, 96),
         CreateVideoCodec(98, "vp9"), CreateVideoRtxCodec(99, 98),
         CreateVideoCodec(100, "av1"), CreateVideoRtxCodec(101, 100)});
    return engine;
  };

  RTCConfiguration config;
  config.bundle_policy = PeerConnectionInterface::kBundlePolicyMaxBundle;
  auto caller = CreatePeerConnection(config, make_engine());
  auto callee = CreatePeerConnection(config, make_engine());
  ASSERT_THAT(caller, NotNull());
  ASSERT_THAT(callee, NotNull());

  for (int i = 0; i < kSectionsPerMediaType; ++i) {
    caller->AddTransceiver(MediaType::AUDIO);
    caller->AddTransceiver(MediaType::VIDEO);
  }

  std::set<int> payload_types_of_first_offer;
  for (int round = 0; round < kNegotiationRounds; ++round) {
    ASSERT_TRUE(caller->ExchangeOfferAnswerWith(callee.get()))
        << "Negotiation round " << round << " failed";
    std::set<int> payload_types =
        PayloadTypesIn(caller->pc()->local_description());
    if (round == 0) {
      payload_types_of_first_offer = payload_types;
      ASSERT_FALSE(payload_types_of_first_offer.empty());
    } else {
      EXPECT_EQ(payload_types, payload_types_of_first_offer)
          << "Payload type assignment changed in negotiation round " << round;
    }
  }
}

TEST_F(PeerConnectionMediaTestUnifiedPlan,
       RepeatedRenegotiationDoesNotExhaustPayloadTypesBalancedBundle) {
  constexpr int kSectionsPerMediaType = 4;
  constexpr int kNegotiationRounds = 5;

  auto make_engine = [] {
    auto engine = std::make_unique<FakeMediaEngine>();
    engine->SetAudioCodecs({CreateAudioCodec(111, kOpusCodecName, 48000, 2),
                            CreateAudioCodec(103, "isac", 16000, 1),
                            CreateAudioCodec(0, kPcmuCodecName, 8000, 1)});
    engine->SetVideoCodecs(
        {CreateVideoCodec(96, "vp8"), CreateVideoRtxCodec(97, 96),
         CreateVideoCodec(98, "vp9"), CreateVideoRtxCodec(99, 98),
         CreateVideoCodec(100, "av1"), CreateVideoRtxCodec(101, 100)});
    return engine;
  };

  RTCConfiguration config;
  config.bundle_policy = PeerConnectionInterface::kBundlePolicyBalanced;
  auto caller = CreatePeerConnection(config, make_engine());
  auto callee = CreatePeerConnection(config, make_engine());
  ASSERT_THAT(caller, NotNull());
  ASSERT_THAT(callee, NotNull());

  for (int i = 0; i < kSectionsPerMediaType; ++i) {
    caller->AddTransceiver(MediaType::AUDIO);
    caller->AddTransceiver(MediaType::VIDEO);
  }

  std::set<int> payload_types_of_first_offer;
  for (int round = 0; round < kNegotiationRounds; ++round) {
    ASSERT_TRUE(caller->ExchangeOfferAnswerWith(callee.get()))
        << "Negotiation round " << round << " failed";
    std::set<int> payload_types =
        PayloadTypesIn(caller->pc()->local_description());
    if (round == 0) {
      payload_types_of_first_offer = payload_types;
      ASSERT_FALSE(payload_types_of_first_offer.empty());
    } else {
      EXPECT_EQ(payload_types, payload_types_of_first_offer)
          << "Payload type assignment changed in negotiation round " << round;
    }
  }
}

// The codecs of a peer that munges codecs of its own into its offer, as the
// application in https://issues.webrtc.org/564178627 does. Two of them, VP8
// and AV1 profile 1, are codecs that the local endpoint has as well; the rest
// only take up payload types. The payload types that collide with the ones
// that the payload type picker reserves for audio codecs are moved elsewhere
// by the picker, so the offer ends up with 45 payload types either way, which
// leaves 8 of the 61 dynamic payload types free.
std::vector<Codec> MungedOfferCodecs() {
  const std::vector<SdpVideoFormat> formats = test::HardwareVideoFormats();
  std::vector<Codec> codecs = {CreateVideoCodec(96, formats.front()),
                               CreateVideoCodec(97, formats.back())};
  for (const auto& [first, last] : {std::pair{35, 63}, std::pair{98, 111}}) {
    for (int pt = first; pt <= last; ++pt) {
      codecs.push_back(CreateVideoCodec(pt, "munged" + std::to_string(pt)));
    }
  }
  return codecs;
}

// Regression test for https://issues.webrtc.org/564178627. An endpoint with
// hardware codecs supports 14 video formats, each with a retransmission codec
// of its own, so its codec matrix needs more payload types than the offer of
// the munging peer has left. The answer therefore only contains the codecs
// that the application asked for if the codecs that it did not ask for are
// left without a payload type.
TEST_F(PeerConnectionMediaTestUnifiedPlan,
       AnswerWithHardwareCodecMatrixUnderPayloadTypePressure) {
  auto caller_engine = std::make_unique<FakeMediaEngine>();
  caller_engine->SetVideoCodecs(MungedOfferCodecs());
  auto callee_engine = std::make_unique<FakeMediaEngine>();
  callee_engine->SetVideoCodecs(test::HardwareVideoCodecs());

  auto caller = CreatePeerConnectionWithVideo(std::move(caller_engine));
  auto callee = CreatePeerConnection(std::move(callee_engine));
  ASSERT_THAT(caller, NotNull());
  ASSERT_THAT(callee, NotNull());
  ASSERT_TRUE(callee->SetRemoteDescription(caller->CreateOfferAndSetAsLocal()));

  auto transceivers = callee->pc()->GetTransceivers();
  ASSERT_EQ(transceivers.size(), 1u);
  RtpCapabilities capabilities =
      callee->pc_factory()->GetRtpReceiverCapabilities(MediaType::VIDEO);
  std::erase_if(capabilities.codecs, [](const RtpCodecCapability& codec) {
    return codec.name != "VP8" && codec.name != "AV1" &&
           codec.name != kRtxCodecName;
  });
  ASSERT_TRUE(transceivers[0]->SetCodecPreferences(capabilities.codecs).ok());

  std::unique_ptr<SessionDescriptionInterface> answer = callee->CreateAnswer();
  ASSERT_THAT(answer, NotNull());
  const std::vector<Codec>& codecs =
      answer->description()->contents()[0].media_description()->codecs();
  EXPECT_THAT(codecs, Contains(Field(&Codec::name, StrCaseEq("VP8"))));
  EXPECT_THAT(codecs, Contains(Field(&Codec::name, StrCaseEq("AV1"))));
}

INSTANTIATE_TEST_SUITE_P(PeerConnectionMediaTest,
                         PeerConnectionMediaTest,
                         Values(SdpSemantics::kPlanB_DEPRECATED,
                                SdpSemantics::kUnifiedPlan));
}  // namespace webrtc
