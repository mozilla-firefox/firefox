/*
 *  Copyright 2025 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/congestion_controller/scream/scream_network_controller.h"

#include <algorithm>
#include <utility>

#include "api/environment/environment.h"
#include "api/transport/network_control.h"
#include "api/transport/network_types.h"
#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "modules/congestion_controller/scream/test/cc_feedback_generator.h"
#include "system_wrappers/include/clock.h"
#include "test/create_test_environment.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

using ::testing::Eq;
using ::testing::Field;
using ::testing::Lt;
using ::testing::Optional;

constexpr double kPacingFactor = 1.1;

TEST(ScreamControllerTest, CanConstruct) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  NetworkControllerConfig config(env);
  ScreamNetworkController scream_controller(config);
}

TEST(ScreamControllerTest, OnNetworkAvailabilityUpdatesTargetRateAndPacerRate) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  NetworkControllerConfig config(env);
  config.constraints.starting_rate = DataRate::KilobitsPerSec(123);
  config.stream_based_config.max_total_allocated_bitrate =
      DataRate::KilobitsPerSec(456);
  ScreamNetworkController scream_controller(config);

  NetworkControlUpdate update = scream_controller.OnNetworkAvailability(
      {.at_time = clock.CurrentTime(), .network_available = true});
  ASSERT_TRUE(update.has_updates());
  ASSERT_TRUE(update.target_rate.has_value());
  EXPECT_EQ(update.target_rate->target_rate, config.constraints.starting_rate);
  ASSERT_TRUE(update.pacer_config);
  EXPECT_EQ(update.pacer_config->data_window,
            *config.constraints.starting_rate * kPacingFactor *
                PacerConfig::kDefaultTimeInterval);
}

TEST(ScreamControllerTest,
     OnTransportPacketsFeedbackUpdatesTargetRateAndPacerRate) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  NetworkControllerConfig config(env);
  ScreamNetworkController scream_controller(config);

  // Simulation with infinite capacity.
  CcFeedbackGenerator feedback_generator({});

  TransportPacketsFeedback feedback =
      feedback_generator.ProcessUntilNextFeedback(DataRate::KilobitsPerSec(100),
                                                  clock);
  NetworkControlUpdate update =
      scream_controller.OnTransportPacketsFeedback(feedback);
  ASSERT_TRUE(update.has_updates());
  ASSERT_TRUE(update.target_rate.has_value());
  EXPECT_GT(update.target_rate->target_rate, DataRate::KilobitsPerSec(100));
  ASSERT_TRUE(update.pacer_config);
  EXPECT_EQ(update.pacer_config->data_window,
            update.target_rate->target_rate * kPacingFactor *
                PacerConfig::kDefaultTimeInterval);
}

TEST(ScreamControllerTest,
     OnNetworkRouteChangeResetsScreamAndUpdatesTargetRate) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  NetworkControllerConfig config(env);
  config.constraints.starting_rate = DataRate::KilobitsPerSec(50);
  config.stream_based_config.max_total_allocated_bitrate =
      DataRate::KilobitsPerSec(1000);
  ScreamNetworkController scream_controller(config);
  scream_controller.OnNetworkAvailability(
      {.at_time = clock.CurrentTime(), .network_available = true});

  CcFeedbackGenerator feedback_generator({});
  DataRate send_rate = DataRate::KilobitsPerSec(100);
  for (int i = 0; i < 10; ++i) {
    TransportPacketsFeedback feedback =
        feedback_generator.ProcessUntilNextFeedback(send_rate, clock);
    NetworkControlUpdate update =
        scream_controller.OnTransportPacketsFeedback(feedback);
    if (update.target_rate.has_value()) {
      send_rate = update.target_rate->target_rate;
    }
  }
  ASSERT_GT(send_rate, DataRate::KilobitsPerSec(50));

  NetworkRouteChange route_change;
  route_change.constraints.starting_rate = config.constraints.starting_rate =
      DataRate::KilobitsPerSec(123);
  route_change.at_time = clock.CurrentTime();
  NetworkControlUpdate update =
      scream_controller.OnNetworkRouteChange(route_change);
  ASSERT_TRUE(update.has_updates());
  ASSERT_TRUE(update.target_rate.has_value());
  EXPECT_EQ(update.target_rate->target_rate,
            route_change.constraints.starting_rate);
  ASSERT_TRUE(update.pacer_config);
  EXPECT_EQ(update.pacer_config->data_window,
            *route_change.constraints.starting_rate * kPacingFactor *
                PacerConfig::kDefaultTimeInterval);
}

class ScreamControllerSafeResetTest : public ::testing::TestWithParam<bool> {};

TEST_P(ScreamControllerSafeResetTest,
       OnNetworkRouteChangeWithRestartBweOnDegradedLink) {
  bool safe_reset = GetParam();
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment(
      {.field_trials =
           safe_reset ? ""
                      : "WebRTC-Bwe-ScreamV2/SafeResetOnRouteChange:false/",
       .time = &clock});
  NetworkControllerConfig config(env);
  config.constraints.starting_rate = DataRate::KilobitsPerSec(300);
  config.stream_based_config.max_total_allocated_bitrate =
      DataRate::KilobitsPerSec(1000);
  ScreamNetworkController scream_controller(config);
  scream_controller.OnNetworkAvailability(
      {.at_time = clock.CurrentTime(), .network_available = true});

  // Low capacity link to cause target rate to decrease below starting rate.
  CcFeedbackGenerator feedback_generator({
      .network_config = {.link_capacity = DataRate::KilobitsPerSec(100)},
  });

  DataRate send_rate = DataRate::KilobitsPerSec(300);
  for (int i = 0; i < 20; ++i) {
    TransportPacketsFeedback feedback =
        feedback_generator.ProcessUntilNextFeedback(send_rate, clock);
    NetworkControlUpdate update =
        scream_controller.OnTransportPacketsFeedback(feedback);
    if (update.target_rate.has_value()) {
      send_rate = update.target_rate->target_rate;
    }
  }
  ASSERT_LT(send_rate, DataRate::KilobitsPerSec(300));

  // Route change with restart_bwe = true and default starting rate of 300 kbps.
  NetworkRouteChange route_change;
  route_change.restart_bwe = true;
  route_change.constraints.starting_rate = DataRate::KilobitsPerSec(300);
  route_change.at_time = clock.CurrentTime();

  NetworkControlUpdate update =
      scream_controller.OnNetworkRouteChange(route_change);
  ASSERT_TRUE(update.has_updates());
  ASSERT_TRUE(update.target_rate.has_value());
  if (safe_reset) {
    // Target rate must not jump back up to 300 kbps on route change.
    EXPECT_LE(update.target_rate->target_rate, send_rate);
  } else {
    // When safe reset is disabled, target rate resets back up to 300 kbps.
    EXPECT_EQ(update.target_rate->target_rate, DataRate::KilobitsPerSec(300));
  }
}

INSTANTIATE_TEST_SUITE_P(All, ScreamControllerSafeResetTest, ::testing::Bool());

TEST(ScreamControllerTest, TargetRateRampsUptoTargetConstraints) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  NetworkControllerConfig config(env);
  config.constraints.max_data_rate = DataRate::KilobitsPerSec(300);
  ScreamNetworkController scream_controller(config);

  // Simulation with infinite capacity.
  CcFeedbackGenerator feedback_generator({});

  DataRate target_rate = DataRate::KilobitsPerSec(100);
  for (int i = 0; i < 10; ++i) {
    TransportPacketsFeedback feedback =
        feedback_generator.ProcessUntilNextFeedback(target_rate, clock);
    NetworkControlUpdate update =
        scream_controller.OnTransportPacketsFeedback(feedback);
    if (update.target_rate.has_value()) {
      target_rate = update.target_rate->target_rate;
    }
  }
  EXPECT_EQ(target_rate, DataRate::KilobitsPerSec(300));

  // Reduce the constraints and expect the next target rate is bound by it.
  TargetRateConstraints constraints;
  constraints.max_data_rate = DataRate::KilobitsPerSec(200);
  scream_controller.OnTargetRateConstraints(constraints);
  TransportPacketsFeedback feedback =
      feedback_generator.ProcessUntilNextFeedback(target_rate, clock);
  NetworkControlUpdate update =
      scream_controller.OnTransportPacketsFeedback(feedback);
  ASSERT_TRUE(update.target_rate.has_value());
  EXPECT_EQ(update.target_rate->target_rate, DataRate::KilobitsPerSec(200));
}

void TestRouteChangeWithoutBweRestart(
    TimeDelta initial_queue_delay,
    TimeDelta queue_delay_after_route_change) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  NetworkControllerConfig config(env);
  config.constraints.starting_rate = DataRate::KilobitsPerSec(100);
  config.constraints.max_data_rate = DataRate::KilobitsPerSec(3000);
  ScreamNetworkController scream_controller(config);
  scream_controller.OnNetworkAvailability(
      {.at_time = clock.CurrentTime(), .network_available = true});

  CcFeedbackGenerator::Config initial_config;
  initial_config.network_config.queue_delay_ms = initial_queue_delay.ms();
  initial_config.network_config.link_capacity = DataRate::KilobitsPerSec(2000);
  CcFeedbackGenerator feedback_generator(std::move(initial_config));

  DataRate target_rate = DataRate::KilobitsPerSec(100);
  for (int i = 0; i < 100; ++i) {
    TransportPacketsFeedback feedback =
        feedback_generator.ProcessUntilNextFeedback(
            target_rate, clock, [&](const SentPacket& packet) {
              scream_controller.OnSentPacket(packet);
            });
    NetworkControlUpdate update =
        scream_controller.OnTransportPacketsFeedback(feedback);
    if (update.target_rate.has_value()) {
      target_rate = update.target_rate->target_rate;
    }
  }
  EXPECT_GT(target_rate, DataRate::KilobitsPerSec(1500));

  NetworkRouteChange route_change;
  route_change.at_time = clock.CurrentTime();
  route_change.restart_bwe = false;
  route_change.constraints = config.constraints;
  scream_controller.OnNetworkRouteChange(route_change);

  CcFeedbackGenerator::Config new_config;
  new_config.network_config.queue_delay_ms =
      queue_delay_after_route_change.ms();
  new_config.network_config.link_capacity = DataRate::KilobitsPerSec(2000);
  CcFeedbackGenerator new_generator(std::move(new_config));

  NetworkControlUpdate update_new_rtt;
  int target_rate_update_counter = 0;
  for (int i = 0; i < 20; ++i) {
    TransportPacketsFeedback feedback_new_rtt =
        new_generator.ProcessUntilNextFeedback(
            target_rate, clock, [&](const SentPacket& packet) {
              scream_controller.OnSentPacket(packet);
            });
    update_new_rtt =
        scream_controller.OnTransportPacketsFeedback(feedback_new_rtt);
    if (update_new_rtt.target_rate.has_value()) {
      ++target_rate_update_counter;
      EXPECT_GT(update_new_rtt.target_rate->target_rate,
                DataRate::KilobitsPerSec(500));
    }
  }
  ASSERT_GE(target_rate_update_counter, 5);
}

TEST(ScreamControllerTest,
     RouteChangeWithoutBweRestartHigherRttDoesNotResetStartingRate) {
  TestRouteChangeWithoutBweRestart(
      /*initial_queue_delay=*/TimeDelta::Millis(10),
      /*queue_delay_after_route_change=*/TimeDelta::Millis(100));
}

