/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "audio/audio_send_stream.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "api/audio/audio_frame.h"
#include "api/audio/audio_processing_statistics.h"
#include "api/audio_codecs/audio_encoder.h"
#include "api/audio_codecs/audio_format.h"
#include "api/call/bitrate_allocation.h"
#include "api/crypto/frame_encryptor_interface.h"
#include "api/environment/environment_factory.h"
#include "api/field_trials.h"
#include "api/function_view.h"
#include "api/make_ref_counted.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_parameters.h"
#include "api/scoped_refptr.h"
#include "api/test/mock_frame_encryptor.h"
#include "api/transport/network_types.h"
#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "audio/audio_state.h"
#include "audio/channel_send.h"
#include "audio/conversion.h"
#include "audio/mock_voe_channel_proxy.h"
#include "call/audio_state.h"
#include "call/bitrate_allocator.h"
#include "call/sframe_options.h"
#include "call/test/mock_bitrate_allocator.h"
#include "call/test/mock_rtp_transport_controller_send.h"
#include "modules/audio_device/include/mock_audio_device.h"
#include "modules/audio_mixer/audio_mixer_impl.h"
#include "modules/audio_mixer/sine_wave_generator.h"
#include "modules/audio_processing/include/mock_audio_processing.h"
#include "modules/rtp_rtcp/include/report_block_data.h"
#include "modules/rtp_rtcp/mocks/mock_network_link_rtcp_observer.h"
#include "modules/rtp_rtcp/mocks/mock_rtp_rtcp.h"
#include "modules/rtp_rtcp/source/rtp_header_extensions.h"
#include "test/create_test_field_trials.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/mock_audio_encoder.h"
#include "test/mock_audio_encoder_factory.h"
#include "test/time_controller/real_time_controller.h"

namespace webrtc {
namespace test {
namespace {

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::Field;
using ::testing::InSequence;
using ::testing::Ne;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::StrEq;
using ::testing::WithArg;

constexpr float kTolerance = 0.0001f;

constexpr uint32_t kSsrc = 1234;
constexpr char kCName[] = "foo_name";
constexpr std::array<uint32_t, 2> kCsrcs = {5678, 9012};
constexpr RtpHeaderExtensionId kAudioLevelId(2);
constexpr RtpHeaderExtensionId kTransportSequenceNumberId(4);
constexpr int32_t kEchoDelayMedian = 254;
constexpr int32_t kEchoDelayStdDev = -3;
constexpr double kDivergentFilterFraction = 0.2f;
constexpr double kEchoReturnLoss = -65;
constexpr double kEchoReturnLossEnhancement = 101;
constexpr double kResidualEchoLikelihood = -1.0f;
constexpr double kResidualEchoLikelihoodMax = 23.0f;
constexpr ChannelSendStatistics kChannelStats = {
    .round_trip_time = TimeDelta::Millis(112),
    .payload_bytes_sent = 12,
    .header_and_padding_bytes_sent = 13456,
    .retransmitted_bytes_sent = 17890};
constexpr int kFractionLost = 123;
constexpr int kCumulativeLost = 567;
constexpr uint32_t kInterarrivalJitter = 132;
constexpr int kTelephoneEventPayloadType = 123;
constexpr int kTelephoneEventPayloadFrequency = 65432;
constexpr int kTelephoneEventCode = 45;
constexpr int kTelephoneEventDuration = 6789;
constexpr int kIsacPayloadType = 103;
const SdpAudioFormat kIsacFormat = {"isac", 16000, 1};
const SdpAudioFormat kOpusFormat = {"opus", 48000, 2};
const SdpAudioFormat kG722Format = {"g722", 8000, 1};
const AudioCodecSpec kCodecSpecs[] = {
    {.format = kIsacFormat, .info = {16000, 1, 32000, 10000, 32000}},
    {.format = kOpusFormat, .info = {48000, 1, 32000, 6000, 510000}},
    {.format = kG722Format, .info = {16000, 1, 64000}}};

// TODO(dklee): This mirrors calculation in audio_send_stream.cc, which
// should be made more precise in the future. This can be changed when that
// logic is more accurate.
constexpr DataSize kOverheadPerPacket = DataSize::Bytes(20 + 8 + 10 + 12);
constexpr TimeDelta kMinFrameLength = TimeDelta::Millis(20);
constexpr TimeDelta kMaxFrameLength = TimeDelta::Millis(120);
constexpr DataRate kMinOverheadRate = kOverheadPerPacket / kMaxFrameLength;
constexpr DataRate kMaxOverheadRate = kOverheadPerPacket / kMinFrameLength;

class MockLimitObserver : public BitrateAllocator::LimitObserver {
 public:
  MOCK_METHOD(void,
              OnAllocationLimitsChanged,
              (BitrateAllocationLimits),
              (override));
};

std::unique_ptr<MockAudioEncoder> SetupAudioEncoderMock(
    const SdpAudioFormat& format) {
  for (const auto& spec : kCodecSpecs) {
    if (format == spec.format) {
      auto encoder = std::make_unique<NiceMock<MockAudioEncoder>>();
      ON_CALL(*encoder, SampleRateHz)
          .WillByDefault(Return(spec.info.sample_rate_hz));
      ON_CALL(*encoder, NumChannels)
          .WillByDefault(Return(spec.info.num_channels));
      ON_CALL(*encoder, RtpTimestampRateHz)
          .WillByDefault(Return(spec.format.clockrate_hz));
      ON_CALL(*encoder, GetFrameLengthRange)
          .WillByDefault(Return(
              std::make_pair(TimeDelta::Millis(20), TimeDelta::Millis(120))));
      return encoder;
    }
  }
  return nullptr;
}

scoped_refptr<MockAudioEncoderFactory> SetupEncoderFactoryMock() {
  scoped_refptr<MockAudioEncoderFactory> factory =
      make_ref_counted<MockAudioEncoderFactory>();
  ON_CALL(*factory, GetSupportedEncoders)
      .WillByDefault(Return(std::vector<AudioCodecSpec>(
          std::begin(kCodecSpecs), std::end(kCodecSpecs))));
  ON_CALL(*factory, QueryAudioEncoder)
      .WillByDefault(
          [](const SdpAudioFormat& format) -> std::optional<AudioCodecInfo> {
            for (const auto& spec : kCodecSpecs) {
              if (format == spec.format) {
                return spec.info;
              }
            }
            return std::nullopt;
          });
  ON_CALL(*factory, Create).WillByDefault(WithArg<1>(&SetupAudioEncoderMock));
  return factory;
}

class ConfigHelper {
 public:
  ConfigHelper(bool audio_bwe_enabled,
               bool expect_set_encoder_call,
               bool use_null_audio_processing)
      : field_trials_(CreateTestFieldTrials()),
        stream_config_(/*send_transport=*/nullptr),
        audio_processing_(
            use_null_audio_processing
                ? nullptr
                : make_ref_counted<NiceMock<MockAudioProcessing>>()),
        audio_encoder_(nullptr) {
    AudioState::Config config;
    config.audio_mixer = AudioMixerImpl::Create();
    config.audio_processing = audio_processing_;
    config.audio_device_module = make_ref_counted<MockAudioDeviceModule>();
    audio_state_ = AudioState::Create(config);

    SetupDefaultChannelSend(audio_bwe_enabled);
    SetupMockForSetupSendCodec(expect_set_encoder_call);
    SetupMockForCallEncoder();

    // Use ISAC as default codec so as to prevent unnecessary `channel_proxy_`
    // calls from the default ctor behavior.
    stream_config_.send_codec_spec =
        AudioSendStream::Config::SendCodecSpec(kIsacPayloadType, kIsacFormat);
    stream_config_.rtp.ssrc = kSsrc;
    stream_config_.rtp.csrcs.assign(kCsrcs.begin(), kCsrcs.end());
    stream_config_.rtp.c_name = kCName;
    stream_config_.rtp.extensions.push_back(
        RtpExtension(RtpExtension::kAudioLevelUri, kAudioLevelId));
    stream_config_.include_in_congestion_control_allocation = audio_bwe_enabled;

    stream_config_.encoder_factory = SetupEncoderFactoryMock();
    stream_config_.min_bitrate_bps = 10000;
    stream_config_.max_bitrate_bps = 65000;
  }

