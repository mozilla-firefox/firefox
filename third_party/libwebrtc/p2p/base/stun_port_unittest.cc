/*
 *  Copyright 2009 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "p2p/base/stun_port.h"

#include <memory>

#include "api/test/mock_async_dns_resolver.h"
#include "p2p/base/basic_packet_socket_factory.h"
#include "p2p/base/mock_dns_resolving_packet_socket_factory.h"
#include "p2p/base/test_stun_server.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/gunit.h"
#include "rtc_base/helpers.h"
#include "rtc_base/network/received_packet.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/ssl_adapter.h"
#include "rtc_base/virtual_socket_server.h"
#include "test/gmock.h"
#include "test/scoped_key_value_config.h"

namespace {

using cricket::ServerAddresses;
using rtc::SocketAddress;
using ::testing::_;
using ::testing::DoAll;
using ::testing::Return;
using ::testing::ReturnPointee;
using ::testing::SetArgPointee;
using webrtc::IceCandidateType;

static const SocketAddress kLocalAddr("127.0.0.1", 0);
static const SocketAddress kIPv6LocalAddr("::1", 0);
static const SocketAddress kStunAddr1("127.0.0.1", 5000);
static const SocketAddress kStunAddr2("127.0.0.1", 4000);
static const SocketAddress kStunAddr3("127.0.0.1", 3000);
static const SocketAddress kIPv6StunAddr1("::1", 5000);
static const SocketAddress kBadAddr("0.0.0.1", 5000);
static const SocketAddress kValidHostnameAddr("valid-hostname", 5000);
static const SocketAddress kBadHostnameAddr("not-a-real-hostname", 5000);
// STUN timeout (with all retries) is cricket::STUN_TOTAL_TIMEOUT.
// Add some margin of error for slow bots.
static const int kTimeoutMs = cricket::STUN_TOTAL_TIMEOUT;
// stun prio = 100 (srflx) << 24 | 30 (IPv4) << 8 | 256 - 1 (component)
static const uint32_t kStunCandidatePriority =
    (100 << 24) | (30 << 8) | (256 - 1);
// stun prio = 100 (srflx) << 24 | 60 (loopback IPv6) << 8 | 256 - 1 (component)
static const uint32_t kIPv6StunCandidatePriority =
    (100 << 24) | (60 << 8) | (256 - 1);
static const int kInfiniteLifetime = -1;
static const int kHighCostPortKeepaliveLifetimeMs = 2 * 60 * 1000;

constexpr uint64_t kTiebreakerDefault = 44444;

class FakeMdnsResponder : public webrtc::MdnsResponderInterface {
 public:
  void CreateNameForAddress(const rtc::IPAddress& addr,
                            NameCreatedCallback callback) override {
    callback(addr, std::string("unittest-mdns-host-name.local"));
  }

  void RemoveNameForAddress(const rtc::IPAddress& addr,
                            NameRemovedCallback callback) override {}
};

class FakeMdnsResponderProvider : public rtc::MdnsResponderProvider {
 public:
  FakeMdnsResponderProvider() : mdns_responder_(new FakeMdnsResponder()) {}

  webrtc::MdnsResponderInterface* GetMdnsResponder() const override {
    return mdns_responder_.get();
  }

 private:
  std::unique_ptr<webrtc::MdnsResponderInterface> mdns_responder_;
};

// Base class for tests connecting a StunPort to a fake STUN server
// (cricket::StunServer).
class StunPortTestBase : public ::testing::Test, public sigslot::has_slots<> {
 public:
  StunPortTestBase()
      : StunPortTestBase(
            rtc::Network("unittest", "unittest", kLocalAddr.ipaddr(), 32),
            kLocalAddr.ipaddr()) {}

  StunPortTestBase(rtc::Network network, const rtc::IPAddress address)
      : ss_(new rtc::VirtualSocketServer()),
        thread_(ss_.get()),
        network_(network),
        socket_factory_(ss_.get()),
        stun_server_1_(
            cricket::TestStunServer::Create(ss_.get(), kStunAddr1, thread_)),
        stun_server_2_(
            cricket::TestStunServer::Create(ss_.get(), kStunAddr2, thread_)),
        mdns_responder_provider_(new FakeMdnsResponderProvider()),
        done_(false),
        error_(false),
        stun_keepalive_delay_(1),
        stun_keepalive_lifetime_(-1) {
    network_.AddIP(address);
  }

  virtual rtc::PacketSocketFactory* socket_factory() {
    return &socket_factory_;
  }

  rtc::VirtualSocketServer* ss() const { return ss_.get(); }
  cricket::UDPPort* port() const { return stun_port_.get(); }
  rtc::AsyncPacketSocket* socket() const { return socket_.get(); }
  bool done() const { return done_; }
  bool error() const { return error_; }

  bool HasPendingRequest(int msg_type) {
    return stun_port_->request_manager().HasRequestForTest(msg_type);
  }

  void SetNetworkType(rtc::AdapterType adapter_type) {
    network_.set_type(adapter_type);
  }

  void CreateStunPort(const rtc::SocketAddress& server_addr,
                      const webrtc::FieldTrialsView* field_trials = nullptr) {
    ServerAddresses stun_servers;
    stun_servers.insert(server_addr);
    CreateStunPort(stun_servers, field_trials);
  }

  void CreateStunPort(const ServerAddresses& stun_servers,
                      const webrtc::FieldTrialsView* field_trials = nullptr) {
    stun_port_ = cricket::StunPort::Create(
        rtc::Thread::Current(), socket_factory(), &network_, 0, 0,
        rtc::CreateRandomString(16), rtc::CreateRandomString(22), stun_servers,
        absl::nullopt, field_trials);
    stun_port_->SetIceTiebreaker(kTiebreakerDefault);
    stun_port_->set_stun_keepalive_delay(stun_keepalive_delay_);
    // If `stun_keepalive_lifetime_` is negative, let the stun port
    // choose its lifetime from the network type.
    if (stun_keepalive_lifetime_ >= 0) {
      stun_port_->set_stun_keepalive_lifetime(stun_keepalive_lifetime_);
    }
    stun_port_->SignalPortComplete.connect(this,
                                           &StunPortTestBase::OnPortComplete);
    stun_port_->SignalPortError.connect(this, &StunPortTestBase::OnPortError);
    stun_port_->SignalCandidateError.connect(
        this, &StunPortTestBase::OnCandidateError);
  }

  void CreateSharedUdpPort(
      const rtc::SocketAddress& server_addr,
      rtc::AsyncPacketSocket* socket,
      const webrtc::FieldTrialsView* field_trials = nullptr) {
    if (socket) {
      socket_.reset(socket);
    } else {
      socket_.reset(socket_factory()->CreateUdpSocket(
          rtc::SocketAddress(kLocalAddr.ipaddr(), 0), 0, 0));
    }
    ASSERT_TRUE(socket_ != NULL);
    socket_->RegisterReceivedPacketCallback(
        [&](rtc::AsyncPacketSocket* socket, const rtc::ReceivedPacket& packet) {
          OnReadPacket(socket, packet);
        });
    stun_port_ = cricket::UDPPort::Create(
        rtc::Thread::Current(), socket_factory(), &network_, socket_.get(),
        rtc::CreateRandomString(16), rtc::CreateRandomString(22), false,
        absl::nullopt, field_trials);
    ASSERT_TRUE(stun_port_ != NULL);
    stun_port_->SetIceTiebreaker(kTiebreakerDefault);
    ServerAddresses stun_servers;
    stun_servers.insert(server_addr);
    stun_port_->set_server_addresses(stun_servers);
    stun_port_->SignalPortComplete.connect(this,
                                           &StunPortTestBase::OnPortComplete);
    stun_port_->SignalPortError.connect(this, &StunPortTestBase::OnPortError);
  }

  void PrepareAddress() { stun_port_->PrepareAddress(); }

  void OnReadPacket(rtc::AsyncPacketSocket* socket,
                    const rtc::ReceivedPacket& packet) {
    stun_port_->HandleIncomingPacket(socket, packet);
  }

  void SendData(const char* data, size_t len) {
    stun_port_->HandleIncomingPacket(socket_.get(),
                                     rtc::ReceivedPacket::CreateFromLegacy(
                                         data, len, /* packet_time_us */ -1,
                                         rtc::SocketAddress("22.22.22.22", 0)));
  }

  void EnableMdnsObfuscation() {
    network_.set_mdns_responder_provider(mdns_responder_provider_.get());
  }

 protected:
  static void SetUpTestSuite() {
    // Ensure the RNG is inited.
    rtc::InitRandom(NULL, 0);
  }

  void OnPortComplete(cricket::Port* port) {
    ASSERT_FALSE(done_);
    done_ = true;
    error_ = false;
  }
  void OnPortError(cricket::Port* port) {
    done_ = true;
    error_ = true;
  }
  void OnCandidateError(cricket::Port* port,
                        const cricket::IceCandidateErrorEvent& event) {
    error_event_ = event;
  }
  void SetKeepaliveDelay(int delay) { stun_keepalive_delay_ = delay; }

  void SetKeepaliveLifetime(int lifetime) {
    stun_keepalive_lifetime_ = lifetime;
  }

  cricket::TestStunServer* stun_server_1() { return stun_server_1_.get(); }
  cricket::TestStunServer* stun_server_2() { return stun_server_2_.get(); }

  rtc::AutoSocketServerThread& thread() { return thread_; }

 private:
  std::unique_ptr<rtc::VirtualSocketServer> ss_;
  rtc::AutoSocketServerThread thread_;
  rtc::Network network_;
  rtc::BasicPacketSocketFactory socket_factory_;
  std::unique_ptr<cricket::UDPPort> stun_port_;
  cricket::TestStunServer::StunServerPtr stun_server_1_;
  cricket::TestStunServer::StunServerPtr stun_server_2_;
  std::unique_ptr<rtc::AsyncPacketSocket> socket_;
  std::unique_ptr<rtc::MdnsResponderProvider> mdns_responder_provider_;
  bool done_;
  bool error_;
  int stun_keepalive_delay_;
  int stun_keepalive_lifetime_;

 protected:
  cricket::IceCandidateErrorEvent error_event_;
};

