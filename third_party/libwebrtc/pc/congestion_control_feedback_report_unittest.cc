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
#include <optional>
#include <span>
#include <vector>

#include "api/transport/ecn_marking.h"
#include "api/units/time_delta.h"
#include "pc/congestion_control_feedback_block.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

using ::testing::AllOf;
using ::testing::Each;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::SizeIs;

constexpr int kAltAtoTicksPerSecond = 4096;

// The most report blocks a standalone feedback packet can carry within a 1200
// byte MTU budget, once the framing layer's type byte is added.
constexpr size_t kMaxStandaloneReportBlocks = 595;

std::vector<uint8_t> ToBytes(
    const CongestionControlFeedbackReport& report,
    int ato_ticks_per_second =
        CongestionControlFeedbackBlock::kDefaultAtoTicksPerSecond) {
  std::vector<uint8_t> bytes(report.SizeBytes());
  report.Encode(bytes, ato_ticks_per_second);
  return bytes;
}

auto IsNotReceived() {
  return Field(&CongestionControlFeedbackBlock::received, false);
}

auto IsReceived(EcnMarking ecn, TimeDelta arrival_time_offset) {
  return AllOf(Field(&CongestionControlFeedbackBlock::received, true),
               Field(&CongestionControlFeedbackBlock::ecn, ecn),
               Field(&CongestionControlFeedbackBlock::arrival_time_offset,
                     arrival_time_offset));
}

TEST(CongestionControlFeedbackReportTest, SizeIsHeaderPlusTwoBytesPerBlock) {
  EXPECT_EQ(CongestionControlFeedbackReport::SizeBytes(0), 9u);
  EXPECT_EQ(CongestionControlFeedbackReport::SizeBytes(1), 11u);
  EXPECT_EQ(
      CongestionControlFeedbackReport::SizeBytes(kMaxStandaloneReportBlocks),
      1199u);
}

TEST(CongestionControlFeedbackReportTest, EncodesFieldsInNetworkByteOrder) {
  CongestionControlFeedbackReport report = {.feedback_count = 0x12,
                                            .report_timestamp = 0xAABBCCDD,
                                            .begin_seq = 0x3456};
  report.blocks = {{.received = true, .ecn = EcnMarking::kCe}};

  EXPECT_THAT(ToBytes(report), ElementsAre(0x12, 0x00, 0x01, 0xAA, 0xBB, 0xCC,
                                           0xDD, 0x34, 0x56, 0xE0, 0x00));
}

TEST(CongestionControlFeedbackReportTest, RoundTripsKeepAliveWithNoBlocks) {
  CongestionControlFeedbackReport report = {
      .feedback_count = 7, .report_timestamp = 0x00010000, .begin_seq = 1234};

  std::optional<CongestionControlFeedbackReport> decoded =
      CongestionControlFeedbackReport::Decode(ToBytes(report));

  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->feedback_count, 7);
  EXPECT_EQ(decoded->report_timestamp, 0x00010000u);
  EXPECT_EQ(decoded->begin_seq, 1234);
  EXPECT_THAT(decoded->blocks, IsEmpty());
}

TEST(CongestionControlFeedbackReportTest,
     RoundTripsBlocksIncludingEcnAndArrivalTime) {
  CongestionControlFeedbackReport report;
  report.blocks = {{.received = true,
                    .ecn = EcnMarking::kEct1,
                    .arrival_time_offset = TimeDelta::Millis(500)},
                   {.received = false},
                   {.received = true, .ecn = EcnMarking::kCe}};

  std::optional<CongestionControlFeedbackReport> decoded =
      CongestionControlFeedbackReport::Decode(ToBytes(report));

  ASSERT_TRUE(decoded.has_value());
  EXPECT_THAT(decoded->blocks,
              ElementsAre(IsReceived(EcnMarking::kEct1, TimeDelta::Millis(500)),
                          IsNotReceived(),
                          IsReceived(EcnMarking::kCe, TimeDelta::Zero())));
}

