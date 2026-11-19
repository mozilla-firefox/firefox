/*
 *  Copyright 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/functional/any_invocable.h"
#include "api/audio_codecs/audio_encoder_factory.h"
#include "api/audio_codecs/audio_format.h"
#include "api/audio_options.h"
#include "api/call/bitrate_allocation.h"
#include "api/crypto/crypto_options.h"
#include "api/crypto/frame_decryptor_interface.h"
#include "api/crypto/frame_encryptor_interface.h"
#include "api/dtmf_sender_interface.h"
#include "api/encoded_audio_frame_injector_interface.h"
#include "api/encoded_video_frame_injector_interface.h"
#include "api/environment/environment.h"
#include "api/frame_transformer_interface.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/rtc_error.h"
#include "api/rtp_packet_infos.h"
#include "api/rtp_parameters.h"
#include "api/rtp_receiver_interface.h"
#include "api/scoped_refptr.h"
#include "api/sframe/sframe_decryptor_interface.h"
#include "api/sframe/sframe_encryptor_interface.h"
#include "api/sframe/sframe_types.h"
#include "api/task_queue/task_queue_base.h"
#include "api/test/fake_frame_decryptor.h"
#include "api/test/fake_frame_encryptor.h"
#include "api/test/mock_transformable_video_frame.h"
#include "api/test/rtc_error_matchers.h"
#include "api/units/data_rate.h"
#include "api/units/timestamp.h"
#include "api/video/builtin_video_bitrate_allocator_factory.h"
#include "api/video/encoded_image.h"
#include "api/video/video_bitrate_allocator_factory.h"
#include "api/video/video_codec_constants.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_frame_metadata.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_encoder.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "media/base/codec.h"
#include "media/base/fake_media_engine.h"
#include "media/base/media_channel.h"
#include "media/base/media_config.h"
#include "media/base/media_engine.h"
#include "media/base/rid_description.h"
#include "media/base/stream_params.h"
#include "media/base/test_utils.h"
#include "media/engine/fake_webrtc_call.h"
#include "modules/sframe/sframe_encryption_config.h"
#include "modules/sframe/sframe_media_encryptor_interface.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/dtls/dtls_transport_internal.h"
#include "p2p/dtls/fake_dtls_transport.h"
#include "pc/audio_rtp_receiver.h"
#include "pc/audio_track.h"
#include "pc/dtls_srtp_transport.h"
#include "pc/encoded_audio_frame_injector.h"
#include "pc/encoded_video_frame_injector.h"
#include "pc/local_audio_source.h"
#include "pc/media_stream.h"
#include "pc/rtp_receiver_proxy.h"
#include "pc/rtp_sender.h"
#include "pc/rtp_transport_internal.h"
#include "pc/scoped_operations_batcher.h"
#include "pc/test/fake_video_track_source.h"
#include "pc/video_rtp_receiver.h"
#include "pc/video_track.h"
#include "rtc_base/checks.h"  // IWYU pragma: keep
#include "rtc_base/task_queue_for_test.h"
#include "rtc_base/thread.h"
#include "rtc_base/unique_id_generator.h"
#include "test/create_test_environment.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/run_loop.h"
#include "test/wait_until.h"

namespace webrtc {

namespace {

constexpr char kStreamId1[] = "local_stream_1";
constexpr char kVideoTrackId[] = "video_1";
constexpr char kAudioTrackId[] = "audio_1";
constexpr uint32_t kVideoSsrc = 98;
constexpr uint32_t kVideoSsrc2 = 100;
constexpr uint32_t kAudioSsrc = 99;
constexpr uint32_t kAudioSsrc2 = 101;
constexpr uint32_t kVideoSsrcSimulcast = 102;
constexpr uint32_t kVideoSimulcastLayerCount = 2;

class MockSetStreamsObserver : public RtpSenderBase::SetStreamsObserver {
 public:
  MOCK_METHOD(void, OnSetStreams, (), (override));
};

}  // namespace


using ::testing::ContainerEq;
using ::testing::MockFunction;
using RidList = std::vector<std::string>;

class MockVideoMediaSendChannel : public FakeVideoMediaSendChannel {
 public:
  MockVideoMediaSendChannel(const VideoOptions& options,
                            TaskQueueBase* network_thread)
      : FakeVideoMediaSendChannel(options, network_thread) {}

  void SetFrameEncryptor(
      uint32_t ssrc,
      scoped_refptr<FrameEncryptorInterface> frame_encryptor) override {
    last_set_frame_encryptor_ = frame_encryptor;
  }

  bool SetEncoderFactoryOverride(
      uint32_t ssrc,
      std::unique_ptr<VideoEncoderFactory> encoder_factory) override {
    last_encoder_factory_override_ = std::move(encoder_factory);
    return true;
  }

  void ResetEncoderFactoryOverride(uint32_t ssrc) override {
    reset_encoder_factory_called_ = true;
    last_encoder_factory_override_ = nullptr;
  }

  void SetSframeEncryptionConfig(
      SframeEncryptionConfig sframe_encryption_config) override {
    last_set_sframe_encryption_config_ = std::move(sframe_encryption_config);
  }

  scoped_refptr<FrameEncryptorInterface> last_set_frame_encryptor_;
  std::unique_ptr<VideoEncoderFactory> last_encoder_factory_override_;
  bool reset_encoder_factory_called_ = false;
  std::optional<SframeEncryptionConfig> last_set_sframe_encryption_config_;
};

class MockVoiceMediaSendChannel : public FakeVoiceMediaSendChannel {
 public:
  MockVoiceMediaSendChannel(const AudioOptions& options,
                            TaskQueueBase* network_thread)
      : FakeVoiceMediaSendChannel(options, network_thread) {}

  bool SetEncoderFactoryOverride(
      uint32_t ssrc,
      scoped_refptr<AudioEncoderFactory> encoder_factory) override {
    last_encoder_factory_override_ = std::move(encoder_factory);
    return true;
  }

  void ResetEncoderFactoryOverride(uint32_t ssrc) override {
    reset_encoder_factory_called_ = true;
    last_encoder_factory_override_ = nullptr;
  }

  scoped_refptr<AudioEncoderFactory> last_encoder_factory_override_;
  bool reset_encoder_factory_called_ = false;
};

class RtpSenderReceiverTest
    : public ::testing::Test,
      public ::testing::WithParamInterface<std::pair<RidList, RidList>> {
 public:
  RtpSenderReceiverTest()
      : network_thread_(Thread::Create()),
        worker_thread_(Thread::Create()),
        video_bitrate_allocator_factory_(
            CreateBuiltinVideoBitrateAllocatorFactory()),
        // Create fake media engine/etc. so we can create channels to use to
        // test RtpSenders/RtpReceivers.
        media_engine_(std::make_unique<FakeMediaEngine>()),
        fake_call_(env_, worker_thread_.get(), network_thread_.get()),
        local_stream_(MediaStream::Create(kStreamId1)) {
    rtp_dtls_transport_ = std::make_unique<FakeDtlsTransport>(
        env_, "fake_dtls_transport", ICE_CANDIDATE_COMPONENT_RTP);
    rtp_transport_ = CreateDtlsSrtpTransport();

    // Create the channels, discard the result; we get them later.
    // Fake media channels are owned by the media engine.
    voice_media_send_channel_ = media_engine_->voice().CreateSendChannel(
        env_, &fake_call_, MediaConfig(), AudioOptions(), CryptoOptions());
    video_media_send_channel_ = media_engine_->video().CreateSendChannel(
        env_, &fake_call_, MediaConfig(), VideoOptions(), CryptoOptions(),
        video_bitrate_allocator_factory_.get(), nullptr, nullptr);
    voice_media_receive_channel_ = media_engine_->voice().CreateReceiveChannel(
        env_, &fake_call_, MediaConfig(), AudioOptions(), CryptoOptions(),
        nullptr, nullptr);
    video_media_receive_channel_ = media_engine_->video().CreateReceiveChannel(
        env_, &fake_call_, MediaConfig(), CryptoOptions(), nullptr,
        [](uint32_t, const RtpPacketInfos&, Timestamp) {});

    // Create streams for predefined SSRCs. Streams need to exist in order
    // for the senders and receievers to apply parameters to them.
    // Normally these would be created by SetLocalDescription and
    // SetRemoteDescription.
    voice_media_send_channel_->AddSendStream(
        StreamParams::CreateLegacy(kAudioSsrc));
    voice_media_receive_channel_->AddRecvStream(
        StreamParams::CreateLegacy(kAudioSsrc));
    voice_media_send_channel_->AddSendStream(
        StreamParams::CreateLegacy(kAudioSsrc2));
    voice_media_receive_channel_->AddRecvStream(
        StreamParams::CreateLegacy(kAudioSsrc2));
    video_media_send_channel_->AddSendStream(
        StreamParams::CreateLegacy(kVideoSsrc));
    video_media_receive_channel_->AddRecvStream(
        StreamParams::CreateLegacy(kVideoSsrc));
    video_media_send_channel_->AddSendStream(
        StreamParams::CreateLegacy(kVideoSsrc2));
    video_media_receive_channel_->AddRecvStream(
        StreamParams::CreateLegacy(kVideoSsrc2));

    network_thread_->Start();
    worker_thread_->Start();
  }

  ~RtpSenderReceiverTest() override {
    DestroyAudioRtpSender();
    DestroyVideoRtpSender();
    DestroyAudioRtpReceiver();
    DestroyVideoRtpReceiver();
    local_stream_ = nullptr;
    video_track_ = nullptr;
    audio_track_ = nullptr;
  }

  std::unique_ptr<RtpTransportInternal> CreateDtlsSrtpTransport() {
    auto dtls_srtp_transport = std::make_unique<DtlsSrtpTransport>(
        /*rtcp_mux_required=*/true, env_.field_trials());
    dtls_srtp_transport->SetDtlsTransports(rtp_dtls_transport_.get(),
                                           /*rtcp_dtls_transport=*/nullptr);
    return dtls_srtp_transport;
  }

  // Needed to use DTMF sender.
  void AddDtmfCodec() {
    AudioSenderParameter params;
    const Codec kTelephoneEventCodec =
        CreateAudioCodec(106, "telephone-event", 8000, 1);
    params.codecs.push_back(kTelephoneEventCodec);
    voice_media_send_channel()->SetSenderParameters(params);
  }

  scoped_refptr<VideoTrackInterface> CreateVideoTrack(bool is_screencast) {
    scoped_refptr<VideoTrackSourceInterface> source(
        FakeVideoTrackSource::Create(is_screencast));
    return VideoTrack::Create(kVideoTrackId, source, Thread::Current());
  }

  void AddVideoTrack() { AddVideoTrack(false); }

  void AddVideoTrack(bool is_screencast) {
    scoped_refptr<VideoTrackSourceInterface> source(
        FakeVideoTrackSource::Create(is_screencast));
    video_track_ = VideoTrack::Create(kVideoTrackId, source, Thread::Current());
    EXPECT_TRUE(local_stream_->AddTrack(video_track_));
  }

  void CreateAudioRtpSender() { CreateAudioRtpSender(nullptr); }

  void CreateAudioRtpSender(const scoped_refptr<LocalAudioSource>& source) {
    audio_track_ = AudioTrack::Create(kAudioTrackId, source);
    EXPECT_TRUE(local_stream_->AddTrack(audio_track_));
    std::unique_ptr<MockSetStreamsObserver> set_streams_observer =
        std::make_unique<MockSetStreamsObserver>();
    worker_thread_->BlockingCall([&]() {
      audio_rtp_sender_ = AudioRtpSender::Create(
          CreateTestEnvironment(), signaling_thread_, worker_thread_.get(),
          audio_track_->id(), nullptr, set_streams_observer.get(),
          /*enable_sframe_at_owner=*/nullptr, voice_media_send_channel_.get());
    });
    ASSERT_TRUE(audio_rtp_sender_->SetTrack(audio_track_.get()));
    EXPECT_CALL(*set_streams_observer, OnSetStreams());
    audio_rtp_sender_->SetStreams({local_stream_->id()});
    SetSsrc(kAudioSsrc, *audio_rtp_sender_);
    VerifyVoiceChannelInput();
  }

  void CreateAudioRtpSenderWithNoTrack() {
    worker_thread_->BlockingCall([&]() {
      audio_rtp_sender_ = AudioRtpSender::Create(
          CreateTestEnvironment(), signaling_thread_, worker_thread_.get(),
          /*id=*/"", nullptr, nullptr,
          /*enable_sframe_at_owner=*/nullptr, voice_media_send_channel_.get());
    });
  }

  void CreateVideoRtpSender(uint32_t ssrc) {
    CreateVideoRtpSender(false, ssrc);
  }

  void CreateVideoRtpSender() {
    CreateVideoRtpSender(/*is_screencast=*/false, kVideoSsrc);
  }

  StreamParams CreateSimulcastStreamParams(int num_layers) {
    std::vector<uint32_t> ssrcs;
    ssrcs.reserve(num_layers);
    for (int i = 0; i < num_layers; ++i) {
      ssrcs.push_back(kVideoSsrcSimulcast + i);
    }
    return CreateSimStreamParams("cname", ssrcs);
  }

  uint32_t CreateVideoRtpSender(const StreamParams& stream_params) {
    video_media_send_channel_->AddSendStream(stream_params);
    uint32_t primary_ssrc = stream_params.first_ssrc();
    CreateVideoRtpSender(primary_ssrc);
    return primary_ssrc;
  }

  uint32_t CreateVideoRtpSenderWithSimulcast(
      int num_layers = kVideoSimulcastLayerCount) {
    return CreateVideoRtpSender(CreateSimulcastStreamParams(num_layers));
  }

  uint32_t CreateVideoRtpSenderWithSimulcast(
      const std::vector<std::string>& rids) {
    StreamParams stream_params = CreateSimulcastStreamParams(rids.size());
    std::vector<RidDescription> rid_descriptions;
    absl::c_transform(rids, std::back_inserter(rid_descriptions),
                      [](const std::string& rid) {
                        return RidDescription(rid, RidDirection::kSend);
                      });
    stream_params.set_rids(rid_descriptions);
    return CreateVideoRtpSender(stream_params);
  }

  void CreateVideoRtpSender(bool is_screencast, uint32_t ssrc) {
    AddVideoTrack(is_screencast);
    std::unique_ptr<MockSetStreamsObserver> set_streams_observer =
        std::make_unique<MockSetStreamsObserver>();
    worker_thread_->BlockingCall([&]() {
      video_rtp_sender_ = VideoRtpSender::Create(
          CreateTestEnvironment(), signaling_thread_, worker_thread_.get(),
          video_track_->id(), set_streams_observer.get(),
          /*enable_sframe_at_owner=*/nullptr, video_media_send_channel(),
          /*init_send_encodings=*/{}, /*simulcast_rejected=*/false,
          /*initial_simulcast_layers=*/{});
    });
    ASSERT_TRUE(video_rtp_sender_->SetTrack(video_track_.get()));
    EXPECT_CALL(*set_streams_observer, OnSetStreams());
    video_rtp_sender_->SetStreams({local_stream_->id()});
    SetSsrc(ssrc, *video_rtp_sender_);
    VerifyVideoChannelInput(ssrc);
  }
  void CreateVideoRtpSenderWithNoTrack() {
    worker_thread_->BlockingCall([&]() {
      video_rtp_sender_ = VideoRtpSender::Create(
          CreateTestEnvironment(), signaling_thread_, worker_thread_.get(),
          /*id=*/"", nullptr,
          /*enable_sframe_at_owner=*/nullptr, video_media_send_channel(),
          /*init_send_encodings=*/{}, /*simulcast_rejected=*/false,
          /*initial_simulcast_layers=*/{});
    });
  }

  // Creates a sender that accepts Sframe and has no track, leaving the send
  // SSRC unassigned until SetSsrc() runs. Empty encodings keep SetSsrc() out of
  // the parameter path, which expects a send stream on the media channel.
  void CreateVideoRtpSenderWithSframe() {
    video_rtp_sender_ = VideoRtpSender::Create(
        CreateTestEnvironment(), signaling_thread_, worker_thread_.get(),
        /*id=*/"", /*set_streams_observer=*/nullptr,
        []() -> RTCError { return RTCError::OK(); }, video_media_send_channel(),
        /*init_send_encodings=*/{}, /*simulcast_rejected=*/false,
        /*initial_simulcast_layers=*/{});
    video_rtp_sender_->set_init_send_encodings({});
  }

  void DestroyAudioRtpSender() {
    if (!audio_rtp_sender_)
      return;
    audio_rtp_sender_->Stop();
    audio_rtp_sender_ = nullptr;
    VerifyVoiceChannelNoInput();
  }

  void DestroyVideoRtpSender() {
    if (!video_rtp_sender_)
      return;
    video_rtp_sender_->Stop();
    video_rtp_sender_ = nullptr;
    VerifyVideoChannelNoInput();
  }

  void CreateAudioRtpReceiver(
      std::vector<scoped_refptr<MediaStreamInterface>> streams = {}) {
    audio_rtp_receiver_ = make_ref_counted<AudioRtpReceiver>(
        worker_thread_.get(), kAudioTrackId, streams);
    worker_thread_->BlockingCall([this] {
      audio_rtp_receiver_->SetMediaChannel(voice_media_receive_channel());
    });
    auto setup_task = audio_rtp_receiver_->GetSetupForMediaChannel(kAudioSsrc);
    worker_thread_->BlockingCall(
        [task = std::move(setup_task)]() mutable { std::move(task)(); });
    audio_track_ = audio_rtp_receiver_->audio_track();
    VerifyVoiceChannelOutput();
  }

  void CreateVideoRtpReceiver(
      std::vector<scoped_refptr<MediaStreamInterface>> streams = {}) {
    video_rtp_receiver_ = make_ref_counted<VideoRtpReceiver>(
        worker_thread_.get(), kVideoTrackId, streams,
        /*enable_sframe_at_owner=*/nullptr);
    worker_thread_->BlockingCall([this] {
      video_rtp_receiver_->SetMediaChannel(video_media_receive_channel());
    });
    auto setup_task = video_rtp_receiver_->GetSetupForMediaChannel(kVideoSsrc);
    worker_thread_->BlockingCall(
        [task = std::move(setup_task)]() mutable { std::move(task)(); });
    video_track_ = video_rtp_receiver_->video_track();
    VerifyVideoChannelOutput();
  }

  void CreateVideoRtpReceiverWithSimulcast(
      std::vector<scoped_refptr<MediaStreamInterface>> streams = {},
      int num_layers = kVideoSimulcastLayerCount) {
    std::vector<uint32_t> ssrcs;
    ssrcs.reserve(num_layers);
    for (int i = 0; i < num_layers; ++i)
      ssrcs.push_back(kVideoSsrcSimulcast + i);
    StreamParams stream_params = CreateSimStreamParams("cname", ssrcs);
    video_media_receive_channel_->AddRecvStream(stream_params);
    uint32_t primary_ssrc = stream_params.first_ssrc();

    video_rtp_receiver_ = make_ref_counted<VideoRtpReceiver>(
        worker_thread_.get(), kVideoTrackId, streams,
        /*enable_sframe_at_owner=*/nullptr);
    worker_thread_->BlockingCall([this] {
      video_rtp_receiver_->SetMediaChannel(video_media_receive_channel());
    });
    auto setup_task =
        video_rtp_receiver_->GetSetupForMediaChannel(primary_ssrc);
    worker_thread_->BlockingCall(
        [task = std::move(setup_task)]() mutable { std::move(task)(); });
    video_track_ = video_rtp_receiver_->video_track();
  }

  void DestroyAudioRtpReceiver() {
    if (!audio_rtp_receiver_)
      return;
    worker_thread_->BlockingCall(
        [this] { audio_rtp_receiver_->SetMediaChannel(nullptr); });
    audio_rtp_receiver_ = nullptr;
    VerifyVoiceChannelNoOutput();
  }

  void DestroyVideoRtpReceiver() {
    if (!video_rtp_receiver_)
      return;
    video_rtp_receiver_->Stop();
    worker_thread_->BlockingCall(
        [this] { video_rtp_receiver_->SetMediaChannel(nullptr); });
    video_rtp_receiver_ = nullptr;
    VerifyVideoChannelNoOutput();
  }

  void FlushWorker() {
    RTC_DCHECK_EQ(TaskQueueBase::Current(), run_loop_.task_queue());
    worker_thread_->PostTask([this, signaling_thread = run_loop_.task_queue()] {
      signaling_thread->PostTask([this] { run_loop_.Quit(); });
    });
    run_loop_.Run();
  }

  void VerifyVoiceChannelInput() { VerifyVoiceChannelInput(kAudioSsrc); }

  void VerifyVoiceChannelInput(uint32_t ssrc) {
    // Verify that the media channel has an audio source, and the stream isn't
    // muted.
    EXPECT_TRUE(voice_media_send_channel()->HasSource(ssrc));
    EXPECT_FALSE(voice_media_send_channel()->IsStreamMuted(ssrc));
  }

  void VerifyVideoChannelInput() { VerifyVideoChannelInput(kVideoSsrc); }

  void VerifyVideoChannelInput(uint32_t ssrc) {
    // Verify that the media channel has a video source,
    EXPECT_TRUE(video_media_send_channel()->HasSource(ssrc));
  }

  void VerifyVoiceChannelNoInput() { VerifyVoiceChannelNoInput(kAudioSsrc); }

  void VerifyVoiceChannelNoInput(uint32_t ssrc) {
    // Verify that the media channel's source is reset.
    EXPECT_FALSE(voice_media_receive_channel()->HasSource(ssrc));
  }

  void VerifyVideoChannelNoInput() { VerifyVideoChannelNoInput(kVideoSsrc); }

  void VerifyVideoChannelNoInput(uint32_t ssrc) {
    // Verify that the media channel's source is reset.
    EXPECT_FALSE(video_media_receive_channel()->HasSource(ssrc));
  }

  void VerifyVoiceChannelOutput() {
    // Verify that the volume is initialized to 1.
    double volume;
    EXPECT_TRUE(
        voice_media_receive_channel()->GetOutputVolume(kAudioSsrc, &volume));
    EXPECT_EQ(1, volume);
  }

  void VerifyVideoChannelOutput() {
    // Verify that the media channel has a sink.
    EXPECT_TRUE(video_media_receive_channel()->HasSink(kVideoSsrc));
  }

  void VerifyVoiceChannelNoOutput() {
    // Verify that the volume is reset to 0.
    double volume;
    EXPECT_TRUE(
        voice_media_receive_channel()->GetOutputVolume(kAudioSsrc, &volume));
    EXPECT_EQ(0, volume);
  }

  void VerifyVideoChannelNoOutput() {
    // Verify that the media channel's sink is reset.
    EXPECT_FALSE(video_media_receive_channel()->HasSink(kVideoSsrc));
  }

  // Verifies that the encoding layers contain the specified RIDs.
  bool VerifyEncodingLayers(const VideoRtpSender& sender,
                            const std::vector<std::string>& rids) {
    bool has_failure = HasFailure();
    RtpParameters parameters = sender.GetParameters();
    std::vector<std::string> encoding_rids;
    absl::c_transform(
        parameters.encodings, std::back_inserter(encoding_rids),
        [](const RtpEncodingParameters& encoding) { return encoding.rid; });
    EXPECT_THAT(rids, ContainerEq(encoding_rids));
    return has_failure || !HasFailure();
  }

  // Runs a test for disabling the encoding layers on the specified sender.
  void RunDisableEncodingLayersTest(
      const std::vector<std::string>& all_layers,
      const std::vector<std::string>& disabled_layers,
      VideoRtpSender* sender) {
    std::vector<std::string> expected;
    absl::c_copy_if(all_layers, std::back_inserter(expected),
                    [&disabled_layers](const std::string& rid) {
                      return !absl::c_linear_search(disabled_layers, rid);
                    });

    EXPECT_TRUE(VerifyEncodingLayers(*sender, all_layers));
    sender->DisableEncodingLayers(disabled_layers);
    EXPECT_TRUE(VerifyEncodingLayers(*sender, expected));
  }

  // Runs a test for setting an encoding layer as inactive.
  // This test assumes that some layers have already been disabled.
  void RunSetLastLayerAsInactiveTest(VideoRtpSender* sender) {
    auto parameters = sender->GetParameters();
    if (parameters.encodings.empty()) {
      return;
    }

    RtpEncodingParameters& encoding = parameters.encodings.back();
    auto rid = encoding.rid;
    EXPECT_TRUE(encoding.active);
    encoding.active = false;
    auto error = sender->SetParameters(parameters);
    ASSERT_TRUE(error.ok());
    parameters = sender->GetParameters();
    RtpEncodingParameters& result_encoding = parameters.encodings.back();
    EXPECT_EQ(rid, result_encoding.rid);
    EXPECT_FALSE(result_encoding.active);
  }

  // Runs a test for disabling the encoding layers on a sender without a media
  // channel.
  void RunDisableSimulcastLayersWithoutMediaEngineTest(
      const std::vector<std::string>& all_layers,
      const std::vector<std::string>& disabled_layers) {
    auto sender = VideoRtpSender::Create(
        CreateTestEnvironment(), signaling_thread_, worker_thread_.get(), "1",
        nullptr, /*enable_sframe_at_owner=*/nullptr, nullptr,
        /*init_send_encodings=*/{}, /*simulcast_rejected=*/false,
        /*initial_simulcast_layers=*/{});
    RtpParameters parameters;
    parameters.encodings.resize(all_layers.size());
    for (size_t i = 0; i < all_layers.size(); ++i) {
      parameters.encodings[i].rid = all_layers[i];
    }
    sender->set_init_send_encodings(parameters.encodings);
    RunDisableEncodingLayersTest(all_layers, disabled_layers, sender.get());
    RunSetLastLayerAsInactiveTest(sender.get());
  }

  // Runs a test for disabling the encoding layers on a sender with a media
  // channel.
  void RunDisableSimulcastLayersWithMediaEngineTest(
      const std::vector<std::string>& all_layers,
      const std::vector<std::string>& disabled_layers) {
    uint32_t ssrc = CreateVideoRtpSenderWithSimulcast(all_layers);
    RunDisableEncodingLayersTest(all_layers, disabled_layers,
                                 video_rtp_sender_.get());

    auto channel_parameters =
        video_media_send_channel_->GetRtpSendParameters(ssrc);
    ASSERT_EQ(channel_parameters.encodings.size(), all_layers.size());
    for (size_t i = 0; i < all_layers.size(); ++i) {
      EXPECT_EQ(all_layers[i], channel_parameters.encodings[i].rid);
      bool is_active = !absl::c_linear_search(disabled_layers, all_layers[i]);
      EXPECT_EQ(is_active, channel_parameters.encodings[i].active);
    }

    RunSetLastLayerAsInactiveTest(video_rtp_sender_.get());
  }

  // Check that minimum Jitter Buffer delay is propagated to the underlying
  // `media_channel`.
  void VerifyRtpReceiverDelayBehaviour(
      MediaReceiveChannelInterface* media_channel,
      RtpReceiverInterface* receiver,
      uint32_t ssrc) {
    worker_thread_->BlockingCall(
        [&] { receiver->SetJitterBufferMinimumDelay(/*delay_seconds=*/0.5); });
    std::optional<int> delay_ms =
        media_channel->GetBaseMinimumPlayoutDelayMs(ssrc);  // In milliseconds.
    EXPECT_DOUBLE_EQ(0.5, delay_ms.value_or(0) / 1000.0);
  }

 protected:
  FakeVideoMediaSendChannel* video_media_send_channel() {
    return static_cast<FakeVideoMediaSendChannel*>(
        video_media_send_channel_.get());
  }
  FakeVoiceMediaSendChannel* voice_media_send_channel() {
    return static_cast<FakeVoiceMediaSendChannel*>(
        voice_media_send_channel_.get());
  }
  FakeVideoMediaReceiveChannel* video_media_receive_channel() {
    return static_cast<FakeVideoMediaReceiveChannel*>(
        video_media_receive_channel_.get());
  }
  FakeVoiceMediaReceiveChannel* voice_media_receive_channel() {
    return static_cast<FakeVoiceMediaReceiveChannel*>(
        voice_media_receive_channel_.get());
  }

  // `layer_count` is the number of send layers the local description would
  // describe for the stream; the default covers the single layer case.
  void SetSsrc(uint32_t ssrc,
               RtpSenderInternal& sender,
               size_t layer_count = 1) {
    ScopedOperationsBatcher worker_tasks(worker_thread_.get());
    worker_tasks.AddWithFinalizer(sender.SetSsrcTask(ssrc, layer_count));
  }

  // Expects `sender` to return the parameters that `media_channel` reports for
  // `ssrc`, from its cache, i.e. without a blocking call.
  void ExpectParametersFromCache(const RtpSenderInternal& sender,
                                 MediaSendChannelInterface* media_channel,
                                 uint32_t ssrc) {
    RtpParameters parameters;
    {
      RTC_DCHECK_DISALLOW_THREAD_BLOCKING_CALLS();
      parameters = sender.GetParametersInternal(/*with_all_layers=*/false);
    }
    EXPECT_EQ(parameters, worker_thread_->BlockingCall([&] {
      return media_channel->GetRtpSendParameters(ssrc);
    }));
  }

  test::RunLoop run_loop_;
  // Initialize `signaling_thread_` to point to the current thread.
  // This is the internal thread owned by `run_loop_`.
  Thread* const signaling_thread_ = Thread::Current();
  const std::unique_ptr<Thread> network_thread_;
  const std::unique_ptr<Thread> worker_thread_;
  const Environment env_ = CreateTestEnvironment();
  // The `rtp_dtls_transport_` and `rtp_transport_` should be destroyed after
  // the `channel_manager`.
  std::unique_ptr<DtlsTransportInternal> rtp_dtls_transport_;
  std::unique_ptr<RtpTransportInternal> rtp_transport_;
  std::unique_ptr<VideoBitrateAllocatorFactory>
      video_bitrate_allocator_factory_;
  std::unique_ptr<FakeMediaEngine> media_engine_;
  UniqueRandomIdGenerator ssrc_generator_;
  FakeCall fake_call_;
  std::unique_ptr<VoiceMediaSendChannelInterface> voice_media_send_channel_;
  std::unique_ptr<VideoMediaSendChannelInterface> video_media_send_channel_;
  std::unique_ptr<VoiceMediaReceiveChannelInterface>
      voice_media_receive_channel_;
  std::unique_ptr<VideoMediaReceiveChannelInterface>
      video_media_receive_channel_;
  scoped_refptr<AudioRtpSender> audio_rtp_sender_;
  scoped_refptr<VideoRtpSender> video_rtp_sender_;
  scoped_refptr<AudioRtpReceiver> audio_rtp_receiver_;
  scoped_refptr<VideoRtpReceiver> video_rtp_receiver_;
  scoped_refptr<MediaStreamInterface> local_stream_;
  scoped_refptr<VideoTrackInterface> video_track_;
  scoped_refptr<AudioTrackInterface> audio_track_;
};

