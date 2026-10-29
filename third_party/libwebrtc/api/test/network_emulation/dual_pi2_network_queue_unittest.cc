/*
 *  Copyright 2025 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "api/test/network_emulation/dual_pi2_network_queue.h"

#include <cstdint>
#include <limits>
#include <optional>

#include "api/test/simulated_network.h"
#include "api/transport/ecn_marking.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {

namespace {

using ::testing::AnyOf;
using ::testing::Field;
using ::testing::Optional;
using ::testing::Property;

constexpr DataSize kPacketSize = DataSize::Bytes(1000);

TEST(DualPi2NetworkQueueTest, EnqueuePacket) {
  DualPi2NetworkQueue queue;
  Timestamp send_time = Timestamp::Seconds(123);
  PacketInFlightInfo packet_info(kPacketSize, send_time, /*packet_id=*/1,
                                 EcnMarking::kNotEct);
  EXPECT_TRUE(queue.EnqueuePacket(packet_info));
}

TEST(DualPi2NetworkQueueTest, PeekNextPacketReturnsNulloptWhenEmpty) {
  DualPi2NetworkQueue queue;
  EXPECT_EQ(queue.PeekNextPacket(), std::nullopt);
}

TEST(DualPi2NetworkQueueTest, PeekNextPacketPrioritizeL4SQueue) {
  DualPi2NetworkQueue queue;
  Timestamp send_time = Timestamp::Seconds(123);
  PacketInFlightInfo packet_info_classic(kPacketSize, send_time,
                                         /*packet_id=*/1, EcnMarking::kNotEct);
  queue.EnqueuePacket(packet_info_classic);
  PacketInFlightInfo packet_info_l4s_1(kPacketSize, send_time,
                                       /*packet_id=*/2, EcnMarking::kEct1);
  queue.EnqueuePacket(packet_info_l4s_1);
  PacketInFlightInfo packet_info_l4s_2(kPacketSize, send_time,
                                       /*packet_id=*/3, EcnMarking::kEct1);
  queue.EnqueuePacket(packet_info_l4s_2);
  std::optional<PacketInFlightInfo> peeked_packet = queue.PeekNextPacket();
  ASSERT_TRUE(peeked_packet.has_value());
  EXPECT_EQ(peeked_packet.value().packet_id, 2u);
}

TEST(DualPi2NetworkQueueTest, DequeuePacketReturnsNulloptWhenEmpty) {
  DualPi2NetworkQueue queue;
  EXPECT_EQ(queue.DequeuePacket(Timestamp::Seconds(123)), std::nullopt);
}

TEST(DualPi2NetworkQueueTest, DequeuePacketPrioritizeL4SQueue) {
  DualPi2NetworkQueue queue;
  Timestamp send_time = Timestamp::Seconds(123);
  PacketInFlightInfo packet_info_classic(kPacketSize, send_time,
                                         /*packet_id=*/1, EcnMarking::kNotEct);
  queue.EnqueuePacket(packet_info_classic);
  PacketInFlightInfo packet_info_l4s_1(kPacketSize, send_time,
                                       /*packet_id=*/2, EcnMarking::kEct1);
  queue.EnqueuePacket(packet_info_l4s_1);
  PacketInFlightInfo packet_info_l4s_2(kPacketSize, send_time,
                                       /*packet_id=*/3, EcnMarking::kEct1);
  queue.EnqueuePacket(packet_info_l4s_2);
  Timestamp dequeue_time = Timestamp::Seconds(123);
  EXPECT_THAT(
      queue.DequeuePacket(dequeue_time),
      Optional(AllOf(Field(&PacketInFlightInfo::packet_id, 2),
                     Field(&PacketInFlightInfo::ecn, EcnMarking::kEct1),
                     Property(&PacketInFlightInfo::send_time, send_time))));
  EXPECT_THAT(
      queue.DequeuePacket(dequeue_time),
      Optional(AllOf(Field(&PacketInFlightInfo::packet_id, 3),
                     Field(&PacketInFlightInfo::ecn, EcnMarking::kEct1),
                     Property(&PacketInFlightInfo::send_time, send_time))));
  EXPECT_THAT(
      queue.DequeuePacket(dequeue_time),
      Optional(AllOf(Field(&PacketInFlightInfo::packet_id, 1),
                     Field(&PacketInFlightInfo::ecn, EcnMarking::kNotEct),
                     Property(&PacketInFlightInfo::send_time, send_time))));
}

