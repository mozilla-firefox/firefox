/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_workers_workerrunnable_h__
#define mozilla_dom_workers_workerrunnable_h__

#include <cstdint>
#include <utility>
#include "MainThreadUtils.h"
#include "mozilla/Atomics.h"
#include "mozilla/RefPtr.h"
#include "mozilla/ThreadSafeWeakPtr.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/WorkerRef.h"
#include "mozilla/dom/WorkerStatus.h"
#include "mozilla/dom/quota/CheckedUnsafePtr.h"
#include "nsCOMPtr.h"
#include "nsIRunnable.h"
#include "nsISupports.h"
#include "nsStringFwd.h"
#include "nsThreadUtils.h"
#include "nscore.h"

struct JSContext;
class nsIEventTarget;
class nsIGlobalObject;

namespace mozilla {

class ErrorResult;

namespace dom {

class Worker;

class WorkerRunnable : public nsIRunnable
#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
    ,
                       public nsINamed
#endif
{
 protected:
#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
  const char* mName = nullptr;
#endif

 public:
  NS_DECL_THREADSAFE_ISUPPORTS
#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
  NS_DECL_NSINAMED
#endif

  virtual nsresult Cancel() = 0;

  // The return value is true if and only if both PreDispatch and
  // DispatchInternal return true.
  virtual bool Dispatch(WorkerPrivate* aWorkerPrivate);

  // True if this runnable is handled by running JavaScript in some global that
  // could possibly be a debuggee, and thus needs to be deferred when the target
  // is paused in the debugger, until the JavaScript invocation in progress has
  // run to completion. Examples are MessageEventRunnable and
  // ReportErrorRunnable. These runnables are segregated into separate
  // ThrottledEventQueues, which the debugger pauses.
  //
  // Note that debugger runnables do not fall in this category, since we don't
  // support debugging the debugger server at the moment.
  virtual bool IsDebuggeeRunnable() const { return false; }

  // True if this runnable needs to be dispatched to
  // WorkerPrivate::mControlEventTareget.
  virtual bool IsControlRunnable() const { return false; }

  // True if this runnable should be dispatched to the debugger queue,
  // and false otherwise.
  virtual bool IsDebuggerRunnable() const { return false; }

  static WorkerRunnable* FromRunnable(nsIRunnable* aRunnable);

 protected:
  explicit WorkerRunnable(const char* aName = "WorkerRunnable")
#ifdef DEBUG
      ;
#else
#  ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
      : mName(aName)
#  endif
  {
  }
#endif

  // This class is reference counted.
  virtual ~WorkerRunnable() = default;

  // Calling Run() directly is not supported. Just call Dispatch() and
  // WorkerRun() will be called on the correct thread automatically.
  NS_DECL_NSIRUNNABLE

  // By default asserts that Dispatch() is being called on the right thread
  virtual bool PreDispatch(WorkerPrivate* aWorkerPrivate) = 0;

  // By default asserts that Dispatch() is being called on the right thread
  virtual void PostDispatch(WorkerPrivate* aWorkerPrivate,
                            bool aDispatchResult) = 0;

  virtual bool DispatchInternal(WorkerPrivate* aWorkerPrivate) = 0;

  // May be implemented by subclasses if desired if they need to do some sort of
  // setup before we try to set up our JSContext and compartment for real.
  // Typically the only thing that should go in here is creation of the worker's
  // global.
  //
  // If false is returned, WorkerRun will not be called at all.  PostRun will
  // still be called, with false passed for aRunResult.
  virtual bool PreRun(WorkerPrivate* aWorkerPrivate) = 0;

  // Must be implemented by subclasses. Called on the target thread.  The return
  // value will be passed to PostRun().  The JSContext passed in here comes from
  // an AutoJSAPI (or AutoEntryScript) that we set up on the stack.
  //
  // If the runnable is for parent thread, aCx is in the compartment of
  // mWorkerPrivate's reflector (i.e. the worker object in the parent thread),
  // unless that reflector is null, in which case it's in the compartment of the
  // parent global (which is the compartment reflector would have been in), or
  // in the null compartment if there is no parent global.
  //
  // For runnables on the worker thread, aCx is in whatever
  // compartment GetCurrentWorkerThreadJSContext() was in when
  // nsIRunnable::Run() got called.  This is actually important for cases when a
  // runnable spins a syncloop and wants everything that happens during the
  // syncloop to happen in the compartment that runnable set up (which may, for
  // example, be a debugger sandbox compartment!).  If aCx wasn't in a
  // compartment to start with, aCx will be in either the debugger global's
  // compartment or the worker's global's compartment depending on whether
  // IsDebuggerRunnable() is true.
  //
  // Immediately after WorkerRun returns, the caller will assert that either it
  // returns false or there is no exception pending on aCx.  Then it will report
  // any pending exceptions on aCx.
  virtual bool WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) = 0;

