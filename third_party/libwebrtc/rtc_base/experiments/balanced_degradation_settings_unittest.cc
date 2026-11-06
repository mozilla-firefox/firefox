/*
 *  Copyright 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/experiments/balanced_degradation_settings.h"

#include <limits>

#include "rtc_base/gunit.h"
#include "test/gmock.h"
#include "test/scoped_key_value_config.h"

namespace webrtc {
namespace {

void VerifyIsDefault(
    const std::vector<BalancedDegradationSettings::Config>& config) {
  EXPECT_THAT(config,
              ::testing::ElementsAre(
                  BalancedDegradationSettings::Config{
                      320 * 240,
                      7,
                      0,
                      0,
                      BalancedDegradationSettings::kNoFpsDiff,
                      {0, 0, 0, 0, 0},
                      {0, 0, 0, 0, 0},
                      {0, 0, 0, 0, 0},
                      {0, 0, 0, 0, 0},
                      {0, 0, 0, 0, 0}},
                  BalancedDegradationSettings::Config{480 * 360,
                                                      10,
                                                      0,
                                                      0,
                                                      1,
                                                      {0, 0, 0, 0, 0},
                                                      {0, 0, 0, 0, 0},
                                                      {0, 0, 0, 0, 0},
                                                      {0, 0, 0, 0, 0},
                                                      {0, 0, 0, 0, 0}},
                  BalancedDegradationSettings::Config{640 * 480,
                                                      15,
                                                      0,
                                                      0,
                                                      1,
                                                      {0, 0, 0, 0, 0},
                                                      {0, 0, 0, 0, 0},
                                                      {0, 0, 0, 0, 0},
                                                      {0, 0, 0, 0, 0},
                                                      {0, 0, 0, 0, 0}}));
}
}  // namespace

TEST(BalancedDegradationSettings, GetsDefaultConfigIfNoList) {
  webrtc::test::ScopedKeyValueConfig field_trials("");
  BalancedDegradationSettings settings(field_trials);
  VerifyIsDefault(settings.GetConfigs());
  EXPECT_TRUE(settings.CanAdaptUp(kVideoCodecVP8, 1, /*bitrate_bps*/ 1));
  EXPECT_TRUE(
      settings.CanAdaptUpResolution(kVideoCodecVP8, 1, /*bitrate_bps*/ 1));
  EXPECT_FALSE(settings.MinFpsDiff(1));
  EXPECT_FALSE(settings.GetQpThresholds(kVideoCodecVP8, 1));
  EXPECT_FALSE(settings.GetQpThresholds(kVideoCodecVP9, 1));
  EXPECT_FALSE(settings.GetQpThresholds(kVideoCodecH264, 1));
  EXPECT_FALSE(settings.GetQpThresholds(kVideoCodecAV1, 1));
  EXPECT_FALSE(settings.GetQpThresholds(kVideoCodecGeneric, 1));
}

TEST(BalancedDegradationSettings, GetsConfig) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:11|22|33,fps:5|15|25,other:4|5|6/");
  BalancedDegradationSettings settings(field_trials);
  EXPECT_THAT(settings.GetConfigs(),
              ::testing::ElementsAre(
                  BalancedDegradationSettings::Config{
                      11,
                      5,
                      0,
                      0,
                      BalancedDegradationSettings::kNoFpsDiff,
                      {0, 0, 0, 0, 0},
                      {0, 0, 0, 0, 0},
                      {0, 0, 0, 0, 0},
                      {0, 0, 0, 0, 0},
                      {0, 0, 0, 0, 0}},
                  BalancedDegradationSettings::Config{
                      22,
                      15,
                      0,
                      0,
                      BalancedDegradationSettings::kNoFpsDiff,
                      {0, 0, 0, 0, 0},
                      {0, 0, 0, 0, 0},
                      {0, 0, 0, 0, 0},
                      {0, 0, 0, 0, 0},
                      {0, 0, 0, 0, 0}},
                  BalancedDegradationSettings::Config{
                      33,
                      25,
                      0,
                      0,
                      BalancedDegradationSettings::kNoFpsDiff,
                      {0, 0, 0, 0, 0},
                      {0, 0, 0, 0, 0},
                      {0, 0, 0, 0, 0},
                      {0, 0, 0, 0, 0},
                      {0, 0, 0, 0, 0}}));
}

