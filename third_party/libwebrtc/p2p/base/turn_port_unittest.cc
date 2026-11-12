/*
 *  Copyright 2012 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "p2p/base/turn_port.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/candidate.h"
#include "api/environment/environment.h"
#include "api/local_network_access_permission.h"
#include "api/packet_socket_factory.h"
#include "api/test/mock_async_dns_resolver.h"
#include "api/test/mock_local_network_access_permission.h"
#include "api/test/rtc_error_matchers.h"
#include "api/transport/ecn_marking.h"
#include "api/transport/stun.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "p2p/base/basic_packet_socket_factory.h"
#include "p2p/base/connection.h"
#include "p2p/base/connection_info.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/port.h"
#include "p2p/base/port_allocator.h"
#include "p2p/base/port_interface.h"
#include "p2p/base/stun_port.h"
#include "p2p/base/stun_request.h"
#include "p2p/base/transport_description.h"
#include "p2p/client/relay_port_factory_interface.h"
#include "p2p/test/mock_dns_resolving_packet_socket_factory.h"
#include "p2p/test/test_turn_customizer.h"
#include "p2p/test/test_turn_server.h"
#include "p2p/test/turn_server.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/buffer.h"
#include "rtc_base/byte_buffer.h"
#include "rtc_base/checks.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/net_helpers.h"
#include "rtc_base/network.h"
#include "rtc_base/network/received_packet.h"
#include "rtc_base/socket.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/thread.h"
#include "rtc_base/virtual_socket_server.h"
#include "system_wrappers/include/metrics.h"
#include "test/create_test_environment.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/time_controller/simulated_time_controller.h"
#include "test/wait_until.h"

#if defined(WEBRTC_POSIX)
#include <dirent.h>  // IWYU pragma: keep
#endif

namespace webrtc {

namespace {
using ::testing::_;
using ::testing::Eq;
using ::testing::IsTrue;
using ::testing::Ne;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnPointee;

const SocketAddress kLocalAddr1("11.11.11.11", 0);
const SocketAddress kLocalAddr2("22.22.22.22", 0);
const SocketAddress kLocalIPv6Addr("2401:fa00:4:1000:be30:5bff:fee5:c3", 0);
const SocketAddress kLocalIPv6Addr2("2401:fa00:4:2000:be30:5bff:fee5:d4", 0);
const SocketAddress kTurnUdpIntAddr("99.99.99.3", TURN_SERVER_PORT);
const SocketAddress kTurnTcpIntAddr("99.99.99.4", TURN_SERVER_PORT);
const SocketAddress kTurnUdpExtAddr("99.99.99.5", 0);
const SocketAddress kTurnAlternateIntAddr("99.99.99.6", TURN_SERVER_PORT);
// Port for redirecting to a TCP Web server. Should not work.
const SocketAddress kTurnDangerousAddr("99.99.99.7", 81);
// Port 53 (the DNS port); should work.
const SocketAddress kTurnPort53Addr("99.99.99.7", 53);
// Port 80 (the HTTP port); should work.
const SocketAddress kTurnPort80Addr("99.99.99.7", 80);
// Port 443 (the HTTPS port); should work.
const SocketAddress kTurnPort443Addr("99.99.99.7", 443);
// The default TURN server port.
const SocketAddress kTurnIntAddr("99.99.99.7", TURN_SERVER_PORT);
const SocketAddress kTurnIPv6IntAddr("2400:4030:2:2c00:be30:abcd:efab:cdef",
                                     TURN_SERVER_PORT);
const SocketAddress kTurnUdpIPv6IntAddr("2400:4030:1:2c00:be30:abcd:efab:cdef",
                                        TURN_SERVER_PORT);
const SocketAddress kTurnInvalidAddr("www.google.invalid.", 3478);
const SocketAddress kTurnValidAddr("www.google.valid.", 3478);

constexpr char kCandidateFoundation[] = "foundation";
constexpr char kIceUfrag1[] = "TESTICEUFRAG0001";
constexpr char kIceUfrag2[] = "TESTICEUFRAG0002";
constexpr char kIcePwd1[] = "TESTICEPWD00000000000001";
constexpr char kIcePwd2[] = "TESTICEPWD00000000000002";
constexpr char kTurnUsername[] = "test";
constexpr char kTurnPassword[] = "test";
// This test configures the virtual socket server to simulate delay so that we
// can verify operations take no more than the expected number of round trips.
constexpr TimeDelta kSimulatedRtt = TimeDelta::Millis(50);
// Connection destruction may happen asynchronously, but it should only
// take one simulated clock tick.
constexpr TimeDelta kConnectionDestructionDelay = TimeDelta::Millis(1);
// This used to be 1 second, but that's not always enough for getaddrinfo().
// See: https://bugs.chromium.org/p/webrtc/issues/detail?id=5191
constexpr TimeDelta kResolverTimeout = TimeDelta::Millis(10000);

constexpr uint64_t kTiebreakerDefault = 44444;

const ProtocolAddress kTurnUdpProtoAddr(kTurnUdpIntAddr, PROTO_UDP);
const ProtocolAddress kTurnTcpProtoAddr(kTurnTcpIntAddr, PROTO_TCP);
const ProtocolAddress kTurnTlsProtoAddr(kTurnTcpIntAddr, PROTO_TLS);
const ProtocolAddress kTurnUdpIPv6ProtoAddr(kTurnUdpIPv6IntAddr, PROTO_UDP);
const ProtocolAddress kTurnDangerousProtoAddr(kTurnDangerousAddr, PROTO_TCP);
const ProtocolAddress kTurnPort53ProtoAddr(kTurnPort53Addr, PROTO_TCP);
const ProtocolAddress kTurnPort80ProtoAddr(kTurnPort80Addr, PROTO_TCP);
const ProtocolAddress kTurnPort443ProtoAddr(kTurnPort443Addr, PROTO_TCP);
const ProtocolAddress kTurnPortInvalidHostnameProtoAddr(kTurnInvalidAddr,
                                                        PROTO_UDP);
const ProtocolAddress kTurnPortValidHostnameProtoAddr(kTurnValidAddr,
                                                      PROTO_UDP);

#if defined(WEBRTC_LINUX) && !defined(WEBRTC_ANDROID)
int GetFDCount() {
  struct dirent* dp;
  int fd_count = 0;
  DIR* dir = opendir("/proc/self/fd/");
  while ((dp = readdir(dir)) != nullptr) {
    if (dp->d_name[0] == '.')
      continue;
    ++fd_count;
  }
  closedir(dir);
  return fd_count;
}
#endif

}  // unnamed namespace

class TurnPortTestVirtualSocketServer : public VirtualSocketServer {
 public:
  TurnPortTestVirtualSocketServer() {
    // This configures the virtual socket server to always add a simulated
    // delay of exactly half of kSimulatedRtt.
    set_delay_mean((kSimulatedRtt / 2).ms());
    UpdateDelayDistribution();
  }

  using VirtualSocketServer::LookupBinding;
};

class TestConnectionWrapper {
 public:
  explicit TestConnectionWrapper(Connection* conn) : connection_(conn) {
    conn->SubscribeDestroyed(this, [this](Connection* connection) {
      OnConnectionDestroyed(connection);
    });
  }

  ~TestConnectionWrapper() {
    if (connection_) {
      connection_->UnsubscribeDestroyed(this);
    }
  }

  Connection* connection() { return connection_; }

 private:
  void OnConnectionDestroyed(Connection* conn) {
    ASSERT_TRUE(conn == connection_);
    connection_ = nullptr;
  }

  Connection* connection_;
};

// Note: This test uses a fake clock with a simulated network round trip
// (between local port and TURN server) of kSimulatedRtt.
class TurnPortTest : public ::testing::Test {
 public:
  TurnPortTest()
      : ss_(new TurnPortTestVirtualSocketServer()),
        time_controller_(Timestamp::Seconds(1), ss_.get()),
        env_(CreateTestEnvironment({.time = &time_controller_})),
        main_(time_controller_.GetMainThread()),
        turn_server_(env_, main_, ss_.get(), kTurnUdpIntAddr, kTurnUdpExtAddr),
        socket_factory_(ss_.get()) {}

  void OnTurnPortComplete(Port* port) { turn_ready_ = true; }
  void OnTurnPortError(Port* port) { turn_error_ = true; }
  void OnCandidateError(Port* port, const IceCandidateErrorEvent& event) {
    error_event_ = event;
  }
  void OnTurnUnknownAddress(PortInterface* port,
                            const SocketAddress& addr,
                            ProtocolType proto,
                            IceMessage* msg,
                            const std::string& rf,
                            bool /*port_muxed*/) {
    turn_unknown_address_ = true;
  }
  void OnUdpPortComplete(Port* port) { udp_ready_ = true; }
  void OnSocketReadPacket(AsyncPacketSocket* socket,
                          const ReceivedIpPacket& packet) {
    turn_port_->HandleIncomingPacket(socket, packet);
  }
  void OnTurnPortDestroyed(PortInterface* port) { turn_port_destroyed_ = true; }

  bool HasPermission(const IPAddress& addr) {
    TurnServerAllocation* alloc =
        turn_server_.FindAllocation(turn_port_->socket()->GetLocalAddress());
    return alloc && alloc->HasPermission(addr);
  }

  void OnConnectionSignalDestroyed(Connection* connection) {
    connection->DeregisterReceivedPacketCallback();
  }

  Socket* CreateServerSocket(const SocketAddress addr) {
    Socket* socket = ss_->CreateSocket(AF_INET, SOCK_STREAM);
    EXPECT_GE(socket->Bind(addr), 0);
    EXPECT_GE(socket->Listen(5), 0);
    return socket;
  }

  Network* MakeNetwork(const SocketAddress& addr) {
    networks_.emplace_back("unittest", "unittest", addr.ipaddr(), 32);
    networks_.back().AddIP(addr.ipaddr());
    return &networks_.back();
  }

  bool CreateTurnPort(absl::string_view username,
                      absl::string_view password,
                      const ProtocolAddress& server_address,
                      absl::string_view turn_logging_id = "") {
    return CreateTurnPortWithAllParams(MakeNetwork(kLocalAddr1), username,
                                       password, server_address,
                                       turn_logging_id);
  }
  bool CreateTurnPort(const SocketAddress& local_address,
                      absl::string_view username,
                      absl::string_view password,
                      const ProtocolAddress& server_address) {
    return CreateTurnPortWithAllParams(MakeNetwork(local_address), username,
                                       password, server_address);
  }

  bool CreateTurnPortWithNetwork(const Network* network,
                                 absl::string_view username,
                                 absl::string_view password,
                                 const ProtocolAddress& server_address) {
    return CreateTurnPortWithAllParams(network, username, password,
                                       server_address);
  }

  // Version of CreateTurnPort that takes all possible parameters; all other
  // helper methods call this, such that "SetIceRole" and "ConnectSignals" (and
  // possibly other things in the future) only happen in one place.
  bool CreateTurnPortWithAllParams(const Network* network,
                                   absl::string_view username,
                                   absl::string_view password,
                                   const ProtocolAddress& server_address,
                                   absl::string_view turn_logging_id = "") {
    RelayServerConfig config;
    config.credentials = RelayCredentials(username, password);
    config.turn_logging_id = std::string(turn_logging_id);
    CreateRelayPortArgs args = {.env = env_};
    args.network_thread = main_;
    args.socket_factory = socket_factory();
    args.network = network;
    args.username = kIceUfrag1;
    args.password = kIcePwd1;
    args.server_address = &server_address;
    args.config = &config;
    args.turn_customizer = turn_customizer_.get();
    args.ice_tiebreaker = kTiebreakerDefault;
    args.lna_permission_factory = lna_permission_factory_.get();
    args.callbacks = GetCallbacks();

    turn_port_ = TurnPort::Create(std::move(args), 0, 0);
    if (turn_port_ == nullptr) {
      return false;
    }
    // This TURN port will be the controlling.
    turn_port_->SetIceRole(ICEROLE_CONTROLLING);
    turn_port_->SetOption(Socket::OPT_RECV_ECN, 1);

    if (server_address.proto == PROTO_TLS) {
      // The test TURN server has a self-signed certificate so will not pass
      // the normal client validation. Instruct the client to ignore certificate
      // errors for testing only.
      turn_port_->SetTlsCertPolicy(
          TlsCertPolicy::TLS_CERT_POLICY_INSECURE_NO_CHECK);
    }
    return true;
  }

  void CreateSharedTurnPort(absl::string_view username,
                            absl::string_view password,
                            const ProtocolAddress& server_address) {
    RTC_CHECK(server_address.proto == PROTO_UDP);

    if (!socket_) {
      socket_ = socket_factory()->CreateUdpSocket(
          env_, SocketAddress(kLocalAddr1.ipaddr(), 0), 0, 0);
      ASSERT_TRUE(socket_ != nullptr);
      socket_->RegisterReceivedPacketCallback(
          [&](AsyncPacketSocket* socket, const ReceivedIpPacket& packet) {
            OnSocketReadPacket(socket, packet);
          });
    }

    RelayServerConfig config;
    config.credentials = RelayCredentials(username, password);
    CreateRelayPortArgs args = {.env = env_};
    args.network_thread = main_;
    args.socket_factory = socket_factory();
    args.network = MakeNetwork(kLocalAddr1);
    args.username = kIceUfrag1;
    args.password = kIcePwd1;
    args.server_address = &server_address;
    args.config = &config;
    args.turn_customizer = turn_customizer_.get();
    args.ice_tiebreaker = kTiebreakerDefault;
    args.callbacks = GetCallbacks();

    turn_port_ = TurnPort::Create(std::move(args), socket_.get());
    // This TURN port will be the controlling.
    turn_port_->SetIceRole(ICEROLE_CONTROLLING);
  }

  Port::Callbacks GetCallbacks() {
    Port::Callbacks callbacks;
    callbacks.tag = this;
    callbacks.port_complete = [this](Port* port) { OnTurnPortComplete(port); };
    callbacks.port_error = [this](Port* port) { OnTurnPortError(port); };
    callbacks.candidate_error = [this](Port* port,
                                       const IceCandidateErrorEvent& event) {
      OnCandidateError(port, event);
    };
    callbacks.port_destroyed = [this](PortInterface* port) {
      OnTurnPortDestroyed(port);
    };
    callbacks.unknown_address = [this](PortInterface* port,
                                       const SocketAddress& address,
                                       ProtocolType proto, IceMessage* msg,
                                       const std::string& rf, bool port_muxed) {
      OnTurnUnknownAddress(port, address, proto, msg, rf, port_muxed);
    };
    return callbacks;
  }

  void CreateUdpPort() { CreateUdpPort(kLocalAddr2); }

  void CreateUdpPort(const SocketAddress& address) {
    Port::Callbacks callbacks;
    callbacks.tag = this;
    callbacks.port_complete = [this](Port* port) { OnUdpPortComplete(port); };
    udp_port_ = UDPPort::Create({.env = env_,
                                 .network_thread = main_,
                                 .socket_factory = socket_factory(),
                                 .network = MakeNetwork(address),
                                 .ice_username_fragment = kIceUfrag2,
                                 .ice_password = kIcePwd2,
                                 .ice_tiebreaker = kTiebreakerDefault,
                                 .callbacks = std::move(callbacks)},
                                0, 0, false, std::nullopt);
    // UDP port will be controlled.
    udp_port_->SetIceRole(ICEROLE_CONTROLLED);
    udp_port_->SetOption(Socket::OPT_RECV_ECN, 1);
  }

  void PrepareTurnAndUdpPorts(ProtocolType protocol_type) {
    // turn_port_ should have been created.
    ASSERT_TRUE(turn_port_ != nullptr);
    turn_port_->PrepareAddress();
    ASSERT_TRUE(WaitUntil([&] { return turn_ready_; },
                          {.timeout = TimeToGetTurnCandidate(protocol_type),
                           .clock = &time_controller_}));

    CreateUdpPort();
    udp_port_->PrepareAddress();
    ASSERT_TRUE(
        WaitUntil([&] { return udp_ready_; },
                  {.timeout = kSimulatedRtt, .clock = &time_controller_}));
  }

  // Returns the fake clock time to establish a connection over the given
  // protocol.
  TimeDelta TimeToConnect(ProtocolType protocol_type) {
    switch (protocol_type) {
      case PROTO_TCP:
        // The virtual socket server will delay by a fixed half a round trip
        // for a TCP connection.
        return kSimulatedRtt / 2;
      case PROTO_TLS:
        // TLS operates over TCP and additionally has a round of HELLO for
        // negotiating ciphers and a round for exchanging certificates.
        return 2 * kSimulatedRtt + TimeToConnect(PROTO_TCP);
      case PROTO_UDP:
      default:
        // UDP requires no round trips to set up the connection.
        return TimeDelta::Zero();
    }
  }

  // Returns the total fake clock time to establish a connection with a TURN
  // server over the given protocol and to allocate a TURN candidate.
  TimeDelta TimeToGetTurnCandidate(ProtocolType protocol_type) {
    // For a simple allocation, the first Allocate message will return with an
    // error asking for credentials and will succeed after the second Allocate
    // message.
    return 2 * kSimulatedRtt + TimeToConnect(protocol_type);
  }

  // Total fake clock time to do the following:
  // 1. Connect to primary TURN server
  // 2. Send Allocate and receive a redirect from the primary TURN server
  // 3. Connect to alternate TURN server
  // 4. Send Allocate and receive a request for credentials
  // 5. Send Allocate with credentials and receive allocation
  TimeDelta TimeToGetAlternateTurnCandidate(ProtocolType protocol_type) {
    return 3 * kSimulatedRtt + 2 * TimeToConnect(protocol_type);
  }

  bool CheckConnectionFailedAndPruned(Connection* conn) {
    return conn && !conn->active() &&
           conn->state() == IceCandidatePairState::FAILED;
  }

  // Checks that `turn_port_` has a nonempty set of connections and they are all
  // failed and pruned.
  bool CheckAllConnectionsFailedAndPruned() {
    auto& connections = turn_port_->connections();
    if (connections.empty()) {
      return false;
    }
    for (const auto& kv : connections) {
      if (!CheckConnectionFailedAndPruned(kv.second.get())) {
        return false;
      }
    }
    return true;
  }

  void TestTurnAllocateSucceeds(TimeDelta timeout) {
    ASSERT_TRUE(turn_port_);
    turn_port_->PrepareAddress();
    EXPECT_TRUE(WaitUntil([&] { return turn_ready_; },
                          {.timeout = timeout, .clock = &time_controller_}));
    ASSERT_EQ(1U, turn_port_->Candidates().size());
    EXPECT_EQ(kTurnUdpExtAddr.ipaddr(),
              turn_port_->Candidates()[0].address().ipaddr());
    EXPECT_NE(0, turn_port_->Candidates()[0].address().port());
  }

  void TestReconstructedServerUrl(ProtocolType protocol_type,
                                  absl::string_view expected_url) {
    ASSERT_TRUE(turn_port_);
    turn_port_->PrepareAddress();
    ASSERT_TRUE(WaitUntil([&] { return turn_ready_; },
                          {.timeout = TimeToGetTurnCandidate(protocol_type),
                           .clock = &time_controller_}));
    ASSERT_EQ(1U, turn_port_->Candidates().size());
    EXPECT_EQ(turn_port_->Candidates()[0].url(), expected_url);
  }

  void TestTurnAlternateServer(ProtocolType protocol_type) {
    std::vector<SocketAddress> redirect_addresses;
    redirect_addresses.push_back(kTurnAlternateIntAddr);

    TestTurnRedirector redirector(redirect_addresses);

    turn_server_.AddInternalSocket(kTurnIntAddr, protocol_type);
    turn_server_.AddInternalSocket(kTurnAlternateIntAddr, protocol_type);
    turn_server_.set_redirect_hook(&redirector);
    CreateTurnPort(kTurnUsername, kTurnPassword,
                   ProtocolAddress(kTurnIntAddr, protocol_type));

    // Retrieve the address before we run the state machine.
    const SocketAddress old_addr = turn_port_->server_address().address;

    turn_port_->PrepareAddress();
    EXPECT_TRUE(
        WaitUntil([&] { return turn_ready_; },
                  {.timeout = TimeToGetAlternateTurnCandidate(protocol_type),
                   .clock = &time_controller_}));
    // Retrieve the address again, the turn port's address should be
    // changed.
    const SocketAddress new_addr = turn_port_->server_address().address;
    EXPECT_NE(old_addr, new_addr);
    ASSERT_EQ(1U, turn_port_->Candidates().size());
    EXPECT_EQ(kTurnUdpExtAddr.ipaddr(),
              turn_port_->Candidates()[0].address().ipaddr());
    EXPECT_NE(0, turn_port_->Candidates()[0].address().port());
  }

  void TestTurnAlternateServerV4toV6(ProtocolType protocol_type) {
    std::vector<SocketAddress> redirect_addresses;
    redirect_addresses.push_back(kTurnIPv6IntAddr);

    TestTurnRedirector redirector(redirect_addresses);
    turn_server_.AddInternalSocket(kTurnIntAddr, protocol_type);
    turn_server_.set_redirect_hook(&redirector);
    CreateTurnPort(kTurnUsername, kTurnPassword,
                   ProtocolAddress(kTurnIntAddr, protocol_type));
    turn_port_->PrepareAddress();
    // Need time to connect to TURN server, send Allocate request and receive
    // redirect notice.
    EXPECT_TRUE(
        WaitUntil([&] { return turn_error_; },
                  {.timeout = kSimulatedRtt + TimeToConnect(protocol_type),
                   .clock = &time_controller_}));
  }

  void TestTurnAlternateServerPingPong(ProtocolType protocol_type) {
    std::vector<SocketAddress> redirect_addresses;
    redirect_addresses.push_back(kTurnAlternateIntAddr);
    redirect_addresses.push_back(kTurnIntAddr);

    TestTurnRedirector redirector(redirect_addresses);

    turn_server_.AddInternalSocket(kTurnIntAddr, protocol_type);
    turn_server_.AddInternalSocket(kTurnAlternateIntAddr, protocol_type);
    turn_server_.set_redirect_hook(&redirector);
    CreateTurnPort(kTurnUsername, kTurnPassword,
                   ProtocolAddress(kTurnIntAddr, protocol_type));

    turn_port_->PrepareAddress();
    EXPECT_TRUE(
        WaitUntil([&] { return turn_error_; },
                  {.timeout = TimeToGetAlternateTurnCandidate(protocol_type),
                   .clock = &time_controller_}));
    ASSERT_EQ(0U, turn_port_->Candidates().size());
    SocketAddress address;
    // Verify that we have exhausted all alternate servers instead of
    // failure caused by other errors.
    EXPECT_FALSE(redirector.ShouldRedirect(address, &address));
  }

  void TestTurnAlternateServerDetectRepetition(ProtocolType protocol_type) {
    std::vector<SocketAddress> redirect_addresses;
    redirect_addresses.push_back(kTurnAlternateIntAddr);
    redirect_addresses.push_back(kTurnAlternateIntAddr);

    TestTurnRedirector redirector(redirect_addresses);

    turn_server_.AddInternalSocket(kTurnIntAddr, protocol_type);
    turn_server_.AddInternalSocket(kTurnAlternateIntAddr, protocol_type);
    turn_server_.set_redirect_hook(&redirector);
    CreateTurnPort(kTurnUsername, kTurnPassword,
                   ProtocolAddress(kTurnIntAddr, protocol_type));

    turn_port_->PrepareAddress();
    EXPECT_TRUE(
        WaitUntil([&] { return turn_error_; },
                  {.timeout = TimeToGetAlternateTurnCandidate(protocol_type),
                   .clock = &time_controller_}));
    ASSERT_EQ(0U, turn_port_->Candidates().size());
  }

  // Redirects the allocation to `redirect_address` and verifies that the
  // redirect is treated as an error, i.e. that the allocation fails and that
  // nothing is sent to `redirect_address`.
  void TestTurnAlternateServerBlocked(ProtocolType protocol_type,
                                      bool ipv6,
                                      const SocketAddress& redirect_address) {
    const SocketAddress& local_address = ipv6 ? kLocalIPv6Addr : kLocalAddr1;
    const SocketAddress& server_address =
        ipv6 ? kTurnIPv6IntAddr : kTurnIntAddr;

    std::vector<SocketAddress> redirect_addresses;
    redirect_addresses.push_back(redirect_address);

    // Make a socket and bind it to the redirect address, to make extra sure no
    // packet is sent to this address.
    std::unique_ptr<Socket> redirect_socket(ss_->CreateSocket(
        AF_INET, protocol_type == PROTO_UDP ? SOCK_DGRAM : SOCK_STREAM));
    ASSERT_NE(nullptr, redirect_socket.get());
    ASSERT_EQ(0, redirect_socket->Bind(redirect_address));
    if (protocol_type != PROTO_UDP) {
      ASSERT_EQ(0, redirect_socket->Listen(1));
    }

    TestTurnRedirector redirector(redirect_addresses);

    turn_server_.AddInternalSocket(server_address, protocol_type);
    turn_server_.set_redirect_hook(&redirector);
    CreateTurnPort(local_address, kTurnUsername, kTurnPassword,
                   ProtocolAddress(server_address, protocol_type));

    turn_port_->PrepareAddress();
    EXPECT_TRUE(WaitUntil([&] { return turn_error_; },
                          {.timeout = TimeToGetTurnCandidate(protocol_type),
                           .clock = &time_controller_}));
    EXPECT_EQ(server_address, turn_port_->server_address().address);

    // Wait for some extra time, and make sure no packets were received on the
    // redirect port we created (or in the case of TCP and TLS, no connection
    // attempt occurred).
    time_controller_.AdvanceTime(kSimulatedRtt);
    if (protocol_type == PROTO_UDP) {
      char buf[1];
      EXPECT_EQ(-1, redirect_socket->Recv(&buf, 1, nullptr));
    } else {
      std::unique_ptr<Socket> accepted_socket(redirect_socket->Accept(nullptr));
      EXPECT_EQ(nullptr, accepted_socket.get());
    }
  }

  // A certain security exploit works by redirecting to a loopback address,
  // which doesn't ever actually make sense. So redirects to loopback should
  // be treated as errors.
  // See: https://bugs.chromium.org/p/chromium/issues/detail?id=649118
  void TestTurnAlternateServerLoopback(ProtocolType protocol_type, bool ipv6) {
    // Pick an unusual address in the 127.0.0.0/8 range to make sure more than
    // 127.0.0.1 is covered.
    TestTurnAlternateServerBlocked(
        protocol_type, ipv6,
        SocketAddress(ipv6 ? "::1" : "127.1.2.3", TURN_SERVER_PORT));
  }

  // Sending to the unspecified address (0.0.0.0 or ::) reaches the local host
  // on some platforms, so redirects to it must be treated as errors, just like
  // redirects to loopback.
  // See: https://crbug.com/502821327
  void TestTurnAlternateServerAnyAddress(ProtocolType protocol_type,
                                         bool ipv6) {
    TestTurnAlternateServerBlocked(
        protocol_type, ipv6,
        SocketAddress(ipv6 ? "::" : "0.0.0.0", TURN_SERVER_PORT));
  }

  // Redirects the allocation to `kTurnAlternateIntAddr`. Unlike connecting to
  // the original server, connecting to the alternate server requires Local
  // Network Access permission, and the permission request is responded to
  // with `status`. Verifies that the allocation only succeeds if the
  // permission is granted.
  void TestTurnAlternateServerLocalNetworkAccess(
      ProtocolType protocol_type,
      LocalNetworkAccessPermissionStatus status) {
    auto lna_permission_factory =
        std::make_unique<MockLocalNetworkAccessPermissionFactory>();
    EXPECT_CALL(*lna_permission_factory, Create()).WillRepeatedly([status] {
      auto permission =
          std::make_unique<NiceMock<MockLocalNetworkAccessPermission>>();
      ON_CALL(*permission, ShouldRequestPermission(kTurnAlternateIntAddr))
          .WillByDefault(Return(true));
      ON_CALL(*permission, RequestPermission(_, _))
          .WillByDefault(
              [status](
                  const SocketAddress& /* addr */,
                  absl::AnyInvocable<void(LocalNetworkAccessPermissionStatus)>
                      callback) {
                Thread::Current()->PostTask(
                    [callback = std::move(callback), status]() mutable {
                      std::move(callback)(status);
                    });
              });
      return permission;
    });
    lna_permission_factory_ = std::move(lna_permission_factory);

    std::vector<SocketAddress> redirect_addresses;
    redirect_addresses.push_back(kTurnAlternateIntAddr);
    TestTurnRedirector redirector(redirect_addresses);

    turn_server_.AddInternalSocket(kTurnIntAddr, protocol_type);
    turn_server_.AddInternalSocket(kTurnAlternateIntAddr, protocol_type);
    turn_server_.set_redirect_hook(&redirector);
    CreateTurnPort(kTurnUsername, kTurnPassword,
                   ProtocolAddress(kTurnIntAddr, protocol_type));

    turn_port_->PrepareAddress();
    const bool granted = status == LocalNetworkAccessPermissionStatus::kGranted;
    EXPECT_TRUE(
        WaitUntil([&] { return granted ? turn_ready_ : turn_error_; },
                  {.timeout = TimeToGetAlternateTurnCandidate(protocol_type),
                   .clock = &time_controller_}));
    EXPECT_EQ(granted ? 1U : 0U, turn_port_->Candidates().size());
  }

  void TestTurnConnection(ProtocolType protocol_type) {
    // Create ports and prepare addresses.
    PrepareTurnAndUdpPorts(protocol_type);

    // Send ping from UDP to TURN.
    ASSERT_GE(turn_port_->Candidates().size(), 1U);
    Connection* conn1 = udp_port_->CreateConnection(turn_port_->Candidates()[0],
                                                    Port::ORIGIN_MESSAGE);
    ASSERT_TRUE(conn1 != nullptr);
    conn1->Ping();
    EXPECT_TRUE(
        WaitUntil([&] { return !turn_unknown_address_; },
                  {.timeout = kSimulatedRtt * 2, .clock = &time_controller_}));
    EXPECT_FALSE(turn_unknown_address_);
    EXPECT_FALSE(conn1->receiving());
    EXPECT_EQ(Connection::STATE_WRITE_INIT, conn1->write_state());

    // Send ping from TURN to UDP.
    Connection* conn2 = turn_port_->CreateConnection(udp_port_->Candidates()[0],
                                                     Port::ORIGIN_MESSAGE);
    ASSERT_TRUE(conn2 != nullptr);
    time_controller_.AdvanceTime(kSimulatedRtt);
    ASSERT_TRUE(HasPermission(udp_port_->Candidates()[0].address().ipaddr()));
    conn2->Ping();

    // Two hops from TURN port to UDP port through TURN server, thus two RTTs.
    EXPECT_THAT(
        WaitUntil([&] { return conn2->write_state(); },
                  Eq(Connection::STATE_WRITABLE),
                  {.timeout = kSimulatedRtt * 2, .clock = &time_controller_}),
        IsRtcOk());
    EXPECT_TRUE(conn1->receiving());
    EXPECT_TRUE(conn2->receiving());
    EXPECT_EQ(Connection::STATE_WRITE_INIT, conn1->write_state());

    // Send another ping from UDP to TURN.
    conn1->Ping();
    EXPECT_THAT(
        WaitUntil([&] { return conn1->write_state(); },
                  Eq(Connection::STATE_WRITABLE),
                  {.timeout = kSimulatedRtt * 2, .clock = &time_controller_}),
        IsRtcOk());
    EXPECT_TRUE(conn2->receiving());
  }

  void TestDestroyTurnConnection() {
    PrepareTurnAndUdpPorts(PROTO_UDP);

    // Create connections on both ends.
    Connection* conn1 = udp_port_->CreateConnection(turn_port_->Candidates()[0],
                                                    Port::ORIGIN_MESSAGE);
    Connection* conn2 = turn_port_->CreateConnection(udp_port_->Candidates()[0],
                                                     Port::ORIGIN_MESSAGE);

    // Increased to 10 minutes, to ensure that the TurnEntry times out before
    // the TurnPort.
    turn_port_->set_timeout_delay(10 * 60 * 1000);

    ASSERT_TRUE(conn2 != nullptr);
    time_controller_.AdvanceTime(kSimulatedRtt);
    ASSERT_TRUE(HasPermission(udp_port_->Candidates()[0].address().ipaddr()));
    // Make sure turn connection can receive.
    conn1->Ping();
    EXPECT_THAT(
        WaitUntil([&] { return conn1->write_state(); },
                  Eq(Connection::STATE_WRITABLE),
                  {.timeout = kSimulatedRtt * 2, .clock = &time_controller_}),
        IsRtcOk());
    EXPECT_FALSE(turn_unknown_address_);

    // Destroy the connection on the TURN port. The TurnEntry still exists, so
    // the TURN port should still process a ping from an unknown address.
    turn_port_->DestroyConnection(conn2);

    conn1->Ping();
    EXPECT_TRUE(
        WaitUntil([&] { return turn_unknown_address_; },
                  {.timeout = kSimulatedRtt, .clock = &time_controller_}));

    // Wait for TurnEntry to expire. Timeout is 5 minutes.
    // Expect that it still processes an incoming ping and signals the
    // unknown address.
    turn_unknown_address_ = false;
    time_controller_.AdvanceTime(TimeDelta::Seconds(5 * 60));

    // TODO(chromium:1395625): When `TurnPort` doesn't find connection objects
    // for incoming packets, it forwards calls to the parent class, `Port`. This
    // happens inside `TurnPort::DispatchPacket`. The `Port` implementation may
    // need to send a binding error back over a connection which, unless the
    // `TurnPort` implementation handles it, could result in a null deref.
    // This special check tests if dispatching messages via `TurnPort` for which
    // there's no connection, results in a no-op rather than crashing.
    // See `TurnPort::SendBindingErrorResponse` for the check.
    // This should probably be done in a neater way both from a testing pov and
    // how incoming messages are handled in the `Port` class, when an assumption
    // is made about connection objects existing and when those assumptions
    // may not hold.
    std::string pwd = conn1->remote_password_for_test();
    conn1->set_remote_password_for_test("bad");
    auto msg = conn1->BuildPingRequestForTest();

    ByteBufferWriter buf;
    msg->Write(&buf);
    conn1->Send(buf.DataView(), options);

    // Now restore the password before continuing.
    conn1->set_remote_password_for_test(pwd);

    conn1->Ping();
    EXPECT_TRUE(
        WaitUntil([&] { return turn_unknown_address_; },
                  {.timeout = kSimulatedRtt, .clock = &time_controller_}));

    // If the connection is created again, it will start to receive pings.
    conn2 = turn_port_->CreateConnection(udp_port_->Candidates()[0],
                                         Port::ORIGIN_MESSAGE);
    conn1->Ping();
    EXPECT_TRUE(
        WaitUntil([&] { return conn2->receiving(); },
                  {.timeout = kSimulatedRtt, .clock = &time_controller_}));
  }

  void TestTurnSendData(ProtocolType protocol_type, bool expect_ecn_propagate) {
    PrepareTurnAndUdpPorts(protocol_type);

    // Create connections and send pings.
    Connection* conn1 = turn_port_->CreateConnection(udp_port_->Candidates()[0],
                                                     Port::ORIGIN_MESSAGE);
    Connection* conn2 = udp_port_->CreateConnection(turn_port_->Candidates()[0],
                                                    Port::ORIGIN_MESSAGE);
    ASSERT_TRUE(conn1 != nullptr);
    ASSERT_TRUE(conn2 != nullptr);
    conn1->RegisterReceivedPacketCallback(
        [&](Connection* connection, const ReceivedIpPacket& packet) {
          turn_packets_.emplace_back(packet);
        });

    conn1->SubscribeDestroyed(this, [this](Connection* connection) {
      OnConnectionSignalDestroyed(connection);
    });
    conn2->RegisterReceivedPacketCallback(
        [&](Connection* connection, const ReceivedIpPacket& packet) {
          udp_packets_.emplace_back(packet);
        });
    conn2->SubscribeDestroyed(this, [this](Connection* connection) {
      OnConnectionSignalDestroyed(connection);
    });
    conn1->Ping();
    EXPECT_THAT(
        WaitUntil([&] { return conn1->write_state(); },
                  Eq(Connection::STATE_WRITABLE),
                  {.timeout = kSimulatedRtt * 2, .clock = &time_controller_}),
        IsRtcOk());
    conn2->Ping();
    EXPECT_THAT(
        WaitUntil([&] { return conn2->write_state(); },
                  Eq(Connection::STATE_WRITABLE),
                  {.timeout = kSimulatedRtt * 2, .clock = &time_controller_}),
        IsRtcOk());

    // Send some data.
    size_t num_packets = 256;
    for (size_t i = 0; i < num_packets; ++i) {
      std::array<uint8_t, 256> buf;
      uint8_t val = 0xFF;
      std::generate(buf.begin(), buf.begin() + i + 1, [&val] { return val--; });
      options.ect_1 = (i % 2 == 0);
      conn1->Send(std::span(buf).first(i + 1), options);
      conn2->Send(std::span(buf).first(i + 1), options);
      time_controller_.AdvanceTime(kSimulatedRtt);
    }

    // Check the data.
    ASSERT_EQ(num_packets, turn_packets_.size());
    ASSERT_EQ(num_packets, udp_packets_.size());
    for (size_t i = 0; i < num_packets; ++i) {
      EXPECT_EQ(i + 1, turn_packets_[i].payload.size());
      EXPECT_EQ(i + 1, udp_packets_[i].payload.size());
      EXPECT_EQ(turn_packets_[i].payload, udp_packets_[i].payload);
      EXPECT_EQ(turn_packets_[i].payload, udp_packets_[i].payload);
      if (expect_ecn_propagate && (i % 2 == 0)) {
        EXPECT_EQ(turn_packets_[i].ecn, EcnMarking::kEct1);
        EXPECT_EQ(udp_packets_[i].ecn, EcnMarking::kEct1);
      } else {
        EXPECT_EQ(turn_packets_[i].ecn, EcnMarking::kNotEct);
        EXPECT_EQ(udp_packets_[i].ecn, EcnMarking::kNotEct);
      }
    }
  }

  // Test that a TURN allocation is released when the port is closed.
  void TestTurnReleaseAllocation(ProtocolType protocol_type) {
    PrepareTurnAndUdpPorts(protocol_type);
    turn_port_.reset();
    EXPECT_THAT(
        WaitUntil([&] { return turn_server_.server()->allocations().size(); },
                  Eq(0U),
                  {.timeout = kSimulatedRtt, .clock = &time_controller_}),
        IsRtcOk());
  }

  // Test that the TURN allocation is released by sending a refresh request
  // with lifetime 0 when Release is called.
  void TestTurnGracefulReleaseAllocation(ProtocolType protocol_type) {
    PrepareTurnAndUdpPorts(protocol_type);

    // Create connections and send pings.
    Connection* conn1 = turn_port_->CreateConnection(udp_port_->Candidates()[0],
                                                     Port::ORIGIN_MESSAGE);
    Connection* conn2 = udp_port_->CreateConnection(turn_port_->Candidates()[0],
                                                    Port::ORIGIN_MESSAGE);
    ASSERT_TRUE(conn1 != nullptr);
    ASSERT_TRUE(conn2 != nullptr);
    conn1->RegisterReceivedPacketCallback(
        [&](Connection* connection, const ReceivedIpPacket& packet) {
          turn_packets_.emplace_back(packet);
        });
    conn1->SubscribeDestroyed(this, [this](Connection* connection) {
      OnConnectionSignalDestroyed(connection);
    });

    conn2->SubscribeDestroyed(this, [this](Connection* connection) {
      OnConnectionSignalDestroyed(connection);
    });

    conn1->Ping();
    EXPECT_THAT(
        WaitUntil([&] { return conn1->write_state(); },
                  Eq(Connection::STATE_WRITABLE),
                  {.timeout = kSimulatedRtt * 2, .clock = &time_controller_}),
        IsRtcOk());
    conn2->Ping();
    EXPECT_THAT(
        WaitUntil([&] { return conn2->write_state(); },
                  Eq(Connection::STATE_WRITABLE),
                  {.timeout = kSimulatedRtt * 2, .clock = &time_controller_}),
        IsRtcOk());

    // Send some data from Udp to TurnPort.
    std::array<uint8_t, 256> buf;
    buf.fill(0);
    conn2->Send(buf, options);

    // Now release the TurnPort allocation.
    // This will send a REFRESH with lifetime 0 to server.
    turn_port_->Release();

    // Wait for the TurnPort to close and destroy all connections.
    ASSERT_TRUE(
        WaitUntil([&] { return turn_port_->connections().empty(); },
                  {.timeout = kSimulatedRtt, .clock = &time_controller_}));

    // But the data should have arrived first.
    ASSERT_EQ(1ul, turn_packets_.size());
    EXPECT_EQ(sizeof(buf), turn_packets_[0].payload.size());

    // The allocation is released at server.
    EXPECT_EQ(0U, turn_server_.server()->allocations().size());
  }

 protected:
  struct Packet {
    explicit Packet(const ReceivedIpPacket& packet)
        : arrival_time(packet.arrival_time()),
          ecn(packet.ecn()),
          payload(Buffer(packet.payload().data(), packet.payload().size())) {}

    std::optional<Timestamp> arrival_time;
    EcnMarking ecn = EcnMarking::kNotEct;
    Buffer payload;
  };

  virtual PacketSocketFactory* socket_factory() { return &socket_factory_; }

  std::list<Network> networks_;
  std::unique_ptr<TurnPortTestVirtualSocketServer> ss_;
  GlobalSimulatedTimeController time_controller_;
  const Environment env_;
  Thread* main_;
  std::unique_ptr<AsyncPacketSocket> socket_;
  TestTurnServer turn_server_;
  // Passed to the TURN port if set, so it must outlive `turn_port_`.
  std::unique_ptr<LocalNetworkAccessPermissionFactoryInterface>
      lna_permission_factory_;
  std::unique_ptr<TurnPort> turn_port_;
  std::unique_ptr<UDPPort> udp_port_;
  bool turn_ready_ = false;
  bool turn_error_ = false;
  bool turn_unknown_address_ = false;
  bool turn_port_destroyed_ = false;
  bool udp_ready_ = false;
  bool test_finish_ = false;
  std::vector<Packet> turn_packets_;
  std::vector<Packet> udp_packets_;
  AsyncSocketPacketOptions options;
  std::unique_ptr<TurnCustomizer> turn_customizer_;
  IceCandidateErrorEvent error_event_;

 private:
  BasicPacketSocketFactory socket_factory_;
};

