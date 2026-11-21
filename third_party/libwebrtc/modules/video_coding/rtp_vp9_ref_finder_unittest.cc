/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_coding/rtp_vp9_ref_finder.h"

#include <utility>
#include <vector>

#include "modules/rtp_rtcp/source/frame_object.h"
#include "test/gmock.h"
#include "test/gtest.h"

using ::testing::Contains;
using ::testing::Matcher;
using ::testing::MatcherInterface;
using ::testing::Matches;
using ::testing::MatchResultListener;
using ::testing::Pointee;
using ::testing::Property;
using ::testing::SizeIs;
using ::testing::UnorderedElementsAreArray;

namespace webrtc {

namespace {
class Frame {
 public:
  Frame& SeqNum(uint16_t start, uint16_t end) {
    seq_num_start = start;
    seq_num_end = end;
    return *this;
  }

  Frame& AsKeyFrame(bool is_keyframe = true) {
    keyframe = is_keyframe;
    return *this;
  }

  Frame& Pid(int pid) {
    picture_id = pid;
    return *this;
  }

  Frame& SidAndTid(int sid, int tid) {
    spatial_id = sid;
    temporal_id = tid;
    return *this;
  }

  Frame& Tl0(int tl0) {
    tl0_idx = tl0;
    return *this;
  }

  Frame& AsUpswitch(bool is_up = true) {
    up_switch = is_up;
    return *this;
  }

  Frame& AsInterLayer(bool is_inter_layer = true) {
    inter_layer = is_inter_layer;
    return *this;
  }

  Frame& NotAsInterPic(bool is_inter_pic = false) {
    inter_pic = is_inter_pic;
    return *this;
  }

  Frame& Gof(GofInfoVP9* ss) {
    scalability_structure = ss;
    return *this;
  }

  Frame& FlexRefs(const std::vector<uint8_t>& refs) {
    flex_refs = refs;
    return *this;
  }

  operator std::unique_ptr<RtpFrameObject>() {
    RTPVideoHeaderVP9 vp9_header{};
    vp9_header.picture_id = *picture_id;
    vp9_header.temporal_idx = *temporal_id;
    vp9_header.spatial_idx = *spatial_id;
    if (tl0_idx.has_value()) {
      RTC_DCHECK(flex_refs.empty());
      vp9_header.flexible_mode = false;
      vp9_header.tl0_pic_idx = *tl0_idx;
    } else {
      vp9_header.flexible_mode = true;
      vp9_header.num_ref_pics = flex_refs.size();
      for (size_t i = 0; i < flex_refs.size(); ++i) {
        vp9_header.pid_diff[i] = flex_refs.at(i);
      }
    }
    vp9_header.temporal_up_switch = up_switch;
    vp9_header.inter_layer_predicted = inter_layer;
    vp9_header.inter_pic_predicted = inter_pic && !keyframe;
    if (scalability_structure != nullptr) {
      vp9_header.ss_data_available = true;
      vp9_header.gof = *scalability_structure;
    }

    RTPVideoHeader video_header;
    video_header.frame_type = keyframe ? VideoFrameType::kVideoFrameKey
                                       : VideoFrameType::kVideoFrameDelta;
    video_header.video_type_header = vp9_header;
    // clang-format off
    return std::make_unique<RtpFrameObject>(
        seq_num_start,
        seq_num_end,
        /*markerBit=*/true,
        /*times_nacked=*/0,
        /*first_packet_received_time=*/0,
        /*last_packet_received_time=*/0,
        /*rtp_timestamp=*/0,
        /*ntp_time_ms=*/0,
        VideoSendTiming(),
        /*payload_type=*/0,
        kVideoCodecVP9,
        kVideoRotation_0,
        VideoContentType::UNSPECIFIED,
        video_header,
        /*color_space=*/absl::nullopt,
        RtpPacketInfos(),
        EncodedImageBuffer::Create(/*size=*/0));
    // clang-format on
  }

