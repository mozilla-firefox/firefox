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

#include <memory>
#include <ostream>  // no-presubmit-check TODO(webrtc:8982)

#include "absl/algorithm/container.h"
#include "absl/strings/string_view.h"
#include "p2p/base/basic_packet_socket_factory.h"
#include "p2p/base/p2p_constants.h"
#include "p2p/base/stun_port.h"
#include "p2p/base/stun_request.h"
#include "p2p/base/stun_server.h"
#include "p2p/base/test_stun_server.h"
#include "p2p/base/test_turn_server.h"
#include "rtc_base/fake_clock.h"
#include "rtc_base/fake_mdns_responder.h"
#include "rtc_base/fake_network.h"
#include "rtc_base/firewall_socket_server.h"
#include "rtc_base/gunit.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/logging.h"
#include "rtc_base/nat_server.h"
#include "rtc_base/nat_socket_factory.h"
#include "rtc_base/nat_types.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/net_helpers.h"
#include "rtc_base/net_test_helpers.h"
#include "rtc_base/network.h"
#include "rtc_base/network_constants.h"
#include "rtc_base/network_monitor.h"
#include "rtc_base/socket.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/socket_address_pair.h"
#include "rtc_base/thread.h"
#include "rtc_base/virtual_socket_server.h"
#include "system_wrappers/include/metrics.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/scoped_key_value_config.h"

using rtc::IPAddress;
using rtc::SocketAddress;
using testing::Contains;
using testing::Not;
using webrtc::IceCandidateType;

#define MAYBE_SKIP_IPV4                        \
  if (!rtc::HasIPv4Enabled()) {                \
    RTC_LOG(LS_INFO) << "No IPv4... skipping"; \
    return;                                    \
  }

static const SocketAddress kAnyAddr("0.0.0.0", 0);
static const SocketAddress kClientAddr("11.11.11.11", 0);
static const SocketAddress kClientAddr2("22.22.22.22", 0);
static const SocketAddress kLoopbackAddr("127.0.0.1", 0);
static const SocketAddress kPrivateAddr("192.168.1.11", 0);
static const SocketAddress kPrivateAddr2("192.168.1.12", 0);
static const SocketAddress kClientIPv6Addr("2401:fa00:4:1000:be30:5bff:fee5:c3",
                                           0);
static const SocketAddress kClientIPv6Addr2(
    "2401:fa00:4:2000:be30:5bff:fee5:c3",
    0);
static const SocketAddress kClientIPv6Addr3(
    "2401:fa00:4:3000:be30:5bff:fee5:c3",
    0);
static const SocketAddress kClientIPv6Addr4(
    "2401:fa00:4:4000:be30:5bff:fee5:c3",
    0);
static const SocketAddress kClientIPv6Addr5(
    "2401:fa00:4:5000:be30:5bff:fee5:c3",
    0);
static const SocketAddress kNatUdpAddr("77.77.77.77", rtc::NAT_SERVER_UDP_PORT);
static const SocketAddress kNatTcpAddr("77.77.77.77", rtc::NAT_SERVER_TCP_PORT);
static const SocketAddress kRemoteClientAddr("22.22.22.22", 0);
static const SocketAddress kStunAddr("99.99.99.1", cricket::STUN_SERVER_PORT);
static const SocketAddress kTurnUdpIntAddr("99.99.99.4", 3478);
static const SocketAddress kTurnUdpIntIPv6Addr(
    "2402:fb00:4:1000:be30:5bff:fee5:c3",
    3479);
static const SocketAddress kTurnTcpIntAddr("99.99.99.5", 3478);
static const SocketAddress kTurnTcpIntIPv6Addr(
    "2402:fb00:4:2000:be30:5bff:fee5:c3",
    3479);
static const SocketAddress kTurnUdpExtAddr("99.99.99.6", 0);

// Minimum and maximum port for port range tests.
static const int kMinPort = 10000;
static const int kMaxPort = 10099;

// Based on ICE_UFRAG_LENGTH
static const char kIceUfrag0[] = "UF00";
// Based on ICE_PWD_LENGTH
static const char kIcePwd0[] = "TESTICEPWD00000000000000";

static const char kContentName[] = "test content";

static const int kDefaultAllocationTimeout = 3000;
static const char kTurnUsername[] = "test";
static const char kTurnPassword[] = "test";

// STUN timeout (with all retries) is cricket::STUN_TOTAL_TIMEOUT.
// Add some margin of error for slow bots.
static const int kStunTimeoutMs = cricket::STUN_TOTAL_TIMEOUT;

namespace {

void CheckStunKeepaliveIntervalOfAllReadyPorts(
    const cricket::PortAllocatorSession* allocator_session,
    int expected) {
  auto ready_ports = allocator_session->ReadyPorts();
  for (const auto* port : ready_ports) {
    if (port->Type() == IceCandidateType::kSrflx ||
        (port->Type() == IceCandidateType::kHost &&
         port->GetProtocol() == cricket::PROTO_UDP)) {
      EXPECT_EQ(
          static_cast<const cricket::UDPPort*>(port)->stun_keepalive_delay(),
          expected);
    }
  }
}

}  // namespace

