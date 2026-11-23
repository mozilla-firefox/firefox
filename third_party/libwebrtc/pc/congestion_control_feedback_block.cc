/*
 *  Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/congestion_control_feedback_block.h"

#include <cstdint>

#include "api/transport/ecn_marking.h"
#include "api/units/time_delta.h"
#include "rtc_base/checks.h"
#include "rtc_base/numerics/divide_round.h"

namespace webrtc {
namespace {

constexpr uint16_t kReceivedFlag = 0x8000;
constexpr int kEcnShift = 13;
constexpr uint16_t kAtoMask = 0x1FFF;

// Reserved ATO codes, just above the largest measurement.
constexpr uint16_t kAtoOverRange = 0x1FFE;
constexpr uint16_t kAtoUnavailable = 0x1FFF;

constexpr int64_t kMicrosPerSecond = 1'000'000;

// RFC 3168, Section 5.
constexpr uint16_t kEcnEct1 = 0x01;
constexpr uint16_t kEcnEct0 = 0x02;
constexpr uint16_t kEcnCe = 0x03;

// Rounds to the nearest microsecond so every tick value survives a round trip.
TimeDelta TicksToTimeDelta(int64_t ticks, int ato_ticks_per_second) {
  return TimeDelta::Micros(
      DivideRoundToNearest(ticks * kMicrosPerSecond, ato_ticks_per_second));
}

// Measurements greater than this are over-range. Also bounds the scaling
// below, whose input may be TimeDelta::PlusInfinity().
TimeDelta MaxAtoOffset(int ato_ticks_per_second) {
  return TicksToTimeDelta(CongestionControlFeedbackBlock::kMaxAtoTicks,
                          ato_ticks_per_second);
}

uint16_t To13bitAto(TimeDelta arrival_time_offset, int ato_ticks_per_second) {
  if (arrival_time_offset < TimeDelta::Zero()) {
    return kAtoUnavailable;
  }
  if (arrival_time_offset > MaxAtoOffset(ato_ticks_per_second)) {
    return kAtoOverRange;
  }
  return static_cast<uint16_t>(DivideRoundToNearest(
      arrival_time_offset.us() * ato_ticks_per_second, kMicrosPerSecond));
}

TimeDelta AtoToTimeDelta(uint16_t block, int ato_ticks_per_second) {
  const uint16_t ato = block & kAtoMask;
  switch (ato) {
    case kAtoOverRange:
      return TimeDelta::PlusInfinity();
    case kAtoUnavailable:
      return TimeDelta::MinusInfinity();
    default:
      return TicksToTimeDelta(ato, ato_ticks_per_second);
  }
}

uint16_t To2BitEcn(EcnMarking ecn_marking) {
  switch (ecn_marking) {
    case EcnMarking::kNotEct:
      return 0;
    case EcnMarking::kEct1:
      return kEcnEct1 << kEcnShift;
    case EcnMarking::kEct0:
      return kEcnEct0 << kEcnShift;
    case EcnMarking::kCe:
      return kEcnCe << kEcnShift;
  }
  RTC_CHECK_NOTREACHED();
}

EcnMarking ToEcnMarking(uint16_t block) {
  switch ((block >> kEcnShift) & 0b11) {
    case kEcnEct1:
      return EcnMarking::kEct1;
    case kEcnEct0:
      return EcnMarking::kEct0;
    case kEcnCe:
      return EcnMarking::kCe;
    default:
      return EcnMarking::kNotEct;
  }
}

}  // namespace

uint16_t CongestionControlFeedbackBlock::Encode(
    int ato_ticks_per_second) const {
  RTC_DCHECK_GT(ato_ticks_per_second, 0);
  RTC_DCHECK_LE(ato_ticks_per_second, kMicrosPerSecond);
  if (!received) {
    return 0;
  }
  return static_cast<uint16_t>(
      kReceivedFlag | To2BitEcn(ecn) |
      To13bitAto(arrival_time_offset, ato_ticks_per_second));
}

CongestionControlFeedbackBlock CongestionControlFeedbackBlock::Decode(
    uint16_t block,
    int ato_ticks_per_second) {
  RTC_DCHECK_GT(ato_ticks_per_second, 0);
  RTC_DCHECK_LE(ato_ticks_per_second, kMicrosPerSecond);
  if ((block & kReceivedFlag) == 0) {
    return CongestionControlFeedbackBlock();
  }
  return CongestionControlFeedbackBlock{
      .received = true,
      .ecn = ToEcnMarking(block),
      .arrival_time_offset = AtoToTimeDelta(block, ato_ticks_per_second)};
}

}  // namespace webrtc
