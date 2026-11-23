/*
 *  Copyright 2009 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "p2p/base/p2p_transport_channel.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/async_dns_resolver.h"
#include "api/candidate.h"
#include "api/environment/environment.h"
#include "api/ice_transport_interface.h"
#include "api/packet_socket_factory.h"
#include "api/scoped_refptr.h"
#include "api/task_queue/pending_task_safety_flag.h"
#include "api/test/mock_async_dns_resolver.h"
#include "api/test/mock_local_network_access_permission.h"
#include "api/test/rtc_error_matchers.h"
#include "api/transport/enums.h"
#include "api/transport/stun.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "p2p/base/basic_ice_controller.h"
#include "p2p/base/basic_packet_socket_factory.h"
#include "p2p/base/candidate_pair_interface.h"
#include "p2p/base/connection.h"
#include "p2p/base/connection_info.h"
#include "p2p/base/ice_controller_factory_interface.h"
#include "p2p/base/ice_controller_interface.h"
#include "p2p/base/ice_switch_reason.h"
#include "p2p/base/ice_transport_internal.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/packet_transport_internal.h"
#include "p2p/base/port.h"
#include "p2p/base/port_allocator.h"
#include "p2p/base/port_interface.h"
#include "p2p/base/stun_dictionary.h"
#include "p2p/base/transport_description.h"
#include "p2p/client/basic_port_allocator.h"
#include "p2p/dtls/dtls_stun_piggyback_callbacks.h"
#include "p2p/test/fake_port_allocator.h"
#include "p2p/test/mock_active_ice_controller.h"
#include "p2p/test/mock_ice_controller.h"
#include "p2p/test/nat_socket_factory.h"
#include "p2p/test/nat_types.h"
#include "p2p/test/stun_server.h"
#include "p2p/test/test_stun_server.h"
#include "p2p/test/test_turn_server.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/buffer.h"
#include "rtc_base/byte_buffer.h"
#include "rtc_base/checks.h"
#include "rtc_base/dscp.h"
#include "rtc_base/fake_mdns_responder.h"
#include "rtc_base/fake_network.h"
#include "rtc_base/firewall_socket_server.h"
#include "rtc_base/internal/default_socket_server.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/logging.h"
#include "rtc_base/mdns_responder_interface.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/net_helpers.h"
#include "rtc_base/network.h"
#include "rtc_base/network/received_packet.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/network_constants.h"
#include "rtc_base/network_route.h"
#include "rtc_base/socket.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/socket_server.h"
#include "rtc_base/thread.h"
#include "rtc_base/virtual_socket_server.h"
#include "system_wrappers/include/metrics.h"
#include "test/create_test_environment.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/run_loop.h"
#include "test/time_controller/simulated_time_controller.h"
#include "test/wait_until.h"

namespace webrtc {
namespace {

using ::testing::_;
using ::testing::Assign;
using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::MockFunction;
using ::testing::Ne;
using ::testing::NotNull;
using ::testing::Property;
using ::testing::Return;
using ::testing::ReturnRef;
using ::testing::SetArgPointee;
using ::testing::SizeIs;
using ::testing::Values;
using ::testing::WithParamInterface;
using LnaFakeResult = FakeLocalNetworkAccessPermissionFactory::Result;

// Default timeout for tests in this file.
// Should be large enough for slow buildbots to run the tests reliably.
constexpr TimeDelta kDefaultTimeout = TimeDelta::Seconds(10);
constexpr TimeDelta kMediumTimeout = TimeDelta::Seconds(3);
constexpr TimeDelta kShortTimeout = TimeDelta::Seconds(1);

constexpr int kOnlyLocalPorts = PORTALLOCATOR_DISABLE_STUN |
                                PORTALLOCATOR_DISABLE_RELAY |
                                PORTALLOCATOR_DISABLE_TCP;
constexpr TimeDelta kLowRtt = TimeDelta::Millis(20);
// Addresses on the public internet.
const SocketAddress kPublicAddrs[2] = {SocketAddress("11.11.11.11", 0),
                                       SocketAddress("22.22.22.22", 0)};
// IPv6 Addresses on the public internet.
const SocketAddress kIPv6PublicAddrs[2] = {
    SocketAddress("2400:4030:1:2c00:be30:abcd:efab:cdef", 0),
    SocketAddress("2600:0:1000:1b03:2e41:38ff:fea6:f2a4", 0)};
// For configuring multihomed clients.
const SocketAddress kAlternateAddrs[2] = {SocketAddress("101.101.101.101", 0),
                                          SocketAddress("202.202.202.202", 0)};
const SocketAddress kIPv6AlternateAddrs[2] = {
    SocketAddress("2401:4030:1:2c00:be30:abcd:efab:cdef", 0),
    SocketAddress("2601:0:1000:1b03:2e41:38ff:fea6:f2a4", 0)};
// Internal addresses for NAT boxes.
const SocketAddress kNatAddrs[2] = {SocketAddress("192.168.1.1", 0),
                                    SocketAddress("192.168.2.1", 0)};
// Private addresses inside the NAT private networks.
const SocketAddress kPrivateAddrs[2] = {SocketAddress("192.168.1.11", 0),
                                        SocketAddress("192.168.2.22", 0)};
// For cascaded NATs, the internal addresses of the inner NAT boxes.
const SocketAddress kCascadedNatAddrs[2] = {SocketAddress("192.168.10.1", 0),
                                            SocketAddress("192.168.20.1", 0)};
// For cascaded NATs, private addresses inside the inner private networks.
const SocketAddress kCascadedPrivateAddrs[2] = {
    SocketAddress("192.168.10.11", 0), SocketAddress("192.168.20.22", 0)};
// The address of the public STUN server.
const SocketAddress kStunAddr("99.99.99.1", STUN_SERVER_PORT);
// The addresses for the public turn server.
const SocketAddress kTurnUdpIntAddr("99.99.99.3", STUN_SERVER_PORT);
const SocketAddress kTurnTcpIntAddr("99.99.99.4", STUN_SERVER_PORT + 1);
const SocketAddress kTurnUdpExtAddr("99.99.99.5", 0);
const RelayCredentials kRelayCredentials("test", "test");

// Based on ICE_UFRAG_LENGTH
const char* kIceUfrag[4] = {"UF00", "UF01", "UF02", "UF03"};
// Based on ICE_PWD_LENGTH
const char* kIcePwd[4] = {
    "TESTICEPWD00000000000000", "TESTICEPWD00000000000001",
    "TESTICEPWD00000000000002", "TESTICEPWD00000000000003"};
const IceParameters kIceParams[4] = {{kIceUfrag[0], kIcePwd[0], false},
                                     {kIceUfrag[1], kIcePwd[1], false},
                                     {kIceUfrag[2], kIcePwd[2], false},
                                     {kIceUfrag[3], kIcePwd[3], false}};

IceConfig CreateIceConfig(
    TimeDelta receiving_timeout,
    ContinualGatheringPolicy continual_gathering_policy,
    std::optional<TimeDelta> backup_ping_interval = std::nullopt) {
  IceConfig config;
  config.receiving_timeout = receiving_timeout;
  config.continual_gathering_policy = continual_gathering_policy;
  config.backup_connection_ping_interval = backup_ping_interval;
  return config;
}

Candidate CreateUdpCandidate(IceCandidateType type,
                             absl::string_view ip,
                             int port,
                             int priority,
                             absl::string_view ufrag = "") {
  Candidate c;
  c.set_address(SocketAddress(ip, port));
  c.set_component(ICE_CANDIDATE_COMPONENT_DEFAULT);
  c.set_protocol(UDP_PROTOCOL_NAME);
  c.set_priority(priority);
  c.set_username(ufrag);
  c.set_type(type);
  return c;
}

std::unique_ptr<BasicPortAllocator> CreateBasicPortAllocator(
    const Environment& env_,
    NetworkManager* network_manager,
    PacketSocketFactory* socket_factory,
    const ServerAddresses& stun_servers,
    const SocketAddress& turn_server_udp,
    const SocketAddress& turn_server_tcp) {
  RelayServerConfig turn_server;
  turn_server.credentials = kRelayCredentials;
  if (!turn_server_udp.IsNil()) {
    turn_server.ports.push_back(ProtocolAddress(turn_server_udp, PROTO_UDP));
  }
  if (!turn_server_tcp.IsNil()) {
    turn_server.ports.push_back(ProtocolAddress(turn_server_tcp, PROTO_TCP));
  }
  std::vector<RelayServerConfig> turn_servers(1, turn_server);

  auto allocator = std::make_unique<BasicPortAllocator>(env_, network_manager,
                                                        socket_factory);
  allocator->Initialize();
  allocator->SetConfiguration(stun_servers, turn_servers, 0, NO_PRUNE);
  return allocator;
}

// An one-shot resolver factory with default return arguments.
// Resolution is immediate, always succeeds, and returns nonsense.
class ResolverFactoryFixture : public MockAsyncDnsResolverFactory {
 public:
  ResolverFactoryFixture() {
    mock_async_dns_resolver_ = std::make_unique<MockAsyncDnsResolver>();
    EXPECT_CALL(*mock_async_dns_resolver_, Start(_, _))
        .WillRepeatedly(
            [](const SocketAddress& /* addr */,
               absl::AnyInvocable<void()> callback) { callback(); });
    EXPECT_CALL(*mock_async_dns_resolver_, result())
        .WillOnce(ReturnRef(mock_async_dns_resolver_result_));

    // A default action for GetResolvedAddress. Will be overruled
    // by SetAddressToReturn.
    EXPECT_CALL(mock_async_dns_resolver_result_, GetResolvedAddress(_, _))
        .WillRepeatedly(Return(true));

    EXPECT_CALL(mock_async_dns_resolver_result_, GetError())
        .WillOnce(Return(0));
    EXPECT_CALL(*this, Create()).WillOnce([this]() {
      return std::move(mock_async_dns_resolver_);
    });
  }

  void SetAddressToReturn(SocketAddress address_to_return) {
    EXPECT_CALL(mock_async_dns_resolver_result_, GetResolvedAddress(_, _))
        .WillOnce([address_to_return](int /*family*/, SocketAddress* addr) {
          *addr = address_to_return;
          return true;
        });
  }
  void DelayResolution() {
    // This function must be called before Create().
    ASSERT_TRUE(!!mock_async_dns_resolver_);
    EXPECT_CALL(*mock_async_dns_resolver_, Start(_, _))
        .WillOnce([this](const SocketAddress& addr,
                         absl::AnyInvocable<void()> callback) {
          saved_callback_ = std::move(callback);
        });
  }
  void FireDelayedResolution() {
    // This function must be called after Create().
    ASSERT_TRUE(saved_callback_);
    saved_callback_();
  }

 private:
  std::unique_ptr<MockAsyncDnsResolver> mock_async_dns_resolver_;
  MockAsyncDnsResolverResult mock_async_dns_resolver_result_;
  absl::AnyInvocable<void()> saved_callback_;
};

bool HasLocalAddress(const CandidatePairInterface* pair,
                     const SocketAddress& address) {
  return pair->local_candidate().address().EqualIPs(address);
}

bool HasRemoteAddress(const CandidatePairInterface* pair,
                      const SocketAddress& address) {
  return pair->remote_candidate().address().EqualIPs(address);
}

// This test simulates 2 P2P endpoints that want to establish connectivity
// with each other over various network topologies and conditions, which can be
// specified in each individial test.
// A virtual network (via VirtualSocketServer) along with virtual firewalls and
// NATs (via Firewall/NATSocketServer) are used to simulate the various network
// conditions. We can configure the IP addresses of the endpoints,
// block various types of connectivity, or add arbitrary levels of NAT.
// We also run a STUN server and a relay server on the virtual network to allow
// our typical P2P mechanisms to do their thing.
// For each case, we expect the P2P stack to eventually settle on a specific
// form of connectivity to the other side. The test checks that the P2P
// negotiation successfully establishes connectivity within a certain time,
// and that the result is what we expect.
// Note that this class is a base class for use by other tests, who will provide
// specialized test behavior.
class P2PTransportChannelTestBase : public ::testing::Test {
 public:
  P2PTransportChannelTestBase()
      : vss_(new VirtualSocketServer()),
        nss_(new NATSocketServer(vss_.get())),
        ss_(new FirewallSocketServer(nss_.get())),
        time_controller_(Timestamp::Millis(1000), ss_.get()),
        env_(CreateTestEnvironment({.time = &time_controller_})),
        socket_factory_(new BasicPacketSocketFactory(ss_.get())),
        main_(time_controller_.GetMainThread()),
        ep1_(main_),
        ep2_(main_),
        force_relay_(false) {
    ep1_.SetIceRole(ICEROLE_CONTROLLING);
    ep2_.SetIceRole(ICEROLE_CONTROLLED);

    metrics::Reset();
  }

  void CreatePortAllocators() {
    stun_server_ = TestStunServer::Create(env_, kStunAddr, *ss_, *main_);
    turn_server_.emplace(env_, main_, ss_.get(), kTurnUdpIntAddr,
                         kTurnUdpExtAddr);
    ServerAddresses stun_servers = {kStunAddr};
    ep1_.set_allocator(CreateBasicPortAllocator(
        env_, &ep1_.network_manager(), socket_factory_.get(), stun_servers,
        kTurnUdpIntAddr, SocketAddress()));
    ep2_.set_allocator(CreateBasicPortAllocator(
        env_, &ep2_.network_manager(), socket_factory_.get(), stun_servers,
        kTurnUdpIntAddr, SocketAddress()));
  }

 protected:
  enum Config {
    OPEN,                         // Open to the Internet
    NAT_FULL_CONE,                // NAT, no filtering
    NAT_ADDR_RESTRICTED,          // NAT, must send to an addr to recv
    NAT_PORT_RESTRICTED,          // NAT, must send to an addr+port to recv
    NAT_SYMMETRIC,                // NAT, endpoint-dependent bindings
    NAT_DOUBLE_CONE,              // Double NAT, both cone
    NAT_SYMMETRIC_THEN_CONE,      // Double NAT, symmetric outer, cone inner
    BLOCK_UDP,                    // Firewall, UDP in/out blocked
    BLOCK_UDP_AND_INCOMING_TCP,   // Firewall, UDP in/out and TCP in blocked
    BLOCK_ALL_BUT_OUTGOING_HTTP,  // Firewall, only TCP out on 80/443
    NUM_CONFIGS
  };

  struct Result {
    Result(IceCandidateType controlling_type,
           absl::string_view controlling_protocol,
           IceCandidateType controlled_type,
           absl::string_view controlled_protocol,
           TimeDelta wait)
        : controlling_type(controlling_type),
          controlling_protocol(controlling_protocol),
          controlled_type(controlled_type),
          controlled_protocol(controlled_protocol),
          connect_wait(wait) {}

    // The expected candidate type and protocol of the controlling ICE agent.
    IceCandidateType controlling_type;
    std::string controlling_protocol;
    // The expected candidate type and protocol of the controlled ICE agent.
    IceCandidateType controlled_type;
    std::string controlled_protocol;
    // How long to wait before the correct candidate pair is selected.
    TimeDelta connect_wait;
  };

  class ChannelData {
   public:
    bool CheckData(const char* data, int len) {
      bool ret = false;
      if (!ch_packets_.empty()) {
        std::string packet = ch_packets_.front();
        ret = (packet == std::string(data, len));
        ch_packets_.pop_front();
      }
      return ret;
    }

    void set_ch(std::unique_ptr<P2PTransportChannel> ch) {
      ch_ = std::move(ch);
    }
    void reset_ch() { ch_.reset(); }
    P2PTransportChannel* ch() const { return ch_.get(); }
    std::list<std::string>& ch_packets() { return ch_packets_; }

   private:
    std::string name_;  // TODO(?) - Currently not used.
    std::list<std::string> ch_packets_;
    std::unique_ptr<P2PTransportChannel> ch_;
  };

  struct CandidateData {
    IceTransportInternal* channel;
    Candidate candidate;
  };

  class Endpoint {
   public:
    explicit Endpoint(Thread* thread)
        : network_manager_(thread),
          role_(ICEROLE_UNKNOWN),
          role_conflict_(false),
          save_candidates_(false) {}
    bool HasTransport(const PacketTransportInternal* transport) const {
      return (transport == cd1_.ch() || transport == cd2_.ch());
    }
    ChannelData* GetChannelData(PacketTransportInternal* transport) {
      if (!HasTransport(transport))
        return nullptr;
      if (cd1_.ch() == transport)
        return &cd1_;
      else
        return &cd2_;
    }

    void SetIceRole(IceRole role) { role_ = role; }
    IceRole ice_role() const { return role_; }
    void OnRoleConflict(bool role_conflict) { role_conflict_ = role_conflict; }
    bool role_conflict() const { return role_conflict_; }
    void SetAllocationStepDelay(uint32_t delay) {
      allocator_->set_step_delay(delay);
    }
    void SetAllowTcpListen(bool allow_tcp_listen) {
      allocator_->set_allow_tcp_listen(allow_tcp_listen);
    }

    void OnIceRegathering(PortAllocatorSession*, IceRegatheringReason reason) {
      ++ice_regathering_counter_[reason];
    }

    int GetIceRegatheringCountForReason(IceRegatheringReason reason) const {
      auto it = ice_regathering_counter_.find(reason);
      return it == ice_regathering_counter_.end() ? 0 : it->second;
    }

    FakeNetworkManager& network_manager() { return network_manager_; }
    void set_allocator(std::unique_ptr<BasicPortAllocator> allocator) {
      allocator_ = std::move(allocator);
    }
    BasicPortAllocator* allocator() const { return allocator_.get(); }
    void set_async_dns_resolver_factory(
        AsyncDnsResolverFactoryInterface* async_dns_resolver_factory) {
      async_dns_resolver_factory_ = async_dns_resolver_factory;
    }
    AsyncDnsResolverFactoryInterface* async_dns_resolver_factory() const {
      return async_dns_resolver_factory_;
    }
    ChannelData& cd1() { return cd1_; }
    ChannelData& cd2() { return cd2_; }
    void set_save_candidates(bool save) { save_candidates_ = save; }
    bool save_candidates() const { return save_candidates_; }
    std::vector<CandidateData>& saved_candidates() { return saved_candidates_; }
    void set_ready_to_send(bool ready) { ready_to_send_ = ready; }
    bool ready_to_send() const { return ready_to_send_; }

   private:
    FakeNetworkManager network_manager_;
    std::unique_ptr<BasicPortAllocator> allocator_;
    AsyncDnsResolverFactoryInterface* async_dns_resolver_factory_ = nullptr;
    ChannelData cd1_;
    ChannelData cd2_;
    IceRole role_;
    bool role_conflict_;
    bool save_candidates_;
    std::vector<CandidateData> saved_candidates_;
    bool ready_to_send_ = false;
    std::map<IceRegatheringReason, int> ice_regathering_counter_;
  };

  ChannelData* GetChannelData(PacketTransportInternal* transport) {
    if (ep1_.HasTransport(transport))
      return ep1_.GetChannelData(transport);
    else
      return ep2_.GetChannelData(transport);
  }

  IceParameters IceParamsWithRenomination(const IceParameters& ice,
                                          bool renomination) {
    IceParameters new_ice = ice;
    new_ice.renomination = renomination;
    return new_ice;
  }

  Waiter DefaultWait() {
    return Waiter({.timeout = kDefaultTimeout, .clock = &time_controller_});
  }
  Waiter MediumWait() {
    return Waiter({.timeout = kMediumTimeout, .clock = &time_controller_});
  }
  Waiter ShortWait() {
    return Waiter({.timeout = kShortTimeout, .clock = &time_controller_});
  }
  Waiter Wait(TimeDelta timeout) {
    return Waiter({.timeout = timeout, .clock = &time_controller_});
  }

  void CreateChannels(const IceConfig& ep1_config,
                      const IceConfig& ep2_config,
                      bool renomination = false) {
    IceParameters ice_ep1_cd1_ch =
        IceParamsWithRenomination(kIceParams[0], renomination);
    IceParameters ice_ep2_cd1_ch =
        IceParamsWithRenomination(kIceParams[1], renomination);
    ep1_.cd1().set_ch(CreateChannel(0, ICE_CANDIDATE_COMPONENT_DEFAULT,
                                    ice_ep1_cd1_ch, ice_ep2_cd1_ch));
    ep2_.cd1().set_ch(CreateChannel(1, ICE_CANDIDATE_COMPONENT_DEFAULT,
                                    ice_ep2_cd1_ch, ice_ep1_cd1_ch));
    ep1_.cd1().ch()->SetIceConfig(ep1_config);
    ep2_.cd1().ch()->SetIceConfig(ep2_config);
    ep1_.cd1().ch()->MaybeStartGathering();
    ep2_.cd1().ch()->MaybeStartGathering();
    ep1_.cd1().ch()->allocator_session()->SubscribeIceRegathering(
        this, [this](PortAllocatorSession* allocator_session,
                     IceRegatheringReason reason) {
          ep1_.OnIceRegathering(allocator_session, reason);
        });
    ep2_.cd1().ch()->allocator_session()->SubscribeIceRegathering(
        this, [this](PortAllocatorSession* allocator_session,
                     IceRegatheringReason reason) {
          ep2_.OnIceRegathering(allocator_session, reason);
        });
  }

  void CreateChannels() {
    IceConfig default_config;
    CreateChannels(default_config, default_config, false);
  }

  std::unique_ptr<P2PTransportChannel> CreateChannel(
      int endpoint,
      int component,
      const IceParameters& local_ice,
      const IceParameters& remote_ice) {
    IceTransportInit init(env_);
    init.set_port_allocator(GetAllocator(endpoint));
    init.set_async_dns_resolver_factory(
        GetEndpoint(endpoint)->async_dns_resolver_factory());
    auto channel = P2PTransportChannel::Create("test content name", component,
                                               std::move(init));
    channel->SubscribeReadyToSend(this,
                                  [this](PacketTransportInternal* transport) {
                                    OnReadyToSend(transport);
                                  });
    channel->SubscribeCandidateGathered(
        this,
        [this](IceTransportInternal* transport, const Candidate& candidate) {
          OnCandidateGathered(transport, candidate);
        });
    channel->SetCandidatesRemovedCallback(
        [this](IceTransportInternal* transport, const Candidates& candidates) {
          OnCandidatesRemoved(transport, candidates);
        });
    channel->RegisterReceivedPacketCallback(
        this, [&](PacketTransportInternal* transport,
                  const ReceivedIpPacket& packet) {
          OnReadPacket(transport, packet);
        });
    channel->SubscribeRoleConflict(
        this,
        [this](IceTransportInternal* transport) { OnRoleConflict(transport); });
    channel->SubscribeNetworkRouteChanged(
        this, [this](std::optional<NetworkRoute> network_route) {
          OnNetworkRouteChanged(network_route);
        });
    channel->SubscribeSentPacket(
        this,
        [this](PacketTransportInternal* transport, const SentPacketInfo& info) {
          OnSentPacket(transport, info);
        });
    channel->SetIceParameters(local_ice);
    if (remote_ice_parameter_source_ == FROM_SETICEPARAMETERS) {
      channel->SetRemoteIceParameters(remote_ice);
    }
    channel->SetIceRole(GetEndpoint(endpoint)->ice_role());
    return channel;
  }

  void DestroyChannels() {
    safety_->SetNotAlive();
    ep1_.cd1().reset_ch();
    ep2_.cd1().reset_ch();
    ep1_.cd2().reset_ch();
    ep2_.cd2().reset_ch();
    // Process pending tasks that need to run for cleanup purposes such as
    // pending deletion of Connection objects (see Connection::Destroy).
    Thread::Current()->ProcessMessages(0);
  }
  P2PTransportChannel* ep1_ch1() { return ep1_.cd1().ch(); }
  P2PTransportChannel* ep1_ch2() { return ep1_.cd2().ch(); }
  P2PTransportChannel* ep2_ch1() { return ep2_.cd1().ch(); }
  P2PTransportChannel* ep2_ch2() { return ep2_.cd2().ch(); }

  TestTurnServer* test_turn_server() {
    EXPECT_TRUE(turn_server_.has_value());
    return &*turn_server_;
  }
  VirtualSocketServer* virtual_socket_server() { return vss_.get(); }

  // Common results.
  static const Result kLocalUdpToLocalUdp;
  static const Result kLocalUdpToStunUdp;
  static const Result kLocalUdpToPrflxUdp;
  static const Result kPrflxUdpToLocalUdp;
  static const Result kStunUdpToLocalUdp;
  static const Result kStunUdpToStunUdp;
  static const Result kStunUdpToPrflxUdp;
  static const Result kPrflxUdpToStunUdp;
  static const Result kLocalUdpToRelayUdp;
  static const Result kPrflxUdpToRelayUdp;
  static const Result kRelayUdpToPrflxUdp;
  static const Result kLocalTcpToLocalTcp;
  static const Result kLocalTcpToPrflxTcp;
  static const Result kPrflxTcpToLocalTcp;

  NATSocketServer* nat() { return nss_.get(); }
  FirewallSocketServer* fw() { return ss_.get(); }

  Endpoint* GetEndpoint(int endpoint) {
    if (endpoint == 0) {
      return &ep1_;
    } else if (endpoint == 1) {
      return &ep2_;
    } else {
      return nullptr;
    }
  }
  BasicPortAllocator* GetAllocator(int endpoint) {
    return GetEndpoint(endpoint)->allocator();
  }
  void AddAddress(int endpoint, const SocketAddress& addr) {
    GetEndpoint(endpoint)->network_manager().AddInterface(addr);
  }
  void AddAddress(
      int endpoint,
      const SocketAddress& addr,
      absl::string_view ifname,
      AdapterType adapter_type,
      std::optional<AdapterType> underlying_vpn_adapter_type = std::nullopt) {
    GetEndpoint(endpoint)->network_manager().AddInterface(
        addr, ifname, adapter_type, underlying_vpn_adapter_type);
  }
  void RemoveAddress(int endpoint, const SocketAddress& addr) {
    GetEndpoint(endpoint)->network_manager().RemoveInterface(addr);
    fw()->AddRule(false, FP_ANY, FD_ANY, addr);
  }
  void SetAllocatorFlags(int endpoint, int flags) {
    GetAllocator(endpoint)->set_flags(flags);
  }
  void SetIceRole(int endpoint, IceRole role) {
    GetEndpoint(endpoint)->SetIceRole(role);
  }
  bool GetRoleConflict(int endpoint) {
    return GetEndpoint(endpoint)->role_conflict();
  }
  void SetAllocationStepDelay(int endpoint, uint32_t delay) {
    return GetEndpoint(endpoint)->SetAllocationStepDelay(delay);
  }
  void SetAllowTcpListen(int endpoint, bool allow_tcp_listen) {
    return GetEndpoint(endpoint)->SetAllowTcpListen(allow_tcp_listen);
  }

  // Return true if the appropriate parts of the expected Result, based
  // on the local and remote candidate of ep1_ch1, match.  This can be
  // used in an EXPECT_TRUE_WAIT.
  bool CheckCandidate1(const Result& expected) {
    auto local_type = LocalCandidate(ep1_ch1())->type();
    const std::string& local_protocol = LocalCandidate(ep1_ch1())->protocol();
    auto remote_type = RemoteCandidate(ep1_ch1())->type();
    const std::string& remote_protocol = RemoteCandidate(ep1_ch1())->protocol();
    return (local_protocol == expected.controlling_protocol &&
            remote_protocol == expected.controlled_protocol &&
            local_type == expected.controlling_type &&
            remote_type == expected.controlled_type);
  }

  // EXPECT_EQ on the appropriate parts of the expected Result, based
  // on the local and remote candidate of ep1_ch1.  This is like
  // CheckCandidate1, except that it will provide more detail about
  // what didn't match.
  void ExpectCandidate1(const Result& expected) {
    if (CheckCandidate1(expected)) {
      return;
    }

    auto local_type = LocalCandidate(ep1_ch1())->type();
    const std::string& local_protocol = LocalCandidate(ep1_ch1())->protocol();
    auto remote_type = RemoteCandidate(ep1_ch1())->type();
    const std::string& remote_protocol = RemoteCandidate(ep1_ch1())->protocol();
    EXPECT_EQ(expected.controlling_type, local_type);
    EXPECT_EQ(expected.controlled_type, remote_type);
    EXPECT_EQ(expected.controlling_protocol, local_protocol);
    EXPECT_EQ(expected.controlled_protocol, remote_protocol);
  }

  // Return true if the appropriate parts of the expected Result, based
  // on the local and remote candidate of ep2_ch1, match.  This can be
  // used in an EXPECT_TRUE_WAIT.
  bool CheckCandidate2(const Result& expected) {
    auto local_type = LocalCandidate(ep2_ch1())->type();
    const std::string& local_protocol = LocalCandidate(ep2_ch1())->protocol();
    auto remote_type = RemoteCandidate(ep2_ch1())->type();
    const std::string& remote_protocol = RemoteCandidate(ep2_ch1())->protocol();
    return (local_protocol == expected.controlled_protocol &&
            remote_protocol == expected.controlling_protocol &&
            local_type == expected.controlled_type &&
            remote_type == expected.controlling_type);
  }

  // EXPECT_EQ on the appropriate parts of the expected Result, based
  // on the local and remote candidate of ep2_ch1.  This is like
  // CheckCandidate2, except that it will provide more detail about
  // what didn't match.
  void ExpectCandidate2(const Result& expected) {
    if (CheckCandidate2(expected)) {
      return;
    }

    auto local_type = LocalCandidate(ep2_ch1())->type();
    const std::string& local_protocol = LocalCandidate(ep2_ch1())->protocol();
    auto remote_type = RemoteCandidate(ep2_ch1())->type();
    const std::string& remote_protocol = RemoteCandidate(ep2_ch1())->protocol();
    EXPECT_EQ(expected.controlled_type, local_type);
    EXPECT_EQ(expected.controlling_type, remote_type);
    EXPECT_EQ(expected.controlled_protocol, local_protocol);
    EXPECT_EQ(expected.controlling_protocol, remote_protocol);
  }

  static bool CheckCandidate(P2PTransportChannel* channel,
                             SocketAddress from,
                             SocketAddress to) {
    auto local_candidate = LocalCandidate(channel);
    auto remote_candidate = RemoteCandidate(channel);
    return local_candidate != nullptr &&
           local_candidate->address().EqualIPs(from) &&
           remote_candidate != nullptr &&
           remote_candidate->address().EqualIPs(to);
  }

  static bool CheckCandidatePair(P2PTransportChannel* ch1,
                                 P2PTransportChannel* ch2,
                                 SocketAddress from,
                                 SocketAddress to) {
    return CheckCandidate(ch1, from, to) && CheckCandidate(ch2, to, from);
  }

  static bool CheckConnected(P2PTransportChannel* ch1,
                             P2PTransportChannel* ch2) {
    return ch1 != nullptr && ch1->receiving() && ch1->writable() &&
           ch2 != nullptr && ch2->receiving() && ch2->writable();
  }

  static bool CheckCandidatePairAndConnected(P2PTransportChannel* ch1,
                                             P2PTransportChannel* ch2,
                                             SocketAddress from,
                                             SocketAddress to) {
    return CheckConnected(ch1, ch2) && CheckCandidatePair(ch1, ch2, from, to);
  }

  void Test(const Result& expected) {
    webrtc::Timestamp connect_start = env_.clock().CurrentTime();
    int64_t connect_time;

    // Create the channels and wait for them to connect.
    CreateChannels();
    EXPECT_TRUE(MediumWait().Until(
        [&] { return CheckConnected(ep1_ch1(), ep2_ch1()); }));
    connect_time = (env_.clock().CurrentTime() - connect_start).ms();
    if (connect_time < expected.connect_wait.ms()) {
      RTC_LOG(LS_INFO) << "Connect time: " << connect_time << " ms";
    } else {
      RTC_LOG(LS_INFO) << "Connect time: TIMEOUT (" << expected.connect_wait
                       << ")";
    }

    // Allow a few turns of the crank for the selected connections to emerge.
    // This may take up to 2 seconds.
    if (ep1_ch1()->selected_connection() && ep2_ch1()->selected_connection()) {
      webrtc::Timestamp converge_start = env_.clock().CurrentTime();
      int64_t converge_time;
      // Verifying local and remote channel selected connection information.
      // This is done only for the RFC 5245 as controlled agent will use
      // USE-CANDIDATE from controlling (ep1) agent. We can easily predict from
      // EP1 result matrix.
      EXPECT_TRUE(ShortWait().Until([&] {
        return CheckCandidate1(expected) && CheckCandidate2(expected);
      }));
      // Also do EXPECT_EQ on each part so that failures are more verbose.
      ExpectCandidate1(expected);
      ExpectCandidate2(expected);

      converge_time = (env_.clock().CurrentTime() - converge_start).ms();
      int64_t converge_wait = 2000;
      if (converge_time < converge_wait) {
        RTC_LOG(LS_INFO) << "Converge time: " << converge_time << " ms";
      } else {
        RTC_LOG(LS_INFO) << "Converge time: TIMEOUT (" << converge_time
                         << " ms)";
      }
    }
    // Try sending some data to other end.
    TestSendRecv();

    // Destroy the channels, and wait for them to be fully cleaned up.
    DestroyChannels();
  }

  void TestSendRecv() {
    for (int i = 0; i < 10; ++i) {
      const char* data = "ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890";
      int len = static_cast<int>(strlen(data));
      // local_channel1 <==> remote_channel1
      EXPECT_THAT(MediumWait().Until(
                      [&] { return SendData(ep1_ch1(), data, len); }, Eq(len)),
                  IsRtcOk());
      EXPECT_TRUE(MediumWait().Until(
          [&] { return CheckDataOnChannel(ep2_ch1(), data, len); }));
      EXPECT_THAT(MediumWait().Until(
                      [&] { return SendData(ep2_ch1(), data, len); }, Eq(len)),
                  IsRtcOk());
      EXPECT_TRUE(MediumWait().Until(
          [&] { return CheckDataOnChannel(ep1_ch1(), data, len); }));
    }
  }

  // This test waits for the transport to become receiving and writable on both
  // end points. Once they are, the end points set new local ice parameters and
  // restart the ice gathering. Finally it waits for the transport to select a
  // new connection using the newly generated ice candidates.
  // Before calling this function the end points must be configured.
  void TestHandleIceUfragPasswordChanged() {
    ep1_ch1()->SetRemoteIceParameters(kIceParams[1]);
    ep2_ch1()->SetRemoteIceParameters(kIceParams[0]);
    EXPECT_TRUE(MediumWait().Until(
        [&] { return CheckConnected(ep1_ch1(), ep2_ch1()); }));

    const Candidate* old_local_candidate1 = LocalCandidate(ep1_ch1());
    const Candidate* old_local_candidate2 = LocalCandidate(ep2_ch1());
    const Candidate* old_remote_candidate1 = RemoteCandidate(ep1_ch1());
    const Candidate* old_remote_candidate2 = RemoteCandidate(ep2_ch1());

    ep1_ch1()->SetIceParameters(kIceParams[2]);
    ep1_ch1()->SetRemoteIceParameters(kIceParams[3]);
    ep1_ch1()->MaybeStartGathering();
    ep2_ch1()->SetIceParameters(kIceParams[3]);

    ep2_ch1()->SetRemoteIceParameters(kIceParams[2]);
    ep2_ch1()->MaybeStartGathering();

    EXPECT_THAT(MediumWait().Until(
                    [&] { return LocalCandidate(ep1_ch1())->generation(); },
                    Ne(old_local_candidate1->generation())),
                IsRtcOk());
    EXPECT_THAT(MediumWait().Until(
                    [&] { return LocalCandidate(ep2_ch1())->generation(); },
                    Ne(old_local_candidate2->generation())),
                IsRtcOk());
    EXPECT_THAT(MediumWait().Until(
                    [&] { return RemoteCandidate(ep1_ch1())->generation(); },
                    Ne(old_remote_candidate1->generation())),
                IsRtcOk());
    EXPECT_THAT(MediumWait().Until(
                    [&] { return RemoteCandidate(ep2_ch1())->generation(); },
                    Ne(old_remote_candidate2->generation())),
                IsRtcOk());
    EXPECT_EQ(1u, RemoteCandidate(ep2_ch1())->generation());
    EXPECT_EQ(1u, RemoteCandidate(ep1_ch1())->generation());
  }

  void TestPacketInfoIsSet(PacketInfo info) {
    EXPECT_NE(info.packet_type, PacketType::kUnknown);
    EXPECT_NE(info.protocol, PacketInfoProtocolType::kUnknown);
    EXPECT_TRUE(info.network_id.has_value());
  }

  void OnReadyToSend(PacketTransportInternal* transport) {
    GetEndpoint(transport)->set_ready_to_send(true);
  }

  // We pass the candidates directly to the other side.
  void OnCandidateGathered(IceTransportInternal* ch, const Candidate& c) {
    if (force_relay_ && !c.is_relay())
      return;

    if (GetEndpoint(ch)->save_candidates()) {
      GetEndpoint(ch)->saved_candidates().push_back(
          {.channel = ch, .candidate = c});
    } else {
      main_->PostTask(SafeTask(
          safety_, [this, ch, c = c]() mutable { AddCandidate(ch, c); }));
    }
  }

  void OnNetworkRouteChanged(std::optional<NetworkRoute> network_route) {
    // If the `network_route` is unset, don't count. This is used in the case
    // when the network on remote side is down, the signal will be fired with an
    // unset network route and it shouldn't trigger a connection switch.
    if (network_route) {
      ++selected_candidate_pair_switches_;
    }
  }

  int reset_selected_candidate_pair_switches() {
    int switches = selected_candidate_pair_switches_;
    selected_candidate_pair_switches_ = 0;
    return switches;
  }

  void PauseCandidates(int endpoint) {
    GetEndpoint(endpoint)->set_save_candidates(true);
  }

  void OnCandidatesRemoved(IceTransportInternal* ch,
                           const std::vector<Candidate>& candidates) {
    // Candidate removals are not paused.
    main_->PostTask(SafeTask(safety_, [this, ch, candidates]() mutable {
      P2PTransportChannel* rch = GetRemoteChannel(ch);
      if (rch == nullptr) {
        return;
      }
      for (const Candidate& c : candidates) {
        RTC_LOG(LS_INFO) << "Removed remote candidate " << c.ToString();
        rch->RemoveRemoteCandidate(c);
      }
    }));
  }

  // Tcp candidate verification has to be done when they are generated.
  void VerifySavedTcpCandidates(int endpoint, absl::string_view tcptype) {
    for (auto& data : GetEndpoint(endpoint)->saved_candidates()) {
      EXPECT_EQ(data.candidate.protocol(), TCP_PROTOCOL_NAME);
      EXPECT_EQ(data.candidate.tcptype(), tcptype);
      if (data.candidate.tcptype() == TCPTYPE_ACTIVE_STR) {
        EXPECT_EQ(data.candidate.address().port(), DISCARD_PORT);
      } else if (data.candidate.tcptype() == TCPTYPE_PASSIVE_STR) {
        EXPECT_NE(data.candidate.address().port(), DISCARD_PORT);
      } else {
        FAIL() << "Unknown tcptype: " << data.candidate.tcptype();
      }
    }
  }

  void ResumeCandidates(int endpoint) {
    Endpoint* ed = GetEndpoint(endpoint);
    std::vector<CandidateData> candidates = std::move(ed->saved_candidates());
    if (!candidates.empty()) {
      main_->PostTask(SafeTask(
          safety_, [this, candidates = std::move(candidates)]() mutable {
            for (CandidateData& data : candidates) {
              AddCandidate(data.channel, data.candidate);
            }
          }));
    }
    ed->saved_candidates().clear();
    ed->set_save_candidates(false);
  }

  void AddCandidate(IceTransportInternal* channel, Candidate& candidate) {
    P2PTransportChannel* rch = GetRemoteChannel(channel);
    if (rch == nullptr) {
      return;
    }
    if (remote_ice_parameter_source_ != FROM_CANDIDATE) {
      candidate.set_username("");
      candidate.set_password("");
    }
    RTC_LOG(LS_INFO) << "Candidate(" << channel->component() << "->"
                     << rch->component() << "): " << candidate.ToString();
    rch->AddRemoteCandidate(candidate);
  }

  void OnReadPacket(PacketTransportInternal* transport,
                    const ReceivedIpPacket& packet) {
    std::list<std::string>& packets = GetPacketList(transport);
    packets.push_front(
        std::string(packet.payload().begin(), packet.payload().end()));
  }

  void OnRoleConflict(IceTransportInternal* channel) {
    GetEndpoint(channel)->OnRoleConflict(true);
    IceRole new_role = GetEndpoint(channel)->ice_role() == ICEROLE_CONTROLLING
                           ? ICEROLE_CONTROLLED
                           : ICEROLE_CONTROLLING;
    channel->SetIceRole(new_role);
  }

  void OnSentPacket(PacketTransportInternal* transport,
                    const SentPacketInfo& packet) {
    TestPacketInfoIsSet(packet.info);
  }

  int SendData(IceTransportInternal* channel, const char* data, size_t len) {
    AsyncSocketPacketOptions options;
    return channel->SendPacket(data, len, options, 0);
  }
  bool CheckDataOnChannel(IceTransportInternal* channel,
                          const char* data,
                          int len) {
    return GetChannelData(channel)->CheckData(data, len);
  }
  static const Candidate* LocalCandidate(P2PTransportChannel* ch) {
    return (ch && ch->selected_connection())
               ? &ch->selected_connection()->local_candidate()
               : nullptr;
  }
  static const Candidate* RemoteCandidate(P2PTransportChannel* ch) {
    return (ch && ch->selected_connection())
               ? &ch->selected_connection()->remote_candidate()
               : nullptr;
  }
  Endpoint* GetEndpoint(PacketTransportInternal* transport) {
    if (ep1_.HasTransport(transport)) {
      return &ep1_;
    } else if (ep2_.HasTransport(transport)) {
      return &ep2_;
    } else {
      return nullptr;
    }
  }
  P2PTransportChannel* GetRemoteChannel(IceTransportInternal* ch) {
    if (ch == ep1_ch1())
      return ep2_ch1();
    else if (ch == ep1_ch2())
      return ep2_ch2();
    else if (ch == ep2_ch1())
      return ep1_ch1();
    else if (ch == ep2_ch2())
      return ep1_ch2();
    else
      return nullptr;
  }
  std::list<std::string>& GetPacketList(PacketTransportInternal* transport) {
    return GetChannelData(transport)->ch_packets();
  }

  enum RemoteIceParameterSource { FROM_CANDIDATE, FROM_SETICEPARAMETERS };

  // How does the test pass ICE parameters to the P2PTransportChannel?
  // On the candidate itself, or through SetRemoteIceParameters?
  // Goes through the candidate itself by default.
  void set_remote_ice_parameter_source(RemoteIceParameterSource source) {
    remote_ice_parameter_source_ = source;
  }

  void set_force_relay(bool relay) { force_relay_ = relay; }

  void ConnectSignalNominated(Connection* conn) {
    conn->SubscribeNominated(
        this, [this](Connection* connection) { OnNominated(connection); });
  }

  void OnNominated(Connection* conn) { nominated_ = true; }
  bool nominated() { return nominated_; }

 protected:
  std::unique_ptr<VirtualSocketServer> vss_;
  std::unique_ptr<NATSocketServer> nss_;
  std::unique_ptr<FirewallSocketServer> ss_;
  GlobalSimulatedTimeController time_controller_;
  Environment env_;
  std::unique_ptr<BasicPacketSocketFactory> socket_factory_;

  Thread* main_;
  scoped_refptr<PendingTaskSafetyFlag> safety_ =
      PendingTaskSafetyFlag::Create();
  TestStunServer::StunServerPtr stun_server_;
  std::optional<TestTurnServer> turn_server_;
  Endpoint ep1_;
  Endpoint ep2_;
  RemoteIceParameterSource remote_ice_parameter_source_ = FROM_CANDIDATE;
  bool force_relay_;
  int selected_candidate_pair_switches_ = 0;

  bool nominated_ = false;
};

// The tests have only a few outcomes, which we predefine.
const P2PTransportChannelTestBase::Result
    P2PTransportChannelTestBase::kLocalUdpToLocalUdp(IceCandidateType::kHost,
                                                     "udp",
                                                     IceCandidateType::kHost,
                                                     "udp",
                                                     TimeDelta::Seconds(1));
const P2PTransportChannelTestBase::Result
    P2PTransportChannelTestBase::kLocalUdpToStunUdp(IceCandidateType::kHost,
                                                    "udp",
                                                    IceCandidateType::kSrflx,
                                                    "udp",
                                                    TimeDelta::Seconds(1));
const P2PTransportChannelTestBase::Result
    P2PTransportChannelTestBase::kLocalUdpToPrflxUdp(IceCandidateType::kHost,
                                                     "udp",
                                                     IceCandidateType::kPrflx,
                                                     "udp",
                                                     TimeDelta::Seconds(1));
const P2PTransportChannelTestBase::Result
    P2PTransportChannelTestBase::kPrflxUdpToLocalUdp(IceCandidateType::kPrflx,
                                                     "udp",
                                                     IceCandidateType::kHost,
                                                     "udp",
                                                     TimeDelta::Seconds(1));
const P2PTransportChannelTestBase::Result
    P2PTransportChannelTestBase::kStunUdpToLocalUdp(IceCandidateType::kSrflx,
                                                    "udp",
                                                    IceCandidateType::kHost,
                                                    "udp",
                                                    TimeDelta::Seconds(1));
const P2PTransportChannelTestBase::Result
    P2PTransportChannelTestBase::kStunUdpToStunUdp(IceCandidateType::kSrflx,
                                                   "udp",
                                                   IceCandidateType::kSrflx,
                                                   "udp",
                                                   TimeDelta::Seconds(1));
const P2PTransportChannelTestBase::Result
    P2PTransportChannelTestBase::kStunUdpToPrflxUdp(IceCandidateType::kSrflx,
                                                    "udp",
                                                    IceCandidateType::kPrflx,
                                                    "udp",
                                                    TimeDelta::Seconds(1));
const P2PTransportChannelTestBase::Result
    P2PTransportChannelTestBase::kPrflxUdpToStunUdp(IceCandidateType::kPrflx,
                                                    "udp",
                                                    IceCandidateType::kSrflx,
                                                    "udp",
                                                    TimeDelta::Seconds(1));
const P2PTransportChannelTestBase::Result
    P2PTransportChannelTestBase::kLocalUdpToRelayUdp(IceCandidateType::kHost,
                                                     "udp",
                                                     IceCandidateType::kRelay,
                                                     "udp",
                                                     TimeDelta::Seconds(2));
const P2PTransportChannelTestBase::Result
    P2PTransportChannelTestBase::kPrflxUdpToRelayUdp(IceCandidateType::kPrflx,
                                                     "udp",
                                                     IceCandidateType::kRelay,
                                                     "udp",
                                                     TimeDelta::Seconds(2));
const P2PTransportChannelTestBase::Result
    P2PTransportChannelTestBase::kRelayUdpToPrflxUdp(IceCandidateType::kRelay,
                                                     "udp",
                                                     IceCandidateType::kPrflx,
                                                     "udp",
                                                     TimeDelta::Seconds(2));
const P2PTransportChannelTestBase::Result
    P2PTransportChannelTestBase::kLocalTcpToLocalTcp(IceCandidateType::kHost,
                                                     "tcp",
                                                     IceCandidateType::kHost,
                                                     "tcp",
                                                     TimeDelta::Seconds(3));
const P2PTransportChannelTestBase::Result
    P2PTransportChannelTestBase::kLocalTcpToPrflxTcp(IceCandidateType::kHost,
                                                     "tcp",
                                                     IceCandidateType::kPrflx,
                                                     "tcp",
                                                     TimeDelta::Seconds(3));
const P2PTransportChannelTestBase::Result
    P2PTransportChannelTestBase::kPrflxTcpToLocalTcp(IceCandidateType::kPrflx,
                                                     "tcp",
                                                     IceCandidateType::kHost,
                                                     "tcp",
                                                     TimeDelta::Seconds(3));

// Test the matrix of all the connectivity types we expect to see in the wild.
// Just test every combination of the configs in the Config enum.
class P2PTransportChannelTest : public P2PTransportChannelTestBase {
 protected:
  void ConfigureEndpoints(Config config1,
                          Config config2,
                          int allocator_flags1,
                          int allocator_flags2) {
    CreatePortAllocators();
    ConfigureEndpoint(0, config1);
    SetAllocatorFlags(0, allocator_flags1);
    SetAllocationStepDelay(0, kMinimumStepDelay);
    ConfigureEndpoint(1, config2);
    SetAllocatorFlags(1, allocator_flags2);
    SetAllocationStepDelay(1, kMinimumStepDelay);

    set_remote_ice_parameter_source(FROM_SETICEPARAMETERS);
  }
  void ConfigureEndpoint(int endpoint, Config config) {
    switch (config) {
      case OPEN:
        AddAddress(endpoint, kPublicAddrs[endpoint]);
        break;
      case NAT_FULL_CONE:
      case NAT_ADDR_RESTRICTED:
      case NAT_PORT_RESTRICTED:
      case NAT_SYMMETRIC:
        AddAddress(endpoint, kPrivateAddrs[endpoint]);
        // Add a single NAT of the desired type
        nat()
            ->AddTranslator(env_, kPublicAddrs[endpoint], kNatAddrs[endpoint],
                            static_cast<NATType>(config - NAT_FULL_CONE))
            ->AddClient(kPrivateAddrs[endpoint]);
        break;
      case NAT_DOUBLE_CONE:
      case NAT_SYMMETRIC_THEN_CONE:
        AddAddress(endpoint, kCascadedPrivateAddrs[endpoint]);
        // Add a two cascaded NATs of the desired types
        nat()
            ->AddTranslator(env_, kPublicAddrs[endpoint], kNatAddrs[endpoint],
                            (config == Config::NAT_DOUBLE_CONE)
                                ? NATType::NAT_OPEN_CONE
                                : NATType::NAT_SYMMETRIC)
            ->AddTranslator(env_, kPrivateAddrs[endpoint],
                            kCascadedNatAddrs[endpoint], NAT_OPEN_CONE)
            ->AddClient(kCascadedPrivateAddrs[endpoint]);
        break;
      case BLOCK_UDP:
      case BLOCK_UDP_AND_INCOMING_TCP:
      case BLOCK_ALL_BUT_OUTGOING_HTTP:
        AddAddress(endpoint, kPublicAddrs[endpoint]);
        // Block all UDP
        fw()->AddRule(false, FP_UDP, FD_ANY, kPublicAddrs[endpoint]);
        if (config == BLOCK_UDP_AND_INCOMING_TCP) {
          // Block TCP inbound to the endpoint
          fw()->AddRule(false, FP_TCP, SocketAddress(), kPublicAddrs[endpoint]);
        } else if (config == BLOCK_ALL_BUT_OUTGOING_HTTP) {
          // Block all TCP to/from the endpoint except 80/443 out
          fw()->AddRule(true, FP_TCP, kPublicAddrs[endpoint],
                        SocketAddress(IPAddress(INADDR_ANY), 80));
          fw()->AddRule(true, FP_TCP, kPublicAddrs[endpoint],
                        SocketAddress(IPAddress(INADDR_ANY), 443));
          fw()->AddRule(false, FP_TCP, FD_ANY, kPublicAddrs[endpoint]);
        }
        break;
      default:
        RTC_DCHECK_NOTREACHED();
        break;
    }
  }
};

class P2PTransportChannelMatrixTest : public P2PTransportChannelTest,
                                      public WithParamInterface<std::string> {
 protected:
  static const Result* kMatrix[NUM_CONFIGS][NUM_CONFIGS];
};

// Shorthands for use in the test matrix.
#define LULU &kLocalUdpToLocalUdp
#define LUSU &kLocalUdpToStunUdp
#define LUPU &kLocalUdpToPrflxUdp
#define PULU &kPrflxUdpToLocalUdp
#define SULU &kStunUdpToLocalUdp
#define SUSU &kStunUdpToStunUdp
#define SUPU &kStunUdpToPrflxUdp
#define PUSU &kPrflxUdpToStunUdp
#define LURU &kLocalUdpToRelayUdp
#define PURU &kPrflxUdpToRelayUdp
#define RUPU &kRelayUdpToPrflxUdp
#define LTLT &kLocalTcpToLocalTcp
#define LTPT &kLocalTcpToPrflxTcp
#define PTLT &kPrflxTcpToLocalTcp
// TODO(?): Enable these once TestRelayServer can accept external TCP.
#define LTRT NULL
#define LSRS NULL

// Test matrix. Originator behavior defined by rows, receiever by columns.

// TODO(?): Fix NULLs caused by lack of TCP support in NATSocket.
// TODO(?): Rearrange rows/columns from best to worst.
const P2PTransportChannelMatrixTest::Result*
    P2PTransportChannelMatrixTest::kMatrix[NUM_CONFIGS][NUM_CONFIGS] = {
        // OPEN  CONE  ADDR  PORT  SYMM  2CON  SCON  !UDP  !TCP  HTTP
        /*OP*/
        {LULU, LUSU, LUSU, LUSU, LUPU, LUSU, LUPU, LTPT, LTPT, LSRS},
        /*CO*/
        {SULU, SUSU, SUSU, SUSU, SUPU, SUSU, SUPU, nullptr, nullptr, LSRS},
        /*AD*/
        {SULU, SUSU, SUSU, SUSU, SUPU, SUSU, SUPU, nullptr, nullptr, LSRS},
        /*PO*/
        {SULU, SUSU, SUSU, SUSU, RUPU, SUSU, RUPU, nullptr, nullptr, LSRS},
        /*SY*/
        {PULU, PUSU, PUSU, PURU, PURU, PUSU, PURU, nullptr, nullptr, LSRS},
        /*2C*/
        {SULU, SUSU, SUSU, SUSU, SUPU, SUSU, SUPU, nullptr, nullptr, LSRS},
        /*SC*/
        {PULU, PUSU, PUSU, PURU, PURU, PUSU, PURU, nullptr, nullptr, LSRS},
        /*!U*/
        {LTPT, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, LTPT, LTPT,
         LSRS},
        /*!T*/
        {PTLT, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, PTLT, LTRT,
         LSRS},
        /*HT*/
        {LSRS, LSRS, LSRS, LSRS, LSRS, LSRS, LSRS, LSRS, LSRS, LSRS},
};

// The actual tests that exercise all the various configurations.
// Test names are of the form P2PTransportChannelTest_TestOPENToNAT_FULL_CONE
#define P2P_TEST_DECLARATION(x, y, z)                            \
  TEST_P(P2PTransportChannelMatrixTest, z##Test##x##To##y) {     \
    ConfigureEndpoints(x, y, PORTALLOCATOR_ENABLE_SHARED_SOCKET, \
                       PORTALLOCATOR_ENABLE_SHARED_SOCKET);      \
    if (kMatrix[x][y] != NULL)                                   \
      Test(*kMatrix[x][y]);                                      \
    else                                                         \
      RTC_LOG(LS_WARNING) << "Not yet implemented";              \
  }

#define P2P_TEST(x, y) P2P_TEST_DECLARATION(x, y, /* empty argument */)

#define P2P_TEST_SET(x)                   \
  P2P_TEST(x, OPEN)                       \
  P2P_TEST(x, NAT_FULL_CONE)              \
  P2P_TEST(x, NAT_ADDR_RESTRICTED)        \
  P2P_TEST(x, NAT_PORT_RESTRICTED)        \
  P2P_TEST(x, NAT_SYMMETRIC)              \
  P2P_TEST(x, NAT_DOUBLE_CONE)            \
  P2P_TEST(x, NAT_SYMMETRIC_THEN_CONE)    \
  P2P_TEST(x, BLOCK_UDP)                  \
  P2P_TEST(x, BLOCK_UDP_AND_INCOMING_TCP) \
  P2P_TEST(x, BLOCK_ALL_BUT_OUTGOING_HTTP)

P2P_TEST_SET(OPEN)
P2P_TEST_SET(NAT_FULL_CONE)
P2P_TEST_SET(NAT_ADDR_RESTRICTED)
P2P_TEST_SET(NAT_PORT_RESTRICTED)
P2P_TEST_SET(NAT_SYMMETRIC)
P2P_TEST_SET(NAT_DOUBLE_CONE)
P2P_TEST_SET(NAT_SYMMETRIC_THEN_CONE)
P2P_TEST_SET(BLOCK_UDP)
P2P_TEST_SET(BLOCK_UDP_AND_INCOMING_TCP)
P2P_TEST_SET(BLOCK_ALL_BUT_OUTGOING_HTTP)

INSTANTIATE_TEST_SUITE_P(
    All,
    P2PTransportChannelMatrixTest,
    // Each field-trial is ~144 tests (some return not-yet-implemented).
    Values("", "WebRTC-IceFieldTrials/enable_goog_ping:true/"));

// Test that we restart candidate allocation when local ufrag&pwd changed.
// Standard Ice protocol is used.
TEST_F(P2PTransportChannelTest, HandleUfragPwdChange) {
  ConfigureEndpoints(OPEN, OPEN, kDefaultPortAllocatorFlags,
                     kDefaultPortAllocatorFlags);
  CreateChannels();
  TestHandleIceUfragPasswordChanged();
  DestroyChannels();
}

// Same as above test, but with a symmetric NAT.
// We should end up with relay<->prflx candidate pairs, with generation "1".
TEST_F(P2PTransportChannelTest, HandleUfragPwdChangeSymmetricNat) {
  ConfigureEndpoints(NAT_SYMMETRIC, NAT_SYMMETRIC, kDefaultPortAllocatorFlags,
                     kDefaultPortAllocatorFlags);
  CreateChannels();
  TestHandleIceUfragPasswordChanged();
  DestroyChannels();
}

// Test the operation of GetStats.
TEST_F(P2PTransportChannelTest, GetStats) {
  ConfigureEndpoints(OPEN, OPEN, kDefaultPortAllocatorFlags,
                     kDefaultPortAllocatorFlags);
  CreateChannels();
  EXPECT_TRUE(MediumWait().Until([&] {
    return ep1_ch1()->receiving() && ep1_ch1()->writable() &&
           ep2_ch1()->receiving() && ep2_ch1()->writable();
  }));
  // Sends and receives 10 packets.
  TestSendRecv();

  // Try sending a packet which is discarded due to the socket being blocked.
  virtual_socket_server()->SetSendingBlocked(true);
  const char* data = "ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890";
  int len = static_cast<int>(strlen(data));
  EXPECT_EQ(-1, SendData(ep1_ch1(), data, len));

  IceTransportStats ice_transport_stats;
  ASSERT_TRUE(ep1_ch1()->GetStats(&ice_transport_stats));
  ASSERT_GE(ice_transport_stats.connection_infos.size(), 1u);
  ASSERT_GE(ice_transport_stats.candidate_stats_list.size(), 1u);
  EXPECT_EQ(ice_transport_stats.selected_candidate_pair_changes, 1u);
  ConnectionInfo* best_conn_info = nullptr;
  for (ConnectionInfo& info : ice_transport_stats.connection_infos) {
    if (info.best_connection) {
      best_conn_info = &info;
      break;
    }
  }
  ASSERT_TRUE(best_conn_info != nullptr);
  EXPECT_TRUE(best_conn_info->receiving);
  EXPECT_TRUE(best_conn_info->writable);
  EXPECT_FALSE(best_conn_info->timeout);
  // Note that discarded packets are counted in sent_total_packets but not
  // sent_total_bytes.
  EXPECT_EQ(11U, best_conn_info->sent_total_packets);
  EXPECT_EQ(1U, best_conn_info->sent_discarded_packets);
  EXPECT_EQ(10 * 36U, best_conn_info->sent_total_bytes);
  EXPECT_EQ(36U, best_conn_info->sent_discarded_bytes);
  EXPECT_EQ(10 * 36U, best_conn_info->recv_total_bytes);
  EXPECT_EQ(10U, best_conn_info->packets_received);

  EXPECT_EQ(10 * 36U, ice_transport_stats.bytes_sent);
  EXPECT_EQ(10 * 36U, ice_transport_stats.bytes_received);

  DestroyChannels();
}

TEST_F(P2PTransportChannelTest, GetStatsSwitchConnection) {
  IceConfig continual_gathering_config =
      CreateIceConfig(TimeDelta::Seconds(1), GATHER_CONTINUALLY);

  ConfigureEndpoints(OPEN, OPEN, kDefaultPortAllocatorFlags,
                     kDefaultPortAllocatorFlags);

  AddAddress(0, kAlternateAddrs[1], "rmnet0", ADAPTER_TYPE_CELLULAR);

  CreateChannels(continual_gathering_config, continual_gathering_config);
  EXPECT_TRUE(MediumWait().Until([&] {
    return ep1_ch1()->receiving() && ep1_ch1()->writable() &&
           ep2_ch1()->receiving() && ep2_ch1()->writable();
  }));
  // Sends and receives 10 packets.
  TestSendRecv();

  IceTransportStats ice_transport_stats;
  ASSERT_TRUE(ep1_ch1()->GetStats(&ice_transport_stats));
  ASSERT_GE(ice_transport_stats.connection_infos.size(), 2u);
  ASSERT_GE(ice_transport_stats.candidate_stats_list.size(), 2u);
  EXPECT_EQ(ice_transport_stats.selected_candidate_pair_changes, 1u);

  ConnectionInfo* best_conn_info = nullptr;
  for (ConnectionInfo& info : ice_transport_stats.connection_infos) {
    if (info.best_connection) {
      best_conn_info = &info;
      break;
    }
  }
  ASSERT_TRUE(best_conn_info != nullptr);
  EXPECT_TRUE(best_conn_info->receiving);
  EXPECT_TRUE(best_conn_info->writable);
  EXPECT_FALSE(best_conn_info->timeout);

  EXPECT_EQ(10 * 36U, best_conn_info->sent_total_bytes);
  EXPECT_EQ(10 * 36U, best_conn_info->recv_total_bytes);
  EXPECT_EQ(10 * 36U, ice_transport_stats.bytes_sent);
  EXPECT_EQ(10 * 36U, ice_transport_stats.bytes_received);

  auto old_selected_connection = ep1_ch1()->selected_connection();
  ep1_ch1()->RemoveConnectionForTest(
      const_cast<Connection*>(old_selected_connection));

  EXPECT_THAT(
      MediumWait().Until([&] { return ep1_ch1()->selected_connection(); },
                         Ne(nullptr)),
      IsRtcOk());

  // Sends and receives 10 packets.
  TestSendRecv();

  IceTransportStats ice_transport_stats2;
  ASSERT_TRUE(ep1_ch1()->GetStats(&ice_transport_stats2));

  int64_t sum_bytes_sent = 0;
  int64_t sum_bytes_received = 0;
  for (ConnectionInfo& info : ice_transport_stats.connection_infos) {
    sum_bytes_sent += info.sent_total_bytes;
    sum_bytes_received += info.recv_total_bytes;
  }

  EXPECT_EQ(10 * 36U, sum_bytes_sent);
  EXPECT_EQ(10 * 36U, sum_bytes_received);

  EXPECT_EQ(20 * 36U, ice_transport_stats2.bytes_sent);
  EXPECT_EQ(20 * 36U, ice_transport_stats2.bytes_received);

  DestroyChannels();
}

// Tests that an ICE regathering reason is recorded when there is a network
// change if and only if continual gathering is enabled.
TEST_F(P2PTransportChannelTest,
       TestIceRegatheringReasonContinualGatheringByNetworkChange) {
  ConfigureEndpoints(OPEN, OPEN, kOnlyLocalPorts, kOnlyLocalPorts);

  // ep1 gathers continually but ep2 does not.
  IceConfig continual_gathering_config =
      CreateIceConfig(TimeDelta::Seconds(1), GATHER_CONTINUALLY);
  IceConfig default_config;
  CreateChannels(continual_gathering_config, default_config);

  EXPECT_TRUE(
      MediumWait().Until([&] { return CheckConnected(ep1_ch1(), ep2_ch1()); }));

  // Adding address in ep1 will trigger continual gathering.
  AddAddress(0, kAlternateAddrs[0]);
  EXPECT_THAT(Wait(TimeDelta::Millis(1900))
                  .Until(
                      [&] {
                        return GetEndpoint(0)->GetIceRegatheringCountForReason(
                            IceRegatheringReason::NETWORK_CHANGE);
                      },
                      Eq(1)),
              IsRtcOk());

  ep2_ch1()->SetIceParameters(kIceParams[3]);
  ep2_ch1()->SetRemoteIceParameters(kIceParams[2]);
  ep2_ch1()->MaybeStartGathering();

  AddAddress(1, kAlternateAddrs[1]);
  time_controller_.AdvanceTime(kDefaultTimeout);
  // ep2 has not enabled continual gathering.
  EXPECT_EQ(0, GetEndpoint(1)->GetIceRegatheringCountForReason(
                   IceRegatheringReason::NETWORK_CHANGE));

  DestroyChannels();
}

// Tests that an ICE regathering reason is recorded when there is a network
// failure if and only if continual gathering is enabled.
TEST_F(P2PTransportChannelTest,
       TestIceRegatheringReasonContinualGatheringByNetworkFailure) {
  ConfigureEndpoints(OPEN, OPEN, kOnlyLocalPorts, kOnlyLocalPorts);

  // ep1 gathers continually but ep2 does not.
  IceConfig config1 =
      CreateIceConfig(TimeDelta::Seconds(1), GATHER_CONTINUALLY);
  config1.regather_on_failed_networks_interval = TimeDelta::Seconds(2);
  IceConfig config2;
  config2.regather_on_failed_networks_interval = TimeDelta::Seconds(2);
  CreateChannels(config1, config2);

  EXPECT_TRUE(DefaultWait().Until(
      [&] { return CheckConnected(ep1_ch1(), ep2_ch1()); }));

  fw()->AddRule(false, FP_ANY, FD_ANY, kPublicAddrs[0]);
  // Timeout value such that all connections are deleted.
  const int kNetworkFailureTimeout = 35000;
  time_controller_.AdvanceTime(TimeDelta::Millis(kNetworkFailureTimeout));
  EXPECT_LE(1, GetEndpoint(0)->GetIceRegatheringCountForReason(
                   IceRegatheringReason::NETWORK_FAILURE));
  EXPECT_EQ(0, GetEndpoint(1)->GetIceRegatheringCountForReason(
                   IceRegatheringReason::NETWORK_FAILURE));

  DestroyChannels();
}

// Test that we properly create a connection on a STUN ping from unknown address
// when the signaling is slow.
TEST_F(P2PTransportChannelTest, PeerReflexiveCandidateBeforeSignaling) {
  ConfigureEndpoints(OPEN, OPEN, kDefaultPortAllocatorFlags,
                     kDefaultPortAllocatorFlags);
  // Emulate no remote parameters coming in.
  set_remote_ice_parameter_source(FROM_CANDIDATE);
  CreateChannels();
  // Only have remote parameters come in for ep2, not ep1.
  ep2_ch1()->SetRemoteIceParameters(kIceParams[0]);

  // Pause sending ep2's candidates to ep1 until ep1 receives the peer reflexive
  // candidate.
  PauseCandidates(1);

  // Wait until the callee becomes writable to make sure that a ping request is
  // received by the caller before their remote ICE credentials are set.
  ASSERT_THAT(
      Wait(TimeDelta::Seconds(11))
          .Until([&] { return ep2_ch1()->selected_connection(); }, Ne(nullptr)),
      IsRtcOk());
  // Add two sets of remote ICE credentials, so that the ones used by the
  // candidate will be generation 1 instead of 0.
  ep1_ch1()->SetRemoteIceParameters(kIceParams[3]);
  ep1_ch1()->SetRemoteIceParameters(kIceParams[1]);
  // The caller should have the selected connection connected to the peer
  // reflexive candidate.
  const Connection* selected_connection = nullptr;
  ASSERT_THAT(MediumWait().Until(
                  [&] {
                    return selected_connection =
                               ep1_ch1()->selected_connection();
                  },
                  Ne(nullptr)),
              IsRtcOk());
  EXPECT_TRUE(selected_connection->remote_candidate().is_prflx());
  EXPECT_EQ(kIceUfrag[1], selected_connection->remote_candidate().username());
  EXPECT_EQ(kIcePwd[1], selected_connection->remote_candidate().password());
  EXPECT_EQ(1u, selected_connection->remote_candidate().generation());

  ResumeCandidates(1);
  // Verify ep1's selected connection is updated to use the 'local' candidate.
  EXPECT_TRUE(MediumWait().Until([&] {
    return ep1_ch1()->selected_connection()->remote_candidate().is_local();
  }));
  EXPECT_EQ(selected_connection, ep1_ch1()->selected_connection());
  DestroyChannels();
}

// Test that if we learn a prflx remote candidate, its address is concealed in
// 1. the selected candidate pair accessed via the public API, and
// 2. the candidate pair stats
// until we learn the same address from signaling.
TEST_F(P2PTransportChannelTest, PeerReflexiveRemoteCandidateIsSanitized) {
  ConfigureEndpoints(OPEN, OPEN, kOnlyLocalPorts, kOnlyLocalPorts);
  // Emulate no remote parameters coming in.
  set_remote_ice_parameter_source(FROM_CANDIDATE);
  CreateChannels();
  // Only have remote parameters come in for ep2, not ep1.
  ep2_ch1()->SetRemoteIceParameters(kIceParams[0]);

  // Pause sending ep2's candidates to ep1 until ep1 receives the peer reflexive
  // candidate.
  PauseCandidates(1);

  ASSERT_THAT(
      MediumWait().Until([&] { return ep2_ch1()->selected_connection(); },
                         Ne(nullptr)),
      IsRtcOk());
  ep1_ch1()->SetRemoteIceParameters(kIceParams[1]);
  ASSERT_THAT(
      MediumWait().Until([&] { return ep1_ch1()->selected_connection(); },
                         Ne(nullptr)),
      IsRtcOk());

  // Check the selected candidate pair.
  auto pair_ep1 = ep1_ch1()->GetSelectedCandidatePair();
  ASSERT_TRUE(pair_ep1.has_value());
  EXPECT_TRUE(pair_ep1->remote_candidate().is_prflx());
  EXPECT_TRUE(pair_ep1->remote_candidate().address().ipaddr().IsNil());

  IceTransportStats ice_transport_stats;
  ep1_ch1()->GetStats(&ice_transport_stats);
  // Check the candidate pair stats.
  ASSERT_EQ(1u, ice_transport_stats.connection_infos.size());
  EXPECT_TRUE(
      ice_transport_stats.connection_infos[0].remote_candidate.is_prflx());
  EXPECT_TRUE(ice_transport_stats.connection_infos[0]
                  .remote_candidate.address()
                  .ipaddr()
                  .IsNil());

  // Let ep1 receive the remote candidate to update its type from prflx to host.
  ResumeCandidates(1);
  ASSERT_TRUE(MediumWait().Until([&] {
    return ep1_ch1()->selected_connection() != nullptr &&
           ep1_ch1()->selected_connection()->remote_candidate().is_local();
  }));

  // We should be able to reveal the address after it is learnt via
  // AddIceCandidate.
  //
  // Check the selected candidate pair.
  auto updated_pair_ep1 = ep1_ch1()->GetSelectedCandidatePair();
  ASSERT_TRUE(updated_pair_ep1.has_value());
  EXPECT_TRUE(updated_pair_ep1->remote_candidate().is_local());
  EXPECT_TRUE(HasRemoteAddress(&updated_pair_ep1.value(), kPublicAddrs[1]));

  ep1_ch1()->GetStats(&ice_transport_stats);
  // Check the candidate pair stats.
  ASSERT_EQ(1u, ice_transport_stats.connection_infos.size());
  EXPECT_TRUE(
      ice_transport_stats.connection_infos[0].remote_candidate.is_local());
  EXPECT_TRUE(ice_transport_stats.connection_infos[0]
                  .remote_candidate.address()
                  .EqualIPs(kPublicAddrs[1]));

  DestroyChannels();
}

// Test that we properly create a connection on a STUN ping from unknown address
// when the signaling is slow and the end points are behind NAT.
TEST_F(P2PTransportChannelTest, PeerReflexiveCandidateBeforeSignalingWithNAT) {
  ConfigureEndpoints(OPEN, NAT_SYMMETRIC, kDefaultPortAllocatorFlags,
                     kDefaultPortAllocatorFlags);
  // Emulate no remote parameters coming in.
  set_remote_ice_parameter_source(FROM_CANDIDATE);
  CreateChannels();
  // Only have remote parameters come in for ep2, not ep1.
  ep2_ch1()->SetRemoteIceParameters(kIceParams[0]);
  // Pause sending ep2's candidates to ep1 until ep1 receives the peer reflexive
  // candidate.
  PauseCandidates(1);

  // Wait until the callee becomes writable to make sure that a ping request is
  // received by the caller before their remote ICE credentials are set.
  ASSERT_THAT(
      MediumWait().Until([&] { return ep2_ch1()->selected_connection(); },
                         Ne(nullptr)),
      IsRtcOk());
  // Add two sets of remote ICE credentials, so that the ones used by the
  // candidate will be generation 1 instead of 0.
  ep1_ch1()->SetRemoteIceParameters(kIceParams[3]);
  ep1_ch1()->SetRemoteIceParameters(kIceParams[1]);

  // The caller's selected connection should be connected to the peer reflexive
  // candidate.
  const Connection* selected_connection = nullptr;
  ASSERT_THAT(MediumWait().Until(
                  [&] {
                    return selected_connection =
                               ep1_ch1()->selected_connection();
                  },
                  Ne(nullptr)),
              IsRtcOk());
  EXPECT_TRUE(selected_connection->remote_candidate().is_prflx());
  EXPECT_EQ(kIceUfrag[1], selected_connection->remote_candidate().username());
  EXPECT_EQ(kIcePwd[1], selected_connection->remote_candidate().password());
  EXPECT_EQ(1u, selected_connection->remote_candidate().generation());

  ResumeCandidates(1);

  EXPECT_TRUE(MediumWait().Until([&] {
    return ep1_ch1()->selected_connection()->remote_candidate().is_prflx();
  }));
  EXPECT_EQ(selected_connection, ep1_ch1()->selected_connection());
  DestroyChannels();
}

// Test that we properly create a connection on a STUN ping from unknown address
// when the signaling is slow, even if the new candidate is created due to the
// remote peer doing an ICE restart, pairing this candidate across generations.
//
// Previously this wasn't working due to a bug where the peer reflexive
// candidate was only updated for the newest generation candidate pairs, and
// not older-generation candidate pairs created by pairing candidates across
// generations. This resulted in the old-generation prflx candidate being
// prioritized above new-generation candidate pairs.
TEST_F(P2PTransportChannelTest,
       PeerReflexiveCandidateBeforeSignalingWithIceRestart) {
  ConfigureEndpoints(OPEN, OPEN, kDefaultPortAllocatorFlags,
                     kDefaultPortAllocatorFlags);
  // Only gather relay candidates, so that when the prflx candidate arrives
  // it's prioritized above the current candidate pair.
  GetEndpoint(0)->allocator()->SetCandidateFilter(CF_RELAY);
  GetEndpoint(1)->allocator()->SetCandidateFilter(CF_RELAY);
  // Setting this allows us to control when SetRemoteIceParameters is called.
  set_remote_ice_parameter_source(FROM_CANDIDATE);
  CreateChannels();
  // Wait for the initial connection to be made.
  ep1_ch1()->SetRemoteIceParameters(kIceParams[1]);
  ep2_ch1()->SetRemoteIceParameters(kIceParams[0]);
  EXPECT_TRUE(
      MediumWait().Until([&] { return CheckConnected(ep1_ch1(), ep2_ch1()); }));

  // Simulate an ICE restart on ep2, but don't signal the candidate or new
  // ICE parameters until after a prflx connection has been made.
  PauseCandidates(1);
  ep2_ch1()->SetIceParameters(kIceParams[3]);

  ep1_ch1()->SetRemoteIceParameters(kIceParams[3]);
  ep2_ch1()->MaybeStartGathering();

  // The caller should have the selected connection connected to the peer
  // reflexive candidate.
  EXPECT_TRUE(Wait(TimeDelta::Seconds(8)).Until([&] {
    return ep1_ch1()->selected_connection()->remote_candidate().is_prflx();
  }));
  const Connection* prflx_selected_connection =
      ep1_ch1()->selected_connection();

  // Now simulate the ICE restart on ep1.
  ep1_ch1()->SetIceParameters(kIceParams[2]);

  ep2_ch1()->SetRemoteIceParameters(kIceParams[2]);
  ep1_ch1()->MaybeStartGathering();

  // Finally send the candidates from ep2's ICE restart and verify that ep1 uses
  // their information to update the peer reflexive candidate.
  ResumeCandidates(1);

  EXPECT_TRUE(DefaultWait().Until([&] {
    return ep1_ch1()->selected_connection()->remote_candidate().is_relay();
  }));
  EXPECT_EQ(prflx_selected_connection, ep1_ch1()->selected_connection());
  DestroyChannels();
}

// Test that if remote candidates don't have ufrag and pwd, we still work.
TEST_F(P2PTransportChannelTest, RemoteCandidatesWithoutUfragPwd) {
  set_remote_ice_parameter_source(FROM_SETICEPARAMETERS);
  ConfigureEndpoints(OPEN, OPEN, kDefaultPortAllocatorFlags,
                     kDefaultPortAllocatorFlags);
  CreateChannels();
  const Connection* selected_connection = nullptr;
  // Wait until the callee's connections are created.
  EXPECT_THAT(DefaultWait().Until(
                  [&] {
                    return selected_connection =
                               ep2_ch1()->selected_connection();
                  },
                  NotNull()),
              IsRtcOk());
  // Wait to make sure the selected connection is not changed.
  (void)MediumWait().Until(
      [&] { return ep2_ch1()->selected_connection() != selected_connection; });
  EXPECT_TRUE(ep2_ch1()->selected_connection() == selected_connection);
  DestroyChannels();
}

// Test that a host behind NAT cannot be reached when incoming_only
// is set to true.
TEST_F(P2PTransportChannelTest, IncomingOnlyBlocked) {
  ConfigureEndpoints(NAT_FULL_CONE, OPEN, kDefaultPortAllocatorFlags,
                     kDefaultPortAllocatorFlags);

  SetAllocatorFlags(0, kOnlyLocalPorts);
  CreateChannels();
  ep1_ch1()->set_incoming_only(true);

  // Pump for 1 second and verify that the channels are not connected.
  time_controller_.AdvanceTime(kShortTimeout);

  EXPECT_FALSE(ep1_ch1()->receiving());
  EXPECT_FALSE(ep1_ch1()->writable());
  EXPECT_FALSE(ep2_ch1()->receiving());
  EXPECT_FALSE(ep2_ch1()->writable());

  DestroyChannels();
}

// Test that a peer behind NAT can connect to a peer that has
// incoming_only flag set.
TEST_F(P2PTransportChannelTest, IncomingOnlyOpen) {
  ConfigureEndpoints(OPEN, NAT_FULL_CONE, kDefaultPortAllocatorFlags,
                     kDefaultPortAllocatorFlags);

  SetAllocatorFlags(0, kOnlyLocalPorts);
  CreateChannels();
  ep1_ch1()->set_incoming_only(true);

  EXPECT_TRUE(
      ShortWait().Until([&] { return CheckConnected(ep1_ch1(), ep2_ch1()); }));

  DestroyChannels();
}

// Test that two peers can connect when one can only make outgoing TCP
// connections. This has been observed in some scenarios involving
// VPNs/firewalls.
TEST_F(P2PTransportChannelTest, CanOnlyMakeOutgoingTcpConnections) {
  // The PORTALLOCATOR_ENABLE_ANY_ADDRESS_PORTS flag is required if the
  // application needs this use case to work, since the application must accept
  // the tradeoff that more candidates need to be allocated.
  //
  // TODO(deadbeef): Later, make this flag the default, and do more elegant
  // things to ensure extra candidates don't waste resources?
  ConfigureEndpoints(
      OPEN, OPEN,
      kDefaultPortAllocatorFlags | PORTALLOCATOR_ENABLE_ANY_ADDRESS_PORTS,
      kDefaultPortAllocatorFlags);
  // In order to simulate nothing working but outgoing TCP connections, prevent
  // the endpoint from binding to its interface's address as well as the
  // "any" addresses. It can then only make a connection by using "Connect()".
  fw()->SetUnbindableIps(
      {GetAnyIP(AF_INET), GetAnyIP(AF_INET6), kPublicAddrs[0].ipaddr()});
  CreateChannels();
  // Expect a IceCandidateType::kPrflx candidate on the side that can only make
  // outgoing connections, endpoint 0.
  Test(kPrflxTcpToLocalTcp);
  DestroyChannels();
}

TEST_F(P2PTransportChannelTest, TestTcpConnectionsFromActiveToPassive) {
  CreatePortAllocators();
  AddAddress(0, kPublicAddrs[0]);
  AddAddress(1, kPublicAddrs[1]);

  SetAllocationStepDelay(0, kMinimumStepDelay);
  SetAllocationStepDelay(1, kMinimumStepDelay);

  int kOnlyLocalTcpPorts = PORTALLOCATOR_DISABLE_UDP |
                           PORTALLOCATOR_DISABLE_STUN |
                           PORTALLOCATOR_DISABLE_RELAY;
  // Disable all protocols except TCP.
  SetAllocatorFlags(0, kOnlyLocalTcpPorts);
  SetAllocatorFlags(1, kOnlyLocalTcpPorts);

  SetAllowTcpListen(0, true);   // actpass.
  SetAllowTcpListen(1, false);  // active.

  // We want SetRemoteIceParameters to be called as it normally would.
  // Otherwise we won't know what parameters to use for the expected
  // prflx TCP candidates.
  set_remote_ice_parameter_source(FROM_SETICEPARAMETERS);

  // Pause candidate so we could verify the candidate properties.
  PauseCandidates(0);
  PauseCandidates(1);
  CreateChannels();

  // Verify tcp candidates.
  VerifySavedTcpCandidates(0, TCPTYPE_PASSIVE_STR);
  VerifySavedTcpCandidates(1, TCPTYPE_ACTIVE_STR);

  // Resume candidates.
  ResumeCandidates(0);
  ResumeCandidates(1);

  EXPECT_TRUE(MediumWait().Until([&] {
    return CheckCandidatePairAndConnected(ep1_ch1(), ep2_ch1(), kPublicAddrs[0],
                                          kPublicAddrs[1]);
  }));

  TestSendRecv();
  DestroyChannels();
}

// Test that tcptype is set on all candidates for a connection running over TCP.
TEST_F(P2PTransportChannelTest, TestTcpConnectionTcptypeSet) {
  ConfigureEndpoints(BLOCK_UDP_AND_INCOMING_TCP, OPEN,
                     PORTALLOCATOR_ENABLE_SHARED_SOCKET,
                     PORTALLOCATOR_ENABLE_SHARED_SOCKET);

  SetAllowTcpListen(0, false);  // active.
  SetAllowTcpListen(1, true);   // actpass.
  CreateChannels();

  EXPECT_TRUE(
      ShortWait().Until([&] { return CheckConnected(ep1_ch1(), ep2_ch1()); }));
  time_controller_.AdvanceTime(kDefaultTimeout);

  EXPECT_EQ(RemoteCandidate(ep1_ch1())->tcptype(), "passive");
  EXPECT_EQ(LocalCandidate(ep1_ch1())->tcptype(), "active");
  EXPECT_EQ(RemoteCandidate(ep2_ch1())->tcptype(), "active");
  EXPECT_EQ(LocalCandidate(ep2_ch1())->tcptype(), "passive");

  DestroyChannels();
}

TEST_F(P2PTransportChannelTest, TestIceRoleConflict) {
  CreatePortAllocators();
  AddAddress(0, kPublicAddrs[0]);
  AddAddress(1, kPublicAddrs[1]);

  // Creating channels with both channels role set to CONTROLLING.
  SetIceRole(0, ICEROLE_CONTROLLING);
  SetIceRole(1, ICEROLE_CONTROLLING);

  CreateChannels();
  bool first_endpoint_has_lower_tiebreaker =
      GetEndpoint(0)->allocator()->ice_tiebreaker() <
      GetEndpoint(1)->allocator()->ice_tiebreaker();
  // Since both the channels initiated with controlling state, the channel with
  // the lower tiebreaker should receive SignalRoleConflict.
  EXPECT_TRUE(MediumWait().Until([&] {
    return GetRoleConflict(first_endpoint_has_lower_tiebreaker ? 0 : 1);
  }));
  EXPECT_FALSE(GetRoleConflict(first_endpoint_has_lower_tiebreaker ? 1 : 0));

  EXPECT_TRUE(
      ShortWait().Until([&] { return CheckConnected(ep1_ch1(), ep2_ch1()); }));

  EXPECT_TRUE(ep1_ch1()->selected_connection() &&
              ep2_ch1()->selected_connection());

  TestSendRecv();
  DestroyChannels();
}

// Tests that the ice configs (protocol and role) can be passed down to ports.
TEST_F(P2PTransportChannelTest, TestIceConfigWillPassDownToPort) {
  CreatePortAllocators();
  AddAddress(0, kPublicAddrs[0]);
  AddAddress(1, kPublicAddrs[1]);

  SetIceRole(0, ICEROLE_CONTROLLING);
  SetIceRole(1, ICEROLE_CONTROLLING);

  CreateChannels();

  // Pick channel with the higher tiebreaker so its role won't change unless we
  // tell it to.
  P2PTransportChannel* channel =
      GetEndpoint(0)->allocator()->ice_tiebreaker() >
              GetEndpoint(1)->allocator()->ice_tiebreaker()
          ? ep1_ch1()
          : ep2_ch1();

  EXPECT_THAT(ShortWait().Until([&] { return channel->ports(); }, SizeIs(2)),
              IsRtcOk());

  EXPECT_THAT(channel->ports(), Each(Property(&PortInterface::GetIceRole,
                                              Eq(ICEROLE_CONTROLLING))));

  channel->SetIceRole(ICEROLE_CONTROLLED);

  EXPECT_THAT(channel->ports(), Each(Property(&PortInterface::GetIceRole,
                                              Eq(ICEROLE_CONTROLLED))));

  EXPECT_TRUE(
      ShortWait().Until([&] { return CheckConnected(ep1_ch1(), ep2_ch1()); }));

  EXPECT_TRUE(ep1_ch1()->selected_connection() &&
              ep2_ch1()->selected_connection());

  TestSendRecv();
  DestroyChannels();
}

// Verify that we can set DSCP value and retrieve properly from P2PTC.
TEST_F(P2PTransportChannelTest, TestDefaultDscpValue) {
  CreatePortAllocators();
  AddAddress(0, kPublicAddrs[0]);
  AddAddress(1, kPublicAddrs[1]);

  CreateChannels();
  EXPECT_EQ(DSCP_NO_CHANGE, GetEndpoint(0)->cd1().ch()->DefaultDscpValue());
  EXPECT_EQ(DSCP_NO_CHANGE, GetEndpoint(1)->cd1().ch()->DefaultDscpValue());
  GetEndpoint(0)->cd1().ch()->SetOption(Socket::OPT_DSCP, DSCP_CS6);
  GetEndpoint(1)->cd1().ch()->SetOption(Socket::OPT_DSCP, DSCP_CS6);
  EXPECT_EQ(DSCP_CS6, GetEndpoint(0)->cd1().ch()->DefaultDscpValue());
  EXPECT_EQ(DSCP_CS6, GetEndpoint(1)->cd1().ch()->DefaultDscpValue());
  GetEndpoint(0)->cd1().ch()->SetOption(Socket::OPT_DSCP, DSCP_AF41);
  GetEndpoint(1)->cd1().ch()->SetOption(Socket::OPT_DSCP, DSCP_AF41);
  EXPECT_EQ(DSCP_AF41, GetEndpoint(0)->cd1().ch()->DefaultDscpValue());
  EXPECT_EQ(DSCP_AF41, GetEndpoint(1)->cd1().ch()->DefaultDscpValue());
  DestroyChannels();
}

// Verify IPv6 connection is preferred over IPv4.
TEST_F(P2PTransportChannelTest, TestIPv6Connections) {
  CreatePortAllocators();
  AddAddress(0, kIPv6PublicAddrs[0]);
  AddAddress(0, kPublicAddrs[0]);
  AddAddress(1, kIPv6PublicAddrs[1]);
  AddAddress(1, kPublicAddrs[1]);

  SetAllocationStepDelay(0, kMinimumStepDelay);
  SetAllocationStepDelay(1, kMinimumStepDelay);

  // Enable IPv6
  SetAllocatorFlags(
      0, PORTALLOCATOR_ENABLE_IPV6 | PORTALLOCATOR_ENABLE_IPV6_ON_WIFI);
  SetAllocatorFlags(
      1, PORTALLOCATOR_ENABLE_IPV6 | PORTALLOCATOR_ENABLE_IPV6_ON_WIFI);

  CreateChannels();

  EXPECT_TRUE(ShortWait().Until([&] {
    return CheckCandidatePairAndConnected(
        ep1_ch1(), ep2_ch1(), kIPv6PublicAddrs[0], kIPv6PublicAddrs[1]);
  }));

  TestSendRecv();
  DestroyChannels();
}

// Testing forceful TURN connections.
TEST_F(P2PTransportChannelTest, TestForceTurn) {
  ConfigureEndpoints(
      NAT_PORT_RESTRICTED, NAT_SYMMETRIC,
      kDefaultPortAllocatorFlags | PORTALLOCATOR_ENABLE_SHARED_SOCKET,
      kDefaultPortAllocatorFlags | PORTALLOCATOR_ENABLE_SHARED_SOCKET);
  set_force_relay(true);

  SetAllocationStepDelay(0, kMinimumStepDelay);
  SetAllocationStepDelay(1, kMinimumStepDelay);

  CreateChannels();

  EXPECT_TRUE(
      ShortWait().Until([&] { return CheckConnected(ep1_ch1(), ep2_ch1()); }));

  EXPECT_TRUE(ep1_ch1()->selected_connection() &&
              ep2_ch1()->selected_connection());

  EXPECT_TRUE(RemoteCandidate(ep1_ch1())->is_relay());
  EXPECT_TRUE(LocalCandidate(ep1_ch1())->is_relay());
  EXPECT_TRUE(RemoteCandidate(ep2_ch1())->is_relay());
  EXPECT_TRUE(LocalCandidate(ep2_ch1())->is_relay());

  TestSendRecv();
  DestroyChannels();
}

// Test that if continual gathering is set to true, ICE gathering state will
// not change to "Complete", and vice versa.
TEST_F(P2PTransportChannelTest, TestContinualGathering) {
  ConfigureEndpoints(OPEN, OPEN, kDefaultPortAllocatorFlags,
                     kDefaultPortAllocatorFlags);
  SetAllocationStepDelay(0, kDefaultStepDelay);
  SetAllocationStepDelay(1, kDefaultStepDelay);
  IceConfig continual_gathering_config =
      CreateIceConfig(TimeDelta::Seconds(1), GATHER_CONTINUALLY);
  // By default, ep2 does not gather continually.
  IceConfig default_config;
  CreateChannels(continual_gathering_config, default_config);

  EXPECT_TRUE(
      MediumWait().Until([&] { return CheckConnected(ep1_ch1(), ep2_ch1()); }));
  (void)MediumWait().Until([&] {
    return IceGatheringState::kIceGatheringComplete ==
           ep1_ch1()->gathering_state();
  });
  EXPECT_EQ(IceGatheringState::kIceGatheringGathering,
            ep1_ch1()->gathering_state());
  // By now, ep2 should have completed gathering.
  EXPECT_EQ(IceGatheringState::kIceGatheringComplete,
            ep2_ch1()->gathering_state());

  DestroyChannels();
}

// Test that a connection succeeds when the P2PTransportChannel uses a pooled
// PortAllocatorSession that has not yet finished gathering candidates.
TEST_F(P2PTransportChannelTest, TestUsingPooledSessionBeforeDoneGathering) {
  ConfigureEndpoints(OPEN, OPEN, kDefaultPortAllocatorFlags,
                     kDefaultPortAllocatorFlags);
  // First create a pooled session for each endpoint.
  auto* allocator_1 = GetEndpoint(0)->allocator();
  auto* allocator_2 = GetEndpoint(1)->allocator();
  int pool_size = 1;
  allocator_1->SetConfiguration(allocator_1->stun_servers(),
                                allocator_1->turn_servers(), pool_size,
                                NO_PRUNE);
  allocator_2->SetConfiguration(allocator_2->stun_servers(),
                                allocator_2->turn_servers(), pool_size,
                                NO_PRUNE);
  const PortAllocatorSession* pooled_session_1 =
      allocator_1->GetPooledSession();
  const PortAllocatorSession* pooled_session_2 =
      allocator_2->GetPooledSession();
  ASSERT_NE(nullptr, pooled_session_1);
  ASSERT_NE(nullptr, pooled_session_2);
  // Sanity check that pooled sessions haven't gathered anything yet.
  EXPECT_TRUE(pooled_session_1->ReadyPorts().empty());
  EXPECT_TRUE(pooled_session_1->ReadyCandidates().empty());
  EXPECT_TRUE(pooled_session_2->ReadyPorts().empty());
  EXPECT_TRUE(pooled_session_2->ReadyCandidates().empty());
  // Now let the endpoints connect and try exchanging some data.
  CreateChannels();
  EXPECT_TRUE(
      ShortWait().Until([&] { return CheckConnected(ep1_ch1(), ep2_ch1()); }));
  TestSendRecv();
  // Make sure the P2PTransportChannels are actually using ports from the
  // pooled sessions.
  auto pooled_ports_1 = pooled_session_1->ReadyPorts();
  auto pooled_ports_2 = pooled_session_2->ReadyPorts();
  EXPECT_THAT(pooled_ports_1,
              Contains(ep1_ch1()->selected_connection()->PortForTest()));
  EXPECT_THAT(pooled_ports_2,
              Contains(ep2_ch1()->selected_connection()->PortForTest()));
  DestroyChannels();
}

// Test that a connection succeeds when the P2PTransportChannel uses a pooled
// PortAllocatorSession that already finished gathering candidates.
TEST_F(P2PTransportChannelTest, TestUsingPooledSessionAfterDoneGathering) {
  ConfigureEndpoints(OPEN, OPEN, kDefaultPortAllocatorFlags,
                     kDefaultPortAllocatorFlags);
  // First create a pooled session for each endpoint.
  auto* allocator_1 = GetEndpoint(0)->allocator();
  auto* allocator_2 = GetEndpoint(1)->allocator();
  int pool_size = 1;
  allocator_1->SetConfiguration(allocator_1->stun_servers(),
                                allocator_1->turn_servers(), pool_size,
                                NO_PRUNE);
  allocator_2->SetConfiguration(allocator_2->stun_servers(),
                                allocator_2->turn_servers(), pool_size,
                                NO_PRUNE);
  const PortAllocatorSession* pooled_session_1 =
      allocator_1->GetPooledSession();
  const PortAllocatorSession* pooled_session_2 =
      allocator_2->GetPooledSession();
  ASSERT_NE(nullptr, pooled_session_1);
  ASSERT_NE(nullptr, pooled_session_2);
  // Wait for the pooled sessions to finish gathering before the
  // P2PTransportChannels try to use them.
  EXPECT_TRUE(MediumWait().Until([&] {
    return pooled_session_1->CandidatesAllocationDone() &&
           pooled_session_2->CandidatesAllocationDone();
  }));
  // Now let the endpoints connect and try exchanging some data.
  CreateChannels();
  EXPECT_TRUE(DefaultWait().Until(
      [&] { return CheckConnected(ep1_ch1(), ep2_ch1()); }));
  TestSendRecv();
  // Make sure the P2PTransportChannels are actually using ports from the
  // pooled sessions.
  auto pooled_ports_1 = pooled_session_1->ReadyPorts();
  auto pooled_ports_2 = pooled_session_2->ReadyPorts();
  EXPECT_THAT(pooled_ports_1,
              Contains(ep1_ch1()->selected_connection()->PortForTest()));
  EXPECT_THAT(pooled_ports_2,
              Contains(ep2_ch1()->selected_connection()->PortForTest()));
  DestroyChannels();
}

// Test that when the "presume_writable_when_fully_relayed" flag is set to
// true and there's a TURN-TURN candidate pair, it's presumed to be writable
// as soon as it's created.
// TODO(deadbeef): Move this and other "presumed writable" tests into a test
// class that operates on a single P2PTransportChannel, once an appropriate one
// (which supports TURN servers and TURN candidate gathering) is available.
TEST_F(P2PTransportChannelTest, TurnToTurnPresumedWritable) {
  ConfigureEndpoints(OPEN, OPEN, kDefaultPortAllocatorFlags,
                     kDefaultPortAllocatorFlags);
  // Only configure one channel so we can control when the remote candidate
  // is added.
  GetEndpoint(0)->cd1().set_ch(CreateChannel(0, ICE_CANDIDATE_COMPONENT_DEFAULT,
                                             kIceParams[0], kIceParams[1]));
  IceConfig config;
  config.presume_writable_when_fully_relayed = true;
  ep1_ch1()->SetIceConfig(config);
  ep1_ch1()->MaybeStartGathering();
  EXPECT_THAT(MediumWait().Until([&] { return ep1_ch1()->gathering_state(); },
                                 Eq(IceGatheringState::kIceGatheringComplete)),
              IsRtcOk());
  // Add two remote candidates; a host candidate (with higher priority)
  // and TURN candidate.
  ep1_ch1()->AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 100));
  ep1_ch1()->AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kRelay, "2.2.2.2", 2, 0));
  // Expect that the TURN-TURN candidate pair will be prioritized since it's
  // "probably writable".
  EXPECT_THAT(
      DefaultWait().Until([&] { return ep1_ch1()->selected_connection(); },
                          Ne(nullptr)),
      IsRtcOk());
  EXPECT_TRUE(LocalCandidate(ep1_ch1())->is_relay());
  EXPECT_TRUE(RemoteCandidate(ep1_ch1())->is_relay());
  // Also expect that the channel instantly indicates that it's writable since
  // it has a TURN-TURN pair.
  EXPECT_TRUE(ep1_ch1()->writable());
  EXPECT_TRUE(GetEndpoint(0)->ready_to_send());
  // Also make sure we can immediately send packets.
  const char* data = "test";
  int len = static_cast<int>(strlen(data));
  EXPECT_EQ(len, SendData(ep1_ch1(), data, len));
  // Prevent pending messages to access endpoints after their destruction.
  DestroyChannels();
}

// Test that a TURN/peer reflexive candidate pair is also presumed writable.
TEST_F(P2PTransportChannelTest, TurnToPrflxPresumedWritable) {
  // We need to add artificial network delay to verify that the connection
  // is presumed writable before it's actually writable. Without this delay
  // it would become writable instantly.
  virtual_socket_server()->set_delay_mean(50);
  virtual_socket_server()->UpdateDelayDistribution();

  ConfigureEndpoints(NAT_SYMMETRIC, NAT_SYMMETRIC, kDefaultPortAllocatorFlags,
                     kDefaultPortAllocatorFlags);
  // We want the remote TURN candidate to show up as prflx. To do this we need
  // to configure the server to accept packets from an address we haven't
  // explicitly installed permission for.
  test_turn_server()->set_enable_permission_checks(false);
  IceConfig config;
  config.presume_writable_when_fully_relayed = true;
  GetEndpoint(0)->cd1().set_ch(CreateChannel(0, ICE_CANDIDATE_COMPONENT_DEFAULT,
                                             kIceParams[0], kIceParams[1]));
  GetEndpoint(1)->cd1().set_ch(CreateChannel(1, ICE_CANDIDATE_COMPONENT_DEFAULT,
                                             kIceParams[1], kIceParams[0]));
  ep1_ch1()->SetIceConfig(config);
  ep2_ch1()->SetIceConfig(config);
  // Don't signal candidates from channel 2, so that channel 1 sees the TURN
  // candidate as peer reflexive.
  PauseCandidates(1);
  ep1_ch1()->MaybeStartGathering();
  ep2_ch1()->MaybeStartGathering();

  // Wait for the TURN<->prflx connection.
  EXPECT_TRUE(ShortWait().Until(
      [&] { return ep1_ch1()->receiving() && ep1_ch1()->writable(); }));
  ASSERT_NE(nullptr, ep1_ch1()->selected_connection());
  EXPECT_TRUE(LocalCandidate(ep1_ch1())->is_relay());
  EXPECT_TRUE(RemoteCandidate(ep1_ch1())->is_prflx());
  // Make sure that at this point the connection is only presumed writable,
  // not fully writable.
  EXPECT_FALSE(ep1_ch1()->selected_connection()->writable());

  // Now wait for it to actually become writable.
  EXPECT_TRUE(ShortWait().Until(
      [&] { return ep1_ch1()->selected_connection()->writable(); }));

  // Explitly destroy channels, before fake clock is destroyed.
  DestroyChannels();
}

// Test that a presumed-writable TURN<->TURN connection is preferred above an
// unreliable connection (one that has failed to be pinged for some time).
TEST_F(P2PTransportChannelTest, PresumedWritablePreferredOverUnreliable) {
  ConfigureEndpoints(NAT_SYMMETRIC, NAT_SYMMETRIC, kDefaultPortAllocatorFlags,
                     kDefaultPortAllocatorFlags);
  IceConfig config;
  config.presume_writable_when_fully_relayed = true;
  GetEndpoint(0)->cd1().set_ch(CreateChannel(0, ICE_CANDIDATE_COMPONENT_DEFAULT,
                                             kIceParams[0], kIceParams[1]));
  GetEndpoint(1)->cd1().set_ch(CreateChannel(1, ICE_CANDIDATE_COMPONENT_DEFAULT,
                                             kIceParams[1], kIceParams[0]));
  ep1_ch1()->SetIceConfig(config);
  ep2_ch1()->SetIceConfig(config);
  ep1_ch1()->MaybeStartGathering();
  ep2_ch1()->MaybeStartGathering();
  // Wait for initial connection as usual.
  EXPECT_TRUE(DefaultWait().Until(
      [&] { return CheckConnected(ep1_ch1(), ep2_ch1()); }));
  const Connection* old_selected_connection = ep1_ch1()->selected_connection();
  // Destroy the second channel and wait for the current connection on the
  // first channel to become "unreliable", making it no longer writable.
  GetEndpoint(1)->cd1().reset_ch();
  EXPECT_TRUE(DefaultWait().Until([&] { return !ep1_ch1()->writable(); }));
  EXPECT_NE(nullptr, ep1_ch1()->selected_connection());
  // Add a remote TURN candidate. The first channel should still have a TURN
  // port available to make a TURN<->TURN pair that's presumed writable.
  ep1_ch1()->AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kRelay, "2.2.2.2", 2, 0));
  EXPECT_TRUE(LocalCandidate(ep1_ch1())->is_relay());
  EXPECT_TRUE(RemoteCandidate(ep1_ch1())->is_relay());
  EXPECT_TRUE(ep1_ch1()->writable());
  EXPECT_TRUE(GetEndpoint(0)->ready_to_send());
  EXPECT_NE(old_selected_connection, ep1_ch1()->selected_connection());
  // Explitly destroy channels, before fake clock is destroyed.
  DestroyChannels();
}

// Ensure that "SignalReadyToSend" is fired as expected with a "presumed
// writable" connection. Previously this did not work.
TEST_F(P2PTransportChannelTest, SignalReadyToSendWithPresumedWritable) {
  ConfigureEndpoints(OPEN, OPEN, kDefaultPortAllocatorFlags,
                     kDefaultPortAllocatorFlags);
  // Only test one endpoint, so we can ensure the connection doesn't receive a
  // binding response and advance beyond being "presumed" writable.
  GetEndpoint(0)->cd1().set_ch(CreateChannel(0, ICE_CANDIDATE_COMPONENT_DEFAULT,
                                             kIceParams[0], kIceParams[1]));
  IceConfig config;
  config.presume_writable_when_fully_relayed = true;
  ep1_ch1()->SetIceConfig(config);
  ep1_ch1()->MaybeStartGathering();
  EXPECT_THAT(DefaultWait().Until([&] { return ep1_ch1()->gathering_state(); },
                                  Eq(IceGatheringState::kIceGatheringComplete)),
              IsRtcOk());
  ep1_ch1()->AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kRelay, "1.1.1.1", 1, 0));
  // Sanity checking the type of the connection.
  EXPECT_THAT(
      DefaultWait().Until([&] { return ep1_ch1()->selected_connection(); },
                          Ne(nullptr)),
      IsRtcOk());
  EXPECT_TRUE(LocalCandidate(ep1_ch1())->is_relay());
  EXPECT_TRUE(RemoteCandidate(ep1_ch1())->is_relay());

  // Tell the socket server to block packets (returning EWOULDBLOCK).
  virtual_socket_server()->SetSendingBlocked(true);
  const char* data = "test";
  int len = static_cast<int>(strlen(data));
  EXPECT_EQ(-1, SendData(ep1_ch1(), data, len));

  // Reset `ready_to_send_` flag, which is set to true if the event fires as it
  // should.
  GetEndpoint(0)->set_ready_to_send(false);
  virtual_socket_server()->SetSendingBlocked(false);
  EXPECT_TRUE(GetEndpoint(0)->ready_to_send());
  EXPECT_EQ(len, SendData(ep1_ch1(), data, len));
  DestroyChannels();
}

// Test that role conflict error responses are sent as expected when receiving a
// ping from an unknown address over a TURN connection. Regression test for
// crbug.com/webrtc/9034.
TEST_F(P2PTransportChannelTest,
       TurnToPrflxSelectedAfterResolvingIceControllingRoleConflict) {
  // Gather only relay candidates.
  ConfigureEndpoints(NAT_SYMMETRIC, NAT_SYMMETRIC,
                     kDefaultPortAllocatorFlags | PORTALLOCATOR_DISABLE_UDP |
                         PORTALLOCATOR_DISABLE_STUN | PORTALLOCATOR_DISABLE_TCP,
                     kDefaultPortAllocatorFlags | PORTALLOCATOR_DISABLE_UDP |
                         PORTALLOCATOR_DISABLE_STUN |
                         PORTALLOCATOR_DISABLE_TCP);

  SetIceRole(0, ICEROLE_CONTROLLING);
  SetIceRole(1, ICEROLE_CONTROLLING);
  // We want the remote TURN candidate to show up as prflx. To do this we need
  // to configure the server to accept packets from an address we haven't
  // explicitly installed permission for.
  test_turn_server()->set_enable_permission_checks(false);
  GetEndpoint(0)->cd1().set_ch(CreateChannel(0, ICE_CANDIDATE_COMPONENT_DEFAULT,
                                             kIceParams[0], kIceParams[1]));
  GetEndpoint(1)->cd1().set_ch(CreateChannel(1, ICE_CANDIDATE_COMPONENT_DEFAULT,
                                             kIceParams[1], kIceParams[0]));
  // Don't signal candidates from channel 2, so that channel 1 sees the TURN
  // candidate as peer reflexive.
  PauseCandidates(1);
  ep1_ch1()->MaybeStartGathering();
  ep2_ch1()->MaybeStartGathering();

  EXPECT_TRUE(ShortWait().Until(
      [&] { return ep1_ch1()->receiving() && ep1_ch1()->writable(); }));

  ASSERT_NE(nullptr, ep1_ch1()->selected_connection());

  EXPECT_TRUE(LocalCandidate(ep1_ch1())->is_relay());
  EXPECT_TRUE(RemoteCandidate(ep1_ch1())->is_prflx());

  DestroyChannels();
}

// Test that the writability can be established with the piggyback
// acknowledgement in the connectivity check from the remote peer.
TEST_F(P2PTransportChannelTest,
       CanConnectWithPiggybackCheckAcknowledgementWhenCheckResponseBlocked) {
  env_ = CreateTestEnvironment(
      {.field_trials = "WebRTC-PiggybackIceCheckAcknowledgement/Enabled/",
       .time = &time_controller_});
  ConfigureEndpoints(OPEN, OPEN, kOnlyLocalPorts, kOnlyLocalPorts);
  IceConfig ep1_config;
  IceConfig ep2_config =
      CreateIceConfig(TimeDelta::Seconds(1), GATHER_CONTINUALLY);
  // Let ep2 be tolerable of the loss of connectivity checks, so that it keeps
  // sending pings even after ep1 becomes unwritable as we configure the
  // firewall below.
  ep2_config.receiving_timeout = TimeDelta::Seconds(30);
  ep2_config.ice_unwritable_timeout = TimeDelta::Seconds(30);
  ep2_config.ice_unwritable_min_checks = 30;
  ep2_config.ice_inactive_timeout = TimeDelta::Seconds(60);

  CreateChannels(ep1_config, ep2_config);

  // Wait until both sides become writable for the first time.
  EXPECT_TRUE(
      MediumWait().Until([&] { return CheckConnected(ep1_ch1(), ep2_ch1()); }));
  // Block the ingress traffic to ep1 so that there is no check response from
  // ep2.
  ASSERT_NE(nullptr, LocalCandidate(ep1_ch1()));
  fw()->AddRule(false, FP_ANY, FD_IN, LocalCandidate(ep1_ch1())->address());
  // Wait until ep1 becomes unwritable. At the same time ep2 should be still
  // fine so that it will keep sending pings.
  EXPECT_TRUE(DefaultWait().Until(
      [&] { return ep1_ch1() != nullptr && !ep1_ch1()->writable(); }));
  EXPECT_TRUE(ep2_ch1() != nullptr && ep2_ch1()->writable());
  // Now let the pings from ep2 to flow but block any pings from ep1, so that
  // ep1 can only become writable again after receiving an incoming ping from
  // ep2 with piggyback acknowledgement of its previously sent pings. Note
  // though that ep1 should have stopped sending pings after becoming unwritable
  // in the current design.
  fw()->ClearRules();
  fw()->AddRule(false, FP_ANY, FD_OUT, LocalCandidate(ep1_ch1())->address());
  EXPECT_TRUE(DefaultWait().Until(
      [&] { return ep1_ch1() != nullptr && ep1_ch1()->writable(); }));
  DestroyChannels();
}

// Test what happens when we have 2 users behind the same NAT. This can lead
// to interesting behavior because the STUN server will only give out the
// address of the outermost NAT.
class P2PTransportChannelSameNatTest : public P2PTransportChannelTestBase {
 protected:
  void ConfigureEndpoints(Config nat_type, Config config1, Config config2) {
    RTC_CHECK_GE(nat_type, NAT_FULL_CONE);
    RTC_CHECK_LE(nat_type, NAT_SYMMETRIC);
    CreatePortAllocators();
    NATSocketServer::Translator* outer_nat =
        nat()->AddTranslator(env_, kPublicAddrs[0], kNatAddrs[0],
                             static_cast<NATType>(nat_type - NAT_FULL_CONE));
    ConfigureEndpoint(outer_nat, 0, config1);
    ConfigureEndpoint(outer_nat, 1, config2);
    set_remote_ice_parameter_source(FROM_SETICEPARAMETERS);
  }
  void ConfigureEndpoint(NATSocketServer::Translator* nat,
                         int endpoint,
                         Config config) {
    RTC_CHECK(config <= NAT_SYMMETRIC);
    if (config == OPEN) {
      AddAddress(endpoint, kPrivateAddrs[endpoint]);
      nat->AddClient(kPrivateAddrs[endpoint]);
    } else {
      AddAddress(endpoint, kCascadedPrivateAddrs[endpoint]);
      nat->AddTranslator(env_, kPrivateAddrs[endpoint],
                         kCascadedNatAddrs[endpoint],
                         static_cast<NATType>(config - NAT_FULL_CONE))
          ->AddClient(kCascadedPrivateAddrs[endpoint]);
    }
  }
};

TEST_F(P2PTransportChannelSameNatTest, TestConesBehindSameCone) {
  ConfigureEndpoints(NAT_FULL_CONE, NAT_FULL_CONE, NAT_FULL_CONE);
  Test(P2PTransportChannelTestBase::Result(IceCandidateType::kPrflx, "udp",
                                           IceCandidateType::kSrflx, "udp",
                                           TimeDelta::Seconds(1)));
}

// Test what happens when we have multiple available pathways.
// In the future we will try different RTTs and configs for the different
// interfaces, so that we can simulate a user with Ethernet and VPN networks.
class P2PTransportChannelMultihomedTest : public P2PTransportChannelTest {
 public:
  const Connection* GetConnectionWithRemoteAddress(
      P2PTransportChannel* channel,
      const SocketAddress& address) {
    for (Connection* conn : channel->connections()) {
      if (HasRemoteAddress(conn, address)) {
        return conn;
      }
    }
    return nullptr;
  }

  Connection* GetConnectionWithLocalAddress(P2PTransportChannel* channel,
                                            const SocketAddress& address) {
    for (Connection* conn : channel->connections()) {
      if (HasLocalAddress(conn, address)) {
        return conn;
      }
    }
    return nullptr;
  }

  Connection* GetConnection(P2PTransportChannel* channel,
                            const SocketAddress& local,
                            const SocketAddress& remote) {
    for (Connection* conn : channel->connections()) {
      if (HasLocalAddress(conn, local) && HasRemoteAddress(conn, remote)) {
        return conn;
      }
    }
    return nullptr;
  }

  Connection* GetBestConnection(P2PTransportChannel* channel) {
    std::span<Connection* const> connections = channel->connections();
    auto it = absl::c_find(connections, channel->selected_connection());
    if (it == connections.end()) {
      return nullptr;
    }
    return *it;
  }

  Connection* GetBackupConnection(P2PTransportChannel* channel) {
    std::span<Connection* const> connections = channel->connections();
    auto it = absl::c_find_if_not(connections, [channel](Connection* conn) {
      return conn == channel->selected_connection();
    });
    if (it == connections.end()) {
      return nullptr;
    }
    return *it;
  }

  void DestroyAllButBestConnection(P2PTransportChannel* channel) {
    const Connection* selected_connection = channel->selected_connection();
    // Copy the list of connections since the original will be modified.
    std::span<Connection* const> view = channel->connections();
    std::vector<Connection*> connections(view.begin(), view.end());
    for (Connection* conn : connections) {
      if (conn != selected_connection)
        channel->RemoveConnectionForTest(conn);
    }
  }
};

// Test that we can establish connectivity when both peers are multihomed.
TEST_F(P2PTransportChannelMultihomedTest, TestBasic) {
  CreatePortAllocators();
  AddAddress(0, kPublicAddrs[0]);
  AddAddress(0, kAlternateAddrs[0]);
  AddAddress(1, kPublicAddrs[1]);
  AddAddress(1, kAlternateAddrs[1]);
  Test(kLocalUdpToLocalUdp);
}

// Test that we can quickly switch links if an interface goes down.
// The controlled side has two interfaces and one will die.
TEST_F(P2PTransportChannelMultihomedTest, TestFailoverControlledSide) {
  CreatePortAllocators();
  AddAddress(0, kPublicAddrs[0]);
  // Simulate failing over from Wi-Fi to cell interface.
  AddAddress(1, kPublicAddrs[1], "eth0", ADAPTER_TYPE_WIFI);
  AddAddress(1, kAlternateAddrs[1], "wlan0", ADAPTER_TYPE_CELLULAR);

  // Use only local ports for simplicity.
  SetAllocatorFlags(0, kOnlyLocalPorts);
  SetAllocatorFlags(1, kOnlyLocalPorts);

  // Make the receiving timeout shorter for testing.
  IceConfig config = CreateIceConfig(TimeDelta::Seconds(1), GATHER_ONCE);
  // Create channels and let them go writable, as usual.
  CreateChannels(config, config);

  EXPECT_TRUE(DefaultWait().Until([&] {
    return CheckCandidatePairAndConnected(ep1_ch1(), ep2_ch1(), kPublicAddrs[0],
                                          kPublicAddrs[1]);
  }));

  // Blackhole any traffic to or from the public addrs.
  RTC_LOG(LS_INFO) << "Failing over...";
  fw()->AddRule(false, FP_ANY, FD_ANY, kPublicAddrs[1]);
  // The selected connections may switch, so keep references to them.
  const Connection* selected_connection1 = ep1_ch1()->selected_connection();
  // We should detect loss of receiving within 1 second or so.
  EXPECT_TRUE(
      MediumWait().Until([&] { return !selected_connection1->receiving(); }));

  // We should switch over to use the alternate addr on both sides
  // when we are not receiving.
  EXPECT_TRUE(MediumWait().Until([&] {
    return ep1_ch1()->selected_connection()->receiving() &&
           ep2_ch1()->selected_connection()->receiving();
  }));
  EXPECT_TRUE(LocalCandidate(ep1_ch1())->address().EqualIPs(kPublicAddrs[0]));
  EXPECT_TRUE(
      RemoteCandidate(ep1_ch1())->address().EqualIPs(kAlternateAddrs[1]));
  EXPECT_TRUE(
      LocalCandidate(ep2_ch1())->address().EqualIPs(kAlternateAddrs[1]));

  DestroyChannels();
}

// Test that we can quickly switch links if an interface goes down.
// The controlling side has two interfaces and one will die.
TEST_F(P2PTransportChannelMultihomedTest, TestFailoverControllingSide) {
  CreatePortAllocators();
  // Simulate failing over from Wi-Fi to cell interface.
  AddAddress(0, kPublicAddrs[0], "eth0", ADAPTER_TYPE_WIFI);
  AddAddress(0, kAlternateAddrs[0], "wlan0", ADAPTER_TYPE_CELLULAR);
  AddAddress(1, kPublicAddrs[1]);

  // Use only local ports for simplicity.
  SetAllocatorFlags(0, kOnlyLocalPorts);
  SetAllocatorFlags(1, kOnlyLocalPorts);

  // Make the receiving timeout shorter for testing.
  IceConfig config = CreateIceConfig(TimeDelta::Seconds(1), GATHER_ONCE);
  // Create channels and let them go writable, as usual.
  CreateChannels(config, config);
  EXPECT_TRUE(MediumWait().Until([&] {
    return CheckCandidatePairAndConnected(ep1_ch1(), ep2_ch1(), kPublicAddrs[0],
                                          kPublicAddrs[1]);
  }));

  // Blackhole any traffic to or from the public addrs.
  RTC_LOG(LS_INFO) << "Failing over...";
  fw()->AddRule(false, FP_ANY, FD_ANY, kPublicAddrs[0]);

  // We should detect loss of receiving within 1 second or so.
  // We should switch over to use the alternate addr on both sides
  // when we are not receiving.
  EXPECT_TRUE(MediumWait().Until([&] {
    return CheckCandidatePairAndConnected(ep1_ch1(), ep2_ch1(),
                                          kAlternateAddrs[0], kPublicAddrs[1]);
  }));

  DestroyChannels();
}

// Tests that we can quickly switch links if an interface goes down when
// there are many connections.
TEST_F(P2PTransportChannelMultihomedTest, TestFailoverWithManyConnections) {
  CreatePortAllocators();
  test_turn_server()->AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
  RelayServerConfig turn_server;
  turn_server.credentials = kRelayCredentials;
  turn_server.ports.push_back(ProtocolAddress(kTurnTcpIntAddr, PROTO_TCP));
  GetAllocator(0)->AddTurnServerForTesting(turn_server);
  GetAllocator(1)->AddTurnServerForTesting(turn_server);
  // Enable IPv6
  SetAllocatorFlags(
      0, PORTALLOCATOR_ENABLE_IPV6 | PORTALLOCATOR_ENABLE_IPV6_ON_WIFI);
  SetAllocatorFlags(
      1, PORTALLOCATOR_ENABLE_IPV6 | PORTALLOCATOR_ENABLE_IPV6_ON_WIFI);
  SetAllocationStepDelay(0, kMinimumStepDelay);
  SetAllocationStepDelay(1, kMinimumStepDelay);

  auto& wifi = kPublicAddrs;
  auto& cellular = kAlternateAddrs;
  auto& wifiIpv6 = kIPv6PublicAddrs;
  auto& cellularIpv6 = kIPv6AlternateAddrs;
  AddAddress(0, wifi[0], "wifi0", ADAPTER_TYPE_WIFI);
  AddAddress(0, wifiIpv6[0], "wifi0", ADAPTER_TYPE_WIFI);
  AddAddress(0, cellular[0], "cellular0", ADAPTER_TYPE_CELLULAR);
  AddAddress(0, cellularIpv6[0], "cellular0", ADAPTER_TYPE_CELLULAR);
  AddAddress(1, wifi[1], "wifi1", ADAPTER_TYPE_WIFI);
  AddAddress(1, wifiIpv6[1], "wifi1", ADAPTER_TYPE_WIFI);
  AddAddress(1, cellular[1], "cellular1", ADAPTER_TYPE_CELLULAR);
  AddAddress(1, cellularIpv6[1], "cellular1", ADAPTER_TYPE_CELLULAR);

  // Set smaller delay on the TCP TURN server so that TCP TURN candidates
  // will be created in time.
  virtual_socket_server()->SetDelayOnAddress(kTurnTcpIntAddr, 1);
  virtual_socket_server()->SetDelayOnAddress(kTurnUdpExtAddr, 1);
  virtual_socket_server()->set_delay_mean(500);
  virtual_socket_server()->UpdateDelayDistribution();

  // Make the receiving timeout shorter for testing.
  IceConfig config = CreateIceConfig(TimeDelta::Seconds(1), GATHER_CONTINUALLY);
  // Create channels and let them go writable, as usual.
  CreateChannels(config, config, true /* ice_renomination */);
  EXPECT_TRUE(MediumWait().Until([&] {
    return CheckCandidatePairAndConnected(ep1_ch1(), ep2_ch1(), wifiIpv6[0],
                                          wifiIpv6[1]);
  }));

  // Blackhole any traffic to or from the wifi on endpoint 1.
  RTC_LOG(LS_INFO) << "Failing over...";
  fw()->AddRule(false, FP_ANY, FD_ANY, wifi[0]);
  fw()->AddRule(false, FP_ANY, FD_ANY, wifiIpv6[0]);

  // The selected connections may switch, so keep references to them.
  const Connection* selected_connection1 = ep1_ch1()->selected_connection();
  const Connection* selected_connection2 = ep2_ch1()->selected_connection();
  EXPECT_TRUE(MediumWait().Until([&] {
    return !selected_connection1->receiving() &&
           !selected_connection2->receiving();
  }));

  // Per-network best connections will be pinged at relatively higher rate when
  // the selected connection becomes not receiving.
  Connection* per_network_best_connection1 =
      GetConnection(ep1_ch1(), cellularIpv6[0], wifiIpv6[1]);
  ASSERT_NE(nullptr, per_network_best_connection1);
  Timestamp last_ping_sent1 = per_network_best_connection1->LastPingSent();
  int num_pings_sent1 = per_network_best_connection1->num_pings_sent();
  EXPECT_THAT(
      MediumWait().Until(
          [&] { return per_network_best_connection1->num_pings_sent(); },
          Gt(num_pings_sent1)),
      IsRtcOk());
  ASSERT_GT(per_network_best_connection1->num_pings_sent() - num_pings_sent1,
            0);
  TimeDelta ping_interval1 =
      (per_network_best_connection1->LastPingSent() - last_ping_sent1) /
      (per_network_best_connection1->num_pings_sent() - num_pings_sent1);
  constexpr TimeDelta kSchedulingDelay = TimeDelta::Millis(200);
  EXPECT_LT(ping_interval1, kWeakOrStabilizingWritableConnectionPingInterval +
                                kSchedulingDelay);

  // It should switch over to use the cellular IPv6 addr on endpoint 1 before
  // it timed out on writing.
  EXPECT_TRUE(MediumWait().Until([&] {
    return CheckCandidatePairAndConnected(ep1_ch1(), ep2_ch1(), cellularIpv6[0],
                                          wifiIpv6[1]);
  }));

  DestroyChannels();
}

// Test that when the controlling side switches the selected connection,
// the nomination of the selected connection on the controlled side will
// increase.
TEST_F(P2PTransportChannelMultihomedTest, TestIceRenomination) {
  CreatePortAllocators();
  // Simulate failing over from Wi-Fi to cell interface.
  AddAddress(0, kPublicAddrs[0], "eth0", ADAPTER_TYPE_WIFI);
  AddAddress(0, kAlternateAddrs[0], "wlan0", ADAPTER_TYPE_CELLULAR);
  AddAddress(1, kPublicAddrs[1]);

  // Use only local ports for simplicity.
  SetAllocatorFlags(0, kOnlyLocalPorts);
  SetAllocatorFlags(1, kOnlyLocalPorts);

  // We want it to set the remote ICE parameters when creating channels.
  set_remote_ice_parameter_source(FROM_SETICEPARAMETERS);
  // Make the receiving timeout shorter for testing.
  IceConfig config = CreateIceConfig(TimeDelta::Seconds(1), GATHER_ONCE);
  // Create channels with ICE renomination and let them go writable as usual.
  CreateChannels(config, config, true);
  ASSERT_TRUE(
      MediumWait().Until([&] { return CheckConnected(ep1_ch1(), ep2_ch1()); }));
  EXPECT_TRUE(MediumWait().Until([&] {
    return ep2_ch1()->selected_connection()->remote_nomination() > 0 &&
           ep1_ch1()->selected_connection()->acked_nomination() > 0;
  }));
  const Connection* selected_connection1 = ep1_ch1()->selected_connection();
  Connection* selected_connection2 =
      const_cast<Connection*>(ep2_ch1()->selected_connection());
  uint32_t remote_nomination2 = selected_connection2->remote_nomination();
  // `selected_connection2` should not be nominated any more since the previous
  // nomination has been acknowledged.
  ConnectSignalNominated(selected_connection2);
  EXPECT_FALSE(DefaultWait().Until([&] { return nominated(); }));
  EXPECT_FALSE(nominated());

  // Blackhole any traffic to or from the public addrs.
  RTC_LOG(LS_INFO) << "Failing over...";
  fw()->AddRule(false, FP_ANY, FD_ANY, kPublicAddrs[0]);

  // The selected connection on the controlling side should switch.
  EXPECT_THAT(
      MediumWait().Until([&] { return ep1_ch1()->selected_connection(); },
                         Ne(selected_connection1)),
      IsRtcOk());
  // The connection on the controlled side should be nominated again
  // and have an increased nomination.
  EXPECT_THAT(
      MediumWait().Until(
          [&] { return ep2_ch1()->selected_connection()->remote_nomination(); },
          Gt(remote_nomination2)),
      IsRtcOk());

  DestroyChannels();
}

// Test that if an interface fails temporarily and then recovers quickly,
// the selected connection will not switch.
// The case that it will switch over to the backup connection if the selected
// connection does not recover after enough time is covered in
// TestFailoverControlledSide and TestFailoverControllingSide.
TEST_F(P2PTransportChannelMultihomedTest,
       TestConnectionSwitchDampeningControlledSide) {
  CreatePortAllocators();
  AddAddress(0, kPublicAddrs[0]);
  // Simulate failing over from Wi-Fi to cell interface.
  AddAddress(1, kPublicAddrs[1], "eth0", ADAPTER_TYPE_WIFI);
  AddAddress(1, kAlternateAddrs[1], "wlan0", ADAPTER_TYPE_CELLULAR);

  // Use only local ports for simplicity.
  SetAllocatorFlags(0, kOnlyLocalPorts);
  SetAllocatorFlags(1, kOnlyLocalPorts);

  // Create channels and let them go writable, as usual.
  CreateChannels();

  EXPECT_TRUE(DefaultWait().Until([&] {
    return CheckCandidatePairAndConnected(ep1_ch1(), ep2_ch1(), kPublicAddrs[0],
                                          kPublicAddrs[1]);
  }));

  // Make the receiving timeout shorter for testing.
  IceConfig config = CreateIceConfig(TimeDelta::Seconds(1), GATHER_ONCE);
  ep1_ch1()->SetIceConfig(config);
  ep2_ch1()->SetIceConfig(config);
  reset_selected_candidate_pair_switches();

  // Blackhole any traffic to or from the public addrs.
  RTC_LOG(LS_INFO) << "Failing over...";
  fw()->AddRule(false, FP_ANY, FD_ANY, kPublicAddrs[1]);

  // The selected connections may switch, so keep references to them.
  const Connection* selected_connection1 = ep1_ch1()->selected_connection();
  // We should detect loss of receiving within 1 second or so.
  EXPECT_TRUE(
      MediumWait().Until([&] { return !selected_connection1->receiving(); }));
  // After a short while, the link recovers itself.
  time_controller_.AdvanceTime(TimeDelta::Millis(10));
  fw()->ClearRules();

  // We should remain on the public address on both sides and no connection
  // switches should have happened.
  EXPECT_TRUE(MediumWait().Until([&] {
    return ep1_ch1()->selected_connection()->receiving() &&
           ep2_ch1()->selected_connection()->receiving();
  }));
  EXPECT_TRUE(RemoteCandidate(ep1_ch1())->address().EqualIPs(kPublicAddrs[1]));
  EXPECT_TRUE(LocalCandidate(ep2_ch1())->address().EqualIPs(kPublicAddrs[1]));
  EXPECT_EQ(0, reset_selected_candidate_pair_switches());

  DestroyChannels();
}

// Test that if an interface fails temporarily and then recovers quickly,
// the selected connection will not switch.
TEST_F(P2PTransportChannelMultihomedTest,
       TestConnectionSwitchDampeningControllingSide) {
  CreatePortAllocators();
  // Simulate failing over from Wi-Fi to cell interface.
  AddAddress(0, kPublicAddrs[0], "eth0", ADAPTER_TYPE_WIFI);
  AddAddress(0, kAlternateAddrs[0], "wlan0", ADAPTER_TYPE_CELLULAR);
  AddAddress(1, kPublicAddrs[1]);

  // Use only local ports for simplicity.
  SetAllocatorFlags(0, kOnlyLocalPorts);
  SetAllocatorFlags(1, kOnlyLocalPorts);

  // Create channels and let them go writable, as usual.
  CreateChannels();
  EXPECT_TRUE(MediumWait().Until([&] {
    return CheckCandidatePairAndConnected(ep1_ch1(), ep2_ch1(), kPublicAddrs[0],
                                          kPublicAddrs[1]);
  }));

  // Make the receiving timeout shorter for testing.
  IceConfig config = CreateIceConfig(TimeDelta::Seconds(1), GATHER_ONCE);
  ep1_ch1()->SetIceConfig(config);
  ep2_ch1()->SetIceConfig(config);
  reset_selected_candidate_pair_switches();

  // Blackhole any traffic to or from the public addrs.
  RTC_LOG(LS_INFO) << "Failing over...";
  fw()->AddRule(false, FP_ANY, FD_ANY, kPublicAddrs[0]);
  // The selected connections may switch, so keep references to them.
  const Connection* selected_connection1 = ep1_ch1()->selected_connection();
  // We should detect loss of receiving within 1 second or so.
  EXPECT_TRUE(
      MediumWait().Until([&] { return !selected_connection1->receiving(); }));
  // The link recovers after a short while.
  time_controller_.AdvanceTime(TimeDelta::Millis(10));
  fw()->ClearRules();

  // We should not switch to the alternate addr on both sides because of the
  // dampening.
  EXPECT_TRUE(MediumWait().Until([&] {
    return CheckCandidatePairAndConnected(ep1_ch1(), ep2_ch1(), kPublicAddrs[0],
                                          kPublicAddrs[1]);
  }));
  EXPECT_EQ(0, reset_selected_candidate_pair_switches());
  DestroyChannels();
}

// Tests that if the remote side's network failed, it won't cause the local
// side to switch connections and networks.
TEST_F(P2PTransportChannelMultihomedTest, TestRemoteFailover) {
  CreatePortAllocators();
  // The interface names are chosen so that `cellular` would have higher
  // candidate priority and higher cost.
  auto& wifi = kPublicAddrs;
  auto& cellular = kAlternateAddrs;
  AddAddress(0, wifi[0], "wifi0", ADAPTER_TYPE_WIFI);
  AddAddress(0, cellular[0], "cellular0", ADAPTER_TYPE_CELLULAR);
  AddAddress(1, wifi[1], "wifi0", ADAPTER_TYPE_WIFI);

  // Use only local ports for simplicity.
  SetAllocatorFlags(0, kOnlyLocalPorts);
  SetAllocatorFlags(1, kOnlyLocalPorts);
  // Create channels and let them go writable, as usual.
  CreateChannels();
  // Make the receiving timeout shorter for testing.
  // Set the backup connection ping interval to 25s.
  IceConfig config = CreateIceConfig(TimeDelta::Seconds(1), GATHER_ONCE,
                                     TimeDelta::Seconds(25));
  // Ping the best connection more frequently since we don't have traffic.
  config.stable_writable_connection_ping_interval = TimeDelta::Millis(900);
  ep1_ch1()->SetIceConfig(config);
  ep2_ch1()->SetIceConfig(config);
  // Need to wait to make sure the connections on both networks are writable.
  EXPECT_TRUE(MediumWait().Until([&] {
    return CheckCandidatePairAndConnected(ep1_ch1(), ep2_ch1(), wifi[0],
                                          wifi[1]);
  }));
  Connection* backup_conn =
      GetConnectionWithLocalAddress(ep1_ch1(), cellular[0]);
  ASSERT_NE(nullptr, backup_conn);
  // After a short while, the backup connection will be writable but not
  // receiving because backup connection is pinged at a slower rate.
  EXPECT_TRUE(DefaultWait().Until(
      [&] { return backup_conn->writable() && !backup_conn->receiving(); }));
  reset_selected_candidate_pair_switches();
  // Blackhole any traffic to or from the remote WiFi networks.
  RTC_LOG(LS_INFO) << "Failing over...";
  fw()->AddRule(false, FP_ANY, FD_ANY, wifi[1]);

  int num_switches = 0;
  (void)DefaultWait().Until([&] {
    return (num_switches = reset_selected_candidate_pair_switches()) > 0;
  });
  EXPECT_EQ(0, num_switches);
  DestroyChannels();
}

// Tests that a Wifi-Wifi connection has the highest precedence.
TEST_F(P2PTransportChannelMultihomedTest, TestPreferWifiToWifiConnection) {
  CreatePortAllocators();
  // The interface names are chosen so that `cellular` would have higher
  // candidate priority if it is not for the network type.
  auto& wifi = kAlternateAddrs;
  auto& cellular = kPublicAddrs;
  AddAddress(0, wifi[0], "test0", ADAPTER_TYPE_WIFI);
  AddAddress(0, cellular[0], "test1", ADAPTER_TYPE_CELLULAR);
  AddAddress(1, wifi[1], "test0", ADAPTER_TYPE_WIFI);
  AddAddress(1, cellular[1], "test1", ADAPTER_TYPE_CELLULAR);

  // Use only local ports for simplicity.
  SetAllocatorFlags(0, kOnlyLocalPorts);
  SetAllocatorFlags(1, kOnlyLocalPorts);

  // Create channels and let them go writable, as usual.
  CreateChannels();

  EXPECT_TRUE(DefaultWait().Until(
      [&]() { return CheckConnected(ep1_ch1(), ep2_ch1()); }));
  // Need to wait to make sure the connections on both networks are writable.
  EXPECT_TRUE(DefaultWait().Until([&] {
    return CheckCandidatePairAndConnected(ep1_ch1(), ep2_ch1(), wifi[0],
                                          wifi[1]);
  }));
  DestroyChannels();
}

// Tests that a Wifi-Cellular connection has higher precedence than
// a Cellular-Cellular connection.
TEST_F(P2PTransportChannelMultihomedTest, TestPreferWifiOverCellularNetwork) {
  CreatePortAllocators();
  // The interface names are chosen so that `cellular` would have higher
  // candidate priority if it is not for the network type.
  auto& wifi = kAlternateAddrs;
  auto& cellular = kPublicAddrs;
  AddAddress(0, cellular[0], "test1", ADAPTER_TYPE_CELLULAR);
  AddAddress(1, wifi[1], "test0", ADAPTER_TYPE_WIFI);
  AddAddress(1, cellular[1], "test1", ADAPTER_TYPE_CELLULAR);

  // Use only local ports for simplicity.
  SetAllocatorFlags(0, kOnlyLocalPorts);
  SetAllocatorFlags(1, kOnlyLocalPorts);

  // Create channels and let them go writable, as usual.
  CreateChannels();

  EXPECT_TRUE(DefaultWait().Until([&]() {
    return CheckCandidatePairAndConnected(ep1_ch1(), ep2_ch1(), cellular[0],
                                          wifi[1]);
  }));
  DestroyChannels();
}

// Test that the backup connection is pinged at a rate no faster than
// what was configured.
TEST_F(P2PTransportChannelMultihomedTest, TestPingBackupConnectionRate) {
  CreatePortAllocators();
  AddAddress(0, kPublicAddrs[0]);
  // Adding alternate address will make sure `kPublicAddrs` has the higher
  // priority than others. This is due to FakeNetwork::AddInterface method.
  AddAddress(1, kAlternateAddrs[1]);
  AddAddress(1, kPublicAddrs[1]);

  // Use only local ports for simplicity.
  SetAllocatorFlags(0, kOnlyLocalPorts);
  SetAllocatorFlags(1, kOnlyLocalPorts);

  // Create channels and let them go writable, as usual.
  CreateChannels();
  EXPECT_TRUE(DefaultWait().Until(
      [&] { return CheckConnected(ep1_ch1(), ep2_ch1()); }));
  TimeDelta backup_ping_interval = TimeDelta::Seconds(2);
  ep2_ch1()->SetIceConfig(CreateIceConfig(TimeDelta::Seconds(2), GATHER_ONCE,
                                          backup_ping_interval));
  // After the state becomes COMPLETED, the backup connection will be pinged
  // once every `backup_ping_interval` milliseconds.
  ASSERT_TRUE(DefaultWait().Until([&] {
    return ep2_ch1()->GetState() == IceTransportStateInternal::STATE_COMPLETED;
  }));
  auto connections = ep2_ch1()->connections();
  ASSERT_EQ(2U, connections.size());
  Connection* backup_conn = GetBackupConnection(ep2_ch1());
  EXPECT_TRUE(DefaultWait().Until([&] { return backup_conn->writable(); }));
  Timestamp last_ping_response = backup_conn->LastPingResponseReceived();
  EXPECT_TRUE(MediumWait().Until([&] {
    return backup_conn->LastPingResponseReceived() > last_ping_response;
  }));
  TimeDelta time_elapsed =
      backup_conn->LastPingResponseReceived() - last_ping_response;
  RTC_LOG(LS_INFO) << "Time elapsed: " << time_elapsed;
  EXPECT_GE(time_elapsed, backup_ping_interval);

  DestroyChannels();
}

// Test that the connection is pinged at a rate no faster than
// what was configured when stable and writable.
TEST_F(P2PTransportChannelMultihomedTest, TestStableWritableRate) {
  CreatePortAllocators();
  AddAddress(0, kPublicAddrs[0]);
  // Adding alternate address will make sure `kPublicAddrs` has the higher
  // priority than others. This is due to FakeNetwork::AddInterface method.
  AddAddress(1, kAlternateAddrs[1]);
  AddAddress(1, kPublicAddrs[1]);

  // Use only local ports for simplicity.
  SetAllocatorFlags(0, kOnlyLocalPorts);
  SetAllocatorFlags(1, kOnlyLocalPorts);

  // Create channels and let them go writable, as usual.
  CreateChannels();
  EXPECT_TRUE(DefaultWait().Until(
      [&] { return CheckConnected(ep1_ch1(), ep2_ch1()); }));
  // Set a value larger than the default value of 2500 ms
  TimeDelta ping_interval = TimeDelta::Millis(3'456);
  IceConfig config = CreateIceConfig(2 * ping_interval, GATHER_ONCE);
  config.stable_writable_connection_ping_interval = ping_interval;
  ep2_ch1()->SetIceConfig(config);
  // After the state becomes COMPLETED and is stable and writable, the
  // connection will be pinged once every `ping_interval`.
  ASSERT_TRUE(DefaultWait().Until([&] {
    return ep2_ch1()->GetState() == IceTransportStateInternal::STATE_COMPLETED;
  }));
  auto connections = ep2_ch1()->connections();
  ASSERT_EQ(2U, connections.size());
  Connection* conn = GetBestConnection(ep2_ch1());
  EXPECT_TRUE(DefaultWait().Until([&] { return conn->writable(); }));

  Timestamp last_ping_response = Timestamp::Zero();
  // Burn through some pings so the connection is stable.
  for (int i = 0; i < 5; i++) {
    last_ping_response = conn->LastPingResponseReceived();
    EXPECT_TRUE(DefaultWait().Until(
        [&] { return conn->LastPingResponseReceived() > last_ping_response; }));
  }
  EXPECT_TRUE(conn->stable(last_ping_response)) << "Connection not stable";
  TimeDelta time_elapsed =
      conn->LastPingResponseReceived() - last_ping_response;
  RTC_LOG(LS_INFO) << "Time elapsed: " << time_elapsed;
  EXPECT_GE(time_elapsed, ping_interval);

  DestroyChannels();
}

TEST_F(P2PTransportChannelMultihomedTest, TestGetState) {
  CreatePortAllocators();
  AddAddress(0, kAlternateAddrs[0]);
  AddAddress(0, kPublicAddrs[0]);
  AddAddress(1, kPublicAddrs[1]);
  // Create channels and let them go writable, as usual.
  CreateChannels();

  // Both transport channels will reach STATE_COMPLETED quickly.
  EXPECT_THAT(
      DefaultWait().Until([&] { return ep1_ch1()->GetState(); },
                          Eq(IceTransportStateInternal::STATE_COMPLETED)),
      IsRtcOk());
  EXPECT_THAT(ShortWait().Until([&] { return ep2_ch1()->GetState(); },
                                Eq(IceTransportStateInternal::STATE_COMPLETED)),
              IsRtcOk());
  DestroyChannels();
}

// Tests that when a network interface becomes inactive, if Continual Gathering
// policy is GATHER_CONTINUALLY, the ports associated with that network
// will be removed from the port list of the channel, and the respective
// remote candidates on the other participant will be removed eventually.
TEST_F(P2PTransportChannelMultihomedTest, TestNetworkBecomesInactive) {
  CreatePortAllocators();
  AddAddress(0, kPublicAddrs[0]);
  AddAddress(1, kPublicAddrs[1]);
  // Create channels and let them go writable, as usual.
  IceConfig ep1_config =
      CreateIceConfig(TimeDelta::Seconds(2), GATHER_CONTINUALLY);
  IceConfig ep2_config = CreateIceConfig(TimeDelta::Seconds(2), GATHER_ONCE);
  CreateChannels(ep1_config, ep2_config);

  SetAllocatorFlags(0, kOnlyLocalPorts);
  SetAllocatorFlags(1, kOnlyLocalPorts);
  ASSERT_TRUE(
      ShortWait().Until([&] { return CheckConnected(ep1_ch1(), ep2_ch1()); }));
  // More than one port has been created.
  EXPECT_LE(1U, ep1_ch1()->ports().size());
  // Endpoint 1 enabled continual gathering; the port will be removed
  // when the interface is removed.
  RemoveAddress(0, kPublicAddrs[0]);
  EXPECT_TRUE(ep1_ch1()->ports().empty());
  // The remote candidates will be removed eventually.
  EXPECT_TRUE(DefaultWait().Until(
      [&] { return ep2_ch1()->remote_candidates().empty(); }));

  size_t num_ports = ep2_ch1()->ports().size();
  EXPECT_LE(1U, num_ports);
  size_t num_remote_candidates = ep1_ch1()->remote_candidates().size();
  // Endpoint 2 did not enable continual gathering; the local port will still be
  // removed when the interface is removed but the remote candidates on the
  // other participant will not be removed.
  RemoveAddress(1, kPublicAddrs[1]);

  EXPECT_THAT(
      DefaultWait().Until([&] { return ep2_ch1()->ports().size(); }, Eq(0U)),
      IsRtcOk());
  (void)DefaultWait().Until(
      [&] { return ep1_ch1()->remote_candidates().empty(); });
  EXPECT_EQ(num_remote_candidates, ep1_ch1()->remote_candidates().size());

  DestroyChannels();
}

// Tests that continual gathering will create new connections when a new
// interface is added.
TEST_F(P2PTransportChannelMultihomedTest,
       TestContinualGatheringOnNewInterface) {
  CreatePortAllocators();
  auto& wifi = kAlternateAddrs;
  auto& cellular = kPublicAddrs;
  AddAddress(0, wifi[0], "test_wifi0", ADAPTER_TYPE_WIFI);
  AddAddress(1, cellular[1], "test_cell1", ADAPTER_TYPE_CELLULAR);
  // Set continual gathering policy.
  IceConfig continual_gathering_config =
      CreateIceConfig(TimeDelta::Seconds(1), GATHER_CONTINUALLY);
  CreateChannels(continual_gathering_config, continual_gathering_config);
  SetAllocatorFlags(0, kOnlyLocalPorts);
  SetAllocatorFlags(1, kOnlyLocalPorts);
  EXPECT_TRUE(DefaultWait().Until(
      [&] { return CheckConnected(ep1_ch1(), ep2_ch1()); }));

  // Add a new wifi interface on end point 2. We should expect a new connection
  // to be created and the new one will be the best connection.
  AddAddress(1, wifi[1], "test_wifi1", ADAPTER_TYPE_WIFI);
  const Connection* conn;
  EXPECT_TRUE(DefaultWait().Until([&] {
    return (conn = ep1_ch1()->selected_connection()) != nullptr &&
           HasRemoteAddress(conn, wifi[1]);
  }));
  EXPECT_TRUE(DefaultWait().Until([&] {
    return (conn = ep2_ch1()->selected_connection()) != nullptr &&
           HasLocalAddress(conn, wifi[1]);
  }));

  // Add a new cellular interface on end point 1, we should expect a new
  // backup connection created using this new interface.
  AddAddress(0, cellular[0], "test_cellular0", ADAPTER_TYPE_CELLULAR);
  EXPECT_TRUE(DefaultWait().Until([&] {
    return ep1_ch1()->GetState() ==
               IceTransportStateInternal::STATE_COMPLETED &&
           absl::c_any_of(ep1_ch1()->connections(),
                          [channel = ep1_ch1(),
                           address = cellular[0]](const Connection* conn) {
                            return HasLocalAddress(conn, address) &&
                                   conn != channel->selected_connection() &&
                                   conn->writable();
                          });
  }));
  EXPECT_TRUE(DefaultWait().Until([&] {
    return ep2_ch1()->GetState() ==
               IceTransportStateInternal::STATE_COMPLETED &&
           absl::c_any_of(ep2_ch1()->connections(),
                          [channel = ep2_ch1(),
                           address = cellular[0]](const Connection* conn) {
                            return HasRemoteAddress(conn, address) &&
                                   conn != channel->selected_connection() &&
                                   conn->receiving();
                          });
  }));

  DestroyChannels();
}

// Tests that we can switch links via continual gathering.
TEST_F(P2PTransportChannelMultihomedTest,
       TestSwitchLinksViaContinualGathering) {
  CreatePortAllocators();
  AddAddress(0, kPublicAddrs[0]);
  AddAddress(1, kPublicAddrs[1]);
  // Use only local ports for simplicity.
  SetAllocatorFlags(0, kOnlyLocalPorts);
  SetAllocatorFlags(1, kOnlyLocalPorts);

  // Set continual gathering policy.
  IceConfig continual_gathering_config =
      CreateIceConfig(TimeDelta::Seconds(1), GATHER_CONTINUALLY);
  // Create channels and let them go writable, as usual.
  CreateChannels(continual_gathering_config, continual_gathering_config);
  EXPECT_TRUE(DefaultWait().Until([&] {
    return CheckCandidatePairAndConnected(ep1_ch1(), ep2_ch1(), kPublicAddrs[0],
                                          kPublicAddrs[1]);
  }));

  // Add the new address first and then remove the other one.
  RTC_LOG(LS_INFO) << "Draining...";
  AddAddress(1, kAlternateAddrs[1]);
  RemoveAddress(1, kPublicAddrs[1]);
  // We should switch to use the alternate address after an exchange of pings.
  EXPECT_TRUE(MediumWait().Until([&] {
    return CheckCandidatePairAndConnected(ep1_ch1(), ep2_ch1(), kPublicAddrs[0],
                                          kAlternateAddrs[1]);
  }));

  // Remove one address first and then add another address.
  RTC_LOG(LS_INFO) << "Draining again...";
  RemoveAddress(1, kAlternateAddrs[1]);
  AddAddress(1, kAlternateAddrs[0]);
  EXPECT_TRUE(MediumWait().Until([&] {
    return CheckCandidatePairAndConnected(ep1_ch1(), ep2_ch1(), kPublicAddrs[0],
                                          kAlternateAddrs[0]);
  }));

  DestroyChannels();
}

// Tests that the backup connection will be restored after it is destroyed.
TEST_F(P2PTransportChannelMultihomedTest, TestRestoreBackupConnection) {
  CreatePortAllocators();
  auto& wifi = kAlternateAddrs;
  auto& cellular = kPublicAddrs;
  AddAddress(0, wifi[0], "test_wifi0", ADAPTER_TYPE_WIFI);
  AddAddress(0, cellular[0], "test_cell0", ADAPTER_TYPE_CELLULAR);
  AddAddress(1, wifi[1], "test_wifi1", ADAPTER_TYPE_WIFI);
  AddAddress(1, cellular[1], "test_cell1", ADAPTER_TYPE_CELLULAR);
  // Use only local ports for simplicity.
  SetAllocatorFlags(0, kOnlyLocalPorts);
  SetAllocatorFlags(1, kOnlyLocalPorts);

  // Create channels and let them go writable, as usual.
  IceConfig config = CreateIceConfig(TimeDelta::Seconds(1), GATHER_CONTINUALLY);
  config.regather_on_failed_networks_interval = TimeDelta::Seconds(2);
  CreateChannels(config, config);
  EXPECT_TRUE(MediumWait().Until([&] {
    return CheckCandidatePairAndConnected(ep1_ch1(), ep2_ch1(), wifi[0],
                                          wifi[1]);
  }));

  // Destroy all backup connections.
  DestroyAllButBestConnection(ep1_ch1());
  // Ensure the backup connection is removed first.
  EXPECT_THAT(
      MediumWait().Until(
          [&] { return GetConnectionWithLocalAddress(ep1_ch1(), cellular[0]); },
          Eq(nullptr)),
      IsRtcOk());
  const Connection* conn;
  EXPECT_TRUE(DefaultWait().Until([&] {
    return (conn = GetConnectionWithLocalAddress(ep1_ch1(), cellular[0])) !=
               nullptr &&
           conn != ep1_ch1()->selected_connection() && conn->writable();
  }));

  DestroyChannels();
}

TEST_F(P2PTransportChannelMultihomedTest, TestVpnDefault) {
  CreatePortAllocators();
  AddAddress(0, kPublicAddrs[0], "eth0", ADAPTER_TYPE_ETHERNET);
  AddAddress(0, kAlternateAddrs[0], "vpn0", ADAPTER_TYPE_VPN);
  AddAddress(1, kPublicAddrs[1]);

  IceConfig config;
  CreateChannels(config, config, false);
  EXPECT_TRUE(DefaultWait().Until([&] {
    return CheckConnected(ep1_ch1(), ep2_ch1()) &&
           !ep1_ch1()->selected_connection()->network()->IsVpn();
  }));
}

TEST_F(P2PTransportChannelMultihomedTest, TestVpnPreferVpn) {
  CreatePortAllocators();
  AddAddress(0, kPublicAddrs[0], "eth0", ADAPTER_TYPE_ETHERNET);
  AddAddress(0, kAlternateAddrs[0], "vpn0", ADAPTER_TYPE_VPN,
             ADAPTER_TYPE_CELLULAR);
  AddAddress(1, kPublicAddrs[1]);

  IceConfig config;
  config.vpn_preference = VpnPreference::kPreferVpn;
  RTC_LOG(LS_INFO) << "KESO: config.vpn_preference: " << config.vpn_preference;
  CreateChannels(config, config, false);
  EXPECT_TRUE(DefaultWait().Until([&] {
    return CheckConnected(ep1_ch1(), ep2_ch1()) &&
           ep1_ch1()->selected_connection()->network()->IsVpn();
  }));

  // Block VPN.
  fw()->AddRule(false, FP_ANY, FD_ANY, kAlternateAddrs[0]);

  // Check that it switches to non-VPN
  EXPECT_TRUE(DefaultWait().Until([&] {
    return CheckConnected(ep1_ch1(), ep2_ch1()) &&
           !ep1_ch1()->selected_connection()->network()->IsVpn();
  }));
}

TEST_F(P2PTransportChannelMultihomedTest, TestVpnAvoidVpn) {
  CreatePortAllocators();
  AddAddress(0, kPublicAddrs[0], "eth0", ADAPTER_TYPE_CELLULAR);
  AddAddress(0, kAlternateAddrs[0], "vpn0", ADAPTER_TYPE_VPN,
             ADAPTER_TYPE_ETHERNET);
  AddAddress(1, kPublicAddrs[1]);

  IceConfig config;
  config.vpn_preference = VpnPreference::kAvoidVpn;
  CreateChannels(config, config, false);
  EXPECT_TRUE(DefaultWait().Until([&] {
    return CheckConnected(ep1_ch1(), ep2_ch1()) &&
           !ep1_ch1()->selected_connection()->network()->IsVpn();
  }));

  // Block non-VPN.
  fw()->AddRule(false, FP_ANY, FD_ANY, kPublicAddrs[0]);

  // Check that it switches to VPN
  EXPECT_TRUE(DefaultWait().Until([&] {
    return CheckConnected(ep1_ch1(), ep2_ch1()) &&
           ep1_ch1()->selected_connection()->network()->IsVpn();
  }));
}

TEST_F(P2PTransportChannelMultihomedTest, TestVpnNeverVpn) {
  CreatePortAllocators();
  AddAddress(0, kPublicAddrs[0], "eth0", ADAPTER_TYPE_CELLULAR);
  AddAddress(0, kAlternateAddrs[0], "vpn0", ADAPTER_TYPE_VPN,
             ADAPTER_TYPE_ETHERNET);
  AddAddress(1, kPublicAddrs[1]);

  IceConfig config;
  config.vpn_preference = VpnPreference::kNeverUseVpn;
  CreateChannels(config, config, false);
  EXPECT_TRUE(DefaultWait().Until([&] {
    return CheckConnected(ep1_ch1(), ep2_ch1()) &&
           !ep1_ch1()->selected_connection()->network()->IsVpn();
  }));

  // Block non-VPN.
  fw()->AddRule(false, FP_ANY, FD_ANY, kPublicAddrs[0]);

  // Check that it does not switches to VPN
  time_controller_.AdvanceTime(kDefaultTimeout);
  EXPECT_TRUE(DefaultWait().Until(
      [&] { return !CheckConnected(ep1_ch1(), ep2_ch1()); }));
}

TEST_F(P2PTransportChannelMultihomedTest, TestVpnOnlyVpn) {
  CreatePortAllocators();
  AddAddress(0, kPublicAddrs[0], "eth0", ADAPTER_TYPE_CELLULAR);
  AddAddress(0, kAlternateAddrs[0], "vpn0", ADAPTER_TYPE_VPN,
             ADAPTER_TYPE_ETHERNET);
  AddAddress(1, kPublicAddrs[1]);

  IceConfig config;
  config.vpn_preference = VpnPreference::kOnlyUseVpn;
  CreateChannels(config, config, false);
  EXPECT_TRUE(DefaultWait().Until([&] {
    return CheckConnected(ep1_ch1(), ep2_ch1()) &&
           ep1_ch1()->selected_connection()->network()->IsVpn();
  }));

  // Block VPN.
  fw()->AddRule(false, FP_ANY, FD_ANY, kAlternateAddrs[0]);

  // Check that it does not switch to non-VPN
  time_controller_.AdvanceTime(kDefaultTimeout);
  EXPECT_TRUE(DefaultWait().Until(
      [&] { return !CheckConnected(ep1_ch1(), ep2_ch1()); }));
}

TEST_F(P2PTransportChannelMultihomedTest, StunDictionaryPerformsSync) {
  CreatePortAllocators();
  AddAddress(0, kPublicAddrs[0], "eth0", ADAPTER_TYPE_CELLULAR);
  AddAddress(0, kAlternateAddrs[0], "vpn0", ADAPTER_TYPE_VPN,
             ADAPTER_TYPE_ETHERNET);
  AddAddress(1, kPublicAddrs[1]);

  // Create channels and let them go writable, as usual.
  CreateChannels();

  MockFunction<void(IceTransportInternal*, const StunDictionaryView&,
                    std::span<uint16_t>)>
      view_updated_func;
  ep2_ch1()->AddDictionaryViewUpdatedCallback(
      "tag", view_updated_func.AsStdFunction());
  MockFunction<void(IceTransportInternal*, const StunDictionaryWriter&)>
      writer_synced_func;
  ep1_ch1()->AddDictionaryWriterSyncedCallback(
      "tag", writer_synced_func.AsStdFunction());
  auto& dict_writer = ep1_ch1()->GetDictionaryWriter()->get();
  dict_writer.SetByteString(12)->CopyBytes("keso");
  EXPECT_CALL(view_updated_func, Call)
      .WillOnce([&](auto* channel, auto& view, auto keys) {
        EXPECT_EQ(keys.size(), 1u);
        EXPECT_EQ(keys[0], 12);
        EXPECT_EQ(view.GetByteString(12)->string_view(), "keso");
      });
  EXPECT_CALL(writer_synced_func, Call).Times(1);
  EXPECT_TRUE(DefaultWait().Until(
      [&] { return CheckConnected(ep1_ch1(), ep2_ch1()); }));
}

// A collection of tests which tests a single P2PTransportChannel by sending
// pings.
class P2PTransportChannelPingTest : public ::testing::Test {
 public:
  P2PTransportChannelPingTest()
      : vss_(std::make_unique<VirtualSocketServer>()),
        time_controller_(Timestamp::Millis(1000), vss_.get()),
        env_(CreateTestEnvironment({.time = &time_controller_})),
        packet_socket_factory_(
            std::make_unique<BasicPacketSocketFactory>(vss_.get())),
        thread_(time_controller_.GetMainThread()) {}

 protected:
  Waiter DefaultWait() {
    return Waiter({.timeout = kDefaultTimeout, .clock = &time_controller_});
  }
  Waiter MediumWait() {
    return Waiter({.timeout = kMediumTimeout, .clock = &time_controller_});
  }
  Waiter ShortWait() {
    return Waiter({.timeout = kShortTimeout, .clock = &time_controller_});
  }
  Waiter Wait(TimeDelta timeout) {
    return Waiter({.timeout = timeout, .clock = &time_controller_});
  }

  void PrepareChannel(P2PTransportChannel* ch) {
    ch->SetIceRole(ICEROLE_CONTROLLING);
    ch->SetIceParameters(kIceParams[0]);
    ch->SetRemoteIceParameters(kIceParams[1]);
    ch->SubscribeNetworkRouteChanged(
        this, [this](std::optional<NetworkRoute> network_route) {
          OnNetworkRouteChanged(network_route);
        });
    ch->SubscribeReadyToSend(this, [this](PacketTransportInternal* transport) {
      OnReadyToSend(transport);
    });
    ch->SubscribeIceTransportStateChanged(
        this, [this](IceTransportInternal* transport) {
          OnChannelStateChanged(transport);
        });
    ch->SetCandidatePairChangeCallback(
        [this](const CandidatePairChangeEvent& event) {
          OnCandidatePairChanged(event);
        });
  }

  Connection* WaitForConnectionTo(P2PTransportChannel* ch,
                                  absl::string_view ip,
                                  int port_num) {
    EXPECT_THAT(
        MediumWait().Until([&] { return GetConnectionTo(ch, ip, port_num); },
                           Ne(nullptr)),
        IsRtcOk());
    return GetConnectionTo(ch, ip, port_num);
  }

  Port* GetPort(P2PTransportChannel* ch) {
    if (ch->ports().empty()) {
      return nullptr;
    }
    return static_cast<Port*>(ch->ports()[0]);
  }

  Port* GetPrunedPort(P2PTransportChannel* ch) {
    if (ch->pruned_ports().empty()) {
      return nullptr;
    }
    return static_cast<Port*>(ch->pruned_ports()[0]);
  }

  Connection* GetConnectionTo(P2PTransportChannel* ch,
                              absl::string_view ip,
                              int port_num) {
    Port* port = GetPort(ch);
    if (!port) {
      return nullptr;
    }
    return port->GetConnection(SocketAddress(ip, port_num));
  }

  Connection* FindNextPingableConnectionAndPingIt(P2PTransportChannel* ch) {
    Connection* conn = ch->FindNextPingableConnection();
    if (conn) {
      ch->MarkConnectionPinged(conn);
    }
    return conn;
  }

  int SendData(IceTransportInternal* channel,
               const char* data,
               size_t len,
               int packet_id) {
    AsyncSocketPacketOptions options;
    options.packet_id = packet_id;
    return channel->SendPacket(data, len, options, 0);
  }

  Connection* CreateConnectionWithCandidate(P2PTransportChannel* channel,
                                            absl::string_view ip_addr,
                                            int port,
                                            int priority,
                                            bool writable) {
    channel->AddRemoteCandidate(
        CreateUdpCandidate(IceCandidateType::kHost, ip_addr, port, priority));
    EXPECT_THAT(MediumWait().Until(
                    [&] { return GetConnectionTo(channel, ip_addr, port); },
                    Ne(nullptr)),
                IsRtcOk());
    Connection* conn = GetConnectionTo(channel, ip_addr, port);

    if (conn && writable) {
      conn->ReceivedPingResponse(kLowRtt, "id");  // make it writable
    }
    return conn;
  }

  void NominateConnection(Connection* conn, uint32_t remote_nomination = 1U) {
    conn->set_remote_nomination(remote_nomination);
    conn->NotifyNominatedForTesting(conn);
  }

  void OnNetworkRouteChanged(std::optional<NetworkRoute> network_route) {
    last_network_route_ = network_route;
    if (last_network_route_) {
      last_sent_packet_id_ = last_network_route_->last_sent_packet_id;
    }
    ++selected_candidate_pair_switches_;
  }

  void ReceivePingOnConnection(
      Connection* conn,
      absl::string_view remote_ufrag,
      int priority,
      uint32_t nomination,
      const std::optional<std::string>& piggyback_ping_id) {
    IceMessage msg(STUN_BINDING_REQUEST);
    msg.AddAttribute(std::make_unique<StunByteStringAttribute>(
        STUN_ATTR_USERNAME,
        conn->local_candidate().username() + ":" + std::string(remote_ufrag)));
    msg.AddAttribute(
        std::make_unique<StunUInt32Attribute>(STUN_ATTR_PRIORITY, priority));
    if (nomination != 0) {
      msg.AddAttribute(std::make_unique<StunUInt32Attribute>(
          STUN_ATTR_NOMINATION, nomination));
    }
    if (piggyback_ping_id) {
      msg.AddAttribute(std::make_unique<StunByteStringAttribute>(
          STUN_ATTR_GOOG_LAST_ICE_CHECK_RECEIVED, piggyback_ping_id.value()));
    }
    msg.AddMessageIntegrity(conn->local_candidate().password());
    msg.AddFingerprint();
    ByteBufferWriter buf;
    msg.Write(&buf);
    conn->OnReadPacket(ReceivedIpPacket(buf.DataView(), SocketAddress()));
  }

  void ReceivePingOnConnection(Connection* conn,
                               absl::string_view remote_ufrag,
                               int priority,
                               uint32_t nomination = 0) {
    ReceivePingOnConnection(conn, remote_ufrag, priority, nomination,
                            std::nullopt);
  }

  void OnReadyToSend(PacketTransportInternal* transport) {
    channel_ready_to_send_ = true;
  }
  void OnChannelStateChanged(IceTransportInternal* channel) {
    channel_state_ = channel->GetState();
  }
  void OnCandidatePairChanged(const CandidatePairChangeEvent& event) {
    RTC_DCHECK(!event.transport_name.empty());
    last_candidate_change_event_ = event;
  }

  int last_sent_packet_id() { return last_sent_packet_id_; }
  bool channel_ready_to_send() { return channel_ready_to_send_; }
  void reset_channel_ready_to_send() { channel_ready_to_send_ = false; }
  IceTransportStateInternal channel_state() { return channel_state_; }
  int reset_selected_candidate_pair_switches() {
    int switches = selected_candidate_pair_switches_;
    selected_candidate_pair_switches_ = 0;
    return switches;
  }

  // Return true if the `pair` matches the last network route.
  bool CandidatePairMatchesNetworkRoute(CandidatePairInterface* pair) {
    if (!pair) {
      return !last_network_route_.has_value();
    } else {
      return pair->local_candidate().network_id() ==
                 last_network_route_->local.network_id() &&
             pair->remote_candidate().network_id() ==
                 last_network_route_->remote.network_id();
    }
  }

  bool ConnectionMatchesChangeEvent(Connection* conn,
                                    absl::string_view reason) {
    if (!conn) {
      return !last_candidate_change_event_.has_value();
    } else {
      const auto& last_selected_pair =
          last_candidate_change_event_->selected_candidate_pair;
      return last_selected_pair.local_candidate().IsEquivalent(
                 conn->local_candidate()) &&
             last_selected_pair.remote_candidate().IsEquivalent(
                 conn->remote_candidate()) &&
             last_candidate_change_event_->last_data_received_ms ==
                 conn->LastDataReceived().ms() &&
             last_candidate_change_event_->reason == reason;
    }
  }

  int64_t LastEstimatedDisconnectedTimeMs() const {
    if (!last_candidate_change_event_.has_value()) {
      return 0;
    } else {
      return last_candidate_change_event_->estimated_disconnected_time_ms;
    }
  }

  SocketServer* ss() const { return vss_.get(); }

  PacketSocketFactory* packet_socket_factory() const {
    return packet_socket_factory_.get();
  }

 protected:
  std::unique_ptr<VirtualSocketServer> vss_;
  GlobalSimulatedTimeController time_controller_;
  Environment env_;
  std::unique_ptr<PacketSocketFactory> packet_socket_factory_;
  Thread* thread_;
  int selected_candidate_pair_switches_ = 0;
  int last_sent_packet_id_ = -1;
  bool channel_ready_to_send_ = false;
  std::optional<CandidatePairChangeEvent> last_candidate_change_event_;
  IceTransportStateInternal channel_state_ =
      IceTransportStateInternal::STATE_INIT;
  std::optional<NetworkRoute> last_network_route_;
};

TEST_F(P2PTransportChannelPingTest, TestTriggeredChecks) {
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "trigger checks", 1, &pa);
  PrepareChannel(&ch);
  ch.MaybeStartGathering();
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 1));
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "2.2.2.2", 2, 2));

  Connection* conn1 = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  Connection* conn2 = WaitForConnectionTo(&ch, "2.2.2.2", 2);
  ASSERT_TRUE(conn1 != nullptr);
  ASSERT_TRUE(conn2 != nullptr);

  // Before a triggered check, the first connection to ping is the
  // highest priority one.
  EXPECT_EQ(conn2, FindNextPingableConnectionAndPingIt(&ch));

  // Receiving a ping causes a triggered check which should make conn1
  // be pinged first instead of conn2, even though conn2 has a higher
  // priority.
  conn1->ReceivedPing();
  EXPECT_EQ(conn1, FindNextPingableConnectionAndPingIt(&ch));
}

TEST_F(P2PTransportChannelPingTest, TestAllConnectionsPingedSufficiently) {
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "ping sufficiently", 1, &pa);
  PrepareChannel(&ch);
  ch.MaybeStartGathering();
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 1));
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "2.2.2.2", 2, 2));

  Connection* conn1 = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  Connection* conn2 = WaitForConnectionTo(&ch, "2.2.2.2", 2);
  ASSERT_TRUE(conn1 != nullptr);
  ASSERT_TRUE(conn2 != nullptr);

  // Low-priority connection becomes writable so that the other connection
  // is not pruned.
  conn1->ReceivedPingResponse(kLowRtt, "id");
  EXPECT_TRUE(MediumWait().Until([&] {
    return conn1->num_pings_sent() >= kMinPingsAtWeakPingInterval &&
           conn2->num_pings_sent() >= kMinPingsAtWeakPingInterval;
  }));
}

// Verify that the connections are pinged at the right time.
TEST_F(P2PTransportChannelPingTest, TestStunPingIntervals) {
  int RTT_RATIO = 4;
  constexpr TimeDelta kSchedulingRange = TimeDelta::Millis(200);
  constexpr TimeDelta kRttRange = TimeDelta::Millis(10);

  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "TestChannel", 1, &pa);
  PrepareChannel(&ch);
  ch.MaybeStartGathering();
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 1));
  Connection* conn = WaitForConnectionTo(&ch, "1.1.1.1", 1);

  ASSERT_TRUE(conn != nullptr);
  EXPECT_TRUE(DefaultWait().Until([&] { return conn->num_pings_sent() == 1; }));

  // Initializing.

  Timestamp start = env_.clock().CurrentTime();
  EXPECT_TRUE(DefaultWait().Until(
      [&] { return conn->num_pings_sent() >= kMinPingsAtWeakPingInterval; }));
  TimeDelta ping_interval =
      (env_.clock().CurrentTime() - start) / (kMinPingsAtWeakPingInterval - 1);
  EXPECT_EQ(ping_interval, kWeakPingInterval);

  // Stabilizing.

  conn->ReceivedPingResponse(kLowRtt, "id");
  int ping_sent_before = conn->num_pings_sent();
  start = env_.clock().CurrentTime();
  // The connection becomes strong but not stable because we haven't been able
  // to converge the RTT.
  EXPECT_TRUE(DefaultWait().Until(
      [&] { return conn->num_pings_sent() == ping_sent_before + 1; }));
  ping_interval = env_.clock().CurrentTime() - start;
  EXPECT_GE(ping_interval, kWeakOrStabilizingWritableConnectionPingInterval);
  EXPECT_LE(ping_interval, kWeakOrStabilizingWritableConnectionPingInterval +
                               kSchedulingRange);

  // Stabilized.

  // The connection becomes stable after receiving more than RTT_RATIO rtt
  // samples.
  for (int i = 0; i < RTT_RATIO; i++) {
    conn->ReceivedPingResponse(kLowRtt, "id");
  }
  ping_sent_before = conn->num_pings_sent();
  start = env_.clock().CurrentTime();
  EXPECT_TRUE(MediumWait().Until(
      [&] { return conn->num_pings_sent() == ping_sent_before + 1; }));
  ping_interval = env_.clock().CurrentTime() - start;
  EXPECT_GE(ping_interval, kStrongAndStableWritableConnectionPingInterval);
  EXPECT_LE(ping_interval,
            kStrongAndStableWritableConnectionPingInterval + kSchedulingRange);

  // Destabilized.

  conn->ReceivedPingResponse(kLowRtt, "id");
  // Create a in-flight ping.
  conn->Ping();
  start = env_.clock().CurrentTime();
  // In-flight ping timeout and the connection will be unstable.
  EXPECT_TRUE(MediumWait().Until(
      [&] { return !conn->stable(env_.clock().CurrentTime()); }));
  TimeDelta duration = env_.clock().CurrentTime() - start;
  EXPECT_GE(duration, 2 * conn->Rtt() - kRttRange);
  EXPECT_LE(duration, 2 * conn->Rtt() + kRttRange);
  // The connection become unstable due to not receiving ping responses.
  ping_sent_before = conn->num_pings_sent();
  EXPECT_TRUE(MediumWait().Until(
      [&] { return conn->num_pings_sent() == ping_sent_before + 1; }));
  // The interval is expected to be
  // kWeakOrStabilizingWritableConnectionPingInterval.
  start = env_.clock().CurrentTime();
  ping_sent_before = conn->num_pings_sent();
  EXPECT_TRUE(MediumWait().Until(
      [&] { return conn->num_pings_sent() == ping_sent_before + 1; }));
  ping_interval = env_.clock().CurrentTime() - start;
  EXPECT_GE(ping_interval, kWeakOrStabilizingWritableConnectionPingInterval);
  EXPECT_LE(ping_interval, kWeakOrStabilizingWritableConnectionPingInterval +
                               kSchedulingRange);
}

// Test that we start pinging as soon as we have a connection and remote ICE
// parameters.
TEST_F(P2PTransportChannelPingTest, PingingStartedAsSoonAsPossible) {
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "TestChannel", 1, &pa);
  ch.SetIceRole(ICEROLE_CONTROLLING);
  ch.SetIceParameters(kIceParams[0]);
  ch.MaybeStartGathering();
  EXPECT_TRUE(MediumWait().Until([&] {
    return ch.gathering_state() == IceGatheringState::kIceGatheringComplete;
  }));

  // Simulate a binding request being received, creating a peer reflexive
  // candidate pair while we still don't have remote ICE parameters.
  IceMessage request(STUN_BINDING_REQUEST);
  request.AddAttribute(std::make_unique<StunByteStringAttribute>(
      STUN_ATTR_USERNAME, kIceUfrag[1]));
  uint32_t prflx_priority = ICE_TYPE_PREFERENCE_PRFLX << 24;
  request.AddAttribute(std::make_unique<StunUInt32Attribute>(STUN_ATTR_PRIORITY,
                                                             prflx_priority));
  Port* port = GetPort(&ch);
  ASSERT_NE(nullptr, port);
  port->NotifyUnknownAddress(port, SocketAddress("1.1.1.1", 1), PROTO_UDP,
                             &request, kIceUfrag[1], false);
  Connection* conn = GetConnectionTo(&ch, "1.1.1.1", 1);
  ASSERT_NE(nullptr, conn);

  // Simulate waiting for a second (and change) and verify that no pings were
  // sent, since we don't yet have remote ICE parameters.
  time_controller_.AdvanceTime(TimeDelta::Millis(1025));
  EXPECT_EQ(0, conn->num_pings_sent());

  // Set remote ICE parameters. Now we should be able to ping. Ensure that
  // the first ping is sent as soon as possible, within one simulated clock
  // tick.
  ch.SetRemoteIceParameters(kIceParams[1]);
  EXPECT_TRUE(DefaultWait().Until([&] { return conn->num_pings_sent() > 0; }));
}

TEST_F(P2PTransportChannelPingTest, TestNoTriggeredChecksWhenWritable) {
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "trigger checks", 1, &pa);
  PrepareChannel(&ch);
  ch.MaybeStartGathering();
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 1));
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "2.2.2.2", 2, 2));

  Connection* conn1 = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  Connection* conn2 = WaitForConnectionTo(&ch, "2.2.2.2", 2);
  ASSERT_TRUE(conn1 != nullptr);
  ASSERT_TRUE(conn2 != nullptr);

  EXPECT_EQ(conn2, FindNextPingableConnectionAndPingIt(&ch));
  EXPECT_EQ(conn1, FindNextPingableConnectionAndPingIt(&ch));
  conn1->ReceivedPingResponse(kLowRtt, "id");
  ASSERT_TRUE(conn1->writable());
  conn1->ReceivedPing();

  // Ping received, but the connection is already writable, so no
  // "triggered check" and conn2 is pinged before conn1 because it has
  // a higher priority.
  EXPECT_EQ(conn2, FindNextPingableConnectionAndPingIt(&ch));
}

TEST_F(P2PTransportChannelPingTest, TestFailedConnectionNotPingable) {
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "Do not ping failed connections", 1, &pa);
  PrepareChannel(&ch);
  ch.MaybeStartGathering();
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 1));

  Connection* conn1 = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  ASSERT_TRUE(conn1 != nullptr);

  EXPECT_EQ(conn1, ch.FindNextPingableConnection());
  conn1->Prune();  // A pruned connection may still be pingable.
  EXPECT_EQ(conn1, ch.FindNextPingableConnection());
  conn1->FailAndPrune();
  EXPECT_TRUE(nullptr == ch.FindNextPingableConnection());
}

TEST_F(P2PTransportChannelPingTest, TestSignalStateChanged) {
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "state change", 1, &pa);
  PrepareChannel(&ch);
  ch.MaybeStartGathering();
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 1));
  Connection* conn1 = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  ASSERT_TRUE(conn1 != nullptr);
  // Pruning the connection reduces the set of active connections and changes
  // the channel state.
  conn1->Prune();
  EXPECT_THAT(DefaultWait().Until([&] { return channel_state(); },
                                  Eq(IceTransportStateInternal::STATE_FAILED)),
              IsRtcOk());
}

// Test adding remote candidates with different ufrags. If a remote candidate
// is added with an old ufrag, it will be discarded. If it is added with a
// ufrag that was not seen before, it will be used to create connections
// although the ICE pwd in the remote candidate will be set when the ICE
// parameters arrive. If a remote candidate is added with the current ICE
// ufrag, its pwd and generation will be set properly.
TEST_F(P2PTransportChannelPingTest, TestAddRemoteCandidateWithVariousUfrags) {
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "add candidate", 1, &pa);
  PrepareChannel(&ch);
  ch.MaybeStartGathering();
  // Add a candidate with a future ufrag.
  ch.AddRemoteCandidate(CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1",
                                           1, 1, kIceUfrag[2]));
  Connection* conn1 = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  ASSERT_TRUE(conn1 != nullptr);
  const Candidate& candidate = conn1->remote_candidate();
  EXPECT_EQ(kIceUfrag[2], candidate.username());
  EXPECT_TRUE(candidate.password().empty());
  EXPECT_TRUE(FindNextPingableConnectionAndPingIt(&ch) == nullptr);

  // Set the remote ICE parameters with the "future" ufrag.
  // This should set the ICE pwd in the remote candidate of `conn1`, making
  // it pingable.
  ch.SetRemoteIceParameters(kIceParams[2]);
  EXPECT_EQ(kIceUfrag[2], candidate.username());
  EXPECT_EQ(kIcePwd[2], candidate.password());
  EXPECT_EQ(conn1, FindNextPingableConnectionAndPingIt(&ch));

  // Add a candidate with an old ufrag. No connection will be created.
  ch.AddRemoteCandidate(CreateUdpCandidate(IceCandidateType::kHost, "2.2.2.2",
                                           2, 2, kIceUfrag[1]));
  time_controller_.AdvanceTime(TimeDelta::Millis(500));
  EXPECT_TRUE(GetConnectionTo(&ch, "2.2.2.2", 2) == nullptr);

  // Add a candidate with the current ufrag, its pwd and generation will be
  // assigned, even if the generation is not set.
  ch.AddRemoteCandidate(CreateUdpCandidate(IceCandidateType::kHost, "3.3.3.3",
                                           3, 0, kIceUfrag[2]));
  Connection* conn3 = nullptr;
  EXPECT_TRUE(DefaultWait().Until(
      [&] { return (conn3 = GetConnectionTo(&ch, "3.3.3.3", 3)) != nullptr; }));
  const Candidate& new_candidate = conn3->remote_candidate();
  EXPECT_EQ(kIcePwd[2], new_candidate.password());
  EXPECT_EQ(1U, new_candidate.generation());

  // Check that the pwd of all remote candidates are properly assigned.
  for (const RemoteCandidate& remote_candidate : ch.remote_candidates()) {
    EXPECT_TRUE(remote_candidate.username() == kIceUfrag[1] ||
                remote_candidate.username() == kIceUfrag[2]);
    if (remote_candidate.username() == kIceUfrag[1]) {
      EXPECT_EQ(kIcePwd[1], remote_candidate.password());
    } else if (remote_candidate.username() == kIceUfrag[2]) {
      EXPECT_EQ(kIcePwd[2], remote_candidate.password());
    }
  }
}

TEST_F(P2PTransportChannelPingTest, ConnectionResurrection) {
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "connection resurrection", 1, &pa);
  PrepareChannel(&ch);
  ch.MaybeStartGathering();

  // Create conn1 and keep track of original candidate priority.
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 1));
  Connection* conn1 = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  ASSERT_TRUE(conn1 != nullptr);
  uint32_t remote_priority = conn1->remote_candidate().priority();

  // Create a higher priority candidate and make the connection
  // receiving/writable. This will prune conn1.
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "2.2.2.2", 2, 2));
  Connection* conn2 = WaitForConnectionTo(&ch, "2.2.2.2", 2);
  ASSERT_TRUE(conn2 != nullptr);
  conn2->ReceivedPing();
  conn2->ReceivedPingResponse(kLowRtt, "id");

  // Wait for conn2 to be selected.
  EXPECT_THAT(
      MediumWait().Until([&] { return ch.selected_connection(); }, Eq(conn2)),
      IsRtcOk());
  // Destroy the connection to test SignalUnknownAddress.
  ch.RemoveConnectionForTest(conn1);
  EXPECT_THAT(
      MediumWait().Until([&] { return GetConnectionTo(&ch, "1.1.1.1", 1); },
                         Eq(nullptr)),
      IsRtcOk());

  // Create a minimal STUN message with prflx priority.
  IceMessage request(STUN_BINDING_REQUEST);
  request.AddAttribute(std::make_unique<StunByteStringAttribute>(
      STUN_ATTR_USERNAME, kIceUfrag[1]));
  uint32_t prflx_priority = ICE_TYPE_PREFERENCE_PRFLX << 24;
  request.AddAttribute(std::make_unique<StunUInt32Attribute>(STUN_ATTR_PRIORITY,
                                                             prflx_priority));
  EXPECT_NE(prflx_priority, remote_priority);

  Port* port = GetPort(&ch);
  // conn1 should be resurrected with original priority.
  port->NotifyUnknownAddress(port, SocketAddress("1.1.1.1", 1), PROTO_UDP,
                             &request, kIceUfrag[1], false);
  conn1 = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  ASSERT_TRUE(conn1 != nullptr);
  EXPECT_EQ(conn1->remote_candidate().priority(), remote_priority);

  // conn3, a real prflx connection, should have prflx priority.
  port->NotifyUnknownAddress(port, SocketAddress("3.3.3.3", 1), PROTO_UDP,
                             &request, kIceUfrag[1], false);
  Connection* conn3 = WaitForConnectionTo(&ch, "3.3.3.3", 1);
  ASSERT_TRUE(conn3 != nullptr);
  EXPECT_EQ(conn3->remote_candidate().priority(), prflx_priority);
}

TEST_F(P2PTransportChannelPingTest, TestReceivingStateChange) {
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "receiving state change", 1, &pa);
  PrepareChannel(&ch);
  // Default receiving timeout and checking receiving interval should not be too
  // small.
  EXPECT_GE(ch.config().receiving_timeout_or_default(), TimeDelta::Seconds(1));
  EXPECT_GE(ch.check_receiving_interval(), TimeDelta::Millis(200));
  ch.SetIceConfig(CreateIceConfig(TimeDelta::Millis(500), GATHER_ONCE));
  EXPECT_EQ(ch.config().receiving_timeout_or_default(), TimeDelta::Millis(500));
  EXPECT_EQ(ch.check_receiving_interval(), TimeDelta::Millis(50));
  ch.MaybeStartGathering();
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 1));
  Connection* conn1 = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  ASSERT_TRUE(conn1 != nullptr);

  time_controller_.AdvanceTime(TimeDelta::Seconds(1));
  conn1->ReceivedPing();
  conn1->OnReadPacket(ReceivedIpPacket::CreateFromLegacy(
      "ABC", 3, env_.clock().TimeInMicroseconds()));

  EXPECT_TRUE(MediumWait().Until([&] { return ch.receiving(); }));
  EXPECT_TRUE(ShortWait().Until([&] { return !ch.receiving(); }));
}

// The controlled side will select a connection as the "selected connection"
// based on priority until the controlling side nominates a connection, at which
// point the controlled side will select that connection as the
// "selected connection". Plus, SignalNetworkRouteChanged will be fired if the
// selected connection changes and SignalReadyToSend will be fired if the new
// selected connection is writable.
TEST_F(P2PTransportChannelPingTest, TestSelectConnectionBeforeNomination) {
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "receiving state change", 1, &pa);
  PrepareChannel(&ch);
  ch.SetIceRole(ICEROLE_CONTROLLED);
  ch.MaybeStartGathering();
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 1));
  Connection* conn1 = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  ASSERT_TRUE(conn1 != nullptr);
  // Channel is not ready to send because it is not writable.
  EXPECT_FALSE(channel_ready_to_send());
  int last_packet_id = 0;
  const char* data = "ABCDEFGH";
  int len = static_cast<int>(strlen(data));
  EXPECT_EQ(-1, SendData(&ch, data, len, ++last_packet_id));
  EXPECT_EQ(-1, last_sent_packet_id());

  // A connection needs to be writable before it is selected for transmission.
  conn1->ReceivedPingResponse(kLowRtt, "id");
  EXPECT_THAT(
      ShortWait().Until([&] { return ch.selected_connection(); }, Eq(conn1)),
      IsRtcOk());
  EXPECT_TRUE(CandidatePairMatchesNetworkRoute(conn1));
  EXPECT_TRUE(ConnectionMatchesChangeEvent(
      conn1, "remote candidate generation maybe changed"));
  EXPECT_EQ(len, SendData(&ch, data, len, ++last_packet_id));

  // When a higher priority candidate comes in, the new connection is chosen
  // as the selected connection.
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "2.2.2.2", 2, 10));
  Connection* conn2 = WaitForConnectionTo(&ch, "2.2.2.2", 2);
  ASSERT_TRUE(conn2 != nullptr);
  conn2->ReceivedPingResponse(kLowRtt, "id");
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.selected_connection(); }, Eq(conn2)),
      IsRtcOk());
  EXPECT_TRUE(CandidatePairMatchesNetworkRoute(conn2));
  EXPECT_TRUE(
      ConnectionMatchesChangeEvent(conn2, "candidate pair state changed"));
  EXPECT_TRUE(channel_ready_to_send());
  EXPECT_EQ(last_packet_id, last_sent_packet_id());

  // If a stun request with use-candidate attribute arrives, the receiving
  // connection will be set as the selected connection, even though
  // its priority is lower.
  EXPECT_EQ(len, SendData(&ch, data, len, ++last_packet_id));
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "3.3.3.3", 3, 1));
  Connection* conn3 = WaitForConnectionTo(&ch, "3.3.3.3", 3);
  ASSERT_TRUE(conn3 != nullptr);
  // Because it has a lower priority, the selected connection is still conn2.
  EXPECT_EQ(conn2, ch.selected_connection());
  conn3->ReceivedPingResponse(kLowRtt, "id");  // Become writable.
  // But if it is nominated via use_candidate, it is chosen as the selected
  // connection.
  NominateConnection(conn3);
  ASSERT_EQ(conn3, ch.selected_connection());

  EXPECT_TRUE(CandidatePairMatchesNetworkRoute(conn3));
  EXPECT_TRUE(
      ConnectionMatchesChangeEvent(conn3, "nomination on the controlled side"));
  EXPECT_EQ(last_packet_id, last_sent_packet_id());
  EXPECT_TRUE(channel_ready_to_send());

  // Even if another higher priority candidate arrives, it will not be set as
  // the selected connection because the selected connection is nominated by
  // the controlling side.
  EXPECT_EQ(len, SendData(&ch, data, len, ++last_packet_id));
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "4.4.4.4", 4, 100));
  Connection* conn4 = WaitForConnectionTo(&ch, "4.4.4.4", 4);
  ASSERT_TRUE(conn4 != nullptr);
  EXPECT_EQ(conn3, ch.selected_connection());
  // But if it is nominated via use_candidate and writable, it will be set as
  // the selected connection.
  NominateConnection(conn4);
  // Not switched yet because conn4 is not writable.
  EXPECT_EQ(conn3, ch.selected_connection());
  reset_channel_ready_to_send();
  // The selected connection switches after conn4 becomes writable.
  conn4->ReceivedPingResponse(kLowRtt, "id");
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.selected_connection(); }, Eq(conn4)),
      IsRtcOk());
  EXPECT_TRUE(CandidatePairMatchesNetworkRoute(conn4));
  EXPECT_TRUE(
      ConnectionMatchesChangeEvent(conn4, "candidate pair state changed"));
  EXPECT_EQ(last_packet_id, last_sent_packet_id());
  // SignalReadyToSend is fired again because conn4 is writable.
  EXPECT_TRUE(channel_ready_to_send());
}

// Test the field trial send_ping_on_nomination_ice_controlled
// that sends a ping directly when a connection has been nominated
// i.e on the ICE_CONTROLLED-side.
TEST_F(P2PTransportChannelPingTest, TestPingOnNomination) {
  env_ = CreateTestEnvironment(
      {.field_trials = "WebRTC-IceFieldTrials/"
                       "send_ping_on_nomination_ice_controlled:true/",
       .time = &time_controller_});
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "receiving state change", 1, &pa);
  PrepareChannel(&ch);
  ch.SetIceConfig(ch.config());
  ch.SetIceRole(ICEROLE_CONTROLLED);
  ch.MaybeStartGathering();
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 1));
  Connection* conn1 = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  ASSERT_TRUE(conn1 != nullptr);

  // A connection needs to be writable before it is selected for transmission.
  conn1->ReceivedPingResponse(kLowRtt, "id");
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.selected_connection(); }, Eq(conn1)),
      IsRtcOk());
  EXPECT_TRUE(CandidatePairMatchesNetworkRoute(conn1));

  // When a higher priority candidate comes in, the new connection is chosen
  // as the selected connection.
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "2.2.2.2", 2, 10));
  Connection* conn2 = WaitForConnectionTo(&ch, "2.2.2.2", 2);
  ASSERT_TRUE(conn2 != nullptr);
  conn2->ReceivedPingResponse(kLowRtt, "id");
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.selected_connection(); }, Eq(conn2)),
      IsRtcOk());
  EXPECT_TRUE(CandidatePairMatchesNetworkRoute(conn2));

  // Now nominate conn1 (low prio), it shall be choosen.
  const int before = conn1->num_pings_sent();
  NominateConnection(conn1);
  ASSERT_EQ(conn1, ch.selected_connection());
  EXPECT_TRUE(CandidatePairMatchesNetworkRoute(conn1));

  // And the additional ping should have been sent directly.
  EXPECT_EQ(conn1->num_pings_sent(), before + 1);
}

// Test the field trial send_ping_on_switch_ice_controlling
// that sends a ping directly when switching to a new connection
// on the ICE_CONTROLLING-side.
TEST_F(P2PTransportChannelPingTest, TestPingOnSwitch) {
  env_ = CreateTestEnvironment(
      {.field_trials =
           "WebRTC-IceFieldTrials/send_ping_on_switch_ice_controlling:true/",
       .time = &time_controller_});
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "receiving state change", 1, &pa);
  PrepareChannel(&ch);
  ch.SetIceConfig(ch.config());
  ch.SetIceRole(ICEROLE_CONTROLLING);
  ch.MaybeStartGathering();
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 1));
  Connection* conn1 = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  ASSERT_TRUE(conn1 != nullptr);

  // A connection needs to be writable before it is selected for transmission.
  conn1->ReceivedPingResponse(kLowRtt, "id");
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.selected_connection(); }, Eq(conn1)),
      IsRtcOk());
  EXPECT_TRUE(CandidatePairMatchesNetworkRoute(conn1));

  // When a higher priority candidate comes in, the new connection is chosen
  // as the selected connection.
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "2.2.2.2", 2, 10));
  Connection* conn2 = WaitForConnectionTo(&ch, "2.2.2.2", 2);
  ASSERT_TRUE(conn2 != nullptr);

  const int before = conn2->num_pings_sent();

  conn2->ReceivedPingResponse(kLowRtt, "id");
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.selected_connection(); }, Eq(conn2)),
      IsRtcOk());
  EXPECT_TRUE(CandidatePairMatchesNetworkRoute(conn2));

  // And the additional ping should have been sent directly.
  EXPECT_EQ(conn2->num_pings_sent(), before + 1);
}

// Test the field trial send_ping_on_switch_ice_controlling
// that sends a ping directly when selecteing a new connection
// on the ICE_CONTROLLING-side (i.e also initial selection).
TEST_F(P2PTransportChannelPingTest, TestPingOnSelected) {
  env_ = CreateTestEnvironment(
      {.field_trials =
           "WebRTC-IceFieldTrials/send_ping_on_selected_ice_controlling:true/",
       .time = &time_controller_});
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "receiving state change", 1, &pa);
  PrepareChannel(&ch);
  ch.SetIceConfig(ch.config());
  ch.SetIceRole(ICEROLE_CONTROLLING);
  ch.MaybeStartGathering();
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 1));
  Connection* conn1 = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  ASSERT_TRUE(conn1 != nullptr);

  const int before = conn1->num_pings_sent();

  // A connection needs to be writable before it is selected for transmission.
  conn1->ReceivedPingResponse(kLowRtt, "id");
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.selected_connection(); }, Eq(conn1)),
      IsRtcOk());
  EXPECT_TRUE(CandidatePairMatchesNetworkRoute(conn1));

  // And the additional ping should have been sent directly.
  EXPECT_EQ(conn1->num_pings_sent(), before + 1);
}

// The controlled side will select a connection as the "selected connection"
// based on requests from an unknown address before the controlling side
// nominates a connection, and will nominate a connection from an unknown
// address if the request contains the use_candidate attribute. Plus, it will
// also sends back a ping response and set the ICE pwd in the remote candidate
// appropriately.
TEST_F(P2PTransportChannelPingTest, TestSelectConnectionFromUnknownAddress) {
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "receiving state change", 1, &pa);
  PrepareChannel(&ch);
  ch.SetIceRole(ICEROLE_CONTROLLED);
  ch.MaybeStartGathering();
  // A minimal STUN message with prflx priority.
  IceMessage request(STUN_BINDING_REQUEST);
  request.AddAttribute(std::make_unique<StunByteStringAttribute>(
      STUN_ATTR_USERNAME, kIceUfrag[1]));
  uint32_t prflx_priority = ICE_TYPE_PREFERENCE_PRFLX << 24;
  request.AddAttribute(std::make_unique<StunUInt32Attribute>(STUN_ATTR_PRIORITY,
                                                             prflx_priority));
  TestUDPPort* port = static_cast<TestUDPPort*>(GetPort(&ch));
  port->NotifyUnknownAddress(port, SocketAddress("1.1.1.1", 1), PROTO_UDP,
                             &request, kIceUfrag[1], false);
  Connection* conn1 = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  ASSERT_TRUE(conn1 != nullptr);
  EXPECT_EQ(conn1->stats().sent_ping_responses, 1u);
  EXPECT_NE(conn1, ch.selected_connection());
  conn1->ReceivedPingResponse(kLowRtt, "id");
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.selected_connection(); }, Eq(conn1)),
      IsRtcOk());

  // Another connection is nominated via use_candidate.
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "2.2.2.2", 2, 1));
  Connection* conn2 = WaitForConnectionTo(&ch, "2.2.2.2", 2);
  ASSERT_TRUE(conn2 != nullptr);
  // Because it has a lower priority, the selected connection is still conn1.
  EXPECT_EQ(conn1, ch.selected_connection());
  // When it is nominated via use_candidate and writable, it is chosen as the
  // selected connection.
  conn2->ReceivedPingResponse(kLowRtt, "id");  // Become writable.
  NominateConnection(conn2);
  EXPECT_EQ(conn2, ch.selected_connection());

  // Another request with unknown address, it will not be set as the selected
  // connection because the selected connection was nominated by the controlling
  // side.
  port->NotifyUnknownAddress(port, SocketAddress("3.3.3.3", 3), PROTO_UDP,
                             &request, kIceUfrag[1], false);
  Connection* conn3 = WaitForConnectionTo(&ch, "3.3.3.3", 3);
  ASSERT_TRUE(conn3 != nullptr);
  EXPECT_EQ(conn3->stats().sent_ping_responses, 1u);
  conn3->ReceivedPingResponse(kLowRtt, "id");  // Become writable.
  EXPECT_EQ(conn2, ch.selected_connection());

  // However if the request contains use_candidate attribute, it will be
  // selected as the selected connection.
  request.AddAttribute(
      std::make_unique<StunByteStringAttribute>(STUN_ATTR_USE_CANDIDATE));
  port->NotifyUnknownAddress(port, SocketAddress("4.4.4.4", 4), PROTO_UDP,
                             &request, kIceUfrag[1], false);
  Connection* conn4 = WaitForConnectionTo(&ch, "4.4.4.4", 4);
  ASSERT_TRUE(conn4 != nullptr);
  EXPECT_EQ(conn4->stats().sent_ping_responses, 1u);
  // conn4 is not the selected connection yet because it is not writable.
  EXPECT_EQ(conn2, ch.selected_connection());
  conn4->ReceivedPingResponse(kLowRtt, "id");  // Become writable.
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.selected_connection(); }, Eq(conn4)),
      IsRtcOk());

  // Test that the request from an unknown address contains a ufrag from an old
  // generation.
  // port->set_sent_binding_response(false);
  ch.SetRemoteIceParameters(kIceParams[2]);
  ch.SetRemoteIceParameters(kIceParams[3]);
  port->NotifyUnknownAddress(port, SocketAddress("5.5.5.5", 5), PROTO_UDP,
                             &request, kIceUfrag[2], false);
  Connection* conn5 = WaitForConnectionTo(&ch, "5.5.5.5", 5);
  ASSERT_TRUE(conn5 != nullptr);
  EXPECT_EQ(conn5->stats().sent_ping_responses, 1u);
  EXPECT_EQ(kIcePwd[2], conn5->remote_candidate().password());
}

// The controlled side will select a connection as the "selected connection"
// based on media received until the controlling side nominates a connection,
// at which point the controlled side will select that connection as
// the "selected connection".
TEST_F(P2PTransportChannelPingTest, TestSelectConnectionBasedOnMediaReceived) {
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "receiving state change", 1, &pa);
  PrepareChannel(&ch);
  ch.SetIceRole(ICEROLE_CONTROLLED);
  ch.MaybeStartGathering();
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 10));
  Connection* conn1 = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  ASSERT_TRUE(conn1 != nullptr);
  conn1->ReceivedPingResponse(kLowRtt, "id");
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.selected_connection(); }, Eq(conn1)),
      IsRtcOk());

  // If a data packet is received on conn2, the selected connection should
  // switch to conn2 because the controlled side must mirror the media path
  // chosen by the controlling side.
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "2.2.2.2", 2, 1));
  Connection* conn2 = WaitForConnectionTo(&ch, "2.2.2.2", 2);
  ASSERT_TRUE(conn2 != nullptr);
  conn2->ReceivedPingResponse(kLowRtt, "id");  // Become writable and receiving.
  conn2->OnReadPacket(ReceivedIpPacket::CreateFromLegacy(
      "ABC", 3, env_.clock().TimeInMicroseconds()));
  EXPECT_EQ(conn2, ch.selected_connection());
  conn2->ReceivedPingResponse(kLowRtt, "id");  // Become writable.

  // Now another STUN message with an unknown address and use_candidate will
  // nominate the selected connection.
  IceMessage request(STUN_BINDING_REQUEST);
  request.AddAttribute(std::make_unique<StunByteStringAttribute>(
      STUN_ATTR_USERNAME, kIceUfrag[1]));
  uint32_t prflx_priority = ICE_TYPE_PREFERENCE_PRFLX << 24;
  request.AddAttribute(std::make_unique<StunUInt32Attribute>(STUN_ATTR_PRIORITY,
                                                             prflx_priority));
  request.AddAttribute(
      std::make_unique<StunByteStringAttribute>(STUN_ATTR_USE_CANDIDATE));
  Port* port = GetPort(&ch);
  port->NotifyUnknownAddress(port, SocketAddress("3.3.3.3", 3), PROTO_UDP,
                             &request, kIceUfrag[1], false);
  Connection* conn3 = WaitForConnectionTo(&ch, "3.3.3.3", 3);
  ASSERT_TRUE(conn3 != nullptr);
  EXPECT_NE(conn3, ch.selected_connection());  // Not writable yet.
  conn3->ReceivedPingResponse(kLowRtt, "id");  // Become writable.
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.selected_connection(); }, Eq(conn3)),
      IsRtcOk());

  // Now another data packet will not switch the selected connection because the
  // selected connection was nominated by the controlling side.
  conn2->ReceivedPing();
  conn2->ReceivedPingResponse(kLowRtt, "id");
  conn2->OnReadPacket(ReceivedIpPacket::CreateFromLegacy(
      "XYZ", 3, env_.clock().TimeInMicroseconds()));
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.selected_connection(); }, Eq(conn3)),
      IsRtcOk());
}

TEST_F(P2PTransportChannelPingTest,
       TestControlledAgentDataReceivingTakesHigherPrecedenceThanPriority) {
  time_controller_.AdvanceTime(TimeDelta::Seconds(1));
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "SwitchSelectedConnection", 1, &pa);
  PrepareChannel(&ch);
  ch.SetIceRole(ICEROLE_CONTROLLED);
  ch.MaybeStartGathering();
  // The connections have decreasing priority.
  Connection* conn1 =
      CreateConnectionWithCandidate(&ch, "1.1.1.1", 1, 10, true);
  ASSERT_TRUE(conn1 != nullptr);
  Connection* conn2 = CreateConnectionWithCandidate(&ch, "2.2.2.2", 2, 9, true);
  ASSERT_TRUE(conn2 != nullptr);

  // Initially, connections are selected based on priority.
  EXPECT_EQ(1, reset_selected_candidate_pair_switches());
  EXPECT_TRUE(CandidatePairMatchesNetworkRoute(conn1));

  // conn2 receives data; it becomes selected.
  // Advance the clock by 1ms so that the last data receiving timestamp of
  // conn2 is larger.
  time_controller_.AdvanceTime(TimeDelta::Millis(1));

  conn2->OnReadPacket(ReceivedIpPacket::CreateFromLegacy(
      "XYZ", 3, env_.clock().TimeInMicroseconds()));
  EXPECT_EQ(1, reset_selected_candidate_pair_switches());
  EXPECT_TRUE(CandidatePairMatchesNetworkRoute(conn2));

  // conn1 also receives data; it becomes selected due to priority again.
  conn1->OnReadPacket(ReceivedIpPacket::CreateFromLegacy(
      "ABC", 3, env_.clock().TimeInMicroseconds()));
  EXPECT_EQ(1, reset_selected_candidate_pair_switches());
  EXPECT_TRUE(CandidatePairMatchesNetworkRoute(conn2));

  // conn2 received data more recently; it is selected now because it
  // received data more recently.
  time_controller_.AdvanceTime(TimeDelta::Millis(1));
  // Need to become writable again because it was pruned.
  conn2->ReceivedPingResponse(kLowRtt, "id");
  conn2->OnReadPacket(ReceivedIpPacket::CreateFromLegacy(
      "ABC", 3, env_.clock().TimeInMicroseconds()));
  EXPECT_EQ(1, reset_selected_candidate_pair_switches());
  EXPECT_TRUE(CandidatePairMatchesNetworkRoute(conn2));

  // Make sure sorting won't reselect candidate pair.
  time_controller_.AdvanceTime(TimeDelta::Millis(10));
  EXPECT_EQ(0, reset_selected_candidate_pair_switches());
}

TEST_F(P2PTransportChannelPingTest,
       TestControlledAgentNominationTakesHigherPrecedenceThanDataReceiving) {
  time_controller_.AdvanceTime(TimeDelta::Seconds(1));

  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "SwitchSelectedConnection", 1, &pa);
  PrepareChannel(&ch);
  ch.SetIceRole(ICEROLE_CONTROLLED);
  ch.MaybeStartGathering();
  // The connections have decreasing priority.
  Connection* conn1 =
      CreateConnectionWithCandidate(&ch, "1.1.1.1", 1, 10, true);
  ASSERT_TRUE(conn1 != nullptr);
  Connection* conn2 = CreateConnectionWithCandidate(&ch, "2.2.2.2", 2, 9, true);
  ASSERT_TRUE(conn2 != nullptr);

  // conn1 received data; it is the selected connection.
  // Advance the clock to have a non-zero last-data-receiving time.
  time_controller_.AdvanceTime(TimeDelta::Millis(1));

  conn1->OnReadPacket(ReceivedIpPacket::CreateFromLegacy(
      "XYZ", 3, env_.clock().TimeInMicroseconds()));
  EXPECT_EQ(1, reset_selected_candidate_pair_switches());
  EXPECT_TRUE(CandidatePairMatchesNetworkRoute(conn1));

  // conn2 is nominated; it becomes the selected connection.
  NominateConnection(conn2);
  EXPECT_EQ(1, reset_selected_candidate_pair_switches());
  EXPECT_TRUE(CandidatePairMatchesNetworkRoute(conn2));

  // conn1 is selected because it has higher priority and also nominated.
  NominateConnection(conn1);
  EXPECT_EQ(1, reset_selected_candidate_pair_switches());
  EXPECT_TRUE(CandidatePairMatchesNetworkRoute(conn2));

  // Make sure sorting won't reselect candidate pair.
  time_controller_.AdvanceTime(TimeDelta::Millis(10));
  EXPECT_EQ(0, reset_selected_candidate_pair_switches());
}

TEST_F(P2PTransportChannelPingTest,
       TestControlledAgentSelectsConnectionWithHigherNomination) {
  time_controller_.AdvanceTime(TimeDelta::Seconds(1));

  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "test", 1, &pa);
  PrepareChannel(&ch);
  ch.SetIceRole(ICEROLE_CONTROLLED);
  ch.MaybeStartGathering();
  // The connections have decreasing priority.
  Connection* conn1 =
      CreateConnectionWithCandidate(&ch, "1.1.1.1", 1, 10, true);
  ASSERT_TRUE(conn1 != nullptr);
  Connection* conn2 = CreateConnectionWithCandidate(&ch, "2.2.2.2", 2, 9, true);
  ASSERT_TRUE(conn2 != nullptr);

  // conn1 is the selected connection because it has a higher priority,
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.selected_connection(); }, Eq(conn1)),
      IsRtcOk());
  EXPECT_TRUE(CandidatePairMatchesNetworkRoute(conn1));
  reset_selected_candidate_pair_switches();

  // conn2 is nominated; it becomes selected.
  NominateConnection(conn2);
  EXPECT_EQ(1, reset_selected_candidate_pair_switches());
  EXPECT_EQ(conn2, ch.selected_connection());
  EXPECT_TRUE(CandidatePairMatchesNetworkRoute(conn2));

  // conn1 is selected because of its priority.
  NominateConnection(conn1);
  EXPECT_EQ(1, reset_selected_candidate_pair_switches());
  EXPECT_EQ(conn1, ch.selected_connection());
  EXPECT_TRUE(CandidatePairMatchesNetworkRoute(conn1));

  // conn2 gets higher remote nomination; it is selected again.
  NominateConnection(conn2, 2U);
  EXPECT_EQ(1, reset_selected_candidate_pair_switches());
  EXPECT_EQ(conn2, ch.selected_connection());
  EXPECT_TRUE(CandidatePairMatchesNetworkRoute(conn2));

  // Make sure sorting won't reselect candidate pair.
  time_controller_.AdvanceTime(TimeDelta::Millis(100));
  EXPECT_EQ(0, reset_selected_candidate_pair_switches());
}

TEST_F(P2PTransportChannelPingTest, TestEstimatedDisconnectedTime) {
  time_controller_.AdvanceTime(TimeDelta::Seconds(1));

  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "test", 1, &pa);
  PrepareChannel(&ch);
  ch.SetIceRole(ICEROLE_CONTROLLED);
  ch.MaybeStartGathering();
  // The connections have decreasing priority.
  Connection* conn1 =
      CreateConnectionWithCandidate(&ch, "1.1.1.1", /* port= */ 1,
                                    /* priority= */ 10, /* writable= */ true);
  ASSERT_TRUE(conn1 != nullptr);
  Connection* conn2 =
      CreateConnectionWithCandidate(&ch, "2.2.2.2", /* port= */ 2,
                                    /* priority= */ 9, /* writable= */ true);
  ASSERT_TRUE(conn2 != nullptr);

  // conn1 is the selected connection because it has a higher priority,
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.selected_connection(); }, Eq(conn1)),
      IsRtcOk());
  EXPECT_TRUE(CandidatePairMatchesNetworkRoute(conn1));
  // No estimateded disconnect time at first connect <=> value is 0.
  EXPECT_EQ(LastEstimatedDisconnectedTimeMs(), 0);

  // Use nomination to force switching of selected connection.
  int nomination = 1;

  {
    time_controller_.AdvanceTime(TimeDelta::Seconds(1));
    // This will not parse as STUN, and is considered data
    conn1->OnReadPacket(ReceivedIpPacket::CreateFromLegacy(
        "XYZ", 3, env_.clock().TimeInMicroseconds()));
    time_controller_.AdvanceTime(TimeDelta::Seconds(2));

    // To be eligible for selection upon nomination, conn2 must be receiving.
    // The real time controller fires timers that cause conn2 to lose receiving
    // status during the 2s wait.
    conn2->ReceivedPingResponse(kLowRtt, "id");

    // conn2 is nominated; it becomes selected.
    NominateConnection(conn2, nomination++);
    EXPECT_EQ(conn2, ch.selected_connection());
    // We got data 2s ago...guess that we lost 2s of connectivity.
    EXPECT_EQ(LastEstimatedDisconnectedTimeMs(), 2000);
  }

  {
    time_controller_.AdvanceTime(TimeDelta::Seconds(1));
    conn2->OnReadPacket(ReceivedIpPacket::CreateFromLegacy(
        "XYZ", 3, env_.clock().TimeInMicroseconds()));
    time_controller_.AdvanceTime(TimeDelta::Seconds(2));
    ReceivePingOnConnection(conn2, kIceUfrag[1], 1, nomination++);

    // To be eligible for selection upon nomination, conn1 must be receiving.
    conn1->ReceivedPingResponse(kLowRtt, "id");

    time_controller_.AdvanceTime(TimeDelta::Millis(500));

    ReceivePingOnConnection(conn1, kIceUfrag[1], 1, nomination++);
    EXPECT_EQ(conn1, ch.selected_connection());
    // We got ping 500ms ago...guess that we lost 500ms of connectivity.
    EXPECT_EQ(LastEstimatedDisconnectedTimeMs(), 500);
  }
}

TEST_F(P2PTransportChannelPingTest,
       TestControlledAgentIgnoresSmallerNomination) {
  time_controller_.AdvanceTime(TimeDelta::Seconds(1));

  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "test", 1, &pa);
  PrepareChannel(&ch);
  ch.SetIceRole(ICEROLE_CONTROLLED);
  ch.MaybeStartGathering();
  Connection* conn =
      CreateConnectionWithCandidate(&ch, "1.1.1.1", 1, 10, false);
  ReceivePingOnConnection(conn, kIceUfrag[1], 1, 2U);
  EXPECT_EQ(2U, conn->remote_nomination());
  // Smaller nomination is ignored.
  ReceivePingOnConnection(conn, kIceUfrag[1], 1, 1U);
  EXPECT_EQ(2U, conn->remote_nomination());
}

TEST_F(P2PTransportChannelPingTest,
       TestControlledAgentWriteStateTakesHigherPrecedenceThanNomination) {
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "SwitchSelectedConnection", 1, &pa);
  PrepareChannel(&ch);
  ch.SetIceRole(ICEROLE_CONTROLLED);
  ch.MaybeStartGathering();
  // The connections have decreasing priority.
  Connection* conn1 =
      CreateConnectionWithCandidate(&ch, "1.1.1.1", 1, 10, false);
  ASSERT_TRUE(conn1 != nullptr);
  Connection* conn2 =
      CreateConnectionWithCandidate(&ch, "2.2.2.2", 2, 9, false);
  ASSERT_TRUE(conn2 != nullptr);

  NominateConnection(conn1);
  // There is no selected connection because no connection is writable.
  EXPECT_EQ(0, reset_selected_candidate_pair_switches());

  // conn2 becomes writable; it is selected even though it is not nominated.
  conn2->ReceivedPingResponse(kLowRtt, "id");
  EXPECT_THAT(
      DefaultWait().Until(
          [&] { return reset_selected_candidate_pair_switches(); }, Eq(1)),
      IsRtcOk());
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.selected_connection(); }, Eq(conn2)),
      IsRtcOk());
  EXPECT_TRUE(CandidatePairMatchesNetworkRoute(conn2));

  // If conn1 is also writable, it will become selected.
  conn1->ReceivedPingResponse(kLowRtt, "id");
  EXPECT_THAT(
      DefaultWait().Until(
          [&] { return reset_selected_candidate_pair_switches(); }, Eq(1)),
      IsRtcOk());
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.selected_connection(); }, Eq(conn1)),
      IsRtcOk());
  EXPECT_TRUE(CandidatePairMatchesNetworkRoute(conn1));

  // Make sure sorting won't reselect candidate pair.
  time_controller_.AdvanceTime(TimeDelta::Millis(10));
  EXPECT_EQ(0, reset_selected_candidate_pair_switches());
}

// Test that if a new remote candidate has the same address and port with
// an old one, it will be used to create a new connection.
TEST_F(P2PTransportChannelPingTest, TestAddRemoteCandidateWithAddressReuse) {
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "candidate reuse", 1, &pa);
  PrepareChannel(&ch);
  ch.MaybeStartGathering();
  const std::string host_address = "1.1.1.1";
  const int port_num = 1;

  // kIceUfrag[1] is the current generation ufrag.
  Candidate candidate = CreateUdpCandidate(
      IceCandidateType::kHost, host_address, port_num, 1, kIceUfrag[1]);
  ch.AddRemoteCandidate(candidate);
  Connection* conn1 = WaitForConnectionTo(&ch, host_address, port_num);
  ASSERT_TRUE(conn1 != nullptr);
  EXPECT_EQ(0u, conn1->remote_candidate().generation());

  // Simply adding the same candidate again won't create a new connection.
  ch.AddRemoteCandidate(candidate);
  Connection* conn2 = GetConnectionTo(&ch, host_address, port_num);
  EXPECT_EQ(conn1, conn2);

  // Update the ufrag of the candidate and add it again.
  candidate.set_username(kIceUfrag[2]);
  ch.AddRemoteCandidate(candidate);
  conn2 = GetConnectionTo(&ch, host_address, port_num);
  EXPECT_NE(conn1, conn2);
  EXPECT_EQ(kIceUfrag[2], conn2->remote_candidate().username());
  EXPECT_EQ(1u, conn2->remote_candidate().generation());

  // Verify that a ping with the new ufrag can be received on the new
  // connection.
  EXPECT_EQ(conn2->LastPingReceived(), Timestamp::Zero());
  ReceivePingOnConnection(conn2, kIceUfrag[2], 1 /* priority */);
  EXPECT_GT(conn2->LastPingReceived(), Timestamp::Zero());
}

// A remote candidate that reuses the address of the current selected
// connection but belongs to a newer generation (ICE restart) must not
// replace it. The selected connection is kept so that it can keep sending
// data during the restart; the newer-generation candidate only takes over
// once a connection for it is created elsewhere (e.g. on a new generation
// port) and becomes writable.
TEST_F(P2PTransportChannelPingTest,
       TestSameAddressCandidateDoesNotReplaceSelectedConnection) {
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "same address reuse selected", 1, &pa);
  PrepareChannel(&ch);
  ch.SetIceRole(ICEROLE_CONTROLLED);
  ch.MaybeStartGathering();
  const std::string host_address = "1.1.1.1";
  const int port_num = 1;

  // kIceUfrag[1] is the current generation ufrag.
  Candidate candidate = CreateUdpCandidate(
      IceCandidateType::kHost, host_address, port_num, 1, kIceUfrag[1]);
  ch.AddRemoteCandidate(candidate);
  Connection* conn1 = WaitForConnectionTo(&ch, host_address, port_num);
  ASSERT_THAT(conn1, NotNull());
  EXPECT_EQ(conn1->remote_candidate().generation(), 0u);

  // Make conn1 writable so that it becomes the selected connection.
  conn1->ReceivedPingResponse(kLowRtt, "id");
  EXPECT_THAT(
      ShortWait().Until([&] { return ch.selected_connection(); }, Eq(conn1)),
      IsRtcOk());

  // A candidate with a newer generation ufrag reuses the same address.
  candidate.set_username(kIceUfrag[2]);
  ch.AddRemoteCandidate(candidate);

  // The selected connection must survive: no replacement and no switch.
  ASSERT_EQ(conn1, GetConnectionTo(&ch, host_address, port_num));
  EXPECT_TRUE(conn1->selected());
  EXPECT_TRUE(conn1->writable());
  EXPECT_EQ(conn1, ch.selected_connection());
}

// When the current selected connection is strong, lower-priority connections
// will be pruned. Otherwise, lower-priority connections are kept.
TEST_F(P2PTransportChannelPingTest, TestDontPruneWhenWeak) {
  time_controller_.AdvanceTime(TimeDelta::Seconds(1));
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "test channel", 1, &pa);
  PrepareChannel(&ch);
  ch.SetIceRole(ICEROLE_CONTROLLED);
  ch.MaybeStartGathering();
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 1));
  Connection* conn1 = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  ASSERT_TRUE(conn1 != nullptr);
  EXPECT_EQ(nullptr, ch.selected_connection());
  conn1->ReceivedPingResponse(kLowRtt, "id");  // Becomes writable and receiving

  // When a higher-priority, nominated candidate comes in, the connections with
  // lower-priority are pruned.
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "2.2.2.2", 2, 10));
  Connection* conn2 = WaitForConnectionTo(&ch, "2.2.2.2", 2);
  ASSERT_TRUE(conn2 != nullptr);
  conn2->ReceivedPingResponse(kLowRtt, "id");  // Becomes writable and receiving
  NominateConnection(conn2);
  EXPECT_TRUE(DefaultWait().Until([&] { return conn1->pruned(); }));

  ch.SetIceConfig(CreateIceConfig(TimeDelta::Millis(500), GATHER_ONCE));
  // Wait until conn2 becomes not receiving.
  EXPECT_TRUE(MediumWait().Until([&] { return !conn2->receiving(); }));

  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "3.3.3.3", 3, 1));
  Connection* conn3 = WaitForConnectionTo(&ch, "3.3.3.3", 3);
  ASSERT_TRUE(conn3 != nullptr);
  // The selected connection should still be conn2. Even through conn3 has lower
  // priority and is not receiving/writable, it is not pruned because the
  // selected connection is not receiving.
  EXPECT_FALSE(MediumWait().Until([&] { return conn3->pruned(); }));
  EXPECT_FALSE(conn3->pruned());
}

TEST_F(P2PTransportChannelPingTest, TestDontPruneHighPriorityConnections) {
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "test channel", 1, &pa);
  PrepareChannel(&ch);
  ch.SetIceRole(ICEROLE_CONTROLLED);
  ch.MaybeStartGathering();
  Connection* conn1 =
      CreateConnectionWithCandidate(&ch, "1.1.1.1", 1, 100, true);
  ASSERT_TRUE(conn1 != nullptr);
  Connection* conn2 =
      CreateConnectionWithCandidate(&ch, "2.2.2.2", 2, 200, false);
  ASSERT_TRUE(conn2 != nullptr);
  // Even if conn1 is writable, nominated, receiving data, it should not prune
  // conn2.
  NominateConnection(conn1);
  time_controller_.AdvanceTime(TimeDelta::Millis(1));
  conn1->OnReadPacket(ReceivedIpPacket::CreateFromLegacy(
      "XYZ", 3, env_.clock().TimeInMicroseconds()));
  (void)ShortWait().Until([&] { return conn2->pruned(); });
  EXPECT_FALSE(conn2->pruned());
}

// Test that GetState returns the state correctly.
TEST_F(P2PTransportChannelPingTest, TestGetState) {
  time_controller_.AdvanceTime(TimeDelta::Seconds(1));
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "test channel", 1, &pa);
  EXPECT_EQ(IceTransportState::kNew, ch.GetIceTransportState());
  PrepareChannel(&ch);
  ch.MaybeStartGathering();
  // After gathering we are still in the kNew state because we aren't checking
  // any connections yet.
  EXPECT_EQ(IceTransportState::kNew, ch.GetIceTransportState());
  EXPECT_EQ(IceTransportStateInternal::STATE_INIT, ch.GetState());
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 100));
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "2.2.2.2", 2, 1));
  // Checking candidates that have been added with gathered candidates.
  ASSERT_GT(ch.connections().size(), 0u);
  EXPECT_EQ(IceTransportState::kChecking, ch.GetIceTransportState());
  Connection* conn1 = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  Connection* conn2 = WaitForConnectionTo(&ch, "2.2.2.2", 2);
  ASSERT_TRUE(conn1 != nullptr);
  ASSERT_TRUE(conn2 != nullptr);
  // Now there are two connections, so the transport channel is connecting.
  EXPECT_EQ(IceTransportStateInternal::STATE_CONNECTING, ch.GetState());
  // No connections are writable yet, so we should still be in the kChecking
  // state.
  EXPECT_EQ(IceTransportState::kChecking, ch.GetIceTransportState());
  // `conn1` becomes writable and receiving; it then should prune `conn2`.
  conn1->ReceivedPingResponse(kLowRtt, "id");
  EXPECT_TRUE(DefaultWait().Until([&] { return conn2->pruned(); }));
  EXPECT_EQ(IceTransportStateInternal::STATE_COMPLETED, ch.GetState());
  EXPECT_EQ(IceTransportState::kConnected, ch.GetIceTransportState());
  conn1->Prune();  // All connections are pruned.
  // Need to wait until the channel state is updated.
  EXPECT_THAT(ShortWait().Until([&] { return ch.GetState(); },
                                Eq(IceTransportStateInternal::STATE_FAILED)),
              IsRtcOk());
  EXPECT_EQ(IceTransportState::kFailed, ch.GetIceTransportState());
}

// Test that when a low-priority connection is pruned, it is not deleted
// right away, and it can become active and be pruned again.
TEST_F(P2PTransportChannelPingTest, TestConnectionPrunedAgain) {
  time_controller_.AdvanceTime(TimeDelta::Seconds(1));

  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "test channel", 1, &pa);
  PrepareChannel(&ch);
  IceConfig config = CreateIceConfig(TimeDelta::Seconds(1), GATHER_ONCE);
  config.receiving_switching_delay = TimeDelta::Millis(800);
  ch.SetIceConfig(config);
  ch.MaybeStartGathering();
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 100));
  Connection* conn1 = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  ASSERT_TRUE(conn1 != nullptr);
  EXPECT_EQ(nullptr, ch.selected_connection());
  conn1->ReceivedPingResponse(kLowRtt, "id");  // Becomes writable and receiving
  EXPECT_THAT(
      ShortWait().Until([&] { return ch.selected_connection(); }, Eq(conn1)),
      IsRtcOk());

  // Add a low-priority connection `conn2`, which will be pruned, but it will
  // not be deleted right away. Once the current selected connection becomes not
  // receiving, `conn2` will start to ping and upon receiving the ping response,
  // it will become the selected connection.
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "2.2.2.2", 2, 1));
  Connection* conn2 = WaitForConnectionTo(&ch, "2.2.2.2", 2);
  ASSERT_TRUE(conn2 != nullptr);
  EXPECT_TRUE(DefaultWait().Until([&] { return !conn2->active(); }));
  // `conn2` should not send a ping yet.
  EXPECT_EQ(IceCandidatePairState::WAITING, conn2->state());
  EXPECT_EQ(IceTransportStateInternal::STATE_COMPLETED, ch.GetState());
  // Wait for `conn1` becoming not receiving.
  EXPECT_TRUE(DefaultWait().Until([&] { return !conn1->receiving(); }));
  // Make sure conn2 is not deleted.
  conn2 = WaitForConnectionTo(&ch, "2.2.2.2", 2);
  ASSERT_TRUE(conn2 != nullptr);
  EXPECT_THAT(MediumWait().Until([&] { return conn2->state(); },
                                 Eq(IceCandidatePairState::IN_PROGRESS)),
              IsRtcOk());
  conn2->ReceivedPingResponse(kLowRtt, "id");
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.selected_connection(); }, Eq(conn2)),
      IsRtcOk());
  EXPECT_EQ(IceTransportStateInternal::STATE_CONNECTING, ch.GetState());

  // When `conn1` comes back again, `conn2` will be pruned again.
  conn1->ReceivedPingResponse(kLowRtt, "id");
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.selected_connection(); }, Eq(conn1)),
      IsRtcOk());
  EXPECT_TRUE(DefaultWait().Until([&] { return !conn2->active(); }));
  EXPECT_EQ(IceTransportStateInternal::STATE_COMPLETED, ch.GetState());
}

// Test that if all connections in a channel has timed out on writing, they
// will all be deleted. We use Prune to simulate write_time_out.
TEST_F(P2PTransportChannelPingTest, TestDeleteConnectionsIfAllWriteTimedout) {
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "test channel", 1, &pa);
  PrepareChannel(&ch);
  ch.MaybeStartGathering();
  // Have one connection only but later becomes write-time-out.
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 100));
  Connection* conn1 = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  ASSERT_TRUE(conn1 != nullptr);
  conn1->ReceivedPing();  // Becomes receiving
  conn1->Prune();
  EXPECT_TRUE(DefaultWait().Until([&] { return ch.connections().empty(); }));

  // Have two connections but both become write-time-out later.
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "2.2.2.2", 2, 1));
  Connection* conn2 = WaitForConnectionTo(&ch, "2.2.2.2", 2);
  ASSERT_TRUE(conn2 != nullptr);
  conn2->ReceivedPing();  // Becomes receiving
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "3.3.3.3", 3, 2));
  Connection* conn3 = WaitForConnectionTo(&ch, "3.3.3.3", 3);
  ASSERT_TRUE(conn3 != nullptr);
  conn3->ReceivedPing();  // Becomes receiving
  // Now prune both conn2 and conn3; they will be deleted soon.
  conn2->Prune();
  conn3->Prune();
  EXPECT_TRUE(ShortWait().Until([&] { return ch.connections().empty(); }));
}

// Tests that after a port allocator session is started, it will be stopped
// when a new connection becomes writable and receiving. Also tests that if a
// connection belonging to an old session becomes writable, it won't stop
// the current port allocator session.
TEST_F(P2PTransportChannelPingTest, TestStopPortAllocatorSessions) {
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "test channel", 1, &pa);
  PrepareChannel(&ch);
  ch.SetIceConfig(CreateIceConfig(TimeDelta::Seconds(2), GATHER_ONCE));
  ch.MaybeStartGathering();
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 100));
  Connection* conn1 = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  ASSERT_TRUE(conn1 != nullptr);
  conn1->ReceivedPingResponse(kLowRtt, "id");  // Becomes writable and receiving
  EXPECT_TRUE(!ch.allocator_session()->IsGettingPorts());

  // Start a new session. Even though conn1, which belongs to an older
  // session, becomes unwritable and writable again, it should not stop the
  // current session.
  ch.SetIceParameters(kIceParams[1]);
  ch.MaybeStartGathering();
  conn1->Prune();
  conn1->ReceivedPingResponse(kLowRtt, "id");
  EXPECT_TRUE(ch.allocator_session()->IsGettingPorts());

  // But if a new connection created from the new session becomes writable,
  // it will stop the current session.
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "2.2.2.2", 2, 100));
  Connection* conn2 = WaitForConnectionTo(&ch, "2.2.2.2", 2);
  ASSERT_TRUE(conn2 != nullptr);
  conn2->ReceivedPingResponse(kLowRtt, "id");  // Becomes writable and receiving
  EXPECT_TRUE(!ch.allocator_session()->IsGettingPorts());
}

TEST_F(P2PTransportChannelPingTest,
       GatheringDoneOfOldGenerationIgnoredAfterIceRestart) {
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "test channel", ICE_CANDIDATE_COMPONENT_DEFAULT,
                         &pa);
  PrepareChannel(&ch);
  std::vector<IceGatheringState> states;
  ch.AddGatheringStateCallback(this, [&](IceTransportInternal* transport) {
    states.push_back(transport->gathering_state());
  });
  ch.MaybeStartGathering();
  PortAllocatorSession* old_session = ch.allocator_session();

  ch.SetIceParameters(kIceParams[1]);
  ch.MaybeStartGathering();
  old_session->NotifyCandidatesAllocationDone(old_session);

  EXPECT_THAT(states,
              ElementsAre(kIceGatheringGathering, kIceGatheringComplete,
                          kIceGatheringGathering, kIceGatheringComplete));
  ch.RemoveGatheringStateCallback(this);
}

// Test that the ICE role is updated even on ports that has been removed.
// These ports may still have connections that need a correct role, in case that
// the connections on it may still receive stun pings.
TEST_F(P2PTransportChannelPingTest, TestIceRoleUpdatedOnRemovedPort) {
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "test channel", ICE_CANDIDATE_COMPONENT_DEFAULT,
                         &pa);
  // Starts with ICEROLE_CONTROLLING.
  PrepareChannel(&ch);
  IceConfig config = CreateIceConfig(TimeDelta::Seconds(1), GATHER_CONTINUALLY);
  ch.SetIceConfig(config);
  ch.MaybeStartGathering();
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 1));

  Connection* conn = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  ASSERT_TRUE(conn != nullptr);

  // Make a fake signal to remove the ports in the p2ptransportchannel. then
  // change the ICE role and expect it to be updated.
  std::vector<PortInterface*> ports(1, conn->PortForTest());
  ch.allocator_session()->NotifyPortsPruned(ch.allocator_session(), ports);
  ch.SetIceRole(ICEROLE_CONTROLLED);
  EXPECT_EQ(ICEROLE_CONTROLLED, conn->PortForTest()->GetIceRole());
}

// Test that the ICE role is updated even on ports with inactive networks.
// These ports may still have connections that need a correct role, for the
// pings sent by those connections until they're replaced by newer-generation
// connections.
TEST_F(P2PTransportChannelPingTest, TestIceRoleUpdatedOnPortAfterIceRestart) {
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "test channel", ICE_CANDIDATE_COMPONENT_DEFAULT,
                         &pa);
  // Starts with ICEROLE_CONTROLLING.
  PrepareChannel(&ch);
  ch.MaybeStartGathering();
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 1));

  Connection* conn = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  ASSERT_TRUE(conn != nullptr);

  // Do an ICE restart, change the role, and expect the old port to have its
  // role updated.
  ch.SetIceParameters(kIceParams[1]);
  ch.MaybeStartGathering();
  ch.SetIceRole(ICEROLE_CONTROLLED);
  EXPECT_EQ(ICEROLE_CONTROLLED, conn->PortForTest()->GetIceRole());
}

// Test that after some amount of time without receiving data, the connection
// will be destroyed. The port will only be destroyed after it is marked as
// "pruned."
TEST_F(P2PTransportChannelPingTest, TestPortDestroyedAfterTimeoutAndPruned) {
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "test channel", ICE_CANDIDATE_COMPONENT_DEFAULT,
                         &pa);
  PrepareChannel(&ch);
  ch.SetIceRole(ICEROLE_CONTROLLED);
  ch.MaybeStartGathering();
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 1));

  Connection* conn = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  ASSERT_TRUE(conn != nullptr);

  // Simulate 2 minutes going by. This should be enough time for the port to
  // time out.
  for (int second = 0; second < 120; ++second) {
    time_controller_.AdvanceTime(TimeDelta::Seconds(1));
  }
  EXPECT_EQ(nullptr, GetConnectionTo(&ch, "1.1.1.1", 1));
  // Port will not be removed because it is not pruned yet.
  PortInterface* port = GetPort(&ch);
  ASSERT_NE(nullptr, port);

  // If the session prunes all ports, the port will be destroyed.
  ch.allocator_session()->PruneAllPorts();
  EXPECT_THAT(DefaultWait().Until([&] { return GetPort(&ch); }, Eq(nullptr)),
              IsRtcOk());
  EXPECT_THAT(
      DefaultWait().Until([&] { return GetPrunedPort(&ch); }, Eq(nullptr)),
      IsRtcOk());
}

TEST_F(P2PTransportChannelPingTest, TestMaxOutstandingPingsFieldTrial) {
  env_ = CreateTestEnvironment(
      {.field_trials = "WebRTC-IceFieldTrials/max_outstanding_pings:3/",
       .time = &time_controller_});
  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "max", 1, &pa);
  ch.SetIceConfig(ch.config());
  PrepareChannel(&ch);
  ch.MaybeStartGathering();
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 1));
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "2.2.2.2", 2, 2));

  Connection* conn1 = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  Connection* conn2 = WaitForConnectionTo(&ch, "2.2.2.2", 2);
  ASSERT_TRUE(conn1 != nullptr);
  ASSERT_TRUE(conn2 != nullptr);

  EXPECT_TRUE(DefaultWait().Until([&] {
    return conn1->num_pings_sent() == 3 && conn2->num_pings_sent() == 3;
  }));

  // Check that these connections don't send any more pings.
  EXPECT_EQ(nullptr, ch.FindNextPingableConnection());
}

class P2PTransportChannelMostLikelyToWorkFirstTest
    : public P2PTransportChannelPingTest {
 public:
  P2PTransportChannelMostLikelyToWorkFirstTest() {
    network_manager_.AddInterface(kPublicAddrs[0]);
  }

  BasicPortAllocator& CreatePortAllocator(const Environment& env_) {
    turn_server_.emplace(env_, Thread::Current(), ss(), kTurnUdpIntAddr,
                         kTurnUdpExtAddr);
    port_allocator_ = CreateBasicPortAllocator(
        env_, &network_manager_, packet_socket_factory(), ServerAddresses(),
        kTurnUdpIntAddr, SocketAddress());
    port_allocator_->set_flags(port_allocator_->flags() |
                               PORTALLOCATOR_DISABLE_STUN |
                               PORTALLOCATOR_DISABLE_TCP);
    port_allocator_->set_step_delay(kMinimumStepDelay);
    return *port_allocator_;
  }

  P2PTransportChannel& StartTransportChannel(
      const Environment& env_,
      bool prioritize_most_likely_to_work,
      TimeDelta stable_writable_connection_ping_interval) {
    channel_ = std::make_unique<P2PTransportChannel>(env_, "checks", 1,
                                                     port_allocator_.get());
    IceConfig config = channel_->config();
    config.prioritize_most_likely_candidate_pairs =
        prioritize_most_likely_to_work;
    config.stable_writable_connection_ping_interval =
        stable_writable_connection_ping_interval;
    channel_->SetIceConfig(config);
    PrepareChannel(channel_.get());
    channel_->MaybeStartGathering();
    return *channel_;
  }

  TestTurnServer& turn_server() {
    EXPECT_TRUE(turn_server_.has_value());
    return *turn_server_;
  }

  // This verifies the next pingable connection has the expected candidates'
  // types and, for relay local candidate, the expected relay protocol and ping
  // it.
  void VerifyNextPingableConnection(
      IceCandidateType local_candidate_type,
      IceCandidateType remote_candidate_type,
      absl::string_view relay_protocol_type = UDP_PROTOCOL_NAME) {
    Connection* conn = FindNextPingableConnectionAndPingIt(channel_.get());
    ASSERT_TRUE(conn != nullptr);
    EXPECT_EQ(conn->local_candidate().type(), local_candidate_type);
    if (conn->local_candidate().is_relay()) {
      EXPECT_EQ(conn->local_candidate().relay_protocol(), relay_protocol_type);
    }
    EXPECT_EQ(conn->remote_candidate().type(), remote_candidate_type);
  }

 protected:
  Waiter DefaultWait() {
    return Waiter({.timeout = kDefaultTimeout, .clock = &time_controller_});
  }
  Waiter MediumWait() {
    return Waiter({.timeout = kMediumTimeout, .clock = &time_controller_});
  }
  Waiter ShortWait() {
    return Waiter({.timeout = kShortTimeout, .clock = &time_controller_});
  }
  Waiter Wait(TimeDelta timeout) {
    return Waiter({.timeout = timeout, .clock = &time_controller_});
  }

  FakeNetworkManager& network_manager() { return network_manager_; }

 private:
  std::unique_ptr<BasicPortAllocator> port_allocator_;
  FakeNetworkManager network_manager_{Thread::Current()};
  std::optional<TestTurnServer> turn_server_;
  std::unique_ptr<P2PTransportChannel> channel_;
};

// Test that Relay/Relay connections will be pinged first when no other
// connections have been pinged yet, unless we need to ping a trigger check or
// we have a selected connection.
TEST_F(P2PTransportChannelMostLikelyToWorkFirstTest,
       TestRelayRelayFirstWhenNothingPingedYet) {
  const TimeDelta max_strong_interval = TimeDelta::Millis(500);
  CreatePortAllocator(env_);
  P2PTransportChannel& ch =
      StartTransportChannel(env_, true, max_strong_interval);
  EXPECT_THAT(DefaultWait().Until([&] { return ch.ports().size(); }, Eq(2)),
              IsRtcOk());
  EXPECT_EQ(ch.ports()[0]->Type(), IceCandidateType::kHost);
  EXPECT_EQ(ch.ports()[1]->Type(), IceCandidateType::kRelay);

  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kRelay, "1.1.1.1", 1, 1));
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "2.2.2.2", 2, 2));

  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.connections().size(); }, Eq(4)),
      IsRtcOk());

  // Relay/Relay should be the first pingable connection.
  Connection* conn = FindNextPingableConnectionAndPingIt(&ch);
  ASSERT_TRUE(conn != nullptr);
  EXPECT_TRUE(conn->local_candidate().is_relay());
  EXPECT_TRUE(conn->remote_candidate().is_relay());

  // Unless that we have a trigger check waiting to be pinged.
  Connection* conn2 = WaitForConnectionTo(&ch, "2.2.2.2", 2);
  ASSERT_TRUE(conn2 != nullptr);
  EXPECT_TRUE(conn2->local_candidate().is_local());
  EXPECT_TRUE(conn2->remote_candidate().is_local());
  conn2->ReceivedPing();
  EXPECT_EQ(conn2, FindNextPingableConnectionAndPingIt(&ch));

  // Make conn3 the selected connection.
  Connection* conn3 = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  ASSERT_TRUE(conn3 != nullptr);
  EXPECT_TRUE(conn3->local_candidate().is_local());
  EXPECT_TRUE(conn3->remote_candidate().is_relay());
  conn3->ReceivedPingResponse(kLowRtt, "id");
  ASSERT_TRUE(conn3->writable());
  conn3->ReceivedPing();

  /*

  TODO(honghaiz): Re-enable this once we use fake clock for this test to fix
  the flakiness. The following test becomes flaky because we now ping the
  connections with fast rates until every connection is pinged at least three
  times. The selected connection may have been pinged before
  `max_strong_interval`, so it may not be the next connection to be pinged as
  expected in the test.

  // Verify that conn3 will be the "selected connection" since it is readable
  // and writable. After `MAX_CURRENT_STRONG_INTERVAL`, it should be the next
  // pingable connection.
  EXPECT_TRUE_WAIT(conn3 == ch.selected_connection(), kDefaultTimeout);
  WAIT(false, max_strong_interval + 100);
  conn3->ReceivedPingResponse(kLowRtt, "id");
  ASSERT_TRUE(conn3->writable());
  EXPECT_EQ(conn3, FindNextPingableConnectionAndPingIt(&ch));

  */
}

// Test that Relay/Relay connections will be pinged first when everything has
// been pinged even if the Relay/Relay connection wasn't the first to be pinged
// in the first round.
TEST_F(P2PTransportChannelMostLikelyToWorkFirstTest,
       TestRelayRelayFirstWhenEverythingPinged) {
  CreatePortAllocator(env_);
  P2PTransportChannel& ch =
      StartTransportChannel(env_, true, TimeDelta::Millis(500));
  EXPECT_THAT(DefaultWait().Until([&] { return ch.ports().size(); }, Eq(2)),
              IsRtcOk());
  EXPECT_EQ(ch.ports()[0]->Type(), IceCandidateType::kHost);
  EXPECT_EQ(ch.ports()[1]->Type(), IceCandidateType::kRelay);

  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 1));
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.connections().size(); }, Eq(2)),
      IsRtcOk());

  // Initially, only have Local/Local and Local/Relay.
  VerifyNextPingableConnection(IceCandidateType::kHost,
                               IceCandidateType::kHost);
  VerifyNextPingableConnection(IceCandidateType::kRelay,
                               IceCandidateType::kHost);

  // Remote Relay candidate arrives.
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kRelay, "2.2.2.2", 2, 2));
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.connections().size(); }, Eq(4)),
      IsRtcOk());

  // Relay/Relay should be the first since it hasn't been pinged before.
  VerifyNextPingableConnection(IceCandidateType::kRelay,
                               IceCandidateType::kRelay);

  // Local/Relay is the final one.
  VerifyNextPingableConnection(IceCandidateType::kHost,
                               IceCandidateType::kRelay);

  // Now, every connection has been pinged once. The next one should be
  // Relay/Relay.
  VerifyNextPingableConnection(IceCandidateType::kRelay,
                               IceCandidateType::kRelay);
}

// Test that when we receive a new remote candidate, they will be tried first
// before we re-ping Relay/Relay connections again.
TEST_F(P2PTransportChannelMostLikelyToWorkFirstTest,
       TestNoStarvationOnNonRelayConnection) {
  CreatePortAllocator(env_);
  P2PTransportChannel& ch =
      StartTransportChannel(env_, true, TimeDelta::Millis(500));
  EXPECT_THAT(DefaultWait().Until([&] { return ch.ports().size(); }, Eq(2)),
              IsRtcOk());
  EXPECT_EQ(ch.ports()[0]->Type(), IceCandidateType::kHost);
  EXPECT_EQ(ch.ports()[1]->Type(), IceCandidateType::kRelay);

  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kRelay, "1.1.1.1", 1, 1));
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.connections().size(); }, Eq(2)),
      IsRtcOk());

  // Initially, only have Relay/Relay and Local/Relay. Ping Relay/Relay first.
  VerifyNextPingableConnection(IceCandidateType::kRelay,
                               IceCandidateType::kRelay);

  // Next, ping Local/Relay.
  VerifyNextPingableConnection(IceCandidateType::kHost,
                               IceCandidateType::kRelay);

  // Remote Local candidate arrives.
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "2.2.2.2", 2, 2));
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.connections().size(); }, Eq(4)),
      IsRtcOk());

  // Local/Local should be the first since it hasn't been pinged before.
  VerifyNextPingableConnection(IceCandidateType::kHost,
                               IceCandidateType::kHost);

  // Relay/Local is the final one.
  VerifyNextPingableConnection(IceCandidateType::kRelay,
                               IceCandidateType::kHost);

  // Now, every connection has been pinged once. The next one should be
  // Relay/Relay.
  VerifyNextPingableConnection(IceCandidateType::kRelay,
                               IceCandidateType::kRelay);
}

// Test skip_relay_to_non_relay_connections field-trial.
// I.e that we never create connection between relay and non-relay.
TEST_F(P2PTransportChannelMostLikelyToWorkFirstTest,
       TestSkipRelayToNonRelayConnectionsFieldTrial) {
  env_ = CreateTestEnvironment(
      {.field_trials =
           "WebRTC-IceFieldTrials/skip_relay_to_non_relay_connections:true/",
       .time = &time_controller_});
  CreatePortAllocator(env_);
  P2PTransportChannel& ch =
      StartTransportChannel(env_, true, TimeDelta::Millis(500));
  EXPECT_THAT(DefaultWait().Until([&] { return ch.ports().size(); }, Eq(2)),
              IsRtcOk());
  EXPECT_EQ(ch.ports()[0]->Type(), IceCandidateType::kHost);
  EXPECT_EQ(ch.ports()[1]->Type(), IceCandidateType::kRelay);

  // Remote Relay candidate arrives.
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kRelay, "1.1.1.1", 1, 1));
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.connections().size(); }, Eq(1)),
      IsRtcOk());

  // Remote Local candidate arrives.
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "2.2.2.2", 2, 2));
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.connections().size(); }, Eq(2)),
      IsRtcOk());
}

// Test the ping sequence is UDP Relay/Relay followed by TCP Relay/Relay,
// followed by the rest.
TEST_F(P2PTransportChannelMostLikelyToWorkFirstTest, TestTcpTurn) {
  RelayServerConfig config;
  config.credentials = kRelayCredentials;
  config.ports.push_back(ProtocolAddress(kTurnTcpIntAddr, PROTO_TCP));
  CreatePortAllocator(env_).AddTurnServerForTesting(config);
  // Add a Tcp Turn server.
  turn_server().AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);

  P2PTransportChannel& ch =
      StartTransportChannel(env_, true, TimeDelta::Millis(500));
  EXPECT_THAT(DefaultWait().Until([&] { return ch.ports().size(); }, Eq(3)),
              IsRtcOk());
  EXPECT_EQ(ch.ports()[0]->Type(), IceCandidateType::kHost);
  EXPECT_EQ(ch.ports()[1]->Type(), IceCandidateType::kRelay);
  EXPECT_EQ(ch.ports()[2]->Type(), IceCandidateType::kRelay);

  // Remote Relay candidate arrives.
  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kRelay, "1.1.1.1", 1, 1));
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.connections().size(); }, Eq(3)),
      IsRtcOk());

  // UDP Relay/Relay should be pinged first.
  VerifyNextPingableConnection(IceCandidateType::kRelay,
                               IceCandidateType::kRelay);

  // TCP Relay/Relay is the next.
  VerifyNextPingableConnection(IceCandidateType::kRelay,
                               IceCandidateType::kRelay, TCP_PROTOCOL_NAME);

  // Finally, Local/Relay will be pinged.
  VerifyNextPingableConnection(IceCandidateType::kHost,
                               IceCandidateType::kRelay);
}

// Test that a resolver is created, asked for a result, and destroyed
// when the address is a hostname. The destruction should happen even
// if the channel is not destroyed.
TEST(P2PTransportChannelResolverTest, HostnameCandidateIsResolved) {
  const Environment env = CreateTestEnvironment();
  ResolverFactoryFixture resolver_fixture;
  std::unique_ptr<SocketServer> socket_server = CreateDefaultSocketServer();
  test::RunLoop main_thread(socket_server.get());
  FakePortAllocator allocator(env, socket_server.get());
  IceTransportInit init(env);
  init.set_port_allocator(&allocator);
  init.set_async_dns_resolver_factory(&resolver_fixture);
  auto channel = P2PTransportChannel::Create("tn", 0, std::move(init));
  Candidate hostname_candidate;
  SocketAddress hostname_address("fake.test", 1000);
  hostname_candidate.set_address(hostname_address);
  channel->AddRemoteCandidate(hostname_candidate);

  ASSERT_TRUE(
      WaitUntil([&] { return channel->remote_candidates().size() == 1; }));
  const RemoteCandidate& candidate = channel->remote_candidates()[0];
  EXPECT_FALSE(candidate.address().IsUnresolvedIP());
}

// Test that if we signal a hostname candidate after the remote endpoint
// discovers a prflx remote candidate with the same underlying IP address, the
// prflx candidate is updated to a host candidate after the name resolution is
// done.
TEST_F(P2PTransportChannelTest,
       PeerReflexiveCandidateBeforeSignalingWithMdnsName) {
  // ep1 and ep2 will only gather host candidates with addresses
  // kPublicAddrs[0] and kPublicAddrs[1], respectively.
  ConfigureEndpoints(OPEN, OPEN, kOnlyLocalPorts, kOnlyLocalPorts);
  // ICE parameter will be set up when creating the channels.
  set_remote_ice_parameter_source(FROM_SETICEPARAMETERS);
  GetEndpoint(0)->network_manager().set_mdns_responder(
      std::make_unique<FakeMdnsResponder>(Thread::Current()));

  ResolverFactoryFixture resolver_fixture;
  GetEndpoint(1)->set_async_dns_resolver_factory(&resolver_fixture);
  CreateChannels();
  // Pause sending candidates from both endpoints until we find out what port
  // number is assgined to ep1's host candidate.
  PauseCandidates(0);
  PauseCandidates(1);
  ASSERT_THAT(
      DefaultWait().Until(
          [&] { return GetEndpoint(0)->saved_candidates().size(); }, Eq(1u)),
      IsRtcOk());
  const auto& local_candidate = GetEndpoint(0)->saved_candidates()[0].candidate;
  // The IP address of ep1's host candidate should be obfuscated.
  EXPECT_TRUE(local_candidate.address().IsUnresolvedIP());
  // This is the underlying private IP address of the same candidate at ep1.
  const auto local_address =
      SocketAddress(kPublicAddrs[0].ipaddr(), local_candidate.address().port());

  // Let ep2 signal its candidate to ep1. ep1 should form a candidate
  // pair and start to ping. After receiving the ping, ep2 discovers a prflx
  // remote candidate and form a candidate pair as well.
  ResumeCandidates(1);
  ASSERT_THAT(
      MediumWait().Until([&] { return ep1_ch1()->selected_connection(); },
                         Ne(nullptr)),
      IsRtcOk());
  // ep2 should have the selected connection connected to the prflx remote
  // candidate.
  const Connection* selected_connection = nullptr;
  ASSERT_THAT(MediumWait().Until(
                  [&] {
                    return selected_connection =
                               ep2_ch1()->selected_connection();
                  },
                  Ne(nullptr)),
              IsRtcOk());
  EXPECT_TRUE(selected_connection->remote_candidate().is_prflx());
  EXPECT_EQ(kIceUfrag[0], selected_connection->remote_candidate().username());
  EXPECT_EQ(kIcePwd[0], selected_connection->remote_candidate().password());
  // Set expectation before ep1 signals a hostname candidate.
  resolver_fixture.SetAddressToReturn(local_address);
  ResumeCandidates(0);
  // Verify ep2's selected connection is updated to use the 'local' candidate.
  EXPECT_TRUE(MediumWait().Until([&] {
    return ep2_ch1()->selected_connection()->remote_candidate().is_local();
  }));
  EXPECT_EQ(selected_connection, ep2_ch1()->selected_connection());

  DestroyChannels();
}

// Test that if we discover a prflx candidate during the process of name
// resolution for a remote hostname candidate, we update the prflx candidate to
// a host candidate if the hostname candidate turns out to have the same IP
// address after the resolution completes.
TEST_F(P2PTransportChannelTest,
       PeerReflexiveCandidateDuringResolvingHostCandidateWithMdnsName) {
  ResolverFactoryFixture resolver_fixture;
  // Prevent resolution until triggered by FireDelayedResolution.
  resolver_fixture.DelayResolution();

  // ep1 and ep2 will only gather host candidates with addresses
  // kPublicAddrs[0] and kPublicAddrs[1], respectively.
  ConfigureEndpoints(OPEN, OPEN, kOnlyLocalPorts, kOnlyLocalPorts);
  // ICE parameter will be set up when creating the channels.
  set_remote_ice_parameter_source(FROM_SETICEPARAMETERS);
  GetEndpoint(0)->network_manager().set_mdns_responder(
      std::make_unique<FakeMdnsResponder>(Thread::Current()));
  GetEndpoint(1)->set_async_dns_resolver_factory(&resolver_fixture);
  CreateChannels();
  // Pause sending candidates from both endpoints until we find out what port
  // number is assgined to ep1's host candidate.
  PauseCandidates(0);
  PauseCandidates(1);

  ASSERT_THAT(
      MediumWait().Until(
          [&] { return GetEndpoint(0)->saved_candidates().size(); }, Eq(1u)),
      IsRtcOk());
  const auto& local_candidate = GetEndpoint(0)->saved_candidates()[0].candidate;
  // The IP address of ep1's host candidate should be obfuscated.
  ASSERT_TRUE(local_candidate.address().IsUnresolvedIP());
  // This is the underlying private IP address of the same candidate at ep1.
  const auto local_address =
      SocketAddress(kPublicAddrs[0].ipaddr(), local_candidate.address().port());
  // Let ep1 signal its hostname candidate to ep2.
  ResumeCandidates(0);
  // Now that ep2 is in the process of resolving the hostname candidate signaled
  // by ep1. Let ep2 signal its host candidate with an IP address to ep1, so
  // that ep1 can form a candidate pair, select it and start to ping ep2.
  ResumeCandidates(1);
  ASSERT_THAT(
      MediumWait().Until([&] { return ep1_ch1()->selected_connection(); },
                         Ne(nullptr)),
      IsRtcOk());
  // Let the mock resolver of ep2 receives the correct resolution.
  resolver_fixture.SetAddressToReturn(local_address);

  // Upon receiving a ping from ep1, ep2 adds a prflx candidate from the
  // unknown address and establishes a connection.
  //
  // There is a caveat in our implementation associated with this expectation.
  // See the big comment in P2PTransportChannel::OnUnknownAddress.
  ASSERT_THAT(
      MediumWait().Until([&] { return ep2_ch1()->selected_connection(); },
                         Ne(nullptr)),
      IsRtcOk());
  EXPECT_TRUE(ep2_ch1()->selected_connection()->remote_candidate().is_prflx());
  // ep2 should also be able resolve the hostname candidate. The resolved remote
  // host candidate should be merged with the prflx remote candidate.

  resolver_fixture.FireDelayedResolution();

  EXPECT_TRUE(MediumWait().Until([&] {
    return ep2_ch1()->selected_connection()->remote_candidate().is_local();
  }));
  EXPECT_EQ(1u, ep2_ch1()->remote_candidates().size());

  DestroyChannels();
}

// Test that if we only gather and signal a host candidate, the IP address of
// which is obfuscated by an mDNS name, and if the peer can complete the name
// resolution with the correct IP address, we can have a p2p connection.
TEST_F(P2PTransportChannelTest, CanConnectWithHostCandidateWithMdnsName) {
  ResolverFactoryFixture resolver_fixture;

  // ep1 and ep2 will only gather host candidates with addresses
  // kPublicAddrs[0] and kPublicAddrs[1], respectively.
  ConfigureEndpoints(OPEN, OPEN, kOnlyLocalPorts, kOnlyLocalPorts);
  // ICE parameter will be set up when creating the channels.
  set_remote_ice_parameter_source(FROM_SETICEPARAMETERS);
  GetEndpoint(0)->network_manager().set_mdns_responder(
      std::make_unique<FakeMdnsResponder>(Thread::Current()));
  GetEndpoint(1)->set_async_dns_resolver_factory(&resolver_fixture);
  CreateChannels();
  // Pause sending candidates from both endpoints until we find out what port
  // number is assgined to ep1's host candidate.
  PauseCandidates(0);
  PauseCandidates(1);
  ASSERT_THAT(
      MediumWait().Until(
          [&] { return GetEndpoint(0)->saved_candidates().size(); }, Eq(1u)),
      IsRtcOk());
  const auto& local_candidate_ep1 =
      GetEndpoint(0)->saved_candidates()[0].candidate;
  // The IP address of ep1's host candidate should be obfuscated.
  EXPECT_TRUE(local_candidate_ep1.address().IsUnresolvedIP());
  // This is the underlying private IP address of the same candidate at ep1,
  // and let the mock resolver of ep2 receive the correct resolution.
  SocketAddress resolved_address_ep1(local_candidate_ep1.address());
  resolved_address_ep1.SetResolvedIP(kPublicAddrs[0].ipaddr());

  resolver_fixture.SetAddressToReturn(resolved_address_ep1);
  // Let ep1 signal its hostname candidate to ep2.
  ResumeCandidates(0);

  // We should be able to receive a ping from ep2 and establish a connection
  // with a peer reflexive candidate from ep2.
  ASSERT_THAT(
      MediumWait().Until([&] { return ep1_ch1()->selected_connection(); },
                         Ne(nullptr)),
      IsRtcOk());
  EXPECT_TRUE(ep1_ch1()->selected_connection()->local_candidate().is_local());
  EXPECT_TRUE(ep1_ch1()->selected_connection()->remote_candidate().is_prflx());

  DestroyChannels();
}

// Test that when the IP of a host candidate is concealed by an mDNS name, the
// stats from the gathering ICE endpoint do not reveal the address of this local
// host candidate or the related address of a local srflx candidate from the
// same endpoint. Also, the remote ICE endpoint that successfully resolves a
// signaled host candidate with an mDNS name should not reveal the address of
// this remote host candidate in stats.
TEST_F(P2PTransportChannelTest,
       CandidatesSanitizedInStatsWhenMdnsObfuscationEnabled) {
  ResolverFactoryFixture resolver_fixture;

  // ep1 and ep2 will gather host candidates with addresses
  // kPublicAddrs[0] and kPublicAddrs[1], respectively. ep1 also gathers a srflx
  // and a relay candidates.
  ConfigureEndpoints(OPEN, OPEN,
                     kDefaultPortAllocatorFlags | PORTALLOCATOR_DISABLE_TCP,
                     kOnlyLocalPorts);
  // ICE parameter will be set up when creating the channels.
  set_remote_ice_parameter_source(FROM_SETICEPARAMETERS);
  GetEndpoint(0)->network_manager().set_mdns_responder(
      std::make_unique<FakeMdnsResponder>(Thread::Current()));
  GetEndpoint(1)->set_async_dns_resolver_factory(&resolver_fixture);
  CreateChannels();
  // Pause sending candidates from both endpoints until we find out what port
  // number is assigned to ep1's host candidate.
  PauseCandidates(0);
  PauseCandidates(1);
  // Ep1 has a UDP host, a srflx and a relay candidates.
  ASSERT_THAT(
      MediumWait().Until(
          [&] { return GetEndpoint(0)->saved_candidates().size(); }, Eq(3u)),
      IsRtcOk());
  ASSERT_THAT(
      MediumWait().Until(
          [&] { return GetEndpoint(1)->saved_candidates().size(); }, Eq(1u)),
      IsRtcOk());

  for (const auto& candidates_data : GetEndpoint(0)->saved_candidates()) {
    const auto& local_candidate_ep1 = candidates_data.candidate;
    if (local_candidate_ep1.is_local()) {
      // This is the underlying private IP address of the same candidate at ep1,
      // and let the mock resolver of ep2 receive the correct resolution.
      SocketAddress resolved_address_ep1(local_candidate_ep1.address());
      resolved_address_ep1.SetResolvedIP(kPublicAddrs[0].ipaddr());
      resolver_fixture.SetAddressToReturn(resolved_address_ep1);
      break;
    }
  }
  ResumeCandidates(0);
  ResumeCandidates(1);

  ASSERT_THAT(MediumWait().Until([&] { return ep1_ch1()->gathering_state(); },
                                 Eq(kIceGatheringComplete)),
              IsRtcOk());
  // We should have the following candidate pairs on both endpoints:
  // ep1_host <-> ep2_host, ep1_srflx <-> ep2_host, ep1_relay <-> ep2_host
  ASSERT_THAT(MediumWait().Until(
                  [&] { return ep1_ch1()->connections().size(); }, Eq(3u)),
              IsRtcOk());
  ASSERT_THAT(MediumWait().Until(
                  [&] { return ep2_ch1()->connections().size(); }, Eq(3u)),
              IsRtcOk());

  IceTransportStats ice_transport_stats1;
  IceTransportStats ice_transport_stats2;
  ep1_ch1()->GetStats(&ice_transport_stats1);
  ep2_ch1()->GetStats(&ice_transport_stats2);
  EXPECT_EQ(3u, ice_transport_stats1.connection_infos.size());
  EXPECT_EQ(3u, ice_transport_stats1.candidate_stats_list.size());
  EXPECT_EQ(3u, ice_transport_stats2.connection_infos.size());
  // Check the stats of ep1 seen by ep1.
  for (const auto& connection_info : ice_transport_stats1.connection_infos) {
    const auto& local_candidate = connection_info.local_candidate;
    if (local_candidate.is_local()) {
      EXPECT_TRUE(local_candidate.address().IsUnresolvedIP());
    } else if (local_candidate.is_stun()) {
      EXPECT_TRUE(local_candidate.related_address().IsAnyIP());
    } else if (local_candidate.is_relay()) {
      // The related address of the relay candidate should be equal to the
      // srflx address. Note that NAT is not configured, hence the following
      // expectation.
      EXPECT_EQ(kPublicAddrs[0].ipaddr(),
                local_candidate.related_address().ipaddr());
    } else {
      FAIL();
    }
  }
  // Check the stats of ep1 seen by ep2.
  for (const auto& connection_info : ice_transport_stats2.connection_infos) {
    const auto& remote_candidate = connection_info.remote_candidate;
    if (remote_candidate.is_local()) {
      EXPECT_TRUE(remote_candidate.address().IsUnresolvedIP());
    } else if (remote_candidate.is_stun()) {
      EXPECT_TRUE(remote_candidate.related_address().IsAnyIP());
    } else if (remote_candidate.is_relay()) {
      EXPECT_EQ(kPublicAddrs[0].ipaddr(),
                remote_candidate.related_address().ipaddr());
    } else {
      FAIL();
    }
  }
  DestroyChannels();
}

TEST_F(P2PTransportChannelTest,
       ConnectingIncreasesSelectedCandidatePairChanges) {
  ConfigureEndpoints(OPEN, OPEN, kDefaultPortAllocatorFlags,
                     kDefaultPortAllocatorFlags);
  CreateChannels();

  IceTransportStats ice_transport_stats;
  ASSERT_TRUE(ep1_ch1()->GetStats(&ice_transport_stats));
  EXPECT_EQ(0u, ice_transport_stats.selected_candidate_pair_changes);

  // Let the channels connect.
  EXPECT_THAT(
      MediumWait().Until([&] { return ep1_ch1()->selected_connection(); },
                         Ne(nullptr)),
      IsRtcOk());

  ASSERT_TRUE(ep1_ch1()->GetStats(&ice_transport_stats));
  EXPECT_EQ(1u, ice_transport_stats.selected_candidate_pair_changes);

  DestroyChannels();
}

TEST_F(P2PTransportChannelTest,
       DisconnectedIncreasesSelectedCandidatePairChanges) {
  ConfigureEndpoints(OPEN, OPEN, kDefaultPortAllocatorFlags,
                     kDefaultPortAllocatorFlags);
  CreateChannels();

  IceTransportStats ice_transport_stats;
  ASSERT_TRUE(ep1_ch1()->GetStats(&ice_transport_stats));
  EXPECT_EQ(0u, ice_transport_stats.selected_candidate_pair_changes);

  // Let the channels connect.
  EXPECT_THAT(
      MediumWait().Until([&] { return ep1_ch1()->selected_connection(); },
                         Ne(nullptr)),
      IsRtcOk());

  ASSERT_TRUE(ep1_ch1()->GetStats(&ice_transport_stats));
  EXPECT_EQ(1u, ice_transport_stats.selected_candidate_pair_changes);

  // Prune connections and wait for disconnect.
  for (Connection* con : ep1_ch1()->connections()) {
    con->Prune();
  }
  EXPECT_THAT(
      MediumWait().Until([&] { return ep1_ch1()->selected_connection(); },
                         Eq(nullptr)),
      IsRtcOk());

  ASSERT_TRUE(ep1_ch1()->GetStats(&ice_transport_stats));
  EXPECT_EQ(2u, ice_transport_stats.selected_candidate_pair_changes);

  DestroyChannels();
}

TEST_F(P2PTransportChannelTest,
       NewSelectionIncreasesSelectedCandidatePairChanges) {
  ConfigureEndpoints(OPEN, OPEN, kDefaultPortAllocatorFlags,
                     kDefaultPortAllocatorFlags);
  CreateChannels();

  IceTransportStats ice_transport_stats;
  ASSERT_TRUE(ep1_ch1()->GetStats(&ice_transport_stats));
  EXPECT_EQ(0u, ice_transport_stats.selected_candidate_pair_changes);

  // Let the channels connect.
  EXPECT_THAT(
      MediumWait().Until([&] { return ep1_ch1()->selected_connection(); },
                         Ne(nullptr)),
      IsRtcOk());

  ASSERT_TRUE(ep1_ch1()->GetStats(&ice_transport_stats));
  EXPECT_EQ(1u, ice_transport_stats.selected_candidate_pair_changes);

  // Prune the currently selected connection and wait for selection
  // of a new one.
  const Connection* selected_connection = ep1_ch1()->selected_connection();
  for (Connection* con : ep1_ch1()->connections()) {
    if (con == selected_connection) {
      con->Prune();
    }
  }
  EXPECT_TRUE(MediumWait().Until([&] {
    return ep1_ch1()->selected_connection() != nullptr &&
           (ep1_ch1()->GetStats(&ice_transport_stats),
            ice_transport_stats.selected_candidate_pair_changes >= 2u);
  }));

  ASSERT_TRUE(ep1_ch1()->GetStats(&ice_transport_stats));
  EXPECT_GE(ice_transport_stats.selected_candidate_pair_changes, 2u);

  DestroyChannels();
}

// A similar test as above to check the selected candidate pair is sanitized
// when it is queried via GetSelectedCandidatePair.
TEST_F(P2PTransportChannelTest,
       SelectedCandidatePairSanitizedWhenMdnsObfuscationEnabled) {
  ResolverFactoryFixture resolver_fixture;

  // ep1 and ep2 will gather host candidates with addresses
  // kPublicAddrs[0] and kPublicAddrs[1], respectively.
  ConfigureEndpoints(OPEN, OPEN, kOnlyLocalPorts, kOnlyLocalPorts);
  // ICE parameter will be set up when creating the channels.
  set_remote_ice_parameter_source(FROM_SETICEPARAMETERS);
  GetEndpoint(0)->network_manager().set_mdns_responder(
      std::make_unique<FakeMdnsResponder>(Thread::Current()));
  GetEndpoint(1)->set_async_dns_resolver_factory(&resolver_fixture);
  CreateChannels();
  // Pause sending candidates from both endpoints until we find out what port
  // number is assigned to ep1's host candidate.
  PauseCandidates(0);
  PauseCandidates(1);
  ASSERT_THAT(
      MediumWait().Until(
          [&] { return GetEndpoint(0)->saved_candidates().size(); }, Eq(1u)),
      IsRtcOk());
  const auto& candidates_data = GetEndpoint(0)->saved_candidates()[0];
  const auto& local_candidate_ep1 = candidates_data.candidate;
  ASSERT_TRUE(local_candidate_ep1.is_local());
  // This is the underlying private IP address of the same candidate at ep1,
  // and let the mock resolver of ep2 receive the correct resolution.
  SocketAddress resolved_address_ep1(local_candidate_ep1.address());
  resolved_address_ep1.SetResolvedIP(kPublicAddrs[0].ipaddr());
  resolver_fixture.SetAddressToReturn(resolved_address_ep1);

  ResumeCandidates(0);
  ResumeCandidates(1);

  ASSERT_TRUE(MediumWait().Until([&] {
    return ep1_ch1()->selected_connection() != nullptr &&
           ep2_ch1()->selected_connection() != nullptr;
  }));

  const auto pair_ep1 = ep1_ch1()->GetSelectedCandidatePair();
  ASSERT_TRUE(pair_ep1.has_value());
  EXPECT_TRUE(pair_ep1->local_candidate().is_local());
  EXPECT_TRUE(pair_ep1->local_candidate().address().IsUnresolvedIP());

  const auto pair_ep2 = ep2_ch1()->GetSelectedCandidatePair();
  ASSERT_TRUE(pair_ep2.has_value());
  EXPECT_TRUE(pair_ep2->remote_candidate().is_local());
  EXPECT_TRUE(pair_ep2->remote_candidate().address().IsUnresolvedIP());

  DestroyChannels();
}

TEST_F(P2PTransportChannelTest,
       NoPairOfLocalRelayCandidateWithRemoteMdnsCandidate) {
  const int kOnlyRelayPorts = PORTALLOCATOR_DISABLE_UDP |
                              PORTALLOCATOR_DISABLE_STUN |
                              PORTALLOCATOR_DISABLE_TCP;
  // We use one endpoint to test the behavior of adding remote candidates, and
  // this endpoint only gathers relay candidates.
  ConfigureEndpoints(OPEN, OPEN, kOnlyRelayPorts, kDefaultPortAllocatorFlags);
  GetEndpoint(0)->cd1().set_ch(CreateChannel(0, ICE_CANDIDATE_COMPONENT_DEFAULT,
                                             kIceParams[0], kIceParams[1]));
  IceConfig config;
  // Start gathering and we should have only a single relay port.
  ep1_ch1()->SetIceConfig(config);
  ep1_ch1()->MaybeStartGathering();
  EXPECT_THAT(MediumWait().Until([&] { return ep1_ch1()->gathering_state(); },
                                 Eq(IceGatheringState::kIceGatheringComplete)),
              IsRtcOk());
  EXPECT_EQ(1u, ep1_ch1()->ports().size());
  // Add a plain remote host candidate and remote mDNS candidates with the host,
  // srflx and relay types. Note that the candidates differ in their ports.
  Candidate host_candidate = CreateUdpCandidate(
      IceCandidateType::kHost, "1.1.1.1", 1 /* port */, 0 /* priority */);
  ep1_ch1()->AddRemoteCandidate(host_candidate);

  std::vector<Candidate> mdns_candidates;
  mdns_candidates.push_back(CreateUdpCandidate(IceCandidateType::kHost,
                                               "example.local", 2 /* port */,
                                               0 /* priority */));
  mdns_candidates.push_back(CreateUdpCandidate(IceCandidateType::kSrflx,
                                               "example.local", 3 /* port */,
                                               0 /* priority */));
  mdns_candidates.push_back(CreateUdpCandidate(IceCandidateType::kRelay,
                                               "example.local", 4 /* port */,
                                               0 /* priority */));
  // DNS names are case-insensitive and may be written in fully qualified form,
  // with a trailing dot. Such names are resolved using mDNS as well.
  mdns_candidates.push_back(CreateUdpCandidate(IceCandidateType::kHost,
                                               "example.local.", 5 /* port */,
                                               0 /* priority */));
  mdns_candidates.push_back(CreateUdpCandidate(IceCandidateType::kHost,
                                               "EXAMPLE.LOCAL", 6 /* port */,
                                               0 /* priority */));
  mdns_candidates.push_back(CreateUdpCandidate(IceCandidateType::kHost,
                                               "Example.Local.", 7 /* port */,
                                               0 /* priority */));
  // We just resolve the hostname to 1.1.1.1, and add the candidates with this
  // address directly to simulate the process of adding remote candidates with
  // the name resolution.
  for (auto& mdns_candidate : mdns_candidates) {
    SocketAddress resolved_address(mdns_candidate.address());
    resolved_address.SetResolvedIP(0x1111);  // 1.1.1.1
    mdns_candidate.set_address(resolved_address);
    EXPECT_FALSE(mdns_candidate.address().IsUnresolvedIP());
    ep1_ch1()->AddRemoteCandidate(mdns_candidate);
  }

  // All remote candidates should have been successfully added.
  EXPECT_EQ(1u + mdns_candidates.size(), ep1_ch1()->remote_candidates().size());

  // Expect that there is no connection paired with any mDNS candidate.
  ASSERT_EQ(1u, ep1_ch1()->connections().size());
  ASSERT_NE(nullptr, ep1_ch1()->connections()[0]);
  EXPECT_EQ(
      "1.1.1.1:1",
      ep1_ch1()->connections()[0]->remote_candidate().address().ToString());
  DestroyChannels();
}

// Test that the address of a remote candidate with an mDNS name is concealed in
// the candidate pair stats, also when the name is written in fully qualified
// form (with a trailing dot) or in a different case. Such names are resolved
// using mDNS as well.
TEST_F(P2PTransportChannelTest, RemoteMdnsCandidatesSanitizedInStats) {
  ConfigureEndpoints(OPEN, OPEN, kOnlyLocalPorts, kOnlyLocalPorts);
  GetEndpoint(0)->cd1().set_ch(CreateChannel(0, ICE_CANDIDATE_COMPONENT_DEFAULT,
                                             kIceParams[0], kIceParams[1]));
  ep1_ch1()->MaybeStartGathering();
  ASSERT_THAT(MediumWait().Until([&] { return ep1_ch1()->gathering_state(); },
                                 Eq(IceGatheringState::kIceGatheringComplete)),
              IsRtcOk());
  ASSERT_EQ(1u, ep1_ch1()->ports().size());

  const std::vector<absl::string_view> mdns_names = {
      "example.local", "example.local.", "EXAMPLE.LOCAL", "Example.Local."};
  int port = 1;
  for (absl::string_view mdns_name : mdns_names) {
    Candidate candidate = CreateUdpCandidate(IceCandidateType::kHost, mdns_name,
                                             port++, 0 /* priority */);
    // Resolve the hostname to 1.1.1.1 and add the candidate with this address
    // directly to simulate adding a remote candidate with name resolution.
    SocketAddress resolved_address(candidate.address());
    resolved_address.SetResolvedIP(0x1111);  // 1.1.1.1
    candidate.set_address(resolved_address);
    ep1_ch1()->AddRemoteCandidate(candidate);
  }

  IceTransportStats ice_transport_stats;
  ASSERT_TRUE(ep1_ch1()->GetStats(&ice_transport_stats));
  ASSERT_EQ(mdns_names.size(), ice_transport_stats.connection_infos.size());
  for (const auto& connection_info : ice_transport_stats.connection_infos) {
    const SocketAddress& address = connection_info.remote_candidate.address();
    EXPECT_TRUE(address.IsUnresolvedIP()) << address.hostname();
  }
  DestroyChannels();
}

class MockMdnsResponder : public MdnsResponderInterface {
 public:
  MOCK_METHOD(void,
              CreateNameForAddress,
              (const IPAddress&, NameCreatedCallback),
              (override));
  MOCK_METHOD(void,
              RemoveNameForAddress,
              (const IPAddress&, NameRemovedCallback),
              (override));
};

TEST_F(P2PTransportChannelTest,
       SrflxCandidateCanBeGatheredBeforeMdnsCandidateToCreateConnection) {
  // ep1 and ep2 will only gather host and srflx candidates with base addresses
  // kPublicAddrs[0] and kPublicAddrs[1], respectively, and we use a shared
  // socket in gathering.
  const auto kOnlyLocalAndStunPorts = PORTALLOCATOR_DISABLE_RELAY |
                                      PORTALLOCATOR_DISABLE_TCP |
                                      PORTALLOCATOR_ENABLE_SHARED_SOCKET;
  // ep1 is configured with a NAT so that we do gather a srflx candidate.
  ConfigureEndpoints(NAT_FULL_CONE, OPEN, kOnlyLocalAndStunPorts,
                     kOnlyLocalAndStunPorts);
  // ICE parameter will be set up when creating the channels.
  set_remote_ice_parameter_source(FROM_SETICEPARAMETERS);
  // Use a mock mDNS responder, which does not complete the name registration by
  // ignoring the completion callback.
  auto mock_mdns_responder = std::make_unique<MockMdnsResponder>();
  EXPECT_CALL(*mock_mdns_responder, CreateNameForAddress(_, _))
      .WillOnce(Return());
  GetEndpoint(0)->network_manager().set_mdns_responder(
      std::move(mock_mdns_responder));

  CreateChannels();

  // We should be able to form a srflx-host connection to ep2.
  ASSERT_THAT(
      DefaultWait().Until([&] { return ep1_ch1()->selected_connection(); },
                          Ne(nullptr)),
      IsRtcOk());
  EXPECT_TRUE(ep1_ch1()->selected_connection()->local_candidate().is_stun());
  EXPECT_TRUE(ep1_ch1()->selected_connection()->remote_candidate().is_local());

  DestroyChannels();
}

// Test that after changing the candidate filter from relay-only to allowing all
// types of candidates when doing continual gathering, we can gather without ICE
// restart the other types of candidates that are now enabled and form candidate
// pairs. Also, we verify that the relay candidates gathered previously are not
// removed and are still usable for necessary route switching.
TEST_F(P2PTransportChannelTest,
       SurfaceHostCandidateOnCandidateFilterChangeFromRelayToAll) {
  ConfigureEndpoints(
      OPEN, OPEN,
      kDefaultPortAllocatorFlags | PORTALLOCATOR_ENABLE_SHARED_SOCKET,
      kDefaultPortAllocatorFlags | PORTALLOCATOR_ENABLE_SHARED_SOCKET);
  auto* ep1 = GetEndpoint(0);
  auto* ep2 = GetEndpoint(1);
  ep1->allocator()->SetCandidateFilter(CF_RELAY);
  ep2->allocator()->SetCandidateFilter(CF_RELAY);
  // Enable continual gathering and also resurfacing gathered candidates upon
  // the candidate filter changed in the ICE configuration.
  IceConfig ice_config =
      CreateIceConfig(TimeDelta::Seconds(1), GATHER_CONTINUALLY);
  ice_config.surface_ice_candidates_on_ice_transport_type_changed = true;
  CreateChannels(ice_config, ice_config);
  ASSERT_THAT(
      MediumWait().Until([&] { return ep1_ch1()->selected_connection(); },
                         Ne(nullptr)),
      IsRtcOk());
  ASSERT_THAT(
      DefaultWait().Until([&] { return ep2_ch1()->selected_connection(); },
                          Ne(nullptr)),
      IsRtcOk());
  EXPECT_TRUE(ep1_ch1()->selected_connection()->local_candidate().is_relay());
  EXPECT_TRUE(ep2_ch1()->selected_connection()->local_candidate().is_relay());

  // Loosen the candidate filter at ep1.
  ep1->allocator()->SetCandidateFilter(CF_ALL);
  EXPECT_TRUE(DefaultWait().Until([&] {
    return ep1_ch1()->selected_connection() != nullptr &&
           ep1_ch1()->selected_connection()->local_candidate().is_local();
  }));
  EXPECT_TRUE(ep1_ch1()->selected_connection()->remote_candidate().is_relay());

  // Loosen the candidate filter at ep2.
  ep2->allocator()->SetCandidateFilter(CF_ALL);
  EXPECT_TRUE(DefaultWait().Until([&] {
    return ep2_ch1()->selected_connection() != nullptr &&
           ep2_ch1()->selected_connection()->local_candidate().is_local();
  }));
  // We have migrated to a host-host candidate pair.
  EXPECT_TRUE(ep2_ch1()->selected_connection()->remote_candidate().is_local());

  // Block the traffic over non-relay-to-relay routes and expect a route change.
  fw()->AddRule(false, FP_ANY, kPublicAddrs[0], kPublicAddrs[1]);
  fw()->AddRule(false, FP_ANY, kPublicAddrs[1], kPublicAddrs[0]);
  fw()->AddRule(false, FP_ANY, kPublicAddrs[0], kTurnUdpExtAddr);
  fw()->AddRule(false, FP_ANY, kPublicAddrs[1], kTurnUdpExtAddr);

  // We should be able to reuse the previously gathered relay candidates.
  EXPECT_TRUE(DefaultWait().Until([&] {
    return ep1_ch1()->selected_connection()->local_candidate().is_relay();
  }));
  EXPECT_TRUE(ep1_ch1()->selected_connection()->remote_candidate().is_relay());
  DestroyChannels();
}

// A similar test as SurfaceHostCandidateOnCandidateFilterChangeFromRelayToAll,
// and we should surface server-reflexive candidates that are enabled after
// changing the candidate filter.
TEST_F(P2PTransportChannelTest,
       SurfaceSrflxCandidateOnCandidateFilterChangeFromRelayToNoHost) {
  // We need an actual NAT so that the host candidate is not equivalent to the
  // srflx candidate; otherwise, the host candidate would still surface even
  // though we disable it via the candidate filter below. This is a result of
  // the following limitation in the current implementation:
  //  1. We don't generate the srflx candidate when we have public IP.
  //  2. We keep the host candidate in this case in CheckCandidateFilter even
  //     though we intend to filter them.
  ConfigureEndpoints(
      NAT_FULL_CONE, NAT_FULL_CONE,
      kDefaultPortAllocatorFlags | PORTALLOCATOR_ENABLE_SHARED_SOCKET,
      kDefaultPortAllocatorFlags | PORTALLOCATOR_ENABLE_SHARED_SOCKET);
  auto* ep1 = GetEndpoint(0);
  auto* ep2 = GetEndpoint(1);
  ep1->allocator()->SetCandidateFilter(CF_RELAY);
  ep2->allocator()->SetCandidateFilter(CF_RELAY);
  // Enable continual gathering and also resurfacing gathered candidates upon
  // the candidate filter changed in the ICE configuration.
  IceConfig ice_config =
      CreateIceConfig(TimeDelta::Seconds(1), GATHER_CONTINUALLY);
  ice_config.surface_ice_candidates_on_ice_transport_type_changed = true;
  CreateChannels(ice_config, ice_config);
  ASSERT_THAT(
      DefaultWait().Until([&] { return ep1_ch1()->selected_connection(); },
                          Ne(nullptr)),
      IsRtcOk());
  ASSERT_THAT(
      DefaultWait().Until([&] { return ep2_ch1()->selected_connection(); },
                          Ne(nullptr)),
      IsRtcOk());
  const uint32_t kCandidateFilterNoHost = CF_ALL & ~CF_HOST;
  // Loosen the candidate filter at ep1.
  ep1->allocator()->SetCandidateFilter(kCandidateFilterNoHost);
  EXPECT_TRUE(DefaultWait().Until([&] {
    return ep1_ch1()->selected_connection() != nullptr &&
           ep1_ch1()->selected_connection()->local_candidate().is_stun();
  }));
  EXPECT_TRUE(ep1_ch1()->selected_connection()->remote_candidate().is_relay());

  // Loosen the candidate filter at ep2.
  ep2->allocator()->SetCandidateFilter(kCandidateFilterNoHost);
  EXPECT_TRUE(DefaultWait().Until([&] {
    return ep2_ch1()->selected_connection() != nullptr &&
           ep2_ch1()->selected_connection()->local_candidate().is_stun();
  }));
  // We have migrated to a srflx-srflx candidate pair.
  EXPECT_TRUE(ep2_ch1()->selected_connection()->remote_candidate().is_stun());

  // Block the traffic over non-relay-to-relay routes and expect a route change.
  fw()->AddRule(false, FP_ANY, kPrivateAddrs[0], kPublicAddrs[1]);
  fw()->AddRule(false, FP_ANY, kPrivateAddrs[1], kPublicAddrs[0]);
  fw()->AddRule(false, FP_ANY, kPrivateAddrs[0], kTurnUdpExtAddr);
  fw()->AddRule(false, FP_ANY, kPrivateAddrs[1], kTurnUdpExtAddr);
  // We should be able to reuse the previously gathered relay candidates.
  EXPECT_TRUE(DefaultWait().Until([&] {
    return ep1_ch1()->selected_connection()->local_candidate().is_relay();
  }));
  EXPECT_TRUE(ep1_ch1()->selected_connection()->remote_candidate().is_relay());
  DestroyChannels();
}

// This is the complement to
// SurfaceHostCandidateOnCandidateFilterChangeFromRelayToAll, and instead of
// gathering continually we only gather once, which makes the config
// `surface_ice_candidates_on_ice_transport_type_changed` ineffective after the
// gathering stopped.
TEST_F(P2PTransportChannelTest,
       CannotSurfaceTheNewlyAllowedOnFilterChangeIfNotGatheringContinually) {
  ConfigureEndpoints(
      OPEN, OPEN,
      kDefaultPortAllocatorFlags | PORTALLOCATOR_ENABLE_SHARED_SOCKET,
      kDefaultPortAllocatorFlags | PORTALLOCATOR_ENABLE_SHARED_SOCKET);
  auto* ep1 = GetEndpoint(0);
  auto* ep2 = GetEndpoint(1);
  ep1->allocator()->SetCandidateFilter(CF_RELAY);
  ep2->allocator()->SetCandidateFilter(CF_RELAY);
  // Only gather once.
  IceConfig ice_config = CreateIceConfig(TimeDelta::Seconds(1), GATHER_ONCE);
  ice_config.surface_ice_candidates_on_ice_transport_type_changed = true;
  CreateChannels(ice_config, ice_config);
  ASSERT_THAT(
      DefaultWait().Until([&] { return ep1_ch1()->selected_connection(); },
                          Ne(nullptr)),
      IsRtcOk());
  ASSERT_THAT(
      DefaultWait().Until([&] { return ep2_ch1()->selected_connection(); },
                          Ne(nullptr)),
      IsRtcOk());
  // Loosen the candidate filter at ep1.
  ep1->allocator()->SetCandidateFilter(CF_ALL);
  // Wait for a period for any potential surfacing of new candidates.
  time_controller_.AdvanceTime(kDefaultTimeout);
  EXPECT_TRUE(ep1_ch1()->selected_connection()->local_candidate().is_relay());

  // Loosen the candidate filter at ep2.
  ep2->allocator()->SetCandidateFilter(CF_ALL);
  EXPECT_TRUE(ep2_ch1()->selected_connection()->local_candidate().is_relay());
  DestroyChannels();
}

// Test that when the candidate filter is updated to be more restrictive,
// candidates that 1) have already been gathered and signaled 2) but no longer
// match the filter, are not removed.
TEST_F(P2PTransportChannelTest,
       RestrictingCandidateFilterDoesNotRemoveRegatheredCandidates) {
  ConfigureEndpoints(
      OPEN, OPEN,
      kDefaultPortAllocatorFlags | PORTALLOCATOR_ENABLE_SHARED_SOCKET,
      kDefaultPortAllocatorFlags | PORTALLOCATOR_ENABLE_SHARED_SOCKET);
  auto* ep1 = GetEndpoint(0);
  auto* ep2 = GetEndpoint(1);
  ep1->allocator()->SetCandidateFilter(CF_ALL);
  ep2->allocator()->SetCandidateFilter(CF_ALL);
  // Enable continual gathering and also resurfacing gathered candidates upon
  // the candidate filter changed in the ICE configuration.
  IceConfig ice_config =
      CreateIceConfig(TimeDelta::Seconds(1), GATHER_CONTINUALLY);
  ice_config.surface_ice_candidates_on_ice_transport_type_changed = true;
  // Pause candidates so we can gather all types of candidates. See
  // P2PTransportChannel::OnConnectionStateChange, where we would stop the
  // gathering when we have a strongly connected candidate pair.
  PauseCandidates(0);
  PauseCandidates(1);
  CreateChannels(ice_config, ice_config);

  // We have gathered host, srflx and relay candidates.
  EXPECT_THAT(DefaultWait().Until(
                  [&] { return ep1->saved_candidates().size(); }, Eq(3u)),
              IsRtcOk());
  ResumeCandidates(0);
  ResumeCandidates(1);
  ASSERT_TRUE(DefaultWait().Until([&] {
    return ep1_ch1()->selected_connection() != nullptr &&
           ep1_ch1()->selected_connection()->local_candidate().is_local() &&
           ep2_ch1()->selected_connection() != nullptr &&
           ep1_ch1()->selected_connection()->remote_candidate().is_local();
  }));
  ASSERT_THAT(
      DefaultWait().Until([&] { return ep2_ch1()->selected_connection(); },
                          Ne(nullptr)),
      IsRtcOk());
  // Test that we have a host-host candidate pair selected and the number of
  // candidates signaled to the remote peer stays the same.
  auto test_invariants = [this]() {
    EXPECT_TRUE(ep1_ch1()->selected_connection()->local_candidate().is_local());
    EXPECT_TRUE(
        ep1_ch1()->selected_connection()->remote_candidate().is_local());
    EXPECT_THAT(ep2_ch1()->remote_candidates(), SizeIs(3));
  };

  test_invariants();

  // Set a more restrictive candidate filter at ep1.
  ep1->allocator()->SetCandidateFilter(CF_HOST | CF_REFLEXIVE);
  time_controller_.AdvanceTime(kDefaultTimeout);
  test_invariants();

  ep1->allocator()->SetCandidateFilter(CF_HOST);
  time_controller_.AdvanceTime(kDefaultTimeout);
  test_invariants();

  ep1->allocator()->SetCandidateFilter(CF_NONE);
  time_controller_.AdvanceTime(kDefaultTimeout);
  test_invariants();
  DestroyChannels();
}

// Verify that things break unless
// - both parties use the surface_ice_candidates_on_ice_transport_type_changed
// - both parties loosen candidate filter at the same time (approx.).
//
// i.e surface_ice_candidates_on_ice_transport_type_changed requires
// coordination outside of webrtc to function properly.
TEST_F(P2PTransportChannelTest, SurfaceRequiresCoordination) {
  env_ = CreateTestEnvironment(
      {.field_trials =
           "WebRTC-IceFieldTrials/skip_relay_to_non_relay_connections:true/",
       .time = &time_controller_});

  ConfigureEndpoints(
      OPEN, OPEN,
      kDefaultPortAllocatorFlags | PORTALLOCATOR_ENABLE_SHARED_SOCKET,
      kDefaultPortAllocatorFlags | PORTALLOCATOR_ENABLE_SHARED_SOCKET);
  auto* ep1 = GetEndpoint(0);
  auto* ep2 = GetEndpoint(1);
  ep1->allocator()->SetCandidateFilter(CF_RELAY);
  ep2->allocator()->SetCandidateFilter(CF_ALL);
  // Enable continual gathering and also resurfacing gathered candidates upon
  // the candidate filter changed in the ICE configuration.
  IceConfig ice_config =
      CreateIceConfig(TimeDelta::Seconds(1), GATHER_CONTINUALLY);
  ice_config.surface_ice_candidates_on_ice_transport_type_changed = true;
  // Pause candidates gathering so we can gather all types of candidates. See
  // P2PTransportChannel::OnConnectionStateChange, where we would stop the
  // gathering when we have a strongly connected candidate pair.
  PauseCandidates(0);
  PauseCandidates(1);
  CreateChannels(ice_config, ice_config);

  // On the caller we only have relay,
  // on the callee we have host, srflx and relay.
  EXPECT_THAT(DefaultWait().Until(
                  [&] { return ep1->saved_candidates().size(); }, Eq(1u)),
              IsRtcOk());
  EXPECT_THAT(DefaultWait().Until(
                  [&] { return ep2->saved_candidates().size(); }, Eq(3u)),
              IsRtcOk());

  ResumeCandidates(0);
  ResumeCandidates(1);
  ASSERT_TRUE(DefaultWait().Until([&] {
    return ep1_ch1()->selected_connection() != nullptr &&
           ep1_ch1()->selected_connection()->local_candidate().is_relay() &&
           ep2_ch1()->selected_connection() != nullptr &&
           ep1_ch1()->selected_connection()->remote_candidate().is_relay();
  }));
  ASSERT_THAT(
      DefaultWait().Until([&] { return ep2_ch1()->selected_connection(); },
                          Ne(nullptr)),
      IsRtcOk());

  // Wait until the callee discards it's candidates
  // since they don't manage to connect.
  time_controller_.AdvanceTime(TimeDelta::Millis(300000));

  // And then loosen caller candidate filter.
  ep1->allocator()->SetCandidateFilter(CF_ALL);

  time_controller_.AdvanceTime(kDefaultTimeout);

  // No p2p connection will be made, it will remain on relay.
  EXPECT_TRUE(ep1_ch1()->selected_connection() != nullptr &&
              ep1_ch1()->selected_connection()->local_candidate().is_relay() &&
              ep2_ch1()->selected_connection() != nullptr &&
              ep1_ch1()->selected_connection()->remote_candidate().is_relay());

  DestroyChannels();
}

TEST_F(P2PTransportChannelPingTest, TestInitialSelectDampening0) {
  time_controller_.AdvanceTime(TimeDelta::Seconds(1));
  env_ = CreateTestEnvironment(
      {.field_trials = "WebRTC-IceFieldTrials/initial_select_dampening:0/",
       .time = &time_controller_});

  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "test channel", 1, &pa);
  PrepareChannel(&ch);
  ch.SetIceConfig(ch.config());
  ch.MaybeStartGathering();

  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 100));
  Connection* conn1 = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  ASSERT_TRUE(conn1 != nullptr);
  EXPECT_EQ(nullptr, ch.selected_connection());
  conn1->ReceivedPingResponse(kLowRtt, "id");  // Becomes writable and receiving
  // It shall not be selected until 0ms has passed....i.e it should be connected
  // directly.
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.selected_connection(); }, Eq(conn1)),
      IsRtcOk());
}

TEST_F(P2PTransportChannelPingTest, TestInitialSelectDampening) {
  time_controller_.AdvanceTime(TimeDelta::Seconds(1));
  env_ = CreateTestEnvironment(
      {.field_trials = "WebRTC-IceFieldTrials/initial_select_dampening:100/",
       .time = &time_controller_});

  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "test channel", 1, &pa);
  PrepareChannel(&ch);
  ch.SetIceConfig(ch.config());
  ch.MaybeStartGathering();

  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 100));
  Connection* conn1 = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  ASSERT_TRUE(conn1 != nullptr);
  EXPECT_EQ(nullptr, ch.selected_connection());
  conn1->ReceivedPingResponse(kLowRtt, "id");  // Becomes writable and receiving
  // It shall not be selected until 100ms has passed.
  (void)DefaultWait().Until([&] { return conn1 == ch.selected_connection(); });
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.selected_connection(); }, Eq(conn1)),
      IsRtcOk());
}

TEST_F(P2PTransportChannelPingTest, TestInitialSelectDampeningPingReceived) {
  time_controller_.AdvanceTime(TimeDelta::Seconds(1));
  env_ = CreateTestEnvironment(
      {.field_trials =
           "WebRTC-IceFieldTrials/initial_select_dampening_ping_received:100/",
       .time = &time_controller_});

  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "test channel", 1, &pa);
  PrepareChannel(&ch);
  ch.SetIceConfig(ch.config());
  ch.MaybeStartGathering();

  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 100));
  Connection* conn1 = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  ASSERT_TRUE(conn1 != nullptr);
  EXPECT_EQ(nullptr, ch.selected_connection());
  conn1->ReceivedPingResponse(kLowRtt, "id");  // Becomes writable and receiving
  conn1->ReceivedPing("id1");                  //
  // It shall not be selected until 100ms has passed.
  (void)DefaultWait().Until([&] { return conn1 == ch.selected_connection(); });
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.selected_connection(); }, Eq(conn1)),
      IsRtcOk());
}

TEST_F(P2PTransportChannelPingTest, TestInitialSelectDampeningBoth) {
  time_controller_.AdvanceTime(TimeDelta::Seconds(1));
  env_ = CreateTestEnvironment({.field_trials =
                                    "WebRTC-IceFieldTrials/"
                                    "initial_select_dampening:100,initial_"
                                    "select_dampening_ping_received:"
                                    "50/",
                                .time = &time_controller_});

  FakePortAllocator pa(env_, ss());
  P2PTransportChannel ch(env_, "test channel", 1, &pa);
  PrepareChannel(&ch);
  ch.SetIceConfig(ch.config());
  ch.MaybeStartGathering();

  ch.AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 100));
  Connection* conn1 = WaitForConnectionTo(&ch, "1.1.1.1", 1);
  ASSERT_TRUE(conn1 != nullptr);
  EXPECT_EQ(nullptr, ch.selected_connection());
  conn1->ReceivedPingResponse(kLowRtt, "id");  // Becomes writable and receiving
  // It shall not be selected until 100ms has passed....but only wait ~50 now.
  (void)DefaultWait().Until([&] { return conn1 == ch.selected_connection(); });
  // Now receiving ping and new timeout should kick in.
  conn1->ReceivedPing("id1");  //
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch.selected_connection(); }, Eq(conn1)),
      IsRtcOk());
}

TEST(P2PTransportChannelIceControllerTest, InjectIceController) {
  const Environment env = CreateTestEnvironment();
  std::unique_ptr<SocketServer> socket_server = CreateDefaultSocketServer();
  test::RunLoop main_thread(socket_server.get());
  MockIceControllerFactory factory;
  FakePortAllocator pa(env, socket_server.get());
  EXPECT_CALL(factory, RecordIceControllerCreated()).Times(1);
  IceTransportInit init(env);
  init.set_port_allocator(&pa);
  init.set_ice_controller_factory(&factory);
  auto dummy =
      P2PTransportChannel::Create("transport_name",
                                  /* component= */ 77, std::move(init));
}

TEST(P2PTransportChannel, InjectActiveIceController) {
  const Environment env = CreateTestEnvironment();
  std::unique_ptr<SocketServer> socket_server = CreateDefaultSocketServer();
  test::RunLoop main_thread(socket_server.get());
  MockActiveIceControllerFactory factory;
  FakePortAllocator pa(env, socket_server.get());
  EXPECT_CALL(factory, RecordActiveIceControllerCreated()).Times(1);
  IceTransportInit init(env);
  init.set_port_allocator(&pa);
  init.set_active_ice_controller_factory(&factory);
  auto dummy =
      P2PTransportChannel::Create("transport_name",
                                  /* component= */ 77, std::move(init));
}

class ForgetLearnedStateController : public BasicIceController {
 public:
  explicit ForgetLearnedStateController(const IceControllerFactoryArgs& args)
      : BasicIceController(args) {}

  SwitchResult SortAndSwitchConnection(IceSwitchReason reason) override {
    auto result = BasicIceController::SortAndSwitchConnection(reason);
    if (forget_connnection_) {
      result.connections_to_forget_state_on.push_back(forget_connnection_);
      forget_connnection_ = nullptr;
    }
    result.recheck_event.emplace(IceSwitchReason::ICE_CONTROLLER_RECHECK, 100);
    return result;
  }

  void ForgetThisConnectionNextTimeSortAndSwitchConnectionIsCalled(
      Connection* con) {
    forget_connnection_ = con;
  }

 private:
  Connection* forget_connnection_ = nullptr;
};

class ForgetLearnedStateControllerFactory
    : public IceControllerFactoryInterface {
 public:
  std::unique_ptr<IceControllerInterface> Create(
      const IceControllerFactoryArgs& args) override {
    auto controller = std::make_unique<ForgetLearnedStateController>(args);
    // Keep a pointer to allow modifying calls.
    // Must not be used after the p2ptransportchannel has been destructed.
    controller_ = controller.get();
    return controller;
  }
  ~ForgetLearnedStateControllerFactory() override = default;

  ForgetLearnedStateController* controller_;
};

TEST_F(P2PTransportChannelPingTest, TestForgetLearnedState) {
  ForgetLearnedStateControllerFactory factory;
  FakePortAllocator pa(env_, ss());
  IceTransportInit init(env_);
  init.set_port_allocator(&pa);
  init.set_ice_controller_factory(&factory);
  auto ch =
      P2PTransportChannel::Create("ping sufficiently", 1, std::move(init));

  PrepareChannel(ch.get());
  ch->MaybeStartGathering();
  ch->AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 1));
  ch->AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "2.2.2.2", 2, 2));

  Connection* conn1 = WaitForConnectionTo(ch.get(), "1.1.1.1", 1);
  Connection* conn2 = WaitForConnectionTo(ch.get(), "2.2.2.2", 2);
  ASSERT_TRUE(conn1 != nullptr);
  ASSERT_TRUE(conn2 != nullptr);

  // Wait for conn1 to be selected.
  conn1->ReceivedPingResponse(kLowRtt, "id");
  EXPECT_THAT(
      DefaultWait().Until([&] { return ch->selected_connection(); }, Eq(conn1)),
      IsRtcOk());

  conn2->ReceivedPingResponse(kLowRtt, "id");
  EXPECT_TRUE(conn2->writable());

  // Now let the ice controller signal to P2PTransportChannel that it
  // should Forget conn2.
  factory.controller_
      ->ForgetThisConnectionNextTimeSortAndSwitchConnectionIsCalled(conn2);

  // We don't have a mock Connection, so verify this by checking that it
  // is no longer writable.
  EXPECT_TRUE(MediumWait().Until([&] { return !conn2->writable(); }));
}

TEST_F(P2PTransportChannelTest, DisableDnsLookupsWithTransportPolicyRelay) {
  ConfigureEndpoints(OPEN, OPEN, kDefaultPortAllocatorFlags,
                     kDefaultPortAllocatorFlags);
  auto* ep1 = GetEndpoint(0);
  ep1->allocator()->SetCandidateFilter(CF_RELAY);

  std::unique_ptr<MockAsyncDnsResolver> mock_async_resolver =
      std::make_unique<MockAsyncDnsResolver>();
  // This test expects resolution to not be started.
  EXPECT_CALL(*mock_async_resolver, Start(_, _)).Times(0);

  MockAsyncDnsResolverFactory mock_async_resolver_factory;
  ON_CALL(mock_async_resolver_factory, Create())
      .WillByDefault(
          [&mock_async_resolver]() { return std::move(mock_async_resolver); });

  ep1->set_async_dns_resolver_factory(&mock_async_resolver_factory);

  CreateChannels();

  ep1_ch1()->AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "hostname.test", 1, 100));

  DestroyChannels();
}

TEST_F(P2PTransportChannelTest, DisableDnsLookupsWithTransportPolicyNone) {
  ConfigureEndpoints(OPEN, OPEN, kDefaultPortAllocatorFlags,
                     kDefaultPortAllocatorFlags);
  auto* ep1 = GetEndpoint(0);
  ep1->allocator()->SetCandidateFilter(CF_NONE);

  std::unique_ptr<MockAsyncDnsResolver> mock_async_resolver =
      std::make_unique<MockAsyncDnsResolver>();
  // This test expects resolution to not be started.
  EXPECT_CALL(*mock_async_resolver, Start(_, _)).Times(0);

  MockAsyncDnsResolverFactory mock_async_resolver_factory;
  ON_CALL(mock_async_resolver_factory, Create())
      .WillByDefault(
          [&mock_async_resolver]() { return std::move(mock_async_resolver); });

  ep1->set_async_dns_resolver_factory(&mock_async_resolver_factory);

  CreateChannels();

  ep1_ch1()->AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "hostname.test", 1, 100));

  DestroyChannels();
}

TEST_F(P2PTransportChannelTest, EnableDnsLookupsWithTransportPolicyNoHost) {
  ConfigureEndpoints(OPEN, OPEN, kDefaultPortAllocatorFlags,
                     kDefaultPortAllocatorFlags);
  auto* ep1 = GetEndpoint(0);
  ep1->allocator()->SetCandidateFilter(CF_ALL & ~CF_HOST);

  std::unique_ptr<MockAsyncDnsResolver> mock_async_resolver =
      std::make_unique<MockAsyncDnsResolver>();
  bool lookup_started = false;
  EXPECT_CALL(*mock_async_resolver, Start(_, _))
      .WillOnce(Assign(&lookup_started, true));

  MockAsyncDnsResolverFactory mock_async_resolver_factory;
  EXPECT_CALL(mock_async_resolver_factory, Create())
      .WillOnce(
          [&mock_async_resolver]() { return std::move(mock_async_resolver); });

  ep1->set_async_dns_resolver_factory(&mock_async_resolver_factory);

  CreateChannels();

  ep1_ch1()->AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "hostname.test", 1, 100));

  EXPECT_TRUE(lookup_started);

  DestroyChannels();
}

constexpr absl::string_view kTestAddresses[] = {
    "127.0.0.1",
    "10.0.0.3",
    "1.1.1.1",
    "::1",
    "fd00:4860:4860::8844",
    "2001:4860:4860::8888",
};

class LocalNetworkAccessPermissionTest
    : public P2PTransportChannelPingTest,
      public ::testing::WithParamInterface<
          std::tuple<absl::string_view, LnaFakeResult>> {};

TEST_P(LocalNetworkAccessPermissionTest, LiteralAddresses) {
  const auto [address, lna_fake_result] = GetParam();
  FakePortAllocator pa(env_, ss());
  FakeLocalNetworkAccessPermissionFactory lna_permission_factory(
      lna_fake_result);

  IceTransportInit init(env_);
  init.set_port_allocator(&pa);
  init.set_lna_permission_factory(&lna_permission_factory);

  auto ch = P2PTransportChannel::Create("foo", 1, std::move(init));
  PrepareChannel(ch.get());
  ch->MaybeStartGathering();

  ch->AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, address, 5000, 1));

  ASSERT_THAT(
      DefaultWait().Until(
          [&] { return ch->PermissionQueriesOutstandingForTesting(); }, Eq(0)),
      IsRtcOk());
  if (lna_fake_result == LnaFakeResult::kPermissionNotNeeded ||
      lna_fake_result == LnaFakeResult::kPermissionGranted) {
    EXPECT_EQ(1u, ch->remote_candidates().size());
  } else {
    EXPECT_EQ(0u, ch->remote_candidates().size());
  }
}

TEST_P(LocalNetworkAccessPermissionTest, UnresolvedAddresses) {
  const auto [address, lna_fake_result] = GetParam();
  FakePortAllocator pa(env_, ss());
  FakeLocalNetworkAccessPermissionFactory lna_permission_factory(
      lna_fake_result);

  ResolverFactoryFixture resolver_fixture;
  resolver_fixture.SetAddressToReturn({address, 5000});

  IceTransportInit init(env_);
  init.set_port_allocator(&pa);
  init.set_lna_permission_factory(&lna_permission_factory);
  init.set_async_dns_resolver_factory(&resolver_fixture);

  auto ch = P2PTransportChannel::Create("foo", 1, std::move(init));
  PrepareChannel(ch.get());
  ch->MaybeStartGathering();

  ch->AddRemoteCandidate(
      CreateUdpCandidate(IceCandidateType::kHost, "fake.test", 5000, 1));

  ASSERT_THAT(
      DefaultWait().Until(
          [&] { return ch->PermissionQueriesOutstandingForTesting(); }, Eq(0)),
      IsRtcOk());
  if (lna_fake_result == LnaFakeResult::kPermissionNotNeeded ||
      lna_fake_result == LnaFakeResult::kPermissionGranted) {
    EXPECT_EQ(1u, ch->remote_candidates().size());
  } else {
    EXPECT_EQ(0u, ch->remote_candidates().size());
  }
}

INSTANTIATE_TEST_SUITE_P(
    All,
    LocalNetworkAccessPermissionTest,
    ::testing::Combine(::testing::ValuesIn(kTestAddresses),
                       ::testing::Values(LnaFakeResult::kPermissionNotNeeded,
                                         LnaFakeResult::kPermissionGranted,
                                         LnaFakeResult::kPermissionDenied)));

class GatherAfterConnectedTest : public P2PTransportChannelTest,
                                 public WithParamInterface<bool> {};

INSTANTIATE_TEST_SUITE_P(All, GatherAfterConnectedTest, Values(true, false));

TEST_P(GatherAfterConnectedTest, GatherAfterConnected) {
  const bool stop_gather_on_strongly_connected = GetParam();
  const std::string field_trial =
      std::string("WebRTC-IceFieldTrials/stop_gather_on_strongly_connected:") +
      (stop_gather_on_strongly_connected ? "true/" : "false/");

  env_ = CreateTestEnvironment(
      {.field_trials = field_trial, .time = &time_controller_});
  // Use local + relay
  constexpr uint32_t flags =
      kDefaultPortAllocatorFlags | PORTALLOCATOR_ENABLE_SHARED_SOCKET |
      PORTALLOCATOR_DISABLE_STUN | PORTALLOCATOR_DISABLE_TCP;
  ConfigureEndpoints(OPEN, OPEN, flags, flags);
  auto* ep1 = GetEndpoint(0);
  auto* ep2 = GetEndpoint(1);
  ep1->allocator()->SetCandidateFilter(CF_ALL);
  ep2->allocator()->SetCandidateFilter(CF_ALL);

  // Use step delay 3s which is long enough for
  // connection to be established before managing to gather relay candidates.
  int delay = 3000;
  SetAllocationStepDelay(0, delay);
  SetAllocationStepDelay(1, delay);
  IceConfig ice_config =
      CreateIceConfig(TimeDelta::Seconds(1), GATHER_CONTINUALLY);
  CreateChannels(ice_config, ice_config);

  PauseCandidates(0);
  PauseCandidates(1);

  // We have gathered host candidates but not relay.
  ASSERT_TRUE(DefaultWait().Until([&] {
    return ep1->saved_candidates().size() == 1u &&
           ep2->saved_candidates().size() == 1u;
  }));

  ResumeCandidates(0);
  ResumeCandidates(1);

  PauseCandidates(0);
  PauseCandidates(1);

  ASSERT_TRUE(DefaultWait().Until([&] {
    return ep1_ch1()->remote_candidates().size() == 1 &&
           ep2_ch1()->remote_candidates().size() == 1;
  }));

  ASSERT_TRUE(DefaultWait().Until([&] {
    return ep1_ch1()->selected_connection() && ep2_ch1()->selected_connection();
  }));

  time_controller_.AdvanceTime(TimeDelta::Millis(10 * delay));

  if (stop_gather_on_strongly_connected) {
    // The relay candidates gathered has not been propagated to channel.
    EXPECT_EQ(ep1->saved_candidates().size(), 0u);
    EXPECT_EQ(ep2->saved_candidates().size(), 0u);
  } else {
    // The relay candidates gathered has been propagated to channel.
    EXPECT_EQ(ep1->saved_candidates().size(), 1u);
    EXPECT_EQ(ep2->saved_candidates().size(), 1u);
  }
}

TEST_P(GatherAfterConnectedTest, GatherAfterConnectedMultiHomed) {
  const bool stop_gather_on_strongly_connected = GetParam();
  const std::string field_trial =
      std::string("WebRTC-IceFieldTrials/stop_gather_on_strongly_connected:") +
      (stop_gather_on_strongly_connected ? "true/" : "false/");

  env_ = CreateTestEnvironment(
      {.field_trials = field_trial, .time = &time_controller_});

  // Use local + relay
  constexpr uint32_t flags =
      kDefaultPortAllocatorFlags | PORTALLOCATOR_ENABLE_SHARED_SOCKET |
      PORTALLOCATOR_DISABLE_STUN | PORTALLOCATOR_DISABLE_TCP;
  AddAddress(0, kAlternateAddrs[0]);
  ConfigureEndpoints(OPEN, OPEN, flags, flags);
  auto* ep1 = GetEndpoint(0);
  auto* ep2 = GetEndpoint(1);
  ep1->allocator()->SetCandidateFilter(CF_ALL);
  ep2->allocator()->SetCandidateFilter(CF_ALL);

  // Use step delay 3s which is long enough for
  // connection to be established before managing to gather relay candidates.
  int delay = 3000;
  SetAllocationStepDelay(0, delay);
  SetAllocationStepDelay(1, delay);
  IceConfig ice_config =
      CreateIceConfig(TimeDelta::Seconds(1), GATHER_CONTINUALLY);
  CreateChannels(ice_config, ice_config);

  PauseCandidates(0);
  PauseCandidates(1);

  // We have gathered host candidates but not relay.
  ASSERT_TRUE(DefaultWait().Until([&] {
    return ep1->saved_candidates().size() == 2u &&
           ep2->saved_candidates().size() == 1u;
  }));

  ResumeCandidates(0);
  ResumeCandidates(1);

  PauseCandidates(0);
  PauseCandidates(1);

  ASSERT_TRUE(DefaultWait().Until([&] {
    return ep1_ch1()->remote_candidates().size() == 1 &&
           ep2_ch1()->remote_candidates().size() == 2;
  }));

  ASSERT_TRUE(DefaultWait().Until([&] {
    return ep1_ch1()->selected_connection() && ep2_ch1()->selected_connection();
  }));

  time_controller_.AdvanceTime(TimeDelta::Millis(10 * delay));

  if (stop_gather_on_strongly_connected) {
    // The relay candidates gathered has not been propagated to channel.
    EXPECT_EQ(ep1->saved_candidates().size(), 0u);
    EXPECT_EQ(ep2->saved_candidates().size(), 0u);
  } else {
    // The relay candidates gathered has been propagated.
    EXPECT_EQ(ep1->saved_candidates().size(), 2u);
    EXPECT_EQ(ep2->saved_candidates().size(), 1u);
  }
}

// Tests no candidates are generated with old ice ufrag/passwd after an ice
// restart even if continual gathering is enabled.
TEST_F(P2PTransportChannelTest, TestIceNoOldCandidatesAfterIceRestart) {
  AddAddress(0, kAlternateAddrs[0]);
  ConfigureEndpoints(OPEN, OPEN, kDefaultPortAllocatorFlags,
                     kDefaultPortAllocatorFlags);

  // gathers continually.
  IceConfig config = CreateIceConfig(TimeDelta::Seconds(1), GATHER_CONTINUALLY);
  CreateChannels(config, config);

  EXPECT_TRUE(DefaultWait().Until(
      [&] { return CheckConnected(ep1_ch1(), ep2_ch1()); }));

  PauseCandidates(0);

  ep1_ch1()->SetIceParameters(kIceParams[3]);
  ep1_ch1()->MaybeStartGathering();

  EXPECT_THAT(
      DefaultWait().Until(
          [&] { return GetEndpoint(0)->saved_candidates().size(); }, Gt(0)),
      IsRtcOk());

  for (const auto& cd : GetEndpoint(0)->saved_candidates()) {
    EXPECT_EQ(cd.candidate.username(), kIceUfrag[3]);
  }

  DestroyChannels();
}

class P2PTransportChannelTestDtlsInStun : public P2PTransportChannelTestBase {
 public:
  P2PTransportChannelTestDtlsInStun() : P2PTransportChannelTestBase() {
    // DTLS server hello done message as test data.
    std::vector<uint8_t> dtls_server_hello = {
        0x16, 0xfe, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x0c, 0x0e, 0x00, 0x00, 0x00, 0x00,
        0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    pending_packet_.SetData(dtls_server_hello);
  }

 protected:
  void Run(bool ep1_support, bool ep2_support) {
    CreatePortAllocators();
    IceConfig ep1_config;
    ep1_config.dtls_handshake_in_stun = ep1_support;
    IceConfig ep2_config;
    ep2_config.dtls_handshake_in_stun = ep2_support;
    CreateChannels(ep1_config, ep2_config);
    if (ep1_support) {
      ep1_ch1()->SetDtlsStunPiggybackCallbacks(DtlsStunPiggybackCallbacks(
          [&](auto type) { return data_to_piggyback_func(type); },
          [&](auto data, auto ack) { piggyback_data_received(data, ack); }));
    }
    if (ep2_support) {
      ep2_ch1()->SetDtlsStunPiggybackCallbacks(DtlsStunPiggybackCallbacks(
          [&](auto type) { return data_to_piggyback_func(type); },
          [&](auto data, auto ack) { piggyback_data_received(data, ack); }));
    }
    EXPECT_TRUE(DefaultWait().Until(
        [&] { return CheckConnected(ep1_ch1(), ep2_ch1()); }));
    DestroyChannels();
  }

  std::pair<std::optional<absl::string_view>,
            std::optional<std::vector<uint32_t>>>
  data_to_piggyback_func(StunMessageType type) {
    return make_pair(absl::string_view(pending_packet_), std::nullopt);
  }

  void piggyback_data_received(std::optional<std::span<uint8_t>> data,
                               std::optional<std::vector<uint32_t>> ack) {}

  Buffer pending_packet_;
};

TEST_F(P2PTransportChannelTestDtlsInStun, NotSupportedByEither) {
  Run(false, false);
}

TEST_F(P2PTransportChannelTestDtlsInStun, SupportedByClient) {
  Run(true, false);
}

TEST_F(P2PTransportChannelTestDtlsInStun, SupportedByServer) {
  Run(false, true);
}

TEST_F(P2PTransportChannelTestDtlsInStun, SupportedByBoth) {
  Run(true, true);
}

class P2PTransportChannelRegatheringTest : public P2PTransportChannelPingTest {
 public:
  void SetupChannel(IceConfig config) {
    // Use a fixed environment for the allocator and channel.
    env_ = std::make_unique<Environment>(
        CreateTestEnvironment({.time = time_controller_.GetClock()}));
    allocator_ = std::make_unique<FakePortAllocator>(*env_, ss());
    channel_ = std::make_unique<P2PTransportChannel>(*env_, "regather", 1,
                                                     allocator_.get());
    PrepareChannel(channel_.get());
    channel_->SetIceConfig(config);
    channel_->SetIceRole(ICEROLE_CONTROLLING);
    channel_->SetIceParameters(kIceParams[0]);
    channel_->SetRemoteIceParameters(kIceParams[1]);
    channel_->MaybeStartGathering();

    // Create a connection to trigger pinging which starts the regathering
    // timer. We need to make sure the connection is pingable.
    channel_->AddRemoteCandidate(
        CreateUdpCandidate(IceCandidateType::kHost, "1.1.1.1", 1, 1));
    connection_ = WaitForConnectionTo(channel_.get(), "1.1.1.1", 1);
    EXPECT_TRUE(connection_ != nullptr);

    channel_->allocator_session()->SubscribeIceRegathering(
        this, [this](PortAllocatorSession*, IceRegatheringReason reason) {
          regathering_counts_[reason]++;
        });
  }

  int GetRegatheringCount(IceRegatheringReason reason) {
    return regathering_counts_[reason];
  }

  std::unique_ptr<Environment> env_;
  std::unique_ptr<FakePortAllocator> allocator_;
  std::unique_ptr<P2PTransportChannel> channel_;
  Connection* connection_ = nullptr;
  std::map<IceRegatheringReason, int> regathering_counts_;
};

TEST_F(P2PTransportChannelRegatheringTest,
       IceRegatheringDoesNotOccurIfSessionNotCleared) {
  IceConfig config;
  config.regather_on_failed_networks_interval = TimeDelta::Millis(2000);
  SetupChannel(config);

  // Session is not cleared by default.
  // Expect no regathering in the last 10s.
  // We use WaitUntil with a condition that is always false to wait for timeout,
  // but we expect it to fail (return false).
  EXPECT_FALSE(DefaultWait().Until([&] { return false; }));
  EXPECT_EQ(0, GetRegatheringCount(IceRegatheringReason::NETWORK_FAILURE));
}

TEST_F(P2PTransportChannelRegatheringTest, IceRegatheringRepeatsAsScheduled) {
  IceConfig config;
  config.regather_on_failed_networks_interval = TimeDelta::Millis(2000);
  SetupChannel(config);

  channel_->allocator_session()->ClearGettingPorts();

  // Expect no regathering immediately (wait < 2000ms).
  EXPECT_FALSE(Wait(TimeDelta::Millis(1900)).Until([&] { return false; }));
  EXPECT_EQ(0, GetRegatheringCount(IceRegatheringReason::NETWORK_FAILURE));

  // Expect regathering to happen once after 2s.
  EXPECT_TRUE(DefaultWait().Until([&] {
    return GetRegatheringCount(IceRegatheringReason::NETWORK_FAILURE) == 1;
  }));

  // Expect regathering to happen for another 5 times in 11s with 2s interval.
  // Total 6.
  EXPECT_TRUE(Wait(TimeDelta::Seconds(11)).Until([&] {
    return GetRegatheringCount(IceRegatheringReason::NETWORK_FAILURE) == 6;
  }));
}

TEST_F(P2PTransportChannelRegatheringTest,
       ScheduleOfIceRegatheringOnFailedNetworksCanBeReplaced) {
  IceConfig config;
  config.regather_on_failed_networks_interval = TimeDelta::Millis(2000);
  SetupChannel(config);

  channel_->allocator_session()->ClearGettingPorts();

  config.regather_on_failed_networks_interval = TimeDelta::Millis(5000);
  channel_->SetIceConfig(config);

  // Expect no regathering from the previous schedule (Wait 3s, previous was
  // 2s). Since we reset the schedule, it should restart counting 5s from now.
  EXPECT_FALSE(MediumWait().Until([&] { return false; }));
  EXPECT_EQ(0, GetRegatheringCount(IceRegatheringReason::NETWORK_FAILURE));

  // Expect regathering to happen twice in the last 11s (total 14s) with 5s
  // interval. First at 5s, second at 10s.
  EXPECT_TRUE(Wait(TimeDelta::Seconds(8)).Until([&] {
    return GetRegatheringCount(IceRegatheringReason::NETWORK_FAILURE) == 2;
  }));
}

}  // namespace
}  // namespace webrtc
