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
#include <string>

#include "absl/strings/string_view.h"
#include "api/field_trials_view.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/task_queue/task_queue_test.h"
#include "api/units/time_delta.h"
#include "rtc_base/event.h"
#include "rtc_base/logging.h"
#include "rtc_base/thread.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

std::unique_ptr<TaskQueueFactory> CreateTaskQueueFactory(
    const FieldTrialsView*) {
  return CreateTaskQueueStdlibFactory();
}

INSTANTIATE_TEST_SUITE_P(TaskQueueStdlib,
                         TaskQueueTest,
                         ::testing::Values(CreateTaskQueueFactory));

class StringPtrLogSink : public LogSink {
 public:
  explicit StringPtrLogSink(std::string* log_data) : log_data_(log_data) {}

 private:
  void OnLogMessage(const LogLineRef& line) override {
    log_data_->append(line.DefaultLogLine());
  }
  std::string* const log_data_;
};

TEST(TaskQueueStdlib, AvoidsSpammingLogOnInactivity) {
  std::string log_output;
  StringPtrLogSink stream(&log_output);
  LogMessage::AddLogToStream(&stream, LS_VERBOSE);
  auto task_queue = CreateTaskQueueStdlibFactory()->CreateTaskQueue(
      "test", TaskQueueFactory::Priority::kNormal);
  auto wait_duration = Event::kDefaultWarnDuration + TimeDelta::Seconds(1);
  Thread::SleepMs(wait_duration.ms());
  EXPECT_EQ(log_output.length(), 0u);
  task_queue = nullptr;
  LogMessage::RemoveLogToStream(&stream);
}

}  // namespace
}  // namespace webrtc
