/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/rtp_rtcp/source/video_rtp_depacketizer_vp9.h"

#include <memory>
#include <vector>

#include "api/array_view.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

void VerifyHeader(const RTPVideoHeaderVP9& expected,
                  const RTPVideoHeaderVP9& actual) {
  EXPECT_EQ(expected.inter_layer_predicted, actual.inter_layer_predicted);
  EXPECT_EQ(expected.inter_pic_predicted, actual.inter_pic_predicted);
  EXPECT_EQ(expected.flexible_mode, actual.flexible_mode);
  EXPECT_EQ(expected.beginning_of_frame, actual.beginning_of_frame);
  EXPECT_EQ(expected.end_of_frame, actual.end_of_frame);
  EXPECT_EQ(expected.ss_data_available, actual.ss_data_available);
  EXPECT_EQ(expected.non_ref_for_inter_layer_pred,
            actual.non_ref_for_inter_layer_pred);
  EXPECT_EQ(expected.picture_id, actual.picture_id);
  EXPECT_EQ(expected.max_picture_id, actual.max_picture_id);
  EXPECT_EQ(expected.temporal_idx, actual.temporal_idx);
  EXPECT_EQ(expected.spatial_idx, actual.spatial_idx);
  EXPECT_EQ(expected.gof_idx, actual.gof_idx);
  EXPECT_EQ(expected.tl0_pic_idx, actual.tl0_pic_idx);
  EXPECT_EQ(expected.temporal_up_switch, actual.temporal_up_switch);

  EXPECT_EQ(expected.num_ref_pics, actual.num_ref_pics);
  for (uint8_t i = 0; i < expected.num_ref_pics; ++i) {
    EXPECT_EQ(expected.pid_diff[i], actual.pid_diff[i]);
    EXPECT_EQ(expected.ref_picture_id[i], actual.ref_picture_id[i]);
  }
  if (expected.ss_data_available) {
    EXPECT_EQ(expected.spatial_layer_resolution_present,
              actual.spatial_layer_resolution_present);
    EXPECT_EQ(expected.num_spatial_layers, actual.num_spatial_layers);
    if (expected.spatial_layer_resolution_present) {
      for (size_t i = 0; i < expected.num_spatial_layers; i++) {
        EXPECT_EQ(expected.width[i], actual.width[i]);
        EXPECT_EQ(expected.height[i], actual.height[i]);
      }
    }
    EXPECT_EQ(expected.gof.num_frames_in_gof, actual.gof.num_frames_in_gof);
    for (size_t i = 0; i < expected.gof.num_frames_in_gof; i++) {
      EXPECT_EQ(expected.gof.temporal_up_switch[i],
                actual.gof.temporal_up_switch[i]);
      EXPECT_EQ(expected.gof.temporal_idx[i], actual.gof.temporal_idx[i]);
      EXPECT_EQ(expected.gof.num_ref_pics[i], actual.gof.num_ref_pics[i]);
      for (uint8_t j = 0; j < expected.gof.num_ref_pics[i]; j++) {
        EXPECT_EQ(expected.gof.pid_diff[i][j], actual.gof.pid_diff[i][j]);
      }
    }
  }
}

TEST(VideoRtpDepacketizerVp9Test, ParseBasicHeader) {
  uint8_t packet[4] = {0};
  packet[0] = 0x0C;  // I:0 P:0 L:0 F:0 B:1 E:1 V:0 Z:0

  RTPVideoHeader video_header;
  int offset = VideoRtpDepacketizerVp9::ParseRtpPayload(packet, &video_header);

  EXPECT_EQ(offset, 1);
  RTPVideoHeaderVP9 expected;
  expected.InitRTPVideoHeaderVP9();
  expected.beginning_of_frame = true;
  expected.end_of_frame = true;
  VerifyHeader(expected,
               absl::get<RTPVideoHeaderVP9>(video_header.video_type_header));
}

