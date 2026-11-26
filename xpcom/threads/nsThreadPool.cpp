/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsThreadPool.h"

#include "ThreadDelay.h"
#include "mozilla/Logging.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ProfilerRunnable.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/StickyTimeDuration.h"
#include "nsCOMArray.h"
#include "nsIEventTarget.h"
#include "nsIRunnable.h"
#include "nsThread.h"
#include "nsThreadManager.h"
#include "nsThreadSyncDispatch.h"
#include "nsThreadUtils.h"
#include "prinrval.h"

using namespace mozilla;

static LazyLogModule sThreadPoolLog("nsThreadPool");
#ifdef LOG
#  undef LOG
#endif
#define LOG(args) MOZ_LOG(sThreadPoolLog, mozilla::LogLevel::Debug, args)

static MOZ_THREAD_LOCAL(nsThreadPool*) gCurrentThreadPool;

void nsThreadPool::InitTLS() { gCurrentThreadPool.infallibleInit(); }

// DESIGN:
//  o  Allocate anonymous threads.
//  o  Use nsThreadPool::Run as the main routine for each thread.
//  o  Each thread waits on the event queue's monitor, checking for
//     pending events and rescheduling itself as an idle thread.

#define DEFAULT_THREAD_LIMIT 4
#define DEFAULT_IDLE_THREAD_LIMIT 1
#define DEFAULT_IDLE_THREAD_GRACE_TIMEOUT_MS 100
#define DEFAULT_IDLE_THREAD_MAX_TIMEOUT_MS 60000

NS_IMPL_ISUPPORTS_INHERITED(nsThreadPool, Runnable, nsIThreadPool,
                            nsIEventTarget)

nsThreadPool* nsThreadPool::GetCurrentThreadPool() {
  return gCurrentThreadPool.get();
}

nsThreadPool::nsThreadPool()
    : Runnable("nsThreadPool"),
      mMutex("[nsThreadPool.mMutex]"),
      mThreadLimit(DEFAULT_THREAD_LIMIT),
      mIdleThreadLimit(DEFAULT_IDLE_THREAD_LIMIT),
      mIdleThreadGraceTimeout(
          TimeDuration::FromMilliseconds(DEFAULT_IDLE_THREAD_GRACE_TIMEOUT_MS)),
      mIdleThreadMaxTimeout(
          TimeDuration::FromMilliseconds(DEFAULT_IDLE_THREAD_MAX_TIMEOUT_MS)),
      mQoSPriority(nsIThread::QOS_PRIORITY_NORMAL),
      mStackSize(nsIThreadManager::DEFAULT_STACK_SIZE),
      mShutdown(false),
      mIsAPoolThreadFree(true) {
  LOG(("THRD-P(%p) constructor!!!\n", this));
}

nsThreadPool::~nsThreadPool() {
  // Threads keep a reference to the nsThreadPool until they return from Run()
  // after removing themselves from mThreads.
  MOZ_ASSERT(mThreads.IsEmpty());
}

// Each thread has its own MRUIdleEntry instance. If it is element of the
// mMRUIdleThreads list, it can be notified for event processing.
struct nsThreadPool::MRUIdleEntry
    : public mozilla::LinkedListElement<MRUIdleEntry> {
  // Created from thread (as local variable).
  explicit MRUIdleEntry(mozilla::Mutex& aMutex)
      : mEventsAvailable(aMutex,
                         "[nsThreadPool.MRUIdleStatus.mEventsAvailable]") {}

  // Keep track of the moment the thread finished its last event.
  mozilla::TimeStamp mIdleSince;
  // Each thread has its own cond var.
  mozilla::CondVar mEventsAvailable;
#ifdef DEBUG
  // If we were notified for work, keeps track when.
  mozilla::TimeStamp mNotifiedSince;
  // If we are going to sleep, keeps track for how long.
  mozilla::TimeDuration mLastWaitDelay;
#endif
};

