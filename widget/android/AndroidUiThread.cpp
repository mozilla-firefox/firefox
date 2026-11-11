/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <android/api-level.h>
#include <pthread.h>

#include <memory>

#include "GeckoProfiler.h"
#include "base/message_loop.h"
#include "mozilla/Atomics.h"
#include "mozilla/EventQueue.h"
#include "mozilla/LinkedList.h"
#include "mozilla/Monitor.h"
#include "mozilla/Mutex.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/ThreadEventQueue.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/java/GeckoThreadWrappers.h"
#include "nsThread.h"
#include "nsThreadManager.h"
#include "nsThreadUtils.h"

using namespace mozilla;

namespace {

class AndroidUiThread;
class AndroidUiTask;

StaticAutoPtr<mozilla::Mutex> sTaskQueueLock;
StaticAutoPtr<LinkedList<AndroidUiTask> > sTaskQueue
    MOZ_GUARDED_BY(sTaskQueueLock);
StaticRefPtr<AndroidUiThread> sThread;
static bool sThreadDestroyed;
static MessageLoop* sMessageLoop;
static Atomic<Monitor*> sMessageLoopAccessMonitor;

void EnqueueTask(already_AddRefed<nsIRunnable> aTask, int aDelayMs);

/*
 * The AndroidUiThread is derived from nsThread so that nsIRunnable objects that
 * get dispatched may be intercepted. Only nsIRunnable objects that need to be
 * synchronously executed are passed into the nsThread to be queued. All other
 * nsIRunnable object are immediately dispatched to the Android UI thread.
 * AndroidUiThread is derived from nsThread instead of being an nsIEventTarget
 * wrapper that contains an nsThread object because if nsIRunnable objects with
 * a delay were dispatch directly to an nsThread object, such as obtained from
 * nsThreadManager::GetCurrentThread(), the nsIRunnable could get stuck in the
 * nsThread nsIRunnable queue. This is due to the fact that Android controls the
 * event loop in the Android UI thread and has no knowledge of when the nsThread
 * needs to be drained.
 */

class AndroidUiThread : public nsThread {
 public:
  NS_INLINE_DECL_REFCOUNTING_INHERITED(AndroidUiThread, nsThread)
  AndroidUiThread()
      : nsThread(MakeNotNull<ThreadEventQueue*>(
                     std::make_unique<mozilla::EventQueue>()),
                 nsThread::NOT_MAIN_THREAD, {.stackSize = 0}) {}

  nsresult Dispatch(already_AddRefed<nsIRunnable> aEvent,
                    DispatchFlags aFlags) override;
  nsresult DelayedDispatch(already_AddRefed<nsIRunnable> aEvent,
                           uint32_t aDelayMs) override;

 private:
  ~AndroidUiThread() {}
};

NS_IMETHODIMP
AndroidUiThread::Dispatch(already_AddRefed<nsIRunnable> aEvent,
                          DispatchFlags aFlags) {
  EnqueueTask(std::move(aEvent), 0);
  return NS_OK;
}

NS_IMETHODIMP
AndroidUiThread::DelayedDispatch(already_AddRefed<nsIRunnable> aEvent,
                                 uint32_t aDelayMs) {
  EnqueueTask(std::move(aEvent), aDelayMs);
  return NS_OK;
}

static void PumpEvents() { NS_ProcessPendingEvents(sThread.get()); }

class ThreadObserver : public nsIThreadObserver {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSITHREADOBSERVER

  ThreadObserver() {}

 private:
  virtual ~ThreadObserver() {}
};

NS_IMPL_ISUPPORTS(ThreadObserver, nsIThreadObserver)

NS_IMETHODIMP
ThreadObserver::OnDispatchedEvent() {
  EnqueueTask(NS_NewRunnableFunction("PumpEvents", &PumpEvents), 0);
  return NS_OK;
}

NS_IMETHODIMP
ThreadObserver::OnProcessNextEvent(nsIThreadInternal* thread, bool mayWait) {
  return NS_OK;
}

NS_IMETHODIMP
ThreadObserver::AfterProcessNextEvent(nsIThreadInternal* thread,
                                      bool eventWasProcessed) {
  return NS_OK;
}

class AndroidUiTask : public LinkedListElement<AndroidUiTask> {
  using TimeStamp = mozilla::TimeStamp;
  using TimeDuration = mozilla::TimeDuration;

