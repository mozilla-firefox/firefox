/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_CycleCollectedJSContext_h
#define mozilla_CycleCollectedJSContext_h

#include <deque>

#include "mozilla/Attributes.h"
#include "mozilla/LinkedList.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/dom/AtomList.h"
#include "mozilla/dom/Promise.h"
#include "js/GCVector.h"
#include "js/Promise.h"

#include "nsCOMPtr.h"
#include "nsRefPtrHashtable.h"
#include "nsTArray.h"

class nsCycleCollectionNoteRootCallback;
class nsIRunnable;
class nsThread;

namespace mozilla {
class AutoSlowOperation;

class CycleCollectedJSContext;
class CycleCollectedJSRuntime;
class PromiseJobRunnable;

namespace dom {
class Exception;
class WorkerJSContext;
class WorkletJSContext;
}  // namespace dom

// Contains various stats about the cycle collection.
struct CycleCollectorResults {
  CycleCollectorResults() {
    // Initialize here so when we increment mNumSlices the first time we're
    // not using uninitialized memory.
    Init();
  }

  void Init() {
    mForcedGC = false;
    mSuspectedAtCCStart = 0;
    mMergedZones = false;
    mAnyManual = false;
    mVisitedRefCounted = 0;
    mVisitedGCed = 0;
    mFreedRefCounted = 0;
    mFreedGCed = 0;
    mFreedJSZones = 0;
    mNumSlices = 1;
    // mNumSlices is initialized to one, because we call Init() after the
    // per-slice increment of mNumSlices has already occurred.
  }

  bool mForcedGC;
  bool mMergedZones;
  // mAnyManual is true if any slice was manually triggered, and at shutdown.
  bool mAnyManual;
  uint32_t mSuspectedAtCCStart;
  uint32_t mVisitedRefCounted;
  uint32_t mVisitedGCed;
  uint32_t mFreedRefCounted;
  uint32_t mFreedGCed;
  uint32_t mFreedJSZones;
  uint32_t mNumSlices;
};

class MicroTaskRunnable : public LinkedListElement<MicroTaskRunnable> {
 public:
  MicroTaskRunnable() = default;
  NS_INLINE_DECL_REFCOUNTING(MicroTaskRunnable)
  MOZ_CAN_RUN_SCRIPT virtual void Run(AutoSlowOperation& aAso) = 0;
  virtual bool Suppressed() { return false; }
  virtual void TraceMicroTask(JSTracer* aTracer) {}

 protected:
  virtual ~MicroTaskRunnable() {
    if (isInList()) {
      remove();
    }
  }
};

// Store the suppressed mictotasks in another microtask so that operations
// for the microtask queue as a whole keep working.
class SuppressedMicroTasks : public MicroTaskRunnable {
 public:
  explicit SuppressedMicroTasks(CycleCollectedJSContext* aContext);

  MOZ_CAN_RUN_SCRIPT_BOUNDARY void Run(AutoSlowOperation& aAso) final {}
  virtual bool Suppressed();

  CycleCollectedJSContext* mContext;
  uint64_t mSuppressionGeneration;
  std::deque<RefPtr<MicroTaskRunnable>> mSuppressedMicroTaskRunnables;
};

// Support for JS FinalizationRegistry objects, which allow a JS callback to be
// registered that is called when objects die.
//
// We keep a vector of functions that call back into the JS engine along
// with their associated incumbent globals, one per FinalizationRegistry object
// that has pending cleanup work. These are run in their own task.
class FinalizationRegistryCleanup {
 public:
  explicit FinalizationRegistryCleanup(CycleCollectedJSContext* aContext);
  void Init();
  void Destroy();
  void QueueCallback(JSFunction* aDoCleanup, JSObject* aHostDefinedData);
  MOZ_CAN_RUN_SCRIPT void DoCleanup();

 private:
  static void QueueCallback(JSFunction* aDoCleanup, JSObject* aHostDefinedData,
                            void* aData);

  class CleanupRunnable;

  struct Callback {
    JSFunction* mCallbackFunction;
    JSObject* mIncumbentGlobal;
    void trace(JSTracer* trc);
  };

