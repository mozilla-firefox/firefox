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

#include <string.h>

#include <cstdint>
#include <limits>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "api/array_view.h"
#include "api/candidate.h"
#include "api/packet_socket_factory.h"
#include "api/transport/stun.h"
#include "api/units/time_delta.h"
#include "p2p/base/basic_packet_socket_factory.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/port_allocator.h"
#include "p2p/base/port_interface.h"
#include "p2p/base/stun_port.h"
#include "p2p/base/stun_server.h"
#include "p2p/base/tcp_port.h"
#include "p2p/base/test_stun_server.h"
#include "p2p/base/test_turn_server.h"
#include "p2p/base/transport_description.h"
#include "p2p/base/turn_port.h"
#include "p2p/base/turn_server.h"
#include "p2p/client/relay_port_factory_interface.h"
#include "rtc_base/arraysize.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/buffer.h"
#include "rtc_base/byte_buffer.h"
#include "rtc_base/checks.h"
#include "rtc_base/dscp.h"
#include "rtc_base/fake_clock.h"
#include "rtc_base/gunit.h"
#include "rtc_base/helpers.h"
#include "rtc_base/logging.h"
#include "rtc_base/nat_server.h"
#include "rtc_base/nat_socket_factory.h"
#include "rtc_base/nat_types.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/network.h"
#include "rtc_base/network/received_packet.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/network_constants.h"
#include "rtc_base/proxy_info.h"
#include "rtc_base/socket.h"
#include "rtc_base/socket_adapters.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/third_party/sigslot/sigslot.h"
#include "rtc_base/thread.h"
#include "rtc_base/time_utils.h"
#include "rtc_base/virtual_socket_server.h"
#include "test/gtest.h"
#include "test/scoped_key_value_config.h"

using rtc::AsyncListenSocket;
using rtc::AsyncPacketSocket;
using rtc::ByteBufferReader;
using rtc::ByteBufferWriter;
using rtc::NAT_ADDR_RESTRICTED;
using rtc::NAT_OPEN_CONE;
using rtc::NAT_PORT_RESTRICTED;
using rtc::NAT_SYMMETRIC;
using rtc::NATType;
using rtc::PacketSocketFactory;
using rtc::Socket;
using rtc::SocketAddress;
using webrtc::IceCandidateType;

namespace cricket {
namespace {

constexpr int kDefaultTimeout = 3000;
constexpr int kShortTimeout = 1000;
constexpr int kMaxExpectedSimulatedRtt = 200;
const SocketAddress kLocalAddr1("192.168.1.2", 0);
const SocketAddress kLocalAddr2("192.168.1.3", 0);
const SocketAddress kLinkLocalIPv6Addr("fe80::aabb:ccff:fedd:eeff", 0);
const SocketAddress kNatAddr1("77.77.77.77", rtc::NAT_SERVER_UDP_PORT);
const SocketAddress kNatAddr2("88.88.88.88", rtc::NAT_SERVER_UDP_PORT);
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

constexpr int kTiebreaker1 = 11111;
constexpr int kTiebreaker2 = 22222;
constexpr int kTiebreakerDefault = 44444;

const char* data = "ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890";

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

}  // namespace

// Stub port class for testing STUN generation and processing.
class TestPort : public Port {
 public:
  TestPort(rtc::Thread* thread,
           rtc::PacketSocketFactory* factory,
           const rtc::Network* network,
           uint16_t min_port,
           uint16_t max_port,
           absl::string_view username_fragment,
           absl::string_view password,
           const webrtc::FieldTrialsView* field_trials = nullptr)
      : Port(thread,
             IceCandidateType::kHost,
             factory,
             network,
             min_port,
             max_port,
             username_fragment,
             password,
             field_trials) {}
  ~TestPort() {}

  // Expose GetStunMessage so that we can test it.
  using cricket::Port::GetStunMessage;

  // The last StunMessage that was sent on this Port.
  rtc::ArrayView<const uint8_t> last_stun_buf() {
    if (!last_stun_buf_)
      return rtc::ArrayView<const uint8_t>();
    return *last_stun_buf_;
  }
  IceMessage* last_stun_msg() { return last_stun_msg_.get(); }
  int last_stun_error_code() {
    int code = 0;
    if (last_stun_msg_) {
      const StunErrorCodeAttribute* error_attr = last_stun_msg_->GetErrorCode();
      if (error_attr) {
        code = error_attr->code();
      }
    }
    return code;
  }

  virtual void PrepareAddress() {
    // Act as if the socket was bound to the best IP on the network, to the
    // first port in the allowed range.
    rtc::SocketAddress addr(Network()->GetBestIP(), min_port());
    AddAddress(addr, addr, rtc::SocketAddress(), "udp", "", "", type(),
               ICE_TYPE_PREFERENCE_HOST, 0, "", true);
  }

  virtual bool SupportsProtocol(absl::string_view protocol) const {
    return true;
  }

  virtual ProtocolType GetProtocol() const { return PROTO_UDP; }

  // Exposed for testing candidate building.
  void AddCandidateAddress(const rtc::SocketAddress& addr) {
    AddAddress(addr, addr, rtc::SocketAddress(), "udp", "", "", type(),
               type_preference_, 0, "", false);
  }
  void AddCandidateAddress(const rtc::SocketAddress& addr,
                           const rtc::SocketAddress& base_address,
                           IceCandidateType type,
                           int type_preference,
                           bool final) {
    AddAddress(addr, base_address, rtc::SocketAddress(), "udp", "", "", type,
               type_preference, 0, "", final);
  }

  virtual Connection* CreateConnection(const Candidate& remote_candidate,
                                       CandidateOrigin origin) {
    Connection* conn = new ProxyConnection(NewWeakPtr(), 0, remote_candidate);
    AddOrReplaceConnection(conn);
    // Set use-candidate attribute flag as this will add USE-CANDIDATE attribute
    // in STUN binding requests.
    conn->set_use_candidate_attr(true);
    return conn;
  }
  virtual int SendTo(const void* data,
                     size_t size,
                     const rtc::SocketAddress& addr,
                     const rtc::PacketOptions& options,
                     bool payload) {
    if (!payload) {
      auto msg = std::make_unique<IceMessage>();
      auto buf = std::make_unique<rtc::BufferT<uint8_t>>(
          static_cast<const char*>(data), size);
      ByteBufferReader read_buf(*buf);
      if (!msg->Read(&read_buf)) {
        return -1;
      }
      last_stun_buf_ = std::move(buf);
      last_stun_msg_ = std::move(msg);
    }
    return static_cast<int>(size);
  }
  virtual int SetOption(rtc::Socket::Option opt, int value) { return 0; }
  virtual int GetOption(rtc::Socket::Option opt, int* value) { return -1; }
  virtual int GetError() { return 0; }
  void Reset() {
    last_stun_buf_.reset();
    last_stun_msg_.reset();
  }
  void set_type_preference(int type_preference) {
    type_preference_ = type_preference;
  }

 private:
  void OnSentPacket(rtc::AsyncPacketSocket* socket,
                    const rtc::SentPacket& sent_packet) {
    PortInterface::SignalSentPacket(sent_packet);
  }
  std::unique_ptr<rtc::BufferT<uint8_t>> last_stun_buf_;
  std::unique_ptr<IceMessage> last_stun_msg_;
  int type_preference_ = 0;
};

bool GetStunMessageFromBufferWriter(TestPort* port,
                                    ByteBufferWriter* buf,
                                    const rtc::SocketAddress& addr,
                                    std::unique_ptr<IceMessage>* out_msg,
                                    std::string* out_username) {
  return port->GetStunMessage(reinterpret_cast<const char*>(buf->Data()),
                              buf->Length(), addr, out_msg, out_username);
}

static void SendPingAndReceiveResponse(Connection* lconn,
                                       TestPort* lport,
                                       Connection* rconn,
                                       TestPort* rport,
                                       rtc::ScopedFakeClock* clock,
                                       int64_t ms) {
  lconn->Ping(rtc::TimeMillis());
  ASSERT_TRUE_WAIT(lport->last_stun_msg(), kDefaultTimeout);
  ASSERT_GT(lport->last_stun_buf().size(), 0u);
  rconn->OnReadPacket(rtc::ReceivedPacket(lport->last_stun_buf(),
                                          rtc::SocketAddress(), absl::nullopt));

  clock->AdvanceTime(webrtc::TimeDelta::Millis(ms));
  ASSERT_TRUE_WAIT(rport->last_stun_msg(), kDefaultTimeout);
  ASSERT_GT(rport->last_stun_buf().size(), 0u);
  lconn->OnReadPacket(rtc::ReceivedPacket(rport->last_stun_buf(),
                                          rtc::SocketAddress(), absl::nullopt));
}

class TestChannel : public sigslot::has_slots<> {
 public:
  // Takes ownership of `p1` (but not `p2`).
  explicit TestChannel(std::unique_ptr<Port> p1) : port_(std::move(p1)) {
    port_->SignalPortComplete.connect(this, &TestChannel::OnPortComplete);
    port_->SignalUnknownAddress.connect(this, &TestChannel::OnUnknownAddress);
    port_->SubscribePortDestroyed(
        [this](PortInterface* port) { OnSrcPortDestroyed(port); });
  }

  ~TestChannel() { Stop(); }

  int complete_count() { return complete_count_; }
  Connection* conn() { return conn_; }
  const SocketAddress& remote_address() { return remote_address_; }
  const std::string remote_fragment() { return remote_frag_; }

  void Start() { port_->PrepareAddress(); }
  void CreateConnection(const Candidate& remote_candidate) {
    RTC_DCHECK(!conn_);
    conn_ = port_->CreateConnection(remote_candidate, Port::ORIGIN_MESSAGE);
    IceMode remote_ice_mode =
        (ice_mode_ == ICEMODE_FULL) ? ICEMODE_LITE : ICEMODE_FULL;
    conn_->set_use_candidate_attr(remote_ice_mode == ICEMODE_FULL);
    conn_->SignalStateChange.connect(this,
                                     &TestChannel::OnConnectionStateChange);
    conn_->SignalDestroyed.connect(this, &TestChannel::OnDestroyed);
    conn_->SignalReadyToSend.connect(this,
                                     &TestChannel::OnConnectionReadyToSend);
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
      conn_->SignalDestroyed.disconnect(this);
      conn_ = nullptr;
    }
    ASSERT_TRUE(remote_request_.get() != NULL);
    Candidate c = remote_candidate;
    c.set_address(remote_address_);
    conn_ = port_->CreateConnection(c, Port::ORIGIN_MESSAGE);
    conn_->SignalDestroyed.connect(this, &TestChannel::OnDestroyed);
    conn_->SendStunBindingResponse(remote_request_.get());
    remote_request_.reset();
  }
  void Ping() { Ping(0); }
  void Ping(int64_t now) { conn_->Ping(now); }
  void Stop() {
    if (conn_) {
      port_->DestroyConnection(conn_);
      conn_ = nullptr;
    }
  }

  void OnPortComplete(Port* port) { complete_count_++; }
  void SetIceMode(IceMode ice_mode) { ice_mode_ = ice_mode; }

