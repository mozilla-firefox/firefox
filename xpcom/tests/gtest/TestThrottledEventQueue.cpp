/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <functional>
#include <string>
#include <utility>

#include "MainThreadUtils.h"
#include "gtest/gtest.h"
#include "mozilla/Attributes.h"
#include "mozilla/CondVar.h"
#include "mozilla/gtest/MozAssertions.h"
#include "mozilla/Mutex.h"
#include "mozilla/RefPtr.h"
#include "mozilla/ThrottledEventQueue.h"
#include "nsCOMPtr.h"
#include "nsError.h"
#include "nsIRunnable.h"
#include "nsISerialEventTarget.h"
#include "nsIThread.h"
#include "nsThreadUtils.h"
#include "prinrval.h"

#include "Helpers.h"

using mozilla::CondVar;
using mozilla::MakeRefPtr;
using mozilla::Mutex;
using mozilla::MutexAutoLock;
using mozilla::ThrottledEventQueue;

namespace TestThrottledEventQueue {

static void Enqueue(nsIEventTarget* target, std::function<void()>&& aCallable) {
  nsresult rv = target->Dispatch(
      NS_NewRunnableFunction("TEQ GTest", std::move(aCallable)));
  MOZ_ALWAYS_TRUE(NS_SUCCEEDED(rv));
}

}  // namespace TestThrottledEventQueue

using testing::RunnableQueue;

TEST(ThrottledEventQueue, RunnableQueue)
{
  std::string log;

  RefPtr<RunnableQueue> queue = MakeRefPtr<RunnableQueue>();
  TestThrottledEventQueue::Enqueue(queue, [&]() { log += 'a'; });
  TestThrottledEventQueue::Enqueue(queue, [&]() { log += 'b'; });
  TestThrottledEventQueue::Enqueue(queue, [&]() { log += 'c'; });

  ASSERT_EQ(log, "");
  ASSERT_NS_SUCCEEDED(queue->Run());
  ASSERT_EQ(log, "abc");
}

TEST(ThrottledEventQueue, SimpleDispatch)
{
  std::string log;

  auto base = MakeRefPtr<RunnableQueue>();
  RefPtr<ThrottledEventQueue> throttled =
      ThrottledEventQueue::Create(base, "test queue 1");

  TestThrottledEventQueue::Enqueue(throttled, [&]() { log += 'a'; });
  ASSERT_NS_SUCCEEDED(base->Run());
  ASSERT_EQ(log, "a");

  ASSERT_TRUE(base->IsEmpty());
  ASSERT_TRUE(throttled->IsEmpty());
}

TEST(ThrottledEventQueue, MixedDispatch)
{
  std::string log;

  auto base = MakeRefPtr<RunnableQueue>();
  RefPtr<ThrottledEventQueue> throttled =
      ThrottledEventQueue::Create(base, "test queue 2");

  // A ThrottledEventQueue limits its impact on the base target by only queuing
  // its next event on the base once the prior event has been run. What it
  // actually queues on the base is a sort of proxy event called an
  // "executor": the base running the executor draws an event from the
  // ThrottledEventQueue and runs that. If the ThrottledEventQueue has further
  // events, it re-queues the executor on the base, effectively "going to the
  // back of the line".

  // Queue an event on the ThrottledEventQueue. This also queues the "executor"
  // event on the base.
  TestThrottledEventQueue::Enqueue(throttled, [&]() { log += 'a'; });
  ASSERT_EQ(throttled->Length(), 1U);
  ASSERT_EQ(base->Length(), 1U);

  // Add a second event to the throttled queue. The executor is already queued.
  TestThrottledEventQueue::Enqueue(throttled, [&]() { log += 'b'; });
  ASSERT_EQ(throttled->Length(), 2U);
  ASSERT_EQ(base->Length(), 1U);

  // Add an event directly to the base, after the executor.
  TestThrottledEventQueue::Enqueue(base, [&]() { log += 'c'; });
  ASSERT_EQ(throttled->Length(), 2U);
  ASSERT_EQ(base->Length(), 2U);

  // Run the base target. This runs:
  // - the executor, which runs the first event from the ThrottledEventQueue,
  //   and re-enqueues itself
  // - the event queued directly on the base
  // - the executor again, which runs the second event from the
  // ThrottledEventQueue.
  ASSERT_EQ(log, "");
  ASSERT_NS_SUCCEEDED(base->Run());
  ASSERT_EQ(log, "acb");

  ASSERT_TRUE(base->IsEmpty());
  ASSERT_TRUE(throttled->IsEmpty());
}

