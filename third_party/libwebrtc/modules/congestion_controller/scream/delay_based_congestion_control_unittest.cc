/*
 *  Copyright 2025 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/congestion_controller/scream/delay_based_congestion_control.h"

#include "api/environment/environment.h"
#include "api/transport/network_types.h"
#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "modules/congestion_controller/scream/scream_feedback.h"
#include "modules/congestion_controller/scream/scream_v2_parameters.h"
#include "modules/congestion_controller/scream/test/cc_feedback_generator.h"
#include "system_wrappers/include/clock.h"
#include "test/create_test_environment.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

TEST(DelayBasedCongestionControlTest, InfiniteQueueBeforeFirstFeedback) {
  Environment env = CreateTestEnvironment();
  DelayBasedCongestionControl delay_controller(
      ScreamV2Parameters(env.field_trials()));
  EXPECT_EQ(delay_controller.queue_delay(), TimeDelta::PlusInfinity());
}

TEST(DelayBasedCongestionControlTest,
     QueueDelayDoesNotIncreaseIfSendRateIsLow) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  DelayBasedCongestionControl delay_controller(
      ScreamV2Parameters(env.field_trials()));

  // Note, network_config.queue_delay_ms is the one way delay in the simulation,
  // not a delay caused by queues.
  CcFeedbackGenerator feedback_generator({
      .network_config = {.queue_delay_ms = 25,
                         .link_capacity = DataRate::KilobitsPerSec(1000)},
  });

  // Sending at a rate below link capacity should not cause queue delay to
  // increase.
  for (int i = 0; i < 10; ++i) {
    DataRate send_rate = DataRate::KilobitsPerSec(100);
    TransportPacketsFeedback feedback =
        feedback_generator.ProcessUntilNextFeedback(send_rate, clock);

    delay_controller.Update(ParseScreamFeedback(feedback), /*alr=*/false);
    EXPECT_EQ(delay_controller.rtt(), TimeDelta::Millis(58));
    EXPECT_EQ(delay_controller.queue_delay(), TimeDelta::Millis(0));
    EXPECT_FALSE(delay_controller.IsQueueDelayDetected());
  }
}

TEST(DelayBasedCongestionControlTest, QueueDelayIncreaseIfSendRateIsHigh) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  DelayBasedCongestionControl delay_controller(
      ScreamV2Parameters(env.field_trials()));
  // Note, network_config.queue_delay_ms is the one way delay in the simulation,
  // not a delay caused by queues.
  CcFeedbackGenerator feedback_generator({
      .network_config = {.queue_delay_ms = 25,
                         .link_capacity = DataRate::KilobitsPerSec(1000)},
  });

  for (int i = 0; i < 10; ++i) {
    // Send faster than link capacity to build a queue.
    DataRate send_rate = DataRate::KilobitsPerSec(2000);
    TransportPacketsFeedback feedback =
        feedback_generator.ProcessUntilNextFeedback(send_rate, clock);
    delay_controller.Update(ParseScreamFeedback(feedback), /*alr=*/false);
  }
  EXPECT_GT(delay_controller.queue_delay(), TimeDelta::Millis(50));
  EXPECT_TRUE(delay_controller.IsQueueDelayDetected());
}



TEST(DelayBasedCongestionControlTest, ResetQueueDelay) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  DelayBasedCongestionControl delay_controller(
      ScreamV2Parameters(env.field_trials()));
  CcFeedbackGenerator feedback_generator({
      .network_config = {.queue_delay_ms = 25,
                         .link_capacity = DataRate::KilobitsPerSec(100)},
  });

  Timestamp start_time = clock.CurrentTime();
  ASSERT_EQ(delay_controller.queue_delay(), TimeDelta::PlusInfinity());

  // Overuse the link during 1s.
  TimeDelta last_smoothed_rtt;
  while (clock.CurrentTime() < start_time + TimeDelta::Seconds(1)) {
    TransportPacketsFeedback feedback =
        feedback_generator.ProcessUntilNextFeedback(
            DataRate::KilobitsPerSec(150), clock);
    delay_controller.Update(ParseScreamFeedback(feedback), /*alr=*/false);
    last_smoothed_rtt = delay_controller.rtt();
  }
  TimeDelta queue_delay_before_reset = delay_controller.queue_delay();
  ASSERT_GT(queue_delay_before_reset, TimeDelta::Zero());
  ASSERT_LT(queue_delay_before_reset, TimeDelta::PlusInfinity());

  delay_controller.ResetQueueDelay();
  ASSERT_EQ(delay_controller.queue_delay(), TimeDelta::PlusInfinity());

  TransportPacketsFeedback feedback =
      feedback_generator.ProcessUntilNextFeedback(DataRate::KilobitsPerSec(150),
                                                  clock);
  delay_controller.Update(ParseScreamFeedback(feedback), /*alr=*/false);
  // RTT is still increasing or equal to the last feedback.
  EXPECT_GE(delay_controller.rtt(), last_smoothed_rtt);
  // But queue delay should be lower.
  EXPECT_LT(delay_controller.queue_delay(), queue_delay_before_reset);
}