TEST(DualPi2NetworkQueueTest,
     CeMarkingProbabilityIncreaseIfSojournTimeTooHigh) {
  DualPi2NetworkQueue queue;

  double marking_probability = 0;
  Timestamp now = Timestamp::Seconds(123);

  for (int i = 0; i < 4; ++i) {
    queue.EnqueuePacket(PacketInFlightInfo(kPacketSize, now,
                                           /*packet_id=*/i, EcnMarking::kEct1));
    // Dequeue 1 packet after 17ms, 1ms more than the probability update
    // interval and more than the target delay.
    now += TimeDelta::Millis(17);
    ASSERT_THAT(queue.DequeuePacket(now),
                Optional(Field(&PacketInFlightInfo::packet_id, i)));
    EXPECT_GT(queue.l4s_marking_probability(), marking_probability);
    marking_probability = queue.l4s_marking_probability();
    EXPECT_GT(marking_probability, 0);
    EXPECT_LE(marking_probability, std::numeric_limits<uint32_t>::max());
  }
}

TEST(DualPi2NetworkQueueTest,
     CeMarkingProbabilityIncreaseIfSojournTimeTooHighForClassicTraffic) {
  DualPi2NetworkQueue queue;

  double marking_probability = 0;
  Timestamp now = Timestamp::Seconds(123);

  for (int i = 0; i < 4; ++i) {
    queue.EnqueuePacket(PacketInFlightInfo(kPacketSize, now,
                                           /*packet_id=*/i, EcnMarking::kEct0));
    // Dequeue 1 packet after 17ms, 1ms more than the probability update
    // interval and more than the target delay.
    now += TimeDelta::Millis(17);
    ASSERT_THAT(queue.DequeuePacket(now),
                Optional(Field(&PacketInFlightInfo::packet_id, i)));
    EXPECT_GT(queue.l4s_marking_probability(), marking_probability);
    marking_probability = queue.l4s_marking_probability();
    EXPECT_GT(marking_probability, 0);
    EXPECT_LE(marking_probability, std::numeric_limits<uint32_t>::max());
  }
}

TEST(DualPi2NetworkQueueTest,
     CeMarkingProbabilityDontIncreaseIfSojournTimeEqualToTarget) {
  DualPi2NetworkQueue queue;
  Timestamp now = Timestamp::Seconds(123);
  int i = 0;
  double marking_probability_at_equilibrium = -1;
  while (now < Timestamp::Seconds(123 + 1)) {
    i = i + 2;
    queue.EnqueuePacket(PacketInFlightInfo(kPacketSize, now,
                                           /*packet_id=*/i, EcnMarking::kEct1));
    now += TimeDelta::Millis(15);
    queue.EnqueuePacket(PacketInFlightInfo(kPacketSize, now,
                                           /*packet_id=*/i + 1,
                                           EcnMarking::kEct1));

    ASSERT_THAT(queue.DequeuePacket(now),
                Optional(Field(&PacketInFlightInfo::packet_id, i)));
    now += TimeDelta::Millis(15);
    ASSERT_THAT(queue.DequeuePacket(now),
                Optional(Field(&PacketInFlightInfo::packet_id, i + 1)));
    if (queue.l4s_marking_probability() != 0 &&
        marking_probability_at_equilibrium == -1) {
      // Both proportional and integral updates are zero after the second update
      // since the sojourn time is equal to the target delay.
      marking_probability_at_equilibrium = queue.l4s_marking_probability();
    }
  }
  EXPECT_EQ(queue.l4s_marking_probability(),
            marking_probability_at_equilibrium);
}

