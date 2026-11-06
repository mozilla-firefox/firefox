/*
 *  Copyright 2012 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include <cstdint>

#include "api/array_view.h"
#include "rtc_base/network/received_packet.h"
#if defined(WEBRTC_POSIX)
#include <dirent.h>

#include "absl/strings/string_view.h"
#endif

#include <list>
#include <memory>
#include <utility>
#include <vector>

#include "absl/types/optional.h"
#include "api/units/time_delta.h"
#include "p2p/base/basic_packet_socket_factory.h"
#include "p2p/base/connection.h"
#include "p2p/base/mock_dns_resolving_packet_socket_factory.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/port_allocator.h"
#include "p2p/base/stun_port.h"
#include "p2p/base/test_turn_customizer.h"
#include "p2p/base/test_turn_server.h"
#include "p2p/base/transport_description.h"
#include "p2p/base/turn_port.h"
#include "p2p/base/turn_server.h"
#include "rtc_base/buffer.h"
#include "rtc_base/byte_buffer.h"
#include "rtc_base/checks.h"
#include "rtc_base/fake_clock.h"
#include "rtc_base/gunit.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/socket.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/thread.h"
#include "rtc_base/time_utils.h"
#include "rtc_base/virtual_socket_server.h"
#include "test/gtest.h"
#include "test/scoped_key_value_config.h"

namespace {
using rtc::SocketAddress;

using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::ReturnPointee;
using ::testing::SetArgPointee;
using ::webrtc::IceCandidateType;

static const SocketAddress kLocalAddr1("11.11.11.11", 0);
static const SocketAddress kLocalAddr2("22.22.22.22", 0);
static const SocketAddress kLocalIPv6Addr("2401:fa00:4:1000:be30:5bff:fee5:c3",
                                          0);
static const SocketAddress kLocalIPv6Addr2("2401:fa00:4:2000:be30:5bff:fee5:d4",
                                           0);
static const SocketAddress kTurnUdpIntAddr("99.99.99.3",
                                           cricket::TURN_SERVER_PORT);
static const SocketAddress kTurnTcpIntAddr("99.99.99.4",
                                           cricket::TURN_SERVER_PORT);
static const SocketAddress kTurnUdpExtAddr("99.99.99.5", 0);
static const SocketAddress kTurnAlternateIntAddr("99.99.99.6",
                                                 cricket::TURN_SERVER_PORT);
// Port for redirecting to a TCP Web server. Should not work.
static const SocketAddress kTurnDangerousAddr("99.99.99.7", 81);
// Port 53 (the DNS port); should work.
static const SocketAddress kTurnPort53Addr("99.99.99.7", 53);
// Port 80 (the HTTP port); should work.
static const SocketAddress kTurnPort80Addr("99.99.99.7", 80);
// Port 443 (the HTTPS port); should work.
static const SocketAddress kTurnPort443Addr("99.99.99.7", 443);
// The default TURN server port.
static const SocketAddress kTurnIntAddr("99.99.99.7",
                                        cricket::TURN_SERVER_PORT);
static const SocketAddress kTurnIPv6IntAddr(
    "2400:4030:2:2c00:be30:abcd:efab:cdef",
    cricket::TURN_SERVER_PORT);
static const SocketAddress kTurnUdpIPv6IntAddr(
    "2400:4030:1:2c00:be30:abcd:efab:cdef",
    cricket::TURN_SERVER_PORT);
static const SocketAddress kTurnInvalidAddr("www.google.invalid.", 3478);
static const SocketAddress kTurnValidAddr("www.google.valid.", 3478);

static const char kCandidateFoundation[] = "foundation";
static const char kIceUfrag1[] = "TESTICEUFRAG0001";
static const char kIceUfrag2[] = "TESTICEUFRAG0002";
static const char kIcePwd1[] = "TESTICEPWD00000000000001";
static const char kIcePwd2[] = "TESTICEPWD00000000000002";
static const char kTurnUsername[] = "test";
static const char kTurnPassword[] = "test";
// This test configures the virtual socket server to simulate delay so that we
// can verify operations take no more than the expected number of round trips.
static constexpr unsigned int kSimulatedRtt = 50;
// Connection destruction may happen asynchronously, but it should only
// take one simulated clock tick.
static constexpr unsigned int kConnectionDestructionDelay = 1;
// This used to be 1 second, but that's not always enough for getaddrinfo().
// See: https://bugs.chromium.org/p/webrtc/issues/detail?id=5191
static constexpr unsigned int kResolverTimeout = 10000;

constexpr uint64_t kTiebreakerDefault = 44444;

static const cricket::ProtocolAddress kTurnUdpProtoAddr(kTurnUdpIntAddr,
                                                        cricket::PROTO_UDP);
static const cricket::ProtocolAddress kTurnTcpProtoAddr(kTurnTcpIntAddr,
                                                        cricket::PROTO_TCP);
static const cricket::ProtocolAddress kTurnTlsProtoAddr(kTurnTcpIntAddr,
                                                        cricket::PROTO_TLS);
static const cricket::ProtocolAddress kTurnUdpIPv6ProtoAddr(kTurnUdpIPv6IntAddr,
                                                            cricket::PROTO_UDP);
static const cricket::ProtocolAddress kTurnDangerousProtoAddr(
    kTurnDangerousAddr,
    cricket::PROTO_TCP);
static const cricket::ProtocolAddress kTurnPort53ProtoAddr(kTurnPort53Addr,
                                                           cricket::PROTO_TCP);
static const cricket::ProtocolAddress kTurnPort80ProtoAddr(kTurnPort80Addr,
                                                           cricket::PROTO_TCP);
static const cricket::ProtocolAddress kTurnPort443ProtoAddr(kTurnPort443Addr,
                                                            cricket::PROTO_TCP);
static const cricket::ProtocolAddress kTurnPortInvalidHostnameProtoAddr(
    kTurnInvalidAddr,
    cricket::PROTO_UDP);
static const cricket::ProtocolAddress kTurnPortValidHostnameProtoAddr(
    kTurnValidAddr,
    cricket::PROTO_UDP);

#if defined(WEBRTC_LINUX) && !defined(WEBRTC_ANDROID)
static int GetFDCount() {
  struct dirent* dp;
  int fd_count = 0;
  DIR* dir = opendir("/proc/self/fd/");
  while ((dp = readdir(dir)) != NULL) {
    if (dp->d_name[0] == '.')
      continue;
    ++fd_count;
  }
  closedir(dir);
  return fd_count;
}
#endif

}  // unnamed namespace

namespace cricket {

class TurnPortTestVirtualSocketServer : public rtc::VirtualSocketServer {
 public:
  TurnPortTestVirtualSocketServer() {
    // This configures the virtual socket server to always add a simulated
    // delay of exactly half of kSimulatedRtt.
    set_delay_mean(kSimulatedRtt / 2);
    UpdateDelayDistribution();
  }

  using rtc::VirtualSocketServer::LookupBinding;
};

class TestConnectionWrapper : public sigslot::has_slots<> {
 public:
  explicit TestConnectionWrapper(Connection* conn) : connection_(conn) {
    conn->SignalDestroyed.connect(
        this, &TestConnectionWrapper::OnConnectionDestroyed);
  }

  ~TestConnectionWrapper() {
    if (connection_) {
      connection_->SignalDestroyed.disconnect(this);
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
class TurnPortTest : public ::testing::Test,
                     public TurnPort::CallbacksForTest,
                     public sigslot::has_slots<> {
 public:
  TurnPortTest()
      : ss_(new TurnPortTestVirtualSocketServer()),
        main_(ss_.get()),
        turn_server_(&main_, ss_.get(), kTurnUdpIntAddr, kTurnUdpExtAddr),
        socket_factory_(ss_.get()) {
    // Some code uses "last received time == 0" to represent "nothing received
    // so far", so we need to start the fake clock at a nonzero time...
    // TODO(deadbeef): Fix this.
    fake_clock_.AdvanceTime(webrtc::TimeDelta::Seconds(1));
  }

  void OnTurnPortComplete(Port* port) { turn_ready_ = true; }
  void OnTurnPortError(Port* port) { turn_error_ = true; }
  void OnCandidateError(Port* port,
                        const cricket::IceCandidateErrorEvent& event) {
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
  void OnSocketReadPacket(rtc::AsyncPacketSocket* socket,
                          const rtc::ReceivedPacket& packet) {
    turn_port_->HandleIncomingPacket(socket, packet);
  }
  void OnTurnPortDestroyed(PortInterface* port) { turn_port_destroyed_ = true; }

  // TurnPort::TestCallbacks
  void OnTurnCreatePermissionResult(int code) override {
    turn_create_permission_success_ = (code == 0);
  }
  void OnTurnRefreshResult(int code) override {
    turn_refresh_success_ = (code == 0);
  }
  void OnTurnPortClosed() override { turn_port_closed_ = true; }

  void OnConnectionSignalDestroyed(Connection* connection) {
    connection->DeregisterReceivedPacketCallback();
  }

  rtc::Socket* CreateServerSocket(const SocketAddress addr) {
    rtc::Socket* socket = ss_->CreateSocket(AF_INET, SOCK_STREAM);
    EXPECT_GE(socket->Bind(addr), 0);
    EXPECT_GE(socket->Listen(5), 0);
    return socket;
  }

  rtc::Network* MakeNetwork(const SocketAddress& addr) {
    networks_.emplace_back("unittest", "unittest", addr.ipaddr(), 32);
    networks_.back().AddIP(addr.ipaddr());
    return &networks_.back();
  }

  bool CreateTurnPort(absl::string_view username,
                      absl::string_view password,
                      const ProtocolAddress& server_address) {
    return CreateTurnPortWithAllParams(MakeNetwork(kLocalAddr1), username,
                                       password, server_address);
  }
  bool CreateTurnPort(const rtc::SocketAddress& local_address,
                      absl::string_view username,
                      absl::string_view password,
                      const ProtocolAddress& server_address) {
    return CreateTurnPortWithAllParams(MakeNetwork(local_address), username,
                                       password, server_address);
  }

  bool CreateTurnPortWithNetwork(const rtc::Network* network,
                                 absl::string_view username,
                                 absl::string_view password,
                                 const ProtocolAddress& server_address) {
    return CreateTurnPortWithAllParams(network, username, password,
                                       server_address);
  }

  // Version of CreateTurnPort that takes all possible parameters; all other
  // helper methods call this, such that "SetIceRole" and "ConnectSignals" (and
  // possibly other things in the future) only happen in one place.
  bool CreateTurnPortWithAllParams(const rtc::Network* network,
                                   absl::string_view username,
                                   absl::string_view password,
                                   const ProtocolAddress& server_address) {
    RelayServerConfig config;
    config.credentials = RelayCredentials(username, password);
    CreateRelayPortArgs args;
    args.network_thread = &main_;
    args.socket_factory = socket_factory();
    args.network = network;
    args.username = kIceUfrag1;
    args.password = kIcePwd1;
    args.server_address = &server_address;
    args.config = &config;
    args.turn_customizer = turn_customizer_.get();
    args.field_trials = &field_trials_;

    turn_port_ = TurnPort::Create(args, 0, 0);
    if (!turn_port_) {
      return false;
    }
    // This TURN port will be the controlling.
    turn_port_->SetIceRole(ICEROLE_CONTROLLING);
    turn_port_->SetIceTiebreaker(kTiebreakerDefault);
    ConnectSignals();

    if (server_address.proto == cricket::PROTO_TLS) {
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
      socket_.reset(socket_factory()->CreateUdpSocket(
          rtc::SocketAddress(kLocalAddr1.ipaddr(), 0), 0, 0));
      ASSERT_TRUE(socket_ != NULL);
      socket_->RegisterReceivedPacketCallback(
          [&](rtc::AsyncPacketSocket* socket,
              const rtc::ReceivedPacket& packet) {
            OnSocketReadPacket(socket, packet);
          });
    }

    RelayServerConfig config;
    config.credentials = RelayCredentials(username, password);
    CreateRelayPortArgs args;
    args.network_thread = &main_;
    args.socket_factory = socket_factory();
    args.network = MakeNetwork(kLocalAddr1);
    args.username = kIceUfrag1;
    args.password = kIcePwd1;
    args.server_address = &server_address;
    args.config = &config;
    args.turn_customizer = turn_customizer_.get();
    args.field_trials = &field_trials_;
    turn_port_ = TurnPort::Create(args, socket_.get());
    // This TURN port will be the controlling.
    turn_port_->SetIceRole(ICEROLE_CONTROLLING);
    turn_port_->SetIceTiebreaker(kTiebreakerDefault);
    ConnectSignals();
  }

  void ConnectSignals() {
    turn_port_->SignalPortComplete.connect(this,
                                           &TurnPortTest::OnTurnPortComplete);
    turn_port_->SignalPortError.connect(this, &TurnPortTest::OnTurnPortError);
    turn_port_->SignalCandidateError.connect(this,
                                             &TurnPortTest::OnCandidateError);
    turn_port_->SignalUnknownAddress.connect(
        this, &TurnPortTest::OnTurnUnknownAddress);
    turn_port_->SubscribePortDestroyed(
        [this](PortInterface* port) { OnTurnPortDestroyed(port); });
    turn_port_->SetCallbacksForTest(this);
  }

  void CreateUdpPort() { CreateUdpPort(kLocalAddr2); }

  void CreateUdpPort(const SocketAddress& address) {
    udp_port_ = UDPPort::Create(&main_, socket_factory(), MakeNetwork(address),
                                0, 0, kIceUfrag2, kIcePwd2, false,
                                absl::nullopt, &field_trials_);
    // UDP port will be controlled.
    udp_port_->SetIceRole(ICEROLE_CONTROLLED);
    udp_port_->SetIceTiebreaker(kTiebreakerDefault);
    udp_port_->SignalPortComplete.connect(this,
                                          &TurnPortTest::OnUdpPortComplete);
  }

  void PrepareTurnAndUdpPorts(ProtocolType protocol_type) {
    // turn_port_ should have been created.
    ASSERT_TRUE(turn_port_ != nullptr);
    turn_port_->PrepareAddress();
    ASSERT_TRUE_SIMULATED_WAIT(
        turn_ready_, TimeToGetTurnCandidate(protocol_type), fake_clock_);

    CreateUdpPort();
    udp_port_->PrepareAddress();
    ASSERT_TRUE_SIMULATED_WAIT(udp_ready_, kSimulatedRtt, fake_clock_);
  }

  // Returns the fake clock time to establish a connection over the given
  // protocol.
  int TimeToConnect(ProtocolType protocol_type) {
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
        return 0;
    }
  }

  // Returns the total fake clock time to establish a connection with a TURN
  // server over the given protocol and to allocate a TURN candidate.
  int TimeToGetTurnCandidate(ProtocolType protocol_type) {
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
  int TimeToGetAlternateTurnCandidate(ProtocolType protocol_type) {
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
      if (!CheckConnectionFailedAndPruned(kv.second)) {
        return false;
      }
    }
    return true;
  }

  void TestTurnAllocateSucceeds(unsigned int timeout) {
    ASSERT_TRUE(turn_port_);
    turn_port_->PrepareAddress();
    EXPECT_TRUE_SIMULATED_WAIT(turn_ready_, timeout, fake_clock_);
    ASSERT_EQ(1U, turn_port_->Candidates().size());
    EXPECT_EQ(kTurnUdpExtAddr.ipaddr(),
              turn_port_->Candidates()[0].address().ipaddr());
    EXPECT_NE(0, turn_port_->Candidates()[0].address().port());
  }

  void TestReconstructedServerUrl(ProtocolType protocol_type,
                                  absl::string_view expected_url) {
    ASSERT_TRUE(turn_port_);
    turn_port_->PrepareAddress();
    ASSERT_TRUE_SIMULATED_WAIT(
        turn_ready_, TimeToGetTurnCandidate(protocol_type), fake_clock_);
    ASSERT_EQ(1U, turn_port_->Candidates().size());
    EXPECT_EQ(turn_port_->Candidates()[0].url(), expected_url);
  }

  void TestTurnAlternateServer(ProtocolType protocol_type) {
    std::vector<rtc::SocketAddress> redirect_addresses;
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
    EXPECT_TRUE_SIMULATED_WAIT(turn_ready_,
                               TimeToGetAlternateTurnCandidate(protocol_type),
                               fake_clock_);
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
    std::vector<rtc::SocketAddress> redirect_addresses;
    redirect_addresses.push_back(kTurnIPv6IntAddr);

    TestTurnRedirector redirector(redirect_addresses);
    turn_server_.AddInternalSocket(kTurnIntAddr, protocol_type);
    turn_server_.set_redirect_hook(&redirector);
    CreateTurnPort(kTurnUsername, kTurnPassword,
                   ProtocolAddress(kTurnIntAddr, protocol_type));
    turn_port_->PrepareAddress();
    // Need time to connect to TURN server, send Allocate request and receive
    // redirect notice.
    EXPECT_TRUE_SIMULATED_WAIT(
        turn_error_, kSimulatedRtt + TimeToConnect(protocol_type), fake_clock_);
  }

  void TestTurnAlternateServerPingPong(ProtocolType protocol_type) {
    std::vector<rtc::SocketAddress> redirect_addresses;
    redirect_addresses.push_back(kTurnAlternateIntAddr);
    redirect_addresses.push_back(kTurnIntAddr);

    TestTurnRedirector redirector(redirect_addresses);

    turn_server_.AddInternalSocket(kTurnIntAddr, protocol_type);
    turn_server_.AddInternalSocket(kTurnAlternateIntAddr, protocol_type);
    turn_server_.set_redirect_hook(&redirector);
    CreateTurnPort(kTurnUsername, kTurnPassword,
                   ProtocolAddress(kTurnIntAddr, protocol_type));

    turn_port_->PrepareAddress();
    EXPECT_TRUE_SIMULATED_WAIT(turn_error_,
                               TimeToGetAlternateTurnCandidate(protocol_type),
                               fake_clock_);
    ASSERT_EQ(0U, turn_port_->Candidates().size());
    rtc::SocketAddress address;
    // Verify that we have exhausted all alternate servers instead of
    // failure caused by other errors.
    EXPECT_FALSE(redirector.ShouldRedirect(address, &address));
  }

  void TestTurnAlternateServerDetectRepetition(ProtocolType protocol_type) {
    std::vector<rtc::SocketAddress> redirect_addresses;
    redirect_addresses.push_back(kTurnAlternateIntAddr);
    redirect_addresses.push_back(kTurnAlternateIntAddr);

    TestTurnRedirector redirector(redirect_addresses);

    turn_server_.AddInternalSocket(kTurnIntAddr, protocol_type);
    turn_server_.AddInternalSocket(kTurnAlternateIntAddr, protocol_type);
    turn_server_.set_redirect_hook(&redirector);
    CreateTurnPort(kTurnUsername, kTurnPassword,
                   ProtocolAddress(kTurnIntAddr, protocol_type));

    turn_port_->PrepareAddress();
    EXPECT_TRUE_SIMULATED_WAIT(turn_error_,
                               TimeToGetAlternateTurnCandidate(protocol_type),
                               fake_clock_);
    ASSERT_EQ(0U, turn_port_->Candidates().size());
  }

  // A certain security exploit works by redirecting to a loopback address,
  // which doesn't ever actually make sense. So redirects to loopback should
  // be treated as errors.
  // See: https://bugs.chromium.org/p/chromium/issues/detail?id=649118
  void TestTurnAlternateServerLoopback(ProtocolType protocol_type, bool ipv6) {
    const SocketAddress& local_address = ipv6 ? kLocalIPv6Addr : kLocalAddr1;
    const SocketAddress& server_address =
        ipv6 ? kTurnIPv6IntAddr : kTurnIntAddr;

    std::vector<rtc::SocketAddress> redirect_addresses;
    // Pick an unusual address in the 127.0.0.0/8 range to make sure more than
    // 127.0.0.1 is covered.
    SocketAddress loopback_address(ipv6 ? "::1" : "127.1.2.3",
                                   TURN_SERVER_PORT);
    redirect_addresses.push_back(loopback_address);

    // Make a socket and bind it to the local port, to make extra sure no
    // packet is sent to this address.
    std::unique_ptr<rtc::Socket> loopback_socket(ss_->CreateSocket(
        AF_INET, protocol_type == PROTO_UDP ? SOCK_DGRAM : SOCK_STREAM));
    ASSERT_NE(nullptr, loopback_socket.get());
    ASSERT_EQ(0, loopback_socket->Bind(loopback_address));
    if (protocol_type == PROTO_TCP) {
      ASSERT_EQ(0, loopback_socket->Listen(1));
    }

    TestTurnRedirector redirector(redirect_addresses);

    turn_server_.AddInternalSocket(server_address, protocol_type);
    turn_server_.set_redirect_hook(&redirector);
    CreateTurnPort(local_address, kTurnUsername, kTurnPassword,
                   ProtocolAddress(server_address, protocol_type));

    turn_port_->PrepareAddress();
    EXPECT_TRUE_SIMULATED_WAIT(
        turn_error_, TimeToGetTurnCandidate(protocol_type), fake_clock_);

    // Wait for some extra time, and make sure no packets were received on the
    // loopback port we created (or in the case of TCP, no connection attempt
    // occurred).
    SIMULATED_WAIT(false, kSimulatedRtt, fake_clock_);
    if (protocol_type == PROTO_UDP) {
      char buf[1];
      EXPECT_EQ(-1, loopback_socket->Recv(&buf, 1, nullptr));
    } else {
      std::unique_ptr<rtc::Socket> accepted_socket(
          loopback_socket->Accept(nullptr));
      EXPECT_EQ(nullptr, accepted_socket.get());
    }
  }

  void TestTurnConnection(ProtocolType protocol_type) {
    // Create ports and prepare addresses.
    PrepareTurnAndUdpPorts(protocol_type);

    // Send ping from UDP to TURN.
    ASSERT_GE(turn_port_->Candidates().size(), 1U);
    Connection* conn1 = udp_port_->CreateConnection(turn_port_->Candidates()[0],
                                                    Port::ORIGIN_MESSAGE);
    ASSERT_TRUE(conn1 != NULL);
    conn1->Ping(0);
    SIMULATED_WAIT(!turn_unknown_address_, kSimulatedRtt * 2, fake_clock_);
    EXPECT_FALSE(turn_unknown_address_);
    EXPECT_FALSE(conn1->receiving());
    EXPECT_EQ(Connection::STATE_WRITE_INIT, conn1->write_state());

    // Send ping from TURN to UDP.
    Connection* conn2 = turn_port_->CreateConnection(udp_port_->Candidates()[0],
                                                     Port::ORIGIN_MESSAGE);
    ASSERT_TRUE(conn2 != NULL);
    ASSERT_TRUE_SIMULATED_WAIT(turn_create_permission_success_, kSimulatedRtt,
                               fake_clock_);
    conn2->Ping(0);

    // Two hops from TURN port to UDP port through TURN server, thus two RTTs.
    EXPECT_EQ_SIMULATED_WAIT(Connection::STATE_WRITABLE, conn2->write_state(),
                             kSimulatedRtt * 2, fake_clock_);
    EXPECT_TRUE(conn1->receiving());
    EXPECT_TRUE(conn2->receiving());
    EXPECT_EQ(Connection::STATE_WRITE_INIT, conn1->write_state());

    // Send another ping from UDP to TURN.
    conn1->Ping(0);
    EXPECT_EQ_SIMULATED_WAIT(Connection::STATE_WRITABLE, conn1->write_state(),
                             kSimulatedRtt * 2, fake_clock_);
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

    ASSERT_TRUE(conn2 != NULL);
    ASSERT_TRUE_SIMULATED_WAIT(turn_create_permission_success_, kSimulatedRtt,
                               fake_clock_);
    // Make sure turn connection can receive.
    conn1->Ping(0);
    EXPECT_EQ_SIMULATED_WAIT(Connection::STATE_WRITABLE, conn1->write_state(),
                             kSimulatedRtt * 2, fake_clock_);
    EXPECT_FALSE(turn_unknown_address_);

    // Destroy the connection on the TURN port. The TurnEntry still exists, so
    // the TURN port should still process a ping from an unknown address.
    turn_port_->DestroyConnection(conn2);

    conn1->Ping(0);
    EXPECT_TRUE_SIMULATED_WAIT(turn_unknown_address_, kSimulatedRtt,
                               fake_clock_);

    // Wait for TurnEntry to expire. Timeout is 5 minutes.
    // Expect that it still processes an incoming ping and signals the
    // unknown address.
    turn_unknown_address_ = false;
    fake_clock_.AdvanceTime(webrtc::TimeDelta::Seconds(5 * 60));

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

    rtc::ByteBufferWriter buf;
    msg->Write(&buf);
    conn1->Send(buf.Data(), buf.Length(), options);

    // Now restore the password before continuing.
    conn1->set_remote_password_for_test(pwd);

    conn1->Ping(0);
    EXPECT_TRUE_SIMULATED_WAIT(turn_unknown_address_, kSimulatedRtt,
                               fake_clock_);

    // If the connection is created again, it will start to receive pings.
    conn2 = turn_port_->CreateConnection(udp_port_->Candidates()[0],
                                         Port::ORIGIN_MESSAGE);
    conn1->Ping(0);
    EXPECT_TRUE_SIMULATED_WAIT(conn2->receiving(), kSimulatedRtt, fake_clock_);
  }

  void TestTurnSendData(ProtocolType protocol_type) {
    PrepareTurnAndUdpPorts(protocol_type);

    // Create connections and send pings.
    Connection* conn1 = turn_port_->CreateConnection(udp_port_->Candidates()[0],
                                                     Port::ORIGIN_MESSAGE);
    Connection* conn2 = udp_port_->CreateConnection(turn_port_->Candidates()[0],
                                                    Port::ORIGIN_MESSAGE);
    ASSERT_TRUE(conn1 != NULL);
    ASSERT_TRUE(conn2 != NULL);
    conn1->RegisterReceivedPacketCallback(
        [&](Connection* connection, const rtc::ReceivedPacket& packet) {
          turn_packets_.push_back(
              rtc::Buffer(packet.payload().data(), packet.payload().size()));
        });
    conn1->SignalDestroyed.connect(this,
                                   &TurnPortTest::OnConnectionSignalDestroyed);
    conn2->RegisterReceivedPacketCallback(
        [&](Connection* connection, const rtc::ReceivedPacket& packet) {
          udp_packets_.push_back(
              rtc::Buffer(packet.payload().data(), packet.payload().size()));
        });
    conn2->SignalDestroyed.connect(this,
                                   &TurnPortTest::OnConnectionSignalDestroyed);
    conn1->Ping(0);
    EXPECT_EQ_SIMULATED_WAIT(Connection::STATE_WRITABLE, conn1->write_state(),
                             kSimulatedRtt * 2, fake_clock_);
    conn2->Ping(0);
    EXPECT_EQ_SIMULATED_WAIT(Connection::STATE_WRITABLE, conn2->write_state(),
                             kSimulatedRtt * 2, fake_clock_);

    // Send some data.
    size_t num_packets = 256;
    for (size_t i = 0; i < num_packets; ++i) {
      unsigned char buf[256] = {0};
      for (size_t j = 0; j < i + 1; ++j) {
        buf[j] = 0xFF - static_cast<unsigned char>(j);
      }
      conn1->Send(buf, i + 1, options);
      conn2->Send(buf, i + 1, options);
      SIMULATED_WAIT(false, kSimulatedRtt, fake_clock_);
    }

    // Check the data.
    ASSERT_EQ(num_packets, turn_packets_.size());
    ASSERT_EQ(num_packets, udp_packets_.size());
    for (size_t i = 0; i < num_packets; ++i) {
      EXPECT_EQ(i + 1, turn_packets_[i].size());
      EXPECT_EQ(i + 1, udp_packets_[i].size());
      EXPECT_EQ(turn_packets_[i], udp_packets_[i]);
    }
  }

  // Test that a TURN allocation is released when the port is closed.
  void TestTurnReleaseAllocation(ProtocolType protocol_type) {
    PrepareTurnAndUdpPorts(protocol_type);
    turn_port_.reset();
    EXPECT_EQ_SIMULATED_WAIT(0U, turn_server_.server()->allocations().size(),
                             kSimulatedRtt, fake_clock_);
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
    ASSERT_TRUE(conn1 != NULL);
    ASSERT_TRUE(conn2 != NULL);
    conn1->RegisterReceivedPacketCallback(
        [&](Connection* connection, const rtc::ReceivedPacket& packet) {
          turn_packets_.push_back(
              rtc::Buffer(packet.payload().data(), packet.payload().size()));
        });
    conn1->SignalDestroyed.connect(this,
                                   &TurnPortTest::OnConnectionSignalDestroyed);
    conn2->RegisterReceivedPacketCallback(
        [&](Connection* connection, const rtc::ReceivedPacket& packet) {
          udp_packets_.push_back(
              rtc::Buffer(packet.payload().data(), packet.payload().size()));
        });
    conn2->SignalDestroyed.connect(this,
                                   &TurnPortTest::OnConnectionSignalDestroyed);

    conn1->Ping(0);
    EXPECT_EQ_SIMULATED_WAIT(Connection::STATE_WRITABLE, conn1->write_state(),
                             kSimulatedRtt * 2, fake_clock_);
    conn2->Ping(0);
    EXPECT_EQ_SIMULATED_WAIT(Connection::STATE_WRITABLE, conn2->write_state(),
                             kSimulatedRtt * 2, fake_clock_);

    // Send some data from Udp to TurnPort.
    unsigned char buf[256] = {0};
    conn2->Send(buf, sizeof(buf), options);

    // Now release the TurnPort allocation.
    // This will send a REFRESH with lifetime 0 to server.
    turn_port_->Release();

    // Wait for the TurnPort to signal closed.
    ASSERT_TRUE_SIMULATED_WAIT(turn_port_closed_, kSimulatedRtt, fake_clock_);

    // But the data should have arrived first.
    ASSERT_EQ(1ul, turn_packets_.size());
    EXPECT_EQ(sizeof(buf), turn_packets_[0].size());

    // The allocation is released at server.
    EXPECT_EQ(0U, turn_server_.server()->allocations().size());
  }

 protected:
  virtual rtc::PacketSocketFactory* socket_factory() {
    return &socket_factory_;
  }

  webrtc::test::ScopedKeyValueConfig field_trials_;
  rtc::ScopedFakeClock fake_clock_;
  // When a "create port" helper method is called with an IP, we create a
  // Network with that IP and add it to this list. Using a list instead of a
  // vector so that when it grows, pointers aren't invalidated.
  std::list<rtc::Network> networks_;
  std::unique_ptr<TurnPortTestVirtualSocketServer> ss_;
  rtc::AutoSocketServerThread main_;
  std::unique_ptr<rtc::AsyncPacketSocket> socket_;
  TestTurnServer turn_server_;
  std::unique_ptr<TurnPort> turn_port_;
  std::unique_ptr<UDPPort> udp_port_;
  bool turn_ready_ = false;
  bool turn_error_ = false;
  bool turn_unknown_address_ = false;
  bool turn_create_permission_success_ = false;
  bool turn_port_closed_ = false;
  bool turn_port_destroyed_ = false;
  bool udp_ready_ = false;
  bool test_finish_ = false;
  bool turn_refresh_success_ = false;
  std::vector<rtc::Buffer> turn_packets_;
  std::vector<rtc::Buffer> udp_packets_;
  rtc::PacketOptions options;
  std::unique_ptr<webrtc::TurnCustomizer> turn_customizer_;
  cricket::IceCandidateErrorEvent error_event_;

 private:
  rtc::BasicPacketSocketFactory socket_factory_;
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
  EXPECT_TRUE_WAIT(turn_error_, kResolverTimeout);
  std::string server_url =
      "turn:" + kTurnInvalidAddr.ToString() + "?transport=udp";
  ASSERT_EQ(error_event_.url, server_url);
}

// Do a normal TURN allocation.
TEST_F(TurnPortTest, TestTurnAllocate) {
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);
  EXPECT_EQ(0, turn_port_->SetOption(rtc::Socket::OPT_SNDBUF, 10 * 1024));
  TestTurnAllocateSucceeds(kSimulatedRtt * 2);
}

class TurnLoggingIdValidator : public StunMessageObserver {
 public:
  explicit TurnLoggingIdValidator(const char* expect_val)
      : expect_val_(expect_val) {}
  ~TurnLoggingIdValidator() {}
  void ReceivedMessage(const TurnMessage* msg) override {
    if (msg->type() == cricket::STUN_ALLOCATE_REQUEST) {
      const StunByteStringAttribute* attr =
          msg->GetByteString(cricket::STUN_ATTR_TURN_LOGGING_ID);
      if (expect_val_) {
        ASSERT_NE(nullptr, attr);
        ASSERT_EQ(expect_val_, attr->string_view());
      } else {
        EXPECT_EQ(nullptr, attr);
      }
    }
  }
  void ReceivedChannelData(rtc::ArrayView<const uint8_t> packet) override {}

 private:
  const char* expect_val_;
};

TEST_F(TurnPortTest, TestTurnAllocateWithLoggingId) {
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);
  turn_port_->SetTurnLoggingId("KESO");
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
  EXPECT_TRUE_SIMULATED_WAIT(turn_error_, kSimulatedRtt * 3, fake_clock_);
  ASSERT_EQ(0U, turn_port_->Candidates().size());
  EXPECT_EQ_SIMULATED_WAIT(error_event_.error_code, STUN_ERROR_UNAUTHORIZED,
                           kSimulatedRtt * 3, fake_clock_);
  EXPECT_EQ(error_event_.error_text, "Unauthorized");
}

// Testing a normal UDP allocation using TCP connection.
TEST_F(TurnPortTest, TestTurnTcpAllocate) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnTcpProtoAddr);
  EXPECT_EQ(0, turn_port_->SetOption(rtc::Socket::OPT_SNDBUF, 10 * 1024));
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
  EXPECT_EQ(0, turn_port_->SetOption(rtc::Socket::OPT_SNDBUF, 10 * 1024));
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
  EXPECT_TRUE_SIMULATED_WAIT(turn_error_, kSimulatedRtt, fake_clock_);
  EXPECT_EQ_SIMULATED_WAIT(error_event_.error_code, STUN_ERROR_GLOBAL_FAILURE,
                           kSimulatedRtt, fake_clock_);
  ASSERT_NE(error_event_.error_text.find('.'), std::string::npos);
  ASSERT_NE(error_event_.address.find(kLocalAddr2.HostAsSensitiveURIString()),
            std::string::npos);
  ASSERT_NE(error_event_.port, 0);
  std::string server_url =
      "turn:" + kTurnTcpIntAddr.ToString() + "?transport=tcp";
  ASSERT_EQ(error_event_.url, server_url);
}

// A caveat for the above logic: if the socket ends up bound to one of the IPs
// associated with the Network, just not the "best" one, this is ok.
TEST_F(TurnPortTest, TurnTcpAllocationNotDiscardedIfNotBoundToBestIP) {
  // Sockets bound to kLocalAddr1 will actually end up with kLocalAddr2.
  ss_->SetAlternativeLocalAddress(kLocalAddr1.ipaddr(), kLocalAddr2.ipaddr());

  // Set up a network with kLocalAddr1 as the "best" IP, and kLocalAddr2 as an
  // alternate.
  rtc::Network* network = MakeNetwork(kLocalAddr1);
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
  EXPECT_TRUE_SIMULATED_WAIT(turn_ready_, kSimulatedRtt * 3, fake_clock_);
  ASSERT_EQ(1U, turn_port_->Candidates().size());

  // Verify that the socket actually used the alternate address, otherwise this
  // test isn't doing what it meant to.
  ASSERT_EQ(kLocalAddr2.ipaddr(),
            turn_port_->Candidates()[0].related_address().ipaddr());
}

// Regression test for crbug.com/webrtc/8972, caused by buggy comparison
// between rtc::IPAddress and rtc::InterfaceAddress.
TEST_F(TurnPortTest, TCPPortNotDiscardedIfBoundToTemporaryIP) {
  networks_.emplace_back("unittest", "unittest", kLocalIPv6Addr.ipaddr(), 32);
  networks_.back().AddIP(rtc::InterfaceAddress(
      kLocalIPv6Addr.ipaddr(), rtc::IPV6_ADDRESS_FLAG_TEMPORARY));

  // Set up TURN server to use TCP (this logic only exists for TCP).
  turn_server_.AddInternalSocket(kTurnIPv6IntAddr, PROTO_TCP);

  // Create TURN port using our special Network, and tell it to start
  // allocation.
  CreateTurnPortWithNetwork(
      &networks_.back(), kTurnUsername, kTurnPassword,
      cricket::ProtocolAddress(kTurnIPv6IntAddr, PROTO_TCP));
  turn_port_->PrepareAddress();

  // Candidate should be gathered as normally.
  EXPECT_TRUE_SIMULATED_WAIT(turn_ready_, kSimulatedRtt * 3, fake_clock_);
  ASSERT_EQ(1U, turn_port_->Candidates().size());
}

// Testing turn port will attempt to create TCP socket on address resolution
// failure.
TEST_F(TurnPortTest, TestTurnTcpOnAddressResolveFailure) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
  CreateTurnPort(kTurnUsername, kTurnPassword,
                 ProtocolAddress(kTurnInvalidAddr, PROTO_TCP));
  turn_port_->PrepareAddress();
  EXPECT_TRUE_WAIT(turn_error_, kResolverTimeout);
  // As VSS doesn't provide DNS resolution, name resolve will fail. TurnPort
  // will proceed in creating a TCP socket which will fail as there is no
  // server on the above domain and error will be set to SOCKET_ERROR.
  EXPECT_EQ(SOCKET_ERROR, turn_port_->error());
  EXPECT_EQ_SIMULATED_WAIT(error_event_.error_code, SERVER_NOT_REACHABLE_ERROR,
                           kSimulatedRtt, fake_clock_);
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
  EXPECT_TRUE_WAIT(turn_error_, kResolverTimeout);
  EXPECT_EQ(SOCKET_ERROR, turn_port_->error());
}

// In case of UDP on address resolve failure, TurnPort will not create socket
// and return allocate failure.
TEST_F(TurnPortTest, TestTurnUdpOnAddressResolveFailure) {
  CreateTurnPort(kTurnUsername, kTurnPassword,
                 ProtocolAddress(kTurnInvalidAddr, PROTO_UDP));
  turn_port_->PrepareAddress();
  EXPECT_TRUE_WAIT(turn_error_, kResolverTimeout);
  // Error from turn port will not be socket error.
  EXPECT_NE(SOCKET_ERROR, turn_port_->error());
}

// Try to do a TURN allocation with an invalid password.
TEST_F(TurnPortTest, TestTurnAllocateBadPassword) {
  CreateTurnPort(kTurnUsername, "bad", kTurnUdpProtoAddr);
  turn_port_->PrepareAddress();
  EXPECT_TRUE_SIMULATED_WAIT(turn_error_, kSimulatedRtt * 2, fake_clock_);
  ASSERT_EQ(0U, turn_port_->Candidates().size());
}

// Tests that TURN port nonce will be reset when receiving an ALLOCATE MISMATCH
// error.
TEST_F(TurnPortTest, TestTurnAllocateNonceResetAfterAllocateMismatch) {
  // Do a normal allocation first.
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);
  turn_port_->PrepareAddress();
  EXPECT_TRUE_SIMULATED_WAIT(turn_ready_, kSimulatedRtt * 2, fake_clock_);
  rtc::SocketAddress first_addr(turn_port_->socket()->GetLocalAddress());
  // Destroy the turnport while keeping the drop probability to 1 to
  // suppress the release of the allocation at the server.
  ss_->set_drop_probability(1.0);
  turn_port_.reset();
  SIMULATED_WAIT(false, kSimulatedRtt, fake_clock_);
  ss_->set_drop_probability(0.0);

  // Force the socket server to assign the same port.
  ss_->SetNextPortForTesting(first_addr.port());
  turn_ready_ = false;
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);

  // It is expected that the turn port will first get a nonce from the server
  // using timestamp `ts_before` but then get an allocate mismatch error and
  // receive an even newer nonce based on the system clock. `ts_before` is
  // chosen so that the two NONCEs generated by the server will be different.
  int64_t ts_before = rtc::TimeMillis() - 1;
  std::string first_nonce =
      turn_server_.server()->SetTimestampForNextNonce(ts_before);
  turn_port_->PrepareAddress();

  // Four round trips; first we'll get "stale nonce", then
  // "allocate mismatch", then "stale nonce" again, then finally it will
  // succeed.
  EXPECT_TRUE_SIMULATED_WAIT(turn_ready_, kSimulatedRtt * 4, fake_clock_);
  EXPECT_NE(first_nonce, turn_port_->nonce());
}

// Tests that a new local address is created after
// STUN_ERROR_ALLOCATION_MISMATCH.
TEST_F(TurnPortTest, TestTurnAllocateMismatch) {
  // Do a normal allocation first.
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);
  turn_port_->PrepareAddress();
  EXPECT_TRUE_SIMULATED_WAIT(turn_ready_, kSimulatedRtt * 2, fake_clock_);
  rtc::SocketAddress first_addr(turn_port_->socket()->GetLocalAddress());

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
  EXPECT_TRUE_SIMULATED_WAIT(turn_ready_, kSimulatedRtt * 4, fake_clock_);

  // Verifies that the new port has a different address now.
  EXPECT_NE(first_addr, turn_port_->socket()->GetLocalAddress());

  // Verify that all packets received from the shared socket are ignored.
  std::string test_packet = "Test packet";
  EXPECT_FALSE(turn_port_->HandleIncomingPacket(
      socket_.get(),
      rtc::ReceivedPacket::CreateFromLegacy(
          test_packet.data(), test_packet.size(), rtc::TimeMicros(),
          rtc::SocketAddress(kTurnUdpExtAddr.ipaddr(), 0))));
}

// Tests that a shared-socket-TurnPort creates its own socket after
// STUN_ERROR_ALLOCATION_MISMATCH.
TEST_F(TurnPortTest, TestSharedSocketAllocateMismatch) {
  // Do a normal allocation first.
  CreateSharedTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);
  turn_port_->PrepareAddress();
  EXPECT_TRUE_SIMULATED_WAIT(turn_ready_, kSimulatedRtt * 2, fake_clock_);
  rtc::SocketAddress first_addr(turn_port_->socket()->GetLocalAddress());

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
  EXPECT_TRUE_SIMULATED_WAIT(turn_ready_, kSimulatedRtt * 4, fake_clock_);

  // Verifies that the new port has a different address now.
  EXPECT_NE(first_addr, turn_port_->socket()->GetLocalAddress());
  EXPECT_FALSE(turn_port_->SharedSocket());
}

TEST_F(TurnPortTest, TestTurnTcpAllocateMismatch) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnTcpProtoAddr);

  // Do a normal allocation first.
  turn_port_->PrepareAddress();
  EXPECT_TRUE_SIMULATED_WAIT(turn_ready_, kSimulatedRtt * 3, fake_clock_);
  rtc::SocketAddress first_addr(turn_port_->socket()->GetLocalAddress());

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
  EXPECT_TRUE_SIMULATED_WAIT(turn_ready_, kSimulatedRtt * 5, fake_clock_);

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
  turn_refresh_success_ = false;
  // This sends out the first RefreshRequest with correct credentials.
  // When this succeeds, it will schedule a new RefreshRequest with the bad
  // credential.
  turn_port_->request_manager().FlushForTest(TURN_REFRESH_REQUEST);
  EXPECT_TRUE_SIMULATED_WAIT(turn_refresh_success_, kSimulatedRtt, fake_clock_);
  // Flush it again, it will receive a bad response.
  turn_port_->request_manager().FlushForTest(TURN_REFRESH_REQUEST);
  EXPECT_TRUE_SIMULATED_WAIT(!turn_refresh_success_, kSimulatedRtt,
                             fake_clock_);
  EXPECT_FALSE(turn_port_->connected());
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
  ASSERT_TRUE(conn1 != NULL);
  ASSERT_TRUE(conn2 != NULL);
  // Make sure conn2 is writable.
  conn2->Ping(0);
  EXPECT_EQ_SIMULATED_WAIT(Connection::STATE_WRITABLE, conn2->write_state(),
                           kSimulatedRtt * 2, fake_clock_);

  turn_port_->CloseForTest();
  SIMULATED_WAIT(false, kSimulatedRtt, fake_clock_);
  turn_unknown_address_ = false;
  conn2->Ping(0);
  SIMULATED_WAIT(false, kSimulatedRtt, fake_clock_);
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
  ASSERT_TRUE(conn1 != NULL);

  // Close the socket and create a connection again.
  turn_port_->OnSocketClose(turn_port_->socket(), 1);
  conn1 = turn_port_->CreateConnection(udp_port_->Candidates()[0],
                                       Port::ORIGIN_MESSAGE);
  ASSERT_TRUE(conn1 == NULL);
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
  EXPECT_TRUE_SIMULATED_WAIT(turn_port_->connections().empty(),
                             kConnectionDestructionDelay, fake_clock_);
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
  ASSERT_TRUE(conn != NULL);
  EXPECT_TRUE_SIMULATED_WAIT(turn_create_permission_success_, kSimulatedRtt,
                             fake_clock_);
  turn_create_permission_success_ = false;
  // A create-permission-request should be pending.
  // After the next create-permission-response is received, it will schedule
  // another request with bad_ufrag and bad_pwd.
  RelayCredentials bad_credentials("bad_user", "bad_pwd");
  turn_port_->set_credentials(bad_credentials);
  turn_port_->request_manager().FlushForTest(kAllRequestsForTest);
  EXPECT_TRUE_SIMULATED_WAIT(turn_create_permission_success_, kSimulatedRtt,
                             fake_clock_);
  // Flush the requests again; the create-permission-request will fail.
  turn_port_->request_manager().FlushForTest(kAllRequestsForTest);
  EXPECT_TRUE_SIMULATED_WAIT(!turn_create_permission_success_, kSimulatedRtt,
                             fake_clock_);
  EXPECT_TRUE(CheckConnectionFailedAndPruned(conn));
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
  conn1->Ping(0);
  EXPECT_TRUE_SIMULATED_WAIT(conn1->writable(), kSimulatedRtt * 2, fake_clock_);
  // TODO(deadbeef): SetEntryChannelId should not be a public method.
  // Instead we should set an option on the fake TURN server to force it to
  // send a channel bind errors.
  ASSERT_TRUE(
      turn_port_->SetEntryChannelId(udp_port_->Candidates()[0].address(), -1));

  std::string data = "ABC";
  conn1->Send(data.data(), data.length(), options);

  EXPECT_TRUE_SIMULATED_WAIT(CheckConnectionFailedAndPruned(conn1),
                             kSimulatedRtt, fake_clock_);
  // Verify that packets are allowed to be sent after a bind request error.
  // They'll just use a send indication instead.

  conn2->RegisterReceivedPacketCallback(
      [&](Connection* connection, const rtc::ReceivedPacket& packet) {
        udp_packets_.push_back(
            rtc::Buffer(packet.payload().data(), packet.payload().size()));
      });
  conn1->Send(data.data(), data.length(), options);
  EXPECT_TRUE_SIMULATED_WAIT(!udp_packets_.empty(), kSimulatedRtt, fake_clock_);
  conn2->DeregisterReceivedPacketCallback();
}

// Do a TURN allocation, establish a UDP connection, and send some data.
TEST_F(TurnPortTest, TestTurnSendDataTurnUdpToUdp) {
  // Create ports and prepare addresses.
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnUdpProtoAddr);
  TestTurnSendData(PROTO_UDP);
  EXPECT_EQ(UDP_PROTOCOL_NAME, turn_port_->Candidates()[0].relay_protocol());
}

// Do a TURN allocation, establish a TCP connection, and send some data.
TEST_F(TurnPortTest, TestTurnSendDataTurnTcpToUdp) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
  // Create ports and prepare addresses.
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnTcpProtoAddr);
  TestTurnSendData(PROTO_TCP);
  EXPECT_EQ(TCP_PROTOCOL_NAME, turn_port_->Candidates()[0].relay_protocol());
}

// Do a TURN allocation, establish a TLS connection, and send some data.
TEST_F(TurnPortTest, TestTurnSendDataTurnTlsToUdp) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TLS);
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnTlsProtoAddr);
  TestTurnSendData(PROTO_TLS);
  EXPECT_EQ(TLS_PROTOCOL_NAME, turn_port_->Candidates()[0].relay_protocol());
}

// Test TURN fails to make a connection from IPv6 address to a server which has
// IPv4 address.
TEST_F(TurnPortTest, TestTurnLocalIPv6AddressServerIPv4) {
  turn_server_.AddInternalSocket(kTurnUdpIPv6IntAddr, PROTO_UDP);
  CreateTurnPort(kLocalIPv6Addr, kTurnUsername, kTurnPassword,
                 kTurnUdpProtoAddr);
  turn_port_->PrepareAddress();
  ASSERT_TRUE_SIMULATED_WAIT(turn_error_, kSimulatedRtt, fake_clock_);
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
  EXPECT_TRUE_SIMULATED_WAIT(turn_ready_, kSimulatedRtt * 2, fake_clock_);
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
  EXPECT_TRUE_SIMULATED_WAIT(turn_ready_, kSimulatedRtt * 3, fake_clock_);

  CreateUdpPort(SocketAddress("10.0.0.10", 0));
  udp_port_->PrepareAddress();
  EXPECT_TRUE_SIMULATED_WAIT(udp_ready_, kSimulatedRtt, fake_clock_);
  // Create a connection.
  TestConnectionWrapper conn(turn_port_->CreateConnection(
      udp_port_->Candidates()[0], Port::ORIGIN_MESSAGE));
  EXPECT_TRUE(conn.connection() != nullptr);

  // Asynchronously, CreatePermission request should be sent and fail, which
  // will make the connection pruned and failed.
  EXPECT_TRUE_SIMULATED_WAIT(CheckConnectionFailedAndPruned(conn.connection()),
                             kSimulatedRtt, fake_clock_);
  EXPECT_TRUE_SIMULATED_WAIT(!turn_create_permission_success_, kSimulatedRtt,
                             fake_clock_);
  // Check that the connection is not deleted asynchronously.
  SIMULATED_WAIT(conn.connection() == nullptr, kConnectionDestructionDelay,
                 fake_clock_);
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
  ASSERT_TRUE_WAIT(turn_error_, kResolverTimeout);
  EXPECT_TRUE(turn_port_->Candidates().empty());
  turn_port_.reset();
  rtc::Thread::Current()->PostTask([this] { test_finish_ = true; });
  // Waiting for above message to be processed.
  ASSERT_TRUE_SIMULATED_WAIT(test_finish_, 1, fake_clock_);
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
  virtual ~MessageObserver() {}
  void ReceivedMessage(const TurnMessage* msg) override {
    if (message_counter_ != nullptr) {
      (*message_counter_)++;
    }
    // Implementation defined attributes are returned as ByteString
    const StunByteStringAttribute* attr =
        msg->GetByteString(TestTurnCustomizer::STUN_ATTR_COUNTER);
    if (attr != nullptr && attr_counter_ != nullptr) {
      rtc::ByteBufferReader buf(attr->array_view());
      unsigned int val = ~0u;
      buf.ReadUInt32(&val);
      (*attr_counter_)++;
    }
  }

  void ReceivedChannelData(rtc::ArrayView<const uint8_t> payload) override {
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
  TestTurnSendData(PROTO_TLS);
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
  TestTurnSendData(PROTO_TLS);
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
TEST_F(TurnPortTest, TestTurnCustomizerAddAttribute) {
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
  TestTurnSendData(PROTO_TLS);
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
  std::vector<rtc::SocketAddress> redirect_addresses;
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
  EXPECT_TRUE_SIMULATED_WAIT(error_event_.error_code != 0,
                             TimeToGetAlternateTurnCandidate(protocol_type),
                             fake_clock_);
  // but should NOT result in the port turning ready, and no candidates
  // should be gathered.
  EXPECT_FALSE(turn_ready_);
  ASSERT_EQ(0U, turn_port_->Candidates().size());
}

class TurnPortWithMockDnsResolverTest : public TurnPortTest {
 public:
  TurnPortWithMockDnsResolverTest()
      : TurnPortTest(), socket_factory_(ss_.get()) {}

  rtc::PacketSocketFactory* socket_factory() override {
    return &socket_factory_;
  }

  void SetDnsResolverExpectations(
      rtc::MockDnsResolvingPacketSocketFactory::Expectations expectations) {
    socket_factory_.SetExpectations(expectations);
  }

 private:
  rtc::MockDnsResolvingPacketSocketFactory socket_factory_;
};

// Test an allocation from a TURN server specified by a hostname.
TEST_F(TurnPortWithMockDnsResolverTest, TestHostnameResolved) {
  CreateTurnPort(kTurnUsername, kTurnPassword, kTurnPortValidHostnameProtoAddr);
  SetDnsResolverExpectations(
      [](webrtc::MockAsyncDnsResolver* resolver,
         webrtc::MockAsyncDnsResolverResult* resolver_result) {
        EXPECT_CALL(*resolver, Start(kTurnValidAddr, /*family=*/AF_INET, _))
            .WillOnce([](const rtc::SocketAddress& addr, int family,
                         absl::AnyInvocable<void()> callback) { callback(); });
        EXPECT_CALL(*resolver, result)
            .WillRepeatedly(ReturnPointee(resolver_result));
        EXPECT_CALL(*resolver_result, GetError).WillRepeatedly(Return(0));
        EXPECT_CALL(*resolver_result, GetResolvedAddress(AF_INET, _))
            .WillOnce(DoAll(SetArgPointee<1>(kTurnUdpIntAddr), Return(true)));
      });
  TestTurnAllocateSucceeds(kSimulatedRtt * 2);
}