  int SendData(const char* data, size_t len) {
    rtc::PacketOptions options;
    return conn_->Send(data, len, options);
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
    const cricket::StunUInt32Attribute* priority_attr =
        msg->GetUInt32(STUN_ATTR_PRIORITY);
    const cricket::StunByteStringAttribute* mi_attr =
        msg->GetByteString(STUN_ATTR_MESSAGE_INTEGRITY);
    const cricket::StunUInt32Attribute* fingerprint_attr =
        msg->GetUInt32(STUN_ATTR_FINGERPRINT);
    EXPECT_TRUE(priority_attr != NULL);
    EXPECT_TRUE(mi_attr != NULL);
    EXPECT_TRUE(fingerprint_attr != NULL);
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

class PortTest : public ::testing::Test, public sigslot::has_slots<> {
 public:
  PortTest()
      : ss_(new rtc::VirtualSocketServer()),
        main_(ss_.get()),
        socket_factory_(ss_.get()),
        nat_factory1_(ss_.get(), kNatAddr1, SocketAddress()),
        nat_factory2_(ss_.get(), kNatAddr2, SocketAddress()),
        nat_socket_factory1_(&nat_factory1_),
        nat_socket_factory2_(&nat_factory2_),
        stun_server_(TestStunServer::Create(ss_.get(), kStunAddr, main_)),
        turn_server_(&main_, ss_.get(), kTurnUdpIntAddr, kTurnUdpExtAddr),
        username_(rtc::CreateRandomString(ICE_UFRAG_LENGTH)),
        password_(rtc::CreateRandomString(ICE_PWD_LENGTH)),
        role_conflict_(false),
        ports_destroyed_(0) {}

  ~PortTest() {
    // Workaround for tests that trigger async destruction of objects that we
    // need to give an opportunity here to run, before proceeding with other
    // teardown.
    rtc::Thread::Current()->ProcessMessages(0);
  }

 protected:
  std::string password() { return password_; }

  void TestLocalToLocal() {
    auto port1 = CreateUdpPort(kLocalAddr1);
    port1->SetIceRole(cricket::ICEROLE_CONTROLLING);
    auto port2 = CreateUdpPort(kLocalAddr2);
    port2->SetIceRole(cricket::ICEROLE_CONTROLLED);
    TestConnectivity("udp", std::move(port1), "udp", std::move(port2), true,
                     true, true, true);
  }
  void TestLocalToStun(NATType ntype) {
    auto port1 = CreateUdpPort(kLocalAddr1);
    port1->SetIceRole(cricket::ICEROLE_CONTROLLING);
    nat_server2_ = CreateNatServer(kNatAddr2, ntype);
    auto port2 = CreateStunPort(kLocalAddr2, &nat_socket_factory2_);
    port2->SetIceRole(cricket::ICEROLE_CONTROLLED);
    TestConnectivity("udp", std::move(port1), StunName(ntype), std::move(port2),
                     ntype == NAT_OPEN_CONE, true, ntype != NAT_SYMMETRIC,
                     true);
  }
  void TestLocalToRelay(ProtocolType proto) {
    auto port1 = CreateUdpPort(kLocalAddr1);
    port1->SetIceRole(cricket::ICEROLE_CONTROLLING);
    auto port2 = CreateRelayPort(kLocalAddr2, proto, PROTO_UDP);
    port2->SetIceRole(cricket::ICEROLE_CONTROLLED);
    TestConnectivity("udp", std::move(port1), RelayName(proto),
                     std::move(port2), false, true, true, true);
  }
  void TestStunToLocal(NATType ntype) {
    nat_server1_ = CreateNatServer(kNatAddr1, ntype);
    auto port1 = CreateStunPort(kLocalAddr1, &nat_socket_factory1_);
    port1->SetIceRole(cricket::ICEROLE_CONTROLLING);
    auto port2 = CreateUdpPort(kLocalAddr2);
    port2->SetIceRole(cricket::ICEROLE_CONTROLLED);
    TestConnectivity(StunName(ntype), std::move(port1), "udp", std::move(port2),
                     true, ntype != NAT_SYMMETRIC, true, true);
  }
  void TestStunToStun(NATType ntype1, NATType ntype2) {
    nat_server1_ = CreateNatServer(kNatAddr1, ntype1);
    auto port1 = CreateStunPort(kLocalAddr1, &nat_socket_factory1_);
    port1->SetIceRole(cricket::ICEROLE_CONTROLLING);
    nat_server2_ = CreateNatServer(kNatAddr2, ntype2);
    auto port2 = CreateStunPort(kLocalAddr2, &nat_socket_factory2_);
    port2->SetIceRole(cricket::ICEROLE_CONTROLLED);
    TestConnectivity(StunName(ntype1), std::move(port1), StunName(ntype2),
                     std::move(port2), ntype2 == NAT_OPEN_CONE,
                     ntype1 != NAT_SYMMETRIC, ntype2 != NAT_SYMMETRIC,
                     ntype1 + ntype2 < (NAT_PORT_RESTRICTED + NAT_SYMMETRIC));
  }
  void TestStunToRelay(NATType ntype, ProtocolType proto) {
    nat_server1_ = CreateNatServer(kNatAddr1, ntype);
    auto port1 = CreateStunPort(kLocalAddr1, &nat_socket_factory1_);
    port1->SetIceRole(cricket::ICEROLE_CONTROLLING);
    auto port2 = CreateRelayPort(kLocalAddr2, proto, PROTO_UDP);
    port2->SetIceRole(cricket::ICEROLE_CONTROLLED);
    TestConnectivity(StunName(ntype), std::move(port1), RelayName(proto),
                     std::move(port2), false, ntype != NAT_SYMMETRIC, true,
                     true);
  }
  void TestTcpToTcp() {
    auto port1 = CreateTcpPort(kLocalAddr1);
    port1->SetIceRole(cricket::ICEROLE_CONTROLLING);
    auto port2 = CreateTcpPort(kLocalAddr2);
    port2->SetIceRole(cricket::ICEROLE_CONTROLLED);
    TestConnectivity("tcp", std::move(port1), "tcp", std::move(port2), true,
                     false, true, true);
  }
  void TestTcpToRelay(ProtocolType proto) {
    auto port1 = CreateTcpPort(kLocalAddr1);
    port1->SetIceRole(cricket::ICEROLE_CONTROLLING);
    auto port2 = CreateRelayPort(kLocalAddr2, proto, PROTO_TCP);
    port2->SetIceRole(cricket::ICEROLE_CONTROLLED);
    TestConnectivity("tcp", std::move(port1), RelayName(proto),
                     std::move(port2), false, false, true, true);
  }
  void TestSslTcpToRelay(ProtocolType proto) {
    auto port1 = CreateTcpPort(kLocalAddr1);
    port1->SetIceRole(cricket::ICEROLE_CONTROLLING);
    auto port2 = CreateRelayPort(kLocalAddr2, proto, PROTO_SSLTCP);
    port2->SetIceRole(cricket::ICEROLE_CONTROLLED);
    TestConnectivity("ssltcp", std::move(port1), RelayName(proto),
                     std::move(port2), false, false, true, true);
  }

  rtc::Network* MakeNetwork(const SocketAddress& addr) {
    networks_.emplace_back("unittest", "unittest", addr.ipaddr(), 32);
    networks_.back().AddIP(addr.ipaddr());
    return &networks_.back();
  }

  rtc::Network* MakeNetworkMultipleAddrs(const SocketAddress& global_addr,
                                         const SocketAddress& link_local_addr) {
    networks_.emplace_back("unittest", "unittest", global_addr.ipaddr(), 32,
                           rtc::ADAPTER_TYPE_UNKNOWN);
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
    auto port = UDPPort::Create(&main_, socket_factory, MakeNetwork(addr), 0, 0,
                                username_, password_, true, absl::nullopt,
                                &field_trials_);
    port->SetIceTiebreaker(kTiebreakerDefault);
    return port;
  }

  std::unique_ptr<UDPPort> CreateUdpPortMultipleAddrs(
      const SocketAddress& global_addr,
      const SocketAddress& link_local_addr,
      PacketSocketFactory* socket_factory) {
    auto port = UDPPort::Create(
        &main_, socket_factory,
        MakeNetworkMultipleAddrs(global_addr, link_local_addr), 0, 0, username_,
        password_, true, absl::nullopt, &field_trials_);
    port->SetIceTiebreaker(kTiebreakerDefault);
    return port;
  }
  std::unique_ptr<TCPPort> CreateTcpPort(const SocketAddress& addr) {
    return CreateTcpPort(addr, &socket_factory_);
  }
  std::unique_ptr<TCPPort> CreateTcpPort(const SocketAddress& addr,
                                         PacketSocketFactory* socket_factory) {
    auto port = TCPPort::Create(&main_, socket_factory, MakeNetwork(addr), 0, 0,
                                username_, password_, true, &field_trials_);
    port->SetIceTiebreaker(kTiebreakerDefault);
    return port;
  }
  std::unique_ptr<StunPort> CreateStunPort(const SocketAddress& addr,
                                           rtc::PacketSocketFactory* factory) {
    ServerAddresses stun_servers;
    stun_servers.insert(kStunAddr);
    auto port = StunPort::Create(&main_, factory, MakeNetwork(addr), 0, 0,
                                 username_, password_, stun_servers,
                                 absl::nullopt, &field_trials_);
    port->SetIceTiebreaker(kTiebreakerDefault);
    return port;
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
  std::unique_ptr<TurnPort> CreateTurnPort(
      const SocketAddress& addr,
      PacketSocketFactory* socket_factory,
      ProtocolType int_proto,
      ProtocolType ext_proto,
      const rtc::SocketAddress& server_addr) {
    RelayServerConfig config;
    config.credentials = kRelayCredentials;
    ProtocolAddress server_address(server_addr, int_proto);
    CreateRelayPortArgs args;
    args.network_thread = &main_;
    args.socket_factory = socket_factory;
    args.network = MakeNetwork(addr);
    args.username = username_;
    args.password = password_;
    args.server_address = &server_address;
    args.config = &config;
    args.field_trials = &field_trials_;

    auto port = TurnPort::Create(args, 0, 0);
    port->SetIceTiebreaker(kTiebreakerDefault);
    return port;
  }

  std::unique_ptr<rtc::NATServer> CreateNatServer(const SocketAddress& addr,
                                                  rtc::NATType type) {
    return std::make_unique<rtc::NATServer>(type, main_, ss_.get(), addr, addr,
                                            main_, ss_.get(), addr);
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
    EXPECT_TRUE_WAIT(ch1->conn()->connected(),
                     kDefaultTimeout);  // for TCP connect
    ch1->Ping();
    WAIT(!ch2->remote_address().IsNil(), kShortTimeout);

    // Send a ping from dst to src.
    ch2->AcceptConnection(GetCandidate(ch1->port()));
    ch2->Ping();
    EXPECT_EQ_WAIT(Connection::STATE_WRITABLE, ch2->conn()->write_state(),
                   kDefaultTimeout);
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
    EXPECT_TRUE_WAIT(!ch1->conn()->connected(), kDefaultTimeout);
    EXPECT_TRUE_WAIT(!ch2->conn()->connected(), kDefaultTimeout);

    // Ensure redundant SignalClose events on TcpConnection won't break tcp
    // reconnection. Chromium will fire SignalClose for all outstanding IPC
    // packets during reconnection.
    tcp_conn1->socket()->NotifyClosedForTest(0);
    tcp_conn2->socket()->NotifyClosedForTest(0);

    // Speed up destroying ch2's connection such that the test is ready to
    // accept a new connection from ch1 before ch1's connection destroys itself.
    ch2->Stop();
    EXPECT_TRUE_WAIT(ch2->conn() == NULL, kDefaultTimeout);
  }

  void TestTcpReconnect(bool ping_after_disconnected,
                        bool send_after_disconnected) {
    auto port1 = CreateTcpPort(kLocalAddr1);
    port1->SetIceRole(cricket::ICEROLE_CONTROLLING);
    auto port2 = CreateTcpPort(kLocalAddr2);
    port2->SetIceRole(cricket::ICEROLE_CONTROLLED);

    port1->set_component(cricket::ICE_CANDIDATE_COMPONENT_DEFAULT);
    port2->set_component(cricket::ICE_CANDIDATE_COMPONENT_DEFAULT);

    // Set up channels and ensure both ports will be deleted.
    TestChannel ch1(std::move(port1));
    TestChannel ch2(std::move(port2));
    EXPECT_EQ(0, ch1.complete_count());
    EXPECT_EQ(0, ch2.complete_count());

    ch1.Start();
    ch2.Start();
    ASSERT_EQ_WAIT(1, ch1.complete_count(), kDefaultTimeout);
    ASSERT_EQ_WAIT(1, ch2.complete_count(), kDefaultTimeout);

    // Initial connecting the channel, create connection on channel1.
    ch1.CreateConnection(GetCandidate(ch2.port()));
    ConnectStartedChannels(&ch1, &ch2);

    // Shorten the timeout period.
    const int kTcpReconnectTimeout = kDefaultTimeout;
    static_cast<TCPConnection*>(ch1.conn())
        ->set_reconnection_timeout(kTcpReconnectTimeout);
    static_cast<TCPConnection*>(ch2.conn())
        ->set_reconnection_timeout(kTcpReconnectTimeout);

    EXPECT_FALSE(ch1.connection_ready_to_send());
    EXPECT_FALSE(ch2.connection_ready_to_send());

    // Once connected, disconnect them.
    DisconnectTcpTestChannels(&ch1, &ch2);

    if (send_after_disconnected || ping_after_disconnected) {
      if (send_after_disconnected) {
        // First SendData after disconnect should fail but will trigger
        // reconnect.
        EXPECT_EQ(-1, ch1.SendData(data, static_cast<int>(strlen(data))));
      }

      if (ping_after_disconnected) {
        // Ping should trigger reconnect.
        ch1.Ping();
      }

      // Wait for channel's outgoing TCPConnection connected.
      EXPECT_TRUE_WAIT(ch1.conn()->connected(), kDefaultTimeout);

      // Verify that we could still connect channels.
      ConnectStartedChannels(&ch1, &ch2);
      EXPECT_TRUE_WAIT(ch1.connection_ready_to_send(), kTcpReconnectTimeout);
      // Channel2 is the passive one so a new connection is created during
      // reconnect. This new connection should never have issued ENOTCONN
      // hence the connection_ready_to_send() should be false.
      EXPECT_FALSE(ch2.connection_ready_to_send());
    } else {
      EXPECT_EQ(ch1.conn()->write_state(), Connection::STATE_WRITABLE);
      // Since the reconnection never happens, the connections should have been
      // destroyed after the timeout.
      EXPECT_TRUE_WAIT(!ch1.conn(), kTcpReconnectTimeout + kDefaultTimeout);
      EXPECT_TRUE(!ch2.conn());
    }

    // Tear down and ensure that goes smoothly.
    ch1.Stop();
    ch2.Stop();
    EXPECT_TRUE_WAIT(ch1.conn() == NULL, kDefaultTimeout);
    EXPECT_TRUE_WAIT(ch2.conn() == NULL, kDefaultTimeout);
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
      const rtc::SocketAddress& addr,
      absl::string_view username,
      absl::string_view password,
      const webrtc::FieldTrialsView* field_trials = nullptr) {
    auto port =
        std::make_unique<TestPort>(&main_, &socket_factory_, MakeNetwork(addr),
                                   0, 0, username, password, field_trials);
    port->SignalRoleConflict.connect(this, &PortTest::OnRoleConflict);
    return port;
  }
  std::unique_ptr<TestPort> CreateTestPort(const rtc::SocketAddress& addr,
                                           absl::string_view username,
                                           absl::string_view password,
                                           cricket::IceRole role,
                                           int tiebreaker) {
    auto port = CreateTestPort(addr, username, password);
    port->SetIceRole(role);
    port->SetIceTiebreaker(tiebreaker);
    return port;
  }
  // Overload to create a test port given an rtc::Network directly.
  std::unique_ptr<TestPort> CreateTestPort(const rtc::Network* network,
                                           absl::string_view username,
                                           absl::string_view password) {
    auto port = std::make_unique<TestPort>(&main_, &socket_factory_, network, 0,
                                           0, username, password);
    port->SignalRoleConflict.connect(this, &PortTest::OnRoleConflict);
    return port;
  }

  void OnRoleConflict(PortInterface* port) { role_conflict_ = true; }
  bool role_conflict() const { return role_conflict_; }

  void ConnectToSignalDestroyed(PortInterface* port) {
    port->SubscribePortDestroyed(
        [this](PortInterface* port) { OnDestroyed(port); });
  }

  void OnDestroyed(PortInterface* port) { ++ports_destroyed_; }
  int ports_destroyed() const { return ports_destroyed_; }

  rtc::BasicPacketSocketFactory* nat_socket_factory1() {
    return &nat_socket_factory1_;
  }

  rtc::VirtualSocketServer* vss() { return ss_.get(); }

 private:
  // When a "create port" helper method is called with an IP, we create a
  // Network with that IP and add it to this list. Using a list instead of a
  // vector so that when it grows, pointers aren't invalidated.
  std::list<rtc::Network> networks_;
  std::unique_ptr<rtc::VirtualSocketServer> ss_;
  rtc::AutoSocketServerThread main_;
  rtc::BasicPacketSocketFactory socket_factory_;
  std::unique_ptr<rtc::NATServer> nat_server1_;
  std::unique_ptr<rtc::NATServer> nat_server2_;
  rtc::NATSocketFactory nat_factory1_;
  rtc::NATSocketFactory nat_factory2_;
  rtc::BasicPacketSocketFactory nat_socket_factory1_;
  rtc::BasicPacketSocketFactory nat_socket_factory2_;
  TestStunServer::StunServerPtr stun_server_;
  TestTurnServer turn_server_;
  std::string username_;
  std::string password_;
  bool role_conflict_;
  int ports_destroyed_;
  webrtc::test::ScopedKeyValueConfig field_trials_;
};

void PortTest::TestConnectivity(absl::string_view name1,
                                std::unique_ptr<Port> port1,
                                absl::string_view name2,
                                std::unique_ptr<Port> port2,
                                bool accept,
                                bool same_addr1,
                                bool same_addr2,
                                bool possible) {
  rtc::ScopedFakeClock clock;
  RTC_LOG(LS_INFO) << "Test: " << name1 << " to " << name2 << ": ";
  port1->set_component(cricket::ICE_CANDIDATE_COMPONENT_DEFAULT);
  port2->set_component(cricket::ICE_CANDIDATE_COMPONENT_DEFAULT);

  // Set up channels and ensure both ports will be deleted.
  TestChannel ch1(std::move(port1));
  TestChannel ch2(std::move(port2));
  EXPECT_EQ(0, ch1.complete_count());
  EXPECT_EQ(0, ch2.complete_count());

  // Acquire addresses.
  ch1.Start();
  ch2.Start();
  ASSERT_EQ_SIMULATED_WAIT(1, ch1.complete_count(), kDefaultTimeout, clock);
  ASSERT_EQ_SIMULATED_WAIT(1, ch2.complete_count(), kDefaultTimeout, clock);

  // Send a ping from src to dst. This may or may not make it.
  ch1.CreateConnection(GetCandidate(ch2.port()));
  ASSERT_TRUE(ch1.conn() != NULL);
  EXPECT_TRUE_SIMULATED_WAIT(ch1.conn()->connected(), kDefaultTimeout,
                             clock);  // for TCP connect
  ch1.Ping();
  SIMULATED_WAIT(!ch2.remote_address().IsNil(), kShortTimeout, clock);

  if (accept) {
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
    ASSERT_TRUE(ch2.conn() != NULL);
    ch2.Ping();
    EXPECT_EQ_SIMULATED_WAIT(Connection::STATE_WRITABLE,
                             ch2.conn()->write_state(), kDefaultTimeout, clock);
  } else {
    // We can't send a ping from src to dst, so flip it around. This will happen
    // when the destination NAT is addr/port restricted or symmetric.
    EXPECT_TRUE(ch1.remote_address().IsNil());
    EXPECT_TRUE(ch2.remote_address().IsNil());

    // Send a ping from dst to src. Again, this may or may not make it.
    ch2.CreateConnection(GetCandidate(ch1.port()));
    ASSERT_TRUE(ch2.conn() != NULL);
    ch2.Ping();
    SIMULATED_WAIT(ch2.conn()->write_state() == Connection::STATE_WRITABLE,
                   kShortTimeout, clock);

    if (same_addr1 && same_addr2) {
      // The new ping got back to the source.
      EXPECT_TRUE(ch1.conn()->receiving());
      EXPECT_EQ(Connection::STATE_WRITABLE, ch2.conn()->write_state());

      // First connection may not be writable if the first ping did not get
      // through.  So we will have to do another.
      if (ch1.conn()->write_state() == Connection::STATE_WRITE_INIT) {
        ch1.Ping();
        EXPECT_EQ_SIMULATED_WAIT(Connection::STATE_WRITABLE,
                                 ch1.conn()->write_state(), kDefaultTimeout,
                                 clock);
      }
    } else if (!same_addr1 && possible) {
      // The new ping went to the candidate address, but that address was bad.
      // This will happen when the source NAT is symmetric.
      EXPECT_TRUE(ch1.remote_address().IsNil());
      EXPECT_TRUE(ch2.remote_address().IsNil());

      // However, since we have now sent a ping to the source IP, we should be
      // able to get a ping from it. This gives us the real source address.
      ch1.Ping();
      EXPECT_TRUE_SIMULATED_WAIT(!ch2.remote_address().IsNil(), kDefaultTimeout,
                                 clock);
      EXPECT_FALSE(ch2.conn()->receiving());
      EXPECT_TRUE(ch1.remote_address().IsNil());

      // Pick up the actual address and establish the connection.
      ch2.AcceptConnection(GetCandidate(ch1.port()));
      ASSERT_TRUE(ch2.conn() != NULL);
      ch2.Ping();
      EXPECT_EQ_SIMULATED_WAIT(Connection::STATE_WRITABLE,
                               ch2.conn()->write_state(), kDefaultTimeout,
                               clock);
    } else if (!same_addr2 && possible) {
      // The new ping came in, but from an unexpected address. This will happen
      // when the destination NAT is symmetric.
      EXPECT_FALSE(ch1.remote_address().IsNil());
      EXPECT_FALSE(ch1.conn()->receiving());

      // Update our address and complete the connection.
      ch1.AcceptConnection(GetCandidate(ch2.port()));
      ch1.Ping();
      EXPECT_EQ_SIMULATED_WAIT(Connection::STATE_WRITABLE,
                               ch1.conn()->write_state(), kDefaultTimeout,
                               clock);
    } else {  // (!possible)
      // There should be s no way for the pings to reach each other. Check it.
      EXPECT_TRUE(ch1.remote_address().IsNil());
      EXPECT_TRUE(ch2.remote_address().IsNil());
      ch1.Ping();
      SIMULATED_WAIT(!ch2.remote_address().IsNil(), kShortTimeout, clock);
      EXPECT_TRUE(ch1.remote_address().IsNil());
      EXPECT_TRUE(ch2.remote_address().IsNil());
    }
  }

  // Everything should be good, unless we know the situation is impossible.
  ASSERT_TRUE(ch1.conn() != NULL);
  ASSERT_TRUE(ch2.conn() != NULL);
  if (possible) {
    EXPECT_TRUE(ch1.conn()->receiving());
    EXPECT_EQ(Connection::STATE_WRITABLE, ch1.conn()->write_state());
    EXPECT_TRUE(ch2.conn()->receiving());
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
  EXPECT_TRUE_SIMULATED_WAIT(ch1.conn() == NULL, kDefaultTimeout, clock);
  EXPECT_TRUE_SIMULATED_WAIT(ch2.conn() == NULL, kDefaultTimeout, clock);
}

class FakePacketSocketFactory : public rtc::PacketSocketFactory {
 public:
  FakePacketSocketFactory()
      : next_udp_socket_(NULL), next_server_tcp_socket_(NULL) {}
  ~FakePacketSocketFactory() override {}

  AsyncPacketSocket* CreateUdpSocket(const SocketAddress& address,
                                     uint16_t min_port,
                                     uint16_t max_port) override {
    EXPECT_TRUE(next_udp_socket_ != NULL);
    AsyncPacketSocket* result = next_udp_socket_;
    next_udp_socket_ = NULL;
    return result;
  }

  AsyncListenSocket* CreateServerTcpSocket(const SocketAddress& local_address,
                                           uint16_t min_port,
                                           uint16_t max_port,
                                           int opts) override {
    EXPECT_TRUE(next_server_tcp_socket_ != NULL);
    AsyncListenSocket* result = next_server_tcp_socket_;
    next_server_tcp_socket_ = NULL;
    return result;
  }

  AsyncPacketSocket* CreateClientTcpSocket(
      const SocketAddress& local_address,
      const SocketAddress& remote_address,
      const rtc::ProxyInfo& proxy_info,
      const std::string& user_agent,
      const rtc::PacketSocketTcpOptions& opts) override {
    EXPECT_TRUE(next_client_tcp_socket_.has_value());
    AsyncPacketSocket* result = *next_client_tcp_socket_;
    next_client_tcp_socket_ = nullptr;
    return result;
  }

  void set_next_udp_socket(AsyncPacketSocket* next_udp_socket) {
    next_udp_socket_ = next_udp_socket;
  }
  void set_next_server_tcp_socket(AsyncListenSocket* next_server_tcp_socket) {
    next_server_tcp_socket_ = next_server_tcp_socket;
  }
  void set_next_client_tcp_socket(AsyncPacketSocket* next_client_tcp_socket) {
    next_client_tcp_socket_ = next_client_tcp_socket;
  }
  std::unique_ptr<webrtc::AsyncDnsResolverInterface> CreateAsyncDnsResolver()
      override {
    return nullptr;
  }

 private:
  AsyncPacketSocket* next_udp_socket_;
  AsyncListenSocket* next_server_tcp_socket_;
  absl::optional<AsyncPacketSocket*> next_client_tcp_socket_;
};

class FakeAsyncPacketSocket : public AsyncPacketSocket {
 public:
  // Returns current local address. Address may be set to NULL if the
  // socket is not bound yet (GetState() returns STATE_BINDING).
  virtual SocketAddress GetLocalAddress() const { return local_address_; }

  // Returns remote address. Returns zeroes if this is not a client TCP socket.
  virtual SocketAddress GetRemoteAddress() const { return remote_address_; }

  // Send a packet.
  virtual int Send(const void* pv,
                   size_t cb,
                   const rtc::PacketOptions& options) {
    if (error_ == 0) {
      return static_cast<int>(cb);
    } else {
      return -1;
    }
  }
  virtual int SendTo(const void* pv,
                     size_t cb,
                     const SocketAddress& addr,
                     const rtc::PacketOptions& options) {
    if (error_ == 0) {
      return static_cast<int>(cb);
    } else {
      return -1;
    }
  }
  virtual int Close() { return 0; }

  virtual State GetState() const { return state_; }
  virtual int GetOption(Socket::Option opt, int* value) { return 0; }
  virtual int SetOption(Socket::Option opt, int value) { return 0; }
  virtual int GetError() const { return 0; }
  virtual void SetError(int error) { error_ = error; }

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
  virtual SocketAddress GetLocalAddress() const { return local_address_; }
  void Bind(const SocketAddress& address) {
    local_address_ = address;
    state_ = State::kBound;
  }
  virtual int GetOption(Socket::Option opt, int* value) { return 0; }
  virtual int SetOption(Socket::Option opt, int value) { return 0; }
  virtual State GetState() const { return state_; }

 private:
  SocketAddress local_address_;
  State state_ = State::kClosed;
};

// Local -> XXXX
TEST_F(PortTest, TestLocalToLocal) {
  TestLocalToLocal();
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
  port1->SetIceRole(cricket::ICEROLE_CONTROLLING);
  port1->set_component(cricket::ICE_CANDIDATE_COMPONENT_DEFAULT);

  // Set up a channel and ensure the port will be deleted.
  TestChannel ch1(std::move(port1));
  EXPECT_EQ(0, ch1.complete_count());

  ch1.Start();
  ASSERT_EQ_WAIT(1, ch1.complete_count(), kDefaultTimeout);

  std::unique_ptr<rtc::Socket> server(
      vss()->CreateSocket(kLocalAddr2.family(), SOCK_STREAM));
  // Bind but not listen.
  EXPECT_EQ(0, server->Bind(kLocalAddr2));

  Candidate c = GetCandidate(ch1.port());
  c.set_address(server->GetLocalAddress());

  ch1.CreateConnection(c);
  EXPECT_TRUE(ch1.conn());
  EXPECT_TRUE_WAIT(!ch1.conn(), kDefaultTimeout);  // for TCP connect
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
// i) it has never received anything for MIN_CONNECTION_LIFETIME milliseconds
//    since it was created, or
// ii) it has not received anything for DEAD_CONNECTION_RECEIVE_TIMEOUT
//     milliseconds since last receiving.
TEST_F(PortTest, TestConnectionDead) {
  TestChannel ch1(CreateUdpPort(kLocalAddr1));
  TestChannel ch2(CreateUdpPort(kLocalAddr2));
  // Acquire address.
  ch1.Start();
  ch2.Start();
  ASSERT_EQ_WAIT(1, ch1.complete_count(), kDefaultTimeout);
  ASSERT_EQ_WAIT(1, ch2.complete_count(), kDefaultTimeout);

  // Test case that the connection has never received anything.
  int64_t before_created = rtc::TimeMillis();
  ch1.CreateConnection(GetCandidate(ch2.port()));
  int64_t after_created = rtc::TimeMillis();
  Connection* conn = ch1.conn();
  ASSERT_NE(conn, nullptr);
  // It is not dead if it is after MIN_CONNECTION_LIFETIME but not pruned.
  conn->UpdateState(after_created + MIN_CONNECTION_LIFETIME + 1);
  rtc::Thread::Current()->ProcessMessages(0);
  EXPECT_TRUE(ch1.conn() != nullptr);
  // It is not dead if it is before MIN_CONNECTION_LIFETIME and pruned.
  conn->UpdateState(before_created + MIN_CONNECTION_LIFETIME - 1);
  conn->Prune();
  rtc::Thread::Current()->ProcessMessages(0);
  EXPECT_TRUE(ch1.conn() != nullptr);
  // It will be dead after MIN_CONNECTION_LIFETIME and pruned.
  conn->UpdateState(after_created + MIN_CONNECTION_LIFETIME + 1);
  EXPECT_TRUE_WAIT(ch1.conn() == nullptr, kDefaultTimeout);

  // Test case that the connection has received something.
  // Create a connection again and receive a ping.
  ch1.CreateConnection(GetCandidate(ch2.port()));
  conn = ch1.conn();
  ASSERT_NE(conn, nullptr);
  int64_t before_last_receiving = rtc::TimeMillis();
  conn->ReceivedPing();
  int64_t after_last_receiving = rtc::TimeMillis();
  // The connection will be dead after DEAD_CONNECTION_RECEIVE_TIMEOUT
  conn->UpdateState(before_last_receiving + DEAD_CONNECTION_RECEIVE_TIMEOUT -
                    1);
  rtc::Thread::Current()->ProcessMessages(100);
  EXPECT_TRUE(ch1.conn() != nullptr);
  conn->UpdateState(after_last_receiving + DEAD_CONNECTION_RECEIVE_TIMEOUT + 1);
  EXPECT_TRUE_WAIT(ch1.conn() == nullptr, kDefaultTimeout);
}

TEST_F(PortTest, TestConnectionDeadWithDeadConnectionTimeout) {
  TestChannel ch1(CreateUdpPort(kLocalAddr1));
  TestChannel ch2(CreateUdpPort(kLocalAddr2));
  // Acquire address.
  ch1.Start();
  ch2.Start();
  ASSERT_EQ_WAIT(1, ch1.complete_count(), kDefaultTimeout);
  ASSERT_EQ_WAIT(1, ch2.complete_count(), kDefaultTimeout);

  // Note: set field trials manually since they are parsed by
  // P2PTransportChannel but P2PTransportChannel is not used in this test.
  IceFieldTrials field_trials;
  field_trials.dead_connection_timeout_ms = 90000;

  // Create a connection again and receive a ping.
  ch1.CreateConnection(GetCandidate(ch2.port()));
  auto conn = ch1.conn();
  conn->SetIceFieldTrials(&field_trials);

  ASSERT_NE(conn, nullptr);
  int64_t before_last_receiving = rtc::TimeMillis();
  conn->ReceivedPing();
  int64_t after_last_receiving = rtc::TimeMillis();
  // The connection will be dead after 90s
  conn->UpdateState(before_last_receiving + 90000 - 1);
  rtc::Thread::Current()->ProcessMessages(100);
  EXPECT_TRUE(ch1.conn() != nullptr);
  conn->UpdateState(after_last_receiving + 90000 + 1);
  EXPECT_TRUE_WAIT(ch1.conn() == nullptr, kDefaultTimeout);
}

TEST_F(PortTest, TestConnectionDeadOutstandingPing) {
  auto port1 = CreateUdpPort(kLocalAddr1);
  port1->SetIceRole(cricket::ICEROLE_CONTROLLING);
  port1->SetIceTiebreaker(kTiebreaker1);
  auto port2 = CreateUdpPort(kLocalAddr2);
  port2->SetIceRole(cricket::ICEROLE_CONTROLLED);
  port2->SetIceTiebreaker(kTiebreaker2);

  TestChannel ch1(std::move(port1));
  TestChannel ch2(std::move(port2));
  // Acquire address.
  ch1.Start();
  ch2.Start();
  ASSERT_EQ_WAIT(1, ch1.complete_count(), kDefaultTimeout);
  ASSERT_EQ_WAIT(1, ch2.complete_count(), kDefaultTimeout);

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
  int64_t send_ping_timestamp = rtc::TimeMillis();
  conn->Ping(send_ping_timestamp);

  // The connection will be dead 30s after the ping was sent.
  conn->UpdateState(send_ping_timestamp + DEAD_CONNECTION_RECEIVE_TIMEOUT - 1);
  rtc::Thread::Current()->ProcessMessages(100);
  EXPECT_TRUE(ch1.conn() != nullptr);
  conn->UpdateState(send_ping_timestamp + DEAD_CONNECTION_RECEIVE_TIMEOUT + 1);
  EXPECT_TRUE_WAIT(ch1.conn() == nullptr, kDefaultTimeout);
}

// This test case verifies standard ICE features in STUN messages. Currently it
// verifies Message Integrity attribute in STUN messages and username in STUN
// binding request will have colon (":") between remote and local username.
TEST_F(PortTest, TestLocalToLocalStandard) {
  auto port1 = CreateUdpPort(kLocalAddr1);
  port1->SetIceRole(cricket::ICEROLE_CONTROLLING);
  port1->SetIceTiebreaker(kTiebreaker1);
  auto port2 = CreateUdpPort(kLocalAddr2);
  port2->SetIceRole(cricket::ICEROLE_CONTROLLED);
  port2->SetIceTiebreaker(kTiebreaker2);
  // Same parameters as TestLocalToLocal above.
  TestConnectivity("udp", std::move(port1), "udp", std::move(port2), true, true,
                   true, true);
}

// This test is trying to validate a successful and failure scenario in a
// loopback test when protocol is RFC5245. For success IceTiebreaker, username
// should remain equal to the request generated by the port and role of port
// must be in controlling.
TEST_F(PortTest, TestLoopbackCall) {
  auto lport = CreateTestPort(kLocalAddr1, "lfrag", "lpass");
  lport->SetIceRole(cricket::ICEROLE_CONTROLLING);
  lport->SetIceTiebreaker(kTiebreaker1);
  lport->PrepareAddress();
  ASSERT_FALSE(lport->Candidates().empty());
  Connection* conn =
      lport->CreateConnection(lport->Candidates()[0], Port::ORIGIN_MESSAGE);
  conn->Ping(0);

  ASSERT_TRUE_WAIT(lport->last_stun_msg() != NULL, kDefaultTimeout);
  IceMessage* msg = lport->last_stun_msg();
  EXPECT_EQ(STUN_BINDING_REQUEST, msg->type());
  conn->OnReadPacket(rtc::ReceivedPacket(lport->last_stun_buf(),
                                         rtc::SocketAddress(), absl::nullopt));
  ASSERT_TRUE_WAIT(lport->last_stun_msg() != NULL, kDefaultTimeout);
  msg = lport->last_stun_msg();
  EXPECT_EQ(STUN_BINDING_RESPONSE, msg->type());

  // If the tiebreaker value is different from port, we expect a error
  // response.
  lport->Reset();
  lport->AddCandidateAddress(kLocalAddr2);
  // Creating a different connection as `conn` is receiving.
  Connection* conn1 =
      lport->CreateConnection(lport->Candidates()[1], Port::ORIGIN_MESSAGE);
  conn1->Ping(0);

  ASSERT_TRUE_WAIT(lport->last_stun_msg() != NULL, kDefaultTimeout);
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
      STUN_ATTR_ICE_CONTROLLING, kTiebreaker1 - 1));
  modified_req->AddMessageIntegrity("lpass");
  modified_req->AddFingerprint();

  lport->Reset();
  auto buf = std::make_unique<ByteBufferWriter>();
  WriteStunMessage(*modified_req, buf.get());
  conn1->OnReadPacket(rtc::ReceivedPacket::CreateFromLegacy(
      reinterpret_cast<const char*>(buf->Data()), buf->Length(),
      /*packet_time_us=*/-1));
  ASSERT_TRUE_WAIT(lport->last_stun_msg() != NULL, kDefaultTimeout);
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
  lport->SetIceRole(cricket::ICEROLE_CONTROLLING);
  lport->SetIceTiebreaker(kTiebreaker1);
  auto rport = CreateTestPort(kLocalAddr2, "rfrag", "rpass");
  rport->SetIceRole(cricket::ICEROLE_CONTROLLING);
  rport->SetIceTiebreaker(kTiebreaker2);

  lport->PrepareAddress();
  rport->PrepareAddress();
  ASSERT_FALSE(lport->Candidates().empty());
  ASSERT_FALSE(rport->Candidates().empty());
  Connection* lconn =
      lport->CreateConnection(rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  Connection* rconn =
      rport->CreateConnection(lport->Candidates()[0], Port::ORIGIN_MESSAGE);
  rconn->Ping(0);

  ASSERT_TRUE_WAIT(rport->last_stun_msg() != NULL, kDefaultTimeout);
  IceMessage* msg = rport->last_stun_msg();
  EXPECT_EQ(STUN_BINDING_REQUEST, msg->type());
  // Send rport binding request to lport.
  lconn->OnReadPacket(rtc::ReceivedPacket(rport->last_stun_buf(),
                                          rtc::SocketAddress(), absl::nullopt));

  ASSERT_TRUE_WAIT(lport->last_stun_msg() != NULL, kDefaultTimeout);
  EXPECT_EQ(STUN_BINDING_RESPONSE, lport->last_stun_msg()->type());
  EXPECT_TRUE(role_conflict());
}

TEST_F(PortTest, TestTcpNoDelay) {
  rtc::ScopedFakeClock clock;
  auto port1 = CreateTcpPort(kLocalAddr1);
  port1->SetIceRole(cricket::ICEROLE_CONTROLLING);
  int option_value = -1;
  int success = port1->GetOption(rtc::Socket::OPT_NODELAY, &option_value);
  ASSERT_EQ(0, success);  // GetOption() should complete successfully w/ 0
  EXPECT_EQ(1, option_value);

  auto port2 = CreateTcpPort(kLocalAddr2);
  port2->SetIceRole(cricket::ICEROLE_CONTROLLED);

  // Set up a connection, and verify that option is set on connected sockets at
  // both ends.
  TestChannel ch1(std::move(port1));
  TestChannel ch2(std::move(port2));
  // Acquire addresses.
  ch1.Start();
  ch2.Start();
  ASSERT_EQ_SIMULATED_WAIT(1, ch1.complete_count(), kDefaultTimeout, clock);
  ASSERT_EQ_SIMULATED_WAIT(1, ch2.complete_count(), kDefaultTimeout, clock);
  // Connect and send a ping from src to dst.
  ch1.CreateConnection(GetCandidate(ch2.port()));
  ASSERT_TRUE(ch1.conn() != NULL);
  EXPECT_TRUE_SIMULATED_WAIT(ch1.conn()->connected(), kDefaultTimeout,
                             clock);  // for TCP connect
  ch1.Ping();
  SIMULATED_WAIT(!ch2.remote_address().IsNil(), kShortTimeout, clock);

  // Accept the connection.
  ch2.AcceptConnection(GetCandidate(ch1.port()));
  ASSERT_TRUE(ch2.conn() != NULL);

  option_value = -1;
  success = static_cast<TCPConnection*>(ch1.conn())
                ->socket()
                ->GetOption(rtc::Socket::OPT_NODELAY, &option_value);
  ASSERT_EQ(0, success);
  EXPECT_EQ(1, option_value);

  option_value = -1;
  success = static_cast<TCPConnection*>(ch2.conn())
                ->socket()
                ->GetOption(rtc::Socket::OPT_NODELAY, &option_value);
  ASSERT_EQ(0, success);
  EXPECT_EQ(1, option_value);
}

TEST_F(PortTest, TestDelayedBindingUdp) {
  FakeAsyncPacketSocket* socket = new FakeAsyncPacketSocket();
  FakePacketSocketFactory socket_factory;

  socket_factory.set_next_udp_socket(socket);
  auto port = CreateUdpPort(kLocalAddr1, &socket_factory);

  socket->set_state(AsyncPacketSocket::STATE_BINDING);
  port->PrepareAddress();

  EXPECT_EQ(0U, port->Candidates().size());
  socket->SignalAddressReady(socket, kLocalAddr2);

  EXPECT_EQ(1U, port->Candidates().size());
}

TEST_F(PortTest, TestDisableInterfaceOfTcpPort) {
  FakeAsyncListenSocket* lsocket = new FakeAsyncListenSocket();
  FakeAsyncListenSocket* rsocket = new FakeAsyncListenSocket();
  FakePacketSocketFactory socket_factory;

  socket_factory.set_next_server_tcp_socket(lsocket);
  auto lport = CreateTcpPort(kLocalAddr1, &socket_factory);

  socket_factory.set_next_server_tcp_socket(rsocket);
  auto rport = CreateTcpPort(kLocalAddr2, &socket_factory);

  lsocket->Bind(kLocalAddr1);
  rsocket->Bind(kLocalAddr2);

  lport->SetIceRole(cricket::ICEROLE_CONTROLLING);
  lport->SetIceTiebreaker(kTiebreaker1);
  rport->SetIceRole(cricket::ICEROLE_CONTROLLED);
  rport->SetIceTiebreaker(kTiebreaker2);

  lport->PrepareAddress();
  rport->PrepareAddress();
  ASSERT_FALSE(rport->Candidates().empty());

  // A client socket.
  FakeAsyncPacketSocket* socket = new FakeAsyncPacketSocket();
  socket->local_address_ = kLocalAddr1;
  socket->remote_address_ = kLocalAddr2;
  socket_factory.set_next_client_tcp_socket(socket);
  Connection* lconn =
      lport->CreateConnection(rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  ASSERT_NE(lconn, nullptr);
  socket->SignalConnect(socket);
  lconn->Ping(0);

  // Now disconnect the client socket...
  socket->NotifyClosedForTest(1);

  // And prevent new sockets from being created.
  socket_factory.set_next_client_tcp_socket(nullptr);

  // Test that Ping() does not cause SEGV.
  lconn->Ping(0);
}

void PortTest::TestCrossFamilyPorts(int type) {
  FakePacketSocketFactory factory;
  std::unique_ptr<Port> ports[4];
  SocketAddress addresses[4] = {
      SocketAddress("192.168.1.3", 0), SocketAddress("192.168.1.4", 0),
      SocketAddress("2001:db8::1", 0), SocketAddress("2001:db8::2", 0)};
  for (int i = 0; i < 4; i++) {
    if (type == SOCK_DGRAM) {
      FakeAsyncPacketSocket* socket = new FakeAsyncPacketSocket();
      factory.set_next_udp_socket(socket);
      ports[i] = CreateUdpPort(addresses[i], &factory);
      socket->set_state(AsyncPacketSocket::STATE_BINDING);
      socket->SignalAddressReady(socket, addresses[i]);
    } else if (type == SOCK_STREAM) {
      FakeAsyncListenSocket* socket = new FakeAsyncListenSocket();
      factory.set_next_server_tcp_socket(socket);
      ports[i] = CreateTcpPort(addresses[i], &factory);
      socket->Bind(addresses[i]);
    }
    ports[i]->PrepareAddress();
  }

  // IPv4 Port, connects to IPv6 candidate and then to IPv4 candidate.
  if (type == SOCK_STREAM) {
    FakeAsyncPacketSocket* clientsocket = new FakeAsyncPacketSocket();
    factory.set_next_client_tcp_socket(clientsocket);
  }
  Connection* c = ports[0]->CreateConnection(GetCandidate(ports[2].get()),
                                             Port::ORIGIN_MESSAGE);
  EXPECT_TRUE(NULL == c);
  EXPECT_EQ(0U, ports[0]->connections().size());
  c = ports[0]->CreateConnection(GetCandidate(ports[1].get()),
                                 Port::ORIGIN_MESSAGE);
  EXPECT_FALSE(NULL == c);
  EXPECT_EQ(1U, ports[0]->connections().size());

  // IPv6 Port, connects to IPv4 candidate and to IPv6 candidate.
  if (type == SOCK_STREAM) {
    FakeAsyncPacketSocket* clientsocket = new FakeAsyncPacketSocket();
    factory.set_next_client_tcp_socket(clientsocket);
  }
  c = ports[2]->CreateConnection(GetCandidate(ports[0].get()),
                                 Port::ORIGIN_MESSAGE);
  EXPECT_TRUE(NULL == c);
  EXPECT_EQ(0U, ports[2]->connections().size());
  c = ports[2]->CreateConnection(GetCandidate(ports[3].get()),
                                 Port::ORIGIN_MESSAGE);
  EXPECT_FALSE(NULL == c);
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
    EXPECT_FALSE(NULL == c);
    EXPECT_EQ(1U, p1->connections().size());
  } else {
    EXPECT_TRUE(NULL == c);
    EXPECT_EQ(0U, p1->connections().size());
  }
}

TEST_F(PortTest, TestUdpSingleAddressV6CrossTypePorts) {
  FakePacketSocketFactory factory;
  std::unique_ptr<Port> ports[4];
  SocketAddress addresses[4] = {
      SocketAddress("2001:db8::1", 0), SocketAddress("fe80::1", 0),
      SocketAddress("fe80::2", 0), SocketAddress("::1", 0)};
  for (int i = 0; i < 4; i++) {
    FakeAsyncPacketSocket* socket = new FakeAsyncPacketSocket();
    factory.set_next_udp_socket(socket);
    ports[i] = CreateUdpPort(addresses[i], &factory);
    socket->set_state(AsyncPacketSocket::STATE_BINDING);
    socket->SignalAddressReady(socket, addresses[i]);
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
  FakePacketSocketFactory factory;
  std::unique_ptr<Port> ports[5];
  SocketAddress addresses[5] = {
      SocketAddress("2001:db8::1", 0), SocketAddress("2001:db8::2", 0),
      SocketAddress("fe80::1", 0), SocketAddress("fe80::2", 0),
      SocketAddress("::1", 0)};
  for (int i = 0; i < 5; i++) {
    FakeAsyncPacketSocket* socket = new FakeAsyncPacketSocket();
    factory.set_next_udp_socket(socket);
    ports[i] =
        CreateUdpPortMultipleAddrs(addresses[i], kLinkLocalIPv6Addr, &factory);
    ports[i]->SetIceTiebreaker(kTiebreakerDefault);
    socket->set_state(AsyncPacketSocket::STATE_BINDING);
    socket->SignalAddressReady(socket, addresses[i]);
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
  EXPECT_EQ(0, udpport->SetOption(rtc::Socket::OPT_DSCP, rtc::DSCP_CS6));
  EXPECT_EQ(0, udpport->GetOption(rtc::Socket::OPT_DSCP, &dscp));
  auto tcpport = CreateTcpPort(kLocalAddr1);
  EXPECT_EQ(0, tcpport->SetOption(rtc::Socket::OPT_DSCP, rtc::DSCP_AF31));
  EXPECT_EQ(0, tcpport->GetOption(rtc::Socket::OPT_DSCP, &dscp));
  EXPECT_EQ(rtc::DSCP_AF31, dscp);
  auto stunport = CreateStunPort(kLocalAddr1, nat_socket_factory1());
  EXPECT_EQ(0, stunport->SetOption(rtc::Socket::OPT_DSCP, rtc::DSCP_AF41));
  EXPECT_EQ(0, stunport->GetOption(rtc::Socket::OPT_DSCP, &dscp));
  EXPECT_EQ(rtc::DSCP_AF41, dscp);
  auto turnport1 =
      CreateTurnPort(kLocalAddr1, nat_socket_factory1(), PROTO_UDP, PROTO_UDP);
  // Socket is created in PrepareAddress.
  turnport1->PrepareAddress();
  EXPECT_EQ(0, turnport1->SetOption(rtc::Socket::OPT_DSCP, rtc::DSCP_CS7));
  EXPECT_EQ(0, turnport1->GetOption(rtc::Socket::OPT_DSCP, &dscp));
  EXPECT_EQ(rtc::DSCP_CS7, dscp);
  // This will verify correct value returned without the socket.
  auto turnport2 =
      CreateTurnPort(kLocalAddr1, nat_socket_factory1(), PROTO_UDP, PROTO_UDP);
  EXPECT_EQ(0, turnport2->SetOption(rtc::Socket::OPT_DSCP, rtc::DSCP_CS6));
  EXPECT_EQ(0, turnport2->GetOption(rtc::Socket::OPT_DSCP, &dscp));
  EXPECT_EQ(rtc::DSCP_CS6, dscp);
}

// Test sending STUN messages.
TEST_F(PortTest, TestSendStunMessage) {
  auto lport = CreateTestPort(kLocalAddr1, "lfrag", "lpass");
  auto rport = CreateTestPort(kLocalAddr2, "rfrag", "rpass");
  lport->SetIceRole(cricket::ICEROLE_CONTROLLING);
  lport->SetIceTiebreaker(kTiebreaker1);
  rport->SetIceRole(cricket::ICEROLE_CONTROLLED);
  rport->SetIceTiebreaker(kTiebreaker2);

  // Send a fake ping from lport to rport.
  lport->PrepareAddress();
  rport->PrepareAddress();
  ASSERT_FALSE(rport->Candidates().empty());
  Connection* lconn =
      lport->CreateConnection(rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  Connection* rconn =
      rport->CreateConnection(lport->Candidates()[0], Port::ORIGIN_MESSAGE);
  lconn->Ping(0);

  // Check that it's a proper BINDING-REQUEST.
  ASSERT_TRUE_WAIT(lport->last_stun_msg() != NULL, kDefaultTimeout);
  IceMessage* msg = lport->last_stun_msg();
  EXPECT_EQ(STUN_BINDING_REQUEST, msg->type());
  EXPECT_FALSE(msg->IsLegacy());
  const StunByteStringAttribute* username_attr =
      msg->GetByteString(STUN_ATTR_USERNAME);
  ASSERT_TRUE(username_attr != NULL);
  const StunUInt32Attribute* priority_attr = msg->GetUInt32(STUN_ATTR_PRIORITY);
  ASSERT_TRUE(priority_attr != NULL);
  EXPECT_EQ(kDefaultPrflxPriority, priority_attr->value());
  EXPECT_EQ("rfrag:lfrag", username_attr->string_view());
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_MESSAGE_INTEGRITY) != NULL);
  EXPECT_EQ(StunMessage::IntegrityStatus::kIntegrityOk,
            msg->ValidateMessageIntegrity("rpass"));
  const StunUInt64Attribute* ice_controlling_attr =
      msg->GetUInt64(STUN_ATTR_ICE_CONTROLLING);
  ASSERT_TRUE(ice_controlling_attr != NULL);
  EXPECT_EQ(lport->IceTiebreaker(), ice_controlling_attr->value());
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_ICE_CONTROLLED) == NULL);
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_USE_CANDIDATE) != NULL);
  EXPECT_TRUE(msg->GetUInt32(STUN_ATTR_FINGERPRINT) != NULL);
  EXPECT_TRUE(StunMessage::ValidateFingerprint(
      reinterpret_cast<const char*>(lport->last_stun_buf().data()),
      lport->last_stun_buf().size()));

  // Request should not include ping count.
  ASSERT_TRUE(msg->GetUInt32(STUN_ATTR_RETRANSMIT_COUNT) == NULL);

  // Save a copy of the BINDING-REQUEST for use below.
  std::unique_ptr<IceMessage> request = CopyStunMessage(*msg);

  // Receive the BINDING-REQUEST and respond with BINDING-RESPONSE.
  rconn->OnReadPacket(rtc::ReceivedPacket(lport->last_stun_buf(),
                                          rtc::SocketAddress(), absl::nullopt));
  msg = rport->last_stun_msg();
  ASSERT_TRUE(msg != NULL);
  EXPECT_EQ(STUN_BINDING_RESPONSE, msg->type());
  // Received a BINDING-RESPONSE.
  lconn->OnReadPacket(rtc::ReceivedPacket(rport->last_stun_buf(),
                                          rtc::SocketAddress(), absl::nullopt));

  // Verify the STUN Stats.
  EXPECT_EQ(1U, lconn->stats().sent_ping_requests_total);
  EXPECT_EQ(1U, lconn->stats().sent_ping_requests_before_first_response);
  EXPECT_EQ(1U, lconn->stats().recv_ping_responses);
  EXPECT_EQ(1U, rconn->stats().recv_ping_requests);
  EXPECT_EQ(1U, rconn->stats().sent_ping_responses);

  EXPECT_FALSE(msg->IsLegacy());
  const StunAddressAttribute* addr_attr =
      msg->GetAddress(STUN_ATTR_XOR_MAPPED_ADDRESS);
  ASSERT_TRUE(addr_attr != NULL);
  EXPECT_EQ(lport->Candidates()[0].address(), addr_attr->GetAddress());
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_MESSAGE_INTEGRITY) != NULL);
  EXPECT_EQ(StunMessage::IntegrityStatus::kIntegrityOk,
            msg->ValidateMessageIntegrity("rpass"));
  EXPECT_TRUE(msg->GetUInt32(STUN_ATTR_FINGERPRINT) != NULL);
  EXPECT_TRUE(StunMessage::ValidateFingerprint(
      reinterpret_cast<const char*>(lport->last_stun_buf().data()),
      lport->last_stun_buf().size()));
  // No USERNAME or PRIORITY in ICE responses.
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_USERNAME) == NULL);
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_PRIORITY) == NULL);
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_MAPPED_ADDRESS) == NULL);
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_ICE_CONTROLLING) == NULL);
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_ICE_CONTROLLED) == NULL);
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_USE_CANDIDATE) == NULL);

  // Response should not include ping count.
  ASSERT_TRUE(msg->GetUInt32(STUN_ATTR_RETRANSMIT_COUNT) == NULL);

  // Respond with a BINDING-ERROR-RESPONSE. This wouldn't happen in real life,
  // but we can do it here.
  rport->SendBindingErrorResponse(
      request.get(), lport->Candidates()[0].address(), STUN_ERROR_SERVER_ERROR,
      STUN_ERROR_REASON_SERVER_ERROR);
  msg = rport->last_stun_msg();
  ASSERT_TRUE(msg != NULL);
  EXPECT_EQ(STUN_BINDING_ERROR_RESPONSE, msg->type());
  EXPECT_FALSE(msg->IsLegacy());
  const StunErrorCodeAttribute* error_attr = msg->GetErrorCode();
  ASSERT_TRUE(error_attr != NULL);
  EXPECT_EQ(STUN_ERROR_SERVER_ERROR, error_attr->code());
  EXPECT_EQ(std::string(STUN_ERROR_REASON_SERVER_ERROR), error_attr->reason());
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_MESSAGE_INTEGRITY) != NULL);
  EXPECT_EQ(StunMessage::IntegrityStatus::kIntegrityOk,
            msg->ValidateMessageIntegrity("rpass"));
  EXPECT_TRUE(msg->GetUInt32(STUN_ATTR_FINGERPRINT) != NULL);
  EXPECT_TRUE(StunMessage::ValidateFingerprint(
      reinterpret_cast<const char*>(lport->last_stun_buf().data()),
      lport->last_stun_buf().size()));
  // No USERNAME with ICE.
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_USERNAME) == NULL);
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_PRIORITY) == NULL);

  // Testing STUN binding requests from rport --> lport, having ICE_CONTROLLED
  // and (incremented) RETRANSMIT_COUNT attributes.
  rport->Reset();
  rport->set_send_retransmit_count_attribute(true);
  rconn->Ping(0);
  rconn->Ping(0);
  rconn->Ping(0);
  ASSERT_TRUE_WAIT(rport->last_stun_msg() != NULL, kDefaultTimeout);
  msg = rport->last_stun_msg();
  EXPECT_EQ(STUN_BINDING_REQUEST, msg->type());
  const StunUInt64Attribute* ice_controlled_attr =
      msg->GetUInt64(STUN_ATTR_ICE_CONTROLLED);
  ASSERT_TRUE(ice_controlled_attr != NULL);
  EXPECT_EQ(rport->IceTiebreaker(), ice_controlled_attr->value());
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_USE_CANDIDATE) == NULL);

  // Request should include ping count.
  const StunUInt32Attribute* retransmit_attr =
      msg->GetUInt32(STUN_ATTR_RETRANSMIT_COUNT);
  ASSERT_TRUE(retransmit_attr != NULL);
  EXPECT_EQ(2U, retransmit_attr->value());

  // Respond with a BINDING-RESPONSE.
  request = CopyStunMessage(*msg);
  lconn->OnReadPacket(rtc::ReceivedPacket(rport->last_stun_buf(),
                                          rtc::SocketAddress(), absl::nullopt));
  msg = lport->last_stun_msg();
  // Receive the BINDING-RESPONSE.
  rconn->OnReadPacket(rtc::ReceivedPacket(lport->last_stun_buf(),
                                          rtc::SocketAddress(), absl::nullopt));

  // Verify the Stun ping stats.
  EXPECT_EQ(3U, rconn->stats().sent_ping_requests_total);
  EXPECT_EQ(3U, rconn->stats().sent_ping_requests_before_first_response);
  EXPECT_EQ(1U, rconn->stats().recv_ping_responses);
  EXPECT_EQ(1U, lconn->stats().sent_ping_responses);
  EXPECT_EQ(1U, lconn->stats().recv_ping_requests);
  // Ping after receiver the first response
  rconn->Ping(0);
  rconn->Ping(0);
  EXPECT_EQ(5U, rconn->stats().sent_ping_requests_total);
  EXPECT_EQ(3U, rconn->stats().sent_ping_requests_before_first_response);

  // Response should include same ping count.
  retransmit_attr = msg->GetUInt32(STUN_ATTR_RETRANSMIT_COUNT);
  ASSERT_TRUE(retransmit_attr != NULL);
  EXPECT_EQ(2U, retransmit_attr->value());
}

