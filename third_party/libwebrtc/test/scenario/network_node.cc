/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "test/scenario/network_node.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "api/call/transport.h"
#include "api/sequence_checker.h"
#include "api/task_queue/task_queue_base.h"
#include "api/test/network_emulation/network_emulation_interfaces.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "call/call.h"
#include "rtc_base/checks.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/event.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/net_helpers.h"
#include "rtc_base/network/sent_packet.h"
#include "rtc_base/network_constants.h"
#include "rtc_base/network_route.h"
#include "rtc_base/socket_address.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/task_queue_for_test.h"
#include "system_wrappers/include/clock.h"
#include "test/network/network_emulation.h"
#include "test/network/simulated_network.h"
#include "test/scenario/column_printer.h"
#include "test/scenario/scenario_config.h"

namespace webrtc {
namespace test {
namespace {
constexpr char kDummyTransportName[] = "dummy";
SimulatedNetwork::Config CreateSimulationConfig(
    NetworkSimulationConfig config) {
  SimulatedNetwork::Config sim_config;
  sim_config.link_capacity = config.bandwidth;
  sim_config.loss_percent = config.loss_rate * 100;
  sim_config.queue_delay_ms = config.delay.ms();
  sim_config.delay_standard_deviation_ms = config.delay_std_dev.ms();
  sim_config.packet_overhead = config.packet_overhead.bytes<int>();
  sim_config.queue_length_packets =
      config.packet_queue_length_limit.value_or(0);
  return sim_config;
}

RouteEndpoint CreateRouteEndpoint(uint16_t network_id, uint16_t adapter_id) {
  return RouteEndpoint(ADAPTER_TYPE_UNKNOWN, adapter_id, network_id,
                       /* uses_turn = */ false);
}
}  // namespace

SimulationNode::SimulationNode(NetworkSimulationConfig config,
                               SimulatedNetwork* behavior,
                               EmulatedNetworkNode* network_node)
    : config_(config), simulation_(behavior), network_node_(network_node) {}

std::unique_ptr<SimulatedNetwork> SimulationNode::CreateBehavior(
    NetworkSimulationConfig config) {
  SimulatedNetwork::Config sim_config = CreateSimulationConfig(config);
  return std::make_unique<SimulatedNetwork>(sim_config);
}

void SimulationNode::UpdateConfig(
    std::function<void(NetworkSimulationConfig*)> modifier) {
  modifier(&config_);
  SimulatedNetwork::Config sim_config = CreateSimulationConfig(config_);
  simulation_->SetConfig(sim_config);
}

void SimulationNode::PauseTransmissionUntil(Timestamp until) {
  simulation_->PauseTransmissionUntil(until.us());
}

ColumnPrinter SimulationNode::ConfigPrinter() const {
  return ColumnPrinter::Lambda(
      "propagation_delay capacity loss_rate", [this](StringBuilder& sb) {
        sb.AppendFormat("%.3lf %.0lf %.2lf", config_.delay.seconds<double>(),
                        config_.bandwidth.bps() / 8.0, config_.loss_rate);
      });
}

NetworkNodeTransport::NetworkNodeTransport(Clock* sender_clock,
                                           Call* sender_call)
    : sender_clock_(sender_clock), sender_call_(sender_call) {
  sequence_checker_.Detach();
}

NetworkNodeTransport::~NetworkNodeTransport() = default;

bool NetworkNodeTransport::SendRtp(std::span<const uint8_t> packet,
                                   const PacketOptions& options) {
  int64_t send_time_ms = sender_clock_->TimeInMilliseconds();
  SentPacketInfo sent_packet;
  sent_packet.packet_id = options.packet_id;
  sent_packet.info.included_in_feedback = options.included_in_feedback;
  sent_packet.info.included_in_allocation = options.included_in_allocation;
  sent_packet.send_time_ms = send_time_ms;
  sent_packet.info.packet_size_bytes = packet.size();
  sent_packet.info.packet_type = PacketType::kData;
  TaskQueueBase* network_thread = sender_call_->network_thread();
  SendTask(network_thread, [&]() { sender_call_->OnSentPacket(sent_packet); });

  MutexLock lock(&mutex_);
  if (!endpoint_)
    return false;
  CopyOnWriteBuffer buffer(packet);
  endpoint_->SendPacket(local_address_, remote_address_, buffer,
                        packet_overhead_.bytes());
  return true;
}

bool NetworkNodeTransport::SendRtcp(std::span<const uint8_t> packet,
                                    const PacketOptions& options) {
  CopyOnWriteBuffer buffer(packet);
  MutexLock lock(&mutex_);
  if (!endpoint_)
    return false;
  endpoint_->SendPacket(local_address_, remote_address_, buffer,
                        packet_overhead_.bytes());
  return true;
}

void NetworkNodeTransport::UpdateAdapterId(int adapter_id) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  adapter_id_ = adapter_id;
}

void NetworkNodeTransport::Connect(EmulatedEndpoint* endpoint,
                                   const SocketAddress& receiver_address,
                                   DataSize packet_overhead) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  NetworkRoute route;
  route.connected = true;
  // We assume that the address will be unique in the lower bytes.
  route.local = CreateRouteEndpoint(
      static_cast<uint16_t>(
          receiver_address.ipaddr().v4AddressAsHostOrderInteger()),
      adapter_id_);
  route.remote = CreateRouteEndpoint(
      static_cast<uint16_t>(
          receiver_address.ipaddr().v4AddressAsHostOrderInteger()),
      adapter_id_);
  route.packet_overhead = packet_overhead.bytes() +
                          receiver_address.ipaddr().overhead() + kUdpHeaderSize;
  {
    // Only IPv4 address is supported.
    RTC_CHECK_EQ(receiver_address.family(), AF_INET);
    MutexLock lock(&mutex_);
    endpoint_ = endpoint;
    local_address_ = SocketAddress(endpoint_->GetPeerLocalAddress(), 0);
    remote_address_ = receiver_address;
    packet_overhead_ = packet_overhead;
    current_network_route_ = route;
  }

  // Must be called from the worker thread.
  Event event;
  auto cleanup = absl::MakeCleanup([&event] { event.Set(); });
  auto&& task = [this, &route, cleanup = std::move(cleanup)] {
    sender_call_->GetTransportControllerSend()->OnNetworkRouteChanged(
        kDummyTransportName, route);
  };
  if (!sender_call_->network_thread()->IsCurrent()) {
    sender_call_->network_thread()->PostTask(std::move(task));
  } else {
    std::move(task)();
  }
  event.Wait(TimeDelta::Seconds(1));
}

void NetworkNodeTransport::Disconnect() {
  MutexLock lock(&mutex_);
  current_network_route_.connected = false;

  sender_call_->GetTransportControllerSend()->OnNetworkRouteChanged(
      kDummyTransportName, current_network_route_);
  current_network_route_ = {};
  endpoint_ = nullptr;
}

}  // namespace test
}  // namespace webrtc
