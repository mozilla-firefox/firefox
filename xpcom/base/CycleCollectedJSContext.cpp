/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/CycleCollectedJSContext.h"

#include <algorithm>
#include <utility>

#include "js/Debug.h"
#include "js/GCAPI.h"
#include "js/Utility.h"
#include "jsapi.h"
#include "mozilla/ArrayUtils.h"
#include "mozilla/AsyncEventDispatcher.h"
#include "mozilla/AutoRestore.h"
#include "mozilla/CycleCollectedJSRuntime.h"
#include "mozilla/DebuggerOnGCRunnable.h"
#include "mozilla/FlowMarkers.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/ProfilerRunnable.h"
#include "mozilla/Sprintf.h"
#include "mozilla/Unused.h"
#include "mozilla/dom/DOMException.h"
#include "mozilla/dom/DOMJSClass.h"
#include "mozilla/dom/FinalizationRegistryBinding.h"
#include "mozilla/dom/CallbackObject.h"
#include "mozilla/dom/PromiseDebugging.h"
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
  // If the allocation failed, here we are.
  if (!mJSContext) {
    return;
  }
  mRecycledPromiseJob = nullptr;

  JS::SetHostCleanupFinalizationRegistryCallback(mJSContext, nullptr, nullptr);

  JS_SetContextPrivate(mJSContext, nullptr);

  mRuntime->SetContext(nullptr);
  mRuntime->Shutdown(mJSContext);

  // Last chance to process any events.
  CleanupIDBTransactions(mBaseRecursionDepth);
  MOZ_ASSERT(mPendingIDBTransactions.IsEmpty());

  ProcessStableStateQueue();
  MOZ_ASSERT(mStableStateEvents.IsEmpty());

  // Clear mPendingException first, since it might be cycle collected.
  mPendingException = nullptr;

  MOZ_ASSERT(mDebuggerMicroTaskQueue.empty());
  MOZ_ASSERT(mPendingMicroTaskRunnables.empty());

  mUncaughtRejections.reset();
  mConsumedRejections.reset();

  mAboutToBeNotifiedRejectedPromises.Clear();
  mPendingUnhandledRejections.Clear();

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