TEST(DualPi2NetworkQueueTest, L4SQueueCeMarkIfDelayIsTooHigh) {
  DualPi2NetworkQueue queue;
  bool has_seen_ce_marked_packet = false;
  Timestamp now = Timestamp::Seconds(123);
  int i = 0;
  while (now < Timestamp::Seconds(123 + 1)) {
    now += TimeDelta::Millis(20);
    // Enqueue 2 L4S packets but only dequeue one. Delay will grow....
    queue.EnqueuePacket(PacketInFlightInfo(kPacketSize, now,
                                           /*packet_id=*/i++,
                                           EcnMarking::kEct1));
    queue.EnqueuePacket(PacketInFlightInfo(kPacketSize, now,
                                           /*packet_id=*/i++,
                                           EcnMarking::kEct1));

    std::optional<PacketInFlightInfo> dequeued_packet =
        queue.DequeuePacket(now);
    ASSERT_TRUE(dequeued_packet.has_value());
    if (dequeued_packet->ecn == EcnMarking::kCe) {
      EXPECT_GT(queue.l4s_marking_probability(), 0);
      has_seen_ce_marked_packet = true;
      break;
    }
  }
  EXPECT_TRUE(has_seen_ce_marked_packet);
}

TEST(DualPi2NetworkQueueTest, ClassicQueueDropPacketIfL4SDelayIsTooHigh) {
  DualPi2NetworkQueue queue;
  bool has_dropped_classic_packet = false;
  Timestamp now = Timestamp::Seconds(123);
  int i = 0;
  while (now < Timestamp::Seconds(123 + 1)) {
    now += TimeDelta::Millis(20);
    // Enqueue 2 L4S packets but only dequeue one. L4S delay will grow....
    queue.EnqueuePacket(PacketInFlightInfo(kPacketSize, now,
                                           /*packet_id=*/i++,
                                           EcnMarking::kEct1));
    queue.EnqueuePacket(PacketInFlightInfo(kPacketSize, now,
                                           /*packet_id=*/i++,
                                           EcnMarking::kEct1));
    // Enqueue a classic packet.
    has_dropped_classic_packet |= !queue.EnqueuePacket(
        PacketInFlightInfo(kPacketSize, now,
                           /*packet_id=*/i++, EcnMarking::kEct0));

    std::optional<PacketInFlightInfo> dequeued_packet =
        queue.DequeuePacket(now);
    ASSERT_TRUE(dequeued_packet.has_value());
    EXPECT_THAT(dequeued_packet->ecn,
                AnyOf(EcnMarking::kEct0, EcnMarking::kEct1, EcnMarking::kCe));
  }
  EXPECT_TRUE(has_dropped_classic_packet);
}

TEST(DualPi2NetworkQueueTest, CeMarksIfStepThresholdIsReached) {
  DualPi2NetworkQueue::Config config;
  config.target_delay = TimeDelta::Millis(15);
  config.step_threshold = TimeDelta::Millis(1);
  DualPi2NetworkQueue queue(config);
  Timestamp now = Timestamp::Seconds(123);

  // Dequeue one packet at t=0 so last_probability_update_time_ is initialized
  // with zero marking probability.
  ASSERT_TRUE(queue.EnqueuePacket(PacketInFlightInfo(kPacketSize, now,
                                                     /*packet_id=*/0,
                                                     EcnMarking::kEct1)));
  ASSERT_TRUE(queue.DequeuePacket(now).has_value());
  ASSERT_EQ(queue.l4s_marking_probability(), 0.0);

  // Enqueue 2 packets (meeting default min_backlog_packets = 2) and dequeue
  // after step_threshold (1ms) has elapsed.
  ASSERT_TRUE(queue.EnqueuePacket(PacketInFlightInfo(kPacketSize, now,
                                                     /*packet_id=*/1,
                                                     EcnMarking::kEct1)));
  ASSERT_TRUE(queue.EnqueuePacket(PacketInFlightInfo(kPacketSize, now,
                                                     /*packet_id=*/2,
                                                     EcnMarking::kEct1)));
  now += TimeDelta::Millis(2);
  std::optional<PacketInFlightInfo> dequeued_packet = queue.DequeuePacket(now);
  ASSERT_TRUE(dequeued_packet.has_value());
  EXPECT_EQ(queue.l4s_marking_probability(), 0.0);
  EXPECT_EQ(dequeued_packet->ecn, EcnMarking::kCe);
}

