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

#include <array>
#include <cstddef>
#include <type_traits>

#include "absl/algorithm/container.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "api/video_codecs/scalability_mode.h"
#include "api/video_codecs/video_codec.h"
#include "rtc_base/checks.h"

namespace webrtc {

namespace {

struct ScalabilityModeParameters {
  const ScalabilityMode scalability_mode;
  const absl::string_view name;
  const int num_spatial_layers;
  const int num_temporal_layers;
  const InterLayerPredMode inter_layer_pred;
  const absl::optional<ScalabilityModeResolutionRatio> ratio =
      ScalabilityModeResolutionRatio::kTwoToOne;
  const bool shift = false;
};

constexpr size_t kNumScalabilityModes =
    static_cast<size_t>(ScalabilityMode::kS3T3h) + 1;

constexpr ScalabilityModeParameters kScalabilityModeParams[] = {
    ScalabilityModeParameters{.scalability_mode = ScalabilityMode::kL1T1,
                              .name = "L1T1",
                              .num_spatial_layers = 1,
                              .num_temporal_layers = 1,
                              .inter_layer_pred = InterLayerPredMode::kOff,
                              .ratio = absl::nullopt},
    ScalabilityModeParameters{.scalability_mode = ScalabilityMode::kL1T2,
                              .name = "L1T2",
                              .num_spatial_layers = 1,
                              .num_temporal_layers = 2,
                              .inter_layer_pred = InterLayerPredMode::kOff,
                              .ratio = absl::nullopt},
    ScalabilityModeParameters{.scalability_mode = ScalabilityMode::kL1T3,
                              .name = "L1T3",
                              .num_spatial_layers = 1,
                              .num_temporal_layers = 3,
                              .inter_layer_pred = InterLayerPredMode::kOff,
                              .ratio = absl::nullopt},
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kL2T1,
        .name = "L2T1",
        .num_spatial_layers = 2,
        .num_temporal_layers = 1,
        .inter_layer_pred = InterLayerPredMode::kOn,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kL2T1h,
        .name = "L2T1h",
        .num_spatial_layers = 2,
        .num_temporal_layers = 1,
        .inter_layer_pred = InterLayerPredMode::kOn,
        .ratio = ScalabilityModeResolutionRatio::kThreeToTwo,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kL2T1_KEY,
        .name = "L2T1_KEY",
        .num_spatial_layers = 2,
        .num_temporal_layers = 1,
        .inter_layer_pred = InterLayerPredMode::kOnKeyPic,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kL2T2,
        .name = "L2T2",
        .num_spatial_layers = 2,
        .num_temporal_layers = 2,
        .inter_layer_pred = InterLayerPredMode::kOn,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kL2T2h,
        .name = "L2T2h",
        .num_spatial_layers = 2,
        .num_temporal_layers = 2,
        .inter_layer_pred = InterLayerPredMode::kOn,
        .ratio = ScalabilityModeResolutionRatio::kThreeToTwo,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kL2T2_KEY,
        .name = "L2T2_KEY",
        .num_spatial_layers = 2,
        .num_temporal_layers = 2,
        .inter_layer_pred = InterLayerPredMode::kOnKeyPic,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kL2T2_KEY_SHIFT,
        .name = "L2T2_KEY_SHIFT",
        .num_spatial_layers = 2,
        .num_temporal_layers = 2,
        .inter_layer_pred = InterLayerPredMode::kOnKeyPic,
        .shift = true},
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kL2T3,
        .name = "L2T3",
        .num_spatial_layers = 2,
        .num_temporal_layers = 3,
        .inter_layer_pred = InterLayerPredMode::kOn,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kL2T3h,
        .name = "L2T3h",
        .num_spatial_layers = 2,
        .num_temporal_layers = 3,
        .inter_layer_pred = InterLayerPredMode::kOn,
        .ratio = ScalabilityModeResolutionRatio::kThreeToTwo,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kL2T3_KEY,
        .name = "L2T3_KEY",
        .num_spatial_layers = 2,
        .num_temporal_layers = 3,
        .inter_layer_pred = InterLayerPredMode::kOnKeyPic,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kL3T1,
        .name = "L3T1",
        .num_spatial_layers = 3,
        .num_temporal_layers = 1,
        .inter_layer_pred = InterLayerPredMode::kOn,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kL3T1h,
        .name = "L3T1h",
        .num_spatial_layers = 3,
        .num_temporal_layers = 1,
        .inter_layer_pred = InterLayerPredMode::kOn,
        .ratio = ScalabilityModeResolutionRatio::kThreeToTwo,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kL3T1_KEY,
        .name = "L3T1_KEY",
        .num_spatial_layers = 3,
        .num_temporal_layers = 1,
        .inter_layer_pred = InterLayerPredMode::kOnKeyPic,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kL3T2,
        .name = "L3T2",
        .num_spatial_layers = 3,
        .num_temporal_layers = 2,
        .inter_layer_pred = InterLayerPredMode::kOn,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kL3T2h,
        .name = "L3T2h",
        .num_spatial_layers = 3,
        .num_temporal_layers = 2,
        .inter_layer_pred = InterLayerPredMode::kOn,
        .ratio = ScalabilityModeResolutionRatio::kThreeToTwo,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kL3T2_KEY,
        .name = "L3T2_KEY",
        .num_spatial_layers = 3,
        .num_temporal_layers = 2,
        .inter_layer_pred = InterLayerPredMode::kOnKeyPic,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kL3T3,
        .name = "L3T3",
        .num_spatial_layers = 3,
        .num_temporal_layers = 3,
        .inter_layer_pred = InterLayerPredMode::kOn,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kL3T3h,
        .name = "L3T3h",
        .num_spatial_layers = 3,
        .num_temporal_layers = 3,
        .inter_layer_pred = InterLayerPredMode::kOn,
        .ratio = ScalabilityModeResolutionRatio::kThreeToTwo,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kL3T3_KEY,
        .name = "L3T3_KEY",
        .num_spatial_layers = 3,
        .num_temporal_layers = 3,
        .inter_layer_pred = InterLayerPredMode::kOnKeyPic,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kS2T1,
        .name = "S2T1",
        .num_spatial_layers = 2,
        .num_temporal_layers = 1,
        .inter_layer_pred = InterLayerPredMode::kOff,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kS2T1h,
        .name = "S2T1h",
        .num_spatial_layers = 2,
        .num_temporal_layers = 1,
        .inter_layer_pred = InterLayerPredMode::kOff,
        .ratio = ScalabilityModeResolutionRatio::kThreeToTwo,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kS2T2,
        .name = "S2T2",
        .num_spatial_layers = 2,
        .num_temporal_layers = 2,
        .inter_layer_pred = InterLayerPredMode::kOff,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kS2T2h,
        .name = "S2T2h",
        .num_spatial_layers = 2,
        .num_temporal_layers = 2,
        .inter_layer_pred = InterLayerPredMode::kOff,
        .ratio = ScalabilityModeResolutionRatio::kThreeToTwo,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kS2T3,
        .name = "S2T3",
        .num_spatial_layers = 2,
        .num_temporal_layers = 3,
        .inter_layer_pred = InterLayerPredMode::kOff,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kS2T3h,
        .name = "S2T3h",
        .num_spatial_layers = 2,
        .num_temporal_layers = 3,
        .inter_layer_pred = InterLayerPredMode::kOff,
        .ratio = ScalabilityModeResolutionRatio::kThreeToTwo,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kS3T1,
        .name = "S3T1",
        .num_spatial_layers = 3,
        .num_temporal_layers = 1,
        .inter_layer_pred = InterLayerPredMode::kOff,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kS3T1h,
        .name = "S3T1h",
        .num_spatial_layers = 3,
        .num_temporal_layers = 1,
        .inter_layer_pred = InterLayerPredMode::kOff,
        .ratio = ScalabilityModeResolutionRatio::kThreeToTwo,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kS3T2,
        .name = "S3T2",
        .num_spatial_layers = 3,
        .num_temporal_layers = 2,
        .inter_layer_pred = InterLayerPredMode::kOff,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kS3T2h,
        .name = "S3T2h",
        .num_spatial_layers = 3,
        .num_temporal_layers = 2,
        .inter_layer_pred = InterLayerPredMode::kOff,
        .ratio = ScalabilityModeResolutionRatio::kThreeToTwo,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kS3T3,
        .name = "S3T3",
        .num_spatial_layers = 3,
        .num_temporal_layers = 3,
        .inter_layer_pred = InterLayerPredMode::kOff,
    },
    ScalabilityModeParameters{
        .scalability_mode = ScalabilityMode::kS3T3h,
        .name = "S3T3h",
        .num_spatial_layers = 3,
        .num_temporal_layers = 3,
        .inter_layer_pred = InterLayerPredMode::kOff,
        .ratio = ScalabilityModeResolutionRatio::kThreeToTwo,
    },
};

