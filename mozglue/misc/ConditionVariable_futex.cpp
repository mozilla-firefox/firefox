/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/PlatformConditionVariable.h"
#include "mozilla/PlatformMutex.h"
#include "mozilla/Futex.h"

// All the memory orderings here are relaxed, because synchronization is done
// by unlocking and locking the mutex. A waiter increments mWaiters while
// holding the mutex, and a notifier must have modified the protected state
// while holding the same mutex, so it will see the waiter and wake it.

mozilla::detail::ConditionVariableImpl::ConditionVariableImpl() = default;
mozilla::detail::ConditionVariableImpl::~ConditionVariableImpl() = default;

void mozilla::detail::ConditionVariableImpl::notify_one() {
  mFutex.mValue.fetch_add(1, std::memory_order_relaxed);
  if (mWaiters.load(std::memory_order_relaxed)) {
    mFutex.wake();
  }
}

void mozilla::detail::ConditionVariableImpl::notify_all() {
  mFutex.mValue.fetch_add(1, std::memory_order_relaxed);
  if (mWaiters.load(std::memory_order_relaxed)) {
    mFutex.wakeAll();
  }
}

void mozilla::detail::ConditionVariableImpl::wait(MutexImpl& lock) {
  wait_for(lock, TimeDuration::Forever());
}

mozilla::CVStatus mozilla::detail::ConditionVariableImpl::wait_for(
    MutexImpl& lock, const mozilla::TimeDuration& rel_time) {
  mWaiters.fetch_add(1, std::memory_order_relaxed);
  // Examine the notification counter _before_ we unlock the mutex.
  uint32_t value = mFutex.mValue.load(std::memory_order_relaxed);
  // Unlock the mutex before going to sleep.
  lock.unlock();
  // Wait, but only if there hasn't been any notification since we unlocked the
  // mutex.
  bool r = mFutex.wait(value, &rel_time);
  mWaiters.fetch_sub(1, std::memory_order_relaxed);
  // Lock the mutex again.
  lock.lock();
  return r ? CVStatus::NoTimeout : CVStatus::Timeout;
}
