/*
 *  Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "pc/dtls_packet_processor.h"

#include <cstdint>
#include <optional>
#include <vector>

#include "api/transport/ecn_marking.h"
#include "api/units/timestamp.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

std::vector<uint8_t> ToVector(const CopyOnWriteBuffer& buffer) {
  return std::vector<uint8_t>(buffer.begin(), buffer.end());
}

uint16_t TransportSeq(const CopyOnWriteBuffer& buffer) {
  return static_cast<uint16_t>((static_cast<uint16_t>(buffer[1]) << 8) |
                               buffer[2]);
}

TEST(DtlsPacketProcessorTest, FramesAndStripsDataOnlyPacket) {
  DtlsPacketProcessor sender;
  DtlsPacketProcessor receiver;

  std::vector<uint8_t> payload = {1, 2, 3, 4, 5};
  CopyOnWriteBuffer wire =
      sender.ProcessOutgoingPacket(payload, Timestamp::Millis(100));

  EXPECT_EQ(wire[0], DtlsPacketProcessor::kPacketTypeData);
  EXPECT_EQ(wire.size(), payload.size() + 3);

  std::optional<CopyOnWriteBuffer> app_payload = receiver.ProcessIncomingPacket(
      wire, Timestamp::Millis(101), EcnMarking::kNotEct);
  ASSERT_TRUE(app_payload.has_value());
  EXPECT_EQ(ToVector(*app_payload), payload);
}

TEST(DtlsPacketProcessorTest, TransportSequenceNumberIncrements) {
  DtlsPacketProcessor sender;

  std::vector<uint8_t> payload = {9};
  CopyOnWriteBuffer p0 =
      sender.ProcessOutgoingPacket(payload, Timestamp::Millis(1));
  CopyOnWriteBuffer p1 =
      sender.ProcessOutgoingPacket(payload, Timestamp::Millis(2));

  EXPECT_EQ(TransportSeq(p1), static_cast<uint16_t>(TransportSeq(p0) + 1));
}

TEST(DtlsPacketProcessorTest, ReservedTypeIsDropped) {
  DtlsPacketProcessor receiver;

  CopyOnWriteBuffer junk(std::vector<uint8_t>{0x02, 1, 2, 3});
  EXPECT_FALSE(receiver
                   .ProcessIncomingPacket(junk, Timestamp::Millis(1),
                                          EcnMarking::kNotEct)
                   .has_value());
}

TEST(DtlsPacketProcessorTest, EmptyPacketIsDropped) {
  DtlsPacketProcessor receiver;

  EXPECT_FALSE(receiver
                   .ProcessIncomingPacket(CopyOnWriteBuffer(),
                                          Timestamp::Millis(1),
                                          EcnMarking::kNotEct)
                   .has_value());
}

TEST(DtlsPacketProcessorTest, TruncatedDataPacketIsDropped) {
  DtlsPacketProcessor receiver;

  // Type byte present but missing part of the sequence number.
  CopyOnWriteBuffer truncated(
      std::vector<uint8_t>{DtlsPacketProcessor::kPacketTypeData, 0x00});
  EXPECT_FALSE(receiver
                   .ProcessIncomingPacket(truncated, Timestamp::Millis(1),
                                          EcnMarking::kNotEct)
                   .has_value());
}

}  // namespace
}  // namespace webrtc