  std::unique_ptr<internal::AudioSendStream> CreateAudioSendStream() {
    return std::make_unique<internal::AudioSendStream>(
        CreateEnvironment(&field_trials_, time_controller_.GetClock(),
                          time_controller_.GetTaskQueueFactory()),
        stream_config_, audio_state_, &rtp_transport_, &bitrate_allocator_,
        std::unique_ptr<voe::ChannelSendInterface>(channel_send_));
  }

  AudioSendStream::Config& config() { return stream_config_; }
  MockAudioEncoderFactory& mock_encoder_factory() {
    return *static_cast<MockAudioEncoderFactory*>(
        stream_config_.encoder_factory.get());
  }
  MockRtpRtcpInterface* rtp_rtcp() { return &rtp_rtcp_; }
  MockChannelSend* channel_send() { return channel_send_; }
  RtpTransportControllerSendInterface* transport() { return &rtp_transport_; }
  MockBitrateAllocator* bitrate_allocator() { return &bitrate_allocator_; }

  void SetupDefaultChannelSend(bool audio_bwe_enabled) {
    EXPECT_TRUE(channel_send_ == nullptr);
    channel_send_ = new ::testing::StrictMock<MockChannelSend>();
    EXPECT_CALL(*channel_send_, GetRtpRtcp()).WillRepeatedly([this]() {
      return &this->rtp_rtcp_;
    });
    EXPECT_CALL(rtp_rtcp_, SSRC).WillRepeatedly(Return(kSsrc));
    EXPECT_CALL(*channel_send_, SetRTCP_CNAME(StrEq(kCName))).Times(1);
    EXPECT_CALL(*channel_send_, SetFrameEncryptor(_)).Times(1);
    EXPECT_CALL(*channel_send_, SetSframeOptions).Times(1);
    EXPECT_CALL(*channel_send_, SetEncoderToPacketizerFrameTransformer(_))
        .Times(1);
    EXPECT_CALL(rtp_rtcp_, SetExtmapAllowMixed(false)).Times(1);
    EXPECT_CALL(*channel_send_, SetCsrcs(ElementsAreArray(kCsrcs))).Times(1);
    EXPECT_CALL(*channel_send_,
                SetSendAudioLevelIndicationStatus(kAudioLevelId))
        .Times(1);
    EXPECT_CALL(rtp_transport_, GetRtcpObserver)
        .WillRepeatedly(Return(&rtcp_observer_));
    EXPECT_CALL(*channel_send_,
                RegisterSenderCongestionControlObjects(&rtp_transport_))
        .Times(1);
    EXPECT_CALL(*channel_send_, ResetSenderCongestionControlObjects()).Times(1);
  }

  void SetupMockForSetupSendCodec(bool expect_set_encoder_call) {
    if (expect_set_encoder_call) {
      EXPECT_CALL(*channel_send_, SetEncoder)
          .WillOnce([this](int /* payload_type */,
                           const SdpAudioFormat& /* format */,
                           std::unique_ptr<AudioEncoder> encoder) {
            this->audio_encoder_ = std::move(encoder);
            return true;
          });
    }
  }

  void SetupMockForCallEncoder() {
    // Let ModifyEncoder to invoke mock audio encoder.
    EXPECT_CALL(*channel_send_, CallEncoder(_))
        .WillRepeatedly([this](FunctionView<void(AudioEncoder*)> modifier) {
          if (this->audio_encoder_)
            modifier(this->audio_encoder_.get());
        });
  }

  void SetupMockForSendTelephoneEvent() {
    EXPECT_TRUE(channel_send_);
    EXPECT_CALL(*channel_send_, SetSendTelephoneEventPayloadType(
                                    kTelephoneEventPayloadType,
                                    kTelephoneEventPayloadFrequency));
    EXPECT_CALL(
        *channel_send_,
        SendTelephoneEventOutband(kTelephoneEventCode, kTelephoneEventDuration))
        .WillOnce(Return(true));
  }