#ifdef DEBUG
// This logging relies on extra members we do not want to bake into release.
void nsThreadPool::DebugLogPoolStatus(MutexAutoLock& aProofOfLock,
                                      MRUIdleEntry* aWakingEntry) {
  if (!MOZ_LOG_TEST(sThreadPoolLog, mozilla::LogLevel::Debug)) {
    return;
  }

  LOG(
      ("THRD-P(%p) \"%s\" (entry %p) status ---- mThreads(%u), mEvents(%u), "
       "mThreadLimit(%u), mIdleThreadLimit(%u), mIdleCount(%zd), "
       "mMRUIdleThreads(%u), mShutdown(%u)\n",
       this, mName.get(), aWakingEntry, mThreads.Length(),
       (uint32_t)mEvents.Count(aProofOfLock), mThreadLimit, mIdleThreadLimit,
       mMRUIdleThreads.length(), (uint32_t)mMRUIdleThreads.length(),
       (uint32_t)mShutdown));

  auto logEntry = [&](MRUIdleEntry* entry, const char* msg) {
    LOG(
        (" - (entry %p) %s, IdleSince(%d), "
         "NotifiedSince(%d) LastWaitDelay(%d)\n",
         entry, msg,
         (int)((entry->mIdleSince.IsNull())
                   ? -1
                   : (TimeStamp::Now() - entry->mIdleSince).ToMilliseconds()),
         (int)((entry->mNotifiedSince.IsNull())
                   ? -1
                   : (TimeStamp::Now() - entry->mNotifiedSince)
                         .ToMilliseconds()),
         (int)entry->mLastWaitDelay.ToMilliseconds()));
  };

  if (aWakingEntry) {
    logEntry(aWakingEntry, "woke up");
  }
  for (auto* idle : mMRUIdleThreads) {
    logEntry(idle, "in idle list");
  }
}
#endif

nsresult nsThreadPool::PutEvent(already_AddRefed<nsIRunnable> aEvent,
                                DispatchFlags aFlags,
                                MutexAutoLock& aProofOfLock) {
  // NOTE: To maintain existing behaviour, we never leak aEvent on error, even
  // if NS_DISPATCH_FALLIBLE is not specified.
  nsCOMPtr<nsIRunnable> event(aEvent);

  // We allow dispatching events during ThreadPool shutdown until all threads
  // have exited, although new threads will not be started.
  if (NS_WARN_IF(mShutdown && mThreads.IsEmpty())) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  LogRunnable::LogDispatch(event);
  mEvents.PutEvent(event.forget(), EventQueuePriority::Normal, aProofOfLock);

#ifdef DEBUG
  DebugLogPoolStatus(aProofOfLock, nullptr);
#endif

  // We've added the event to the queue, make sure a thread
  // will wake up to handle it.

  if (aFlags & NS_DISPATCH_AT_END) {
    // If NS_DISPATCH_AT_END is set, this thread is about to
    // become free to process the event, so we don't need to
    // signal another thread.
    MOZ_ASSERT(IsOnCurrentThreadInfallible(),
               "NS_DISPATCH_AT_END can only be set when "
               "dispatching from on the thread pool.");
    LOG(("THRD-P(%p) put [%zd %d %d]: NS_DISPATCH_AT_END w/out Notify.\n", this,
         mMRUIdleThreads.length(), mThreads.Count(), mThreadLimit));
    return NS_OK;
  }

  if (auto* mruThread = mMRUIdleThreads.getFirst()) {
    // If we have an idle thread, wake it up and remove it
    // from the idle list, so that future dispatches try
    // to wake other threads.
    mruThread->remove();
    mruThread->mEventsAvailable.Notify();
#ifdef DEBUG
    mruThread->mNotifiedSince = TimeStamp::Now();
#endif
    LOG(("THRD-P(%p) put [%zd %d %d]: Notify idle thread via entry(%p).\n",
         this, mMRUIdleThreads.length(), mThreads.Count(), mThreadLimit,
         mruThread));
    return NS_OK;
  }

  if (mThreads.Count() >= (int32_t)mThreadLimit || mShutdown) {
    // If we have no thread available, just leave the event in the queue
    // ready for the next thread about to become idle and pick it up.
    MOZ_ASSERT(!mThreads.IsEmpty(),
               "There must be a thread which will handle this dispatch");
    LOG(("THRD-P(%p) put [%zd %d %d]: No idle or new thread available.\n", this,
         mMRUIdleThreads.length(), mThreads.Count(), mThreadLimit));
    return NS_OK;
  }

  // HISTORIC NOTE: Previously we would unlock mMutex before starting a new
  // thread here. Prior to bug 1510226, NS_NewNamedThread would block the
  // calling thread waiting for the newly started thread to check in, meaning
  // this operation could be quite slow. As NS_NewNamedThread no longer blocks,
  // we no longer bother unlocking here, which greatly simplifies logic around
  // nsThreadPool thread lifetimes.
  nsCOMPtr<nsIThread> thread;
  nsresult rv = NS_NewNamedThread(
      mThreadNaming.GetNextThreadName(mName), getter_AddRefs(thread), this,
      {.stackSize = mStackSize, .blockDispatch = true});
  if (NS_WARN_IF(NS_FAILED(rv))) {
    if (mThreads.IsEmpty()) {
      MOZ_CRASH(
          "nsThreadPool::PutEvent() - Failed to create a new thread when pool "
          "is empty");
    }
    return NS_OK;
  }

  mThreads.AppendObject(thread);
  if (mThreads.Count() >= (int32_t)mThreadLimit) {
    MOZ_ASSERT(mMRUIdleThreads.isEmpty());
    mIsAPoolThreadFree = false;
  }

  LOG(("THRD-P(%p) put [%zd %d %d]: Spawn a new thread.\n", this,
       mMRUIdleThreads.length(), mThreads.Count(), mThreadLimit));

  return NS_OK;
}