TEST(ScreamControllerTest,
     RouteChangeWithoutBweRestartLowerRttDoesNotResetStartingRate) {
  TestRouteChangeWithoutBweRestart(
      /*initial_queue_delay=*/TimeDelta::Millis(50),
      /*queue_delay_after_route_change=*/TimeDelta::Millis(10));
}

TEST(ScreamControllerTest, TargetRateLimitedByRemoteBitrateReport) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  NetworkControllerConfig config(env);
  config.constraints.max_data_rate = DataRate::KilobitsPerSec(1000);
  ScreamNetworkController scream_controller(config);

  // Simulation with infinite capacity.
  CcFeedbackGenerator feedback_generator({});
  DataRate target_rate = DataRate::KilobitsPerSec(100);
  for (int i = 0; i < 10; ++i) {
    TransportPacketsFeedback feedback =
        feedback_generator.ProcessUntilNextFeedback(
            target_rate, clock, [&](const SentPacket& packet) {
              scream_controller.OnSentPacket(packet);
            });
    NetworkControlUpdate update =
        scream_controller.OnTransportPacketsFeedback(feedback);
    if (update.target_rate.has_value()) {
      target_rate = update.target_rate->target_rate;
    }
  }
  EXPECT_EQ(target_rate, DataRate::KilobitsPerSec(1000));

  RemoteBitrateReport msg;
  msg.bandwidth = DataRate::KilobitsPerSec(500);
  msg.receive_time = clock.CurrentTime();
  NetworkControlUpdate update = scream_controller.OnRemoteBitrateReport(msg);

  ASSERT_TRUE(update.target_rate.has_value());
  EXPECT_EQ(update.target_rate->target_rate, DataRate::KilobitsPerSec(500));

  for (int i = 0; i < 2; ++i) {
    TransportPacketsFeedback feedback =
        feedback_generator.ProcessUntilNextFeedback(
            target_rate, clock, [&](const SentPacket& packet) {
              scream_controller.OnSentPacket(packet);
            });
    update = scream_controller.OnTransportPacketsFeedback(feedback);
    if (update.target_rate.has_value()) {
      EXPECT_EQ(update.target_rate->target_rate, DataRate::KilobitsPerSec(500));
    }
  }
}

