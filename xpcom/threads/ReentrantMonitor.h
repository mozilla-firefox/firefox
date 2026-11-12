/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_ReentrantMonitor_h
#define mozilla_ReentrantMonitor_h

#include "prmon.h"

#if defined(MOZILLA_INTERNAL_API) && !defined(DEBUG)
#  include "mozilla/ProfilerThreadSleep.h"
#endif  // defined( MOZILLA_INTERNAL_API) && !defined(DEBUG)

#include "mozilla/BlockingResourceBase.h"
#include "mozilla/ThreadSafety.h"
#include "nsISupports.h"
//
// Provides:
//
//  - ReentrantMonitor, a Java-like monitor
//  - ReentrantMonitorAutoEnter, an RAII class for ensuring that
//    ReentrantMonitors are properly entered and exited
//
// Using ReentrantMonitorAutoEnter is MUCH preferred to making bare calls to
// ReentrantMonitor.Enter and Exit.
//
namespace mozilla {

/**
 * ReentrantMonitor
 * Java-like monitor.
 * When possible, use ReentrantMonitorAutoEnter to hold this monitor within a
 * scope, instead of calling Enter/Exit directly.
 **/
class MOZ_CAPABILITY("reentrant monitor") ReentrantMonitor
    : BlockingResourceBase {
 public:
  /**
   * ReentrantMonitor
   * @param aName A name which can reference this monitor
   */
  explicit ReentrantMonitor(const char* aName)
      : BlockingResourceBase(aName, eReentrantMonitor)
#ifdef DEBUG
        ,
        mEntryCount(0)
#endif
  {
    MOZ_COUNT_CTOR(ReentrantMonitor);
    mReentrantMonitor = PR_NewMonitor();
    if (!mReentrantMonitor) {
      MOZ_CRASH("Can't allocate mozilla::ReentrantMonitor");
    }
  }

  /**
   * ~ReentrantMonitor
   **/
  ~ReentrantMonitor() {
    NS_ASSERTION(mReentrantMonitor,
                 "improperly constructed ReentrantMonitor or double free");
    PR_DestroyMonitor(mReentrantMonitor);
    mReentrantMonitor = nullptr;
    MOZ_COUNT_DTOR(ReentrantMonitor);
  }

#ifndef DEBUG
  /**
   * Enter
   * @see prmon.h
   **/
  void Enter() MOZ_CAPABILITY_ACQUIRE() { PR_EnterMonitor(mReentrantMonitor); }

  /**
   * Exit
   * @see prmon.h
   **/
  void Exit() MOZ_CAPABILITY_RELEASE() { PR_ExitMonitor(mReentrantMonitor); }

  /**
   * Wait
   * @see prmon.h
   **/
  nsresult Wait(PRIntervalTime aInterval = PR_INTERVAL_NO_TIMEOUT) {
    PR_ASSERT_CURRENT_THREAD_IN_MONITOR(mReentrantMonitor);
#  ifdef MOZILLA_INTERNAL_API
    AUTO_PROFILER_THREAD_SLEEP;
#  endif  // MOZILLA_INTERNAL_API
    return PR_Wait(mReentrantMonitor, aInterval) == PR_SUCCESS
               ? NS_OK
               : NS_ERROR_FAILURE;
  }

#else  // ifndef DEBUG
  void Enter() MOZ_CAPABILITY_ACQUIRE();
  void Exit() MOZ_CAPABILITY_RELEASE();
  nsresult Wait(PRIntervalTime aInterval = PR_INTERVAL_NO_TIMEOUT);

#endif  // ifndef DEBUG

  /**
   * Notify
   * @see prmon.h
   **/
  nsresult Notify() {
    return PR_Notify(mReentrantMonitor) == PR_SUCCESS ? NS_OK
                                                      : NS_ERROR_FAILURE;
  }

  /**
   * NotifyAll
   * @see prmon.h
   **/
  nsresult NotifyAll() {
    return PR_NotifyAll(mReentrantMonitor) == PR_SUCCESS ? NS_OK
                                                         : NS_ERROR_FAILURE;
  }

#ifdef DEBUG
  /**
   * AssertCurrentThreadIn
   * @see prmon.h
   **/
  void AssertCurrentThreadIn() MOZ_ASSERT_CAPABILITY(this) {
    PR_ASSERT_CURRENT_THREAD_IN_MONITOR(mReentrantMonitor);
  }

  /**
   * AssertNotCurrentThreadIn
   * @see prmon.h
   **/
  void AssertNotCurrentThreadIn() MOZ_ASSERT_CAPABILITY(!this) {
    // FIXME bug 476536
  }

#else
  void AssertCurrentThreadIn() MOZ_ASSERT_CAPABILITY(this) {}
  void AssertNotCurrentThreadIn() MOZ_ASSERT_CAPABILITY(!this) {}

#endif  // ifdef DEBUG

  ReentrantMonitor() = delete;
  ReentrantMonitor(const ReentrantMonitor&) = delete;
  ReentrantMonitor& operator=(const ReentrantMonitor&) = delete;

 private:
  PRMonitor* mReentrantMonitor;
#ifdef DEBUG
  int32_t mEntryCount;
#endif
};

/**
 * ReentrantMonitorAutoEnter
 * Enters the ReentrantMonitor when it enters scope, and exits it when
 * it leaves scope.
 *
 * MUCH PREFERRED to bare calls to ReentrantMonitor.Enter and Exit.
 */
class MOZ_SCOPED_CAPABILITY MOZ_STACK_CLASS ReentrantMonitorAutoEnter {
 public:
  /**
   * Constructor
   * The constructor aquires the given lock.  The destructor
   * releases the lock.
   *
   * @param aReentrantMonitor A valid mozilla::ReentrantMonitor*.
   **/
  explicit ReentrantMonitorAutoEnter(
      mozilla::ReentrantMonitor& aReentrantMonitor)
      MOZ_CAPABILITY_ACQUIRE(aReentrantMonitor)
      : mReentrantMonitor(&aReentrantMonitor) {
    NS_ASSERTION(mReentrantMonitor, "null monitor");
    mReentrantMonitor->Enter();
  }