void nsThreadPool::ShutdownThread(nsIThread* aThread) {
  LOG(("THRD-P(%p) shutdown async [%p]\n", this, aThread));

  // This is called by a threadpool thread that is out of work and exceeded
  // its idle timeout. The thread has already been unregistered from the pool's
  // bookkeeping and will go idle right after this call. We must go to another
  // thread to shut it down completely (because it is the current thread).
  // The simplest way to cover that case is to asynchronously shutdown aThread
  // from the main thread.
  // NOTE: If this fails, it's OK, as XPCOM shutdown will already have destroyed
  // the nsThread for us.
  SchedulerGroup::Dispatch(
      NewRunnableMethod("nsIThread::AsyncShutdown", aThread,
                        &nsIThread::AsyncShutdown),
      NS_DISPATCH_FALLIBLE);
}

NS_IMETHODIMP
nsThreadPool::SetQoSForThreads(nsIThread::QoSPriority aPriority) {
  MutexAutoLock lock(mMutex);
  mQoSPriority = aPriority;

  // We don't notify threads here to observe the change, because we don't want
  // to create spurious wakeups during idle. Rather, we want threads to simply
  // observe the change on their own if they wake up to do some task.

  return NS_OK;
}

void nsThreadPool::NotifyChangeToAllIdleThreads() {
  for (auto* idleThread : mMRUIdleThreads) {
    idleThread->mEventsAvailable.Notify();
  }
}

// This event 'runs' for the lifetime of the worker thread.  The actual
// eventqueue is mEvents, and is shared by all the worker threads.  This
// means that the set of threads together define the delay seen by a new
// event sent to the pool.
//
// To model the delay experienced by the pool, we can have each thread in
// the pool report 0 if it's idle OR if the pool is below the threadlimit;
// or otherwise the current event's queuing delay plus current running
// time.
//
// To reconstruct the delays for the pool, the profiler can look at all the
// threads that are part of a pool (pools have defined naming patterns that
// can be user to connect them).  If all threads have delays at time X,
// that means that all threads saturated at that point and any event
// dispatched to the pool would get a delay.
//
// The delay experienced by an event dispatched when all pool threads are
// busy is based on the calculations shown in platform.cpp.  Run that
// algorithm for each thread in the pool, and the delay at time X is the
// longest value for time X of any of the threads, OR the time from X until
// any one of the threads reports 0 (i.e. it's not busy), whichever is
// shorter.

// In order to record this when the profiler samples threads in the pool,
// each thread must (effectively) override GetRunnningEventDelay, by
// resetting the mLastEventDelay/Start values in the nsThread when we start
// to run an event (or when we run out of events to run).  Note that handling
// the shutdown of a thread may be a little tricky.