class StunPortTestWithRealClock : public StunPortTestBase {};

class FakeClockBase {
 public:
  rtc::ScopedFakeClock fake_clock;
};

class StunPortTest : public FakeClockBase, public StunPortTestBase {};

// Test that we can create a STUN port.
TEST_F(StunPortTest, TestCreateStunPort) {
  CreateStunPort(kStunAddr1);
  EXPECT_EQ(IceCandidateType::kSrflx, port()->Type());
  EXPECT_EQ(0U, port()->Candidates().size());
}

// Test that we can create a UDP port.
TEST_F(StunPortTest, TestCreateUdpPort) {
  CreateSharedUdpPort(kStunAddr1, nullptr);
  EXPECT_EQ(IceCandidateType::kHost, port()->Type());
  EXPECT_EQ(0U, port()->Candidates().size());
}

// Test that we can get an address from a STUN server.
TEST_F(StunPortTest, TestPrepareAddress) {
  CreateStunPort(kStunAddr1);
  PrepareAddress();
  EXPECT_TRUE_SIMULATED_WAIT(done(), kTimeoutMs, fake_clock);
  ASSERT_EQ(1U, port()->Candidates().size());
  EXPECT_TRUE(kLocalAddr.EqualIPs(port()->Candidates()[0].address()));
  std::string expected_server_url = "stun:127.0.0.1:5000";
  EXPECT_EQ(port()->Candidates()[0].url(), expected_server_url);
}