TEST_F(PortTest, TestNomination) {
  auto lport = CreateTestPort(kLocalAddr1, "lfrag", "lpass");
  auto rport = CreateTestPort(kLocalAddr2, "rfrag", "rpass");
  lport->SetIceRole(cricket::ICEROLE_CONTROLLING);
  lport->SetIceTiebreaker(kTiebreaker1);
  rport->SetIceRole(cricket::ICEROLE_CONTROLLED);
  rport->SetIceTiebreaker(kTiebreaker2);

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
  lconn->Ping(0);
  ASSERT_TRUE_WAIT(lport->last_stun_msg(), kDefaultTimeout);
  ASSERT_GT(lport->last_stun_buf().size(), 0u);
  rconn->OnReadPacket(rtc::ReceivedPacket(lport->last_stun_buf(),
                                          rtc::SocketAddress(), absl::nullopt));

  EXPECT_EQ(nomination, rconn->remote_nomination());
  EXPECT_FALSE(lconn->nominated());
  EXPECT_TRUE(rconn->nominated());
  EXPECT_EQ(lconn->nominated(), lconn->stats().nominated);
  EXPECT_EQ(rconn->nominated(), rconn->stats().nominated);

  // This should result in an acknowledgment sent back from `rconn` to `lconn`,
  // updating the acknowledged nomination of `lconn`.
  ASSERT_TRUE_WAIT(rport->last_stun_msg(), kDefaultTimeout);
  ASSERT_GT(rport->last_stun_buf().size(), 0u);
  lconn->OnReadPacket(rtc::ReceivedPacket(rport->last_stun_buf(),
                                          rtc::SocketAddress(), absl::nullopt));

  EXPECT_EQ(nomination, lconn->acked_nomination());
  EXPECT_TRUE(lconn->nominated());
  EXPECT_TRUE(rconn->nominated());
  EXPECT_EQ(lconn->nominated(), lconn->stats().nominated);
  EXPECT_EQ(rconn->nominated(), rconn->stats().nominated);
}

