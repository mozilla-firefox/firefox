/*
 *  Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_coding/svc/reference_buffer_manager.h"

#include <optional>
#include <vector>

#include "api/units/timestamp.h"
#include "api/video_codecs/video_encoder_interface.h"
#include "rtc_base/checks.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::Optional;

using Buffer = ReferenceBufferManager::Buffer;
using Metadata = ReferenceBufferManager::Metadata;
using PredictionConstraints = ReferenceBufferManager::PredictionConstraints;
using BufferSpaceType = PredictionConstraints::BufferSpaceType;
using FrameType = VideoEncoderInterface::FrameType;

PredictionConstraints Constraints(BufferSpaceType type,
                                  int num_buffers,
                                  int num_spatial_layers = 1) {
  PredictionConstraints constraints;
  constraints.set_buffer_space_type(type);
  constraints.set_num_buffers(num_buffers);
  constraints.set_max_spatial_layers(num_spatial_layers);
  return constraints;
}

Metadata MakeMetadata(FrameType type,
                      int spatial_id,
                      std::optional<int> temporal_id,
                      Timestamp timestamp = Timestamp::Zero()) {
  return Metadata{.type = type,
                  .spatial_id = spatial_id,
                  .temporal_id = temporal_id,
                  .timestamp = timestamp};
}

Metadata Keyframe(int spatial_id = 0) {
  return MakeMetadata(FrameType::kKeyframe, spatial_id, 0);
}

Metadata Delta(int spatial_id,
               int temporal_id,
               Timestamp timestamp = Timestamp::Zero()) {
  return MakeMetadata(FrameType::kDeltaFrame, spatial_id, temporal_id,
                      timestamp);
}

Metadata WithFrameId(int frame_id, int spatial_id = 0) {
  Metadata metadata = Delta(spatial_id, 0);
  metadata.frame_id = frame_id;
  return metadata;
}

// Reserves a buffer, which the test setup requires to succeed.
Buffer Reserve(ReferenceBufferManager& manager, int spatial_id = 0) {
  std::optional<Buffer> buffer = manager.ReserveBuffer(spatial_id);
  RTC_CHECK(buffer.has_value());
  return *buffer;
}

// Reserves and populates a buffer, returning it.
Buffer Populate(ReferenceBufferManager& manager, const Metadata& metadata) {
  Buffer buffer = Reserve(manager, metadata.spatial_id);
  manager.SetBufferMetadata(buffer, metadata);
  return buffer;
}

// Reservation.

TEST(ReferenceBufferManagerTest, ReservesLowestFreeBuffer) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 3));
  EXPECT_THAT(manager.ReserveBuffer(0),
              Optional(Buffer{.id = 0, .spatial_id = 0}));
  EXPECT_THAT(manager.ReserveBuffer(0),
              Optional(Buffer{.id = 1, .spatial_id = 0}));
  manager.ReleaseBuffer({.id = 0, .spatial_id = 0});
  EXPECT_THAT(manager.ReserveBuffer(0),
              Optional(Buffer{.id = 0, .spatial_id = 0}));
}

TEST(ReferenceBufferManagerTest, NumFreeBuffersExcludesReservedAndPopulated) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 3));
  EXPECT_EQ(manager.NumFreeBuffers(0), 3);
  Reserve(manager);
  EXPECT_EQ(manager.NumFreeBuffers(0), 2);
  Populate(manager, Keyframe());
  EXPECT_EQ(manager.NumFreeBuffers(0), 1);
}

TEST(ReferenceBufferManagerTest, ReserveFailsWhenAllBuffersAreInUse) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 2));
  Populate(manager, Keyframe());
  Reserve(manager);
  EXPECT_EQ(manager.ReserveBuffer(0), std::nullopt);
}

// Metadata.

TEST(ReferenceBufferManagerTest, ReservedBufferIsNotVisibleToQueries) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 2));
  Buffer reserved = Reserve(manager);
  EXPECT_EQ(manager.GetLastTl0(0), std::nullopt);
  EXPECT_EQ(manager.GetMetadata(reserved), std::nullopt);
}

TEST(ReferenceBufferManagerTest, GetMetadataReturnsPopulatedMetadata) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 2));
  Buffer buffer = Populate(manager, Delta(0, 1, Timestamp::Seconds(1)));
  EXPECT_THAT(
      manager.GetMetadata(buffer),
      Optional(AllOf(Field(&Metadata::type, FrameType::kDeltaFrame),
                     Field(&Metadata::spatial_id, 0),
                     Field(&Metadata::temporal_id, Optional(1)),
                     Field(&Metadata::timestamp, Timestamp::Seconds(1)))));
}

TEST(ReferenceBufferManagerTest, GetMetadataIgnoresHandleOfOtherLayer) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kMultiKeyframe, 1, 2));
  Populate(manager, Keyframe(0));
  EXPECT_EQ(manager.GetMetadata({.id = 0, .spatial_id = 1}), std::nullopt);
}

// Queries.

TEST(ReferenceBufferManagerTest, GetReturnsMostRecentlyPopulatedBuffer) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 3));
  // Populate in reverse buffer index order and with decreasing timestamps to
  // verify that neither index nor timestamp defines "last".
  Buffer b0 = Reserve(manager);
  Buffer b1 = Reserve(manager);
  manager.SetBufferMetadata(b1, Delta(0, 0, Timestamp::Seconds(2)));
  manager.SetBufferMetadata(b0, Delta(0, 0, Timestamp::Seconds(1)));
  EXPECT_THAT(manager.Get(0, 0), Optional(b0));
}

TEST(ReferenceBufferManagerTest, GetFiltersOnTemporalId) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 3));
  Buffer t0 = Populate(manager, Keyframe());
  Buffer t1 = Populate(manager, Delta(0, 1));
  EXPECT_THAT(manager.Get(0, 0), Optional(t0));
  EXPECT_THAT(manager.Get(0, 1), Optional(t1));
  EXPECT_EQ(manager.Get(0, 2), std::nullopt);
}

TEST(ReferenceBufferManagerTest, GetFiltersOnSpatialIdInSharedPool) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kMultiKeyframe, 2, 2));
  Buffer s0 = Populate(manager, Keyframe(0));
  Populate(manager, Delta(1, 0));
  EXPECT_THAT(manager.Get(0, 0), Optional(s0));
}

TEST(ReferenceBufferManagerTest, GetLastTl0IgnoresHigherTemporalLayers) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 2));
  Buffer t0 = Populate(manager, Keyframe());
  Populate(manager, Delta(0, 1));
  EXPECT_THAT(manager.GetLastTl0(0), Optional(t0));
}

TEST(ReferenceBufferManagerTest, GetAllLowerTemporalLayersSkipsMissing) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 4));
  Buffer t0 = Populate(manager, Keyframe());
  Buffer t2 = Populate(manager, Delta(0, 2));
  Populate(manager, Delta(0, 3));
  EXPECT_THAT(manager.GetAllLowerTemporalLayers(0, 3), ElementsAre(t0, t2));
  EXPECT_THAT(manager.GetAllLowerTemporalLayers(0, 0), IsEmpty());
}

// SortByRecency.

TEST(ReferenceBufferManagerTest, SortByRecencyPutsMostRecentFirst) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 3));
  // Populate in reverse buffer index order and with decreasing timestamps to
  // verify that neither index nor timestamp defines the order.
  Buffer b0 = Reserve(manager);
  Buffer b1 = Reserve(manager);
  Buffer b2 = Reserve(manager);
  manager.SetBufferMetadata(b1, Delta(0, 0, Timestamp::Seconds(3)));
  manager.SetBufferMetadata(b2, Delta(0, 1, Timestamp::Seconds(2)));
  manager.SetBufferMetadata(b0, Delta(0, 0, Timestamp::Seconds(1)));
  EXPECT_THAT(manager.SortByRecency(std::vector{b1, b2, b0}),
              ElementsAre(b0, b2, b1));
}

TEST(ReferenceBufferManagerTest, SortByRecencySkipsUnpopulatedBuffers) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 3));
  Buffer reserved = Reserve(manager);
  Buffer populated = Populate(manager, Keyframe());
  Buffer free_buffer = {.id = 2, .spatial_id = 0};
  EXPECT_THAT(
      manager.SortByRecency(std::vector{free_buffer, reserved, populated}),
      ElementsAre(populated));
}

TEST(ReferenceBufferManagerTest, SortByRecencyKeepsDuplicates) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 2));
  Buffer old_buffer = Populate(manager, Keyframe());
  Buffer new_buffer = Populate(manager, Delta(0, 0));
  EXPECT_THAT(
      manager.SortByRecency(std::vector{old_buffer, new_buffer, old_buffer}),
      ElementsAre(new_buffer, old_buffer, old_buffer));
}

// Releasing.

TEST(ReferenceBufferManagerTest, ReleaseBufferFreesPopulatedBuffer) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 1));
  Buffer buffer = Populate(manager, Keyframe());
  manager.ReleaseBuffer(buffer);
  EXPECT_EQ(manager.GetMetadata(buffer), std::nullopt);
  EXPECT_EQ(manager.NumFreeBuffers(0), 1);
}

TEST(ReferenceBufferManagerTest, ReleaseBufferFreesReservedBuffer) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 1));
  manager.ReleaseBuffer(Reserve(manager));
  EXPECT_EQ(manager.NumFreeBuffers(0), 1);
}

TEST(ReferenceBufferManagerTest, ReleaseBufferOfFreeBufferIsNoOp) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 2));
  Buffer populated = Populate(manager, Keyframe());
  manager.ReleaseBuffer({.id = 1, .spatial_id = 0});
  EXPECT_THAT(manager.GetLastTl0(0), Optional(populated));
  EXPECT_EQ(manager.NumFreeBuffers(0), 1);
}

TEST(ReferenceBufferManagerTest,
     ReleaseBufferIgnoresBufferReservedByOtherLayer) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kMultiKeyframe, 1, 2));
  Buffer stale = Populate(manager, Keyframe(0));
  manager.ReleaseBuffer(stale);
  ASSERT_EQ(Reserve(manager, 1), (Buffer{.id = 0, .spatial_id = 1}));
  manager.ReleaseBuffer(stale);
  EXPECT_EQ(manager.NumFreeBuffers(1), 0);
}

TEST(ReferenceBufferManagerTest, ReleaseAllHigherTemporalLayers) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 4));
  Buffer t0 = Populate(manager, Keyframe());
  Buffer t1 = Populate(manager, Delta(0, 1));
  Populate(manager, Delta(0, 2));
  manager.ReleaseAllHigherTemporalLayers(0, 1);
  EXPECT_THAT(manager.GetAllLowerTemporalLayers(0, 3), ElementsAre(t0, t1));
}

TEST(ReferenceBufferManagerTest,
     ReleaseAllHigherTemporalLayersKeepsBuffersWithoutTemporalId) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 1));
  Buffer buffer =
      Populate(manager, MakeMetadata(FrameType::kDeltaFrame, 0, std::nullopt));
  manager.ReleaseAllHigherTemporalLayers(0, 0);
  EXPECT_NE(manager.GetMetadata(buffer), std::nullopt);
}

TEST(ReferenceBufferManagerTest, ReleaseAllHigherTemporalLayersKeepsReserved) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 2));
  Populate(manager, Keyframe());
  Buffer reserved = Reserve(manager);
  manager.ReleaseAllHigherTemporalLayers(0, 0);
  EXPECT_EQ(manager.NumFreeBuffers(0), 0);
  // Still reserved, so populating it must succeed.
  manager.SetBufferMetadata(reserved, Delta(0, 1));
  EXPECT_THAT(manager.Get(0, 1), Optional(reserved));
}

TEST(ReferenceBufferManagerTest,
     ReleaseAllPriorTemporalUnitsReleasesOnlyEarlierTimestamps) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 3));
  Buffer before = Populate(manager, Delta(0, 0, Timestamp::Seconds(1)));
  Buffer at_cutoff = Populate(manager, Delta(0, 1, Timestamp::Seconds(2)));
  Buffer after = Populate(manager, Delta(0, 0, Timestamp::Seconds(3)));
  manager.ReleaseAllPriorTemporalUnits(0, Timestamp::Seconds(2));
  EXPECT_EQ(manager.GetMetadata(before), std::nullopt);
  EXPECT_NE(manager.GetMetadata(at_cutoff), std::nullopt);
  EXPECT_NE(manager.GetMetadata(after), std::nullopt);
}

TEST(ReferenceBufferManagerTest,
     ReleaseAllPriorTemporalUnitsOnlyAffectsGivenLayer) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kMultiKeyframe, 2, 2));
  Populate(manager, Delta(0, 0, Timestamp::Seconds(1)));
  Buffer s1 = Populate(manager, Delta(1, 0, Timestamp::Seconds(1)));
  manager.ReleaseAllPriorTemporalUnits(0, Timestamp::Seconds(2));
  EXPECT_EQ(manager.GetLastTl0(0), std::nullopt);
  EXPECT_THAT(manager.GetLastTl0(1), Optional(s1));
}

TEST(ReferenceBufferManagerTest, ReleaseAllBuffersFreesReservedAndPopulated) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 2));
  Populate(manager, Keyframe());
  Reserve(manager);
  manager.ReleaseAllBuffers(0);
  EXPECT_EQ(manager.NumFreeBuffers(0), 2);
}

TEST(ReferenceBufferManagerTest, ReleaseAllBuffersOnlyAffectsGivenLayer) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kMultiKeyframe, 2, 2));
  Populate(manager, Keyframe(0));
  Buffer s1 = Populate(manager, Keyframe(1));
  manager.ReleaseAllBuffers(0);
  EXPECT_EQ(manager.GetLastTl0(0), std::nullopt);
  EXPECT_THAT(manager.GetLastTl0(1), Optional(s1));
}

// Buffer space types and keyframe invalidation.

TEST(ReferenceBufferManagerTest, SingleKeyframeInvalidatesAllLayers) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 4, 2));
  Buffer s0 = Populate(manager, Keyframe(0));
  Buffer s1 = Populate(manager, Delta(1, 0));
  Populate(manager, Keyframe(0));
  EXPECT_EQ(manager.GetMetadata(s0), std::nullopt);
  EXPECT_EQ(manager.GetMetadata(s1), std::nullopt);
}

TEST(ReferenceBufferManagerTest, MultiKeyframeInvalidatesOnlyOwnLayer) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kMultiKeyframe, 4, 2));
  Buffer s0 = Populate(manager, Keyframe(0));
  Buffer s1 = Populate(manager, Keyframe(1));
  Populate(manager, Keyframe(0));
  EXPECT_EQ(manager.GetMetadata(s0), std::nullopt);
  EXPECT_NE(manager.GetMetadata(s1), std::nullopt);
}

TEST(ReferenceBufferManagerTest, MultiKeyframeLayersShareOnePool) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kMultiKeyframe, 2, 2));
  Populate(manager, Keyframe(0));
  EXPECT_EQ(manager.NumFreeBuffers(1), 1);
  EXPECT_THAT(manager.ReserveBuffer(1),
              Optional(Buffer{.id = 1, .spatial_id = 1}));
}

TEST(ReferenceBufferManagerTest, MultiInstanceHasOnePoolPerLayer) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kMultiInstance, 1, 2));
  Populate(manager, Keyframe(0));
  EXPECT_EQ(manager.NumFreeBuffers(0), 0);
  EXPECT_THAT(manager.ReserveBuffer(1),
              Optional(Buffer{.id = 0, .spatial_id = 1}));
}

TEST(ReferenceBufferManagerTest, MultiInstanceKeyframeInvalidatesOwnInstance) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kMultiInstance, 2, 2));
  Buffer s0 = Populate(manager, Keyframe(0));
  Buffer s1 = Populate(manager, Keyframe(1));
  Populate(manager, Keyframe(0));
  EXPECT_EQ(manager.GetMetadata(s0), std::nullopt);
  EXPECT_NE(manager.GetMetadata(s1), std::nullopt);
}

TEST(ReferenceBufferManagerTest, KeyframeDoesNotInvalidateReservedBuffer) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 2));
  Buffer reserved = Reserve(manager);
  Populate(manager, Keyframe());
  EXPECT_EQ(manager.NumFreeBuffers(0), 0);
  // Still reserved, so populating it must succeed.
  manager.SetBufferMetadata(reserved, Delta(0, 0));
  EXPECT_THAT(manager.GetLastTl0(0), Optional(reserved));
}

TEST(ReferenceBufferManagerTest, StartFrameDoesNotInvalidate) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 2));
  Buffer key = Populate(manager, Keyframe());
  Populate(manager, MakeMetadata(FrameType::kStartFrame, 0, 1));
  EXPECT_NE(manager.GetMetadata(key), std::nullopt);
}

// Frame acknowledgment.

TEST(ReferenceBufferManagerTest, FrameIsNotAcknowledgedInitially) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 2));
  Populate(manager, WithFrameId(/*frame_id=*/1));
  EXPECT_EQ(manager.GetLastAcknowledgedFrame(0), std::nullopt);
}