// Test that we fail properly if we can't get an address.
TEST_F(StunPortTest, TestPrepareAddressFail) {
  CreateStunPort(kBadAddr);
  PrepareAddress();
  EXPECT_TRUE_SIMULATED_WAIT(done(), kTimeoutMs, fake_clock);
  EXPECT_TRUE(error());
  EXPECT_EQ(0U, port()->Candidates().size());
  EXPECT_EQ_SIMULATED_WAIT(error_event_.error_code,
                           cricket::SERVER_NOT_REACHABLE_ERROR, kTimeoutMs,
                           fake_clock);
  ASSERT_NE(error_event_.error_text.find('.'), std::string::npos);
  ASSERT_NE(error_event_.address.find(kLocalAddr.HostAsSensitiveURIString()),
            std::string::npos);
  std::string server_url = "stun:" + kBadAddr.ToString();
  ASSERT_EQ(error_event_.url, server_url);
}

class StunPortWithMockDnsResolverTest : public StunPortTest {
 public:
  StunPortWithMockDnsResolverTest() : StunPortTest(), socket_factory_(ss()) {}

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

// Test that we can get an address from a STUN server specified by a hostname.
TEST_F(StunPortWithMockDnsResolverTest, TestPrepareAddressHostname) {
  SetDnsResolverExpectations(
      [](webrtc::MockAsyncDnsResolver* resolver,
         webrtc::MockAsyncDnsResolverResult* resolver_result) {
        EXPECT_CALL(*resolver, Start(kValidHostnameAddr, /*family=*/AF_INET, _))
            .WillOnce([](const rtc::SocketAddress& addr, int family,
                         absl::AnyInvocable<void()> callback) { callback(); });

        EXPECT_CALL(*resolver, result)
            .WillRepeatedly(ReturnPointee(resolver_result));
        EXPECT_CALL(*resolver_result, GetError).WillOnce(Return(0));
        EXPECT_CALL(*resolver_result, GetResolvedAddress(AF_INET, _))
            .WillOnce(DoAll(SetArgPointee<1>(SocketAddress("127.0.0.1", 5000)),
                            Return(true)));
      });
  CreateStunPort(kValidHostnameAddr);
  PrepareAddress();
  EXPECT_TRUE_SIMULATED_WAIT(done(), kTimeoutMs, fake_clock);
  ASSERT_EQ(1U, port()->Candidates().size());
  EXPECT_TRUE(kLocalAddr.EqualIPs(port()->Candidates()[0].address()));
  EXPECT_EQ(kStunCandidatePriority, port()->Candidates()[0].priority());
}

TEST_F(StunPortWithMockDnsResolverTest,
       TestPrepareAddressHostnameWithPriorityAdjustment) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-IncreaseIceCandidatePriorityHostSrflx/Enabled/");
  SetDnsResolverExpectations(
      [](webrtc::MockAsyncDnsResolver* resolver,
         webrtc::MockAsyncDnsResolverResult* resolver_result) {
        EXPECT_CALL(*resolver, Start(kValidHostnameAddr, /*family=*/AF_INET, _))
            .WillOnce([](const rtc::SocketAddress& addr, int family,
                         absl::AnyInvocable<void()> callback) { callback(); });
        EXPECT_CALL(*resolver, result)
            .WillRepeatedly(ReturnPointee(resolver_result));
        EXPECT_CALL(*resolver_result, GetError).WillOnce(Return(0));
        EXPECT_CALL(*resolver_result, GetResolvedAddress(AF_INET, _))
            .WillOnce(DoAll(SetArgPointee<1>(SocketAddress("127.0.0.1", 5000)),
                            Return(true)));
      });
  CreateStunPort(kValidHostnameAddr);
  PrepareAddress();
  EXPECT_TRUE_SIMULATED_WAIT(done(), kTimeoutMs, fake_clock);
  ASSERT_EQ(1U, port()->Candidates().size());
  EXPECT_TRUE(kLocalAddr.EqualIPs(port()->Candidates()[0].address()));
  EXPECT_EQ(kStunCandidatePriority + (cricket::kMaxTurnServers << 8),
            port()->Candidates()[0].priority());
}

