/*
 *  Copyright 2009 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "p2p/client/basic_port_allocator.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/async_dns_resolver.h"
#include "api/candidate.h"
#include "api/environment/environment.h"
#include "api/field_trials.h"
#include "api/test/rtc_error_matchers.h"
#include "api/transport/enums.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "p2p/base/basic_packet_socket_factory.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/port.h"
#include "p2p/base/port_allocator.h"
#include "p2p/base/port_interface.h"
#include "p2p/base/stun_port.h"
#include "p2p/base/stun_request.h"
#include "p2p/test/nat_server.h"
#include "p2p/test/nat_socket_factory.h"
#include "p2p/test/nat_types.h"
#include "p2p/test/stun_server.h"
#include "p2p/test/test_stun_server.h"
#include "p2p/test/test_turn_server.h"
#include "rtc_base/checks.h"
#include "rtc_base/fake_mdns_responder.h"
#include "rtc_base/fake_network.h"
#include "rtc_base/firewall_socket_server.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/logging.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/net_helpers.h"
#include "rtc_base/net_test_helpers.h"
#include "rtc_base/network.h"
#include "rtc_base/network_constants.h"
#include "rtc_base/socket.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/socket_factory.h"
#include "rtc_base/thread.h"
#include "rtc_base/virtual_socket_server.h"
#include "system_wrappers/include/metrics.h"
#include "test/create_test_environment.h"
#include "test/create_test_field_trials.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/time_controller/simulated_time_controller.h"
#include "test/wait_until.h"

#define MAYBE_SKIP_IPV4                        \
  if (!::webrtc::HasIPv4Enabled()) {           \
    RTC_LOG(LS_INFO) << "No IPv4... skipping"; \
    return;                                    \
  }

namespace webrtc {
namespace {

using ::testing::Contains;
using ::testing::ElementsAre;
using ::testing::Eq;
using ::testing::IsTrue;
using ::testing::Not;

const SocketAddress kAnyAddr("0.0.0.0", 0);
const SocketAddress kClientAddr("11.11.11.11", 0);
const SocketAddress kClientAddr2("22.22.22.22", 0);
const SocketAddress kLoopbackAddr("127.0.0.1", 0);
const SocketAddress kPrivateAddr("192.168.1.11", 0);
const SocketAddress kPrivateAddr2("192.168.1.12", 0);
const SocketAddress kClientIPv6Addr("2401:fa00:4:1000:be30:5bff:fee5:c3", 0);
const SocketAddress kClientIPv6Addr2("2401:fa00:4:2000:be30:5bff:fee5:c3", 0);
const SocketAddress kClientIPv6Addr3("2401:fa00:4:3000:be30:5bff:fee5:c3", 0);
const SocketAddress kClientIPv6Addr4("2401:fa00:4:4000:be30:5bff:fee5:c3", 0);
const SocketAddress kClientIPv6Addr5("2401:fa00:4:5000:be30:5bff:fee5:c3", 0);
const SocketAddress kNatUdpAddr("77.77.77.77", webrtc::NAT_SERVER_UDP_PORT);
const SocketAddress kNatTcpAddr("77.77.77.77", webrtc::NAT_SERVER_TCP_PORT);
const SocketAddress kRemoteClientAddr("22.22.22.22", 0);
const SocketAddress kStunAddr("99.99.99.1", webrtc::STUN_SERVER_PORT);
const SocketAddress kTurnUdpIntAddr("99.99.99.4", 3478);
const SocketAddress kTurnUdpIntIPv6Addr("2402:fb00:4:1000:be30:5bff:fee5:c3",
                                        3479);
const SocketAddress kTurnTcpIntAddr("99.99.99.5", 3478);
const SocketAddress kTurnTcpIntIPv6Addr("2402:fb00:4:2000:be30:5bff:fee5:c3",
                                        3479);
const SocketAddress kTurnUdpExtAddr("99.99.99.6", 0);

// Minimum and maximum port for port range tests.
const int kMinPort = 10000;
const int kMaxPort = 10099;

// Based on ICE_UFRAG_LENGTH
const char kIceUfrag0[] = "UF00";
// Based on ICE_PWD_LENGTH
const char kIcePwd0[] = "TESTICEPWD00000000000000";

const char kContentName[] = "test content";

constexpr TimeDelta kDefaultAllocationTimeout = TimeDelta::Millis(3000);
const char kTurnUsername[] = "test";
const char kTurnPassword[] = "test";

// STUN timeout (with all retries) is webrtc::STUN_TOTAL_TIMEOUT.
// Add some margin of error for slow bots.
constexpr TimeDelta kStunTimeout = TimeDelta::Millis(STUN_TOTAL_TIMEOUT);

void CheckStunKeepaliveIntervalOfAllReadyPorts(
    const PortAllocatorSession* allocator_session,
    int expected) {
  auto ready_ports = allocator_session->ReadyPorts();
  for (const auto* port : ready_ports) {
    if (port->Type() == IceCandidateType::kSrflx ||
        (port->Type() == IceCandidateType::kHost &&
         port->GetProtocol() == PROTO_UDP)) {
      EXPECT_EQ(static_cast<const UDPPort*>(port)->stun_keepalive_delay(),
                TimeDelta::Millis(expected));
    }
  }
}

class FakeAsyncDnsResolverResult : public AsyncDnsResolverResult {
 public:
  FakeAsyncDnsResolverResult(const SocketAddress& addr, int error)
      : addr_(addr), error_(error) {}

  bool GetResolvedAddress(int family, SocketAddress* addr) const override {
    if (error_ != 0)
      return false;
    if (family != AF_UNSPEC && addr_.family() != family)
      return false;
    *addr = addr_;
    return true;
  }

  int GetError() const override { return error_; }

 private:
  SocketAddress addr_;
  int error_;
};

class FakeAsyncDnsResolver : public AsyncDnsResolverInterface {
 public:
  void Start(const SocketAddress& addr,
             absl::AnyInvocable<void()> callback) override {
    StartInternal(addr, AF_UNSPEC, std::move(callback));
  }

  void Start(const SocketAddress& addr,
             int family,
             absl::AnyInvocable<void()> callback) override {
    StartInternal(addr, family, std::move(callback));
  }

  const AsyncDnsResolverResult& result() const override {
    RTC_DCHECK(result_);
    return *result_;
  }

 private:
  void StartInternal(const SocketAddress& addr,
                     int family,
                     absl::AnyInvocable<void()> callback) {
    SocketAddress resolved = addr;
    int error = 0;
    if (addr.hostname() == "localhost") {
      if (family == AF_INET6) {
        resolved.SetIP("::1");
      } else {
        resolved.SetIP("127.0.0.1");
      }
    } else {
      error = -1;
    }
    result_ = std::make_unique<FakeAsyncDnsResolverResult>(resolved, error);
    Thread::Current()->PostTask(std::move(callback));
  }

  std::unique_ptr<FakeAsyncDnsResolverResult> result_;
};

class FakePacketSocketFactory : public BasicPacketSocketFactory {
 public:
  explicit FakePacketSocketFactory(SocketFactory* socket_factory)
      : BasicPacketSocketFactory(socket_factory) {}

  std::unique_ptr<AsyncDnsResolverInterface> CreateAsyncDnsResolver() override {
    return std::make_unique<FakeAsyncDnsResolver>();
  }
};

class BasicPortAllocatorTest : public ::testing::Test {
 public:
  BasicPortAllocatorTest()
      : vss_(new VirtualSocketServer()),
        fss_(new FirewallSocketServer(vss_.get())),
        time_controller_(Timestamp::Zero(), fss_.get()),
        env_(CreateTestEnvironment({.time = &time_controller_})),
        socket_factory_(fss_.get()),
        thread_(time_controller_.GetMainThread()),
        // Note that the NAT is not used by default. ResetWithStunServerAndNat
        // must be called.
        nat_factory_(vss_.get(), kNatUdpAddr, kNatTcpAddr),
        nat_socket_factory_(new BasicPacketSocketFactory(&nat_factory_)),
        stun_server_(TestStunServer::Create(env_, kStunAddr, *fss_, *thread_)),
        turn_server_(env_,
                     thread_,
                     fss_.get(),
                     kTurnUdpIntAddr,
                     kTurnUdpExtAddr),
        network_manager_(thread_),
        candidate_allocation_done_(false),
        waiter_({.timeout = kDefaultAllocationTimeout,
                 .clock = &time_controller_}) {
    allocator_.emplace(env_, &network_manager_, &socket_factory_);
    allocator_->SetConfiguration({kStunAddr}, {}, 0, NO_PRUNE, nullptr);

    allocator_->Initialize();
    allocator_->set_step_delay(kMinimumStepDelay);
    metrics::Reset();
  }

  void AddInterface(const SocketAddress& addr) {
    network_manager_.AddInterface(addr);
  }
  void AddInterface(const SocketAddress& addr, absl::string_view if_name) {
    network_manager_.AddInterface(addr, if_name);
  }
  void AddInterface(const SocketAddress& addr,
                    absl::string_view if_name,
                    AdapterType type) {
    network_manager_.AddInterface(addr, if_name, type);
  }
  // The default source address is the public address that STUN server will
  // observe when the endpoint is sitting on the public internet and the local
  // port is bound to the "any" address. Intended for simulating the situation
  // that client binds the "any" address, and that's also the address returned
  // by getsockname/GetLocalAddress, so that the client can learn the actual
  // local address only from the STUN response.
  void AddInterfaceAsDefaultSourceAddresss(const SocketAddress& addr) {
    AddInterface(addr);
    // When a binding comes from the any address, the `addr` will be used as the
    // srflx address.
    vss_->SetDefaultSourceAddress(addr.ipaddr());
  }
  void RemoveInterface(const SocketAddress& addr) {
    network_manager_.RemoveInterface(addr);
  }
  bool SetPortRange(int min_port, int max_port) {
    return allocator_->SetPortRange(min_port, max_port);
  }
  // Endpoint is on the public network. No STUN or TURN.
  void ResetWithNoServersOrNat() {
    allocator_.emplace(env_, &network_manager_, &socket_factory_);
    allocator_->Initialize();
    allocator_->set_step_delay(kMinimumStepDelay);
  }
  // Endpoint is behind a NAT, with STUN specified.
  void ResetWithStunServerAndNat(const SocketAddress& stun_server) {
    ResetWithStunServer(stun_server, true);
  }
  // Endpoint is on the public network, with STUN specified.
  void ResetWithStunServerNoNat(const SocketAddress& stun_server) {
    ResetWithStunServer(stun_server, false);
  }
  // Endpoint is on the public network, with TURN specified.
  void ResetWithTurnServersNoNat(const SocketAddress& udp_turn,
                                 const SocketAddress& tcp_turn) {
    ResetWithNoServersOrNat();
    AddTurnServers(udp_turn, tcp_turn);
  }

  RelayServerConfig CreateTurnServers(const SocketAddress& udp_turn,
                                      const SocketAddress& tcp_turn) {
    RelayServerConfig turn_server;
    RelayCredentials credentials(kTurnUsername, kTurnPassword);
    turn_server.credentials = credentials;

    if (!udp_turn.IsNil()) {
      turn_server.ports.push_back(ProtocolAddress(udp_turn, PROTO_UDP));
    }
    if (!tcp_turn.IsNil()) {
      turn_server.ports.push_back(ProtocolAddress(tcp_turn, PROTO_TCP));
    }
    return turn_server;
  }

  void AddTurnServers(const SocketAddress& udp_turn,
                      const SocketAddress& tcp_turn) {
    RelayServerConfig turn_server = CreateTurnServers(udp_turn, tcp_turn);
    allocator_->AddTurnServerForTesting(turn_server);
  }

  bool CreateSession(int component) {
    session_ = CreateSession("session", component);
    if (!session_) {
      return false;
    }
    return true;
  }

  bool CreateSession(int component, absl::string_view content_name) {
    session_ = CreateSession("session", content_name, component);
    if (!session_) {
      return false;
    }
    return true;
  }

  std::unique_ptr<PortAllocatorSession> CreateSession(absl::string_view sid,
                                                      int component) {
    return CreateSession(sid, kContentName, component);
  }

  std::unique_ptr<PortAllocatorSession> CreateSession(
      absl::string_view sid,
      absl::string_view content_name,
      int component) {
    return CreateSession(sid, content_name, component, kIceUfrag0, kIcePwd0);
  }

  std::unique_ptr<PortAllocatorSession> CreateSession(
      absl::string_view sid,
      absl::string_view content_name,
      int component,
      absl::string_view ice_ufrag,
      absl::string_view ice_pwd) {
    std::unique_ptr<PortAllocatorSession> session =
        allocator_->CreateSession(content_name, component, ice_ufrag, ice_pwd);
    session->SubscribePortReady(
        this, [this](PortAllocatorSession* session, PortInterface* port) {
          OnPortReady(session, port);
        });
    session->SubscribePortsPruned(
        this, [this](PortAllocatorSession* session,
                     const std::vector<PortInterface*>& ports) {
          OnPortsPruned(session, ports);
        });
    session->SubscribeCandidatesReady(
        this, [this](PortAllocatorSession* session,
                     const std::vector<Candidate>& candidate) {
          OnCandidatesReady(session, candidate);
        });
    session->SubscribeCandidatesRemoved(
        this, [this](PortAllocatorSession* session,
                     const std::vector<Candidate>& removed_candidates) {
          OnCandidatesRemoved(session, removed_candidates);
        });
    session->SubscribeCandidatesAllocationDone(
        this, [this](PortAllocatorSession* session) {
          OnCandidatesAllocationDone(session);
        });
    return session;
  }

  // Return true if the addresses are the same, or the port is 0 in `pattern`
  // (acting as a wildcard) and the IPs are the same.
  // Even with a wildcard port, the port of the address should be nonzero if
  // the IP is nonzero.
  static bool AddressMatch(const SocketAddress& address,
                           const SocketAddress& pattern) {
    return address.ipaddr() == pattern.ipaddr() &&
           ((pattern.port() == 0 &&
             (address.port() != 0 || IPIsAny(address.ipaddr()))) ||
            (pattern.port() != 0 && address.port() == pattern.port()));
  }

  // Returns the number of ports that have matching type, protocol and
  // address.
  static int CountPorts(const std::vector<PortInterface*>& ports,
                        IceCandidateType type,
                        ProtocolType protocol,
                        const SocketAddress& client_addr) {
    return absl::c_count_if(
        ports, [type, protocol, client_addr](PortInterface* port) {
          return port->Type() == type && port->GetProtocol() == protocol &&
                 port->Network()->GetBestIP() == client_addr.ipaddr();
        });
  }

  // Find a candidate and return it.
  static bool FindCandidate(const std::vector<Candidate>& candidates,
                            IceCandidateType type,
                            absl::string_view proto,
                            const SocketAddress& addr,
                            Candidate* found) {
    auto it =
        absl::c_find_if(candidates, [type, proto, addr](const Candidate& c) {
          return c.type() == type && c.protocol() == proto &&
                 AddressMatch(c.address(), addr);
        });
    if (it != candidates.end() && found) {
      *found = *it;
    }
    return it != candidates.end();
  }

  // Convenience method to call FindCandidate with no return.
  static bool HasCandidate(const std::vector<Candidate>& candidates,
                           IceCandidateType type,
                           absl::string_view proto,
                           const SocketAddress& addr) {
    return FindCandidate(candidates, type, proto, addr, nullptr);
  }

  // Version of HasCandidate that also takes a related address.
  static bool HasCandidateWithRelatedAddr(
      const std::vector<Candidate>& candidates,
      IceCandidateType type,
      absl::string_view proto,
      const SocketAddress& addr,
      const SocketAddress& related_addr) {
    return absl::c_any_of(
        candidates, [type, proto, addr, related_addr](const Candidate& c) {
          return c.type() == type && c.protocol() == proto &&
                 AddressMatch(c.address(), addr) &&
                 AddressMatch(c.related_address(), related_addr);
        });
  }

  static bool CheckPort(const SocketAddress& addr, int min_port, int max_port) {
    return (addr.port() >= min_port && addr.port() <= max_port);
  }

  static bool HasNetwork(const std::vector<const Network*>& networks,
                         const Network& to_be_found) {
    auto it = absl::c_find_if(networks, [&to_be_found](const Network* network) {
      return network->description() == to_be_found.description() &&
             network->name() == to_be_found.name() &&
             network->prefix() == to_be_found.prefix();
    });
    return it != networks.end();
  }

  void OnCandidatesAllocationDone(PortAllocatorSession* session) {
    // We should only get this callback once, except in the mux test where
    // we have multiple port allocation sessions.
    if (session == session_.get()) {
      ASSERT_FALSE(candidate_allocation_done_);
      candidate_allocation_done_ = true;
    }
    EXPECT_TRUE(session->CandidatesAllocationDone());
  }

  // Check if all ports allocated have send-buffer size `expected`. If
  // `expected` == -1, check if GetOptions returns SOCKET_ERROR.
  void CheckSendBufferSizesOfAllPorts(int expected) {
    std::vector<PortInterface*>::iterator it;
    for (it = ports_.begin(); it < ports_.end(); ++it) {
      int send_buffer_size;
      if (expected == -1) {
        EXPECT_EQ(SOCKET_ERROR,
                  (*it)->GetOption(Socket::OPT_SNDBUF, &send_buffer_size));
      } else {
        EXPECT_EQ(0, (*it)->GetOption(Socket::OPT_SNDBUF, &send_buffer_size));
        ASSERT_EQ(expected, send_buffer_size);
      }
    }
  }

  VirtualSocketServer* virtual_socket_server() { return vss_.get(); }

 protected:
  BasicPortAllocator& allocator() { return *allocator_; }

  void OnPortReady(PortAllocatorSession* ses, PortInterface* port) {
    RTC_LOG(LS_INFO) << "OnPortReady: " << port->ToString();
    ports_.push_back(port);
    // Make sure the new port is added to ReadyPorts.
    auto ready_ports = ses->ReadyPorts();
    EXPECT_THAT(ready_ports, Contains(port));
  }
  void OnPortsPruned(PortAllocatorSession* ses,
                     const std::vector<PortInterface*>& pruned_ports) {
    RTC_LOG(LS_INFO) << "Number of ports pruned: " << pruned_ports.size();
    auto ready_ports = ses->ReadyPorts();
    auto new_end = ports_.end();
    for (PortInterface* port : pruned_ports) {
      new_end = std::remove(ports_.begin(), new_end, port);
      // Make sure the pruned port is not in ReadyPorts.
      EXPECT_THAT(ready_ports, Not(Contains(port)));
    }
    ports_.erase(new_end, ports_.end());
  }

  void OnCandidatesReady(PortAllocatorSession* ses,
                         const std::vector<Candidate>& candidates) {
    for (const Candidate& candidate : candidates) {
      RTC_LOG(LS_INFO) << "OnCandidatesReady: " << candidate.ToString();
      // Sanity check that the ICE component is set.
      EXPECT_EQ(ICE_CANDIDATE_COMPONENT_RTP, candidate.component());
      candidates_.push_back(candidate);
    }
    // Make sure the new candidates are added to Candidates.
    auto ses_candidates = ses->ReadyCandidates();
    for (const Candidate& candidate : candidates) {
      EXPECT_THAT(ses_candidates, Contains(candidate));
    }
  }

  void OnCandidatesRemoved(PortAllocatorSession* session,
                           const std::vector<Candidate>& removed_candidates) {
    std::erase_if(candidates_, [removed_candidates](Candidate& candidate) {
      return absl::c_any_of(
          removed_candidates, [&candidate](const Candidate& removed_candidate) {
            return candidate.MatchesForRemoval(removed_candidate);
          });
    });
  }

  bool HasRelayAddress(const ProtocolAddress& proto_addr) {
    for (size_t i = 0; i < allocator_->turn_servers().size(); ++i) {
      RelayServerConfig server_config = allocator_->turn_servers()[i];
      PortList::const_iterator relay_port;
      for (relay_port = server_config.ports.begin();
           relay_port != server_config.ports.end(); ++relay_port) {
        if (proto_addr.address == relay_port->address &&
            proto_addr.proto == relay_port->proto)
          return true;
      }
    }
    return false;
  }

  void ResetWithStunServer(const SocketAddress& stun_server, bool with_nat) {
    if (with_nat) {
      nat_server_ = std::make_unique<NATServer>(
          env_, NAT_OPEN_CONE, *thread_, vss_.get(), kNatUdpAddr, kNatTcpAddr,
          *thread_, vss_.get(), SocketAddress(kNatUdpAddr.ipaddr(), 0));
    } else {
      nat_socket_factory_ =
          std::make_unique<BasicPacketSocketFactory>(fss_.get());
    }

    ServerAddresses stun_servers;
    if (!stun_server.IsNil()) {
      stun_servers.insert(stun_server);
    }
    allocator_.emplace(env_, &network_manager_, nat_socket_factory_.get());
    allocator_->SetConfiguration(stun_servers, {}, 0, NO_PRUNE, nullptr);

    allocator_->Initialize();
    allocator_->set_step_delay(kMinimumStepDelay);
  }

  std::unique_ptr<VirtualSocketServer> vss_;
  std::unique_ptr<FirewallSocketServer> fss_;
  GlobalSimulatedTimeController time_controller_;
  const Environment env_;
  BasicPacketSocketFactory socket_factory_;
  webrtc::Thread* const thread_;
  std::unique_ptr<NATServer> nat_server_;
  NATSocketFactory nat_factory_;
  std::unique_ptr<BasicPacketSocketFactory> nat_socket_factory_;
  TestStunServer::StunServerPtr stun_server_;
  TestTurnServer turn_server_;
  FakeNetworkManager network_manager_;
  std::optional<BasicPortAllocator> allocator_;
  std::unique_ptr<PortAllocatorSession> session_;
  std::vector<PortInterface*> ports_;
  std::vector<Candidate> candidates_;
  bool candidate_allocation_done_;
  Waiter waiter_;

 public:
  // This function starts the port/address gathering and check the existence of
  // candidates as specified. When `expect_stun_candidate` is true,
  // `stun_candidate_addr` carries the expected reflective address, which is
  // also the related address for TURN candidate if it is expected. Otherwise,
  // it should be ignore.
  void CheckDisableAdapterEnumeration(
      uint32_t total_ports,
      const IPAddress& host_candidate_addr,
      const IPAddress& stun_candidate_addr,
      const IPAddress& relay_candidate_udp_transport_addr,
      const IPAddress& relay_candidate_tcp_transport_addr) {
    network_manager_.set_default_local_addresses(kPrivateAddr.ipaddr(),
                                                 IPAddress());
    if (!session_) {
      ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
    }
    session_->set_flags(session_->flags() |
                        PORTALLOCATOR_DISABLE_ADAPTER_ENUMERATION |
                        PORTALLOCATOR_ENABLE_SHARED_SOCKET);
    allocator().set_allow_tcp_listen(false);
    session_->StartGettingPorts();
    EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));

    uint32_t total_candidates = 0;
    if (!host_candidate_addr.IsNil()) {
      EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kHost, "udp",
                               SocketAddress(kPrivateAddr.ipaddr(), 0)));
      ++total_candidates;
    }
    if (!stun_candidate_addr.IsNil()) {
      SocketAddress related_address(host_candidate_addr, 0);
      if (host_candidate_addr.IsNil()) {
        related_address.SetIP(GetAnyIP(stun_candidate_addr.family()));
      }
      EXPECT_TRUE(HasCandidateWithRelatedAddr(
          candidates_, IceCandidateType::kSrflx, "udp",
          SocketAddress(stun_candidate_addr, 0), related_address));
      ++total_candidates;
    }
    if (!relay_candidate_udp_transport_addr.IsNil()) {
      EXPECT_TRUE(HasCandidateWithRelatedAddr(
          candidates_, IceCandidateType::kRelay, "udp",
          SocketAddress(relay_candidate_udp_transport_addr, 0),
          SocketAddress(stun_candidate_addr, 0)));
      ++total_candidates;
    }
    if (!relay_candidate_tcp_transport_addr.IsNil()) {
      EXPECT_TRUE(HasCandidateWithRelatedAddr(
          candidates_, IceCandidateType::kRelay, "udp",
          SocketAddress(relay_candidate_tcp_transport_addr, 0),
          SocketAddress(stun_candidate_addr, 0)));
      ++total_candidates;
    }

    EXPECT_EQ(total_candidates, candidates_.size());
    EXPECT_EQ(total_ports, ports_.size());
  }

  void TestIPv6TurnPortPrunesIPv4TurnPort() {
    turn_server_.AddInternalSocket(kTurnUdpIntIPv6Addr, PROTO_UDP);
    // Add two IP addresses on the same interface.
    AddInterface(kClientAddr, "net1");
    AddInterface(kClientIPv6Addr, "net1");
    allocator_.emplace(env_, &network_manager_, &socket_factory_);
    allocator_->Initialize();
    allocator_->SetConfiguration(allocator_->stun_servers(),
                                 allocator_->turn_servers(), 0,
                                 PRUNE_BASED_ON_PRIORITY);
    AddTurnServers(kTurnUdpIntIPv6Addr, SocketAddress());
    AddTurnServers(kTurnUdpIntAddr, SocketAddress());

    allocator_->set_step_delay(kMinimumStepDelay);
    allocator_->set_flags(
        allocator().flags() | PORTALLOCATOR_ENABLE_SHARED_SOCKET |
        PORTALLOCATOR_ENABLE_IPV6 | PORTALLOCATOR_DISABLE_TCP);

    ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
    session_->StartGettingPorts();
    EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
    // Three ports (one IPv4 STUN, one IPv6 STUN and one TURN) will be ready.
    EXPECT_EQ(3U, session_->ReadyPorts().size());
    EXPECT_EQ(3U, ports_.size());
    EXPECT_EQ(
        1, CountPorts(ports_, IceCandidateType::kHost, PROTO_UDP, kClientAddr));
    EXPECT_EQ(1, CountPorts(ports_, IceCandidateType::kHost, PROTO_UDP,
                            kClientIPv6Addr));
    EXPECT_EQ(1, CountPorts(ports_, IceCandidateType::kRelay, PROTO_UDP,
                            kClientIPv6Addr));
    EXPECT_EQ(0, CountPorts(ports_, IceCandidateType::kRelay, PROTO_UDP,
                            kClientAddr));

    // Now that we remove candidates when a TURN port is pruned, there will be
    // exactly 3 candidates in both `candidates_` and `ready_candidates`.
    EXPECT_EQ(3U, candidates_.size());
    const std::vector<Candidate>& ready_candidates =
        session_->ReadyCandidates();
    EXPECT_EQ(3U, ready_candidates.size());
    EXPECT_TRUE(HasCandidate(ready_candidates, IceCandidateType::kHost, "udp",
                             kClientAddr));
    EXPECT_TRUE(HasCandidate(ready_candidates, IceCandidateType::kRelay, "udp",
                             SocketAddress(kTurnUdpExtAddr.ipaddr(), 0)));
  }

  void TestTurnPortPrunesWithUdpAndTcpPorts(PortPrunePolicy prune_policy,
                                            bool tcp_pruned) {
    turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
    AddInterface(kClientAddr);
    allocator_.emplace(env_, &network_manager_, &socket_factory_);
    allocator_->Initialize();
    allocator_->SetConfiguration(allocator_->stun_servers(),
                                 allocator_->turn_servers(), 0, prune_policy);
    AddTurnServers(kTurnUdpIntAddr, kTurnTcpIntAddr);
    allocator_->set_step_delay(kMinimumStepDelay);
    allocator_->set_flags(allocator().flags() |
                          PORTALLOCATOR_ENABLE_SHARED_SOCKET |
                          PORTALLOCATOR_DISABLE_TCP);

    ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
    session_->StartGettingPorts();
    EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
    // Only 2 ports (one STUN and one TURN) are actually being used.
    EXPECT_EQ(2U, session_->ReadyPorts().size());
    // We have verified that each port, when it is added to `ports_`, it is
    // found in `ready_ports`, and when it is pruned, it is not found in
    // `ready_ports`, so we only need to verify the content in one of them.
    EXPECT_EQ(2U, ports_.size());
    EXPECT_EQ(
        1, CountPorts(ports_, IceCandidateType::kHost, PROTO_UDP, kClientAddr));
    int num_udp_ports = tcp_pruned ? 1 : 0;
    EXPECT_EQ(num_udp_ports, CountPorts(ports_, IceCandidateType::kRelay,
                                        PROTO_UDP, kClientAddr));
    EXPECT_EQ(1 - num_udp_ports, CountPorts(ports_, IceCandidateType::kRelay,
                                            PROTO_TCP, kClientAddr));

    // Now that we remove candidates when a TURN port is pruned, `candidates_`
    // should only contains two candidates regardless whether the TCP TURN port
    // is created before or after the UDP turn port.
    EXPECT_EQ(2U, candidates_.size());
    // There will only be 2 candidates in `ready_candidates` because it only
    // includes the candidates in the ready ports.
    const std::vector<Candidate>& ready_candidates =
        session_->ReadyCandidates();
    EXPECT_EQ(2U, ready_candidates.size());
    EXPECT_TRUE(HasCandidate(ready_candidates, IceCandidateType::kHost, "udp",
                             kClientAddr));

    // The external candidate is always udp.
    EXPECT_TRUE(HasCandidate(ready_candidates, IceCandidateType::kRelay, "udp",
                             SocketAddress(kTurnUdpExtAddr.ipaddr(), 0)));
  }

  void TestEachInterfaceHasItsOwnTurnPorts() {
    turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
    turn_server_.AddInternalSocket(kTurnUdpIntIPv6Addr, PROTO_UDP);
    turn_server_.AddInternalSocket(kTurnTcpIntIPv6Addr, PROTO_TCP);
    // Add two interfaces both having IPv4 and IPv6 addresses.
    AddInterface(kClientAddr, "net1", ADAPTER_TYPE_WIFI);
    AddInterface(kClientIPv6Addr, "net1", ADAPTER_TYPE_WIFI);
    AddInterface(kClientAddr2, "net2", ADAPTER_TYPE_CELLULAR);
    AddInterface(kClientIPv6Addr2, "net2", ADAPTER_TYPE_CELLULAR);
    allocator_.emplace(env_, &network_manager_, &socket_factory_);
    allocator_->Initialize();
    allocator_->SetConfiguration(allocator_->stun_servers(),
                                 allocator_->turn_servers(), 0,
                                 PRUNE_BASED_ON_PRIORITY);
    // Have both UDP/TCP and IPv4/IPv6 TURN ports.
    AddTurnServers(kTurnUdpIntAddr, kTurnTcpIntAddr);
    AddTurnServers(kTurnUdpIntIPv6Addr, kTurnTcpIntIPv6Addr);

    allocator_->set_step_delay(kMinimumStepDelay);
    allocator_->set_flags(
        allocator().flags() | PORTALLOCATOR_ENABLE_SHARED_SOCKET |
        PORTALLOCATOR_ENABLE_IPV6 | PORTALLOCATOR_ENABLE_IPV6_ON_WIFI);
    ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
    session_->StartGettingPorts();
    EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
    // 10 ports (4 STUN and 1 TURN ports on each interface) will be ready to
    // use.
    EXPECT_EQ(10U, session_->ReadyPorts().size());
    EXPECT_EQ(10U, ports_.size());
    EXPECT_EQ(
        1, CountPorts(ports_, IceCandidateType::kHost, PROTO_UDP, kClientAddr));
    EXPECT_EQ(1, CountPorts(ports_, IceCandidateType::kHost, PROTO_UDP,
                            kClientAddr2));
    EXPECT_EQ(1, CountPorts(ports_, IceCandidateType::kHost, PROTO_UDP,
                            kClientIPv6Addr));
    EXPECT_EQ(1, CountPorts(ports_, IceCandidateType::kHost, PROTO_UDP,
                            kClientIPv6Addr2));
    EXPECT_EQ(
        1, CountPorts(ports_, IceCandidateType::kHost, PROTO_TCP, kClientAddr));
    EXPECT_EQ(1, CountPorts(ports_, IceCandidateType::kHost, PROTO_TCP,
                            kClientAddr2));
    EXPECT_EQ(1, CountPorts(ports_, IceCandidateType::kHost, PROTO_TCP,
                            kClientIPv6Addr));
    EXPECT_EQ(1, CountPorts(ports_, IceCandidateType::kHost, PROTO_TCP,
                            kClientIPv6Addr2));
    EXPECT_EQ(1, CountPorts(ports_, IceCandidateType::kRelay, PROTO_UDP,
                            kClientIPv6Addr));
    EXPECT_EQ(1, CountPorts(ports_, IceCandidateType::kRelay, PROTO_UDP,
                            kClientIPv6Addr2));

    // Now that we remove candidates when TURN ports are pruned, there will be
    // exactly 10 candidates in `candidates_`.
    EXPECT_EQ(10U, candidates_.size());
    const std::vector<Candidate>& ready_candidates =
        session_->ReadyCandidates();
    EXPECT_EQ(10U, ready_candidates.size());
    EXPECT_TRUE(HasCandidate(ready_candidates, IceCandidateType::kHost, "udp",
                             kClientAddr));
    EXPECT_TRUE(HasCandidate(ready_candidates, IceCandidateType::kHost, "udp",
                             kClientAddr2));
    EXPECT_TRUE(HasCandidate(ready_candidates, IceCandidateType::kHost, "udp",
                             kClientIPv6Addr));
    EXPECT_TRUE(HasCandidate(ready_candidates, IceCandidateType::kHost, "udp",
                             kClientIPv6Addr2));
    EXPECT_TRUE(HasCandidate(ready_candidates, IceCandidateType::kHost, "tcp",
                             kClientAddr));
    EXPECT_TRUE(HasCandidate(ready_candidates, IceCandidateType::kHost, "tcp",
                             kClientAddr2));
    EXPECT_TRUE(HasCandidate(ready_candidates, IceCandidateType::kHost, "tcp",
                             kClientIPv6Addr));
    EXPECT_TRUE(HasCandidate(ready_candidates, IceCandidateType::kHost, "tcp",
                             kClientIPv6Addr2));
    EXPECT_TRUE(HasCandidate(ready_candidates, IceCandidateType::kRelay, "udp",
                             SocketAddress(kTurnUdpExtAddr.ipaddr(), 0)));
  }
};

// Tests that we can init the port allocator and create a session.
TEST_F(BasicPortAllocatorTest, TestBasic) {
  EXPECT_EQ(&network_manager_, allocator().network_manager());
  EXPECT_EQ(kStunAddr, *allocator().stun_servers().begin());
  ASSERT_EQ(0u, allocator().turn_servers().size());

  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  EXPECT_FALSE(session_->CandidatesAllocationDone());
}

// Tests that our network filtering works properly.
TEST_F(BasicPortAllocatorTest, TestIgnoreOnlyLoopbackNetworkByDefault) {
  AddInterface(SocketAddress(IPAddress(0x12345600U), 0), "test_eth0",
               ADAPTER_TYPE_ETHERNET);
  AddInterface(SocketAddress(IPAddress(0x12345601U), 0), "test_wlan0",
               ADAPTER_TYPE_WIFI);
  AddInterface(SocketAddress(IPAddress(0x12345602U), 0), "test_cell0",
               ADAPTER_TYPE_CELLULAR);
  AddInterface(SocketAddress(IPAddress(0x12345603U), 0), "test_vpn0",
               ADAPTER_TYPE_VPN);
  AddInterface(SocketAddress(IPAddress(0x12345604U), 0), "test_lo",
               ADAPTER_TYPE_LOOPBACK);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->set_flags(PORTALLOCATOR_DISABLE_STUN | PORTALLOCATOR_DISABLE_RELAY |
                      PORTALLOCATOR_DISABLE_TCP);
  session_->StartGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(4U, candidates_.size());
  for (const Candidate& candidate : candidates_) {
    EXPECT_LT(candidate.address().ip(), 0x12345604U);
  }
}

TEST_F(BasicPortAllocatorTest, TestIgnoreNetworksAccordingToIgnoreMask) {
  AddInterface(SocketAddress(IPAddress(0x12345600U), 0), "test_eth0",
               ADAPTER_TYPE_ETHERNET);
  AddInterface(SocketAddress(IPAddress(0x12345601U), 0), "test_wlan0",
               ADAPTER_TYPE_WIFI);
  AddInterface(SocketAddress(IPAddress(0x12345602U), 0), "test_cell0",
               ADAPTER_TYPE_CELLULAR);
  allocator_->SetNetworkIgnoreMask(ADAPTER_TYPE_ETHERNET |
                                   ADAPTER_TYPE_LOOPBACK | ADAPTER_TYPE_WIFI);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->set_flags(PORTALLOCATOR_DISABLE_STUN | PORTALLOCATOR_DISABLE_RELAY |
                      PORTALLOCATOR_DISABLE_TCP);
  session_->StartGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(1U, candidates_.size());
  EXPECT_EQ(0x12345602U, candidates_[0].address().ip());
}

// Test that when the PORTALLOCATOR_DISABLE_COSTLY_NETWORKS flag is set and
// both Wi-Fi and cell interfaces are available, only Wi-Fi is used.
TEST_F(BasicPortAllocatorTest,
       WifiUsedInsteadOfCellWhenCostlyNetworksDisabled) {
  SocketAddress wifi(IPAddress(0x12345600U), 0);
  SocketAddress cell(IPAddress(0x12345601U), 0);
  AddInterface(wifi, "test_wlan0", ADAPTER_TYPE_WIFI);
  AddInterface(cell, "test_cell0", ADAPTER_TYPE_CELLULAR);
  // Disable all but UDP candidates to make the test simpler.
  allocator().set_flags(
      PORTALLOCATOR_DISABLE_STUN | PORTALLOCATOR_DISABLE_RELAY |
      PORTALLOCATOR_DISABLE_TCP | PORTALLOCATOR_DISABLE_COSTLY_NETWORKS);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  // Should only get one Wi-Fi candidate.
  EXPECT_EQ(1U, candidates_.size());
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kHost, "udp", wifi));
}

// Test that when the PORTALLOCATOR_DISABLE_COSTLY_NETWORKS flag is set and
// both "unknown" and cell interfaces are available, only the unknown are used.
// The unknown interface may be something that ultimately uses Wi-Fi, so we do
// this to be on the safe side.
TEST_F(BasicPortAllocatorTest,
       UnknownInterfaceUsedInsteadOfCellWhenCostlyNetworksDisabled) {
  SocketAddress cell(IPAddress(0x12345601U), 0);
  SocketAddress unknown1(IPAddress(0x12345602U), 0);
  SocketAddress unknown2(IPAddress(0x12345603U), 0);
  AddInterface(cell, "test_cell0", ADAPTER_TYPE_CELLULAR);
  AddInterface(unknown1, "test_unknown0", ADAPTER_TYPE_UNKNOWN);
  AddInterface(unknown2, "test_unknown1", ADAPTER_TYPE_UNKNOWN);
  // Disable all but UDP candidates to make the test simpler.
  allocator().set_flags(
      PORTALLOCATOR_DISABLE_STUN | PORTALLOCATOR_DISABLE_RELAY |
      PORTALLOCATOR_DISABLE_TCP | PORTALLOCATOR_DISABLE_COSTLY_NETWORKS);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  // Should only get two candidates, none of which is cell.
  EXPECT_EQ(2U, candidates_.size());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", unknown1));
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", unknown2));
}

// Test that when the PORTALLOCATOR_DISABLE_COSTLY_NETWORKS flag is set and
// there are a mix of Wi-Fi, "unknown" and cell interfaces, only the Wi-Fi
// interface is used.
TEST_F(BasicPortAllocatorTest,
       WifiUsedInsteadOfUnknownOrCellWhenCostlyNetworksDisabled) {
  SocketAddress wifi(IPAddress(0x12345600U), 0);
  SocketAddress cellular(IPAddress(0x12345601U), 0);
  SocketAddress unknown1(IPAddress(0x12345602U), 0);
  SocketAddress unknown2(IPAddress(0x12345603U), 0);
  AddInterface(wifi, "test_wlan0", ADAPTER_TYPE_WIFI);
  AddInterface(cellular, "test_cell0", ADAPTER_TYPE_CELLULAR);
  AddInterface(unknown1, "test_unknown0", ADAPTER_TYPE_UNKNOWN);
  AddInterface(unknown2, "test_unknown1", ADAPTER_TYPE_UNKNOWN);
  // Disable all but UDP candidates to make the test simpler.
  allocator().set_flags(
      PORTALLOCATOR_DISABLE_STUN | PORTALLOCATOR_DISABLE_RELAY |
      PORTALLOCATOR_DISABLE_TCP | PORTALLOCATOR_DISABLE_COSTLY_NETWORKS);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  // Should only get one Wi-Fi candidate.
  EXPECT_EQ(1U, candidates_.size());
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kHost, "udp", wifi));
}

// Test that if the PORTALLOCATOR_DISABLE_COSTLY_NETWORKS flag is set, but the
// only interface available is cellular, it ends up used anyway. A costly
// connection is always better than no connection.
TEST_F(BasicPortAllocatorTest,
       CellUsedWhenCostlyNetworksDisabledButThereAreNoOtherInterfaces) {
  SocketAddress cellular(IPAddress(0x12345601U), 0);
  AddInterface(cellular, "test_cell0", ADAPTER_TYPE_CELLULAR);
  // Disable all but UDP candidates to make the test simpler.
  allocator().set_flags(
      PORTALLOCATOR_DISABLE_STUN | PORTALLOCATOR_DISABLE_RELAY |
      PORTALLOCATOR_DISABLE_TCP | PORTALLOCATOR_DISABLE_COSTLY_NETWORKS);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  // Make sure we got the cell candidate.
  EXPECT_EQ(1U, candidates_.size());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", cellular));
}

// Test that if both PORTALLOCATOR_DISABLE_COSTLY_NETWORKS is set, and there is
// a WiFi network with link-local IP address and a cellular network, then the
// cellular candidate will still be gathered.
TEST_F(BasicPortAllocatorTest,
       CellNotRemovedWhenCostlyNetworksDisabledAndWifiIsLinkLocal) {
  SocketAddress wifi_link_local("169.254.0.1", 0);
  SocketAddress cellular(IPAddress(0x12345601U), 0);
  AddInterface(wifi_link_local, "test_wlan0", ADAPTER_TYPE_WIFI);
  AddInterface(cellular, "test_cell0", ADAPTER_TYPE_CELLULAR);

  allocator().set_flags(
      PORTALLOCATOR_DISABLE_STUN | PORTALLOCATOR_DISABLE_RELAY |
      PORTALLOCATOR_DISABLE_TCP | PORTALLOCATOR_DISABLE_COSTLY_NETWORKS);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  // Make sure we got both wifi and cell candidates.
  EXPECT_EQ(2U, candidates_.size());
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kHost, "udp",
                           wifi_link_local));
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", cellular));
}

// Test that if both PORTALLOCATOR_DISABLE_COSTLY_NETWORKS is set, and there is
// a WiFi network with link-local IP address, a WiFi network with a normal IP
// address and a cellular network, then the cellular candidate will not be
// gathered.
TEST_F(BasicPortAllocatorTest,
       CellRemovedWhenCostlyNetworksDisabledAndBothWifisPresent) {
  SocketAddress wifi(IPAddress(0x12345600U), 0);
  SocketAddress wifi_link_local("169.254.0.1", 0);
  SocketAddress cellular(IPAddress(0x12345601U), 0);
  AddInterface(wifi, "test_wlan0", ADAPTER_TYPE_WIFI);
  AddInterface(wifi_link_local, "test_wlan1", ADAPTER_TYPE_WIFI);
  AddInterface(cellular, "test_cell0", ADAPTER_TYPE_CELLULAR);

  allocator().set_flags(
      PORTALLOCATOR_DISABLE_STUN | PORTALLOCATOR_DISABLE_RELAY |
      PORTALLOCATOR_DISABLE_TCP | PORTALLOCATOR_DISABLE_COSTLY_NETWORKS);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  // Make sure we got only wifi candidates.
  EXPECT_EQ(2U, candidates_.size());
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kHost, "udp", wifi));
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kHost, "udp",
                           wifi_link_local));
}

// Test that the adapter types of the Ethernet and the VPN can be correctly
// identified so that the Ethernet has a lower network cost than the VPN, and
// the Ethernet is not filtered out if PORTALLOCATOR_DISABLE_COSTLY_NETWORKS is
// set.
TEST_F(BasicPortAllocatorTest,
       EthernetIsNotFilteredOutWhenCostlyNetworksDisabledAndVpnPresent) {
  AddInterface(kClientAddr, "eth0", ADAPTER_TYPE_ETHERNET);
  AddInterface(kClientAddr2, "tap0", ADAPTER_TYPE_VPN);
  allocator().set_flags(PORTALLOCATOR_DISABLE_COSTLY_NETWORKS |
                        PORTALLOCATOR_DISABLE_RELAY |
                        PORTALLOCATOR_DISABLE_TCP);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  ASSERT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  // The VPN tap0 network should be filtered out as a costly network, and we
  // should have a UDP port and a STUN port from the Ethernet eth0.
  ASSERT_EQ(2U, ports_.size());
  EXPECT_EQ(ports_[0]->Network()->name(), "eth0");
  EXPECT_EQ(ports_[1]->Network()->name(), "eth0");
}

// Test that no more than allocator.max_ipv6_networks() IPv6 networks are used
// to gather candidates.
TEST_F(BasicPortAllocatorTest, MaxIpv6NetworksLimitEnforced) {
  // Add three IPv6 network interfaces, but tell the allocator to only use two.
  allocator().set_max_ipv6_networks(2);
  AddInterface(kClientIPv6Addr, "eth0", ADAPTER_TYPE_ETHERNET);
  AddInterface(kClientIPv6Addr2, "eth1", ADAPTER_TYPE_ETHERNET);
  AddInterface(kClientIPv6Addr3, "eth2", ADAPTER_TYPE_ETHERNET);

  // To simplify the test, only gather UDP host candidates.
  allocator().set_flags(PORTALLOCATOR_ENABLE_IPV6 | PORTALLOCATOR_DISABLE_TCP |
                        PORTALLOCATOR_DISABLE_STUN |
                        PORTALLOCATOR_DISABLE_RELAY);

  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(2U, candidates_.size());
  // Ensure the expected two interfaces (eth0 and eth1) were used.
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kHost, "udp",
                           kClientIPv6Addr));
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kHost, "udp",
                           kClientIPv6Addr2));
}

// Ensure that allocator.max_ipv6_networks() doesn't prevent IPv4 networks from
// being used.
TEST_F(BasicPortAllocatorTest, MaxIpv6NetworksLimitDoesNotImpactIpv4Networks) {
  // Set the "max IPv6" limit to 1, adding two IPv6 and two IPv4 networks.
  allocator().set_max_ipv6_networks(1);
  AddInterface(kClientIPv6Addr, "eth0", ADAPTER_TYPE_ETHERNET);
  AddInterface(kClientIPv6Addr2, "eth1", ADAPTER_TYPE_ETHERNET);
  AddInterface(kClientAddr, "eth2", ADAPTER_TYPE_ETHERNET);
  AddInterface(kClientAddr2, "eth3", ADAPTER_TYPE_ETHERNET);

  // To simplify the test, only gather UDP host candidates.
  allocator().set_flags(PORTALLOCATOR_ENABLE_IPV6 | PORTALLOCATOR_DISABLE_TCP |
                        PORTALLOCATOR_DISABLE_STUN |
                        PORTALLOCATOR_DISABLE_RELAY);

  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(3U, candidates_.size());
  // Ensure that only one IPv6 interface was used, but both IPv4 interfaces
  // were used.
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kHost, "udp",
                           kClientIPv6Addr));
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", kClientAddr));
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", kClientAddr2));
}

// Test that we could use loopback interface as host candidate.
TEST_F(BasicPortAllocatorTest, TestLoopbackNetworkInterface) {
  AddInterface(kLoopbackAddr, "test_loopback", ADAPTER_TYPE_LOOPBACK);
  allocator_->SetNetworkIgnoreMask(0);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->set_flags(PORTALLOCATOR_DISABLE_STUN | PORTALLOCATOR_DISABLE_RELAY |
                      PORTALLOCATOR_DISABLE_TCP);
  session_->StartGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(1U, candidates_.size());
}

// Tests that we can get all the desired addresses successfully.
TEST_F(BasicPortAllocatorTest, TestGetAllPortsWithMinimumStepDelay) {
  AddInterface(kClientAddr);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  ASSERT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(3U, candidates_.size());
  EXPECT_EQ(3U, ports_.size());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", kClientAddr));
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kSrflx, "udp", kClientAddr));
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "tcp", kClientAddr));
}

TEST_F(BasicPortAllocatorTest,
       NetworkChangeDuringGatheringDoesNotDuplicateTcp) {
  ResetWithNoServersOrNat();
  AddInterface(kClientAddr);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  ASSERT_THAT(waiter_.Until([&] { return candidates_.size(); }, Eq(1U)),
              IsRtcOk());
  ASSERT_EQ(candidates_.front().protocol(), UDP_PROTOCOL_NAME);
  ASSERT_FALSE(candidate_allocation_done_);

  network_manager_.NotifyNetworksChanged();
  network_manager_.NotifyNetworksChanged();
  ASSERT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(candidates_.size(), 2U);
  EXPECT_EQ(ports_.size(), 2U);
  EXPECT_EQ(CountPorts(ports_, IceCandidateType::kHost, PROTO_TCP, kClientAddr),
            1);
}

TEST_F(BasicPortAllocatorTest,
       NetworkChangeBeforeFirstPhaseDoesNotDuplicateUdp) {
  ResetWithNoServersOrNat();
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  time_controller_.AdvanceTime(TimeDelta::Millis(1000));
  candidate_allocation_done_ = false;

  // Both notifications run DoAllocate() before the sequence created by the
  // first one gets to its UDP phase.
  AddInterface(kClientAddr);
  network_manager_.NotifyNetworksChanged();
  ASSERT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(CountPorts(ports_, IceCandidateType::kHost, PROTO_UDP, kClientAddr),
            1);
  EXPECT_EQ(CountPorts(ports_, IceCandidateType::kHost, PROTO_TCP, kClientAddr),
            1);
}

TEST_F(BasicPortAllocatorTest, ConcurrentSessionsDoNotDuplicatePorts) {
  constexpr int kNumSessions = 10;
  ResetWithNoServersOrNat();
  AddInterface(kClientAddr);

  // Each session subscribes to the shared network manager, whose
  // StartUpdating() signals all sessions. Start each one a task after the
  // previous one so these signals hit the others at every stage of gathering.
  std::vector<std::unique_ptr<PortAllocatorSession>> sessions;
  absl::AnyInvocable<void()> start_next = [&] {
    sessions.push_back(CreateSession("session", ICE_CANDIDATE_COMPONENT_RTP));
    sessions.back()->StartGettingPorts();
    if (sessions.size() < kNumSessions) {
      thread_->PostTask([&] { start_next(); });
    }
  };
  start_next();
  ASSERT_TRUE(waiter_.Until([&] {
    return sessions.size() == kNumSessions &&
           absl::c_all_of(sessions, [](const auto& session) {
             return session->CandidatesAllocationDone();
           });
  }));

  for (const auto& session : sessions) {
    std::vector<PortInterface*> ports = session->ReadyPorts();
    EXPECT_EQ(
        CountPorts(ports, IceCandidateType::kHost, PROTO_UDP, kClientAddr), 1);
    EXPECT_EQ(
        CountPorts(ports, IceCandidateType::kHost, PROTO_TCP, kClientAddr), 1);
  }
}

// Test that when the same network interface is brought down and up, the
// port allocator session will restart a new allocation sequence if
// it is not stopped.
TEST_F(BasicPortAllocatorTest, TestSameNetworkDownAndUpWhenSessionNotStopped) {
  std::string if_name("test_net0");
  AddInterface(kClientAddr, if_name);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  ASSERT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(3U, candidates_.size());
  EXPECT_EQ(3U, ports_.size());
  candidate_allocation_done_ = false;
  candidates_.clear();
  ports_.clear();

  // Disable socket creation to simulate the network interface being down. When
  // no network interfaces are available, BasicPortAllocator will fall back to
  // binding to the "ANY" address, so we need to make sure that fails too.
  fss_->set_tcp_sockets_enabled(false);
  fss_->set_udp_sockets_enabled(false);
  RemoveInterface(kClientAddr);
  time_controller_.AdvanceTime(TimeDelta::Millis(1000));
  EXPECT_EQ(0U, candidates_.size());
  ports_.clear();
  candidate_allocation_done_ = false;

  // When the same interfaces are added again, new candidates/ports should be
  // generated.
  fss_->set_tcp_sockets_enabled(true);
  fss_->set_udp_sockets_enabled(true);
  AddInterface(kClientAddr, if_name);
  ASSERT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(3U, candidates_.size());
  EXPECT_EQ(3U, ports_.size());
}

// Test that when the same network interface is brought down and up, the
// port allocator session will not restart a new allocation sequence if
// it is stopped.
TEST_F(BasicPortAllocatorTest, TestSameNetworkDownAndUpWhenSessionStopped) {
  std::string if_name("test_net0");
  AddInterface(kClientAddr, if_name);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  ASSERT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(3U, candidates_.size());
  EXPECT_EQ(3U, ports_.size());
  session_->StopGettingPorts();
  candidates_.clear();
  ports_.clear();

  RemoveInterface(kClientAddr);
  // Wait one (simulated) second and then verify no new candidates have
  // appeared.
  time_controller_.AdvanceTime(TimeDelta::Millis(1000));
  EXPECT_EQ(0U, candidates_.size());
  EXPECT_EQ(0U, ports_.size());

  // When the same interfaces are added again, new candidates/ports should not
  // be generated because the session has stopped.
  AddInterface(kClientAddr, if_name);
  time_controller_.AdvanceTime(TimeDelta::Millis(1000));
  EXPECT_EQ(0U, candidates_.size());
  EXPECT_EQ(0U, ports_.size());
}

// Similar to the above tests, but tests a situation when sockets can't be
// bound to a network interface, then after a network change event can be.
// Related bug: https://bugs.chromium.org/p/webrtc/issues/detail?id=8256
TEST_F(BasicPortAllocatorTest, CandidatesRegatheredAfterBindingFails) {
  // Only test local ports to simplify test.
  ResetWithNoServersOrNat();
  // Provide a situation where the interface appears to be available, but
  // binding the sockets fails. See bug for description of when this can
  // happen.
  std::string if_name("test_net0");
  AddInterface(kClientAddr, if_name);
  fss_->set_tcp_sockets_enabled(false);
  fss_->set_udp_sockets_enabled(false);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  ASSERT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  // Make sure we actually prevented candidates from being gathered (other than
  // a single TCP active candidate, since that doesn't require creating a
  // socket).
  ASSERT_EQ(1U, candidates_.size());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "tcp", kClientAddr));
  candidate_allocation_done_ = false;

  // Now simulate the interface coming up, with the newfound ability to bind
  // sockets.
  fss_->set_tcp_sockets_enabled(true);
  fss_->set_udp_sockets_enabled(true);
  AddInterface(kClientAddr, if_name);
  ASSERT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  // Should get UDP and TCP candidate.
  ASSERT_EQ(2U, candidates_.size());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", kClientAddr));
  // TODO(deadbeef): This is actually the same active TCP candidate as before.
  // We should extend this test to also verify that a server candidate is
  // gathered.
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "tcp", kClientAddr));
}

// Verify candidates with default step delay of 1sec.
TEST_F(BasicPortAllocatorTest, TestGetAllPortsWithOneSecondStepDelay) {
  AddInterface(kClientAddr);
  allocator_->set_step_delay(kDefaultStepDelay);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  ASSERT_THAT(waiter_.Until([&] { return candidates_.size(); }, Eq(2U)),
              IsRtcOk());
  EXPECT_EQ(2U, ports_.size());
  ASSERT_THAT(waiter_.Until([&] { return candidates_.size(); }, Eq(3U)),
              IsRtcOk());
  EXPECT_EQ(3U, ports_.size());

  ASSERT_THAT(waiter_.Until([&] { return candidates_.size(); }, Eq(3U)),
              IsRtcOk());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "tcp", kClientAddr));
  EXPECT_EQ(3U, ports_.size());
  EXPECT_TRUE(candidate_allocation_done_);
  // If we Stop gathering now, we shouldn't get a second "done" callback.
  session_->StopGettingPorts();
}

TEST_F(BasicPortAllocatorTest, TestSetupVideoRtpPortsWithNormalSendBuffers) {
  AddInterface(kClientAddr);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP, "video"));
  session_->StartGettingPorts();
  ASSERT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(3U, candidates_.size());
  // If we Stop gathering now, we shouldn't get a second "done" callback.
  session_->StopGettingPorts();

  // All ports should have unset send-buffer sizes.
  CheckSendBufferSizesOfAllPorts(-1);
}

// Tests that we can get callback after StopGetAllPorts when called in the
// middle of gathering.
TEST_F(BasicPortAllocatorTest, TestStopGetAllPorts) {
  AddInterface(kClientAddr);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  ASSERT_THAT(waiter_.Until([&] { return candidates_.size(); }, Eq(2U)),
              IsRtcOk());
  EXPECT_EQ(2U, ports_.size());
  session_->StopGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
}

// Test that we restrict client ports appropriately when a port range is set.
// We check the candidates for udp/stun/tcp ports, and the from address
// for relay ports.
TEST_F(BasicPortAllocatorTest, TestGetAllPortsPortRange) {
  AddInterface(kClientAddr);
  // Check that an invalid port range fails.
  EXPECT_FALSE(SetPortRange(kMaxPort, kMinPort));
  // Check that a null port range succeeds.
  EXPECT_TRUE(SetPortRange(0, 0));
  // Check that a valid port range succeeds.
  EXPECT_TRUE(SetPortRange(kMinPort, kMaxPort));
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  ASSERT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(3U, candidates_.size());
  EXPECT_EQ(3U, ports_.size());

  int num_nonrelay_candidates = 0;
  for (const Candidate& candidate : candidates_) {
    // Check the port number for the UDP/STUN/TCP port objects.
    if (!candidate.is_relay()) {
      EXPECT_TRUE(CheckPort(candidate.address(), kMinPort, kMaxPort));
      ++num_nonrelay_candidates;
    }
  }
  EXPECT_EQ(3, num_nonrelay_candidates);
}

// Test that if we have no network adapters, we bind to the ANY address and
// still get non-host candidates.
TEST_F(BasicPortAllocatorTest, TestGetAllPortsNoAdapters) {
  // Default config uses GTURN and no NAT, so replace that with the
  // desired setup (NAT, STUN server, TURN server, UDP/TCP).
  ResetWithStunServerAndNat(kStunAddr);
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
  AddTurnServers(kTurnUdpIntAddr, kTurnTcpIntAddr);
  AddTurnServers(kTurnUdpIntIPv6Addr, kTurnTcpIntIPv6Addr);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(4U, ports_.size());
  EXPECT_EQ(1,
            CountPorts(ports_, IceCandidateType::kSrflx, PROTO_UDP, kAnyAddr));
  EXPECT_EQ(1,
            CountPorts(ports_, IceCandidateType::kHost, PROTO_TCP, kAnyAddr));
  // Two TURN ports, using UDP/TCP for the first hop to the TURN server.
  EXPECT_EQ(1,
            CountPorts(ports_, IceCandidateType::kRelay, PROTO_UDP, kAnyAddr));
  EXPECT_EQ(1,
            CountPorts(ports_, IceCandidateType::kRelay, PROTO_TCP, kAnyAddr));
  // The "any" address port should be in the signaled ready ports, but the host
  // candidate for it is useless and shouldn't be signaled. So we only have
  // STUN/TURN candidates.
  EXPECT_EQ(3U, candidates_.size());
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kSrflx, "udp",
                           SocketAddress(kNatUdpAddr.ipaddr(), 0)));
  // Again, two TURN candidates, using UDP/TCP for the first hop to the TURN
  // server.
  SocketAddress addr(kTurnUdpExtAddr.ipaddr(), 0);
  EXPECT_EQ(2, absl::c_count_if(candidates_, [&](const Candidate& c) {
              return c.is_relay() && c.protocol() == "udp" &&
                     AddressMatch(c.address(), addr);
            }));
}

// Test that when enumeration is disabled, we should not have any ports when
// candidate_filter() is set to CF_RELAY and no relay is specified.
TEST_F(BasicPortAllocatorTest,
       TestDisableAdapterEnumerationWithoutNatRelayTransportOnly) {
  ResetWithStunServerNoNat(kStunAddr);
  allocator().SetCandidateFilter(CF_RELAY);
  // Expect to see no ports and no candidates.
  CheckDisableAdapterEnumeration(0U, IPAddress(), IPAddress(), IPAddress(),
                                 IPAddress());
}

// Test that even with multiple interfaces, the result should still be a single
// default private, one STUN and one TURN candidate since we bind to any address
// (i.e. all 0s).
TEST_F(BasicPortAllocatorTest,
       TestDisableAdapterEnumerationBehindNatMultipleInterfaces) {
  AddInterface(kPrivateAddr);
  AddInterface(kPrivateAddr2);
  ResetWithStunServerAndNat(kStunAddr);
  AddTurnServers(kTurnUdpIntAddr, SocketAddress());

  // Enable IPv6 here. Since the network_manager doesn't have IPv6 default
  // address set and we have no IPv6 STUN server, there should be no IPv6
  // candidates.
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->set_flags(PORTALLOCATOR_ENABLE_IPV6);

  // Expect to see 3 ports for IPv4: HOST/STUN, TURN/UDP and TCP ports, 2 ports
  // for IPv6: HOST, and TCP. Only IPv4 candidates: a default private, STUN and
  // TURN/UDP candidates.
  CheckDisableAdapterEnumeration(5U, kPrivateAddr.ipaddr(),
                                 kNatUdpAddr.ipaddr(), kTurnUdpExtAddr.ipaddr(),
                                 IPAddress());
}

// Test that we should get a default private, STUN, TURN/UDP and TURN/TCP
// candidates when both TURN/UDP and TURN/TCP servers are specified.
TEST_F(BasicPortAllocatorTest, TestDisableAdapterEnumerationBehindNatWithTcp) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
  AddInterface(kPrivateAddr);
  ResetWithStunServerAndNat(kStunAddr);
  AddTurnServers(kTurnUdpIntAddr, kTurnTcpIntAddr);
  // Expect to see 4 ports - STUN, TURN/UDP, TURN/TCP and TCP port. A default
  // private, STUN, TURN/UDP, and TURN/TCP candidates.
  CheckDisableAdapterEnumeration(4U, kPrivateAddr.ipaddr(),
                                 kNatUdpAddr.ipaddr(), kTurnUdpExtAddr.ipaddr(),
                                 kTurnUdpExtAddr.ipaddr());
}

// Test that when adapter enumeration is disabled, for endpoints without
// STUN/TURN specified, a default private candidate is still generated.
TEST_F(BasicPortAllocatorTest,
       TestDisableAdapterEnumerationWithoutNatOrServers) {
  ResetWithNoServersOrNat();
  // Expect to see 2 ports: STUN and TCP ports, one default private candidate.
  CheckDisableAdapterEnumeration(2U, kPrivateAddr.ipaddr(), IPAddress(),
                                 IPAddress(), IPAddress());
}

// Test that when adapter enumeration is disabled, with
// PORTALLOCATOR_DISABLE_LOCALHOST_CANDIDATE specified, for endpoints not behind
// a NAT, there is no local candidate.
TEST_F(BasicPortAllocatorTest,
       TestDisableAdapterEnumerationWithoutNatLocalhostCandidateDisabled) {
  ResetWithStunServerNoNat(kStunAddr);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->set_flags(PORTALLOCATOR_DISABLE_DEFAULT_LOCAL_CANDIDATE);
  // Expect to see 2 ports: STUN and TCP ports, localhost candidate and STUN
  // candidate.
  CheckDisableAdapterEnumeration(2U, IPAddress(), IPAddress(), IPAddress(),
                                 IPAddress());
}

// Test that when adapter enumeration is disabled, with
// PORTALLOCATOR_DISABLE_LOCALHOST_CANDIDATE specified, for endpoints not behind
// a NAT, there is no local candidate. However, this specified default route
// (kClientAddr) which was discovered when sending STUN requests, will become
// the srflx addresses.
TEST_F(BasicPortAllocatorTest,
       TestDisableAdapterEnumerationWithoutNatLocalhostCandDisabledDiffRoute) {
  ResetWithStunServerNoNat(kStunAddr);
  AddInterfaceAsDefaultSourceAddresss(kClientAddr);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->set_flags(PORTALLOCATOR_DISABLE_DEFAULT_LOCAL_CANDIDATE);
  // Expect to see 2 ports: STUN and TCP ports, localhost candidate and STUN
  // candidate.
  CheckDisableAdapterEnumeration(2U, IPAddress(), kClientAddr.ipaddr(),
                                 IPAddress(), IPAddress());
}

// Test that when adapter enumeration is disabled, with
// PORTALLOCATOR_DISABLE_LOCALHOST_CANDIDATE specified, for endpoints behind a
// NAT, there is only one STUN candidate.
TEST_F(BasicPortAllocatorTest,
       TestDisableAdapterEnumerationWithNatLocalhostCandidateDisabled) {
  ResetWithStunServerAndNat(kStunAddr);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->set_flags(PORTALLOCATOR_DISABLE_DEFAULT_LOCAL_CANDIDATE);
  // Expect to see 2 ports: STUN and TCP ports, and single STUN candidate.
  CheckDisableAdapterEnumeration(2U, IPAddress(), kNatUdpAddr.ipaddr(),
                                 IPAddress(), IPAddress());
}

// Test that we disable relay over UDP, and only TCP is used when connecting to
// the relay server.
TEST_F(BasicPortAllocatorTest, TestDisableUdpTurn) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
  AddInterface(kClientAddr);
  ResetWithStunServerAndNat(kStunAddr);
  AddTurnServers(kTurnUdpIntAddr, kTurnTcpIntAddr);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->set_flags(PORTALLOCATOR_DISABLE_UDP_RELAY |
                      PORTALLOCATOR_DISABLE_UDP | PORTALLOCATOR_DISABLE_STUN |
                      PORTALLOCATOR_ENABLE_SHARED_SOCKET);

  session_->StartGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));

  // Expect to see 2 ports and 2 candidates - TURN/TCP and TCP ports, TCP and
  // TURN/TCP candidates.
  EXPECT_EQ(2U, ports_.size());
  EXPECT_EQ(2U, candidates_.size());
  Candidate turn_candidate;
  EXPECT_TRUE(FindCandidate(candidates_, IceCandidateType::kRelay, "udp",
                            kTurnUdpExtAddr, &turn_candidate));
  // The TURN candidate should use TCP to contact the TURN server.
  EXPECT_EQ(TCP_PROTOCOL_NAME, turn_candidate.relay_protocol());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "tcp", kClientAddr));
}

// Test that we can get OnCandidatesAllocationDone callback when all the ports
// are disabled.
TEST_F(BasicPortAllocatorTest, TestDisableAllPorts) {
  AddInterface(kClientAddr);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->set_flags(PORTALLOCATOR_DISABLE_UDP | PORTALLOCATOR_DISABLE_STUN |
                      PORTALLOCATOR_DISABLE_RELAY | PORTALLOCATOR_DISABLE_TCP);
  session_->StartGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(0U, candidates_.size());
}

// Test that we don't crash or malfunction if we can't create UDP sockets.
TEST_F(BasicPortAllocatorTest, TestGetAllPortsNoUdpSockets) {
  AddInterface(kClientAddr);
  fss_->set_udp_sockets_enabled(false);
  ASSERT_TRUE(CreateSession(1));
  session_->StartGettingPorts();
  ASSERT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(1U, candidates_.size());
  EXPECT_EQ(1U, ports_.size());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "tcp", kClientAddr));
}

// Test that we don't crash or malfunction if we can't create UDP sockets or
// listen on TCP sockets. We still give out a local TCP address, since
// apparently this is needed for the remote side to accept our connection.
TEST_F(BasicPortAllocatorTest, TestGetAllPortsNoUdpSocketsNoTcpListen) {
  AddInterface(kClientAddr);
  fss_->set_udp_sockets_enabled(false);
  fss_->set_tcp_listen_enabled(false);
  ASSERT_TRUE(CreateSession(1));
  session_->StartGettingPorts();
  ASSERT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(1U, candidates_.size());
  EXPECT_EQ(1U, ports_.size());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "tcp", kClientAddr));
}

// Test that we don't crash or malfunction if we can't create any sockets.
// TODO(deadbeef): Find a way to exit early here.
TEST_F(BasicPortAllocatorTest, TestGetAllPortsNoSockets) {
  AddInterface(kClientAddr);
  fss_->set_tcp_sockets_enabled(false);
  fss_->set_udp_sockets_enabled(false);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE(
      Waiter({.timeout = TimeDelta::Millis(2000), .clock = &time_controller_})
          .Until([&] { return !candidates_.empty(); }));
  // TODO(deadbeef): Check candidate_allocation_done signal.
  // In case of Relay, ports creation will succeed but sockets will fail.
  // There is no error reporting from RelayEntry to handle this failure.
}

// Testing STUN timeout.
TEST_F(BasicPortAllocatorTest, TestGetAllPortsNoUdpAllowed) {
  fss_->AddRule(false, FP_UDP, FD_ANY, kClientAddr);
  AddInterface(kClientAddr);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_THAT(waiter_.Until([&] { return candidates_.size(); }, Eq(2U)),
              IsRtcOk());
  EXPECT_EQ(2U, ports_.size());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", kClientAddr));
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "tcp", kClientAddr));
  // We wait at least for a full STUN timeout, which
  // STUN_TOTAL_TIMEOUT seconds.
  EXPECT_TRUE(Waiter({.timeout = TimeDelta::Millis(STUN_TOTAL_TIMEOUT),
                      .clock = &time_controller_})
                  .Until([&] { return candidate_allocation_done_; }));
  // No additional (STUN) candidates.
  EXPECT_EQ(2U, candidates_.size());
}

TEST_F(BasicPortAllocatorTest, TestCandidatePriorityOfMultipleInterfaces) {
  AddInterface(kClientAddr);
  AddInterface(kClientAddr2);
  // Allocating only host UDP ports. This is done purely for testing
  // convenience.
  allocator().set_flags(PORTALLOCATOR_DISABLE_TCP | PORTALLOCATOR_DISABLE_STUN |
                        PORTALLOCATOR_DISABLE_RELAY);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  ASSERT_EQ(2U, candidates_.size());
  EXPECT_EQ(2U, ports_.size());
  // Candidates priorities should be different.
  EXPECT_NE(candidates_[0].priority(), candidates_[1].priority());
}

// Test to verify ICE restart process.
TEST_F(BasicPortAllocatorTest, TestGetAllPortsRestarts) {
  AddInterface(kClientAddr);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(3U, candidates_.size());
  EXPECT_EQ(3U, ports_.size());
  // TODO(deadbeef): Extend this to verify ICE restart.
}

// Test that the allocator session uses the candidate filter it's created with,
// rather than the filter of its parent allocator.
// The filter of the allocator should only affect the next gathering phase,
// according to JSEP, which means the *next* allocator session returned.
TEST_F(BasicPortAllocatorTest, TestSessionUsesOwnCandidateFilter) {
  AddInterface(kClientAddr);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  // Set candidate filter *after* creating the session. Should have no effect.
  allocator().SetCandidateFilter(CF_RELAY);
  session_->StartGettingPorts();
  // 7 candidates and 4 ports is what we would normally get (see the
  // TestGetAllPorts* tests).
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(3U, candidates_.size());
  EXPECT_EQ(3U, ports_.size());
}

// Test ICE candidate filter mechanism with options Relay/Host/Reflexive.
// This test also verifies that when the allocator is only allowed to use
// relay (i.e. IceTransportsType is relay), the raddr is an empty
// address with the correct family. This is to prevent any local
// reflective address leakage in the sdp line.
TEST_F(BasicPortAllocatorTest, TestCandidateFilterWithRelayOnly) {
  AddInterface(kClientAddr);
  // GTURN is not configured here.
  ResetWithTurnServersNoNat(kTurnUdpIntAddr, SocketAddress());
  allocator().SetCandidateFilter(CF_RELAY);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kRelay, "udp",
                           SocketAddress(kTurnUdpExtAddr.ipaddr(), 0)));

  EXPECT_EQ(1U, candidates_.size());
  EXPECT_EQ(1U, ports_.size());  // Only Relay port will be in ready state.
  EXPECT_TRUE(candidates_[0].is_relay());
  EXPECT_EQ(candidates_[0].related_address(),
            EmptySocketAddressWithFamily(candidates_[0].address().family()));
}

TEST_F(BasicPortAllocatorTest, TestCandidateFilterWithHostOnly) {
  AddInterface(kClientAddr);
  allocator().set_flags(PORTALLOCATOR_ENABLE_SHARED_SOCKET);
  allocator().SetCandidateFilter(CF_HOST);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(2U, candidates_.size());  // Host UDP/TCP candidates only.
  EXPECT_EQ(2U, ports_.size());       // UDP/TCP ports only.
  for (const Candidate& candidate : candidates_) {
    EXPECT_TRUE(candidate.is_local());
  }
}

// Host is behind the NAT.
TEST_F(BasicPortAllocatorTest, TestCandidateFilterWithReflexiveOnly) {
  AddInterface(kPrivateAddr);
  ResetWithStunServerAndNat(kStunAddr);

  allocator().set_flags(PORTALLOCATOR_ENABLE_SHARED_SOCKET);
  allocator().SetCandidateFilter(CF_REFLEXIVE);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  // Host is behind NAT, no private address will be exposed. Hence only UDP
  // port with STUN candidate will be sent outside.
  EXPECT_EQ(1U, candidates_.size());  // Only STUN candidate.
  EXPECT_EQ(1U, ports_.size());       // Only UDP port will be in ready state.
  EXPECT_TRUE(candidates_[0].is_stun());
  EXPECT_EQ(candidates_[0].related_address(),
            EmptySocketAddressWithFamily(candidates_[0].address().family()));
}

// Host is not behind the NAT.
TEST_F(BasicPortAllocatorTest, TestCandidateFilterWithReflexiveOnlyAndNoNAT) {
  AddInterface(kClientAddr);
  allocator().set_flags(PORTALLOCATOR_ENABLE_SHARED_SOCKET);
  allocator().SetCandidateFilter(CF_REFLEXIVE);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  // Host has a public address, both UDP and TCP candidates will be exposed.
  EXPECT_EQ(2U, candidates_.size());  // Local UDP + TCP candidate.
  EXPECT_EQ(2U, ports_.size());  //  UDP and TCP ports will be in ready state.
  for (const Candidate& candidate : candidates_) {
    EXPECT_TRUE(candidate.is_local());
  }
}

// Test that we get the same ufrag and pwd for all candidates.
TEST_F(BasicPortAllocatorTest, TestEnableSharedUfrag) {
  AddInterface(kClientAddr);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  ASSERT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(3U, candidates_.size());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", kClientAddr));
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kSrflx, "udp", kClientAddr));
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "tcp", kClientAddr));
  EXPECT_EQ(3U, ports_.size());
  for (const Candidate& candidate : candidates_) {
    EXPECT_EQ(kIceUfrag0, candidate.username());
    EXPECT_EQ(kIcePwd0, candidate.password());
  }
}

// Test that when PORTALLOCATOR_ENABLE_SHARED_SOCKET is enabled only one port
// is allocated for udp and stun. Also verify there is only one candidate
// (local) if stun candidate is same as local candidate, which will be the case
// in a public network like the below test.
TEST_F(BasicPortAllocatorTest, TestSharedSocketWithoutNat) {
  AddInterface(kClientAddr);
  allocator_->set_flags(allocator().flags() |
                        PORTALLOCATOR_ENABLE_SHARED_SOCKET);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  ASSERT_THAT(waiter_.Until([&] { return candidates_.size(); }, Eq(2U)),
              IsRtcOk());
  EXPECT_EQ(2U, ports_.size());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", kClientAddr));
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
}

// Test that when PORTALLOCATOR_ENABLE_SHARED_SOCKET is enabled only one port
// is allocated for udp and stun. In this test we should expect both stun and
// local candidates as client behind a nat.
TEST_F(BasicPortAllocatorTest, TestSharedSocketWithNat) {
  AddInterface(kClientAddr);
  ResetWithStunServerAndNat(kStunAddr);

  allocator_->set_flags(allocator().flags() |
                        PORTALLOCATOR_ENABLE_SHARED_SOCKET);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  ASSERT_THAT(waiter_.Until([&] { return candidates_.size(); }, Eq(3U)),
              IsRtcOk());
  ASSERT_EQ(2U, ports_.size());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", kClientAddr));
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kSrflx, "udp",
                           SocketAddress(kNatUdpAddr.ipaddr(), 0)));
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(3U, candidates_.size());
}

// Test TURN port in shared socket mode with UDP and TCP TURN server addresses.
TEST_F(BasicPortAllocatorTest, TestSharedSocketWithoutNatUsingTurn) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
  AddInterface(kClientAddr);
  allocator_.emplace(env_, &network_manager_, &socket_factory_);
  allocator_->Initialize();

  AddTurnServers(kTurnUdpIntAddr, kTurnTcpIntAddr);

  allocator_->set_step_delay(kMinimumStepDelay);
  allocator_->set_flags(allocator().flags() |
                        PORTALLOCATOR_ENABLE_SHARED_SOCKET |
                        PORTALLOCATOR_DISABLE_TCP);

  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();

  ASSERT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  ASSERT_EQ(3U, candidates_.size());
  ASSERT_EQ(3U, ports_.size());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", kClientAddr));
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kRelay, "udp",
                           SocketAddress(kTurnUdpExtAddr.ipaddr(), 0)));
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kRelay, "udp",
                           SocketAddress(kTurnUdpExtAddr.ipaddr(), 0)));
}

// Test that if the turn port prune policy is PRUNE_BASED_ON_PRIORITY, TCP TURN
// port will not be used if UDP TurnPort is used, given that TCP TURN port
// becomes ready first.
TEST_F(BasicPortAllocatorTest,
       TestUdpTurnPortPrunesTcpTurnPortWithTcpPortReadyFirst) {
  // UDP has longer delay than TCP so that TCP TURN port becomes ready first.
  virtual_socket_server()->SetDelayOnAddress(kTurnUdpIntAddr, 200);
  virtual_socket_server()->SetDelayOnAddress(kTurnTcpIntAddr, 100);

  TestTurnPortPrunesWithUdpAndTcpPorts(PRUNE_BASED_ON_PRIORITY,
                                       true /* tcp_pruned */);
}

