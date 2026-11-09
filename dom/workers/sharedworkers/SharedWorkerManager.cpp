/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedWorkerManager.h"

#include "SharedWorkerParent.h"
#include "SharedWorkerService.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/dom/MessagePort.h"
#include "mozilla/dom/PSharedWorker.h"
#include "mozilla/dom/RemoteWorkerController.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "mozilla/ipc/URIUtils.h"
#include "nsIConsoleReportCollector.h"
#include "nsIPrincipal.h"
#include "nsProxyRelease.h"

namespace mozilla::dom {

// static
already_AddRefed<SharedWorkerManagerHolder> SharedWorkerManager::Create(
    SharedWorkerService* aService, nsIEventTarget* aPBackgroundEventTarget,
    const RemoteWorkerData& aData, nsIPrincipal* aLoadingPrincipal,
    const OriginAttributes& aEffectiveStoragePrincipalAttrs) {
  MOZ_ASSERT(NS_IsMainThread());

  RefPtr<SharedWorkerManager> manager =
      new SharedWorkerManager(aPBackgroundEventTarget, aData, aLoadingPrincipal,
                              aEffectiveStoragePrincipalAttrs);

  RefPtr<SharedWorkerManagerHolder> holder =
      new SharedWorkerManagerHolder(manager, aService);
  return holder.forget();
}

SharedWorkerManager::SharedWorkerManager(
    nsIEventTarget* aPBackgroundEventTarget, const RemoteWorkerData& aData,
    nsIPrincipal* aLoadingPrincipal,
    const OriginAttributes& aEffectiveStoragePrincipalAttrs)
    : mPBackgroundEventTarget(aPBackgroundEventTarget),
      mLoadingPrincipal(aLoadingPrincipal),
      mDomain(aData.domain()),
      mEffectiveStoragePrincipalAttrs(aEffectiveStoragePrincipalAttrs),
      mResolvedScriptURL(aData.resolvedScriptURL()),
      mWorkerOptions(aData.workerOptions()),
      mIsSecureContext(aData.isSecureContext()),
      mSuspended(false),
      mFrozen(false) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aLoadingPrincipal);
}

SharedWorkerManager::~SharedWorkerManager() {
  NS_ReleaseOnMainThread("SharedWorkerManager::mLoadingPrincipal",
                         mLoadingPrincipal.forget());
  NS_ProxyRelease("SharedWorkerManager::mRemoteWorkerController",
                  mPBackgroundEventTarget, mRemoteWorkerController.forget());
}

bool SharedWorkerManager::MaybeCreateRemoteWorker(
    const RemoteWorkerData& aData, uint64_t aWindowID,
    UniqueMessagePortId& aPortIdentifier, base::ProcessId aProcessId) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  // Creating remote workers may result in creating new processes, but during
  // parent shutdown that would add just noise, so better bail out.
  if (AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdownConfirmed)) {
    return false;
  }

  if (!mRemoteWorkerController) {
    mRemoteWorkerController =
        RemoteWorkerController::Create(aData, this, aProcessId);
    if (NS_WARN_IF(!mRemoteWorkerController)) {
      return false;
    }
  }

  if (aWindowID) {
    mRemoteWorkerController->AddWindowID(aWindowID);
  }

  mRemoteWorkerController->AddPortIdentifier(aPortIdentifier.release());
  return true;
}

