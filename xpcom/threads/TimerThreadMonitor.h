/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef TimerThreadMonitor_h_
#define TimerThreadMonitor_h_

#include "mozilla/CondVar.h"
#include "mozilla/Monitor.h"
#include "mozilla/Mutex.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/UniquePtrExtensions.h"
#include "prthread.h"

namespace mozilla {

/**
 * The monitor the timer thread waits on. Its timed waits take a tolerance
 * alongside the duration.
 *
 * A wait whose tolerance IsPreciseTolerance() accepts is scheduled as precisely
 * as the platform allows, bypassing OS-level wakeup coalescing at the cost of
 * power. A larger tolerance is handed to the OS as a budget within which it may
 * coalesce our wakeup with other work. Where the platform can express neither,
 * the wait runs to the end of that budget.
 *
 * That control comes from waiting on an OS timer object rather than on a
 * condition variable, and there is one such object per monitor: at most one
 * thread may wait at a time, asserted in debug builds, and NotifyAll() is
 * absent by design.
 *
 * Waits may return early or spuriously; as with a condition variable, the
 * caller must re-check its own predicate in a loop.
 */
class MOZ_CAPABILITY("monitor") TimerThreadMonitor {
 public:
  explicit TimerThreadMonitor(const char* aName);
  ~TimerThreadMonitor();

  void Lock() MOZ_CAPABILITY_ACQUIRE() { mMutex.Lock(); }
  void Unlock() MOZ_CAPABILITY_RELEASE() { mMutex.Unlock(); }

  // Wait until notified.
  void Wait() MOZ_REQUIRES(this);

  // Wait until aDuration has elapsed or until notified, whichever comes first.
  // The wakeup may be delayed by up to aTolerance to let the OS coalesce it.
  // A duration of TimeDuration::Forever() waits until notified.
  void Wait(TimeDuration aDuration, TimeDuration aTolerance) MOZ_REQUIRES(this);

  // Wake the waiting thread, if any.
  void Notify() MOZ_REQUIRES(this);

  void AssertCurrentThreadOwns() const MOZ_ASSERT_CAPABILITY(this) {
    mMutex.AssertCurrentThreadOwns();
  }
  void AssertNotCurrentThreadOwns() const MOZ_ASSERT_CAPABILITY(!this) {
    mMutex.AssertNotCurrentThreadOwns();
  }

  // Whether a tolerance is small enough to warrant a precise wait. Every
  // backend tests it here, so the boundary cannot drift between platforms.
  static bool IsPreciseTolerance(TimeDuration aTolerance) {
    return aTolerance <= TimeDuration::FromMilliseconds(16);
  }

  TimerThreadMonitor() = delete;
  TimerThreadMonitor(const TimerThreadMonitor&) = delete;
  TimerThreadMonitor& operator=(const TimerThreadMonitor&) = delete;
  TimerThreadMonitor(TimerThreadMonitor&&) = delete;
  TimerThreadMonitor& operator=(TimerThreadMonitor&&) = delete;

 private:
  void BeginWait() MOZ_REQUIRES(this) {
#ifdef DEBUG
    MOZ_ASSERT(!mWaiterThread,
               "TimerThreadMonitor supports only one waiting thread");
    mWaiterThread = PR_GetCurrentThread();
#endif
  }

  void EndWait() MOZ_REQUIRES(this) {
#ifdef DEBUG
    MOZ_ASSERT(mWaiterThread == PR_GetCurrentThread());
    mWaiterThread = nullptr;
#endif
  }

  void AssertNoWaiter() const MOZ_NO_THREAD_SAFETY_ANALYSIS {
#ifdef DEBUG
    MOZ_ASSERT(!mWaiterThread);
#endif
  }

  // Converts a duration to the whole microseconds the OS timer APIs take,
  // clamped to [0, 2^53]: an out-of-range floating point cast is undefined,
  // and that bound is exact as a double and far beyond any wait we could be
  // asked for.
  static constexpr int64_t kMaxWaitMicroseconds = 1LL << 53;
  static int64_t ToWaitMicroseconds(TimeDuration aDuration) {
    const double us = aDuration.ToMicroseconds();
    if (us <= 0.0) {
      return 0;
    }
    return us < static_cast<double>(kMaxWaitMicroseconds)
               ? static_cast<int64_t>(us)
               : kMaxWaitMicroseconds;
  }

  Mutex mMutex;

#if defined(XP_WIN)
  // mHiResTimer is null on Windows versions that lack high-resolution timers,
  // and unlike mLoResTimer it cannot be given a tolerance. mEvent is auto-reset
  // and signalled by Notify().
  UniqueFileHandle mHiResTimer;
  UniqueFileHandle mLoResTimer;
  UniqueFileHandle mEvent;
#elif defined(XP_MACOSX)
  // A kqueue holding a persistent EVFILT_USER for Notify() and, while waiting,
  // a one-shot EVFILT_TIMER for the deadline.
  UniqueFileHandle mKq;
#else
  CondVar mCondVar;
#endif

#ifdef DEBUG
  PRThread* mWaiterThread MOZ_GUARDED_BY(this) = nullptr;
#endif
};

using TimerThreadMonitorAutoLock = MonitorAutoLockBase<TimerThreadMonitor>;
using TimerThreadMonitorAutoUnlock = MonitorAutoUnlockBase<TimerThreadMonitor>;

}  // namespace mozilla

#endif  // TimerThreadMonitor_h_
