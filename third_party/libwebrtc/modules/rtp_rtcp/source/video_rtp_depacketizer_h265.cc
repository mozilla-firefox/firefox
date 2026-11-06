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

#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "absl/base/macros.h"
#include "absl/types/optional.h"
#include "absl/types/variant.h"
#include "api/video/video_codec_type.h"
#include "common_video/h264/h264_common.h"
#include "common_video/h265/h265_bitstream_parser.h"
#include "common_video/h265/h265_common.h"
#include "modules/rtp_rtcp/source/byte_io.h"
#include "modules/rtp_rtcp/source/rtp_packet_h265_common.h"
#include "modules/rtp_rtcp/source/video_rtp_depacketizer.h"
#include "rtc_base/checks.h"
#include "rtc_base/logging.h"

// RTP Payload Format for HEVC: https://datatracker.ietf.org/doc/html/rfc7798

namespace webrtc {
namespace {

bool ParseApStartOffsets(const uint8_t* nalu_ptr,
                         size_t length_remaining,
                         std::vector<size_t>* offsets) {
  size_t offset = 0;
  while (length_remaining > 0) {
    // Buffer doesn't contain room for additional NALU length.
    if (length_remaining < kH265LengthFieldSizeBytes)
      return false;
    // Read 16-bit NALU size defined in RFC7798 section 4.4.2.
    uint16_t nalu_size = ByteReader<uint16_t>::ReadBigEndian(nalu_ptr);
    nalu_ptr += kH265LengthFieldSizeBytes;
    length_remaining -= kH265LengthFieldSizeBytes;
    if (nalu_size > length_remaining)
      return false;
    nalu_ptr += nalu_size;
    length_remaining -= nalu_size;

    offsets->push_back(offset + kH265ApHeaderSizeBytes);
    offset += kH265LengthFieldSizeBytes + nalu_size;
  }
  return true;
}

// Single NALU packet structure
// https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.1
// Aggregation Packet (AP) strcture
// https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.2
absl::optional<VideoRtpDepacketizer::ParsedRtpPayload> ProcessApOrSingleNalu(
    rtc::CopyOnWriteBuffer rtp_payload) {
  // Skip the single NALU header (payload header), aggregated packet case will
  // be checked later.
  if (rtp_payload.size() <= kH265PayloadHeaderSizeBytes) {
    RTC_LOG(LS_ERROR) << "Single NALU header truncated.";
    return absl::nullopt;
  }
  const uint8_t* const payload_data = rtp_payload.cdata();
  absl::optional<VideoRtpDepacketizer::ParsedRtpPayload> parsed_payload(
      absl::in_place);
  parsed_payload->video_header.width = 0;
  parsed_payload->video_header.height = 0;
  parsed_payload->video_header.codec = kVideoCodecH265;
  parsed_payload->video_header.is_first_packet_in_frame = true;

  const uint8_t* nalu_start = payload_data + kH265PayloadHeaderSizeBytes;
  const size_t nalu_length = rtp_payload.size() - kH265PayloadHeaderSizeBytes;
  uint8_t nal_type = (payload_data[0] & kH265TypeMask) >> 1;
  std::vector<size_t> nalu_start_offsets;
  rtc::CopyOnWriteBuffer video_payload;
  if (nal_type == H265::NaluType::kAp) {
    // Skip the aggregated packet header (Aggregated packet NAL type + length).
    if (rtp_payload.size() <= kH265ApHeaderSizeBytes) {
      RTC_LOG(LS_ERROR) << "Aggregated packet header truncated.";
      return absl::nullopt;
    }

    if (!ParseApStartOffsets(nalu_start, nalu_length, &nalu_start_offsets)) {
      RTC_LOG(LS_ERROR)
          << "Aggregated packet with incorrect NALU packet lengths.";
      return absl::nullopt;
    }

    nal_type = (payload_data[kH265ApHeaderSizeBytes] & kH265TypeMask) >> 1;
  } else {
    nalu_start_offsets.push_back(0);
  }
  parsed_payload->video_header.frame_type = VideoFrameType::kVideoFrameDelta;

  nalu_start_offsets.push_back(rtp_payload.size() +
                               kH265LengthFieldSizeBytes);  // End offset.
  for (size_t i = 0; i < nalu_start_offsets.size() - 1; ++i) {
    size_t start_offset = nalu_start_offsets[i];
    // End offset is actually start offset for next unit, excluding length field
    // so remove that from this units length.
    size_t end_offset = nalu_start_offsets[i + 1] - kH265LengthFieldSizeBytes;
    if (end_offset - start_offset < kH265NalHeaderSizeBytes) {
      RTC_LOG(LS_ERROR) << "Aggregated packet too short";
      return absl::nullopt;
    }

    // Insert start code before each NALU in aggregated packet.
    video_payload.AppendData(kStartCode);
    video_payload.AppendData(&payload_data[start_offset],
                             end_offset - start_offset);

    uint8_t nalu_type = (payload_data[start_offset] & kH265TypeMask) >> 1;
    start_offset += kH265NalHeaderSizeBytes;
    switch (nalu_type) {
      case H265::NaluType::kBlaWLp:
      case H265::NaluType::kBlaWRadl:
      case H265::NaluType::kBlaNLp:
      case H265::NaluType::kIdrWRadl:
      case H265::NaluType::kIdrNLp:
      case H265::NaluType::kCra:
      case H265::NaluType::kRsvIrapVcl23:
        // Mark IRAP(Intra Random Access Point) frames as key frames. Their NALU
        // types are in the range of BLA_W_LP (16) to RSV_IRAP_VCL23 (23),
        // inclusive.
        // https://datatracker.ietf.org/doc/html/rfc7798#section-3.1.1
        parsed_payload->video_header.frame_type =
            VideoFrameType::kVideoFrameKey;
        ABSL_FALLTHROUGH_INTENDED;
      case H265::NaluType::kSps: {
        // Copy any previous data first (likely just the first header).
        std::unique_ptr<rtc::Buffer> output_buffer(new rtc::Buffer());
        if (start_offset)
          output_buffer->AppendData(payload_data, start_offset);

        absl::optional<H265SpsParser::SpsState> sps = H265SpsParser::ParseSps(
            &payload_data[start_offset], end_offset - start_offset);

        if (sps) {
          // TODO(bugs.webrtc.org/13485): Implement the size calculation taking
          // VPS->vui_parameters.def_disp_win_xx_offset into account.
          parsed_payload->video_header.width = sps->width;
          parsed_payload->video_header.height = sps->height;
        } else {
          RTC_LOG(LS_WARNING) << "Failed to parse SPS from SPS slice.";
        }
      }
        ABSL_FALLTHROUGH_INTENDED;
      case H265::NaluType::kVps:
      case H265::NaluType::kPps:
      case H265::NaluType::kTrailN:
      case H265::NaluType::kTrailR:
      // Slices below don't contain SPS or PPS ids.
      case H265::NaluType::kAud:
      case H265::NaluType::kTsaN:
      case H265::NaluType::kTsaR:
      case H265::NaluType::kStsaN:
      case H265::NaluType::kStsaR:
      case H265::NaluType::kRadlN:
      case H265::NaluType::kRadlR:
      case H265::NaluType::kPrefixSei:
      case H265::NaluType::kSuffixSei:
        break;
      case H265::NaluType::kAp:
      case H265::NaluType::kFu:
      case H265::NaluType::kPaci:
        RTC_LOG(LS_WARNING) << "Unexpected AP, FU or PACI received.";
        return absl::nullopt;
    }
  }
  parsed_payload->video_payload = video_payload;
  return parsed_payload;
}

// Fragmentation Unit (FU) structure:
// https://datatracker.ietf.org/doc/html/rfc7798#section-4.4.3
absl::optional<VideoRtpDepacketizer::ParsedRtpPayload> ParseFuNalu(
    rtc::CopyOnWriteBuffer rtp_payload) {
  if (rtp_payload.size() < kH265FuHeaderSizeBytes + kH265NalHeaderSizeBytes) {
    RTC_LOG(LS_ERROR) << "FU NAL units truncated.";
    return absl::nullopt;
  }
  absl::optional<VideoRtpDepacketizer::ParsedRtpPayload> parsed_payload(
      absl::in_place);

  uint8_t f = rtp_payload.cdata()[0] & kH265FBit;
  uint8_t layer_id_h = rtp_payload.cdata()[0] & kH265LayerIDHMask;
  uint8_t layer_id_l_unshifted = rtp_payload.cdata()[1] & kH265LayerIDLMask;
  uint8_t tid = rtp_payload.cdata()[1] & kH265TIDMask;

  uint8_t original_nal_type = rtp_payload.cdata()[2] & kH265TypeMaskInFuHeader;
  bool first_fragment = rtp_payload.cdata()[2] & kH265SBitMask;
  if (first_fragment) {
    rtp_payload = rtp_payload.Slice(
        kH265FuHeaderSizeBytes, rtp_payload.size() - kH265FuHeaderSizeBytes);
    rtp_payload.MutableData()[0] = f | original_nal_type << 1 | layer_id_h;
    rtp_payload.MutableData()[1] = layer_id_l_unshifted | tid;
    rtc::CopyOnWriteBuffer video_payload;
    // Insert start code before the first fragment in FU.
    video_payload.AppendData(kStartCode);
    video_payload.AppendData(rtp_payload);
    parsed_payload->video_payload = video_payload;
  } else {
    parsed_payload->video_payload = rtp_payload.Slice(
        kH265NalHeaderSizeBytes + kH265FuHeaderSizeBytes,
        rtp_payload.size() - kH265NalHeaderSizeBytes - kH265FuHeaderSizeBytes);
  }

  if (original_nal_type >= H265::NaluType::kBlaWLp &&
      original_nal_type <= H265::NaluType::kRsvIrapVcl23) {
    // IRAP picture.
    // https://datatracker.ietf.org/doc/html/rfc7798#section-3.1.1
    parsed_payload->video_header.frame_type = VideoFrameType::kVideoFrameKey;
  } else {
    parsed_payload->video_header.frame_type = VideoFrameType::kVideoFrameDelta;
  }
  parsed_payload->video_header.width = 0;
  parsed_payload->video_header.height = 0;
  parsed_payload->video_header.codec = kVideoCodecH265;
  parsed_payload->video_header.is_first_packet_in_frame = first_fragment;

  return parsed_payload;
}

}  // namespace

absl::optional<VideoRtpDepacketizer::ParsedRtpPayload>
VideoRtpDepacketizerH265::Parse(rtc::CopyOnWriteBuffer rtp_payload) {
  if (rtp_payload.empty()) {
    RTC_LOG(LS_ERROR) << "Empty payload.";
    return absl::nullopt;
  }

  uint8_t nal_type = (rtp_payload.cdata()[0] & kH265TypeMask) >> 1;

  if (nal_type == H265::NaluType::kFu) {
    // Fragmented NAL units (FU).
    return ParseFuNalu(std::move(rtp_payload));
  } else if (nal_type == H265::NaluType::kPaci) {
    // TODO(bugs.webrtc.org/13485): Implement PACI parse for H265
    RTC_LOG(LS_ERROR) << "Not support type:" << nal_type;
    return absl::nullopt;
  } else {
    // Single NAL unit packet or Aggregated packets (AP).
    return ProcessApOrSingleNalu(std::move(rtp_payload));
  }
}

}  // namespace webrtc