NS_IMETHODIMP
nsThreadPool::Run() {
  nsCOMPtr<nsIThread> current;
  nsThreadManager::get().GetCurrentThread(getter_AddRefs(current));

  bool shutdownThreadOnExit = false;
  bool exitThread = false;
  MRUIdleEntry idleEntry(mMutex);
  bool wasIdle = false;
  nsIThread::QoSPriority threadPriority = nsIThread::QOS_PRIORITY_NORMAL;

  // This thread is an nsThread created below with NS_NewNamedThread()
  static_cast<nsThread*>(current.get())
      ->SetPoolThreadFreePtr(&mIsAPoolThreadFree);

  nsCOMPtr<nsIThreadPoolListener> listener;
  {
    MutexAutoLock lock(mMutex);
    listener = mListener;
    LOG(("THRD-P(%p) enter %s\n", this, mName.get()));

    // Go ahead and check for thread priority. If priority is normal, do nothing
    // because threads are created with default priority.
    if (threadPriority != mQoSPriority) {
      current->SetThreadQoS(threadPriority);
      threadPriority = mQoSPriority;
    }
  }

  if (listener) {
    listener->OnThreadCreated();
  }

  MOZ_ASSERT(!gCurrentThreadPool.get());
  gCurrentThreadPool.set(this);

  do {
    nsCOMPtr<nsIRunnable> event;
    TimeDuration lastEventDelay;
    {
      MutexAutoLock lock(mMutex);

#ifdef DEBUG
      DebugLogPoolStatus(lock, &idleEntry);
      idleEntry.mNotifiedSince = TimeStamp();
#endif

      // Before getting the next event, we can adjust priority as needed.
      if (threadPriority != mQoSPriority) {
        current->SetThreadQoS(threadPriority);
        threadPriority = mQoSPriority;
      }

      event = mEvents.GetEvent(lock, &lastEventDelay);
      if (!event) {
        TimeStamp now = TimeStamp::Now();
        uint32_t cnt = mMRUIdleThreads.length() + ((wasIdle) ? 0 : 1);
        TimeDuration currentTimeout = (cnt > mIdleThreadLimit)
                                          ? mIdleThreadGraceTimeout
                                          : mIdleThreadMaxTimeout;

        if (mShutdown) {
          exitThread = true;
        } else {
          if (!wasIdle) {
            // Going idle for a new idle period.
            MOZ_ASSERT(!idleEntry.isInList());
            idleEntry.mIdleSince = now;
            wasIdle = true;
            mMRUIdleThreads.insertFront(&idleEntry);
          } else if ((now - idleEntry.mIdleSince) < currentTimeout) {
            // Continue to stay idle without touching mIdleSince.
            if (!idleEntry.isInList()) {
              mMRUIdleThreads.insertFront(&idleEntry);
            }
          } else {
            // We reached our timeout.
            exitThread = true;
          }
        }

        if (exitThread) {
          wasIdle = false;
          if (idleEntry.isInList()) {
            idleEntry.remove();
          }

          // If we're not currently in pool shutdown, we need to dispatch a
          // task to shut down the thread ourselves.
          shutdownThreadOnExit = !mShutdown;

          // Remove the thread from our threads list.
          // We may fail to find it only if the thread pool shutdown timed out.
          DebugOnly<bool> found = mThreads.RemoveObject(current);
          MOZ_ASSERT(found || (mShutdown && mThreads.IsEmpty()));

          // Keep track if there are threads available. If we are shutting
          // down, no new threads can start.
          mIsAPoolThreadFree =
              !mMRUIdleThreads.isEmpty() ||
              (!mShutdown && mThreads.Count() < (int32_t)mThreadLimit);
        } else {
          current->SetRunningEventDelay(TimeDuration(), TimeStamp());

          AUTO_PROFILER_LABEL("nsThreadPool::Run::Wait", IDLE);

          // Depending on the allowed number of idle threads, wait for events
          // at most our grace or max time minus the time we were already idle.
          // Use StickyTimeDuration when performing math to preserve a timeout
          // of TimeDuration::Forever.
          TimeDuration delta{StickyTimeDuration{currentTimeout} -
                             (now - idleEntry.mIdleSince)};
          delta = TimeDuration::Max(delta, TimeDuration::FromMilliseconds(1));
          LOG(("THRD-P(%p) %s waiting [%f]\n", this, mName.get(),
               delta.ToMilliseconds()));
#ifdef DEBUG
          idleEntry.mLastWaitDelay = delta;
#endif
          idleEntry.mEventsAvailable.Wait(delta);
          LOG(("THRD-P(%p) done waiting\n", this));
        }
      } else {
        // We have an event to work on.
        wasIdle = false;
        if (idleEntry.isInList()) {
          idleEntry.remove();
        }
      }
      // Release our lock.
    }

    if (event) {
      if (MOZ_LOG_TEST(sThreadPoolLog, mozilla::LogLevel::Debug)) {
        MutexAutoLock lock(mMutex);
        LOG(("THRD-P(%p) %s running [%p]\n", this, mName.get(), event.get()));
      }

      // Random delay, so the dispatching and running threads race in varying
      // order.
      DelayForChaosMode(ChaosFeature::TaskRunning);

      if (profiler_thread_is_being_profiled(
              ThreadProfilingFeatures::Sampling)) {
        // We'll handle the case of unstarted threads available
        // when we sample.
        current->SetRunningEventDelay(lastEventDelay, TimeStamp::Now());
      }

      LogRunnable::Run log(event);
      AUTO_PROFILE_FOLLOWING_RUNNABLE(event);
      event->Run();
      // To cover the event's destructor code in the LogRunnable span
      event = nullptr;
    }
  } while (!exitThread);

  if (listener) {
    listener->OnThreadShuttingDown();
  }

  MOZ_ASSERT(gCurrentThreadPool.get() == this);
  gCurrentThreadPool.set(nullptr);

  // Clear the thread's back-pointer into this pool so that the profiler's
  // SamplerThread stops using it for this thread.
  static_cast<nsThread*>(current.get())->SetPoolThreadFreePtr(nullptr);

  if (shutdownThreadOnExit) {
    ShutdownThread(current);
  }

  LOG(("THRD-P(%p) leave\n", this));
  return NS_OK;
}

