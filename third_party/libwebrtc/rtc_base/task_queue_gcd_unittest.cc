/*
 *  Copyright 2026 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/task_queue_gcd.h"

#include <memory>

#include "api/task_queue/task_queue_base.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/units/time_delta.h"
#include "rtc_base/event.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

TEST(TaskQueueGcdTest, DeleteFromTask) {
  std::unique_ptr<TaskQueueFactory> factory = CreateTaskQueueGcdFactory();
  auto queue = factory->CreateTaskQueue("DeleteFromTask",
                                        TaskQueueFactory::Priority::kNormal);
  Event done;
  queue->PostTask([&queue, &done] {
    // This will call TaskQueueGcd::Delete() from the current queue.
    queue.reset();
    done.Set();
  });
  EXPECT_TRUE(done.Wait(TimeDelta::Seconds(1)));
}

TEST(TaskQueueGcdTest, DeleteFromHighPrecisionTask) {
  std::unique_ptr<TaskQueueFactory> factory = CreateTaskQueueGcdFactory();
  auto queue = factory->CreateTaskQueue("DeleteFromHighPrecisionTask",
                                        TaskQueueFactory::Priority::kNormal);
  Event done;
  queue->PostDelayedHighPrecisionTask(
      [&queue, &done] {
        // This will call TaskQueueGcd::Delete() from the current queue.
        queue.reset();
        done.Set();
      },
      TimeDelta::Millis(1));
  EXPECT_TRUE(done.Wait(TimeDelta::Seconds(1)));
}

}  // namespace
}  // namespace webrtc