 public:
  explicit AndroidUiTask(already_AddRefed<nsIRunnable> aTask)
      : mTask(aTask),
        mRunTime()  // Null timestamp representing no delay.
  {}

  AndroidUiTask(already_AddRefed<nsIRunnable> aTask, int aDelayMs)
      : mTask(aTask),
        mRunTime(TimeStamp::Now() + TimeDuration::FromMilliseconds(aDelayMs)) {}

  bool IsEarlierThan(const AndroidUiTask& aOther) const {
    if (mRunTime) {
      return aOther.mRunTime ? mRunTime < aOther.mRunTime : false;
    }
    // In the case of no delay, we're earlier if aOther has a delay.
    // Otherwise, we're not earlier, to maintain task order.
    return !!aOther.mRunTime;
  }

  int64_t MillisecondsToRunTime() const {
    if (mRunTime) {
      return int64_t((mRunTime - TimeStamp::Now()).ToMilliseconds());
    }
    return 0;
  }

  already_AddRefed<nsIRunnable> TakeTask() { return mTask.forget(); }

 private:
  nsCOMPtr<nsIRunnable> mTask;
  const TimeStamp mRunTime;
};

class CreateOnUiThread : public Runnable {
 public:
  CreateOnUiThread() : Runnable("CreateOnUiThread") {}

  NS_IMETHOD Run() override {
    MOZ_ASSERT(!sThreadDestroyed);
    MOZ_ASSERT(sMessageLoopAccessMonitor);
    MonitorAutoLock lock(*sMessageLoopAccessMonitor);
    sThread = new AndroidUiThread();
    sThread->InitCurrentThread();
    sThread->SetObserver(new ThreadObserver());
    RegisterThreadWithProfiler();
    sMessageLoop =
        new MessageLoop(MessageLoop::TYPE_MOZILLA_ANDROID_UI, sThread.get());
    lock.NotifyAll();
    return NS_OK;
  }

 private:
  static void RegisterThreadWithProfiler() {
    // We don't use the PROFILER_REGISTER_THREAD macro here because by this
    // point the Android UI thread is already quite a ways into its stack;
    // the profiler's sampler thread will ignore a lot of frames if we do not
    // provide a better value for the stack top. We'll reuse the info already
    // obtained via pthreads.
    const char* stackTop = static_cast<const char*>(sThread->StackBase()) +
                           sThread->StackSize() - 1;
    profiler_register_thread("AndroidUI", const_cast<char*>(stackTop));
  }
};

class DestroyOnUiThread : public Runnable {
 public:
  DestroyOnUiThread() : Runnable("DestroyOnUiThread"), mDestroyed(false) {}

  NS_IMETHOD Run() override {
    MOZ_ASSERT(!sThreadDestroyed);
    MOZ_ASSERT(sMessageLoopAccessMonitor);
    MonitorAutoLock lock(*sMessageLoopAccessMonitor);
    sThreadDestroyed = true;

    {
      // Flush the queue
      MutexAutoLock lock(*sTaskQueueLock);
      MOZ_ASSERT(sTaskQueue);
      while (AndroidUiTask* task = sTaskQueue->getFirst()) {
        delete task;
      }
    }

    delete sMessageLoop;
    sMessageLoop = nullptr;
    MOZ_ASSERT(sThread);
    PROFILER_UNREGISTER_THREAD();
    nsThreadManager::get().UnregisterCurrentThread(*sThread);
    sThread = nullptr;
    mDestroyed = true;
    lock.NotifyAll();
    return NS_OK;
  }

  void WaitForDestruction() {
    MOZ_ASSERT(sMessageLoopAccessMonitor);
    MonitorAutoLock lock(*sMessageLoopAccessMonitor);
    while (!mDestroyed) {
      lock.Wait();
    }
  }