// Test that if turn port prune policy is PRUNE_BASED_ON_PRIORITY, TCP TURN port
// will not be used if UDP TurnPort is used, given that UDP TURN port becomes
// ready first.
TEST_F(BasicPortAllocatorTest,
       TestUdpTurnPortPrunesTcpTurnPortsWithUdpPortReadyFirst) {
  // UDP has shorter delay than TCP so that UDP TURN port becomes ready first.
  virtual_socket_server()->SetDelayOnAddress(kTurnUdpIntAddr, 100);
  virtual_socket_server()->SetDelayOnAddress(kTurnTcpIntAddr, 200);

  TestTurnPortPrunesWithUdpAndTcpPorts(PRUNE_BASED_ON_PRIORITY,
                                       true /* tcp_pruned */);
}

// Test that if turn_port_prune policy is KEEP_FIRST_READY, the first ready port
// will be kept regardless of the priority.
TEST_F(BasicPortAllocatorTest,
       TestUdpTurnPortPrunesTcpTurnPortIfUdpReadyFirst) {
  // UDP has shorter delay than TCP so that UDP TURN port becomes ready first.
  virtual_socket_server()->SetDelayOnAddress(kTurnUdpIntAddr, 100);
  virtual_socket_server()->SetDelayOnAddress(kTurnTcpIntAddr, 200);

  TestTurnPortPrunesWithUdpAndTcpPorts(KEEP_FIRST_READY, true /* tcp_pruned */);
}

