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

#include <cstddef>
#include <utility>
#include <variant>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "api/rtc_error.h"
#include "api/sequence_checker.h"
#include "rtc_base/checks.h"
#include "rtc_base/thread.h"

namespace webrtc {

ScopedOperationsBatcher::ScopedOperationsBatcher(Thread* target_thread)
    : target_thread_(target_thread) {
  RTC_DCHECK(target_thread_);
}

ScopedOperationsBatcher::~ScopedOperationsBatcher() {
  Run();
}

RTCError ScopedOperationsBatcher::Run() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  const bool target_thread_is_current = target_thread_->IsCurrent();

#if RTC_DCHECK_IS_ON
  RTC_LOG_THREAD_BLOCK_COUNT();
  int expected_block_count = tasks_.empty() ? 0 : 1;
  if (target_thread_is_current) {
    // Many tests in peerconnection_unittests run in single threaded mode where
    // the operations below will not be accurately measured. Yielding is not
    // supported for single threaded mode and tasks in those tests may
    // internally run blocking tasks themselves, which affects the count but is
    // not useful for the purposes of measuring the multithreaded behavior.
    RTC_IGNORE_THREAD_BLOCK_COUNT();
  }
#endif

  std::vector<FinalizerTask> return_tasks;
  size_t task_idx = 0;

  RTCError error = RTCError::OK();
  while (task_idx < tasks_.size()) {
    size_t previous_task_idx = task_idx;
    target_thread_->BlockingCall([&] {
      while (task_idx < tasks_.size()) {
        if (auto* void_task = std::get_if<SimpleBatchTask>(&tasks_[task_idx])) {
          std::move (*void_task)();
        } else {
          auto* returning_task =
              std::get_if<BatchTaskWithFinalizer>(&tasks_[task_idx]);
          RTC_DCHECK(returning_task);
          BatchTaskResult result = std::move(*returning_task)();
          if (result.finalizer) {
            return_tasks.push_back(std::move(result.finalizer));
          }
          error = std::move(result.error);
        }
        ++task_idx;
        if (!error.ok()) {
          return;
        }
        if (!target_thread_is_current && target_thread_->HasPendingTasks()) {
#if RTC_DCHECK_IS_ON
          ++expected_block_count;
#endif
          return;
        }
      }
    });
    // If the target thread dropped the task (e.g. because it is quitting),
    // task_idx will not have advanced. Abort to prevent an infinite busy-loop.
    // This is not expected to happen under normal circumstances but if the
    // integrating application prematurely terminates the target thread, that's
    // when it can. However, doing so, breaks design assumptions.
    if (error.ok() && task_idx == previous_task_idx) {
      error = RTCError::InternalError("Thread was prematurely terminated");
    }
    if (!error.ok()) {
      RTC_LOG_ERROR(error);
      break;
    }
  }

  tasks_.clear();

  for (auto& task : return_tasks) {
    std::move(task)();
  }

#if RTC_DCHECK_IS_ON
  // If this triggers, then likely one of the `return_tasks` has issued a
  // blocking call.
  RTC_DCHECK_BLOCK_COUNT_NO_MORE_THAN(expected_block_count);
#endif
  return error;
}

bool ScopedOperationsBatcher::IsEmpty() const {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  return tasks_.empty();
}

void ScopedOperationsBatcher::Add(SimpleBatchTask task) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  if (task) {
    tasks_.emplace_back(std::move(task));
  }
}

void ScopedOperationsBatcher::AddWithFinalizer(BatchTaskWithFinalizer task) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  if (task) {
    tasks_.emplace_back(std::move(task));
  }
}

}  // namespace webrtc