TEST(VideoRtpDepacketizerVp9Test, ParseOneBytePictureId) {
  uint8_t packet[10] = {0};
  packet[0] = 0x80;  // I:1 P:0 L:0 F:0 B:0 E:0 V:0 Z:0
  packet[1] = kMaxOneBytePictureId;

  RTPVideoHeader video_header;
  int offset = VideoRtpDepacketizerVp9::ParseRtpPayload(packet, &video_header);

  EXPECT_EQ(offset, 2);
  RTPVideoHeaderVP9 expected;
  expected.InitRTPVideoHeaderVP9();
  expected.picture_id = kMaxOneBytePictureId;
  expected.max_picture_id = kMaxOneBytePictureId;
  VerifyHeader(expected,
               absl::get<RTPVideoHeaderVP9>(video_header.video_type_header));
}

TEST(VideoRtpDepacketizerVp9Test, ParseTwoBytePictureId) {
  uint8_t packet[10] = {0};
  packet[0] = 0x80;  // I:1 P:0 L:0 F:0 B:0 E:0 V:0 Z:0
  packet[1] = 0x80 | ((kMaxTwoBytePictureId >> 8) & 0x7F);
  packet[2] = kMaxTwoBytePictureId & 0xFF;

  RTPVideoHeader video_header;
  int offset = VideoRtpDepacketizerVp9::ParseRtpPayload(packet, &video_header);

  EXPECT_EQ(offset, 3);
  RTPVideoHeaderVP9 expected;
  expected.InitRTPVideoHeaderVP9();
  expected.picture_id = kMaxTwoBytePictureId;
  expected.max_picture_id = kMaxTwoBytePictureId;
  VerifyHeader(expected,
               absl::get<RTPVideoHeaderVP9>(video_header.video_type_header));
}

TEST(VideoRtpDepacketizerVp9Test, ParseLayerInfoWithNonFlexibleMode) {
  const uint8_t kTemporalIdx = 2;
  const uint8_t kUbit = 1;
  const uint8_t kSpatialIdx = 1;
  const uint8_t kDbit = 1;
  const uint8_t kTl0PicIdx = 17;
  uint8_t packet[13] = {0};
  packet[0] = 0x20;  // I:0 P:0 L:1 F:0 B:0 E:0 V:0 Z:0
  packet[1] = (kTemporalIdx << 5) | (kUbit << 4) | (kSpatialIdx << 1) | kDbit;
  packet[2] = kTl0PicIdx;

  RTPVideoHeader video_header;
  int offset = VideoRtpDepacketizerVp9::ParseRtpPayload(packet, &video_header);

  EXPECT_EQ(offset, 3);
  RTPVideoHeaderVP9 expected;
  expected.InitRTPVideoHeaderVP9();
  // T:2 U:1 S:1 D:1
  // TL0PICIDX:17
  expected.temporal_idx = kTemporalIdx;
  expected.temporal_up_switch = kUbit ? true : false;
  expected.spatial_idx = kSpatialIdx;
  expected.inter_layer_predicted = kDbit ? true : false;
  expected.tl0_pic_idx = kTl0PicIdx;
  VerifyHeader(expected,
               absl::get<RTPVideoHeaderVP9>(video_header.video_type_header));
}

TEST(VideoRtpDepacketizerVp9Test, ParseLayerInfoWithFlexibleMode) {
  const uint8_t kTemporalIdx = 2;
  const uint8_t kUbit = 1;
  const uint8_t kSpatialIdx = 0;
  const uint8_t kDbit = 0;
  uint8_t packet[13] = {0};
  packet[0] = 0x38;  // I:0 P:0 L:1 F:1 B:1 E:0 V:0 Z:0
  packet[1] = (kTemporalIdx << 5) | (kUbit << 4) | (kSpatialIdx << 1) | kDbit;

  RTPVideoHeader video_header;
  int offset = VideoRtpDepacketizerVp9::ParseRtpPayload(packet, &video_header);

  EXPECT_EQ(offset, 2);
  RTPVideoHeaderVP9 expected;
  expected.InitRTPVideoHeaderVP9();
  // I:0 P:0 L:1 F:1 B:1 E:0 V:0 Z:0
  // L:   T:2 U:1 S:0 D:0
  expected.beginning_of_frame = true;
  expected.flexible_mode = true;
  expected.temporal_idx = kTemporalIdx;
  expected.temporal_up_switch = kUbit ? true : false;
  expected.spatial_idx = kSpatialIdx;
  expected.inter_layer_predicted = kDbit ? true : false;
  VerifyHeader(expected,
               absl::get<RTPVideoHeaderVP9>(video_header.video_type_header));
}