TEST_F(TurnPortTest, TestTurnPortType) {
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);
  EXPECT_EQ(IceCandidateType::kRelay, turn_port_->Type());
}

// Tests that the URL of the servers can be correctly reconstructed when
// gathering the candidates.
TEST_F(TurnPortTest, TestReconstructedServerUrlForUdpIPv4) {
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);
  TestReconstructedServerUrl(PROTO_UDP, "turn:99.99.99.3:3478?transport=udp");
}

TEST_F(TurnPortTest, TestReconstructedServerUrlForUdpIPv6) {
  turn_server_.AddInternalSocket(kTurnUdpIPv6IntAddr, PROTO_UDP);
  CreateTurnPort(kLocalIPv6Addr, kTurnUsername, kTurnPassword,
                 kTurnUdpIPv6ProtoAddr);
  // Should add [] around the IPv6.
  TestReconstructedServerUrl(
      PROTO_UDP,
      "turn:[2400:4030:1:2c00:be30:abcd:efab:cdef]:3478?transport=udp");
}

TEST_F(TurnPortTest, TestReconstructedServerUrlForTcp) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnTcpProtoAddr);
  TestReconstructedServerUrl(PROTO_TCP, "turn:99.99.99.4:3478?transport=tcp");
}

TEST_F(TurnPortTest, TestReconstructedServerUrlForTls) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TLS);
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnTlsProtoAddr);
  TestReconstructedServerUrl(PROTO_TLS, "turns:99.99.99.4:3478?transport=tcp");
}

