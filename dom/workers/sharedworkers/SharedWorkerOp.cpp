/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedWorkerOp.h"
#include "mozilla/dom/MessagePort.h"
#include "mozilla/dom/RemoteWorkerChild.h"
#include "mozilla/dom/RemoteWorkerNonLifeCycleOpControllerChild.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/WorkerRunnable.h"
#include "mozilla/dom/WorkerScope.h"

namespace mozilla::dom {

using remoteworker::Canceled;
using remoteworker::Killed;
using remoteworker::Pending;
using remoteworker::Running;

namespace {

// Normal runnable because AddPortIdentifier() is going to exec JS code.
class MessagePortIdentifierRunnable final : public WorkerSameThreadRunnable {
 public:
  MessagePortIdentifierRunnable(
      RemoteWorkerNonLifeCycleOpControllerChild* aActor,
      const MessagePortIdentifier& aPortIdentifier)
      : WorkerSameThreadRunnable("MessagePortIdentifierRunnable"),
        mActor(aActor),
        mPortIdentifier(aPortIdentifier) {}

 private:
  bool WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    if (aWorkerPrivate->GlobalScope()->IsDying()) {
      mPortIdentifier.ForceClose();
      return true;
    }
    if (!aWorkerPrivate->ConnectMessagePort(aCx, mPortIdentifier)) {
      mActor->ErrorPropagation(NS_ERROR_FAILURE);
    }
    return true;
  }

  RefPtr<RemoteWorkerNonLifeCycleOpControllerChild> mActor;
  UniqueMessagePortId mPortIdentifier;
};

class UpdateWindowIDRunnable final : public WorkerThreadRunnable {
 public:
  UpdateWindowIDRunnable(const uint64_t& aWindowID, const bool& aIsAdd)
      : WorkerThreadRunnable("UpdateWindowIDRunnable"),
        mWindowID(aWindowID),
        mIsAdd(aIsAdd) {}

 private:
  bool PreDispatch(WorkerPrivate* aWorkerPrivate) final { return true; }
  void PostDispatch(WorkerPrivate* aWorkerPrivate, bool aDispatchResult) final {
  }

  bool WorkerRun(JSContext* aCx, WorkerPrivate* aWorkerPrivate) override {
    aWorkerPrivate->UpdateWindowIDToDebugger(mWindowID, mIsAdd);
    return true;
  }

  uint64_t mWindowID;
  bool mIsAdd;
};

}  // namespace

SharedWorkerOp::SharedWorkerOp(SharedWorkerOpArgs&& aArgs)
    : mOpArgs(std::move(aArgs)) {}

SharedWorkerOp::~SharedWorkerOp() { MOZ_ASSERT(mStarted); }

void SharedWorkerOp::Cancel() {
#ifdef DEBUG
  mStarted = true;
#endif
}

bool SharedWorkerOp::MaybeStart(RemoteWorkerChild* aOwner,
                                RemoteWorkerState& aState) {
  MOZ_ASSERT(!mStarted);
  MOZ_ASSERT(aOwner);
  // Thread: We are on the Worker Launcher thread.

  // Return false, indicating we should queue this op if our current state is
  // pending and this isn't a termination op (which should skip the line).
  if (aState.is<Pending>() && !IsTerminationOp()) {
    return false;
  }

  // If the worker is already shutting down (which should be unexpected
  // because we should be told new operations after a termination op), just
  // return true to indicate the op should be discarded.
  if (aState.is<Canceled>() || aState.is<Killed>()) {
#ifdef DEBUG
    mStarted = true;
#endif
    return true;
  }

  MOZ_ASSERT(aState.is<Running>() || IsTerminationOp());

  if (mOpArgs.type() == SharedWorkerOpArgs::TSharedWorkerThawOpArgs) {
    aOwner->SetIsThawing(true);
  }

  RefPtr<SharedWorkerOp> self = this;
  RefPtr<RemoteWorkerChild> owner = aOwner;

  nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
      __func__, [self = std::move(self), owner = std::move(owner)]() mutable {
        {
          auto lock = owner->mState.Lock();

          if (NS_WARN_IF(lock->is<Canceled>() || lock->is<Killed>())) {
            self->Cancel();
            return;
          }
        }

        self->StartOnMainThread(owner);
        if (self->mOpArgs.type() ==
            SharedWorkerOpArgs::TSharedWorkerThawOpArgs) {
          owner->SetIsThawing(false);
          owner->RunAllPendingOpsOnMainThread();
        }
      });

  MOZ_ALWAYS_SUCCEEDS(SchedulerGroup::Dispatch(r.forget()));

#ifdef DEBUG
  mStarted = true;
#endif

  return true;
}

