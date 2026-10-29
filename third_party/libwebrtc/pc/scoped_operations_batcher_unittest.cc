/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/scoped_operations_batcher.h"

#include <memory>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "api/function_view.h"
#include "api/location.h"
#include "api/rtc_error.h"
#include "rtc_base/null_socket_server.h"
#include "rtc_base/thread.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

class FakeQuittingThread : public Thread {
 public:
  FakeQuittingThread() : Thread(std::make_unique<NullSocketServer>()) {}

  void BlockingCallImpl(FunctionView<void()> functor,
                        const Location& location) override {
    // Do nothing to simulate dropping the task when quitting.
  }
};

TEST(ScopedOperationsBatcherTest, ExecutesTasksOnTargetThread) {
  auto target_thread = Thread::Create();
  target_thread->Start();

  bool task_executed = false;
  bool target_checked = false;

  {
    ScopedOperationsBatcher batcher(target_thread.get());
    batcher.Add([&] {
      task_executed = true;
      target_checked = target_thread->IsCurrent();
    });
  }

  EXPECT_TRUE(task_executed);
  EXPECT_TRUE(target_checked);
}

TEST(ScopedOperationsBatcherTest, ExecutesReturnedTasksOnCallingThread) {
  // Use current thread as the calling (signaling) thread. This test runs
  // without an explicit RunLoop, because the returned tasks are executed
  // synchronously within the caller's context of `Run()` or
  // `~ScopedOperationsBatcher()`.
  auto signaling_thread = Thread::Current();

  auto target_thread = Thread::Create();
  target_thread->Start();

  bool return_task_executed = false;
  Thread* return_task_thread = nullptr;
  bool task_executed = false;
  Thread* task_thread = nullptr;

  {
    ScopedOperationsBatcher batcher(target_thread.get());
    ScopedOperationsBatcher::BatchTaskWithFinalizer task =
        [&]() -> ScopedOperationsBatcher::BatchTaskResult {
      task_executed = true;
      task_thread = Thread::Current();
      return ScopedOperationsBatcher::FinalizerTask([&]() {
        return_task_executed = true;
        return_task_thread = Thread::Current();
      });
    };
    batcher.AddWithFinalizer(std::move(task));
  }

  EXPECT_TRUE(task_executed);
  EXPECT_EQ(task_thread, target_thread.get());
  EXPECT_TRUE(return_task_executed);
  EXPECT_EQ(return_task_thread, signaling_thread);
}

TEST(ScopedOperationsBatcherTest, YieldsToOtherTasks) {
  auto target_thread = Thread::Create();
  target_thread->Start();

  std::vector<int> execution_order;

  {
    ScopedOperationsBatcher batcher(target_thread.get());
    batcher.Add([&] { execution_order.push_back(1); });
    batcher.Add([&] {
      execution_order.push_back(2);
      // Post a task that should interrupt the batch since we now yield to any
      // pending task.
      target_thread->PostTask([&] { execution_order.push_back(3); });
    });
    batcher.Add([&] { execution_order.push_back(4); });
    batcher.Add([&] { execution_order.push_back(5); });
  }

  // Expect the task (3) to execute immediately after the task
  // that posted it (2). The operations remaining in the batch (4, 5)
  // should resume after the thread has processed the new task.
  EXPECT_EQ(execution_order, std::vector<int>({1, 2, 3, 4, 5}));
}

TEST(ScopedOperationsBatcherTest, StopsExecutionOnError) {
  auto target_thread = Thread::Create();
  target_thread->Start();

  bool task1_executed = false;
  bool task2_executed = false;
  bool task3_executed = false;
  // There's no finalizer for task2.
  bool finalizer1_executed = false;
  bool finalizer3_executed = false;

  RTCError error = RTCError::OK();
  {
    ScopedOperationsBatcher batcher(target_thread.get());
    // Task 1.
    batcher.AddWithFinalizer([&]() -> ScopedOperationsBatcher::BatchTaskResult {
      task1_executed = true;
      return ScopedOperationsBatcher::FinalizerTask(
          [&] { finalizer1_executed = true; });
    });
    // Task 2.
    batcher.AddWithFinalizer([&]() -> ScopedOperationsBatcher::BatchTaskResult {
      task2_executed = true;
      return RTCError(RTCErrorType::INVALID_STATE, "Failed");
    });
    // Task 3.
    batcher.AddWithFinalizer([&]() -> ScopedOperationsBatcher::BatchTaskResult {
      task3_executed = true;
      return ScopedOperationsBatcher::FinalizerTask(
          [&] { finalizer3_executed = true; });
    });

    error = batcher.Run();
  }

  EXPECT_FALSE(error.ok());
  EXPECT_EQ(error.type(), RTCErrorType::INVALID_STATE);
  EXPECT_STREQ(error.message(), "Failed");

  EXPECT_TRUE(task1_executed);
  EXPECT_TRUE(finalizer1_executed);
  EXPECT_TRUE(task2_executed);
  EXPECT_FALSE(task3_executed);
  EXPECT_FALSE(finalizer3_executed);
}

TEST(ScopedOperationsBatcherTest, RunsFinalizerReturnedWithError) {
  auto target_thread = Thread::Create();
  target_thread->Start();

  bool finalizer_executed = false;
  bool task2_executed = false;

  RTCError error = RTCError::OK();
  {
    ScopedOperationsBatcher batcher(target_thread.get());
    batcher.AddWithFinalizer([&]() -> ScopedOperationsBatcher::BatchTaskResult {
      return {RTCError(RTCErrorType::INVALID_STATE, "Failed"),
              [&] { finalizer_executed = true; }};
    });
    batcher.Add([&] { task2_executed = true; });

    error = batcher.Run();
  }

  EXPECT_FALSE(error.ok());
  EXPECT_EQ(error.type(), RTCErrorType::INVALID_STATE);
  EXPECT_STREQ(error.message(), "Failed");
  EXPECT_TRUE(finalizer_executed);
  EXPECT_FALSE(task2_executed);
}

TEST(ScopedOperationsBatcherTest, IsEmptyReflectsTaskCount) {
  auto target_thread = Thread::Create();
  target_thread->Start();

  ScopedOperationsBatcher batcher(target_thread.get());
  EXPECT_TRUE(batcher.IsEmpty());

  batcher.Add([] {});
  EXPECT_FALSE(batcher.IsEmpty());

  EXPECT_TRUE(batcher.Run().ok());
  EXPECT_TRUE(batcher.IsEmpty());
}

TEST(ScopedOperationsBatcherTest, AbortsGracefullyWhenTargetThreadQuits) {
  FakeQuittingThread target_thread;
  target_thread.Start();

  ScopedOperationsBatcher batcher(&target_thread);
  batcher.Add([] {});

  // This should return an error and not hang or crash.
  RTCError error = batcher.Run();
  EXPECT_FALSE(error.ok());
  EXPECT_EQ(error.type(), RTCErrorType::INTERNAL_ERROR);

  // Explicitly stop `target_thread` before it is destroyed to avoid a conflict
  // between the FakeQuittingThread destructor and ~Thread().
  // Otherwise, the background thread continues running `ProcessMessages()` and
  // making virtual calls (like `IsQuitting()`) while the
  // `~FakeQuittingThread()` destructor is run and resets the vptr (which
  // triggers a race with TSAN).
  target_thread.Stop();
}

}  // namespace
}  // namespace webrtc
