/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

// Original author: ekr@rtfm.com

#include <algorithm>
#include <deque>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "logging.h"
#include "mozilla/DataMutex.h"
#include "mozilla/Preferences.h"
#include "nsThreadUtils.h"
#include "nsXPCOM.h"
#include "sigslot.h"
#include "ssl.h"

// clang-format off
#include "r_types.h"
#include "async_wait.h"
#include "async_timer.h"
#include "r_data.h"
#include "util.h"
#include "r_time.h"
// clang-format on

#include "gtest_ringbuffer_dumper.h"
#include "ice_ctx.h"
#include "ice_media_stream.h"
#include "ice_peer_ctx.h"
#include "mozilla/net/DNS.h"
#include "nr_socket_prsock.h"
#include "nricectx.h"
#include "nricemediastream.h"
#include "nriceresolver.h"
#include "nriceresolverfake.h"
#include "nrinterfaceprioritizer.h"
#include "nsISocketFilter.h"
#include "rlogconnector.h"
#include "runnable_utils.h"
#include "stunserver.h"
#include "test_nr_socket.h"

#define GTEST_HAS_RTTI 0
#include "gtest/gtest.h"
#include "gtest_utils.h"

using namespace mozilla;

static unsigned int kDefaultTimeout = 7000;

// TODO: It would be nice to have a test STUN/TURN server that can run with
// gtest.
MOZ_RUNINIT const std::string kDefaultStunServerHostname((char*)"");
MOZ_RUNINIT const std::string kBogusStunServerHostname(
    (char*)"stun-server-nonexistent.invalid");
const uint16_t kDefaultStunServerPort = 19305;
MOZ_RUNINIT const std::string kBogusIceCandidate(
    (char*)"candidate:0 2 UDP 2113601790 192.168.178.20 50769 typ");

MOZ_RUNINIT const std::string kUnreachableHostIceCandidate(
    (char*)"candidate:0 1 UDP 2113601790 192.168.178.20 50769 typ host");

namespace {

// DNS resolution helper code
static std::string Resolve(const std::string& fqdn, int address_family) {
  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = address_family;
  hints.ai_protocol = IPPROTO_UDP;
  struct addrinfo* res;
  int err = getaddrinfo(fqdn.c_str(), nullptr, &hints, &res);
  if (err) {
    std::cerr << "Error in getaddrinfo: " << err << std::endl;
    return "";
  }

  char str_addr[64] = {0};
  switch (res->ai_family) {
    case AF_INET:
      inet_ntop(AF_INET,
                &reinterpret_cast<struct sockaddr_in*>(res->ai_addr)->sin_addr,
                str_addr, sizeof(str_addr));
      break;
    case AF_INET6:
      inet_ntop(
          AF_INET6,
          &reinterpret_cast<struct sockaddr_in6*>(res->ai_addr)->sin6_addr,
          str_addr, sizeof(str_addr));
      break;
    default:
      std::cerr << "Got unexpected address family in DNS lookup: "
                << res->ai_family << std::endl;
      freeaddrinfo(res);
      return "";
  }

  if (!strlen(str_addr)) {
    std::cerr << "inet_ntop failed" << std::endl;
  }

  freeaddrinfo(res);
  return str_addr;
}

class StunTest : public MtransportTest {
 public:
  StunTest() = default;

  void SetUp() override {
    MtransportTest::SetUp();

    stun_server_hostname_ = kDefaultStunServerHostname;
    // If only a STUN server FQDN was provided, look up its IP address for the
    // address-only tests.
    if (stun_server_address_.empty() && !stun_server_hostname_.empty()) {
      stun_server_address_ = Resolve(stun_server_hostname_, AF_INET);
      ASSERT_TRUE(!stun_server_address_.empty());
    }

    test_utils_->SyncDispatchToSTS(WrapRunnable(this, &StunTest::SetUp_s));
  }

  void SetUp_s() {
    // Make sure NrIceCtx is in a testable state.
    NrIceCtx::internal_DeinitializeGlobal();
    RLogConnector::CreateInstance();

    TestStunServer::GetInstance(AF_INET);
    TestStunServer::GetInstance(AF_INET6);

    TestStunTcpServer::GetInstance(AF_INET);
    TestStunTcpServer::GetInstance(AF_INET6);
  }

  void TearDown() override {
    test_utils_->SyncDispatchToSTS(WrapRunnable(this, &StunTest::TearDown_s));
    MtransportTest::TearDown();
  }

  void TearDown_s() {
    NrIceCtx::internal_DeinitializeGlobal();
    TestStunServer::ShutdownInstance();
    TestStunTcpServer::ShutdownInstance();
  }
};

enum TrickleMode { TRICKLE_NONE, TRICKLE_SIMULATE, TRICKLE_REAL };

enum ConsentStatus { CONSENT_FRESH, CONSENT_STALE, CONSENT_EXPIRED };

typedef std::string (*CandidateFilter)(const std::string& candidate);

std::vector<std::string> split(const std::string& s, char delim) {
  std::vector<std::string> elems;
  std::stringstream ss(s);
  std::string item;
  while (std::getline(ss, item, delim)) {
    elems.push_back(item);
  }
  return elems;
}

static std::string IsSrflxCandidate(const std::string& candidate) {
  std::vector<std::string> tokens = split(candidate, ' ');
  if ((tokens.at(6) == "typ") && (tokens.at(7) == "srflx")) {
    return candidate;
  }
  return std::string();
}

static std::string IsRelayCandidate(const std::string& candidate) {
  if (candidate.find("typ relay") != std::string::npos) {
    return candidate;
  }
  return std::string();
}

static std::string IsTcpCandidate(const std::string& candidate) {
  if (candidate.find("TCP") != std::string::npos) {
    return candidate;
  }
  return std::string();
}

static std::string IsTcpSoCandidate(const std::string& candidate) {
  if (candidate.find("tcptype so") != std::string::npos) {
    return candidate;
  }
  return std::string();
}

static std::string IsLoopbackCandidate(const std::string& candidate) {
  if (candidate.find("127.0.0.") != std::string::npos) {
    return candidate;
  }
  return std::string();
}

static std::string IsIpv4Candidate(const std::string& candidate) {
  std::vector<std::string> tokens = split(candidate, ' ');
  if (tokens.at(4).find(':') == std::string::npos) {
    return candidate;
  }
  return std::string();
}

static std::string SabotageHostCandidateAndDropReflexive(
    const std::string& candidate) {
  if (candidate.find("typ srflx") != std::string::npos) {
    return std::string();
  }

  if (candidate.find("typ host") != std::string::npos) {
    return kUnreachableHostIceCandidate;
  }

  return candidate;
}

bool ContainsSucceededPair(const std::vector<NrIceCandidatePair>& pairs) {
  for (const auto& pair : pairs) {
    if (pair.state == NrIceCandidatePair::STATE_SUCCEEDED) {
      return true;
    }
  }
  return false;
}

// Note: Does not correspond to any notion of prioritization; this is just
// so we can use stl containers/algorithms that need a comparator
bool operator<(const NrIceCandidate& lhs, const NrIceCandidate& rhs) {
  if (lhs.cand_addr.host == rhs.cand_addr.host) {
    if (lhs.cand_addr.port == rhs.cand_addr.port) {
      if (lhs.cand_addr.transport == rhs.cand_addr.transport) {
        if (lhs.type == rhs.type) {
          return lhs.tcp_type < rhs.tcp_type;
        }
        return lhs.type < rhs.type;
      }
      return lhs.cand_addr.transport < rhs.cand_addr.transport;
    }
    return lhs.cand_addr.port < rhs.cand_addr.port;
  }
  return lhs.cand_addr.host < rhs.cand_addr.host;
}

bool operator==(const NrIceCandidate& lhs, const NrIceCandidate& rhs) {
  return !((lhs < rhs) || (rhs < lhs));
}

class IceCandidatePairCompare {
 public:
  bool operator()(const NrIceCandidatePair& lhs,
                  const NrIceCandidatePair& rhs) const {
    if (lhs.priority == rhs.priority) {
      if (lhs.local == rhs.local) {
        if (lhs.remote == rhs.remote) {
          return lhs.codeword < rhs.codeword;
        }
        return lhs.remote < rhs.remote;
      }
      return lhs.local < rhs.local;
    }
    return lhs.priority < rhs.priority;
  }
};

class IceTestPeer;

class SchedulableTrickleCandidate {
 public:
  SchedulableTrickleCandidate(IceTestPeer* peer, size_t stream,
                              const std::string& candidate,
                              const std::string& ufrag,
                              MtransportTestUtils* utils)
      : peer_(peer),
        stream_(stream),
        candidate_(candidate),
        ufrag_(ufrag),
        timer_handle_(nullptr),
        test_utils_(utils) {}

  ~SchedulableTrickleCandidate() {
    if (timer_handle_) NR_async_timer_cancel(timer_handle_);
  }

  void Schedule(unsigned int ms) {
    std::cerr << "Scheduling " << Candidate() << " in " << ms << "ms"
              << std::endl;
    test_utils_->SyncDispatchToSTS(
        WrapRunnable(this, &SchedulableTrickleCandidate::Schedule_s, ms));
  }

  void Schedule_s(unsigned int ms) {
    MOZ_ASSERT(!timer_handle_);
    NR_ASYNC_TIMER_SET(ms, Trickle_cb, this, &timer_handle_);
  }

  static void Trickle_cb(NR_SOCKET s, int how, void* cb_arg) {
    static_cast<SchedulableTrickleCandidate*>(cb_arg)->Trickle();
  }

  void Trickle();

  std::string& Candidate() { return candidate_; }

  const std::string& Candidate() const { return candidate_; }

  bool IsHost() const {
    return candidate_.find("typ host") != std::string::npos;
  }

  bool IsReflexive() const {
    return candidate_.find("typ srflx") != std::string::npos;
  }

  bool IsRelay() const {
    return candidate_.find("typ relay") != std::string::npos;
  }

 private:
  IceTestPeer* peer_;
  size_t stream_;
  std::string candidate_;
  std::string ufrag_;
  void* timer_handle_;
  MtransportTestUtils* test_utils_;

  DISALLOW_COPY_ASSIGN(SchedulableTrickleCandidate);
};

class IceTestPeer : public sigslot::has_slots<> {
 public:
  IceTestPeer(const std::string& name, MtransportTestUtils* utils, bool offerer,
              const NrIceCtx::Config& config)
      : name_(name),
        ice_ctx_(NrIceCtx::Create(name)),
        offerer_(offerer),
        stream_counter_(0),
        shutting_down_(false),
        mConnectionStates("IceTestPeer::mConnectionStates"),
        mGatheringStates("IceTestPeer::mGatheringStates"),
        ready_ct_(0),
        ice_reached_checking_(false),
        received_(0),
        sent_(0),
        dns_resolver_(new NrIceResolver()),
        remote_(nullptr),
        candidate_filter_(nullptr),
        expected_local_type_(NrIceCandidate::ICE_HOST),
        expected_local_transport_(kNrIceTransportUdp),
        expected_remote_type_(NrIceCandidate::ICE_HOST),
        trickle_mode_(TRICKLE_NONE),
        simulate_ice_lite_(false),
        nat_(new TestNat),
        test_utils_(utils) {
    ice_ctx_->SignalConnectionStateChange.connect(
        this, &IceTestPeer::ConnectionStateChange);

    ice_ctx_->SetIceConfig(config);

    consent_timestamp_.tv_sec = 0;
    consent_timestamp_.tv_usec = 0;
    int r = ice_ctx_->SetNat(nat_);
    (void)r;
    MOZ_ASSERT(!r);
  }

  ~IceTestPeer() {
    test_utils_->SyncDispatchToSTS(WrapRunnable(this, &IceTestPeer::Shutdown));

    // Give the ICE destruction callback time to fire before
    // we destroy the resolver.
    PR_Sleep(1000);
  }

  std::string MakeTransportId(size_t index) const {
    char id[100];
    snprintf(id, sizeof(id), "%s:stream%d", name_.c_str(), (int)index);
    return id;
  }

  void SetIceCredentials_s(NrIceMediaStream& stream) {
    static size_t counter = 0;
    std::ostringstream prefix;
    prefix << name_ << "-" << counter++;
    std::string ufrag = prefix.str() + "-ufrag";
    std::string pwd = prefix.str() + "-pwd";
    if (mIceCredentials.count(stream.GetId())) {
      mOldIceCredentials[stream.GetId()] = mIceCredentials[stream.GetId()];
    }
    mIceCredentials[stream.GetId()] = std::make_pair(ufrag, pwd);
    stream.SetIceCredentials(ufrag, pwd);
  }

  void AddStream_s(int components) {
    std::string id = MakeTransportId(stream_counter_++);

    RefPtr<NrIceMediaStream> stream =
        ice_ctx_->CreateStream(id, id, components);

    ASSERT_TRUE(stream);
    SetIceCredentials_s(*stream);

    stream->SignalCandidate.connect(this, &IceTestPeer::CandidateInitialized);
    stream->SignalReady.connect(this, &IceTestPeer::StreamReady);
    stream->SignalFailed.connect(this, &IceTestPeer::StreamFailed);
    stream->SignalPacketReceived.connect(this, &IceTestPeer::PacketReceived);
    stream->SignalGatheringStateChange.connect(
        this, &IceTestPeer::GatheringStateChange);
    {
      auto lock = mConnectionStates.Lock();
      lock.ref()[id] = NrIceCtx::ICE_CTX_INIT;
    }
    {
      auto lock = mGatheringStates.Lock();
      lock.ref()[id] = NrIceMediaStream::ICE_STREAM_GATHER_INIT;
    }
  }

  void AddStream(int components) {
    test_utils_->SyncDispatchToSTS(
        WrapRunnable(this, &IceTestPeer::AddStream_s, components));
  }

  void RemoveStream_s(size_t index) {
    const std::string id = MakeTransportId(index);
    ice_ctx_->DestroyStream(id);
    {
      auto lock = mConnectionStates.Lock();
      lock->erase(id);
    }
    {
      auto lock = mGatheringStates.Lock();
      lock->erase(id);
    }
  }

  void RemoveStream(size_t index) {
    test_utils_->SyncDispatchToSTS(
        WrapRunnable(this, &IceTestPeer::RemoveStream_s, index));
  }

  RefPtr<NrIceMediaStream> GetStream_s(size_t index) {
    std::string id = MakeTransportId(index);
    return ice_ctx_->GetStream(id);
  }

  void SetStunServer(const std::string addr, uint16_t port,
                     IceTransport transport = IceTransport::Udp) {
    if (addr.empty()) {
      // Happens when MOZ_DISABLE_NONLOCAL_CONNECTIONS is set
      return;
    }

    nsTArray<ParsedIceServer> servers;
    servers.AppendElement(MakeStunEntry(addr, port, transport));
    SetIceServers(servers);
  }

  void SetIceServers(const nsTArray<ParsedIceServer>& servers) {
    ASSERT_TRUE(NS_SUCCEEDED(ice_ctx_->SetIceServers(servers, false)));
  }

  void UseTestStunServer() {
    SetStunServer(TestStunServer::GetInstance(AF_INET)->addr(),
                  TestStunServer::GetInstance(AF_INET)->port());
  }

  void SetTurnServer(const std::string addr, uint16_t port,
                     const std::string username, const std::string password,
                     IceTransport transport) {
    nsTArray<ParsedIceServer> servers;
    servers.AppendElement(
        MakeTurnEntry(addr, port, username, password, transport));
    ASSERT_TRUE(NS_SUCCEEDED(ice_ctx_->SetIceServers(servers, false)));
  }

  void SetTurnServers(const nsTArray<ParsedIceServer>& servers) {
    ASSERT_TRUE(NS_SUCCEEDED(ice_ctx_->SetIceServers(servers, false)));
  }

  void SetFakeResolver(const std::string& ip, const std::string& fqdn) {
    ASSERT_TRUE(NS_SUCCEEDED(dns_resolver_->Init()));
    if (!ip.empty() && !fqdn.empty()) {
      PRNetAddr addr;
      PRStatus status = PR_StringToNetAddr(ip.c_str(), &addr);
      addr.inet.port = kDefaultStunServerPort;
      ASSERT_EQ(PR_SUCCESS, status);
      fake_resolver_.SetAddr(fqdn, addr);
    }
    ASSERT_TRUE(
        NS_SUCCEEDED(ice_ctx_->SetResolver(fake_resolver_.AllocateResolver())));
  }

  void SetDNSResolver() {
    ASSERT_TRUE(NS_SUCCEEDED(dns_resolver_->Init()));
    ASSERT_TRUE(
        NS_SUCCEEDED(ice_ctx_->SetResolver(dns_resolver_->AllocateResolver())));
  }

  void Gather(bool default_route_only = false,
              bool obfuscate_host_addresses = false) {
    nsresult res;

    test_utils_->SyncDispatchToSTS(
        WrapRunnableRet(&res, ice_ctx_, &NrIceCtx::StartGathering,
                        default_route_only, obfuscate_host_addresses));

    ASSERT_TRUE(NS_SUCCEEDED(res));
  }

  void SetCtxFlags(bool default_route_only) {
    test_utils_->SyncDispatchToSTS(
        WrapRunnable(ice_ctx_, &NrIceCtx::SetCtxFlags, default_route_only));
  }

  nsTArray<NrIceStunAddr> GetStunAddrs() { return ice_ctx_->GetStunAddrs(); }

  void SetStunAddrs(const nsTArray<NrIceStunAddr>& addrs) {
    ice_ctx_->SetStunAddrs(addrs);
  }

  void UseNat() {
    test_utils_->SyncDispatchToSTS(WrapRunnable(this, &IceTestPeer::UseNat_s));
  }

  void UseNat_s() { nat_->enabled_ = true; }

  void SetTimerDivider(int div) { ice_ctx_->internal_SetTimerAccelarator(div); }

  void SetStunResponseDelay(uint32_t delay) {
    test_utils_->SyncDispatchToSTS(
        WrapRunnable(this, &IceTestPeer::SetStunResponseDelay_s, delay));
  }

  void SetStunResponseDelay_s(uint32_t delay) {
    nat_->delay_stun_resp_ms_ = delay;
  }

  void SetFilteringType(TestNat::NatBehavior type) {
    test_utils_->SyncDispatchToSTS(
        WrapRunnable(this, &IceTestPeer::SetFilteringType_s, type));
  }

  void SetFilteringType_s(TestNat::NatBehavior type) {
    MOZ_ASSERT(!nat_->has_port_mappings());
    nat_->filtering_type_ = type;
  }

  void SetMappingType(TestNat::NatBehavior type) {
    test_utils_->SyncDispatchToSTS(
        WrapRunnable(this, &IceTestPeer::SetMappingType_s, type));
  }

  void SetMappingType_s(TestNat::NatBehavior type) {
    MOZ_ASSERT(!nat_->has_port_mappings());
    nat_->mapping_type_ = type;
  }

  void SetBlockUdp(bool block) {
    test_utils_->SyncDispatchToSTS(
        WrapRunnable(this, &IceTestPeer::SetBlockUdp_s, block));
  }

  void SetBlockUdp_s(bool block) {
    MOZ_ASSERT(!nat_->has_port_mappings());
    nat_->block_udp_ = block;
  }

  void SetBlockStun(bool block) {
    test_utils_->SyncDispatchToSTS(
        WrapRunnable(this, &IceTestPeer::SetBlockStun_s, block));
  }

  void SetBlockStun_s(bool block) { nat_->block_stun_ = block; }

  // Get various pieces of state
  std::vector<std::string> GetGlobalAttributes() {
    std::vector<std::string> attrs(ice_ctx_->GetGlobalAttributes());
    if (simulate_ice_lite_) {
      attrs.push_back("ice-lite");
    }
    return attrs;
  }

  std::vector<std::string> GetAttributes(size_t stream) {
    std::vector<std::string> v;

    RUN_ON_THREAD(
        test_utils_->sts_target(),
        WrapRunnableRet(&v, this, &IceTestPeer::GetAttributes_s, stream));

    return v;
  }

  std::string FilterCandidate(const std::string& candidate) {
    if (candidate_filter_) {
      return candidate_filter_(candidate);
    }
    return candidate;
  }

  std::vector<std::string> GetAttributes_s(size_t index) {
    std::vector<std::string> attributes;

    auto stream = GetStream_s(index);
    if (!stream) {
      EXPECT_TRUE(false) << "No such stream " << index;
      return attributes;
    }

    std::vector<std::string> attributes_in = stream->GetAttributes();

    for (const auto& attribute : attributes_in) {
      if (attribute.find("candidate:") != std::string::npos) {
        std::string candidate(FilterCandidate(attribute));
        if (!candidate.empty()) {
          std::cerr << name_ << " Returning candidate: " << candidate
                    << std::endl;
          attributes.push_back(candidate);
        }
      } else {
        attributes.push_back(attribute);
      }
    }

    return attributes;
  }

  void SetExpectedTypes(NrIceCandidate::Type local, NrIceCandidate::Type remote,
                        std::string local_transport = kNrIceTransportUdp) {
    expected_local_type_ = local;
    expected_local_transport_ = local_transport;
    expected_remote_type_ = remote;
  }

  void SetExpectedRemoteCandidateAddr(const std::string& addr) {
    expected_remote_addr_ = addr;
  }