void SharedWorkerOp::StartOnMainThread(RefPtr<RemoteWorkerChild>& aOwner) {
  AssertIsOnMainThread();

  if (IsTerminationOp()) {
    aOwner->CloseWorkerOnMainThread();
    return;
  }

  auto lock = aOwner->mState.Lock();
  MOZ_ASSERT(lock->is<Running>());
  if (!lock->is<Running>()) {
    aOwner->ErrorPropagationDispatch(NS_ERROR_DOM_INVALID_STATE_ERR);
    return;
  }

  RefPtr<WorkerPrivate> workerPrivate = lock->as<Running>().mWorkerPrivate;

  MOZ_ASSERT(workerPrivate);

  if (mOpArgs.type() == SharedWorkerOpArgs::TSharedWorkerSuspendOpArgs) {
    workerPrivate->ParentWindowPaused();
  } else if (mOpArgs.type() == SharedWorkerOpArgs::TSharedWorkerResumeOpArgs) {
    workerPrivate->ParentWindowResumed();
  } else if (mOpArgs.type() == SharedWorkerOpArgs::TSharedWorkerFreezeOpArgs) {
    workerPrivate->Freeze(nullptr);
  } else if (mOpArgs.type() == SharedWorkerOpArgs::TSharedWorkerThawOpArgs) {
    workerPrivate->Thaw(nullptr);
  } else if (mOpArgs.type() ==
             SharedWorkerOpArgs::TSharedWorkerPortIdentifierOpArgs) {
    MOZ_CRASH(
        "PortIdentifierOpArgs should not be processed by "
        "StartOnMainThread!!!");
  } else if (mOpArgs.type() ==
             SharedWorkerOpArgs::TSharedWorkerAddWindowIDOpArgs) {
    aOwner->mWindowIDs.AppendElement(
        mOpArgs.get_SharedWorkerAddWindowIDOpArgs().windowID());
    RefPtr<UpdateWindowIDRunnable> r = new UpdateWindowIDRunnable(
        mOpArgs.get_SharedWorkerAddWindowIDOpArgs().windowID(), true);
    Unused << r->Dispatch(workerPrivate);
  } else if (mOpArgs.type() ==
             SharedWorkerOpArgs::TSharedWorkerRemoveWindowIDOpArgs) {
    aOwner->mWindowIDs.RemoveElement(
        mOpArgs.get_SharedWorkerRemoveWindowIDOpArgs().windowID());
    RefPtr<UpdateWindowIDRunnable> r = new UpdateWindowIDRunnable(
        mOpArgs.get_SharedWorkerRemoveWindowIDOpArgs().windowID(), false);
    Unused << r->Dispatch(workerPrivate);
  } else {
    MOZ_CRASH("Unknown SharedWorkerOpArgs type!");
  }

#ifdef DEBUG
  if (!mStarted) {
    mStarted = true;
  }
#endif
}

void SharedWorkerOp::Start(RemoteWorkerNonLifeCycleOpControllerChild* aOwner,
                           RemoteWorkerState& aState) {
  MOZ_ASSERT(!mStarted);
  MOZ_ASSERT(aOwner);
  // Thread: We are on the Worker thread.

  // Only PortIdentifierOp is NonLifeCycle related opertaion.
  MOZ_ASSERT(mOpArgs.type() ==
             SharedWorkerOpArgs::TSharedWorkerPortIdentifierOpArgs);

  // Should never be Pending state.
  MOZ_ASSERT(!aState.is<Pending>());

  // If the worker is already shutting down (which should be unexpected
  // because we should be told new operations after a termination op), just
  // return directly.
  if (aState.is<Canceled>() || aState.is<Killed>()) {
#ifdef DEBUG
    mStarted = true;
#endif
    MessagePort::ForceClose(
        mOpArgs.get_SharedWorkerPortIdentifierOpArgs().portIdentifier());
    return;
  }

  MOZ_ASSERT(aState.is<Running>());

  // RefPtr<WorkerPrivate> workerPrivate = aState.as<Running>().mWorkerPrivate;
  WorkerPrivate* workerPrivate = GetCurrentThreadWorkerPrivate();

  RefPtr<MessagePortIdentifierRunnable> r = new MessagePortIdentifierRunnable(
      aOwner, mOpArgs.get_SharedWorkerPortIdentifierOpArgs().portIdentifier());

  if (NS_WARN_IF(!r->Dispatch(workerPrivate))) {
    aOwner->ErrorPropagation(NS_ERROR_FAILURE);
  }

#ifdef DEBUG
  mStarted = true;
#endif
}

bool SharedWorkerOp::IsTerminationOp() const {
  return mOpArgs.type() == SharedWorkerOpArgs::TSharedWorkerTerminateOpArgs;
}

}  // namespace mozilla::dom
