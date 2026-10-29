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

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "api/transport/ecn_marking.h"
#include "api/units/timestamp.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/logging.h"

namespace webrtc {
namespace {

// Data-only header: 1 byte type + 2 byte transport sequence number.
constexpr size_t kDataHeaderSize = 3;

}  // namespace

CopyOnWriteBuffer DtlsPacketProcessor::ProcessOutgoingPacket(
    std::span<const uint8_t> payload,
    Timestamp /*send_time*/) {
  const uint16_t seq = next_transport_seq_++;

  // Data-only packet (type 0x01).
  const uint8_t header[kDataHeaderSize] = {
      kPacketTypeData,
      static_cast<uint8_t>(seq >> 8),
      static_cast<uint8_t>(seq & 0xFF),
  };
  CopyOnWriteBuffer out(/*size=*/0,
                        /*capacity=*/kDataHeaderSize + payload.size());
  out.AppendData(header);
  out.AppendData(payload.data(), payload.size());
  return out;
}

std::optional<CopyOnWriteBuffer> DtlsPacketProcessor::ProcessIncomingPacket(
    CopyOnWriteBuffer packet,
    Timestamp /*receive_time*/,
    EcnMarking /*ecn*/) {
  if (packet.empty()) {
    return std::nullopt;
  }
  switch (packet.cdata()[0]) {
    case kPacketTypeData: {
      if (packet.size() < kDataHeaderSize) {
        RTC_LOG(LS_WARNING) << "Dropping truncated DTLS data packet";
        return std::nullopt;
      }
      // Zero-copy view of the app payload (extends to the end of the record).
      return packet.Slice(kDataHeaderSize, packet.size() - kDataHeaderSize);
    }
    default:
      RTC_LOG(LS_WARNING) << "Dropping DTLS packet with reserved type "
                          << static_cast<int>(packet.cdata()[0]);
      return std::nullopt;
  }
}

}  // namespace webrtc
