/*
 *  Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_coding/codecs/av1/libaom_av1_encoder_v2.h"

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "api/scoped_refptr.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "api/video/i420_buffer.h"
#include "api/video/resolution.h"
#include "api/video/video_frame_buffer.h"
#include "api/video_codecs/video_encoder_factory_interface.h"
#include "api/video_codecs/video_encoder_interface.h"
#include "api/video_codecs/video_encoding_general.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "third_party/libgav1/src/src/buffer_pool.h"
#include "third_party/libgav1/src/src/decoder_state.h"
#include "third_party/libgav1/src/src/gav1/status_code.h"
#include "third_party/libgav1/src/src/obu_parser.h"
#include "third_party/libgav1/src/src/utils/types.h"
#include "third_party/libyuv/include/libyuv/planar_functions.h"

// This file contains tests that are specific only for libaom encoder. Most
// tests should be done in the generic `video_encoder_functional_unittest.cc`
// file instead.

namespace webrtc {
namespace {

using ::testing::AllOf;
using ::testing::Ge;
using ::testing::Le;
using ::testing::Ne;
using FrameEncodeSettings = VideoEncoderInterface::FrameEncodeSettings;
using FrameOutput = VideoEncoderInterface::FrameOutput;
using FrameType = VideoEncoderInterface::FrameType;
using StaticEncoderSettings =
    VideoEncoderFactoryInterface::StaticEncoderSettings;
using TemporalUnitSettings = VideoEncoderInterface::TemporalUnitSettings;

struct EncodedFrameData : public FrameOutput {
  explicit EncodedFrameData(std::vector<uint8_t>& out) : bitstream(out) {}
  std::vector<uint8_t>& bitstream;
  std::span<uint8_t> GetBitstreamOutputBuffer(DataSize size) override {
    bitstream.resize(size.bytes());
    return bitstream;
  }
  void EncodeComplete(
      const VideoEncoderInterface::EncodeResult& result) override {}
};

class Av1HeaderParser {
 public:
  Av1HeaderParser()
      : buffer_pool_(/*on_frame_buffer_size_changed=*/nullptr,
                     /*get_frame_buffer=*/nullptr,
                     /*release_frame_buffer=*/nullptr,
                     /*callback_private_data=*/nullptr) {}

  std::optional<libgav1::ObuFrameHeader> ParseFrame(
      std::span<const uint8_t> data) {
    libgav1::RefCountedBufferPtr curr_frame;
    libgav1::ObuParser parser(data.data(), data.size(), /*operating_point=*/0,
                              &buffer_pool_, &decoder_state_);
    if (sequence_header_) {
      parser.set_sequence_header(*sequence_header_);
    }
    if (parser.ParseOneFrame(&curr_frame) != libgav1::kStatusOk) {
      return std::nullopt;
    }
    const libgav1::ObuFrameHeader& header = parser.frame_header();
    if (parser.sequence_header_changed()) {
      sequence_header_ = parser.sequence_header();
    }
    decoder_state_.UpdateReferenceFrames(curr_frame,
                                         header.refresh_frame_flags);
    return header;
  }

 private:
  libgav1::BufferPool buffer_pool_;
  libgav1::DecoderState decoder_state_;
  std::optional<libgav1::ObuSequenceHeader> sequence_header_;
};

scoped_refptr<I420Buffer> MakeDisjointRangeFrame(int width,
                                                 int height,
                                                 uint8_t base_y) {
  scoped_refptr<I420Buffer> buf = I420Buffer::Create(width, height);
  for (int by = 0; by < height / 4; ++by) {
    for (int bx = 0; bx < width / 4; ++bx) {
      int hash = (bx * 7 + by * 13 + bx * by * 11) ^ (bx * bx * 3 + by * 5);
      uint8_t y_val = base_y + ((hash & 0x7fff) % 9) * 4;
      libyuv::I420Rect(buf->MutableDataY(), buf->StrideY(), buf->MutableDataU(),
                       buf->StrideU(), buf->MutableDataV(), buf->StrideV(),
                       bx * 4, by * 4, 4, 4, y_val, 128, 128);
    }
  }
  return buf;
}