// This could be replaced with std::all_of in c++20.
constexpr bool CheckScalabilityModeParams() {
  static_assert(std::size(kScalabilityModeParams) == kNumScalabilityModes);
  for (size_t s = 0; s < kNumScalabilityModes; ++s) {
    if (kScalabilityModeParams[s].scalability_mode !=
        static_cast<ScalabilityMode>(s)) {
      return false;
    }
  }
  return true;
}

static_assert(CheckScalabilityModeParams(),
              "There is a scalability mode mismatch in the array!");

constexpr auto Idx(ScalabilityMode s) {
  const auto index = static_cast<std::underlying_type_t<ScalabilityMode>>(s);
  RTC_CHECK_LT(index, kNumScalabilityModes);
  return index;
}

}  // namespace

absl::optional<ScalabilityMode> MakeScalabilityMode(
    int num_spatial_layers,
    int num_temporal_layers,
    InterLayerPredMode inter_layer_pred,
    absl::optional<ScalabilityModeResolutionRatio> ratio,
    bool shift) {
  for (const auto& candidate_mode : kScalabilityModeParams) {
    if (candidate_mode.num_spatial_layers == num_spatial_layers &&
        candidate_mode.num_temporal_layers == num_temporal_layers) {
      if (num_spatial_layers == 1 ||
          (candidate_mode.inter_layer_pred == inter_layer_pred &&
           candidate_mode.ratio == ratio && candidate_mode.shift == shift)) {
        return candidate_mode.scalability_mode;
      }
    }
  }
  return absl::nullopt;
}

