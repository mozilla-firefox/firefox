/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// This file contains tests for `RtpTransceiver`.

#include "pc/rtp_transceiver.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/audio_options.h"
#include "api/crypto/crypto_options.h"
#include "api/environment/environment.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/media_types.h"
#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_parameters.h"
#include "api/rtp_transceiver_direction.h"
#include "api/scoped_refptr.h"
#include "api/test/rtc_error_matchers.h"
#include "api/video_codecs/scalability_mode.h"
#include "api/video_codecs/sdp_video_format.h"
#include "call/call.h"
#include "media/base/codec.h"
#include "media/base/codec_comparators.h"
#include "media/base/fake_media_engine.h"
#include "media/base/media_channel.h"
#include "media/base/media_config.h"
#include "media/base/stream_params.h"
#include "media/engine/fake_webrtc_call.h"
#include "p2p/dtls/fake_dtls_transport.h"
#include "pc/codec_vendor.h"
#include "pc/connection_context.h"
#include "pc/dtls_transport.h"
#include "pc/rtp_parameters_conversion.h"
#include "pc/rtp_receiver.h"
#include "pc/rtp_receiver_proxy.h"
#include "pc/rtp_sender.h"
#include "pc/rtp_sender_proxy.h"
#include "pc/rtp_transport.h"
#include "pc/rtp_transport_internal.h"
#include "pc/scoped_operations_batcher.h"
#include "pc/session_description.h"
#include "pc/simulcast_description.h"
#include "pc/test/enable_fake_media.h"
#include "pc/test/fake_codec_lookup_helper.h"
#include "pc/test/mock_channel_interface.h"
#include "pc/test/mock_rtp_receiver_internal.h"
#include "pc/test/mock_rtp_sender_internal.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"
#include "rtc_base/network_route.h"
#include "rtc_base/thread.h"
#include "test/create_test_environment.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/run_loop.h"

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::IsNull;
using ::testing::NiceMock;
using ::testing::Optional;
using ::testing::Property;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SizeIs;

namespace webrtc {

namespace {

scoped_refptr<RtpTransceiver> CreateAudioTransceiverWithChannel(
    const Environment& env,
    Call* call,
    ConnectionContext* context,
    CodecLookupHelper* codec_lookup_helper,
    const AudioOptions& audio_options,
    absl::AnyInvocable<RtpTransportInternal*() &&> transport_lookup = []() {
      return nullptr;
    }) {
  auto transceiver = make_ref_counted<RtpTransceiver>(
      env, call, MediaConfig(), "sender", "receiver", MediaType::AUDIO, nullptr,
      std::vector<std::string>(), std::vector<RtpEncodingParameters>(), context,
      codec_lookup_helper, nullptr, nullptr, audio_options, VideoOptions(),
      CryptoOptions(), nullptr, std::vector<RtpHeaderExtensionCapability>(),
      false, std::vector<SimulcastLayer>(), [] {});

  ScopedOperationsBatcher worker_tasks(context->worker_thread());
  ScopedOperationsBatcher network_tasks(context->network_thread());
  transceiver->CreateChannel("0", call, MediaConfig(), false, CryptoOptions(),
                             nullptr, std::move(transport_lookup), worker_tasks,
                             network_tasks);

  // CreateChannel does not schedule any worker tasks when the transceiver is
  // constructed with pre-allocated channels, so `worker_tasks` should be empty.
  RTC_CHECK(worker_tasks.IsEmpty());
  RTC_CHECK(network_tasks.Run().ok());

  return transceiver;
}

scoped_refptr<RtpTransceiver> CreateVideoTransceiverWithChannel(
    const Environment& env,
    Call* call,
    ConnectionContext* context,
    CodecLookupHelper* codec_lookup_helper,
    const VideoOptions& video_options,
    absl::AnyInvocable<RtpTransportInternal*() &&> transport_lookup = []() {
      return nullptr;
    }) {
  auto transceiver = make_ref_counted<RtpTransceiver>(
      env, call, MediaConfig(), "sender", "receiver", MediaType::VIDEO, nullptr,
      std::vector<std::string>(), std::vector<RtpEncodingParameters>(), context,
      codec_lookup_helper, nullptr, nullptr, AudioOptions(), video_options,
      CryptoOptions(), nullptr, std::vector<RtpHeaderExtensionCapability>(),
      false, std::vector<SimulcastLayer>(), [] {});

  ScopedOperationsBatcher worker_tasks(context->worker_thread());
  ScopedOperationsBatcher network_tasks(context->network_thread());
  transceiver->CreateChannel("0", call, MediaConfig(), false, CryptoOptions(),
                             nullptr, std::move(transport_lookup), worker_tasks,
                             network_tasks);

  // CreateChannel does not schedule any worker tasks when the transceiver is
  // constructed with pre-allocated channels, so `worker_tasks` should be empty.
  RTC_CHECK(worker_tasks.IsEmpty());
  RTC_CHECK(network_tasks.Run().ok());

  return transceiver;
}

class RtpTransceiverTest : public testing::Test {
 public:
  RtpTransceiverTest()
      : env_(CreateTestEnvironment()),
        dependencies_(MakeDependencies()),
        context_(ConnectionContext::Create(env_, &dependencies_)),
        media_engine_ref_(context_),
        codec_lookup_helper_(context_.get(), env_.field_trials()) {}

 protected:
  const Environment& env() const { return env_; }
  FakeMediaEngine* media_engine() {
    // We know this cast is safe because we supplied the fake implementation
    // in MakeDependencies().
    return static_cast<FakeMediaEngine*>(media_engine_ref_.media_engine());
  }
  ConnectionContext* context() { return context_.get(); }
  CodecLookupHelper* codec_lookup_helper() { return &codec_lookup_helper_; }
  FakeCodecLookupHelper* fake_codec_lookup_helper() {
    return &codec_lookup_helper_;
  }

 protected:
  test::RunLoop main_thread_;

 private:
  static PeerConnectionFactoryDependencies MakeDependencies() {
    PeerConnectionFactoryDependencies d;
    d.network_thread = Thread::Current();
    d.signaling_thread = Thread::Current();
    EnableFakeMedia(d, std::make_unique<FakeMediaEngine>());
    return d;
  }

  Environment env_;
  PeerConnectionFactoryDependencies dependencies_;
  scoped_refptr<ConnectionContext> context_;
  ConnectionContext::MediaEngineReference media_engine_ref_;
  FakeCodecLookupHelper codec_lookup_helper_;
};

class RtpTransceiverDoubleThreadTest : public testing::Test {
 public:
  RtpTransceiverDoubleThreadTest()
      : env_(CreateTestEnvironment()),
        network_thread_(Thread::Create()),
        dependencies_(MakeDependencies(network_thread_.get())),
        context_(ConnectionContext::Create(env_, &dependencies_)),
        codec_lookup_helper_(context_.get(), env_.field_trials()) {
    network_thread_->Start();
    context_->worker_thread()->BlockingCall([&]() {
      media_engine_ref_ =
          std::make_unique<ConnectionContext::MediaEngineReference>(context_);
    });
  }

  ~RtpTransceiverDoubleThreadTest() override {
    context_->worker_thread()->BlockingCall(
        [&]() { media_engine_ref_.reset(); });
  }

 protected:
  const Environment& env() const { return env_; }
  FakeMediaEngine* media_engine() {
    return static_cast<FakeMediaEngine*>(media_engine_ref_->media_engine());
  }
  ConnectionContext* context() { return context_.get(); }
  CodecLookupHelper* codec_lookup_helper() { return &codec_lookup_helper_; }

 private:
  static PeerConnectionFactoryDependencies MakeDependencies(
      Thread* network_thread) {
    PeerConnectionFactoryDependencies d;
    d.network_thread = network_thread;
    d.signaling_thread = Thread::Current();
    RTC_LOG(LS_INFO) << "MakeDependencies signaling_thread="
                     << d.signaling_thread;
    EnableFakeMedia(d, std::make_unique<FakeMediaEngine>());
    return d;
  }

  test::RunLoop main_thread_;
  Environment env_;
  std::unique_ptr<Thread> network_thread_;
  PeerConnectionFactoryDependencies dependencies_;
  scoped_refptr<ConnectionContext> context_;
  std::unique_ptr<ConnectionContext::MediaEngineReference> media_engine_ref_;
  FakeCodecLookupHelper codec_lookup_helper_;
};

class RtpTransceiverTestWithFakeCall : public RtpTransceiverTest {
 public:
  RtpTransceiverTestWithFakeCall()
      : call_(std::make_unique<FakeCall>(env(),
                                         Thread::Current(),
                                         Thread::Current())) {}