namespace cricket {

// Helper for dumping candidates
std::ostream& operator<<(std::ostream& os,
                         const std::vector<Candidate>& candidates) {
  os << '[';
  bool first = true;
  for (const Candidate& c : candidates) {
    if (!first) {
      os << ", ";
    }
    os << c.ToString();
    first = false;
  }
  os << ']';
  return os;
}

class BasicPortAllocatorTestBase : public ::testing::Test,
                                   public sigslot::has_slots<> {
 public:
  BasicPortAllocatorTestBase()
      : vss_(new rtc::VirtualSocketServer()),
        fss_(new rtc::FirewallSocketServer(vss_.get())),
        socket_factory_(fss_.get()),
        thread_(fss_.get()),
        // Note that the NAT is not used by default. ResetWithStunServerAndNat
        // must be called.
        nat_factory_(vss_.get(), kNatUdpAddr, kNatTcpAddr),
        nat_socket_factory_(new rtc::BasicPacketSocketFactory(&nat_factory_)),
        stun_server_(TestStunServer::Create(fss_.get(), kStunAddr, thread_)),
        turn_server_(rtc::Thread::Current(),
                     fss_.get(),
                     kTurnUdpIntAddr,
                     kTurnUdpExtAddr),
        candidate_allocation_done_(false) {
    ServerAddresses stun_servers;
    stun_servers.insert(kStunAddr);

    allocator_ = std::make_unique<BasicPortAllocator>(
        &network_manager_, &socket_factory_, stun_servers, &field_trials_);
    allocator_->Initialize();
    allocator_->set_step_delay(kMinimumStepDelay);
    webrtc::metrics::Reset();
  }

  void AddInterface(const SocketAddress& addr) {
    network_manager_.AddInterface(addr);
  }
  void AddInterface(const SocketAddress& addr, absl::string_view if_name) {
    network_manager_.AddInterface(addr, if_name);
  }
  void AddInterface(const SocketAddress& addr,
                    absl::string_view if_name,
                    rtc::AdapterType type) {
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
    allocator_.reset(
        new BasicPortAllocator(&network_manager_, &socket_factory_));
    allocator_->Initialize();
    allocator_->set_step_delay(kMinimumStepDelay);
  }
  // Endpoint is behind a NAT, with STUN specified.
  void ResetWithStunServerAndNat(const rtc::SocketAddress& stun_server) {
    ResetWithStunServer(stun_server, true);
  }
  // Endpoint is on the public network, with STUN specified.
  void ResetWithStunServerNoNat(const rtc::SocketAddress& stun_server) {
    ResetWithStunServer(stun_server, false);
  }
  // Endpoint is on the public network, with TURN specified.
  void ResetWithTurnServersNoNat(const rtc::SocketAddress& udp_turn,
                                 const rtc::SocketAddress& tcp_turn) {
    ResetWithNoServersOrNat();
    AddTurnServers(udp_turn, tcp_turn);
  }

  RelayServerConfig CreateTurnServers(const rtc::SocketAddress& udp_turn,
                                      const rtc::SocketAddress& tcp_turn) {
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

  void AddTurnServers(const rtc::SocketAddress& udp_turn,
                      const rtc::SocketAddress& tcp_turn) {
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
    session->SignalPortReady.connect(this,
                                     &BasicPortAllocatorTestBase::OnPortReady);
    session->SignalPortsPruned.connect(
        this, &BasicPortAllocatorTestBase::OnPortsPruned);
    session->SignalCandidatesReady.connect(
        this, &BasicPortAllocatorTestBase::OnCandidatesReady);
    session->SignalCandidatesRemoved.connect(
        this, &BasicPortAllocatorTestBase::OnCandidatesRemoved);
    session->SignalCandidatesAllocationDone.connect(
        this, &BasicPortAllocatorTestBase::OnCandidatesAllocationDone);
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

  static bool CheckPort(const rtc::SocketAddress& addr,
                        int min_port,
                        int max_port) {
    return (addr.port() >= min_port && addr.port() <= max_port);
  }

  static bool HasNetwork(const std::vector<const rtc::Network*>& networks,
                         const rtc::Network& to_be_found) {
    auto it =
        absl::c_find_if(networks, [to_be_found](const rtc::Network* network) {
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
                  (*it)->GetOption(rtc::Socket::OPT_SNDBUF, &send_buffer_size));
      } else {
        EXPECT_EQ(0,
                  (*it)->GetOption(rtc::Socket::OPT_SNDBUF, &send_buffer_size));
        ASSERT_EQ(expected, send_buffer_size);
      }
    }
  }

  rtc::VirtualSocketServer* virtual_socket_server() { return vss_.get(); }

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
    auto new_end = std::remove_if(
        candidates_.begin(), candidates_.end(),
        [removed_candidates](Candidate& candidate) {
          for (const Candidate& removed_candidate : removed_candidates) {
            if (candidate.MatchesForRemoval(removed_candidate)) {
              return true;
            }
          }
          return false;
        });
    candidates_.erase(new_end, candidates_.end());
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

  void ResetWithStunServer(const rtc::SocketAddress& stun_server,
                           bool with_nat) {
    if (with_nat) {
      nat_server_.reset(new rtc::NATServer(
          rtc::NAT_OPEN_CONE, thread_, vss_.get(), kNatUdpAddr, kNatTcpAddr,
          thread_, vss_.get(), rtc::SocketAddress(kNatUdpAddr.ipaddr(), 0)));
    } else {
      nat_socket_factory_ =
          std::make_unique<rtc::BasicPacketSocketFactory>(fss_.get());
    }

    ServerAddresses stun_servers;
    if (!stun_server.IsNil()) {
      stun_servers.insert(stun_server);
    }
    allocator_.reset(new BasicPortAllocator(&network_manager_,
                                            nat_socket_factory_.get(),
                                            stun_servers, &field_trials_));
    allocator_->Initialize();
    allocator_->set_step_delay(kMinimumStepDelay);
  }

  std::unique_ptr<rtc::VirtualSocketServer> vss_;
  std::unique_ptr<rtc::FirewallSocketServer> fss_;
  rtc::BasicPacketSocketFactory socket_factory_;
  rtc::AutoSocketServerThread thread_;
  std::unique_ptr<rtc::NATServer> nat_server_;
  rtc::NATSocketFactory nat_factory_;
  std::unique_ptr<rtc::BasicPacketSocketFactory> nat_socket_factory_;
  TestStunServer::StunServerPtr stun_server_;
  TestTurnServer turn_server_;
  rtc::FakeNetworkManager network_manager_;
  std::unique_ptr<BasicPortAllocator> allocator_;
  std::unique_ptr<PortAllocatorSession> session_;
  std::vector<PortInterface*> ports_;
  std::vector<Candidate> candidates_;
  bool candidate_allocation_done_;
  webrtc::test::ScopedKeyValueConfig field_trials_;
};

class BasicPortAllocatorTestWithRealClock : public BasicPortAllocatorTestBase {
};

class FakeClockBase {
 public:
  rtc::ScopedFakeClock fake_clock;
};

class BasicPortAllocatorTest : public FakeClockBase,
                               public BasicPortAllocatorTestBase {
 public:
  // This function starts the port/address gathering and check the existence of
  // candidates as specified. When `expect_stun_candidate` is true,
  // `stun_candidate_addr` carries the expected reflective address, which is
  // also the related address for TURN candidate if it is expected. Otherwise,
  // it should be ignore.
  void CheckDisableAdapterEnumeration(
      uint32_t total_ports,
      const rtc::IPAddress& host_candidate_addr,
      const rtc::IPAddress& stun_candidate_addr,
      const rtc::IPAddress& relay_candidate_udp_transport_addr,
      const rtc::IPAddress& relay_candidate_tcp_transport_addr) {
    network_manager_.set_default_local_addresses(kPrivateAddr.ipaddr(),
                                                 rtc::IPAddress());
    if (!session_) {
      ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
    }
    session_->set_flags(session_->flags() |
                        PORTALLOCATOR_DISABLE_ADAPTER_ENUMERATION |
                        PORTALLOCATOR_ENABLE_SHARED_SOCKET);
    allocator().set_allow_tcp_listen(false);
    session_->StartGettingPorts();
    EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                               kDefaultAllocationTimeout, fake_clock);

    uint32_t total_candidates = 0;
    if (!host_candidate_addr.IsNil()) {
      EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kHost, "udp",
                               rtc::SocketAddress(kPrivateAddr.ipaddr(), 0)));
      ++total_candidates;
    }
    if (!stun_candidate_addr.IsNil()) {
      rtc::SocketAddress related_address(host_candidate_addr, 0);
      if (host_candidate_addr.IsNil()) {
        related_address.SetIP(rtc::GetAnyIP(stun_candidate_addr.family()));
      }
      EXPECT_TRUE(HasCandidateWithRelatedAddr(
          candidates_, IceCandidateType::kSrflx, "udp",
          rtc::SocketAddress(stun_candidate_addr, 0), related_address));
      ++total_candidates;
    }
    if (!relay_candidate_udp_transport_addr.IsNil()) {
      EXPECT_TRUE(HasCandidateWithRelatedAddr(
          candidates_, IceCandidateType::kRelay, "udp",
          rtc::SocketAddress(relay_candidate_udp_transport_addr, 0),
          rtc::SocketAddress(stun_candidate_addr, 0)));
      ++total_candidates;
    }
    if (!relay_candidate_tcp_transport_addr.IsNil()) {
      EXPECT_TRUE(HasCandidateWithRelatedAddr(
          candidates_, IceCandidateType::kRelay, "udp",
          rtc::SocketAddress(relay_candidate_tcp_transport_addr, 0),
          rtc::SocketAddress(stun_candidate_addr, 0)));
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
    allocator_.reset(
        new BasicPortAllocator(&network_manager_, &socket_factory_));
    allocator_->Initialize();
    allocator_->SetConfiguration(allocator_->stun_servers(),
                                 allocator_->turn_servers(), 0,
                                 webrtc::PRUNE_BASED_ON_PRIORITY);
    AddTurnServers(kTurnUdpIntIPv6Addr, rtc::SocketAddress());
    AddTurnServers(kTurnUdpIntAddr, rtc::SocketAddress());

    allocator_->set_step_delay(kMinimumStepDelay);
    allocator_->set_flags(
        allocator().flags() | PORTALLOCATOR_ENABLE_SHARED_SOCKET |
        PORTALLOCATOR_ENABLE_IPV6 | PORTALLOCATOR_DISABLE_TCP);

    ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
    session_->StartGettingPorts();
    EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                               kDefaultAllocationTimeout, fake_clock);
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
                             rtc::SocketAddress(kTurnUdpExtAddr.ipaddr(), 0)));
  }

  void TestTurnPortPrunesWithUdpAndTcpPorts(
      webrtc::PortPrunePolicy prune_policy,
      bool tcp_pruned) {
    turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
    AddInterface(kClientAddr);
    allocator_.reset(
        new BasicPortAllocator(&network_manager_, &socket_factory_));
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
    EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                               kDefaultAllocationTimeout, fake_clock);
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
                             rtc::SocketAddress(kTurnUdpExtAddr.ipaddr(), 0)));
  }

  void TestEachInterfaceHasItsOwnTurnPorts() {
    turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
    turn_server_.AddInternalSocket(kTurnUdpIntIPv6Addr, PROTO_UDP);
    turn_server_.AddInternalSocket(kTurnTcpIntIPv6Addr, PROTO_TCP);
    // Add two interfaces both having IPv4 and IPv6 addresses.
    AddInterface(kClientAddr, "net1", rtc::ADAPTER_TYPE_WIFI);
    AddInterface(kClientIPv6Addr, "net1", rtc::ADAPTER_TYPE_WIFI);
    AddInterface(kClientAddr2, "net2", rtc::ADAPTER_TYPE_CELLULAR);
    AddInterface(kClientIPv6Addr2, "net2", rtc::ADAPTER_TYPE_CELLULAR);
    allocator_.reset(
        new BasicPortAllocator(&network_manager_, &socket_factory_));
    allocator_->Initialize();
    allocator_->SetConfiguration(allocator_->stun_servers(),
                                 allocator_->turn_servers(), 0,
                                 webrtc::PRUNE_BASED_ON_PRIORITY);
    // Have both UDP/TCP and IPv4/IPv6 TURN ports.
    AddTurnServers(kTurnUdpIntAddr, kTurnTcpIntAddr);
    AddTurnServers(kTurnUdpIntIPv6Addr, kTurnTcpIntIPv6Addr);

    allocator_->set_step_delay(kMinimumStepDelay);
    allocator_->set_flags(
        allocator().flags() | PORTALLOCATOR_ENABLE_SHARED_SOCKET |
        PORTALLOCATOR_ENABLE_IPV6 | PORTALLOCATOR_ENABLE_IPV6_ON_WIFI);
    ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
    session_->StartGettingPorts();
    EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                               kDefaultAllocationTimeout, fake_clock);
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
                             rtc::SocketAddress(kTurnUdpExtAddr.ipaddr(), 0)));
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
               rtc::ADAPTER_TYPE_ETHERNET);
  AddInterface(SocketAddress(IPAddress(0x12345601U), 0), "test_wlan0",
               rtc::ADAPTER_TYPE_WIFI);
  AddInterface(SocketAddress(IPAddress(0x12345602U), 0), "test_cell0",
               rtc::ADAPTER_TYPE_CELLULAR);
  AddInterface(SocketAddress(IPAddress(0x12345603U), 0), "test_vpn0",
               rtc::ADAPTER_TYPE_VPN);
  AddInterface(SocketAddress(IPAddress(0x12345604U), 0), "test_lo",
               rtc::ADAPTER_TYPE_LOOPBACK);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->set_flags(PORTALLOCATOR_DISABLE_STUN | PORTALLOCATOR_DISABLE_RELAY |
                      PORTALLOCATOR_DISABLE_TCP);
  session_->StartGettingPorts();
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
  EXPECT_EQ(4U, candidates_.size());
  for (const Candidate& candidate : candidates_) {
    EXPECT_LT(candidate.address().ip(), 0x12345604U);
  }
}

