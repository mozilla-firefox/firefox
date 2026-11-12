/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_CondVar_h
#define mozilla_CondVar_h

#include "mozilla/BlockingResourceBase.h"
#include "mozilla/Mutex.h"
#include "mozilla/PlatformConditionVariable.h"
#include "mozilla/TimeStamp.h"

#if defined(MOZILLA_INTERNAL_API) && !defined(DEBUG)
#  include "mozilla/ProfilerThreadSleep.h"
#endif  // defined( MOZILLA_INTERNAL_API) && !defined(DEBUG)

namespace mozilla {

/**
 * Similarly to OffTheBooksMutex, OffTheBooksCondvar is identical to CondVar,
 * except that OffTheBooksCondVar doesn't include leak checking.  Sometimes
 * you want to intentionally "leak" a CondVar until shutdown; in these cases,
 * OffTheBooksCondVar is for you.
 */
class OffTheBooksCondVar : BlockingResourceBase {
 public:
  /**
   * OffTheBooksCondVar
   *
   * The CALLER owns |aLock|.
   *
   * @param aLock A Mutex to associate with this condition variable.
   * @param aName A name which can reference this monitor
   * @returns If failure, nullptr.
   *          If success, a valid Monitor* which must be destroyed
   *          by Monitor::DestroyMonitor()
   **/
  OffTheBooksCondVar(OffTheBooksMutex& aLock, const char* aName)
      : BlockingResourceBase(aName, eCondVar), mLock(&aLock) {}

  /**
   * ~OffTheBooksCondVar
   * Clean up after this OffTheBooksCondVar, but NOT its associated Mutex.
   **/
  ~OffTheBooksCondVar() = default;

  OffTheBooksCondVar(const OffTheBooksCondVar&) = delete;
  OffTheBooksCondVar& operator=(const OffTheBooksCondVar&) = delete;

  /**
   * Wait
   * @see prcvar.h
   **/
#ifndef DEBUG
  void Wait() {
#  ifdef MOZILLA_INTERNAL_API
    AUTO_PROFILER_THREAD_SLEEP;
#  endif  // MOZILLA_INTERNAL_API
    mImpl.wait(*mLock);
  }

  CVStatus Wait(TimeDuration aDuration) {
#  ifdef MOZILLA_INTERNAL_API
    AUTO_PROFILER_THREAD_SLEEP;
#  endif  // MOZILLA_INTERNAL_API
    return mImpl.wait_for(*mLock, aDuration);
  }
#else
  // NOTE: debug impl is in BlockingResourceBase.cpp
  void Wait();
  CVStatus Wait(TimeDuration aDuration);
#endif

  /**
   * Notify
   * @see prcvar.h
   **/
  void Notify() { mImpl.notify_one(); }

  /**
   * NotifyAll
   * @see prcvar.h
   **/
  void NotifyAll() { mImpl.notify_all(); }

#ifdef DEBUG
  /**
   * AssertCurrentThreadOwnsMutex
   * @see Mutex::AssertCurrentThreadOwns
   **/
  void AssertCurrentThreadOwnsMutex() const MOZ_ASSERT_CAPABILITY(mLock) {
    mLock->AssertCurrentThreadOwns();
  }

  /**
   * AssertNotCurrentThreadOwnsMutex
   * @see Mutex::AssertNotCurrentThreadOwns
   **/
  void AssertNotCurrentThreadOwnsMutex() const MOZ_ASSERT_CAPABILITY(!mLock) {
    mLock->AssertNotCurrentThreadOwns();
  }

#else
  void AssertCurrentThreadOwnsMutex() const MOZ_ASSERT_CAPABILITY(mLock) {}
  void AssertNotCurrentThreadOwnsMutex() const MOZ_ASSERT_CAPABILITY(!mLock) {}

#endif  // ifdef DEBUG

  OffTheBooksCondVar() = delete;

 private:
  OffTheBooksMutex* mLock;
  detail::ConditionVariableImpl mImpl;
};

/**
 * CondVar
 * Vanilla condition variable.  Please don't use this unless you have a
 * compelling reason --- Monitor provides a simpler API.
 */
class CondVar : public OffTheBooksCondVar {
 public:
  CondVar(OffTheBooksMutex& aLock, const char* aName)
      : OffTheBooksCondVar(aLock, aName) {
    MOZ_COUNT_CTOR(CondVar);
  }

  CondVar() = delete;
  CondVar(const CondVar&) = delete;
  CondVar& operator=(const CondVar&) = delete;

  MOZ_COUNTED_DTOR(CondVar)
};

}  // namespace mozilla

#endif  // ifndef mozilla_CondVar_h
