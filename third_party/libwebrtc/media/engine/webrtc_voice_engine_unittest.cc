/*
 *  Copyright (c) 2008 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "media/engine/webrtc_voice_engine.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"
#include "api/audio/audio_processing.h"
#include "api/audio/builtin_audio_processing_builder.h"
#include "api/audio_codecs/audio_format.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/audio_options.h"
#include "api/call/audio_sink.h"
#include "api/call/transport.h"
#include "api/crypto/crypto_options.h"
#include "api/environment/environment.h"
#include "api/make_ref_counted.h"
#include "api/media_types.h"
#include "api/payload_type.h"
#include "api/priority.h"
#include "api/ref_count.h"
#include "api/rtc_error.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_headers.h"
#include "api/rtp_packet_infos.h"
#include "api/rtp_parameters.h"
#include "api/scoped_refptr.h"
#include "api/transport/bitrate_settings.h"
#include "api/units/timestamp.h"
#include "call/audio_receive_stream.h"
#include "call/audio_send_stream.h"
#include "call/audio_state.h"
#include "call/call.h"
#include "call/call_config.h"
#include "call/payload_type_picker.h"
#include "media/base/audio_source.h"
#include "media/base/codec.h"
#include "media/base/fake_network_interface.h"
#include "media/base/fake_rtp.h"
#include "media/base/media_channel.h"
#include "media/base/media_config.h"
#include "media/base/media_constants.h"
#include "media/base/media_engine.h"
#include "media/base/stream_params.h"
#include "media/engine/fake_webrtc_call.h"
#include "modules/audio_device/include/mock_audio_device.h"
#include "modules/audio_mixer/audio_mixer_impl.h"
#include "modules/audio_processing/include/mock_audio_processing.h"
#include "modules/rtp_rtcp/include/rtp_header_extension_map.h"
#include "modules/rtp_rtcp/source/rtp_header_extensions.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "rtc_base/byte_order.h"
#include "rtc_base/checks.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/dscp.h"
#include "rtc_base/numerics/safe_conversions.h"
#include "test/create_test_environment.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/mock_audio_decoder_factory.h"
#include "test/mock_audio_encoder_factory.h"
#include "test/run_loop.h"

namespace webrtc {
namespace {
using ::testing::_;
using ::testing::ContainerEq;
using ::testing::Contains;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::Mock;
using ::testing::Return;
using ::testing::ReturnPointee;
using ::testing::SaveArg;
using ::testing::StrictMock;
using ::testing::UnorderedElementsAreArray;

constexpr uint32_t kMaxUnsignaledRecvStreams = 4;

const Codec kPcmuCodec = CreateAudioCodec(0, "PCMU", 8000, 1);
const Codec kOpusCodec = CreateAudioCodec(111, "opus", 48000, 2);
const Codec kG722CodecVoE = CreateAudioCodec(9, "G722", 16000, 1);
const Codec kG722CodecSdp = CreateAudioCodec(9, "G722", 8000, 1);
const Codec kCn8000Codec = CreateAudioCodec(13, "CN", 8000, 1);
const Codec kCn16000Codec = CreateAudioCodec(105, "CN", 16000, 1);
const Codec kRed48000Codec = CreateAudioCodec(112, "RED", 48000, 2);
const Codec kTelephoneEventCodec1 =
    CreateAudioCodec(106, "telephone-event", 8000, 1);
const Codec kTelephoneEventCodec2 =
    CreateAudioCodec(107, "telephone-event", 32000, 1);
const Codec kUnknownCodec = CreateAudioCodec(127, "XYZ", 32000, 1);

constexpr uint32_t kSsrc0 = 0;
constexpr uint32_t kSsrc1 = 1;
constexpr uint32_t kSsrcX = 0x99;
constexpr uint32_t kSsrcY = 0x17;
constexpr uint32_t kSsrcZ = 0x42;
constexpr uint32_t kSsrcs4[] = {11, 200, 30, 44};

constexpr int kRtpHistoryMs = 5000;

constexpr AudioProcessing::Config::GainController1::Mode kDefaultAgcMode =
#if defined(WEBRTC_IOS) || defined(WEBRTC_ANDROID)
    AudioProcessing::Config::GainController1::kFixedDigital;
#else
    AudioProcessing::Config::GainController1::kAdaptiveAnalog;
#endif

constexpr AudioProcessing::Config::NoiseSuppression::Level kDefaultNsLevel =
    AudioProcessing::Config::NoiseSuppression::Level::kHigh;

// A test RAII helper class for `WebRtcVoiceEngine` which calls Init()
// in the constructor and Terminate() when it goes out of scope. It's similar to
// `absl::MakeCleanup()` except that it also issues a call during construction.
class AutoInitTerminate {
 public:
  explicit AutoInitTerminate(WebRtcVoiceEngine& engine) : engine_(engine) {
    engine_.Init();
  }
  ~AutoInitTerminate() { engine_.Terminate(); }

 private:
  WebRtcVoiceEngine& engine_;
};

void AdmSetupExpectations(test::MockAudioDeviceModule* adm) {
  RTC_DCHECK(adm);

  // Setup.
  EXPECT_CALL(*adm, Init()).WillOnce(Return(0));
  EXPECT_CALL(*adm, RegisterAudioCallback(_)).WillOnce(Return(0));
#if defined(WEBRTC_WIN)
  EXPECT_CALL(
      *adm,
      SetPlayoutDevice(::testing::Matcher<AudioDeviceModule::WindowsDeviceType>(
          AudioDeviceModule::kDefaultCommunicationDevice)))
      .WillOnce(Return(0));
#else
  EXPECT_CALL(*adm, SetPlayoutDevice(0)).WillOnce(Return(0));
#endif  // #if defined(WEBRTC_WIN)
  EXPECT_CALL(*adm, InitSpeaker()).WillOnce(Return(0));
  EXPECT_CALL(*adm, StereoPlayoutIsAvailable(::testing::_)).WillOnce(Return(0));
  EXPECT_CALL(*adm, SetStereoPlayout(false)).WillOnce(Return(0));
#if defined(WEBRTC_WIN)
  EXPECT_CALL(*adm,
              SetRecordingDevice(
                  ::testing::Matcher<AudioDeviceModule::WindowsDeviceType>(
                      AudioDeviceModule::kDefaultCommunicationDevice)))
      .WillOnce(Return(0));
#else
  EXPECT_CALL(*adm, SetRecordingDevice(0)).WillOnce(Return(0));
#endif  // #if defined(WEBRTC_WIN)
  EXPECT_CALL(*adm, InitMicrophone()).WillOnce(Return(0));
  EXPECT_CALL(*adm, StereoRecordingIsAvailable(::testing::_))
      .WillOnce(Return(0));
  EXPECT_CALL(*adm, SetStereoRecording(false)).WillOnce(Return(0));
  EXPECT_CALL(*adm, BuiltInAECIsAvailable()).WillOnce(Return(false));
  EXPECT_CALL(*adm, BuiltInAGCIsAvailable()).WillOnce(Return(false));
  EXPECT_CALL(*adm, BuiltInNSIsAvailable()).WillOnce(Return(false));

  // Teardown.
  EXPECT_CALL(*adm, StopPlayout()).WillOnce(Return(0));
  EXPECT_CALL(*adm, StopRecording()).WillOnce(Return(0));
  EXPECT_CALL(*adm, RegisterAudioCallback(nullptr)).WillOnce(Return(0));
  EXPECT_CALL(*adm, Terminate()).WillOnce(Return(0));
}

std::vector<Codec> AddIdToCodecs(PayloadTypePicker& pt_mapper,
                                 std::vector<Codec>&& codecs_in) {
  std::vector<Codec> codecs = std::move(codecs_in);
  for (Codec& codec : codecs) {
    if (codec.id == Codec::kIdNotSet) {
      auto id_or_error = pt_mapper.SuggestMapping(codec);
      EXPECT_TRUE(id_or_error.ok());
      if (id_or_error.ok()) {
        codec.id = id_or_error.value();
      }
    }
  }
  return codecs;
}

std::vector<Codec> ReceiveCodecsWithId(WebRtcVoiceEngine& engine) {
  PayloadTypePicker pt_mapper;
  std::vector<Codec> codecs = engine.LegacyRecvCodecs();
  return AddIdToCodecs(pt_mapper, std::move(codecs));
}

// Tests that our stub library "works".
TEST(WebRtcVoiceEngineTestStubLibrary, StartupShutdown) {
  Environment env = CreateTestEnvironment();
  for (bool use_null_apm : {false, true}) {
    scoped_refptr<test::MockAudioDeviceModule> adm =
        test::MockAudioDeviceModule::CreateStrict();
    AdmSetupExpectations(adm.get());
    scoped_refptr<StrictMock<test::MockAudioProcessing>> apm =
        use_null_apm
            ? nullptr
            : make_ref_counted<StrictMock<test::MockAudioProcessing>>();

    AudioProcessing::Config apm_config;
    if (!use_null_apm) {
      EXPECT_CALL(*apm, GetConfig()).WillRepeatedly(ReturnPointee(&apm_config));
      EXPECT_CALL(*apm, ApplyConfig(_)).WillRepeatedly(SaveArg<0>(&apm_config));
      EXPECT_CALL(*apm, DetachAecDump());
    }
    {
      WebRtcVoiceEngine engine(env, adm,
                               MockAudioEncoderFactory::CreateUnusedFactory(),
                               MockAudioDecoderFactory::CreateUnusedFactory(),
                               nullptr, apm, nullptr);
      AutoInitTerminate init_term(engine);
    }
  }
}

class FakeAudioSink : public AudioSinkInterface {
 public:
  void OnData(const Data& /* audio */) override {}
};

class FakeAudioSource : public AudioSource {
  void SetSink(Sink* /* sink */) override {}
};

class WebRtcVoiceEngineTestFake : public ::testing::TestWithParam<bool> {
 public:
  explicit WebRtcVoiceEngineTestFake(absl::string_view field_trials_string = "")
      : use_null_apm_(GetParam()),
        env_(CreateTestEnvironment({.field_trials = field_trials_string})),
        adm_(test::MockAudioDeviceModule::CreateStrict()),
        apm_(use_null_apm_
                 ? nullptr
                 : make_ref_counted<StrictMock<test::MockAudioProcessing>>()),
        call_(env_) {
    // AudioDeviceModule.
    AdmSetupExpectations(adm_.get());

    if (!use_null_apm_) {
      // AudioProcessing.
      EXPECT_CALL(*apm_, GetConfig())
          .WillRepeatedly(ReturnPointee(&apm_config_));
      EXPECT_CALL(*apm_, ApplyConfig(_))
          .WillRepeatedly(SaveArg<0>(&apm_config_));
      EXPECT_CALL(*apm_, DetachAecDump());
    }

    // Default Options.
    // TODO(kwiberg): We should use mock factories here, but a bunch of
    // the tests here probe the specific set of codecs provided by the builtin
    // factories. Those tests should probably be moved elsewhere.
    auto encoder_factory = CreateBuiltinAudioEncoderFactory();
    auto decoder_factory = CreateBuiltinAudioDecoderFactory();
    engine_ = std::make_unique<WebRtcVoiceEngine>(
        env_, adm_, encoder_factory, decoder_factory, nullptr, apm_, nullptr);
    engine_->Init();
    send_parameters_.codecs.push_back(kPcmuCodec);
    recv_parameters_.codecs.push_back(kPcmuCodec);

    if (!use_null_apm_) {
      // Default Options.
      VerifyEchoCancellationSettings(/*enabled=*/true);
      EXPECT_TRUE(IsHighPassFilterEnabled());
      EXPECT_TRUE(apm_config_.noise_suppression.enabled);
      EXPECT_EQ(apm_config_.noise_suppression.level, kDefaultNsLevel);
      VerifyGainControlEnabledCorrectly();
      VerifyGainControlDefaultSettings();
    }
  }

  ~WebRtcVoiceEngineTestFake() override { engine_->Terminate(); }

  bool SetupChannel() {
    send_channel_ = engine_->CreateSendChannel(env_, &call_, MediaConfig(),
                                               AudioOptions(), CryptoOptions());
    receive_channel_ = engine_->CreateReceiveChannel(
        env_, &call_, MediaConfig(), AudioOptions(), CryptoOptions(), nullptr,
        [](uint32_t, const RtpPacketInfos&, Timestamp) {});
    return true;
  }

  bool SetupRecvStream() {
    if (!SetupChannel()) {
      return false;
    }
    return AddRecvStream(kSsrcX);
  }

  bool SetupSendStream() {
    return SetupSendStream(StreamParams::CreateLegacy(kSsrcX));
  }

  bool SetupSendStream(const StreamParams& sp) {
    if (!SetupChannel()) {
      return false;
    }
    if (!send_channel_->AddSendStream(sp)) {
      return false;
    }
    if (!use_null_apm_) {
      EXPECT_CALL(*apm_, set_output_will_be_muted(false));
    }
    return send_channel_->SetAudioSend(kSsrcX, true, nullptr, &fake_source_);
  }

  bool AddRecvStream(uint32_t ssrc) {
    EXPECT_TRUE(receive_channel_);
    return receive_channel_->AddRecvStream(StreamParams::CreateLegacy(ssrc));
  }

  void SetupForMultiSendStream() {
    EXPECT_TRUE(SetupSendStream());
    // Remove stream added in Setup.
    EXPECT_TRUE(call_.GetAudioSendStream(kSsrcX));
    EXPECT_TRUE(send_channel_->RemoveSendStream(kSsrcX));
    // Verify the channel does not exist.
    EXPECT_FALSE(call_.GetAudioSendStream(kSsrcX));
  }

  void DeliverPacket(std::span<const uint8_t> data) {
    RtpPacketReceived packet;
    packet.Parse(data);
    receive_channel_->OnPacketReceived(packet);
    run_loop_.Flush();
  }

  const FakeAudioSendStream& GetSendStream(uint32_t ssrc) {
    const auto* send_stream = call_.GetAudioSendStream(ssrc);
    EXPECT_TRUE(send_stream);
    return *send_stream;
  }

  const FakeAudioReceiveStream& GetRecvStream(uint32_t ssrc) {
    const auto* recv_stream = call_.GetAudioReceiveStream(ssrc);
    EXPECT_TRUE(recv_stream);
    return *recv_stream;
  }

  const AudioSendStream::Config& GetSendStreamConfig(uint32_t ssrc) {
    return GetSendStream(ssrc).GetConfig();
  }

  const AudioReceiveStreamInterface::Config& GetRecvStreamConfig(
      uint32_t ssrc) {
    return GetRecvStream(ssrc).GetConfig();
  }

  void SetSend(bool enable) {
    ASSERT_TRUE(send_channel_);
    if (enable) {
      EXPECT_CALL(*adm_, RecordingIsInitialized())
          .Times(::testing::AtMost(1))
          .WillOnce(Return(false));
      EXPECT_CALL(*adm_, Recording())
          .Times(::testing::AtMost(1))
          .WillOnce(Return(false));
      EXPECT_CALL(*adm_, InitRecording())
          .Times(::testing::AtMost(1))
          .WillOnce(Return(0));
    }
    send_channel_->SetSend(enable);
  }

  void SetSenderParameters(const AudioSenderParameter& params) {
    ASSERT_TRUE(send_channel_);
    EXPECT_TRUE(send_channel_->SetSenderParameters(params));
    if (receive_channel_) {
      receive_channel_->SetRtcpMode(params.rtcp.reduced_size
                                        ? RtcpMode::kReducedSize
                                        : RtcpMode::kCompound);
      receive_channel_->SetReceiveNackEnabled(
          send_channel_->SenderNackEnabled());
      receive_channel_->SetReceiveNonSenderRttEnabled(
          send_channel_->SenderNonSenderRttEnabled());
    }
  }

  void SetAudioSend(uint32_t ssrc,
                    bool enable,
                    AudioSource* source,
                    const AudioOptions* options = nullptr) {
    ASSERT_TRUE(send_channel_);
    if (!use_null_apm_) {
      EXPECT_CALL(*apm_, set_output_will_be_muted(!enable));
    }
    EXPECT_TRUE(send_channel_->SetAudioSend(ssrc, enable, options, source));
  }

  void TestInsertDtmf(uint32_t ssrc, bool caller, const Codec& codec) {
    EXPECT_TRUE(SetupChannel());
    if (caller) {
      // If this is a caller, local description will be applied and add the
      // send stream.
      EXPECT_TRUE(
          send_channel_->AddSendStream(StreamParams::CreateLegacy(kSsrcX)));
    }

    // Test we can only InsertDtmf when the other side supports telephone-event.
    SetSenderParameters(send_parameters_);
    SetSend(true);
    EXPECT_FALSE(send_channel_->CanInsertDtmf());
    EXPECT_FALSE(send_channel_->InsertDtmf(ssrc, 1, 111));
    send_parameters_.codecs.push_back(codec);
    SetSenderParameters(send_parameters_);
    EXPECT_TRUE(send_channel_->CanInsertDtmf());

    if (!caller) {
      // If this is callee, there's no active send channel yet.
      EXPECT_FALSE(send_channel_->InsertDtmf(ssrc, 2, 123));
      EXPECT_TRUE(
          send_channel_->AddSendStream(StreamParams::CreateLegacy(kSsrcX)));
    }

    // Check we fail if the ssrc is invalid.
    EXPECT_FALSE(send_channel_->InsertDtmf(-1, 1, 111));

    // Test send.
    FakeAudioSendStream::TelephoneEvent telephone_event =
        GetSendStream(kSsrcX).GetLatestTelephoneEvent();
    EXPECT_EQ(-1, telephone_event.payload_type);
    EXPECT_TRUE(send_channel_->InsertDtmf(ssrc, 2, 123));
    telephone_event = GetSendStream(kSsrcX).GetLatestTelephoneEvent();
    EXPECT_EQ(codec.id, telephone_event.payload_type);
    EXPECT_EQ(codec.clockrate, telephone_event.payload_frequency);
    EXPECT_EQ(2, telephone_event.event_code);
    EXPECT_EQ(123, telephone_event.duration_ms);
  }

  void TestExtmapAllowMixedCaller(bool extmap_allow_mixed) {
    // For a caller, the answer will be applied in set remote description
    // where SetSenderParameters() is called.
    EXPECT_TRUE(SetupChannel());
    EXPECT_TRUE(
        send_channel_->AddSendStream(StreamParams::CreateLegacy(kSsrcX)));
    send_parameters_.extmap_allow_mixed = extmap_allow_mixed;
    SetSenderParameters(send_parameters_);
    const AudioSendStream::Config& config = GetSendStreamConfig(kSsrcX);
    EXPECT_EQ(extmap_allow_mixed, config.rtp.extmap_allow_mixed);
  }

  void TestExtmapAllowMixedCallee(bool extmap_allow_mixed) {
    // For a callee, the answer will be applied in set local description
    // where SetExtmapAllowMixed() and AddSendStream() are called.
    EXPECT_TRUE(SetupChannel());
    send_channel_->SetExtmapAllowMixed(extmap_allow_mixed);
    EXPECT_TRUE(
        send_channel_->AddSendStream(StreamParams::CreateLegacy(kSsrcX)));

    const AudioSendStream::Config& config = GetSendStreamConfig(kSsrcX);
    EXPECT_EQ(extmap_allow_mixed, config.rtp.extmap_allow_mixed);
  }

  // Test that send bandwidth is set correctly.
  // `codec` is the codec under test.
  // `max_bitrate` is a parameter to set to SetMaxSendBandwidth().
  // `expected_result` is the expected result from SetMaxSendBandwidth().
  // `expected_bitrate` is the expected audio bitrate afterward.
  void TestMaxSendBandwidth(const Codec& codec,
                            int max_bitrate,
                            bool expected_result,
                            int expected_bitrate) {
    AudioSenderParameter parameters;
    parameters.codecs.push_back(codec);
    parameters.max_bandwidth_bps = max_bitrate;
    if (expected_result) {
      SetSenderParameters(parameters);
    } else {
      EXPECT_FALSE(send_channel_->SetSenderParameters(parameters));
    }
    EXPECT_EQ(expected_bitrate, GetCodecBitrate(kSsrcX));
  }

  // Sets the per-stream maximum bitrate limit for the specified SSRC.
  bool SetMaxBitrateForStream(int32_t ssrc, int bitrate) {
    RtpParameters parameters = send_channel_->GetRtpSendParameters(ssrc);
    EXPECT_EQ(1UL, parameters.encodings.size());

    parameters.encodings[0].max_bitrate_bps = bitrate;
    return send_channel_->SetRtpSendParameters(ssrc, parameters).ok();
  }

  void SetGlobalMaxBitrate(const Codec& codec, int bitrate) {
    AudioSenderParameter send_parameters;
    send_parameters.codecs.push_back(codec);
    send_parameters.max_bandwidth_bps = bitrate;
    SetSenderParameters(send_parameters);
  }

  void CheckSendCodecBitrate(int32_t ssrc,
                             const char expected_name[],
                             int expected_bitrate) {
    const auto& spec = GetSendStreamConfig(ssrc).send_codec_spec;
    EXPECT_EQ(expected_name, spec->format.name);
    EXPECT_EQ(expected_bitrate, spec->target_bitrate_bps);
  }

  std::optional<int> GetCodecBitrate(int32_t ssrc) {
    auto spec = GetSendStreamConfig(ssrc).send_codec_spec;
    if (!spec.has_value()) {
      return std::nullopt;
    }
    return spec->target_bitrate_bps;
  }

  int GetMaxBitrate(int32_t ssrc) {
    return GetSendStreamConfig(ssrc).max_bitrate_bps;
  }

  const std::optional<std::string>& GetAudioNetworkAdaptorConfig(int32_t ssrc) {
    return GetSendStreamConfig(ssrc).audio_network_adaptor_config;
  }

  void SetAndExpectMaxBitrate(const Codec& codec,
                              int global_max,
                              int stream_max,
                              bool expected_result,
                              int expected_codec_bitrate) {
    // Clear the bitrate limit from the previous test case.
    EXPECT_TRUE(SetMaxBitrateForStream(kSsrcX, -1));

    // Attempt to set the requested bitrate limits.
    SetGlobalMaxBitrate(codec, global_max);
    EXPECT_EQ(expected_result, SetMaxBitrateForStream(kSsrcX, stream_max));

    // Verify that reading back the parameters gives results
    // consistent with the Set() result.
    RtpParameters resulting_parameters =
        send_channel_->GetRtpSendParameters(kSsrcX);
    EXPECT_EQ(1UL, resulting_parameters.encodings.size());
    EXPECT_EQ(expected_result ? stream_max : -1,
              resulting_parameters.encodings[0].max_bitrate_bps);

    // Verify that the codec settings have the expected bitrate.
    EXPECT_EQ(expected_codec_bitrate, GetCodecBitrate(kSsrcX));
    EXPECT_EQ(expected_codec_bitrate, GetMaxBitrate(kSsrcX));
  }