TEST_F(BasicPortAllocatorTest, TestIgnoreNetworksAccordingToIgnoreMask) {
  AddInterface(SocketAddress(IPAddress(0x12345600U), 0), "test_eth0",
               rtc::ADAPTER_TYPE_ETHERNET);
  AddInterface(SocketAddress(IPAddress(0x12345601U), 0), "test_wlan0",
               rtc::ADAPTER_TYPE_WIFI);
  AddInterface(SocketAddress(IPAddress(0x12345602U), 0), "test_cell0",
               rtc::ADAPTER_TYPE_CELLULAR);
  allocator_->SetNetworkIgnoreMask(rtc::ADAPTER_TYPE_ETHERNET |
                                   rtc::ADAPTER_TYPE_LOOPBACK |
                                   rtc::ADAPTER_TYPE_WIFI);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->set_flags(PORTALLOCATOR_DISABLE_STUN | PORTALLOCATOR_DISABLE_RELAY |
                      PORTALLOCATOR_DISABLE_TCP);
  session_->StartGettingPorts();
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
  EXPECT_EQ(1U, candidates_.size());
  EXPECT_EQ(0x12345602U, candidates_[0].address().ip());
}

// Test that when the PORTALLOCATOR_DISABLE_COSTLY_NETWORKS flag is set and
// both Wi-Fi and cell interfaces are available, only Wi-Fi is used.
TEST_F(BasicPortAllocatorTest,
       WifiUsedInsteadOfCellWhenCostlyNetworksDisabled) {
  SocketAddress wifi(IPAddress(0x12345600U), 0);
  SocketAddress cell(IPAddress(0x12345601U), 0);
  AddInterface(wifi, "test_wlan0", rtc::ADAPTER_TYPE_WIFI);
  AddInterface(cell, "test_cell0", rtc::ADAPTER_TYPE_CELLULAR);
  // Disable all but UDP candidates to make the test simpler.
  allocator().set_flags(cricket::PORTALLOCATOR_DISABLE_STUN |
                        cricket::PORTALLOCATOR_DISABLE_RELAY |
                        cricket::PORTALLOCATOR_DISABLE_TCP |
                        cricket::PORTALLOCATOR_DISABLE_COSTLY_NETWORKS);
  ASSERT_TRUE(CreateSession(cricket::ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  AddInterface(cell, "test_cell0", rtc::ADAPTER_TYPE_CELLULAR);
  AddInterface(unknown1, "test_unknown0", rtc::ADAPTER_TYPE_UNKNOWN);
  AddInterface(unknown2, "test_unknown1", rtc::ADAPTER_TYPE_UNKNOWN);
  // Disable all but UDP candidates to make the test simpler.
  allocator().set_flags(cricket::PORTALLOCATOR_DISABLE_STUN |
                        cricket::PORTALLOCATOR_DISABLE_RELAY |
                        cricket::PORTALLOCATOR_DISABLE_TCP |
                        cricket::PORTALLOCATOR_DISABLE_COSTLY_NETWORKS);
  ASSERT_TRUE(CreateSession(cricket::ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  AddInterface(wifi, "test_wlan0", rtc::ADAPTER_TYPE_WIFI);
  AddInterface(cellular, "test_cell0", rtc::ADAPTER_TYPE_CELLULAR);
  AddInterface(unknown1, "test_unknown0", rtc::ADAPTER_TYPE_UNKNOWN);
  AddInterface(unknown2, "test_unknown1", rtc::ADAPTER_TYPE_UNKNOWN);
  // Disable all but UDP candidates to make the test simpler.
  allocator().set_flags(cricket::PORTALLOCATOR_DISABLE_STUN |
                        cricket::PORTALLOCATOR_DISABLE_RELAY |
                        cricket::PORTALLOCATOR_DISABLE_TCP |
                        cricket::PORTALLOCATOR_DISABLE_COSTLY_NETWORKS);
  ASSERT_TRUE(CreateSession(cricket::ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  AddInterface(cellular, "test_cell0", rtc::ADAPTER_TYPE_CELLULAR);
  // Disable all but UDP candidates to make the test simpler.
  allocator().set_flags(cricket::PORTALLOCATOR_DISABLE_STUN |
                        cricket::PORTALLOCATOR_DISABLE_RELAY |
                        cricket::PORTALLOCATOR_DISABLE_TCP |
                        cricket::PORTALLOCATOR_DISABLE_COSTLY_NETWORKS);
  ASSERT_TRUE(CreateSession(cricket::ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  AddInterface(wifi_link_local, "test_wlan0", rtc::ADAPTER_TYPE_WIFI);
  AddInterface(cellular, "test_cell0", rtc::ADAPTER_TYPE_CELLULAR);

  allocator().set_flags(cricket::PORTALLOCATOR_DISABLE_STUN |
                        cricket::PORTALLOCATOR_DISABLE_RELAY |
                        cricket::PORTALLOCATOR_DISABLE_TCP |
                        cricket::PORTALLOCATOR_DISABLE_COSTLY_NETWORKS);
  ASSERT_TRUE(CreateSession(cricket::ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  AddInterface(wifi, "test_wlan0", rtc::ADAPTER_TYPE_WIFI);
  AddInterface(wifi_link_local, "test_wlan1", rtc::ADAPTER_TYPE_WIFI);
  AddInterface(cellular, "test_cell0", rtc::ADAPTER_TYPE_CELLULAR);

  allocator().set_flags(cricket::PORTALLOCATOR_DISABLE_STUN |
                        cricket::PORTALLOCATOR_DISABLE_RELAY |
                        cricket::PORTALLOCATOR_DISABLE_TCP |
                        cricket::PORTALLOCATOR_DISABLE_COSTLY_NETWORKS);
  ASSERT_TRUE(CreateSession(cricket::ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  AddInterface(kClientAddr, "eth0", rtc::ADAPTER_TYPE_ETHERNET);
  AddInterface(kClientAddr2, "tap0", rtc::ADAPTER_TYPE_VPN);
  allocator().set_flags(PORTALLOCATOR_DISABLE_COSTLY_NETWORKS |
                        PORTALLOCATOR_DISABLE_RELAY |
                        PORTALLOCATOR_DISABLE_TCP);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  ASSERT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  AddInterface(kClientIPv6Addr, "eth0", rtc::ADAPTER_TYPE_ETHERNET);
  AddInterface(kClientIPv6Addr2, "eth1", rtc::ADAPTER_TYPE_ETHERNET);
  AddInterface(kClientIPv6Addr3, "eth2", rtc::ADAPTER_TYPE_ETHERNET);

  // To simplify the test, only gather UDP host candidates.
  allocator().set_flags(PORTALLOCATOR_ENABLE_IPV6 | PORTALLOCATOR_DISABLE_TCP |
                        PORTALLOCATOR_DISABLE_STUN |
                        PORTALLOCATOR_DISABLE_RELAY);

  ASSERT_TRUE(CreateSession(cricket::ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  AddInterface(kClientIPv6Addr, "eth0", rtc::ADAPTER_TYPE_ETHERNET);
  AddInterface(kClientIPv6Addr2, "eth1", rtc::ADAPTER_TYPE_ETHERNET);
  AddInterface(kClientAddr, "eth2", rtc::ADAPTER_TYPE_ETHERNET);
  AddInterface(kClientAddr2, "eth3", rtc::ADAPTER_TYPE_ETHERNET);

  // To simplify the test, only gather UDP host candidates.
  allocator().set_flags(PORTALLOCATOR_ENABLE_IPV6 | PORTALLOCATOR_DISABLE_TCP |
                        PORTALLOCATOR_DISABLE_STUN |
                        PORTALLOCATOR_DISABLE_RELAY);

  ASSERT_TRUE(CreateSession(cricket::ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  AddInterface(kLoopbackAddr, "test_loopback", rtc::ADAPTER_TYPE_LOOPBACK);
  allocator_->SetNetworkIgnoreMask(0);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->set_flags(PORTALLOCATOR_DISABLE_STUN | PORTALLOCATOR_DISABLE_RELAY |
                      PORTALLOCATOR_DISABLE_TCP);
  session_->StartGettingPorts();
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
  EXPECT_EQ(1U, candidates_.size());
}

// Tests that we can get all the desired addresses successfully.
TEST_F(BasicPortAllocatorTest, TestGetAllPortsWithMinimumStepDelay) {
  AddInterface(kClientAddr);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  ASSERT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
  EXPECT_EQ(3U, candidates_.size());
  EXPECT_EQ(3U, ports_.size());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", kClientAddr));
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kSrflx, "udp", kClientAddr));
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "tcp", kClientAddr));
}

// Test that when the same network interface is brought down and up, the
// port allocator session will restart a new allocation sequence if
// it is not stopped.
TEST_F(BasicPortAllocatorTest, TestSameNetworkDownAndUpWhenSessionNotStopped) {
  std::string if_name("test_net0");
  AddInterface(kClientAddr, if_name);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  ASSERT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  SIMULATED_WAIT(false, 1000, fake_clock);
  EXPECT_EQ(0U, candidates_.size());
  ports_.clear();
  candidate_allocation_done_ = false;

  // When the same interfaces are added again, new candidates/ports should be
  // generated.
  fss_->set_tcp_sockets_enabled(true);
  fss_->set_udp_sockets_enabled(true);
  AddInterface(kClientAddr, if_name);
  ASSERT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  ASSERT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
  EXPECT_EQ(3U, candidates_.size());
  EXPECT_EQ(3U, ports_.size());
  session_->StopGettingPorts();
  candidates_.clear();
  ports_.clear();

  RemoveInterface(kClientAddr);
  // Wait one (simulated) second and then verify no new candidates have
  // appeared.
  SIMULATED_WAIT(false, 1000, fake_clock);
  EXPECT_EQ(0U, candidates_.size());
  EXPECT_EQ(0U, ports_.size());

  // When the same interfaces are added again, new candidates/ports should not
  // be generated because the session has stopped.
  AddInterface(kClientAddr, if_name);
  SIMULATED_WAIT(false, 1000, fake_clock);
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
  ASSERT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  ASSERT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  ASSERT_EQ_SIMULATED_WAIT(2U, candidates_.size(), 1000, fake_clock);
  EXPECT_EQ(2U, ports_.size());
  ASSERT_EQ_SIMULATED_WAIT(3U, candidates_.size(), 2000, fake_clock);
  EXPECT_EQ(3U, ports_.size());

  ASSERT_EQ_SIMULATED_WAIT(3U, candidates_.size(), 1500, fake_clock);
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "tcp", kClientAddr));
  EXPECT_EQ(3U, ports_.size());
  EXPECT_TRUE(candidate_allocation_done_);
  // If we Stop gathering now, we shouldn't get a second "done" callback.
  session_->StopGettingPorts();
}

TEST_F(BasicPortAllocatorTest, TestSetupVideoRtpPortsWithNormalSendBuffers) {
  AddInterface(kClientAddr);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP, CN_VIDEO));
  session_->StartGettingPorts();
  ASSERT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  ASSERT_EQ_SIMULATED_WAIT(2U, candidates_.size(), kDefaultAllocationTimeout,
                           fake_clock);
  EXPECT_EQ(2U, ports_.size());
  session_->StopGettingPorts();
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  ASSERT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
                           rtc::SocketAddress(kNatUdpAddr.ipaddr(), 0)));
  // Again, two TURN candidates, using UDP/TCP for the first hop to the TURN
  // server.
  rtc::SocketAddress addr(kTurnUdpExtAddr.ipaddr(), 0);
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
  CheckDisableAdapterEnumeration(0U, rtc::IPAddress(), rtc::IPAddress(),
                                 rtc::IPAddress(), rtc::IPAddress());
}

// Test that even with multiple interfaces, the result should still be a single
// default private, one STUN and one TURN candidate since we bind to any address
// (i.e. all 0s).
TEST_F(BasicPortAllocatorTest,
       TestDisableAdapterEnumerationBehindNatMultipleInterfaces) {
  AddInterface(kPrivateAddr);
  AddInterface(kPrivateAddr2);
  ResetWithStunServerAndNat(kStunAddr);
  AddTurnServers(kTurnUdpIntAddr, rtc::SocketAddress());

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
                                 rtc::IPAddress());
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
  CheckDisableAdapterEnumeration(2U, kPrivateAddr.ipaddr(), rtc::IPAddress(),
                                 rtc::IPAddress(), rtc::IPAddress());
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
  CheckDisableAdapterEnumeration(2U, rtc::IPAddress(), rtc::IPAddress(),
                                 rtc::IPAddress(), rtc::IPAddress());
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
  CheckDisableAdapterEnumeration(2U, rtc::IPAddress(), kClientAddr.ipaddr(),
                                 rtc::IPAddress(), rtc::IPAddress());
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
  CheckDisableAdapterEnumeration(2U, rtc::IPAddress(), kNatUdpAddr.ipaddr(),
                                 rtc::IPAddress(), rtc::IPAddress());
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
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);

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
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_, 1000, fake_clock);
  EXPECT_EQ(0U, candidates_.size());
}

// Test that we don't crash or malfunction if we can't create UDP sockets.
TEST_F(BasicPortAllocatorTest, TestGetAllPortsNoUdpSockets) {
  AddInterface(kClientAddr);
  fss_->set_udp_sockets_enabled(false);
  ASSERT_TRUE(CreateSession(1));
  session_->StartGettingPorts();
  ASSERT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  ASSERT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  SIMULATED_WAIT(candidates_.size() > 0, 2000, fake_clock);
  // TODO(deadbeef): Check candidate_allocation_done signal.
  // In case of Relay, ports creation will succeed but sockets will fail.
  // There is no error reporting from RelayEntry to handle this failure.
}

// Testing STUN timeout.
TEST_F(BasicPortAllocatorTest, TestGetAllPortsNoUdpAllowed) {
  fss_->AddRule(false, rtc::FP_UDP, rtc::FD_ANY, kClientAddr);
  AddInterface(kClientAddr);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_EQ_SIMULATED_WAIT(2U, candidates_.size(), kDefaultAllocationTimeout,
                           fake_clock);
  EXPECT_EQ(2U, ports_.size());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", kClientAddr));
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "tcp", kClientAddr));
  // We wait at least for a full STUN timeout, which
  // cricket::STUN_TOTAL_TIMEOUT seconds.
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             cricket::STUN_TOTAL_TIMEOUT, fake_clock);
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
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  ResetWithTurnServersNoNat(kTurnUdpIntAddr, rtc::SocketAddress());
  allocator().SetCandidateFilter(CF_RELAY);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kRelay, "udp",
                           rtc::SocketAddress(kTurnUdpExtAddr.ipaddr(), 0)));

  EXPECT_EQ(1U, candidates_.size());
  EXPECT_EQ(1U, ports_.size());  // Only Relay port will be in ready state.
  EXPECT_TRUE(candidates_[0].is_relay());
  EXPECT_EQ(
      candidates_[0].related_address(),
      rtc::EmptySocketAddressWithFamily(candidates_[0].address().family()));
}