  // By default asserts that Run() (and WorkerRun()) were called on the correct
  // thread.
  //
  // The aCx passed here is the same one as was passed to WorkerRun and is
  // still in the same compartment.  PostRun implementations must NOT leave an
  // exception on the JSContext and must not run script, because the incoming
  // JSContext may be in the null compartment.
  virtual void PostRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate,
                       bool aRunResult) = 0;
};

class WorkerParentThreadRunnable : public WorkerRunnable {
 public:
  NS_INLINE_DECL_REFCOUNTING_INHERITED(WorkerParentThreadRunnable,
                                       WorkerRunnable)

  virtual nsresult Cancel() override;

 protected:
  explicit WorkerParentThreadRunnable(
      const char* aName = "WorkerParentThreadRunnable");

  // This class is reference counted.
  virtual ~WorkerParentThreadRunnable();

  virtual bool PreDispatch(WorkerPrivate* aWorkerPrivate) override;

  virtual void PostDispatch(WorkerPrivate* aWorkerPrivate,
                            bool aDispatchResult) override;

  virtual bool PreRun(WorkerPrivate* aWorkerPrivate) override;

  virtual bool WorkerRun(JSContext* aCx,
                         WorkerPrivate* aWorkerPrivate) override = 0;

  virtual void PostRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate,
                       bool aRunResult) override;

  virtual bool DispatchInternal(WorkerPrivate* aWorkerPrivate) final;

  // Calling Run() directly is not supported. Just call Dispatch() and
  // WorkerRun() will be called on the correct thread automatically.
  NS_DECL_NSIRUNNABLE

 private:
  RefPtr<WorkerParentRef> mWorkerParentRef;
};

class WorkerParentControlRunnable : public WorkerParentThreadRunnable {
  friend class WorkerPrivate;

 protected:
  explicit WorkerParentControlRunnable(
      const char* aName = "WorkerParentControlRunnable");

  virtual ~WorkerParentControlRunnable();

  nsresult Cancel() override;

 public:
  NS_INLINE_DECL_REFCOUNTING_INHERITED(WorkerParentControlRunnable,
                                       WorkerParentThreadRunnable)

 private:
  bool IsControlRunnable() const override { return true; }

  // Should only be called by WorkerPrivate::DoRunLoop.
  using WorkerParentThreadRunnable::Cancel;
};

class WorkerParentDebuggeeRunnable : public WorkerParentThreadRunnable {
 protected:
  explicit WorkerParentDebuggeeRunnable(
      const char* aName = "WorkerParentDebuggeeRunnable")
      : WorkerParentThreadRunnable(aName) {}

 private:
  // This override is deliberately private: it doesn't make sense to call it if
  // we know statically that we are a WorkerDebuggeeRunnable.
  bool IsDebuggeeRunnable() const override { return true; }
};

class WorkerThreadRunnable : public WorkerRunnable {
  friend class WorkerPrivate;

 public:
  NS_INLINE_DECL_REFCOUNTING_INHERITED(WorkerThreadRunnable, WorkerRunnable)

  virtual nsresult Cancel() override;

 protected:
  explicit WorkerThreadRunnable(const char* aName = "WorkerThreadRunnable");

  // This class is reference counted.
  virtual ~WorkerThreadRunnable() = default;

  nsIGlobalObject* DefaultGlobalObject(WorkerPrivate* aWorkerPrivate) const;