TEST(ScreamControllerTest, PacingWindowReducedIfCeCongestedStreamsConfigured) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  CcFeedbackGenerator feedback_generator({
      .network_config = {.link_capacity = DataRate::KilobitsPerSec(900)},
      .send_as_ect1 = true,
  });

  NetworkControllerConfig config(env);
  ScreamNetworkController scream_controller(config);

  StreamsConfig streams_config;
  streams_config.max_total_allocated_bitrate = DataRate::KilobitsPerSec(1000);
  scream_controller.OnStreamsConfig(streams_config);

  NetworkControlUpdate update;
  DataRate send_rate = DataRate::KilobitsPerSec(500);
  for (int i = 0; i < 20; ++i) {
    TransportPacketsFeedback feedback =
        feedback_generator.ProcessUntilNextFeedback(
            send_rate, clock, [&](const SentPacket& packet) {
              scream_controller.OnSentPacket(packet);
            });
    update = scream_controller.OnTransportPacketsFeedback(feedback);
    if (update.target_rate.has_value()) {
      send_rate = update.target_rate->target_rate;
    }
  }
  EXPECT_THAT(update.pacer_config,
              Optional(Field(&PacerConfig::time_window,
                             Lt(PacerConfig::kDefaultTimeInterval))));
}

TEST(ScreamControllerTest,
     PacingWindowReducedIfDelayCongestedStreamsConfigured) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  CcFeedbackGenerator feedback_generator(
      {.network_config = {.link_capacity = DataRate::KilobitsPerSec(900)},
       .send_as_ect1 = false});  // Scream will react to delay increase, not CE.

  NetworkControllerConfig config(env);
  ScreamNetworkController scream_controller(config);

  StreamsConfig streams_config;
  streams_config.max_total_allocated_bitrate = DataRate::KilobitsPerSec(1000);
  scream_controller.OnStreamsConfig(streams_config);

  NetworkControlUpdate update;
  DataRate send_rate = DataRate::KilobitsPerSec(500);
  for (int i = 0; i < 30; ++i) {
    TransportPacketsFeedback feedback =
        feedback_generator.ProcessUntilNextFeedback(
            send_rate, clock, [&](const SentPacket& packet) {
              scream_controller.OnSentPacket(packet);
            });
    update = scream_controller.OnTransportPacketsFeedback(feedback);
    if (update.target_rate.has_value()) {
      send_rate = update.target_rate->target_rate;
    }
  }
  EXPECT_THAT(update.pacer_config,
              Optional(Field(&PacerConfig::time_window,
                             Lt(PacerConfig::kDefaultTimeInterval))));
}

TEST(ScreamControllerTest, PacingWindowNotReducedIfNotCongested) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  CcFeedbackGenerator feedback_generator(
      {.network_config = {.link_capacity = DataRate::KilobitsPerSec(9000)},
       .send_as_ect1 = false});  // Scream will react to delay increase, not CE.

  NetworkControllerConfig config(env);
  ScreamNetworkController scream_controller(config);

  StreamsConfig streams_config;
  streams_config.max_total_allocated_bitrate = DataRate::KilobitsPerSec(1000);
  scream_controller.OnStreamsConfig(streams_config);

  NetworkControlUpdate update;
  DataRate send_rate = DataRate::KilobitsPerSec(500);
  for (int i = 0; i < 30; ++i) {
    TransportPacketsFeedback feedback =
        feedback_generator.ProcessUntilNextFeedback(
            send_rate, clock, [&](const SentPacket& packet) {
              scream_controller.OnSentPacket(packet);
            });
    update = scream_controller.OnTransportPacketsFeedback(feedback);
    if (update.target_rate.has_value()) {
      send_rate = update.target_rate->target_rate;
    }
  }
  EXPECT_THAT(update.pacer_config,
              Optional(Field(&PacerConfig::time_window,
                             Eq(PacerConfig::kDefaultTimeInterval))));
}

TEST(ScreamControllerTest, InitiallyPaddingIsAllowedToReachNeededRate) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  NetworkControllerConfig config(env);
  ScreamNetworkController scream_controller(config);
  CcFeedbackGenerator feedback_generator(
      {.network_config = {.queue_delay_ms = 10,
                          .link_capacity = DataRate::KilobitsPerSec(5000)},
       .send_as_ect1 = true});
  StreamsConfig streams_config;
  streams_config.max_total_allocated_bitrate = DataRate::KilobitsPerSec(1000);
  scream_controller.OnStreamsConfig(streams_config);
  NetworkControlUpdate update = scream_controller.OnNetworkAvailability(
      {.at_time = clock.CurrentTime(), .network_available = true});

  DataRate send_rate = DataRate::KilobitsPerSec(50);
  DataRate target_rate = DataRate::Zero();
  bool padding_set = false;
  Timestamp padding_stop = Timestamp::Zero();
  Timestamp start_time = clock.CurrentTime();
  while (clock.CurrentTime() < start_time + TimeDelta::Seconds(1)) {
    TransportPacketsFeedback feedback =
        feedback_generator.ProcessUntilNextFeedback(
            send_rate, clock,
            [&](SentPacket packet) { scream_controller.OnSentPacket(packet); });
    update = scream_controller.OnTransportPacketsFeedback(feedback);
    if (update.pacer_config.has_value()) {
      if (update.pacer_config->pad_rate() != DataRate::Zero()) {
        padding_set = true;
        // Set the send rate equal to the padding rate.
        send_rate = update.pacer_config->pad_rate();
        // Pacing rate is rounded.
        EXPECT_GT(
            update.pacer_config->pad_rate(),
            update.target_rate->target_rate - DataRate::KilobitsPerSec(1));
        EXPECT_LT(
            update.pacer_config->pad_rate(),
            update.target_rate->target_rate + DataRate::KilobitsPerSec(1));
      } else if (padding_set && padding_stop.IsZero()) {
        padding_stop = clock.CurrentTime();
      }
    }
    if (update.target_rate) {
      target_rate = update.target_rate->target_rate;
    }
  }
  EXPECT_TRUE(padding_set);
  // Target rate should reach max needed rate.
  EXPECT_GE(target_rate, (*streams_config.max_total_allocated_bitrate));
  // But not much more, since seen data in flight should limit the target rate
  // increase.
  EXPECT_LE(target_rate, 1.5 * (*streams_config.max_total_allocated_bitrate));
  // Padding should stop when target is reached.
  EXPECT_LT(padding_stop - start_time, TimeDelta::Seconds(1));
}

