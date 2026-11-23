/*
 *  Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "modules/video_coding/svc/scalable_video_controller_no_layering.h"

#include <vector>

#include "api/transport/rtp/dependency_descriptor.h"
#include "api/video/video_bitrate_allocation.h"
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

TEST(ScalableVideoControllerNoLayeringTest, StreamAndDependencyConfig) {
  ScalableVideoControllerNoLayering controller;

  ScalableVideoController::StreamLayersConfig stream_config =
      controller.StreamConfig();
  EXPECT_EQ(stream_config.num_spatial_layers, 1);
  EXPECT_EQ(stream_config.num_temporal_layers, 1);
  EXPECT_FALSE(stream_config.uses_reference_scaling);

  FrameDependencyStructure dep_structure = controller.DependencyStructure();
  EXPECT_EQ(dep_structure.num_decode_targets, 1);
  EXPECT_EQ(dep_structure.num_chains, 1);
  EXPECT_THAT(dep_structure.decode_target_protected_by_chain, ElementsAre(0));
  ASSERT_THAT(dep_structure.templates, SizeIs(2));
  EXPECT_THAT(dep_structure.templates[0].decode_target_indications,
              ElementsAre(DecodeTargetIndication::kSwitch));
  EXPECT_THAT(dep_structure.templates[0].frame_diffs, IsEmpty());
  EXPECT_THAT(dep_structure.templates[0].chain_diffs, ElementsAre(0));
  EXPECT_THAT(dep_structure.templates[1].decode_target_indications,
              ElementsAre(DecodeTargetIndication::kSwitch));
  EXPECT_THAT(dep_structure.templates[1].frame_diffs, ElementsAre(1));
  EXPECT_THAT(dep_structure.templates[1].chain_diffs, ElementsAre(1));
}

TEST(ScalableVideoControllerNoLayeringTest,
     GeneratesKeyframeThenDeltaFramesAndHandlesRestart) {
  ScalableVideoControllerNoLayering controller;
  ScalabilityStructureWrapper wrapper(controller);
  std::vector<GenericFrameInfo> frames;

  wrapper.GenerateFrames(/*num_temporal_units=*/3, frames);
  ASSERT_THAT(frames, SizeIs(3));
  EXPECT_THAT(frames[0].frame_diffs, IsEmpty());
  EXPECT_THAT(frames[0].chain_diffs, ElementsAre(0));
  EXPECT_THAT(frames[1].frame_diffs, ElementsAre(1));
  EXPECT_THAT(frames[1].chain_diffs, ElementsAre(1));
  EXPECT_THAT(frames[2].frame_diffs, ElementsAre(1));
  EXPECT_THAT(frames[2].chain_diffs, ElementsAre(1));

  std::vector<LayerFrameConfig> restart_configs =
      controller.NextFrameConfig(/*restart=*/true);
  ASSERT_THAT(restart_configs, SizeIs(1));
  EXPECT_TRUE(restart_configs[0].IsKeyframe());
  wrapper.EncodeTemporalUnit(restart_configs, frames);

  wrapper.GenerateFrames(/*num_temporal_units=*/1, frames);
  ASSERT_THAT(frames, SizeIs(5));
  EXPECT_THAT(frames[3].frame_diffs, IsEmpty());
  EXPECT_THAT(frames[3].chain_diffs, ElementsAre(0));
  EXPECT_THAT(frames[4].frame_diffs, ElementsAre(1));
  EXPECT_THAT(frames[4].chain_diffs, ElementsAre(1));
  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
}

