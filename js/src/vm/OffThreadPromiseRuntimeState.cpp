/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "vm/OffThreadPromiseRuntimeState.h"

#include "mozilla/Assertions.h"  // MOZ_ASSERT{,_IF}

#include <utility>  // mozilla::Swap

#include "jspubtd.h"  // js::CurrentThreadCanAccessRuntime

#include "js/AllocPolicy.h"  // js::ReportOutOfMemory
#include "js/HeapAPI.h"      // JS::shadow::Zone
#include "js/Promise.h"  // JS::Dispatchable, JS::DispatchToEventLoopCallback,
                         // JS::DelayedDispatchToEventLoopCallback
#include "js/Utility.h"  // js_delete, js::AutoEnterOOMUnsafeRegion
#include "threading/ProtectedData.h"  // js::UnprotectedData
#include "vm/HelperThreads.h"         // js::AutoLockHelperThreadState
#include "vm/JSContext.h"             // JSContext
#include "vm/PromiseObject.h"         // js::PromiseObject
#include "vm/Realm.h"                 // js::AutoRealm
#include "vm/Runtime.h"               // JSRuntime

#include "vm/Realm-inl.h"  // js::AutoRealm::AutoRealm

using JS::Handle;

using js::OffThreadPromiseRuntimeState;
using js::OffThreadPromiseTask;

OffThreadPromiseTask::OffThreadPromiseTask(JSContext* cx,
                                           JS::Handle<PromiseObject*> promise)
    : runtime_(cx->runtime()), promise_(cx, promise), cancellable_(false) {
  MOZ_ASSERT(runtime_ == promise_->zone()->runtimeFromMainThread());
  MOZ_ASSERT(CurrentThreadCanAccessRuntime(runtime_));
  MOZ_ASSERT(cx->runtime()->offThreadPromiseState.ref().initialized());
}

OffThreadPromiseTask::~OffThreadPromiseTask() {
  MOZ_ASSERT(CurrentThreadCanAccessRuntime(runtime_));

  if (registered_) {
    OffThreadPromiseRuntimeState& state = runtime_->offThreadPromiseState.ref();
    MOZ_RELEASE_ASSERT(state.initialized());
    unregister(state);
  }
}

bool OffThreadPromiseTask::init(JSContext* cx) {
  AutoLockHelperThreadState lock;
  return init(cx, lock);
}

bool OffThreadPromiseTask::init(JSContext* cx,
                                const AutoLockHelperThreadState& lock) {
  MOZ_ASSERT(cx->runtime() == runtime_);
  MOZ_ASSERT(CurrentThreadCanAccessRuntime(runtime_));

  OffThreadPromiseRuntimeState& state = runtime_->offThreadPromiseState.ref();
  if (!state.initialized()) {
    // A task registered after shutdown would never be cancelled or freed
    // before the runtime goes away.  We may hold the futex lock here, so use
    // the non-allocating error report.
    ReportOutOfMemory(cx);
    return false;
  }

  state.registerTask(cx, this);
  return true;
}

bool OffThreadPromiseTask::InitCancellable(
    JSContext* cx, js::UniquePtr<OffThreadPromiseTask>&& task) {
  AutoLockHelperThreadState lock;
  return InitCancellable(cx, lock, std::move(task));
}

bool OffThreadPromiseTask::InitCancellable(
    JSContext* cx, const AutoLockHelperThreadState& lock,
    js::UniquePtr<OffThreadPromiseTask>&& task) {
  MOZ_ASSERT(cx->runtime() == task->runtime_);
  MOZ_ASSERT(CurrentThreadCanAccessRuntime(task->runtime_));
  OffThreadPromiseRuntimeState& state =
      task->runtime_->offThreadPromiseState.ref();
  if (!task->init(cx, lock)) {
    return false;
  }

  if (!state.cancellable().putNew(task.get())) {
    // The task will be freed because it's only owned by this function. Eagerly
    // unregister it now using the provided helper thread lock so that it
    // doesn't need to be reacquired.
    task->unregister(state, lock);
    ReportOutOfMemory(cx);
    return false;
  }

  // We're infallible from this point on.
  OffThreadPromiseTask* rawTask = task.release();

  // Only mark the task as cancellable once we've added it to the cancellable
  // set. The destructor will remove from the set if this flag is set.
  rawTask->cancellable_ = true;

  return true;
}