TEST(CongestionControlFeedbackReportTest, RoundTripsBeginSeqAtWrap) {
  CongestionControlFeedbackReport report = {.begin_seq = 0xFFFF};
  report.blocks.resize(2);

  std::optional<CongestionControlFeedbackReport> decoded =
      CongestionControlFeedbackReport::Decode(ToBytes(report));

  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->begin_seq, 0xFFFF);
}

TEST(CongestionControlFeedbackReportTest, RoundTripsLargestStandaloneReport) {
  CongestionControlFeedbackReport report;
  report.blocks.assign(kMaxStandaloneReportBlocks,
                       {.received = true,
                        .ecn = EcnMarking::kEct0,
                        .arrival_time_offset = TimeDelta::Millis(500)});
  std::vector<uint8_t> bytes = ToBytes(report);
  ASSERT_EQ(bytes.size(), 1199u);

  std::optional<CongestionControlFeedbackReport> decoded =
      CongestionControlFeedbackReport::Decode(bytes);

  ASSERT_TRUE(decoded.has_value());
  EXPECT_THAT(
      decoded->blocks,
      AllOf(SizeIs(kMaxStandaloneReportBlocks),
            Each(IsReceived(EcnMarking::kEct0, TimeDelta::Millis(500)))));
}

TEST(CongestionControlFeedbackReportTest, RoundTripsAtNonDefaultTickRate) {
  CongestionControlFeedbackReport report;
  report.blocks = {
      {.received = true, .arrival_time_offset = TimeDelta::Seconds(1)}};
  std::vector<uint8_t> bytes = ToBytes(report, kAltAtoTicksPerSecond);

  std::optional<CongestionControlFeedbackReport> decoded =
      CongestionControlFeedbackReport::Decode(bytes, kAltAtoTicksPerSecond);

  ASSERT_TRUE(decoded.has_value());
  EXPECT_THAT(decoded->blocks, ElementsAre(IsReceived(EcnMarking::kNotEct,
                                                      TimeDelta::Seconds(1))));
}

TEST(CongestionControlFeedbackReportTest, RejectsTruncatedHeader) {
  const std::vector<uint8_t> bytes(
      CongestionControlFeedbackReport::kHeaderBytes, 0);

  for (size_t size = 0; size < CongestionControlFeedbackReport::kHeaderBytes;
       ++size) {
    EXPECT_FALSE(
        CongestionControlFeedbackReport::Decode(std::span(bytes).first(size))
            .has_value())
        << "size=" << size;
  }
  EXPECT_TRUE(CongestionControlFeedbackReport::Decode(bytes).has_value());
}

TEST(CongestionControlFeedbackReportTest, RejectsReportTruncatedInsideBlocks) {
  CongestionControlFeedbackReport report;
  report.blocks.resize(2);
  const std::vector<uint8_t> bytes = ToBytes(report);

  for (size_t size = CongestionControlFeedbackReport::kHeaderBytes;
       size < bytes.size(); ++size) {
    EXPECT_FALSE(
        CongestionControlFeedbackReport::Decode(std::span(bytes).first(size))
            .has_value())
        << "size=" << size;
  }
  EXPECT_TRUE(CongestionControlFeedbackReport::Decode(bytes).has_value());
}

TEST(CongestionControlFeedbackReportTest,
     LeavesTrailingBytesForTheCarrierPayload) {
  CongestionControlFeedbackReport report;
  report.blocks.resize(1);
  std::vector<uint8_t> bytes = ToBytes(report);
  bytes.insert(bytes.end(), {0xDE, 0xAD});

  std::optional<CongestionControlFeedbackReport> decoded =
      CongestionControlFeedbackReport::Decode(bytes);

  ASSERT_TRUE(decoded.has_value());
  EXPECT_EQ(decoded->SizeBytes(), bytes.size() - 2);
}

}  // namespace
}  // namespace webrtc