TEST(ThrottledEventQueue, EnqueueFromRun)
{
  std::string log;

  auto base = MakeRefPtr<RunnableQueue>();
  RefPtr<ThrottledEventQueue> throttled =
      ThrottledEventQueue::Create(base, "test queue 3");

  // When an event from the throttled queue dispatches a new event directly to
  // the base target, it is queued after the executor, so the next event from
  // the throttled queue will run before it.
  TestThrottledEventQueue::Enqueue(base, [&]() { log += 'a'; });
  TestThrottledEventQueue::Enqueue(throttled, [&]() {
    log += 'b';
    TestThrottledEventQueue::Enqueue(base, [&]() { log += 'c'; });
  });
  TestThrottledEventQueue::Enqueue(throttled, [&]() { log += 'd'; });

  ASSERT_EQ(log, "");
  ASSERT_NS_SUCCEEDED(base->Run());
  ASSERT_EQ(log, "abdc");

  ASSERT_TRUE(base->IsEmpty());
  ASSERT_TRUE(throttled->IsEmpty());
}

TEST(ThrottledEventQueue, RunFromRun)
{
  std::string log;

  auto base = MakeRefPtr<RunnableQueue>();
  RefPtr<ThrottledEventQueue> throttled =
      ThrottledEventQueue::Create(base, "test queue 4");

  // Running the event queue from within an event (i.e., a nested event loop)
  // does not stall the ThrottledEventQueue.
  TestThrottledEventQueue::Enqueue(throttled, [&]() {
    log += '(';
    // This should run subsequent events from throttled.
    ASSERT_NS_SUCCEEDED(base->Run());
    log += ')';
  });

  TestThrottledEventQueue::Enqueue(throttled, [&]() { log += 'a'; });

  ASSERT_EQ(log, "");
  ASSERT_NS_SUCCEEDED(base->Run());
  ASSERT_EQ(log, "(a)");

  ASSERT_TRUE(base->IsEmpty());
  ASSERT_TRUE(throttled->IsEmpty());
}

TEST(ThrottledEventQueue, DropWhileRunning)
{
  std::string log;

  auto base = MakeRefPtr<RunnableQueue>();

  // If we drop the event queue while it still has events, they still run.
  {
    RefPtr<ThrottledEventQueue> throttled =
        ThrottledEventQueue::Create(base, "test queue 5");
    TestThrottledEventQueue::Enqueue(throttled, [&]() { log += 'a'; });
  }

  ASSERT_EQ(log, "");
  ASSERT_NS_SUCCEEDED(base->Run());
  ASSERT_EQ(log, "a");
}

TEST(ThrottledEventQueue, AwaitIdle)
{
  Mutex mutex MOZ_UNANNOTATED("TEQ AwaitIdle");
  CondVar cond(mutex, "TEQ AwaitIdle");

  std::string dequeue_await;      // mutex
  bool threadFinished = false;    // mutex & cond
  bool runnableFinished = false;  // main thread only

  auto base = MakeRefPtr<RunnableQueue>();
  RefPtr<ThrottledEventQueue> throttled =
      ThrottledEventQueue::Create(base, "test queue 6");

  // Put an event in the queue so the AwaitIdle might block.
  TestThrottledEventQueue::Enqueue(throttled,
                                   [&]() { runnableFinished = true; });

  // Create a separate thread that waits for the queue to become idle, and
  // then takes observable action.
  nsCOMPtr<nsIRunnable> await = NS_NewRunnableFunction("TEQ AwaitIdle", [&]() {
    throttled->AwaitIdle();
    MutexAutoLock lock(mutex);
    dequeue_await += " await";
    threadFinished = true;
    cond.Notify();
  });

  nsCOMPtr<nsIThread> thread;
  nsresult rv =
      NS_NewNamedThread("TEQ AwaitIdle", getter_AddRefs(thread), await);
  ASSERT_NS_SUCCEEDED(rv);

  // We can't guarantee that the thread has reached the AwaitIdle call, but we
  // can get pretty close. Either way, it shouldn't affect the behavior of the
  // test.
  PR_Sleep(PR_MillisecondsToInterval(100));

  // Drain the queue.
  {
    MutexAutoLock lock(mutex);
    ASSERT_EQ(dequeue_await, "");
    dequeue_await += "dequeue";
    ASSERT_FALSE(threadFinished);
  }
  ASSERT_FALSE(runnableFinished);
  ASSERT_NS_SUCCEEDED(base->Run());
  ASSERT_TRUE(runnableFinished);

  // Wait for the thread to finish.
  {
    MutexAutoLock lock(mutex);
    while (!threadFinished) cond.Wait();
    ASSERT_EQ(dequeue_await, "dequeue await");
  }

  ASSERT_NS_SUCCEEDED(thread->Shutdown());
}