  void SetSendCodecsShouldWorkForBitrates(const char* min_bitrate_kbps,
                                          int expected_min_bitrate_bps,
                                          const char* start_bitrate_kbps,
                                          int expected_start_bitrate_bps,
                                          const char* max_bitrate_kbps,
                                          int expected_max_bitrate_bps) {
    EXPECT_TRUE(SetupSendStream());
    auto& codecs = send_parameters_.codecs;
    codecs.clear();
    codecs.push_back(kOpusCodec);
    codecs[0].SetParam(kCodecParamMinBitrate, min_bitrate_kbps);
    codecs[0].SetParam(kCodecParamStartBitrate, start_bitrate_kbps);
    codecs[0].SetParam(kCodecParamMaxBitrate, max_bitrate_kbps);
    EXPECT_CALL(*call_.GetMockTransportControllerSend(),
                SetSdpBitrateParameters(
                    AllOf(Field(&BitrateConstraints::min_bitrate_bps,
                                expected_min_bitrate_bps),
                          Field(&BitrateConstraints::start_bitrate_bps,
                                expected_start_bitrate_bps),
                          Field(&BitrateConstraints::max_bitrate_bps,
                                expected_max_bitrate_bps))));

    SetSenderParameters(send_parameters_);
  }

  void TestSetSendRtpHeaderExtensions(const std::string& ext) {
    EXPECT_TRUE(SetupSendStream());

    // Ensure extensions are off by default.
    EXPECT_EQ(0u, GetSendStreamConfig(kSsrcX).rtp.extensions.size());

    // Ensure unknown extensions won't cause an error.
    send_parameters_.extensions.push_back(RtpExtension(
        "urn:ietf:params:unknownextention", RtpHeaderExtensionId(1)));
    SetSenderParameters(send_parameters_);
    EXPECT_EQ(0u, GetSendStreamConfig(kSsrcX).rtp.extensions.size());

    // Ensure extensions stay off with an empty list of headers.
    send_parameters_.extensions.clear();
    SetSenderParameters(send_parameters_);
    EXPECT_EQ(0u, GetSendStreamConfig(kSsrcX).rtp.extensions.size());

    // Ensure extension is set properly.
    const RtpHeaderExtensionId id(1);
    send_parameters_.extensions.push_back(RtpExtension(ext, id));
    SetSenderParameters(send_parameters_);
    EXPECT_EQ(1u, GetSendStreamConfig(kSsrcX).rtp.extensions.size());
    EXPECT_EQ(ext, GetSendStreamConfig(kSsrcX).rtp.extensions[0].uri);
    EXPECT_EQ(id, GetSendStreamConfig(kSsrcX).rtp.extensions[0].id);

    // Ensure extension is set properly on new stream.
    EXPECT_TRUE(
        send_channel_->AddSendStream(StreamParams::CreateLegacy(kSsrcY)));
    EXPECT_NE(call_.GetAudioSendStream(kSsrcX),
              call_.GetAudioSendStream(kSsrcY));
    EXPECT_EQ(1u, GetSendStreamConfig(kSsrcY).rtp.extensions.size());
    EXPECT_EQ(ext, GetSendStreamConfig(kSsrcY).rtp.extensions[0].uri);
    EXPECT_EQ(id, GetSendStreamConfig(kSsrcY).rtp.extensions[0].id);

    // Ensure all extensions go back off with an empty list.
    send_parameters_.codecs.push_back(kPcmuCodec);
    send_parameters_.extensions.clear();
    SetSenderParameters(send_parameters_);
    EXPECT_EQ(0u, GetSendStreamConfig(kSsrcX).rtp.extensions.size());
    EXPECT_EQ(0u, GetSendStreamConfig(kSsrcY).rtp.extensions.size());
  }

  void TestSetRecvRtpHeaderExtensions(const std::string& ext) {
    EXPECT_TRUE(SetupRecvStream());

    // Ensure extensions are off by default.
    EXPECT_THAT(
        receive_channel_->GetRtpReceiverParameters(kSsrcX).header_extensions,
        IsEmpty());

    // Ensure unknown extensions won't cause an error.
    recv_parameters_.extensions.push_back(RtpExtension(
        "urn:ietf:params:unknownextention", RtpHeaderExtensionId(1)));
    EXPECT_TRUE(receive_channel_->SetReceiverParameters(recv_parameters_));
    EXPECT_THAT(
        receive_channel_->GetRtpReceiverParameters(kSsrcX).header_extensions,
        IsEmpty());

    // Ensure extensions stay off with an empty list of headers.
    recv_parameters_.extensions.clear();
    EXPECT_TRUE(receive_channel_->SetReceiverParameters(recv_parameters_));
    EXPECT_THAT(
        receive_channel_->GetRtpReceiverParameters(kSsrcX).header_extensions,
        IsEmpty());

    // Ensure extension is set properly.
    const RtpHeaderExtensionId id(2);
    recv_parameters_.extensions.push_back(RtpExtension(ext, id));
    EXPECT_TRUE(receive_channel_->SetReceiverParameters(recv_parameters_));
    EXPECT_EQ(
        receive_channel_->GetRtpReceiverParameters(kSsrcX).header_extensions,
        recv_parameters_.extensions);

    // Ensure extension is set properly on new stream.
    EXPECT_TRUE(AddRecvStream(kSsrcY));
    EXPECT_EQ(
        receive_channel_->GetRtpReceiverParameters(kSsrcY).header_extensions,
        recv_parameters_.extensions);

    // Ensure all extensions go back off with an empty list.
    recv_parameters_.extensions.clear();
    EXPECT_TRUE(receive_channel_->SetReceiverParameters(recv_parameters_));
    EXPECT_THAT(
        receive_channel_->GetRtpReceiverParameters(kSsrcX).header_extensions,
        IsEmpty());
    EXPECT_THAT(
        receive_channel_->GetRtpReceiverParameters(kSsrcY).header_extensions,
        IsEmpty());
  }

  AudioSendStream::Stats GetAudioSendStreamStats() const {
    AudioSendStream::Stats stats;
    stats.local_ssrc = 12;
    stats.payload_bytes_sent = 345;
    stats.header_and_padding_bytes_sent = 56;
    stats.packets_sent = 678;
    stats.packets_lost = 9012;
    stats.fraction_lost = 34.56f;
    stats.codec_name = "codec_name_send";
    stats.codec_payload_type = 0;
    stats.jitter_ms = 12;
    stats.rtt_ms = 345;
    stats.audio_level = 678;
    stats.apm_statistics.delay_median_ms = 234;
    stats.apm_statistics.delay_standard_deviation_ms = 567;
    stats.apm_statistics.echo_return_loss = 890;
    stats.apm_statistics.echo_return_loss_enhancement = 1234;
    stats.apm_statistics.residual_echo_likelihood = 0.432f;
    stats.apm_statistics.residual_echo_likelihood_recent_max = 0.6f;
    stats.ana_statistics.bitrate_action_counter = 321;
    stats.ana_statistics.channel_action_counter = 432;
    stats.ana_statistics.dtx_action_counter = 543;
    stats.ana_statistics.fec_action_counter = 654;
    stats.ana_statistics.frame_length_increase_counter = 765;
    stats.ana_statistics.frame_length_decrease_counter = 876;
    stats.ana_statistics.uplink_packet_loss_fraction = 987.0;
    return stats;
  }
  void SetAudioSendStreamStats() {
    for (auto* s : call_.GetAudioSendStreams()) {
      s->SetStats(GetAudioSendStreamStats());
    }
  }
  void VerifyVoiceSenderInfo(const VoiceSenderInfo& info,
                             bool /* is_sending */) {
    const auto stats = GetAudioSendStreamStats();
    EXPECT_EQ(info.ssrc(), stats.local_ssrc);
    EXPECT_EQ(info.payload_bytes_sent, stats.payload_bytes_sent);
    EXPECT_EQ(info.header_and_padding_bytes_sent,
              stats.header_and_padding_bytes_sent);
    EXPECT_EQ(info.packets_sent, stats.packets_sent);
    EXPECT_EQ(info.packets_lost, stats.packets_lost);
    EXPECT_EQ(info.fraction_lost, stats.fraction_lost);
    EXPECT_EQ(info.codec_name, stats.codec_name);
    EXPECT_EQ(info.codec_payload_type, stats.codec_payload_type);
    EXPECT_EQ(info.jitter_ms, stats.jitter_ms);
    EXPECT_EQ(info.rtt_ms, stats.rtt_ms);
    EXPECT_EQ(info.audio_level, stats.audio_level);
    EXPECT_EQ(info.apm_statistics.delay_median_ms,
              stats.apm_statistics.delay_median_ms);
    EXPECT_EQ(info.apm_statistics.delay_standard_deviation_ms,
              stats.apm_statistics.delay_standard_deviation_ms);
    EXPECT_EQ(info.apm_statistics.echo_return_loss,
              stats.apm_statistics.echo_return_loss);
    EXPECT_EQ(info.apm_statistics.echo_return_loss_enhancement,
              stats.apm_statistics.echo_return_loss_enhancement);
    EXPECT_EQ(info.apm_statistics.residual_echo_likelihood,
              stats.apm_statistics.residual_echo_likelihood);
    EXPECT_EQ(info.apm_statistics.residual_echo_likelihood_recent_max,
              stats.apm_statistics.residual_echo_likelihood_recent_max);
    EXPECT_EQ(info.ana_statistics.bitrate_action_counter,
              stats.ana_statistics.bitrate_action_counter);
    EXPECT_EQ(info.ana_statistics.channel_action_counter,
              stats.ana_statistics.channel_action_counter);
    EXPECT_EQ(info.ana_statistics.dtx_action_counter,
              stats.ana_statistics.dtx_action_counter);
    EXPECT_EQ(info.ana_statistics.fec_action_counter,
              stats.ana_statistics.fec_action_counter);
    EXPECT_EQ(info.ana_statistics.frame_length_increase_counter,
              stats.ana_statistics.frame_length_increase_counter);
    EXPECT_EQ(info.ana_statistics.frame_length_decrease_counter,
              stats.ana_statistics.frame_length_decrease_counter);
    EXPECT_EQ(info.ana_statistics.uplink_packet_loss_fraction,
              stats.ana_statistics.uplink_packet_loss_fraction);
  }

  AudioReceiveStreamInterface::Stats GetAudioReceiveStreamStats() const {
    AudioReceiveStreamInterface::Stats stats;
    stats.remote_ssrc = 123;
    stats.payload_bytes_received = 456;
    stats.header_and_padding_bytes_received = 67;
    stats.packets_received = 768;
    stats.packets_lost = 101;
    stats.codec_name = "codec_name_recv";
    stats.codec_payload_type = 0;
    stats.jitter_ms = 901;
    stats.jitter_buffer_ms = 234;
    stats.jitter_buffer_preferred_ms = 567;
    stats.delay_estimate_ms = 890;
    stats.audio_level = 1234;
    stats.total_samples_received = 5678901;
    stats.concealed_samples = 234;
    stats.concealment_events = 12;
    stats.jitter_buffer_delay_seconds = 34;
    stats.jitter_buffer_emitted_count = 77;
    stats.total_processing_delay_seconds = 0.123;
    stats.expand_rate = 5.67f;
    stats.speech_expand_rate = 8.90f;
    stats.secondary_decoded_rate = 1.23f;
    stats.secondary_discarded_rate = 0.12f;
    stats.accelerate_rate = 4.56f;
    stats.preemptive_expand_rate = 7.89f;
    stats.decoding_calls_to_silence_generator = 12;
    stats.decoding_calls_to_neteq = 345;
    stats.decoding_normal = 67890;
    stats.decoding_plc = 1234;
    stats.decoding_codec_plc = 1236;
    stats.decoding_cng = 5678;
    stats.decoding_plc_cng = 9012;
    stats.decoding_muted_output = 3456;
    stats.capture_start_ntp_time_ms = 7890;
    return stats;
  }
  void SetAudioReceiveStreamStats() {
    for (auto* s : call_.GetAudioReceiveStreams()) {
      s->SetStats(GetAudioReceiveStreamStats());
    }
  }
  void VerifyVoiceReceiverInfo(const VoiceReceiverInfo& info) {
    const auto stats = GetAudioReceiveStreamStats();
    EXPECT_EQ(info.ssrc(), stats.remote_ssrc);
    EXPECT_EQ(info.payload_bytes_received, stats.payload_bytes_received);
    EXPECT_EQ(info.header_and_padding_bytes_received,
              stats.header_and_padding_bytes_received);
    EXPECT_EQ(checked_cast<unsigned int>(info.packets_received),
              stats.packets_received);
    EXPECT_EQ(info.packets_lost, stats.packets_lost);
    EXPECT_EQ(info.codec_name, stats.codec_name);
    EXPECT_EQ(info.codec_payload_type, stats.codec_payload_type);
    EXPECT_EQ(checked_cast<unsigned int>(info.jitter_ms), stats.jitter_ms);
    EXPECT_EQ(checked_cast<unsigned int>(info.jitter_buffer_ms),
              stats.jitter_buffer_ms);
    EXPECT_EQ(checked_cast<unsigned int>(info.jitter_buffer_preferred_ms),
              stats.jitter_buffer_preferred_ms);
    EXPECT_EQ(checked_cast<unsigned int>(info.delay_estimate_ms),
              stats.delay_estimate_ms);
    EXPECT_EQ(info.audio_level, stats.audio_level);
    EXPECT_EQ(info.total_samples_received, stats.total_samples_received);
    EXPECT_EQ(info.concealed_samples, stats.concealed_samples);
    EXPECT_EQ(info.concealment_events, stats.concealment_events);
    EXPECT_EQ(info.jitter_buffer_delay_seconds,
              stats.jitter_buffer_delay_seconds);
    EXPECT_EQ(info.jitter_buffer_emitted_count,
              stats.jitter_buffer_emitted_count);
    EXPECT_EQ(info.total_processing_delay_seconds,
              stats.total_processing_delay_seconds);
    EXPECT_EQ(info.expand_rate, stats.expand_rate);
    EXPECT_EQ(info.speech_expand_rate, stats.speech_expand_rate);
    EXPECT_EQ(info.secondary_decoded_rate, stats.secondary_decoded_rate);
    EXPECT_EQ(info.secondary_discarded_rate, stats.secondary_discarded_rate);
    EXPECT_EQ(info.accelerate_rate, stats.accelerate_rate);
    EXPECT_EQ(info.preemptive_expand_rate, stats.preemptive_expand_rate);
    EXPECT_EQ(info.decoding_calls_to_silence_generator,
              stats.decoding_calls_to_silence_generator);
    EXPECT_EQ(info.decoding_calls_to_neteq, stats.decoding_calls_to_neteq);
    EXPECT_EQ(info.decoding_normal, stats.decoding_normal);
    EXPECT_EQ(info.decoding_plc, stats.decoding_plc);
    EXPECT_EQ(info.decoding_codec_plc, stats.decoding_codec_plc);
    EXPECT_EQ(info.decoding_cng, stats.decoding_cng);
    EXPECT_EQ(info.decoding_plc_cng, stats.decoding_plc_cng);
    EXPECT_EQ(info.decoding_muted_output, stats.decoding_muted_output);
    EXPECT_EQ(info.capture_start_ntp_time_ms, stats.capture_start_ntp_time_ms);
  }
  void VerifyVoiceSendRecvCodecs(
      const VoiceMediaSendInfo& send_info,
      const VoiceMediaReceiveInfo& receive_info) const {
    EXPECT_EQ(send_parameters_.codecs.size(), send_info.send_codecs.size());
    for (const Codec& codec : send_parameters_.codecs) {
      ASSERT_EQ(send_info.send_codecs.count(codec.id), 1U);
      EXPECT_EQ(send_info.send_codecs.find(codec.id)->second,
                codec.ToCodecParameters());
    }
    EXPECT_EQ(recv_parameters_.codecs.size(),
              receive_info.receive_codecs.size());
    for (const Codec& codec : recv_parameters_.codecs) {
      ASSERT_EQ(receive_info.receive_codecs.count(codec.id), 1U);
      EXPECT_EQ(receive_info.receive_codecs.find(codec.id)->second,
                codec.ToCodecParameters());
    }
  }

  void VerifyGainControlEnabledCorrectly() {
    EXPECT_TRUE(apm_config_.gain_controller1.enabled);
    EXPECT_EQ(kDefaultAgcMode, apm_config_.gain_controller1.mode);
  }

  void VerifyGainControlDefaultSettings() {
    EXPECT_EQ(3, apm_config_.gain_controller1.target_level_dbfs);
    EXPECT_EQ(9, apm_config_.gain_controller1.compression_gain_db);
    EXPECT_TRUE(apm_config_.gain_controller1.enable_limiter);
  }

  void VerifyEchoCancellationSettings(bool enabled) {
    EXPECT_EQ(apm_config_.echo_canceller.enabled, enabled);
  }

  bool IsHighPassFilterEnabled() {
    return apm_config_.high_pass_filter.enabled;
  }

  WebRtcVoiceSendChannel* SendImplFromPointer(
      VoiceMediaSendChannelInterface* channel) {
    return static_cast<WebRtcVoiceSendChannel*>(channel);
  }

  WebRtcVoiceSendChannel* SendImpl() {
    return SendImplFromPointer(send_channel_.get());
  }
  WebRtcVoiceReceiveChannel* ReceiveImpl() {
    return static_cast<WebRtcVoiceReceiveChannel*>(receive_channel_.get());
  }
  std::vector<Codec> SendCodecsWithId() {
    std::vector<Codec> codecs = engine_->LegacySendCodecs();
    return AddIdToCodecs(pt_mapper_, std::move(codecs));
  }

 protected:
  test::RunLoop run_loop_;
  const bool use_null_apm_;
  const Environment env_;
  scoped_refptr<test::MockAudioDeviceModule> adm_;
  scoped_refptr<StrictMock<test::MockAudioProcessing>> apm_;
  FakeCall call_;
  FakeAudioSource fake_source_;
  std::unique_ptr<WebRtcVoiceEngine> engine_;
  std::unique_ptr<VoiceMediaSendChannelInterface> send_channel_;
  std::unique_ptr<VoiceMediaReceiveChannelInterface> receive_channel_;
  AudioSenderParameter send_parameters_;
  AudioReceiverParameters recv_parameters_;
  AudioProcessing::Config apm_config_;
  PayloadTypePicker pt_mapper_;
};

INSTANTIATE_TEST_SUITE_P(TestBothWithAndWithoutNullApm,
                         WebRtcVoiceEngineTestFake,
                         ::testing::Values(false, true));

// Tests that we can create and destroy a channel.
TEST_P(WebRtcVoiceEngineTestFake, CreateMediaChannel) {
  EXPECT_TRUE(SetupChannel());
}

TEST_P(WebRtcVoiceEngineTestFake, MultipleStartStop) {
  // Call Start/Stop a few times in a loop. The `engine_` will have already been
  // started so we'll start by stopping and re-starting.
  for (int i = 0; i < 10; ++i) {
    engine_->Terminate();
    Mock::VerifyAndClearExpectations(adm_.get());
    AdmSetupExpectations(adm_.get());
    if (!use_null_apm_) {
      // AudioProcessing.
      EXPECT_CALL(*apm_, GetConfig())
          .WillRepeatedly(ReturnPointee(&apm_config_));
      EXPECT_CALL(*apm_, ApplyConfig(_))
          .WillRepeatedly(SaveArg<0>(&apm_config_));
      EXPECT_CALL(*apm_, DetachAecDump());
    }
    engine_->Init();
  }
  // SetupChannel should succeed as before.
  EXPECT_TRUE(SetupChannel());
}

// Test that we can add a send stream and that it has the correct defaults.
TEST_P(WebRtcVoiceEngineTestFake, CreateSendStream) {
  EXPECT_TRUE(SetupChannel());
  EXPECT_TRUE(send_channel_->AddSendStream(StreamParams::CreateLegacy(kSsrcX)));
  const AudioSendStream::Config& config = GetSendStreamConfig(kSsrcX);
  EXPECT_EQ(kSsrcX, config.rtp.ssrc);
  EXPECT_EQ("", config.rtp.c_name);
  EXPECT_EQ(0u, config.rtp.extensions.size());
  EXPECT_EQ(SendImpl()->transport(), config.send_transport);
}

// Test that a send stream that is added after the MID has been set is created
// with the MID.
TEST_P(WebRtcVoiceEngineTestFake, CreateSendStreamWithMid) {
  EXPECT_TRUE(SetupChannel());
  send_channel_->SetMid("audio");
  EXPECT_TRUE(send_channel_->AddSendStream(StreamParams::CreateLegacy(kSsrcX)));
  EXPECT_EQ(GetSendStreamConfig(kSsrcX).rtp.mid, "audio");
}

// Test that we can add a receive stream and that it has the correct defaults.
TEST_P(WebRtcVoiceEngineTestFake, CreateRecvStream) {
  EXPECT_TRUE(SetupChannel());
  EXPECT_TRUE(AddRecvStream(kSsrcX));
  const AudioReceiveStreamInterface::Config& config =
      GetRecvStreamConfig(kSsrcX);
  EXPECT_EQ(kSsrcX, config.rtp.remote_ssrc);
  EXPECT_EQ(ReceiveImpl()->transport(), config.rtcp_send_transport);
  EXPECT_EQ("", config.sync_group);
}

// Test that we set our inbound codecs properly, including changing PT.
TEST_P(WebRtcVoiceEngineTestFake, SetRecvCodecs) {
  EXPECT_TRUE(SetupChannel());
  AudioReceiverParameters parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs.push_back(kPcmuCodec);
  parameters.codecs.push_back(kTelephoneEventCodec1);
  parameters.codecs.push_back(kTelephoneEventCodec2);
  parameters.codecs[0].id = 106;  // collide with existing CN 32k
  parameters.codecs[2].id = 126;
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));
  EXPECT_TRUE(AddRecvStream(kSsrcX));
  EXPECT_THAT(GetRecvStreamConfig(kSsrcX).decoder_map,
              (ContainerEq<std::map<int, SdpAudioFormat>>(
                  {{0, {"PCMU", 8000, 1}},
                   {106, {"OPUS", 48000, 2}},
                   {126, {"telephone-event", 8000, 1}},
                   {107, {"telephone-event", 32000, 1}}})));
}

// Test that we fail to set an unknown inbound codec.
TEST_P(WebRtcVoiceEngineTestFake, SetRecvCodecsUnsupportedCodec) {
  EXPECT_TRUE(SetupChannel());
  AudioReceiverParameters parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs.push_back(kUnknownCodec);
  EXPECT_FALSE(receive_channel_->SetReceiverParameters(parameters));
}

// Test that we fail if we have duplicate types in the inbound list.
TEST_P(WebRtcVoiceEngineTestFake, SetRecvCodecsDuplicatePayloadType) {
  EXPECT_TRUE(SetupChannel());
  AudioReceiverParameters parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs.push_back(kCn16000Codec);
  parameters.codecs[1].id = kOpusCodec.id;
  EXPECT_FALSE(receive_channel_->SetReceiverParameters(parameters));
}

// Test that we can decode OPUS without stereo parameters.
TEST_P(WebRtcVoiceEngineTestFake, SetRecvCodecsWithOpusNoStereo) {
  EXPECT_TRUE(SetupChannel());
  AudioReceiverParameters parameters;
  parameters.codecs.push_back(kPcmuCodec);
  parameters.codecs.push_back(kOpusCodec);
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));
  EXPECT_TRUE(AddRecvStream(kSsrcX));
  EXPECT_THAT(GetRecvStreamConfig(kSsrcX).decoder_map,
              (ContainerEq<std::map<int, SdpAudioFormat>>(
                  {{0, {"PCMU", 8000, 1}}, {111, {"opus", 48000, 2}}})));
}