// Test that if turn_port_prune policy is KEEP_FIRST_READY, the first ready port
// will be kept regardless of the priority.
TEST_F(BasicPortAllocatorTest,
       TestTcpTurnPortPrunesUdpTurnPortIfTcpReadyFirst) {
  // UDP has longer delay than TCP so that TCP TURN port becomes ready first.
  virtual_socket_server()->SetDelayOnAddress(kTurnUdpIntAddr, 200);
  virtual_socket_server()->SetDelayOnAddress(kTurnTcpIntAddr, 100);

  TestTurnPortPrunesWithUdpAndTcpPorts(KEEP_FIRST_READY,
                                       false /* tcp_pruned */);
}

// Tests that if turn port prune policy is PRUNE_BASED_ON_PRIORITY, IPv4
// TurnPort will not be used if IPv6 TurnPort is used, given that IPv4 TURN port
// becomes ready first.
TEST_F(BasicPortAllocatorTest,
       TestIPv6TurnPortPrunesIPv4TurnPortWithIPv4PortReadyFirst) {
  // IPv6 has longer delay than IPv4, so that IPv4 TURN port becomes ready
  // first.
  virtual_socket_server()->SetDelayOnAddress(kTurnUdpIntAddr, 100);
  virtual_socket_server()->SetDelayOnAddress(kTurnUdpIntIPv6Addr, 200);

  TestIPv6TurnPortPrunesIPv4TurnPort();
}