already_AddRefed<SharedWorkerManagerHolder>
SharedWorkerManager::MatchOnMainThread(
    SharedWorkerService* aService, const RemoteWorkerData& aData,
    nsIURI* aScriptURL, nsIPrincipal* aLoadingPrincipal,
    const OriginAttributes& aEffectiveStoragePrincipalAttrs,
    bool* aMatchNameButNotOptions) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aMatchNameButNotOptions);

  bool urlEquals;
  if (NS_FAILED(aScriptURL->Equals(mResolvedScriptURL, &urlEquals))) {
    return nullptr;
  }

  bool match =
      aData.domain() == mDomain && urlEquals &&
      aData.workerOptions().mName == mWorkerOptions.mName &&
      // We want to be sure that the window's principal subsumes the
      // SharedWorker's loading principal and vice versa.
      mLoadingPrincipal->Subsumes(aLoadingPrincipal) &&
      aLoadingPrincipal->Subsumes(mLoadingPrincipal) &&
      mEffectiveStoragePrincipalAttrs == aEffectiveStoragePrincipalAttrs;
  if (!match) {
    return nullptr;
  }

  *aMatchNameButNotOptions =
      aData.workerOptions().mType != mWorkerOptions.mType ||
      aData.workerOptions().mCredentials != mWorkerOptions.mCredentials;

  if (*aMatchNameButNotOptions) {
    return nullptr;
  }

  RefPtr<SharedWorkerManagerHolder> holder =
      new SharedWorkerManagerHolder(this, aService);
  return holder.forget();
}

void SharedWorkerManager::AddActor(SharedWorkerParent* aParent) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  MOZ_ASSERT(aParent);
  MOZ_ASSERT(!mActors.Contains(aParent));

  mActors.AppendElement(aParent);

  if (mLockCount) {
    (void)aParent->SendNotifyLock(true);
  }

  if (mWebTransportCount) {
    (void)aParent->SendNotifyWebTransport(true);
  }

  // NB: We don't update our Suspended/Frozen state here, yet. The aParent is
  // responsible for doing so from SharedWorkerParent::ManagerCreated.
  // XXX But we could avoid iterating all of our actors because if aParent is
  // not frozen and we are, we would just need to thaw ourselves.
}

void SharedWorkerManager::RemoveActor(SharedWorkerParent* aParent) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  MOZ_ASSERT(aParent);
  MOZ_ASSERT(mActors.Contains(aParent));

  uint64_t windowID = aParent->WindowID();
  if (windowID) {
    mRemoteWorkerController->RemoveWindowID(windowID);
  }

  mActors.RemoveElement(aParent);

  if (!mActors.IsEmpty()) {
    // Our remaining actors could be all suspended or frozen.
    UpdateSuspend();
    UpdateFrozen();
    return;
  }
}

void SharedWorkerManager::Terminate() {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  MOZ_ASSERT(mActors.IsEmpty());
  MOZ_ASSERT(mHolders.IsEmpty());

  // mRemoteWorkerController creation can fail. If the creation fails
  // mRemoteWorkerController is nullptr and we should stop termination here.
  if (!mRemoteWorkerController) {
    return;
  }

  mRemoteWorkerController->Terminate();
  mRemoteWorkerController = nullptr;
}

void SharedWorkerManager::UpdateSuspend() {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  MOZ_ASSERT(mRemoteWorkerController);

  uint32_t suspended = 0;

  for (SharedWorkerParent* actor : mActors) {
    if (actor->IsSuspended()) {
      ++suspended;
    }
  }

  // Call Suspend only when all of our actors' windows are suspended and call
  // Resume only when one of them resumes.
  if ((mSuspended && suspended == mActors.Length()) ||
      (!mSuspended && suspended != mActors.Length())) {
    return;
  }

  if (!mSuspended) {
    mSuspended = true;
    mRemoteWorkerController->Suspend();
  } else {
    mSuspended = false;
    mRemoteWorkerController->Resume();
  }
}

void SharedWorkerManager::UpdateFrozen() {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  MOZ_ASSERT(mRemoteWorkerController);

  uint32_t frozen = 0;

  for (SharedWorkerParent* actor : mActors) {
    if (actor->IsFrozen()) {
      ++frozen;
    }
  }

  // Similar to UpdateSuspend, above, we only want to be frozen when all of our
  // actors are frozen.
  if ((mFrozen && frozen == mActors.Length()) ||
      (!mFrozen && frozen != mActors.Length())) {
    return;
  }

  if (!mFrozen) {
    mFrozen = true;
    mRemoteWorkerController->Freeze();
  } else {
    mFrozen = false;
    mRemoteWorkerController->Thaw();
  }
}

