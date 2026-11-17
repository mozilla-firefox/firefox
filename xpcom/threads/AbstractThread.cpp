/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/AbstractThread.h"

#include <memory>

#include "mozilla/Atomics.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/DelayedRunnable.h"
#include "mozilla/MozPromise.h"  // We initialize the MozPromise logging in this file.
#include "mozilla/ProfilerRunnable.h"
#include "mozilla/StateWatching.h"  // We initialize the StateWatching logging in this file.
#include "mozilla/StaticPtr.h"
#include "mozilla/TaskDispatcher.h"
#include "mozilla/TaskQueue.h"
#include "nsContentUtils.h"
#include "nsIDirectTaskDispatcher.h"
#include "nsIThreadInternal.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadManager.h"
#include "nsThreadUtils.h"

namespace mozilla {

LazyLogModule gMozPromiseLog("MozPromise");
LazyLogModule gStateWatchingLog("StateWatching");

StaticAutoPtr<AutoXPCOMThreadWrapper> sMainThread;
MOZ_THREAD_LOCAL(AbstractThread*) AbstractThread::sCurrentThreadTLS;

class XPCOMThreadWrapper final : public AbstractThread,
                                 public nsIThreadObserver,
                                 public nsIDirectTaskDispatcher {
 public:
  XPCOMThreadWrapper(nsIThreadInternal* aThread,
                     enum TailDispatchPolicy aTailDispatchPolicy)
      : AbstractThread(aTailDispatchPolicy),
        mThread(aThread),
        mDirectTaskDispatcher(do_QueryInterface(aThread)) {
    MOZ_DIAGNOSTIC_ASSERT(mThread && mDirectTaskDispatcher);
    MOZ_DIAGNOSTIC_ASSERT(IsCurrentThreadIn());
  }

  // Makes this what GetCurrent() returns on mThread, until Unregister().
  // mThread only.
  void Register() {
    MOZ_ASSERT(!sCurrentThreadTLS.get(),
               "There can only be a single XPCOMThreadWrapper available on a "
               "thread");
    mUsageCount = 1;
    sCurrentThreadTLS.set(this);
  }

  // Stops being the current thread on mThread, see AutoXPCOMThreadWrapper.
  // mThread only.
  void Unregister() {
    MOZ_DIAGNOSTIC_ASSERT(IsCurrentThreadIn());
    MOZ_DIAGNOSTIC_ASSERT(sCurrentThreadTLS.get() == this);
    MOZ_ASSERT(!mTailDispatcher, "Tail tasks must have been dispatched by now");
    DebugOnly<uint32_t> remaining = --mUsageCount;
    MOZ_ASSERT(remaining == 0,
               "Tasks accepted by the wrapper must have run by now");
    sCurrentThreadTLS.set(nullptr);
  }

  NS_DECL_THREADSAFE_ISUPPORTS

  nsresult Dispatch(already_AddRefed<nsIRunnable> aRunnable,
                    DispatchReason aReason = NormalDispatch) override {
    nsCOMPtr<nsIRunnable> r = aRunnable;
    AbstractThread* currentThread;
    if (aReason != TailDispatch && (currentThread = GetCurrent()) &&
        RequiresTailDispatch(currentThread) &&
        currentThread->IsTailDispatcherAvailable()) {
      return currentThread->TailDispatcher().AddTask(this, r.forget());
    }

    while (true) {
      uint32_t count = mUsageCount;
      if (count == 0) {
        // Unregistered.
        return NS_ERROR_FAILURE;
      }
      if (mUsageCount.compareExchange(count, count + 1)) {
        break;
      }
    }

    RefPtr runner = MakeRefPtr<Runner>(this, r.forget());
    // Once mThread no longer processes events, like the main thread late in
    // shutdown, dispatching fails instead of queueing a task that would never
    // run. Dispatching fallibly releases the runner, on this thread, rather
    // than leaking it, which would otherwise leak the cycle thread -> event
    // queue -> Runner -> thread as well.
    nsresult rv = mThread->Dispatch(runner.forget(), NS_DISPATCH_FALLIBLE);
    if (NS_FAILED(rv)) {
      --mUsageCount;
    }
    return rv;
  }

  // Prevent a GCC warning about the other overload of Dispatch being hidden.
  using AbstractThread::Dispatch;

  NS_IMETHOD RegisterShutdownTask(nsITargetShutdownTask* aTask) override {
    return mThread->RegisterShutdownTask(aTask);
  }

  NS_IMETHOD UnregisterShutdownTask(nsITargetShutdownTask* aTask) override {
    return mThread->UnregisterShutdownTask(aTask);
  }

  NS_IMETHOD_(FeatureFlags) GetFeatures() override {
    return mThread->GetFeatures() |
           (SupportsTailDispatch() ? SUPPORTS_TAIL_DISPATCH : SUPPORTS_BASE);
  }

  bool IsCurrentThreadIn() const override {
    return mThread->IsOnCurrentThread();
  }

  TaskDispatcher& TailDispatcher() override {
    MOZ_ASSERT(IsCurrentThreadIn());
    MOZ_ASSERT(IsTailDispatcherAvailable());
    if (!mTailDispatcher) {
      mTailDispatcher = std::make_unique<AutoTaskDispatcher>(
          mDirectTaskDispatcher, mTailDispatcherPolicy);
      mThread->AddObserver(this);
    }

    return *mTailDispatcher;
  }

  bool IsTailDispatcherAvailable() override {
    // Our tail dispatching implementation relies on nsIThreadObserver
    // callbacks. If we're not doing event processing, it won't work.
    bool inEventLoop =
        static_cast<nsThread*>(mThread.get())->RecursionDepth() > 0;
    return inEventLoop;
  }

  bool MightHaveTailTasks() override { return !!mTailDispatcher; }

  nsIEventTarget* AsEventTarget() override { return mThread; }

  //-----------------------------------------------------------------------------
  // nsIThreadObserver
  //-----------------------------------------------------------------------------
  NS_IMETHOD OnDispatchedEvent() override { return NS_OK; }

  NS_IMETHOD AfterProcessNextEvent(nsIThreadInternal* thread,
                                   bool eventWasProcessed) override {
    // This is the primary case.
    MaybeFireTailDispatcher();
    return NS_OK;
  }

  NS_IMETHOD OnProcessNextEvent(nsIThreadInternal* thread,
                                bool mayWait) override {
    // In general, the tail dispatcher is handled at the end of the current in
    // AfterProcessNextEvent() above. However, if start spinning a nested event
    // loop, it's generally better to fire the tail dispatcher before the first
    // nested event, rather than after it. This check handles that case.
    MaybeFireTailDispatcher();
    return NS_OK;
  }

  //-----------------------------------------------------------------------------
  // nsIDirectTaskDispatcher
  //-----------------------------------------------------------------------------
  // Forward calls to nsIDirectTaskDispatcher to the underlying nsThread object.
  // We can't use the generated NS_FORWARD_NSIDIRECTTASKDISPATCHER macro
  // as already_AddRefed type must be moved.
  NS_IMETHOD DispatchDirectTask(already_AddRefed<nsIRunnable> aEvent) override {
    return mDirectTaskDispatcher->DispatchDirectTask(std::move(aEvent));
  }
  NS_IMETHOD DrainDirectTasks() override {
    return mDirectTaskDispatcher->DrainDirectTasks();
  }
  NS_IMETHOD HaveDirectTasks(bool* aResult) override {
    return mDirectTaskDispatcher->HaveDirectTasks(aResult);
  }

 private:
  const RefPtr<nsIThreadInternal> mThread;
  const nsCOMPtr<nsIDirectTaskDispatcher> mDirectTaskDispatcher;
  std::unique_ptr<AutoTaskDispatcher> mTailDispatcher;

  // Registration and every pending task counts as one.
  // Users must have stopped dispatching before unregistration. Unregister
  // checks that the count reaches zero.
  Atomic<uint32_t, Relaxed> mUsageCount{0};

  ~XPCOMThreadWrapper() {
    MOZ_ASSERT(mUsageCount == 0, "Wrappers are unregistered before they go");
  }

  void MaybeFireTailDispatcher() {
    if (mTailDispatcher) {
      mTailDispatcher->DrainDirectTasks();
      mThread->RemoveObserver(this);
      mTailDispatcher.reset();
    }
  }

  class Runner : public Runnable {
   public:
    explicit Runner(XPCOMThreadWrapper* aThread,
                    already_AddRefed<nsIRunnable> aRunnable)
        : Runnable("XPCOMThreadWrapper::Runner"),
          mThread(aThread),
          mRunnable(aRunnable) {}

    NS_IMETHOD Run() override {
      --mThread->mUsageCount;
      MOZ_ASSERT(mThread == AbstractThread::GetCurrent());
      MOZ_ASSERT(mThread->IsCurrentThreadIn());
      SerialEventTargetGuard guard(mThread);
      AUTO_PROFILE_FOLLOWING_RUNNABLE(mRunnable);
      return mRunnable->Run();
    }

#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
    NS_IMETHOD GetName(nsACString& aName) override {
      aName.AssignLiteral("AbstractThread::Runner");
      if (nsCOMPtr<nsINamed> named = do_QueryInterface(mRunnable)) {
        nsAutoCString name;
        named->GetName(name);
        if (!name.IsEmpty()) {
          aName.AppendLiteral(" for ");
          aName.Append(name);
        }
      }
      return NS_OK;
    }
#endif

   private:
    const RefPtr<XPCOMThreadWrapper> mThread;
    const RefPtr<nsIRunnable> mRunnable;
  };
};

NS_IMPL_ISUPPORTS(XPCOMThreadWrapper, nsIThreadObserver,
                  nsIDirectTaskDispatcher, nsISerialEventTarget, nsIEventTarget)

NS_IMETHODIMP_(bool)
AbstractThread::IsOnCurrentThreadInfallible() { return IsCurrentThreadIn(); }

NS_IMETHODIMP
AbstractThread::IsOnCurrentThread(bool* aResult) {
  *aResult = IsCurrentThreadIn();
  return NS_OK;
}

NS_IMETHODIMP
AbstractThread::DispatchFromScript(nsIRunnable* aEvent, DispatchFlags aFlags) {
  return Dispatch(do_AddRef(aEvent), aFlags);
}

NS_IMETHODIMP
AbstractThread::Dispatch(already_AddRefed<nsIRunnable> aEvent,
                         DispatchFlags aFlags) {
  // NOTE: This dispatch implementation never leaks aEvent on error, whether or
  // not `NS_DISPATCH_FALLIBLE` is specified.
  return Dispatch(std::move(aEvent), NormalDispatch);
}

NS_IMETHODIMP
AbstractThread::DelayedDispatch(already_AddRefed<nsIRunnable> aEvent,
                                uint32_t aDelayMs) {
  nsCOMPtr<nsIRunnable> event = aEvent;
  NS_ENSURE_TRUE(!!aDelayMs, NS_ERROR_UNEXPECTED);

  RefPtr r =
      MakeRefPtr<DelayedRunnable>(do_AddRef(this), event.forget(), aDelayMs);
  nsresult rv = r->Init();
  NS_ENSURE_SUCCESS(rv, rv);

  return Dispatch(r.forget(), NS_DISPATCH_NORMAL);
}

nsresult AbstractThread::TailDispatchTasksFor(AbstractThread* aThread) {
  if (MightHaveTailTasks()) {
    return TailDispatcher().DispatchTasksFor(aThread);
  }

  return NS_OK;
}

bool AbstractThread::HasTailTasksFor(AbstractThread* aThread) {
  if (!MightHaveTailTasks()) {
    return false;
  }
  return TailDispatcher().HasTasksFor(aThread);
}

bool AbstractThread::RequiresTailDispatch(AbstractThread* aThread) const {
  MOZ_ASSERT(aThread);
  // We require tail dispatch if both the source and destination
  // threads support it.
  return SupportsTailDispatch() && aThread->SupportsTailDispatch();
}

bool AbstractThread::RequiresTailDispatchFromCurrentThread() const {
  AbstractThread* current = GetCurrent();
  return current && RequiresTailDispatch(current);
}

AbstractThread* AbstractThread::MainThread() {
  MOZ_ASSERT(sMainThread);
  return sMainThread ? sMainThread->get() : nullptr;
}

void AbstractThread::InitTLS() {
  if (!sCurrentThreadTLS.init()) {
    MOZ_CRASH();
  }
}

void AbstractThread::InitMainThread() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!sMainThread);

  if (!sCurrentThreadTLS.init()) {
    MOZ_CRASH();
  }
  sMainThread =
      new AutoXPCOMThreadWrapper(TailDispatchPolicy::ConsistentOrdering);
}