// Tests that if turn port prune policy is PRUNE_BASED_ON_PRIORITY, IPv4
// TurnPort will not be used if IPv6 TurnPort is used, given that IPv6 TURN port
// becomes ready first.
TEST_F(BasicPortAllocatorTest,
       TestIPv6TurnPortPrunesIPv4TurnPortWithIPv6PortReadyFirst) {
  // IPv6 has longer delay than IPv4, so that IPv6 TURN port becomes ready
  // first.
  virtual_socket_server()->SetDelayOnAddress(kTurnUdpIntAddr, 200);
  virtual_socket_server()->SetDelayOnAddress(kTurnUdpIntIPv6Addr, 100);

  TestIPv6TurnPortPrunesIPv4TurnPort();
}

// Tests that if turn port prune policy is PRUNE_BASED_ON_PRIORITY, each network
// interface will has its own set of TurnPorts based on their priorities, in the
// default case where no transit delay is set.
TEST_F(BasicPortAllocatorTest, TestEachInterfaceHasItsOwnTurnPortsNoDelay) {
  TestEachInterfaceHasItsOwnTurnPorts();
}

// Tests that if turn port prune policy is PRUNE_BASED_ON_PRIORITY, each network
// interface will has its own set of TurnPorts based on their priorities, given
// that IPv4/TCP TURN port becomes ready first.
TEST_F(BasicPortAllocatorTest,
       TestEachInterfaceHasItsOwnTurnPortsWithTcpIPv4ReadyFirst) {
  // IPv6/UDP have longer delay than IPv4/TCP, so that IPv4/TCP TURN port
  // becomes ready last.
  virtual_socket_server()->SetDelayOnAddress(kTurnTcpIntAddr, 10);
  virtual_socket_server()->SetDelayOnAddress(kTurnUdpIntAddr, 100);
  virtual_socket_server()->SetDelayOnAddress(kTurnTcpIntIPv6Addr, 20);
  virtual_socket_server()->SetDelayOnAddress(kTurnUdpIntIPv6Addr, 300);

  TestEachInterfaceHasItsOwnTurnPorts();
}

