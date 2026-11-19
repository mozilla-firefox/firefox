/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <ostream>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "api/transport/rtp/dependency_descriptor.h"
#include "api/video/video_bitrate_allocation.h"
#include "api/video_codecs/scalability_mode.h"
#include "common_video/generic_frame_descriptor/generic_frame_info.h"
#include "modules/video_coding/svc/create_scalability_structure.h"
#include "modules/video_coding/svc/scalability_mode_util.h"
#include "modules/video_coding/svc/scalability_structure_test_helpers.h"
#include "modules/video_coding/svc/scalable_video_controller.h"
#include "rtc_base/checks.h"
#include "rtc_base/strings/string_builder.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

using ::testing::AllOf;
using ::testing::Contains;
using ::testing::Each;
using ::testing::ElementsAreArray;
using ::testing::Field;
using ::testing::Ge;
using ::testing::IsEmpty;
using ::testing::Le;
using ::testing::Lt;
using ::testing::Not;
using ::testing::NotNull;
using ::testing::SizeIs;
using ::testing::TestWithParam;
using ::testing::Values;
using LayerFrameConfig = ScalableVideoController::LayerFrameConfig;

std::string FrameDependencyTemplateToString(const FrameDependencyTemplate& t) {
  StringBuilder sb;
  sb << "S" << t.spatial_id << "T" << t.temporal_id;
  sb << ": dtis = ";
  for (const auto dtis : t.decode_target_indications) {
    switch (dtis) {
      case DecodeTargetIndication::kNotPresent:
        sb << "-";
        break;
      case DecodeTargetIndication::kDiscardable:
        sb << "D";
        break;
      case DecodeTargetIndication::kSwitch:
        sb << "S";
        break;
      case DecodeTargetIndication::kRequired:
        sb << "R";
        break;
      default:
        sb << "?";
        break;
    }
  }
  sb << ", frame diffs = { ";
  for (int d : t.frame_diffs) {
    sb << d << ", ";
  }
  sb << "}, chain diffs = { ";
  for (int d : t.chain_diffs) {
    sb << d << ", ";
  }
  sb << "}";
  return sb.Release();
}

struct SvcTestParam {
  friend std::ostream& operator<<(std::ostream& os, const SvcTestParam& param) {
    return os << param.name;
  }

  ScalabilityMode GetScalabilityMode() const {
    std::optional<ScalabilityMode> scalability_mode =
        ScalabilityModeFromString(name);
    RTC_CHECK(scalability_mode.has_value());
    return *scalability_mode;
  }

  std::string name;
  // Number of temporal units in a test cycle (~2 pattern periods). Keep small:
  // `OnFrameDropped*` tests generate O(N^2) frames and invariant checks are
  // quadratic in frame count (O(N^4) overall).
  int num_temporal_units;
};

class ScalabilityStructureTest : public TestWithParam<SvcTestParam> {};

