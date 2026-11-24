/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"
#include "mozilla/Assertions.h"
#include "mozilla/CondVar.h"
#include "mozilla/Logging.h"
#include "mozilla/Mutex.h"
#include "mozilla/ThreadSafety.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/gtest/MozAssertions.h"
#include "nsIThread.h"
#include "nsThreadPool.h"
#include "nsThreadUtils.h"
#include "pratom.h"
#include "prinrval.h"
#include "prmon.h"
#include "prthread.h"

using namespace mozilla;

#define NUMBER_OF_MAX_THREADS ((uint32_t)6)
#define NUMBER_OF_IDLE_THREADS ((uint32_t)3)
#define IDLE_THREAD_GRACE_TIMEOUT 250
#define IDLE_THREAD_MAX_TIMEOUT 1000

namespace TestThreadPoolIdleTimeout {

// Given that this test wants to check if timeouts happen when they should,
// and given that timeouts can be unreliable depending on the execution
// environment, while running the timeout test we run in parallel some
// extra timeouts of known duration in order to check for their deviation.
// This allows us to skip test conditions if the deviation is getting
// significant.
//
// The checker uses CondVar::Wait to measure timeout precision because the
// thread pool idle timeout itself uses CondVar::Wait internally. This ensures
// the checker reflects the same timing characteristics as the code under test.
template <uint32_t ms, size_t repeats>
class ScopedTimingChecker {
  double& mDeviationPerc;
  nsCOMPtr<nsIThread> mThread;

 public:
  explicit ScopedTimingChecker(double& aDeviationPerc)
      : mDeviationPerc(aDeviationPerc) {
    NS_NewNamedThread(
        "TimingCheck", getter_AddRefs(mThread),
        NS_NewRunnableFunction("TimingCheckRun", [this]() {
          Mutex mutex{"ScopedTimingCheckMutex"};
          CondVar condVar(mutex, "ScopedTimingCheckCondVar");
          double maxDeviation = 0.0;
          for (size_t i = 0; i < repeats; i++) {
            TimeStamp before = TimeStamp::Now();
            TimeStamp deadline = before + TimeDuration::FromMilliseconds(ms);
            {
              MutexAutoLock lock(mutex);
              // This condvar is never notified; we wait on it (rather than
              // using e.g. PR_Sleep) precisely because nsThreadPool's idle
              // timeout waits on a CondVar too, so measuring this primitive
              // reflects the timing characteristics of the code under test.
              // Like nsThreadPool::Run, we re-check the elapsed wall-clock time
              // against the deadline and keep waiting until it has truly
              // passed, so a spurious wakeup doesn't cut the measurement short.
              for (TimeStamp now = before; now < deadline;
                   now = TimeStamp::Now()) {
                condVar.Wait(deadline - now);
              }
            }
            double elapsed = (TimeStamp::Now() - before).ToMilliseconds();
            maxDeviation = std::max(maxDeviation, std::abs(elapsed - ms));
          }
          mDeviationPerc = 100.0 * (maxDeviation / ms);
        }));
  }

  ~ScopedTimingChecker() {
    mThread->Shutdown();
    printf("ScopedTimingChecker calculated %.2f %% deviation.\n",
           mDeviationPerc);
  }
};

class Listener final : public nsIThreadPoolListener {
  ~Listener() = default;

  TimeStamp mExecStart;
  Atomic<uint32_t>& mNumberOfThreadsCurrent;
  Atomic<uint32_t>& mNumberOfThreadsCreated;
  Mutex& mWaitMutex;
  CondVar& mWaitForIdleLimit;
  CondVar& mWaitForNoThreadsAlive;

 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSITHREADPOOLLISTENER

