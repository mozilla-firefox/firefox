/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOCK_CALL_H_
#define MOCK_CALL_H_

#include <span>
#include <tuple>

#include "gmock/gmock.h"
#include "mozilla/Assertions.h"
#include "mozilla/Maybe.h"
#include "mozilla/media/MediaUtils.h"
#include "nsTArray.h"
#include "WebrtcCallWrapper.h"
#include "WebrtcEnvironmentWrapper.h"
#include "PeerConnectionCtx.h"

// libwebrtc
#include "api/call/audio_sink.h"
#include "api/video/video_stream_encoder_settings.h"
#include "call/call.h"

namespace test {
class MockCallWrapper;

class MockAudioSendStream : public webrtc::AudioSendStream {
 public:
  explicit MockAudioSendStream(RefPtr<MockCallWrapper> aCallWrapper)
      : mCallWrapper(std::move(aCallWrapper)) {}

  const webrtc::AudioSendStream::Config& GetConfig() const override;

  void Reconfigure(const Config& config,
                   webrtc::SetParametersCallback callback) override;

  void Start() override {}

  void Stop() override {}

  void SendAudioData(std::unique_ptr<webrtc::AudioFrame> audio_frame) override {
  }

  bool SendTelephoneEvent(int /*payload_type*/, int /*payload_frequency*/,
                          int /*event*/, int /*duration_ms*/) override {
    return true;
  }

  void SetMuted(bool muted) override {}

  Stats GetStats() const override { return mStats; }

  Stats GetStats(bool /*has_remote_tracks*/) const override { return mStats; }

  virtual ~MockAudioSendStream() {}

  const RefPtr<MockCallWrapper> mCallWrapper;
  webrtc::AudioSendStream::Stats mStats;
};

class MockAudioReceiveStream : public webrtc::AudioReceiveStreamInterface {
 public:
  explicit MockAudioReceiveStream(RefPtr<MockCallWrapper> aCallWrapper)
      : mCallWrapper(std::move(aCallWrapper)) {}

  void Start() override {}

  void Stop() override {}

  bool IsRunning() const override { return true; }

  Stats GetStats(bool /*get_and_clear_legacy_stats*/) const override {
    return mStats;
  }

  void SetSink(webrtc::AudioSinkInterface* sink) override {}

  void SetGain(float gain) override {}

  void SetJitterBufferMaxPackets(size_t max_packets) override {}

  void SetJitterBufferFastAccelerate(bool fast_accelerate) override {}

  webrtc::AudioMixer::Source* source() override { return nullptr; }

  void SetRtcpMode(webrtc::RtcpMode mode) override {}

  virtual void SetDepacketizerToDecoderFrameTransformer(
      webrtc::scoped_refptr<webrtc::FrameTransformerInterface>
          /*frame_transformer*/) override {
    MOZ_CRASH(
        "Unimplemented after webrtc.org e2561e17e2 removed the Reconfigure "
        "method.");
  }
  virtual void SetDecoderMap(
      std::map<int, webrtc::SdpAudioFormat> decoder_map) override;
  virtual void SetNackHistory(int /*history_ms*/) override {
    MOZ_CRASH(
        "Unimplemented after webrtc.org e2561e17e2 removed the Reconfigure "
        "method.");
  }
  virtual void SetNonSenderRttMeasurement(bool enabled) override {}
  void SetFrameDecryptor(webrtc::scoped_refptr<webrtc::FrameDecryptorInterface>
                             frame_decryptor) override {}
  bool SetBaseMinimumPlayoutDelayMs(int /*delay_ms*/) override { return false; }
  int GetBaseMinimumPlayoutDelayMs() const override { return 0; }
  uint32_t remote_ssrc() const override { return 0; }

  virtual ~MockAudioReceiveStream() {}