  void SetupMockForGetStats(bool use_null_audio_processing) {
    std::vector<ReportBlockData> report_blocks;
    ReportBlockData block;
    block.set_source_ssrc(780);
    block.set_fraction_lost_raw(kFractionLost);
    block.set_cumulative_lost(kCumulativeLost);
    block.set_jitter(kInterarrivalJitter);
    report_blocks.push_back(block);  // Has wrong SSRC.
    block.set_source_ssrc(kSsrc);
    report_blocks.push_back(block);  // Correct block.
    block.set_fraction_lost_raw(0);
    report_blocks.push_back(block);  // Duplicate SSRC, bad fraction_lost.

    EXPECT_TRUE(channel_send_);
    EXPECT_CALL(*channel_send_, GetRTCPStatistics())
        .WillRepeatedly(Return(kChannelStats));
    EXPECT_CALL(*channel_send_, GetRemoteRTCPReportBlocks())
        .WillRepeatedly(Return(report_blocks));
    EXPECT_CALL(*channel_send_, GetANAStatistics())
        .WillRepeatedly(Return(ANAStats()));
    EXPECT_CALL(*channel_send_, GetTargetBitrate()).WillRepeatedly(Return(0));

    audio_processing_stats_.echo_return_loss = kEchoReturnLoss;
    audio_processing_stats_.echo_return_loss_enhancement =
        kEchoReturnLossEnhancement;
    audio_processing_stats_.delay_median_ms = kEchoDelayMedian;
    audio_processing_stats_.delay_standard_deviation_ms = kEchoDelayStdDev;
    audio_processing_stats_.divergent_filter_fraction =
        kDivergentFilterFraction;
    audio_processing_stats_.residual_echo_likelihood = kResidualEchoLikelihood;
    audio_processing_stats_.residual_echo_likelihood_recent_max =
        kResidualEchoLikelihoodMax;
    if (!use_null_audio_processing) {
      ASSERT_TRUE(audio_processing_);
      EXPECT_CALL(*audio_processing_, GetStatistics(true))
          .WillRepeatedly(Return(audio_processing_stats_));
    }
  }

  FieldTrials& field_trials() { return field_trials_; }

 private:
  FieldTrials field_trials_;
  RealTimeController time_controller_;
  scoped_refptr<AudioState> audio_state_;
  AudioSendStream::Config stream_config_;
  ::testing::StrictMock<MockChannelSend>* channel_send_ = nullptr;
  scoped_refptr<MockAudioProcessing> audio_processing_;
  AudioProcessingStats audio_processing_stats_;
  ::testing::StrictMock<MockNetworkLinkRtcpObserver> rtcp_observer_;
  ::testing::NiceMock<MockRtpTransportControllerSend> rtp_transport_;
  ::testing::NiceMock<MockRtpRtcpInterface> rtp_rtcp_;
  ::testing::NiceMock<MockLimitObserver> limit_observer_;
  ::testing::NiceMock<MockBitrateAllocator> bitrate_allocator_;
  std::unique_ptr<AudioEncoder> audio_encoder_;
};

// The audio level ranges linearly [0,32767].
std::unique_ptr<AudioFrame> CreateAudioFrame1kHzSineWave(int16_t audio_level,
                                                         int duration_ms,
                                                         int sample_rate_hz,
                                                         size_t num_channels) {
  size_t samples_per_channel = sample_rate_hz / (1000 / duration_ms);
  std::vector<int16_t> audio_data(samples_per_channel * num_channels, 0);
  std::unique_ptr<AudioFrame> audio_frame = std::make_unique<AudioFrame>();
  audio_frame->UpdateFrame(0 /* RTP timestamp */, &audio_data[0],
                           samples_per_channel, sample_rate_hz,
                           AudioFrame::SpeechType::kNormalSpeech,
                           AudioFrame::VADActivity::kVadUnknown, num_channels);
  SineWaveGenerator wave_generator(1000.0, audio_level);
  wave_generator.GenerateNextFrame(audio_frame.get());
  return audio_frame;
}

}  // namespace

TEST(AudioSendStreamTest, ConfigToString) {
  AudioSendStream::Config config(/*send_transport=*/nullptr);
  config.rtp.ssrc = kSsrc;
  config.rtp.csrcs.assign(kCsrcs.begin(), kCsrcs.end());
  config.rtp.c_name = kCName;
  config.min_bitrate_bps = 12000;
  config.max_bitrate_bps = 34000;
  config.has_dscp = true;
  config.send_codec_spec =
      AudioSendStream::Config::SendCodecSpec(kIsacPayloadType, kIsacFormat);
  config.send_codec_spec->nack_enabled = true;
  config.send_codec_spec->cng_payload_type = 42;
  config.send_codec_spec->red_payload_type = 43;
  config.encoder_factory = MockAudioEncoderFactory::CreateUnusedFactory();
  config.rtp.extmap_allow_mixed = true;
  config.rtp.extensions.push_back(
      RtpExtension(RtpExtension::kAudioLevelUri, kAudioLevelId));
  config.rtcp_report_interval_ms = 2500;
  EXPECT_EQ(
      "{rtp: {ssrc: 1234, csrcs: [5678, 9012], extmap-allow-mixed: true, "
      "extensions: [{uri: urn:ietf:params:rtp-hdrext:ssrc-audio-level, "
      "id: 2}], c_name: foo_name}, rtcp_report_interval_ms: 2500, "
      "send_transport: null, "
      "min_bitrate_bps: 12000, max_bitrate_bps: 34000, has "
      "audio_network_adaptor_config: false, has_dscp: true, "
      "send_codec_spec: {nack_enabled: true, "
      "enable_non_sender_rtt: false, cng_payload_type: 42, "
      "red_payload_type: 43, payload_type: 103, "
      "format: {name: isac, clockrate_hz: 16000, num_channels: 1, "
      "parameters: {}}}, sframe_required: false}",
      config.ToString());
}

TEST(AudioSendStreamTest, ConstructDestruct) {
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(false, true, use_null_audio_processing);
    auto send_stream = helper.CreateAudioSendStream();
  }
}

TEST(AudioSendStreamTest, SendTelephoneEvent) {
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(false, true, use_null_audio_processing);
    auto send_stream = helper.CreateAudioSendStream();
    helper.SetupMockForSendTelephoneEvent();
    EXPECT_TRUE(send_stream->SendTelephoneEvent(
        kTelephoneEventPayloadType, kTelephoneEventPayloadFrequency,
        kTelephoneEventCode, kTelephoneEventDuration));
  }
}

TEST(AudioSendStreamTest, SetMuted) {
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(false, true, use_null_audio_processing);
    auto send_stream = helper.CreateAudioSendStream();
    EXPECT_CALL(*helper.channel_send(), SetInputMute(true));
    send_stream->SetMuted(true);
  }
}

TEST(AudioSendStreamTest, SetCsrcs) {
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(false, true, use_null_audio_processing);
    auto send_stream = helper.CreateAudioSendStream();

    std::vector<uint32_t> updated_csrcs = {4, 5, 6};
    helper.config().rtp.csrcs = updated_csrcs;
    EXPECT_CALL(*helper.channel_send(),
                SetCsrcs(ElementsAreArray(updated_csrcs)));
    send_stream->Reconfigure(helper.config(), nullptr);
  }
}