void OffThreadPromiseTask::unregister(OffThreadPromiseRuntimeState& state) {
  AutoLockHelperThreadState lock;
  unregister(state, lock);
}
void OffThreadPromiseTask::unregister(OffThreadPromiseRuntimeState& state,
                                      const AutoLockHelperThreadState& lock) {
  MOZ_ASSERT(registered_);
  state.unregisterTask(this);
}

void OffThreadPromiseTask::run(JSContext* cx,
                               MaybeShuttingDown maybeShuttingDown) {
  MOZ_ASSERT(cx->runtime() == runtime_);
  MOZ_ASSERT(CurrentThreadCanAccessRuntime(runtime_));
  MOZ_ASSERT(registered_);

  // Remove this task from numRegistered_ before calling `resolve`, so that if
  // `resolve` itself drains the queue reentrantly, the queue will not think
  // this task is yet to be queued and block waiting for it.
  //
  // The unregister method synchronizes on the helper thread lock and ensures
  // that we don't delete the task while the helper thread is still running.
  OffThreadPromiseRuntimeState& state = runtime_->offThreadPromiseState.ref();
  MOZ_RELEASE_ASSERT(state.initialized());
  unregister(state);

  if (maybeShuttingDown == JS::Dispatchable::NotShuttingDown) {
    // We can't leave a pending exception when returning to the caller so do
    // the same thing as Gecko, which is to ignore the error. This should
    // only happen due to OOM or interruption.
    AutoRealm ar(cx, promise_);
    if (!resolve(cx, promise_)) {
      cx->clearPendingException();
    }
  }

  js_delete(this);
}

void OffThreadPromiseTask::transferToRuntime() {
  MOZ_ASSERT(registered_);

  // The unregister method synchronizes on the helper thread lock and ensures
  // that we don't delete the task while the helper thread is still running.
  OffThreadPromiseRuntimeState& state = runtime_->offThreadPromiseState.ref();
  MOZ_RELEASE_ASSERT(state.initialized());

  // Task is now owned by the state and will be deleted on ::shutdown.
  state.stealFailedTask(this);
}

/* static */
void OffThreadPromiseTask::DestroyUndispatchedTask(
    OffThreadPromiseTask* task, OffThreadPromiseRuntimeState& state,
    const AutoLockHelperThreadState& lock) {
  MOZ_ASSERT(CurrentThreadCanAccessRuntime(task->runtime_));
  MOZ_ASSERT(task->registered_);
  MOZ_ASSERT(task->cancellable_);
  // Cleanup Steps from 4. in SMDOC for Atomics.waitAsync
  task->prepareForCancel();
  // unregister with the passed lock. Necessary so that
  // there is no conflict in js_delete with shutdown code.
  task->unregister(state, lock);
  js_delete(task);
}

void OffThreadPromiseTask::dispatchResolveAndDestroy() {
  AutoLockHelperThreadState lock;
  js::UniquePtr<OffThreadPromiseTask> task(this);
  DispatchResolveAndDestroy(std::move(task), lock);
}

void OffThreadPromiseTask::dispatchResolveAndDestroy(
    const AutoLockHelperThreadState& lock) {
  js::UniquePtr<OffThreadPromiseTask> task(this);
  DispatchResolveAndDestroy(std::move(task), lock);
}

void OffThreadPromiseTask::removeFromCancellableListAndDispatch() {
  AutoLockHelperThreadState lock;
  removeFromCancellableListAndDispatch(lock);
}