void CheckDependencyDescriptorInvariants(
    const FrameDependencyStructure& structure,
    std::span<const GenericFrameInfo> frame_infos) {
  const int64_t num_frames = frame_infos.size();
  std::vector<std::set<int64_t>> full_deps(num_frames);

  for (int64_t frame_id = 0; frame_id < num_frames; ++frame_id) {
    const GenericFrameInfo& frame = frame_infos[frame_id];
    EXPECT_GE(frame.spatial_id, 0) << " for frame " << frame_id;
    EXPECT_GE(frame.temporal_id, 0) << " for frame " << frame_id;
    EXPECT_THAT(frame.decode_target_indications,
                SizeIs(structure.num_decode_targets))
        << " for frame " << frame_id;
    EXPECT_THAT(frame.part_of_chain, SizeIs(structure.num_chains))
        << " for frame " << frame_id;
    EXPECT_THAT(frame.chain_diffs, SizeIs(structure.num_chains))
        << " for frame " << frame_id;

    std::set<int64_t> all_base_frames;
    for (int frame_diff : frame.frame_diffs) {
      int64_t base_frame_id = frame_id - frame_diff;
      ASSERT_GE(base_frame_id, 0) << " for frame " << frame_id;
      ASSERT_LT(base_frame_id, frame_id) << " for frame " << frame_id;
      const GenericFrameInfo& base_frame = frame_infos[base_frame_id];
      EXPECT_GE(frame.spatial_id, base_frame.spatial_id)
          << "Frame " << frame_id << " depends on frame " << base_frame_id;
      EXPECT_GE(frame.temporal_id, base_frame.temporal_id)
          << "Frame " << frame_id << " depends on frame " << base_frame_id;

      all_base_frames.insert(base_frame_id);
      const std::set<int64_t>& indirect = full_deps[base_frame_id];
      all_base_frames.insert(indirect.begin(), indirect.end());
    }
    full_deps[frame_id] = std::move(all_base_frames);
  }

  for (int dt = 0; dt < structure.num_decode_targets; ++dt) {
    for (int64_t frame_id = 0; frame_id < num_frames; ++frame_id) {
      const GenericFrameInfo& frame = frame_infos[frame_id];
      if (frame.decode_target_indications[dt] ==
          DecodeTargetIndication::kNotPresent) {
        continue;
      }
      for (int frame_diff : frame.frame_diffs) {
        int64_t base_frame_id = frame_id - frame_diff;
        const GenericFrameInfo& base_frame = frame_infos[base_frame_id];
        EXPECT_NE(base_frame.decode_target_indications[dt],
                  DecodeTargetIndication::kNotPresent)
            << "Frame " << frame_id << " depends on frame " << base_frame_id
            << " that is not part of decode target#" << dt;
        EXPECT_NE(base_frame.decode_target_indications[dt],
                  DecodeTargetIndication::kDiscardable)
            << "Frame " << frame_id << " depends on frame " << base_frame_id
            << " that is discardable for decode target#" << dt;
      }
    }

    for (int64_t switch_frame_id = 0; switch_frame_id < num_frames;
         ++switch_frame_id) {
      if (frame_infos[switch_frame_id].decode_target_indications[dt] !=
          DecodeTargetIndication::kSwitch) {
        continue;
      }
      for (int64_t later_frame_id = switch_frame_id + 1;
           later_frame_id < num_frames; ++later_frame_id) {
        if (frame_infos[later_frame_id].decode_target_indications[dt] ==
            DecodeTargetIndication::kNotPresent) {
          continue;
        }
        for (int frame_diff : frame_infos[later_frame_id].frame_diffs) {
          int64_t early_frame_id = later_frame_id - frame_diff;
          if (early_frame_id < switch_frame_id) {
            EXPECT_THAT(full_deps[switch_frame_id], Contains(early_frame_id))
                << "For decode target #" << dt << " frame " << later_frame_id
                << " depends on the frame " << early_frame_id
                << " that switch indication frame " << switch_frame_id
                << " doesn't directly or indirectly depend on.";
          }
        }
      }

      // Verify chain integrity and protection: walking `chain_diffs` back must
      // hit only frames marked `part_of_chain`, and for chain-member `kSwitch`
      // frames (e.g. T0), the chain must cover all transitive dependencies.
      if (structure.num_chains > 0) {
        int chain_id = structure.decode_target_protected_by_chain[dt];
        std::set<int64_t> frames_in_chain;
        int64_t cur = switch_frame_id;
        while (frame_infos[cur].chain_diffs[chain_id] > 0) {
          int cdiff = frame_infos[cur].chain_diffs[chain_id];
          ASSERT_LE(cdiff, cur) << "Invalid chain_diff at frame " << cur;
          cur -= cdiff;
          EXPECT_TRUE(frame_infos[cur].part_of_chain[chain_id])
              << "Frame " << cur << " referenced by chain " << chain_id
              << " is not marked part_of_chain.";
          frames_in_chain.insert(cur);
        }
        if (frame_infos[switch_frame_id].part_of_chain[chain_id]) {
          for (int64_t dep_frame_id : full_deps[switch_frame_id]) {
            EXPECT_THAT(frames_in_chain, Contains(dep_frame_id))
                << "For decode target #" << dt << " (chain #" << chain_id
                << "), switch frame " << switch_frame_id << " depends on frame "
                << dep_frame_id << " which is not protected by the chain.";
          }
        }
      }
    }
  }
}