TEST_F(TurnPortTest, TestReconstructedServerUrlForHostname) {
  CreateTurnPort(kTurnUsername, kTurnPassword,
                 kTurnPortInvalidHostnameProtoAddr);
  // This test follows the pattern from TestTurnTcpOnAddressResolveFailure.
  // As VSS doesn't provide DNS resolution, name resolve will fail,
  // the error will be set and contain the url.
  turn_port_->PrepareAddress();
  EXPECT_TRUE(
      WaitUntil([&] { return turn_error_; }, {.timeout = kResolverTimeout}));
  std::string server_url =
      "turn:" + kTurnInvalidAddr.ToString() + "?transport=udp";
  ASSERT_EQ(error_event_.url, server_url);
}

// Do a normal TURN allocation.
TEST_F(TurnPortTest, TestTurnAllocate) {
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);
  EXPECT_EQ(0, turn_port_->SetOption(Socket::OPT_SNDBUF, 10 * 1024));
  TestTurnAllocateSucceeds(kSimulatedRtt * 2);
}

class TurnLoggingIdValidator : public StunMessageObserver {
 public:
  explicit TurnLoggingIdValidator(const char* expect_val)
      : expect_val_(expect_val) {}
  ~TurnLoggingIdValidator() override {}
  void ReceivedMessage(const TurnMessage* msg) override {
    if (msg->type() == STUN_ALLOCATE_REQUEST) {
      const StunByteStringAttribute* attr =
          msg->GetByteString(STUN_ATTR_TURN_LOGGING_ID);
      if (expect_val_) {
        ASSERT_NE(nullptr, attr);
        ASSERT_EQ(expect_val_, attr->string_view());
      } else {
        EXPECT_EQ(nullptr, attr);
      }
    }
  }
  void ReceivedChannelData(std::span<const uint8_t> packet) override {}