  int GetCandidatesPrivateIpv4Range(size_t stream) {
    std::vector<std::string> attributes = GetAttributes(stream);

    int host_net = 0;
    for (const auto& a : attributes) {
      if (a.find("typ host") != std::string::npos) {
        nr_transport_addr addr;
        std::vector<std::string> tokens = split(a, ' ');
        int r = nr_str_port_to_transport_addr(tokens.at(4).c_str(), 0,
                                              IPPROTO_UDP, &addr);
        MOZ_ASSERT(!r);
        if (!r && (addr.ip_version == NR_IPV4)) {
          int n = nr_transport_addr_get_private_addr_range(&addr);
          if (n) {
            if (host_net) {
              // TODO: add support for multiple private interfaces
              std::cerr
                  << "This test doesn't support multiple private interfaces";
              return -1;
            }
            host_net = n;
          }
        }
      }
    }
    return host_net;
  }

  bool gathering_complete() {
    auto lock = mGatheringStates.Lock();
    for (const auto& [id, state] : lock.ref()) {
      (void)id;
      if (state != NrIceMediaStream::ICE_STREAM_GATHER_COMPLETE) {
        return false;
      }
    }
    return true;
  }
  int ready_ct() { return ready_ct_; }
  bool is_ready_s(size_t index) {
    auto media_stream = GetStream_s(index);
    if (!media_stream) {
      EXPECT_TRUE(false) << "No such stream " << index;
      return false;
    }
    return media_stream->state() == NrIceMediaStream::ICE_OPEN;
  }
  bool is_ready(size_t stream) {
    bool result;
    test_utils_->SyncDispatchToSTS(
        WrapRunnableRet(&result, this, &IceTestPeer::is_ready_s, stream));
    return result;
  }
  bool ice_connected() {
    auto lock = mConnectionStates.Lock();
    for (const auto& [id, state] : lock.ref()) {
      if (state != NrIceCtx::ICE_CTX_CONNECTED) {
        return false;
      }
    }
    return true;
  }
  bool ice_failed() {
    auto lock = mConnectionStates.Lock();
    for (const auto& [id, state] : lock.ref()) {
      if (state == NrIceCtx::ICE_CTX_FAILED) {
        return true;
      }
    }
    return false;
  }
  bool ice_checking() {
    if (ice_failed() || ice_connected()) {
      return false;
    }
    auto lock = mConnectionStates.Lock();
    for (const auto& [id, state] : lock.ref()) {
      if (state == NrIceCtx::ICE_CTX_CHECKING) {
        return true;
      }
    }
    return false;
  }
  bool ice_reached_checking() { return ice_reached_checking_; }
  size_t received() { return received_; }
  size_t sent() { return sent_; }

  void RestartIce() {
    test_utils_->SyncDispatchToSTS(
        WrapRunnable(this, &IceTestPeer::RestartIce_s));
  }

  void RestartIce_s() {
    for (auto& stream : ice_ctx_->GetStreams()) {
      SetIceCredentials_s(*stream);
      {
        auto lock = mConnectionStates.Lock();
        lock.ref()[stream->GetId()] = NrIceCtx::ICE_CTX_INIT;
      }
      {
        auto lock = mGatheringStates.Lock();
        lock.ref()[stream->GetId()] = NrIceMediaStream::ICE_STREAM_GATHER_INIT;
      }
    }
    // take care of some local bookkeeping
    ready_ct_ = 0;
    // We do not unset ice_reached_checking_ here, since we do not expect
    // ICE to return to checking in an ICE restart, because the ICE stack
    // continues using the old streams (which are probably connected) until the
    // new ones are connected.
    remote_ = nullptr;
  }

  void RollbackIceRestart() {
    test_utils_->SyncDispatchToSTS(
        WrapRunnable(this, &IceTestPeer::RollbackIceRestart_s));
  }

  void RollbackIceRestart_s() {
    for (auto& stream : ice_ctx_->GetStreams()) {
      mIceCredentials[stream->GetId()] = mOldIceCredentials[stream->GetId()];
    }
  }

  // Start connecting to another peer
  void Connect_s(IceTestPeer* remote, TrickleMode trickle_mode,
                 bool start = true) {
    nsresult res;

    remote_ = remote;

    trickle_mode_ = trickle_mode;
    res = ice_ctx_->ParseGlobalAttributes(remote->GetGlobalAttributes());
    ASSERT_FALSE(remote->simulate_ice_lite_ &&
                 (ice_ctx_->GetControlling() == NrIceCtx::ICE_CONTROLLED));
    ASSERT_TRUE(NS_SUCCEEDED(res));

    for (size_t i = 0; i < stream_counter_; ++i) {
      auto aStream = GetStream_s(i);
      if (aStream) {
        std::vector<std::string> attributes = remote->GetAttributes(i);

        for (auto it = attributes.begin(); it != attributes.end();) {
          if (trickle_mode == TRICKLE_SIMULATE &&
              it->find("candidate:") != std::string::npos) {
            std::cerr << name_ << " Deferring remote candidate: " << *it
                      << std::endl;
            attributes.erase(it);
          } else {
            std::cerr << name_ << " Adding remote attribute: " + *it
                      << std::endl;
            ++it;
          }
        }
        auto credentials = mIceCredentials[aStream->GetId()];
        res = aStream->ConnectToPeer(credentials.first, credentials.second,
                                     attributes);
        ASSERT_TRUE(NS_SUCCEEDED(res));
      }
    }

    if (start) {
      ice_ctx_->SetControlling(offerer_ ? NrIceCtx::ICE_CONTROLLING
                                        : NrIceCtx::ICE_CONTROLLED);
      // Now start checks
      res = ice_ctx_->StartChecks();
      ASSERT_TRUE(NS_SUCCEEDED(res));
    }
  }

  void Connect(IceTestPeer* remote, TrickleMode trickle_mode,
               bool start = true) {
    test_utils_->SyncDispatchToSTS(WrapRunnable(this, &IceTestPeer::Connect_s,
                                                remote, trickle_mode, start));
  }

  void SimulateTrickle(size_t stream) {
    std::cerr << name_ << " Doing trickle for stream " << stream << std::endl;
    // If we are in trickle deferred mode, now trickle in the candidates
    // for |stream|

    std::vector<SchedulableTrickleCandidate*>& candidates =
        ControlTrickle(stream);

    for (auto& candidate : candidates) {
      candidate->Schedule(0);
    }
  }

  // Allows test case to completely control when/if candidates are trickled
  // (test could also do things like insert extra trickle candidates, or
  // change existing ones, or insert duplicates, really anything is fair game)
  std::vector<SchedulableTrickleCandidate*>& ControlTrickle(size_t stream) {
    std::cerr << "Doing controlled trickle for stream " << stream << std::endl;

    std::vector<std::string> attributes = remote_->GetAttributes(stream);

    for (const auto& attribute : attributes) {
      if (attribute.find("candidate:") != std::string::npos) {
        controlled_trickle_candidates_[stream].push_back(
            new SchedulableTrickleCandidate(this, stream, attribute, "",
                                            test_utils_));
      }
    }

    return controlled_trickle_candidates_[stream];
  }

  nsresult TrickleCandidate_s(const std::string& candidate,
                              const std::string& ufrag, size_t index) {
    auto stream = GetStream_s(index);
    if (!stream) {
      // stream might have gone away before the trickle timer popped
      std::cerr << "Trickle candidate has no stream: " << index << std::endl;
      return NS_OK;
    }
    std::cerr << "Trickle candidate for " << index << " (" << stream->GetId()
              << "):" << candidate << std::endl;
    return stream->ParseTrickleCandidate(candidate, ufrag, "");
  }

  void DumpCandidate(std::string which, const NrIceCandidate& cand) {
    std::string type;
    std::string tcp_type;

    std::string addr;
    int port;

    if (which.find("Remote") != std::string::npos) {
      addr = cand.cand_addr.host;
      port = cand.cand_addr.port;
    } else {
      addr = cand.local_addr.host;
      port = cand.local_addr.port;
    }
    switch (cand.type) {
      case NrIceCandidate::ICE_HOST:
        type = "host";
        break;
      case NrIceCandidate::ICE_SERVER_REFLEXIVE:
        type = "srflx";
        break;
      case NrIceCandidate::ICE_PEER_REFLEXIVE:
        type = "prflx";
        break;
      case NrIceCandidate::ICE_RELAYED:
        type = "relay";
        if (which.find("Local") != std::string::npos) {
          type += "(" + cand.local_addr.transport + ")";
        }
        break;
      default:
        FAIL();
    };

    switch (cand.tcp_type) {
      case NrIceCandidate::ICE_NONE:
        break;
      case NrIceCandidate::ICE_ACTIVE:
        tcp_type = " tcptype=active";
        break;
      case NrIceCandidate::ICE_PASSIVE:
        tcp_type = " tcptype=passive";
        break;
      case NrIceCandidate::ICE_SO:
        tcp_type = " tcptype=so";
        break;
      default:
        FAIL();
    };

    std::cerr << which << " --> " << type << " " << addr << ":" << port << "/"
              << cand.cand_addr.transport << tcp_type
              << " codeword=" << cand.codeword << std::endl;
  }

  void DumpAndCheckActiveCandidates_s() {
    std::cerr << name_ << " Active candidates:" << std::endl;
    for (const auto& stream : ice_ctx_->GetStreams()) {
      for (size_t j = 0; j < stream->components(); ++j) {
        std::cerr << name_ << " Stream " << stream->GetId() << " component "
                  << j + 1 << std::endl;

        UniquePtr<NrIceCandidate> local;
        UniquePtr<NrIceCandidate> remote;

        nsresult res = stream->GetActivePair(j + 1, &local, &remote);
        if (res == NS_ERROR_NOT_AVAILABLE) {
          std::cerr << "Component unpaired or disabled." << std::endl;
        } else {
          ASSERT_TRUE(NS_SUCCEEDED(res));
          DumpCandidate("Local  ", *local);
          /* Depending on timing, and the whims of the network
           * stack/configuration we're running on top of, prflx is always a
           * possibility. */
          if (expected_local_type_ == NrIceCandidate::ICE_HOST) {
            ASSERT_NE(NrIceCandidate::ICE_SERVER_REFLEXIVE, local->type);
            ASSERT_NE(NrIceCandidate::ICE_RELAYED, local->type);
          } else {
            ASSERT_EQ(expected_local_type_, local->type);
          }
          ASSERT_EQ(expected_local_transport_, local->local_addr.transport);
          DumpCandidate("Remote ", *remote);
          /* Depending on timing, and the whims of the network
           * stack/configuration we're running on top of, prflx is always a
           * possibility. */
          if (expected_remote_type_ == NrIceCandidate::ICE_HOST) {
            ASSERT_NE(NrIceCandidate::ICE_SERVER_REFLEXIVE, remote->type);
            ASSERT_NE(NrIceCandidate::ICE_RELAYED, remote->type);
          } else {
            ASSERT_EQ(expected_remote_type_, remote->type);
          }
          if (!expected_remote_addr_.empty()) {
            ASSERT_EQ(expected_remote_addr_, remote->cand_addr.host);
          }
        }
      }
    }
  }

  void DumpAndCheckActiveCandidates() {
    test_utils_->SyncDispatchToSTS(
        WrapRunnable(this, &IceTestPeer::DumpAndCheckActiveCandidates_s));
  }

  void Close() {
    test_utils_->SyncDispatchToSTS(
        WrapRunnable(ice_ctx_, &NrIceCtx::destroy_peer_ctx));
  }

  void Shutdown() {
    std::cerr << name_ << " Shutdown" << std::endl;
    shutting_down_ = true;
    for (auto& controlled_trickle_candidate : controlled_trickle_candidates_) {
      for (auto& cand : controlled_trickle_candidate.second) {
        delete cand;
      }
    }

    ice_ctx_->Destroy();
    ice_ctx_ = nullptr;

    if (remote_) {
      remote_->UnsetRemote();
      remote_ = nullptr;
    }
  }

  void UnsetRemote() { remote_ = nullptr; }

  void StartChecks() {
    nsresult res;

    test_utils_->SyncDispatchToSTS(WrapRunnableRet(
        &res, ice_ctx_, &NrIceCtx::SetControlling,
        offerer_ ? NrIceCtx::ICE_CONTROLLING : NrIceCtx::ICE_CONTROLLED));
    // Now start checks
    test_utils_->SyncDispatchToSTS(
        WrapRunnableRet(&res, ice_ctx_, &NrIceCtx::StartChecks));
    ASSERT_TRUE(NS_SUCCEEDED(res));
  }

  // Handle events
  void GatheringStateChange(const std::string& aTransportId,
                            NrIceMediaStream::GatheringState state) {
    if (shutting_down_) {
      return;
    }

    {
      auto lock = mGatheringStates.Lock();
      lock.ref()[aTransportId] = state;
    }

    if (!gathering_complete()) {
      return;
    }

    std::cerr << name_ << " Gathering complete" << std::endl;

    std::cerr << name_ << " ATTRIBUTES:" << std::endl;
    for (const auto& stream : ice_ctx_->GetStreams()) {
      std::cerr << "Stream " << stream->GetId() << std::endl;

      std::vector<std::string> attributes = stream->GetAttributes();

      for (const auto& attribute : attributes) {
        std::cerr << attribute << std::endl;
      }
    }
    std::cerr << std::endl;
  }

  void CandidateInitialized(NrIceMediaStream* stream,
                            const std::string& raw_candidate,
                            const std::string& ufrag,
                            const std::string& mdns_addr,
                            const std::string& actual_addr) {
    std::string candidate(FilterCandidate(raw_candidate));
    if (candidate.empty()) {
      return;
    }
    std::cerr << "Candidate for stream " << stream->GetId()
              << " initialized: " << candidate << std::endl;
    candidates_[stream->GetId()].push_back(candidate);

    // If we are connected, then try to trickle to the other side.
    if (remote_ && remote_->remote_ && (trickle_mode_ != TRICKLE_SIMULATE)) {
      // first, find the index of the stream we've been given so
      // we can get the corresponding stream on the remote side
      for (size_t i = 0; i < stream_counter_; ++i) {
        if (GetStream_s(i) == stream) {
          ASSERT_GT(remote_->stream_counter_, i);
          nsresult res = remote_->GetStream_s(i)->ParseTrickleCandidate(
              candidate, ufrag, "");
          ASSERT_TRUE(NS_SUCCEEDED(res));
          return;
        }
      }
      ADD_FAILURE() << "No matching stream found for " << stream->GetId();
    }
  }

  nsresult GetCandidatePairs_s(size_t stream_index,
                               std::vector<NrIceCandidatePair>* pairs) {
    MOZ_ASSERT(pairs);
    auto stream = GetStream_s(stream_index);
    if (!stream) {
      // Is there a better error for "no such index"?
      ADD_FAILURE() << "No such media stream index: " << stream_index;
      return NS_ERROR_INVALID_ARG;
    }

    return stream->GetCandidatePairs(pairs);
  }

  nsresult GetCandidatePairs(size_t stream_index,
                             std::vector<NrIceCandidatePair>* pairs) {
    nsresult v;
    test_utils_->SyncDispatchToSTS(WrapRunnableRet(
        &v, this, &IceTestPeer::GetCandidatePairs_s, stream_index, pairs));
    return v;
  }

  bool HasNominatedPair(size_t stream_index, uint64_t component_id = 1) {
    std::vector<NrIceCandidatePair> pairs;
    if (NS_FAILED(GetCandidatePairs(stream_index, &pairs))) {
      return false;
    }

    return std::any_of(pairs.begin(), pairs.end(),
                       [component_id](const NrIceCandidatePair& pair) {
                         return pair.component_id == component_id &&
                                pair.nominated;
                       });
  }

  void AssertRegularlyNominatedAndSelectedPair(size_t stream_index,
                                               uint64_t component_id = 1) {
    std::vector<NrIceCandidatePair> pairs;
    ASSERT_EQ(NS_OK, GetCandidatePairs(stream_index, &pairs));

    size_t nominated = 0;
    size_t selected = 0;
    for (const auto& pair : pairs) {
      if (pair.component_id != component_id) {
        continue;
      }
      if (pair.nominated) {
        ++nominated;
      }
      if (pair.selected) {
        ++selected;
        EXPECT_TRUE(pair.nominated);
        EXPECT_EQ(NrIceCandidatePair::STATE_SUCCEEDED, pair.state);
        if (pair.local.type == NrIceCandidate::ICE_HOST) {
          EXPECT_GE(pair.responses_recvd, 2U);
        }
      }
    }

    EXPECT_EQ(1U, nominated);
    EXPECT_EQ(1U, selected);
  }

  void DumpCandidatePair(const NrIceCandidatePair& pair) {
    std::cerr << std::endl;
    DumpCandidate("Local", pair.local);
    DumpCandidate("Remote", pair.remote);
    std::cerr << "state = " << pair.state << " priority = " << pair.priority
              << " nominated = " << pair.nominated
              << " selected = " << pair.selected
              << " codeword = " << pair.codeword << std::endl;
  }

  void DumpCandidatePairs_s(NrIceMediaStream* stream) {
    std::vector<NrIceCandidatePair> pairs;
    nsresult res = stream->GetCandidatePairs(&pairs);
    ASSERT_TRUE(NS_SUCCEEDED(res));

    std::cerr << "Begin list of candidate pairs [" << std::endl;

    for (auto& pair : pairs) {
      DumpCandidatePair(pair);
    }
    std::cerr << "]" << std::endl;
  }

  void DumpCandidatePairs_s() {
    std::cerr << "Dumping candidate pairs for all streams [" << std::endl;
    for (const auto& stream : ice_ctx_->GetStreams()) {
      DumpCandidatePairs_s(stream.get());
    }
    std::cerr << "]" << std::endl;
  }

  bool CandidatePairsPriorityDescending(
      const std::vector<NrIceCandidatePair>& pairs) {
    // Verify that priority is descending
    uint64_t priority = std::numeric_limits<uint64_t>::max();

    for (size_t p = 0; p < pairs.size(); ++p) {
      if (priority < pairs[p].priority) {
        std::cerr << "Priority increased in subsequent pairs:" << std::endl;
        DumpCandidatePair(pairs[p - 1]);
        DumpCandidatePair(pairs[p]);
        return false;
      }
      if (priority == pairs[p].priority) {
        if (!IceCandidatePairCompare()(pairs[p], pairs[p - 1]) &&
            !IceCandidatePairCompare()(pairs[p - 1], pairs[p])) {
          std::cerr << "Ignoring identical pair from trigger check"
                    << std::endl;
        } else {
          std::cerr << "Duplicate priority in subseqent pairs:" << std::endl;
          DumpCandidatePair(pairs[p - 1]);
          DumpCandidatePair(pairs[p]);
          return false;
        }
      }
      priority = pairs[p].priority;
    }
    return true;
  }

  void UpdateAndValidateCandidatePairs(
      size_t stream_index, std::vector<NrIceCandidatePair>* new_pairs) {
    std::vector<NrIceCandidatePair> old_pairs = *new_pairs;
    GetCandidatePairs(stream_index, new_pairs);
    ASSERT_TRUE(CandidatePairsPriorityDescending(*new_pairs))
    << "New list of "
       "candidate pairs is either not sorted in priority order, or has "
       "duplicate priorities.";
    ASSERT_TRUE(CandidatePairsPriorityDescending(old_pairs))
    << "Old list of "
       "candidate pairs is either not sorted in priority order, or has "
       "duplicate priorities. This indicates some bug in the test case.";
    std::vector<NrIceCandidatePair> added_pairs;
    std::vector<NrIceCandidatePair> removed_pairs;

    // set_difference computes the set of elements that are present in the
    // first set, but not the second
    // NrIceCandidatePair::operator< compares based on the priority, local
    // candidate, and remote candidate in that order. This means this will
    // catch cases where the priority has remained the same, but one of the
    // candidates has changed.
    std::set_difference((*new_pairs).begin(), (*new_pairs).end(),
                        old_pairs.begin(), old_pairs.end(),
                        std::inserter(added_pairs, added_pairs.begin()),
                        IceCandidatePairCompare());

    std::set_difference(old_pairs.begin(), old_pairs.end(),
                        (*new_pairs).begin(), (*new_pairs).end(),
                        std::inserter(removed_pairs, removed_pairs.begin()),
                        IceCandidatePairCompare());

    for (auto& added_pair : added_pairs) {
      std::cerr << "Found new candidate pair." << std::endl;
      DumpCandidatePair(added_pair);
    }

    for (auto& removed_pair : removed_pairs) {
      std::cerr << "Pre-existing candidate pair is now missing:" << std::endl;
      DumpCandidatePair(removed_pair);
    }

    ASSERT_TRUE(removed_pairs.empty())
    << "At least one candidate pair has "
       "gone missing.";
  }

  void StreamReady(NrIceMediaStream* stream) {
    ++ready_ct_;
    std::cerr << name_ << " Stream ready for " << stream->name()
              << " ct=" << ready_ct_ << std::endl;
    DumpCandidatePairs_s(stream);
  }
  void StreamFailed(NrIceMediaStream* stream) {
    std::cerr << name_ << " Stream failed for " << stream->name()
              << " ct=" << ready_ct_ << std::endl;
    DumpCandidatePairs_s(stream);
  }