TEST(VideoRtpDepacketizerVp9Test, ParseRefIdx) {
  const int16_t kPictureId = 17;
  const uint8_t kPdiff1 = 17;
  const uint8_t kPdiff2 = 18;
  const uint8_t kPdiff3 = 127;
  uint8_t packet[13] = {0};
  packet[0] = 0xD8;  // I:1 P:1 L:0 F:1 B:1 E:0 V:0 Z:0
  packet[1] = 0x80 | ((kPictureId >> 8) & 0x7F);  // Two byte pictureID.
  packet[2] = kPictureId;
  packet[3] = (kPdiff1 << 1) | 1;  // P_DIFF N:1
  packet[4] = (kPdiff2 << 1) | 1;  // P_DIFF N:1
  packet[5] = (kPdiff3 << 1) | 0;  // P_DIFF N:0

  RTPVideoHeader video_header;
  int offset = VideoRtpDepacketizerVp9::ParseRtpPayload(packet, &video_header);

  EXPECT_EQ(offset, 6);
  RTPVideoHeaderVP9 expected;
  expected.InitRTPVideoHeaderVP9();
  // I:1 P:1 L:0 F:1 B:1 E:0 V:0 Z:0
  // I:    PICTURE ID:17
  // I:
  // P,F:  P_DIFF:17  N:1 => refPicId = 17 - 17 = 0
  // P,F:  P_DIFF:18  N:1 => refPicId = (kMaxPictureId + 1) + 17 - 18 = 0x7FFF
  // P,F:  P_DIFF:127 N:0 => refPicId = (kMaxPictureId + 1) + 17 - 127 = 32658
  expected.beginning_of_frame = true;
  expected.inter_pic_predicted = true;
  expected.flexible_mode = true;
  expected.picture_id = kPictureId;
  expected.num_ref_pics = 3;
  expected.pid_diff[0] = kPdiff1;
  expected.pid_diff[1] = kPdiff2;
  expected.pid_diff[2] = kPdiff3;
  expected.ref_picture_id[0] = 0;
  expected.ref_picture_id[1] = 0x7FFF;
  expected.ref_picture_id[2] = 32658;
  VerifyHeader(expected,
               absl::get<RTPVideoHeaderVP9>(video_header.video_type_header));
}

TEST(VideoRtpDepacketizerVp9Test, ParseRefIdxFailsWithNoPictureId) {
  const uint8_t kPdiff = 3;
  uint8_t packet[13] = {0};
  packet[0] = 0x58;           // I:0 P:1 L:0 F:1 B:1 E:0 V:0 Z:0
  packet[1] = (kPdiff << 1);  // P,F:  P_DIFF:3 N:0

  RTPVideoHeader video_header;
  EXPECT_EQ(VideoRtpDepacketizerVp9::ParseRtpPayload(packet, &video_header), 0);
}

TEST(VideoRtpDepacketizerVp9Test, ParseRefIdxFailsWithTooManyRefPics) {
  const uint8_t kPdiff = 3;
  uint8_t packet[13] = {0};
  packet[0] = 0xD8;                  // I:1 P:1 L:0 F:1 B:1 E:0 V:0 Z:0
  packet[1] = kMaxOneBytePictureId;  // I:    PICTURE ID:127
  packet[2] = (kPdiff << 1) | 1;     // P,F:  P_DIFF:3 N:1
  packet[3] = (kPdiff << 1) | 1;     // P,F:  P_DIFF:3 N:1
  packet[4] = (kPdiff << 1) | 1;     // P,F:  P_DIFF:3 N:1
  packet[5] = (kPdiff << 1) | 0;     // P,F:  P_DIFF:3 N:0

  RTPVideoHeader video_header;
  EXPECT_EQ(VideoRtpDepacketizerVp9::ParseRtpPayload(packet, &video_header), 0);
}

