/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/CycleCollectedJSContext.h"

#include <algorithm>
#include <utility>

#include "js/Debug.h"
#include "js/GCAPI.h"
#include "js/Utility.h"
#include "js/friend/DumpFunctions.h"
#include "js/friend/MicroTask.h"
#include "jsapi.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/CycleCollectedJSRuntime.h"
#include "mozilla/DebuggerOnGCRunnable.h"
#include "mozilla/FlowMarkers.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/ProfilerRunnable.h"
#include "mozilla/Sprintf.h"
#include "mozilla/StaticPrefs_javascript.h"
#include "mozilla/dom/CallbackObject.h"
#include "mozilla/dom/DOMException.h"
#include "mozilla/dom/DOMJSClass.h"
#include "mozilla/dom/FinalizationRegistryBinding.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/PromiseDebugging.h"
#include "mozilla/dom/PromiseDebuggingBinding.h"
#include "mozilla/dom/PromiseRejectionEvent.h"
#include "mozilla/dom/PromiseRejectionEventBinding.h"
#include "mozilla/dom/RootedDictionary.h"
#include "mozilla/dom/ScriptSettings.h"
#include "mozilla/dom/UserActivation.h"
#include "mozilla/dom/WebTaskScheduler.h"
#include "nsContentUtils.h"
#include "nsCycleCollectionNoteRootCallback.h"
#include "nsCycleCollectionParticipant.h"
#include "nsCycleCollector.h"
#include "nsDOMJSUtils.h"
#include "nsDOMMutationObserver.h"
#include "nsJSUtils.h"
#include "nsPIDOMWindow.h"
#include "nsThread.h"
#include "nsThreadUtils.h"
#include "nsWrapperCache.h"
#include "xpcpublic.h"

using namespace mozilla;
using namespace mozilla::dom;

