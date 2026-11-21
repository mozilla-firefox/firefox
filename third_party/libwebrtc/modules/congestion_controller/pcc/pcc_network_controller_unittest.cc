/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/congestion_controller/pcc/pcc_network_controller.h"

#include <memory>

#include "modules/congestion_controller/pcc/pcc_factory.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/scenario/scenario.h"

using ::testing::AllOf;
using ::testing::Field;
using ::testing::Ge;
using ::testing::Le;
using ::testing::Matcher;
using ::testing::Property;

namespace webrtc {
namespace test {
namespace {

const DataRate kInitialBitrate = DataRate::KilobitsPerSec(60);
const Timestamp kDefaultStartTime = Timestamp::Millis(10000000);

constexpr double kDataRateMargin = 0.20;
constexpr double kMinDataRateFactor = 1 - kDataRateMargin;
constexpr double kMaxDataRateFactor = 1 + kDataRateMargin;
inline Matcher<TargetTransferRate> TargetRateCloseTo(DataRate rate) {
  DataRate min_data_rate = rate * kMinDataRateFactor;
  DataRate max_data_rate = rate * kMaxDataRateFactor;
  return Field(&TargetTransferRate::target_rate,
               AllOf(Ge(min_data_rate), Le(max_data_rate)));
}

NetworkControllerConfig InitialConfig(
    int starting_bandwidth_kbps = kInitialBitrate.kbps(),
    int min_data_rate_kbps = 0,
    int max_data_rate_kbps = 5 * kInitialBitrate.kbps()) {
  NetworkControllerConfig config;
  config.constraints.at_time = kDefaultStartTime;
  config.constraints.min_data_rate =
      DataRate::KilobitsPerSec(min_data_rate_kbps);
  config.constraints.max_data_rate =
      DataRate::KilobitsPerSec(max_data_rate_kbps);
  config.constraints.starting_rate =
      DataRate::KilobitsPerSec(starting_bandwidth_kbps);
  return config;
}

ProcessInterval InitialProcessInterval() {
  ProcessInterval process_interval;
  process_interval.at_time = kDefaultStartTime;
  return process_interval;
}

}  // namespace

TEST(PccNetworkControllerTest, SendsConfigurationOnFirstProcess) {
  std::unique_ptr<NetworkControllerInterface> controller_;
  controller_.reset(new pcc::PccNetworkController(InitialConfig()));

  NetworkControlUpdate update =
      controller_->OnProcessInterval(InitialProcessInterval());
  EXPECT_THAT(*update.target_rate, TargetRateCloseTo(kInitialBitrate));
  EXPECT_THAT(*update.pacer_config,
              Property(&PacerConfig::data_rate, Ge(kInitialBitrate)));
}

TEST(PccNetworkControllerTest, UpdatesTargetSendRate) {
  PccNetworkControllerFactory factory;
  Scenario s("pcc_unit/updates_rate", false);
  CallClientConfig config;
  config.transport.cc_factory = &factory;
  config.transport.rates.min_rate = DataRate::KilobitsPerSec(10);
  config.transport.rates.max_rate = DataRate::KilobitsPerSec(1500);
  config.transport.rates.start_rate = DataRate::KilobitsPerSec(300);
  auto send_net = s.CreateMutableSimulationNode([](NetworkSimulationConfig* c) {
    c->bandwidth = DataRate::KilobitsPerSec(500);
    c->delay = TimeDelta::Millis(100);
  });
  auto ret_net = s.CreateMutableSimulationNode(
      [](NetworkSimulationConfig* c) { c->delay = TimeDelta::Millis(100); });

  auto* client = s.CreateClient("send", config);
  auto* route = s.CreateRoutes(client, {send_net->node()},
                               s.CreateClient("return", CallClientConfig()),
                               {ret_net->node()});
  VideoStreamConfig video;
  video.stream.use_rtx = false;
  s.CreateVideoStream(route->forward(), video);
  s.RunFor(TimeDelta::Seconds(30));
  EXPECT_NEAR(client->target_rate().kbps(), 450, 100);
  send_net->UpdateConfig([](NetworkSimulationConfig* c) {
    c->bandwidth = DataRate::KilobitsPerSec(800);
    c->delay = TimeDelta::Millis(100);
  });
  s.RunFor(TimeDelta::Seconds(40));
  EXPECT_NEAR(client->target_rate().kbps(), 750, 150);
  send_net->UpdateConfig([](NetworkSimulationConfig* c) {
    c->bandwidth = DataRate::KilobitsPerSec(200);
    c->delay = TimeDelta::Millis(200);
  });
  ret_net->UpdateConfig(
      [](NetworkSimulationConfig* c) { c->delay = TimeDelta::Millis(200); });
  s.RunFor(TimeDelta::Seconds(35));
  EXPECT_LE(client->target_rate().kbps(), 200);
  EXPECT_GT(client->target_rate().kbps(), 90);
}

}  // namespace test
}  // namespace webrtc