NS_IMETHODIMP
nsThreadPool::DispatchFromScript(nsIRunnable* aEvent, DispatchFlags aFlags) {
  return Dispatch(do_AddRef(aEvent), aFlags);
}

NS_IMETHODIMP
nsThreadPool::Dispatch(already_AddRefed<nsIRunnable> aEvent,
                       DispatchFlags aFlags) {
  nsresult rv = NS_OK;
  {
    MutexAutoLock lock(mMutex);
    rv = PutEvent(std::move(aEvent), aFlags, lock);
  }

  // Random delay, so the dispatching and running threads race in varying
  // order.
  DelayForChaosMode(ChaosFeature::TaskDispatching);

  return rv;
}

NS_IMETHODIMP
nsThreadPool::DelayedDispatch(already_AddRefed<nsIRunnable>, uint32_t) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsThreadPool::RegisterShutdownTask(nsITargetShutdownTask* aTask) {
  NS_ENSURE_ARG(aTask);
  MutexAutoLock lock(mMutex);
  if (mShutdown) {
    return NS_ERROR_UNEXPECTED;
  }
  return mShutdownTasks.AddTask(aTask);
}

NS_IMETHODIMP
nsThreadPool::UnregisterShutdownTask(nsITargetShutdownTask* aTask) {
  NS_ENSURE_ARG(aTask);
  MutexAutoLock lock(mMutex);
  if (mShutdown) {
    return NS_ERROR_UNEXPECTED;
  }
  return mShutdownTasks.RemoveTask(aTask);
}

nsIEventTarget::FeatureFlags nsThreadPool::GetFeatures() {
  return SUPPORTS_SHUTDOWN_TASKS | SUPPORTS_SHUTDOWN_TASK_DISPATCH;
}

NS_IMETHODIMP_(bool)
nsThreadPool::IsOnCurrentThreadInfallible() {
  return gCurrentThreadPool.get() == this;
}

NS_IMETHODIMP
nsThreadPool::IsOnCurrentThread(bool* aResult) {
  MutexAutoLock lock(mMutex);
  if (NS_WARN_IF(mShutdown && mThreads.IsEmpty())) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  *aResult = IsOnCurrentThreadInfallible();
  return NS_OK;
}

NS_IMETHODIMP
nsThreadPool::Shutdown() { return ShutdownWithTimeout(-1); }