TEST(ScreamControllerTest, InitialProbingWithoutMaxTotalAllocatedBitrate) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  NetworkControllerConfig config(env);
  config.stream_based_config.enable_repeated_initial_probing = true;
  config.constraints.starting_rate = DataRate::KilobitsPerSec(100);
  config.constraints.max_data_rate = DataRate::KilobitsPerSec(1000);
  // Do not set config.stream_based_config.max_total_allocated_bitrate

  ScreamNetworkController scream_controller(config);
  NetworkControlUpdate update = scream_controller.OnNetworkAvailability(
      {.at_time = clock.CurrentTime(), .network_available = true});

  ASSERT_TRUE(update.pacer_config.has_value());
  // Padding is allowed during the first 6 seconds even if
  // max_total_allocated_bitrate is zero.
  EXPECT_EQ(update.pacer_config->pad_rate(), config.constraints.starting_rate);

  // Advance clock past the 6s initial BWE probe window.
  clock.AdvanceTime(TimeDelta::Seconds(7));
  update =
      scream_controller.OnProcessInterval({.at_time = clock.CurrentTime()});
  // Since max_total_allocated_bitrate is not set, padding should stop after the
  // initial probe window.
  if (update.pacer_config.has_value()) {
    EXPECT_EQ(update.pacer_config->pad_rate(), DataRate::Zero());
  }
}

struct PaddingTestResult {
  DataRate target_rate;
  Timestamp padding_start;
  Timestamp padding_stop;
};

PaddingTestResult ProcessUntilPaddingStartAndStop(
    SimulatedClock& clock,
    ScreamNetworkController& scream_controller,
    CcFeedbackGenerator& feedback_generator,
    bool increase_send_rate = true) {
  DataRate target_rate = DataRate::Zero();
  Timestamp padding_start = Timestamp::Zero();
  Timestamp padding_stop = Timestamp::Zero();
  Timestamp start_time = clock.CurrentTime();
  DataRate send_rate = DataRate::KilobitsPerSec(50);

  while (clock.CurrentTime() < start_time + TimeDelta::Seconds(10)) {
    TransportPacketsFeedback feedback =
        feedback_generator.ProcessUntilNextFeedback(
            send_rate, clock,
            [&](SentPacket packet) { scream_controller.OnSentPacket(packet); });
    NetworkControlUpdate update =
        scream_controller.OnTransportPacketsFeedback(feedback);
    if (update.pacer_config.has_value()) {
      if (update.pacer_config->pad_rate() != DataRate::Zero()) {
        if (padding_start.IsZero()) {
          padding_start = clock.CurrentTime();
        }
        if (increase_send_rate) {
          // Set the send rate equal to the padding rate.
          send_rate = update.pacer_config->pad_rate();
        }
      } else if (!padding_start.IsZero() && padding_stop.IsZero()) {
        padding_stop = clock.CurrentTime();
      }
    }
    if (update.target_rate) {
      target_rate = update.target_rate->target_rate;
    }
    if (!padding_stop.IsZero()) {
      break;
    }
  }
  EXPECT_FALSE(padding_start.IsZero());
  EXPECT_FALSE(padding_stop.IsZero());
  return {.target_rate = target_rate,
          .padding_start = padding_start,
          .padding_stop = padding_stop};
}

TEST(ScreamControllerTest, PaddingStopIfNetworkCongested) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  NetworkControllerConfig config(env);
  ScreamNetworkController scream_controller(config);
  CcFeedbackGenerator feedback_generator(
      {.network_config = {.queue_delay_ms = 10,
                          .link_capacity = DataRate::KilobitsPerSec(500)},
       .send_as_ect1 = true});
  StreamsConfig streams_config;
  streams_config.max_total_allocated_bitrate = DataRate::KilobitsPerSec(1000);
  scream_controller.OnStreamsConfig(streams_config);

  PaddingTestResult result = ProcessUntilPaddingStartAndStop(
      clock, scream_controller, feedback_generator);

  EXPECT_LT(result.target_rate, DataRate::KilobitsPerSec(750));
  // Padding should stop when congestion is detected.
  EXPECT_LT(result.padding_stop - result.padding_start, TimeDelta::Seconds(1));
}

TEST(ScreamControllerTest, PeriodicallyAllowPadding) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  CcFeedbackGenerator feedback_generator(
      {.network_config = {.queue_delay_ms = 10,
                          .link_capacity = DataRate::KilobitsPerSec(15000)},
       .send_as_ect1 = true});

  NetworkControllerConfig config(env);
  ScreamNetworkController scream_controller(config);

  StreamsConfig streams_config;
  streams_config.max_total_allocated_bitrate = DataRate::KilobitsPerSec(1000);
  scream_controller.OnStreamsConfig(streams_config);

  PaddingTestResult result_1 = ProcessUntilPaddingStartAndStop(
      clock, scream_controller, feedback_generator,
      /*increase_send_rate=*/false);
  PaddingTestResult result_2 = ProcessUntilPaddingStartAndStop(
      clock, scream_controller, feedback_generator);

  TimeDelta padding_duration = result_1.padding_stop - result_1.padding_start;
  TimeDelta time_between_padding =
      result_2.padding_start - result_1.padding_stop;
  EXPECT_GT(padding_duration, TimeDelta::Millis(2500));
  EXPECT_LT(padding_duration, TimeDelta::Millis(3300));

  EXPECT_GT(time_between_padding, TimeDelta::Millis(2500));
  EXPECT_LT(time_between_padding, TimeDelta::Millis(3300));
}

