/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedWorkerService.h"

#include "mozilla/BasePrincipal.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/MessagePort.h"
#include "mozilla/dom/MessagePortParent.h"
#include "mozilla/dom/RemoteWorkerManager.h"  // RemoteWorkerManager::GetRemoteType
#include "mozilla/dom/RemoteWorkerTypes.h"
#include "mozilla/dom/SharedWorkerManager.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "mozilla/ipc/URIUtils.h"
#include "nsIPrincipal.h"
#include "nsProxyRelease.h"

namespace mozilla {

using namespace ipc;

namespace dom {

namespace {

StaticMutex sSharedWorkerMutex;

StaticRefPtr<SharedWorkerService> sSharedWorkerService;

class GetOrCreateWorkerManagerRunnable final : public Runnable {
 public:
  GetOrCreateWorkerManagerRunnable(
      SharedWorkerService* aService,
      ThreadsafeContentParentHandle* aContentParentHandle,
      SharedWorkerParent* aActor, const RemoteWorkerData& aData,
      uint64_t aWindowID, const MessagePortIdentifier& aPortIdentifier)
      : Runnable("GetOrCreateWorkerManagerRunnable"),
        mBackgroundEventTarget(GetCurrentSerialEventTarget()),
        mService(aService),
        mContentParentHandle(aContentParentHandle),
        mActor(aActor),
        mData(aData),
        mWindowID(aWindowID),
        mPortIdentifier(aPortIdentifier) {}

  NS_IMETHOD
  Run() {
    mService->GetOrCreateWorkerManagerOnMainThread(
        mBackgroundEventTarget, mContentParentHandle, mActor, mData, mWindowID,
        mPortIdentifier);

    return NS_OK;
  }

 private:
  nsCOMPtr<nsIEventTarget> mBackgroundEventTarget;
  RefPtr<SharedWorkerService> mService;
  RefPtr<ThreadsafeContentParentHandle> mContentParentHandle;
  RefPtr<SharedWorkerParent> mActor;
  RemoteWorkerData mData;
  uint64_t mWindowID;
  UniqueMessagePortId mPortIdentifier;
};

class WorkerManagerCreatedRunnable final : public Runnable {
 public:
  WorkerManagerCreatedRunnable(
      already_AddRefed<SharedWorkerManagerWrapper> aManagerWrapper,
      SharedWorkerParent* aActor, const RemoteWorkerData& aData,
      uint64_t aWindowID, UniqueMessagePortId& aPortIdentifier)
      : Runnable("WorkerManagerCreatedRunnable"),
        mManagerWrapper(aManagerWrapper),
        mActor(aActor),
        mData(aData),
        mWindowID(aWindowID),
        mPortIdentifier(std::move(aPortIdentifier)) {}

  NS_IMETHOD
  Run() {
    AssertIsOnBackgroundThread();

    if (NS_WARN_IF(
            !mActor->CanSend() ||
            !mManagerWrapper->Manager()->MaybeCreateRemoteWorker(
                mData, mWindowID, mPortIdentifier, mActor->OtherPid()))) {
      // Letting mPortIdentifier force-close the port would create a
      // PBackgroundChild on this thread, blocking its shutdown.
      MessagePortIdentifier port = mPortIdentifier.release();
      (void)NS_WARN_IF(!MessagePortParent::ForceClose(
          port.uuid(), port.destinationUuid(), port.sequenceId()));

      // If we cannot send, the error won't arrive, but we may log something.
      mActor->ErrorPropagation(NS_ERROR_FAILURE);
      return NS_OK;
    }

    mManagerWrapper->Manager()->AddActor(mActor);
    mActor->ManagerCreated(mManagerWrapper.forget());
    return NS_OK;
  }

 private:
  RefPtr<SharedWorkerManagerWrapper> mManagerWrapper;
  RefPtr<SharedWorkerParent> mActor;
  RemoteWorkerData mData;
  uint64_t mWindowID;
  UniqueMessagePortId mPortIdentifier;
};

class ErrorPropagationRunnable final : public Runnable {
 public:
  ErrorPropagationRunnable(SharedWorkerParent* aActor, nsresult aError)
      : Runnable("ErrorPropagationRunnable"), mActor(aActor), mError(aError) {}

  NS_IMETHOD
  Run() {
    AssertIsOnBackgroundThread();
    mActor->ErrorPropagation(mError);
    return NS_OK;
  }

