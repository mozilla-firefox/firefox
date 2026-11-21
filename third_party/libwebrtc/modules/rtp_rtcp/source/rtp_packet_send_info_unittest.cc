/*
 *  Copyright (c) 2024 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <cstdint>

#include "absl/types/optional.h"
#include "modules/rtp_rtcp/include/rtp_header_extension_map.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtp_header_extensions.h"
#include "modules/rtp_rtcp/source/rtp_packet_to_send.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

RtpPacketToSend BuildPacket(RtpPacketMediaType type) {
  RtpHeaderExtensionMap extension_manager;
  RtpPacketToSend packet(&extension_manager);

  packet.SetSsrc(1);
  packet.SetSequenceNumber(89);
  if (type == RtpPacketMediaType::kRetransmission) {
    packet.set_original_ssrc(2);
    packet.set_retransmitted_sequence_number(678);
  }
  packet.set_transport_sequence_number(0xFFFFFFFF01);
  packet.SetTimestamp(123);
  packet.SetPayloadSize(5);
  packet.set_packet_type(type);
  return packet;
}

void VerifyDefaultProperties(const RtpPacketSendInfo& send_info,
                             const RtpPacketToSend& packet,
                             const PacedPacketInfo& paced_info) {
  EXPECT_EQ(send_info.length, packet.size());
  EXPECT_EQ(send_info.rtp_timestamp, packet.Timestamp());
  EXPECT_EQ(send_info.packet_type, packet.packet_type());
  EXPECT_EQ(send_info.pacing_info, paced_info);
  if (packet.transport_sequence_number()) {
    EXPECT_EQ(send_info.transport_sequence_number,
              *packet.transport_sequence_number() & 0xFFFF);
  } else {
    EXPECT_EQ(send_info.transport_sequence_number,
              *packet.GetExtension<TransportSequenceNumber>());
  }
}

TEST(RtpPacketSendInfoTest, FromConvertsMediaPackets) {
  RtpPacketToSend packet = BuildPacket(RtpPacketMediaType::kAudio);
  PacedPacketInfo paced_info;
  paced_info.probe_cluster_id = 8;

  RtpPacketSendInfo send_info = RtpPacketSendInfo::From(packet, paced_info);
  EXPECT_EQ(send_info.media_ssrc, packet.Ssrc());
  VerifyDefaultProperties(send_info, packet, paced_info);
}

TEST(RtpPacketSendInfoTest, FromConvertsPadding) {
  RtpPacketToSend packet = BuildPacket(RtpPacketMediaType::kPadding);
  PacedPacketInfo paced_info;
  paced_info.probe_cluster_id = 8;

  RtpPacketSendInfo send_info = RtpPacketSendInfo::From(packet, paced_info);
  EXPECT_EQ(send_info.media_ssrc, absl::nullopt);
  VerifyDefaultProperties(send_info, packet, paced_info);
}

TEST(RtpPacketSendInfoTest, FromConvertsFec) {
  RtpPacketToSend packet =
      BuildPacket(RtpPacketMediaType::kForwardErrorCorrection);
  PacedPacketInfo paced_info;
  paced_info.probe_cluster_id = 8;

  RtpPacketSendInfo send_info = RtpPacketSendInfo::From(packet, paced_info);
  EXPECT_EQ(send_info.media_ssrc, absl::nullopt);
  VerifyDefaultProperties(send_info, packet, paced_info);
}

TEST(RtpPacketSendInfoTest, FromConvertsRetransmission) {
  RtpPacketToSend packet = BuildPacket(RtpPacketMediaType::kRetransmission);
  PacedPacketInfo paced_info;
  paced_info.probe_cluster_id = 8;

  RtpPacketSendInfo send_info = RtpPacketSendInfo::From(packet, paced_info);
  EXPECT_EQ(send_info.media_ssrc, *packet.original_ssrc());
  EXPECT_EQ(send_info.rtp_sequence_number,
            *packet.retransmitted_sequence_number());
  VerifyDefaultProperties(send_info, packet, paced_info);
}

TEST(RtpPacketSendInfoTest, FromFallbackToTranportSequenceHeaderExtension) {
  RtpHeaderExtensionMap extension_manager;
  extension_manager.Register<TransportSequenceNumber>(/*id=*/1);
  PacedPacketInfo paced_info;
  paced_info.probe_cluster_id = 8;
  RtpPacketToSend packet(&extension_manager);
  packet.SetSsrc(1);
  packet.SetSequenceNumber(89);
  const uint16_t kTransportSequenceNumber = 5555;
  packet.SetExtension<TransportSequenceNumber>(kTransportSequenceNumber);
  packet.SetTimestamp(123);
  packet.AllocatePayload(5);
  packet.set_packet_type(RtpPacketMediaType::kAudio);

  RtpPacketSendInfo send_info = RtpPacketSendInfo::From(packet, paced_info);
  VerifyDefaultProperties(send_info, packet, paced_info);
}

}  // namespace
}  // namespace webrtc
