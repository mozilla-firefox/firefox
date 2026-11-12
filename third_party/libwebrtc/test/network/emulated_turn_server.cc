/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "test/network/emulated_turn_server.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>

#include "api/async_dns_resolver.h"
#include "api/environment/environment.h"
#include "api/packet_socket_factory.h"
#include "api/sequence_checker.h"
#include "api/test/network_emulation/network_emulation_interfaces.h"
#include "api/test/network_emulation_manager.h"
#include "p2p/test/turn_server.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/checks.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/network/received_packet.h"
#include "rtc_base/socket.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/task_queue_for_test.h"
#include "rtc_base/thread.h"

namespace webrtc {
namespace test {

namespace {

const char kTestRealm[] = "example.org";
const char kTestSoftware[] = "TestTurnServer";

// A wrapper class for webrtc::TurnServer to allocate sockets.
class PacketSocketFactoryWrapper : public PacketSocketFactory {
 public:
  explicit PacketSocketFactoryWrapper(EmulatedTURNServer* turn_server)
      : turn_server_(turn_server) {}
  ~PacketSocketFactoryWrapper() override {}

  // This method is called from TurnServer when making a TURN ALLOCATION.
  // It will create a socket on the `peer_` endpoint.
  std::unique_ptr<AsyncPacketSocket> CreateUdpSocket(
      const Environment& env,
      const SocketAddress& address,
      uint16_t min_port,
      uint16_t max_port) override {
    return turn_server_->CreatePeerSocket();
  }

  std::unique_ptr<AsyncListenSocket> CreateServerTcpSocket(
      const Environment& env,
      const SocketAddress& local_address,
      uint16_t min_port,
      uint16_t max_port,
      int opts) override {
    return nullptr;
  }
  std::unique_ptr<AsyncPacketSocket> CreateClientTcpSocket(
      const Environment& env,
      const SocketAddress& local_address,
      const SocketAddress& remote_address,
      const PacketSocketTcpOptions& tcp_options) override {
    return nullptr;
  }
  std::unique_ptr<AsyncDnsResolverInterface> CreateAsyncDnsResolver() override {
    return nullptr;
  }

  std::unique_ptr<AsyncPacketSocket> CreateClientUdpSocket(
      const Environment& env,
      const SocketAddress& local_address,
      const SocketAddress& remote_address,
      uint16_t min_port,
      uint16_t max_port,
      const PacketSocketTcpOptions& udp_options) override {
    return nullptr;
  }

 private:
  EmulatedTURNServer* turn_server_;
};

}  //  namespace

// A wrapper class for copying data between an AsyncPacketSocket and a
// EmulatedEndpoint. This is used by the webrtc::TurnServer when
// sending data back into the emulated network.
class EmulatedTURNServer::AsyncPacketSocketWrapper : public AsyncPacketSocket {
 public:
  AsyncPacketSocketWrapper(test::EmulatedTURNServer* turn_server,
                           EmulatedEndpoint* endpoint,
                           uint16_t port)
      : turn_server_(turn_server),
        endpoint_(endpoint),
        local_address_(SocketAddress(endpoint_->GetPeerLocalAddress(), port)) {}
  ~AsyncPacketSocketWrapper() override { turn_server_->Unbind(local_address_); }

  SocketAddress GetLocalAddress() const override { return local_address_; }
  SocketAddress GetRemoteAddress() const override { return SocketAddress(); }
  int Send(const void* pv,
           size_t cb,
           const AsyncSocketPacketOptions& options) override {
    RTC_CHECK(false) << "TCP not implemented";
    return -1;
  }
  int SendTo(const void* pv,
             size_t cb,
             const SocketAddress& addr,
             const AsyncSocketPacketOptions& options) override {
    // Copy from webrtc::AsyncPacketSocket to EmulatedEndpoint.
    CopyOnWriteBuffer buf(std::span(reinterpret_cast<const uint8_t*>(pv), cb));
    endpoint_->SendPacket(local_address_, addr, buf);
    return cb;
  }
  int Close() override { return 0; }
  void NotifyPacketReceived(const ReceivedIpPacket& packet) {
    AsyncPacketSocket::NotifyPacketReceived(packet);
  }