 private:
  RefPtr<SharedWorkerParent> mActor;
  nsresult mError;
};

}  // namespace

/* static */
SharedWorkerService::SharedWorkerService() = default;
SharedWorkerService::~SharedWorkerService() = default;

already_AddRefed<SharedWorkerService> SharedWorkerService::GetOrCreate() {
  AssertIsOnBackgroundThread();

  StaticMutexAutoLock lock(sSharedWorkerMutex);

  if (!sSharedWorkerService) {
    sSharedWorkerService = new SharedWorkerService();
    // ClearOnShutdown can only be called on main thread
    nsresult rv = SchedulerGroup::Dispatch(NS_NewRunnableFunction(
        "RegisterSharedWorkerServiceClearOnShutdown", []() {
          StaticMutexAutoLock lock(sSharedWorkerMutex);
          MOZ_ASSERT(sSharedWorkerService);
          ClearOnShutdown(&sSharedWorkerService);
        }));
    (void)NS_WARN_IF(NS_FAILED(rv));
  }

  RefPtr<SharedWorkerService> instance = sSharedWorkerService;
  return instance.forget();
}

/* static */
SharedWorkerService* SharedWorkerService::Get() {
  StaticMutexAutoLock lock(sSharedWorkerMutex);

  MOZ_ASSERT(sSharedWorkerService);
  return sSharedWorkerService;
}

void SharedWorkerService::GetOrCreateWorkerManager(
    SharedWorkerParent* aActor, const RemoteWorkerData& aData,
    uint64_t aWindowID, const MessagePortIdentifier& aPortIdentifier) {
  AssertIsOnBackgroundThread();

  RefPtr<ThreadsafeContentParentHandle> contentParentHandle =
      BackgroundParent::GetContentParentHandle(aActor->Manager());

  // The real check happens on main-thread.
  RefPtr<GetOrCreateWorkerManagerRunnable> r =
      new GetOrCreateWorkerManagerRunnable(this, contentParentHandle, aActor,
                                           aData, aWindowID, aPortIdentifier);

  nsresult rv = SchedulerGroup::Dispatch(r.forget());
  (void)NS_WARN_IF(NS_FAILED(rv));
}

void SharedWorkerService::GetOrCreateWorkerManagerOnMainThread(
    nsIEventTarget* aBackgroundEventTarget,
    ThreadsafeContentParentHandle* aContentParentHandle,
    SharedWorkerParent* aActor, RemoteWorkerData aData, uint64_t aWindowID,
    UniqueMessagePortId& aPortIdentifier) {
  AssertIsOnMainThread();
  MOZ_ASSERT(aBackgroundEventTarget);
  MOZ_ASSERT(aActor);

  RefPtr<ContentParent> contentParent =
      aContentParentHandle ? aContentParentHandle->GetContentParent() : nullptr;
  if (aContentParentHandle && !contentParent) {
    ErrorPropagationOnMainThread(aBackgroundEventTarget, aActor,
                                 NS_ERROR_UNEXPECTED);
    return;
  }

  auto principalOrErr = PrincipalInfoToPrincipal(aData.principalInfo());
  if (NS_WARN_IF(principalOrErr.isErr())) {
    ErrorPropagationOnMainThread(aBackgroundEventTarget, aActor,
                                 principalOrErr.unwrapErr());
    return;
  }

  auto partitionedPrincipalOrErr =
      PrincipalInfoToPrincipal(aData.partitionedPrincipalInfo());
  if (NS_WARN_IF(partitionedPrincipalOrErr.isErr())) {
    ErrorPropagationOnMainThread(aBackgroundEventTarget, aActor,
                                 partitionedPrincipalOrErr.unwrapErr());
    return;
  }

  auto loadingPrincipalOrErr =
      PrincipalInfoToPrincipal(aData.loadingPrincipalInfo());
  if (NS_WARN_IF(loadingPrincipalOrErr.isErr())) {
    ErrorPropagationOnMainThread(aBackgroundEventTarget, aActor,
                                 loadingPrincipalOrErr.unwrapErr());
    return;
  }

  nsCOMPtr<nsIPrincipal> principal = principalOrErr.unwrap();
  nsCOMPtr<nsIPrincipal> loadingPrincipal = loadingPrincipalOrErr.unwrap();
  nsCOMPtr<nsIPrincipal> partitionedPrincipal =
      partitionedPrincipalOrErr.unwrap();

  // If this is being created by a content process, validate that that process
  // would be allowed to load the SharedWorker.
  //
  // This may be hit for cross-origin loads triggered by web content which would
  // fail later in the load process. As those loads would fail anyway, it should
  // be OK to fail early here in those cases.
  if (contentParent) {
    if (NS_WARN_IF(!contentParent->ValidatePrincipal(loadingPrincipal, {})) ||
        NS_WARN_IF(!contentParent->ValidatePrincipal(principal, {})) ||
        NS_WARN_IF(
            !contentParent->ValidatePrincipal(partitionedPrincipal, {}))) {
      ErrorPropagationOnMainThread(aBackgroundEventTarget, aActor,
                                   NS_ERROR_DOM_SECURITY_ERR);
      return;
    }
  }

  // Determine the remote type this SharedWorker should load into, and clobber
  // the ununsed remoteType field from the passed-in `RemoteWorkerData` with it.
  auto remoteType = RemoteWorkerManager::GetRemoteType(
      principal, WorkerKind::WorkerKindShared,
      contentParent ? contentParent->GetRemoteType() : RemoteType::NotRemote());
  if (NS_WARN_IF(remoteType.isErr())) {
    ErrorPropagationOnMainThread(aBackgroundEventTarget, aActor,
                                 remoteType.unwrapErr());
    return;
  }
  aData.remoteType() = remoteType.unwrap();

  nsCOMPtr<nsIPrincipal> effectiveStoragePrincipal = partitionedPrincipal;
  if (aData.useRegularPrincipal()) {
    effectiveStoragePrincipal = loadingPrincipal;
  }

  nsCOMPtr<nsIURI> resolvedScriptURL(aData.resolvedScriptURL());
  if (!resolvedScriptURL || !aData.baseScriptURL()) {
    ErrorPropagationOnMainThread(aBackgroundEventTarget, aActor,
                                 NS_ERROR_FAILURE);
    return;
  }

  // Let's see if there is already a SharedWorker to share.
  RefPtr<SharedWorkerManagerHolder> managerHolder;
  for (SharedWorkerManager* workerManager : mWorkerManagers) {
    bool matchNameButNotOptions = false;

    managerHolder = workerManager->MatchOnMainThread(
        this, aData, resolvedScriptURL, loadingPrincipal,
        BasePrincipal::Cast(effectiveStoragePrincipal)->OriginAttributesRef(),
        &matchNameButNotOptions);
    if (managerHolder) {
      break;
    }

    if (matchNameButNotOptions) {
      MismatchOptionsErrorPropagationOnMainThread(aBackgroundEventTarget,
                                                  aActor);
      return;
    }
  }

  // Let's create a new one.
  if (!managerHolder) {
    managerHolder = SharedWorkerManager::Create(
        this, aBackgroundEventTarget, aData, loadingPrincipal,
        BasePrincipal::Cast(effectiveStoragePrincipal)->OriginAttributesRef());

    mWorkerManagers.AppendElement(managerHolder->Manager());
  } else {
    // We are attaching the actor to an existing one.
    if (managerHolder->Manager()->IsSecureContext() !=
        aData.isSecureContext()) {
      ErrorPropagationOnMainThread(aBackgroundEventTarget, aActor,
                                   NS_ERROR_DOM_SECURITY_ERR);
      return;
    }
  }

  RefPtr<SharedWorkerManagerWrapper> wrapper =
      new SharedWorkerManagerWrapper(managerHolder.forget());

  RefPtr<WorkerManagerCreatedRunnable> r = new WorkerManagerCreatedRunnable(
      wrapper.forget(), aActor, aData, aWindowID, aPortIdentifier);
  aBackgroundEventTarget->Dispatch(r.forget(), NS_DISPATCH_NORMAL);
}

void SharedWorkerService::ErrorPropagationOnMainThread(
    nsIEventTarget* aBackgroundEventTarget, SharedWorkerParent* aActor,
    nsresult aError) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aBackgroundEventTarget);
  MOZ_ASSERT(aActor);
  MOZ_ASSERT(NS_FAILED(aError));

  RefPtr<ErrorPropagationRunnable> r =
      new ErrorPropagationRunnable(aActor, aError);
  aBackgroundEventTarget->Dispatch(r.forget(), NS_DISPATCH_NORMAL);
}

void SharedWorkerService::MismatchOptionsErrorPropagationOnMainThread(
    nsIEventTarget* aBackgroundEventTarget, SharedWorkerParent* aActor) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aBackgroundEventTarget);
  MOZ_ASSERT(aActor);

  aBackgroundEventTarget->Dispatch(
      NS_NewRunnableFunction(__func__,
                             [aActor = RefPtr(aActor)] {
                               AssertIsOnBackgroundThread();
                               aActor->MismatchOptionsErrorPropagation();
                             }),
      NS_DISPATCH_NORMAL);
}

void SharedWorkerService::RemoveWorkerManagerOnMainThread(
    SharedWorkerManager* aManager) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aManager);
  MOZ_ASSERT(mWorkerManagers.Contains(aManager));

  mWorkerManagers.RemoveElement(aManager);
}

}  // namespace dom
}  // namespace mozilla
