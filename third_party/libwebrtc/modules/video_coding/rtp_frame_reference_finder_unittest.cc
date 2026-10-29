/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_coding/rtp_frame_reference_finder.h"

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <utility>

#include "api/field_trials.h"
#include "api/rtp_packet_infos.h"
#include "api/video/encoded_frame.h"
#include "api/video/encoded_image.h"
#include "api/video/video_codec_type.h"
#include "api/video/video_content_type.h"
#include "api/video/video_frame_type.h"
#include "api/video/video_rotation.h"
#include "api/video/video_timing.h"
#include "modules/rtp_rtcp/source/frame_object.h"
#include "modules/rtp_rtcp/source/rtp_video_header.h"
#include "rtc_base/random.h"
#include "test/create_test_field_trials.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

using ::testing::IsEmpty;
using ::testing::SizeIs;

std::unique_ptr<RtpFrameObject> CreateFrame(
    uint16_t seq_num_start,
    uint16_t seq_num_end,
    bool keyframe,
    VideoCodecType codec,
    const RTPVideoTypeHeader& video_type_header,
    uint32_t rtp_timestamp) {
  RTPVideoHeader video_header;
  video_header.frame_type = keyframe ? VideoFrameType::kVideoFrameKey
                                     : VideoFrameType::kVideoFrameDelta;
  video_header.video_type_header = video_type_header;

  // clang-format off
  return std::make_unique<RtpFrameObject>(
      seq_num_start,
      seq_num_end,
      /*markerBit=*/true,
      /*times_nacked=*/0,
      /*first_packet_received_time=*/std::nullopt,
      /*last_packet_received_time=*/std::nullopt,
      /*rtp_timestamp=*/rtp_timestamp,
      /*ntp_time_ms=*/0,
      VideoSendTiming(),
      /*payload_type=*/0,
      codec,
      kVideoRotation_0,
      VideoContentType::UNSPECIFIED,
      video_header,
      /*color_space=*/std::nullopt,
      /*frame_instrumentation_data=*/std::nullopt,
      RtpPacketInfos(),
      EncodedImageBuffer::Create(/*size=*/0));
  // clang-format on
}
}  // namespace

class TestRtpFrameReferenceFinder : public ::testing::Test {
 protected:
  TestRtpFrameReferenceFinder()
      : rand_(0x8739211),
        reference_finder_(std::make_unique<RtpFrameReferenceFinder>()),
        frames_from_callback_(FrameComp()) {}

  uint16_t Rand() { return rand_.Rand<uint16_t>(); }

  void OnCompleteFrames(RtpFrameReferenceFinder::ReturnVector frames) {
    for (auto& frame : frames) {
      int64_t pid = frame->Id();
      uint16_t sidx = frame->SpatialIndex().value_or(0);
      auto frame_it = frames_from_callback_.find(std::make_pair(pid, sidx));
      if (frame_it != frames_from_callback_.end()) {
        ADD_FAILURE() << "Already received frame with (pid:sidx): (" << pid
                      << ":" << sidx << ")";
        return;
      }

      frames_from_callback_.insert(
          std::make_pair(std::make_pair(pid, sidx), std::move(frame)));
    }
  }

  void InsertGeneric(uint16_t seq_num_start,
                     uint16_t seq_num_end,
                     bool keyframe) {
    std::unique_ptr<RtpFrameObject> frame =
        CreateFrame(seq_num_start, seq_num_end, keyframe, kVideoCodecGeneric,
                    RTPVideoTypeHeader(), /*rtp_timestamp=*/0);

    OnCompleteFrames(reference_finder_->ManageFrame(std::move(frame)));
  }

  void InsertGenericWithTimestamp(uint16_t seq_num_start,
                                  uint16_t seq_num_end,
                                  bool keyframe,
                                  uint32_t rtp_timestamp) {
    OnCompleteFrames(reference_finder_->ManageFrame(
        CreateFrame(seq_num_start, seq_num_end, keyframe, kVideoCodecGeneric,
                    RTPVideoTypeHeader(), rtp_timestamp)));
  }

  void InsertH264(uint16_t seq_num_start, uint16_t seq_num_end, bool keyframe) {
    std::unique_ptr<RtpFrameObject> frame =
        CreateFrame(seq_num_start, seq_num_end, keyframe, kVideoCodecH264,
                    RTPVideoTypeHeader(), /*rtp_timestamp=*/0);
    OnCompleteFrames(reference_finder_->ManageFrame(std::move(frame)));
  }