TEST(BalancedDegradationSettings, GetsDefaultConfigForZeroFpsValue) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000,fps:0|15|25/");
  BalancedDegradationSettings settings(field_trials);
  VerifyIsDefault(settings.GetConfigs());
}

TEST(BalancedDegradationSettings, GetsDefaultConfigIfPixelsDecreases) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|999|3000,fps:5|15|25/");
  BalancedDegradationSettings settings(field_trials);
  VerifyIsDefault(settings.GetConfigs());
}

TEST(BalancedDegradationSettings, GetsDefaultConfigIfFramerateDecreases) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000,fps:5|4|25/");
  BalancedDegradationSettings settings(field_trials);
  VerifyIsDefault(settings.GetConfigs());
}

TEST(BalancedDegradationSettings, GetsConfigWithSpecificFps) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000,fps:5|15|25,vp8_fps:7|8|9,vp9_fps:9|10|11,"
      "h264_fps:11|12|13,av1_fps:1|2|3,generic_fps:13|14|15/");
  BalancedDegradationSettings settings(field_trials);
  EXPECT_THAT(settings.GetConfigs(),
              ::testing::ElementsAre(
                  BalancedDegradationSettings::Config{
                      1000,
                      5,
                      0,
                      0,
                      BalancedDegradationSettings::kNoFpsDiff,
                      {0, 0, 7, 0, 0},
                      {0, 0, 9, 0, 0},
                      {0, 0, 11, 0, 0},
                      {0, 0, 1, 0, 0},
                      {0, 0, 13, 0, 0}},
                  BalancedDegradationSettings::Config{
                      2000,
                      15,
                      0,
                      0,
                      BalancedDegradationSettings::kNoFpsDiff,
                      {0, 0, 8, 0, 0},
                      {0, 0, 10, 0, 0},
                      {0, 0, 12, 0, 0},
                      {0, 0, 2, 0, 0},
                      {0, 0, 14, 0, 0}},
                  BalancedDegradationSettings::Config{
                      3000,
                      25,
                      0,
                      0,
                      BalancedDegradationSettings::kNoFpsDiff,
                      {0, 0, 9, 0, 0},
                      {0, 0, 11, 0, 0},
                      {0, 0, 13, 0, 0},
                      {0, 0, 3, 0, 0},
                      {0, 0, 15, 0, 0}}));
}

TEST(BalancedDegradationSettings, GetsDefaultConfigForZeroVp8FpsValue) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000,fps:7|15|25,vp8_fps:0|15|25/");
  BalancedDegradationSettings settings(field_trials);
  VerifyIsDefault(settings.GetConfigs());
}

TEST(BalancedDegradationSettings, GetsDefaultConfigForInvalidFpsValue) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000,fps:7|15|25,vp8_fps:10|15|2000/");
  BalancedDegradationSettings settings(field_trials);
  VerifyIsDefault(settings.GetConfigs());
}

TEST(BalancedDegradationSettings, GetsDefaultConfigIfVp8FramerateDecreases) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000,fps:4|5|25,vp8_fps:5|4|25/");
  BalancedDegradationSettings settings(field_trials);
  VerifyIsDefault(settings.GetConfigs());
}

TEST(BalancedDegradationSettings, GetsMinFps) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000,fps:5|15|25/");
  BalancedDegradationSettings settings(field_trials);
  EXPECT_EQ(5, settings.MinFps(kVideoCodecVP8, 1));
  EXPECT_EQ(5, settings.MinFps(kVideoCodecVP8, 1000));
  EXPECT_EQ(15, settings.MinFps(kVideoCodecVP8, 1001));
  EXPECT_EQ(15, settings.MinFps(kVideoCodecVP8, 2000));
  EXPECT_EQ(25, settings.MinFps(kVideoCodecVP8, 2001));
  EXPECT_EQ(25, settings.MinFps(kVideoCodecVP8, 3000));
  EXPECT_EQ(std::numeric_limits<int>::max(),
            settings.MinFps(kVideoCodecVP8, 3001));
}

