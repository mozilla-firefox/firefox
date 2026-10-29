/*
 *  Copyright 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "test/network/network_emulation.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "api/task_queue/task_queue_base.h"
#include "api/test/network_emulation/network_emulation_interfaces.h"
#include "api/test/network_emulation_manager.h"
#include "api/test/rtc_error_matchers.h"
#include "api/test/simulated_network.h"
#include "api/transport/ecn_marking.h"
#include "api/transport/stun.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "rtc_base/buffer.h"
#include "rtc_base/byte_buffer.h"
#include "rtc_base/checks.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/net_helpers.h"
#include "rtc_base/socket.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/task_queue_for_test.h"
#include "rtc_base/thread.h"
#include "rtc_base/thread_annotations.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/network/network_emulation_manager.h"
#include "test/network/simulated_network.h"
#include "test/wait_until.h"

namespace webrtc {
namespace test {
namespace {

using ::testing::ElementsAreArray;
using ::testing::Eq;

constexpr TimeDelta kNetworkPacketWaitTimeout = TimeDelta::Millis(100);
constexpr TimeDelta kStatsWaitTimeout = TimeDelta::Seconds(1);
constexpr int kOverheadIpv4Udp = 20 + 8;

class SocketReader {
 public:
  explicit SocketReader(Socket* socket, Thread* network_thread)
      : socket_(socket), network_thread_(network_thread) {
    socket_->SubscribeReadEvent(
        this, [this](Socket* socket) { OnReadEvent(socket); });
  }

  void OnReadEvent(Socket* socket) {
    RTC_DCHECK(socket_ == socket);
    RTC_DCHECK(network_thread_->IsCurrent());

    Socket::ReceiveBuffer receive_buffer(payload_);
    socket_->RecvFrom(receive_buffer);
    last_ecn_mark_ = receive_buffer.ecn;

    MutexLock lock(&lock_);
    received_count_++;
  }

  int ReceivedCount() const {
    MutexLock lock(&lock_);
    return received_count_;
  }

  EcnMarking LastEcnMarking() const {
    MutexLock lock(&lock_);
    return last_ecn_mark_;
  }

 private:
  Socket* const socket_;
  Thread* const network_thread_;
  Buffer payload_;
  EcnMarking last_ecn_mark_;

  mutable Mutex lock_;
  int received_count_ RTC_GUARDED_BY(lock_) = 0;
};

class MockReceiver : public EmulatedNetworkReceiverInterface {
 public:
  MOCK_METHOD(void, OnPacketReceived, (EmulatedIpPacket packet), (override));
};

class MockNetworkBehaviourInterface : public NetworkBehaviorInterface {
 public:
  MOCK_METHOD(bool, EnqueuePacket, (PacketInFlightInfo), (override));
  MOCK_METHOD(std::vector<PacketDeliveryInfo>,
              DequeueDeliverablePackets,
              (int64_t),
              (override));
  MOCK_METHOD(std::optional<int64_t>,
              NextDeliveryTimeUs,
              (),
              (const, override));
  MOCK_METHOD(void,
              RegisterDeliveryTimeChangedCallback,
              (absl::AnyInvocable<void()>),
              (override));
};

class NetworkEmulationManagerThreeNodesRoutingTest : public ::testing::Test {
 public:
  NetworkEmulationManagerThreeNodesRoutingTest() {
    e1_ = emulation_.CreateEndpoint(EmulatedEndpointConfig());
    e2_ = emulation_.CreateEndpoint(EmulatedEndpointConfig());
    e3_ = emulation_.CreateEndpoint(EmulatedEndpointConfig());
  }

  void SetupRouting(
      std::function<void(EmulatedEndpoint*,
                         EmulatedEndpoint*,
                         EmulatedEndpoint*,
                         NetworkEmulationManager*)> create_routing_func) {
    create_routing_func(e1_, e2_, e3_, &emulation_);
  }

  void SendPacketsAndValidateDelivery() {
    EXPECT_CALL(r_e1_e2_, OnPacketReceived(::testing::_)).Times(1);
    EXPECT_CALL(r_e2_e1_, OnPacketReceived(::testing::_)).Times(1);
    EXPECT_CALL(r_e1_e3_, OnPacketReceived(::testing::_)).Times(1);
    EXPECT_CALL(r_e3_e1_, OnPacketReceived(::testing::_)).Times(1);

    uint16_t common_send_port = 80;
    uint16_t r_e1_e2_port = e2_->BindReceiver(0, &r_e1_e2_).value();
    uint16_t r_e2_e1_port = e1_->BindReceiver(0, &r_e2_e1_).value();
    uint16_t r_e1_e3_port = e3_->BindReceiver(0, &r_e1_e3_).value();
    uint16_t r_e3_e1_port = e1_->BindReceiver(0, &r_e3_e1_).value();

    // Next code is using API of EmulatedEndpoint, that is visible only for
    // internals of network emulation layer. Don't use this API in other tests.
    // Send packet from e1 to e2.
    e1_->SendPacket(SocketAddress(e1_->GetPeerLocalAddress(), common_send_port),
                    SocketAddress(e2_->GetPeerLocalAddress(), r_e1_e2_port),
                    CopyOnWriteBuffer(10));

    // Send packet from e2 to e1.
    e2_->SendPacket(SocketAddress(e2_->GetPeerLocalAddress(), common_send_port),
                    SocketAddress(e1_->GetPeerLocalAddress(), r_e2_e1_port),
                    CopyOnWriteBuffer(10));

    // Send packet from e1 to e3.
    e1_->SendPacket(SocketAddress(e1_->GetPeerLocalAddress(), common_send_port),
                    SocketAddress(e3_->GetPeerLocalAddress(), r_e1_e3_port),
                    CopyOnWriteBuffer(10));

    // Send packet from e3 to e1.
    e3_->SendPacket(SocketAddress(e3_->GetPeerLocalAddress(), common_send_port),
                    SocketAddress(e1_->GetPeerLocalAddress(), r_e3_e1_port),
                    CopyOnWriteBuffer(10));

    // Sleep at the end to wait for async packets delivery.
    emulation_.time_controller()->AdvanceTime(kNetworkPacketWaitTimeout);
  }

 private:
  // Receivers: r_<source endpoint>_<destination endpoint>
  // They must be destroyed after emulation, so they should be declared before.
  MockReceiver r_e1_e2_;
  MockReceiver r_e2_e1_;
  MockReceiver r_e1_e3_;
  MockReceiver r_e3_e1_;

  NetworkEmulationManagerImpl emulation_{
      NetworkEmulationManagerConfig{.time_mode = TimeMode::kRealTime}};
  EmulatedEndpoint* e1_;
  EmulatedEndpoint* e2_;
  EmulatedEndpoint* e3_;
};

EmulatedNetworkNode* CreateEmulatedNodeWithDefaultBuiltInConfig(
    NetworkEmulationManager* emulation) {
  return emulation->CreateEmulatedNode(
      std::make_unique<SimulatedNetwork>(BuiltInNetworkBehaviorConfig()));
}

}  // namespace

TEST(NetworkEmulationManagerTest, GeneratedIpv4AddressDoesNotCollide) {
  NetworkEmulationManagerImpl network_manager(
      {.time_mode = TimeMode::kRealTime});
  std::set<IPAddress> ips;
  EmulatedEndpointConfig config;
  config.generated_ip_family = EmulatedEndpointConfig::IpAddressFamily::kIpv4;
  for (int i = 0; i < 1000; i++) {
    EmulatedEndpoint* endpoint = network_manager.CreateEndpoint(config);
    ASSERT_EQ(endpoint->GetPeerLocalAddress().family(), AF_INET);
    bool result = ips.insert(endpoint->GetPeerLocalAddress()).second;
    ASSERT_TRUE(result);
  }
}

TEST(NetworkEmulationManagerTest, GeneratedIpv6AddressDoesNotCollide) {
  NetworkEmulationManagerImpl network_manager(
      {.time_mode = TimeMode::kRealTime});
  std::set<IPAddress> ips;
  EmulatedEndpointConfig config;
  config.generated_ip_family = EmulatedEndpointConfig::IpAddressFamily::kIpv6;
  for (int i = 0; i < 1000; i++) {
    EmulatedEndpoint* endpoint = network_manager.CreateEndpoint(config);
    ASSERT_EQ(endpoint->GetPeerLocalAddress().family(), AF_INET6);
    bool result = ips.insert(endpoint->GetPeerLocalAddress()).second;
    ASSERT_TRUE(result);
  }
}

TEST(NetworkEmulationManagerTest, Run) {
  NetworkEmulationManagerImpl network_manager(
      {.time_mode = TimeMode::kRealTime});

  EmulatedNetworkNode* alice_node = network_manager.CreateEmulatedNode(
      std::make_unique<SimulatedNetwork>(BuiltInNetworkBehaviorConfig()));
  EmulatedNetworkNode* bob_node = network_manager.CreateEmulatedNode(
      std::make_unique<SimulatedNetwork>(BuiltInNetworkBehaviorConfig()));
  EmulatedEndpoint* alice_endpoint =
      network_manager.CreateEndpoint(EmulatedEndpointConfig());
  EmulatedEndpoint* bob_endpoint =
      network_manager.CreateEndpoint(EmulatedEndpointConfig());
  network_manager.CreateRoute(alice_endpoint, {alice_node}, bob_endpoint);
  network_manager.CreateRoute(bob_endpoint, {bob_node}, alice_endpoint);

  EmulatedNetworkManagerInterface* nt1 =
      network_manager.CreateEmulatedNetworkManagerInterface({alice_endpoint});
  EmulatedNetworkManagerInterface* nt2 =
      network_manager.CreateEmulatedNetworkManagerInterface({bob_endpoint});

  Thread* t1 = nt1->network_thread();
  Thread* t2 = nt2->network_thread();

  CopyOnWriteBuffer data("Hello");
  for (uint64_t j = 0; j < 2; j++) {
    Socket* s1 = nullptr;
    Socket* s2 = nullptr;
    SendTask(t1, [&] {
      s1 = t1->socketserver()->CreateSocket(AF_INET, SOCK_DGRAM);
    });
    SendTask(t2, [&] {
      s2 = t2->socketserver()->CreateSocket(AF_INET, SOCK_DGRAM);
    });

    SocketReader r1(s1, t1);
    SocketReader r2(s2, t2);

    SocketAddress a1(alice_endpoint->GetPeerLocalAddress(), 0);
    SocketAddress a2(bob_endpoint->GetPeerLocalAddress(), 0);

    SendTask(t1, [&] {
      s1->Bind(a1);
      a1 = s1->GetLocalAddress();
    });
    SendTask(t2, [&] {
      s2->Bind(a2);
      a2 = s2->GetLocalAddress();
    });

    SendTask(t1, [&] { s1->Connect(a2); });
    SendTask(t2, [&] { s2->Connect(a1); });

    for (uint64_t i = 0; i < 1000; i++) {
      t1->PostTask([&]() { s1->Send(data.data(), data.size()); });
      t2->PostTask([&]() { s2->Send(data.data(), data.size()); });
    }

    network_manager.time_controller()->AdvanceTime(TimeDelta::Seconds(1));

    EXPECT_EQ(r1.ReceivedCount(), 1000);
    EXPECT_EQ(r2.ReceivedCount(), 1000);

    SendTask(t1, [&] { delete s1; });
    SendTask(t2, [&] { delete s2; });
  }

  const int64_t single_packet_size = data.size() + kOverheadIpv4Udp;
  std::atomic<int> received_stats_count{0};
  nt1->GetStats([&](EmulatedNetworkStats st) {
    EXPECT_EQ(st.PacketsSent(), 2000l);
    EXPECT_EQ(st.BytesSent().bytes(), single_packet_size * 2000l);
    EXPECT_THAT(st.local_addresses,
                ElementsAreArray({alice_endpoint->GetPeerLocalAddress()}));
    EXPECT_EQ(st.PacketsReceived(), 2000l);
    EXPECT_EQ(st.BytesReceived().bytes(), single_packet_size * 2000l);
    EXPECT_EQ(st.PacketsDiscardedNoReceiver(), 0l);
    EXPECT_EQ(st.BytesDiscardedNoReceiver().bytes(), 0l);

    IPAddress bob_ip = bob_endpoint->GetPeerLocalAddress();
    std::map<IPAddress, EmulatedNetworkIncomingStats> source_st =
        st.incoming_stats_per_source;
    ASSERT_EQ(source_st.size(), 1lu);
    EXPECT_EQ(source_st.at(bob_ip).packets_received, 2000l);
    EXPECT_EQ(source_st.at(bob_ip).bytes_received.bytes(),
              single_packet_size * 2000l);
    EXPECT_EQ(source_st.at(bob_ip).packets_discarded_no_receiver, 0l);
    EXPECT_EQ(source_st.at(bob_ip).bytes_discarded_no_receiver.bytes(), 0l);

    std::map<IPAddress, EmulatedNetworkOutgoingStats> dest_st =
        st.outgoing_stats_per_destination;
    ASSERT_EQ(dest_st.size(), 1lu);
    EXPECT_EQ(dest_st.at(bob_ip).packets_sent, 2000l);
    EXPECT_EQ(dest_st.at(bob_ip).bytes_sent.bytes(),
              single_packet_size * 2000l);

    // No debug stats are collected by default.
    EXPECT_TRUE(st.SentPacketsSizeCounter().IsEmpty());
    EXPECT_TRUE(st.sent_packets_queue_wait_time_us.IsEmpty());
    EXPECT_TRUE(st.ReceivedPacketsSizeCounter().IsEmpty());
    EXPECT_TRUE(st.PacketsDiscardedNoReceiverSizeCounter().IsEmpty());
    EXPECT_TRUE(dest_st.at(bob_ip).sent_packets_size.IsEmpty());
    EXPECT_TRUE(source_st.at(bob_ip).received_packets_size.IsEmpty());
    EXPECT_TRUE(
        source_st.at(bob_ip).packets_discarded_no_receiver_size.IsEmpty());

    received_stats_count++;
  });
  nt2->GetStats([&](EmulatedNetworkStats st) {
    EXPECT_EQ(st.PacketsSent(), 2000l);
    EXPECT_EQ(st.BytesSent().bytes(), single_packet_size * 2000l);
    EXPECT_THAT(st.local_addresses,
                ElementsAreArray({bob_endpoint->GetPeerLocalAddress()}));
    EXPECT_EQ(st.PacketsReceived(), 2000l);
    EXPECT_EQ(st.BytesReceived().bytes(), single_packet_size * 2000l);
    EXPECT_EQ(st.PacketsDiscardedNoReceiver(), 0l);
    EXPECT_EQ(st.BytesDiscardedNoReceiver().bytes(), 0l);
    EXPECT_GT(st.FirstReceivedPacketSize(), DataSize::Zero());
    EXPECT_TRUE(st.FirstPacketReceivedTime().IsFinite());
    EXPECT_TRUE(st.LastPacketReceivedTime().IsFinite());

    IPAddress alice_ip = alice_endpoint->GetPeerLocalAddress();
    std::map<IPAddress, EmulatedNetworkIncomingStats> source_st =
        st.incoming_stats_per_source;
    ASSERT_EQ(source_st.size(), 1lu);
    EXPECT_EQ(source_st.at(alice_ip).packets_received, 2000l);
    EXPECT_EQ(source_st.at(alice_ip).bytes_received.bytes(),
              single_packet_size * 2000l);
    EXPECT_EQ(source_st.at(alice_ip).packets_discarded_no_receiver, 0l);
    EXPECT_EQ(source_st.at(alice_ip).bytes_discarded_no_receiver.bytes(), 0l);

    std::map<IPAddress, EmulatedNetworkOutgoingStats> dest_st =
        st.outgoing_stats_per_destination;
    ASSERT_EQ(dest_st.size(), 1lu);
    EXPECT_EQ(dest_st.at(alice_ip).packets_sent, 2000l);
    EXPECT_EQ(dest_st.at(alice_ip).bytes_sent.bytes(),
              single_packet_size * 2000l);

    // No debug stats are collected by default.
    EXPECT_TRUE(st.SentPacketsSizeCounter().IsEmpty());
    EXPECT_TRUE(st.sent_packets_queue_wait_time_us.IsEmpty());
    EXPECT_TRUE(st.ReceivedPacketsSizeCounter().IsEmpty());
    EXPECT_TRUE(st.PacketsDiscardedNoReceiverSizeCounter().IsEmpty());
    EXPECT_TRUE(dest_st.at(alice_ip).sent_packets_size.IsEmpty());
    EXPECT_TRUE(source_st.at(alice_ip).received_packets_size.IsEmpty());
    EXPECT_TRUE(
        source_st.at(alice_ip).packets_discarded_no_receiver_size.IsEmpty());

    received_stats_count++;
  });
  ASSERT_THAT(WaitUntil([&] { return received_stats_count.load(); }, Eq(2),
                        {.timeout = kStatsWaitTimeout,
                         .clock = network_manager.time_controller()}),
              IsRtcOk());
}

TEST(NetworkEmulationManagerTest, EcnMarkingIsPropagated) {
  NetworkEmulationManagerImpl network_manager(
      {.time_mode = TimeMode::kRealTime});

  EmulatedNetworkNode* alice_node = network_manager.CreateEmulatedNode(
      std::make_unique<SimulatedNetwork>(BuiltInNetworkBehaviorConfig()));
  EmulatedNetworkNode* bob_node = network_manager.CreateEmulatedNode(
      std::make_unique<SimulatedNetwork>(BuiltInNetworkBehaviorConfig()));
  EmulatedEndpoint* alice_endpoint =
      network_manager.CreateEndpoint(EmulatedEndpointConfig());
  EmulatedEndpoint* bob_endpoint =
      network_manager.CreateEndpoint(EmulatedEndpointConfig());
  network_manager.CreateRoute(alice_endpoint, {alice_node}, bob_endpoint);
  network_manager.CreateRoute(bob_endpoint, {bob_node}, alice_endpoint);

  EmulatedNetworkManagerInterface* nt1 =
      network_manager.CreateEmulatedNetworkManagerInterface({alice_endpoint});
  EmulatedNetworkManagerInterface* nt2 =
      network_manager.CreateEmulatedNetworkManagerInterface({bob_endpoint});

  Thread* t1 = nt1->network_thread();
  Thread* t2 = nt2->network_thread();

  Socket* s1 = nullptr;
  Socket* s2 = nullptr;
  SendTask(t1,
           [&] { s1 = t1->socketserver()->CreateSocket(AF_INET, SOCK_DGRAM); });
  SendTask(t2,
           [&] { s2 = t2->socketserver()->CreateSocket(AF_INET, SOCK_DGRAM); });

  SocketReader r1(s1, t1);
  SocketReader r2(s2, t2);

  SocketAddress a1(alice_endpoint->GetPeerLocalAddress(), 0);
  SocketAddress a2(bob_endpoint->GetPeerLocalAddress(), 0);

  SendTask(t1, [&] {
    s1->Bind(a1);
    a1 = s1->GetLocalAddress();
  });
  SendTask(t2, [&] {
    s2->Bind(a2);
    a2 = s2->GetLocalAddress();
  });

  SendTask(t1, [&] { s1->Connect(a2); });
  SendTask(t2, [&] { s2->Connect(a1); });

  t1->PostTask([&]() {
    s1->SetOption(Socket::Option::OPT_SEND_ECN, 1);
    CopyOnWriteBuffer data("Hello");
    s1->Send(data.data(), data.size());
  });

  network_manager.time_controller()->AdvanceTime(TimeDelta::Seconds(1));

  EXPECT_EQ(r2.ReceivedCount(), 1);
  EXPECT_EQ(r2.LastEcnMarking(), EcnMarking::kEct1);

  std::atomic<int> received_stats_count{0};
  nt1->GetStats([&](EmulatedNetworkStats st) {
    EXPECT_EQ(st.overall_incoming_stats.packets_received, 0);
    EXPECT_EQ(st.overall_outgoing_stats.packets_sent, 1);
    EXPECT_EQ(st.overall_outgoing_stats.ecn_count.ect_1(), 1);
    EXPECT_EQ(st.overall_outgoing_stats.ecn_count.ce(), 0);
    EXPECT_EQ(st.overall_outgoing_stats.ecn_count.not_ect(), 0);
    ++received_stats_count;
  });
  nt2->GetStats([&](EmulatedNetworkStats st) {
    EXPECT_EQ(st.overall_incoming_stats.packets_received, 1);
    EXPECT_EQ(st.overall_outgoing_stats.packets_sent, 0);
    EXPECT_EQ(st.overall_incoming_stats.ecn_count.ect_1(), 1);
    EXPECT_EQ(st.overall_incoming_stats.ecn_count.ce(), 0);
    EXPECT_EQ(st.overall_incoming_stats.ecn_count.not_ect(), 0);
    ++received_stats_count;
  });
  ASSERT_THAT(WaitUntil([&] { return received_stats_count.load(); }, Eq(2),
                        {.timeout = kStatsWaitTimeout,
                         .clock = network_manager.time_controller()}),
              IsRtcOk());

  SendTask(t1, [&] { delete s1; });
  SendTask(t2, [&] { delete s2; });
}

TEST(NetworkEmulationManagerTest, DebugStatsCollectedInDebugMode) {
  NetworkEmulationManagerImpl network_manager(
      {.time_mode = TimeMode::kSimulated,
       .stats_gathering_mode = EmulatedNetworkStatsGatheringMode::kDebug});

  EmulatedNetworkNode* alice_node = network_manager.CreateEmulatedNode(
      std::make_unique<SimulatedNetwork>(BuiltInNetworkBehaviorConfig()));
  EmulatedNetworkNode* bob_node = network_manager.CreateEmulatedNode(
      std::make_unique<SimulatedNetwork>(BuiltInNetworkBehaviorConfig()));
  EmulatedEndpoint* alice_endpoint =
      network_manager.CreateEndpoint(EmulatedEndpointConfig());
  EmulatedEndpoint* bob_endpoint =
      network_manager.CreateEndpoint(EmulatedEndpointConfig());
  network_manager.CreateRoute(alice_endpoint, {alice_node}, bob_endpoint);
  network_manager.CreateRoute(bob_endpoint, {bob_node}, alice_endpoint);

  EmulatedNetworkManagerInterface* nt1 =
      network_manager.CreateEmulatedNetworkManagerInterface({alice_endpoint});
  EmulatedNetworkManagerInterface* nt2 =
      network_manager.CreateEmulatedNetworkManagerInterface({bob_endpoint});

  Thread* t1 = nt1->network_thread();
  Thread* t2 = nt2->network_thread();

  CopyOnWriteBuffer data("Hello");
  for (uint64_t j = 0; j < 2; j++) {
    Socket* s1 = nullptr;
    Socket* s2 = nullptr;
    SendTask(t1, [&] {
      s1 = t1->socketserver()->CreateSocket(AF_INET, SOCK_DGRAM);
    });
    SendTask(t2, [&] {
      s2 = t2->socketserver()->CreateSocket(AF_INET, SOCK_DGRAM);
    });

    SocketReader r1(s1, t1);
    SocketReader r2(s2, t2);

    SocketAddress a1(alice_endpoint->GetPeerLocalAddress(), 0);
    SocketAddress a2(bob_endpoint->GetPeerLocalAddress(), 0);

    SendTask(t1, [&] {
      s1->Bind(a1);
      a1 = s1->GetLocalAddress();
    });
    SendTask(t2, [&] {
      s2->Bind(a2);
      a2 = s2->GetLocalAddress();
    });

    SendTask(t1, [&] { s1->Connect(a2); });
    SendTask(t2, [&] { s2->Connect(a1); });

    for (uint64_t i = 0; i < 1000; i++) {
      t1->PostTask([&]() { s1->Send(data.data(), data.size()); });
      t2->PostTask([&]() { s2->Send(data.data(), data.size()); });
    }

    network_manager.time_controller()->AdvanceTime(TimeDelta::Seconds(1));

    EXPECT_EQ(r1.ReceivedCount(), 1000);
    EXPECT_EQ(r2.ReceivedCount(), 1000);

    SendTask(t1, [&] { delete s1; });
    SendTask(t2, [&] { delete s2; });
  }

  const int64_t single_packet_size = data.size() + kOverheadIpv4Udp;
  std::atomic<int> received_stats_count{0};
  nt1->GetStats([&](EmulatedNetworkStats st) {
    IPAddress bob_ip = bob_endpoint->GetPeerLocalAddress();
    std::map<IPAddress, EmulatedNetworkIncomingStats> source_st =
        st.incoming_stats_per_source;
    ASSERT_EQ(source_st.size(), 1lu);

    std::map<IPAddress, EmulatedNetworkOutgoingStats> dest_st =
        st.outgoing_stats_per_destination;
    ASSERT_EQ(dest_st.size(), 1lu);

    // No debug stats are collected by default.
    EXPECT_EQ(st.SentPacketsSizeCounter().NumSamples(), 2000l);
    EXPECT_EQ(st.ReceivedPacketsSizeCounter().GetAverage(), single_packet_size);
    EXPECT_EQ(st.sent_packets_queue_wait_time_us.NumSamples(), 2000l);
    EXPECT_LT(st.sent_packets_queue_wait_time_us.GetMax(), 1);
    EXPECT_TRUE(st.PacketsDiscardedNoReceiverSizeCounter().IsEmpty());
    EXPECT_EQ(dest_st.at(bob_ip).sent_packets_size.NumSamples(), 2000l);
    EXPECT_EQ(dest_st.at(bob_ip).sent_packets_size.GetAverage(),
              single_packet_size);
    EXPECT_EQ(source_st.at(bob_ip).received_packets_size.NumSamples(), 2000l);
    EXPECT_EQ(source_st.at(bob_ip).received_packets_size.GetAverage(),
              single_packet_size);
    EXPECT_TRUE(
        source_st.at(bob_ip).packets_discarded_no_receiver_size.IsEmpty());

    received_stats_count++;
  });
  ASSERT_THAT(WaitUntil([&] { return received_stats_count.load(); }, Eq(1),
                        {.timeout = kStatsWaitTimeout,
                         .clock = network_manager.time_controller()}),
              IsRtcOk());
}

TEST(NetworkEmulationManagerTest, ThroughputStats) {
  NetworkEmulationManagerImpl network_manager(
      {.time_mode = TimeMode::kRealTime});

  EmulatedNetworkNode* alice_node = network_manager.CreateEmulatedNode(
      std::make_unique<SimulatedNetwork>(BuiltInNetworkBehaviorConfig()));
  EmulatedNetworkNode* bob_node = network_manager.CreateEmulatedNode(
      std::make_unique<SimulatedNetwork>(BuiltInNetworkBehaviorConfig()));
  EmulatedEndpoint* alice_endpoint =
      network_manager.CreateEndpoint(EmulatedEndpointConfig());
  EmulatedEndpoint* bob_endpoint =
      network_manager.CreateEndpoint(EmulatedEndpointConfig());
  network_manager.CreateRoute(alice_endpoint, {alice_node}, bob_endpoint);
  network_manager.CreateRoute(bob_endpoint, {bob_node}, alice_endpoint);

  EmulatedNetworkManagerInterface* nt1 =
      network_manager.CreateEmulatedNetworkManagerInterface({alice_endpoint});
  EmulatedNetworkManagerInterface* nt2 =
      network_manager.CreateEmulatedNetworkManagerInterface({bob_endpoint});

  Thread* t1 = nt1->network_thread();
  Thread* t2 = nt2->network_thread();

  constexpr int64_t kUdpPayloadSize = 100;
  constexpr int64_t kSinglePacketSize = kUdpPayloadSize + kOverheadIpv4Udp;
  CopyOnWriteBuffer data(kUdpPayloadSize);

  Socket* s1 = nullptr;
  Socket* s2 = nullptr;
  SendTask(t1,
           [&] { s1 = t1->socketserver()->CreateSocket(AF_INET, SOCK_DGRAM); });
  SendTask(t2,
           [&] { s2 = t2->socketserver()->CreateSocket(AF_INET, SOCK_DGRAM); });

  SocketReader r1(s1, t1);
  SocketReader r2(s2, t2);

  SocketAddress a1(alice_endpoint->GetPeerLocalAddress(), 0);
  SocketAddress a2(bob_endpoint->GetPeerLocalAddress(), 0);

  SendTask(t1, [&] {
    s1->Bind(a1);
    a1 = s1->GetLocalAddress();
  });
  SendTask(t2, [&] {
    s2->Bind(a2);
    a2 = s2->GetLocalAddress();
  });

  SendTask(t1, [&] { s1->Connect(a2); });
  SendTask(t2, [&] { s2->Connect(a1); });

  // Send 11 packets, totalizing 1 second between the first and the last->
  const int kNumPacketsSent = 11;
  const TimeDelta kDelay = TimeDelta::Millis(100);
  for (int i = 0; i < kNumPacketsSent; i++) {
    t1->PostTask([&]() { s1->Send(data.data(), data.size()); });
    t2->PostTask([&]() { s2->Send(data.data(), data.size()); });
    network_manager.time_controller()->AdvanceTime(kDelay);
  }

  std::atomic<int> received_stats_count{0};
  nt1->GetStats([&](EmulatedNetworkStats st) {
    EXPECT_EQ(st.PacketsSent(), kNumPacketsSent);
    EXPECT_EQ(st.BytesSent().bytes(), kSinglePacketSize * kNumPacketsSent);

    const double tolerance = 0.95;  // Accept 5% tolerance for timing.
    EXPECT_GE(st.LastPacketSentTime() - st.FirstPacketSentTime(),
              (kNumPacketsSent - 1) * kDelay * tolerance);
    EXPECT_GT(st.AverageSendRate().bps(), 0);
    received_stats_count++;
  });

  ASSERT_THAT(WaitUntil([&] { return received_stats_count.load(); }, Eq(1),
                        {.timeout = kStatsWaitTimeout,
                         .clock = network_manager.time_controller()}),
              IsRtcOk());

  EXPECT_EQ(r1.ReceivedCount(), 11);
  EXPECT_EQ(r2.ReceivedCount(), 11);

  SendTask(t1, [&] { delete s1; });
  SendTask(t2, [&] { delete s2; });
}

// Testing that packets are delivered via all routes using a routing scheme as
// follows:
//  * e1 -> n1 -> e2
//  * e2 -> n2 -> e1
//  * e1 -> n3 -> e3
//  * e3 -> n4 -> e1
TEST_F(NetworkEmulationManagerThreeNodesRoutingTest,
       PacketsAreDeliveredInBothWaysWhenConnectedToTwoPeers) {
  SetupRouting([](EmulatedEndpoint* e1, EmulatedEndpoint* e2,
                  EmulatedEndpoint* e3, NetworkEmulationManager* emulation) {
    auto* node1 = CreateEmulatedNodeWithDefaultBuiltInConfig(emulation);
    auto* node2 = CreateEmulatedNodeWithDefaultBuiltInConfig(emulation);
    auto* node3 = CreateEmulatedNodeWithDefaultBuiltInConfig(emulation);
    auto* node4 = CreateEmulatedNodeWithDefaultBuiltInConfig(emulation);

    emulation->CreateRoute(e1, {node1}, e2);
    emulation->CreateRoute(e2, {node2}, e1);

    emulation->CreateRoute(e1, {node3}, e3);
    emulation->CreateRoute(e3, {node4}, e1);
  });
  SendPacketsAndValidateDelivery();
}

// Testing that packets are delivered via all routes using a routing scheme as
// follows:
//  * e1 -> n1 -> e2
//  * e2 -> n2 -> e1
//  * e1 -> n1 -> e3
//  * e3 -> n4 -> e1
TEST_F(NetworkEmulationManagerThreeNodesRoutingTest,
       PacketsAreDeliveredInBothWaysWhenConnectedToTwoPeersOverSameSendLink) {
  SetupRouting([](EmulatedEndpoint* e1, EmulatedEndpoint* e2,
                  EmulatedEndpoint* e3, NetworkEmulationManager* emulation) {
    auto* node1 = CreateEmulatedNodeWithDefaultBuiltInConfig(emulation);
    auto* node2 = CreateEmulatedNodeWithDefaultBuiltInConfig(emulation);
    auto* node3 = CreateEmulatedNodeWithDefaultBuiltInConfig(emulation);

    emulation->CreateRoute(e1, {node1}, e2);
    emulation->CreateRoute(e2, {node2}, e1);

    emulation->CreateRoute(e1, {node1}, e3);
    emulation->CreateRoute(e3, {node3}, e1);
  });
  SendPacketsAndValidateDelivery();
}

TEST(NetworkEmulationManagerTest, EndpointLoopback) {
  NetworkEmulationManagerImpl network_manager(
      {.time_mode = TimeMode::kSimulated});
  auto endpoint = network_manager.CreateEndpoint(EmulatedEndpointConfig());

  MockReceiver receiver;
  EXPECT_CALL(receiver, OnPacketReceived(::testing::_)).Times(1);
  ASSERT_EQ(endpoint->BindReceiver(80, &receiver), 80);

  endpoint->SendPacket(SocketAddress(endpoint->GetPeerLocalAddress(), 80),
                       SocketAddress(endpoint->GetPeerLocalAddress(), 80),
                       CopyOnWriteBuffer("Hello"));
  network_manager.time_controller()->AdvanceTime(TimeDelta::Seconds(1));
}

TEST(NetworkEmulationManagerTest, EndpointCanSendWithDifferentSourceIp) {
  constexpr uint32_t kEndpointIp = 0xC0A80011;  // 192.168.0.17
  constexpr uint32_t kSourceIp = 0xC0A80012;    // 192.168.0.18
  NetworkEmulationManagerImpl network_manager(
      {.time_mode = TimeMode::kSimulated});
  EmulatedEndpointConfig endpoint_config;
  endpoint_config.ip = IPAddress(kEndpointIp);
  endpoint_config.allow_send_packet_with_different_source_ip = true;
  auto endpoint = network_manager.CreateEndpoint(endpoint_config);

  MockReceiver receiver;
  EXPECT_CALL(receiver, OnPacketReceived(::testing::_)).Times(1);
  ASSERT_EQ(endpoint->BindReceiver(80, &receiver), 80);

  endpoint->SendPacket(SocketAddress(kSourceIp, 80),
                       SocketAddress(endpoint->GetPeerLocalAddress(), 80),
                       CopyOnWriteBuffer("Hello"));
  network_manager.time_controller()->AdvanceTime(TimeDelta::Seconds(1));
}

TEST(NetworkEmulationManagerTest,
     EndpointCanReceiveWithDifferentDestIpThroughDefaultRoute) {
  constexpr uint32_t kDestEndpointIp = 0xC0A80011;  // 192.168.0.17
  constexpr uint32_t kDestIp = 0xC0A80012;          // 192.168.0.18
  NetworkEmulationManagerImpl network_manager(
      {.time_mode = TimeMode::kSimulated});
  auto sender_endpoint =
      network_manager.CreateEndpoint(EmulatedEndpointConfig());
  EmulatedEndpointConfig endpoint_config;
  endpoint_config.ip = IPAddress(kDestEndpointIp);
  endpoint_config.allow_receive_packets_with_different_dest_ip = true;
  auto receiver_endpoint = network_manager.CreateEndpoint(endpoint_config);

  MockReceiver receiver;
  EXPECT_CALL(receiver, OnPacketReceived(::testing::_)).Times(1);
  ASSERT_EQ(receiver_endpoint->BindReceiver(80, &receiver), 80);

  network_manager.CreateDefaultRoute(
      sender_endpoint, {network_manager.NodeBuilder().Build().node},
      receiver_endpoint);

  sender_endpoint->SendPacket(
      SocketAddress(sender_endpoint->GetPeerLocalAddress(), 80),
      SocketAddress(kDestIp, 80), CopyOnWriteBuffer("Hello"));
  network_manager.time_controller()->AdvanceTime(TimeDelta::Seconds(1));
}

TEST(NetworkEmulationManagerTURNTest, GetIceServerConfig) {
  NetworkEmulationManagerImpl network_manager(
      {.time_mode = TimeMode::kRealTime});
  auto turn = network_manager.CreateTURNServer(EmulatedTURNServerConfig());

  EXPECT_GT(turn->GetIceServerConfig().username.size(), 0u);
  EXPECT_GT(turn->GetIceServerConfig().password.size(), 0u);
  EXPECT_NE(turn->GetIceServerConfig().url.find(
                turn->GetClientEndpoint()->GetPeerLocalAddress().ToString()),
            std::string::npos);
}

TEST(NetworkEmulationManagerTURNTest, ClientTraffic) {
  NetworkEmulationManagerImpl emulation({.time_mode = TimeMode::kSimulated});
  auto* ep = emulation.CreateEndpoint(EmulatedEndpointConfig());
  auto* turn = emulation.CreateTURNServer(EmulatedTURNServerConfig());
  auto* node = CreateEmulatedNodeWithDefaultBuiltInConfig(&emulation);
  emulation.CreateRoute(ep, {node}, turn->GetClientEndpoint());
  emulation.CreateRoute(turn->GetClientEndpoint(), {node}, ep);

  MockReceiver recv;
  int port = ep->BindReceiver(0, &recv).value();

  // Construct a STUN BINDING.
  StunMessage ping(STUN_BINDING_REQUEST);
  ByteBufferWriter buf;
  ping.Write(&buf);
  CopyOnWriteBuffer packet(buf.DataView());

  // We expect to get a ping reply.
  EXPECT_CALL(recv, OnPacketReceived(::testing::_)).Times(1);

  ep->SendPacket(SocketAddress(ep->GetPeerLocalAddress(), port),
                 turn->GetClientEndpointAddress(), packet);
  emulation.time_controller()->AdvanceTime(TimeDelta::Seconds(1));
}

TEST(LinkEmulationTest, HandlesDeliveryTimeChangedCallback) {
  constexpr uint32_t kEndpointIp = 0xC0A80011;  // 192.168.0.17
  NetworkEmulationManagerImpl network_manager(
      {.time_mode = TimeMode::kSimulated});
  auto mock_behaviour =
      std::make_unique<::testing::NiceMock<MockNetworkBehaviourInterface>>();
  MockNetworkBehaviourInterface* mock_behaviour_ptr = mock_behaviour.get();
  absl::AnyInvocable<void()> delivery_time_changed_callback = nullptr;
  TaskQueueBase* emulation_task_queue = nullptr;
  EXPECT_CALL(*mock_behaviour_ptr, RegisterDeliveryTimeChangedCallback)
      .WillOnce([&](absl::AnyInvocable<void()> callback) {
        delivery_time_changed_callback = std::move(callback);
        emulation_task_queue = TaskQueueBase::Current();
      });
  LinkEmulation* link =
      network_manager.CreateEmulatedNode(std::move(mock_behaviour))->link();
  network_manager.time_controller()->AdvanceTime(TimeDelta::Zero());
  ASSERT_TRUE(delivery_time_changed_callback);

  EXPECT_CALL(*mock_behaviour_ptr, EnqueuePacket);
  EXPECT_CALL(*mock_behaviour_ptr, NextDeliveryTimeUs)
      .WillOnce(::testing::Return(
          network_manager.time_controller()->GetClock()->TimeInMicroseconds() +
          10));
  link->OnPacketReceived(EmulatedIpPacket(
      SocketAddress(kEndpointIp, 50), SocketAddress(kEndpointIp, 79),
      CopyOnWriteBuffer(10), Timestamp::Millis(1)));
  network_manager.time_controller()->AdvanceTime(TimeDelta::Zero());

  // Test that NetworkBehaviour can reschedule time for delivery. When
  // delivery_time_changed_callback is triggered, LinkEmulation re-query the
  // next delivery time.
  EXPECT_CALL(*mock_behaviour_ptr, NextDeliveryTimeUs)
      .WillOnce(::testing::Return(
          network_manager.time_controller()->GetClock()->TimeInMicroseconds() +
          20));
  emulation_task_queue->PostTask([&]() { delivery_time_changed_callback(); });
  network_manager.time_controller()->AdvanceTime(TimeDelta::Zero());
}

}  // namespace test
}  // namespace webrtc