TEST(DelayBasedCongestionControlTest,
     IsQueueDrainedInTimeReturnFalseIfLongOverUse) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  DelayBasedCongestionControl delay_controller(
      ScreamV2Parameters(env.field_trials()));
  CcFeedbackGenerator feedback_generator({
      .network_config = {.queue_delay_ms = 25,
                         .link_capacity = DataRate::KilobitsPerSec(100)},
  });

  Timestamp start_time = clock.CurrentTime();
  while (clock.CurrentTime() < start_time + TimeDelta::Seconds(30) &&
         delay_controller.IsQueueDrainedInTime(clock.CurrentTime())) {
    TransportPacketsFeedback feedback =
        feedback_generator.ProcessUntilNextFeedback(
            DataRate::KilobitsPerSec(150), clock);
    delay_controller.Update(ParseScreamFeedback(feedback), /*alr=*/false);
  }
  EXPECT_LT(clock.CurrentTime(), start_time + TimeDelta::Seconds(30));
  EXPECT_GT(clock.CurrentTime(), start_time + TimeDelta::Seconds(10));
  EXPECT_FALSE(delay_controller.IsQueueDrainedInTime(clock.CurrentTime()));
}

TEST(DelayBasedCongestionControlTest,
     RefWindowScaleFactorDueToMinAverageQueueDelay) {
  SimulatedClock clock(Timestamp::Seconds(1234));
  Environment env = CreateTestEnvironment(
      {.field_trials = "WebRTC-Bwe-ScreamV2/ReduceJitter:false/",
       .time = &clock});
  ScreamV2Parameters params(env.field_trials());
  DelayBasedCongestionControl delay_controller(params);

  const TimeDelta kQueueDelyMinThreshold =
      params.queue_delay_min_threshold.Get();

  // Helper to establish queue delay.
  // Assumes base delay is 100ms.
  auto feed_packets = [&](TimeDelta qdelay, int count) {
    for (int i = 0; i < count; ++i) {
      clock.AdvanceTime(TimeDelta::Millis(10));
      TransportPacketsFeedback msg;
      msg.feedback_time = clock.CurrentTime();
      PacketResult packet;
      packet.receive_time = clock.CurrentTime();
      packet.sent_packet.send_time =
          clock.CurrentTime() - TimeDelta::Millis(100) - qdelay;
      packet.sent_packet.sequence_number = i;
      msg.packet_feedbacks.push_back(packet);
      delay_controller.Update(ParseScreamFeedback(msg), /*alr=*/false);
    }
  };

  // Establish base delay (100ms) with qdelay=0.
  feed_packets(TimeDelta::Zero(), 100);
  EXPECT_DOUBLE_EQ(
      delay_controller.ref_window_scale_factor_due_to_avg_min_delay(), 1.0);

  // Drive queue delay to kQueueDelyMinThreshold.
  feed_packets(kQueueDelyMinThreshold + TimeDelta::Millis(10), 250);
  EXPECT_NEAR(delay_controller.ref_window_scale_factor_due_to_avg_min_delay(),
              0.1, 0.01);

  // Drive queue delay to `0.5 * kQueueDelyMinThreshold`.
  // Expected: 0.1 + 1.2 * 0.5 = 0.7
  feed_packets(kQueueDelyMinThreshold / 2, 250);
  EXPECT_NEAR(delay_controller.ref_window_scale_factor_due_to_avg_min_delay(),
              0.7, 0.01);

  // Drive queue delay to `0.25 * kQueueDelyMinThreshold`.
  // Expected: 0.1 + 1.2 * 0.75 = 1.0.
  feed_packets(kQueueDelyMinThreshold / 4, 250);
  EXPECT_NEAR(delay_controller.ref_window_scale_factor_due_to_avg_min_delay(),
              1.0, 0.01);
}