TEST(BalancedDegradationSettings, GetsVp8MinFps) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000,fps:5|15|25,vp8_fps:7|10|12/");
  BalancedDegradationSettings settings(field_trials);
  EXPECT_EQ(7, settings.MinFps(kVideoCodecVP8, 1));
  EXPECT_EQ(7, settings.MinFps(kVideoCodecVP8, 1000));
  EXPECT_EQ(10, settings.MinFps(kVideoCodecVP8, 1001));
  EXPECT_EQ(10, settings.MinFps(kVideoCodecVP8, 2000));
  EXPECT_EQ(12, settings.MinFps(kVideoCodecVP8, 2001));
  EXPECT_EQ(12, settings.MinFps(kVideoCodecVP8, 3000));
  EXPECT_EQ(std::numeric_limits<int>::max(),
            settings.MinFps(kVideoCodecVP8, 3001));
}

TEST(BalancedDegradationSettings, GetsMaxFps) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000,fps:5|15|25/");
  BalancedDegradationSettings settings(field_trials);
  EXPECT_EQ(15, settings.MaxFps(kVideoCodecVP8, 1));
  EXPECT_EQ(15, settings.MaxFps(kVideoCodecVP8, 1000));
  EXPECT_EQ(25, settings.MaxFps(kVideoCodecVP8, 1001));
  EXPECT_EQ(25, settings.MaxFps(kVideoCodecVP8, 2000));
  EXPECT_EQ(std::numeric_limits<int>::max(),
            settings.MaxFps(kVideoCodecVP8, 2001));
}

TEST(BalancedDegradationSettings, GetsVp8MaxFps) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000,fps:5|15|25,vp8_fps:7|10|12/");
  BalancedDegradationSettings settings(field_trials);
  EXPECT_EQ(10, settings.MaxFps(kVideoCodecVP8, 1));
  EXPECT_EQ(10, settings.MaxFps(kVideoCodecVP8, 1000));
  EXPECT_EQ(12, settings.MaxFps(kVideoCodecVP8, 1001));
  EXPECT_EQ(12, settings.MaxFps(kVideoCodecVP8, 2000));
  EXPECT_EQ(std::numeric_limits<int>::max(),
            settings.MaxFps(kVideoCodecVP8, 2001));
}

TEST(BalancedDegradationSettings, GetsVp9Fps) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000,fps:5|15|25,vp9_fps:7|10|12/");
  BalancedDegradationSettings settings(field_trials);
  EXPECT_EQ(7, settings.MinFps(kVideoCodecVP9, 1000));
  EXPECT_EQ(10, settings.MaxFps(kVideoCodecVP9, 1000));
}

TEST(BalancedDegradationSettings, GetsH264Fps) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000,fps:5|15|25,h264_fps:8|11|13/");
  BalancedDegradationSettings settings(field_trials);
  EXPECT_EQ(11, settings.MinFps(kVideoCodecH264, 2000));
  EXPECT_EQ(13, settings.MaxFps(kVideoCodecH264, 2000));
}

TEST(BalancedDegradationSettings, GetsGenericFps) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000,fps:5|15|25,generic_fps:9|12|14/");
  BalancedDegradationSettings settings(field_trials);
  EXPECT_EQ(14, settings.MinFps(kVideoCodecGeneric, 3000));
  EXPECT_EQ(std::numeric_limits<int>::max(),
            settings.MaxFps(kVideoCodecGeneric, 3000));
}