  void InsertPadding(uint16_t seq_num) {
    OnCompleteFrames(reference_finder_->PaddingReceived(seq_num));
  }

  // Check if a frame with picture id `pid` and spatial index `sidx` has been
  // delivered from the packet buffer, and if so, if it has the references
  // specified by `refs`.
  template <typename... T>
  void CheckReferences(int64_t picture_id_offset,
                       uint16_t sidx,
                       T... refs) const {
    int64_t pid = picture_id_offset;
    auto frame_it = frames_from_callback_.find(std::make_pair(pid, sidx));
    if (frame_it == frames_from_callback_.end()) {
      ADD_FAILURE() << "Could not find frame with (pid:sidx): (" << pid << ":"
                    << sidx << ")";
      return;
    }

    std::set<int64_t> actual_refs;
    for (uint8_t r = 0; r < frame_it->second->num_references; ++r)
      actual_refs.insert(frame_it->second->references[r]);

    std::set<int64_t> expected_refs;
    RefsToSet(&expected_refs, refs...);

    ASSERT_EQ(expected_refs, actual_refs);
  }

  template <typename... T>
  void CheckReferencesGeneric(int64_t pid, T... refs) const {
    CheckReferences(pid, 0, refs...);
  }

  template <typename... T>
  void CheckReferencesH264(int64_t pid, T... refs) const {
    CheckReferences(pid, 0, refs...);
  }

  template <typename... T>
  void RefsToSet(std::set<int64_t>* m, int64_t ref, T... refs) const {
    m->insert(ref);
    RefsToSet(m, refs...);
  }

  void RefsToSet(std::set<int64_t>* /* m */) const {}

  Random rand_;
  std::unique_ptr<RtpFrameReferenceFinder> reference_finder_;
  struct FrameComp {
    bool operator()(const std::pair<int64_t, uint8_t> f1,
                    const std::pair<int64_t, uint8_t> f2) const {
      if (f1.first == f2.first)
        return f1.second < f2.second;
      return f1.first < f2.first;
    }
  };
  std::
      map<std::pair<int64_t, uint8_t>, std::unique_ptr<EncodedFrame>, FrameComp>
          frames_from_callback_;
};

TEST_F(TestRtpFrameReferenceFinder, PaddingPackets) {
  uint16_t sn = Rand();

  InsertGeneric(sn, sn, true);
  InsertGeneric(sn + 2, sn + 2, false);
  EXPECT_EQ(1UL, frames_from_callback_.size());
  InsertPadding(sn + 1);
  EXPECT_EQ(2UL, frames_from_callback_.size());
}

TEST_F(TestRtpFrameReferenceFinder, PaddingPacketsReordered) {
  uint16_t sn = Rand();

  InsertGeneric(sn, sn, true);
  InsertPadding(sn + 1);
  InsertPadding(sn + 4);
  InsertGeneric(sn + 2, sn + 3, false);

  EXPECT_EQ(2UL, frames_from_callback_.size());
  CheckReferencesGeneric(sn);
  CheckReferencesGeneric(sn + 3, sn + 0);
}

TEST_F(TestRtpFrameReferenceFinder, PaddingPacketsReorderedMultipleKeyframes) {
  uint16_t sn = Rand();

  InsertGeneric(sn, sn, true);
  InsertPadding(sn + 1);
  InsertPadding(sn + 4);
  InsertGeneric(sn + 2, sn + 3, false);
  InsertGeneric(sn + 5, sn + 5, true);
  InsertPadding(sn + 6);
  InsertPadding(sn + 9);
  InsertGeneric(sn + 7, sn + 8, false);

  EXPECT_EQ(4UL, frames_from_callback_.size());
}

TEST_F(TestRtpFrameReferenceFinder, AdvanceSavedKeyframe) {
  uint16_t sn = Rand();

  InsertGeneric(sn, sn, true);
  InsertGeneric(sn + 1, sn + 1, true);
  InsertGeneric(sn + 2, sn + 10000, false);
  InsertGeneric(sn + 10001, sn + 20000, false);
  InsertGeneric(sn + 20001, sn + 30000, false);
  InsertGeneric(sn + 30001, sn + 40000, false);

  EXPECT_EQ(6UL, frames_from_callback_.size());
}

