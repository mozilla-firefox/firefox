/*
 *  Copyright 2025 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef P2P_TEST_TEST_PORT_H_
#define P2P_TEST_TEST_PORT_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "absl/strings/string_view.h"
#include "api/candidate.h"
#include "api/transport/stun.h"
#include "p2p/base/connection.h"
#include "p2p/base/port.h"
#include "p2p/base/port_interface.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/buffer.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/socket.h"
#include "rtc_base/socket_address.h"

namespace webrtc {

// Stub port class for testing STUN generation and processing.
class TestPort : public Port {
 public:
  TestPort(const PortParametersRef& args, uint16_t min_port, uint16_t max_port);
  ~TestPort() override;

  // Expose GetStunMessage so that we can test it.
  using Port::GetStunMessage;

  // The last StunMessage that was sent on this Port.
  std::span<const uint8_t> last_stun_buf();
  IceMessage* last_stun_msg();
  int last_stun_error_code();

  void PrepareAddress() override;

  bool SupportsProtocol(absl::string_view protocol) const override;

  ProtocolType GetProtocol() const override;

  // Exposed for testing candidate building.
  void AddCandidateAddress(const SocketAddress& addr);
  void AddCandidateAddress(const SocketAddress& addr,
                           const SocketAddress& base_address,
                           IceCandidateType type,
                           int type_preference,
                           bool final_candidate);

  Connection* CreateConnection(const Candidate& remote_candidate,
                               CandidateOrigin origin) override;
  int SendTo(std::span<const uint8_t> data,
             const SocketAddress& addr,
             const AsyncSocketPacketOptions& options,
             bool payload) override;

  int SetOption(Socket::Option opt, int value) override;

  int GetOption(Socket::Option opt, int* value) override;
  int GetError() override;
  void Reset();
  void set_type_preference(int type_preference);

 private:
  void OnSentPacket(AsyncPacketSocket* socket,
                    const SentPacketInfo& sent_packet) override;
  std::unique_ptr<Buffer> last_stun_buf_;
  std::unique_ptr<IceMessage> last_stun_msg_;
  int type_preference_ = 0;
};

}  // namespace webrtc

#endif  // P2P_TEST_TEST_PORT_H_
