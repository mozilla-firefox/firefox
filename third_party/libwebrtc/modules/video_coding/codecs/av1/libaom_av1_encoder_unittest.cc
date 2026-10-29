/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_coding/codecs/av1/libaom_av1_encoder.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "api/environment/environment.h"
#include "api/environment/environment_factory.h"
#include "api/field_trials.h"
#include "api/scoped_refptr.h"
#include "api/test/create_frame_generator.h"
#include "api/test/frame_generator_interface.h"
#include "api/test/mock_video_encoder.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/encoded_image.h"
#include "api/video/i420_buffer.h"
#include "api/video/render_resolution.h"
#include "api/video/video_bitrate_allocation.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/video_frame_type.h"
#include "api/video_codecs/scalability_mode.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_decoder.h"
#include "api/video_codecs/video_encoder.h"
#include "common_video/libyuv/include/webrtc_libyuv.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/video_coding/codecs/av1/av1_svc_config.h"
#include "modules/video_coding/codecs/av1/dav1d_decoder.h"
#include "modules/video_coding/codecs/test/encoded_video_frame_producer.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "rtc_base/checks.h"
#include "system_wrappers/include/clock.h"
#include "test/create_test_environment.h"
#include "test/create_test_field_trials.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/testsupport/file_utils.h"
#include "test/testsupport/frame_reader.h"

namespace webrtc {
namespace {

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Field;
using ::testing::Gt;
using ::testing::IsEmpty;
using ::testing::Property;
using ::testing::Return;
using ::testing::SizeIs;
using ::testing::Values;

VideoCodec DefaultCodecSettings() {
  VideoCodec codec_settings;
  codec_settings.codecType = kVideoCodecAV1;
  codec_settings.width = 320;
  codec_settings.height = 180;
  codec_settings.maxFramerate = 30;
  codec_settings.startBitrate = 1000;
  codec_settings.qpMax = 63;
  return codec_settings;
}

VideoCodec HDCodecSettings() {
  VideoCodec codec_settings;
  codec_settings.codecType = kVideoCodecAV1;
  codec_settings.width = 1280;
  codec_settings.height = 720;
  codec_settings.maxFramerate = 30;
  codec_settings.startBitrate = 2048;
  codec_settings.maxBitrate = 2048;
  codec_settings.qpMax = 63;
  return codec_settings;
}

VideoEncoder::Settings DefaultEncoderSettings() {
  return VideoEncoder::Settings(
      VideoEncoder::Capabilities(/*loss_notification=*/false),
      /*number_of_cores=*/1, /*max_payload_size=*/1200);
}

class LibaomAv1EncoderTest : public ::testing::TestWithParam<bool> {
 public:
  LibaomAv1EncoderTest()
      : drop_repeat_frames_on_enhancement_layers_(GetParam()) {}

 protected:
  Environment CreateTestEnvironment() {
    auto field_trials = std::make_unique<FieldTrials>(CreateTestFieldTrials(
        drop_repeat_frames_on_enhancement_layers_
            ? "WebRTC-LibaomAv1Encoder-DropRepeatFramesOnEnhancementLayers/"
              "Enabled/"
            : "WebRTC-LibaomAv1Encoder-DropRepeatFramesOnEnhancementLayers/"
              "Disabled/"));
    return CreateEnvironment(std::move(field_trials));
  }