// Test that we can decode OPUS with stereo = 0.
TEST_P(WebRtcVoiceEngineTestFake, SetRecvCodecsWithOpus0Stereo) {
  EXPECT_TRUE(SetupChannel());
  AudioReceiverParameters parameters;
  parameters.codecs.push_back(kPcmuCodec);
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs[1].params["stereo"] = "0";
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));
  EXPECT_TRUE(AddRecvStream(kSsrcX));
  EXPECT_THAT(GetRecvStreamConfig(kSsrcX).decoder_map,
              (ContainerEq<std::map<int, SdpAudioFormat>>(
                  {{0, {"PCMU", 8000, 1}},
                   {111, {"opus", 48000, 2, {{"stereo", "0"}}}}})));
}

// Test that we can decode OPUS with stereo = 1.
TEST_P(WebRtcVoiceEngineTestFake, SetRecvCodecsWithOpus1Stereo) {
  EXPECT_TRUE(SetupChannel());
  AudioReceiverParameters parameters;
  parameters.codecs.push_back(kPcmuCodec);
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs[1].params["stereo"] = "1";
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));
  EXPECT_TRUE(AddRecvStream(kSsrcX));
  EXPECT_THAT(GetRecvStreamConfig(kSsrcX).decoder_map,
              (ContainerEq<std::map<int, SdpAudioFormat>>(
                  {{0, {"PCMU", 8000, 1}},
                   {111, {"opus", 48000, 2, {{"stereo", "1"}}}}})));
}

// Test that changes to recv codecs are applied to all streams.
TEST_P(WebRtcVoiceEngineTestFake, SetRecvCodecsWithMultipleStreams) {
  EXPECT_TRUE(SetupChannel());
  AudioReceiverParameters parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs.push_back(kPcmuCodec);
  parameters.codecs.push_back(kTelephoneEventCodec1);
  parameters.codecs.push_back(kTelephoneEventCodec2);
  parameters.codecs[0].id = 106;  // collide with existing CN 32k
  parameters.codecs[2].id = 126;
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));
  for (const auto& ssrc : {kSsrcX, kSsrcY}) {
    EXPECT_TRUE(AddRecvStream(ssrc));
    EXPECT_THAT(GetRecvStreamConfig(ssrc).decoder_map,
                (ContainerEq<std::map<int, SdpAudioFormat>>(
                    {{0, {"PCMU", 8000, 1}},
                     {106, {"OPUS", 48000, 2}},
                     {126, {"telephone-event", 8000, 1}},
                     {107, {"telephone-event", 32000, 1}}})));
  }
}

TEST_P(WebRtcVoiceEngineTestFake, SetRecvCodecsAfterAddingStreams) {
  EXPECT_TRUE(SetupRecvStream());
  AudioReceiverParameters parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs[0].id = 106;  // collide with existing CN 32k
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));

  const auto& dm = GetRecvStreamConfig(kSsrcX).decoder_map;
  ASSERT_EQ(1u, dm.count(106));
  EXPECT_EQ(SdpAudioFormat("opus", 48000, 2), dm.at(106));
}

// Test that we can apply the same set of codecs again while playing.
TEST_P(WebRtcVoiceEngineTestFake, SetRecvCodecsWhilePlaying) {
  EXPECT_TRUE(SetupRecvStream());
  AudioReceiverParameters parameters;
  parameters.codecs.push_back(kPcmuCodec);
  parameters.codecs.push_back(kCn16000Codec);
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));
  receive_channel_->SetReceive(true);
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));

  // Remapping a payload type to a different codec should fail.
  parameters.codecs[0] = kOpusCodec;
  parameters.codecs[0].id = kPcmuCodec.id;
  EXPECT_FALSE(receive_channel_->SetReceiverParameters(parameters));
  EXPECT_TRUE(GetRecvStream(kSsrcX).started());
}

// Test that we can add a codec while playing.
TEST_P(WebRtcVoiceEngineTestFake, AddRecvCodecsWhilePlaying) {
  EXPECT_TRUE(SetupRecvStream());
  AudioReceiverParameters parameters;
  parameters.codecs.push_back(kPcmuCodec);
  parameters.codecs.push_back(kCn16000Codec);
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));
  receive_channel_->SetReceive(true);

  parameters.codecs.push_back(kOpusCodec);
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));
  EXPECT_TRUE(GetRecvStream(kSsrcX).started());
}

// Test that we accept adding the same codec with a different payload type.
// See: https://bugs.chromium.org/p/webrtc/issues/detail?id=5847
TEST_P(WebRtcVoiceEngineTestFake, ChangeRecvCodecPayloadType) {
  EXPECT_TRUE(SetupRecvStream());
  AudioReceiverParameters parameters;
  parameters.codecs.push_back(kOpusCodec);
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));

  parameters.codecs[0].id = PayloadType(parameters.codecs[0].id + 1);
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));
}

// Test that we do allow setting Opus/Red by default.
TEST_P(WebRtcVoiceEngineTestFake, RecvRedDefault) {
  EXPECT_TRUE(SetupRecvStream());
  AudioReceiverParameters parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs.push_back(kRed48000Codec);
  parameters.codecs[1].params[""] = "111/111";
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));
  EXPECT_THAT(GetRecvStreamConfig(kSsrcX).decoder_map,
              (ContainerEq<std::map<int, SdpAudioFormat>>(
                  {{111, {"opus", 48000, 2}},
                   {112, {"red", 48000, 2, {{"", "111/111"}}}}})));
}

TEST_P(WebRtcVoiceEngineTestFake, SetSendBandwidthAuto) {
  EXPECT_TRUE(SetupSendStream());

  // Test that when autobw is enabled, bitrate is kept as the default
  // value. autobw is enabled for the following tests because the target
  // bitrate is <= 0.

  // PCMU, default bitrate == 64000.
  TestMaxSendBandwidth(kPcmuCodec, -1, true, 64000);

  // opus, default bitrate == 32000 in mono.
  TestMaxSendBandwidth(kOpusCodec, -1, true, 32000);
}

TEST_P(WebRtcVoiceEngineTestFake, SetMaxSendBandwidthMultiRateAsCaller) {
  EXPECT_TRUE(SetupSendStream());

  // opus, default bitrate == 64000.
  TestMaxSendBandwidth(kOpusCodec, 96000, true, 96000);
  TestMaxSendBandwidth(kOpusCodec, 48000, true, 48000);
  // Rates above the max (510000) should be capped.
  TestMaxSendBandwidth(kOpusCodec, 600000, true, 510000);
}

TEST_P(WebRtcVoiceEngineTestFake, SetMaxSendBandwidthFixedRateAsCaller) {
  EXPECT_TRUE(SetupSendStream());

  // Test that we can only set a maximum bitrate for a fixed-rate codec
  // if it's bigger than the fixed rate.

  // PCMU, fixed bitrate == 64000.
  TestMaxSendBandwidth(kPcmuCodec, 0, true, 64000);
  TestMaxSendBandwidth(kPcmuCodec, 1, false, 64000);
  TestMaxSendBandwidth(kPcmuCodec, 128000, true, 64000);
  TestMaxSendBandwidth(kPcmuCodec, 32000, false, 64000);
  TestMaxSendBandwidth(kPcmuCodec, 64000, true, 64000);
  TestMaxSendBandwidth(kPcmuCodec, 63999, false, 64000);
  TestMaxSendBandwidth(kPcmuCodec, 64001, true, 64000);
}

TEST_P(WebRtcVoiceEngineTestFake, SetMaxSendBandwidthMultiRateAsCallee) {
  EXPECT_TRUE(SetupChannel());
  const int kDesiredBitrate = 128000;
  AudioSenderParameter parameters;
  parameters.codecs = SendCodecsWithId();
  parameters.max_bandwidth_bps = kDesiredBitrate;
  SetSenderParameters(parameters);

  EXPECT_TRUE(send_channel_->AddSendStream(StreamParams::CreateLegacy(kSsrcX)));

  EXPECT_EQ(kDesiredBitrate, GetCodecBitrate(kSsrcX));
}

// Test that bitrate cannot be set for CBR codecs.
// Bitrate is ignored if it is higher than the fixed bitrate.
// Bitrate less then the fixed bitrate is an error.
TEST_P(WebRtcVoiceEngineTestFake, SetMaxSendBandwidthCbr) {
  EXPECT_TRUE(SetupSendStream());

  // PCMU, default bitrate == 64000.
  SetSenderParameters(send_parameters_);
  EXPECT_EQ(64000, GetCodecBitrate(kSsrcX));

  send_parameters_.max_bandwidth_bps = 128000;
  SetSenderParameters(send_parameters_);
  EXPECT_EQ(64000, GetCodecBitrate(kSsrcX));

  send_parameters_.max_bandwidth_bps = 128;
  EXPECT_FALSE(send_channel_->SetSenderParameters(send_parameters_));
  EXPECT_EQ(64000, GetCodecBitrate(kSsrcX));
}

// Test that the per-stream bitrate limit and the global
// bitrate limit both apply.
TEST_P(WebRtcVoiceEngineTestFake, SetMaxBitratePerStream) {
  EXPECT_TRUE(SetupSendStream());

  // opus, default bitrate == 32000.
  SetAndExpectMaxBitrate(kOpusCodec, 0, 0, true, 32000);
  SetAndExpectMaxBitrate(kOpusCodec, 48000, 0, true, 48000);
  SetAndExpectMaxBitrate(kOpusCodec, 48000, 64000, true, 48000);
  SetAndExpectMaxBitrate(kOpusCodec, 64000, 48000, true, 48000);

  // CBR codecs allow both maximums to exceed the bitrate.
  SetAndExpectMaxBitrate(kPcmuCodec, 0, 0, true, 64000);
  SetAndExpectMaxBitrate(kPcmuCodec, 64001, 0, true, 64000);
  SetAndExpectMaxBitrate(kPcmuCodec, 0, 64001, true, 64000);
  SetAndExpectMaxBitrate(kPcmuCodec, 64001, 64001, true, 64000);

  // CBR codecs don't allow per stream maximums to be too low.
  SetAndExpectMaxBitrate(kPcmuCodec, 0, 63999, false, 64000);
  SetAndExpectMaxBitrate(kPcmuCodec, 64001, 63999, false, 64000);
}

// Test that an attempt to set RtpParameters for a stream that does not exist
// fails.
TEST_P(WebRtcVoiceEngineTestFake, CannotSetMaxBitrateForNonexistentStream) {
  EXPECT_TRUE(SetupChannel());
  RtpParameters nonexistent_parameters =
      send_channel_->GetRtpSendParameters(kSsrcX);
  EXPECT_EQ(0u, nonexistent_parameters.encodings.size());

  nonexistent_parameters.encodings.push_back(RtpEncodingParameters());
  EXPECT_FALSE(
      send_channel_->SetRtpSendParameters(kSsrcX, nonexistent_parameters).ok());
}

TEST_P(WebRtcVoiceEngineTestFake,
       CannotSetRtpSendParametersWithIncorrectNumberOfEncodings) {
  // This test verifies that setting RtpParameters succeeds only if
  // the structure contains exactly one encoding.
  // TODO(skvlad): Update this test when we start supporting setting parameters
  // for each encoding individually.

  EXPECT_TRUE(SetupSendStream());
  RtpParameters parameters = send_channel_->GetRtpSendParameters(kSsrcX);
  // Two or more encodings should result in failure.
  parameters.encodings.push_back(RtpEncodingParameters());
  EXPECT_FALSE(send_channel_->SetRtpSendParameters(kSsrcX, parameters).ok());
  // Zero encodings should also fail.
  parameters.encodings.clear();
  EXPECT_FALSE(send_channel_->SetRtpSendParameters(kSsrcX, parameters).ok());
}

// Changing the SSRC through RtpParameters is not allowed.
TEST_P(WebRtcVoiceEngineTestFake, CannotSetSsrcInRtpSendParameters) {
  EXPECT_TRUE(SetupSendStream());
  RtpParameters parameters = send_channel_->GetRtpSendParameters(kSsrcX);
  parameters.encodings[0].ssrc = 0xdeadbeef;
  EXPECT_FALSE(send_channel_->SetRtpSendParameters(kSsrcX, parameters).ok());
}

// Test that a stream will not be sending if its encoding is made
// inactive through SetRtpSendParameters.
TEST_P(WebRtcVoiceEngineTestFake, SetRtpParametersEncodingsActive) {
  EXPECT_TRUE(SetupSendStream());
  SetSend(true);
  EXPECT_TRUE(GetSendStream(kSsrcX).IsSending());
  // Get current parameters and change "active" to false.
  RtpParameters parameters = send_channel_->GetRtpSendParameters(kSsrcX);
  ASSERT_EQ(1u, parameters.encodings.size());
  ASSERT_TRUE(parameters.encodings[0].active);
  parameters.encodings[0].active = false;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(kSsrcX, parameters).ok());
  EXPECT_FALSE(GetSendStream(kSsrcX).IsSending());

  // Now change it back to active and verify we resume sending.
  // This should occur even when other parameters are updated.
  parameters.encodings[0].active = true;
  parameters.encodings[0].max_bitrate_bps = std::optional<int>(6000);
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(kSsrcX, parameters).ok());
  EXPECT_TRUE(GetSendStream(kSsrcX).IsSending());
}

TEST_P(WebRtcVoiceEngineTestFake, SetRtpParametersAdaptivePtime) {
  EXPECT_TRUE(SetupSendStream());
  // Get current parameters and change "adaptive_ptime" to true.
  RtpParameters parameters = send_channel_->GetRtpSendParameters(kSsrcX);
  ASSERT_EQ(1u, parameters.encodings.size());
  ASSERT_FALSE(parameters.encodings[0].adaptive_ptime);
  parameters.encodings[0].adaptive_ptime = true;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(kSsrcX, parameters).ok());
  EXPECT_TRUE(GetAudioNetworkAdaptorConfig(kSsrcX));
  EXPECT_EQ(16000, GetSendStreamConfig(kSsrcX).min_bitrate_bps);

  parameters.encodings[0].adaptive_ptime = false;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(kSsrcX, parameters).ok());
  EXPECT_FALSE(GetAudioNetworkAdaptorConfig(kSsrcX));
  EXPECT_EQ(32000, GetSendStreamConfig(kSsrcX).min_bitrate_bps);
}

TEST_P(WebRtcVoiceEngineTestFake,
       DisablingAdaptivePtimeDoesNotRemoveAudioNetworkAdaptorFromOptions) {
  EXPECT_TRUE(SetupSendStream());
  send_parameters_.options.audio_network_adaptor = true;
  send_parameters_.options.audio_network_adaptor_config = {"1234"};
  SetSenderParameters(send_parameters_);
  EXPECT_EQ(send_parameters_.options.audio_network_adaptor_config,
            GetAudioNetworkAdaptorConfig(kSsrcX));

  RtpParameters parameters = send_channel_->GetRtpSendParameters(kSsrcX);
  parameters.encodings[0].adaptive_ptime = false;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(kSsrcX, parameters).ok());
  EXPECT_EQ(send_parameters_.options.audio_network_adaptor_config,
            GetAudioNetworkAdaptorConfig(kSsrcX));
}

class WebRtcVoiceEngineTestWithAdaptivePtime
    : public WebRtcVoiceEngineTestFake {
 public:
  WebRtcVoiceEngineTestWithAdaptivePtime()
      : WebRtcVoiceEngineTestFake("WebRTC-Audio-AdaptivePtime/enabled:true/") {}
};

TEST_P(WebRtcVoiceEngineTestWithAdaptivePtime, AdaptivePtimeFieldTrial) {
  EXPECT_TRUE(SetupSendStream());
  EXPECT_TRUE(GetAudioNetworkAdaptorConfig(kSsrcX));
}

INSTANTIATE_TEST_SUITE_P(TestBothWithAndWithoutNullApm,
                         WebRtcVoiceEngineTestWithAdaptivePtime,
                         ::testing::Values(false, true));

// Test that SetRtpSendParameters configures the correct encoding channel for
// each SSRC.
TEST_P(WebRtcVoiceEngineTestFake, RtpParametersArePerStream) {
  SetupForMultiSendStream();
  // Create send streams.
  for (uint32_t ssrc : kSsrcs4) {
    EXPECT_TRUE(send_channel_->AddSendStream(StreamParams::CreateLegacy(ssrc)));
  }
  // Configure one stream to be limited by the stream config, another to be
  // limited by the global max, and the third one with no per-stream limit
  // (still subject to the global limit).
  SetGlobalMaxBitrate(kOpusCodec, 32000);
  EXPECT_TRUE(SetMaxBitrateForStream(kSsrcs4[0], 24000));
  EXPECT_TRUE(SetMaxBitrateForStream(kSsrcs4[1], 48000));
  EXPECT_TRUE(SetMaxBitrateForStream(kSsrcs4[2], -1));

  EXPECT_EQ(24000, GetCodecBitrate(kSsrcs4[0]));
  EXPECT_EQ(32000, GetCodecBitrate(kSsrcs4[1]));
  EXPECT_EQ(32000, GetCodecBitrate(kSsrcs4[2]));

  // Remove the global cap; the streams should switch to their respective
  // maximums (or remain unchanged if there was no other limit on them.)
  SetGlobalMaxBitrate(kOpusCodec, -1);
  EXPECT_EQ(24000, GetCodecBitrate(kSsrcs4[0]));
  EXPECT_EQ(48000, GetCodecBitrate(kSsrcs4[1]));
  EXPECT_EQ(32000, GetCodecBitrate(kSsrcs4[2]));
}

// Test that GetRtpSendParameters returns the currently configured codecs.
TEST_P(WebRtcVoiceEngineTestFake, GetRtpSendParametersCodecs) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs.push_back(kPcmuCodec);
  SetSenderParameters(parameters);

  RtpParameters rtp_parameters = send_channel_->GetRtpSendParameters(kSsrcX);
  ASSERT_EQ(2u, rtp_parameters.codecs.size());
  EXPECT_EQ(kOpusCodec.ToCodecParameters(), rtp_parameters.codecs[0]);
  EXPECT_EQ(kPcmuCodec.ToCodecParameters(), rtp_parameters.codecs[1]);
}

// Test that GetRtpSendParameters returns the currently configured RTCP CNAME.
TEST_P(WebRtcVoiceEngineTestFake, GetRtpSendParametersRtcpCname) {
  StreamParams params = StreamParams::CreateLegacy(kSsrcX);
  params.cname = "rtcpcname";
  EXPECT_TRUE(SetupSendStream(params));

  RtpParameters rtp_parameters = send_channel_->GetRtpSendParameters(kSsrcX);
  EXPECT_STREQ("rtcpcname", rtp_parameters.rtcp.cname.c_str());
}

TEST_P(WebRtcVoiceEngineTestFake,
       DetectRtpSendParameterHeaderExtensionsChange) {
  EXPECT_TRUE(SetupSendStream());

  RtpParameters rtp_parameters = send_channel_->GetRtpSendParameters(kSsrcX);
  rtp_parameters.header_extensions.emplace_back();

  EXPECT_NE(0u, rtp_parameters.header_extensions.size());

  RTCError result = send_channel_->SetRtpSendParameters(kSsrcX, rtp_parameters);
  EXPECT_EQ(RTCErrorType::INVALID_MODIFICATION, result.type());
}

// Test that GetRtpSendParameters returns an SSRC.
TEST_P(WebRtcVoiceEngineTestFake, GetRtpSendParametersSsrc) {
  EXPECT_TRUE(SetupSendStream());
  RtpParameters rtp_parameters = send_channel_->GetRtpSendParameters(kSsrcX);
  ASSERT_EQ(1u, rtp_parameters.encodings.size());
  EXPECT_EQ(kSsrcX, rtp_parameters.encodings[0].ssrc);
}

// Test that if we set/get parameters multiple times, we get the same results.
TEST_P(WebRtcVoiceEngineTestFake, SetAndGetRtpSendParameters) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs.push_back(kPcmuCodec);
  SetSenderParameters(parameters);

  RtpParameters initial_params = send_channel_->GetRtpSendParameters(kSsrcX);

  // We should be able to set the params we just got.
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(kSsrcX, initial_params).ok());

  // ... And this shouldn't change the params returned by GetRtpSendParameters.
  RtpParameters new_params = send_channel_->GetRtpSendParameters(kSsrcX);
  EXPECT_EQ(initial_params, send_channel_->GetRtpSendParameters(kSsrcX));
}

// Test that we remove the codec from RTP parameters if it's not negotiated
// anymore.
TEST_P(WebRtcVoiceEngineTestFake,
       SetSendParametersRemovesSelectedCodecFromRtpParameters) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs.push_back(kPcmuCodec);
  SetSenderParameters(parameters);

  RtpParameters initial_params = send_channel_->GetRtpSendParameters(kSsrcX);

  RtpCodec opus_rtp_codec;
  opus_rtp_codec.name = "opus";
  opus_rtp_codec.kind = MediaType::AUDIO;
  opus_rtp_codec.num_channels = 2;
  opus_rtp_codec.clock_rate = 48000;
  initial_params.encodings[0].codec = opus_rtp_codec;

  // We should be able to set the params with the opus codec that has been
  // negotiated.
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(kSsrcX, initial_params).ok());

  parameters.codecs.clear();
  parameters.codecs.push_back(kPcmuCodec);
  SetSenderParameters(parameters);

  // Since Opus is no longer negotiated, the RTP parameters should not have a
  // forced codec anymore.
  RtpParameters new_params = send_channel_->GetRtpSendParameters(kSsrcX);
  EXPECT_EQ(new_params.encodings[0].codec, std::nullopt);
}

// Test that max_bitrate_bps in send stream config gets updated correctly when
// SetRtpSendParameters is called.
TEST_P(WebRtcVoiceEngineTestFake, SetRtpSendParameterUpdatesMaxBitrate) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter send_parameters;
  send_parameters.codecs.push_back(kOpusCodec);
  SetSenderParameters(send_parameters);

  RtpParameters rtp_parameters = send_channel_->GetRtpSendParameters(kSsrcX);
  // Expect empty on parameters.encodings[0].max_bitrate_bps;
  EXPECT_FALSE(rtp_parameters.encodings[0].max_bitrate_bps);

  constexpr int kMaxBitrateBps = 6000;
  rtp_parameters.encodings[0].max_bitrate_bps = kMaxBitrateBps;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(kSsrcX, rtp_parameters).ok());

  const int max_bitrate = GetSendStreamConfig(kSsrcX).max_bitrate_bps;
  EXPECT_EQ(max_bitrate, kMaxBitrateBps);
}

// Tests that when RTCRtpEncodingParameters.bitrate_priority gets set to
// a value <= 0, setting the parameters returns false.
TEST_P(WebRtcVoiceEngineTestFake, SetRtpSendParameterInvalidBitratePriority) {
  EXPECT_TRUE(SetupSendStream());
  RtpParameters rtp_parameters = send_channel_->GetRtpSendParameters(kSsrcX);
  EXPECT_EQ(1UL, rtp_parameters.encodings.size());
  EXPECT_EQ(kDefaultBitratePriority,
            rtp_parameters.encodings[0].bitrate_priority);

  rtp_parameters.encodings[0].bitrate_priority = 0;
  EXPECT_FALSE(
      send_channel_->SetRtpSendParameters(kSsrcX, rtp_parameters).ok());
  rtp_parameters.encodings[0].bitrate_priority = -1.0;
  EXPECT_FALSE(
      send_channel_->SetRtpSendParameters(kSsrcX, rtp_parameters).ok());
}