TEST_P(ScalabilityStructureTest,
       StaticConfigMatchesConfigReturnedByController) {
  std::unique_ptr<ScalableVideoController> controller =
      CreateScalabilityStructure(GetParam().GetScalabilityMode());
  std::optional<ScalableVideoController::StreamLayersConfig> static_config =
      ScalabilityStructureConfig(GetParam().GetScalabilityMode());
  ASSERT_THAT(controller, NotNull());
  ASSERT_NE(static_config, std::nullopt);
  ScalableVideoController::StreamLayersConfig config =
      controller->StreamConfig();
  EXPECT_EQ(config.num_spatial_layers, static_config->num_spatial_layers);
  EXPECT_EQ(config.num_temporal_layers, static_config->num_temporal_layers);
  EXPECT_THAT(std::span(config.scaling_factor_num, config.num_spatial_layers),
              ElementsAreArray(static_config->scaling_factor_num,
                               static_config->num_spatial_layers));
  EXPECT_THAT(std::span(config.scaling_factor_den, config.num_spatial_layers),
              ElementsAreArray(static_config->scaling_factor_den,
                               static_config->num_spatial_layers));
}

TEST_P(ScalabilityStructureTest,
       NumberOfDecodeTargetsAndChainsAreInRangeAndConsistent) {
  FrameDependencyStructure structure =
      CreateScalabilityStructure(GetParam().GetScalabilityMode())
          ->DependencyStructure();
  EXPECT_GT(structure.num_decode_targets, 0);
  EXPECT_LE(structure.num_decode_targets,
            DependencyDescriptor::kMaxDecodeTargets);
  EXPECT_GE(structure.num_chains, 0);
  EXPECT_LE(structure.num_chains, structure.num_decode_targets);
  if (structure.num_chains == 0) {
    EXPECT_THAT(structure.decode_target_protected_by_chain, IsEmpty());
  } else {
    EXPECT_THAT(structure.decode_target_protected_by_chain,
                AllOf(SizeIs(structure.num_decode_targets), Each(Ge(0)),
                      Each(Lt(structure.num_chains))));
  }
  EXPECT_THAT(structure.templates,
              SizeIs(Lt(size_t{DependencyDescriptor::kMaxTemplates})));
}

TEST_P(ScalabilityStructureTest, TemplatesAreSortedByLayerId) {
  FrameDependencyStructure structure =
      CreateScalabilityStructure(GetParam().GetScalabilityMode())
          ->DependencyStructure();
  ASSERT_THAT(structure.templates, Not(IsEmpty()));
  const auto& first_templates = structure.templates.front();
  EXPECT_EQ(first_templates.spatial_id, 0);
  EXPECT_EQ(first_templates.temporal_id, 0);
  for (size_t i = 1; i < structure.templates.size(); ++i) {
    const auto& prev_template = structure.templates[i - 1];
    const auto& next_template = structure.templates[i];
    if (next_template.spatial_id == prev_template.spatial_id &&
        next_template.temporal_id == prev_template.temporal_id) {
      // Same layer, next_layer_idc == 0
    } else if (next_template.spatial_id == prev_template.spatial_id &&
               next_template.temporal_id == prev_template.temporal_id + 1) {
      // Next temporal layer, next_layer_idc == 1
    } else if (next_template.spatial_id == prev_template.spatial_id + 1 &&
               next_template.temporal_id == 0) {
      // Next spatial layer, next_layer_idc == 2
    } else {
      // everything else is invalid.
      ADD_FAILURE() << "Invalid templates order. Template #" << i
                    << " with layer (" << next_template.spatial_id << ","
                    << next_template.temporal_id
                    << ") follows template with layer ("
                    << prev_template.spatial_id << ","
                    << prev_template.temporal_id << ").";
    }
  }
}

TEST_P(ScalabilityStructureTest, TemplatesMatchNumberOfDecodeTargetsAndChains) {
  FrameDependencyStructure structure =
      CreateScalabilityStructure(GetParam().GetScalabilityMode())
          ->DependencyStructure();
  EXPECT_THAT(
      structure.templates,
      Each(AllOf(Field(&FrameDependencyTemplate::decode_target_indications,
                       SizeIs(structure.num_decode_targets)),
                 Field(&FrameDependencyTemplate::chain_diffs,
                       SizeIs(structure.num_chains)))));
}

TEST_P(ScalabilityStructureTest, FrameInfoMatchesFrameDependencyStructure) {
  std::unique_ptr<ScalableVideoController> svc_controller =
      CreateScalabilityStructure(GetParam().GetScalabilityMode());
  FrameDependencyStructure structure = svc_controller->DependencyStructure();
  std::vector<GenericFrameInfo> frame_infos =
      ScalabilityStructureWrapper(*svc_controller)
          .GenerateFrames(GetParam().num_temporal_units);
  for (size_t frame_id = 0; frame_id < frame_infos.size(); ++frame_id) {
    const auto& frame = frame_infos[frame_id];
    EXPECT_GE(frame.spatial_id, 0) << " for frame " << frame_id;
    EXPECT_GE(frame.temporal_id, 0) << " for frame " << frame_id;
    EXPECT_THAT(frame.decode_target_indications,
                SizeIs(structure.num_decode_targets))
        << " for frame " << frame_id;
    EXPECT_THAT(frame.part_of_chain, SizeIs(structure.num_chains))
        << " for frame " << frame_id;
  }
}