  void ConnectionStateChange(NrIceMediaStream* stream,
                             NrIceCtx::ConnectionState state) {
    {
      auto lock = mConnectionStates.Lock();
      lock.ref()[stream->GetId()] = state;
    }
    if (ice_checking()) {
      ice_reached_checking_ = true;
    }

    switch (state) {
      case NrIceCtx::ICE_CTX_INIT:
        break;
      case NrIceCtx::ICE_CTX_CHECKING:
        std::cerr << name_ << " ICE reached checking (" << stream->GetId()
                  << ")" << std::endl;
        MOZ_ASSERT(ice_reached_checking_);
        break;
      case NrIceCtx::ICE_CTX_CONNECTED:
        std::cerr << name_ << " ICE reached connected (" << stream->GetId()
                  << ")" << std::endl;
        MOZ_ASSERT(ice_reached_checking_);
        break;
      case NrIceCtx::ICE_CTX_COMPLETED:
        std::cerr << name_ << " ICE reached completed (" << stream->GetId()
                  << ")" << std::endl;
        MOZ_ASSERT(ice_reached_checking_);
        break;
      case NrIceCtx::ICE_CTX_FAILED:
        std::cerr << name_ << " ICE reached failed (" << stream->GetId() << ")"
                  << std::endl;
        MOZ_ASSERT(ice_reached_checking_);
        break;
      case NrIceCtx::ICE_CTX_DISCONNECTED:
        std::cerr << name_ << " ICE reached disconnected (" << stream->GetId()
                  << ")" << std::endl;
        MOZ_ASSERT(ice_reached_checking_);
        break;
      case NrIceCtx::ICE_CTX_CLOSED:
        std::cerr << name_ << " ICE reached closed (" << stream->GetId() << ")"
                  << std::endl;
        break;
    }
  }

  void PacketReceived(NrIceMediaStream* stream, int component,
                      const unsigned char* data, int len) {
    std::cerr << name_ << ": received " << len << " bytes" << std::endl;
    ++received_;
  }

  void SendPacket(int stream, int component, const unsigned char* data,
                  int len) {
    auto media_stream = GetStream_s(stream);
    if (!media_stream) {
      ADD_FAILURE() << "No such stream " << stream;
      return;
    }

    ASSERT_TRUE(NS_SUCCEEDED(media_stream->SendPacket(component, data, len)));

    ++sent_;
    std::cerr << name_ << ": sent " << len << " bytes" << std::endl;
  }

  void SendFailure(int stream, int component) {
    auto media_stream = GetStream_s(stream);
    if (!media_stream) {
      ADD_FAILURE() << "No such stream " << stream;
      return;
    }

    const std::string d("FAIL");
    ASSERT_TRUE(NS_FAILED(media_stream->SendPacket(
        component, reinterpret_cast<const unsigned char*>(d.c_str()),
        d.length())));

    std::cerr << name_ << ": send failed as expected" << std::endl;
  }

  void SetCandidateFilter(CandidateFilter filter) {
    candidate_filter_ = filter;
  }

  void ParseCandidate_s(size_t i, const std::string& candidate,
                        const std::string& mdns_addr) {
    auto media_stream = GetStream_s(i);
    ASSERT_TRUE(media_stream.get())
    << "No such stream " << i;
    media_stream->ParseTrickleCandidate(candidate, "", mdns_addr);
  }

  void ParseCandidate(size_t i, const std::string& candidate,
                      const std::string& mdns_addr) {
    test_utils_->SyncDispatchToSTS(WrapRunnable(
        this, &IceTestPeer::ParseCandidate_s, i, candidate, mdns_addr));
  }

  void DisableComponent_s(size_t index, int component_id) {
    ASSERT_LT(index, stream_counter_);
    auto stream = GetStream_s(index);
    ASSERT_TRUE(stream.get())
    << "No such stream " << index;
    nsresult res = stream->DisableComponent(component_id);
    ASSERT_TRUE(NS_SUCCEEDED(res));
  }

  void DisableComponent(size_t stream, int component_id) {
    test_utils_->SyncDispatchToSTS(WrapRunnable(
        this, &IceTestPeer::DisableComponent_s, stream, component_id));
  }

  void AssertConsentRefresh_s(size_t index, int component_id,
                              ConsentStatus status) {
    ASSERT_LT(index, stream_counter_);
    auto stream = GetStream_s(index);
    ASSERT_TRUE(stream.get())
    << "No such stream " << index;
    bool can_send;
    struct timeval timestamp;
    nsresult res =
        stream->GetConsentStatus(component_id, &can_send, &timestamp);
    ASSERT_TRUE(NS_SUCCEEDED(res));
    if (status == CONSENT_EXPIRED) {
      ASSERT_EQ(can_send, 0);
    } else {
      ASSERT_EQ(can_send, 1);
    }
    if (consent_timestamp_.tv_sec) {
      if (status == CONSENT_FRESH) {
        ASSERT_EQ(r_timeval_cmp(&timestamp, &consent_timestamp_), 1);
      } else {
        ASSERT_EQ(r_timeval_cmp(&timestamp, &consent_timestamp_), 0);
      }
    }
    consent_timestamp_.tv_sec = timestamp.tv_sec;
    consent_timestamp_.tv_usec = timestamp.tv_usec;
    std::cerr << name_
              << ": new consent timestamp = " << consent_timestamp_.tv_sec
              << "." << consent_timestamp_.tv_usec << std::endl;
  }

  void AssertConsentRefresh(ConsentStatus status) {
    test_utils_->SyncDispatchToSTS(
        WrapRunnable(this, &IceTestPeer::AssertConsentRefresh_s, 0, 1, status));
  }

  void ChangeNetworkState_s(bool online) {
    ice_ctx_->UpdateNetworkState(online);
  }

  void ChangeNetworkStateToOffline() {
    test_utils_->SyncDispatchToSTS(
        WrapRunnable(this, &IceTestPeer::ChangeNetworkState_s, false));
  }

  void ChangeNetworkStateToOnline() {
    test_utils_->SyncDispatchToSTS(
        WrapRunnable(this, &IceTestPeer::ChangeNetworkState_s, true));
  }

  void SetControlling(NrIceCtx::Controlling controlling) {
    nsresult res;
    test_utils_->SyncDispatchToSTS(WrapRunnableRet(
        &res, ice_ctx_, &NrIceCtx::SetControlling, controlling));
    ASSERT_TRUE(NS_SUCCEEDED(res));
  }

  NrIceCtx::Controlling GetControlling() { return ice_ctx_->GetControlling(); }

  void SetTiebreaker(uint64_t tiebreaker) {
    test_utils_->SyncDispatchToSTS(
        WrapRunnable(this, &IceTestPeer::SetTiebreaker_s, tiebreaker));
  }

  void SetTiebreaker_s(uint64_t tiebreaker) {
    ice_ctx_->peer()->tiebreaker = tiebreaker;
  }

  void SimulateIceLite() {
    simulate_ice_lite_ = true;
    SetControlling(NrIceCtx::ICE_CONTROLLED);
  }

  nsresult GetDefaultCandidate(unsigned int stream, NrIceCandidate* cand) {
    nsresult rv;

    test_utils_->SyncDispatchToSTS(WrapRunnableRet(
        &rv, this, &IceTestPeer::GetDefaultCandidate_s, stream, cand));

    return rv;
  }

  nsresult GetDefaultCandidate_s(unsigned int index, NrIceCandidate* cand) {
    return GetStream_s(index)->GetDefaultCandidate(1, cand);
  }

 private:
  std::string name_;
  RefPtr<NrIceCtx> ice_ctx_;
  bool offerer_;
  std::map<std::string, std::vector<std::string>> candidates_;
  // Maps from stream id to list of remote trickle candidates
  std::map<size_t, std::vector<SchedulableTrickleCandidate*>>
      controlled_trickle_candidates_;
  std::map<std::string, std::pair<std::string, std::string>> mIceCredentials;
  std::map<std::string, std::pair<std::string, std::string>> mOldIceCredentials;
  size_t stream_counter_;
  bool shutting_down_;
  DataMutex<std::map<std::string, NrIceCtx::ConnectionState>> mConnectionStates;
  DataMutex<std::map<std::string, NrIceMediaStream::GatheringState>>
      mGatheringStates;
  std::atomic<int> ready_ct_;
  bool ice_reached_checking_;
  std::atomic<size_t> received_;
  std::atomic<size_t> sent_;
  struct timeval consent_timestamp_;
  NrIceResolverFake fake_resolver_;
  RefPtr<NrIceResolver> dns_resolver_;
  IceTestPeer* remote_;
  CandidateFilter candidate_filter_;
  NrIceCandidate::Type expected_local_type_;
  std::string expected_local_transport_;
  NrIceCandidate::Type expected_remote_type_;
  std::string expected_remote_addr_;
  TrickleMode trickle_mode_;
  bool simulate_ice_lite_;
  RefPtr<mozilla::TestNat> nat_;
  MtransportTestUtils* test_utils_;
};

void SchedulableTrickleCandidate::Trickle() {
  timer_handle_ = nullptr;
  nsresult res = peer_->TrickleCandidate_s(candidate_, ufrag_, stream_);
  ASSERT_TRUE(NS_SUCCEEDED(res));
}

class WebRtcIceGatherTest : public StunTest {
 public:
  void SetUp() override {
    StunTest::SetUp();

    Preferences::SetInt("media.peerconnection.ice.tcp_so_sock_count", 3);

    test_utils_->SyncDispatchToSTS(WrapRunnable(
        TestStunServer::GetInstance(AF_INET), &TestStunServer::Reset));
    if (TestStunServer::GetInstance(AF_INET6)) {
      test_utils_->SyncDispatchToSTS(WrapRunnable(
          TestStunServer::GetInstance(AF_INET6), &TestStunServer::Reset));
    }
  }

  void TearDown() override {
    peer_ = nullptr;
    StunTest::TearDown();
  }

  void EnsurePeer() {
    if (!peer_) {
      peer_ =
          MakeUnique<IceTestPeer>("P1", test_utils_, true, NrIceCtx::Config());
    }
  }

  void Gather(unsigned int waitTime = kDefaultTimeout,
              bool default_route_only = false,
              bool obfuscate_host_addresses = false) {
    EnsurePeer();
    peer_->Gather(default_route_only, obfuscate_host_addresses);

    if (waitTime) {
      WaitForGather(waitTime);
    }
  }

  void WaitForGather(unsigned int waitTime = kDefaultTimeout) {
    ASSERT_TRUE_WAIT(peer_->gathering_complete(), waitTime);
  }

  void AddStunServerWithResponse(const std::string& fake_addr,
                                 uint16_t fake_port, const std::string& fqdn,
                                 IceTransport transport,
                                 nsTArray<ParsedIceServer>* stun_servers) {
    int family;
    if (fake_addr.find(':') != std::string::npos) {
      family = AF_INET6;
    } else {
      family = AF_INET;
    }

    std::string stun_addr;
    uint16_t stun_port;
    if (transport == IceTransport::Udp) {
      TestStunServer::GetInstance(family)->SetResponseAddr(fake_addr,
                                                           fake_port);
      stun_addr = TestStunServer::GetInstance(family)->addr();
      stun_port = TestStunServer::GetInstance(family)->port();
    } else if (transport == IceTransport::Tcp) {
      TestStunTcpServer::GetInstance(family)->SetResponseAddr(fake_addr,
                                                              fake_port);
      stun_addr = TestStunTcpServer::GetInstance(family)->addr();
      stun_port = TestStunTcpServer::GetInstance(family)->port();
    } else {
      MOZ_CRASH();
    }

    if (!fqdn.empty()) {
      peer_->SetFakeResolver(stun_addr, fqdn);
      stun_addr = fqdn;
    }

    stun_servers->AppendElement(MakeStunEntry(stun_addr, stun_port, transport));
  }

  void UseFakeStunUdpServerWithResponse(
      const std::string& fake_addr, uint16_t fake_port,
      const std::string& fqdn = std::string()) {
    EnsurePeer();
    nsTArray<ParsedIceServer> stun_servers;
    AddStunServerWithResponse(fake_addr, fake_port, fqdn, IceTransport::Udp,
                              &stun_servers);
    peer_->SetIceServers(stun_servers);
  }

  void UseFakeStunTcpServerWithResponse(
      const std::string& fake_addr, uint16_t fake_port,
      const std::string& fqdn = std::string()) {
    EnsurePeer();
    nsTArray<ParsedIceServer> stun_servers;
    AddStunServerWithResponse(fake_addr, fake_port, fqdn, IceTransport::Tcp,
                              &stun_servers);
    peer_->SetIceServers(stun_servers);
  }

  void UseFakeStunUdpTcpServersWithResponse(const std::string& fake_udp_addr,
                                            uint16_t fake_udp_port,
                                            const std::string& fake_tcp_addr,
                                            uint16_t fake_tcp_port) {
    EnsurePeer();
    nsTArray<ParsedIceServer> stun_servers;
    AddStunServerWithResponse(fake_udp_addr, fake_udp_port,
                              "",  // no fqdn
                              IceTransport::Udp, &stun_servers);
    AddStunServerWithResponse(fake_tcp_addr, fake_tcp_port,
                              "",  // no fqdn
                              IceTransport::Tcp, &stun_servers);

    peer_->SetIceServers(stun_servers);
  }

  void UseTestStunServer() {
    TestStunServer::GetInstance(AF_INET)->Reset();
    peer_->SetStunServer(TestStunServer::GetInstance(AF_INET)->addr(),
                         TestStunServer::GetInstance(AF_INET)->port());
  }

  // NB: Only does substring matching, watch out for stuff like "1.2.3.4"
  // matching "21.2.3.47". " 1.2.3.4 " should not have false positives.
  bool StreamHasMatchingCandidate(unsigned int stream, const std::string& match,
                                  const std::string& match2 = "") {
    std::vector<std::string> attributes = peer_->GetAttributes(stream);
    for (auto& attribute : attributes) {
      if (std::string::npos != attribute.find(match)) {
        if (!match2.length() || std::string::npos != attribute.find(match2)) {
          return true;
        }
      }
    }
    return false;
  }

  void DumpAttributes(unsigned int stream) {
    std::vector<std::string> attributes = peer_->GetAttributes(stream);

    std::cerr << "Attributes for stream " << stream << "->" << attributes.size()
              << std::endl;

    for (const auto& a : attributes) {
      std::cerr << "Attribute: " << a << std::endl;
    }
  }

 protected:
  mozilla::UniquePtr<IceTestPeer> peer_;
};

class WebRtcIceConnectTest : public StunTest {
 public:
  WebRtcIceConnectTest()
      : initted_(false),
        test_stun_server_inited_(false),
        use_nat_(false),
        filtering_type_(TestNat::ENDPOINT_INDEPENDENT),
        mapping_type_(TestNat::ENDPOINT_INDEPENDENT),
        block_udp_(false) {}

  void SetUp() override {
    StunTest::SetUp();

    nsresult rv;
    target_ = do_GetService(NS_SOCKETTRANSPORTSERVICE_CONTRACTID, &rv);
    ASSERT_TRUE(NS_SUCCEEDED(rv));
  }

  void TearDown() override {
    p1_ = nullptr;
    p2_ = nullptr;

    StunTest::TearDown();
  }

  void AddStream(int components) {
    Init();
    p1_->AddStream(components);
    p2_->AddStream(components);
  }

  void RemoveStream(size_t index) {
    p1_->RemoveStream(index);
    p2_->RemoveStream(index);
  }

  void Init(bool setup_stun_servers = true,
            NrIceCtx::Config config = NrIceCtx::Config()) {
    if (initted_) {
      return;
    }

    p1_ = MakeUnique<IceTestPeer>("P1", test_utils_, true, config);
    p2_ = MakeUnique<IceTestPeer>("P2", test_utils_, false, config);
    InitPeer(p1_.get(), setup_stun_servers);
    InitPeer(p2_.get(), setup_stun_servers);

    initted_ = true;
  }

  void InitPeer(IceTestPeer* peer, bool setup_stun_servers = true) {
    if (use_nat_) {
      // If we enable nat simulation, but still use a real STUN server somewhere
      // on the internet, we will see failures if there is a real NAT in
      // addition to our simulated one, particularly if it disallows
      // hairpinning.
      if (setup_stun_servers) {
        InitTestStunServer();
        peer->UseTestStunServer();
      }
      peer->UseNat();
      peer->SetFilteringType(filtering_type_);
      peer->SetMappingType(mapping_type_);
      peer->SetBlockUdp(block_udp_);
    } else if (setup_stun_servers) {
      if (stun_server_address_.empty()) {
        InitTestStunServer();
        peer->UseTestStunServer();
      } else {
        nsTArray<ParsedIceServer> stun_servers;
        stun_servers.AppendElement(MakeStunEntry(
            stun_server_address_, kDefaultStunServerPort, IceTransport::Udp));
        peer->SetIceServers(stun_servers);
      }
    }
  }

  bool Gather(unsigned int waitTime = kDefaultTimeout,
              bool default_route_only = false) {
    Init();

    return GatherCallerAndCallee(p1_.get(), p2_.get(), waitTime,
                                 default_route_only);
  }

  bool GatherCallerAndCallee(IceTestPeer* caller, IceTestPeer* callee,
                             unsigned int waitTime = kDefaultTimeout,
                             bool default_route_only = false) {
    caller->Gather(default_route_only);
    callee->Gather(default_route_only);

    if (waitTime) {
      EXPECT_TRUE_WAIT(caller->gathering_complete(), waitTime);
      if (!caller->gathering_complete()) return false;
      EXPECT_TRUE_WAIT(callee->gathering_complete(), waitTime);
      if (!callee->gathering_complete()) return false;
    }
    return true;
  }

  void UseNat() {
    // to be useful, this method should be called before Init
    ASSERT_FALSE(initted_);
    use_nat_ = true;
  }

  void SetFilteringType(TestNat::NatBehavior type) {
    // to be useful, this method should be called before Init
    ASSERT_FALSE(initted_);
    filtering_type_ = type;
  }

  void SetMappingType(TestNat::NatBehavior type) {
    // to be useful, this method should be called before Init
    ASSERT_FALSE(initted_);
    mapping_type_ = type;
  }

  void BlockUdp() {
    // note: |block_udp_| is used only in InitPeer.
    // Use IceTestPeer::SetBlockUdp to act on the peer directly.
    block_udp_ = true;
  }

  void SetupAndCheckConsent() {
    p1_->SetTimerDivider(10);
    p2_->SetTimerDivider(10);
    ASSERT_TRUE(Gather());
    Connect();
    p1_->AssertConsentRefresh(CONSENT_FRESH);
    p2_->AssertConsentRefresh(CONSENT_FRESH);
    SendReceive();
  }

  void AssertConsentRefresh(ConsentStatus status = CONSENT_FRESH) {
    p1_->AssertConsentRefresh(status);
    p2_->AssertConsentRefresh(status);
  }

  void InitTestStunServer() {
    if (test_stun_server_inited_) {
      return;
    }

    std::cerr << "Resetting TestStunServer" << std::endl;
    TestStunServer::GetInstance(AF_INET)->Reset();
    test_stun_server_inited_ = true;
  }

  void UseTestStunServer() {
    InitTestStunServer();
    p1_->UseTestStunServer();
    p2_->UseTestStunServer();
  }

  void SetTurnServer(const std::string addr, uint16_t port,
                     const std::string username, const std::string password,
                     IceTransport transport = IceTransport::Udp) {
    p1_->SetTurnServer(addr, port, username, password, transport);
    p2_->SetTurnServer(addr, port, username, password, transport);
  }

  void SetTurnServers(const nsTArray<ParsedIceServer>& servers) {
    p1_->SetTurnServers(servers);
    p2_->SetTurnServers(servers);
  }

  void SetCandidateFilter(CandidateFilter filter, bool both = true) {
    p1_->SetCandidateFilter(filter);
    if (both) {
      p2_->SetCandidateFilter(filter);
    }
  }

  void Connect() { ConnectCallerAndCallee(p1_.get(), p2_.get()); }

  void ConnectCallerAndCallee(IceTestPeer* caller, IceTestPeer* callee,
                              TrickleMode mode = TRICKLE_NONE) {
    ASSERT_TRUE(caller->ready_ct() == 0);
    ASSERT_TRUE(caller->ice_connected() == 0);
    ASSERT_TRUE(callee->ready_ct() == 0);
    ASSERT_TRUE(callee->ice_connected() == 0);

    // IceTestPeer::Connect grabs attributes from the first arg, and
    // gives them to |this|, meaning that callee->Connect(caller, ...)
    // simulates caller sending an offer to callee. Order matters here
    // because it determines which peer is controlling.
    callee->Connect(caller, mode);
    caller->Connect(callee, mode);

    if (mode != TRICKLE_SIMULATE) {
      ASSERT_TRUE_WAIT(caller->ice_connected() && callee->ice_connected(),
                       kDefaultTimeout);
      ASSERT_TRUE(caller->ready_ct() >= 1 && callee->ready_ct() >= 1);
      ASSERT_TRUE(caller->ice_reached_checking());
      ASSERT_TRUE(callee->ice_reached_checking());

      caller->DumpAndCheckActiveCandidates();
      callee->DumpAndCheckActiveCandidates();
    }
  }

