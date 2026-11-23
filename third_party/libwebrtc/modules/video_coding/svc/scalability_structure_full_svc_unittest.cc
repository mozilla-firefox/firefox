/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "modules/video_coding/svc/scalability_structure_full_svc.h"

#include <vector>

#include "common_video/generic_frame_descriptor/generic_frame_info.h"
#include "modules/video_coding/svc/scalability_structure_test_helpers.h"
#include "modules/video_coding/svc/scalable_video_controller.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

using ::testing::IsEmpty;
using ::testing::SizeIs;
using LayerFrameConfig = ScalableVideoController::LayerFrameConfig;

TEST(ScalabilityStructureL3T3Test, SkipT0FrameByEncoderKeepsReferencesValid) {
  std::vector<GenericFrameInfo> frames;
  ScalabilityStructureL3T3 structure;
  ScalabilityStructureWrapper wrapper(structure);

  // Only S0T0 decode target is enabled.
  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/1, /*s1=*/0));
  // Encoder generates S0T0 key frame.
  wrapper.GenerateFrames(/*num_temporal_units=*/1, frames);
  EXPECT_THAT(frames, SizeIs(1));
  // Spatial layers 1 is enabled.
  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/1, /*s1=*/1));
  // Encoder tries to generate S0T0 and S1T0 delta frames but they are dropped.
  structure.NextFrameConfig(/*restart=*/false);
  // Encoder successfully generates S0T0 and S1T0 delta frames.
  wrapper.GenerateFrames(/*num_temporal_units=*/1, frames);
  EXPECT_THAT(frames, SizeIs(3));

  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
}

TEST(ScalabilityStructureL3T3Test, SkipS1T1FrameKeepsStructureValid) {
  ScalabilityStructureL3T3 structure;
  ScalabilityStructureWrapper wrapper(structure);

  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/3, /*s1=*/3));
  auto frames = wrapper.GenerateFrames(/*num_temporal_units=*/1);
  EXPECT_THAT(frames, SizeIs(2));
  EXPECT_EQ(frames[0].temporal_id, 0);

  frames = wrapper.GenerateFrames(/*num_temporal_units=*/1);
  EXPECT_THAT(frames, SizeIs(2));
  EXPECT_EQ(frames[0].temporal_id, 2);

  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/3, /*s1=*/0));
  frames = wrapper.GenerateFrames(/*num_temporal_units=*/1);
  EXPECT_THAT(frames, SizeIs(1));
  EXPECT_EQ(frames[0].temporal_id, 1);

  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/3, /*s1=*/3));
  // Rely on checks inside GenerateFrames frame references are valid.
  frames = wrapper.GenerateFrames(/*num_temporal_units=*/1);
  EXPECT_THAT(frames, SizeIs(2));
  EXPECT_EQ(frames[0].temporal_id, 2);
}

TEST(ScalabilityStructureL3T3Test, SkipT1FrameByEncoderKeepsReferencesValid) {
  std::vector<GenericFrameInfo> frames;
  ScalabilityStructureL3T3 structure;
  ScalabilityStructureWrapper wrapper(structure);

  // 1st 2 temporal units (T0 and T2)
  wrapper.GenerateFrames(/*num_temporal_units=*/2, frames);
  // Simulate T1 frame dropped by the encoder,
  // i.e. retrieve config, but skip calling OnEncodeDone.
  structure.NextFrameConfig(/*restart=*/false);
  // one more temporal units (T2)
  wrapper.GenerateFrames(/*num_temporal_units=*/1, frames);

  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
}

TEST(ScalabilityStructureL3T3Test,
     SkippingFrameReusePreviousFrameConfiguration) {
  std::vector<GenericFrameInfo> frames;
  ScalabilityStructureL3T3 structure;
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

TEST(ScalabilityStructureL3T3Test, SwitchSpatialLayerBeforeT1Frame) {
  ScalabilityStructureL3T3 structure;
  ScalabilityStructureWrapper wrapper(structure);

  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/2, /*s1=*/0));
  EXPECT_THAT(wrapper.GenerateFrames(1), SizeIs(1));
  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/0, /*s1=*/2));
  auto frames = wrapper.GenerateFrames(1);
  ASSERT_THAT(frames, SizeIs(1));
  EXPECT_THAT(frames[0].frame_diffs, IsEmpty());
  EXPECT_EQ(frames[0].temporal_id, 0);
}