class PromiseJobRunnable final : public CallbackObjectBase,
                                 public MicroTaskRunnable {
 public:
  PromiseJobRunnable(JS::HandleObject aPromise, JS::HandleObject aCallback,
                     JS::HandleObject aCallbackGlobal,
                     JS::HandleObject aAllocationSite,
                     nsIGlobalObject* aIncumbentGlobal,
                     WebTaskSchedulingState* aSchedulingState)
      : CallbackObjectBase(aCallback, aCallbackGlobal, aAllocationSite,
                           aIncumbentGlobal),
        mPropagateUserInputEventHandling(false) {
    MOZ_ASSERT(js::IsFunctionObject(aCallback));
    InitInternal(aPromise, aSchedulingState);
  }

  void Reinit(JS::HandleObject aPromise, JS::HandleObject aCallback,
              JS::HandleObject aCallbackGlobal,
              JS::HandleObject aAllocationSite,
              nsIGlobalObject* aIncumbentGlobal,
              WebTaskSchedulingState* aSchedulingState) {
    InitNoHold(aCallback, aCallbackGlobal, aAllocationSite, aIncumbentGlobal);
    InitInternal(aPromise, aSchedulingState);
  }

 protected:
  virtual ~PromiseJobRunnable() = default;

  // This is modeled on the Call methods which WebIDL codegen creates for
  // callback PromiseJobCallback = undefined();
  MOZ_CAN_RUN_SCRIPT inline void Call() {
    IgnoredErrorResult rv;
    CallSetup s(this, rv, "promise callback", eReportExceptions);
    if (!s.GetContext()) {
      MOZ_ASSERT(rv.Failed());
      return;
    }
    JS::Rooted<JS::Value> rval(s.GetContext());

    JS::Rooted<JS::Value> callable(s.GetContext(), JS::ObjectValue(*mCallback));
    if (!JS::Call(s.GetContext(), JS::UndefinedHandleValue, callable,
                  JS::HandleValueArray::empty(), &rval)) {
      // This isn't really needed but it ensures that rv's value is updated
      // consistently.
      rv.NoteJSContextException(s.GetContext());
    }
  }

  MOZ_CAN_RUN_SCRIPT
  virtual void Run(AutoSlowOperation& aAso) override {
    JSObject* callback = CallbackPreserveColor();
    nsCOMPtr<nsIGlobalObject> global =
        callback ? xpc::NativeGlobal(callback) : nullptr;
    if (global && !global->IsDying()) {
      // Propagate the user input event handling bit if needed.
      AutoHandlingUserInputStatePusher userInpStatePusher(
          mPropagateUserInputEventHandling);

      // https://wicg.github.io/scheduling-apis/#sec-patches-html-hostcalljobcallback
      // 2. Set event loop’s current scheduling state to
      // callback.[[HostDefined]].[[SchedulingState]].
      global->SetWebTaskSchedulingState(mSchedulingState);

      Call();

      // (The step after step 7): Set event loop’s current scheduling state to
      // null
      global->SetWebTaskSchedulingState(nullptr);
    }
    // Now that PromiseJobCallback is no longer needed, clear any pointers it
    // contains. This removes any storebuffer entries associated with those
    // pointers, which can cause problems by taking up memory and by triggering
    // minor GCs. This otherwise would not happen until the next minor GC or
    // cycle collection.
    Reset();
    // Clear also other explicit member variables of PromiseJobRunnable so that
    // we can possibly reuse it.
    mSchedulingState = nullptr;
    mPropagateUserInputEventHandling = false;

    if (CycleCollectedJSContext* ccjs = CycleCollectedJSContext::Get()) {
      ccjs->mRecycledPromiseJob = this;
    }
  }

  virtual bool Suppressed() override {
    JSObject* callback = CallbackPreserveColor();
    nsIGlobalObject* global = callback ? xpc::NativeGlobal(callback) : nullptr;
    return global && global->IsInSyncOperation();
  }

  void TraceMicroTask(JSTracer* aTracer) override {
    // We can trace CallbackObjectBase.
    Trace(aTracer);
  }

 private:
  void InitInternal(JS::HandleObject aPromise,
                    WebTaskSchedulingState* aSchedulingState) {
    if (aPromise) {
      JS::PromiseUserInputEventHandlingState state =
          JS::GetPromiseUserInputEventHandlingState(aPromise);
      mPropagateUserInputEventHandling =
          state ==
          JS::PromiseUserInputEventHandlingState::HadUserInteractionAtCreation;
    }
    mSchedulingState = aSchedulingState;
  }

  RefPtr<WebTaskSchedulingState> mSchedulingState;
  bool mPropagateUserInputEventHandling;
};

enum { INCUMBENT_SETTING_SLOT, SCHEDULING_STATE_SLOT, HOSTDEFINED_DATA_SLOTS };

// Finalizer for instances of HostDefinedData.
void FinalizeHostDefinedData(JS::GCContext* gcx, JSObject* objSelf) {
  JS::Value slotEvent = JS::GetReservedSlot(objSelf, SCHEDULING_STATE_SLOT);
  if (slotEvent.isUndefined()) {
    return;
  }

  WebTaskSchedulingState* schedulingState =
      static_cast<WebTaskSchedulingState*>(slotEvent.toPrivate());
  JS_SetReservedSlot(objSelf, SCHEDULING_STATE_SLOT, JS::UndefinedValue());
  schedulingState->Release();
}

static const JSClassOps sHostDefinedData = {
    nullptr /* addProperty */, nullptr /* delProperty */,
    nullptr /* enumerate */,   nullptr /* newEnumerate */,
    nullptr /* resolve */,     nullptr /* mayResolve */,
    FinalizeHostDefinedData /* finalize */
};

// Implements `HostDefined` in https://html.spec.whatwg.org/#hostmakejobcallback
static const JSClass sHostDefinedDataClass = {
    "HostDefinedData",
    JSCLASS_HAS_RESERVED_SLOTS(HOSTDEFINED_DATA_SLOTS) |
        JSCLASS_FOREGROUND_FINALIZE,
    &sHostDefinedData};