TEST_F(PortTest, TestRoundTripTime) {
  rtc::ScopedFakeClock clock;

  auto lport = CreateTestPort(kLocalAddr1, "lfrag", "lpass");
  auto rport = CreateTestPort(kLocalAddr2, "rfrag", "rpass");
  lport->SetIceRole(cricket::ICEROLE_CONTROLLING);
  lport->SetIceTiebreaker(kTiebreaker1);
  rport->SetIceRole(cricket::ICEROLE_CONTROLLED);
  rport->SetIceTiebreaker(kTiebreaker2);

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

  SendPingAndReceiveResponse(lconn, lport.get(), rconn, rport.get(), &clock,
                             10);
  EXPECT_EQ(10u, lconn->stats().total_round_trip_time_ms);
  ASSERT_TRUE(lconn->stats().current_round_trip_time_ms);
  EXPECT_EQ(10u, *lconn->stats().current_round_trip_time_ms);

  SendPingAndReceiveResponse(lconn, lport.get(), rconn, rport.get(), &clock,
                             20);
  EXPECT_EQ(30u, lconn->stats().total_round_trip_time_ms);
  ASSERT_TRUE(lconn->stats().current_round_trip_time_ms);
  EXPECT_EQ(20u, *lconn->stats().current_round_trip_time_ms);

  SendPingAndReceiveResponse(lconn, lport.get(), rconn, rport.get(), &clock,
                             30);
  EXPECT_EQ(60u, lconn->stats().total_round_trip_time_ms);
  ASSERT_TRUE(lconn->stats().current_round_trip_time_ms);
  EXPECT_EQ(30u, *lconn->stats().current_round_trip_time_ms);
}

