/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "test/run_loop.h"

#include "api/environment/environment.h"
#include "api/task_queue/task_queue_base.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "test/create_test_environment.h"
#include "test/gtest.h"

namespace webrtc {

TEST(RunLoopTest, TaskQueueOnThread) {
  test::RunLoop loop;
  EXPECT_EQ(TaskQueueBase::Current(), loop.task_queue());
  EXPECT_TRUE(loop.task_queue()->IsCurrent());
}

TEST(RunLoopTest, Flush) {
  test::RunLoop loop;
  int counter = 0;
  loop.PostTask([&counter]() { ++counter; });
  EXPECT_EQ(counter, 0);
  loop.Flush();
  EXPECT_EQ(counter, 1);
}

TEST(RunLoopTest, Delayed) {
  test::RunLoop loop;
  bool ran = false;
  loop.task_queue()->PostDelayedTask(
      [&ran, &loop]() {
        ran = true;
        loop.Quit();
      },
      TimeDelta::Millis(100));
  loop.Flush();
  EXPECT_FALSE(ran);
  loop.Run();
  EXPECT_TRUE(ran);
}

TEST(RunLoopTest, PostAndQuit) {
  test::RunLoop loop;
  bool ran = false;
  loop.PostTask([&ran, &loop]() {
    ran = true;
    loop.Quit();
  });
  loop.Run();
  EXPECT_TRUE(ran);
}

TEST(RunLoopTest, RunForWaitsForMaxWaitDurationIfNoQuit) {
  Environment env = CreateTestEnvironment();
  test::RunLoop loop;
  Timestamp start = env.clock().CurrentTime();
  loop.RunFor(TimeDelta::Millis(20));
  EXPECT_GE(env.clock().CurrentTime() - start, TimeDelta::Millis(19));
}

TEST(RunLoopTest, RunForReturnsBeforeMaxWaitDurationIfQuitCalled) {
  // Much longer than the delay of the quit task, to leave a large margin for
  // scheduling delays on loaded test machines.
  constexpr TimeDelta kMaxWaitDuration = TimeDelta::Seconds(10);
  Environment env = CreateTestEnvironment();
  test::RunLoop loop;
  Timestamp start = env.clock().CurrentTime();
  loop.task_queue()->PostDelayedHighPrecisionTask(loop.QuitClosure(),
                                                  TimeDelta::Millis(10));
  loop.RunFor(kMaxWaitDuration);
  EXPECT_LT(env.clock().CurrentTime() - start, kMaxWaitDuration / 2);
}

TEST(RunLoopTest, RunForCancelsTimeoutIfQuitCalled) {
  test::RunLoop loop;
  loop.task_queue()->PostDelayedHighPrecisionTask(loop.QuitClosure(),
                                                  TimeDelta::Millis(10));
  loop.RunFor(TimeDelta::Millis(20));

  bool ran = false;
  loop.task_queue()->PostDelayedHighPrecisionTask(
      [&ran, &loop]() {
        ran = true;
        loop.Quit();
      },
      TimeDelta::Millis(100));
  loop.Run();
  // If the old `RunFor` causes the loop to quit then `Run` returns before the
  // task above has run. Delayed tasks run in the order of their scheduled
  // time, so this check doesn't depend on the load of the machine.
  EXPECT_TRUE(ran);
}

}  // namespace webrtc