scoped_refptr<I420Buffer> BlendFrames(const I420BufferInterface& a,
                                      const I420BufferInterface& b,
                                      uint8_t bias_base_y = 0) {
  scoped_refptr<I420Buffer> out = I420Buffer::Create(a.width(), a.height());
  libyuv::InterpolatePlane(a.DataY(), a.StrideY(), b.DataY(), b.StrideY(),
                           out->MutableDataY(), out->StrideY(), a.width(),
                           a.height(), 128);
  libyuv::InterpolatePlane(a.DataU(), a.StrideU(), b.DataU(), b.StrideU(),
                           out->MutableDataU(), out->StrideU(), a.ChromaWidth(),
                           a.ChromaHeight(), 128);
  libyuv::InterpolatePlane(a.DataV(), a.StrideV(), b.DataV(), b.StrideV(),
                           out->MutableDataV(), out->StrideV(), a.ChromaWidth(),
                           a.ChromaHeight(), 128);
  if (bias_base_y > 0) {
    for (int by = 0; by < a.height() / 4; ++by) {
      for (int bx = 0; bx < a.width() / 4; ++bx) {
        if (((bx / 8) + (by / 8)) % 2 == 1) {
          int hash = (bx * 7 + by * 13 + bx * by * 11) ^ (bx * bx * 3 + by * 5);
          uint8_t y_val = bias_base_y + ((hash & 0x7fff) % 9) * 4;
          libyuv::I420Rect(out->MutableDataY(), out->StrideY(),
                           out->MutableDataU(), out->StrideU(),
                           out->MutableDataV(), out->StrideV(), bx * 4, by * 4,
                           4, 4, y_val, 128, 128);
        }
      }
    }
  }
  return out;
}

