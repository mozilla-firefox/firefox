/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

/*
 *  This is a diagram of how TCP reconnect works for the active side. The
 *  passive side just waits for an incoming connection.
 *
 *  - Connected: Indicate whether the TCP socket is connected.
 *
 *  - Writable: Whether the stun binding is completed. Sending a data packet
 *    before stun binding completed will trigger IPC socket layer to shutdown
 *    the connection.
 *
 *  - PendingTCP: `connection_pending_` indicates whether there is an
 *    outstanding TCP connection in progress.
 *
 *  - PretendWri: Tracked by `pretending_to_be_writable_`. Marking connection as
 *    WRITE_TIMEOUT will cause the connection be deleted. Instead, we're
 *    "pretending" we're still writable for a period of time such that reconnect
 *    could work.
 *
 *  Data could only be sent in state 3. Sening data during state 2 & 6 will get
 *  EWOULDBLOCK, 4 & 5 EPIPE.
 *
 *         OS Timeout         7 -------------+
 *   +----------------------->|Connected: N  |
 *   |                        |Writable:  N  |     Timeout
 *   |       Timeout          |Connection is |<----------------+
 *   |   +------------------->|Dead          |                 |
 *   |   |                    +--------------+                 |
 *   |   |                               ^                     |
 *   |   |            OnClose            |                     |
 *   |   |    +-----------------------+  |                     |
 *   |   |    |                       |  |Timeout              |
 *   |   |    v                       |  |                     |
 *   | 4 +----------+          5 -----+--+--+           6 -----+-----+
 *   | |Connected: N|Send() or |Connected: N|           |Connected: Y|
 *   | |Writable:  Y|Ping()    |Writable:  Y|OnConnect  |Writable:  Y|
 *   | |PendingTCP:N+--------> |PendingTCP:Y+---------> |PendingTCP:N|
 *   | |PretendWri:Y|          |PretendWri:Y|           |PretendWri:Y|
 *   | +-----+------+          +------------+           +---+--+-----+
 *   |   ^   ^                                              |  |
 *   |   |   |                     OnClose                  |  |
 *   |   |   +----------------------------------------------+  |
 *   |   |                                                     |
 *   |   |                              Stun Binding Completed |
 *   |   |                                                     |
 *   |   |                    OnClose                          |
 *   |   +------------------------------------------------+    |
 *   |                                                    |    v
 *  1 -----------+           2 -----------+Stun      3 -----------+
 *  |Connected: N|           |Connected: Y|Binding   |Connected: Y|
 *  |Writable:  N|OnConnect  |Writable:  N|Completed |Writable:  Y|
 *  |PendingTCP:Y+---------> |PendingTCP:N+--------> |PendingTCP:N|
 *  |PretendWri:N|           |PretendWri:N|          |PretendWri:N|
 *  +------------+           +------------+          +------------+
 *
 */

#include "p2p/base/tcp_port.h"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <list>
#include <span>
#include <utility>

#include "absl/algorithm/container.h"
#include "absl/strings/string_view.h"
#include "api/candidate.h"
#include "api/environment/environment.h"
#include "api/packet_socket_factory.h"
#include "api/sequence_checker.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/transport/stun.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "p2p/base/connection.h"
#include "p2p/base/connection_info.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/port.h"
#include "p2p/base/port_interface.h"
#include "p2p/base/stun_request.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/checks.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/logging.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/network/received_packet.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/socket.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/weak_ptr.h"