void OffThreadPromiseTask::removeFromCancellableListAndDispatch(
    const AutoLockHelperThreadState& lock) {
  OffThreadPromiseRuntimeState& state = runtime_->offThreadPromiseState.ref();
  MOZ_RELEASE_ASSERT(state.initialized());
  MOZ_ASSERT(state.cancellable().has(this));

  MOZ_ASSERT(registered_);
  MOZ_ASSERT(cancellable_);
  cancellable_ = false;
  // remove this task from the runnable's cancellable list. This ends the
  // runtime's ownership of the the task.
  state.cancellable().remove(this);

  // Create a UniquePtr that will be passed to the embedding.
  js::UniquePtr<OffThreadPromiseTask> task;
  // move ownership of this task to the newly created pointer
  task.reset(this);
  DispatchResolveAndDestroy(std::move(task), lock);
}

/* static */
void OffThreadPromiseTask::DispatchResolveAndDestroy(
    js::UniquePtr<OffThreadPromiseTask>&& task) {
  AutoLockHelperThreadState lock;
  DispatchResolveAndDestroy(std::move(task), lock);
}

/* static */
void OffThreadPromiseTask::DispatchResolveAndDestroy(
    js::UniquePtr<OffThreadPromiseTask>&& task,
    const AutoLockHelperThreadState& lock) {
  OffThreadPromiseRuntimeState& state =
      task->runtime()->offThreadPromiseState.ref();
  MOZ_RELEASE_ASSERT(state.initialized());

  MOZ_ASSERT(task->registered_);
  MOZ_ASSERT(!task->cancellable_);
  // If the dispatch succeeds, then we are guaranteed that run() will be
  // called on an active JSContext of runtime_.
  {
    // Hazard analysis can't tell that the callback does not GC.
    JS::AutoSuppressGCAnalysis nogc;
    if (state.dispatchToEventLoop(std::move(task))) {
      return;
    }
  }

  // The DispatchToEventLoopCallback has failed to dispatch this task,
  // indicating that shutdown has begun. Compare the number of failed tasks that
  // have called dispatchResolveAndDestroy, and when they account for all of
  // numRegistered_, notify OffThreadPromiseRuntimeState::shutdown that it is
  // safe to destruct them.
  if (state.failed().length() == state.numRegistered_) {
    state.allFailed().notify_one();
  }
}

OffThreadPromiseRuntimeState::OffThreadPromiseRuntimeState()
    : dispatchToEventLoopCallback_(nullptr),
      delayedDispatchToEventLoopCallback_(nullptr),
      asyncTaskStartedCallback_(nullptr),
      asyncTaskFinishedCallback_(nullptr),
      dispatchToEventLoopClosure_(nullptr),
#ifdef DEBUG
      forceQuitting_(false),
#endif
      numRegistered_(0),
      internalDispatchQueueClosed_(false) {
}

OffThreadPromiseRuntimeState::~OffThreadPromiseRuntimeState() {
  MOZ_ASSERT_IF(!forceQuitting_, numRegistered_ == 0);
  MOZ_ASSERT_IF(!forceQuitting_, numDelayed_ == 0);
  MOZ_ASSERT_IF(!forceQuitting_, internalDispatchQueue_.refNoCheck().empty());
  MOZ_ASSERT(!initialized());
}

void OffThreadPromiseRuntimeState::init(
    JS::DispatchToEventLoopCallback dispatchCallback,
    JS::DelayedDispatchToEventLoopCallback delayedDispatchCallback,
    JS::AsyncTaskStartedCallback asyncTaskStartedCallback,
    JS::AsyncTaskFinishedCallback asyncTaskFinishedCallback, void* closure) {
  MOZ_ASSERT(!initialized());

  dispatchToEventLoopCallback_ = dispatchCallback;
  delayedDispatchToEventLoopCallback_ = delayedDispatchCallback;
  asyncTaskStartedCallback_ = asyncTaskStartedCallback;
  asyncTaskFinishedCallback_ = asyncTaskFinishedCallback;
  dispatchToEventLoopClosure_ = closure;

  MOZ_ASSERT(initialized());
}

bool OffThreadPromiseRuntimeState::dispatchToEventLoop(
    js::UniquePtr<JS::Dispatchable>&& dispatchable) {
  return dispatchToEventLoopCallback_(dispatchToEventLoopClosure_,
                                      std::move(dispatchable));
}