// Test that we handle hostname lookup failures properly.
TEST_F(StunPortTestWithRealClock, TestPrepareAddressHostnameFail) {
  CreateStunPort(kBadHostnameAddr);
  PrepareAddress();
  EXPECT_TRUE_WAIT(done(), kTimeoutMs);
  EXPECT_TRUE(error());
  EXPECT_EQ(0U, port()->Candidates().size());
  EXPECT_EQ_WAIT(error_event_.error_code, cricket::SERVER_NOT_REACHABLE_ERROR,
                 kTimeoutMs);
}

// This test verifies keepalive response messages don't result in
// additional candidate generation.
TEST_F(StunPortTest, TestKeepAliveResponse) {
  SetKeepaliveDelay(500);  // 500ms of keepalive delay.
  CreateStunPort(kStunAddr1);
  PrepareAddress();
  EXPECT_TRUE_SIMULATED_WAIT(done(), kTimeoutMs, fake_clock);
  ASSERT_EQ(1U, port()->Candidates().size());
  EXPECT_TRUE(kLocalAddr.EqualIPs(port()->Candidates()[0].address()));
  SIMULATED_WAIT(false, 1000, fake_clock);
  EXPECT_EQ(1U, port()->Candidates().size());
}

// Test that a local candidate can be generated using a shared socket.
TEST_F(StunPortTest, TestSharedSocketPrepareAddress) {
  CreateSharedUdpPort(kStunAddr1, nullptr);
  PrepareAddress();
  EXPECT_TRUE_SIMULATED_WAIT(done(), kTimeoutMs, fake_clock);
  ASSERT_EQ(1U, port()->Candidates().size());
  EXPECT_TRUE(kLocalAddr.EqualIPs(port()->Candidates()[0].address()));
}