  const RefPtr<MockCallWrapper> mCallWrapper;
  webrtc::AudioReceiveStreamInterface::Stats mStats;
};

class MockVideoSendStream : public webrtc::VideoSendStream {
 public:
  explicit MockVideoSendStream(RefPtr<MockCallWrapper> aCallWrapper)
      : mCallWrapper(std::move(aCallWrapper)) {}

  void Start() override {}

  void Stop() override {}

  bool started() override { return false; }

  void SetSource(
      webrtc::VideoSourceInterface<webrtc::VideoFrame>* source,
      const webrtc::DegradationPreference& degradation_preference) override;

  void ReconfigureVideoEncoder(webrtc::VideoEncoderConfig config) override;

  void ReconfigureVideoEncoder(webrtc::VideoEncoderConfig config,
                               webrtc::SetParametersCallback callback) override;

  Stats GetStats() override { return mStats; }

  void AddAdaptationResource(
      webrtc::scoped_refptr<webrtc::Resource> resource) override {}

  std::vector<webrtc::scoped_refptr<webrtc::Resource>> GetAdaptationResources()
      override {
    return std::vector<webrtc::scoped_refptr<webrtc::Resource>>();
  }

  void SetCsrcs(std::span<const uint32_t> csrcs) override {}
  void GenerateKeyFrame(const std::vector<std::string>& rids) override {}

  virtual ~MockVideoSendStream() {}

  const RefPtr<MockCallWrapper> mCallWrapper;
  webrtc::VideoSendStream::Stats mStats;
};

class MockVideoReceiveStream : public webrtc::VideoReceiveStreamInterface {
 public:
  explicit MockVideoReceiveStream(RefPtr<MockCallWrapper> aCallWrapper)
      : mCallWrapper(std::move(aCallWrapper)) {}

  void Start() override {}

  void Stop() override {}

  Stats GetStats() const override { return mStats; }

  bool SetBaseMinimumPlayoutDelayMs(int /*delay_ms*/) override { return false; }

  int GetBaseMinimumPlayoutDelayMs() const override { return 0; }

  void SetFrameDecryptor(webrtc::scoped_refptr<webrtc::FrameDecryptorInterface>
                             frame_decryptor) override {}

  void SetDepacketizerToDecoderFrameTransformer(
      webrtc::scoped_refptr<webrtc::FrameTransformerInterface>
          frame_transformer) override {}

  RecordingState SetAndGetRecordingState(RecordingState /*state*/,
                                         bool /*generate_key_frame*/) override {
    return {};
  }

  void GenerateKeyFrame() override {}

  void SetRtcpMode(webrtc::RtcpMode mode) override {}

  void SetFlexFecProtection(
      webrtc::RtpPacketSinkInterface* flexfec_sink) override {}

  void SetNackHistory(webrtc::TimeDelta history) override {}

  void SetProtectionPayloadTypes(int red_payload_type,
                                 int ulpfec_payload_type) override {}

  void SetRtcpXr(Config::Rtp::RtcpXr rtcp_xr) override {}

  virtual void SetAssociatedPayloadTypes(
      std::map<int, int> associated_payload_types) override {}

  void SetDecoders(std::vector<Decoder> decoders) override {}

  virtual void SetRawPayloadTypes(std::set<int> raw_payload_types) override {}

  virtual void UpdateRtxSsrc(uint32_t ssrc) override {};

  virtual ~MockVideoReceiveStream() {}

  const RefPtr<MockCallWrapper> mCallWrapper;
  webrtc::VideoReceiveStreamInterface::Stats mStats;
};

class MockCall : public webrtc::Call {
 public:
  explicit MockCall(RefPtr<MockCallWrapper> aCallWrapper)
      : mCallWrapper(std::move(aCallWrapper)) {}

  webrtc::AudioSendStream* CreateAudioSendStream(
      const webrtc::AudioSendStream::Config& config) override {
    MOZ_RELEASE_ASSERT(!mAudioSendConfig);
    mAudioSendConfig = mozilla::Some(config);
    return new MockAudioSendStream(mCallWrapper);
  }

