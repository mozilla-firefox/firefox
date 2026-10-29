/*
 *  Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef PC_DTLS_PACKET_PROCESSOR_H_
#define PC_DTLS_PACKET_PROCESSOR_H_

#include <cstdint>
#include <optional>
#include <span>

#include "api/transport/ecn_marking.h"
#include "api/units/timestamp.h"
#include "pc/packet_processor.h"
#include "rtc_base/copy_on_write_buffer.h"

namespace webrtc {

// Implements the wire format of the DTLS-only RtcTransport mode
// (WireProtocol::kDtlsWithFeedback).
//
// Every DTLS payload begins with a 1-byte packet type:
//   0x01 Data only : [type][seq(2)][app payload]
// Reserved values (0x00, 0x02-0xFF) are dropped with a warning; they are set
// aside for the congestion-control feedback wire format, which is not yet
// defined (its format is still under discussion) and will be added in a
// follow-up.
//
// The transport sequence number is carried now so that future feedback can
// report on packets sent by this stub without a wire-format change.
//
// The sender wraps each application payload in a transport header via
// ProcessOutgoingPacket(); the receiver strips it via ProcessIncomingPacket(),
// which returns the application payload (sharing storage with the input, no
// copy). All methods run on the network thread.
class DtlsPacketProcessor : public PacketProcessor {
 public:
  DtlsPacketProcessor() = default;
  ~DtlsPacketProcessor() override = default;

  DtlsPacketProcessor(const DtlsPacketProcessor&) = delete;
  DtlsPacketProcessor& operator=(const DtlsPacketProcessor&) = delete;

  // PacketProcessor implementation.
  CopyOnWriteBuffer ProcessOutgoingPacket(std::span<const uint8_t> payload,
                                          Timestamp send_time) override;
  std::optional<CopyOnWriteBuffer> ProcessIncomingPacket(
      CopyOnWriteBuffer packet,
      Timestamp receive_time,
      EcnMarking ecn) override;

  // Wire format packet types. Feedback types are reserved but not yet defined.
  static constexpr uint8_t kPacketTypeData = 0x01;

 private:
  uint16_t next_transport_seq_ = 0;
};

}  // namespace webrtc
#endif  // PC_DTLS_PACKET_PROCESSOR_H_