 private:
  const char* expect_val_;
};

TEST_F(TurnPortTest, TestTurnAllocateWithLoggingId) {
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr, "KESO");
  turn_server_.server()->SetStunMessageObserver(
      std::make_unique<TurnLoggingIdValidator>("KESO"));
  TestTurnAllocateSucceeds(kSimulatedRtt * 2);
}

TEST_F(TurnPortTest, TestTurnAllocateWithoutLoggingId) {
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);
  turn_server_.server()->SetStunMessageObserver(
      std::make_unique<TurnLoggingIdValidator>(nullptr));
  TestTurnAllocateSucceeds(kSimulatedRtt * 2);
}

// Test bad credentials.
TEST_F(TurnPortTest, TestTurnBadCredentials) {
  CreateTurnPort(kTurnUsername, "bad", kTurnUdpProtoAddr);
  turn_port_->PrepareAddress();
  EXPECT_TRUE(
      WaitUntil([&] { return turn_error_; },
                {.timeout = kSimulatedRtt * 3, .clock = &time_controller_}));
  ASSERT_EQ(0U, turn_port_->Candidates().size());
  EXPECT_THAT(
      WaitUntil([&] { return error_event_.error_code; },
                Eq(STUN_ERROR_UNAUTHORIZED),
                {.timeout = kSimulatedRtt * 3, .clock = &time_controller_}),
      IsRtcOk());
  EXPECT_EQ(error_event_.error_text, "Unauthorized.");
}

// Test that we fail without emitting an error if we try to get an address from
// a TURN server with a different address family. IPv4 local, IPv6 TURN.
TEST_F(TurnPortTest, TestServerAddressFamilyMismatch) {
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnUdpIPv6ProtoAddr);
  turn_port_->PrepareAddress();
  EXPECT_TRUE(
      WaitUntil([&] { return turn_error_; },
                {.timeout = kSimulatedRtt * 3, .clock = &time_controller_}));
  ASSERT_EQ(0U, turn_port_->Candidates().size());
  EXPECT_EQ(0, error_event_.error_code);
}

// Test that we fail without emitting an error if we try to get an address from
// a TURN server with a different address family. IPv6 local, IPv4 TURN.
TEST_F(TurnPortTest, TestServerAddressFamilyMismatch6) {
  CreateTurnPort(kLocalIPv6Addr, kTurnUsername, kTurnPassword,
                 kTurnUdpProtoAddr);
  turn_port_->PrepareAddress();
  EXPECT_TRUE(
      WaitUntil([&] { return turn_error_; },
                {.timeout = kSimulatedRtt * 3, .clock = &time_controller_}));
  ASSERT_EQ(0U, turn_port_->Candidates().size());
  EXPECT_EQ(0, error_event_.error_code);
}

// Testing a normal UDP allocation using TCP connection.
TEST_F(TurnPortTest, TestTurnTcpAllocate) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnTcpProtoAddr);
  EXPECT_EQ(0, turn_port_->SetOption(Socket::OPT_SNDBUF, 10 * 1024));
  TestTurnAllocateSucceeds(kSimulatedRtt * 3);
}

// Test case for WebRTC issue 3927 where a proxy binds to the local host address
// instead the address that TurnPort originally bound to. The candidate pair
// impacted by this behavior should still be used.
TEST_F(TurnPortTest, TestTurnTcpAllocationWhenProxyChangesAddressToLocalHost) {
  SocketAddress local_address("127.0.0.1", 0);
  // After calling this, when TurnPort attempts to get a socket bound to
  // kLocalAddr, it will end up using localhost instead.
  ss_->SetAlternativeLocalAddress(kLocalAddr1.ipaddr(), local_address.ipaddr());

  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
  CreateTurnPort(kLocalAddr1, kTurnUsername, kTurnPassword, kTurnTcpProtoAddr);
  EXPECT_EQ(0, turn_port_->SetOption(Socket::OPT_SNDBUF, 10 * 1024));
  TestTurnAllocateSucceeds(kSimulatedRtt * 3);

  // Verify that the socket actually used localhost, otherwise this test isn't
  // doing what it meant to.
  ASSERT_EQ(local_address.ipaddr(),
            turn_port_->Candidates()[0].related_address().ipaddr());
}

// If the address the socket ends up bound to does not match any address of the
// TurnPort's Network, then the socket should be discarded and no candidates
// should be signaled. In the context of ICE, where one TurnPort is created for
// each Network, when this happens it's likely that the unexpected address is
// associated with some other Network, which another TurnPort is already
// covering.
TEST_F(TurnPortTest,
       TurnTcpAllocationDiscardedIfBoundAddressDoesNotMatchNetwork) {
  // Sockets bound to kLocalAddr1 will actually end up with kLocalAddr2.
  ss_->SetAlternativeLocalAddress(kLocalAddr1.ipaddr(), kLocalAddr2.ipaddr());

  // Set up TURN server to use TCP (this logic only exists for TCP).
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);

  // Create TURN port and tell it to start allocation.
  CreateTurnPort(kLocalAddr1, kTurnUsername, kTurnPassword, kTurnTcpProtoAddr);
  turn_port_->PrepareAddress();

  // Shouldn't take more than 1 RTT to realize the bound address isn't the one
  // expected.
  EXPECT_TRUE(
      WaitUntil([&] { return turn_error_; },
                {.timeout = kSimulatedRtt, .clock = &time_controller_}));
  EXPECT_THAT(WaitUntil([&] { return error_event_.error_code; },
                        Eq(STUN_ERROR_SERVER_NOT_REACHABLE),
                        {.timeout = kSimulatedRtt, .clock = &time_controller_}),
              IsRtcOk());
  EXPECT_NE(error_event_.error_text.find('.'), std::string::npos);
  EXPECT_NE(error_event_.address.find(kLocalAddr2.HostAsSensitiveURIString()),
            std::string::npos);
  EXPECT_NE(error_event_.port, 0);
  std::string server_url =
      "turn:" + kTurnTcpIntAddr.ToString() + "?transport=tcp";
  EXPECT_EQ(error_event_.url, server_url);
}

// A caveat for the above logic: if the socket ends up bound to one of the IPs
// associated with the Network, just not the "best" one, this is ok.
TEST_F(TurnPortTest, TurnTcpAllocationNotDiscardedIfNotBoundToBestIP) {
  // Sockets bound to kLocalAddr1 will actually end up with kLocalAddr2.
  ss_->SetAlternativeLocalAddress(kLocalAddr1.ipaddr(), kLocalAddr2.ipaddr());

  // Set up a network with kLocalAddr1 as the "best" IP, and kLocalAddr2 as an
  // alternate.
  Network* network = MakeNetwork(kLocalAddr1);
  network->AddIP(kLocalAddr2.ipaddr());
  ASSERT_EQ(kLocalAddr1.ipaddr(), network->GetBestIP());

  // Set up TURN server to use TCP (this logic only exists for TCP).
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);

  // Create TURN port using our special Network, and tell it to start
  // allocation.
  CreateTurnPortWithNetwork(network, kTurnUsername, kTurnPassword,
                            kTurnTcpProtoAddr);
  turn_port_->PrepareAddress();

  // Candidate should be gathered as normally.
  EXPECT_TRUE(
      WaitUntil([&] { return turn_ready_; },
                {.timeout = kSimulatedRtt * 3, .clock = &time_controller_}));
  ASSERT_EQ(1U, turn_port_->Candidates().size());

  // Verify that the socket actually used the alternate address, otherwise this
  // test isn't doing what it meant to.
  ASSERT_EQ(kLocalAddr2.ipaddr(),
            turn_port_->Candidates()[0].related_address().ipaddr());
}

// Regression test for crbug.com/webrtc/8972, caused by buggy comparison
// between IPAddress and InterfaceAddress.
TEST_F(TurnPortTest, TCPPortNotDiscardedIfBoundToTemporaryIP) {
  networks_.emplace_back("unittest", "unittest", kLocalIPv6Addr.ipaddr(), 32);
  networks_.back().AddIP(
      InterfaceAddress(kLocalIPv6Addr.ipaddr(), IPV6_ADDRESS_FLAG_TEMPORARY));

  // Set up TURN server to use TCP (this logic only exists for TCP).
  turn_server_.AddInternalSocket(kTurnIPv6IntAddr, PROTO_TCP);

  // Create TURN port using our special Network, and tell it to start
  // allocation.
  CreateTurnPortWithNetwork(&networks_.back(), kTurnUsername, kTurnPassword,
                            ProtocolAddress(kTurnIPv6IntAddr, PROTO_TCP));
  turn_port_->PrepareAddress();

  // Candidate should be gathered as normally.
  EXPECT_TRUE(
      WaitUntil([&] { return turn_ready_; },
                {.timeout = kSimulatedRtt * 3, .clock = &time_controller_}));
  ASSERT_EQ(1U, turn_port_->Candidates().size());
}

// Testing turn port will attempt to create TCP socket on address resolution
// failure.
TEST_F(TurnPortTest, TestTurnTcpOnAddressResolveFailure) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
  CreateTurnPort(kTurnUsername, kTurnPassword,
                 ProtocolAddress(kTurnInvalidAddr, PROTO_TCP));
  turn_port_->PrepareAddress();
  EXPECT_TRUE(
      WaitUntil([&] { return turn_error_; }, {.timeout = kResolverTimeout}));
  // As VSS doesn't provide DNS resolution, name resolve will fail. TurnPort
  // will proceed in creating a TCP socket which will fail as there is no
  // server on the above domain and error will be set to SOCKET_ERROR.
  EXPECT_EQ(SOCKET_ERROR, turn_port_->error());
  EXPECT_THAT(WaitUntil([&] { return error_event_.error_code; },
                        Eq(STUN_ERROR_SERVER_NOT_REACHABLE),
                        {.timeout = kSimulatedRtt, .clock = &time_controller_}),
              IsRtcOk());
  std::string server_url =
      "turn:" + kTurnInvalidAddr.ToString() + "?transport=tcp";
  ASSERT_EQ(error_event_.url, server_url);
}