TEST(ThrottledEventQueue, AwaitIdleMixed)
{
  // Create a separate thread that waits for the queue to become idle, and
  // then takes observable action.
  nsCOMPtr<nsIThread> thread;
  ASSERT_TRUE(NS_SUCCEEDED(
      NS_NewNamedThread("AwaitIdleMixed", getter_AddRefs(thread))));

  Mutex mutex MOZ_UNANNOTATED("AwaitIdleMixed");
  CondVar cond(mutex, "AwaitIdleMixed");

  // The following are protected by mutex and cond, above.
  std::string log;
  bool threadStarted = false;
  bool threadFinished = false;

  auto base = MakeRefPtr<RunnableQueue>();
  RefPtr<ThrottledEventQueue> throttled =
      ThrottledEventQueue::Create(base, "test queue 7");

  TestThrottledEventQueue::Enqueue(throttled, [&]() {
    MutexAutoLock lock(mutex);
    log += 'a';
  });

  TestThrottledEventQueue::Enqueue(throttled, [&]() {
    MutexAutoLock lock(mutex);
    log += 'b';
  });

  nsCOMPtr<nsIRunnable> await = NS_NewRunnableFunction("AwaitIdleMixed", [&]() {
    {
      MutexAutoLock lock(mutex);

      // Note that we are about to begin awaiting. When the main thread sees
      // this notification, it will begin draining the queue.
      log += '(';
      threadStarted = true;
      cond.Notify();
    }

    // Wait for the main thread to drain the TEQ.
    throttled->AwaitIdle();

    {
      MutexAutoLock lock(mutex);

      // Note that we have finished awaiting.
      log += ')';
      threadFinished = true;
      cond.Notify();
    }
  });

  {
    MutexAutoLock lock(mutex);
    ASSERT_EQ(log, "");
  }

  ASSERT_NS_SUCCEEDED(thread->Dispatch(await.forget()));

  // Wait for the thread to be ready to await. We can't be sure it will actually
  // be blocking before we get around to draining the event queue, but that's
  // the nature of the API; this test should work even if we drain the queue
  // before it awaits.
  {
    MutexAutoLock lock(mutex);
    while (!threadStarted) cond.Wait();
    ASSERT_EQ(log, "(");
  }

  // Let the queue drain.
  ASSERT_NS_SUCCEEDED(base->Run());

  {
    MutexAutoLock lock(mutex);
    // The first runnable must always finish before AwaitIdle returns. But the
    // TEQ notifies the condition variable as soon as it dequeues the last
    // runnable, without waiting for that runnable to complete. So the thread
    // and the last runnable could run in either order. Or, we might beat the
    // thread to the mutex.
    //
    // (The only combination excluded here is "(a)": the 'b' runnable should
    // definitely have run.)
    ASSERT_TRUE(log == "(ab" || log == "(a)b" || log == "(ab)");
    while (!threadFinished) cond.Wait();
    ASSERT_TRUE(log == "(a)b" || log == "(ab)");
  }

  ASSERT_NS_SUCCEEDED(thread->Shutdown());
}