TEST(ScreamControllerTest, DelayPaddingAfterCongestion) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  NetworkControllerConfig config(env);
  ScreamNetworkController scream_controller(config);
  CcFeedbackGenerator feedback_generator(
      {.network_config = {.queue_delay_ms = 10,
                          .link_capacity = DataRate::KilobitsPerSec(500)},
       .send_as_ect1 = true});
  StreamsConfig streams_config;
  streams_config.max_total_allocated_bitrate = DataRate::KilobitsPerSec(1000);
  scream_controller.OnStreamsConfig(streams_config);

  // Padding should start, but then stop since pushing to max allocated causes
  // congestion.
  PaddingTestResult result_1 = ProcessUntilPaddingStartAndStop(
      clock, scream_controller, feedback_generator,
      /*increase_send_rate=*/true);

  // Ensure it was stopped quickly when congestion kicked in.
  EXPECT_LT(result_1.padding_stop - result_1.padding_start,
            TimeDelta::Seconds(1));

  // Network recovers because increase_send_rate=false keeps sending rate at
  // 50kbps. Wait until next padding starts.
  PaddingTestResult result_2 = ProcessUntilPaddingStartAndStop(
      clock, scream_controller, feedback_generator,
      /*increase_send_rate=*/false);

  TimeDelta time_until_padding = result_2.padding_start - result_1.padding_stop;
  EXPECT_GT(time_until_padding, TimeDelta::Millis(2500));
  EXPECT_LT(time_until_padding, TimeDelta::Millis(3500));
}

TEST(ScreamControllerTest, PadsToMinOf2xCurrentMaxAndEverSeenMax) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  NetworkControllerConfig config(env);
  ScreamNetworkController scream_controller(config);
  CcFeedbackGenerator feedback_generator(
      {.network_config = {.queue_delay_ms = 50,
                          .link_capacity = DataRate::KilobitsPerSec(5000)},
       .send_as_ect1 = true});
  StreamsConfig streams_config;
  streams_config.max_total_allocated_bitrate = DataRate::KilobitsPerSec(1000);
  scream_controller.OnStreamsConfig(streams_config);
  // Even if max_total_allocated_bitrate is lowered, padding is still allowed up
  // to 2x the new max and previous max.
  streams_config.max_total_allocated_bitrate = DataRate::KilobitsPerSec(300);
  scream_controller.OnStreamsConfig(streams_config);

  PaddingTestResult result_1 = ProcessUntilPaddingStartAndStop(
      clock, scream_controller, feedback_generator);
  EXPECT_LT(result_1.target_rate, DataRate::KilobitsPerSec(700));

  streams_config.max_total_allocated_bitrate = DataRate::KilobitsPerSec(800);
  scream_controller.OnStreamsConfig(streams_config);

  PaddingTestResult result_2 = ProcessUntilPaddingStartAndStop(
      clock, scream_controller, feedback_generator);
  EXPECT_LT(result_2.target_rate, DataRate::KilobitsPerSec(1100));
}

TEST(ScreamControllerTest, CanSetStartBitrate) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  NetworkControllerConfig config(env);

  config.constraints.starting_rate = DataRate::KilobitsPerSec(3000);
  ScreamNetworkController scream_controller(config);
  CcFeedbackGenerator feedback_generator(
      {.network_config = {.queue_delay_ms = 50,
                          .link_capacity = DataRate::KilobitsPerSec(5000)}});

  TransportPacketsFeedback feedback =
      feedback_generator.ProcessUntilNextFeedback(
          /*send_rate=*/DataRate::KilobitsPerSec(100), clock,
          [&](SentPacket packet) { scream_controller.OnSentPacket(packet); });
  NetworkControlUpdate update =
      scream_controller.OnTransportPacketsFeedback(feedback);
  EXPECT_GE(update.target_rate->target_rate, DataRate::KilobitsPerSec(2980));
  EXPECT_LT(update.target_rate->target_rate, DataRate::KilobitsPerSec(3300));
}

TEST(ScreamControllerTest, IgnoreFeedbackWithoutReceivedPackets) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  NetworkControllerConfig config(env);
  config.constraints.starting_rate = DataRate::KilobitsPerSec(300);
  ScreamNetworkController scream_controller(config);

  TransportPacketsFeedback msg;
  msg.feedback_time = clock.CurrentTime();
  PacketResult result;
  result.sent_packet.send_time = clock.CurrentTime();
  result.sent_packet.sequence_number = 1;
  result.sent_packet.size = DataSize::Bytes(1000);
  ASSERT_FALSE(result.IsReceived());
  msg.packet_feedbacks.push_back(result);

  NetworkControlUpdate update =
      scream_controller.OnTransportPacketsFeedback(msg);
  // Scream should not change the target rate if there are no received packets.
  // Since this is the first feedback, the target rate should be the starting
  // rate.
  EXPECT_EQ(update.target_rate->target_rate, DataRate::KilobitsPerSec(300));
}

TEST(ScreamControllerTest, ReportsIsBandwidthLimited) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  NetworkControllerConfig config(env);
  config.constraints.starting_rate = DataRate::KilobitsPerSec(100);
  config.constraints.max_data_rate = DataRate::KilobitsPerSec(5000);
  ScreamNetworkController scream_controller(config);

  // Initially, is_bandwidth_limited should be true because
  // is_application_limited starts as false.
  NetworkControlUpdate update = scream_controller.OnNetworkAvailability(
      {.at_time = clock.CurrentTime(), .network_available = true});
  ASSERT_TRUE(update.target_rate.has_value());
  EXPECT_TRUE(update.target_rate->is_bandwidth_limited);

  CcFeedbackGenerator feedback_generator(
      {.network_config = {.queue_delay_ms = 10,
                          .link_capacity = DataRate::KilobitsPerSec(2000)}});

  DataRate target_rate = DataRate::KilobitsPerSec(100);
  // Send at full BWE rate so we are not application limited.
  for (int i = 0; i < 20; ++i) {
    TransportPacketsFeedback feedback =
        feedback_generator.ProcessUntilNextFeedback(
            target_rate, clock,
            [&](SentPacket packet) { scream_controller.OnSentPacket(packet); });
    update = scream_controller.OnTransportPacketsFeedback(feedback);
    if (update.target_rate.has_value()) {
      target_rate = update.target_rate->target_rate;
      EXPECT_TRUE(update.target_rate->is_bandwidth_limited);
    }
  }

  // Now send at a very low rate (e.g. 50kbps) while BWE remains high.
  // This should trigger application limited state.
  bool alr_detected = false;
  for (int i = 0; i < 50; ++i) {
    TransportPacketsFeedback feedback =
        feedback_generator.ProcessUntilNextFeedback(
            DataRate::KilobitsPerSec(50), clock,
            [&](SentPacket packet) { scream_controller.OnSentPacket(packet); });
    update = scream_controller.OnTransportPacketsFeedback(feedback);
    if (update.target_rate.has_value()) {
      if (!update.target_rate->is_bandwidth_limited) {
        alr_detected = true;
        break;
      }
    }
  }
  EXPECT_TRUE(alr_detected);
}