// Test that the bitrate_priority in the send stream config gets updated when
// SetRtpSendParameters is set for the VoiceMediaChannel.
TEST_P(WebRtcVoiceEngineTestFake, SetRtpSendParameterUpdatesBitratePriority) {
  EXPECT_TRUE(SetupSendStream());
  RtpParameters rtp_parameters = send_channel_->GetRtpSendParameters(kSsrcX);

  EXPECT_EQ(1UL, rtp_parameters.encodings.size());
  EXPECT_EQ(kDefaultBitratePriority,
            rtp_parameters.encodings[0].bitrate_priority);
  double new_bitrate_priority = 2.0;
  rtp_parameters.encodings[0].bitrate_priority = new_bitrate_priority;
  EXPECT_TRUE(send_channel_->SetRtpSendParameters(kSsrcX, rtp_parameters).ok());

  // The priority should get set for both the audio channel's rtp parameters
  // and the audio send stream's audio config.
  EXPECT_EQ(new_bitrate_priority, send_channel_->GetRtpSendParameters(kSsrcX)
                                      .encodings[0]
                                      .bitrate_priority);
  EXPECT_EQ(new_bitrate_priority, GetSendStreamConfig(kSsrcX).bitrate_priority);
}

// Test that GetRtpReceiverParameters returns the currently configured codecs.
TEST_P(WebRtcVoiceEngineTestFake, GetRtpReceiveParametersCodecs) {
  EXPECT_TRUE(SetupRecvStream());
  AudioReceiverParameters parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs.push_back(kPcmuCodec);
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));

  RtpParameters rtp_parameters =
      receive_channel_->GetRtpReceiverParameters(kSsrcX);
  ASSERT_EQ(2u, rtp_parameters.codecs.size());
  EXPECT_EQ(kOpusCodec.ToCodecParameters(), rtp_parameters.codecs[0]);
  EXPECT_EQ(kPcmuCodec.ToCodecParameters(), rtp_parameters.codecs[1]);
}

// Test that GetRtpReceiverParameters returns an SSRC.
TEST_P(WebRtcVoiceEngineTestFake, GetRtpReceiveParametersSsrc) {
  EXPECT_TRUE(SetupRecvStream());
  RtpParameters rtp_parameters =
      receive_channel_->GetRtpReceiverParameters(kSsrcX);
  ASSERT_EQ(1u, rtp_parameters.encodings.size());
  EXPECT_EQ(kSsrcX, rtp_parameters.encodings[0].ssrc);
}

// Test that if we set/get parameters multiple times, we get the same results.
TEST_P(WebRtcVoiceEngineTestFake, SetAndGetRtpReceiveParameters) {
  EXPECT_TRUE(SetupRecvStream());
  AudioReceiverParameters parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs.push_back(kPcmuCodec);
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));

  RtpParameters initial_params =
      receive_channel_->GetRtpReceiverParameters(kSsrcX);

  // ... And this shouldn't change the params returned by
  // GetRtpReceiverParameters.
  RtpParameters new_params = receive_channel_->GetRtpReceiverParameters(kSsrcX);
  EXPECT_EQ(initial_params, receive_channel_->GetRtpReceiverParameters(kSsrcX));
}

// Test that GetRtpReceiverParameters returns parameters correctly when SSRCs
// aren't signaled. It should return an empty "RtpEncodingParameters" when
// configured to receive an unsignaled stream and no packets have been received
// yet, and start returning the SSRC once a packet has been received.
TEST_P(WebRtcVoiceEngineTestFake, GetRtpReceiveParametersWithUnsignaledSsrc) {
  ASSERT_TRUE(SetupChannel());
  // Call necessary methods to configure receiving a default stream as
  // soon as it arrives.
  AudioReceiverParameters parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs.push_back(kPcmuCodec);
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));

  // Call GetDefaultRtpReceiveParameters before configured to receive an
  // unsignaled stream. Should return nothing.
  EXPECT_EQ(RtpParameters(),
            receive_channel_->GetDefaultRtpReceiveParameters());

  // Set a sink for an unsignaled stream.
  std::unique_ptr<FakeAudioSink> fake_sink(new FakeAudioSink());
  receive_channel_->SetDefaultRawAudioSink(std::move(fake_sink));

  // Call GetDefaultRtpReceiveParameters before the SSRC is known.
  RtpParameters rtp_parameters =
      receive_channel_->GetDefaultRtpReceiveParameters();
  ASSERT_EQ(1u, rtp_parameters.encodings.size());
  EXPECT_FALSE(rtp_parameters.encodings[0].ssrc);

  // Receive PCMU packet (SSRC=1).
  DeliverPacket(kPcmuFrame);

  // The `ssrc` member should still be unset.
  rtp_parameters = receive_channel_->GetDefaultRtpReceiveParameters();
  ASSERT_EQ(1u, rtp_parameters.encodings.size());
  EXPECT_FALSE(rtp_parameters.encodings[0].ssrc);
}

TEST_P(WebRtcVoiceEngineTestFake, OnPacketReceivedIdentifiesExtensions) {
  ASSERT_TRUE(SetupChannel());
  AudioReceiverParameters parameters = recv_parameters_;
  parameters.extensions.push_back(
      RtpExtension(RtpExtension::kAudioLevelUri, RtpHeaderExtensionId(1)));
  ASSERT_TRUE(receive_channel_->SetReceiverParameters(parameters));
  run_loop_.Flush();
  RtpHeaderExtensionMap extension_map(parameters.extensions);
  RtpPacketReceived reference_packet(&extension_map);
  constexpr uint8_t kAudioLevel = 123;
  reference_packet.SetExtension<AudioLevelExtension>(
      AudioLevel(/*voice_activity=*/true, kAudioLevel));
  //  Create a packet without the extension map but with the same content.
  RtpPacketReceived received_packet(&extension_map);
  ASSERT_TRUE(received_packet.Parse(reference_packet.Buffer()));

  receive_channel_->OnPacketReceived(received_packet);
  run_loop_.Flush();

  AudioLevel audio_level;
  EXPECT_TRUE(
      call_.last_received_rtp_packet().GetExtension<AudioLevelExtension>(
          &audio_level));
  EXPECT_EQ(audio_level.level(), kAudioLevel);
}

// Test that we apply codecs properly.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecs) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs.push_back(kPcmuCodec);
  parameters.codecs.push_back(kCn8000Codec);
  parameters.codecs[0].id = 96;
  parameters.codecs[0].bitrate = 22000;
  SetSenderParameters(parameters);
  const auto& send_codec_spec = *GetSendStreamConfig(kSsrcX).send_codec_spec;
  EXPECT_EQ(96, send_codec_spec.payload_type);
  EXPECT_EQ(22000, send_codec_spec.target_bitrate_bps);
  EXPECT_STRCASEEQ("OPUS", send_codec_spec.format.name.c_str());
  EXPECT_NE(send_codec_spec.format.clockrate_hz, 8000);
  EXPECT_EQ(std::nullopt, send_codec_spec.cng_payload_type);
  EXPECT_FALSE(send_channel_->CanInsertDtmf());
}

// Test that we use Opus/Red by default when it is
// listed as the first codec and there is an fmtp line.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecsRed) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kRed48000Codec);
  parameters.codecs[0].params[""] = "111/111";
  parameters.codecs.push_back(kOpusCodec);
  SetSenderParameters(parameters);
  const auto& send_codec_spec = *GetSendStreamConfig(kSsrcX).send_codec_spec;
  EXPECT_EQ(111, send_codec_spec.payload_type);
  EXPECT_STRCASEEQ("opus", send_codec_spec.format.name.c_str());
  EXPECT_EQ(112, send_codec_spec.red_payload_type);
}

// Test that we do not use Opus/Red by default when it is
// listed as the first codec but there is no fmtp line.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecsRedNoFmtp) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kRed48000Codec);
  parameters.codecs.push_back(kOpusCodec);
  SetSenderParameters(parameters);
  const auto& send_codec_spec = *GetSendStreamConfig(kSsrcX).send_codec_spec;
  EXPECT_EQ(111, send_codec_spec.payload_type);
  EXPECT_STRCASEEQ("opus", send_codec_spec.format.name.c_str());
  EXPECT_EQ(std::nullopt, send_codec_spec.red_payload_type);
}

// Test that we do not use Opus/Red by default.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecsRedDefault) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs.push_back(kRed48000Codec);
  parameters.codecs[1].params[""] = "111/111";
  SetSenderParameters(parameters);
  const auto& send_codec_spec = *GetSendStreamConfig(kSsrcX).send_codec_spec;
  EXPECT_EQ(111, send_codec_spec.payload_type);
  EXPECT_STRCASEEQ("opus", send_codec_spec.format.name.c_str());
  EXPECT_EQ(std::nullopt, send_codec_spec.red_payload_type);
}

// Test that the RED fmtp line must match the payload type.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecsRedFmtpMismatch) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kRed48000Codec);
  parameters.codecs[0].params[""] = "8/8";
  parameters.codecs.push_back(kOpusCodec);
  SetSenderParameters(parameters);
  const auto& send_codec_spec = *GetSendStreamConfig(kSsrcX).send_codec_spec;
  EXPECT_EQ(111, send_codec_spec.payload_type);
  EXPECT_STRCASEEQ("opus", send_codec_spec.format.name.c_str());
  EXPECT_EQ(std::nullopt, send_codec_spec.red_payload_type);
}

// Test that the RED fmtp line must show 2..32 payloads.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecsRedFmtpAmountOfRedundancy) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kRed48000Codec);
  parameters.codecs[0].params[""] = "111";
  parameters.codecs.push_back(kOpusCodec);
  SetSenderParameters(parameters);
  const auto& send_codec_spec = *GetSendStreamConfig(kSsrcX).send_codec_spec;
  EXPECT_EQ(111, send_codec_spec.payload_type);
  EXPECT_STRCASEEQ("opus", send_codec_spec.format.name.c_str());
  EXPECT_EQ(std::nullopt, send_codec_spec.red_payload_type);
  for (int i = 1; i < 32; i++) {
    parameters.codecs[0].params[""] += "/111";
    SetSenderParameters(parameters);
    const auto& send_codec_spec2 = *GetSendStreamConfig(kSsrcX).send_codec_spec;
    EXPECT_EQ(111, send_codec_spec2.payload_type);
    EXPECT_STRCASEEQ("opus", send_codec_spec2.format.name.c_str());
    EXPECT_EQ(112, send_codec_spec2.red_payload_type);
  }
  parameters.codecs[0].params[""] += "/111";
  SetSenderParameters(parameters);
  const auto& send_codec_spec3 = *GetSendStreamConfig(kSsrcX).send_codec_spec;
  EXPECT_EQ(111, send_codec_spec3.payload_type);
  EXPECT_STRCASEEQ("opus", send_codec_spec3.format.name.c_str());
  EXPECT_EQ(std::nullopt, send_codec_spec3.red_payload_type);
}

// Test that we use Opus/Red by default if an unknown codec
// is before RED and Opus.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecRedWithUnknownCodec) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kUnknownCodec);
  parameters.codecs.push_back(kRed48000Codec);
  parameters.codecs.back().params[""] = "111/111";
  parameters.codecs.push_back(kOpusCodec);
  SetSenderParameters(parameters);
  const auto& send_codec_spec = *GetSendStreamConfig(kSsrcX).send_codec_spec;
  EXPECT_EQ(111, send_codec_spec.payload_type);
  EXPECT_STRCASEEQ("opus", send_codec_spec.format.name.c_str());
  EXPECT_EQ(112, send_codec_spec.red_payload_type);
}

// Test that WebRtcVoiceEngine reconfigures, rather than recreates its
// AudioSendStream.
TEST_P(WebRtcVoiceEngineTestFake, DontRecreateSendStream) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs.push_back(kPcmuCodec);
  parameters.codecs.push_back(kCn8000Codec);
  parameters.codecs[0].id = 96;
  parameters.codecs[0].bitrate = 48000;
  const int initial_num = call_.GetNumCreatedSendStreams();
  SetSenderParameters(parameters);
  EXPECT_EQ(initial_num, call_.GetNumCreatedSendStreams());
  // Calling SetSendCodec again with same codec which is already set.
  // In this case media channel shouldn't send codec to VoE.
  SetSenderParameters(parameters);
  EXPECT_EQ(initial_num, call_.GetNumCreatedSendStreams());
}

// TODO(ossu): Revisit if these tests need to be here, now that these kinds of
// tests should be available in AudioEncoderOpusTest.

// Test that if clockrate is not 48000 for opus, we do not have a send codec.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecOpusBadClockrate) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs[0].bitrate = 0;
  parameters.codecs[0].clockrate = 50000;
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));
  EXPECT_EQ(send_channel_->GetSendCodec(), std::nullopt);
}

// Test that if channels=0 for opus, we do not have a send codec.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecOpusBad0ChannelsNoStereo) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs[0].bitrate = 0;
  parameters.codecs[0].channels = 0;
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));
  EXPECT_EQ(send_channel_->GetSendCodec(), std::nullopt);
}

// Test that if channels=0 for opus, we do not have a send codec.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecOpusBad0Channels1Stereo) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs[0].bitrate = 0;
  parameters.codecs[0].channels = 0;
  parameters.codecs[0].params["stereo"] = "1";
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));
  EXPECT_EQ(send_channel_->GetSendCodec(), std::nullopt);
}

// Test that if channel is 1 for opus and there's no stereo, we do not have a
// send codec.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecOpus1ChannelNoStereo) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs[0].bitrate = 0;
  parameters.codecs[0].channels = 1;
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));
  EXPECT_EQ(send_channel_->GetSendCodec(), std::nullopt);
}

// Test that if channel is 1 for opus and stereo=0, we do not have a send codec.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecOpusBad1Channel0Stereo) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs[0].bitrate = 0;
  parameters.codecs[0].channels = 1;
  parameters.codecs[0].params["stereo"] = "0";
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));
  EXPECT_EQ(send_channel_->GetSendCodec(), std::nullopt);
}

// Test that if channel is 1 for opus and stereo=1, we do not have a send codec.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecOpusBad1Channel1Stereo) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs[0].bitrate = 0;
  parameters.codecs[0].channels = 1;
  parameters.codecs[0].params["stereo"] = "1";
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));
  EXPECT_EQ(send_channel_->GetSendCodec(), std::nullopt);
}

// Test that with bitrate=0 and no stereo, bitrate is 32000.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecOpusGood0BitrateNoStereo) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs[0].bitrate = 0;
  SetSenderParameters(parameters);
  CheckSendCodecBitrate(kSsrcX, "opus", 32000);
}

// Test that with bitrate=0 and stereo=0, bitrate is 32000.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecOpusGood0Bitrate0Stereo) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs[0].bitrate = 0;
  parameters.codecs[0].params["stereo"] = "0";
  SetSenderParameters(parameters);
  CheckSendCodecBitrate(kSsrcX, "opus", 32000);
}

// Test that with bitrate=invalid and stereo=0, bitrate is 32000.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecOpusGoodXBitrate0Stereo) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs[0].params["stereo"] = "0";
  // bitrate that's out of the range between 6000 and 510000 will be clamped.
  parameters.codecs[0].bitrate = 5999;
  SetSenderParameters(parameters);
  CheckSendCodecBitrate(kSsrcX, "opus", 6000);

  parameters.codecs[0].bitrate = 510001;
  SetSenderParameters(parameters);
  CheckSendCodecBitrate(kSsrcX, "opus", 510000);
}

// Test that with bitrate=0 and stereo=1, bitrate is 64000.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecOpusGood0Bitrate1Stereo) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs[0].bitrate = 0;
  parameters.codecs[0].params["stereo"] = "1";
  SetSenderParameters(parameters);
  CheckSendCodecBitrate(kSsrcX, "opus", 64000);
}

// Test that with bitrate=invalid and stereo=1, bitrate is 64000.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecOpusGoodXBitrate1Stereo) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs[0].params["stereo"] = "1";
  // bitrate that's out of the range between 6000 and 510000 will be clamped.
  parameters.codecs[0].bitrate = 5999;
  SetSenderParameters(parameters);
  CheckSendCodecBitrate(kSsrcX, "opus", 6000);

  parameters.codecs[0].bitrate = 510001;
  SetSenderParameters(parameters);
  CheckSendCodecBitrate(kSsrcX, "opus", 510000);
}

// Test that with bitrate=N and stereo unset, bitrate is N.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecOpusGoodNBitrateNoStereo) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs[0].bitrate = 96000;
  SetSenderParameters(parameters);
  const auto& spec = *GetSendStreamConfig(kSsrcX).send_codec_spec;
  EXPECT_EQ(111, spec.payload_type);
  EXPECT_EQ(96000, spec.target_bitrate_bps);
  EXPECT_EQ("opus", spec.format.name);
  EXPECT_EQ(2u, spec.format.num_channels);
  EXPECT_EQ(48000, spec.format.clockrate_hz);
}

// Test that with bitrate=N and stereo=0, bitrate is N.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecOpusGoodNBitrate0Stereo) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs[0].bitrate = 30000;
  parameters.codecs[0].params["stereo"] = "0";
  SetSenderParameters(parameters);
  CheckSendCodecBitrate(kSsrcX, "opus", 30000);
}

// Test that with bitrate=N and without any parameters, bitrate is N.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecOpusGoodNBitrateNoParameters) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs[0].bitrate = 30000;
  SetSenderParameters(parameters);
  CheckSendCodecBitrate(kSsrcX, "opus", 30000);
}

// Test that with bitrate=N and stereo=1, bitrate is N.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecOpusGoodNBitrate1Stereo) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs[0].bitrate = 30000;
  parameters.codecs[0].params["stereo"] = "1";
  SetSenderParameters(parameters);
  CheckSendCodecBitrate(kSsrcX, "opus", 30000);
}

TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecsWithBitrates) {
  SetSendCodecsShouldWorkForBitrates("100", 100000, "150", 150000, "200",
                                     200000);
}

TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecsWithHighMaxBitrate) {
  SetSendCodecsShouldWorkForBitrates("", 0, "", -1, "10000", 10000000);
}

TEST_P(WebRtcVoiceEngineTestFake,
       SetSendCodecsWithoutBitratesUsesCorrectDefaults) {
  SetSendCodecsShouldWorkForBitrates("", 0, "", -1, "", -1);
}

TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecsCapsMinAndStartBitrate) {
  SetSendCodecsShouldWorkForBitrates("-1", 0, "-100", -1, "", -1);
}

TEST_P(WebRtcVoiceEngineTestFake, SetMaxSendBandwidthForAudioDoesntAffectBwe) {
  SetSendCodecsShouldWorkForBitrates("100", 100000, "150", 150000, "200",
                                     200000);
  send_parameters_.max_bandwidth_bps = 100000;
  // Setting max bitrate should keep previous min bitrate
  // Setting max bitrate should not reset start bitrate.
  EXPECT_CALL(*call_.GetMockTransportControllerSend(),
              SetSdpBitrateParameters(
                  AllOf(Field(&BitrateConstraints::min_bitrate_bps, 100000),
                        Field(&BitrateConstraints::start_bitrate_bps, -1),
                        Field(&BitrateConstraints::max_bitrate_bps, 200000))));
  SetSenderParameters(send_parameters_);
}

// Test that we can enable NACK with opus as callee.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecEnableNackAsCallee) {
  EXPECT_TRUE(SetupRecvStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs[0].AddFeedbackParam(
      FeedbackParam(kRtcpFbParamNack, kParamValueEmpty));
  EXPECT_EQ(0, GetRecvStreamConfig(kSsrcX).rtp.nack.rtp_history_ms);
  SetSenderParameters(parameters);
  // NACK should be enabled even with no send stream.
  EXPECT_EQ(kRtpHistoryMs, GetRecvStreamConfig(kSsrcX).rtp.nack.rtp_history_ms);

  EXPECT_TRUE(send_channel_->AddSendStream(StreamParams::CreateLegacy(kSsrcX)));
}

// Test that we can enable NACK on receive streams.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecEnableNackRecvStreams) {
  EXPECT_TRUE(SetupSendStream());
  EXPECT_TRUE(AddRecvStream(kSsrcY));
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs[0].AddFeedbackParam(
      FeedbackParam(kRtcpFbParamNack, kParamValueEmpty));
  EXPECT_EQ(0, GetRecvStreamConfig(kSsrcY).rtp.nack.rtp_history_ms);
  SetSenderParameters(parameters);
  EXPECT_EQ(kRtpHistoryMs, GetRecvStreamConfig(kSsrcY).rtp.nack.rtp_history_ms);
}

// Test that we can disable NACK on receive streams.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecDisableNackRecvStreams) {
  EXPECT_TRUE(SetupSendStream());
  EXPECT_TRUE(AddRecvStream(kSsrcY));
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs[0].AddFeedbackParam(
      FeedbackParam(kRtcpFbParamNack, kParamValueEmpty));
  SetSenderParameters(parameters);
  EXPECT_EQ(kRtpHistoryMs, GetRecvStreamConfig(kSsrcY).rtp.nack.rtp_history_ms);

  parameters.codecs.clear();
  parameters.codecs.push_back(kOpusCodec);
  SetSenderParameters(parameters);
  EXPECT_EQ(0, GetRecvStreamConfig(kSsrcY).rtp.nack.rtp_history_ms);
}

// Test that NACK is enabled on a new receive stream.
TEST_P(WebRtcVoiceEngineTestFake, AddRecvStreamEnableNack) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs.push_back(kCn16000Codec);
  parameters.codecs[0].AddFeedbackParam(
      FeedbackParam(kRtcpFbParamNack, kParamValueEmpty));
  SetSenderParameters(parameters);

  EXPECT_TRUE(AddRecvStream(kSsrcY));
  EXPECT_EQ(kRtpHistoryMs, GetRecvStreamConfig(kSsrcY).rtp.nack.rtp_history_ms);
  EXPECT_TRUE(AddRecvStream(kSsrcZ));
  EXPECT_EQ(kRtpHistoryMs, GetRecvStreamConfig(kSsrcZ).rtp.nack.rtp_history_ms);
}

// Test that we can enable RTCP reduced size mode with opus as callee.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecEnableRtcpReducedSizeAsCallee) {
  EXPECT_TRUE(SetupRecvStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.rtcp.reduced_size = true;
  EXPECT_EQ(RtcpMode::kCompound, GetRecvStreamConfig(kSsrcX).rtp.rtcp_mode);
  SetSenderParameters(parameters);
  // Reduced size mode should be enabled even with no send stream.
  EXPECT_EQ(RtcpMode::kReducedSize, GetRecvStreamConfig(kSsrcX).rtp.rtcp_mode);

  EXPECT_TRUE(send_channel_->AddSendStream(StreamParams::CreateLegacy(kSsrcX)));
}

// Test that we can enable RTCP reduced size mode on receive streams.
TEST_P(WebRtcVoiceEngineTestFake,
       SetSendCodecEnableRtcpReducedSizeRecvStreams) {
  EXPECT_TRUE(SetupSendStream());
  EXPECT_TRUE(AddRecvStream(kSsrcY));
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.rtcp.reduced_size = true;
  EXPECT_EQ(RtcpMode::kCompound, GetRecvStreamConfig(kSsrcY).rtp.rtcp_mode);
  SetSenderParameters(parameters);
  EXPECT_EQ(RtcpMode::kReducedSize, GetRecvStreamConfig(kSsrcY).rtp.rtcp_mode);
}