namespace mozilla {

CycleCollectedJSContext::CycleCollectedJSContext()
    : mRuntime(nullptr),
      mJSContext(nullptr),
      mDoingStableStates(false),
      mTargetedMicroTaskRecursionDepth(0),
      mMicroTaskLevel(0),
      mSyncOperations(0),
      mSuppressionGeneration(0),
      mDebuggerRecursionDepth(0),
      mFinalizationRegistryCleanup(this) {
  MOZ_COUNT_CTOR(CycleCollectedJSContext);

  nsCOMPtr<nsIThread> thread = do_GetCurrentThread();
  mOwningThread = thread.forget().downcast<nsThread>().take();
  MOZ_RELEASE_ASSERT(mOwningThread);
}

CycleCollectedJSContext::~CycleCollectedJSContext() {
  MOZ_COUNT_DTOR(CycleCollectedJSContext);
  MOZ_ASSERT(mWebTaskSchedulingStateCount == 0,
             "A global leaked a WebTaskSchedulingState, which would have "
             "permanently disabled the getHostDefinedData fast path");
  // If the allocation failed, here we are.
  if (!mJSContext) {
    return;
  }

  JS::SetHostCleanupFinalizationRegistryCallback(mJSContext, nullptr, nullptr);

  JS_SetContextPrivate(mJSContext, nullptr);

  MOZ_ASSERT(!JS::HasAnyMicroTasks(mJSContext));

  mRuntime->SetContext(nullptr);
  mRuntime->Shutdown(mJSContext);

  // Last chance to process any events.
  CleanupIDBTransactions(mBaseRecursionDepth);
  MOZ_ASSERT(mPendingIDBTransactions.IsEmpty());

  ProcessStableStateQueue();
  MOZ_ASSERT(mStableStateEvents.IsEmpty());

  // Clear mPendingException first, since it might be cycle collected.
  mPendingException = nullptr;

  mUncaughtRejections.reset();
  mUncaughtRejectionIndices.Clear();
  mConsumedRejections.reset();

  mAboutToBeNotifiedRejectedPromises.Clear();
  mPendingUnhandledRejections.Clear();
  mUncaughtRejectionObservers.Clear();

  mFinalizationRegistryCleanup.Destroy();

  JS_DestroyContext(mJSContext);
  mJSContext = nullptr;

  nsCycleCollector_forgetJSContext();

  mozilla::dom::DestroyScriptSettings();

  mOwningThread->SetScriptObserver(nullptr);
  NS_RELEASE(mOwningThread);

  delete mRuntime;
  mRuntime = nullptr;
}

nsresult CycleCollectedJSContext::Initialize(JSRuntime* aParentRuntime,
                                             uint32_t aMaxBytes) {
  MOZ_ASSERT(!mJSContext);

  mozilla::dom::InitScriptSettings();
  mJSContext = JS_NewContext(aMaxBytes, aParentRuntime);
  if (!mJSContext) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  mRuntime = CreateRuntime(mJSContext);
  mRuntime->SetContext(this);

  mOwningThread->SetScriptObserver(this);
  // The main thread has a base recursion depth of 0, workers of 1.
  mBaseRecursionDepth = RecursionDepth();

  NS_GetCurrentThread()->SetCanInvokeJS(true);

  JS::SetJobQueue(mJSContext, this);
  JS::SetPromiseRejectionTrackerCallback(mJSContext,
                                         PromiseRejectionTrackerCallback, this);
  mUncaughtRejections.init(mJSContext,
                           JS::GCVector<JSObject*, 0, js::SystemAllocPolicy>(
                               js::SystemAllocPolicy()));
  mConsumedRejections.init(mJSContext,
                           JS::GCVector<JSObject*, 0, js::SystemAllocPolicy>(
                               js::SystemAllocPolicy()));

  mFinalizationRegistryCleanup.Init();

  // Cast to PerThreadAtomCache for dom::GetAtomCache(JSContext*).
  JS_SetContextPrivate(mJSContext, static_cast<PerThreadAtomCache*>(this));

  nsCycleCollector_registerJSContext(this);

  return NS_OK;
}

/* static */
CycleCollectedJSContext* CycleCollectedJSContext::GetFor(JSContext* aCx) {
  // Cast from void* matching JS_SetContextPrivate.
  auto atomCache = static_cast<PerThreadAtomCache*>(JS_GetContextPrivate(aCx));
  // Down cast.
  return static_cast<CycleCollectedJSContext*>(atomCache);
}

size_t CycleCollectedJSContext::SizeOfExcludingThis(
    MallocSizeOf aMallocSizeOf) const {
  return 0;
}

void FinalizeSchedulingStateWrapper(JS::GCContext* aGCX, JSObject* aObjSelf) {
  nsISupports* schedulingState = JS::GetObjectISupports<nsISupports>(aObjSelf);
  if (!schedulingState) {
    return;
  }

  JS::SetObjectISupports(aObjSelf, nullptr);
  schedulingState->Release();
}

static const JSClassOps sSchedulingStateWrapper = {
    .finalize = FinalizeSchedulingStateWrapper,
};

// The only slot holds the WebTaskSchedulingState as an nsISupports, which is
// how the cycle collector sees the strong reference.
static const JSClass sSchedulingStateClass = {"SchedulingStateWrapper",
                                              JSCLASS_HAS_RESERVED_SLOTS(1) |
                                                  JSCLASS_SLOT0_IS_NSISUPPORTS |
                                                  JSCLASS_FOREGROUND_FINALIZE,
                                              &sSchedulingStateWrapper};

bool CycleCollectedJSContext::getHostDefinedGlobal(
    JSContext* aCx, JS::MutableHandle<JSObject*> out) const {
  nsIGlobalObject* global = mozilla::dom::GetIncumbentGlobal();
  if (!global) {
    return true;
  }

  out.set(global->GetGlobalJSObject());
  return true;
}

void CycleCollectedJSContext::traceNonGCThingMicroTask(JSTracer* trc,
                                                       JS::Value* valuePtr) {
  // This hook is called for non-JSObject microtask values.
  // In Gecko, the microtask queue should only contain JSObjects (JS microtasks)
  // or Private values (Gecko MicroTaskRunnables). Private values are
  // indistinguishable from doubles at the bit level, so if this hook is called,
  // we know it's not an object, and by design it must be a Private value
  // containing a MicroTaskRunnable pointer that was enqueued via
  // EnqueueMicroTask.

  MOZ_ASSERT(!valuePtr->isObject(),
             "This hook should only be called for non-objects");
  MOZ_ASSERT(!valuePtr->isNullOrUndefined(),
             "This hook should only be called with private values");
  if (void* ptr = valuePtr->toPrivate()) {
    // The pointer is a MicroTaskRunnable that may have GC-reachable data
    auto* runnable = static_cast<MicroTaskRunnable*>(ptr);
    runnable->TraceMicroTask(trc);
  }
}

bool CycleCollectedJSContext::getHostDefinedData(
    JSContext* aCx, JS::MutableHandle<JSObject*> aIncumbentGlobal,
    JS::MutableHandle<JSObject*> aOptionalHostDefinedData) const {
  aIncumbentGlobal.set(nullptr);
  aOptionalHostDefinedData.set(nullptr);

  if (!getHostDefinedGlobal(aCx, aIncumbentGlobal)) {
    return false;
  }

  if (!aIncumbentGlobal) {
    return true;
  }

  // A performance note: On promise heavy benchmarks the allocation of an
  // object can be heavy, which is why this is conditional on the existence
  // of schedulingState. Checking the count first avoids walking the script
  // settings stack (and the principal check in GetEntryGlobal) when no global
  // on this thread has a scheduling state at all.
  if (!MayHaveWebTaskSchedulingState()) {
    return true;
  }

  mozilla::dom::WebTaskSchedulingState* schedulingState =
      mozilla::dom::GetWebTaskSchedulingState();
  if (!schedulingState) {
    return true;
  }

  JS::Rooted<JSObject*> schedulingStateResult(
      aCx, JS_NewObjectWithGivenProto(aCx, &sSchedulingStateClass, nullptr));
  if (!schedulingStateResult) {
    aOptionalHostDefinedData.set(nullptr);
    aIncumbentGlobal.set(nullptr);
    return false;
  }

  // This ref will be removed by FinalizeSchedulingStateWrapper.
  schedulingState->AddRef();
  JS::SetObjectISupports(schedulingStateResult,
                         static_cast<nsISupports*>(schedulingState));
  aOptionalHostDefinedData.set(schedulingStateResult);

  return true;
}

// Used only by the SpiderMonkey Debugger API, and even then only via
// JS::AutoDebuggerJobQueueInterruption, to ensure that the debuggee's queue is
// not affected; see comments in js/public/Promise.h.
void CycleCollectedJSContext::runJobs(JSContext* aCx) {
  MOZ_ASSERT(aCx == Context());
  MOZ_ASSERT(Get() == this);
  PerformMicroTaskCheckPoint();
}

JS::JSMicroTaskRef AsJSMicroTask(JS::Handle<MayConsumeMicroTask> aMicroTask) {
  // Safe: aMicroTask is rooted, and its trace() traces mMicroTask.
  return aMicroTask.get().mMicroTask->asJS();
}

// Unwrap (without interacting with refcounting) a Gecko MicroTaskRunnable if
// the task is not a JS MicroTask (see MicroTask.h for "JS MicroTask");
// otherwise, return nullptr.
//
// This is a non-owning conversion: aMicroTask still owns the refcount.
MicroTaskRunnable* MaybeUnwrapTaskToRunnable(
    JS::Handle<MayConsumeMicroTask> aMicroTask) {
  if (aMicroTask.get().IsJSMicroTask()) {
    return nullptr;
  }

  void* nonJSTask = aMicroTask.get().mMicroTask->embedderPtr();
  return static_cast<MicroTaskRunnable*>(nonJSTask);
}

already_AddRefed<MicroTaskRunnable> MaybeConsumeAsOwnedRunnable(
    JS::MutableHandle<MustConsumeMicroTask> aMicroTask) {
  MOZ_ASSERT(!aMicroTask.get().IsConsumed(),
             "Attempting to consume an already-consumed task");
  MicroTaskRunnable* mtr = MaybeUnwrapTaskToRunnable(aMicroTask);
  if (!mtr) {
    return nullptr;
  }
  aMicroTask.get().markAsConsumed();
  return already_AddRefed(mtr);
}

bool RunAndConsumeJSMicroTask(
    JSContext* aCx, JS::MutableHandle<MustConsumeMicroTask> aMicroTask) {
  MOZ_ASSERT(!JS_IsExceptionPending(aCx));
  // Safe: aMicroTask is rooted, and its trace() traces mMicroTask.
  bool result = JS::RunJSMicroTask(
      aCx, JS::Handle<Maybe<JS::MicroTask>>::fromMarkedLocation(
               &aMicroTask.get().mMicroTask));
  aMicroTask.get().markAsConsumed();
  return result;
}

// Preserve a debuggee's microtask queue while it is interrupted by the
// debugger. See the comments for JS::AutoDebuggerJobQueueInterruption.
class CycleCollectedJSContext::SavedMicroTaskQueue
    : public JS::JobQueue::SavedJobQueue {
 public:
  explicit SavedMicroTaskQueue(CycleCollectedJSContext* ccjs) : ccjs(ccjs) {
    ccjs->mDebuggerRecursionDepth++;
    mSavedQueue = JS::SaveMicroTaskQueue(ccjs->Context());
  }

  ~SavedMicroTaskQueue() {
    // The JS Debugger attempts to maintain the invariant that microtasks which
    // occur durring debugger operation are completely flushed from the task
    // queue before returning control to the debuggee, in order to avoid
    // micro-tasks generated during debugging from interfering with regular
    // operation.
    //
    // While the vast majority of microtasks can be reliably flushed,
    // synchronous operations (see nsAutoSyncOperation) such as printing and
    // alert diaglogs suppress the execution of some microtasks.
    //
    // When PerformMicroTaskCheckpoint is run while microtasks are suppressed,
    // any suppressed microtasks are gathered into a new SuppressedMicroTasks
    // runnable, which is enqueued on exit from PerformMicroTaskCheckpoint. As a
    // result, AutoDebuggerJobQueueInterruption::runJobs is not able to
    // correctly guarantee that the microtask queue is totally empty in the
    // presence of sync operations.
    //
    // Previous versions of this code release-asserted that the queue was empty,
    // causing user observable crashes (Bug 1849675). To avoid this, we instead
    // choose to move suspended microtasks from the SavedMicroTaskQueue to the
    // main microtask queue in this destructor. This means that jobs enqueued
    // during synchnronous events under debugger control may produce events
    // which run outside the debugger, but this is viewed as strictly
    // preferrable to crashing.
    MOZ_RELEASE_ASSERT(ccjs->mDebuggerRecursionDepth);

    JSContext* cx = ccjs->Context();

    JS::Rooted<MustConsumeMicroTask> suppressedTasks(cx);
    MOZ_ASSERT(JS::GetRegularMicroTaskCount(cx) <= 1);
    if (JS::HasRegularMicroTasks(cx)) {
      suppressedTasks = DequeueNextRegularMicroTask(cx);
      MOZ_ASSERT(MaybeUnwrapTaskToRunnable(suppressedTasks) ==
                 ccjs->mSuppressedMicroTaskList);
    }
    MOZ_RELEASE_ASSERT(!JS::HasRegularMicroTasks(cx));
    JS::RestoreMicroTaskQueue(cx, std::move(mSavedQueue));

    if (suppressedTasks.get()) {
      if (!EnqueueMicroTask(cx,
                            MaybeConsumeAsOwnedRunnable(&suppressedTasks))) {
        NS_ABORT_OOM(0);
      }
    }

    ccjs->mDebuggerRecursionDepth--;
  }

 private:
  CycleCollectedJSContext* ccjs;
  std::deque<RefPtr<MicroTaskRunnable>> mQueue;
  js::UniquePtr<JS::SavedMicroTaskQueue> mSavedQueue;
};

js::UniquePtr<JS::JobQueue::SavedJobQueue>
CycleCollectedJSContext::saveJobQueue(JSContext* cx) {
  auto saved = js::MakeUnique<SavedMicroTaskQueue>(this);
  if (!saved) {
    // When std::make_unique's allocation fails, the SavedMicroTaskQueue
    // constructor is never called, so mPendingMicroTaskRunnables is still
    // initialized.
    JS_ReportOutOfMemory(cx);
    return nullptr;
  }

  return saved;
}

/* static */
void CycleCollectedJSContext::PromiseRejectionTrackerCallback(
    JSContext* aCx, bool aMutedErrors, JS::HandleObject aPromise,
    JS::PromiseRejectionHandlingState state, void* aData) {
  CycleCollectedJSContext* self = static_cast<CycleCollectedJSContext*>(aData);

  MOZ_ASSERT(aCx == self->Context());
  MOZ_ASSERT(Get() == self);

  // TODO: Bug 1549351 - Promise rejection event should not be sent for
  // cross-origin scripts

  PromiseArray& aboutToBeNotified = self->mAboutToBeNotifiedRejectedPromises;
  PromiseHashtable& unhandled = self->mPendingUnhandledRejections;
  uint64_t promiseID = JS::GetPromiseID(aPromise);

  if (state == JS::PromiseRejectionHandlingState::Unhandled) {
    PromiseDebugging::AddUncaughtRejection(aPromise, promiseID);
    if (!aMutedErrors) {
      RefPtr<Promise> promise =
          Promise::CreateFromExisting(xpc::NativeGlobal(aPromise), aPromise);
      size_t index = aboutToBeNotified.Length();
      aboutToBeNotified.AppendElement(promise);
      unhandled.InsertOrUpdate(promiseID,
                               PendingRejection{std::move(promise), index});
    }
  } else {
    PromiseDebugging::AddConsumedRejection(aPromise, promiseID);
    if (Maybe<PendingRejection> pending = unhandled.Extract(promiseID)) {
      // The stored index outlives the array whenever AfterProcessMicrotasks
      // hands it off, so only clear the slot if it still holds this promise.
      // To avoid large amounts of memmoves, we don't shrink the vector here.
      // Instead, we filter out nullptrs when iterating over the vector later.
      if (pending->mIndex < aboutToBeNotified.Length() &&
          aboutToBeNotified[pending->mIndex] == pending->mPromise) {
        aboutToBeNotified[pending->mIndex] = nullptr;
      }
      return;
    }
    if (!aMutedErrors) {
      nsIGlobalObject* global = xpc::NativeGlobal(aPromise);
      if (nsCOMPtr<EventTarget> owner = do_QueryInterface(global)) {
        RootedDictionary<PromiseRejectionEventInit> init(aCx);
        if (RefPtr<Promise> newPromise =
                Promise::CreateFromExisting(global, aPromise)) {
          init.mPromise = newPromise->PromiseObj();
        }
        init.mReason = JS::GetPromiseResult(aPromise);

        RefPtr<PromiseRejectionEvent> event =
            PromiseRejectionEvent::Constructor(owner, u"rejectionhandled"_ns,
                                               init);

        RefPtr asyncDispatcher =
            MakeRefPtr<AsyncEventDispatcher>(owner, event.forget());
        asyncDispatcher->PostDOMEvent();
      }
    }
  }
}

already_AddRefed<Exception> CycleCollectedJSContext::GetPendingException()
    const {
  MOZ_ASSERT(mJSContext);

  nsCOMPtr<Exception> out = mPendingException;
  return out.forget();
}

void CycleCollectedJSContext::SetPendingException(Exception* aException) {
  MOZ_ASSERT(mJSContext);
  mPendingException = aException;
}

void CycleCollectedJSContext::TraceMicroTasks(JSTracer* aTracer) {
  for (MicroTaskRunnable* mt : mMicrotasksToTrace) {
    mt->TraceMicroTask(aTracer);
  }
}

void CycleCollectedJSContext::ProcessStableStateQueue() {
  MOZ_ASSERT(mJSContext);
  MOZ_RELEASE_ASSERT(!mDoingStableStates);
  mDoingStableStates = true;

  // When run, one event can add another event to the mStableStateEvents, as
  // such you can't use iterators here.
  for (uint32_t i = 0; i < mStableStateEvents.Length(); ++i) {
    nsCOMPtr<nsIRunnable> event = std::move(mStableStateEvents[i]);
    AUTO_PROFILE_FOLLOWING_RUNNABLE(event);
    event->Run();
  }

  mStableStateEvents.Clear();
  mDoingStableStates = false;
}

void CycleCollectedJSContext::CleanupIDBTransactions(uint32_t aRecursionDepth) {
  MOZ_ASSERT(mJSContext);
  MOZ_RELEASE_ASSERT(!mDoingStableStates);
  mDoingStableStates = true;

  nsTArray<PendingIDBTransactionData> localQueue =
      std::move(mPendingIDBTransactions);

  localQueue.RemoveLastElements(
      localQueue.end() -
      std::remove_if(localQueue.begin(), localQueue.end(),
                     [aRecursionDepth](PendingIDBTransactionData& data) {
                       if (data.mRecursionDepth != aRecursionDepth) {
                         return false;
                       }

                       {
                         nsCOMPtr<nsIRunnable> transaction =
                             std::move(data.mTransaction);
                         transaction->Run();
                       }

                       return true;
                     }));

  // If mPendingIDBTransactions has events in it now, they were added from
  // something we called, so they belong at the end of the queue.
  localQueue.AppendElements(std::move(mPendingIDBTransactions));
  mPendingIDBTransactions = std::move(localQueue);
  mDoingStableStates = false;
}

void CycleCollectedJSContext::BeforeProcessTask(bool aMightBlock) {
  // If ProcessNextEvent was called during a microtask callback, we
  // must process any pending microtasks before blocking in the event loop,
  // otherwise we may deadlock until an event enters the queue later.
  if (aMightBlock && PerformMicroTaskCheckPoint()) {
    // If any microtask was processed, we post a dummy event in order to
    // force the ProcessNextEvent call not to block.  This is required
    // to support nested event loops implemented using a pattern like
    // "while (condition) thread.processNextEvent(true)", in case the
    // condition is triggered here by a Promise "then" callback.
    NS_DispatchToMainThread(new Runnable("BeforeProcessTask"));
  }
}

void CycleCollectedJSContext::AfterProcessTask(uint32_t aRecursionDepth) {
  MOZ_ASSERT(mJSContext);

  // See HTML 6.1.4.2 Processing model

  // Step 4.1: Execute microtasks.
  PerformMicroTaskCheckPoint();

  // Step 4.2 Execute any events that were waiting for a stable state.
  ProcessStableStateQueue();

  // This should be a fast test so that it won't affect the next task
  // processing.
  MaybePokeGC();

  mRuntime->FinalizeDeferredThings(CycleCollectedJSRuntime::FinalizeNow);
  nsCycleCollector_maybeDoDeferredDeletion();
}

void CycleCollectedJSContext::AfterProcessMicrotasks() {
  MOZ_ASSERT(mJSContext);
  // Notify unhandled promise rejections:
  // https://html.spec.whatwg.org/multipage/webappapis.html#notify-about-rejected-promises
  if (mAboutToBeNotifiedRejectedPromises.Length()) {
    RefPtr runnable = MakeRefPtr<NotifyUnhandledRejections>(
        std::move(mAboutToBeNotifiedRejectedPromises));
    NS_DispatchToCurrentThread(runnable);
  }
  // Cleanup Indexed Database transactions:
  // https://html.spec.whatwg.org/multipage/webappapis.html#perform-a-microtask-checkpoint
  CleanupIDBTransactions(RecursionDepth());

  // Clear kept alive objects in JS WeakRef.
  // https://whatpr.org/html/4571/webappapis.html#perform-a-microtask-checkpoint
  //
  // ECMAScript implementations are expected to call ClearKeptObjects when
  // a synchronous sequence of ECMAScript execution completes.
  //
  // https://tc39.es/proposal-weakrefs/#sec-clear-kept-objects
  JS::ClearKeptObjects(mJSContext);
}

void CycleCollectedJSContext::MaybePokeGC() {
  // Worker-compatible check to see if we want to do an idle-time minor
  // GC.
  class IdleTimeGCTaskRunnable : public mozilla::IdleRunnable {
   public:
    using mozilla::IdleRunnable::IdleRunnable;

   public:
    IdleTimeGCTaskRunnable() : IdleRunnable("IdleTimeGCTask") {}

    NS_IMETHOD Run() override {
      CycleCollectedJSRuntime* ccrt = CycleCollectedJSRuntime::Get();
      if (ccrt) {
        ccrt->RunIdleTimeGCTask();
      }
      return NS_OK;
    }
  };

  if (Runtime()->IsIdleGCTaskNeeded()) {
    nsCOMPtr<nsIRunnable> gc_task = new IdleTimeGCTaskRunnable();
    NS_DispatchToCurrentThreadQueue(gc_task.forget(), EventQueuePriority::Idle);
    Runtime()->SetPendingIdleGCTask();
  }
}

uint32_t CycleCollectedJSContext::RecursionDepth() const {
  // Debugger interruptions are included in the recursion depth so that debugger
  // microtask checkpoints do not run IDB transactions which were initiated
  // before the interruption.
  return mOwningThread->RecursionDepth() + mDebuggerRecursionDepth;
}

void CycleCollectedJSContext::RunInStableState(
    already_AddRefed<nsIRunnable> aRunnable) {
  MOZ_ASSERT(mJSContext);
  nsCOMPtr<nsIRunnable> runnable = std::move(aRunnable);
  PROFILER_MARKER("CycleCollectedJSContext::RunInStableState", OTHER, {},
                  FlowMarker, Flow::FromPointer(runnable.get()));
  mStableStateEvents.AppendElement(std::move(runnable));
}

void CycleCollectedJSContext::AddPendingIDBTransaction(
    already_AddRefed<nsIRunnable> aTransaction) {
  MOZ_ASSERT(mJSContext);

  PendingIDBTransactionData data;
  data.mTransaction = aTransaction;

  MOZ_ASSERT(mOwningThread);
  data.mRecursionDepth = RecursionDepth();

  // There must be an event running to get here.
#ifndef MOZ_WIDGET_COCOA
  MOZ_ASSERT(data.mRecursionDepth > mBaseRecursionDepth);
#else
  // XXX bug 1261143
  // Recursion depth should be greater than mBaseRecursionDepth,
  // or the runnable will stay in the queue forever.
  if (data.mRecursionDepth <= mBaseRecursionDepth) {
    data.mRecursionDepth = mBaseRecursionDepth + 1;
  }
#endif

  mPendingIDBTransactions.AppendElement(std::move(data));
}

// MicroTaskRunnables and the JS MicroTask Queue:
//
// The following describes our refcounting scheme:
//
// - A runnable wrapped in a JS::Value (RunnableToValue) is always created from
// an already_AddRefed (so has a positive refcount) and it holds onto that ref
// count until it is finally eventually unwrapped to an owning reference
// (MaybeUnwrapTaskToOwnedRunnable)
//
// - This means runnables in the queue have their refcounts stay above zero for
// the duration of the time they are in the queue.
JS::MicroTask RunnableToMicroTask(
    already_AddRefed<MicroTaskRunnable>& aRunnable) {
  auto* r = aRunnable.take();
  MOZ_ASSERT(r);
  return JS::MicroTask::FromEmbedderPtr(r);
}

bool EnqueueMicroTask(JSContext* aCx,
                      already_AddRefed<MicroTaskRunnable> aRunnable) {
  return JS::EnqueueMicroTask(aCx, RunnableToMicroTask(aRunnable));
}
bool EnqueueDebugMicroTask(JSContext* aCx,
                           already_AddRefed<MicroTaskRunnable> aRunnable) {
  return JS::EnqueueDebugMicroTask(aCx, RunnableToMicroTask(aRunnable));
}

void CycleCollectedJSContext::DispatchToMicroTask(
    already_AddRefed<MicroTaskRunnable> aRunnable) {
  RefPtr<MicroTaskRunnable> runnable(aRunnable);
  MOZ_ASSERT(NS_IsMainThread());

  JS::JobQueueMayNotBeEmpty(Context());
  PROFILER_MARKER_FLOW_ONLY("CycleCollectedJSContext::DispatchToMicroTask",
                            OTHER, {}, FlowMarker,
                            Flow::FromPointer(runnable.get()));

  LogMicroTaskRunnable::LogDispatch(runnable.get());
  if (!EnqueueMicroTask(Context(), runnable.forget())) {
    NS_ABORT_OOM(0);
  }
}

class AsyncMutationHandler final : public mozilla::Runnable {
 public:
  AsyncMutationHandler() : mozilla::Runnable("AsyncMutationHandler") {}