  void ConnectIceLite(IceTestPeer* lite, IceTestPeer* full,
                      TrickleMode mode = TRICKLE_NONE) {
    lite->Connect(full, mode, false);
    full->Connect(lite, mode);

    EXPECT_EQ(NrIceCtx::ICE_CONTROLLED, lite->GetControlling());
    EXPECT_EQ(NrIceCtx::ICE_CONTROLLING, full->GetControlling());
    if (mode != TRICKLE_SIMULATE) {
      ASSERT_TRUE_WAIT(full->ice_connected(), kDefaultTimeout);
    }
    EXPECT_FALSE(lite->ice_reached_checking());
  }

  void SetExpectedTypes(NrIceCandidate::Type local, NrIceCandidate::Type remote,
                        std::string transport = kNrIceTransportUdp) {
    p1_->SetExpectedTypes(local, remote, transport);
    p2_->SetExpectedTypes(local, remote, transport);
  }

  void SetExpectedRemoteCandidateAddr(const std::string& addr) {
    p1_->SetExpectedRemoteCandidateAddr(addr);
    p2_->SetExpectedRemoteCandidateAddr(addr);
  }

  void ConnectP1(TrickleMode mode = TRICKLE_NONE) {
    p1_->Connect(p2_.get(), mode);
  }

  void ConnectP2(TrickleMode mode = TRICKLE_NONE) {
    p2_->Connect(p1_.get(), mode);
  }

  void WaitForConnectedStreams(int expected_streams = 1) {
    ASSERT_TRUE_WAIT(p1_->ready_ct() == expected_streams &&
                         p2_->ready_ct() == expected_streams,
                     kDefaultTimeout);
    ASSERT_TRUE_WAIT(p1_->ice_connected() && p2_->ice_connected(),
                     kDefaultTimeout);
  }

  void AssertCheckingReached() {
    ASSERT_TRUE(p1_->ice_reached_checking());
    ASSERT_TRUE(p2_->ice_reached_checking());
  }

  void WaitForConnected(unsigned int timeout = kDefaultTimeout) {
    ASSERT_TRUE_WAIT(p1_->ice_connected(), timeout);
    ASSERT_TRUE_WAIT(p2_->ice_connected(), timeout);
  }

  void WaitForGather() {
    ASSERT_TRUE_WAIT(p1_->gathering_complete(), kDefaultTimeout);
    ASSERT_TRUE_WAIT(p2_->gathering_complete(), kDefaultTimeout);
  }

  void WaitForDisconnected(unsigned int timeout = kDefaultTimeout) {
    ASSERT_TRUE(p1_->ice_connected());
    ASSERT_TRUE(p2_->ice_connected());
    ASSERT_TRUE_WAIT(p1_->ice_connected() == 0 && p2_->ice_connected() == 0,
                     timeout);
  }

  void WaitForFailed(unsigned int timeout = kDefaultTimeout) {
    ASSERT_TRUE_WAIT(p1_->ice_failed() && p2_->ice_failed(), timeout);
  }

  void ConnectTrickle(TrickleMode trickle = TRICKLE_SIMULATE) {
    p2_->Connect(p1_.get(), trickle);
    p1_->Connect(p2_.get(), trickle);
  }

  void SimulateTrickle(size_t stream) {
    p1_->SimulateTrickle(stream);
    p2_->SimulateTrickle(stream);
    ASSERT_TRUE_WAIT(p1_->is_ready(stream), kDefaultTimeout);
    ASSERT_TRUE_WAIT(p2_->is_ready(stream), kDefaultTimeout);
  }

  void SimulateTrickleP1(size_t stream) { p1_->SimulateTrickle(stream); }

  void SimulateTrickleP2(size_t stream) { p2_->SimulateTrickle(stream); }

  void CloseP1() { p1_->Close(); }

  void ConnectThenDelete() {
    p2_->Connect(p1_.get(), TRICKLE_NONE, false);
    p1_->Connect(p2_.get(), TRICKLE_NONE, true);
    test_utils_->SyncDispatchToSTS(
        WrapRunnable(this, &WebRtcIceConnectTest::CloseP1));
    p2_->StartChecks();

    // Wait to see if we crash
    PR_Sleep(PR_MillisecondsToInterval(kDefaultTimeout));
  }

  // default is p1_ sending to p2_
  void SendReceive() { SendReceive(p1_.get(), p2_.get()); }

  void SendReceive(IceTestPeer* p1, IceTestPeer* p2,
                   bool expect_tx_failure = false,
                   bool expect_rx_failure = false) {
    size_t previousSent = p1->sent();
    size_t previousReceived = p2->received();

    if (expect_tx_failure) {
      test_utils_->SyncDispatchToSTS(
          WrapRunnable(p1, &IceTestPeer::SendFailure, 0, 1));
      ASSERT_EQ(previousSent, p1->sent());
    } else {
      test_utils_->SyncDispatchToSTS(
          WrapRunnable(p1, &IceTestPeer::SendPacket, 0, 1,
                       reinterpret_cast<const unsigned char*>("TEST"), 4));
      ASSERT_EQ(previousSent + 1, p1->sent());
    }
    if (expect_rx_failure) {
      usleep(1000);
      ASSERT_EQ(previousReceived, p2->received());
    } else {
      ASSERT_TRUE_WAIT(p2->received() == previousReceived + 1, 1000);
    }
  }

  void SendFailure() {
    test_utils_->SyncDispatchToSTS(
        WrapRunnable(p1_.get(), &IceTestPeer::SendFailure, 0, 1));
  }

 protected:
  bool initted_;
  bool test_stun_server_inited_;
  nsCOMPtr<nsIEventTarget> target_;
  mozilla::UniquePtr<IceTestPeer> p1_;
  mozilla::UniquePtr<IceTestPeer> p2_;
  bool use_nat_;
  TestNat::NatBehavior filtering_type_;
  TestNat::NatBehavior mapping_type_;
  bool block_udp_;
};

class WebRtcIcePrioritizerTest : public StunTest {
 public:
  WebRtcIcePrioritizerTest() : prioritizer_(nullptr) {}

  ~WebRtcIcePrioritizerTest() {
    if (prioritizer_) {
      nr_interface_prioritizer_destroy(&prioritizer_);
    }
  }

  void SetPriorizer(nr_interface_prioritizer* prioritizer) {
    prioritizer_ = prioritizer;
  }

  void AddInterface(const std::string& num, int type, int estimated_speed) {
    std::string str_addr = "10.0.0." + num;
    std::string ifname = "eth" + num;
    nr_local_addr local_addr;
    local_addr.interface.type = type;
    local_addr.interface.estimated_speed = estimated_speed;

    int r = nr_str_port_to_transport_addr(str_addr.c_str(), 0, IPPROTO_UDP,
                                          &(local_addr.addr));
    ASSERT_EQ(0, r);
    strncpy(local_addr.addr.ifname, ifname.c_str(), MAXIFNAME - 1);
    local_addr.addr.ifname[MAXIFNAME - 1] = '\0';

    r = nr_interface_prioritizer_add_interface(prioritizer_, &local_addr);
    ASSERT_EQ(0, r);
    r = nr_interface_prioritizer_sort_preference(prioritizer_);
    ASSERT_EQ(0, r);
  }

  void HasLowerPreference(const std::string& num1, const std::string& num2) {
    std::string key1 = "eth" + num1 + ":10.0.0." + num1;
    std::string key2 = "eth" + num2 + ":10.0.0." + num2;
    UCHAR pref1, pref2;
    int r = nr_interface_prioritizer_get_priority(prioritizer_, key1.c_str(),
                                                  &pref1);
    ASSERT_EQ(0, r);
    r = nr_interface_prioritizer_get_priority(prioritizer_, key2.c_str(),
                                              &pref2);
    ASSERT_EQ(0, r);
    ASSERT_LE(pref1, pref2);
  }

 private:
  nr_interface_prioritizer* prioritizer_;
};

class WebRtcIcePacketFilterTest : public StunTest {
 public:
  WebRtcIcePacketFilterTest() : udp_filter_(nullptr), tcp_filter_(nullptr) {}

  void SetUp() {
    StunTest::SetUp();

    test_utils_->SyncDispatchToSTS(
        WrapRunnable(this, &WebRtcIcePacketFilterTest::SetUp_s));

    nsCOMPtr<nsISocketFilterHandler> udp_handler =
        do_GetService(NS_STUN_UDP_SOCKET_FILTER_HANDLER_CONTRACTID);
    ASSERT_TRUE(udp_handler);
    udp_handler->NewFilter(getter_AddRefs(udp_filter_));

    nsCOMPtr<nsISocketFilterHandler> tcp_handler =
        do_GetService(NS_STUN_TCP_SOCKET_FILTER_HANDLER_CONTRACTID);
    ASSERT_TRUE(tcp_handler);
    tcp_handler->NewFilter(getter_AddRefs(tcp_filter_));
  }

  void SetUp_s() {
    NrIceCtx::InitializeGlobals(NrIceCtx::GlobalConfig());
    // Set up enough of the ICE ctx to allow the packet filter to work
    ice_ctx_ = NrIceCtx::Create("test");
  }

  void TearDown() {
    test_utils_->SyncDispatchToSTS(
        WrapRunnable(this, &WebRtcIcePacketFilterTest::TearDown_s));
    StunTest::TearDown();
  }

  void TearDown_s() { ice_ctx_ = nullptr; }

  void TestIncoming(const uint8_t* data, uint32_t len, uint8_t from_addr,
                    int from_port, bool expected_result) {
    mozilla::net::NetAddr addr;
    MakeNetAddr(&addr, from_addr, from_port);
    bool result;
    nsresult rv = udp_filter_->FilterPacket(
        &addr, data, len, nsISocketFilter::SF_INCOMING, &result);
    ASSERT_EQ(NS_OK, rv);
    ASSERT_EQ(expected_result, result);
  }

  void TestIncomingTcp(const uint8_t* data, uint32_t len,
                       bool expected_result) {
    mozilla::net::NetAddr addr;
    bool result;
    nsresult rv = tcp_filter_->FilterPacket(
        &addr, data, len, nsISocketFilter::SF_INCOMING, &result);
    ASSERT_EQ(NS_OK, rv);
    ASSERT_EQ(expected_result, result);
  }

  void TestIncomingTcpFramed(const uint8_t* data, uint32_t len,
                             bool expected_result) {
    mozilla::net::NetAddr addr;
    bool result;
    uint8_t* framed_data = new uint8_t[len + 2];
    framed_data[0] = htons(len);
    memcpy(&framed_data[2], data, len);
    nsresult rv = tcp_filter_->FilterPacket(
        &addr, framed_data, len + 2, nsISocketFilter::SF_INCOMING, &result);
    ASSERT_EQ(NS_OK, rv);
    ASSERT_EQ(expected_result, result);
    delete[] framed_data;
  }

  void TestOutgoing(const uint8_t* data, uint32_t len, uint8_t to_addr,
                    int to_port, bool expected_result) {
    mozilla::net::NetAddr addr;
    MakeNetAddr(&addr, to_addr, to_port);
    bool result;
    nsresult rv = udp_filter_->FilterPacket(
        &addr, data, len, nsISocketFilter::SF_OUTGOING, &result);
    ASSERT_EQ(NS_OK, rv);
    ASSERT_EQ(expected_result, result);
  }

  void TestOutgoingTcp(const uint8_t* data, uint32_t len,
                       bool expected_result) {
    mozilla::net::NetAddr addr;
    bool result;
    nsresult rv = tcp_filter_->FilterPacket(
        &addr, data, len, nsISocketFilter::SF_OUTGOING, &result);
    ASSERT_EQ(NS_OK, rv);
    ASSERT_EQ(expected_result, result);
  }

  void TestOutgoingTcpFramed(const uint8_t* data, uint32_t len,
                             bool expected_result) {
    mozilla::net::NetAddr addr;
    bool result;
    uint8_t* framed_data = new uint8_t[len + 2];
    framed_data[0] = htons(len);
    memcpy(&framed_data[2], data, len);
    nsresult rv = tcp_filter_->FilterPacket(
        &addr, framed_data, len + 2, nsISocketFilter::SF_OUTGOING, &result);
    ASSERT_EQ(NS_OK, rv);
    ASSERT_EQ(expected_result, result);
    delete[] framed_data;
  }

 private:
  void MakeNetAddr(mozilla::net::NetAddr* net_addr, uint8_t last_digit,
                   uint16_t port) {
    net_addr->inet.family = AF_INET;
    net_addr->inet.ip = 192 << 24 | 168 << 16 | 1 << 8 | last_digit;
    net_addr->inet.port = port;
  }

  nsCOMPtr<nsISocketFilter> udp_filter_;
  nsCOMPtr<nsISocketFilter> tcp_filter_;
  RefPtr<NrIceCtx> ice_ctx_;
};
}  // end namespace

TEST_F(WebRtcIceGatherTest, TestGatherFakeStunServerHostnameNoResolver) {
  if (stun_server_hostname_.empty()) {
    return;
  }

  NrIceCtx::GlobalConfig globalConfig;
  globalConfig.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(globalConfig);
  EnsurePeer();
  peer_->SetStunServer(stun_server_hostname_, kDefaultStunServerPort);
  peer_->AddStream(1);
  Gather();
}

// Disabled because google isn't running any TCP stun servers right now
TEST_F(WebRtcIceGatherTest,
       DISABLED_TestGatherFakeStunServerTcpHostnameNoResolver) {
  if (stun_server_hostname_.empty()) {
    return;
  }

  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = true;
  NrIceCtx::InitializeGlobals(config);
  EnsurePeer();
  peer_->SetStunServer(stun_server_hostname_, kDefaultStunServerPort,
                       IceTransport::Tcp);
  peer_->AddStream(1);
  Gather();
  ASSERT_TRUE(StreamHasMatchingCandidate(0, " TCP "));
}

TEST_F(WebRtcIceGatherTest, TestGatherFakeStunServerIpAddress) {
  if (stun_server_address_.empty()) {
    return;
  }

  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  EnsurePeer();
  peer_->SetStunServer(stun_server_address_, kDefaultStunServerPort);
  peer_->SetFakeResolver(stun_server_address_, stun_server_hostname_);
  peer_->AddStream(1);
  Gather();
}

TEST_F(WebRtcIceGatherTest, TestGatherStunServerIpAddressNoHost) {
  if (stun_server_address_.empty()) {
    return;
  }

  {
    NrIceCtx::GlobalConfig config;
    config.mTcpEnabled = false;
    NrIceCtx::InitializeGlobals(config);
  }

  NrIceCtx::Config config;
  config.mPolicy = NrIceCtx::ICE_POLICY_NO_HOST;
  peer_ = MakeUnique<IceTestPeer>("P1", test_utils_, true, config);
  peer_->AddStream(1);
  peer_->SetStunServer(stun_server_address_, kDefaultStunServerPort);
  peer_->SetFakeResolver(stun_server_address_, stun_server_hostname_);
  Gather();
  ASSERT_FALSE(StreamHasMatchingCandidate(0, " host "));
}

TEST_F(WebRtcIceGatherTest, TestGatherFakeStunServerHostname) {
  if (stun_server_hostname_.empty()) {
    return;
  }

  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  EnsurePeer();
  peer_->SetStunServer(stun_server_hostname_, kDefaultStunServerPort);
  peer_->SetFakeResolver(stun_server_address_, stun_server_hostname_);
  peer_->AddStream(1);
  Gather();
}

TEST_F(WebRtcIceGatherTest, TestGatherFakeStunBogusHostname) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  EnsurePeer();
  peer_->SetStunServer(kBogusStunServerHostname, kDefaultStunServerPort);
  peer_->SetFakeResolver(stun_server_address_, stun_server_hostname_);
  peer_->AddStream(1);
  Gather();
}

TEST_F(WebRtcIceGatherTest, TestGatherDNSStunServerIpAddress) {
  if (stun_server_address_.empty()) {
    return;
  }

  {
    NrIceCtx::GlobalConfig config;
    config.mTcpEnabled = false;
    NrIceCtx::InitializeGlobals(config);
  }

  // A srflx candidate is considered redundant and discarded if its address
  // equals that of a host candidate. (Frequently, a srflx candidate and a host
  // candidate have equal addresses when the agent is not behind a NAT.) So set
  // ICE_POLICY_NO_HOST here to ensure that a srflx candidate is not falsely
  // discarded in this test.
  NrIceCtx::Config config;
  config.mPolicy = NrIceCtx::ICE_POLICY_NO_HOST;
  peer_ = MakeUnique<IceTestPeer>("P1", test_utils_, true, config);

  peer_->SetStunServer(stun_server_address_, kDefaultStunServerPort);
  peer_->SetDNSResolver();
  peer_->AddStream(1);
  Gather();
  ASSERT_TRUE(StreamHasMatchingCandidate(0, " UDP "));
  ASSERT_TRUE(StreamHasMatchingCandidate(0, "typ srflx raddr"));
}

// Disabled because google isn't running any TCP stun servers right now
TEST_F(WebRtcIceGatherTest, DISABLED_TestGatherDNSStunServerIpAddressTcp) {
  if (stun_server_address_.empty()) {
    return;
  }

  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = true;
  NrIceCtx::InitializeGlobals(config);
  EnsurePeer();
  peer_->SetStunServer(stun_server_address_, kDefaultStunServerPort,
                       IceTransport::Tcp);
  peer_->SetDNSResolver();
  peer_->AddStream(1);
  Gather();
  ASSERT_TRUE(StreamHasMatchingCandidate(0, "tcptype passive"));
  ASSERT_FALSE(StreamHasMatchingCandidate(0, "tcptype passive", " 9 "));
  ASSERT_TRUE(StreamHasMatchingCandidate(0, "tcptype so"));
  ASSERT_FALSE(StreamHasMatchingCandidate(0, "tcptype so", " 9 "));
  ASSERT_TRUE(StreamHasMatchingCandidate(0, "tcptype active", " 9 "));
}

TEST_F(WebRtcIceGatherTest, TestGatherDNSStunServerHostname) {
  if (stun_server_hostname_.empty()) {
    return;
  }

  {
    NrIceCtx::GlobalConfig config;
    config.mTcpEnabled = false;
    NrIceCtx::InitializeGlobals(config);
  }

  // A srflx candidate is considered redundant and discarded if its address
  // equals that of a host candidate. (Frequently, a srflx candidate and a host
  // candidate have equal addresses when the agent is not behind a NAT.) So set
  // ICE_POLICY_NO_HOST here to ensure that a srflx candidate is not falsely
  // discarded in this test.
  NrIceCtx::Config config;
  config.mPolicy = NrIceCtx::ICE_POLICY_NO_HOST;
  peer_ = MakeUnique<IceTestPeer>("P1", test_utils_, true, config);

  peer_->SetStunServer(stun_server_hostname_, kDefaultStunServerPort);
  peer_->SetDNSResolver();
  peer_->AddStream(1);
  Gather();
  ASSERT_TRUE(StreamHasMatchingCandidate(0, " UDP "));
  ASSERT_TRUE(StreamHasMatchingCandidate(0, "typ srflx raddr"));
}

// Disabled because google isn't running any TCP stun servers right now
TEST_F(WebRtcIceGatherTest, DISABLED_TestGatherDNSStunServerHostnameTcp) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = true;
  NrIceCtx::InitializeGlobals(config);
  EnsurePeer();
  peer_->SetStunServer(stun_server_hostname_, kDefaultStunServerPort,
                       IceTransport::Tcp);
  peer_->SetDNSResolver();
  peer_->AddStream(1);
  Gather();
  ASSERT_TRUE(StreamHasMatchingCandidate(0, "tcptype passive"));
  ASSERT_FALSE(StreamHasMatchingCandidate(0, "tcptype passive", " 9 "));
  ASSERT_TRUE(StreamHasMatchingCandidate(0, "tcptype so"));
  ASSERT_FALSE(StreamHasMatchingCandidate(0, "tcptype so", " 9 "));
  ASSERT_TRUE(StreamHasMatchingCandidate(0, "tcptype active", " 9 "));
}

// Disabled because google isn't running any TCP stun servers right now
TEST_F(WebRtcIceGatherTest,
       DISABLED_TestGatherDNSStunServerHostnameBothUdpTcp) {
  if (stun_server_hostname_.empty()) {
    return;
  }

  nsTArray<ParsedIceServer> stun_servers;

  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = true;
  NrIceCtx::InitializeGlobals(config);
  EnsurePeer();
  stun_servers.AppendElement(MakeStunEntry(
      stun_server_hostname_, kDefaultStunServerPort, IceTransport::Udp));
  stun_servers.AppendElement(MakeStunEntry(
      stun_server_hostname_, kDefaultStunServerPort, IceTransport::Tcp));
  peer_->SetIceServers(stun_servers);
  peer_->SetDNSResolver();
  peer_->AddStream(1);
  Gather();
  ASSERT_TRUE(StreamHasMatchingCandidate(0, " UDP "));
  ASSERT_TRUE(StreamHasMatchingCandidate(0, " TCP "));
}