TEST_F(BasicPortAllocatorTest, TestCandidateFilterWithHostOnly) {
  AddInterface(kClientAddr);
  allocator().set_flags(PORTALLOCATOR_ENABLE_SHARED_SOCKET);
  allocator().SetCandidateFilter(CF_HOST);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
  // Host is behind NAT, no private address will be exposed. Hence only UDP
  // port with STUN candidate will be sent outside.
  EXPECT_EQ(1U, candidates_.size());  // Only STUN candidate.
  EXPECT_EQ(1U, ports_.size());       // Only UDP port will be in ready state.
  EXPECT_TRUE(candidates_[0].is_stun());
  EXPECT_EQ(
      candidates_[0].related_address(),
      rtc::EmptySocketAddressWithFamily(candidates_[0].address().family()));
}

// Host is not behind the NAT.
TEST_F(BasicPortAllocatorTest, TestCandidateFilterWithReflexiveOnlyAndNoNAT) {
  AddInterface(kClientAddr);
  allocator().set_flags(PORTALLOCATOR_ENABLE_SHARED_SOCKET);
  allocator().SetCandidateFilter(CF_REFLEXIVE);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  ASSERT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  ASSERT_EQ_SIMULATED_WAIT(2U, candidates_.size(), kDefaultAllocationTimeout,
                           fake_clock);
  EXPECT_EQ(2U, ports_.size());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", kClientAddr));
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  ASSERT_EQ_SIMULATED_WAIT(3U, candidates_.size(), kDefaultAllocationTimeout,
                           fake_clock);
  ASSERT_EQ(2U, ports_.size());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", kClientAddr));
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kSrflx, "udp",
                           rtc::SocketAddress(kNatUdpAddr.ipaddr(), 0)));
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
  EXPECT_EQ(3U, candidates_.size());
}