void SharedWorkerManager::SetLocaleOverride(
    const nsACString& aLanguageOverride, const nsTArray<nsString>& aLanguages) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  if (mRemoteWorkerController) {
    mRemoteWorkerController->SetLocaleOverride(aLanguageOverride, aLanguages);
  }
}

void SharedWorkerManager::UpdateTimezoneOverride(
    const nsAString& aTimezoneOverride) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  if (mRemoteWorkerController) {
    mRemoteWorkerController->UpdateTimezoneOverride(aTimezoneOverride);
  }
}

bool SharedWorkerManager::IsSecureContext() const { return mIsSecureContext; }

void SharedWorkerManager::CreationFailed() {
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  for (SharedWorkerParent* actor : mActors) {
    (void)actor->SendError(NS_ERROR_FAILURE);
  }
}

void SharedWorkerManager::CreationSucceeded() {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  // Nothing to do here.
}

void SharedWorkerManager::ErrorReceived(const ErrorValue& aValue) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  for (SharedWorkerParent* actor : mActors) {
    (void)actor->SendError(aValue);
  }
}

void SharedWorkerManager::LockNotified(bool aCreated) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  MOZ_ASSERT_IF(!aCreated, mLockCount > 0);

  mLockCount += aCreated ? 1 : -1;

  // Notify only when we either:
  // 1. Got a new lock when nothing were there
  // 2. Lost all locks
  if ((aCreated && mLockCount == 1) || !mLockCount) {
    for (SharedWorkerParent* actor : mActors) {
      (void)actor->SendNotifyLock(aCreated);
    }
  }
};

void SharedWorkerManager::WebTransportNotified(bool aCreated) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  MOZ_ASSERT_IF(!aCreated, mWebTransportCount > 0);

  mWebTransportCount += aCreated ? 1 : -1;

  // Notify only when we either:
  // 1. Got a first WebTransport
  // 2. The last WebTransport goes away
  if ((aCreated && mWebTransportCount == 1) || mWebTransportCount == 0) {
    for (SharedWorkerParent* actor : mActors) {
      (void)actor->SendNotifyWebTransport(aCreated);
    }
  }
};

void SharedWorkerManager::Terminated() {
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  for (SharedWorkerParent* actor : mActors) {
    (void)actor->SendTerminate();
  }
}

void SharedWorkerManager::RegisterHolder(SharedWorkerManagerHolder* aHolder) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aHolder);
  MOZ_ASSERT(!mHolders.Contains(aHolder));

  mHolders.AppendElement(aHolder);
}

void SharedWorkerManager::UnregisterHolder(SharedWorkerManagerHolder* aHolder) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aHolder);
  MOZ_ASSERT(mHolders.Contains(aHolder));

  mHolders.RemoveElement(aHolder);

  if (!mHolders.IsEmpty()) {
    return;
  }

  // Time to go.

  aHolder->Service()->RemoveWorkerManagerOnMainThread(this);

  RefPtr<SharedWorkerManager> self = this;
  mPBackgroundEventTarget->Dispatch(
      NS_NewRunnableFunction(
          "SharedWorkerService::RemoveWorkerManagerOnMainThread",
          [self]() { self->Terminate(); }),
      NS_DISPATCH_NORMAL);
}

SharedWorkerManagerHolder::SharedWorkerManagerHolder(
    SharedWorkerManager* aManager, SharedWorkerService* aService)
    : mManager(aManager), mService(aService) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aManager);
  MOZ_ASSERT(aService);

  aManager->RegisterHolder(this);
}

SharedWorkerManagerHolder::~SharedWorkerManagerHolder() {
  MOZ_ASSERT(NS_IsMainThread());
  mManager->UnregisterHolder(this);
}

SharedWorkerManagerWrapper::SharedWorkerManagerWrapper(
    already_AddRefed<SharedWorkerManagerHolder> aHolder)
    : mHolder(aHolder) {
  MOZ_ASSERT(NS_IsMainThread());
}

SharedWorkerManagerWrapper::~SharedWorkerManagerWrapper() {
  NS_ReleaseOnMainThread("SharedWorkerManagerWrapper::mHolder",
                         mHolder.forget());
}

}  // namespace mozilla::dom