  // MOZ_CAN_RUN_SCRIPT_BOUNDARY until Runnable::Run is MOZ_CAN_RUN_SCRIPT.  See
  // bug 1535398.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  NS_IMETHOD Run() override {
    CycleCollectedJSContext* ccjs = CycleCollectedJSContext::Get();
    if (ccjs) {
      ccjs->PerformMicroTaskCheckPoint();
    }
    return NS_OK;
  }
};

LazyLogModule gLog("mtq");

SuppressedMicroTaskList::SuppressedMicroTaskList(
    CycleCollectedJSContext* aContext)
    : mContext(aContext),
      mSuppressionGeneration(aContext->mSuppressionGeneration),
      mSuppressedMicroTaskRunnables(aContext->Context(), aContext->Context()) {}

bool SuppressedMicroTaskList::Suppressed() {
  if (mSuppressionGeneration == mContext->mSuppressionGeneration) {
    return true;
  }

  MOZ_ASSERT(mContext->mSuppressedMicroTaskList == this);

  MOZ_LOG_FMT(gLog, LogLevel::Verbose, "Prepending %zu suppressed microtasks",
              mSuppressedMicroTaskRunnables.get().length());
  for (size_t i = mSuppressedMicroTaskRunnables.get().length(); i > 0; i--) {
    mSuppressedMicroTaskRunnables.get()[i - 1].ConsumeByPrependToQueue(
        mContext->Context());
  }

  mSuppressedMicroTaskRunnables.get().clear();

  mContext->mSuppressedMicroTaskList = nullptr;

  // Return false: We are -not- ourselves suppressed, so,
  // in PerformMicroTasks we will end up in the branch where
  // we can drop the final refcount.
  return false;
}