TEST(AudioSendStreamTest, AudioBweCorrectObjectsOnChannelProxy) {
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(true, true, use_null_audio_processing);
    auto send_stream = helper.CreateAudioSendStream();
  }
}

TEST(AudioSendStreamTest, NoAudioBweCorrectObjectsOnChannelProxy) {
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(false, true, use_null_audio_processing);
    auto send_stream = helper.CreateAudioSendStream();
  }
}

TEST(AudioSendStreamTest, GetStats) {
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(false, true, use_null_audio_processing);
    auto send_stream = helper.CreateAudioSendStream();
    helper.SetupMockForGetStats(use_null_audio_processing);
    AudioSendStream::Stats stats = send_stream->GetStats(true);
    EXPECT_EQ(kSsrc, stats.local_ssrc);
    EXPECT_EQ(kChannelStats.payload_bytes_sent, stats.payload_bytes_sent);
    EXPECT_EQ(kChannelStats.header_and_padding_bytes_sent,
              stats.header_and_padding_bytes_sent);
    EXPECT_EQ(kChannelStats.packets_sent, stats.packets_sent);
    EXPECT_EQ(stats.packets_lost, kCumulativeLost);
    EXPECT_FLOAT_EQ(stats.fraction_lost, Q8ToFloat(kFractionLost));
    EXPECT_EQ(kIsacFormat.name, stats.codec_name);
    EXPECT_EQ(stats.jitter_ms,
              static_cast<int32_t>(kInterarrivalJitter /
                                   (kIsacFormat.clockrate_hz / 1000)));
    EXPECT_EQ(kChannelStats.round_trip_time.ms(), stats.rtt_ms);
    EXPECT_EQ(0, stats.audio_level);
    EXPECT_EQ(0, stats.total_input_energy);
    EXPECT_EQ(0, stats.total_input_duration);

    if (!use_null_audio_processing) {
      EXPECT_EQ(kEchoDelayMedian, stats.apm_statistics.delay_median_ms);
      EXPECT_EQ(kEchoDelayStdDev,
                stats.apm_statistics.delay_standard_deviation_ms);
      EXPECT_EQ(kEchoReturnLoss, stats.apm_statistics.echo_return_loss);
      EXPECT_EQ(kEchoReturnLossEnhancement,
                stats.apm_statistics.echo_return_loss_enhancement);
      EXPECT_EQ(kDivergentFilterFraction,
                stats.apm_statistics.divergent_filter_fraction);
      EXPECT_EQ(kResidualEchoLikelihood,
                stats.apm_statistics.residual_echo_likelihood);
      EXPECT_EQ(kResidualEchoLikelihoodMax,
                stats.apm_statistics.residual_echo_likelihood_recent_max);
    }
  }
}

TEST(AudioSendStreamTest, GetStatsAudioLevel) {
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(false, true, use_null_audio_processing);
    auto send_stream = helper.CreateAudioSendStream();
    helper.SetupMockForGetStats(use_null_audio_processing);
    EXPECT_CALL(*helper.channel_send(), ProcessAndEncodeAudio)
        .Times(AnyNumber());

    constexpr int kSampleRateHz = 48000;
    constexpr size_t kNumChannels = 1;

    constexpr int16_t kSilentAudioLevel = 0;
    constexpr int16_t kMaxAudioLevel = 32767;  // Audio level is [0,32767].
    constexpr int kAudioFrameDurationMs = 10;

    // Process 10 audio frames (100 ms) of silence. After this, on the next
    // (11-th) frame, the audio level will be updated with the maximum audio
    // level of the first 11 frames. See AudioLevel.
    for (size_t i = 0; i < 10; ++i) {
      send_stream->SendAudioData(
          CreateAudioFrame1kHzSineWave(kSilentAudioLevel, kAudioFrameDurationMs,
                                       kSampleRateHz, kNumChannels));
    }
    AudioSendStream::Stats stats = send_stream->GetStats();
    EXPECT_EQ(kSilentAudioLevel, stats.audio_level);
    EXPECT_NEAR(0.0f, stats.total_input_energy, kTolerance);
    EXPECT_NEAR(0.1f, stats.total_input_duration,
                kTolerance);  // 100 ms = 0.1 s

    // Process 10 audio frames (100 ms) of maximum audio level.
    // Note that AudioLevel updates the audio level every 11th frame, processing
    // 10 frames above was needed to see a non-zero audio level here.
    for (size_t i = 0; i < 10; ++i) {
      send_stream->SendAudioData(CreateAudioFrame1kHzSineWave(
          kMaxAudioLevel, kAudioFrameDurationMs, kSampleRateHz, kNumChannels));
    }
    stats = send_stream->GetStats();
    EXPECT_EQ(kMaxAudioLevel, stats.audio_level);
    // Energy increases by energy*duration, where energy is audio level in
    // [0,1].
    EXPECT_NEAR(0.1f, stats.total_input_energy, kTolerance);  // 0.1 s of max
    EXPECT_NEAR(0.2f, stats.total_input_duration,
                kTolerance);  // 200 ms = 0.2 s
  }
}

TEST(AudioSendStreamTest, SendCodecAppliesAudioNetworkAdaptor) {
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(true, true, use_null_audio_processing);
    helper.config().send_codec_spec =
        AudioSendStream::Config::SendCodecSpec(0, kOpusFormat);
    const std::string kAnaConfigString = "abcde";
    const std::string kAnaReconfigString = "12345";

    helper.config().audio_network_adaptor_config = kAnaConfigString;

    EXPECT_CALL(helper.mock_encoder_factory(), Create)
        .WillOnce(WithArg<1>([&kAnaConfigString, &kAnaReconfigString](
                                 const SdpAudioFormat& format) {
          auto mock_encoder = SetupAudioEncoderMock(format);
          EXPECT_CALL(*mock_encoder,
                      EnableAudioNetworkAdaptor(StrEq(kAnaConfigString)))
              .WillOnce(Return(true));
          EXPECT_CALL(*mock_encoder,
                      EnableAudioNetworkAdaptor(StrEq(kAnaReconfigString)))
              .WillOnce(Return(true));
          return mock_encoder;
        }));

    auto send_stream = helper.CreateAudioSendStream();

    auto stream_config = helper.config();
    stream_config.audio_network_adaptor_config = kAnaReconfigString;

    send_stream->Reconfigure(stream_config, nullptr);
  }
}

