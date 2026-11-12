/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "mozilla/CondVar.h"
#include "mozilla/Mutex.h"
#include "mozilla/RecursiveMutex.h"
#include "mozilla/ReentrantMonitor.h"
#include "mozilla/gtest/MozHelpers.h"
#include "nsCOMPtr.h"
#include "nsTArray.h"
#include "prthread.h"

using namespace mozilla;

// The code in this file is also used by
// storage/test/gtest/test_deadlock_detector.cpp. The following two macros are
// used to provide the necessary differentiation between this file and that
// file.
#ifndef MUTEX
#  define MUTEX mozilla::Mutex
#endif
#ifndef TESTNAME
#  define TESTNAME(name) XPCOM##name
#endif

static PRThread* spawn(void (*run)(void*), void* arg) {
  return PR_CreateThread(PR_SYSTEM_THREAD, run, arg, PR_PRIORITY_NORMAL,
                         PR_GLOBAL_THREAD, PR_JOINABLE_THREAD, 0);
}

/**
 * Simple test fixture that makes sure the gdb sleep setup in the
 * ah crap handler is bypassed during the death tests.
 */
class TESTNAME(DeadlockDetectorTest) : public ::testing::Test {
 protected:
  void SetUp() final { SAVE_GDB_SLEEP_GLOBAL(mOldSleepDuration); }

  void TearDown() final { RESTORE_GDB_SLEEP_GLOBAL(mOldSleepDuration); }

 private:
#if defined(HAS_GDB_SLEEP_DURATION)
  unsigned int mOldSleepDuration;
#endif  // defined(HAS_GDB_SLEEP_DURATION)
};

//-----------------------------------------------------------------------------
// Single-threaded sanity tests

// Stupidest possible deadlock.
static int Sanity_Child() MOZ_NO_THREAD_SAFETY_ANALYSIS {
  mozilla::gtest::DisableCrashReporter();

  MUTEX m1("dd.sanity.m1");
  m1.Lock();
  m1.Lock();
  return 0;  // not reached
}

TEST_F(TESTNAME(DeadlockDetectorTest), TESTNAME(SanityDeathTest)) {
  const char* const regex =
      "###!!! ERROR: Potential deadlock detected.*"
      "=== Cyclical dependency starts at.*--- Mutex : dd.sanity.m1.*"
      "=== Cycle completed at.*--- Mutex : dd.sanity.m1.*"
      "###!!! Deadlock may happen NOW!.*"  // better catch these easy cases...
      "###!!! ASSERTION: Potential deadlock detected.*";

  ASSERT_DEATH_IF_SUPPORTED(Sanity_Child(), regex);
}

// Slightly less stupid deadlock.
static int Sanity2_Child() MOZ_NO_THREAD_SAFETY_ANALYSIS {
  mozilla::gtest::DisableCrashReporter();

  MUTEX m1("dd.sanity2.m1");
  MUTEX m2("dd.sanity2.m2");
  m1.Lock();
  m2.Lock();
  m1.Lock();
  return 0;  // not reached
}

TEST_F(TESTNAME(DeadlockDetectorTest), TESTNAME(Sanity2DeathTest)) {
  const char* const regex =
      "###!!! ERROR: Potential deadlock detected.*"
      "=== Cyclical dependency starts at.*--- Mutex : dd.sanity2.m1.*"
      "--- Next dependency:.*--- Mutex : dd.sanity2.m2.*"
      "=== Cycle completed at.*--- Mutex : dd.sanity2.m1.*"
      "###!!! Deadlock may happen NOW!.*"  // better catch these easy cases...
      "###!!! ASSERTION: Potential deadlock detected.*";

  ASSERT_DEATH_IF_SUPPORTED(Sanity2_Child(), regex);
}