// Test that `voice_channel_` is updated when an audio track is associated
// and disassociated with an AudioRtpSender.
TEST_F(RtpSenderReceiverTest, AddAndDestroyAudioRtpSender) {
  CreateAudioRtpSender();
  DestroyAudioRtpSender();
}

// Test that `video_channel_` is updated when a video track is associated and
// disassociated with a VideoRtpSender.
TEST_F(RtpSenderReceiverTest, AddAndDestroyVideoRtpSender) {
  CreateVideoRtpSender();
  DestroyVideoRtpSender();
}

// Test that `voice_channel_` is updated when a remote audio track is
// associated and disassociated with an AudioRtpReceiver.
TEST_F(RtpSenderReceiverTest, AddAndDestroyAudioRtpReceiver) {
  CreateAudioRtpReceiver();
  DestroyAudioRtpReceiver();
}

// Test that `video_channel_` is updated when a remote video track is
// associated and disassociated with a VideoRtpReceiver.
TEST_F(RtpSenderReceiverTest, AddAndDestroyVideoRtpReceiver) {
  CreateVideoRtpReceiver();
  DestroyVideoRtpReceiver();
}

TEST_F(RtpSenderReceiverTest, AddAndDestroyAudioRtpReceiverWithStreams) {
  CreateAudioRtpReceiver({local_stream_});
  DestroyAudioRtpReceiver();
}

TEST_F(RtpSenderReceiverTest, AddAndDestroyVideoRtpReceiverWithStreams) {
  CreateVideoRtpReceiver({local_stream_});
  DestroyVideoRtpReceiver();
}

// Test that the AudioRtpSender applies options from the local audio source.
TEST_F(RtpSenderReceiverTest, LocalAudioSourceOptionsApplied) {
  AudioOptions options;
  options.echo_cancellation = true;
  auto source = LocalAudioSource::Create(&options);
  CreateAudioRtpSender(source);

  EXPECT_EQ(true, voice_media_send_channel()->options().echo_cancellation);

  DestroyAudioRtpSender();
}

// Test that the stream is muted when the track is disabled, and unmuted when
// the track is enabled.
TEST_F(RtpSenderReceiverTest, LocalAudioTrackDisable) {
  CreateAudioRtpSender();

  audio_track_->set_enabled(false);
  EXPECT_TRUE(voice_media_send_channel()->IsStreamMuted(kAudioSsrc));

  audio_track_->set_enabled(true);
  EXPECT_FALSE(voice_media_send_channel()->IsStreamMuted(kAudioSsrc));

  DestroyAudioRtpSender();
}

// Test that the volume is set to 0 when the track is disabled, and back to
// 1 when the track is enabled.
TEST_F(RtpSenderReceiverTest, RemoteAudioTrackDisable) {
  CreateAudioRtpReceiver();

  double volume;
  EXPECT_TRUE(
      voice_media_receive_channel()->GetOutputVolume(kAudioSsrc, &volume));
  EXPECT_EQ(1, volume);

  // Handling of enable/disable is applied asynchronously.
  audio_track_->set_enabled(false);
  FlushWorker();  // Wait for volume change.

  EXPECT_TRUE(
      voice_media_receive_channel()->GetOutputVolume(kAudioSsrc, &volume));
  EXPECT_EQ(0, volume);

  audio_track_->set_enabled(true);
  FlushWorker();  // Wait for volume change.

  EXPECT_TRUE(
      voice_media_receive_channel()->GetOutputVolume(kAudioSsrc, &volume));
  EXPECT_EQ(1, volume);

  DestroyAudioRtpReceiver();
}

// Currently no action is taken when a remote video track is disabled or
// enabled, so there's nothing to test here, other than what is normally
// verified in DestroyVideoRtpSender.
TEST_F(RtpSenderReceiverTest, LocalVideoTrackDisable) {
  CreateVideoRtpSender();

  video_track_->set_enabled(false);
  video_track_->set_enabled(true);

  DestroyVideoRtpSender();
}