 private:
  bool mDestroyed;
};

void EnqueueTask(already_AddRefed<nsIRunnable> aTask, int aDelayMs) {
  if (sThreadDestroyed) {
    return;
  }

  // add the new task into the sTaskQueue, sorted with
  // the earliest task first in the queue
  AndroidUiTask* newTask =
      (aDelayMs ? new AndroidUiTask(std::move(aTask), aDelayMs)
                : new AndroidUiTask(std::move(aTask)));

  bool headOfList = false;
  {
    MOZ_ASSERT(sTaskQueueLock);
    MutexAutoLock lock(*sTaskQueueLock);

    MOZ_ASSERT(sTaskQueue);
    AndroidUiTask* task = sTaskQueue->getFirst();

    while (task) {
      if (newTask->IsEarlierThan(*task)) {
        task->setPrevious(newTask);
        break;
      }
      task = task->getNext();
    }

    if (!newTask->isInList()) {
      sTaskQueue->insertBack(newTask);
    }
    headOfList = !newTask->getPrevious();
  }

  if (headOfList) {
    // if we're inserting it at the head of the queue, notify Java because
    // we need to get a callback at an earlier time than the last scheduled
    // callback
    java::GeckoThread::RequestUiThreadCallback(int64_t(aDelayMs));
  }
}

}  // namespace

namespace mozilla {

void CreateAndroidUiThread() {
  MOZ_ASSERT(!sThread);
  MOZ_ASSERT(!sMessageLoopAccessMonitor);
  // No lock needed.
  MOZ_PUSH_IGNORE_THREAD_SAFETY
  sTaskQueue = new LinkedList<AndroidUiTask>();
  MOZ_POP_THREAD_SAFETY
  sTaskQueueLock = new Mutex("AndroidUiThreadTaskQueueLock");
  sMessageLoopAccessMonitor =
      new Monitor("AndroidUiThreadMessageLoopAccessMonitor");
  sThreadDestroyed = false;
  RefPtr<CreateOnUiThread> runnable = new CreateOnUiThread;
  EnqueueTask(do_AddRef(runnable), 0);
}

void DestroyAndroidUiThread() {
  MOZ_ASSERT(sThread);
  RefPtr<DestroyOnUiThread> runnable = new DestroyOnUiThread;
  EnqueueTask(do_AddRef(runnable), 0);
  runnable->WaitForDestruction();
  delete sMessageLoopAccessMonitor;
  sMessageLoopAccessMonitor = nullptr;
}

MessageLoop* GetAndroidUiThreadMessageLoop() {
  if (!sMessageLoopAccessMonitor) {
    return nullptr;
  }

  MonitorAutoLock lock(*sMessageLoopAccessMonitor);
  while (!sMessageLoop) {
    lock.Wait();
  }

  return sMessageLoop;
}

RefPtr<nsThread> GetAndroidUiThread() {
  if (!sMessageLoopAccessMonitor) {
    return nullptr;
  }

  MonitorAutoLock lock(*sMessageLoopAccessMonitor);
  while (!sThread) {
    lock.Wait();
  }

  return sThread;
}

int64_t RunAndroidUiTasks() {
  MutexAutoLock lock(*sTaskQueueLock);

  if (sThreadDestroyed) {
    return -1;
  }

  while (!sTaskQueue->isEmpty()) {
    AndroidUiTask* task = sTaskQueue->getFirst();
    const int64_t timeLeft = task->MillisecondsToRunTime();
    if (timeLeft > 0) {
      // this task (and therefore all remaining tasks)
      // have not yet reached their runtime. return the
      // time left until we should be called again
      return timeLeft;
    }

    // Retrieve task before unlocking/running.
    nsCOMPtr<nsIRunnable> runnable(task->TakeTask());
    // LinkedListElements auto remove from list upon destruction
    delete task;

    // Unlock to allow posting new tasks reentrantly.
    MutexAutoUnlock unlock(*sTaskQueueLock);
    runnable->Run();
    if (sThreadDestroyed) {
      return -1;
    }
  }
  return -1;
}

}  // namespace mozilla