// Testing DNS resolve for the TURN server, this will test AllocationSequence
// handling the unresolved address signal from TurnPort.
TEST_F(BasicPortAllocatorTest, TestSharedSocketWithServerAddressResolve) {
  // This test relies on a real query for "localhost", so it won't work on an
  // IPv6-only machine.
  MAYBE_SKIP_IPV4;
  turn_server_.AddInternalSocket(SocketAddress("127.0.0.1", 3478), PROTO_UDP);
  AddInterface(kClientAddr);
  // Use a fake socket factory to avoid real DNS lookups which may not used a
  // fake clock.
  FakePacketSocketFactory fake_socket_factory(fss_.get());
  allocator_.emplace(env_, &network_manager_, &fake_socket_factory);
  allocator_->Initialize();
  RelayServerConfig turn_server;
  RelayCredentials credentials(kTurnUsername, kTurnPassword);
  turn_server.credentials = credentials;
  turn_server.ports.push_back(
      ProtocolAddress(SocketAddress("localhost", 3478), PROTO_UDP));
  allocator_->AddTurnServerForTesting(turn_server);

  allocator_->set_step_delay(kMinimumStepDelay);
  allocator_->set_flags(allocator().flags() |
                        PORTALLOCATOR_ENABLE_SHARED_SOCKET |
                        PORTALLOCATOR_DISABLE_TCP);

  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();

  EXPECT_THAT(waiter_.Until([&] { return ports_.size(); }, Eq(2U)), IsRtcOk());
  session_.reset();
  allocator_.reset();
}

