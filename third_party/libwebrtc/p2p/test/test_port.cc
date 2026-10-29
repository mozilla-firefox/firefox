/*
 *  Copyright 2025 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "p2p/test/test_port.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>

#include "absl/strings/string_view.h"
#include "api/candidate.h"
#include "api/transport/stun.h"
#include "p2p/base/connection.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/port.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/buffer.h"
#include "rtc_base/byte_buffer.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/network.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/socket.h"
#include "rtc_base/socket_address.h"

namespace webrtc {

TestPort::TestPort(const PortParametersRef& args,
                   uint16_t min_port,
                   uint16_t max_port)
    : Port(args, IceCandidateType::kHost, min_port, max_port) {}
TestPort::~TestPort() = default;

std::span<const uint8_t> TestPort::last_stun_buf() {
  if (!last_stun_buf_)
    return std::span<const uint8_t>();
  return *last_stun_buf_;
}
IceMessage* TestPort::last_stun_msg() {
  return last_stun_msg_.get();
}
int TestPort::last_stun_error_code() {
  int code = 0;
  if (last_stun_msg_) {
    const StunErrorCodeAttribute* error_attr = last_stun_msg_->GetErrorCode();
    if (error_attr) {
      code = error_attr->code();
    }
  }
  return code;
}

void TestPort::PrepareAddress() {
  // Act as if the socket was bound to the best IP on the network, to the
  // first port in the allowed range.
  SocketAddress addr(Network()->GetBestIP(), min_port());
  AddAddress(addr, addr, SocketAddress(), "udp", "", "", type(),
             ICE_TYPE_PREFERENCE_HOST, 0, "", true);
}

bool TestPort::SupportsProtocol(absl::string_view /* protocol */) const {
  return true;
}

ProtocolType TestPort::GetProtocol() const {
  return PROTO_UDP;
}

void TestPort::AddCandidateAddress(const SocketAddress& addr) {
  AddAddress(addr, addr, SocketAddress(), "udp", "", "", type(),
             type_preference_, 0, "", false);
}
void TestPort::AddCandidateAddress(const SocketAddress& addr,
                                   const SocketAddress& base_address,
                                   IceCandidateType type,
                                   int type_preference,
                                   bool final_candidate) {
  AddAddress(addr, base_address, SocketAddress(), "udp", "", "", type,
             type_preference, 0, "", final_candidate);
}

Connection* TestPort::CreateConnection(const Candidate& remote_candidate,
                                       CandidateOrigin /* origin */) {
  Connection* conn =
      new ProxyConnection(env(), NewWeakPtr(), 0, remote_candidate);
  AddOrReplaceConnection(conn);
  // Set use-candidate attribute flag as this will add USE-CANDIDATE attribute
  // in STUN binding requests.
  conn->set_use_candidate_attr(true);
  return conn;
}
int TestPort::SendTo(std::span<const uint8_t> data,
                     const SocketAddress& /* addr */,
                     const AsyncSocketPacketOptions& /* options */,
                     bool payload) {
  if (!payload) {
    auto msg = std::make_unique<IceMessage>();
    auto buf = std::make_unique<Buffer>(data);
    ByteBufferReader read_buf(*buf);
    if (!msg->Read(&read_buf)) {
      return -1;
    }
    last_stun_buf_ = std::move(buf);
    last_stun_msg_ = std::move(msg);
  }
  return static_cast<int>(data.size());
}

int TestPort::SetOption(Socket::Option /* opt */, int /* value */) {
  return 0;
}
int TestPort::GetOption(Socket::Option opt, int* value) {
  return -1;
}
int TestPort::GetError() {
  return 0;
}
void TestPort::Reset() {
  last_stun_buf_.reset();
  last_stun_msg_.reset();
}
void TestPort::set_type_preference(int type_preference) {
  type_preference_ = type_preference;
}

void TestPort::OnSentPacket(AsyncPacketSocket* socket,
                            const SentPacketInfo& sent_packet) {
  NotifySentPacket(sent_packet);
}

}  // namespace webrtc
