/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RemoteWorkerControllerParent.h"

#include <utility>

#include "mozilla/Assertions.h"
#include "mozilla/dom/FetchEventOpParent.h"
#include "mozilla/dom/RemoteWorkerParent.h"
#include "mozilla/dom/ServiceWorkerOpPromise.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "nsCOMPtr.h"
#include "nsDebug.h"
#include "nsError.h"
#include "nsThreadUtils.h"

namespace mozilla {

using namespace ipc;

namespace dom {

RemoteWorkerControllerParent::RemoteWorkerControllerParent(
    const RemoteWorkerData& aRemoteWorkerData)
    : mRemoteWorkerController(RemoteWorkerController::Create(
          aRemoteWorkerData, this, 0 /* random process ID */)) {
  AssertIsInMainProcess();
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mRemoteWorkerController);
}

RefPtr<RemoteWorkerParent> RemoteWorkerControllerParent::GetRemoteWorkerParent()
    const {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mRemoteWorkerController);

  return mRemoteWorkerController->mActor;
}

void RemoteWorkerControllerParent::MaybeSendSetServiceWorkerSkipWaitingFlag(
    std::function<void(bool)>&& aCallback) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aCallback);

  if (!mIPCActive) {
    aCallback(false);
    return;
  }

  SendSetServiceWorkerSkipWaitingFlag()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [callback = std::move(aCallback)](
          const SetServiceWorkerSkipWaitingFlagPromise::ResolveOrRejectValue&
              aResult) {
        callback(aResult.IsResolve() ? aResult.ResolveValue() : false);
      });
}

RemoteWorkerControllerParent::~RemoteWorkerControllerParent() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mIPCActive);
  MOZ_ASSERT(!mRemoteWorkerController);
}

PFetchEventOpParent* RemoteWorkerControllerParent::AllocPFetchEventOpParent(
    const ParentToParentServiceWorkerFetchEventOpArgs& aArgs) {
  AssertIsOnBackgroundThread();

  RefPtr<FetchEventOpParent> actor = new FetchEventOpParent();
  return actor.forget().take();
}

IPCResult RemoteWorkerControllerParent::RecvPFetchEventOpConstructor(
    PFetchEventOpParent* aActor,
    const ParentToParentServiceWorkerFetchEventOpArgs& aArgs) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);

  RefPtr<FetchEventOpParent> realFetchOp =
      mozilla::ipc::ActorCast<FetchEventOpParent>(aActor);
  mRemoteWorkerController->ExecServiceWorkerFetchEventOp(aArgs, realFetchOp)
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [fetchOp = std::move(realFetchOp)](
                 ServiceWorkerFetchEventOpPromise::ResolveOrRejectValue&&
                     aResult) {
               if (NS_WARN_IF(aResult.IsReject())) {
                 MOZ_ASSERT(NS_FAILED(aResult.RejectValue()));
                 (void)fetchOp->Send__delete__(fetchOp, aResult.RejectValue());
                 return;
               }

               (void)fetchOp->Send__delete__(fetchOp, aResult.ResolveValue());
             });

  return IPC_OK();
}

bool RemoteWorkerControllerParent::DeallocPFetchEventOpParent(
    PFetchEventOpParent* aActor) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(aActor);

  RefPtr<FetchEventOpParent> actor =
      dont_AddRef(mozilla::ipc::ActorCast<FetchEventOpParent>(aActor));
  return true;
}

IPCResult RemoteWorkerControllerParent::RecvExecServiceWorkerOp(
    ServiceWorkerOpArgs&& aArgs, ExecServiceWorkerOpResolver&& aResolve) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mIPCActive);
  MOZ_ASSERT(mRemoteWorkerController);

  mRemoteWorkerController->ExecServiceWorkerOp(std::move(aArgs))
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [resolve = std::move(aResolve)](
                 ServiceWorkerOpPromise::ResolveOrRejectValue&& aResult) {
               if (NS_WARN_IF(aResult.IsReject())) {
                 MOZ_ASSERT(NS_FAILED(aResult.RejectValue()));
                 resolve(aResult.RejectValue());
                 return;
               }

               resolve(aResult.ResolveValue());
             });

  return IPC_OK();
}

IPCResult RemoteWorkerControllerParent::RecvShutdown(
    ShutdownResolver&& aResolve) {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(mIPCActive);
  MOZ_ASSERT(mRemoteWorkerController);

  mIPCActive = false;

  mRemoteWorkerController->Shutdown();
  mRemoteWorkerController = nullptr;

  aResolve(true);

  return IPC_OK();
}

IPCResult RemoteWorkerControllerParent::Recv__delete__() {
  AssertIsOnBackgroundThread();
  MOZ_ASSERT(!mIPCActive);
  MOZ_ASSERT(!mRemoteWorkerController);

  return IPC_OK();
}

void RemoteWorkerControllerParent::ActorDestroy(ActorDestroyReason aReason) {
  AssertIsOnBackgroundThread();

  if (NS_WARN_IF(mIPCActive)) {
    mIPCActive = false;
  }

  if (NS_WARN_IF(mRemoteWorkerController)) {
    mRemoteWorkerController->Shutdown();
    mRemoteWorkerController = nullptr;
  }
}

void RemoteWorkerControllerParent::CreationFailed() {
  AssertIsOnBackgroundThread();

  if (!mIPCActive) {
    return;
  }

  (void)SendCreationFailed();
}

void RemoteWorkerControllerParent::CreationSucceeded() {
  AssertIsOnBackgroundThread();

  if (!mIPCActive) {
    return;
  }

  (void)SendCreationSucceeded();
}

void RemoteWorkerControllerParent::ErrorReceived(const ErrorValue& aValue) {
  AssertIsOnBackgroundThread();

  if (!mIPCActive) {
    return;
  }

  (void)SendErrorReceived(aValue);
}

void RemoteWorkerControllerParent::Terminated() {
  AssertIsOnBackgroundThread();

  if (!mIPCActive) {
    return;
  }

  (void)SendTerminated();
}

}  // namespace dom
}  // namespace mozilla