TEST(DelayBasedCongestionControlTest,
     RefWindowScaleFactorDueToLatencyDifference) {
  SimulatedClock clock(Timestamp::Seconds(1234));
  Environment env = CreateTestEnvironment(
      {.field_trials = "WebRTC-Bwe-ScreamV2/ReduceJitter:false/",
       .time = &clock});
  ScreamV2Parameters params(env.field_trials());
  DelayBasedCongestionControl delay_controller(params);

  const TimeDelta kLatencyThreshold = params.latency_diff_threshold.Get();

  // Helper to establish latency differences within feedbacks.
  // Assumes a base one-way delay of 100ms.
  auto feed_packets = [&](TimeDelta latency_diff, int count) {
    for (int i = 0; i < count; ++i) {
      clock.AdvanceTime(TimeDelta::Millis(10));
      TransportPacketsFeedback msg;
      msg.feedback_time = clock.CurrentTime();

      PacketResult packet1;
      packet1.receive_time = clock.CurrentTime();
      packet1.sent_packet.send_time =
          clock.CurrentTime() - TimeDelta::Millis(100);
      packet1.sent_packet.sequence_number = i * 2;

      PacketResult packet2;
      packet2.receive_time = clock.CurrentTime();
      packet2.sent_packet.send_time =
          clock.CurrentTime() - TimeDelta::Millis(100) - latency_diff;
      packet2.sent_packet.sequence_number = i * 2 + 1;

      msg.packet_feedbacks.push_back(packet1);
      msg.packet_feedbacks.push_back(packet2);
      delay_controller.Update(ParseScreamFeedback(msg), /*alr=*/false);
    }
  };

  // Establish 0 latency difference.
  feed_packets(TimeDelta::Zero(), 100);
  EXPECT_DOUBLE_EQ(
      delay_controller.ref_window_scale_factor_due_to_latency_difference(),
      1.0);

  // Drive to `kLatencyThreshold`.
  feed_packets(kLatencyThreshold + TimeDelta::Millis(10), 250);
  EXPECT_NEAR(
      delay_controller.ref_window_scale_factor_due_to_latency_difference(), 0.1,
      0.01);

  // Drive to `0.5 * kLatencyThreshold`.
  // Expected: 0.1 + 1.2 * 0.5 = 0.7
  feed_packets(kLatencyThreshold / 2, 250);
  EXPECT_NEAR(
      delay_controller.ref_window_scale_factor_due_to_latency_difference(), 0.7,
      0.01);

  // Drive to `0.25 * kLatencyThreshold`.
  // Expected: 0.1 + 1.2 * 0.75 = 1.0.
  feed_packets(kLatencyThreshold / 4, 250);
  EXPECT_NEAR(
      delay_controller.ref_window_scale_factor_due_to_latency_difference(), 1.0,
      0.01);
}

TEST(DelayBasedCongestionControlTest,
     PacketsSentOutsideBurstDoNotReduceRefWindowScaleFactor) {
  SimulatedClock clock(Timestamp::Seconds(1234));
  Environment env = CreateTestEnvironment({.time = &clock});
  ScreamV2Parameters params(env.field_trials());
  DelayBasedCongestionControl delay_controller(params);

  // Feed packets with 50ms delay difference, but sent 100ms apart (> 25ms burst
  // delta).
  for (int i = 0; i < 10; ++i) {
    clock.AdvanceTime(TimeDelta::Millis(100));
    TransportPacketsFeedback msg;
    msg.feedback_time = clock.CurrentTime();

    PacketResult packet1;
    packet1.receive_time = clock.CurrentTime() - TimeDelta::Millis(50);
    packet1.sent_packet.send_time =
        clock.CurrentTime() - TimeDelta::Millis(150);
    packet1.sent_packet.sequence_number = i * 2;

    PacketResult packet2;
    packet2.receive_time = clock.CurrentTime();
    packet2.sent_packet.send_time = clock.CurrentTime() - TimeDelta::Millis(50);
    packet2.sent_packet.sequence_number = i * 2 + 1;

    msg.packet_feedbacks.push_back(packet1);
    msg.packet_feedbacks.push_back(packet2);
    delay_controller.Update(ParseScreamFeedback(msg, params), /*alr=*/false);
  }

  // Because send delta is 100ms > 25ms, latency_difference is 0 and scale
  // factor remains 1.0.
  EXPECT_DOUBLE_EQ(
      delay_controller.ref_window_scale_factor_due_to_latency_difference(),
      1.0);
}