SuppressedMicroTaskList::~SuppressedMicroTaskList() {
  MOZ_ASSERT(mContext->mSuppressedMicroTaskList == nullptr);
  MOZ_ASSERT(mSuppressedMicroTaskRunnables.get().empty());
};

static void MOZ_CAN_RUN_SCRIPT
RunJSMicroTask(JSContext* aCx, CycleCollectedJSContext* aCCJS,
               JS::MutableHandle<MustConsumeMicroTask> aMicroTask,
               bool aHasSuppressedMicroTasks);

// Run a microtask. Handles both non-JS (enqueued MicroTaskRunnables) and JS
// microtasks.
static void MOZ_CAN_RUN_SCRIPT
RunMicroTask(JSContext* aCx, CycleCollectedJSContext* aCCJS,
             JS::MutableHandle<MustConsumeMicroTask> aMicroTask,
             bool aHasSuppressedMicroTasks) {
  LogMustConsumeMicroTask::Run log(&aMicroTask.get());

  if (RefPtr<MicroTaskRunnable> runnable =
          MaybeConsumeAsOwnedRunnable(aMicroTask)) {
    AUTO_PROFILER_TERMINATING_FLOW_MARKER_FLOW_ONLY(
        "RunMicroTaskRunnable", OTHER, Flow::FromPointer(runnable.get()));
    AutoSlowOperation aso;
    runnable->Run(aso);
    return;
  }

  RunJSMicroTask(aCx, aCCJS, aMicroTask, aHasSuppressedMicroTasks);
}