// Test TURN port in shared socket mode with UDP and TCP TURN server addresses.
TEST_F(BasicPortAllocatorTest, TestSharedSocketWithoutNatUsingTurn) {
  turn_server_.AddInternalSocket(kTurnTcpIntAddr, PROTO_TCP);
  AddInterface(kClientAddr);
  allocator_.reset(new BasicPortAllocator(&network_manager_, &socket_factory_));
  allocator_->Initialize();

  AddTurnServers(kTurnUdpIntAddr, kTurnTcpIntAddr);

  allocator_->set_step_delay(kMinimumStepDelay);
  allocator_->set_flags(allocator().flags() |
                        PORTALLOCATOR_ENABLE_SHARED_SOCKET |
                        PORTALLOCATOR_DISABLE_TCP);

  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();

  ASSERT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
  ASSERT_EQ(3U, candidates_.size());
  ASSERT_EQ(3U, ports_.size());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", kClientAddr));
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kRelay, "udp",
                           rtc::SocketAddress(kTurnUdpExtAddr.ipaddr(), 0)));
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kRelay, "udp",
                           rtc::SocketAddress(kTurnUdpExtAddr.ipaddr(), 0)));
}

// Test that if the turn port prune policy is PRUNE_BASED_ON_PRIORITY, TCP TURN
// port will not be used if UDP TurnPort is used, given that TCP TURN port
// becomes ready first.
TEST_F(BasicPortAllocatorTest,
       TestUdpTurnPortPrunesTcpTurnPortWithTcpPortReadyFirst) {
  // UDP has longer delay than TCP so that TCP TURN port becomes ready first.
  virtual_socket_server()->SetDelayOnAddress(kTurnUdpIntAddr, 200);
  virtual_socket_server()->SetDelayOnAddress(kTurnTcpIntAddr, 100);

  TestTurnPortPrunesWithUdpAndTcpPorts(webrtc::PRUNE_BASED_ON_PRIORITY,
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

  TestTurnPortPrunesWithUdpAndTcpPorts(webrtc::PRUNE_BASED_ON_PRIORITY,
                                       true /* tcp_pruned */);
}

// Test that if turn_port_prune policy is KEEP_FIRST_READY, the first ready port
// will be kept regardless of the priority.
TEST_F(BasicPortAllocatorTest,
       TestUdpTurnPortPrunesTcpTurnPortIfUdpReadyFirst) {
  // UDP has shorter delay than TCP so that UDP TURN port becomes ready first.
  virtual_socket_server()->SetDelayOnAddress(kTurnUdpIntAddr, 100);
  virtual_socket_server()->SetDelayOnAddress(kTurnTcpIntAddr, 200);

  TestTurnPortPrunesWithUdpAndTcpPorts(webrtc::KEEP_FIRST_READY,
                                       true /* tcp_pruned */);
}

// Test that if turn_port_prune policy is KEEP_FIRST_READY, the first ready port
// will be kept regardless of the priority.
TEST_F(BasicPortAllocatorTest,
       TestTcpTurnPortPrunesUdpTurnPortIfTcpReadyFirst) {
  // UDP has longer delay than TCP so that TCP TURN port becomes ready first.
  virtual_socket_server()->SetDelayOnAddress(kTurnUdpIntAddr, 200);
  virtual_socket_server()->SetDelayOnAddress(kTurnTcpIntAddr, 100);

  TestTurnPortPrunesWithUdpAndTcpPorts(webrtc::KEEP_FIRST_READY,
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
// TODO(pthatcher): Make this test work with SIMULATED_WAIT. It
// appears that it doesn't currently because of the DNS look up not
// using the fake clock.
TEST_F(BasicPortAllocatorTestWithRealClock,
       TestSharedSocketWithServerAddressResolve) {
  // This test relies on a real query for "localhost", so it won't work on an
  // IPv6-only machine.
  MAYBE_SKIP_IPV4;
  turn_server_.AddInternalSocket(rtc::SocketAddress("127.0.0.1", 3478),
                                 PROTO_UDP);
  AddInterface(kClientAddr);
  allocator_.reset(new BasicPortAllocator(&network_manager_, &socket_factory_));
  allocator_->Initialize();
  RelayServerConfig turn_server;
  RelayCredentials credentials(kTurnUsername, kTurnPassword);
  turn_server.credentials = credentials;
  turn_server.ports.push_back(
      ProtocolAddress(rtc::SocketAddress("localhost", 3478), PROTO_UDP));
  allocator_->AddTurnServerForTesting(turn_server);

  allocator_->set_step_delay(kMinimumStepDelay);
  allocator_->set_flags(allocator().flags() |
                        PORTALLOCATOR_ENABLE_SHARED_SOCKET |
                        PORTALLOCATOR_DISABLE_TCP);

  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();

  EXPECT_EQ_WAIT(2U, ports_.size(), kDefaultAllocationTimeout);
}

// Test that when PORTALLOCATOR_ENABLE_SHARED_SOCKET is enabled only one port
// is allocated for udp/stun/turn. In this test we should expect all local,
// stun and turn candidates.
TEST_F(BasicPortAllocatorTest, TestSharedSocketWithNatUsingTurn) {
  AddInterface(kClientAddr);
  ResetWithStunServerAndNat(kStunAddr);

  AddTurnServers(kTurnUdpIntAddr, rtc::SocketAddress());

  allocator_->set_flags(allocator().flags() |
                        PORTALLOCATOR_ENABLE_SHARED_SOCKET |
                        PORTALLOCATOR_DISABLE_TCP);

  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();

  ASSERT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
  EXPECT_EQ(3U, candidates_.size());
  ASSERT_EQ(2U, ports_.size());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", kClientAddr));
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kSrflx, "udp",
                           rtc::SocketAddress(kNatUdpAddr.ipaddr(), 0)));
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kRelay, "udp",
                           rtc::SocketAddress(kTurnUdpExtAddr.ipaddr(), 0)));
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  AddTurnServers(kTurnUdpIntAddr, rtc::SocketAddress());

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

  ASSERT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
  EXPECT_EQ(3U, candidates_.size());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", kClientAddr));
  Candidate stun_candidate;
  EXPECT_TRUE(FindCandidate(candidates_, IceCandidateType::kSrflx, "udp",
                            rtc::SocketAddress(kNatUdpAddr.ipaddr(), 0),
                            &stun_candidate));
  EXPECT_TRUE(HasCandidateWithRelatedAddr(
      candidates_, IceCandidateType::kRelay, "udp",
      rtc::SocketAddress(kTurnUdpExtAddr.ipaddr(), 0),
      stun_candidate.address()));

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
  ResetWithStunServerAndNat(rtc::SocketAddress());
  AddTurnServers(rtc::SocketAddress(), kTurnTcpIntAddr);

  allocator_->set_flags(allocator().flags() |
                        PORTALLOCATOR_ENABLE_SHARED_SOCKET |
                        PORTALLOCATOR_DISABLE_TCP);

  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();

  ASSERT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
  EXPECT_EQ(2U, candidates_.size());
  ASSERT_EQ(2U, ports_.size());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", kClientAddr));
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kRelay, "udp",
                           rtc::SocketAddress(kTurnUdpExtAddr.ipaddr(), 0)));
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
  AddTurnServers(kTurnUdpIntAddr, rtc::SocketAddress());

  allocator_->set_flags(allocator().flags() | PORTALLOCATOR_DISABLE_TCP);

  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();

  ASSERT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
  EXPECT_EQ(3U, candidates_.size());
  ASSERT_EQ(3U, ports_.size());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", kClientAddr));
  Candidate stun_candidate;
  EXPECT_TRUE(FindCandidate(candidates_, IceCandidateType::kSrflx, "udp",
                            rtc::SocketAddress(kNatUdpAddr.ipaddr(), 0),
                            &stun_candidate));
  Candidate turn_candidate;
  EXPECT_TRUE(FindCandidate(candidates_, IceCandidateType::kRelay, "udp",
                            rtc::SocketAddress(kTurnUdpExtAddr.ipaddr(), 0),
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
  AddTurnServers(kTurnUdpIntAddr, rtc::SocketAddress());

  allocator_->set_flags(allocator().flags() |
                        PORTALLOCATOR_ENABLE_SHARED_SOCKET |
                        PORTALLOCATOR_DISABLE_TCP);

  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();

  ASSERT_EQ_SIMULATED_WAIT(3U, candidates_.size(), kDefaultAllocationTimeout,
                           fake_clock);
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", kClientAddr));
  Candidate stun_candidate;
  EXPECT_TRUE(FindCandidate(candidates_, IceCandidateType::kSrflx, "udp",
                            rtc::SocketAddress(kNatUdpAddr.ipaddr(), 0),
                            &stun_candidate));
  EXPECT_TRUE(HasCandidateWithRelatedAddr(
      candidates_, IceCandidateType::kRelay, "udp",
      rtc::SocketAddress(kTurnUdpExtAddr.ipaddr(), 0),
      stun_candidate.address()));

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
  fss_->AddRule(false, rtc::FP_UDP, rtc::FD_ANY, kClientAddr);
  AddInterface(kClientAddr);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  ASSERT_EQ_SIMULATED_WAIT(1U, ports_.size(), kDefaultAllocationTimeout,
                           fake_clock);
  EXPECT_EQ(1U, candidates_.size());
  EXPECT_TRUE(
      HasCandidate(candidates_, IceCandidateType::kHost, "udp", kClientAddr));
  // STUN timeout is 9.5sec. We need to wait to get candidate done signal.
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_, kStunTimeoutMs,
                             fake_clock);
  EXPECT_EQ(1U, candidates_.size());
}