 protected:
  std::unique_ptr<FakeCall> call_;
};

// Checks that a channel cannot be set on a stopped `RtpTransceiver`.
TEST_F(RtpTransceiverTest, CannotSetChannelOnStoppedTransceiver) {
  const std::string content_name("my_mid");
  auto transceiver = make_ref_counted<RtpTransceiver>(
      env(), MediaType::AUDIO, context(), codec_lookup_helper(), nullptr,
      AudioOptions(), VideoOptions());
  transceiver->set_mid(content_name);
  auto channel1 = std::make_unique<NiceMock<MockChannelInterface>>();
  EXPECT_CALL(*channel1, media_type()).WillRepeatedly(Return(MediaType::AUDIO));
  EXPECT_CALL(*channel1, mid()).WillRepeatedly(ReturnRef(content_name));
  EXPECT_CALL(*channel1, SetRtpTransport(_)).WillRepeatedly(Return(true));

  transceiver->SetChannelForTest(std::move(channel1));
  EXPECT_TRUE(transceiver->HasChannel());

  // Stop the transceiver.
  transceiver->StopInternal();
  EXPECT_TRUE(transceiver->HasChannel());

  auto channel2 = std::make_unique<NiceMock<MockChannelInterface>>();
  EXPECT_CALL(*channel2, media_type()).WillRepeatedly(Return(MediaType::AUDIO));

  // Clear the current channel - required to allow SetChannelForTest()
  transceiver->ClearChannel();

  ASSERT_FALSE(transceiver->HasChannel());

  // Channel can no longer be set, so this call should be a no-op.
  transceiver->SetChannelForTest(std::move(channel2));
  EXPECT_FALSE(transceiver->HasChannel());
}

// Checks that a channel can be unset on a stopped `RtpTransceiver`
TEST_F(RtpTransceiverTest, CanUnsetChannelOnStoppedTransceiver) {
  const std::string content_name("my_mid");
  auto transceiver = make_ref_counted<RtpTransceiver>(
      env(), MediaType::VIDEO, context(), codec_lookup_helper(), nullptr,
      AudioOptions(), VideoOptions());
  transceiver->set_mid(content_name);
  auto channel = std::make_unique<NiceMock<MockChannelInterface>>();
  EXPECT_CALL(*channel, media_type()).WillRepeatedly(Return(MediaType::VIDEO));
  EXPECT_CALL(*channel, mid()).WillRepeatedly(ReturnRef(content_name));
  EXPECT_CALL(*channel, SetRtpTransport(_)).WillRepeatedly(Return(true));

  transceiver->SetChannelForTest(std::move(channel));
  EXPECT_TRUE(transceiver->HasChannel());

  // Stop the transceiver.
  transceiver->StopInternal();
  EXPECT_TRUE(transceiver->HasChannel());

  // Set the channel to `nullptr`.
  transceiver->ClearChannel();
  EXPECT_FALSE(transceiver->HasChannel());
}

TEST_F(RtpTransceiverTestWithFakeCall, TransportNameIsUpdated) {
  const std::string content_name("my_mid");

  auto fake_dtls =
      std::make_unique<FakeDtlsTransport>(env(), "test_transport", false);
  auto rtp_transport = std::make_unique<RtpTransport>(/*rtcp_mux_enabled=*/true,
                                                      env().field_trials());
  rtp_transport->SetRtpPacketTransport(fake_dtls.get());

  auto transceiver = CreateAudioTransceiverWithChannel(
      env(), call_.get(), context(), codec_lookup_helper(), AudioOptions(),
      [&]() -> RtpTransportInternal* { return rtp_transport.get(); });

  EXPECT_TRUE(transceiver->HasChannel());
  EXPECT_EQ(transceiver->transport_name(), "test_transport");

  auto dtls_transport = make_ref_counted<DtlsTransport>(fake_dtls.get());
  transceiver->SetTransport(dtls_transport, "updated_transport");
  EXPECT_EQ(transceiver->transport_name(), "updated_transport");

  // Setting null transport should clear the name.
  transceiver->SetTransport(nullptr, std::nullopt);
  EXPECT_EQ(transceiver->transport_name(), std::nullopt);
  EXPECT_TRUE(transceiver->HasChannel());

  // Clearing the channel should reset the transport name to nullopt.
  transceiver->SetTransport(dtls_transport, "yadt");
  EXPECT_EQ(transceiver->transport_name(), "yadt");
  transceiver->ClearChannel();
  EXPECT_FALSE(transceiver->transport_name().has_value());
}

class RtpTransceiverUnifiedPlanTest : public RtpTransceiverTest {
 public:
  static scoped_refptr<MockRtpReceiverInternal> MockReceiver(
      MediaType media_type) {
    auto receiver = make_ref_counted<NiceMock<MockRtpReceiverInternal>>();
    EXPECT_CALL(*receiver.get(), media_type())
        .WillRepeatedly(Return(media_type));
    return receiver;
  }

  static scoped_refptr<MockRtpSenderInternal> MockSender(MediaType media_type) {
    auto sender = make_ref_counted<NiceMock<MockRtpSenderInternal>>();
    EXPECT_CALL(*sender.get(), media_type()).WillRepeatedly(Return(media_type));
    return sender;
  }

