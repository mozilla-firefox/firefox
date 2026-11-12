/*
 *  Copyright 2025 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/string_view.h"
#include "api/environment/environment.h"
#include "api/local_network_access_permission.h"
#include "api/test/mock_async_dns_resolver.h"
#include "api/test/mock_local_network_access_permission.h"
#include "api/units/timestamp.h"
#include "p2p/base/port.h"
#include "p2p/base/port_allocator.h"
#include "p2p/base/stun_port.h"
#include "p2p/base/turn_port.h"
#include "p2p/client/relay_port_factory_interface.h"
#include "p2p/test/mock_dns_resolving_packet_socket_factory.h"
#include "p2p/test/test_stun_server.h"
#include "p2p/test/test_turn_server.h"
#include "p2p/test/turn_server.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/net_helpers.h"
#include "rtc_base/network.h"
#include "rtc_base/socket.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/virtual_socket_server.h"
#include "test/create_test_environment.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/time_controller/simulated_time_controller.h"
#include "test/wait_until.h"

namespace webrtc {
namespace {

using LnaFakeResult = FakeLocalNetworkAccessPermissionFactory::Result;
using ::testing::_;
using ::testing::IsTrue;
using ::testing::Return;
using ::testing::ReturnPointee;

const SocketAddress kTurnUdpIntAddr("99.99.99.3", webrtc::TURN_SERVER_PORT);
const SocketAddress kTurnUdpExtAddr("99.99.99.5", 0);
const SocketAddress kLocalAddr("11.11.11.11", 0);
const SocketAddress kLocalIPv6Addr("2401:fa00:4:1000:be30:5bff:fee5:c3", 0);

constexpr char kIceUfrag[] = "TESTICEUFRAG0001";
constexpr char kIcePwd[] = "TESTICEPWD00000000000001";
constexpr char kTurnUsername[] = "test";
constexpr char kTurnPassword[] = "test";

enum ServerType { kStun, kTurn };

// Class to test LocalNetworkAccess integration with STUN and TURN ports.
class LocalNetworkAccessPortTest
    : public ::testing::TestWithParam<
          std::tuple<ServerType, absl::string_view, LnaFakeResult>> {
 public:
  LocalNetworkAccessPortTest() {
    network_.AddIP(local_address_.ipaddr());

    switch (server_type()) {
      case kStun:
        stun_server_ =
            TestStunServer::Create(env_, {server_address(), 5000}, ss_,
                                   *time_controller_.GetMainThread());
        break;
      case kTurn:
        turn_server_.AddInternalSocket({server_address(), 5000}, PROTO_UDP);
        break;
    }
  }

  ~LocalNetworkAccessPortTest() override = default;

  void OnPortComplete(Port* port) { port_ready_ = true; }
  void OnPortError(Port* port) { port_error_ = true; }

 protected:
  static ServerType server_type() { return std::get<0>(GetParam()); }
  static absl::string_view server_address() { return std::get<1>(GetParam()); }
  static LnaFakeResult lna_fake_result() { return std::get<2>(GetParam()); }

  // Creates STUN or TURN port depending on the type of port we are testing.
  std::unique_ptr<Port> CreatePort(
      absl::string_view server_address,
      LocalNetworkAccessPermissionFactoryInterface& lna_permission_factory) {
    switch (server_type()) {
      case kStun:
        return CreateStunPort(server_address, lna_permission_factory);
      case kTurn:
        return CreateTurnPort(server_address, lna_permission_factory);
    }
  }

  std::unique_ptr<TurnPort> CreateTurnPort(
      absl::string_view server_address,
      LocalNetworkAccessPermissionFactoryInterface& lna_permission_factory) {
    RelayServerConfig config;
    config.credentials = RelayCredentials(kTurnUsername, kTurnPassword);

    ProtocolAddress proto_server_address({server_address, 5000}, PROTO_UDP);
    CreateRelayPortArgs args = {
        .env = env_,
        .network_thread = time_controller_.GetMainThread(),
        .socket_factory = &socket_factory_,
        .network = &network_,
        .server_address = &proto_server_address,
        .config = &config,
        .username = kIceUfrag,
        .password = kIcePwd,
        .lna_permission_factory = &lna_permission_factory,
    };

    // The tests wait for either of the callbacks to be fired by checking if
    // port_ready_ or port_error_ becomes true. If neither happens, the test
    // will fail after a timeout.
    args.callbacks.tag = this;
    args.callbacks.port_complete = [this](Port* port) { OnPortComplete(port); };
    args.callbacks.port_error = [this](Port* port) { OnPortError(port); };

    return TurnPort::Create(std::move(args), /*min_port=*/0, /*max_port=*/0);
  }

  std::unique_ptr<StunPort> CreateStunPort(
      absl::string_view server_address,
      LocalNetworkAccessPermissionFactoryInterface& lna_permission_factory) {
    Port::PortParametersRef params = {
        .env = env_,
        .network_thread = time_controller_.GetMainThread(),
        .socket_factory = &socket_factory_,
        .network = &network_,
        .ice_username_fragment = kIceUfrag,
        .ice_password = kIcePwd,
        .lna_permission_factory = &lna_permission_factory,
    };

    params.callbacks.tag = this;
    params.callbacks.port_complete = [this](Port* port) {
      OnPortComplete(port);
    };
    params.callbacks.port_error = [this](Port* port) { OnPortError(port); };

    return StunPort::Create(std::move(params), 0, 0,
                            {SocketAddress(server_address, 5000)},
                            std::nullopt);
  }

  void setup_dns_resolver_mock() {
    auto expectations =
        [](webrtc::MockAsyncDnsResolver* resolver,
           webrtc::MockAsyncDnsResolverResult* resolver_result) {
          EXPECT_CALL(*resolver, Start(_, _, _))
              .WillOnce(
                  [](const webrtc::SocketAddress& /* addr */, int /* family */,
                     absl::AnyInvocable<void()> callback) { callback(); });

          EXPECT_CALL(*resolver, result)
              .WillRepeatedly(ReturnPointee(resolver_result));
          EXPECT_CALL(*resolver_result, GetError).WillRepeatedly(Return(0));
          EXPECT_CALL(*resolver_result, GetResolvedAddress(_, _))
              .WillOnce([](int /*family*/, SocketAddress* addr) {
                *addr = SocketAddress(server_address(), 5000);
                return true;
              });
        };

    socket_factory_.SetExpectations(std::move(expectations));
  }

  bool port_ready_ = false;
  bool port_error_ = false;

  VirtualSocketServer ss_;
  GlobalSimulatedTimeController time_controller_{Timestamp::Micros(1234567),
                                                 &ss_};
  const Environment env_ = CreateTestEnvironment({.time = &time_controller_});
  MockDnsResolvingPacketSocketFactory socket_factory_{&ss_};

  const bool is_using_ipv6_{SocketAddress(server_address(), 5000).family() ==
                            AF_INET6};
  const SocketAddress local_address_{is_using_ipv6_ ? kLocalIPv6Addr
                                                    : kLocalAddr};
  Network network_{"unittest", "unittest", local_address_.ipaddr(), 32};

  TestTurnServer turn_server_{env_, time_controller_.GetMainThread(), &ss_,
                              kTurnUdpIntAddr, kTurnUdpExtAddr};
  TestStunServer::StunServerPtr stun_server_;
};