bool OffThreadPromiseRuntimeState::delayedDispatchToEventLoop(
    js::UniquePtr<JS::Dispatchable>&& dispatchable, uint32_t delay) {
  return delayedDispatchToEventLoopCallback_(dispatchToEventLoopClosure_,
                                             std::move(dispatchable), delay);
}

void OffThreadPromiseRuntimeState::registerTask(JSContext* cx,
                                                OffThreadPromiseTask* task) {
  // Track the total number of pending async tasks
  numRegistered_++;

  // Mark the task as registered
  task->registered_ = true;

  if (!asyncTaskStartedCallback_) {
    return;
  }

  // The embedder must not perform a GC, suppress GC analysis here.
  JS::AutoSuppressGCAnalysis nogc(cx);
  asyncTaskStartedCallback_(dispatchToEventLoopClosure_, task);
}

void OffThreadPromiseRuntimeState::unregisterTask(OffThreadPromiseTask* task) {
  // Track the total number of pending async tasks
  MOZ_ASSERT(numRegistered_ != 0);
  numRegistered_--;

  // Mark the task as unregistered
  task->registered_ = false;

  // If the task was cancellable, remove from our cancellable set.
  if (task->cancellable_) {
    task->cancellable_ = false;
    cancellable().remove(task);
  }

  if (!asyncTaskFinishedCallback_) {
    return;
  }

  // The embedder must not perform a GC, suppress GC analysis here.
  JS::AutoSuppressGCAnalysis nogc;
  asyncTaskFinishedCallback_(dispatchToEventLoopClosure_, task);
}

/* static */
bool OffThreadPromiseRuntimeState::internalDispatchToEventLoop(
    void* closure, js::UniquePtr<JS::Dispatchable>&& d) {
  OffThreadPromiseRuntimeState& state =
      *reinterpret_cast<OffThreadPromiseRuntimeState*>(closure);
  MOZ_ASSERT(state.usingInternalDispatchQueue());
  gHelperThreadLock.assertOwnedByCurrentThread();

  if (state.internalDispatchQueueClosed_) {
    JS::Dispatchable::ReleaseFailedTask(std::move(d));
    return false;
  }

  state.dispatchDelayedTasks();

  // The JS API contract is that 'false' means shutdown, so be infallible
  // here (like Gecko).
  AutoEnterOOMUnsafeRegion noOOM;
  if (!state.internalDispatchQueue().pushBack(std::move(d))) {
    noOOM.crash("internalDispatchToEventLoop");
  }

  // Wake up internalDrain() if it is waiting for a job to finish.
  state.internalDispatchQueueAppended().notify_one();
  return true;
}

// This function (and all related delayedDispatch data structures), are in place
// in order to make the JS Shell work as expected with delayed tasks such as
// Atomics.waitAsync.
bool OffThreadPromiseRuntimeState::internalDelayedDispatchToEventLoop(
    void* closure, js::UniquePtr<JS::Dispatchable>&& d, uint32_t delay) {
  OffThreadPromiseRuntimeState& state =
      *reinterpret_cast<OffThreadPromiseRuntimeState*>(closure);
  MOZ_ASSERT(state.usingInternalDispatchQueue());
  gHelperThreadLock.assertOwnedByCurrentThread();

  if (state.internalDispatchQueueClosed_) {
    return false;
  }

  state.dispatchDelayedTasks();

  // endTime is calculated synchronously from the moment we call
  // internalDelayedDispatchToEventLoop. The only current use-case is
  // Atomics.waitAsync.
  mozilla::TimeStamp endTime = mozilla::TimeStamp::Now() +
                               mozilla::TimeDuration::FromMilliseconds(delay);
  if (!state.internalDelayedDispatchPriorityQueue().reserveOne()) {
    JS::Dispatchable::ReleaseFailedTask(std::move(d));
    return false;
  }

  state.internalDelayedDispatchPriorityQueue().infallibleInsert(
      DelayedDispatchable(std::move(d), endTime));

  return true;
}

