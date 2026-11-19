/*
 *  Copyright 2022 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/task_queue_stdlib.h"

#include <memory>

#include "api/field_trials_view.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/task_queue/task_queue_test.h"
#include "test/gtest.h"

#if defined(WEBRTC_ANDROID) && !defined(WEBRTC_CHROMIUM_BUILD)
#include <utility>

#include "api/units/time_delta.h"
#include "rtc_base/event.h"
#include "rtc_base/logging.h"
#include "test/scoped_logging_config.h"
#endif

namespace webrtc {
namespace {

std::unique_ptr<TaskQueueFactory> CreateTaskQueueFactory(
    const FieldTrialsView*) {
  return CreateTaskQueueStdlibFactory();
}

INSTANTIATE_TEST_SUITE_P(TaskQueueStdlib,
                         TaskQueueTest,
                         ::testing::Values(CreateTaskQueueFactory));

#if defined(WEBRTC_ANDROID) && !defined(WEBRTC_CHROMIUM_BUILD)
class EventLogSink final : public LogSink {
 public:
  explicit EventLogSink(Event* logged) : logged_(logged) {}

 private:
  void OnLogMessage(const LogLineRef& line) override { logged_->Set(); }
  Event* const logged_;
};

TEST(TaskQueueStdlib, AvoidsSpammingLogOnInactivity) {
  Event logged;
  LoggingConfig config;
  config.set_min_severity(LS_VERBOSE);
  config.AddSink(std::make_unique<EventLogSink>(&logged));
  ScopedLoggingConfig scoped_logging(std::move(config));

  auto task_queue = CreateTaskQueueStdlibFactory()->CreateTaskQueue(
      "test", TaskQueueFactory::Priority::kNormal);
  EXPECT_FALSE(
      logged.Wait(Event::kDefaultWarnDuration + TimeDelta::Seconds(1)));
  task_queue = nullptr;
}
#endif  // defined(WEBRTC_ANDROID) && !defined(WEBRTC_CHROMIUM_BUILD)

}  // namespace
}  // namespace webrtc