// Test that we still get a local candidate with invalid stun server hostname.
// Also verifing that UDPPort can receive packets when stun address can't be
// resolved.
TEST_F(StunPortTestWithRealClock,
       TestSharedSocketPrepareAddressInvalidHostname) {
  CreateSharedUdpPort(kBadHostnameAddr, nullptr);
  PrepareAddress();
  EXPECT_TRUE_WAIT(done(), kTimeoutMs);
  ASSERT_EQ(1U, port()->Candidates().size());
  EXPECT_TRUE(kLocalAddr.EqualIPs(port()->Candidates()[0].address()));

  // Send data to port after it's ready. This is to make sure, UDP port can
  // handle data with unresolved stun server address.
  std::string data = "some random data, sending to cricket::Port.";
  SendData(data.c_str(), data.length());
  // No crash is success.
}

// Test that a stun candidate (srflx candidate) is discarded whose address is
// equal to that of a local candidate if mDNS obfuscation is not enabled.
TEST_F(StunPortTest, TestStunCandidateDiscardedWithMdnsObfuscationNotEnabled) {
  CreateSharedUdpPort(kStunAddr1, nullptr);
  PrepareAddress();
  EXPECT_TRUE_SIMULATED_WAIT(done(), kTimeoutMs, fake_clock);
  ASSERT_EQ(1U, port()->Candidates().size());
  EXPECT_TRUE(kLocalAddr.EqualIPs(port()->Candidates()[0].address()));
  EXPECT_TRUE(port()->Candidates()[0].is_local());
}

// Test that a stun candidate (srflx candidate) is generated whose address is
// equal to that of a local candidate if mDNS obfuscation is enabled.
TEST_F(StunPortTest, TestStunCandidateGeneratedWithMdnsObfuscationEnabled) {
  EnableMdnsObfuscation();
  CreateSharedUdpPort(kStunAddr1, nullptr);
  PrepareAddress();
  EXPECT_TRUE_SIMULATED_WAIT(done(), kTimeoutMs, fake_clock);
  ASSERT_EQ(2U, port()->Candidates().size());

  // The addresses of the candidates are both equal to kLocalAddr.
  EXPECT_TRUE(kLocalAddr.EqualIPs(port()->Candidates()[0].address()));
  EXPECT_TRUE(kLocalAddr.EqualIPs(port()->Candidates()[1].address()));

  // One of the generated candidates is a local candidate and the other is a
  // stun candidate.
  EXPECT_NE(port()->Candidates()[0].type(), port()->Candidates()[1].type());
  if (port()->Candidates()[0].is_local()) {
    EXPECT_TRUE(port()->Candidates()[1].is_stun());
  } else {
    EXPECT_TRUE(port()->Candidates()[0].is_stun());
    EXPECT_TRUE(port()->Candidates()[1].is_local());
  }
}

// Test that the same address is added only once if two STUN servers are in
// use.
TEST_F(StunPortTest, TestNoDuplicatedAddressWithTwoStunServers) {
  ServerAddresses stun_servers;
  stun_servers.insert(kStunAddr1);
  stun_servers.insert(kStunAddr2);
  CreateStunPort(stun_servers);
  EXPECT_EQ(IceCandidateType::kSrflx, port()->Type());
  PrepareAddress();
  EXPECT_TRUE_SIMULATED_WAIT(done(), kTimeoutMs, fake_clock);
  EXPECT_EQ(1U, port()->Candidates().size());
  EXPECT_EQ(port()->Candidates()[0].relay_protocol(), "");
}

// Test that candidates can be allocated for multiple STUN servers, one of
// which is not reachable.
TEST_F(StunPortTest, TestMultipleStunServersWithBadServer) {
  ServerAddresses stun_servers;
  stun_servers.insert(kStunAddr1);
  stun_servers.insert(kBadAddr);
  CreateStunPort(stun_servers);
  EXPECT_EQ(IceCandidateType::kSrflx, port()->Type());
  PrepareAddress();
  EXPECT_TRUE_SIMULATED_WAIT(done(), kTimeoutMs, fake_clock);
  EXPECT_EQ(1U, port()->Candidates().size());
  std::string server_url = "stun:" + kBadAddr.ToString();
  ASSERT_EQ_SIMULATED_WAIT(error_event_.url, server_url, kTimeoutMs,
                           fake_clock);
}