void OffThreadPromiseRuntimeState::dispatchDelayedTasks() {
  MOZ_ASSERT(usingInternalDispatchQueue());
  gHelperThreadLock.assertOwnedByCurrentThread();

  if (internalDispatchQueueClosed_) {
    return;
  }

  auto& queue = internalDelayedDispatchPriorityQueue();

  if (queue.empty()) {
    return;
  }

  mozilla::TimeStamp now = mozilla::TimeStamp::Now();

  while (!queue.empty() && queue.highest().endTime() <= now) {
    DelayedDispatchable d(std::move(queue.highest()));
    queue.popHighest();

    AutoEnterOOMUnsafeRegion noOOM;
    numDelayed_++;
    if (!internalDispatchQueue().pushBack(d.dispatchable())) {
      noOOM.crash("dispatchDelayedTasks");
    }
    internalDispatchQueueAppended().notify_one();
  }
}

bool OffThreadPromiseRuntimeState::usingInternalDispatchQueue() const {
  return dispatchToEventLoopCallback_ == internalDispatchToEventLoop;
}

void OffThreadPromiseRuntimeState::initInternalDispatchQueue() {
  init(internalDispatchToEventLoop, internalDelayedDispatchToEventLoop, nullptr,
       nullptr, this);
  MOZ_ASSERT(usingInternalDispatchQueue());
}

bool OffThreadPromiseRuntimeState::initialized() const {
  return !!dispatchToEventLoopCallback_;
}

void OffThreadPromiseRuntimeState::internalDrain(JSContext* cx) {
  MOZ_ASSERT(usingInternalDispatchQueue());

  for (;;) {
    js::UniquePtr<JS::Dispatchable> d;
    {
      AutoLockHelperThreadState lock;
      dispatchDelayedTasks();

      MOZ_ASSERT(!internalDispatchQueueClosed_);
      MOZ_ASSERT_IF(!internalDispatchQueue().empty(),
                    numRegistered_ + numDelayed_ > 0);
      if (internalDispatchQueue().empty() && !internalHasPending(lock)) {
        return;
      }

      // There are extant live dispatched OffThreadPromiseTasks.
      // If none are in the queue, block until one of them finishes
      // and enqueues a dispatchable.
      while (internalDispatchQueue().empty()) {
        internalDispatchQueueAppended().wait(lock);
      }

      d = std::move(internalDispatchQueue().front());
      internalDispatchQueue().popFront();
      if (!d->registered()) {
        numDelayed_--;
      }
    }

    // Don't call Run() with lock held to avoid deadlock.
    OffThreadPromiseTask::Run(cx, std::move(d),
                              JS::Dispatchable::NotShuttingDown);
  }
}

bool OffThreadPromiseRuntimeState::internalHasPending() {
  AutoLockHelperThreadState lock;
  return internalHasPending(lock);
}

bool OffThreadPromiseRuntimeState::internalHasPending(
    AutoLockHelperThreadState& lock) {
  MOZ_ASSERT(usingInternalDispatchQueue());

  MOZ_ASSERT(!internalDispatchQueueClosed_);
  MOZ_ASSERT_IF(!internalDispatchQueue().empty(),
                numRegistered_ + numDelayed_ > 0);
  return numDelayed_ > 0 || numRegistered_ > cancellable().count();
}

void OffThreadPromiseRuntimeState::stealFailedTask(JS::Dispatchable* task) {
  js::AutoEnterOOMUnsafeRegion noOOM;
  if (!failed().pushBack(task)) {
    noOOM.crash("stealFailedTask");
  }
}

void OffThreadPromiseRuntimeState::cancelTasks(
    js::AutoLockHelperThreadState& lock, JSContext* cx) {
  MOZ_ASSERT(initialized());
  if (!initialized()) {
    return;
  }

  // Cancel all undispatched tasks that are cancellable.
  for (auto iter = cancellable().modIter(); !iter.done(); iter.next()) {
    OffThreadPromiseTask* task = iter.get();
    MOZ_ASSERT(task->cancellable_);
    iter.remove();

    OffThreadPromiseTask::DestroyUndispatchedTask(task, *this, lock);
  }
}