// Test that when the NetworkManager doesn't have permission to enumerate
// adapters, the PORTALLOCATOR_DISABLE_ADAPTER_ENUMERATION is specified
// automatically.
TEST_F(BasicPortAllocatorTest, TestNetworkPermissionBlocked) {
  network_manager_.set_default_local_addresses(kPrivateAddr.ipaddr(),
                                               rtc::IPAddress());
  network_manager_.set_enumeration_permission(
      rtc::NetworkManager::ENUMERATION_BLOCKED);
  allocator().set_flags(allocator().flags() | PORTALLOCATOR_DISABLE_RELAY |
                        PORTALLOCATOR_DISABLE_TCP |
                        PORTALLOCATOR_ENABLE_SHARED_SOCKET);
  EXPECT_EQ(0U,
            allocator_->flags() & PORTALLOCATOR_DISABLE_ADAPTER_ENUMERATION);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  EXPECT_EQ(0U, session_->flags() & PORTALLOCATOR_DISABLE_ADAPTER_ENUMERATION);
  session_->StartGettingPorts();
  EXPECT_EQ_SIMULATED_WAIT(1U, ports_.size(), kDefaultAllocationTimeout,
                           fake_clock);
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
  ASSERT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  ASSERT_EQ_SIMULATED_WAIT(2U, candidates_.size(), 1000, fake_clock);
  EXPECT_EQ(2U, ports_.size());
  session_->StopGettingPorts();
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_, 1000, fake_clock);

  // After stopping getting ports, adding a new interface will not start
  // getting ports again.
  allocator_->set_step_delay(kMinimumStepDelay);
  candidates_.clear();
  ports_.clear();
  candidate_allocation_done_ = false;
  network_manager_.AddInterface(kClientAddr2);
  SIMULATED_WAIT(false, 1000, fake_clock);
  EXPECT_EQ(0U, candidates_.size());
  EXPECT_EQ(0U, ports_.size());
}

TEST_F(BasicPortAllocatorTest, TestClearGettingPorts) {
  AddInterface(kClientAddr);
  allocator_->set_step_delay(kDefaultStepDelay);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  ASSERT_EQ_SIMULATED_WAIT(2U, candidates_.size(), 1000, fake_clock);
  EXPECT_EQ(2U, ports_.size());
  session_->ClearGettingPorts();
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_, 1000, fake_clock);

  // After clearing getting ports, adding a new interface will start getting
  // ports again.
  allocator_->set_step_delay(kMinimumStepDelay);
  candidates_.clear();
  ports_.clear();
  candidate_allocation_done_ = false;
  network_manager_.AddInterface(kClientAddr2);
  ASSERT_EQ_SIMULATED_WAIT(2U, candidates_.size(), 1000, fake_clock);
  EXPECT_EQ(2U, ports_.size());
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
}

// Test that the ports and candidates are updated with new ufrag/pwd/etc. when
// a pooled session is taken out of the pool.
TEST_F(BasicPortAllocatorTest, TestTransportInformationUpdated) {
  AddInterface(kClientAddr);
  int pool_size = 1;
  allocator_->SetConfiguration(allocator_->stun_servers(),
                               allocator_->turn_servers(), pool_size,
                               webrtc::NO_PRUNE);
  const PortAllocatorSession* peeked_session = allocator_->GetPooledSession();
  ASSERT_NE(nullptr, peeked_session);
  EXPECT_EQ_SIMULATED_WAIT(true, peeked_session->CandidatesAllocationDone(),
                           kDefaultAllocationTimeout, fake_clock);
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
                               allocator_->turn_servers(), pool_size,
                               webrtc::NO_PRUNE);
  const PortAllocatorSession* peeked_session = allocator_->GetPooledSession();
  ASSERT_NE(nullptr, peeked_session);
  EXPECT_EQ_SIMULATED_WAIT(true, peeked_session->CandidatesAllocationDone(),
                           kDefaultAllocationTimeout, fake_clock);
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
              rtc::EmptySocketAddressWithFamily(candidate.address().family()));
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

  AddTurnServers(kTurnUdpIntAddr, rtc::SocketAddress());

  allocator_->set_flags(allocator().flags() |
                        PORTALLOCATOR_ENABLE_SHARED_SOCKET |
                        PORTALLOCATOR_DISABLE_TCP);

  allocator_->SetCandidateFilter(CF_NONE);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
  EXPECT_TRUE(candidates_.empty());
  EXPECT_TRUE(ports_.empty());

  // Surface the relay candidate previously gathered but not signaled.
  session_->SetCandidateFilter(CF_RELAY);
  ASSERT_EQ_SIMULATED_WAIT(1u, candidates_.size(), kDefaultAllocationTimeout,
                           fake_clock);
  EXPECT_TRUE(candidates_.back().is_relay());
  EXPECT_EQ(1u, ports_.size());

  // Surface the srflx candidate previously gathered but not signaled.
  session_->SetCandidateFilter(CF_RELAY | CF_REFLEXIVE);
  ASSERT_EQ_SIMULATED_WAIT(2u, candidates_.size(), kDefaultAllocationTimeout,
                           fake_clock);
  EXPECT_TRUE(candidates_.back().is_stun());
  EXPECT_EQ(2u, ports_.size());

  // Surface the srflx candidate previously gathered but not signaled.
  session_->SetCandidateFilter(CF_ALL);
  ASSERT_EQ_SIMULATED_WAIT(3u, candidates_.size(), kDefaultAllocationTimeout,
                           fake_clock);
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

  AddTurnServers(kTurnUdpIntAddr, rtc::SocketAddress());

  allocator_->set_flags(allocator().flags() |
                        PORTALLOCATOR_ENABLE_SHARED_SOCKET |
                        PORTALLOCATOR_DISABLE_TCP);

  allocator_->SetCandidateFilter(CF_NONE);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
  EXPECT_TRUE(candidates_.empty());
  EXPECT_TRUE(ports_.empty());

  // Surface the relay candidate previously gathered but not signaled.
  session_->SetCandidateFilter(CF_RELAY);
  EXPECT_EQ_SIMULATED_WAIT(1u, candidates_.size(), kDefaultAllocationTimeout,
                           fake_clock);
  EXPECT_TRUE(candidates_.back().is_relay());
  EXPECT_EQ(1u, ports_.size());

  // Surface the srflx candidate previously gathered but not signaled.
  session_->SetCandidateFilter(CF_REFLEXIVE);
  EXPECT_EQ_SIMULATED_WAIT(2u, candidates_.size(), kDefaultAllocationTimeout,
                           fake_clock);
  EXPECT_TRUE(candidates_.back().is_stun());
  EXPECT_EQ(2u, ports_.size());

  // Surface the host candidate previously gathered but not signaled.
  session_->SetCandidateFilter(CF_HOST);
  EXPECT_EQ_SIMULATED_WAIT(3u, candidates_.size(), kDefaultAllocationTimeout,
                           fake_clock);
  EXPECT_TRUE(candidates_.back().is_local());
  // We use a shared socket and cricket::UDPPort handles the srflx candidate.
  EXPECT_EQ(2u, ports_.size());
}