NS_IMETHODIMP
nsThreadPool::ShutdownWithTimeout(int32_t aTimeoutMs) {
  nsCOMArray<nsIThread> threads;
  nsCString name;
  {
    MutexAutoLock lock(mMutex);
    if (mShutdown) {
      return NS_ERROR_ILLEGAL_DURING_SHUTDOWN;
    }

    // If we have any shutdown tasks, queue a task to ensure they're triggered
    // before we block new thread creation.
    if (!mShutdownTasks.IsEmpty()) {
      PutEvent(
          NS_NewRunnableFunction("nsThreadPool ShutdownTasks",
                                 [tasks = mShutdownTasks.Extract()] {
                                   for (nsITargetShutdownTask* task : tasks) {
                                     task->TargetShutdown();
                                   }
                                 }),
          NS_DISPATCH_NORMAL, lock);
    }

    // NOTE: We do this after adding ShutdownTasks, as no new threads can be
    // started after `mShutdown` is set.
    //
    // FIXME: It might make sense to avoid changing thread creation and shutdown
    // behaviour until all threads have gone idle, and we are no longer
    // accepting events. This would unfortunately be fiddly without changing
    // nsThreadPool to use bare PRThreads instead of nsThread due to the need to
    // join each thread.
    name = mName;
    mShutdown = true;
    mIsAPoolThreadFree = !mMRUIdleThreads.isEmpty();
    NotifyChangeToAllIdleThreads();

    // From now on we do not allow the creation of new threads, and threads
    // will no longer shut themselves down. mThreads continues to track threads
    // within Run().
    threads.AppendObjects(mThreads);
  }

  nsTArray<nsCOMPtr<nsIThreadShutdown>> contexts;
  for (int32_t i = 0; i < threads.Count(); ++i) {
    nsCOMPtr<nsIThreadShutdown> context;
    if (NS_SUCCEEDED(threads[i]->BeginShutdown(getter_AddRefs(context)))) {
      contexts.AppendElement(std::move(context));
    }
  }

  // Start a timer which will stop waiting & leak the thread, forcing
  // onCompletion to be called when it expires.
  nsCOMPtr<nsITimer> timer;
  if (aTimeoutMs >= 0) {
    NS_NewTimerWithCallback(
        getter_AddRefs(timer),
        [&](nsITimer*) {
          {
            // Clear `mThreads` to stop accepting events on timeout.
            MutexAutoLock lock(mMutex);
            mThreads.Clear();
          }
          for (auto& context : contexts) {
            context->StopWaitingAndLeakThread();
          }
        },
        aTimeoutMs, nsITimer::TYPE_ONE_SHOT,
        "nsThreadPool::ShutdownWithTimeout"_ns);
  }

  // Start a counter and register a callback to decrement outstandingThreads
  // when the threads finish exiting. We'll spin an event loop until
  // outstandingThreads reaches 0.
  uint32_t outstandingThreads = contexts.Length();
  RefPtr onCompletion = NS_NewCancelableRunnableFunction(
      "nsThreadPool thread completion", [&] { --outstandingThreads; });
  for (auto& context : contexts) {
    context->OnCompletion(onCompletion);
  }

  mozilla::SpinEventLoopUntil("nsThreadPool::ShutdownWithTimeout "_ns + name,
                              [&] { return outstandingThreads == 0; });

  if (timer) {
    timer->Cancel();
  }
  onCompletion->Cancel();

  nsCOMPtr<nsIThreadPoolListener> listener;
  {
    MutexAutoLock lock(mMutex);
    MOZ_RELEASE_ASSERT(mThreads.IsEmpty(),
                       "Thread wasn't removed from mThreads");
    listener = mListener.forget();
  }

  return NS_OK;
}

NS_IMETHODIMP
nsThreadPool::GetThreadLimit(uint32_t* aValue) {
  MutexAutoLock lock(mMutex);
  *aValue = mThreadLimit;
  return NS_OK;
}

NS_IMETHODIMP
nsThreadPool::SetThreadLimit(uint32_t aValue) {
  MutexAutoLock lock(mMutex);
  LOG(("THRD-P(%p) thread limit [%u]\n", this, aValue));
  mThreadLimit = aValue;
  if (mIdleThreadLimit > mThreadLimit) {
    mIdleThreadLimit = mThreadLimit;
  }
  NotifyChangeToAllIdleThreads();
  return NS_OK;
}