TEST(BalancedDegradationSettings, GetsUnlimitedForMaxValidFps) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000,fps:5|15|100,vp8_fps:30|100|100/");
  const int kUnlimitedFps = std::numeric_limits<int>::max();
  BalancedDegradationSettings settings(field_trials);
  EXPECT_EQ(15, settings.MinFps(kVideoCodecH264, 2000));
  EXPECT_EQ(kUnlimitedFps, settings.MinFps(kVideoCodecH264, 2001));
  EXPECT_EQ(30, settings.MinFps(kVideoCodecVP8, 1000));
  EXPECT_EQ(kUnlimitedFps, settings.MinFps(kVideoCodecVP8, 1001));
}

TEST(BalancedDegradationSettings, GetsConfigWithBitrate) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:11|22|33,fps:5|15|25,kbps:44|88|99,kbps_res:55|111|222,"
      "vp8_kbps:11|12|13,vp8_kbps_res:14|15|16,"
      "vp9_kbps:21|22|23,vp9_kbps_res:24|25|26,"
      "h264_kbps:31|32|33,h264_kbps_res:34|35|36,"
      "av1_kbps:41|42|43,av1_kbps_res:44|45|46,"
      "generic_kbps:51|52|53,generic_kbps_res:54|55|56/");
  BalancedDegradationSettings settings(field_trials);
  EXPECT_THAT(settings.GetConfigs(),
              ::testing::ElementsAre(
                  BalancedDegradationSettings::Config{
                      11,
                      5,
                      44,
                      55,
                      BalancedDegradationSettings::kNoFpsDiff,
                      {0, 0, 0, 11, 14},
                      {0, 0, 0, 21, 24},
                      {0, 0, 0, 31, 34},
                      {0, 0, 0, 41, 44},
                      {0, 0, 0, 51, 54}},
                  BalancedDegradationSettings::Config{
                      22,
                      15,
                      88,
                      111,
                      BalancedDegradationSettings::kNoFpsDiff,
                      {0, 0, 0, 12, 15},
                      {0, 0, 0, 22, 25},
                      {0, 0, 0, 32, 35},
                      {0, 0, 0, 42, 45},
                      {0, 0, 0, 52, 55}},
                  BalancedDegradationSettings::Config{
                      33,
                      25,
                      99,
                      222,
                      BalancedDegradationSettings::kNoFpsDiff,
                      {0, 0, 0, 13, 16},
                      {0, 0, 0, 23, 26},
                      {0, 0, 0, 33, 36},
                      {0, 0, 0, 43, 46},
                      {0, 0, 0, 53, 56}}));
}

TEST(BalancedDegradationSettings, GetsDefaultConfigIfBitrateDecreases) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:11|22|33,fps:5|15|25,kbps:44|43|99/");
  BalancedDegradationSettings settings(field_trials);
  VerifyIsDefault(settings.GetConfigs());
}

TEST(BalancedDegradationSettings,
     GetsDefaultConfigIfBitrateDecreasesWithUnsetValue) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:11|22|33,fps:5|15|25,kbps:44|0|43/");
  BalancedDegradationSettings settings(field_trials);
  VerifyIsDefault(settings.GetConfigs());
}

TEST(BalancedDegradationSettings, CanAdaptUp) {
  VideoCodecType vp8 = kVideoCodecVP8;
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000|4000,fps:5|15|25|30,kbps:0|80|0|90,"
      "vp9_kbps:40|50|60|70/");
  BalancedDegradationSettings s(field_trials);
  EXPECT_TRUE(s.CanAdaptUp(vp8, 1000, 0));  // No bitrate provided.
  EXPECT_FALSE(s.CanAdaptUp(vp8, 1000, 79000));
  EXPECT_TRUE(s.CanAdaptUp(vp8, 1000, 80000));
  EXPECT_TRUE(s.CanAdaptUp(vp8, 1001, 1));  // No limit configured.
  EXPECT_FALSE(s.CanAdaptUp(vp8, 3000, 89000));
  EXPECT_TRUE(s.CanAdaptUp(vp8, 3000, 90000));
  EXPECT_TRUE(s.CanAdaptUp(vp8, 3001, 1));  // No limit.
}