// Test that after an allocation session has stopped getting ports, changing the
// candidate filter to allow new types of gathered candidates does not surface
// any candidate.
TEST_F(BasicPortAllocatorTest,
       NoCandidateSurfacedWhenUpdatingCandidateFilterIfSessionStopped) {
  AddInterface(kPrivateAddr);
  ResetWithStunServerAndNat(kStunAddr);

  AddTurnServers(kTurnUdpIntAddr, rtc::SocketAddress());

  allocator_->set_flags(allocator().flags() |
                        PORTALLOCATOR_ENABLE_SHARED_SOCKET |
                        PORTALLOCATOR_DISABLE_TCP);

  allocator_->SetCandidateFilter(CF_NONE);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
  auto test_invariants = [this]() {
    EXPECT_TRUE(candidates_.empty());
    EXPECT_TRUE(ports_.empty());
  };

  test_invariants();

  session_->StopGettingPorts();

  session_->SetCandidateFilter(CF_RELAY);
  SIMULATED_WAIT(false, kDefaultAllocationTimeout, fake_clock);
  test_invariants();

  session_->SetCandidateFilter(CF_RELAY | CF_REFLEXIVE);
  SIMULATED_WAIT(false, kDefaultAllocationTimeout, fake_clock);
  test_invariants();

  session_->SetCandidateFilter(CF_ALL);
  SIMULATED_WAIT(false, kDefaultAllocationTimeout, fake_clock);
  test_invariants();
}

TEST_F(BasicPortAllocatorTest, SetStunKeepaliveIntervalForPorts) {
  const int pool_size = 1;
  const int expected_stun_keepalive_interval = 123;
  AddInterface(kClientAddr);
  allocator_->SetConfiguration(
      allocator_->stun_servers(), allocator_->turn_servers(), pool_size,
      webrtc::NO_PRUNE, nullptr, expected_stun_keepalive_interval);
  auto* pooled_session = allocator_->GetPooledSession();
  ASSERT_NE(nullptr, pooled_session);
  EXPECT_EQ_SIMULATED_WAIT(true, pooled_session->CandidatesAllocationDone(),
                           kDefaultAllocationTimeout, fake_clock);
  CheckStunKeepaliveIntervalOfAllReadyPorts(pooled_session,
                                            expected_stun_keepalive_interval);
}

TEST_F(BasicPortAllocatorTest,
       ChangeStunKeepaliveIntervalForPortsAfterInitialConfig) {
  const int pool_size = 1;
  AddInterface(kClientAddr);
  allocator_->SetConfiguration(
      allocator_->stun_servers(), allocator_->turn_servers(), pool_size,
      webrtc::NO_PRUNE, nullptr, 123 /* stun keepalive interval */);
  auto* pooled_session = allocator_->GetPooledSession();
  ASSERT_NE(nullptr, pooled_session);
  EXPECT_EQ_SIMULATED_WAIT(true, pooled_session->CandidatesAllocationDone(),
                           kDefaultAllocationTimeout, fake_clock);
  const int expected_stun_keepalive_interval = 321;
  allocator_->SetConfiguration(
      allocator_->stun_servers(), allocator_->turn_servers(), pool_size,
      webrtc::NO_PRUNE, nullptr, expected_stun_keepalive_interval);
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
  allocator_->SetConfiguration(
      allocator_->stun_servers(), allocator_->turn_servers(), pool_size,
      webrtc::NO_PRUNE, nullptr, expected_stun_keepalive_interval);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  allocator_->SetConfiguration(
      allocator_->stun_servers(), allocator_->turn_servers(), pool_size,
      webrtc::NO_PRUNE, nullptr, expected_stun_keepalive_interval);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
      std::make_unique<webrtc::FakeMdnsResponder>(rtc::Thread::Current()));
  AddInterface(kClientAddr);
  ASSERT_TRUE(CreateSession(ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  ASSERT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
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
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-UseTurnServerAsStunServer/Disabled/");
  ServerAddresses stun_servers;
  stun_servers.insert(kStunAddr);
  PortConfiguration port_config(stun_servers, "" /* user_name */,
                                "" /* password */, &field_trials);
  RelayServerConfig turn_servers =
      CreateTurnServers(kTurnUdpIntAddr, kTurnTcpIntAddr);
  port_config.AddRelay(turn_servers);

  EXPECT_EQ(1U, port_config.StunServers().size());
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
  ASSERT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);
  EXPECT_EQ(3u, candidates_.size());
  EXPECT_GT((candidates_[0].priority() >> 8) & 0xFFFF,
            (candidates_[1].priority() >> 8) & 0xFFFF);
  EXPECT_GT((candidates_[1].priority() >> 8) & 0xFFFF,
            (candidates_[2].priority() >> 8) & 0xFFFF);
}

// Test that no more than allocator.max_ipv6_networks() IPv6 networks are used
// to gather candidates.
TEST_F(BasicPortAllocatorTest, TwoIPv6AreSelectedBecauseOfMaxIpv6Limit) {
  rtc::Network wifi1("wifi1", "Test NetworkAdapter 1", kClientIPv6Addr.ipaddr(),
                     64, rtc::ADAPTER_TYPE_WIFI);
  rtc::Network ethe1("ethe1", "Test NetworkAdapter 2",
                     kClientIPv6Addr2.ipaddr(), 64, rtc::ADAPTER_TYPE_ETHERNET);
  rtc::Network wifi2("wifi2", "Test NetworkAdapter 3",
                     kClientIPv6Addr3.ipaddr(), 64, rtc::ADAPTER_TYPE_WIFI);
  std::vector<const rtc::Network*> networks = {&wifi1, &ethe1, &wifi2};

  // Ensure that only 2 interfaces were selected.
  EXPECT_EQ(2U, BasicPortAllocatorSession::SelectIPv6Networks(
                    networks, /*max_ipv6_networks=*/2)
                    .size());
}

// Test that if the number of available IPv6 networks is less than
// allocator.max_ipv6_networks(), all IPv6 networks will be selected.
TEST_F(BasicPortAllocatorTest, AllIPv6AreSelected) {
  rtc::Network wifi1("wifi1", "Test NetworkAdapter 1", kClientIPv6Addr.ipaddr(),
                     64, rtc::ADAPTER_TYPE_WIFI);
  rtc::Network ethe1("ethe1", "Test NetworkAdapter 2",
                     kClientIPv6Addr2.ipaddr(), 64, rtc::ADAPTER_TYPE_ETHERNET);
  std::vector<const rtc::Network*> networks = {&wifi1, &ethe1};

  // Ensure that all 2 interfaces were selected.
  EXPECT_EQ(2U, BasicPortAllocatorSession::SelectIPv6Networks(
                    networks, /*max_ipv6_networks=*/3)
                    .size());
}