// Test that when multiple reference buffers are specified, the encoder attempts
// to use compound prediction mode. This is not necessary from a general
// compliance perspective (passing general BD-rate tests is enough), but is
// expected from the libaom encoder wrapper in this case.
TEST(LibaomAv1EncoderV2Test, CompoundPredictionUsedForTwoAndThreeReferences) {
  constexpr Resolution kRes = {.width = 256, .height = 256};
  // Use strictly disjoint luma intervals across the entire frame:
  //   p1 (GOLDEN) = [36..68],   m12 (GOLDEN_LAST) = [74..106],
  //   p2 (LAST)   = [112..144], m02 (ALTREF_LAST) = [150..182],
  //   p0 (ALTREF) = [188..220].
  // No motion vector from any single reference or wrong compound pair can
  // match m12 or m02. Half the 32x32 blocks per superblock in m02 equal p0 so
  // superblock y_sad_alt = y_sad_last / 3 (< 0.9 * y_sad_last in libaom).
  scoped_refptr<I420Buffer> p0 = MakeDisjointRangeFrame(256, 256, 188);
  scoped_refptr<I420Buffer> p1 = MakeDisjointRangeFrame(256, 256, 36);
  scoped_refptr<I420Buffer> p2 = MakeDisjointRangeFrame(256, 256, 112);
  scoped_refptr<I420Buffer> m12 = BlendFrames(*p1, *p2);
  scoped_refptr<I420Buffer> m02 = BlendFrames(*p0, *p2, 188);

  StaticEncoderSettings encoder_settings;
  encoder_settings.set_max_encode_dimensions(kRes);
  encoder_settings.set_encoding_format(
      {.sub_sampling = EncodingFormat::k420, .bit_depth = 8});

  LibaomAv1EncoderV2 enc;
  ASSERT_TRUE(enc.InitEncode(encoder_settings, {}));

  Av1HeaderParser parser;
  int64_t ts_ms = 0;
  auto encode_frame = [&](scoped_refptr<I420Buffer> frame, FrameType type,
                          std::initializer_list<int> refs,
                          std::optional<int> update_slot, int qp) {
    std::vector<uint8_t> bitstream;
    FrameEncodeSettings settings;
    settings.set_cqp_options(qp);
    settings.set_frame_type(type);
    settings.set_resolution(kRes);
    settings.set_reference_buffers(refs);
    settings.set_update_buffer(update_slot);
    settings.set_effort_level(2);
    settings.set_frame_output(std::make_unique<EncodedFrameData>(bitstream));
    std::vector<FrameEncodeSettings> frame_settings;
    frame_settings.push_back(std::move(settings));
    enc.Encode(frame, TemporalUnitSettings(Timestamp::Millis(ts_ms)),
               std::move(frame_settings));
    ts_ms += 33;
    return parser.ParseFrame(bitstream);
  };

  ASSERT_TRUE(encode_frame(p0, FrameType::kKeyframe, {}, 0, 20).has_value());
  ASSERT_TRUE(encode_frame(p1, FrameType::kDeltaFrame, {0}, 1, 20).has_value());

  auto encode_after_p2 = [&](scoped_refptr<I420Buffer> frame,
                             std::initializer_list<int> refs) {
    EXPECT_TRUE(
        encode_frame(p2, FrameType::kDeltaFrame, {1}, 2, 20).has_value());
    return encode_frame(frame, FrameType::kDeltaFrame, refs, std::nullopt, 180);
  };

  // Single reference: compound prediction must not be signaled.
  std::optional<libgav1::ObuFrameHeader> single_hdr = encode_after_p2(m12, {2});
  ASSERT_TRUE(single_hdr.has_value());
  EXPECT_FALSE(single_hdr->reference_mode_select);

  // Two references (LAST=2, GOLDEN=1) on m12 = (p1 + p2) / 2: exercises
  // GOLDEN_LAST compound prediction.
  std::optional<libgav1::ObuFrameHeader> comp2_hdr =
      encode_after_p2(m12, {2, 1});
  ASSERT_TRUE(comp2_hdr.has_value());
  EXPECT_TRUE(comp2_hdr->reference_mode_select);

  // Three references (LAST=2, GOLDEN=1, ALTREF=0) on m02 = (p0 + p2) / 2:
  // exercises ALTREF_LAST compound prediction.
  std::optional<libgav1::ObuFrameHeader> comp3_hdr =
      encode_after_p2(m02, {2, 1, 0});
  ASSERT_TRUE(comp3_hdr.has_value());
  EXPECT_TRUE(comp3_hdr->reference_mode_select);
}

TEST(LibaomAv1EncoderV2Test, CqpEnablesBoundedCyclicRefreshSegmentation) {
  constexpr Resolution kRes = {.width = 256, .height = 256};
  scoped_refptr<I420Buffer> f0 = MakeDisjointRangeFrame(256, 256, 64);
  scoped_refptr<I420Buffer> f1 = MakeDisjointRangeFrame(256, 256, 96);

  StaticEncoderSettings encoder_settings;
  encoder_settings.set_max_encode_dimensions(kRes);
  encoder_settings.set_encoding_format(
      {.sub_sampling = EncodingFormat::k420, .bit_depth = 8});

  LibaomAv1EncoderV2 enc;
  ASSERT_TRUE(enc.InitEncode(encoder_settings, {}));

  Av1HeaderParser parser;
  Timestamp ts = Timestamp::Zero();
  auto encode_frame = [&](scoped_refptr<I420Buffer> frame, FrameType type,
                          std::initializer_list<int> refs,
                          std::optional<int> update_slot, int qp) {
    std::vector<uint8_t> bitstream;
    FrameEncodeSettings settings;
    settings.set_cqp_options(qp);
    settings.set_frame_type(type);
    settings.set_resolution(kRes);
    settings.set_reference_buffers(refs);
    settings.set_update_buffer(update_slot);
    settings.set_effort_level(0);
    settings.set_frame_output(std::make_unique<EncodedFrameData>(bitstream));
    std::vector<FrameEncodeSettings> frame_settings;
    frame_settings.push_back(std::move(settings));
    enc.Encode(frame, TemporalUnitSettings(ts), std::move(frame_settings));
    ts += TimeDelta::Millis(33);
    return parser.ParseFrame(bitstream);
  };

  std::optional<libgav1::ObuFrameHeader> key_hdr =
      encode_frame(f0, FrameType::kKeyframe, {}, 0, 120);
  ASSERT_TRUE(key_hdr.has_value());
  EXPECT_EQ(key_hdr->quantizer.base_index, 120);

  std::optional<libgav1::ObuFrameHeader> delta_hdr =
      encode_frame(f1, FrameType::kDeltaFrame, {0}, 0, 120);
  ASSERT_TRUE(delta_hdr.has_value());
  EXPECT_EQ(delta_hdr->quantizer.base_index, 120);
  EXPECT_TRUE(delta_hdr->segmentation.enabled);
  EXPECT_TRUE(delta_hdr->segmentation.FeatureActive(
      1, libgav1::kSegmentFeatureQuantizer));
  const int16_t delta_q =
      delta_hdr->segmentation
          .feature_data[1][libgav1::kSegmentFeatureQuantizer];
  // When min_q == max_q == qindex, libaom still enables segmentation and
  // kSegmentFeatureQuantizer on segment 1, but clamps its delta_q to 0. A
  // non-zero delta_q verifies that the quantizer range gives cyclic refresh
  // room to adjust segment QP.
  EXPECT_THAT(delta_q, AllOf(Ne(0), Ge(-16), Le(16)));
}