// Test that the state of the video track created by the VideoRtpReceiver is
// updated when the receiver is destroyed.
TEST_F(RtpSenderReceiverTest, RemoteVideoTrackState) {
  CreateVideoRtpReceiver();

  EXPECT_EQ(MediaStreamTrackInterface::kLive, video_track_->state());
  EXPECT_EQ(MediaSourceInterface::kLive, video_track_->GetSource()->state());

  DestroyVideoRtpReceiver();

  EXPECT_EQ(MediaStreamTrackInterface::kEnded, video_track_->state());
  EXPECT_EQ(MediaSourceInterface::kEnded, video_track_->GetSource()->state());
  DestroyVideoRtpReceiver();
}

// Currently no action is taken when a remote video track is disabled or
// enabled, so there's nothing to test here, other than what is normally
// verified in DestroyVideoRtpReceiver.
TEST_F(RtpSenderReceiverTest, RemoteVideoTrackDisable) {
  CreateVideoRtpReceiver();

  video_track_->set_enabled(false);
  video_track_->set_enabled(true);

  DestroyVideoRtpReceiver();
}

// Test that the AudioRtpReceiver applies volume changes from the track source
// to the media channel.
TEST_F(RtpSenderReceiverTest, RemoteAudioTrackSetVolume) {
  CreateAudioRtpReceiver();

  double volume;
  audio_track_->GetSource()->SetVolume(0.5);
  // Wait for the worker thread to apply the volume change.
  FlushWorker();

  EXPECT_TRUE(
      voice_media_receive_channel()->GetOutputVolume(kAudioSsrc, &volume));
  EXPECT_EQ(0.5, volume);

  // Disable the audio track, this should prevent setting the volume.
  audio_track_->set_enabled(false);
  audio_track_->GetSource()->SetVolume(0.8);
  FlushWorker();  // Wait for the the volume change.
  EXPECT_TRUE(
      voice_media_receive_channel()->GetOutputVolume(kAudioSsrc, &volume));
  EXPECT_EQ(0, volume);

  // When the track is enabled, the previously set volume should take effect.
  audio_track_->set_enabled(true);
  FlushWorker();  // The volume is applied asynchronously.

  EXPECT_TRUE(
      voice_media_receive_channel()->GetOutputVolume(kAudioSsrc, &volume));
  EXPECT_EQ(0.8, volume);

  // Try changing volume one more time.
  audio_track_->GetSource()->SetVolume(0.9);
  FlushWorker();

  EXPECT_TRUE(
      voice_media_receive_channel()->GetOutputVolume(kAudioSsrc, &volume));
  EXPECT_EQ(0.9, volume);

  DestroyAudioRtpReceiver();
}

TEST_F(RtpSenderReceiverTest, AudioRtpReceiverDelay) {
  CreateAudioRtpReceiver();
  VerifyRtpReceiverDelayBehaviour(
      voice_media_receive_channel()->AsVoiceReceiveChannel(),
      audio_rtp_receiver_.get(), kAudioSsrc);
  DestroyAudioRtpReceiver();
}

TEST_F(RtpSenderReceiverTest, VideoRtpReceiverDelay) {
  CreateVideoRtpReceiver();
  VerifyRtpReceiverDelayBehaviour(
      video_media_receive_channel()->AsVideoReceiveChannel(),
      video_rtp_receiver_.get(), kVideoSsrc);
  DestroyVideoRtpReceiver();
}

// Test that the media channel isn't enabled for sending if the audio sender
// doesn't have both a track and SSRC.
TEST_F(RtpSenderReceiverTest, AudioSenderWithoutTrackAndSsrc) {
  CreateAudioRtpSenderWithNoTrack();
  scoped_refptr<AudioTrackInterface> track =
      AudioTrack::Create(kAudioTrackId, nullptr);

  // Track but no SSRC.
  EXPECT_TRUE(audio_rtp_sender_->SetTrack(track.get()));
  VerifyVoiceChannelNoInput();

  // SSRC but no track.
  EXPECT_TRUE(audio_rtp_sender_->SetTrack(nullptr));
  SetSsrc(kAudioSsrc, *audio_rtp_sender_);
  VerifyVoiceChannelNoInput();
}

// Test that the media channel isn't enabled for sending if the video sender
// doesn't have both a track and SSRC.
TEST_F(RtpSenderReceiverTest, VideoSenderWithoutTrackAndSsrc) {
  CreateVideoRtpSenderWithNoTrack();

  // Track but no SSRC.
  EXPECT_TRUE(video_rtp_sender_->SetTrack(video_track_.get()));
  VerifyVideoChannelNoInput();

  // SSRC but no track.
  EXPECT_TRUE(video_rtp_sender_->SetTrack(nullptr));
  SetSsrc(kVideoSsrc, *video_rtp_sender_);
  VerifyVideoChannelNoInput();
}

// Test that the media channel is enabled for sending when the audio sender
// has a track and SSRC, when the SSRC is set first.
TEST_F(RtpSenderReceiverTest, AudioSenderEarlyWarmupSsrcThenTrack) {
  CreateAudioRtpSenderWithNoTrack();
  scoped_refptr<AudioTrackInterface> track =
      AudioTrack::Create(kAudioTrackId, nullptr);
  SetSsrc(kAudioSsrc, *audio_rtp_sender_);
  audio_rtp_sender_->SetTrack(track.get());
  VerifyVoiceChannelInput();

  DestroyAudioRtpSender();
}

// Test that the media channel is enabled for sending when the audio sender
// has a track and SSRC, when the SSRC is set last.
TEST_F(RtpSenderReceiverTest, AudioSenderEarlyWarmupTrackThenSsrc) {
  CreateAudioRtpSenderWithNoTrack();
  scoped_refptr<AudioTrackInterface> track =
      AudioTrack::Create(kAudioTrackId, nullptr);
  audio_rtp_sender_->SetTrack(track.get());
  SetSsrc(kAudioSsrc, *audio_rtp_sender_);
  VerifyVoiceChannelInput();

  DestroyAudioRtpSender();
}

// Test that the media channel is enabled for sending when the video sender
// has a track and SSRC, when the SSRC is set first.
TEST_F(RtpSenderReceiverTest, VideoSenderEarlyWarmupSsrcThenTrack) {
  AddVideoTrack();
  CreateVideoRtpSenderWithNoTrack();
  SetSsrc(kVideoSsrc, *video_rtp_sender_);
  video_rtp_sender_->SetTrack(video_track_.get());
  VerifyVideoChannelInput();

  DestroyVideoRtpSender();
}

// Test that the media channel is enabled for sending when the video sender
// has a track and SSRC, when the SSRC is set last.
TEST_F(RtpSenderReceiverTest, VideoSenderEarlyWarmupTrackThenSsrc) {
  AddVideoTrack();
  CreateVideoRtpSenderWithNoTrack();
  video_rtp_sender_->SetTrack(video_track_.get());
  SetSsrc(kVideoSsrc, *video_rtp_sender_);
  VerifyVideoChannelInput();

  DestroyVideoRtpSender();
}

// Test that the media channel stops sending when the audio sender's SSRC is set
// to 0.
TEST_F(RtpSenderReceiverTest, AudioSenderSsrcSetToZero) {
  CreateAudioRtpSender();

  SetSsrc(0, *audio_rtp_sender_);
  VerifyVoiceChannelNoInput();
}

// Test that the media channel stops sending when the video sender's SSRC is set
// to 0.
TEST_F(RtpSenderReceiverTest, VideoSenderSsrcSetToZero) {
  CreateAudioRtpSender();

  SetSsrc(0, *audio_rtp_sender_);
  VerifyVideoChannelNoInput();
}

// Test that the media channel stops sending when the audio sender's track is
// set to null.
TEST_F(RtpSenderReceiverTest, AudioSenderTrackSetToNull) {
  CreateAudioRtpSender();

  EXPECT_TRUE(audio_rtp_sender_->SetTrack(nullptr));
  VerifyVoiceChannelNoInput();
}

// Test that the media channel stops sending when the video sender's track is
// set to null.
TEST_F(RtpSenderReceiverTest, VideoSenderTrackSetToNull) {
  CreateVideoRtpSender();

  SetSsrc(0, *video_rtp_sender_);
  VerifyVideoChannelNoInput();
}

// Test that when the audio sender's SSRC is changed, the media channel stops
// sending with the old SSRC and starts sending with the new one.
TEST_F(RtpSenderReceiverTest, AudioSenderSsrcChanged) {
  CreateAudioRtpSender();

  SetSsrc(kAudioSsrc2, *audio_rtp_sender_);
  VerifyVoiceChannelNoInput(kAudioSsrc);
  VerifyVoiceChannelInput(kAudioSsrc2);

  DestroyAudioRtpSender();
  VerifyVoiceChannelNoInput(kAudioSsrc2);
}

// Test that when the audio sender's SSRC is changed, the media channel stops
// sending with the old SSRC and starts sending with the new one.
TEST_F(RtpSenderReceiverTest, VideoSenderSsrcChanged) {
  CreateVideoRtpSender();

  SetSsrc(kVideoSsrc2, *video_rtp_sender_);
  VerifyVideoChannelNoInput(kVideoSsrc);
  VerifyVideoChannelInput(kVideoSsrc2);

  DestroyVideoRtpSender();
  VerifyVideoChannelNoInput(kVideoSsrc2);
}

TEST_F(RtpSenderReceiverTest, AudioSenderCanSetParameters) {
  CreateAudioRtpSender();

  RtpParameters params = audio_rtp_sender_->GetParameters();
  EXPECT_EQ(1u, params.encodings.size());
  EXPECT_TRUE(audio_rtp_sender_->SetParameters(params).ok());

  DestroyAudioRtpSender();
}

TEST_F(RtpSenderReceiverTest, AudioSenderCanSetParametersAsync) {
  CreateAudioRtpSender();

  RtpParameters params = audio_rtp_sender_->GetParameters();
  EXPECT_EQ(1u, params.encodings.size());
  std::optional<RTCError> result;
  audio_rtp_sender_->SetParametersAsync(params, [&](RTCError error) {
    result = error;
    run_loop_.Quit();
  });
  run_loop_.Run();
  EXPECT_TRUE(result->ok());

  DestroyAudioRtpSender();
}

TEST_F(RtpSenderReceiverTest, AudioSenderCanSetParametersBeforeNegotiation) {
  audio_rtp_sender_ = AudioRtpSender::Create(
      CreateTestEnvironment(), signaling_thread_, worker_thread_.get(),
      /*id=*/"", nullptr, nullptr,
      /*enable_sframe_at_owner=*/nullptr, nullptr);

  RtpParameters params = audio_rtp_sender_->GetParameters();
  ASSERT_EQ(1u, params.encodings.size());
  params.encodings[0].max_bitrate_bps = 90000;
  EXPECT_TRUE(audio_rtp_sender_->SetParameters(params).ok());

  params = audio_rtp_sender_->GetParameters();
  EXPECT_EQ(params.encodings[0].max_bitrate_bps, 90000);
  EXPECT_TRUE(audio_rtp_sender_->SetParameters(params).ok());

  DestroyAudioRtpSender();
}

TEST_F(RtpSenderReceiverTest,
       AudioSenderCanSetParametersAsyncBeforeNegotiation) {
  audio_rtp_sender_ = AudioRtpSender::Create(
      CreateTestEnvironment(), signaling_thread_, worker_thread_.get(),
      /*id=*/"", nullptr, nullptr,
      /*enable_sframe_at_owner=*/nullptr, nullptr);

  std::optional<RTCError> result;
  RtpParameters params = audio_rtp_sender_->GetParameters();
  ASSERT_EQ(1u, params.encodings.size());
  params.encodings[0].max_bitrate_bps = 90000;

  audio_rtp_sender_->SetParametersAsync(params, [&](RTCError error) {
    result = error;
    run_loop_.Quit();
  });
  run_loop_.Run();
  EXPECT_TRUE(result->ok());

  params = audio_rtp_sender_->GetParameters();
  EXPECT_EQ(params.encodings[0].max_bitrate_bps, 90000);

  audio_rtp_sender_->SetParametersAsync(params, [&](RTCError error) {
    result = error;
    run_loop_.Quit();
  });
  run_loop_.Run();
  EXPECT_TRUE(result->ok());

  DestroyAudioRtpSender();
}

TEST_F(RtpSenderReceiverTest, AudioSenderInitParametersMovedAfterNegotiation) {
  audio_track_ = AudioTrack::Create(kAudioTrackId, nullptr);
  EXPECT_TRUE(local_stream_->AddTrack(audio_track_));

  std::unique_ptr<MockSetStreamsObserver> set_streams_observer =
      std::make_unique<MockSetStreamsObserver>();
  audio_rtp_sender_ = AudioRtpSender::Create(
      CreateTestEnvironment(), signaling_thread_, worker_thread_.get(),
      audio_track_->id(), nullptr, set_streams_observer.get(),
      /*enable_sframe_at_owner=*/nullptr, nullptr);
  ASSERT_TRUE(audio_rtp_sender_->SetTrack(audio_track_.get()));
  EXPECT_CALL(*set_streams_observer, OnSetStreams());
  audio_rtp_sender_->SetStreams({local_stream_->id()});

  std::vector<RtpEncodingParameters> init_encodings(1);
  init_encodings[0].max_bitrate_bps = 60000;
  audio_rtp_sender_->set_init_send_encodings(init_encodings);

  RtpParameters params = audio_rtp_sender_->GetParameters();
  ASSERT_EQ(1u, params.encodings.size());
  EXPECT_EQ(params.encodings[0].max_bitrate_bps, 60000);

  // Simulate the setLocalDescription call
  std::vector<uint32_t> ssrcs(1, 1);
  StreamParams stream_params = CreateSimStreamParams("cname", ssrcs);
  voice_media_send_channel()->AddSendStream(stream_params);
  worker_thread_->BlockingCall([&] {
    audio_rtp_sender_->SetMediaChannel(
        voice_media_send_channel()->AsVoiceSendChannel());
  });
  SetSsrc(1, *audio_rtp_sender_);

  params = audio_rtp_sender_->GetParameters();
  ASSERT_EQ(1u, params.encodings.size());
  EXPECT_EQ(params.encodings[0].max_bitrate_bps, 60000);

  DestroyAudioRtpSender();
}

TEST_F(RtpSenderReceiverTest,
       AudioSenderMustCallGetParametersBeforeSetParametersBeforeNegotiation) {
  audio_rtp_sender_ = AudioRtpSender::Create(
      CreateTestEnvironment(), signaling_thread_, worker_thread_.get(),
      /*id=*/"", nullptr, nullptr,
      /*enable_sframe_at_owner=*/nullptr, nullptr);

  RtpParameters params;
  RTCError result = audio_rtp_sender_->SetParameters(params);
  EXPECT_EQ(RTCErrorType::INVALID_STATE, result.type());
  DestroyAudioRtpSender();
}

TEST_F(RtpSenderReceiverTest,
       AudioSenderMustCallGetParametersBeforeSetParameters) {
  CreateAudioRtpSender();

  RtpParameters params;
  RTCError result = audio_rtp_sender_->SetParameters(params);
  EXPECT_EQ(RTCErrorType::INVALID_STATE, result.type());

  DestroyAudioRtpSender();
}

TEST_F(RtpSenderReceiverTest,
       AudioSenderSetParametersInvalidatesTransactionId) {
  CreateAudioRtpSender();

  RtpParameters params = audio_rtp_sender_->GetParameters();
  EXPECT_EQ(1u, params.encodings.size());
  EXPECT_TRUE(audio_rtp_sender_->SetParameters(params).ok());
  RTCError result = audio_rtp_sender_->SetParameters(params);
  EXPECT_EQ(RTCErrorType::INVALID_STATE, result.type());

  DestroyAudioRtpSender();
}

TEST_F(RtpSenderReceiverTest,
       AudioSenderSetParametersAsyncInvalidatesTransactionId) {
  CreateAudioRtpSender();

  RtpParameters params = audio_rtp_sender_->GetParameters();
  EXPECT_EQ(1u, params.encodings.size());
  std::optional<RTCError> result;
  audio_rtp_sender_->SetParametersAsync(params, [&](RTCError error) {
    result = error;
    run_loop_.Quit();
  });
  run_loop_.Run();
  EXPECT_TRUE(result->ok());
  audio_rtp_sender_->SetParametersAsync(params, [&](RTCError error) {
    result = error;
    run_loop_.Quit();
  });
  run_loop_.Run();
  EXPECT_EQ(RTCErrorType::INVALID_STATE, result->type());

  DestroyAudioRtpSender();
}

TEST_F(RtpSenderReceiverTest, AudioSenderDetectTransactionIdModification) {
  CreateAudioRtpSender();

  RtpParameters params = audio_rtp_sender_->GetParameters();
  params.transaction_id = "";
  RTCError result = audio_rtp_sender_->SetParameters(params);
  EXPECT_EQ(RTCErrorType::INVALID_MODIFICATION, result.type());

  DestroyAudioRtpSender();
}

TEST_F(RtpSenderReceiverTest, AudioSenderCheckTransactionIdRefresh) {
  CreateAudioRtpSender();

  RtpParameters params = audio_rtp_sender_->GetParameters();
  EXPECT_NE(params.transaction_id.size(), 0U);
  auto saved_transaction_id = params.transaction_id;
  params = audio_rtp_sender_->GetParameters();
  EXPECT_NE(saved_transaction_id, params.transaction_id);

  DestroyAudioRtpSender();
}

TEST_F(RtpSenderReceiverTest, AudioSenderSetParametersOldValueFail) {
  CreateAudioRtpSender();

  RtpParameters params = audio_rtp_sender_->GetParameters();
  RtpParameters second_params = audio_rtp_sender_->GetParameters();

  RTCError result = audio_rtp_sender_->SetParameters(params);
  EXPECT_EQ(RTCErrorType::INVALID_MODIFICATION, result.type());
  DestroyAudioRtpSender();
}