TEST(ScalableVideoControllerNoLayeringTest,
     SkippingInitialOrRestartKeyframePreservesKeyframeRequest) {
  ScalableVideoControllerNoLayering controller;
  ScalabilityStructureWrapper wrapper(controller);
  std::vector<GenericFrameInfo> frames;

  // Encoder silently drops initial keyframe (neither OnEncodeDone nor
  // OnFrameDropped called).
  std::vector<LayerFrameConfig> initial_configs =
      controller.NextFrameConfig(/*restart=*/false);
  ASSERT_THAT(initial_configs, SizeIs(1));
  EXPECT_TRUE(initial_configs[0].IsKeyframe());

  // Next frame must still be a keyframe.
  std::vector<LayerFrameConfig> retry_configs =
      controller.NextFrameConfig(/*restart=*/false);
  ASSERT_THAT(retry_configs, SizeIs(1));
  EXPECT_TRUE(retry_configs[0].IsKeyframe());
  wrapper.EncodeTemporalUnit(retry_configs, frames);

  // Encoder silently drops a requested restart keyframe.
  std::vector<LayerFrameConfig> restart_configs =
      controller.NextFrameConfig(/*restart=*/true);
  ASSERT_THAT(restart_configs, SizeIs(1));
  EXPECT_TRUE(restart_configs[0].IsKeyframe());

  // Next frame with restart=false must still emit a keyframe.
  std::vector<LayerFrameConfig> after_restart_drop =
      controller.NextFrameConfig(/*restart=*/false);
  ASSERT_THAT(after_restart_drop, SizeIs(1));
  EXPECT_TRUE(after_restart_drop[0].IsKeyframe());
  wrapper.EncodeTemporalUnit(after_restart_drop, frames);

  wrapper.GenerateFrames(/*num_temporal_units=*/1, frames);
  ASSERT_THAT(frames, SizeIs(3));
  EXPECT_THAT(frames[0].frame_diffs, IsEmpty());
  EXPECT_THAT(frames[1].frame_diffs, IsEmpty());
  EXPECT_THAT(frames[2].frame_diffs, ElementsAre(1));
  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
}

TEST(ScalableVideoControllerNoLayeringTest,
     OnFrameDroppedOnInitialOrRestartKeyframePreservesKeyframeRequest) {
  ScalableVideoControllerNoLayering controller;
  ScalabilityStructureWrapper wrapper(controller);
  std::vector<GenericFrameInfo> frames;

  // Drop the initial keyframe via OnFrameDropped.
  std::vector<LayerFrameConfig> initial_configs =
      controller.NextFrameConfig(/*restart=*/false);
  ASSERT_THAT(initial_configs, SizeIs(1));
  EXPECT_TRUE(initial_configs[0].IsKeyframe());
  controller.OnFrameDropped(initial_configs[0]);

  // Next frame must still be a keyframe since buffer 0 has never been updated.
  std::vector<LayerFrameConfig> first_encoded =
      controller.NextFrameConfig(/*restart=*/false);
  ASSERT_THAT(first_encoded, SizeIs(1));
  EXPECT_TRUE(first_encoded[0].IsKeyframe());
  wrapper.EncodeTemporalUnit(first_encoded, frames);

  wrapper.GenerateFrames(/*num_temporal_units=*/1, frames);

  // Request a restart keyframe and drop it via OnFrameDropped.
  std::vector<LayerFrameConfig> restart_configs =
      controller.NextFrameConfig(/*restart=*/true);
  ASSERT_THAT(restart_configs, SizeIs(1));
  EXPECT_TRUE(restart_configs[0].IsKeyframe());
  controller.OnFrameDropped(restart_configs[0]);

  // Next frame with restart=false must still be a keyframe.
  std::vector<LayerFrameConfig> after_restart_drop =
      controller.NextFrameConfig(/*restart=*/false);
  ASSERT_THAT(after_restart_drop, SizeIs(1));
  EXPECT_TRUE(after_restart_drop[0].IsKeyframe());
  wrapper.EncodeTemporalUnit(after_restart_drop, frames);

  wrapper.GenerateFrames(/*num_temporal_units=*/1, frames);
  ASSERT_THAT(frames, SizeIs(4));
  EXPECT_THAT(frames[0].frame_diffs, IsEmpty());
  EXPECT_THAT(frames[1].frame_diffs, ElementsAre(1));
  EXPECT_THAT(frames[2].frame_diffs, IsEmpty());
  EXPECT_THAT(frames[3].frame_diffs, ElementsAre(1));
  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
}

