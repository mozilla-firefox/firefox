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
#include <utility>

#include "api/transport/ecn_marking.h"
#include "api/units/time_delta.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

// The wire codes RFC 8888 requires, spelled out so that renumbering EcnMarking
// cannot silently change the wire format.
constexpr std::pair<EcnMarking, uint16_t> kEcnWireCodes[] = {
    {EcnMarking::kNotEct, 0b00},
    {EcnMarking::kEct1, 0b01},
    {EcnMarking::kEct0, 0b10},
    {EcnMarking::kCe, 0b11},
};

// A tick rate other than the default, to keep the parameter exercised.
constexpr int kAltAtoTicksPerSecond = 4096;

constexpr uint16_t kReceivedFlag = 0x8000;
constexpr uint16_t kAtoMask = 0x1FFF;
constexpr int kEcnShift = 13;
constexpr uint16_t kAtoOverRange = 0x1FFE;
constexpr uint16_t kAtoUnavailable = 0x1FFF;

TEST(CongestionControlFeedbackBlockTest, NotReceivedEncodesToZero) {
  EXPECT_EQ(CongestionControlFeedbackBlock().Encode(), 0);
}

TEST(CongestionControlFeedbackBlockTest,
     ClearedReceivedBitDecodesAsNotReceived) {
  CongestionControlFeedbackBlock block =
      CongestionControlFeedbackBlock::Decode(0x7FFF);

  EXPECT_FALSE(block.received);
  EXPECT_EQ(block.ecn, EcnMarking::kNotEct);
}

TEST(CongestionControlFeedbackBlockTest, EncodesArrivalTimeOffsetInTicks) {
  CongestionControlFeedbackBlock block = {
      .received = true, .arrival_time_offset = TimeDelta::Millis(500)};

  EXPECT_EQ(block.Encode() & kAtoMask, 512);
}

TEST(CongestionControlFeedbackBlockTest, DecodesOffsetToTheNearestMicrosecond) {
  // One tick at the default rate is 976.5625 us.
  EXPECT_EQ(CongestionControlFeedbackBlock::Decode(kReceivedFlag | 1)
                .arrival_time_offset,
            TimeDelta::Micros(977));
}

TEST(CongestionControlFeedbackBlockTest, EncodesZeroOffset) {
  CongestionControlFeedbackBlock block = {.received = true};

  EXPECT_EQ(block.Encode(), kReceivedFlag);
}

TEST(CongestionControlFeedbackBlockTest, EncodesLargestRepresentableOffset) {
  CongestionControlFeedbackBlock block = {
      .received = true,
      .arrival_time_offset =
          TimeDelta::Seconds(CongestionControlFeedbackBlock::kMaxAtoTicks) /
          CongestionControlFeedbackBlock::kDefaultAtoTicksPerSecond};

  EXPECT_EQ(block.Encode() & kAtoMask,
            CongestionControlFeedbackBlock::kMaxAtoTicks);
}

TEST(CongestionControlFeedbackBlockTest, EncodesTooLargeOffsetAsOverRange) {
  CongestionControlFeedbackBlock block = {
      .received = true, .arrival_time_offset = TimeDelta::Seconds(9)};

  EXPECT_EQ(block.Encode() & kAtoMask, kAtoOverRange);
  EXPECT_TRUE(CongestionControlFeedbackBlock::Decode(block.Encode())
                  .arrival_time_offset.IsPlusInfinity());
}

TEST(CongestionControlFeedbackBlockTest,
     EncodesArrivalAfterReportAsUnavailable) {
  CongestionControlFeedbackBlock block = {
      .received = true, .arrival_time_offset = TimeDelta::Millis(-1)};

  EXPECT_EQ(block.Encode() & kAtoMask, kAtoUnavailable);
  EXPECT_TRUE(CongestionControlFeedbackBlock::Decode(block.Encode())
                  .arrival_time_offset.IsMinusInfinity());
}

TEST(CongestionControlFeedbackBlockTest, RoundTripsEveryAtoAndEcnValue) {
  for (int ato_ticks_per_second :
       {CongestionControlFeedbackBlock::kDefaultAtoTicksPerSecond,
        kAltAtoTicksPerSecond}) {
    for (const auto& [marking, wire_code] : kEcnWireCodes) {
      for (uint16_t ato = 0; ato <= kAtoMask; ++ato) {
        const uint16_t wire = kReceivedFlag | (wire_code << kEcnShift) | ato;

        CongestionControlFeedbackBlock block =
            CongestionControlFeedbackBlock::Decode(wire, ato_ticks_per_second);

        EXPECT_TRUE(block.received);
        EXPECT_EQ(block.ecn, marking);
        EXPECT_EQ(block.Encode(ato_ticks_per_second), wire)
            << "ato=" << ato
            << " ato_ticks_per_second=" << ato_ticks_per_second;
      }
    }
  }
}

TEST(CongestionControlFeedbackBlockTest, TickRateScalesTheOffset) {
  CongestionControlFeedbackBlock block = {
      .received = true, .arrival_time_offset = TimeDelta::Seconds(1)};

  EXPECT_EQ(
      block.Encode(CongestionControlFeedbackBlock::kDefaultAtoTicksPerSecond) &
          kAtoMask,
      CongestionControlFeedbackBlock::kDefaultAtoTicksPerSecond);
  EXPECT_EQ(block.Encode(kAltAtoTicksPerSecond) & kAtoMask,
            kAltAtoTicksPerSecond);
}

}  // namespace
}  // namespace webrtc