// Test that we can disable RTCP reduced size mode on receive streams.
TEST_P(WebRtcVoiceEngineTestFake,
       SetSendCodecDisableRtcpReducedSizeRecvStreams) {
  EXPECT_TRUE(SetupSendStream());
  EXPECT_TRUE(AddRecvStream(kSsrcY));
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.rtcp.reduced_size = true;
  SetSenderParameters(parameters);
  EXPECT_EQ(RtcpMode::kReducedSize, GetRecvStreamConfig(kSsrcY).rtp.rtcp_mode);

  parameters.rtcp.reduced_size = false;
  SetSenderParameters(parameters);
  EXPECT_EQ(RtcpMode::kCompound, GetRecvStreamConfig(kSsrcY).rtp.rtcp_mode);
}

// Test that RTCP reduced size mode is enabled on a new receive stream.
TEST_P(WebRtcVoiceEngineTestFake, AddRecvStreamEnableRtcpReducedSize) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs.push_back(kCn16000Codec);
  parameters.rtcp.reduced_size = true;
  SetSenderParameters(parameters);

  EXPECT_TRUE(AddRecvStream(kSsrcY));
  EXPECT_EQ(RtcpMode::kReducedSize, GetRecvStreamConfig(kSsrcY).rtp.rtcp_mode);
  EXPECT_TRUE(AddRecvStream(kSsrcZ));
  EXPECT_EQ(RtcpMode::kReducedSize, GetRecvStreamConfig(kSsrcZ).rtp.rtcp_mode);
}

// Test that we can switch back and forth between Opus and PCMU with CN.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecsOpusPcmuSwitching) {
  EXPECT_TRUE(SetupSendStream());

  AudioSenderParameter opus_parameters;
  opus_parameters.codecs.push_back(kOpusCodec);
  SetSenderParameters(opus_parameters);
  {
    const auto& spec = *GetSendStreamConfig(kSsrcX).send_codec_spec;
    EXPECT_EQ(111, spec.payload_type);
    EXPECT_STRCASEEQ("opus", spec.format.name.c_str());
  }

  AudioSenderParameter pcmu_parameters;
  pcmu_parameters.codecs.push_back(kPcmuCodec);
  pcmu_parameters.codecs.push_back(kCn16000Codec);
  pcmu_parameters.codecs.push_back(kOpusCodec);
  SetSenderParameters(pcmu_parameters);
  {
    const auto& spec = *GetSendStreamConfig(kSsrcX).send_codec_spec;
    EXPECT_EQ(0, spec.payload_type);
    EXPECT_STRCASEEQ("PCMU", spec.format.name.c_str());
  }

  SetSenderParameters(opus_parameters);
  {
    const auto& spec = *GetSendStreamConfig(kSsrcX).send_codec_spec;
    EXPECT_EQ(111, spec.payload_type);
    EXPECT_STRCASEEQ("opus", spec.format.name.c_str());
  }
}

// Test that we handle various ways of specifying bitrate.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecsBitrate) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kPcmuCodec);
  SetSenderParameters(parameters);
  {
    const auto& spec = *GetSendStreamConfig(kSsrcX).send_codec_spec;
    EXPECT_EQ(0, spec.payload_type);
    EXPECT_STRCASEEQ("PCMU", spec.format.name.c_str());
    EXPECT_EQ(64000, spec.target_bitrate_bps);
  }

  parameters.codecs[0].bitrate = 0;  // bitrate == default
  SetSenderParameters(parameters);
  {
    const auto& spec = *GetSendStreamConfig(kSsrcX).send_codec_spec;
    EXPECT_EQ(0, spec.payload_type);
    EXPECT_STREQ("PCMU", spec.format.name.c_str());
    EXPECT_EQ(64000, spec.target_bitrate_bps);
  }

  parameters.codecs[0] = kOpusCodec;
  parameters.codecs[0].bitrate = 0;  // bitrate == default
  SetSenderParameters(parameters);
  {
    const auto& spec = *GetSendStreamConfig(kSsrcX).send_codec_spec;
    EXPECT_EQ(111, spec.payload_type);
    EXPECT_STREQ("opus", spec.format.name.c_str());
    EXPECT_EQ(32000, spec.target_bitrate_bps);
  }
}

// Test that we do not fail if no codecs are specified.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecsNoCodecs) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  EXPECT_TRUE(send_channel_->SetSenderParameters(parameters));
  EXPECT_EQ(send_channel_->GetSendCodec(), std::nullopt);
}

// Test that we can set send codecs even with telephone-event codec as the first
// one on the list.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecsDTMFOnTop) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kTelephoneEventCodec1);
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs.push_back(kPcmuCodec);
  parameters.codecs[0].id = 98;  // DTMF
  parameters.codecs[1].id = 96;
  SetSenderParameters(parameters);
  const auto& spec = *GetSendStreamConfig(kSsrcX).send_codec_spec;
  EXPECT_EQ(96, spec.payload_type);
  EXPECT_STRCASEEQ("OPUS", spec.format.name.c_str());
  SetSend(true);
  EXPECT_TRUE(send_channel_->CanInsertDtmf());
}

// Test that CanInsertDtmf() is governed by the send flag
TEST_P(WebRtcVoiceEngineTestFake, DTMFControlledBySendFlag) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kTelephoneEventCodec1);
  parameters.codecs.push_back(kPcmuCodec);
  parameters.codecs[0].id = 98;  // DTMF
  parameters.codecs[1].id = 96;
  SetSenderParameters(parameters);
  EXPECT_FALSE(send_channel_->CanInsertDtmf());
  SetSend(true);
  EXPECT_TRUE(send_channel_->CanInsertDtmf());
  SetSend(false);
  EXPECT_FALSE(send_channel_->CanInsertDtmf());
}

// Test that payload type range is limited for telephone-event codec.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecsDTMFPayloadTypeOutOfRange) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kTelephoneEventCodec2);
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs[0].id = 0;  // DTMF
  parameters.codecs[1].id = 96;
  SetSenderParameters(parameters);
  SetSend(true);
  EXPECT_TRUE(send_channel_->CanInsertDtmf());
  parameters.codecs[0].id = 128;  // DTMF
  EXPECT_FALSE(send_channel_->SetSenderParameters(parameters));
  EXPECT_FALSE(send_channel_->CanInsertDtmf());
  parameters.codecs[0].id = 127;
  SetSenderParameters(parameters);
  EXPECT_TRUE(send_channel_->CanInsertDtmf());
  parameters.codecs[0].id = -1;  // DTMF
  EXPECT_FALSE(send_channel_->SetSenderParameters(parameters));
  EXPECT_FALSE(send_channel_->CanInsertDtmf());
}

// Test that we can set send codecs even with CN codec as the first
// one on the list.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecsCNOnTop) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kCn8000Codec);
  parameters.codecs.push_back(kPcmuCodec);
  parameters.codecs[0].id = 98;  // narrowband CN
  SetSenderParameters(parameters);
  const auto& send_codec_spec = *GetSendStreamConfig(kSsrcX).send_codec_spec;
  EXPECT_EQ(0, send_codec_spec.payload_type);
  EXPECT_STRCASEEQ("PCMU", send_codec_spec.format.name.c_str());
  EXPECT_EQ(98, send_codec_spec.cng_payload_type);
}

// Test that we set VAD and DTMF types correctly as caller.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecsCNandDTMFAsCaller) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kPcmuCodec);
  parameters.codecs.push_back(kCn16000Codec);
  parameters.codecs.push_back(kCn8000Codec);
  parameters.codecs.push_back(kTelephoneEventCodec1);
  parameters.codecs[0].id = 96;
  parameters.codecs[2].id = 97;  // narrowband CN
  parameters.codecs[3].id = 98;  // DTMF
  SetSenderParameters(parameters);
  const auto& send_codec_spec = *GetSendStreamConfig(kSsrcX).send_codec_spec;
  EXPECT_EQ(96, send_codec_spec.payload_type);
  EXPECT_STRCASEEQ("PCMU", send_codec_spec.format.name.c_str());
  EXPECT_EQ(1u, send_codec_spec.format.num_channels);
  EXPECT_EQ(97, send_codec_spec.cng_payload_type);
  SetSend(true);
  EXPECT_TRUE(send_channel_->CanInsertDtmf());
}

// Test that we set VAD and DTMF types correctly as callee.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecsCNandDTMFAsCallee) {
  EXPECT_TRUE(SetupChannel());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kPcmuCodec);
  parameters.codecs.push_back(kCn16000Codec);
  parameters.codecs.push_back(kCn8000Codec);
  parameters.codecs.push_back(kTelephoneEventCodec2);
  parameters.codecs[0].id = 96;
  parameters.codecs[2].id = 97;  // narrowband CN
  parameters.codecs[3].id = 98;  // DTMF
  SetSenderParameters(parameters);
  EXPECT_TRUE(send_channel_->AddSendStream(StreamParams::CreateLegacy(kSsrcX)));

  const auto& send_codec_spec = *GetSendStreamConfig(kSsrcX).send_codec_spec;
  EXPECT_EQ(96, send_codec_spec.payload_type);
  EXPECT_STRCASEEQ("PCMU", send_codec_spec.format.name.c_str());
  EXPECT_EQ(1u, send_codec_spec.format.num_channels);
  EXPECT_EQ(97, send_codec_spec.cng_payload_type);
  SetSend(true);
  EXPECT_TRUE(send_channel_->CanInsertDtmf());
}

// Test that we only apply VAD if we have a CN codec that matches the
// send codec clockrate.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecsCNNoMatch) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  // Set PCMU(8K) and CN(16K). VAD should not be activated.
  parameters.codecs.push_back(kPcmuCodec);
  parameters.codecs.push_back(kCn16000Codec);
  parameters.codecs[1].id = 97;
  SetSenderParameters(parameters);
  {
    const auto& send_codec_spec = *GetSendStreamConfig(kSsrcX).send_codec_spec;
    EXPECT_STRCASEEQ("PCMU", send_codec_spec.format.name.c_str());
    EXPECT_EQ(std::nullopt, send_codec_spec.cng_payload_type);
  }
  // Set PCMU(8K) and CN(8K). VAD should be activated.
  parameters.codecs[1] = kCn8000Codec;
  SetSenderParameters(parameters);
  {
    const auto& send_codec_spec = *GetSendStreamConfig(kSsrcX).send_codec_spec;
    EXPECT_STRCASEEQ("PCMU", send_codec_spec.format.name.c_str());
    EXPECT_EQ(1u, send_codec_spec.format.num_channels);
    EXPECT_EQ(13, send_codec_spec.cng_payload_type);
  }
  // Set OPUS(48K) and CN(8K). VAD should not be activated.
  parameters.codecs[0] = kOpusCodec;
  SetSenderParameters(parameters);
  {
    const auto& send_codec_spec = *GetSendStreamConfig(kSsrcX).send_codec_spec;
    EXPECT_STRCASEEQ("OPUS", send_codec_spec.format.name.c_str());
    EXPECT_EQ(std::nullopt, send_codec_spec.cng_payload_type);
  }
}

// Test that we perform case-insensitive matching of codec names.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecsCaseInsensitive) {
  EXPECT_TRUE(SetupSendStream());
  AudioSenderParameter parameters;
  parameters.codecs.push_back(kPcmuCodec);
  parameters.codecs.push_back(kCn16000Codec);
  parameters.codecs.push_back(kCn8000Codec);
  parameters.codecs.push_back(kTelephoneEventCodec1);
  parameters.codecs[0].name = "PcMu";
  parameters.codecs[0].id = 96;
  parameters.codecs[2].id = 97;  // narrowband CN
  parameters.codecs[3].id = 98;  // DTMF
  SetSenderParameters(parameters);
  const auto& send_codec_spec = *GetSendStreamConfig(kSsrcX).send_codec_spec;
  EXPECT_EQ(96, send_codec_spec.payload_type);
  EXPECT_STRCASEEQ("PCMU", send_codec_spec.format.name.c_str());
  EXPECT_EQ(1u, send_codec_spec.format.num_channels);
  EXPECT_EQ(97, send_codec_spec.cng_payload_type);
  SetSend(true);
  EXPECT_TRUE(send_channel_->CanInsertDtmf());
}

TEST_P(WebRtcVoiceEngineTestFake,
       SupportsTransportSequenceNumberHeaderExtension) {
  const std::vector<RtpHeaderExtensionCapability> header_extensions =
      GetDefaultEnabledRtpHeaderCapabilities(*engine_,
                                             /* field_trials= */ nullptr);
  EXPECT_THAT(
      header_extensions,
      Contains(::testing::Field("uri", &RtpHeaderExtensionCapability::uri,
                                RtpExtension::kTransportSequenceNumberUri)));
}

// Test support for audio level header extension.
TEST_P(WebRtcVoiceEngineTestFake, SendAudioLevelHeaderExtensions) {
  TestSetSendRtpHeaderExtensions(RtpExtension::kAudioLevelUri);
}
TEST_P(WebRtcVoiceEngineTestFake, RecvAudioLevelHeaderExtensions) {
  TestSetRecvRtpHeaderExtensions(RtpExtension::kAudioLevelUri);
}

// Test support for transport sequence number header extension.
TEST_P(WebRtcVoiceEngineTestFake, SendTransportSequenceNumberHeaderExtensions) {
  TestSetSendRtpHeaderExtensions(RtpExtension::kTransportSequenceNumberUri);
}
TEST_P(WebRtcVoiceEngineTestFake, RecvTransportSequenceNumberHeaderExtensions) {
  TestSetRecvRtpHeaderExtensions(RtpExtension::kTransportSequenceNumberUri);
}

// Test that we can create a channel and start sending on it.
TEST_P(WebRtcVoiceEngineTestFake, Send) {
  EXPECT_TRUE(SetupSendStream());
  SetSenderParameters(send_parameters_);
  SetSend(true);
  EXPECT_TRUE(GetSendStream(kSsrcX).IsSending());
  SetSend(false);
  EXPECT_FALSE(GetSendStream(kSsrcX).IsSending());
}

// Test that a channel is muted/unmuted.
TEST_P(WebRtcVoiceEngineTestFake, SendStateMuteUnmute) {
  EXPECT_TRUE(SetupSendStream());
  SetSenderParameters(send_parameters_);
  EXPECT_FALSE(GetSendStream(kSsrcX).muted());
  SetAudioSend(kSsrcX, true, nullptr);
  EXPECT_FALSE(GetSendStream(kSsrcX).muted());
  SetAudioSend(kSsrcX, false, nullptr);
  EXPECT_TRUE(GetSendStream(kSsrcX).muted());
}

// Test that SetSenderParameters() does not alter a stream's send state.
TEST_P(WebRtcVoiceEngineTestFake, SendStateWhenStreamsAreRecreated) {
  EXPECT_TRUE(SetupSendStream());
  EXPECT_FALSE(GetSendStream(kSsrcX).IsSending());

  // Turn on sending.
  SetSend(true);
  EXPECT_TRUE(GetSendStream(kSsrcX).IsSending());

  // Changing RTP header extensions will recreate the AudioSendStream.
  send_parameters_.extensions.push_back(
      RtpExtension(RtpExtension::kAudioLevelUri, RtpHeaderExtensionId(12)));
  SetSenderParameters(send_parameters_);
  EXPECT_TRUE(GetSendStream(kSsrcX).IsSending());

  // Turn off sending.
  SetSend(false);
  EXPECT_FALSE(GetSendStream(kSsrcX).IsSending());

  // Changing RTP header extensions will recreate the AudioSendStream.
  send_parameters_.extensions.clear();
  SetSenderParameters(send_parameters_);
  EXPECT_FALSE(GetSendStream(kSsrcX).IsSending());
}

// Test that we can create a channel and start playing out on it.
TEST_P(WebRtcVoiceEngineTestFake, Playout) {
  EXPECT_TRUE(SetupRecvStream());
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(recv_parameters_));
  receive_channel_->SetReceive(true);
  EXPECT_TRUE(GetRecvStream(kSsrcX).started());
  receive_channel_->SetReceive(false);
  EXPECT_FALSE(GetRecvStream(kSsrcX).started());
}

// Test that we can add and remove send streams.
TEST_P(WebRtcVoiceEngineTestFake, CreateAndDeleteMultipleSendStreams) {
  SetupForMultiSendStream();

  // Set the global state for sending.
  SetSend(true);

  for (uint32_t ssrc : kSsrcs4) {
    EXPECT_TRUE(send_channel_->AddSendStream(StreamParams::CreateLegacy(ssrc)));
    SetAudioSend(ssrc, true, &fake_source_);
    // Verify that we are in a sending state for all the created streams.
    EXPECT_TRUE(GetSendStream(ssrc).IsSending());
  }
  EXPECT_EQ(std::size(kSsrcs4), call_.GetAudioSendStreams().size());

  // Delete the send streams.
  for (uint32_t ssrc : kSsrcs4) {
    EXPECT_TRUE(send_channel_->RemoveSendStream(ssrc));
    EXPECT_FALSE(call_.GetAudioSendStream(ssrc));
    EXPECT_FALSE(send_channel_->RemoveSendStream(ssrc));
  }
  EXPECT_EQ(0u, call_.GetAudioSendStreams().size());
}

// Test SetSendCodecs correctly configure the codecs in all send streams.
TEST_P(WebRtcVoiceEngineTestFake, SetSendCodecsWithMultipleSendStreams) {
  SetupForMultiSendStream();

  // Create send streams.
  for (uint32_t ssrc : kSsrcs4) {
    EXPECT_TRUE(send_channel_->AddSendStream(StreamParams::CreateLegacy(ssrc)));
  }

  AudioSenderParameter parameters;
  // Set PCMU and CN(8K). VAD should be activated.
  parameters.codecs.push_back(kPcmuCodec);
  parameters.codecs.push_back(kCn8000Codec);
  parameters.codecs[1].id = 97;
  SetSenderParameters(parameters);

  // Verify PCMU and VAD are corrected configured on all send channels.
  for (uint32_t ssrc : kSsrcs4) {
    ASSERT_TRUE(call_.GetAudioSendStream(ssrc) != nullptr);
    const auto& send_codec_spec =
        *call_.GetAudioSendStream(ssrc)->GetConfig().send_codec_spec;
    EXPECT_STRCASEEQ("PCMU", send_codec_spec.format.name.c_str());
    EXPECT_EQ(1u, send_codec_spec.format.num_channels);
    EXPECT_EQ(97, send_codec_spec.cng_payload_type);
  }

  // Change to PCMU(8K) and CN(16K).
  parameters.codecs[0] = kPcmuCodec;
  parameters.codecs[1] = kCn16000Codec;
  SetSenderParameters(parameters);
  for (uint32_t ssrc : kSsrcs4) {
    ASSERT_TRUE(call_.GetAudioSendStream(ssrc) != nullptr);
    const auto& send_codec_spec =
        *call_.GetAudioSendStream(ssrc)->GetConfig().send_codec_spec;
    EXPECT_STRCASEEQ("PCMU", send_codec_spec.format.name.c_str());
    EXPECT_EQ(std::nullopt, send_codec_spec.cng_payload_type);
  }
}

// Test we can SetSend on all send streams correctly.
TEST_P(WebRtcVoiceEngineTestFake, SetSendWithMultipleSendStreams) {
  SetupForMultiSendStream();

  // Create the send channels and they should be a "not sending" date.
  for (uint32_t ssrc : kSsrcs4) {
    EXPECT_TRUE(send_channel_->AddSendStream(StreamParams::CreateLegacy(ssrc)));
    SetAudioSend(ssrc, true, &fake_source_);
    EXPECT_FALSE(GetSendStream(ssrc).IsSending());
  }

  // Set the global state for starting sending.
  SetSend(true);
  for (uint32_t ssrc : kSsrcs4) {
    // Verify that we are in a sending state for all the send streams.
    EXPECT_TRUE(GetSendStream(ssrc).IsSending());
  }

  // Set the global state for stopping sending.
  SetSend(false);
  for (uint32_t ssrc : kSsrcs4) {
    // Verify that we are in a stop state for all the send streams.
    EXPECT_FALSE(GetSendStream(ssrc).IsSending());
  }
}

// Test we can set the correct statistics on all send streams.
TEST_P(WebRtcVoiceEngineTestFake, GetStatsWithMultipleSendStreams) {
  SetupForMultiSendStream();

  // Create send streams.
  for (uint32_t ssrc : kSsrcs4) {
    EXPECT_TRUE(send_channel_->AddSendStream(StreamParams::CreateLegacy(ssrc)));
  }

  // Create a receive stream to check that none of the send streams end up in
  // the receive stream stats.
  EXPECT_TRUE(AddRecvStream(kSsrcY));

  // We need send codec to be set to get all stats.
  SetSenderParameters(send_parameters_);
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(recv_parameters_));
  SetAudioSendStreamStats();
  SetAudioReceiveStreamStats();

  // Check stats for the added streams.
  {
    EXPECT_CALL(*adm_, GetPlayoutUnderrunCount()).WillOnce(Return(0));
    VoiceMediaSendInfo send_info;
    VoiceMediaReceiveInfo receive_info;
    EXPECT_EQ(true, send_channel_->GetStats(&send_info));
    EXPECT_EQ(true, receive_channel_->GetStats(
                        &receive_info, /*get_and_clear_legacy_stats=*/true));

    // We have added 4 send streams. We should see empty stats for all.
    EXPECT_EQ(std::size(kSsrcs4), send_info.senders.size());
    for (const auto& sender : send_info.senders) {
      VerifyVoiceSenderInfo(sender, false);
    }
    VerifyVoiceSendRecvCodecs(send_info, receive_info);

    // We have added one receive stream. We should see empty stats.
    EXPECT_EQ(receive_info.receivers.size(), 1u);
    EXPECT_EQ(receive_info.receivers[0].ssrc(), 123u);
  }

  // Remove the kSsrcY stream. No receiver stats.
  {
    VoiceMediaReceiveInfo receive_info;
    VoiceMediaSendInfo send_info;
    EXPECT_TRUE(receive_channel_->RemoveRecvStream(kSsrcY));
    EXPECT_CALL(*adm_, GetPlayoutUnderrunCount()).WillOnce(Return(0));
    EXPECT_EQ(true, send_channel_->GetStats(&send_info));
    EXPECT_EQ(true, receive_channel_->GetStats(
                        &receive_info, /*get_and_clear_legacy_stats=*/true));
    EXPECT_EQ(std::size(kSsrcs4), send_info.senders.size());
    EXPECT_EQ(0u, receive_info.receivers.size());
  }

  // Deliver a new packet - a default receive stream should be created and we
  // should see stats again.
  {
    VoiceMediaSendInfo send_info;
    VoiceMediaReceiveInfo receive_info;
    DeliverPacket(kPcmuFrame);
    SetAudioReceiveStreamStats();
    EXPECT_CALL(*adm_, GetPlayoutUnderrunCount()).WillOnce(Return(0));
    EXPECT_EQ(true, send_channel_->GetStats(&send_info));
    EXPECT_EQ(true, receive_channel_->GetStats(
                        &receive_info, /*get_and_clear_legacy_stats=*/true));
    EXPECT_EQ(std::size(kSsrcs4), send_info.senders.size());
    EXPECT_EQ(1u, receive_info.receivers.size());
    VerifyVoiceReceiverInfo(receive_info.receivers[0]);
    VerifyVoiceSendRecvCodecs(send_info, receive_info);
  }
}