  void DestroyAudioSendStream(webrtc::AudioSendStream* send_stream) override {
    mAudioSendConfig = mozilla::Nothing();
    delete static_cast<MockAudioSendStream*>(send_stream);
  }

  webrtc::AudioReceiveStreamInterface* CreateAudioReceiveStream(
      webrtc::AudioReceiveStreamInterface::Config config) override {
    MOZ_RELEASE_ASSERT(!mAudioReceiveConfig);
    mAudioReceiveConfig = mozilla::Some(std::move(config));
    return new MockAudioReceiveStream(mCallWrapper);
  }
  void DestroyAudioReceiveStream(
      webrtc::AudioReceiveStreamInterface* receive_stream) override {
    mAudioReceiveConfig = mozilla::Nothing();
    delete static_cast<MockAudioReceiveStream*>(receive_stream);
  }

  webrtc::VideoSendStream* CreateVideoSendStream(
      webrtc::VideoSendStream::Config config,
      webrtc::VideoEncoderConfig encoder_config,
      webrtc::EncoderSwitchRequestCallback /*encoder_switch_request_callback*/ =
          nullptr) override {
    MOZ_RELEASE_ASSERT(!mVideoSendConfig);
    MOZ_RELEASE_ASSERT(!mVideoSendEncoderConfig);
    mVideoSendConfig = mozilla::Some(std::move(config));
    mVideoSendEncoderConfig = mozilla::Some(std::move(encoder_config));
    return new MockVideoSendStream(mCallWrapper);
  }

  webrtc::VideoSendStream* CreateVideoSendStream(
      webrtc::VideoSendStream::Config /*config*/,
      webrtc::VideoEncoderConfig /*encoder_config*/,
      webrtc::EncoderSwitchRequestCallback /*encoder_switch_request_callback*/,
      std::unique_ptr<webrtc::FecController> /*fec_controller*/) override {
    return nullptr;
  };

  void DestroyVideoSendStream(webrtc::VideoSendStream* send_stream) override {
    mVideoSendConfig = mozilla::Nothing();
    mVideoSendEncoderConfig = mozilla::Nothing();
    delete static_cast<MockVideoSendStream*>(send_stream);
  }

  webrtc::VideoReceiveStreamInterface* CreateVideoReceiveStream(
      webrtc::VideoReceiveStreamInterface::Config configuration) override {
    MOZ_RELEASE_ASSERT(!mVideoReceiveConfig);
    mVideoReceiveConfig = mozilla::Some(std::move(configuration));
    return new MockVideoReceiveStream(mCallWrapper);
  }

  void DestroyVideoReceiveStream(
      webrtc::VideoReceiveStreamInterface* receive_stream) override {
    mVideoReceiveConfig = mozilla::Nothing();
    delete static_cast<MockVideoReceiveStream*>(receive_stream);
  }

  webrtc::FlexfecReceiveStream* CreateFlexfecReceiveStream(
      const webrtc::FlexfecReceiveStream::Config /*config*/) override {
    return nullptr;
  }

  void DestroyFlexfecReceiveStream(
      webrtc::FlexfecReceiveStream* receive_stream) override {}

  void AddAdaptationResource(
      webrtc::scoped_refptr<webrtc::Resource> resource) override {}

  webrtc::PacketReceiver* Receiver() override { return nullptr; }

  webrtc::RtpTransportControllerSendInterface* GetTransportControllerSend()
      override {
    return nullptr;
  }

  Stats GetStats() const override { return mStats; }

  void SignalChannelNetworkState(webrtc::MediaType media,
                                 webrtc::NetworkState state) override {
    mNetworkStateSignals.EmplaceBack(media, state);
  }

  void OnUpdateSyncGroup(webrtc::AudioReceiveStreamInterface& stream,
                         absl::string_view sync_group) override {}

  void OnSentPacket(const webrtc::SentPacketInfo& sent_packet) override {}