TEST(VideoRtpDepacketizerVp9Test, ParseSsData) {
  const uint8_t kYbit = 0;
  const size_t kNs = 2;
  const size_t kNg = 2;
  uint8_t packet[23] = {0};
  packet[0] = 0x0A;  // I:0 P:0 L:0 F:0 B:1 E:0 V:1 Z:0
  packet[1] = ((kNs - 1) << 5) | (kYbit << 4) | (1 << 3);  // N_S Y G:1 -
  packet[2] = kNg;                                         // N_G
  packet[3] = (0 << 5) | (1 << 4) | (0 << 2) | 0;          // T:0 U:1 R:0 -
  packet[4] = (2 << 5) | (0 << 4) | (1 << 2) | 0;          // T:2 U:0 R:1 -
  packet[5] = 33;

  RTPVideoHeader video_header;
  int offset = VideoRtpDepacketizerVp9::ParseRtpPayload(packet, &video_header);

  EXPECT_EQ(offset, 6);
  RTPVideoHeaderVP9 expected;
  expected.InitRTPVideoHeaderVP9();
  expected.beginning_of_frame = true;
  expected.ss_data_available = true;
  expected.num_spatial_layers = kNs;
  expected.spatial_layer_resolution_present = kYbit ? true : false;
  expected.gof.num_frames_in_gof = kNg;
  expected.gof.temporal_idx[0] = 0;
  expected.gof.temporal_idx[1] = 2;
  expected.gof.temporal_up_switch[0] = true;
  expected.gof.temporal_up_switch[1] = false;
  expected.gof.num_ref_pics[0] = 0;
  expected.gof.num_ref_pics[1] = 1;
  expected.gof.pid_diff[1][0] = 33;
  VerifyHeader(expected,
               absl::get<RTPVideoHeaderVP9>(video_header.video_type_header));
}

TEST(VideoRtpDepacketizerVp9Test, ParseFirstPacketInKeyFrame) {
  uint8_t packet[2] = {0};
  packet[0] = 0x08;  // I:0 P:0 L:0 F:0 B:1 E:0 V:0 Z:0

  RTPVideoHeader video_header;
  VideoRtpDepacketizerVp9::ParseRtpPayload(packet, &video_header);

  EXPECT_EQ(video_header.frame_type, VideoFrameType::kVideoFrameKey);
  EXPECT_TRUE(video_header.is_first_packet_in_frame);
  EXPECT_FALSE(video_header.is_last_packet_in_frame);
}

TEST(VideoRtpDepacketizerVp9Test, ParseLastPacketInDeltaFrame) {
  uint8_t packet[2] = {0};
  packet[0] = 0x44;  // I:0 P:1 L:0 F:0 B:0 E:1 V:0 Z:0

  RTPVideoHeader video_header;
  VideoRtpDepacketizerVp9::ParseRtpPayload(packet, &video_header);

  EXPECT_EQ(video_header.frame_type, VideoFrameType::kVideoFrameDelta);
  EXPECT_FALSE(video_header.is_first_packet_in_frame);
  EXPECT_TRUE(video_header.is_last_packet_in_frame);
}

TEST(VideoRtpDepacketizerVp9Test, ParseResolution) {
  const uint16_t kWidth[2] = {640, 1280};
  const uint16_t kHeight[2] = {360, 720};
  uint8_t packet[20] = {0};
  packet[0] = 0x0A;                     // I:0 P:0 L:0 F:0 B:1 E:0 V:1 Z:0
  packet[1] = (1 << 5) | (1 << 4) | 0;  // N_S:1 Y:1 G:0
  packet[2] = kWidth[0] >> 8;
  packet[3] = kWidth[0] & 0xFF;
  packet[4] = kHeight[0] >> 8;
  packet[5] = kHeight[0] & 0xFF;
  packet[6] = kWidth[1] >> 8;
  packet[7] = kWidth[1] & 0xFF;
  packet[8] = kHeight[1] >> 8;
  packet[9] = kHeight[1] & 0xFF;

  RTPVideoHeader video_header;
  VideoRtpDepacketizerVp9::ParseRtpPayload(packet, &video_header);

  EXPECT_EQ(video_header.width, kWidth[0]);
  EXPECT_EQ(video_header.height, kHeight[0]);
}