  scoped_refptr<RtpTransceiver> CreateTransceiver(
      scoped_refptr<RtpSenderInternal> sender,
      scoped_refptr<RtpReceiverInternal> receiver) {
    return make_ref_counted<RtpTransceiver>(
        env(),
        RtpSenderProxyWithInternal<RtpSenderInternal>::Create(
            Thread::Current(), std::move(sender)),
        RtpReceiverProxyWithInternal<RtpReceiverInternal>::Create(
            Thread::Current(), Thread::Current(), std::move(receiver)),
        context(), codec_lookup_helper(),
        media_engine()->voice().GetRtpHeaderExtensions(&env().field_trials()),
        /* on_negotiation_needed= */ [] {}, AudioOptions(), VideoOptions());
  }
};

// Basic tests for Stop()
TEST_F(RtpTransceiverUnifiedPlanTest, StopSetsDirection) {
  scoped_refptr<MockRtpReceiverInternal> receiver =
      MockReceiver(MediaType::AUDIO);
  scoped_refptr<MockRtpSenderInternal> sender = MockSender(MediaType::AUDIO);
  scoped_refptr<RtpTransceiver> transceiver =
      CreateTransceiver(sender, receiver);

  EXPECT_EQ(RtpTransceiverDirection::kInactive, transceiver->direction());
  EXPECT_FALSE(transceiver->current_direction());
  transceiver->StopStandard();
  EXPECT_EQ(RtpTransceiverDirection::kStopped, transceiver->direction());
  EXPECT_FALSE(transceiver->current_direction());
  auto stop_task = transceiver->GetStopTransceiverProcedure();
  if (stop_task) {
    std::move(stop_task)();
  }
  EXPECT_TRUE(transceiver->current_direction());
  EXPECT_EQ(RtpTransceiverDirection::kStopped, transceiver->direction());
  EXPECT_EQ(RtpTransceiverDirection::kStopped,
            *transceiver->current_direction());
}

// Applying content to the channel can fail after the media channel has been
// partially updated. The parameters that the senders cache must still be
// updated with the parameters of the media channel.
TEST_F(RtpTransceiverUnifiedPlanTest,
       UpdatesSenderParametersWhenSettingContentFails) {
  constexpr uint32_t kSsrc = 1234;
  scoped_refptr<MockRtpSenderInternal> sender = MockSender(MediaType::AUDIO);
  EXPECT_CALL(*sender, ssrc()).WillRepeatedly(Return(kSsrc));
  scoped_refptr<RtpTransceiver> transceiver =
      CreateTransceiver(sender, MockReceiver(MediaType::AUDIO));

  FakeVoiceMediaSendChannel send_channel(AudioOptions(), Thread::Current());
  send_channel.AddSendStream(StreamParams::CreateLegacy(kSsrc));
  const RtpParameters expected_parameters =
      send_channel.GetRtpSendParameters(kSsrc);
  ASSERT_EQ(expected_parameters.encodings.size(), 1u);

  const std::string content_name("my_mid");
  transceiver->set_mid(content_name);
  auto channel = std::make_unique<NiceMock<MockChannelInterface>>();
  EXPECT_CALL(*channel, media_type()).WillRepeatedly(Return(MediaType::AUDIO));
  EXPECT_CALL(*channel, mid()).WillRepeatedly(ReturnRef(content_name));
  EXPECT_CALL(*channel, SetRtpTransport(_)).WillRepeatedly(Return(true));
  EXPECT_CALL(*channel, media_send_channel())
      .WillRepeatedly(Return(&send_channel));
  EXPECT_CALL(*channel, SetLocalContent(_, _))
      .WillOnce(Return(RTCError::InvalidParameter()));
  ASSERT_TRUE(transceiver->SetChannelForTest(std::move(channel)).ok());

  EXPECT_CALL(*sender, SetCachedParameters(Optional(expected_parameters)));

  AudioContentDescription description;
  ScopedOperationsBatcher batcher(context()->worker_thread());
  transceiver->SetChannelLocalContent(&description, SdpType::kOffer, batcher);
  EXPECT_FALSE(batcher.Run().ok());

  EXPECT_CALL(*sender, SetMediaChannel(IsNull())).WillRepeatedly(Return());
  transceiver->ClearChannel();
}

class RtpTransceiverFilteredCodecPreferencesTest
    : public RtpTransceiverUnifiedPlanTest {
 public:
  RtpTransceiverFilteredCodecPreferencesTest()
      : transceiver_(CreateTransceiver(MockSender(MediaType::VIDEO),
                                       MockReceiver(MediaType::VIDEO))) {}

  struct H264CodecCapabilities {
    Codec cricket_sendrecv_codec;
    RtpCodecCapability sendrecv_codec;
    Codec cricket_sendonly_codec;
    RtpCodecCapability sendonly_codec;
    Codec cricket_recvonly_codec;
    RtpCodecCapability recvonly_codec;
    Codec cricket_rtx_codec;
    RtpCodecCapability rtx_codec;
  };

  // This function must be called after modifying the media factory's
  // capabilities, since the transceiver picks up codecs from the factory
  // at transceiver create time.
  void RecreateTransceiver() {
    fake_codec_lookup_helper()->Reset();
    transceiver_ = CreateTransceiver(MockSender(MediaType::VIDEO),
                                     MockReceiver(MediaType::VIDEO));
  }

  // For H264, the profile and level IDs are entangled. This function uses
  // profile-level-id values that are not equal even when levels are ignored.
  H264CodecCapabilities ConfigureH264CodecCapabilities() {
    Codec cricket_sendrecv_codec =
        CreateVideoCodec(SdpVideoFormat("H264",
                                        {{"level-asymmetry-allowed", "1"},
                                         {"packetization-mode", "1"},
                                         {"profile-level-id", "42f00b"}},
                                        {ScalabilityMode::kL1T1}));
    Codec cricket_sendonly_codec =
        CreateVideoCodec(SdpVideoFormat("H264",
                                        {{"level-asymmetry-allowed", "1"},
                                         {"packetization-mode", "1"},
                                         {"profile-level-id", "640034"}},
                                        {ScalabilityMode::kL1T1}));
    Codec cricket_recvonly_codec =
        CreateVideoCodec(SdpVideoFormat("H264",
                                        {{"level-asymmetry-allowed", "1"},
                                         {"packetization-mode", "1"},
                                         {"profile-level-id", "f4001f"}},
                                        {ScalabilityMode::kL1T1}));
    Codec cricket_rtx_codec =
        CreateVideoRtxCodec(Codec::kIdNotSet, Codec::kIdNotSet);
    media_engine()->SetVideoSendCodecs(
        {cricket_sendrecv_codec, cricket_sendonly_codec, cricket_rtx_codec});
    media_engine()->SetVideoRecvCodecs(
        {cricket_sendrecv_codec, cricket_recvonly_codec, cricket_rtx_codec});
    H264CodecCapabilities capabilities = {
        .cricket_sendrecv_codec = cricket_sendrecv_codec,
        .sendrecv_codec = ToRtpCodecCapability(cricket_sendrecv_codec),
        .cricket_sendonly_codec = cricket_sendonly_codec,
        .sendonly_codec = ToRtpCodecCapability(cricket_sendonly_codec),
        .cricket_recvonly_codec = cricket_recvonly_codec,
        .recvonly_codec = ToRtpCodecCapability(cricket_recvonly_codec),
        .cricket_rtx_codec = cricket_rtx_codec,
        .rtx_codec = ToRtpCodecCapability(cricket_rtx_codec),
    };
    EXPECT_FALSE(IsSameRtpCodecIgnoringLevel(
        capabilities.cricket_sendrecv_codec, capabilities.sendonly_codec));
    EXPECT_FALSE(IsSameRtpCodecIgnoringLevel(
        capabilities.cricket_sendrecv_codec, capabilities.recvonly_codec));
    EXPECT_FALSE(IsSameRtpCodecIgnoringLevel(
        capabilities.cricket_sendonly_codec, capabilities.recvonly_codec));
    // Because RtpTransceiver buffers codec information in a CodecVendor,
    // we must recreate it after changing the supported codecs.
    RecreateTransceiver();
    return capabilities;
  }

#ifdef RTC_ENABLE_H265
  struct H265CodecCapabilities {
    // The level-id from sender getCapabilities() or receiver getCapabilities().
    static constexpr const char* kSendOnlyLevel = "180";
    static constexpr const char* kRecvOnlyLevel = "156";
    // A valid H265 level-id, but one not present in either getCapabilities().
    static constexpr const char* kLevelNotInCapabilities = "135";

    Codec cricket_sendonly_codec;
    RtpCodecCapability sendonly_codec;
    Codec cricket_recvonly_codec;
    RtpCodecCapability recvonly_codec;
  };

  // For H265, the profile and level IDs are separate and are ignored by
  // IsSameRtpCodecIgnoringLevel().
  H265CodecCapabilities ConfigureH265CodecCapabilities() {
    Codec cricket_sendonly_codec = CreateVideoCodec(
        SdpVideoFormat("H265",
                       {{"profile-id", "1"},
                        {"tier-flag", "0"},
                        {"level-id", H265CodecCapabilities::kSendOnlyLevel},
                        {"tx-mode", "SRST"}},
                       {ScalabilityMode::kL1T1}));
    Codec cricket_recvonly_codec = CreateVideoCodec(
        SdpVideoFormat("H265",
                       {{"profile-id", "1"},
                        {"tier-flag", "0"},
                        {"level-id", H265CodecCapabilities::kRecvOnlyLevel},
                        {"tx-mode", "SRST"}},
                       {ScalabilityMode::kL1T1}));
    media_engine()->SetVideoSendCodecs({cricket_sendonly_codec});
    media_engine()->SetVideoRecvCodecs({cricket_recvonly_codec});
    // Because RtpTransceiver buffers codec information in a CodecVendor,
    // we must recreate it after changing the supported codecs.
    RecreateTransceiver();
    return {
        .cricket_sendonly_codec = cricket_sendonly_codec,
        .sendonly_codec = ToRtpCodecCapability(cricket_sendonly_codec),
        .cricket_recvonly_codec = cricket_recvonly_codec,
        .recvonly_codec = ToRtpCodecCapability(cricket_recvonly_codec),
    };
  }
#endif  // RTC_ENABLE_H265

 protected:
  scoped_refptr<RtpTransceiver> transceiver_;
};

TEST_F(RtpTransceiverFilteredCodecPreferencesTest, EmptyByDefault) {
  ConfigureH264CodecCapabilities();

  EXPECT_THAT(
      transceiver_->SetDirectionWithError(RtpTransceiverDirection::kSendRecv),
      IsRtcOk());
  EXPECT_THAT(transceiver_->filtered_codec_preferences(), SizeIs(0));

  EXPECT_THAT(
      transceiver_->SetDirectionWithError(RtpTransceiverDirection::kSendOnly),
      IsRtcOk());
  EXPECT_THAT(transceiver_->filtered_codec_preferences(), SizeIs(0));

  EXPECT_THAT(
      transceiver_->SetDirectionWithError(RtpTransceiverDirection::kRecvOnly),
      IsRtcOk());
  EXPECT_THAT(transceiver_->filtered_codec_preferences(), SizeIs(0));

  EXPECT_THAT(
      transceiver_->SetDirectionWithError(RtpTransceiverDirection::kInactive),
      IsRtcOk());
  EXPECT_THAT(transceiver_->filtered_codec_preferences(), SizeIs(0));
}

TEST_F(RtpTransceiverFilteredCodecPreferencesTest, OrderIsMaintained) {
  const auto codecs = ConfigureH264CodecCapabilities();
  std::vector<RtpCodecCapability> codec_capabilities = {codecs.sendrecv_codec,
                                                        codecs.rtx_codec};
  EXPECT_THAT(transceiver_->SetCodecPreferences(codec_capabilities), IsRtcOk());
  EXPECT_THAT(transceiver_->filtered_codec_preferences(),
              ElementsAre(codec_capabilities[0], codec_capabilities[1]));
  // Reverse order.
  codec_capabilities = {codecs.rtx_codec, codecs.sendrecv_codec};
  EXPECT_THAT(transceiver_->SetCodecPreferences(codec_capabilities), IsRtcOk());
  EXPECT_THAT(transceiver_->filtered_codec_preferences(),
              ElementsAre(codec_capabilities[0], codec_capabilities[1]));
}

TEST_F(RtpTransceiverFilteredCodecPreferencesTest,
       FiltersCodecsBasedOnDirection) {
  const auto codecs = ConfigureH264CodecCapabilities();
  std::vector<RtpCodecCapability> codec_capabilities = {
      codecs.sendonly_codec, codecs.sendrecv_codec, codecs.recvonly_codec};
  EXPECT_THAT(transceiver_->SetCodecPreferences(codec_capabilities), IsRtcOk());

  EXPECT_THAT(
      transceiver_->SetDirectionWithError(RtpTransceiverDirection::kSendRecv),
      IsRtcOk());
  EXPECT_THAT(transceiver_->filtered_codec_preferences(),
              ElementsAre(codecs.sendrecv_codec));

  EXPECT_THAT(
      transceiver_->SetDirectionWithError(RtpTransceiverDirection::kSendOnly),
      IsRtcOk());
  EXPECT_THAT(transceiver_->filtered_codec_preferences(),
              ElementsAre(codecs.sendonly_codec, codecs.sendrecv_codec));

  EXPECT_THAT(
      transceiver_->SetDirectionWithError(RtpTransceiverDirection::kRecvOnly),
      IsRtcOk());
  EXPECT_THAT(transceiver_->filtered_codec_preferences(),
              ElementsAre(codecs.sendrecv_codec, codecs.recvonly_codec));

  EXPECT_THAT(
      transceiver_->SetDirectionWithError(RtpTransceiverDirection::kInactive),
      IsRtcOk());
  EXPECT_THAT(transceiver_->filtered_codec_preferences(),
              ElementsAre(codecs.sendrecv_codec));
}

TEST_F(RtpTransceiverFilteredCodecPreferencesTest,
       RtxIsIncludedAfterFiltering) {
  const auto codecs = ConfigureH264CodecCapabilities();
  std::vector<RtpCodecCapability> codec_capabilities = {codecs.recvonly_codec,
                                                        codecs.rtx_codec};
  EXPECT_THAT(transceiver_->SetCodecPreferences(codec_capabilities), IsRtcOk());

  EXPECT_THAT(
      transceiver_->SetDirectionWithError(RtpTransceiverDirection::kRecvOnly),
      IsRtcOk());
  EXPECT_THAT(transceiver_->filtered_codec_preferences(),
              ElementsAre(codecs.recvonly_codec, codecs.rtx_codec));
}

TEST_F(RtpTransceiverFilteredCodecPreferencesTest,
       NoMediaIsTheSameAsNoPreference) {
  const auto codecs = ConfigureH264CodecCapabilities();
  std::vector<RtpCodecCapability> codec_capabilities = {codecs.recvonly_codec,
                                                        codecs.rtx_codec};
  EXPECT_THAT(transceiver_->SetCodecPreferences(codec_capabilities), IsRtcOk());

  EXPECT_THAT(
      transceiver_->SetDirectionWithError(RtpTransceiverDirection::kSendOnly),
      IsRtcOk());
  // After filtering the only codec that remains is RTX which is not a media
  // codec, this is the same as not having any preferences.
  EXPECT_THAT(transceiver_->filtered_codec_preferences(), SizeIs(0));

  // But the preferences are remembered in case the direction changes such that
  // we do have a media codec.
  EXPECT_THAT(
      transceiver_->SetDirectionWithError(RtpTransceiverDirection::kRecvOnly),
      IsRtcOk());
  EXPECT_THAT(transceiver_->filtered_codec_preferences(),
              ElementsAre(codecs.recvonly_codec, codecs.rtx_codec));
}

TEST_F(RtpTransceiverFilteredCodecPreferencesTest,
       H264LevelIdsIgnoredByFilter) {
  // Baseline 3.1 and 5.2 are compatible when ignoring level IDs.
  Codec baseline_3_1 =
      CreateVideoCodec(SdpVideoFormat("H264",
                                      {{"level-asymmetry-allowed", "1"},
                                       {"packetization-mode", "1"},
                                       {"profile-level-id", "42001f"}},
                                      {ScalabilityMode::kL1T1}));
  Codec baseline_5_2 =
      CreateVideoCodec(SdpVideoFormat("H264",
                                      {{"level-asymmetry-allowed", "1"},
                                       {"packetization-mode", "1"},
                                       {"profile-level-id", "420034"}},
                                      {ScalabilityMode::kL1T1}));
  // High is NOT compatible with baseline.
  Codec high_3_1 =
      CreateVideoCodec(SdpVideoFormat("H264",
                                      {{"level-asymmetry-allowed", "1"},
                                       {"packetization-mode", "1"},
                                       {"profile-level-id", "64001f"}},
                                      {ScalabilityMode::kL1T1}));
  // Configure being able to both send and receive Baseline but using different
  // level IDs in either direction, while the High profile is "truly" recvonly.
  media_engine()->SetVideoSendCodecs({baseline_3_1});
  media_engine()->SetVideoRecvCodecs({baseline_5_2, high_3_1});
  // Because RtpTransceiver buffers codec information in a CodecVendor,
  // we must recreate it after changing the supported codecs.
  RecreateTransceiver();

  // Prefer to "sendrecv" Baseline 5.2. Even though we can only send 3.1 this
  // codec is not filtered out due to 5.2 and 3.1 being compatible when ignoring
  // level IDs.
  std::vector<RtpCodecCapability> codec_capabilities = {
      ToRtpCodecCapability(baseline_5_2)};
  EXPECT_THAT(transceiver_->SetCodecPreferences(codec_capabilities), IsRtcOk());
  EXPECT_THAT(
      transceiver_->SetDirectionWithError(RtpTransceiverDirection::kSendRecv),
      IsRtcOk());
  EXPECT_THAT(transceiver_->filtered_codec_preferences(),
              ElementsAre(codec_capabilities[0]));
  // Prefer to "sendrecv" High 3.1. This gets filtered out because we cannot
  // send it (Baseline 3.1 is not compatible with it).
  codec_capabilities = {ToRtpCodecCapability(high_3_1)};
  EXPECT_THAT(transceiver_->SetCodecPreferences(codec_capabilities), IsRtcOk());
  EXPECT_THAT(transceiver_->filtered_codec_preferences(), SizeIs(0));
  // Change direction to "recvonly" to avoid High 3.1 being filtered out.
  EXPECT_THAT(
      transceiver_->SetDirectionWithError(RtpTransceiverDirection::kRecvOnly),
      IsRtcOk());
  EXPECT_THAT(transceiver_->filtered_codec_preferences(),
              ElementsAre(codec_capabilities[0]));
}

#ifdef RTC_ENABLE_H265
TEST_F(RtpTransceiverFilteredCodecPreferencesTest,
       H265LevelIdIsIgnoredByFilter) {
  const auto codecs = ConfigureH265CodecCapabilities();
  std::vector<RtpCodecCapability> codec_capabilities = {codecs.sendonly_codec,
                                                        codecs.recvonly_codec};
  EXPECT_THAT(transceiver_->SetCodecPreferences(codec_capabilities), IsRtcOk());
  // Regardless of direction, both codecs are preferred due to ignoring levels.
  EXPECT_THAT(
      transceiver_->SetDirectionWithError(RtpTransceiverDirection::kSendOnly),
      IsRtcOk());
  EXPECT_THAT(transceiver_->filtered_codec_preferences(),
              ElementsAre(codec_capabilities[0], codec_capabilities[1]));
  EXPECT_THAT(
      transceiver_->SetDirectionWithError(RtpTransceiverDirection::kRecvOnly),
      IsRtcOk());
  EXPECT_THAT(transceiver_->filtered_codec_preferences(),
              ElementsAre(codec_capabilities[0], codec_capabilities[1]));
  EXPECT_THAT(
      transceiver_->SetDirectionWithError(RtpTransceiverDirection::kSendRecv),
      IsRtcOk());
  EXPECT_THAT(transceiver_->filtered_codec_preferences(),
              ElementsAre(codec_capabilities[0], codec_capabilities[1]));
}

TEST_F(RtpTransceiverFilteredCodecPreferencesTest,
       H265LevelIdHasToBeFromSenderOrReceiverCapabilities) {
  ConfigureH265CodecCapabilities();
  Codec cricket_codec = CreateVideoCodec(SdpVideoFormat(
      "H265",
      {{"profile-id", "1"},
       {"tier-flag", "0"},
       {"level-id", H265CodecCapabilities::kLevelNotInCapabilities},
       {"tx-mode", "SRST"}},
      {ScalabilityMode::kL1T1}));

  std::vector<RtpCodecCapability> codec_capabilities = {
      ToRtpCodecCapability(cricket_codec)};
  EXPECT_THAT(transceiver_->SetCodecPreferences(codec_capabilities),
              IsRtcErrorWithTypeAndMessage(
                  RTCErrorType::INVALID_MODIFICATION,
                  "Invalid codec preferences: Missing codec from codec "
                  "capabilities."));
}
#endif  // RTC_ENABLE_H265

class RtpTransceiverTestForHeaderExtensions
    : public RtpTransceiverUnifiedPlanTest {
 public:
  RtpTransceiverTestForHeaderExtensions()
      : extensions_(
            {RtpHeaderExtensionCapability("uri1",
                                          RtpTransceiverDirection::kSendOnly),
             RtpHeaderExtensionCapability("uri2",
                                          RtpTransceiverDirection::kRecvOnly),
             RtpHeaderExtensionCapability(RtpExtension::kMidUri,
                                          RtpTransceiverDirection::kSendRecv),
             RtpHeaderExtensionCapability(RtpExtension::kVideoRotationUri,
                                          RtpTransceiverDirection::kSendRecv)}),
        transceiver_(make_ref_counted<RtpTransceiver>(
            env(),
            RtpSenderProxyWithInternal<RtpSenderInternal>::Create(
                Thread::Current(),
                sender_),
            RtpReceiverProxyWithInternal<RtpReceiverInternal>::Create(
                Thread::Current(),
                Thread::Current(),
                receiver_),
            context(),
            codec_lookup_helper(),
            extensions_,
            /* on_negotiation_needed= */ [] {},
            AudioOptions(),
            VideoOptions())) {}

  void ClearChannel() {
    EXPECT_CALL(*sender_.get(), SetMediaChannel(IsNull()))
        .WillRepeatedly(Return());
    transceiver_->ClearChannel();
  }

 protected:
  scoped_refptr<MockRtpReceiverInternal> receiver_ =
      MockReceiver(MediaType::AUDIO);
  scoped_refptr<MockRtpSenderInternal> sender_ = MockSender(MediaType::AUDIO);

  std::vector<RtpHeaderExtensionCapability> extensions_;
  scoped_refptr<RtpTransceiver> transceiver_;
};

TEST_F(RtpTransceiverTestForHeaderExtensions, OffersChannelManagerList) {
  EXPECT_EQ(transceiver_->GetHeaderExtensionsToNegotiate(), extensions_);
}

TEST_F(RtpTransceiverTestForHeaderExtensions, ModifiesDirection) {
  auto modified_extensions = extensions_;
  modified_extensions[0].direction = RtpTransceiverDirection::kSendOnly;
  EXPECT_TRUE(
      transceiver_->SetHeaderExtensionsToNegotiate(modified_extensions).ok());
  EXPECT_EQ(transceiver_->GetHeaderExtensionsToNegotiate(),
            modified_extensions);
  modified_extensions[0].direction = RtpTransceiverDirection::kRecvOnly;
  EXPECT_TRUE(
      transceiver_->SetHeaderExtensionsToNegotiate(modified_extensions).ok());
  EXPECT_EQ(transceiver_->GetHeaderExtensionsToNegotiate(),
            modified_extensions);
  modified_extensions[0].direction = RtpTransceiverDirection::kSendRecv;
  EXPECT_TRUE(
      transceiver_->SetHeaderExtensionsToNegotiate(modified_extensions).ok());
  EXPECT_EQ(transceiver_->GetHeaderExtensionsToNegotiate(),
            modified_extensions);
  modified_extensions[0].direction = RtpTransceiverDirection::kInactive;
  EXPECT_TRUE(
      transceiver_->SetHeaderExtensionsToNegotiate(modified_extensions).ok());
  EXPECT_EQ(transceiver_->GetHeaderExtensionsToNegotiate(),
            modified_extensions);
}

TEST_F(RtpTransceiverTestForHeaderExtensions, AcceptsStoppedExtension) {
  auto modified_extensions = extensions_;
  modified_extensions[0].direction = RtpTransceiverDirection::kStopped;
  EXPECT_TRUE(
      transceiver_->SetHeaderExtensionsToNegotiate(modified_extensions).ok());
  EXPECT_EQ(transceiver_->GetHeaderExtensionsToNegotiate(),
            modified_extensions);
}

TEST_F(RtpTransceiverTestForHeaderExtensions, RejectsDifferentSize) {
  auto modified_extensions = extensions_;
  modified_extensions.pop_back();

  EXPECT_THAT(transceiver_->SetHeaderExtensionsToNegotiate(modified_extensions),
              Property(&RTCError::type, RTCErrorType::INVALID_MODIFICATION));
  EXPECT_EQ(transceiver_->GetHeaderExtensionsToNegotiate(), extensions_);
}

TEST_F(RtpTransceiverTestForHeaderExtensions, RejectsChangedUri) {
  auto modified_extensions = extensions_;
  ASSERT_TRUE(!modified_extensions.empty());
  modified_extensions[0].uri = "http://webrtc.org";

  EXPECT_THAT(transceiver_->SetHeaderExtensionsToNegotiate(modified_extensions),
              Property(&RTCError::type, RTCErrorType::INVALID_MODIFICATION));
  EXPECT_EQ(transceiver_->GetHeaderExtensionsToNegotiate(), extensions_);
}

TEST_F(RtpTransceiverTestForHeaderExtensions, RejectsReorder) {
  auto modified_extensions = extensions_;
  ASSERT_GE(modified_extensions.size(), 2u);
  std::swap(modified_extensions[0], modified_extensions[1]);

  EXPECT_THAT(transceiver_->SetHeaderExtensionsToNegotiate(modified_extensions),
              Property(&RTCError::type, RTCErrorType::INVALID_MODIFICATION));
  EXPECT_EQ(transceiver_->GetHeaderExtensionsToNegotiate(), extensions_);
}

TEST_F(RtpTransceiverTestForHeaderExtensions,
       RejectsStoppedMandatoryExtensions) {
  std::vector<RtpHeaderExtensionCapability> modified_extensions = extensions_;
  // Attempting to stop the mandatory MID extension.
  modified_extensions[2].direction = RtpTransceiverDirection::kStopped;
  EXPECT_THAT(transceiver_->SetHeaderExtensionsToNegotiate(modified_extensions),
              Property(&RTCError::type, RTCErrorType::INVALID_MODIFICATION));
  EXPECT_EQ(transceiver_->GetHeaderExtensionsToNegotiate(), extensions_);
}

TEST_F(RtpTransceiverTestForHeaderExtensions,
       NoNegotiatedHdrExtsWithoutChannel) {
  EXPECT_THAT(transceiver_->GetNegotiatedHeaderExtensions(),
              ElementsAre(Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped)));
}

TEST_F(RtpTransceiverTestForHeaderExtensions,
       NoNegotiatedHdrExtsWithChannelWithoutNegotiation) {
  const std::string content_name("my_mid");
  transceiver_->set_mid(content_name);
  auto mock_channel = std::make_unique<NiceMock<MockChannelInterface>>();
  // Raw ptr for updating expectations later since `mock_channel` will be moved
  // to `SetChannel`.
  EXPECT_CALL(*mock_channel, media_type())
      .WillRepeatedly(Return(MediaType::AUDIO));
  EXPECT_CALL(*mock_channel, mid()).WillRepeatedly(ReturnRef(content_name));
  EXPECT_CALL(*mock_channel, SetRtpTransport(_)).WillRepeatedly(Return(true));

  transceiver_->SetChannelForTest(std::move(mock_channel));
  EXPECT_THAT(transceiver_->GetNegotiatedHeaderExtensions(),
              ElementsAre(Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped)));

