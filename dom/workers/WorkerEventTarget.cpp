/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WorkerEventTarget.h"
#include "WorkerPrivate.h"
#include "WorkerRunnable.h"

#include "mozilla/Logging.h"
#include "mozilla/dom/ReferrerInfo.h"

namespace mozilla::dom {

static mozilla::LazyLogModule sWorkerEventTargetLog("WorkerEventTarget");

#ifdef LOG
#  undef LOG
#endif
#ifdef LOGV
#  undef LOGV
#endif
#define LOG(args) MOZ_LOG(sWorkerEventTargetLog, LogLevel::Debug, args);
#define LOGV(args) MOZ_LOG(sWorkerEventTargetLog, LogLevel::Verbose, args);

namespace {

class WrappedControlRunnable final : public WorkerControlRunnable {
  nsCOMPtr<nsIRunnable> mInner;

  ~WrappedControlRunnable() = default;

 public:
  WrappedControlRunnable(WorkerPrivate* aWorkerPrivate,
                         nsCOMPtr<nsIRunnable>&& aInner)
      : WorkerControlRunnable("WrappedControlRunnable"),
        mInner(std::move(aInner)) {}

  virtual bool PreDispatch(WorkerPrivate* aWorkerPrivate) override {
    // Silence bad assertions, this can be dispatched from any thread.
    return true;
  }

  virtual void PostDispatch(WorkerPrivate* aWorkerPrivate,
                            bool aDispatchResult) override {
    // Silence bad assertions, this can be dispatched from any thread.
  }

  bool WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    mInner->Run();
    return true;
  }

  nsresult Cancel() override {
    nsCOMPtr<nsICancelableRunnable> cr = do_QueryInterface(mInner);

    // If the inner runnable is not cancellable, then just do the normal
    // WorkerControlRunnable thing.  This will end up calling Run().
    if (!cr) {
      return Run();
    }

    // Otherwise call the inner runnable's Cancel() and treat this like
    // a WorkerRunnable cancel.  We can't call WorkerControlRunnable::Cancel()
    // in this case since that would result in both Run() and the inner
    // Cancel() being called.
    return cr->Cancel();
  }

#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
  NS_IMETHOD GetName(nsACString& aName) override {
    aName.AssignLiteral("WrappedControlRunnable(");
    if (nsCOMPtr<nsINamed> named = do_QueryInterface(mInner)) {
      nsAutoCString containedName;
      named->GetName(containedName);
      aName.Append(containedName);
    } else {
      aName.AppendLiteral("?");
    }
    aName.AppendLiteral(")");
    return NS_OK;
  }
#endif
};

class WrappedDebuggerRunnable final : public WorkerDebuggerRunnable {
  nsCOMPtr<nsIRunnable> mInner;

  ~WrappedDebuggerRunnable() = default;

 public:
  WrappedDebuggerRunnable(WorkerPrivate* aWorkerPrivate,
                          nsCOMPtr<nsIRunnable>&& aInner)
      : WorkerDebuggerRunnable("WrappedDebuggerRunnable"),
        mInner(std::move(aInner)) {}

  virtual bool PreDispatch(WorkerPrivate* aWorkerPrivate) override {
    // Silence bad assertions, this can be dispatched from any thread.
    return true;
  }

  virtual void PostDispatch(WorkerPrivate* aWorkerPrivate,
                            bool aDispatchResult) override {
    // Silence bad assertions, this can be dispatched from any thread.
  }

  bool WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    mInner->Run();
    return true;
  }

#ifdef MOZ_COLLECTING_RUNNABLE_TELEMETRY
  NS_IMETHOD GetName(nsACString& aName) override {
    aName.AssignLiteral("WrappedDebuggerRunnable(");
    if (nsCOMPtr<nsINamed> named = do_QueryInterface(mInner)) {
      nsAutoCString containedName;
      named->GetName(containedName);
      aName.Append(containedName);
    } else {
      aName.AppendLiteral("?");
    }
    aName.AppendLiteral(")");
    return NS_OK;
  }
#endif
};

}  // anonymous namespace

NS_IMPL_ISUPPORTS(WorkerEventTarget, nsIEventTarget, nsISerialEventTarget)

WorkerEventTarget::WorkerEventTarget(WorkerPrivate* aWorkerPrivate,
                                     Behavior aBehavior)
    : mMutex("WorkerEventTarget"),
      mWorkerPrivate(aWorkerPrivate),
      mBehavior(aBehavior) {
  LOG(("WorkerEventTarget::WorkerEventTarget [%p] aBehavior: %u", this,
       (uint8_t)aBehavior));
  MOZ_DIAGNOSTIC_ASSERT(mWorkerPrivate);
}

