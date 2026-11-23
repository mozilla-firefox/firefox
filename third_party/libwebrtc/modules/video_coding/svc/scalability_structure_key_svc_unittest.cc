/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "modules/video_coding/svc/scalability_structure_key_svc.h"

#include <span>
#include <vector>

#include "common_video/generic_frame_descriptor/generic_frame_info.h"
#include "modules/video_coding/svc/scalability_structure_test_helpers.h"
#include "modules/video_coding/svc/scalable_video_controller.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::SizeIs;
using LayerFrameConfig = ScalableVideoController::LayerFrameConfig;

TEST(ScalabilityStructureL3T3KeyTest,
     SkipingT1FrameOnOneSpatialLayerKeepsStructureValid) {
  ScalabilityStructureL3T3Key structure;
  ScalabilityStructureWrapper wrapper(structure);
  std::vector<GenericFrameInfo> frames;

  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/3, /*s1=*/3));
  wrapper.GenerateFrames(/*num_temporal_units=*/2, frames);
  EXPECT_THAT(frames, SizeIs(4));
  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/3, /*s1=*/1));
  wrapper.GenerateFrames(/*num_temporal_units=*/1, frames);
  EXPECT_THAT(frames, SizeIs(5));
  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/3, /*s1=*/3));
  wrapper.GenerateFrames(/*num_temporal_units=*/1, frames);
  ASSERT_THAT(frames, SizeIs(7));

  EXPECT_EQ(frames[0].temporal_id, 0);
  EXPECT_EQ(frames[1].temporal_id, 0);
  EXPECT_EQ(frames[2].temporal_id, 2);
  EXPECT_EQ(frames[3].temporal_id, 2);
  EXPECT_EQ(frames[4].temporal_id, 1);
  EXPECT_EQ(frames[5].temporal_id, 2);
  EXPECT_EQ(frames[6].temporal_id, 2);
  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
}

TEST(ScalabilityStructureL3T3KeyTest,
     SkipT1FrameByEncoderKeepsReferencesValid) {
  std::vector<GenericFrameInfo> frames;
  ScalabilityStructureL3T3Key structure;
  ScalabilityStructureWrapper wrapper(structure);

  // 1st 2 temporal units (T0 and T2)
  wrapper.GenerateFrames(/*num_temporal_units=*/2, frames);
  // Simulate T1 frame dropped by the encoder,
  // i.e. retrieve config, but skip calling OnEncodeDone.
  structure.NextFrameConfig(/*restart=*/false);
  // one more temporal unit.
  wrapper.GenerateFrames(/*num_temporal_units=*/1, frames);

  EXPECT_THAT(frames, SizeIs(9));
  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
}

TEST(ScalabilityStructureL3T3KeyTest,
     SkippingFrameReusePreviousFrameConfiguration) {
  std::vector<GenericFrameInfo> frames;
  ScalabilityStructureL3T3Key structure;
  ScalabilityStructureWrapper wrapper(structure);

  // 1st 2 temporal units (T0 and T2)
  wrapper.GenerateFrames(/*num_temporal_units=*/2, frames);
  ASSERT_THAT(frames, SizeIs(6));
  ASSERT_EQ(frames[0].temporal_id, 0);
  ASSERT_EQ(frames[3].temporal_id, 2);

  // Simulate a frame dropped by the encoder,
  // i.e. retrieve config, but skip calling OnEncodeDone.
  structure.NextFrameConfig(/*restart=*/false);
  // two more temporal unit, expect temporal pattern continues
  wrapper.GenerateFrames(/*num_temporal_units=*/2, frames);
  ASSERT_THAT(frames, SizeIs(12));
  // Expect temporal pattern continues as if there were no dropped frames.
  EXPECT_EQ(frames[6].temporal_id, 1);
  EXPECT_EQ(frames[9].temporal_id, 2);
}

TEST(ScalabilityStructureL3T3KeyTest, SkippingKeyFrameTriggersNewKeyFrame) {
  std::vector<GenericFrameInfo> frames;
  ScalabilityStructureL3T3Key structure;
  ScalabilityStructureWrapper wrapper(structure);

  // Ask for a key frame config, but do not return any frames
  structure.NextFrameConfig(/*restart=*/false);

  // Ask for more frames, expect they start with a key frame.
  wrapper.GenerateFrames(/*num_temporal_units=*/2, frames);
  ASSERT_THAT(frames, SizeIs(6));
  ASSERT_EQ(frames[0].temporal_id, 0);
  ASSERT_EQ(frames[3].temporal_id, 2);
  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
}