  virtual bool PreDispatch(WorkerPrivate* aWorkerPrivate) override;

  virtual void PostDispatch(WorkerPrivate* aWorkerPrivate,
                            bool aDispatchResult) override;

  virtual bool PreRun(WorkerPrivate* aWorkerPrivate) override;

  virtual bool WorkerRun(JSContext* aCx,
                         WorkerPrivate* aWorkerPrivate) override = 0;

  virtual void PostRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate,
                       bool aRunResult) override;

  virtual bool DispatchInternal(WorkerPrivate* aWorkerPrivate) override;

  // Calling Run() directly is not supported. Just call Dispatch() and
  // WorkerRun() will be called on the correct thread automatically.
  NS_DECL_NSIRUNNABLE

  // Whether or not Cancel() is currently being called from inside the Run()
  // method. Avoids infinite recursion when a subclass calls Run() from inside
  // Cancel(). Only checked and modified on the target thread.
  bool mCallingCancelWithinRun;

  bool mCleanPreStartDispatching{false};
};

// This runnable is used to send a message to a worker debugger.
class WorkerDebuggerRunnable : public WorkerThreadRunnable {
 protected:
  explicit WorkerDebuggerRunnable(const char* aName = "WorkerDebuggerRunnable")
      : WorkerThreadRunnable(aName) {}

  virtual ~WorkerDebuggerRunnable() = default;

 private:
  virtual bool IsDebuggerRunnable() const override { return true; }

  virtual bool PreDispatch(WorkerPrivate* aWorkerPrivate) override {
    AssertIsOnMainThread();

    return true;
  }

  virtual void PostDispatch(WorkerPrivate* aWorkerPrivate,
                            bool aDispatchResult) override;
};

// This runnable is used to send a message directly to a worker's sync loop.
class WorkerSyncRunnable : public WorkerThreadRunnable {
 protected:
  nsCOMPtr<nsIEventTarget> mSyncLoopTarget;

  // Passing null for aSyncLoopTarget is allowed and will result in the behavior
  // of a normal WorkerThreadRunnable.
  explicit WorkerSyncRunnable(nsIEventTarget* aSyncLoopTarget,
                              const char* aName = "WorkerSyncRunnable");

  explicit WorkerSyncRunnable(nsCOMPtr<nsIEventTarget>&& aSyncLoopTarget,
                              const char* aName = "WorkerSyncRunnable");

  virtual ~WorkerSyncRunnable();

  virtual bool DispatchInternal(WorkerPrivate* aWorkerPrivate) override;
};

// This runnable is identical to WorkerSyncRunnable except it is meant to be
// created on and dispatched from the main thread only.  Its WorkerRun/PostRun
// will run on the worker thread.
class MainThreadWorkerSyncRunnable : public WorkerSyncRunnable {
 protected:
  // Passing null for aSyncLoopTarget is allowed and will result in the behavior
  // of a normal WorkerThreadRunnable.
  explicit MainThreadWorkerSyncRunnable(
      nsIEventTarget* aSyncLoopTarget,
      const char* aName = "MainThreadWorkerSyncRunnable")
      : WorkerSyncRunnable(aSyncLoopTarget, aName) {
    AssertIsOnMainThread();
  }

  explicit MainThreadWorkerSyncRunnable(
      nsCOMPtr<nsIEventTarget>&& aSyncLoopTarget,
      const char* aName = "MainThreadWorkerSyncRunnable")
      : WorkerSyncRunnable(std::move(aSyncLoopTarget), aName) {
    AssertIsOnMainThread();
  }

  virtual ~MainThreadWorkerSyncRunnable() = default;

 private:
  virtual bool PreDispatch(WorkerPrivate* aWorkerPrivate) override {
    AssertIsOnMainThread();
    return true;
  }

  virtual void PostDispatch(WorkerPrivate* aWorkerPrivate,
                            bool aDispatchResult) override;
};

// This runnable is processed as soon as it is received by the worker,
// potentially running before previously queued runnables and perhaps even with
// other JS code executing on the stack. These runnables must not alter the
// state of the JS runtime and should only twiddle state values.
class WorkerControlRunnable : public WorkerThreadRunnable {
  friend class WorkerPrivate;