TEST(DualPi2NetworkQueueTest,
     DoesNotCeMarkIfBelowMinBacklogPacketsEvenWhenStepThresholdReached) {
  DualPi2NetworkQueue::Config config;
  config.step_threshold = TimeDelta::Millis(1);
  DualPi2NetworkQueue queue(config);
  Timestamp now = Timestamp::Seconds(123);

  // Only 1 packet in the queue (< default min_backlog_packets = 2).
  ASSERT_TRUE(queue.EnqueuePacket(PacketInFlightInfo(kPacketSize, now,
                                                     /*packet_id=*/1,
                                                     EcnMarking::kEct1)));
  now += TimeDelta::Millis(20);
  std::optional<PacketInFlightInfo> dequeued_packet = queue.DequeuePacket(now);
  ASSERT_TRUE(dequeued_packet.has_value());
  EXPECT_GT(queue.l4s_marking_probability(), 0.0);
  EXPECT_EQ(dequeued_packet->ecn, EcnMarking::kEct1);
}

TEST(DualPi2NetworkQueueTest,
     CeMarksSinglePacketWhenMinBacklogPacketsIsConfiguredToOne) {
  DualPi2NetworkQueue::Config config;
  config.step_threshold = TimeDelta::Millis(1);
  config.min_backlog_packets = 1;
  DualPi2NetworkQueue queue(config);
  Timestamp now = Timestamp::Seconds(123);

  ASSERT_TRUE(queue.EnqueuePacket(PacketInFlightInfo(kPacketSize, now,
                                                     /*packet_id=*/1,
                                                     EcnMarking::kEct1)));
  now += TimeDelta::Millis(2);
  std::optional<PacketInFlightInfo> dequeued_packet = queue.DequeuePacket(now);
  ASSERT_TRUE(dequeued_packet.has_value());
  EXPECT_EQ(dequeued_packet->ecn, EcnMarking::kCe);
}

TEST(DualPi2NetworkQueueTest, DoesNotDropClassicPacketBelowMinBacklogPackets) {
  DualPi2NetworkQueue queue;
  Timestamp now = Timestamp::Seconds(123);

  // Drive base_marking_probability_ to 1.0 using high sojourn time.
  ASSERT_TRUE(queue.EnqueuePacket(PacketInFlightInfo(kPacketSize, now,
                                                     /*packet_id=*/0,
                                                     EcnMarking::kEct1)));
  now += TimeDelta::Seconds(1);
  ASSERT_TRUE(queue.DequeuePacket(now).has_value());
  ASSERT_EQ(queue.classic_drop_probability(), 1.0);

  // With 0 and 1 packets in the queue (< default min_backlog_packets = 2),
  // classic packets must not be dropped despite 100% drop probability.
  EXPECT_TRUE(queue.EnqueuePacket(PacketInFlightInfo(kPacketSize, now,
                                                     /*packet_id=*/1,
                                                     EcnMarking::kEct0)));
  EXPECT_TRUE(queue.EnqueuePacket(PacketInFlightInfo(kPacketSize, now,
                                                     /*packet_id=*/2,
                                                     EcnMarking::kEct0)));
  // Now 2 packets are in the queue (>= min_backlog_packets = 2), so the 3rd
  // classic packet is dropped.
  EXPECT_FALSE(queue.EnqueuePacket(PacketInFlightInfo(kPacketSize, now,
                                                      /*packet_id=*/3,
                                                      EcnMarking::kEct0)));
}