  // This object is part of CycleCollectedJSContext, so it's safe to have a raw
  // pointer to its containing context here.
  CycleCollectedJSContext* mContext;

  using CallbackVector = JS::GCVector<Callback, 0, InfallibleAllocPolicy>;
  JS::PersistentRooted<CallbackVector> mCallbacks;
};

class CycleCollectedJSContext : dom::PerThreadAtomCache, private JS::JobQueue {
  friend class CycleCollectedJSRuntime;
  friend class SuppressedMicroTasks;

 protected:
  CycleCollectedJSContext();
  virtual ~CycleCollectedJSContext();

  MOZ_IS_CLASS_INIT
  nsresult Initialize(JSRuntime* aParentRuntime, uint32_t aMaxBytes);

  virtual CycleCollectedJSRuntime* CreateRuntime(JSContext* aCx) = 0;

  size_t SizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf) const;

 private:
  static void PromiseRejectionTrackerCallback(
      JSContext* aCx, bool aMutedErrors, JS::Handle<JSObject*> aPromise,
      JS::PromiseRejectionHandlingState state, void* aData);

  void AfterProcessMicrotasks();

 public:
  void ProcessStableStateQueue();

 private:
  void CleanupIDBTransactions(uint32_t aRecursionDepth);

 public:
  virtual dom::WorkerJSContext* GetAsWorkerJSContext() { return nullptr; }
  virtual dom::WorkletJSContext* GetAsWorkletJSContext() { return nullptr; }

  CycleCollectedJSRuntime* Runtime() const {
    MOZ_ASSERT(mRuntime);
    return mRuntime;
  }

  already_AddRefed<dom::Exception> GetPendingException() const;
  void SetPendingException(dom::Exception* aException);

  std::deque<RefPtr<MicroTaskRunnable>>& GetMicroTaskQueue();
  std::deque<RefPtr<MicroTaskRunnable>>& GetDebuggerMicroTaskQueue();

  void TraceMicroTasks(JSTracer* aTracer);

  JSContext* Context() const {
    MOZ_ASSERT(mJSContext);
    return mJSContext;
  }

  JS::RootingContext* RootingCx() const {
    MOZ_ASSERT(mJSContext);
    return JS::RootingContext::get(mJSContext);
  }

  void SetTargetedMicroTaskRecursionDepth(uint32_t aDepth) {
    mTargetedMicroTaskRecursionDepth = aDepth;
  }

  void UpdateMicroTaskSuppressionGeneration() { ++mSuppressionGeneration; }

 protected:
  JSContext* MaybeContext() const { return mJSContext; }

 public:
  // nsThread entrypoints
  //
  // MOZ_CAN_RUN_SCRIPT_BOUNDARY so we don't need to annotate
  // nsThread::ProcessNextEvent and all its callers MOZ_CAN_RUN_SCRIPT for now.
  // But we really should!
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  virtual void BeforeProcessTask(bool aMightBlock);
  // MOZ_CAN_RUN_SCRIPT_BOUNDARY so we don't need to annotate
  // nsThread::ProcessNextEvent and all its callers MOZ_CAN_RUN_SCRIPT for now.
  // But we really should!
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  virtual void AfterProcessTask(uint32_t aRecursionDepth);

  // Check whether any eager thresholds have been reached, which would mean
  // an idle GC task (minor or major) would be useful.
  virtual void MaybePokeGC();

  uint32_t RecursionDepth() const;

  // Run in stable state (call through nsContentUtils)
  void RunInStableState(already_AddRefed<nsIRunnable>&& aRunnable);

  void AddPendingIDBTransaction(already_AddRefed<nsIRunnable>&& aTransaction);

  // Get the CycleCollectedJSContext for a JSContext.
  // Returns null only if Initialize() has not completed on or during
  // destruction of the CycleCollectedJSContext.
  static CycleCollectedJSContext* GetFor(JSContext* aCx);

  // Get the current thread's CycleCollectedJSContext.  Returns null if there
  // isn't one.
  static CycleCollectedJSContext* Get();

