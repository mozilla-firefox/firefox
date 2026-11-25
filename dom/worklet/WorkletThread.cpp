/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WorkletThread.h"
#include "prthread.h"
#include "nsContentUtils.h"
#include "nsCycleCollector.h"
#include "nsJSEnvironment.h"
#include "nsJSPrincipals.h"
#include "mozilla/dom/AtomList.h"
#include "mozilla/dom/WorkletGlobalScope.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/Attributes.h"
#include "mozilla/CycleCollectedJSRuntime.h"
#include "mozilla/EventQueue.h"
#include "mozilla/ThreadEventQueue.h"
#include "js/ContextOptions.h"
#include "js/Exception.h"
#include "js/Initialization.h"
#include "XPCSelfHostedShmem.h"

namespace mozilla::dom {

namespace {

// The size of the worklet runtime heaps in bytes.
#define WORKLET_DEFAULT_RUNTIME_HEAPSIZE 32 * 1024 * 1024

// The C stack size. We use the same stack size on all platforms for
// consistency.
const uint32_t kWorkletStackSize = 256 * sizeof(size_t) * 1024;

// Half the size of the actual C stack, to be safe.
#define WORKLET_CONTEXT_NATIVE_STACK_LIMIT 128 * sizeof(size_t) * 1024

// Helper functions

bool PreserveWrapper(JSContext* aCx, JS::Handle<JSObject*> aObj) {
  MOZ_ASSERT(aCx);
  MOZ_ASSERT(aObj);
  MOZ_ASSERT(mozilla::dom::IsDOMObject(aObj));
  return mozilla::dom::TryPreserveWrapper(aObj);
}

JSObject* Wrap(JSContext* aCx, JS::Handle<JSObject*> aExisting,
               JS::Handle<JSObject*> aObj) {
  if (aExisting) {
    js::Wrapper::Renew(aExisting, aObj,
                       &js::OpaqueCrossCompartmentWrapper::singleton);
  }

  return js::Wrapper::New(aCx, aObj,
                          &js::OpaqueCrossCompartmentWrapper::singleton);
}

const JSWrapObjectCallbacks WrapObjectCallbacks = {
    Wrap,
    nullptr,
};

}  // namespace

// This classes control CC in the worklet thread.

class WorkletJSRuntime final : public mozilla::CycleCollectedJSRuntime {
 public:
  explicit WorkletJSRuntime(JSContext* aCx) : CycleCollectedJSRuntime(aCx) {}

  ~WorkletJSRuntime() override = default;

  virtual void PrepareForForgetSkippable() override {}

  virtual void BeginCycleCollectionCallback(
      mozilla::CCReason aReason) override {}

  virtual void EndCycleCollectionCallback(
      CycleCollectorResults& aResults) override {}

  virtual void DispatchDeferredDeletion(bool aContinuation,
                                        bool aPurge) override {
    MOZ_ASSERT(!aContinuation);
    nsCycleCollector_doDeferredDeletion();
  }

  virtual void CustomGCCallback(JSGCStatus aStatus) override {
    // nsCycleCollector_collect() requires a cycle collector but
    // ~WorkletJSContext calls nsCycleCollector_shutdown() and the base class
    // destructor will trigger a final GC.  The nsCycleCollector_collect()
    // call can be skipped in this GC as ~CycleCollectedJSContext removes the
    // context from |this|.
    if (aStatus == JSGC_END && GetContext()) {
      nsCycleCollector_collect(CCReason::GC_FINISHED, nullptr);
    }
  }
};

class WorkletJSContext final : public CycleCollectedJSContext {
 public:
  WorkletJSContext() {
    MOZ_ASSERT(!NS_IsMainThread());

    nsCycleCollector_startup();
  }

  // MOZ_CAN_RUN_SCRIPT_BOUNDARY because otherwise we have to annotate the
  // SpiderMonkey JS::JobQueue's destructor as MOZ_CAN_RUN_SCRIPT, which is a
  // bit of a pain.
  MOZ_CAN_RUN_SCRIPT_BOUNDARY ~WorkletJSContext() override {
    MOZ_ASSERT(!NS_IsMainThread());

    JSContext* cx = MaybeContext();
    if (!cx) {
      return;  // Initialize() must have failed
    }

    nsCycleCollector_shutdown();
  }

  WorkletJSContext* GetAsWorkletJSContext() override { return this; }

