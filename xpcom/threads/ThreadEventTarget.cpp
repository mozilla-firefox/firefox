/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ThreadEventTarget.h"

#include "MaybeLeakRefPtr.h"
#include "ThreadDelay.h"
#include "mozilla/DelayedRunnable.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "mozilla/ThreadEventQueue.h"
#include "mozilla/TimeStamp.h"
#include "nsComponentManagerUtils.h"
#include "nsITimer.h"
#include "nsThreadManager.h"
#include "nsThreadSyncDispatch.h"
#include "nsThreadUtils.h"

using namespace mozilla;

#ifdef DEBUG
// This flag will be set right after XPCOMShutdownThreads finished but before
// we continue with other processing. It is exclusively meant to prime the
// assertion of ThreadEventTarget::Dispatch as early as possible.
// Please use AppShutdown::IsInOrBeyond(ShutdownPhase::???)
// elsewhere to check for shutdown phases.
static mozilla::Atomic<bool, mozilla::SequentiallyConsistent>
    gXPCOMThreadsShutDownNotified(false);
#endif

ThreadEventTarget::ThreadEventTarget(ThreadTargetSink* aSink,
                                     bool aIsMainThread, bool aBlockDispatch)
    : mSink(aSink),
#ifdef DEBUG
      mIsMainThread(aIsMainThread),
#endif
      mBlockDispatch(aBlockDispatch) {
  mThread = PR_GetCurrentThread();
}

ThreadEventTarget::~ThreadEventTarget() = default;

void ThreadEventTarget::SetCurrentThread(PRThread* aThread) {
  mThread = aThread;
}

void ThreadEventTarget::ClearCurrentThread() { mThread = nullptr; }

NS_IMPL_ISUPPORTS(ThreadEventTarget, nsIEventTarget, nsISerialEventTarget)

NS_IMETHODIMP
ThreadEventTarget::DispatchFromScript(nsIRunnable* aRunnable,
                                      DispatchFlags aFlags) {
  return Dispatch(do_AddRef(aRunnable), aFlags);
}

#ifdef DEBUG
// static
void ThreadEventTarget::XPCOMShutdownThreadsNotificationFinished() {
  gXPCOMThreadsShutDownNotified = true;
}
#endif

NS_IMETHODIMP
ThreadEventTarget::Dispatch(already_AddRefed<nsIRunnable> aEvent,
                            DispatchFlags aFlags) {
  MaybeLeakRefPtr<nsIRunnable> event(std::move(aEvent),
                                     aFlags & NS_DISPATCH_FALLIBLE);
  if (NS_WARN_IF(!event)) {
    return NS_ERROR_INVALID_ARG;
  }

  NS_ASSERTION(
      !gXPCOMThreadsShutDownNotified || mIsMainThread ||
          PR_GetCurrentThread() == mThread ||
          (aFlags & NS_DISPATCH_IGNORE_BLOCK_DISPATCH) ||
          (aFlags & NS_DISPATCH_FALLIBLE),
      "Infallible dispatch to non-main thread after xpcom-shutdown-threads");

  if (mBlockDispatch && !(aFlags & NS_DISPATCH_IGNORE_BLOCK_DISPATCH)) {
    MOZ_DIAGNOSTIC_ASSERT(
        aFlags & NS_DISPATCH_FALLIBLE,
        "Attempt to dispatch to thread which does not usually process "
        "dispatched runnables until shutdown");
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  LogRunnable::LogDispatch(event.get());

  if (!mSink->PutEvent(event, EventQueuePriority::Normal)) {
    return NS_ERROR_UNEXPECTED;
  }
  // Random delay, so the dispatching and running threads race in varying
  // order.
  DelayForChaosMode(ChaosFeature::TaskDispatching);
  return NS_OK;
}

NS_IMETHODIMP
ThreadEventTarget::DelayedDispatch(already_AddRefed<nsIRunnable> aEvent,
                                   uint32_t aDelayMs) {
  nsCOMPtr<nsIRunnable> event = aEvent;
  NS_ENSURE_TRUE(!!aDelayMs, NS_ERROR_UNEXPECTED);

  RefPtr r =
      MakeRefPtr<DelayedRunnable>(do_AddRef(this), event.forget(), aDelayMs);
  nsresult rv = r->Init();
  NS_ENSURE_SUCCESS(rv, rv);

  return Dispatch(r.forget(), NS_DISPATCH_NORMAL);
}

NS_IMETHODIMP
ThreadEventTarget::RegisterShutdownTask(nsITargetShutdownTask* aTask) {
  return mSink->RegisterShutdownTask(aTask);
}

NS_IMETHODIMP
ThreadEventTarget::UnregisterShutdownTask(nsITargetShutdownTask* aTask) {
  return mSink->UnregisterShutdownTask(aTask);
}

nsIEventTarget::FeatureFlags ThreadEventTarget::GetFeatures() {
  // TODO: Check if SUPPORTS_SHUTDOWN_TASK_DISPATCH.
  return SUPPORTS_SHUTDOWN_TASKS;
}

NS_IMETHODIMP
ThreadEventTarget::IsOnCurrentThread(bool* aIsOnCurrentThread) {
  *aIsOnCurrentThread = IsOnCurrentThread();
  return NS_OK;
}

NS_IMETHODIMP_(bool)
ThreadEventTarget::IsOnCurrentThreadInfallible() {
  // This method is only going to be called if `mThread` is null, which
  // only happens when the thread has exited the event loop.  Therefore, when
  // we are called, we can never be on this thread.
  return false;
}
