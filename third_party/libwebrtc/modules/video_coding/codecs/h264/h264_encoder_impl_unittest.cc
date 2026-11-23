/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 *
 */

#include "modules/video_coding/codecs/h264/h264_encoder_impl.h"

#include <cstdint>
#include <optional>

#include "api/make_ref_counted.h"
#include "api/scoped_refptr.h"
#include "api/test/create_frame_generator.h"
#include "api/test/frame_generator_interface.h"
#include "api/test/mock_video_encoder.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_encoder.h"
#include "modules/video_coding/codecs/h264/include/h264_globals.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "test/create_test_environment.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "third_party/openh264/src/codec/api/wels/codec_app_def.h"

using ::testing::AnyNumber;
using ::testing::AtLeast;
using ::testing::Return;

namespace webrtc {

namespace {

constexpr int kMaxPayloadSize = 1024;
constexpr int kNumCores = 1;

constexpr VideoEncoder::Settings kSettings(kNumCores, kMaxPayloadSize);

void SetDefaultSettings(VideoCodec* codec_settings) {
  codec_settings->codecType = kVideoCodecH264;
  codec_settings->maxFramerate = 60;
  codec_settings->width = 640;
  codec_settings->height = 480;
  // If frame dropping is false, we get a warning that bitrate can't
  // be controlled for RC_QUALITY_MODE; RC_BITRATE_MODE and RC_TIMESTAMP_MODE
  codec_settings->SetFrameDropEnabled(true);
  codec_settings->startBitrate = 2000;
  codec_settings->maxBitrate = 4000;
}

TEST(H264EncoderImplTest, CanInitializeWithDefaultParameters) {
  H264EncoderImpl encoder(CreateTestEnvironment(), {});
  VideoCodec codec_settings;
  SetDefaultSettings(&codec_settings);
  EXPECT_EQ(WEBRTC_VIDEO_CODEC_OK,
            encoder.InitEncode(&codec_settings, kSettings));
  EXPECT_EQ(H264PacketizationMode::NonInterleaved,
            encoder.PacketizationModeForTesting());
}

TEST(H264EncoderImplTest, CanInitializeWithNonInterleavedModeExplicitly) {
  H264EncoderImpl encoder(
      CreateTestEnvironment(),
      {.packetization_mode = H264PacketizationMode::NonInterleaved});
  VideoCodec codec_settings;
  SetDefaultSettings(&codec_settings);
  EXPECT_EQ(WEBRTC_VIDEO_CODEC_OK,
            encoder.InitEncode(&codec_settings, kSettings));
  EXPECT_EQ(H264PacketizationMode::NonInterleaved,
            encoder.PacketizationModeForTesting());
}

TEST(H264EncoderImplTest, CanInitializeWithSingleNalUnitModeExplicitly) {
  H264EncoderImpl encoder(
      CreateTestEnvironment(),
      {.packetization_mode = H264PacketizationMode::SingleNalUnit});
  VideoCodec codec_settings;
  SetDefaultSettings(&codec_settings);
  EXPECT_EQ(WEBRTC_VIDEO_CODEC_OK,
            encoder.InitEncode(&codec_settings, kSettings));
  EXPECT_EQ(H264PacketizationMode::SingleNalUnit,
            encoder.PacketizationModeForTesting());
}

TEST(H264EncoderImplTest, OnFrameDropped) {
  H264EncoderImpl encoder(CreateTestEnvironment(), {});
  VideoCodec codec_settings;
  SetDefaultSettings(&codec_settings);
  // Set a very low bitrate to force frame drops.
  codec_settings.startBitrate = 1;
  codec_settings.maxBitrate = 1;

  MockEncodedImageCallback callback;
  EXPECT_EQ(WEBRTC_VIDEO_CODEC_OK,
            encoder.InitEncode(&codec_settings, kSettings));
  EXPECT_EQ(WEBRTC_VIDEO_CODEC_OK,
            encoder.RegisterEncodeCompleteCallback(&callback));

  auto frame_generator = test::CreateSquareFrameGenerator(
      codec_settings.width, codec_settings.height,
      test::FrameGeneratorInterface::OutputType::kI420,
      /*num_squares=*/std::nullopt);

  // We need to encode enough frames to trigger rate control dropping.
  // The exact number might vary, but a loop should catch it.
  const int kNumFramesToEncode = 30;

  EXPECT_CALL(callback, OnEncodedImage)
      .Times(AnyNumber())
      .WillRepeatedly(Return(
          EncodedImageCallback::Result(EncodedImageCallback::Result::OK)));

  EXPECT_CALL(callback, OnFrameDropped)
      .Times(AtLeast(1))
      .WillRepeatedly([&](uint32_t rtp_timestamp, int spatial_id,
                          bool is_end_of_temporal_unit) {
        // Verify arguments match what we expect for a single layer drop.
        EXPECT_EQ(spatial_id, 0);  // H264 encoder usually uses simlucast index
                                   // as spatial_id, or just 0 for single layer.
        EXPECT_TRUE(is_end_of_temporal_unit);
      });

  for (int i = 0; i < kNumFramesToEncode; ++i) {
    VideoFrame frame =
        VideoFrame::Builder()
            .set_video_frame_buffer(frame_generator->NextFrame().buffer)
            .set_rtp_timestamp(i * 90000 / codec_settings.maxFramerate)
            .build();
    encoder.Encode(frame, nullptr);
  }
}

TEST(H264EncoderImplTest, RejectsI420FramesWithUnequalChromaStrides) {
  H264EncoderImpl encoder(CreateTestEnvironment(), {});
  VideoCodec codec_settings;
  SetDefaultSettings(&codec_settings);
  MockEncodedImageCallback callback;
  EXPECT_EQ(WEBRTC_VIDEO_CODEC_OK,
            encoder.InitEncode(&codec_settings, kSettings));
  EXPECT_EQ(WEBRTC_VIDEO_CODEC_OK,
            encoder.RegisterEncodeCompleteCallback(&callback));
  // Create a VideoFrame where the U and V strides are different.
  auto buffer = I420Buffer::Create(
      /*width=*/codec_settings.width,
      /*height=*/codec_settings.height,
      /*stride_y=*/codec_settings.width,
      /*stride_u=*/(codec_settings.width + 1) / 2,
      /*stride_v=*/(codec_settings.width + 1) / 2 + 1);

  VideoFrame frame = VideoFrame::Builder()
                         .set_video_frame_buffer(buffer)
                         .set_rtp_timestamp(0)
                         .build();

  EXPECT_EQ(WEBRTC_VIDEO_CODEC_ENCODER_FAILURE, encoder.Encode(frame, nullptr));
}

TEST(H264EncoderImplTest, RejectsNativeFramesWithUnequalChromaStrides) {
  H264EncoderImpl encoder(CreateTestEnvironment(), {});
  VideoCodec codec_settings;
  SetDefaultSettings(&codec_settings);
  MockEncodedImageCallback callback;
  EXPECT_EQ(WEBRTC_VIDEO_CODEC_OK,
            encoder.InitEncode(&codec_settings, kSettings));
  EXPECT_EQ(WEBRTC_VIDEO_CODEC_OK,
            encoder.RegisterEncodeCompleteCallback(&callback));

  class FakeNativeBuffer : public VideoFrameBuffer {
   public:
    FakeNativeBuffer(int width, int height) : width_(width), height_(height) {}
    Type type() const override { return Type::kNative; }
    int width() const override { return width_; }
    int height() const override { return height_; }
    scoped_refptr<I420BufferInterface> ToI420() override {
      return I420Buffer::Create(width_, height_, width_, (width_ + 1) / 2,
                                (width_ + 1) / 2 + 1);
    }

   private:
    int width_;
    int height_;
  };

  auto buffer = make_ref_counted<FakeNativeBuffer>(codec_settings.width,
                                                   codec_settings.height);

  VideoFrame frame = VideoFrame::Builder()
                         .set_video_frame_buffer(buffer)
                         .set_rtp_timestamp(0)
                         .build();

  EXPECT_EQ(WEBRTC_VIDEO_CODEC_ENCODER_FAILURE, encoder.Encode(frame, nullptr));
}

TEST(H264EncoderImplTest, RejectsTooLargeResolution) {
  H264EncoderImpl encoder(CreateTestEnvironment(), {});
  VideoCodec codec_settings;
  SetDefaultSettings(&codec_settings);

  // Level 5.2 MaxFS is 36864 macroblocks.
  // 4096 x 2304 = 256 x 144 macroblocks = 36864 macroblocks (Allowed).
  codec_settings.width = 4096;
  codec_settings.height = 2304;
  EXPECT_EQ(WEBRTC_VIDEO_CODEC_OK,
            encoder.InitEncode(&codec_settings, kSettings));

  // 4096 x 2320 = 256 x 145 macroblocks = 37120 macroblocks (Too large).
  codec_settings.width = 4096;
  codec_settings.height = 2320;
  EXPECT_EQ(WEBRTC_VIDEO_CODEC_ERR_PARAMETER,
            encoder.InitEncode(&codec_settings, kSettings));
}

TEST(H264EncoderImplTest, RejectsSkewedAspectRatioWidth) {
  H264EncoderImpl encoder(CreateTestEnvironment(), {});
  VideoCodec codec_settings;
  SetDefaultSettings(&codec_settings);

  // Level 5.2 Max width in macroblocks is 543 (8688 pixels).
  // 8688 x 16 = 543 x 1 macroblocks = 543 macroblocks (Allowed).
  codec_settings.width = 8688;
  codec_settings.height = 16;
  EXPECT_EQ(WEBRTC_VIDEO_CODEC_OK,
            encoder.InitEncode(&codec_settings, kSettings));

  // 8704 x 16 = 544 x 1 macroblocks = 544 macroblocks (Too wide).
  codec_settings.width = 8704;
  codec_settings.height = 16;
  EXPECT_EQ(WEBRTC_VIDEO_CODEC_ERR_PARAMETER,
            encoder.InitEncode(&codec_settings, kSettings));
}

TEST(H264EncoderImplTest, RejectsSkewedAspectRatioHeight) {
  H264EncoderImpl encoder(CreateTestEnvironment(), {});
  VideoCodec codec_settings;
  SetDefaultSettings(&codec_settings);

  // Level 5.2 Max height in macroblocks is 543 (8688 pixels).
  // 16 x 8688 = 1 x 543 macroblocks = 543 macroblocks (Allowed).
  codec_settings.width = 16;
  codec_settings.height = 8688;
  EXPECT_EQ(WEBRTC_VIDEO_CODEC_OK,
            encoder.InitEncode(&codec_settings, kSettings));

  // 16 x 8704 = 1 x 544 macroblocks = 544 macroblocks (Too high).
  codec_settings.width = 16;
  codec_settings.height = 8704;
  EXPECT_EQ(WEBRTC_VIDEO_CODEC_ERR_PARAMETER,
            encoder.InitEncode(&codec_settings, kSettings));
}

TEST(H264EncoderImplTest, SetComplexity) {
  H264EncoderImpl encoder(CreateTestEnvironment(), {});
  VideoCodec codec_settings;
  SetDefaultSettings(&codec_settings);
  ASSERT_EQ(WEBRTC_VIDEO_CODEC_OK,
            encoder.InitEncode(&codec_settings, kSettings));

  const VideoCodecComplexity kLow[] = {VideoCodecComplexity::kComplexityLow};
  encoder.SetComplexity(kLow, VideoEncoder::ComplexityControlMode::kConstant);
  EXPECT_EQ(encoder.ComplexityModeForTesting(), LOW_COMPLEXITY);

  const VideoCodecComplexity kNormal[] = {
      VideoCodecComplexity::kComplexityNormal};
  encoder.SetComplexity(kNormal,
                        VideoEncoder::ComplexityControlMode::kConstant);
  EXPECT_EQ(encoder.ComplexityModeForTesting(), LOW_COMPLEXITY);

  const VideoCodecComplexity kHigh[] = {VideoCodecComplexity::kComplexityHigh};
  encoder.SetComplexity(kHigh, VideoEncoder::ComplexityControlMode::kConstant);
  EXPECT_EQ(encoder.ComplexityModeForTesting(), MEDIUM_COMPLEXITY);

  const VideoCodecComplexity kHigher[] = {
      VideoCodecComplexity::kComplexityHigher};
  encoder.SetComplexity(kHigher,
                        VideoEncoder::ComplexityControlMode::kConstant);
  EXPECT_EQ(encoder.ComplexityModeForTesting(), HIGH_COMPLEXITY);

  // Extra elements outside configured stream count should be ignored.
  const VideoCodecComplexity kMaxWithExtra[] = {
      VideoCodecComplexity::kComplexityMax,
      VideoCodecComplexity::kComplexityLow};
  encoder.SetComplexity(kMaxWithExtra,
                        VideoEncoder::ComplexityControlMode::kConstant);
  EXPECT_EQ(encoder.ComplexityModeForTesting(), HIGH_COMPLEXITY);
}

TEST(H264EncoderImplTest, SetComplexitySimulcast) {
  H264EncoderImpl encoder(CreateTestEnvironment(), {});
  VideoCodec codec_settings;
  SetDefaultSettings(&codec_settings);
  codec_settings.numberOfSimulcastStreams = 3;
  codec_settings.simulcastStream[0] = {.width = 160,
                                       .height = 120,
                                       .maxFramerate = 60,
                                       .maxBitrate = 200,
                                       .targetBitrate = 100,
                                       .minBitrate = 30,
                                       .active = true};
  codec_settings.simulcastStream[1] = {.width = 320,
                                       .height = 240,
                                       .maxFramerate = 60,
                                       .maxBitrate = 500,
                                       .targetBitrate = 300,
                                       .minBitrate = 50,
                                       .active = true};
  codec_settings.simulcastStream[2] = {.width = 640,
                                       .height = 480,
                                       .maxFramerate = 60,
                                       .maxBitrate = 2000,
                                       .targetBitrate = 1000,
                                       .minBitrate = 100,
                                       .active = true};
  ASSERT_EQ(WEBRTC_VIDEO_CODEC_OK,
            encoder.InitEncode(&codec_settings, kSettings));

  const VideoCodecComplexity kComplexities[] = {
      VideoCodecComplexity::kComplexityLow,
      VideoCodecComplexity::kComplexityHigh,
      VideoCodecComplexity::kComplexityMax};
  encoder.SetComplexity(kComplexities,
                        VideoEncoder::ComplexityControlMode::kConstant);
  EXPECT_EQ(encoder.ComplexityModeForTesting(0), LOW_COMPLEXITY);
  EXPECT_EQ(encoder.ComplexityModeForTesting(1), MEDIUM_COMPLEXITY);
  EXPECT_EQ(encoder.ComplexityModeForTesting(2), HIGH_COMPLEXITY);
}

}  // anonymous namespace

}  // namespace webrtc