TEST(ThrottledEventQueue, SimplePauseResume)
{
  std::string log;

  auto base = MakeRefPtr<RunnableQueue>();
  RefPtr<ThrottledEventQueue> throttled =
      ThrottledEventQueue::Create(base, "test queue 8");

  ASSERT_FALSE(throttled->IsPaused());

  TestThrottledEventQueue::Enqueue(throttled, [&]() { log += 'a'; });

  ASSERT_EQ(log, "");
  ASSERT_NS_SUCCEEDED(base->Run());
  ASSERT_EQ(log, "a");

  ASSERT_NS_SUCCEEDED(throttled->SetIsPaused(true));
  ASSERT_TRUE(throttled->IsPaused());

  TestThrottledEventQueue::Enqueue(throttled, [&]() { log += 'b'; });

  ASSERT_EQ(log, "a");
  ASSERT_NS_SUCCEEDED(base->Run());
  ASSERT_EQ(log, "a");

  ASSERT_NS_SUCCEEDED(throttled->SetIsPaused(false));
  ASSERT_FALSE(throttled->IsPaused());

  ASSERT_EQ(log, "a");
  ASSERT_NS_SUCCEEDED(base->Run());
  ASSERT_EQ(log, "ab");

  ASSERT_TRUE(base->IsEmpty());
  ASSERT_TRUE(throttled->IsEmpty());
}

TEST(ThrottledEventQueue, MixedPauseResume)
{
  std::string log;

  auto base = MakeRefPtr<RunnableQueue>();
  RefPtr<ThrottledEventQueue> throttled =
      ThrottledEventQueue::Create(base, "test queue 9");

  ASSERT_FALSE(throttled->IsPaused());

  TestThrottledEventQueue::Enqueue(base, [&]() { log += 'A'; });
  TestThrottledEventQueue::Enqueue(throttled, [&]() {
    log += 'b';
    MOZ_ALWAYS_TRUE(NS_SUCCEEDED(throttled->SetIsPaused(true)));
  });
  TestThrottledEventQueue::Enqueue(throttled, [&]() { log += 'c'; });
  TestThrottledEventQueue::Enqueue(base, [&]() { log += 'D'; });

  ASSERT_EQ(log, "");
  ASSERT_NS_SUCCEEDED(base->Run());
  // Since the 'b' event paused the throttled queue, 'c' should not have run.
  // but 'D' was enqueued directly on the base, and should have run.
  ASSERT_EQ(log, "AbD");
  ASSERT_TRUE(base->IsEmpty());
  ASSERT_FALSE(throttled->IsEmpty());
  ASSERT_TRUE(throttled->IsPaused());

  TestThrottledEventQueue::Enqueue(base, [&]() { log += 'E'; });
  ASSERT_NS_SUCCEEDED(throttled->SetIsPaused(false));
  TestThrottledEventQueue::Enqueue(base, [&]() { log += 'F'; });
  ASSERT_FALSE(throttled->IsPaused());

  ASSERT_NS_SUCCEEDED(base->Run());
  // Since we've unpaused, 'c' should be able to run now. The executor should
  // have been enqueued between 'E' and 'F'.
  ASSERT_EQ(log, "AbDEcF");

  ASSERT_TRUE(base->IsEmpty());
  ASSERT_TRUE(throttled->IsEmpty());
}