TEST(ScalabilityStructureL3T3KeyTest,
     SkippingT2FrameAndDisablingT2LayerProduceT1AsNextFrame) {
  std::vector<GenericFrameInfo> frames;
  ScalabilityStructureL3T3Key structure;
  ScalabilityStructureWrapper wrapper(structure);

  wrapper.GenerateFrames(/*num_temporal_units=*/1, frames);
  // Ask for next (T2) frame config, but do not return any frames
  auto config = structure.NextFrameConfig(/*restart=*/false);
  ASSERT_THAT(config, Not(IsEmpty()));
  ASSERT_EQ(config.front().TemporalId(), 2);

  // Disable T2 layer,
  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/2, /*s1=*/2, /*s2=*/2));
  // Expect instead of reusing unused config, T1 config is generated.
  config = structure.NextFrameConfig(/*restart=*/false);
  ASSERT_THAT(config, Not(IsEmpty()));
  EXPECT_EQ(config.front().TemporalId(), 1);
}

TEST(ScalabilityStructureL3T3KeyTest, EnableT2LayerWhileProducingT1Frame) {
  std::vector<GenericFrameInfo> frames;
  ScalabilityStructureL3T3Key structure;
  ScalabilityStructureWrapper wrapper(structure);

  // Disable T2 layer,
  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/2, /*s1=*/2, /*s2=*/2));

  // Generate the key frame.
  wrapper.GenerateFrames(/*num_temporal_units=*/1, frames);
  ASSERT_THAT(frames, SizeIs(3));
  EXPECT_EQ(frames[0].temporal_id, 0);

  // Ask for next (T1) frame config, but do not return any frames yet.
  auto config = structure.NextFrameConfig(/*restart=*/false);
  ASSERT_THAT(config, Not(IsEmpty()));
  ASSERT_EQ(config.front().TemporalId(), 1);

  // Reenable T2 layer.
  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/3, /*s1=*/3, /*s2=*/3));

  // Finish encoding previously requested config.
  for (auto layer_config : config) {
    GenericFrameInfo info = structure.OnEncodeDone(layer_config);
    EXPECT_EQ(info.temporal_id, 1);
    frames.push_back(info);
  }
  ASSERT_THAT(frames, SizeIs(6));

  // Generate more frames, expect T2 pattern resumes.
  wrapper.GenerateFrames(/*num_temporal_units=*/4, frames);
  ASSERT_THAT(frames, SizeIs(18));
  EXPECT_EQ(frames[6].temporal_id, 2);
  EXPECT_EQ(frames[9].temporal_id, 0);
  EXPECT_EQ(frames[12].temporal_id, 2);
  EXPECT_EQ(frames[15].temporal_id, 1);

  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
}

TEST(ScalabilityStructureL3T3KeyTest,
     ReenablingSpatialLayerBeforeMissedT0FrameDoesntTriggerAKeyFrame) {
  ScalabilityStructureL3T3Key structure;
  ScalabilityStructureWrapper wrapper(structure);
  std::vector<GenericFrameInfo> frames;

  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/2, /*s1=*/2));
  wrapper.GenerateFrames(1, frames);
  EXPECT_THAT(frames, SizeIs(2));
  // Drop a spatial layer.
  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/2, /*s1=*/0));
  wrapper.GenerateFrames(1, frames);
  EXPECT_THAT(frames, SizeIs(3));
  // Reenable a spatial layer before T0 frame is encoded.
  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/2, /*s1=*/2));
  wrapper.GenerateFrames(1, frames);
  EXPECT_THAT(frames, SizeIs(5));

  EXPECT_EQ(frames[0].temporal_id, 0);
  EXPECT_EQ(frames[1].temporal_id, 0);
  EXPECT_EQ(frames[2].temporal_id, 1);
  EXPECT_EQ(frames[3].temporal_id, 0);
  EXPECT_EQ(frames[4].temporal_id, 0);
  EXPECT_THAT(frames[3].frame_diffs, SizeIs(1));
  EXPECT_THAT(frames[4].frame_diffs, SizeIs(1));
  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
}