TEST(BalancedDegradationSettings, CanAdaptUpWithCodecType) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000|4000,fps:5|15|25|30,vp8_kbps:0|30|40|50,"
      "vp9_kbps:0|60|70|80,h264_kbps:0|55|65|75,av1_kbps:0|77|88|99,"
      "generic_kbps:0|25|35|45/");
  BalancedDegradationSettings s(field_trials);
  EXPECT_FALSE(s.CanAdaptUp(kVideoCodecVP8, 1000, 29000));
  EXPECT_TRUE(s.CanAdaptUp(kVideoCodecVP8, 1000, 30000));
  EXPECT_FALSE(s.CanAdaptUp(kVideoCodecVP9, 1000, 59000));
  EXPECT_TRUE(s.CanAdaptUp(kVideoCodecVP9, 1000, 60000));
  EXPECT_FALSE(s.CanAdaptUp(kVideoCodecH264, 1000, 54000));
  EXPECT_TRUE(s.CanAdaptUp(kVideoCodecH264, 1000, 55000));
  EXPECT_FALSE(s.CanAdaptUp(kVideoCodecAV1, 1000, 76000));
  EXPECT_TRUE(s.CanAdaptUp(kVideoCodecAV1, 1000, 77000));
  EXPECT_FALSE(s.CanAdaptUp(kVideoCodecGeneric, 1000, 24000));
  EXPECT_TRUE(s.CanAdaptUp(kVideoCodecGeneric, 1000, 25000));
}

TEST(BalancedDegradationSettings, CanAdaptUpResolution) {
  VideoCodecType vp8 = kVideoCodecVP8;
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000|4000,fps:5|15|25|30,kbps_res:0|80|0|90,"
      "vp9_kbps_res:40|50|60|70/");
  BalancedDegradationSettings s(field_trials);
  EXPECT_TRUE(s.CanAdaptUpResolution(vp8, 1000, 0));  // No bitrate provided.
  EXPECT_FALSE(s.CanAdaptUpResolution(vp8, 1000, 79000));
  EXPECT_TRUE(s.CanAdaptUpResolution(vp8, 1000, 80000));
  EXPECT_TRUE(s.CanAdaptUpResolution(vp8, 1001, 1));  // No limit configured.
  EXPECT_FALSE(s.CanAdaptUpResolution(vp8, 3000, 89000));
  EXPECT_TRUE(s.CanAdaptUpResolution(vp8, 3000, 90000));
  EXPECT_TRUE(s.CanAdaptUpResolution(vp8, 3001, 1));  // No limit.
}

TEST(BalancedDegradationSettings, CanAdaptUpResolutionWithCodecType) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000|4000,fps:5|15|25|30,vp8_kbps_res:0|30|40|50,"
      "vp9_kbps_res:0|60|70|80,h264_kbps_res:0|55|65|75,"
      "av1_kbps_res:0|77|88|99,generic_kbps_res:0|25|35|45/");
  BalancedDegradationSettings s(field_trials);
  EXPECT_FALSE(s.CanAdaptUpResolution(kVideoCodecVP8, 1000, 29000));
  EXPECT_TRUE(s.CanAdaptUpResolution(kVideoCodecVP8, 1000, 30000));
  EXPECT_FALSE(s.CanAdaptUpResolution(kVideoCodecVP9, 1000, 59000));
  EXPECT_TRUE(s.CanAdaptUpResolution(kVideoCodecVP9, 1000, 60000));
  EXPECT_FALSE(s.CanAdaptUpResolution(kVideoCodecH264, 1000, 54000));
  EXPECT_TRUE(s.CanAdaptUpResolution(kVideoCodecH264, 1000, 55000));
  EXPECT_FALSE(s.CanAdaptUpResolution(kVideoCodecAV1, 1000, 76000));
  EXPECT_TRUE(s.CanAdaptUpResolution(kVideoCodecAV1, 1000, 77000));
  EXPECT_FALSE(s.CanAdaptUpResolution(kVideoCodecGeneric, 1000, 24000));
  EXPECT_TRUE(s.CanAdaptUpResolution(kVideoCodecGeneric, 1000, 25000));
}

