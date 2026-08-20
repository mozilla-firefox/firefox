/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "LockRequestChild.h"

#include "LockManagerChild.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/WorkerPrivate.h"

namespace mozilla::dom::locks {

using IPCResult = mozilla::ipc::IPCResult;

NS_IMPL_ISUPPORTS(LockRequestChild, nsISupports)

MOZ_CAN_RUN_SCRIPT static void RunCallbackAndSettlePromise(
    LockGrantedCallback& aCallback, mozilla::dom::Lock* lock,
    Promise& aPromise) {
  ErrorResult rv;
  if (RefPtr<Promise> result = aCallback.Call(
          lock, rv, nullptr, CallbackObject::eRethrowExceptions)) {
    aPromise.MaybeResolve(result);
  } else if (rv.Failed() && !rv.IsUncatchableException()) {
    aPromise.MaybeReject(std::move(rv));
    return;
  } else {
    aPromise.MaybeResolveWithUndefined();
  }
  // This is required even with no failure. IgnoredErrorResult is not an option
  // since MaybeReject does not accept it.
  rv.WouldReportJSException();
  if (NS_WARN_IF(rv.IsUncatchableException())) {
    rv.SuppressException();  // XXX: Why does this happen anyway?
  }
  MOZ_ASSERT(!rv.Failed());
}

LockRequestChild::LockRequestChild(
    const LockRequest& aRequest,
    const Optional<OwningNonNull<AbortSignal>>& aSignal)
    : mRequest(aRequest) {
  if (aSignal.WasPassed()) {
    Follow(&aSignal.Value());
  }
}

void LockRequestChild::MaybeSetWorkerRef() {
  if (!NS_IsMainThread()) {
    mWorkerRef = StrongWorkerRef::Create(
        GetCurrentThreadWorkerPrivate(), "LockManager",
        [self = RefPtr(this)]() { self->mWorkerRef = nullptr; });
  }
}

void LockRequestChild::ActorDestroy(ActorDestroyReason aReason) {
  Unfollow();
  CastedManager()->NotifyRequestDestroy();
}

IPCResult LockRequestChild::RecvResolve(const LockMode& aLockMode,
                                        bool aIsAvailable) {
  Unfollow();

  RefPtr<Lock> lock;
  RefPtr<Promise> promise;
  if (aIsAvailable) {
    IgnoredErrorResult err;
    lock = new Lock(CastedManager()->GetParentObject(), this, mRequest.mName,
                    aLockMode, mRequest.mPromise, err);
    if (MOZ_UNLIKELY(err.Failed())) {
      mRequest.mPromise->MaybeRejectWithUnknownError(
          "Failed to allocate a lock");
      return IPC_OK();
    }
    lock->GetWaitingPromise().AppendNativeHandler(lock);
    promise = &lock->GetWaitingPromise();
  } else {
    // We are in `ifAvailable: true` mode and the lock is not available.
    // There is no waitingPromise since there is no lock, so settle the promise
    // from the request instead.
    // This matches "If ifAvailable is true and request is not grantable" step.
    promise = mRequest.mPromise;
  }

  // XXX(krosylight): MOZ_KnownLive shouldn't be needed here, mRequest is const
  RunCallbackAndSettlePromise(MOZ_KnownLive(*mRequest.mCallback), lock,
                              *promise);
  return IPC_OK();
}

IPCResult LockRequestChild::Recv__delete__(bool aAborted) {
  MOZ_ASSERT(aAborted, "__delete__ is currently only for abort");
  Unfollow();
  mRequest.mPromise->MaybeRejectWithAbortError("The lock request is aborted");
  return IPC_OK();
}

void LockRequestChild::RunAbortAlgorithm() {
  AutoJSAPI jsapi;
  if (NS_WARN_IF(!jsapi.Init(
          static_cast<AbortSignal*>(Signal())->GetRelevantGlobal()))) {
    mRequest.mPromise->MaybeRejectWithAbortError("The lock request is aborted");
  } else {
    JSContext* cx = jsapi.cx();
    JS::Rooted<JS::Value> reason(cx);
    Signal()->GetReason(cx, &reason);
    mRequest.mPromise->MaybeReject(reason);
  }

  Send__delete__(this, true);
}

inline LockManagerChild* LockRequestChild::CastedManager() const {
  return mozilla::ipc::ActorCast<LockManagerChild>(Manager());
};

}  // namespace mozilla::dom::locks