TEST(ScalabilityStructureL3T3KeyTest, ReenablingSpatialLayerTriggersKeyFrame) {
  ScalabilityStructureL3T3Key structure;
  ScalabilityStructureWrapper wrapper(structure);
  std::vector<GenericFrameInfo> frames;

  // Start with all spatial layers enabled.
  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/2, /*s1=*/2, /*s2=*/2));
  wrapper.GenerateFrames(3, frames);
  EXPECT_THAT(frames, SizeIs(9));
  // Drop a spatial layer. Two remaining spatial layers should just continue.
  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/2, /*s1=*/0, /*s2=*/2));
  wrapper.GenerateFrames(2, frames);
  EXPECT_THAT(frames, SizeIs(13));
  // Reenable spatial layer, expect a full restart.
  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/2, /*s1=*/2, /*s2=*/2));
  wrapper.GenerateFrames(1, frames);
  ASSERT_THAT(frames, SizeIs(16));

  // First 3 temporal units with all spatial layers enabled.
  EXPECT_EQ(frames[0].temporal_id, 0);
  EXPECT_EQ(frames[3].temporal_id, 1);
  EXPECT_EQ(frames[6].temporal_id, 0);
  // 2 temporal units with spatial layer 1 disabled.
  EXPECT_EQ(frames[9].spatial_id, 0);
  EXPECT_EQ(frames[9].temporal_id, 1);
  EXPECT_EQ(frames[10].spatial_id, 2);
  EXPECT_EQ(frames[10].temporal_id, 1);
  // T0 frames were encoded while spatial layer 1 is disabled.
  EXPECT_EQ(frames[11].spatial_id, 0);
  EXPECT_EQ(frames[11].temporal_id, 0);
  EXPECT_EQ(frames[12].spatial_id, 2);
  EXPECT_EQ(frames[12].temporal_id, 0);
  // Key frame to reenable spatial layer 1.
  EXPECT_THAT(frames[13].frame_diffs, IsEmpty());
  EXPECT_THAT(frames[14].frame_diffs, ElementsAre(1));
  EXPECT_THAT(frames[15].frame_diffs, ElementsAre(1));
  EXPECT_EQ(frames[13].temporal_id, 0);
  EXPECT_EQ(frames[14].temporal_id, 0);
  EXPECT_EQ(frames[15].temporal_id, 0);
  auto all_frames = std::span(frames.data(), frames.size());
  EXPECT_TRUE(wrapper.FrameReferencesAreValid(all_frames.subspan(0, 13)));
  // Frames starting from the frame#13 should not reference any earlier frames.
  EXPECT_TRUE(wrapper.FrameReferencesAreValid(all_frames.subspan(13)));
}

TEST(ScalabilityStructureKeySvcTest,
     OnFrameDroppedAdvancesPatternAndKeepsReferencesValid) {
  std::vector<GenericFrameInfo> frames;
  ScalabilityStructureL2T3Key structure;
  ScalabilityStructureWrapper wrapper(structure);

  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/3, /*s1=*/3));
  // 1st 2 temporal units (T0 and T2a).
  wrapper.GenerateFrames(/*num_temporal_units=*/2, frames);
  ASSERT_THAT(frames, SizeIs(4));
  EXPECT_EQ(frames[0].temporal_id, 0);
  EXPECT_EQ(frames[2].temporal_id, 2);

  // Drop T1 frame via OnFrameDropped.
  for (const LayerFrameConfig& layer_frame :
       structure.NextFrameConfig(/*restart=*/false)) {
    EXPECT_EQ(layer_frame.TemporalId(), 1);
    structure.OnFrameDropped(layer_frame);
  }

  // Next temporal unit should advance to T2b and fall back to referencing T0
  // (frames[0] for S0 and frames[1] for S1).
  wrapper.GenerateFrames(/*num_temporal_units=*/2, frames);
  ASSERT_THAT(frames, SizeIs(8));
  EXPECT_EQ(frames[4].temporal_id, 2);
  EXPECT_EQ(frames[5].temporal_id, 2);
  EXPECT_THAT(frames[4].frame_diffs, ElementsAre(4));
  EXPECT_THAT(frames[5].frame_diffs, ElementsAre(4));
  EXPECT_EQ(frames[6].temporal_id, 0);
  EXPECT_EQ(frames[7].temporal_id, 0);
  EXPECT_THAT(frames[6].frame_diffs, ElementsAre(6));
  EXPECT_THAT(frames[7].frame_diffs, ElementsAre(6));
  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
}