TEST(AudioSendStreamTest, AudioNetworkAdaptorReceivesOverhead) {
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(true, true, use_null_audio_processing);
    helper.config().send_codec_spec =
        AudioSendStream::Config::SendCodecSpec(0, kOpusFormat);
    const std::string kAnaConfigString = "abcde";

    EXPECT_CALL(helper.mock_encoder_factory(), Create)
        .WillOnce(WithArg<1>([&kAnaConfigString](const SdpAudioFormat& format) {
          auto mock_encoder = SetupAudioEncoderMock(format);
          InSequence s;
          EXPECT_CALL(
              *mock_encoder,
              OnReceivedOverhead(Eq(kOverheadPerPacket.bytes<size_t>())));
          EXPECT_CALL(*mock_encoder,
                      EnableAudioNetworkAdaptor(StrEq(kAnaConfigString)))
              .WillOnce(Return(true));
          // Note: Overhead is received AFTER ANA has been enabled.
          EXPECT_CALL(
              *mock_encoder,
              OnReceivedOverhead(Eq(kOverheadPerPacket.bytes<size_t>())));
          return mock_encoder;
        }));
    EXPECT_CALL(*helper.rtp_rtcp(), ExpectedPerPacketOverhead)
        .WillRepeatedly(Return(kOverheadPerPacket.bytes<size_t>()));
    EXPECT_CALL(*helper.channel_send(), RegisterPacketOverhead);

    auto send_stream = helper.CreateAudioSendStream();

    auto stream_config = helper.config();
    stream_config.audio_network_adaptor_config = kAnaConfigString;

    send_stream->Reconfigure(stream_config, nullptr);
  }
}

// VAD is applied when codec is mono and the CNG frequency matches the codec
// clock rate.
TEST(AudioSendStreamTest, SendCodecCanApplyVad) {
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(false, false, use_null_audio_processing);
    helper.config().send_codec_spec =
        AudioSendStream::Config::SendCodecSpec(9, kG722Format);
    helper.config().send_codec_spec->cng_payload_type = 105;
    std::unique_ptr<AudioEncoder> stolen_encoder;
    EXPECT_CALL(*helper.channel_send(), SetEncoder)
        .WillOnce([&stolen_encoder](int /* payload_type */,
                                    const SdpAudioFormat& /* format */,
                                    std::unique_ptr<AudioEncoder> encoder) {
          stolen_encoder = std::move(encoder);
          return true;
        });
    EXPECT_CALL(*helper.channel_send(), RegisterCngPayloadType(105, 8000));

    auto send_stream = helper.CreateAudioSendStream();

    // We cannot truly determine if the encoder created is an AudioEncoderCng.
    // It is the only reasonable implementation that will return something from
    // ReclaimContainedEncoders, though.
    ASSERT_TRUE(stolen_encoder);
    EXPECT_FALSE(stolen_encoder->ReclaimContainedEncoders().empty());
  }
}

TEST(AudioSendStreamTest, DoesNotPassHigherBitrateThanMaxBitrate) {
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(false, true, use_null_audio_processing);
    auto send_stream = helper.CreateAudioSendStream();
    EXPECT_CALL(
        *helper.channel_send(),
        OnBitrateAllocation(
            Field(&BitrateAllocationUpdate::target_bitrate,
                  Eq(DataRate::BitsPerSec(helper.config().max_bitrate_bps)))));
    BitrateAllocationUpdate update;
    update.target_bitrate =
        DataRate::BitsPerSec(helper.config().max_bitrate_bps + 5000);
    update.packet_loss_ratio = 0;
    update.round_trip_time = TimeDelta::Millis(50);
    send_stream->OnBitrateUpdated(update);
  }
}

TEST(AudioSendStreamTest, SSBweTargetInRangeRespected) {
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(true, true, use_null_audio_processing);
    auto send_stream = helper.CreateAudioSendStream();
    EXPECT_CALL(
        *helper.channel_send(),
        OnBitrateAllocation(Field(
            &BitrateAllocationUpdate::target_bitrate,
            Eq(DataRate::BitsPerSec(helper.config().max_bitrate_bps - 5000)))));
    BitrateAllocationUpdate update;
    update.target_bitrate =
        DataRate::BitsPerSec(helper.config().max_bitrate_bps - 5000);
    send_stream->OnBitrateUpdated(update);
  }
}

TEST(AudioSendStreamTest, SSBweFieldTrialMinRespected) {
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(true, true, use_null_audio_processing);
    helper.field_trials().Set("WebRTC-Audio-Allocation",
                              "min:6kbps,max:64kbps");
    auto send_stream = helper.CreateAudioSendStream();
    EXPECT_CALL(
        *helper.channel_send(),
        OnBitrateAllocation(Field(&BitrateAllocationUpdate::target_bitrate,
                                  Eq(DataRate::KilobitsPerSec(6)))));
    BitrateAllocationUpdate update;
    update.target_bitrate = DataRate::KilobitsPerSec(1);
    send_stream->OnBitrateUpdated(update);
  }
}

TEST(AudioSendStreamTest, SSBweFieldTrialMaxRespected) {
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(true, true, use_null_audio_processing);
    helper.field_trials().Set("WebRTC-Audio-Allocation",
                              "min:6kbps,max:64kbps");
    auto send_stream = helper.CreateAudioSendStream();
    EXPECT_CALL(
        *helper.channel_send(),
        OnBitrateAllocation(Field(&BitrateAllocationUpdate::target_bitrate,
                                  Eq(DataRate::KilobitsPerSec(64)))));
    BitrateAllocationUpdate update;
    update.target_bitrate = DataRate::KilobitsPerSec(128);
    send_stream->OnBitrateUpdated(update);
  }
}

TEST(AudioSendStreamTest, SSBweWithOverhead) {
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(true, true, use_null_audio_processing);
    helper.field_trials().Set("WebRTC-Audio-LegacyOverhead", "Disabled");
    EXPECT_CALL(*helper.rtp_rtcp(), ExpectedPerPacketOverhead)
        .WillRepeatedly(Return(kOverheadPerPacket.bytes<size_t>()));
    EXPECT_CALL(*helper.channel_send(), RegisterPacketOverhead);
    auto send_stream = helper.CreateAudioSendStream();
    const DataRate bitrate =
        DataRate::BitsPerSec(helper.config().max_bitrate_bps) +
        kMaxOverheadRate;
    EXPECT_CALL(*helper.channel_send(),
                OnBitrateAllocation(Field(
                    &BitrateAllocationUpdate::target_bitrate, Eq(bitrate))));
    BitrateAllocationUpdate update;
    update.target_bitrate = bitrate;
    send_stream->OnBitrateUpdated(update);
  }
}