TEST_F(RtpSenderReceiverTest, AudioSenderCantSetUnimplementedRtpParameters) {
  CreateAudioRtpSender();
  RtpParameters params = audio_rtp_sender_->GetParameters();
  EXPECT_EQ(1u, params.encodings.size());

  // Unimplemented RtpParameters: mid
  params.mid = "dummy_mid";
  EXPECT_EQ(RTCErrorType::UNSUPPORTED_PARAMETER,
            audio_rtp_sender_->SetParameters(params).type());
  params = audio_rtp_sender_->GetParameters();

  DestroyAudioRtpSender();
}

TEST_F(RtpSenderReceiverTest, SetAudioMaxSendBitrate) {
  CreateAudioRtpSender();

  EXPECT_EQ(-1, voice_media_send_channel()->max_bps());
  RtpParameters params = audio_rtp_sender_->GetParameters();
  EXPECT_EQ(1U, params.encodings.size());
  EXPECT_FALSE(params.encodings[0].max_bitrate_bps);
  params.encodings[0].max_bitrate_bps = 1000;
  EXPECT_TRUE(audio_rtp_sender_->SetParameters(params).ok());

  // Read back the parameters and verify they have been changed.
  params = audio_rtp_sender_->GetParameters();
  EXPECT_EQ(1U, params.encodings.size());
  EXPECT_EQ(1000, params.encodings[0].max_bitrate_bps);

  // Verify that the audio channel received the new parameters.
  params = voice_media_send_channel()->GetRtpSendParameters(kAudioSsrc);
  EXPECT_EQ(1U, params.encodings.size());
  EXPECT_EQ(1000, params.encodings[0].max_bitrate_bps);

  // Verify that the global bitrate limit has not been changed.
  EXPECT_EQ(-1, voice_media_send_channel()->max_bps());

  DestroyAudioRtpSender();
}

TEST_F(RtpSenderReceiverTest, SetAudioBitratePriority) {
  CreateAudioRtpSender();

  RtpParameters params = audio_rtp_sender_->GetParameters();
  EXPECT_EQ(1U, params.encodings.size());
  EXPECT_EQ(kDefaultBitratePriority, params.encodings[0].bitrate_priority);
  double new_bitrate_priority = 2.0;
  params.encodings[0].bitrate_priority = new_bitrate_priority;
  EXPECT_TRUE(audio_rtp_sender_->SetParameters(params).ok());

  params = audio_rtp_sender_->GetParameters();
  EXPECT_EQ(1U, params.encodings.size());
  EXPECT_EQ(new_bitrate_priority, params.encodings[0].bitrate_priority);

  params = voice_media_send_channel()->GetRtpSendParameters(kAudioSsrc);
  EXPECT_EQ(1U, params.encodings.size());
  EXPECT_EQ(new_bitrate_priority, params.encodings[0].bitrate_priority);

  DestroyAudioRtpSender();
}

TEST_F(RtpSenderReceiverTest, VideoSenderCanSetParameters) {
  CreateVideoRtpSender();

  RtpParameters params = video_rtp_sender_->GetParameters();
  EXPECT_EQ(1u, params.encodings.size());
  EXPECT_TRUE(video_rtp_sender_->SetParameters(params).ok());

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest, CreateVideoFrameInjector) {
  CreateVideoRtpSenderWithNoTrack();

  bool keyframe_called = false;
  KeyFrameCallback keyframe_callback = [&keyframe_called]() {
    keyframe_called = true;
  };

  int32_t last_allocated_bitrate = 0;
  int32_t last_available_outgoing_bitrate = 0;
  BitrateInfoCallback bitrate_callback =
      [&last_allocated_bitrate, &last_available_outgoing_bitrate](
          int32_t allocated_bitrate, int32_t available_outgoing_bitrate) {
        last_allocated_bitrate = allocated_bitrate;
        last_available_outgoing_bitrate = available_outgoing_bitrate;
      };

  auto injector = video_rtp_sender_->CreateEncodedVideoFrameInjector(
      std::move(keyframe_callback), std::move(bitrate_callback));
  ASSERT_TRUE(injector);

  // When frame injector is active, public track API returns nullptr.
  EXPECT_FALSE(video_rtp_sender_->track());

  // Test callbacks route correctly.
  auto* injector_impl = static_cast<EncodedVideoFrameInjector*>(injector.get());
  injector_impl->InvokeKeyFrameCallback();
  EXPECT_TRUE(keyframe_called);

  injector_impl->InvokeBitrateInfoCallback(12345, 67890);
  EXPECT_EQ(12345, last_allocated_bitrate);
  EXPECT_EQ(67890, last_available_outgoing_bitrate);

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest, InjectVideoFrameOnDifferentThread) {
  auto mock_channel = std::make_unique<MockVideoMediaSendChannel>(
      VideoOptions(), network_thread_.get());
  MockVideoMediaSendChannel* mock_channel_ptr = mock_channel.get();
  video_media_send_channel_ = std::move(mock_channel);

  CreateVideoRtpSenderWithNoTrack();

  auto injector =
      video_rtp_sender_->CreateEncodedVideoFrameInjector(nullptr, nullptr);
  ASSERT_TRUE(injector);

  mock_channel_ptr->AddSendStream(StreamParams::CreateLegacy(kVideoSsrc));
  SetSsrc(kVideoSsrc, *video_rtp_sender_);
  ASSERT_TRUE(mock_channel_ptr->last_encoder_factory_override_);

  class MockEncodedImageCallback : public EncodedImageCallback {
   public:
    MOCK_METHOD(Result,
                OnEncodedImage,
                (const EncodedImage&, const CodecSpecificInfo*),
                (override));
    MOCK_METHOD(void, OnFrameDropped, (uint32_t, int, bool), (override));
  };
  ::testing::NiceMock<MockEncodedImageCallback> image_callback;
  ON_CALL(image_callback, OnEncodedImage)
      .WillByDefault(::testing::Return(
          EncodedImageCallback::Result(EncodedImageCallback::Result::OK)));

  std::unique_ptr<VideoEncoder> encoder;
  worker_thread_->BlockingCall([&] {
    encoder = mock_channel_ptr->last_encoder_factory_override_->Create(
        env_, SdpVideoFormat("VP8"));
    VideoCodec codec_settings;
    codec_settings.codecType = kVideoCodecVP8;
    encoder->InitEncode(
        &codec_settings,
        VideoEncoder::Settings(VideoEncoder::Capabilities(false), 1, 1000));
    encoder->RegisterEncodeCompleteCallback(&image_callback);
  });

  auto frame =
      std::make_unique<::testing::NiceMock<MockTransformableVideoFrame>>();
  VideoFrameMetadata metadata;
  metadata.SetWidth(640);
  metadata.SetHeight(480);
  ON_CALL(*frame, Metadata).WillByDefault(::testing::Return(metadata));
  static const uint8_t kPayloadData[] = {1, 2, 3, 4};
  ON_CALL(*frame, GetData)
      .WillByDefault(::testing::Return(std::span<const uint8_t>(kPayloadData)));
  ON_CALL(*frame, GetRtpTimestampInfo)
      .WillByDefault(::testing::Return(RtpTimestampWithoutOffset{12345}));
  ON_CALL(*frame, IsKeyFrame).WillByDefault(::testing::Return(true));

  std::unique_ptr<Thread> injection_thread = Thread::Create();
  injection_thread->Start();

  // run InjectFrame and Release concurrently to test thread safety
  injection_thread->PostTask([&] { injector->InjectFrame(std::move(frame)); });
  worker_thread_->PostTask([&] {
    encoder->Release();
    encoder = nullptr;
  });

  injection_thread->Stop();
  worker_thread_->BlockingCall([] {});

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest, CreateVideoFrameInjectorInvalidStates) {
  CreateVideoRtpSender();

  // 1. Cannot create injector on a stopped sender
  video_rtp_sender_->Stop();
  EXPECT_FALSE(
      video_rtp_sender_->CreateEncodedVideoFrameInjector(nullptr, nullptr));

  DestroyVideoRtpSender();
  CreateVideoRtpSenderWithNoTrack();

  // 2. Cannot create injector if one is already active.
  auto injector =
      video_rtp_sender_->CreateEncodedVideoFrameInjector(nullptr, nullptr);
  ASSERT_TRUE(injector);
  EXPECT_FALSE(
      video_rtp_sender_->CreateEncodedVideoFrameInjector(nullptr, nullptr));

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest, CreateVideoFrameInjectorTrackPresent) {
  CreateVideoRtpSender();

  // Cannot create injector if a track is already present on the sender
  EXPECT_FALSE(
      video_rtp_sender_->CreateEncodedVideoFrameInjector(nullptr, nullptr));

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest, SetTrackClearsVideoFrameInjector) {
  CreateVideoRtpSenderWithNoTrack();

  auto injector =
      video_rtp_sender_->CreateEncodedVideoFrameInjector(nullptr, nullptr);
  ASSERT_TRUE(injector);

  // Attach a track, which should clear the frame injector
  scoped_refptr<VideoTrackInterface> other_track = CreateVideoTrack(false);
  video_rtp_sender_->SetTrack(other_track.get());

  // Verify the frame injector is cleared by ensuring we can create a new one
  // after clearing the track
  video_rtp_sender_->SetTrack(nullptr);
  auto injector3 =
      video_rtp_sender_->CreateEncodedVideoFrameInjector(nullptr, nullptr);
  EXPECT_TRUE(injector3);

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest, SetSsrcPropagatesEncoderFactory) {
  auto mock_channel = std::make_unique<MockVideoMediaSendChannel>(
      VideoOptions(), network_thread_.get());
  MockVideoMediaSendChannel* mock_channel_ptr = mock_channel.get();
  video_media_send_channel_ = std::move(mock_channel);

  CreateVideoRtpSenderWithNoTrack();

  auto injector =
      video_rtp_sender_->CreateEncodedVideoFrameInjector(nullptr, nullptr);
  ASSERT_TRUE(injector);

  mock_channel_ptr->AddSendStream(StreamParams::CreateLegacy(kVideoSsrc));
  SetSsrc(kVideoSsrc, *video_rtp_sender_);

  EXPECT_TRUE(mock_channel_ptr->last_encoder_factory_override_);

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest, ClearVideoFrameInjectorResetsEncoderFactory) {
  auto mock_channel = std::make_unique<MockVideoMediaSendChannel>(
      VideoOptions(), network_thread_.get());
  MockVideoMediaSendChannel* mock_channel_ptr = mock_channel.get();
  video_media_send_channel_ = std::move(mock_channel);

  CreateVideoRtpSenderWithNoTrack();

  auto injector =
      video_rtp_sender_->CreateEncodedVideoFrameInjector(nullptr, nullptr);
  ASSERT_TRUE(injector);

  mock_channel_ptr->AddSendStream(StreamParams::CreateLegacy(kVideoSsrc));
  SetSsrc(kVideoSsrc, *video_rtp_sender_);
  EXPECT_TRUE(mock_channel_ptr->last_encoder_factory_override_);

  scoped_refptr<VideoTrackInterface> other_track = CreateVideoTrack(false);
  video_rtp_sender_->SetTrack(other_track.get());

  EXPECT_TRUE(mock_channel_ptr->reset_encoder_factory_called_);
  EXPECT_FALSE(mock_channel_ptr->last_encoder_factory_override_);

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest,
       CreateVideoFrameInjectorAfterSsrcPropagatesEncoderFactory) {
  auto mock_channel = std::make_unique<MockVideoMediaSendChannel>(
      VideoOptions(), network_thread_.get());
  MockVideoMediaSendChannel* mock_channel_ptr = mock_channel.get();
  video_media_send_channel_ = std::move(mock_channel);

  CreateVideoRtpSenderWithNoTrack();

  mock_channel_ptr->AddSendStream(StreamParams::CreateLegacy(kVideoSsrc));
  SetSsrc(kVideoSsrc, *video_rtp_sender_);

  auto injector =
      video_rtp_sender_->CreateEncodedVideoFrameInjector(nullptr, nullptr);
  ASSERT_TRUE(injector);

  EXPECT_TRUE(mock_channel_ptr->last_encoder_factory_override_);

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest, CreateAudioFrameInjector) {
  CreateAudioRtpSenderWithNoTrack();

  int32_t last_allocated_bitrate = 0;
  TargetBitrateCallback bitrate_callback =
      [&last_allocated_bitrate](int32_t allocated_bitrate) {
        last_allocated_bitrate = allocated_bitrate;
      };

  auto injector = audio_rtp_sender_->CreateEncodedAudioFrameInjector(
      std::move(bitrate_callback));
  ASSERT_TRUE(injector);

  // When frame injector is active, public track API returns nullptr.
  EXPECT_FALSE(audio_rtp_sender_->track());

  // Test callback routes correctly.
  auto* injector_impl = static_cast<EncodedAudioFrameInjector*>(injector.get());
  injector_impl->InvokeBitrateInfoCallback(12345);
  EXPECT_EQ(12345, last_allocated_bitrate);

  DestroyAudioRtpSender();
}

TEST_F(RtpSenderReceiverTest, ProxyAudioEncoderInvokesBitrateCallback) {
  CreateAudioRtpSenderWithNoTrack();

  std::atomic<int32_t> last_allocated_bitrate = 0;
  TargetBitrateCallback bitrate_callback =
      [&last_allocated_bitrate](int32_t allocated_bitrate) {
        last_allocated_bitrate = allocated_bitrate;
      };

  auto injector = audio_rtp_sender_->CreateEncodedAudioFrameInjector(
      std::move(bitrate_callback));
  ASSERT_TRUE(injector);

  auto* injector_impl = static_cast<EncodedAudioFrameInjector*>(injector.get());
  auto encoder_factory = injector_impl->CreateEncoderFactory();
  ASSERT_TRUE(encoder_factory);
  auto encoder =
      encoder_factory->Create(env_, SdpAudioFormat("opus", 48000, 2), {});
  ASSERT_TRUE(encoder);

  // Verify that the callback is invoked no matter which method is called on the
  // proxy encoder, including when called from different threads.
  encoder->OnReceivedTargetAudioBitrate(32000);
  EXPECT_EQ(32000, last_allocated_bitrate.load());

  BitrateAllocationUpdate update;
  update.target_bitrate = DataRate::BitsPerSec(64000);
  encoder->OnReceivedUplinkAllocation(update);
  EXPECT_EQ(64000, last_allocated_bitrate.load());

  worker_thread_->BlockingCall(
      [&] { encoder->OnReceivedTargetAudioBitrate(48000); });
  EXPECT_EQ(48000, last_allocated_bitrate.load());

  worker_thread_->BlockingCall([&] {
    BitrateAllocationUpdate worker_update;
    worker_update.target_bitrate = DataRate::BitsPerSec(96000);
    encoder->OnReceivedUplinkAllocation(worker_update);
  });
  EXPECT_EQ(96000, last_allocated_bitrate.load());

  network_thread_->BlockingCall(
      [&] { encoder->OnReceivedTargetAudioBitrate(50000); });
  EXPECT_EQ(50000, last_allocated_bitrate.load());

  network_thread_->BlockingCall([&] {
    BitrateAllocationUpdate network_update;
    network_update.target_bitrate = DataRate::BitsPerSec(100000);
    encoder->OnReceivedUplinkAllocation(network_update);
  });
  EXPECT_EQ(100000, last_allocated_bitrate.load());

  DestroyAudioRtpSender();
}

TEST_F(RtpSenderReceiverTest, CreateAudioFrameInjectorInvalidStates) {
  CreateAudioRtpSender();

  // 1. Cannot create injector on a stopped sender
  audio_rtp_sender_->Stop();
  EXPECT_FALSE(audio_rtp_sender_->CreateEncodedAudioFrameInjector(nullptr));

  DestroyAudioRtpSender();
  CreateAudioRtpSenderWithNoTrack();

  // 2. Cannot create injector if one is already active.
  auto injector = audio_rtp_sender_->CreateEncodedAudioFrameInjector(nullptr);
  ASSERT_TRUE(injector);
  EXPECT_FALSE(audio_rtp_sender_->CreateEncodedAudioFrameInjector(nullptr));

  DestroyAudioRtpSender();
}

TEST_F(RtpSenderReceiverTest, CreateAudioFrameInjectorTrackPresent) {
  CreateAudioRtpSender();

  // Cannot create injector if a track is already present on the sender
  EXPECT_FALSE(audio_rtp_sender_->CreateEncodedAudioFrameInjector(nullptr));

  DestroyAudioRtpSender();
}

TEST_F(RtpSenderReceiverTest, SetTrackClearsAudioFrameInjector) {
  CreateAudioRtpSenderWithNoTrack();

  auto injector = audio_rtp_sender_->CreateEncodedAudioFrameInjector(nullptr);
  ASSERT_TRUE(injector);

  // Attach a track, which should clear the frame injector
  scoped_refptr<AudioTrackInterface> other_track =
      AudioTrack::Create(kAudioTrackId, nullptr);
  audio_rtp_sender_->SetTrack(other_track.get());

  // Verify the frame injector is cleared by ensuring we can create a new one
  // after clearing the track
  audio_rtp_sender_->SetTrack(nullptr);
  auto injector3 = audio_rtp_sender_->CreateEncodedAudioFrameInjector(nullptr);
  EXPECT_TRUE(injector3);

  DestroyAudioRtpSender();
}

