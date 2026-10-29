/*
 * Copyright 2024 The WebRTC project authors. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license
 * that can be found in the LICENSE file in the root of the source
 * tree. An additional intellectual property rights grant can be found
 * in the file PATENTS.  All contributing project authors may
 * be found in the AUTHORS file in the root of the source tree.
 */

#include "video/corruption_detection/halton_frame_sampler.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "api/make_ref_counted.h"
#include "api/scoped_refptr.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "video/corruption_detection/video_frame_sampler.h"

namespace webrtc {
namespace {

using ::testing::_;
using ::testing::AllOf;
using ::testing::DoubleEq;
using ::testing::DoubleNear;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::Not;

using Coordinates = HaltonFrameSampler::Coordinates;

// Defaults for sampling tests.
constexpr int kDefaultScaledWidth = 4;
constexpr int kDefaultScaledHeight = 4;
constexpr double kDefaultStdDevGaussianBlur = 0.02;

class NullI420ConversionBuffer : public VideoFrameBuffer {
 public:
  Type type() const override { return Type::kNative; }
  int width() const override { return kDefaultScaledWidth; }
  int height() const override { return kDefaultScaledHeight; }
  scoped_refptr<I420BufferInterface> ToI420() override { return nullptr; }
};

#if GTEST_HAS_DEATH_TEST
// Defaults for blurring tests.
constexpr int kDefaultRow = 3;
constexpr int kDefaultColumn = 2;
constexpr double kDefaultStdDev = 1.12;
#endif  // GTEST_HAS_DEATH_TEST

VideoFrame MakeDefaultFrame() {
  // Create an I420 frame of size 4x4.
  const int kDefaultLumaWidth = 4;
  const int kDefaultLumaHeight = 4;
  const int kDefaultChromaWidth = 2;
  const uint8_t kDefaultYContent[16] = {20, 196, 250, 115, 139, 39, 99, 197,
                                        21, 166, 254, 28,  227, 54, 64, 46};
  const uint8_t kDefaultUContent[4] = {156, 203, 36, 128};
  const uint8_t kDefaultVContent[4] = {112, 2, 0, 24};

  return VideoFrame::Builder()
      .set_video_frame_buffer(I420Buffer::Copy(
          kDefaultLumaWidth, kDefaultLumaHeight, kDefaultYContent,
          kDefaultLumaWidth, kDefaultUContent, kDefaultChromaWidth,
          kDefaultVContent, kDefaultChromaWidth))
      .build();
}

VideoFrame MakeFrame(int width, int height, std::span<const uint8_t> data) {
  scoped_refptr<I420Buffer> buffer = I420Buffer::Create(width, height);
  memcpy(buffer->MutableDataY(), data.data(), width * height);
  return VideoFrame::Builder().set_video_frame_buffer(buffer).build();
}

std::vector<Coordinates> MakeDefaultSampleCoordinates() {
  // Coordinates in all planes.
  return {{.row = 0.2, .column = 0.7},
          {.row = 0.5, .column = 0.9},
          {.row = 0.3, .column = 0.7},
          {.row = 0.8, .column = 0.4}};
}

TEST(GaussianFilteringTest, ShouldReturnFilteredValueWhenInputIsValid) {
  const int kWidth = 8;
  const int kHeight = 8;
  const uint8_t kData[kWidth * kHeight] = {
      219, 38,  75,  13,  77,  22,  108, 5,    //
      199, 105, 237, 3,   194, 63,  200, 95,   //
      116, 21,  224, 21,  79,  210, 138, 3,    //
      130, 156, 139, 176, 1,   134, 191, 61,   //
      123, 59,  34,  237, 223, 162, 113, 108,  //
      146, 210, 214, 110, 50,  205, 135, 18,   //
      51,  198, 63,  69,  70,  117, 180, 126,  //
      244, 250, 194, 195, 85,  24,  25,  224};
  // Chosing the point in the middle so all pixels are used.
  const int kRow = 3;
  const int kColumn = 3;
  // Resulting in a filter size of 3 pixels.
  const double kStdDev = 1;

  std::unique_ptr<VideoFrameSampler> sampler =
      VideoFrameSampler::Create(MakeFrame(kWidth, kHeight, kData));
  EXPECT_THAT(GetFilteredElement(*sampler, VideoFrameSampler::ChannelType::Y,
                                 kRow, kColumn, kStdDev),
              DoubleEq(126.45897447350468));
}

#if GTEST_HAS_DEATH_TEST
std::unique_ptr<VideoFrameSampler> MakeDefaultSampler() {
  return VideoFrameSampler::Create(MakeDefaultFrame());
}

TEST(GaussianFilteringTest, ShouldCrashWhenRowIsNegative) {
  EXPECT_DEATH(GetFilteredElement(*MakeDefaultSampler(),
                                  VideoFrameSampler::ChannelType::Y, -1,
                                  kDefaultColumn, kDefaultStdDev),
               _);
}

TEST(GaussianFilteringTest, ShouldCrashWhenRowIsOutOfRange) {
  EXPECT_DEATH(GetFilteredElement(*MakeDefaultSampler(),
                                  VideoFrameSampler::ChannelType::Y, 4,
                                  kDefaultColumn, kDefaultStdDev),
               _);
}

TEST(GaussianFilteringTest, ShouldCrashWhenColumnIsNegative) {
  EXPECT_DEATH(GetFilteredElement(*MakeDefaultSampler(),
                                  VideoFrameSampler::ChannelType::Y,
                                  kDefaultRow, -1, kDefaultStdDev),
               _);
}

TEST(GaussianFilteringTest, ShouldCrashWhenColumnIsOutOfRange) {
  EXPECT_DEATH(GetFilteredElement(*MakeDefaultSampler(),
                                  VideoFrameSampler::ChannelType::Y,
                                  kDefaultRow, 4, kDefaultStdDev),
               _);
}

TEST(GaussianFilteringTest, ShouldCrashWhenStdDevIsNegative) {
  EXPECT_DEATH(GetFilteredElement(*MakeDefaultSampler(),
                                  VideoFrameSampler::ChannelType::Y,
                                  kDefaultRow, kDefaultColumn, -1.0),
               _);
}

TEST(GaussianFilteringTest, RoundingErrorsShouldNotHappen) {
  // These values should force a rounding error.
  constexpr int kWidth = 128;
  constexpr int kHeight = 128;
  constexpr double kStdDev = 40;
  const std::vector<uint8_t> data(kWidth * kHeight, 255);
  std::unique_ptr<VideoFrameSampler> sampler =
      VideoFrameSampler::Create(MakeFrame(kWidth, kHeight, data));

  EXPECT_THAT(GetFilteredElement(*sampler, VideoFrameSampler::ChannelType::Y,
                                 kWidth / 2, kHeight / 2, kStdDev),
              255);
}

TEST(HaltonFrameSamplerTest, FrameIsNotSampledWhenTimestampsAreEqual) {
  HaltonFrameSampler halton_frame_sampler;

  EXPECT_THAT(
      halton_frame_sampler.GetSampleCoordinatesForFrameIfFrameShouldBeSampled(
          /*is_key_frame=*/false, /*rtp_timestamp=*/0, /*num_samples=*/1),
      Not(IsEmpty()));
  EXPECT_DEATH(
      halton_frame_sampler.GetSampleCoordinatesForFrameIfFrameShouldBeSampled(
          /*is_key_frame=*/false, /*rtp_timestamp=*/0, /*num_samples=*/1),
      _);
}

#endif  // GTEST_HAS_DEATH_TEST

TEST(HaltonFrameSamplerGaussianFilteringTest,
     ShouldReturnEmptyListGivenInvalidInputNoCoordinates) {
  EXPECT_THAT(
      GetSampleValuesForFrame(MakeDefaultFrame(), {}, kDefaultScaledWidth,
                              kDefaultScaledHeight, kDefaultStdDevGaussianBlur),
      IsEmpty());
}

TEST(HaltonFrameSamplerGaussianFilteringTest,
     ShouldReturnEmptyListGivenInvalidInputOutOfRangeCoordinates) {
  const std::vector<Coordinates> kSampleCoordinates = {
      {.row = 0.2, .column = 0.7},
      {.row = 0.5, .column = 1.0},
      {.row = 0.3, .column = 0.7},
      {.row = 0.8, .column = 0.4}};

  EXPECT_THAT(GetSampleValuesForFrame(MakeDefaultFrame(), kSampleCoordinates,
                                      kDefaultScaledWidth, kDefaultScaledHeight,
                                      kDefaultStdDevGaussianBlur),
              IsEmpty());
}

TEST(HaltonFrameSamplerGaussianFilteringTest,
     ShouldReturnEmptyListGivenInvalidInputWidthZero) {
  const std::vector<Coordinates> kDefaultSampleCoordinates =
      MakeDefaultSampleCoordinates();

  EXPECT_THAT(
      GetSampleValuesForFrame(MakeDefaultFrame(), kDefaultSampleCoordinates, 0,
                              kDefaultScaledHeight, kDefaultStdDevGaussianBlur),
      IsEmpty());
}

TEST(HaltonFrameSamplerGaussianFilteringTest,
     ShouldReturnEmptyListGivenInvalidInputHeightZero) {
  const std::vector<Coordinates> kDefaultSampleCoordinates =
      MakeDefaultSampleCoordinates();

  EXPECT_THAT(GetSampleValuesForFrame(
                  MakeDefaultFrame(), kDefaultSampleCoordinates,
                  kDefaultScaledWidth, 0, kDefaultStdDevGaussianBlur),
              IsEmpty());
}

TEST(HaltonFrameSamplerGaussianFilteringTest,
     ShouldReturnEmptyListGivenInvalidInputStdDevNegative) {
  const std::vector<Coordinates> kDefaultSampleCoordinates =
      MakeDefaultSampleCoordinates();

  EXPECT_THAT(
      GetSampleValuesForFrame(MakeDefaultFrame(), kDefaultSampleCoordinates,
                              kDefaultScaledWidth, kDefaultScaledHeight, -1.0),
      IsEmpty());
}

TEST(HaltonFrameSamplerGaussianFilteringTest,
     ShouldReturnEmptyListWhenUpscaling) {
  EXPECT_THAT(GetSampleValuesForFrame(MakeDefaultFrame(),
                                      MakeDefaultSampleCoordinates(),
                                      /*scaled_width=*/8, /*scaled_height=*/8,
                                      kDefaultStdDevGaussianBlur),
              IsEmpty());
}

TEST(HaltonFrameSamplerGaussianFilteringTest,
     ShouldReturnEmptyListWhenToI420ConversionFails) {
  VideoFrame frame =
      VideoFrame::Builder()
          .set_video_frame_buffer(make_ref_counted<NullI420ConversionBuffer>())
          .build();

  EXPECT_THAT(GetSampleValuesForFrame(frame, MakeDefaultSampleCoordinates(),
                                      kDefaultScaledWidth, kDefaultScaledHeight,
                                      kDefaultStdDevGaussianBlur),
              IsEmpty());
  EXPECT_THAT(GetSampleValuesForFrame(frame, MakeDefaultSampleCoordinates(),
                                      /*scaled_width=*/2, /*scaled_height=*/2,
                                      kDefaultStdDevGaussianBlur),
              IsEmpty());
}

TEST(HaltonFrameSamplerGaussianFilteringTest,
     ShouldReturnGivenValueWhenNoScalingOrFilteringIsDefined) {
  // 4x4 i420 frame data.
  const int kLumaWidth = 4;
  const int kLumaHeight = 4;
  const int kChromaWidth = 2;
  const uint8_t kYContent[16] = {20, 196, 250, 115, 139, 39, 99, 197,
                                 21, 166, 254, 28,  227, 54, 64, 46};
  const uint8_t kUContent[4] = {156, 203, 36, 128};
  const uint8_t kVContent[4] = {112, 2, 0, 24};
  const scoped_refptr<I420Buffer> kI420Buffer =
      I420Buffer::Copy(kLumaWidth, kLumaHeight, kYContent, kLumaWidth,
                       kUContent, kChromaWidth, kVContent, kChromaWidth);
  VideoFrame frame =
      VideoFrame::Builder().set_video_frame_buffer(kI420Buffer).build();

  // Coordinates in all planes.
  const std::vector<Coordinates> kSampleCoordinates = {
      {.row = 0.2, .column = 0.7},
      {.row = 0.5, .column = 0.9},
      {.row = 0.3, .column = 0.7},
      {.row = 0.8, .column = 0.4}};

  // No scaling.
  const int kScaledWidth = kLumaWidth;
  const int kScaledHeight = kLumaHeight;

  // No filtering.
  const double kStdDevGaussianBlur = 0.02;

  EXPECT_THAT(
      GetSampleValuesForFrame(frame, kSampleCoordinates, kScaledWidth,
                              kScaledHeight, kStdDevGaussianBlur),
      ElementsAre(AllOf(Field(&FilteredSample::value, DoubleEq(156.0)),
                        Field(&FilteredSample::plane, ImagePlane::kChroma)),
                  AllOf(Field(&FilteredSample::value, DoubleEq(2.0)),
                        Field(&FilteredSample::plane, ImagePlane::kChroma)),
                  AllOf(Field(&FilteredSample::value, DoubleEq(36.0)),
                        Field(&FilteredSample::plane, ImagePlane::kChroma)),
                  AllOf(Field(&FilteredSample::value, DoubleEq(64.0)),
                        Field(&FilteredSample::plane, ImagePlane::kLuma))));
}

TEST(HaltonFrameSamplerGaussianFilteringTest,
     ShouldScaleTheFrameWhenScalingIsRequested) {
  // 4x4 i420 frame data.
  const int kLumaWidth = 4;
  const int kLumaHeight = 4;
  const int kChromaWidth = 2;
  const uint8_t kYContent[16] = {20, 196, 250, 115, 139, 39, 99, 197,
                                 21, 166, 254, 28,  227, 54, 64, 46};
  const uint8_t kUContent[4] = {156, 203, 36, 128};
  const uint8_t kVContent[4] = {112, 2, 0, 24};
  const scoped_refptr<I420Buffer> kI420Buffer =
      I420Buffer::Copy(kLumaWidth, kLumaHeight, kYContent, kLumaWidth,
                       kUContent, kChromaWidth, kVContent, kChromaWidth);
  VideoFrame frame =
      VideoFrame::Builder().set_video_frame_buffer(kI420Buffer).build();

  // Coordinates in all planes.
  const std::vector<Coordinates> kSampleCoordinates = {
      {.row = 0.2, .column = 0.7},
      {.row = 0.5, .column = 0.9},
      {.row = 0.3, .column = 0.7},
      {.row = 0.8, .column = 0.4}};

  // With scaling.
  const int kScaledWidth = 2;
  const int kScaledHeight = 2;

  // No filtering.
  const double kStdDevGaussianBlur = 0.02;

  EXPECT_THAT(
      GetSampleValuesForFrame(frame, kSampleCoordinates, kScaledWidth,
                              kScaledHeight, kStdDevGaussianBlur),
      ElementsAre(AllOf(Field(&FilteredSample::value, DoubleEq(131.0)),
                        Field(&FilteredSample::plane, ImagePlane::kChroma)),
                  AllOf(Field(&FilteredSample::value, DoubleEq(35.0)),
                        Field(&FilteredSample::plane, ImagePlane::kChroma)),
                  AllOf(Field(&FilteredSample::value, DoubleEq(131.0)),
                        Field(&FilteredSample::plane, ImagePlane::kChroma)),
                  AllOf(Field(&FilteredSample::value, DoubleEq(98.0)),
                        Field(&FilteredSample::plane, ImagePlane::kLuma))));
}

TEST(HaltonFrameSamplerGaussianFilteringTest,
     ShouldReturnFilteredValuesWhenFilteringIsRequested) {
  // 8x8 i420 frame data.
  const int kLumaWidth = 8;
  const int kLumaHeight = 8;
  const int kChromaWidth = 4;
  const uint8_t kYContent[kLumaWidth * kLumaHeight] = {
      219, 38,  75,  13,  77,  22,  108, 5,    //
      199, 105, 237, 3,   194, 63,  200, 95,   //
      116, 21,  224, 21,  79,  210, 138, 3,    //
      130, 156, 139, 176, 1,   134, 191, 61,   //
      123, 59,  34,  237, 223, 162, 113, 108,  //
      146, 210, 214, 110, 50,  205, 135, 18,   //
      51,  198, 63,  69,  70,  117, 180, 126,  //
      244, 250, 194, 195, 85,  24,  25,  224};
  const uint8_t kUContent[16] = {
      219, 38,  75,  13, 77,  22, 108, 5,  //
      199, 105, 237, 3,  194, 63, 200, 95,
  };
  const uint8_t kVContent[16] = {
      123, 59,  34, 237, 223, 162, 113, 108,  //
      51,  198, 63, 69,  70,  117, 180, 126,
  };
  const scoped_refptr<I420Buffer> kI420Buffer =
      I420Buffer::Copy(kLumaWidth, kLumaHeight, kYContent, kLumaWidth,
                       kUContent, kChromaWidth, kVContent, kChromaWidth);
  VideoFrame frame =
      VideoFrame::Builder().set_video_frame_buffer(kI420Buffer).build();

  // Coordinates in all (YUV) planes.
  const std::vector<Coordinates> kSampleCoordinates = {
      {.row = 0.2, .column = 0.7},
      {.row = 0.5, .column = 0.9},
      {.row = 0.3, .column = 0.7},
      {.row = 0.8, .column = 0.4}};

  // No scaling.
  const int kScaledWidth = kLumaWidth;
  const int kScaledHeight = kLumaHeight;

  // With filtering (kernel size 3x3 minimum required).
  const double kStdDevGaussianBlur = 1;

  EXPECT_THAT(
      GetSampleValuesForFrame(frame, kSampleCoordinates, kScaledWidth,
                              kScaledHeight, kStdDevGaussianBlur),
      ElementsAre(
          AllOf(Field(&FilteredSample::value, DoubleEq(114.6804322931639)),
                Field(&FilteredSample::plane, ImagePlane::kChroma)),
          AllOf(Field(&FilteredSample::value, DoubleEq(109.66816384377159)),
                Field(&FilteredSample::plane, ImagePlane::kChroma)),
          AllOf(Field(&FilteredSample::value, DoubleEq(133.7339472739954)),
                Field(&FilteredSample::plane, ImagePlane::kChroma)),
          AllOf(Field(&FilteredSample::value, DoubleEq(104.43135638243807)),
                Field(&FilteredSample::plane, ImagePlane::kLuma))));
}

TEST(HaltonFrameSamplerTest, CoordinatesFollowsHaltonSequence) {
  HaltonFrameSampler halton_frame_sampler;
  const int kNumSamples = 1;
  EXPECT_THAT(halton_frame_sampler.GetSampleCoordinatesForFrame(kNumSamples),
              ElementsAre(AllOf(Field(&Coordinates::row, DoubleEq(0.0)),
                                Field(&Coordinates::column, DoubleEq(0.0)))));
  EXPECT_THAT(
      halton_frame_sampler.GetSampleCoordinatesForFrame(kNumSamples),
      ElementsAre(AllOf(Field(&Coordinates::row, DoubleEq(1.0 / 2)),
                        Field(&Coordinates::column, DoubleEq(1.0 / 3)))));
  EXPECT_THAT(
      halton_frame_sampler.GetSampleCoordinatesForFrame(kNumSamples),
      ElementsAre(AllOf(Field(&Coordinates::row, DoubleEq(1.0 / 4)),
                        Field(&Coordinates::column, DoubleEq(2.0 / 3)))));
  EXPECT_THAT(
      halton_frame_sampler.GetSampleCoordinatesForFrame(kNumSamples),
      ElementsAre(AllOf(Field(&Coordinates::row, DoubleEq(3.0 / 4)),
                        Field(&Coordinates::column, DoubleEq(1.0 / 9)))));
  EXPECT_THAT(
      halton_frame_sampler.GetSampleCoordinatesForFrame(kNumSamples),
      ElementsAre(AllOf(Field(&Coordinates::row, DoubleEq(1.0 / 8)),
                        Field(&Coordinates::column, DoubleEq(4.0 / 9)))));
  EXPECT_THAT(
      halton_frame_sampler.GetSampleCoordinatesForFrame(kNumSamples),
      ElementsAre(AllOf(Field(&Coordinates::row, DoubleEq(5.0 / 8)),
                        Field(&Coordinates::column, DoubleEq(7.0 / 9)))));
  EXPECT_THAT(
      halton_frame_sampler.GetSampleCoordinatesForFrame(kNumSamples),
      ElementsAre(AllOf(Field(&Coordinates::row, DoubleEq(3.0 / 8)),
                        Field(&Coordinates::column, DoubleEq(2.0 / 9)))));
}

TEST(HaltonFrameSamplerTest, GeneratesMultipleSamplesWhenRequested) {
  HaltonFrameSampler halton_frame_sampler;
  EXPECT_THAT(
      halton_frame_sampler.GetSampleCoordinatesForFrame(3),
      ElementsAre(AllOf(Field(&Coordinates::row, DoubleEq(0.0)),
                        Field(&Coordinates::column, DoubleEq(0.0))),
                  AllOf(Field(&Coordinates::row, DoubleEq(1.0 / 2)),
                        Field(&Coordinates::column, DoubleEq(1.0 / 3))),
                  AllOf(Field(&Coordinates::row, DoubleEq(1.0 / 4)),
                        Field(&Coordinates::column, DoubleEq(2.0 / 3)))));
}

TEST(HaltonFrameSamplerTest, ShouldChangeIndexWhenRequestedTo) {
  HaltonFrameSampler halton_frame_sampler;
  halton_frame_sampler.SetCurrentIndex(1);
  EXPECT_EQ(halton_frame_sampler.GetCurrentIndex(), 1);
  EXPECT_THAT(
      halton_frame_sampler.GetSampleCoordinatesForFrame(1),
      ElementsAre(AllOf(Field(&Coordinates::row, DoubleEq(1.0 / 2)),
                        Field(&Coordinates::column, DoubleEq(1.0 / 3)))));
}

TEST(HaltonFrameSamplerTest, FirstFrameIsSampled) {
  HaltonFrameSampler halton_frame_sampler;
  EXPECT_THAT(
      halton_frame_sampler.GetSampleCoordinatesForFrameIfFrameShouldBeSampled(
          /*is_key_frame=*/false, /*rtp_timestamp=*/0, /*num_samples=*/1),
      Not(IsEmpty()));
}

TEST(HaltonFrameSamplerTest,
     DeltaFrameFollowingSampledFrameWithTooShortTimeDeltaIsNotSampled) {
  HaltonFrameSampler halton_frame_sampler;
  halton_frame_sampler.GetSampleCoordinatesForFrameIfFrameShouldBeSampled(
      /*is_key_frame=*/false, /*rtp_timestamp=*/0, /*num_samples=*/1);
  EXPECT_THAT(
      halton_frame_sampler.GetSampleCoordinatesForFrameIfFrameShouldBeSampled(
          /*is_key_frame=*/false, /*rtp_timestamp=*/1, /*num_samples=*/1),
      IsEmpty());
}

TEST(HaltonFrameSamplerTest,
     DeltaFramesAreSampledBasedOnHowManyFramesHasPassedSinceLastSampledFrame) {
  HaltonFrameSampler halton_frame_sampler;
  uint32_t rtp_timestamp = 0;
  const int kNumSamples = 1;

  // The number of frames between each sample is defined as
  //   33 - mod(number_of_sampled_frames, 8)
  // so the following gets get coverage for [26, 33] two times.
  for (int iterations = 0; iterations < 2; ++iterations) {
    for (int num_sampled_frames = 0; num_sampled_frames < 8;
         ++num_sampled_frames) {
      EXPECT_THAT(halton_frame_sampler
                      .GetSampleCoordinatesForFrameIfFrameShouldBeSampled(
                          /*is_key_frame=*/false, rtp_timestamp, kNumSamples),
                  Not(IsEmpty()));
      ++rtp_timestamp;
      for (int num_unsampled_frames = 1;
           num_unsampled_frames < 33 - num_sampled_frames;
           ++num_unsampled_frames) {
        EXPECT_THAT(halton_frame_sampler
                        .GetSampleCoordinatesForFrameIfFrameShouldBeSampled(
                            /*is_key_frame=*/false, rtp_timestamp, kNumSamples),
                    IsEmpty());
        ++rtp_timestamp;
      }
    }
  }
}

TEST(HaltonFrameSamplerTest, KeyFrameIsSampled) {
  HaltonFrameSampler halton_frame_sampler;
  EXPECT_THAT(
      halton_frame_sampler.GetSampleCoordinatesForFrameIfFrameShouldBeSampled(
          /*is_key_frame=*/false, /*rtp_timestamp=*/0, /*num_samples=*/1),
      Not(IsEmpty()));
  EXPECT_THAT(
      halton_frame_sampler.GetSampleCoordinatesForFrameIfFrameShouldBeSampled(
          /*is_key_frame=*/true, /*rtp_timestamp=*/1, /*num_samples=*/1),
      Not(IsEmpty()));
}

TEST(HaltonFrameSamplerTest,
     SampleFramesWhenEnoughTimeHasPassedSinceLastSampledFrame) {
  HaltonFrameSampler halton_frame_sampler;
  const uint32_t kRtpTimestamp = 0;
  const int kNumSamples = 1;
  const uint32_t kSufficientDuration = 90'000;
  const uint32_t kTooShortDuration = 1;
  halton_frame_sampler.GetSampleCoordinatesForFrameIfFrameShouldBeSampled(
      /*is_key_frame=*/false, kRtpTimestamp, kNumSamples);
  EXPECT_THAT(
      halton_frame_sampler.GetSampleCoordinatesForFrameIfFrameShouldBeSampled(
          /*is_key_frame=*/false, kRtpTimestamp + kSufficientDuration,
          kNumSamples),
      Not(IsEmpty()));
  EXPECT_THAT(
      halton_frame_sampler.GetSampleCoordinatesForFrameIfFrameShouldBeSampled(
          /*is_key_frame=*/false,
          kRtpTimestamp + kSufficientDuration + kTooShortDuration, kNumSamples),
      IsEmpty());
  EXPECT_THAT(
      halton_frame_sampler.GetSampleCoordinatesForFrameIfFrameShouldBeSampled(
          /*is_key_frame=*/false, kRtpTimestamp + 2 * kSufficientDuration,
          kNumSamples),
      Not(IsEmpty()));
}

TEST(HaltonFrameSamplerTest,
     FrameIsNotSampledWhenTooShortTimeHasPassedSinceLastSampledFrame) {
  HaltonFrameSampler halton_frame_sampler;
  const uint32_t kRtpTimestamp = 0;
  const uint32_t kTooShortDuration = 90'000 - 1;
  halton_frame_sampler.GetSampleCoordinatesForFrameIfFrameShouldBeSampled(
      /*is_key_frame=*/false, kRtpTimestamp, /*num_samples=*/1);
  EXPECT_THAT(
      halton_frame_sampler.GetSampleCoordinatesForFrameIfFrameShouldBeSampled(
          /*is_key_frame=*/false, kRtpTimestamp + kTooShortDuration,
          /*num_samples=*/1),
      IsEmpty());
}

TEST(HaltonFrameSamplerTest,
     SampleFramesWhenEnoughTimeWithWraparoundHasPassedSinceLastSampledFrame) {
  HaltonFrameSampler halton_frame_sampler;

  // Time delta = 90'000.
  EXPECT_THAT(
      halton_frame_sampler.GetSampleCoordinatesForFrameIfFrameShouldBeSampled(
          /*is_key_frame=*/false, /*rtp_timestamp=*/0xFFFE'A071,
          /*num_samples=*/1),
      Not(IsEmpty()));
  EXPECT_THAT(
      halton_frame_sampler.GetSampleCoordinatesForFrameIfFrameShouldBeSampled(
          /*is_key_frame=*/false, /*rtp_timestamp=*/1, /*num_samples=*/1),
      Not(IsEmpty()));
}

TEST(
    HaltonFrameSamplerTest,
    FrameIsNotSampledWhenTooShortTimeDeltaWithWraparoundSinceLastSampledFrame) {
  HaltonFrameSampler halton_frame_sampler;

  // Time delta = 89'999.
  EXPECT_THAT(
      halton_frame_sampler.GetSampleCoordinatesForFrameIfFrameShouldBeSampled(
          /*is_key_frame=*/false, /*rtp_timestamp=*/0xFFFE'A072,
          /*num_samples=*/1),
      Not(IsEmpty()));
  EXPECT_THAT(
      halton_frame_sampler.GetSampleCoordinatesForFrameIfFrameShouldBeSampled(
          /*is_key_frame=*/false, /*rtp_timestamp=*/1, /*num_samples=*/1),
      IsEmpty());
}

}  // namespace
}  // namespace webrtc
