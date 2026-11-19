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

// S1T1     3   7
//         /   /
// S1T0   1---5---9
//        |
// S0T1   |   4   8
//        |  /   /
// S0T0   0-2---6
// Time-> 0 1 2 3 4
TEST(ScalabilityStructureL2T2KeyShiftTest, DecodeTargetsAreEnabledByDefault) {
  ScalabilityStructureL2T2KeyShift structure;
  ScalabilityStructureWrapper wrapper(structure);
  std::vector<GenericFrameInfo> frames;
  wrapper.GenerateFrames(/*num_temporal_units=*/5, frames);
  ASSERT_THAT(frames, SizeIs(10));

  EXPECT_EQ(frames[0].spatial_id, 0);
  EXPECT_EQ(frames[1].spatial_id, 1);
  EXPECT_EQ(frames[2].spatial_id, 0);
  EXPECT_EQ(frames[3].spatial_id, 1);
  EXPECT_EQ(frames[4].spatial_id, 0);
  EXPECT_EQ(frames[5].spatial_id, 1);
  EXPECT_EQ(frames[6].spatial_id, 0);
  EXPECT_EQ(frames[7].spatial_id, 1);
  EXPECT_EQ(frames[8].spatial_id, 0);
  EXPECT_EQ(frames[9].spatial_id, 1);

  // spatial_id = 0 has the temporal shift.
  EXPECT_EQ(frames[0].temporal_id, 0);
  EXPECT_EQ(frames[2].temporal_id, 0);
  EXPECT_EQ(frames[4].temporal_id, 1);
  EXPECT_EQ(frames[6].temporal_id, 0);
  EXPECT_EQ(frames[8].temporal_id, 1);

  // spatial_id = 1 hasn't temporal shift.
  EXPECT_EQ(frames[1].temporal_id, 0);
  EXPECT_EQ(frames[3].temporal_id, 1);
  EXPECT_EQ(frames[5].temporal_id, 0);
  EXPECT_EQ(frames[7].temporal_id, 1);
  EXPECT_EQ(frames[9].temporal_id, 0);

  // Key frame diff.
  EXPECT_THAT(frames[0].frame_diffs, IsEmpty());
  EXPECT_THAT(frames[1].frame_diffs, ElementsAre(1));
  // S0T0 frame diffs
  EXPECT_THAT(frames[2].frame_diffs, ElementsAre(2));
  EXPECT_THAT(frames[6].frame_diffs, ElementsAre(4));
  // S1T0 frame diffs
  EXPECT_THAT(frames[5].frame_diffs, ElementsAre(4));
  EXPECT_THAT(frames[9].frame_diffs, ElementsAre(4));
  // T1 frames refer T0 frame of same spatial layer which is 2 frame ids away.
  EXPECT_THAT(frames[3].frame_diffs, ElementsAre(2));
  EXPECT_THAT(frames[4].frame_diffs, ElementsAre(2));
  EXPECT_THAT(frames[7].frame_diffs, ElementsAre(2));
  EXPECT_THAT(frames[8].frame_diffs, ElementsAre(2));
}

// S1T0   1---4---7
//        |
// S0T1   |   3   6
//        |  /   /
// S0T0   0-2---5--
// Time-> 0 1 2 3 4
TEST(ScalabilityStructureL2T2KeyShiftTest, DisableS1T1Layer) {
  ScalabilityStructureL2T2KeyShift structure;
  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/2, /*s1=*/1));
  ScalabilityStructureWrapper wrapper(structure);
  std::vector<GenericFrameInfo> frames;
  wrapper.GenerateFrames(/*num_temporal_units=*/5, frames);
  ASSERT_THAT(frames, SizeIs(8));

  EXPECT_EQ(frames[0].spatial_id, 0);
  EXPECT_EQ(frames[1].spatial_id, 1);
  EXPECT_EQ(frames[2].spatial_id, 0);
  EXPECT_EQ(frames[3].spatial_id, 0);
  EXPECT_EQ(frames[4].spatial_id, 1);
  EXPECT_EQ(frames[5].spatial_id, 0);
  EXPECT_EQ(frames[6].spatial_id, 0);
  EXPECT_EQ(frames[7].spatial_id, 1);

  // spatial_id = 0 has the temporal shift.
  EXPECT_EQ(frames[0].temporal_id, 0);
  EXPECT_EQ(frames[2].temporal_id, 0);
  EXPECT_EQ(frames[3].temporal_id, 1);
  EXPECT_EQ(frames[5].temporal_id, 0);
  EXPECT_EQ(frames[6].temporal_id, 1);

  // spatial_id = 1 has single temporal layer.
  EXPECT_EQ(frames[1].temporal_id, 0);
  EXPECT_EQ(frames[4].temporal_id, 0);
  EXPECT_EQ(frames[5].temporal_id, 0);
}

