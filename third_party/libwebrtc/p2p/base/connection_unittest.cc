/*
 *  Copyright 2025 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "p2p/base/connection.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "api/environment/environment.h"
#include "api/rtc_error.h"
#include "api/transport/stun.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "p2p/base/basic_packet_socket_factory.h"
#include "p2p/base/port.h"
#include "p2p/base/port_interface.h"
#include "p2p/base/transport_description.h"
#include "p2p/dtls/dtls_stun_piggyback_callbacks.h"
#include "p2p/test/test_port.h"
#include "rtc_base/buffer.h"
#include "rtc_base/network.h"
#include "rtc_base/network/received_packet.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/virtual_socket_server.h"
#include "test/create_test_environment.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/time_controller/simulated_time_controller.h"
#include "test/wait_until.h"

namespace webrtc {
namespace {

using ::testing::IsTrue;

constexpr int kDefaultTimeout = 3000;
const SocketAddress kLocalAddr1("192.168.1.2", 0);
const SocketAddress kLocalAddr2("192.168.1.3", 0);

constexpr uint64_t kTiebreaker1 = 11111;
constexpr uint64_t kTiebreaker2 = 22222;

class ConnectionTest : public ::testing::Test {
 public:
  ConnectionTest()
      : ss_(new VirtualSocketServer()),
        socket_factory_(ss_.get()) {
    lport_ = CreateTestPort(kLocalAddr1, "lfrag", "lpass", kTiebreaker1);
    rport_ = CreateTestPort(kLocalAddr2, "rfrag", "rpass", kTiebreaker2);
    lport_->SetIceRole(ICEROLE_CONTROLLING);
    rport_->SetIceRole(ICEROLE_CONTROLLED);

    lport_->PrepareAddress();
    rport_->PrepareAddress();
  }

  Connection* CreateConnection(IceRole role) {
    Connection* conn;
    if (role == ICEROLE_CONTROLLING) {
      conn = lport_->CreateConnection(rport_->Candidates()[0],
                                      Port::ORIGIN_MESSAGE);
    } else {
      conn = rport_->CreateConnection(lport_->Candidates()[0],
                                      Port::ORIGIN_MESSAGE);
    }
    conn->SubscribeStateChange(this, [this](Connection* connection) {
      OnConnectionStateChange(connection);
    });
    return conn;
  }

  void SendPingAndCaptureReply(Connection* lconn,
                               Connection* rconn,
                               int64_t ms,
                               Buffer* reply) {
    TestPort* lport =
        lconn->PortForTest() == lport_.get() ? lport_.get() : rport_.get();
    TestPort* rport =
        rconn->PortForTest() == rport_.get() ? rport_.get() : lport_.get();
    lconn->Ping();
    ASSERT_TRUE(WaitUntil([&] { return lport->last_stun_msg(); },
                          {.timeout = TimeDelta::Millis(kDefaultTimeout),
                           .clock = &time_controller_}));
    ASSERT_GT(lport->last_stun_buf().size(), 0u);
    rconn->OnReadPacket(ReceivedIpPacket(lport->last_stun_buf(),
                                         SocketAddress(), std::nullopt));

    time_controller_.AdvanceTime(TimeDelta::Millis(ms));
    ASSERT_TRUE(WaitUntil([&] { return rport->last_stun_msg(); },
                          {.timeout = TimeDelta::Millis(kDefaultTimeout),
                           .clock = &time_controller_}));
    ASSERT_GT(rport->last_stun_buf().size(), 0u);
    reply->SetData(rport->last_stun_buf());
  }

  void SendPingAndReceiveResponse(Connection* lconn,
                                  Connection* rconn,
                                  int64_t ms) {
    Buffer reply;
    SendPingAndCaptureReply(lconn, rconn, ms, &reply);

    lconn->OnReadPacket(ReceivedIpPacket(reply, SocketAddress(), std::nullopt));
  }

  void OnConnectionStateChange(Connection* connection) { num_state_changes_++; }

  Network* MakeNetwork(const SocketAddress& addr) {
    networks_.emplace_back("unittest", "unittest", addr.ipaddr(), 32);
    networks_.back().AddIP(addr.ipaddr());
    return &networks_.back();
  }

  std::unique_ptr<TestPort> CreateTestPort(const SocketAddress& addr,
                                           absl::string_view username,
                                           absl::string_view password,
                                           uint64_t tiebreaker) {
    Port::PortParametersRef args = {
        .env = env_,
        .network_thread = time_controller_.GetMainThread(),
        .socket_factory = &socket_factory_,
        .network = MakeNetwork(addr),
        .ice_username_fragment = username,
        .ice_password = password,
        .ice_tiebreaker = tiebreaker};
    auto port = std::make_unique<TestPort>(args, 0, 0);
    port->SubscribeRoleConflict([this]() { OnRoleConflict(); });
    return port;
  }

  void OnRoleConflict() { role_conflict_ = true; }
  const Environment& env() const { return env_; }

  GlobalSimulatedTimeController time_controller_{Timestamp::Zero()};
  const Environment env_ = CreateTestEnvironment({.time = &time_controller_});
  int num_state_changes_ = 0;
  std::unique_ptr<VirtualSocketServer> ss_;
  BasicPacketSocketFactory socket_factory_;
  std::list<Network> networks_;
  bool role_conflict_ = false;

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

  Buffer reply;
  SendPingAndCaptureReply(lconn, rconn, 10, &reply);

  lconn->ForgetLearnedState();

  EXPECT_FALSE(lconn->writable());
  EXPECT_FALSE(lconn->receiving());

  lconn->OnReadPacket(ReceivedIpPacket(reply, SocketAddress(), std::nullopt));

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
      [&](RTCErrorOr<const StunUInt64Attribute*> error_or_ack) {
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
      [](RTCErrorOr<const StunUInt64Attribute*> error_or__ack) {});

  lconn->Ping(env().clock().CurrentTime(), std::move(delta));
  ASSERT_TRUE(WaitUntil([&] { return lport_->last_stun_msg(); },
                        {.timeout = TimeDelta::Millis(kDefaultTimeout),
                         .clock = &time_controller_}));
  ASSERT_GT(lport_->last_stun_buf().size(), 0u);
  rconn->OnReadPacket(
      ReceivedIpPacket(lport_->last_stun_buf(), SocketAddress(), std::nullopt));
  EXPECT_TRUE(received_goog_delta);

  time_controller_.SkipForwardBy(TimeDelta::Millis(ms));
  ASSERT_TRUE(WaitUntil([&] { return rport_->last_stun_msg(); },
                        {.timeout = TimeDelta::Millis(kDefaultTimeout),
                         .clock = &time_controller_}));
  ASSERT_GT(rport_->last_stun_buf().size(), 0u);
  lconn->OnReadPacket(
      ReceivedIpPacket(rport_->last_stun_buf(), SocketAddress(), std::nullopt));
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
      [&](RTCErrorOr<const StunUInt64Attribute*> error_or_ack) {
        received_goog_delta_ack_error = true;
        EXPECT_FALSE(error_or_ack.ok());
      });

  lconn->Ping(env().clock().CurrentTime(), std::move(delta));
  ASSERT_TRUE(WaitUntil([&] { return lport_->last_stun_msg(); },
                        {.timeout = TimeDelta::Millis(kDefaultTimeout),
                         .clock = &time_controller_}));
  ASSERT_GT(lport_->last_stun_buf().size(), 0u);
  rconn->OnReadPacket(
      ReceivedIpPacket(lport_->last_stun_buf(), SocketAddress(), std::nullopt));

  time_controller_.SkipForwardBy(TimeDelta::Millis(ms));
  ASSERT_TRUE(WaitUntil([&] { return rport_->last_stun_msg(); },
                        {.timeout = TimeDelta::Millis(kDefaultTimeout),
                         .clock = &time_controller_}));
  ASSERT_GT(rport_->last_stun_buf().size(), 0u);
  lconn->OnReadPacket(
      ReceivedIpPacket(rport_->last_stun_buf(), SocketAddress(), std::nullopt));
  EXPECT_TRUE(received_goog_delta_ack_error);
}

// Test that if StunBinding is sufficiently full, aka DELTA is too big, it is
// not sent.
TEST_F(ConnectionTest, TooBigDeltaIsNotSent) {
  constexpr int64_t ms = 10;
  Connection* lconn = CreateConnection(ICEROLE_CONTROLLING);
  Connection* rconn = CreateConnection(ICEROLE_CONTROLLED);

  std::string a_long_string(1200, 'a');
  std::unique_ptr<StunByteStringAttribute> delta =
      absl::WrapUnique(new StunByteStringAttribute(STUN_ATTR_GOOG_DELTA));
  delta->CopyBytes(a_long_string);

  std::unique_ptr<StunAttribute> delta_ack =
      absl::WrapUnique(new StunUInt64Attribute(STUN_ATTR_GOOG_DELTA_ACK, 133));

  bool received_goog_delta = false;
  bool received_goog_delta_ack = false;
  lconn->SetStunDictConsumer(
      // DeltaReceived
      [](const StunByteStringAttribute* delta)
          -> std::unique_ptr<StunAttribute> { return nullptr; },
      // DeltaAckReceived
      [&](RTCErrorOr<const StunUInt64Attribute*> error_or_ack) {
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
      [](RTCErrorOr<const StunUInt64Attribute*> error_or__ack) {});

  lconn->Ping(env().clock().CurrentTime(), std::move(delta));
  ASSERT_TRUE(WaitUntil([&] { return lport_->last_stun_msg(); },
                        {.timeout = TimeDelta::Millis(kDefaultTimeout),
                         .clock = &time_controller_}));
  ASSERT_GT(lport_->last_stun_buf().size(), 0u);
  rconn->OnReadPacket(
      ReceivedIpPacket(lport_->last_stun_buf(), SocketAddress(), std::nullopt));
  EXPECT_FALSE(received_goog_delta);

  time_controller_.SkipForwardBy(TimeDelta::Millis(ms));
  ASSERT_TRUE(WaitUntil([&] { return rport_->last_stun_msg(); },
                        {.timeout = TimeDelta::Millis(kDefaultTimeout),
                         .clock = &time_controller_}));
  ASSERT_GT(rport_->last_stun_buf().size(), 0u);
  lconn->OnReadPacket(
      ReceivedIpPacket(rport_->last_stun_buf(), SocketAddress(), std::nullopt));

  EXPECT_FALSE(received_goog_delta_ack);
}

using DtlsStunPiggybackConnectionTest = ConnectionTest;

TEST_F(DtlsStunPiggybackConnectionTest, SpedAttributesExecuteCallbacks) {
  int rconn_callback_count = 0;
  std::optional<size_t> rconn_data_size;
  std::optional<size_t> rconn_ack_size;
  int lconn_callback_count = 0;
  std::optional<size_t> lconn_data_size;
  std::optional<size_t> lconn_ack_size;

  const absl::string_view kRequestData = "request";
  std::vector<uint32_t> acks;
  acks.push_back(0);
  // lconn sends embedded data and acks.
  Connection* lconn = CreateConnection(ICEROLE_CONTROLLING);
  lconn->RegisterDtlsPiggyback(DtlsStunPiggybackCallbacks(
      /*send_data=*/
      [&](StunMessageType type)
          -> std::pair<std::optional<absl::string_view>,
                       std::optional<std::vector<uint32_t>>> {
        return {kRequestData, {acks}};
      },
      /*recv_data=*/
      [&](std::optional<std::span<uint8_t>> data,
          std::optional<std::vector<uint32_t>> ack) {
        ++lconn_callback_count;
        if (data.has_value()) {
          lconn_data_size = data->size();
        }
        if (ack.has_value()) {
          lconn_ack_size = ack->size();
        }
      }));
  // rconn does not send embedded data and acks.
  Connection* rconn = CreateConnection(ICEROLE_CONTROLLED);
  rconn->RegisterDtlsPiggyback(DtlsStunPiggybackCallbacks(
      /*send_data=*/
      [](StunMessageType type)
          -> std::pair<std::optional<absl::string_view>,
                       std::optional<std::vector<uint32_t>>> { return {}; },
      /*recv_data=*/
      [&](std::optional<std::span<uint8_t>> data,
          std::optional<std::vector<uint32_t>> ack) {
        ++rconn_callback_count;
        if (data.has_value()) {
          rconn_data_size = data->size();
        }
        if (ack.has_value()) {
          rconn_ack_size = ack->size();
        }
      }));

  Buffer reply;
  SendPingAndCaptureReply(lconn, rconn, env().clock().CurrentTime().ms(),
                          &reply);
  lconn->OnReadPacket(ReceivedIpPacket(reply, SocketAddress(), std::nullopt));

  // rconn received data and acks from lconn.
  EXPECT_EQ(rconn_callback_count, 1);
  EXPECT_EQ(rconn_data_size, kRequestData.size());
  EXPECT_EQ(rconn_ack_size, acks.size());
  // lconn received no data and no acks from rconn.
  EXPECT_EQ(lconn_callback_count, 1);
  EXPECT_EQ(lconn_data_size, std::nullopt);
  EXPECT_EQ(lconn_ack_size, std::nullopt);
}

}  // namespace
}  // namespace webrtc
