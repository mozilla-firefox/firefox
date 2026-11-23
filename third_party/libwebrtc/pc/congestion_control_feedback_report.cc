/*
 *  Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/congestion_control_feedback_report.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

#include "modules/rtp_rtcp/source/byte_io.h"
#include "pc/congestion_control_feedback_block.h"
#include "rtc_base/checks.h"

namespace webrtc {
namespace {

constexpr size_t kFeedbackCountOffset = 0;
constexpr size_t kNumReportBlocksOffset = 1;
constexpr size_t kReportTimestampOffset = 3;
constexpr size_t kBeginSeqOffset = 7;

}  // namespace

std::optional<CongestionControlFeedbackReport>
CongestionControlFeedbackReport::Decode(std::span<const uint8_t> data,
                                        int ato_ticks_per_second) {
  if (data.size() < kHeaderBytes) {
    return std::nullopt;
  }
  const uint16_t num_report_blocks =
      ByteReader<uint16_t>::ReadBigEndian(&data[kNumReportBlocksOffset]);
  if (data.size() < SizeBytes(num_report_blocks)) {
    return std::nullopt;
  }

  CongestionControlFeedbackReport report;
  report.feedback_count = data[kFeedbackCountOffset];
  report.report_timestamp =
      ByteReader<uint32_t>::ReadBigEndian(&data[kReportTimestampOffset]);
  report.begin_seq =
      ByteReader<uint16_t>::ReadBigEndian(&data[kBeginSeqOffset]);
  report.blocks.reserve(num_report_blocks);
  for (size_t i = 0; i < num_report_blocks; ++i) {
    report.blocks.push_back(CongestionControlFeedbackBlock::Decode(
        ByteReader<uint16_t>::ReadBigEndian(
            &data[kHeaderBytes + i * CongestionControlFeedbackBlock::kBytes]),
        ato_ticks_per_second));
  }
  return report;
}

void CongestionControlFeedbackReport::Encode(std::span<uint8_t> destination,
                                             int ato_ticks_per_second) const {
  RTC_DCHECK_GE(destination.size(), SizeBytes());
  RTC_DCHECK_LE(blocks.size(), std::numeric_limits<uint16_t>::max());

  destination[kFeedbackCountOffset] = feedback_count;
  ByteWriter<uint16_t>::WriteBigEndian(&destination[kNumReportBlocksOffset],
                                       static_cast<uint16_t>(blocks.size()));
  ByteWriter<uint32_t>::WriteBigEndian(&destination[kReportTimestampOffset],
                                       report_timestamp);
  ByteWriter<uint16_t>::WriteBigEndian(&destination[kBeginSeqOffset],
                                       begin_seq);
  for (size_t i = 0; i < blocks.size(); ++i) {
    ByteWriter<uint16_t>::WriteBigEndian(
        &destination[kHeaderBytes + i * CongestionControlFeedbackBlock::kBytes],
        blocks[i].Encode(ato_ticks_per_second));
  }
}

}  // namespace webrtc