TEST(AudioSendStreamTest, SSBweWithOverheadMinRespected) {
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(true, true, use_null_audio_processing);
    helper.field_trials().Set("WebRTC-Audio-LegacyOverhead", "Disabled");
    helper.field_trials().Set("WebRTC-Audio-Allocation",
                              "min:6kbps,max:64kbps");
    EXPECT_CALL(*helper.rtp_rtcp(), ExpectedPerPacketOverhead)
        .WillRepeatedly(Return(kOverheadPerPacket.bytes<size_t>()));
    EXPECT_CALL(*helper.channel_send(), RegisterPacketOverhead);
    auto send_stream = helper.CreateAudioSendStream();
    const DataRate bitrate = DataRate::KilobitsPerSec(6) + kMinOverheadRate;
    EXPECT_CALL(*helper.channel_send(),
                OnBitrateAllocation(Field(
                    &BitrateAllocationUpdate::target_bitrate, Eq(bitrate))));
    BitrateAllocationUpdate update;
    update.target_bitrate = DataRate::KilobitsPerSec(1);
    send_stream->OnBitrateUpdated(update);
  }
}

TEST(AudioSendStreamTest, SSBweWithOverheadMaxRespected) {
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(true, true, use_null_audio_processing);
    helper.field_trials().Set("WebRTC-Audio-LegacyOverhead", "Disabled");
    helper.field_trials().Set("WebRTC-Audio-Allocation",
                              "min:6kbps,max:64kbps");
    EXPECT_CALL(*helper.rtp_rtcp(), ExpectedPerPacketOverhead)
        .WillRepeatedly(Return(kOverheadPerPacket.bytes<size_t>()));
    EXPECT_CALL(*helper.channel_send(), RegisterPacketOverhead);
    auto send_stream = helper.CreateAudioSendStream();
    const DataRate bitrate = DataRate::KilobitsPerSec(64) + kMaxOverheadRate;
    EXPECT_CALL(*helper.channel_send(),
                OnBitrateAllocation(Field(
                    &BitrateAllocationUpdate::target_bitrate, Eq(bitrate))));
    BitrateAllocationUpdate update;
    update.target_bitrate = DataRate::KilobitsPerSec(128);
    send_stream->OnBitrateUpdated(update);
  }
}


// Test that AudioSendStream doesn't recreate the encoder unnecessarily.
TEST(AudioSendStreamTest, DontRecreateEncoder) {
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(false, false, use_null_audio_processing);
    // WillOnce is (currently) the default used by ConfigHelper if asked to set
    // an expectation for SetEncoder. Since this behavior is essential for this
    // test to be correct, it's instead set-up manually here. Otherwise a simple
    // change to ConfigHelper (say to WillRepeatedly) would silently make this
    // test useless.
    EXPECT_CALL(*helper.channel_send(), SetEncoder).WillOnce(Return());

    EXPECT_CALL(*helper.channel_send(), RegisterCngPayloadType(105, 8000));

    helper.config().send_codec_spec =
        AudioSendStream::Config::SendCodecSpec(9, kG722Format);
    helper.config().send_codec_spec->cng_payload_type = 105;
    auto send_stream = helper.CreateAudioSendStream();
    send_stream->Reconfigure(helper.config(), nullptr);
  }
}

TEST(AudioSendStreamTest, ConfiguresTransportCcIfExtensionNegotiated) {
  ConfigHelper helper(true, true, false);
  helper.config().rtp.extensions.push_back(RtpExtension(
      RtpExtension::kTransportSequenceNumberUri, kTransportSequenceNumberId));

  EXPECT_CALL(*helper.rtp_rtcp(),
              RegisterRtpHeaderExtension(TransportSequenceNumber::Uri(),
                                         kTransportSequenceNumberId))
      .Times(1);
  auto send_stream = helper.CreateAudioSendStream();
}

TEST(AudioSendStreamTest, ReconfigureTransportCcResetsFirst) {
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(false, true, use_null_audio_processing);
    auto send_stream = helper.CreateAudioSendStream();
    auto new_config = helper.config();
    new_config.rtp.extensions.push_back(RtpExtension(
        RtpExtension::kTransportSequenceNumberUri, kTransportSequenceNumberId));

    EXPECT_CALL(*helper.rtp_rtcp(),
                RegisterRtpHeaderExtension(TransportSequenceNumber::Uri(),
                                           kTransportSequenceNumberId))
        .Times(1);
    {
      ::testing::InSequence seq;
      EXPECT_CALL(*helper.channel_send(), ResetSenderCongestionControlObjects())
          .Times(1);
      EXPECT_CALL(*helper.channel_send(),
                  RegisterSenderCongestionControlObjects(helper.transport()))
          .Times(1);
    }

    send_stream->Reconfigure(new_config, nullptr);
  }
}

TEST(AudioSendStreamTest, ReconfigureTransportCcDeregistersExtension) {
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(false, true, use_null_audio_processing);
    helper.config().rtp.extensions.push_back(RtpExtension(
        RtpExtension::kTransportSequenceNumberUri, kTransportSequenceNumberId));

    EXPECT_CALL(*helper.rtp_rtcp(),
                RegisterRtpHeaderExtension(TransportSequenceNumber::Uri(),
                                           kTransportSequenceNumberId))
        .Times(1);
    auto send_stream = helper.CreateAudioSendStream();

    // Reconfigure with a different transport-cc extension ID.
    constexpr RtpHeaderExtensionId kNewTransportSequenceNumberId(
        kTransportSequenceNumberId.value() + 1);
    auto new_config = helper.config();
    new_config.rtp.extensions.clear();
    new_config.rtp.extensions.push_back(
        RtpExtension(RtpExtension::kAudioLevelUri, kAudioLevelId));
    new_config.rtp.extensions.push_back(
        RtpExtension(RtpExtension::kTransportSequenceNumberUri,
                     kNewTransportSequenceNumberId));

    // Expect deregistration before re-registration with the new ID.
    EXPECT_CALL(*helper.rtp_rtcp(), DeregisterSendRtpHeaderExtension(
                                        TransportSequenceNumber::Uri()))
        .Times(1);
    EXPECT_CALL(*helper.rtp_rtcp(),
                RegisterRtpHeaderExtension(TransportSequenceNumber::Uri(),
                                           kNewTransportSequenceNumberId))
        .Times(1);
    {
      ::testing::InSequence seq;
      EXPECT_CALL(*helper.channel_send(), ResetSenderCongestionControlObjects())
          .Times(1);
      EXPECT_CALL(*helper.channel_send(),
                  RegisterSenderCongestionControlObjects(helper.transport()))
          .Times(1);
    }

    send_stream->Reconfigure(new_config, nullptr);
  }
}

