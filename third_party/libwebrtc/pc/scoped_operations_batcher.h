/*
 *  Copyright 2026 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef PC_SCOPED_OPERATIONS_BATCHER_H_
#define PC_SCOPED_OPERATIONS_BATCHER_H_

#include <utility>
#include <variant>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "api/rtc_error.h"
#include "api/sequence_checker.h"
#include "rtc_base/system/no_unique_address.h"
#include "rtc_base/thread.h"

namespace webrtc {

// Batches operations to be executed synchronously on a target thread.
//
// ScopedOperationsBatcher must only be created on the signaling thread.
//
// When the batcher goes out of scope (or `Run()` is explicitly called), it
// executes all queued tasks in one or more `BlockingCall`s to the target
// thread. If the target thread requests a yield during batch execution, the
// batcher will cooperatively yield the thread and resume execution in a
// subsequent `BlockingCall`.
//
// Tasks can either have a `void` return type, or return a `BatchTaskResult`
// that holds an error, a new task or both.
// Any tasks returned by the executed worker thread tasks are collected
// and subsequently executed on the calling thread (typically the signaling
// thread) after the worker thread operations have completed, including tasks
// that were returned along with an error.
class ScopedOperationsBatcher {
 public:
  using SimpleBatchTask = absl::AnyInvocable<void() &&>;
  using FinalizerTask = absl::AnyInvocable<void() &&>;

  // The result of a `BatchTaskWithFinalizer`. If `error` is set, the remaining
  // tasks are not executed. `finalizer`, if set, runs regardless of `error`.
  struct BatchTaskResult {
    BatchTaskResult() = default;
    BatchTaskResult(RTCError error)  // NOLINT(runtime/explicit)
        : error(std::move(error)) {}
    BatchTaskResult(FinalizerTask finalizer)  // NOLINT(runtime/explicit)
        : finalizer(std::move(finalizer)) {}
    BatchTaskResult(RTCError error, FinalizerTask finalizer)
        : error(std::move(error)), finalizer(std::move(finalizer)) {}

    RTCError error = RTCError::OK();
    FinalizerTask finalizer;
  };
  using BatchTaskWithFinalizer = absl::AnyInvocable<BatchTaskResult() &&>;

  explicit ScopedOperationsBatcher(Thread* target_thread);
  ~ScopedOperationsBatcher();

  RTCError Run();

  bool IsEmpty() const;

  // Queues non-nullptr tasks to be executed on the target thread when the
  // ScopedOperationsBatcher goes out of scope.
  void Add(SimpleBatchTask task);
  void AddWithFinalizer(BatchTaskWithFinalizer task);

 private:
  using BatchedTask = std::variant<SimpleBatchTask, BatchTaskWithFinalizer>;

  RTC_NO_UNIQUE_ADDRESS SequenceChecker sequence_checker_;
  Thread* const target_thread_;
  std::vector<BatchedTask> tasks_;
};

}  // namespace webrtc

#endif  // PC_SCOPED_OPERATIONS_BATCHER_H_