  ~ReentrantMonitorAutoEnter(void) MOZ_CAPABILITY_RELEASE() {
    mReentrantMonitor->Exit();
  }

  nsresult Wait(PRIntervalTime aInterval = PR_INTERVAL_NO_TIMEOUT) {
    return mReentrantMonitor->Wait(aInterval);
  }

  nsresult Notify() { return mReentrantMonitor->Notify(); }
  nsresult NotifyAll() { return mReentrantMonitor->NotifyAll(); }

 private:
  ReentrantMonitorAutoEnter();
  ReentrantMonitorAutoEnter(const ReentrantMonitorAutoEnter&);
  ReentrantMonitorAutoEnter& operator=(const ReentrantMonitorAutoEnter&);
  static void* operator new(size_t) noexcept(true);

  friend class ReentrantMonitorAutoExit;

  mozilla::ReentrantMonitor* mReentrantMonitor;
};

/**
 * ReentrantMonitorAutoExit
 * Exit the ReentrantMonitor when it enters scope, and enters it when it leaves
 * scope.
 *
 * MUCH PREFERRED to bare calls to ReentrantMonitor.Exit and Enter.
 */
class MOZ_SCOPED_CAPABILITY MOZ_STACK_CLASS ReentrantMonitorAutoExit {
 public:
  /**
   * Constructor
   * The constructor releases the given lock.  The destructor
   * acquires the lock. The lock must be held before constructing
   * this object!
   *
   * @param aReentrantMonitor A valid mozilla::ReentrantMonitor*. It
   *                 must be already locked.
   **/
  explicit ReentrantMonitorAutoExit(ReentrantMonitor& aReentrantMonitor)
      MOZ_EXCLUSIVE_RELEASE(aReentrantMonitor)
      : mReentrantMonitor(&aReentrantMonitor) {
    NS_ASSERTION(mReentrantMonitor, "null monitor");
    mReentrantMonitor->AssertCurrentThreadIn();
    mReentrantMonitor->Exit();
  }

  explicit ReentrantMonitorAutoExit(
      ReentrantMonitorAutoEnter& aReentrantMonitorAutoEnter)
      MOZ_EXCLUSIVE_RELEASE(aReentrantMonitorAutoEnter.mReentrantMonitor)
      : mReentrantMonitor(aReentrantMonitorAutoEnter.mReentrantMonitor) {
    NS_ASSERTION(mReentrantMonitor, "null monitor");
    mReentrantMonitor->AssertCurrentThreadIn();
    mReentrantMonitor->Exit();
  }

  ~ReentrantMonitorAutoExit(void) MOZ_EXCLUSIVE_RELEASE() {
    mReentrantMonitor->Enter();
  }

 private:
  ReentrantMonitorAutoExit();
  ReentrantMonitorAutoExit(const ReentrantMonitorAutoExit&);
  ReentrantMonitorAutoExit& operator=(const ReentrantMonitorAutoExit&);
  static void* operator new(size_t) noexcept(true);

  ReentrantMonitor* mReentrantMonitor;
};

}  // namespace mozilla

#endif  // ifndef mozilla_ReentrantMonitor_h