TEST(ScalabilityStructureKeySvcTest,
     OnFrameDroppedForT0AdvancesPatternAndReferencesPreviousT0) {
  std::vector<GenericFrameInfo> frames;
  ScalabilityStructureL2T3Key structure;
  ScalabilityStructureWrapper wrapper(structure);

  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/3, /*s1=*/3));
  // Encode full 4-TU cycle: T0, T2a, T1, T2b (8 frames across 2 spatial
  // layers).
  wrapper.GenerateFrames(/*num_temporal_units=*/4, frames);
  ASSERT_THAT(frames, SizeIs(8));

  // Drop the next T0 temporal unit via OnFrameDropped.
  for (const LayerFrameConfig& layer_frame :
       structure.NextFrameConfig(/*restart=*/false)) {
    EXPECT_EQ(layer_frame.TemporalId(), 0);
    EXPECT_FALSE(layer_frame.IsKeyframe());
    structure.OnFrameDropped(layer_frame);
  }

  // Pattern advances to T2a without forcing a keyframe, referencing the old T0
  // frames (frames[0] for S0 and frames[1] for S1).
  std::vector<LayerFrameConfig> next_configs =
      structure.NextFrameConfig(/*restart=*/false);
  ASSERT_THAT(next_configs, SizeIs(2));
  EXPECT_FALSE(next_configs[0].IsKeyframe());
  EXPECT_EQ(next_configs[0].TemporalId(), 2);
  wrapper.EncodeTemporalUnit(next_configs, frames);
  wrapper.GenerateFrames(/*num_temporal_units=*/3, frames);
  ASSERT_THAT(frames, SizeIs(16));
  EXPECT_EQ(frames[8].temporal_id, 2);
  EXPECT_EQ(frames[9].temporal_id, 2);
  EXPECT_THAT(frames[8].frame_diffs, ElementsAre(8));
  EXPECT_THAT(frames[9].frame_diffs, ElementsAre(8));
  EXPECT_EQ(frames[10].temporal_id, 1);
  EXPECT_EQ(frames[11].temporal_id, 1);
  EXPECT_THAT(frames[10].frame_diffs, ElementsAre(10));
  EXPECT_THAT(frames[11].frame_diffs, ElementsAre(10));
  EXPECT_EQ(frames[12].temporal_id, 2);
  EXPECT_EQ(frames[13].temporal_id, 2);
  EXPECT_THAT(frames[12].frame_diffs, ElementsAre(2));
  EXPECT_THAT(frames[13].frame_diffs, ElementsAre(2));
  EXPECT_EQ(frames[14].temporal_id, 0);
  EXPECT_EQ(frames[15].temporal_id, 0);
  EXPECT_THAT(frames[14].frame_diffs, ElementsAre(14));
  EXPECT_THAT(frames[15].frame_diffs, ElementsAre(14));
  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
}

TEST(ScalabilityStructureKeySvcTest,
     OnFrameDroppedForS1OnKeyframeRecoversWithoutInvalidReferences) {
  std::vector<GenericFrameInfo> frames;
  ScalabilityStructureL2T2Key structure;
  ScalabilityStructureWrapper wrapper(structure);

  std::vector<LayerFrameConfig> key_configs =
      structure.NextFrameConfig(/*restart=*/false);
  ASSERT_THAT(key_configs, SizeIs(2));
  // Encode S0, drop S1 on the initial keyframe.
  wrapper.EncodeTemporalUnit(std::span(key_configs).first(1), frames);
  structure.OnFrameDropped(key_configs[1]);

  // Next TU advances to T1 for S0 only (S1 has no T0 reference yet).
  std::vector<LayerFrameConfig> t1_configs =
      structure.NextFrameConfig(/*restart=*/false);
  ASSERT_THAT(t1_configs, SizeIs(1));
  EXPECT_EQ(t1_configs[0].SpatialId(), 0);
  EXPECT_EQ(t1_configs[0].TemporalId(), 1);
  wrapper.EncodeTemporalUnit(t1_configs, frames);

  // At the T0 boundary, a keyframe is emitted to initialize S1.
  std::vector<LayerFrameConfig> recovery_configs =
      structure.NextFrameConfig(/*restart=*/false);
  ASSERT_THAT(recovery_configs, SizeIs(2));
  EXPECT_TRUE(recovery_configs[0].IsKeyframe());
  wrapper.EncodeTemporalUnit(recovery_configs, frames);
  wrapper.GenerateFrames(/*num_temporal_units=*/2, frames);
  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
}

}  // namespace
}  // namespace webrtc