 protected:
  explicit WorkerControlRunnable(const char* aName = "WorkerControlRunnable");

  virtual ~WorkerControlRunnable() = default;

  nsresult Cancel() override;

 public:
  NS_INLINE_DECL_REFCOUNTING_INHERITED(WorkerControlRunnable,
                                       WorkerThreadRunnable)

 private:
  bool IsControlRunnable() const override { return true; }

  // Should only be called by WorkerPrivate::DoRunLoop.
  using WorkerThreadRunnable::Cancel;
};

// A convenience class for WorkerThreadRunnables that are originated on the main
// thread.
class MainThreadWorkerRunnable : public WorkerThreadRunnable {
 protected:
  explicit MainThreadWorkerRunnable(
      const char* aName = "MainThreadWorkerRunnable")
      : WorkerThreadRunnable(aName) {
    AssertIsOnMainThread();
  }

  virtual ~MainThreadWorkerRunnable() = default;

  virtual bool PreDispatch(WorkerPrivate* aWorkerPrivate) override {
    AssertIsOnMainThread();
    return true;
  }

  virtual void PostDispatch(WorkerPrivate* aWorkerPrivate,
                            bool aDispatchResult) override {
    AssertIsOnMainThread();
  }
};

// A convenience class for WorkerControlRunnables that originate on the main
// thread.
class MainThreadWorkerControlRunnable : public WorkerControlRunnable {
 protected:
  explicit MainThreadWorkerControlRunnable(
      const char* aName = "MainThreadWorkerControlRunnable")
      : WorkerControlRunnable(aName) {}

  virtual ~MainThreadWorkerControlRunnable() = default;

  virtual bool PreDispatch(WorkerPrivate* aWorkerPrivate) override {
    AssertIsOnMainThread();
    return true;
  }

  virtual void PostDispatch(WorkerPrivate* aWorkerPrivate,
                            bool aDispatchResult) override {
    AssertIsOnMainThread();
  }
};

// A WorkerThreadRunnable that should be dispatched from the worker to itself
// for async tasks.
//
// Async tasks will almost always want to use this since
// a WorkerSameThreadRunnable keeps the Worker from being GCed.
class WorkerSameThreadRunnable : public WorkerThreadRunnable {
 protected:
  explicit WorkerSameThreadRunnable(
      const char* aName = "WorkerSameThreadRunnable")
      : WorkerThreadRunnable(aName) {}

  virtual ~WorkerSameThreadRunnable() = default;

  virtual bool PreDispatch(WorkerPrivate* aWorkerPrivate) override;

  virtual void PostDispatch(WorkerPrivate* aWorkerPrivate,
                            bool aDispatchResult) override;

  // We just delegate PostRun to WorkerThreadRunnable, since it does exactly
  // what we want.
};

// Base class for the runnable objects, which makes a synchronous call to
// dispatch the tasks from the worker thread to the main thread.
//
// Note that the derived class must override MainThreadRun.
class WorkerMainThreadRunnable : public Runnable {
 protected:
  RefPtr<ThreadSafeWorkerRef> mWorkerRef;
  nsCOMPtr<nsISerialEventTarget> mSyncLoopTarget;
  const nsCString mTelemetryKey;
  const char* mName;

  explicit WorkerMainThreadRunnable(
      WorkerPrivate* aWorkerPrivate, const nsACString& aTelemetryKey,
      const char* aName = "WorkerMainThreadRunnable");

  ~WorkerMainThreadRunnable();

  virtual bool MainThreadRun() = 0;

 public:
  // Dispatch the runnable to the main thread.  If dispatch to main thread
  // fails, or if the worker is in a state equal or greater of aFailStatus, an
  // error will be reported on aRv. Normally you want to use 'Canceling' for
  // aFailStatus, except if you want an infallible runnable. In this case, use
  // 'Killing'.
  // In that case the error MUST be propagated out to script.
  void Dispatch(WorkerPrivate* aWorkerPrivate, WorkerStatus aFailStatus,
                ErrorResult& aRv);

 private:
  NS_IMETHOD Run() override;
};