void WorkerEventTarget::ForgetWorkerPrivate(WorkerPrivate* aWorkerPrivate) {
  LOG(("WorkerEventTarget::ForgetWorkerPrivate [%p] aWorkerPrivate: %p", this,
       aWorkerPrivate));
  MutexAutoLock lock(mMutex);
  MOZ_DIAGNOSTIC_ASSERT(!mWorkerPrivate || mWorkerPrivate == aWorkerPrivate);
  mWorkerPrivate = nullptr;
}

NS_IMETHODIMP
WorkerEventTarget::DispatchFromScript(nsIRunnable* aRunnable, uint32_t aFlags) {
  LOGV(("WorkerEventTarget::DispatchFromScript [%p] aRunnable: %p", this,
        aRunnable));
  nsCOMPtr<nsIRunnable> runnable(aRunnable);
  return Dispatch(runnable.forget(), aFlags);
}

NS_IMETHODIMP
WorkerEventTarget::Dispatch(already_AddRefed<nsIRunnable> aRunnable,
                            uint32_t aFlags) {
  nsCOMPtr<nsIRunnable> runnable(aRunnable);
  LOGV(
      ("WorkerEventTarget::Dispatch [%p] aRunnable: %p", this, runnable.get()));

  MutexAutoLock lock(mMutex);

  if (!mWorkerPrivate) {
    return NS_ERROR_FAILURE;
  }

  if (mBehavior == Behavior::DebuggerOnly) {
    RefPtr<WorkerDebuggerRunnable> r =
        new WrappedDebuggerRunnable(mWorkerPrivate, std::move(runnable));
    LOGV(
        ("WorkerEventTarget::Dispatch [%p] Wrapped runnable as debugger "
         "runnable(%p)",
         this, r.get()));
    if (!r->Dispatch(mWorkerPrivate)) {
      LOGV(
          ("WorkerEventTarget::Dispatch [%p] Dispatch as debugger runnable(%p) "
           "fail",
           this, r.get()));
      return NS_ERROR_FAILURE;
    }
    return NS_OK;
  }

  if (mBehavior == Behavior::Hybrid) {
    LOGV(("WorkerEventTarget::Dispatch [%p] Dispatch as normal runnable(%p)",
          this, runnable.get()));

    RefPtr<WorkerRunnable> r =
        mWorkerPrivate->MaybeWrapAsWorkerRunnable(runnable.forget());
    if (r->Dispatch(mWorkerPrivate)) {
      return NS_OK;
    }
    runnable = std::move(r);
    LOGV((
        "WorkerEventTarget::Dispatch [%p] Dispatch as normal runnable(%p) fail",
        this, runnable.get()));
  }

  RefPtr<WorkerControlRunnable> r =
      new WrappedControlRunnable(mWorkerPrivate, std::move(runnable));
  LOGV(
      ("WorkerEventTarget::Dispatch [%p] Wrapped runnable as control "
       "runnable(%p)",
       this, r.get()));
  if (!r->Dispatch(mWorkerPrivate)) {
    LOGV(
        ("WorkerEventTarget::Dispatch [%p] Dispatch as control runnable(%p) "
         "fail",
         this, r.get()));
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

NS_IMETHODIMP
WorkerEventTarget::DelayedDispatch(already_AddRefed<nsIRunnable>, uint32_t) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
WorkerEventTarget::RegisterShutdownTask(nsITargetShutdownTask* aTask) {
  NS_ENSURE_ARG(aTask);

  MutexAutoLock lock(mMutex);

  // If mWorkerPrivate is gone, the event target is already late during
  // shutdown, return NS_ERROR_UNEXPECTED as documented in `nsIEventTarget.idl`.
  if (!mWorkerPrivate) {
    return NS_ERROR_UNEXPECTED;
  }

  return mWorkerPrivate->RegisterShutdownTask(aTask);
}

NS_IMETHODIMP
WorkerEventTarget::UnregisterShutdownTask(nsITargetShutdownTask* aTask) {
  NS_ENSURE_ARG(aTask);

  MutexAutoLock lock(mMutex);

  if (!mWorkerPrivate) {
    return NS_ERROR_UNEXPECTED;
  }

  return mWorkerPrivate->UnregisterShutdownTask(aTask);
}

NS_IMETHODIMP_(bool)
WorkerEventTarget::IsOnCurrentThreadInfallible() {
  MutexAutoLock lock(mMutex);

  if (!mWorkerPrivate) {
    return false;
  }

  return mWorkerPrivate->IsOnCurrentThread();
}

NS_IMETHODIMP
WorkerEventTarget::IsOnCurrentThread(bool* aIsOnCurrentThread) {
  MOZ_ASSERT(aIsOnCurrentThread);
  *aIsOnCurrentThread = IsOnCurrentThreadInfallible();
  return NS_OK;
}

}  // namespace mozilla::dom
