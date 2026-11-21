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
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/rtp_header_extensions.h"
#include "modules/rtp_rtcp/source/rtp_packet_to_send.h"

namespace webrtc {

RtpPacketSendInfo RtpPacketSendInfo::From(const RtpPacketToSend& packet,
                                          const PacedPacketInfo& pacing_info) {
  RtpPacketSendInfo packet_info;
  if (packet.transport_sequence_number()) {
    packet_info.transport_sequence_number =
        *packet.transport_sequence_number() & 0xFFFF;
  } else {
    absl::optional<uint16_t> packet_id =
        packet.GetExtension<TransportSequenceNumber>();
    if (packet_id) {
      packet_info.transport_sequence_number = *packet_id;
    }
  }

  packet_info.rtp_timestamp = packet.Timestamp();
  packet_info.length = packet.size();
  packet_info.pacing_info = pacing_info;
  packet_info.packet_type = packet.packet_type();

  switch (*packet_info.packet_type) {
    case RtpPacketMediaType::kAudio:
    case RtpPacketMediaType::kVideo:
      packet_info.media_ssrc = packet.Ssrc();
      packet_info.rtp_sequence_number = packet.SequenceNumber();
      break;
    case RtpPacketMediaType::kRetransmission:
      RTC_DCHECK(packet.original_ssrc() &&
                 packet.retransmitted_sequence_number());
      // For retransmissions, we're want to remove the original media packet
      // if the retransmit arrives - so populate that in the packet info.
      packet_info.media_ssrc = packet.original_ssrc().value_or(0);
      packet_info.rtp_sequence_number =
          packet.retransmitted_sequence_number().value_or(0);
      break;
    case RtpPacketMediaType::kPadding:
    case RtpPacketMediaType::kForwardErrorCorrection:
      // We're not interested in feedback about these packets being received
      // or lost.
      break;
  }
  return packet_info;
}

}  // namespace webrtc