// Disabled because google isn't running any TCP stun servers right now
TEST_F(WebRtcIceGatherTest,
       DISABLED_TestGatherDNSStunServerIpAddressBothUdpTcp) {
  if (stun_server_address_.empty()) {
    return;
  }

  nsTArray<ParsedIceServer> stun_servers;

  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = true;
  NrIceCtx::InitializeGlobals(config);
  EnsurePeer();
  stun_servers.AppendElement(MakeStunEntry(
      stun_server_address_, kDefaultStunServerPort, IceTransport::Udp));
  stun_servers.AppendElement(MakeStunEntry(
      stun_server_address_, kDefaultStunServerPort, IceTransport::Tcp));
  peer_->SetIceServers(stun_servers);
  peer_->SetDNSResolver();
  peer_->AddStream(1);
  Gather();
  ASSERT_TRUE(StreamHasMatchingCandidate(0, " UDP "));
  ASSERT_TRUE(StreamHasMatchingCandidate(0, " TCP "));
}

TEST_F(WebRtcIceGatherTest, TestGatherDNSStunBogusHostname) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  EnsurePeer();
  peer_->SetStunServer(kBogusStunServerHostname, kDefaultStunServerPort);
  peer_->SetDNSResolver();
  peer_->AddStream(1);
  Gather();
  ASSERT_TRUE(StreamHasMatchingCandidate(0, " UDP "));
}

// Disabled because google isn't running any TCP stun servers right now
TEST_F(WebRtcIceGatherTest, DISABLED_TestGatherDNSStunBogusHostnameTcp) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = true;
  NrIceCtx::InitializeGlobals(config);
  EnsurePeer();
  peer_->SetStunServer(kBogusStunServerHostname, kDefaultStunServerPort,
                       IceTransport::Tcp);
  peer_->SetDNSResolver();
  peer_->AddStream(1);
  Gather();
  ASSERT_TRUE(StreamHasMatchingCandidate(0, " TCP "));
}

TEST_F(WebRtcIceGatherTest, TestDefaultCandidate) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  EnsurePeer();
  peer_->SetStunServer(stun_server_hostname_, kDefaultStunServerPort);
  peer_->AddStream(1);
  Gather();
  NrIceCandidate default_candidate;
  ASSERT_TRUE(NS_SUCCEEDED(peer_->GetDefaultCandidate(0, &default_candidate)));
}

TEST_F(WebRtcIceGatherTest, TestGatherTurn) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  EnsurePeer();
  if (turn_server_.empty()) return;
  peer_->SetTurnServer(turn_server_, kDefaultStunServerPort, turn_user_,
                       turn_password_, IceTransport::Udp);
  peer_->AddStream(1);
  Gather();
}

TEST_F(WebRtcIceGatherTest, TestGatherTurnTcp) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  EnsurePeer();
  if (turn_server_.empty()) return;
  peer_->SetTurnServer(turn_server_, kDefaultStunServerPort, turn_user_,
                       turn_password_, IceTransport::Tcp);
  peer_->AddStream(1);
  Gather();
}

TEST_F(WebRtcIceGatherTest, TestGatherDisableComponent) {
  if (stun_server_hostname_.empty()) {
    return;
  }

  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  EnsurePeer();
  peer_->SetStunServer(stun_server_hostname_, kDefaultStunServerPort);
  peer_->AddStream(1);
  peer_->AddStream(2);
  peer_->DisableComponent(1, 2);
  Gather();
  std::vector<std::string> attributes = peer_->GetAttributes(1);

  for (auto& attribute : attributes) {
    if (attribute.find("candidate:") != std::string::npos) {
      size_t sp1 = attribute.find(' ');
      ASSERT_EQ(0, attribute.compare(sp1 + 1, 1, "1", 1));
    }
  }
}

TEST_F(WebRtcIceGatherTest, TestGatherVerifyNoLoopback) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  EnsurePeer();
  peer_->AddStream(1);
  Gather();
  ASSERT_FALSE(StreamHasMatchingCandidate(0, "127.0.0.1"));
}

TEST_F(WebRtcIceGatherTest, TestGatherAllowLoopback) {
  NrIceCtx::GlobalConfig globalConfig;
  globalConfig.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(globalConfig);

  NrIceCtx::Config config;
  config.mAllowLoopback = true;
  // Set up peer with loopback allowed.
  peer_ = MakeUnique<IceTestPeer>("P1", test_utils_, true, config);
  peer_->AddStream(1);
  Gather();
  ASSERT_TRUE(StreamHasMatchingCandidate(0, "127.0.0.1"));
}

TEST_F(WebRtcIceGatherTest, TestGatherTcpDisabledNoStun) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  EnsurePeer();
  peer_->AddStream(1);
  Gather();
  ASSERT_FALSE(StreamHasMatchingCandidate(0, " TCP "));
  ASSERT_TRUE(StreamHasMatchingCandidate(0, " UDP "));
}

TEST_F(WebRtcIceGatherTest, VerifyTestStunServer) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  UseFakeStunUdpServerWithResponse("192.0.2.133", 3333);
  peer_->AddStream(1);
  Gather();
  ASSERT_TRUE(StreamHasMatchingCandidate(0, " 192.0.2.133 3333 "));
}

TEST_F(WebRtcIceGatherTest, VerifyTestStunTcpServer) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = true;
  NrIceCtx::InitializeGlobals(config);
  UseFakeStunTcpServerWithResponse("192.0.2.233", 3333);
  peer_->AddStream(1);
  Gather();
  ASSERT_TRUE(StreamHasMatchingCandidate(0, " 192.0.2.233 3333 typ srflx",
                                         " tcptype "));
}

TEST_F(WebRtcIceGatherTest, VerifyTestStunServerV6) {
  if (!TestStunServer::GetInstance(AF_INET6)) {
    // No V6 addresses
    return;
  }
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  UseFakeStunUdpServerWithResponse("beef::", 3333);
  peer_->AddStream(1);
  Gather();
  ASSERT_TRUE(StreamHasMatchingCandidate(0, " beef:: 3333 "));
}

TEST_F(WebRtcIceGatherTest, VerifyTestStunServerFQDN) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  UseFakeStunUdpServerWithResponse("192.0.2.133", 3333, "stun.example.com");
  peer_->AddStream(1);
  Gather();
  ASSERT_TRUE(StreamHasMatchingCandidate(0, " 192.0.2.133 3333 "));
}

TEST_F(WebRtcIceGatherTest, VerifyTestStunServerV6FQDN) {
  if (!TestStunServer::GetInstance(AF_INET6)) {
    // No V6 addresses
    return;
  }
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  UseFakeStunUdpServerWithResponse("beef::", 3333, "stun.example.com");
  peer_->AddStream(1);
  Gather();
  ASSERT_TRUE(StreamHasMatchingCandidate(0, " beef:: 3333 "));
}

TEST_F(WebRtcIceGatherTest, TestStunServerReturnsWildcardAddr) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  UseFakeStunUdpServerWithResponse("0.0.0.0", 3333);
  peer_->AddStream(1);
  Gather(kDefaultTimeout * 3);
  ASSERT_FALSE(StreamHasMatchingCandidate(0, " 0.0.0.0 "));
}

TEST_F(WebRtcIceGatherTest, TestStunServerReturnsWildcardAddrV6) {
  if (!TestStunServer::GetInstance(AF_INET6)) {
    // No V6 addresses
    return;
  }
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  UseFakeStunUdpServerWithResponse("::", 3333);
  peer_->AddStream(1);
  Gather(kDefaultTimeout * 3);
  ASSERT_FALSE(StreamHasMatchingCandidate(0, " :: "));
}

TEST_F(WebRtcIceGatherTest, TestStunServerReturnsPort0) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  UseFakeStunUdpServerWithResponse("192.0.2.133", 0);
  peer_->AddStream(1);
  Gather(kDefaultTimeout * 3);
  ASSERT_FALSE(StreamHasMatchingCandidate(0, " 192.0.2.133 0 "));
}

TEST_F(WebRtcIceGatherTest, TestStunServerReturnsLoopbackAddr) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  UseFakeStunUdpServerWithResponse("127.0.0.133", 3333);
  peer_->AddStream(1);
  Gather(kDefaultTimeout * 3);
  ASSERT_FALSE(StreamHasMatchingCandidate(0, " 127.0.0.133 "));
}

TEST_F(WebRtcIceGatherTest, TestStunServerReturnsLoopbackAddrV6) {
  if (!TestStunServer::GetInstance(AF_INET6)) {
    // No V6 addresses
    return;
  }
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  UseFakeStunUdpServerWithResponse("::1", 3333);
  peer_->AddStream(1);
  Gather(kDefaultTimeout * 3);
  ASSERT_FALSE(StreamHasMatchingCandidate(0, " ::1 "));
}

TEST_F(WebRtcIceGatherTest, TestStunServerTrickle) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  UseFakeStunUdpServerWithResponse("192.0.2.1", 3333);
  peer_->AddStream(1);
  TestStunServer::GetInstance(AF_INET)->SetDropInitialPackets(3);
  Gather(0);
  ASSERT_FALSE(StreamHasMatchingCandidate(0, "192.0.2.1"));
  WaitForGather();
  ASSERT_TRUE(StreamHasMatchingCandidate(0, "192.0.2.1"));
}

// Test no host with our fake STUN server and apparently NATted.
TEST_F(WebRtcIceGatherTest, TestFakeStunServerNatedNoHost) {
  {
    NrIceCtx::GlobalConfig config;
    config.mTcpEnabled = false;
    NrIceCtx::InitializeGlobals(config);
  }

  NrIceCtx::Config config;
  config.mPolicy = NrIceCtx::ICE_POLICY_NO_HOST;
  peer_ = MakeUnique<IceTestPeer>("P1", test_utils_, true, config);
  UseFakeStunUdpServerWithResponse("192.0.2.1", 3333);
  peer_->AddStream(1);
  Gather(0);
  WaitForGather();
  DumpAttributes(0);
  ASSERT_FALSE(StreamHasMatchingCandidate(0, "host"));
  ASSERT_TRUE(StreamHasMatchingCandidate(0, "srflx"));
  NrIceCandidate default_candidate;
  nsresult rv = peer_->GetDefaultCandidate(0, &default_candidate);
  if (NS_SUCCEEDED(rv)) {
    ASSERT_NE(NrIceCandidate::ICE_HOST, default_candidate.type);
  }
}

// Test no host with our fake STUN server and apparently non-NATted.
TEST_F(WebRtcIceGatherTest, TestFakeStunServerNoNatNoHost) {
  {
    NrIceCtx::GlobalConfig config;
    config.mTcpEnabled = false;
    NrIceCtx::InitializeGlobals(config);
  }

  NrIceCtx::Config config;
  config.mPolicy = NrIceCtx::ICE_POLICY_NO_HOST;
  peer_ = MakeUnique<IceTestPeer>("P1", test_utils_, true, config);
  UseTestStunServer();
  peer_->AddStream(1);
  Gather(0);
  WaitForGather();
  DumpAttributes(0);
  ASSERT_FALSE(StreamHasMatchingCandidate(0, "host"));
  ASSERT_TRUE(StreamHasMatchingCandidate(0, "srflx"));
}

// Test that srflx candidate is discarded in non-NATted environment if host
// address obfuscation is not enabled.
TEST_F(WebRtcIceGatherTest,
       TestSrflxCandidateDiscardedWithObfuscateHostAddressesNotEnabled) {
  {
    NrIceCtx::GlobalConfig config;
    config.mTcpEnabled = false;
    NrIceCtx::InitializeGlobals(config);
  }

  NrIceCtx::Config config;
  peer_ = MakeUnique<IceTestPeer>("P1", test_utils_, true, config);
  UseTestStunServer();
  peer_->AddStream(1);
  Gather(0, false, false);
  WaitForGather();
  DumpAttributes(0);
  EXPECT_TRUE(StreamHasMatchingCandidate(0, "host"));
  EXPECT_FALSE(StreamHasMatchingCandidate(0, "srflx"));
}

// Test that srflx candidate is generated in non-NATted environment if host
// address obfuscation is enabled.
TEST_F(WebRtcIceGatherTest,
       TestSrflxCandidateGeneratedWithObfuscateHostAddressesEnabled) {
  {
    NrIceCtx::GlobalConfig config;
    config.mTcpEnabled = false;
    NrIceCtx::InitializeGlobals(config);
  }

  NrIceCtx::Config config;
  peer_ = MakeUnique<IceTestPeer>("P1", test_utils_, true, config);
  UseTestStunServer();
  peer_->AddStream(1);
  Gather(0, false, true);
  WaitForGather();
  DumpAttributes(0);
  EXPECT_TRUE(StreamHasMatchingCandidate(0, "host"));
  EXPECT_TRUE(StreamHasMatchingCandidate(0, "srflx"));
}

TEST_F(WebRtcIceGatherTest, TestStunTcpServerTrickle) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = true;
  NrIceCtx::InitializeGlobals(config);
  UseFakeStunTcpServerWithResponse("192.0.3.1", 3333);
  TestStunTcpServer::GetInstance(AF_INET)->SetDelay(500);
  peer_->AddStream(1);
  Gather(0);
  ASSERT_FALSE(StreamHasMatchingCandidate(0, " 192.0.3.1 ", " tcptype "));
  WaitForGather();
  ASSERT_TRUE(StreamHasMatchingCandidate(0, " 192.0.3.1 ", " tcptype "));
}

TEST_F(WebRtcIceGatherTest, TestStunTcpAndUdpServerTrickle) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = true;
  NrIceCtx::InitializeGlobals(config);
  UseFakeStunUdpTcpServersWithResponse("192.0.2.1", 3333, "192.0.3.1", 3333);
  TestStunServer::GetInstance(AF_INET)->SetDropInitialPackets(3);
  TestStunTcpServer::GetInstance(AF_INET)->SetDelay(500);
  peer_->AddStream(1);
  Gather(0);
  ASSERT_FALSE(StreamHasMatchingCandidate(0, "192.0.2.1", "UDP"));
  ASSERT_FALSE(StreamHasMatchingCandidate(0, " 192.0.3.1 ", " tcptype "));
  WaitForGather();
  ASSERT_TRUE(StreamHasMatchingCandidate(0, "192.0.2.1", "UDP"));
  ASSERT_TRUE(StreamHasMatchingCandidate(0, " 192.0.3.1 ", " tcptype "));
}

TEST_F(WebRtcIceGatherTest, TestSetIceControlling) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  EnsurePeer();
  peer_->AddStream(1);
  peer_->SetControlling(NrIceCtx::ICE_CONTROLLING);
  NrIceCtx::Controlling controlling = peer_->GetControlling();
  ASSERT_EQ(NrIceCtx::ICE_CONTROLLING, controlling);
  // SetControlling should only allow setting this once
  peer_->SetControlling(NrIceCtx::ICE_CONTROLLED);
  controlling = peer_->GetControlling();
  ASSERT_EQ(NrIceCtx::ICE_CONTROLLING, controlling);
}

TEST_F(WebRtcIceGatherTest, TestSetIceControlled) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  EnsurePeer();
  peer_->AddStream(1);
  peer_->SetControlling(NrIceCtx::ICE_CONTROLLED);
  NrIceCtx::Controlling controlling = peer_->GetControlling();
  ASSERT_EQ(NrIceCtx::ICE_CONTROLLED, controlling);
  // SetControlling should only allow setting this once
  peer_->SetControlling(NrIceCtx::ICE_CONTROLLING);
  controlling = peer_->GetControlling();
  ASSERT_EQ(NrIceCtx::ICE_CONTROLLED, controlling);
}

TEST_F(WebRtcIceConnectTest, TestGather) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  ASSERT_TRUE(Gather());
}

TEST_F(WebRtcIceConnectTest, TestGatherTcp) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = true;
  NrIceCtx::InitializeGlobals(config);
  Init();
  AddStream(1);
  ASSERT_TRUE(Gather());
}

TEST_F(WebRtcIceConnectTest, TestGatherAutoPrioritize) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  Init();
  AddStream(1);
  ASSERT_TRUE(Gather());
}

TEST_F(WebRtcIceConnectTest, TestConnect) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  ASSERT_TRUE(Gather());
  Connect();
}

TEST_F(WebRtcIceConnectTest, TestConnectRestartIce) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  ASSERT_TRUE(Gather());
  Connect();
  SendReceive(p1_.get(), p2_.get());

  p2_->RestartIce();
  ASSERT_FALSE(p2_->gathering_complete());

  // verify p1 and p2 streams are still connected after restarting ice on p2
  SendReceive(p1_.get(), p2_.get());

  mozilla::UniquePtr<IceTestPeer> p3_;
  p3_ = MakeUnique<IceTestPeer>("P3", test_utils_, true, NrIceCtx::Config());
  InitPeer(p3_.get());
  p3_->AddStream(1);

  ASSERT_TRUE(GatherCallerAndCallee(p2_.get(), p3_.get()));
  std::cout << "-------------------------------------------------" << std::endl;
  ConnectCallerAndCallee(p3_.get(), p2_.get(), TRICKLE_SIMULATE);
  SendReceive(p1_.get(), p2_.get());  // p1 and p2 are still connected
  SendReceive(p3_.get(), p2_.get(), true, true);  // p3 and p2 not yet connected
  p2_->SimulateTrickle(0);
  p3_->SimulateTrickle(0);
  ASSERT_TRUE_WAIT(p3_->is_ready(0), kDefaultTimeout);
  ASSERT_TRUE_WAIT(p2_->is_ready(0), kDefaultTimeout);
  SendReceive(p1_.get(), p2_.get(), false, true);  // p1 and p2 not connected
  SendReceive(p3_.get(), p2_.get());  // p3 and p2 are now connected

  p3_ = nullptr;
}

TEST_F(WebRtcIceConnectTest, TestConnectRestartIceThenAbort) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  ASSERT_TRUE(Gather());
  Connect();
  SendReceive(p1_.get(), p2_.get());

  p2_->RestartIce();
  ASSERT_FALSE(p2_->gathering_complete());

  // verify p1 and p2 streams are still connected after restarting ice on p2
  SendReceive(p1_.get(), p2_.get());

  mozilla::UniquePtr<IceTestPeer> p3_;
  p3_ = MakeUnique<IceTestPeer>("P3", test_utils_, true, NrIceCtx::Config());
  InitPeer(p3_.get());
  p3_->AddStream(1);

  ASSERT_TRUE(GatherCallerAndCallee(p2_.get(), p3_.get()));
  std::cout << "-------------------------------------------------" << std::endl;
  p2_->RollbackIceRestart();
  p2_->Connect(p1_.get(), TRICKLE_NONE);
  SendReceive(p1_.get(), p2_.get());
  p3_ = nullptr;
}

TEST_F(WebRtcIceConnectTest, TestConnectIceRestartRoleConflict) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  ASSERT_TRUE(Gather());
  // Just for fun lets do this with switched rolls
  p1_->SetControlling(NrIceCtx::ICE_CONTROLLED);
  p2_->SetControlling(NrIceCtx::ICE_CONTROLLING);
  Connect();
  SendReceive(p1_.get(), p2_.get());
  // Set rolls should not switch by connecting
  ASSERT_EQ(NrIceCtx::ICE_CONTROLLED, p1_->GetControlling());
  ASSERT_EQ(NrIceCtx::ICE_CONTROLLING, p2_->GetControlling());

  p2_->RestartIce();
  ASSERT_FALSE(p2_->gathering_complete());
  p2_->SetControlling(NrIceCtx::ICE_CONTROLLED);
  ASSERT_EQ(NrIceCtx::ICE_CONTROLLING, p2_->GetControlling())
      << "ICE restart should not allow role to change, unless ice-lite happens";

  mozilla::UniquePtr<IceTestPeer> p3_;
  p3_ = MakeUnique<IceTestPeer>("P3", test_utils_, true, NrIceCtx::Config());
  InitPeer(p3_.get());
  p3_->AddStream(1);
  // Set control role for p3 accordingly (with role conflict)
  p3_->SetControlling(NrIceCtx::ICE_CONTROLLING);
  ASSERT_EQ(NrIceCtx::ICE_CONTROLLING, p3_->GetControlling());

  ASSERT_TRUE(GatherCallerAndCallee(p2_.get(), p3_.get()));
  std::cout << "-------------------------------------------------" << std::endl;
  ConnectCallerAndCallee(p3_.get(), p2_.get());
  auto p2role = p2_->GetControlling();
  ASSERT_NE(p2role, p3_->GetControlling()) << "Conflict should be resolved";
  ASSERT_EQ(NrIceCtx::ICE_CONTROLLED, p1_->GetControlling())
      << "P1 should be unaffected by role conflict";

  // And again we are not allowed to switch roles at this point any more
  p1_->SetControlling(NrIceCtx::ICE_CONTROLLING);
  ASSERT_EQ(NrIceCtx::ICE_CONTROLLED, p1_->GetControlling());
  p3_->SetControlling(p2role);
  ASSERT_NE(p2role, p3_->GetControlling());

  p3_ = nullptr;
}