TEST_F(PortTest, TestUseCandidateAttribute) {
  auto lport = CreateTestPort(kLocalAddr1, "lfrag", "lpass");
  auto rport = CreateTestPort(kLocalAddr2, "rfrag", "rpass");
  lport->SetIceRole(cricket::ICEROLE_CONTROLLING);
  lport->SetIceTiebreaker(kTiebreaker1);
  rport->SetIceRole(cricket::ICEROLE_CONTROLLED);
  rport->SetIceTiebreaker(kTiebreaker2);

  // Send a fake ping from lport to rport.
  lport->PrepareAddress();
  rport->PrepareAddress();
  ASSERT_FALSE(rport->Candidates().empty());
  Connection* lconn =
      lport->CreateConnection(rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  lconn->Ping(0);
  ASSERT_TRUE_WAIT(lport->last_stun_msg() != NULL, kDefaultTimeout);
  IceMessage* msg = lport->last_stun_msg();
  const StunUInt64Attribute* ice_controlling_attr =
      msg->GetUInt64(STUN_ATTR_ICE_CONTROLLING);
  ASSERT_TRUE(ice_controlling_attr != NULL);
  const StunByteStringAttribute* use_candidate_attr =
      msg->GetByteString(STUN_ATTR_USE_CANDIDATE);
  ASSERT_TRUE(use_candidate_attr != NULL);
}

// Tests that when the network type changes, the network cost of the port will
// change, the network cost of the local candidates will change. Also tests that
// the remote network costs are updated with the stun binding requests.
TEST_F(PortTest, TestNetworkCostChange) {
  rtc::Network* test_network = MakeNetwork(kLocalAddr1);
  auto lport = CreateTestPort(test_network, "lfrag", "lpass");
  auto rport = CreateTestPort(test_network, "rfrag", "rpass");
  lport->SetIceRole(cricket::ICEROLE_CONTROLLING);
  lport->SetIceTiebreaker(kTiebreaker1);
  rport->SetIceRole(cricket::ICEROLE_CONTROLLED);
  rport->SetIceTiebreaker(kTiebreaker2);
  lport->PrepareAddress();
  rport->PrepareAddress();

  // Default local port cost is rtc::kNetworkCostUnknown.
  EXPECT_EQ(rtc::kNetworkCostUnknown, lport->network_cost());
  ASSERT_TRUE(!lport->Candidates().empty());
  for (const cricket::Candidate& candidate : lport->Candidates()) {
    EXPECT_EQ(rtc::kNetworkCostUnknown, candidate.network_cost());
  }

  // Change the network type to wifi.
  test_network->set_type(rtc::ADAPTER_TYPE_WIFI);
  EXPECT_EQ(rtc::kNetworkCostLow, lport->network_cost());
  for (const cricket::Candidate& candidate : lport->Candidates()) {
    EXPECT_EQ(rtc::kNetworkCostLow, candidate.network_cost());
  }

  // Add a connection and then change the network type.
  Connection* lconn =
      lport->CreateConnection(rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  // Change the network type to cellular.
  test_network->set_type(rtc::ADAPTER_TYPE_CELLULAR);
  EXPECT_EQ(rtc::kNetworkCostHigh, lport->network_cost());
  for (const cricket::Candidate& candidate : lport->Candidates()) {
    EXPECT_EQ(rtc::kNetworkCostHigh, candidate.network_cost());
  }

  test_network->set_type(rtc::ADAPTER_TYPE_WIFI);
  Connection* rconn =
      rport->CreateConnection(lport->Candidates()[0], Port::ORIGIN_MESSAGE);
  test_network->set_type(rtc::ADAPTER_TYPE_CELLULAR);
  lconn->Ping(0);
  // The rconn's remote candidate cost is rtc::kNetworkCostLow, but the ping
  // contains an attribute of network cost of rtc::kNetworkCostHigh. Once the
  // message is handled in rconn, The rconn's remote candidate will have cost
  // rtc::kNetworkCostHigh;
  EXPECT_EQ(rtc::kNetworkCostLow, rconn->remote_candidate().network_cost());
  ASSERT_TRUE_WAIT(lport->last_stun_msg() != NULL, kDefaultTimeout);
  IceMessage* msg = lport->last_stun_msg();
  EXPECT_EQ(STUN_BINDING_REQUEST, msg->type());
  // Pass the binding request to rport.
  rconn->OnReadPacket(rtc::ReceivedPacket(lport->last_stun_buf(),
                                          rtc::SocketAddress(), absl::nullopt));

  // Wait until rport sends the response and then check the remote network cost.
  ASSERT_TRUE_WAIT(rport->last_stun_msg() != NULL, kDefaultTimeout);
  EXPECT_EQ(rtc::kNetworkCostHigh, rconn->remote_candidate().network_cost());
}

TEST_F(PortTest, TestNetworkInfoAttribute) {
  rtc::Network* test_network = MakeNetwork(kLocalAddr1);
  auto lport = CreateTestPort(test_network, "lfrag", "lpass");
  auto rport = CreateTestPort(test_network, "rfrag", "rpass");
  lport->SetIceRole(cricket::ICEROLE_CONTROLLING);
  lport->SetIceTiebreaker(kTiebreaker1);
  rport->SetIceRole(cricket::ICEROLE_CONTROLLED);
  rport->SetIceTiebreaker(kTiebreaker2);

  uint16_t lnetwork_id = 9;
  test_network->set_id(lnetwork_id);
  // Send a fake ping from lport to rport.
  lport->PrepareAddress();
  rport->PrepareAddress();
  Connection* lconn =
      lport->CreateConnection(rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  lconn->Ping(0);
  ASSERT_TRUE_WAIT(lport->last_stun_msg() != NULL, kDefaultTimeout);
  IceMessage* msg = lport->last_stun_msg();
  const StunUInt32Attribute* network_info_attr =
      msg->GetUInt32(STUN_ATTR_GOOG_NETWORK_INFO);
  ASSERT_TRUE(network_info_attr != NULL);
  uint32_t network_info = network_info_attr->value();
  EXPECT_EQ(lnetwork_id, network_info >> 16);
  // Default network has unknown type and cost kNetworkCostUnknown.
  EXPECT_EQ(rtc::kNetworkCostUnknown, network_info & 0xFFFF);

  // Set the network type to be cellular so its cost will be kNetworkCostHigh.
  // Send a fake ping from rport to lport.
  test_network->set_type(rtc::ADAPTER_TYPE_CELLULAR);
  uint16_t rnetwork_id = 8;
  test_network->set_id(rnetwork_id);
  Connection* rconn =
      rport->CreateConnection(lport->Candidates()[0], Port::ORIGIN_MESSAGE);
  rconn->Ping(0);
  ASSERT_TRUE_WAIT(rport->last_stun_msg() != NULL, kDefaultTimeout);
  msg = rport->last_stun_msg();
  network_info_attr = msg->GetUInt32(STUN_ATTR_GOOG_NETWORK_INFO);
  ASSERT_TRUE(network_info_attr != NULL);
  network_info = network_info_attr->value();
  EXPECT_EQ(rnetwork_id, network_info >> 16);
  EXPECT_EQ(rtc::kNetworkCostHigh, network_info & 0xFFFF);
}

// Test handling STUN messages.
TEST_F(PortTest, TestHandleStunMessage) {
  // Our port will act as the "remote" port.
  auto port = CreateTestPort(kLocalAddr2, "rfrag", "rpass");

  std::unique_ptr<IceMessage> in_msg, out_msg;
  auto buf = std::make_unique<ByteBufferWriter>();
  rtc::SocketAddress addr(kLocalAddr1);
  std::string username;

  // BINDING-REQUEST from local to remote with valid ICE username,
  // MESSAGE-INTEGRITY, and FINGERPRINT.
  in_msg = CreateStunMessageWithUsername(STUN_BINDING_REQUEST, "rfrag:lfrag");
  in_msg->AddMessageIntegrity("rpass");
  in_msg->AddFingerprint();
  WriteStunMessage(*in_msg, buf.get());
  EXPECT_TRUE(GetStunMessageFromBufferWriter(port.get(), buf.get(), addr,
                                             &out_msg, &username));
  EXPECT_TRUE(out_msg.get() != NULL);
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
  EXPECT_TRUE(out_msg.get() != NULL);
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
  EXPECT_TRUE(out_msg.get() != NULL);
  EXPECT_EQ("", username);
  ASSERT_TRUE(out_msg->GetErrorCode() != NULL);
  EXPECT_EQ(STUN_ERROR_SERVER_ERROR, out_msg->GetErrorCode()->code());
  EXPECT_EQ(std::string(STUN_ERROR_REASON_SERVER_ERROR),
            out_msg->GetErrorCode()->reason());
}

// Tests handling of ICE binding requests with missing or incorrect usernames.
TEST_F(PortTest, TestHandleStunMessageBadUsername) {
  auto port = CreateTestPort(kLocalAddr2, "rfrag", "rpass");

  std::unique_ptr<IceMessage> in_msg, out_msg;
  auto buf = std::make_unique<ByteBufferWriter>();
  rtc::SocketAddress addr(kLocalAddr1);
  std::string username;

  // BINDING-REQUEST with no username.
  in_msg = CreateStunMessage(STUN_BINDING_REQUEST);
  in_msg->AddMessageIntegrity("rpass");
  in_msg->AddFingerprint();
  WriteStunMessage(*in_msg, buf.get());
  EXPECT_TRUE(GetStunMessageFromBufferWriter(port.get(), buf.get(), addr,
                                             &out_msg, &username));
  EXPECT_TRUE(out_msg.get() == NULL);
  EXPECT_EQ("", username);
  EXPECT_EQ(STUN_ERROR_BAD_REQUEST, port->last_stun_error_code());

  // BINDING-REQUEST with empty username.
  in_msg = CreateStunMessageWithUsername(STUN_BINDING_REQUEST, "");
  in_msg->AddMessageIntegrity("rpass");
  in_msg->AddFingerprint();
  WriteStunMessage(*in_msg, buf.get());
  EXPECT_TRUE(GetStunMessageFromBufferWriter(port.get(), buf.get(), addr,
                                             &out_msg, &username));
  EXPECT_TRUE(out_msg.get() == NULL);
  EXPECT_EQ("", username);
  EXPECT_EQ(STUN_ERROR_UNAUTHORIZED, port->last_stun_error_code());

  // BINDING-REQUEST with too-short username.
  in_msg = CreateStunMessageWithUsername(STUN_BINDING_REQUEST, "rfra");
  in_msg->AddMessageIntegrity("rpass");
  in_msg->AddFingerprint();
  WriteStunMessage(*in_msg, buf.get());
  EXPECT_TRUE(GetStunMessageFromBufferWriter(port.get(), buf.get(), addr,
                                             &out_msg, &username));
  EXPECT_TRUE(out_msg.get() == NULL);
  EXPECT_EQ("", username);
  EXPECT_EQ(STUN_ERROR_UNAUTHORIZED, port->last_stun_error_code());

  // BINDING-REQUEST with reversed username.
  in_msg = CreateStunMessageWithUsername(STUN_BINDING_REQUEST, "lfrag:rfrag");
  in_msg->AddMessageIntegrity("rpass");
  in_msg->AddFingerprint();
  WriteStunMessage(*in_msg, buf.get());
  EXPECT_TRUE(GetStunMessageFromBufferWriter(port.get(), buf.get(), addr,
                                             &out_msg, &username));
  EXPECT_TRUE(out_msg.get() == NULL);
  EXPECT_EQ("", username);
  EXPECT_EQ(STUN_ERROR_UNAUTHORIZED, port->last_stun_error_code());

  // BINDING-REQUEST with garbage username.
  in_msg = CreateStunMessageWithUsername(STUN_BINDING_REQUEST, "abcd:efgh");
  in_msg->AddMessageIntegrity("rpass");
  in_msg->AddFingerprint();
  WriteStunMessage(*in_msg, buf.get());
  EXPECT_TRUE(GetStunMessageFromBufferWriter(port.get(), buf.get(), addr,
                                             &out_msg, &username));
  EXPECT_TRUE(out_msg.get() == NULL);
  EXPECT_EQ("", username);
  EXPECT_EQ(STUN_ERROR_UNAUTHORIZED, port->last_stun_error_code());
}

// Test handling STUN messages with missing or malformed M-I.
TEST_F(PortTest, TestHandleStunMessageBadMessageIntegrity) {
  // Our port will act as the "remote" port.
  auto port = CreateTestPort(kLocalAddr2, "rfrag", "rpass");

  std::unique_ptr<IceMessage> in_msg, out_msg;
  auto buf = std::make_unique<ByteBufferWriter>();
  rtc::SocketAddress addr(kLocalAddr1);
  std::string username;

  // BINDING-REQUEST from local to remote with valid ICE username and
  // FINGERPRINT, but no MESSAGE-INTEGRITY.
  in_msg = CreateStunMessageWithUsername(STUN_BINDING_REQUEST, "rfrag:lfrag");
  in_msg->AddFingerprint();
  WriteStunMessage(*in_msg, buf.get());
  EXPECT_TRUE(GetStunMessageFromBufferWriter(port.get(), buf.get(), addr,
                                             &out_msg, &username));
  EXPECT_TRUE(out_msg.get() == NULL);
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
  EXPECT_TRUE(out_msg.get() == NULL);
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
  rtc::SocketAddress addr(kLocalAddr1);
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
  rtc::SocketAddress addr(kLocalAddr1);
  std::string username;

  // Build ordinary message with valid ufrag/pass.
  in_msg = CreateStunMessageWithUsername(STUN_BINDING_REQUEST, "rfrag:lfrag");
  in_msg->AddMessageIntegrity("rpass");
  // Add a couple attributes with ID in comprehension-required range.
  in_msg->AddAttribute(StunAttribute::CreateUInt32(0x7777));
  in_msg->AddAttribute(StunAttribute::CreateUInt32(0x4567));
  // ... And one outside the range.
  in_msg->AddAttribute(StunAttribute::CreateUInt32(0xdead));
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
  auto lport = CreateTestPort(kLocalAddr1, "lfrag", "lpass",
                              cricket::ICEROLE_CONTROLLING, kTiebreakerDefault);
  auto rport = CreateTestPort(kLocalAddr2, "rfrag", "rpass",
                              cricket::ICEROLE_CONTROLLED, kTiebreakerDefault);
  lport->PrepareAddress();
  rport->PrepareAddress();
  ASSERT_FALSE(lport->Candidates().empty());
  ASSERT_FALSE(rport->Candidates().empty());
  Connection* lconn =
      lport->CreateConnection(rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  Connection* rconn =
      rport->CreateConnection(lport->Candidates()[0], Port::ORIGIN_MESSAGE);

  // Send request.
  lconn->Ping(0);
  ASSERT_TRUE_WAIT(lport->last_stun_msg() != NULL, kDefaultTimeout);
  rconn->OnReadPacket(rtc::ReceivedPacket(lport->last_stun_buf(),
                                          rtc::SocketAddress(), absl::nullopt));

  // Intercept request and add comprehension required attribute.
  ASSERT_TRUE_WAIT(rport->last_stun_msg() != NULL, kDefaultTimeout);
  auto modified_response = rport->last_stun_msg()->Clone();
  modified_response->AddAttribute(StunAttribute::CreateUInt32(0x7777));
  modified_response->RemoveAttribute(STUN_ATTR_FINGERPRINT);
  modified_response->AddFingerprint();
  ByteBufferWriter buf;
  WriteStunMessage(*modified_response, &buf);
  lconn->OnReadPacket(rtc::ReceivedPacket::CreateFromLegacy(
      reinterpret_cast<const char*>(buf.Data()), buf.Length(),
      /*packet_time_us=*/-1));
  // Response should have been ignored, leaving us unwritable still.
  EXPECT_FALSE(lconn->writable());
}

// Similar to the above, but with an indication. As with a response, it should
// just be ignored.
TEST_F(PortTest,
       TestHandleStunIndicationWithUnknownComprehensionRequiredAttribute) {
  // Generic set up.
  auto lport = CreateTestPort(kLocalAddr2, "lfrag", "lpass",
                              cricket::ICEROLE_CONTROLLING, kTiebreakerDefault);
  auto rport = CreateTestPort(kLocalAddr2, "rfrag", "rpass",
                              cricket::ICEROLE_CONTROLLED, kTiebreakerDefault);
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
  lconn->OnReadPacket(rtc::ReceivedPacket::CreateFromLegacy(
      buf.Data(), buf.Length(), /*packet_time_us=*/-1));
  EXPECT_EQ(0u, lconn->last_ping_received());
}

// Test handling of STUN binding indication messages . STUN binding
// indications are allowed only to the connection which is in read mode.
TEST_F(PortTest, TestHandleStunBindingIndication) {
  auto lport = CreateTestPort(kLocalAddr2, "lfrag", "lpass",
                              cricket::ICEROLE_CONTROLLING, kTiebreaker1);

  // Verifying encoding and decoding STUN indication message.
  std::unique_ptr<IceMessage> in_msg, out_msg;
  std::unique_ptr<ByteBufferWriter> buf(new ByteBufferWriter());
  rtc::SocketAddress addr(kLocalAddr1);
  std::string username;

  in_msg = CreateStunMessage(STUN_BINDING_INDICATION);
  in_msg->AddFingerprint();
  WriteStunMessage(*in_msg, buf.get());
  EXPECT_TRUE(GetStunMessageFromBufferWriter(lport.get(), buf.get(), addr,
                                             &out_msg, &username));
  EXPECT_TRUE(out_msg.get() != NULL);
  EXPECT_EQ(out_msg->type(), STUN_BINDING_INDICATION);
  EXPECT_EQ("", username);

  // Verify connection can handle STUN indication and updates
  // last_ping_received.
  auto rport = CreateTestPort(kLocalAddr2, "rfrag", "rpass");
  rport->SetIceRole(cricket::ICEROLE_CONTROLLED);
  rport->SetIceTiebreaker(kTiebreaker2);

  lport->PrepareAddress();
  rport->PrepareAddress();
  ASSERT_FALSE(lport->Candidates().empty());
  ASSERT_FALSE(rport->Candidates().empty());

  Connection* lconn =
      lport->CreateConnection(rport->Candidates()[0], Port::ORIGIN_MESSAGE);
  Connection* rconn =
      rport->CreateConnection(lport->Candidates()[0], Port::ORIGIN_MESSAGE);
  rconn->Ping(0);

  ASSERT_TRUE_WAIT(rport->last_stun_msg() != NULL, kDefaultTimeout);
  IceMessage* msg = rport->last_stun_msg();
  EXPECT_EQ(STUN_BINDING_REQUEST, msg->type());
  // Send rport binding request to lport.
  lconn->OnReadPacket(rtc::ReceivedPacket(rport->last_stun_buf(),
                                          rtc::SocketAddress(), absl::nullopt));

  ASSERT_TRUE_WAIT(lport->last_stun_msg() != NULL, kDefaultTimeout);
  EXPECT_EQ(STUN_BINDING_RESPONSE, lport->last_stun_msg()->type());
  int64_t last_ping_received1 = lconn->last_ping_received();

  // Adding a delay of 100ms.
  rtc::Thread::Current()->ProcessMessages(100);
  // Pinging lconn using stun indication message.
  lconn->OnReadPacket(rtc::ReceivedPacket::CreateFromLegacy(
      buf->Data(), buf->Length(), /*packet_time_us=*/-1));
  int64_t last_ping_received2 = lconn->last_ping_received();
  EXPECT_GT(last_ping_received2, last_ping_received1);
}

TEST_F(PortTest, TestComputeCandidatePriority) {
  auto port = CreateTestPort(kLocalAddr1, "name", "pass");
  port->SetIceTiebreaker(kTiebreakerDefault);
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
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-IncreaseIceCandidatePriorityHostSrflx/Enabled/");
  auto port = CreateTestPort(kLocalAddr1, "name", "pass", &field_trials);
  port->SetIceTiebreaker(kTiebreakerDefault);
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
  testport->SetIceTiebreaker(kTiebreakerDefault);
  testport->AddCandidateAddress(kLocalAddr1, kLocalAddr1,
                                IceCandidateType::kHost,
                                cricket::ICE_TYPE_PREFERENCE_HOST, false);
  testport->AddCandidateAddress(kLocalAddr2, kLocalAddr1,
                                IceCandidateType::kSrflx,
                                cricket::ICE_TYPE_PREFERENCE_SRFLX, true);
  EXPECT_NE(testport->Candidates()[0].foundation(),
            testport->Candidates()[1].foundation());
}

// This test verifies the foundation of different types of ICE candidates.
TEST_F(PortTest, TestCandidateFoundation) {
  std::unique_ptr<rtc::NATServer> nat_server(
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
  ASSERT_EQ_WAIT(1U, stunport->Candidates().size(), kDefaultTimeout);
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
  ASSERT_EQ_WAIT(1U, turnport1->Candidates().size(), kDefaultTimeout);
  EXPECT_NE(udpport1->Candidates()[0].foundation(),
            turnport1->Candidates()[0].foundation());
  EXPECT_NE(udpport2->Candidates()[0].foundation(),
            turnport1->Candidates()[0].foundation());
  EXPECT_NE(stunport->Candidates()[0].foundation(),
            turnport1->Candidates()[0].foundation());
  auto turnport2 =
      CreateTurnPort(kLocalAddr1, nat_socket_factory1(), PROTO_UDP, PROTO_UDP);
  turnport2->PrepareAddress();
  ASSERT_EQ_WAIT(1U, turnport2->Candidates().size(), kDefaultTimeout);
  EXPECT_EQ(turnport1->Candidates()[0].foundation(),
            turnport2->Candidates()[0].foundation());

  // Running a second turn server, to get different base IP address.
  SocketAddress kTurnUdpIntAddr2("99.99.98.4", STUN_SERVER_PORT);
  SocketAddress kTurnUdpExtAddr2("99.99.98.5", 0);
  TestTurnServer turn_server2(rtc::Thread::Current(), vss(), kTurnUdpIntAddr2,
                              kTurnUdpExtAddr2);
  auto turnport3 = CreateTurnPort(kLocalAddr1, nat_socket_factory1(), PROTO_UDP,
                                  PROTO_UDP, kTurnUdpIntAddr2);
  turnport3->PrepareAddress();
  ASSERT_EQ_WAIT(1U, turnport3->Candidates().size(), kDefaultTimeout);
  EXPECT_NE(turnport3->Candidates()[0].foundation(),
            turnport2->Candidates()[0].foundation());

  // Start a TCP turn server, and check that two turn candidates have
  // different foundations if their relay protocols are different.
  TestTurnServer turn_server3(rtc::Thread::Current(), vss(), kTurnTcpIntAddr,
                              kTurnUdpExtAddr, PROTO_TCP);
  auto turnport4 =
      CreateTurnPort(kLocalAddr1, nat_socket_factory1(), PROTO_TCP, PROTO_UDP);
  turnport4->PrepareAddress();
  ASSERT_EQ_WAIT(1U, turnport4->Candidates().size(), kDefaultTimeout);
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
  ASSERT_EQ_WAIT(1U, stunport->Candidates().size(), kDefaultTimeout);
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
  ASSERT_EQ_WAIT(1U, turnport->Candidates().size(), kDefaultTimeout);
  EXPECT_EQ(kTurnUdpExtAddr.ipaddr(),
            turnport->Candidates()[0].address().ipaddr());
  EXPECT_EQ(kNatAddr1.ipaddr(),
            turnport->Candidates()[0].related_address().ipaddr());
}

// Test priority value overflow handling when preference is set to 3.
TEST_F(PortTest, TestCandidatePriority) {
  cricket::Candidate cand1;
  cand1.set_priority(3);
  cricket::Candidate cand2;
  cand2.set_priority(1);
  EXPECT_TRUE(cand1.priority() > cand2.priority());
}

// Test the Connection priority is calculated correctly.
TEST_F(PortTest, TestConnectionPriority) {
  auto lport = CreateTestPort(kLocalAddr1, "lfrag", "lpass");
  lport->SetIceTiebreaker(kTiebreakerDefault);
  lport->set_type_preference(cricket::ICE_TYPE_PREFERENCE_HOST);

  auto rport = CreateTestPort(kLocalAddr2, "rfrag", "rpass");
  rport->SetIceTiebreaker(kTiebreakerDefault);
  rport->set_type_preference(cricket::ICE_TYPE_PREFERENCE_RELAY_UDP);
  lport->set_component(123);
  lport->AddCandidateAddress(SocketAddress("192.168.1.4", 1234));
  rport->set_component(23);
  rport->AddCandidateAddress(SocketAddress("10.1.1.100", 1234));

  EXPECT_EQ(0x7E001E85U, lport->Candidates()[0].priority());
  EXPECT_EQ(0x2001EE9U, rport->Candidates()[0].priority());

  // RFC 5245
  // pair priority = 2^32*MIN(G,D) + 2*MAX(G,D) + (G>D?1:0)
  lport->SetIceRole(cricket::ICEROLE_CONTROLLING);
  rport->SetIceRole(cricket::ICEROLE_CONTROLLED);
  Connection* lconn =
      lport->CreateConnection(rport->Candidates()[0], Port::ORIGIN_MESSAGE);
#if defined(WEBRTC_WIN)
  EXPECT_EQ(0x2001EE9FC003D0BU, lconn->priority());
#else
  EXPECT_EQ(0x2001EE9FC003D0BLLU, lconn->priority());
#endif

  lport->SetIceRole(cricket::ICEROLE_CONTROLLED);
  rport->SetIceRole(cricket::ICEROLE_CONTROLLING);
  Connection* rconn =
      rport->CreateConnection(lport->Candidates()[0], Port::ORIGIN_MESSAGE);
#if defined(WEBRTC_WIN)
  EXPECT_EQ(0x2001EE9FC003D0AU, rconn->priority());
#else
  EXPECT_EQ(0x2001EE9FC003D0ALLU, rconn->priority());
#endif
}

// Test the Connection priority is calculated correctly.
TEST_F(PortTest, TestConnectionPriorityWithPriorityAdjustment) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-IncreaseIceCandidatePriorityHostSrflx/Enabled/");
  auto lport = CreateTestPort(kLocalAddr1, "lfrag", "lpass", &field_trials);
  lport->SetIceTiebreaker(kTiebreakerDefault);
  lport->set_type_preference(cricket::ICE_TYPE_PREFERENCE_HOST);

  auto rport = CreateTestPort(kLocalAddr2, "rfrag", "rpass", &field_trials);
  rport->SetIceTiebreaker(kTiebreakerDefault);
  rport->set_type_preference(cricket::ICE_TYPE_PREFERENCE_RELAY_UDP);
  lport->set_component(123);
  lport->AddCandidateAddress(SocketAddress("192.168.1.4", 1234));
  rport->set_component(23);
  rport->AddCandidateAddress(SocketAddress("10.1.1.100", 1234));

  EXPECT_EQ(0x7E001E85U + (kMaxTurnServers << 8),
            lport->Candidates()[0].priority());
  EXPECT_EQ(0x2001EE9U + (kMaxTurnServers << 8),
            rport->Candidates()[0].priority());

  // RFC 5245
  // pair priority = 2^32*MIN(G,D) + 2*MAX(G,D) + (G>D?1:0)
  lport->SetIceRole(cricket::ICEROLE_CONTROLLING);
  rport->SetIceRole(cricket::ICEROLE_CONTROLLED);
  Connection* lconn =
      lport->CreateConnection(rport->Candidates()[0], Port::ORIGIN_MESSAGE);
#if defined(WEBRTC_WIN)
  EXPECT_EQ(0x2003EE9FC007D0BU, lconn->priority());
#else
  EXPECT_EQ(0x2003EE9FC007D0BLLU, lconn->priority());
#endif

  lport->SetIceRole(cricket::ICEROLE_CONTROLLED);
  rport->SetIceRole(cricket::ICEROLE_CONTROLLING);
  Connection* rconn =
      rport->CreateConnection(lport->Candidates()[0], Port::ORIGIN_MESSAGE);
  RTC_LOG(LS_ERROR) << "RCONN " << rconn->priority();
#if defined(WEBRTC_WIN)
  EXPECT_EQ(0x2003EE9FC007D0AU, rconn->priority());
#else
  EXPECT_EQ(0x2003EE9FC007D0ALLU, rconn->priority());
#endif
}

// Note that UpdateState takes into account the estimated RTT, and the
// correctness of using `kMaxExpectedSimulatedRtt` as an upper bound of RTT in
// the following tests depends on the link rate and the delay distriubtion
// configured in VirtualSocketServer::AddPacketToNetwork. The tests below use
// the default setup where the RTT is deterministically one, which generates an
// estimate given by `MINIMUM_RTT` = 100.
TEST_F(PortTest, TestWritableState) {
  rtc::ScopedFakeClock clock;
  auto port1 = CreateUdpPort(kLocalAddr1);
  port1->SetIceRole(cricket::ICEROLE_CONTROLLING);
  auto port2 = CreateUdpPort(kLocalAddr2);
  port2->SetIceRole(cricket::ICEROLE_CONTROLLED);

  // Set up channels.
  TestChannel ch1(std::move(port1));
  TestChannel ch2(std::move(port2));

  // Acquire addresses.
  ch1.Start();
  ch2.Start();
  ASSERT_EQ_SIMULATED_WAIT(1, ch1.complete_count(), kDefaultTimeout, clock);
  ASSERT_EQ_SIMULATED_WAIT(1, ch2.complete_count(), kDefaultTimeout, clock);

  // Send a ping from src to dst.
  ch1.CreateConnection(GetCandidate(ch2.port()));
  ASSERT_TRUE(ch1.conn() != NULL);
  EXPECT_EQ(Connection::STATE_WRITE_INIT, ch1.conn()->write_state());
  // for TCP connect
  EXPECT_TRUE_SIMULATED_WAIT(ch1.conn()->connected(), kDefaultTimeout, clock);
  ch1.Ping();
  SIMULATED_WAIT(!ch2.remote_address().IsNil(), kShortTimeout, clock);

  // Data should be sendable before the connection is accepted.
  char data[] = "abcd";
  int data_size = arraysize(data);
  rtc::PacketOptions options;
  EXPECT_EQ(data_size, ch1.conn()->Send(data, data_size, options));

  // Accept the connection to return the binding response, transition to
  // writable, and allow data to be sent.
  ch2.AcceptConnection(GetCandidate(ch1.port()));
  EXPECT_EQ_SIMULATED_WAIT(Connection::STATE_WRITABLE,
                           ch1.conn()->write_state(), kDefaultTimeout, clock);
  EXPECT_EQ(data_size, ch1.conn()->Send(data, data_size, options));

  // Ask the connection to update state as if enough time has passed to lose
  // full writability and 5 pings went unresponded to. We'll accomplish the
  // latter by sending pings but not pumping messages.
  for (uint32_t i = 1; i <= CONNECTION_WRITE_CONNECT_FAILURES; ++i) {
    ch1.Ping(i);
  }
  int unreliable_timeout_delay =
      CONNECTION_WRITE_CONNECT_TIMEOUT + kMaxExpectedSimulatedRtt;
  ch1.conn()->UpdateState(unreliable_timeout_delay);
  EXPECT_EQ(Connection::STATE_WRITE_UNRELIABLE, ch1.conn()->write_state());

  // Data should be able to be sent in this state.
  EXPECT_EQ(data_size, ch1.conn()->Send(data, data_size, options));

  // And now allow the other side to process the pings and send binding
  // responses.
  EXPECT_EQ_SIMULATED_WAIT(Connection::STATE_WRITABLE,
                           ch1.conn()->write_state(), kDefaultTimeout, clock);
  // Wait long enough for a full timeout (past however long we've already
  // waited).
  for (uint32_t i = 1; i <= CONNECTION_WRITE_CONNECT_FAILURES; ++i) {
    ch1.Ping(unreliable_timeout_delay + i);
  }
  ch1.conn()->UpdateState(unreliable_timeout_delay + CONNECTION_WRITE_TIMEOUT +
                          kMaxExpectedSimulatedRtt);
  EXPECT_EQ(Connection::STATE_WRITE_TIMEOUT, ch1.conn()->write_state());

  // Even if the connection has timed out, the Connection shouldn't block
  // the sending of data.
  EXPECT_EQ(data_size, ch1.conn()->Send(data, data_size, options));

  ch1.Stop();
  ch2.Stop();
}

// Test writability states using the configured threshold value to replace
// the default value given by `CONNECTION_WRITE_CONNECT_TIMEOUT` and
// `CONNECTION_WRITE_CONNECT_FAILURES`.
TEST_F(PortTest, TestWritableStateWithConfiguredThreshold) {
  rtc::ScopedFakeClock clock;
  auto port1 = CreateUdpPort(kLocalAddr1);
  port1->SetIceRole(cricket::ICEROLE_CONTROLLING);
  auto port2 = CreateUdpPort(kLocalAddr2);
  port2->SetIceRole(cricket::ICEROLE_CONTROLLED);

  // Set up channels.
  TestChannel ch1(std::move(port1));
  TestChannel ch2(std::move(port2));

  // Acquire addresses.
  ch1.Start();
  ch2.Start();
  ASSERT_EQ_SIMULATED_WAIT(1, ch1.complete_count(), kDefaultTimeout, clock);
  ASSERT_EQ_SIMULATED_WAIT(1, ch2.complete_count(), kDefaultTimeout, clock);

  // Send a ping from src to dst.
  ch1.CreateConnection(GetCandidate(ch2.port()));
  ASSERT_TRUE(ch1.conn() != NULL);
  ch1.Ping();
  SIMULATED_WAIT(!ch2.remote_address().IsNil(), kShortTimeout, clock);

  // Accept the connection to return the binding response, transition to
  // writable, and allow data to be sent.
  ch2.AcceptConnection(GetCandidate(ch1.port()));
  EXPECT_EQ_SIMULATED_WAIT(Connection::STATE_WRITABLE,
                           ch1.conn()->write_state(), kDefaultTimeout, clock);

  ch1.conn()->set_unwritable_timeout(1000);
  ch1.conn()->set_unwritable_min_checks(3);
  // Send two checks.
  ch1.Ping(1);
  ch1.Ping(2);
  // We have not reached the timeout nor have we sent the minimum number of
  // checks to change the state to Unreliable.
  ch1.conn()->UpdateState(999);
  EXPECT_EQ(Connection::STATE_WRITABLE, ch1.conn()->write_state());
  // We have not sent the minimum number of checks without responses.
  ch1.conn()->UpdateState(1000 + kMaxExpectedSimulatedRtt);
  EXPECT_EQ(Connection::STATE_WRITABLE, ch1.conn()->write_state());
  // Last ping after which the candidate pair should become Unreliable after
  // timeout.
  ch1.Ping(3);
  // We have not reached the timeout.
  ch1.conn()->UpdateState(999);
  EXPECT_EQ(Connection::STATE_WRITABLE, ch1.conn()->write_state());
  // We should be in the state Unreliable now.
  ch1.conn()->UpdateState(1000 + kMaxExpectedSimulatedRtt);
  EXPECT_EQ(Connection::STATE_WRITE_UNRELIABLE, ch1.conn()->write_state());

  ch1.Stop();
  ch2.Stop();
}

TEST_F(PortTest, TestTimeoutForNeverWritable) {
  auto port1 = CreateUdpPort(kLocalAddr1);
  port1->SetIceRole(cricket::ICEROLE_CONTROLLING);
  auto port2 = CreateUdpPort(kLocalAddr2);
  port2->SetIceRole(cricket::ICEROLE_CONTROLLED);

  // Set up channels.
  TestChannel ch1(std::move(port1));
  TestChannel ch2(std::move(port2));

  // Acquire addresses.
  ch1.Start();
  ch2.Start();

  ch1.CreateConnection(GetCandidate(ch2.port()));
  ASSERT_TRUE(ch1.conn() != NULL);
  EXPECT_EQ(Connection::STATE_WRITE_INIT, ch1.conn()->write_state());

  // Attempt to go directly to write timeout.
  for (uint32_t i = 1; i <= CONNECTION_WRITE_CONNECT_FAILURES; ++i) {
    ch1.Ping(i);
  }
  ch1.conn()->UpdateState(CONNECTION_WRITE_TIMEOUT + kMaxExpectedSimulatedRtt);
  EXPECT_EQ(Connection::STATE_WRITE_TIMEOUT, ch1.conn()->write_state());
}

// This test verifies the connection setup between ICEMODE_FULL
// and ICEMODE_LITE.
// In this test `ch1` behaves like FULL mode client and we have created
// port which responds to the ping message just like LITE client.
TEST_F(PortTest, TestIceLiteConnectivity) {
  auto ice_full_port =
      CreateTestPort(kLocalAddr1, "lfrag", "lpass",
                     cricket::ICEROLE_CONTROLLING, kTiebreaker1);
  auto* ice_full_port_ptr = ice_full_port.get();

  auto ice_lite_port = CreateTestPort(
      kLocalAddr2, "rfrag", "rpass", cricket::ICEROLE_CONTROLLED, kTiebreaker2);
  // Setup TestChannel. This behaves like FULL mode client.
  TestChannel ch1(std::move(ice_full_port));
  ch1.SetIceMode(ICEMODE_FULL);

  // Start gathering candidates.
  ch1.Start();
  ice_lite_port->PrepareAddress();

  ASSERT_EQ_WAIT(1, ch1.complete_count(), kDefaultTimeout);
  ASSERT_FALSE(ice_lite_port->Candidates().empty());

  ch1.CreateConnection(GetCandidate(ice_lite_port.get()));
  ASSERT_TRUE(ch1.conn() != NULL);
  EXPECT_EQ(Connection::STATE_WRITE_INIT, ch1.conn()->write_state());

  // Send ping from full mode client.
  // This ping must not have USE_CANDIDATE_ATTR.
  ch1.Ping();

  // Verify stun ping is without USE_CANDIDATE_ATTR. Getting message directly
  // from port.
  ASSERT_TRUE_WAIT(ice_full_port_ptr->last_stun_msg() != NULL, kDefaultTimeout);
  IceMessage* msg = ice_full_port_ptr->last_stun_msg();
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_USE_CANDIDATE) == NULL);

  // Respond with a BINDING-RESPONSE from litemode client.
  // NOTE: Ideally we should't create connection at this stage from lite
  // port, as it should be done only after receiving ping with USE_CANDIDATE.
  // But we need a connection to send a response message.
  auto* con = ice_lite_port->CreateConnection(
      ice_full_port_ptr->Candidates()[0], cricket::Port::ORIGIN_MESSAGE);
  std::unique_ptr<IceMessage> request = CopyStunMessage(*msg);
  con->SendStunBindingResponse(request.get());

  // Feeding the respone message from litemode to the full mode connection.
  ch1.conn()->OnReadPacket(rtc::ReceivedPacket(
      ice_lite_port->last_stun_buf(), rtc::SocketAddress(), absl::nullopt));

  // Verifying full mode connection becomes writable from the response.
  EXPECT_EQ_WAIT(Connection::STATE_WRITABLE, ch1.conn()->write_state(),
                 kDefaultTimeout);
  EXPECT_TRUE_WAIT(ch1.nominated(), kDefaultTimeout);

  // Clear existing stun messsages. Otherwise we will process old stun
  // message right after we send ping.
  ice_full_port_ptr->Reset();
  // Send ping. This must have USE_CANDIDATE_ATTR.
  ch1.Ping();
  ASSERT_TRUE_WAIT(ice_full_port_ptr->last_stun_msg() != NULL, kDefaultTimeout);
  msg = ice_full_port_ptr->last_stun_msg();
  EXPECT_TRUE(msg->GetByteString(STUN_ATTR_USE_CANDIDATE) != NULL);
  ch1.Stop();
}

namespace {

// Utility function for testing goog ping.
absl::optional<int> GetSupportedGoogPingVersion(const StunMessage* msg) {
  auto goog_misc = msg->GetUInt16List(STUN_ATTR_GOOG_MISC_INFO);
  if (goog_misc == nullptr) {
    return absl::nullopt;
  }

  if (msg->type() == STUN_BINDING_REQUEST) {
    if (goog_misc->Size() <
        static_cast<int>(cricket::IceGoogMiscInfoBindingRequestAttributeIndex::
                             SUPPORT_GOOG_PING_VERSION)) {
      return absl::nullopt;
    }

    return goog_misc->GetType(
        static_cast<int>(cricket::IceGoogMiscInfoBindingRequestAttributeIndex::
                             SUPPORT_GOOG_PING_VERSION));
  }

  if (msg->type() == STUN_BINDING_RESPONSE) {
    if (goog_misc->Size() <
        static_cast<int>(cricket::IceGoogMiscInfoBindingResponseAttributeIndex::
                             SUPPORT_GOOG_PING_VERSION)) {
      return absl::nullopt;
    }

    return goog_misc->GetType(
        static_cast<int>(cricket::IceGoogMiscInfoBindingResponseAttributeIndex::
                             SUPPORT_GOOG_PING_VERSION));
  }
  return absl::nullopt;
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
      CreateTestPort(kLocalAddr1, "lfrag", "lpass",
                     cricket::ICEROLE_CONTROLLING, kTiebreaker1);
  auto* port1 = port1_unique.get();
  auto port2 = CreateTestPort(kLocalAddr2, "rfrag", "rpass",
                              cricket::ICEROLE_CONTROLLED, kTiebreaker2);

  TestChannel ch1(std::move(port1_unique));
  // Block usage of STUN_ATTR_USE_CANDIDATE so that
  // ch1.conn() will sent GOOG_PING_REQUEST directly.
  // This only makes test a bit shorter...
  ch1.SetIceMode(ICEMODE_LITE);
  // Start gathering candidates.
  ch1.Start();
  port2->PrepareAddress();

  ASSERT_EQ_WAIT(1, ch1.complete_count(), kDefaultTimeout);
  ASSERT_FALSE(port2->Candidates().empty());

  ch1.CreateConnection(GetCandidate(port2.get()));
  ASSERT_TRUE(ch1.conn() != NULL);
  EXPECT_EQ(Connection::STATE_WRITE_INIT, ch1.conn()->write_state());
  ch1.conn()->SetIceFieldTrials(&trials);

  // Send ping.
  ch1.Ping();

  ASSERT_TRUE_WAIT(port1->last_stun_msg() != NULL, kDefaultTimeout);
  const IceMessage* request1 = port1->last_stun_msg();

  ASSERT_EQ(trials.enable_goog_ping,
            GetSupportedGoogPingVersion(request1) &&
                GetSupportedGoogPingVersion(request1) >= kGoogPingVersion);

  auto* con = port2->CreateConnection(port1->Candidates()[0],
                                      cricket::Port::ORIGIN_MESSAGE);
  con->SetIceFieldTrials(&trials);

  con->SendStunBindingResponse(request1);

  // Then check the response matches the settings.
  const auto* response = port2->last_stun_msg();
  EXPECT_EQ(response->type(), STUN_BINDING_RESPONSE);
  EXPECT_EQ(trials.enable_goog_ping && trials.announce_goog_ping,
            GetSupportedGoogPingVersion(response) &&
                GetSupportedGoogPingVersion(response) >= kGoogPingVersion);

  // Feeding the respone message back.
  ch1.conn()->OnReadPacket(rtc::ReceivedPacket(
      port2->last_stun_buf(), rtc::SocketAddress(), absl::nullopt));

  port1->Reset();
  port2->Reset();

  ch1.Ping();
  ASSERT_TRUE_WAIT(port1->last_stun_msg() != NULL, kDefaultTimeout);
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
      CreateTestPort(kLocalAddr1, "lfrag", "lpass",
                     cricket::ICEROLE_CONTROLLING, kTiebreaker1);
  auto* port1 = port1_unique.get();
  auto port2 = CreateTestPort(kLocalAddr2, "rfrag", "rpass",
                              cricket::ICEROLE_CONTROLLED, kTiebreaker2);

  TestChannel ch1(std::move(port1_unique));
  // Block usage of STUN_ATTR_USE_CANDIDATE so that
  // ch1.conn() will sent GOOG_PING_REQUEST directly.
  // This only makes test a bit shorter...
  ch1.SetIceMode(ICEMODE_LITE);
  // Start gathering candidates.
  ch1.Start();
  port2->PrepareAddress();

  ASSERT_EQ_WAIT(1, ch1.complete_count(), kDefaultTimeout);
  ASSERT_FALSE(port2->Candidates().empty());

  ch1.CreateConnection(GetCandidate(port2.get()));
  ASSERT_TRUE(ch1.conn() != NULL);
  EXPECT_EQ(Connection::STATE_WRITE_INIT, ch1.conn()->write_state());
  ch1.conn()->SetIceFieldTrials(&trials);

  // Send ping.
  ch1.Ping();

  ASSERT_TRUE_WAIT(port1->last_stun_msg() != NULL, kDefaultTimeout);
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
        static_cast<uint16_t>(
            cricket::IceGoogMiscInfoBindingRequestAttributeIndex::
                SUPPORT_GOOG_PING_VERSION),
        /* version */ 0);
    modified_request1->AddAttribute(std::move(list));
    modified_request1->AddMessageIntegrity("rpass");
  }
  auto* con = port2->CreateConnection(port1->Candidates()[0],
                                      cricket::Port::ORIGIN_MESSAGE);
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
      CreateTestPort(kLocalAddr1, "lfrag", "lpass",
                     cricket::ICEROLE_CONTROLLING, kTiebreaker1);
  auto* port1 = port1_unique.get();
  auto port2 = CreateTestPort(kLocalAddr2, "rfrag", "rpass",
                              cricket::ICEROLE_CONTROLLED, kTiebreaker2);

  TestChannel ch1(std::move(port1_unique));
  // Block usage of STUN_ATTR_USE_CANDIDATE so that
  // ch1.conn() will sent GOOG_PING_REQUEST directly.
  // This only makes test a bit shorter...
  ch1.SetIceMode(ICEMODE_LITE);
  // Start gathering candidates.
  ch1.Start();
  port2->PrepareAddress();

  ASSERT_EQ_WAIT(1, ch1.complete_count(), kDefaultTimeout);
  ASSERT_FALSE(port2->Candidates().empty());

  ch1.CreateConnection(GetCandidate(port2.get()));
  ASSERT_TRUE(ch1.conn() != NULL);
  EXPECT_EQ(Connection::STATE_WRITE_INIT, ch1.conn()->write_state());
  ch1.conn()->SetIceFieldTrials(&trials);

  // Send ping.
  ch1.Ping();

  ASSERT_TRUE_WAIT(port1->last_stun_msg() != NULL, kDefaultTimeout);
  const IceMessage* request1 = port1->last_stun_msg();

  ASSERT_TRUE(GetSupportedGoogPingVersion(request1) &&
              GetSupportedGoogPingVersion(request1) >= kGoogPingVersion);

  auto* con = port2->CreateConnection(port1->Candidates()[0],
                                      cricket::Port::ORIGIN_MESSAGE);
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
        static_cast<uint16_t>(
            cricket::IceGoogMiscInfoBindingResponseAttributeIndex::
                SUPPORT_GOOG_PING_VERSION),
        /* version */ 0);
    modified_response->AddAttribute(std::move(list));
    modified_response->AddMessageIntegrity("rpass");
    modified_response->AddFingerprint();
  }

  rtc::ByteBufferWriter buf;
  modified_response->Write(&buf);

  // Feeding the modified respone message back.
  ch1.conn()->OnReadPacket(rtc::ReceivedPacket::CreateFromLegacy(
      buf.Data(), buf.Length(), /*packet_time_us=*/-1));

  port1->Reset();
  port2->Reset();

  ch1.Ping();
  ASSERT_TRUE_WAIT(port1->last_stun_msg() != NULL, kDefaultTimeout);

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
      CreateTestPort(kLocalAddr1, "lfrag", "lpass",
                     cricket::ICEROLE_CONTROLLING, kTiebreaker1);
  auto* port1 = port1_unique.get();
  auto port2 = CreateTestPort(kLocalAddr2, "rfrag", "rpass",
                              cricket::ICEROLE_CONTROLLED, kTiebreaker2);

  TestChannel ch1(std::move(port1_unique));
  // Block usage of STUN_ATTR_USE_CANDIDATE so that
  // ch1.conn() will sent GOOG_PING_REQUEST directly.
  // This only makes test a bit shorter...
  ch1.SetIceMode(ICEMODE_LITE);
  // Start gathering candidates.
  ch1.Start();
  port2->PrepareAddress();

  ASSERT_EQ_WAIT(1, ch1.complete_count(), kDefaultTimeout);
  ASSERT_FALSE(port2->Candidates().empty());

  ch1.CreateConnection(GetCandidate(port2.get()));
  ASSERT_TRUE(ch1.conn() != nullptr);
  EXPECT_EQ(Connection::STATE_WRITE_INIT, ch1.conn()->write_state());
  ch1.conn()->SetIceFieldTrials(&trials);

  // Send ping.
  ch1.Ping();

  ASSERT_TRUE_WAIT(port1->last_stun_msg() != NULL, kDefaultTimeout);
  const IceMessage* msg = port1->last_stun_msg();
  auto* con = port2->CreateConnection(port1->Candidates()[0],
                                      cricket::Port::ORIGIN_MESSAGE);
  con->SetIceFieldTrials(&trials);

  // Feed the message into the connection.
  con->SendStunBindingResponse(msg);

  // The check reply wrt to settings.
  const auto* response = port2->last_stun_msg();
  ASSERT_EQ(response->type(), STUN_BINDING_RESPONSE);
  ASSERT_TRUE(GetSupportedGoogPingVersion(response) >= kGoogPingVersion);

  // Feeding the respone message back.
  ch1.conn()->OnReadPacket(rtc::ReceivedPacket(
      port2->last_stun_buf(), rtc::SocketAddress(), absl::nullopt));

  port1->Reset();
  port2->Reset();

  ch1.Ping();
  ASSERT_TRUE_WAIT(port1->last_stun_msg() != NULL, kDefaultTimeout);
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
  ASSERT_TRUE_WAIT(port1->last_stun_msg() != NULL, kDefaultTimeout);
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
      CreateTestPort(kLocalAddr1, "lfrag", "lpass",
                     cricket::ICEROLE_CONTROLLING, kTiebreaker1);
  auto* port1 = port1_unique.get();
  auto port2 = CreateTestPort(kLocalAddr2, "rfrag", "rpass",
                              cricket::ICEROLE_CONTROLLED, kTiebreaker2);

  TestChannel ch1(std::move(port1_unique));
  // Block usage of STUN_ATTR_USE_CANDIDATE so that
  // ch1.conn() will sent GOOG_PING_REQUEST directly.
  // This only makes test a bit shorter...
  ch1.SetIceMode(ICEMODE_LITE);
  // Start gathering candidates.
  ch1.Start();
  port2->PrepareAddress();

  ASSERT_EQ_WAIT(1, ch1.complete_count(), kDefaultTimeout);
  ASSERT_FALSE(port2->Candidates().empty());

  ch1.CreateConnection(GetCandidate(port2.get()));
  ASSERT_TRUE(ch1.conn() != NULL);
  EXPECT_EQ(Connection::STATE_WRITE_INIT, ch1.conn()->write_state());
  ch1.conn()->SetIceFieldTrials(&trials);

  // Send ping.
  ch1.Ping();

  ASSERT_TRUE_WAIT(port1->last_stun_msg() != NULL, kDefaultTimeout);
  const IceMessage* msg = port1->last_stun_msg();
  auto* con = port2->CreateConnection(port1->Candidates()[0],
                                      cricket::Port::ORIGIN_MESSAGE);
  con->SetIceFieldTrials(&trials);

  // Feed the message into the connection.
  con->SendStunBindingResponse(msg);

  // The check reply wrt to settings.
  const auto* response = port2->last_stun_msg();
  ASSERT_EQ(response->type(), STUN_BINDING_RESPONSE);
  ASSERT_TRUE(GetSupportedGoogPingVersion(response) >= kGoogPingVersion);

  // Feeding the respone message back.
  ch1.conn()->OnReadPacket(rtc::ReceivedPacket(
      port2->last_stun_buf(), rtc::SocketAddress(), absl::nullopt));

  port1->Reset();
  port2->Reset();

  ch1.Ping();
  ASSERT_TRUE_WAIT(port1->last_stun_msg() != NULL, kDefaultTimeout);
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
  rtc::ByteBufferWriter buf;
  error_response.Write(&buf);

  ch1.conn()->OnReadPacket(rtc::ReceivedPacket::CreateFromLegacy(
      buf.Data(), buf.Length(), /*packet_time_us=*/-1));

  // And now the third ping...this should be a binding.
  port1->Reset();
  port2->Reset();

  ch1.Ping();
  ASSERT_TRUE_WAIT(port1->last_stun_msg() != NULL, kDefaultTimeout);
  const IceMessage* msg3 = port1->last_stun_msg();

  // It should be a STUN_BINDING_REQUEST
  ASSERT_EQ(msg3->type(), STUN_BINDING_REQUEST);

  ch1.Stop();
}