// Test that we can add and remove receive streams, and do proper send/playout.
// We can receive on multiple streams while sending one stream.
TEST_P(WebRtcVoiceEngineTestFake, PlayoutWithMultipleStreams) {
  EXPECT_TRUE(SetupSendStream());

  // Start playout without a receive stream.
  SetSenderParameters(send_parameters_);
  receive_channel_->SetReceive(true);

  // Adding another stream should enable playout on the new stream only.
  EXPECT_TRUE(AddRecvStream(kSsrcY));
  SetSend(true);
  EXPECT_TRUE(GetSendStream(kSsrcX).IsSending());

  // Make sure only the new stream is played out.
  EXPECT_TRUE(GetRecvStream(kSsrcY).started());

  // Adding yet another stream should have stream 2 and 3 enabled for playout.
  EXPECT_TRUE(AddRecvStream(kSsrcZ));
  EXPECT_TRUE(GetRecvStream(kSsrcY).started());
  EXPECT_TRUE(GetRecvStream(kSsrcZ).started());

  // Stop sending.
  SetSend(false);
  EXPECT_FALSE(GetSendStream(kSsrcX).IsSending());

  // Stop playout.
  receive_channel_->SetReceive(false);
  EXPECT_FALSE(GetRecvStream(kSsrcY).started());
  EXPECT_FALSE(GetRecvStream(kSsrcZ).started());

  // Restart playout and make sure recv streams are played out.
  receive_channel_->SetReceive(true);
  EXPECT_TRUE(GetRecvStream(kSsrcY).started());
  EXPECT_TRUE(GetRecvStream(kSsrcZ).started());

  // Now remove the recv streams.
  EXPECT_TRUE(receive_channel_->RemoveRecvStream(kSsrcZ));
  EXPECT_TRUE(receive_channel_->RemoveRecvStream(kSsrcY));
}

TEST_P(WebRtcVoiceEngineTestFake, SetAudioNetworkAdaptorViaOptions) {
  EXPECT_TRUE(SetupSendStream());
  send_parameters_.options.audio_network_adaptor = true;
  send_parameters_.options.audio_network_adaptor_config = {"1234"};
  SetSenderParameters(send_parameters_);
  EXPECT_EQ(send_parameters_.options.audio_network_adaptor_config,
            GetAudioNetworkAdaptorConfig(kSsrcX));
}

TEST_P(WebRtcVoiceEngineTestFake, AudioSendResetAudioNetworkAdaptor) {
  EXPECT_TRUE(SetupSendStream());
  send_parameters_.options.audio_network_adaptor = true;
  send_parameters_.options.audio_network_adaptor_config = {"1234"};
  SetSenderParameters(send_parameters_);
  EXPECT_EQ(send_parameters_.options.audio_network_adaptor_config,
            GetAudioNetworkAdaptorConfig(kSsrcX));
  AudioOptions options;
  options.audio_network_adaptor = false;
  SetAudioSend(kSsrcX, true, nullptr, &options);
  EXPECT_EQ(std::nullopt, GetAudioNetworkAdaptorConfig(kSsrcX));
}

TEST_P(WebRtcVoiceEngineTestFake, AudioNetworkAdaptorNotGetOverridden) {
  EXPECT_TRUE(SetupSendStream());
  send_parameters_.options.audio_network_adaptor = true;
  send_parameters_.options.audio_network_adaptor_config = {"1234"};
  SetSenderParameters(send_parameters_);
  EXPECT_EQ(send_parameters_.options.audio_network_adaptor_config,
            GetAudioNetworkAdaptorConfig(kSsrcX));
  const int initial_num = call_.GetNumCreatedSendStreams();
  AudioOptions options;
  options.audio_network_adaptor = std::nullopt;
  // Unvalued `options.audio_network_adaptor` should not reset audio network
  // adaptor.
  SetAudioSend(kSsrcX, true, nullptr, &options);
  // AudioSendStream not expected to be recreated.
  EXPECT_EQ(initial_num, call_.GetNumCreatedSendStreams());
  EXPECT_EQ(send_parameters_.options.audio_network_adaptor_config,
            GetAudioNetworkAdaptorConfig(kSsrcX));
}

// Test that we can set the outgoing SSRC properly.
// SSRC is set in SetupSendStream() by calling AddSendStream.
TEST_P(WebRtcVoiceEngineTestFake, SetSendSsrc) {
  EXPECT_TRUE(SetupSendStream());
  EXPECT_TRUE(call_.GetAudioSendStream(kSsrcX));
}

TEST_P(WebRtcVoiceEngineTestFake, GetStats) {
  // Setup. We need send codec to be set to get all stats.
  EXPECT_TRUE(SetupSendStream());
  // SetupSendStream adds a send stream with kSsrcX, so the receive
  // stream has to use a different SSRC.
  EXPECT_TRUE(AddRecvStream(kSsrcY));
  SetSenderParameters(send_parameters_);
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(recv_parameters_));
  SetAudioSendStreamStats();

  // Check stats for the added streams.
  {
    EXPECT_CALL(*adm_, GetPlayoutUnderrunCount()).WillOnce(Return(0));
    VoiceMediaSendInfo send_info;
    VoiceMediaReceiveInfo receive_info;
    EXPECT_EQ(true, send_channel_->GetStats(&send_info));
    EXPECT_EQ(true, receive_channel_->GetStats(
                        &receive_info, /*get_and_clear_legacy_stats=*/true));

    // We have added one send stream. We should see the stats we've set.
    EXPECT_EQ(1u, send_info.senders.size());
    VerifyVoiceSenderInfo(send_info.senders[0], false);
    // We have added one receive stream. We should see empty stats.
    EXPECT_EQ(receive_info.receivers.size(), 1u);
    EXPECT_EQ(receive_info.receivers[0].ssrc(), 0u);
  }

  // Start sending - this affects some reported stats.
  {
    SetSend(true);
    EXPECT_CALL(*adm_, GetPlayoutUnderrunCount()).WillOnce(Return(0));
    VoiceMediaSendInfo send_info;
    VoiceMediaReceiveInfo receive_info;
    SetAudioReceiveStreamStats();
    EXPECT_EQ(true, send_channel_->GetStats(&send_info));
    EXPECT_EQ(true, receive_channel_->GetStats(
                        &receive_info, /*get_and_clear_legacy_stats=*/true));
    VerifyVoiceSenderInfo(send_info.senders[0], true);
    VerifyVoiceSendRecvCodecs(send_info, receive_info);
  }

  // Remove the kSsrcY stream. No receiver stats.
  {
    EXPECT_TRUE(receive_channel_->RemoveRecvStream(kSsrcY));
    EXPECT_CALL(*adm_, GetPlayoutUnderrunCount()).WillOnce(Return(0));
    VoiceMediaSendInfo send_info;
    VoiceMediaReceiveInfo receive_info;
    EXPECT_EQ(true, send_channel_->GetStats(&send_info));
    EXPECT_EQ(true, receive_channel_->GetStats(
                        &receive_info, /*get_and_clear_legacy_stats=*/true));
    EXPECT_EQ(1u, send_info.senders.size());
    EXPECT_EQ(0u, receive_info.receivers.size());
  }

  // Deliver a new packet - a default receive stream should be created and we
  // should see stats again.
  {
    DeliverPacket(kPcmuFrame);
    SetAudioReceiveStreamStats();
    EXPECT_CALL(*adm_, GetPlayoutUnderrunCount()).WillOnce(Return(0));
    VoiceMediaSendInfo send_info;
    VoiceMediaReceiveInfo receive_info;
    EXPECT_EQ(true, send_channel_->GetStats(&send_info));
    EXPECT_EQ(true, receive_channel_->GetStats(
                        &receive_info, /*get_and_clear_legacy_stats=*/true));
    EXPECT_EQ(1u, send_info.senders.size());
    EXPECT_EQ(1u, receive_info.receivers.size());
    VerifyVoiceReceiverInfo(receive_info.receivers[0]);
    VerifyVoiceSendRecvCodecs(send_info, receive_info);
  }
}

// Test that we can set the outgoing SSRC properly with multiple streams.
// SSRC is set in SetupSendStream() by calling AddSendStream.
TEST_P(WebRtcVoiceEngineTestFake, SetSendSsrcWithMultipleStreams) {
  EXPECT_TRUE(SetupSendStream());
  EXPECT_TRUE(call_.GetAudioSendStream(kSsrcX));
  EXPECT_TRUE(AddRecvStream(kSsrcY));
}

// Test that the local SSRC is the same on sending and receiving channels if the
// receive channel is created before the send channel.
TEST_P(WebRtcVoiceEngineTestFake, SetSendSsrcAfterCreatingReceiveChannel) {
  EXPECT_TRUE(SetupChannel());
  EXPECT_TRUE(AddRecvStream(kSsrcY));
  EXPECT_TRUE(send_channel_->AddSendStream(StreamParams::CreateLegacy(kSsrcX)));
  EXPECT_TRUE(call_.GetAudioSendStream(kSsrcX));
}

// Test that we can properly receive packets.
TEST_P(WebRtcVoiceEngineTestFake, Recv) {
  EXPECT_TRUE(SetupChannel());
  EXPECT_TRUE(AddRecvStream(1));
  DeliverPacket(kPcmuFrame);

  EXPECT_TRUE(GetRecvStream(1).VerifyLastPacket(kPcmuFrame));
}

// Test that we can properly receive packets on multiple streams.
TEST_P(WebRtcVoiceEngineTestFake, RecvWithMultipleStreams) {
  EXPECT_TRUE(SetupChannel());
  const uint32_t ssrc1 = 1;
  const uint32_t ssrc2 = 2;
  const uint32_t ssrc3 = 3;
  EXPECT_TRUE(AddRecvStream(ssrc1));
  EXPECT_TRUE(AddRecvStream(ssrc2));
  EXPECT_TRUE(AddRecvStream(ssrc3));
  // Create packets with the right SSRCs.
  uint8_t packets[4][sizeof(kPcmuFrame)];
  for (size_t i = 0; i < std::size(packets); ++i) {
    memcpy(packets[i], kPcmuFrame, sizeof(kPcmuFrame));
    SetBE32(std::span<uint8_t>(packets[i] + 8, 4), static_cast<uint32_t>(i));
  }

  const FakeAudioReceiveStream& s1 = GetRecvStream(ssrc1);
  const FakeAudioReceiveStream& s2 = GetRecvStream(ssrc2);
  const FakeAudioReceiveStream& s3 = GetRecvStream(ssrc3);

  EXPECT_EQ(s1.received_packets(), 0);
  EXPECT_EQ(s2.received_packets(), 0);
  EXPECT_EQ(s3.received_packets(), 0);

  DeliverPacket(packets[0]);
  EXPECT_EQ(s1.received_packets(), 0);
  EXPECT_EQ(s2.received_packets(), 0);
  EXPECT_EQ(s3.received_packets(), 0);

  DeliverPacket(packets[1]);
  EXPECT_EQ(s1.received_packets(), 1);
  EXPECT_TRUE(s1.VerifyLastPacket(packets[1]));
  EXPECT_EQ(s2.received_packets(), 0);
  EXPECT_EQ(s3.received_packets(), 0);

  DeliverPacket(packets[2]);
  EXPECT_EQ(s1.received_packets(), 1);
  EXPECT_EQ(s2.received_packets(), 1);
  EXPECT_TRUE(s2.VerifyLastPacket(packets[2]));
  EXPECT_EQ(s3.received_packets(), 0);

  DeliverPacket(packets[3]);
  EXPECT_EQ(s1.received_packets(), 1);
  EXPECT_EQ(s2.received_packets(), 1);
  EXPECT_EQ(s3.received_packets(), 1);
  EXPECT_TRUE(s3.VerifyLastPacket(packets[3]));

  EXPECT_TRUE(receive_channel_->RemoveRecvStream(ssrc3));
  EXPECT_TRUE(receive_channel_->RemoveRecvStream(ssrc2));
  EXPECT_TRUE(receive_channel_->RemoveRecvStream(ssrc1));
}

// Test that receiving on an unsignaled stream works (a stream is created).
TEST_P(WebRtcVoiceEngineTestFake, RecvUnsignaled) {
  EXPECT_TRUE(SetupChannel());
  EXPECT_EQ(0u, call_.GetAudioReceiveStreams().size());

  DeliverPacket(kPcmuFrame);

  EXPECT_EQ(1u, call_.GetAudioReceiveStreams().size());
  EXPECT_TRUE(GetRecvStream(kSsrc1).VerifyLastPacket(kPcmuFrame));
}

// Tests that when we add a stream without SSRCs, but contains a stream_id
// that it is stored and its stream id is later used when the first packet
// arrives to properly create a receive stream with a sync label.
TEST_P(WebRtcVoiceEngineTestFake, RecvUnsignaledSsrcWithSignaledStreamId) {
  const char kSyncLabel[] = "sync_label";
  EXPECT_TRUE(SetupChannel());
  StreamParams unsignaled_stream;
  unsignaled_stream.set_stream_ids({kSyncLabel});
  ASSERT_TRUE(receive_channel_->AddRecvStream(unsignaled_stream));
  // The stream shouldn't have been created at this point because it doesn't
  // have any SSRCs.
  EXPECT_EQ(0u, call_.GetAudioReceiveStreams().size());
  DeliverPacket(kPcmuFrame);

  EXPECT_EQ(1u, call_.GetAudioReceiveStreams().size());
  EXPECT_TRUE(GetRecvStream(kSsrc1).VerifyLastPacket(kPcmuFrame));
  EXPECT_EQ(kSyncLabel, GetRecvStream(kSsrc1).GetConfig().sync_group);

  // Remset the unsignaled stream to clear the cached parameters. If a new
  // default unsignaled receive stream is created it will not have a sync group.
  receive_channel_->ResetUnsignaledRecvStream();
  receive_channel_->RemoveRecvStream(kSsrc1);

  DeliverPacket(kPcmuFrame);

  EXPECT_EQ(1u, call_.GetAudioReceiveStreams().size());
  EXPECT_TRUE(GetRecvStream(kSsrc1).VerifyLastPacket(kPcmuFrame));
  EXPECT_TRUE(GetRecvStream(kSsrc1).GetConfig().sync_group.empty());
}

TEST_P(WebRtcVoiceEngineTestFake,
       ResetUnsignaledRecvStreamDeletesAllDefaultStreams) {
  ASSERT_TRUE(SetupChannel());
  // No receive streams to start with.
  ASSERT_TRUE(call_.GetAudioReceiveStreams().empty());

  // Deliver a couple packets with unsignaled SSRCs.
  uint8_t packet[sizeof(kPcmuFrame)];
  memcpy(packet, kPcmuFrame, sizeof(kPcmuFrame));
  SetBE32(std::span<uint8_t>(&packet[8], 4), 0x1234);
  DeliverPacket(packet);
  SetBE32(std::span<uint8_t>(&packet[8], 4), 0x5678);
  DeliverPacket(packet);

  // Verify that the receive streams were created.
  const auto& receivers1 = call_.GetAudioReceiveStreams();
  ASSERT_EQ(receivers1.size(), 2u);

  // Should remove all default streams.
  receive_channel_->ResetUnsignaledRecvStream();
  const auto& receivers2 = call_.GetAudioReceiveStreams();
  EXPECT_EQ(0u, receivers2.size());
}

TEST_P(WebRtcVoiceEngineTestFake, GetUnsignaledSsrcs) {
  ASSERT_TRUE(SetupChannel());
  // No receive streams to start with.
  ASSERT_TRUE(call_.GetAudioReceiveStreams().empty());
  EXPECT_TRUE(receive_channel_->GetUnsignaledSsrcs().empty());

  // Deliver a couple packets with unsignaled SSRCs.
  uint8_t packet[sizeof(kPcmuFrame)];
  memcpy(packet, kPcmuFrame, sizeof(kPcmuFrame));
  SetBE32(std::span<uint8_t>(&packet[8], 4), 0x1234);
  DeliverPacket(packet);
  SetBE32(std::span<uint8_t>(&packet[8], 4), 0x5678);
  DeliverPacket(packet);

  EXPECT_THAT(receive_channel_->GetUnsignaledSsrcs(),
              testing::ElementsAre(0x1234, 0x5678));

  // Should remove all default streams.
  receive_channel_->ResetUnsignaledRecvStream();
  EXPECT_TRUE(receive_channel_->GetUnsignaledSsrcs().empty());
}

// Test that receiving N unsignaled stream works (streams will be created), and
// that packets are forwarded to them all.
TEST_P(WebRtcVoiceEngineTestFake, RecvMultipleUnsignaled) {
  EXPECT_TRUE(SetupChannel());
  uint8_t packet[sizeof(kPcmuFrame)];
  memcpy(packet, kPcmuFrame, sizeof(kPcmuFrame));

  // Note that SSRC = 0 is not supported.
  for (uint32_t ssrc = 1; ssrc < (1 + kMaxUnsignaledRecvStreams); ++ssrc) {
    SetBE32(std::span<uint8_t>(&packet[8], 4), ssrc);
    DeliverPacket(packet);

    // Verify we have one new stream for each loop iteration.
    EXPECT_EQ(ssrc, call_.GetAudioReceiveStreams().size());
    EXPECT_EQ(1, GetRecvStream(ssrc).received_packets());
    EXPECT_TRUE(GetRecvStream(ssrc).VerifyLastPacket(packet));
  }

  // Sending on the same SSRCs again should not create new streams.
  for (uint32_t ssrc = 1; ssrc < (1 + kMaxUnsignaledRecvStreams); ++ssrc) {
    SetBE32(std::span<uint8_t>(&packet[8], 4), ssrc);
    DeliverPacket(packet);

    EXPECT_EQ(kMaxUnsignaledRecvStreams, call_.GetAudioReceiveStreams().size());
    EXPECT_EQ(2, GetRecvStream(ssrc).received_packets());
    EXPECT_TRUE(GetRecvStream(ssrc).VerifyLastPacket(packet));
  }

  // Send on another SSRC, the oldest unsignaled stream (SSRC=1) is replaced.
  constexpr uint32_t kAnotherSsrc = 667;
  SetBE32(std::span<uint8_t>(&packet[8], 4), kAnotherSsrc);
  DeliverPacket(packet);

  const auto& streams = call_.GetAudioReceiveStreams();
  EXPECT_EQ(kMaxUnsignaledRecvStreams, streams.size());
  size_t i = 0;
  for (uint32_t ssrc = 2; ssrc < (1 + kMaxUnsignaledRecvStreams); ++ssrc, ++i) {
    EXPECT_EQ(ssrc, streams[i]->GetConfig().rtp.remote_ssrc);
    EXPECT_EQ(2, streams[i]->received_packets());
  }
  EXPECT_EQ(kAnotherSsrc, streams[i]->GetConfig().rtp.remote_ssrc);
  EXPECT_EQ(1, streams[i]->received_packets());
  // Sanity check that we've checked all streams.
  EXPECT_EQ(kMaxUnsignaledRecvStreams, (i + 1));
}

// Test that a default channel is created even after a signaled stream has been
// added, and that this stream will get any packets for unknown SSRCs.
TEST_P(WebRtcVoiceEngineTestFake, RecvUnsignaledAfterSignaled) {
  EXPECT_TRUE(SetupChannel());
  uint8_t packet[sizeof(kPcmuFrame)];
  memcpy(packet, kPcmuFrame, sizeof(kPcmuFrame));

  // Add a known stream, send packet and verify we got it.
  const uint32_t signaled_ssrc = 1;
  SetBE32(std::span<uint8_t>(&packet[8], 4), signaled_ssrc);
  EXPECT_TRUE(AddRecvStream(signaled_ssrc));
  DeliverPacket(packet);
  EXPECT_TRUE(GetRecvStream(signaled_ssrc).VerifyLastPacket(packet));
  EXPECT_EQ(1u, call_.GetAudioReceiveStreams().size());

  // Note that the first unknown SSRC cannot be 0, because we only support
  // creating receive streams for SSRC!=0.
  const uint32_t unsignaled_ssrc = 7011;
  SetBE32(std::span<uint8_t>(&packet[8], 4), unsignaled_ssrc);
  DeliverPacket(packet);
  EXPECT_TRUE(GetRecvStream(unsignaled_ssrc).VerifyLastPacket(packet));
  EXPECT_EQ(2u, call_.GetAudioReceiveStreams().size());

  DeliverPacket(packet);
  EXPECT_EQ(2, GetRecvStream(unsignaled_ssrc).received_packets());

  SetBE32(std::span<uint8_t>(&packet[8], 4), signaled_ssrc);
  DeliverPacket(packet);
  EXPECT_EQ(2, GetRecvStream(signaled_ssrc).received_packets());
  EXPECT_EQ(2u, call_.GetAudioReceiveStreams().size());
}

// Two tests to verify that adding a receive stream with the same SSRC as a
// previously added unsignaled stream will only recreate underlying stream
// objects if the stream parameters have changed.
TEST_P(WebRtcVoiceEngineTestFake, AddRecvStreamAfterUnsignaled_NoRecreate) {
  EXPECT_TRUE(SetupChannel());

  // Spawn unsignaled stream with SSRC=1.
  DeliverPacket(kPcmuFrame);
  EXPECT_EQ(1u, call_.GetAudioReceiveStreams().size());
  EXPECT_TRUE(GetRecvStream(1).VerifyLastPacket(kPcmuFrame));

  // Verify that the underlying stream object in Call is not recreated when a
  // stream with SSRC=1 is added.
  const auto& streams = call_.GetAudioReceiveStreams();
  EXPECT_EQ(1u, streams.size());
  int audio_receive_stream_id = streams.front()->id();
  EXPECT_TRUE(AddRecvStream(1));
  EXPECT_EQ(1u, streams.size());
  EXPECT_EQ(audio_receive_stream_id, streams.front()->id());
}

TEST_P(WebRtcVoiceEngineTestFake, AddRecvStreamAfterUnsignaled_Updates) {
  EXPECT_TRUE(SetupChannel());

  // Spawn unsignaled stream with SSRC=1.
  DeliverPacket(kPcmuFrame);
  EXPECT_EQ(1u, call_.GetAudioReceiveStreams().size());
  EXPECT_TRUE(GetRecvStream(1).VerifyLastPacket(kPcmuFrame));

  // Verify that the underlying stream object in Call gets updated when a
  // stream with SSRC=1 is added, and which has changed stream parameters.
  const auto& streams = call_.GetAudioReceiveStreams();
  EXPECT_EQ(1u, streams.size());
  // The sync_group id should be empty.
  EXPECT_TRUE(streams.front()->GetConfig().sync_group.empty());

  const std::string new_stream_id("stream_id");
  int audio_receive_stream_id = streams.front()->id();
  StreamParams stream_params;
  stream_params.ssrcs.push_back(1);
  stream_params.set_stream_ids({new_stream_id});

  EXPECT_TRUE(receive_channel_->AddRecvStream(stream_params));
  EXPECT_EQ(1u, streams.size());
  // The audio receive stream should not have been recreated.
  EXPECT_EQ(audio_receive_stream_id, streams.front()->id());

  // The sync_group id should now match with the new stream params.
  EXPECT_EQ(new_stream_id, streams.front()->GetConfig().sync_group);
}

// Test that AddRecvStream creates new stream.
TEST_P(WebRtcVoiceEngineTestFake, AddRecvStream) {
  EXPECT_TRUE(SetupRecvStream());
  EXPECT_TRUE(AddRecvStream(1));
}