TEST_F(WebRtcIceConnectTest,
       TestIceRestartWithMultipleInterfacesAndUserStartingScreenSharing) {
  const char* FAKE_WIFI_ADDR = "10.0.0.1";
  const char* FAKE_WIFI_IF_NAME = "wlan9";

  // prepare a fake wifi interface
  nr_local_addr wifi_addr;
  wifi_addr.interface.type = NR_INTERFACE_TYPE_WIFI;
  wifi_addr.interface.estimated_speed = 1000;

  int r = nr_str_port_to_transport_addr(FAKE_WIFI_ADDR, 0, IPPROTO_UDP,
                                        &(wifi_addr.addr));
  ASSERT_EQ(0, r);
  strncpy(wifi_addr.addr.ifname, FAKE_WIFI_IF_NAME, MAXIFNAME);

  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  // setup initial ICE connection between p1_ and p2_
  UseNat();
  AddStream(1);
  SetExpectedTypes(NrIceCandidate::Type::ICE_SERVER_REFLEXIVE,
                   NrIceCandidate::Type::ICE_SERVER_REFLEXIVE);
  ASSERT_TRUE(Gather(kDefaultTimeout, true));
  Connect();

  // verify the connection is working
  SendReceive(p1_.get(), p2_.get());

  // simulate user accepting permissions for screen sharing
  p2_->SetCtxFlags(false);

  // and having an additional non-default interface
  nsTArray<NrIceStunAddr> stunAddr = p2_->GetStunAddrs();
  stunAddr.InsertElementAt(0, NrIceStunAddr(&wifi_addr));
  p2_->SetStunAddrs(stunAddr);

  std::cout << "-------------------------------------------------" << std::endl;

  // now restart ICE
  p2_->RestartIce();
  ASSERT_FALSE(p2_->gathering_complete());

  // verify that we can successfully gather candidates
  p2_->Gather();
  EXPECT_TRUE_WAIT(p2_->gathering_complete(), kDefaultTimeout);
}

TEST_F(WebRtcIceConnectTest, TestConnectTcp) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = true;
  NrIceCtx::InitializeGlobals(config);
  Init();
  AddStream(1);
  ASSERT_TRUE(Gather());
  SetCandidateFilter(IsTcpCandidate);
  SetExpectedTypes(NrIceCandidate::Type::ICE_HOST,
                   NrIceCandidate::Type::ICE_HOST, kNrIceTransportTcp);
  Connect();
}

// TCP SO tests works on localhost only with delay applied:
//  tc qdisc add dev lo root netem delay 10ms
TEST_F(WebRtcIceConnectTest, DISABLED_TestConnectTcpSo) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = true;
  NrIceCtx::InitializeGlobals(config);
  Init();
  AddStream(1);
  ASSERT_TRUE(Gather());
  SetCandidateFilter(IsTcpSoCandidate);
  SetExpectedTypes(NrIceCandidate::Type::ICE_HOST,
                   NrIceCandidate::Type::ICE_HOST, kNrIceTransportTcp);
  Connect();
}

// Disabled because this breaks with hairpinning.
TEST_F(WebRtcIceConnectTest, DISABLED_TestConnectNoHost) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  Init(false, NrIceCtx::Config{.mPolicy = NrIceCtx::ICE_POLICY_NO_HOST});
  AddStream(1);
  ASSERT_TRUE(Gather());
  SetExpectedTypes(NrIceCandidate::Type::ICE_SERVER_REFLEXIVE,
                   NrIceCandidate::Type::ICE_SERVER_REFLEXIVE,
                   kNrIceTransportTcp);
  Connect();
}

TEST_F(WebRtcIceConnectTest, TestLoopbackOnlySortOf) {
  NrIceCtx::GlobalConfig globalConfig;
  globalConfig.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(globalConfig);

  NrIceCtx::Config config;
  config.mAllowLoopback = true;
  Init(false, config);
  AddStream(1);
  SetCandidateFilter(IsLoopbackCandidate);
  ASSERT_TRUE(Gather());
  SetExpectedRemoteCandidateAddr("127.0.0.1");
  Connect();
}

TEST_F(WebRtcIceConnectTest, TestConnectBothControllingP1Wins) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  p1_->SetTiebreaker(1);
  p2_->SetTiebreaker(0);
  ASSERT_TRUE(Gather());
  p1_->SetControlling(NrIceCtx::ICE_CONTROLLING);
  p2_->SetControlling(NrIceCtx::ICE_CONTROLLING);
  Connect();
}

TEST_F(WebRtcIceConnectTest, TestConnectBothControllingP2Wins) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  p1_->SetTiebreaker(0);
  p2_->SetTiebreaker(1);
  ASSERT_TRUE(Gather());
  p1_->SetControlling(NrIceCtx::ICE_CONTROLLING);
  p2_->SetControlling(NrIceCtx::ICE_CONTROLLING);
  Connect();
}

TEST_F(WebRtcIceConnectTest, TestConnectIceLiteOfferer) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  Init(false);
  AddStream(1);
  ASSERT_TRUE(Gather());
  p1_->SimulateIceLite();
  Connect();

  std::vector<NrIceCandidatePair> pairs;
  ASSERT_EQ(NS_OK, p2_->GetCandidatePairs(0, &pairs));
  EXPECT_EQ(1, std::count_if(pairs.begin(), pairs.end(),
                             [](const NrIceCandidatePair& pair) {
                               return pair.nominated;
                             }));
  EXPECT_EQ(1, std::count_if(pairs.begin(), pairs.end(),
                             [](const NrIceCandidatePair& pair) {
                               return pair.selected;
                             }));
}

TEST_F(WebRtcIceConnectTest, TestConnectIceLitePassiveOfferer) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  Init(false);
  AddStream(1);
  ASSERT_TRUE(Gather());
  p1_->SimulateIceLite();
  ConnectIceLite(p1_.get(), p2_.get());
  ASSERT_TRUE_WAIT(p2_->HasNominatedPair(0), kDefaultTimeout);
  p2_->AssertRegularlyNominatedAndSelectedPair(0);
}

TEST_F(WebRtcIceConnectTest, TestConnectIceLiteAnswerer) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);

  Init(false);
  AddStream(1);
  ASSERT_TRUE(Gather());
  p2_->SimulateIceLite();
  ConnectIceLite(p2_.get(), p1_.get());
  ASSERT_TRUE_WAIT(p1_->HasNominatedPair(0), kDefaultTimeout);
  p1_->AssertRegularlyNominatedAndSelectedPair(0);
}

TEST_F(WebRtcIceConnectTest, TestConnectIceLiteServerReflexiveCandidate) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  config.mStunClientMaxTransmits = 3;
  NrIceCtx::InitializeGlobals(config);

  Init();
  p2_->UseNat();
  p2_->SetExpectedTypes(NrIceCandidate::ICE_SERVER_REFLEXIVE,
                        NrIceCandidate::ICE_HOST);

  AddStream(1);
  ASSERT_TRUE(Gather());
  p1_->SimulateIceLite();
  ConnectIceLite(p1_.get(), p2_.get(), TRICKLE_SIMULATE);
  for (auto* peer : {p1_.get(), p2_.get()}) {
    for (auto* candidate : peer->ControlTrickle(0)) {
      if (!IsIpv4Candidate(candidate->Candidate()).empty()) {
        candidate->Schedule(0);
      }
    }
  }
  ASSERT_TRUE_WAIT(p2_->ice_connected(), kDefaultTimeout);
  ASSERT_TRUE_WAIT(p2_->HasNominatedPair(0), kDefaultTimeout);

  std::vector<NrIceCandidatePair> pairs;
  ASSERT_EQ(NS_OK, p2_->GetCandidatePairs(0, &pairs));
  EXPECT_GT(pairs.size(), 1U);
  p2_->DumpAndCheckActiveCandidates();
  p2_->AssertRegularlyNominatedAndSelectedPair(0);
  SendReceive(p2_.get(), p1_.get());
}

TEST_F(WebRtcIceConnectTest, TestConnectIceLitePeerReflexiveCandidate) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  config.mStunClientMaxTransmits = 3;
  NrIceCtx::InitializeGlobals(config);

  Init();
  p2_->UseNat();
  p2_->SetFilteringType(TestNat::PORT_DEPENDENT);
  p2_->SetMappingType(TestNat::PORT_DEPENDENT);
  p2_->SetExpectedTypes(NrIceCandidate::ICE_PEER_REFLEXIVE,
                        NrIceCandidate::ICE_HOST);

  AddStream(1);
  ASSERT_TRUE(Gather());

  const auto attributes = p2_->GetAttributes(0);
  EXPECT_TRUE(std::any_of(
      attributes.begin(), attributes.end(), [](const std::string& attribute) {
        return attribute.find(" typ srflx ") != std::string::npos;
      }));

  p1_->SimulateIceLite();
  ConnectIceLite(p1_.get(), p2_.get());
  ASSERT_TRUE_WAIT(p2_->HasNominatedPair(0), kDefaultTimeout);

  std::vector<NrIceCandidatePair> pairs;
  ASSERT_EQ(NS_OK, p2_->GetCandidatePairs(0, &pairs));
  EXPECT_GT(pairs.size(), 1U);
  p2_->DumpAndCheckActiveCandidates();
  p2_->AssertRegularlyNominatedAndSelectedPair(0);
}

TEST_F(WebRtcIceConnectTest, TestConnectIceLiteTwoComponents) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);

  Init(false);
  AddStream(2);
  ASSERT_TRUE(Gather());
  p1_->SimulateIceLite();
  ConnectIceLite(p1_.get(), p2_.get());
  ASSERT_TRUE_WAIT(p2_->HasNominatedPair(0, 1), kDefaultTimeout);
  ASSERT_TRUE_WAIT(p2_->HasNominatedPair(0, 2), kDefaultTimeout);
  p2_->AssertRegularlyNominatedAndSelectedPair(0, 1);
  p2_->AssertRegularlyNominatedAndSelectedPair(0, 2);
}

TEST_F(WebRtcIceConnectTest, TestTrickleBothControllingP1Wins) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  p1_->SetTiebreaker(1);
  p2_->SetTiebreaker(0);
  ASSERT_TRUE(Gather());
  p1_->SetControlling(NrIceCtx::ICE_CONTROLLING);
  p2_->SetControlling(NrIceCtx::ICE_CONTROLLING);
  ConnectTrickle();
  SimulateTrickle(0);
  WaitForConnected(1000);
  AssertCheckingReached();
}

TEST_F(WebRtcIceConnectTest, TestTrickleBothControllingP2Wins) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  p1_->SetTiebreaker(0);
  p2_->SetTiebreaker(1);
  ASSERT_TRUE(Gather());
  p1_->SetControlling(NrIceCtx::ICE_CONTROLLING);
  p2_->SetControlling(NrIceCtx::ICE_CONTROLLING);
  ConnectTrickle();
  SimulateTrickle(0);
  WaitForConnected(1000);
  AssertCheckingReached();
}

TEST_F(WebRtcIceConnectTest, TestTrickleIceLiteOfferer) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  Init(false);
  AddStream(1);
  ASSERT_TRUE(Gather());
  p1_->SimulateIceLite();
  ConnectTrickle();
  SimulateTrickle(0);
  WaitForConnected(1000);
  AssertCheckingReached();

  std::vector<NrIceCandidatePair> pairs;
  ASSERT_EQ(NS_OK, p2_->GetCandidatePairs(0, &pairs));
  EXPECT_EQ(1, std::count_if(pairs.begin(), pairs.end(),
                             [](const NrIceCandidatePair& pair) {
                               return pair.nominated;
                             }));
  EXPECT_EQ(1, std::count_if(pairs.begin(), pairs.end(),
                             [](const NrIceCandidatePair& pair) {
                               return pair.selected;
                             }));
}

TEST_F(WebRtcIceConnectTest, TestTrickleIceLitePassiveOfferer) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  Init(false);
  AddStream(1);
  ASSERT_TRUE(Gather());
  p1_->SimulateIceLite();
  ConnectIceLite(p1_.get(), p2_.get(), TRICKLE_SIMULATE);
  p1_->SimulateTrickle(0);
  p2_->SimulateTrickle(0);
  ASSERT_TRUE_WAIT(p2_->ice_connected(), kDefaultTimeout);
  EXPECT_FALSE(p1_->ice_reached_checking());
  ASSERT_TRUE_WAIT(p2_->HasNominatedPair(0), kDefaultTimeout);
  p2_->AssertRegularlyNominatedAndSelectedPair(0);
}

TEST_F(WebRtcIceConnectTest, TestGatherFullCone) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  UseNat();
  AddStream(1);
  ASSERT_TRUE(Gather());
}

TEST_F(WebRtcIceConnectTest, TestGatherFullConeAutoPrioritize) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  UseNat();
  Init();
  AddStream(1);
  ASSERT_TRUE(Gather());
}

TEST_F(WebRtcIceConnectTest, TestConnectFullCone) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  UseNat();
  AddStream(1);
  SetExpectedTypes(NrIceCandidate::Type::ICE_SERVER_REFLEXIVE,
                   NrIceCandidate::Type::ICE_SERVER_REFLEXIVE);
  ASSERT_TRUE(Gather());
  Connect();
}

TEST_F(WebRtcIceConnectTest, TestConnectNoNatNoHost) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  Init(false, NrIceCtx::Config{.mPolicy = NrIceCtx::ICE_POLICY_NO_HOST});
  UseTestStunServer();
  // Because we are connecting from our host candidate to the
  // other side's apparent srflx (which is also their host)
  // we see a host/srflx pair.
  SetExpectedTypes(NrIceCandidate::Type::ICE_HOST,
                   NrIceCandidate::Type::ICE_SERVER_REFLEXIVE);
  AddStream(1);
  ASSERT_TRUE(Gather());
  Connect();
}

TEST_F(WebRtcIceConnectTest, TestConnectFullConeNoHost) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  UseNat();
  Init(false, NrIceCtx::Config{.mPolicy = NrIceCtx::ICE_POLICY_NO_HOST});
  UseTestStunServer();
  SetExpectedTypes(NrIceCandidate::Type::ICE_SERVER_REFLEXIVE,
                   NrIceCandidate::Type::ICE_SERVER_REFLEXIVE);
  AddStream(1);
  ASSERT_TRUE(Gather());
  Connect();
}

TEST_F(WebRtcIceConnectTest, TestGatherAddressRestrictedCone) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  UseNat();
  SetFilteringType(TestNat::ADDRESS_DEPENDENT);
  SetMappingType(TestNat::ENDPOINT_INDEPENDENT);
  AddStream(1);
  ASSERT_TRUE(Gather());
}

TEST_F(WebRtcIceConnectTest, TestConnectAddressRestrictedCone) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  UseNat();
  SetFilteringType(TestNat::ADDRESS_DEPENDENT);
  SetMappingType(TestNat::ENDPOINT_INDEPENDENT);
  AddStream(1);
  SetExpectedTypes(NrIceCandidate::Type::ICE_SERVER_REFLEXIVE,
                   NrIceCandidate::Type::ICE_SERVER_REFLEXIVE);
  ASSERT_TRUE(Gather());
  Connect();
}

TEST_F(WebRtcIceConnectTest, TestGatherPortRestrictedCone) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  UseNat();
  SetFilteringType(TestNat::PORT_DEPENDENT);
  SetMappingType(TestNat::ENDPOINT_INDEPENDENT);
  AddStream(1);
  ASSERT_TRUE(Gather());
}

TEST_F(WebRtcIceConnectTest, TestConnectPortRestrictedCone) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  UseNat();
  SetFilteringType(TestNat::PORT_DEPENDENT);
  SetMappingType(TestNat::ENDPOINT_INDEPENDENT);
  AddStream(1);
  SetExpectedTypes(NrIceCandidate::Type::ICE_SERVER_REFLEXIVE,
                   NrIceCandidate::Type::ICE_SERVER_REFLEXIVE);
  ASSERT_TRUE(Gather());
  Connect();
}

TEST_F(WebRtcIceConnectTest, TestGatherSymmetricNat) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  UseNat();
  SetFilteringType(TestNat::PORT_DEPENDENT);
  SetMappingType(TestNat::PORT_DEPENDENT);
  AddStream(1);
  ASSERT_TRUE(Gather());
}

TEST_F(WebRtcIceConnectTest, TestConnectSymmetricNat) {
  if (turn_server_.empty()) return;

  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  UseNat();
  SetFilteringType(TestNat::PORT_DEPENDENT);
  SetMappingType(TestNat::PORT_DEPENDENT);
  p1_->SetExpectedTypes(NrIceCandidate::Type::ICE_RELAYED,
                        NrIceCandidate::Type::ICE_RELAYED);
  p2_->SetExpectedTypes(NrIceCandidate::Type::ICE_RELAYED,
                        NrIceCandidate::Type::ICE_RELAYED);
  SetTurnServer(turn_server_, kDefaultStunServerPort, turn_user_,
                turn_password_);
  AddStream(1);
  ASSERT_TRUE(Gather());
  Connect();
}

TEST_F(WebRtcIceConnectTest, TestConnectSymmetricNatAndNoNat) {
  {
    NrIceCtx::GlobalConfig config;
    config.mTcpEnabled = true;
    NrIceCtx::InitializeGlobals(config);
  }

  NrIceCtx::Config config;
  p1_ = MakeUnique<IceTestPeer>("P1", test_utils_, true, config);
  p1_->UseNat();
  p1_->SetFilteringType(TestNat::PORT_DEPENDENT);
  p1_->SetMappingType(TestNat::PORT_DEPENDENT);

  p2_ = MakeUnique<IceTestPeer>("P2", test_utils_, false, config);
  initted_ = true;

  AddStream(1);
  p1_->SetExpectedTypes(NrIceCandidate::Type::ICE_PEER_REFLEXIVE,
                        NrIceCandidate::Type::ICE_HOST);
  p2_->SetExpectedTypes(NrIceCandidate::Type::ICE_HOST,
                        NrIceCandidate::Type::ICE_PEER_REFLEXIVE);
  ASSERT_TRUE(Gather());
  Connect();
}

TEST_F(WebRtcIceConnectTest, TestGatherNatBlocksUDP) {
  if (turn_server_.empty()) return;

  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  UseNat();
  BlockUdp();
  nsTArray<ParsedIceServer> turn_servers;
  turn_servers.AppendElement(MakeTurnEntry(turn_server_, kDefaultStunServerPort,
                                           turn_user_, turn_password_,
                                           IceTransport::Tcp));
  turn_servers.AppendElement(MakeTurnEntry(turn_server_, kDefaultStunServerPort,
                                           turn_user_, turn_password_,
                                           IceTransport::Udp));
  SetTurnServers(turn_servers);
  AddStream(1);
  // We have to wait for the UDP-based stuff to time out.
  ASSERT_TRUE(Gather(kDefaultTimeout * 3));
}

TEST_F(WebRtcIceConnectTest, TestConnectNatBlocksUDP) {
  if (turn_server_.empty()) return;

  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  UseNat();
  BlockUdp();
  nsTArray<ParsedIceServer> turn_servers;
  turn_servers.AppendElement(MakeTurnEntry(turn_server_, kDefaultStunServerPort,
                                           turn_user_, turn_password_,
                                           IceTransport::Tcp));
  turn_servers.AppendElement(MakeTurnEntry(turn_server_, kDefaultStunServerPort,
                                           turn_user_, turn_password_,
                                           IceTransport::Udp));
  SetTurnServers(turn_servers);
  p1_->SetExpectedTypes(NrIceCandidate::Type::ICE_RELAYED,
                        NrIceCandidate::Type::ICE_RELAYED, kNrIceTransportTcp);
  p2_->SetExpectedTypes(NrIceCandidate::Type::ICE_RELAYED,
                        NrIceCandidate::Type::ICE_RELAYED, kNrIceTransportTcp);
  AddStream(1);
  ASSERT_TRUE(Gather(kDefaultTimeout * 3));
  Connect();
}

TEST_F(WebRtcIceConnectTest, TestConnectTwoComponents) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(2);
  ASSERT_TRUE(Gather());
  Connect();
}

TEST_F(WebRtcIceConnectTest, TestConnectTwoComponentsDisableSecond) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(2);
  ASSERT_TRUE(Gather());
  p1_->DisableComponent(0, 2);
  p2_->DisableComponent(0, 2);
  Connect();
}

TEST_F(WebRtcIceConnectTest, TestConnectP2ThenP1) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  ASSERT_TRUE(Gather());
  ConnectP2();
  PR_Sleep(1000);
  ConnectP1();
  WaitForConnectedStreams();
}

TEST_F(WebRtcIceConnectTest, TestConnectP2ThenP1Trickle) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  ASSERT_TRUE(Gather());
  ConnectP2();
  PR_Sleep(1000);
  ConnectP1(TRICKLE_SIMULATE);
  SimulateTrickleP1(0);
  WaitForConnectedStreams();
}

TEST_F(WebRtcIceConnectTest, TestConnectP2ThenP1TrickleTwoComponents) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  AddStream(2);
  ASSERT_TRUE(Gather());
  ConnectP2();
  PR_Sleep(1000);
  ConnectP1(TRICKLE_SIMULATE);
  SimulateTrickleP1(0);
  std::cerr << "Sleeping between trickle streams" << std::endl;
  PR_Sleep(1000);  // Give this some time to settle but not complete
                   // all of ICE.
  SimulateTrickleP1(1);
  WaitForConnectedStreams(2);
}

TEST_F(WebRtcIceConnectTest, TestConnectAutoPrioritize) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  Init();
  AddStream(1);
  ASSERT_TRUE(Gather());
  Connect();
}

TEST_F(WebRtcIceConnectTest, TestConnectTrickleOneStreamOneComponent) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  ASSERT_TRUE(Gather());
  ConnectTrickle();
  SimulateTrickle(0);
  WaitForConnected(1000);
  AssertCheckingReached();
}