TEST(ScreamControllerTest, ReportsIsBandwidthLimitedEvenIfTargetRateClamped) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  NetworkControllerConfig config(env);
  config.constraints.starting_rate = DataRate::KilobitsPerSec(500);
  config.constraints.min_data_rate = DataRate::KilobitsPerSec(500);
  config.constraints.max_data_rate = DataRate::KilobitsPerSec(500);
  ScreamNetworkController scream_controller(config);

  NetworkControlUpdate update = scream_controller.OnNetworkAvailability(
      {.at_time = clock.CurrentTime(), .network_available = true});
  ASSERT_TRUE(update.target_rate.has_value());
  EXPECT_TRUE(update.target_rate->is_bandwidth_limited);
  EXPECT_EQ(update.target_rate->target_rate, DataRate::KilobitsPerSec(500));

  CcFeedbackGenerator feedback_generator(
      {.network_config = {.queue_delay_ms = 150,
                          .link_capacity = DataRate::KilobitsPerSec(2000)}});

  // Warm up to build up data in flight so that we are not application limited.
  for (int i = 0; i < 5; ++i) {
    TransportPacketsFeedback feedback =
        feedback_generator.ProcessUntilNextFeedback(
            DataRate::KilobitsPerSec(500), clock,
            [&](SentPacket packet) { scream_controller.OnSentPacket(packet); });
    scream_controller.OnTransportPacketsFeedback(feedback);
  }

  // Now we should be actively sending near capacity (is_bandwidth_limited =
  // true).
  for (int i = 0; i < 15; ++i) {
    TransportPacketsFeedback feedback =
        feedback_generator.ProcessUntilNextFeedback(
            DataRate::KilobitsPerSec(500), clock,
            [&](SentPacket packet) { scream_controller.OnSentPacket(packet); });
    update = scream_controller.OnTransportPacketsFeedback(feedback);
    if (update.target_rate.has_value()) {
      EXPECT_TRUE(update.target_rate->is_bandwidth_limited);
      EXPECT_EQ(update.target_rate->target_rate, DataRate::KilobitsPerSec(500));
    }
  }

  // Now send at a very low rate (50kbps).
  // The target rate is clamped at 500kbps so it cannot change.
  // We expect an update.target_rate to be generated anyway because
  // is_bandwidth_limited changes to false.
  bool alr_detected = false;
  for (int i = 0; i < 50; ++i) {
    TransportPacketsFeedback feedback =
        feedback_generator.ProcessUntilNextFeedback(
            DataRate::KilobitsPerSec(50), clock,
            [&](SentPacket packet) { scream_controller.OnSentPacket(packet); });
    update = scream_controller.OnTransportPacketsFeedback(feedback);
    if (update.target_rate.has_value()) {
      EXPECT_EQ(update.target_rate->target_rate, DataRate::KilobitsPerSec(500));
      if (!update.target_rate->is_bandwidth_limited) {
        alr_detected = true;
        break;
      }
    }
  }
  EXPECT_TRUE(alr_detected);
}

