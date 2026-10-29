/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_RTP_RTCP_SOURCE_RTP_PACKETIZER_SFRAME_H_
#define MODULES_RTP_RTCP_SOURCE_RTP_PACKETIZER_SFRAME_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "absl/base/attributes.h"
#include "api/media_types.h"
#include "api/rtc_error.h"
#include "modules/rtp_rtcp/source/rtp_format.h"
#include "modules/rtp_rtcp/source/rtp_packet_to_send.h"
#include "modules/rtp_rtcp/source/sframe_descriptor.h"

namespace webrtc {

// Packetizes a frame encrypted with per-frame Sframe like raw packetization,
// starting every packet with the Sframe descriptor.
class RtpPacketizerSframe : public RtpPacketizer {
 public:
  RtpPacketizerSframe(std::span<const uint8_t> payload
                          ABSL_ATTRIBUTE_LIFETIME_BOUND,
                      PayloadSizeLimits limits,
                      bool is_last_frame_in_picture);
  ~RtpPacketizerSframe() override;

  size_t NumPackets() const override;
  bool NextPacket(RtpPacketToSend* packet) override;

 private:
  std::span<const uint8_t> remaining_payload_;
  std::vector<int> payload_sizes_;
  std::vector<int>::const_iterator current_packet_;
  const bool is_last_frame_in_picture_;
};

// Prepends the Sframe RTP payload descriptor to `packet`'s payload.
// Returns an error if `packet` has no room for the descriptor.
RTCError PacketizeSframeRtpPacketOrError(
    MediaType media_type,
    RtpPacketToSend* packet,
    SframeEncryptionLevel encryption_level);

}  // namespace webrtc

#endif  // MODULES_RTP_RTCP_SOURCE_RTP_PACKETIZER_SFRAME_H_
