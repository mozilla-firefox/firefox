/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "api/environment/environment.h"
#include "api/rtp_header_extension_id.h"
#include "api/rtp_parameters.h"
#include "api/test/video/function_video_decoder_factory.h"
#include "api/test/video/function_video_encoder_factory.h"
#include "api/transport/bitrate_settings.h"
#include "api/units/timestamp.h"
#include "api/video/color_space.h"
#include "api/video/video_frame.h"
#include "api/video/video_rotation.h"
#include "api/video/video_sink_interface.h"
#include "api/video_codecs/scalability_mode.h"
#include "api/video_codecs/sdp_video_format.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_decoder_factory.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "call/video_receive_stream.h"
#include "call/video_send_stream.h"
#include "common_video/test/utilities.h"
#include "media/base/media_constants.h"
#include "modules/video_coding/codecs/av1/dav1d_decoder.h"
#include "modules/video_coding/codecs/av1/libaom_av1_encoder.h"
#include "modules/video_coding/codecs/h264/include/h264.h"
#include "modules/video_coding/codecs/vp8/include/vp8.h"
#include "modules/video_coding/codecs/vp9/include/vp9.h"
#include "modules/video_coding/svc/scalability_mode_util.h"
#include "test/call_test.h"
#include "test/frame_generator_capturer.h"
#include "test/gtest.h"
#include "test/time_controller/simulated_time_controller.h"
#include "test/video_test_constants.h"
#include "video/config/video_encoder_config.h"

