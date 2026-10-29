/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/rtp_rtcp/source/rtp_packetizer_sframe.h"

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "api/media_types.h"
#include "modules/rtp_rtcp/source/rtp_format.h"
#include "modules/rtp_rtcp/source/rtp_packet_to_send.h"
#include "modules/rtp_rtcp/source/sframe_descriptor.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

using ::testing::ElementsAre;
using ::testing::ElementsAreArray;

uint8_t MakeDescriptorByte(bool s, bool e, bool t) {
  std::bitset<SframeDescriptor::kNumBits> bits;
  bits.set(SframeDescriptor::kSBit, s);
  bits.set(SframeDescriptor::kEBit, e);
  bits.set(SframeDescriptor::kTBit, t);
  return static_cast<uint8_t>(bits.to_ulong());
}

RtpPacketToSend MakeSendPacket(std::vector<uint8_t> payload) {
  RtpPacketToSend packet(/*extensions=*/nullptr);
  packet.SetSequenceNumber(0x4242);
  packet.SetTimestamp(0xDEADBEEF);
  packet.SetPayloadType(96);
  packet.SetPayload(payload);
  return packet;
}

TEST(RtpPacketizerSframeTest, SmallFrameFitsInOneMarkedPacket) {
  const std::vector<uint8_t> payload = {1, 2, 3, 4};
  RtpPacketizerSframe packetizer(payload, RtpPacketizer::PayloadSizeLimits(),
                                 /*is_last_frame_in_picture=*/true);
  ASSERT_EQ(packetizer.NumPackets(), 1u);

  RtpPacketToSend packet = MakeSendPacket({});
  ASSERT_TRUE(packetizer.NextPacket(&packet));
  EXPECT_THAT(packet.payload(), ElementsAre(0b110'00000, 1, 2, 3, 4));
  EXPECT_TRUE(packet.Marker());
}

TEST(RtpPacketizerSframeTest, ReturnsFalseAfterLastPacket) {
  const std::vector<uint8_t> payload = {1, 2, 3, 4};
  RtpPacketizerSframe packetizer(payload, RtpPacketizer::PayloadSizeLimits(),
                                 /*is_last_frame_in_picture=*/true);
  RtpPacketToSend packet = MakeSendPacket({});
  ASSERT_TRUE(packetizer.NextPacket(&packet));

  EXPECT_EQ(packetizer.NumPackets(), 0u);
  EXPECT_FALSE(packetizer.NextPacket(&packet));
}

// The frame and the descriptor would fit in one 5-byte packet, but not with
// the single packet reduction, so the frame is split into two packets.
TEST(RtpPacketizerSframeTest, HonorsPacketReductionsWithDescriptor) {
  const std::vector<uint8_t> payload = {1, 2, 3, 4};
  RtpPacketizer::PayloadSizeLimits limits;
  limits.max_payload_len = 5;
  limits.single_packet_reduction_len = 1;
  RtpPacketizerSframe packetizer(payload, limits,
                                 /*is_last_frame_in_picture=*/true);

  EXPECT_EQ(packetizer.NumPackets(), 2u);
}

// The limits cover the descriptor, so each packet carries 3 bytes of the frame.
TEST(RtpPacketizerSframeTest, FragmentsFrameAndPrependsDescriptorToEachPacket) {
  const std::vector<uint8_t> payload = {0, 1, 2, 3, 4, 5};
  RtpPacketizer::PayloadSizeLimits limits;
  limits.max_payload_len = 4;
  RtpPacketizerSframe packetizer(payload, limits,
                                 /*is_last_frame_in_picture=*/true);
  ASSERT_EQ(packetizer.NumPackets(), 2u);

  RtpPacketToSend first = MakeSendPacket({});
  ASSERT_TRUE(packetizer.NextPacket(&first));
  EXPECT_THAT(first.payload(), ElementsAre(0b100'00000, 0, 1, 2));
  EXPECT_FALSE(first.Marker());

  RtpPacketToSend last = MakeSendPacket({});
  ASSERT_TRUE(packetizer.NextPacket(&last));
  EXPECT_THAT(last.payload(), ElementsAre(0b010'00000, 3, 4, 5));
  EXPECT_TRUE(last.Marker());
}

// With spatial scalability only the last frame of a picture ends with a marked
// packet.
TEST(RtpPacketizerSframeTest, DoesNotMarkFrameThatIsNotLastInPicture) {
  const std::vector<uint8_t> payload = {1, 2, 3, 4};
  RtpPacketizerSframe packetizer(payload, RtpPacketizer::PayloadSizeLimits(),
                                 /*is_last_frame_in_picture=*/false);

  RtpPacketToSend packet = MakeSendPacket({});
  ASSERT_TRUE(packetizer.NextPacket(&packet));
  EXPECT_FALSE(packet.Marker());
}

TEST(RtpPacketizerSframeTest, RejectsLimitsWithRoomOnlyForDescriptor) {
  const std::vector<uint8_t> payload = {0x12};
  RtpPacketizer::PayloadSizeLimits limits;
  limits.max_payload_len = SframeDescriptor::kSize;
  RtpPacketizerSframe packetizer(payload, limits,
                                 /*is_last_frame_in_picture=*/true);
  EXPECT_EQ(packetizer.NumPackets(), 0u);

  RtpPacketToSend packet = MakeSendPacket({});
  EXPECT_FALSE(packetizer.NextPacket(&packet));
}

TEST(PacketizeSframeRtpPacketOrErrorTest, PrependsDescriptorByteToPayload) {
  const std::vector<uint8_t> payload = {0x01, 0x02, 0x03, 0x04, 0x05};
  RtpPacketToSend packet = MakeSendPacket(payload);
  packet.set_first_packet_of_frame(true);
  packet.set_last_packet_of_frame(true);
  ASSERT_TRUE(PacketizeSframeRtpPacketOrError(MediaType::VIDEO, &packet,
                                              SframeEncryptionLevel::kFrame)
                  .ok());
  std::vector<uint8_t> expected = payload;
  expected.insert(expected.begin(),
                  MakeDescriptorByte(/*s=*/true, /*e=*/true, /*t=*/false));
  EXPECT_THAT(packet.payload(), ElementsAreArray(expected));
}

TEST(PacketizeSframeRtpPacketOrErrorTest, EncodesTBitAsPacketEncryptionLevel) {
  RtpPacketToSend packet = MakeSendPacket({0xAA});
  packet.set_first_packet_of_frame(true);
  packet.set_last_packet_of_frame(true);
  ASSERT_TRUE(PacketizeSframeRtpPacketOrError(MediaType::VIDEO, &packet,
                                              SframeEncryptionLevel::kPacket)
                  .ok());
  EXPECT_EQ(packet.payload()[0],
            MakeDescriptorByte(/*s=*/true, /*e=*/true, /*t=*/true));
}

TEST(PacketizeSframeRtpPacketOrErrorTest, AcceptsEmptyPayload) {
  RtpPacketToSend packet = MakeSendPacket({});
  packet.set_first_packet_of_frame(true);
  packet.set_last_packet_of_frame(true);
  ASSERT_TRUE(PacketizeSframeRtpPacketOrError(MediaType::VIDEO, &packet,
                                              SframeEncryptionLevel::kFrame)
                  .ok());
  EXPECT_THAT(packet.payload(),
              ElementsAreArray(std::vector<uint8_t>{MakeDescriptorByte(
                  /*s=*/true, /*e=*/true, /*t=*/false)}));
}

TEST(PacketizeSframeRtpPacketOrErrorTest, PreservesRtpHeaderFields) {
  RtpPacketToSend packet = MakeSendPacket({0xAB, 0xCD});
  packet.set_first_packet_of_frame(true);
  packet.set_last_packet_of_frame(false);
  ASSERT_TRUE(PacketizeSframeRtpPacketOrError(MediaType::VIDEO, &packet,
                                              SframeEncryptionLevel::kFrame)
                  .ok());
  EXPECT_EQ(packet.SequenceNumber(), 0x4242);
  EXPECT_EQ(packet.Timestamp(), 0xDEADBEEFu);
  EXPECT_EQ(packet.PayloadType(), 96);
}

TEST(PacketizeSframeRtpPacketOrErrorTest, PreservesMarkerBit) {
  for (bool marker : {false, true}) {
    SCOPED_TRACE(marker);
    RtpPacketToSend packet = MakeSendPacket({0xAA});
    packet.SetMarker(marker);
    packet.set_first_packet_of_frame(true);
    packet.set_last_packet_of_frame(true);
    ASSERT_TRUE(PacketizeSframeRtpPacketOrError(MediaType::VIDEO, &packet,
                                                SframeEncryptionLevel::kFrame)
                    .ok());
    EXPECT_EQ(packet.Marker(), marker);
  }
}

TEST(PacketizeSframeRtpPacketOrErrorTest, PreservesSsrc) {
  RtpPacketToSend packet = MakeSendPacket({0xAA});
  packet.SetSsrc(0xCAFEBABE);
  packet.set_first_packet_of_frame(true);
  packet.set_last_packet_of_frame(true);
  ASSERT_TRUE(PacketizeSframeRtpPacketOrError(MediaType::VIDEO, &packet,
                                              SframeEncryptionLevel::kFrame)
                  .ok());
  EXPECT_EQ(packet.Ssrc(), 0xCAFEBABEu);
}

TEST(PacketizeSframeRtpPacketOrErrorTest, ShrinksPaddingByOneWhenPresent) {
  const std::vector<uint8_t> payload = {0xAB, 0xCD};
  RtpPacketToSend packet = MakeSendPacket(payload);
  ASSERT_TRUE(packet.SetPadding(4));
  const size_t size_before = packet.size();
  packet.set_first_packet_of_frame(true);
  packet.set_last_packet_of_frame(true);
  ASSERT_TRUE(PacketizeSframeRtpPacketOrError(MediaType::VIDEO, &packet,
                                              SframeEncryptionLevel::kFrame)
                  .ok());
  EXPECT_EQ(packet.padding_size(), 3u);
  EXPECT_EQ(packet.size(), size_before);
  std::vector<uint8_t> expected = payload;
  expected.insert(expected.begin(),
                  MakeDescriptorByte(/*s=*/true, /*e=*/true, /*t=*/false));
  EXPECT_THAT(packet.payload(), ElementsAreArray(expected));
}

TEST(PacketizeSframeRtpPacketOrErrorTest, ClearsPaddingWhenOnlyOneBytePresent) {
  RtpPacketToSend packet = MakeSendPacket({0xAB});
  ASSERT_TRUE(packet.SetPadding(1));
  packet.set_first_packet_of_frame(true);
  packet.set_last_packet_of_frame(true);
  ASSERT_TRUE(PacketizeSframeRtpPacketOrError(MediaType::VIDEO, &packet,
                                              SframeEncryptionLevel::kFrame)
                  .ok());
  EXPECT_EQ(packet.padding_size(), 0u);
  EXPECT_FALSE(packet.has_padding());
}

TEST(PacketizeSframeRtpPacketOrErrorTest, FailsWhenNoRoomForDescriptor) {
  RtpPacketToSend packet(/*extensions=*/nullptr, /*capacity=*/12 + 1);
  packet.SetSequenceNumber(1);
  packet.SetTimestamp(2);
  packet.SetPayloadType(96);
  packet.SetPayload(std::vector<uint8_t>{0x01});
  packet.set_first_packet_of_frame(true);
  packet.set_last_packet_of_frame(true);
  EXPECT_FALSE(PacketizeSframeRtpPacketOrError(MediaType::VIDEO, &packet,
                                               SframeEncryptionLevel::kFrame)
                   .ok());
}

TEST(PacketizeSframeRtpPacketOrErrorTest,
     SucceedsForPaddedPacketAtCapacityByShrinkingPadding) {
  RtpPacketToSend packet(/*extensions=*/nullptr, /*capacity=*/12 + 2 + 4);
  packet.SetSequenceNumber(1);
  packet.SetTimestamp(2);
  packet.SetPayloadType(96);
  packet.SetPayload(std::vector<uint8_t>{0x01, 0x02});
  ASSERT_TRUE(packet.SetPadding(4));
  packet.set_first_packet_of_frame(true);
  packet.set_last_packet_of_frame(true);
  EXPECT_TRUE(PacketizeSframeRtpPacketOrError(MediaType::VIDEO, &packet,
                                              SframeEncryptionLevel::kFrame)
                  .ok());
  EXPECT_EQ(packet.padding_size(), 3u);
  EXPECT_EQ(packet.size(), 12u + 2u + 4u);
}

TEST(PacketizeSframeRtpPacketOrErrorTest, AudioForcesStartAndEndBits) {
  RtpPacketToSend packet = MakeSendPacket({0x55});
  packet.set_first_packet_of_frame(false);
  packet.set_last_packet_of_frame(false);
  ASSERT_TRUE(PacketizeSframeRtpPacketOrError(MediaType::AUDIO, &packet,
                                              SframeEncryptionLevel::kPacket)
                  .ok());
  EXPECT_EQ(packet.payload()[0],
            MakeDescriptorByte(/*s=*/true, /*e=*/true, /*t=*/true));
}

}  // namespace
}  // namespace webrtc