TEST(AudioSendStreamTest, OnTransportOverheadChanged) {
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(false, true, use_null_audio_processing);
    auto send_stream = helper.CreateAudioSendStream();
    auto new_config = helper.config();

    // CallEncoder will be called on overhead change.
    EXPECT_CALL(*helper.channel_send(), CallEncoder);
    EXPECT_CALL(*helper.channel_send(), RegisterPacketOverhead);

    const size_t transport_overhead_per_packet_bytes = 333;
    BitrateAllocationUpdate update;
    update.packet_overhead =
        DataSize::Bytes(transport_overhead_per_packet_bytes);
    EXPECT_CALL(*helper.channel_send(), OnBitrateAllocation);
    send_stream->OnBitrateUpdated(update);

    EXPECT_EQ(transport_overhead_per_packet_bytes,
              send_stream->TestOnlyGetPerPacketOverheadBytes());
  }
}

TEST(AudioSendStreamTest, DoesntCallEncoderWhenOverheadUnchanged) {
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(false, true, use_null_audio_processing);
    auto send_stream = helper.CreateAudioSendStream();
    auto new_config = helper.config();

    EXPECT_CALL(*helper.channel_send(), RegisterPacketOverhead).Times(2);

    // CallEncoder will be called on overhead change.
    EXPECT_CALL(*helper.channel_send(), CallEncoder);
    const size_t transport_overhead_per_packet_bytes = 333;
    BitrateAllocationUpdate update;
    update.packet_overhead =
        DataSize::Bytes(transport_overhead_per_packet_bytes);
    EXPECT_CALL(*helper.channel_send(), OnBitrateAllocation).Times(3);
    send_stream->OnBitrateUpdated(update);

    // Set the same overhead again, CallEncoder should not be called again.
    EXPECT_CALL(*helper.channel_send(), CallEncoder).Times(0);
    send_stream->OnBitrateUpdated(update);

    // New overhead, call CallEncoder again
    EXPECT_CALL(*helper.channel_send(), CallEncoder);
    update.packet_overhead =
        DataSize::Bytes(transport_overhead_per_packet_bytes + 1);
    send_stream->OnBitrateUpdated(update);
  }
}

TEST(AudioSendStreamTest, AudioOverheadChanged) {
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(false, true, use_null_audio_processing);
    const size_t audio_overhead_per_packet_bytes = 555;
    EXPECT_CALL(*helper.rtp_rtcp(), ExpectedPerPacketOverhead)
        .WillRepeatedly(Return(audio_overhead_per_packet_bytes));
    EXPECT_CALL(*helper.channel_send(), RegisterPacketOverhead).Times(2);
    auto send_stream = helper.CreateAudioSendStream();

    BitrateAllocationUpdate update;
    update.target_bitrate =
        DataRate::BitsPerSec(helper.config().max_bitrate_bps) +
        kMaxOverheadRate;
    EXPECT_CALL(*helper.channel_send(), OnBitrateAllocation);
    send_stream->OnBitrateUpdated(update);

    EXPECT_EQ(audio_overhead_per_packet_bytes,
              send_stream->TestOnlyGetPerPacketOverheadBytes());

    EXPECT_CALL(*helper.rtp_rtcp(), ExpectedPerPacketOverhead)
        .WillRepeatedly(Return(audio_overhead_per_packet_bytes + 20));
    // RTP overhead can only change in response to RTCP or configuration change.
    send_stream->Reconfigure(helper.config(), nullptr);
    EXPECT_CALL(*helper.channel_send(), OnBitrateAllocation);
    send_stream->OnBitrateUpdated(update);

    EXPECT_EQ(audio_overhead_per_packet_bytes + 20,
              send_stream->TestOnlyGetPerPacketOverheadBytes());
  }
}

TEST(AudioSendStreamTest, OnAudioAndTransportOverheadChanged) {
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(false, true, use_null_audio_processing);
    const size_t audio_overhead_per_packet_bytes = 555;
    EXPECT_CALL(*helper.rtp_rtcp(), ExpectedPerPacketOverhead)
        .WillRepeatedly(Return(audio_overhead_per_packet_bytes));
    EXPECT_CALL(*helper.channel_send(), RegisterPacketOverhead).Times(2);
    auto send_stream = helper.CreateAudioSendStream();
    auto new_config = helper.config();

    const size_t transport_overhead_per_packet_bytes = 333;
    BitrateAllocationUpdate update;
    update.target_bitrate =
        DataRate::BitsPerSec(helper.config().max_bitrate_bps) +
        kMaxOverheadRate;
    update.packet_overhead =
        DataSize::Bytes(transport_overhead_per_packet_bytes);
    EXPECT_CALL(*helper.channel_send(), OnBitrateAllocation);
    send_stream->OnBitrateUpdated(update);

    EXPECT_EQ(
        transport_overhead_per_packet_bytes + audio_overhead_per_packet_bytes,
        send_stream->TestOnlyGetPerPacketOverheadBytes());
  }
}

// Validates that reconfiguring the AudioSendStream with a Frame encryptor
// correctly reconfigures on the object without crashing.
TEST(AudioSendStreamTest, ReconfigureWithFrameEncryptor) {
  for (bool use_null_audio_processing : {false, true}) {
    ConfigHelper helper(false, true, use_null_audio_processing);
    auto send_stream = helper.CreateAudioSendStream();
    auto new_config = helper.config();

    scoped_refptr<FrameEncryptorInterface> mock_frame_encryptor_0(
        make_ref_counted<MockFrameEncryptor>());
    new_config.frame_encryptor = mock_frame_encryptor_0;
    EXPECT_CALL(*helper.channel_send(), SetFrameEncryptor(Ne(nullptr)))
        .Times(1);
    send_stream->Reconfigure(new_config, nullptr);

    // Not updating the frame encryptor shouldn't force it to reconfigure.
    EXPECT_CALL(*helper.channel_send(), SetFrameEncryptor(_)).Times(0);
    send_stream->Reconfigure(new_config, nullptr);

    // Updating frame encryptor to a new object should force a call to the
    // proxy.
    scoped_refptr<FrameEncryptorInterface> mock_frame_encryptor_1(
        make_ref_counted<MockFrameEncryptor>());
    new_config.frame_encryptor = mock_frame_encryptor_1;
    new_config.crypto_options.sframe.require_frame_encryption = true;
    EXPECT_CALL(*helper.channel_send(), SetFrameEncryptor(Ne(nullptr)))
        .Times(1);
    send_stream->Reconfigure(new_config, nullptr);
  }
}