TEST(ReferenceBufferManagerTest, AcknowledgedFrameIsReturned) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 3));
  Buffer frame1 = Populate(manager, WithFrameId(/*frame_id=*/1));
  Populate(manager, WithFrameId(/*frame_id=*/2));
  manager.MarkFrameAsAcknowledged(0, /*frame_id=*/1);
  EXPECT_THAT(manager.GetLastAcknowledgedFrame(0), Optional(frame1));
}

TEST(ReferenceBufferManagerTest, AckMarksAllBuffersHoldingFrame) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 2));
  Buffer copy1 = Populate(manager, WithFrameId(/*frame_id=*/1));
  Buffer copy2 = Populate(manager, WithFrameId(/*frame_id=*/1));
  manager.MarkFrameAsAcknowledged(0, /*frame_id=*/1);
  const auto kAcknowledged = Optional(Field(&Metadata::acknowledged, true));
  EXPECT_THAT(manager.GetMetadata(copy1), kAcknowledged);
  EXPECT_THAT(manager.GetMetadata(copy2), kAcknowledged);
}

TEST(ReferenceBufferManagerTest, LastAcknowledgedFrameIsMostRecent) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 3));
  Populate(manager, WithFrameId(/*frame_id=*/1));
  Buffer frame2 = Populate(manager, WithFrameId(/*frame_id=*/2));
  manager.MarkFrameAsAcknowledged(0, /*frame_id=*/2);
  manager.MarkFrameAsAcknowledged(0, /*frame_id=*/1);
  EXPECT_THAT(manager.GetLastAcknowledgedFrame(0), Optional(frame2));
}