// Test that two candidates are allocated if the two STUN servers return
// different mapped addresses.
TEST_F(StunPortTest, TestTwoCandidatesWithTwoStunServersAcrossNat) {
  const SocketAddress kStunMappedAddr1("77.77.77.77", 0);
  const SocketAddress kStunMappedAddr2("88.77.77.77", 0);
  stun_server_1()->set_fake_stun_addr(kStunMappedAddr1);
  stun_server_2()->set_fake_stun_addr(kStunMappedAddr2);

  ServerAddresses stun_servers;
  stun_servers.insert(kStunAddr1);
  stun_servers.insert(kStunAddr2);
  CreateStunPort(stun_servers);
  EXPECT_EQ(IceCandidateType::kSrflx, port()->Type());
  PrepareAddress();
  EXPECT_TRUE_SIMULATED_WAIT(done(), kTimeoutMs, fake_clock);
  EXPECT_EQ(2U, port()->Candidates().size());
  EXPECT_EQ(port()->Candidates()[0].relay_protocol(), "");
  EXPECT_EQ(port()->Candidates()[1].relay_protocol(), "");
}

// Test that the stun_keepalive_lifetime is set correctly based on the network
// type on a STUN port. Also test that it will be updated if the network type
// changes.
TEST_F(StunPortTest, TestStunPortGetStunKeepaliveLifetime) {
  // Lifetime for the default (unknown) network type is `kInfiniteLifetime`.
  CreateStunPort(kStunAddr1);
  EXPECT_EQ(kInfiniteLifetime, port()->stun_keepalive_lifetime());
  // Lifetime for the cellular network is `kHighCostPortKeepaliveLifetimeMs`
  SetNetworkType(rtc::ADAPTER_TYPE_CELLULAR);
  EXPECT_EQ(kHighCostPortKeepaliveLifetimeMs,
            port()->stun_keepalive_lifetime());

  // Lifetime for the wifi network is `kInfiniteLifetime`.
  SetNetworkType(rtc::ADAPTER_TYPE_WIFI);
  CreateStunPort(kStunAddr2);
  EXPECT_EQ(kInfiniteLifetime, port()->stun_keepalive_lifetime());
}

// Test that the stun_keepalive_lifetime is set correctly based on the network
// type on a shared STUN port (UDPPort). Also test that it will be updated
// if the network type changes.
TEST_F(StunPortTest, TestUdpPortGetStunKeepaliveLifetime) {
  // Lifetime for the default (unknown) network type is `kInfiniteLifetime`.
  CreateSharedUdpPort(kStunAddr1, nullptr);
  EXPECT_EQ(kInfiniteLifetime, port()->stun_keepalive_lifetime());
  // Lifetime for the cellular network is `kHighCostPortKeepaliveLifetimeMs`.
  SetNetworkType(rtc::ADAPTER_TYPE_CELLULAR);
  EXPECT_EQ(kHighCostPortKeepaliveLifetimeMs,
            port()->stun_keepalive_lifetime());

  // Lifetime for the wifi network type is `kInfiniteLifetime`.
  SetNetworkType(rtc::ADAPTER_TYPE_WIFI);
  CreateSharedUdpPort(kStunAddr2, nullptr);
  EXPECT_EQ(kInfiniteLifetime, port()->stun_keepalive_lifetime());
}

// Test that STUN binding requests will be stopped shortly if the keep-alive
// lifetime is short.
TEST_F(StunPortTest, TestStunBindingRequestShortLifetime) {
  SetKeepaliveDelay(101);
  SetKeepaliveLifetime(100);
  CreateStunPort(kStunAddr1);
  PrepareAddress();
  EXPECT_TRUE_SIMULATED_WAIT(done(), kTimeoutMs, fake_clock);
  EXPECT_TRUE_SIMULATED_WAIT(!HasPendingRequest(cricket::STUN_BINDING_REQUEST),
                             2000, fake_clock);
}

// Test that by default, the STUN binding requests will last for a long time.
TEST_F(StunPortTest, TestStunBindingRequestLongLifetime) {
  SetKeepaliveDelay(101);
  CreateStunPort(kStunAddr1);
  PrepareAddress();
  EXPECT_TRUE_SIMULATED_WAIT(done(), kTimeoutMs, fake_clock);
  EXPECT_TRUE_SIMULATED_WAIT(HasPendingRequest(cricket::STUN_BINDING_REQUEST),
                             1000, fake_clock);
}

class MockAsyncPacketSocket : public rtc::AsyncPacketSocket {
 public:
  ~MockAsyncPacketSocket() = default;