// This test case verifies that both the controlling port and the controlled
// port will time out after connectivity is lost, if they are not marked as
// "keep alive until pruned."
TEST_F(PortTest, TestPortTimeoutIfNotKeptAlive) {
  rtc::ScopedFakeClock clock;
  int timeout_delay = 100;
  auto port1 = CreateUdpPort(kLocalAddr1);
  ConnectToSignalDestroyed(port1.get());
  port1->set_timeout_delay(timeout_delay);  // milliseconds
  port1->SetIceRole(cricket::ICEROLE_CONTROLLING);
  port1->SetIceTiebreaker(kTiebreaker1);

  auto port2 = CreateUdpPort(kLocalAddr2);
  ConnectToSignalDestroyed(port2.get());
  port2->set_timeout_delay(timeout_delay);  // milliseconds
  port2->SetIceRole(cricket::ICEROLE_CONTROLLED);
  port2->SetIceTiebreaker(kTiebreaker2);

  // Set up channels and ensure both ports will be deleted.
  TestChannel ch1(std::move(port1));
  TestChannel ch2(std::move(port2));

  // Simulate a connection that succeeds, and then is destroyed.
  StartConnectAndStopChannels(&ch1, &ch2);
  // After the connection is destroyed, the port will be destroyed because
  // none of them is marked as "keep alive until pruned.
  EXPECT_EQ_SIMULATED_WAIT(2, ports_destroyed(), 110, clock);
}

