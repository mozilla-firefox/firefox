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

#include <algorithm>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "api/media_types.h"
#include "api/rtc_error.h"
#include "modules/rtp_rtcp/source/rtp_format.h"
#include "modules/rtp_rtcp/source/rtp_packet_to_send.h"
#include "modules/rtp_rtcp/source/sframe_descriptor.h"
#include "rtc_base/checks.h"

namespace webrtc {
namespace {

// Returns S and E bits for the Sframe descriptor.
// - Video uses frame fragmentation flags on `packet`;
// - Audio always sets both (one RTP packet per frame).
std::pair<bool, bool> GetSframeStartAndEndBits(MediaType media_type,
                                               const RtpPacketToSend& packet) {
  if (media_type == MediaType::AUDIO) {
    return {true, true};
  }

  return {packet.is_first_packet_of_frame(), packet.is_last_packet_of_frame()};
}

uint8_t BuildDescriptorByte(bool start,
                            bool end,
                            SframeEncryptionLevel encryption_level) {
  std::bitset<SframeDescriptor::kNumBits> bits;
  bits.set(SframeDescriptor::kSBit, start);
  bits.set(SframeDescriptor::kEBit, end);
  bits.set(SframeDescriptor::kTBit,
           encryption_level == SframeEncryptionLevel::kPacket);
  return static_cast<uint8_t>(bits.to_ulong());
}

}  // namespace

RtpPacketizerSframe::RtpPacketizerSframe(std::span<const uint8_t> payload,
                                         PayloadSizeLimits limits,
                                         bool is_last_frame_in_picture)
    : remaining_payload_(payload),
      is_last_frame_in_picture_(is_last_frame_in_picture) {
  limits.max_payload_len -= SframeDescriptor::kSize;
  payload_sizes_ = SplitAboutEqually(payload.size(), limits);
  current_packet_ = payload_sizes_.begin();
}

RtpPacketizerSframe::~RtpPacketizerSframe() = default;

size_t RtpPacketizerSframe::NumPackets() const {
  return payload_sizes_.end() - current_packet_;
}

bool RtpPacketizerSframe::NextPacket(RtpPacketToSend* packet) {
  RTC_DCHECK(packet);
  if (current_packet_ == payload_sizes_.end()) {
    return false;
  }

  const bool first_packet = current_packet_ == payload_sizes_.begin();
  const size_t media_size = *current_packet_;
  ++current_packet_;
  const bool last_packet = current_packet_ == payload_sizes_.end();

  const size_t payload_size = SframeDescriptor::kSize + media_size;
  std::span<uint8_t> payload(packet->AllocatePayload(payload_size),
                             payload_size);
  payload[0] = BuildDescriptorByte(first_packet, last_packet,
                                   SframeEncryptionLevel::kFrame);
  std::ranges::copy(remaining_payload_.first(media_size),
                    payload.subspan(SframeDescriptor::kSize).begin());
  remaining_payload_ = remaining_payload_.subspan(media_size);

  // Packets left to produce and data left to split end at the same time.
  RTC_DCHECK_EQ(last_packet, remaining_payload_.empty());

  packet->SetMarker(last_packet && is_last_frame_in_picture_);
  return true;
}

RTCError PacketizeSframeRtpPacketOrError(
    MediaType media_type,
    RtpPacketToSend* packet,
    SframeEncryptionLevel encryption_level) {
  RTC_DCHECK(media_type == MediaType::AUDIO || media_type == MediaType::VIDEO);
  RTC_DCHECK(packet);

  const size_t payload_size = packet->payload_size();
  const size_t padding_size = packet->padding_size();
  const size_t restored_padding_size =
      std::max(padding_size, SframeDescriptor::kSize) - SframeDescriptor::kSize;

  if (payload_size + SframeDescriptor::kSize + restored_padding_size >
      packet->MaxPayloadSize()) {
    return RTCError::InvalidParameter(
        "RTP packet has no room for Sframe descriptor");
  }

  packet->SetPadding(0);

  std::span<uint8_t> payload(
      packet->SetPayloadSize(payload_size + SframeDescriptor::kSize),
      payload_size + SframeDescriptor::kSize);

  if (payload_size > 0) {
    auto src = payload.first(payload_size);
    std::copy_backward(src.begin(), src.end(), payload.end());
  }

  const auto [start, end] = GetSframeStartAndEndBits(media_type, *packet);
  payload[0] = BuildDescriptorByte(start, end, encryption_level);

  if (restored_padding_size > 0 && !packet->SetPadding(restored_padding_size)) {
    return RTCError::InternalError("Failed to restore RTP padding");
  }

  return RTCError::OK();
}

}  // namespace webrtc