  MOCK_METHOD(SocketAddress, GetLocalAddress, (), (const, override));
  MOCK_METHOD(SocketAddress, GetRemoteAddress, (), (const, override));
  MOCK_METHOD(int,
              Send,
              (const void* pv, size_t cb, const rtc::PacketOptions& options),
              (override));

  MOCK_METHOD(int,
              SendTo,
              (const void* pv,
               size_t cb,
               const SocketAddress& addr,
               const rtc::PacketOptions& options),
              (override));
  MOCK_METHOD(int, Close, (), (override));
  MOCK_METHOD(State, GetState, (), (const, override));
  MOCK_METHOD(int,
              GetOption,
              (rtc::Socket::Option opt, int* value),
              (override));
  MOCK_METHOD(int, SetOption, (rtc::Socket::Option opt, int value), (override));
  MOCK_METHOD(int, GetError, (), (const, override));
  MOCK_METHOD(void, SetError, (int error), (override));
};

// Test that outbound packets inherit the dscp value assigned to the socket.
TEST_F(StunPortTest, TestStunPacketsHaveDscpPacketOption) {
  MockAsyncPacketSocket* socket = new MockAsyncPacketSocket();
  CreateSharedUdpPort(kStunAddr1, socket);
  EXPECT_CALL(*socket, GetLocalAddress()).WillRepeatedly(Return(kLocalAddr));
  EXPECT_CALL(*socket, GetState())
      .WillRepeatedly(Return(rtc::AsyncPacketSocket::STATE_BOUND));
  EXPECT_CALL(*socket, SetOption(_, _)).WillRepeatedly(Return(0));

  // If DSCP is not set on the socket, stun packets should have no value.
  EXPECT_CALL(*socket,
              SendTo(_, _, _,
                     ::testing::Field(&rtc::PacketOptions::dscp,
                                      ::testing::Eq(rtc::DSCP_NO_CHANGE))))
      .WillOnce(Return(100));
  PrepareAddress();

  // Once it is set transport wide, they should inherit that value.
  port()->SetOption(rtc::Socket::OPT_DSCP, rtc::DSCP_AF41);
  EXPECT_CALL(*socket, SendTo(_, _, _,
                              ::testing::Field(&rtc::PacketOptions::dscp,
                                               ::testing::Eq(rtc::DSCP_AF41))))
      .WillRepeatedly(Return(100));
  EXPECT_TRUE_SIMULATED_WAIT(done(), kTimeoutMs, fake_clock);
}

class StunIPv6PortTestBase : public StunPortTestBase {
 public:
  StunIPv6PortTestBase()
      : StunPortTestBase(rtc::Network("unittestipv6",
                                      "unittestipv6",
                                      kIPv6LocalAddr.ipaddr(),
                                      128),
                         kIPv6LocalAddr.ipaddr()) {
    stun_server_ipv6_1_ =
        cricket::TestStunServer::Create(ss(), kIPv6StunAddr1, thread());
  }

 protected:
  cricket::TestStunServer::StunServerPtr stun_server_ipv6_1_;
};

class StunIPv6PortTestWithRealClock : public StunIPv6PortTestBase {};

class StunIPv6PortTest : public FakeClockBase, public StunIPv6PortTestBase {};

// Test that we can get an address from a STUN server.
TEST_F(StunIPv6PortTest, TestPrepareAddress) {
  CreateStunPort(kIPv6StunAddr1);
  PrepareAddress();
  EXPECT_TRUE_SIMULATED_WAIT(done(), kTimeoutMs, fake_clock);
  ASSERT_EQ(1U, port()->Candidates().size());
  EXPECT_TRUE(kIPv6LocalAddr.EqualIPs(port()->Candidates()[0].address()));
  std::string expected_server_url = "stun:::1:5000";
  EXPECT_EQ(port()->Candidates()[0].url(), expected_server_url);
}

// Test that we fail properly if we can't get an address.
TEST_F(StunIPv6PortTest, TestPrepareAddressFail) {
  CreateStunPort(kBadAddr);
  PrepareAddress();
  EXPECT_TRUE_SIMULATED_WAIT(done(), kTimeoutMs, fake_clock);
  EXPECT_TRUE(error());
  EXPECT_EQ(0U, port()->Candidates().size());
  EXPECT_EQ_SIMULATED_WAIT(error_event_.error_code,
                           cricket::SERVER_NOT_REACHABLE_ERROR, kTimeoutMs,
                           fake_clock);
  ASSERT_NE(error_event_.error_text.find('.'), std::string::npos);
  ASSERT_NE(
      error_event_.address.find(kIPv6LocalAddr.HostAsSensitiveURIString()),
      std::string::npos);
  std::string server_url = "stun:" + kBadAddr.ToString();
  ASSERT_EQ(error_event_.url, server_url);
}