TEST_P(ScalabilityStructureTest, ThereIsAPerfectTemplateForEachFrame) {
  std::unique_ptr<ScalableVideoController> svc_controller =
      CreateScalabilityStructure(GetParam().GetScalabilityMode());
  FrameDependencyStructure structure = svc_controller->DependencyStructure();
  std::vector<GenericFrameInfo> frame_infos =
      ScalabilityStructureWrapper(*svc_controller)
          .GenerateFrames(GetParam().num_temporal_units);
  for (size_t frame_id = 0; frame_id < frame_infos.size(); ++frame_id) {
    EXPECT_THAT(structure.templates, Contains(frame_infos[frame_id]))
        << " for frame " << frame_id << ", Expected "
        << FrameDependencyTemplateToString(frame_infos[frame_id]);
  }
}

TEST_P(ScalabilityStructureTest, FrameDependsOnSameOrLowerLayer) {
  std::unique_ptr<ScalableVideoController> svc_controller =
      CreateScalabilityStructure(GetParam().GetScalabilityMode());
  std::vector<GenericFrameInfo> frame_infos =
      ScalabilityStructureWrapper(*svc_controller)
          .GenerateFrames(GetParam().num_temporal_units);
  int64_t num_frames = frame_infos.size();

  for (int64_t frame_id = 0; frame_id < num_frames; ++frame_id) {
    const auto& frame = frame_infos[frame_id];
    for (int frame_diff : frame.frame_diffs) {
      int64_t base_frame_id = frame_id - frame_diff;
      const auto& base_frame = frame_infos[base_frame_id];
      EXPECT_GE(frame.spatial_id, base_frame.spatial_id)
          << "Frame " << frame_id << " depends on frame " << base_frame_id;
      EXPECT_GE(frame.temporal_id, base_frame.temporal_id)
          << "Frame " << frame_id << " depends on frame " << base_frame_id;
    }
  }
}

TEST_P(ScalabilityStructureTest, NoFrameDependsOnDiscardableOrNotPresent) {
  std::unique_ptr<ScalableVideoController> svc_controller =
      CreateScalabilityStructure(GetParam().GetScalabilityMode());
  std::vector<GenericFrameInfo> frame_infos =
      ScalabilityStructureWrapper(*svc_controller)
          .GenerateFrames(GetParam().num_temporal_units);
  int64_t num_frames = frame_infos.size();
  FrameDependencyStructure structure = svc_controller->DependencyStructure();

  for (int dt = 0; dt < structure.num_decode_targets; ++dt) {
    for (int64_t frame_id = 0; frame_id < num_frames; ++frame_id) {
      const auto& frame = frame_infos[frame_id];
      if (frame.decode_target_indications[dt] ==
          DecodeTargetIndication::kNotPresent) {
        continue;
      }
      for (int frame_diff : frame.frame_diffs) {
        int64_t base_frame_id = frame_id - frame_diff;
        const auto& base_frame = frame_infos[base_frame_id];
        EXPECT_NE(base_frame.decode_target_indications[dt],
                  DecodeTargetIndication::kNotPresent)
            << "Frame " << frame_id << " depends on frame " << base_frame_id
            << " that is not part of decode target#" << dt;
        EXPECT_NE(base_frame.decode_target_indications[dt],
                  DecodeTargetIndication::kDiscardable)
            << "Frame " << frame_id << " depends on frame " << base_frame_id
            << " that is discardable for decode target#" << dt;
      }
    }
  }
}