static int Sanity3_Child() MOZ_NO_THREAD_SAFETY_ANALYSIS {
  mozilla::gtest::DisableCrashReporter();

  MUTEX m1("dd.sanity3.m1");
  MUTEX m2("dd.sanity3.m2");
  MUTEX m3("dd.sanity3.m3");
  MUTEX m4("dd.sanity3.m4");

  m1.Lock();
  m2.Lock();
  m3.Lock();
  m4.Lock();
  m4.Unlock();
  m3.Unlock();
  m2.Unlock();
  m1.Unlock();

  m4.Lock();
  m1.Lock();
  MOZ_CRASH();
}

TEST_F(TESTNAME(DeadlockDetectorTest), TESTNAME(Sanity3DeathTest)) {
  const char* const regex =
      "###!!! ERROR: Potential deadlock detected.*"
      "=== Cyclical dependency starts at.*--- Mutex : dd.sanity3.m1.*"
      "--- Next dependency:.*--- Mutex : dd.sanity3.m2.*"
      "--- Next dependency:.*--- Mutex : dd.sanity3.m3.*"
      "--- Next dependency:.*--- Mutex : dd.sanity3.m4.*"
      "=== Cycle completed at.*--- Mutex : dd.sanity3.m1.*"
      "Deadlock may happen for some other execution.*";

  ASSERT_DEATH_IF_SUPPORTED(Sanity3_Child(), regex);
}

static int Sanity4_Child() MOZ_NO_THREAD_SAFETY_ANALYSIS {
  mozilla::gtest::DisableCrashReporter();

  mozilla::ReentrantMonitor m1 MOZ_UNANNOTATED("dd.sanity4.m1");
  MUTEX m2("dd.sanity4.m2");
  m1.Enter();
  m2.Lock();
  m1.Enter();
  return 0;
}

TEST_F(TESTNAME(DeadlockDetectorTest), TESTNAME(Sanity4DeathTest)) {
  const char* const regex =
      "Re-entering ReentrantMonitor after acquiring other resources.*"
      "###!!! ERROR: Potential deadlock detected.*"
      "=== Cyclical dependency starts at.*--- ReentrantMonitor : "
      "dd.sanity4.m1.*"
      "--- Next dependency:.*--- Mutex : dd.sanity4.m2.*"
      "=== Cycle completed at.*--- ReentrantMonitor : dd.sanity4.m1.*"
      "###!!! ASSERTION: Potential deadlock detected.*";
  ASSERT_DEATH_IF_SUPPORTED(Sanity4_Child(), regex);
}

static int Sanity5_Child() MOZ_NO_THREAD_SAFETY_ANALYSIS {
  mozilla::gtest::DisableCrashReporter();

  mozilla::RecursiveMutex m1 MOZ_UNANNOTATED("dd.sanity4.m1");
  MUTEX m2("dd.sanity4.m2");
  m1.Lock();
  m2.Lock();
  m1.Lock();
  return 0;
}

#if !defined(DISABLE_STORAGE_SANITY5_DEATH_TEST)
TEST_F(TESTNAME(DeadlockDetectorTest), TESTNAME(Sanity5DeathTest)) {
  const char* const regex =
      "Re-entering RecursiveMutex after acquiring other resources.*"
      "###!!! ERROR: Potential deadlock detected.*"
      "=== Cyclical dependency starts at.*--- RecursiveMutex : dd.sanity4.m1.*"
      "--- Next dependency:.*--- Mutex : dd.sanity4.m2.*"
      "=== Cycle completed at.*--- RecursiveMutex : dd.sanity4.m1.*"
      "###!!! ASSERTION: Potential deadlock detected.*";
  ASSERT_DEATH_IF_SUPPORTED(Sanity5_Child(), regex);
}
#endif

//-----------------------------------------------------------------------------
// Multithreaded tests

/**
 * Helper for passing state to threads in the multithread tests.
 */
struct ThreadState {
  /**
   * Locks to use during the test. This is just a reference and is owned by
   * the main test thread.
   */
  const nsTArray<MUTEX*>& locks;