NS_IMETHODIMP
nsThreadPool::GetIdleThreadLimit(uint32_t* aValue) {
  MutexAutoLock lock(mMutex);
  *aValue = mIdleThreadLimit;
  return NS_OK;
}

NS_IMETHODIMP
nsThreadPool::SetIdleThreadLimit(uint32_t aValue) {
  MutexAutoLock lock(mMutex);
  LOG(("THRD-P(%p) idle thread limit [%u]\n", this, aValue));
  mIdleThreadLimit = aValue;
  if (mIdleThreadLimit > mThreadLimit) {
    mIdleThreadLimit = mThreadLimit;
  }
  NotifyChangeToAllIdleThreads();
  return NS_OK;
}

NS_IMETHODIMP
nsThreadPool::GetIdleThreadGraceTimeout(uint32_t* aValue) {
  MutexAutoLock lock(mMutex);
  *aValue = (uint32_t)mIdleThreadGraceTimeout.ToMilliseconds();
  return NS_OK;
}

NS_IMETHODIMP
nsThreadPool::SetIdleThreadGraceTimeout(uint32_t aValue) {
  // We do not want to support forever here.
  MOZ_ASSERT(aValue != UINT32_MAX);

  MutexAutoLock lock(mMutex);
  TimeDuration oldTimeout = mIdleThreadGraceTimeout;
  mIdleThreadGraceTimeout = TimeDuration::FromMilliseconds(aValue);
  // We do not want to clamp here to avoid unexpected results due to the order
  // of calling the setters, but we also do not want to clamp where we use it
  // for performance reasons. Tell the caller.
  MOZ_ASSERT(mIdleThreadGraceTimeout <= mIdleThreadMaxTimeout);

  // Do we need to notify any idle threads that their sleep time has shortened?
  if (mIdleThreadGraceTimeout < oldTimeout) {
    NotifyChangeToAllIdleThreads();
  }
  return NS_OK;
}

NS_IMETHODIMP
nsThreadPool::GetIdleThreadMaximumTimeout(uint32_t* aValue) {
  MutexAutoLock lock(mMutex);
  *aValue = (uint32_t)mIdleThreadMaxTimeout.ToMilliseconds();
  return NS_OK;
}

NS_IMETHODIMP
nsThreadPool::SetIdleThreadMaximumTimeout(uint32_t aValue) {
  MutexAutoLock lock(mMutex);
  TimeDuration oldTimeout = mIdleThreadMaxTimeout;
  if (aValue == UINT32_MAX) {
    mIdleThreadMaxTimeout = TimeDuration::Forever();
  } else {
    mIdleThreadMaxTimeout = TimeDuration::FromMilliseconds(aValue);
  }
  // We do not want to clamp here to avoid unexpected results due to the order
  // of calling the setters, but we also do not want to clamp where we use it
  // for performance reasons. Tell the caller.
  MOZ_ASSERT(mIdleThreadGraceTimeout <= mIdleThreadMaxTimeout);

  // Do we need to notify any idle threads that their sleep time has shortened?
  if (mIdleThreadMaxTimeout < oldTimeout) {
    NotifyChangeToAllIdleThreads();
  }
  return NS_OK;
}

NS_IMETHODIMP
nsThreadPool::GetThreadStackSize(uint32_t* aValue) {
  MutexAutoLock lock(mMutex);
  *aValue = mStackSize;
  return NS_OK;
}

NS_IMETHODIMP
nsThreadPool::SetThreadStackSize(uint32_t aValue) {
  MutexAutoLock lock(mMutex);
  mStackSize = aValue;
  return NS_OK;
}

NS_IMETHODIMP
nsThreadPool::GetListener(nsIThreadPoolListener** aListener) {
  MutexAutoLock lock(mMutex);
  NS_IF_ADDREF(*aListener = mListener);
  return NS_OK;
}

NS_IMETHODIMP
nsThreadPool::SetListener(nsIThreadPoolListener* aListener) {
  nsCOMPtr<nsIThreadPoolListener> swappedListener(aListener);
  {
    MutexAutoLock lock(mMutex);
    mListener.swap(swappedListener);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsThreadPool::SetName(const nsACString& aName) {
  MutexAutoLock lock(mMutex);
  if (mThreads.Count()) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  mName = aName;
  return NS_OK;
}
