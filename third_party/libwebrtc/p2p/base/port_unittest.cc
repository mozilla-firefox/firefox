/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "p2p/base/port.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "api/async_dns_resolver.h"
#include "api/candidate.h"
#include "api/environment/environment.h"
#include "api/field_trials.h"
#include "api/packet_socket_factory.h"
#include "api/test/mock_packet_socket_factory.h"
#include "api/test/rtc_error_matchers.h"
#include "api/transport/stun.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "p2p/base/basic_packet_socket_factory.h"
#include "p2p/base/connection.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/p2p_transport_channel_ice_field_trials.h"
#include "p2p/base/port_allocator.h"
#include "p2p/base/port_interface.h"
#include "p2p/base/stun_port.h"
#include "p2p/base/tcp_port.h"
#include "p2p/base/transport_description.h"
#include "p2p/base/turn_port.h"
#include "p2p/client/relay_port_factory_interface.h"
#include "p2p/test/nat_server.h"
#include "p2p/test/nat_socket_factory.h"
#include "p2p/test/nat_types.h"
#include "p2p/test/stun_server.h"
#include "p2p/test/test_port.h"
#include "p2p/test/test_stun_server.h"
#include "p2p/test/test_turn_server.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/byte_buffer.h"
#include "rtc_base/checks.h"
#include "rtc_base/crypto_random.h"
#include "rtc_base/dscp.h"
#include "rtc_base/logging.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/network.h"
#include "rtc_base/network/received_packet.h"
#include "rtc_base/network_constants.h"
#include "rtc_base/socket.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/system/unused.h"
#include "rtc_base/thread.h"
#include "rtc_base/virtual_socket_server.h"
#include "test/create_test_environment.h"
#include "test/create_test_field_trials.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/time_controller/simulated_time_controller.h"
#include "test/wait_until.h"