TEST_F(WebRtcIceConnectTest, TestConnectTrickleTwoStreamsOneComponent) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  AddStream(1);
  ASSERT_TRUE(Gather());
  ConnectTrickle();
  SimulateTrickle(0);
  SimulateTrickle(1);
  WaitForConnected(1000);
  AssertCheckingReached();
}

void RealisticTrickleDelay(
    std::vector<SchedulableTrickleCandidate*>& candidates) {
  for (size_t i = 0; i < candidates.size(); ++i) {
    SchedulableTrickleCandidate* cand = candidates[i];
    if (cand->IsHost()) {
      cand->Schedule(i * 10);
    } else if (cand->IsReflexive()) {
      cand->Schedule(i * 10 + 100);
    } else if (cand->IsRelay()) {
      cand->Schedule(i * 10 + 200);
    }
  }
}

void DelayRelayCandidates(std::vector<SchedulableTrickleCandidate*>& candidates,
                          unsigned int ms) {
  for (auto& candidate : candidates) {
    if (candidate->IsRelay()) {
      candidate->Schedule(ms);
    } else {
      candidate->Schedule(0);
    }
  }
}

void AddNonPairableCandidates(
    std::vector<SchedulableTrickleCandidate*>& candidates, IceTestPeer* peer,
    size_t stream, int net_type, MtransportTestUtils* test_utils_) {
  for (int i = 1; i < 5; i++) {
    if (net_type == i) continue;
    switch (i) {
      case 1:
        candidates.push_back(new SchedulableTrickleCandidate(
            peer, stream,
            "candidate:0 1 UDP 2113601790 10.0.0.1 12345 typ host", "",
            test_utils_));
        break;
      case 2:
        candidates.push_back(new SchedulableTrickleCandidate(
            peer, stream,
            "candidate:0 1 UDP 2113601791 172.16.1.1 12345 typ host", "",
            test_utils_));
        break;
      case 3:
        candidates.push_back(new SchedulableTrickleCandidate(
            peer, stream,
            "candidate:0 1 UDP 2113601792 192.168.0.1 12345 typ host", "",
            test_utils_));
        break;
      case 4:
        candidates.push_back(new SchedulableTrickleCandidate(
            peer, stream,
            "candidate:0 1 UDP 2113601793 100.64.1.1 12345 typ host", "",
            test_utils_));
        break;
      default:
        NR_UNIMPLEMENTED;
    }
  }

  for (auto i = candidates.rbegin(); i != candidates.rend(); ++i) {
    std::cerr << "Scheduling candidate: " << (*i)->Candidate().c_str()
              << std::endl;
    (*i)->Schedule(0);
  }
}

void DropTrickleCandidates(
    std::vector<SchedulableTrickleCandidate*>& candidates) {}

TEST_F(WebRtcIceConnectTest, TestConnectTrickleAddStreamDuringICE) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  ASSERT_TRUE(Gather());
  ConnectTrickle();
  RealisticTrickleDelay(p1_->ControlTrickle(0));
  RealisticTrickleDelay(p2_->ControlTrickle(0));
  AddStream(1);
  ASSERT_TRUE(Gather());
  ConnectTrickle();
  RealisticTrickleDelay(p1_->ControlTrickle(1));
  RealisticTrickleDelay(p2_->ControlTrickle(1));
  WaitForConnected(1000);
  AssertCheckingReached();
}

TEST_F(WebRtcIceConnectTest, TestConnectTrickleAddStreamAfterICE) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  ASSERT_TRUE(Gather());
  ConnectTrickle();
  RealisticTrickleDelay(p1_->ControlTrickle(0));
  RealisticTrickleDelay(p2_->ControlTrickle(0));
  WaitForConnected(1000);
  AddStream(1);
  ASSERT_TRUE(Gather());
  ConnectTrickle();
  RealisticTrickleDelay(p1_->ControlTrickle(1));
  RealisticTrickleDelay(p2_->ControlTrickle(1));
  WaitForConnected(1000);
  AssertCheckingReached();
}

TEST_F(WebRtcIceConnectTest, RemoveStream) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  AddStream(1);
  ASSERT_TRUE(Gather());
  ConnectTrickle();
  RealisticTrickleDelay(p1_->ControlTrickle(0));
  RealisticTrickleDelay(p2_->ControlTrickle(0));
  RealisticTrickleDelay(p1_->ControlTrickle(1));
  RealisticTrickleDelay(p2_->ControlTrickle(1));
  WaitForConnected(1000);

  RemoveStream(0);
  ASSERT_TRUE(Gather());
  ConnectTrickle();
}

TEST_F(WebRtcIceConnectTest, P1NoTrickle) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  ASSERT_TRUE(Gather());
  ConnectTrickle();
  DropTrickleCandidates(p1_->ControlTrickle(0));
  RealisticTrickleDelay(p2_->ControlTrickle(0));
  WaitForConnected(1000);
}

TEST_F(WebRtcIceConnectTest, P2NoTrickle) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  ASSERT_TRUE(Gather());
  ConnectTrickle();
  RealisticTrickleDelay(p1_->ControlTrickle(0));
  DropTrickleCandidates(p2_->ControlTrickle(0));
  WaitForConnected(1000);
}

TEST_F(WebRtcIceConnectTest, RemoveAndAddStream) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  AddStream(1);
  ASSERT_TRUE(Gather());
  ConnectTrickle();
  RealisticTrickleDelay(p1_->ControlTrickle(0));
  RealisticTrickleDelay(p2_->ControlTrickle(0));
  RealisticTrickleDelay(p1_->ControlTrickle(1));
  RealisticTrickleDelay(p2_->ControlTrickle(1));
  WaitForConnected(1000);

  RemoveStream(0);
  AddStream(1);
  ASSERT_TRUE(Gather());
  ConnectTrickle();
  RealisticTrickleDelay(p1_->ControlTrickle(2));
  RealisticTrickleDelay(p2_->ControlTrickle(2));
  WaitForConnected(1000);
}

TEST_F(WebRtcIceConnectTest, RemoveStreamBeforeGather) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  AddStream(1);
  ASSERT_TRUE(Gather(0));
  RemoveStream(0);
  WaitForGather();
  ConnectTrickle();
  RealisticTrickleDelay(p1_->ControlTrickle(1));
  RealisticTrickleDelay(p2_->ControlTrickle(1));
  WaitForConnected(1000);
}

TEST_F(WebRtcIceConnectTest, RemoveStreamDuringGather) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  AddStream(1);
  RemoveStream(0);
  ASSERT_TRUE(Gather());
  ConnectTrickle();
  RealisticTrickleDelay(p1_->ControlTrickle(1));
  RealisticTrickleDelay(p2_->ControlTrickle(1));
  WaitForConnected(1000);
}

TEST_F(WebRtcIceConnectTest, RemoveStreamDuringConnect) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  AddStream(1);
  ASSERT_TRUE(Gather());
  ConnectTrickle();
  RealisticTrickleDelay(p1_->ControlTrickle(0));
  RealisticTrickleDelay(p2_->ControlTrickle(0));
  RealisticTrickleDelay(p1_->ControlTrickle(1));
  RealisticTrickleDelay(p2_->ControlTrickle(1));
  RemoveStream(0);
  WaitForConnected(1000);
}

TEST_F(WebRtcIceConnectTest, TestConnectRealTrickleOneStreamOneComponent) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  AddStream(1);
  ASSERT_TRUE(Gather(0));
  ConnectTrickle(TRICKLE_REAL);
  WaitForConnected();
  WaitForGather();  // ICE can complete before we finish gathering.
  AssertCheckingReached();
}

TEST_F(WebRtcIceConnectTest, TestSendReceive) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  ASSERT_TRUE(Gather());
  Connect();
  SendReceive();
}

TEST_F(WebRtcIceConnectTest, TestSendReceiveTcp) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = true;
  NrIceCtx::InitializeGlobals(config);
  Init();
  AddStream(1);
  ASSERT_TRUE(Gather());
  SetCandidateFilter(IsTcpCandidate);
  SetExpectedTypes(NrIceCandidate::Type::ICE_HOST,
                   NrIceCandidate::Type::ICE_HOST, kNrIceTransportTcp);
  Connect();
  SendReceive();
}

// TCP SO tests works on localhost only with delay applied:
//  tc qdisc add dev lo root netem delay 10ms
TEST_F(WebRtcIceConnectTest, DISABLED_TestSendReceiveTcpSo) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = true;
  NrIceCtx::InitializeGlobals(config);
  Init();
  AddStream(1);
  ASSERT_TRUE(Gather());
  SetCandidateFilter(IsTcpSoCandidate);
  SetExpectedTypes(NrIceCandidate::Type::ICE_HOST,
                   NrIceCandidate::Type::ICE_HOST, kNrIceTransportTcp);
  Connect();
  SendReceive();
}

TEST_F(WebRtcIceConnectTest, TestConsent) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  SetupAndCheckConsent();
  PR_Sleep(1500);
  AssertConsentRefresh();
  SendReceive();
}

TEST_F(WebRtcIceConnectTest, TestConsentTcp) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = true;
  NrIceCtx::InitializeGlobals(config);
  Init();
  AddStream(1);
  SetCandidateFilter(IsTcpCandidate);
  SetExpectedTypes(NrIceCandidate::Type::ICE_HOST,
                   NrIceCandidate::Type::ICE_HOST, kNrIceTransportTcp);
  SetupAndCheckConsent();
  PR_Sleep(1500);
  AssertConsentRefresh();
  SendReceive();
}

TEST_F(WebRtcIceConnectTest, TestConsentIntermittent) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  SetupAndCheckConsent();
  p1_->SetBlockStun(true);
  p2_->SetBlockStun(true);
  WaitForDisconnected();
  AssertConsentRefresh(CONSENT_STALE);
  SendReceive();
  p1_->SetBlockStun(false);
  p2_->SetBlockStun(false);
  WaitForConnected();
  AssertConsentRefresh();
  SendReceive();
  p1_->SetBlockStun(true);
  p2_->SetBlockStun(true);
  WaitForDisconnected();
  AssertConsentRefresh(CONSENT_STALE);
  SendReceive();
  p1_->SetBlockStun(false);
  p2_->SetBlockStun(false);
  WaitForConnected();
  AssertConsentRefresh();
}

TEST_F(WebRtcIceConnectTest, TestConsentTimeout) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  SetupAndCheckConsent();
  p1_->SetBlockStun(true);
  p2_->SetBlockStun(true);
  WaitForDisconnected();
  AssertConsentRefresh(CONSENT_STALE);
  SendReceive();
  WaitForFailed();
  AssertConsentRefresh(CONSENT_EXPIRED);
  SendFailure();
}

TEST_F(WebRtcIceConnectTest, TestConsentDelayed) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  SetupAndCheckConsent();
  /* Note: We don't have a list of STUN transaction IDs of the previously timed
           out consent requests. Thus responses after sending the next consent
           request are ignored. */
  p1_->SetStunResponseDelay(200);
  p2_->SetStunResponseDelay(200);
  PR_Sleep(1000);
  AssertConsentRefresh();
  SendReceive();
}

TEST_F(WebRtcIceConnectTest, TestNetworkForcedOfflineAndRecovery) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  SetupAndCheckConsent();
  p1_->ChangeNetworkStateToOffline();
  ASSERT_TRUE_WAIT(p1_->ice_connected() == 0, kDefaultTimeout);
  // Next round of consent check should switch it back to online
  ASSERT_TRUE_WAIT(p1_->ice_connected(), kDefaultTimeout);
}

TEST_F(WebRtcIceConnectTest, TestNetworkForcedOfflineTwice) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  SetupAndCheckConsent();
  p2_->ChangeNetworkStateToOffline();
  ASSERT_TRUE_WAIT(p2_->ice_connected() == 0, kDefaultTimeout);
  p2_->ChangeNetworkStateToOffline();
  ASSERT_TRUE_WAIT(p2_->ice_connected() == 0, kDefaultTimeout);
}

TEST_F(WebRtcIceConnectTest, TestNetworkOnlineDoesntChangeState) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  SetupAndCheckConsent();
  p2_->ChangeNetworkStateToOnline();
  ASSERT_TRUE(p2_->ice_connected());
  PR_Sleep(1500);
  p2_->ChangeNetworkStateToOnline();
  ASSERT_TRUE(p2_->ice_connected());
}

TEST_F(WebRtcIceConnectTest, TestNetworkOnlineTriggersConsent) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  // Let's emulate audio + video w/o rtcp-mux
  AddStream(2);
  AddStream(2);
  SetupAndCheckConsent();
  p1_->ChangeNetworkStateToOffline();
  p1_->SetBlockStun(true);
  ASSERT_TRUE_WAIT(p1_->ice_connected() == 0, kDefaultTimeout);
  PR_Sleep(1500);
  ASSERT_TRUE(p1_->ice_connected() == 0);
  p1_->SetBlockStun(false);
  p1_->ChangeNetworkStateToOnline();
  ASSERT_TRUE_WAIT(p1_->ice_connected(), 500);
}

TEST_F(WebRtcIceConnectTest, TestConnectTurn) {
  if (turn_server_.empty()) return;

  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  SetTurnServer(turn_server_, kDefaultStunServerPort, turn_user_,
                turn_password_);
  AddStream(1);
  ASSERT_TRUE(Gather());
  Connect();
}

TEST_F(WebRtcIceConnectTest, TestConnectTurnWithDelay) {
  if (turn_server_.empty()) return;

  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  SetTurnServer(turn_server_, kDefaultStunServerPort, turn_user_,
                turn_password_);
  SetCandidateFilter(SabotageHostCandidateAndDropReflexive);
  AddStream(1);
  p1_->Gather();
  PR_Sleep(500);
  p2_->Gather();
  ConnectTrickle(TRICKLE_REAL);
  WaitForGather();
  WaitForConnectedStreams();
}

TEST_F(WebRtcIceConnectTest, TestConnectTurnWithNormalTrickleDelay) {
  if (turn_server_.empty()) return;

  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  SetTurnServer(turn_server_, kDefaultStunServerPort, turn_user_,
                turn_password_);
  AddStream(1);
  ASSERT_TRUE(Gather());
  ConnectTrickle();
  RealisticTrickleDelay(p1_->ControlTrickle(0));
  RealisticTrickleDelay(p2_->ControlTrickle(0));

  WaitForConnected();
  AssertCheckingReached();
}

TEST_F(WebRtcIceConnectTest, TestConnectTurnWithNormalTrickleDelayOneSided) {
  if (turn_server_.empty()) return;

  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  SetTurnServer(turn_server_, kDefaultStunServerPort, turn_user_,
                turn_password_);
  AddStream(1);
  ASSERT_TRUE(Gather());
  ConnectTrickle();
  RealisticTrickleDelay(p1_->ControlTrickle(0));
  p2_->SimulateTrickle(0);

  WaitForConnected();
  AssertCheckingReached();
}

TEST_F(WebRtcIceConnectTest, TestConnectTurnWithLargeTrickleDelay) {
  if (turn_server_.empty()) return;

  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  SetTurnServer(turn_server_, kDefaultStunServerPort, turn_user_,
                turn_password_);
  SetCandidateFilter(SabotageHostCandidateAndDropReflexive);
  AddStream(1);
  ASSERT_TRUE(Gather());
  ConnectTrickle();
  // Trickle host candidates immediately, but delay relay candidates
  DelayRelayCandidates(p1_->ControlTrickle(0), 3700);
  DelayRelayCandidates(p2_->ControlTrickle(0), 3700);

  WaitForConnected();
  AssertCheckingReached();
}

TEST_F(WebRtcIceConnectTest, TestConnectTurnTcp) {
  if (turn_server_.empty()) return;

  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  SetTurnServer(turn_server_, kDefaultStunServerPort, turn_user_,
                turn_password_, IceTransport::Tcp);
  AddStream(1);
  ASSERT_TRUE(Gather());
  Connect();
}

TEST_F(WebRtcIceConnectTest, TestConnectTurnOnly) {
  if (turn_server_.empty()) return;

  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  SetTurnServer(turn_server_, kDefaultStunServerPort, turn_user_,
                turn_password_);
  AddStream(1);
  ASSERT_TRUE(Gather());
  SetCandidateFilter(IsRelayCandidate);
  SetExpectedTypes(NrIceCandidate::Type::ICE_RELAYED,
                   NrIceCandidate::Type::ICE_RELAYED);
  Connect();
}

TEST_F(WebRtcIceConnectTest, TestConnectTurnTcpOnly) {
  if (turn_server_.empty()) return;

  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  SetTurnServer(turn_server_, kDefaultStunServerPort, turn_user_,
                turn_password_, IceTransport::Tcp);
  AddStream(1);
  ASSERT_TRUE(Gather());
  SetCandidateFilter(IsRelayCandidate);
  SetExpectedTypes(NrIceCandidate::Type::ICE_RELAYED,
                   NrIceCandidate::Type::ICE_RELAYED, kNrIceTransportTcp);
  Connect();
}

TEST_F(WebRtcIceConnectTest, TestSendReceiveTurnOnly) {
  if (turn_server_.empty()) return;

  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  SetTurnServer(turn_server_, kDefaultStunServerPort, turn_user_,
                turn_password_);
  AddStream(1);
  ASSERT_TRUE(Gather());
  SetCandidateFilter(IsRelayCandidate);
  SetExpectedTypes(NrIceCandidate::Type::ICE_RELAYED,
                   NrIceCandidate::Type::ICE_RELAYED);
  Connect();
  SendReceive();
}

TEST_F(WebRtcIceConnectTest, TestSendReceiveTurnTcpOnly) {
  if (turn_server_.empty()) return;

  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  SetTurnServer(turn_server_, kDefaultStunServerPort, turn_user_,
                turn_password_, IceTransport::Tcp);
  AddStream(1);
  ASSERT_TRUE(Gather());
  SetCandidateFilter(IsRelayCandidate);
  SetExpectedTypes(NrIceCandidate::Type::ICE_RELAYED,
                   NrIceCandidate::Type::ICE_RELAYED, kNrIceTransportTcp);
  Connect();
  SendReceive();
}

TEST_F(WebRtcIceConnectTest, TestSendReceiveTurnBothOnly) {
  if (turn_server_.empty()) return;

  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  nsTArray<ParsedIceServer> turn_servers;
  turn_servers.AppendElement(MakeTurnEntry(turn_server_, kDefaultStunServerPort,
                                           turn_user_, turn_password_,
                                           IceTransport::Tcp));
  turn_servers.AppendElement(MakeTurnEntry(turn_server_, kDefaultStunServerPort,
                                           turn_user_, turn_password_,
                                           IceTransport::Udp));
  SetTurnServers(turn_servers);
  AddStream(1);
  ASSERT_TRUE(Gather());
  SetCandidateFilter(IsRelayCandidate);
  // UDP is preferred.
  SetExpectedTypes(NrIceCandidate::Type::ICE_RELAYED,
                   NrIceCandidate::Type::ICE_RELAYED, kNrIceTransportUdp);
  Connect();
  SendReceive();
}

TEST_F(WebRtcIceConnectTest, TestConnectShutdownOneSide) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  ASSERT_TRUE(Gather());
  ConnectThenDelete();
}

TEST_F(WebRtcIceConnectTest, TestPollCandPairsBeforeConnect) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  ASSERT_TRUE(Gather());

  std::vector<NrIceCandidatePair> pairs;
  nsresult res = p1_->GetCandidatePairs(0, &pairs);
  // There should be no candidate pairs prior to calling Connect()
  ASSERT_EQ(NS_OK, res);
  ASSERT_EQ(0U, pairs.size());

  res = p2_->GetCandidatePairs(0, &pairs);
  ASSERT_EQ(NS_OK, res);
  ASSERT_EQ(0U, pairs.size());
}

TEST_F(WebRtcIceConnectTest, TestPollCandPairsAfterConnect) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  ASSERT_TRUE(Gather());
  Connect();

  std::vector<NrIceCandidatePair> pairs;
  nsresult r = p1_->GetCandidatePairs(0, &pairs);
  ASSERT_EQ(NS_OK, r);
  // How detailed of a check do we want to do here? If the turn server is
  // functioning, we'll get at least two pairs, but this is probably not
  // something we should assume.
  ASSERT_NE(0U, pairs.size());
  ASSERT_TRUE(p1_->CandidatePairsPriorityDescending(pairs));
  ASSERT_TRUE(ContainsSucceededPair(pairs));
  pairs.clear();

  r = p2_->GetCandidatePairs(0, &pairs);
  ASSERT_EQ(NS_OK, r);
  ASSERT_NE(0U, pairs.size());
  ASSERT_TRUE(p2_->CandidatePairsPriorityDescending(pairs));
  ASSERT_TRUE(ContainsSucceededPair(pairs));
}