TEST(ScreamControllerTest, AlrRecoversDuringPeriodicPadding) {
  SimulatedClock clock(Timestamp::Zero());
  // Set periodic padding and padding duration to 1s so it aligns exactly with
  // warmup and activates repeatedly in the final loop.
  Environment env = CreateTestEnvironment({.time = &clock});
  CcFeedbackGenerator feedback_generator(CcFeedbackGenerator::Config{
      .network_config = {.queue_delay_ms = 50,
                         .link_capacity = DataRate::KilobitsPerSec(2000)}});

  NetworkControllerConfig controller_config(env);
  ScreamNetworkController scream_controller(controller_config);

  StreamsConfig streams_config;
  streams_config.max_total_allocated_bitrate = DataRate::KilobitsPerSec(10'000);
  scream_controller.OnStreamsConfig(streams_config);

  TargetRateConstraints rate_constraints;
  rate_constraints.max_data_rate = DataRate::KilobitsPerSec(10'000);
  rate_constraints.starting_rate = DataRate::KilobitsPerSec(300);
  scream_controller.OnTargetRateConstraints(rate_constraints);

  NetworkControlUpdate update = scream_controller.OnNetworkAvailability(
      {.at_time = clock.CurrentTime(), .network_available = true});
  DataRate target_rate = DataRate::KilobitsPerSec(300);

  // Warmup loop: allow BWE to adapt up to the 2 Mbps link capacity.
  Timestamp warmup_start = clock.CurrentTime();
  while (clock.CurrentTime() - warmup_start < TimeDelta::Seconds(10)) {
    TransportPacketsFeedback feedback =
        feedback_generator.ProcessUntilNextFeedback(
            target_rate, clock, [&](const SentPacket& packet) {
              scream_controller.OnSentPacket(packet);
            });
    update = scream_controller.OnTransportPacketsFeedback(feedback);
    if (update.target_rate.has_value()) {
      target_rate = update.target_rate->target_rate;
    }
  }
  EXPECT_GE(target_rate, DataRate::KilobitsPerSec(1800));
  EXPECT_LE(target_rate, DataRate::KilobitsPerSec(2100));

  // ALR entry: ensure we enter ALR by sending below capacity.
  bool alr_detected = false;
  Timestamp alr_start = clock.CurrentTime();
  while (clock.CurrentTime() - alr_start < TimeDelta::Seconds(1)) {
    TransportPacketsFeedback feedback =
        feedback_generator.ProcessUntilNextFeedback(
            DataRate::KilobitsPerSec(100), clock,
            [&](const SentPacket& packet) {
              scream_controller.OnSentPacket(packet);
            });
    update = scream_controller.OnTransportPacketsFeedback(feedback);
    if (update.target_rate.has_value()) {
      target_rate = update.target_rate->target_rate;
      if (!update.target_rate->is_bandwidth_limited) {
        alr_detected = true;
        break;
      }
    }
  }
  EXPECT_TRUE(alr_detected);

  // Recovery loop: create a new channel with higher capacity (10 Mbps) and
  // lower delay (5ms). Even though the application only sends at `target_rate`
  // (which was frozen by ALR), active periodic padding automatically unblocks
  // ALR, allowing BWE to adapt upwards close to the new 10 Mbps link capacity.
  CcFeedbackGenerator high_cap_feedback_generator(CcFeedbackGenerator::Config{
      .network_config = {.queue_delay_ms = 5,
                         .link_capacity = DataRate::KilobitsPerSec(10'000)}});

  bool alr_recovered = false;
  Timestamp recovery_start = clock.CurrentTime();
  DataRate padding_rate = DataRate::Zero();
  while (clock.CurrentTime() - recovery_start < TimeDelta::Seconds(5)) {
    TransportPacketsFeedback feedback =
        high_cap_feedback_generator.ProcessUntilNextFeedback(
            std::max(padding_rate, DataRate::KilobitsPerSec(100)), clock,
            [&](const SentPacket& packet) {
              scream_controller.OnSentPacket(packet);
            });
    update = scream_controller.OnTransportPacketsFeedback(feedback);
    if (update.target_rate.has_value()) {
      target_rate = update.target_rate->target_rate;
      if (update.target_rate->is_bandwidth_limited) {
        alr_recovered = true;
      }
    }
    if (update.pacer_config.has_value()) {
      padding_rate = update.pacer_config->pad_rate();
    }
  }

  EXPECT_TRUE(alr_recovered);
  EXPECT_GE(target_rate, DataRate::KilobitsPerSec(9000));
}

TEST(ScreamControllerTest, CwndReduceRatioSetWhenPacerQueueGrowsAndShrinks) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  NetworkControllerConfig config(env);
  config.constraints.starting_rate = DataRate::KilobitsPerSec(1000);
  ScreamNetworkController scream_controller(config);

  scream_controller.OnNetworkAvailability(
      {.at_time = clock.CurrentTime(), .network_available = true});

  // Starting rate is 1000 kbps, so pacing rate = 1.1 * 1000 kbps = 1100 kbps
  // (137,500 bytes/sec). Pacer queue of 41250 bytes gives (41250/137500) =
  // 300ms pacing delay. Expected ratio: (300ms - 100ms) / (500ms - 100ms) = 200
  // / 400 = 0.5.
  ProcessInterval msg1;
  msg1.at_time = clock.CurrentTime();
  msg1.pacer_queue = DataSize::Bytes(41250);

  NetworkControlUpdate update1 = scream_controller.OnProcessInterval(msg1);
  ASSERT_TRUE(update1.target_rate.has_value());
  EXPECT_NEAR(update1.target_rate->cwnd_reduce_ratio, 0.5, 0.05);

  // Send ProcessInterval with empty pacer queue -> cwnd_reduce_ratio drops back
  // to 0.0.
  clock.AdvanceTime(TimeDelta::Millis(100));
  ProcessInterval msg2;
  msg2.at_time = clock.CurrentTime();
  msg2.pacer_queue = DataSize::Zero();

  NetworkControlUpdate update2 = scream_controller.OnProcessInterval(msg2);
  ASSERT_TRUE(update2.target_rate.has_value());
  EXPECT_EQ(update2.target_rate->cwnd_reduce_ratio, 0.0);
}

TEST(ScreamControllerTest,
     TargetRateSetToZeroWhenCongestedAndQueueExceedsMaxPacingDelay) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  NetworkControllerConfig config(env);
  config.constraints.starting_rate = DataRate::KilobitsPerSec(1000);
  ScreamNetworkController scream_controller(config);

  scream_controller.OnNetworkAvailability(
      {.at_time = clock.CurrentTime(), .network_available = true});

  // Congestion: data_in_flight (12000 bytes) exceeds max_data_in_flight (~10000
  // bytes).
  SentPacket sent_packet;
  sent_packet.send_time = clock.CurrentTime();
  sent_packet.data_in_flight = DataSize::Bytes(12000);
  scream_controller.OnSentPacket(sent_packet);

  // Starting rate is 1000 kbps, so pacing rate = 1.1 * 1000 kbps = 1100 kbps
  // (137,500 bytes/sec). Max pacing delay is 500 ms (68,750 bytes). 600 ms
  // pacing delay = 0.6 * 137,500 = 82,500 bytes.
  ProcessInterval msg;
  msg.at_time = clock.CurrentTime();
  msg.pacer_queue = DataSize::Bytes(82500);

  NetworkControlUpdate update = scream_controller.OnProcessInterval(msg);
  ASSERT_TRUE(update.target_rate.has_value());
  EXPECT_EQ(update.target_rate->target_rate, DataRate::Zero());

  // Congestion clears (data_in_flight drops to 1000 bytes), but pacer queue is
  // still at 300 ms (41,250 bytes > min_pacing_delay of 100 ms).
  // Due to hysteresis, target rate remains zero.
  SentPacket clear_congestion;
  clear_congestion.send_time = clock.CurrentTime();
  clear_congestion.data_in_flight = DataSize::Bytes(1000);
  scream_controller.OnSentPacket(clear_congestion);

  ProcessInterval mid_queue_msg;
  mid_queue_msg.at_time = clock.CurrentTime();
  mid_queue_msg.pacer_queue = DataSize::Bytes(41250);
  NetworkControlUpdate update_mid =
      scream_controller.OnProcessInterval(mid_queue_msg);
  if (update_mid.target_rate.has_value()) {
    EXPECT_EQ(update_mid.target_rate->target_rate, DataRate::Zero());
  }

  // Once queue drains below min pacing delay (< 100 ms / 13,750 bytes),
  // target rate is restored.
  clock.AdvanceTime(TimeDelta::Millis(100));
  ProcessInterval drain_msg;
  drain_msg.at_time = clock.CurrentTime();
  drain_msg.pacer_queue = DataSize::Bytes(5000);
  NetworkControlUpdate update2 = scream_controller.OnProcessInterval(drain_msg);
  ASSERT_TRUE(update2.target_rate.has_value());
  EXPECT_GT(update2.target_rate->target_rate, DataRate::Zero());
}

TEST(ScreamControllerTest,
     TargetRateNotZeroWhenQueueExceedsMaxPacingDelayButNotCongested) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  NetworkControllerConfig config(env);
  config.constraints.starting_rate = DataRate::KilobitsPerSec(1000);
  ScreamNetworkController scream_controller(config);

  scream_controller.OnNetworkAvailability(
      {.at_time = clock.CurrentTime(), .network_available = true});

  // Not congested: data_in_flight (1000 bytes) < max_data_in_flight.
  SentPacket sent_packet;
  sent_packet.send_time = clock.CurrentTime();
  sent_packet.data_in_flight = DataSize::Bytes(1000);
  scream_controller.OnSentPacket(sent_packet);

  // 600 ms pacing delay = 82,500 bytes.
  ProcessInterval msg;
  msg.at_time = clock.CurrentTime();
  msg.pacer_queue = DataSize::Bytes(82500);

  NetworkControlUpdate update = scream_controller.OnProcessInterval(msg);
  ASSERT_TRUE(update.target_rate.has_value());
  EXPECT_GT(update.target_rate->target_rate, DataRate::Zero());
  EXPECT_EQ(update.target_rate->cwnd_reduce_ratio, 1.0);
}

TEST(ScreamControllerTest,
     TargetRateNotZeroWhenCongestedButQueueBelowMaxPacingDelay) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  NetworkControllerConfig config(env);
  config.constraints.starting_rate = DataRate::KilobitsPerSec(1000);
  ScreamNetworkController scream_controller(config);

  scream_controller.OnNetworkAvailability(
      {.at_time = clock.CurrentTime(), .network_available = true});

  // Congested: data_in_flight exceeds max_data_in_flight.
  SentPacket sent_packet;
  sent_packet.send_time = clock.CurrentTime();
  sent_packet.data_in_flight = DataSize::Bytes(12000);
  scream_controller.OnSentPacket(sent_packet);

  // 50 ms pacing delay = 0.05 * 137,500 = 6,875 bytes (< 500 ms).
  ProcessInterval msg;
  msg.at_time = clock.CurrentTime();
  msg.pacer_queue = DataSize::Bytes(6875);

  NetworkControlUpdate update = scream_controller.OnProcessInterval(msg);
  // Target rate is still > 0 (only cwnd_reduce_ratio is set).
  if (update.target_rate.has_value()) {
    EXPECT_GT(update.target_rate->target_rate, DataRate::Zero());
  }
}

TEST(ScreamControllerTest, PacingRateClampedToMinTotalAllocatedBitrate) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  NetworkControllerConfig config(env);
  config.constraints.starting_rate = DataRate::KilobitsPerSec(100);
  config.stream_based_config.min_total_allocated_bitrate =
      DataRate::KilobitsPerSec(200);
  config.stream_based_config.max_total_allocated_bitrate =
      DataRate::KilobitsPerSec(500);
  ScreamNetworkController scream_controller(config);

  NetworkControlUpdate update = scream_controller.OnNetworkAvailability(
      {.at_time = clock.CurrentTime(), .network_available = true});
  ASSERT_TRUE(update.pacer_config.has_value());
  // Pacing rate should be at least min_total_allocated_bitrate * kPacingFactor
  // = 200 * 1.1 = 220 kbps.
  EXPECT_EQ(update.pacer_config->data_window,
            DataRate::KilobitsPerSec(200) * kPacingFactor *
                PacerConfig::kDefaultTimeInterval);
}

TEST(ScreamControllerTest, PacingRateUpdatesOnStreamsConfigChange) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  NetworkControllerConfig config(env);
  config.constraints.starting_rate = DataRate::KilobitsPerSec(100);
  config.stream_based_config.max_total_allocated_bitrate =
      DataRate::KilobitsPerSec(500);
  ScreamNetworkController scream_controller(config);

  NetworkControlUpdate update = scream_controller.OnNetworkAvailability(
      {.at_time = clock.CurrentTime(), .network_available = true});
  ASSERT_TRUE(update.pacer_config.has_value());
  EXPECT_EQ(update.pacer_config->data_window,
            DataRate::KilobitsPerSec(100) * kPacingFactor *
                PacerConfig::kDefaultTimeInterval);

  StreamsConfig streams_config;
  streams_config.at_time = clock.CurrentTime();
  streams_config.min_total_allocated_bitrate = DataRate::KilobitsPerSec(300);
  streams_config.max_total_allocated_bitrate = DataRate::KilobitsPerSec(500);

  NetworkControlUpdate stream_update =
      scream_controller.OnStreamsConfig(streams_config);
  ASSERT_TRUE(stream_update.pacer_config.has_value());
  EXPECT_EQ(stream_update.pacer_config->data_window,
            DataRate::KilobitsPerSec(300) * kPacingFactor *
                PacerConfig::kDefaultTimeInterval);
}