TEST_P(ScalabilityStructureTest, NoFrameDependsThroughSwitchIndication) {
  std::unique_ptr<ScalableVideoController> svc_controller =
      CreateScalabilityStructure(GetParam().GetScalabilityMode());
  FrameDependencyStructure structure = svc_controller->DependencyStructure();
  std::vector<GenericFrameInfo> frame_infos =
      ScalabilityStructureWrapper(*svc_controller)
          .GenerateFrames(GetParam().num_temporal_units);
  int64_t num_frames = frame_infos.size();
  std::vector<std::set<int64_t>> full_deps(num_frames);

  // For each frame calculate set of all frames it depends on, both directly and
  // indirectly.
  for (int64_t frame_id = 0; frame_id < num_frames; ++frame_id) {
    std::set<int64_t> all_base_frames;
    for (int frame_diff : frame_infos[frame_id].frame_diffs) {
      int64_t base_frame_id = frame_id - frame_diff;
      all_base_frames.insert(base_frame_id);
      const auto& indirect = full_deps[base_frame_id];
      all_base_frames.insert(indirect.begin(), indirect.end());
    }
    full_deps[frame_id] = std::move(all_base_frames);
  }

  // Now check the switch indication: frames after the switch indication mustn't
  // depend on any addition frames before the switch indications.
  for (int dt = 0; dt < structure.num_decode_targets; ++dt) {
    for (int64_t switch_frame_id = 0; switch_frame_id < num_frames;
         ++switch_frame_id) {
      if (frame_infos[switch_frame_id].decode_target_indications[dt] !=
          DecodeTargetIndication::kSwitch) {
        continue;
      }
      for (int64_t later_frame_id = switch_frame_id + 1;
           later_frame_id < num_frames; ++later_frame_id) {
        if (frame_infos[later_frame_id].decode_target_indications[dt] ==
            DecodeTargetIndication::kNotPresent) {
          continue;
        }
        for (int frame_diff : frame_infos[later_frame_id].frame_diffs) {
          int64_t early_frame_id = later_frame_id - frame_diff;
          if (early_frame_id < switch_frame_id) {
            EXPECT_THAT(full_deps[switch_frame_id], Contains(early_frame_id))
                << "For decode target #" << dt << " frame " << later_frame_id
                << " depends on the frame " << early_frame_id
                << " that switch indication frame " << switch_frame_id
                << " doesn't directly on indirectly depend on.";
          }
        }
      }
    }
  }
}