// This runnable is an helper class for dispatching something from a worker
// thread to the main-thread and back to the worker-thread. During this
// operation, this class will keep the worker alive.
// The purpose of RunBackOnWorkerThreadForCleanup() must be used, as the name
// says, only to release resources, no JS has to be executed, no timers, or
// other things. The reason of such limitations is that, in order to execute
// this method in any condition (also when the worker is shutting down), a
// Control Runnable is used, and, this could generate a reordering of existing
// runnables.
class WorkerProxyToMainThreadRunnable : public Runnable {
 protected:
  WorkerProxyToMainThreadRunnable();

  virtual ~WorkerProxyToMainThreadRunnable();

  // First this method is called on the main-thread.
  virtual void RunOnMainThread(WorkerPrivate* aWorkerPrivate) = 0;

  // After this second method is called on the worker-thread.
  virtual void RunBackOnWorkerThreadForCleanup(
      WorkerPrivate* aWorkerPrivate) = 0;

 public:
  bool Dispatch(WorkerPrivate* aWorkerPrivate);

  virtual bool ForMessaging() const { return false; }

 private:
  NS_IMETHOD Run() override;

  void PostDispatchOnMainThread();

  void ReleaseWorker();

  RefPtr<ThreadSafeWorkerRef> mWorkerRef;
};

// This runnable is used to stop a sync loop and it's meant to be used on the
// main-thread only.
class MainThreadStopSyncLoopRunnable : public WorkerSyncRunnable {
  nsresult mResult;

 public:
  // Passing null for aSyncLoopTarget is not allowed.
  MainThreadStopSyncLoopRunnable(nsCOMPtr<nsIEventTarget>&& aSyncLoopTarget,
                                 nsresult aResult);

  // By default StopSyncLoopRunnables cannot be canceled since they could leave
  // a sync loop spinning forever.
  nsresult Cancel() override;

 protected:
  virtual ~MainThreadStopSyncLoopRunnable() = default;

 private:
  bool PreDispatch(WorkerPrivate* aWorkerPrivate) final {
    AssertIsOnMainThread();
    return true;
  }

  virtual void PostDispatch(WorkerPrivate* aWorkerPrivate,
                            bool aDispatchResult) override;

  virtual bool WorkerRun(JSContext* aCx,
                         WorkerPrivate* aWorkerPrivate) override;

  bool DispatchInternal(WorkerPrivate* aWorkerPrivate) final;
};

// Runnables handled by content JavaScript (MessageEventRunnable, JavaScript
// error reports, and so on) must not be delivered while that content is in the
// midst of being debugged; the debuggee must be allowed to complete its current
// JavaScript invocation and return to its own event loop. Only then is it
// prepared for messages sent from the worker.
//
// Runnables that need to be deferred in this way should inherit from this
// class. They will be routed to mMainThreadDebuggeeEventTarget, which is paused
// while the window is suspended, as it is whenever the debugger spins its
// nested event loop. When the debugger leaves its nested event loop, it resumes
// the window, so that mMainThreadDebuggeeEventTarget will resume delivering
// runnables from the worker when control returns to the main event loop.
//
// When a page enters the bfcache, it freezes all its workers. Since a frozen
// worker processes only control runnables, it doesn't take any special
// consideration to prevent WorkerDebuggeeRunnables sent from child to parent
// workers from running; they'll never run anyway. But WorkerDebuggeeRunnables
// from a top-level frozen worker to its parent window must not be delivered
// either, even as the main thread event loop continues to spin. Thus, freezing
// a top-level worker also pauses mMainThreadDebuggeeEventTarget.
class WorkerDebuggeeRunnable : public WorkerThreadRunnable {
 protected:
  explicit WorkerDebuggeeRunnable(const char* aName = "WorkerDebuggeeRunnable")
      : WorkerThreadRunnable(aName) {}

 private:
  // This override is deliberately private: it doesn't make sense to call it if
  // we know statically that we are a WorkerDebuggeeRunnable.
  bool IsDebuggeeRunnable() const override { return true; }
};

}  // namespace dom
}  // namespace mozilla

#endif  // mozilla_dom_workers_workerrunnable_h__
