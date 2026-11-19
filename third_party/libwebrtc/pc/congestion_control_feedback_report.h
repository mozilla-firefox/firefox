/*
 *  Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef PC_CONGESTION_CONTROL_FEEDBACK_REPORT_H_
#define PC_CONGESTION_CONTROL_FEEDBACK_REPORT_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "pc/congestion_control_feedback_block.h"

namespace webrtc {

// Congestion control feedback for WireProtocol::kDtlsWithFeedback: the report
// of RFC 8888 section 3.1 without the RTCP framing.
//
// Shown below inside a standalone feedback packet. The framing layer owns the
// type byte, and a packet that also carries payload inserts a transport
// sequence number between that byte and feedback_count:
//
//   0                   1                   2                   3
//   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |     type      |feedback_count |       num_report_blocks       |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |                report_timestamp (compact NTP)                 |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |          begin_seq            |       feedback block 0        |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  :                              ...                              :
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
// Block i reports on transport sequence number begin_seq + i, modulo 2^16.
// Arrival time offsets are measured back from report_timestamp.
struct CongestionControlFeedbackReport {
  // feedback_count through begin_seq; the feedback blocks follow.
  static constexpr size_t kHeaderBytes = 9;

  static constexpr size_t SizeBytes(size_t num_report_blocks) {
    return kHeaderBytes +
           num_report_blocks * CongestionControlFeedbackBlock::kBytes;
  }

  // Reads a report from the front of `data` and leaves anything after it: a
  // report is self-delimiting so that application payload can follow. A caller
  // that expects the report to be the whole packet must compare SizeBytes()
  // against the length it was given.
  static std::optional<CongestionControlFeedbackReport> Decode(
      std::span<const uint8_t> data,
      int ato_ticks_per_second =
          CongestionControlFeedbackBlock::kDefaultAtoTicksPerSecond);

  void Encode(
      std::span<uint8_t> destination,
      int ato_ticks_per_second =
          CongestionControlFeedbackBlock::kDefaultAtoTicksPerSecond) const;

  size_t SizeBytes() const { return SizeBytes(blocks.size()); }

  bool operator==(const CongestionControlFeedbackReport& other) const = default;

  uint8_t feedback_count = 0;
  uint32_t report_timestamp = 0;
  uint16_t begin_seq = 0;
  // Encoded as num_report_blocks.
  std::vector<CongestionControlFeedbackBlock> blocks;
};

}  // namespace webrtc

#endif  // PC_CONGESTION_CONTROL_FEEDBACK_REPORT_H_
