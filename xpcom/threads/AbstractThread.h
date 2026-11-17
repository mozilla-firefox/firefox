/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef XPCOM_THREADS_ABSTRACTTHREAD_H_
#define XPCOM_THREADS_ABSTRACTTHREAD_H_

#include "mozilla/AlreadyAddRefed.h"
#include "mozilla/DefineEnum.h"
#include "mozilla/RefPtr.h"
#include "mozilla/ThreadLocal.h"
#include "nsISerialEventTarget.h"
#include "nsISupports.h"
#include "nscore.h"

class nsIEventTarget;
class nsIRunnable;
class nsIThread;

namespace mozilla {

class TaskDispatcher;
class XPCOMThreadWrapper;

MOZ_DEFINE_ENUM_CLASS_WITH_BASE_AND_TOSTRING(
    TailDispatchPolicy, uint8_t,
    (
        // Don't use tail dispatch - all dispatches go straight to the
        // destination event target. This is the default.
        NoTailDispatch,
        // Use tail dispatch with consistent ordering between dispatched tasks.
        // If you're looking for tail dispatch, this is almost certainly what
        // you want.
        // As an example, dispatching to target A, then B, then A will result in
        // group runnables dispatching to A, then B, then A.
        // This policy is used when both source and destination targets use it.
        ConsistentOrdering,
        // Use tail dispatch with a guarantee that all runnables dispatched to a
        // single target in a single task will run atomically on the target.
        // As an example, dispatching to target A, then B, then A will result in
        // group runnables dispatching to A, then B. The order could be inverted
        // if something had dispatched to B at an earlier point without the tail
        // dispatcher firing in between. There are NO guarantees on ordering
        // between targets A and B. This policy is used when both source and
        // destination targets support tail dispatch, and *either* of them use
        // this policy.
        TargetAtomicity));

/*
 * We often want to run tasks on a target that guarantees that events will never
 * run in parallel. There are various target types that achieve this - namely
 * nsIThread and TaskQueue. Note that nsIThreadPool (which implements
 * nsIEventTarget) does not have this property, so we do not want to use
 * nsIEventTarget for this purpose. This class encapsulates the specifics of
 * the structures we might use here and provides a consistent interface.
 *
 * At present, the supported AbstractThread implementations are TaskQueue,
 * MediaTrackGraph for running tasks on an audio thread,
 * AbstractThread::MainThread() and XPCOMThreadWrapper which can wrap any
 * nsThread.
 *
 * The primary use of XPCOMThreadWrapper is to allow any threads to provide
 * Direct Task dispatching which is similar (but not identical to) the microtask
 * semantics of JS promises. Instantiating a XPCOMThreadWrapper on the current
 * nsThread is sufficient to enable direct task dispatching.
 *
 * You shouldn't use pointers when comparing AbstractThread or nsIThread to
 * determine if you are currently on the thread, but instead use the
 * nsISerialEventTarget::IsOnCurrentThread() method.
 */
class AbstractThread : public nsISerialEventTarget {
 public:
  // Returns the AbstractThread that the caller is currently running in, or null
  // if the caller is not running in an AbstractThread.
  static AbstractThread* GetCurrent() { return sCurrentThreadTLS.get(); }

  AbstractThread(TailDispatchPolicy aTailDispatchPolicy)
      : mTailDispatcherPolicy(aTailDispatchPolicy) {}

  // We don't use NS_DECL_NSIEVENTTARGET so that we can remove the default
  // |flags| parameter from Dispatch. Otherwise, a single-argument Dispatch call
  // would be ambiguous.
  using nsISerialEventTarget::IsOnCurrentThread;
  NS_IMETHOD_(bool) IsOnCurrentThreadInfallible(void) override;
  NS_IMETHOD IsOnCurrentThread(bool* _retval) override;
  NS_IMETHOD Dispatch(already_AddRefed<nsIRunnable> event,
                      DispatchFlags flags) override;
  NS_IMETHOD DispatchFromScript(nsIRunnable* event,
                                DispatchFlags flags) override;
  NS_IMETHOD DelayedDispatch(already_AddRefed<nsIRunnable> event,
                             uint32_t delay) override;

  enum DispatchReason { NormalDispatch, TailDispatch };
  virtual nsresult Dispatch(already_AddRefed<nsIRunnable> aRunnable,
                            DispatchReason aReason = NormalDispatch) = 0;