// Test that when PORTALLOCATOR_ENABLE_SHARED_SOCKET is enabled only one port
// is allocated for udp/stun/turn. In this test we should expect all local,
// stun and turn candidates.
TEST_F(BasicPortAllocatorTest, TestSharedSocketWithNatUsingTurn) {
  AddInterface(kClientAddr);
  ResetWithStunServerAndNat(kStunAddr);

  AddTurnServers(kTurnUdpIntAddr, SocketAddress());

  allocator_->set_flags(allocator().flags() |
                        PORTALLOCATOR_ENABLE_SHARED_SOCKET |
                        PORTALLOCATOR_DISABLE_TCP);

  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();

  ASSERT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(3U, candidates_.size());
  ASSERT_EQ(2U, ports_.size());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", kClientAddr));
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kSrflx, "udp",
                           SocketAddress(kNatUdpAddr.ipaddr(), 0)));
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kRelay, "udp",
                           SocketAddress(kTurnUdpExtAddr.ipaddr(), 0)));
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  // Local port will be created first and then TURN port.
  // TODO(deadbeef): This isn't something the BasicPortAllocator API contract
  // guarantees...
  EXPECT_EQ(2U, ports_[0]->Candidates().size());
  EXPECT_EQ(1U, ports_[1]->Candidates().size());
}

// Test that when PORTALLOCATOR_ENABLE_SHARED_SOCKET is enabled and the TURN
// server is also used as the STUN server, we should get 'local', 'stun', and
// 'relay' candidates.
TEST_F(BasicPortAllocatorTest, TestSharedSocketWithNatUsingTurnAsStun) {
  AddInterface(kClientAddr);
  // Use an empty SocketAddress to add a NAT without STUN server.
  ResetWithStunServerAndNat(SocketAddress());
  AddTurnServers(kTurnUdpIntAddr, SocketAddress());

  // Must set the step delay to 0 to make sure the relay allocation phase is
  // started before the STUN candidates are obtained, so that the STUN binding
  // response is processed when both StunPort and TurnPort exist to reproduce
  // webrtc issue 3537.
  allocator_->set_step_delay(0);
  allocator_->set_flags(allocator().flags() |
                        PORTALLOCATOR_ENABLE_SHARED_SOCKET |
                        PORTALLOCATOR_DISABLE_TCP);

  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();

  ASSERT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(3U, candidates_.size());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", kClientAddr));
  Candidate stun_candidate;
  EXPECT_TRUE(FindCandidate(candidates_, IceCandidateType::kSrflx, "udp",
                            SocketAddress(kNatUdpAddr.ipaddr(), 0),
                            &stun_candidate));
  EXPECT_TRUE(HasCandidateWithRelatedAddr(
      candidates_, IceCandidateType::kRelay, "udp",
      SocketAddress(kTurnUdpExtAddr.ipaddr(), 0), stun_candidate.address()));

  // Local port will be created first and then TURN port.
  // TODO(deadbeef): This isn't something the BasicPortAllocator API contract
  // guarantees...
  EXPECT_EQ(2U, ports_[0]->Candidates().size());
  EXPECT_EQ(1U, ports_[1]->Candidates().size());
}

// Test that when only a TCP TURN server is available, we do NOT use it as
// a UDP STUN server, as this could leak our IP address. Thus we should only
// expect two ports, a UDPPort and TurnPort.
TEST_F(BasicPortAllocatorTest, TestSharedSocketWithNatUsingTurnTcpOnly) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
  AddInterface(kClientAddr);
  ResetWithStunServerAndNat(SocketAddress());
  AddTurnServers(SocketAddress(), kTurnTcpIntAddr);

  allocator_->set_flags(allocator().flags() |
                        PORTALLOCATOR_ENABLE_SHARED_SOCKET |
                        PORTALLOCATOR_DISABLE_TCP);

  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();

  ASSERT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(2U, candidates_.size());
  ASSERT_EQ(2U, ports_.size());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", kClientAddr));
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kRelay, "udp",
                           SocketAddress(kTurnUdpExtAddr.ipaddr(), 0)));
  EXPECT_EQ(1U, ports_[0]->Candidates().size());
  EXPECT_EQ(1U, ports_[1]->Candidates().size());
}

// Test that even when PORTALLOCATOR_ENABLE_SHARED_SOCKET is NOT enabled, the
// TURN server is used as the STUN server and we get 'local', 'stun', and
// 'relay' candidates.
// TODO(deadbeef): Remove this test when support for non-shared socket mode
// is removed.
TEST_F(BasicPortAllocatorTest, TestNonSharedSocketWithNatUsingTurnAsStun) {
  AddInterface(kClientAddr);
  // Use an empty SocketAddress to add a NAT without STUN server.
  ResetWithStunServerAndNat(SocketAddress());
  AddTurnServers(kTurnUdpIntAddr, SocketAddress());

  allocator_->set_flags(allocator().flags() | PORTALLOCATOR_DISABLE_TCP);

  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();

  ASSERT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(3U, candidates_.size());
  ASSERT_EQ(3U, ports_.size());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", kClientAddr));
  Candidate stun_candidate;
  EXPECT_TRUE(FindCandidate(candidates_, IceCandidateType::kSrflx, "udp",
                            SocketAddress(kNatUdpAddr.ipaddr(), 0),
                            &stun_candidate));
  Candidate turn_candidate;
  EXPECT_TRUE(FindCandidate(candidates_, IceCandidateType::kRelay, "udp",
                            SocketAddress(kTurnUdpExtAddr.ipaddr(), 0),
                            &turn_candidate));
  // Not using shared socket, so the STUN request's server reflexive address
  // should be different than the TURN request's server reflexive address.
  EXPECT_NE(turn_candidate.related_address(), stun_candidate.address());

  EXPECT_EQ(1U, ports_[0]->Candidates().size());
  EXPECT_EQ(1U, ports_[1]->Candidates().size());
  EXPECT_EQ(1U, ports_[2]->Candidates().size());
}

// Test that even when both a STUN and TURN server are configured, the TURN
// server is used as a STUN server and we get a 'stun' candidate.
TEST_F(BasicPortAllocatorTest, TestSharedSocketWithNatUsingTurnAndStun) {
  AddInterface(kClientAddr);
  // Configure with STUN server but destroy it, so we can ensure that it's
  // the TURN server actually being used as a STUN server.
  ResetWithStunServerAndNat(kStunAddr);
  stun_server_.reset();
  AddTurnServers(kTurnUdpIntAddr, SocketAddress());

  allocator_->set_flags(allocator().flags() |
                        PORTALLOCATOR_ENABLE_SHARED_SOCKET |
                        PORTALLOCATOR_DISABLE_TCP);

  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();

  ASSERT_THAT(waiter_.Until([&] { return candidates_.size(); }, Eq(3U)),
              IsRtcOk());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", kClientAddr));
  Candidate stun_candidate;
  EXPECT_TRUE(FindCandidate(candidates_, IceCandidateType::kSrflx, "udp",
                            SocketAddress(kNatUdpAddr.ipaddr(), 0),
                            &stun_candidate));
  EXPECT_TRUE(HasCandidateWithRelatedAddr(
      candidates_, IceCandidateType::kRelay, "udp",
      SocketAddress(kTurnUdpExtAddr.ipaddr(), 0), stun_candidate.address()));

  // Don't bother waiting for STUN timeout, since we already verified
  // that we got a STUN candidate from the TURN server.
}

// This test verifies when PORTALLOCATOR_ENABLE_SHARED_SOCKET flag is enabled
// and fail to generate STUN candidate, local UDP candidate is generated
// properly.
TEST_F(BasicPortAllocatorTest, TestSharedSocketNoUdpAllowed) {
  allocator().set_flags(allocator().flags() | PORTALLOCATOR_DISABLE_RELAY |
                        PORTALLOCATOR_DISABLE_TCP |
                        PORTALLOCATOR_ENABLE_SHARED_SOCKET);
  fss_->AddRule(false, FP_UDP, FD_ANY, kClientAddr);
  AddInterface(kClientAddr);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  ASSERT_THAT(waiter_.Until([&] { return ports_.size(); }, Eq(1U)), IsRtcOk());
  EXPECT_EQ(1U, candidates_.size());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", kClientAddr));
  // STUN timeout is 9.5sec. We need to wait to get candidate done signal.
  EXPECT_TRUE(
      Waiter({.timeout = kStunTimeout, .clock = &time_controller_}).Until([&] {
        return candidate_allocation_done_;
      }));
  EXPECT_EQ(1U, candidates_.size());
}

// Test that when the NetworkManager doesn't have permission to enumerate
// adapters, the PORTALLOCATOR_DISABLE_ADAPTER_ENUMERATION is specified
// automatically.
TEST_F(BasicPortAllocatorTest, TestNetworkPermissionBlocked) {
  network_manager_.set_default_local_addresses(kPrivateAddr.ipaddr(),
                                               IPAddress());
  network_manager_.set_enumeration_permission(
      NetworkManager::ENUMERATION_BLOCKED);
  allocator().set_flags(allocator().flags() | PORTALLOCATOR_DISABLE_RELAY |
                        PORTALLOCATOR_DISABLE_TCP |
                        PORTALLOCATOR_ENABLE_SHARED_SOCKET);
  EXPECT_EQ(0U,
            allocator_->flags() & PORTALLOCATOR_DISABLE_ADAPTER_ENUMERATION);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  EXPECT_EQ(0U, session_->flags() & PORTALLOCATOR_DISABLE_ADAPTER_ENUMERATION);
  session_->StartGettingPorts();
  EXPECT_THAT(waiter_.Until([&] { return ports_.size(); }, Eq(1U)), IsRtcOk());
  EXPECT_EQ(1U, candidates_.size());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", kPrivateAddr));
  EXPECT_NE(0U, session_->flags() & PORTALLOCATOR_DISABLE_ADAPTER_ENUMERATION);
}