TEST(ScalableVideoControllerNoLayeringTest,
     OnFrameDroppedOnDeltaFrameKeepsReferencingPreviousFrame) {
  ScalableVideoControllerNoLayering controller;
  ScalabilityStructureWrapper wrapper(controller);
  std::vector<GenericFrameInfo> frames;

  // Encode initial keyframe into buffer 0.
  wrapper.GenerateFrames(/*num_temporal_units=*/1, frames);
  ASSERT_THAT(frames, SizeIs(1));

  // Drop a delta frame via OnFrameDropped.
  std::vector<LayerFrameConfig> dropped_delta =
      controller.NextFrameConfig(/*restart=*/false);
  ASSERT_THAT(dropped_delta, SizeIs(1));
  EXPECT_FALSE(dropped_delta[0].IsKeyframe());
  controller.OnFrameDropped(dropped_delta[0]);

  // Next frame should remain a delta frame referencing buffer 0 (not forced to
  // a keyframe).
  std::vector<LayerFrameConfig> next_delta =
      controller.NextFrameConfig(/*restart=*/false);
  ASSERT_THAT(next_delta, SizeIs(1));
  EXPECT_FALSE(next_delta[0].IsKeyframe());
  wrapper.EncodeTemporalUnit(next_delta, frames);

  ASSERT_THAT(frames, SizeIs(2));
  EXPECT_THAT(frames[1].frame_diffs, ElementsAre(1));
  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
}

TEST(ScalableVideoControllerNoLayeringTest,
     DisablingAndReenablingViaOnRatesUpdated) {
  ScalableVideoControllerNoLayering controller;
  ScalabilityStructureWrapper wrapper(controller);
  std::vector<GenericFrameInfo> frames;

  VideoBitrateAllocation zero_bitrates;
  zero_bitrates.SetBitrate(0, 0, 0);
  controller.OnRatesUpdated(zero_bitrates);
  EXPECT_THAT(controller.NextFrameConfig(/*restart=*/false), IsEmpty());
  EXPECT_THAT(controller.NextFrameConfig(/*restart=*/true), IsEmpty());

  // Re-enabling before any frame was encoded still starts with a keyframe.
  controller.OnRatesUpdated(EnableTemporalLayers(/*s0=*/1));
  wrapper.GenerateFrames(/*num_temporal_units=*/2, frames);
  ASSERT_THAT(frames, SizeIs(2));
  EXPECT_THAT(frames[0].frame_diffs, IsEmpty());
  EXPECT_THAT(frames[1].frame_diffs, ElementsAre(1));

  // If `NextFrameConfig(/*restart=*/true)` is called while disabled after
  // frames have already been encoded, the restart request is latched so
  // re-enabling emits a keyframe on the next
  // `NextFrameConfig(/*restart=*/false)`.
  controller.OnRatesUpdated(zero_bitrates);
  EXPECT_THAT(controller.NextFrameConfig(/*restart=*/true), IsEmpty());

  controller.OnRatesUpdated(EnableTemporalLayers(/*s0=*/1));
  std::vector<LayerFrameConfig> reenabled_configs =
      controller.NextFrameConfig(/*restart=*/false);
  ASSERT_THAT(reenabled_configs, SizeIs(1));
  EXPECT_TRUE(reenabled_configs[0].IsKeyframe());
  wrapper.EncodeTemporalUnit(reenabled_configs, frames);
  wrapper.GenerateFrames(/*num_temporal_units=*/1, frames);

  ASSERT_THAT(frames, SizeIs(4));
  EXPECT_THAT(frames[2].frame_diffs, IsEmpty());
  EXPECT_THAT(frames[3].frame_diffs, ElementsAre(1));
  EXPECT_TRUE(wrapper.FrameReferencesAreValid(frames));
}

}  // namespace
}  // namespace webrtc