TEST(BalancedDegradationSettings, GetsFpsDiff) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000,fps:5|15|25,fps_diff:0|-2|3/");
  BalancedDegradationSettings settings(field_trials);
  EXPECT_EQ(0, settings.MinFpsDiff(1));
  EXPECT_EQ(0, settings.MinFpsDiff(1000));
  EXPECT_EQ(-2, settings.MinFpsDiff(1001));
  EXPECT_EQ(-2, settings.MinFpsDiff(2000));
  EXPECT_EQ(3, settings.MinFpsDiff(2001));
  EXPECT_EQ(3, settings.MinFpsDiff(3000));
  EXPECT_FALSE(settings.MinFpsDiff(3001));
}

TEST(BalancedDegradationSettings, GetsNoFpsDiffIfValueBelowMinSetting) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000,fps:5|15|25,fps_diff:-100|-99|-101/");
  // Min valid fps_diff setting: -99.
  BalancedDegradationSettings settings(field_trials);
  EXPECT_FALSE(settings.MinFpsDiff(1000));
  EXPECT_EQ(-99, settings.MinFpsDiff(2000));
  EXPECT_FALSE(settings.MinFpsDiff(3000));
}

TEST(BalancedDegradationSettings, QpThresholdsNotSetByDefault) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000,fps:5|15|25/");
  BalancedDegradationSettings settings(field_trials);
  EXPECT_FALSE(settings.GetQpThresholds(kVideoCodecVP8, 1));
  EXPECT_FALSE(settings.GetQpThresholds(kVideoCodecVP9, 1));
  EXPECT_FALSE(settings.GetQpThresholds(kVideoCodecH264, 1));
  EXPECT_FALSE(settings.GetQpThresholds(kVideoCodecAV1, 1));
  EXPECT_FALSE(settings.GetQpThresholds(kVideoCodecGeneric, 1));
}

TEST(BalancedDegradationSettings, GetsConfigWithQpThresholds) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000,fps:5|15|25,vp8_qp_low:89|90|88,"
      "vp8_qp_high:90|91|92,vp9_qp_low:27|28|29,vp9_qp_high:120|130|140,"
      "h264_qp_low:12|13|14,h264_qp_high:20|30|40,av1_qp_low:2|3|4,"
      "av1_qp_high:11|33|44,generic_qp_low:7|6|5,generic_qp_high:22|23|24/");
  BalancedDegradationSettings settings(field_trials);
  EXPECT_THAT(settings.GetConfigs(),
              ::testing::ElementsAre(
                  BalancedDegradationSettings::Config{
                      1000,
                      5,
                      0,
                      0,
                      BalancedDegradationSettings::kNoFpsDiff,
                      {89, 90, 0, 0, 0},
                      {27, 120, 0, 0, 0},
                      {12, 20, 0, 0, 0},
                      {2, 11, 0, 0, 0},
                      {7, 22, 0, 0, 0}},
                  BalancedDegradationSettings::Config{
                      2000,
                      15,
                      0,
                      0,
                      BalancedDegradationSettings::kNoFpsDiff,
                      {90, 91, 0, 0, 0},
                      {28, 130, 0, 0, 0},
                      {13, 30, 0, 0, 0},
                      {3, 33, 0, 0, 0},
                      {6, 23, 0, 0, 0}},
                  BalancedDegradationSettings::Config{
                      3000,
                      25,
                      0,
                      0,
                      BalancedDegradationSettings::kNoFpsDiff,
                      {88, 92, 0, 0, 0},
                      {29, 140, 0, 0, 0},
                      {14, 40, 0, 0, 0},
                      {4, 44, 0, 0, 0},
                      {5, 24, 0, 0, 0}}));
}

TEST(BalancedDegradationSettings, GetsDefaultConfigIfOnlyHasLowThreshold) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000,fps:5|15|25,vp8_qp_low:89|90|88/");
  BalancedDegradationSettings settings(field_trials);
  VerifyIsDefault(settings.GetConfigs());
}

