/*
 *  Copyright 2025 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#ifndef API_TEST_NETWORK_EMULATION_DUAL_PI2_NETWORK_QUEUE_H_
#define API_TEST_NETWORK_EMULATION_DUAL_PI2_NETWORK_QUEUE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <queue>
#include <vector>

#include "api/sequence_checker.h"
#include "api/test/network_emulation/network_queue.h"
#include "api/test/simulated_network.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "rtc_base/random.h"

namespace webrtc {

// DualPi2NetworkQueue is a simplified version of the DualPi2 AQM controller in
// https://github.com/L4STeam/linux/. Concepts are described in
// https://datatracker.ietf.org/doc/html/rfc9332.
// Developed for testing purposes.
class DualPi2NetworkQueue : public NetworkQueue {
 public:
  struct Config {
    // Target delay for the coupled PI2 controller (RFC 9332 default is 15ms).
    // Governs the classic queue delay target and L4S overload protection.
    TimeDelta target_delay = TimeDelta::Millis(15);
    // Step threshold for the native L4S AQM. If the sojourn time of a packet in
    // the L4S queue exceeds this value on dequeue (while standing backlog >=
    // `min_backlog_packets`), it will be CE marked.
    // RFC 9332 / Linux sch_dualpi2 defaults to 1ms for broadband links (4-200
    // Mbps), but notes (RFC 9332 Section A.2) that on lower-capacity links the
    // threshold should be at least 1-2 MTU serialization time so packets are
    // not prematurely marked while waiting for a single packet in flight on the
    // wire. A 10ms default corresponds to ~1 MTU (1200B) serialization time at
    // ~1 Mbps, making these defaults suitable for link capacities >= ~500 kbps
    // while keeping L4S queuing delay below `target_delay` (15ms) so the slow
    // PI2 controller stays at zero under normal L4S load.
    TimeDelta step_threshold = TimeDelta::Millis(10);
    // Minimum standing queue backlog (in packets) required before taking
    // marking or dropping actions.
    size_t min_backlog_packets = 2;

    // These constants are used to calculate the proportional and integral
    // factors when updating the marking probability.
    // Values are from the original implementation.
    double alpha = 0.16;
    double beta = 3.2;
    // Coupling factor.
    int k = 2;

    // Classic queue protection weight percentage (0-100) for the WRR credit
    // scheduler (c_protection in Linux sch_dualpi2). Default is 10%, meaning
    // 10% weight for the classic queue and 90% weight for the L4S queue.
    int c_protection = 10;

    // How often the base marking probability is updated.
    TimeDelta probability_update_interval = TimeDelta::Millis(16);
    int seed = 1;
  };

  DualPi2NetworkQueue() : DualPi2NetworkQueue(Config()) {}
  explicit DualPi2NetworkQueue(const Config& config);

  void SetMaxPacketCapacity(size_t max_packet_capacity) override;
  bool EnqueuePacket(const PacketInFlightInfo& packet_info) override;

  std::optional<PacketInFlightInfo> PeekNextPacket() const override;
  std::optional<PacketInFlightInfo> DequeuePacket(Timestamp time_now) override;
  std::vector<PacketInFlightInfo> DequeueDroppedPackets() override {
    // DualPi2 always tail drop packets.
    return {};
  }
  bool empty() const override;

  // Returns the marking probability of the L4S the l4s queue. Public for
  // testing.
  double l4s_marking_probability() const {
    return base_marking_probability_ * config_.k;
  }
  // Returns the drop probability of the classic queue. Public for
  // testing.
  double classic_drop_probability() const {
    return (base_marking_probability_ * base_marking_probability_);
  }

 private:
  void UpdateBaseMarkingProbability(Timestamp time_now);
  TimeDelta l4s_queue_delay(Timestamp time_now) const;
  TimeDelta classic_queue_delay(Timestamp time_now) const;
  bool HasStandingQueueBacklog() const;
  bool ShouldDequeueFromClassicQueue() const;
  void ResetCProtectionCredit();

  SequenceChecker sequence_checker_;

  const Config config_;

  std::queue<PacketInFlightInfo> l4s_queue_;
  std::queue<PacketInFlightInfo> classic_queue_;

  Random random_;

  std::optional<size_t> max_packet_capacity_;
  int64_t c_protection_credit_ = 0;
  double base_marking_probability_ = 0;
  Timestamp last_probability_update_time_ = Timestamp::MinusInfinity();
  // The delay of the queue after the last probability update.
  TimeDelta previous_sojourn_time_ = TimeDelta::Zero();
};

class DualPi2NetworkQueueFactory : public NetworkQueueFactory {
 public:
  explicit DualPi2NetworkQueueFactory(
      const DualPi2NetworkQueue::Config& config = DualPi2NetworkQueue::Config())
      : config_(config) {}

  std::unique_ptr<NetworkQueue> CreateQueue() override {
    return std::make_unique<DualPi2NetworkQueue>(config_);
  }

 private:
  const DualPi2NetworkQueue::Config config_;
};
}  // namespace webrtc

#endif  // API_TEST_NETWORK_EMULATION_DUAL_PI2_NETWORK_QUEUE_H_
