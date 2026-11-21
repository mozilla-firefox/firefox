/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_coding/svc/scalability_mode_util.h"

#include <string>
#include <tuple>
#include <vector>

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "api/video_codecs/scalability_mode.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {

namespace {

TEST(ScalabilityModeUtil, ConvertsL1T2) {
  EXPECT_EQ(ScalabilityModeFromString("L1T2"), ScalabilityMode::kL1T2);
  EXPECT_EQ(ScalabilityModeToString(ScalabilityMode::kL1T2), "L1T2");
}

TEST(ScalabilityModeUtil, RejectsUnknownString) {
  EXPECT_EQ(ScalabilityModeFromString(""), absl::nullopt);
  EXPECT_EQ(ScalabilityModeFromString("not-a-mode"), absl::nullopt);
}

TEST(ScalabilityModeUtil, MakeScalabilityModeRoundTrip) {
  const ScalabilityMode kLastEnum = ScalabilityMode::kS3T3h;
  for (int numerical_enum = 0; numerical_enum <= static_cast<int>(kLastEnum);
       numerical_enum++) {
    ScalabilityMode scalability_mode =
        static_cast<ScalabilityMode>(numerical_enum);
    absl::optional<ScalabilityMode> created_mode = MakeScalabilityMode(
        ScalabilityModeToNumSpatialLayers(scalability_mode),
        ScalabilityModeToNumTemporalLayers(scalability_mode),
        ScalabilityModeToInterLayerPredMode(scalability_mode),
        ScalabilityModeToResolutionRatio(scalability_mode),
        ScalabilityModeIsShiftMode(scalability_mode));
    EXPECT_THAT(created_mode, ::testing::Optional(scalability_mode))
        << "Expected "
        << (created_mode.has_value() ? ScalabilityModeToString(*created_mode)
                                     : "(nullopt)")
        << " to equal " << ScalabilityModeToString(scalability_mode);
  }
}

// Check roundtrip conversion of all enum values.
TEST(ScalabilityModeUtil, ConvertsAllToAndFromString) {
  const ScalabilityMode kLastEnum = ScalabilityMode::kS3T3h;
  for (int numerical_enum = 0; numerical_enum <= static_cast<int>(kLastEnum);
       numerical_enum++) {
    ScalabilityMode scalability_mode =
        static_cast<ScalabilityMode>(numerical_enum);
    absl::string_view scalability_mode_string =
        ScalabilityModeToString(scalability_mode);
    EXPECT_FALSE(scalability_mode_string.empty());
    EXPECT_EQ(ScalabilityModeFromString(scalability_mode_string),
              scalability_mode);
  }
}

struct TestParams {
  std::string scalability_mode;
  std::vector<std::tuple<std::vector<int>, std::string>>
      limited_scalability_mode;
};

class NumSpatialLayersTest : public ::testing::TestWithParam<TestParams> {};

INSTANTIATE_TEST_SUITE_P(
    MaxLayers,
    NumSpatialLayersTest,
    ::testing::ValuesIn<TestParams>(
        {{"L1T1", {{{0, 1}, "L1T1"}, {{2}, "L1T1"}, {{3}, "L1T1"}}},
         {"L1T2", {{{0, 1}, "L1T2"}, {{2}, "L1T2"}, {{3}, "L1T2"}}},
         {"L1T3", {{{0, 1}, "L1T3"}, {{2}, "L1T3"}, {{3}, "L1T3"}}},
         {"L2T1", {{{0, 1}, "L1T1"}, {{2}, "L2T1"}, {{3}, "L2T1"}}},
         {"L2T1h", {{{0, 1}, "L1T1"}, {{2}, "L2T1h"}, {{3}, "L2T1h"}}},
         {"L2T1_KEY", {{{0, 1}, "L1T1"}, {{2}, "L2T1_KEY"}, {{3}, "L2T1_KEY"}}},
         {"L2T2", {{{0, 1}, "L1T2"}, {{2}, "L2T2"}, {{3}, "L2T2"}}},
         {"L2T2h", {{{0, 1}, "L1T2"}, {{2}, "L2T2h"}, {{3}, "L2T2h"}}},
         {"L2T2_KEY", {{{0, 1}, "L1T2"}, {{2}, "L2T2_KEY"}, {{3}, "L2T2_KEY"}}},
         {"L2T2_KEY_SHIFT",
          {{{0, 1}, "L1T2"}, {{2}, "L2T2_KEY_SHIFT"}, {{3}, "L2T2_KEY_SHIFT"}}},
         {"L2T3", {{{0, 1}, "L1T3"}, {{2}, "L2T3"}, {{3}, "L2T3"}}},
         {"L2T3h", {{{0, 1}, "L1T3"}, {{2}, "L2T3h"}, {{3}, "L2T3h"}}},
         {"L2T3_KEY", {{{0, 1}, "L1T3"}, {{2}, "L2T3_KEY"}, {{3}, "L2T3_KEY"}}},
         {"L3T1", {{{0, 1}, "L1T1"}, {{2}, "L2T1"}, {{3}, "L3T1"}}},
         {"L3T1h", {{{0, 1}, "L1T1"}, {{2}, "L2T1h"}, {{3}, "L3T1h"}}},
         {"L3T1_KEY", {{{0, 1}, "L1T1"}, {{2}, "L2T1_KEY"}, {{3}, "L3T1_KEY"}}},
         {"L3T2", {{{0, 1}, "L1T2"}, {{2}, "L2T2"}, {{3}, "L3T2"}}},
         {"L3T2h", {{{0, 1}, "L1T2"}, {{2}, "L2T2h"}, {{3}, "L3T2h"}}},
         {"L3T2_KEY", {{{0, 1}, "L1T2"}, {{2}, "L2T2_KEY"}, {{3}, "L3T2_KEY"}}},
         {"L3T3", {{{0, 1}, "L1T3"}, {{2}, "L2T3"}, {{3}, "L3T3"}}},
         {"L3T3h", {{{0, 1}, "L1T3"}, {{2}, "L2T3h"}, {{3}, "L3T3h"}}},
         {"L3T3_KEY", {{{0, 1}, "L1T3"}, {{2}, "L2T3_KEY"}, {{3}, "L3T3_KEY"}}},
         {"S2T1", {{{0, 1}, "L1T1"}, {{2}, "S2T1"}, {{3}, "S2T1"}}},
         {"S2T1h", {{{0, 1}, "L1T1"}, {{2}, "S2T1h"}, {{3}, "S2T1h"}}},
         {"S2T2", {{{0, 1}, "L1T2"}, {{2}, "S2T2"}, {{3}, "S2T2"}}},
         {"S2T2h", {{{0, 1}, "L1T2"}, {{2}, "S2T2h"}, {{3}, "S2T2h"}}},
         {"S2T3", {{{0, 1}, "L1T3"}, {{2}, "S2T3"}, {{3}, "S2T3"}}},
         {"S2T3h", {{{0, 1}, "L1T3"}, {{2}, "S2T3h"}, {{3}, "S2T3h"}}},
         {"S3T1", {{{0, 1}, "L1T1"}, {{2}, "S2T1"}, {{3}, "S3T1"}}},
         {"S3T1h", {{{0, 1}, "L1T1"}, {{2}, "S2T1h"}, {{3}, "S3T1h"}}},
         {"S3T2", {{{0, 1}, "L1T2"}, {{2}, "S2T2"}, {{3}, "S3T2"}}},
         {"S3T2h", {{{0, 1}, "L1T2"}, {{2}, "S2T2h"}, {{3}, "S3T2h"}}},
         {"S3T3", {{{0, 1}, "L1T3"}, {{2}, "S2T3"}, {{3}, "S3T3"}}},
         {"S3T3h", {{{0, 1}, "L1T3"}, {{2}, "S2T3h"}, {{3}, "S3T3h"}}}}),
    [](const ::testing::TestParamInfo<TestParams>& info) {
      return info.param.scalability_mode;
    });

TEST_P(NumSpatialLayersTest, LimitsSpatialLayers) {
  const ScalabilityMode mode =
      *ScalabilityModeFromString(GetParam().scalability_mode);
  for (const auto& param : GetParam().limited_scalability_mode) {
    const std::vector<int> max_num_spatial_layers =
        std::get<std::vector<int>>(param);
    const ScalabilityMode expected_mode =
        *ScalabilityModeFromString(std::get<std::string>(param));
    for (const auto& max_layers : max_num_spatial_layers) {
      EXPECT_EQ(expected_mode, LimitNumSpatialLayers(mode, max_layers));
    }
  }
}

}  // namespace
}  // namespace webrtc