// This test verifies allocator can use IPv6 addresses along with IPv4.
TEST_F(BasicPortAllocatorTest, TestEnableIPv6Addresses) {
  allocator().set_flags(allocator().flags() | PORTALLOCATOR_DISABLE_RELAY |
                        PORTALLOCATOR_ENABLE_IPV6 |
                        PORTALLOCATOR_ENABLE_SHARED_SOCKET);
  AddInterface(kClientIPv6Addr);
  AddInterface(kClientAddr);
  allocator_->set_step_delay(kMinimumStepDelay);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  ASSERT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(4U, ports_.size());
  EXPECT_EQ(4U, candidates_.size());
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kHost, "udp",
                           kClientIPv6Addr));
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", kClientAddr));
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kHost, "tcp",
                           kClientIPv6Addr));
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "tcp", kClientAddr));
}

TEST_F(BasicPortAllocatorTest, TestStopGettingPorts) {
  AddInterface(kClientAddr);
  allocator_->set_step_delay(kDefaultStepDelay);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  ASSERT_THAT(waiter_.Until([&] { return candidates_.size(); }, Eq(2U)),
              IsRtcOk());
  EXPECT_EQ(2U, ports_.size());
  session_->StopGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));

  // After stopping getting ports, adding a new interface will not start
  // getting ports again.
  allocator_->set_step_delay(kMinimumStepDelay);
  candidates_.clear();
  ports_.clear();
  candidate_allocation_done_ = false;
  network_manager_.AddInterface(kClientAddr2);
  time_controller_.AdvanceTime(TimeDelta::Millis(1000));
  EXPECT_EQ(0U, candidates_.size());
  EXPECT_EQ(0U, ports_.size());
}

TEST_F(BasicPortAllocatorTest, TestClearGettingPorts) {
  AddInterface(kClientAddr);
  allocator_->set_step_delay(kDefaultStepDelay);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  ASSERT_THAT(waiter_.Until([&] { return candidates_.size(); }, Eq(2U)),
              IsRtcOk());
  EXPECT_EQ(2U, ports_.size());
  session_->ClearGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));

  // After clearing getting ports, adding a new interface will start getting
  // ports again.
  allocator_->set_step_delay(kMinimumStepDelay);
  candidates_.clear();
  ports_.clear();
  candidate_allocation_done_ = false;
  network_manager_.AddInterface(kClientAddr2);
  ASSERT_THAT(waiter_.Until([&] { return candidates_.size(); }, Eq(2U)),
              IsRtcOk());
  EXPECT_EQ(2U, ports_.size());
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
}

// Test that the ports and candidates are updated with new ufrag/pwd/etc. when
// a pooled session is taken out of the pool.
TEST_F(BasicPortAllocatorTest, TestTransportInformationUpdated) {
  AddInterface(kClientAddr);
  int pool_size = 1;
  allocator_->SetConfiguration(allocator_->stun_servers(),
                               allocator_->turn_servers(), pool_size, NO_PRUNE);
  const PortAllocatorSession* peeked_session = allocator_->GetPooledSession();
  ASSERT_NE(nullptr, peeked_session);
  EXPECT_TRUE(waiter_.Until(
      [&] { return peeked_session->CandidatesAllocationDone(); }));
  // Expect that when TakePooledSession is called,
  // UpdateTransportInformationInternal will be called and the
  // BasicPortAllocatorSession will update the ufrag/pwd of ports and
  // candidates.
  session_ =
      allocator_->TakePooledSession(kContentName, 1, kIceUfrag0, kIcePwd0);
  ASSERT_NE(nullptr, session_.get());
  auto ready_ports = session_->ReadyPorts();
  auto candidates = session_->ReadyCandidates();
  EXPECT_FALSE(ready_ports.empty());
  EXPECT_FALSE(candidates.empty());
  for (const PortInterface* port_interface : ready_ports) {
    const Port* port = static_cast<const Port*>(port_interface);
    EXPECT_EQ(kContentName, port->content_name());
    EXPECT_EQ(1, port->component());
    EXPECT_EQ(kIceUfrag0, port->username_fragment());
    EXPECT_EQ(kIcePwd0, port->password());
  }
  for (const Candidate& candidate : candidates) {
    EXPECT_EQ(1, candidate.component());
    EXPECT_EQ(kIceUfrag0, candidate.username());
    EXPECT_EQ(kIcePwd0, candidate.password());
  }
}

// Test that a new candidate filter takes effect even on already-gathered
// candidates.
TEST_F(BasicPortAllocatorTest, TestSetCandidateFilterAfterCandidatesGathered) {
  AddInterface(kClientAddr);
  int pool_size = 1;
  allocator_->SetConfiguration(allocator_->stun_servers(),
                               allocator_->turn_servers(), pool_size, NO_PRUNE);
  const PortAllocatorSession* peeked_session = allocator_->GetPooledSession();
  ASSERT_NE(nullptr, peeked_session);
  EXPECT_TRUE(waiter_.Until(
      [&] { return peeked_session->CandidatesAllocationDone(); }));
  size_t initial_candidates_size = peeked_session->ReadyCandidates().size();
  size_t initial_ports_size = peeked_session->ReadyPorts().size();
  allocator_->SetCandidateFilter(CF_RELAY);
  // Assume that when TakePooledSession is called, the candidate filter will be
  // applied to the pooled session. This is tested by PortAllocatorTest.
  session_ =
      allocator_->TakePooledSession(kContentName, 1, kIceUfrag0, kIcePwd0);
  ASSERT_NE(nullptr, session_.get());
  auto candidates = session_->ReadyCandidates();
  auto ports = session_->ReadyPorts();
  // Sanity check that the number of candidates and ports decreased.
  EXPECT_GT(initial_candidates_size, candidates.size());
  EXPECT_GT(initial_ports_size, ports.size());
  for (const PortInterface* port : ports) {
    // Expect only relay ports.
    EXPECT_EQ(IceCandidateType::kRelay, port->Type());
  }
  for (const Candidate& candidate : candidates) {
    // Expect only relay candidates now that the filter is applied.
    EXPECT_TRUE(candidate.is_relay());
    // Expect that the raddr is emptied due to the CF_RELAY filter.
    EXPECT_EQ(candidate.related_address(),
              EmptySocketAddressWithFamily(candidate.address().family()));
  }
}

// Test that candidates that do not match a previous candidate filter can be
// surfaced if they match the new one after setting the filter value.
TEST_F(BasicPortAllocatorTest,
       SurfaceNewCandidatesAfterSetCandidateFilterToAddCandidateTypes) {
  // We would still surface a host candidate if the IP is public, even though it
  // is disabled by the candidate filter. See
  // BasicPortAllocatorSession::CheckCandidateFilter. Use the private address so
  // that the srflx candidate is not equivalent to the host candidate.
  AddInterface(kPrivateAddr);
  ResetWithStunServerAndNat(kStunAddr);

  AddTurnServers(kTurnUdpIntAddr, SocketAddress());

  allocator_->set_flags(allocator().flags() |
                        PORTALLOCATOR_ENABLE_SHARED_SOCKET |
                        PORTALLOCATOR_DISABLE_TCP);

  allocator_->SetCandidateFilter(CF_NONE);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_TRUE(candidates_.empty());
  EXPECT_TRUE(ports_.empty());

  // Surface the relay candidate previously gathered but not signaled.
  session_->SetCandidateFilter(CF_RELAY);
  ASSERT_THAT(waiter_.Until([&] { return candidates_.size(); }, Eq(1u)),
              IsRtcOk());
  EXPECT_TRUE(candidates_.back().is_relay());
  EXPECT_EQ(1u, ports_.size());

  // Surface the srflx candidate previously gathered but not signaled.
  session_->SetCandidateFilter(CF_RELAY | CF_REFLEXIVE);
  ASSERT_THAT(waiter_.Until([&] { return candidates_.size(); }, Eq(2u)),
              IsRtcOk());
  EXPECT_TRUE(candidates_.back().is_stun());
  EXPECT_EQ(2u, ports_.size());

  // Surface the srflx candidate previously gathered but not signaled.
  session_->SetCandidateFilter(CF_ALL);
  ASSERT_THAT(waiter_.Until([&] { return candidates_.size(); }, Eq(3u)),
              IsRtcOk());
  EXPECT_TRUE(candidates_.back().is_local());
  EXPECT_EQ(2u, ports_.size());
}

// This is a similar test as
// SurfaceNewCandidatesAfterSetCandidateFilterToAddCandidateTypes, and we
// test the transitions for which the new filter value is not a super set of the
// previous value.
TEST_F(
    BasicPortAllocatorTest,
    SurfaceNewCandidatesAfterSetCandidateFilterToAllowDifferentCandidateTypes) {
  // We would still surface a host candidate if the IP is public, even though it
  // is disabled by the candidate filter. See
  // BasicPortAllocatorSession::CheckCandidateFilter. Use the private address so
  // that the srflx candidate is not equivalent to the host candidate.
  AddInterface(kPrivateAddr);
  ResetWithStunServerAndNat(kStunAddr);

  AddTurnServers(kTurnUdpIntAddr, SocketAddress());

  allocator_->set_flags(allocator().flags() |
                        PORTALLOCATOR_ENABLE_SHARED_SOCKET |
                        PORTALLOCATOR_DISABLE_TCP);

  allocator_->SetCandidateFilter(CF_NONE);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_TRUE(candidates_.empty());
  EXPECT_TRUE(ports_.empty());

  // Surface the relay candidate previously gathered but not signaled.
  session_->SetCandidateFilter(CF_RELAY);
  EXPECT_THAT(waiter_.Until([&] { return candidates_.size(); }, Eq(1u)),
              IsRtcOk());
  EXPECT_TRUE(candidates_.back().is_relay());
  EXPECT_EQ(1u, ports_.size());

  // Surface the srflx candidate previously gathered but not signaled.
  session_->SetCandidateFilter(CF_REFLEXIVE);
  EXPECT_THAT(waiter_.Until([&] { return candidates_.size(); }, Eq(2u)),
              IsRtcOk());
  EXPECT_TRUE(candidates_.back().is_stun());
  EXPECT_EQ(2u, ports_.size());

  // Surface the host candidate previously gathered but not signaled.
  session_->SetCandidateFilter(CF_HOST);
  EXPECT_THAT(waiter_.Until([&] { return candidates_.size(); }, Eq(3u)),
              IsRtcOk());
  EXPECT_TRUE(candidates_.back().is_local());
  // We use a shared socket and UDPPort handles the srflx candidate.
  EXPECT_EQ(2u, ports_.size());
}

// Test that after an allocation session has stopped getting ports, changing the
// candidate filter to allow new types of gathered candidates does not surface
// any candidate.
TEST_F(BasicPortAllocatorTest,
       NoCandidateSurfacedWhenUpdatingCandidateFilterIfSessionStopped) {
  AddInterface(kPrivateAddr);
  ResetWithStunServerAndNat(kStunAddr);

  AddTurnServers(kTurnUdpIntAddr, SocketAddress());

  allocator_->set_flags(allocator().flags() |
                        PORTALLOCATOR_ENABLE_SHARED_SOCKET |
                        PORTALLOCATOR_DISABLE_TCP);

  allocator_->SetCandidateFilter(CF_NONE);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  auto test_invariants = [this]() {
    EXPECT_TRUE(candidates_.empty());
    EXPECT_TRUE(ports_.empty());
  };

  test_invariants();

  session_->StopGettingPorts();

  session_->SetCandidateFilter(CF_RELAY);
  time_controller_.AdvanceTime(kDefaultAllocationTimeout);
  test_invariants();

  session_->SetCandidateFilter(CF_RELAY | CF_REFLEXIVE);
  time_controller_.AdvanceTime(kDefaultAllocationTimeout);
  test_invariants();

  session_->SetCandidateFilter(CF_ALL);
  time_controller_.AdvanceTime(kDefaultAllocationTimeout);
  test_invariants();
}

TEST_F(BasicPortAllocatorTest, SetStunKeepaliveIntervalForPorts) {
  const int pool_size = 1;
  const int expected_stun_keepalive_interval = 123;
  AddInterface(kClientAddr);
  allocator_->SetConfiguration(allocator_->stun_servers(),
                               allocator_->turn_servers(), pool_size, NO_PRUNE,
                               nullptr, expected_stun_keepalive_interval);
  auto* pooled_session = allocator_->GetPooledSession();
  ASSERT_NE(nullptr, pooled_session);
  EXPECT_TRUE(waiter_.Until(
      [&] { return pooled_session->CandidatesAllocationDone(); }));
  CheckStunKeepaliveIntervalOfAllReadyPorts(pooled_session,
                                            expected_stun_keepalive_interval);
}

TEST_F(BasicPortAllocatorTest,
       ChangeStunKeepaliveIntervalForPortsAfterInitialConfig) {
  const int pool_size = 1;
  AddInterface(kClientAddr);
  allocator_->SetConfiguration(allocator_->stun_servers(),
                               allocator_->turn_servers(), pool_size, NO_PRUNE,
                               nullptr, 123 /* stun keepalive interval */);
  auto* pooled_session = allocator_->GetPooledSession();
  ASSERT_NE(nullptr, pooled_session);
  EXPECT_TRUE(waiter_.Until(
      [&] { return pooled_session->CandidatesAllocationDone(); }));
  const int expected_stun_keepalive_interval = 321;
  allocator_->SetConfiguration(allocator_->stun_servers(),
                               allocator_->turn_servers(), pool_size, NO_PRUNE,
                               nullptr, expected_stun_keepalive_interval);
  CheckStunKeepaliveIntervalOfAllReadyPorts(pooled_session,
                                            expected_stun_keepalive_interval);
}

TEST_F(BasicPortAllocatorTest,
       SetStunKeepaliveIntervalForPortsWithSharedSocket) {
  const int pool_size = 1;
  const int expected_stun_keepalive_interval = 123;
  AddInterface(kClientAddr);
  allocator_->set_flags(allocator().flags() |
                        PORTALLOCATOR_ENABLE_SHARED_SOCKET);
  allocator_->SetConfiguration(allocator_->stun_servers(),
                               allocator_->turn_servers(), pool_size, NO_PRUNE,
                               nullptr, expected_stun_keepalive_interval);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  CheckStunKeepaliveIntervalOfAllReadyPorts(session_.get(),
                                            expected_stun_keepalive_interval);
}

TEST_F(BasicPortAllocatorTest,
       SetStunKeepaliveIntervalForPortsWithoutSharedSocket) {
  const int pool_size = 1;
  const int expected_stun_keepalive_interval = 123;
  AddInterface(kClientAddr);
  allocator_->set_flags(allocator().flags() &
                        ~(PORTALLOCATOR_ENABLE_SHARED_SOCKET));
  allocator_->SetConfiguration(allocator_->stun_servers(),
                               allocator_->turn_servers(), pool_size, NO_PRUNE,
                               nullptr, expected_stun_keepalive_interval);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  CheckStunKeepaliveIntervalOfAllReadyPorts(session_.get(),
                                            expected_stun_keepalive_interval);
}