TEST_P(ScalabilityStructureTest, ProduceNoFrameForDisabledLayers) {
  std::unique_ptr<ScalableVideoController> svc_controller =
      CreateScalabilityStructure(GetParam().GetScalabilityMode());
  ScalableVideoController::StreamLayersConfig structure =
      svc_controller->StreamConfig();

  VideoBitrateAllocation all_bitrates;
  for (int sid = 0; sid < structure.num_spatial_layers; ++sid) {
    for (int tid = 0; tid < structure.num_temporal_layers; ++tid) {
      all_bitrates.SetBitrate(sid, tid, 100'000);
    }
  }

  svc_controller->OnRatesUpdated(all_bitrates);
  ScalabilityStructureWrapper wrapper(*svc_controller);
  std::vector<GenericFrameInfo> frames =
      wrapper.GenerateFrames(GetParam().num_temporal_units);

  for (int sid = 0; sid < structure.num_spatial_layers; ++sid) {
    for (int tid = 0; tid < structure.num_temporal_layers; ++tid) {
      // When all layers were enabled, expect there was a frame for each layer.
      EXPECT_THAT(frames,
                  Contains(AllOf(Field(&GenericFrameInfo::spatial_id, sid),
                                 Field(&GenericFrameInfo::temporal_id, tid))))
          << "For layer (" << sid << "," << tid << ")";
      // Restore bitrates for all layers before disabling single layer.
      VideoBitrateAllocation bitrates = all_bitrates;
      bitrates.SetBitrate(sid, tid, 0);
      svc_controller->OnRatesUpdated(bitrates);
      // With layer (sid, tid) disabled, expect no frames are produced for it.
      EXPECT_THAT(
          wrapper.GenerateFrames(GetParam().num_temporal_units),
          Not(Contains(AllOf(Field(&GenericFrameInfo::spatial_id, sid),
                             Field(&GenericFrameInfo::temporal_id, tid)))))
          << "For layer (" << sid << "," << tid << ")";
    }
  }
}

// TODO(bugs.webrtc.org/445115234): Populate supported scalability modes here,
// and then remove once all are supported.
bool SupportsOnFrameDropped(ScalabilityMode mode) {
  switch (mode) {
    case ScalabilityMode::kL1T1:
    case ScalabilityMode::kL1T2:
    case ScalabilityMode::kL1T3:
    case ScalabilityMode::kL2T1:
    case ScalabilityMode::kL2T1h:
    case ScalabilityMode::kL2T1_KEY:
    case ScalabilityMode::kL2T2:
    case ScalabilityMode::kL2T2h:
    case ScalabilityMode::kL2T2_KEY:
    case ScalabilityMode::kL2T3:
    case ScalabilityMode::kL2T3h:
    case ScalabilityMode::kL2T3_KEY:
    case ScalabilityMode::kL3T1:
    case ScalabilityMode::kL3T1h:
    case ScalabilityMode::kL3T1_KEY:
    case ScalabilityMode::kL3T2:
    case ScalabilityMode::kL3T2h:
    case ScalabilityMode::kL3T2_KEY:
    case ScalabilityMode::kL3T3:
    case ScalabilityMode::kL3T3h:
    case ScalabilityMode::kL3T3_KEY:
      return true;
    default:
      return false;
  }
}

TEST_P(ScalabilityStructureTest,
       OnFrameDroppedForEnhancementTemporalLayersKeepsReferencesValid) {
  if (!SupportsOnFrameDropped(GetParam().GetScalabilityMode())) {
    GTEST_SKIP();
  }
  std::unique_ptr<ScalableVideoController> svc_controller =
      CreateScalabilityStructure(GetParam().GetScalabilityMode());
  ScalabilityStructureWrapper wrapper(*svc_controller);
  std::vector<GenericFrameInfo> frames;

  // Generate first temporal unit (keyframe / T0), then run `N` cycles of `N`
  // temporal units (`N = num_temporal_units`). Each outer iteration drops a
  // single enhancement temporal unit offset `drop_tu` and encodes the remaining
  // temporal units so the controller recovers across a full cycle on the same
  // stream before testing the next offset. `num_temporal_units` must remain
  // small to avoid excessive runtime.
  wrapper.GenerateFrames(/*num_temporal_units=*/1, frames);
  for (int drop_tu = 0; drop_tu < GetParam().num_temporal_units; ++drop_tu) {
    for (int i = 0; i < GetParam().num_temporal_units; ++i) {
      std::vector<LayerFrameConfig> configs =
          svc_controller->NextFrameConfig(/*restart=*/false);
      const bool is_enhancement_temporal_unit =
          std::ranges::any_of(configs, [](const LayerFrameConfig& config) {
            return config.TemporalId() > 0;
          });
      if (i == drop_tu && is_enhancement_temporal_unit) {
        for (const LayerFrameConfig& config : configs) {
          svc_controller->OnFrameDropped(config);
        }
      } else {
        wrapper.EncodeTemporalUnit(configs, frames);
      }
    }
  }
  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
  CheckDependencyDescriptorInvariants(svc_controller->DependencyStructure(),
                                      frames);
}

TEST_P(ScalabilityStructureTest,
       OnFrameDroppedForAnyTemporalUnitKeepsReferencesValid) {
  if (!SupportsOnFrameDropped(GetParam().GetScalabilityMode())) {
    GTEST_SKIP();
  }
  std::unique_ptr<ScalableVideoController> svc_controller =
      CreateScalabilityStructure(GetParam().GetScalabilityMode());
  ScalabilityStructureWrapper wrapper(*svc_controller);
  std::vector<GenericFrameInfo> frames;

  // Drop the initial keyframe temporal unit before any frame has been encoded.
  std::vector<LayerFrameConfig> initial_configs =
      svc_controller->NextFrameConfig(/*restart=*/false);
  ASSERT_THAT(initial_configs, Not(IsEmpty()));
  for (const LayerFrameConfig& config : initial_configs) {
    svc_controller->OnFrameDropped(config);
  }

  // Generate the first encoded temporal unit, then run `N` cycles of `N`
  // temporal units (`N = num_temporal_units`), encoding `N * (N - 1)` temporal
  // units in total. Each outer iteration drops a single temporal unit offset
  // `drop_tu` (including T0) and encodes the remaining `N - 1` temporal units
  // so the controller recovers across a full cycle on the same stream before
  // testing the next offset. `num_temporal_units` must remain small since
  // O(N^2) frames with O(frames^2) invariant checks gives O(N^4) work.
  wrapper.GenerateFrames(/*num_temporal_units=*/1, frames);
  for (int drop_tu = 0; drop_tu < GetParam().num_temporal_units; ++drop_tu) {
    for (int i = 0; i < GetParam().num_temporal_units; ++i) {
      std::vector<LayerFrameConfig> configs =
          svc_controller->NextFrameConfig(/*restart=*/false);
      ASSERT_THAT(configs, Not(IsEmpty()));
      if (i == drop_tu) {
        for (const LayerFrameConfig& config : configs) {
          svc_controller->OnFrameDropped(config);
        }
      } else {
        wrapper.EncodeTemporalUnit(configs, frames);
      }
    }
  }
  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
  CheckDependencyDescriptorInvariants(svc_controller->DependencyStructure(),
                                      frames);
}

TEST_P(ScalabilityStructureTest,
       OnFrameDroppedForUpperSpatialLayersKeepsReferencesValid) {
  if (!SupportsOnFrameDropped(GetParam().GetScalabilityMode())) {
    GTEST_SKIP();
  }
  ScalableVideoController::StreamLayersConfig stream_config =
      CreateScalabilityStructure(GetParam().GetScalabilityMode())
          ->StreamConfig();
  for (int drop_from_sid = 0; drop_from_sid < stream_config.num_spatial_layers;
       ++drop_from_sid) {
    for (int drop_tu = 0; drop_tu <= GetParam().num_temporal_units; ++drop_tu) {
      std::unique_ptr<ScalableVideoController> svc_controller =
          CreateScalabilityStructure(GetParam().GetScalabilityMode());
      ScalabilityStructureWrapper wrapper(*svc_controller);
      std::vector<GenericFrameInfo> frames;

      const int total_tus = GetParam().num_temporal_units * 2 + 1;
      for (int i = 0; i < total_tus; ++i) {
        std::vector<LayerFrameConfig> configs =
            svc_controller->NextFrameConfig(/*restart=*/false);
        ASSERT_THAT(configs, Not(IsEmpty()));
        if (i == drop_tu) {
          std::vector<LayerFrameConfig> encoded;
          for (const LayerFrameConfig& config : configs) {
            if (config.SpatialId() >= drop_from_sid) {
              svc_controller->OnFrameDropped(config);
            } else {
              encoded.push_back(config);
            }
          }
          wrapper.EncodeTemporalUnit(encoded, frames);
        } else {
          wrapper.EncodeTemporalUnit(configs, frames);
        }
      }
      EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames))
          << "drop_from_sid=" << drop_from_sid << ", drop_tu=" << drop_tu;
      CheckDependencyDescriptorInvariants(svc_controller->DependencyStructure(),
                                          frames);
    }
  }
}