TEST(ScalabilityStructureL3T3Test,
     OnFrameDroppedAdvancesPatternAndKeepsReferencesValid) {
  std::vector<GenericFrameInfo> frames;
  ScalabilityStructureL3T3 structure;
  ScalabilityStructureWrapper wrapper(structure);

  // 1st 2 temporal units (T0 and T2a).
  wrapper.GenerateFrames(/*num_temporal_units=*/2, frames);
  ASSERT_THAT(frames, SizeIs(6));
  EXPECT_EQ(frames[0].temporal_id, 0);
  EXPECT_EQ(frames[3].temporal_id, 2);

  // Drop T1 frame via OnFrameDropped.
  for (const LayerFrameConfig& layer_frame :
       structure.NextFrameConfig(/*restart=*/false)) {
    EXPECT_EQ(layer_frame.TemporalId(), 1);
    structure.OnFrameDropped(layer_frame);
  }

  // Next temporal unit should advance to T2b and fall back to referencing T0.
  wrapper.GenerateFrames(/*num_temporal_units=*/2, frames);
  ASSERT_THAT(frames, SizeIs(12));
  EXPECT_EQ(frames[6].temporal_id, 2);
  EXPECT_EQ(frames[9].temporal_id, 0);
  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
}

TEST(ScalabilityStructureL3T3Test,
     OnFrameDroppedForT0AdvancesPatternAndReferencesPreviousT0) {
  std::vector<GenericFrameInfo> frames;
  ScalabilityStructureL3T3 structure;
  ScalabilityStructureWrapper wrapper(structure);

  // Encode full 4-TU cycle: T0, T2a, T1, T2b (12 frames across 3 spatial
  // layers).
  wrapper.GenerateFrames(/*num_temporal_units=*/4, frames);
  ASSERT_THAT(frames, SizeIs(12));

  // Drop the next T0 temporal unit via OnFrameDropped.
  for (const LayerFrameConfig& layer_frame :
       structure.NextFrameConfig(/*restart=*/false)) {
    EXPECT_EQ(layer_frame.TemporalId(), 0);
    structure.OnFrameDropped(layer_frame);
  }

  // Next temporal unit should progress to T2a (not repeat T0) and reference the
  // previous T0 frames.
  wrapper.GenerateFrames(/*num_temporal_units=*/4, frames);
  ASSERT_THAT(frames, SizeIs(24));
  EXPECT_EQ(frames[12].temporal_id, 2);
  EXPECT_EQ(frames[15].temporal_id, 1);
  EXPECT_EQ(frames[18].temporal_id, 2);
  EXPECT_EQ(frames[21].temporal_id, 0);
  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
}

TEST(ScalabilityStructureFullSvcTest,
     OnFrameDroppedForInitialKeyframeProducesKeyframeNext) {
  ScalabilityStructureL2T1 structure;
  ScalabilityStructureWrapper wrapper(structure);
  std::vector<GenericFrameInfo> frames;

  std::vector<LayerFrameConfig> initial =
      structure.NextFrameConfig(/*restart=*/false);
  ASSERT_THAT(initial, SizeIs(2));
  EXPECT_TRUE(initial[0].IsKeyframe());
  for (const LayerFrameConfig& layer_frame : initial) {
    structure.OnFrameDropped(layer_frame);
  }

  std::vector<LayerFrameConfig> retry =
      structure.NextFrameConfig(/*restart=*/false);
  ASSERT_THAT(retry, SizeIs(2));
  EXPECT_TRUE(retry[0].IsKeyframe());
  wrapper.EncodeTemporalUnit(retry, frames);
  wrapper.GenerateFrames(/*num_temporal_units=*/2, frames);
  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
}

}  // namespace
}  // namespace webrtc
