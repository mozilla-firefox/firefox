/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_coding/h26x_packet_buffer.h"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

#include "api/array_view.h"
#include "api/rtp_packet_info.h"
#include "api/video/video_frame_type.h"
#include "common_video/h264/h264_common.h"
#include "common_video/h264/pps_parser.h"
#include "common_video/h264/sps_parser.h"
#include "modules/rtp_rtcp/source/rtp_header_extensions.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "modules/rtp_rtcp/source/rtp_video_header.h"
#include "modules/video_coding/codecs/h264/include/h264_globals.h"
#include "modules/video_coding/h264_sprop_parameter_sets.h"
#include "rtc_base/checks.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/logging.h"
#include "rtc_base/numerics/sequence_number_util.h"
#ifdef RTC_ENABLE_H265
#include "common_video/h265/h265_common.h"
#endif

namespace webrtc {
namespace {

int64_t EuclideanMod(int64_t n, int64_t div) {
  RTC_DCHECK_GT(div, 0);
  return (n %= div) < 0 ? n + div : n;
}

rtc::ArrayView<const NaluInfo> GetNaluInfos(
    const RTPVideoHeaderH264& h264_header) {
  if (h264_header.nalus_length > kMaxNalusPerPacket) {
    return {};
  }

  return rtc::MakeArrayView(h264_header.nalus, h264_header.nalus_length);
}

bool IsFirstPacketOfFragment(const RTPVideoHeaderH264& h264_header) {
  return h264_header.nalus_length > 0;
}

bool BeginningOfIdr(const H26xPacketBuffer::Packet& packet) {
  const auto& h264_header =
      absl::get<RTPVideoHeaderH264>(packet.video_header.video_type_header);
  const bool contains_idr_nalu =
      absl::c_any_of(GetNaluInfos(h264_header), [](const auto& nalu_info) {
        return nalu_info.type == H264::NaluType::kIdr;
      });
  switch (h264_header.packetization_type) {
    case kH264StapA:
    case kH264SingleNalu: {
      return contains_idr_nalu;
    }
    case kH264FuA: {
      return contains_idr_nalu && IsFirstPacketOfFragment(h264_header);
    }
  }
}

bool HasSps(const H26xPacketBuffer::Packet& packet) {
  auto& h264_header =
      absl::get<RTPVideoHeaderH264>(packet.video_header.video_type_header);
  return absl::c_any_of(GetNaluInfos(h264_header), [](const auto& nalu_info) {
    return nalu_info.type == H264::NaluType::kSps;
  });
}

#ifdef RTC_ENABLE_H265
bool HasVps(const H26xPacketBuffer::Packet& packet) {
  std::vector<H265::NaluIndex> nalu_indices = H265::FindNaluIndices(
      packet.video_payload.cdata(), packet.video_payload.size());
  return absl::c_any_of((nalu_indices), [&packet](
                                            const H265::NaluIndex& nalu_index) {
    return H265::ParseNaluType(
               packet.video_payload.cdata()[nalu_index.payload_start_offset]) ==
           H265::NaluType::kVps;
  });
}
#endif

}  // namespace

H26xPacketBuffer::H26xPacketBuffer(bool h264_idr_only_keyframes_allowed)
    : h264_idr_only_keyframes_allowed_(h264_idr_only_keyframes_allowed) {}

H26xPacketBuffer::InsertResult H26xPacketBuffer::InsertPacket(
    std::unique_ptr<Packet> packet) {
  RTC_DCHECK(packet->video_header.codec == kVideoCodecH264 ||
             packet->video_header.codec == kVideoCodecH265);

  InsertResult result;

  int64_t unwrapped_seq_num = seq_num_unwrapper_.Unwrap(packet->seq_num);
  auto& packet_slot = GetPacket(unwrapped_seq_num);
  if (packet_slot != nullptr &&
      AheadOrAt(packet_slot->timestamp, packet->timestamp)) {
    // The incoming `packet` is old or a duplicate.
    return result;
  } else {
    packet_slot = std::move(packet);
  }

  return FindFrames(unwrapped_seq_num);
}

std::unique_ptr<H26xPacketBuffer::Packet>& H26xPacketBuffer::GetPacket(
    int64_t unwrapped_seq_num) {
  return buffer_[EuclideanMod(unwrapped_seq_num, kBufferSize)];
}

bool H26xPacketBuffer::BeginningOfStream(
    const H26xPacketBuffer::Packet& packet) const {
  if (packet.codec() == kVideoCodecH264) {
    return HasSps(packet) ||
           (h264_idr_only_keyframes_allowed_ && BeginningOfIdr(packet));
#ifdef RTC_ENABLE_H265
  } else if (packet.codec() == kVideoCodecH265) {
    return HasVps(packet);
#endif
  }
  RTC_DCHECK_NOTREACHED();
  return false;
}

H26xPacketBuffer::InsertResult H26xPacketBuffer::FindFrames(
    int64_t unwrapped_seq_num) {
  InsertResult result;

  Packet* packet = GetPacket(unwrapped_seq_num).get();
  RTC_CHECK(packet != nullptr);

  // Check if the packet is continuous or the beginning of a new coded video
  // sequence.
  if (unwrapped_seq_num - 1 != last_continuous_unwrapped_seq_num_) {
    if (unwrapped_seq_num <= last_continuous_unwrapped_seq_num_ ||
        !BeginningOfStream(*packet)) {
      return result;
    }

    last_continuous_unwrapped_seq_num_ = unwrapped_seq_num;
  }

  for (int64_t seq_num = unwrapped_seq_num;
       seq_num < unwrapped_seq_num + kBufferSize;) {
    RTC_DCHECK_GE(seq_num, *last_continuous_unwrapped_seq_num_);

    // Packets that were never assembled into a completed frame will stay in
    // the 'buffer_'. Check that the `packet` sequence number match the expected
    // unwrapped sequence number.
    if (static_cast<uint16_t>(seq_num) != packet->seq_num) {
      return result;
    }

    last_continuous_unwrapped_seq_num_ = seq_num;
    // Last packet of the frame, try to assemble the frame.
    if (packet->marker_bit) {
      uint32_t rtp_timestamp = packet->timestamp;

      // Iterate backwards to find where the frame starts.
      for (int64_t seq_num_start = seq_num;
           seq_num_start > seq_num - kBufferSize; --seq_num_start) {
        auto& prev_packet = GetPacket(seq_num_start - 1);

        if (prev_packet == nullptr || prev_packet->timestamp != rtp_timestamp) {
          if (MaybeAssembleFrame(seq_num_start, seq_num, result)) {
            // Frame was assembled, continue to look for more frames.
            break;
          } else {
            // Frame was not assembled, no subsequent frame will be continuous.
            return result;
          }
        }
      }
    }

    seq_num++;
    packet = GetPacket(seq_num).get();
    if (packet == nullptr) {
      return result;
    }
  }

  return result;
}

bool H26xPacketBuffer::MaybeAssembleFrame(int64_t start_seq_num_unwrapped,
                                          int64_t end_sequence_number_unwrapped,
                                          InsertResult& result) {
#ifdef RTC_ENABLE_H265
  bool has_vps = false;
#endif
  bool has_sps = false;
  bool has_pps = false;
  // Includes IDR, CRA and BLA for HEVC.
  bool has_idr = false;

  int width = -1;
  int height = -1;

  for (int64_t seq_num = start_seq_num_unwrapped;
       seq_num <= end_sequence_number_unwrapped; ++seq_num) {
    const auto& packet = GetPacket(seq_num);
    if (packet->codec() == kVideoCodecH264) {
      const auto& h264_header =
          absl::get<RTPVideoHeaderH264>(packet->video_header.video_type_header);
      for (const auto& nalu : GetNaluInfos(h264_header)) {
        has_idr |= nalu.type == H264::NaluType::kIdr;
        has_sps |= nalu.type == H264::NaluType::kSps;
        has_pps |= nalu.type == H264::NaluType::kPps;
      }
      if (has_idr) {
        if (!h264_idr_only_keyframes_allowed_ && (!has_sps || !has_pps)) {
          return false;
        }
      }
#ifdef RTC_ENABLE_H265
    } else if (packet->codec() == kVideoCodecH265) {
      std::vector<H265::NaluIndex> nalu_indices = H265::FindNaluIndices(
          packet->video_payload.cdata(), packet->video_payload.size());
      for (const auto& nalu_index : nalu_indices) {
        uint8_t nalu_type = H265::ParseNaluType(
            packet->video_payload.cdata()[nalu_index.payload_start_offset]);
        has_idr |= (nalu_type >= H265::NaluType::kBlaWLp &&
                    nalu_type <= H265::NaluType::kRsvIrapVcl23);
        has_vps |= nalu_type == H265::NaluType::kVps;
        has_sps |= nalu_type == H265::NaluType::kSps;
        has_pps |= nalu_type == H265::NaluType::kPps;
      }
      if (has_idr) {
        if (!has_vps || !has_sps || !has_pps) {
          return false;
        }
      }
#endif  // RTC_ENABLE_H265
    }

    width = std::max<int>(packet->video_header.width, width);
    height = std::max<int>(packet->video_header.height, height);
  }

  for (int64_t seq_num = start_seq_num_unwrapped;
       seq_num <= end_sequence_number_unwrapped; ++seq_num) {
    auto& packet = GetPacket(seq_num);

    packet->video_header.is_first_packet_in_frame =
        (seq_num == start_seq_num_unwrapped);
    packet->video_header.is_last_packet_in_frame =
        (seq_num == end_sequence_number_unwrapped);

    if (packet->video_header.is_first_packet_in_frame) {
      if (width > 0 && height > 0) {
        packet->video_header.width = width;
        packet->video_header.height = height;
      }

      packet->video_header.frame_type = has_idr
                                            ? VideoFrameType::kVideoFrameKey
                                            : VideoFrameType::kVideoFrameDelta;
    }

    // Only applies to H.264 because start code is inserted by depacktizer for
    // H.265 and out-of-band parameter sets is not supported by H.265.
    if (packet->codec() == kVideoCodecH264) {
      if (!FixH264Packet(*packet)) {
        // The buffer is not cleared actually, but a key frame request is
        // needed.
        result.buffer_cleared = true;
        return false;
      }
    }

    result.packets.push_back(std::move(packet));
  }

  return true;
}

void H26xPacketBuffer::SetSpropParameterSets(
    const std::string& sprop_parameter_sets) {
  if (!h264_idr_only_keyframes_allowed_) {
    RTC_LOG(LS_WARNING) << "Ignore sprop parameter sets because IDR only "
                           "keyframe is not allowed.";
    return;
  }
  H264SpropParameterSets sprop_decoder;
  if (!sprop_decoder.DecodeSprop(sprop_parameter_sets)) {
    return;
  }
  InsertSpsPpsNalus(sprop_decoder.sps_nalu(), sprop_decoder.pps_nalu());
}

void H26xPacketBuffer::InsertSpsPpsNalus(const std::vector<uint8_t>& sps,
                                         const std::vector<uint8_t>& pps) {
  RTC_CHECK(h264_idr_only_keyframes_allowed_);
  constexpr size_t kNaluHeaderOffset = 1;
  if (sps.size() < kNaluHeaderOffset) {
    RTC_LOG(LS_WARNING) << "SPS size  " << sps.size() << " is smaller than "
                        << kNaluHeaderOffset;
    return;
  }
  if ((sps[0] & 0x1f) != H264::NaluType::kSps) {
    RTC_LOG(LS_WARNING) << "SPS Nalu header missing";
    return;
  }
  if (pps.size() < kNaluHeaderOffset) {
    RTC_LOG(LS_WARNING) << "PPS size  " << pps.size() << " is smaller than "
                        << kNaluHeaderOffset;
    return;
  }
  if ((pps[0] & 0x1f) != H264::NaluType::kPps) {
    RTC_LOG(LS_WARNING) << "SPS Nalu header missing";
    return;
  }
  absl::optional<SpsParser::SpsState> parsed_sps = SpsParser::ParseSps(
      sps.data() + kNaluHeaderOffset, sps.size() - kNaluHeaderOffset);
  absl::optional<PpsParser::PpsState> parsed_pps = PpsParser::ParsePps(
      pps.data() + kNaluHeaderOffset, pps.size() - kNaluHeaderOffset);

  if (!parsed_sps) {
    RTC_LOG(LS_WARNING) << "Failed to parse SPS.";
  }

  if (!parsed_pps) {
    RTC_LOG(LS_WARNING) << "Failed to parse PPS.";
  }

  if (!parsed_pps || !parsed_sps) {
    return;
  }

  SpsInfo sps_info;
  sps_info.size = sps.size();
  sps_info.width = parsed_sps->width;
  sps_info.height = parsed_sps->height;
  uint8_t* sps_data = new uint8_t[sps_info.size];
  memcpy(sps_data, sps.data(), sps_info.size);
  sps_info.payload.reset(sps_data);
  sps_data_[parsed_sps->id] = std::move(sps_info);

  PpsInfo pps_info;
  pps_info.size = pps.size();
  pps_info.sps_id = parsed_pps->sps_id;
  uint8_t* pps_data = new uint8_t[pps_info.size];
  memcpy(pps_data, pps.data(), pps_info.size);
  pps_info.payload.reset(pps_data);
  pps_data_[parsed_pps->id] = std::move(pps_info);

  RTC_LOG(LS_INFO) << "Inserted SPS id " << parsed_sps->id << " and PPS id "
                   << parsed_pps->id << " (referencing SPS "
                   << parsed_pps->sps_id << ")";
}

// TODO(bugs.webrtc.org/13157): Update the H264 depacketizer so we don't have to
//                              fiddle with the payload at this point.
bool H26xPacketBuffer::FixH264Packet(Packet& packet) {
  constexpr uint8_t kStartCode[] = {0, 0, 0, 1};

  RTPVideoHeader& video_header = packet.video_header;
  RTPVideoHeaderH264& h264_header =
      absl::get<RTPVideoHeaderH264>(video_header.video_type_header);

  rtc::CopyOnWriteBuffer result;

  if (h264_idr_only_keyframes_allowed_) {
    // Check if sps and pps insertion is needed.
    bool prepend_sps_pps = false;
    auto sps = sps_data_.end();
    auto pps = pps_data_.end();

    for (size_t i = 0; i < h264_header.nalus_length; ++i) {
      const NaluInfo& nalu = h264_header.nalus[i];
      switch (nalu.type) {
        case H264::NaluType::kSps: {
          SpsInfo& sps_info = sps_data_[nalu.sps_id];
          sps_info.width = video_header.width;
          sps_info.height = video_header.height;
          break;
        }
        case H264::NaluType::kPps: {
          pps_data_[nalu.pps_id].sps_id = nalu.sps_id;
          break;
        }
        case H264::NaluType::kIdr: {
          // If this is the first packet of an IDR, make sure we have the
          // required SPS/PPS and also calculate how much extra space we need
          // in the buffer to prepend the SPS/PPS to the bitstream with start
          // codes.
          if (video_header.is_first_packet_in_frame) {
            if (nalu.pps_id == -1) {
              RTC_LOG(LS_WARNING) << "No PPS id in IDR nalu.";
              return false;
            }

            pps = pps_data_.find(nalu.pps_id);
            if (pps == pps_data_.end()) {
              RTC_LOG(LS_WARNING)
                  << "No PPS with id << " << nalu.pps_id << " received";
              return false;
            }

            sps = sps_data_.find(pps->second.sps_id);
            if (sps == sps_data_.end()) {
              RTC_LOG(LS_WARNING)
                  << "No SPS with id << " << pps->second.sps_id << " received";
              return false;
            }

            // Since the first packet of every keyframe should have its width
            // and height set we set it here in the case of it being supplied
            // out of band.
            video_header.width = sps->second.width;
            video_header.height = sps->second.height;

            // If the SPS/PPS was supplied out of band then we will have saved
            // the actual bitstream in `data`.
            if (sps->second.payload && pps->second.payload) {
              RTC_DCHECK_GT(sps->second.size, 0);
              RTC_DCHECK_GT(pps->second.size, 0);
              prepend_sps_pps = true;
            }
          }
          break;
        }
        default:
          break;
      }
    }

    RTC_CHECK(!prepend_sps_pps ||
              (sps != sps_data_.end() && pps != pps_data_.end()));

    // Insert SPS and PPS if they are missing.
    if (prepend_sps_pps) {
      // Insert SPS.
      result.AppendData(kStartCode);
      result.AppendData(sps->second.payload.get(), sps->second.size);

      // Insert PPS.
      result.AppendData(kStartCode);
      result.AppendData(pps->second.payload.get(), pps->second.size);

      // Update codec header to reflect the newly added SPS and PPS.
      NaluInfo sps_info;
      sps_info.type = H264::NaluType::kSps;
      sps_info.sps_id = sps->first;
      sps_info.pps_id = -1;
      NaluInfo pps_info;
      pps_info.type = H264::NaluType::kPps;
      pps_info.sps_id = sps->first;
      pps_info.pps_id = pps->first;
      if (h264_header.nalus_length + 2 <= kMaxNalusPerPacket) {
        h264_header.nalus[h264_header.nalus_length++] = sps_info;
        h264_header.nalus[h264_header.nalus_length++] = pps_info;
      } else {
        RTC_LOG(LS_WARNING)
            << "Not enough space in H.264 codec header to insert "
               "SPS/PPS provided out-of-band.";
      }
    }
  }

  // Insert start code.
  switch (h264_header.packetization_type) {
    case kH264StapA: {
      const uint8_t* payload_end =
          packet.video_payload.data() + packet.video_payload.size();
      const uint8_t* nalu_ptr = packet.video_payload.data() + 1;
      while (nalu_ptr < payload_end - 1) {
        // The first two bytes describe the length of the segment, where a
        // segment is the nalu type plus nalu payload.
        uint16_t segment_length = nalu_ptr[0] << 8 | nalu_ptr[1];
        nalu_ptr += 2;

        if (nalu_ptr + segment_length <= payload_end) {
          result.AppendData(kStartCode);
          result.AppendData(nalu_ptr, segment_length);
        }
        nalu_ptr += segment_length;
      }
      packet.video_payload = result;
      return true;
    }

    case kH264FuA: {
      if (IsFirstPacketOfFragment(h264_header)) {
        result.AppendData(kStartCode);
      }
      result.AppendData(packet.video_payload);
      packet.video_payload = result;
      return true;
    }

    case kH264SingleNalu: {
      result.AppendData(kStartCode);
      result.AppendData(packet.video_payload);
      packet.video_payload = result;
      return true;
    }
  }

  RTC_DCHECK_NOTREACHED();
  return false;
}

}  // namespace webrtc