// Test an allocation from a TURN server specified by a hostname on an IPv6
// network.
TEST_F(TurnPortWithMockDnsResolverTest, TestHostnameResolvedIPv6Network) {
  turn_server_.AddInternalSocket(kTurnUdpIPv6IntAddr, PROTO_UDP);
  CreateTurnPort(kLocalIPv6Addr, kTurnUsername, kTurnPassword,
                 kTurnPortValidHostnameProtoAddr);
  SetDnsResolverExpectations(
      [](webrtc::MockAsyncDnsResolver* resolver,
         webrtc::MockAsyncDnsResolverResult* resolver_result) {
        EXPECT_CALL(*resolver, Start(kTurnValidAddr, /*family=*/AF_INET6, _))
            .WillOnce([](const rtc::SocketAddress& addr, int family,
                         absl::AnyInvocable<void()> callback) { callback(); });
        EXPECT_CALL(*resolver, result)
            .WillRepeatedly(ReturnPointee(resolver_result));
        EXPECT_CALL(*resolver_result, GetError).WillRepeatedly(Return(0));
        EXPECT_CALL(*resolver_result, GetResolvedAddress(AF_INET6, _))
            .WillOnce(
                DoAll(SetArgPointee<1>(kTurnUdpIPv6IntAddr), Return(true)));
      });
  TestTurnAllocateSucceeds(kSimulatedRtt * 2);
}

}  // namespace cricket