TEST_F(RtpSenderReceiverTest, SetSsrcPropagatesAudioEncoderFactory) {
  auto mock_channel = std::make_unique<MockVoiceMediaSendChannel>(
      AudioOptions(), network_thread_.get());
  MockVoiceMediaSendChannel* mock_channel_ptr = mock_channel.get();
  voice_media_send_channel_ = std::move(mock_channel);

  CreateAudioRtpSenderWithNoTrack();

  auto injector = audio_rtp_sender_->CreateEncodedAudioFrameInjector(nullptr);
  ASSERT_TRUE(injector);

  mock_channel_ptr->AddSendStream(StreamParams::CreateLegacy(kAudioSsrc));
  SetSsrc(kAudioSsrc, *audio_rtp_sender_);

  EXPECT_TRUE(mock_channel_ptr->last_encoder_factory_override_);

  DestroyAudioRtpSender();
}

TEST_F(RtpSenderReceiverTest,
       ClearAudioFrameInjectorResetsAudioEncoderFactory) {
  auto mock_channel = std::make_unique<MockVoiceMediaSendChannel>(
      AudioOptions(), network_thread_.get());
  MockVoiceMediaSendChannel* mock_channel_ptr = mock_channel.get();
  voice_media_send_channel_ = std::move(mock_channel);

  CreateAudioRtpSenderWithNoTrack();

  auto injector = audio_rtp_sender_->CreateEncodedAudioFrameInjector(nullptr);
  ASSERT_TRUE(injector);

  mock_channel_ptr->AddSendStream(StreamParams::CreateLegacy(kAudioSsrc));
  SetSsrc(kAudioSsrc, *audio_rtp_sender_);
  EXPECT_TRUE(mock_channel_ptr->last_encoder_factory_override_);

  scoped_refptr<AudioTrackInterface> other_track =
      AudioTrack::Create(kAudioTrackId, nullptr);
  audio_rtp_sender_->SetTrack(other_track.get());

  EXPECT_TRUE(mock_channel_ptr->reset_encoder_factory_called_);
  EXPECT_FALSE(mock_channel_ptr->last_encoder_factory_override_);

  DestroyAudioRtpSender();
}

TEST_F(RtpSenderReceiverTest,
       CreateAudioFrameInjectorAfterSsrcPropagatesAudioEncoderFactory) {
  auto mock_channel = std::make_unique<MockVoiceMediaSendChannel>(
      AudioOptions(), network_thread_.get());
  MockVoiceMediaSendChannel* mock_channel_ptr = mock_channel.get();
  voice_media_send_channel_ = std::move(mock_channel);

  CreateAudioRtpSenderWithNoTrack();

  mock_channel_ptr->AddSendStream(StreamParams::CreateLegacy(kAudioSsrc));
  SetSsrc(kAudioSsrc, *audio_rtp_sender_);

  auto injector = audio_rtp_sender_->CreateEncodedAudioFrameInjector(nullptr);
  ASSERT_TRUE(injector);

  EXPECT_TRUE(mock_channel_ptr->last_encoder_factory_override_);

  DestroyAudioRtpSender();
}

TEST_F(RtpSenderReceiverTest, VideoSenderCanSetParametersAsync) {
  CreateVideoRtpSender();

  RtpParameters params = video_rtp_sender_->GetParameters();
  EXPECT_EQ(1u, params.encodings.size());
  std::optional<RTCError> result;
  video_rtp_sender_->SetParametersAsync(params, [&](RTCError error) {
    result = error;
    run_loop_.Quit();
  });
  run_loop_.Run();
  EXPECT_TRUE(result->ok());

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest, VideoSenderCanSetParametersBeforeNegotiation) {
  video_rtp_sender_ = VideoRtpSender::Create(
      CreateTestEnvironment(), signaling_thread_, worker_thread_.get(),
      /*id=*/"", nullptr, /*enable_sframe_at_owner=*/nullptr, nullptr,
      /*init_send_encodings=*/{}, /*simulcast_rejected=*/false,
      /*initial_simulcast_layers=*/{});

  std::optional<RTCError> result;
  RtpParameters params = video_rtp_sender_->GetParameters();
  ASSERT_EQ(1u, params.encodings.size());
  params.encodings[0].max_bitrate_bps = 90000;
  video_rtp_sender_->SetParametersAsync(params, [&](RTCError error) {
    result = error;
    run_loop_.Quit();
  });
  run_loop_.Run();
  EXPECT_TRUE(result->ok());

  params = video_rtp_sender_->GetParameters();
  EXPECT_EQ(params.encodings[0].max_bitrate_bps, 90000);
  video_rtp_sender_->SetParametersAsync(params, [&](RTCError error) {
    result = error;
    run_loop_.Quit();
  });
  run_loop_.Run();
  EXPECT_TRUE(result->ok());

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest, VideoSenderInitParametersMovedAfterNegotiation) {
  AddVideoTrack(false);

  std::unique_ptr<MockSetStreamsObserver> set_streams_observer =
      std::make_unique<MockSetStreamsObserver>();
  video_rtp_sender_ = VideoRtpSender::Create(
      CreateTestEnvironment(), signaling_thread_, worker_thread_.get(),
      video_track_->id(), set_streams_observer.get(),
      /*enable_sframe_at_owner=*/nullptr, nullptr,
      /*init_send_encodings=*/{}, /*simulcast_rejected=*/false,
      /*initial_simulcast_layers=*/{});
  ASSERT_TRUE(video_rtp_sender_->SetTrack(video_track_.get()));
  EXPECT_CALL(*set_streams_observer, OnSetStreams());
  video_rtp_sender_->SetStreams({local_stream_->id()});

  std::vector<RtpEncodingParameters> init_encodings(2);
  init_encodings[0].max_bitrate_bps = 60000;
  init_encodings[1].max_bitrate_bps = 900000;
  video_rtp_sender_->set_init_send_encodings(init_encodings);

  RtpParameters params = video_rtp_sender_->GetParameters();
  ASSERT_EQ(2u, params.encodings.size());
  EXPECT_EQ(params.encodings[0].max_bitrate_bps, 60000);
  EXPECT_EQ(params.encodings[1].max_bitrate_bps, 900000);

  // Simulate the setLocalDescription call
  std::vector<uint32_t> ssrcs;
  ssrcs.reserve(2);
  for (int i = 0; i < 2; ++i)
    ssrcs.push_back(kVideoSsrcSimulcast + i);
  StreamParams stream_params = CreateSimStreamParams("cname", ssrcs);
  video_media_send_channel()->AddSendStream(stream_params);
  worker_thread_->BlockingCall([&] {
    video_rtp_sender_->SetMediaChannel(
        video_media_send_channel()->AsVideoSendChannel());
  });
  SetSsrc(kVideoSsrcSimulcast, *video_rtp_sender_, /*layer_count=*/2);

  params = video_rtp_sender_->GetParameters();
  ASSERT_EQ(2u, params.encodings.size());
  EXPECT_EQ(params.encodings[0].max_bitrate_bps, 60000);
  EXPECT_EQ(params.encodings[1].max_bitrate_bps, 900000);

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest,
       VideoSenderInitParametersMovedAfterManualSimulcastAndNegotiation) {
  AddVideoTrack(false);

  std::unique_ptr<MockSetStreamsObserver> set_streams_observer =
      std::make_unique<MockSetStreamsObserver>();
  video_rtp_sender_ = VideoRtpSender::Create(
      CreateTestEnvironment(), signaling_thread_, worker_thread_.get(),
      video_track_->id(), set_streams_observer.get(),
      /*enable_sframe_at_owner=*/nullptr, nullptr,
      /*init_send_encodings=*/{}, /*simulcast_rejected=*/false,
      /*initial_simulcast_layers=*/{});
  ASSERT_TRUE(video_rtp_sender_->SetTrack(video_track_.get()));
  EXPECT_CALL(*set_streams_observer, OnSetStreams());
  video_rtp_sender_->SetStreams({local_stream_->id()});

  std::vector<RtpEncodingParameters> init_encodings(1);
  init_encodings[0].max_bitrate_bps = 60000;
  video_rtp_sender_->set_init_send_encodings(init_encodings);

  RtpParameters params = video_rtp_sender_->GetParameters();
  ASSERT_EQ(1u, params.encodings.size());
  EXPECT_EQ(params.encodings[0].max_bitrate_bps, 60000);

  // Simulate the setLocalDescription call as if the user used SDP munging
  // to enable simulcast
  std::vector<uint32_t> ssrcs;
  ssrcs.reserve(2);
  for (int i = 0; i < 2; ++i)
    ssrcs.push_back(kVideoSsrcSimulcast + i);
  StreamParams stream_params = CreateSimStreamParams("cname", ssrcs);
  video_media_send_channel()->AddSendStream(stream_params);
  worker_thread_->BlockingCall([&] {
    video_rtp_sender_->SetMediaChannel(
        video_media_send_channel()->AsVideoSendChannel());
  });
  SetSsrc(kVideoSsrcSimulcast, *video_rtp_sender_, /*layer_count=*/2);

  params = video_rtp_sender_->GetParameters();
  ASSERT_EQ(2u, params.encodings.size());
  EXPECT_EQ(params.encodings[0].max_bitrate_bps, 60000);

  DestroyVideoRtpSender();
}

// Simulates SDP munging that removes simulcast: the sender was configured with
// more encodings than the local description ends up describing. The local
// description is authoritative, so the surplus layers are dropped.
TEST_F(RtpSenderReceiverTest,
       VideoSenderManualRemoveSimulcastDropsSurplusLayers) {
  AddVideoTrack(/*is_screencast=*/false);

  MockSetStreamsObserver set_streams_observer;
  video_rtp_sender_ = VideoRtpSender::Create(
      CreateTestEnvironment(), signaling_thread_, worker_thread_.get(),
      video_track_->id(), &set_streams_observer,
      /*enable_sframe_at_owner=*/nullptr, nullptr,
      /*init_send_encodings=*/{}, /*simulcast_rejected=*/false,
      /*initial_simulcast_layers=*/{});
  ASSERT_TRUE(video_rtp_sender_->SetTrack(video_track_.get()));
  EXPECT_CALL(set_streams_observer, OnSetStreams());
  video_rtp_sender_->SetStreams({local_stream_->id()});

  std::vector<RtpEncodingParameters> init_encodings(2);
  init_encodings[0].max_bitrate_bps = 60000;
  init_encodings[1].max_bitrate_bps = 120000;
  video_rtp_sender_->set_init_send_encodings(init_encodings);

  RtpParameters params = video_rtp_sender_->GetParameters();
  ASSERT_EQ(2u, params.encodings.size());

  // Simulate the setLocalDescription call. The media channel only knows about
  // the single, non-simulcast stream that was negotiated.
  worker_thread_->BlockingCall([&] {
    video_rtp_sender_->SetMediaChannel(
        video_media_send_channel()->AsVideoSendChannel());
  });
  SetSsrc(kVideoSsrc, *video_rtp_sender_);

  params = video_rtp_sender_->GetParameters();
  ASSERT_EQ(1u, params.encodings.size());
  EXPECT_EQ(params.encodings[0].max_bitrate_bps, 60000);

  DestroyVideoRtpSender();
}

// The layer count from the local description is reconciled with the encodings
// the application asked for on the signaling thread, before the worker thread
// task that configures the media channel runs.
TEST_F(RtpSenderReceiverTest, SurplusLayersAreDroppedOnTheSignalingThread) {
  AddVideoTrack(/*is_screencast=*/false);

  MockSetStreamsObserver set_streams_observer;
  video_rtp_sender_ = VideoRtpSender::Create(
      CreateTestEnvironment(), signaling_thread_, worker_thread_.get(),
      video_track_->id(), &set_streams_observer,
      /*enable_sframe_at_owner=*/nullptr, nullptr,
      /*init_send_encodings=*/{}, /*simulcast_rejected=*/false,
      /*initial_simulcast_layers=*/{});
  ASSERT_TRUE(video_rtp_sender_->SetTrack(video_track_.get()));
  EXPECT_CALL(set_streams_observer, OnSetStreams());
  video_rtp_sender_->SetStreams({local_stream_->id()});

  std::vector<RtpEncodingParameters> init_encodings(2);
  init_encodings[0].max_bitrate_bps = 60000;
  init_encodings[1].max_bitrate_bps = 120000;
  video_rtp_sender_->set_init_send_encodings(init_encodings);

  worker_thread_->BlockingCall([&] {
    video_rtp_sender_->SetMediaChannel(
        video_media_send_channel()->AsVideoSendChannel());
  });

  {
    ScopedOperationsBatcher worker_tasks(worker_thread_.get());
    ScopedOperationsBatcher::BatchTaskWithFinalizer task =
        video_rtp_sender_->SetSsrcTask(kVideoSsrc, /*layer_count=*/1);
    EXPECT_EQ(1u, video_rtp_sender_->init_send_encodings().size());
    worker_tasks.AddWithFinalizer(std::move(task));
  }

  RtpParameters params = video_rtp_sender_->GetParameters();
  ASSERT_EQ(1u, params.encodings.size());
  EXPECT_EQ(params.encodings[0].max_bitrate_bps, 60000);

  DestroyVideoRtpSender();
}

// The media channel has no send stream for the ssrc the sender is attached to.
// This is equivalent to attaching to ssrc 0; the initial parameters are dropped
// and no attempt is made to configure the channel.
TEST_F(RtpSenderReceiverTest, VideoSenderWithSsrcUnknownToTheMediaChannel) {
  AddVideoTrack(/*is_screencast=*/false);

  MockSetStreamsObserver set_streams_observer;
  video_rtp_sender_ = VideoRtpSender::Create(
      CreateTestEnvironment(), signaling_thread_, worker_thread_.get(),
      video_track_->id(), &set_streams_observer,
      /*enable_sframe_at_owner=*/nullptr, nullptr,
      /*init_send_encodings=*/{}, /*simulcast_rejected=*/false,
      /*initial_simulcast_layers=*/{});
  ASSERT_TRUE(video_rtp_sender_->SetTrack(video_track_.get()));
  EXPECT_CALL(set_streams_observer, OnSetStreams());
  video_rtp_sender_->SetStreams({local_stream_->id()});

  std::vector<RtpEncodingParameters> init_encodings(2);
  init_encodings[0].max_bitrate_bps = 60000;
  init_encodings[1].max_bitrate_bps = 120000;
  video_rtp_sender_->set_init_send_encodings(init_encodings);

  worker_thread_->BlockingCall([&] {
    video_rtp_sender_->SetMediaChannel(
        video_media_send_channel()->AsVideoSendChannel());
  });
  // No send stream was ever added for `kVideoSsrcSimulcast`.
  SetSsrc(kVideoSsrcSimulcast, *video_rtp_sender_);

  EXPECT_TRUE(video_rtp_sender_->GetParameters().encodings.empty());
  EXPECT_TRUE(video_rtp_sender_->init_send_encodings().empty());

  DestroyVideoRtpSender();
}

// The encodings the application configured before negotiation are kept when
// the media channel rejects them, so that a later negotiation can apply them.
TEST_F(RtpSenderReceiverTest, InitParametersSurviveARejectedUpdate) {
  AddVideoTrack(/*is_screencast=*/false);

  MockSetStreamsObserver set_streams_observer;
  video_rtp_sender_ = VideoRtpSender::Create(
      CreateTestEnvironment(), signaling_thread_, worker_thread_.get(),
      video_track_->id(), &set_streams_observer,
      /*enable_sframe_at_owner=*/nullptr, nullptr,
      /*init_send_encodings=*/{}, /*simulcast_rejected=*/false,
      /*initial_simulcast_layers=*/{});
  ASSERT_TRUE(video_rtp_sender_->SetTrack(video_track_.get()));
  EXPECT_CALL(set_streams_observer, OnSetStreams());
  video_rtp_sender_->SetStreams({local_stream_->id()});

  // A minimum bitrate above the maximum makes the media channel reject the
  // parameters.
  std::vector<RtpEncodingParameters> init_encodings(1);
  init_encodings[0].min_bitrate_bps = 120000;
  init_encodings[0].max_bitrate_bps = 60000;
  video_rtp_sender_->set_init_send_encodings(init_encodings);

  worker_thread_->BlockingCall([&] {
    video_rtp_sender_->SetMediaChannel(
        video_media_send_channel()->AsVideoSendChannel());
  });
  SetSsrc(kVideoSsrc, *video_rtp_sender_);

  ASSERT_EQ(1u, video_rtp_sender_->init_send_encodings().size());
  EXPECT_EQ(video_rtp_sender_->init_send_encodings()[0].max_bitrate_bps, 60000);

  DestroyVideoRtpSender();
}