  ClearChannel();
}

TEST_F(RtpTransceiverTestForHeaderExtensions, ReturnsNegotiatedHdrExts) {
  const std::string content_name("my_mid");
  transceiver_->set_mid(content_name);
  auto mock_channel = std::make_unique<NiceMock<MockChannelInterface>>();
  EXPECT_CALL(*mock_channel, media_type())
      .WillRepeatedly(Return(MediaType::AUDIO));
  EXPECT_CALL(*mock_channel, voice_media_send_channel())
      .WillRepeatedly(Return(nullptr));
  EXPECT_CALL(*mock_channel, mid()).WillRepeatedly(ReturnRef(content_name));
  EXPECT_CALL(*mock_channel, SetRtpTransport(_)).WillRepeatedly(Return(true));

  RtpHeaderExtensions extensions = {
      RtpExtension("uri1", RtpHeaderExtensionId(1)),
      RtpExtension("uri2", RtpHeaderExtensionId(2))};
  AudioContentDescription description;
  description.set_rtp_header_extensions(extensions);
  transceiver_->OnNegotiationUpdate(SdpType::kAnswer, &description);

  transceiver_->SetChannelForTest(std::move(mock_channel));

  EXPECT_THAT(transceiver_->GetNegotiatedHeaderExtensions(),
              ElementsAre(Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kSendRecv),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kSendRecv),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped)));

  ClearChannel();
}