TEST(DualPi2NetworkQueueTest, DoesNotCeMarkOnEnqueue) {
  DualPi2NetworkQueue queue;
  Timestamp now = Timestamp::Seconds(123);

  // Drive base_marking_probability_ to 1.0.
  ASSERT_TRUE(queue.EnqueuePacket(PacketInFlightInfo(kPacketSize, now,
                                                     /*packet_id=*/0,
                                                     EcnMarking::kEct1)));
  now += TimeDelta::Seconds(1);
  ASSERT_TRUE(queue.DequeuePacket(now).has_value());
  ASSERT_GE(queue.l4s_marking_probability(), 1.0);

  // Enqueue 3 L4S packets. Even though standing backlog >= 2 and marking
  // probability >= 1.0, packets in the queue must remain kEct1 until dequeued.
  ASSERT_TRUE(queue.EnqueuePacket(PacketInFlightInfo(kPacketSize, now,
                                                     /*packet_id=*/1,
                                                     EcnMarking::kEct1)));
  ASSERT_TRUE(queue.EnqueuePacket(PacketInFlightInfo(kPacketSize, now,
                                                     /*packet_id=*/2,
                                                     EcnMarking::kEct1)));
  ASSERT_TRUE(queue.EnqueuePacket(PacketInFlightInfo(kPacketSize, now,
                                                     /*packet_id=*/3,
                                                     EcnMarking::kEct1)));

  EXPECT_THAT(queue.PeekNextPacket(),
              Optional(Field(&PacketInFlightInfo::ecn, EcnMarking::kEct1)));
  EXPECT_THAT(queue.DequeuePacket(now),
              Optional(Field(&PacketInFlightInfo::ecn, EcnMarking::kCe)));
  EXPECT_THAT(queue.PeekNextPacket(),
              Optional(Field(&PacketInFlightInfo::ecn, EcnMarking::kEct1)));
}

TEST(DualPi2NetworkQueueTest, WrrPreventsClassicQueueStarvation) {
  // Default c_protection is 10% (90% L4S, 10% Classic).
  DualPi2NetworkQueue queue;
  Timestamp now = Timestamp::Seconds(123);

  // Enqueue 100 L4S packets and 20 Classic packets of equal size (1500B).
  constexpr DataSize kMtuSize = DataSize::Bytes(1500);
  int packet_id = 0;
  for (int i = 0; i < 100; ++i) {
    ASSERT_TRUE(queue.EnqueuePacket(PacketInFlightInfo(
        kMtuSize, now, /*packet_id=*/packet_id++, EcnMarking::kEct1)));
  }
  for (int i = 0; i < 20; ++i) {
    ASSERT_TRUE(queue.EnqueuePacket(PacketInFlightInfo(
        kMtuSize, now, /*packet_id=*/packet_id++, EcnMarking::kNotEct)));
  }

  int l4s_dequeued = 0;
  int classic_dequeued = 0;
  for (int i = 0; i < 100; ++i) {
    std::optional<PacketInFlightInfo> peeked = queue.PeekNextPacket();
    std::optional<PacketInFlightInfo> dequeued = queue.DequeuePacket(now);
    ASSERT_TRUE(peeked.has_value());
    ASSERT_TRUE(dequeued.has_value());
    EXPECT_EQ(peeked->packet_id, dequeued->packet_id);
    if (dequeued->ecn == EcnMarking::kNotEct) {
      ++classic_dequeued;
    } else {
      ++l4s_dequeued;
    }
  }
  // Out of 100 dequeued packets while both queues are backlogged, 10% (10)
  // should be from the classic queue and 90% (90) from the L4S queue.
  EXPECT_EQ(classic_dequeued, 10);
  EXPECT_EQ(l4s_dequeued, 90);
}

}  // namespace
}  // namespace webrtc