TEST(BalancedDegradationSettings, GetsDefaultConfigIfOnlyHasHighThreshold) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000,fps:5|15|25,vp8_qp_high:90|91|92/");
  BalancedDegradationSettings settings(field_trials);
  VerifyIsDefault(settings.GetConfigs());
}

TEST(BalancedDegradationSettings, GetsDefaultConfigIfLowEqualsHigh) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000,fps:5|15|25,"
      "vp8_qp_low:89|90|88,vp8_qp_high:90|91|88/");
  BalancedDegradationSettings settings(field_trials);
  VerifyIsDefault(settings.GetConfigs());
}

TEST(BalancedDegradationSettings, GetsDefaultConfigIfLowGreaterThanHigh) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000,fps:5|15|25,"
      "vp8_qp_low:89|90|88,vp8_qp_high:90|91|87/");
  BalancedDegradationSettings settings(field_trials);
  VerifyIsDefault(settings.GetConfigs());
}

TEST(BalancedDegradationSettings, GetsDefaultConfigForZeroQpValue) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000,fps:5|15|25,"
      "vp8_qp_low:89|0|88,vp8_qp_high:90|91|92/");
  BalancedDegradationSettings settings(field_trials);
  VerifyIsDefault(settings.GetConfigs());
}

TEST(BalancedDegradationSettings, GetsVp8QpThresholds) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000,fps:5|15|25,"
      "vp8_qp_low:89|90|88,vp8_qp_high:90|91|92/");
  BalancedDegradationSettings settings(field_trials);
  EXPECT_EQ(89, settings.GetQpThresholds(kVideoCodecVP8, 1)->low);
  EXPECT_EQ(90, settings.GetQpThresholds(kVideoCodecVP8, 1)->high);
  EXPECT_EQ(90, settings.GetQpThresholds(kVideoCodecVP8, 1000)->high);
  EXPECT_EQ(91, settings.GetQpThresholds(kVideoCodecVP8, 1001)->high);
  EXPECT_EQ(91, settings.GetQpThresholds(kVideoCodecVP8, 2000)->high);
  EXPECT_EQ(92, settings.GetQpThresholds(kVideoCodecVP8, 2001)->high);
  EXPECT_EQ(92, settings.GetQpThresholds(kVideoCodecVP8, 3000)->high);
  EXPECT_EQ(92, settings.GetQpThresholds(kVideoCodecVP8, 3001)->high);
}

TEST(BalancedDegradationSettings, GetsVp9QpThresholds) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000,fps:5|15|25,"
      "vp9_qp_low:55|56|57,vp9_qp_high:155|156|157/");
  BalancedDegradationSettings settings(field_trials);
  const auto thresholds = settings.GetQpThresholds(kVideoCodecVP9, 1000);
  EXPECT_TRUE(thresholds);
  EXPECT_EQ(55, thresholds->low);
  EXPECT_EQ(155, thresholds->high);
}

TEST(BalancedDegradationSettings, GetsH264QpThresholds) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000,fps:5|15|25,"
      "h264_qp_low:21|22|23,h264_qp_high:41|43|42/");
  BalancedDegradationSettings settings(field_trials);
  const auto thresholds = settings.GetQpThresholds(kVideoCodecH264, 2000);
  EXPECT_TRUE(thresholds);
  EXPECT_EQ(22, thresholds->low);
  EXPECT_EQ(43, thresholds->high);
}

TEST(BalancedDegradationSettings, GetsGenericQpThresholds) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-Video-BalancedDegradationSettings/"
      "pixels:1000|2000|3000,fps:5|15|25,"
      "generic_qp_low:2|3|4,generic_qp_high:22|23|24/");
  BalancedDegradationSettings settings(field_trials);
  const auto thresholds = settings.GetQpThresholds(kVideoCodecGeneric, 3000);
  EXPECT_TRUE(thresholds);
  EXPECT_EQ(4, thresholds->low);
  EXPECT_EQ(24, thresholds->high);
}

}  // namespace webrtc