TEST(DelayBasedCongestionControlTest, RttDecaysSlowerInAlr) {
  SimulatedClock clock(Timestamp::Seconds(1234));
  Environment env = CreateTestEnvironment({.time = &clock});
  DelayBasedCongestionControl delay_controller_alr(
      ScreamV2Parameters(env.field_trials()));
  DelayBasedCongestionControl delay_controller_no_alr(
      ScreamV2Parameters(env.field_trials()));

  auto feed_feedback = [&](DelayBasedCongestionControl& controller,
                           TimeDelta rtt, bool alr) {
    TransportPacketsFeedback msg;
    msg.feedback_time = clock.CurrentTime();
    PacketResult packet;
    packet.receive_time = clock.CurrentTime();
    packet.sent_packet.send_time = clock.CurrentTime() - rtt;
    packet.sent_packet.sequence_number = 0;
    msg.packet_feedbacks.push_back(packet);
    controller.Update(ParseScreamFeedback(msg), alr);
  };

  // Establish initial smoothed RTT of 200ms.
  feed_feedback(delay_controller_alr, TimeDelta::Millis(200), /*alr=*/false);
  feed_feedback(delay_controller_no_alr, TimeDelta::Millis(200), /*alr=*/false);

  EXPECT_EQ(delay_controller_alr.rtt(), TimeDelta::Millis(200));
  EXPECT_EQ(delay_controller_no_alr.rtt(), TimeDelta::Millis(200));

  // Advance time.
  clock.AdvanceTime(TimeDelta::Millis(100));

  // Send feedback with a lower RTT sample of 100ms.
  feed_feedback(delay_controller_alr, TimeDelta::Millis(100), /*alr=*/true);
  feed_feedback(delay_controller_no_alr, TimeDelta::Millis(100), /*alr=*/false);

  // With ALR, RTT decay is slower, so the resulting smoothed RTT is higher.
  EXPECT_GT(delay_controller_alr.rtt(), delay_controller_no_alr.rtt());

  // Verify actual values.
  // no_alr: 100 * 0.125 + 200 * 0.875 = 187.5ms
  // alr: 100 * (1/128) + 200 * (127/128) = 199.21875ms
  EXPECT_NEAR(delay_controller_no_alr.rtt().ms<double>(), 187.5, 0.1);
  EXPECT_NEAR(delay_controller_alr.rtt().ms<double>(), 199.2, 0.1);
}

TEST(DelayBasedCongestionControlTest, RttIncreasesSlowerInAlr) {
  SimulatedClock clock(Timestamp::Seconds(1234));
  Environment env = CreateTestEnvironment({.time = &clock});
  DelayBasedCongestionControl delay_controller_alr(
      ScreamV2Parameters(env.field_trials()));
  DelayBasedCongestionControl delay_controller_no_alr(
      ScreamV2Parameters(env.field_trials()));

  auto feed_feedback = [&](DelayBasedCongestionControl& controller,
                           TimeDelta rtt, bool alr) {
    TransportPacketsFeedback msg;
    msg.feedback_time = clock.CurrentTime();
    PacketResult packet;
    packet.receive_time = clock.CurrentTime();
    packet.sent_packet.send_time = clock.CurrentTime() - rtt;
    packet.sent_packet.sequence_number = 0;
    msg.packet_feedbacks.push_back(packet);
    controller.Update(ParseScreamFeedback(msg), alr);
  };

  // Establish initial smoothed RTT of 100ms.
  feed_feedback(delay_controller_alr, TimeDelta::Millis(100), /*alr=*/false);
  feed_feedback(delay_controller_no_alr, TimeDelta::Millis(100), /*alr=*/false);

  EXPECT_EQ(delay_controller_alr.rtt(), TimeDelta::Millis(100));
  EXPECT_EQ(delay_controller_no_alr.rtt(), TimeDelta::Millis(100));

  // Advance time.
  clock.AdvanceTime(TimeDelta::Millis(100));

  // Send feedback with a higher RTT sample of 200ms.
  feed_feedback(delay_controller_alr, TimeDelta::Millis(200), /*alr=*/true);
  feed_feedback(delay_controller_no_alr, TimeDelta::Millis(200), /*alr=*/false);

  // With ALR, RTT increase is slower, so the resulting smoothed RTT is lower.
  EXPECT_LT(delay_controller_alr.rtt(), delay_controller_no_alr.rtt());

  // Verify actual values.
  // no_alr: 200 * 0.125 + 100 * 0.875 = 112.5ms
  // alr: 200 * (1/128) + 100 * (127/128) = 100.78125ms
  EXPECT_NEAR(delay_controller_no_alr.rtt().ms<double>(), 112.5, 0.1);
  EXPECT_NEAR(delay_controller_alr.rtt().ms<double>(), 100.8, 0.1);
}

