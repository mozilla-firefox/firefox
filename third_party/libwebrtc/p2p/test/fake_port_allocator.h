/*
 *  Copyright 2010 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef P2P_TEST_FAKE_PORT_ALLOCATOR_H_
#define P2P_TEST_FAKE_PORT_ALLOCATOR_H_

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/functional/any_invocable.h"
#include "absl/memory/memory.h"
#include "absl/strings/string_view.h"
#include "api/candidate.h"
#include "api/environment/environment.h"
#include "api/packet_socket_factory.h"
#include "api/task_queue/task_queue_base.h"
#include "p2p/base/basic_packet_socket_factory.h"
#include "p2p/base/port.h"
#include "p2p/base/port_allocator.h"
#include "p2p/base/port_interface.h"
#include "p2p/base/stun_port.h"
#include "rtc_base/async_packet_socket.h"
#include "rtc_base/checks.h"
#include "rtc_base/ip_address.h"
#include "rtc_base/net_helpers.h"
#include "rtc_base/net_test_helpers.h"
#include "rtc_base/network.h"
#include "rtc_base/socket_factory.h"
#include "rtc_base/task_queue_for_test.h"
#include "rtc_base/weak_ptr.h"

namespace webrtc {

class TestUDPPort : public UDPPort {
 public:
  static TestUDPPort* Create(PortParametersRef args,
                             uint16_t min_port,
                             uint16_t max_port,
                             bool emit_localhost_for_anyaddress) {
    TestUDPPort* port = new TestUDPPort(std::move(args), min_port, max_port,
                                        emit_localhost_for_anyaddress);
    if (!port->Init()) {
      delete port;
      port = nullptr;
    }
    return port;
  }

  static std::unique_ptr<TestUDPPort> Create(
      PortParametersRef args,
      AsyncPacketSocket* socket,
      bool emit_localhost_for_anyaddress) {
    auto port = absl::WrapUnique(new TestUDPPort(
        std::move(args), socket, emit_localhost_for_anyaddress));
    if (!port->Init()) {
      return nullptr;
    }
    return port;
  }

 protected:
  TestUDPPort(PortParametersRef args,
              uint16_t min_port,
              uint16_t max_port,
              bool emit_localhost_for_anyaddress)
      : UDPPort(std::move(args),
                IceCandidateType::kHost,
                min_port,
                max_port,
                emit_localhost_for_anyaddress) {}

  TestUDPPort(PortParametersRef args,
              AsyncPacketSocket* socket,
              bool emit_localhost_for_anyaddress)
      : UDPPort(std::move(args),
                IceCandidateType::kHost,
                socket,
                emit_localhost_for_anyaddress) {}
};

// A FakePortAllocatorSession can be used with either a real or fake socket
// factory. It gathers a single loopback port, using IPv6 if available and
// not disabled.
class FakePortAllocatorSession : public PortAllocatorSession {
 public:
  FakePortAllocatorSession(const Environment& env,
                           PortAllocator* allocator,
                           TaskQueueBase* network_thread,
                           PacketSocketFactory* factory,
                           absl::string_view content_name,
                           int component,
                           absl::string_view ice_ufrag,
                           absl::string_view ice_pwd)
      : PortAllocatorSession(content_name,
                             component,
                             ice_ufrag,
                             ice_pwd,
                             allocator->flags()),
        env_(env),
        allocator_(allocator),
        network_thread_(network_thread),
        factory_(factory),
        ipv4_network_("network", "unittest", GetLoopbackIP(AF_INET), 32),
        ipv6_network_("network", "unittest", GetLoopbackIP(AF_INET6), 64),
        port_(),
        port_config_count_(0),
        stun_servers_(allocator->stun_servers()),
        turn_servers_(allocator->turn_servers()) {
    ipv4_network_.AddIP(GetLoopbackIP(AF_INET));
    ipv6_network_.AddIP(GetLoopbackIP(AF_INET6));
  }

  void SetCandidateFilter(uint32_t filter) override {
    candidate_filter_ = filter;
  }

  void StartGettingPorts() override {
    if (!port_) {
      Network& network =
          (HasIPv6Enabled() && (flags() & PORTALLOCATOR_ENABLE_IPV6))
              ? ipv6_network_
              : ipv4_network_;
      Port::Callbacks callbacks;
      callbacks.tag = this;
      callbacks.port_complete = [this](Port* port) { OnPortComplete(port); };
      callbacks.port_destroyed = [this](PortInterface* port) {
        OnPortDestroyed(port);
      };
      port_.reset(
          TestUDPPort::Create({.env = env_,
                               .network_thread = network_thread_,
                               .socket_factory = factory_,
                               .network = &network,
                               .ice_username_fragment = username(),
                               .ice_password = password(),
                               .content_name = content_name(),
                               .component = component(),
                               .generation = generation(),
                               .ice_tiebreaker = allocator_->ice_tiebreaker(),
                               .callbacks = std::move(callbacks)},
                              0, 0, false));
      RTC_DCHECK(port_ != nullptr);
      AddPort(port_.get());
    }
    ++port_config_count_;
    running_ = true;
  }

  void StopGettingPorts() override { running_ = false; }
  bool IsGettingPorts() override { return running_; }
  void ClearGettingPorts() override { is_cleared = true; }
  bool IsCleared() const override { return is_cleared; }

  void RegatherOnFailedNetworks() override {
    NotifyIceRegathering(this, IceRegatheringReason::NETWORK_FAILURE);
  }

  std::vector<PortInterface*> ReadyPorts() const override {
    return ready_ports_;
  }
  std::vector<Candidate> ReadyCandidates() const override {
    return candidates_;
  }
  void PruneAllPorts() override { port_->Prune(); }
  bool CandidatesAllocationDone() const override { return allocation_done_; }

  int port_config_count() { return port_config_count_; }

  const ServerAddresses& stun_servers() const { return stun_servers_; }

  const std::vector<RelayServerConfig>& turn_servers() const {
    return turn_servers_;
  }

  uint32_t candidate_filter() const { return candidate_filter_; }

  int transport_info_update_count() const {
    return transport_info_update_count_;
  }

 protected:
  void UpdateIceParametersInternal() override {
    // Since this class is a fake and this method only is overridden for tests,
    // we don't need to actually update the transport info.
    ++transport_info_update_count_;
  }

 private:
  void AddPort(Port* port) {
    port->PrepareAddress();
    ready_ports_.push_back(port);
    NotifyPortReady(this, port);
    port->KeepAliveUntilPruned();
  }
  void OnPortComplete(Port* port) {
    const std::vector<Candidate>& candidates = port->Candidates();
    candidates_.insert(candidates_.end(), candidates.begin(), candidates.end());
    NotifyCandidatesReady(this, candidates);

    allocation_done_ = true;
    NotifyCandidatesAllocationDone(this);
  }
  void OnPortDestroyed(PortInterface* /* port */) {
    // Don't want to double-delete port if it deletes itself.
    port_.release();
  }

  const Environment env_;
  PortAllocator* const allocator_;
  TaskQueueBase* const network_thread_;
  PacketSocketFactory* const factory_;
  Network ipv4_network_;
  Network ipv6_network_;
  std::unique_ptr<Port> port_;
  int port_config_count_;
  std::vector<Candidate> candidates_;
  std::vector<PortInterface*> ready_ports_;
  bool allocation_done_ = false;
  bool is_cleared = false;
  const ServerAddresses stun_servers_;
  const std::vector<RelayServerConfig> turn_servers_;
  uint32_t candidate_filter_ = CF_ALL;
  int transport_info_update_count_ = 0;
  bool running_ = false;
};