TEST(ReferenceBufferManagerTest, ReleasedAcknowledgedFrameIsNotReturned) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 2));
  Buffer frame = Populate(manager, WithFrameId(/*frame_id=*/1));
  manager.MarkFrameAsAcknowledged(0, /*frame_id=*/1);
  manager.ReleaseBuffer(frame);
  EXPECT_EQ(manager.GetLastAcknowledgedFrame(0), std::nullopt);
}

TEST(ReferenceBufferManagerTest, AckForReleasedFrameIsIgnored) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 2));
  manager.ReleaseBuffer(Populate(manager, WithFrameId(/*frame_id=*/1)));
  manager.MarkFrameAsAcknowledged(0, /*frame_id=*/1);
  // A later frame reusing the same buffer is not affected by the stale ack.
  Populate(manager, WithFrameId(/*frame_id=*/2));
  EXPECT_EQ(manager.GetLastAcknowledgedFrame(0), std::nullopt);
}

TEST(ReferenceBufferManagerTest, FrameAckIsPerSpatialLayer) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kMultiKeyframe, 2, 2));
  Populate(manager, WithFrameId(/*frame_id=*/1, /*spatial_id=*/0));
  Buffer s1 = Populate(manager, WithFrameId(/*frame_id=*/1, /*spatial_id=*/1));
  manager.MarkFrameAsAcknowledged(1, /*frame_id=*/1);
  EXPECT_EQ(manager.GetLastAcknowledgedFrame(0), std::nullopt);
  EXPECT_THAT(manager.GetLastAcknowledgedFrame(1), Optional(s1));
}

TEST(ReferenceBufferManagerTest, KeyframeInvalidatesAcknowledgedFrame) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 2));
  Populate(manager, WithFrameId(/*frame_id=*/1));
  manager.MarkFrameAsAcknowledged(0, /*frame_id=*/1);
  Populate(manager, Keyframe());
  EXPECT_EQ(manager.GetLastAcknowledgedFrame(0), std::nullopt);
}

#if RTC_DCHECK_IS_ON && GTEST_HAS_DEATH_TEST && !defined(WEBRTC_ANDROID)
TEST(ReferenceBufferManagerDeathTest, SetMetadataOnUnreservedBufferDies) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 2));
  EXPECT_DEATH(
      manager.SetBufferMetadata({.id = 0, .spatial_id = 0}, Keyframe()), "");
}

TEST(ReferenceBufferManagerDeathTest, InvalidSpatialIdDies) {
  ReferenceBufferManager manager(
      Constraints(BufferSpaceType::kSingleKeyframe, 2));
  EXPECT_DEATH(manager.ReserveBuffer(1), "");
}
#endif

}  // namespace
}  // namespace webrtc
