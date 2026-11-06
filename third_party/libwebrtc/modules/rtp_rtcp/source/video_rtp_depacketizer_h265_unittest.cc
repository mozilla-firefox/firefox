/*
 *  Copyright (c) 2024 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/rtp_rtcp/source/video_rtp_depacketizer_h265.h"

#include <cstdint>
#include <vector>

#include "absl/types/optional.h"
#include "api/array_view.h"
#include "common_video/h265/h265_common.h"
#include "modules/rtp_rtcp/mocks/mock_rtp_rtcp.h"
#include "modules/rtp_rtcp/source/byte_io.h"
#include "modules/rtp_rtcp/source/rtp_packet_h265_common.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::testing::SizeIs;

TEST(VideoRtpDepacketizerH265Test, SingleNalu) {
  uint8_t packet[3] = {0x26, 0x02,
                       0xFF};  // F=0, Type=19 (Idr), LayerId=0, TID=2.
  uint8_t expected_packet[] = {0x00, 0x00, 0x00, 0x01, 0x26, 0x02, 0xff};
  rtc::CopyOnWriteBuffer rtp_payload(packet);

  VideoRtpDepacketizerH265 depacketizer;
  absl::optional<VideoRtpDepacketizer::ParsedRtpPayload> parsed =
      depacketizer.Parse(rtp_payload);
  ASSERT_TRUE(parsed);

  EXPECT_THAT(rtc::MakeArrayView(parsed->video_payload.cdata(),
                                 parsed->video_payload.size()),
              ElementsAreArray(expected_packet));
  EXPECT_EQ(parsed->video_header.frame_type, VideoFrameType::kVideoFrameKey);
  EXPECT_EQ(parsed->video_header.codec, kVideoCodecH265);
  EXPECT_TRUE(parsed->video_header.is_first_packet_in_frame);
}

TEST(VideoRtpDepacketizerH265Test, SingleNaluSpsWithResolution) {
  // SPS for a 1280x720 camera capture from ffmpeg on linux. Contains
  // emulation bytes but no cropping. This buffer is generated
  // with following command:
  // 1) ffmpeg -i /dev/video0 -r 30 -c:v libx265 -s 1280x720 camera.h265
  //
  // 2) Open camera.h265 and find the SPS, generally everything between the
  // second and third start codes (0 0 0 1 or 0 0 1). The first two bytes
  // 0x42 and 0x02 shows the nal header of SPS.
  uint8_t packet[] = {0x42, 0x02, 0x01, 0x04, 0x08, 0x00, 0x00, 0x03,
                      0x00, 0x9d, 0x08, 0x00, 0x00, 0x03, 0x00, 0x00,
                      0x5d, 0xb0, 0x02, 0x80, 0x80, 0x2d, 0x16, 0x59,
                      0x59, 0xa4, 0x93, 0x2b, 0x80, 0x40, 0x00, 0x00,
                      0x03, 0x00, 0x40, 0x00, 0x00, 0x07, 0x82};
  uint8_t expected_packet[] = {
      0x00, 0x00, 0x00, 0x01, 0x42, 0x02, 0x01, 0x04, 0x08, 0x00, 0x00,
      0x03, 0x00, 0x9d, 0x08, 0x00, 0x00, 0x03, 0x00, 0x00, 0x5d, 0xb0,
      0x02, 0x80, 0x80, 0x2d, 0x16, 0x59, 0x59, 0xa4, 0x93, 0x2b, 0x80,
      0x40, 0x00, 0x00, 0x03, 0x00, 0x40, 0x00, 0x00, 0x07, 0x82};
  rtc::CopyOnWriteBuffer rtp_payload(packet);

  VideoRtpDepacketizerH265 depacketizer;
  absl::optional<VideoRtpDepacketizer::ParsedRtpPayload> parsed =
      depacketizer.Parse(rtp_payload);
  ASSERT_TRUE(parsed);

  EXPECT_THAT(rtc::MakeArrayView(parsed->video_payload.cdata(),
                                 parsed->video_payload.size()),
              ElementsAreArray(expected_packet));
  EXPECT_EQ(parsed->video_header.codec, kVideoCodecH265);
  EXPECT_TRUE(parsed->video_header.is_first_packet_in_frame);
  EXPECT_EQ(parsed->video_header.width, 1280u);
  EXPECT_EQ(parsed->video_header.height, 720u);
}

TEST(VideoRtpDepacketizerH265Test, PaciPackets) {
  uint8_t packet[2] = {0x64, 0x02};  // F=0, Type=50 (PACI), LayerId=0, TID=2.
  rtc::CopyOnWriteBuffer rtp_payload(packet);

  VideoRtpDepacketizerH265 depacketizer;
  absl::optional<VideoRtpDepacketizer::ParsedRtpPayload> parsed =
      depacketizer.Parse(rtp_payload);
  ASSERT_FALSE(parsed);
}

TEST(VideoRtpDepacketizerH265Test, ApKey) {
  uint8_t payload_header[] = {0x60, 0x02};
  uint8_t vps_nalu_size[] = {0, 0x17};
  uint8_t sps_nalu_size[] = {0, 0x27};
  uint8_t pps_nalu_size[] = {0, 0x32};
  uint8_t slice_nalu_size[] = {0, 0xa};
  uint8_t start_code[] = {0x00, 0x00, 0x00, 0x01};
  // VPS/SPS/PPS/IDR for a 1280x720 camera capture from ffmpeg on linux.
  // Contains emulation bytes but no cropping. This buffer is generated with
  // following command: 1) ffmpeg -i /dev/video0 -r 30 -c:v libx265 -s 1280x720
  // camera.h265
  //
  // 2) Open camera.h265 and find:
  // VPS - generally everything between the first and second start codes (0 0 0
  // 1 or 0 0 1). The first two bytes 0x40 and 0x02 shows the nal header of VPS.
  // SPS - generally everything between the
  // second and third start codes (0 0 0 1 or 0 0 1). The first two bytes
  // 0x42 and 0x02 shows the nal header of SPS.
  // PPS - generally everything between the third and fourth start codes (0 0 0
  // 1 or 0 0 1). The first two bytes 0x44 and 0x02 shows the nal header of PPS.
  // IDR - Part of the keyframe bitstream (no need to show all the bytes for
  // depacketizer testing). The first two bytes 0x26 and 0x02 shows the nal
  // header of IDR frame.
  uint8_t vps[] = {
      0x40, 0x02, 0x1c, 0x01, 0xff, 0xff, 0x04, 0x08, 0x00, 0x00, 0x03, 0x00,
      0x9d, 0x08, 0x00, 0x00, 0x03, 0x00, 0x00, 0x78, 0x95, 0x98, 0x09,
  };
  uint8_t sps[] = {0x42, 0x02, 0x01, 0x04, 0x08, 0x00, 0x00, 0x03, 0x00, 0x9d,
                   0x08, 0x00, 0x00, 0x03, 0x00, 0x00, 0x5d, 0xb0, 0x02, 0x80,
                   0x80, 0x2d, 0x16, 0x59, 0x59, 0xa4, 0x93, 0x2b, 0x80, 0x40,
                   0x00, 0x00, 0x03, 0x00, 0x40, 0x00, 0x00, 0x07, 0x82};
  uint8_t pps[] = {0x44, 0x02, 0xa4, 0x04, 0x55, 0xa2, 0x6d, 0xce, 0xc0, 0xc3,
                   0xed, 0x0b, 0xac, 0xbc, 0x00, 0xc4, 0x44, 0x2e, 0xf7, 0x55,
                   0xfd, 0x05, 0x86, 0x92, 0x19, 0xdf, 0x58, 0xec, 0x38, 0x36,
                   0xb7, 0x7c, 0x00, 0x15, 0x33, 0x78, 0x03, 0x67, 0x26, 0x0f,
                   0x7b, 0x30, 0x1c, 0xd7, 0xd4, 0x3a, 0xec, 0xad, 0xef, 0x73};
  uint8_t idr[] = {0x26, 0x02, 0xaf, 0x08, 0x4a, 0x31, 0x11, 0x15, 0xe5, 0xc0};

  rtc::Buffer packet;
  packet.AppendData(payload_header);
  packet.AppendData(vps_nalu_size);
  packet.AppendData(vps);
  packet.AppendData(sps_nalu_size);
  packet.AppendData(sps);
  packet.AppendData(pps_nalu_size);
  packet.AppendData(pps);
  packet.AppendData(slice_nalu_size);
  packet.AppendData(idr);

  rtc::Buffer expected_packet;
  expected_packet.AppendData(start_code);
  expected_packet.AppendData(vps);
  expected_packet.AppendData(start_code);
  expected_packet.AppendData(sps);
  expected_packet.AppendData(start_code);
  expected_packet.AppendData(pps);
  expected_packet.AppendData(start_code);
  expected_packet.AppendData(idr);

  // clang-format on
  rtc::CopyOnWriteBuffer rtp_payload(packet);

  VideoRtpDepacketizerH265 depacketizer;
  absl::optional<VideoRtpDepacketizer::ParsedRtpPayload> parsed =
      depacketizer.Parse(rtp_payload);
  ASSERT_TRUE(parsed);

  EXPECT_THAT(rtc::MakeArrayView(parsed->video_payload.cdata(),
                                 parsed->video_payload.size()),
              ElementsAreArray(expected_packet));
  EXPECT_EQ(parsed->video_header.frame_type, VideoFrameType::kVideoFrameKey);
  EXPECT_EQ(parsed->video_header.codec, kVideoCodecH265);
  EXPECT_TRUE(parsed->video_header.is_first_packet_in_frame);
}

TEST(VideoRtpDepacketizerH265Test, ApNaluSpsWithResolution) {
  uint8_t payload_header[] = {0x60, 0x02};
  uint8_t vps_nalu_size[] = {0, 0x17};
  uint8_t sps_nalu_size[] = {0, 0x27};
  uint8_t pps_nalu_size[] = {0, 0x32};
  uint8_t slice_nalu_size[] = {0, 0xa};
  uint8_t start_code[] = {0x00, 0x00, 0x00, 0x01};
  // The VPS/SPS/PPS/IDR bytes are generated using the same way as above case.
  uint8_t vps[] = {
      0x40, 0x02, 0x1c, 0x01, 0xff, 0xff, 0x04, 0x08, 0x00, 0x00, 0x03, 0x00,
      0x9d, 0x08, 0x00, 0x00, 0x03, 0x00, 0x00, 0x78, 0x95, 0x98, 0x09,
  };
  uint8_t sps[] = {0x42, 0x02, 0x01, 0x04, 0x08, 0x00, 0x00, 0x03, 0x00, 0x9d,
                   0x08, 0x00, 0x00, 0x03, 0x00, 0x00, 0x5d, 0xb0, 0x02, 0x80,
                   0x80, 0x2d, 0x16, 0x59, 0x59, 0xa4, 0x93, 0x2b, 0x80, 0x40,
                   0x00, 0x00, 0x03, 0x00, 0x40, 0x00, 0x00, 0x07, 0x82};
  uint8_t pps[] = {0x44, 0x02, 0xa4, 0x04, 0x55, 0xa2, 0x6d, 0xce, 0xc0, 0xc3,
                   0xed, 0x0b, 0xac, 0xbc, 0x00, 0xc4, 0x44, 0x2e, 0xf7, 0x55,
                   0xfd, 0x05, 0x86, 0x92, 0x19, 0xdf, 0x58, 0xec, 0x38, 0x36,
                   0xb7, 0x7c, 0x00, 0x15, 0x33, 0x78, 0x03, 0x67, 0x26, 0x0f,
                   0x7b, 0x30, 0x1c, 0xd7, 0xd4, 0x3a, 0xec, 0xad, 0xef, 0x73};
  uint8_t idr[] = {0x26, 0x02, 0xaf, 0x08, 0x4a, 0x31, 0x11, 0x15, 0xe5, 0xc0};

  rtc::Buffer packet;
  packet.AppendData(payload_header);
  packet.AppendData(vps_nalu_size);
  packet.AppendData(vps);
  packet.AppendData(sps_nalu_size);
  packet.AppendData(sps);
  packet.AppendData(pps_nalu_size);
  packet.AppendData(pps);
  packet.AppendData(slice_nalu_size);
  packet.AppendData(idr);

  rtc::Buffer expected_packet;
  expected_packet.AppendData(start_code);
  expected_packet.AppendData(vps);
  expected_packet.AppendData(start_code);
  expected_packet.AppendData(sps);
  expected_packet.AppendData(start_code);
  expected_packet.AppendData(pps);
  expected_packet.AppendData(start_code);
  expected_packet.AppendData(idr);

  rtc::CopyOnWriteBuffer rtp_payload(packet);

  VideoRtpDepacketizerH265 depacketizer;
  absl::optional<VideoRtpDepacketizer::ParsedRtpPayload> parsed =
      depacketizer.Parse(rtp_payload);
  ASSERT_TRUE(parsed);

  EXPECT_THAT(rtc::MakeArrayView(parsed->video_payload.cdata(),
                                 parsed->video_payload.size()),
              ElementsAreArray(expected_packet));
  EXPECT_EQ(parsed->video_header.frame_type, VideoFrameType::kVideoFrameKey);
  EXPECT_EQ(parsed->video_header.codec, kVideoCodecH265);
  EXPECT_TRUE(parsed->video_header.is_first_packet_in_frame);
  EXPECT_EQ(parsed->video_header.width, 1280u);
  EXPECT_EQ(parsed->video_header.height, 720u);
}

TEST(VideoRtpDepacketizerH265Test, EmptyApRejected) {
  uint8_t lone_empty_packet[] = {0x60, 0x02,  // F=0, Type=48 (kH265Ap).
                                 0x00, 0x00};
  uint8_t leading_empty_packet[] = {0x60, 0x02,  // F=0, Type=48 (kH265Ap).
                                    0x00, 0x00, 0x00, 0x05, 0x26,
                                    0x02, 0xFF, 0x00, 0x11};  // kIdrWRadl
  uint8_t middle_empty_packet[] = {0x60, 0x02,  // F=0, Type=48 (kH265Ap).
                                   0x00, 0x04, 0x26, 0x02, 0xFF,
                                   0x00, 0x00, 0x00, 0x00, 0x05,
                                   0x26, 0x02, 0xFF, 0x00, 0x11};  // kIdrWRadl
  uint8_t trailing_empty_packet[] = {0x60, 0x02,  // F=0, Type=48 (kH265Ap).
                                     0x00, 0x04, 0x26,
                                     0x02, 0xFF, 0x00,  // kIdrWRadl
                                     0x00, 0x00};

  VideoRtpDepacketizerH265 depacketizer;
  EXPECT_FALSE(depacketizer.Parse(rtc::CopyOnWriteBuffer(lone_empty_packet)));
  EXPECT_FALSE(
      depacketizer.Parse(rtc::CopyOnWriteBuffer(leading_empty_packet)));
  EXPECT_FALSE(depacketizer.Parse(rtc::CopyOnWriteBuffer(middle_empty_packet)));
  EXPECT_FALSE(
      depacketizer.Parse(rtc::CopyOnWriteBuffer(trailing_empty_packet)));
}

TEST(VideoRtpDepacketizerH265Test, ApDelta) {
  uint8_t packet[20] = {0x60, 0x02,  // F=0, Type=48 (kH265Ap).
                                     // Length, nal header, payload.
                        0, 0x03, 0x02, 0x02, 0xFF,               // TrailR
                        0, 0x04, 0x02, 0x02, 0xFF, 0x00,         // TrailR
                        0, 0x05, 0x02, 0x02, 0xFF, 0x00, 0x11};  // TrailR
  uint8_t expected_packet[] = {
      0x00, 0x00, 0x00, 0x01, 0x02, 0x02, 0xFF,               // TrailR
      0x00, 0x00, 0x00, 0x01, 0x02, 0x02, 0xFF, 0x00,         // TrailR
      0x00, 0x00, 0x00, 0x01, 0x02, 0x02, 0xFF, 0x00, 0x11};  // TrailR
  rtc::CopyOnWriteBuffer rtp_payload(packet);

  VideoRtpDepacketizerH265 depacketizer;
  absl::optional<VideoRtpDepacketizer::ParsedRtpPayload> parsed =
      depacketizer.Parse(rtp_payload);
  ASSERT_TRUE(parsed);

  EXPECT_THAT(rtc::MakeArrayView(parsed->video_payload.cdata(),
                                 parsed->video_payload.size()),
              ElementsAreArray(expected_packet));

  EXPECT_EQ(parsed->video_header.frame_type, VideoFrameType::kVideoFrameDelta);
  EXPECT_EQ(parsed->video_header.codec, kVideoCodecH265);
  EXPECT_TRUE(parsed->video_header.is_first_packet_in_frame);
}

TEST(VideoRtpDepacketizerH265Test, Fu) {
  // clang-format off
  uint8_t packet1[] = {
      0x62, 0x02,  // F=0, Type=49 (kH265Fu).
      0x93,  // FU header kH265SBitMask | H265::kIdrWRadl.
      0xaf, 0x08, 0x4a, 0x31, 0x11, 0x15, 0xe5, 0xc0  // Payload.
  };
  // clang-format on
  // F=0, Type=19, (kIdrWRadl), tid=1, nalu header: 00100110 00000010, which is
  // 0x26, 0x02
  const uint8_t kExpected1[] = {0x00, 0x00, 0x00, 0x01, 0x26, 0x02, 0xaf,
                                0x08, 0x4a, 0x31, 0x11, 0x15, 0xe5, 0xc0};

  uint8_t packet2[] = {
      0x62, 0x02,     // F=0, Type=49 (kH265Fu).
      H265::kBlaWLp,  // FU header.
      0x02            // Payload.
  };
  const uint8_t kExpected2[] = {0x02};

  uint8_t packet3[] = {
      0x62, 0x02,  // F=0, Type=49 (kH265Fu).
      0x53,        // FU header kH265EBitMask | H265::kIdrWRadl.
      0x03         // Payload.
  };
  const uint8_t kExpected3[] = {0x03};

  VideoRtpDepacketizerH265 depacketizer;
  absl::optional<VideoRtpDepacketizer::ParsedRtpPayload> parsed1 =
      depacketizer.Parse(rtc::CopyOnWriteBuffer(packet1));
  ASSERT_TRUE(parsed1);
  // We expect that the first packet is one byte shorter since the FU header
  // has been replaced by the original nal header.
  EXPECT_THAT(rtc::MakeArrayView(parsed1->video_payload.cdata(),
                                 parsed1->video_payload.size()),
              ElementsAreArray(kExpected1));
  EXPECT_EQ(parsed1->video_header.frame_type, VideoFrameType::kVideoFrameKey);
  EXPECT_EQ(parsed1->video_header.codec, kVideoCodecH265);
  EXPECT_TRUE(parsed1->video_header.is_first_packet_in_frame);

  // Following packets will be 2 bytes shorter since they will only be appended
  // onto the first packet.
  auto parsed2 = depacketizer.Parse(rtc::CopyOnWriteBuffer(packet2));
  EXPECT_THAT(rtc::MakeArrayView(parsed2->video_payload.cdata(),
                                 parsed2->video_payload.size()),
              ElementsAreArray(kExpected2));
  EXPECT_FALSE(parsed2->video_header.is_first_packet_in_frame);
  EXPECT_EQ(parsed2->video_header.frame_type, VideoFrameType::kVideoFrameKey);
  EXPECT_EQ(parsed2->video_header.codec, kVideoCodecH265);

  auto parsed3 = depacketizer.Parse(rtc::CopyOnWriteBuffer(packet3));
  EXPECT_THAT(rtc::MakeArrayView(parsed3->video_payload.cdata(),
                                 parsed3->video_payload.size()),
              ElementsAreArray(kExpected3));
  EXPECT_FALSE(parsed3->video_header.is_first_packet_in_frame);
  EXPECT_EQ(parsed3->video_header.frame_type, VideoFrameType::kVideoFrameKey);
  EXPECT_EQ(parsed3->video_header.codec, kVideoCodecH265);
}

TEST(VideoRtpDepacketizerH265Test, EmptyPayload) {
  rtc::CopyOnWriteBuffer empty;
  VideoRtpDepacketizerH265 depacketizer;
  EXPECT_FALSE(depacketizer.Parse(empty));
}

TEST(VideoRtpDepacketizerH265Test, TruncatedFuNalu) {
  const uint8_t kPayload[] = {0x62};
  VideoRtpDepacketizerH265 depacketizer;
  EXPECT_FALSE(depacketizer.Parse(rtc::CopyOnWriteBuffer(kPayload)));
}

TEST(VideoRtpDepacketizerH265Test, TruncatedSingleApNalu) {
  const uint8_t kPayload[] = {0xe0, 0x02, 0x40};
  VideoRtpDepacketizerH265 depacketizer;
  EXPECT_FALSE(depacketizer.Parse(rtc::CopyOnWriteBuffer(kPayload)));
}

TEST(VideoRtpDepacketizerH265Test, ApPacketWithTruncatedNalUnits) {
  const uint8_t kPayload[] = {0x60, 0x02, 0xED, 0xDF};
  VideoRtpDepacketizerH265 depacketizer;
  EXPECT_FALSE(depacketizer.Parse(rtc::CopyOnWriteBuffer(kPayload)));
}

TEST(VideoRtpDepacketizerH265Test, TruncationJustAfterSingleApNalu) {
  const uint8_t kPayload[] = {0x60, 0x02, 0x40, 0x40};
  VideoRtpDepacketizerH265 depacketizer;
  EXPECT_FALSE(depacketizer.Parse(rtc::CopyOnWriteBuffer(kPayload)));
}

TEST(VideoRtpDepacketizerH265Test, ShortSpsPacket) {
  const uint8_t kPayload[] = {0x40, 0x80, 0x00};
  VideoRtpDepacketizerH265 depacketizer;
  EXPECT_TRUE(depacketizer.Parse(rtc::CopyOnWriteBuffer(kPayload)));
}

TEST(VideoRtpDepacketizerH265Test, InvalidNaluSizeApNalu) {
  const uint8_t kPayload[] = {0x60, 0x02,  // F=0, Type=48 (kH265Ap).
                                           // Length, nal header, payload.
                              0, 0xff, 0x02, 0x02, 0xFF,  // TrailR
                              0, 0x05, 0x02, 0x02, 0xFF, 0x00,
                              0x11};  // TrailR;
  VideoRtpDepacketizerH265 depacketizer;
  EXPECT_FALSE(depacketizer.Parse(rtc::CopyOnWriteBuffer(kPayload)));
}

TEST(VideoRtpDepacketizerH265Test, SeiPacket) {
  const uint8_t kPayload[] = {
      0x4e, 0x02,             // F=0, Type=39 (kPrefixSei).
      0x03, 0x03, 0x03, 0x03  // Payload.
  };
  VideoRtpDepacketizerH265 depacketizer;
  auto parsed = depacketizer.Parse(rtc::CopyOnWriteBuffer(kPayload));
  ASSERT_TRUE(parsed);
}

}  // namespace
}  // namespace webrtc