  /**
   * Integer argument used to identify each thread.
   */
  int id;
};

#if 0
// Temporarily disabled, see bug 1370644.
static void
TwoThreads_thread(void* arg) MOZ_NO_THREAD_SAFETY_ANALYSIS
{
    ThreadState* state = static_cast<ThreadState*>(arg);

    MUTEX* ttM1 = state->locks[0];
    MUTEX* ttM2 = state->locks[1];

    if (state->id) {
        ttM1->Lock();
        ttM2->Lock();
        ttM2->Unlock();
        ttM1->Unlock();
    }
    else {
        ttM2->Lock();
        ttM1->Lock();
        ttM1->Unlock();
        ttM2->Unlock();
    }
}

int
TwoThreads_Child() MOZ_NO_THREAD_SAFETY_ANALYSIS
{
    mozilla::gtest::DisableCrashReporter();

    nsTArray<MUTEX*> locks = {
      new MUTEX("dd.twothreads.m1"),
      new MUTEX("dd.twothreads.m2")
    };

    ThreadState state_1 {locks, 0};
    PRThread* t1 = spawn(TwoThreads_thread, &state_1);
    PR_JoinThread(t1);

    ThreadState state_2 {locks, 1};
    PRThread* t2 = spawn(TwoThreads_thread, &state_2);
    PR_JoinThread(t2);

    for (auto& lock : locks) {
      delete lock;
    }

    return 0;
}

TEST_F(TESTNAME(DeadlockDetectorTest), TESTNAME(TwoThreadsDeathTest))
{
    const char* const regex =
        "###!!! ERROR: Potential deadlock detected.*"
        "=== Cyclical dependency starts at.*--- Mutex : dd.twothreads.m2.*"
        "--- Next dependency:.*--- Mutex : dd.twothreads.m1.*"
        "=== Cycle completed at.*--- Mutex : dd.twothreads.m2.*"
        "###!!! ASSERTION: Potential deadlock detected.*";

    ASSERT_DEATH_IF_SUPPORTED(TwoThreads_Child(), regex);
}
#endif

static void ContentionNoDeadlock_thread(void* arg)
    MOZ_NO_THREAD_SAFETY_ANALYSIS {
  const uint32_t K = 100000;

  ThreadState* state = static_cast<ThreadState*>(arg);
  int32_t starti = static_cast<int32_t>(state->id);
  auto& cndMs = state->locks;

  for (uint32_t k = 0; k < K; ++k) {
    for (int32_t i = starti; i < (int32_t)cndMs.Length(); ++i) cndMs[i]->Lock();
    // comment out the next two lines for deadlocking fun!
    for (int32_t i = cndMs.Length() - 1; i >= starti; --i) cndMs[i]->Unlock();

    starti = (starti + 1) % 3;
  }
}

static int ContentionNoDeadlock_Child() MOZ_NO_THREAD_SAFETY_ANALYSIS {
  const size_t kMutexCount = 4;

  PRThread* threads[3];
  nsTArray<MUTEX*> locks;
  ThreadState states[] = {{locks, 0}, {locks, 1}, {locks, 2}};

  for (uint32_t i = 0; i < kMutexCount; ++i)
    locks.AppendElement(new MUTEX("dd.cnd.ms"));

  for (int32_t i = 0; i < (int32_t)std::size(threads); ++i)
    threads[i] = spawn(ContentionNoDeadlock_thread, states + i);

  for (uint32_t i = 0; i < std::size(threads); ++i) PR_JoinThread(threads[i]);

  for (uint32_t i = 0; i < locks.Length(); ++i) delete locks[i];

  return 0;
}

TEST_F(TESTNAME(DeadlockDetectorTest), TESTNAME(ContentionNoDeadlock)) {
  // Just check that this test runs to completion.
  ASSERT_EQ(ContentionNoDeadlock_Child(), 0);
}