// Testing turn port will attempt to create TLS socket on address resolution
// failure.
TEST_F(TurnPortTest, TestTurnTlsOnAddressResolveFailure) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TLS);
  CreateTurnPort(kTurnUsername, kTurnPassword,
                 ProtocolAddress(kTurnInvalidAddr, PROTO_TLS));
  turn_port_->PrepareAddress();
  EXPECT_TRUE(
      WaitUntil([&] { return turn_error_; }, {.timeout = kResolverTimeout}));
  EXPECT_EQ(SOCKET_ERROR, turn_port_->error());
}

// In case of UDP on address resolve failure, TurnPort will not create socket
// and return allocate failure.
TEST_F(TurnPortTest, TestTurnUdpOnAddressResolveFailure) {
  CreateTurnPort(kTurnUsername, kTurnPassword,
                 ProtocolAddress(kTurnInvalidAddr, PROTO_UDP));
  turn_port_->PrepareAddress();
  EXPECT_TRUE(
      WaitUntil([&] { return turn_error_; }, {.timeout = kResolverTimeout}));
  // Error from turn port will not be socket error.
  EXPECT_NE(SOCKET_ERROR, turn_port_->error());
}

// Try to do a TURN allocation with an invalid password.
TEST_F(TurnPortTest, TestTurnAllocateBadPassword) {
  CreateTurnPort(kTurnUsername, "bad", kTurnUdpProtoAddr);
  turn_port_->PrepareAddress();
  EXPECT_TRUE(
      WaitUntil([&] { return turn_error_; },
                {.timeout = kSimulatedRtt * 2, .clock = &time_controller_}));
  ASSERT_EQ(0U, turn_port_->Candidates().size());
}

// Tests that TURN port nonce will be reset when receiving an ALLOCATE MISMATCH
// error.
TEST_F(TurnPortTest, TestTurnAllocateNonceResetAfterAllocateMismatch) {
  // Do a normal allocation first.
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);
  turn_port_->PrepareAddress();
  EXPECT_TRUE(
      WaitUntil([&] { return turn_ready_; },
                {.timeout = kSimulatedRtt * 2, .clock = &time_controller_}));
  SocketAddress first_addr(turn_port_->socket()->GetLocalAddress());
  // Destroy the turnport while keeping the drop probability to 1 to
  // suppress the release of the allocation at the server.
  ss_->set_drop_probability(1.0);
  turn_port_.reset();
  time_controller_.AdvanceTime(kSimulatedRtt);
  ss_->set_drop_probability(0.0);

  // Force the socket server to assign the same port.
  ss_->SetNextPortForTesting(first_addr.port());
  turn_ready_ = false;
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);

  // It is expected that the turn port will first get a nonce from the server
  // using timestamp `ts_before` but then get an allocate mismatch error and
  // receive an even newer nonce based on the system clock. `ts_before` is
  // chosen so that the two NONCEs generated by the server will be different.
  int64_t ts_before = env_.clock().TimeInMilliseconds() - 1;
  std::string first_nonce =
      turn_server_.server()->SetTimestampForNextNonce(ts_before);
  turn_port_->PrepareAddress();

  // Four round trips; first we'll get "stale nonce", then
  // "allocate mismatch", then "stale nonce" again, then finally it will
  // succeed.
  EXPECT_TRUE(
      WaitUntil([&] { return turn_ready_; },
                {.timeout = kSimulatedRtt * 4, .clock = &time_controller_}));
  EXPECT_NE(first_nonce, turn_port_->nonce());
}

// Tests that a new local address is created after
// STUN_ERROR_ALLOCATION_MISMATCH.
TEST_F(TurnPortTest, TestTurnAllocateMismatch) {
  // Do a normal allocation first.
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);
  turn_port_->PrepareAddress();
  EXPECT_TRUE(
      WaitUntil([&] { return turn_ready_; },
                {.timeout = kSimulatedRtt * 2, .clock = &time_controller_}));
  SocketAddress first_addr(turn_port_->socket()->GetLocalAddress());

  // Clear connected_ flag on turnport to suppress the release of
  // the allocation.
  turn_port_->OnSocketClose(turn_port_->socket(), 0);

  // Forces the socket server to assign the same port.
  ss_->SetNextPortForTesting(first_addr.port());

  turn_ready_ = false;
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);
  turn_port_->PrepareAddress();

  // Verifies that the new port has the same address.
  EXPECT_EQ(first_addr, turn_port_->socket()->GetLocalAddress());

  // Four round trips; first we'll get "stale nonce", then
  // "allocate mismatch", then "stale nonce" again, then finally it will
  // succeed.
  EXPECT_TRUE(
      WaitUntil([&] { return turn_ready_; },
                {.timeout = kSimulatedRtt * 4, .clock = &time_controller_}));

  // Verifies that the new port has a different address now.
  EXPECT_NE(first_addr, turn_port_->socket()->GetLocalAddress());

  // Verify that all packets received from the shared socket are ignored.
  std::string test_packet = "Test packet";
  EXPECT_FALSE(turn_port_->HandleIncomingPacket(
      socket_.get(), ReceivedIpPacket::CreateFromLegacy(
                         test_packet.data(), test_packet.size(),
                         env_.clock().TimeInMicroseconds(),
                         SocketAddress(kTurnUdpExtAddr.ipaddr(), 0))));
}

// Tests that a shared-socket-TurnPort creates its own socket after
// STUN_ERROR_ALLOCATION_MISMATCH.
TEST_F(TurnPortTest, TestSharedSocketAllocateMismatch) {
  // Do a normal allocation first.
  CreateSharedTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);
  turn_port_->PrepareAddress();
  EXPECT_TRUE(
      WaitUntil([&] { return turn_ready_; },
                {.timeout = kSimulatedRtt * 2, .clock = &time_controller_}));
  SocketAddress first_addr(turn_port_->socket()->GetLocalAddress());

  // Clear connected_ flag on turnport to suppress the release of
  // the allocation.
  turn_port_->OnSocketClose(turn_port_->socket(), 0);

  turn_ready_ = false;
  CreateSharedTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);

  // Verifies that the new port has the same address.
  EXPECT_EQ(first_addr, turn_port_->socket()->GetLocalAddress());
  EXPECT_TRUE(turn_port_->SharedSocket());

  turn_port_->PrepareAddress();
  // Extra 2 round trips due to allocate mismatch.
  EXPECT_TRUE(
      WaitUntil([&] { return turn_ready_; },
                {.timeout = kSimulatedRtt * 4, .clock = &time_controller_}));

  // Verifies that the new port has a different address now.
  EXPECT_NE(first_addr, turn_port_->socket()->GetLocalAddress());
  EXPECT_FALSE(turn_port_->SharedSocket());
}

TEST_F(TurnPortTest, TestTurnTcpAllocateMismatch) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnTcpProtoAddr);

  // Do a normal allocation first.
  turn_port_->PrepareAddress();
  EXPECT_TRUE(
      WaitUntil([&] { return turn_ready_; },
                {.timeout = kSimulatedRtt * 3, .clock = &time_controller_}));
  SocketAddress first_addr(turn_port_->socket()->GetLocalAddress());

  // Clear connected_ flag on turnport to suppress the release of
  // the allocation.
  turn_port_->OnSocketClose(turn_port_->socket(), 0);

  // Forces the socket server to assign the same port.
  ss_->SetNextPortForTesting(first_addr.port());

  turn_ready_ = false;
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnTcpProtoAddr);
  turn_port_->PrepareAddress();

  // Verifies that the new port has the same address.
  EXPECT_EQ(first_addr, turn_port_->socket()->GetLocalAddress());

  // Extra 2 round trips due to allocate mismatch.
  EXPECT_TRUE(
      WaitUntil([&] { return turn_ready_; },
                {.timeout = kSimulatedRtt * 5, .clock = &time_controller_}));

  // Verifies that the new port has a different address now.
  EXPECT_NE(first_addr, turn_port_->socket()->GetLocalAddress());
}

TEST_F(TurnPortTest, TestRefreshRequestGetsErrorResponse) {
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);
  PrepareTurnAndUdpPorts(PROTO_UDP);
  turn_port_->CreateConnection(udp_port_->Candidates()[0],
                               Port::ORIGIN_MESSAGE);
  // Set bad credentials.
  RelayCredentials bad_credentials("bad_user", "bad_pwd");
  turn_port_->set_credentials(bad_credentials);
  // This sends out the first RefreshRequest with correct credentials.
  // When this succeeds, it will schedule a new RefreshRequest with the bad
  // credential.
  turn_port_->request_manager().FlushForTest(TURN_REFRESH_REQUEST);
  time_controller_.AdvanceTime(kSimulatedRtt);
  EXPECT_TRUE(turn_port_->connected());
  EXPECT_TRUE(turn_port_->HasRequests());
  // Flush it again, it will receive a bad response.
  turn_port_->request_manager().FlushForTest(TURN_REFRESH_REQUEST);
  EXPECT_TRUE(
      WaitUntil([&] { return !turn_port_->connected(); },
                {.timeout = kSimulatedRtt, .clock = &time_controller_}));
  EXPECT_TRUE(CheckAllConnectionsFailedAndPruned());
  EXPECT_FALSE(turn_port_->HasRequests());
}

// Test that TurnPort will not handle any incoming packets once it has been
// closed.
TEST_F(TurnPortTest, TestStopProcessingPacketsAfterClosed) {
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);
  PrepareTurnAndUdpPorts(PROTO_UDP);
  Connection* conn1 = turn_port_->CreateConnection(udp_port_->Candidates()[0],
                                                   Port::ORIGIN_MESSAGE);
  Connection* conn2 = udp_port_->CreateConnection(turn_port_->Candidates()[0],
                                                  Port::ORIGIN_MESSAGE);
  ASSERT_TRUE(conn1 != nullptr);
  ASSERT_TRUE(conn2 != nullptr);
  // Make sure conn2 is writable.
  conn2->Ping();
  EXPECT_THAT(
      WaitUntil([&] { return conn2->write_state(); },
                Eq(Connection::STATE_WRITABLE),
                {.timeout = kSimulatedRtt * 2, .clock = &time_controller_}),
      IsRtcOk());

  turn_port_->CloseForTest();
  time_controller_.AdvanceTime(kSimulatedRtt);
  turn_unknown_address_ = false;
  conn2->Ping();
  time_controller_.AdvanceTime(kSimulatedRtt);
  // Since the turn port does not handle packets any more, it should not
  // SignalUnknownAddress.
  EXPECT_FALSE(turn_unknown_address_);
}

// Test that CreateConnection will return null if port becomes disconnected.
TEST_F(TurnPortTest, TestCreateConnectionWhenSocketClosed) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnTcpProtoAddr);
  PrepareTurnAndUdpPorts(PROTO_TCP);
  // Create a connection.
  Connection* conn1 = turn_port_->CreateConnection(udp_port_->Candidates()[0],
                                                   Port::ORIGIN_MESSAGE);
  ASSERT_TRUE(conn1 != nullptr);

  // Close the socket and create a connection again.
  turn_port_->OnSocketClose(turn_port_->socket(), 1);
  conn1 = turn_port_->CreateConnection(udp_port_->Candidates()[0],
                                       Port::ORIGIN_MESSAGE);
  ASSERT_TRUE(conn1 == nullptr);
}

// Tests that when a TCP socket is closed, the respective TURN connection will
// be destroyed.
TEST_F(TurnPortTest, TestSocketCloseWillDestroyConnection) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnTcpProtoAddr);
  PrepareTurnAndUdpPorts(PROTO_TCP);
  Connection* conn = turn_port_->CreateConnection(udp_port_->Candidates()[0],
                                                  Port::ORIGIN_MESSAGE);
  EXPECT_NE(nullptr, conn);
  EXPECT_TRUE(!turn_port_->connections().empty());
  turn_port_->socket()->NotifyClosedForTest(1);
  EXPECT_TRUE(WaitUntil(
      [&] { return turn_port_->connections().empty(); },
      {.timeout = kConnectionDestructionDelay, .clock = &time_controller_}));
}

TEST_F(TurnPortTest, TestUdpSocketCloseFailsPendingAllocateRequest) {
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);
  turn_port_->PrepareAddress();
  ASSERT_FALSE(turn_ready_);

  turn_port_->socket()->NotifyClosedForTest(1);

  // Without advancing the clock, except for the posted NotifyPortError().
  EXPECT_EQ(error_event_.error_code, STUN_ERROR_SERVER_NOT_REACHABLE);
  EXPECT_EQ(error_event_.error_text, "Socket was closed");
  time_controller_.AdvanceTime(TimeDelta::Zero());
  EXPECT_TRUE(turn_error_);
  EXPECT_EQ(turn_port_->Candidates().size(), 0U);
}

TEST_F(TurnPortTest, TestPrivateIpTurnCandidateErrorAddressScrubbedTcp) {
  const SocketAddress kTurnPrivateIntAddr("192.168.1.1", TURN_SERVER_PORT);
  const ProtocolAddress kTurnPrivateTcpProtoAddr(kTurnPrivateIntAddr,
                                                 PROTO_TCP);
  turn_server_.AddInternalSocket(kTurnPrivateIntAddr, PROTO_TCP);
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnPrivateTcpProtoAddr);
  turn_port_->PrepareAddress();
  ASSERT_FALSE(turn_ready_);
  ASSERT_NE(turn_port_->socket(), nullptr);

  turn_port_->socket()->NotifyClosedForTest(1);

  EXPECT_EQ(error_event_.error_code, STUN_ERROR_SERVER_NOT_REACHABLE);
  EXPECT_EQ(error_event_.address, "");
  EXPECT_EQ(error_event_.port, 0);
}