// Test that we handle hostname lookup failures properly with a real clock.
TEST_F(StunIPv6PortTestWithRealClock, TestPrepareAddressHostnameFail) {
  CreateStunPort(kBadHostnameAddr);
  PrepareAddress();
  EXPECT_TRUE_WAIT(done(), kTimeoutMs);
  EXPECT_TRUE(error());
  EXPECT_EQ(0U, port()->Candidates().size());
  EXPECT_EQ_WAIT(error_event_.error_code, cricket::SERVER_NOT_REACHABLE_ERROR,
                 kTimeoutMs);
}

class StunIPv6PortTestWithMockDnsResolver : public StunIPv6PortTest {
 public:
  StunIPv6PortTestWithMockDnsResolver()
      : StunIPv6PortTest(), socket_factory_(ss()) {}

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

// Test that we can get an address from a STUN server specified by a hostname.
TEST_F(StunIPv6PortTestWithMockDnsResolver, TestPrepareAddressHostname) {
  SetDnsResolverExpectations(
      [](webrtc::MockAsyncDnsResolver* resolver,
         webrtc::MockAsyncDnsResolverResult* resolver_result) {
        EXPECT_CALL(*resolver,
                    Start(kValidHostnameAddr, /*family=*/AF_INET6, _))
            .WillOnce([](const rtc::SocketAddress& addr, int family,
                         absl::AnyInvocable<void()> callback) { callback(); });

        EXPECT_CALL(*resolver, result)
            .WillRepeatedly(ReturnPointee(resolver_result));
        EXPECT_CALL(*resolver_result, GetError).WillOnce(Return(0));
        EXPECT_CALL(*resolver_result, GetResolvedAddress(AF_INET6, _))
            .WillOnce(DoAll(SetArgPointee<1>(SocketAddress("::1", 5000)),
                            Return(true)));
      });
  CreateStunPort(kValidHostnameAddr);
  PrepareAddress();
  EXPECT_TRUE_SIMULATED_WAIT(done(), kTimeoutMs, fake_clock);
  ASSERT_EQ(1U, port()->Candidates().size());
  EXPECT_TRUE(kIPv6LocalAddr.EqualIPs(port()->Candidates()[0].address()));
  EXPECT_EQ(kIPv6StunCandidatePriority, port()->Candidates()[0].priority());
}

// Same as before but with a field trial that changes the priority.
TEST_F(StunIPv6PortTestWithMockDnsResolver,
       TestPrepareAddressHostnameWithPriorityAdjustment) {
  webrtc::test::ScopedKeyValueConfig field_trials(
      "WebRTC-IncreaseIceCandidatePriorityHostSrflx/Enabled/");
  SetDnsResolverExpectations(
      [](webrtc::MockAsyncDnsResolver* resolver,
         webrtc::MockAsyncDnsResolverResult* resolver_result) {
        EXPECT_CALL(*resolver,
                    Start(kValidHostnameAddr, /*family=*/AF_INET6, _))
            .WillOnce([](const rtc::SocketAddress& addr, int family,
                         absl::AnyInvocable<void()> callback) { callback(); });
        EXPECT_CALL(*resolver, result)
            .WillRepeatedly(ReturnPointee(resolver_result));
        EXPECT_CALL(*resolver_result, GetError).WillOnce(Return(0));
        EXPECT_CALL(*resolver_result, GetResolvedAddress(AF_INET6, _))
            .WillOnce(DoAll(SetArgPointee<1>(SocketAddress("::1", 5000)),
                            Return(true)));
      });
  CreateStunPort(kValidHostnameAddr, &field_trials);
  PrepareAddress();
  EXPECT_TRUE_SIMULATED_WAIT(done(), kTimeoutMs, fake_clock);
  ASSERT_EQ(1U, port()->Candidates().size());
  EXPECT_TRUE(kIPv6LocalAddr.EqualIPs(port()->Candidates()[0].address()));
  EXPECT_EQ(kIPv6StunCandidatePriority + (cricket::kMaxTurnServers << 8),
            port()->Candidates()[0].priority());
}

}  // namespace
