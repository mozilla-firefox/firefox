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

#include "absl/types/optional.h"
#include "api/environment/environment.h"
#include "api/test/video/function_video_encoder_factory.h"
#include "api/video/color_space.h"
#include "api/video/video_rotation.h"
#include "common_video/test/utilities.h"
#include "media/base/codec.h"
#include "media/base/media_constants.h"
#include "media/engine/internal_decoder_factory.h"
#include "media/engine/internal_encoder_factory.h"
#include "modules/video_coding/codecs/h264/include/h264.h"
#include "modules/video_coding/codecs/vp8/include/vp8.h"
#include "modules/video_coding/codecs/vp9/include/vp9.h"
#include "test/call_test.h"
#include "test/encoder_settings.h"
#include "test/field_trial.h"
#include "test/gtest.h"
#include "test/video_test_constants.h"

namespace webrtc {
namespace {
enum : int {  // The first valid value is 1.
  kColorSpaceExtensionId = 1,
  kVideoRotationExtensionId,
};
}  // namespace

class CodecEndToEndTest : public test::CallTest {
 public:
  CodecEndToEndTest() {
    RegisterRtpExtension(
        RtpExtension(RtpExtension::kColorSpaceUri, kColorSpaceExtensionId));
    RegisterRtpExtension(RtpExtension(RtpExtension::kVideoRotationUri,
                                      kVideoRotationExtensionId));
  }
};

class CodecObserver : public test::EndToEndTest,
                      public rtc::VideoSinkInterface<VideoFrame> {
 public:
  CodecObserver(int no_frames_to_wait_for,
                VideoRotation rotation_to_test,
                absl::optional<ColorSpace> color_space_to_test,
                const std::string& payload_name,
                VideoEncoderFactory* encoder_factory,
                VideoDecoderFactory* decoder_factory)
      : EndToEndTest(4 * test::VideoTestConstants::kDefaultTimeout),
        // TODO(hta): This timeout (120 seconds) is excessive.
        // https://bugs.webrtc.org/6830
        no_frames_to_wait_for_(no_frames_to_wait_for),
        expected_rotation_(rotation_to_test),
        expected_color_space_(color_space_to_test),
        payload_name_(payload_name),
        encoder_factory_(encoder_factory),
        decoder_factory_(decoder_factory),
        frame_counter_(0) {}

  void PerformTest() override {
    EXPECT_TRUE(Wait())
        << "Timed out while waiting for enough frames to be decoded.";
  }

  void ModifyVideoConfigs(
      VideoSendStream::Config* send_config,
      std::vector<VideoReceiveStreamInterface::Config>* receive_configs,
      VideoEncoderConfig* encoder_config) override {
    encoder_config->codec_type = PayloadStringToCodecType(payload_name_);
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
    EXPECT_EQ(expected_rotation_, video_frame.rotation());
    // Test only if explicit color space has been specified since otherwise the
    // color space is codec dependent.
    if (expected_color_space_) {
      EXPECT_EQ(expected_color_space_,
                video_frame.color_space()
                    ? absl::make_optional(*video_frame.color_space())
                    : absl::nullopt);
    }
    if (++frame_counter_ == no_frames_to_wait_for_)
      observation_complete_.Set();
  }

  void OnFrameGeneratorCapturerCreated(
      test::FrameGeneratorCapturer* frame_generator_capturer) override {
    frame_generator_capturer->SetFakeRotation(expected_rotation_);
    frame_generator_capturer->SetFakeColorSpace(expected_color_space_);
  }

 private:
  int no_frames_to_wait_for_;
  VideoRotation expected_rotation_;
  absl::optional<ColorSpace> expected_color_space_;
  std::string payload_name_;
  VideoEncoderFactory* encoder_factory_;
  VideoDecoderFactory* decoder_factory_;
  int frame_counter_;
};

TEST_F(CodecEndToEndTest, SendsAndReceivesVP8) {
  test::FunctionVideoEncoderFactory encoder_factory(
      [](const Environment& env, const SdpVideoFormat& format) {
        return CreateVp8Encoder(env);
      });
  test::FunctionVideoDecoderFactory decoder_factory(
      [](const Environment& env, const SdpVideoFormat& format) {
        return CreateVp8Decoder(env);
      });
  CodecObserver test(5, kVideoRotation_0, absl::nullopt, "VP8",
                     &encoder_factory, &decoder_factory);
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
  CodecObserver test(5, kVideoRotation_90, absl::nullopt, "VP8",
                     &encoder_factory, &decoder_factory);
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
  CodecObserver test(500, kVideoRotation_0, absl::nullopt, "VP9",
                     &encoder_factory, &decoder_factory);
  RunBaseTest(&test);
}

TEST_F(CodecEndToEndTest, SendsAndReceivesVP9VideoRotation90) {
  test::FunctionVideoEncoderFactory encoder_factory(
      [](const Environment& env, const SdpVideoFormat& format) {
        return CreateVp9Encoder(env);
      });
  test::FunctionVideoDecoderFactory decoder_factory(
      []() { return VP9Decoder::Create(); });
  CodecObserver test(5, kVideoRotation_90, absl::nullopt, "VP9",
                     &encoder_factory, &decoder_factory);
  RunBaseTest(&test);
}

TEST_F(CodecEndToEndTest, SendsAndReceivesVP9ExplicitColorSpace) {
  test::FunctionVideoEncoderFactory encoder_factory(
      [](const Environment& env, const SdpVideoFormat& format) {
        return CreateVp9Encoder(env);
      });
  test::FunctionVideoDecoderFactory decoder_factory(
      []() { return VP9Decoder::Create(); });
  CodecObserver test(5, kVideoRotation_90,
                     CreateTestColorSpace(/*with_hdr_metadata=*/false), "VP9",
                     &encoder_factory, &decoder_factory);
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
  CodecObserver test(5, kVideoRotation_90,
                     CreateTestColorSpace(/*with_hdr_metadata=*/true), "VP9",
                     &encoder_factory, &decoder_factory);
  RunBaseTest(&test);
}

#endif  // defined(RTC_ENABLE_VP9)

#if defined(WEBRTC_USE_H264)
class EndToEndTestH264 : public test::CallTest,
                         public ::testing::WithParamInterface<std::string> {
 public:
  EndToEndTestH264() : field_trial_(GetParam()) {
    RegisterRtpExtension(RtpExtension(RtpExtension::kVideoRotationUri,
                                      kVideoRotationExtensionId));
  }

 private:
  test::ScopedFieldTrials field_trial_;
};

INSTANTIATE_TEST_SUITE_P(
    SpsPpsIdrIsKeyframe,
    EndToEndTestH264,
    ::testing::Values("WebRTC-SpsPpsIdrIsH264Keyframe/Disabled/",
                      "WebRTC-SpsPpsIdrIsH264Keyframe/Enabled/"));

TEST_P(EndToEndTestH264, SendsAndReceivesH264) {
  test::FunctionVideoEncoderFactory encoder_factory(
      [](const Environment& env, const SdpVideoFormat& format) {
        return CreateH264Encoder(env);
      });
  test::FunctionVideoDecoderFactory decoder_factory(
      []() { return H264Decoder::Create(); });
  CodecObserver test(500, kVideoRotation_0, absl::nullopt, "H264",
                     &encoder_factory, &decoder_factory);
  RunBaseTest(&test);
}

TEST_P(EndToEndTestH264, SendsAndReceivesH264VideoRotation90) {
  test::FunctionVideoEncoderFactory encoder_factory(
      [](const Environment& env, const SdpVideoFormat& format) {
        return CreateH264Encoder(env);
      });
  test::FunctionVideoDecoderFactory decoder_factory(
      []() { return H264Decoder::Create(); });
  CodecObserver test(5, kVideoRotation_90, absl::nullopt, "H264",
                     &encoder_factory, &decoder_factory);
  RunBaseTest(&test);
}

TEST_P(EndToEndTestH264, SendsAndReceivesH264PacketizationMode0) {
  SdpVideoFormat codec(cricket::kH264CodecName);
  codec.parameters[cricket::kH264FmtpPacketizationMode] = "0";
  test::FunctionVideoEncoderFactory encoder_factory(
      [codec](const Environment& env, const SdpVideoFormat& format) {
        return CreateH264Encoder(env, H264EncoderSettings::Parse(codec));
      });
  test::FunctionVideoDecoderFactory decoder_factory(
      []() { return H264Decoder::Create(); });
  CodecObserver test(500, kVideoRotation_0, absl::nullopt, "H264",
                     &encoder_factory, &decoder_factory);
  RunBaseTest(&test);
}

TEST_P(EndToEndTestH264, SendsAndReceivesH264PacketizationMode1) {
  SdpVideoFormat codec(cricket::kH264CodecName);
  codec.parameters[cricket::kH264FmtpPacketizationMode] = "1";
  test::FunctionVideoEncoderFactory encoder_factory(
      [codec](const Environment& env, const SdpVideoFormat& format) {
        return CreateH264Encoder(env, H264EncoderSettings::Parse(codec));
      });
  test::FunctionVideoDecoderFactory decoder_factory(
      []() { return H264Decoder::Create(); });
  CodecObserver test(500, kVideoRotation_0, absl::nullopt, "H264",
                     &encoder_factory, &decoder_factory);
  RunBaseTest(&test);
}
#endif  // defined(WEBRTC_USE_H264)

}  // namespace webrtc