TEST_F(TestRtpFrameReferenceFinder, ClearTo) {
  uint16_t sn = Rand();

  InsertGeneric(sn, sn + 1, true);
  InsertGeneric(sn + 4, sn + 5, false);  // stashed
  EXPECT_EQ(1UL, frames_from_callback_.size());

  InsertGeneric(sn + 6, sn + 7, true);  // keyframe
  EXPECT_EQ(2UL, frames_from_callback_.size());
  reference_finder_->ClearTo(sn + 7);

  InsertGeneric(sn + 8, sn + 9, false);  // first frame after keyframe.
  EXPECT_EQ(3UL, frames_from_callback_.size());

  InsertGeneric(sn + 2, sn + 3, false);  // late, cleared past this frame.
  EXPECT_EQ(3UL, frames_from_callback_.size());
}

// Regression: timestamp hybrid prevents dropping wrapped-new frames (site 1).
TEST_F(TestRtpFrameReferenceFinder, FrameNotDroppedWhenSeqWrapsButTsNewer) {
  reference_finder_->ClearTo(/*seq_num=*/50000, /*rtp_timestamp=*/1000000);
  // Genuinely old: seq behind AND timestamp older -> dropped.
  InsertGenericWithTimestamp(/*seq_num_start=*/49900, /*seq_num_end=*/49901,
                             /*keyframe=*/true, /*rtp_timestamp=*/910000);
  EXPECT_THAT(frames_from_callback_, IsEmpty());
  // Wrapped-new: seq appears behind but timestamp newer -> delivered.
  InsertGenericWithTimestamp(/*seq_num_start=*/24464, /*seq_num_end=*/24465,
                             /*keyframe=*/true, /*rtp_timestamp=*/1090000);
  EXPECT_THAT(frames_from_callback_, SizeIs(1));
}

// Backwards-compat: seq-only ClearTo still drops.
TEST_F(TestRtpFrameReferenceFinder, ClearToSeqNumOnlyWithoutTimestamp) {
  reference_finder_->ClearTo(/*seq_num=*/50000);
  InsertGeneric(/*seq_num_start=*/49900, /*seq_num_end=*/49901,
                /*keyframe=*/true);
  EXPECT_THAT(frames_from_callback_, IsEmpty());
}

// Regression: stale GoP removal prevents delta drops after seq jump (site 2).
TEST_F(TestRtpFrameReferenceFinder, DeltasNotDroppedByStaleGopAfterSeqNumJump) {
  InsertGeneric(/*seq_num_start=*/60'000, /*seq_num_end=*/60'000,
                /*keyframe=*/true);
  EXPECT_THAT(frames_from_callback_, SizeIs(1));
  uint16_t seq_num = 34'464;
  InsertGeneric(/*seq_num_start=*/seq_num, /*seq_num_end=*/seq_num,
                /*keyframe=*/true);
  for (uint16_t i = 1; i <= 140; ++i) {
    ++seq_num;
    InsertGeneric(/*seq_num_start=*/seq_num, /*seq_num_end=*/seq_num,
                  /*keyframe=*/false);
  }
  EXPECT_THAT(frames_from_callback_, SizeIs(142));
}

// Hardening: the stale-GoP removal (site 2) must only erase GoPs that are
// *far* ahead. A legitimately reordered near-future keyframe is at most a few
// hundred sequence numbers ahead and must be preserved so its deltas are not
// dropped. issues.webrtc.org/516639936.
TEST_F(TestRtpFrameReferenceFinder, NearFutureReorderedGopNotErased) {
  // Keyframe G arrives slightly ahead.
  InsertGeneric(/*seq_num_start=*/2000, /*seq_num_end=*/2000,
                /*keyframe=*/true);
  // A reordered earlier keyframe K arrives just behind G. With the distance
  // threshold, processing K must NOT erase the near-future GoP G (only 200
  // sequence numbers ahead, well within kMaxGopReorderingDistance).
  InsertGeneric(/*seq_num_start=*/1800, /*seq_num_end=*/1800,
                /*keyframe=*/true);
  // Deltas belonging to G must still be delivered (G was not erased).
  InsertGeneric(/*seq_num_start=*/2001, /*seq_num_end=*/2001,
                /*keyframe=*/false);
  InsertGeneric(/*seq_num_start=*/2002, /*seq_num_end=*/2002,
                /*keyframe=*/false);
  EXPECT_THAT(frames_from_callback_, SizeIs(4));
}