std::string GetTestName(
    const testing::TestParamInfo<LocalNetworkAccessPortTest::ParamType>& info) {
  std::string protocol_str;
  switch (std::get<0>(info.param)) {
    case kStun:
      protocol_str = "Stun";
      break;
    case kTurn:
      protocol_str = "Turn";
      break;
  }

  // Remove ":"
  std::string sanitized_address = absl::StrReplaceAll(
      std::get<1>(info.param), {{"::", "_"}, {":", "_"}, {".", "_"}});

  std::string result_str;
  switch (std::get<2>(info.param)) {
    case LnaFakeResult::kPermissionNotNeeded:
      result_str = "PermissionNotNeeded";
      break;
    case LnaFakeResult::kPermissionGranted:
      result_str = "PermissionGranted";
      break;
    case LnaFakeResult::kPermissionDenied:
      result_str = "PermissionDenied";
      break;
  }

  return absl::StrFormat("%s_%s_%s", protocol_str, sanitized_address,
                         result_str);
}

constexpr absl::string_view kTestAddresses[] = {
    "127.0.0.1",
    "10.0.0.3",
    "1.1.1.1",
    "::1",
    "fd00:4860:4860::8844",
    "2001:4860:4860::8888",
};

INSTANTIATE_TEST_SUITE_P(
    All,
    LocalNetworkAccessPortTest,
    ::testing::Combine(::testing::Values(kStun, kTurn),
                       ::testing::ValuesIn(kTestAddresses),
                       ::testing::Values(LnaFakeResult::kPermissionNotNeeded,
                                         LnaFakeResult::kPermissionGranted,
                                         LnaFakeResult::kPermissionDenied)),
    &GetTestName);

TEST_P(LocalNetworkAccessPortTest, ResolvedAddress) {
  FakeLocalNetworkAccessPermissionFactory factory(lna_fake_result());

  auto port = CreatePort(server_address(), factory);
  port->PrepareAddress();

  if (lna_fake_result() == LnaFakeResult::kPermissionNotNeeded ||
      lna_fake_result() == LnaFakeResult::kPermissionGranted) {
    EXPECT_TRUE(
        WaitUntil([&] { return port_ready_; }, {.clock = &time_controller_}));
    EXPECT_EQ(1u, port->Candidates().size());
    EXPECT_NE(SOCKET_ERROR, port->GetError());
  } else {
    EXPECT_TRUE(
        WaitUntil([&] { return port_error_; }, {.clock = &time_controller_}));
    EXPECT_EQ(0u, port->Candidates().size());
    EXPECT_NE(SOCKET_ERROR, port->GetError());
  }
}

TEST_P(LocalNetworkAccessPortTest, UnresolvedAddress) {
  setup_dns_resolver_mock();
  FakeLocalNetworkAccessPermissionFactory factory(lna_fake_result());

  auto port = CreatePort("fakehost.test", factory);
  port->PrepareAddress();

  if (lna_fake_result() == LnaFakeResult::kPermissionNotNeeded ||
      lna_fake_result() == LnaFakeResult::kPermissionGranted) {
    EXPECT_TRUE(
        WaitUntil([&] { return port_ready_; }, {.clock = &time_controller_}));
    EXPECT_EQ(1u, port->Candidates().size());
    EXPECT_NE(SOCKET_ERROR, port->GetError());
  } else {
    EXPECT_TRUE(
        WaitUntil([&] { return port_error_; }, {.clock = &time_controller_}));
    EXPECT_EQ(0u, port->Candidates().size());
    EXPECT_NE(SOCKET_ERROR, port->GetError());
  }
}

}  // namespace
}  // namespace webrtc