// S1T1     3  |
//         /   |
// S1T0   1---5+--7
//        |    |
// S0T1   |   4|
//        |  / |
// S0T0   0-2--+6---8
// Time-> 0 1 2 3 4 5
TEST(ScalabilityStructureL2T2KeyShiftTest, DisableT1LayersAfterFewFrames) {
  ScalabilityStructureL2T2KeyShift structure;
  ScalabilityStructureWrapper wrapper(structure);
  std::vector<GenericFrameInfo> frames;

  wrapper.GenerateFrames(/*num_temporal_units=*/3, frames);
  EXPECT_THAT(frames, SizeIs(6));
  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/1, /*s1=*/1));
  wrapper.GenerateFrames(/*num_temporal_units=*/3, frames);
  ASSERT_THAT(frames, SizeIs(9));

  // Skip validation before T1 was disabled as that is covered by the test
  // where no layers are disabled.
  EXPECT_EQ(frames[6].spatial_id, 0);
  EXPECT_EQ(frames[7].spatial_id, 1);
  EXPECT_EQ(frames[8].spatial_id, 0);

  EXPECT_EQ(frames[6].temporal_id, 0);
  EXPECT_EQ(frames[7].temporal_id, 0);
  EXPECT_EQ(frames[8].temporal_id, 0);

  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
}

// S1T1     1   3
//         /   /
// S1T0   0---2
// Time-> 0 1 2 3 4 5
TEST(ScalabilityStructureL2T2KeyShiftTest, DisableS0FromTheStart) {
  ScalabilityStructureL2T2KeyShift structure;
  ScalabilityStructureWrapper wrapper(structure);
  std::vector<GenericFrameInfo> frames;

  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/0, /*s1=*/2));
  wrapper.GenerateFrames(/*num_temporal_units=*/4, frames);
  EXPECT_THAT(frames, SizeIs(4));

  EXPECT_EQ(frames[0].spatial_id, 1);
  EXPECT_EQ(frames[1].spatial_id, 1);
  EXPECT_EQ(frames[2].spatial_id, 1);
  EXPECT_EQ(frames[3].spatial_id, 1);

  EXPECT_EQ(frames[0].temporal_id, 0);
  EXPECT_EQ(frames[1].temporal_id, 1);
  EXPECT_EQ(frames[2].temporal_id, 0);
  EXPECT_EQ(frames[3].temporal_id, 1);

  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
}

// S1T1     3  |6   8
//         /   /   /
// S1T0   1---5+--7
//        |    |
// S0T1   |   4|
//        |  / |
// S0T0   0-2  |
// Time-> 0 1 2 3 4 5
TEST(ScalabilityStructureL2T2KeyShiftTest, DisableS0AfterFewFrames) {
  ScalabilityStructureL2T2KeyShift structure;
  ScalabilityStructureWrapper wrapper(structure);
  std::vector<GenericFrameInfo> frames;

  wrapper.GenerateFrames(/*num_temporal_units=*/3, frames);
  EXPECT_THAT(frames, SizeIs(6));
  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/0, /*s1=*/2));
  wrapper.GenerateFrames(/*num_temporal_units=*/3, frames);
  ASSERT_THAT(frames, SizeIs(9));

  // Expect frame[6] is delta frame.
  EXPECT_THAT(frames[6].frame_diffs, ElementsAre(1));
  // Skip validation before S0 was disabled as that should be covered by
  // test where no layers are disabled.
  EXPECT_EQ(frames[6].spatial_id, 1);
  EXPECT_EQ(frames[7].spatial_id, 1);
  EXPECT_EQ(frames[8].spatial_id, 1);

  EXPECT_EQ(frames[6].temporal_id, 1);
  EXPECT_EQ(frames[7].temporal_id, 0);
  EXPECT_EQ(frames[8].temporal_id, 1);

  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
}