bool CycleCollectedJSContext::getHostDefinedData(
    JSContext* aCx, JS::MutableHandle<JSObject*> aData) const {
  nsIGlobalObject* global = mozilla::dom::GetIncumbentGlobal();
  if (!global) {
    aData.set(nullptr);
    return true;
  }

  JS::Rooted<JSObject*> incumbentGlobal(aCx, global->GetGlobalJSObject());

  if (!incumbentGlobal) {
    aData.set(nullptr);
    return true;
  }

  JSAutoRealm ar(aCx, incumbentGlobal);

  JS::Rooted<JSObject*> objResult(aCx,
                                  JS_NewObject(aCx, &sHostDefinedDataClass));
  if (!objResult) {
    aData.set(nullptr);
    return false;
  }

  JS_SetReservedSlot(objResult, INCUMBENT_SETTING_SLOT,
                     JS::ObjectValue(*incumbentGlobal));

  if (mozilla::dom::WebTaskSchedulingState* schedulingState =
          mozilla::dom::GetWebTaskSchedulingState()) {
    schedulingState->AddRef();
    JS_SetReservedSlot(objResult, SCHEDULING_STATE_SLOT,
                       JS::PrivateValue(schedulingState));
  }

  aData.set(objResult);

  return true;
}

bool CycleCollectedJSContext::enqueuePromiseJob(
    JSContext* aCx, JS::HandleObject aPromise, JS::HandleObject aJob,
    JS::HandleObject aAllocationSite, JS::HandleObject hostDefinedData) {
  MOZ_ASSERT(aCx == Context());
  MOZ_ASSERT(Get() == this);

  nsIGlobalObject* global = nullptr;
  WebTaskSchedulingState* schedulingState = nullptr;

  if (hostDefinedData) {
    MOZ_RELEASE_ASSERT(JS::GetClass(hostDefinedData.get()) ==
                       &sHostDefinedDataClass);
    JS::Value incumbentGlobal =
        JS::GetReservedSlot(hostDefinedData.get(), INCUMBENT_SETTING_SLOT);
    // hostDefinedData is only created when incumbent global exists.
    MOZ_ASSERT(incumbentGlobal.isObject());
    global = xpc::NativeGlobal(&incumbentGlobal.toObject());

    JS::Value state =
        JS::GetReservedSlot(hostDefinedData.get(), SCHEDULING_STATE_SLOT);
    if (!state.isUndefined()) {
      schedulingState = static_cast<WebTaskSchedulingState*>(state.toPrivate());
    }
  }

  JS::RootedObject jobGlobal(aCx, JS::CurrentGlobalOrNull(aCx));
  RefPtr<PromiseJobRunnable> runnable;
  if (mRecycledPromiseJob) {
    runnable = mRecycledPromiseJob.forget();
    runnable->Reinit(aPromise, aJob, jobGlobal, aAllocationSite, global,
                     schedulingState);
  } else {
    runnable = new PromiseJobRunnable(aPromise, aJob, jobGlobal,
                                      aAllocationSite, global, schedulingState);
  }
  DispatchToMicroTask(runnable.forget());
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

bool CycleCollectedJSContext::empty() const {
  // This is our override of JS::JobQueue::empty. Since that interface is only
  // concerned with the ordinary microtask queue, not the debugger microtask
  // queue, we only report on the former.
  return mPendingMicroTaskRunnables.empty();
}

// Preserve a debuggee's microtask queue while it is interrupted by the
// debugger. See the comments for JS::AutoDebuggerJobQueueInterruption.
class CycleCollectedJSContext::SavedMicroTaskQueue
    : public JS::JobQueue::SavedJobQueue {
 public:
  explicit SavedMicroTaskQueue(CycleCollectedJSContext* ccjs) : ccjs(ccjs) {
    ccjs->mDebuggerRecursionDepth++;
    ccjs->mPendingMicroTaskRunnables.swap(mQueue);
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
    MOZ_RELEASE_ASSERT(ccjs->mPendingMicroTaskRunnables.size() <= 1);
    MOZ_RELEASE_ASSERT(ccjs->mDebuggerRecursionDepth);
    RefPtr<MicroTaskRunnable> maybeSuppressedTasks;

    // Handle the case where there is a SuppressedMicroTask still in the queue.
    if (!ccjs->mPendingMicroTaskRunnables.empty()) {
      maybeSuppressedTasks = ccjs->mPendingMicroTaskRunnables.front();
      ccjs->mPendingMicroTaskRunnables.pop_front();
    }

    MOZ_RELEASE_ASSERT(ccjs->mPendingMicroTaskRunnables.empty());
    ccjs->mDebuggerRecursionDepth--;
    ccjs->mPendingMicroTaskRunnables.swap(mQueue);

    // Re-enqueue the suppressed task now that we've put the original microtask
    // queue back.
    if (maybeSuppressedTasks) {
      ccjs->mPendingMicroTaskRunnables.push_back(maybeSuppressedTasks);
    }
  }

 private:
  CycleCollectedJSContext* ccjs;
  std::deque<RefPtr<MicroTaskRunnable>> mQueue;
};

js::UniquePtr<JS::JobQueue::SavedJobQueue>
CycleCollectedJSContext::saveJobQueue(JSContext* cx) {
  auto saved = js::MakeUnique<SavedMicroTaskQueue>(this);
  if (!saved) {
    // When MakeUnique's allocation fails, the SavedMicroTaskQueue constructor
    // is never called, so mPendingMicroTaskRunnables is still initialized.
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
    PromiseDebugging::AddUncaughtRejection(aPromise);
    if (!aMutedErrors) {
      RefPtr<Promise> promise =
          Promise::CreateFromExisting(xpc::NativeGlobal(aPromise), aPromise);
      aboutToBeNotified.AppendElement(promise);
      unhandled.InsertOrUpdate(promiseID, std::move(promise));
    }
  } else {
    PromiseDebugging::AddConsumedRejection(aPromise);
    for (size_t i = 0; i < aboutToBeNotified.Length(); i++) {
      if (aboutToBeNotified[i] &&
          aboutToBeNotified[i]->PromiseObj() == aPromise) {
        // To avoid large amounts of memmoves, we don't shrink the vector
        // here. Instead, we filter out nullptrs when iterating over the
        // vector later.
        aboutToBeNotified[i] = nullptr;
        DebugOnly<bool> isFound = unhandled.Remove(promiseID);
        MOZ_ASSERT(isFound);
        return;
      }
    }
    RefPtr<Promise> promise;
    unhandled.Remove(promiseID, getter_AddRefs(promise));
    if (!promise && !aMutedErrors) {
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

        RefPtr<AsyncEventDispatcher> asyncDispatcher =
            new AsyncEventDispatcher(owner, event.forget());
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

std::deque<RefPtr<MicroTaskRunnable>>&
CycleCollectedJSContext::GetMicroTaskQueue() {
  MOZ_ASSERT(mJSContext);
  return mPendingMicroTaskRunnables;
}

std::deque<RefPtr<MicroTaskRunnable>>&
CycleCollectedJSContext::GetDebuggerMicroTaskQueue() {
  MOZ_ASSERT(mJSContext);
  return mDebuggerMicroTaskQueue;
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
    RefPtr<NotifyUnhandledRejections> runnable = new NotifyUnhandledRejections(
        std::move(mAboutToBeNotifiedRejectedPromises));
    NS_DispatchToCurrentThread(runnable);
  }
  // Cleanup Indexed Database transactions:
  // https://html.spec.whatwg.org/multipage/webappapis.html#perform-a-microtask-checkpoint
  CleanupIDBTransactions(RecursionDepth());

  // Clear kept alive objects in JS WeakRef.
  // https://whatpr.org/html/4571/webappapis.html#perform-a-microtask-checkpoint
  //
  // ECMAScript implementations are expected to call ClearKeptObjects when a
  // synchronous sequence of ECMAScript execution completes.
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
    already_AddRefed<nsIRunnable>&& aRunnable) {
  MOZ_ASSERT(mJSContext);
  nsCOMPtr<nsIRunnable> runnable = std::move(aRunnable);
  PROFILER_MARKER("CycleCollectedJSContext::RunInStableState", OTHER, {},
                  FlowMarker, Flow::FromPointer(runnable.get()));
  mStableStateEvents.AppendElement(std::move(runnable));
}

void CycleCollectedJSContext::AddPendingIDBTransaction(
    already_AddRefed<nsIRunnable>&& aTransaction) {
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

void CycleCollectedJSContext::DispatchToMicroTask(
    already_AddRefed<MicroTaskRunnable> aRunnable) {
  RefPtr<MicroTaskRunnable> runnable(aRunnable);

  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(runnable);

  JS::JobQueueMayNotBeEmpty(Context());

  LogMicroTaskRunnable::LogDispatch(runnable.get());
  if (!runnable->isInList()) {
    // A recycled object may be in the list already.
    mMicrotasksToTrace.insertBack(runnable);
  }
  mPendingMicroTaskRunnables.push_back(std::move(runnable));
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

SuppressedMicroTasks::SuppressedMicroTasks(CycleCollectedJSContext* aContext)
    : mContext(aContext),
      mSuppressionGeneration(aContext->mSuppressionGeneration) {}

bool SuppressedMicroTasks::Suppressed() {
  if (mSuppressionGeneration == mContext->mSuppressionGeneration) {
    return true;
  }

  for (std::deque<RefPtr<MicroTaskRunnable>>::reverse_iterator it =
           mSuppressedMicroTaskRunnables.rbegin();
       it != mSuppressedMicroTaskRunnables.rend(); ++it) {
    mContext->GetMicroTaskQueue().push_front(*it);
  }
  mContext->mSuppressedMicroTasks = nullptr;

  return false;
}

bool CycleCollectedJSContext::PerformMicroTaskCheckPoint(bool aForce) {
  if (mPendingMicroTaskRunnables.empty() && mDebuggerMicroTaskQueue.empty()) {
    AfterProcessMicrotasks();
    // Nothing to do, return early.
    return false;
  }

  uint32_t currentDepth = RecursionDepth();
  if (mMicroTaskRecursionDepth && *mMicroTaskRecursionDepth >= currentDepth &&
      !aForce) {
    // We are already executing microtasks for the current recursion depth.
    return false;
  }

  if (mTargetedMicroTaskRecursionDepth != 0 &&
      mTargetedMicroTaskRecursionDepth + mDebuggerRecursionDepth !=
          currentDepth) {
    return false;
  }

  if (NS_IsMainThread() && !nsContentUtils::IsSafeToRunScript()) {
    // Special case for main thread where DOM mutations may happen when
    // it is not safe to run scripts.
    nsContentUtils::AddScriptRunner(new AsyncMutationHandler());
    return false;
  }

  mozilla::AutoRestore<Maybe<uint32_t>> restore(mMicroTaskRecursionDepth);
  mMicroTaskRecursionDepth = Some(currentDepth);

  AUTO_PROFILER_TRACING_MARKER("JS", "Perform microtasks", JS);

  bool didProcess = false;
  AutoSlowOperation aso;

  for (;;) {
    RefPtr<MicroTaskRunnable> runnable;
    if (!mDebuggerMicroTaskQueue.empty()) {
      runnable = std::move(mDebuggerMicroTaskQueue.front());
      mDebuggerMicroTaskQueue.pop_front();
    } else if (!mPendingMicroTaskRunnables.empty()) {
      runnable = std::move(mPendingMicroTaskRunnables.front());
      mPendingMicroTaskRunnables.pop_front();
    } else {
      break;
    }

    // No need to check Suppressed if there aren't ongoing sync operations nor
    // pending mSuppressedMicroTasks.
    if ((IsInSyncOperation() || mSuppressedMicroTasks) &&
        runnable->Suppressed()) {
      // Microtasks in worker shall never be suppressed.
      // Otherwise, mPendingMicroTaskRunnables will be replaced later with
      // all suppressed tasks in mDebuggerMicroTaskQueue unexpectedly.
      MOZ_ASSERT(NS_IsMainThread());
      JS::JobQueueMayNotBeEmpty(Context());
      if (runnable != mSuppressedMicroTasks) {
        if (!mSuppressedMicroTasks) {
          mSuppressedMicroTasks = new SuppressedMicroTasks(this);
        }
        mSuppressedMicroTasks->mSuppressedMicroTaskRunnables.push_back(
            runnable);
      }
    } else {
      if (mPendingMicroTaskRunnables.empty() &&
          mDebuggerMicroTaskQueue.empty() && !mSuppressedMicroTasks) {
        JS::JobQueueIsEmpty(Context());
      }
      didProcess = true;

      LogMicroTaskRunnable::Run log(runnable.get());
      runnable->Run(aso);
      runnable = nullptr;
    }
  }

  // Put back the suppressed microtasks so that they will be run later.
  // Note, it is possible that we end up keeping these suppressed tasks around
  // for some time, but no longer than spinning the event loop nestedly
  // (sync XHR, alert, etc.)
  if (mSuppressedMicroTasks) {
    mPendingMicroTaskRunnables.push_back(mSuppressedMicroTasks);
  }

  AfterProcessMicrotasks();

  return didProcess;
}

void CycleCollectedJSContext::PerformDebuggerMicroTaskCheckpoint() {
  // Don't do normal microtask handling checks here, since whoever is calling
  // this method is supposed to know what they are doing.

  AutoSlowOperation aso;
  for (;;) {
    // For a debugger microtask checkpoint, we always use the debugger microtask
    // queue.
    std::deque<RefPtr<MicroTaskRunnable>>* microtaskQueue =
        &GetDebuggerMicroTaskQueue();

    if (microtaskQueue->empty()) {
      break;
    }

    RefPtr<MicroTaskRunnable> runnable = std::move(microtaskQueue->front());
    MOZ_ASSERT(runnable);

    LogMicroTaskRunnable::Run log(runnable.get());

    // This function can re-enter, so we remove the element before calling.
    microtaskQueue->pop_front();

    if (mPendingMicroTaskRunnables.empty() && mDebuggerMicroTaskQueue.empty()) {
      JS::JobQueueIsEmpty(Context());
    }
    runnable->Run(aso);
    runnable = nullptr;
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
        // We don't use the result of dispatching event here to check whether to
        // report the Promise to console.
        target->DispatchEvent(*event);
      }
    }

    cccx = CycleCollectedJSContext::Get();
    NS_ENSURE_STATE(cccx);
    if (!JS::GetPromiseIsHandled(promiseObj)) {
      DebugOnly<bool> isFound =
          cccx->mPendingUnhandledRejections.Remove(promiseID);
      MOZ_ASSERT(isFound);
    }

    // If a rejected promise is being handled in "unhandledrejection" event
    // handler, it should be removed from the table in
    // PromiseRejectionTrackerCallback.
    MOZ_ASSERT(!cccx->mPendingUnhandledRejections.Lookup(promiseID));
  }
  return NS_OK;
}

nsresult CycleCollectedJSContext::NotifyUnhandledRejections::Cancel() {
  CycleCollectedJSContext* cccx = CycleCollectedJSContext::Get();
  NS_ENSURE_STATE(cccx);

  for (size_t i = 0; i < mUnhandledRejections.Length(); ++i) {
    RefPtr<Promise>& promise = mUnhandledRejections[i];
    if (!promise) {
      continue;
    }

    JS::RootedObject promiseObj(cccx->RootingCx(), promise->PromiseObj());
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
      : DiscardableRunnable("CleanupRunnable"), mCleanupWork(aCleanupWork) {}

  // MOZ_CAN_RUN_SCRIPT_BOUNDARY until Runnable::Run is MOZ_CAN_RUN_SCRIPT.  See
  // bug 1535398.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  NS_IMETHOD Run() override {
    mCleanupWork->DoCleanup();
    return NS_OK;
  }

 private:
  FinalizationRegistryCleanup* mCleanupWork;
};

FinalizationRegistryCleanup::FinalizationRegistryCleanup(
    CycleCollectedJSContext* aContext)
    : mContext(aContext) {}

void FinalizationRegistryCleanup::Destroy() {
  // This must happen before the CycleCollectedJSContext destructor calls
  // JS_DestroyContext().
  mCallbacks.reset();
}

void FinalizationRegistryCleanup::Init() {
  JSContext* cx = mContext->Context();
  mCallbacks.init(cx);
  JS::SetHostCleanupFinalizationRegistryCallback(cx, QueueCallback, this);
}

/* static */
void FinalizationRegistryCleanup::QueueCallback(JSFunction* aDoCleanup,
                                                JSObject* aHostDefinedData,
                                                void* aData) {
  FinalizationRegistryCleanup* cleanup =
      static_cast<FinalizationRegistryCleanup*>(aData);
  cleanup->QueueCallback(aDoCleanup, aHostDefinedData);
}

void FinalizationRegistryCleanup::QueueCallback(JSFunction* aDoCleanup,
                                                JSObject* aHostDefinedData) {
  bool firstCallback = mCallbacks.empty();

  JSObject* incumbentGlobal = nullptr;

  // Extract incumbentGlobal from aHostDefinedData.
  if (aHostDefinedData) {
    MOZ_RELEASE_ASSERT(JS::GetClass(aHostDefinedData) ==
                       &sHostDefinedDataClass);
    JS::Value global =
        JS::GetReservedSlot(aHostDefinedData, INCUMBENT_SETTING_SLOT);
    incumbentGlobal = &global.toObject();
  }

  MOZ_ALWAYS_TRUE(mCallbacks.append(Callback{aDoCleanup, incumbentGlobal}));

  if (firstCallback) {
    RefPtr<CleanupRunnable> cleanup = new CleanupRunnable(this);
    NS_DispatchToCurrentThread(cleanup.forget());
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
    JS::ExposeObjectToActiveJS(
        JS_GetFunctionObject(callback.mCallbackFunction));
    JS::ExposeObjectToActiveJS(callback.mIncumbentGlobal);

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
