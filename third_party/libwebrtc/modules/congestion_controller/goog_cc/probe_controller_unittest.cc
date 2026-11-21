/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "modules/congestion_controller/goog_cc/probe_controller.h"

#include <memory>
#include <vector>

#include "absl/strings/string_view.h"
#include "api/transport/network_types.h"
#include "api/units/data_rate.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "logging/rtc_event_log/mock/mock_rtc_event_log.h"
#include "system_wrappers/include/clock.h"
#include "test/explicit_key_value_config.h"
#include "test/gmock.h"
#include "test/gtest.h"

using ::testing::Gt;
using ::testing::IsEmpty;
using ::testing::NiceMock;
using ::testing::SizeIs;

namespace webrtc {
namespace test {

namespace {

constexpr DataRate kMinBitrate = DataRate::BitsPerSec(100);
constexpr DataRate kStartBitrate = DataRate::BitsPerSec(300);
constexpr DataRate kMaxBitrate = DataRate::BitsPerSec(10000);

constexpr TimeDelta kExponentialProbingTimeout = TimeDelta::Seconds(5);

constexpr TimeDelta kAlrProbeInterval = TimeDelta::Seconds(5);
constexpr TimeDelta kAlrEndedTimeout = TimeDelta::Seconds(3);
constexpr TimeDelta kBitrateDropTimeout = TimeDelta::Seconds(5);
}  // namespace

class ProbeControllerFixture {
 public:
  explicit ProbeControllerFixture(absl::string_view field_trials = "")
      : field_trial_config_(field_trials), clock_(100000000L) {}

  std::unique_ptr<ProbeController> CreateController() {
    return std::make_unique<ProbeController>(&field_trial_config_,
                                             &mock_rtc_event_log);
  }

  Timestamp CurrentTime() { return clock_.CurrentTime(); }
  void AdvanceTime(TimeDelta delta) { clock_.AdvanceTime(delta); }