void TestClampsRefWindowOnFirstCongestionWhenStartingRateIsHigh(
    bool send_as_ect1) {
  SimulatedClock clock(Timestamp::Seconds(1'234));
  Environment env = CreateTestEnvironment({.time = &clock});
  NetworkControllerConfig config(env);
  config.constraints.starting_rate = DataRate::KilobitsPerSec(300);
  config.constraints.min_data_rate = DataRate::KilobitsPerSec(20);
  config.constraints.max_data_rate = DataRate::KilobitsPerSec(1000);
  config.stream_based_config.max_total_allocated_bitrate =
      DataRate::KilobitsPerSec(1000);
  ScreamNetworkController scream_controller(config);
  scream_controller.OnNetworkAvailability(
      {.at_time = clock.CurrentTime(), .network_available = true});

  // RTT is 200ms (100ms one-way delay), link capacity is 30 kbps.
  CcFeedbackGenerator feedback_generator({
      .network_config = {.queue_delay_ms = 100,
                         .link_capacity = DataRate::KilobitsPerSec(30)},
      .send_as_ect1 = send_as_ect1,
      .packet_size = DataSize::Bytes(150),
  });

  TimeDelta rtt = TimeDelta::Millis(200);
  Timestamp start_time = clock.CurrentTime();
  DataRate target_rate = config.constraints.starting_rate.value();
  while (target_rate > DataRate::KilobitsPerSec(60) &&
         clock.CurrentTime() - start_time < 10 * rtt) {
    TransportPacketsFeedback feedback =
        feedback_generator.ProcessUntilNextFeedback(
            target_rate, clock, [&](const SentPacket& packet) {
              scream_controller.OnSentPacket(packet);
            });
    NetworkControlUpdate update =
        scream_controller.OnTransportPacketsFeedback(feedback);
    if (update.target_rate.has_value()) {
      target_rate = update.target_rate->target_rate;
    }
  }
  EXPECT_LE(target_rate, DataRate::KilobitsPerSec(60));
  EXPECT_GE(target_rate, DataRate::KilobitsPerSec(20));
  EXPECT_LE(clock.CurrentTime() - start_time, 2 * rtt);
}

TEST(ScreamControllerTest,
     ClampsRefWindowOnFirstDelayCongestionWhenStartingRateIsHigh) {
  TestClampsRefWindowOnFirstCongestionWhenStartingRateIsHigh(
      /*send_as_ect1=*/false);
}

TEST(ScreamControllerTest,
     ClampsRefWindowOnFirstCeCongestionWhenStartingRateIsHigh) {
  TestClampsRefWindowOnFirstCongestionWhenStartingRateIsHigh(
      /*send_as_ect1=*/true);
}

}  // namespace
}  // namespace webrtc