TEST(VideoRtpDepacketizerVp9Test, ParseFailsForNoPayloadLength) {
  rtc::ArrayView<const uint8_t> empty;

  RTPVideoHeader video_header;
  EXPECT_EQ(VideoRtpDepacketizerVp9::ParseRtpPayload(empty, &video_header), 0);
}

TEST(VideoRtpDepacketizerVp9Test, ParseFailsForTooShortBufferToFitPayload) {
  uint8_t packet[] = {0};

  RTPVideoHeader video_header;
  EXPECT_EQ(VideoRtpDepacketizerVp9::ParseRtpPayload(packet, &video_header), 0);
}

TEST(VideoRtpDepacketizerVp9Test, ParseNonRefForInterLayerPred) {
  RTPVideoHeader video_header;
  RTPVideoHeaderVP9 expected;
  expected.InitRTPVideoHeaderVP9();
  uint8_t packet[2] = {0};

  packet[0] = 0x08;  // I:0 P:0 L:0 F:0 B:1 E:0 V:0 Z:0
  VideoRtpDepacketizerVp9::ParseRtpPayload(packet, &video_header);

  expected.beginning_of_frame = true;
  expected.non_ref_for_inter_layer_pred = false;
  VerifyHeader(expected,
               absl::get<RTPVideoHeaderVP9>(video_header.video_type_header));

  packet[0] = 0x05;  // I:0 P:0 L:0 F:0 B:0 E:1 V:0 Z:1
  VideoRtpDepacketizerVp9::ParseRtpPayload(packet, &video_header);

  expected.beginning_of_frame = false;
  expected.end_of_frame = true;
  expected.non_ref_for_inter_layer_pred = true;
  VerifyHeader(expected,
               absl::get<RTPVideoHeaderVP9>(video_header.video_type_header));
}

TEST(VideoRtpDepacketizerVp9Test, ReferencesInputCopyOnWriteBuffer) {
  constexpr size_t kHeaderSize = 1;
  uint8_t packet[4] = {0};
  packet[0] = 0x0C;  // I:0 P:0 L:0 F:0 B:1 E:1 V:0 Z:0

  rtc::CopyOnWriteBuffer rtp_payload(packet);
  VideoRtpDepacketizerVp9 depacketizer;
  absl::optional<VideoRtpDepacketizer::ParsedRtpPayload> parsed =
      depacketizer.Parse(rtp_payload);
  ASSERT_TRUE(parsed);

  EXPECT_EQ(parsed->video_payload.size(), rtp_payload.size() - kHeaderSize);
  // Compare pointers to check there was no copy on write buffer unsharing.
  EXPECT_EQ(parsed->video_payload.cdata(), rtp_payload.cdata() + kHeaderSize);
}

TEST(VideoRtpDepacketizerVp9Test, InterLayerPredOnlyFrameMerkedAsDelta) {
  // Set P=0 and D=1 and vefify that the depaketizers marks this packet as a
  // part of a delta frame (not a keyframe).
  uint8_t packet[13] = {0};
  packet[0] = 0b0010'0000;  // I:0 P:0 L:1 F:0 B:0 E:0 V:0 Z:0
  packet[1] = 0b0000'0001;  // T:000 U:0 S:000 D:1
  packet[2] = 0;            // TL0PICIDX

  RTPVideoHeader video_header;
  VideoRtpDepacketizerVp9::ParseRtpPayload(packet, &video_header);
  EXPECT_EQ(video_header.frame_type, VideoFrameType::kVideoFrameDelta);
}
}  // namespace
}  // namespace webrtc