  CycleCollectedJSRuntime* CreateRuntime(JSContext* aCx) override {
    return new WorkletJSRuntime(aCx);
  }

  nsresult Initialize(JSRuntime* aParentRuntime) {
    MOZ_ASSERT(!NS_IsMainThread());

    nsresult rv = CycleCollectedJSContext::Initialize(
        aParentRuntime, WORKLET_DEFAULT_RUNTIME_HEAPSIZE);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    JSContext* cx = Context();

    js::SetPreserveWrapperCallbacks(cx, PreserveWrapper, HasReleasedWrapper);
    JS_InitDestroyPrincipalsCallback(cx, nsJSPrincipals::Destroy);
    JS_InitReadPrincipalsCallback(cx, nsJSPrincipals::ReadPrincipals);
    JS_SetWrapObjectCallbacks(cx, &WrapObjectCallbacks);
    JS_SetFutexCanWait(cx);

    return NS_OK;
  }

  void DispatchToMicroTask(
      already_AddRefed<MicroTaskRunnable> aRunnable) override {
    RefPtr<MicroTaskRunnable> runnable(aRunnable);

    MOZ_ASSERT(!NS_IsMainThread());
    MOZ_ASSERT(runnable);

    JSContext* cx = Context();
    MOZ_ASSERT(cx);

#ifdef DEBUG
    JS::Rooted<JSObject*> global(cx, JS::CurrentGlobalOrNull(cx));
    MOZ_ASSERT(global);
#endif

    JS::JobQueueMayNotBeEmpty(cx);
    GetMicroTaskQueue().push_back(std::move(runnable));
  }

  bool IsSystemCaller() const override {
    // Currently no support for special system worklet privileges.
    return false;
  }

  void ReportError(JSErrorReport* aReport,
                   JS::ConstUTF8CharsZ aToStringResult) override;

  uint64_t GetCurrentWorkletWindowID() {
    JSObject* global = JS::CurrentGlobalOrNull(Context());
    if (NS_WARN_IF(!global)) {
      return 0;
    }
    nsIGlobalObject* nativeGlobal = xpc::NativeGlobal(global);
    nsCOMPtr<WorkletGlobalScope> workletGlobal =
        do_QueryInterface(nativeGlobal);
    if (NS_WARN_IF(!workletGlobal)) {
      return 0;
    }
    return workletGlobal->Impl()->LoadInfo().InnerWindowID();
  }
};

void WorkletJSContext::ReportError(JSErrorReport* aReport,
                                   JS::ConstUTF8CharsZ aToStringResult) {
  RefPtr<xpc::ErrorReport> xpcReport = new xpc::ErrorReport();
  xpcReport->Init(aReport, aToStringResult.c_str(), IsSystemCaller(),
                  GetCurrentWorkletWindowID());
  RefPtr<AsyncErrorReporter> reporter = new AsyncErrorReporter(xpcReport);

  JSContext* cx = Context();
  if (JS_IsExceptionPending(cx)) {
    JS::ExceptionStack exnStack(cx);
    if (JS::StealPendingExceptionStack(cx, &exnStack)) {
      JS::Rooted<JSObject*> stack(cx);
      JS::Rooted<JSObject*> stackGlobal(cx);
      xpc::FindExceptionStackForConsoleReport(nullptr, exnStack.exception(),
                                              exnStack.stack(), &stack,
                                              &stackGlobal);
      if (stack) {
        reporter->SerializeStack(cx, stack);
      }
    }
  }

  NS_DispatchToMainThread(reporter);
}

// This is the first runnable to be dispatched. It calls the RunEventLoop() so
// basically everything happens into this runnable. The reason behind this
// approach is that, when the Worklet is terminated, it must not have any JS in
// stack, but, because we have CC, nsIThread creates an AutoNoJSAPI object by
// default. Using this runnable, CC exists only into it.
class WorkletThread::PrimaryRunnable final : public Runnable {
 public:
  explicit PrimaryRunnable(WorkletThread* aWorkletThread)
      : Runnable("WorkletThread::PrimaryRunnable"),
        mWorkletThread(aWorkletThread) {
    MOZ_ASSERT(aWorkletThread);
    MOZ_ASSERT(NS_IsMainThread());
  }

  NS_IMETHOD
  Run() override {
    mWorkletThread->RunEventLoop();
    return NS_OK;
  }