TEST_F(TestRtpFrameReferenceFinder, H264KeyFrameReferences) {
  uint16_t sn = Rand();
  InsertH264(sn, sn, true);

  ASSERT_EQ(1UL, frames_from_callback_.size());
  CheckReferencesH264(sn);
}

TEST_F(TestRtpFrameReferenceFinder, H264SequenceNumberWrap) {
  uint16_t sn = 0xFFFF;

  InsertH264(sn - 1, sn - 1, true);
  InsertH264(sn, sn, false);
  InsertH264(sn + 1, sn + 1, false);
  InsertH264(sn + 2, sn + 2, false);

  ASSERT_EQ(4UL, frames_from_callback_.size());
  CheckReferencesH264(sn - 1);
  CheckReferencesH264(sn, sn - 1);
  CheckReferencesH264(sn + 1, sn);
  CheckReferencesH264(sn + 2, sn + 1);
}

TEST_F(TestRtpFrameReferenceFinder, H264Frames) {
  uint16_t sn = Rand();

  InsertH264(sn, sn, true);
  InsertH264(sn + 1, sn + 1, false);
  InsertH264(sn + 2, sn + 2, false);
  InsertH264(sn + 3, sn + 3, false);

  ASSERT_EQ(4UL, frames_from_callback_.size());
  CheckReferencesH264(sn);
  CheckReferencesH264(sn + 1, sn);
  CheckReferencesH264(sn + 2, sn + 1);
  CheckReferencesH264(sn + 3, sn + 2);
}

TEST_F(TestRtpFrameReferenceFinder, H264Reordering) {
  uint16_t sn = Rand();

  InsertH264(sn, sn, true);
  InsertH264(sn + 1, sn + 1, false);
  InsertH264(sn + 3, sn + 3, false);
  InsertH264(sn + 2, sn + 2, false);
  InsertH264(sn + 5, sn + 5, false);
  InsertH264(sn + 6, sn + 6, false);
  InsertH264(sn + 4, sn + 4, false);

  ASSERT_EQ(7UL, frames_from_callback_.size());
  CheckReferencesH264(sn);
  CheckReferencesH264(sn + 1, sn);
  CheckReferencesH264(sn + 2, sn + 1);
  CheckReferencesH264(sn + 3, sn + 2);
  CheckReferencesH264(sn + 4, sn + 3);
  CheckReferencesH264(sn + 5, sn + 4);
  CheckReferencesH264(sn + 6, sn + 5);
}

TEST_F(TestRtpFrameReferenceFinder, H264SequenceNumberWrapMulti) {
  uint16_t sn = 0xFFFF;

  InsertH264(sn - 3, sn - 2, true);
  InsertH264(sn - 1, sn + 1, false);
  InsertH264(sn + 2, sn + 3, false);
  InsertH264(sn + 4, sn + 7, false);

  ASSERT_EQ(4UL, frames_from_callback_.size());
  CheckReferencesH264(sn - 2);
  CheckReferencesH264(sn + 1, sn - 2);
  CheckReferencesH264(sn + 3, sn + 1);
  CheckReferencesH264(sn + 7, sn + 3);
}

TEST_F(TestRtpFrameReferenceFinder, Av1FrameNoDependencyDescriptor) {
  uint16_t sn = 0xFFFF;
  std::unique_ptr<RtpFrameObject> frame =
      CreateFrame(/*seq_num_start=*/sn, /*seq_num_end=*/sn, /*keyframe=*/true,
                  kVideoCodecAV1, RTPVideoTypeHeader(), /*rtp_timestamp=*/0);

  OnCompleteFrames(reference_finder_->ManageFrame(std::move(frame)));

  ASSERT_THAT(frames_from_callback_, SizeIs(1));
  EXPECT_EQ(frames_from_callback_.begin()->second->SpatialIndex(),
            std::nullopt);
  CheckReferencesGeneric(sn);
}