void OffThreadPromiseRuntimeState::cancelTasks(JSContext* cx) {
  if (!initialized()) {
    return;
  }

  AutoLockHelperThreadState lock;
  cancelTasks(lock, cx);
}

void OffThreadPromiseRuntimeState::shutdown(JSContext* cx) {
  if (!initialized()) {
    return;
  }

  AutoLockHelperThreadState lock;

  // Cancel all undispatched tasks.
  cancelTasks(lock, cx);
  MOZ_ASSERT(cancellable().empty());

  // When the shell is using the internal event loop, we must simulate our
  // requirement of the embedding that, before shutdown, all successfully-
  // dispatched-to-event-loop tasks have been run.
  if (usingInternalDispatchQueue()) {
    DispatchableFifo dispatchQueue;
    {
      std::swap(dispatchQueue, internalDispatchQueue());
      MOZ_ASSERT(internalDispatchQueue().empty());
      internalDispatchQueueClosed_ = true;
    }

    // Don't call run() with lock held to avoid deadlock.
    AutoUnlockHelperThreadState unlock(lock);
    while (!dispatchQueue.empty()) {
      js::UniquePtr<JS::Dispatchable> d = std::move(dispatchQueue.front());
      dispatchQueue.popFront();
      OffThreadPromiseTask::Run(cx, std::move(d),
                                JS::Dispatchable::ShuttingDown);
    }
  }

  // An OffThreadPromiseTask may only be safely deleted on its JSContext's
  // thread (since it contains a PersistentRooted holding its promise), and
  // only after it has called DispatchResolveAndDestroy (since that is our
  // only indication that its owner is done writing into it).
  //
  // OffThreadPromiseTasks accepted by the DispatchToEventLoopCallback are
  // deleted by their 'run' methods. Only DispatchResolveAndDestroy invokes
  // the callback, and the point of the callback is to call 'run' on the
  // JSContext's thread, so the conditions above are met.
  //
  // But although the embedding's DispatchToEventLoopCallback promises to run
  // every task it accepts before shutdown, when shutdown does begin it starts
  // rejecting tasks; we cannot count on 'run' to clean those up for us.
  // Instead, tasks which fail to run have their ownership passed to the failed_
  // list; once that count covers everything in numRegisterd_, this function
  // itself runs only on the JSContext's thread, so we can delete them all here.
  while (numRegistered_ != failed().length()) {
    MOZ_ASSERT(failed().length() < numRegistered_);
    allFailed().wait(lock);
  }

  {
    DispatchableFifo failedQueue;
    {
      std::swap(failedQueue, failed());
      MOZ_ASSERT(failed().empty());
    }

    AutoUnlockHelperThreadState unlock(lock);
    while (!failedQueue.empty()) {
      js::UniquePtr<JS::Dispatchable> d = std::move(failedQueue.front());
      failedQueue.popFront();
      js_delete(d.release());
    }
  }

  // Everything should be empty at this point.
  MOZ_ASSERT(numRegistered_ == 0);

  // After shutdown, there should be no OffThreadPromiseTask activity in this
  // JSRuntime. Revert to the !initialized() state to catch bugs.
  dispatchToEventLoopCallback_ = nullptr;
  MOZ_ASSERT(!initialized());
}

/* static */
js::PromiseObject* OffThreadPromiseTask::ExtractAndForget(
    OffThreadPromiseTask* task, const AutoLockHelperThreadState& lock) {
  OffThreadPromiseRuntimeState& state =
      task->runtime()->offThreadPromiseState.ref();
  MOZ_RELEASE_ASSERT(state.initialized());
  MOZ_ASSERT(task->registered_);
  js::PromiseObject* promise = task->promise_;
  // Call the unregister method manually with our provided lock, otherwise the
  // destructor will try to acquire it and fail.
  task->unregister(state, lock);
  // Now we can call the destructor.
  js_delete(task);
  return promise;
}
