/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "video/config/encoder_stream_factory.h"

#include <cstddef>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "absl/strings/string_view.h"
#include "api/field_trials.h"
#include "api/field_trials_view.h"
#include "api/make_ref_counted.h"
#include "api/video/resolution.h"
#include "api/video/video_codec_constants.h"
#include "api/video/video_codec_type.h"
#include "api/video_codecs/scalability_mode.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_encoder.h"
#include "call/adaptation/video_source_restrictions.h"
#include "rtc_base/experiments/min_video_bitrate_experiment.h"
#include "rtc_base/numerics/safe_conversions.h"
#include "test/create_test_field_trials.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "video/config/video_encoder_config.h"

namespace webrtc {
namespace {
using ::testing::Combine;
using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::Not;
using ::testing::SizeIs;
using ::testing::TestWithParam;
using ::testing::Values;

MATCHER_P3(MatchesStream, active, width, height, "") {
  return arg.active == active && arg.width == static_cast<size_t>(width) &&
         arg.height == static_cast<size_t>(height);
}

struct CreateVideoStreamParams {
  int width = 0;
  int height = 0;
  int max_framerate_fps = -1;
  int min_bitrate_bps = -1;
  int target_bitrate_bps = -1;
  int max_bitrate_bps = -1;
  int scale_resolution_down_by = -1;
  std::optional<ScalabilityMode> scalability_mode;
};

// A helper function that creates `VideoStream` with given settings.
VideoStream CreateVideoStream(const CreateVideoStreamParams& params) {
  VideoStream stream;
  stream.width = params.width;
  stream.height = params.height;
  stream.max_framerate = params.max_framerate_fps;
  stream.min_bitrate_bps = params.min_bitrate_bps;
  stream.target_bitrate_bps = params.target_bitrate_bps;
  stream.max_bitrate_bps = params.max_bitrate_bps;
  stream.scale_resolution_down_by = params.scale_resolution_down_by;
  stream.scalability_mode = params.scalability_mode;
  return stream;
}

std::vector<Resolution> GetStreamResolutions(
    const std::vector<VideoStream>& streams) {
  std::vector<Resolution> res;
  for (const auto& s : streams) {
    res.push_back({.width = checked_cast<int>(s.width),
                   .height = checked_cast<int>(s.height)});
  }
  return res;
}

std::vector<VideoStream> CreateEncoderStreams(
    const FieldTrialsView& field_trials,
    const Resolution& resolution,
    const VideoEncoderConfig& encoder_config,
    std::optional<VideoSourceRestrictions> restrictions = std::nullopt) {
  VideoEncoder::EncoderInfo encoder_info;
  auto factory =
      make_ref_counted<EncoderStreamFactory>(encoder_info, restrictions);
  return factory->CreateEncoderStreams(field_trials, resolution.width,
                                       resolution.height, encoder_config);
}

}  // namespace

TEST(EncoderStreamFactory, SinglecastScaleResolutionDownTo) {
  FieldTrials field_trials = CreateTestFieldTrials();
  VideoEncoderConfig encoder_config;
  encoder_config.number_of_streams = 1;
  encoder_config.simulcast_layers.resize(1);
  encoder_config.simulcast_layers[0].scale_resolution_down_to = {.width = 640,
                                                                 .height = 360};
  auto streams = CreateEncoderStreams(
      field_trials, {.width = 1280, .height = 720}, encoder_config);
  EXPECT_EQ(streams[0].scale_resolution_down_to,
            (Resolution{.width = 640, .height = 360}));
  EXPECT_THAT(GetStreamResolutions(streams),
              ElementsAre(Resolution{.width = 640, .height = 360}));
}

TEST(EncoderStreamFactory, SinglecastScaleResolutionDownToWithAdaptation) {
  FieldTrials field_trials = CreateTestFieldTrials();
  VideoSourceRestrictions restrictions(
      /* max_pixels_per_frame= */ (320 * 320),
      /* target_pixels_per_frame= */ std::nullopt,
      /* max_frame_rate= */ std::nullopt);
  VideoEncoderConfig encoder_config;
  encoder_config.number_of_streams = 1;
  encoder_config.simulcast_layers.resize(1);
  encoder_config.simulcast_layers[0].scale_resolution_down_to = {.width = 640,
                                                                 .height = 360};
  auto streams =
      CreateEncoderStreams(field_trials, {.width = 1280, .height = 720},
                           encoder_config, restrictions);
  EXPECT_EQ(streams[0].scale_resolution_down_to,
            (Resolution{.width = 640, .height = 360}));
  EXPECT_THAT(GetStreamResolutions(streams),
              ElementsAre(Resolution{.width = 320, .height = 180}));
}

TEST(EncoderStreamFactory, SimulcastScaleResolutionDownToUnrestricted) {
  FieldTrials field_trials = CreateTestFieldTrials();
  VideoEncoderConfig encoder_config;
  encoder_config.number_of_streams = 3;
  encoder_config.simulcast_layers.resize(3);
  encoder_config.simulcast_layers[0].scale_resolution_down_to = {.width = 320,
                                                                 .height = 180};
  encoder_config.simulcast_layers[1].scale_resolution_down_to = {.width = 640,
                                                                 .height = 360};
  encoder_config.simulcast_layers[2].scale_resolution_down_to = {.width = 1280,
                                                                 .height = 720};
  auto streams = CreateEncoderStreams(
      field_trials, {.width = 1280, .height = 720}, encoder_config);
  EXPECT_THAT(GetStreamResolutions(streams),
              ElementsAre(Resolution{.width = 320, .height = 180},
                          Resolution{.width = 640, .height = 360},
                          Resolution{.width = 1280, .height = 720}));
}

TEST(EncoderStreamFactory,
     SimulcastScaleResolutionDownToWithUnalignedResolution) {
  // When `scale_resolution_down_to` is specified, the input resolution should
  // not be normalized to a multiple of `2^(number_of_streams - 1)`.
  FieldTrials field_trials = CreateTestFieldTrials();
  VideoEncoderConfig encoder_config;
  encoder_config.number_of_streams = 3;
  encoder_config.simulcast_layers.resize(3);
  encoder_config.simulcast_layers[0].scale_resolution_down_to = {
      .width = 513, .height = 1025};
  encoder_config.simulcast_layers[1].scale_resolution_down_to = {
      .width = 513, .height = 1025};
  encoder_config.simulcast_layers[2].scale_resolution_down_to = {
      .width = 513, .height = 1025};
  auto streams = CreateEncoderStreams(
      field_trials, {.width = 513, .height = 1025}, encoder_config);
  EXPECT_THAT(GetStreamResolutions(streams),
              ElementsAre(Resolution{.width = 513, .height = 1025},
                          Resolution{.width = 513, .height = 1025},
                          Resolution{.width = 513, .height = 1025}));
}

TEST(EncoderStreamFactory, SimulcastScaleResolutionDownToWith360pRestriction) {
  FieldTrials field_trials = CreateTestFieldTrials();
  VideoSourceRestrictions restrictions(
      /* max_pixels_per_frame= */ (640 * 360),
      /* target_pixels_per_frame= */ std::nullopt,
      /* max_frame_rate= */ std::nullopt);
  VideoEncoderConfig encoder_config;
  encoder_config.number_of_streams = 3;
  encoder_config.simulcast_layers.resize(3);
  encoder_config.simulcast_layers[0].scale_resolution_down_to = {.width = 320,
                                                                 .height = 180};
  encoder_config.simulcast_layers[1].scale_resolution_down_to = {.width = 640,
                                                                 .height = 360};
  encoder_config.simulcast_layers[2].scale_resolution_down_to = {.width = 1280,
                                                                 .height = 720};
  auto streams =
      CreateEncoderStreams(field_trials, {.width = 1280, .height = 720},
                           encoder_config, restrictions);
  // 720p layer is dropped due to 360p restrictions.
  EXPECT_THAT(GetStreamResolutions(streams),
              ElementsAre(Resolution{.width = 320, .height = 180},
                          Resolution{.width = 640, .height = 360}));
}

TEST(EncoderStreamFactory, SimulcastScaleResolutionDownToWith90pRestriction) {
  FieldTrials field_trials = CreateTestFieldTrials();
  VideoSourceRestrictions restrictions(
      /* max_pixels_per_frame= */ (160 * 90),
      /* target_pixels_per_frame= */ std::nullopt,
      /* max_frame_rate= */ std::nullopt);
  VideoEncoderConfig encoder_config;
  encoder_config.number_of_streams = 3;
  encoder_config.simulcast_layers.resize(3);
  encoder_config.simulcast_layers[0].scale_resolution_down_to = {.width = 320,
                                                                 .height = 180};
  encoder_config.simulcast_layers[1].scale_resolution_down_to = {.width = 640,
                                                                 .height = 360};
  encoder_config.simulcast_layers[2].scale_resolution_down_to = {.width = 1280,
                                                                 .height = 720};
  auto streams =
      CreateEncoderStreams(field_trials, {.width = 1280, .height = 720},
                           encoder_config, restrictions);
  // 90p restriction means all but the first layer (180p) is dropped. The one
  // and only layer is downsized to 90p.
  EXPECT_THAT(GetStreamResolutions(streams),
              ElementsAre(Resolution{.width = 160, .height = 90}));
}

TEST(EncoderStreamFactory,
     ReverseSimulcastScaleResolutionDownToWithRestriction) {
  FieldTrials field_trials = CreateTestFieldTrials();
  VideoSourceRestrictions restrictions(
      /* max_pixels_per_frame= */ (640 * 360),
      /* target_pixels_per_frame= */ std::nullopt,
      /* max_frame_rate= */ std::nullopt);
  VideoEncoderConfig encoder_config;
  encoder_config.number_of_streams = 3;
  encoder_config.simulcast_layers.resize(3);
  // 720p, 360p, 180p (instead of the usual 180p, 360p, 720p).
  encoder_config.simulcast_layers[0].scale_resolution_down_to = {.width = 1280,
                                                                 .height = 720};
  encoder_config.simulcast_layers[1].scale_resolution_down_to = {.width = 640,
                                                                 .height = 360};
  encoder_config.simulcast_layers[2].scale_resolution_down_to = {.width = 320,
                                                                 .height = 180};
  auto streams =
      CreateEncoderStreams(field_trials, {.width = 1280, .height = 720},
                           encoder_config, restrictions);
  // The layer dropping that is performed for lower-to-higher ordered simulcast
  // streams is not applicable when higher-to-lower order is used. In this case
  // the 360p restriction is applied to all layers.
  EXPECT_THAT(GetStreamResolutions(streams),
              ElementsAre(Resolution{.width = 640, .height = 360},
                          Resolution{.width = 640, .height = 360},
                          Resolution{.width = 320, .height = 180}));
}

TEST(EncoderStreamFactory,
     SimulcastScaleResolutionDownToWithInactiveLowerLayersAnd360pRestriction) {
  FieldTrials field_trials = CreateTestFieldTrials();
  VideoSourceRestrictions restrictions(
      /* max_pixels_per_frame= */ (640 * 360),
      /* target_pixels_per_frame= */ std::nullopt,
      /* max_frame_rate= */ std::nullopt);
  VideoEncoderConfig encoder_config;
  encoder_config.number_of_streams = 3;
  encoder_config.simulcast_layers.resize(3);
  encoder_config.simulcast_layers[0].active = false;
  encoder_config.simulcast_layers[0].scale_resolution_down_to = {.width = 320,
                                                                 .height = 180};
  encoder_config.simulcast_layers[1].active = false;
  encoder_config.simulcast_layers[1].scale_resolution_down_to = {.width = 640,
                                                                 .height = 360};
  encoder_config.simulcast_layers[2].active = true;
  encoder_config.simulcast_layers[2].scale_resolution_down_to = {.width = 1280,
                                                                 .height = 720};
  auto streams =
      CreateEncoderStreams(field_trials, {.width = 1280, .height = 720},
                           encoder_config, restrictions);
  // Highest layer is preserved and adapted down to 360p.
  EXPECT_THAT(streams, ElementsAre(MatchesStream(/*active=*/false, 320, 180),
                                   MatchesStream(/*active=*/false, 640, 360),
                                   MatchesStream(/*active=*/true, 640, 360)));
}

TEST(EncoderStreamFactory,
     SimulcastScaleResolutionDownToWithInactiveLowestLayerAnd180pRestriction) {
  FieldTrials field_trials = CreateTestFieldTrials();
  VideoSourceRestrictions restrictions(
      /* max_pixels_per_frame= */ (320 * 180),
      /* target_pixels_per_frame= */ std::nullopt,
      /* max_frame_rate= */ std::nullopt);
  VideoEncoderConfig encoder_config;
  encoder_config.number_of_streams = 3;
  encoder_config.simulcast_layers.resize(3);
  encoder_config.simulcast_layers[0].active = false;
  encoder_config.simulcast_layers[0].scale_resolution_down_to = {.width = 320,
                                                                 .height = 180};
  encoder_config.simulcast_layers[1].active = true;
  encoder_config.simulcast_layers[1].scale_resolution_down_to = {.width = 640,
                                                                 .height = 360};
  encoder_config.simulcast_layers[2].active = true;
  encoder_config.simulcast_layers[2].scale_resolution_down_to = {.width = 1280,
                                                                 .height = 720};
  auto streams =
      CreateEncoderStreams(field_trials, {.width = 1280, .height = 720},
                           encoder_config, restrictions);
  // Middle layer is preserved and adapted down to 180p.
  EXPECT_THAT(streams, ElementsAre(MatchesStream(/*active=*/false, 320, 180),
                                   MatchesStream(/*active=*/true, 320, 180)));
}

TEST(EncoderStreamFactory,
     SimulcastScaleResolutionDownToWithAllInactiveLayersAnd360pRestriction) {
  FieldTrials field_trials = CreateTestFieldTrials();
  VideoSourceRestrictions restrictions(
      /* max_pixels_per_frame= */ (640 * 360),
      /* target_pixels_per_frame= */ std::nullopt,
      /* max_frame_rate= */ std::nullopt);
  VideoEncoderConfig encoder_config;
  encoder_config.number_of_streams = 3;
  encoder_config.simulcast_layers.resize(3);
  encoder_config.simulcast_layers[0].active = false;
  encoder_config.simulcast_layers[0].scale_resolution_down_to = {.width = 320,
                                                                 .height = 180};
  encoder_config.simulcast_layers[1].active = false;
  encoder_config.simulcast_layers[1].scale_resolution_down_to = {.width = 640,
                                                                 .height = 360};
  encoder_config.simulcast_layers[2].active = false;
  encoder_config.simulcast_layers[2].scale_resolution_down_to = {.width = 1280,
                                                                 .height = 720};
  auto streams =
      CreateEncoderStreams(field_trials, {.width = 1280, .height = 720},
                           encoder_config, restrictions);
  // Highest layer is dropped (min_num_layers is 0).
  EXPECT_THAT(streams, ElementsAre(MatchesStream(/*active=*/false, 320, 180),
                                   MatchesStream(/*active=*/false, 640, 360)));
}

TEST(EncoderStreamFactory,
     SimulcastScaleResolutionDownToWithMiddleActiveLayerAnd180pRestriction) {
  FieldTrials field_trials = CreateTestFieldTrials();
  VideoSourceRestrictions restrictions(
      /* max_pixels_per_frame= */ (320 * 180),
      /* target_pixels_per_frame= */ std::nullopt,
      /* max_frame_rate= */ std::nullopt);
  VideoEncoderConfig encoder_config;
  encoder_config.number_of_streams = 3;
  encoder_config.simulcast_layers.resize(3);
  encoder_config.simulcast_layers[0].active = false;
  encoder_config.simulcast_layers[0].scale_resolution_down_to = {.width = 320,
                                                                 .height = 180};
  encoder_config.simulcast_layers[1].active = true;
  encoder_config.simulcast_layers[1].scale_resolution_down_to = {.width = 640,
                                                                 .height = 360};
  encoder_config.simulcast_layers[2].active = false;
  encoder_config.simulcast_layers[2].scale_resolution_down_to = {.width = 1280,
                                                                 .height = 720};
  auto streams =
      CreateEncoderStreams(field_trials, {.width = 1280, .height = 720},
                           encoder_config, restrictions);
  // Middle layer is preserved and adapted down to 180p.
  EXPECT_THAT(streams, ElementsAre(MatchesStream(/*active=*/false, 320, 180),
                                   MatchesStream(/*active=*/true, 320, 180)));
}

TEST(EncoderStreamFactory,
     SinglecastScaleResolutionDownToTakesPrecedenceOverBy) {
  FieldTrials field_trials = CreateTestFieldTrials();
  VideoEncoderConfig encoder_config;
  encoder_config.number_of_streams = 1;
  encoder_config.simulcast_layers.resize(1);
  encoder_config.simulcast_layers[0].scale_resolution_down_to = {.width = 960,
                                                                 .height = 650};
  encoder_config.simulcast_layers[0].scale_resolution_down_by = 2;
  auto streams = CreateEncoderStreams(
      field_trials, {.width = 1280, .height = 720}, encoder_config);
  EXPECT_THAT(GetStreamResolutions(streams),
              ElementsAre(Resolution{.width = 960, .height = 540}));
}

TEST(EncoderStreamFactory, ScaleResolutionDownBy) {
  FieldTrials field_trials = CreateTestFieldTrials();
  VideoEncoderConfig encoder_config;
  encoder_config.number_of_streams = 1;
  encoder_config.simulcast_layers.resize(1);
  encoder_config.simulcast_layers[0].scale_resolution_down_by = 2;
  auto streams = CreateEncoderStreams(
      field_trials, {.width = 1280, .height = 720}, encoder_config);
  EXPECT_EQ(streams[0].scale_resolution_down_by, 2);
  EXPECT_EQ(GetStreamResolutions(streams), (std::vector<Resolution>{
                                               {.width = 640, .height = 360},
                                           }));
}

TEST(EncoderStreamFactory, ScaleResolutionDownByWithZeroScaleFactor) {
  FieldTrials field_trials = CreateTestFieldTrials();
  VideoEncoderConfig encoder_config;
  encoder_config.number_of_streams = 1;
  encoder_config.simulcast_layers.resize(1);
  encoder_config.simulcast_layers[0].scale_resolution_down_by = 0;
  auto streams = CreateEncoderStreams(
      field_trials, {.width = 1280, .height = 720}, encoder_config);
  EXPECT_THAT(GetStreamResolutions(streams),
              ElementsAre(Resolution{.width = 1280, .height = 720}));
}

TEST(EncoderStreamFactory, ScaleResolutionDownByWithNegativeScaleFactor) {
  FieldTrials field_trials = CreateTestFieldTrials();
  VideoEncoderConfig encoder_config;
  encoder_config.number_of_streams = 1;
  encoder_config.simulcast_layers.resize(1);
  encoder_config.simulcast_layers[0].scale_resolution_down_by = -2;
  auto streams = CreateEncoderStreams(
      field_trials, {.width = 1280, .height = 720}, encoder_config);
  EXPECT_THAT(GetStreamResolutions(streams),
              ElementsAre(Resolution{.width = 1280, .height = 720}));
}

TEST(EncoderStreamFactory, ScaleResolutionDownByWithPartiallySetScaleFactors) {
  FieldTrials field_trials = CreateTestFieldTrials();
  VideoEncoderConfig encoder_config;
  encoder_config.number_of_streams = 2;
  encoder_config.simulcast_layers.resize(2);
  encoder_config.simulcast_layers[0].scale_resolution_down_by = 2;
  encoder_config.simulcast_layers[1].scale_resolution_down_by = -1;
  auto streams = CreateEncoderStreams(
      field_trials, {.width = 1280, .height = 720}, encoder_config);
  EXPECT_THAT(GetStreamResolutions(streams),
              ElementsAre(Resolution{.width = 640, .height = 360},
                          Resolution{.width = 1280, .height = 720}));
}

TEST(EncoderStreamFactory, ScaleDownByWithUnalignResolutionDyadicScaling) {
  FieldTrials field_trials = CreateTestFieldTrials();
  VideoEncoderConfig encoder_config;
  encoder_config.number_of_streams = 2;
  encoder_config.simulcast_layers.resize(2);
  encoder_config.simulcast_layers[0].scale_resolution_down_by = 1;
  encoder_config.simulcast_layers[1].scale_resolution_down_by = 2;
  auto streams = CreateEncoderStreams(
      field_trials, {.width = 513, .height = 1025}, encoder_config);
  EXPECT_THAT(GetStreamResolutions(streams),
              ElementsAre(Resolution{.width = 512, .height = 1024},
                          Resolution{.width = 256, .height = 512}));
}

TEST(EncoderStreamFactory, ScaleDownByWithUnalignResolutionNonDyadicScaling) {
  // Keeps resolution unchanged if any of `scale_resolution_down_by` is not a
  // power of two number.
  FieldTrials field_trials = CreateTestFieldTrials();
  VideoEncoderConfig encoder_config;
  encoder_config.number_of_streams = 2;
  encoder_config.simulcast_layers.resize(2);
  encoder_config.simulcast_layers[0].scale_resolution_down_by = 1;
  encoder_config.simulcast_layers[1].scale_resolution_down_by = 3;
  auto streams = CreateEncoderStreams(
      field_trials, {.width = 513, .height = 1025}, encoder_config);
  EXPECT_THAT(GetStreamResolutions(streams),
              ElementsAre(Resolution{.width = 513, .height = 1025},
                          Resolution{.width = 171, .height = 342}));
}

TEST(EncoderStreamFactory, ScaleDownByWithUnalignedResolutionLargeScaleFactor) {
  FieldTrials field_trials = CreateTestFieldTrials();
  VideoEncoderConfig encoder_config;
  encoder_config.number_of_streams = 1;
  encoder_config.simulcast_layers.resize(1);
  encoder_config.simulcast_layers[0].scale_resolution_down_by = 1024;
  auto streams = CreateEncoderStreams(
      field_trials, {.width = 513, .height = 1025}, encoder_config);
  EXPECT_THAT(GetStreamResolutions(streams),
              ElementsAre(Resolution{.width = 16, .height = 32}));
}

TEST(EncoderStreamFactory, ScaleDownByWithUnalignedResolutionWithoutScaling) {
  FieldTrials field_trials = CreateTestFieldTrials();
  VideoEncoderConfig encoder_config;
  encoder_config.number_of_streams = 2;
  encoder_config.simulcast_layers.resize(2);
  encoder_config.simulcast_layers[0].scale_resolution_down_by = 1;
  encoder_config.simulcast_layers[1].scale_resolution_down_by = 1;
  auto streams = CreateEncoderStreams(
      field_trials, {.width = 513, .height = 1025}, encoder_config);
  EXPECT_THAT(GetStreamResolutions(streams),
              ElementsAre(Resolution{.width = 513, .height = 1025},
                          Resolution{.width = 513, .height = 1025}));
}

TEST(EncoderStreamFactory, ScaleDownByNormalizesResolutionUpToMaxScaleFactor) {
  // Without `scale_resolution_down_by` resolution normalization assumes 1/2
  // scaling. When three streams are requested, resolution is normalized to be
  // multiple of 8. With `scale_resolution_down_by` resolution is normalized
  // to be multiple of the maximum scale factor.
  FieldTrials field_trials = CreateTestFieldTrials();
  VideoEncoderConfig encoder_config;
  encoder_config.number_of_streams = 3;
  encoder_config.simulcast_layers.resize(3);
  encoder_config.simulcast_layers[0].scale_resolution_down_by = 1;
  encoder_config.simulcast_layers[1].scale_resolution_down_by = 1;
  encoder_config.simulcast_layers[2].scale_resolution_down_by = 2;
  auto streams = CreateEncoderStreams(
      field_trials, {.width = 511, .height = 1027}, encoder_config);
  EXPECT_THAT(GetStreamResolutions(streams),
              ElementsAre(Resolution{.width = 510, .height = 1026},
                          Resolution{.width = 510, .height = 1026},
                          Resolution{.width = 255, .height = 513}));
}

TEST(EncoderStreamFactory, BitratePriority) {
  constexpr double kBitratePriority = 0.123;
  VideoEncoderConfig encoder_config;
  encoder_config.number_of_streams = 2;
  encoder_config.simulcast_layers.resize(encoder_config.number_of_streams);
  encoder_config.bitrate_priority = kBitratePriority;
  auto streams = CreateEncoderStreams(
      /*field_trials=*/CreateTestFieldTrials(), {.width = 640, .height = 360},
      encoder_config);
  ASSERT_THAT(streams, SizeIs(2));
  EXPECT_EQ(streams[0].bitrate_priority, kBitratePriority);
  EXPECT_FALSE(streams[1].bitrate_priority);
}

TEST(EncoderStreamFactory, SetsMinBitrateToDefaultValue) {
  VideoEncoder::EncoderInfo encoder_info;
  auto factory = make_ref_counted<EncoderStreamFactory>(encoder_info);
  VideoEncoderConfig encoder_config;
  encoder_config.number_of_streams = 2;
  encoder_config.simulcast_layers.resize(encoder_config.number_of_streams);
  auto streams = factory->CreateEncoderStreams(CreateTestFieldTrials(), 1920,
                                               1080, encoder_config);
  ASSERT_THAT(streams, Not(IsEmpty()));
  EXPECT_EQ(streams[0].min_bitrate_bps, kDefaultMinVideoBitrateBps);
}

TEST(EncoderStreamFactory, SetsMinBitrateToExperimentalValue) {
  VideoEncoder::EncoderInfo encoder_info;
  auto factory = make_ref_counted<EncoderStreamFactory>(encoder_info);
  VideoEncoderConfig encoder_config;
  encoder_config.number_of_streams = 2;
  encoder_config.simulcast_layers.resize(encoder_config.number_of_streams);
  auto streams = factory->CreateEncoderStreams(
      CreateTestFieldTrials("WebRTC-Video-MinVideoBitrate/Enabled,br:1kbps/"),
      1920, 1080, encoder_config);
  ASSERT_THAT(streams, Not(IsEmpty()));
  EXPECT_NE(streams[0].min_bitrate_bps, kDefaultMinVideoBitrateBps);
  EXPECT_EQ(streams[0].min_bitrate_bps, 1000);
}

struct StreamResolutionTestParams {
  absl::string_view field_trials;
  size_t number_of_streams = 1;
  Resolution resolution = {.width = 640, .height = 480};
  bool is_legacy_screencast = false;
  size_t first_active_layer_idx = 0;
};

std::vector<Resolution> CreateStreamResolutions(
    const StreamResolutionTestParams& test_params) {
  VideoEncoderConfig encoder_config;
  encoder_config.codec_type = VideoCodecType::kVideoCodecVP8;
  encoder_config.number_of_streams = test_params.number_of_streams;
  encoder_config.simulcast_layers.resize(test_params.number_of_streams);
  for (size_t i = 0; i < encoder_config.number_of_streams; ++i) {
    encoder_config.simulcast_layers[i].active =
        (i >= test_params.first_active_layer_idx);
  }
  if (test_params.is_legacy_screencast) {
    encoder_config.content_type = VideoEncoderConfig::ContentType::kScreen;
    encoder_config.legacy_conference_mode = true;
  }
  return GetStreamResolutions(
      CreateEncoderStreams(CreateTestFieldTrials(test_params.field_trials),
                           test_params.resolution, encoder_config));
}

TEST(EncoderStreamFactory, KeepsResolutionUnchangedWhenAligned) {
  EXPECT_THAT(
      CreateStreamResolutions({.number_of_streams = 2,
                               .resolution = {.width = 516, .height = 526}}),
      ElementsAre(Resolution{.width = 516 / 2, .height = 526 / 2},
                  Resolution{.width = 516, .height = 526}));
}

TEST(EncoderStreamFactory, AdjustsResolutionWhenUnaligned) {
  // By default width and height of the smallest simulcast stream are required
  // to be whole numbers. To achieve that, the resolution of the highest
  // simulcast stream is adjusted to be multiple of (2 ^ (number_of_streams -
  // 1)) by rounding down.
  EXPECT_THAT(
      CreateStreamResolutions({.number_of_streams = 2,
                               .resolution = {.width = 515, .height = 517}}),
      ElementsAre(Resolution{.width = 514 / 2, .height = 516 / 2},
                  Resolution{.width = 514, .height = 516}));
}

TEST(EncoderStreamFactory, MakesResolutionDivisibleBy4) {
  EXPECT_THAT(
      CreateStreamResolutions(
          {.field_trials = "WebRTC-NormalizeSimulcastResolution/Enabled-2/",
           .number_of_streams = 2,
           .resolution = {.width = 515, .height = 517}}),
      ElementsAre(Resolution{.width = 512 / 2, .height = 516 / 2},
                  Resolution{.width = 512, .height = 516}));
}

TEST(EncoderStreamFactory, KeepsStreamCountUnchangedWhenResolutionIsHigh) {
  EXPECT_THAT(
      CreateStreamResolutions({.number_of_streams = 3,
                               .resolution = {.width = 1000, .height = 1000}}),
      SizeIs(3));
}

TEST(EncoderStreamFactory, ReducesStreamCountWhenResolutionIsLow) {
  EXPECT_THAT(
      CreateStreamResolutions({.number_of_streams = 3,
                               .resolution = {.width = 100, .height = 100}}),
      SizeIs(1));
}

TEST(EncoderStreamFactory, ReducesStreamCountDownToFirstActiveStream) {
  EXPECT_THAT(
      CreateStreamResolutions({.number_of_streams = 3,
                               .resolution = {.width = 100, .height = 100},
                               .first_active_layer_idx = 1}),
      SizeIs(2));
}

TEST(EncoderStreamFactory,
     ReducesLegacyScreencastStreamCountWhenResolutionIsLow) {
  // At least 2 streams are expected to be configured in legacy screencast mode.
  EXPECT_THAT(
      CreateStreamResolutions({.number_of_streams = 3,
                               .resolution = {.width = 100, .height = 100},
                               .is_legacy_screencast = true}),
      SizeIs(2));
}

TEST(EncoderStreamFactory, KeepsStreamCountUnchangedWhenLegacyLimitIsDisabled) {
  EXPECT_THAT(CreateStreamResolutions(
                  {.field_trials = "WebRTC-LegacySimulcastLayerLimit/Disabled/",
                   .number_of_streams = 3,
                   .resolution = {.width = 100, .height = 100}}),
              SizeIs(3));
}

TEST(EncoderStreamFactory, KeepsHighResolutionWhenStreamCountIsReduced) {
  EXPECT_THAT(
      CreateStreamResolutions({.number_of_streams = 3,
                               .resolution = {.width = 640, .height = 360}}),
      ElementsAre(Resolution{.width = 320, .height = 180},
                  Resolution{.width = 640, .height = 360}));
}

struct OverrideStreamSettingsTestParams {
  std::string field_trials;
  Resolution input_resolution;
  VideoEncoderConfig::ContentType content_type;
  std::vector<VideoStream> requested_streams;
  std::vector<VideoStream> expected_streams;
};

using EncoderStreamFactoryOverrideStreamSettingsTest =
    TestWithParam<std::tuple<OverrideStreamSettingsTestParams, VideoCodecType>>;

TEST_P(EncoderStreamFactoryOverrideStreamSettingsTest, OverrideStreamSettings) {
  OverrideStreamSettingsTestParams test_params = std::get<0>(GetParam());
  VideoEncoderConfig encoder_config;
  encoder_config.codec_type = std::get<1>(GetParam());
  encoder_config.number_of_streams = test_params.requested_streams.size();
  encoder_config.simulcast_layers = test_params.requested_streams;
  encoder_config.content_type = test_params.content_type;
  auto streams =
      CreateEncoderStreams(CreateTestFieldTrials(test_params.field_trials),
                           test_params.input_resolution, encoder_config);
  ASSERT_EQ(streams.size(), test_params.expected_streams.size());
  for (size_t i = 0; i < streams.size(); ++i) {
    SCOPED_TRACE(i);
    const VideoStream& expected = test_params.expected_streams[i];
    EXPECT_EQ(streams[i].width, expected.width);
    EXPECT_EQ(streams[i].height, expected.height);
    EXPECT_EQ(streams[i].max_framerate, expected.max_framerate);
    EXPECT_EQ(streams[i].min_bitrate_bps, expected.min_bitrate_bps);
    EXPECT_EQ(streams[i].target_bitrate_bps, expected.target_bitrate_bps);
    EXPECT_EQ(streams[i].max_bitrate_bps, expected.max_bitrate_bps);
    EXPECT_EQ(streams[i].scalability_mode, expected.scalability_mode);
  }
}

INSTANTIATE_TEST_SUITE_P(
    Vp8H264Screencast,
    EncoderStreamFactoryOverrideStreamSettingsTest,
    Combine(Values(OverrideStreamSettingsTestParams{
                .input_resolution = {.width = 1920, .height = 1080},
                .content_type = VideoEncoderConfig::ContentType::kScreen,
                .requested_streams =
                    {CreateVideoStream(
                         {.max_framerate_fps = 5,
                          .max_bitrate_bps = 420'000,
                          .scale_resolution_down_by = 1,
                          .scalability_mode = ScalabilityMode::kL1T2}),
                     CreateVideoStream(
                         {.max_framerate_fps = 30,
                          .max_bitrate_bps = 2'500'000,
                          .scale_resolution_down_by = 1,
                          .scalability_mode = ScalabilityMode::kL1T2})},
                .expected_streams =
                    {CreateVideoStream(
                         {.width = 1920,
                          .height = 1080,
                          .max_framerate_fps = 5,
                          .min_bitrate_bps = 30'000,
                          .target_bitrate_bps = 420'000,
                          .max_bitrate_bps = 420'000,
                          .scalability_mode = ScalabilityMode::kL1T2}),
                     CreateVideoStream(
                         {.width = 1920,
                          .height = 1080,
                          .max_framerate_fps = 30,
                          .min_bitrate_bps = 800'000,
                          .target_bitrate_bps = 2'500'000,
                          .max_bitrate_bps = 2'500'000,
                          .scalability_mode = ScalabilityMode::kL1T2})}}),
            Values(VideoCodecType::kVideoCodecVP8,
                   VideoCodecType::kVideoCodecH264)));

INSTANTIATE_TEST_SUITE_P(
    Av1Vp9H265Screencast,
    EncoderStreamFactoryOverrideStreamSettingsTest,
    Combine(Values(OverrideStreamSettingsTestParams{
                .input_resolution = {.width = 1920, .height = 1080},
                .content_type = VideoEncoderConfig::ContentType::kScreen,
                .requested_streams =
                    {CreateVideoStream(
                         {.max_framerate_fps = 5,
                          .max_bitrate_bps = 420'000,
                          .scale_resolution_down_by = 1,
                          .scalability_mode = ScalabilityMode::kL1T2}),
                     CreateVideoStream(
                         {.max_framerate_fps = 30,
                          .max_bitrate_bps = 2'500'000,
                          .scale_resolution_down_by = 1,
                          .scalability_mode = ScalabilityMode::kL1T2})},
                .expected_streams =
                    {CreateVideoStream(
                         {.width = 1920,
                          .height = 1080,
                          .max_framerate_fps = 5,
                          .min_bitrate_bps = 30'000,
                          .target_bitrate_bps = 420'000,
                          .max_bitrate_bps = 420'000,
                          .scalability_mode = ScalabilityMode::kL1T2}),
                     CreateVideoStream(
                         {.width = 1920,
                          .height = 1080,
                          .max_framerate_fps = 30,
                          .min_bitrate_bps = 769'000,
                          .target_bitrate_bps = 2'500'000,
                          .max_bitrate_bps = 2'500'000,
                          .scalability_mode = ScalabilityMode::kL1T2})}}),
            Values(
#ifdef RTC_ENABLE_H265
                kVideoCodecH265,
#endif
                VideoCodecType::kVideoCodecAV1,
                VideoCodecType::kVideoCodecVP9)));

TEST(EncoderStreamFactory, VP9TemporalLayerCountTransferToStreamSettings) {
  VideoEncoderConfig encoder_config;
  VideoCodecVP9 vp9_settings = VideoEncoder::GetDefaultVp9Settings();
  encoder_config.encoder_specific_settings =
      make_ref_counted<VideoEncoderConfig::Vp9EncoderSpecificSettings>(
          vp9_settings);
  encoder_config.codec_type = VideoCodecType::kVideoCodecVP9;
  encoder_config.number_of_streams = 1;
  encoder_config.simulcast_layers.resize(1);
  encoder_config.simulcast_layers[0].num_temporal_layers = 3;
  auto streams = CreateEncoderStreams(
      CreateTestFieldTrials(), {.width = 1280, .height = 720}, encoder_config);
  ASSERT_THAT(streams, SizeIs(1));
  EXPECT_EQ(streams[0].num_temporal_layers, 3);
}

TEST(EncoderStreamFactory, AV1TemporalLayerCountTransferToStreamSettings) {
  VideoEncoderConfig encoder_config;
  encoder_config.codec_type = VideoCodecType::kVideoCodecAV1;
  encoder_config.number_of_streams = 1;
  encoder_config.simulcast_layers.resize(1);
  encoder_config.simulcast_layers[0].num_temporal_layers = 3;
  auto streams = CreateEncoderStreams(
      CreateTestFieldTrials(), {.width = 1280, .height = 720}, encoder_config);
  ASSERT_THAT(streams, SizeIs(1));
  EXPECT_EQ(streams[0].num_temporal_layers, 3);
}

TEST(EncoderStreamFactory, H264TemporalLayerCountTransferToStreamSettings) {
  VideoEncoderConfig encoder_config;
  encoder_config.codec_type = VideoCodecType::kVideoCodecH264;
  encoder_config.number_of_streams = 1;
  encoder_config.simulcast_layers.resize(1);
  encoder_config.simulcast_layers[0].num_temporal_layers = 3;
  auto streams = CreateEncoderStreams(
      CreateTestFieldTrials(), {.width = 1280, .height = 720}, encoder_config);
  ASSERT_THAT(streams, SizeIs(1));
  EXPECT_EQ(streams[0].num_temporal_layers, std::nullopt);
}

#ifdef RTC_ENABLE_H265
TEST(EncoderStreamFactory, H265TemporalLayerCountTransferToStreamSettings) {
  VideoEncoderConfig encoder_config;
  encoder_config.codec_type = VideoCodecType::kVideoCodecH265;
  encoder_config.number_of_streams = 1;
  encoder_config.simulcast_layers.resize(1);
  encoder_config.simulcast_layers[0].num_temporal_layers = 3;
  auto streams = CreateEncoderStreams(
      CreateTestFieldTrials(), {.width = 1280, .height = 720}, encoder_config);
  ASSERT_THAT(streams, SizeIs(1));
  EXPECT_EQ(streams[0].num_temporal_layers, 3);
}
#endif

TEST(EncoderStreamFactory, EncodingComplexityTransferredToStreamSettings) {
  VideoEncoderConfig encoder_config;
  encoder_config.codec_type = VideoCodecType::kVideoCodecVP8;
  encoder_config.number_of_streams = 1;
  encoder_config.simulcast_layers.resize(1);
  encoder_config.simulcast_layers[0].encoding_complexity =
      VideoCodecComplexity::kComplexityHigh;
  auto single_stream = CreateEncoderStreams(
      CreateTestFieldTrials(), {.width = 1280, .height = 720}, encoder_config);
  ASSERT_THAT(single_stream, SizeIs(1));
  EXPECT_EQ(single_stream[0].encoding_complexity,
            VideoCodecComplexity::kComplexityHigh);

  encoder_config.number_of_streams = 2;
  encoder_config.simulcast_layers.resize(2);
  encoder_config.simulcast_layers[0].encoding_complexity =
      VideoCodecComplexity::kComplexityLow;
  encoder_config.simulcast_layers[1].encoding_complexity =
      VideoCodecComplexity::kComplexityMax;
  auto streams = CreateEncoderStreams(
      CreateTestFieldTrials(), {.width = 1280, .height = 720}, encoder_config);
  ASSERT_THAT(streams, SizeIs(2));
  EXPECT_EQ(streams[0].encoding_complexity,
            VideoCodecComplexity::kComplexityLow);
  EXPECT_EQ(streams[1].encoding_complexity,
            VideoCodecComplexity::kComplexityMax);
}

TEST(EncoderStreamFactory, VP9SetsMaxBitrateToConfiguredEncodingValue) {
  VideoEncoderConfig encoder_config;
  VideoCodecVP9 vp9_settings = VideoEncoder::GetDefaultVp9Settings();
  encoder_config.encoder_specific_settings =
      make_ref_counted<VideoEncoderConfig::Vp9EncoderSpecificSettings>(
          vp9_settings);
  encoder_config.codec_type = VideoCodecType::kVideoCodecVP9;
  encoder_config.number_of_streams = 1;
  encoder_config.simulcast_layers.resize(3);
  encoder_config.simulcast_layers[0].max_bitrate_bps = 5000000;
  auto streams = CreateEncoderStreams(
      CreateTestFieldTrials(), {.width = 1280, .height = 720}, encoder_config);
  ASSERT_THAT(streams, SizeIs(1));
  EXPECT_EQ(streams[0].max_bitrate_bps, 5000000);
}

// A configured max bitrate of zero is a valid value that means "do not
// allocate bitrate to this encoding", see
// https://w3c.github.io/webrtc-pc/#dom-rtcrtpencodingparameters-maxbitrate
// It must not be confused with an unset max bitrate, which falls back to
// GetMaxDefaultVideoBitrateKbps().
TEST(EncoderStreamFactory, SinglecastMaxBitrateZeroIsNotTreatedAsUnset) {
  VideoEncoderConfig encoder_config;
  encoder_config.number_of_streams = 1;
  encoder_config.simulcast_layers.resize(1);
  encoder_config.simulcast_layers[0].max_bitrate_bps = 0;
  auto streams = CreateEncoderStreams(
      CreateTestFieldTrials(), {.width = 1280, .height = 720}, encoder_config);
  ASSERT_THAT(streams, SizeIs(1));
  EXPECT_EQ(streams[0].max_bitrate_bps, 0);
  EXPECT_EQ(streams[0].target_bitrate_bps, 0);
  EXPECT_EQ(streams[0].min_bitrate_bps, 0);
}

TEST(EncoderStreamFactory, SinglecastUnsetMaxBitrateUsesDefault) {
  VideoEncoderConfig encoder_config;
  encoder_config.number_of_streams = 1;
  encoder_config.simulcast_layers.resize(1);
  ASSERT_EQ(encoder_config.simulcast_layers[0].max_bitrate_bps, -1);
  auto streams = CreateEncoderStreams(
      CreateTestFieldTrials(), {.width = 1280, .height = 720}, encoder_config);
  ASSERT_THAT(streams, SizeIs(1));
  EXPECT_EQ(streams[0].max_bitrate_bps, 2'500'000);
  EXPECT_EQ(streams[0].min_bitrate_bps, kDefaultMinVideoBitrateBps);
}

TEST(EncoderStreamFactory, SinglecastMaxBitrateZeroIsNotRaisedBySdpMaxBitrate) {
  VideoEncoderConfig encoder_config;
  encoder_config.number_of_streams = 1;
  encoder_config.simulcast_layers.resize(1);
  encoder_config.simulcast_layers[0].max_bitrate_bps = 0;
  // "b=AS" or "x-google-max-bitrate".
  encoder_config.max_bitrate_bps = 1'000'000;
  auto streams = CreateEncoderStreams(
      CreateTestFieldTrials(), {.width = 1280, .height = 720}, encoder_config);
  ASSERT_THAT(streams, SizeIs(1));
  EXPECT_EQ(streams[0].max_bitrate_bps, 0);
}

TEST(EncoderStreamFactory, SinglecastMaxBitrateZeroIsNotRaisedByMinBitrate) {
  VideoEncoderConfig encoder_config;
  encoder_config.number_of_streams = 1;
  encoder_config.simulcast_layers.resize(1);
  encoder_config.simulcast_layers[0].max_bitrate_bps = 0;
  encoder_config.simulcast_layers[0].min_bitrate_bps = 600'000;
  auto streams = CreateEncoderStreams(
      CreateTestFieldTrials(), {.width = 1280, .height = 720}, encoder_config);
  ASSERT_THAT(streams, SizeIs(1));
  EXPECT_EQ(streams[0].max_bitrate_bps, 0);
  EXPECT_EQ(streams[0].min_bitrate_bps, 0);
}

TEST(EncoderStreamFactory, SimulcastMaxBitrateZeroOnMiddleLayer) {
  VideoEncoderConfig unmodified_config;
  unmodified_config.codec_type = VideoCodecType::kVideoCodecVP8;
  unmodified_config.number_of_streams = 3;
  unmodified_config.simulcast_layers.resize(3);
  auto expected =
      CreateEncoderStreams(CreateTestFieldTrials(),
                           {.width = 1280, .height = 720}, unmodified_config);
  ASSERT_THAT(expected, SizeIs(3));

  VideoEncoderConfig encoder_config;
  encoder_config.codec_type = VideoCodecType::kVideoCodecVP8;
  encoder_config.number_of_streams = 3;
  encoder_config.simulcast_layers.resize(3);
  encoder_config.simulcast_layers[1].max_bitrate_bps = 0;
  auto streams = CreateEncoderStreams(
      CreateTestFieldTrials(), {.width = 1280, .height = 720}, encoder_config);
  ASSERT_THAT(streams, SizeIs(3));
  EXPECT_EQ(streams[1].max_bitrate_bps, 0);
  EXPECT_EQ(streams[1].target_bitrate_bps, 0);
  EXPECT_EQ(streams[1].min_bitrate_bps, 0);
  // The other layers keep the values they would have had anyway.
  EXPECT_EQ(streams[0].max_bitrate_bps, expected[0].max_bitrate_bps);
  EXPECT_EQ(streams[0].min_bitrate_bps, expected[0].min_bitrate_bps);
  EXPECT_EQ(streams[2].max_bitrate_bps, expected[2].max_bitrate_bps);
  EXPECT_EQ(streams[2].min_bitrate_bps, expected[2].min_bitrate_bps);
}

TEST(EncoderStreamFactory, SimulcastMaxBitrateZeroOnHighestLayerIsNotBoosted) {
  VideoEncoderConfig encoder_config;
  encoder_config.codec_type = VideoCodecType::kVideoCodecVP8;
  encoder_config.number_of_streams = 3;
  encoder_config.simulcast_layers.resize(3);
  encoder_config.simulcast_layers[2].max_bitrate_bps = 0;
  // Leftover bitrate is only given to the highest layer if that layer has no
  // application-configured maximum; zero is such a configured maximum.
  encoder_config.max_bitrate_bps = 10'000'000;
  auto streams = CreateEncoderStreams(
      CreateTestFieldTrials(), {.width = 1280, .height = 720}, encoder_config);
  ASSERT_THAT(streams, SizeIs(3));
  EXPECT_EQ(streams[2].max_bitrate_bps, 0);
  EXPECT_EQ(streams[2].target_bitrate_bps, 0);
  EXPECT_EQ(streams[2].min_bitrate_bps, 0);
}

TEST(EncoderStreamFactory, VP9SetsMaxBitrateToConfiguredEncodingValueZero) {
  VideoEncoderConfig encoder_config;
  VideoCodecVP9 vp9_settings = VideoEncoder::GetDefaultVp9Settings();
  encoder_config.encoder_specific_settings =
      make_ref_counted<VideoEncoderConfig::Vp9EncoderSpecificSettings>(
          vp9_settings);
  encoder_config.codec_type = VideoCodecType::kVideoCodecVP9;
  encoder_config.number_of_streams = 1;
  encoder_config.simulcast_layers.resize(3);
  encoder_config.simulcast_layers[0].max_bitrate_bps = 0;
  auto streams = CreateEncoderStreams(
      CreateTestFieldTrials(), {.width = 1280, .height = 720}, encoder_config);
  ASSERT_THAT(streams, SizeIs(1));
  // Neither the SvcConfig bitrate sum nor the min bitrate floor may raise a
  // configured zero.
  EXPECT_EQ(streams[0].max_bitrate_bps, 0);
  EXPECT_EQ(streams[0].target_bitrate_bps, 0);
  EXPECT_EQ(streams[0].min_bitrate_bps, 0);
}

}  // namespace webrtc
