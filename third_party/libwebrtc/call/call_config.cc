/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "call/call_config.h"

#include "absl/base/nullability.h"
#include "api/environment/environment.h"
#include "api/task_queue/task_queue_base.h"
#include "api/transport/network_types.h"
#include "call/rtp_transport_config.h"
#include "rtc_base/checks.h"

namespace webrtc {

CallConfig::CallConfig(const Environment& env,
                       TaskQueueBase* network_task_queue)
    : env(env),
      network_task_queue_(network_task_queue ? network_task_queue
                                             : TaskQueueBase::Current()),
      worker_task_queue(network_task_queue_) {
  RTC_DCHECK(network_task_queue_ != nullptr);
}

CallConfig::CallConfig(const Environment& env,
                       TaskQueueBase* absl_nonnull worker_task_queue,
                       TaskQueueBase* absl_nonnull network_task_queue)
    : env(env),
      network_task_queue_(network_task_queue),
      worker_task_queue(worker_task_queue) {
  RTC_DCHECK(network_task_queue != nullptr);
  // Call runs only on the network queue.
  RTC_CHECK_EQ(worker_task_queue, network_task_queue);
}

CallConfig CallConfig::CreateWithJoinedWorkerAndNetworkQueue(
    const Environment& env,
    TaskQueueBase* absl_nonnull worker_and_network_queue) {
  return CallConfig(env, worker_and_network_queue);
}

CallConfig CallConfig::CreateSingleThreaded(const Environment& env) {
  return CallConfig(env, TaskQueueBase::Current());
}

RtpTransportConfig CallConfig::ExtractTransportConfig() const {
  return RtpTransportConfig{
      .env = env,
      .bitrate_config = bitrate_config,
      .network_state_predictor_factory = network_state_predictor_factory,
      .network_controller_factory =
          per_call_network_controller_factory
              ? per_call_network_controller_factory.get()
              : network_controller_factory,
      .default_pacing_time_window =
          pacer_burst_interval.value_or(PacerConfig::kDefaultTimeInterval),
      .worker_thread = network_task_queue_,
  };
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
CallConfig::CallConfig(CallConfig&&) = default;
CallConfig& CallConfig::operator=(CallConfig&& other) = default;
#pragma clang diagnostic pop

CallConfig::~CallConfig() = default;

}  // namespace webrtc