// Basically this is nsAutoMicroTask, however it takes CycleCollectedJSContext*
// as a parameter and caches it to avoid having to two TLS gets.
//
// This is safe because CycleCollectedJSContext::Get will not be unset
// until shutdown well after the last call to PerformMicroTaskCheckpoint.
//
// In debug builds this is asserted.
class MOZ_STACK_CLASS StatefulMicroTask {
 public:
  explicit StatefulMicroTask(CycleCollectedJSContext* aCCJS) : mCCJS(aCCJS) {
    MOZ_ASSERT(aCCJS);
    MOZ_ASSERT(aCCJS == CycleCollectedJSContext::Get());
    mWillPerform = mCCJS->EnterMicroTask();
  }

  MOZ_CAN_RUN_SCRIPT ~StatefulMicroTask() {
    MOZ_ASSERT(mCCJS == CycleCollectedJSContext::Get());

    mCCJS->LeaveMicroTask();
  }

  bool WillPerformMicroTaskCheckPoint() { return mWillPerform; }

 private:
  CycleCollectedJSContext* mCCJS;
  bool mWillPerform = false;
};

// Given a (possibly null) incumbent global JS object and
// optionalHostDefinedData extract the nsIGlobalObject native global and
// WebTaskSchedulingState.
void ExtractIncumbentAndSchedulingState(
    JS::Handle<JSObject*> aIncumbentGlobalObj,
    JS::Handle<JSObject*> aOptionalHostDefinedData,
    nsIGlobalObject*& aIncumbentGlobal,
    WebTaskSchedulingState*& aSchedulingState) {
  MOZ_ASSERT(!aIncumbentGlobal && !aSchedulingState);
  MOZ_ASSERT_IF(!aIncumbentGlobalObj, !aOptionalHostDefinedData);

  if (aIncumbentGlobalObj) {
    // hostDefinedData is only created when incumbent global exists.
    aIncumbentGlobal = xpc::NativeGlobal(aIncumbentGlobalObj);

    if (aOptionalHostDefinedData) {
      MOZ_ASSERT(JS::GetClass(aOptionalHostDefinedData) ==
                 &sSchedulingStateClass);
      aSchedulingState = static_cast<WebTaskSchedulingState*>(
          JS::GetObjectISupports<nsISupports>(aOptionalHostDefinedData));
    }
  }
}

static void MaybeGetFlowMarker(
    JS::Handle<MayConsumeMicroTask> aMicroTask,
    mozilla::Maybe<AutoProfilerTerminatingFlowMarkerFlowOnly>&
        aTerminatingMarker) {
  // Avoid the overhead of getFlowId in the common case
  // of not having the profiler enabled.
  if (profiler_is_active_and_unpaused() &&
      profiler_feature_active(ProfilerFeature::Flows)) {
    uint64_t flowId = 0;
    // This only returns false when the microtask won't run (dead wrapper) or
    // has no flow, so we can elide the marker if it does fail.
    if (AsJSMicroTask(aMicroTask).getFlowId(&flowId)) {
      aTerminatingMarker.emplace("RunMicroTask",
                                 mozilla::baseprofiler::category::OTHER,
                                 Flow::ProcessScoped(flowId));
    }
  }
}

// Extract the data required to run a task.
//
// Returns false if the task is in an unrunnable state.
static bool ExtractTaskData(
    JS::Handle<MayConsumeMicroTask> aMicroTask,
    JS::MutableHandle<JSObject*> aCallbackGlobal,
    JS::MutableHandle<JSObject*> aIncumbentGlobal,
    JS::MutableHandle<JSObject*> aOptionalHostDefinedData,
    JS::MutableHandle<JSObject*> aAllocStack) {
  aCallbackGlobal.set(AsJSMicroTask(aMicroTask).executionGlobal());
  if (!aCallbackGlobal) {
    return false;
  }

  // Don't run if we fail to unwrap the host defined data, as that
  // would indicate the target realm is gone.
  if (!AsJSMicroTask(aMicroTask)
           .maybeGetHostDefinedData(aIncumbentGlobal,
                                    aOptionalHostDefinedData)) {
    return false;
  }

  (void)AsJSMicroTask(aMicroTask).maybeGetAllocationSite(aAllocStack);
  return true;
}

// Return true if execution can proceed, or false if we cannot.
static bool CanRunJSCallback(nsIGlobalObject* aGlobalObject,
                             JSObject* aCallbackGlobal,
                             nsIGlobalObject* aIncumbentGlobal) {
  if (!aGlobalObject->CanRunJSMicroTask(aCallbackGlobal)) {
    return false;
  }

  if (!aGlobalObject->HasJSGlobal()) {
    return false;
  }

  if (aIncumbentGlobal && !aIncumbentGlobal->HasJSGlobal()) {
    return false;
  }

  return true;
}

static bool ShouldPropagateUserInputEventHandlingState(
    JS::Handle<MayConsumeMicroTask> aMicroTask) {
  JSObject* maybePromise = AsJSMicroTask(aMicroTask).maybeGetPromise();

  // User Input State propagation.
  auto state = maybePromise
                   ? JS::GetPromiseUserInputEventHandlingState(maybePromise)
                   : JS::PromiseUserInputEventHandlingState::DontCare;
  return state ==
         JS::PromiseUserInputEventHandlingState::HadUserInteractionAtCreation;
}

// Return true if semantically we can try to drain more microtasks from
// the queue without doing more task setup.
//
// Here we're concerned about the parts indepenendent of the next task;
// task specific checks happen later.
bool CanAttemptToDrainMoreMicroTasks(CycleCollectedJSContext* aCCJS,
                                     StatefulMicroTask& aSMT) {
  if (!aSMT.WillPerformMicroTaskCheckPoint()) {
    return true;
  }

  // Can't attempt to recycle setup if we'll actually start draining again
  // (n.b. this may be conservative)
  return !aCCJS->CheckRecursionDepth(aCCJS->RecursionDepth());
}

nsIGlobalObject* GetCheckedGlobalObject(JS::Handle<JSObject*> aCallbackGlobal,
                                        bool aIsMainThread,
                                        nsIGlobalObject* aIncumbentGlobal) {
  IgnoredErrorResult errorResult;
  nsIGlobalObject* globalObject = CallSetup::GetActiveGlobalObjectForCall(
      aCallbackGlobal, aIsMainThread, /*aIsJSImplementedWebIDL=*/false,
      errorResult);
  if (!globalObject) {
    return nullptr;
  }

  if (!CanRunJSCallback(globalObject, aCallbackGlobal, aIncumbentGlobal)) {
    return nullptr;
  }

  return globalObject;
}