// Test that when an mDNS responder is present, the local address of a host
// candidate is concealed by an mDNS hostname and the related address of a srflx
// candidate is set to 0.0.0.0 or ::0.
TEST_F(BasicPortAllocatorTest, HostCandidateAddressIsReplacedByHostname) {
  // Default config uses GTURN and no NAT, so replace that with the
  // desired setup (NAT, STUN server, TURN server, UDP/TCP).
  ResetWithStunServerAndNat(kStunAddr);
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
  AddTurnServers(kTurnUdpIntAddr, kTurnTcpIntAddr);
  AddTurnServers(kTurnUdpIntIPv6Addr, kTurnTcpIntIPv6Addr);

  ASSERT_EQ(&network_manager_, allocator().network_manager());
  network_manager_.set_mdns_responder(
      std::make_unique<FakeMdnsResponder>(Thread::Current()));
  AddInterface(kClientAddr);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  ASSERT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(5u, candidates_.size());
  int num_host_udp_candidates = 0;
  int num_host_tcp_candidates = 0;
  int num_srflx_candidates = 0;
  int num_relay_candidates = 0;
  for (const auto& candidate : candidates_) {
    const auto& raddr = candidate.related_address();

    if (candidate.is_local()) {
      EXPECT_FALSE(candidate.address().hostname().empty());
      EXPECT_TRUE(raddr.IsNil());
      if (candidate.protocol() == UDP_PROTOCOL_NAME) {
        ++num_host_udp_candidates;
      } else {
        ++num_host_tcp_candidates;
      }
    } else if (candidate.is_stun()) {
      // For a srflx candidate, the related address should be set to 0.0.0.0 or
      // ::0
      EXPECT_TRUE(IPIsAny(raddr.ipaddr()));
      EXPECT_EQ(raddr.port(), 0);
      ++num_srflx_candidates;
    } else if (candidate.is_relay()) {
      EXPECT_EQ(kNatUdpAddr.ipaddr(), raddr.ipaddr());
      EXPECT_EQ(kNatUdpAddr.family(), raddr.family());
      ++num_relay_candidates;
    } else {
      // prflx candidates are not expected
      FAIL();
    }
  }
  EXPECT_EQ(1, num_host_udp_candidates);
  EXPECT_EQ(1, num_host_tcp_candidates);
  EXPECT_EQ(1, num_srflx_candidates);
  EXPECT_EQ(2, num_relay_candidates);
}

TEST_F(BasicPortAllocatorTest, TestUseTurnServerAsStunSever) {
  ServerAddresses stun_servers;
  stun_servers.insert(kStunAddr);
  PortConfiguration port_config(stun_servers, "", "");
  RelayServerConfig turn_servers =
      CreateTurnServers(kTurnUdpIntAddr, kTurnTcpIntAddr);
  port_config.AddRelay(turn_servers);

  EXPECT_EQ(2U, port_config.StunServers().size());
}

TEST_F(BasicPortAllocatorTest, TestDoNotUseTurnServerAsStunSever) {
  FieldTrials field_trials =
      CreateTestFieldTrials("WebRTC-UseTurnServerAsStunServer/Disabled/");
  ServerAddresses stun_servers;
  stun_servers.insert(kStunAddr);
  PortConfiguration port_config(stun_servers, "" /* user_name */,
                                "" /* password */, &field_trials);
  RelayServerConfig turn_servers =
      CreateTurnServers(kTurnUdpIntAddr, kTurnTcpIntAddr);
  port_config.AddRelay(turn_servers);

  EXPECT_EQ(1U, port_config.StunServers().size());
}

// Test that a TURN server is used as a STUN server at the address of its UDP
// port, even when that is not the first port of the server.
TEST_F(BasicPortAllocatorTest, TestUseTurnServerUdpPortAsStunServer) {
  PortConfiguration port_config(ServerAddresses(), /*username=*/"",
                                /*password=*/"");
  RelayServerConfig turn_server;
  turn_server.ports.push_back(ProtocolAddress(kTurnTcpIntAddr, PROTO_TCP));
  turn_server.ports.push_back(ProtocolAddress(kTurnUdpIntAddr, PROTO_UDP));
  port_config.AddRelay(turn_server);

  EXPECT_THAT(port_config.StunServers(), ElementsAre(kTurnUdpIntAddr));
}

// Test that candidates from different servers get assigned a unique local
// preference (the middle 16 bits of the priority)
TEST_F(BasicPortAllocatorTest, AssignsUniqueLocalPreferencetoRelayCandidates) {
  allocator_->SetCandidateFilter(CF_RELAY);
  allocator_->AddTurnServerForTesting(
      CreateTurnServers(kTurnUdpIntAddr, SocketAddress()));
  allocator_->AddTurnServerForTesting(
      CreateTurnServers(kTurnUdpIntAddr, SocketAddress()));
  allocator_->AddTurnServerForTesting(
      CreateTurnServers(kTurnUdpIntAddr, SocketAddress()));

  AddInterface(kClientAddr);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  ASSERT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));
  EXPECT_EQ(3u, candidates_.size());
  EXPECT_GT((candidates_[0].priority() >> 8) & 0xFFFF,
            (candidates_[1].priority() >> 8) & 0xFFFF);
  EXPECT_GT((candidates_[1].priority() >> 8) & 0xFFFF,
            (candidates_[2].priority() >> 8) & 0xFFFF);
}

// Test that no more than allocator.max_ipv6_networks() IPv6 networks are used
// to gather candidates.
TEST_F(BasicPortAllocatorTest, TwoIPv6AreSelectedBecauseOfMaxIpv6Limit) {
  Network wifi1("wifi1", "Test NetworkAdapter 1", kClientIPv6Addr.ipaddr(), 64,
                ADAPTER_TYPE_WIFI);
  Network ethe1("ethe1", "Test NetworkAdapter 2", kClientIPv6Addr2.ipaddr(), 64,
                ADAPTER_TYPE_ETHERNET);
  Network wifi2("wifi2", "Test NetworkAdapter 3", kClientIPv6Addr3.ipaddr(), 64,
                ADAPTER_TYPE_WIFI);
  std::vector<const Network*> networks = {&wifi1, &ethe1, &wifi2};

  // Ensure that only 2 interfaces were selected.
  EXPECT_EQ(2U, BasicPortAllocatorSession::SelectIPv6Networks(
                    networks, /*max_ipv6_networks=*/2)
                    .size());
}

// Test that if the number of available IPv6 networks is less than
// allocator.max_ipv6_networks(), all IPv6 networks will be selected.
TEST_F(BasicPortAllocatorTest, AllIPv6AreSelected) {
  Network wifi1("wifi1", "Test NetworkAdapter 1", kClientIPv6Addr.ipaddr(), 64,
                ADAPTER_TYPE_WIFI);
  Network ethe1("ethe1", "Test NetworkAdapter 2", kClientIPv6Addr2.ipaddr(), 64,
                ADAPTER_TYPE_ETHERNET);
  std::vector<const Network*> networks = {&wifi1, &ethe1};

  // Ensure that all 2 interfaces were selected.
  EXPECT_EQ(2U, BasicPortAllocatorSession::SelectIPv6Networks(
                    networks, /*max_ipv6_networks=*/3)
                    .size());
}

// If there are some IPv6 networks with different types, diversify IPv6
// networks.
TEST_F(BasicPortAllocatorTest, TwoIPv6WifiAreSelectedIfThereAreTwo) {
  Network wifi1("wifi1", "Test NetworkAdapter 1", kClientIPv6Addr.ipaddr(), 64,
                ADAPTER_TYPE_WIFI);
  Network ethe1("ethe1", "Test NetworkAdapter 2", kClientIPv6Addr2.ipaddr(), 64,
                ADAPTER_TYPE_ETHERNET);
  Network ethe2("ethe2", "Test NetworkAdapter 3", kClientIPv6Addr3.ipaddr(), 64,
                ADAPTER_TYPE_ETHERNET);
  Network unknown1("unknown1", "Test NetworkAdapter 4",
                   kClientIPv6Addr2.ipaddr(), 64, ADAPTER_TYPE_UNKNOWN);
  Network cell1("cell1", "Test NetworkAdapter 5", kClientIPv6Addr3.ipaddr(), 64,
                ADAPTER_TYPE_CELLULAR_4G);
  std::vector<const Network*> networks = {&wifi1, &ethe1, &ethe2, &unknown1,
                                          &cell1};

  networks = BasicPortAllocatorSession::SelectIPv6Networks(
      networks, /*max_ipv6_networks=*/4);

  EXPECT_EQ(4U, networks.size());
  // Ensure the expected 4 interfaces (wifi1, ethe1, cell1, unknown1) were
  // selected.
  EXPECT_TRUE(HasNetwork(networks, wifi1));
  EXPECT_TRUE(HasNetwork(networks, ethe1));
  EXPECT_TRUE(HasNetwork(networks, cell1));
  EXPECT_TRUE(HasNetwork(networks, unknown1));
}

// If there are some IPv6 networks with the same type, select them because there
// is no other option.
TEST_F(BasicPortAllocatorTest, IPv6WithSameTypeAreSelectedIfNoOtherOption) {
  // Add 5 cellular interfaces
  Network cell1("cell1", "Test NetworkAdapter 1", kClientIPv6Addr.ipaddr(), 64,
                ADAPTER_TYPE_CELLULAR_2G);
  Network cell2("cell2", "Test NetworkAdapter 2", kClientIPv6Addr2.ipaddr(), 64,
                ADAPTER_TYPE_CELLULAR_3G);
  Network cell3("cell3", "Test NetworkAdapter 3", kClientIPv6Addr3.ipaddr(), 64,
                ADAPTER_TYPE_CELLULAR_4G);
  Network cell4("cell4", "Test NetworkAdapter 4", kClientIPv6Addr2.ipaddr(), 64,
                ADAPTER_TYPE_CELLULAR_5G);
  Network cell5("cell5", "Test NetworkAdapter 5", kClientIPv6Addr3.ipaddr(), 64,
                ADAPTER_TYPE_CELLULAR_3G);
  std::vector<const Network*> networks = {&cell1, &cell2, &cell3, &cell4,
                                          &cell5};

  // Ensure that 4 interfaces were selected.
  EXPECT_EQ(4U, BasicPortAllocatorSession::SelectIPv6Networks(
                    networks, /*max_ipv6_networks=*/4)
                    .size());
}

TEST_F(BasicPortAllocatorTest, IPv6EthernetHasHigherPriorityThanWifi) {
  Network wifi1("wifi1", "Test NetworkAdapter 1", kClientIPv6Addr.ipaddr(), 64,
                ADAPTER_TYPE_WIFI);
  Network ethe1("ethe1", "Test NetworkAdapter 2", kClientIPv6Addr2.ipaddr(), 64,
                ADAPTER_TYPE_ETHERNET);
  Network wifi2("wifi2", "Test NetworkAdapter 3", kClientIPv6Addr3.ipaddr(), 64,
                ADAPTER_TYPE_WIFI);
  std::vector<const Network*> networks = {&wifi1, &ethe1, &wifi2};

  networks = BasicPortAllocatorSession::SelectIPv6Networks(
      networks, /*max_ipv6_networks=*/1);

  EXPECT_EQ(1U, networks.size());
  // Ensure ethe1 was selected.
  EXPECT_TRUE(HasNetwork(networks, ethe1));
}

TEST_F(BasicPortAllocatorTest, IPv6EtherAndWifiHaveHigherPriorityThanOthers) {
  Network cell1("cell1", "Test NetworkAdapter 1", kClientIPv6Addr.ipaddr(), 64,
                ADAPTER_TYPE_CELLULAR_3G);
  Network ethe1("ethe1", "Test NetworkAdapter 2", kClientIPv6Addr2.ipaddr(), 64,
                ADAPTER_TYPE_ETHERNET);
  Network wifi1("wifi1", "Test NetworkAdapter 3", kClientIPv6Addr3.ipaddr(), 64,
                ADAPTER_TYPE_WIFI);
  Network unknown("unknown", "Test NetworkAdapter 4", kClientIPv6Addr2.ipaddr(),
                  64, ADAPTER_TYPE_UNKNOWN);
  Network vpn1("vpn1", "Test NetworkAdapter 5", kClientIPv6Addr3.ipaddr(), 64,
               ADAPTER_TYPE_VPN);
  std::vector<const Network*> networks = {&cell1, &ethe1, &wifi1, &unknown,
                                          &vpn1};

  networks = BasicPortAllocatorSession::SelectIPv6Networks(
      networks, /*max_ipv6_networks=*/2);

  EXPECT_EQ(2U, networks.size());
  // Ensure ethe1 and wifi1 were selected.
  EXPECT_TRUE(HasNetwork(networks, wifi1));
  EXPECT_TRUE(HasNetwork(networks, ethe1));
}

TEST_F(BasicPortAllocatorTest, Select2DifferentIntefaces) {
  allocator().set_max_ipv6_networks(2);
  AddInterface(kClientIPv6Addr, "ethe1", ADAPTER_TYPE_ETHERNET);
  AddInterface(kClientIPv6Addr2, "ethe2", ADAPTER_TYPE_ETHERNET);
  AddInterface(kClientIPv6Addr3, "wifi1", ADAPTER_TYPE_WIFI);
  AddInterface(kClientIPv6Addr4, "wifi2", ADAPTER_TYPE_WIFI);
  AddInterface(kClientIPv6Addr5, "cell1", ADAPTER_TYPE_CELLULAR_3G);

  // To simplify the test, only gather UDP host candidates.
  allocator().set_flags(PORTALLOCATOR_ENABLE_IPV6 | PORTALLOCATOR_DISABLE_TCP |
                        PORTALLOCATOR_DISABLE_STUN |
                        PORTALLOCATOR_DISABLE_RELAY |
                        PORTALLOCATOR_ENABLE_IPV6_ON_WIFI);

  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));

  EXPECT_EQ(2U, candidates_.size());
  // ethe1 and wifi1 were selected.
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kHost, "udp",
                           kClientIPv6Addr));
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kHost, "udp",
                           kClientIPv6Addr3));
}

TEST_F(BasicPortAllocatorTest, Select3DifferentIntefaces) {
  allocator().set_max_ipv6_networks(3);
  AddInterface(kClientIPv6Addr, "ethe1", ADAPTER_TYPE_ETHERNET);
  AddInterface(kClientIPv6Addr2, "ethe2", ADAPTER_TYPE_ETHERNET);
  AddInterface(kClientIPv6Addr3, "wifi1", ADAPTER_TYPE_WIFI);
  AddInterface(kClientIPv6Addr4, "wifi2", ADAPTER_TYPE_WIFI);
  AddInterface(kClientIPv6Addr5, "cell1", ADAPTER_TYPE_CELLULAR_3G);

  // To simplify the test, only gather UDP host candidates.
  allocator().set_flags(PORTALLOCATOR_ENABLE_IPV6 | PORTALLOCATOR_DISABLE_TCP |
                        PORTALLOCATOR_DISABLE_STUN |
                        PORTALLOCATOR_DISABLE_RELAY |
                        PORTALLOCATOR_ENABLE_IPV6_ON_WIFI);

  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));

  EXPECT_EQ(3U, candidates_.size());
  // ethe1, wifi1, and cell1 were selected.
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kHost, "udp",
                           kClientIPv6Addr));
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kHost, "udp",
                           kClientIPv6Addr3));
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kHost, "udp",
                           kClientIPv6Addr5));
}

TEST_F(BasicPortAllocatorTest, Select4DifferentIntefaces) {
  allocator().set_max_ipv6_networks(4);
  AddInterface(kClientIPv6Addr, "ethe1", ADAPTER_TYPE_ETHERNET);
  AddInterface(kClientIPv6Addr2, "ethe2", ADAPTER_TYPE_ETHERNET);
  AddInterface(kClientIPv6Addr3, "wifi1", ADAPTER_TYPE_WIFI);
  AddInterface(kClientIPv6Addr4, "wifi2", ADAPTER_TYPE_WIFI);
  AddInterface(kClientIPv6Addr5, "cell1", ADAPTER_TYPE_CELLULAR_3G);

  // To simplify the test, only gather UDP host candidates.
  allocator().set_flags(PORTALLOCATOR_ENABLE_IPV6 | PORTALLOCATOR_DISABLE_TCP |
                        PORTALLOCATOR_DISABLE_STUN |
                        PORTALLOCATOR_DISABLE_RELAY |
                        PORTALLOCATOR_ENABLE_IPV6_ON_WIFI);

  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE(waiter_.Until([&] { return candidate_allocation_done_; }));

  EXPECT_EQ(4U, candidates_.size());
  // ethe1, ethe2, wifi1, and cell1 were selected.
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kHost, "udp",
                           kClientIPv6Addr));
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kHost, "udp",
                           kClientIPv6Addr2));
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kHost, "udp",
                           kClientIPv6Addr3));
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kHost, "udp",
                           kClientIPv6Addr5));
}

}  // namespace
}  // namespace webrtc