TEST(AudioSendStreamTest, ReconfigureWithSframeOptions) {
  ConfigHelper helper(/*audio_bwe_enabled=*/false,
                      /*expect_set_encoder_call=*/true,
                      /*use_null_audio_processing=*/false);
  std::unique_ptr<internal::AudioSendStream> send_stream =
      helper.CreateAudioSendStream();
  AudioSendStream::Config new_config = helper.config();
  const SframeSendOptions options = {.required = true};
  new_config.sframe_options = options;

  EXPECT_CALL(*helper.channel_send(), SetSframeOptions(Eq(options))).Times(1);
  send_stream->Reconfigure(new_config, nullptr);

  EXPECT_CALL(*helper.channel_send(), SetSframeOptions).Times(0);
  send_stream->Reconfigure(new_config, nullptr);
}

TEST(AudioSendStreamTest, DefaultsHonorsPriorityBitrate) {
  ConfigHelper helper(true, true, true);
  helper.field_trials().Set("WebRTC-Audio-Allocation", "prio_rate:20");
  auto send_stream = helper.CreateAudioSendStream();
  EXPECT_CALL(*helper.bitrate_allocator(), AddObserver(send_stream.get(), _))
      .WillOnce(
          [&](BitrateAllocatorObserver*, MediaStreamAllocationConfig config) {
            EXPECT_EQ(config.priority_bitrate_bps, 20000);
          });
  EXPECT_CALL(*helper.channel_send(), StartSend());
  send_stream->Start();
  EXPECT_CALL(*helper.channel_send(), StopSend());
  send_stream->Stop();
}

TEST(AudioSendStreamTest, DefaultsToContributeUnusedBitrate) {
  ConfigHelper helper(true, true, true);
  auto send_stream = helper.CreateAudioSendStream();
  EXPECT_CALL(
      *helper.bitrate_allocator(),
      AddObserver(send_stream.get(),
                  Field(&MediaStreamAllocationConfig::rate_elasticity,
                        TrackRateElasticity::kCanContributeUnusedRate)));
  EXPECT_CALL(*helper.channel_send(), StartSend());
  send_stream->Start();
  EXPECT_CALL(*helper.channel_send(), StopSend());
  send_stream->Stop();
}

TEST(AudioSendStreamTest, OverridesPriorityBitrate) {
  ConfigHelper helper(true, true, true);
  helper.field_trials().Set("WebRTC-Audio-Allocation", "prio_rate:20");
  helper.field_trials().Set("WebRTC-Audio-PriorityBitrate", "Disabled");
  auto send_stream = helper.CreateAudioSendStream();
  EXPECT_CALL(*helper.bitrate_allocator(), AddObserver(send_stream.get(), _))
      .WillOnce(
          [&](BitrateAllocatorObserver*, MediaStreamAllocationConfig config) {
            EXPECT_EQ(config.priority_bitrate_bps, 0);
          });
  EXPECT_CALL(*helper.channel_send(), StartSend());
  send_stream->Start();
  EXPECT_CALL(*helper.channel_send(), StopSend());
  send_stream->Stop();
}

TEST(AudioSendStreamTest, UseEncoderBitrateRange) {
  ConfigHelper helper(true, true, true);
  std::pair<DataRate, DataRate> bitrate_range{DataRate::BitsPerSec(5000),
                                              DataRate::BitsPerSec(10000)};
  EXPECT_CALL(helper.mock_encoder_factory(), Create)
      .WillOnce(WithArg<1>([&](const SdpAudioFormat& format) {
        auto mock_encoder = SetupAudioEncoderMock(format);
        EXPECT_CALL(*mock_encoder, GetBitrateRange)
            .WillRepeatedly(Return(bitrate_range));
        return mock_encoder;
      }));
  auto send_stream = helper.CreateAudioSendStream();
  EXPECT_CALL(*helper.bitrate_allocator(), AddObserver(send_stream.get(), _))
      .WillOnce(
          [&](BitrateAllocatorObserver*, MediaStreamAllocationConfig config) {
            EXPECT_EQ(config.min_bitrate_bps, bitrate_range.first.bps());
            EXPECT_EQ(config.max_bitrate_bps, bitrate_range.second.bps());
          });
  EXPECT_CALL(*helper.channel_send(), StartSend());
  send_stream->Start();
  EXPECT_CALL(*helper.channel_send(), StopSend());
  send_stream->Stop();
}

// Verifies that ReconfigureBitrateObserver() preserves the
// BitrateAllocator registration when the WebRTC-Audio-ABWENoTWCC field trial
// is enabled and TWCC is not negotiated for audio (i.e.
// `include_in_congestion_control_allocation` is false). The Start() path
// already short-circuits on `allocate_audio_without_feedback_`; this test
// ensures the Reconfigure() path applies the same short-circuit.
TEST(AudioSendStreamTest, AbweNoTwccTrialKeepsBaRegistrationOnReconfigure) {
  ConfigHelper helper(/*audio_bwe_enabled=*/false,
                      /*expect_set_encoder_call=*/true,
                      /*use_null_audio_processing=*/true);
  helper.field_trials().Set("WebRTC-Audio-ABWENoTWCC", "Enabled");
  auto send_stream = helper.CreateAudioSendStream();

  // Start() registers the stream with the BitrateAllocator.
  EXPECT_CALL(*helper.bitrate_allocator(), AddObserver(send_stream.get(), _))
      .Times(1);
  EXPECT_CALL(*helper.channel_send(), StartSend());
  send_stream->Start();

  // Reconfigure with a different bitrate range. The observer should be
  // re-added (update path) and must not be removed.
  auto new_config = helper.config();
  new_config.min_bitrate_bps = 12000;
  new_config.max_bitrate_bps = 80000;
  EXPECT_CALL(*helper.bitrate_allocator(), AddObserver(send_stream.get(), _))
      .Times(1);
  EXPECT_CALL(*helper.bitrate_allocator(), RemoveObserver(send_stream.get()))
      .Times(0);
  send_stream->Reconfigure(new_config, nullptr);

  EXPECT_CALL(*helper.bitrate_allocator(), RemoveObserver(send_stream.get()))
      .Times(1);
  EXPECT_CALL(*helper.channel_send(), StopSend());
  send_stream->Stop();
}

}  // namespace test
}  // namespace webrtc
