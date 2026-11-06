/*
 *  Copyright (c) 2004 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "media/base/rtp_utils.h"

#include <string.h>

#include <cstdint>
#include <vector>

#include "media/base/fake_rtp.h"
#include "rtc_base/async_packet_socket.h"
#include "test/gtest.h"

namespace cricket {

static const uint8_t kInvalidPacket[] = {0x80, 0x00};

// PT = 206, FMT = 1, Sender SSRC  = 0x1111, Media SSRC = 0x1111
// No FCI information is needed for PLI.
static const uint8_t kNonCompoundRtcpPliFeedbackPacket[] = {
    0x81, 0xCE, 0x00, 0x0C, 0x00, 0x00, 0x11, 0x11, 0x00, 0x00, 0x11, 0x11};

// Packet has only mandatory fixed RTCP header
// PT = 204, SSRC = 0x1111
static const uint8_t kNonCompoundRtcpAppPacket[] = {0x81, 0xCC, 0x00, 0x0C,
                                                    0x00, 0x00, 0x11, 0x11};

// PT = 202, Source count = 0
static const uint8_t kNonCompoundRtcpSDESPacket[] = {0x80, 0xCA, 0x00, 0x00};

static uint8_t kFakeTag[4] = {0xba, 0xdd, 0xba, 0xdd};
static uint8_t kTestKey[] = "12345678901234567890";
static uint8_t kTestAstValue[3] = {0xaa, 0xbb, 0xcc};

// Valid rtp Message with 2 byte header extension.
static uint8_t kRtpMsgWith2ByteExtnHeader[] = {
    // clang-format off
    // clang formatting doesn't respect inline comments.
  0x90, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0xAA, 0xBB, 0xCC, 0XDD,  // SSRC
  0x10, 0x00, 0x00, 0x01,  // 2 Byte header extension
  0x01, 0x00, 0x00, 0x00
    // clang-format on
};

// RTP packet with two one-byte header extensions. The last 4 bytes consist of
// abs-send-time with extension id = 3 and length = 3.
static uint8_t kRtpMsgWithOneByteAbsSendTimeExtension[] = {
    0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xBE, 0xDE, 0x00, 0x02, 0x22, 0x00, 0x02, 0x1c, 0x32, 0xaa, 0xbb, 0xcc,
};

// RTP packet with two two-byte header extensions. The last 5 bytes consist of
// abs-send-time with extension id = 3 and length = 3.
static uint8_t kRtpMsgWithTwoByteAbsSendTimeExtension[] = {
    0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x10, 0x00, 0x00, 0x02, 0x02, 0x01, 0x02, 0x03, 0x03, 0xaa, 0xbb, 0xcc,
};

// Index of AbsSendTimeExtn data in message
// `kRtpMsgWithOneByteAbsSendTimeExtension`.
static const int kAstIndexInOneByteRtpMsg = 21;
// and in message `kRtpMsgWithTwoByteAbsSendTimeExtension`.
static const int kAstIndexInTwoByteRtpMsg = 21;

static const rtc::ArrayView<const uint8_t> kPcmuFrameArrayView =
    rtc::MakeArrayView(kPcmuFrame, sizeof(kPcmuFrame));
static const rtc::ArrayView<const uint8_t> kRtcpReportArrayView =
    rtc::MakeArrayView(kRtcpReport, sizeof(kRtcpReport));
static const rtc::ArrayView<const uint8_t> kInvalidPacketArrayView =
    rtc::MakeArrayView(kInvalidPacket, sizeof(kInvalidPacket));

TEST(RtpUtilsTest, GetRtcp) {
  int pt;
  EXPECT_TRUE(GetRtcpType(kRtcpReport, sizeof(kRtcpReport), &pt));
  EXPECT_EQ(0xc9, pt);

  EXPECT_FALSE(GetRtcpType(kInvalidPacket, sizeof(kInvalidPacket), &pt));

  uint32_t ssrc;
  EXPECT_TRUE(GetRtcpSsrc(kNonCompoundRtcpPliFeedbackPacket,
                          sizeof(kNonCompoundRtcpPliFeedbackPacket), &ssrc));
  EXPECT_TRUE(GetRtcpSsrc(kNonCompoundRtcpAppPacket,
                          sizeof(kNonCompoundRtcpAppPacket), &ssrc));
  EXPECT_FALSE(GetRtcpSsrc(kNonCompoundRtcpSDESPacket,
                           sizeof(kNonCompoundRtcpSDESPacket), &ssrc));
}

// Invalid RTP packets.
TEST(RtpUtilsTest, InvalidRtpHeader) {
  // Rtp message with invalid length.
  const uint8_t kRtpMsgWithInvalidLength[] = {
      // clang-format off
      // clang formatting doesn't respect inline comments.
      0x94, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0xAA, 0xBB, 0xCC, 0XDD,  // SSRC
      0xDD, 0xCC, 0xBB, 0xAA,  // Only 1 CSRC, but CC count is 4.
      // clang-format on
  };
  EXPECT_FALSE(ValidateRtpHeader(kRtpMsgWithInvalidLength,
                                 sizeof(kRtpMsgWithInvalidLength), nullptr));

  // Rtp message with single byte header extension, invalid extension length.
  const uint8_t kRtpMsgWithInvalidExtnLength[] = {
      0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0xBE, 0xDE, 0x0A, 0x00,  // Extn length - 0x0A00
  };
  EXPECT_FALSE(ValidateRtpHeader(kRtpMsgWithInvalidExtnLength,
                                 sizeof(kRtpMsgWithInvalidExtnLength),
                                 nullptr));
}

// Valid RTP packet with a 2byte header extension.
TEST(RtpUtilsTest, Valid2ByteExtnHdrRtpMessage) {
  EXPECT_TRUE(ValidateRtpHeader(kRtpMsgWith2ByteExtnHeader,
                                sizeof(kRtpMsgWith2ByteExtnHeader), nullptr));
}

// Valid RTP packet which has 1 byte header AbsSendTime extension in it.
TEST(RtpUtilsTest, ValidRtpPacketWithOneByteAbsSendTimeExtension) {
  EXPECT_TRUE(ValidateRtpHeader(kRtpMsgWithOneByteAbsSendTimeExtension,
                                sizeof(kRtpMsgWithOneByteAbsSendTimeExtension),
                                nullptr));
}

// Valid RTP packet which has 2 byte header AbsSendTime extension in it.
TEST(RtpUtilsTest, ValidRtpPacketWithTwoByteAbsSendTimeExtension) {
  EXPECT_TRUE(ValidateRtpHeader(kRtpMsgWithTwoByteAbsSendTimeExtension,
                                sizeof(kRtpMsgWithTwoByteAbsSendTimeExtension),
                                nullptr));
}

// Verify finding an extension ID in the TURN send indication message.
TEST(RtpUtilsTest, UpdateAbsSendTimeExtensionInTurnSendIndication) {
  // A valid STUN indication message with a valid RTP header in data attribute
  // payload field and no extension bit set.
  uint8_t message_without_extension[] = {
      // clang-format off
      // clang formatting doesn't respect inline comments.
      0x00, 0x16, 0x00, 0x18,  // length of
      0x21, 0x12, 0xA4, 0x42,  // magic cookie
      '0',  '1',  '2',  '3',   // transaction id
      '4',  '5',  '6',  '7',
      '8',  '9',  'a',  'b',
      0x00, 0x20, 0x00, 0x04,  // Mapped address.
      0x00, 0x00, 0x00, 0x00,
      0x00, 0x13, 0x00, 0x0C,  // Data attribute.
      0x80, 0x00, 0x00, 0x00,  // RTP packet.
      0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00,
      // clang-format on
  };
  EXPECT_TRUE(UpdateRtpAbsSendTimeExtension(
      message_without_extension, sizeof(message_without_extension), 3, 0));

  // A valid STUN indication message with a valid RTP header and a extension
  // header.
  uint8_t message[] = {
      // clang-format off
      // clang formatting doesn't respect inline comments.
      0x00, 0x16, 0x00, 0x24,  // length of
      0x21, 0x12, 0xA4, 0x42,  // magic cookie
      '0',  '1',  '2',  '3',   // transaction id
      '4',  '5',  '6',  '7',
      '8',  '9',  'a',  'b',
      0x00, 0x20, 0x00, 0x04,  // Mapped address.
      0x00, 0x00, 0x00, 0x00,
      0x00, 0x13, 0x00, 0x18,  // Data attribute.
      0x90, 0x00, 0x00, 0x00,  // RTP packet.
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xBE, 0xDE,
      0x00, 0x02, 0x22, 0xaa, 0xbb, 0xcc, 0x32, 0xaa, 0xbb, 0xcc,
      // clang-format on
  };
  EXPECT_TRUE(UpdateRtpAbsSendTimeExtension(message, sizeof(message), 3, 0));
}

// Test without any packet options variables set. This method should return
// without HMAC value in the packet.
TEST(RtpUtilsTest, ApplyPacketOptionsWithDefaultValues) {
  rtc::PacketTimeUpdateParams packet_time_params;
  std::vector<uint8_t> rtp_packet(
      kRtpMsgWithOneByteAbsSendTimeExtension,
      kRtpMsgWithOneByteAbsSendTimeExtension +
          sizeof(kRtpMsgWithOneByteAbsSendTimeExtension));
  rtp_packet.insert(rtp_packet.end(), kFakeTag, kFakeTag + sizeof(kFakeTag));
  EXPECT_TRUE(ApplyPacketOptions(&rtp_packet[0], rtp_packet.size(),
                                 packet_time_params, 0));

  // Making sure HMAC wasn't updated..
  EXPECT_EQ(0,
            memcmp(&rtp_packet[sizeof(kRtpMsgWithOneByteAbsSendTimeExtension)],
                   kFakeTag, 4));

  // Verify AbsouluteSendTime extension field wasn't modified.
  EXPECT_EQ(0, memcmp(&rtp_packet[kAstIndexInOneByteRtpMsg], kTestAstValue,
                      sizeof(kTestAstValue)));
}

// Veirfy HMAC is updated when packet option parameters are set.
TEST(RtpUtilsTest, ApplyPacketOptionsWithAuthParams) {
  rtc::PacketTimeUpdateParams packet_time_params;
  packet_time_params.srtp_auth_key.assign(kTestKey,
                                          kTestKey + sizeof(kTestKey));
  packet_time_params.srtp_auth_tag_len = 4;

  std::vector<uint8_t> rtp_packet(
      kRtpMsgWithOneByteAbsSendTimeExtension,
      kRtpMsgWithOneByteAbsSendTimeExtension +
          sizeof(kRtpMsgWithOneByteAbsSendTimeExtension));
  rtp_packet.insert(rtp_packet.end(), kFakeTag, kFakeTag + sizeof(kFakeTag));
  EXPECT_TRUE(ApplyPacketOptions(&rtp_packet[0], rtp_packet.size(),
                                 packet_time_params, 0));

  uint8_t kExpectedTag[] = {0xc1, 0x7a, 0x8c, 0xa0};
  EXPECT_EQ(0,
            memcmp(&rtp_packet[sizeof(kRtpMsgWithOneByteAbsSendTimeExtension)],
                   kExpectedTag, sizeof(kExpectedTag)));

  // Verify AbsouluteSendTime extension field is not modified.
  EXPECT_EQ(0, memcmp(&rtp_packet[kAstIndexInOneByteRtpMsg], kTestAstValue,
                      sizeof(kTestAstValue)));
}

// Verify finding an extension ID in a raw rtp message.
TEST(RtpUtilsTest, UpdateOneByteAbsSendTimeExtensionInRtpPacket) {
  std::vector<uint8_t> rtp_packet(
      kRtpMsgWithOneByteAbsSendTimeExtension,
      kRtpMsgWithOneByteAbsSendTimeExtension +
          sizeof(kRtpMsgWithOneByteAbsSendTimeExtension));

  EXPECT_TRUE(UpdateRtpAbsSendTimeExtension(&rtp_packet[0], rtp_packet.size(),
                                            3, 51183266));

  // Verify that the timestamp was updated.
  const uint8_t kExpectedTimestamp[3] = {0xcc, 0xbb, 0xaa};
  EXPECT_EQ(0, memcmp(&rtp_packet[kAstIndexInOneByteRtpMsg], kExpectedTimestamp,
                      sizeof(kExpectedTimestamp)));
}

// Verify finding an extension ID in a raw rtp message.
TEST(RtpUtilsTest, UpdateTwoByteAbsSendTimeExtensionInRtpPacket) {
  std::vector<uint8_t> rtp_packet(
      kRtpMsgWithTwoByteAbsSendTimeExtension,
      kRtpMsgWithTwoByteAbsSendTimeExtension +
          sizeof(kRtpMsgWithTwoByteAbsSendTimeExtension));

  EXPECT_TRUE(UpdateRtpAbsSendTimeExtension(&rtp_packet[0], rtp_packet.size(),
                                            3, 51183266));

  // Verify that the timestamp was updated.
  const uint8_t kExpectedTimestamp[3] = {0xcc, 0xbb, 0xaa};
  EXPECT_EQ(0, memcmp(&rtp_packet[kAstIndexInTwoByteRtpMsg], kExpectedTimestamp,
                      sizeof(kExpectedTimestamp)));
}

// Verify we update both AbsSendTime extension header and HMAC.
TEST(RtpUtilsTest, ApplyPacketOptionsWithAuthParamsAndAbsSendTime) {
  rtc::PacketTimeUpdateParams packet_time_params;
  packet_time_params.srtp_auth_key.assign(kTestKey,
                                          kTestKey + sizeof(kTestKey));
  packet_time_params.srtp_auth_tag_len = 4;
  packet_time_params.rtp_sendtime_extension_id = 3;
  // 3 is also present in the test message.

  std::vector<uint8_t> rtp_packet(
      kRtpMsgWithOneByteAbsSendTimeExtension,
      kRtpMsgWithOneByteAbsSendTimeExtension +
          sizeof(kRtpMsgWithOneByteAbsSendTimeExtension));
  rtp_packet.insert(rtp_packet.end(), kFakeTag, kFakeTag + sizeof(kFakeTag));
  EXPECT_TRUE(ApplyPacketOptions(&rtp_packet[0], rtp_packet.size(),
                                 packet_time_params, 51183266));

  const uint8_t kExpectedTag[] = {0x81, 0xd1, 0x2c, 0x0e};
  EXPECT_EQ(0,
            memcmp(&rtp_packet[sizeof(kRtpMsgWithOneByteAbsSendTimeExtension)],
                   kExpectedTag, sizeof(kExpectedTag)));

  // Verify that the timestamp was updated.
  const uint8_t kExpectedTimestamp[3] = {0xcc, 0xbb, 0xaa};
  EXPECT_EQ(0, memcmp(&rtp_packet[kAstIndexInOneByteRtpMsg], kExpectedTimestamp,
                      sizeof(kExpectedTimestamp)));
}

TEST(RtpUtilsTest, InferRtpPacketType) {
  EXPECT_EQ(RtpPacketType::kRtp, InferRtpPacketType(kPcmuFrameArrayView));
  EXPECT_EQ(RtpPacketType::kRtcp, InferRtpPacketType(kRtcpReportArrayView));
  EXPECT_EQ(RtpPacketType::kUnknown,
            InferRtpPacketType(kInvalidPacketArrayView));
}

}  // namespace cricket