TEST_F(RtpTransceiverTestForHeaderExtensions,
       ReturnsNegotiatedHdrExtsOnPrAnswer) {
  const std::string content_name("my_mid");
  transceiver_->set_mid(content_name);
  auto mock_channel = std::make_unique<NiceMock<MockChannelInterface>>();
  EXPECT_CALL(*mock_channel, media_type())
      .WillRepeatedly(Return(MediaType::AUDIO));
  EXPECT_CALL(*mock_channel, voice_media_send_channel())
      .WillRepeatedly(Return(nullptr));
  EXPECT_CALL(*mock_channel, mid()).WillRepeatedly(ReturnRef(content_name));
  EXPECT_CALL(*mock_channel, SetRtpTransport(_)).WillRepeatedly(Return(true));

  RtpHeaderExtensions extensions = {
      RtpExtension("uri1", RtpHeaderExtensionId(1)),
      RtpExtension("uri2", RtpHeaderExtensionId(2))};
  AudioContentDescription description;
  description.set_rtp_header_extensions(extensions);
  transceiver_->OnNegotiationUpdate(SdpType::kPrAnswer, &description);

  transceiver_->SetChannelForTest(std::move(mock_channel));

  EXPECT_THAT(transceiver_->GetNegotiatedHeaderExtensions(),
              ElementsAre(Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kSendRecv),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kSendRecv),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped)));

  ClearChannel();
}