// Test that if after all connection are destroyed, new connections are created
// and destroyed again, ports won't be destroyed until a timeout period passes
// after the last set of connections are all destroyed.
TEST_F(PortTest, TestPortTimeoutAfterNewConnectionCreatedAndDestroyed) {
  rtc::ScopedFakeClock clock;
  int timeout_delay = 100;
  auto port1 = CreateUdpPort(kLocalAddr1);
  ConnectToSignalDestroyed(port1.get());
  port1->set_timeout_delay(timeout_delay);  // milliseconds
  port1->SetIceRole(cricket::ICEROLE_CONTROLLING);
  port1->SetIceTiebreaker(kTiebreaker1);

  auto port2 = CreateUdpPort(kLocalAddr2);
  ConnectToSignalDestroyed(port2.get());
  port2->set_timeout_delay(timeout_delay);  // milliseconds

  port2->SetIceRole(cricket::ICEROLE_CONTROLLED);
  port2->SetIceTiebreaker(kTiebreaker2);

  // Set up channels and ensure both ports will be deleted.
  TestChannel ch1(std::move(port1));
  TestChannel ch2(std::move(port2));

  // Simulate a connection that succeeds, and then is destroyed.
  StartConnectAndStopChannels(&ch1, &ch2);
  SIMULATED_WAIT(ports_destroyed() > 0, 80, clock);
  EXPECT_EQ(0, ports_destroyed());

  // Start the second set of connection and destroy them.
  ch1.CreateConnection(GetCandidate(ch2.port()));
  ch2.CreateConnection(GetCandidate(ch1.port()));
  ch1.Stop();
  ch2.Stop();

  SIMULATED_WAIT(ports_destroyed() > 0, 80, clock);
  EXPECT_EQ(0, ports_destroyed());

  // The ports on both sides should be destroyed after timeout.
  EXPECT_TRUE_SIMULATED_WAIT(ports_destroyed() == 2, 30, clock);
}