absl::optional<ScalabilityMode> ScalabilityModeFromString(
    absl::string_view mode_string) {
  const auto it =
      absl::c_find_if(kScalabilityModeParams,
                      [&](const ScalabilityModeParameters& candidate_mode) {
                        return candidate_mode.name == mode_string;
                      });
  if (it != std::end(kScalabilityModeParams)) {
    return it->scalability_mode;
  }
  return absl::nullopt;
}

InterLayerPredMode ScalabilityModeToInterLayerPredMode(
    ScalabilityMode scalability_mode) {
  return kScalabilityModeParams[Idx(scalability_mode)].inter_layer_pred;
}

int ScalabilityModeToNumSpatialLayers(ScalabilityMode scalability_mode) {
  return kScalabilityModeParams[Idx(scalability_mode)].num_spatial_layers;
}

int ScalabilityModeToNumTemporalLayers(ScalabilityMode scalability_mode) {
  return kScalabilityModeParams[Idx(scalability_mode)].num_temporal_layers;
}

absl::optional<ScalabilityModeResolutionRatio> ScalabilityModeToResolutionRatio(
    ScalabilityMode scalability_mode) {
  return kScalabilityModeParams[Idx(scalability_mode)].ratio;
}

ScalabilityMode LimitNumSpatialLayers(ScalabilityMode scalability_mode,
                                      int max_spatial_layers) {
  int num_spatial_layers = ScalabilityModeToNumSpatialLayers(scalability_mode);
  if (max_spatial_layers >= num_spatial_layers) {
    return scalability_mode;
  }

  switch (scalability_mode) {
    case ScalabilityMode::kL1T1:
      return ScalabilityMode::kL1T1;
    case ScalabilityMode::kL1T2:
      return ScalabilityMode::kL1T2;
    case ScalabilityMode::kL1T3:
      return ScalabilityMode::kL1T3;
    case ScalabilityMode::kL2T1:
      return ScalabilityMode::kL1T1;
    case ScalabilityMode::kL2T1h:
      return ScalabilityMode::kL1T1;
    case ScalabilityMode::kL2T1_KEY:
      return ScalabilityMode::kL1T1;
    case ScalabilityMode::kL2T2:
      return ScalabilityMode::kL1T2;
    case ScalabilityMode::kL2T2h:
      return ScalabilityMode::kL1T2;
    case ScalabilityMode::kL2T2_KEY:
      return ScalabilityMode::kL1T2;
    case ScalabilityMode::kL2T2_KEY_SHIFT:
      return ScalabilityMode::kL1T2;
    case ScalabilityMode::kL2T3:
      return ScalabilityMode::kL1T3;
    case ScalabilityMode::kL2T3h:
      return ScalabilityMode::kL1T3;
    case ScalabilityMode::kL2T3_KEY:
      return ScalabilityMode::kL1T3;
    case ScalabilityMode::kL3T1:
      return max_spatial_layers == 2 ? ScalabilityMode::kL2T1
                                     : ScalabilityMode::kL1T1;
    case ScalabilityMode::kL3T1h:
      return max_spatial_layers == 2 ? ScalabilityMode::kL2T1h
                                     : ScalabilityMode::kL1T1;
    case ScalabilityMode::kL3T1_KEY:
      return max_spatial_layers == 2 ? ScalabilityMode::kL2T1_KEY
                                     : ScalabilityMode::kL1T1;
    case ScalabilityMode::kL3T2:
      return max_spatial_layers == 2 ? ScalabilityMode::kL2T2
                                     : ScalabilityMode::kL1T2;
    case ScalabilityMode::kL3T2h:
      return max_spatial_layers == 2 ? ScalabilityMode::kL2T2h
                                     : ScalabilityMode::kL1T2;
    case ScalabilityMode::kL3T2_KEY:
      return max_spatial_layers == 2 ? ScalabilityMode::kL2T2_KEY
                                     : ScalabilityMode::kL1T2;
    case ScalabilityMode::kL3T3:
      return max_spatial_layers == 2 ? ScalabilityMode::kL2T3
                                     : ScalabilityMode::kL1T3;
    case ScalabilityMode::kL3T3h:
      return max_spatial_layers == 2 ? ScalabilityMode::kL2T3h
                                     : ScalabilityMode::kL1T3;
    case ScalabilityMode::kL3T3_KEY:
      return max_spatial_layers == 2 ? ScalabilityMode::kL2T3_KEY
                                     : ScalabilityMode::kL1T3;
    case ScalabilityMode::kS2T1:
      return ScalabilityMode::kL1T1;
    case ScalabilityMode::kS2T1h:
      return ScalabilityMode::kL1T1;
    case ScalabilityMode::kS2T2:
      return ScalabilityMode::kL1T2;
    case ScalabilityMode::kS2T2h:
      return ScalabilityMode::kL1T2;
    case ScalabilityMode::kS2T3:
      return ScalabilityMode::kL1T3;
    case ScalabilityMode::kS2T3h:
      return ScalabilityMode::kL1T3;
    case ScalabilityMode::kS3T1:
      return max_spatial_layers == 2 ? ScalabilityMode::kS2T1
                                     : ScalabilityMode::kL1T1;
    case ScalabilityMode::kS3T1h:
      return max_spatial_layers == 2 ? ScalabilityMode::kS2T1h
                                     : ScalabilityMode::kL1T1;
    case ScalabilityMode::kS3T2:
      return max_spatial_layers == 2 ? ScalabilityMode::kS2T2
                                     : ScalabilityMode::kL1T2;
    case ScalabilityMode::kS3T2h:
      return max_spatial_layers == 2 ? ScalabilityMode::kS2T2h
                                     : ScalabilityMode::kL1T2;
    case ScalabilityMode::kS3T3:
      return max_spatial_layers == 2 ? ScalabilityMode::kS2T3
                                     : ScalabilityMode::kL1T3;
    case ScalabilityMode::kS3T3h:
      return max_spatial_layers == 2 ? ScalabilityMode::kS2T3h
                                     : ScalabilityMode::kL1T3;
  }
  RTC_CHECK_NOTREACHED();
}

bool ScalabilityModeIsShiftMode(ScalabilityMode scalability_mode) {
  return kScalabilityModeParams[Idx(scalability_mode)].shift;
}

}  // namespace webrtc