/* static */
void RunJSMicroTask(JSContext* aCx, CycleCollectedJSContext* aCCJS,
                    JS::MutableHandle<MustConsumeMicroTask> aMicroTask,
                    bool aHasSuppressedMicroTasks) {
  // After this point, if we fail to run, we
  //
  // 1. Know we have JS microtask
  // 2. Can freely ignore it if we cannot execute it.
  //
  // Create a ScopeExit to handle this.
  auto ignoreMicroTasks = mozilla::MakeScopeExit(
      [&aMicroTask]() { aMicroTask.get().IgnoreJSMicroTask(); });

  JS::RootedTuple<JSObject*, JSObject*, JSObject*, JSObject*,
                  WontConsumeMicroTask, JSObject*, JSObject*, JSObject*,
                  JSObject*>
      roots(aCx);

  JS::RootedField<JSObject*, 0> callbackGlobal(roots);
  JS::RootedField<JSObject*, 1> incumbentGlobalObj(roots);
  JS::RootedField<JSObject*, 2> optionalHostDefinedData(roots);
  JS::RootedField<JSObject*, 3> allocStack(roots);

  if (!ExtractTaskData(aMicroTask, &callbackGlobal, &incumbentGlobalObj,
                       &optionalHostDefinedData, &allocStack)) {
    return;
  }

  // We may have an incumbent global to deal with.
  //
  // See https://github.com/whatwg/html/issues/11686 for discussion
  // around trying to deprecate this eventually.
  nsIGlobalObject* incumbentGlobal = nullptr;

  // Promises carry along a web-task scheduling state as well.
  WebTaskSchedulingState* schedulingState = nullptr;
  ExtractIncumbentAndSchedulingState(incumbentGlobalObj,
                                     optionalHostDefinedData, incumbentGlobal,
                                     schedulingState);

  const bool isMainThread = NS_IsMainThread();

  // We need to do EnterMicroTask and LeaveMicroTask (on all exit paths), so use
  // RAII class.
  StatefulMicroTask smt(aCCJS);

  {
    IgnoredErrorResult errorResult;
    nsIGlobalObject* globalObject =
        GetCheckedGlobalObject(callbackGlobal, isMainThread, incumbentGlobal);
    if (!globalObject) {
      return;
    }

    // At this point we will definitely consume the task, so we
    // no longer need the scope exit.
    ignoreMicroTasks.release();

    const char* reason = "promise callback";

    // SetupForExecution
    AutoAllowLegacyScriptExecution exemption;
    AutoEntryScript aes(globalObject, reason, isMainThread);

    Maybe<AutoIncumbentScript> autoIncumbentScript;
    if (incumbentGlobal) {
      autoIncumbentScript.emplace(incumbentGlobal);
    }

    MOZ_ASSERT(aCx == aes.cx());

    JSAutoRealm ar(aCx, callbackGlobal);

    Maybe<JS::AutoSetAsyncStackForNewCalls> asyncStackSetter;
    if (allocStack) {
      asyncStackSetter.emplace(aCx, allocStack, reason);
    }

    // Inform the profiler about the flow for this microtask.
    mozilla::Maybe<AutoProfilerTerminatingFlowMarkerFlowOnly> terminatingMarker;
    MaybeGetFlowMarker(aMicroTask, terminatingMarker);

    {
      mozilla::Maybe<AutoHandlingUserInputStatePusher> userInputStateSwitcher;
      // A new scope is used to make sure the UserInputState is reset before
      // potentially draining more microtasks.
      if (NS_IsMainThread()) {
        bool propagate = ShouldPropagateUserInputEventHandlingState(aMicroTask);
        userInputStateSwitcher.emplace(propagate);
      }

      if (incumbentGlobal) {
        // https://wicg.github.io/scheduling-apis/#sec-patches-html-hostcalljobcallback
        // 2. Set event loop’s current scheduling state to
        // callback.[[HostDefined]].[[SchedulingState]].
        incumbentGlobal->SetWebTaskSchedulingState(schedulingState);
      }

      // Note: We're dropping the return value on the floor here, however
      // cleanup and exception handling are done as part of the CallSetup
      // destructor if necessary.
      bool ret = RunAndConsumeJSMicroTask(aCx, aMicroTask);

      // (The step after step 7): Set event loop’s current scheduling
      // state to null
      if (incumbentGlobal) {
        incumbentGlobal->SetWebTaskSchedulingState(nullptr);
      }

      // If we failed to execute, we should not attempt to execute more
      // tasks without running cleanup.
      if (!ret) {
        return;
      }
    }

    // Note: It's quite costly to set up all the execution state, and there's
    // a common case where the next task is run in the same execution state.
    // To avoid setting it up again, we'll try to drain more if it's possible.
    if (!StaticPrefs::javascript_options_batch_microtask_execution()) {
      return;
    }

    if (!JS::HasAnyMicroTasks(aCx)) {
      return;
    }

    if (!CanAttemptToDrainMoreMicroTasks(aCCJS, smt)) {
      return;
    }

    do {
      JS::RootedField<WontConsumeMicroTask, 4> peekTask(roots,
                                                        PeekNextMicroTask(aCx));

      // We can only coalesce JS tasks.
      if (!peekTask.get().IsJSMicroTask()) {
        break;
      }

      JS::RootedField<JSObject*, 5> peekedCallbackGlobal(roots);
      JS::RootedField<JSObject*, 6> peekedIncumbentGlobalObj(roots);
      JS::RootedField<JSObject*, 7> peekedOptionalHostDefinedData(roots);
      JS::RootedField<JSObject*, 8> peekedAllocStack(roots);

      if (!ExtractTaskData(peekTask, &peekedCallbackGlobal,
                           &peekedIncumbentGlobalObj,
                           &peekedOptionalHostDefinedData, &peekedAllocStack)) {
        break;
      }

      // Change of global or alloc stack need to run setup again
      // (stack is conservative. We probably could make this work.)
      if (peekedCallbackGlobal != callbackGlobal ||
          peekedAllocStack != allocStack) {
        break;
      }

      nsIGlobalObject* peekedIncumbentGlobal = nullptr;
      WebTaskSchedulingState* peekedSchedulingState = nullptr;
      ExtractIncumbentAndSchedulingState(
          peekedIncumbentGlobalObj, peekedOptionalHostDefinedData,
          peekedIncumbentGlobal, peekedSchedulingState);

      // Change of global
      if (peekedIncumbentGlobal != incumbentGlobal) {
        break;
      }

      // Validate the global -- this also checks if JS execution continues to be
      // allowed.
      nsIGlobalObject* peekedGlobal = GetCheckedGlobalObject(
          peekedCallbackGlobal, isMainThread, peekedIncumbentGlobal);
      if (!peekedGlobal || peekedGlobal != globalObject) {
        break;
      }

      // Ok, we're good to run again.
      aMicroTask.set(DequeueNextMicroTask(aCx));

      // Notify the JS engine if the queue is now empty, enabling optimizations
      // like skipping await microtask creation for resolved promises.
      if (!JS::HasAnyMicroTasks(aCx) && !aHasSuppressedMicroTasks) {
        JS::JobQueueIsEmpty(aCx);
      }

      if (incumbentGlobal) {
        // https://wicg.github.io/scheduling-apis/#sec-patches-html-hostcalljobcallback
        // 2. Set event loop's current scheduling state to
        // callback.[[HostDefined]].[[SchedulingState]].
        incumbentGlobal->SetWebTaskSchedulingState(peekedSchedulingState);
      }

      mozilla::Maybe<AutoHandlingUserInputStatePusher> userInputStateSwitcher;
      if (NS_IsMainThread()) {
        bool propagate = ShouldPropagateUserInputEventHandlingState(aMicroTask);
        userInputStateSwitcher.emplace(propagate);
      }

      // If this task fails we need cleanup code, which is in AutoJSAPI's
      // destructor to run, so abort execution.
      bool ret = RunAndConsumeJSMicroTask(aCx, aMicroTask);

      // (The step after step 7): Set event loop’s current scheduling
      // state to null
      if (incumbentGlobal) {
        incumbentGlobal->SetWebTaskSchedulingState(nullptr);
      }

      if (!ret) {
        break;
      }

      MOZ_ASSERT(!JS_IsExceptionPending(aCx));
    } while (JS::HasAnyMicroTasks(aCx));
  }
}

MustConsumeMicroTask DequeueNextMicroTask(JSContext* aCx) {
  return MustConsumeMicroTask(JS::DequeueNextMicroTask(aCx));
}

MustConsumeMicroTask DequeueNextRegularMicroTask(JSContext* aCx) {
  return MustConsumeMicroTask(JS::DequeueNextRegularMicroTask(aCx));
}

MustConsumeMicroTask DequeueNextDebuggerMicroTask(JSContext* aCx) {
  return MustConsumeMicroTask(JS::DequeueNextDebuggerMicroTask(aCx));
}

WontConsumeMicroTask PeekNextMicroTask(JSContext* aCx) {
  return WontConsumeMicroTask(JS::PeekNextMicroTask(aCx));
}

static bool IsSuppressed(JS::Handle<MustConsumeMicroTask> aTask) {
  if (aTask.get().IsJSMicroTask()) {
    JSObject* jsGlobal = AsJSMicroTask(aTask).executionGlobal();
    if (!jsGlobal) {
      return false;
    }
    nsIGlobalObject* global = xpc::NativeGlobal(jsGlobal);
    return global && global->IsInSyncOperation();
  }

  MicroTaskRunnable* runnable = MaybeUnwrapTaskToRunnable(aTask);

  // If it's not a JS microtask, it must be a MicroTaskRunnable,
  // and so MaybeUnwrapTaskToRunnable must return non-null.
  MOZ_ASSERT(runnable, "Unexpected task type");

  return runnable->Suppressed();
}