TEST_F(TurnPortTest, TestPrivateIpTurnCandidateErrorAddressScrubbedTls) {
  const SocketAddress kTurnPrivateIntAddr("192.168.1.1", TURN_SERVER_PORT);
  const ProtocolAddress kTurnPrivateTlsProtoAddr(kTurnPrivateIntAddr,
                                                 PROTO_TLS);
  turn_server_.AddInternalSocket(kTurnPrivateIntAddr, PROTO_TLS);
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnPrivateTlsProtoAddr);
  turn_port_->PrepareAddress();
  ASSERT_FALSE(turn_ready_);
  ASSERT_NE(turn_port_->socket(), nullptr);

  turn_port_->socket()->NotifyClosedForTest(1);

  EXPECT_EQ(error_event_.error_code, STUN_ERROR_SERVER_NOT_REACHABLE);
  EXPECT_EQ(error_event_.address, "");
  EXPECT_EQ(error_event_.port, 0);
}

TEST_F(TurnPortTest, TestPublicIpTurnCandidateErrorAddressNotScrubbedTcp) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnTcpProtoAddr);
  turn_port_->PrepareAddress();
  ASSERT_FALSE(turn_ready_);
  ASSERT_NE(turn_port_->socket(), nullptr);

  turn_port_->socket()->NotifyClosedForTest(1);

  EXPECT_EQ(error_event_.error_code, STUN_ERROR_SERVER_NOT_REACHABLE);
  EXPECT_FALSE(error_event_.address.empty());
  EXPECT_NE(error_event_.port, 0);
}

TEST_F(TurnPortTest, TestPublicIpTurnCandidateErrorAddressNotScrubbedTls) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TLS);
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnTlsProtoAddr);
  turn_port_->PrepareAddress();
  ASSERT_FALSE(turn_ready_);
  ASSERT_NE(turn_port_->socket(), nullptr);

  turn_port_->socket()->NotifyClosedForTest(1);

  EXPECT_EQ(error_event_.error_code, STUN_ERROR_SERVER_NOT_REACHABLE);
  EXPECT_FALSE(error_event_.address.empty());
  EXPECT_NE(error_event_.port, 0);
}

// Test try-alternate-server feature.
TEST_F(TurnPortTest, TestTurnAlternateServerUDP) {
  TestTurnAlternateServer(PROTO_UDP);
}

TEST_F(TurnPortTest, TestTurnAlternateServerTCP) {
  TestTurnAlternateServer(PROTO_TCP);
}

TEST_F(TurnPortTest, TestTurnAlternateServerTLS) {
  TestTurnAlternateServer(PROTO_TLS);
}

// Test that we fail when we redirect to an address different from
// current IP family.
TEST_F(TurnPortTest, TestTurnAlternateServerV4toV6UDP) {
  TestTurnAlternateServerV4toV6(PROTO_UDP);
}

TEST_F(TurnPortTest, TestTurnAlternateServerV4toV6TCP) {
  TestTurnAlternateServerV4toV6(PROTO_TCP);
}

TEST_F(TurnPortTest, TestTurnAlternateServerV4toV6TLS) {
  TestTurnAlternateServerV4toV6(PROTO_TLS);
}

// Test try-alternate-server catches the case of pingpong.
TEST_F(TurnPortTest, TestTurnAlternateServerPingPongUDP) {
  TestTurnAlternateServerPingPong(PROTO_UDP);
}

TEST_F(TurnPortTest, TestTurnAlternateServerPingPongTCP) {
  TestTurnAlternateServerPingPong(PROTO_TCP);
}

TEST_F(TurnPortTest, TestTurnAlternateServerPingPongTLS) {
  TestTurnAlternateServerPingPong(PROTO_TLS);
}

// Test try-alternate-server catch the case of repeated server.
TEST_F(TurnPortTest, TestTurnAlternateServerDetectRepetitionUDP) {
  TestTurnAlternateServerDetectRepetition(PROTO_UDP);
}

TEST_F(TurnPortTest, TestTurnAlternateServerDetectRepetitionTCP) {
  TestTurnAlternateServerDetectRepetition(PROTO_TCP);
}

TEST_F(TurnPortTest, TestTurnAlternateServerDetectRepetitionTLS) {
  TestTurnAlternateServerDetectRepetition(PROTO_TCP);
}

// Test catching the case of a redirect to loopback.
TEST_F(TurnPortTest, TestTurnAlternateServerLoopbackUdpIpv4) {
  TestTurnAlternateServerLoopback(PROTO_UDP, false);
}

TEST_F(TurnPortTest, TestTurnAlternateServerLoopbackUdpIpv6) {
  TestTurnAlternateServerLoopback(PROTO_UDP, true);
}

TEST_F(TurnPortTest, TestTurnAlternateServerLoopbackTcpIpv4) {
  TestTurnAlternateServerLoopback(PROTO_TCP, false);
}

TEST_F(TurnPortTest, TestTurnAlternateServerLoopbackTcpIpv6) {
  TestTurnAlternateServerLoopback(PROTO_TCP, true);
}

TEST_F(TurnPortTest, TestTurnAlternateServerLoopbackTlsIpv4) {
  TestTurnAlternateServerLoopback(PROTO_TLS, false);
}

TEST_F(TurnPortTest, TestTurnAlternateServerLoopbackTlsIpv6) {
  TestTurnAlternateServerLoopback(PROTO_TLS, true);
}

// Test catching the case of a redirect to the unspecified address.
TEST_F(TurnPortTest, TestTurnAlternateServerAnyAddressUdpIpv4) {
  TestTurnAlternateServerAnyAddress(PROTO_UDP, false);
}

TEST_F(TurnPortTest, TestTurnAlternateServerAnyAddressUdpIpv6) {
  TestTurnAlternateServerAnyAddress(PROTO_UDP, true);
}

// The IPv4-mapped form of the unspecified address (::ffff:0.0.0.0) must be
// blocked as well, since dual-stack sockets treat it like 0.0.0.0.
TEST_F(TurnPortTest, TestTurnAlternateServerAnyAddressUdpIpv4Mapped) {
  TestTurnAlternateServerBlocked(
      PROTO_UDP, /*ipv6=*/true,
      SocketAddress("::ffff:0.0.0.0", TURN_SERVER_PORT));
}

TEST_F(TurnPortTest, TestTurnAlternateServerAnyAddressTcpIpv4) {
  TestTurnAlternateServerAnyAddress(PROTO_TCP, false);
}

TEST_F(TurnPortTest, TestTurnAlternateServerAnyAddressTcpIpv6) {
  TestTurnAlternateServerAnyAddress(PROTO_TCP, true);
}

TEST_F(TurnPortTest, TestTurnAlternateServerAnyAddressTcpIpv4Mapped) {
  TestTurnAlternateServerBlocked(
      PROTO_TCP, /*ipv6=*/true,
      SocketAddress("::ffff:0.0.0.0", TURN_SERVER_PORT));
}

TEST_F(TurnPortTest, TestTurnAlternateServerAnyAddressTlsIpv4) {
  TestTurnAlternateServerAnyAddress(PROTO_TLS, false);
}

TEST_F(TurnPortTest, TestTurnAlternateServerAnyAddressTlsIpv6) {
  TestTurnAlternateServerAnyAddress(PROTO_TLS, true);
}

// Test that a redirect to a server that requires Local Network Access
// permission only succeeds if the permission is granted.
TEST_F(TurnPortTest, TestTurnAlternateServerLocalNetworkAccessGrantedUDP) {
  TestTurnAlternateServerLocalNetworkAccess(
      PROTO_UDP, LocalNetworkAccessPermissionStatus::kGranted);
}

TEST_F(TurnPortTest, TestTurnAlternateServerLocalNetworkAccessGrantedTCP) {
  TestTurnAlternateServerLocalNetworkAccess(
      PROTO_TCP, LocalNetworkAccessPermissionStatus::kGranted);
}

TEST_F(TurnPortTest, TestTurnAlternateServerLocalNetworkAccessDeniedUDP) {
  TestTurnAlternateServerLocalNetworkAccess(
      PROTO_UDP, LocalNetworkAccessPermissionStatus::kDenied);
}

TEST_F(TurnPortTest, TestTurnAlternateServerLocalNetworkAccessDeniedTCP) {
  TestTurnAlternateServerLocalNetworkAccess(
      PROTO_TCP, LocalNetworkAccessPermissionStatus::kDenied);
}

// Redirects for UDP reuse the socket, which may be closed while the Local
// Network Access permission for the alternate server is pending. Granting the
// permission after that must not send an Allocate request on the closed socket.
TEST_F(TurnPortTest, TestTurnAlternateServerLocalNetworkAccessSocketClosedUDP) {
  absl::AnyInvocable<void(LocalNetworkAccessPermissionStatus)>
      permission_callback;
  auto lna_permission_factory =
      std::make_unique<MockLocalNetworkAccessPermissionFactory>();
  EXPECT_CALL(*lna_permission_factory, Create())
      .WillRepeatedly([&permission_callback] {
        auto permission =
            std::make_unique<NiceMock<MockLocalNetworkAccessPermission>>();
        ON_CALL(*permission, ShouldRequestPermission(kTurnAlternateIntAddr))
            .WillByDefault(Return(true));
        // Hold on to the callback, so that the test decides when to respond.
        ON_CALL(*permission, RequestPermission(_, _))
            .WillByDefault(
                [&permission_callback](
                    const SocketAddress& /* addr */,
                    absl::AnyInvocable<void(LocalNetworkAccessPermissionStatus)>
                        callback) {
                  permission_callback = std::move(callback);
                });
        return permission;
      });
  lna_permission_factory_ = std::move(lna_permission_factory);

  std::vector<SocketAddress> redirect_addresses;
  redirect_addresses.push_back(kTurnAlternateIntAddr);
  TestTurnRedirector redirector(redirect_addresses);

  turn_server_.AddInternalSocket(kTurnIntAddr, PROTO_UDP);
  turn_server_.AddInternalSocket(kTurnAlternateIntAddr, PROTO_UDP);
  turn_server_.set_redirect_hook(&redirector);
  CreateTurnPort(kTurnUsername, kTurnPassword,
                 ProtocolAddress(kTurnIntAddr, PROTO_UDP));

  turn_port_->PrepareAddress();
  ASSERT_TRUE(WaitUntil([&] { return permission_callback != nullptr; },
                        {.timeout = TimeToGetAlternateTurnCandidate(PROTO_UDP),
                         .clock = &time_controller_}));

  turn_port_->socket()->NotifyClosedForTest(1);
  std::move(permission_callback)(LocalNetworkAccessPermissionStatus::kGranted);
  EXPECT_FALSE(turn_port_->HasRequests());

  time_controller_.AdvanceTime(TimeToGetTurnCandidate(PROTO_UDP));
  EXPECT_TRUE(turn_error_);
  EXPECT_FALSE(turn_ready_);
  EXPECT_EQ(turn_port_->Candidates().size(), 0U);
}

// Do a TURN allocation and try to send a packet to it from the outside.
// The packet should be dropped. Then, try to send a packet from TURN to the
// outside. It should reach its destination. Finally, try again from the
// outside. It should now work as well.
TEST_F(TurnPortTest, TestTurnConnection) {
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);
  TestTurnConnection(PROTO_UDP);
}

// Similar to above, except that this test will use the shared socket.
TEST_F(TurnPortTest, TestTurnConnectionUsingSharedSocket) {
  CreateSharedTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);
  TestTurnConnection(PROTO_UDP);
}

// Test that we can establish a TCP connection with TURN server.
TEST_F(TurnPortTest, TestTurnTcpConnection) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnTcpProtoAddr);
  TestTurnConnection(PROTO_TCP);
}

// Test that we can establish a TLS connection with TURN server.
TEST_F(TurnPortTest, TestTurnTlsConnection) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TLS);
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnTlsProtoAddr);
  TestTurnConnection(PROTO_TLS);
}

// Test that if a connection on a TURN port is destroyed, the TURN port can
// still receive ping on that connection as if it is from an unknown address.
// If the connection is created again, it will be used to receive ping.
TEST_F(TurnPortTest, TestDestroyTurnConnection) {
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);
  TestDestroyTurnConnection();
}

// Similar to above, except that this test will use the shared socket.
TEST_F(TurnPortTest, TestDestroyTurnConnectionUsingSharedSocket) {
  CreateSharedTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);
  TestDestroyTurnConnection();
}

// Run TurnConnectionTest with one-time-use nonce feature.
// Here server will send a 438 STALE_NONCE error message for
// every TURN transaction.
TEST_F(TurnPortTest, TestTurnConnectionUsingOTUNonce) {
  turn_server_.set_enable_otu_nonce(true);
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);
  TestTurnConnection(PROTO_UDP);
}

// Test that CreatePermissionRequest will be scheduled after the success
// of the first create permission request and the request will get an
// ErrorResponse if the ufrag and pwd are incorrect.
TEST_F(TurnPortTest, TestRefreshCreatePermissionRequest) {
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);
  PrepareTurnAndUdpPorts(PROTO_UDP);

  Connection* conn = turn_port_->CreateConnection(udp_port_->Candidates()[0],
                                                  Port::ORIGIN_MESSAGE);
  ASSERT_TRUE(conn != nullptr);
  time_controller_.AdvanceTime(kSimulatedRtt);
  EXPECT_TRUE(HasPermission(udp_port_->Candidates()[0].address().ipaddr()));
  // A create-permission-request should be pending.
  // After the next create-permission-response is received, it will schedule
  // another request with bad_ufrag and bad_pwd.
  RelayCredentials bad_credentials("bad_user", "bad_pwd");
  turn_port_->set_credentials(bad_credentials);
  turn_port_->request_manager().FlushForTest(kAllRequestsForTest);
  time_controller_.AdvanceTime(kSimulatedRtt);
  EXPECT_FALSE(CheckConnectionFailedAndPruned(conn));
  // Flush the requests again; the create-permission-request will fail.
  turn_port_->request_manager().FlushForTest(kAllRequestsForTest);
  EXPECT_TRUE(
      WaitUntil([&] { return CheckConnectionFailedAndPruned(conn); },
                {.timeout = kSimulatedRtt, .clock = &time_controller_}));
}