void AbstractThread::ShutdownMainThread() {
  MOZ_ASSERT(NS_IsMainThread());
  sMainThread = nullptr;
}

AutoXPCOMThreadWrapper::AutoXPCOMThreadWrapper(
    TailDispatchPolicy aTailDispatchPolicy)
    : mWrapper(new XPCOMThreadWrapper(nsThreadManager::get().GetCurrentThread(),
                                      aTailDispatchPolicy)) {
  mWrapper->Register();
}

AbstractThread* AutoXPCOMThreadWrapper::get() const { return mWrapper; }

AutoXPCOMThreadWrapper::~AutoXPCOMThreadWrapper() { mWrapper->Unregister(); }

void AbstractThread::DispatchStateChange(
    already_AddRefed<nsIRunnable> aRunnable) {
  AbstractThread* currentThread = GetCurrent();
  MOZ_DIAGNOSTIC_ASSERT(currentThread, "An AbstractThread must exist");
  if (currentThread->IsTailDispatcherAvailable()) {
    currentThread->TailDispatcher().AddStateChangeTask(this,
                                                       std::move(aRunnable));
  } else {
    // If the tail dispatcher isn't available, we just avoid sending state
    // updates.
    //
    // This happens, specifically (1) During async shutdown (via the media
    // shutdown blocker), and (2) During the final shutdown cycle collection.
    // Both of these trigger changes to various watched and mirrored state.
    nsCOMPtr<nsIRunnable> neverDispatched = aRunnable;
  }
}

/* static */
void AbstractThread::DispatchDirectTask(
    already_AddRefed<nsIRunnable> aRunnable) {
  AbstractThread* currentThread = GetCurrent();
  MOZ_DIAGNOSTIC_ASSERT(currentThread, "An AbstractThread must exist");
  if (currentThread->IsTailDispatcherAvailable()) {
    currentThread->TailDispatcher().AddDirectTask(std::move(aRunnable));
  } else {
    // If the tail dispatcher isn't available, we post as a regular task.
    currentThread->Dispatch(std::move(aRunnable));
  }
}

}  // namespace mozilla