// TODO: bugs.webrtc.org/447037083 - add tests for clock drift in feedback NTP
// time.

TEST(DelayBasedCongestionControlTest,
     LatencyDifferenceWithNegativeOneWayDelay) {
  SimulatedClock clock(Timestamp::Seconds(1234));
  Environment env = CreateTestEnvironment({.time = &clock});
  DelayBasedCongestionControl delay_controller(
      ScreamV2Parameters(env.field_trials()));

  // Send packets where receiver clock is behind sender clock (negative OWD),
  // but there is zero latency variation between packets.
  for (int i = 0; i < 50; ++i) {
    clock.AdvanceTime(TimeDelta::Millis(10));
    TransportPacketsFeedback msg;
    msg.feedback_time = clock.CurrentTime();

    PacketResult packet1;
    packet1.receive_time = clock.CurrentTime();
    packet1.sent_packet.send_time =
        clock.CurrentTime() + TimeDelta::Millis(100);
    packet1.sent_packet.sequence_number = i * 2;

    PacketResult packet2;
    packet2.receive_time = clock.CurrentTime();
    packet2.sent_packet.send_time =
        clock.CurrentTime() + TimeDelta::Millis(100);
    packet2.sent_packet.sequence_number = i * 2 + 1;

    msg.packet_feedbacks.push_back(packet1);
    msg.packet_feedbacks.push_back(packet2);
    delay_controller.Update(ParseScreamFeedback(msg), /*alr=*/false);
  }

  EXPECT_EQ(delay_controller.latency_difference_avg(), TimeDelta::Zero());
  EXPECT_DOUBLE_EQ(
      delay_controller.ref_window_scale_factor_due_to_latency_difference(),
      1.0);
}

TEST(DelayBasedCongestionControlTest,
     ReduceJitterTracksShortAndLongQueueDelayTrend) {
  SimulatedClock clock(Timestamp::Seconds(1234));
  Environment env = CreateTestEnvironment(
      {.field_trials = "WebRTC-Bwe-ScreamV2/ReduceJitter:true/",
       .time = &clock});
  DelayBasedCongestionControl delay_controller(
      ScreamV2Parameters(env.field_trials()));

  auto send_feedback_with_qdelay = [&](TimeDelta qdelay) {
    clock.AdvanceTime(TimeDelta::Millis(25));
    TransportPacketsFeedback msg;
    msg.feedback_time = clock.CurrentTime();
    PacketResult packet;
    packet.receive_time = clock.CurrentTime();
    packet.sent_packet.send_time =
        clock.CurrentTime() - TimeDelta::Millis(50) - qdelay;
    packet.sent_packet.sequence_number = 0;
    msg.packet_feedbacks.push_back(packet);
    delay_controller.Update(ParseScreamFeedback(msg), /*alr=*/false);
  };

  send_feedback_with_qdelay(/*qdelay=*/TimeDelta::Zero());
  EXPECT_DOUBLE_EQ(
      delay_controller.ref_window_scale_factor_due_to_latency_difference(),
      1.0);

  // Simulate rising queue delay.
  Timestamp start_time = clock.CurrentTime();
  while (clock.CurrentTime() < start_time + TimeDelta::Millis(500)) {
    send_feedback_with_qdelay(/*qdelay=*/TimeDelta::Millis(20));
  }
  EXPECT_LT(
      delay_controller.ref_window_scale_factor_due_to_latency_difference(),
      0.8);

  // When queue delay stabilizes, the scale factor recovers back above 0.95
  // despite queue delay staying high.
  start_time = clock.CurrentTime();
  while (delay_controller.ref_window_scale_factor_due_to_latency_difference() <=
             0.95 &&
         clock.CurrentTime() < start_time + TimeDelta::Seconds(30)) {
    send_feedback_with_qdelay(/*qdelay=*/TimeDelta::Millis(20));
  }
  EXPECT_GT(
      delay_controller.ref_window_scale_factor_due_to_latency_difference(),
      0.95);
  EXPECT_LT(clock.CurrentTime() - start_time, TimeDelta::Seconds(20));
  EXPECT_GT(clock.CurrentTime() - start_time, TimeDelta::Seconds(10));
}

}  // namespace
}  // namespace webrtc