  Listener(TimeStamp aStart, Atomic<uint32_t>& aNumberOfThreadsCurrent,
           Atomic<uint32_t>& aNumberOfThreadsCreated, Mutex& aWaitMutex,
           CondVar& aWaitForGrace, CondVar& aWaitForMaximum)
      : mExecStart(aStart),
        mNumberOfThreadsCurrent(aNumberOfThreadsCurrent),
        mNumberOfThreadsCreated(aNumberOfThreadsCreated),
        mWaitMutex(aWaitMutex),
        mWaitForIdleLimit(aWaitForGrace),
        mWaitForNoThreadsAlive(aWaitForMaximum) {}
};

NS_IMPL_ISUPPORTS(Listener, nsIThreadPoolListener)

NS_IMETHODIMP
Listener::OnThreadCreated() {
  // We cannot lock gWaitMutex here, we would deadlock, thus
  // the Atomic<gNumberOfThreadsX> above.
  ++mNumberOfThreadsCurrent;
  ++mNumberOfThreadsCreated;
  printf("%u Start new thread. %u alive, %u ever created.\n",
         (uint32_t)(TimeStamp::Now() - mExecStart).ToMilliseconds(),
         (uint32_t)mNumberOfThreadsCurrent, (uint32_t)mNumberOfThreadsCreated);
  return NS_OK;
}

NS_IMETHODIMP
Listener::OnThreadShuttingDown() {
  MutexAutoLock lock(mWaitMutex);
  --mNumberOfThreadsCurrent;
  if (mNumberOfThreadsCurrent == NUMBER_OF_IDLE_THREADS) {
    mWaitForIdleLimit.Notify();
  }
  if (mNumberOfThreadsCurrent == 0) {
    mWaitForNoThreadsAlive.Notify();
  }
  printf("%u Shutdown thread. %u alive, %u ever created.\n",
         (uint32_t)(TimeStamp::Now() - mExecStart).ToMilliseconds(),
         (uint32_t)mNumberOfThreadsCurrent, (uint32_t)mNumberOfThreadsCreated);
  return NS_OK;
}

// We want to see how the thread pool behaves under different loads
// in terms of creating and/or timing out threads as expected.
TEST(ThreadPoolIdleTimeout, Test)
{
  nsresult rv;

  // Setup pool and environment.
  Mutex waitMutex("WaitMutex");
  CondVar waitForPeak(waitMutex, "WaitForPeak");
  CondVar waitForIdleAfterPeak(waitMutex, "WaitForIdleAfterPeak");
  CondVar waitForGrace(waitMutex, "WaitForGrace");
  CondVar waitForMaximum(waitMutex, "WaitForMaximum");
  TimeStamp execStart = TimeStamp::Now();
  Atomic<uint32_t> numberOfThreads(0);
  Atomic<uint32_t> numberOfThreadsCreated(0);
  Atomic<uint32_t> numberOfActivationRunnables(0);
  bool peakReached = false;

  nsCOMPtr<nsIThreadPool> pool = new nsThreadPool();

  rv = pool->SetThreadLimit(NUMBER_OF_MAX_THREADS);
  ASSERT_NS_SUCCEEDED(rv);

  rv = pool->SetIdleThreadLimit(NUMBER_OF_IDLE_THREADS);
  ASSERT_NS_SUCCEEDED(rv);

  rv = pool->SetIdleThreadGraceTimeout(IDLE_THREAD_GRACE_TIMEOUT);
  ASSERT_NS_SUCCEEDED(rv);

  rv = pool->SetIdleThreadMaximumTimeout(IDLE_THREAD_MAX_TIMEOUT);
  ASSERT_NS_SUCCEEDED(rv);

  pool->SetName("IdleTest"_ns);

  nsCOMPtr<nsIThreadPoolListener> listener =
      new Listener(execStart, numberOfThreads, numberOfThreadsCreated,
                   waitMutex, waitForGrace, waitForMaximum);
  ASSERT_TRUE(listener);

  rv = pool->SetListener(listener);
  ASSERT_NS_SUCCEEDED(rv);

  nsCOMPtr<nsITimer> timer;
  nsCOMPtr<nsISerialEventTarget> helperTarget;
  rv = NS_CreateBackgroundTaskQueue("Helper", getter_AddRefs(helperTarget));
  ASSERT_NS_SUCCEEDED(rv);

  auto activateThreads = [&](uint32_t aNumThreads) MOZ_REQUIRES(waitMutex) {
    // Ramp up to have all 4 threads running.
    // The pool must be completely idle before calling this!
    MOZ_ASSERT(!numberOfActivationRunnables);
    printf("%u Activate %u threads.\n",
           (uint32_t)(TimeStamp::Now() - execStart).ToMilliseconds(),
           (uint32_t)aNumThreads);

    peakReached = false;

    // We dispatch a runnable per thread that will wait for us unless we are
    // the last to run.
    for (uint32_t i = 0; i < aNumThreads; i++) {
      nsCOMPtr<nsIRunnable> runnable =
          NS_NewRunnableFunction("TestRunnable", [&]() {
            MutexAutoLock lock(waitMutex);
            numberOfActivationRunnables++;
            if (numberOfActivationRunnables >= aNumThreads) {
              peakReached = true;
              waitForPeak.NotifyAll();
            } else {
              // Block this thread until we reach our max.
              while (!peakReached) {
                waitForPeak.Wait();
              }
            }
            numberOfActivationRunnables--;
            if (numberOfActivationRunnables == 0) {
              waitForIdleAfterPeak.NotifyAll();
            }
          });
      ASSERT_TRUE(runnable);

      rv = pool->Dispatch(runnable, NS_DISPATCH_NORMAL);
      ASSERT_NS_SUCCEEDED(rv);
    }
    // Ensure all runnables were executed. Should be immediate.
    while (!peakReached) {
      waitForPeak.Wait();
    }
    while (numberOfActivationRunnables) {
      waitForIdleAfterPeak.Wait();
    }
  };

  // 1st Test: Ramp up once to maximum threads and see how the threads are
  // timing out.
  {
    double deviationPerc = 0.0;
    TimeStamp start;
    TimeDuration graceTime;
    {
      ScopedTimingChecker<IDLE_THREAD_GRACE_TIMEOUT / 5, 5> checker(
          deviationPerc);
      {
        MutexAutoLock lock(waitMutex);
        activateThreads(NUMBER_OF_MAX_THREADS);
      }
      printf("%u Found %u threads alive.\n",
             (uint32_t)(TimeStamp::Now() - execStart).ToMilliseconds(),
             numberOfThreads.load());
      EXPECT_EQ(numberOfThreads, (uint32_t)NUMBER_OF_MAX_THREADS);

      start = TimeStamp::Now();

      // We expect the excess idle threads to terminate after
      // IDLE_THREAD_GRACE_TIMEOUT.
      printf("%u Wait for grace timeout...\n",
             (uint32_t)(TimeStamp::Now() - execStart).ToMilliseconds());
      {
        MutexAutoLock lock(waitMutex);
        while (numberOfThreads > NUMBER_OF_IDLE_THREADS) {
          waitForGrace.Wait();
        }
      }
      printf("%u Found %u threads alive.\n",
             (uint32_t)(TimeStamp::Now() - execStart).ToMilliseconds(),
             numberOfThreads.load());
      graceTime = TimeStamp::Now() - start;
    }
    EXPECT_EQ(numberOfThreads, (uint32_t)NUMBER_OF_IDLE_THREADS);
    if (deviationPerc < 10) {
      EXPECT_GE(graceTime,
                TimeDuration::FromMilliseconds(IDLE_THREAD_GRACE_TIMEOUT * .9));
      EXPECT_LE(graceTime, TimeDuration::FromMilliseconds(
                               IDLE_THREAD_GRACE_TIMEOUT * 1.5));
    } else {
      printf(
          "Encountered flaky timers (deviation=%.2f), skipping grace timeout "
          "check.\n",
          deviationPerc);
    }

    TimeDuration maxTime;
    {
      ScopedTimingChecker<
          (IDLE_THREAD_MAX_TIMEOUT - IDLE_THREAD_GRACE_TIMEOUT) / 5, 5>
          checker(deviationPerc);
      printf("%u Wait for maximum timeout...\n",
             (uint32_t)(TimeStamp::Now() - execStart).ToMilliseconds());
      {
        MutexAutoLock lock(waitMutex);
        while (numberOfThreads > 0) {
          waitForMaximum.Wait();
        }
      }
    }
    printf("%u Found %u threads alive.\n",
           (uint32_t)(TimeStamp::Now() - execStart).ToMilliseconds(),
           numberOfThreads.load());
    maxTime = TimeStamp::Now() - start;
    EXPECT_EQ(numberOfThreads, (uint32_t)0);
    if (deviationPerc < 10) {
      EXPECT_GE(maxTime,
                TimeDuration::FromMilliseconds(IDLE_THREAD_MAX_TIMEOUT * .9));
      EXPECT_LE(maxTime,
                TimeDuration::FromMilliseconds(IDLE_THREAD_MAX_TIMEOUT * 1.5));
    } else {
      printf(
          "Encountered flaky timers (deviation=%.2f), skipping max timeout "
          "check.\n",
          deviationPerc);
    }
  }

  // 2nd Test: Have several bursts that ramp up to maximum threads interleaved
  // with short pauses and see how many threads are created and/or shutdown.
  TimeStamp started = TimeStamp::Now();
  CondVar waitForRepeats(waitMutex, "WaitForRepeats");
  bool repeatsDone = false;
  {
    numberOfThreadsCreated = 0;

    // Cause a burst every 50ms for 500ms
    auto res = NS_NewTimerWithCallback(
        [&](nsITimer* t) {
          MutexAutoLock lock(waitMutex);
          activateThreads(NUMBER_OF_MAX_THREADS);
          if (TimeStamp::Now() - started >
              TimeDuration::FromMilliseconds(2.0 * IDLE_THREAD_GRACE_TIMEOUT)) {
            repeatsDone = true;
            waitForRepeats.Notify();
          }
        },
        50, nsITimer::TYPE_REPEATING_PRECISE_CAN_SKIP, "Background Bursts"_ns,
        helperTarget);

    ASSERT_TRUE(res.isOk());
    timer = res.unwrap();

    printf("%u Wait for repeated bursts...\n",
           (uint32_t)(TimeStamp::Now() - execStart).ToMilliseconds());
    {
      MutexAutoLock lock(waitMutex);
      while (!repeatsDone) {
        waitForRepeats.Wait();
      }
      timer->Cancel();
    }

    // We expect only 6 initial threads to be created and kept alive by the
    // grace timeout.
    printf("%u Found %u threads created.\n",
           (uint32_t)(TimeStamp::Now() - execStart).ToMilliseconds(),
           numberOfThreadsCreated.load());
    EXPECT_EQ(NUMBER_OF_MAX_THREADS, numberOfThreadsCreated.load());
  }

  // 3rd Test: After an initial burst, see how low noise of repeated single
  // events keeps alive threads.
  {
    // Reset the timeouts of all threads.
    {
      MutexAutoLock lock(waitMutex);
      activateThreads(NUMBER_OF_MAX_THREADS);
    }
    printf("%u Found %u threads alive.\n",
           (uint32_t)(TimeStamp::Now() - execStart).ToMilliseconds(),
           numberOfThreads.load());
    EXPECT_EQ(numberOfThreads, (uint32_t)NUMBER_OF_MAX_THREADS);

    double deviationPerc = 0.0;
    {
      ScopedTimingChecker<IDLE_THREAD_GRACE_TIMEOUT / 5, 5> checker(
          deviationPerc);

      // Create some low level noise that should need only 1 idle thread at
      // a time (1 runnable every 50ms for 625ms).
      numberOfActivationRunnables = 0;
      started = TimeStamp::Now();
      repeatsDone = false;
      CondVar waitForRepeatExecutions(waitMutex, "waitForRepeatExecutions");
      Atomic<uint32_t> numberOfNoiseRunnables(0);

      auto res = NS_NewTimerWithCallback(
          [&](nsITimer* t) {
            MutexAutoLock lock(waitMutex);
            if (TimeStamp::Now() - started >
                TimeDuration::FromMilliseconds(2.5 *
                                               IDLE_THREAD_GRACE_TIMEOUT)) {
              repeatsDone = true;
              waitForRepeats.Notify();
            } else {
              // Decouple the dispatch to the tested pool from the timer
              // execution. If there is still a runnable in flight for slow
              // execution, skip.
              if (numberOfNoiseRunnables == 0) {
                nsCOMPtr<nsIRunnable> runnable =
                    NS_NewRunnableFunction("EmptyRunnable", [&]() {
                      MutexAutoLock lock(waitMutex);
                      printf("%u Execute empty runnable, num %u.\n",
                             (uint32_t)(TimeStamp::Now() - execStart)
                                 .ToMilliseconds(),
                             numberOfNoiseRunnables.load());
                      numberOfNoiseRunnables--;
                      if (numberOfNoiseRunnables == 0) {
                        waitForRepeatExecutions.Notify();
                      }
                    });
                ASSERT_TRUE(runnable);

                numberOfNoiseRunnables++;
                printf(
                    "%u Dispatch empty runnable, num %u.\n",
                    (uint32_t)(TimeStamp::Now() - execStart).ToMilliseconds(),
                    numberOfNoiseRunnables.load());
                rv = pool->Dispatch(runnable, NS_DISPATCH_NORMAL);
                ASSERT_NS_SUCCEEDED(rv);
              }
            }
          },
          50, nsITimer::TYPE_REPEATING_PRECISE_CAN_SKIP, "Background Noise"_ns,
          helperTarget);

      ASSERT_TRUE(res.isOk());
      timer = res.unwrap();

      printf("%u Wait for repeated low noise...\n",
             (uint32_t)(TimeStamp::Now() - execStart).ToMilliseconds());
      {
        MutexAutoLock lock(waitMutex);
        while (!repeatsDone) {
          waitForRepeats.Wait();
        }
        timer->Cancel();
        while (numberOfNoiseRunnables) {
          printf("%u Runnables in flight after cancel: %u, wait...\n",
                 (uint32_t)(TimeStamp::Now() - execStart).ToMilliseconds(),
                 numberOfNoiseRunnables.load());
          waitForRepeatExecutions.Wait();
        }
      }
    }

    if (deviationPerc < 10) {
      // We would expect the idle threads to have gone back to
      // NUMBER_OF_IDLE_THREADS.
      // But due to the same MRU thread being used all the time we can
      // find NUMBER_OF_IDLE_THREADS + 1 if none of the other threads waiting
      // with IDLE_THREAD_MAX_TIMEOUT expired, yet.
      printf("%u End of low noise, found %u threads alive.\n",
             (uint32_t)(TimeStamp::Now() - execStart).ToMilliseconds(),
             numberOfThreads.load());
      EXPECT_LE(NUMBER_OF_IDLE_THREADS, numberOfThreads.load());
      EXPECT_GE(NUMBER_OF_IDLE_THREADS + 1, numberOfThreads.load());
    } else {
      printf(
          "Encountered flaky timers (deviation=%.2f), skipping low noise "
          "timeout check.\n",
          deviationPerc);
    }
  }

  // Teardown.
  rv = pool->Shutdown();
  ASSERT_NS_SUCCEEDED(rv);
}
}  // namespace TestThreadPoolIdleTimeout
