/*
 *  Copyright 2017 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef P2P_TEST_FAKE_PACKET_TRANSPORT_H_
#define P2P_TEST_FAKE_PACKET_TRANSPORT_H_

#include <cstddef>
#include <map>
#include <optional>
#include <string>

#include "absl/strings/string_view.h"
#include "api/environment/environment.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/task_queue/task_queue_base.h"
#include "api/transport/ecn_marking.h"
#include "p2p/base/packet_transport_internal.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/network/received_packet.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/network_route.h"
#include "rtc_base/socket.h"
#include "rtc_base/socket_address.h"

namespace webrtc {

// Used to simulate a packet-based transport.
class FakePacketTransport : public PacketTransportInternal {
 public:
  FakePacketTransport(const Environment& env, absl::string_view transport_name)
      : env_(env), transport_name_(transport_name) {}
  ~FakePacketTransport() override {
    if (dest_ && dest_->dest_ == this) {
      dest_->dest_ = nullptr;
    }
  }

  // SetWritable, SetReceiving and SetDestination are the main methods that can
  // be used for testing, to simulate connectivity or lack thereof.
  void SetWritable(bool writable) { set_writable(writable); }
  void SetReceiving(bool receiving) { set_receiving(receiving); }

  // Simulates the two transports connecting to each other.
  // If `asymmetric` is true this method only affects this FakePacketTransport.
  // If false, it affects `dest` as well.
  void SetDestination(FakePacketTransport* dest, bool asymmetric) {
    if (dest) {
      dest_ = dest;
      set_writable(true);
      if (!asymmetric) {
        dest->SetDestination(this, true);
      }
    } else {
      // Simulates loss of connectivity, by asymmetrically forgetting dest_.
      dest_ = nullptr;
      set_writable(false);
    }
  }

  // Fake PacketTransportInternal implementation.
  const std::string& transport_name() const override { return transport_name_; }
  bool writable() const override { return writable_; }
  bool receiving() const override { return receiving_; }
  int SendPacket(const char* data,
                 size_t len,
                 const AsyncSocketPacketOptions& options,
                 int /* flags */) override {
    if (!dest_ || error_ != 0) {
      return -1;
    }
    CopyOnWriteBuffer packet(absl::string_view(data, len));
    SendPacketInternal(packet, options);

    SentPacketInfo sent_packet(options.packet_id,
                               env_.clock().TimeInMilliseconds());
    // Because handlers of NotifySentPacket may be sending packets,
    // dispatch this call to a new task.
    TaskQueueBase::Current()->PostTask(
        SafeTask(safety_.flag(),
                 [this, sent_packet] { NotifySentPacket(this, sent_packet); }));
    return static_cast<int>(len);
  }

  int SetOption(Socket::Option opt, int value) override {
    options_[opt] = value;
    return 0;
  }

  bool GetOption(Socket::Option opt, int* value) override {
    auto it = options_.find(opt);
    if (it == options_.end()) {
      return false;
    }
    *value = it->second;
    return true;
  }

  int GetError() override { return error_; }
  void SetError(int error) { error_ = error; }

  const CopyOnWriteBuffer* last_sent_packet() { return &last_sent_packet_; }
  const AsyncSocketPacketOptions& last_sent_packet_options() const {
    return last_sent_packet_options_;
  }

  std::optional<NetworkRoute> network_route() const override {
    return network_route_;
  }
  void SetNetworkRoute(std::optional<NetworkRoute> network_route) {
    network_route_ = network_route;
    NotifyNetworkRouteChanged(network_route);
  }

  using PacketTransportInternal::NotifyOnClose;
  using PacketTransportInternal::NotifyPacketReceived;

 private:
  void set_writable(bool writable) {
    if (writable_ == writable) {
      return;
    }
    writable_ = writable;
    if (writable_) {
      NotifyReadyToSend(this);
    }
    NotifyWritableState(this);
  }

  void set_receiving(bool receiving) {
    if (receiving_ == receiving) {
      return;
    }
    receiving_ = receiving;
    NotifyReceivingState(this);
  }

  void SendPacketInternal(const CopyOnWriteBuffer& packet,
                          const AsyncSocketPacketOptions& options) {
    last_sent_packet_ = packet;
    last_sent_packet_options_ = options;
    if (dest_) {
      dest_->NotifyPacketReceived(ReceivedIpPacket(
          packet, SocketAddress(), env_.clock().CurrentTime(),
          options.ect_1 ? EcnMarking::kEct1 : EcnMarking::kNotEct));
    }
  }

  const Environment env_;
  CopyOnWriteBuffer last_sent_packet_;
  AsyncSocketPacketOptions last_sent_packet_options_;
  std::string transport_name_;
  FakePacketTransport* dest_ = nullptr;
  bool writable_ = false;
  bool receiving_ = false;

  std::map<Socket::Option, int> options_;
  int error_ = 0;

  std::optional<NetworkRoute> network_route_;
  ScopedTaskSafety safety_;
};

}  //  namespace webrtc


#endif  // P2P_TEST_FAKE_PACKET_TRANSPORT_H_