TEST_F(TurnPortTest, TestChannelBindGetErrorResponse) {
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);
  PrepareTurnAndUdpPorts(PROTO_UDP);
  Connection* conn1 = turn_port_->CreateConnection(udp_port_->Candidates()[0],
                                                   Port::ORIGIN_MESSAGE);
  ASSERT_TRUE(conn1 != nullptr);
  Connection* conn2 = udp_port_->CreateConnection(turn_port_->Candidates()[0],
                                                  Port::ORIGIN_MESSAGE);

  ASSERT_TRUE(conn2 != nullptr);
  conn1->Ping();
  EXPECT_TRUE(
      WaitUntil([&] { return conn1->writable(); },
                {.timeout = kSimulatedRtt * 2, .clock = &time_controller_}));

  // Tell the TURN server to reject all bind requests from now on.
  turn_server_.server()->set_reject_bind_requests(true);

  auto data = std::to_array<uint8_t>({'A', 'B', 'C'});
  conn1->Send(data, options);

  EXPECT_TRUE(
      WaitUntil([&] { return CheckConnectionFailedAndPruned(conn1); },
                {.timeout = kSimulatedRtt, .clock = &time_controller_}));
  // Verify that packets are allowed to be sent after a bind request error.
  // They'll just use a send indication instead.
  conn2->RegisterReceivedPacketCallback(
      [&](Connection* connection, const ReceivedIpPacket& packet) {
        // TODO(bugs.webrtc.org/345518625): Verify that the packet was
        // received unchanneled, not channeled.
        udp_packets_.emplace_back(packet);
      });
  conn1->Send(data, options);
  EXPECT_TRUE(
      WaitUntil([&] { return !udp_packets_.empty(); },
                {.timeout = kSimulatedRtt, .clock = &time_controller_}));
  conn2->DeregisterReceivedPacketCallback();
}

// Do a TURN allocation, establish a UDP connection, and send some data.
TEST_F(TurnPortTest, TestTurnSendDataTurnUdpToUdp) {
  // Create ports and prepare addresses.
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);
  TestTurnSendData(PROTO_UDP, /*expect_ecn_propagate=*/true);
  EXPECT_EQ(UDP_PROTOCOL_NAME, turn_port_->Candidates()[0].relay_protocol());
}

// Do a TURN allocation, establish a TCP connection, and send some data.
TEST_F(TurnPortTest, TestTurnSendDataTurnTcpToUdp) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
  // Create ports and prepare addresses.
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnTcpProtoAddr);
  TestTurnSendData(PROTO_TCP, /*expect_ecn_propagate=*/false);
  EXPECT_EQ(TCP_PROTOCOL_NAME, turn_port_->Candidates()[0].relay_protocol());
}

// Do a TURN allocation, establish a TLS connection, and send some data.
TEST_F(TurnPortTest, TestTurnSendDataTurnTlsToUdp) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TLS);
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnTlsProtoAddr);
  TestTurnSendData(PROTO_TLS, /*expect_ecn_propagate=*/false);
  EXPECT_EQ(TLS_PROTOCOL_NAME, turn_port_->Candidates()[0].relay_protocol());
}

// Test TURN fails to make a connection from IPv6 address to a server which has
// IPv4 address.
TEST_F(TurnPortTest, TestTurnLocalIPv6AddressServerIPv4) {
  turn_server_.AddInternalSocket(kTurnUdpIPv6IntAddr, PROTO_UDP);
  CreateTurnPort(kLocalIPv6Addr, kTurnUsername, kTurnPassword,
                 kTurnUdpProtoAddr);
  turn_port_->PrepareAddress();
  ASSERT_TRUE(
      WaitUntil([&] { return turn_error_; },
                {.timeout = kSimulatedRtt, .clock = &time_controller_}));
  EXPECT_TRUE(turn_port_->Candidates().empty());
}

// Test TURN make a connection from IPv6 address to a server which has
// IPv6 intenal address. But in this test external address is a IPv4 address,
// hence allocated address will be a IPv4 address.
TEST_F(TurnPortTest, TestTurnLocalIPv6AddressServerIPv6ExtenalIPv4) {
  turn_server_.AddInternalSocket(kTurnUdpIPv6IntAddr, PROTO_UDP);
  CreateTurnPort(kLocalIPv6Addr, kTurnUsername, kTurnPassword,
                 kTurnUdpIPv6ProtoAddr);
  TestTurnAllocateSucceeds(kSimulatedRtt * 2);
}

// Tests that the local and remote candidate address families should match when
// a connection is created. Specifically, if a TURN port has an IPv6 address,
// its local candidate will still be an IPv4 address and it can only create
// connections with IPv4 remote candidates.
TEST_F(TurnPortTest, TestCandidateAddressFamilyMatch) {
  turn_server_.AddInternalSocket(kTurnUdpIPv6IntAddr, PROTO_UDP);

  CreateTurnPort(kLocalIPv6Addr, kTurnUsername, kTurnPassword,
                 kTurnUdpIPv6ProtoAddr);
  turn_port_->PrepareAddress();
  EXPECT_TRUE(
      WaitUntil([&] { return turn_ready_; },
                {.timeout = kSimulatedRtt * 2, .clock = &time_controller_}));
  ASSERT_EQ(1U, turn_port_->Candidates().size());

  // Create an IPv4 candidate. It will match the TURN candidate.
  Candidate remote_candidate(ICE_CANDIDATE_COMPONENT_RTP, "udp", kLocalAddr2, 0,
                             "", "", IceCandidateType::kHost, 0,
                             kCandidateFoundation);
  remote_candidate.set_address(kLocalAddr2);
  Connection* conn =
      turn_port_->CreateConnection(remote_candidate, Port::ORIGIN_MESSAGE);
  EXPECT_NE(nullptr, conn);

  // Set the candidate address family to IPv6. It won't match the TURN
  // candidate.
  remote_candidate.set_address(kLocalIPv6Addr2);
  conn = turn_port_->CreateConnection(remote_candidate, Port::ORIGIN_MESSAGE);
  EXPECT_EQ(nullptr, conn);
}

// Test that a CreatePermission failure will result in the connection being
// pruned and failed.
TEST_F(TurnPortTest, TestConnectionFailedAndPrunedOnCreatePermissionFailure) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
  turn_server_.server()->set_reject_private_addresses(true);
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnTcpProtoAddr);
  turn_port_->PrepareAddress();
  EXPECT_TRUE(
      WaitUntil([&] { return turn_ready_; },
                {.timeout = kSimulatedRtt * 3, .clock = &time_controller_}));

  CreateUdpPort(SocketAddress("10.0.0.10", 0));
  udp_port_->PrepareAddress();
  EXPECT_TRUE(
      WaitUntil([&] { return udp_ready_; },
                {.timeout = kSimulatedRtt, .clock = &time_controller_}));
  // Create a connection.
  TestConnectionWrapper conn(turn_port_->CreateConnection(
      udp_port_->Candidates()[0], Port::ORIGIN_MESSAGE));
  EXPECT_TRUE(conn.connection() != nullptr);

  // Asynchronously, CreatePermission request should be sent and fail, which
  // will make the connection pruned and failed.
  EXPECT_TRUE(WaitUntil(
      [&] { return CheckConnectionFailedAndPruned(conn.connection()); },
      {.timeout = kSimulatedRtt, .clock = &time_controller_}));
  EXPECT_FALSE(HasPermission(udp_port_->Candidates()[0].address().ipaddr()));
  // Check that the connection is not deleted asynchronously.
  time_controller_.AdvanceTime(kConnectionDestructionDelay);
  EXPECT_NE(nullptr, conn.connection());
}

// Test that a TURN allocation is released when the port is closed.
TEST_F(TurnPortTest, TestTurnReleaseAllocation) {
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);
  TestTurnReleaseAllocation(PROTO_UDP);
}

// Test that a TURN TCP allocation is released when the port is closed.
TEST_F(TurnPortTest, TestTurnTCPReleaseAllocation) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnTcpProtoAddr);
  TestTurnReleaseAllocation(PROTO_TCP);
}

TEST_F(TurnPortTest, TestTurnTLSReleaseAllocation) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TLS);
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnTlsProtoAddr);
  TestTurnReleaseAllocation(PROTO_TLS);
}

TEST_F(TurnPortTest, TestTurnUDPGracefulReleaseAllocation) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_UDP);
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);
  TestTurnGracefulReleaseAllocation(PROTO_UDP);
}

TEST_F(TurnPortTest, TestTurnTCPGracefulReleaseAllocation) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnTcpProtoAddr);
  TestTurnGracefulReleaseAllocation(PROTO_TCP);
}

TEST_F(TurnPortTest, TestTurnTLSGracefulReleaseAllocation) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TLS);
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnTlsProtoAddr);
  TestTurnGracefulReleaseAllocation(PROTO_TLS);
}

// Test that nothing bad happens if we try to create a connection to the same
// remote address twice. Previously there was a bug that caused this to hit a
// DCHECK.
TEST_F(TurnPortTest, CanCreateTwoConnectionsToSameAddress) {
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);
  PrepareTurnAndUdpPorts(PROTO_UDP);
  Connection* conn1 = turn_port_->CreateConnection(udp_port_->Candidates()[0],
                                                   Port::ORIGIN_MESSAGE);
  Connection* conn2 = turn_port_->CreateConnection(udp_port_->Candidates()[0],
                                                   Port::ORIGIN_MESSAGE);
  EXPECT_NE(conn1, conn2);
}

// This test verifies any FD's are not leaked after TurnPort is destroyed.
// https://code.google.com/p/webrtc/issues/detail?id=2651
#if defined(WEBRTC_LINUX) && !defined(WEBRTC_ANDROID)

TEST_F(TurnPortTest, TestResolverShutdown) {
  turn_server_.AddInternalSocket(kTurnUdpIPv6IntAddr, PROTO_UDP);
  int last_fd_count = GetFDCount();
  // Need to supply unresolved address to kick off resolver.
  CreateTurnPort(kLocalIPv6Addr, kTurnUsername, kTurnPassword,
                 ProtocolAddress(kTurnInvalidAddr, PROTO_UDP));
  turn_port_->PrepareAddress();
  ASSERT_TRUE(
      WaitUntil([&] { return turn_error_; }, {.timeout = kResolverTimeout}));
  EXPECT_TRUE(turn_port_->Candidates().empty());
  turn_port_.reset();
  Thread::Current()->PostTask([this] { test_finish_ = true; });
  // Waiting for above message to be processed.
  ASSERT_TRUE(
      WaitUntil([&] { return test_finish_; }, {.clock = &time_controller_}));
  EXPECT_EQ(last_fd_count, GetFDCount());
}
#endif

class MessageObserver : public StunMessageObserver {
 public:
  MessageObserver(unsigned int* message_counter,
                  unsigned int* channel_data_counter,
                  unsigned int* attr_counter)
      : message_counter_(message_counter),
        channel_data_counter_(channel_data_counter),
        attr_counter_(attr_counter) {}
  ~MessageObserver() override {}
  void ReceivedMessage(const TurnMessage* msg) override {
    if (message_counter_ != nullptr) {
      (*message_counter_)++;
    }
    // Implementation defined attributes are returned as ByteString
    const StunByteStringAttribute* attr =
        msg->GetByteString(TestTurnCustomizer::STUN_ATTR_COUNTER);
    if (attr != nullptr && attr_counter_ != nullptr) {
      ByteBufferReader buf(attr->array_view());
      unsigned int val = ~0u;
      buf.ReadUInt32(&val);
      (*attr_counter_)++;
    }
  }

  void ReceivedChannelData(std::span<const uint8_t> payload) override {
    if (channel_data_counter_ != nullptr) {
      (*channel_data_counter_)++;
    }
  }

  // Number of TurnMessages observed.
  unsigned int* message_counter_ = nullptr;

  // Number of channel data observed.
  unsigned int* channel_data_counter_ = nullptr;

  // Number of TurnMessages that had STUN_ATTR_COUNTER.
  unsigned int* attr_counter_ = nullptr;
};

// Test that an unauthenticated TURN ALLOCATE success response is NOT accepted
// if integrity is expected but not present.
// This is a regression test for b/504572664.
TEST_F(TurnPortTest, TestUnauthenticatedAllocateSuccessRejected) {
  SocketAddress fake_server_addr("99.99.99.99", 3478);
  CreateTurnPort(kTurnUsername, kTurnPassword,
                 ProtocolAddress(fake_server_addr, PROTO_UDP));

  std::unique_ptr<AsyncPacketSocket> server_socket =
      socket_factory()->CreateUdpSocket(env_, fake_server_addr, 0, 0);

  std::string transaction_id;
  server_socket->RegisterReceivedPacketCallback(
      [&](AsyncPacketSocket* /* socket */, const ReceivedIpPacket& packet) {
        ByteBufferReader reader(packet.payload());
        TurnMessage msg;
        if (msg.Read(&reader) && msg.type() == STUN_ALLOCATE_REQUEST) {
          transaction_id = msg.transaction_id();
        }
      });

  turn_port_->PrepareAddress();

  // Wait for the request to reach the server.
  ASSERT_TRUE(
      WaitUntil([&] { return !transaction_id.empty(); },
                {.timeout = kSimulatedRtt, .clock = &time_controller_}));

  TurnMessage forged_success(STUN_ALLOCATE_RESPONSE, transaction_id);

  // Add required attributes for ALLOCATE success.
  SocketAddress relayed_addr("198.51.100.99", 6666);
  SocketAddress mapped_addr("203.0.113.77", 5555);

  forged_success.AddAttribute(std::make_unique<StunXorAddressAttribute>(
      STUN_ATTR_XOR_RELAYED_ADDRESS, relayed_addr));
  forged_success.AddAttribute(std::make_unique<StunXorAddressAttribute>(
      STUN_ATTR_XOR_MAPPED_ADDRESS, mapped_addr));
  forged_success.AddAttribute(
      std::make_unique<StunUInt32Attribute>(STUN_ATTR_LIFETIME, 300));

  ByteBufferWriter buf;
  forged_success.Write(&buf);

  // Send the forged response to the TurnPort.
  SocketAddress local_addr = turn_port_->socket()->GetLocalAddress();
  AsyncSocketPacketOptions local_options;
  server_socket->SendTo(buf.Data(), buf.Length(), local_addr, local_options);

  // Wait a bit for the packet to be processed.
  time_controller_.AdvanceTime(kSimulatedRtt);

  // If vulnerable, turn_ready_ would be true because it accepted the forged
  // success. The correct behavior is to reject it and eventually fail or
  // receive the 401.
  EXPECT_FALSE(turn_ready_)
      << "Vulnerability present: Unauthenticated ALLOCATE success accepted!";
}