// If there are some IPv6 networks with different types, diversify IPv6
// networks.
TEST_F(BasicPortAllocatorTest, TwoIPv6WifiAreSelectedIfThereAreTwo) {
  rtc::Network wifi1("wifi1", "Test NetworkAdapter 1", kClientIPv6Addr.ipaddr(),
                     64, rtc::ADAPTER_TYPE_WIFI);
  rtc::Network ethe1("ethe1", "Test NetworkAdapter 2",
                     kClientIPv6Addr2.ipaddr(), 64, rtc::ADAPTER_TYPE_ETHERNET);
  rtc::Network ethe2("ethe2", "Test NetworkAdapter 3",
                     kClientIPv6Addr3.ipaddr(), 64, rtc::ADAPTER_TYPE_ETHERNET);
  rtc::Network unknown1("unknown1", "Test NetworkAdapter 4",
                        kClientIPv6Addr2.ipaddr(), 64,
                        rtc::ADAPTER_TYPE_UNKNOWN);
  rtc::Network cell1("cell1", "Test NetworkAdapter 5",
                     kClientIPv6Addr3.ipaddr(), 64,
                     rtc::ADAPTER_TYPE_CELLULAR_4G);
  std::vector<const rtc::Network*> networks = {&wifi1, &ethe1, &ethe2,
                                               &unknown1, &cell1};

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
  rtc::Network cell1("cell1", "Test NetworkAdapter 1", kClientIPv6Addr.ipaddr(),
                     64, rtc::ADAPTER_TYPE_CELLULAR_2G);
  rtc::Network cell2("cell2", "Test NetworkAdapter 2",
                     kClientIPv6Addr2.ipaddr(), 64,
                     rtc::ADAPTER_TYPE_CELLULAR_3G);
  rtc::Network cell3("cell3", "Test NetworkAdapter 3",
                     kClientIPv6Addr3.ipaddr(), 64,
                     rtc::ADAPTER_TYPE_CELLULAR_4G);
  rtc::Network cell4("cell4", "Test NetworkAdapter 4",
                     kClientIPv6Addr2.ipaddr(), 64,
                     rtc::ADAPTER_TYPE_CELLULAR_5G);
  rtc::Network cell5("cell5", "Test NetworkAdapter 5",
                     kClientIPv6Addr3.ipaddr(), 64,
                     rtc::ADAPTER_TYPE_CELLULAR_3G);
  std::vector<const rtc::Network*> networks = {&cell1, &cell2, &cell3, &cell4,
                                               &cell5};

  // Ensure that 4 interfaces were selected.
  EXPECT_EQ(4U, BasicPortAllocatorSession::SelectIPv6Networks(
                    networks, /*max_ipv6_networks=*/4)
                    .size());
}

TEST_F(BasicPortAllocatorTest, IPv6EthernetHasHigherPriorityThanWifi) {
  rtc::Network wifi1("wifi1", "Test NetworkAdapter 1", kClientIPv6Addr.ipaddr(),
                     64, rtc::ADAPTER_TYPE_WIFI);
  rtc::Network ethe1("ethe1", "Test NetworkAdapter 2",
                     kClientIPv6Addr2.ipaddr(), 64, rtc::ADAPTER_TYPE_ETHERNET);
  rtc::Network wifi2("wifi2", "Test NetworkAdapter 3",
                     kClientIPv6Addr3.ipaddr(), 64, rtc::ADAPTER_TYPE_WIFI);
  std::vector<const rtc::Network*> networks = {&wifi1, &ethe1, &wifi2};

  networks = BasicPortAllocatorSession::SelectIPv6Networks(
      networks, /*max_ipv6_networks=*/1);

  EXPECT_EQ(1U, networks.size());
  // Ensure ethe1 was selected.
  EXPECT_TRUE(HasNetwork(networks, ethe1));
}

TEST_F(BasicPortAllocatorTest, IPv6EtherAndWifiHaveHigherPriorityThanOthers) {
  rtc::Network cell1("cell1", "Test NetworkAdapter 1", kClientIPv6Addr.ipaddr(),
                     64, rtc::ADAPTER_TYPE_CELLULAR_3G);
  rtc::Network ethe1("ethe1", "Test NetworkAdapter 2",
                     kClientIPv6Addr2.ipaddr(), 64, rtc::ADAPTER_TYPE_ETHERNET);
  rtc::Network wifi1("wifi1", "Test NetworkAdapter 3",
                     kClientIPv6Addr3.ipaddr(), 64, rtc::ADAPTER_TYPE_WIFI);
  rtc::Network unknown("unknown", "Test NetworkAdapter 4",
                       kClientIPv6Addr2.ipaddr(), 64,
                       rtc::ADAPTER_TYPE_UNKNOWN);
  rtc::Network vpn1("vpn1", "Test NetworkAdapter 5", kClientIPv6Addr3.ipaddr(),
                    64, rtc::ADAPTER_TYPE_VPN);
  std::vector<const rtc::Network*> networks = {&cell1, &ethe1, &wifi1, &unknown,
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
  AddInterface(kClientIPv6Addr, "ethe1", rtc::ADAPTER_TYPE_ETHERNET);
  AddInterface(kClientIPv6Addr2, "ethe2", rtc::ADAPTER_TYPE_ETHERNET);
  AddInterface(kClientIPv6Addr3, "wifi1", rtc::ADAPTER_TYPE_WIFI);
  AddInterface(kClientIPv6Addr4, "wifi2", rtc::ADAPTER_TYPE_WIFI);
  AddInterface(kClientIPv6Addr5, "cell1", rtc::ADAPTER_TYPE_CELLULAR_3G);

  // To simplify the test, only gather UDP host candidates.
  allocator().set_flags(PORTALLOCATOR_ENABLE_IPV6 | PORTALLOCATOR_DISABLE_TCP |
                        PORTALLOCATOR_DISABLE_STUN |
                        PORTALLOCATOR_DISABLE_RELAY |
                        PORTALLOCATOR_ENABLE_IPV6_ON_WIFI);

  ASSERT_TRUE(CreateSession(cricket::ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);

  EXPECT_EQ(2U, candidates_.size());
  // ethe1 and wifi1 were selected.
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kHost, "udp",
                           kClientIPv6Addr));
  EXPECT_TRUE(HasCandidate(candidates_, IceCandidateType::kHost, "udp",
                           kClientIPv6Addr3));
}

TEST_F(BasicPortAllocatorTest, Select3DifferentIntefaces) {
  allocator().set_max_ipv6_networks(3);
  AddInterface(kClientIPv6Addr, "ethe1", rtc::ADAPTER_TYPE_ETHERNET);
  AddInterface(kClientIPv6Addr2, "ethe2", rtc::ADAPTER_TYPE_ETHERNET);
  AddInterface(kClientIPv6Addr3, "wifi1", rtc::ADAPTER_TYPE_WIFI);
  AddInterface(kClientIPv6Addr4, "wifi2", rtc::ADAPTER_TYPE_WIFI);
  AddInterface(kClientIPv6Addr5, "cell1", rtc::ADAPTER_TYPE_CELLULAR_3G);

  // To simplify the test, only gather UDP host candidates.
  allocator().set_flags(PORTALLOCATOR_ENABLE_IPV6 | PORTALLOCATOR_DISABLE_TCP |
                        PORTALLOCATOR_DISABLE_STUN |
                        PORTALLOCATOR_DISABLE_RELAY |
                        PORTALLOCATOR_ENABLE_IPV6_ON_WIFI);

  ASSERT_TRUE(CreateSession(cricket::ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);

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
  AddInterface(kClientIPv6Addr, "ethe1", rtc::ADAPTER_TYPE_ETHERNET);
  AddInterface(kClientIPv6Addr2, "ethe2", rtc::ADAPTER_TYPE_ETHERNET);
  AddInterface(kClientIPv6Addr3, "wifi1", rtc::ADAPTER_TYPE_WIFI);
  AddInterface(kClientIPv6Addr4, "wifi2", rtc::ADAPTER_TYPE_WIFI);
  AddInterface(kClientIPv6Addr5, "cell1", rtc::ADAPTER_TYPE_CELLULAR_3G);

  // To simplify the test, only gather UDP host candidates.
  allocator().set_flags(PORTALLOCATOR_ENABLE_IPV6 | PORTALLOCATOR_DISABLE_TCP |
                        PORTALLOCATOR_DISABLE_STUN |
                        PORTALLOCATOR_DISABLE_RELAY |
                        PORTALLOCATOR_ENABLE_IPV6_ON_WIFI);

  ASSERT_TRUE(CreateSession(cricket::ICE_CANDIDATE_COMPONENT_RTP));
  session_->StartGettingPorts();
  EXPECT_TRUE_SIMULATED_WAIT(candidate_allocation_done_,
                             kDefaultAllocationTimeout, fake_clock);

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

}  // namespace cricket