// S1T1     3| |  8
//         / | | /
// S1T0   1  | |6
//        |  | ||
// S0T1   |  |4||
//        |  / ||
// S0T0   0-2| |5-7
// Time-> 0 1 2 3 4 5
TEST(ScalabilityStructureL2T2KeyShiftTest, ReenableS1TriggersKeyFrame) {
  ScalabilityStructureL2T2KeyShift structure;
  ScalabilityStructureWrapper wrapper(structure);
  std::vector<GenericFrameInfo> frames;

  wrapper.GenerateFrames(/*num_temporal_units=*/2, frames);
  EXPECT_THAT(frames, SizeIs(4));

  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/2, /*s1=*/0));
  wrapper.GenerateFrames(/*num_temporal_units=*/1, frames);
  EXPECT_THAT(frames, SizeIs(5));

  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/2, /*s1=*/2));
  wrapper.GenerateFrames(/*num_temporal_units=*/2, frames);
  ASSERT_THAT(frames, SizeIs(9));

  EXPECT_THAT(frames[4].spatial_id, 0);
  EXPECT_THAT(frames[4].temporal_id, 1);

  // Expect frame[5] to be a key frame.
  EXPECT_TRUE(wrapper.FrameReferencesAreValid(std::span(frames.data() + 5, 4)));

  EXPECT_THAT(frames[5].spatial_id, 0);
  EXPECT_THAT(frames[6].spatial_id, 1);
  EXPECT_THAT(frames[7].spatial_id, 0);
  EXPECT_THAT(frames[8].spatial_id, 1);

  // S0 should do temporal shift after the key frame.
  EXPECT_THAT(frames[5].temporal_id, 0);
  EXPECT_THAT(frames[7].temporal_id, 0);

  // No temporal shift for the top spatial layer.
  EXPECT_THAT(frames[6].temporal_id, 0);
  EXPECT_THAT(frames[8].temporal_id, 1);
}

TEST(ScalabilityStructureL2T2KeyShiftTest, EnableOnlyS0T0FromTheStart) {
  ScalabilityStructureL2T2KeyShift structure;
  ScalabilityStructureWrapper wrapper(structure);
  std::vector<GenericFrameInfo> frames;

  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/1, /*s1=*/0));
  wrapper.GenerateFrames(/*num_temporal_units=*/3, frames);
  ASSERT_THAT(frames, SizeIs(3));

  EXPECT_EQ(frames[0].spatial_id, 0);
  EXPECT_EQ(frames[1].spatial_id, 0);
  EXPECT_EQ(frames[2].spatial_id, 0);

  EXPECT_EQ(frames[0].temporal_id, 0);
  EXPECT_EQ(frames[1].temporal_id, 0);
  EXPECT_EQ(frames[2].temporal_id, 0);

  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
}

// S1T1     3|
//         / |
// S1T0   1  |
//        |  |
// S0T1   |  |
//        |  |
// S0T0   0-2+4-5-6
// Time-> 0 1 2 3 4
TEST(ScalabilityStructureL2T2KeyShiftTest, EnableOnlyS0T0AfterFewFrames) {
  ScalabilityStructureL2T2KeyShift structure;
  ScalabilityStructureWrapper wrapper(structure);
  std::vector<GenericFrameInfo> frames;

  wrapper.GenerateFrames(/*num_temporal_units=*/2, frames);
  EXPECT_THAT(frames, SizeIs(4));
  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/1, /*s1=*/0));
  wrapper.GenerateFrames(/*num_temporal_units=*/3, frames);
  ASSERT_THAT(frames, SizeIs(7));

  EXPECT_EQ(frames[4].spatial_id, 0);
  EXPECT_EQ(frames[5].spatial_id, 0);
  EXPECT_EQ(frames[6].spatial_id, 0);

  EXPECT_EQ(frames[4].temporal_id, 0);
  EXPECT_EQ(frames[5].temporal_id, 0);
  EXPECT_EQ(frames[6].temporal_id, 0);

  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
}

TEST(ScalabilityStructureL2T2KeyShiftTest, EnableOnlyS1T0FromTheStart) {
  ScalabilityStructureL2T2KeyShift structure;
  ScalabilityStructureWrapper wrapper(structure);
  std::vector<GenericFrameInfo> frames;

  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/0, /*s1=*/1));
  wrapper.GenerateFrames(/*num_temporal_units=*/3, frames);
  ASSERT_THAT(frames, SizeIs(3));

  EXPECT_EQ(frames[0].spatial_id, 1);
  EXPECT_EQ(frames[1].spatial_id, 1);
  EXPECT_EQ(frames[2].spatial_id, 1);

  EXPECT_EQ(frames[0].temporal_id, 0);
  EXPECT_EQ(frames[1].temporal_id, 0);
  EXPECT_EQ(frames[2].temporal_id, 0);

  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
}