// Test that after adding a recv stream, we do not decode more codecs than
// those previously passed into SetRecvCodecs.
TEST_P(WebRtcVoiceEngineTestFake, AddRecvStreamUnsupportedCodec) {
  EXPECT_TRUE(SetupSendStream());
  AudioReceiverParameters parameters;
  parameters.codecs.push_back(kOpusCodec);
  parameters.codecs.push_back(kPcmuCodec);
  EXPECT_TRUE(receive_channel_->SetReceiverParameters(parameters));
  EXPECT_TRUE(AddRecvStream(kSsrcX));
  EXPECT_THAT(GetRecvStreamConfig(kSsrcX).decoder_map,
              (ContainerEq<std::map<int, SdpAudioFormat>>(
                  {{0, {"PCMU", 8000, 1}}, {111, {"OPUS", 48000, 2}}})));
}

// Test that we properly clean up any streams that were added, even if
// not explicitly removed.
TEST_P(WebRtcVoiceEngineTestFake, StreamCleanup) {
  EXPECT_TRUE(SetupSendStream());
  SetSenderParameters(send_parameters_);
  EXPECT_TRUE(AddRecvStream(1));
  EXPECT_TRUE(AddRecvStream(2));

  EXPECT_EQ(1u, call_.GetAudioSendStreams().size());
  EXPECT_EQ(2u, call_.GetAudioReceiveStreams().size());
  send_channel_.reset();
  receive_channel_.reset();
  EXPECT_EQ(0u, call_.GetAudioSendStreams().size());
  EXPECT_EQ(0u, call_.GetAudioReceiveStreams().size());
}

TEST_P(WebRtcVoiceEngineTestFake, TestAddRecvStreamSuccessWithZeroSsrc) {
  EXPECT_TRUE(SetupSendStream());
  EXPECT_TRUE(AddRecvStream(0));
}

TEST_P(WebRtcVoiceEngineTestFake, TestAddRecvStreamFailWithSameSsrc) {
  EXPECT_TRUE(SetupChannel());
  EXPECT_TRUE(AddRecvStream(1));
  EXPECT_FALSE(AddRecvStream(1));
}

// Test the InsertDtmf on default send stream as caller.
TEST_P(WebRtcVoiceEngineTestFake, InsertDtmfOnDefaultSendStreamAsCaller) {
  TestInsertDtmf(0, true, kTelephoneEventCodec1);
}

// Test the InsertDtmf on default send stream as callee
TEST_P(WebRtcVoiceEngineTestFake, InsertDtmfOnDefaultSendStreamAsCallee) {
  TestInsertDtmf(0, false, kTelephoneEventCodec2);
}

// Test the InsertDtmf on specified send stream as caller.
TEST_P(WebRtcVoiceEngineTestFake, InsertDtmfOnSendStreamAsCaller) {
  TestInsertDtmf(kSsrcX, true, kTelephoneEventCodec2);
}

// Test the InsertDtmf on specified send stream as callee.
TEST_P(WebRtcVoiceEngineTestFake, InsertDtmfOnSendStreamAsCallee) {
  TestInsertDtmf(kSsrcX, false, kTelephoneEventCodec1);
}

// Test propagation of extmap allow mixed setting.
TEST_P(WebRtcVoiceEngineTestFake, SetExtmapAllowMixedAsCaller) {
  TestExtmapAllowMixedCaller(/*extmap_allow_mixed=*/true);
}
TEST_P(WebRtcVoiceEngineTestFake, SetExtmapAllowMixedDisabledAsCaller) {
  TestExtmapAllowMixedCaller(/*extmap_allow_mixed=*/false);
}
TEST_P(WebRtcVoiceEngineTestFake, SetExtmapAllowMixedAsCallee) {
  TestExtmapAllowMixedCallee(/*extmap_allow_mixed=*/true);
}
TEST_P(WebRtcVoiceEngineTestFake, SetExtmapAllowMixedDisabledAsCallee) {
  TestExtmapAllowMixedCallee(/*extmap_allow_mixed=*/false);
}

TEST_P(WebRtcVoiceEngineTestFake, InitRecordingOnSend) {
  EXPECT_CALL(*adm_, RecordingIsInitialized()).WillOnce(Return(false));
  EXPECT_CALL(*adm_, Recording()).WillOnce(Return(false));
  EXPECT_CALL(*adm_, InitRecording()).Times(1);

  std::unique_ptr<VoiceMediaSendChannelInterface> send_channel(
      engine_->CreateSendChannel(env_, &call_, MediaConfig(), AudioOptions(),
                                 CryptoOptions()));

  send_channel->SetSend(true);
}

TEST_P(WebRtcVoiceEngineTestFake, SkipInitRecordingOnSend) {
  EXPECT_CALL(*adm_, RecordingIsInitialized()).Times(0);
  EXPECT_CALL(*adm_, Recording()).Times(0);
  EXPECT_CALL(*adm_, InitRecording()).Times(0);

  AudioOptions options;
  options.init_recording_on_send = false;

  std::unique_ptr<VoiceMediaSendChannelInterface> send_channel(
      engine_->CreateSendChannel(env_, &call_, MediaConfig(), options,
                                 CryptoOptions()));

  send_channel->SetSend(true);
}

// This test verifies DSCP settings are properly applied on voice media channel.
TEST_P(WebRtcVoiceEngineTestFake, TestSetDscpOptions) {
  EXPECT_TRUE(SetupSendStream());
  FakeNetworkInterface network_interface(env_);
  MediaConfig config;
  std::unique_ptr<VoiceMediaSendChannelInterface> channel;
  RtpParameters parameters;

  channel = engine_->CreateSendChannel(env_, &call_, config, AudioOptions(),
                                       CryptoOptions());
  channel->SetInterface(&network_interface);
  // Default value when DSCP is disabled should be DSCP_DEFAULT.
  EXPECT_EQ(DSCP_DEFAULT, network_interface.dscp());
  channel->SetInterface(nullptr);

  config.enable_dscp = true;
  channel = engine_->CreateSendChannel(env_, &call_, config, AudioOptions(),
                                       CryptoOptions());
  channel->SetInterface(&network_interface);
  EXPECT_EQ(DSCP_DEFAULT, network_interface.dscp());

  // Create a send stream to configure
  EXPECT_TRUE(channel->AddSendStream(StreamParams::CreateLegacy(kSsrcZ)));
  parameters = channel->GetRtpSendParameters(kSsrcZ);
  ASSERT_FALSE(parameters.encodings.empty());

  // Various priorities map to various dscp values.
  parameters.encodings[0].network_priority = Priority::kHigh;
  ASSERT_TRUE(channel->SetRtpSendParameters(kSsrcZ, parameters, nullptr).ok());
  EXPECT_EQ(DSCP_EF, network_interface.dscp());
  parameters.encodings[0].network_priority = Priority::kVeryLow;
  ASSERT_TRUE(channel->SetRtpSendParameters(kSsrcZ, parameters, nullptr).ok());
  EXPECT_EQ(DSCP_CS1, network_interface.dscp());

  // Packets should also self-identify their dscp in PacketOptions.
  const uint8_t kData[10] = {0};
  EXPECT_TRUE(SendImplFromPointer(channel.get())
                  ->transport()
                  ->SendRtcp(kData, /*packet_options=*/{}));
  EXPECT_EQ(DSCP_CS1, network_interface.options().dscp);
  channel->SetInterface(nullptr);

  // Verify that setting the option to false resets the
  // DiffServCodePoint.
  config.enable_dscp = false;
  channel = engine_->CreateSendChannel(env_, &call_, config, AudioOptions(),
                                       CryptoOptions());
  channel->SetInterface(&network_interface);
  // Default value when DSCP is disabled should be DSCP_DEFAULT.
  EXPECT_EQ(DSCP_DEFAULT, network_interface.dscp());

  channel->SetInterface(nullptr);
}

TEST_P(WebRtcVoiceEngineTestFake, SetOutputVolume) {
  EXPECT_TRUE(SetupChannel());
  EXPECT_FALSE(receive_channel_->SetOutputVolume(kSsrcY, 0.5));
  StreamParams stream;
  stream.ssrcs.push_back(kSsrcY);
  EXPECT_TRUE(receive_channel_->AddRecvStream(stream));
  EXPECT_DOUBLE_EQ(1, GetRecvStream(kSsrcY).gain());
  EXPECT_TRUE(receive_channel_->SetOutputVolume(kSsrcY, 3));
  EXPECT_DOUBLE_EQ(3, GetRecvStream(kSsrcY).gain());
}

TEST_P(WebRtcVoiceEngineTestFake, SetOutputVolumeUnsignaledRecvStream) {
  EXPECT_TRUE(SetupChannel());

  // Spawn an unsignaled stream by sending a packet - gain should be 1.
  DeliverPacket(kPcmuFrame);
  EXPECT_DOUBLE_EQ(1, GetRecvStream(kSsrc1).gain());

  // Should remember the volume "2" which will be set on new unsignaled streams,
  // and also set the gain to 2 on existing unsignaled streams.
  EXPECT_TRUE(receive_channel_->SetDefaultOutputVolume(2));
  EXPECT_DOUBLE_EQ(2, GetRecvStream(kSsrc1).gain());

  // Spawn an unsignaled stream by sending a packet - gain should be 2.
  uint8_t pcmuFrame2[sizeof(kPcmuFrame)];
  memcpy(pcmuFrame2, kPcmuFrame, sizeof(kPcmuFrame));
  SetBE32(std::span<uint8_t>(&pcmuFrame2[8], 4), kSsrcX);
  DeliverPacket(pcmuFrame2);
  EXPECT_DOUBLE_EQ(2, GetRecvStream(kSsrcX).gain());

  // Setting gain for all unsignaled streams.
  EXPECT_TRUE(receive_channel_->SetDefaultOutputVolume(3));
  if (kMaxUnsignaledRecvStreams > 1) {
    EXPECT_DOUBLE_EQ(3, GetRecvStream(kSsrc1).gain());
  }
  EXPECT_DOUBLE_EQ(3, GetRecvStream(kSsrcX).gain());

  // Setting gain on an individual stream affects only that.
  EXPECT_TRUE(receive_channel_->SetOutputVolume(kSsrcX, 4));
  if (kMaxUnsignaledRecvStreams > 1) {
    EXPECT_DOUBLE_EQ(3, GetRecvStream(kSsrc1).gain());
  }
  EXPECT_DOUBLE_EQ(4, GetRecvStream(kSsrcX).gain());
}

TEST_P(WebRtcVoiceEngineTestFake, BaseMinimumPlayoutDelayMs) {
  EXPECT_TRUE(SetupChannel());
  EXPECT_FALSE(receive_channel_->SetBaseMinimumPlayoutDelayMs(kSsrcY, 200));
  EXPECT_FALSE(
      receive_channel_->GetBaseMinimumPlayoutDelayMs(kSsrcY).has_value());

  StreamParams stream;
  stream.ssrcs.push_back(kSsrcY);
  EXPECT_TRUE(receive_channel_->AddRecvStream(stream));
  EXPECT_EQ(0, GetRecvStream(kSsrcY).base_mininum_playout_delay_ms());
  EXPECT_TRUE(receive_channel_->SetBaseMinimumPlayoutDelayMs(kSsrcY, 300));
  EXPECT_EQ(300, GetRecvStream(kSsrcY).base_mininum_playout_delay_ms());
}

TEST_P(WebRtcVoiceEngineTestFake,
       BaseMinimumPlayoutDelayMsUnsignaledRecvStream) {
  // Here base minimum delay is abbreviated to delay in comments for shortness.
  EXPECT_TRUE(SetupChannel());

  // Spawn an unsignaled stream by sending a packet - delay should be 0.
  DeliverPacket(kPcmuFrame);
  EXPECT_EQ(
      0, receive_channel_->GetBaseMinimumPlayoutDelayMs(kSsrc1).value_or(-1));
  // Check that it doesn't provide default values for unknown ssrc.
  EXPECT_FALSE(
      receive_channel_->GetBaseMinimumPlayoutDelayMs(kSsrcY).has_value());

  // Check that default value for unsignaled streams is 0.
  EXPECT_EQ(
      0, receive_channel_->GetBaseMinimumPlayoutDelayMs(kSsrc0).value_or(-1));

  // Should remember the delay 100 which will be set on new unsignaled streams,
  // and also set the delay to 100 on existing unsignaled streams.
  EXPECT_TRUE(receive_channel_->SetBaseMinimumPlayoutDelayMs(kSsrc0, 100));
  EXPECT_EQ(
      100, receive_channel_->GetBaseMinimumPlayoutDelayMs(kSsrc0).value_or(-1));
  // Check that it doesn't provide default values for unknown ssrc.
  EXPECT_FALSE(
      receive_channel_->GetBaseMinimumPlayoutDelayMs(kSsrcY).has_value());

  // Spawn an unsignaled stream by sending a packet - delay should be 100.
  uint8_t pcmuFrame2[sizeof(kPcmuFrame)];
  memcpy(pcmuFrame2, kPcmuFrame, sizeof(kPcmuFrame));
  SetBE32(std::span<uint8_t>(&pcmuFrame2[8], 4), kSsrcX);
  DeliverPacket(pcmuFrame2);
  EXPECT_EQ(
      100, receive_channel_->GetBaseMinimumPlayoutDelayMs(kSsrcX).value_or(-1));

  // Setting delay with SSRC=0 should affect all unsignaled streams.
  EXPECT_TRUE(receive_channel_->SetBaseMinimumPlayoutDelayMs(kSsrc0, 300));
  if (kMaxUnsignaledRecvStreams > 1) {
    EXPECT_EQ(
        300,
        receive_channel_->GetBaseMinimumPlayoutDelayMs(kSsrc1).value_or(-1));
  }
  EXPECT_EQ(
      300, receive_channel_->GetBaseMinimumPlayoutDelayMs(kSsrcX).value_or(-1));

  // Setting delay on an individual stream affects only that.
  EXPECT_TRUE(receive_channel_->SetBaseMinimumPlayoutDelayMs(kSsrcX, 400));
  if (kMaxUnsignaledRecvStreams > 1) {
    EXPECT_EQ(
        300,
        receive_channel_->GetBaseMinimumPlayoutDelayMs(kSsrc1).value_or(-1));
  }
  EXPECT_EQ(
      400, receive_channel_->GetBaseMinimumPlayoutDelayMs(kSsrcX).value_or(-1));
  EXPECT_EQ(
      300, receive_channel_->GetBaseMinimumPlayoutDelayMs(kSsrc0).value_or(-1));
  // Check that it doesn't provide default values for unknown ssrc.
  EXPECT_FALSE(
      receive_channel_->GetBaseMinimumPlayoutDelayMs(kSsrcY).has_value());
}

TEST_P(WebRtcVoiceEngineTestFake, SetsSyncGroupFromStreamId) {
  const uint32_t kAudioSsrc = 123;
  const std::string kStreamId = "AvSyncLabel";

  EXPECT_TRUE(SetupSendStream());
  StreamParams sp = StreamParams::CreateLegacy(kAudioSsrc);
  sp.set_stream_ids({kStreamId});
  // Creating two channels to make sure that sync label is set properly for both
  // the default voice channel and following ones.
  EXPECT_TRUE(receive_channel_->AddRecvStream(sp));
  sp.ssrcs[0] += 1;
  EXPECT_TRUE(receive_channel_->AddRecvStream(sp));

  ASSERT_EQ(2u, call_.GetAudioReceiveStreams().size());
  EXPECT_EQ(kStreamId,
            call_.GetAudioReceiveStream(kAudioSsrc)->GetConfig().sync_group)
      << "SyncGroup should be set based on stream id";
  EXPECT_EQ(kStreamId,
            call_.GetAudioReceiveStream(kAudioSsrc + 1)->GetConfig().sync_group)
      << "SyncGroup should be set based on stream id";
}

// Enabling Sframe before a send stream is added is propagated to the stream
// config at construction.
TEST_P(WebRtcVoiceEngineTestFake, EnableSframePropagatesToSendStreamConfig) {
  ASSERT_TRUE(SetupChannel());
  send_channel_->EnableSframe();

  ASSERT_TRUE(send_channel_->AddSendStream(StreamParams::CreateLegacy(kSsrcX)));

  EXPECT_TRUE(GetSendStreamConfig(kSsrcX).sframe_options.required);
}

// Enabling Sframe on an existing send stream reconfigures it in place.
TEST_P(WebRtcVoiceEngineTestFake, EnableSframeReconfiguresExistingSendStream) {
  ASSERT_TRUE(SetupSendStream());
  ASSERT_FALSE(GetSendStreamConfig(kSsrcX).sframe_options.required);

  send_channel_->EnableSframe();

  EXPECT_TRUE(GetSendStreamConfig(kSsrcX).sframe_options.required);
}

// TODO(solenberg): Remove, once recv streams are configured through Call.
//                  (This is then covered by TestSetRecvRtpHeaderExtensions.)
TEST_P(WebRtcVoiceEngineTestFake, ConfiguresAudioReceiveStreamRtpExtensions) {
  // Test that setting the header extensions results in the expected state
  // changes on an associated Call.
  std::vector<uint32_t> ssrcs;
  ssrcs.push_back(223);
  ssrcs.push_back(224);

  EXPECT_TRUE(SetupSendStream());
  SetSenderParameters(send_parameters_);
  for (uint32_t ssrc : ssrcs) {
    EXPECT_TRUE(
        receive_channel_->AddRecvStream(StreamParams::CreateLegacy(ssrc)));
  }

  EXPECT_EQ(2u, call_.GetAudioReceiveStreams().size());
  for (uint32_t ssrc : ssrcs) {
    EXPECT_THAT(
        receive_channel_->GetRtpReceiverParameters(ssrc).header_extensions,
        IsEmpty());
  }

  // Set up receive extensions.
  const auto header_extensions_caps =
      GetDefaultEnabledRtpHeaderCapabilities(*engine_,
                                             /* field_trials= */ nullptr);
  std::vector<RtpExtension> header_extensions;
  for (const auto& cap : header_extensions_caps) {
    header_extensions.emplace_back(
        cap.uri, cap.preferred_id.value_or(RtpHeaderExtensionId::NotSet()),
        cap.preferred_encrypt);
  }
  AudioReceiverParameters recv_parameters;
  recv_parameters.extensions = header_extensions;
  receive_channel_->SetReceiverParameters(recv_parameters);
  EXPECT_EQ(2u, call_.GetAudioReceiveStreams().size());
  for (uint32_t ssrc : ssrcs) {
    EXPECT_THAT(
        receive_channel_->GetRtpReceiverParameters(ssrc).header_extensions,
        testing::UnorderedElementsAreArray(header_extensions));
  }

  // Disable receive extensions.
  receive_channel_->SetReceiverParameters(AudioReceiverParameters());
  for (uint32_t ssrc : ssrcs) {
    EXPECT_THAT(
        receive_channel_->GetRtpReceiverParameters(ssrc).header_extensions,
        IsEmpty());
  }
}

