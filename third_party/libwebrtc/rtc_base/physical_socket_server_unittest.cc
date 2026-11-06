/*
 *  Copyright 2004 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/physical_socket_server.h"

#include <signal.h>

#include <algorithm>
#include <memory>

#include "rtc_base/gunit.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/logging.h"
#include "rtc_base/net_helpers.h"
#include "rtc_base/net_test_helpers.h"
#include "rtc_base/network_monitor.h"
#include "rtc_base/socket_unittest.h"
#include "rtc_base/test_utils.h"
#include "rtc_base/thread.h"
#include "test/field_trial.h"
#include "test/gtest.h"

namespace rtc {

#define MAYBE_SKIP_IPV4                        \
  if (!HasIPv4Enabled()) {                     \
    RTC_LOG(LS_INFO) << "No IPv4... skipping"; \
    return;                                    \
  }

#define MAYBE_SKIP_IPV6                        \
  if (!HasIPv6Enabled()) {                     \
    RTC_LOG(LS_INFO) << "No IPv6... skipping"; \
    return;                                    \
  }

class PhysicalSocketTest;

class FakeSocketDispatcher : public SocketDispatcher {
 public:
  explicit FakeSocketDispatcher(PhysicalSocketServer* ss)
      : SocketDispatcher(ss) {}

  FakeSocketDispatcher(SOCKET s, PhysicalSocketServer* ss)
      : SocketDispatcher(s, ss) {}

 protected:
  SOCKET DoAccept(SOCKET socket, sockaddr* addr, socklen_t* addrlen) override;
  int DoSend(SOCKET socket, const char* buf, int len, int flags) override;
  int DoSendTo(SOCKET socket,
               const char* buf,
               int len,
               int flags,
               const struct sockaddr* dest_addr,
               socklen_t addrlen) override;
};

class FakePhysicalSocketServer : public PhysicalSocketServer {
 public:
  explicit FakePhysicalSocketServer(PhysicalSocketTest* test) : test_(test) {}

  Socket* CreateSocket(int family, int type) override {
    SocketDispatcher* dispatcher = new FakeSocketDispatcher(this);
    if (!dispatcher->Create(family, type)) {
      delete dispatcher;
      return nullptr;
    }
    return dispatcher;
  }

  Socket* WrapSocket(SOCKET s) override {
    SocketDispatcher* dispatcher = new FakeSocketDispatcher(s, this);
    if (!dispatcher->Initialize()) {
      delete dispatcher;
      return nullptr;
    }
    return dispatcher;
  }

  PhysicalSocketTest* GetTest() const { return test_; }

 private:
  PhysicalSocketTest* test_;
};

class FakeNetworkBinder : public NetworkBinderInterface {
 public:
  NetworkBindingResult BindSocketToNetwork(int, const IPAddress&) override {
    ++num_binds_;
    return result_;
  }

  void set_result(NetworkBindingResult result) { result_ = result; }

  int num_binds() { return num_binds_; }

 private:
  NetworkBindingResult result_ = NetworkBindingResult::SUCCESS;
  int num_binds_ = 0;
};

class PhysicalSocketTest : public SocketTest {
 public:
  // Set flag to simluate failures when calling "::accept" on a Socket.
  void SetFailAccept(bool fail) { fail_accept_ = fail; }
  bool FailAccept() const { return fail_accept_; }

  // Maximum size to ::send to a socket. Set to < 0 to disable limiting.
  void SetMaxSendSize(int max_size) { max_send_size_ = max_size; }
  int MaxSendSize() const { return max_send_size_; }

 protected:
  PhysicalSocketTest()
      : SocketTest(&server_),
        server_(this),
        thread_(&server_),
        fail_accept_(false),
        max_send_size_(-1) {}

  void ConnectInternalAcceptError(const IPAddress& loopback);
  void WritableAfterPartialWrite(const IPAddress& loopback);

  FakePhysicalSocketServer server_;
  rtc::AutoSocketServerThread thread_;
  bool fail_accept_;
  int max_send_size_;
};

SOCKET FakeSocketDispatcher::DoAccept(SOCKET socket,
                                      sockaddr* addr,
                                      socklen_t* addrlen) {
  FakePhysicalSocketServer* ss =
      static_cast<FakePhysicalSocketServer*>(socketserver());
  if (ss->GetTest()->FailAccept()) {
    return INVALID_SOCKET;
  }

  return SocketDispatcher::DoAccept(socket, addr, addrlen);
}

int FakeSocketDispatcher::DoSend(SOCKET socket,
                                 const char* buf,
                                 int len,
                                 int flags) {
  FakePhysicalSocketServer* ss =
      static_cast<FakePhysicalSocketServer*>(socketserver());
  if (ss->GetTest()->MaxSendSize() >= 0) {
    len = std::min(len, ss->GetTest()->MaxSendSize());
  }

  return SocketDispatcher::DoSend(socket, buf, len, flags);
}

int FakeSocketDispatcher::DoSendTo(SOCKET socket,
                                   const char* buf,
                                   int len,
                                   int flags,
                                   const struct sockaddr* dest_addr,
                                   socklen_t addrlen) {
  FakePhysicalSocketServer* ss =
      static_cast<FakePhysicalSocketServer*>(socketserver());
  if (ss->GetTest()->MaxSendSize() >= 0) {
    len = std::min(len, ss->GetTest()->MaxSendSize());
  }

  return SocketDispatcher::DoSendTo(socket, buf, len, flags, dest_addr,
                                    addrlen);
}

TEST_F(PhysicalSocketTest, TestConnectIPv4) {
  MAYBE_SKIP_IPV4;
  SocketTest::TestConnectIPv4();
}

TEST_F(PhysicalSocketTest, TestConnectIPv6) {
  SocketTest::TestConnectIPv6();
}

TEST_F(PhysicalSocketTest, TestConnectWithDnsLookupIPv4) {
  MAYBE_SKIP_IPV4;
  SocketTest::TestConnectWithDnsLookupIPv4();
}

TEST_F(PhysicalSocketTest, TestConnectWithDnsLookupIPv6) {
  SocketTest::TestConnectWithDnsLookupIPv6();
}

TEST_F(PhysicalSocketTest, TestConnectFailIPv4) {
  MAYBE_SKIP_IPV4;
  SocketTest::TestConnectFailIPv4();
}

void PhysicalSocketTest::ConnectInternalAcceptError(const IPAddress& loopback) {
  webrtc::testing::StreamSink sink;
  SocketAddress accept_addr;

  // Create two clients.
  std::unique_ptr<Socket> client1(
      server_.CreateSocket(loopback.family(), SOCK_STREAM));
  sink.Monitor(client1.get());
  EXPECT_EQ(Socket::CS_CLOSED, client1->GetState());
  EXPECT_TRUE(IsUnspecOrEmptyIP(client1->GetLocalAddress().ipaddr()));

  std::unique_ptr<Socket> client2(
      server_.CreateSocket(loopback.family(), SOCK_STREAM));
  sink.Monitor(client2.get());
  EXPECT_EQ(Socket::CS_CLOSED, client2->GetState());
  EXPECT_TRUE(IsUnspecOrEmptyIP(client2->GetLocalAddress().ipaddr()));

  // Create server and listen.
  std::unique_ptr<Socket> server(
      server_.CreateSocket(loopback.family(), SOCK_STREAM));
  sink.Monitor(server.get());
  EXPECT_EQ(0, server->Bind(SocketAddress(loopback, 0)));
  EXPECT_EQ(0, server->Listen(5));
  EXPECT_EQ(Socket::CS_CONNECTING, server->GetState());

  // Ensure no pending server connections, since we haven't done anything yet.
  EXPECT_FALSE(sink.Check(server.get(), webrtc::testing::SSE_READ));
  EXPECT_TRUE(nullptr == server->Accept(&accept_addr));
  EXPECT_TRUE(accept_addr.IsNil());

  // Attempt first connect to listening socket.
  EXPECT_EQ(0, client1->Connect(server->GetLocalAddress()));
  EXPECT_FALSE(client1->GetLocalAddress().IsNil());
  EXPECT_NE(server->GetLocalAddress(), client1->GetLocalAddress());

  // Client is connecting, outcome not yet determined.
  EXPECT_EQ(Socket::CS_CONNECTING, client1->GetState());
  EXPECT_FALSE(sink.Check(client1.get(), webrtc::testing::SSE_OPEN));
  EXPECT_FALSE(sink.Check(client1.get(), webrtc::testing::SSE_CLOSE));

  // Server has pending connection, try to accept it (will fail).
  EXPECT_TRUE_WAIT((sink.Check(server.get(), webrtc::testing::SSE_READ)),
                   kTimeout);
  // Simulate "::accept" returning an error.
  SetFailAccept(true);
  std::unique_ptr<Socket> accepted(server->Accept(&accept_addr));
  EXPECT_FALSE(accepted);
  ASSERT_TRUE(accept_addr.IsNil());

  // Ensure no more pending server connections.
  EXPECT_FALSE(sink.Check(server.get(), webrtc::testing::SSE_READ));
  EXPECT_TRUE(nullptr == server->Accept(&accept_addr));
  EXPECT_TRUE(accept_addr.IsNil());

  // Attempt second connect to listening socket.
  EXPECT_EQ(0, client2->Connect(server->GetLocalAddress()));
  EXPECT_FALSE(client2->GetLocalAddress().IsNil());
  EXPECT_NE(server->GetLocalAddress(), client2->GetLocalAddress());

  // Client is connecting, outcome not yet determined.
  EXPECT_EQ(Socket::CS_CONNECTING, client2->GetState());
  EXPECT_FALSE(sink.Check(client2.get(), webrtc::testing::SSE_OPEN));
  EXPECT_FALSE(sink.Check(client2.get(), webrtc::testing::SSE_CLOSE));

  // Server has pending connection, try to accept it (will succeed).
  EXPECT_TRUE_WAIT((sink.Check(server.get(), webrtc::testing::SSE_READ)),
                   kTimeout);
  SetFailAccept(false);
  std::unique_ptr<Socket> accepted2(server->Accept(&accept_addr));
  ASSERT_TRUE(accepted2);
  EXPECT_FALSE(accept_addr.IsNil());
  EXPECT_EQ(accepted2->GetRemoteAddress(), accept_addr);
}

TEST_F(PhysicalSocketTest, TestConnectAcceptErrorIPv4) {
  MAYBE_SKIP_IPV4;
  ConnectInternalAcceptError(kIPv4Loopback);
}

TEST_F(PhysicalSocketTest, TestConnectAcceptErrorIPv6) {
  MAYBE_SKIP_IPV6;
  ConnectInternalAcceptError(kIPv6Loopback);
}

void PhysicalSocketTest::WritableAfterPartialWrite(const IPAddress& loopback) {
  // Simulate a really small maximum send size.
  const int kMaxSendSize = 128;
  SetMaxSendSize(kMaxSendSize);

  // Run the default send/receive socket tests with a smaller amount of data
  // to avoid long running times due to the small maximum send size.
  const size_t kDataSize = 128 * 1024;
  TcpInternal(loopback, kDataSize, kMaxSendSize);
}

// https://bugs.chromium.org/p/webrtc/issues/detail?id=6167
#if defined(WEBRTC_WIN)
#define MAYBE_TestWritableAfterPartialWriteIPv4 \
  DISABLED_TestWritableAfterPartialWriteIPv4
#else
#define MAYBE_TestWritableAfterPartialWriteIPv4 \
  TestWritableAfterPartialWriteIPv4
#endif
TEST_F(PhysicalSocketTest, MAYBE_TestWritableAfterPartialWriteIPv4) {
  MAYBE_SKIP_IPV4;
  WritableAfterPartialWrite(kIPv4Loopback);
}

// https://bugs.chromium.org/p/webrtc/issues/detail?id=6167
#if defined(WEBRTC_WIN)
#define MAYBE_TestWritableAfterPartialWriteIPv6 \
  DISABLED_TestWritableAfterPartialWriteIPv6
#else
#define MAYBE_TestWritableAfterPartialWriteIPv6 \
  TestWritableAfterPartialWriteIPv6
#endif
TEST_F(PhysicalSocketTest, MAYBE_TestWritableAfterPartialWriteIPv6) {
  MAYBE_SKIP_IPV6;
  WritableAfterPartialWrite(kIPv6Loopback);
}

TEST_F(PhysicalSocketTest, TestConnectFailIPv6) {
  SocketTest::TestConnectFailIPv6();
}

TEST_F(PhysicalSocketTest, TestConnectWithDnsLookupFailIPv4) {
  MAYBE_SKIP_IPV4;
  SocketTest::TestConnectWithDnsLookupFailIPv4();
}

TEST_F(PhysicalSocketTest, TestConnectWithDnsLookupFailIPv6) {
  SocketTest::TestConnectWithDnsLookupFailIPv6();
}

TEST_F(PhysicalSocketTest, TestConnectWithClosedSocketIPv4) {
  MAYBE_SKIP_IPV4;
  SocketTest::TestConnectWithClosedSocketIPv4();
}

TEST_F(PhysicalSocketTest, TestConnectWithClosedSocketIPv6) {
  SocketTest::TestConnectWithClosedSocketIPv6();
}

TEST_F(PhysicalSocketTest, TestConnectWhileNotClosedIPv4) {
  MAYBE_SKIP_IPV4;
  SocketTest::TestConnectWhileNotClosedIPv4();
}

TEST_F(PhysicalSocketTest, TestConnectWhileNotClosedIPv6) {
  SocketTest::TestConnectWhileNotClosedIPv6();
}

TEST_F(PhysicalSocketTest, TestServerCloseDuringConnectIPv4) {
  MAYBE_SKIP_IPV4;
  SocketTest::TestServerCloseDuringConnectIPv4();
}

TEST_F(PhysicalSocketTest, TestServerCloseDuringConnectIPv6) {
  SocketTest::TestServerCloseDuringConnectIPv6();
}

TEST_F(PhysicalSocketTest, TestClientCloseDuringConnectIPv4) {
  MAYBE_SKIP_IPV4;
  SocketTest::TestClientCloseDuringConnectIPv4();
}

TEST_F(PhysicalSocketTest, TestClientCloseDuringConnectIPv6) {
  SocketTest::TestClientCloseDuringConnectIPv6();
}

TEST_F(PhysicalSocketTest, TestServerCloseIPv4) {
  MAYBE_SKIP_IPV4;
  SocketTest::TestServerCloseIPv4();
}

TEST_F(PhysicalSocketTest, TestServerCloseIPv6) {
  SocketTest::TestServerCloseIPv6();
}

TEST_F(PhysicalSocketTest, TestCloseInClosedCallbackIPv4) {
  MAYBE_SKIP_IPV4;
  SocketTest::TestCloseInClosedCallbackIPv4();
}

TEST_F(PhysicalSocketTest, TestCloseInClosedCallbackIPv6) {
  SocketTest::TestCloseInClosedCallbackIPv6();
}

TEST_F(PhysicalSocketTest, TestDeleteInReadCallbackIPv4) {
  MAYBE_SKIP_IPV4;
  SocketTest::TestDeleteInReadCallbackIPv4();
}

TEST_F(PhysicalSocketTest, TestDeleteInReadCallbackIPv6) {
  SocketTest::TestDeleteInReadCallbackIPv6();
}

TEST_F(PhysicalSocketTest, TestSocketServerWaitIPv4) {
  MAYBE_SKIP_IPV4;
  SocketTest::TestSocketServerWaitIPv4();
}

TEST_F(PhysicalSocketTest, TestSocketServerWaitIPv6) {
  SocketTest::TestSocketServerWaitIPv6();
}

TEST_F(PhysicalSocketTest, TestTcpIPv4) {
  MAYBE_SKIP_IPV4;
  SocketTest::TestTcpIPv4();
}

TEST_F(PhysicalSocketTest, TestTcpIPv6) {
  SocketTest::TestTcpIPv6();
}

TEST_F(PhysicalSocketTest, TestUdpIPv4) {
  MAYBE_SKIP_IPV4;
  SocketTest::TestUdpIPv4();
}

TEST_F(PhysicalSocketTest, TestUdpIPv6) {
  SocketTest::TestUdpIPv6();
}

// Disable for TSan v2, see
// https://code.google.com/p/webrtc/issues/detail?id=3498 for details.
// Also disable for MSan, see:
// https://code.google.com/p/webrtc/issues/detail?id=4958
// TODO(deadbeef): Enable again once test is reimplemented to be unflaky.
// Also disable for ASan.
// Disabled on Android: https://code.google.com/p/webrtc/issues/detail?id=4364
// Disabled on Linux: https://bugs.chromium.org/p/webrtc/issues/detail?id=5233
#if defined(THREAD_SANITIZER) || defined(MEMORY_SANITIZER) || \
    defined(ADDRESS_SANITIZER) || defined(WEBRTC_ANDROID) ||  \
    defined(WEBRTC_LINUX)
#define MAYBE_TestUdpReadyToSendIPv4 DISABLED_TestUdpReadyToSendIPv4
#else
#define MAYBE_TestUdpReadyToSendIPv4 TestUdpReadyToSendIPv4
#endif
TEST_F(PhysicalSocketTest, MAYBE_TestUdpReadyToSendIPv4) {
  MAYBE_SKIP_IPV4;
  SocketTest::TestUdpReadyToSendIPv4();
}

// https://bugs.chromium.org/p/webrtc/issues/detail?id=6167
#if defined(WEBRTC_WIN)
#define MAYBE_TestUdpReadyToSendIPv6 DISABLED_TestUdpReadyToSendIPv6
#else
#define MAYBE_TestUdpReadyToSendIPv6 TestUdpReadyToSendIPv6
#endif
TEST_F(PhysicalSocketTest, MAYBE_TestUdpReadyToSendIPv6) {
  SocketTest::TestUdpReadyToSendIPv6();
}

TEST_F(PhysicalSocketTest, TestGetSetOptionsIPv4) {
  MAYBE_SKIP_IPV4;
  SocketTest::TestGetSetOptionsIPv4();
}

TEST_F(PhysicalSocketTest, TestGetSetOptionsIPv6) {
  SocketTest::TestGetSetOptionsIPv6();
}

#if defined(WEBRTC_POSIX)

TEST_F(PhysicalSocketTest, TestSocketRecvTimestampIPv4) {
  MAYBE_SKIP_IPV4;
  SocketTest::TestSocketRecvTimestampIPv4();
}

TEST_F(PhysicalSocketTest, TestSocketRecvTimestampIPv6) {
  SocketTest::TestSocketRecvTimestampIPv6();
}

#if !defined(WEBRTC_MAC)
TEST_F(PhysicalSocketTest, TestSocketRecvTimestampIPv4ScmExperimentDisabled) {
  MAYBE_SKIP_IPV4;
  webrtc::test::ScopedFieldTrials trial("WebRTC-SCM-Timestamp/Disabled/");
  SocketTest::TestSocketRecvTimestampIPv4();
}

TEST_F(PhysicalSocketTest, TestSocketRecvTimestampIPv6ScmExperimentDisabled) {
  webrtc::test::ScopedFieldTrials trial("WebRTC-SCM-Timestamp/Disabled/");
  SocketTest::TestSocketRecvTimestampIPv6();
}
#endif

#if !defined(WEBRTC_MAC) && !defined(WEBRTC_IOS)
// TODO(bugs.webrtc.org/15368): IpV4 fails on IOS and MAC. IPV6 works.
TEST_F(PhysicalSocketTest, TestSocketSendRecvWithEcnIPv4) {
  MAYBE_SKIP_IPV6;
  SocketTest::TestSocketSendRecvWithEcnIPV4();
}
#endif

TEST_F(PhysicalSocketTest, TestSocketSendRecvWithEcnIPv6) {
  MAYBE_SKIP_IPV6;
  SocketTest::TestSocketSendRecvWithEcnIPV6();
}

// Verify that if the socket was unable to be bound to a real network interface
// (not loopback), Bind will return an error.
TEST_F(PhysicalSocketTest,
       BindFailsIfNetworkBinderFailsForNonLoopbackInterface) {
  MAYBE_SKIP_IPV4;
  FakeNetworkBinder fake_network_binder;
  server_.set_network_binder(&fake_network_binder);
  std::unique_ptr<Socket> socket(server_.CreateSocket(AF_INET, SOCK_DGRAM));
  fake_network_binder.set_result(NetworkBindingResult::FAILURE);
  EXPECT_EQ(-1, socket->Bind(SocketAddress("192.168.0.1", 0)));
  server_.set_network_binder(nullptr);
}

// Network binder shouldn't be used if the socket is bound to the "any" IP.
TEST_F(PhysicalSocketTest, NetworkBinderIsNotUsedForAnyIp) {
  MAYBE_SKIP_IPV4;
  FakeNetworkBinder fake_network_binder;
  server_.set_network_binder(&fake_network_binder);
  std::unique_ptr<Socket> socket(server_.CreateSocket(AF_INET, SOCK_DGRAM));
  EXPECT_EQ(0, socket->Bind(SocketAddress("0.0.0.0", 0)));
  EXPECT_EQ(0, fake_network_binder.num_binds());
  server_.set_network_binder(nullptr);
}

// For a loopback interface, failures to bind to the interface should be
// tolerated.
TEST_F(PhysicalSocketTest,
       BindSucceedsIfNetworkBinderFailsForLoopbackInterface) {
  MAYBE_SKIP_IPV4;
  FakeNetworkBinder fake_network_binder;
  server_.set_network_binder(&fake_network_binder);
  std::unique_ptr<Socket> socket(server_.CreateSocket(AF_INET, SOCK_DGRAM));
  fake_network_binder.set_result(NetworkBindingResult::FAILURE);
  EXPECT_EQ(0, socket->Bind(SocketAddress(kIPv4Loopback, 0)));
  server_.set_network_binder(nullptr);
}

#endif

TEST_F(PhysicalSocketTest, UdpSocketRecvTimestampUseRtcEpochIPv4) {
  MAYBE_SKIP_IPV4;
  SocketTest::TestUdpSocketRecvTimestampUseRtcEpochIPv4();
}

TEST_F(PhysicalSocketTest, UdpSocketRecvTimestampUseRtcEpochIPv6) {
  SocketTest::TestUdpSocketRecvTimestampUseRtcEpochIPv6();
}

}  // namespace rtc