INSTANTIATE_TEST_SUITE_P(
    Svc,
    ScalabilityStructureTest,
    Values(SvcTestParam{"L1T1", /*num_temporal_units=*/3},
           SvcTestParam{"L1T2", /*num_temporal_units=*/4},
           SvcTestParam{"L1T3", /*num_temporal_units=*/8},
           SvcTestParam{"L2T1", /*num_temporal_units=*/3},
           SvcTestParam{"L2T1_KEY", /*num_temporal_units=*/3},
           SvcTestParam{"L3T1", /*num_temporal_units=*/3},
           SvcTestParam{"L3T1_KEY", /*num_temporal_units=*/3},
           SvcTestParam{"L3T3", /*num_temporal_units=*/8},
           SvcTestParam{"S2T1", /*num_temporal_units=*/3},
           SvcTestParam{"S2T2", /*num_temporal_units=*/4},
           SvcTestParam{"S2T3", /*num_temporal_units=*/8},
           SvcTestParam{"S3T1", /*num_temporal_units=*/3},
           SvcTestParam{"S3T2", /*num_temporal_units=*/4},
           SvcTestParam{"S3T3", /*num_temporal_units=*/8},
           SvcTestParam{"L2T2", /*num_temporal_units=*/4},
           SvcTestParam{"L2T2_KEY", /*num_temporal_units=*/4},
           SvcTestParam{"L2T2_KEY_SHIFT", /*num_temporal_units=*/4},
           SvcTestParam{"L2T3", /*num_temporal_units=*/8},
           SvcTestParam{"L2T3_KEY", /*num_temporal_units=*/8},
           SvcTestParam{"L3T2", /*num_temporal_units=*/4},
           SvcTestParam{"L3T2_KEY", /*num_temporal_units=*/4},
           SvcTestParam{"L3T3_KEY", /*num_temporal_units=*/8}),
    [](const testing::TestParamInfo<SvcTestParam>& info) {
      return info.param.name;
    });

}  // namespace
}  // namespace webrtc
