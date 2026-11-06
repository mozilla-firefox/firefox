/*
 *  Copyright (c) 2023 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "common_video/h265/h265_bitstream_parser.h"

#include <stdlib.h>

#include <cstdint>
#include <limits>
#include <vector>

#include "common_video/h265/h265_common.h"
#include "rtc_base/bit_buffer.h"
#include "rtc_base/bitstream_reader.h"
#include "rtc_base/logging.h"

#define IN_RANGE_OR_RETURN(val, min, max)                                     \
  do {                                                                        \
    if (!slice_reader.Ok() || (val) < (min) || (val) > (max)) {               \
      RTC_LOG(LS_WARNING) << "Error in stream: invalid value, expected " #val \
                             " to be"                                         \
                          << " in range [" << (min) << ":" << (max) << "]"    \
                          << " found " << (val) << " instead";                \
      return kInvalidStream;                                                  \
    }                                                                         \
  } while (0)

#define IN_RANGE_OR_RETURN_NULL(val, min, max)                                \
  do {                                                                        \
    if (!slice_reader.Ok() || (val) < (min) || (val) > (max)) {               \
      RTC_LOG(LS_WARNING) << "Error in stream: invalid value, expected " #val \
                             " to be"                                         \
                          << " in range [" << (min) << ":" << (max) << "]"    \
                          << " found " << (val) << " instead";                \
      return absl::nullopt;                                                   \
    }                                                                         \
  } while (0)

#define IN_RANGE_OR_RETURN_VOID(val, min, max)                                \
  do {                                                                        \
    if (!slice_reader.Ok() || (val) < (min) || (val) > (max)) {               \
      RTC_LOG(LS_WARNING) << "Error in stream: invalid value, expected " #val \
                             " to be"                                         \
                          << " in range [" << (min) << ":" << (max) << "]"    \
                          << " found " << (val) << " instead";                \
      return;                                                                 \
    }                                                                         \
  } while (0)

#define TRUE_OR_RETURN(a)                                                \
  do {                                                                   \
    if (!slice_reader.Ok() || !(a)) {                                    \
      RTC_LOG(LS_WARNING) << "Error in stream: invalid value, expected " \
                          << #a;                                         \
      return kInvalidStream;                                             \
    }                                                                    \
  } while (0)

namespace {

constexpr int kMaxAbsQpDeltaValue = 51;
constexpr int kMinQpValue = 0;
constexpr int kMaxQpValue = 51;
constexpr int kMaxRefIdxActive = 15;

}  // namespace

namespace webrtc {

H265BitstreamParser::H265BitstreamParser() = default;
H265BitstreamParser::~H265BitstreamParser() = default;

// General note: this is based off the 08/2021 version of the H.265 standard,
// section 7.3.6.1. You can find it on this page:
// http://www.itu.int/rec/T-REC-H.265
H265BitstreamParser::Result H265BitstreamParser::ParseNonParameterSetNalu(
    const uint8_t* source,
    size_t source_length,
    uint8_t nalu_type) {
  last_slice_qp_delta_ = absl::nullopt;
  last_slice_pps_id_ = absl::nullopt;
  const std::vector<uint8_t> slice_rbsp =
      H265::ParseRbsp(source, source_length);
  if (slice_rbsp.size() < H265::kNaluHeaderSize)
    return kInvalidStream;

  BitstreamReader slice_reader(slice_rbsp);
  slice_reader.ConsumeBits(H265::kNaluHeaderSize * 8);

  // first_slice_segment_in_pic_flag: u(1)
  bool first_slice_segment_in_pic_flag = slice_reader.Read<bool>();
  bool irap_pic = (H265::NaluType::kBlaWLp <= nalu_type &&
                   nalu_type <= H265::NaluType::kRsvIrapVcl23);
  if (irap_pic) {
    // no_output_of_prior_pics_flag: u(1)
    slice_reader.ConsumeBits(1);
  }
  // slice_pic_parameter_set_id: ue(v)
  uint32_t pps_id = slice_reader.ReadExponentialGolomb();
  IN_RANGE_OR_RETURN(pps_id, 0, 63);
  const H265PpsParser::PpsState* pps = GetPPS(pps_id);
  TRUE_OR_RETURN(pps);
  const H265SpsParser::SpsState* sps = GetSPS(pps->sps_id);
  TRUE_OR_RETURN(sps);
  bool dependent_slice_segment_flag = 0;
  if (!first_slice_segment_in_pic_flag) {
    if (pps->dependent_slice_segments_enabled_flag) {
      // dependent_slice_segment_flag: u(1)
      dependent_slice_segment_flag = slice_reader.Read<bool>();
    }

    // slice_segment_address: u(v)
    int32_t log2_ctb_size_y = sps->log2_min_luma_coding_block_size_minus3 + 3 +
                              sps->log2_diff_max_min_luma_coding_block_size;
    uint32_t ctb_size_y = 1 << log2_ctb_size_y;
    uint32_t pic_width_in_ctbs_y = sps->pic_width_in_luma_samples / ctb_size_y;
    if (sps->pic_width_in_luma_samples % ctb_size_y)
      pic_width_in_ctbs_y++;

    uint32_t pic_height_in_ctbs_y =
        sps->pic_height_in_luma_samples / ctb_size_y;
    if (sps->pic_height_in_luma_samples % ctb_size_y)
      pic_height_in_ctbs_y++;

    uint32_t slice_segment_address_bits =
        H265::Log2Ceiling(pic_height_in_ctbs_y * pic_width_in_ctbs_y);
    TRUE_OR_RETURN(slice_segment_address_bits !=
                   std::numeric_limits<uint32_t>::max());
    slice_reader.ConsumeBits(slice_segment_address_bits);
  }

  if (dependent_slice_segment_flag == 0) {
    for (uint32_t i = 0; i < pps->num_extra_slice_header_bits; i++) {
      // slice_reserved_flag: u(1)
      slice_reader.ConsumeBits(1);
    }
    // slice_type: ue(v)
    uint32_t slice_type = slice_reader.ReadExponentialGolomb();
    IN_RANGE_OR_RETURN(slice_type, 0, 2);
    if (pps->output_flag_present_flag) {
      // pic_output_flag: u(1)
      slice_reader.ConsumeBits(1);
    }
    if (sps->separate_colour_plane_flag) {
      // colour_plane_id: u(2)
      slice_reader.ConsumeBits(2);
    }
    uint32_t num_long_term_sps = 0;
    uint32_t num_long_term_pics = 0;
    std::vector<bool> used_by_curr_pic_lt_flag;
    bool short_term_ref_pic_set_sps_flag = false;
    uint32_t short_term_ref_pic_set_idx = 0;
    H265SpsParser::ShortTermRefPicSet short_term_ref_pic_set;
    bool slice_temporal_mvp_enabled_flag = 0;
    if (nalu_type != H265::NaluType::kIdrWRadl &&
        nalu_type != H265::NaluType::kIdrNLp) {
      // slice_pic_order_cnt_lsb: u(v)
      uint32_t slice_pic_order_cnt_lsb_bits =
          sps->log2_max_pic_order_cnt_lsb_minus4 + 4;
      slice_reader.ConsumeBits(slice_pic_order_cnt_lsb_bits);
      // short_term_ref_pic_set_sps_flag: u(1)
      short_term_ref_pic_set_sps_flag = slice_reader.Read<bool>();
      if (!short_term_ref_pic_set_sps_flag) {
        absl::optional<H265SpsParser::ShortTermRefPicSet> ref_pic_set =
            H265SpsParser::ParseShortTermRefPicSet(
                sps->num_short_term_ref_pic_sets,
                sps->num_short_term_ref_pic_sets, sps->short_term_ref_pic_set,
                sps->sps_max_dec_pic_buffering_minus1
                    [sps->sps_max_sub_layers_minus1],
                slice_reader);
        TRUE_OR_RETURN(ref_pic_set);
        short_term_ref_pic_set = *ref_pic_set;

      } else if (sps->num_short_term_ref_pic_sets > 1) {
        // short_term_ref_pic_set_idx: u(v)
        uint32_t short_term_ref_pic_set_idx_bits =
            H265::Log2Ceiling(sps->num_short_term_ref_pic_sets);
        if ((1 << short_term_ref_pic_set_idx_bits) <
            sps->num_short_term_ref_pic_sets) {
          short_term_ref_pic_set_idx_bits++;
        }
        if (short_term_ref_pic_set_idx_bits > 0) {
          short_term_ref_pic_set_idx =
              slice_reader.ReadBits(short_term_ref_pic_set_idx_bits);
          IN_RANGE_OR_RETURN(short_term_ref_pic_set_idx, 0,
                             sps->num_short_term_ref_pic_sets - 1);
        }
      }
      if (sps->long_term_ref_pics_present_flag) {
        if (sps->num_long_term_ref_pics_sps > 0) {
          // num_long_term_sps: ue(v)
          num_long_term_sps = slice_reader.ReadExponentialGolomb();
          IN_RANGE_OR_RETURN(num_long_term_sps, 0,
                             sps->num_long_term_ref_pics_sps);
        }
        // num_long_term_pics: ue(v)
        num_long_term_pics = slice_reader.ReadExponentialGolomb();
        IN_RANGE_OR_RETURN(num_long_term_pics, 0,
                           kMaxLongTermRefPicSets - num_long_term_sps);
        used_by_curr_pic_lt_flag.resize(num_long_term_sps + num_long_term_pics,
                                        0);
        for (uint32_t i = 0; i < num_long_term_sps + num_long_term_pics; i++) {
          if (i < num_long_term_sps) {
            uint32_t lt_idx_sps = 0;
            if (sps->num_long_term_ref_pics_sps > 1) {
              // lt_idx_sps: u(v)
              uint32_t lt_idx_sps_bits =
                  H265::Log2Ceiling(sps->num_long_term_ref_pics_sps);
              lt_idx_sps = slice_reader.ReadBits(lt_idx_sps_bits);
              IN_RANGE_OR_RETURN(lt_idx_sps, 0,
                                 sps->num_long_term_ref_pics_sps - 1);
            }
            used_by_curr_pic_lt_flag[i] =
                sps->used_by_curr_pic_lt_sps_flag[lt_idx_sps];
          } else {
            // poc_lsb_lt: u(v)
            uint32_t poc_lsb_lt_bits =
                sps->log2_max_pic_order_cnt_lsb_minus4 + 4;
            slice_reader.ConsumeBits(poc_lsb_lt_bits);
            // used_by_curr_pic_lt_flag: u(1)
            used_by_curr_pic_lt_flag[i] = slice_reader.Read<bool>();
          }
          // delta_poc_msb_present_flag: u(1)
          bool delta_poc_msb_present_flag = slice_reader.Read<bool>();
          if (delta_poc_msb_present_flag) {
            // delta_poc_msb_cycle_lt: ue(v)
            int delta_poc_msb_cycle_lt = slice_reader.ReadExponentialGolomb();
            IN_RANGE_OR_RETURN(
                delta_poc_msb_cycle_lt, 0,
                std::pow(2, 32 - sps->log2_max_pic_order_cnt_lsb_minus4 - 4));
          }
        }
      }
      if (sps->sps_temporal_mvp_enabled_flag) {
        // slice_temporal_mvp_enabled_flag: u(1)
        slice_temporal_mvp_enabled_flag = slice_reader.Read<bool>();
      }
    }

    if (sps->sample_adaptive_offset_enabled_flag) {
      // slice_sao_luma_flag: u(1)
      slice_reader.ConsumeBits(1);
      uint32_t chroma_array_type =
          sps->separate_colour_plane_flag == 0 ? sps->chroma_format_idc : 0;
      if (chroma_array_type != 0) {
        // slice_sao_chroma_flag: u(1)
        slice_reader.ConsumeBits(1);
      }
    }

    if (slice_type == H265::SliceType::kP ||
        slice_type == H265::SliceType::kB) {
      // num_ref_idx_active_override_flag: u(1)
      bool num_ref_idx_active_override_flag = slice_reader.Read<bool>();
      uint32_t num_ref_idx_l0_active_minus1 =
          pps->num_ref_idx_l0_default_active_minus1;
      uint32_t num_ref_idx_l1_active_minus1 =
          pps->num_ref_idx_l1_default_active_minus1;
      if (num_ref_idx_active_override_flag) {
        // num_ref_idx_l0_active_minus1: ue(v)
        num_ref_idx_l0_active_minus1 = slice_reader.ReadExponentialGolomb();
        IN_RANGE_OR_RETURN(num_ref_idx_l0_active_minus1, 0,
                           kMaxRefIdxActive - 1);
        if (slice_type == H265::SliceType::kB) {
          // num_ref_idx_l1_active_minus1: ue(v)
          num_ref_idx_l1_active_minus1 = slice_reader.ReadExponentialGolomb();
          IN_RANGE_OR_RETURN(num_ref_idx_l1_active_minus1, 0,
                             kMaxRefIdxActive - 1);
        }
      }

      uint32_t num_pic_total_curr = 0;
      uint32_t curr_sps_idx = 0;
      if (short_term_ref_pic_set_sps_flag) {
        curr_sps_idx = short_term_ref_pic_set_idx;
      } else {
        curr_sps_idx = sps->num_short_term_ref_pic_sets;
      }
      if (sps->short_term_ref_pic_set.size() <= curr_sps_idx) {
        TRUE_OR_RETURN(!(curr_sps_idx != 0 || short_term_ref_pic_set_sps_flag));
      }
      const H265SpsParser::ShortTermRefPicSet* ref_pic_set;
      if (curr_sps_idx < sps->short_term_ref_pic_set.size()) {
        ref_pic_set = &(sps->short_term_ref_pic_set[curr_sps_idx]);
      } else {
        ref_pic_set = &short_term_ref_pic_set;
      }

      // Equation 7-57
      IN_RANGE_OR_RETURN(ref_pic_set->num_negative_pics, 0,
                         kMaxShortTermRefPicSets);
      IN_RANGE_OR_RETURN(ref_pic_set->num_positive_pics, 0,
                         kMaxShortTermRefPicSets);
      for (uint32_t i = 0; i < ref_pic_set->num_negative_pics; i++) {
        if (ref_pic_set->used_by_curr_pic_s0[i]) {
          num_pic_total_curr++;
        }
      }
      for (uint32_t i = 0; i < ref_pic_set->num_positive_pics; i++) {
        if (ref_pic_set->used_by_curr_pic_s1[i]) {
          num_pic_total_curr++;
        }
      }
      for (uint32_t i = 0; i < num_long_term_sps + num_long_term_pics; i++) {
        if (used_by_curr_pic_lt_flag[i]) {
          num_pic_total_curr++;
        }
      }

      if (pps->lists_modification_present_flag && num_pic_total_curr > 1) {
        // ref_pic_lists_modification()
        uint32_t list_entry_bits = H265::Log2Ceiling(num_pic_total_curr);
        if ((1 << list_entry_bits) < num_pic_total_curr) {
          list_entry_bits++;
        }
        // ref_pic_list_modification_flag_l0: u(1)
        bool ref_pic_list_modification_flag_l0 = slice_reader.Read<bool>();
        if (ref_pic_list_modification_flag_l0) {
          for (uint32_t i = 0; i < num_ref_idx_l0_active_minus1; i++) {
            // list_entry_l0: u(v)
            slice_reader.ConsumeBits(list_entry_bits);
          }
        }
        if (slice_type == H265::SliceType::kB) {
          // ref_pic_list_modification_flag_l1: u(1)
          bool ref_pic_list_modification_flag_l1 = slice_reader.Read<bool>();
          if (ref_pic_list_modification_flag_l1) {
            for (uint32_t i = 0; i < num_ref_idx_l1_active_minus1; i++) {
              // list_entry_l1: u(v)
              slice_reader.ConsumeBits(list_entry_bits);
            }
          }
        }
      }
      if (slice_type == H265::SliceType::kB) {
        // mvd_l1_zero_flag: u(1)
        slice_reader.ConsumeBits(1);
      }
      if (pps->cabac_init_present_flag) {
        // cabac_init_flag: u(1)
        slice_reader.ConsumeBits(1);
      }
      if (slice_temporal_mvp_enabled_flag) {
        bool collocated_from_l0_flag = false;
        if (slice_type == H265::SliceType::kB) {
          // collocated_from_l0_flag: u(1)
          collocated_from_l0_flag = slice_reader.Read<bool>();
        }
        if ((collocated_from_l0_flag && num_ref_idx_l0_active_minus1 > 0) ||
            (!collocated_from_l0_flag && num_ref_idx_l1_active_minus1 > 0)) {
          // collocated_ref_idx: ue(v)
          uint32_t collocated_ref_idx = slice_reader.ReadExponentialGolomb();
          if ((slice_type == H265::SliceType::kP ||
               slice_type == H265::SliceType::kB) &&
              collocated_from_l0_flag) {
            IN_RANGE_OR_RETURN(collocated_ref_idx, 0,
                               num_ref_idx_l0_active_minus1);
          }
          if (slice_type == H265::SliceType::kB && !collocated_from_l0_flag) {
            IN_RANGE_OR_RETURN(collocated_ref_idx, 0,
                               num_ref_idx_l1_active_minus1);
          }
        }
      }
      if (!slice_reader.Ok() ||
          ((pps->weighted_pred_flag && slice_type == H265::SliceType::kP) ||
           (pps->weighted_bipred_flag && slice_type == H265::SliceType::kB))) {
        // pred_weight_table()
        RTC_LOG(LS_ERROR) << "Streams with pred_weight_table unsupported.";
        return kUnsupportedStream;
      }
      // five_minus_max_num_merge_cand: ue(v)
      uint32_t five_minus_max_num_merge_cand =
          slice_reader.ReadExponentialGolomb();
      IN_RANGE_OR_RETURN(5 - five_minus_max_num_merge_cand, 1, 5);
    }
  }

  // slice_qp_delta: se(v)
  int32_t last_slice_qp_delta = slice_reader.ReadSignedExponentialGolomb();
  if (!slice_reader.Ok() || (abs(last_slice_qp_delta) > kMaxAbsQpDeltaValue)) {
    // Something has gone wrong, and the parsed value is invalid.
    RTC_LOG(LS_ERROR) << "Parsed QP value out of range.";
    return kInvalidStream;
  }
  // 7-54 in H265 spec.
  IN_RANGE_OR_RETURN(26 + pps->init_qp_minus26 + last_slice_qp_delta,
                     -pps->qp_bd_offset_y, 51);

  last_slice_qp_delta_ = last_slice_qp_delta;
  last_slice_pps_id_ = pps_id;
  if (!slice_reader.Ok()) {
    return kInvalidStream;
  }

  return kOk;
}

const H265PpsParser::PpsState* H265BitstreamParser::GetPPS(uint32_t id) const {
  auto it = pps_.find(id);
  if (it == pps_.end()) {
    RTC_LOG(LS_WARNING) << "Requested a nonexistent PPS id " << id;
    return nullptr;
  }

  return &it->second;
}

const H265SpsParser::SpsState* H265BitstreamParser::GetSPS(uint32_t id) const {
  auto it = sps_.find(id);
  if (it == sps_.end()) {
    RTC_LOG(LS_WARNING) << "Requested a nonexistent SPS id " << id;
    return nullptr;
  }

  return &it->second;
}

void H265BitstreamParser::ParseSlice(const uint8_t* slice, size_t length) {
  H265::NaluType nalu_type = H265::ParseNaluType(slice[0]);
  switch (nalu_type) {
    case H265::NaluType::kVps: {
      absl::optional<H265VpsParser::VpsState> vps_state;
      if (length >= H265::kNaluHeaderSize) {
        vps_state = H265VpsParser::ParseVps(slice + H265::kNaluHeaderSize,
                                            length - H265::kNaluHeaderSize);
      }

      if (!vps_state) {
        RTC_LOG(LS_WARNING) << "Unable to parse VPS from H265 bitstream.";
      } else {
        vps_[vps_state->id] = *vps_state;
      }
      break;
    }
    case H265::NaluType::kSps: {
      absl::optional<H265SpsParser::SpsState> sps_state;
      if (length >= H265::kNaluHeaderSize) {
        sps_state = H265SpsParser::ParseSps(slice + H265::kNaluHeaderSize,
                                            length - H265::kNaluHeaderSize);
      }
      if (!sps_state) {
        RTC_LOG(LS_WARNING) << "Unable to parse SPS from H265 bitstream.";
      } else {
        sps_[sps_state->sps_id] = *sps_state;
      }
      break;
    }
    case H265::NaluType::kPps: {
      absl::optional<H265PpsParser::PpsState> pps_state;
      if (length >= H265::kNaluHeaderSize) {
        std::vector<uint8_t> unpacked_buffer = H265::ParseRbsp(
            slice + H265::kNaluHeaderSize, length - H265::kNaluHeaderSize);
        BitstreamReader slice_reader(unpacked_buffer);
        // pic_parameter_set_id: ue(v)
        uint32_t pps_id = slice_reader.ReadExponentialGolomb();
        IN_RANGE_OR_RETURN_VOID(pps_id, 0, 63);
        // seq_parameter_set_id: ue(v)
        uint32_t sps_id = slice_reader.ReadExponentialGolomb();
        IN_RANGE_OR_RETURN_VOID(sps_id, 0, 15);
        const H265SpsParser::SpsState* sps = GetSPS(sps_id);
        pps_state = H265PpsParser::ParsePps(
            slice + H265::kNaluHeaderSize, length - H265::kNaluHeaderSize, sps);
      }
      if (!pps_state) {
        RTC_LOG(LS_WARNING) << "Unable to parse PPS from H265 bitstream.";
      } else {
        pps_[pps_state->pps_id] = *pps_state;
      }
      break;
    }
    case H265::NaluType::kAud:
    case H265::NaluType::kPrefixSei:
    case H265::NaluType::kSuffixSei:
    case H265::NaluType::kAp:
    case H265::NaluType::kFu:
      break;
    default:
      Result res = ParseNonParameterSetNalu(slice, length, nalu_type);
      if (res != kOk) {
        RTC_LOG(LS_INFO) << "Failed to parse bitstream. Error: " << res;
      }
      break;
  }
}

absl::optional<uint32_t>
H265BitstreamParser::ParsePpsIdFromSliceSegmentLayerRbsp(const uint8_t* data,
                                                         size_t length,
                                                         uint8_t nalu_type) {
  std::vector<uint8_t> unpacked_buffer = H265::ParseRbsp(data, length);
  BitstreamReader slice_reader(unpacked_buffer);

  // first_slice_segment_in_pic_flag: u(1)
  slice_reader.ConsumeBits(1);
  if (!slice_reader.Ok()) {
    return absl::nullopt;
  }

  if (nalu_type >= H265::NaluType::kBlaWLp &&
      nalu_type <= H265::NaluType::kRsvIrapVcl23) {
    // no_output_of_prior_pics_flag: u(1)
    slice_reader.ConsumeBits(1);
  }

  // slice_pic_parameter_set_id: ue(v)
  uint32_t slice_pic_parameter_set_id = slice_reader.ReadExponentialGolomb();
  IN_RANGE_OR_RETURN_NULL(slice_pic_parameter_set_id, 0, 63);
  if (!slice_reader.Ok()) {
    return absl::nullopt;
  }

  return slice_pic_parameter_set_id;
}

void H265BitstreamParser::ParseBitstream(
    rtc::ArrayView<const uint8_t> bitstream) {
  std::vector<H265::NaluIndex> nalu_indices =
      H265::FindNaluIndices(bitstream.data(), bitstream.size());
  for (const H265::NaluIndex& index : nalu_indices)
    ParseSlice(&bitstream[index.payload_start_offset], index.payload_size);
}

absl::optional<int> H265BitstreamParser::GetLastSliceQp() const {
  if (!last_slice_qp_delta_ || !last_slice_pps_id_) {
    return absl::nullopt;
  }
  const H265PpsParser::PpsState* pps = GetPPS(last_slice_pps_id_.value());
  if (!pps)
    return absl::nullopt;
  const int parsed_qp = 26 + pps->init_qp_minus26 + *last_slice_qp_delta_;
  if (parsed_qp < kMinQpValue || parsed_qp > kMaxQpValue) {
    RTC_LOG(LS_ERROR) << "Parsed invalid QP from bitstream.";
    return absl::nullopt;
  }
  return parsed_qp;
}

absl::optional<uint32_t> H265BitstreamParser::GetLastSlicePpsId() const {
  if (!last_slice_pps_id_) {
    RTC_LOG(LS_ERROR) << "Failed to parse PPS id from bitstream.";
    return absl::nullopt;
  }

  return last_slice_pps_id_;
}

}  // namespace webrtc