namespace webrtc {
namespace {
constexpr RtpHeaderExtensionId kColorSpaceExtensionId(1);
constexpr RtpHeaderExtensionId kVideoRotationExtensionId(2);

// Default CallTest bitrate (300 kbps) is too low to activate multiple spatial
// layers in SVC modes; use 1 Mbps start / 2 Mbps max so all configured spatial
// layers are active.
constexpr int kSvcStartBitrateBps = 1'000'000;
constexpr int kSvcMaxBitrateBps = 2'000'000;

// Scales the default resolution (320x180) by 2^(spatial_layers - 1) so that the
// lowest spatial layer stays at 320x180 and all configured spatial layers meet
// minimum encoder resolution thresholds.
int GetScaleFactor(std::optional<ScalabilityMode> scalability_mode) {
  if (!scalability_mode.has_value()) {
    return 1;
  }
  return 1 << (ScalabilityModeToNumSpatialLayers(*scalability_mode) - 1);
}

class CodecEndToEndTest : public test::CallTest {
 public:
  CodecEndToEndTest()
      : CallTest(std::make_unique<GlobalSimulatedTimeController>(
            /*start_time=*/Timestamp::Seconds(1))) {
    RegisterRtpExtension(
        RtpExtension(RtpExtension::kColorSpaceUri, kColorSpaceExtensionId));
    RegisterRtpExtension(RtpExtension(RtpExtension::kVideoRotationUri,
                                      kVideoRotationExtensionId));
  }
};

class CodecObserver : public test::EndToEndTest,
                      public VideoSinkInterface<VideoFrame> {
 public:
  struct Config {
    int num_frames_to_wait_for = 5;
    VideoRotation rotation = kVideoRotation_0;
    std::optional<ColorSpace> color_space;
    std::optional<ScalabilityMode> scalability_mode;
  };

  CodecObserver(const Config& config,
                const std::string& payload_name,
                VideoEncoderFactory* encoder_factory,
                VideoDecoderFactory* decoder_factory)
      : EndToEndTest(4 * test::VideoTestConstants::kDefaultTimeout),
        // TODO(hta): This timeout (120 seconds) is excessive.
        // https://bugs.webrtc.org/6830
        config_(config),
        payload_name_(payload_name),
        encoder_factory_(encoder_factory),
        decoder_factory_(decoder_factory),
        scale_factor_(GetScaleFactor(config.scalability_mode)),
        frame_counter_(0) {}

  void PerformTest() override {
    EXPECT_TRUE(Wait(time_controller()))
        << "Timed out while waiting for enough frames to be decoded.";
  }

  void ModifySenderBitrateConfig(BitrateConstraints* bitrate_config) override {
    if (config_.scalability_mode.has_value()) {
      bitrate_config->start_bitrate_bps = kSvcStartBitrateBps;
      bitrate_config->max_bitrate_bps = kSvcMaxBitrateBps;
    }
  }

  void ModifyVideoConfigs(
      VideoSendStream::Config* send_config,
      std::vector<VideoReceiveStreamInterface::Config>* receive_configs,
      VideoEncoderConfig* encoder_config) override {
    encoder_config->codec_type = PayloadStringToCodecType(payload_name_);
    if (config_.scalability_mode.has_value()) {
      encoder_config->max_bitrate_bps = kSvcStartBitrateBps;
      encoder_config->simulcast_layers[0].max_bitrate_bps = kSvcStartBitrateBps;
      encoder_config->simulcast_layers[0].target_bitrate_bps =
          kSvcStartBitrateBps;
      encoder_config->simulcast_layers[0].scalability_mode =
          *config_.scalability_mode;
    }
    send_config->encoder_settings.encoder_factory = encoder_factory_;
    send_config->rtp.payload_name = payload_name_;
    send_config->rtp.payload_type =
        test::VideoTestConstants::kVideoSendPayloadType;

    (*receive_configs)[0].renderer = this;
    (*receive_configs)[0].decoders.resize(1);
    (*receive_configs)[0].decoders[0].payload_type =
        send_config->rtp.payload_type;
    (*receive_configs)[0].decoders[0].video_format =
        SdpVideoFormat(send_config->rtp.payload_name);
    (*receive_configs)[0].decoder_factory = decoder_factory_;
  }

  void OnFrame(const VideoFrame& video_frame) override {
    EXPECT_EQ(config_.rotation, video_frame.rotation());
    if (config_.scalability_mode.has_value()) {
      EXPECT_EQ(video_frame.width(),
                scale_factor_ * test::VideoTestConstants::kDefaultWidth);
      EXPECT_EQ(video_frame.height(),
                scale_factor_ * test::VideoTestConstants::kDefaultHeight);
    }
    // Test only if explicit color space has been specified since otherwise the
    // color space is codec dependent.
    if (config_.color_space) {
      EXPECT_EQ(config_.color_space,
                video_frame.color_space()
                    ? std::make_optional(*video_frame.color_space())
                    : std::nullopt);
    }
    if (++frame_counter_ == config_.num_frames_to_wait_for) {
      observation_complete_.Set();
    }
  }

  void ModifyVideoCaptureStartResolution(int* width,
                                         int* height,
                                         int* frame_rate) override {
    *width = scale_factor_ * test::VideoTestConstants::kDefaultWidth;
    *height = scale_factor_ * test::VideoTestConstants::kDefaultHeight;
  }

  void OnFrameGeneratorCapturerCreated(
      test::FrameGeneratorCapturer* frame_generator_capturer) override {
    frame_generator_capturer->SetFakeRotation(config_.rotation);
    frame_generator_capturer->SetFakeColorSpace(config_.color_space);
  }

 private:
  const Config config_;
  const std::string payload_name_;
  VideoEncoderFactory* const encoder_factory_;
  VideoDecoderFactory* const decoder_factory_;
  const int scale_factor_;
  int frame_counter_;
};

TEST_F(CodecEndToEndTest, SendsAndReceivesAv1SvcWithoutDependencyDescriptor) {
  test::FunctionVideoEncoderFactory encoder_factory(
      [](const Environment& env, const SdpVideoFormat& format) {
        return CreateLibaomAv1Encoder(env);
      });
  test::FunctionVideoDecoderFactory decoder_factory(
      [](const Environment& env, const SdpVideoFormat& format) {
        return CreateDav1dDecoder(env);
      });
  // Wait for 20 frames across multiple temporal units to cover both the
  // multi-layer keyframe and subsequent multi-layer delta frames.
  CodecObserver test({.num_frames_to_wait_for = 20,
                      .scalability_mode = ScalabilityMode::kL2T3_KEY},
                     "AV1", &encoder_factory, &decoder_factory);
  RunBaseTest(&test);
}

TEST_F(CodecEndToEndTest, SendsAndReceivesVP8) {
  test::FunctionVideoEncoderFactory encoder_factory(
      [](const Environment& env, const SdpVideoFormat& format) {
        return CreateVp8Encoder(env);
      });
  test::FunctionVideoDecoderFactory decoder_factory(
      [](const Environment& env, const SdpVideoFormat& format) {
        return CreateVp8Decoder(env);
      });
  CodecObserver test({}, "VP8", &encoder_factory, &decoder_factory);
  RunBaseTest(&test);
}

TEST_F(CodecEndToEndTest, SendsAndReceivesVP8Rotation90) {
  test::FunctionVideoEncoderFactory encoder_factory(
      [](const Environment& env, const SdpVideoFormat& format) {
        return CreateVp8Encoder(env);
      });
  test::FunctionVideoDecoderFactory decoder_factory(
      [](const Environment& env, const SdpVideoFormat& format) {
        return CreateVp8Decoder(env);
      });
  CodecObserver test({.rotation = kVideoRotation_90}, "VP8", &encoder_factory,
                     &decoder_factory);
  RunBaseTest(&test);
}

#if defined(RTC_ENABLE_VP9)
TEST_F(CodecEndToEndTest, SendsAndReceivesVP9) {
  test::FunctionVideoEncoderFactory encoder_factory(
      [](const Environment& env, const SdpVideoFormat& format) {
        return CreateVp9Encoder(env);
      });
  test::FunctionVideoDecoderFactory decoder_factory(
      []() { return VP9Decoder::Create(); });
  CodecObserver test({.num_frames_to_wait_for = 500}, "VP9", &encoder_factory,
                     &decoder_factory);
  RunBaseTest(&test);
}

TEST_F(CodecEndToEndTest, SendsAndReceivesVP9VideoRotation90) {
  test::FunctionVideoEncoderFactory encoder_factory(
      [](const Environment& env, const SdpVideoFormat& format) {
        return CreateVp9Encoder(env);
      });
  test::FunctionVideoDecoderFactory decoder_factory(
      []() { return VP9Decoder::Create(); });
  CodecObserver test({.rotation = kVideoRotation_90}, "VP9", &encoder_factory,
                     &decoder_factory);
  RunBaseTest(&test);
}

TEST_F(CodecEndToEndTest, SendsAndReceivesVP9ExplicitColorSpace) {
  test::FunctionVideoEncoderFactory encoder_factory(
      [](const Environment& env, const SdpVideoFormat& format) {
        return CreateVp9Encoder(env);
      });
  test::FunctionVideoDecoderFactory decoder_factory(
      []() { return VP9Decoder::Create(); });
  CodecObserver test(
      {.rotation = kVideoRotation_90,
       .color_space = CreateTestColorSpace(/*with_hdr_metadata=*/false)},
      "VP9", &encoder_factory, &decoder_factory);
  RunBaseTest(&test);
}

TEST_F(CodecEndToEndTest,
       SendsAndReceivesVP9ExplicitColorSpaceWithHdrMetadata) {
  test::FunctionVideoEncoderFactory encoder_factory(
      [](const Environment& env, const SdpVideoFormat& format) {
        return CreateVp9Encoder(env);
      });
  test::FunctionVideoDecoderFactory decoder_factory(
      []() { return VP9Decoder::Create(); });
  CodecObserver test(
      {.rotation = kVideoRotation_90,
       .color_space = CreateTestColorSpace(/*with_hdr_metadata=*/true)},
      "VP9", &encoder_factory, &decoder_factory);
  RunBaseTest(&test);
}

#endif  // defined(RTC_ENABLE_VP9)

#if defined(WEBRTC_USE_H264)
class EndToEndTestH264 : public test::CallTest,
                         public ::testing::WithParamInterface<std::string> {
 public:
  EndToEndTestH264()
      : CallTest(std::make_unique<GlobalSimulatedTimeController>(
            /*start_time=*/Timestamp::Seconds(1))) {
    field_trials().Set("WebRTC-SpsPpsIdrIsH264Keyframe", GetParam());
    RegisterRtpExtension(RtpExtension(RtpExtension::kVideoRotationUri,
                                      kVideoRotationExtensionId));
  }
};

INSTANTIATE_TEST_SUITE_P(
    SpsPpsIdrIsKeyframe,
    EndToEndTestH264,
    ::testing::Values(/*WebRTC-SpsPpsIdrIsH264Keyframe*/ "Disabled",
                      /*WebRTC-SpsPpsIdrIsH264Keyframe*/ "Enabled"));

TEST_P(EndToEndTestH264, SendsAndReceivesH264) {
  test::FunctionVideoEncoderFactory encoder_factory(
      [](const Environment& env, const SdpVideoFormat& format) {
        return CreateH264Encoder(env);
      });
  test::FunctionVideoDecoderFactory decoder_factory(
      []() { return H264Decoder::Create(); });
  CodecObserver test({.num_frames_to_wait_for = 500}, "H264", &encoder_factory,
                     &decoder_factory);
  RunBaseTest(&test);
}

TEST_P(EndToEndTestH264, SendsAndReceivesH264VideoRotation90) {
  test::FunctionVideoEncoderFactory encoder_factory(
      [](const Environment& env, const SdpVideoFormat& format) {
        return CreateH264Encoder(env);
      });
  test::FunctionVideoDecoderFactory decoder_factory(
      []() { return H264Decoder::Create(); });
  CodecObserver test({.rotation = kVideoRotation_90}, "H264", &encoder_factory,
                     &decoder_factory);
  RunBaseTest(&test);
}

TEST_P(EndToEndTestH264, SendsAndReceivesH264PacketizationMode0) {
  SdpVideoFormat codec(kH264CodecName);
  codec.parameters[kH264FmtpPacketizationMode] = "0";
  test::FunctionVideoEncoderFactory encoder_factory(
      [codec](const Environment& env, const SdpVideoFormat& format) {
        return CreateH264Encoder(env, H264EncoderSettings::Parse(codec));
      });
  test::FunctionVideoDecoderFactory decoder_factory(
      []() { return H264Decoder::Create(); });
  CodecObserver test({.num_frames_to_wait_for = 500}, "H264", &encoder_factory,
                     &decoder_factory);
  RunBaseTest(&test);
}

TEST_P(EndToEndTestH264, SendsAndReceivesH264PacketizationMode1) {
  SdpVideoFormat codec(kH264CodecName);
  codec.parameters[kH264FmtpPacketizationMode] = "1";
  test::FunctionVideoEncoderFactory encoder_factory(
      [codec](const Environment& env, const SdpVideoFormat& format) {
        return CreateH264Encoder(env, H264EncoderSettings::Parse(codec));
      });
  test::FunctionVideoDecoderFactory decoder_factory(
      []() { return H264Decoder::Create(); });
  CodecObserver test({.num_frames_to_wait_for = 500}, "H264", &encoder_factory,
                     &decoder_factory);
  RunBaseTest(&test);
}
#endif  // defined(WEBRTC_USE_H264)

}  // namespace
}  // namespace webrtc