bool CycleCollectedJSContext::CheckRecursionDepth(uint32_t aCurrentDepth,
                                                  bool aForce) {
  if (mMicroTaskRecursionDepth && *mMicroTaskRecursionDepth >= aCurrentDepth &&
      !aForce) {
    // We are already executing microtasks for the current recursion depth.
    return false;
  }

  return !(mTargetedMicroTaskRecursionDepth != 0 &&
           mTargetedMicroTaskRecursionDepth + mDebuggerRecursionDepth !=
               aCurrentDepth);
}

bool CycleCollectedJSContext::PerformMicroTaskCheckPoint(bool aForce) {
  MOZ_LOG_FMT(gLog, LogLevel::Verbose, "Called PerformMicroTaskCheckpoint");

  JSContext* cx = Context();

  // If we have no JSContext we are not capable of checking for
  // nor running microtasks, and so simply return false early here.
  if (!cx) {
    return false;
  }

  if (!JS::HasAnyMicroTasks(cx)) {
    // Nothing to do, return early.
    AfterProcessMicrotasks();
    return false;
  }

  uint32_t currentDepth = RecursionDepth();
  if (!CheckRecursionDepth(currentDepth, aForce)) {
    return false;
  }

  if (NS_IsMainThread() && !nsContentUtils::IsSafeToRunScript()) {
    // Special case for main thread where DOM mutations may happen when
    // it is not safe to run scripts.
    nsContentUtils::AddScriptRunner(MakeAndAddRef<AsyncMutationHandler>());
    return false;
  }

  mozilla::AutoRestore<Maybe<uint32_t>> restore(mMicroTaskRecursionDepth);
  mMicroTaskRecursionDepth = Some(currentDepth);

  AUTO_PROFILER_MARKER("Perform microtasks", JS);

  bool didProcess = false;
  AutoSlowOperation aso;

  // Make sure we don't leak tasks into the Gecko MicroTask queues.
  JS::Rooted<MustConsumeMicroTask> job(cx);
  while (JS::HasAnyMicroTasks(cx)) {
    job.set(DequeueNextMicroTask(cx));

    // To avoid us accidentally re-enqueing a SuppressionMicroTaskList in
    // itself, we determine here if the job is actually the suppression task
    // list.
    bool isSuppressionJob =
        mSuppressedMicroTaskList
            ? MaybeUnwrapTaskToRunnable(job) == mSuppressedMicroTaskList
            : false;
    if (isSuppressionJob) {
      mSuppressedMicroTaskList->mIsQueued = false;
    }

    // No need to check Suppressed if there aren't ongoing sync operations nor
    // pending mSuppressedMicroTasks.s
    if ((IsInSyncOperation() || mSuppressedMicroTaskList) &&
        IsSuppressed(job)) {
      // Microtasks in worker shall never be suppressed.
      // Otherwise, the micro tasks queue will be replaced later with
      // all suppressed tasks in mDebuggerMicroTaskQueue unexpectedly.
      MOZ_ASSERT(NS_IsMainThread());
      JS::JobQueueMayNotBeEmpty(Context());

      // To avoid re-enqueing a suppressed SuppressionMicroTaskList in itself.
      if (!isSuppressionJob) {
        if (!mSuppressedMicroTaskList) {
          mSuppressedMicroTaskList = new SuppressedMicroTaskList(this);
        }

        if (!mSuppressedMicroTaskList->mSuppressedMicroTaskRunnables.get()
                 .append(std::move(job.get()))) {
          NS_ABORT_OOM(0);
        }
      } else {
        // Consume the runnable & simultaneously drop a ref count.
        RefPtr<MicroTaskRunnable> refToDrop(MaybeConsumeAsOwnedRunnable(&job));
        MOZ_ASSERT(refToDrop);
      }
    } else {
      // MG:XXX: It's sort of too bad that we can't handle the JobQueueIsEmpty
      // note entirely within the JS engine, but in order to do that we'd need
      // to move the suppressed micro task handling inside and that's more
      // divergence than I would like.
      if (!JS::HasAnyMicroTasks(cx) && !mSuppressedMicroTaskList) {
        JS::JobQueueIsEmpty(Context());
      }
      didProcess = true;

      RunMicroTask(cx, this, &job, !!mSuppressedMicroTaskList);
    }
  }

  // Put back the suppressed microtasks so that they will be run later.
  // Note, it is possible that we end up keeping these suppressed tasks around
  // for some time, but no longer than spinning the event loop nestedly
  // (sync XHR, alert, etc.)
  if (mSuppressedMicroTaskList) {
    // Like everywhere else, do_AddRef when enqueing. Then the refcount in the
    // queue is 2; when ->Suppressed is called, mSuppressedMicroTaskList will
    // be nulled out, dropping the refcount to 1, then when the conversion to
    // owned happens, inside of RunMicroTask, the remaining ref will be
    // dropped, and the code will be cleaned up.
    //
    // The list must only be in the queue once: a stale duplicate would be
    // dequeued after a newer list replaced this one. This matters for nested
    // checkpoints run by AutoDebuggerJobQueueInterruption, where the list may
    // already be sitting in the saved queue.
    if (!mSuppressedMicroTaskList->mIsQueued) {
      if (!EnqueueMicroTask(cx, do_AddRef(mSuppressedMicroTaskList))) {
        MOZ_CRASH("Failed to re-enqueue suppressed microtask list");
      }
      mSuppressedMicroTaskList->mIsQueued = true;
    }
  }

  AfterProcessMicrotasks();

  return didProcess;
}

void CycleCollectedJSContext::PerformDebuggerMicroTaskCheckpoint() {
  // Don't do normal microtask handling checks here, since whoever is calling
  // this method is supposed to know what they are doing.

  JSContext* cx = Context();

  JS::Rooted<MustConsumeMicroTask> job(cx);
  while (JS::HasDebuggerMicroTasks(cx)) {
    job.set(DequeueNextDebuggerMicroTask(cx));

    // MG:XXX: Need to add a JS::JobQueueIsEmpty call here.

    RunMicroTask(cx, this, &job, false);
  }

  AfterProcessMicrotasks();
}

NS_IMETHODIMP CycleCollectedJSContext::NotifyUnhandledRejections::Run() {
  for (size_t i = 0; i < mUnhandledRejections.Length(); ++i) {
    CycleCollectedJSContext* cccx = CycleCollectedJSContext::Get();
    NS_ENSURE_STATE(cccx);

    RefPtr<Promise>& promise = mUnhandledRejections[i];
    if (!promise) {
      continue;
    }

    JS::RootingContext* cx = cccx->RootingCx();
    JS::RootedObject promiseObj(cx, promise->PromiseObj());
    MOZ_ASSERT(JS::IsPromiseObject(promiseObj));

    // Only fire unhandledrejection if the promise is still not handled;
    uint64_t promiseID = JS::GetPromiseID(promiseObj);
    bool defaultPrevented = false;
    if (!JS::GetPromiseIsHandled(promiseObj)) {
      if (nsCOMPtr<EventTarget> target =
              do_QueryInterface(promise->GetParentObject())) {
        RootedDictionary<PromiseRejectionEventInit> init(cx);
        init.mPromise = promiseObj;
        init.mReason = JS::GetPromiseResult(promiseObj);
        init.mCancelable = true;

        RefPtr<PromiseRejectionEvent> event =
            PromiseRejectionEvent::Constructor(target, u"unhandledrejection"_ns,
                                               init);
        target->DispatchEvent(*event);
        defaultPrevented = event->DefaultPrevented();
      }
    }

    cccx = CycleCollectedJSContext::Get();
    NS_ENSURE_STATE(cccx);

    // Notify observers only if still unhandled (matches old
    // FlushUncaughtRejectionsInternal behavior for observer consumers
    // like PromiseTestUtils). An observer returning true takes ownership of
    // the rejection and suppresses the console report, as on the
    // FlushUncaughtRejectionsInternal and Cancel() paths.
    bool suppressReporting = false;
    if (!JS::GetPromiseIsHandled(promiseObj)) {
      auto& observers = cccx->mUncaughtRejectionObservers;
      for (size_t j = 0; j < observers.Length(); ++j) {
        RefPtr<UncaughtRejectionObserver> obs =
            static_cast<UncaughtRejectionObserver*>(observers[j].get());
        if (obs->OnLeftUncaught(promiseObj, IgnoreErrors())) {
          suppressReporting = true;
        }
      }
    }

    // Report to console regardless of handled state — this matches the
    // pre-existing behavior where FlushRejections reported before handling
    // could occur. Only preventDefault() suppresses the console report.
    if (!defaultPrevented && !suppressReporting) {
      AutoJSAPI jsapi;
      jsapi.Init();
      JSAutoRealm ar(jsapi.cx(), promiseObj);
      Promise::ReportRejectedPromise(jsapi.cx(), promiseObj);
    }

    // Remove from the pending table. May already be removed if the
    // promise was handled between tracking and now.
    cccx->mPendingUnhandledRejections.Remove(promiseID);
  }
  return NS_OK;
}

