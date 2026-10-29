/*
 *  Copyright 2016 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

// This file contains the implementation of TaskQueue for Mac and iOS.
// The implementation uses Grand Central Dispatch queues (GCD) to
// do the actual task queuing.

#include "rtc_base/task_queue_gcd.h"

#include <dispatch/dispatch.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "absl/functional/any_invocable.h"
#include "absl/strings/string_view.h"
#include "api/location.h"
#include "api/task_queue/task_queue_base.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/units/time_delta.h"
#include "rtc_base/checks.h"
#include "rtc_base/system/gcd_helpers.h"

namespace webrtc {
namespace {

// TODO(crbug.com/470337728): make use of QoS instead of dispatch queue
// priorities to enable greater fidelity mapping of AUDIO and VIDEO
// priorities.
int TaskQueuePriorityToGCD(TaskQueueFactory::Priority priority) {
  switch (priority) {
    case TaskQueueFactory::Priority::kNormal:
      return DISPATCH_QUEUE_PRIORITY_DEFAULT;
    case TaskQueueFactory::Priority::kHigh:
    case TaskQueueFactory::Priority::kAudio:
    case TaskQueueFactory::Priority::kVideo:
      return DISPATCH_QUEUE_PRIORITY_HIGH;
    case TaskQueueFactory::Priority::kLow:
      return DISPATCH_QUEUE_PRIORITY_LOW;
  }
}

// Returns the leeway of the strict timer that runs a high precision task. Like
// for dispatch_after(), the leeway is 10% of the delay, but it's capped at
// 1 ms.
uint64_t HighPrecisionLeewayNs(TimeDelta delay) {
  return std::min(delay / 10, TimeDelta::Millis(1)).ns<uint64_t>();
}

class TaskQueueGcd final : public TaskQueueBase {
 public:
  TaskQueueGcd(absl::string_view queue_name, int gcd_priority);

  absl::string_view queue_name() const override { return name_; }
  void Delete() override;

 protected:
  void PostTaskImpl(absl::AnyInvocable<void() &&> task,
                    const PostTaskTraits& traits,
                    const Location& location) override;
  void PostDelayedTaskImpl(absl::AnyInvocable<void() &&> task,
                           TimeDelta delay,
                           const PostDelayedTaskTraits& traits,
                           const Location& location) override;

 private:
  struct TaskContext {
    TaskContext(TaskQueueGcd* queue, absl::AnyInvocable<void() &&> task)
        : queue(queue), task(std::move(task)) {}

    TaskQueueGcd* const queue;
    absl::AnyInvocable<void() &&> task;
    // Only set for tasks that are run by a strict timer. Released when the
    // timer fires.
    dispatch_source_t timer = nullptr;
  };

  ~TaskQueueGcd() override;
  static void RunTask(void* task_context);
  static void RunTimerTask(void* task_context);
  static void SetNotActive(void* task_queue);
  static void DeleteQueue(void* task_queue);

  const std::string name_;
  dispatch_queue_t queue_;
  bool is_active_;
};

TaskQueueGcd::TaskQueueGcd(absl::string_view queue_name, int gcd_priority)
    : name_(queue_name),
      queue_(RTCDispatchQueueCreateWithTarget(
          name_.c_str(),
          DISPATCH_QUEUE_SERIAL,
          dispatch_get_global_queue(gcd_priority, 0))),
      is_active_(true) {
  RTC_CHECK(queue_);
  dispatch_set_context(queue_, this);
  // Assign a finalizer that will delete the queue when the last reference
  // is released. This may run after the TaskQueue::Delete.
  dispatch_set_finalizer_f(queue_, &DeleteQueue);
}

TaskQueueGcd::~TaskQueueGcd() = default;

void TaskQueueGcd::Delete() {
  // Implementation/behavioral note:
  // Dispatch queues are reference counted via calls to dispatch_retain and
  // dispatch_release. Pending blocks submitted to a queue also hold a
  // reference to the queue until they have finished. Once all references to a
  // queue have been released, the queue will be deallocated by the system. We
  // check `is_active_` before running tasks to ensure they don't execute after
  // `Delete()` has been called.

  if (IsCurrent()) {
    SetNotActive(this);
  } else {
    // Use dispatch_sync to set the is_active_ to guarantee that there's not a
    // race with checking it from a task.
    dispatch_sync_f(queue_, this, &SetNotActive);
  }

  dispatch_release(queue_);
}

void TaskQueueGcd::PostTaskImpl(absl::AnyInvocable<void() &&> task,
                                const PostTaskTraits& traits,
                                const Location& location) {
  auto* context = new TaskContext(this, std::move(task));
  dispatch_async_f(queue_, context, &RunTask);
}

void TaskQueueGcd::PostDelayedTaskImpl(absl::AnyInvocable<void() &&> task,
                                       TimeDelta delay,
                                       const PostDelayedTaskTraits& traits,
                                       const Location& location) {
  auto* context = new TaskContext(this, std::move(task));
  const dispatch_time_t when =
      dispatch_time(DISPATCH_TIME_NOW, delay.us() * NSEC_PER_USEC);
  // dispatch_after() posts tasks without a positive delay right away, so a
  // timer is only needed for high precision tasks with a positive delay.
  if (!traits.high_precision || delay <= TimeDelta::Zero()) {
    dispatch_after_f(when, queue_, context, &RunTask);
    return;
  }

  // The OS may coalesce the timers of dispatch_after() with other timers, which
  // can delay tasks by much more than the timer leeway, up to 100 ms when the
  // process runs with background policy. A strict timer asks the OS to observe
  // the leeway instead. Like the timer behind dispatch_after(), the timer
  // source retains its target queue, `queue_`, until it's released.
  dispatch_source_t timer = dispatch_source_create(
      DISPATCH_SOURCE_TYPE_TIMER, 0, DISPATCH_TIMER_STRICT, queue_);
  RTC_CHECK(timer);
  context->timer = timer;
  // The event handler is called with the context that the source has when the
  // handler is set.
  dispatch_set_context(timer, context);
  dispatch_source_set_event_handler_f(timer, &RunTimerTask);
  dispatch_source_set_timer(timer, when, DISPATCH_TIME_FOREVER,
                            HighPrecisionLeewayNs(delay));
  // `context` may be deleted on `queue_` as soon as the timer is activated.
  dispatch_activate(timer);
}

// static
void TaskQueueGcd::RunTask(void* task_context) {
  std::unique_ptr<TaskContext> tc(static_cast<TaskContext*>(task_context));
  CurrentTaskQueueSetter set_current(tc->queue);
  if (tc->queue->is_active_) {
    std::move(tc->task)();
  }
  // Delete the task before CurrentTaskQueueSetter clears state that this code
  // is running on the task queue.
  tc = nullptr;
}

// static
void TaskQueueGcd::RunTimerTask(void* task_context) {
  auto* tc = static_cast<TaskContext*>(task_context);
  // The timer is one-shot. Cancel it so that it never fires again and release
  // it before running the task. GCD keeps the source alive while this handler
  // runs.
  dispatch_source_cancel(tc->timer);
  dispatch_release(tc->timer);
  tc->timer = nullptr;
  RunTask(tc);  // Takes ownership of `tc`.
}

// static
void TaskQueueGcd::SetNotActive(void* task_queue) {
  static_cast<TaskQueueGcd*>(task_queue)->is_active_ = false;
}

// static
void TaskQueueGcd::DeleteQueue(void* task_queue) {
  delete static_cast<TaskQueueGcd*>(task_queue);
}

class TaskQueueGcdFactory final : public TaskQueueFactory {
 public:
  std::unique_ptr<TaskQueueBase, TaskQueueDeleter> CreateTaskQueue(
      absl::string_view name,
      Priority priority) const override {
    return std::unique_ptr<TaskQueueBase, TaskQueueDeleter>(
        new TaskQueueGcd(name, TaskQueuePriorityToGCD(priority)));
  }
};

}  // namespace

std::unique_ptr<TaskQueueFactory> CreateTaskQueueGcdFactory() {
  return std::make_unique<TaskQueueGcdFactory>();
}

}  // namespace webrtc