 private:
  uint16_t seq_num_start = 0;
  uint16_t seq_num_end = 0;
  bool keyframe = false;
  absl::optional<int> picture_id;
  absl::optional<int> spatial_id;
  absl::optional<int> temporal_id;
  absl::optional<int> tl0_idx;
  bool up_switch = false;
  bool inter_layer = false;
  bool inter_pic = true;
  GofInfoVP9* scalability_structure = nullptr;
  std::vector<uint8_t> flex_refs;
};

using FrameVector = std::vector<std::unique_ptr<EncodedFrame>>;

// Would have been nice to use the MATCHER_P3 macro instead, but when used it
// fails to infer the type of the vector if not explicitly given in the
class HasFrameMatcher : public MatcherInterface<const FrameVector&> {
 public:
  explicit HasFrameMatcher(int64_t frame_id,
                           const std::vector<int64_t>& expected_refs)
      : frame_id_(frame_id), expected_refs_(expected_refs) {}

  bool MatchAndExplain(const FrameVector& frames,
                       MatchResultListener* result_listener) const override {
    auto it = std::find_if(frames.begin(), frames.end(),
                           [this](const std::unique_ptr<EncodedFrame>& f) {
                             return f->Id() == frame_id_;
                           });
    if (it == frames.end()) {
      if (result_listener->IsInterested()) {
        *result_listener << "No frame with frame_id:" << frame_id_;
      }
      return false;
    }

    rtc::ArrayView<int64_t> actual_refs((*it)->references,
                                        (*it)->num_references);
    if (!Matches(UnorderedElementsAreArray(expected_refs_))(actual_refs)) {
      if (result_listener->IsInterested()) {
        *result_listener << "Frame with frame_id:" << frame_id_ << " and "
                         << actual_refs.size() << " references { ";
        for (auto r : actual_refs) {
          *result_listener << r << " ";
        }
        *result_listener << "}";
      }
      return false;
    }

    return true;
  }

  void DescribeTo(std::ostream* os) const override {
    *os << "frame with frame_id:" << frame_id_ << " and "
        << expected_refs_.size() << " references { ";
    for (auto r : expected_refs_) {
      *os << r << " ";
    }
    *os << "}";
  }

 private:
  const int64_t frame_id_;
  const std::vector<int64_t> expected_refs_;
};

}  // namespace

class RtpVp9RefFinderTest : public ::testing::Test {
 protected:
  RtpVp9RefFinderTest() : ref_finder_(std::make_unique<RtpVp9RefFinder>()) {}

  void Insert(std::unique_ptr<RtpFrameObject> frame) {
    for (auto& f : ref_finder_->ManageFrame(std::move(frame))) {
      frames_.push_back(std::move(f));
    }
  }

  std::unique_ptr<RtpVp9RefFinder> ref_finder_;
  FrameVector frames_;
};

Matcher<const FrameVector&> HasFrameWithIdAndRefs(int64_t frame_id,
                                                  std::vector<int64_t> refs) {
  return MakeMatcher(new HasFrameMatcher(frame_id, refs));
}

TEST_F(RtpVp9RefFinderTest, GofInsertOneFrame) {
  GofInfoVP9 ss;
  ss.SetGofInfoVP9(kTemporalStructureMode1);

  Insert(Frame().Pid(1).SidAndTid(0, 0).Tl0(0).AsKeyFrame().Gof(&ss));

  EXPECT_EQ(frames_.size(), 1UL);
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(5, {}));
}

TEST_F(RtpVp9RefFinderTest, GofTemporalLayers_0) {
  GofInfoVP9 ss;
  ss.SetGofInfoVP9(kTemporalStructureMode1);  // Only 1 spatial layer.

  Insert(Frame().Pid(1).SidAndTid(0, 0).Tl0(0).AsKeyFrame().Gof(&ss));
  Insert(Frame().Pid(2).SidAndTid(0, 0).Tl0(1));

  EXPECT_EQ(frames_.size(), 2UL);
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(5, {}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(10, {5}));
}