TEST_P(WebRtcVoiceEngineTestFake, DeliverAudioPacket_Call) {
  // Test that packets are forwarded to the Call when configured accordingly.
  const uint32_t kAudioSsrc = 1;
  CopyOnWriteBuffer kPcmuPacket(kPcmuFrame);
  static const uint8_t kRtcp[] = {
      0x80, 0xc9, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
      0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  CopyOnWriteBuffer kRtcpPacket(kRtcp);

  EXPECT_TRUE(SetupSendStream());
  VoiceMediaReceiveChannelInterface* media_channel = ReceiveImpl();
  SetSenderParameters(send_parameters_);
  EXPECT_TRUE(
      media_channel->AddRecvStream(StreamParams::CreateLegacy(kAudioSsrc)));

  EXPECT_EQ(1u, call_.GetAudioReceiveStreams().size());
  const FakeAudioReceiveStream* s = call_.GetAudioReceiveStream(kAudioSsrc);
  EXPECT_EQ(0, s->received_packets());
  RtpPacketReceived parsed_packet;
  RTC_CHECK(parsed_packet.Parse(kPcmuPacket));
  receive_channel_->OnPacketReceived(parsed_packet);
  run_loop_.Flush();

  EXPECT_EQ(1, s->received_packets());
}

TEST_P(WebRtcVoiceEngineTestFake, SetRawAudioSink) {
  EXPECT_TRUE(SetupChannel());
  std::unique_ptr<FakeAudioSink> fake_sink_1(new FakeAudioSink());
  std::unique_ptr<FakeAudioSink> fake_sink_2(new FakeAudioSink());

  // Setting the sink before a recv stream exists should do nothing.
  receive_channel_->SetRawAudioSink(kSsrcX, std::move(fake_sink_1));
  EXPECT_TRUE(AddRecvStream(kSsrcX));
  EXPECT_EQ(nullptr, GetRecvStream(kSsrcX).sink());

  // Now try actually setting the sink.
  receive_channel_->SetRawAudioSink(kSsrcX, std::move(fake_sink_2));
  EXPECT_NE(nullptr, GetRecvStream(kSsrcX).sink());

  // Now try resetting it.
  receive_channel_->SetRawAudioSink(kSsrcX, nullptr);
  EXPECT_EQ(nullptr, GetRecvStream(kSsrcX).sink());
}

TEST_P(WebRtcVoiceEngineTestFake, SetRawAudioSinkUnsignaledRecvStream) {
  EXPECT_TRUE(SetupChannel());
  std::unique_ptr<FakeAudioSink> fake_sink_1(new FakeAudioSink());
  std::unique_ptr<FakeAudioSink> fake_sink_2(new FakeAudioSink());
  std::unique_ptr<FakeAudioSink> fake_sink_3(new FakeAudioSink());
  std::unique_ptr<FakeAudioSink> fake_sink_4(new FakeAudioSink());

  // Should be able to set a default sink even when no stream exists.
  receive_channel_->SetDefaultRawAudioSink(std::move(fake_sink_1));

  // Spawn an unsignaled stream by sending a packet - it should be assigned the
  // default sink.
  DeliverPacket(kPcmuFrame);
  EXPECT_NE(nullptr, GetRecvStream(kSsrc1).sink());

  // Try resetting the default sink.
  receive_channel_->SetDefaultRawAudioSink(nullptr);
  EXPECT_EQ(nullptr, GetRecvStream(kSsrc1).sink());

  // Try setting the default sink while the default stream exists.
  receive_channel_->SetDefaultRawAudioSink(std::move(fake_sink_2));
  EXPECT_NE(nullptr, GetRecvStream(kSsrc1).sink());

  // If we remove and add a default stream, it should get the same sink.
  EXPECT_TRUE(receive_channel_->RemoveRecvStream(kSsrc1));
  DeliverPacket(kPcmuFrame);
  EXPECT_NE(nullptr, GetRecvStream(kSsrc1).sink());

  // Spawn another unsignaled stream - it should be assigned the default sink
  // and the previous unsignaled stream should lose it.
  uint8_t pcmuFrame2[sizeof(kPcmuFrame)];
  memcpy(pcmuFrame2, kPcmuFrame, sizeof(kPcmuFrame));
  SetBE32(std::span<uint8_t>(&pcmuFrame2[8], 4), kSsrcX);
  DeliverPacket(pcmuFrame2);
  if (kMaxUnsignaledRecvStreams > 1) {
    EXPECT_EQ(nullptr, GetRecvStream(kSsrc1).sink());
  }
  EXPECT_NE(nullptr, GetRecvStream(kSsrcX).sink());

  // Reset the default sink - the second unsignaled stream should lose it.
  receive_channel_->SetDefaultRawAudioSink(nullptr);
  if (kMaxUnsignaledRecvStreams > 1) {
    EXPECT_EQ(nullptr, GetRecvStream(kSsrc1).sink());
  }
  EXPECT_EQ(nullptr, GetRecvStream(kSsrcX).sink());

  // Try setting the default sink while two streams exists.
  receive_channel_->SetDefaultRawAudioSink(std::move(fake_sink_3));
  if (kMaxUnsignaledRecvStreams > 1) {
    EXPECT_EQ(nullptr, GetRecvStream(kSsrc1).sink());
  }
  EXPECT_NE(nullptr, GetRecvStream(kSsrcX).sink());

  // Try setting the sink for the first unsignaled stream using its known SSRC.
  receive_channel_->SetRawAudioSink(kSsrc1, std::move(fake_sink_4));
  if (kMaxUnsignaledRecvStreams > 1) {
    EXPECT_NE(nullptr, GetRecvStream(kSsrc1).sink());
  }
  EXPECT_NE(nullptr, GetRecvStream(kSsrcX).sink());
  if (kMaxUnsignaledRecvStreams > 1) {
    EXPECT_NE(GetRecvStream(kSsrc1).sink(), GetRecvStream(kSsrcX).sink());
  }
}

// Test that, just like the video channel, the voice channel communicates the
// network state to the call.
TEST_P(WebRtcVoiceEngineTestFake, OnReadyToSendSignalsNetworkState) {
  EXPECT_TRUE(SetupChannel());

  EXPECT_EQ(kNetworkUp, call_.GetNetworkState(MediaType::AUDIO));
  EXPECT_EQ(kNetworkUp, call_.GetNetworkState(MediaType::VIDEO));

  send_channel_->OnReadyToSend(false);
  EXPECT_EQ(kNetworkDown, call_.GetNetworkState(MediaType::AUDIO));
  EXPECT_EQ(kNetworkUp, call_.GetNetworkState(MediaType::VIDEO));

  send_channel_->OnReadyToSend(true);
  EXPECT_EQ(kNetworkUp, call_.GetNetworkState(MediaType::AUDIO));
  EXPECT_EQ(kNetworkUp, call_.GetNetworkState(MediaType::VIDEO));
}

// Test that when an unsignaled stream is promoted to a signaled stream,
// its ProxySink doesn't hold a dangling raw pointer if the default sink
// is subsequently destroyed.
TEST_P(WebRtcVoiceEngineTestFake,
       ProxySinkSurvivesUnsignaledToSignaledPromotion) {
  EXPECT_TRUE(SetupChannel());
  std::unique_ptr<FakeAudioSink> fake_sink(new FakeAudioSink());

  // Set the default sink.
  receive_channel_->SetDefaultRawAudioSink(std::move(fake_sink));

  // Deliver an RTP packet to create an unsignaled stream.
  DeliverPacket(kPcmuFrame);
  const AudioSinkInterface* proxy_sink = GetRecvStream(kSsrc1).sink();
  EXPECT_NE(nullptr, proxy_sink);

  // Promote the unsignaled stream to a signaled stream.
  StreamParams sp = StreamParams::CreateLegacy(kSsrc1);
  EXPECT_TRUE(receive_channel_->AddRecvStream(sp));

  // The proxy sink should be removed from the stream upon promotion.
  EXPECT_EQ(nullptr, GetRecvStream(kSsrc1).sink());

  // Destroy the original sink by passing nullptr.
  receive_channel_->SetDefaultRawAudioSink(nullptr);

  // Note: calling proxy_sink->OnData would crash here if the proxy_sink
  // would still be attached to the stream and hold a dangling pointer to
  // default_sink_. But we've verified that it's detached, so that won't happen.
}

// Test that playout is still started after changing parameters
TEST_P(WebRtcVoiceEngineTestFake, PreservePlayoutWhenRecreateRecvStream) {
  SetupRecvStream();
  receive_channel_->SetReceive(true);
  EXPECT_TRUE(GetRecvStream(kSsrcX).started());

  // Changing RTP header extensions will recreate the
  // AudioReceiveStreamInterface.
  AudioReceiverParameters parameters;
  parameters.extensions.push_back(
      RtpExtension(RtpExtension::kAudioLevelUri, RtpHeaderExtensionId(12)));
  receive_channel_->SetReceiverParameters(parameters);

  EXPECT_TRUE(GetRecvStream(kSsrcX).started());
}

// Tests that the library initializes and shuts down properly.
TEST(WebRtcVoiceEngineTest, StartupShutdown) {
  test::RunLoop run_loop;
  for (bool use_null_apm : {false, true}) {
    // If the VoiceEngine wants to gather available codecs early, that's fine
    // but we never want it to create a decoder at this stage.
    Environment env = CreateTestEnvironment();
    scoped_refptr<test::MockAudioDeviceModule> adm =
        test::MockAudioDeviceModule::CreateNice();
    scoped_refptr<AudioProcessing> apm =
        use_null_apm ? nullptr : BuiltinAudioProcessingBuilder().Build(env);
    WebRtcVoiceEngine engine(
        env, adm, MockAudioEncoderFactory::CreateUnusedFactory(),
        MockAudioDecoderFactory::CreateUnusedFactory(), nullptr, apm, nullptr);
    AutoInitTerminate init_term(engine);
    std::unique_ptr<Call> call =
        Call::Create(CallConfig::CreateSingleThreaded(env));
    std::unique_ptr<VoiceMediaSendChannelInterface> send_channel =
        engine.CreateSendChannel(env, call.get(), MediaConfig(), AudioOptions(),
                                 CryptoOptions());
    EXPECT_TRUE(send_channel);
    std::unique_ptr<VoiceMediaReceiveChannelInterface> receive_channel =
        engine.CreateReceiveChannel(
            env, call.get(), MediaConfig(), AudioOptions(), CryptoOptions(),
            nullptr, [](uint32_t, const RtpPacketInfos&, Timestamp) {});
    EXPECT_TRUE(receive_channel);
  }
}

// Tests that reference counting on the external ADM is correct.
TEST(WebRtcVoiceEngineTest, StartupShutdownWithExternalADM) {
  test::RunLoop run_loop;
  for (bool use_null_apm : {false, true}) {
    Environment env = CreateTestEnvironment();
    auto adm =
        make_ref_counted<::testing::NiceMock<test::MockAudioDeviceModule>>();
    {
      scoped_refptr<AudioProcessing> apm =
          use_null_apm ? nullptr : BuiltinAudioProcessingBuilder().Build(env);
      WebRtcVoiceEngine engine(env, adm,
                               MockAudioEncoderFactory::CreateUnusedFactory(),
                               MockAudioDecoderFactory::CreateUnusedFactory(),
                               nullptr, apm, nullptr);
      AutoInitTerminate init_term(engine);
      std::unique_ptr<Call> call =
          Call::Create(CallConfig::CreateSingleThreaded(env));
      std::unique_ptr<VoiceMediaSendChannelInterface> send_channel =
          engine.CreateSendChannel(env, call.get(), MediaConfig(),
                                   AudioOptions(), CryptoOptions());
      EXPECT_TRUE(send_channel);
      std::unique_ptr<VoiceMediaReceiveChannelInterface> receive_channel =
          engine.CreateReceiveChannel(
              env, call.get(), MediaConfig(), AudioOptions(), CryptoOptions(),
              nullptr, [](uint32_t, const RtpPacketInfos&, Timestamp) {});
      EXPECT_TRUE(receive_channel);
    }
    // The engine/channel should have dropped their references.
    EXPECT_EQ(adm.release()->Release(), RefCountReleaseStatus::kDroppedLastRef);
  }
}

// Verify the payload id of common audio codecs, including CN and G722.
TEST(WebRtcVoiceEngineTest, HasCorrectPayloadTypeMapping) {
  Environment env = CreateTestEnvironment();
  for (bool use_null_apm : {false, true}) {
    // TODO(ossu): Why are the payload types of codecs with non-static payload
    // type assignments checked here? It shouldn't really matter.
    scoped_refptr<test::MockAudioDeviceModule> adm =
        test::MockAudioDeviceModule::CreateNice();
    scoped_refptr<AudioProcessing> apm =
        use_null_apm ? nullptr : BuiltinAudioProcessingBuilder().Build(env);
    WebRtcVoiceEngine engine(
        env, adm, MockAudioEncoderFactory::CreateUnusedFactory(),
        MockAudioDecoderFactory::CreateUnusedFactory(), nullptr, apm, nullptr);
    AutoInitTerminate init_term(engine);
    for (const Codec& codec : engine.LegacySendCodecs()) {
      auto is_codec = [&codec](const char* name, int clockrate = 0) {
        return absl::EqualsIgnoreCase(codec.name, name) &&
               (clockrate == 0 || codec.clockrate == clockrate);
      };
      if (is_codec("CN", 16000)) {
        EXPECT_EQ(105, codec.id);
      } else if (is_codec("CN", 32000)) {
        EXPECT_EQ(106, codec.id);
      } else if (is_codec("G722", 8000)) {
        EXPECT_EQ(9, codec.id);
      } else if (is_codec("telephone-event", 8000)) {
        EXPECT_EQ(126, codec.id);
        // TODO(solenberg): 16k, 32k, 48k DTMF should be dynamically assigned.
        // Remove these checks once both send and receive side assigns payload
        // types dynamically.
      } else if (is_codec("telephone-event", 16000)) {
        EXPECT_EQ(113, codec.id);
      } else if (is_codec("telephone-event", 32000)) {
        EXPECT_EQ(112, codec.id);
      } else if (is_codec("telephone-event", 48000)) {
        EXPECT_EQ(110, codec.id);
      } else if (is_codec("opus")) {
        EXPECT_EQ(111, codec.id);
        ASSERT_TRUE(codec.params.find("minptime") != codec.params.end());
        EXPECT_EQ("10", codec.params.find("minptime")->second);
        ASSERT_TRUE(codec.params.find("useinbandfec") != codec.params.end());
        EXPECT_EQ("1", codec.params.find("useinbandfec")->second);
      }
    }
  }
}

// Tests that VoE supports at least 32 channels
TEST(WebRtcVoiceEngineTest, Has32Channels) {
  test::RunLoop run_loop;
  for (bool use_null_apm : {false, true}) {
    Environment env = CreateTestEnvironment();
    scoped_refptr<test::MockAudioDeviceModule> adm =
        test::MockAudioDeviceModule::CreateNice();
    scoped_refptr<AudioProcessing> apm =
        use_null_apm ? nullptr : BuiltinAudioProcessingBuilder().Build(env);
    WebRtcVoiceEngine engine(
        env, adm, MockAudioEncoderFactory::CreateUnusedFactory(),
        MockAudioDecoderFactory::CreateUnusedFactory(), nullptr, apm, nullptr);
    AutoInitTerminate init_term(engine);
    std::unique_ptr<Call> call =
        Call::Create(CallConfig::CreateSingleThreaded(env));

    std::vector<std::unique_ptr<VoiceMediaSendChannelInterface>> channels;
    while (channels.size() < 32) {
      std::unique_ptr<VoiceMediaSendChannelInterface> channel =
          engine.CreateSendChannel(env, call.get(), MediaConfig(),
                                   AudioOptions(), CryptoOptions());
      if (!channel)
        break;
      channels.emplace_back(std::move(channel));
    }

    EXPECT_EQ(channels.size(), 32u);
  }
}

// Test that we set our preferred codecs properly.
TEST(WebRtcVoiceEngineTest, SetRecvCodecs) {
  test::RunLoop run_loop;
  for (bool use_null_apm : {false, true}) {
    Environment env = CreateTestEnvironment();
    // TODO(ossu): I'm not sure of the intent of this test. It's either:
    // - Check that our builtin codecs are usable by Channel.
    // - The codecs provided by the engine is usable by Channel.
    // It does not check that the codecs in the RecvParameters are actually
    // what we sent in - though it's probably reasonable to expect so, if
    // SetReceiverParameters returns true.
    // I think it will become clear once audio decoder injection is completed.
    scoped_refptr<test::MockAudioDeviceModule> adm =
        test::MockAudioDeviceModule::CreateNice();
    scoped_refptr<AudioProcessing> apm =
        use_null_apm ? nullptr : BuiltinAudioProcessingBuilder().Build(env);
    WebRtcVoiceEngine engine(
        env, adm, MockAudioEncoderFactory::CreateUnusedFactory(),
        CreateBuiltinAudioDecoderFactory(), nullptr, apm, nullptr);
    AutoInitTerminate init_term(engine);
    std::unique_ptr<Call> call =
        Call::Create(CallConfig::CreateSingleThreaded(env));
    WebRtcVoiceReceiveChannel channel(
        env, &engine, MediaConfig(), AudioOptions(), CryptoOptions(),
        call.get(), nullptr, [](uint32_t, const RtpPacketInfos&, Timestamp) {});
    AudioReceiverParameters parameters;
    parameters.codecs = ReceiveCodecsWithId(engine);
    EXPECT_TRUE(channel.SetReceiverParameters(parameters));
  }
}

TEST(WebRtcVoiceEngineTest, SetRtpSendParametersMaxBitrate) {
  test::RunLoop run_loop;
  Environment env = CreateTestEnvironment();
  scoped_refptr<test::MockAudioDeviceModule> adm =
      test::MockAudioDeviceModule::CreateNice();
  FakeAudioSource source;
  WebRtcVoiceEngine engine(env, adm, CreateBuiltinAudioEncoderFactory(),
                           CreateBuiltinAudioDecoderFactory(), nullptr, nullptr,
                           nullptr);
  AutoInitTerminate init_term(engine);
  CallConfig call_config = CallConfig::CreateSingleThreaded(env);
  {
    AudioState::Config config;
    config.audio_mixer = AudioMixerImpl::Create();
    config.audio_device_module = test::MockAudioDeviceModule::CreateNice();
    call_config.audio_state = AudioState::Create(config);
  }
  std::unique_ptr<Call> call = Call::Create(std::move(call_config));
  WebRtcVoiceSendChannel channel(env, &engine, MediaConfig(), AudioOptions(),
                                 CryptoOptions(), call.get());
  {
    AudioSenderParameter params;
    params.codecs.push_back(CreateAudioCodec(1, "opus", 48000, 2));
    params.extensions.push_back(RtpExtension(
        RtpExtension::kTransportSequenceNumberUri, RtpHeaderExtensionId(1)));
    EXPECT_TRUE(channel.SetSenderParameters(params));
  }
  constexpr int kSsrc = 1234;
  {
    StreamParams params;
    params.add_ssrc(kSsrc);
    channel.AddSendStream(params);
  }
  channel.SetAudioSend(kSsrc, true, nullptr, &source);
  channel.SetSend(true);
  RtpParameters params = channel.GetRtpSendParameters(kSsrc);
  for (int max_bitrate : {-10, -1, 0, 10000}) {
    params.encodings[0].max_bitrate_bps = max_bitrate;
    channel.SetRtpSendParameters(
        kSsrc, params, [](RTCError error) { EXPECT_TRUE(error.ok()); });
  }
}

TEST(WebRtcVoiceEngineTest, CollectRecvCodecs) {
  Environment env = CreateTestEnvironment();
  for (bool use_null_apm : {false, true}) {
    std::vector<AudioCodecSpec> specs;
    AudioCodecSpec spec1 = {
        .format = {"codec1", 48000, 2, {{"param1", "value1"}}},
        .info = {48000, 2, 16000, 10000, 20000},
    };
    spec1.info.allow_comfort_noise = false;
    spec1.info.supports_network_adaption = true;
    specs.push_back(spec1);
    AudioCodecSpec spec2 = {
        .format = {"codec2", 48000, 2, {{"param1", "value1"}}},
        .info = {48000, 2, 16000, 10000, 20000}};
    // We do not support 48khz CN.
    spec2.info.allow_comfort_noise = true;
    specs.push_back(spec2);
    specs.push_back({.format = {"codec3", 8000, 1}, .info = {8000, 1, 64000}});
    specs.push_back({.format = {"codec4", 8000, 2}, .info = {8000, 1, 64000}});

    scoped_refptr<MockAudioEncoderFactory> unused_encoder_factory =
        MockAudioEncoderFactory::CreateUnusedFactory();
    scoped_refptr<MockAudioDecoderFactory> mock_decoder_factory =
        make_ref_counted<MockAudioDecoderFactory>();
    EXPECT_CALL(*mock_decoder_factory.get(), GetSupportedDecoders())
        .WillOnce(Return(specs));
    scoped_refptr<test::MockAudioDeviceModule> adm =
        test::MockAudioDeviceModule::CreateNice();

    scoped_refptr<AudioProcessing> apm =
        use_null_apm ? nullptr : BuiltinAudioProcessingBuilder().Build(env);
    WebRtcVoiceEngine engine(env, adm, unused_encoder_factory,
                             mock_decoder_factory, nullptr, apm, nullptr);
    AutoInitTerminate init_term(engine);
    auto codecs = engine.LegacyRecvCodecs();
    EXPECT_EQ(7u, codecs.size());

    // Rather than just ASSERTing that there are enough codecs, ensure that we
    // can check the actual values safely, to provide better test results.
    auto get_codec = [&codecs](size_t index) -> const Codec& {
      static const Codec missing_codec =
          CreateAudioCodec(0, "<missing>", kDefaultAudioClockRateHz, 0);
      if (codecs.size() > index)
        return codecs[index];
      return missing_codec;
    };

    // Ensure the general codecs are generated first and in order.
    for (size_t i = 0; i != specs.size(); ++i) {
      EXPECT_EQ(specs[i].format.name, get_codec(i).name);
      EXPECT_EQ(specs[i].format.clockrate_hz, get_codec(i).clockrate);
      EXPECT_EQ(specs[i].format.num_channels, get_codec(i).channels);
      EXPECT_EQ(specs[i].format.parameters, get_codec(i).params);
    }

    // Find the index of a codec, or -1 if not found, so that we can easily
    // check supplementary codecs are ordered after the general codecs.
    auto find_codec = [&codecs](const SdpAudioFormat& format) -> int {
      for (size_t i = 0; i != codecs.size(); ++i) {
        const Codec& codec = codecs[i];
        if (absl::EqualsIgnoreCase(codec.name, format.name) &&
            codec.clockrate == format.clockrate_hz &&
            codec.channels == format.num_channels) {
          return checked_cast<int>(i);
        }
      }
      return -1;
    };

    // Ensure all supplementary codecs are generated last. Their internal
    // ordering is not important. Without this cast, the comparison turned
    // unsigned and, thus, failed for -1.
    const int num_specs = static_cast<int>(specs.size());
    EXPECT_GE(find_codec({"cn", 8000, 1}), num_specs);
    EXPECT_EQ(find_codec({"cn", 16000, 1}), -1);
    EXPECT_EQ(find_codec({"cn", 32000, 1}), -1);
    EXPECT_EQ(find_codec({"cn", 48000, 1}), -1);
    EXPECT_GE(find_codec({"telephone-event", 8000, 1}), num_specs);
    EXPECT_EQ(find_codec({"telephone-event", 16000, 1}), -1);
    EXPECT_EQ(find_codec({"telephone-event", 32000, 1}), -1);
    EXPECT_GE(find_codec({"telephone-event", 48000, 1}), num_specs);
  }
}

TEST(WebRtcVoiceEngineTest, CollectRecvCodecsWithLatePtAssignment) {
  Environment env = CreateTestEnvironment(
      {.field_trials = "WebRTC-PayloadTypesInTransport/Enabled/"});

  for (bool use_null_apm : {false, true}) {
    std::vector<AudioCodecSpec> specs;
    AudioCodecSpec spec1 = {
        .format = {"codec1", 48000, 2, {{"param1", "value1"}}},
        .info = {48000, 2, 16000, 10000, 20000}};
    spec1.info.allow_comfort_noise = false;
    spec1.info.supports_network_adaption = true;
    specs.push_back(spec1);
    AudioCodecSpec spec2 = {
        .format = {"codec2", 48000, 2, {{"param1", "value1"}}},
        .info = {48000, 2, 16000, 10000, 20000}};
    // We do not support 48khz CN.
    spec2.info.allow_comfort_noise = true;
    specs.push_back(spec2);
    specs.push_back({.format = {"codec3", 8000, 1}, .info = {8000, 1, 64000}});
    specs.push_back({.format = {"codec4", 8000, 2}, .info = {8000, 1, 64000}});

    scoped_refptr<MockAudioEncoderFactory> unused_encoder_factory =
        MockAudioEncoderFactory::CreateUnusedFactory();
    scoped_refptr<MockAudioDecoderFactory> mock_decoder_factory =
        make_ref_counted<MockAudioDecoderFactory>();
    EXPECT_CALL(*mock_decoder_factory.get(), GetSupportedDecoders())
        .WillOnce(Return(specs));
    scoped_refptr<test::MockAudioDeviceModule> adm =
        test::MockAudioDeviceModule::CreateNice();

    scoped_refptr<AudioProcessing> apm =
        use_null_apm ? nullptr : BuiltinAudioProcessingBuilder().Build(env);
    WebRtcVoiceEngine engine(env, adm, unused_encoder_factory,
                             mock_decoder_factory, nullptr, apm, nullptr);
    AutoInitTerminate init_term(engine);
    auto codecs = engine.LegacyRecvCodecs();
    EXPECT_EQ(7u, codecs.size());

    // Rather than just ASSERTing that there are enough codecs, ensure that we
    // can check the actual values safely, to provide better test results.
    auto get_codec = [&codecs](size_t index) -> const Codec& {
      static const Codec missing_codec =
          CreateAudioCodec(0, "<missing>", kDefaultAudioClockRateHz, 0);
      if (codecs.size() > index)
        return codecs[index];
      return missing_codec;
    };

    // Ensure the general codecs are generated first and in order.
    for (size_t i = 0; i != specs.size(); ++i) {
      EXPECT_EQ(specs[i].format.name, get_codec(i).name);
      EXPECT_EQ(specs[i].format.clockrate_hz, get_codec(i).clockrate);
      EXPECT_EQ(specs[i].format.num_channels, get_codec(i).channels);
      EXPECT_EQ(specs[i].format.parameters, get_codec(i).params);
    }

    // Find the index of a codec, or -1 if not found, so that we can easily
    // check supplementary codecs are ordered after the general codecs.
    auto find_codec = [&codecs](const SdpAudioFormat& format) -> int {
      for (size_t i = 0; i != codecs.size(); ++i) {
        const Codec& codec = codecs[i];
        if (absl::EqualsIgnoreCase(codec.name, format.name) &&
            codec.clockrate == format.clockrate_hz &&
            codec.channels == format.num_channels) {
          return checked_cast<int>(i);
        }
      }
      return -1;
    };

    // Ensure all supplementary codecs are generated last. Their internal
    // ordering is not important. Without this cast, the comparison turned
    // unsigned and, thus, failed for -1.
    const int num_specs = static_cast<int>(specs.size());
    EXPECT_GE(find_codec({"cn", 8000, 1}), num_specs);
    EXPECT_EQ(find_codec({"cn", 16000, 1}), -1);
    EXPECT_EQ(find_codec({"cn", 32000, 1}), -1);
    EXPECT_EQ(find_codec({"cn", 48000, 1}), -1);
    EXPECT_GE(find_codec({"telephone-event", 8000, 1}), num_specs);
    EXPECT_EQ(find_codec({"telephone-event", 16000, 1}), -1);
    EXPECT_EQ(find_codec({"telephone-event", 32000, 1}), -1);
    EXPECT_GE(find_codec({"telephone-event", 48000, 1}), num_specs);
  }
}

}  // namespace
}  // namespace webrtc
