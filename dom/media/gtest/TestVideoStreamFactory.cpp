/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include <initializer_list>
#include <vector>

#include "VideoConduit.h"
#include "VideoStreamFactory.h"
#include "api/field_trials_view.h"
#include "api/video/resolution.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "mozilla/Span.h"
#include "mozilla/gfx/Point.h"
#include "video/config/simulcast.h"
#include "video/config/video_encoder_config.h"

namespace mozilla {

using testing::Pointwise;

namespace {
class NoFieldTrials : public webrtc::FieldTrialsView {
 public:
  std::string Lookup(absl::string_view) const override { return ""; }
};

const webrtc::VideoCodecType kCodecs[] = {
    webrtc::kVideoCodecVP8, webrtc::kVideoCodecVP9, webrtc::kVideoCodecH264,
    webrtc::kVideoCodecAV1};

MATCHER(LimitsMatchLayer, "") {
  const auto& [limits, layer] = arg;
  return limits.min_bitrate_bps == layer.min_bitrate_bps &&
         limits.start_bitrate_bps == layer.target_bitrate_bps &&
         limits.max_bitrate_bps == layer.max_bitrate_bps;
}

webrtc::VideoEncoderConfig MakeConfig(webrtc::VideoCodecType aCodec) {
  webrtc::VideoEncoderConfig config;
  config.codec_type = aCodec;
  return config;
}

std::vector<webrtc::VideoStream> MakeStreams(
    std::initializer_list<gfx::IntSize> aSizes) {
  std::vector<webrtc::VideoStream> streams;
  for (const auto& size : aSizes) {
    webrtc::VideoStream& stream = streams.emplace_back();
    stream.width = size.width;
    stream.height = size.height;
  }
  return streams;
}
}  // namespace

TEST(VideoStreamFactoryLimits, SingleStreamMinimumIsLow)
{
  NoFieldTrials trials;
  for (auto codec : kCodecs) {
    for (gfx::IntSize size :
         {gfx::IntSize(320, 240), gfx::IntSize(640, 480),
          gfx::IntSize(1280, 720), gfx::IntSize(1920, 1080)}) {
      auto limits = VideoStreamFactory::GetBitrateLimitsFor(
          trials, MakeConfig(codec), false, MakeStreams({size}));
      ASSERT_EQ(limits.Length(), 1U);
      EXPECT_EQ(limits[0].min_bitrate_bps, kViEMinCodecBitrate_bps);
    }
  }
}

TEST(VideoStreamFactoryLimits, SingleStreamUsesResolutionTable)
{
  NoFieldTrials trials;
  for (gfx::IntSize size :
       {gfx::IntSize(320, 240), gfx::IntSize(640, 480), gfx::IntSize(1280, 720),
        gfx::IntSize(1920, 1080)}) {
    auto limits = VideoStreamFactory::GetBitrateLimitsFor(
        trials, MakeConfig(webrtc::kVideoCodecVP8), false, MakeStreams({size}));
    ASSERT_EQ(limits.Length(), 1U);
    auto expected = VideoStreamFactory::GetUnicastLimitsFor(size);
    webrtc::VideoStream layer;
    layer.min_bitrate_bps = kViEMinCodecBitrate_bps;
    layer.target_bitrate_bps = expected.start_bitrate_bps;
    layer.max_bitrate_bps = expected.max_bitrate_bps;
    EXPECT_THAT(Span(limits),
                Pointwise(LimitsMatchLayer(), std::vector{layer}));
  }
  auto limits = VideoStreamFactory::GetBitrateLimitsFor(
      trials, MakeConfig(webrtc::kVideoCodecVP8), false,
      MakeStreams({{1280, 720}}));
  EXPECT_EQ(limits[0].start_bitrate_bps, 1'500'000);
  EXPECT_EQ(limits[0].max_bitrate_bps, 5'000'000);
}

TEST(VideoStreamFactoryLimits, SimulcastMatchesLibwebrtc)
{
  NoFieldTrials trials;
  const std::initializer_list<gfx::IntSize> sizes = {
      {320, 180}, {640, 360}, {1280, 720}};
  for (bool temporalLayers : {false, true}) {
    for (auto codec : kCodecs) {
      auto limits = VideoStreamFactory::GetBitrateLimitsFor(
          trials, MakeConfig(codec), temporalLayers, MakeStreams(sizes));
      std::vector<webrtc::Resolution> resolutions;
      for (const auto& size : sizes) {
        resolutions.push_back(webrtc::Resolution{size.width, size.height});
      }
      auto layers = webrtc::GetSimulcastConfig(resolutions, false,
                                               temporalLayers, trials, codec);
      EXPECT_THAT(Span(limits), Pointwise(LimitsMatchLayer(), layers));
    }
  }
}

// libwebrtc lets the lowest layer go as low as the encoder allows.
TEST(VideoStreamFactoryLimits, SimulcastLowestLayerMinimumIsLow)
{
  NoFieldTrials trials;
  auto limits = VideoStreamFactory::GetBitrateLimitsFor(
      trials, MakeConfig(webrtc::kVideoCodecVP8), false,
      MakeStreams({{640, 360}, {1280, 720}}));
  ASSERT_EQ(limits.Length(), 2U);
  EXPECT_EQ(limits[0].min_bitrate_bps, kViEMinCodecBitrate_bps);
  EXPECT_GT(limits[1].min_bitrate_bps, kViEMinCodecBitrate_bps);
}

// When the lowest layer is inactive the lowest active layer takes over its
// minimum, so that a lone HD layer isn't forced to its high minimum.
TEST(VideoStreamFactoryLimits, SimulcastInactiveLowestLayerLowersNextMinimum)
{
  NoFieldTrials trials;
  auto streams = MakeStreams({{320, 180}, {640, 360}, {1280, 720}});
  streams[0].active = false;
  auto limits = VideoStreamFactory::GetBitrateLimitsFor(
      trials, MakeConfig(webrtc::kVideoCodecVP8), false, streams);
  ASSERT_EQ(limits.Length(), 3U);
  EXPECT_EQ(limits[1].min_bitrate_bps, kViEMinCodecBitrate_bps);
  EXPECT_GT(limits[2].min_bitrate_bps, kViEMinCodecBitrate_bps);

  streams[1].active = false;
  limits = VideoStreamFactory::GetBitrateLimitsFor(
      trials, MakeConfig(webrtc::kVideoCodecVP8), false, streams);
  EXPECT_EQ(limits[2].min_bitrate_bps, kViEMinCodecBitrate_bps);
}

TEST(VideoStreamFactoryLimits, SimulcastIgnoredLayerHasLimits)
{
  NoFieldTrials trials;
  auto limits = VideoStreamFactory::GetBitrateLimitsFor(
      trials, MakeConfig(webrtc::kVideoCodecVP8), false,
      MakeStreams({{0, 0}, {640, 360}, {1280, 720}}));
  ASSERT_EQ(limits.Length(), 3U);
  EXPECT_GE(limits[0].min_bitrate_bps, 0);
  EXPECT_GE(limits[0].max_bitrate_bps, limits[0].min_bitrate_bps);
}

}  // namespace mozilla