// TODO Bug 1259842 - disabled until we find a better way to handle two
// candidates from different RFC1918 ranges
TEST_F(WebRtcIceConnectTest, DISABLED_TestHostCandPairingFilter) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  Init(false);
  AddStream(1);
  ASSERT_TRUE(Gather());
  SetCandidateFilter(IsIpv4Candidate);

  int host_net = p1_->GetCandidatesPrivateIpv4Range(0);
  if (host_net <= 0) {
    // TODO bug 1226838: make this work with multiple private IPs
    FAIL() << "This test needs exactly one private IPv4 host candidate to work"
           << std::endl;
  }

  ConnectTrickle();
  AddNonPairableCandidates(p1_->ControlTrickle(0), p1_.get(), 0, host_net,
                           test_utils_);
  AddNonPairableCandidates(p2_->ControlTrickle(0), p2_.get(), 0, host_net,
                           test_utils_);

  std::vector<NrIceCandidatePair> pairs;
  p1_->GetCandidatePairs(0, &pairs);
  for (auto p : pairs) {
    std::cerr << "Verifying pair:" << std::endl;
    p1_->DumpCandidatePair(p);
    nr_transport_addr addr;
    nr_str_port_to_transport_addr(p.local.local_addr.host.c_str(), 0,
                                  IPPROTO_UDP, &addr);
    ASSERT_TRUE(nr_transport_addr_get_private_addr_range(&addr) == host_net);
    nr_str_port_to_transport_addr(p.remote.cand_addr.host.c_str(), 0,
                                  IPPROTO_UDP, &addr);
    ASSERT_TRUE(nr_transport_addr_get_private_addr_range(&addr) == host_net);
  }
}

// TODO Bug 1226838 - See Comment 2 - this test can't work as written
TEST_F(WebRtcIceConnectTest, DISABLED_TestSrflxCandPairingFilter) {
  if (stun_server_address_.empty()) {
    return;
  }

  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  Init(false);
  AddStream(1);
  ASSERT_TRUE(Gather());
  SetCandidateFilter(IsSrflxCandidate);

  if (p1_->GetCandidatesPrivateIpv4Range(0) <= 0) {
    // TODO bug 1226838: make this work with public IP addresses
    std::cerr << "Don't run this test at IETF meetings!" << std::endl;
    FAIL() << "This test needs one private IPv4 host candidate to work"
           << std::endl;
  }

  ConnectTrickle();
  SimulateTrickleP1(0);
  SimulateTrickleP2(0);

  std::vector<NrIceCandidatePair> pairs;
  p1_->GetCandidatePairs(0, &pairs);
  for (auto p : pairs) {
    std::cerr << "Verifying P1 pair:" << std::endl;
    p1_->DumpCandidatePair(p);
    nr_transport_addr addr;
    nr_str_port_to_transport_addr(p.local.local_addr.host.c_str(), 0,
                                  IPPROTO_UDP, &addr);
    ASSERT_TRUE(nr_transport_addr_get_private_addr_range(&addr) != 0);
    nr_str_port_to_transport_addr(p.remote.cand_addr.host.c_str(), 0,
                                  IPPROTO_UDP, &addr);
    ASSERT_TRUE(nr_transport_addr_get_private_addr_range(&addr) == 0);
  }
  p2_->GetCandidatePairs(0, &pairs);
  for (auto p : pairs) {
    std::cerr << "Verifying P2 pair:" << std::endl;
    p2_->DumpCandidatePair(p);
    nr_transport_addr addr;
    nr_str_port_to_transport_addr(p.local.local_addr.host.c_str(), 0,
                                  IPPROTO_UDP, &addr);
    ASSERT_TRUE(nr_transport_addr_get_private_addr_range(&addr) != 0);
    nr_str_port_to_transport_addr(p.remote.cand_addr.host.c_str(), 0,
                                  IPPROTO_UDP, &addr);
    ASSERT_TRUE(nr_transport_addr_get_private_addr_range(&addr) == 0);
  }
}

TEST_F(WebRtcIceConnectTest, TestPollCandPairsDuringConnect) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  ASSERT_TRUE(Gather());

  p2_->Connect(p1_.get(), TRICKLE_NONE, false);
  p1_->Connect(p2_.get(), TRICKLE_NONE, false);

  std::vector<NrIceCandidatePair> pairs1;
  std::vector<NrIceCandidatePair> pairs2;

  p1_->StartChecks();
  p1_->UpdateAndValidateCandidatePairs(0, &pairs1);
  p2_->UpdateAndValidateCandidatePairs(0, &pairs2);

  p2_->StartChecks();
  p1_->UpdateAndValidateCandidatePairs(0, &pairs1);
  p2_->UpdateAndValidateCandidatePairs(0, &pairs2);

  WaitForConnectedStreams();
  p1_->UpdateAndValidateCandidatePairs(0, &pairs1);
  p2_->UpdateAndValidateCandidatePairs(0, &pairs2);
  ASSERT_TRUE(ContainsSucceededPair(pairs1));
  ASSERT_TRUE(ContainsSucceededPair(pairs2));
}

TEST_F(WebRtcIceConnectTest, TestRLogConnector) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  ASSERT_TRUE(Gather());

  p2_->Connect(p1_.get(), TRICKLE_NONE, false);
  p1_->Connect(p2_.get(), TRICKLE_NONE, false);

  std::vector<NrIceCandidatePair> pairs1;
  std::vector<NrIceCandidatePair> pairs2;

  p1_->StartChecks();
  p1_->UpdateAndValidateCandidatePairs(0, &pairs1);
  p2_->UpdateAndValidateCandidatePairs(0, &pairs2);

  p2_->StartChecks();
  p1_->UpdateAndValidateCandidatePairs(0, &pairs1);
  p2_->UpdateAndValidateCandidatePairs(0, &pairs2);

  WaitForConnectedStreams();
  p1_->UpdateAndValidateCandidatePairs(0, &pairs1);
  p2_->UpdateAndValidateCandidatePairs(0, &pairs2);
  ASSERT_TRUE(ContainsSucceededPair(pairs1));
  ASSERT_TRUE(ContainsSucceededPair(pairs2));

  for (auto& p : pairs1) {
    std::deque<std::string> logs;
    std::string substring("CAND-PAIR(");
    substring += p.codeword;
    RLogConnector::GetInstance()->Filter(substring, 0, &logs);
    ASSERT_NE(0U, logs.size());
  }

  for (auto& p : pairs2) {
    std::deque<std::string> logs;
    std::string substring("CAND-PAIR(");
    substring += p.codeword;
    RLogConnector::GetInstance()->Filter(substring, 0, &logs);
    ASSERT_NE(0U, logs.size());
  }
}

// Verify that a bogus candidate doesn't cause crashes on the
// main thread. See bug 856433.
TEST_F(WebRtcIceConnectTest, TestBogusCandidate) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  Gather();
  ConnectTrickle();
  p1_->ParseCandidate(0, kBogusIceCandidate, "");

  std::vector<NrIceCandidatePair> pairs;
  nsresult res = p1_->GetCandidatePairs(0, &pairs);
  ASSERT_EQ(NS_OK, res);
  ASSERT_EQ(0U, pairs.size());
}

TEST_F(WebRtcIceConnectTest, TestNonMDNSCandidate) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  Gather();
  ConnectTrickle();
  p1_->ParseCandidate(0, kUnreachableHostIceCandidate, "");

  std::vector<NrIceCandidatePair> pairs;
  nsresult res = p1_->GetCandidatePairs(0, &pairs);
  ASSERT_EQ(NS_OK, res);
  ASSERT_EQ(1U, pairs.size());
  ASSERT_EQ(pairs[0].remote.mdns_addr, "");
}

TEST_F(WebRtcIceConnectTest, TestMDNSCandidate) {
  NrIceCtx::GlobalConfig config;
  config.mTcpEnabled = false;
  NrIceCtx::InitializeGlobals(config);
  AddStream(1);
  Gather();
  ConnectTrickle();
  p1_->ParseCandidate(0, kUnreachableHostIceCandidate, "host.local");

  std::vector<NrIceCandidatePair> pairs;
  nsresult res = p1_->GetCandidatePairs(0, &pairs);
  ASSERT_EQ(NS_OK, res);
  ASSERT_EQ(1U, pairs.size());
  ASSERT_EQ(pairs[0].remote.mdns_addr, "host.local");
}

TEST_F(WebRtcIcePrioritizerTest, TestPrioritizer) {
  SetPriorizer(::mozilla::CreateInterfacePrioritizer());

  AddInterface("0", NR_INTERFACE_TYPE_VPN, 100);  // unknown vpn
  AddInterface("1", NR_INTERFACE_TYPE_VPN | NR_INTERFACE_TYPE_WIRED,
               100);  // wired vpn
  AddInterface("2", NR_INTERFACE_TYPE_VPN | NR_INTERFACE_TYPE_WIFI,
               100);  // wifi vpn
  AddInterface("3", NR_INTERFACE_TYPE_VPN | NR_INTERFACE_TYPE_MOBILE,
               100);                                    // wifi vpn
  AddInterface("4", NR_INTERFACE_TYPE_WIRED, 1000);     // wired, high speed
  AddInterface("5", NR_INTERFACE_TYPE_WIRED, 10);       // wired, low speed
  AddInterface("6", NR_INTERFACE_TYPE_WIFI, 10);        // wifi, low speed
  AddInterface("7", NR_INTERFACE_TYPE_WIFI, 1000);      // wifi, high speed
  AddInterface("8", NR_INTERFACE_TYPE_MOBILE, 10);      // mobile, low speed
  AddInterface("9", NR_INTERFACE_TYPE_MOBILE, 1000);    // mobile, high speed
  AddInterface("10", NR_INTERFACE_TYPE_UNKNOWN, 10);    // unknown, low speed
  AddInterface("11", NR_INTERFACE_TYPE_UNKNOWN, 1000);  // unknown, high speed

  // expected preference "4" > "5" > "1" > "7" > "6" > "2" > "9" > "8" > "3" >
  // "11" > "10" > "0"

  HasLowerPreference("0", "10");
  HasLowerPreference("10", "11");
  HasLowerPreference("11", "3");
  HasLowerPreference("3", "8");
  HasLowerPreference("8", "9");
  HasLowerPreference("9", "2");
  HasLowerPreference("2", "6");
  HasLowerPreference("6", "7");
  HasLowerPreference("7", "1");
  HasLowerPreference("1", "5");
  HasLowerPreference("5", "4");
}

TEST_F(WebRtcIcePacketFilterTest, TestSendNonStunPacket) {
  const unsigned char data[] = "12345abcde";
  TestOutgoing(data, sizeof(data), 123, 45, false);
  TestOutgoingTcp(data, sizeof(data), false);
}

TEST_F(WebRtcIcePacketFilterTest, TestRecvNonStunPacket) {
  const unsigned char data[] = "12345abcde";
  TestIncoming(data, sizeof(data), 123, 45, false);
  TestIncomingTcp(data, sizeof(data), true);
}

TEST_F(WebRtcIcePacketFilterTest, TestSendStunPacket) {
  nr_stun_message* msg;
  ASSERT_EQ(0, nr_stun_build_req_no_auth(nullptr, &msg));
  msg->header.type = NR_STUN_MSG_BINDING_REQUEST;
  ASSERT_EQ(0, nr_stun_encode_message(msg));
  TestOutgoing(msg->buffer, msg->length, 123, 45, true);
  TestOutgoingTcp(msg->buffer, msg->length, true);
  TestOutgoingTcpFramed(msg->buffer, msg->length, true);
  ASSERT_EQ(0, nr_stun_message_destroy(&msg));
}

TEST_F(WebRtcIcePacketFilterTest, TestRecvStunPacketWithoutAPendingId) {
  nr_stun_message* msg;
  ASSERT_EQ(0, nr_stun_build_req_no_auth(nullptr, &msg));

  msg->header.id.octet[0] = 1;
  msg->header.type = NR_STUN_MSG_BINDING_REQUEST;
  ASSERT_EQ(0, nr_stun_encode_message(msg));
  TestOutgoing(msg->buffer, msg->length, 123, 45, true);
  TestOutgoingTcp(msg->buffer, msg->length, true);

  msg->header.id.octet[0] = 0;
  msg->header.type = NR_STUN_MSG_BINDING_RESPONSE;
  ASSERT_EQ(0, nr_stun_encode_message(msg));
  TestIncoming(msg->buffer, msg->length, 123, 45, true);
  TestIncomingTcp(msg->buffer, msg->length, true);

  ASSERT_EQ(0, nr_stun_message_destroy(&msg));
}

TEST_F(WebRtcIcePacketFilterTest, TestRecvStunBindingRequestWithoutAPendingId) {
  nr_stun_message* msg;
  ASSERT_EQ(0, nr_stun_build_req_no_auth(nullptr, &msg));

  msg->header.id.octet[0] = 1;
  msg->header.type = NR_STUN_MSG_BINDING_REQUEST;
  ASSERT_EQ(0, nr_stun_encode_message(msg));
  TestIncoming(msg->buffer, msg->length, 123, 45, true);
  TestIncomingTcp(msg->buffer, msg->length, true);

  msg->header.id.octet[0] = 1;
  msg->header.type = NR_STUN_MSG_BINDING_RESPONSE;
  ASSERT_EQ(0, nr_stun_encode_message(msg));
  TestOutgoing(msg->buffer, msg->length, 123, 45, true);
  TestOutgoingTcp(msg->buffer, msg->length, true);

  ASSERT_EQ(0, nr_stun_message_destroy(&msg));
}

TEST_F(WebRtcIcePacketFilterTest,
       TestRecvStunPacketWithoutAPendingIdTcpFramed) {
  nr_stun_message* msg;
  ASSERT_EQ(0, nr_stun_build_req_no_auth(nullptr, &msg));

  msg->header.id.octet[0] = 1;
  msg->header.type = NR_STUN_MSG_BINDING_REQUEST;
  ASSERT_EQ(0, nr_stun_encode_message(msg));
  TestOutgoingTcpFramed(msg->buffer, msg->length, true);

  msg->header.id.octet[0] = 0;
  msg->header.type = NR_STUN_MSG_BINDING_RESPONSE;
  ASSERT_EQ(0, nr_stun_encode_message(msg));
  TestIncomingTcpFramed(msg->buffer, msg->length, true);

  ASSERT_EQ(0, nr_stun_message_destroy(&msg));
}

TEST_F(WebRtcIcePacketFilterTest, TestRecvStunPacketWithoutAPendingAddress) {
  nr_stun_message* msg;
  ASSERT_EQ(0, nr_stun_build_req_no_auth(nullptr, &msg));

  msg->header.type = NR_STUN_MSG_BINDING_REQUEST;
  ASSERT_EQ(0, nr_stun_encode_message(msg));
  TestOutgoing(msg->buffer, msg->length, 123, 45, true);
  // nothing to test here for the TCP filter

  msg->header.type = NR_STUN_MSG_BINDING_RESPONSE;
  ASSERT_EQ(0, nr_stun_encode_message(msg));
  TestIncoming(msg->buffer, msg->length, 123, 46, false);
  TestIncoming(msg->buffer, msg->length, 124, 45, false);

  ASSERT_EQ(0, nr_stun_message_destroy(&msg));
}

TEST_F(WebRtcIcePacketFilterTest, TestRecvStunPacketWithPendingIdAndAddress) {
  nr_stun_message* msg;
  ASSERT_EQ(0, nr_stun_build_req_no_auth(nullptr, &msg));

  msg->header.type = NR_STUN_MSG_BINDING_REQUEST;
  ASSERT_EQ(0, nr_stun_encode_message(msg));
  TestOutgoing(msg->buffer, msg->length, 123, 45, true);
  TestOutgoingTcp(msg->buffer, msg->length, true);

  msg->header.type = NR_STUN_MSG_BINDING_RESPONSE;
  ASSERT_EQ(0, nr_stun_encode_message(msg));
  TestIncoming(msg->buffer, msg->length, 123, 45, true);
  TestIncomingTcp(msg->buffer, msg->length, true);

  // Test whitelist by filtering non-stun packets.
  const unsigned char data[] = "12345abcde";

  // 123:45 is white-listed.
  TestOutgoing(data, sizeof(data), 123, 45, true);
  TestOutgoingTcp(data, sizeof(data), true);
  TestIncoming(data, sizeof(data), 123, 45, true);
  TestIncomingTcp(data, sizeof(data), true);

  // Indications pass as well.
  msg->header.type = NR_STUN_MSG_BINDING_INDICATION;
  ASSERT_EQ(0, nr_stun_encode_message(msg));
  TestOutgoing(msg->buffer, msg->length, 123, 45, true);
  TestOutgoingTcp(msg->buffer, msg->length, true);
  TestIncoming(msg->buffer, msg->length, 123, 45, true);
  TestIncomingTcp(msg->buffer, msg->length, true);

  // Packets from and to other address are still disallowed.
  // Note: this doesn't apply for TCP connections
  TestOutgoing(data, sizeof(data), 123, 46, false);
  TestIncoming(data, sizeof(data), 123, 46, false);
  TestOutgoing(data, sizeof(data), 124, 45, false);
  TestIncoming(data, sizeof(data), 124, 45, false);

  ASSERT_EQ(0, nr_stun_message_destroy(&msg));
}

TEST_F(WebRtcIcePacketFilterTest, TestRecvStunPacketWithPendingIdTcpFramed) {
  nr_stun_message* msg;
  ASSERT_EQ(0, nr_stun_build_req_no_auth(nullptr, &msg));

  msg->header.type = NR_STUN_MSG_BINDING_REQUEST;
  ASSERT_EQ(0, nr_stun_encode_message(msg));
  TestOutgoingTcpFramed(msg->buffer, msg->length, true);

  msg->header.type = NR_STUN_MSG_BINDING_RESPONSE;
  ASSERT_EQ(0, nr_stun_encode_message(msg));
  TestIncomingTcpFramed(msg->buffer, msg->length, true);

  // Test whitelist by filtering non-stun packets.
  const unsigned char data[] = "12345abcde";

  TestOutgoingTcpFramed(data, sizeof(data), true);
  TestIncomingTcpFramed(data, sizeof(data), true);

  ASSERT_EQ(0, nr_stun_message_destroy(&msg));
}

TEST_F(WebRtcIcePacketFilterTest, TestSendNonRequestStunPacket) {
  nr_stun_message* msg;
  ASSERT_EQ(0, nr_stun_build_req_no_auth(nullptr, &msg));

  msg->header.type = NR_STUN_MSG_BINDING_RESPONSE;
  ASSERT_EQ(0, nr_stun_encode_message(msg));
  TestOutgoing(msg->buffer, msg->length, 123, 45, false);
  TestOutgoingTcp(msg->buffer, msg->length, false);

  // Send a packet so we allow the incoming request.
  msg->header.type = NR_STUN_MSG_BINDING_REQUEST;
  ASSERT_EQ(0, nr_stun_encode_message(msg));
  TestOutgoing(msg->buffer, msg->length, 123, 45, true);
  TestOutgoingTcp(msg->buffer, msg->length, true);

  // This packet makes us able to send a response.
  msg->header.type = NR_STUN_MSG_BINDING_REQUEST;
  ASSERT_EQ(0, nr_stun_encode_message(msg));
  TestIncoming(msg->buffer, msg->length, 123, 45, true);
  TestIncomingTcp(msg->buffer, msg->length, true);

  msg->header.type = NR_STUN_MSG_BINDING_RESPONSE;
  ASSERT_EQ(0, nr_stun_encode_message(msg));
  TestOutgoing(msg->buffer, msg->length, 123, 45, true);
  TestOutgoingTcp(msg->buffer, msg->length, true);

  ASSERT_EQ(0, nr_stun_message_destroy(&msg));
}

TEST_F(WebRtcIcePacketFilterTest, TestRecvDataPacketWithAPendingAddress) {
  nr_stun_message* msg;
  ASSERT_EQ(0, nr_stun_build_req_no_auth(nullptr, &msg));

  msg->header.type = NR_STUN_MSG_BINDING_REQUEST;
  ASSERT_EQ(0, nr_stun_encode_message(msg));
  TestOutgoing(msg->buffer, msg->length, 123, 45, true);
  TestOutgoingTcp(msg->buffer, msg->length, true);

  const unsigned char data[] = "12345abcde";
  TestIncoming(data, sizeof(data), 123, 45, true);
  TestIncomingTcp(data, sizeof(data), true);

  ASSERT_EQ(0, nr_stun_message_destroy(&msg));
}

TEST_F(WebRtcIcePacketFilterTest, ZeroLengthIncoming) {
  nr_stun_message* msg;
  ASSERT_EQ(0, nr_stun_build_req_no_auth(nullptr, &msg));

  msg->header.type = NR_STUN_MSG_BINDING_REQUEST;
  ASSERT_EQ(0, nr_stun_encode_message(msg));
  TestOutgoing(msg->buffer, msg->length, 123, 45, true);
  TestOutgoingTcp(msg->buffer, msg->length, true);

  ASSERT_EQ(0, nr_stun_message_destroy(&msg));

  const unsigned char data[] = "";
  TestIncoming(data, sizeof(data), 123, 45, true);
  TestIncomingTcp(data, sizeof(data), true);
}

TEST(WebRtcIceInternalsTest, TestAddBogusAttribute)
{
  nr_stun_message* req;
  ASSERT_EQ(0, nr_stun_message_create(&req));
  Data* data;
  ASSERT_EQ(0, r_data_alloc(&data, 3000));
  memset(data->data, 'A', data->len);
  ASSERT_TRUE(nr_stun_message_add_message_integrity_attribute(req, data));
  ASSERT_EQ(0, r_data_destroy(&data));
  ASSERT_EQ(0, nr_stun_message_destroy(&req));
}