class FakePortAllocator : public PortAllocator {
 public:
  FakePortAllocator(
      const Environment& env,
      SocketFactory* absl_nonnull socket_factory,
      TaskQueueBase* absl_nonnull network_thread = TaskQueueBase::Current())
      : env_(env), network_thread_(network_thread), factory_(socket_factory) {
    RTC_CHECK(network_thread);
    SendTask(network_thread_, [this] { Initialize(); });
  }

  void SetNetworkIgnoreMask(int /* network_ignore_mask */) override {}

  PortAllocatorSession* CreateSessionInternal(
      absl::string_view content_name,
      int component,
      absl::string_view ice_ufrag,
      absl::string_view ice_pwd) override {
    return new FakePortAllocatorSession(env_, this, network_thread_, &factory_,
                                        content_name, component, ice_ufrag,
                                        ice_pwd);
  }

  bool initialized() const { return initialized_; }

  // For testing: Manipulate MdnsObfuscationEnabled()
  bool MdnsObfuscationEnabled() const override {
    return mdns_obfuscation_enabled_;
  }

  void SetMdnsObfuscationEnabledForTesting(bool enabled) {
    mdns_obfuscation_enabled_ = enabled;
  }

  void DiscardCandidatePool() override {
    PortAllocator::DiscardCandidatePool();
    if (on_candidate_pool_discarded_) {
      on_candidate_pool_discarded_();
    }
  }

  void SetOnDiscardCandidatePool(
      absl::AnyInvocable<void()> on_candidate_pool_discarded) {
    on_candidate_pool_discarded_ = std::move(on_candidate_pool_discarded);
  }

  WeakPtr<FakePortAllocator> NewWeakPtr() { return weak_factory_.GetWeakPtr(); }

 private:
  const Environment env_;
  TaskQueueBase* const absl_nonnull network_thread_;
  BasicPacketSocketFactory factory_;
  bool mdns_obfuscation_enabled_ = false;
  absl::AnyInvocable<void()> on_candidate_pool_discarded_;
  WeakPtrFactory<FakePortAllocator> weak_factory_{this};
};

}  //  namespace webrtc


#endif  // P2P_TEST_FAKE_PORT_ALLOCATOR_H_