nsresult CycleCollectedJSContext::NotifyUnhandledRejections::Cancel() {
  CycleCollectedJSContext* cccx = CycleCollectedJSContext::Get();
  NS_ENSURE_STATE(cccx);

  // The runnable was canceled, so the unhandledrejection event will never
  // fire. Report any still-unhandled rejections directly to observers and
  // the console, since the deferred path in FlushUncaughtRejectionsInternal
  // already skipped them.
  for (size_t i = 0; i < mUnhandledRejections.Length(); ++i) {
    RefPtr<Promise>& promise = mUnhandledRejections[i];
    if (!promise) {
      continue;
    }

    JS::RootedObject promiseObj(cccx->RootingCx(), promise->PromiseObj());

    if (!JS::GetPromiseIsHandled(promiseObj)) {
      bool suppressReporting = false;
      auto& observers = cccx->mUncaughtRejectionObservers;
      for (size_t j = 0; j < observers.Length(); ++j) {
        RefPtr<UncaughtRejectionObserver> obs =
            static_cast<UncaughtRejectionObserver*>(observers[j].get());
        if (obs->OnLeftUncaught(promiseObj, IgnoreErrors())) {
          suppressReporting = true;
        }
      }

      if (!suppressReporting) {
        AutoJSAPI jsapi;
        jsapi.Init();
        JSAutoRealm ar(jsapi.cx(), promiseObj);
        Promise::ReportRejectedPromise(jsapi.cx(), promiseObj);
      }
    }

    cccx->mPendingUnhandledRejections.Remove(JS::GetPromiseID(promiseObj));
  }
  return NS_OK;
}

#ifdef MOZ_EXECUTION_TRACING

void CycleCollectedJSContext::BeginExecutionTracingAsync() {
  mOwningThread->Dispatch(NS_NewRunnableFunction(
      "CycleCollectedJSContext::BeginExecutionTracingAsync", [] {
        CycleCollectedJSContext* ccjs = CycleCollectedJSContext::Get();
        if (ccjs) {
          JS_TracerBeginTracing(ccjs->Context());
        }
      }));
}

void CycleCollectedJSContext::EndExecutionTracingAsync() {
  mOwningThread->Dispatch(NS_NewRunnableFunction(
      "CycleCollectedJSContext::EndExecutionTracingAsync", [] {
        CycleCollectedJSContext* ccjs = CycleCollectedJSContext::Get();
        if (ccjs) {
          JS_TracerEndTracing(ccjs->Context());
        }
      }));
}

#else

void CycleCollectedJSContext::BeginExecutionTracingAsync() {}
void CycleCollectedJSContext::EndExecutionTracingAsync() {}

#endif

class FinalizationRegistryCleanup::CleanupRunnable
    : public DiscardableRunnable {
 public:
  explicit CleanupRunnable(FinalizationRegistryCleanup* aCleanupWork)
      : DiscardableRunnable("CleanupRunnable"), mCleanupWork(aCleanupWork) {
    MOZ_ASSERT(aCleanupWork);
  }

  virtual ~CleanupRunnable() {
    if (mCleanupWork) {
      clearPendingRunnable();
    }
  }

  // MOZ_CAN_RUN_SCRIPT_BOUNDARY until Runnable::Run is MOZ_CAN_RUN_SCRIPT.  See
  // bug 1535398.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  NS_IMETHOD Run() override {
    if (!mCleanupWork) {
      // The FinalizationRegistryCleanup has been destroyed.
      return NS_OK;
    }

    clearPendingRunnable();

    mCleanupWork->DoCleanup();
    mCleanupWork = nullptr;
    return NS_OK;
  }

  void clearPendingRunnable() {
    MOZ_ASSERT(mCleanupWork->mPendingRunnable == this);
    mCleanupWork->mPendingRunnable = nullptr;
  }

 private:
  FinalizationRegistryCleanup* mCleanupWork;
  friend class FinalizationRegistryCleanup;
};

FinalizationRegistryCleanup::FinalizationRegistryCleanup(
    CycleCollectedJSContext* aContext)
    : mContext(aContext) {}

void FinalizationRegistryCleanup::Destroy() {
  // This must happen before the CycleCollectedJSContext destructor calls
  // JS_DestroyContext().
  if (mPendingRunnable) {
    MOZ_ASSERT(mPendingRunnable->mCleanupWork == this);
    mPendingRunnable->mCleanupWork = nullptr;
  }
  mCallbacks.reset();
}

void FinalizationRegistryCleanup::Init() {
  JSContext* cx = mContext->Context();
  mCallbacks.init(cx);
  JS::SetHostCleanupFinalizationRegistryCallback(cx, QueueCallback, this);
}

/* static */
void FinalizationRegistryCleanup::QueueCallback(JSFunction* aDoCleanup,
                                                JSObject* aIncumbentGlobal,
                                                void* aData) {
  FinalizationRegistryCleanup* cleanup =
      static_cast<FinalizationRegistryCleanup*>(aData);
  cleanup->QueueCallback(aDoCleanup, aIncumbentGlobal);
}

void FinalizationRegistryCleanup::QueueCallback(JSFunction* aDoCleanup,
                                                JSObject* aIncumbentGlobal) {
  MOZ_ASSERT_IF(!mCallbacks.empty(), mPendingRunnable);

  MOZ_ALWAYS_TRUE(mCallbacks.append(Callback{aDoCleanup, aIncumbentGlobal}));

  if (!mPendingRunnable) {
    mPendingRunnable = new CleanupRunnable(this);
    NS_DispatchToCurrentThread(mPendingRunnable);
  }
}

void FinalizationRegistryCleanup::DoCleanup() {
  if (mCallbacks.empty()) {
    return;
  }

  JS::RootingContext* cx = mContext->RootingCx();

  JS::Rooted<CallbackVector> callbacks(cx);
  std::swap(callbacks.get(), mCallbacks.get());

  for (const Callback& callback : callbacks) {
    JS::AssertObjectIsNotGray(JS_GetFunctionObject(callback.mCallbackFunction));
    JS::AssertObjectIsNotGray(callback.mIncumbentGlobal);

    JS::RootedObject functionObj(
        cx, JS_GetFunctionObject(callback.mCallbackFunction));
    JS::RootedObject globalObj(cx, JS::GetNonCCWObjectGlobal(functionObj));

    nsIGlobalObject* incumbentGlobal =
        xpc::NativeGlobal(callback.mIncumbentGlobal);
    if (!incumbentGlobal) {
      continue;
    }

    RefPtr<FinalizationRegistryCleanupCallback> cleanupCallback(
        new FinalizationRegistryCleanupCallback(functionObj, globalObj, nullptr,
                                                incumbentGlobal));

    nsIGlobalObject* global =
        xpc::NativeGlobal(cleanupCallback->CallbackPreserveColor());
    if (global) {
      cleanupCallback->Call("FinalizationRegistryCleanup::DoCleanup");
    }
  }
}

void FinalizationRegistryCleanup::Callback::trace(JSTracer* trc) {
  JS::TraceRoot(trc, &mCallbackFunction, "mCallbackFunction");
  JS::TraceRoot(trc, &mIncumbentGlobal, "mIncumbentGlobal");
}

}  // namespace mozilla