namespace webrtc {
namespace {

using ::testing::Eq;
using ::testing::IsNull;
using ::testing::IsTrue;
using ::testing::NotNull;
using ::testing::Return;

constexpr TimeDelta kDefaultTimeout = TimeDelta::Millis(3000);
constexpr TimeDelta kShortTimeout = TimeDelta::Millis(1000);
constexpr TimeDelta kMaxExpectedSimulatedRtt = TimeDelta::Millis(200);
constexpr TimeDelta kEpsilon = TimeDelta::Millis(1);
const SocketAddress kLocalAddr1("192.168.1.2", 0);
const SocketAddress kLocalAddr2("192.168.1.3", 0);
const SocketAddress kLinkLocalIPv6Addr("fe80::aabb:ccff:fedd:eeff", 0);
const SocketAddress kNatAddr1("77.77.77.77", NAT_SERVER_UDP_PORT);
const SocketAddress kNatAddr2("88.88.88.88", NAT_SERVER_UDP_PORT);
const SocketAddress kStunAddr("99.99.99.1", STUN_SERVER_PORT);
const SocketAddress kTurnUdpIntAddr("99.99.99.4", STUN_SERVER_PORT);
const SocketAddress kTurnTcpIntAddr("99.99.99.4", 5010);
const SocketAddress kTurnUdpExtAddr("99.99.99.5", 0);
const RelayCredentials kRelayCredentials("test", "test");

// TODO(?): Update these when RFC5245 is completely supported.
// Magic value of 30 is from RFC3484, for IPv4 addresses.
const uint32_t kDefaultPrflxPriority = ICE_TYPE_PREFERENCE_PRFLX << 24 |
                                       30 << 8 |
                                       (256 - ICE_CANDIDATE_COMPONENT_DEFAULT);

constexpr uint64_t kTiebreakerDefault = 44444;

constexpr uint8_t kTestData[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890";

Candidate GetCandidate(Port* port) {
  RTC_DCHECK_GE(port->Candidates().size(), 1);
  return port->Candidates()[0];
}

SocketAddress GetAddress(Port* port) {
  return GetCandidate(port).address();
}

std::unique_ptr<IceMessage> CopyStunMessage(const IceMessage& src) {
  auto dst = std::make_unique<IceMessage>();
  ByteBufferWriter buf;
  src.Write(&buf);
  ByteBufferReader read_buf(buf);
  dst->Read(&read_buf);
  return dst;
}

bool WriteStunMessage(const StunMessage& msg, ByteBufferWriter* buf) {
  buf->Resize(0);  // clear out any existing buffer contents
  return msg.Write(buf);
}

bool GetStunMessageFromBufferWriter(TestPort* port,
                                    ByteBufferWriter* buf,
                                    const SocketAddress& addr,
                                    std::unique_ptr<IceMessage>* out_msg,
                                    std::string* out_username) {
  return port->GetStunMessage(buf->DataView(), addr, out_msg, out_username);
}
void SendPingAndReceiveResponse(Connection* lconn,
                                TestPort* lport,
                                Connection* rconn,
                                TestPort* rport,
                                GlobalSimulatedTimeController& time_controller,
                                int64_t ms) {
  lconn->Ping();
  ASSERT_TRUE(
      WaitUntil([&] { return lport->last_stun_msg(); },
                {.timeout = kDefaultTimeout, .clock = &time_controller}));
  ASSERT_GT(lport->last_stun_buf().size(), 0u);
  rconn->OnReadPacket(
      ReceivedIpPacket(lport->last_stun_buf(), SocketAddress(), std::nullopt));

  time_controller.AdvanceTime(TimeDelta::Millis(ms));
  ASSERT_TRUE(
      WaitUntil([&] { return rport->last_stun_msg(); },
                {.timeout = kDefaultTimeout, .clock = &time_controller}));
  ASSERT_GT(rport->last_stun_buf().size(), 0u);
  lconn->OnReadPacket(
      ReceivedIpPacket(rport->last_stun_buf(), SocketAddress(), std::nullopt));
}

class TestChannel {
 public:
  // Takes ownership of `p1` (but not `p2`).
  TestChannel(std::unique_ptr<Port> p1,
              GlobalSimulatedTimeController& time_controller)
      : time_controller_(time_controller), port_(std::move(p1)) {
    port_->SubscribePortComplete(this,
                                 [this](Port* port) { OnPortComplete(port); });
    port_->SubscribeUnknownAddress(
        this, [this](PortInterface* port, const SocketAddress& address,
                     ProtocolType proto, IceMessage* msg, const std::string& rf,
                     bool port_muxed) {
          OnUnknownAddress(port, address, proto, msg, rf, port_muxed);
        });
    port_->SubscribePortDestroyed(
        this, [this](PortInterface* port) { OnSrcPortDestroyed(port); });
  }

  ~TestChannel() { Stop(); }

  int complete_count() { return complete_count_; }
  Connection* conn() { return conn_; }
  const SocketAddress& remote_address() { return remote_address_; }
  std::string remote_fragment() { return remote_frag_; }

  void Start() {
    port_->PrepareAddress();
    time_controller_.AdvanceTime(TimeDelta::Zero());
  }
  void CreateConnection(const Candidate& remote_candidate) {
    RTC_DCHECK(!conn_);
    conn_ = port_->CreateConnection(remote_candidate, Port::ORIGIN_MESSAGE);
    IceMode remote_ice_mode =
        (ice_mode_ == ICEMODE_FULL) ? ICEMODE_LITE : ICEMODE_FULL;
    conn_->set_use_candidate_attr(remote_ice_mode == ICEMODE_FULL);
    conn_->SubscribeStateChange(this, [this](Connection* connection) {
      OnConnectionStateChange(connection);
    });
    conn_->SubscribeDestroyed(
        this, [this](Connection* connection) { OnDestroyed(connection); });
    conn_->SubscribeReadyToSend(this, [this](Connection* connection) {
      OnConnectionReadyToSend(connection);
    });
    connection_ready_to_send_ = false;
  }

  void OnConnectionStateChange(Connection* conn) {
    if (conn->write_state() == Connection::STATE_WRITABLE) {
      conn->set_use_candidate_attr(true);
      nominated_ = true;
    }
  }
  void AcceptConnection(const Candidate& remote_candidate) {
    if (conn_) {
      conn_->UnsubscribeDestroyed(this);
      conn_ = nullptr;
    }
    ASSERT_TRUE(remote_request_.get() != nullptr);
    Candidate c = remote_candidate;
    c.set_address(remote_address_);
    conn_ = port_->CreateConnection(c, Port::ORIGIN_MESSAGE);
    conn_->SubscribeDestroyed(
        this, [this](Connection* connection) { OnDestroyed(connection); });
    conn_->SendStunBindingResponse(remote_request_.get());
    remote_request_.reset();
  }
  void Ping() { conn_->Ping(); }
  void Ping(Timestamp now) { conn_->Ping(now); }
  void Stop() {
    if (conn_) {
      port_->DestroyConnection(conn_);
      conn_ = nullptr;
    }
  }

  void OnPortComplete(Port* port) { complete_count_++; }
  void SetIceMode(IceMode ice_mode) { ice_mode_ = ice_mode; }

  int SendData(std::span<const uint8_t> data) {
    AsyncSocketPacketOptions options;
    return conn_->Send(data, options);
  }

  void OnUnknownAddress(PortInterface* port,
                        const SocketAddress& addr,
                        ProtocolType proto,
                        IceMessage* msg,
                        const std::string& rf,
                        bool /*port_muxed*/) {
    ASSERT_EQ(port_.get(), port);
    if (!remote_address_.IsNil()) {
      ASSERT_EQ(remote_address_, addr);
    }
    const StunUInt32Attribute* priority_attr =
        msg->GetUInt32(STUN_ATTR_PRIORITY);
    const StunByteStringAttribute* mi_attr =
        msg->GetByteString(STUN_ATTR_MESSAGE_INTEGRITY);
    const StunUInt32Attribute* fingerprint_attr =
        msg->GetUInt32(STUN_ATTR_FINGERPRINT);
    EXPECT_TRUE(priority_attr != nullptr);
    EXPECT_TRUE(mi_attr != nullptr);
    EXPECT_TRUE(fingerprint_attr != nullptr);
    remote_address_ = addr;
    remote_request_ = CopyStunMessage(*msg);
    remote_frag_ = rf;
  }

  void OnDestroyed(Connection* conn) {
    ASSERT_EQ(conn_, conn);
    RTC_LOG(LS_INFO) << "OnDestroy connection " << conn << " deleted";
    conn_ = nullptr;
    // When the connection is destroyed, also clear these fields so future
    // connections are possible.
    remote_request_.reset();
    remote_address_.Clear();
  }

  void OnSrcPortDestroyed(PortInterface* port) {
    Port* destroyed_src = port_.release();
    ASSERT_EQ(destroyed_src, port);
  }

  Port* port() { return port_.get(); }

  bool nominated() const { return nominated_; }

  void set_connection_ready_to_send(bool ready) {
    connection_ready_to_send_ = ready;
  }
  bool connection_ready_to_send() const { return connection_ready_to_send_; }

 private:
  // ReadyToSend will only issue after a Connection recovers from ENOTCONN
  void OnConnectionReadyToSend(Connection* conn) {
    ASSERT_EQ(conn, conn_);
    connection_ready_to_send_ = true;
  }

  GlobalSimulatedTimeController& time_controller_;
  IceMode ice_mode_ = ICEMODE_FULL;
  std::unique_ptr<Port> port_;

  int complete_count_ = 0;
  Connection* conn_ = nullptr;
  SocketAddress remote_address_;
  std::unique_ptr<StunMessage> remote_request_;
  std::string remote_frag_;
  bool nominated_ = false;
  bool connection_ready_to_send_ = false;
};

class PortTest : public ::testing::Test {
 public:
  PortTest()
      : ss_(new VirtualSocketServer()),
        time_controller_(Timestamp::Millis(1000), ss_.get()),
        main_(time_controller_.GetMainThread()),
        env_(CreateTestEnvironment({.time = &time_controller_})),
        socket_factory_(ss_.get()),
        nat_factory1_(ss_.get(), kNatAddr1, SocketAddress()),
        nat_factory2_(ss_.get(), kNatAddr2, SocketAddress()),
        nat_socket_factory1_(&nat_factory1_),
        nat_socket_factory2_(&nat_factory2_),
        stun_server_(TestStunServer::Create(env_, kStunAddr, *ss_, *main_)),
        turn_server_(env_, main_, ss_.get(), kTurnUdpIntAddr, kTurnUdpExtAddr),
        username_(CreateRandomString(ICE_UFRAG_LENGTH)),
        password_(CreateRandomString(ICE_PWD_LENGTH)),
        role_conflict_(false),
        ports_destroyed_(0) {}

  ~PortTest() override {
    // Workaround for tests that trigger async destruction of objects that we
    // need to give an opportunity here to run, before proceeding with other
    // teardown.
    time_controller_.AdvanceTime(TimeDelta::Zero());
  }

 protected:
  VirtualSocketServer* vss() { return ss_.get(); }
  const Environment& env() const { return env_; }

  std::string password() { return password_; }

  void TestLocalToLocal() {
    auto port1 = CreateUdpPort(kLocalAddr1);
    port1->SetIceRole(ICEROLE_CONTROLLING);
    auto port2 = CreateUdpPort(kLocalAddr2);
    port2->SetIceRole(ICEROLE_CONTROLLED);
    TestConnectivity("udp", std::move(port1), "udp", std::move(port2), true,
                     true, true, true);
  }
  void TestLocalToStun(NATType ntype) {
    auto port1 = CreateUdpPort(kLocalAddr1);
    port1->SetIceRole(ICEROLE_CONTROLLING);
    nat_server2_ = CreateNatServer(kNatAddr2, ntype);
    auto port2 = CreateStunPort(kLocalAddr2, &nat_socket_factory2_);
    port2->SetIceRole(ICEROLE_CONTROLLED);
    TestConnectivity("udp", std::move(port1), StunName(ntype), std::move(port2),
                     ntype == NAT_OPEN_CONE, true, ntype != NAT_SYMMETRIC,
                     true);
  }
  void TestLocalToRelay(ProtocolType proto) {
    auto port1 = CreateUdpPort(kLocalAddr1);
    port1->SetIceRole(ICEROLE_CONTROLLING);
    auto port2 = CreateRelayPort(kLocalAddr2, proto, PROTO_UDP);
    port2->SetIceRole(ICEROLE_CONTROLLED);
    TestConnectivity("udp", std::move(port1), RelayName(proto),
                     std::move(port2), false, true, true, true);
  }
  void TestStunToLocal(NATType ntype) {
    nat_server1_ = CreateNatServer(kNatAddr1, ntype);
    auto port1 = CreateStunPort(kLocalAddr1, &nat_socket_factory1_);
    port1->SetIceRole(ICEROLE_CONTROLLING);
    auto port2 = CreateUdpPort(kLocalAddr2);
    port2->SetIceRole(ICEROLE_CONTROLLED);
    TestConnectivity(StunName(ntype), std::move(port1), "udp", std::move(port2),
                     true, ntype != NAT_SYMMETRIC, true, true);
  }
  void TestStunToStun(NATType ntype1, NATType ntype2) {
    nat_server1_ = CreateNatServer(kNatAddr1, ntype1);
    auto port1 = CreateStunPort(kLocalAddr1, &nat_socket_factory1_);
    port1->SetIceRole(ICEROLE_CONTROLLING);
    nat_server2_ = CreateNatServer(kNatAddr2, ntype2);
    auto port2 = CreateStunPort(kLocalAddr2, &nat_socket_factory2_);
    port2->SetIceRole(ICEROLE_CONTROLLED);
    TestConnectivity(StunName(ntype1), std::move(port1), StunName(ntype2),
                     std::move(port2), ntype2 == NAT_OPEN_CONE,
                     ntype1 != NAT_SYMMETRIC, ntype2 != NAT_SYMMETRIC,
                     ntype1 + ntype2 < (NAT_PORT_RESTRICTED + NAT_SYMMETRIC));
  }
  void TestStunToRelay(NATType ntype, ProtocolType proto) {
    nat_server1_ = CreateNatServer(kNatAddr1, ntype);
    auto port1 = CreateStunPort(kLocalAddr1, &nat_socket_factory1_);
    port1->SetIceRole(ICEROLE_CONTROLLING);
    auto port2 = CreateRelayPort(kLocalAddr2, proto, PROTO_UDP);
    port2->SetIceRole(ICEROLE_CONTROLLED);
    TestConnectivity(StunName(ntype), std::move(port1), RelayName(proto),
                     std::move(port2), false, ntype != NAT_SYMMETRIC, true,
                     true);
  }
  void TestTcpToTcp() {
    auto port1 = CreateTcpPort(kLocalAddr1);
    port1->SetIceRole(ICEROLE_CONTROLLING);
    auto port2 = CreateTcpPort(kLocalAddr2);
    port2->SetIceRole(ICEROLE_CONTROLLED);
    TestConnectivity("tcp", std::move(port1), "tcp", std::move(port2), true,
                     false, true, true);
  }
  void TestTcpToRelay(ProtocolType proto) {
    auto port1 = CreateTcpPort(kLocalAddr1);
    port1->SetIceRole(ICEROLE_CONTROLLING);
    auto port2 = CreateRelayPort(kLocalAddr2, proto, PROTO_TCP);
    port2->SetIceRole(ICEROLE_CONTROLLED);
    TestConnectivity("tcp", std::move(port1), RelayName(proto),
                     std::move(port2), false, false, true, true);
  }
  void TestSslTcpToRelay(ProtocolType proto) {
    auto port1 = CreateTcpPort(kLocalAddr1);
    port1->SetIceRole(ICEROLE_CONTROLLING);
    auto port2 = CreateRelayPort(kLocalAddr2, proto, PROTO_SSLTCP);
    port2->SetIceRole(ICEROLE_CONTROLLED);
    TestConnectivity("ssltcp", std::move(port1), RelayName(proto),
                     std::move(port2), false, false, true, true);
  }

  Network* MakeNetwork(const SocketAddress& addr) {
    networks_.emplace_back("unittest", "unittest", addr.ipaddr(), 32);
    networks_.back().AddIP(addr.ipaddr());
    return &networks_.back();
  }

  Network* MakeNetworkMultipleAddrs(const SocketAddress& global_addr,
                                    const SocketAddress& link_local_addr) {
    networks_.emplace_back("unittest", "unittest", global_addr.ipaddr(), 32,
                           ADAPTER_TYPE_UNKNOWN);
    networks_.back().AddIP(link_local_addr.ipaddr());
    networks_.back().AddIP(global_addr.ipaddr());
    networks_.back().AddIP(link_local_addr.ipaddr());
    return &networks_.back();
  }

  // helpers for above functions
  std::unique_ptr<UDPPort> CreateUdpPort(const SocketAddress& addr) {
    return CreateUdpPort(addr, &socket_factory_);
  }
  std::unique_ptr<UDPPort> CreateUdpPort(const SocketAddress& addr,
                                         PacketSocketFactory* socket_factory) {
    return UDPPort::Create({.env = env_,
                            .network_thread = main_,
                            .socket_factory = socket_factory,
                            .network = MakeNetwork(addr),
                            .ice_username_fragment = username_,
                            .ice_password = password_,
                            .ice_tiebreaker = kTiebreakerDefault},
                           0, 0, true, std::nullopt);
  }

  std::unique_ptr<UDPPort> CreateUdpPortMultipleAddrs(
      const SocketAddress& global_addr,
      const SocketAddress& link_local_addr,
      PacketSocketFactory* socket_factory) {
    return UDPPort::Create(
        {.env = env_,
         .network_thread = main_,
         .socket_factory = socket_factory,
         .network = MakeNetworkMultipleAddrs(global_addr, link_local_addr),
         .ice_username_fragment = username_,
         .ice_password = password_,
         .ice_tiebreaker = kTiebreakerDefault},
        0, 0, true, std::nullopt);
  }
  std::unique_ptr<TCPPort> CreateTcpPort(const SocketAddress& addr) {
    return CreateTcpPort(addr, &socket_factory_);
  }
  std::unique_ptr<TCPPort> CreateTcpPort(const SocketAddress& addr,
                                         PacketSocketFactory* socket_factory) {
    return TCPPort::Create({.env = env_,
                            .network_thread = main_,
                            .socket_factory = socket_factory,
                            .network = MakeNetwork(addr),
                            .ice_username_fragment = username_,
                            .ice_password = password_,
                            .ice_tiebreaker = kTiebreakerDefault},
                           0, 0, true);
  }
  std::unique_ptr<StunPort> CreateStunPort(
      const SocketAddress& addr,
      PacketSocketFactory* socket_factory) {
    ServerAddresses stun_servers;
    stun_servers.insert(kStunAddr);
    return StunPort::Create({.env = env_,
                             .network_thread = main_,
                             .socket_factory = socket_factory,
                             .network = MakeNetwork(addr),
                             .ice_username_fragment = username_,
                             .ice_password = password_,
                             .ice_tiebreaker = kTiebreakerDefault},
                            0, 0, stun_servers, std::nullopt);
  }
  std::unique_ptr<Port> CreateRelayPort(const SocketAddress& addr,
                                        ProtocolType int_proto,
                                        ProtocolType ext_proto) {
    return CreateTurnPort(addr, &socket_factory_, int_proto, ext_proto);
  }
  std::unique_ptr<TurnPort> CreateTurnPort(const SocketAddress& addr,
                                           PacketSocketFactory* socket_factory,
                                           ProtocolType int_proto,
                                           ProtocolType ext_proto) {
    SocketAddress server_addr =
        int_proto == PROTO_TCP ? kTurnTcpIntAddr : kTurnUdpIntAddr;
    return CreateTurnPort(addr, socket_factory, int_proto, ext_proto,
                          server_addr);
  }
  std::unique_ptr<TurnPort> CreateTurnPort(const SocketAddress& addr,
                                           PacketSocketFactory* socket_factory,
                                           ProtocolType int_proto,
                                           ProtocolType ext_proto,
                                           const SocketAddress& server_addr) {
    RelayServerConfig config;
    config.credentials = kRelayCredentials;
    ProtocolAddress server_address(server_addr, int_proto);
    CreateRelayPortArgs args = {.env = env_};
    args.network_thread = main_;
    args.socket_factory = socket_factory;
    args.network = MakeNetwork(addr);
    args.username = username_;
    args.password = password_;
    args.server_address = &server_address;
    args.config = &config;
    args.ice_tiebreaker = kTiebreakerDefault;
    return TurnPort::Create(std::move(args), 0, 0);
  }

  std::unique_ptr<NATServer> CreateNatServer(const SocketAddress& addr,
                                             NATType type) {
    return std::make_unique<NATServer>(env_, type, *main_, ss_.get(), addr,
                                       addr, *main_, ss_.get(), addr);
  }
  static const char* StunName(NATType type) {
    switch (type) {
      case NAT_OPEN_CONE:
        return "stun(open cone)";
      case NAT_ADDR_RESTRICTED:
        return "stun(addr restricted)";
      case NAT_PORT_RESTRICTED:
        return "stun(port restricted)";
      case NAT_SYMMETRIC:
        return "stun(symmetric)";
      default:
        return "stun(?)";
    }
  }
  static const char* RelayName(ProtocolType proto) {
    switch (proto) {
      case PROTO_UDP:
        return "turn(udp)";
      case PROTO_TCP:
        return "turn(tcp)";
      case PROTO_SSLTCP:
        return "turn(ssltcp)";
      case PROTO_TLS:
        return "turn(tls)";
      default:
        return "turn(?)";
    }
  }

  void TestCrossFamilyPorts(int type);

  void ExpectPortsCanConnect(bool can_connect, Port* p1, Port* p2);

  // This does all the work and then deletes `port1` and `port2`.
  void TestConnectivity(absl::string_view name1,
                        std::unique_ptr<Port> port1,
                        absl::string_view name2,
                        std::unique_ptr<Port> port2,
                        bool accept,
                        bool same_addr1,
                        bool same_addr2,
                        bool possible);

  // This connects the provided channels which have already started.  `ch1`
  // should have its Connection created (either through CreateConnection() or
  // TCP reconnecting mechanism before entering this function.
  void ConnectStartedChannels(TestChannel* ch1, TestChannel* ch2) {
    ASSERT_TRUE(ch1->conn());
    EXPECT_TRUE(WaitUntil([&] { return ch1->conn()->connected(); },
                          {.timeout = kDefaultTimeout,
                           .clock = &time_controller_}));  // for TCP connect
    ch1->Ping();
    EXPECT_TRUE(
        WaitUntil([&] { return !ch2->remote_address().IsNil(); },
                  {.timeout = kShortTimeout, .clock = &time_controller_}));

    // Send a ping from dst to src.
    ch2->AcceptConnection(GetCandidate(ch1->port()));
    ch2->Ping();
    EXPECT_THAT(
        WaitUntil([&] { return ch2->conn()->write_state(); },
                  Eq(Connection::STATE_WRITABLE),
                  {.timeout = kDefaultTimeout, .clock = &time_controller_}),
        IsRtcOk());
  }

  // This connects and disconnects the provided channels in the same sequence as
  // TestConnectivity with all options set to `true`.  It does not delete either
  // channel.
  void StartConnectAndStopChannels(TestChannel* ch1, TestChannel* ch2) {
    // Acquire addresses.
    ch1->Start();
    ch2->Start();

    ch1->CreateConnection(GetCandidate(ch2->port()));
    ConnectStartedChannels(ch1, ch2);

    // Destroy the connections.
    ch1->Stop();
    ch2->Stop();
  }

  // This disconnects both end's Connection and make sure ch2 ready for new
  // connection.
  void DisconnectTcpTestChannels(TestChannel* ch1, TestChannel* ch2) {
    TCPConnection* tcp_conn1 = static_cast<TCPConnection*>(ch1->conn());
    TCPConnection* tcp_conn2 = static_cast<TCPConnection*>(ch2->conn());
    ASSERT_TRUE(
        ss_->CloseTcpConnections(tcp_conn1->socket()->GetLocalAddress(),
                                 tcp_conn2->socket()->GetLocalAddress()));

    // Wait for both OnClose are delivered.
    EXPECT_TRUE(
        WaitUntil([&] { return !ch1->conn()->connected(); },
                  {.timeout = kDefaultTimeout, .clock = &time_controller_}));
    EXPECT_TRUE(
        WaitUntil([&] { return !ch2->conn()->connected(); },
                  {.timeout = kDefaultTimeout, .clock = &time_controller_}));

    // Ensure redundant SignalClose events on TcpConnection won't break tcp
    // reconnection. Chromium will fire SignalClose for all outstanding IPC
    // packets during reconnection.
    tcp_conn1->socket()->NotifyClosedForTest(0);
    tcp_conn2->socket()->NotifyClosedForTest(0);

    // Speed up destroying ch2's connection such that the test is ready to
    // accept a new connection from ch1 before ch1's connection destroys itself.
    ch2->Stop();
    EXPECT_THAT(
        WaitUntil([&] { return ch2->conn(); }, IsNull(),
                  {.timeout = kDefaultTimeout, .clock = &time_controller_}),
        IsRtcOk());
  }

  void TestTcpReconnect(bool ping_after_disconnected,
                        bool send_after_disconnected) {
    auto port1 = CreateTcpPort(kLocalAddr1);
    port1->SetIceRole(ICEROLE_CONTROLLING);
    auto port2 = CreateTcpPort(kLocalAddr2);
    port2->SetIceRole(ICEROLE_CONTROLLED);

    port1->set_component(ICE_CANDIDATE_COMPONENT_DEFAULT);
    port2->set_component(ICE_CANDIDATE_COMPONENT_DEFAULT);

    // Set up channels and ensure both ports will be deleted.
    TestChannel ch1(std::move(port1), time_controller_);
    TestChannel ch2(std::move(port2), time_controller_);
    EXPECT_EQ(0, ch1.complete_count());
    EXPECT_EQ(0, ch2.complete_count());

    ch1.Start();
    ch2.Start();
    ASSERT_THAT(
        WaitUntil([&] { return ch1.complete_count(); }, Eq(1),
                  {.timeout = kDefaultTimeout, .clock = &time_controller_}),
        IsRtcOk());
    ASSERT_THAT(
        WaitUntil([&] { return ch2.complete_count(); }, Eq(1),
                  {.timeout = kDefaultTimeout, .clock = &time_controller_}),
        IsRtcOk());

    // Initial connecting the channel, create connection on channel1.
    ch1.CreateConnection(GetCandidate(ch2.port()));
    ConnectStartedChannels(&ch1, &ch2);

    // Shorten the timeout period.
    const TimeDelta kTcpReconnectTimeout = kDefaultTimeout;
    static_cast<TCPConnection*>(ch1.conn())
        ->SetReconnectionTimeoutForTesting(kTcpReconnectTimeout);
    static_cast<TCPConnection*>(ch2.conn())
        ->SetReconnectionTimeoutForTesting(kTcpReconnectTimeout);

    EXPECT_FALSE(ch1.connection_ready_to_send());
    EXPECT_FALSE(ch2.connection_ready_to_send());

    // Once connected, disconnect them.
    DisconnectTcpTestChannels(&ch1, &ch2);

    if (send_after_disconnected || ping_after_disconnected) {
      if (send_after_disconnected) {
        // First SendData after disconnect should fail but will trigger
        // reconnect.
        EXPECT_EQ(-1, ch1.SendData(kTestData));
      }

      if (ping_after_disconnected) {
        // Ping should trigger reconnect.
        ch1.Ping();
      }

      // Wait for channel's outgoing TCPConnection connected.
      EXPECT_TRUE(
          WaitUntil([&] { return ch1.conn()->connected(); },
                    {.timeout = kDefaultTimeout, .clock = &time_controller_}));

      // Verify that we could still connect channels.
      ConnectStartedChannels(&ch1, &ch2);
      EXPECT_TRUE(WaitUntil(
          [&] { return ch1.connection_ready_to_send(); },
          {.timeout = kTcpReconnectTimeout, .clock = &time_controller_}));
      // Channel2 is the passive one so a new connection is created during
      // reconnect. This new connection should never have issued ENOTCONN
      // hence the connection_ready_to_send() should be false.
      EXPECT_FALSE(ch2.connection_ready_to_send());
    } else {
      EXPECT_EQ(ch1.conn()->write_state(), Connection::STATE_WRITABLE);
      // Since the reconnection never happens, the connections should have been
      // destroyed after the timeout.
      EXPECT_TRUE(WaitUntil([&] { return !ch1.conn(); },
                            {.timeout = kTcpReconnectTimeout + kDefaultTimeout,
                             .clock = &time_controller_}));
      EXPECT_TRUE(!ch2.conn());
    }

    // Tear down and ensure that goes smoothly.
    ch1.Stop();
    ch2.Stop();
    EXPECT_THAT(
        WaitUntil([&] { return ch1.conn(); }, IsNull(),
                  {.timeout = kDefaultTimeout, .clock = &time_controller_}),
        IsRtcOk());
    EXPECT_THAT(
        WaitUntil([&] { return ch2.conn(); }, IsNull(),
                  {.timeout = kDefaultTimeout, .clock = &time_controller_}),
        IsRtcOk());
  }

  std::unique_ptr<IceMessage> CreateStunMessage(StunMessageType type) {
    auto msg = std::make_unique<IceMessage>(type, "TESTTESTTEST");
    return msg;
  }
  std::unique_ptr<IceMessage> CreateStunMessageWithUsername(
      StunMessageType type,
      absl::string_view username) {
    std::unique_ptr<IceMessage> msg = CreateStunMessage(type);
    msg->AddAttribute(std::make_unique<StunByteStringAttribute>(
        STUN_ATTR_USERNAME, std::string(username)));
    return msg;
  }
  std::unique_ptr<TestPort> CreateTestPort(
      const SocketAddress& addr,
      absl::string_view username,
      absl::string_view password,
      const FieldTrialsView* field_trials = nullptr) {
    auto port = std::make_unique<TestPort>(
        Port::PortParametersRef{
            .env = CreateTestEnvironment(
                {.field_trials = field_trials, .time = &time_controller_}),
            .network_thread = main_,
            .socket_factory = &socket_factory_,
            .network = MakeNetwork(addr),
            .ice_username_fragment = username,
            .ice_password = password,
            .ice_tiebreaker = kTiebreakerDefault},
        0, 0);
    port->SubscribeRoleConflict([this]() { OnRoleConflict(); });
    return port;
  }
  std::unique_ptr<TestPort> CreateTestPort(const SocketAddress& addr,
                                           absl::string_view username,
                                           absl::string_view password,
                                           IceRole role) {
    auto port = CreateTestPort(addr, username, password);
    port->SetIceRole(role);
    return port;
  }
  // Overload to create a test port given an Network directly.
  std::unique_ptr<TestPort> CreateTestPort(const Network* network,
                                           absl::string_view username,
                                           absl::string_view password) {
    auto port = std::make_unique<TestPort>(
        Port::PortParametersRef{.env = env_,
                                .network_thread = main_,
                                .socket_factory = &socket_factory_,
                                .network = network,
                                .ice_username_fragment = username,
                                .ice_password = password,
                                .ice_tiebreaker = kTiebreakerDefault},
        0, 0);
    port->SubscribeRoleConflict([this]() { OnRoleConflict(); });
    return port;
  }

  std::unique_ptr<TestPort> CreateRawTestPort() {
    return std::make_unique<TestPort>(
        Port::PortParametersRef{
            .env = env_,
            .network_thread = main_,
            .socket_factory = &socket_factory_,
            .network = MakeNetwork(kLocalAddr1),
        },
        0, 0);
  }

  void OnRoleConflict() { role_conflict_ = true; }
  bool role_conflict() const { return role_conflict_; }

  void ConnectToSignalDestroyed(PortInterface* port) {
    port->SubscribePortDestroyed(
        this, [this](PortInterface* port) { OnDestroyed(port); });
  }

  void OnDestroyed(PortInterface* port) { ++ports_destroyed_; }
  int ports_destroyed() const { return ports_destroyed_; }

  BasicPacketSocketFactory* nat_socket_factory1() {
    return &nat_socket_factory1_;
  }

 protected:
  std::unique_ptr<VirtualSocketServer> ss_;
  GlobalSimulatedTimeController time_controller_;
  Thread* main_;
  const Environment env_;

  // When a "create port" helper method is called with an IP, we create a
  // Network with that IP and add it to this list. Using a list instead of a
  // vector so that when it grows, pointers aren't invalidated.
  std::list<Network> networks_;
  BasicPacketSocketFactory socket_factory_;
  std::unique_ptr<NATServer> nat_server1_;
  std::unique_ptr<NATServer> nat_server2_;
  NATSocketFactory nat_factory1_;
  NATSocketFactory nat_factory2_;
  BasicPacketSocketFactory nat_socket_factory1_;
  BasicPacketSocketFactory nat_socket_factory2_;
  TestStunServer::StunServerPtr stun_server_;
  TestTurnServer turn_server_;
  std::string username_;
  std::string password_;
  bool role_conflict_;
  int ports_destroyed_;
};

void PortTest::TestConnectivity(absl::string_view name1,
                                std::unique_ptr<Port> port1,
                                absl::string_view name2,
                                std::unique_ptr<Port> port2,
                                bool accept,
                                bool same_addr1,
                                bool same_addr2,
                                bool possible) {
  RTC_LOG(LS_INFO) << "Test: " << name1 << " to " << name2 << ": ";
  port1->set_component(ICE_CANDIDATE_COMPONENT_DEFAULT);
  port2->set_component(ICE_CANDIDATE_COMPONENT_DEFAULT);

  // Set up channels and ensure both ports will be deleted.
  TestChannel ch1(std::move(port1), time_controller_);
  TestChannel ch2(std::move(port2), time_controller_);
  EXPECT_EQ(0, ch1.complete_count());
  EXPECT_EQ(0, ch2.complete_count());

  // Acquire addresses.
  ch1.Start();
  ch2.Start();
  ASSERT_THAT(
      WaitUntil([&] { return ch1.complete_count(); }, Eq(1),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  ASSERT_THAT(
      WaitUntil([&] { return ch2.complete_count(); }, Eq(1),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());

  // Send a ping from src to dst. This may or may not make it.
  ch1.CreateConnection(GetCandidate(ch2.port()));
  ASSERT_TRUE(ch1.conn() != nullptr);

  EXPECT_TRUE(WaitUntil([&] { return ch1.conn()->connected(); },
                        {.timeout = kDefaultTimeout,
                         .clock = &time_controller_}));  // for TCP connect
  ch1.Ping();
  if (accept) {
    EXPECT_TRUE(
        WaitUntil([&] { return !ch2.remote_address().IsNil(); },
                  {.timeout = kShortTimeout, .clock = &time_controller_}));

    // We are able to send a ping from src to dst. This is the case when
    // sending to UDP ports and cone NATs.
    EXPECT_TRUE(ch1.remote_address().IsNil());
    EXPECT_EQ(ch2.remote_fragment(), ch1.port()->username_fragment());

    // Ensure the ping came from the same address used for src.
    // This is the case unless the source NAT was symmetric.
    if (same_addr1)
      EXPECT_EQ(ch2.remote_address(), GetAddress(ch1.port()));
    EXPECT_TRUE(same_addr2);

    // Send a ping from dst to src.
    ch2.AcceptConnection(GetCandidate(ch1.port()));
    ASSERT_TRUE(ch2.conn() != nullptr);

    ch2.Ping();
    EXPECT_THAT(
        WaitUntil([&] { return ch2.conn()->write_state(); },
                  Eq(Connection::STATE_WRITABLE),
                  {.timeout = kDefaultTimeout, .clock = &time_controller_}),
        IsRtcOk());
  } else {
    // We can't send a ping from src to dst, so flip it around. This will happen
    // when the destination NAT is addr/port restricted or symmetric.
    EXPECT_TRUE(ch1.remote_address().IsNil());
    EXPECT_TRUE(ch2.remote_address().IsNil());

    // Send a ping from dst to src. Again, this may or may not make it.
    ch2.CreateConnection(GetCandidate(ch1.port()));
    ASSERT_TRUE(ch2.conn() != nullptr);

    ch2.Ping();
    RTC_UNUSED(
        WaitUntil([&] { return ch2.conn()->write_state(); },
                  Eq(Connection::STATE_WRITABLE),
                  {.timeout = kShortTimeout, .clock = &time_controller_}));

    if (same_addr1 && same_addr2) {
      // The new ping got back to the source.
      EXPECT_TRUE(ch1.conn()->receiving());
      EXPECT_EQ(Connection::STATE_WRITABLE, ch2.conn()->write_state());

      // First connection may not be writable if the first ping did not get
      // through.  So we will have to do another.
      if (ch1.conn()->write_state() == Connection::STATE_WRITE_INIT) {
        ch1.Ping();
        EXPECT_THAT(
            WaitUntil([&] { return ch1.conn()->write_state(); },
                      Eq(Connection::STATE_WRITABLE),
                      {.timeout = kDefaultTimeout, .clock = &time_controller_}),
            IsRtcOk());
      }
    } else if (!same_addr1 && possible) {
      // The new ping went to the candidate address, but that address was bad.
      // This will happen when the source NAT is symmetric.
      // With simulated time and some NAT types (like Turn/AR-Nat), the packet
      // can be delivered early.

      // However, since we have now sent a ping to the source IP, we should be
      // able to get a ping from it. This gives us the real source address.
      ch1.Ping();
      EXPECT_TRUE(
          WaitUntil([&] { return !ch2.remote_address().IsNil(); },
                    {.timeout = kDefaultTimeout, .clock = &time_controller_}));
      EXPECT_FALSE(ch2.conn()->receiving());
      EXPECT_TRUE(ch1.remote_address().IsNil());

      // Pick up the actual address and establish the connection.
      ch2.AcceptConnection(GetCandidate(ch1.port()));
      ASSERT_TRUE(ch2.conn() != nullptr);

      ch2.Ping();
      EXPECT_THAT(
          WaitUntil([&] { return ch2.conn()->write_state(); },
                    Eq(Connection::STATE_WRITABLE),
                    {.timeout = kDefaultTimeout, .clock = &time_controller_}),
          IsRtcOk());
    } else if (!same_addr2 && possible) {
      // The new ping came in, but from an unexpected address. This will happen
      // when the destination NAT is symmetric.
      EXPECT_FALSE(ch1.remote_address().IsNil());
      EXPECT_FALSE(ch1.conn()->receiving());

      // Update our address and complete the connection.
      ch1.AcceptConnection(GetCandidate(ch2.port()));

      ch1.Ping();
      EXPECT_THAT(
          WaitUntil([&] { return ch1.conn()->write_state(); },
                    Eq(Connection::STATE_WRITABLE),
                    {.timeout = kDefaultTimeout, .clock = &time_controller_}),
          IsRtcOk());
    } else {  // (!possible)
      // There should be s no way for the pings to reach each other. Check it.
      EXPECT_TRUE(ch1.remote_address().IsNil());
      EXPECT_TRUE(ch2.remote_address().IsNil());
      ch1.Ping();
      RTC_UNUSED(
          WaitUntil([&] { return !ch2.remote_address().IsNil(); }, IsTrue(),
                    {.timeout = kShortTimeout, .clock = &time_controller_}));
      EXPECT_TRUE(ch1.remote_address().IsNil());
      EXPECT_TRUE(ch2.remote_address().IsNil());
    }
  }

  // Everything should be good, unless we know the situation is impossible.
  ASSERT_TRUE(ch1.conn() != nullptr);
  ASSERT_TRUE(ch2.conn() != nullptr);
  if (possible) {
    EXPECT_TRUE(WaitUntil([&] { return ch1.conn()->receiving(); },
                          {.clock = &time_controller_}));
    EXPECT_EQ(Connection::STATE_WRITABLE, ch1.conn()->write_state());
    EXPECT_TRUE(WaitUntil([&] { return ch2.conn()->receiving(); },
                          {.clock = &time_controller_}));
    EXPECT_EQ(Connection::STATE_WRITABLE, ch2.conn()->write_state());
  } else {
    EXPECT_FALSE(ch1.conn()->receiving());
    EXPECT_NE(Connection::STATE_WRITABLE, ch1.conn()->write_state());
    EXPECT_FALSE(ch2.conn()->receiving());
    EXPECT_NE(Connection::STATE_WRITABLE, ch2.conn()->write_state());
  }

  // Tear down and ensure that goes smoothly.
  ch1.Stop();
  ch2.Stop();
  EXPECT_THAT(
      WaitUntil([&] { return ch1.conn(); }, IsNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  EXPECT_THAT(
      WaitUntil([&] { return ch2.conn(); }, IsNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
}

class FakePacketSocketFactory : public PacketSocketFactory {
 public:
  FakePacketSocketFactory() = default;
  ~FakePacketSocketFactory() override = default;

  std::unique_ptr<AsyncPacketSocket> CreateUdpSocket(
      const Environment& env,
      const SocketAddress& address,
      uint16_t min_port,
      uint16_t max_port) override {
    EXPECT_THAT(next_udp_socket_, NotNull());
    return std::move(next_udp_socket_);
  }

  std::unique_ptr<AsyncPacketSocket> CreateClientUdpSocket(
      const Environment& env,
      const SocketAddress& local_address,
      const SocketAddress& remote_address,
      uint16_t min_port,
      uint16_t max_port,
      const PacketSocketTcpOptions& options) override {
    EXPECT_THAT(next_udp_socket_, NotNull());
    return std::move(next_udp_socket_);
  }

  std::unique_ptr<AsyncListenSocket> CreateServerTcpSocket(
      const Environment& env,
      const SocketAddress& local_address,
      uint16_t min_port,
      uint16_t max_port,
      int opts) override {
    EXPECT_THAT(next_server_tcp_socket_, NotNull());
    return std::move(next_server_tcp_socket_);
  }

  std::unique_ptr<AsyncPacketSocket> CreateClientTcpSocket(
      const Environment& env,
      const SocketAddress& local_address,
      const SocketAddress& remote_address,
      const PacketSocketTcpOptions& opts) override {
    EXPECT_TRUE(next_client_tcp_socket_.has_value());
    auto result = std::move(*next_client_tcp_socket_);
    next_client_tcp_socket_.reset();
    return result;
  }

  void set_next_udp_socket(std::unique_ptr<AsyncPacketSocket> next_udp_socket) {
    next_udp_socket_ = std::move(next_udp_socket);
  }
  void set_next_server_tcp_socket(
      std::unique_ptr<AsyncListenSocket> next_server_tcp_socket) {
    next_server_tcp_socket_ = std::move(next_server_tcp_socket);
  }
  void set_next_client_tcp_socket(
      std::unique_ptr<AsyncPacketSocket> next_client_tcp_socket) {
    next_client_tcp_socket_ = std::move(next_client_tcp_socket);
  }
  std::unique_ptr<AsyncDnsResolverInterface> CreateAsyncDnsResolver() override {
    return nullptr;
  }

 private:
  std::unique_ptr<AsyncPacketSocket> next_udp_socket_;
  std::unique_ptr<AsyncListenSocket> next_server_tcp_socket_;
  std::optional<std::unique_ptr<AsyncPacketSocket>> next_client_tcp_socket_;
};

class FakeAsyncPacketSocket : public AsyncPacketSocket {
 public:
  // Returns current local address. Address may be set to NULL if the
  // socket is not bound yet (GetState() returns STATE_BINDING).
  SocketAddress GetLocalAddress() const override { return local_address_; }

  // Returns remote address. Returns zeroes if this is not a client TCP socket.
  SocketAddress GetRemoteAddress() const override { return remote_address_; }

  // Send a packet.
  int Send(const void* pv,
           size_t cb,
           const AsyncSocketPacketOptions& options) override {
    if (error_ == 0) {
      return static_cast<int>(cb);
    } else {
      return -1;
    }
  }
  int SendTo(const void* pv,
             size_t cb,
             const SocketAddress& addr,
             const AsyncSocketPacketOptions& options) override {
    if (error_ == 0) {
      return static_cast<int>(cb);
    } else {
      return -1;
    }
  }
  int Close() override { return 0; }

  State GetState() const override { return state_; }
  int GetOption(Socket::Option opt, int* value) override { return 0; }
  int SetOption(Socket::Option opt, int value) override { return 0; }
  int GetError() const override { return 0; }
  void SetError(int error) override { error_ = error; }

  void set_state(State state) { state_ = state; }

  SocketAddress local_address_;
  SocketAddress remote_address_;

 private:
  int error_ = 0;
  State state_;
};

class FakeAsyncListenSocket : public AsyncListenSocket {
 public:
  // Returns current local address. Address may be set to NULL if the
  // socket is not bound yet (GetState() returns STATE_BINDING).
  SocketAddress GetLocalAddress() const override { return local_address_; }
  void Bind(const SocketAddress& address) {
    local_address_ = address;
    state_ = State::kBound;
  }
  virtual int GetOption(Socket::Option opt, int* value) { return 0; }
  virtual int SetOption(Socket::Option opt, int value) { return 0; }
  State GetState() const override { return state_; }

 private:
  SocketAddress local_address_;
  State state_ = State::kClosed;
};

// Local -> XXXX
TEST_F(PortTest, TestLocalToLocal) {
  TestLocalToLocal();
}

TEST_F(PortTest, TestNetworkSliceInitiallyDefault) {
  std::unique_ptr<UDPPort> port = CreateUdpPort(kLocalAddr1);
  port->PrepareAddress();

  ASSERT_EQ(1U, port->Candidates().size());
  EXPECT_EQ(NetworkSlice::NO_SLICE, port->Candidates()[0].network_slice());
}

TEST_F(PortTest, TestNetworkSliceInitiallySet) {
  std::unique_ptr<UDPPort> port = CreateUdpPort(kLocalAddr1);
  Network* network = const_cast<Network*>(port->Network());
  network->set_network_slice(NetworkSlice::UNIFIED_COMMUNICATIONS);

  port->PrepareAddress();
  ASSERT_EQ(1U, port->Candidates().size());
  EXPECT_EQ(NetworkSlice::UNIFIED_COMMUNICATIONS,
            port->Candidates()[0].network_slice());
}

TEST_F(PortTest, TestNetworkSliceUpdate) {
  std::unique_ptr<UDPPort> port = CreateUdpPort(kLocalAddr1);
  Network* network = const_cast<Network*>(port->Network());
  network->set_network_slice(NetworkSlice::NO_SLICE);

  port->PrepareAddress();
  ASSERT_EQ(1U, port->Candidates().size());
  EXPECT_EQ(NetworkSlice::NO_SLICE, port->Candidates()[0].network_slice());

  network->set_network_slice(NetworkSlice::UNIFIED_COMMUNICATIONS);
  EXPECT_EQ(NetworkSlice::UNIFIED_COMMUNICATIONS,
            port->Candidates()[0].network_slice());

  network->set_network_slice(NetworkSlice::NO_SLICE);
  EXPECT_EQ(NetworkSlice::NO_SLICE, port->Candidates()[0].network_slice());
}

TEST_F(PortTest, TestLocalToConeNat) {
  TestLocalToStun(NAT_OPEN_CONE);
}

TEST_F(PortTest, TestLocalToARNat) {
  TestLocalToStun(NAT_ADDR_RESTRICTED);
}

TEST_F(PortTest, TestLocalToPRNat) {
  TestLocalToStun(NAT_PORT_RESTRICTED);
}

TEST_F(PortTest, TestLocalToSymNat) {
  TestLocalToStun(NAT_SYMMETRIC);
}

// Flaky: https://code.google.com/p/webrtc/issues/detail?id=3316.
TEST_F(PortTest, DISABLED_TestLocalToTurn) {
  TestLocalToRelay(PROTO_UDP);
}

// Cone NAT -> XXXX
TEST_F(PortTest, TestConeNatToLocal) {
  TestStunToLocal(NAT_OPEN_CONE);
}

TEST_F(PortTest, TestConeNatToConeNat) {
  TestStunToStun(NAT_OPEN_CONE, NAT_OPEN_CONE);
}

TEST_F(PortTest, TestConeNatToARNat) {
  TestStunToStun(NAT_OPEN_CONE, NAT_ADDR_RESTRICTED);
}

TEST_F(PortTest, TestConeNatToPRNat) {
  TestStunToStun(NAT_OPEN_CONE, NAT_PORT_RESTRICTED);
}

TEST_F(PortTest, TestConeNatToSymNat) {
  TestStunToStun(NAT_OPEN_CONE, NAT_SYMMETRIC);
}

TEST_F(PortTest, TestConeNatToTurn) {
  TestStunToRelay(NAT_OPEN_CONE, PROTO_UDP);
}

// Address-restricted NAT -> XXXX
TEST_F(PortTest, TestARNatToLocal) {
  TestStunToLocal(NAT_ADDR_RESTRICTED);
}

TEST_F(PortTest, TestARNatToConeNat) {
  TestStunToStun(NAT_ADDR_RESTRICTED, NAT_OPEN_CONE);
}

TEST_F(PortTest, TestARNatToARNat) {
  TestStunToStun(NAT_ADDR_RESTRICTED, NAT_ADDR_RESTRICTED);
}

TEST_F(PortTest, TestARNatToPRNat) {
  TestStunToStun(NAT_ADDR_RESTRICTED, NAT_PORT_RESTRICTED);
}

TEST_F(PortTest, TestARNatToSymNat) {
  TestStunToStun(NAT_ADDR_RESTRICTED, NAT_SYMMETRIC);
}

TEST_F(PortTest, TestARNatToTurn) {
  TestStunToRelay(NAT_ADDR_RESTRICTED, PROTO_UDP);
}

// Port-restricted NAT -> XXXX
TEST_F(PortTest, TestPRNatToLocal) {
  TestStunToLocal(NAT_PORT_RESTRICTED);
}

TEST_F(PortTest, TestPRNatToConeNat) {
  TestStunToStun(NAT_PORT_RESTRICTED, NAT_OPEN_CONE);
}

TEST_F(PortTest, TestPRNatToARNat) {
  TestStunToStun(NAT_PORT_RESTRICTED, NAT_ADDR_RESTRICTED);
}

TEST_F(PortTest, TestPRNatToPRNat) {
  TestStunToStun(NAT_PORT_RESTRICTED, NAT_PORT_RESTRICTED);
}

TEST_F(PortTest, TestPRNatToSymNat) {
  // Will "fail"
  TestStunToStun(NAT_PORT_RESTRICTED, NAT_SYMMETRIC);
}

TEST_F(PortTest, TestPRNatToTurn) {
  TestStunToRelay(NAT_PORT_RESTRICTED, PROTO_UDP);
}

// Symmetric NAT -> XXXX
TEST_F(PortTest, TestSymNatToLocal) {
  TestStunToLocal(NAT_SYMMETRIC);
}

TEST_F(PortTest, TestSymNatToConeNat) {
  TestStunToStun(NAT_SYMMETRIC, NAT_OPEN_CONE);
}

TEST_F(PortTest, TestSymNatToARNat) {
  TestStunToStun(NAT_SYMMETRIC, NAT_ADDR_RESTRICTED);
}

TEST_F(PortTest, TestSymNatToPRNat) {
  // Will "fail"
  TestStunToStun(NAT_SYMMETRIC, NAT_PORT_RESTRICTED);
}

TEST_F(PortTest, TestSymNatToSymNat) {
  // Will "fail"
  TestStunToStun(NAT_SYMMETRIC, NAT_SYMMETRIC);
}

TEST_F(PortTest, TestSymNatToTurn) {
  TestStunToRelay(NAT_SYMMETRIC, PROTO_UDP);
}

// Outbound TCP -> XXXX
TEST_F(PortTest, TestTcpToTcp) {
  TestTcpToTcp();
}

TEST_F(PortTest, TestTcpReconnectOnSendPacket) {
  TestTcpReconnect(false /* ping */, true /* send */);
}

TEST_F(PortTest, TestTcpReconnectOnPing) {
  TestTcpReconnect(true /* ping */, false /* send */);
}

TEST_F(PortTest, TestTcpReconnectTimeout) {
  TestTcpReconnect(false /* ping */, false /* send */);
}

// Test when TcpConnection never connects, the OnClose() will be called to
// destroy the connection.
TEST_F(PortTest, TestTcpNeverConnect) {
  auto port1 = CreateTcpPort(kLocalAddr1);
  port1->SetIceRole(ICEROLE_CONTROLLING);
  port1->set_component(ICE_CANDIDATE_COMPONENT_DEFAULT);

  // Set up a channel and ensure the port will be deleted.
  TestChannel ch1(std::move(port1), time_controller_);
  EXPECT_EQ(0, ch1.complete_count());

  ch1.Start();
  ASSERT_THAT(
      WaitUntil([&] { return ch1.complete_count(); }, Eq(1),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());

  std::unique_ptr<Socket> server(
      vss()->CreateSocket(kLocalAddr2.family(), SOCK_STREAM));
  // Bind but not listen.
  EXPECT_EQ(0, server->Bind(kLocalAddr2));

  Candidate c = GetCandidate(ch1.port());
  c.set_address(server->GetLocalAddress());

  ch1.CreateConnection(c);
  EXPECT_TRUE(ch1.conn());
  EXPECT_TRUE(WaitUntil([&] { return !ch1.conn(); },
                        {.timeout = kDefaultTimeout,
                         .clock = &time_controller_}));  // for TCP connect
}

/* TODO(?): Enable these once testrelayserver can accept external TCP.
TEST_F(PortTest, TestTcpToTcpRelay) {
  TestTcpToRelay(PROTO_TCP);
}

TEST_F(PortTest, TestTcpToSslTcpRelay) {
  TestTcpToRelay(PROTO_SSLTCP);
}
*/

// Outbound SSLTCP -> XXXX
/* TODO(?): Enable these once testrelayserver can accept external SSL.
TEST_F(PortTest, TestSslTcpToTcpRelay) {
  TestSslTcpToRelay(PROTO_TCP);
}

TEST_F(PortTest, TestSslTcpToSslTcpRelay) {
  TestSslTcpToRelay(PROTO_SSLTCP);
}
*/

// Test that a connection will be dead and deleted if
// i) it has never received anything for kMinConnectionLifetime since
//     it was created, or
// ii) it has not received anything for kDeadConnectionReceiveTimeout since
//     last receiving.
TEST_F(PortTest, TestConnectionDead) {
  TestChannel ch1(CreateUdpPort(kLocalAddr1), time_controller_);
  TestChannel ch2(CreateUdpPort(kLocalAddr2), time_controller_);
  // Acquire address.
  ch1.Start();
  ch2.Start();
  ASSERT_THAT(
      WaitUntil([&] { return ch1.complete_count(); }, Eq(1),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  ASSERT_THAT(
      WaitUntil([&] { return ch2.complete_count(); }, Eq(1),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());

  // Test case that the connection has never received anything.
  Timestamp before_created = env().clock().CurrentTime();
  ch1.CreateConnection(GetCandidate(ch2.port()));
  Timestamp after_created = env().clock().CurrentTime();
  Connection* conn = ch1.conn();
  ASSERT_NE(conn, nullptr);
  // It is not dead if it is after kMinConnectionLifetime but not pruned.
  conn->UpdateState(after_created + kMinConnectionLifetime + kEpsilon);
  time_controller_.AdvanceTime(TimeDelta::Zero());
  EXPECT_TRUE(ch1.conn() != nullptr);
  // It is not dead if it is before kMinConnectionLifetime and pruned.
  conn->UpdateState(before_created + kMinConnectionLifetime - kEpsilon);
  conn->Prune();
  time_controller_.AdvanceTime(TimeDelta::Zero());
  EXPECT_TRUE(ch1.conn() != nullptr);
  // It will be dead after kMinConnectionLifetime and pruned.
  conn->UpdateState(after_created + kMinConnectionLifetime + kEpsilon);
  EXPECT_THAT(
      WaitUntil([&] { return ch1.conn(); }, Eq(nullptr),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());

  // Test case that the connection has received something.
  // Create a connection again and receive a ping.
  ch1.CreateConnection(GetCandidate(ch2.port()));
  conn = ch1.conn();
  ASSERT_NE(conn, nullptr);
  Timestamp before_last_receiving = env().clock().CurrentTime();
  conn->ReceivedPing();
  Timestamp after_last_receiving = env().clock().CurrentTime();
  // The connection will be dead after kDeadConnectionReceiveTimeout
  conn->UpdateState(before_last_receiving + kDeadConnectionReceiveTimeout -
                    kEpsilon);
  time_controller_.AdvanceTime(TimeDelta::Zero());
  EXPECT_TRUE(ch1.conn() != nullptr);
  conn->UpdateState(after_last_receiving + kDeadConnectionReceiveTimeout +
                    kEpsilon);
  EXPECT_THAT(
      WaitUntil([&] { return ch1.conn(); }, Eq(nullptr),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
}

TEST_F(PortTest, TestConnectionDeadWithDeadConnectionTimeout) {
  TestChannel ch1(CreateUdpPort(kLocalAddr1), time_controller_);
  TestChannel ch2(CreateUdpPort(kLocalAddr2), time_controller_);
  // Acquire address.
  ch1.Start();
  ch2.Start();
  ASSERT_THAT(
      WaitUntil([&] { return ch1.complete_count(); }, Eq(1),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  ASSERT_THAT(
      WaitUntil([&] { return ch2.complete_count(); }, Eq(1),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());

  // Note: set field trials manually since they are parsed by
  // P2PTransportChannel but P2PTransportChannel is not used in this test.
  IceFieldTrials field_trials;
  field_trials.dead_connection_timeout_ms = 90000;

  // Create a connection again and receive a ping.
  ch1.CreateConnection(GetCandidate(ch2.port()));
  auto conn = ch1.conn();
  conn->SetIceFieldTrials(&field_trials);

  ASSERT_NE(conn, nullptr);
  Timestamp before_last_receiving = env().clock().CurrentTime();
  conn->ReceivedPing();
  Timestamp after_last_receiving = env().clock().CurrentTime();
  // The connection will be dead after 90s
  conn->UpdateState(before_last_receiving + TimeDelta::Seconds(90) - kEpsilon);
  time_controller_.AdvanceTime(TimeDelta::Zero());
  EXPECT_TRUE(ch1.conn() != nullptr);
  conn->UpdateState(after_last_receiving + TimeDelta::Seconds(90) + kEpsilon);
  EXPECT_THAT(
      WaitUntil([&] { return ch1.conn(); }, Eq(nullptr),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
}

TEST_F(PortTest, TestConnectionDeadOutstandingPing) {
  auto port1 = CreateUdpPort(kLocalAddr1);
  port1->SetIceRole(ICEROLE_CONTROLLING);
  auto port2 = CreateUdpPort(kLocalAddr2);
  port2->SetIceRole(ICEROLE_CONTROLLED);
  TestChannel ch1(std::move(port1), time_controller_);
  TestChannel ch2(std::move(port2), time_controller_);
  // Acquire address.
  ch1.Start();
  ch2.Start();
  ASSERT_THAT(
      WaitUntil([&] { return ch1.complete_count(); }, Eq(1),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  ASSERT_THAT(
      WaitUntil([&] { return ch2.complete_count(); }, Eq(1),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());

  // Note: set field trials manually since they are parsed by
  // P2PTransportChannel but P2PTransportChannel is not used in this test.
  IceFieldTrials field_trials;
  field_trials.dead_connection_timeout_ms = 360000;

  // Create a connection again and receive a ping and then send
  // a ping and keep it outstanding.
  ch1.CreateConnection(GetCandidate(ch2.port()));
  auto conn = ch1.conn();
  conn->SetIceFieldTrials(&field_trials);

  ASSERT_NE(conn, nullptr);
  conn->ReceivedPing();
  Timestamp send_ping_timestamp = env().clock().CurrentTime();
  conn->Ping(send_ping_timestamp);

  // The connection will be dead 30s after the ping was sent.
  conn->UpdateState(send_ping_timestamp + kDeadConnectionReceiveTimeout -
                    kEpsilon);
  time_controller_.AdvanceTime(TimeDelta::Zero());
  EXPECT_TRUE(ch1.conn() != nullptr);
  conn->UpdateState(send_ping_timestamp + kDeadConnectionReceiveTimeout +
                    kEpsilon);
  EXPECT_THAT(
      WaitUntil([&] { return ch1.conn(); }, Eq(nullptr),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
}

// This test case verifies standard ICE features in STUN messages. Currently it
// verifies Message Integrity attribute in STUN messages and username in STUN
// binding request will have colon (":") between remote and local username.
TEST_F(PortTest, TestLocalToLocalStandard) {
  auto port1 = CreateUdpPort(kLocalAddr1);
  port1->SetIceRole(ICEROLE_CONTROLLING);
  auto port2 = CreateUdpPort(kLocalAddr2);
  port2->SetIceRole(
      ICEROLE_CONTROLLED);  // Same parameters as TestLocalToLocal above.
  TestConnectivity("udp", std::move(port1), "udp", std::move(port2), true, true,
                   true, true);
}

// This test is trying to validate a successful and failure scenario in a
// loopback test when protocol is RFC5245. For success IceTiebreaker, username
// should remain equal to the request generated by the port and role of port
// must be in controlling.
TEST_F(PortTest, TestLoopbackCall) {
  auto lport = CreateTestPort(kLocalAddr1, "lfrag", "lpass");
  lport->SetIceRole(ICEROLE_CONTROLLING);
  lport->PrepareAddress();
  ASSERT_FALSE(lport->Candidates().empty());
  Connection* conn =
      lport->CreateConnection(lport->Candidates()[0], Port::ORIGIN_MESSAGE);
  conn->Ping();

  ASSERT_THAT(
      WaitUntil([&] { return lport->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  IceMessage* msg = lport->last_stun_msg();
  EXPECT_EQ(STUN_BINDING_REQUEST, msg->type());
  conn->OnReadPacket(
      ReceivedIpPacket(lport->last_stun_buf(), SocketAddress(), std::nullopt));
  ASSERT_THAT(
      WaitUntil([&] { return lport->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  msg = lport->last_stun_msg();
  EXPECT_EQ(STUN_BINDING_RESPONSE, msg->type());

  // If the tiebreaker value is different from port, we expect a error
  // response.
  lport->Reset();
  lport->AddCandidateAddress(kLocalAddr2);
  // Creating a different connection as `conn` is receiving.
  Connection* conn1 =
      lport->CreateConnection(lport->Candidates()[1], Port::ORIGIN_MESSAGE);
  conn1->Ping();

  ASSERT_THAT(
      WaitUntil([&] { return lport->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  msg = lport->last_stun_msg();
  EXPECT_EQ(STUN_BINDING_REQUEST, msg->type());
  std::unique_ptr<IceMessage> modified_req(
      CreateStunMessage(STUN_BINDING_REQUEST));
  const StunByteStringAttribute* username_attr =
      msg->GetByteString(STUN_ATTR_USERNAME);
  modified_req->AddAttribute(std::make_unique<StunByteStringAttribute>(
      STUN_ATTR_USERNAME, username_attr->string_view()));
  // To make sure we receive error response, adding tiebreaker less than
  // what's present in request.
  modified_req->AddAttribute(std::make_unique<StunUInt64Attribute>(
      STUN_ATTR_ICE_CONTROLLING, lport->IceTiebreaker() - 1));
  modified_req->AddMessageIntegrity("lpass");
  modified_req->AddFingerprint();

  lport->Reset();
  auto buf = std::make_unique<ByteBufferWriter>();
  WriteStunMessage(*modified_req, buf.get());
  conn1->OnReadPacket(ReceivedIpPacket(buf->DataView(), SocketAddress()));
  ASSERT_THAT(
      WaitUntil([&] { return lport->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  msg = lport->last_stun_msg();
  EXPECT_EQ(STUN_BINDING_ERROR_RESPONSE, msg->type());
}

// This test verifies role conflict signal is received when there is
// conflict in the role. In this case both ports are in controlling and
// `rport` has higher tiebreaker value than `lport`. Since `lport` has lower
// value of tiebreaker, when it receives ping request from `rport` it will
// send role conflict signal.
TEST_F(PortTest, TestIceRoleConflict) {
  auto lport = CreateTestPort(kLocalAddr1, "lfrag", "lpass");
  lport->SetIceRole(ICEROLE_CONTROLLING);
  auto rport = CreateTestPort(kLocalAddr2, "rfrag", "rpass");
  rport->SetIceRole(ICEROLE_CONTROLLING);
  lport->PrepareAddress();
  rport->PrepareAddress();
  ASSERT_FALSE(lport->Candidates().empty());
  ASSERT_FALSE(rport->Candidates().empty());
  Connection* lconn =
      lport->CreateConnection(rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  Connection* rconn =
      rport->CreateConnection(lport->Candidates()[0], Port::ORIGIN_MESSAGE);
  rconn->Ping();

  ASSERT_THAT(
      WaitUntil([&] { return rport->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  IceMessage* msg = rport->last_stun_msg();
  EXPECT_EQ(STUN_BINDING_REQUEST, msg->type());
  // Send rport binding request to lport.
  lconn->OnReadPacket(
      ReceivedIpPacket(rport->last_stun_buf(), SocketAddress(), std::nullopt));

  ASSERT_THAT(
      WaitUntil([&] { return lport->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  EXPECT_EQ(STUN_BINDING_RESPONSE, lport->last_stun_msg()->type());
  EXPECT_TRUE(role_conflict());
}

// Verifies that a port without an ICE role, i.e. one that has not been taken
// over by an ICE agent, drops incoming STUN binding requests from unknown
// addresses. Handling such a request used to hit a DCHECK in
// MaybeIceRoleConflict.
TEST_F(PortTest, BindingRequestDroppedWithoutIceRole) {
  auto lport = CreateUdpPort(kLocalAddr1);
  lport->SetIceRole(ICEROLE_CONTROLLED);
  auto rport = CreateUdpPort(kLocalAddr2);
  int unknown_address_count = 0;
  rport->SubscribeUnknownAddress(
      this, [&](PortInterface*, const SocketAddress&, ProtocolType, IceMessage*,
                const std::string&, bool) { ++unknown_address_count; });
  lport->PrepareAddress();
  rport->PrepareAddress();
  time_controller_.AdvanceTime(TimeDelta::Zero());
  ASSERT_FALSE(lport->Candidates().empty());
  ASSERT_FALSE(rport->Candidates().empty());

  Connection* lconn =
      lport->CreateConnection(rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  ASSERT_NE(lconn, nullptr);
  lconn->Ping();
  time_controller_.AdvanceTime(TimeDelta::Millis(100));
  EXPECT_EQ(unknown_address_count, 0);

  // Once the port has an ICE role, the binding request is signaled.
  rport->SetIceRole(ICEROLE_CONTROLLING);
  lconn->Ping();
  EXPECT_THAT(
      WaitUntil([&] { return unknown_address_count; }, Eq(1),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
}

TEST_F(PortTest, TestTcpNoDelay) {
  auto port1 = CreateTcpPort(kLocalAddr1);
  port1->SetIceRole(ICEROLE_CONTROLLING);
  int option_value = -1;
  int success = port1->GetOption(Socket::OPT_NODELAY, &option_value);
  ASSERT_EQ(0, success);  // GetOption() should complete successfully w/ 0
  EXPECT_EQ(1, option_value);

  auto port2 = CreateTcpPort(kLocalAddr2);
  port2->SetIceRole(ICEROLE_CONTROLLED);

  // Set up a connection, and verify that option is set on connected sockets at
  // both ends.
  TestChannel ch1(std::move(port1), time_controller_);
  TestChannel ch2(std::move(port2), time_controller_);
  // Acquire addresses.
  ch1.Start();
  ch2.Start();
  ASSERT_THAT(
      WaitUntil([&] { return ch1.complete_count(); }, Eq(1),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  ASSERT_THAT(
      WaitUntil([&] { return ch2.complete_count(); }, Eq(1),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  // Connect and send a ping from src to dst.
  ch1.CreateConnection(GetCandidate(ch2.port()));
  ASSERT_TRUE(ch1.conn() != nullptr);
  EXPECT_TRUE(WaitUntil([&] { return ch1.conn()->connected(); },
                        {.timeout = kDefaultTimeout,
                         .clock = &time_controller_}));  // for TCP connect
  ch1.Ping();
  EXPECT_TRUE(WaitUntil([&] { return !ch2.remote_address().IsNil(); },
                        {.clock = &time_controller_}));

  // Accept the connection.
  ch2.AcceptConnection(GetCandidate(ch1.port()));
  ASSERT_TRUE(ch2.conn() != nullptr);

  option_value = -1;
  success = static_cast<TCPConnection*>(ch1.conn())
                ->socket()
                ->GetOption(Socket::OPT_NODELAY, &option_value);
  ASSERT_EQ(0, success);
  EXPECT_EQ(1, option_value);

  option_value = -1;
  success = static_cast<TCPConnection*>(ch2.conn())
                ->socket()
                ->GetOption(Socket::OPT_NODELAY, &option_value);
  ASSERT_EQ(0, success);
  EXPECT_EQ(1, option_value);
}

TEST_F(PortTest, TestDelayedBindingUdp) {
  FakeAsyncPacketSocket* socket = new FakeAsyncPacketSocket();
  MockPacketSocketFactory socket_factory;

  EXPECT_CALL(socket_factory, CreateUdpSocket)
      .WillOnce(Return(absl::WrapUnique(socket)));
  auto port = CreateUdpPort(kLocalAddr1, &socket_factory);

  socket->set_state(AsyncPacketSocket::STATE_BINDING);
  port->PrepareAddress();

  EXPECT_EQ(0U, port->Candidates().size());
  socket->NotifyAddressReady(socket, kLocalAddr2);

  EXPECT_EQ(1U, port->Candidates().size());
}

TEST_F(PortTest, TestDisableInterfaceOfTcpPort) {
  FakeAsyncListenSocket* lsocket = new FakeAsyncListenSocket();
  FakeAsyncListenSocket* rsocket = new FakeAsyncListenSocket();
  MockPacketSocketFactory socket_factory;
  EXPECT_CALL(socket_factory, CreateServerTcpSocket)
      .WillOnce(Return(absl::WrapUnique(lsocket)))
      .WillOnce(Return(absl::WrapUnique(rsocket)));

  auto lport = CreateTcpPort(kLocalAddr1, &socket_factory);
  auto rport = CreateTcpPort(kLocalAddr2, &socket_factory);

  lsocket->Bind(kLocalAddr1);
  rsocket->Bind(kLocalAddr2);

  lport->SetIceRole(ICEROLE_CONTROLLING);
  rport->SetIceRole(ICEROLE_CONTROLLED);
  lport->PrepareAddress();
  rport->PrepareAddress();
  ASSERT_FALSE(rport->Candidates().empty());

  // A client socket.
  FakeAsyncPacketSocket* socket = new FakeAsyncPacketSocket();
  socket->local_address_ = kLocalAddr1;
  socket->remote_address_ = kLocalAddr2;
  EXPECT_CALL(socket_factory, CreateClientTcpSocket)
      .WillOnce(Return(absl::WrapUnique(socket)));
  Connection* lconn =
      lport->CreateConnection(rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  ASSERT_NE(lconn, nullptr);
  socket->NotifyConnect(socket);
  lconn->Ping();

  // Now disconnect the client socket...
  socket->NotifyClosedForTest(1);

  // And prevent new sockets from being created.
  EXPECT_CALL(socket_factory, CreateClientTcpSocket).WillOnce(Return(nullptr));

  // Test that Ping() does not cause SEGV.
  lconn->Ping();
}

void PortTest::TestCrossFamilyPorts(int type) {
  MockPacketSocketFactory factory;
  std::unique_ptr<Port> ports[4];
  SocketAddress addresses[4] = {
      SocketAddress("192.168.1.3", 0), SocketAddress("192.168.1.4", 0),
      SocketAddress("2001:db8::1", 0), SocketAddress("2001:db8::2", 0)};
  for (int i = 0; i < 4; i++) {
    if (type == SOCK_DGRAM) {
      FakeAsyncPacketSocket* socket = new FakeAsyncPacketSocket();
      EXPECT_CALL(factory, CreateUdpSocket)
          .WillOnce(Return(absl::WrapUnique(socket)));
      ports[i] = CreateUdpPort(addresses[i], &factory);
      socket->set_state(AsyncPacketSocket::STATE_BINDING);
      socket->NotifyAddressReady(socket, addresses[i]);
    } else if (type == SOCK_STREAM) {
      FakeAsyncListenSocket* socket = new FakeAsyncListenSocket();
      EXPECT_CALL(factory, CreateServerTcpSocket)
          .WillOnce(Return(absl::WrapUnique(socket)));
      ports[i] = CreateTcpPort(addresses[i], &factory);
      socket->Bind(addresses[i]);
    }
    ports[i]->PrepareAddress();
  }

  // IPv4 Port, connects to IPv6 candidate and then to IPv4 candidate.
  if (type == SOCK_STREAM) {
    EXPECT_CALL(factory, CreateClientTcpSocket)
        .WillOnce(Return(std::make_unique<FakeAsyncPacketSocket>()));
  }
  Connection* c = ports[0]->CreateConnection(GetCandidate(ports[2].get()),
                                             Port::ORIGIN_MESSAGE);
  EXPECT_TRUE(nullptr == c);
  EXPECT_EQ(0U, ports[0]->connections().size());
  c = ports[0]->CreateConnection(GetCandidate(ports[1].get()),
                                 Port::ORIGIN_MESSAGE);
  EXPECT_FALSE(nullptr == c);
  EXPECT_EQ(1U, ports[0]->connections().size());

  // IPv6 Port, connects to IPv4 candidate and to IPv6 candidate.
  if (type == SOCK_STREAM) {
    EXPECT_CALL(factory, CreateClientTcpSocket)
        .WillOnce(Return(std::make_unique<FakeAsyncPacketSocket>()));
  }
  c = ports[2]->CreateConnection(GetCandidate(ports[0].get()),
                                 Port::ORIGIN_MESSAGE);
  EXPECT_TRUE(nullptr == c);
  EXPECT_EQ(0U, ports[2]->connections().size());
  c = ports[2]->CreateConnection(GetCandidate(ports[3].get()),
                                 Port::ORIGIN_MESSAGE);
  EXPECT_FALSE(nullptr == c);
  EXPECT_EQ(1U, ports[2]->connections().size());
}

TEST_F(PortTest, TestSkipCrossFamilyTcp) {
  TestCrossFamilyPorts(SOCK_STREAM);
}

TEST_F(PortTest, TestSkipCrossFamilyUdp) {
  TestCrossFamilyPorts(SOCK_DGRAM);
}

void PortTest::ExpectPortsCanConnect(bool can_connect, Port* p1, Port* p2) {
  Connection* c = p1->CreateConnection(GetCandidate(p2), Port::ORIGIN_MESSAGE);
  if (can_connect) {
    EXPECT_FALSE(nullptr == c);
    EXPECT_EQ(1U, p1->connections().size());
  } else {
    EXPECT_TRUE(nullptr == c);
    EXPECT_EQ(0U, p1->connections().size());
  }
}

TEST_F(PortTest, TestUdpSingleAddressV6CrossTypePorts) {
  MockPacketSocketFactory factory;
  std::unique_ptr<Port> ports[4];
  SocketAddress addresses[4] = {
      SocketAddress("2001:db8::1", 0), SocketAddress("fe80::1", 0),
      SocketAddress("fe80::2", 0), SocketAddress("::1", 0)};
  for (int i = 0; i < 4; i++) {
    FakeAsyncPacketSocket* socket = new FakeAsyncPacketSocket();
    EXPECT_CALL(factory, CreateUdpSocket)
        .WillOnce(Return(absl::WrapUnique(socket)));
    ports[i] = CreateUdpPort(addresses[i], &factory);
    socket->set_state(AsyncPacketSocket::STATE_BINDING);
    socket->NotifyAddressReady(socket, addresses[i]);
    ports[i]->PrepareAddress();
  }

  Port* standard = ports[0].get();
  Port* link_local1 = ports[1].get();
  Port* link_local2 = ports[2].get();
  Port* localhost = ports[3].get();

  ExpectPortsCanConnect(false, link_local1, standard);
  ExpectPortsCanConnect(false, standard, link_local1);
  ExpectPortsCanConnect(false, link_local1, localhost);
  ExpectPortsCanConnect(false, localhost, link_local1);

  ExpectPortsCanConnect(true, link_local1, link_local2);
  ExpectPortsCanConnect(true, localhost, standard);
  ExpectPortsCanConnect(true, standard, localhost);
}

TEST_F(PortTest, TestUdpMultipleAddressesV6CrossTypePorts) {
  MockPacketSocketFactory factory;
  std::unique_ptr<Port> ports[5];
  SocketAddress addresses[5] = {
      SocketAddress("2001:db8::1", 0), SocketAddress("2001:db8::2", 0),
      SocketAddress("fe80::1", 0), SocketAddress("fe80::2", 0),
      SocketAddress("::1", 0)};
  for (int i = 0; i < 5; i++) {
    FakeAsyncPacketSocket* socket = new FakeAsyncPacketSocket();
    EXPECT_CALL(factory, CreateUdpSocket)
        .WillOnce(Return(absl::WrapUnique(socket)));
    ports[i] =
        CreateUdpPortMultipleAddrs(addresses[i], kLinkLocalIPv6Addr, &factory);
    socket->set_state(AsyncPacketSocket::STATE_BINDING);
    socket->NotifyAddressReady(socket, addresses[i]);
    ports[i]->PrepareAddress();
  }

  Port* standard1 = ports[0].get();
  Port* standard2 = ports[1].get();
  Port* link_local1 = ports[2].get();
  Port* link_local2 = ports[3].get();
  Port* localhost = ports[4].get();

  ExpectPortsCanConnect(false, link_local1, standard1);
  ExpectPortsCanConnect(false, standard1, link_local1);
  ExpectPortsCanConnect(false, link_local1, localhost);
  ExpectPortsCanConnect(false, localhost, link_local1);

  ExpectPortsCanConnect(true, link_local1, link_local2);
  ExpectPortsCanConnect(true, localhost, standard1);
  ExpectPortsCanConnect(true, standard1, localhost);
  ExpectPortsCanConnect(true, standard2, standard1);
}

// This test verifies DSCP value set through SetOption interface can be
// get through DefaultDscpValue.
TEST_F(PortTest, TestDefaultDscpValue) {
  int dscp;
  auto udpport = CreateUdpPort(kLocalAddr1);
  EXPECT_EQ(0, udpport->SetOption(Socket::OPT_DSCP, DSCP_CS6));
  EXPECT_EQ(0, udpport->GetOption(Socket::OPT_DSCP, &dscp));
  auto tcpport = CreateTcpPort(kLocalAddr1);
  EXPECT_EQ(0, tcpport->SetOption(Socket::OPT_DSCP, DSCP_AF31));
  EXPECT_EQ(0, tcpport->GetOption(Socket::OPT_DSCP, &dscp));
  EXPECT_EQ(DSCP_AF31, dscp);
  auto stunport = CreateStunPort(kLocalAddr1, nat_socket_factory1());
  EXPECT_EQ(0, stunport->SetOption(Socket::OPT_DSCP, DSCP_AF41));
  EXPECT_EQ(0, stunport->GetOption(Socket::OPT_DSCP, &dscp));
  EXPECT_EQ(DSCP_AF41, dscp);
  auto turnport1 =
      CreateTurnPort(kLocalAddr1, nat_socket_factory1(), PROTO_UDP, PROTO_UDP);
  // Socket is created in PrepareAddress.
  turnport1->PrepareAddress();
  EXPECT_EQ(0, turnport1->SetOption(Socket::OPT_DSCP, DSCP_CS7));
  EXPECT_EQ(0, turnport1->GetOption(Socket::OPT_DSCP, &dscp));
  EXPECT_EQ(DSCP_CS7, dscp);
  // This will verify correct value returned without the socket.
  auto turnport2 =
      CreateTurnPort(kLocalAddr1, nat_socket_factory1(), PROTO_UDP, PROTO_UDP);
  EXPECT_EQ(0, turnport2->SetOption(Socket::OPT_DSCP, DSCP_CS6));
  EXPECT_EQ(0, turnport2->GetOption(Socket::OPT_DSCP, &dscp));
  EXPECT_EQ(DSCP_CS6, dscp);
}

// Test sending STUN messages.
TEST_F(PortTest, TestSendStunMessage) {
  auto lport = CreateTestPort(kLocalAddr1, "lfrag", "lpass");
  auto rport = CreateTestPort(kLocalAddr2, "rfrag", "rpass");
  lport->SetIceRole(ICEROLE_CONTROLLING);
  rport->SetIceRole(ICEROLE_CONTROLLED);
  // Send a fake ping from lport to rport.
  lport->PrepareAddress();
  rport->PrepareAddress();
  ASSERT_FALSE(rport->Candidates().empty());
  Connection* lconn =
      lport->CreateConnection(rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  Connection* rconn =
      rport->CreateConnection(lport->Candidates()[0], Port::ORIGIN_MESSAGE);
  lconn->Ping();

  // Check that it's a proper BINDING-REQUEST.
  ASSERT_THAT(
      WaitUntil([&] { return lport->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  IceMessage* msg = lport->last_stun_msg();
  EXPECT_EQ(STUN_BINDING_REQUEST, msg->type());
  EXPECT_FALSE(msg->IsLegacy());
  const StunByteStringAttribute* username_attr =
      msg->GetByteString(STUN_ATTR_USERNAME);
  ASSERT_TRUE(username_attr != nullptr);
  const StunUInt32Attribute* priority_attr = msg->GetUInt32(STUN_ATTR_PRIORITY);
  ASSERT_TRUE(priority_attr != nullptr);
  EXPECT_EQ(kDefaultPrflxPriority, priority_attr->value());
  EXPECT_EQ("rfrag:lfrag", username_attr->string_view());
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_MESSAGE_INTEGRITY) != nullptr);
  EXPECT_EQ(StunMessage::IntegrityStatus::kIntegrityOk,
            msg->ValidateMessageIntegrity("rpass"));
  const StunUInt64Attribute* ice_controlling_attr =
      msg->GetUInt64(STUN_ATTR_ICE_CONTROLLING);
  ASSERT_TRUE(ice_controlling_attr != nullptr);
  EXPECT_EQ(lport->IceTiebreaker(), ice_controlling_attr->value());
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_ICE_CONTROLLED) == nullptr);
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_USE_CANDIDATE) != nullptr);
  EXPECT_TRUE(msg->GetUInt32(STUN_ATTR_FINGERPRINT) != nullptr);
  EXPECT_TRUE(StunMessage::ValidateFingerprint(lport->last_stun_buf()));

  // Request should not include ping count.
  ASSERT_TRUE(msg->GetUInt32(STUN_ATTR_RETRANSMIT_COUNT) == nullptr);

  // Save a copy of the BINDING-REQUEST for use below.
  std::unique_ptr<IceMessage> request = CopyStunMessage(*msg);

  // Receive the BINDING-REQUEST and respond with BINDING-RESPONSE.
  rconn->OnReadPacket(
      ReceivedIpPacket(lport->last_stun_buf(), SocketAddress(), std::nullopt));
  msg = rport->last_stun_msg();
  ASSERT_TRUE(msg != nullptr);
  EXPECT_EQ(STUN_BINDING_RESPONSE, msg->type());
  // Received a BINDING-RESPONSE.
  lconn->OnReadPacket(
      ReceivedIpPacket(rport->last_stun_buf(), SocketAddress(), std::nullopt));

  // Verify the STUN Stats.
  EXPECT_EQ(1U, lconn->stats().sent_ping_requests_total);
  EXPECT_EQ(1U, lconn->stats().sent_ping_requests_before_first_response);
  EXPECT_EQ(1U, lconn->stats().recv_ping_responses);
  EXPECT_EQ(1U, rconn->stats().recv_ping_requests);
  EXPECT_EQ(1U, rconn->stats().sent_ping_responses);

  EXPECT_FALSE(msg->IsLegacy());
  const StunAddressAttribute* addr_attr =
      msg->GetAddress(STUN_ATTR_XOR_MAPPED_ADDRESS);
  ASSERT_TRUE(addr_attr != nullptr);
  EXPECT_EQ(lport->Candidates()[0].address(), addr_attr->GetAddress());
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_MESSAGE_INTEGRITY) != nullptr);
  EXPECT_EQ(StunMessage::IntegrityStatus::kIntegrityOk,
            msg->ValidateMessageIntegrity("rpass"));
  EXPECT_TRUE(msg->GetUInt32(STUN_ATTR_FINGERPRINT) != nullptr);
  EXPECT_TRUE(StunMessage::ValidateFingerprint(lport->last_stun_buf()));
  // No USERNAME or PRIORITY in ICE responses.
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_USERNAME) == nullptr);
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_PRIORITY) == nullptr);
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_MAPPED_ADDRESS) == nullptr);
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_ICE_CONTROLLING) == nullptr);
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_ICE_CONTROLLED) == nullptr);
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_USE_CANDIDATE) == nullptr);

  // Response should not include ping count.
  ASSERT_TRUE(msg->GetUInt32(STUN_ATTR_RETRANSMIT_COUNT) == nullptr);

  // Respond with a BINDING-ERROR-RESPONSE. This wouldn't happen in real life,
  // but we can do it here.
  rport->SendBindingErrorResponse(
      request.get(), lport->Candidates()[0].address(), STUN_ERROR_SERVER_ERROR,
      STUN_ERROR_REASON_SERVER_ERROR);
  msg = rport->last_stun_msg();
  ASSERT_TRUE(msg != nullptr);
  EXPECT_EQ(STUN_BINDING_ERROR_RESPONSE, msg->type());
  EXPECT_FALSE(msg->IsLegacy());
  const StunErrorCodeAttribute* error_attr = msg->GetErrorCode();
  ASSERT_TRUE(error_attr != nullptr);
  EXPECT_EQ(STUN_ERROR_SERVER_ERROR, error_attr->code());
  EXPECT_EQ(std::string(STUN_ERROR_REASON_SERVER_ERROR), error_attr->reason());
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_MESSAGE_INTEGRITY) != nullptr);
  EXPECT_EQ(StunMessage::IntegrityStatus::kIntegrityOk,
            msg->ValidateMessageIntegrity("rpass"));
  EXPECT_TRUE(msg->GetUInt32(STUN_ATTR_FINGERPRINT) != nullptr);
  EXPECT_TRUE(StunMessage::ValidateFingerprint(lport->last_stun_buf()));
  // No USERNAME with ICE.
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_USERNAME) == nullptr);
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_PRIORITY) == nullptr);

  // Testing STUN binding requests from rport --> lport, having ICE_CONTROLLED
  // and (incremented) RETRANSMIT_COUNT attributes.
  rport->Reset();
  rport->set_send_retransmit_count_attribute(true);
  rconn->Ping();
  rconn->Ping();
  rconn->Ping();
  ASSERT_THAT(
      WaitUntil([&] { return rport->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  msg = rport->last_stun_msg();
  EXPECT_EQ(STUN_BINDING_REQUEST, msg->type());
  const StunUInt64Attribute* ice_controlled_attr =
      msg->GetUInt64(STUN_ATTR_ICE_CONTROLLED);
  ASSERT_TRUE(ice_controlled_attr != nullptr);
  EXPECT_EQ(rport->IceTiebreaker(), ice_controlled_attr->value());
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_USE_CANDIDATE) == nullptr);

  // Request should include ping count.
  const StunUInt32Attribute* retransmit_attr =
      msg->GetUInt32(STUN_ATTR_RETRANSMIT_COUNT);
  ASSERT_TRUE(retransmit_attr != nullptr);
  EXPECT_EQ(2U, retransmit_attr->value());

  // Respond with a BINDING-RESPONSE.
  request = CopyStunMessage(*msg);
  lconn->OnReadPacket(
      ReceivedIpPacket(rport->last_stun_buf(), SocketAddress(), std::nullopt));
  msg = lport->last_stun_msg();
  // Receive the BINDING-RESPONSE.
  rconn->OnReadPacket(
      ReceivedIpPacket(lport->last_stun_buf(), SocketAddress(), std::nullopt));

  // Verify the Stun ping stats.
  EXPECT_EQ(3U, rconn->stats().sent_ping_requests_total);
  EXPECT_EQ(3U, rconn->stats().sent_ping_requests_before_first_response);
  EXPECT_EQ(1U, rconn->stats().recv_ping_responses);
  EXPECT_EQ(1U, lconn->stats().sent_ping_responses);
  EXPECT_EQ(1U, lconn->stats().recv_ping_requests);
  // Ping after receiver the first response
  rconn->Ping();
  rconn->Ping();
  EXPECT_EQ(5U, rconn->stats().sent_ping_requests_total);
  EXPECT_EQ(3U, rconn->stats().sent_ping_requests_before_first_response);

  // Response should include same ping count.
  retransmit_attr = msg->GetUInt32(STUN_ATTR_RETRANSMIT_COUNT);
  ASSERT_TRUE(retransmit_attr != nullptr);
  EXPECT_EQ(2U, retransmit_attr->value());
}

TEST_F(PortTest, TestNomination) {
  auto lport = CreateTestPort(kLocalAddr1, "lfrag", "lpass");
  auto rport = CreateTestPort(kLocalAddr2, "rfrag", "rpass");
  lport->SetIceRole(ICEROLE_CONTROLLING);
  rport->SetIceRole(ICEROLE_CONTROLLED);
  lport->PrepareAddress();
  rport->PrepareAddress();
  ASSERT_FALSE(lport->Candidates().empty());
  ASSERT_FALSE(rport->Candidates().empty());
  Connection* lconn =
      lport->CreateConnection(rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  Connection* rconn =
      rport->CreateConnection(lport->Candidates()[0], Port::ORIGIN_MESSAGE);

  // `lconn` is controlling, `rconn` is controlled.
  uint32_t nomination = 1234;
  lconn->set_nomination(nomination);

  EXPECT_FALSE(lconn->nominated());
  EXPECT_FALSE(rconn->nominated());
  EXPECT_EQ(lconn->nominated(), lconn->stats().nominated);
  EXPECT_EQ(rconn->nominated(), rconn->stats().nominated);

  // Send ping (including the nomination value) from `lconn` to `rconn`. This
  // should set the remote nomination of `rconn`.
  lconn->Ping();
  ASSERT_TRUE(
      WaitUntil([&] { return lport->last_stun_msg(); },
                {.timeout = kDefaultTimeout, .clock = &time_controller_}));
  ASSERT_GT(lport->last_stun_buf().size(), 0u);
  rconn->OnReadPacket(
      ReceivedIpPacket(lport->last_stun_buf(), SocketAddress(), std::nullopt));

  EXPECT_EQ(nomination, rconn->remote_nomination());
  EXPECT_FALSE(lconn->nominated());
  EXPECT_TRUE(rconn->nominated());
  EXPECT_EQ(lconn->nominated(), lconn->stats().nominated);
  EXPECT_EQ(rconn->nominated(), rconn->stats().nominated);

  // This should result in an acknowledgment sent back from `rconn` to `lconn`,
  // updating the acknowledged nomination of `lconn`.
  ASSERT_TRUE(
      WaitUntil([&] { return rport->last_stun_msg(); },
                {.timeout = kDefaultTimeout, .clock = &time_controller_}));
  ASSERT_GT(rport->last_stun_buf().size(), 0u);
  lconn->OnReadPacket(
      ReceivedIpPacket(rport->last_stun_buf(), SocketAddress(), std::nullopt));

  EXPECT_EQ(nomination, lconn->acked_nomination());
  EXPECT_TRUE(lconn->nominated());
  EXPECT_TRUE(rconn->nominated());
  EXPECT_EQ(lconn->nominated(), lconn->stats().nominated);
  EXPECT_EQ(rconn->nominated(), rconn->stats().nominated);
}

TEST_F(PortTest, TestRoundTripTime) {
  auto lport = CreateTestPort(kLocalAddr1, "lfrag", "lpass");
  auto rport = CreateTestPort(kLocalAddr2, "rfrag", "rpass");
  lport->SetIceRole(ICEROLE_CONTROLLING);
  rport->SetIceRole(ICEROLE_CONTROLLED);
  lport->PrepareAddress();
  rport->PrepareAddress();
  ASSERT_FALSE(lport->Candidates().empty());
  ASSERT_FALSE(rport->Candidates().empty());
  Connection* lconn =
      lport->CreateConnection(rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  Connection* rconn =
      rport->CreateConnection(lport->Candidates()[0], Port::ORIGIN_MESSAGE);

  EXPECT_EQ(0u, lconn->stats().total_round_trip_time_ms);
  EXPECT_FALSE(lconn->stats().current_round_trip_time_ms);

  SendPingAndReceiveResponse(lconn, lport.get(), rconn, rport.get(),
                             time_controller_, 10);
  EXPECT_EQ(10u, lconn->stats().total_round_trip_time_ms);
  ASSERT_TRUE(lconn->stats().current_round_trip_time_ms);
  EXPECT_EQ(10u, *lconn->stats().current_round_trip_time_ms);

  SendPingAndReceiveResponse(lconn, lport.get(), rconn, rport.get(),
                             time_controller_, 20);
  EXPECT_EQ(30u, lconn->stats().total_round_trip_time_ms);
  ASSERT_TRUE(lconn->stats().current_round_trip_time_ms);
  EXPECT_EQ(20u, *lconn->stats().current_round_trip_time_ms);

  SendPingAndReceiveResponse(lconn, lport.get(), rconn, rport.get(),
                             time_controller_, 30);
  EXPECT_EQ(60u, lconn->stats().total_round_trip_time_ms);
  ASSERT_TRUE(lconn->stats().current_round_trip_time_ms);
  EXPECT_EQ(30u, *lconn->stats().current_round_trip_time_ms);
}

TEST_F(PortTest, TestUseCandidateAttribute) {
  auto lport = CreateTestPort(kLocalAddr1, "lfrag", "lpass");
  auto rport = CreateTestPort(kLocalAddr2, "rfrag", "rpass");
  lport->SetIceRole(ICEROLE_CONTROLLING);
  rport->SetIceRole(ICEROLE_CONTROLLED);
  // Send a fake ping from lport to rport.
  lport->PrepareAddress();
  rport->PrepareAddress();
  ASSERT_FALSE(rport->Candidates().empty());
  Connection* lconn =
      lport->CreateConnection(rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  lconn->Ping();
  ASSERT_THAT(
      WaitUntil([&] { return lport->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  IceMessage* msg = lport->last_stun_msg();
  const StunUInt64Attribute* ice_controlling_attr =
      msg->GetUInt64(STUN_ATTR_ICE_CONTROLLING);
  ASSERT_TRUE(ice_controlling_attr != nullptr);
  const StunByteStringAttribute* use_candidate_attr =
      msg->GetByteString(STUN_ATTR_USE_CANDIDATE);
  ASSERT_TRUE(use_candidate_attr != nullptr);
}

// Tests that when the network type changes, the network cost of the port will
// change, the network cost of the local candidates will change. Also tests that
// the remote network costs are updated with the stun binding requests.
TEST_F(PortTest, TestNetworkCostChange) {
  Network* test_network = MakeNetwork(kLocalAddr1);
  auto lport = CreateTestPort(test_network, "lfrag", "lpass");
  auto rport = CreateTestPort(test_network, "rfrag", "rpass");
  lport->SetIceRole(ICEROLE_CONTROLLING);
  rport->SetIceRole(ICEROLE_CONTROLLED);
  lport->PrepareAddress();
  rport->PrepareAddress();

  // Default local port cost is kNetworkCostUnknown.
  EXPECT_EQ(kNetworkCostUnknown, lport->network_cost());
  ASSERT_TRUE(!lport->Candidates().empty());
  for (const Candidate& candidate : lport->Candidates()) {
    EXPECT_EQ(kNetworkCostUnknown, candidate.network_cost());
  }

  // Change the network type to wifi.
  test_network->set_type(ADAPTER_TYPE_WIFI);
  EXPECT_EQ(kNetworkCostLow, lport->network_cost());
  for (const Candidate& candidate : lport->Candidates()) {
    EXPECT_EQ(kNetworkCostLow, candidate.network_cost());
  }

  // Add a connection and then change the network type.
  Connection* lconn =
      lport->CreateConnection(rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  // Change the network type to cellular.
  test_network->set_type(ADAPTER_TYPE_CELLULAR);
  EXPECT_EQ(kNetworkCostHigh, lport->network_cost());
  for (const Candidate& candidate : lport->Candidates()) {
    EXPECT_EQ(kNetworkCostHigh, candidate.network_cost());
  }

  test_network->set_type(ADAPTER_TYPE_WIFI);
  Connection* rconn =
      rport->CreateConnection(lport->Candidates()[0], Port::ORIGIN_MESSAGE);
  test_network->set_type(ADAPTER_TYPE_CELLULAR);
  lconn->Ping();
  // The rconn's remote candidate cost is kNetworkCostLow, but the ping
  // contains an attribute of network cost of kNetworkCostHigh. Once the
  // message is handled in rconn, The rconn's remote candidate will have cost
  // kNetworkCostHigh;
  EXPECT_EQ(kNetworkCostLow, rconn->remote_candidate().network_cost());
  ASSERT_THAT(
      WaitUntil([&] { return lport->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  IceMessage* msg = lport->last_stun_msg();
  EXPECT_EQ(STUN_BINDING_REQUEST, msg->type());
  // Pass the binding request to rport.
  rconn->OnReadPacket(
      ReceivedIpPacket(lport->last_stun_buf(), SocketAddress(), std::nullopt));

  // Wait until rport sends the response and then check the remote network cost.
  ASSERT_THAT(
      WaitUntil([&] { return rport->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  EXPECT_EQ(kNetworkCostHigh, rconn->remote_candidate().network_cost());
}

TEST_F(PortTest, TestNetworkInfoAttribute) {
  Network* test_network = MakeNetwork(kLocalAddr1);
  auto lport = CreateTestPort(test_network, "lfrag", "lpass");
  auto rport = CreateTestPort(test_network, "rfrag", "rpass");
  lport->SetIceRole(ICEROLE_CONTROLLING);
  rport->SetIceRole(ICEROLE_CONTROLLED);
  uint16_t lnetwork_id = 9;
  test_network->set_id(lnetwork_id);
  // Send a fake ping from lport to rport.
  lport->PrepareAddress();
  rport->PrepareAddress();
  Connection* lconn =
      lport->CreateConnection(rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  lconn->Ping();
  ASSERT_THAT(
      WaitUntil([&] { return lport->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  IceMessage* msg = lport->last_stun_msg();
  const StunUInt32Attribute* network_info_attr =
      msg->GetUInt32(STUN_ATTR_GOOG_NETWORK_INFO);
  ASSERT_TRUE(network_info_attr != nullptr);
  uint32_t network_info = network_info_attr->value();
  EXPECT_EQ(lnetwork_id, network_info >> 16);
  // Default network has unknown type and cost kNetworkCostUnknown.
  EXPECT_EQ(kNetworkCostUnknown, network_info & 0xFFFF);

  // Set the network type to be cellular so its cost will be kNetworkCostHigh.
  // Send a fake ping from rport to lport.
  test_network->set_type(ADAPTER_TYPE_CELLULAR);
  uint16_t rnetwork_id = 8;
  test_network->set_id(rnetwork_id);
  Connection* rconn =
      rport->CreateConnection(lport->Candidates()[0], Port::ORIGIN_MESSAGE);
  rconn->Ping();
  ASSERT_THAT(
      WaitUntil([&] { return rport->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  msg = rport->last_stun_msg();
  network_info_attr = msg->GetUInt32(STUN_ATTR_GOOG_NETWORK_INFO);
  ASSERT_TRUE(network_info_attr != nullptr);
  network_info = network_info_attr->value();
  EXPECT_EQ(rnetwork_id, network_info >> 16);
  EXPECT_EQ(kNetworkCostHigh, network_info & 0xFFFF);
}

// Test handling STUN messages.
TEST_F(PortTest, TestHandleStunMessage) {
  // Our port will act as the "remote" port.
  auto port = CreateTestPort(kLocalAddr2, "rfrag", "rpass");

  std::unique_ptr<IceMessage> in_msg, out_msg;
  auto buf = std::make_unique<ByteBufferWriter>();
  SocketAddress addr(kLocalAddr1);
  std::string username;

  // BINDING-REQUEST from local to remote with valid ICE username,
  // MESSAGE-INTEGRITY, and FINGERPRINT.
  in_msg = CreateStunMessageWithUsername(STUN_BINDING_REQUEST, "rfrag:lfrag");
  in_msg->AddMessageIntegrity("rpass");
  in_msg->AddFingerprint();
  WriteStunMessage(*in_msg, buf.get());
  EXPECT_TRUE(GetStunMessageFromBufferWriter(port.get(), buf.get(), addr,
                                             &out_msg, &username));
  EXPECT_TRUE(out_msg.get() != nullptr);
  EXPECT_EQ("lfrag", username);

  // BINDING-RESPONSE without username, with MESSAGE-INTEGRITY and FINGERPRINT.
  in_msg = CreateStunMessage(STUN_BINDING_RESPONSE);
  in_msg->AddAttribute(std::make_unique<StunXorAddressAttribute>(
      STUN_ATTR_XOR_MAPPED_ADDRESS, kLocalAddr2));
  in_msg->AddMessageIntegrity("rpass");
  in_msg->AddFingerprint();
  WriteStunMessage(*in_msg, buf.get());
  EXPECT_TRUE(GetStunMessageFromBufferWriter(port.get(), buf.get(), addr,
                                             &out_msg, &username));
  EXPECT_TRUE(out_msg.get() != nullptr);
  EXPECT_EQ("", username);

  // BINDING-ERROR-RESPONSE without username, with error, M-I, and FINGERPRINT.
  in_msg = CreateStunMessage(STUN_BINDING_ERROR_RESPONSE);
  in_msg->AddAttribute(std::make_unique<StunErrorCodeAttribute>(
      STUN_ATTR_ERROR_CODE, STUN_ERROR_SERVER_ERROR,
      STUN_ERROR_REASON_SERVER_ERROR));
  in_msg->AddFingerprint();
  WriteStunMessage(*in_msg, buf.get());
  EXPECT_TRUE(GetStunMessageFromBufferWriter(port.get(), buf.get(), addr,
                                             &out_msg, &username));
  EXPECT_TRUE(out_msg.get() != nullptr);
  EXPECT_EQ("", username);
  ASSERT_TRUE(out_msg->GetErrorCode() != nullptr);
  EXPECT_EQ(STUN_ERROR_SERVER_ERROR, out_msg->GetErrorCode()->code());
  EXPECT_EQ(std::string(STUN_ERROR_REASON_SERVER_ERROR),
            out_msg->GetErrorCode()->reason());
}

// Tests handling of ICE binding requests with missing or incorrect usernames.
TEST_F(PortTest, TestHandleStunMessageBadUsername) {
  auto port = CreateTestPort(kLocalAddr2, "rfrag", "rpass");

  std::unique_ptr<IceMessage> in_msg, out_msg;
  auto buf = std::make_unique<ByteBufferWriter>();
  SocketAddress addr(kLocalAddr1);
  std::string username;

  // BINDING-REQUEST with no username.
  in_msg = CreateStunMessage(STUN_BINDING_REQUEST);
  in_msg->AddMessageIntegrity("rpass");
  in_msg->AddFingerprint();
  WriteStunMessage(*in_msg, buf.get());
  EXPECT_TRUE(GetStunMessageFromBufferWriter(port.get(), buf.get(), addr,
                                             &out_msg, &username));
  EXPECT_TRUE(out_msg.get() == nullptr);
  EXPECT_EQ("", username);
  EXPECT_EQ(STUN_ERROR_BAD_REQUEST, port->last_stun_error_code());

  // BINDING-REQUEST with empty username.
  in_msg = CreateStunMessageWithUsername(STUN_BINDING_REQUEST, "");
  in_msg->AddMessageIntegrity("rpass");
  in_msg->AddFingerprint();
  WriteStunMessage(*in_msg, buf.get());
  EXPECT_TRUE(GetStunMessageFromBufferWriter(port.get(), buf.get(), addr,
                                             &out_msg, &username));
  EXPECT_TRUE(out_msg.get() == nullptr);
  EXPECT_EQ("", username);
  EXPECT_EQ(STUN_ERROR_UNAUTHORIZED, port->last_stun_error_code());

  // BINDING-REQUEST with too-short username.
  in_msg = CreateStunMessageWithUsername(STUN_BINDING_REQUEST, "rfra");
  in_msg->AddMessageIntegrity("rpass");
  in_msg->AddFingerprint();
  WriteStunMessage(*in_msg, buf.get());
  EXPECT_TRUE(GetStunMessageFromBufferWriter(port.get(), buf.get(), addr,
                                             &out_msg, &username));
  EXPECT_TRUE(out_msg.get() == nullptr);
  EXPECT_EQ("", username);
  EXPECT_EQ(STUN_ERROR_UNAUTHORIZED, port->last_stun_error_code());

  // BINDING-REQUEST with reversed username.
  in_msg = CreateStunMessageWithUsername(STUN_BINDING_REQUEST, "lfrag:rfrag");
  in_msg->AddMessageIntegrity("rpass");
  in_msg->AddFingerprint();
  WriteStunMessage(*in_msg, buf.get());
  EXPECT_TRUE(GetStunMessageFromBufferWriter(port.get(), buf.get(), addr,
                                             &out_msg, &username));
  EXPECT_TRUE(out_msg.get() == nullptr);
  EXPECT_EQ("", username);
  EXPECT_EQ(STUN_ERROR_UNAUTHORIZED, port->last_stun_error_code());

  // BINDING-REQUEST with garbage username.
  in_msg = CreateStunMessageWithUsername(STUN_BINDING_REQUEST, "abcd:efgh");
  in_msg->AddMessageIntegrity("rpass");
  in_msg->AddFingerprint();
  WriteStunMessage(*in_msg, buf.get());
  EXPECT_TRUE(GetStunMessageFromBufferWriter(port.get(), buf.get(), addr,
                                             &out_msg, &username));
  EXPECT_TRUE(out_msg.get() == nullptr);
  EXPECT_EQ("", username);
  EXPECT_EQ(STUN_ERROR_UNAUTHORIZED, port->last_stun_error_code());
}

// Test handling STUN messages with missing or malformed M-I.
TEST_F(PortTest, TestHandleStunMessageBadMessageIntegrity) {
  // Our port will act as the "remote" port.
  auto port = CreateTestPort(kLocalAddr2, "rfrag", "rpass");

  std::unique_ptr<IceMessage> in_msg, out_msg;
  auto buf = std::make_unique<ByteBufferWriter>();
  SocketAddress addr(kLocalAddr1);
  std::string username;

  // BINDING-REQUEST from local to remote with valid ICE username and
  // FINGERPRINT, but no MESSAGE-INTEGRITY.
  in_msg = CreateStunMessageWithUsername(STUN_BINDING_REQUEST, "rfrag:lfrag");
  in_msg->AddFingerprint();
  WriteStunMessage(*in_msg, buf.get());
  EXPECT_TRUE(GetStunMessageFromBufferWriter(port.get(), buf.get(), addr,
                                             &out_msg, &username));
  EXPECT_TRUE(out_msg.get() == nullptr);
  EXPECT_EQ("", username);
  EXPECT_EQ(STUN_ERROR_BAD_REQUEST, port->last_stun_error_code());

  // BINDING-REQUEST from local to remote with valid ICE username and
  // FINGERPRINT, but invalid MESSAGE-INTEGRITY.
  in_msg = CreateStunMessageWithUsername(STUN_BINDING_REQUEST, "rfrag:lfrag");
  in_msg->AddMessageIntegrity("invalid");
  in_msg->AddFingerprint();
  WriteStunMessage(*in_msg, buf.get());
  EXPECT_TRUE(GetStunMessageFromBufferWriter(port.get(), buf.get(), addr,
                                             &out_msg, &username));
  EXPECT_TRUE(out_msg.get() == nullptr);
  EXPECT_EQ("", username);
  EXPECT_EQ(STUN_ERROR_UNAUTHORIZED, port->last_stun_error_code());

  // TODO(?): BINDING-RESPONSES and BINDING-ERROR-RESPONSES are checked
  // by the Connection, not the Port, since they require the remote username.
  // Change this test to pass in data via Connection::OnReadPacket instead.
}

// Test handling STUN messages with missing or malformed FINGERPRINT.
TEST_F(PortTest, TestHandleStunMessageBadFingerprint) {
  // Our port will act as the "remote" port.
  auto port = CreateTestPort(kLocalAddr2, "rfrag", "rpass");

  std::unique_ptr<IceMessage> in_msg, out_msg;
  auto buf = std::make_unique<ByteBufferWriter>();
  SocketAddress addr(kLocalAddr1);
  std::string username;

  // BINDING-REQUEST from local to remote with valid ICE username and
  // MESSAGE-INTEGRITY, but no FINGERPRINT; GetStunMessage should fail.
  in_msg = CreateStunMessageWithUsername(STUN_BINDING_REQUEST, "rfrag:lfrag");
  in_msg->AddMessageIntegrity("rpass");
  WriteStunMessage(*in_msg, buf.get());
  EXPECT_FALSE(GetStunMessageFromBufferWriter(port.get(), buf.get(), addr,
                                              &out_msg, &username));
  EXPECT_EQ(0, port->last_stun_error_code());

  // Now, add a fingerprint, but munge the message so it's not valid.
  in_msg->AddFingerprint();
  in_msg->SetTransactionIdForTesting("TESTTESTBADD");
  WriteStunMessage(*in_msg, buf.get());
  EXPECT_FALSE(GetStunMessageFromBufferWriter(port.get(), buf.get(), addr,
                                              &out_msg, &username));
  EXPECT_EQ(0, port->last_stun_error_code());

  // Valid BINDING-RESPONSE, except no FINGERPRINT.
  in_msg = CreateStunMessage(STUN_BINDING_RESPONSE);
  in_msg->AddAttribute(std::make_unique<StunXorAddressAttribute>(
      STUN_ATTR_XOR_MAPPED_ADDRESS, kLocalAddr2));
  in_msg->AddMessageIntegrity("rpass");
  WriteStunMessage(*in_msg, buf.get());
  EXPECT_FALSE(GetStunMessageFromBufferWriter(port.get(), buf.get(), addr,
                                              &out_msg, &username));
  EXPECT_EQ(0, port->last_stun_error_code());

  // Now, add a fingerprint, but munge the message so it's not valid.
  in_msg->AddFingerprint();
  in_msg->SetTransactionIdForTesting("TESTTESTBADD");
  WriteStunMessage(*in_msg, buf.get());
  EXPECT_FALSE(GetStunMessageFromBufferWriter(port.get(), buf.get(), addr,
                                              &out_msg, &username));
  EXPECT_EQ(0, port->last_stun_error_code());

  // Valid BINDING-ERROR-RESPONSE, except no FINGERPRINT.
  in_msg = CreateStunMessage(STUN_BINDING_ERROR_RESPONSE);
  in_msg->AddAttribute(std::make_unique<StunErrorCodeAttribute>(
      STUN_ATTR_ERROR_CODE, STUN_ERROR_SERVER_ERROR,
      STUN_ERROR_REASON_SERVER_ERROR));
  in_msg->AddMessageIntegrity("rpass");
  WriteStunMessage(*in_msg, buf.get());
  EXPECT_FALSE(GetStunMessageFromBufferWriter(port.get(), buf.get(), addr,
                                              &out_msg, &username));
  EXPECT_EQ(0, port->last_stun_error_code());

  // Now, add a fingerprint, but munge the message so it's not valid.
  in_msg->AddFingerprint();
  in_msg->SetTransactionIdForTesting("TESTTESTBADD");
  WriteStunMessage(*in_msg, buf.get());
  EXPECT_FALSE(GetStunMessageFromBufferWriter(port.get(), buf.get(), addr,
                                              &out_msg, &username));
  EXPECT_EQ(0, port->last_stun_error_code());
}

// Test handling a STUN message with unknown attributes in the
// "comprehension-required" range. Should respond with an error with the
// unknown attributes' IDs.
TEST_F(PortTest,
       TestHandleStunRequestWithUnknownComprehensionRequiredAttribute) {
  // Our port will act as the "remote" port.
  std::unique_ptr<TestPort> port(CreateTestPort(kLocalAddr2, "rfrag", "rpass"));

  std::unique_ptr<IceMessage> in_msg, out_msg;
  auto buf = std::make_unique<ByteBufferWriter>();
  SocketAddress addr(kLocalAddr1);
  std::string username;

  // Build ordinary message with valid ufrag/pass.
  in_msg = CreateStunMessageWithUsername(STUN_BINDING_REQUEST, "rfrag:lfrag");
  // Add a couple attributes with ID in comprehension-required range.
  in_msg->AddAttribute(StunAttribute::CreateUInt32(0x7777));
  in_msg->AddAttribute(StunAttribute::CreateUInt32(0x4567));
  // ... And one outside the range.
  in_msg->AddAttribute(StunAttribute::CreateUInt32(0xdead));
  in_msg->AddMessageIntegrity("rpass");
  in_msg->AddFingerprint();
  WriteStunMessage(*in_msg, buf.get());
  ASSERT_TRUE(GetStunMessageFromBufferWriter(port.get(), buf.get(), addr,
                                             &out_msg, &username));
  IceMessage* error_response = port->last_stun_msg();
  ASSERT_NE(nullptr, error_response);

  // Verify that the "unknown attribute" error response has the right error
  // code, and includes an attribute that lists out the unrecognized attribute
  // types.
  EXPECT_EQ(STUN_ERROR_UNKNOWN_ATTRIBUTE, error_response->GetErrorCodeValue());
  const StunUInt16ListAttribute* unknown_attributes =
      error_response->GetUnknownAttributes();
  ASSERT_NE(nullptr, unknown_attributes);
  ASSERT_EQ(2u, unknown_attributes->Size());
  EXPECT_EQ(0x7777, unknown_attributes->GetType(0));
  EXPECT_EQ(0x4567, unknown_attributes->GetType(1));
}

// Similar to the above, but with a response instead of a request. In this
// case the response should just be ignored and transaction treated is failed.
TEST_F(PortTest,
       TestHandleStunResponseWithUnknownComprehensionRequiredAttribute) {
  // Generic setup.
  auto lport =
      CreateTestPort(kLocalAddr1, "lfrag", "lpass", ICEROLE_CONTROLLING);
  auto rport =
      CreateTestPort(kLocalAddr2, "rfrag", "rpass", ICEROLE_CONTROLLED);
  lport->PrepareAddress();
  rport->PrepareAddress();
  ASSERT_FALSE(lport->Candidates().empty());
  ASSERT_FALSE(rport->Candidates().empty());
  Connection* lconn =
      lport->CreateConnection(rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  Connection* rconn =
      rport->CreateConnection(lport->Candidates()[0], Port::ORIGIN_MESSAGE);

  // Send request.
  lconn->Ping();
  ASSERT_THAT(
      WaitUntil([&] { return lport->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  rconn->OnReadPacket(
      ReceivedIpPacket(lport->last_stun_buf(), SocketAddress(), std::nullopt));

  // Intercept request and add comprehension required attribute.
  ASSERT_THAT(
      WaitUntil([&] { return rport->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  auto modified_response = rport->last_stun_msg()->Clone();
  modified_response->AddAttribute(StunAttribute::CreateUInt32(0x7777));
  modified_response->RemoveAttribute(STUN_ATTR_MESSAGE_INTEGRITY);
  modified_response->RemoveAttribute(STUN_ATTR_FINGERPRINT);
  modified_response->AddMessageIntegrity("rpass");
  modified_response->AddFingerprint();
  ByteBufferWriter buf;
  WriteStunMessage(*modified_response, &buf);
  lconn->OnReadPacket(ReceivedIpPacket(buf.DataView(), SocketAddress()));
  // Response should have been ignored, leaving us unwritable still.
  EXPECT_FALSE(lconn->writable());
}

// Similar to the above, but with an indication. As with a response, it should
// just be ignored.
TEST_F(PortTest,
       TestHandleStunIndicationWithUnknownComprehensionRequiredAttribute) {
  // Generic set up.
  auto lport =
      CreateTestPort(kLocalAddr2, "lfrag", "lpass", ICEROLE_CONTROLLING);
  auto rport =
      CreateTestPort(kLocalAddr2, "rfrag", "rpass", ICEROLE_CONTROLLED);
  lport->PrepareAddress();
  rport->PrepareAddress();
  ASSERT_FALSE(lport->Candidates().empty());
  ASSERT_FALSE(rport->Candidates().empty());
  Connection* lconn =
      lport->CreateConnection(rport->Candidates()[0], Port::ORIGIN_MESSAGE);

  // Generate indication with comprehension required attribute and verify it
  // doesn't update last_ping_received.
  auto in_msg = CreateStunMessage(STUN_BINDING_INDICATION);
  in_msg->AddAttribute(StunAttribute::CreateUInt32(0x7777));
  in_msg->AddFingerprint();
  ByteBufferWriter buf;
  WriteStunMessage(*in_msg, &buf);
  lconn->OnReadPacket(ReceivedIpPacket::CreateFromLegacy(
      buf.Data(), buf.Length(), /*packet_time_us=*/-1));
  EXPECT_EQ(lconn->LastPingReceived(), Timestamp::Zero());
}

// Test handling of STUN binding indication messages . STUN binding
// indications are allowed only to the connection which is in read mode.
TEST_F(PortTest, TestHandleStunBindingIndication) {
  auto lport =
      CreateTestPort(kLocalAddr2, "lfrag", "lpass", ICEROLE_CONTROLLING);

  // Verifying encoding and decoding STUN indication message.
  std::unique_ptr<IceMessage> in_msg, out_msg;
  std::unique_ptr<ByteBufferWriter> buf(new ByteBufferWriter());
  SocketAddress addr(kLocalAddr1);
  std::string username;

  in_msg = CreateStunMessage(STUN_BINDING_INDICATION);
  in_msg->AddFingerprint();
  WriteStunMessage(*in_msg, buf.get());
  EXPECT_TRUE(GetStunMessageFromBufferWriter(lport.get(), buf.get(), addr,
                                             &out_msg, &username));
  EXPECT_TRUE(out_msg.get() != nullptr);
  EXPECT_EQ(out_msg->type(), STUN_BINDING_INDICATION);
  EXPECT_EQ("", username);

  // Verify connection can handle STUN indication and updates
  // last_ping_received.
  auto rport = CreateTestPort(kLocalAddr2, "rfrag", "rpass");
  rport->SetIceRole(ICEROLE_CONTROLLED);
  lport->PrepareAddress();
  rport->PrepareAddress();
  ASSERT_FALSE(lport->Candidates().empty());
  ASSERT_FALSE(rport->Candidates().empty());

  Connection* lconn =
      lport->CreateConnection(rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  Connection* rconn =
      rport->CreateConnection(lport->Candidates()[0], Port::ORIGIN_MESSAGE);
  rconn->Ping();

  ASSERT_THAT(
      WaitUntil([&] { return rport->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  IceMessage* msg = rport->last_stun_msg();
  EXPECT_EQ(STUN_BINDING_REQUEST, msg->type());
  // Send rport binding request to lport.
  lconn->OnReadPacket(
      ReceivedIpPacket(rport->last_stun_buf(), SocketAddress(), std::nullopt));

  ASSERT_THAT(
      WaitUntil([&] { return lport->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  EXPECT_EQ(STUN_BINDING_RESPONSE, lport->last_stun_msg()->type());
  Timestamp last_ping_received1 = lconn->LastPingReceived();

  // Adding a delay of 100ms.
  time_controller_.AdvanceTime(TimeDelta::Millis(100));
  // Pinging lconn using stun indication message.
  lconn->OnReadPacket(ReceivedIpPacket::CreateFromLegacy(
      buf->Data(), buf->Length(), /*packet_time_us=*/-1));
  Timestamp last_ping_received2 = lconn->LastPingReceived();
  EXPECT_GT(last_ping_received2, last_ping_received1);
}

TEST_F(PortTest, TestComputeCandidatePriority) {
  auto port = CreateTestPort(kLocalAddr1, "name", "pass");
  port->set_type_preference(90);
  port->set_component(177);
  port->AddCandidateAddress(SocketAddress("192.168.1.4", 1234));
  port->AddCandidateAddress(SocketAddress("2001:db8::1234", 1234));
  port->AddCandidateAddress(SocketAddress("fc12:3456::1234", 1234));
  port->AddCandidateAddress(SocketAddress("::ffff:192.168.1.4", 1234));
  port->AddCandidateAddress(SocketAddress("::192.168.1.4", 1234));
  port->AddCandidateAddress(SocketAddress("2002::1234:5678", 1234));
  port->AddCandidateAddress(SocketAddress("2001::1234:5678", 1234));
  port->AddCandidateAddress(SocketAddress("fecf::1234:5678", 1234));
  port->AddCandidateAddress(SocketAddress("3ffe::1234:5678", 1234));
  // These should all be:
  // (90 << 24) | ([rfc3484 pref value] << 8) | (256 - 177)
  uint32_t expected_priority_v4 = 1509957199U;
  uint32_t expected_priority_v6 = 1509959759U;
  uint32_t expected_priority_ula = 1509962319U;
  uint32_t expected_priority_v4mapped = expected_priority_v4;
  uint32_t expected_priority_v4compat = 1509949775U;
  uint32_t expected_priority_6to4 = 1509954639U;
  uint32_t expected_priority_teredo = 1509952079U;
  uint32_t expected_priority_sitelocal = 1509949775U;
  uint32_t expected_priority_6bone = 1509949775U;
  ASSERT_EQ(expected_priority_v4, port->Candidates()[0].priority());
  ASSERT_EQ(expected_priority_v6, port->Candidates()[1].priority());
  ASSERT_EQ(expected_priority_ula, port->Candidates()[2].priority());
  ASSERT_EQ(expected_priority_v4mapped, port->Candidates()[3].priority());
  ASSERT_EQ(expected_priority_v4compat, port->Candidates()[4].priority());
  ASSERT_EQ(expected_priority_6to4, port->Candidates()[5].priority());
  ASSERT_EQ(expected_priority_teredo, port->Candidates()[6].priority());
  ASSERT_EQ(expected_priority_sitelocal, port->Candidates()[7].priority());
  ASSERT_EQ(expected_priority_6bone, port->Candidates()[8].priority());
}

TEST_F(PortTest, TestComputeCandidatePriorityWithPriorityAdjustment) {
  FieldTrials field_trials = CreateTestFieldTrials(
      "WebRTC-IncreaseIceCandidatePriorityHostSrflx/Enabled/");
  auto port = CreateTestPort(kLocalAddr1, "name", "pass", &field_trials);
  port->set_type_preference(90);
  port->set_component(177);
  port->AddCandidateAddress(SocketAddress("192.168.1.4", 1234));
  port->AddCandidateAddress(SocketAddress("2001:db8::1234", 1234));
  port->AddCandidateAddress(SocketAddress("fc12:3456::1234", 1234));
  port->AddCandidateAddress(SocketAddress("::ffff:192.168.1.4", 1234));
  port->AddCandidateAddress(SocketAddress("::192.168.1.4", 1234));
  port->AddCandidateAddress(SocketAddress("2002::1234:5678", 1234));
  port->AddCandidateAddress(SocketAddress("2001::1234:5678", 1234));
  port->AddCandidateAddress(SocketAddress("fecf::1234:5678", 1234));
  port->AddCandidateAddress(SocketAddress("3ffe::1234:5678", 1234));
  // These should all be:
  // (90 << 24) | (([rfc3484 pref value] << 8) + kMaxTurnServers) | (256 - 177)
  uint32_t expected_priority_v4 = 1509957199U + (kMaxTurnServers << 8);
  uint32_t expected_priority_v6 = 1509959759U + (kMaxTurnServers << 8);
  uint32_t expected_priority_ula = 1509962319U + (kMaxTurnServers << 8);
  uint32_t expected_priority_v4mapped = expected_priority_v4;
  uint32_t expected_priority_v4compat = 1509949775U + (kMaxTurnServers << 8);
  uint32_t expected_priority_6to4 = 1509954639U + (kMaxTurnServers << 8);
  uint32_t expected_priority_teredo = 1509952079U + (kMaxTurnServers << 8);
  uint32_t expected_priority_sitelocal = 1509949775U + (kMaxTurnServers << 8);
  uint32_t expected_priority_6bone = 1509949775U + (kMaxTurnServers << 8);
  ASSERT_EQ(expected_priority_v4, port->Candidates()[0].priority());
  ASSERT_EQ(expected_priority_v6, port->Candidates()[1].priority());
  ASSERT_EQ(expected_priority_ula, port->Candidates()[2].priority());
  ASSERT_EQ(expected_priority_v4mapped, port->Candidates()[3].priority());
  ASSERT_EQ(expected_priority_v4compat, port->Candidates()[4].priority());
  ASSERT_EQ(expected_priority_6to4, port->Candidates()[5].priority());
  ASSERT_EQ(expected_priority_teredo, port->Candidates()[6].priority());
  ASSERT_EQ(expected_priority_sitelocal, port->Candidates()[7].priority());
  ASSERT_EQ(expected_priority_6bone, port->Candidates()[8].priority());
}

// In the case of shared socket, one port may be shared by local and stun.
// Test that candidates with different types will have different foundation.
TEST_F(PortTest, TestFoundation) {
  auto testport = CreateTestPort(kLocalAddr1, "name", "pass");
  testport->AddCandidateAddress(kLocalAddr1, kLocalAddr1,
                                IceCandidateType::kHost,
                                ICE_TYPE_PREFERENCE_HOST, false);
  testport->AddCandidateAddress(kLocalAddr2, kLocalAddr1,
                                IceCandidateType::kSrflx,
                                ICE_TYPE_PREFERENCE_SRFLX, true);
  EXPECT_NE(testport->Candidates()[0].foundation(),
            testport->Candidates()[1].foundation());
}

// This test verifies the foundation of different types of ICE candidates.
TEST_F(PortTest, TestCandidateFoundation) {
  std::unique_ptr<NATServer> nat_server(
      CreateNatServer(kNatAddr1, NAT_OPEN_CONE));
  auto udpport1 = CreateUdpPort(kLocalAddr1);
  udpport1->PrepareAddress();
  auto udpport2 = CreateUdpPort(kLocalAddr1);
  udpport2->PrepareAddress();
  EXPECT_EQ(udpport1->Candidates()[0].foundation(),
            udpport2->Candidates()[0].foundation());
  auto tcpport1 = CreateTcpPort(kLocalAddr1);
  tcpport1->PrepareAddress();
  auto tcpport2 = CreateTcpPort(kLocalAddr1);
  tcpport2->PrepareAddress();
  EXPECT_EQ(tcpport1->Candidates()[0].foundation(),
            tcpport2->Candidates()[0].foundation());
  auto stunport = CreateStunPort(kLocalAddr1, nat_socket_factory1());
  stunport->PrepareAddress();
  ASSERT_THAT(
      WaitUntil([&] { return stunport->Candidates().size(); }, Eq(1U),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  EXPECT_NE(tcpport1->Candidates()[0].foundation(),
            stunport->Candidates()[0].foundation());
  EXPECT_NE(tcpport2->Candidates()[0].foundation(),
            stunport->Candidates()[0].foundation());
  EXPECT_NE(udpport1->Candidates()[0].foundation(),
            stunport->Candidates()[0].foundation());
  EXPECT_NE(udpport2->Candidates()[0].foundation(),
            stunport->Candidates()[0].foundation());
  // Verifying TURN candidate foundation.
  auto turnport1 =
      CreateTurnPort(kLocalAddr1, nat_socket_factory1(), PROTO_UDP, PROTO_UDP);
  turnport1->PrepareAddress();
  ASSERT_THAT(
      WaitUntil([&] { return turnport1->Candidates().size(); }, Eq(1U),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  EXPECT_NE(udpport1->Candidates()[0].foundation(),
            turnport1->Candidates()[0].foundation());
  EXPECT_NE(udpport2->Candidates()[0].foundation(),
            turnport1->Candidates()[0].foundation());
  EXPECT_NE(stunport->Candidates()[0].foundation(),
            turnport1->Candidates()[0].foundation());
  auto turnport2 =
      CreateTurnPort(kLocalAddr1, nat_socket_factory1(), PROTO_UDP, PROTO_UDP);
  turnport2->PrepareAddress();
  ASSERT_THAT(
      WaitUntil([&] { return turnport2->Candidates().size(); }, Eq(1U),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  EXPECT_EQ(turnport1->Candidates()[0].foundation(),
            turnport2->Candidates()[0].foundation());

  // Running a second turn server, to get different base IP address.
  SocketAddress kTurnUdpIntAddr2("99.99.98.4", STUN_SERVER_PORT);
  SocketAddress kTurnUdpExtAddr2("99.99.98.5", 0);
  TestTurnServer turn_server2(env(), Thread::Current(), vss(), kTurnUdpIntAddr2,
                              kTurnUdpExtAddr2);
  auto turnport3 = CreateTurnPort(kLocalAddr1, nat_socket_factory1(), PROTO_UDP,
                                  PROTO_UDP, kTurnUdpIntAddr2);
  turnport3->PrepareAddress();
  ASSERT_THAT(
      WaitUntil([&] { return turnport3->Candidates().size(); }, Eq(1U),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  EXPECT_NE(turnport3->Candidates()[0].foundation(),
            turnport2->Candidates()[0].foundation());

  // Start a TCP turn server, and check that two turn candidates have
  // different foundations if their relay protocols are different.
  TestTurnServer turn_server3(env(), Thread::Current(), vss(), kTurnTcpIntAddr,
                              kTurnUdpExtAddr, PROTO_TCP);
  auto turnport4 =
      CreateTurnPort(kLocalAddr1, nat_socket_factory1(), PROTO_TCP, PROTO_UDP);
  turnport4->PrepareAddress();
  ASSERT_THAT(
      WaitUntil([&] { return turnport4->Candidates().size(); }, Eq(1U),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  EXPECT_NE(turnport2->Candidates()[0].foundation(),
            turnport4->Candidates()[0].foundation());
}

// This test verifies the related addresses of different types of
// ICE candidates.
TEST_F(PortTest, TestCandidateRelatedAddress) {
  auto nat_server = CreateNatServer(kNatAddr1, NAT_OPEN_CONE);
  auto udpport = CreateUdpPort(kLocalAddr1);
  udpport->PrepareAddress();
  // For UDPPort, related address will be empty.
  EXPECT_TRUE(udpport->Candidates()[0].related_address().IsNil());
  // Testing related address for stun candidates.
  // For stun candidate related address must be equal to the base
  // socket address.
  auto stunport = CreateStunPort(kLocalAddr1, nat_socket_factory1());
  stunport->PrepareAddress();
  ASSERT_THAT(
      WaitUntil([&] { return stunport->Candidates().size(); }, Eq(1U),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  // Check STUN candidate address.
  EXPECT_EQ(stunport->Candidates()[0].address().ipaddr(), kNatAddr1.ipaddr());
  // Check STUN candidate related address.
  EXPECT_EQ(stunport->Candidates()[0].related_address(),
            stunport->GetLocalAddress());
  // Verifying the related address for TURN candidate.
  // For TURN related address must be equal to the mapped address.
  auto turnport =
      CreateTurnPort(kLocalAddr1, nat_socket_factory1(), PROTO_UDP, PROTO_UDP);
  turnport->PrepareAddress();
  ASSERT_THAT(
      WaitUntil([&] { return turnport->Candidates().size(); }, Eq(1U),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  EXPECT_EQ(kTurnUdpExtAddr.ipaddr(),
            turnport->Candidates()[0].address().ipaddr());
  EXPECT_EQ(kNatAddr1.ipaddr(),
            turnport->Candidates()[0].related_address().ipaddr());
}

// Test priority value overflow handling when preference is set to 3.
TEST_F(PortTest, TestCandidatePriority) {
  Candidate cand1;
  cand1.set_priority(3);
  Candidate cand2;
  cand2.set_priority(1);
  EXPECT_TRUE(cand1.priority() > cand2.priority());
}

// Test the Connection priority is calculated correctly.
TEST_F(PortTest, TestConnectionPriority) {
  auto lport = CreateTestPort(kLocalAddr1, "lfrag", "lpass");
  lport->set_type_preference(ICE_TYPE_PREFERENCE_HOST);

  auto rport = CreateTestPort(kLocalAddr2, "rfrag", "rpass");
  rport->set_type_preference(ICE_TYPE_PREFERENCE_RELAY_UDP);
  lport->set_component(123);
  lport->AddCandidateAddress(SocketAddress("192.168.1.4", 1234));
  rport->set_component(23);
  rport->AddCandidateAddress(SocketAddress("10.1.1.100", 1234));
  rport->set_type_preference(ICE_TYPE_PREFERENCE_RELAY_DTLS);
  rport->AddCandidateAddress(SocketAddress("10.1.1.100", 1234));

  EXPECT_EQ(0x7E001E85U, lport->Candidates()[0].priority());
  EXPECT_EQ(0x3001EE9U, rport->Candidates()[0].priority());
  EXPECT_EQ(0x2001EE9U, rport->Candidates()[1].priority());

  // RFC 5245
  // pair priority = 2^32*MIN(G,D) + 2*MAX(G,D) + (G>D?1:0)
  lport->SetIceRole(ICEROLE_CONTROLLING);
  rport->SetIceRole(ICEROLE_CONTROLLED);
  Connection* lconn =
      lport->CreateConnection(rport->Candidates()[0], Port::ORIGIN_MESSAGE);
#if defined(WEBRTC_WIN)
  EXPECT_EQ(0x3001EE9FC003D0BU, lconn->priority());
#else
  EXPECT_EQ(0x3001EE9FC003D0BLLU, lconn->priority());
#endif

  lconn = lport->CreateConnection(rport->Candidates()[1], Port::ORIGIN_MESSAGE);
  EXPECT_EQ(uint64_t{0x2001EE9FC003D0B}, lconn->priority());

  lport->SetIceRole(ICEROLE_CONTROLLED);
  rport->SetIceRole(ICEROLE_CONTROLLING);
  Connection* rconn =
      rport->CreateConnection(lport->Candidates()[0], Port::ORIGIN_MESSAGE);
#if defined(WEBRTC_WIN)
  EXPECT_EQ(0x3001EE9FC003D0AU, rconn->priority());
#else
  EXPECT_EQ(0x3001EE9FC003D0ALLU, rconn->priority());
#endif
}

// Test the Connection priority is calculated correctly.
TEST_F(PortTest, TestConnectionPriorityWithPriorityAdjustment) {
  FieldTrials field_trials = CreateTestFieldTrials(
      "WebRTC-IncreaseIceCandidatePriorityHostSrflx/Enabled/");
  auto lport = CreateTestPort(kLocalAddr1, "lfrag", "lpass", &field_trials);
  lport->set_type_preference(ICE_TYPE_PREFERENCE_HOST);

  auto rport = CreateTestPort(kLocalAddr2, "rfrag", "rpass", &field_trials);
  rport->set_type_preference(ICE_TYPE_PREFERENCE_RELAY_UDP);
  lport->set_component(123);
  lport->AddCandidateAddress(SocketAddress("192.168.1.4", 1234));
  rport->set_component(23);
  rport->AddCandidateAddress(SocketAddress("10.1.1.100", 1234));

  EXPECT_EQ(0x7E001E85U + (kMaxTurnServers << 8),
            lport->Candidates()[0].priority());
  EXPECT_EQ(0x3001EE9U + (kMaxTurnServers << 8),
            rport->Candidates()[0].priority());

  // RFC 5245
  // pair priority = 2^32*MIN(G,D) + 2*MAX(G,D) + (G>D?1:0)
  lport->SetIceRole(ICEROLE_CONTROLLING);
  rport->SetIceRole(ICEROLE_CONTROLLED);
  Connection* lconn =
      lport->CreateConnection(rport->Candidates()[0], Port::ORIGIN_MESSAGE);
#if defined(WEBRTC_WIN)
  EXPECT_EQ(0x3003EE9FC007D0BU, lconn->priority());
#else
  EXPECT_EQ(0x3003EE9FC007D0BLLU, lconn->priority());
#endif

  lport->SetIceRole(ICEROLE_CONTROLLED);
  rport->SetIceRole(ICEROLE_CONTROLLING);
  Connection* rconn =
      rport->CreateConnection(lport->Candidates()[0], Port::ORIGIN_MESSAGE);
  RTC_LOG(LS_ERROR) << "RCONN " << rconn->priority();
#if defined(WEBRTC_WIN)
  EXPECT_EQ(0x3003EE9FC007D0AU, rconn->priority());
#else
  EXPECT_EQ(0x3003EE9FC007D0ALLU, rconn->priority());
#endif
}

// Note that UpdateState takes into account the estimated RTT, and the
// correctness of using `kMaxExpectedSimulatedRtt` as an upper bound of RTT in
// the following tests depends on the link rate and the delay distriubtion
// configured in VirtualSocketServer::AddPacketToNetwork. The tests below use
// the default setup where the RTT is deterministically one, which generates an
// estimate given by `MINIMUM_RTT` = 100.
TEST_F(PortTest, TestWritableState) {
  auto port1 = CreateUdpPort(kLocalAddr1);
  port1->SetIceRole(ICEROLE_CONTROLLING);
  auto port2 = CreateUdpPort(kLocalAddr2);
  port2->SetIceRole(ICEROLE_CONTROLLED);

  // Set up channels.
  TestChannel ch1(std::move(port1), time_controller_);
  TestChannel ch2(std::move(port2), time_controller_);

  // Acquire addresses.
  ch1.Start();
  ch2.Start();
  ASSERT_THAT(
      WaitUntil([&] { return ch1.complete_count(); }, Eq(1),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  ASSERT_THAT(
      WaitUntil([&] { return ch2.complete_count(); }, Eq(1),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());

  // Send a ping from src to dst.
  ch1.CreateConnection(GetCandidate(ch2.port()));
  ASSERT_TRUE(ch1.conn() != nullptr);
  EXPECT_EQ(Connection::STATE_WRITE_INIT, ch1.conn()->write_state());
  // for TCP connect
  EXPECT_TRUE(
      WaitUntil([&] { return ch1.conn()->connected(); },
                {.timeout = kDefaultTimeout, .clock = &time_controller_}));
  ch1.Ping();
  EXPECT_TRUE(WaitUntil([&] { return !ch2.remote_address().IsNil(); },
                        {.clock = &time_controller_}));

  // Data should be sendable before the connection is accepted.
  auto data = std::to_array<uint8_t>({'a', 'b', 'c', 'd', '\0'});
  AsyncSocketPacketOptions options;
  EXPECT_EQ(static_cast<int>(data.size()), ch1.conn()->Send(data, options));

  // Accept the connection to return the binding response, transition to
  // writable, and allow data to be sent.
  ch2.AcceptConnection(GetCandidate(ch1.port()));
  EXPECT_THAT(
      WaitUntil([&] { return ch1.conn()->write_state(); },
                Eq(Connection::STATE_WRITABLE),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  EXPECT_EQ(static_cast<int>(data.size()), ch1.conn()->Send(data, options));

  // Ask the connection to update state as if enough time has passed to lose
  // full writability and 5 pings went unresponded to. We'll accomplish the
  // latter by sending pings but not pumping messages.
  for (uint32_t i = 1; i <= kConnectionWriteConnectFailures; ++i) {
    ch1.Ping(Timestamp::Millis(i));
  }
  Timestamp unreliable_timeout_delay = Timestamp::Zero() +
                                       kConnectionWriteConnectTimeout +
                                       kMaxExpectedSimulatedRtt;
  ch1.conn()->UpdateState(unreliable_timeout_delay);
  EXPECT_EQ(Connection::STATE_WRITE_UNRELIABLE, ch1.conn()->write_state());

  // Data should be able to be sent in this state.
  EXPECT_EQ(static_cast<int>(data.size()), ch1.conn()->Send(data, options));

  // And now allow the other side to process the pings and send binding
  // responses.
  EXPECT_THAT(
      WaitUntil([&] { return ch1.conn()->write_state(); },
                Eq(Connection::STATE_WRITABLE),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  // Wait long enough for a full timeout (past however long we've already
  // waited).
  for (uint32_t i = 1; i <= kConnectionWriteConnectFailures; ++i) {
    ch1.Ping(unreliable_timeout_delay + TimeDelta::Millis(i));
  }
  ch1.conn()->UpdateState(unreliable_timeout_delay + kConnectionWriteTimeout +
                          kMaxExpectedSimulatedRtt);
  EXPECT_EQ(Connection::STATE_WRITE_TIMEOUT, ch1.conn()->write_state());

  // Even if the connection has timed out, the Connection shouldn't block
  // the sending of data.
  EXPECT_EQ(static_cast<int>(data.size()), ch1.conn()->Send(data, options));

  ch1.Stop();
  ch2.Stop();
}

// Test writability states using the configured threshold value to replace
// the default value given by `kConnectionWriteConnectTimeout` and
// `kConnectionWriteConnectFailures`.
TEST_F(PortTest, TestWritableStateWithConfiguredThreshold) {
  auto port1 = CreateUdpPort(kLocalAddr1);
  port1->SetIceRole(ICEROLE_CONTROLLING);
  auto port2 = CreateUdpPort(kLocalAddr2);
  port2->SetIceRole(ICEROLE_CONTROLLED);

  // Set up channels.
  TestChannel ch1(std::move(port1), time_controller_);
  TestChannel ch2(std::move(port2), time_controller_);

  // Acquire addresses.
  ch1.Start();
  ch2.Start();
  ASSERT_THAT(
      WaitUntil([&] { return ch1.complete_count(); }, Eq(1),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  ASSERT_THAT(
      WaitUntil([&] { return ch2.complete_count(); }, Eq(1),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());

  // Send a ping from src to dst.
  ch1.CreateConnection(GetCandidate(ch2.port()));
  ASSERT_TRUE(ch1.conn() != nullptr);
  ch1.Ping();
  EXPECT_TRUE(WaitUntil([&] { return !ch2.remote_address().IsNil(); },
                        {.clock = &time_controller_}));

  // Accept the connection to return the binding response, transition to
  // writable, and allow data to be sent.
  ch2.AcceptConnection(GetCandidate(ch1.port()));
  EXPECT_THAT(
      WaitUntil([&] { return ch1.conn()->write_state(); },
                Eq(Connection::STATE_WRITABLE),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());

  ch1.conn()->SetUnwritableTimeout(TimeDelta::Seconds(1));
  ch1.conn()->set_unwritable_min_checks(3);
  // Send two checks.
  ch1.Ping(Timestamp::Millis(1));
  ch1.Ping(Timestamp::Millis(2));
  // We have not reached the timeout nor have we sent the minimum number of
  // checks to change the state to Unreliable.
  ch1.conn()->UpdateState(Timestamp::Seconds(1) - kEpsilon);
  EXPECT_EQ(Connection::STATE_WRITABLE, ch1.conn()->write_state());
  // We have not sent the minimum number of checks without responses.
  ch1.conn()->UpdateState(Timestamp::Seconds(1) + kMaxExpectedSimulatedRtt);
  EXPECT_EQ(Connection::STATE_WRITABLE, ch1.conn()->write_state());
  // Last ping after which the candidate pair should become Unreliable after
  // timeout.
  ch1.Ping(Timestamp::Millis(3));
  // We have not reached the timeout.
  ch1.conn()->UpdateState(Timestamp::Seconds(1) - kEpsilon);
  EXPECT_EQ(Connection::STATE_WRITABLE, ch1.conn()->write_state());
  // We should be in the state Unreliable now.
  ch1.conn()->UpdateState(Timestamp::Seconds(1) + kMaxExpectedSimulatedRtt);
  EXPECT_EQ(Connection::STATE_WRITE_UNRELIABLE, ch1.conn()->write_state());

  ch1.Stop();
  ch2.Stop();
}

TEST_F(PortTest, TestTimeoutForNeverWritable) {
  auto port1 = CreateUdpPort(kLocalAddr1);
  port1->SetIceRole(ICEROLE_CONTROLLING);
  auto port2 = CreateUdpPort(kLocalAddr2);
  port2->SetIceRole(ICEROLE_CONTROLLED);

  // Set up channels.
  TestChannel ch1(std::move(port1), time_controller_);
  TestChannel ch2(std::move(port2), time_controller_);

  // Acquire addresses.
  ch1.Start();
  ch2.Start();

  ch1.CreateConnection(GetCandidate(ch2.port()));
  ASSERT_TRUE(ch1.conn() != nullptr);
  EXPECT_EQ(Connection::STATE_WRITE_INIT, ch1.conn()->write_state());

  // Attempt to go directly to write timeout.
  for (uint32_t i = 1; i <= kConnectionWriteConnectFailures; ++i) {
    time_controller_.AdvanceTime(TimeDelta::Millis(1));
    ch1.Ping();
  }
  time_controller_.AdvanceTime(kConnectionWriteTimeout);
  ch1.conn()->UpdateState(env_.clock().CurrentTime());
  // The connection should be destroyed because no data received before the
  // write timeout.
  EXPECT_THAT(ch1.conn(), IsNull());
}

// This test verifies the connection setup between ICEMODE_FULL
// and ICEMODE_LITE.
// In this test `ch1` behaves like FULL mode client and we have created
// port which responds to the ping message just like LITE client.
TEST_F(PortTest, TestIceLiteConnectivity) {
  auto ice_full_port =
      CreateTestPort(kLocalAddr1, "lfrag", "lpass", ICEROLE_CONTROLLING);
  auto* ice_full_port_ptr = ice_full_port.get();

  auto ice_lite_port =
      CreateTestPort(kLocalAddr2, "rfrag", "rpass", ICEROLE_CONTROLLED);
  // Setup TestChannel. This behaves like FULL mode client.
  TestChannel ch1(std::move(ice_full_port), time_controller_);
  ch1.SetIceMode(ICEMODE_FULL);

  // Start gathering candidates.
  ch1.Start();
  ice_lite_port->PrepareAddress();

  ASSERT_THAT(
      WaitUntil([&] { return ch1.complete_count(); }, Eq(1),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  ASSERT_FALSE(ice_lite_port->Candidates().empty());

  ch1.CreateConnection(GetCandidate(ice_lite_port.get()));
  ASSERT_TRUE(ch1.conn() != nullptr);
  EXPECT_EQ(Connection::STATE_WRITE_INIT, ch1.conn()->write_state());

  // Send ping from full mode client.
  // This ping must not have USE_CANDIDATE_ATTR.
  ch1.Ping();

  // Verify stun ping is without USE_CANDIDATE_ATTR. Getting message directly
  // from port.
  ASSERT_THAT(
      WaitUntil([&] { return ice_full_port_ptr->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  IceMessage* msg = ice_full_port_ptr->last_stun_msg();
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_USE_CANDIDATE) == nullptr);

  // Respond with a BINDING-RESPONSE from litemode client.
  // NOTE: Ideally we should't create connection at this stage from lite
  // port, as it should be done only after receiving ping with USE_CANDIDATE.
  // But we need a connection to send a response message.
  auto* con = ice_lite_port->CreateConnection(
      ice_full_port_ptr->Candidates()[0], Port::ORIGIN_MESSAGE);
  std::unique_ptr<IceMessage> request = CopyStunMessage(*msg);
  con->SendStunBindingResponse(request.get());

  // Feeding the respone message from litemode to the full mode connection.
  ch1.conn()->OnReadPacket(ReceivedIpPacket(ice_lite_port->last_stun_buf(),
                                            SocketAddress(), std::nullopt));

  // Verifying full mode connection becomes writable from the response.
  EXPECT_THAT(
      WaitUntil([&] { return ch1.conn()->write_state(); },
                Eq(Connection::STATE_WRITABLE),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  EXPECT_TRUE(
      WaitUntil([&] { return ch1.nominated(); },
                {.timeout = kDefaultTimeout, .clock = &time_controller_}));

  // Clear existing stun messsages. Otherwise we will process old stun
  // message right after we send ping.
  ice_full_port_ptr->Reset();
  // Send ping. This must have USE_CANDIDATE_ATTR.
  ch1.Ping();
  ASSERT_THAT(
      WaitUntil([&] { return ice_full_port_ptr->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  msg = ice_full_port_ptr->last_stun_msg();
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_USE_CANDIDATE) != nullptr);
  ch1.Stop();
}

namespace {

// Utility function for testing goog ping.
std::optional<int> GetSupportedGoogPingVersion(const StunMessage* msg) {
  auto goog_misc = msg->GetUInt16List(STUN_ATTR_GOOG_MISC_INFO);
  if (goog_misc == nullptr) {
    return std::nullopt;
  }

  if (msg->type() == STUN_BINDING_REQUEST) {
    if (goog_misc->Size() <=
        static_cast<size_t>(IceGoogMiscInfoBindingRequestAttributeIndex::
                                SUPPORT_GOOG_PING_VERSION)) {
      return std::nullopt;
    }

    return goog_misc->GetType(
        static_cast<int>(IceGoogMiscInfoBindingRequestAttributeIndex::
                             SUPPORT_GOOG_PING_VERSION));
  }

  if (msg->type() == STUN_BINDING_RESPONSE) {
    if (goog_misc->Size() <=
        static_cast<size_t>(IceGoogMiscInfoBindingResponseAttributeIndex::
                                SUPPORT_GOOG_PING_VERSION)) {
      return std::nullopt;
    }

    return goog_misc->GetType(
        static_cast<int>(IceGoogMiscInfoBindingResponseAttributeIndex::
                             SUPPORT_GOOG_PING_VERSION));
  }
  return std::nullopt;
}

}  // namespace

class GoogPingTest
    : public PortTest,
      public ::testing::WithParamInterface<std::pair<bool, bool>> {};

// This test verifies the announce/enable on/off behavior
TEST_P(GoogPingTest, TestGoogPingAnnounceEnable) {
  IceFieldTrials trials;
  trials.announce_goog_ping = GetParam().first;
  trials.enable_goog_ping = GetParam().second;
  RTC_LOG(LS_INFO) << "Testing combination: "
                      " announce: "
                   << trials.announce_goog_ping
                   << " enable:" << trials.enable_goog_ping;

  auto port1_unique =
      CreateTestPort(kLocalAddr1, "lfrag", "lpass", ICEROLE_CONTROLLING);
  auto* port1 = port1_unique.get();
  auto port2 =
      CreateTestPort(kLocalAddr2, "rfrag", "rpass", ICEROLE_CONTROLLED);

  TestChannel ch1(std::move(port1_unique), time_controller_);
  // Block usage of STUN_ATTR_USE_CANDIDATE so that
  // ch1.conn() will sent GOOG_PING_REQUEST directly.
  // This only makes test a bit shorter...
  ch1.SetIceMode(ICEMODE_LITE);
  // Start gathering candidates.
  ch1.Start();
  port2->PrepareAddress();

  ASSERT_THAT(
      WaitUntil([&] { return ch1.complete_count(); }, Eq(1),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  ASSERT_FALSE(port2->Candidates().empty());

  ch1.CreateConnection(GetCandidate(port2.get()));
  ASSERT_TRUE(ch1.conn() != nullptr);
  EXPECT_EQ(Connection::STATE_WRITE_INIT, ch1.conn()->write_state());
  ch1.conn()->SetIceFieldTrials(&trials);

  // Send ping.
  ch1.Ping();

  ASSERT_THAT(
      WaitUntil([&] { return port1->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  const IceMessage* request1 = port1->last_stun_msg();

  ASSERT_EQ(trials.enable_goog_ping,
            GetSupportedGoogPingVersion(request1) &&
                GetSupportedGoogPingVersion(request1) >= kGoogPingVersion);

  auto* con =
      port2->CreateConnection(port1->Candidates()[0], Port::ORIGIN_MESSAGE);
  con->SetIceFieldTrials(&trials);

  con->SendStunBindingResponse(request1);

  // Then check the response matches the settings.
  const auto* response = port2->last_stun_msg();
  EXPECT_EQ(response->type(), STUN_BINDING_RESPONSE);
  EXPECT_EQ(trials.enable_goog_ping && trials.announce_goog_ping,
            GetSupportedGoogPingVersion(response) &&
                GetSupportedGoogPingVersion(response) >= kGoogPingVersion);

  // Feeding the respone message back.
  ch1.conn()->OnReadPacket(
      ReceivedIpPacket(port2->last_stun_buf(), SocketAddress(), std::nullopt));

  port1->Reset();
  port2->Reset();

  ch1.Ping();
  ASSERT_THAT(
      WaitUntil([&] { return port1->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  const IceMessage* request2 = port1->last_stun_msg();

  // It should be a GOOG_PING if both of these are TRUE
  if (trials.announce_goog_ping && trials.enable_goog_ping) {
    ASSERT_EQ(request2->type(), GOOG_PING_REQUEST);
    con->SendGoogPingResponse(request2);
  } else {
    ASSERT_EQ(request2->type(), STUN_BINDING_REQUEST);
    // If we sent a BINDING with enable, and we got a reply that
    // didn't contain announce, the next ping should not contain
    // the enable again.
    ASSERT_FALSE(GetSupportedGoogPingVersion(request2).has_value());
    con->SendStunBindingResponse(request2);
  }

  const auto* response2 = port2->last_stun_msg();
  ASSERT_TRUE(response2 != nullptr);

  // It should be a GOOG_PING_RESPONSE if both of these are TRUE
  if (trials.announce_goog_ping && trials.enable_goog_ping) {
    ASSERT_EQ(response2->type(), GOOG_PING_RESPONSE);
  } else {
    ASSERT_EQ(response2->type(), STUN_BINDING_RESPONSE);
  }

  ch1.Stop();
}

// This test if a someone send a STUN_BINDING with unsupported version
// (kGoogPingVersion == 0)
TEST_F(PortTest, TestGoogPingUnsupportedVersionInStunBinding) {
  IceFieldTrials trials;
  trials.announce_goog_ping = true;
  trials.enable_goog_ping = true;

  auto port1_unique =
      CreateTestPort(kLocalAddr1, "lfrag", "lpass", ICEROLE_CONTROLLING);
  auto* port1 = port1_unique.get();
  auto port2 =
      CreateTestPort(kLocalAddr2, "rfrag", "rpass", ICEROLE_CONTROLLED);

  TestChannel ch1(std::move(port1_unique), time_controller_);
  // Block usage of STUN_ATTR_USE_CANDIDATE so that
  // ch1.conn() will sent GOOG_PING_REQUEST directly.
  // This only makes test a bit shorter...
  ch1.SetIceMode(ICEMODE_LITE);
  // Start gathering candidates.
  ch1.Start();
  port2->PrepareAddress();

  ASSERT_THAT(
      WaitUntil([&] { return ch1.complete_count(); }, Eq(1),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  ASSERT_FALSE(port2->Candidates().empty());

  ch1.CreateConnection(GetCandidate(port2.get()));
  ASSERT_TRUE(ch1.conn() != nullptr);
  EXPECT_EQ(Connection::STATE_WRITE_INIT, ch1.conn()->write_state());
  ch1.conn()->SetIceFieldTrials(&trials);

  // Send ping.
  ch1.Ping();

  ASSERT_THAT(
      WaitUntil([&] { return port1->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  const IceMessage* request1 = port1->last_stun_msg();

  ASSERT_TRUE(GetSupportedGoogPingVersion(request1) &&
              GetSupportedGoogPingVersion(request1) >= kGoogPingVersion);

  // Modify the STUN message request1 to send GetSupportedGoogPingVersion == 0
  auto modified_request1 = request1->Clone();
  ASSERT_TRUE(modified_request1->RemoveAttribute(STUN_ATTR_GOOG_MISC_INFO) !=
              nullptr);
  ASSERT_TRUE(modified_request1->RemoveAttribute(STUN_ATTR_MESSAGE_INTEGRITY) !=
              nullptr);
  {
    auto list =
        StunAttribute::CreateUInt16ListAttribute(STUN_ATTR_GOOG_MISC_INFO);
    list->AddTypeAtIndex(
        static_cast<uint16_t>(IceGoogMiscInfoBindingRequestAttributeIndex::
                                  SUPPORT_GOOG_PING_VERSION),
        /* version */ 0);
    modified_request1->AddAttribute(std::move(list));
    modified_request1->AddMessageIntegrity("rpass");
  }
  auto* con =
      port2->CreateConnection(port1->Candidates()[0], Port::ORIGIN_MESSAGE);
  con->SetIceFieldTrials(&trials);

  con->SendStunBindingResponse(modified_request1.get());

  // Then check the response matches the settings.
  const auto* response = port2->last_stun_msg();
  EXPECT_EQ(response->type(), STUN_BINDING_RESPONSE);
  EXPECT_FALSE(GetSupportedGoogPingVersion(response));

  ch1.Stop();
}

// This test if a someone send a STUN_BINDING_RESPONSE with unsupported version
// (kGoogPingVersion == 0)
TEST_F(PortTest, TestGoogPingUnsupportedVersionInStunBindingResponse) {
  IceFieldTrials trials;
  trials.announce_goog_ping = true;
  trials.enable_goog_ping = true;

  auto port1_unique =
      CreateTestPort(kLocalAddr1, "lfrag", "lpass", ICEROLE_CONTROLLING);
  auto* port1 = port1_unique.get();
  auto port2 =
      CreateTestPort(kLocalAddr2, "rfrag", "rpass", ICEROLE_CONTROLLED);

  TestChannel ch1(std::move(port1_unique), time_controller_);
  // Block usage of STUN_ATTR_USE_CANDIDATE so that
  // ch1.conn() will sent GOOG_PING_REQUEST directly.
  // This only makes test a bit shorter...
  ch1.SetIceMode(ICEMODE_LITE);
  // Start gathering candidates.
  ch1.Start();
  port2->PrepareAddress();

  ASSERT_THAT(
      WaitUntil([&] { return ch1.complete_count(); }, Eq(1),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  ASSERT_FALSE(port2->Candidates().empty());

  ch1.CreateConnection(GetCandidate(port2.get()));
  ASSERT_TRUE(ch1.conn() != nullptr);
  EXPECT_EQ(Connection::STATE_WRITE_INIT, ch1.conn()->write_state());
  ch1.conn()->SetIceFieldTrials(&trials);

  // Send ping.
  ch1.Ping();

  ASSERT_THAT(
      WaitUntil([&] { return port1->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  const IceMessage* request1 = port1->last_stun_msg();

  ASSERT_TRUE(GetSupportedGoogPingVersion(request1) &&
              GetSupportedGoogPingVersion(request1) >= kGoogPingVersion);

  auto* con =
      port2->CreateConnection(port1->Candidates()[0], Port::ORIGIN_MESSAGE);
  con->SetIceFieldTrials(&trials);

  con->SendStunBindingResponse(request1);

  // Then check the response matches the settings.
  const auto* response = port2->last_stun_msg();
  EXPECT_EQ(response->type(), STUN_BINDING_RESPONSE);
  EXPECT_TRUE(GetSupportedGoogPingVersion(response));

  // Modify the STUN message response to contain GetSupportedGoogPingVersion ==
  // 0
  auto modified_response = response->Clone();
  ASSERT_TRUE(modified_response->RemoveAttribute(STUN_ATTR_GOOG_MISC_INFO) !=
              nullptr);
  ASSERT_TRUE(modified_response->RemoveAttribute(STUN_ATTR_MESSAGE_INTEGRITY) !=
              nullptr);
  ASSERT_TRUE(modified_response->RemoveAttribute(STUN_ATTR_FINGERPRINT) !=
              nullptr);
  {
    auto list =
        StunAttribute::CreateUInt16ListAttribute(STUN_ATTR_GOOG_MISC_INFO);
    list->AddTypeAtIndex(
        static_cast<uint16_t>(IceGoogMiscInfoBindingResponseAttributeIndex::
                                  SUPPORT_GOOG_PING_VERSION),
        /* version */ 0);
    modified_response->AddAttribute(std::move(list));
    modified_response->AddMessageIntegrity("rpass");
    modified_response->AddFingerprint();
  }

  ByteBufferWriter buf;
  modified_response->Write(&buf);

  // Feeding the modified respone message back.
  ch1.conn()->OnReadPacket(ReceivedIpPacket::CreateFromLegacy(
      buf.Data(), buf.Length(), /*packet_time_us=*/-1));

  port1->Reset();
  port2->Reset();

  ch1.Ping();
  ASSERT_THAT(
      WaitUntil([&] { return port1->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());

  // This should now be a STUN_BINDING...without a kGoogPingVersion
  const IceMessage* request2 = port1->last_stun_msg();
  EXPECT_EQ(request2->type(), STUN_BINDING_REQUEST);
  EXPECT_FALSE(GetSupportedGoogPingVersion(request2));

  ch1.Stop();
}

// Test that we don't crash if we receive a STUN_BINDING request with an empty
// STUN_ATTR_GOOG_MISC_INFO attribute.
TEST_F(PortTest, TestGoogPingEmptyMiscInfoInStunBindingRequest) {
  IceFieldTrials trials;
  trials.announce_goog_ping = true;
  trials.enable_goog_ping = true;

  auto port1_unique =
      CreateTestPort(kLocalAddr1, "lfrag", "lpass", ICEROLE_CONTROLLING);
  auto* port1 = port1_unique.get();
  auto port2 =
      CreateTestPort(kLocalAddr2, "rfrag", "rpass", ICEROLE_CONTROLLED);

  TestChannel ch1(std::move(port1_unique), time_controller_);
  ch1.SetIceMode(ICEMODE_LITE);
  ch1.Start();
  port2->PrepareAddress();

  ASSERT_THAT(
      WaitUntil([&] { return ch1.complete_count(); }, Eq(1),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  ASSERT_FALSE(port2->Candidates().empty());

  ch1.CreateConnection(GetCandidate(port2.get()));
  ASSERT_TRUE(ch1.conn() != nullptr);
  ch1.conn()->SetIceFieldTrials(&trials);

  ch1.Ping();

  ASSERT_THAT(
      WaitUntil([&] { return port1->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  const IceMessage* request1 = port1->last_stun_msg();

  // Modify the STUN message request1 to send empty STUN_ATTR_GOOG_MISC_INFO
  auto modified_request1 = request1->Clone();
  ASSERT_TRUE(modified_request1->RemoveAttribute(STUN_ATTR_MESSAGE_INTEGRITY) !=
              nullptr);
  ASSERT_TRUE(modified_request1->RemoveAttribute(STUN_ATTR_GOOG_MISC_INFO) !=
              nullptr);
  {
    auto list =
        StunAttribute::CreateUInt16ListAttribute(STUN_ATTR_GOOG_MISC_INFO);
    modified_request1->AddAttribute(std::move(list));
    modified_request1->AddMessageIntegrity("rpass");
    modified_request1->AddFingerprint();
  }
  auto* con =
      port2->CreateConnection(port1->Candidates()[0], Port::ORIGIN_MESSAGE);
  con->SetIceFieldTrials(&trials);

  // This should not crash.
  con->SendStunBindingResponse(modified_request1.get());

  // The response should not contain GOOG_PING_VERSION because request was
  // empty/invalid.
  const auto* response = port2->last_stun_msg();
  EXPECT_EQ(response->type(), STUN_BINDING_RESPONSE);
  EXPECT_FALSE(GetSupportedGoogPingVersion(response));

  ch1.Stop();
}

// Test that we don't crash if we receive a STUN_BINDING response with an empty
// STUN_ATTR_GOOG_MISC_INFO attribute.
TEST_F(PortTest, TestGoogPingEmptyMiscInfoInStunBindingResponse) {
  IceFieldTrials trials;
  trials.announce_goog_ping = true;
  trials.enable_goog_ping = true;

  auto port1_unique =
      CreateTestPort(kLocalAddr1, "lfrag", "lpass", ICEROLE_CONTROLLING);
  auto* port1 = port1_unique.get();
  auto port2 =
      CreateTestPort(kLocalAddr2, "rfrag", "rpass", ICEROLE_CONTROLLED);

  TestChannel ch1(std::move(port1_unique), time_controller_);
  ch1.SetIceMode(ICEMODE_LITE);
  ch1.Start();
  port2->PrepareAddress();

  ASSERT_THAT(
      WaitUntil([&] { return ch1.complete_count(); }, Eq(1),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  ASSERT_FALSE(port2->Candidates().empty());

  ch1.CreateConnection(GetCandidate(port2.get()));
  ASSERT_TRUE(ch1.conn() != nullptr);
  ch1.conn()->SetIceFieldTrials(&trials);

  ch1.Ping();

  ASSERT_THAT(
      WaitUntil([&] { return port1->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  const IceMessage* request1 = port1->last_stun_msg();

  auto* con =
      port2->CreateConnection(port1->Candidates()[0], Port::ORIGIN_MESSAGE);
  con->SetIceFieldTrials(&trials);

  con->SendStunBindingResponse(request1);

  const auto* response = port2->last_stun_msg();
  EXPECT_EQ(response->type(), STUN_BINDING_RESPONSE);
  EXPECT_TRUE(GetSupportedGoogPingVersion(response));

  // Modify the STUN message response to contain empty STUN_ATTR_GOOG_MISC_INFO
  auto modified_response = response->Clone();
  ASSERT_TRUE(modified_response->RemoveAttribute(STUN_ATTR_GOOG_MISC_INFO) !=
              nullptr);
  ASSERT_TRUE(modified_response->RemoveAttribute(STUN_ATTR_MESSAGE_INTEGRITY) !=
              nullptr);
  ASSERT_TRUE(modified_response->RemoveAttribute(STUN_ATTR_FINGERPRINT) !=
              nullptr);
  {
    auto list =
        StunAttribute::CreateUInt16ListAttribute(STUN_ATTR_GOOG_MISC_INFO);
    ASSERT_EQ(0U, list->Size());
    modified_response->AddAttribute(std::move(list));
    modified_response->AddMessageIntegrity("rpass");
    modified_response->AddFingerprint();
  }

  ByteBufferWriter buf;
  modified_response->Write(&buf);

  // Feeding the modified response message back. This should not crash.
  ch1.conn()->OnReadPacket(ReceivedIpPacket::CreateFromLegacy(
      buf.Data(), buf.Length(), /*packet_time_us=*/-1));

  port1->Reset();
  port2->Reset();

  ch1.Ping();
  ASSERT_THAT(
      WaitUntil([&] { return port1->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());

  // This should now be a STUN_BINDING...without a kGoogPingVersion
  const IceMessage* request2 = port1->last_stun_msg();
  EXPECT_EQ(request2->type(), STUN_BINDING_REQUEST);
  EXPECT_FALSE(GetSupportedGoogPingVersion(request2));

  ch1.Stop();
}

INSTANTIATE_TEST_SUITE_P(GoogPingTest,
                         GoogPingTest,
                         // test all combinations of <announce, enable> pairs.
                         ::testing::Values(std::make_pair(false, false),
                                           std::make_pair(true, false),
                                           std::make_pair(false, true),
                                           std::make_pair(true, true)));

// This test checks that a change in attributes falls back to STUN_BINDING
TEST_F(PortTest, TestChangeInAttributeMakesGoogPingFallsbackToStunBinding) {
  IceFieldTrials trials;
  trials.announce_goog_ping = true;
  trials.enable_goog_ping = true;

  auto port1_unique =
      CreateTestPort(kLocalAddr1, "lfrag", "lpass", ICEROLE_CONTROLLING);
  auto* port1 = port1_unique.get();
  auto port2 =
      CreateTestPort(kLocalAddr2, "rfrag", "rpass", ICEROLE_CONTROLLED);

  TestChannel ch1(std::move(port1_unique), time_controller_);
  // Block usage of STUN_ATTR_USE_CANDIDATE so that
  // ch1.conn() will sent GOOG_PING_REQUEST directly.
  // This only makes test a bit shorter...
  ch1.SetIceMode(ICEMODE_LITE);
  // Start gathering candidates.
  ch1.Start();
  port2->PrepareAddress();

  ASSERT_THAT(
      WaitUntil([&] { return ch1.complete_count(); }, Eq(1),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  ASSERT_FALSE(port2->Candidates().empty());

  ch1.CreateConnection(GetCandidate(port2.get()));
  ASSERT_TRUE(ch1.conn() != nullptr);
  EXPECT_EQ(Connection::STATE_WRITE_INIT, ch1.conn()->write_state());
  ch1.conn()->SetIceFieldTrials(&trials);

  // Send ping.
  ch1.Ping();

  ASSERT_THAT(
      WaitUntil([&] { return port1->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  const IceMessage* msg = port1->last_stun_msg();
  auto* con =
      port2->CreateConnection(port1->Candidates()[0], Port::ORIGIN_MESSAGE);
  con->SetIceFieldTrials(&trials);

  // Feed the message into the connection.
  con->SendStunBindingResponse(msg);

  // The check reply wrt to settings.
  const auto* response = port2->last_stun_msg();
  ASSERT_EQ(response->type(), STUN_BINDING_RESPONSE);
  ASSERT_TRUE(GetSupportedGoogPingVersion(response) >= kGoogPingVersion);

  // Feeding the respone message back.
  ch1.conn()->OnReadPacket(
      ReceivedIpPacket(port2->last_stun_buf(), SocketAddress(), std::nullopt));

  port1->Reset();
  port2->Reset();

  ch1.Ping();
  ASSERT_THAT(
      WaitUntil([&] { return port1->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  const IceMessage* msg2 = port1->last_stun_msg();

  // It should be a GOOG_PING if both of these are TRUE
  ASSERT_EQ(msg2->type(), GOOG_PING_REQUEST);
  con->SendGoogPingResponse(msg2);

  const auto* response2 = port2->last_stun_msg();
  ASSERT_TRUE(response2 != nullptr);

  // It should be a GOOG_PING_RESPONSE.
  ASSERT_EQ(response2->type(), GOOG_PING_RESPONSE);

  // And now the third ping.
  port1->Reset();
  port2->Reset();

  // Modify the message to be sent.
  ch1.conn()->set_use_candidate_attr(!ch1.conn()->use_candidate_attr());

  ch1.Ping();
  ASSERT_THAT(
      WaitUntil([&] { return port1->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  const IceMessage* msg3 = port1->last_stun_msg();

  // It should be a STUN_BINDING_REQUEST
  ASSERT_EQ(msg3->type(), STUN_BINDING_REQUEST);

  ch1.Stop();
}

// This test that an error response fall back to STUN_BINDING.
TEST_F(PortTest, TestErrorResponseMakesGoogPingFallBackToStunBinding) {
  IceFieldTrials trials;
  trials.announce_goog_ping = true;
  trials.enable_goog_ping = true;

  auto port1_unique =
      CreateTestPort(kLocalAddr1, "lfrag", "lpass", ICEROLE_CONTROLLING);
  auto* port1 = port1_unique.get();
  auto port2 =
      CreateTestPort(kLocalAddr2, "rfrag", "rpass", ICEROLE_CONTROLLED);

  TestChannel ch1(std::move(port1_unique), time_controller_);
  // Block usage of STUN_ATTR_USE_CANDIDATE so that
  // ch1.conn() will sent GOOG_PING_REQUEST directly.
  // This only makes test a bit shorter...
  ch1.SetIceMode(ICEMODE_LITE);
  // Start gathering candidates.
  ch1.Start();
  port2->PrepareAddress();

  ASSERT_THAT(
      WaitUntil([&] { return ch1.complete_count(); }, Eq(1),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  ASSERT_FALSE(port2->Candidates().empty());

  ch1.CreateConnection(GetCandidate(port2.get()));
  ASSERT_TRUE(ch1.conn() != nullptr);
  EXPECT_EQ(Connection::STATE_WRITE_INIT, ch1.conn()->write_state());
  ch1.conn()->SetIceFieldTrials(&trials);

  // Send ping.
  ch1.Ping();

  ASSERT_THAT(
      WaitUntil([&] { return port1->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  const IceMessage* msg = port1->last_stun_msg();
  auto* con =
      port2->CreateConnection(port1->Candidates()[0], Port::ORIGIN_MESSAGE);
  con->SetIceFieldTrials(&trials);

  // Feed the message into the connection.
  con->SendStunBindingResponse(msg);

  // The check reply wrt to settings.
  const auto* response = port2->last_stun_msg();
  ASSERT_EQ(response->type(), STUN_BINDING_RESPONSE);
  ASSERT_TRUE(GetSupportedGoogPingVersion(response) >= kGoogPingVersion);

  // Feeding the respone message back.
  ch1.conn()->OnReadPacket(
      ReceivedIpPacket(port2->last_stun_buf(), SocketAddress(), std::nullopt));

  port1->Reset();
  port2->Reset();

  ch1.Ping();
  ASSERT_THAT(
      WaitUntil([&] { return port1->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  const IceMessage* msg2 = port1->last_stun_msg();

  // It should be a GOOG_PING.
  ASSERT_EQ(msg2->type(), GOOG_PING_REQUEST);
  con->SendGoogPingResponse(msg2);

  const auto* response2 = port2->last_stun_msg();
  ASSERT_TRUE(response2 != nullptr);

  // It should be a GOOG_PING_RESPONSE.
  ASSERT_EQ(response2->type(), GOOG_PING_RESPONSE);

  // But rather than the RESPONSE...feedback an error.
  StunMessage error_response(GOOG_PING_ERROR_RESPONSE);
  error_response.SetTransactionIdForTesting(response2->transaction_id());
  error_response.AddMessageIntegrity32("rpass");
  ByteBufferWriter buf;
  error_response.Write(&buf);

  ch1.conn()->OnReadPacket(ReceivedIpPacket::CreateFromLegacy(
      buf.Data(), buf.Length(), /*packet_time_us=*/-1));

  // And now the third ping...this should be a binding.
  port1->Reset();
  port2->Reset();

  ch1.Ping();
  ASSERT_THAT(
      WaitUntil([&] { return port1->last_stun_msg(); }, NotNull(),
                {.timeout = kDefaultTimeout, .clock = &time_controller_}),
      IsRtcOk());
  const IceMessage* msg3 = port1->last_stun_msg();

  // It should be a STUN_BINDING_REQUEST
  ASSERT_EQ(msg3->type(), STUN_BINDING_REQUEST);

  ch1.Stop();
}

// This test case verifies that both the controlling port and the controlled
// port will time out after connectivity is lost, if they are not marked as
// "keep alive until pruned."
TEST_F(PortTest, TestPortTimeoutIfNotKeptAlive) {
  int timeout_delay = 100;
  auto port1 = CreateUdpPort(kLocalAddr1);
  ConnectToSignalDestroyed(port1.get());
  port1->set_timeout_delay(timeout_delay);  // milliseconds
  port1->SetIceRole(ICEROLE_CONTROLLING);
  auto port2 = CreateUdpPort(kLocalAddr2);
  ConnectToSignalDestroyed(port2.get());
  port2->set_timeout_delay(timeout_delay);  // milliseconds
  port2->SetIceRole(ICEROLE_CONTROLLED);
  // Set up channels and ensure both ports will be deleted.
  TestChannel ch1(std::move(port1), time_controller_);
  TestChannel ch2(std::move(port2), time_controller_);

  // Simulate a connection that succeeds, and then is destroyed.
  StartConnectAndStopChannels(&ch1, &ch2);
  // After the connection is destroyed, the port will be destroyed because
  // none of them is marked as "keep alive until pruned.
  EXPECT_THAT(WaitUntil([&] { return ports_destroyed(); }, Eq(2),
                        {.clock = &time_controller_}),
              IsRtcOk());
}

// Test that if after all connection are destroyed, new connections are created
// and destroyed again, ports won't be destroyed until a timeout period passes
// after the last set of connections are all destroyed.
TEST_F(PortTest, TestPortTimeoutAfterNewConnectionCreatedAndDestroyed) {
  int timeout_delay = 100;
  auto port1 = CreateUdpPort(kLocalAddr1);
  ConnectToSignalDestroyed(port1.get());
  port1->set_timeout_delay(timeout_delay);  // milliseconds
  port1->SetIceRole(ICEROLE_CONTROLLING);
  auto port2 = CreateUdpPort(kLocalAddr2);
  ConnectToSignalDestroyed(port2.get());
  port2->set_timeout_delay(timeout_delay);  // milliseconds

  port2->SetIceRole(ICEROLE_CONTROLLED);
  // Set up channels and ensure both ports will be deleted.
  TestChannel ch1(std::move(port1), time_controller_);
  TestChannel ch2(std::move(port2), time_controller_);

  // Simulate a connection that succeeds, and then is destroyed.
  StartConnectAndStopChannels(&ch1, &ch2);
  time_controller_.AdvanceTime(TimeDelta::Millis(80));
  EXPECT_EQ(0, ports_destroyed());

  // Start the second set of connection and destroy them.
  ch1.CreateConnection(GetCandidate(ch2.port()));
  ch2.CreateConnection(GetCandidate(ch1.port()));
  ch1.Stop();
  ch2.Stop();

  time_controller_.AdvanceTime(TimeDelta::Millis(80));
  EXPECT_EQ(0, ports_destroyed());

  // The ports on both sides should be destroyed after timeout.
  EXPECT_THAT(WaitUntil([&] { return ports_destroyed(); }, Eq(2),
                        {.clock = &time_controller_}),
              IsRtcOk());
}

// This test case verifies that neither the controlling port nor the controlled
// port will time out after connectivity is lost if they are marked as "keep
// alive until pruned". They will time out after they are pruned.
TEST_F(PortTest, TestPortNotTimeoutUntilPruned) {
  int timeout_delay = 100;
  auto port1 = CreateUdpPort(kLocalAddr1);
  ConnectToSignalDestroyed(port1.get());
  port1->set_timeout_delay(timeout_delay);  // milliseconds
  port1->SetIceRole(ICEROLE_CONTROLLING);
  auto port2 = CreateUdpPort(kLocalAddr2);
  ConnectToSignalDestroyed(port2.get());
  port2->set_timeout_delay(timeout_delay);  // milliseconds
  port2->SetIceRole(
      ICEROLE_CONTROLLED);  // The connection must not be destroyed before a
                            // connection is attempted.
  EXPECT_EQ(0, ports_destroyed());

  port1->set_component(ICE_CANDIDATE_COMPONENT_DEFAULT);
  port2->set_component(ICE_CANDIDATE_COMPONENT_DEFAULT);

  // Set up channels and keep the port alive.
  TestChannel ch1(std::move(port1), time_controller_);
  TestChannel ch2(std::move(port2), time_controller_);
  // Simulate a connection that succeeds, and then is destroyed. But ports
  // are kept alive. Ports won't be destroyed.
  StartConnectAndStopChannels(&ch1, &ch2);
  ch1.port()->KeepAliveUntilPruned();
  ch2.port()->KeepAliveUntilPruned();
  time_controller_.AdvanceTime(TimeDelta::Millis(150));
  EXPECT_EQ(0, ports_destroyed());

  // If they are pruned now, they will be destroyed right away.
  ch1.port()->Prune();
  ch2.port()->Prune();
  // The ports on both sides should be destroyed after timeout.
  EXPECT_THAT(WaitUntil([&] { return ports_destroyed(); }, Eq(2),
                        {.clock = &time_controller_}),
              IsRtcOk());
}

TEST_F(PortTest, TestSupportsProtocol) {
  auto udp_port = CreateUdpPort(kLocalAddr1);
  EXPECT_TRUE(udp_port->SupportsProtocol(UDP_PROTOCOL_NAME));
  EXPECT_FALSE(udp_port->SupportsProtocol(TCP_PROTOCOL_NAME));

  auto stun_port = CreateStunPort(kLocalAddr1, nat_socket_factory1());
  EXPECT_TRUE(stun_port->SupportsProtocol(UDP_PROTOCOL_NAME));
  EXPECT_FALSE(stun_port->SupportsProtocol(TCP_PROTOCOL_NAME));

  auto tcp_port = CreateTcpPort(kLocalAddr1);
  EXPECT_TRUE(tcp_port->SupportsProtocol(TCP_PROTOCOL_NAME));
  EXPECT_TRUE(tcp_port->SupportsProtocol(SSLTCP_PROTOCOL_NAME));
  EXPECT_FALSE(tcp_port->SupportsProtocol(UDP_PROTOCOL_NAME));

  auto turn_port =
      CreateTurnPort(kLocalAddr1, nat_socket_factory1(), PROTO_UDP, PROTO_UDP);
  EXPECT_TRUE(turn_port->SupportsProtocol(UDP_PROTOCOL_NAME));
  EXPECT_FALSE(turn_port->SupportsProtocol(TCP_PROTOCOL_NAME));
}

// Test that SetIceParameters updates the component, ufrag and password
// on both the port itself and its candidates.
TEST_F(PortTest, TestSetIceParameters) {
  auto port = CreateTestPort(kLocalAddr1, "ufrag1", "password1");
  port->PrepareAddress();
  EXPECT_EQ(1UL, port->Candidates().size());
  port->SetIceParameters(1, "ufrag2", "password2");
  EXPECT_EQ(1, port->component());
  EXPECT_EQ("ufrag2", port->username_fragment());
  EXPECT_EQ("password2", port->password());
  const Candidate& candidate = port->Candidates()[0];
  EXPECT_EQ(1, candidate.component());
  EXPECT_EQ("ufrag2", candidate.username());
  EXPECT_EQ("password2", candidate.password());
}

TEST_F(PortTest, TestAddConnectionWithSameAddress) {
  auto port = CreateTestPort(kLocalAddr1, "ufrag1", "password1");
  port->PrepareAddress();
  EXPECT_EQ(1u, port->Candidates().size());
  SocketAddress address("1.1.1.1", 5000);
  Candidate candidate(1, "udp", address, 0, "", "", IceCandidateType::kRelay, 0,
                      "");
  Connection* conn1 = port->CreateConnection(candidate, Port::ORIGIN_MESSAGE);
  Connection* conn_in_use = port->GetConnection(address);
  EXPECT_EQ(conn1, conn_in_use);
  EXPECT_EQ(0u, conn_in_use->remote_candidate().generation());

  // Creating with a candidate with the same address again will get us a
  // different connection with the new candidate.
  candidate.set_generation(2);
  Connection* conn2 = port->CreateConnection(candidate, Port::ORIGIN_MESSAGE);
  EXPECT_NE(conn1, conn2);
  conn_in_use = port->GetConnection(address);
  EXPECT_EQ(conn2, conn_in_use);
  EXPECT_EQ(2u, conn_in_use->remote_candidate().generation());

  // Make sure the new connection was not deleted.
  time_controller_.AdvanceTime(TimeDelta::Millis(300));
  EXPECT_TRUE(port->GetConnection(address) != nullptr);
}

// This is a COMPILE time check to ensure that the TurnPort class can be
// subclassed properly.
class TestPortWrapper : public TurnPort {
  TestPortWrapper()
      : TurnPort(CreateArgs(),
                 /*socket=*/nullptr,
                 my_server_address_,
                 credentials_,
                 /*server_priority=*/1,
                 /*tls_alpn_protocols=*/{"alpn"},
                 /*tls_elliptic_curves=*/{"ecc"},
                 /*customizer=*/nullptr,
                 /*tls_cert_verifier=*/nullptr) {}

 private:
  static PortParametersRef CreateArgs() {
    return {.env = CreateTestEnvironment()};
  }

  ProtocolAddress my_server_address_{kLocalAddr1, PROTO_UDP};
  RelayCredentials credentials_;
};

}  // namespace
}  // namespace webrtc