// This test case verifies that neither the controlling port nor the controlled
// port will time out after connectivity is lost if they are marked as "keep
// alive until pruned". They will time out after they are pruned.
TEST_F(PortTest, TestPortNotTimeoutUntilPruned) {
  rtc::ScopedFakeClock clock;
  int timeout_delay = 100;
  auto port1 = CreateUdpPort(kLocalAddr1);
  ConnectToSignalDestroyed(port1.get());
  port1->set_timeout_delay(timeout_delay);  // milliseconds
  port1->SetIceRole(cricket::ICEROLE_CONTROLLING);
  port1->SetIceTiebreaker(kTiebreaker1);

  auto port2 = CreateUdpPort(kLocalAddr2);
  ConnectToSignalDestroyed(port2.get());
  port2->set_timeout_delay(timeout_delay);  // milliseconds
  port2->SetIceRole(cricket::ICEROLE_CONTROLLED);
  port2->SetIceTiebreaker(kTiebreaker2);
  // The connection must not be destroyed before a connection is attempted.
  EXPECT_EQ(0, ports_destroyed());

  port1->set_component(cricket::ICE_CANDIDATE_COMPONENT_DEFAULT);
  port2->set_component(cricket::ICE_CANDIDATE_COMPONENT_DEFAULT);

  // Set up channels and keep the port alive.
  TestChannel ch1(std::move(port1));
  TestChannel ch2(std::move(port2));
  // Simulate a connection that succeeds, and then is destroyed. But ports
  // are kept alive. Ports won't be destroyed.
  StartConnectAndStopChannels(&ch1, &ch2);
  ch1.port()->KeepAliveUntilPruned();
  ch2.port()->KeepAliveUntilPruned();
  SIMULATED_WAIT(ports_destroyed() > 0, 150, clock);
  EXPECT_EQ(0, ports_destroyed());

  // If they are pruned now, they will be destroyed right away.
  ch1.port()->Prune();
  ch2.port()->Prune();
  // The ports on both sides should be destroyed after timeout.
  EXPECT_TRUE_SIMULATED_WAIT(ports_destroyed() == 2, 1, clock);
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
  port->SetIceTiebreaker(kTiebreakerDefault);
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
  port->SetIceTiebreaker(kTiebreakerDefault);
  port->PrepareAddress();
  EXPECT_EQ(1u, port->Candidates().size());
  rtc::SocketAddress address("1.1.1.1", 5000);
  cricket::Candidate candidate(1, "udp", address, 0, "", "",
                               IceCandidateType::kRelay, 0, "");
  cricket::Connection* conn1 =
      port->CreateConnection(candidate, Port::ORIGIN_MESSAGE);
  cricket::Connection* conn_in_use = port->GetConnection(address);
  EXPECT_EQ(conn1, conn_in_use);
  EXPECT_EQ(0u, conn_in_use->remote_candidate().generation());

  // Creating with a candidate with the same address again will get us a
  // different connection with the new candidate.
  candidate.set_generation(2);
  cricket::Connection* conn2 =
      port->CreateConnection(candidate, Port::ORIGIN_MESSAGE);
  EXPECT_NE(conn1, conn2);
  conn_in_use = port->GetConnection(address);
  EXPECT_EQ(conn2, conn_in_use);
  EXPECT_EQ(2u, conn_in_use->remote_candidate().generation());

  // Make sure the new connection was not deleted.
  rtc::Thread::Current()->ProcessMessages(300);
  EXPECT_TRUE(port->GetConnection(address) != nullptr);
}

// TODO(webrtc:11463) : Move Connection tests into separate unit test
// splitting out shared test code as needed.

class ConnectionTest : public PortTest {
 public:
  ConnectionTest() {
    lport_ = CreateTestPort(kLocalAddr1, "lfrag", "lpass");
    rport_ = CreateTestPort(kLocalAddr2, "rfrag", "rpass");
    lport_->SetIceRole(cricket::ICEROLE_CONTROLLING);
    lport_->SetIceTiebreaker(kTiebreaker1);
    rport_->SetIceRole(cricket::ICEROLE_CONTROLLED);
    rport_->SetIceTiebreaker(kTiebreaker2);

    lport_->PrepareAddress();
    rport_->PrepareAddress();
  }

  rtc::ScopedFakeClock clock_;
  int num_state_changes_ = 0;

  Connection* CreateConnection(IceRole role) {
    Connection* conn;
    if (role == cricket::ICEROLE_CONTROLLING) {
      conn = lport_->CreateConnection(rport_->Candidates()[0],
                                      Port::ORIGIN_MESSAGE);
    } else {
      conn = rport_->CreateConnection(lport_->Candidates()[0],
                                      Port::ORIGIN_MESSAGE);
    }
    conn->SignalStateChange.connect(this,
                                    &ConnectionTest::OnConnectionStateChange);
    return conn;
  }

  void SendPingAndCaptureReply(Connection* lconn,
                               Connection* rconn,
                               int64_t ms,
                               rtc::BufferT<uint8_t>* reply) {
    TestPort* lport =
        lconn->PortForTest() == lport_.get() ? lport_.get() : rport_.get();
    TestPort* rport =
        rconn->PortForTest() == rport_.get() ? rport_.get() : lport_.get();
    lconn->Ping(rtc::TimeMillis());
    ASSERT_TRUE_WAIT(lport->last_stun_msg(), kDefaultTimeout);
    ASSERT_GT(lport->last_stun_buf().size(), 0u);
    rconn->OnReadPacket(rtc::ReceivedPacket(
        lport->last_stun_buf(), rtc::SocketAddress(), absl::nullopt));

    clock_.AdvanceTime(webrtc::TimeDelta::Millis(ms));
    ASSERT_TRUE_WAIT(rport->last_stun_msg(), kDefaultTimeout);
    ASSERT_GT(rport->last_stun_buf().size(), 0u);
    reply->SetData(rport->last_stun_buf());
  }

  void SendPingAndReceiveResponse(Connection* lconn,
                                  Connection* rconn,
                                  int64_t ms) {
    rtc::BufferT<uint8_t> reply;
    SendPingAndCaptureReply(lconn, rconn, ms, &reply);

    lconn->OnReadPacket(
        rtc::ReceivedPacket(reply, rtc::SocketAddress(), absl::nullopt));
  }

  void OnConnectionStateChange(Connection* connection) { num_state_changes_++; }

  std::unique_ptr<TestPort> lport_;
  std::unique_ptr<TestPort> rport_;
};

TEST_F(ConnectionTest, ConnectionForgetLearnedState) {
  Connection* lconn = CreateConnection(ICEROLE_CONTROLLING);
  Connection* rconn = CreateConnection(ICEROLE_CONTROLLED);

  EXPECT_FALSE(lconn->writable());
  EXPECT_FALSE(lconn->receiving());
  EXPECT_TRUE(std::isnan(lconn->GetRttEstimate().GetAverage()));
  EXPECT_EQ(lconn->GetRttEstimate().GetVariance(),
            std::numeric_limits<double>::infinity());

  SendPingAndReceiveResponse(lconn, rconn, 10);

  EXPECT_TRUE(lconn->writable());
  EXPECT_TRUE(lconn->receiving());
  EXPECT_EQ(lconn->GetRttEstimate().GetAverage(), 10);
  EXPECT_EQ(lconn->GetRttEstimate().GetVariance(),
            std::numeric_limits<double>::infinity());

  SendPingAndReceiveResponse(lconn, rconn, 11);

  EXPECT_TRUE(lconn->writable());
  EXPECT_TRUE(lconn->receiving());
  EXPECT_NEAR(lconn->GetRttEstimate().GetAverage(), 10, 0.5);
  EXPECT_LT(lconn->GetRttEstimate().GetVariance(),
            std::numeric_limits<double>::infinity());

  lconn->ForgetLearnedState();

  EXPECT_FALSE(lconn->writable());
  EXPECT_FALSE(lconn->receiving());
  EXPECT_TRUE(std::isnan(lconn->GetRttEstimate().GetAverage()));
  EXPECT_EQ(lconn->GetRttEstimate().GetVariance(),
            std::numeric_limits<double>::infinity());
}

TEST_F(ConnectionTest, ConnectionForgetLearnedStateDiscardsPendingPings) {
  Connection* lconn = CreateConnection(ICEROLE_CONTROLLING);
  Connection* rconn = CreateConnection(ICEROLE_CONTROLLED);

  SendPingAndReceiveResponse(lconn, rconn, 10);

  EXPECT_TRUE(lconn->writable());
  EXPECT_TRUE(lconn->receiving());

  rtc::BufferT<uint8_t> reply;
  SendPingAndCaptureReply(lconn, rconn, 10, &reply);

  lconn->ForgetLearnedState();

  EXPECT_FALSE(lconn->writable());
  EXPECT_FALSE(lconn->receiving());

  lconn->OnReadPacket(
      rtc::ReceivedPacket(reply, rtc::SocketAddress(), absl::nullopt));

  // That reply was discarded due to the ForgetLearnedState() while it was
  // outstanding.
  EXPECT_FALSE(lconn->writable());
  EXPECT_FALSE(lconn->receiving());

  // But sending a new ping and getting a reply works.
  SendPingAndReceiveResponse(lconn, rconn, 11);
  EXPECT_TRUE(lconn->writable());
  EXPECT_TRUE(lconn->receiving());
}

TEST_F(ConnectionTest, ConnectionForgetLearnedStateDoesNotTriggerStateChange) {
  Connection* lconn = CreateConnection(ICEROLE_CONTROLLING);
  Connection* rconn = CreateConnection(ICEROLE_CONTROLLED);

  EXPECT_EQ(num_state_changes_, 0);
  SendPingAndReceiveResponse(lconn, rconn, 10);

  EXPECT_TRUE(lconn->writable());
  EXPECT_TRUE(lconn->receiving());
  EXPECT_EQ(num_state_changes_, 2);

  lconn->ForgetLearnedState();

  EXPECT_FALSE(lconn->writable());
  EXPECT_FALSE(lconn->receiving());
  EXPECT_EQ(num_state_changes_, 2);
}

// Test normal happy case.
// Sending a delta and getting a delta ack in response.
TEST_F(ConnectionTest, SendReceiveGoogDelta) {
  constexpr int64_t ms = 10;
  Connection* lconn = CreateConnection(ICEROLE_CONTROLLING);
  Connection* rconn = CreateConnection(ICEROLE_CONTROLLED);

  std::unique_ptr<StunByteStringAttribute> delta =
      absl::WrapUnique(new StunByteStringAttribute(STUN_ATTR_GOOG_DELTA));
  delta->CopyBytes("DELTA");

  std::unique_ptr<StunAttribute> delta_ack =
      absl::WrapUnique(new StunUInt64Attribute(STUN_ATTR_GOOG_DELTA_ACK, 133));

  bool received_goog_delta = false;
  bool received_goog_delta_ack = false;
  lconn->SetStunDictConsumer(
      // DeltaReceived
      [](const StunByteStringAttribute* delta)
          -> std::unique_ptr<StunAttribute> { return nullptr; },
      // DeltaAckReceived
      [&](webrtc::RTCErrorOr<const StunUInt64Attribute*> error_or_ack) {
        received_goog_delta_ack = true;
        EXPECT_TRUE(error_or_ack.ok());
        EXPECT_EQ(error_or_ack.value()->value(), 133ull);
      });

  rconn->SetStunDictConsumer(
      // DeltaReceived
      [&](const StunByteStringAttribute* delta)
          -> std::unique_ptr<StunAttribute> {
        received_goog_delta = true;
        EXPECT_EQ(delta->string_view(), "DELTA");
        return std::move(delta_ack);
      },
      // DeltaAckReceived
      [](webrtc::RTCErrorOr<const StunUInt64Attribute*> error_or__ack) {});

  lconn->Ping(rtc::TimeMillis(), std::move(delta));
  ASSERT_TRUE_WAIT(lport_->last_stun_msg(), kDefaultTimeout);
  ASSERT_GT(lport_->last_stun_buf().size(), 0u);
  rconn->OnReadPacket(rtc::ReceivedPacket(lport_->last_stun_buf(),
                                          rtc::SocketAddress(), absl::nullopt));
  EXPECT_TRUE(received_goog_delta);

  clock_.AdvanceTime(webrtc::TimeDelta::Millis(ms));
  ASSERT_TRUE_WAIT(rport_->last_stun_msg(), kDefaultTimeout);
  ASSERT_GT(rport_->last_stun_buf().size(), 0u);
  lconn->OnReadPacket(rtc::ReceivedPacket(rport_->last_stun_buf(),
                                          rtc::SocketAddress(), absl::nullopt));

  EXPECT_TRUE(received_goog_delta_ack);
}

// Test that sending a goog delta and not getting
// a delta ack in reply gives an error callback.
TEST_F(ConnectionTest, SendGoogDeltaNoReply) {
  constexpr int64_t ms = 10;
  Connection* lconn = CreateConnection(ICEROLE_CONTROLLING);
  Connection* rconn = CreateConnection(ICEROLE_CONTROLLED);

  std::unique_ptr<StunByteStringAttribute> delta =
      absl::WrapUnique(new StunByteStringAttribute(STUN_ATTR_GOOG_DELTA));
  delta->CopyBytes("DELTA");

  bool received_goog_delta_ack_error = false;
  lconn->SetStunDictConsumer(
      // DeltaReceived
      [](const StunByteStringAttribute* delta)
          -> std::unique_ptr<StunAttribute> { return nullptr; },
      // DeltaAckReceived
      [&](webrtc::RTCErrorOr<const StunUInt64Attribute*> error_or_ack) {
        received_goog_delta_ack_error = true;
        EXPECT_FALSE(error_or_ack.ok());
      });

  lconn->Ping(rtc::TimeMillis(), std::move(delta));
  ASSERT_TRUE_WAIT(lport_->last_stun_msg(), kDefaultTimeout);
  ASSERT_GT(lport_->last_stun_buf().size(), 0u);
  rconn->OnReadPacket(rtc::ReceivedPacket(lport_->last_stun_buf(),
                                          rtc::SocketAddress(), absl::nullopt));

  clock_.AdvanceTime(webrtc::TimeDelta::Millis(ms));
  ASSERT_TRUE_WAIT(rport_->last_stun_msg(), kDefaultTimeout);
  ASSERT_GT(rport_->last_stun_buf().size(), 0u);
  lconn->OnReadPacket(rtc::ReceivedPacket(rport_->last_stun_buf(),
                                          rtc::SocketAddress(), absl::nullopt));
  EXPECT_TRUE(received_goog_delta_ack_error);
}

}  // namespace cricket
