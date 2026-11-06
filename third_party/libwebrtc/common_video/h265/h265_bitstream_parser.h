/*
 *  Copyright (c) 2023 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef COMMON_VIDEO_H265_H265_BITSTREAM_PARSER_H_
#define COMMON_VIDEO_H265_H265_BITSTREAM_PARSER_H_

#include <stddef.h>
#include <stdint.h>

#include <vector>

#include "absl/types/optional.h"
#include "api/video_codecs/bitstream_parser.h"
#include "common_video/h265/h265_pps_parser.h"
#include "common_video/h265/h265_sps_parser.h"
#include "common_video/h265/h265_vps_parser.h"
#include "rtc_base/containers/flat_map.h"
#include "rtc_base/system/rtc_export.h"

namespace webrtc {

// Stateful H265 bitstream parser (due to VPS/SPS/PPS). Used to parse out QP
// values from the bitstream.
class RTC_EXPORT H265BitstreamParser : public BitstreamParser {
 public:
  H265BitstreamParser();
  ~H265BitstreamParser() override;

  // New interface.
  void ParseBitstream(rtc::ArrayView<const uint8_t> bitstream) override;
  absl::optional<int> GetLastSliceQp() const override;

  absl::optional<uint32_t> GetLastSlicePpsId() const;

  static absl::optional<uint32_t> ParsePpsIdFromSliceSegmentLayerRbsp(
      const uint8_t* data,
      size_t length,
      uint8_t nalu_type);

 protected:
  enum Result {
    kOk,
    kInvalidStream,
    kUnsupportedStream,
  };
  void ParseSlice(const uint8_t* slice, size_t length);
  Result ParseNonParameterSetNalu(const uint8_t* source,
                                  size_t source_length,
                                  uint8_t nalu_type);

  const H265PpsParser::PpsState* GetPPS(uint32_t id) const;
  const H265SpsParser::SpsState* GetSPS(uint32_t id) const;

  // VPS/SPS/PPS state, updated when parsing new VPS/SPS/PPS, used to parse
  // slices.
  flat_map<uint32_t, H265VpsParser::VpsState> vps_;
  flat_map<uint32_t, H265SpsParser::SpsState> sps_;
  flat_map<uint32_t, H265PpsParser::PpsState> pps_;

  // Last parsed slice QP.
  absl::optional<int32_t> last_slice_qp_delta_;
  absl::optional<uint32_t> last_slice_pps_id_;
};

}  // namespace webrtc

#endif  // COMMON_VIDEO_H265_H265_BITSTREAM_PARSER_H_