 private:
  RefPtr<WorkletThread> mWorkletThread;
};

// This is the last runnable to be dispatched. It calls the TerminateInternal()
class WorkletThread::TerminateRunnable final : public Runnable {
 public:
  explicit TerminateRunnable(WorkletThread* aWorkletThread)
      : Runnable("WorkletThread::TerminateRunnable"),
        mWorkletThread(aWorkletThread) {
    MOZ_ASSERT(aWorkletThread);
    MOZ_ASSERT(NS_IsMainThread());
  }

  NS_IMETHOD
  Run() override {
    mWorkletThread->TerminateInternal();
    return NS_OK;
  }

 private:
  RefPtr<WorkletThread> mWorkletThread;
};

WorkletThread::WorkletThread(WorkletImpl* aWorkletImpl)
    : nsThread(
          MakeNotNull<ThreadEventQueue*>(MakeUnique<mozilla::EventQueue>()),
          nsThread::NOT_MAIN_THREAD, {.stackSize = kWorkletStackSize}),
      mWorkletImpl(aWorkletImpl),
      mExitLoop(false),
      mIsTerminating(false) {
  MOZ_ASSERT(NS_IsMainThread());
  nsContentUtils::RegisterShutdownObserver(this);
}

WorkletThread::~WorkletThread() = default;

// static
already_AddRefed<WorkletThread> WorkletThread::Create(
    WorkletImpl* aWorkletImpl) {
  RefPtr<WorkletThread> thread = new WorkletThread(aWorkletImpl);
  if (NS_WARN_IF(NS_FAILED(thread->Init("DOM Worklet"_ns)))) {
    return nullptr;
  }

  RefPtr<PrimaryRunnable> runnable = new PrimaryRunnable(thread);
  if (NS_WARN_IF(NS_FAILED(thread->DispatchRunnable(runnable.forget())))) {
    return nullptr;
  }

  return thread.forget();
}

nsresult WorkletThread::DispatchRunnable(
    already_AddRefed<nsIRunnable> aRunnable) {
  nsCOMPtr<nsIRunnable> runnable(aRunnable);
  return nsThread::Dispatch(runnable.forget(), NS_DISPATCH_NORMAL);
}

NS_IMETHODIMP
WorkletThread::DispatchFromScript(nsIRunnable* aRunnable, uint32_t aFlags) {
  nsCOMPtr<nsIRunnable> runnable(aRunnable);
  return Dispatch(runnable.forget(), aFlags);
}

NS_IMETHODIMP
WorkletThread::Dispatch(already_AddRefed<nsIRunnable> aRunnable,
                        uint32_t aFlags) {
  nsCOMPtr<nsIRunnable> runnable(aRunnable);

  // Worklet only supports asynchronous dispatch.
  if (NS_WARN_IF(aFlags != NS_DISPATCH_NORMAL)) {
    return NS_ERROR_UNEXPECTED;
  }

  return nsThread::Dispatch(runnable.forget(), NS_DISPATCH_NORMAL);
}

NS_IMETHODIMP
WorkletThread::DelayedDispatch(already_AddRefed<nsIRunnable>, uint32_t aFlags) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

static bool DispatchToEventLoop(void* aClosure,
                                JS::Dispatchable* aDispatchable) {
  // This callback may execute either on the worklet thread or a random
  // JS-internal helper thread.

  // See comment at JS::InitDispatchToEventLoop() below for how we know the
  // thread is alive.
  nsIThread* thread = static_cast<nsIThread*>(aClosure);

  nsresult rv = thread->Dispatch(
      NS_NewRunnableFunction(
          "WorkletThread::DispatchToEventLoop",
          [aDispatchable]() {
            CycleCollectedJSContext* ccjscx = CycleCollectedJSContext::Get();
            if (!ccjscx) {
              return;
            }

            WorkletJSContext* wjc = ccjscx->GetAsWorkletJSContext();
            if (!wjc) {
              return;
            }

            aDispatchable->run(wjc->Context(),
                               JS::Dispatchable::NotShuttingDown);
          }),
      NS_DISPATCH_NORMAL);

  return NS_SUCCEEDED(rv);
}

// static
void WorkletThread::EnsureCycleCollectedJSContext(
    JSRuntime* aParentRuntime, const JS::ContextOptions& aOptions) {
  CycleCollectedJSContext* ccjscx = CycleCollectedJSContext::Get();
  if (ccjscx) {
    MOZ_ASSERT(ccjscx->GetAsWorkletJSContext());
    return;
  }

  WorkletJSContext* context = new WorkletJSContext();
  nsresult rv = context->Initialize(aParentRuntime);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    // TODO: error propagation
    return;
  }

