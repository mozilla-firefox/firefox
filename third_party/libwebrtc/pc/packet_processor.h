/*
 *  Copyright (c) 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef PC_PACKET_PROCESSOR_H_
#define PC_PACKET_PROCESSOR_H_

#include <cstdint>
#include <optional>
#include <span>

#include "api/transport/ecn_marking.h"
#include "api/units/timestamp.h"
#include "rtc_base/copy_on_write_buffer.h"

namespace webrtc {

// Transport-protocol-agnostic hook that a DatagramConnection consults on every
// outgoing and incoming packet. Implementations encapsulate all protocol
// specifics (e.g. in-band framing for DTLS-only mode) so the connection itself
// stays protocol-agnostic and simply forwards packets through the two transform
// methods below.
//
// This is currently a minimal stub: it only frames the application payload with
// a transport header. Congestion-control feedback carried inside that framing
// is intentionally left out until its wire format has been agreed upon, at
// which point parsed feedback will be surfaced out-of-band (not through a
// return value) so the caller never has to reason about it.
//
// All methods run on the network thread.
class PacketProcessor {
 public:
  virtual ~PacketProcessor() = default;

  // Sender side. Returns the wire bytes to send for the application `payload`.
  // Implementations may wrap the payload in transport framing and record
  // whatever per-packet metadata is needed. `send_time` is when the packet is
  // handed to the wire. Protocols that need no framing may return a buffer that
  // simply copies `payload`.
  virtual CopyOnWriteBuffer ProcessOutgoingPacket(
      std::span<const uint8_t> payload,
      Timestamp send_time) = 0;

  // Receiver side. Consumes an incoming wire `packet` and returns the
  // application payload to deliver to the app, or nullopt when the packet
  // carries no application data (e.g. a packet dropped as malformed). The
  // returned buffer shares storage with `packet` (no copy of the payload
  // bytes).
  virtual std::optional<CopyOnWriteBuffer> ProcessIncomingPacket(
      CopyOnWriteBuffer packet,
      Timestamp receive_time,
      EcnMarking ecn) = 0;
};

}  // namespace webrtc
#endif  // PC_PACKET_PROCESSOR_H_