namespace webrtc {

TCPPort::TCPPort(PortParametersRef args,
                 uint16_t min_port,
                 uint16_t max_port,
                 bool allow_listen)
    : Port(std::move(args), IceCandidateType::kHost, min_port, max_port),
      allow_listen_(allow_listen),
      error_(0) {
  // TODO(mallinath) - Set preference value as per RFC 6544.
  // http://b/issue?id=7141794
  if (allow_listen_) {
    TryCreateServerSocket();
  }
  // Set TCP_NODELAY (via OPT_NODELAY) for improved performance; this causes
  // small media packets to be sent immediately rather than being buffered up,
  // reducing latency.
  SetOption(Socket::OPT_NODELAY, 1);
}

TCPPort::~TCPPort() {
  listen_socket_ = nullptr;
  std::list<Incoming>::iterator it;
  for (it = incoming_.begin(); it != incoming_.end(); ++it)
    delete it->socket;
  incoming_.clear();
}

Connection* TCPPort::CreateConnection(const Candidate& address,
                                      CandidateOrigin origin) {
  if (!SupportsProtocol(address.protocol())) {
    return nullptr;
  }

  if ((address.tcptype() == TCPTYPE_ACTIVE_STR && !address.is_prflx()) ||
      (address.tcptype().empty() && address.address().port() == 0)) {
    // It's active only candidate, we should not try to create connections
    // for these candidates.
    return nullptr;
  }

  // We can't accept TCP connections incoming on other ports
  if (origin == ORIGIN_OTHER_PORT)
    return nullptr;

  // We don't know how to act as an ssl server yet
  if ((address.protocol() == SSLTCP_PROTOCOL_NAME) &&
      (origin == ORIGIN_THIS_PORT)) {
    return nullptr;
  }

  if (!IsCompatibleAddress(address.address())) {
    return nullptr;
  }

  TCPConnection* conn = nullptr;
  if (AsyncPacketSocket* socket = GetIncoming(address.address(), true)) {
    // Incoming connection; we already created a socket and connected signals,
    // so we need to hand off the "read packet" responsibility to
    // TCPConnection.
    socket->DeregisterReceivedPacketCallback();
    conn = new TCPConnection(env(), NewWeakPtr(), address, socket);
  } else {
    // Outgoing connection, which will create a new socket.
    conn = new TCPConnection(env(), NewWeakPtr(), address);
  }
  AddOrReplaceConnection(conn);
  return conn;
}

void TCPPort::PrepareAddress() {
  if (listen_socket_) {
    // Socket may be in the CLOSED state if Listen()
    // failed, we still want to add the socket address.
    RTC_LOG(LS_VERBOSE) << "Preparing TCP address, current state: "
                        << static_cast<int>(listen_socket_->GetState());
    AddAddress(
        listen_socket_->GetLocalAddress(), listen_socket_->GetLocalAddress(),
        SocketAddress(), TCP_PROTOCOL_NAME, "", TCPTYPE_PASSIVE_STR,
        IceCandidateType::kHost, ICE_TYPE_PREFERENCE_HOST_TCP, 0, "", true);
  } else {
    RTC_LOG(LS_INFO) << ToString()
                     << ": Not listening due to firewall restrictions.";
    // Note: We still add the address, since otherwise the remote side won't
    // recognize our incoming TCP connections. According to
    // https://tools.ietf.org/html/rfc6544#section-4.5, for active candidate,
    // the port must be set to the discard port, i.e. 9. We can't be 100% sure
    // which IP address will actually be used, so GetBestIP is as good as we
    // can do.
    // TODO(deadbeef): We could do something like create a dummy socket just to
    // see what IP we get. But that may be overkill.
    AddAddress(SocketAddress(Network()->GetBestIP(), DISCARD_PORT),
               SocketAddress(Network()->GetBestIP(), 0), SocketAddress(),
               TCP_PROTOCOL_NAME, "", TCPTYPE_ACTIVE_STR,
               IceCandidateType::kHost, ICE_TYPE_PREFERENCE_HOST_TCP, 0, "",
               true);
  }
}

int TCPPort::SendTo(std::span<const uint8_t> data,
                    const SocketAddress& addr,
                    const AsyncSocketPacketOptions& options,
                    bool payload) {
  AsyncPacketSocket* socket = nullptr;
  TCPConnection* conn = static_cast<TCPConnection*>(GetConnection(addr));

  // For Connection, this is the code path used by Ping() to establish
  // WRITABLE. It has to send through the socket directly as TCPConnection::Send
  // checks writability.
  if (conn) {
    if (!conn->connected()) {
      conn->MaybeReconnect();
      return SOCKET_ERROR;
    }
    socket = conn->socket();
    if (!socket) {
      // The failure to initialize should have been logged elsewhere,
      // so this log is not important.
      RTC_LOG(LS_INFO) << ToString()
                       << ": Attempted to send to an uninitialized socket: "
                       << addr.ToSensitiveString();
      error_ = EHOSTUNREACH;
      return SOCKET_ERROR;
    }
  } else {
    socket = GetIncoming(addr);
    if (!socket) {
      RTC_LOG(LS_ERROR) << ToString()
                        << ": Attempted to send to an unknown destination: "
                        << addr.ToSensitiveString();
      error_ = EHOSTUNREACH;
      return SOCKET_ERROR;
    }
  }
  AsyncSocketPacketOptions modified_options(options);
  CopyPortInformationToPacketInfo(&modified_options.info_signaled_after_sent);
  int sent = socket->Send(data.data(), data.size(), modified_options);
  if (sent < 0) {
    error_ = socket->GetError();
    // Error from this code path for a Connection (instead of from a bare
    // socket) will not trigger reconnecting. In theory, this shouldn't matter
    // as OnClose should always be called and set connected to false.
    RTC_LOG(LS_ERROR) << ToString() << ": TCP send of " << data.size()
                      << " bytes failed with error " << error_;
  }
  return sent;
}

int TCPPort::GetOption(Socket::Option opt, int* value) {
  auto const& it = socket_options_.find(opt);
  if (it == socket_options_.end()) {
    return -1;
  }
  *value = it->second;
  return 0;
}

int TCPPort::SetOption(Socket::Option opt, int value) {
  socket_options_[opt] = value;
  return 0;
}

int TCPPort::GetError() {
  return error_;
}

bool TCPPort::SupportsProtocol(absl::string_view protocol) const {
  return protocol == TCP_PROTOCOL_NAME || protocol == SSLTCP_PROTOCOL_NAME;
}

ProtocolType TCPPort::GetProtocol() const {
  return PROTO_TCP;
}

void TCPPort::OnNewConnection(AsyncListenSocket* socket,
                              AsyncPacketSocket* new_socket) {
  RTC_DCHECK_EQ(socket, listen_socket_.get());

  for (const auto& option : socket_options_) {
    new_socket->SetOption(option.first, option.second);
  }
  Incoming incoming;
  incoming.addr = new_socket->GetRemoteAddress();
  incoming.socket = new_socket;
  incoming.socket->RegisterReceivedPacketCallback(
      [&](AsyncPacketSocket* socket, const ReceivedIpPacket& packet) {
        OnReadPacket(socket, packet);
      });
  incoming.socket->SubscribeReadyToSend(
      this, [this](AsyncPacketSocket* socket) { OnReadyToSend(socket); });
  incoming.socket->SubscribeSentPacket(
      this, [this](AsyncPacketSocket* socket, const SentPacketInfo& info) {
        OnSentPacket(socket, info);
      });

  RTC_LOG(LS_VERBOSE) << ToString() << ": Accepted connection from "
                      << incoming.addr.ToSensitiveString();
  incoming_.push_back(incoming);
}

void TCPPort::TryCreateServerSocket() {
  listen_socket_ = socket_factory()->CreateServerTcpSocket(
      env(), SocketAddress(Network()->GetBestIP(), 0), min_port(), max_port(),
      /*opts=*/0);
  if (!listen_socket_) {
    RTC_LOG(LS_WARNING)
        << ToString()
        << ": TCP server socket creation failed; continuing anyway.";
    return;
  }
  listen_socket_->SubscribeNewConnection(
      this, [this](AsyncListenSocket* listen_socket,
                   AsyncPacketSocket* packet_socket) {
        OnNewConnection(listen_socket, packet_socket);
      });
}

AsyncPacketSocket* TCPPort::GetIncoming(const SocketAddress& addr,
                                        bool remove) {
  AsyncPacketSocket* socket = nullptr;
  for (std::list<Incoming>::iterator it = incoming_.begin();
       it != incoming_.end(); ++it) {
    if (it->addr == addr) {
      socket = it->socket;
      if (remove)
        incoming_.erase(it);
      break;
    }
  }
  return socket;
}

void TCPPort::OnReadPacket(AsyncPacketSocket* socket,
                           const ReceivedIpPacket& packet) {
  Port::OnReadPacket(packet, PROTO_TCP);
}

void TCPPort::OnSentPacket(AsyncPacketSocket* socket,
                           const SentPacketInfo& sent_packet) {
  NotifySentPacket(sent_packet);
}

void TCPPort::OnReadyToSend(AsyncPacketSocket* socket) {
  Port::OnReadyToSend();
}

// TODO(qingsi): `kConnectionWriteConnectTimeout` is overriden by
// `ice_unwritable_timeout` in IceConfig when determining the writability state.
// Replace this constant with the config parameter assuming the default value if
// we decide it is also applicable here.
TCPConnection::TCPConnection(const Environment& env,
                             WeakPtr<Port> tcp_port,
                             const Candidate& candidate,
                             AsyncPacketSocket* socket)
    : Connection(env, std::move(tcp_port), 0, candidate),
      socket_(socket),
      error_(0),
      outgoing_(socket == nullptr),
      connection_pending_(false),
      pretending_to_be_writable_(false),
      reconnection_timeout_(kConnectionWriteConnectTimeout) {
  RTC_DCHECK_RUN_ON(network_thread());
  RTC_DCHECK_EQ(port()->GetProtocol(),
                PROTO_TCP);  // Needs to be TCPPort.

  SubscribeDestroyed(
      this, [this](Connection* connection) { OnDestroyed(connection); });

  if (outgoing_) {
    CreateOutgoingTcpSocket();
  } else {
    // Incoming connections should match one of the network addresses. Same as
    // what's being checked in OnConnect, but just DCHECKing here.
    RTC_LOG(LS_VERBOSE) << ToString() << ": socket ipaddr: "
                        << socket_->GetLocalAddress().ToSensitiveString()
                        << ", port() Network:" << port()->Network()->ToString();
    RTC_DCHECK(absl::c_any_of(
        port()->Network()->GetIPs(), [this](const InterfaceAddress& addr) {
          return socket_->GetLocalAddress().ipaddr() == addr;
        }));
    ConnectSocketSignals(socket);
  }
}

TCPConnection::~TCPConnection() {
  RTC_DCHECK_RUN_ON(network_thread());
}

int TCPConnection::Send(std::span<const uint8_t> data,
                        const AsyncSocketPacketOptions& options) {
  if (!socket_) {
    error_ = ENOTCONN;
    return SOCKET_ERROR;
  }

  // Sending after OnClose on active side will trigger a reconnect for a
  // outgoing connection. Note that the write state is still WRITABLE as we want
  // to spend a few seconds attempting a reconnect before saying we're
  // unwritable.
  if (!connected()) {
    MaybeReconnect();
    return SOCKET_ERROR;
  }

  // Note that this is important to put this after the previous check to give
  // the connection a chance to reconnect.
  if (pretending_to_be_writable_ || write_state() != STATE_WRITABLE) {
    // TODO(?): Should STATE_WRITE_TIMEOUT return a non-blocking error?
    error_ = ENOTCONN;
    return SOCKET_ERROR;
  }
  mutable_stats().sent_total_packets++;
  AsyncSocketPacketOptions modified_options(options);
  tcp_port()->CopyPortInformationToPacketInfo(
      &modified_options.info_signaled_after_sent);
  int sent = socket_->Send(data.data(), data.size(), modified_options);
  Timestamp now = env().clock().CurrentTime();
  if (sent < 0) {
    mutable_stats().sent_discarded_packets++;
    error_ = socket_->GetError();
  } else {
    AddSentBytesToStats(sent, now);
  }
  set_last_send_data(now);
  return sent;
}

int TCPConnection::GetError() {
  return error_;
}

void TCPConnection::OnSentPacket(AsyncPacketSocket* socket,
                                 const SentPacketInfo& sent_packet) {
  RTC_DCHECK_RUN_ON(network_thread());
  if (port()) {
    port()->NotifySentPacket(sent_packet);
  }
}

void TCPConnection::OnConnectionRequestResponse(StunRequest* req,
                                                StunMessage* response) {
  // Process the STUN response before we inform upper layer ready to send.
  Connection::OnConnectionRequestResponse(req, response);

  // If we're in the state of pretending to be writeable, we should inform the
  // upper layer it's ready to send again as previous EWOULDLBLOCK from socket
  // would have stopped the outgoing stream.
  if (pretending_to_be_writable_) {
    Connection::OnReadyToSend();
  }
  pretending_to_be_writable_ = false;
  RTC_DCHECK(write_state() == STATE_WRITABLE);
}

void TCPConnection::OnConnect(AsyncPacketSocket* socket) {
  RTC_DCHECK_EQ(socket, socket_.get());

  if (port() == nullptr) {
    RTC_LOG(LS_ERROR) << "TCPConnection: Port has been deleted.";
    return;
  }

  // Do not use this port if the socket bound to an address not associated with
  // the desired network interface. This is seen in Chrome, where TCP sockets
  // cannot be given a binding address, and the platform is expected to pick
  // the correct local address.
  //
  // However, there are two situations in which we allow the bound address to
  // not be one of the addresses of the requested interface:
  // 1. The bound address is the loopback address. This happens when a proxy
  // forces TCP to bind to only the localhost address (see issue 3927).
  // 2. The bound address is the "any address". This happens when
  // multiple_routes is disabled (see issue 4780).
  //
  // Note that, aside from minor differences in log statements, this logic is
  // identical to that in TurnPort.
  const SocketAddress& socket_address = socket->GetLocalAddress();
  if (absl::c_any_of(port()->Network()->GetIPs(),
                     [socket_address](const InterfaceAddress& addr) {
                       return socket_address.ipaddr() == addr;
                     })) {
    RTC_LOG(LS_VERBOSE) << ToString() << ": Connection established to "
                        << socket->GetRemoteAddress().ToSensitiveString();
  } else {
    if (socket->GetLocalAddress().IsLoopbackIP()) {
      RTC_LOG(LS_WARNING) << "Socket is bound to the address:"
                          << socket_address.ipaddr().ToSensitiveString()
                          << ", rather than an address associated with network:"
                          << port()->Network()->ToString()
                          << ". Still allowing it since it's localhost.";
    } else if (IPIsAny(port()->Network()->GetBestIP())) {
      RTC_LOG(LS_WARNING)
          << "Socket is bound to the address:"
          << socket_address.ipaddr().ToSensitiveString()
          << ", rather than an address associated with network:"
          << port()->Network()->ToString()
          << ". Still allowing it since it's the 'any' address"
             ", possibly caused by multiple_routes being disabled.";
    } else {
      RTC_LOG(LS_WARNING) << "Dropping connection as TCP socket bound to IP "
                          << socket_address.ipaddr().ToSensitiveString()
                          << ", rather than an address associated with network:"
                          << port()->Network()->ToString();
      OnClose(socket, 0);
      return;
    }
  }

  // Connection is established successfully.
  set_connected(true);
  connection_pending_ = false;
}

void TCPConnection::OnClose(AsyncPacketSocket* socket, int error) {
  RTC_DCHECK_RUN_ON(network_thread());
  RTC_DCHECK_EQ(socket, socket_.get());
  RTC_LOG(LS_INFO) << ToString() << ": Connection closed with error " << error;

  if (port() == nullptr) {
    RTC_LOG(LS_ERROR) << "TCPConnection: Port has been deleted.";
    return;
  }

  // Guard against the condition where IPC socket will call OnClose for every
  // packet it can't send.
  if (connected()) {
    set_connected(false);

    // Prevent the connection from being destroyed by redundant SignalClose
    // events.
    pretending_to_be_writable_ = true;

    // If this connection can't become connected and writable again in 5
    // seconds, it's time to tear this down. This is the case for the original
    // TCP connection on passive side during a reconnect.
    // We don't attempt reconnect right here. This is to avoid a case where the
    // shutdown is intentional and reconnect is not necessary. We only reconnect
    // when the connection is used to Send() or Ping().
    network_thread()->PostDelayedTask(
        SafeTask(network_safety_.flag(),
                 [this]() {
                   if (pretending_to_be_writable_ && port() != nullptr) {
                     Destroy();
                   }
                 }),
        reconnection_timeout());
  } else if (!pretending_to_be_writable_) {
    // OnClose could be called when the underneath socket times out during the
    // initial connect() (i.e. `pretending_to_be_writable_` is false) . We have
    // to manually destroy here as this connection, as never connected, will not
    // be scheduled for ping to trigger destroy.
    DisconnectSocketSignals(socket_.get());
    port()->DestroyConnectionAsync(this);
  }
}

void TCPConnection::MaybeReconnect() {
  RTC_DCHECK_RUN_ON(network_thread());
  // Only reconnect for an outgoing TCPConnection when OnClose was signaled and
  // no outstanding reconnect is pending.
  if (connected() || connection_pending_ || !outgoing_) {
    return;
  }

  RTC_LOG(LS_INFO) << ToString()
                   << ": TCP Connection with remote is closed, "
                      "trying to reconnect";

  CreateOutgoingTcpSocket();
  error_ = EPIPE;
}

void TCPConnection::OnReadPacket(AsyncPacketSocket* socket,
                                 const ReceivedIpPacket& packet) {
  RTC_DCHECK_RUN_ON(network_thread());
  RTC_DCHECK_EQ(socket, socket_.get());
  Connection::OnReadPacket(packet);
}

void TCPConnection::OnReadyToSend(AsyncPacketSocket* socket) {
  RTC_DCHECK_RUN_ON(network_thread());
  RTC_DCHECK_EQ(socket, socket_.get());
  Connection::OnReadyToSend();
}

void TCPConnection::OnDestroyed(Connection* c) {
  RTC_DCHECK_RUN_ON(network_thread());
  RTC_DCHECK_EQ(c, this);
  if (socket_) {
    DisconnectSocketSignals(socket_.get());
  }
}

void TCPConnection::CreateOutgoingTcpSocket() {
  RTC_DCHECK(outgoing_);
  int opts = (remote_candidate().protocol() == SSLTCP_PROTOCOL_NAME)
                 ? PacketSocketFactory::OPT_TLS_FAKE
                 : 0;

  if (socket_) {
    DisconnectSocketSignals(socket_.get());
  }

  PacketSocketTcpOptions tcp_opts;
  tcp_opts.opts = opts;
  socket_ = port()->socket_factory()->CreateClientTcpSocket(
      env(), SocketAddress(port()->Network()->GetBestIP(), 0),
      remote_candidate().address(), tcp_opts);
  if (socket_) {
    RTC_LOG(LS_VERBOSE) << ToString() << ": Connecting from "
                        << socket_->GetLocalAddress().ToSensitiveString()
                        << " to "
                        << remote_candidate().address().ToSensitiveString();
    set_connected(false);
    connection_pending_ = true;
    ConnectSocketSignals(socket_.get());
  } else {
    RTC_LOG(LS_WARNING) << ToString() << ": Failed to create connection to "
                        << remote_candidate().address().ToSensitiveString();
    set_state(IceCandidatePairState::FAILED);
    // We can't FailAndPrune directly here. FailAndPrune and deletes all
    // the StunRequests from the request_map_. And if this is in the stack
    // of Connection::Ping(), we are still using the request.
    // Unwind the stack and defer the FailAndPrune.
    network_thread()->PostTask(
        SafeTask(network_safety_.flag(), [this]() { FailAndPrune(); }));
  }
}

void TCPConnection::ConnectSocketSignals(AsyncPacketSocket* socket) {
  // Incoming connections register SignalSentPacket and SignalReadyToSend
  // directly on the port in TCPPort::OnNewConnection.
  if (outgoing_) {
    socket->SubscribeConnect(
        this, [this](AsyncPacketSocket* socket) { OnConnect(socket); });
    socket->SubscribeSentPacket(
        this, [this](AsyncPacketSocket* socket, const SentPacketInfo& info) {
          OnSentPacket(socket, info);
        });
    socket->SubscribeReadyToSend(
        this, [this](AsyncPacketSocket* socket) { OnReadyToSend(socket); });
  }

  // For incoming connections, this re-register ReceivedPacketCallback to the
  // connection instead of the port.
  socket->RegisterReceivedPacketCallback(
      [&](AsyncPacketSocket* socket, const ReceivedIpPacket& packet) {
        OnReadPacket(socket, packet);
      });
  socket->SubscribeCloseEvent(this, [this, safety = network_safety_.flag()](
                                        AsyncPacketSocket* s, int err) {
    if (safety->alive())
      OnClose(s, err);
  });
}

void TCPConnection::DisconnectSocketSignals(AsyncPacketSocket* socket) {
  if (outgoing_) {
    // Incoming connections do not register these signals in TCPConnection.
    socket->UnsubscribeConnect(this);
    socket->UnsubscribeReadyToSend(this);
    socket->UnsubscribeSentPacket(this);
  }
  socket->DeregisterReceivedPacketCallback();
  socket->UnsubscribeCloseEvent(this);
}

}  // namespace webrtc