  // Queue an async microtask to the current main or worker thread.
  virtual void DispatchToMicroTask(
      already_AddRefed<MicroTaskRunnable> aRunnable);

  // Call EnterMicroTask when you're entering JS execution.
  // Usually the best way to do this is to use nsAutoMicroTask.
  void EnterMicroTask() { ++mMicroTaskLevel; }

  MOZ_CAN_RUN_SCRIPT
  void LeaveMicroTask() {
    if (--mMicroTaskLevel == 0) {
      PerformMicroTaskCheckPoint();
    }
  }

  uint32_t MicroTaskLevel() const { return mMicroTaskLevel; }

  void SetMicroTaskLevel(uint32_t aLevel) { mMicroTaskLevel = aLevel; }

  void EnterSyncOperation() { ++mSyncOperations; }
  void LeaveSyncOperation() { --mSyncOperations; }
  bool IsInSyncOperation() const { return mSyncOperations > 0; }

  MOZ_CAN_RUN_SCRIPT
  bool PerformMicroTaskCheckPoint(bool aForce = false);

  MOZ_CAN_RUN_SCRIPT
  void PerformDebuggerMicroTaskCheckpoint();

  bool IsInStableOrMetaStableState() const { return mDoingStableStates; }

  // Storage for watching rejected promises waiting for some client to
  // consume their rejection.
  // Promises in this list have been rejected in the last turn of the
  // event loop without the rejection being handled.
  // Note that this can contain nullptrs in place of promises removed because
  // they're consumed before it'd be reported.
  JS::PersistentRooted<JS::GCVector<JSObject*, 0, js::SystemAllocPolicy>>
      mUncaughtRejections;

  // Promises in this list have previously been reported as rejected
  // (because they were in the above list), but the rejection was handled
  // in the last turn of the event loop.
  JS::PersistentRooted<JS::GCVector<JSObject*, 0, js::SystemAllocPolicy>>
      mConsumedRejections;
  nsTArray<nsCOMPtr<nsISupports /* UncaughtRejectionObserver */>>
      mUncaughtRejectionObservers;

  virtual bool IsSystemCaller() const = 0;

  // Unused on main thread.  Used by AutoJSAPI on Worker and Worklet threads.
  virtual void ReportError(JSErrorReport* aReport,
                           JS::ConstUTF8CharsZ aToStringResult) {
    MOZ_ASSERT_UNREACHABLE("Not supported");
  }

  // These two functions control a special flag variable which lets us turn
  // tracing on and off from a thread other than this JSContext's main thread.
  // This is useful because we want to be able to start tracing many threads
  // all at once from the Gecko Profiler in Firefox.
  //
  // NOTE: the caller must ensure that this CycleCollectedJSContext is not
  // being destroyed when this is called. At the time of this API being added,
  // the only consumer is the Gecko Profiler, which guarantees this via a mutex
  // around unregistering the context, which always occurs before the context
  // is destroyed.
  void BeginExecutionTracingAsync();
  void EndExecutionTracingAsync();

 private:
  // JS::JobQueue implementation: see js/public/Promise.h.
  // SpiderMonkey uses some of these methods to enqueue promise resolution jobs.
  // Others protect the debuggee microtask queue from the debugger's
  // interruptions; see the comments on JS::AutoDebuggerJobQueueInterruption for
  // details.
  bool getHostDefinedData(JSContext* cx,
                          JS::MutableHandle<JSObject*> aData) const override;

  bool enqueuePromiseJob(JSContext* cx, JS::Handle<JSObject*> promise,
                         JS::Handle<JSObject*> job,
                         JS::Handle<JSObject*> allocationSite,
                         JS::Handle<JSObject*> hostDefinedData) override;
  // MOZ_CAN_RUN_SCRIPT_BOUNDARY for now so we don't have to change SpiderMonkey
  // headers.  The caller presumably knows this can run script (like everything
  // in SpiderMonkey!) and will deal.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  void runJobs(JSContext* cx) override;
  bool empty() const override;
  bool isDrainingStopped() const override { return false; }
  class SavedMicroTaskQueue;
  js::UniquePtr<SavedJobQueue> saveJobQueue(JSContext*) override;