TEST_F(TestRtpFrameReferenceFinder, Av1SvcFramesNoDependencyDescriptor) {
  uint16_t sn = 100;

  // Keyframe spatial layer 0 (packets 100..101).
  std::unique_ptr<RtpFrameObject> s0_key = CreateFrame(
      /*seq_num_start=*/sn, /*seq_num_end=*/sn + 1, /*keyframe=*/true,
      kVideoCodecAV1, RTPVideoTypeHeader(), /*rtp_timestamp=*/1000);
  // Keyframe spatial layer 1 (packets 102..103, marked as delta by AV1
  // since it's using inter-layer prediction).
  std::unique_ptr<RtpFrameObject> s1_key = CreateFrame(
      /*seq_num_start=*/sn + 2, /*seq_num_end=*/sn + 3, /*keyframe=*/false,
      kVideoCodecAV1, RTPVideoTypeHeader(), /*rtp_timestamp=*/1000);

  // Reorder arrival time of spatial layers for second temporal unit:
  // Delta frame spatial layer 1 arrives before spatial layer 0 (gap at 104).
  std::unique_ptr<RtpFrameObject> s1_delta = CreateFrame(
      /*seq_num_start=*/sn + 5, /*seq_num_end=*/sn + 5, /*keyframe=*/false,
      kVideoCodecAV1, RTPVideoTypeHeader(), /*rtp_timestamp=*/2000);
  // Missing delta frame spatial layer 0 (packet 104).
  std::unique_ptr<RtpFrameObject> s0_delta = CreateFrame(
      /*seq_num_start=*/sn + 4, /*seq_num_end=*/sn + 4, /*keyframe=*/false,
      kVideoCodecAV1, RTPVideoTypeHeader(), /*rtp_timestamp=*/2000);

  OnCompleteFrames(reference_finder_->ManageFrame(std::move(s0_key)));
  OnCompleteFrames(reference_finder_->ManageFrame(std::move(s1_key)));
  ASSERT_THAT(frames_from_callback_, SizeIs(2));

  // `s1_delta` has a packet gap (`sn + 4` is missing), so it must be stashed.
  OnCompleteFrames(reference_finder_->ManageFrame(std::move(s1_delta)));
  ASSERT_THAT(frames_from_callback_, SizeIs(2));

  // Once `s0_delta` arrives, both `s0_delta` and `s1_delta` complete.
  OnCompleteFrames(reference_finder_->ManageFrame(std::move(s0_delta)));
  ASSERT_THAT(frames_from_callback_, SizeIs(4));
  for (const auto& [key, frame] : frames_from_callback_) {
    EXPECT_EQ(frame->SpatialIndex(), std::nullopt);
  }
  CheckReferencesGeneric(sn + 1);
  CheckReferencesGeneric(sn + 3, sn + 1);
  CheckReferencesGeneric(sn + 4, sn + 3);
  CheckReferencesGeneric(sn + 5, sn + 4);
}

TEST_F(TestRtpFrameReferenceFinder, Av1SvcFramesSpatialIdKillSwitch) {
  FieldTrials field_trials = CreateTestFieldTrials(
      "WebRTC-Video-SeqNumOnlyRefFinderSpatialIndexKillSwitch/Enabled/");
  reference_finder_ = std::make_unique<RtpFrameReferenceFinder>(&field_trials);
  uint16_t sn = 100;

  std::unique_ptr<RtpFrameObject> s0_key = CreateFrame(
      /*seq_num_start=*/sn, /*seq_num_end=*/sn + 1, /*keyframe=*/true,
      kVideoCodecAV1, RTPVideoTypeHeader(), /*rtp_timestamp=*/1000);
  std::unique_ptr<RtpFrameObject> s1_key = CreateFrame(
      /*seq_num_start=*/sn + 2, /*seq_num_end=*/sn + 3, /*keyframe=*/false,
      kVideoCodecAV1, RTPVideoTypeHeader(), /*rtp_timestamp=*/1000);

  OnCompleteFrames(reference_finder_->ManageFrame(std::move(s0_key)));
  OnCompleteFrames(reference_finder_->ManageFrame(std::move(s1_key)));

  ASSERT_THAT(frames_from_callback_, SizeIs(2));
  for (const auto& [key, frame] : frames_from_callback_) {
    EXPECT_EQ(frame->SpatialIndex(), 0);
  }
}

}  // namespace webrtc