TEST_F(RtpTransceiverTestForHeaderExtensions,
       AnswerCanUseOtherHdrExtensionsThanPrAnswer) {
  const std::string content_name("my_mid");
  transceiver_->set_mid(content_name);
  auto mock_channel = std::make_unique<NiceMock<MockChannelInterface>>();

  EXPECT_CALL(*mock_channel, media_type())
      .WillRepeatedly(Return(MediaType::AUDIO));
  EXPECT_CALL(*mock_channel, voice_media_send_channel())
      .WillRepeatedly(Return(nullptr));
  EXPECT_CALL(*mock_channel, mid()).WillRepeatedly(ReturnRef(content_name));
  EXPECT_CALL(*mock_channel, SetRtpTransport(_)).WillRepeatedly(Return(true));

  transceiver_->SetChannelForTest(std::move(mock_channel));

  AudioContentDescription description_pr_answer;
  description_pr_answer.set_rtp_header_extensions(
      {RtpExtension("uri1", RtpHeaderExtensionId(1))});
  transceiver_->OnNegotiationUpdate(SdpType::kPrAnswer, &description_pr_answer);

  EXPECT_THAT(transceiver_->GetNegotiatedHeaderExtensions(),
              ElementsAre(Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kSendRecv),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped)));

  AudioContentDescription description_answer;
  description_answer.set_rtp_header_extensions(
      {RtpExtension("uri1", RtpHeaderExtensionId(1)),
       RtpExtension("uri2", RtpHeaderExtensionId(2))});
  transceiver_->OnNegotiationUpdate(SdpType::kAnswer, &description_answer);

  EXPECT_THAT(transceiver_->GetNegotiatedHeaderExtensions(),
              ElementsAre(Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kSendRecv),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kSendRecv),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped)));

  ClearChannel();
}

TEST_F(RtpTransceiverTestForHeaderExtensions,
       ReturnsNegotiatedHdrExtsSecondTime) {
  RtpHeaderExtensions extensions = {
      RtpExtension("uri1", RtpHeaderExtensionId(1)),
      RtpExtension("uri2", RtpHeaderExtensionId(2))};
  AudioContentDescription description;
  description.set_rtp_header_extensions(extensions);
  transceiver_->OnNegotiationUpdate(SdpType::kAnswer, &description);

  EXPECT_THAT(transceiver_->GetNegotiatedHeaderExtensions(),
              ElementsAre(Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kSendRecv),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kSendRecv),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped)));
  extensions = {RtpExtension("uri3", RtpHeaderExtensionId(4)),
                RtpExtension("uri5", RtpHeaderExtensionId(6))};
  description.set_rtp_header_extensions(extensions);
  transceiver_->OnNegotiationUpdate(SdpType::kAnswer, &description);

  EXPECT_THAT(transceiver_->GetNegotiatedHeaderExtensions(),
              ElementsAre(Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped)));
}