TEST(LibaomAv1EncoderV2Test, CqpZeroEnablesLossless) {
  constexpr Resolution kRes = {.width = 256, .height = 256};
  scoped_refptr<I420Buffer> f0 = MakeDisjointRangeFrame(256, 256, 64);
  scoped_refptr<I420Buffer> f1 = MakeDisjointRangeFrame(256, 256, 96);

  StaticEncoderSettings encoder_settings;
  encoder_settings.set_max_encode_dimensions(kRes);
  encoder_settings.set_encoding_format(
      {.sub_sampling = EncodingFormat::k420, .bit_depth = 8});

  LibaomAv1EncoderV2 enc;
  ASSERT_TRUE(enc.InitEncode(encoder_settings, {}));

  Av1HeaderParser parser;
  Timestamp ts = Timestamp::Zero();
  auto encode_frame = [&](scoped_refptr<I420Buffer> frame, FrameType type,
                          std::initializer_list<int> refs,
                          std::optional<int> update_slot, int qp) {
    std::vector<uint8_t> bitstream;
    FrameEncodeSettings settings;
    settings.set_cqp_options(qp);
    settings.set_frame_type(type);
    settings.set_resolution(kRes);
    settings.set_reference_buffers(refs);
    settings.set_update_buffer(update_slot);
    settings.set_effort_level(0);
    settings.set_frame_output(std::make_unique<EncodedFrameData>(bitstream));
    std::vector<FrameEncodeSettings> frame_settings;
    frame_settings.push_back(std::move(settings));
    enc.Encode(frame, TemporalUnitSettings(ts), std::move(frame_settings));
    ts += TimeDelta::Millis(33);
    return parser.ParseFrame(bitstream);
  };

  std::optional<libgav1::ObuFrameHeader> key_hdr =
      encode_frame(f0, FrameType::kKeyframe, {}, 0, 0);
  ASSERT_TRUE(key_hdr.has_value());
  EXPECT_EQ(key_hdr->quantizer.base_index, 0);
  EXPECT_TRUE(key_hdr->coded_lossless);
  EXPECT_FALSE(key_hdr->segmentation.enabled);

  std::optional<libgav1::ObuFrameHeader> delta_hdr =
      encode_frame(f1, FrameType::kDeltaFrame, {0}, 0, 0);
  ASSERT_TRUE(delta_hdr.has_value());
  EXPECT_EQ(delta_hdr->quantizer.base_index, 0);
  EXPECT_TRUE(delta_hdr->coded_lossless);
  EXPECT_FALSE(delta_hdr->segmentation.enabled);
}

}  // namespace
}  // namespace webrtc