  ExplicitKeyValueConfig field_trial_config_;
  SimulatedClock clock_;
  NiceMock<MockRtcEventLog> mock_rtc_event_log;
};

TEST(ProbeControllerTest, InitiatesProbingAfterSetBitrates) {
  ProbeControllerFixture fixture;
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());

  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  EXPECT_GE(probes.size(), 2u);
}

TEST(ProbeControllerTest, InitiatesProbingWhenNetworkAvailable) {
  ProbeControllerFixture fixture;
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();

  std::vector<ProbeClusterConfig> probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  EXPECT_THAT(probes, IsEmpty());
  probes = probe_controller->OnNetworkAvailability({.network_available = true});
  EXPECT_GE(probes.size(), 2u);
}

TEST(ProbeControllerTest, SetsDefaultTargetDurationAndTargetProbeCount) {
  ProbeControllerFixture fixture;
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  std::vector<ProbeClusterConfig> probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  ASSERT_GE(probes.size(), 2u);

  EXPECT_EQ(probes[0].target_duration, TimeDelta::Millis(15));
  EXPECT_EQ(probes[0].target_probe_count, 5);
}

TEST(ProbeControllerTest,
     FieldTrialsOverrideDefaultTargetDurationAndTargetProbeCount) {
  ProbeControllerFixture fixture(
      "WebRTC-Bwe-ProbingBehavior/"
      "min_probe_packets_sent:2,min_probe_duration:123ms/");
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  std::vector<ProbeClusterConfig> probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  ASSERT_GE(probes.size(), 2u);

  EXPECT_EQ(probes[0].target_duration, TimeDelta::Millis(123));
  EXPECT_EQ(probes[0].target_probe_count, 2);
}

TEST(ProbeControllerTest, ProbeOnlyWhenNetworkIsUp) {
  ProbeControllerFixture fixture;
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  auto probes = probe_controller->OnNetworkAvailability(
      {.at_time = fixture.CurrentTime(), .network_available = false});
  probes = probe_controller->SetBitrates(kMinBitrate, kStartBitrate,
                                         kMaxBitrate, fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());
  probes = probe_controller->OnNetworkAvailability(
      {.at_time = fixture.CurrentTime(), .network_available = true});
  EXPECT_GE(probes.size(), 2u);
}

TEST(ProbeControllerTest, CanConfigureInitialProbeRateFactor) {
  ProbeControllerFixture fixture("WebRTC-Bwe-ProbingConfiguration/p1:2,p2:3/");
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  EXPECT_EQ(probes.size(), 2u);
  EXPECT_EQ(probes[0].target_data_rate, kStartBitrate * 2);
  EXPECT_EQ(probes[1].target_data_rate, kStartBitrate * 3);
}

TEST(ProbeControllerTest, DisableSecondInitialProbeIfRateFactorZero) {
  ProbeControllerFixture fixture("WebRTC-Bwe-ProbingConfiguration/p1:2,p2:0/");
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  EXPECT_EQ(probes.size(), 1u);
  EXPECT_EQ(probes[0].target_data_rate, kStartBitrate * 2);
}

TEST(ProbeControllerTest, InitiatesProbingOnMaxBitrateIncrease) {
  ProbeControllerFixture fixture;
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  // Long enough to time out exponential probing.
  fixture.AdvanceTime(kExponentialProbingTimeout);
  probes = probe_controller->SetEstimatedBitrate(
      kStartBitrate, BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  probes = probe_controller->Process(fixture.CurrentTime());
  probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate + DataRate::BitsPerSec(100),
      fixture.CurrentTime());
  EXPECT_EQ(probes.size(), 1u);
  EXPECT_EQ(probes[0].target_data_rate.bps(), kMaxBitrate.bps() + 100);
}

TEST(ProbeControllerTest, ProbesOnMaxAllocatedBitrateIncreaseOnlyWhenInAlr) {
  ProbeControllerFixture fixture;
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  probes = probe_controller->SetEstimatedBitrate(
      kMaxBitrate - DataRate::BitsPerSec(1),
      BandwidthLimitedCause::kDelayBasedLimited, fixture.CurrentTime());

  // Wait long enough to time out exponential probing.
  fixture.AdvanceTime(kExponentialProbingTimeout);
  probes = probe_controller->Process(fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());

  // Probe when in alr.
  probe_controller->SetAlrStartTimeMs(fixture.CurrentTime().ms());
  probes = probe_controller->OnMaxTotalAllocatedBitrate(
      kMaxBitrate + DataRate::BitsPerSec(1), fixture.CurrentTime());
  EXPECT_EQ(probes.size(), 2u);
  EXPECT_EQ(probes.at(0).target_data_rate, kMaxBitrate);

  // Do not probe when not in alr.
  probe_controller->SetAlrStartTimeMs(absl::nullopt);
  probes = probe_controller->OnMaxTotalAllocatedBitrate(
      kMaxBitrate + DataRate::BitsPerSec(2), fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());
}

TEST(ProbeControllerTest, ProbesOnMaxAllocatedBitrateLimitedByCurrentBwe) {
  ProbeControllerFixture fixture(
      "WebRTC-Bwe-ProbingConfiguration/"
      "alloc_current_bwe_limit:1.5/");
  ASSERT_TRUE(kMaxBitrate > 1.5 * kStartBitrate);
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  probes = probe_controller->SetEstimatedBitrate(
      kStartBitrate, BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());

  // Wait long enough to time out exponential probing.
  fixture.AdvanceTime(kExponentialProbingTimeout);
  probes = probe_controller->Process(fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());

  // Probe when in alr.
  probe_controller->SetAlrStartTimeMs(fixture.CurrentTime().ms());
  probes = probe_controller->OnMaxTotalAllocatedBitrate(kMaxBitrate,
                                                        fixture.CurrentTime());
  EXPECT_EQ(probes.size(), 1u);
  EXPECT_EQ(probes.at(0).target_data_rate, 1.5 * kStartBitrate);

  // Continue probing if probe succeeds.
  probes = probe_controller->SetEstimatedBitrate(
      1.5 * kStartBitrate, BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  EXPECT_EQ(probes.size(), 1u);
  EXPECT_GT(probes.at(0).target_data_rate, 1.5 * kStartBitrate);
}

TEST(ProbeControllerTest, CanDisableProbingOnMaxTotalAllocatedBitrateIncrease) {
  ProbeControllerFixture fixture(
      "WebRTC-Bwe-ProbingConfiguration/"
      "probe_max_allocation:false/");
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  probes = probe_controller->SetEstimatedBitrate(
      kMaxBitrate - DataRate::BitsPerSec(1),
      BandwidthLimitedCause::kDelayBasedLimited, fixture.CurrentTime());
  fixture.AdvanceTime(kExponentialProbingTimeout);
  probes = probe_controller->Process(fixture.CurrentTime());
  ASSERT_TRUE(probes.empty());
  probe_controller->SetAlrStartTimeMs(fixture.CurrentTime().ms());

  // Do no probe, since probe_max_allocation:false.
  probe_controller->SetAlrStartTimeMs(fixture.CurrentTime().ms());
  probes = probe_controller->OnMaxTotalAllocatedBitrate(
      kMaxBitrate + DataRate::BitsPerSec(1), fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());
}

TEST(ProbeControllerTest, InitiatesProbingOnMaxBitrateIncreaseAtMaxBitrate) {
  ProbeControllerFixture fixture;
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  // Long enough to time out exponential probing.
  fixture.AdvanceTime(kExponentialProbingTimeout);
  probes = probe_controller->SetEstimatedBitrate(
      kStartBitrate, BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  probes = probe_controller->Process(fixture.CurrentTime());
  probes = probe_controller->SetEstimatedBitrate(
      kMaxBitrate, BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate + DataRate::BitsPerSec(100),
      fixture.CurrentTime());
  EXPECT_EQ(probes.size(), 1u);
  EXPECT_EQ(probes[0].target_data_rate,
            kMaxBitrate + DataRate::BitsPerSec(100));
}

TEST(ProbeControllerTest, TestExponentialProbing) {
  ProbeControllerFixture fixture;
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());

  // Repeated probe should only be sent when estimated bitrate climbs above
  // 0.7 * 6 * kStartBitrate = 1260.
  probes = probe_controller->SetEstimatedBitrate(
      DataRate::BitsPerSec(1000), BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());

  probes = probe_controller->SetEstimatedBitrate(
      DataRate::BitsPerSec(1800), BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  EXPECT_EQ(probes.size(), 1u);
  EXPECT_EQ(probes[0].target_data_rate.bps(), 2 * 1800);
}

TEST(ProbeControllerTest, ExponentialProbingStopIfMaxAllocatedBitrateLow) {
  ProbeControllerFixture fixture(
      "WebRTC-Bwe-ProbingConfiguration/abort_further:true/");
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  ASSERT_THAT(probes, SizeIs(Gt(0)));

  // Repeated probe normally is sent when estimated bitrate climbs above
  // 0.7 * 6 * kStartBitrate = 1260. But since allocated bitrate i slow, expect
  // exponential probing to stop.
  probes = probe_controller->OnMaxTotalAllocatedBitrate(kStartBitrate,
                                                        fixture.CurrentTime());
  EXPECT_THAT(probes, IsEmpty());

  probes = probe_controller->SetEstimatedBitrate(
      DataRate::BitsPerSec(1800), BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  EXPECT_THAT(probes, IsEmpty());
}

TEST(ProbeControllerTest, TestExponentialProbingTimeout) {
  ProbeControllerFixture fixture;
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  // Advance far enough to cause a time out in waiting for probing result.
  fixture.AdvanceTime(kExponentialProbingTimeout);
  probes = probe_controller->Process(fixture.CurrentTime());

  probes = probe_controller->SetEstimatedBitrate(
      DataRate::BitsPerSec(1800), BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());
}

TEST(ProbeControllerTest, RequestProbeInAlr) {
  ProbeControllerFixture fixture;
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  EXPECT_GE(probes.size(), 2u);
  probes = probe_controller->SetEstimatedBitrate(
      DataRate::BitsPerSec(500), BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());

  probe_controller->SetAlrStartTimeMs(fixture.CurrentTime().ms());
  fixture.AdvanceTime(kAlrProbeInterval + TimeDelta::Millis(1));
  probes = probe_controller->Process(fixture.CurrentTime());
  probes = probe_controller->SetEstimatedBitrate(
      DataRate::BitsPerSec(250), BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  probes = probe_controller->RequestProbe(fixture.CurrentTime());

  EXPECT_EQ(probes.size(), 1u);
  EXPECT_EQ(probes[0].target_data_rate.bps(), 0.85 * 500);
}

TEST(ProbeControllerTest, RequestProbeWhenAlrEndedRecently) {
  ProbeControllerFixture fixture;
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  EXPECT_EQ(probes.size(), 2u);
  probes = probe_controller->SetEstimatedBitrate(
      DataRate::BitsPerSec(500), BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());

  probe_controller->SetAlrStartTimeMs(absl::nullopt);
  fixture.AdvanceTime(kAlrProbeInterval + TimeDelta::Millis(1));
  probes = probe_controller->Process(fixture.CurrentTime());
  probes = probe_controller->SetEstimatedBitrate(
      DataRate::BitsPerSec(250), BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  probe_controller->SetAlrEndedTimeMs(fixture.CurrentTime().ms());
  fixture.AdvanceTime(kAlrEndedTimeout - TimeDelta::Millis(1));
  probes = probe_controller->RequestProbe(fixture.CurrentTime());

  EXPECT_EQ(probes.size(), 1u);
  EXPECT_EQ(probes[0].target_data_rate.bps(), 0.85 * 500);
}

TEST(ProbeControllerTest, RequestProbeWhenAlrNotEndedRecently) {
  ProbeControllerFixture fixture;
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  EXPECT_EQ(probes.size(), 2u);
  probes = probe_controller->SetEstimatedBitrate(
      DataRate::BitsPerSec(500), BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());

  probe_controller->SetAlrStartTimeMs(absl::nullopt);
  fixture.AdvanceTime(kAlrProbeInterval + TimeDelta::Millis(1));
  probes = probe_controller->Process(fixture.CurrentTime());
  probes = probe_controller->SetEstimatedBitrate(
      DataRate::BitsPerSec(250), BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  probe_controller->SetAlrEndedTimeMs(fixture.CurrentTime().ms());
  fixture.AdvanceTime(kAlrEndedTimeout + TimeDelta::Millis(1));
  probes = probe_controller->RequestProbe(fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());
}

TEST(ProbeControllerTest, RequestProbeWhenBweDropNotRecent) {
  ProbeControllerFixture fixture;
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  EXPECT_EQ(probes.size(), 2u);
  probes = probe_controller->SetEstimatedBitrate(
      DataRate::BitsPerSec(500), BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());

  probe_controller->SetAlrStartTimeMs(fixture.CurrentTime().ms());
  fixture.AdvanceTime(kAlrProbeInterval + TimeDelta::Millis(1));
  probes = probe_controller->Process(fixture.CurrentTime());
  probes = probe_controller->SetEstimatedBitrate(
      DataRate::BitsPerSec(250), BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  fixture.AdvanceTime(kBitrateDropTimeout + TimeDelta::Millis(1));
  probes = probe_controller->RequestProbe(fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());
}

TEST(ProbeControllerTest, PeriodicProbing) {
  ProbeControllerFixture fixture;
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  probe_controller->EnablePeriodicAlrProbing(true);
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  EXPECT_EQ(probes.size(), 2u);
  probes = probe_controller->SetEstimatedBitrate(
      DataRate::BitsPerSec(500), BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());

  Timestamp start_time = fixture.CurrentTime();

  // Expect the controller to send a new probe after 5s has passed.
  probe_controller->SetAlrStartTimeMs(start_time.ms());
  fixture.AdvanceTime(TimeDelta::Seconds(5));
  probes = probe_controller->Process(fixture.CurrentTime());
  EXPECT_EQ(probes.size(), 1u);
  EXPECT_EQ(probes[0].target_data_rate.bps(), 1000);

  probes = probe_controller->SetEstimatedBitrate(
      DataRate::BitsPerSec(500), BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());

  // The following probe should be sent at 10s into ALR.
  probe_controller->SetAlrStartTimeMs(start_time.ms());
  fixture.AdvanceTime(TimeDelta::Seconds(4));
  probes = probe_controller->Process(fixture.CurrentTime());
  probes = probe_controller->SetEstimatedBitrate(
      DataRate::BitsPerSec(500), BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());

  probe_controller->SetAlrStartTimeMs(start_time.ms());
  fixture.AdvanceTime(TimeDelta::Seconds(1));
  probes = probe_controller->Process(fixture.CurrentTime());
  EXPECT_EQ(probes.size(), 1u);
  probes = probe_controller->SetEstimatedBitrate(
      DataRate::BitsPerSec(500), BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());
}

TEST(ProbeControllerTest, PeriodicProbingAfterReset) {
  ProbeControllerFixture fixture;
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  Timestamp alr_start_time = fixture.CurrentTime();

  probe_controller->SetAlrStartTimeMs(alr_start_time.ms());
  probe_controller->EnablePeriodicAlrProbing(true);
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  probe_controller->Reset(fixture.CurrentTime());

  fixture.AdvanceTime(TimeDelta::Seconds(10));
  probes = probe_controller->Process(fixture.CurrentTime());
  // Since bitrates are not yet set, no probe is sent event though we are in ALR
  // mode.
  EXPECT_TRUE(probes.empty());

  probes = probe_controller->SetBitrates(kMinBitrate, kStartBitrate,
                                         kMaxBitrate, fixture.CurrentTime());
  EXPECT_EQ(probes.size(), 2u);

  // Make sure we use `kStartBitrateBps` as the estimated bitrate
  // until SetEstimatedBitrate is called with an updated estimate.
  fixture.AdvanceTime(TimeDelta::Seconds(10));
  probes = probe_controller->Process(fixture.CurrentTime());
  EXPECT_EQ(probes.size(), 1u);
  EXPECT_EQ(probes[0].target_data_rate, kStartBitrate * 2);
}

TEST(ProbeControllerTest, NoProbesWhenTransportIsNotWritable) {
  ProbeControllerFixture fixture;
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  probe_controller->EnablePeriodicAlrProbing(true);

  std::vector<ProbeClusterConfig> probes =
      probe_controller->OnNetworkAvailability({.network_available = false});
  EXPECT_THAT(probes, IsEmpty());
  EXPECT_THAT(probe_controller->SetBitrates(kMinBitrate, kStartBitrate,
                                            kMaxBitrate, fixture.CurrentTime()),
              IsEmpty());
  fixture.AdvanceTime(TimeDelta::Seconds(10));
  EXPECT_THAT(probe_controller->Process(fixture.CurrentTime()), IsEmpty());

  // Controller is reset after a network route change.
  // But, a probe should not be sent since the transport is not writable.
  // Transport is not writable until after DTLS negotiation completes.
  // However, the bitrate constraints may change.
  probe_controller->Reset(fixture.CurrentTime());
  EXPECT_THAT(
      probe_controller->SetBitrates(2 * kMinBitrate, 2 * kStartBitrate,
                                    2 * kMaxBitrate, fixture.CurrentTime()),
      IsEmpty());
  fixture.AdvanceTime(TimeDelta::Seconds(10));
  EXPECT_THAT(probe_controller->Process(fixture.CurrentTime()), IsEmpty());
}

TEST(ProbeControllerTest, TestExponentialProbingOverflow) {
  ProbeControllerFixture fixture;
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  const DataRate kMbpsMultiplier = DataRate::KilobitsPerSec(1000);
  auto probes = probe_controller->SetBitrates(kMinBitrate, 10 * kMbpsMultiplier,
                                              100 * kMbpsMultiplier,
                                              fixture.CurrentTime());
  // Verify that probe bitrate is capped at the specified max bitrate.
  probes = probe_controller->SetEstimatedBitrate(
      60 * kMbpsMultiplier, BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  EXPECT_EQ(probes.size(), 1u);
  EXPECT_EQ(probes[0].target_data_rate, 100 * kMbpsMultiplier);
  // Verify that repeated probes aren't sent.
  probes = probe_controller->SetEstimatedBitrate(
      100 * kMbpsMultiplier, BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());
}

TEST(ProbeControllerTest, TestAllocatedBitrateCap) {
  ProbeControllerFixture fixture;
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  const DataRate kMbpsMultiplier = DataRate::KilobitsPerSec(1000);
  const DataRate kMaxBitrate = 100 * kMbpsMultiplier;
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, 10 * kMbpsMultiplier, kMaxBitrate, fixture.CurrentTime());

  // Configure ALR for periodic probing.
  probe_controller->EnablePeriodicAlrProbing(true);
  Timestamp alr_start_time = fixture.CurrentTime();
  probe_controller->SetAlrStartTimeMs(alr_start_time.ms());

  DataRate estimated_bitrate = kMaxBitrate / 10;
  probes = probe_controller->SetEstimatedBitrate(
      estimated_bitrate, BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());

  // Set a max allocated bitrate below the current estimate.
  DataRate max_allocated = estimated_bitrate - 1 * kMbpsMultiplier;
  probes = probe_controller->OnMaxTotalAllocatedBitrate(max_allocated,
                                                        fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());  // No probe since lower than current max.

  // Probes such as ALR capped at 2x the max allocation limit.
  fixture.AdvanceTime(TimeDelta::Seconds(5));
  probes = probe_controller->Process(fixture.CurrentTime());
  EXPECT_EQ(probes.size(), 1u);
  EXPECT_EQ(probes[0].target_data_rate, 2 * max_allocated);

  // Remove allocation limit.
  EXPECT_TRUE(
      probe_controller
          ->OnMaxTotalAllocatedBitrate(DataRate::Zero(), fixture.CurrentTime())
          .empty());
  fixture.AdvanceTime(TimeDelta::Seconds(5));
  probes = probe_controller->Process(fixture.CurrentTime());
  EXPECT_EQ(probes.size(), 1u);
  EXPECT_EQ(probes[0].target_data_rate, estimated_bitrate * 2);
}

TEST(ProbeControllerTest, ConfigurableProbingFieldTrial) {
  ProbeControllerFixture fixture(
      "WebRTC-Bwe-ProbingConfiguration/"
      "p1:2,p2:5,step_size:3,further_probe_threshold:0.8,"
      "alloc_p1:2,alloc_p2,min_probe_packets_sent:2/");
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());

  auto probes = probe_controller->SetBitrates(kMinBitrate, kStartBitrate,
                                              DataRate::KilobitsPerSec(5000),
                                              fixture.CurrentTime());
  EXPECT_EQ(probes.size(), 2u);
  EXPECT_EQ(probes[0].target_data_rate.bps(), 600);
  EXPECT_EQ(probes[0].target_probe_count, 2);
  EXPECT_EQ(probes[1].target_data_rate.bps(), 1500);
  EXPECT_EQ(probes[1].target_probe_count, 2);

  // Repeated probe should only be sent when estimated bitrate climbs above
  // 0.8 * 5 * kStartBitrateBps = 1200.
  probes = probe_controller->SetEstimatedBitrate(
      DataRate::BitsPerSec(1100), BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  EXPECT_EQ(probes.size(), 0u);

  probes = probe_controller->SetEstimatedBitrate(
      DataRate::BitsPerSec(1250), BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  EXPECT_EQ(probes.size(), 1u);
  EXPECT_EQ(probes[0].target_data_rate.bps(), 3 * 1250);

  fixture.AdvanceTime(TimeDelta::Seconds(5));
  probes = probe_controller->Process(fixture.CurrentTime());

  probe_controller->SetAlrStartTimeMs(fixture.CurrentTime().ms());
  probes = probe_controller->OnMaxTotalAllocatedBitrate(
      DataRate::KilobitsPerSec(200), fixture.CurrentTime());
  EXPECT_EQ(probes.size(), 1u);
  EXPECT_EQ(probes[0].target_data_rate.bps(), 400'000);
}

TEST(ProbeControllerTest, LimitAlrProbeWhenLossBasedBweLimited) {
  ProbeControllerFixture fixture;
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  probe_controller->EnablePeriodicAlrProbing(true);
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  probes = probe_controller->SetEstimatedBitrate(
      DataRate::BitsPerSec(500), BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  // Expect the controller to send a new probe after 5s has passed.
  probe_controller->SetAlrStartTimeMs(fixture.CurrentTime().ms());
  fixture.AdvanceTime(TimeDelta::Seconds(5));
  probes = probe_controller->Process(fixture.CurrentTime());
  ASSERT_EQ(probes.size(), 1u);

  probes = probe_controller->SetEstimatedBitrate(
      DataRate::BitsPerSec(500),
      BandwidthLimitedCause::kLossLimitedBweIncreasing, fixture.CurrentTime());
  fixture.AdvanceTime(TimeDelta::Seconds(6));
  probes = probe_controller->Process(fixture.CurrentTime());
  ASSERT_EQ(probes.size(), 1u);
  EXPECT_EQ(probes[0].target_data_rate, 1.5 * DataRate::BitsPerSec(500));

  probes = probe_controller->SetEstimatedBitrate(
      1.5 * DataRate::BitsPerSec(500),
      BandwidthLimitedCause::kDelayBasedLimited, fixture.CurrentTime());
  fixture.AdvanceTime(TimeDelta::Seconds(6));
  probes = probe_controller->Process(fixture.CurrentTime());
  ASSERT_FALSE(probes.empty());
  EXPECT_GT(probes[0].target_data_rate, 1.5 * 1.5 * DataRate::BitsPerSec(500));
}

TEST(ProbeControllerTest, PeriodicProbeAtUpperNetworkStateEstimate) {
  ProbeControllerFixture fixture(
      "WebRTC-Bwe-ProbingConfiguration/network_state_interval:5s/");
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());

  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  probes = probe_controller->SetEstimatedBitrate(
      DataRate::BitsPerSec(5000), BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  // Expect the controller to send a new probe after 5s has passed.
  NetworkStateEstimate state_estimate;
  state_estimate.link_capacity_upper = DataRate::KilobitsPerSec(6);
  probe_controller->SetNetworkStateEstimate(state_estimate);

  fixture.AdvanceTime(TimeDelta::Seconds(5));
  probes = probe_controller->Process(fixture.CurrentTime());
  ASSERT_EQ(probes.size(), 1u);
  EXPECT_EQ(probes[0].target_data_rate, state_estimate.link_capacity_upper);
  fixture.AdvanceTime(TimeDelta::Seconds(5));
  probes = probe_controller->Process(fixture.CurrentTime());
  ASSERT_EQ(probes.size(), 1u);
  EXPECT_EQ(probes[0].target_data_rate, state_estimate.link_capacity_upper);
}

TEST(ProbeControllerTest,
     LimitProbeAtUpperNetworkStateEstimateIfLossBasedLimited) {
  ProbeControllerFixture fixture(
      "WebRTC-Bwe-ProbingConfiguration/"
      "network_state_interval:5s/");
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());

  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  probes = probe_controller->SetEstimatedBitrate(
      DataRate::BitsPerSec(500), BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  // Expect the controller to send a new probe after 5s has passed.
  NetworkStateEstimate state_estimate;
  state_estimate.link_capacity_upper = DataRate::BitsPerSec(700);
  probe_controller->SetNetworkStateEstimate(state_estimate);
  fixture.AdvanceTime(TimeDelta::Seconds(5));
  probes = probe_controller->Process(fixture.CurrentTime());
  ASSERT_EQ(probes.size(), 1u);

  probes = probe_controller->SetEstimatedBitrate(
      DataRate::BitsPerSec(500),
      BandwidthLimitedCause::kLossLimitedBweIncreasing, fixture.CurrentTime());
  // Expect the controller to send a new probe after 5s has passed.
  fixture.AdvanceTime(TimeDelta::Seconds(5));
  probes = probe_controller->Process(fixture.CurrentTime());
  ASSERT_FALSE(probes.empty());
  EXPECT_EQ(probes[0].target_data_rate, DataRate::BitsPerSec(700));
}

TEST(ProbeControllerTest, AlrProbesLimitedByNetworkStateEstimate) {
  ProbeControllerFixture fixture(
      "WebRTC-Bwe-ProbingConfiguration/network_state_interval:5s/");
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  probe_controller->EnablePeriodicAlrProbing(true);
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  probes = probe_controller->SetEstimatedBitrate(
      DataRate::KilobitsPerSec(6), BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  probe_controller->SetAlrStartTimeMs(fixture.CurrentTime().ms());

  fixture.AdvanceTime(TimeDelta::Seconds(5));
  probes = probe_controller->Process(fixture.CurrentTime());
  ASSERT_EQ(probes.size(), 1u);
  EXPECT_EQ(probes[0].target_data_rate, kMaxBitrate);

  NetworkStateEstimate state_estimate;
  state_estimate.link_capacity_upper = DataRate::BitsPerSec(8000);
  probe_controller->SetNetworkStateEstimate(state_estimate);
  fixture.AdvanceTime(TimeDelta::Seconds(5));
  probes = probe_controller->Process(fixture.CurrentTime());
  ASSERT_EQ(probes.size(), 1u);
  EXPECT_EQ(probes[0].target_data_rate, state_estimate.link_capacity_upper);
}

TEST(ProbeControllerTest, CanSetLongerProbeDurationAfterNetworkStateEstimate) {
  ProbeControllerFixture fixture(
      "WebRTC-Bwe-ProbingConfiguration/"
      "network_state_interval:5s,network_state_probe_duration:100ms/");
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());

  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  probes = probe_controller->SetEstimatedBitrate(
      DataRate::KilobitsPerSec(5), BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  ASSERT_FALSE(probes.empty());
  EXPECT_LT(probes[0].target_duration, TimeDelta::Millis(100));

  NetworkStateEstimate state_estimate;
  state_estimate.link_capacity_upper = DataRate::KilobitsPerSec(6);
  probe_controller->SetNetworkStateEstimate(state_estimate);
  fixture.AdvanceTime(TimeDelta::Seconds(5));
  probes = probe_controller->Process(fixture.CurrentTime());
  ASSERT_EQ(probes.size(), 1u);
  EXPECT_EQ(probes[0].target_duration, TimeDelta::Millis(100));
}

TEST(ProbeControllerTest, ProbeInAlrIfLossBasedIncreasing) {
  ProbeControllerFixture fixture;
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  probe_controller->EnablePeriodicAlrProbing(true);
  probes = probe_controller->SetEstimatedBitrate(
      kStartBitrate, BandwidthLimitedCause::kLossLimitedBweIncreasing,
      fixture.CurrentTime());

  // Wait long enough to time out exponential probing.
  fixture.AdvanceTime(kExponentialProbingTimeout);
  probes = probe_controller->Process(fixture.CurrentTime());
  ASSERT_TRUE(probes.empty());

  // Probe when in alr.
  probe_controller->SetAlrStartTimeMs(fixture.CurrentTime().ms());
  fixture.AdvanceTime(kAlrProbeInterval + TimeDelta::Millis(1));
  probes = probe_controller->Process(fixture.CurrentTime());
  ASSERT_EQ(probes.size(), 1u);
  EXPECT_EQ(probes.at(0).target_data_rate, 1.5 * kStartBitrate);
}

TEST(ProbeControllerTest, ProbeFurtherInAlrIfLossBasedIncreasing) {
  ProbeControllerFixture fixture;
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  probe_controller->EnablePeriodicAlrProbing(true);
  probes = probe_controller->SetEstimatedBitrate(
      kStartBitrate, BandwidthLimitedCause::kLossLimitedBweIncreasing,
      fixture.CurrentTime());

  // Wait long enough to time out exponential probing.
  fixture.AdvanceTime(kExponentialProbingTimeout);
  probes = probe_controller->Process(fixture.CurrentTime());
  ASSERT_TRUE(probes.empty());

  // Probe when in alr.
  probe_controller->SetAlrStartTimeMs(fixture.CurrentTime().ms());
  fixture.AdvanceTime(kAlrProbeInterval + TimeDelta::Millis(1));
  probes = probe_controller->Process(fixture.CurrentTime());
  ASSERT_EQ(probes.size(), 1u);
  ASSERT_EQ(probes.at(0).target_data_rate, 1.5 * kStartBitrate);

  probes = probe_controller->SetEstimatedBitrate(
      1.5 * kStartBitrate, BandwidthLimitedCause::kLossLimitedBweIncreasing,
      fixture.CurrentTime());
  ASSERT_EQ(probes.size(), 1u);
  EXPECT_EQ(probes[0].target_data_rate, 1.5 * 1.5 * kStartBitrate);
}

TEST(ProbeControllerTest, NotProbeWhenInAlrIfLossBasedDecreases) {
  ProbeControllerFixture fixture;
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  probe_controller->EnablePeriodicAlrProbing(true);
  probes = probe_controller->SetEstimatedBitrate(
      kStartBitrate, BandwidthLimitedCause::kLossLimitedBwe,
      fixture.CurrentTime());

  // Wait long enough to time out exponential probing.
  fixture.AdvanceTime(kExponentialProbingTimeout);
  probes = probe_controller->Process(fixture.CurrentTime());
  ASSERT_TRUE(probes.empty());

  // Not probe in alr when loss based estimate decreases.
  probe_controller->SetAlrStartTimeMs(fixture.CurrentTime().ms());
  fixture.AdvanceTime(kAlrProbeInterval + TimeDelta::Millis(1));
  probes = probe_controller->Process(fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());
}

TEST(ProbeControllerTest, NotProbeIfLossBasedIncreasingOutsideAlr) {
  ProbeControllerFixture fixture;
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  probe_controller->EnablePeriodicAlrProbing(true);
  probes = probe_controller->SetEstimatedBitrate(
      kStartBitrate, BandwidthLimitedCause::kLossLimitedBweIncreasing,
      fixture.CurrentTime());

  // Wait long enough to time out exponential probing.
  fixture.AdvanceTime(kExponentialProbingTimeout);
  probes = probe_controller->Process(fixture.CurrentTime());
  ASSERT_TRUE(probes.empty());

  probe_controller->SetAlrStartTimeMs(absl::nullopt);
  fixture.AdvanceTime(kAlrProbeInterval + TimeDelta::Millis(1));
  probes = probe_controller->Process(fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());
}

TEST(ProbeControllerTest, ProbeFurtherWhenLossBasedIsSameAsDelayBasedEstimate) {
  ProbeControllerFixture fixture(
      "WebRTC-Bwe-ProbingConfiguration/"
      "network_state_interval:5s/");
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());

  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  ASSERT_FALSE(probes.empty());

  // Need to wait at least one second before process can trigger a new probe.
  fixture.AdvanceTime(TimeDelta::Millis(1100));
  probes = probe_controller->Process(fixture.CurrentTime());
  ASSERT_TRUE(probes.empty());

  NetworkStateEstimate state_estimate;
  state_estimate.link_capacity_upper = 5 * kStartBitrate;
  probe_controller->SetNetworkStateEstimate(state_estimate);
  fixture.AdvanceTime(TimeDelta::Seconds(5));
  probes = probe_controller->Process(fixture.CurrentTime());
  ASSERT_FALSE(probes.empty());

  DataRate probe_target_rate = probes[0].target_data_rate;
  EXPECT_LT(probe_target_rate, state_estimate.link_capacity_upper);
  // Expect that more probes are sent if BWE is the same as delay based
  // estimate.
  probes = probe_controller->SetEstimatedBitrate(
      probe_target_rate, BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  ASSERT_FALSE(probes.empty());
  EXPECT_EQ(probes[0].target_data_rate, 2 * probe_target_rate);
}

TEST(ProbeControllerTest, ProbeIfEstimateLowerThanNetworkStateEstimate) {
  // Periodic probe every 1 second if estimate is lower than 50% of the
  // NetworkStateEstimate.
  ProbeControllerFixture fixture(
      "WebRTC-Bwe-ProbingConfiguration/est_lower_than_network_interval:1s,"
      "est_lower_than_network_ratio:0.5,limit_probe_"
      "target_rate_to_loss_bwe:true/");
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());

  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  probes = probe_controller->SetEstimatedBitrate(
      kStartBitrate, BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  // Need to wait at least one second before process can trigger a new probe.
  fixture.AdvanceTime(TimeDelta::Millis(1100));
  probes = probe_controller->Process(fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());

  NetworkStateEstimate state_estimate;
  state_estimate.link_capacity_upper = kStartBitrate;
  probe_controller->SetNetworkStateEstimate(state_estimate);
  probes = probe_controller->Process(fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());

  state_estimate.link_capacity_upper = kStartBitrate * 3;
  probe_controller->SetNetworkStateEstimate(state_estimate);
  probes = probe_controller->Process(fixture.CurrentTime());
  ASSERT_EQ(probes.size(), 1u);
  EXPECT_GT(probes[0].target_data_rate, kStartBitrate);

  // If network state not increased, send another probe.
  fixture.AdvanceTime(TimeDelta::Millis(1100));
  probes = probe_controller->Process(fixture.CurrentTime());
  EXPECT_FALSE(probes.empty());

  // Stop probing if estimate increase. We might probe further here though.
  probes = probe_controller->SetEstimatedBitrate(
      2 * kStartBitrate, BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  // No more periodic probes.
  fixture.AdvanceTime(TimeDelta::Millis(1100));
  probes = probe_controller->Process(fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());
}

TEST(ProbeControllerTest, DontProbeFurtherWhenLossLimited) {
  ProbeControllerFixture fixture(
      "WebRTC-Bwe-ProbingConfiguration/"
      "network_state_interval:5s/");
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());

  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  ASSERT_FALSE(probes.empty());

  // Need to wait at least one second before process can trigger a new probe.
  fixture.AdvanceTime(TimeDelta::Millis(1100));
  probes = probe_controller->Process(fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());

  NetworkStateEstimate state_estimate;
  state_estimate.link_capacity_upper = 3 * kStartBitrate;
  probe_controller->SetNetworkStateEstimate(state_estimate);
  fixture.AdvanceTime(TimeDelta::Seconds(5));
  probes = probe_controller->Process(fixture.CurrentTime());
  EXPECT_FALSE(probes.empty());
  EXPECT_LT(probes[0].target_data_rate, state_estimate.link_capacity_upper);
  // Expect that no more probes are sent immediately if BWE is loss limited.
  probes = probe_controller->SetEstimatedBitrate(
      probes[0].target_data_rate, BandwidthLimitedCause::kLossLimitedBwe,
      fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());
}

TEST(ProbeControllerTest, ProbeFurtherWhenDelayBasedLimited) {
  ProbeControllerFixture fixture(
      "WebRTC-Bwe-ProbingConfiguration/"
      "network_state_interval:5s/");
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());

  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  ASSERT_FALSE(probes.empty());

  // Need to wait at least one second before process can trigger a new probe.
  fixture.AdvanceTime(TimeDelta::Millis(1100));
  probes = probe_controller->Process(fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());

  NetworkStateEstimate state_estimate;
  state_estimate.link_capacity_upper = 3 * kStartBitrate;
  probe_controller->SetNetworkStateEstimate(state_estimate);
  fixture.AdvanceTime(TimeDelta::Seconds(5));
  probes = probe_controller->Process(fixture.CurrentTime());
  EXPECT_FALSE(probes.empty());
  EXPECT_LT(probes[0].target_data_rate, state_estimate.link_capacity_upper);
  // Since the probe was successfull, expect to continue probing.
  probes = probe_controller->SetEstimatedBitrate(
      probes[0].target_data_rate, BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  EXPECT_FALSE(probes.empty());
  EXPECT_EQ(probes[0].target_data_rate, state_estimate.link_capacity_upper);
}

TEST(ProbeControllerTest,
     ProbeFurtherIfNetworkStateEstimateIncreaseAfterProbeSent) {
  ProbeControllerFixture fixture(
      "WebRTC-Bwe-ProbingConfiguration/"
      "network_state_interval:5s/");
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  ASSERT_FALSE(probes.empty());
  NetworkStateEstimate state_estimate;
  state_estimate.link_capacity_upper = 1.2 * probes[0].target_data_rate / 2;
  probe_controller->SetNetworkStateEstimate(state_estimate);
  // No immediate further probing since probe result is low.
  probes = probe_controller->SetEstimatedBitrate(
      probes[0].target_data_rate / 2, BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  ASSERT_TRUE(probes.empty());

  fixture.AdvanceTime(TimeDelta::Seconds(5));
  probes = probe_controller->Process(fixture.CurrentTime());
  ASSERT_FALSE(probes.empty());
  EXPECT_LE(probes[0].target_data_rate, state_estimate.link_capacity_upper);
  // If the network state estimate increase above the threshold to probe
  // further, and the probe suceeed, expect a new probe.
  state_estimate.link_capacity_upper = 3 * kStartBitrate;
  probe_controller->SetNetworkStateEstimate(state_estimate);
  probes = probe_controller->SetEstimatedBitrate(
      probes[0].target_data_rate, BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  EXPECT_FALSE(probes.empty());

  // But no more probes if estimate is close to the link capacity.
  probes = probe_controller->SetEstimatedBitrate(
      state_estimate.link_capacity_upper * 0.9,
      BandwidthLimitedCause::kDelayBasedLimited, fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());
}

TEST(ProbeControllerTest, SkipAlrProbeIfEstimateLargerThanMaxProbe) {
  ProbeControllerFixture fixture(
      "WebRTC-Bwe-ProbingConfiguration/"
      "skip_if_est_larger_than_fraction_of_max:0.9/");
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  probe_controller->EnablePeriodicAlrProbing(true);
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  ASSERT_FALSE(probes.empty());

  probes = probe_controller->SetEstimatedBitrate(
      kMaxBitrate, BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());

  probe_controller->SetAlrStartTimeMs(fixture.CurrentTime().ms());
  fixture.AdvanceTime(TimeDelta::Seconds(10));
  probes = probe_controller->Process(fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());

  // But if the max rate increase, A new probe is sent.
  probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, 2 * kMaxBitrate, fixture.CurrentTime());
  EXPECT_FALSE(probes.empty());
}

TEST(ProbeControllerTest,
     SkipAlrProbeIfEstimateLargerThanFractionOfMaxAllocated) {
  ProbeControllerFixture fixture(
      "WebRTC-Bwe-ProbingConfiguration/"
      "skip_if_est_larger_than_fraction_of_max:1.0/");
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  probe_controller->EnablePeriodicAlrProbing(true);
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  ASSERT_FALSE(probes.empty());
  probes = probe_controller->SetEstimatedBitrate(
      kMaxBitrate / 2, BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());

  fixture.AdvanceTime(TimeDelta::Seconds(10));
  probe_controller->SetAlrStartTimeMs(fixture.CurrentTime().ms());
  probes = probe_controller->OnMaxTotalAllocatedBitrate(kMaxBitrate / 2,
                                                        fixture.CurrentTime());
  // No probes since total allocated is not higher than the current estimate.
  EXPECT_TRUE(probes.empty());
  fixture.AdvanceTime(TimeDelta::Seconds(2));
  probes = probe_controller->Process(fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());

  // But if the max allocated increase, A new probe is sent.
  probes = probe_controller->OnMaxTotalAllocatedBitrate(
      kMaxBitrate / 2 + DataRate::BitsPerSec(1), fixture.CurrentTime());
  EXPECT_FALSE(probes.empty());
}

TEST(ProbeControllerTest, SkipNetworkStateProbeIfEstimateLargerThanMaxProbe) {
  ProbeControllerFixture fixture(
      "WebRTC-Bwe-ProbingConfiguration/"
      "network_state_interval:2s,skip_if_est_larger_than_fraction_of_max:0.9/");
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  ASSERT_FALSE(probes.empty());

  probe_controller->SetNetworkStateEstimate(
      {.link_capacity_upper = 2 * kMaxBitrate});
  probes = probe_controller->SetEstimatedBitrate(
      kMaxBitrate, BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());

  fixture.AdvanceTime(TimeDelta::Seconds(10));
  probes = probe_controller->Process(fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());
}

TEST(ProbeControllerTest, SendsProbeIfNetworkStateEstimateLowerThanMaxProbe) {
  ProbeControllerFixture fixture(
      "WebRTC-Bwe-ProbingConfiguration/"
      "network_state_interval:2s,skip_if_est_larger_than_fraction_of_max:0.9,"
      "/");
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  ASSERT_FALSE(probes.empty());
  probe_controller->SetNetworkStateEstimate(
      {.link_capacity_upper = 2 * kMaxBitrate});
  probes = probe_controller->SetEstimatedBitrate(
      kMaxBitrate, BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());

  // Need to wait at least two seconds before process can trigger a new probe.
  fixture.AdvanceTime(TimeDelta::Millis(2100));

  probes = probe_controller->SetEstimatedBitrate(
      kStartBitrate, BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());
  probe_controller->SetNetworkStateEstimate(
      {.link_capacity_upper = 2 * kStartBitrate});
  probes = probe_controller->Process(fixture.CurrentTime());
  EXPECT_FALSE(probes.empty());
}

TEST(ProbeControllerTest,
     ProbeNotLimitedByNetworkStateEsimateIfLowerThantCurrent) {
  ProbeControllerFixture fixture(
      "WebRTC-Bwe-ProbingConfiguration/"
      "network_state_interval:5s/");
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());
  probe_controller->EnablePeriodicAlrProbing(true);
  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  probes = probe_controller->SetEstimatedBitrate(
      kStartBitrate, BandwidthLimitedCause::kDelayBasedLimited,
      fixture.CurrentTime());
  probe_controller->SetNetworkStateEstimate(
      {.link_capacity_upper = kStartBitrate});
  // Need to wait at least one second before process can trigger a new probe.
  fixture.AdvanceTime(TimeDelta::Millis(1100));
  probes = probe_controller->Process(fixture.CurrentTime());
  ASSERT_TRUE(probes.empty());

  probe_controller->SetAlrStartTimeMs(fixture.CurrentTime().ms());
  probe_controller->SetNetworkStateEstimate(
      {.link_capacity_upper = kStartBitrate / 2});
  fixture.AdvanceTime(TimeDelta::Seconds(6));
  probes = probe_controller->Process(fixture.CurrentTime());
  ASSERT_FALSE(probes.empty());
  EXPECT_EQ(probes[0].target_data_rate, kStartBitrate);
}

TEST(ProbeControllerTest, DontProbeIfDelayIncreased) {
  ProbeControllerFixture fixture;
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());

  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  ASSERT_FALSE(probes.empty());

  // Need to wait at least one second before process can trigger a new probe.
  fixture.AdvanceTime(TimeDelta::Millis(1100));
  probes = probe_controller->Process(fixture.CurrentTime());
  ASSERT_TRUE(probes.empty());

  NetworkStateEstimate state_estimate;
  state_estimate.link_capacity_upper = 3 * kStartBitrate;
  probe_controller->SetNetworkStateEstimate(state_estimate);
  probes = probe_controller->SetEstimatedBitrate(
      kStartBitrate, BandwidthLimitedCause::kDelayBasedLimitedDelayIncreased,
      fixture.CurrentTime());
  ASSERT_TRUE(probes.empty());

  fixture.AdvanceTime(TimeDelta::Seconds(5));
  probes = probe_controller->Process(fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());
}

TEST(ProbeControllerTest, DontProbeIfHighRtt) {
  ProbeControllerFixture fixture;
  std::unique_ptr<ProbeController> probe_controller =
      fixture.CreateController();
  ASSERT_THAT(
      probe_controller->OnNetworkAvailability({.network_available = true}),
      IsEmpty());

  auto probes = probe_controller->SetBitrates(
      kMinBitrate, kStartBitrate, kMaxBitrate, fixture.CurrentTime());
  ASSERT_FALSE(probes.empty());

  // Need to wait at least one second before process can trigger a new probe.
  fixture.AdvanceTime(TimeDelta::Millis(1100));
  probes = probe_controller->Process(fixture.CurrentTime());
  ASSERT_TRUE(probes.empty());

  NetworkStateEstimate state_estimate;
  state_estimate.link_capacity_upper = 3 * kStartBitrate;
  probe_controller->SetNetworkStateEstimate(state_estimate);
  probes = probe_controller->SetEstimatedBitrate(
      kStartBitrate, BandwidthLimitedCause::kRttBasedBackOffHighRtt,
      fixture.CurrentTime());
  ASSERT_TRUE(probes.empty());

  fixture.AdvanceTime(TimeDelta::Seconds(5));
  probes = probe_controller->Process(fixture.CurrentTime());
  EXPECT_TRUE(probes.empty());
}
}  // namespace test
}  // namespace webrtc