TEST_F(RtpTransceiverTestForHeaderExtensions,
       SimulcastOrSvcEnablesExtensionsByDefault) {
  std::vector<RtpHeaderExtensionCapability> extensions = {
      {RtpExtension::kDependencyDescriptorUri,
       RtpTransceiverDirection::kStopped},
      {RtpExtension::kVideoLayersAllocationUri,
       RtpTransceiverDirection::kStopped},
  };

  // Default is stopped.
  auto sender = MockSender(MediaType::VIDEO);
  auto receiver = MockReceiver(MediaType::VIDEO);
  auto transceiver = make_ref_counted<RtpTransceiver>(
      env(),
      RtpSenderProxyWithInternal<RtpSenderInternal>::Create(Thread::Current(),
                                                            sender),
      RtpReceiverProxyWithInternal<RtpReceiverInternal>::Create(
          Thread::Current(), Thread::Current(), receiver),
      context(), codec_lookup_helper(), extensions,
      /* on_negotiation_needed= */ [] {}, AudioOptions(), VideoOptions());
  ASSERT_EQ(sender->media_type(), MediaType::VIDEO);
  std::vector<RtpHeaderExtensionCapability> header_extensions =
      transceiver->GetHeaderExtensionsToNegotiate();
  ASSERT_EQ(header_extensions.size(), 2u);
  EXPECT_EQ(header_extensions[0].uri, RtpExtension::kDependencyDescriptorUri);
  EXPECT_EQ(header_extensions[0].direction, RtpTransceiverDirection::kStopped);
  EXPECT_EQ(header_extensions[1].uri, RtpExtension::kVideoLayersAllocationUri);
  EXPECT_EQ(header_extensions[1].direction, RtpTransceiverDirection::kStopped);

  // Simulcast, i.e. more than one encoding.
  RtpParameters simulcast_parameters;
  simulcast_parameters.encodings.resize(2);
  auto simulcast_sender = MockSender(MediaType::VIDEO);
  EXPECT_CALL(*simulcast_sender, GetParametersInternal(_, _))
      .WillRepeatedly(Return(simulcast_parameters));
  auto simulcast_transceiver = make_ref_counted<RtpTransceiver>(
      env(),
      RtpSenderProxyWithInternal<RtpSenderInternal>::Create(Thread::Current(),
                                                            simulcast_sender),
      RtpReceiverProxyWithInternal<RtpReceiverInternal>::Create(
          Thread::Current(), Thread::Current(), receiver),
      context(), codec_lookup_helper(), extensions,
      /* on_negotiation_needed= */ [] {}, AudioOptions(), VideoOptions());
  auto simulcast_extensions =
      simulcast_transceiver->GetHeaderExtensionsToNegotiate();
  ASSERT_EQ(simulcast_extensions.size(), 2u);
  EXPECT_EQ(simulcast_extensions[0].uri,
            RtpExtension::kDependencyDescriptorUri);
  EXPECT_EQ(simulcast_extensions[0].direction,
            RtpTransceiverDirection::kSendRecv);
  EXPECT_EQ(simulcast_extensions[1].uri,
            RtpExtension::kVideoLayersAllocationUri);
  EXPECT_EQ(simulcast_extensions[1].direction,
            RtpTransceiverDirection::kSendRecv);

  // SVC, a single encoding with a scalabilityMode other than L1T1.
  RtpParameters svc_parameters;
  svc_parameters.encodings.resize(1);
  svc_parameters.encodings[0].scalability_mode = "L3T3";

  auto svc_sender = MockSender(MediaType::VIDEO);
  EXPECT_CALL(*svc_sender, GetParametersInternal(_, _))
      .WillRepeatedly(Return(svc_parameters));
  auto svc_transceiver = make_ref_counted<RtpTransceiver>(
      env(),
      RtpSenderProxyWithInternal<RtpSenderInternal>::Create(Thread::Current(),
                                                            svc_sender),
      RtpReceiverProxyWithInternal<RtpReceiverInternal>::Create(
          Thread::Current(), Thread::Current(), receiver),
      context(), codec_lookup_helper(), extensions,
      /* on_negotiation_needed= */ [] {}, AudioOptions(), VideoOptions());
  std::vector<RtpHeaderExtensionCapability> svc_extensions =
      svc_transceiver->GetHeaderExtensionsToNegotiate();
  ASSERT_EQ(svc_extensions.size(), 2u);
  EXPECT_EQ(svc_extensions[0].uri, RtpExtension::kDependencyDescriptorUri);
  EXPECT_EQ(svc_extensions[0].direction, RtpTransceiverDirection::kSendRecv);
  EXPECT_EQ(svc_extensions[1].uri, RtpExtension::kVideoLayersAllocationUri);
  EXPECT_EQ(svc_extensions[1].direction, RtpTransceiverDirection::kSendRecv);
}

TEST_F(RtpTransceiverTestWithFakeCall,
       SetChannelAppliesAudioOptionsToSendChannel) {
  AudioOptions audio_options;
  audio_options.audio_network_adaptor = true;

  auto transceiver = CreateAudioTransceiverWithChannel(
      env(), call_.get(), context(), codec_lookup_helper(), audio_options);

  ASSERT_TRUE(transceiver->HasChannel());
  auto* voice_channel = transceiver->voice_media_send_channel();
  ASSERT_TRUE(voice_channel);
  auto* fake_channel = static_cast<FakeVoiceMediaSendChannel*>(voice_channel);
  EXPECT_TRUE(fake_channel->options().audio_network_adaptor);

  ScopedOperationsBatcher worker_tasks(context()->worker_thread());
  ScopedOperationsBatcher network_tasks(context()->network_thread());
  network_tasks.Add(transceiver->GetClearChannelNetworkTask());
  worker_tasks.Add(
      transceiver->GetDeleteChannelWorkerTask(/*stop_senders=*/true));
}

TEST_F(RtpTransceiverTestWithFakeCall, OnNetworkRouteChangedForwardsToChannel) {
  const std::string content_name("my_mid");

  auto fake_dtls =
      std::make_unique<FakeDtlsTransport>(env(), "test_transport", false);
  auto rtp_transport = std::make_unique<RtpTransport>(/*rtcp_mux_enabled=*/true,
                                                      env().field_trials());
  rtp_transport->SetRtpPacketTransport(fake_dtls.get());

  auto transceiver = CreateAudioTransceiverWithChannel(
      env(), call_.get(), context(), codec_lookup_helper(), AudioOptions(),
      [&]() -> RtpTransportInternal* { return rtp_transport.get(); });

  ASSERT_TRUE(transceiver->HasChannel());
  auto* voice_channel = transceiver->voice_media_send_channel();
  ASSERT_TRUE(voice_channel);
  auto* fake_channel = static_cast<FakeVoiceMediaSendChannel*>(voice_channel);

  NetworkRoute network_route;
  network_route.connected = true;
  network_route.local = RouteEndpoint::CreateWithNetworkId(1);
  network_route.remote = RouteEndpoint::CreateWithNetworkId(2);
  network_route.last_sent_packet_id = 100;
  network_route.packet_overhead = 28;

  // Trigger network route change on the transport on the network thread.
  context()->network_thread()->BlockingCall([&]() {
    fake_dtls->ice_transport()->NotifyNetworkRouteChanged(
        std::optional<NetworkRoute>(network_route));
  });

  // Verify that the channel received it.
  EXPECT_EQ(1, fake_channel->num_network_route_changes());
  EXPECT_TRUE(fake_channel->last_network_route().connected);
  EXPECT_EQ(1, fake_channel->last_network_route().local.network_id());
  EXPECT_EQ(2, fake_channel->last_network_route().remote.network_id());
  EXPECT_EQ(100, fake_channel->last_network_route().last_sent_packet_id);
  EXPECT_EQ(28, fake_channel->transport_overhead_per_packet());

  ScopedOperationsBatcher worker_tasks(context()->worker_thread());
  ScopedOperationsBatcher network_tasks(context()->network_thread());
  network_tasks.Add(transceiver->GetClearChannelNetworkTask());
  worker_tasks.Add(
      transceiver->GetDeleteChannelWorkerTask(/*stop_senders=*/true));
}

TEST_F(RtpTransceiverTestWithFakeCall,
       OnNetworkRouteChangedForwardsToVideoChannel) {
  const std::string content_name("my_mid");

  auto fake_dtls =
      std::make_unique<FakeDtlsTransport>(env(), "test_transport", false);
  auto rtp_transport = std::make_unique<RtpTransport>(/*rtcp_mux_enabled=*/true,
                                                      env().field_trials());
  rtp_transport->SetRtpPacketTransport(fake_dtls.get());

  auto transceiver = CreateVideoTransceiverWithChannel(
      env(), call_.get(), context(), codec_lookup_helper(), VideoOptions(),
      [&]() -> RtpTransportInternal* { return rtp_transport.get(); });

  ASSERT_TRUE(transceiver->HasChannel());
  auto* video_channel = transceiver->video_media_send_channel();
  ASSERT_TRUE(video_channel);
  auto* fake_channel = static_cast<FakeVideoMediaSendChannel*>(video_channel);

  NetworkRoute network_route;
  network_route.connected = true;
  network_route.local = RouteEndpoint::CreateWithNetworkId(1);
  network_route.remote = RouteEndpoint::CreateWithNetworkId(2);
  network_route.last_sent_packet_id = 100;
  network_route.packet_overhead = 28;

  // Trigger network route change on the transport on the network thread.
  context()->network_thread()->BlockingCall([&]() {
    fake_dtls->ice_transport()->NotifyNetworkRouteChanged(
        std::optional<NetworkRoute>(network_route));
  });

  // Verify that the channel received it.
  EXPECT_EQ(1, fake_channel->num_network_route_changes());
  EXPECT_TRUE(fake_channel->last_network_route().connected);
  EXPECT_EQ(1, fake_channel->last_network_route().local.network_id());
  EXPECT_EQ(2, fake_channel->last_network_route().remote.network_id());
  EXPECT_EQ(100, fake_channel->last_network_route().last_sent_packet_id);
  EXPECT_EQ(28, fake_channel->transport_overhead_per_packet());

  ScopedOperationsBatcher worker_tasks(context()->worker_thread());
  ScopedOperationsBatcher network_tasks(context()->network_thread());
  network_tasks.Add(transceiver->GetClearChannelNetworkTask());
  worker_tasks.Add(
      transceiver->GetDeleteChannelWorkerTask(/*stop_senders=*/true));
}

