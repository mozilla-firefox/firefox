/*
 *  Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef PC_CONGESTION_CONTROL_FEEDBACK_BLOCK_H_
#define PC_CONGESTION_CONTROL_FEEDBACK_BLOCK_H_

#include <cstddef>
#include <cstdint>

#include "api/transport/ecn_marking.h"
#include "api/units/time_delta.h"

namespace webrtc {

// Feedback block inside CongestionControlFeedbackReport (based on RFC 8888):
//
//   0                   1
//   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//  |R|ECN|           ATO           |
//  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
// ATO is the arrival time of the packet, as an offset before the report
// timestamp of the enclosing report. R=0 means the packet was not received,
// in which case the other 15 bits are zero and MUST be ignored.
//
// This is pure bit manipulation: byte order and placement in a report are the
// caller's concern.
struct CongestionControlFeedbackBlock {
  static constexpr size_t kBytes = 2;

  // Ticks per second for the 13-bit ATO field. RFC 8888 puts the field in units
  // of 1/1024 seconds: a precision of ~977 us, and a range whose ceiling is
  // 8189/1024 = ~8.0 s, above which a measurement is over-range.
  //
  // TODO(crbug.com/443019066): consider letting the application choose the
  // value of `ato_ticks_per_second`. Both peers must "agree" on it before it
  // can differ from the default. A higher value improves precision but reduces
  // the largest offset ATO can represent; a lower value does the opposite.
  static constexpr int kDefaultAtoTicksPerSecond = 1024;

  // The largest encodable measurement; the two codes above it are reserved.
  // It is a property of the 13-bit field, so it holds for any tick rate.
  static constexpr uint16_t kMaxAtoTicks = 0x1FFD;

  static CongestionControlFeedbackBlock Decode(
      uint16_t block,
      int ato_ticks_per_second = kDefaultAtoTicksPerSecond);

  uint16_t Encode(int ato_ticks_per_second = kDefaultAtoTicksPerSecond) const;

  bool operator==(const CongestionControlFeedbackBlock& other) const = default;

  // Stored rather than derived from `arrival_time_offset`, because an
  // over-range or unavailable measurement is still a delivered packet.
  bool received = false;
  EcnMarking ecn = EcnMarking::kNotEct;
  // Ignored unless `received`. PlusInfinity encodes an over-range measurement
  // and MinusInfinity an unavailable one; both survive a round trip.
  TimeDelta arrival_time_offset = TimeDelta::Zero();
};

}  // namespace webrtc

#endif  // PC_CONGESTION_CONTROL_FEEDBACK_BLOCK_H_