  JS::ContextOptionsRef(context->Context()) = aOptions;

  JS_SetGCParameter(context->Context(), JSGC_MAX_BYTES, uint32_t(-1));

  // FIXME: JS_SetDefaultLocale
  // FIXME: JSSettings
  // FIXME: JS_SetSecurityCallbacks
  // FIXME: JS::SetAsyncTaskCallbacks
  // FIXME: JS::SetCTypesActivityCallback
  // FIXME: JS_SetGCZeal

  // A thread lives strictly longer than its JSRuntime so we can safely
  // store a raw pointer as the callback's closure argument on the JSRuntime.
  JS::InitDispatchToEventLoop(context->Context(), DispatchToEventLoop,
                              NS_GetCurrentThread());

  JS_SetNativeStackQuota(context->Context(),
                         WORKLET_CONTEXT_NATIVE_STACK_LIMIT);

  // When available, set the self-hosted shared memory to be read, so that we
  // can decode the self-hosted content instead of parsing it.
  auto& shm = xpc::SelfHostedShmem::GetSingleton();
  JS::SelfHostedCache selfHostedContent = shm.Content();

  if (!JS::InitSelfHostedCode(context->Context(), selfHostedContent)) {
    // TODO: error propagation
    return;
  }
}

void WorkletThread::RunEventLoop() {
  MOZ_ASSERT(!NS_IsMainThread());

  PR_SetCurrentThreadName("worklet");

  while (!mExitLoop) {
    MOZ_ALWAYS_TRUE(NS_ProcessNextEvent(this, /* wait: */ true));
  }

  DeleteCycleCollectedJSContext();
}

void WorkletThread::Terminate() {
  MOZ_ASSERT(NS_IsMainThread());

  if (mIsTerminating) {
    // nsThread::Dispatch() would leak the runnable if the event queue is no
    // longer accepting runnables.
    return;
  }

  mIsTerminating = true;

  nsContentUtils::UnregisterShutdownObserver(this);

  RefPtr<TerminateRunnable> runnable = new TerminateRunnable(this);
  DispatchRunnable(runnable.forget());
}

uint32_t WorkletThread::StackSize() { return kWorkletStackSize; }

void WorkletThread::TerminateInternal() {
  MOZ_ASSERT(!CycleCollectedJSContext::Get() || IsOnWorkletThread());

  mExitLoop = true;

  nsCOMPtr<nsIRunnable> runnable = NewRunnableMethod(
      "WorkletThread::Shutdown", this, &WorkletThread::Shutdown);
  NS_DispatchToMainThread(runnable);
}

/* static */
void WorkletThread::DeleteCycleCollectedJSContext() {
  CycleCollectedJSContext* ccjscx = CycleCollectedJSContext::Get();
  if (!ccjscx) {
    return;
  }

  // Release any MessagePort kept alive by its ipc actor.
  mozilla::ipc::BackgroundChild::CloseForCurrentThread();

  WorkletJSContext* workletjscx = ccjscx->GetAsWorkletJSContext();
  MOZ_ASSERT(workletjscx);
  delete workletjscx;
}

/* static */
bool WorkletThread::IsOnWorkletThread() {
  CycleCollectedJSContext* ccjscx = CycleCollectedJSContext::Get();
  return ccjscx && ccjscx->GetAsWorkletJSContext();
}

/* static */
void WorkletThread::AssertIsOnWorkletThread() {
  MOZ_ASSERT(IsOnWorkletThread());
}

// nsIObserver
NS_IMETHODIMP
WorkletThread::Observe(nsISupports* aSubject, const char* aTopic,
                       const char16_t*) {
  MOZ_ASSERT(strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID) == 0);

  // The WorkletImpl will terminate the worklet thread after sending a message
  // to release worklet thread objects.
  mWorkletImpl->NotifyWorkletFinished();
  return NS_OK;
}

NS_IMPL_ISUPPORTS_INHERITED(WorkletThread, nsThread, nsIObserver)

}  // namespace mozilla::dom