TEST_F(RtpTransceiverDoubleThreadTest,
       OnNetworkRouteChangedForwardsToChannel_DoubleThread) {
  const std::string content_name("my_mid");
  RTC_LOG(LS_INFO) << "TestBody current_thread=" << Thread::Current();
  RTC_LOG(LS_INFO) << "TestBody signaling_thread="
                   << context()->signaling_thread();
  RTC_LOG(LS_INFO) << "TestBody network_thread=" << context()->network_thread();

  std::unique_ptr<FakeDtlsTransport> fake_dtls;
  auto rtp_transport = std::make_unique<RtpTransport>(/*rtcp_mux_enabled=*/true,
                                                      env().field_trials());
  context()->network_thread()->BlockingCall([&]() {
    fake_dtls = std::make_unique<FakeDtlsTransport>(
        env(), "test_transport", 0, context()->network_thread());
    rtp_transport->SetRtpPacketTransport(fake_dtls.get());
  });

  auto call = std::make_unique<FakeCall>(env(), context()->worker_thread(),
                                         context()->network_thread());

  auto transceiver = CreateAudioTransceiverWithChannel(
      env(), call.get(), context(), codec_lookup_helper(), AudioOptions(),
      [&]() -> RtpTransportInternal* { return rtp_transport.get(); });

  ASSERT_TRUE(transceiver->HasChannel());
  auto* voice_channel = transceiver->voice_media_send_channel();
  ASSERT_TRUE(voice_channel);
  auto* fake_channel = static_cast<FakeVoiceMediaSendChannel*>(voice_channel);

  NetworkRoute network_route;
  network_route.connected = true;
  network_route.local = RouteEndpoint::CreateWithNetworkId(1);
  network_route.remote = RouteEndpoint::CreateWithNetworkId(2);
  network_route.last_sent_packet_id = 100;
  network_route.packet_overhead = 28;

  // Trigger network route change on the transport on the network thread.
  context()->network_thread()->BlockingCall([&]() {
    fake_dtls->ice_transport()->NotifyNetworkRouteChanged(
        std::optional<NetworkRoute>(network_route));
  });

  // Verify that the channel received it.
  int changes = context()->network_thread()->BlockingCall(
      [&]() { return fake_channel->num_network_route_changes(); });
  EXPECT_EQ(1, changes);

  NetworkRoute last_route = context()->network_thread()->BlockingCall(
      [&]() { return fake_channel->last_network_route(); });
  EXPECT_TRUE(last_route.connected);
  EXPECT_EQ(1, last_route.local.network_id());
  EXPECT_EQ(2, last_route.remote.network_id());
  EXPECT_EQ(100, last_route.last_sent_packet_id);

  int overhead = context()->network_thread()->BlockingCall(
      [&]() { return fake_channel->transport_overhead_per_packet(); });
  EXPECT_EQ(28, overhead);

  ScopedOperationsBatcher worker_tasks(context()->worker_thread());
  ScopedOperationsBatcher network_tasks(context()->network_thread());
  network_tasks.Add(transceiver->GetClearChannelNetworkTask());
  worker_tasks.Add(
      transceiver->GetDeleteChannelWorkerTask(/*stop_senders=*/true));

  RTC_CHECK(network_tasks.Run().ok());
  RTC_CHECK(worker_tasks.Run().ok());

  context()->network_thread()->BlockingCall([&]() { fake_dtls.reset(); });
}

// Sframe tests

TEST_F(RtpTransceiverUnifiedPlanTest, SframeEnabledIsNulloptByDefault) {
  scoped_refptr<RtpTransceiver> transceiver = CreateTransceiver(
      MockSender(MediaType::AUDIO), MockReceiver(MediaType::AUDIO));

  EXPECT_EQ(transceiver->SframeEnabled(), std::nullopt);
}

TEST_F(RtpTransceiverUnifiedPlanTest, TryToEnableSframeSetsValueToTrue) {
  scoped_refptr<RtpTransceiver> transceiver = CreateTransceiver(
      MockSender(MediaType::AUDIO), MockReceiver(MediaType::AUDIO));

  EXPECT_TRUE(transceiver->TryToEnableSframe().ok());
  EXPECT_THAT(transceiver->SframeEnabled(), Optional(true));
}

TEST_F(RtpTransceiverUnifiedPlanTest,
       TryToEnableSframeCanBeCalledMultipleTimes) {
  scoped_refptr<RtpTransceiver> transceiver = CreateTransceiver(
      MockSender(MediaType::AUDIO), MockReceiver(MediaType::AUDIO));

  EXPECT_TRUE(transceiver->TryToEnableSframe().ok());
  EXPECT_TRUE(transceiver->TryToEnableSframe().ok());
  EXPECT_THAT(transceiver->SframeEnabled(), Optional(true));
}

TEST_F(RtpTransceiverUnifiedPlanTest,
       TryToEnableSframeFailsAfterExplicitlySetToFalse) {
  scoped_refptr<RtpTransceiver> transceiver = CreateTransceiver(
      MockSender(MediaType::AUDIO), MockReceiver(MediaType::AUDIO));

  // Simulate the transceiver having been explicitly set to false (e.g. via
  // ApplySframeEnabled during SDP application).
  transceiver->ApplySframeEnabled(false);
  EXPECT_THAT(transceiver->SframeEnabled(), Optional(false));

  RTCError error = transceiver->TryToEnableSframe();
  EXPECT_FALSE(error.ok());
  EXPECT_EQ(error.type(), RTCErrorType::INVALID_MODIFICATION);
  EXPECT_THAT(transceiver->SframeEnabled(), Optional(false));
}

TEST_F(RtpTransceiverUnifiedPlanTest, ApplySframeEnabledTrueSetsState) {
  scoped_refptr<RtpTransceiver> transceiver = CreateTransceiver(
      MockSender(MediaType::AUDIO), MockReceiver(MediaType::AUDIO));

  transceiver->ApplySframeEnabled(true);
  EXPECT_THAT(transceiver->SframeEnabled(), Optional(true));
}

TEST_F(RtpTransceiverUnifiedPlanTest, ApplySframeEnabledFalseSetsState) {
  scoped_refptr<RtpTransceiver> transceiver = CreateTransceiver(
      MockSender(MediaType::AUDIO), MockReceiver(MediaType::AUDIO));

  transceiver->ApplySframeEnabled(false);
  EXPECT_THAT(transceiver->SframeEnabled(), Optional(false));
}

TEST_F(RtpTransceiverUnifiedPlanTest,
       ApplySframeEnabledTrueEnablesSframeOnChannel) {
  scoped_refptr<RtpTransceiver> transceiver = CreateTransceiver(
      MockSender(MediaType::AUDIO), MockReceiver(MediaType::AUDIO));
  const std::string content_name = "audio";
  transceiver->set_mid(content_name);

  auto mock_channel = std::make_unique<NiceMock<MockChannelInterface>>();
  ON_CALL(*mock_channel, media_type).WillByDefault(Return(MediaType::AUDIO));
  ON_CALL(*mock_channel, mid).WillByDefault(ReturnRef(content_name));
  ON_CALL(*mock_channel, SetRtpTransport).WillByDefault(Return(true));

  EXPECT_CALL(*mock_channel, EnableSframe);
  transceiver->SetChannelForTest(std::move(mock_channel));

  transceiver->ApplySframeEnabled(true);

  transceiver->ClearChannel();
}

TEST_F(RtpTransceiverUnifiedPlanTest,
       ApplySframeEnabledFalseDoesNotEnableSframeOnChannel) {
  scoped_refptr<RtpTransceiver> transceiver = CreateTransceiver(
      MockSender(MediaType::AUDIO), MockReceiver(MediaType::AUDIO));
  const std::string content_name = "audio";
  transceiver->set_mid(content_name);

  auto mock_channel = std::make_unique<NiceMock<MockChannelInterface>>();
  ON_CALL(*mock_channel, media_type).WillByDefault(Return(MediaType::AUDIO));
  ON_CALL(*mock_channel, mid).WillByDefault(ReturnRef(content_name));
  ON_CALL(*mock_channel, SetRtpTransport).WillByDefault(Return(true));

  EXPECT_CALL(*mock_channel, EnableSframe).Times(0);
  transceiver->SetChannelForTest(std::move(mock_channel));

  transceiver->ApplySframeEnabled(false);

  transceiver->ClearChannel();
}

}  // namespace

}  // namespace webrtc