 private:
  CycleCollectedJSRuntime* mRuntime;

  JSContext* mJSContext;

  nsCOMPtr<dom::Exception> mPendingException;
  nsThread* mOwningThread;  // Manual refcounting to avoid include hell.

  struct PendingIDBTransactionData {
    nsCOMPtr<nsIRunnable> mTransaction;
    uint32_t mRecursionDepth;
  };

  nsTArray<nsCOMPtr<nsIRunnable>> mStableStateEvents;
  nsTArray<PendingIDBTransactionData> mPendingIDBTransactions;
  uint32_t mBaseRecursionDepth;
  bool mDoingStableStates;

  // If set to none 0, microtasks will be processed only when recursion depth
  // is the set value.
  uint32_t mTargetedMicroTaskRecursionDepth;

  uint32_t mMicroTaskLevel;

  uint32_t mSyncOperations;

  std::deque<RefPtr<MicroTaskRunnable>> mPendingMicroTaskRunnables;
  std::deque<RefPtr<MicroTaskRunnable>> mDebuggerMicroTaskQueue;
  RefPtr<SuppressedMicroTasks> mSuppressedMicroTasks;
  uint64_t mSuppressionGeneration;

 protected:
  mozilla::LinkedList<MicroTaskRunnable> mMicrotasksToTrace;

 private:
  friend class PromiseJobRunnable;
  RefPtr<PromiseJobRunnable> mRecycledPromiseJob;

  // How many times the debugger has interrupted execution, possibly creating
  // microtask checkpoints in places that they would not normally occur.
  uint32_t mDebuggerRecursionDepth;

  Maybe<uint32_t> mMicroTaskRecursionDepth;

  // This implements about-to-be-notified rejected promises list in the spec.
  // https://html.spec.whatwg.org/multipage/webappapis.html#about-to-be-notified-rejected-promises-list
  typedef nsTArray<RefPtr<dom::Promise>> PromiseArray;
  PromiseArray mAboutToBeNotifiedRejectedPromises;

  // This is for the "outstanding rejected promises weak set" in the spec,
  // https://html.spec.whatwg.org/multipage/webappapis.html#outstanding-rejected-promises-weak-set
  // We use different data structure and opposite logic here to achieve the same
  // effect. Basically this is used for tracking the rejected promise that does
  // NOT need firing a rejectionhandled event. We will check the table to see if
  // firing rejectionhandled event is required when a rejected promise is being
  // handled.
  //
  // The rejected promise will be stored in the table if
  // - it is unhandled, and
  // - The unhandledrejection is not yet fired.
  //
  // And be removed when
  // - it is handled, or
  // - A unhandledrejection is fired and it isn't being handled in event
  // handler.
  typedef nsRefPtrHashtable<nsUint64HashKey, dom::Promise> PromiseHashtable;
  PromiseHashtable mPendingUnhandledRejections;

  class NotifyUnhandledRejections final : public CancelableRunnable {
   public:
    explicit NotifyUnhandledRejections(PromiseArray&& aPromises)
        : CancelableRunnable("NotifyUnhandledRejections"),
          mUnhandledRejections(std::move(aPromises)) {}

    NS_IMETHOD Run() final;

    nsresult Cancel() final;

   private:
    PromiseArray mUnhandledRejections;
  };

  FinalizationRegistryCleanup mFinalizationRegistryCleanup;
};

class MOZ_STACK_CLASS nsAutoMicroTask {
 public:
  nsAutoMicroTask() {
    CycleCollectedJSContext* ccjs = CycleCollectedJSContext::Get();
    if (ccjs) {
      ccjs->EnterMicroTask();
    }
  }
  MOZ_CAN_RUN_SCRIPT ~nsAutoMicroTask() {
    CycleCollectedJSContext* ccjs = CycleCollectedJSContext::Get();
    if (ccjs) {
      ccjs->LeaveMicroTask();
    }
  }
};

}  // namespace mozilla

#endif  // mozilla_CycleCollectedJSContext_h