TEST(ThrottledEventQueue, AwaitIdlePaused)
{
  Mutex mutex MOZ_UNANNOTATED("AwaitIdlePaused");
  CondVar cond(mutex, "AwaitIdlePaused");

  std::string dequeue_await;      // mutex
  bool threadFinished = false;    // mutex & cond
  bool runnableFinished = false;  // main thread only

  auto base = MakeRefPtr<RunnableQueue>();
  RefPtr<ThrottledEventQueue> throttled =
      ThrottledEventQueue::Create(base, "test queue 10");

  ASSERT_NS_SUCCEEDED(throttled->SetIsPaused(true));

  // Put an event in the queue so the AwaitIdle might block. Since throttled is
  // paused, this should not enqueue an executor in the base target.
  TestThrottledEventQueue::Enqueue(throttled,
                                   [&]() { runnableFinished = true; });
  ASSERT_TRUE(base->IsEmpty());

  // Create a separate thread that waits for the queue to become idle, and
  // then takes observable action.
  nsCOMPtr<nsIRunnable> await =
      NS_NewRunnableFunction("AwaitIdlePaused", [&]() {
        throttled->AwaitIdle();
        MutexAutoLock lock(mutex);
        dequeue_await += " await";
        threadFinished = true;
        cond.Notify();
      });

  nsCOMPtr<nsIThread> thread;
  nsresult rv =
      NS_NewNamedThread("AwaitIdlePaused", getter_AddRefs(thread), await);
  ASSERT_NS_SUCCEEDED(rv);

  // We can't guarantee that the thread has reached the AwaitIdle call, but we
  // can get pretty close. Either way, it shouldn't affect the behavior of the
  // test.
  PR_Sleep(PR_MillisecondsToInterval(100));

  // The AwaitIdle call should be blocked, even though there is no executor,
  // because throttled is paused.
  {
    MutexAutoLock lock(mutex);
    ASSERT_EQ(dequeue_await, "");
    dequeue_await += "dequeue";
    ASSERT_FALSE(threadFinished);
  }

  // A paused TEQ contributes no events to its base target. (This is covered by
  // other tests...)
  ASSERT_NS_SUCCEEDED(base->Run());
  ASSERT_TRUE(base->IsEmpty());
  ASSERT_FALSE(throttled->IsEmpty());

  // Resume and drain the queue.
  ASSERT_FALSE(runnableFinished);
  ASSERT_NS_SUCCEEDED(throttled->SetIsPaused(false));
  ASSERT_NS_SUCCEEDED(base->Run());
  ASSERT_TRUE(base->IsEmpty());
  ASSERT_TRUE(throttled->IsEmpty());
  ASSERT_TRUE(runnableFinished);

  // Wait for the thread to finish.
  {
    MutexAutoLock lock(mutex);
    while (!threadFinished) cond.Wait();
    ASSERT_EQ(dequeue_await, "dequeue await");
  }

  ASSERT_NS_SUCCEEDED(thread->Shutdown());
}

TEST(ThrottledEventQueue, ExecutorTransitions)
{
  std::string log;

  auto base = MakeRefPtr<RunnableQueue>();
  RefPtr<ThrottledEventQueue> throttled =
      ThrottledEventQueue::Create(base, "test queue 11");

  ASSERT_NS_SUCCEEDED(throttled->SetIsPaused(true));

  // Since we're paused, queueing an event on throttled shouldn't queue the
  // executor on the base target.
  TestThrottledEventQueue::Enqueue(throttled, [&]() { log += 'a'; });
  ASSERT_EQ(throttled->Length(), 1U);
  ASSERT_EQ(base->Length(), 0U);

  // Resuming throttled should create the executor, since throttled is not
  // empty.
  ASSERT_NS_SUCCEEDED(throttled->SetIsPaused(false));
  ASSERT_EQ(throttled->Length(), 1U);
  ASSERT_EQ(base->Length(), 1U);

  // Pausing can't remove the executor from the base target since we've already
  // queued it there, but it can ensure that it doesn't do anything.
  ASSERT_NS_SUCCEEDED(throttled->SetIsPaused(true));

  ASSERT_EQ(log, "");
  ASSERT_NS_SUCCEEDED(base->Run());
  ASSERT_EQ(log, "");
  ASSERT_EQ(throttled->Length(), 1U);
  ASSERT_EQ(base->Length(), 0U);

  // As before, resuming must create the executor, since throttled is not empty.
  ASSERT_NS_SUCCEEDED(throttled->SetIsPaused(false));
  ASSERT_EQ(throttled->Length(), 1U);
  ASSERT_EQ(base->Length(), 1U);

  ASSERT_EQ(log, "");
  ASSERT_NS_SUCCEEDED(base->Run());
  ASSERT_EQ(log, "a");
  ASSERT_EQ(throttled->Length(), 0U);
  ASSERT_EQ(base->Length(), 0U);

  // Since throttled is empty, pausing and resuming now should not enqueue an
  // executor.
  ASSERT_NS_SUCCEEDED(throttled->SetIsPaused(true));
  ASSERT_NS_SUCCEEDED(throttled->SetIsPaused(false));
  ASSERT_EQ(throttled->Length(), 0U);
  ASSERT_EQ(base->Length(), 0U);
}