  virtual bool IsCurrentThreadIn() const = 0;

  // Returns a TaskDispatcher that will dispatch its tasks when the currently-
  // running tasks pops off the stack.
  //
  // May only be called when running within the it is invoked up, and only on
  // threads which support it.
  virtual TaskDispatcher& TailDispatcher() = 0;

  // Returns true if we have tail tasks scheduled, or if this isn't known.
  // Returns false if we definitely don't have any tail tasks.
  virtual bool MightHaveTailTasks() { return true; }

  // Returns true if the tail dispatcher is available. In certain edge cases
  // like shutdown, it might not be.
  virtual bool IsTailDispatcherAvailable() { return true; }

  // Helper functions for methods on the tail TasklDispatcher. These check
  // HasTailTasks to avoid allocating a TailDispatcher if it isn't
  // needed.
  nsresult TailDispatchTasksFor(AbstractThread* aThread);
  bool HasTailTasksFor(AbstractThread* aThread);

  // Returns true if this supports the tail dispatcher.
  bool SupportsTailDispatch() const {
    return mTailDispatcherPolicy != TailDispatchPolicy::NoTailDispatch;
  }

  // Returns the policy for tail-dispatch of this AbstractThread.
  TailDispatchPolicy TailDispatcherPolicy() const {
    return mTailDispatcherPolicy;
  }

  // Returns true if this thread requires all dispatches originating from
  // aThread go through the tail dispatcher.
  bool RequiresTailDispatch(AbstractThread* aThread) const;
  bool RequiresTailDispatchFromCurrentThread() const;

  virtual nsIEventTarget* AsEventTarget() { MOZ_CRASH("Not an event target!"); }

  // Returns the non-DocGroup version of AbstractThread on the main thread.
  // A DocGroup-versioned one is available in
  // DispatcherTrait::AbstractThreadFor(). Note:
  // DispatcherTrait::AbstractThreadFor() SHALL be used when possible.
  static AbstractThread* MainThread();

  // Must be called exactly once during startup.
  static void InitTLS();
  static void InitMainThread();
  static void ShutdownMainThread();

  void DispatchStateChange(already_AddRefed<nsIRunnable> aRunnable);

  static void DispatchDirectTask(already_AddRefed<nsIRunnable> aRunnable);

 protected:
  virtual ~AbstractThread() = default;
  static MOZ_THREAD_LOCAL(AbstractThread*) sCurrentThreadTLS;

  // Defines if we want tasks dispatched from tasks running in this
  // AbstractThread to go through our tail dispatcher, and if so, how they
  // should be grouped together for different event targets.
  const TailDispatchPolicy mTailDispatcherPolicy;
};

/**
 * Makes the current XPCOM thread an AbstractThread for the lifetime of this
 * object. AbstractThread::GetCurrent() returns the same instance as get() while
 * this is alive. Only one instance may exist per thread at a time.
 *
 * With a tail dispatch policy, tasks running on the XPCOM thread may use the
 * tail dispatcher whether or not they were dispatched through the wrapper.
 *
 * Shutdown tasks registered with the wrapper are registered with the XPCOM
 * thread directly, as they are with GetCurrentSerialEventTarget() from any task
 * running on the XPCOM thread. They run when the XPCOM thread shuts down, which
 * can be after this has gone away.
 *
 * On destruction the wrapper is unregistered from the XPCOM thread. The wrapper
 * may outlive this through other references, but from then on it is not the
 * current AbstractThread anywhere and dispatching to it fails.
 */
class AutoXPCOMThreadWrapper final {
 public:
  explicit AutoXPCOMThreadWrapper(TailDispatchPolicy aTailDispatchPolicy);
  ~AutoXPCOMThreadWrapper();

  AutoXPCOMThreadWrapper(const AutoXPCOMThreadWrapper&) = delete;
  AutoXPCOMThreadWrapper(AutoXPCOMThreadWrapper&&) = delete;
  AutoXPCOMThreadWrapper& operator=(const AutoXPCOMThreadWrapper&) = delete;
  AutoXPCOMThreadWrapper& operator=(AutoXPCOMThreadWrapper&&) = delete;

  AbstractThread* get() const;

 private:
  const RefPtr<XPCOMThreadWrapper> mWrapper;
};

}  // namespace mozilla

#endif