TEST_F(RtpVp9RefFinderTest, GofSpatialLayers_2) {
  GofInfoVP9 ss;
  ss.SetGofInfoVP9(kTemporalStructureMode1);  // Only 1 spatial layer.

  Insert(Frame().Pid(1).SidAndTid(0, 0).Tl0(0).AsKeyFrame().Gof(&ss));
  Insert(Frame().Pid(2).SidAndTid(0, 0).Tl0(1));
  Insert(Frame().Pid(2).SidAndTid(1, 0).Tl0(1).NotAsInterPic());
  Insert(Frame().Pid(3).SidAndTid(0, 0).Tl0(2));
  Insert(Frame().Pid(3).SidAndTid(1, 0).Tl0(2));

  EXPECT_EQ(frames_.size(), 5UL);
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(5, {}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(10, {5}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(11, {}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(15, {10}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(16, {11}));
}

TEST_F(RtpVp9RefFinderTest, GofTemporalLayersReordered_0) {
  GofInfoVP9 ss;
  ss.SetGofInfoVP9(kTemporalStructureMode1);  // Only 1 spatial layer.

  Insert(Frame().Pid(2).SidAndTid(0, 0).Tl0(1));
  Insert(Frame().Pid(2).SidAndTid(1, 0).Tl0(1).NotAsInterPic());
  Insert(Frame().Pid(1).SidAndTid(0, 0).Tl0(0).AsKeyFrame().Gof(&ss));
  Insert(Frame().Pid(3).SidAndTid(0, 0).Tl0(2));
  Insert(Frame().Pid(3).SidAndTid(1, 0).Tl0(2));
  Insert(Frame().Pid(4).SidAndTid(0, 0).Tl0(3));
  Insert(Frame().Pid(5).SidAndTid(1, 0).Tl0(4));
  Insert(Frame().Pid(4).SidAndTid(1, 0).Tl0(3));
  Insert(Frame().Pid(5).SidAndTid(0, 0).Tl0(4));

  EXPECT_EQ(frames_.size(), 9UL);
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(5, {}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(10, {5}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(11, {}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(15, {10}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(16, {11}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(20, {15}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(21, {16}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(25, {20}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(26, {21}));
}

TEST_F(RtpVp9RefFinderTest, GofSkipFramesTemporalLayers_01) {
  GofInfoVP9 ss;
  ss.SetGofInfoVP9(kTemporalStructureMode2);  // 0101 pattern

  Insert(Frame().Pid(0).SidAndTid(0, 0).Tl0(0).AsKeyFrame().NotAsInterPic().Gof(
      &ss));
  Insert(Frame().Pid(1).SidAndTid(0, 1).Tl0(0));
  // Skip GOF with tl0 1
  Insert(Frame().Pid(4).SidAndTid(0, 0).Tl0(2).AsKeyFrame().Gof(&ss));
  Insert(Frame().Pid(5).SidAndTid(0, 1).Tl0(2));
  // Skip GOF with tl0 3
  // Skip GOF with tl0 4
  Insert(Frame().Pid(10).SidAndTid(0, 0).Tl0(5).Gof(&ss));
  Insert(Frame().Pid(11).SidAndTid(0, 1).Tl0(5));

  ASSERT_EQ(6UL, frames_.size());
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(0, {}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(5, {0}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(20, {}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(25, {20}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(50, {40}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(55, {50}));
}

TEST_F(RtpVp9RefFinderTest, GofSkipFramesTemporalLayers_0212) {
  GofInfoVP9 ss;
  ss.SetGofInfoVP9(kTemporalStructureMode3);  // 02120212 pattern

  Insert(Frame().Pid(0).SidAndTid(0, 0).Tl0(0).AsKeyFrame().NotAsInterPic().Gof(
      &ss));
  Insert(Frame().Pid(1).SidAndTid(0, 2).Tl0(0));
  Insert(Frame().Pid(2).SidAndTid(0, 1).Tl0(0));
  Insert(Frame().Pid(3).SidAndTid(0, 2).Tl0(0));

  ASSERT_EQ(4UL, frames_.size());
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(0, {}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(5, {0}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(10, {0}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(15, {10}));

  // Skip frames with tl0 = 1

  Insert(Frame().Pid(8).SidAndTid(0, 0).Tl0(2).AsKeyFrame().NotAsInterPic().Gof(
      &ss));
  Insert(Frame().Pid(9).SidAndTid(0, 2).Tl0(2));
  Insert(Frame().Pid(10).SidAndTid(0, 1).Tl0(2));
  Insert(Frame().Pid(11).SidAndTid(0, 2).Tl0(2));

  ASSERT_EQ(8UL, frames_.size());
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(40, {}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(45, {40}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(50, {40}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(55, {50}));

  // Now insert frames with tl0 = 1
  Insert(Frame().Pid(4).SidAndTid(0, 0).Tl0(1).AsKeyFrame().Gof(&ss));
  Insert(Frame().Pid(7).SidAndTid(0, 2).Tl0(1));

  ASSERT_EQ(9UL, frames_.size());
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(20, {}));

  Insert(Frame().Pid(5).SidAndTid(0, 2).Tl0(1));
  Insert(Frame().Pid(6).SidAndTid(0, 1).Tl0(1));

  ASSERT_EQ(12UL, frames_.size());
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(25, {20}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(30, {20}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(35, {30}));
}

TEST_F(RtpVp9RefFinderTest, GofInterLayerPredS0KeyS1Delta) {
  GofInfoVP9 ss;
  ss.SetGofInfoVP9(kTemporalStructureMode1);

  Insert(Frame().Pid(1).SidAndTid(0, 0).Tl0(0).AsKeyFrame().Gof(&ss));
  Insert(Frame().Pid(1).SidAndTid(1, 0).Tl0(0).AsInterLayer().NotAsInterPic());

  ASSERT_EQ(2UL, frames_.size());
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(5, {}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(6, {5}));
}

TEST_F(RtpVp9RefFinderTest, GofTemporalLayers_01) {
  GofInfoVP9 ss;
  ss.SetGofInfoVP9(kTemporalStructureMode2);  // 0101 pattern

  Insert(Frame().Pid(0).SidAndTid(0, 0).Tl0(0).AsKeyFrame().NotAsInterPic().Gof(
      &ss));
  Insert(Frame().Pid(1).SidAndTid(0, 1).Tl0(0));
  Insert(Frame().Pid(2).SidAndTid(0, 0).Tl0(1));
  Insert(Frame().Pid(3).SidAndTid(0, 1).Tl0(1));

  ASSERT_EQ(4UL, frames_.size());
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(0, {}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(5, {0}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(10, {0}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(15, {10}));
}

TEST_F(RtpVp9RefFinderTest, GofTemporalLayersReordered_01) {
  GofInfoVP9 ss;
  ss.SetGofInfoVP9(kTemporalStructureMode2);  // 01 pattern

  Insert(Frame().Pid(1).SidAndTid(0, 1).Tl0(0));
  Insert(Frame().Pid(0).SidAndTid(0, 0).Tl0(0).AsKeyFrame().NotAsInterPic().Gof(
      &ss));
  Insert(Frame().Pid(2).SidAndTid(0, 0).Tl0(1));
  Insert(Frame().Pid(4).SidAndTid(0, 0).Tl0(2));
  Insert(Frame().Pid(3).SidAndTid(0, 1).Tl0(1));
  Insert(Frame().Pid(5).SidAndTid(0, 1).Tl0(2));
  Insert(Frame().Pid(7).SidAndTid(0, 1).Tl0(3));
  Insert(Frame().Pid(6).SidAndTid(0, 0).Tl0(3));
  Insert(Frame().Pid(8).SidAndTid(0, 0).Tl0(4));
  Insert(Frame().Pid(9).SidAndTid(0, 1).Tl0(4));

  ASSERT_EQ(10UL, frames_.size());
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(0, {}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(5, {0}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(10, {0}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(15, {10}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(20, {10}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(25, {20}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(30, {20}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(35, {30}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(40, {30}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(45, {40}));
}

TEST_F(RtpVp9RefFinderTest, GofTemporalLayers_0212) {
  GofInfoVP9 ss;
  ss.SetGofInfoVP9(kTemporalStructureMode3);  // 0212 pattern

  Insert(Frame().Pid(0).SidAndTid(0, 0).Tl0(0).AsKeyFrame().NotAsInterPic().Gof(
      &ss));
  Insert(Frame().Pid(1).SidAndTid(0, 2).Tl0(0));
  Insert(Frame().Pid(2).SidAndTid(0, 1).Tl0(0));
  Insert(Frame().Pid(3).SidAndTid(0, 2).Tl0(0));
  Insert(Frame().Pid(4).SidAndTid(0, 0).Tl0(1));
  Insert(Frame().Pid(5).SidAndTid(0, 2).Tl0(1));
  Insert(Frame().Pid(6).SidAndTid(0, 1).Tl0(1));
  Insert(Frame().Pid(7).SidAndTid(0, 2).Tl0(1));

  ASSERT_EQ(8UL, frames_.size());
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(0, {}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(5, {0}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(10, {0}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(15, {10}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(20, {0}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(25, {20}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(30, {20}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(35, {30}));
}

TEST_F(RtpVp9RefFinderTest, GofTemporalLayersReordered_0212) {
  GofInfoVP9 ss;
  ss.SetGofInfoVP9(kTemporalStructureMode3);  // 0212 pattern

  Insert(Frame().Pid(2).SidAndTid(0, 1).Tl0(0));
  Insert(Frame().Pid(1).SidAndTid(0, 2).Tl0(0));
  Insert(Frame().Pid(0).SidAndTid(0, 0).Tl0(0).AsKeyFrame().NotAsInterPic().Gof(
      &ss));
  Insert(Frame().Pid(3).SidAndTid(0, 2).Tl0(0));
  Insert(Frame().Pid(6).SidAndTid(0, 1).Tl0(1));
  Insert(Frame().Pid(5).SidAndTid(0, 2).Tl0(1));
  Insert(Frame().Pid(4).SidAndTid(0, 0).Tl0(1));
  Insert(Frame().Pid(9).SidAndTid(0, 2).Tl0(2));
  Insert(Frame().Pid(7).SidAndTid(0, 2).Tl0(1));
  Insert(Frame().Pid(8).SidAndTid(0, 0).Tl0(2));
  Insert(Frame().Pid(11).SidAndTid(0, 2).Tl0(2));
  Insert(Frame().Pid(10).SidAndTid(0, 1).Tl0(2));

  ASSERT_EQ(12UL, frames_.size());
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(0, {}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(5, {0}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(10, {0}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(15, {10}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(20, {0}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(25, {20}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(30, {20}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(35, {30}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(40, {20}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(45, {40}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(50, {40}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(55, {50}));
}

TEST_F(RtpVp9RefFinderTest, GofTemporalLayersReordered_01_0212) {
  GofInfoVP9 ss;
  ss.SetGofInfoVP9(kTemporalStructureMode2);  // 01 pattern

  Insert(Frame().Pid(1).SidAndTid(0, 1).Tl0(0));
  Insert(Frame().Pid(0).SidAndTid(0, 0).Tl0(0).AsKeyFrame().NotAsInterPic().Gof(
      &ss));
  Insert(Frame().Pid(3).SidAndTid(0, 1).Tl0(1));
  Insert(Frame().Pid(6).SidAndTid(0, 1).Tl0(2));
  ss.SetGofInfoVP9(kTemporalStructureMode3);  // 0212 pattern
  Insert(Frame().Pid(4).SidAndTid(0, 0).Tl0(2).Gof(&ss));
  Insert(Frame().Pid(2).SidAndTid(0, 0).Tl0(1));
  Insert(Frame().Pid(5).SidAndTid(0, 2).Tl0(2));
  Insert(Frame().Pid(8).SidAndTid(0, 0).Tl0(3));
  Insert(Frame().Pid(10).SidAndTid(0, 1).Tl0(3));
  Insert(Frame().Pid(7).SidAndTid(0, 2).Tl0(2));
  Insert(Frame().Pid(11).SidAndTid(0, 2).Tl0(3));
  Insert(Frame().Pid(9).SidAndTid(0, 2).Tl0(3));

  ASSERT_EQ(12UL, frames_.size());
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(0, {}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(5, {0}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(10, {0}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(15, {10}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(20, {0}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(25, {20}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(30, {20}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(35, {30}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(40, {20}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(45, {40}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(50, {40}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(55, {50}));
}

TEST_F(RtpVp9RefFinderTest, FlexibleModeOneFrame) {
  Insert(Frame().Pid(0).SidAndTid(0, 0).AsKeyFrame());

  ASSERT_EQ(1UL, frames_.size());
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(0, {}));
}

TEST_F(RtpVp9RefFinderTest, FlexibleModeTwoSpatialLayers) {
  Insert(Frame().Pid(0).SidAndTid(0, 0).AsKeyFrame());
  Insert(Frame().Pid(0).SidAndTid(1, 0).AsKeyFrame().AsInterLayer());
  Insert(Frame().Pid(1).SidAndTid(1, 0).FlexRefs({1}));
  Insert(Frame().Pid(2).SidAndTid(0, 0).FlexRefs({2}));
  Insert(Frame().Pid(2).SidAndTid(1, 0).FlexRefs({1}));
  Insert(Frame().Pid(3).SidAndTid(1, 0).FlexRefs({1}));
  Insert(Frame().Pid(4).SidAndTid(0, 0).FlexRefs({2}));
  Insert(Frame().Pid(4).SidAndTid(1, 0).FlexRefs({1}));

  ASSERT_EQ(8UL, frames_.size());
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(0, {}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(1, {0}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(6, {1}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(10, {0}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(11, {6}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(16, {11}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(20, {10}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(21, {16}));
}

TEST_F(RtpVp9RefFinderTest, FlexibleModeTwoSpatialLayersReordered) {
  Insert(Frame().Pid(0).SidAndTid(1, 0).AsKeyFrame().AsInterLayer());
  Insert(Frame().Pid(1).SidAndTid(1, 0).FlexRefs({1}));
  Insert(Frame().Pid(0).SidAndTid(0, 0).AsKeyFrame());
  Insert(Frame().Pid(2).SidAndTid(1, 0).FlexRefs({1}));
  Insert(Frame().Pid(3).SidAndTid(1, 0).FlexRefs({1}));
  Insert(Frame().Pid(2).SidAndTid(0, 0).FlexRefs({2}));
  Insert(Frame().Pid(4).SidAndTid(1, 0).FlexRefs({1}));
  Insert(Frame().Pid(4).SidAndTid(0, 0).FlexRefs({2}));

  ASSERT_EQ(8UL, frames_.size());
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(0, {}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(1, {0}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(6, {1}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(10, {0}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(11, {6}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(16, {11}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(20, {10}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(21, {16}));
}

TEST_F(RtpVp9RefFinderTest, WrappingFlexReference) {
  Insert(Frame().Pid(0).SidAndTid(0, 0).FlexRefs({1}));

  ASSERT_EQ(1UL, frames_.size());
  const EncodedFrame& frame = *frames_[0];

  ASSERT_EQ(frame.Id() - frame.references[0], 5);
}

TEST_F(RtpVp9RefFinderTest, GofPidJump) {
  GofInfoVP9 ss;
  ss.SetGofInfoVP9(kTemporalStructureMode3);

  Insert(Frame().Pid(0).SidAndTid(0, 0).Tl0(0).AsKeyFrame().NotAsInterPic().Gof(
      &ss));
  Insert(Frame().Pid(1000).SidAndTid(0, 0).Tl0(1));
}

TEST_F(RtpVp9RefFinderTest, GofTl0Jump) {
  GofInfoVP9 ss;
  ss.SetGofInfoVP9(kTemporalStructureMode3);

  Insert(Frame()
             .Pid(0)
             .SidAndTid(0, 0)
             .Tl0(125)
             .AsUpswitch()
             .AsKeyFrame()
             .NotAsInterPic()
             .Gof(&ss));
  Insert(Frame().Pid(1).SidAndTid(0, 0).Tl0(0).Gof(&ss));
}

TEST_F(RtpVp9RefFinderTest, GofTidTooHigh) {
  const int kMaxTemporalLayers = 5;
  GofInfoVP9 ss;
  ss.SetGofInfoVP9(kTemporalStructureMode2);
  ss.temporal_idx[1] = kMaxTemporalLayers;

  Insert(Frame().Pid(0).SidAndTid(0, 0).Tl0(0).AsKeyFrame().NotAsInterPic().Gof(
      &ss));
  Insert(Frame().Pid(1).SidAndTid(0, 0).Tl0(1));

  ASSERT_EQ(1UL, frames_.size());
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(0, {}));
}

TEST_F(RtpVp9RefFinderTest, GofZeroFrames) {
  GofInfoVP9 ss;
  ss.num_frames_in_gof = 0;

  Insert(Frame().Pid(0).SidAndTid(0, 0).Tl0(0).AsKeyFrame().NotAsInterPic().Gof(
      &ss));
  Insert(Frame().Pid(1).SidAndTid(0, 0).Tl0(1));

  ASSERT_EQ(2UL, frames_.size());
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(0, {}));
  EXPECT_THAT(frames_, HasFrameWithIdAndRefs(5, {0}));
}

TEST_F(RtpVp9RefFinderTest, SpatialIndex) {
  Insert(Frame().Pid(0).SidAndTid(0, 0).AsKeyFrame());
  Insert(Frame().Pid(0).SidAndTid(1, 0).AsKeyFrame());
  Insert(Frame().Pid(0).SidAndTid(2, 0).AsKeyFrame());

  ASSERT_EQ(3UL, frames_.size());
  EXPECT_THAT(frames_,
              Contains(Pointee(Property(&EncodedFrame::SpatialIndex, 0))));
  EXPECT_THAT(frames_,
              Contains(Pointee(Property(&EncodedFrame::SpatialIndex, 1))));
  EXPECT_THAT(frames_,
              Contains(Pointee(Property(&EncodedFrame::SpatialIndex, 2))));
}

TEST_F(RtpVp9RefFinderTest, StashedFramesDoNotWrapTl0Backwards) {
  GofInfoVP9 ss;
  ss.SetGofInfoVP9(kTemporalStructureMode1);

  Insert(Frame().Pid(0).SidAndTid(0, 0).Tl0(0));
  EXPECT_THAT(frames_, SizeIs(0));

  Insert(Frame().Pid(128).SidAndTid(0, 0).Tl0(128).AsKeyFrame().Gof(&ss));
  EXPECT_THAT(frames_, SizeIs(1));
  Insert(Frame().Pid(129).SidAndTid(0, 0).Tl0(129));
  EXPECT_THAT(frames_, SizeIs(2));
}

}  // namespace webrtc