// S1T1     3|
//         / |
// S1T0   1--+4-5-6
//        |  |
// S0T1   |  |
//        |  |
// S0T0   0-2|
// Time-> 0 1 2 3 4
TEST(ScalabilityStructureL2T2KeyShiftTest, EnableOnlyS1T0AfterFewFrames) {
  ScalabilityStructureL2T2KeyShift structure;
  ScalabilityStructureWrapper wrapper(structure);
  std::vector<GenericFrameInfo> frames;

  wrapper.GenerateFrames(/*num_temporal_units=*/2, frames);
  EXPECT_THAT(frames, SizeIs(4));
  structure.OnRatesUpdated(EnableTemporalLayers(/*s0=*/0, /*s1=*/1));
  wrapper.GenerateFrames(/*num_temporal_units=*/3, frames);
  ASSERT_THAT(frames, SizeIs(7));

  EXPECT_EQ(frames[4].spatial_id, 1);
  EXPECT_EQ(frames[5].spatial_id, 1);
  EXPECT_EQ(frames[6].spatial_id, 1);

  EXPECT_EQ(frames[4].temporal_id, 0);
  EXPECT_EQ(frames[5].temporal_id, 0);
  EXPECT_EQ(frames[6].temporal_id, 0);

  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
}

TEST(ScalabilityStructureL2T2KeyShiftTest,
     SkippingFrameByEncoderReusesPatternAndKeepsReferencesValid) {
  ScalabilityStructureL2T2KeyShift structure;
  ScalabilityStructureWrapper wrapper(structure);
  std::vector<GenericFrameInfo> frames;

  // Simulate encoder dropping the initial keyframe (neither OnEncodeDone nor
  // OnFrameDropped called).
  structure.NextFrameConfig(/*restart=*/false);

  // Next call should still produce the initial keyframe.
  wrapper.GenerateFrames(/*num_temporal_units=*/1, frames);
  ASSERT_THAT(frames, SizeIs(2));
  EXPECT_THAT(frames[0].frame_diffs, IsEmpty());
  EXPECT_THAT(frames[1].frame_diffs, ElementsAre(1));

  // Simulate encoder dropping kDelta0 (S0T0 + S1T1).
  std::vector<LayerFrameConfig> skipped_delta =
      structure.NextFrameConfig(/*restart=*/false);
  ASSERT_THAT(skipped_delta, SizeIs(2));
  EXPECT_EQ(skipped_delta[0].TemporalId(), 0);
  EXPECT_EQ(skipped_delta[1].TemporalId(), 1);

  // Next call should reuse kDelta0 rather than advancing to kDelta1.
  wrapper.GenerateFrames(/*num_temporal_units=*/2, frames);
  ASSERT_THAT(frames, SizeIs(6));
  EXPECT_EQ(frames[2].spatial_id, 0);
  EXPECT_EQ(frames[2].temporal_id, 0);
  EXPECT_EQ(frames[3].spatial_id, 1);
  EXPECT_EQ(frames[3].temporal_id, 1);
  EXPECT_EQ(frames[4].spatial_id, 0);
  EXPECT_EQ(frames[4].temporal_id, 1);
  EXPECT_EQ(frames[5].spatial_id, 1);
  EXPECT_EQ(frames[5].temporal_id, 0);
  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
}