// A degradation preference that the application configured before negotiation
// is kept when there is no send stream to apply it to, so that it still takes
// effect once the sender is attached to one.
TEST_F(RtpSenderReceiverTest, DegradationPreferenceSurvivesAMissingSendStream) {
  AddVideoTrack(/*is_screencast=*/false);

  MockSetStreamsObserver set_streams_observer;
  video_rtp_sender_ = VideoRtpSender::Create(
      CreateTestEnvironment(), signaling_thread_, worker_thread_.get(),
      video_track_->id(), &set_streams_observer,
      /*enable_sframe_at_owner=*/nullptr, nullptr,
      /*init_send_encodings=*/{}, /*simulcast_rejected=*/false,
      /*initial_simulcast_layers=*/{});
  ASSERT_TRUE(video_rtp_sender_->SetTrack(video_track_.get()));
  EXPECT_CALL(set_streams_observer, OnSetStreams());
  video_rtp_sender_->SetStreams({local_stream_->id()});

  RtpParameters params = video_rtp_sender_->GetParameters();
  params.degradation_preference = DegradationPreference::MAINTAIN_FRAMERATE;
  ASSERT_TRUE(video_rtp_sender_->SetParameters(params).ok());

  worker_thread_->BlockingCall([&] {
    video_rtp_sender_->SetMediaChannel(
        video_media_send_channel()->AsVideoSendChannel());
  });
  // No send stream was ever added for `kVideoSsrcSimulcast`, so there is
  // nothing to apply the preference to.
  SetSsrc(kVideoSsrcSimulcast, *video_rtp_sender_);

  // The next negotiation attaches the sender to a send stream that the media
  // channel knows about, which is where the preference takes effect.
  SetSsrc(kVideoSsrc, *video_rtp_sender_);

  EXPECT_EQ(video_rtp_sender_->GetParameters().degradation_preference,
            DegradationPreference::MAINTAIN_FRAMERATE);

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest,
       VideoSenderMustCallGetParametersBeforeSetParametersBeforeNegotiation) {
  video_rtp_sender_ = VideoRtpSender::Create(
      CreateTestEnvironment(), signaling_thread_, worker_thread_.get(),
      /*id=*/"", nullptr, /*enable_sframe_at_owner=*/nullptr, nullptr,
      /*init_send_encodings=*/{}, /*simulcast_rejected=*/false,
      /*initial_simulcast_layers=*/{});

  RtpParameters params;
  RTCError result = video_rtp_sender_->SetParameters(params);
  EXPECT_EQ(RTCErrorType::INVALID_STATE, result.type());
  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest,
       VideoSenderMustCallGetParametersBeforeSetParameters) {
  CreateVideoRtpSender();

  RtpParameters params;
  RTCError result = video_rtp_sender_->SetParameters(params);
  EXPECT_EQ(RTCErrorType::INVALID_STATE, result.type());

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest,
       VideoSenderSetParametersInvalidatesTransactionId) {
  CreateVideoRtpSender();

  RtpParameters params = video_rtp_sender_->GetParameters();
  EXPECT_EQ(1u, params.encodings.size());
  EXPECT_TRUE(video_rtp_sender_->SetParameters(params).ok());
  RTCError result = video_rtp_sender_->SetParameters(params);
  EXPECT_EQ(RTCErrorType::INVALID_STATE, result.type());

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest,
       VideoSenderSetParametersAsyncInvalidatesTransactionId) {
  CreateVideoRtpSender();

  RtpParameters params = video_rtp_sender_->GetParameters();
  EXPECT_EQ(1u, params.encodings.size());
  std::string old_id = params.transaction_id;
  std::optional<RTCError> result;
  video_rtp_sender_->SetParametersAsync(params, [&](RTCError error) {
    result = error;
    run_loop_.Quit();
  });
  run_loop_.Run();
  EXPECT_TRUE(result->ok());

  params.transaction_id = old_id;
  result.reset();
  video_rtp_sender_->SetParametersAsync(params, [&](RTCError error) {
    result = error;
    run_loop_.Quit();
  });
  run_loop_.Run();
  EXPECT_EQ(RTCErrorType::INVALID_STATE, result->type());

  DestroyVideoRtpSender();
}

// The parameters that are read back after SetParametersAsync() has applied
// them, are used to populate the cache.
TEST_F(RtpSenderReceiverTest, SetParametersAsyncPopulatesCachedParameters) {
  CreateVideoRtpSender();

  RtpParameters params = video_rtp_sender_->GetParameters();
  ASSERT_EQ(1u, params.encodings.size());
  params.encodings[0].max_bitrate_bps = 100000;
  std::optional<RTCError> result;
  video_rtp_sender_->SetParametersAsync(params, [&](RTCError error) {
    result = std::move(error);
    run_loop_.Quit();
  });
  run_loop_.Run();
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(result->ok());

  RtpParameters cached_params;
  {
    // The parameters must be served from the cache.
    RTC_DCHECK_DISALLOW_THREAD_BLOCKING_CALLS();
    cached_params = video_rtp_sender_->GetParametersInternal();
  }
  ASSERT_EQ(1u, cached_params.encodings.size());
  EXPECT_EQ(cached_params.encodings[0].max_bitrate_bps, 100000);

  DestroyVideoRtpSender();
}

// The completion of SetParametersAsync() must not overwrite the cache with
// parameters that are older than the parameters of the media channel. Here the
// parameters are set again with a blocking call before the completion has run.
TEST_F(RtpSenderReceiverTest,
       SetParametersAsyncCompletionDoesNotOverwriteNewerParameters) {
  CreateVideoRtpSender();

  RtpParameters params = video_rtp_sender_->GetParameters();
  ASSERT_EQ(1u, params.encodings.size());
  params.encodings[0].max_bitrate_bps = 100000;
  std::optional<RTCError> async_result;
  video_rtp_sender_->SetParametersAsync(
      params, [&](RTCError error) { async_result = std::move(error); });

  // The worker thread applies the asynchronous operation before this one, but
  // the completion of the asynchronous operation runs on this thread after
  // this call returns.
  params.encodings[0].max_bitrate_bps = 200000;
  EXPECT_TRUE(video_rtp_sender_->SetParameters(params).ok());

  FlushWorker();
  ASSERT_TRUE(async_result.has_value());
  EXPECT_TRUE(async_result->ok());

  RtpParameters channel_params = worker_thread_->BlockingCall([&] {
    return video_media_send_channel()->GetRtpSendParameters(kVideoSsrc);
  });
  ASSERT_EQ(1u, channel_params.encodings.size());
  EXPECT_EQ(channel_params.encodings[0].max_bitrate_bps, 200000);
  EXPECT_EQ(video_rtp_sender_->GetParametersInternal(), channel_params);
  EXPECT_EQ(video_rtp_sender_->GetParameters(), channel_params);

  DestroyVideoRtpSender();
}

// The completion of SetParametersAsync() must not populate the cache with the
// parameters of the send stream that the sender was attached to when the
// operation was started, if the sender has been attached to a different send
// stream since.
TEST_F(RtpSenderReceiverTest, SetParametersAsyncCompletionAfterSsrcChange) {
  CreateVideoRtpSender();

  RtpParameters params = video_rtp_sender_->GetParameters();
  ASSERT_EQ(1u, params.encodings.size());
  params.encodings[0].max_bitrate_bps = 100000;
  std::optional<RTCError> async_result;
  video_rtp_sender_->SetParametersAsync(
      params, [&](RTCError error) { async_result = std::move(error); });

  SetSsrc(kVideoSsrc2, *video_rtp_sender_);

  FlushWorker();
  ASSERT_TRUE(async_result.has_value());
  EXPECT_TRUE(async_result->ok());

  RtpParameters channel_params = worker_thread_->BlockingCall([&] {
    return video_media_send_channel()->GetRtpSendParameters(kVideoSsrc2);
  });
  ASSERT_EQ(1u, channel_params.encodings.size());
  EXPECT_EQ(channel_params.encodings[0].ssrc, kVideoSsrc2);
  EXPECT_EQ(video_rtp_sender_->GetParametersInternal(), channel_params);
  EXPECT_EQ(video_rtp_sender_->GetParameters(), channel_params);

  DestroyVideoRtpSender();
}

// The parameters are read back when the sender is attached to a send stream,
// also when there are no init parameters to apply.
TEST_F(RtpSenderReceiverTest, SetSsrcPopulatesCachedParameters) {
  CreateVideoRtpSender();
  // The init parameters were applied to `kVideoSsrc`.
  SetSsrc(kVideoSsrc2, *video_rtp_sender_);
  ExpectParametersFromCache(*video_rtp_sender_, video_media_send_channel(),
                            kVideoSsrc2);
  DestroyVideoRtpSender();
}

// The parameters are read back after SetVideoSend(), which can change them.
TEST_F(RtpSenderReceiverTest,
       VideoSenderContentHintChangePopulatesCachedParameters) {
  CreateVideoRtpSender();
  video_track_->set_content_hint(VideoTrackInterface::ContentHint::kDetailed);
  ExpectParametersFromCache(*video_rtp_sender_, video_media_send_channel(),
                            kVideoSsrc);
  DestroyVideoRtpSender();
}

// The parameters are read back after SetAudioSend().
TEST_F(RtpSenderReceiverTest,
       AudioSenderTrackEnabledChangePopulatesCachedParameters) {
  CreateAudioRtpSender();
  audio_track_->set_enabled(false);
  ExpectParametersFromCache(*audio_rtp_sender_, voice_media_send_channel(),
                            kAudioSsrc);
  DestroyAudioRtpSender();
}

TEST_F(RtpSenderReceiverTest, VideoSenderDetectTransactionIdModification) {
  CreateVideoRtpSender();

  RtpParameters params = video_rtp_sender_->GetParameters();
  params.transaction_id = "";
  RTCError result = video_rtp_sender_->SetParameters(params);
  EXPECT_EQ(RTCErrorType::INVALID_MODIFICATION, result.type());

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest, VideoSenderCheckTransactionIdRefresh) {
  CreateVideoRtpSender();

  RtpParameters params = video_rtp_sender_->GetParameters();
  EXPECT_NE(params.transaction_id.size(), 0U);
  auto saved_transaction_id = params.transaction_id;
  params = video_rtp_sender_->GetParameters();
  EXPECT_NE(saved_transaction_id, params.transaction_id);

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest, VideoSenderSetParametersOldValueFail) {
  CreateVideoRtpSender();

  RtpParameters params = video_rtp_sender_->GetParameters();
  RtpParameters second_params = video_rtp_sender_->GetParameters();

  RTCError result = video_rtp_sender_->SetParameters(params);
  EXPECT_EQ(RTCErrorType::INVALID_MODIFICATION, result.type());

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest, VideoSenderCantSetUnimplementedRtpParameters) {
  CreateVideoRtpSender();
  RtpParameters params = video_rtp_sender_->GetParameters();
  EXPECT_EQ(1u, params.encodings.size());

  // Unimplemented RtpParameters: mid
  params.mid = "dummy_mid";
  EXPECT_EQ(RTCErrorType::UNSUPPORTED_PARAMETER,
            video_rtp_sender_->SetParameters(params).type());
  params = video_rtp_sender_->GetParameters();

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest, VideoSenderCanSetScaleResolutionDownBy) {
  CreateVideoRtpSender();

  RtpParameters params = video_rtp_sender_->GetParameters();
  params.encodings[0].scale_resolution_down_by = 2;

  EXPECT_TRUE(video_rtp_sender_->SetParameters(params).ok());
  params = video_rtp_sender_->GetParameters();
  EXPECT_EQ(2, params.encodings[0].scale_resolution_down_by);

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest, VideoSenderDetectInvalidScaleResolutionDownBy) {
  CreateVideoRtpSender();

  RtpParameters params = video_rtp_sender_->GetParameters();
  params.encodings[0].scale_resolution_down_by = 0.5;
  RTCError result = video_rtp_sender_->SetParameters(params);
  EXPECT_EQ(RTCErrorType::INVALID_RANGE, result.type());

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest, VideoSenderCanSetNumTemporalLayers) {
  CreateVideoRtpSender();

  RtpParameters params = video_rtp_sender_->GetParameters();
  params.encodings[0].num_temporal_layers = 2;

  EXPECT_TRUE(video_rtp_sender_->SetParameters(params).ok());
  params = video_rtp_sender_->GetParameters();
  EXPECT_EQ(2, params.encodings[0].num_temporal_layers);

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest, VideoSenderDetectInvalidNumTemporalLayers) {
  CreateVideoRtpSender();

  RtpParameters params = video_rtp_sender_->GetParameters();
  params.encodings[0].num_temporal_layers = kMaxTemporalStreams + 1;
  RTCError result = video_rtp_sender_->SetParameters(params);
  EXPECT_EQ(RTCErrorType::INVALID_RANGE, result.type());

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest, VideoSenderCanSetMaxFramerate) {
  CreateVideoRtpSender();

  RtpParameters params = video_rtp_sender_->GetParameters();
  params.encodings[0].max_framerate = 20;

  EXPECT_TRUE(video_rtp_sender_->SetParameters(params).ok());
  params = video_rtp_sender_->GetParameters();
  EXPECT_EQ(20., params.encodings[0].max_framerate);

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest, VideoSenderCanSetMaxFramerateZero) {
  CreateVideoRtpSender();

  RtpParameters params = video_rtp_sender_->GetParameters();
  params.encodings[0].max_framerate = 0.;

  EXPECT_TRUE(video_rtp_sender_->SetParameters(params).ok());
  params = video_rtp_sender_->GetParameters();
  EXPECT_EQ(0., params.encodings[0].max_framerate);

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest, VideoSenderDetectInvalidMaxFramerate) {
  CreateVideoRtpSender();

  RtpParameters params = video_rtp_sender_->GetParameters();
  params.encodings[0].max_framerate = -5.;
  RTCError result = video_rtp_sender_->SetParameters(params);
  EXPECT_EQ(RTCErrorType::INVALID_RANGE, result.type());

  DestroyVideoRtpSender();
}

// A video sender can have multiple simulcast layers, in which case it will
// contain multiple RtpEncodingParameters. This tests that if this is the case
// (simulcast), then we can't set the bitrate_priority, or max_bitrate_bps
// for any encodings besides at index 0, because these are both implemented
// "per-sender."
TEST_F(RtpSenderReceiverTest, VideoSenderCantSetPerSenderEncodingParameters) {
  // Add a simulcast specific send stream that contains 2 encoding parameters.
  CreateVideoRtpSenderWithSimulcast();
  RtpParameters params = video_rtp_sender_->GetParameters();
  EXPECT_EQ(kVideoSimulcastLayerCount, params.encodings.size());

  params.encodings[1].bitrate_priority = 2.0;
  EXPECT_EQ(RTCErrorType::UNSUPPORTED_PARAMETER,
            video_rtp_sender_->SetParameters(params).type());
  params = video_rtp_sender_->GetParameters();

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest, VideoSenderCantSetReadOnlyEncodingParameters) {
  // Add a simulcast specific send stream that contains 2 encoding parameters.
  CreateVideoRtpSenderWithSimulcast();
  RtpParameters params = video_rtp_sender_->GetParameters();
  EXPECT_EQ(kVideoSimulcastLayerCount, params.encodings.size());

  for (size_t i = 0; i < params.encodings.size(); i++) {
    params.encodings[i].ssrc = 1337;
    EXPECT_EQ(RTCErrorType::INVALID_MODIFICATION,
              video_rtp_sender_->SetParameters(params).type());
    params = video_rtp_sender_->GetParameters();
  }

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest, SetVideoMinMaxSendBitrate) {
  CreateVideoRtpSender();

  EXPECT_EQ(-1, video_media_send_channel()->max_bps());
  RtpParameters params = video_rtp_sender_->GetParameters();
  EXPECT_EQ(1U, params.encodings.size());
  EXPECT_FALSE(params.encodings[0].min_bitrate_bps);
  EXPECT_FALSE(params.encodings[0].max_bitrate_bps);
  params.encodings[0].min_bitrate_bps = 100;
  params.encodings[0].max_bitrate_bps = 1000;
  EXPECT_TRUE(video_rtp_sender_->SetParameters(params).ok());

  // Read back the parameters and verify they have been changed.
  params = video_rtp_sender_->GetParameters();
  EXPECT_EQ(1U, params.encodings.size());
  EXPECT_EQ(100, params.encodings[0].min_bitrate_bps);
  EXPECT_EQ(1000, params.encodings[0].max_bitrate_bps);

  // Verify that the video channel received the new parameters.
  params = video_media_send_channel()->GetRtpSendParameters(kVideoSsrc);
  EXPECT_EQ(1U, params.encodings.size());
  EXPECT_EQ(100, params.encodings[0].min_bitrate_bps);
  EXPECT_EQ(1000, params.encodings[0].max_bitrate_bps);

  // Verify that the global bitrate limit has not been changed.
  EXPECT_EQ(-1, video_media_send_channel()->max_bps());

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest, SetVideoMinMaxSendBitrateSimulcast) {
  // Add a simulcast specific send stream that contains 2 encoding parameters.
  CreateVideoRtpSenderWithSimulcast();

  RtpParameters params = video_rtp_sender_->GetParameters();
  EXPECT_EQ(kVideoSimulcastLayerCount, params.encodings.size());
  params.encodings[0].min_bitrate_bps = 100;
  params.encodings[0].max_bitrate_bps = 1000;
  params.encodings[1].min_bitrate_bps = 200;
  params.encodings[1].max_bitrate_bps = 2000;
  EXPECT_TRUE(video_rtp_sender_->SetParameters(params).ok());

  // Verify that the video channel received the new parameters.
  params =
      video_media_send_channel()->GetRtpSendParameters(kVideoSsrcSimulcast);
  EXPECT_EQ(kVideoSimulcastLayerCount, params.encodings.size());
  EXPECT_EQ(100, params.encodings[0].min_bitrate_bps);
  EXPECT_EQ(1000, params.encodings[0].max_bitrate_bps);
  EXPECT_EQ(200, params.encodings[1].min_bitrate_bps);
  EXPECT_EQ(2000, params.encodings[1].max_bitrate_bps);

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest, SetVideoBitratePriority) {
  CreateVideoRtpSender();

  RtpParameters params = video_rtp_sender_->GetParameters();
  EXPECT_EQ(1U, params.encodings.size());
  EXPECT_EQ(kDefaultBitratePriority, params.encodings[0].bitrate_priority);
  double new_bitrate_priority = 2.0;
  params.encodings[0].bitrate_priority = new_bitrate_priority;
  EXPECT_TRUE(video_rtp_sender_->SetParameters(params).ok());

  params = video_rtp_sender_->GetParameters();
  EXPECT_EQ(1U, params.encodings.size());
  EXPECT_EQ(new_bitrate_priority, params.encodings[0].bitrate_priority);

  params = video_media_send_channel()->GetRtpSendParameters(kVideoSsrc);
  EXPECT_EQ(1U, params.encodings.size());
  EXPECT_EQ(new_bitrate_priority, params.encodings[0].bitrate_priority);

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest, VideoReceiverCanGetParametersWithSimulcast) {
  CreateVideoRtpReceiverWithSimulcast({}, 2);

  RtpParameters params = worker_thread_->BlockingCall(
      [&] { return video_rtp_receiver_->GetParameters(); });
  EXPECT_EQ(2u, params.encodings.size());

  DestroyVideoRtpReceiver();
}