  const bool drop_repeat_frames_on_enhancement_layers_;
};

INSTANTIATE_TEST_SUITE_P(DropRepeatFrames,
                         LibaomAv1EncoderTest,
                         testing::Bool());

TEST_P(LibaomAv1EncoderTest, CanCreate) {
  std::unique_ptr<VideoEncoder> encoder =
      CreateLibaomAv1Encoder(CreateTestEnvironment());
  EXPECT_TRUE(encoder);
}

TEST_P(LibaomAv1EncoderTest, InitAndRelease) {
  std::unique_ptr<VideoEncoder> encoder =
      CreateLibaomAv1Encoder(CreateTestEnvironment());
  ASSERT_TRUE(encoder);
  VideoCodec codec_settings = DefaultCodecSettings();
  EXPECT_EQ(encoder->InitEncode(&codec_settings, DefaultEncoderSettings()),
            WEBRTC_VIDEO_CODEC_OK);
  EXPECT_EQ(encoder->Release(), WEBRTC_VIDEO_CODEC_OK);
}

TEST_P(LibaomAv1EncoderTest,
       NoBitrateOnTopLayerRefecltedInActiveDecodeTargets) {
  // Configure encoder with 2 temporal layers.
  std::unique_ptr<VideoEncoder> encoder =
      CreateLibaomAv1Encoder(CreateTestEnvironment());
  VideoCodec codec_settings = DefaultCodecSettings();
  codec_settings.SetScalabilityMode(ScalabilityMode::kL1T2);
  ASSERT_EQ(encoder->InitEncode(&codec_settings, DefaultEncoderSettings()),
            WEBRTC_VIDEO_CODEC_OK);

  VideoEncoder::RateControlParameters rate_parameters;
  rate_parameters.framerate_fps = 30;
  rate_parameters.bitrate.SetBitrate(0, /*temporal_index=*/0, 300'000);
  rate_parameters.bitrate.SetBitrate(0, /*temporal_index=*/1, 0);
  encoder->SetRates(rate_parameters);

  std::vector<EncodedVideoFrameProducer::EncodedFrame> encoded_frames =
      EncodedVideoFrameProducer(*encoder).SetNumInputFrames(1).Encode();
  ASSERT_THAT(encoded_frames, SizeIs(1));
  ASSERT_NE(encoded_frames[0].codec_specific_info.generic_frame_info,
            std::nullopt);
  // Assuming L1T2 structure uses 1st decode target for T0 and 2nd decode target
  // for T0+T1 frames, expect only 1st decode target is active.
  EXPECT_EQ(encoded_frames[0]
                .codec_specific_info.generic_frame_info->active_decode_targets,
            0b01);
}

TEST_P(LibaomAv1EncoderTest,
       SpatialScalabilityInTemporalUnitReportedAsDeltaFrame) {
  std::unique_ptr<VideoEncoder> encoder =
      CreateLibaomAv1Encoder(CreateTestEnvironment());
  VideoCodec codec_settings = DefaultCodecSettings();
  codec_settings.SetScalabilityMode(ScalabilityMode::kL2T1);
  ASSERT_EQ(encoder->InitEncode(&codec_settings, DefaultEncoderSettings()),
            WEBRTC_VIDEO_CODEC_OK);

  VideoEncoder::RateControlParameters rate_parameters;
  rate_parameters.framerate_fps = 30;
  rate_parameters.bitrate.SetBitrate(/*spatial_index=*/0, 0, 300'000);
  rate_parameters.bitrate.SetBitrate(/*spatial_index=*/1, 0, 300'000);
  encoder->SetRates(rate_parameters);

  std::vector<EncodedVideoFrameProducer::EncodedFrame> encoded_frames =
      EncodedVideoFrameProducer(*encoder).SetNumInputFrames(1).Encode();
  ASSERT_THAT(encoded_frames, SizeIs(2));
  EXPECT_THAT(encoded_frames[0].encoded_image.frame_type(),
              Eq(VideoFrameType::kVideoFrameKey));
  EXPECT_THAT(encoded_frames[1].encoded_image.frame_type(),
              Eq(VideoFrameType::kVideoFrameDelta));
}

TEST_P(LibaomAv1EncoderTest, NoBitrateOnTopSpatialLayerProduceDeltaFrames) {
  std::unique_ptr<VideoEncoder> encoder =
      CreateLibaomAv1Encoder(CreateTestEnvironment());
  VideoCodec codec_settings = DefaultCodecSettings();
  codec_settings.SetScalabilityMode(ScalabilityMode::kL2T1);
  ASSERT_EQ(encoder->InitEncode(&codec_settings, DefaultEncoderSettings()),
            WEBRTC_VIDEO_CODEC_OK);

  VideoEncoder::RateControlParameters rate_parameters;
  rate_parameters.framerate_fps = 30;
  rate_parameters.bitrate.SetBitrate(/*spatial_index=*/0, 0, 300'000);
  rate_parameters.bitrate.SetBitrate(/*spatial_index=*/1, 0, 0);
  encoder->SetRates(rate_parameters);

  std::vector<EncodedVideoFrameProducer::EncodedFrame> encoded_frames =
      EncodedVideoFrameProducer(*encoder).SetNumInputFrames(2).Encode();
  ASSERT_THAT(encoded_frames, SizeIs(2));
  EXPECT_THAT(encoded_frames[0].encoded_image.frame_type(),
              Eq(VideoFrameType::kVideoFrameKey));
  EXPECT_THAT(encoded_frames[1].encoded_image.frame_type(),
              Eq(VideoFrameType::kVideoFrameDelta));
}

TEST_P(LibaomAv1EncoderTest, SetsEndOfPictureForLastFrameInTemporalUnit) {
  VideoBitrateAllocation allocation;
  allocation.SetBitrate(0, 0, 30000);
  allocation.SetBitrate(1, 0, 40000);
  allocation.SetBitrate(2, 0, 30000);

  std::unique_ptr<VideoEncoder> encoder =
      CreateLibaomAv1Encoder(CreateTestEnvironment());
  VideoCodec codec_settings = DefaultCodecSettings();
  // Configure encoder with 3 spatial layers.
  codec_settings.SetScalabilityMode(ScalabilityMode::kL3T1);
  codec_settings.startBitrate = allocation.get_sum_kbps();
  ASSERT_EQ(encoder->InitEncode(&codec_settings, DefaultEncoderSettings()),
            WEBRTC_VIDEO_CODEC_OK);

  encoder->SetRates(VideoEncoder::RateControlParameters(
      allocation, codec_settings.maxFramerate));

  std::vector<EncodedVideoFrameProducer::EncodedFrame> encoded_frames =
      EncodedVideoFrameProducer(*encoder).SetNumInputFrames(2).Encode();
  ASSERT_THAT(encoded_frames, SizeIs(6));
  EXPECT_FALSE(encoded_frames[0].codec_specific_info.end_of_picture);
  EXPECT_FALSE(encoded_frames[1].codec_specific_info.end_of_picture);
  EXPECT_TRUE(encoded_frames[2].codec_specific_info.end_of_picture);
  EXPECT_FALSE(encoded_frames[3].codec_specific_info.end_of_picture);
  EXPECT_FALSE(encoded_frames[4].codec_specific_info.end_of_picture);
  EXPECT_TRUE(encoded_frames[5].codec_specific_info.end_of_picture);
}

TEST_P(LibaomAv1EncoderTest,
       SetsEndOfPictureForLastFrameInTemporalUnitWhenLayerDrop) {
  VideoBitrateAllocation allocation;
  allocation.SetBitrate(0, 0, 30000);
  allocation.SetBitrate(1, 0, 40000);
  // Lower bitrate for the last spatial layer to provoke layer drop.
  allocation.SetBitrate(2, 0, 500);

  std::unique_ptr<VideoEncoder> encoder =
      CreateLibaomAv1Encoder(CreateTestEnvironment());
  VideoCodec codec_settings = DefaultCodecSettings();
  // Configure encoder with 3 spatial layers.
  codec_settings.SetScalabilityMode(ScalabilityMode::kL3T1);
  codec_settings.startBitrate = allocation.get_sum_kbps();
  ASSERT_EQ(encoder->InitEncode(&codec_settings, DefaultEncoderSettings()),
            WEBRTC_VIDEO_CODEC_OK);

  encoder->SetRates(VideoEncoder::RateControlParameters(
      allocation, codec_settings.maxFramerate));

  std::vector<EncodedVideoFrameProducer::EncodedFrame> encoded_frames =
      EncodedVideoFrameProducer(*encoder).SetNumInputFrames(2).Encode();
  ASSERT_THAT(encoded_frames, SizeIs(4));
  EXPECT_FALSE(encoded_frames[0].codec_specific_info.end_of_picture);
  EXPECT_TRUE(encoded_frames[1].codec_specific_info.end_of_picture);
  EXPECT_FALSE(encoded_frames[2].codec_specific_info.end_of_picture);
  EXPECT_TRUE(encoded_frames[3].codec_specific_info.end_of_picture);
}

TEST_P(LibaomAv1EncoderTest, CheckOddDimensionsWithSpatialLayers) {
  VideoBitrateAllocation allocation;
  allocation.SetBitrate(0, 0, 30000);
  allocation.SetBitrate(1, 0, 40000);
  allocation.SetBitrate(2, 0, 30000);
  std::unique_ptr<VideoEncoder> encoder =
      CreateLibaomAv1Encoder(CreateTestEnvironment());
  VideoCodec codec_settings = DefaultCodecSettings();
  // Configure encoder with 3 spatial layers.
  codec_settings.SetScalabilityMode(ScalabilityMode::kL3T1);
  // Odd width and height values should not make encoder crash.
  codec_settings.width = 623;
  codec_settings.height = 405;
  codec_settings.startBitrate = allocation.get_sum_kbps();
  ASSERT_EQ(encoder->InitEncode(&codec_settings, DefaultEncoderSettings()),
            WEBRTC_VIDEO_CODEC_OK);
  encoder->SetRates(VideoEncoder::RateControlParameters(
      allocation, codec_settings.maxFramerate));
  EncodedVideoFrameProducer evfp(*encoder);
  evfp.SetResolution(RenderResolution{623, 405});
  std::vector<EncodedVideoFrameProducer::EncodedFrame> encoded_frames =
      evfp.SetNumInputFrames(2).Encode();
  ASSERT_THAT(encoded_frames, SizeIs(6));
}

class LibaomAv1EncoderMaxConsecDropTest
    : public ::testing::TestWithParam</*framerate_fps=*/int> {};

TEST_P(LibaomAv1EncoderMaxConsecDropTest, MaxConsecDrops) {
  VideoBitrateAllocation allocation;
  allocation.SetBitrate(0, 0,
                        2000);  // A low bitrate to provoke frame drops.
  std::unique_ptr<VideoEncoder> encoder =
      CreateLibaomAv1Encoder(CreateTestEnvironment());
  VideoCodec codec_settings = DefaultCodecSettings();
  codec_settings.SetFrameDropEnabled(true);
  codec_settings.SetScalabilityMode(ScalabilityMode::kL1T1);
  codec_settings.startBitrate = allocation.get_sum_kbps();
  codec_settings.maxFramerate = GetParam();
  ASSERT_EQ(encoder->InitEncode(&codec_settings, DefaultEncoderSettings()),
            WEBRTC_VIDEO_CODEC_OK);
  encoder->SetRates(VideoEncoder::RateControlParameters(
      allocation, codec_settings.maxFramerate));
  std::vector<EncodedVideoFrameProducer::EncodedFrame> encoded_frames =
      EncodedVideoFrameProducer(*encoder)
          .SetNumInputFrames(60)
          .SetFramerateFps(codec_settings.maxFramerate)
          .SetResolution(RenderResolution{320, 180})
          .Encode();
  ASSERT_GE(encoded_frames.size(), 2u);

  int max_consec_drops = 0;
  for (size_t i = 1; i < encoded_frames.size(); ++i) {
    uint32_t frame_duration_rtp =
        encoded_frames[i].encoded_image.RtpTimestamp() -
        encoded_frames[i - 1].encoded_image.RtpTimestamp();
    // X consecutive drops result in a freeze of (X + 1) frame duration.
    // Subtract 1 to get pure number of drops.
    int num_drops = frame_duration_rtp * codec_settings.maxFramerate /
                        kVideoPayloadTypeFrequency -
                    1;
    max_consec_drops = std::max(max_consec_drops, num_drops);
  }

  const int expected_max_consec_drops =
      std::ceil(0.25 * codec_settings.maxFramerate);
  EXPECT_EQ(max_consec_drops, expected_max_consec_drops);
}

INSTANTIATE_TEST_SUITE_P(LibaomAv1EncoderMaxConsecDropTests,
                         LibaomAv1EncoderMaxConsecDropTest,
                         Values(1, 2, 5, 15, 30, 60));

TEST_P(LibaomAv1EncoderTest, EncoderInfoWithoutResolutionBitrateLimits) {
  std::unique_ptr<VideoEncoder> encoder =
      CreateLibaomAv1Encoder(CreateTestEnvironment());
  EXPECT_TRUE(encoder->GetEncoderInfo().resolution_bitrate_limits.empty());
}

TEST_P(LibaomAv1EncoderTest, EncoderInfoWithBitrateLimitsFromFieldTrial) {
  auto field_trials = std::make_unique<FieldTrials>(
      CreateTestFieldTrials("WebRTC-Av1-GetEncoderInfoOverride/"
                            "frame_size_pixels:123|456|789,"
                            "min_start_bitrate_bps:11000|22000|33000,"
                            "min_bitrate_bps:44000|55000|66000,"
                            "max_bitrate_bps:77000|88000|99000/"));
  const Environment env = CreateEnvironment(std::move(field_trials));
  std::unique_ptr<VideoEncoder> encoder = CreateLibaomAv1Encoder(env);

  EXPECT_THAT(
      encoder->GetEncoderInfo().resolution_bitrate_limits,
      ::testing::ElementsAre(
          VideoEncoder::ResolutionBitrateLimits{123, 11000, 44000, 77000},
          VideoEncoder::ResolutionBitrateLimits{456, 22000, 55000, 88000},
          VideoEncoder::ResolutionBitrateLimits{789, 33000, 66000, 99000}));
}

TEST_P(LibaomAv1EncoderTest, EncoderInfoProvidesFpsAllocation) {
  std::unique_ptr<VideoEncoder> encoder =
      CreateLibaomAv1Encoder(CreateTestEnvironment());
  VideoCodec codec_settings = DefaultCodecSettings();
  codec_settings.SetScalabilityMode(ScalabilityMode::kL3T3);
  codec_settings.maxFramerate = 60;
  ASSERT_EQ(encoder->InitEncode(&codec_settings, DefaultEncoderSettings()),
            WEBRTC_VIDEO_CODEC_OK);

  const auto& encoder_info = encoder->GetEncoderInfo();
  EXPECT_THAT(encoder_info.fps_allocation[0],
              ElementsAre(255 / 4, 255 / 2, 255));
  EXPECT_THAT(encoder_info.fps_allocation[1],
              ElementsAre(255 / 4, 255 / 2, 255));
  EXPECT_THAT(encoder_info.fps_allocation[2],
              ElementsAre(255 / 4, 255 / 2, 255));
  EXPECT_THAT(encoder_info.fps_allocation[3], IsEmpty());
}

TEST_P(LibaomAv1EncoderTest, PopulatesEncodedFrameSize) {
  VideoBitrateAllocation allocation;
  allocation.SetBitrate(0, 0, 30000);
  allocation.SetBitrate(1, 0, 40000);
  allocation.SetBitrate(2, 0, 30000);
  std::unique_ptr<VideoEncoder> encoder =
      CreateLibaomAv1Encoder(CreateTestEnvironment());
  VideoCodec codec_settings = DefaultCodecSettings();
  codec_settings.startBitrate = allocation.get_sum_kbps();
  ASSERT_GT(codec_settings.width, 4);
  // Configure encoder with 3 spatial layers.
  codec_settings.SetScalabilityMode(ScalabilityMode::kL3T1);
  ASSERT_EQ(encoder->InitEncode(&codec_settings, DefaultEncoderSettings()),
            WEBRTC_VIDEO_CODEC_OK);
  encoder->SetRates(VideoEncoder::RateControlParameters(
      allocation, codec_settings.maxFramerate));
  using Frame = EncodedVideoFrameProducer::EncodedFrame;
  std::vector<Frame> encoded_frames =
      EncodedVideoFrameProducer(*encoder).SetNumInputFrames(1).Encode();
  EXPECT_THAT(
      encoded_frames,
      ElementsAre(
          Field(&Frame::encoded_image,
                AllOf(Field(&EncodedImage::_encodedWidth,
                            codec_settings.width / 4),
                      Field(&EncodedImage::_encodedHeight,
                            codec_settings.height / 4))),
          Field(&Frame::encoded_image,
                AllOf(Field(&EncodedImage::_encodedWidth,
                            codec_settings.width / 2),
                      Field(&EncodedImage::_encodedHeight,
                            codec_settings.height / 2))),
          Field(&Frame::encoded_image,
                AllOf(Field(&EncodedImage::_encodedWidth, codec_settings.width),
                      Field(&EncodedImage::_encodedHeight,
                            codec_settings.height)))));
}

TEST_P(LibaomAv1EncoderTest, RtpTimestampWrap) {
  std::unique_ptr<VideoEncoder> encoder =
      CreateLibaomAv1Encoder(CreateTestEnvironment());
  VideoCodec codec_settings = DefaultCodecSettings();
  codec_settings.SetScalabilityMode(ScalabilityMode::kL1T1);
  ASSERT_EQ(encoder->InitEncode(&codec_settings, DefaultEncoderSettings()),
            WEBRTC_VIDEO_CODEC_OK);

  VideoEncoder::RateControlParameters rate_parameters;
  rate_parameters.framerate_fps = 30;
  rate_parameters.bitrate.SetBitrate(/*spatial_index=*/0, 0, 300'000);
  encoder->SetRates(rate_parameters);

  std::vector<EncodedVideoFrameProducer::EncodedFrame> encoded_frames =
      EncodedVideoFrameProducer(*encoder)
          .SetNumInputFrames(2)
          .SetRtpTimestamp(std::numeric_limits<uint32_t>::max())
          .Encode();
  ASSERT_THAT(encoded_frames, SizeIs(2));
  EXPECT_THAT(encoded_frames[0].encoded_image.frame_type(),
              Eq(VideoFrameType::kVideoFrameKey));
  EXPECT_THAT(encoded_frames[1].encoded_image.frame_type(),
              Eq(VideoFrameType::kVideoFrameDelta));
}

TEST_P(LibaomAv1EncoderTest, TestPresentationTimestamp) {
  std::unique_ptr<VideoEncoder> encoder =
      CreateLibaomAv1Encoder(CreateTestEnvironment());
  const Timestamp presentation_timestamp = Timestamp::Micros(2000);
  VideoCodec codec_settings = DefaultCodecSettings();
  codec_settings.SetScalabilityMode(ScalabilityMode::kL2T1);
  ASSERT_EQ(encoder->InitEncode(&codec_settings, DefaultEncoderSettings()),
            WEBRTC_VIDEO_CODEC_OK);

  VideoEncoder::RateControlParameters rate_parameters;
  rate_parameters.framerate_fps = 30;
  rate_parameters.bitrate.SetBitrate(/*spatial_index=*/0, /*temporal_index=*/0,
                                     300'000);
  rate_parameters.bitrate.SetBitrate(/*spatial_index=*/1, /*temporal_index=*/0,
                                     300'000);
  encoder->SetRates(rate_parameters);

  std::vector<EncodedVideoFrameProducer::EncodedFrame> encoded_frames =
      EncodedVideoFrameProducer(*encoder)
          .SetNumInputFrames(1)
          .SetPresentationTimestamp(presentation_timestamp)
          .Encode();
  ASSERT_THAT(encoded_frames, SizeIs(2));
  ASSERT_TRUE(
      encoded_frames[0].encoded_image.PresentationTimestamp().has_value());
  ASSERT_TRUE(
      encoded_frames[1].encoded_image.PresentationTimestamp().has_value());
  EXPECT_EQ(encoded_frames[0].encoded_image.PresentationTimestamp()->us(),
            presentation_timestamp.us());
  EXPECT_EQ(encoded_frames[1].encoded_image.PresentationTimestamp()->us(),
            presentation_timestamp.us());
}

TEST_P(LibaomAv1EncoderTest, AdheresToTargetBitrateDespiteUnevenFrameTiming) {
  std::unique_ptr<VideoEncoder> encoder =
      CreateLibaomAv1Encoder(CreateTestEnvironment());
  VideoCodec codec_settings = DefaultCodecSettings();
  codec_settings.SetScalabilityMode(ScalabilityMode::kL1T1);
  codec_settings.startBitrate = 300;  // kbps
  codec_settings.width = 320;
  codec_settings.height = 180;
  ASSERT_EQ(encoder->InitEncode(&codec_settings, DefaultEncoderSettings()),
            WEBRTC_VIDEO_CODEC_OK);

  const int kFps = 30;
  const int kTargetBitrateBps = codec_settings.startBitrate * 1000;
  VideoEncoder::RateControlParameters rate_parameters;
  rate_parameters.framerate_fps = kFps;
  rate_parameters.bitrate.SetBitrate(/*spatial_index=*/0, 0, kTargetBitrateBps);
  encoder->SetRates(rate_parameters);

  class EncoderCallback : public EncodedImageCallback {
   public:
    EncoderCallback() = default;
    DataSize BytesEncoded() const { return bytes_encoded_; }

   private:
    Result OnEncodedImage(
        const EncodedImage& encoded_image,
        const CodecSpecificInfo* /* codec_specific_info */) override {
      bytes_encoded_ += DataSize::Bytes(encoded_image.size());
      return Result(Result::Error::OK);
    }
    void OnFrameDropped(uint32_t rtp_timestamp,
                        int spatial_id,
                        bool is_end_of_temporal_unit) override {}

    DataSize bytes_encoded_ = DataSize::Zero();
  } callback;
  encoder->RegisterEncodeCompleteCallback(&callback);

  // Insert frames with too low rtp timestamp delta compared to what is expected
  // based on the framerate, then insert on with 2x the delta it should - making
  // the average correct.
  const uint32_t kHighTimestampDelta =
      static_cast<uint32_t>((90000.0 / kFps) * 2 + 0.5);
  const uint32_t kLowTimestampDelta =
      static_cast<uint32_t>((90000.0 - kHighTimestampDelta) / (kFps - 1));

  std::unique_ptr<test::FrameGeneratorInterface> frame_buffer_generator =
      test::CreateSquareFrameGenerator(
          codec_settings.width, codec_settings.height,
          test::FrameGeneratorInterface::OutputType::kI420, /*num_squares=*/20);

  uint32_t rtp_timestamp = 1000;
  std::vector<VideoFrameType> frame_types = {VideoFrameType::kVideoFrameKey};

  const int kRunTimeSeconds = 3;
  for (int i = 0; i < kRunTimeSeconds; ++i) {
    for (int j = 0; j < kFps; ++j) {
      if (j < kFps - 1) {
        rtp_timestamp += kLowTimestampDelta;
      } else {
        rtp_timestamp += kHighTimestampDelta;
      }
      VideoFrame frame = VideoFrame::Builder()
                             .set_video_frame_buffer(
                                 frame_buffer_generator->NextFrame().buffer)
                             .set_rtp_timestamp(rtp_timestamp)
                             .build();

      RTC_CHECK_EQ(encoder->Encode(frame, &frame_types), WEBRTC_VIDEO_CODEC_OK);
      frame_types[0] = VideoFrameType::kVideoFrameDelta;
    }
  }

  // Expect produced bitrate to match, to within 10%.
  // This catches an issue that was seen when real frame timestamps with jitter
  // was used. It resulted in the overall produced bitrate to be overshot by
  // ~30% even though the averages should have been ok.
  EXPECT_NEAR(
      (callback.BytesEncoded() / TimeDelta::Seconds(kRunTimeSeconds)).bps(),
      kTargetBitrateBps, kTargetBitrateBps / 10);
}

TEST_P(LibaomAv1EncoderTest, DisableAutomaticResize) {
  std::unique_ptr<VideoEncoder> encoder =
      CreateLibaomAv1Encoder(CreateTestEnvironment());
  ASSERT_TRUE(encoder);
  VideoCodec codec_settings = DefaultCodecSettings();
  codec_settings.AV1()->automatic_resize_on = false;
  EXPECT_EQ(encoder->InitEncode(&codec_settings, DefaultEncoderSettings()),
            WEBRTC_VIDEO_CODEC_OK);
  EXPECT_EQ(encoder->GetEncoderInfo().scaling_settings.thresholds,
            std::nullopt);
}

TEST_P(LibaomAv1EncoderTest, PostEncodeFrameDrop) {
  // To trigger post-encode frame drop, encode a frame of a high complexity
  // using a medium bitrate, then reduce the bitrate and encode the same frame
  // again.
  // Using a medium bitrate for the first frame prevents quality and QP
  // saturation. Encoding the same content twice prevents scene change
  // detection. The second frame overshoots RC buffer and provokes post-encode
  // drop.
  VideoFrame input_frame =
      VideoFrame::Builder()
          .set_video_frame_buffer(
              test::CreateYuvFrameReader(
                  test::ResourcePath("photo_1850_1110", "yuv"),
                  {.width = 1850, .height = 1110})
                  ->PullFrame())
          .build();

  VideoBitrateAllocation allocation;
  allocation.SetBitrate(/*spatial_index=*/0, /*temporal_index=*/0,
                        /*bitrate_bps=*/10000000);
  std::unique_ptr<VideoEncoder> encoder =
      CreateLibaomAv1Encoder(CreateTestEnvironment());
  VideoCodec codec_settings = DefaultCodecSettings();
  codec_settings.width = input_frame.width();
  codec_settings.height = input_frame.height();
  codec_settings.startBitrate = allocation.get_sum_kbps();
  codec_settings.SetFrameDropEnabled(true);
  codec_settings.SetScalabilityMode(ScalabilityMode::kL1T1);
  ASSERT_EQ(encoder->InitEncode(&codec_settings, DefaultEncoderSettings()),
            WEBRTC_VIDEO_CODEC_OK);
  encoder->SetRates(VideoEncoder::RateControlParameters(
      allocation, codec_settings.maxFramerate));

  class EncoderCallback : public EncodedImageCallback {
   public:
    EncoderCallback() = default;
    int frames_encoded() const { return frames_encoded_; }

   private:
    Result OnEncodedImage(
        const EncodedImage& encoded_image,
        const CodecSpecificInfo* /* codec_specific_info */) override {
      frames_encoded_++;
      return Result(Result::Error::OK);
    }
    void OnFrameDropped(uint32_t rtp_timestamp,
                        int spatial_id,
                        bool is_end_of_temporal_unit) override {}

    int frames_encoded_ = 0;
  } callback;
  encoder->RegisterEncodeCompleteCallback(&callback);

  input_frame.set_rtp_timestamp(1 * kVideoPayloadTypeFrequency /
                                codec_settings.maxFramerate);
  RTC_CHECK_EQ(encoder->Encode(input_frame, /*frame_types=*/nullptr),
               WEBRTC_VIDEO_CODEC_OK);

  allocation.SetBitrate(/*spatial_index=*/0, /*temporal_index=*/0,
                        /*bitrate_bps=*/1000);
  encoder->SetRates(VideoEncoder::RateControlParameters(
      allocation, codec_settings.maxFramerate));

  input_frame.set_rtp_timestamp(2 * kVideoPayloadTypeFrequency /
                                codec_settings.maxFramerate);
  RTC_CHECK_EQ(encoder->Encode(input_frame, /*frame_types=*/nullptr),
               WEBRTC_VIDEO_CODEC_OK);
  RTC_CHECK_EQ(callback.frames_encoded(), 1);
}

TEST_P(LibaomAv1EncoderTest, EnableDisableSpatialLayersWithSvcController) {
  constexpr int kNumSpatialLayers = 3;
  constexpr int kNumTemporalLayers = 1;
  constexpr size_t kWidth = 1280;
  constexpr size_t kHeight = 720;

  // Configure encoder to produce 3 spatial layers. Encode frames of layer 0
  // then enable layer 1 and encode more frames and so on.
  // Then disable layers one by one in the same way.
  // Note: bit rate allocation is high to avoid frame dropping due to rate
  // control, the encoder should always produce a frame. A dropped
  // frame indicates a problem and the test will fail.
  std::unique_ptr<VideoEncoder> encoder =
      CreateLibaomAv1Encoder(CreateTestEnvironment());
  VideoCodec codec_settings = HDCodecSettings();
  SetAv1SvcConfig(codec_settings, kNumTemporalLayers, kNumSpatialLayers);
  codec_settings.SetFrameDropEnabled(true);
  EXPECT_EQ(encoder->InitEncode(&codec_settings, DefaultEncoderSettings()),
            WEBRTC_VIDEO_CODEC_OK);

  EncodedVideoFrameProducer producer(*encoder);
  producer.SetResolution({kWidth, kHeight});

  VideoBitrateAllocation bitrate_allocation;

  // Set all layers active for initial allocation.
  for (size_t sl_idx = 0; sl_idx < kNumSpatialLayers; ++sl_idx) {
    // Allocate high bit rate to avoid frame dropping due to rate control.
    bitrate_allocation.SetBitrate(
        sl_idx, 0,
        codec_settings.spatialLayers[sl_idx].targetBitrate * 1000 * 2);
  }

  encoder->SetRates(VideoEncoder::RateControlParameters(
      bitrate_allocation, codec_settings.maxFramerate));

  // Encode a key frame to validate all other frames are delta frames.
  std::vector<EncodedVideoFrameProducer::EncodedFrame> frames =
      producer.SetNumInputFrames(1).Encode();
  ASSERT_THAT(frames, Not(IsEmpty()));
  EXPECT_TRUE(frames[0].codec_specific_info.template_structure);

  constexpr size_t kNumFramesToEncode = 5;

  // Disable layers one by one.
  for (int sl_idx = kNumSpatialLayers - 1; sl_idx > 0; --sl_idx) {
    bitrate_allocation.SetBitrate(sl_idx, 0, 0);
    encoder->SetRates(VideoEncoder::RateControlParameters(
        bitrate_allocation, codec_settings.maxFramerate));

    frames = producer.SetNumInputFrames(kNumFramesToEncode).Encode();
    // With `sl_idx` spatial layer disabled, there are `sl_idx` spatial layers
    // left.
    ASSERT_THAT(frames, SizeIs(kNumFramesToEncode * sl_idx));
    for (size_t i = 0; i < frames.size(); ++i) {
      EXPECT_TRUE(frames[i].codec_specific_info.generic_frame_info);
      EXPECT_FALSE(frames[i].codec_specific_info.template_structure);
    }
  }

  // Enable layers back one by one.
  for (size_t sl_idx = 1; sl_idx < kNumSpatialLayers; ++sl_idx) {
    // Allocate high bit rate to avoid frame dropping due to rate control.
    bitrate_allocation.SetBitrate(
        sl_idx, 0,
        codec_settings.spatialLayers[sl_idx].targetBitrate * 1000 * 2);
    encoder->SetRates(VideoEncoder::RateControlParameters(
        bitrate_allocation, codec_settings.maxFramerate));

    frames = producer.SetNumInputFrames(kNumFramesToEncode).Encode();
    // With (sl_idx+1) spatial layers expect (sl_idx+1) frames per input frame.
    ASSERT_THAT(frames, SizeIs(kNumFramesToEncode * (sl_idx + 1)));
    // Only the first frame after enabling the layer must be a keyframe.
    EXPECT_TRUE(frames[0].codec_specific_info.generic_frame_info);
    EXPECT_TRUE(frames[0].codec_specific_info.template_structure);
    for (size_t i = 1; i < frames.size(); ++i) {
      EXPECT_TRUE(frames[i].codec_specific_info.generic_frame_info);
      EXPECT_FALSE(frames[i].codec_specific_info.template_structure);
    }
  }
}

TEST_P(LibaomAv1EncoderTest, L1T2RepeatFrame) {
  std::unique_ptr<VideoEncoder> encoder =
      CreateLibaomAv1Encoder(CreateTestEnvironment());
  VideoCodec codec_settings = DefaultCodecSettings();
  codec_settings.SetScalabilityMode(ScalabilityMode::kL1T2);
  ASSERT_EQ(encoder->InitEncode(&codec_settings, DefaultEncoderSettings()),
            WEBRTC_VIDEO_CODEC_OK);

  VideoEncoder::RateControlParameters rate_parameters;
  rate_parameters.framerate_fps = 30;
  rate_parameters.bitrate.SetBitrate(0, 0, 150'000);
  rate_parameters.bitrate.SetBitrate(0, 1, 150'000);
  encoder->SetRates(rate_parameters);

  MockEncodedImageCallback callback;
  encoder->RegisterEncodeCompleteCallback(&callback);

  auto frame_generator = test::CreateSquareFrameGenerator(
      codec_settings.width, codec_settings.height,
      test::FrameGeneratorInterface::OutputType::kI420, std::nullopt);

  // Encode a key frame.
  VideoFrame key_frame =
      VideoFrame::Builder()
          .set_video_frame_buffer(frame_generator->NextFrame().buffer)
          .set_rtp_timestamp(1000)
          .build();
  std::vector<VideoFrameType> key_frame_types = {
      VideoFrameType::kVideoFrameKey};
  EXPECT_CALL(callback,
              OnEncodedImage(Property(&EncodedImage::TemporalIndex, Eq(0)), _))
      .WillOnce(Return(EncodedImageCallback::Result(
          EncodedImageCallback::Result::Error::OK)));
  ASSERT_EQ(encoder->Encode(key_frame, &key_frame_types),
            WEBRTC_VIDEO_CODEC_OK);
  testing::Mock::VerifyAndClearExpectations(&callback);

  // Encode 4 delta frames, marked as repeat.
  const int expected_frames = drop_repeat_frames_on_enhancement_layers_ ? 2 : 4;
  EXPECT_CALL(callback, OnEncodedImage)
      .Times(expected_frames)
      .WillRepeatedly(
          [&](const EncodedImage& image, const CodecSpecificInfo* info) {
            if (drop_repeat_frames_on_enhancement_layers_) {
              EXPECT_EQ(image.TemporalIndex(), 0);
            }
            return EncodedImageCallback::Result(
                EncodedImageCallback::Result::Error::OK);
          });

  for (int i = 0; i < 4; ++i) {
    VideoFrame delta_frame =
        VideoFrame::Builder()
            .set_video_frame_buffer(frame_generator->NextFrame().buffer)
            .set_rtp_timestamp(key_frame.rtp_timestamp() + (i + 1) * 3000)
            .set_is_repeat_frame(true)
            .build();
    std::vector<VideoFrameType> delta_frame_types = {
        VideoFrameType::kVideoFrameDelta};
    ASSERT_EQ(encoder->Encode(delta_frame, &delta_frame_types),
              WEBRTC_VIDEO_CODEC_OK);
  }
}

TEST_P(LibaomAv1EncoderTest, L1T2RepeatFrameNotDroppedIfDeltaTooLarge) {
  if (!drop_repeat_frames_on_enhancement_layers_) {
    GTEST_SKIP() << "Test only relevant when drop feature is enabled";
  }

  const Environment env = CreateTestEnvironment();
  std::unique_ptr<VideoEncoder> encoder = CreateLibaomAv1Encoder(env);
  VideoCodec codec_settings = DefaultCodecSettings();
  codec_settings.SetScalabilityMode(ScalabilityMode::kL1T2);
  ASSERT_EQ(encoder->InitEncode(&codec_settings, DefaultEncoderSettings()),
            WEBRTC_VIDEO_CODEC_OK);

  VideoEncoder::RateControlParameters rate_parameters;
  rate_parameters.framerate_fps = 30;
  rate_parameters.bitrate.SetBitrate(0, 0, 150'000);
  rate_parameters.bitrate.SetBitrate(0, 1, 150'000);
  encoder->SetRates(rate_parameters);

  MockEncodedImageCallback callback;
  encoder->RegisterEncodeCompleteCallback(&callback);

  auto frame_generator = test::CreateSquareFrameGenerator(
      codec_settings.width, codec_settings.height,
      test::FrameGeneratorInterface::OutputType::kI420, std::nullopt);

  // Encode a key frame.
  VideoFrame key_frame =
      VideoFrame::Builder()
          .set_video_frame_buffer(frame_generator->NextFrame().buffer)
          .set_rtp_timestamp(1000)
          .build();
  std::vector<VideoFrameType> key_frame_types = {
      VideoFrameType::kVideoFrameKey};
  EXPECT_CALL(callback, OnEncodedImage)
      .Times(1)
      .WillOnce([](const EncodedImage& image, const CodecSpecificInfo* info) {
        return EncodedImageCallback::Result(
            EncodedImageCallback::Result::Error::OK);
      });
  ASSERT_EQ(encoder->Encode(key_frame, &key_frame_types),
            WEBRTC_VIDEO_CODEC_OK);
  testing::Mock::VerifyAndClearExpectations(&callback);

  // Encode a repeat delta frame with a small gap - should be dropped.
  EXPECT_CALL(callback, OnEncodedImage).Times(0);
  VideoFrame delta_frame_drop =
      VideoFrame::Builder()
          .set_video_frame_buffer(frame_generator->NextFrame().buffer)
          .set_rtp_timestamp(key_frame.rtp_timestamp() + 3000)
          .set_is_repeat_frame(true)
          .build();
  std::vector<VideoFrameType> delta_frame_types = {
      VideoFrameType::kVideoFrameDelta};
  ASSERT_EQ(encoder->Encode(delta_frame_drop, &delta_frame_types),
            WEBRTC_VIDEO_CODEC_OK);
  testing::Mock::VerifyAndClearExpectations(&callback);

  // Encode a repeat delta frame with a large gap - should NOT be dropped.
  EXPECT_CALL(callback, OnEncodedImage)
      .Times(1)
      .WillOnce([](const EncodedImage& image, const CodecSpecificInfo* info) {
        return EncodedImageCallback::Result(
            EncodedImageCallback::Result::Error::OK);
      });
  VideoFrame delta_frame_keep =
      VideoFrame::Builder()
          .set_video_frame_buffer(frame_generator->NextFrame().buffer)
          .set_rtp_timestamp(key_frame.rtp_timestamp() + 93000)
          .set_is_repeat_frame(true)
          .build();
  ASSERT_EQ(encoder->Encode(delta_frame_keep, &delta_frame_types),
            WEBRTC_VIDEO_CODEC_OK);
}

TEST_P(LibaomAv1EncoderTest, TriggerFrameDropOnBaseLayer) {
  std::unique_ptr<VideoEncoder> encoder =
      CreateLibaomAv1Encoder(CreateTestEnvironment());
  VideoCodec codec_settings = DefaultCodecSettings();
  codec_settings.width = 640;
  codec_settings.height = 360;
  codec_settings.SetScalabilityMode(ScalabilityMode::kL2T1);
  codec_settings.SetFrameDropEnabled(true);
  ASSERT_EQ(encoder->InitEncode(&codec_settings, DefaultEncoderSettings()),
            WEBRTC_VIDEO_CODEC_OK);

  MockEncodedImageCallback callback;
  encoder->RegisterEncodeCompleteCallback(&callback);

  VideoEncoder::RateControlParameters rate_parameters;
  rate_parameters.framerate_fps = 30;
  // Set sufficient bitrate for both layers initially.
  // Layer 0: 50 kbps.
  rate_parameters.bitrate.SetBitrate(0, 0, 50000);
  // Layer 1: 50 kbps.
  rate_parameters.bitrate.SetBitrate(1, 0, 50000);
  encoder->SetRates(rate_parameters);

  auto frame_generator = test::CreateSquareFrameGenerator(
      codec_settings.width, codec_settings.height,
      test::FrameGeneratorInterface::OutputType::kI420, std::nullopt);

  // Encode a keyframe first. Both layers should be encoded.
  EXPECT_CALL(
      callback,
      OnEncodedImage(
          AllOf(Property(&EncodedImage::SpatialIndex, Eq(0)),
                Property(&EncodedImage::is_end_of_temporal_unit, Eq(false))),
          Pointee(Field(&CodecSpecificInfo::end_of_picture, Eq(false)))))
      .WillOnce(Return(EncodedImageCallback::Result(
          EncodedImageCallback::Result::Error::OK)));
  EXPECT_CALL(
      callback,
      OnEncodedImage(
          AllOf(Property(&EncodedImage::SpatialIndex, Eq(1)),
                Property(&EncodedImage::is_end_of_temporal_unit, Eq(true))),
          Pointee(Field(&CodecSpecificInfo::end_of_picture, Eq(true)))))
      .WillOnce(Return(EncodedImageCallback::Result(
          EncodedImageCallback::Result::Error::OK)));

  VideoFrame key_frame =
      VideoFrame::Builder()
          .set_video_frame_buffer(frame_generator->NextFrame().buffer)
          .set_rtp_timestamp(1000)
          .build();

  std::vector<VideoFrameType> key_frame_types = {
      VideoFrameType::kVideoFrameKey};
  ASSERT_EQ(encoder->Encode(key_frame, &key_frame_types),
            WEBRTC_VIDEO_CODEC_OK);

  // Now lower Layer 1 bitrate to force a drop.
  // Layer 1: 0 kbps (force drop).
  rate_parameters.bitrate.SetBitrate(1, 0, 0);
  encoder->SetRates(rate_parameters);

  // Expect OnEncodedImage for spatial layer 0.
  // Since L1 is dropped, L0 is the last encoded frame, so
  // is_end_of_temporal_unit should be true.
  EXPECT_CALL(
      callback,
      OnEncodedImage(
          AllOf(Property(&EncodedImage::SpatialIndex, Eq(0)),
                Property(&EncodedImage::is_end_of_temporal_unit, Eq(false))),
          Pointee(Field(&CodecSpecificInfo::end_of_picture, Eq(true)))))
      .WillOnce(Return(EncodedImageCallback::Result(
          EncodedImageCallback::Result::Error::OK)));

  // Expect OnFrameDropped for spatial layer 1.
  EXPECT_CALL(callback, OnFrameDropped(_, /*spatial_id=*/1,
                                       /*is_end_of_temporal_unit=*/true));

  VideoFrame delta_frame =
      VideoFrame::Builder()
          .set_video_frame_buffer(frame_generator->NextFrame().buffer)
          .set_rtp_timestamp(2000)
          .build();

  std::vector<VideoFrameType> delta_frame_types = {
      VideoFrameType::kVideoFrameDelta};
  ASSERT_EQ(encoder->Encode(delta_frame, &delta_frame_types),
            WEBRTC_VIDEO_CODEC_OK);
}

TEST_P(LibaomAv1EncoderTest, TriggerFrameDropOnLayer0WhileLayer1Encoded) {
  std::unique_ptr<VideoEncoder> encoder =
      CreateLibaomAv1Encoder(CreateTestEnvironment());
  VideoCodec codec_settings = DefaultCodecSettings();
  codec_settings.SetScalabilityMode(ScalabilityMode::kS2T1);
  codec_settings.SetFrameDropEnabled(true);
  ASSERT_EQ(encoder->InitEncode(&codec_settings, DefaultEncoderSettings()),
            WEBRTC_VIDEO_CODEC_OK);

  MockEncodedImageCallback callback;
  encoder->RegisterEncodeCompleteCallback(&callback);

  VideoEncoder::RateControlParameters rate_parameters;
  rate_parameters.framerate_fps = 30;
  // Set moderate bitrate for both layers initially to prevent excessive buffer
  // accumulation, but ensure it's enough to encode.
  // Layer 0: 30 kbps.
  rate_parameters.bitrate.SetBitrate(0, 0, 30000);
  // Layer 1: High bitrate.
  rate_parameters.bitrate.SetBitrate(1, 0, 1000000);
  encoder->SetRates(rate_parameters);

  auto frame_generator = test::CreateSquareFrameGenerator(
      codec_settings.width, codec_settings.height,
      test::FrameGeneratorInterface::OutputType::kI420, std::nullopt);

  // Encode a keyframe first. Both layers should be encoded.
  EXPECT_CALL(
      callback,
      OnEncodedImage(
          AllOf(Property(&EncodedImage::SpatialIndex, Eq(0)),
                Property(&EncodedImage::is_end_of_temporal_unit, Eq(false))),
          Pointee(Field(&CodecSpecificInfo::end_of_picture, Eq(false)))))
      .WillOnce(Return(EncodedImageCallback::Result(
          EncodedImageCallback::Result::Error::OK)));
  EXPECT_CALL(
      callback,
      OnEncodedImage(
          AllOf(Property(&EncodedImage::SpatialIndex, Eq(1)),
                Property(&EncodedImage::is_end_of_temporal_unit, Eq(true))),
          Pointee(Field(&CodecSpecificInfo::end_of_picture, Eq(true)))))
      .WillOnce(Return(EncodedImageCallback::Result(
          EncodedImageCallback::Result::Error::OK)));

  VideoFrame key_frame =
      VideoFrame::Builder()
          .set_video_frame_buffer(frame_generator->NextFrame().buffer)
          .set_rtp_timestamp(1000)
          .build();

  std::vector<VideoFrameType> key_frame_types = {
      VideoFrameType::kVideoFrameKey};
  ASSERT_EQ(encoder->Encode(key_frame, &key_frame_types),
            WEBRTC_VIDEO_CODEC_OK);

  // Now lower Layer 0 bitrate to force a drop.
  // Layer 0: 1 kbps.
  rate_parameters.bitrate.SetBitrate(0, 0, 1000);
  encoder->SetRates(rate_parameters);

  // Expect OnFrameDropped for spatial layer 0.
  EXPECT_CALL(callback, OnFrameDropped(_, /*spatial_id=*/0,
                                       /*is_end_of_temporal_unit=*/false));
  // Expect OnEncodedImage for spatial layer 1.
  EXPECT_CALL(
      callback,
      OnEncodedImage(
          AllOf(Property(&EncodedImage::SpatialIndex, Eq(1)),
                Property(&EncodedImage::is_end_of_temporal_unit, Eq(true))),
          Pointee(Field(&CodecSpecificInfo::end_of_picture, Eq(true)))))
      .WillOnce(Return(EncodedImageCallback::Result(
          EncodedImageCallback::Result::Error::OK)));

  VideoFrame delta_frame =
      VideoFrame::Builder()
          .set_video_frame_buffer(frame_generator->NextFrame().buffer)
          .set_rtp_timestamp(2000)
          .build();

  std::vector<VideoFrameType> delta_frame_types = {
      VideoFrameType::kVideoFrameDelta};
  ASSERT_EQ(encoder->Encode(delta_frame, &delta_frame_types),
            WEBRTC_VIDEO_CODEC_OK);
}

class LibaomAv1EncoderComplexityTest
    : public ::testing::TestWithParam<VideoEncoder::ComplexityControlMode> {};

TEST_P(LibaomAv1EncoderComplexityTest, SetComplexityIncreasesQuality) {
  constexpr int kWidth = 352;
  constexpr int kHeight = 288;
  constexpr int kNumSourceFrames = 100;

  std::unique_ptr<test::FrameReader> frame_reader =
      test::CreateYuvFrameReader(test::ResourcePath("foreman_cif", "yuv"),
                                 {.width = kWidth, .height = kHeight});
  std::vector<scoped_refptr<I420Buffer>> input_frames;
  for (int i = 0; i < kNumSourceFrames; ++i) {
    input_frames.push_back(frame_reader->PullFrame());
  }

  class DecodedCallback : public DecodedImageCallback {
   public:
    int32_t Decoded(VideoFrame& decoded_image) override {
      last_decoded_frame_ = decoded_image;
      return 0;
    }
    const std::optional<VideoFrame>& last_decoded_frame() const {
      return last_decoded_frame_;
    }

   private:
    std::optional<VideoFrame> last_decoded_frame_;
  };

  class EncodedCallback : public EncodedImageCallback {
   public:
    Result OnEncodedImage(
        const EncodedImage& encoded_image,
        const CodecSpecificInfo* /*codec_specific_info*/) override {
      last_encoded_image_ = encoded_image;
      return Result(Result::Error::OK);
    }
    void OnFrameDropped(uint32_t /*rtp_timestamp*/,
                        int /*spatial_id*/,
                        bool /*is_end_of_temporal_unit*/) override {}
    const std::optional<EncodedImage>& last_encoded_image() const {
      return last_encoded_image_;
    }

   private:
    std::optional<EncodedImage> last_encoded_image_;
  };

  // In `kVariable` mode, `EncoderSpeedController` starts at a fast speed and
  // steps down to slower speeds over time (requiring at least 6 frames per step
  // and at least 1000ms between PSNR probes). Using a low framerate (5 fps =
  // 200ms per frame) and a constrained bitrate (40 kbps across L1T2) ensures
  // that PSNR probes see enough quality gain (and QP stays high enough) to
  // unlock all effort levels down to speed 5 within 95 frames. In `kConstant`
  // mode, speeds are applied immediately, so 10 frames are sufficient.
  const VideoEncoder::ComplexityControlMode mode = GetParam();
  const int num_frames = 100;
  constexpr int kFramerateFps = 5;
  constexpr int kBitrateKbps = 40;
  SimulatedClock clock(Timestamp::Seconds(1000));
  const Environment env = CreateTestEnvironment({.time = &clock});
  std::optional<double> last_avg_psnr;
  using enum VideoCodecComplexity;
  for (VideoCodecComplexity complexity :
       {kComplexityLow, kComplexityNormal, kComplexityHigh, kComplexityHigher,
        kComplexityMax}) {
    std::unique_ptr<VideoEncoder> encoder = CreateLibaomAv1Encoder(env);
    VideoCodec codec_settings = DefaultCodecSettings();
    codec_settings.width = kWidth;
    codec_settings.height = kHeight;
    codec_settings.maxFramerate = kFramerateFps;
    codec_settings.startBitrate = kBitrateKbps;
    codec_settings.maxBitrate = kBitrateKbps;
    codec_settings.SetFrameDropEnabled(false);
    codec_settings.SetScalabilityMode(ScalabilityMode::kL1T2);
    ASSERT_EQ(encoder->InitEncode(&codec_settings, DefaultEncoderSettings()),
              WEBRTC_VIDEO_CODEC_OK);

    EncodedCallback encoded_callback;
    encoder->RegisterEncodeCompleteCallback(&encoded_callback);

    VideoEncoder::RateControlParameters rate_parameters;
    rate_parameters.framerate_fps = codec_settings.maxFramerate;
    rate_parameters.bitrate.SetBitrate(/*spatial_index=*/0, 0,
                                       kBitrateKbps * 600);
    rate_parameters.bitrate.SetBitrate(/*spatial_index=*/0, 1,
                                       kBitrateKbps * 400);
    encoder->SetRates(rate_parameters);
    encoder->SetComplexity({{complexity}}, mode);

    std::unique_ptr<VideoDecoder> decoder = CreateDav1dDecoder(env);
    ASSERT_TRUE(decoder->Configure({}));
    DecodedCallback decoded_callback;
    ASSERT_EQ(decoder->RegisterDecodeCompleteCallback(&decoded_callback),
              WEBRTC_VIDEO_CODEC_OK);

    double psnr_sum = 0;
    for (int i = 0; i < num_frames; ++i) {
      const scoped_refptr<I420Buffer>& buffer =
          input_frames[i % input_frames.size()];
      VideoFrame input_frame =
          VideoFrame::Builder()
              .set_video_frame_buffer(buffer)
              .set_rtp_timestamp((i + 1) * kVideoPayloadTypeFrequency /
                                 codec_settings.maxFramerate)
              .set_timestamp_us(clock.TimeInMicroseconds())
              .build();
      std::vector<VideoFrameType> frame_types = {
          i == 0 ? VideoFrameType::kVideoFrameKey
                 : VideoFrameType::kVideoFrameDelta};
      ASSERT_EQ(encoder->Encode(input_frame, &frame_types),
                WEBRTC_VIDEO_CODEC_OK);
      clock.AdvanceTime(TimeDelta::Seconds(1) / codec_settings.maxFramerate);
      ASSERT_TRUE(encoded_callback.last_encoded_image().has_value());
      ASSERT_EQ(decoder->Decode(*encoded_callback.last_encoded_image(),
                                /*render_time_ms=*/0),
                WEBRTC_VIDEO_CODEC_OK);
      ASSERT_TRUE(decoded_callback.last_decoded_frame().has_value());
      psnr_sum += I420PSNR(*buffer, *decoded_callback.last_decoded_frame()
                                         ->video_frame_buffer()
                                         ->ToI420());
    }
    const double avg_psnr = psnr_sum / num_frames;
    if (last_avg_psnr.has_value()) {
      EXPECT_THAT(avg_psnr, Gt(*last_avg_psnr))
          << "mode=" << static_cast<int>(mode)
          << " complexity=" << static_cast<int>(complexity);
    }
    last_avg_psnr = avg_psnr;
  }
}

INSTANTIATE_TEST_SUITE_P(
    LibaomAv1EncoderComplexityTests,
    LibaomAv1EncoderComplexityTest,
    Values(VideoEncoder::ComplexityControlMode::kConstant,
           VideoEncoder::ComplexityControlMode::kVariable));

}  // namespace
}  // namespace webrtc
