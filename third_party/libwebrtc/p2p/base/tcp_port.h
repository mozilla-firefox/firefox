/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef P2P_BASE_TCP_PORT_H_
#define P2P_BASE_TCP_PORT_H_

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <span>
#include <utility>

#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "api/candidate.h"
#include "api/environment/environment.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/transport/stun.h"
#include "api/units/time_delta.h"
#include "p2p/base/connection.h"
#include "p2p/base/port.h"
#include "p2p/base/port_interface.h"
#include "p2p/base/stun_request.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/checks.h"
#include "rtc_base/containers/flat_map.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/network/received_packet.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/socket.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/weak_ptr.h"

namespace webrtc {

class TCPConnection;

// Communicates using a local TCP port.
//
// This class is designed to allow subclasses to take advantage of the
// connection management provided by this class.  A subclass should take of all
// packet sending and preparation, but when a packet is received, it should
// call this TCPPort::OnReadPacket (3 arg) to dispatch to a connection.
class TCPPort : public Port {
 public:
  static std::unique_ptr<TCPPort> Create(PortParametersRef args,
                                         uint16_t min_port,
                                         uint16_t max_port,
                                         bool allow_listen) {
    // Using `new` to access a non-public constructor.
    return absl::WrapUnique(
        new TCPPort(std::move(args), min_port, max_port, allow_listen));
  }

  ~TCPPort() override;

  Connection* CreateConnection(const Candidate& address,
                               CandidateOrigin origin) override;

  void PrepareAddress() override;

  // Options apply to accepted sockets.
  // TODO(bugs.webrtc.org/13065): Apply also to outgoing and existing
  // connections.
  int GetOption(Socket::Option opt, int* value) override;
  int SetOption(Socket::Option opt, int value) override;
  int GetError() override;
  bool SupportsProtocol(absl::string_view protocol) const override;
  ProtocolType GetProtocol() const override;

 protected:
  TCPPort(PortParametersRef args,
          uint16_t min_port,
          uint16_t max_port,
          bool allow_listen);

  // Handles sending using the local TCP socket.
  int SendTo(std::span<const uint8_t> data,
             const SocketAddress& addr,
             const AsyncSocketPacketOptions& options,
             bool payload) override;
  // Accepts incoming TCP connection.

  void OnNewConnection(AsyncListenSocket* socket,
                       AsyncPacketSocket* new_socket);

 private:
  struct Incoming {
    SocketAddress addr;
    AsyncPacketSocket* socket;
  };

  void TryCreateServerSocket();

  AsyncPacketSocket* GetIncoming(const SocketAddress& addr,
                                 bool remove = false);

  // Receives packet signal from the local TCP Socket.
  void OnReadPacket(AsyncPacketSocket* socket, const ReceivedIpPacket& packet);

  void OnSentPacket(AsyncPacketSocket* socket,
                    const SentPacketInfo& sent_packet) override;

  void OnReadyToSend(AsyncPacketSocket* socket);

  bool allow_listen_;
  std::unique_ptr<AsyncListenSocket> listen_socket_;
  // Options to be applied to accepted sockets.
  // TODO(bugs.webrtc:13065): Configure connect/accept in the same way, but
  // currently, setting OPT_NODELAY for client sockets is done (unconditionally)
  // by BasicPacketSocketFactory::CreateClientTcpSocket.
  flat_map<Socket::Option, int> socket_options_;

  int error_;
  std::list<Incoming> incoming_;

  friend class TCPConnection;
};

class TCPConnection : public Connection {
 public:
  // Connection is outgoing unless socket is specified
  TCPConnection(const Environment& env,
                WeakPtr<Port> tcp_port,
                const Candidate& candidate,
                AsyncPacketSocket* socket = nullptr);
  ~TCPConnection() override;

  int Send(std::span<const uint8_t> data,
           const AsyncSocketPacketOptions& options) override;
  int GetError() override;

  AsyncPacketSocket* socket() { return socket_.get(); }

  TimeDelta reconnection_timeout() const { return reconnection_timeout_; }
  void SetReconnectionTimeoutForTesting(TimeDelta timeout) {
    reconnection_timeout_ = timeout;
  }

 protected:
  // Set waiting_for_stun_binding_complete_ to false to allow data packets in
  // addition to what Port::OnConnectionRequestResponse does.
  void OnConnectionRequestResponse(StunRequest* req,
                                   StunMessage* response) override;

 private:
  friend class TCPPort;  // For `MaybeReconnect()`.

  // Helper function to handle the case when Ping or Send fails with error
  // related to socket close.
  void MaybeReconnect();

  void CreateOutgoingTcpSocket() RTC_RUN_ON(network_thread());

  void ConnectSocketSignals(AsyncPacketSocket* socket)
      RTC_RUN_ON(network_thread());

  void DisconnectSocketSignals(AsyncPacketSocket* socket)
      RTC_RUN_ON(network_thread());

  void OnConnect(AsyncPacketSocket* socket);
  void OnClose(AsyncPacketSocket* socket, int error);
  void OnSentPacket(AsyncPacketSocket* socket,
                    const SentPacketInfo& sent_packet);
  void OnReadPacket(AsyncPacketSocket* socket, const ReceivedIpPacket& packet);
  void OnReadyToSend(AsyncPacketSocket* socket);
  void OnDestroyed(Connection* c);

  TCPPort* tcp_port() {
    RTC_DCHECK_EQ(port()->GetProtocol(), PROTO_TCP);
    return static_cast<TCPPort*>(port());
  }

  std::unique_ptr<AsyncPacketSocket> socket_;
  int error_;
  const bool outgoing_;

  // Guard against multiple outgoing tcp connection during a reconnect.
  bool connection_pending_;

  // Guard against data packets sent when we reconnect a TCP connection. During
  // reconnecting, when a new tcp connection has being made, we can't send data
  // packets out until the STUN binding is completed (i.e. the write state is
  // set to WRITABLE again by Connection::OnConnectionRequestResponse). IPC
  // socket, when receiving data packets before that, will trigger OnError which
  // will terminate the newly created connection.
  bool pretending_to_be_writable_;

  TimeDelta reconnection_timeout_;

  ScopedTaskSafety network_safety_;
};

}  //  namespace webrtc


#endif  // P2P_BASE_TCP_PORT_H_