TEST(ScalabilityStructureL2T2KeyShiftTest,
     OnFrameDroppedAdvancesPatternAndKeepsReferencesValid) {
  ScalabilityStructureL2T2KeyShift structure;
  ScalabilityStructureWrapper wrapper(structure);
  std::vector<GenericFrameInfo> frames;

  // Encode initial keyframe (S0T0, S1T0).
  wrapper.GenerateFrames(/*num_temporal_units=*/1, frames);
  ASSERT_THAT(frames, SizeIs(2));

  // Drop kDelta0 (S0T0 + S1T1) via OnFrameDropped.
  std::vector<LayerFrameConfig> delta0 =
      structure.NextFrameConfig(/*restart=*/false);
  ASSERT_THAT(delta0, SizeIs(2));
  EXPECT_EQ(delta0[0].TemporalId(), 0);
  EXPECT_EQ(delta0[1].TemporalId(), 1);
  for (const LayerFrameConfig& config : delta0) {
    structure.OnFrameDropped(config);
  }

  // Pattern advances to kDelta1 (S0T1 + S1T0), referencing the keyframe's T0
  // buffers.
  wrapper.GenerateFrames(/*num_temporal_units=*/1, frames);
  ASSERT_THAT(frames, SizeIs(4));
  EXPECT_EQ(frames[2].spatial_id, 0);
  EXPECT_EQ(frames[2].temporal_id, 1);
  EXPECT_THAT(frames[2].frame_diffs, ElementsAre(2));
  EXPECT_EQ(frames[3].spatial_id, 1);
  EXPECT_EQ(frames[3].temporal_id, 0);
  EXPECT_THAT(frames[3].frame_diffs, ElementsAre(2));

  // Next TU is kDelta0 (S0T0 + S1T1); drop only enhancement layer S1T1 while
  // encoding S0T0 (frames[4]).
  delta0 = structure.NextFrameConfig(/*restart=*/false);
  ASSERT_THAT(delta0, SizeIs(2));
  wrapper.EncodeTemporalUnit(std::span(delta0).first(1), frames);
  structure.OnFrameDropped(delta0[1]);

  // Next TU is kDelta1 (S0T1 + S1T0); drop only enhancement layer S0T1 while
  // encoding S1T0 (frames[5]).
  std::vector<LayerFrameConfig> delta1 =
      structure.NextFrameConfig(/*restart=*/false);
  ASSERT_THAT(delta1, SizeIs(2));
  structure.OnFrameDropped(delta1[0]);
  wrapper.EncodeTemporalUnit(std::span(delta1).subspan(1), frames);

  // Next TU is kDelta0 (S0T0 + S1T1); drop S0T0 while encoding S1T1 (frames[6],
  // which only references S1's T0 buffer in frames[5]).
  delta0 = structure.NextFrameConfig(/*restart=*/false);
  ASSERT_THAT(delta0, SizeIs(2));
  EXPECT_EQ(delta0[0].SpatialId(), 0);
  EXPECT_EQ(delta0[0].TemporalId(), 0);
  EXPECT_EQ(delta0[1].SpatialId(), 1);
  EXPECT_EQ(delta0[1].TemporalId(), 1);
  structure.OnFrameDropped(delta0[0]);
  wrapper.EncodeTemporalUnit(std::span(delta0).subspan(1), frames);
  ASSERT_THAT(frames, SizeIs(7));
  EXPECT_THAT(frames[6].frame_diffs, ElementsAre(1));

  // Subsequent kDelta1 and kDelta0 fall back to the earlier S0T0 (frames[4]).
  wrapper.GenerateFrames(/*num_temporal_units=*/2, frames);
  ASSERT_THAT(frames, SizeIs(11));
  EXPECT_EQ(frames[7].spatial_id, 0);
  EXPECT_EQ(frames[7].temporal_id, 1);
  EXPECT_THAT(frames[7].frame_diffs, ElementsAre(3));
  EXPECT_EQ(frames[9].spatial_id, 0);
  EXPECT_EQ(frames[9].temporal_id, 0);
  EXPECT_THAT(frames[9].frame_diffs, ElementsAre(5));
  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
}

TEST(ScalabilityStructureL2T2KeyShiftTest,
     OnFrameDroppedForS1OnKeyframeRecoversWithoutInvalidReferences) {
  ScalabilityStructureL2T2KeyShift structure;
  ScalabilityStructureWrapper wrapper(structure);
  std::vector<GenericFrameInfo> frames;

  std::vector<LayerFrameConfig> key_configs =
      structure.NextFrameConfig(/*restart=*/false);
  ASSERT_THAT(key_configs, SizeIs(2));
  // Encode S0T0, drop S1T0 on the initial keyframe.
  wrapper.EncodeTemporalUnit(std::span(key_configs).first(1), frames);
  structure.OnFrameDropped(key_configs[1]);

  // kDelta0 emits only S0T0 since S1 has no T0 reference yet.
  std::vector<LayerFrameConfig> delta0 =
      structure.NextFrameConfig(/*restart=*/false);
  ASSERT_THAT(delta0, SizeIs(1));
  EXPECT_EQ(delta0[0].SpatialId(), 0);
  EXPECT_EQ(delta0[0].TemporalId(), 0);
  wrapper.EncodeTemporalUnit(delta0, frames);

  // At S1's T0 slot (kDelta1), a keyframe is emitted to initialize S1.
  std::vector<LayerFrameConfig> recovery =
      structure.NextFrameConfig(/*restart=*/false);
  ASSERT_THAT(recovery, SizeIs(2));
  EXPECT_TRUE(recovery[0].IsKeyframe());
  wrapper.EncodeTemporalUnit(recovery, frames);
  wrapper.GenerateFrames(/*num_temporal_units=*/2, frames);
  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
}

}  // namespace
}  // namespace webrtc