  void SetClientBitratePreferences(
      const webrtc::BitrateSettings& preferences) override {}

  void SetEncoderInfo(const webrtc::VideoEncoder::EncoderInfo& aInfo) {
    mEncoderInfo = aInfo;
  }

  void SetPreferredRtcpCcAckType(
      webrtc::RtcpFeedbackType preferred_rtcp_cc_ack_type) override {}
  std::optional<int> FeedbackAccordingToRfc8888Count() override { return 0; }
  std::optional<int> FeedbackAccordingToTransportCcCount() override {
    return 0;
  }

  std::vector<webrtc::VideoStream> CreateEncoderStreams(int width, int height) {
    mVideoSendEncoderConfig->video_stream_factory->SetEncoderInfo(mEncoderInfo);
    return mVideoSendEncoderConfig->video_stream_factory->CreateEncoderStreams(
        mUnusedConfig, width, height, *mVideoSendEncoderConfig);
  }

  virtual webrtc::TaskQueueBase* network_thread() const override {
    return nullptr;
  }

  virtual void DisconnectFromNetworkThread() override { }

  virtual webrtc::TaskQueueBase* worker_thread() const override {
    return nullptr;
  }

  virtual ~MockCall() {};

  const RefPtr<MockCallWrapper> mCallWrapper;
  mozilla::Maybe<webrtc::AudioReceiveStreamInterface::Config>
      mAudioReceiveConfig;
  mozilla::Maybe<webrtc::AudioSendStream::Config> mAudioSendConfig;
  mozilla::Maybe<webrtc::VideoReceiveStreamInterface::Config>
      mVideoReceiveConfig;
  mozilla::Maybe<webrtc::VideoSendStream::Config> mVideoSendConfig;
  mozilla::Maybe<webrtc::VideoEncoderConfig> mVideoSendEncoderConfig;
  nsTArray<std::tuple<webrtc::MediaType, webrtc::NetworkState>>
      mNetworkStateSignals;
  webrtc::Call::Stats mStats;
  webrtc::MozTrialsConfig mUnusedConfig;
  webrtc::VideoEncoder::EncoderInfo mEncoderInfo;
  webrtc::DegradationPreference mConfiguredDegradationPreference;
};

class MockCallWrapper : public mozilla::WebrtcCallWrapper {
 public:
  MockCallWrapper(
      RefPtr<mozilla::SharedWebrtcState> aSharedState,
      mozilla::UniquePtr<webrtc::VideoBitrateAllocatorFactory>
          aVideoBitrateAllocatorFactory,
      mozilla::UniquePtr<webrtc::RtcEventLog> /*aEventLog*/,
      mozilla::UniquePtr<webrtc::TaskQueueFactory> /*aTaskQueueFactory*/,
      const mozilla::dom::RTCStatsTimestampMaker& aTimestampMaker,
      mozilla::UniquePtr<mozilla::media::ShutdownBlockingTicket>
          aShutdownTicket)
      : mozilla::WebrtcCallWrapper(
            std::move(aSharedState), std::move(aVideoBitrateAllocatorFactory),
            mozilla::WebrtcEnvironmentWrapper::Create(aTimestampMaker),
            aTimestampMaker, std::move(aShutdownTicket)) {}

  static RefPtr<MockCallWrapper> Create() {
    auto state = mozilla::MakeRefPtr<mozilla::SharedWebrtcState>(
        mozilla::AbstractThread::GetCurrent(), webrtc::AudioState::Config(),
        nullptr, nullptr);
    auto wrapper = mozilla::MakeRefPtr<MockCallWrapper>(
        state, nullptr, nullptr, nullptr,
        mozilla::dom::RTCStatsTimestampMaker::Create(), nullptr);
    wrapper->SetCall(mozilla::WrapUnique(new MockCall(wrapper)));
    return wrapper;
  }

  MockCall* GetMockCall() { return static_cast<MockCall*>(Call()); }
};

}  // namespace test
#endif
