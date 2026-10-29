/*
 *  Copyright 2019 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "api/task_queue/task_queue_test.h"

#ifdef BUILD_EXPERIMENTAL_TASK_QUEUE_COROUTINE_TESTS
#include <coroutine>
#endif

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#if defined(WEBRTC_WIN)
#include <windows.h>
#endif

#include "absl/cleanup/cleanup.h"
#ifdef BUILD_EXPERIMENTAL_TASK_QUEUE_COROUTINE_TESTS
#include "absl/functional/any_invocable.h"
#endif
#include "absl/strings/string_view.h"
#include "api/make_ref_counted.h"
#include "api/ref_count.h"
#if !defined(WEBRTC_CHROMIUM_BUILD) && \
    (defined(WEBRTC_WIN) || defined(WEBRTC_MAC) || defined(WEBRTC_IOS))
#include "api/task_queue/default_task_queue_factory.h"
#endif
#include "api/task_queue/task_queue_base.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/units/time_delta.h"
#include "rtc_base/event.h"
#include "rtc_base/ref_counter.h"
#include "rtc_base/time_utils.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

using ::testing::ElementsAre;

// Avoids a dependency to system_wrappers.
void SleepFor(TimeDelta duration) {
  ScopedAllowBaseSyncPrimitivesForTesting allow;
  Event event;
  event.Wait(duration);
}

std::unique_ptr<TaskQueueBase, TaskQueueDeleter> CreateTaskQueue(
    const std::unique_ptr<TaskQueueFactory>& factory,
    absl::string_view task_queue_name,
    TaskQueueFactory::Priority priority = TaskQueueFactory::Priority::kNormal) {
  return factory->CreateTaskQueue(task_queue_name, priority);
}

TEST_P(TaskQueueTest, Construct) {
  std::unique_ptr<TaskQueueFactory> factory = GetParam()(nullptr);
  auto queue = CreateTaskQueue(factory, "Construct");
  EXPECT_FALSE(queue->IsCurrent());
}

TEST_P(TaskQueueTest, PostAndCheckCurrent) {
  std::unique_ptr<TaskQueueFactory> factory = GetParam()(nullptr);
  Event event;
  auto queue = CreateTaskQueue(factory, "PostAndCheckCurrent");

  // We're not running a task, so `queue` shouldn't be current.
  // Note that because Thread also supports the TQ interface and
  // TestMainImpl::Init wraps the main test thread (bugs.webrtc.org/9714), that
  // means that TaskQueueBase::Current() will still return a valid value.
  EXPECT_FALSE(queue->IsCurrent());

  queue->PostTask([&event, &queue] {
    EXPECT_TRUE(queue->IsCurrent());
    event.Set();
  });
  EXPECT_TRUE(event.Wait(TimeDelta::Seconds(1)));
}

// Verifies that a task can post a new task from within the task
// and that the new one eventually runs.
TEST_P(TaskQueueTest, TaskCanPostContinuationTask) {
  std::unique_ptr<TaskQueueFactory> factory = GetParam()(nullptr);
  auto queue = CreateTaskQueue(factory, "TaskCanPostContinuationTask");

  std::vector<std::string> events;
  Event done;

  queue->PostTask([&events, &done, queue = queue.get()] {
    events.push_back("Start");
    queue->PostTask([&events, &done] {
      events.push_back("Continue");
      done.Set();
    });
    events.push_back("FirstDone");
  });

  EXPECT_TRUE(done.Wait(TimeDelta::Seconds(1)));

  EXPECT_THAT(events, ElementsAre("Start", "FirstDone", "Continue"));
}

TEST_P(TaskQueueTest, PostCustomTask) {
  std::unique_ptr<TaskQueueFactory> factory = GetParam()(nullptr);
  Event ran;
  auto queue = CreateTaskQueue(factory, "PostCustomImplementation");

  class CustomTask {
   public:
    explicit CustomTask(Event* ran) : ran_(ran) {}

    void operator()() { ran_->Set(); }

   private:
    Event* const ran_;
  } my_task(&ran);

  queue->PostTask(my_task);
  EXPECT_TRUE(ran.Wait(TimeDelta::Seconds(1)));
}

TEST_P(TaskQueueTest, PostDelayedZero) {
  std::unique_ptr<TaskQueueFactory> factory = GetParam()(nullptr);
  Event event;
  auto queue = CreateTaskQueue(factory, "PostDelayedZero");

  queue->PostDelayedTask([&event] { event.Set(); }, TimeDelta::Zero());
  EXPECT_TRUE(event.Wait(TimeDelta::Seconds(1)));
}

TEST_P(TaskQueueTest, PostFromQueue) {
  std::unique_ptr<TaskQueueFactory> factory = GetParam()(nullptr);
  Event event;
  auto queue = CreateTaskQueue(factory, "PostFromQueue");

  queue->PostTask(
      [&event, &queue] { queue->PostTask([&event] { event.Set(); }); });
  EXPECT_TRUE(event.Wait(TimeDelta::Seconds(1)));
}

TEST_P(TaskQueueTest, PostDelayed) {
  std::unique_ptr<TaskQueueFactory> factory = GetParam()(nullptr);
  Event event;
  auto queue = CreateTaskQueue(factory, "PostDelayed",
                               TaskQueueFactory::Priority::kHigh);

  int64_t start = TimeMillis();
  queue->PostDelayedTask(
      [&event, &queue] {
        EXPECT_TRUE(queue->IsCurrent());
        event.Set();
      },
      TimeDelta::Millis(100));
  EXPECT_TRUE(event.Wait(TimeDelta::Seconds(1)));
  int64_t end = TimeMillis();
  // These tests are a little relaxed due to how "powerful" our test bots can
  // be.  Most recently we've seen windows bots fire the callback after 94-99ms,
  // which is why we have a little bit of leeway backwards as well.
  EXPECT_GE(end - start, 90u);
  EXPECT_NEAR(end - start, 190u, 100u);  // Accept 90-290.
}

TEST_P(TaskQueueTest, PostMultipleDelayed) {
  std::unique_ptr<TaskQueueFactory> factory = GetParam()(nullptr);
  auto queue = CreateTaskQueue(factory, "PostMultipleDelayed");

  std::vector<Event> events(100);
  for (int i = 0; i < 100; ++i) {
    Event* event = &events[i];
    queue->PostDelayedTask(
        [event, &queue] {
          EXPECT_TRUE(queue->IsCurrent());
          event->Set();
        },
        TimeDelta::Millis(i));
  }

  for (Event& e : events)
    EXPECT_TRUE(e.Wait(TimeDelta::Seconds(1)));
}

TEST_P(TaskQueueTest, PostDelayedAfterDestruct) {
  std::unique_ptr<TaskQueueFactory> factory = GetParam()(nullptr);
  Event run;
  Event deleted;
  auto queue = CreateTaskQueue(factory, "PostDelayedAfterDestruct");
  absl::Cleanup cleanup = [&deleted] { deleted.Set(); };
  queue->PostDelayedTask([&run, cleanup = std::move(cleanup)] { run.Set(); },
                         TimeDelta::Millis(100));
  // Destroy the queue.
  queue = nullptr;
  // Task might outlive the TaskQueue, but still should be deleted.
  EXPECT_TRUE(deleted.Wait(TimeDelta::Seconds(1)));
  EXPECT_FALSE(run.Wait(TimeDelta::Zero()));  // and should not run.
}

TEST_P(TaskQueueTest, PostDelayedHighPrecisionAfterDestruct) {
  std::unique_ptr<TaskQueueFactory> factory = GetParam()(nullptr);
  Event run;
  Event deleted;
  auto queue =
      CreateTaskQueue(factory, "PostDelayedHighPrecisionAfterDestruct");
  absl::Cleanup cleanup = [&deleted] { deleted.Set(); };
  queue->PostDelayedHighPrecisionTask(
      [&run, cleanup = std::move(cleanup)] { run.Set(); },
      TimeDelta::Millis(100));
  // Destroy the queue.
  queue = nullptr;
  // Task might outlive the TaskQueue, but still should be deleted.
  EXPECT_TRUE(deleted.Wait(TimeDelta::Seconds(1)));
  EXPECT_FALSE(run.Wait(TimeDelta::Zero()));  // and should not run.
}

TEST_P(TaskQueueTest, PostedUnexecutedClosureDestroyedOnTaskQueue) {
  std::unique_ptr<TaskQueueFactory> factory = GetParam()(nullptr);
  auto queue =
      CreateTaskQueue(factory, "PostedUnexecutedClosureDestroyedOnTaskQueue");
  TaskQueueBase* queue_ptr = queue.get();
  queue->PostTask([] { SleepFor(TimeDelta::Millis(100)); });
  //  Give the task queue a chance to start executing the first lambda.
  SleepFor(TimeDelta::Millis(10));
  Event finished;
  //  Then ensure the next lambda (which is likely not executing yet) is
  //  destroyed in the task queue context when the queue is deleted.
  auto cleanup = absl::Cleanup([queue_ptr, &finished] {
    EXPECT_EQ(queue_ptr, TaskQueueBase::Current());
    finished.Set();
  });
  queue->PostTask([cleanup = std::move(cleanup)] {});
  queue = nullptr;
  finished.Wait(TimeDelta::Seconds(1));
}

TEST_P(TaskQueueTest, PostedClosureDestroyedOnTaskQueue) {
  std::unique_ptr<TaskQueueFactory> factory = GetParam()(nullptr);
  auto queue = CreateTaskQueue(factory, "PostedClosureDestroyedOnTaskQueue");
  TaskQueueBase* queue_ptr = queue.get();
  Event finished;
  auto cleanup = absl::Cleanup([queue_ptr, &finished] {
    EXPECT_EQ(queue_ptr, TaskQueueBase::Current());
    finished.Set();
  });
  // The cleanup task may or may not have had time to execute when the task
  // queue is destroyed. Regardless, the task should be destroyed on the
  // queue.
  queue->PostTask([cleanup = std::move(cleanup)] {});
  queue = nullptr;
  finished.Wait(TimeDelta::Seconds(1));
}

TEST_P(TaskQueueTest, PostedExecutedClosureDestroyedOnTaskQueue) {
  std::unique_ptr<TaskQueueFactory> factory = GetParam()(nullptr);
  auto queue =
      CreateTaskQueue(factory, "PostedExecutedClosureDestroyedOnTaskQueue");
  TaskQueueBase* queue_ptr = queue.get();
  // Ensure an executed lambda is destroyed on the task queue.
  Event finished;
  queue->PostTask([cleanup = absl::Cleanup([queue_ptr, &finished] {
                     EXPECT_EQ(queue_ptr, TaskQueueBase::Current());
                     finished.Set();
                   })] {});
  finished.Wait(TimeDelta::Seconds(1));
}

TEST_P(TaskQueueTest, PostAndReuse) {
  std::unique_ptr<TaskQueueFactory> factory = GetParam()(nullptr);
  Event event;
  auto post_queue = CreateTaskQueue(factory, "PostQueue");
  auto reply_queue = CreateTaskQueue(factory, "ReplyQueue");

  int call_count = 0;

  class ReusedTask {
   public:
    ReusedTask(int* counter, TaskQueueBase* reply_queue, Event* event)
        : counter_(*counter), reply_queue_(reply_queue), event_(*event) {
      EXPECT_EQ(counter_, 0);
    }
    ReusedTask(ReusedTask&&) = default;
    ReusedTask& operator=(ReusedTask&&) = delete;

    void operator()() && {
      if (++counter_ == 1) {
        reply_queue_->PostTask(std::move(*this));
        // At this point, the object is in the moved-from state.
      } else {
        EXPECT_EQ(counter_, 2);
        EXPECT_TRUE(reply_queue_->IsCurrent());
        event_.Set();
      }
    }

   private:
    int& counter_;
    TaskQueueBase* const reply_queue_;
    Event& event_;
  };

  ReusedTask task(&call_count, reply_queue.get(), &event);
  post_queue->PostTask(std::move(task));
  EXPECT_TRUE(event.Wait(TimeDelta::Seconds(1)));
}

TEST_P(TaskQueueTest, PostALot) {
  // Waits until DecrementCount called `count` times. Thread safe.
  class BlockingCounter {
   public:
    explicit BlockingCounter(int initial_count) : count_(initial_count) {}

    void DecrementCount() {
      if (count_.DecRef() == RefCountReleaseStatus::kDroppedLastRef) {
        event_.Set();
      }
    }
    bool Wait(TimeDelta give_up_after) { return event_.Wait(give_up_after); }

   private:
    webrtc_impl::RefCounter count_;
    Event event_;
  };

  std::unique_ptr<TaskQueueFactory> factory = GetParam()(nullptr);
  static constexpr int kTaskCount = 0xffff;
  Event posting_done;
  BlockingCounter all_destroyed(kTaskCount);

  int tasks_executed = 0;
  auto task_queue = CreateTaskQueue(factory, "PostALot");

  task_queue->PostTask([&] {
    // Post tasks from the queue to guarantee that the 1st task won't be
    // executed before the last one is posted.
    for (int i = 0; i < kTaskCount; ++i) {
      absl::Cleanup cleanup = [&] { all_destroyed.DecrementCount(); };
      task_queue->PostTask([&tasks_executed, cleanup = std::move(cleanup)] {
        ++tasks_executed;
      });
    }

    posting_done.Set();
  });

  // Before destroying the task queue wait until all child tasks are posted.
  posting_done.Wait(Event::kForever);
  // Destroy the task queue.
  task_queue = nullptr;

  // Expect all tasks are destroyed eventually. In some task queue
  // implementations that might happen on a different thread after task queue is
  // destroyed.
  EXPECT_TRUE(all_destroyed.Wait(TimeDelta::Minutes(1)));
  EXPECT_LE(tasks_executed, kTaskCount);
}

// Test posting two tasks that have shared state not protected by a
// lock. The TaskQueue should guarantee memory read-write order and
// FIFO task execution order, so the second task should always see the
// changes that were made by the first task.
//
// If the TaskQueue doesn't properly synchronize the execution of
// tasks, there will be a data race, which is undefined behavior. The
// EXPECT calls may randomly catch this, but to make the most of this
// unit test, run it under TSan or some other tool that is able to
// directly detect data races.
TEST_P(TaskQueueTest, PostTwoWithSharedUnprotectedState) {
  std::unique_ptr<TaskQueueFactory> factory = GetParam()(nullptr);
  struct SharedState {
    // First task will set this value to 1 and second will assert it.
    int state = 0;
  } state;

  auto queue = CreateTaskQueue(factory, "PostTwoWithSharedUnprotectedState");
  Event done;
  queue->PostTask([&state, &queue, &done] {
    // Post tasks from queue to guarantee, that 1st task won't be
    // executed before the second one will be posted.
    queue->PostTask([&state] { state.state = 1; });
    queue->PostTask([&state, &done] {
      EXPECT_EQ(state.state, 1);
      done.Set();
    });
    // Check, that state changing tasks didn't start yet.
    EXPECT_EQ(state.state, 0);
  });
  EXPECT_TRUE(done.Wait(TimeDelta::Seconds(1)));
}

#if !defined(WEBRTC_CHROMIUM_BUILD) && defined(WEBRTC_WIN)
void CALLBACK ApcProc(ULONG_PTR data) {
  reinterpret_cast<Event*>(data)->Set();
}

// This works for TaskQueueWin, but not for the Thread backed
// implementation or TaskQueueStdlib.
// Change this to `TEST_P(TaskQueueTest, QueueUserAPC)` when all
// implementations support this and use GetParam() instead of
// CreateDefaultTaskQueueFactory().
TEST(TaskQueueTest, QueueUserAPC) {
  std::unique_ptr<TaskQueueFactory> factory =
      CreateDefaultTaskQueueFactory(nullptr);

  auto queue = CreateTaskQueue(factory, "ApcCompat");
  Event done;
  queue->PostTask([&done] {
    QueueUserAPC(&ApcProc, GetCurrentThread(),
                 reinterpret_cast<ULONG_PTR>(&done));
  });
  EXPECT_TRUE(done.Wait(TimeDelta::Seconds(1)));
}
#endif  // !defined(WEBRTC_CHROMIUM_BUILD) && defined(WEBRTC_WIN)

#if !defined(WEBRTC_CHROMIUM_BUILD) && \
    (defined(WEBRTC_MAC) || defined(WEBRTC_IOS))
// Only TaskQueueGcd (the default factory on macOS and iOS) distinguishes
// PostDelayedHighPrecisionTask() from PostDelayedTask() in standalone WebRTC.
// Change these to `TEST_P(TaskQueueTest, ...)` using GetParam() when all
// implementations support high-precision delayed tasks and Priority::kHigh.
TEST(TaskQueueTest, PostDelayedHighPrecisionZero) {
  std::unique_ptr<TaskQueueFactory> factory =
      CreateDefaultTaskQueueFactory(nullptr);
  Event event;
  auto queue = CreateTaskQueue(factory, "PostDelayedHighPrecisionZero",
                               TaskQueueFactory::Priority::kHigh);

  queue->PostDelayedHighPrecisionTask([&event] { event.Set(); },
                                      TimeDelta::Zero());
  EXPECT_TRUE(event.Wait(TimeDelta::Seconds(1)));
}

TEST(TaskQueueTest, PostDelayedHighPrecision) {
  std::unique_ptr<TaskQueueFactory> factory =
      CreateDefaultTaskQueueFactory(nullptr);
  Event event;
  auto queue = CreateTaskQueue(factory, "PostDelayedHighPrecision",
                               TaskQueueFactory::Priority::kHigh);

  int64_t start = TimeMillis();
  int64_t end = 0;
  queue->PostDelayedHighPrecisionTask(
      [&event, &queue, &end] {
        EXPECT_TRUE(queue->IsCurrent());
        end = TimeMillis();
        event.Set();
      },
      TimeDelta::Millis(100));
  EXPECT_TRUE(event.Wait(TimeDelta::Seconds(1)));
  EXPECT_GE(end - start, 90u);
  EXPECT_NEAR(end - start, 190u, 100u);  // Accept 90-290.
}

TEST(TaskQueueTest, PostMultipleDelayedHighPrecision) {
  std::unique_ptr<TaskQueueFactory> factory =
      CreateDefaultTaskQueueFactory(nullptr);
  auto queue = CreateTaskQueue(factory, "PostMultipleDelayedHighPrecision",
                               TaskQueueFactory::Priority::kHigh);

  std::vector<Event> events(100);
  for (int i = 0; i < 100; ++i) {
    Event* event = &events[i];
    queue->PostDelayedHighPrecisionTask(
        [event, &queue] {
          EXPECT_TRUE(queue->IsCurrent());
          event->Set();
        },
        TimeDelta::Millis(i));
  }

  for (Event& e : events)
    EXPECT_TRUE(e.Wait(TimeDelta::Seconds(1)));
}
#endif  // !defined(WEBRTC_CHROMIUM_BUILD) && (defined(WEBRTC_MAC) ||
        // defined(WEBRTC_IOS))

#ifdef BUILD_EXPERIMENTAL_TASK_QUEUE_COROUTINE_TESTS

struct TaskQueueCoroutine {
  TaskQueueBase* const tq;

  bool await_ready() const noexcept { return tq->IsCurrent(); }
  void await_suspend(std::coroutine_handle<> h) const {
    tq->PostTask([h = std::move(h)]() { h.resume(); });
  }
  void await_resume() const noexcept {}
};

struct TaskQueueCoTask {
  struct promise_type {
    TaskQueueCoTask get_return_object() { return {}; }
    std::suspend_never initial_suspend() { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() { FAIL() << "exceptional!"; }
  };
};

// A test implementation of a coroutine on top of task queues.
// This method is called on one TQ, original, switches over to the
// `hop_to` task queue to do some work and then switches back to
// the original task queue again and invoke the `done_cb` callback.
TaskQueueCoTask HoppingCoroutine(absl::AnyInvocable<void() &&> done_cb,
                                 TaskQueueBase* hop_to) {
  TaskQueueBase* original = TaskQueueBase::Current();

  // Switch to the `hop_to` task queue.
  co_await TaskQueueCoroutine{hop_to};
  EXPECT_FALSE(original->IsCurrent());
  EXPECT_TRUE(hop_to->IsCurrent());

  // Switch back to the original task queue.
  co_await TaskQueueCoroutine{original};
  EXPECT_TRUE(original->IsCurrent());
  EXPECT_FALSE(hop_to->IsCurrent());

  // Try switching again to the same, original, task queue. This tests not
  // continuing on without any switching.
  co_await TaskQueueCoroutine{original};
  EXPECT_TRUE(original->IsCurrent());
  EXPECT_FALSE(hop_to->IsCurrent());

  std::move(done_cb)();
}

// This coroutine pretends to be asynchronous, but is actually synchronous. The
// function does incorporate `co_await` but since the async task is already
// meant for the current TQ, `co_await` should actually execute immediately
// without involving any asynchronous tasks.
TaskQueueCoTask ActuallySynchronous(absl::AnyInvocable<void() &&> done_cb) {
  TaskQueueBase* current_tq = TaskQueueBase::Current();
  co_await TaskQueueCoroutine{current_tq};
  std::move(done_cb)();
}

TEST_P(TaskQueueTest, Coroutine) {
  std::unique_ptr<TaskQueueFactory> factory = GetParam()(nullptr);
  Event event;
  auto queue1 = CreateTaskQueue(factory, "CoroutineTQ1");
  auto queue2 = CreateTaskQueue(factory, "CoroutineTQ2");
  queue1->PostTask([&event, &queue2] {
    struct Called {
      bool callback_called = false;
    };
    auto called = make_ref_counted<Called>();
    HoppingCoroutine(
        [&event, called]() {
          called->callback_called = true;
          event.Set();
        },
        queue2.get());
    // The couroutine will execute a few async tasks before invoking the
    // callback.
    EXPECT_FALSE(called->callback_called);
    EXPECT_FALSE(called->HasOneRef());
  });
  EXPECT_TRUE(event.Wait(TimeDelta::Seconds(1)));
}

// Test that when calling a coroutine that can perform work synchronously, does
// so without posting a task. This is to test compatibility with existing
// patterns where two conceptual task queues (or threads), are configured to
// refer to the same underlying task queue. Typically this is done via calls to
// Thread::BlockingCall() but also in proxy code (`pc/proxy.h`) where explicit
// checks are done for what TQ is active and depending on that either invoke the
// method directly or use a combination of PostTask()+Event::Wait(). This test
// covers the synchronous aspect of that pattern whereas the `Coroutine` test
// above covers the async part.
TEST_P(TaskQueueTest, CoroutineSynchronous) {
  std::unique_ptr<TaskQueueFactory> factory = GetParam()(nullptr);
  Event event;
  auto q = CreateTaskQueue(factory, "TQ");
  q->PostTask([&event] {
    bool done = false;
    // Don't use co_await, just call the function directly. This is to
    // make sure that if any async operations were actually queued then
    // we have not given them a chance to execute by using `co_wait`.
    // Since we know how `ActuallySynchronous` is implemented, we can
    // do that to verify that the full function ran without the
    // `co_wait` operation inside the coroutine, forking execution.
    ActuallySynchronous([&]() { done = true; });
    // Even though `ActuallySynchronous` is a coroutine it is intentionally
    // implemented to switch to the current TQ, which should happen without any
    // async operations.
    EXPECT_TRUE(done);
    event.Set();
  });
  EXPECT_TRUE(event.Wait(TimeDelta::Seconds(1)));
}

#endif  // BUILD_EXPERIMENTAL_TASK_QUEUE_COROUTINE_TESTS

// TaskQueueTest is a set of tests for any implementation of the TaskQueueBase.
// Tests are instantiated next to the concrete implementation(s).
// https://github.com/google/googletest/blob/master/googletest/docs/advanced.md#creating-value-parameterized-abstract-tests
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(TaskQueueTest);

}  // namespace
}  // namespace webrtc