  AsyncPacketSocket::State GetState() const override {
    return AsyncPacketSocket::STATE_BOUND;
  }
  int GetOption(Socket::Option opt, int* value) override { return 0; }
  int SetOption(Socket::Option opt, int value) override { return 0; }
  int GetError() const override { return 0; }
  void SetError(int error) override {}

 private:
  test::EmulatedTURNServer* const turn_server_;
  EmulatedEndpoint* const endpoint_;
  const SocketAddress local_address_;
};

EmulatedTURNServer::EmulatedTURNServer(const Environment& env,
                                       const EmulatedTURNServerConfig& config,
                                       std::unique_ptr<Thread> thread,
                                       EmulatedEndpoint* client,
                                       EmulatedEndpoint* peer)
    : thread_(std::move(thread)), client_(client), peer_(peer) {
  ice_config_.username = "keso";
  ice_config_.password = "keso";
  SendTask(thread_.get(), [&] {
    RTC_DCHECK_RUN_ON(thread_.get());
    turn_server_ = std::make_unique<TurnServer>(env, thread_.get());
    turn_server_->set_realm(kTestRealm);
    turn_server_->set_realm(kTestSoftware);
    turn_server_->set_auth_hook(this);
    turn_server_->set_enable_permission_checks(config.enable_permission_checks);

    std::unique_ptr<AsyncPacketSocket> client_socket = Wrap(client_);
    client_address_ = client_socket->GetLocalAddress();
    turn_server_->AddInternalSocket(std::move(client_socket), PROTO_UDP);
    turn_server_->SetExternalSocketFactory(new PacketSocketFactoryWrapper(this),
                                           SocketAddress());
    StringBuilder sb;
    sb << "turn:" << client_address_.ToString() << "?transport=udp";
    ice_config_.url = sb.Release();
  });
}

void EmulatedTURNServer::Stop() {
  SendTask(thread_.get(), [this]() {
    RTC_DCHECK_RUN_ON(thread_.get());
    sockets_.clear();
  });
}

EmulatedTURNServer::~EmulatedTURNServer() {
  SendTask(thread_.get(), [this]() {
    RTC_DCHECK_RUN_ON(thread_.get());
    turn_server_.reset(nullptr);
  });
}

std::unique_ptr<AsyncPacketSocket> EmulatedTURNServer::Wrap(
    EmulatedEndpoint* endpoint) {
  RTC_DCHECK_RUN_ON(thread_.get());
  auto port = endpoint->BindReceiver(0, this).value();
  auto socket =
      std::make_unique<AsyncPacketSocketWrapper>(this, endpoint, port);
  sockets_[SocketAddress(endpoint->GetPeerLocalAddress(), port)] = socket.get();
  return socket;
}

void EmulatedTURNServer::OnPacketReceived(EmulatedIpPacket packet) {
  // Copy from EmulatedEndpoint to webrtc::AsyncPacketSocket.
  thread_->PostTask([this, packet(std::move(packet))]() {
    RTC_DCHECK_RUN_ON(thread_.get());
    auto it = sockets_.find(packet.to);
    if (it != sockets_.end()) {
      it->second->NotifyPacketReceived(
          ReceivedIpPacket(packet.data, packet.from, packet.arrival_time));
    }
  });
}

void EmulatedTURNServer::Unbind(SocketAddress address) {
  RTC_DCHECK_RUN_ON(thread_.get());
  if (GetClientEndpoint()->GetPeerLocalAddress() == address.ipaddr()) {
    GetClientEndpoint()->UnbindReceiver(address.port());
  } else {
    GetPeerEndpoint()->UnbindReceiver(address.port());
  }
  sockets_.erase(address);
}

}  // namespace test
}  // namespace webrtc