// Do a TURN allocation, establish a TLS connection, and send some data.
// Add customizer and check that it get called.
TEST_F(TurnPortTest, TestTurnCustomizerCount) {
  unsigned int observer_message_counter = 0;
  unsigned int observer_channel_data_counter = 0;
  unsigned int observer_attr_counter = 0;
  TestTurnCustomizer* customizer = new TestTurnCustomizer();
  std::unique_ptr<MessageObserver> validator(new MessageObserver(
      &observer_message_counter, &observer_channel_data_counter,
      &observer_attr_counter));

  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TLS);
  turn_customizer_.reset(customizer);
  turn_server_.server()->SetStunMessageObserver(std::move(validator));

  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnTlsProtoAddr);
  // Don't expect ECN to be propagated via TCP
  TestTurnSendData(PROTO_TLS, /*expect_ecn_propagate=*/false);
  EXPECT_EQ(TLS_PROTOCOL_NAME, turn_port_->Candidates()[0].relay_protocol());

  // There should have been at least turn_packets_.size() calls to `customizer`.
  EXPECT_GE(customizer->modify_cnt_ + customizer->allow_channel_data_cnt_,
            turn_packets_.size());

  // Some channel data should be received.
  EXPECT_GE(observer_channel_data_counter, 0u);

  // Need to release TURN port before the customizer.
  turn_port_.reset(nullptr);
}

// Do a TURN allocation, establish a TLS connection, and send some data.
// Add customizer and check that it can can prevent usage of channel data.
TEST_F(TurnPortTest, TestTurnCustomizerDisallowChannelData) {
  unsigned int observer_message_counter = 0;
  unsigned int observer_channel_data_counter = 0;
  unsigned int observer_attr_counter = 0;
  TestTurnCustomizer* customizer = new TestTurnCustomizer();
  std::unique_ptr<MessageObserver> validator(new MessageObserver(
      &observer_message_counter, &observer_channel_data_counter,
      &observer_attr_counter));
  customizer->allow_channel_data_ = false;
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TLS);
  turn_customizer_.reset(customizer);
  turn_server_.server()->SetStunMessageObserver(std::move(validator));

  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnTlsProtoAddr);
  // Don't expect ECN to propagate via TCP.
  TestTurnSendData(PROTO_TLS, /*expect_ecn_propagate=*/false);
  EXPECT_EQ(TLS_PROTOCOL_NAME, turn_port_->Candidates()[0].relay_protocol());

  // There should have been at least turn_packets_.size() calls to `customizer`.
  EXPECT_GE(customizer->modify_cnt_, turn_packets_.size());

  // No channel data should be received.
  EXPECT_EQ(observer_channel_data_counter, 0u);

  // Need to release TURN port before the customizer.
  turn_port_.reset(nullptr);
}

// Do a TURN allocation, establish a TLS connection, and send some data.
// Add customizer and check that it can add attribute to messages.
// This destroys the integrity checks on the message.
// TODO: crbug.com/504567957 - enable once integrity issues have been sorted.
TEST_F(TurnPortTest, DISABLED_TestTurnCustomizerAddAttribute) {
  unsigned int observer_message_counter = 0;
  unsigned int observer_channel_data_counter = 0;
  unsigned int observer_attr_counter = 0;
  TestTurnCustomizer* customizer = new TestTurnCustomizer();
  std::unique_ptr<MessageObserver> validator(new MessageObserver(
      &observer_message_counter, &observer_channel_data_counter,
      &observer_attr_counter));
  customizer->allow_channel_data_ = false;
  customizer->add_counter_ = true;
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TLS);
  turn_customizer_.reset(customizer);
  turn_server_.server()->SetStunMessageObserver(std::move(validator));

  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnTlsProtoAddr);
  // Don't expect ECN to be propagated via TCP
  TestTurnSendData(PROTO_TLS, /*expect_ecn_propagate=*/false);
  EXPECT_EQ(TLS_PROTOCOL_NAME, turn_port_->Candidates()[0].relay_protocol());

  // There should have been at least turn_packets_.size() calls to `customizer`.
  EXPECT_GE(customizer->modify_cnt_, turn_packets_.size());

  // Everything will be sent as messages since channel data is disallowed.
  EXPECT_GE(customizer->modify_cnt_, observer_message_counter);

  // All messages should have attribute.
  EXPECT_EQ(observer_message_counter, observer_attr_counter);

  // At least allow_channel_data_cnt_ messages should have been sent.
  EXPECT_GE(customizer->modify_cnt_, customizer->allow_channel_data_cnt_);
  EXPECT_GE(customizer->allow_channel_data_cnt_, 0u);

  // No channel data should be received.
  EXPECT_EQ(observer_channel_data_counter, 0u);

  // Need to release TURN port before the customizer.
  turn_port_.reset(nullptr);
}

TEST_F(TurnPortTest, TestOverlongUsername) {
  std::string overlong_username(513, 'x');
  RelayCredentials credentials(overlong_username, kTurnPassword);
  EXPECT_FALSE(
      CreateTurnPort(overlong_username, kTurnPassword, kTurnTlsProtoAddr));
}

TEST_F(TurnPortTest, TestTurnDangerousServer) {
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnDangerousProtoAddr);
  ASSERT_FALSE(turn_port_);
}

TEST_F(TurnPortTest, TestTurnDangerousServerPermits53) {
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnPort53ProtoAddr);
  ASSERT_TRUE(turn_port_);
}

TEST_F(TurnPortTest, TestTurnDangerousServerPermits80) {
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnPort80ProtoAddr);
  ASSERT_TRUE(turn_port_);
}

TEST_F(TurnPortTest, TestTurnDangerousServerPermits443) {
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnPort443ProtoAddr);
  ASSERT_TRUE(turn_port_);
}

TEST_F(TurnPortTest, TestTurnDangerousAlternateServer) {
  const ProtocolType protocol_type = PROTO_TCP;
  std::vector<SocketAddress> redirect_addresses;
  redirect_addresses.push_back(kTurnDangerousAddr);

  TestTurnRedirector redirector(redirect_addresses);

  turn_server_.AddInternalSocket(kTurnIntAddr, protocol_type);
  turn_server_.AddInternalSocket(kTurnDangerousAddr, protocol_type);
  turn_server_.set_redirect_hook(&redirector);
  CreateTurnPort(kTurnUsername, kTurnPassword,
                 ProtocolAddress(kTurnIntAddr, protocol_type));

  // Retrieve the address before we run the state machine.
  const SocketAddress old_addr = turn_port_->server_address().address;

  turn_port_->PrepareAddress();
  // This should result in an error event.
  EXPECT_THAT(
      WaitUntil([&] { return error_event_.error_code; }, Ne(0),
                {.timeout = TimeToGetAlternateTurnCandidate(protocol_type),
                 .clock = &time_controller_}),
      IsRtcOk());
  // but should NOT result in the port turning ready, and no candidates
  // should be gathered.
  EXPECT_FALSE(turn_ready_);
  ASSERT_EQ(0U, turn_port_->Candidates().size());
}

// Redirects for UDP reuse the socket and don't go through PrepareAddress(), but
// must be blocked from going to a dangerous port too.
TEST_F(TurnPortTest, TestTurnDangerousAlternateServerUDP) {
  TestTurnAlternateServerBlocked(PROTO_UDP, /*ipv6=*/false, kTurnDangerousAddr);
}

class TurnPortWithMockDnsResolverTest : public TurnPortTest {
 public:
  TurnPortWithMockDnsResolverTest()
      : TurnPortTest(), socket_factory_(ss_.get()) {}

  PacketSocketFactory* socket_factory() override { return &socket_factory_; }

  void SetDnsResolverExpectations(
      MockDnsResolvingPacketSocketFactory::Expectations expectations) {
    socket_factory_.SetExpectations(expectations);
  }

 private:
  MockDnsResolvingPacketSocketFactory socket_factory_;
};

// Test an allocation from a TURN server specified by a hostname.
TEST_F(TurnPortWithMockDnsResolverTest, TestHostnameResolved) {
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnPortValidHostnameProtoAddr);
  SetDnsResolverExpectations([](MockAsyncDnsResolver* resolver,
                                MockAsyncDnsResolverResult* resolver_result) {
    EXPECT_CALL(*resolver, Start(kTurnValidAddr, /*family=*/AF_INET, _))
        .WillOnce([](const SocketAddress& addr, int family,
                     absl::AnyInvocable<void()> callback) { callback(); });
    EXPECT_CALL(*resolver, result)
        .WillRepeatedly(ReturnPointee(resolver_result));
    EXPECT_CALL(*resolver_result, GetError).WillRepeatedly(Return(0));
    EXPECT_CALL(*resolver_result, GetResolvedAddress(AF_INET, _))
        .WillOnce([](int /*family*/, SocketAddress* addr) {
          *addr = kTurnUdpIntAddr;
          return true;
        });
  });
  TestTurnAllocateSucceeds(kSimulatedRtt * 2);
}

// Test an allocation from a TURN server specified by a hostname on an IPv6
// network.
TEST_F(TurnPortWithMockDnsResolverTest, TestHostnameResolvedIPv6Network) {
  turn_server_.AddInternalSocket(kTurnUdpIPv6IntAddr, PROTO_UDP);
  CreateTurnPort(kLocalIPv6Addr, kTurnUsername, kTurnPassword,
                 kTurnPortValidHostnameProtoAddr);
  SetDnsResolverExpectations([](MockAsyncDnsResolver* resolver,
                                MockAsyncDnsResolverResult* resolver_result) {
    EXPECT_CALL(*resolver, Start(kTurnValidAddr, /*family=*/AF_INET6, _))
        .WillOnce([](const SocketAddress& addr, int family,
                     absl::AnyInvocable<void()> callback) { callback(); });
    EXPECT_CALL(*resolver, result)
        .WillRepeatedly(ReturnPointee(resolver_result));
    EXPECT_CALL(*resolver_result, GetError).WillRepeatedly(Return(0));
    EXPECT_CALL(*resolver_result, GetResolvedAddress(AF_INET6, _))
        .WillOnce([](int /*family*/, SocketAddress* addr) {
          *addr = kTurnUdpIPv6IntAddr;
          return true;
        });
  });
  TestTurnAllocateSucceeds(kSimulatedRtt * 2);
}

static struct IPAddressTypeTestConfig {
  absl::string_view address;
  IPAddressType address_type;
} kAllIPAddressTypeTestConfigs[] = {
    {.address = "127.0.0.1", .address_type = IPAddressType::kLoopback},
    {.address = "localhost", .address_type = IPAddressType::kLoopback},
    {.address = "::1", .address_type = IPAddressType::kLoopback},
    {.address = "10.0.0.3", .address_type = IPAddressType::kPrivate},
    {.address = "fd00:4860:4860::8844",
     .address_type = IPAddressType::kPrivate},
    {.address = "1.1.1.1", .address_type = IPAddressType::kPublic},
    {.address = "2001:4860:4860::8888", .address_type = IPAddressType::kPublic},
};

// Used by the test framework to print the param value for parameterized tests.
std::string PrintToString(const IPAddressTypeTestConfig& param) {
  return std::string(param.address);
}

class TurnPortIPAddressTypeMetricsTest
    : public TurnPortWithMockDnsResolverTest,
      public ::testing::WithParamInterface<IPAddressTypeTestConfig> {};

TEST_P(TurnPortIPAddressTypeMetricsTest, TestIPAddressTypeMetrics) {
  metrics::Reset();

  SetDnsResolverExpectations(
      [](MockAsyncDnsResolver* resolver,
         MockAsyncDnsResolverResult* resolver_result) {
        EXPECT_CALL(*resolver, Start(SocketAddress("localhost", 5000),
                                     /*family=*/AF_INET, _))
            .WillOnce([](const SocketAddress& /* addr */, int /* family */,
                         absl::AnyInvocable<void()> callback) { callback(); });
        EXPECT_CALL(*resolver, result)
            .WillRepeatedly(ReturnPointee(resolver_result));
        EXPECT_CALL(*resolver_result, GetError).WillRepeatedly(Return(0));
        EXPECT_CALL(*resolver_result, GetResolvedAddress(AF_INET, _))
            .WillOnce([](int /*family*/, SocketAddress* addr) {
              *addr = SocketAddress("127.0.0.1", 5000);
              return true;
            });
      });

  ProtocolAddress server_address({GetParam().address, 5000}, PROTO_UDP);
  SocketAddress local_address = server_address.address.family() == AF_INET6
                                    ? kLocalIPv6Addr
                                    : kLocalAddr1;
  CreateTurnPort(local_address, kTurnUsername, kTurnPassword, server_address);
  turn_port_->PrepareAddress();

  ASSERT_TRUE(WaitUntil([&] { return turn_port_->HasRequests(); }));

  auto samples =
      metrics::Samples("WebRTC.PeerConnection.Turn.ServerAddressType");
  ASSERT_EQ(samples.size(), 1u);
  EXPECT_EQ(samples[static_cast<int>(GetParam().address_type)], 1);
}

INSTANTIATE_TEST_SUITE_P(All,
                         TurnPortIPAddressTypeMetricsTest,
                         ::testing::ValuesIn(kAllIPAddressTypeTestConfigs));

}  // namespace webrtc