TEST_F(RtpSenderReceiverTest, GenerateKeyFrameWithAudio) {
  CreateAudioRtpSender();

  auto error = audio_rtp_sender_->GenerateKeyFrame({});
  EXPECT_FALSE(error.ok());
  EXPECT_EQ(error.type(), RTCErrorType::UNSUPPORTED_OPERATION);

  DestroyAudioRtpSender();
}

TEST_F(RtpSenderReceiverTest, GenerateKeyFrameWithVideo) {
  CreateVideoRtpSenderWithSimulcast({"1", "2", "3"});

  auto error = video_rtp_sender_->GenerateKeyFrame({});
  EXPECT_TRUE(error.ok());

  error = video_rtp_sender_->GenerateKeyFrame({"1"});
  EXPECT_TRUE(error.ok());

  error = video_rtp_sender_->GenerateKeyFrame({""});
  EXPECT_FALSE(error.ok());
  EXPECT_EQ(error.type(), RTCErrorType::INVALID_PARAMETER);

  error = video_rtp_sender_->GenerateKeyFrame({"no-such-rid"});
  EXPECT_FALSE(error.ok());
  EXPECT_EQ(error.type(), RTCErrorType::INVALID_PARAMETER);

  DestroyVideoRtpSender();
}

// Test that makes sure that a video track content hint translates to the proper
// value for sources that are not screencast.
TEST_F(RtpSenderReceiverTest, PropagatesVideoTrackContentHint) {
  CreateVideoRtpSender();

  video_track_->set_enabled(true);

  // `video_track_` is not screencast by default.
  EXPECT_EQ(false, video_media_send_channel()->options().is_screencast);
  // No content hint should be set by default.
  EXPECT_EQ(VideoTrackInterface::ContentHint::kNone,
            video_track_->content_hint());
  // Setting detailed should turn a non-screencast source into screencast mode.
  video_track_->set_content_hint(VideoTrackInterface::ContentHint::kDetailed);
  EXPECT_EQ(true, video_media_send_channel()->options().is_screencast);
  // Removing the content hint should turn the track back into non-screencast
  // mode.
  video_track_->set_content_hint(VideoTrackInterface::ContentHint::kNone);
  EXPECT_EQ(false, video_media_send_channel()->options().is_screencast);
  // Setting fluid should remain in non-screencast mode (its default).
  video_track_->set_content_hint(VideoTrackInterface::ContentHint::kFluid);
  EXPECT_EQ(false, video_media_send_channel()->options().is_screencast);
  // Setting text should have the same effect as Detailed
  video_track_->set_content_hint(VideoTrackInterface::ContentHint::kText);
  EXPECT_EQ(true, video_media_send_channel()->options().is_screencast);

  DestroyVideoRtpSender();
}

// Test that makes sure that a video track content hint translates to the proper
// value for screencast sources.
TEST_F(RtpSenderReceiverTest,
       PropagatesVideoTrackContentHintForScreencastSource) {
  CreateVideoRtpSender(true, kVideoSsrc);

  video_track_->set_enabled(true);

  // `video_track_` with a screencast source should be screencast by default.
  EXPECT_EQ(true, video_media_send_channel()->options().is_screencast);
  // No content hint should be set by default.
  EXPECT_EQ(VideoTrackInterface::ContentHint::kNone,
            video_track_->content_hint());
  // Setting fluid should turn a screencast source into non-screencast mode.
  video_track_->set_content_hint(VideoTrackInterface::ContentHint::kFluid);
  EXPECT_EQ(false, video_media_send_channel()->options().is_screencast);
  // Removing the content hint should turn the track back into screencast mode.
  video_track_->set_content_hint(VideoTrackInterface::ContentHint::kNone);
  EXPECT_EQ(true, video_media_send_channel()->options().is_screencast);
  // Setting detailed should still remain in screencast mode (its default).
  video_track_->set_content_hint(VideoTrackInterface::ContentHint::kDetailed);
  EXPECT_EQ(true, video_media_send_channel()->options().is_screencast);
  // Setting text should have the same effect as Detailed
  video_track_->set_content_hint(VideoTrackInterface::ContentHint::kText);
  EXPECT_EQ(true, video_media_send_channel()->options().is_screencast);

  DestroyVideoRtpSender();
}

// Test that makes sure any content hints that are set on a track before
// VideoRtpSender is ready to send are still applied when it gets ready to send.
TEST_F(RtpSenderReceiverTest,
       PropagatesVideoTrackContentHintSetBeforeEnabling) {
  AddVideoTrack();
  std::unique_ptr<MockSetStreamsObserver> set_streams_observer =
      std::make_unique<MockSetStreamsObserver>();
  // Setting detailed overrides the default non-screencast mode. This should be
  // applied even if the track is set on construction.
  video_track_->set_content_hint(VideoTrackInterface::ContentHint::kDetailed);
  video_rtp_sender_ = VideoRtpSender::Create(
      CreateTestEnvironment(), signaling_thread_, worker_thread_.get(),
      video_track_->id(), set_streams_observer.get(),
      /*enable_sframe_at_owner=*/nullptr, nullptr,
      /*init_send_encodings=*/{}, /*simulcast_rejected=*/false,
      /*initial_simulcast_layers=*/{});
  ASSERT_TRUE(video_rtp_sender_->SetTrack(video_track_.get()));
  EXPECT_CALL(*set_streams_observer, OnSetStreams());
  video_rtp_sender_->SetStreams({local_stream_->id()});
  worker_thread_->BlockingCall([&] {
    video_rtp_sender_->SetMediaChannel(
        video_media_send_channel()->AsVideoSendChannel());
  });
  video_track_->set_enabled(true);

  // Sender is not ready to send (no SSRC) so no option should have been set.
  EXPECT_EQ(std::nullopt, video_media_send_channel()->options().is_screencast);

  // Verify that the content hint is accounted for when video_rtp_sender_ does
  // get enabled.
  SetSsrc(kVideoSsrc, *video_rtp_sender_);
  EXPECT_EQ(true, video_media_send_channel()->options().is_screencast);

  // And removing the hint should go back to false (to verify that false was
  // default correctly).
  video_track_->set_content_hint(VideoTrackInterface::ContentHint::kNone);
  EXPECT_EQ(false, video_media_send_channel()->options().is_screencast);

  DestroyVideoRtpSender();
}

TEST_F(RtpSenderReceiverTest, AudioSenderHasDtmfSender) {
  CreateAudioRtpSender();
  EXPECT_NE(nullptr, audio_rtp_sender_->GetDtmfSender());
}

TEST_F(RtpSenderReceiverTest, VideoSenderDoesNotHaveDtmfSender) {
  CreateVideoRtpSender();
  EXPECT_EQ(nullptr, video_rtp_sender_->GetDtmfSender());
}

// Test that the DTMF sender is really using `voice_channel_`, and thus returns
// true/false from CanSendDtmf based on what `voice_channel_` returns.
TEST_F(RtpSenderReceiverTest, CanInsertDtmf) {
  AddDtmfCodec();
  CreateAudioRtpSender();
  auto dtmf_sender = audio_rtp_sender_->GetDtmfSender();
  ASSERT_NE(nullptr, dtmf_sender);
  EXPECT_TRUE(dtmf_sender->CanInsertDtmf());
}

TEST_F(RtpSenderReceiverTest, CanNotInsertDtmf) {
  CreateAudioRtpSender();
  auto dtmf_sender = audio_rtp_sender_->GetDtmfSender();
  ASSERT_NE(nullptr, dtmf_sender);
  // DTMF codec has not been added, as it was in the above test.
  EXPECT_FALSE(dtmf_sender->CanInsertDtmf());
}

TEST_F(RtpSenderReceiverTest, InsertDtmf) {
  AddDtmfCodec();
  CreateAudioRtpSender();
  auto dtmf_sender = audio_rtp_sender_->GetDtmfSender();
  ASSERT_NE(nullptr, dtmf_sender);

  EXPECT_EQ(0U, voice_media_send_channel()->dtmf_info_queue().size());

  // Insert DTMF
  const int expected_duration = 90;
  dtmf_sender->InsertDtmf("012", expected_duration, 100);

  // Verify
  ASSERT_THAT(
      WaitUntil(
          [&] { return voice_media_send_channel()->dtmf_info_queue().size(); },
          ::testing::Eq(3U)),
      IsRtcOk());
  const uint32_t send_ssrc =
      voice_media_send_channel()->send_streams()[0].first_ssrc();
  EXPECT_TRUE(CompareDtmfInfo(voice_media_send_channel()->dtmf_info_queue()[0],
                              send_ssrc, 0, expected_duration));
  EXPECT_TRUE(CompareDtmfInfo(voice_media_send_channel()->dtmf_info_queue()[1],
                              send_ssrc, 1, expected_duration));
  EXPECT_TRUE(CompareDtmfInfo(voice_media_send_channel()->dtmf_info_queue()[2],
                              send_ssrc, 2, expected_duration));
}

// Validate that the default FrameEncryptor setting is nullptr.
TEST_F(RtpSenderReceiverTest, AudioSenderCanSetFrameEncryptor) {
  CreateAudioRtpSender();
  scoped_refptr<FrameEncryptorInterface> fake_frame_encryptor(
      new FakeFrameEncryptor());
  EXPECT_EQ(nullptr, audio_rtp_sender_->GetFrameEncryptor());
  audio_rtp_sender_->SetFrameEncryptor(fake_frame_encryptor);
  EXPECT_EQ(fake_frame_encryptor.get(),
            audio_rtp_sender_->GetFrameEncryptor().get());
}

// Validate that setting a FrameEncryptor after the send stream is stopped does
// nothing.
TEST_F(RtpSenderReceiverTest, AudioSenderCannotSetFrameEncryptorAfterStop) {
  CreateAudioRtpSender();
  scoped_refptr<FrameEncryptorInterface> fake_frame_encryptor(
      new FakeFrameEncryptor());
  EXPECT_EQ(nullptr, audio_rtp_sender_->GetFrameEncryptor());
  audio_rtp_sender_->Stop();
  audio_rtp_sender_->SetFrameEncryptor(fake_frame_encryptor);
  // TODO(webrtc:9926) - Validate media channel not set once fakes updated.
}

// Validate that the default FrameEncryptor setting is nullptr.
TEST_F(RtpSenderReceiverTest, AudioReceiverCanSetFrameDecryptor) {
  CreateAudioRtpReceiver();
  scoped_refptr<FrameDecryptorInterface> fake_frame_decryptor(
      make_ref_counted<FakeFrameDecryptor>());
  worker_thread_->BlockingCall([&] {
    EXPECT_EQ(nullptr, audio_rtp_receiver_->GetFrameDecryptor());
    audio_rtp_receiver_->SetFrameDecryptor(fake_frame_decryptor);
    EXPECT_EQ(fake_frame_decryptor.get(),
              audio_rtp_receiver_->GetFrameDecryptor().get());
  });
  DestroyAudioRtpReceiver();
}

// Validate that the default FrameEncryptor setting is nullptr.
TEST_F(RtpSenderReceiverTest, AudioReceiverCannotSetFrameDecryptorAfterStop) {
  CreateAudioRtpReceiver();
  scoped_refptr<FrameDecryptorInterface> fake_frame_decryptor(
      make_ref_counted<FakeFrameDecryptor>());
  worker_thread_->BlockingCall([&] {
    EXPECT_EQ(nullptr, audio_rtp_receiver_->GetFrameDecryptor());
    audio_rtp_receiver_->SetMediaChannel(nullptr);
    audio_rtp_receiver_->SetFrameDecryptor(fake_frame_decryptor);
  });
  // TODO(webrtc:9926) - Validate media channel not set once fakes updated.
  DestroyAudioRtpReceiver();
}

// Validate that the default FrameEncryptor setting is nullptr.
TEST_F(RtpSenderReceiverTest, VideoSenderCanSetFrameEncryptor) {
  CreateVideoRtpSender();
  scoped_refptr<FrameEncryptorInterface> fake_frame_encryptor(
      new FakeFrameEncryptor());
  EXPECT_EQ(nullptr, video_rtp_sender_->GetFrameEncryptor());
  video_rtp_sender_->SetFrameEncryptor(fake_frame_encryptor);
  EXPECT_EQ(fake_frame_encryptor.get(),
            video_rtp_sender_->GetFrameEncryptor().get());
}

// Validate that setting a FrameEncryptor after the send stream is stopped does
// nothing.
TEST_F(RtpSenderReceiverTest, VideoSenderCannotSetFrameEncryptorAfterStop) {
  CreateVideoRtpSender();
  scoped_refptr<FrameEncryptorInterface> fake_frame_encryptor(
      new FakeFrameEncryptor());
  EXPECT_EQ(nullptr, video_rtp_sender_->GetFrameEncryptor());
  video_rtp_sender_->Stop();
  video_rtp_sender_->SetFrameEncryptor(fake_frame_encryptor);
  // TODO(webrtc:9926) - Validate media channel not set once fakes updated.
}

// Validate that the default FrameEncryptor setting is nullptr.
TEST_F(RtpSenderReceiverTest, VideoReceiverCanSetFrameDecryptor) {
  CreateVideoRtpReceiver();
  scoped_refptr<FrameDecryptorInterface> fake_frame_decryptor(
      make_ref_counted<FakeFrameDecryptor>());
  worker_thread_->BlockingCall([&] {
    EXPECT_EQ(nullptr, video_rtp_receiver_->GetFrameDecryptor());
    video_rtp_receiver_->SetFrameDecryptor(fake_frame_decryptor);
    EXPECT_EQ(fake_frame_decryptor.get(),
              video_rtp_receiver_->GetFrameDecryptor().get());
  });
  DestroyVideoRtpReceiver();
}

// Validate that the default FrameEncryptor setting is nullptr.
TEST_F(RtpSenderReceiverTest, VideoReceiverCannotSetFrameDecryptorAfterStop) {
  CreateVideoRtpReceiver();
  scoped_refptr<FrameDecryptorInterface> fake_frame_decryptor(
      make_ref_counted<FakeFrameDecryptor>());
  worker_thread_->BlockingCall([&] {
    EXPECT_EQ(nullptr, video_rtp_receiver_->GetFrameDecryptor());
    video_rtp_receiver_->SetMediaChannel(nullptr);
    video_rtp_receiver_->SetFrameDecryptor(fake_frame_decryptor);
  });
  // TODO(webrtc:9926) - Validate media channel not set once fakes updated.
  DestroyVideoRtpReceiver();
}

// Checks that calling the internal methods for get/set parameters do not
// invalidate any parameters retreived by clients.
TEST_F(RtpSenderReceiverTest,
       InternalParameterMethodsDoNotInvalidateTransaction) {
  CreateVideoRtpSender();
  RtpParameters parameters = video_rtp_sender_->GetParameters();
  RtpParameters new_parameters = video_rtp_sender_->GetParametersInternal();
  new_parameters.encodings[0].active = false;
  EXPECT_TRUE(
      video_rtp_sender_->SetParametersInternal(new_parameters, nullptr, true)
          .ok());
  new_parameters.encodings[0].active = true;
  EXPECT_TRUE(
      video_rtp_sender_->SetParametersInternal(new_parameters, nullptr, true)
          .ok());
  parameters.encodings[0].active = false;
  EXPECT_TRUE(video_rtp_sender_->SetParameters(parameters).ok());
}

// Checks that the senders SetStreams eliminates duplicate stream ids.
TEST_F(RtpSenderReceiverTest, SenderSetStreamsEliminatesDuplicateIds) {
  AddVideoTrack();
  video_rtp_sender_ = VideoRtpSender::Create(
      CreateTestEnvironment(), signaling_thread_, worker_thread_.get(),
      video_track_->id(), nullptr,
      /*enable_sframe_at_owner=*/nullptr, nullptr,
      /*init_send_encodings=*/{}, /*simulcast_rejected=*/false,
      /*initial_simulcast_layers=*/{});
  video_rtp_sender_->SetStreams({"1", "2", "1"});
  EXPECT_EQ(video_rtp_sender_->stream_ids().size(), 2u);
}

TEST_F(RtpSenderReceiverTest, SetSsrcPropagatesFrameEncryptorWithNoEncodings) {
  // Create mock channel with strict expectations.
  auto mock_channel = std::make_unique<MockVideoMediaSendChannel>(
      VideoOptions(), network_thread_.get());
  MockVideoMediaSendChannel* mock_channel_ptr = mock_channel.get();
  // RtpSenderReceiverTest takes ownership of the channel.
  video_media_send_channel_ = std::move(mock_channel);

  // Create sender with no track, which results in empty encodings.
  CreateVideoRtpSenderWithNoTrack();
  video_rtp_sender_->set_init_send_encodings({});

  auto frame_encryptor =
      scoped_refptr<FakeFrameEncryptor>(new FakeFrameEncryptor());
  video_rtp_sender_->SetFrameEncryptor(frame_encryptor);
  mock_channel_ptr->last_set_frame_encryptor_ = nullptr;

  // Expect propagation to media channel when SSRC is set.
  SetSsrc(kVideoSsrc, *video_rtp_sender_);
  EXPECT_EQ(mock_channel_ptr->last_set_frame_encryptor_, frame_encryptor);
}

