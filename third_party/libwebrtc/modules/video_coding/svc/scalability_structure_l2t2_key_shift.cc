/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "modules/video_coding/svc/scalability_structure_l2t2_key_shift.h"

#include <vector>

#include "api/transport/rtp/dependency_descriptor.h"
#include "api/video/video_bitrate_allocation.h"
#include "common_video/generic_frame_descriptor/generic_frame_info.h"
#include "modules/video_coding/svc/scalable_video_controller.h"
#include "rtc_base/checks.h"

namespace webrtc {
namespace {

DecodeTargetIndication
Dti(int sid, int tid, const ScalableVideoController::LayerFrameConfig& config) {
  if (config.IsKeyframe()) {
    RTC_DCHECK_EQ(config.TemporalId(), 0);
    return sid < config.SpatialId() ? DecodeTargetIndication::kNotPresent
                                    : DecodeTargetIndication::kSwitch;
  }

  if (sid != config.SpatialId() || tid < config.TemporalId()) {
    return DecodeTargetIndication::kNotPresent;
  }
  if (tid == config.TemporalId() && tid > 0) {
    return DecodeTargetIndication::kDiscardable;
  }
  return DecodeTargetIndication::kSwitch;
}

}  // namespace

ScalabilityStructureL2T2KeyShift::~ScalabilityStructureL2T2KeyShift() = default;

ScalableVideoController::StreamLayersConfig
ScalabilityStructureL2T2KeyShift::StreamConfig() const {
  StreamLayersConfig result;
  result.num_spatial_layers = 2;
  result.num_temporal_layers = 2;
  result.scaling_factor_num[0] = 1;
  result.scaling_factor_den[0] = 2;
  result.uses_reference_scaling = true;
  return result;
}

FrameDependencyStructure ScalabilityStructureL2T2KeyShift::DependencyStructure()
    const {
  FrameDependencyStructure structure;
  structure.num_decode_targets = 4;
  structure.num_chains = 2;
  structure.decode_target_protected_by_chain = {0, 0, 1, 1};
  structure.templates.resize(7);
  auto& templates = structure.templates;
  templates[0].S(0).T(0).Dtis("SSSS").ChainDiffs({0, 0});
  templates[1].S(0).T(0).Dtis("SS--").ChainDiffs({2, 1}).FrameDiffs({2});
  templates[2].S(0).T(0).Dtis("SS--").ChainDiffs({4, 1}).FrameDiffs({4});
  templates[3].S(0).T(1).Dtis("-D--").ChainDiffs({2, 3}).FrameDiffs({2});
  templates[4].S(1).T(0).Dtis("--SS").ChainDiffs({1, 1}).FrameDiffs({1});
  templates[5].S(1).T(0).Dtis("--SS").ChainDiffs({3, 4}).FrameDiffs({4});
  templates[6].S(1).T(1).Dtis("---D").ChainDiffs({1, 2}).FrameDiffs({2});
  return structure;
}

ScalabilityStructureL2T2KeyShift::FramePattern
ScalabilityStructureL2T2KeyShift::NextPattern() const {
  if (last_pattern_ == kNone) {
    return kKey;
  }

  const bool s0_active = DecodeTargetIsActive(/*sid=*/0, /*tid=*/0);
  const bool s1_active = DecodeTargetIsActive(/*sid=*/1, /*tid=*/0);
  const bool s0_has_t0_ref = can_reference_t0_frame_for_spatial_id_[0];
  const bool s1_has_t0_ref = can_reference_t0_frame_for_spatial_id_[1];

  const bool any_active_layer_has_t0_ref =
      (s0_active && s0_has_t0_ref) || (s1_active && s1_has_t0_ref);
  if (!any_active_layer_has_t0_ref) {
    return kKey;
  }

  FramePattern next_pattern =
      (last_pattern_ == kKey || last_pattern_ == kDelta1) ? kDelta0 : kDelta1;

  // Each delta pattern updates T0 for one spatial layer (S0 in kDelta0, S1 in
  // kDelta1). If that layer is active but lacks a T0 reference, emit a keyframe
  // to initialize it.
  if (next_pattern == kDelta0 && s0_active && !s0_has_t0_ref) {
    return kKey;
  }
  if (next_pattern == kDelta1 && s1_active && !s1_has_t0_ref) {
    return kKey;
  }

  return next_pattern;
}

std::vector<ScalableVideoController::LayerFrameConfig>
ScalabilityStructureL2T2KeyShift::NextFrameConfig(bool restart) {
  std::vector<LayerFrameConfig> configs;
  if (active_decode_targets_.none()) {
    last_pattern_ = kNone;
    return configs;
  }
  configs.reserve(2);
  if (restart) {
    last_pattern_ = kNone;
  }

  FramePattern current_pattern = NextPattern();
  // Buffer0 keeps latest S0T0 frame,
  // Buffer1 keeps latest S1T0 frame.
  switch (current_pattern) {
    case kKey:
      can_reference_t0_frame_for_spatial_id_.reset();
      if (DecodeTargetIsActive(/*sid=*/0, /*tid=*/0)) {
        configs.emplace_back();
        configs.back().Id(kKey).S(0).T(0).Update(0).Keyframe();
      }
      if (DecodeTargetIsActive(/*sid=*/1, /*tid=*/0)) {
        configs.emplace_back();
        configs.back().Id(kKey).S(1).T(0).Update(1);
        if (DecodeTargetIsActive(/*sid=*/0, /*tid=*/0)) {
          configs.back().Reference(0);
        } else {
          configs.back().Keyframe();
        }
      }
      break;
    case kDelta0:
      if (DecodeTargetIsActive(/*sid=*/0, /*tid=*/0)) {
        RTC_DCHECK(can_reference_t0_frame_for_spatial_id_[0]);
        configs.emplace_back();
        configs.back().Id(kDelta0).S(0).T(0).ReferenceAndUpdate(0);
      }
      if (DecodeTargetIsActive(/*sid=*/1, /*tid=*/1) &&
          can_reference_t0_frame_for_spatial_id_[1]) {
        configs.emplace_back();
        configs.back().Id(kDelta0).S(1).T(1).Reference(1);
      }
      if (configs.empty() && DecodeTargetIsActive(/*sid=*/1, /*tid=*/0)) {
        RTC_DCHECK(can_reference_t0_frame_for_spatial_id_[1]);
        configs.emplace_back();
        configs.back().Id(kDelta0).S(1).T(0).ReferenceAndUpdate(1);
      }
      break;
    case kDelta1:
      if (DecodeTargetIsActive(/*sid=*/0, /*tid=*/1) &&
          can_reference_t0_frame_for_spatial_id_[0]) {
        configs.emplace_back();
        configs.back().Id(kDelta1).S(0).T(1).Reference(0);
      }
      if (DecodeTargetIsActive(/*sid=*/1, /*tid=*/0)) {
        RTC_DCHECK(can_reference_t0_frame_for_spatial_id_[1]);
        configs.emplace_back();
        configs.back().Id(kDelta1).S(1).T(0).ReferenceAndUpdate(1);
      }
      if (configs.empty() && DecodeTargetIsActive(/*sid=*/0, /*tid=*/0)) {
        RTC_DCHECK(can_reference_t0_frame_for_spatial_id_[0]);
        configs.emplace_back();
        configs.back().Id(kDelta1).S(0).T(0).ReferenceAndUpdate(0);
      }
      break;
    case kNone:
      RTC_DCHECK_NOTREACHED();
      break;
  }

  RTC_DCHECK(!configs.empty());
  return configs;
}

void ScalabilityStructureL2T2KeyShift::OnFrameDropped(
    const LayerFrameConfig& config) {
  last_pattern_ = static_cast<FramePattern>(config.Id());
}

GenericFrameInfo ScalabilityStructureL2T2KeyShift::OnEncodeDone(
    const LayerFrameConfig& config) {
  last_pattern_ = static_cast<FramePattern>(config.Id());
  if (config.TemporalId() == 0) {
    can_reference_t0_frame_for_spatial_id_.set(config.SpatialId());
  }
  GenericFrameInfo frame_info;
  frame_info.spatial_id = config.SpatialId();
  frame_info.temporal_id = config.TemporalId();
  frame_info.encoder_buffers = config.Buffers();
  for (int sid = 0; sid < kNumSpatialLayers; ++sid) {
    for (int tid = 0; tid < kNumTemporalLayers; ++tid) {
      frame_info.decode_target_indications.push_back(Dti(sid, tid, config));
    }
  }
  if (config.IsKeyframe()) {
    frame_info.part_of_chain = {true, true};
  } else if (config.TemporalId() == 0) {
    frame_info.part_of_chain = {config.SpatialId() == 0,
                                config.SpatialId() == 1};
  } else {
    frame_info.part_of_chain = {false, false};
  }
  return frame_info;
}

void ScalabilityStructureL2T2KeyShift::OnRatesUpdated(
    const VideoBitrateAllocation& bitrates) {
  for (int sid = 0; sid < kNumSpatialLayers; ++sid) {
    // Enable/disable spatial layers independetely.
    bool active = bitrates.GetBitrate(sid, /*tid=*/0) > 0;
    if (!active) {
      can_reference_t0_frame_for_spatial_id_.reset(sid);
    } else if (!DecodeTargetIsActive(sid, /*tid=*/0)) {
      // Key frame is required to reenable any spatial layer.
      last_pattern_ = kNone;
    }

    SetDecodeTargetIsActive(sid, /*tid=*/0, active);
    SetDecodeTargetIsActive(sid, /*tid=*/1,
                            active && bitrates.GetBitrate(sid, /*tid=*/1) > 0);
  }
}

}  // namespace webrtc