// Helper method for syntactic sugar for accepting a vector with '{}' notation.
std::pair<RidList, RidList> CreatePairOfRidVectors(
    const std::vector<std::string>& first,
    const std::vector<std::string>& second) {
  return std::make_pair(first, second);
}

// These parameters are used to test disabling simulcast layers.
const std::pair<RidList, RidList> kDisableSimulcastLayersParameters[] = {
    // Tests removing the first layer. This is a special case because
    // the first layer's SSRC is also the 'primary' SSRC used to associate the
    // parameters to the media channel.
    CreatePairOfRidVectors({"1", "2", "3", "4"}, {"1"}),
    // Tests removing some layers.
    CreatePairOfRidVectors({"1", "2", "3", "4"}, {"2", "4"}),
    // Tests simulcast rejected scenario all layers except first are rejected.
    CreatePairOfRidVectors({"1", "2", "3", "4"}, {"2", "3", "4"}),
    // Tests removing all layers.
    CreatePairOfRidVectors({"1", "2", "3", "4"}, {"1", "2", "3", "4"}),
};

// Runs test for disabling layers on a sender without a media engine set.
TEST_P(RtpSenderReceiverTest, DisableSimulcastLayersWithoutMediaEngine) {
  auto parameter = GetParam();
  RunDisableSimulcastLayersWithoutMediaEngineTest(parameter.first,
                                                  parameter.second);
}

// Runs test for disabling layers on a sender with a media engine set.
TEST_P(RtpSenderReceiverTest, DisableSimulcastLayersWithMediaEngine) {
  auto parameter = GetParam();
  RunDisableSimulcastLayersWithMediaEngineTest(parameter.first,
                                               parameter.second);
}

INSTANTIATE_TEST_SUITE_P(
    DisableSimulcastLayersInSender,
    RtpSenderReceiverTest,
    ::testing::ValuesIn(kDisableSimulcastLayersParameters));

TEST_F(RtpSenderReceiverTest, AudioSenderCreateSframeEncryptorInvokesCallback) {
  bool callback_called = false;
  audio_rtp_sender_ = AudioRtpSender::Create(
      CreateTestEnvironment(), signaling_thread_, worker_thread_.get(),
      /*id=*/"", nullptr, nullptr,
      [&callback_called]() -> RTCError {
        callback_called = true;
        return RTCError::OK();
      },
      nullptr);

  SframeEncryptorInit options{SframeMode::kPerPacket,
                              SframeCipherSuite::kAes128GcmSha256_128};
  audio_rtp_sender_->CreateSframeEncryptorOrError(options);
  EXPECT_TRUE(callback_called);
}

TEST_F(RtpSenderReceiverTest, VideoSenderCreateSframeEncryptorInvokesCallback) {
  bool callback_called = false;
  video_rtp_sender_ = VideoRtpSender::Create(
      CreateTestEnvironment(), signaling_thread_, worker_thread_.get(),
      /*id=*/"", nullptr,
      [&callback_called]() -> RTCError {
        callback_called = true;
        return RTCError::OK();
      },
      nullptr, /*init_send_encodings=*/{},
      /*simulcast_rejected=*/false, /*initial_simulcast_layers=*/{});

  SframeEncryptorInit options{SframeMode::kPerFrame,
                              SframeCipherSuite::kAes128GcmSha256_128};
  video_rtp_sender_->CreateSframeEncryptorOrError(options);
  EXPECT_TRUE(callback_called);
}

TEST_F(RtpSenderReceiverTest,
       SenderCreateSframeEncryptorPropagatesCallbackError) {
  audio_rtp_sender_ = AudioRtpSender::Create(
      CreateTestEnvironment(), signaling_thread_, worker_thread_.get(),
      /*id=*/"", nullptr, nullptr,
      []() -> RTCError {
        return RTCError(RTCErrorType::UNSUPPORTED_OPERATION,
                        "Rejected for testing");
      },
      nullptr);

  SframeEncryptorInit options{SframeMode::kPerFrame,
                              SframeCipherSuite::kAes128GcmSha256_128};
  auto result = audio_rtp_sender_->CreateSframeEncryptorOrError(options);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.error().type(), RTCErrorType::UNSUPPORTED_OPERATION);
  EXPECT_STREQ(result.error().message(), "Rejected for testing");
}

TEST_F(RtpSenderReceiverTest,
       SenderCreateSframeEncryptorFailsWithNullCallback) {
  audio_rtp_sender_ = AudioRtpSender::Create(
      CreateTestEnvironment(), signaling_thread_, worker_thread_.get(),
      /*id=*/"", nullptr, nullptr,
      /*enable_sframe_at_owner=*/nullptr, nullptr);

  SframeEncryptorInit options{SframeMode::kPerFrame,
                              SframeCipherSuite::kAes128GcmSha256_128};
  auto result = audio_rtp_sender_->CreateSframeEncryptorOrError(options);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.error().type(), RTCErrorType::INTERNAL_ERROR);
}

TEST_F(RtpSenderReceiverTest, SenderCreateSframeEncryptorFailsWhenStopped) {
  CreateVideoRtpSenderWithSframe();
  video_rtp_sender_->Stop();

  SframeEncryptorInit options{SframeMode::kPerFrame,
                              SframeCipherSuite::kAes128GcmSha256_128};
  auto result = video_rtp_sender_->CreateSframeEncryptorOrError(options);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.error().type(), RTCErrorType::INVALID_STATE);
}

TEST_F(RtpSenderReceiverTest, VideoSenderPushesSframeEncryptorToMediaChannel) {
  auto mock_channel = std::make_unique<MockVideoMediaSendChannel>(
      VideoOptions(), network_thread_.get());
  MockVideoMediaSendChannel* mock_channel_ptr = mock_channel.get();
  // RtpSenderReceiverTest takes ownership of the channel.
  video_media_send_channel_ = std::move(mock_channel);

  CreateVideoRtpSenderWithSframe();
  SetSsrc(kVideoSsrc, *video_rtp_sender_);

  SframeEncryptorInit options{SframeMode::kPerFrame,
                              SframeCipherSuite::kAes128GcmSha256_128};
  auto result = video_rtp_sender_->CreateSframeEncryptorOrError(options);
  ASSERT_TRUE(result.ok());
  EXPECT_NE(result.value().get(), nullptr);
  FlushWorker();
  ASSERT_TRUE(mock_channel_ptr->last_set_sframe_encryption_config_.has_value());
  EXPECT_NE(mock_channel_ptr->last_set_sframe_encryption_config_->encryptor,
            nullptr);
  EXPECT_EQ(mock_channel_ptr->last_set_sframe_encryption_config_->mode,
            options.mode);
}

// The application may create the encryptor before the local description has
// assigned a send SSRC. The encryptor is then handed over right away and
// pushed again once the SSRC is known.
TEST_F(RtpSenderReceiverTest, SetSsrcPropagatesSframeEncryptor) {
  auto mock_channel = std::make_unique<MockVideoMediaSendChannel>(
      VideoOptions(), network_thread_.get());
  MockVideoMediaSendChannel* mock_channel_ptr = mock_channel.get();
  video_media_send_channel_ = std::move(mock_channel);

  CreateVideoRtpSenderWithSframe();

  SframeEncryptorInit options{SframeMode::kPerPacket,
                              SframeCipherSuite::kAes128GcmSha256_128};
  auto result = video_rtp_sender_->CreateSframeEncryptorOrError(options);
  ASSERT_TRUE(result.ok());
  FlushWorker();

  ASSERT_TRUE(mock_channel_ptr->last_set_sframe_encryption_config_.has_value());
  scoped_refptr<SframeMediaEncryptorInterface> encryptor =
      mock_channel_ptr->last_set_sframe_encryption_config_->encryptor;
  ASSERT_NE(encryptor, nullptr);
  mock_channel_ptr->last_set_sframe_encryption_config_.reset();

  SetSsrc(kVideoSsrc, *video_rtp_sender_);
  ASSERT_TRUE(mock_channel_ptr->last_set_sframe_encryption_config_.has_value());
  EXPECT_EQ(mock_channel_ptr->last_set_sframe_encryption_config_->encryptor,
            encryptor);
  EXPECT_EQ(mock_channel_ptr->last_set_sframe_encryption_config_->mode,
            options.mode);
}

TEST_F(RtpSenderReceiverTest,
       SframeEncryptorHandleSharesContextWithMediaThreads) {
  auto mock_channel = std::make_unique<MockVideoMediaSendChannel>(
      VideoOptions(), network_thread_.get());
  MockVideoMediaSendChannel* mock_channel_ptr = mock_channel.get();
  video_media_send_channel_ = std::move(mock_channel);

  CreateVideoRtpSenderWithSframe();
  SetSsrc(kVideoSsrc, *video_rtp_sender_);

  SframeEncryptorInit options{SframeMode::kPerFrame,
                              SframeCipherSuite::kAes128GcmSha256_128};
  auto result = video_rtp_sender_->CreateSframeEncryptorOrError(options);
  ASSERT_TRUE(result.ok());

  const std::vector<uint8_t> key_material(16, 0xab);
  EXPECT_TRUE(
      result.value()->SetEncryptionKey(/*key_id=*/1, key_material).ok());

  ASSERT_TRUE(mock_channel_ptr->last_set_sframe_encryption_config_.has_value());
  scoped_refptr<SframeMediaEncryptorInterface> encryptor =
      mock_channel_ptr->last_set_sframe_encryption_config_->encryptor;
  ASSERT_NE(encryptor, nullptr);
  const std::vector<uint8_t> frame = {0x01, 0x02, 0x03};
  std::vector<uint8_t> encrypted_frame(
      encryptor->GetMaxCiphertextByteSize(frame.size()));
  worker_thread_->BlockingCall([&] {
    EXPECT_TRUE(encryptor
                    ->Encrypt(frame, /*additional_data=*/{},
                              std::span<uint8_t>(encrypted_frame))
                    .ok());
  });
  // Video frames are encrypted on the encoder or frame transformer queue, which
  // is neither the signaling nor the worker thread.
  TaskQueueForTest encoder_queue("EncoderQueue");
  encoder_queue.SendTask([&] {
    EXPECT_TRUE(encryptor->Encrypt(frame, {}, encrypted_frame).ok());
  });
}

TEST_F(RtpSenderReceiverTest, ReplacingSframeEncryptorUpdatesConfiguration) {
  auto mock_channel = std::make_unique<MockVideoMediaSendChannel>(
      VideoOptions(), network_thread_.get());
  MockVideoMediaSendChannel* mock_channel_ptr = mock_channel.get();
  video_media_send_channel_ = std::move(mock_channel);
  CreateVideoRtpSenderWithSframe();
  SetSsrc(kVideoSsrc, *video_rtp_sender_);

  scoped_refptr<SframeMediaEncryptorInterface> previous_encryptor;
  for (SframeMode mode : {SframeMode::kPerFrame, SframeMode::kPerPacket}) {
    SframeEncryptorInit options{mode, SframeCipherSuite::kAes128GcmSha256_128};
    RTCErrorOr<scoped_refptr<SframeEncryptorInterface>> result =
        video_rtp_sender_->CreateSframeEncryptorOrError(options);
    ASSERT_TRUE(result.ok());
    FlushWorker();

    ASSERT_TRUE(
        mock_channel_ptr->last_set_sframe_encryption_config_.has_value());
    const SframeEncryptionConfig& encryption =
        *mock_channel_ptr->last_set_sframe_encryption_config_;
    EXPECT_NE(encryption.encryptor, nullptr);
    EXPECT_NE(encryption.encryptor, previous_encryptor);
    EXPECT_EQ(encryption.mode, mode);
    previous_encryptor = encryption.encryptor;
  }
}

TEST_F(RtpSenderReceiverTest,
       AudioReceiverCreateSframeDecryptorInvokesCallback) {
  bool callback_called = false;
  auto receiver = make_ref_counted<AudioRtpReceiver>(
      worker_thread_.get(), kAudioTrackId, std::vector<std::string>(),
      [&callback_called]() -> RTCError {
        callback_called = true;
        return RTCError::OK();
      });

  receiver->CreateSframeDecryptorOrError(
      SframeCipherSuite::kAes128GcmSha256_128, [](SframeDecryptError) {});
  EXPECT_TRUE(callback_called);
}

TEST_F(RtpSenderReceiverTest,
       VideoReceiverCreateSframeDecryptorInvokesCallback) {
  bool callback_called = false;
  auto receiver = make_ref_counted<VideoRtpReceiver>(
      worker_thread_.get(), kVideoTrackId, std::vector<std::string>(),
      [&callback_called]() -> RTCError {
        callback_called = true;
        return RTCError::OK();
      });

  receiver->CreateSframeDecryptorOrError(
      SframeCipherSuite::kAes128GcmSha256_128, [](SframeDecryptError) {});
  EXPECT_TRUE(callback_called);
}

TEST_F(RtpSenderReceiverTest,
       ReceiverCreateSframeDecryptorAcceptsMoveOnlyErrorCallbackThroughProxy) {
  bool owner_callback_called = false;
  scoped_refptr<VideoRtpReceiver> receiver = make_ref_counted<VideoRtpReceiver>(
      worker_thread_.get(), kVideoTrackId, std::vector<std::string>(),
      /*enable_sframe_at_owner=*/[&owner_callback_called]() -> RTCError {
        owner_callback_called = true;
        return RTCError::OK();
      });
  scoped_refptr<RtpReceiverInterface> proxy = RtpReceiverProxy::Create(
      Thread::Current(), worker_thread_.get(), receiver);
  auto error_callback = std::make_unique<MockFunction<void()>>();
  EXPECT_CALL(*error_callback, Call).Times(0);

  RTCErrorOr<scoped_refptr<SframeDecryptorInterface>> result =
      proxy->CreateSframeDecryptorOrError(
          SframeCipherSuite::kAes128GcmSha256_128,
          [error_callback = std::move(error_callback)](SframeDecryptError) {
            error_callback->Call();
          });

  EXPECT_TRUE(owner_callback_called);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error().type(), RTCErrorType::UNSUPPORTED_OPERATION);
}

TEST_F(RtpSenderReceiverTest,
       ReceiverCreateSframeDecryptorPropagatesCallbackError) {
  auto receiver = make_ref_counted<AudioRtpReceiver>(
      worker_thread_.get(), kAudioTrackId, std::vector<std::string>(),
      []() -> RTCError {
        return RTCError(RTCErrorType::UNSUPPORTED_OPERATION,
                        "Rejected for testing");
      });

  RTCErrorOr<scoped_refptr<SframeDecryptorInterface>> result =
      receiver->CreateSframeDecryptorOrError(
          SframeCipherSuite::kAes128GcmSha256_128, [](SframeDecryptError) {});
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.error().type(), RTCErrorType::UNSUPPORTED_OPERATION);
  EXPECT_STREQ(result.error().message(), "Rejected for testing");
}

TEST_F(RtpSenderReceiverTest,
       ReceiverCreateSframeDecryptorFailsWithNullCallback) {
  auto receiver = make_ref_counted<AudioRtpReceiver>(
      worker_thread_.get(), kAudioTrackId, std::vector<std::string>(),
      /*enable_sframe_at_owner=*/nullptr);

  RTCErrorOr<scoped_refptr<SframeDecryptorInterface>> result =
      receiver->CreateSframeDecryptorOrError(
          SframeCipherSuite::kAes128GcmSha256_128, [](SframeDecryptError) {});
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.error().type(), RTCErrorType::INTERNAL_ERROR);
}

TEST_F(RtpSenderReceiverTest,
       AudioReceiverCreateSframeDecryptorFailsWhenStopped) {
  bool owner_callback_called = false;
  scoped_refptr<AudioRtpReceiver> receiver = make_ref_counted<AudioRtpReceiver>(
      worker_thread_.get(), kAudioTrackId, std::vector<std::string>(),
      /*enable_sframe_at_owner=*/[&owner_callback_called]() -> RTCError {
        owner_callback_called = true;
        return RTCError::OK();
      });
  receiver->Stop();

  RTCErrorOr<scoped_refptr<SframeDecryptorInterface>> result =
      receiver->CreateSframeDecryptorOrError(
          SframeCipherSuite::kAes128GcmSha256_128, [](SframeDecryptError) {});

  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error().type(), RTCErrorType::INVALID_STATE);
  EXPECT_FALSE(owner_callback_called);
}

TEST_F(RtpSenderReceiverTest,
       VideoReceiverCreateSframeDecryptorFailsWhenStopped) {
  bool owner_callback_called = false;
  scoped_refptr<VideoRtpReceiver> receiver = make_ref_counted<VideoRtpReceiver>(
      worker_thread_.get(), kVideoTrackId, std::vector<std::string>(),
      /*enable_sframe_at_owner=*/[&owner_callback_called]() -> RTCError {
        owner_callback_called = true;
        return RTCError::OK();
      });
  receiver->Stop();

  RTCErrorOr<scoped_refptr<SframeDecryptorInterface>> result =
      receiver->CreateSframeDecryptorOrError(
          SframeCipherSuite::kAes128GcmSha256_128, [](SframeDecryptError) {});

  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.error().type(), RTCErrorType::INVALID_STATE);
  EXPECT_FALSE(owner_callback_called);
}

}  // namespace webrtc
