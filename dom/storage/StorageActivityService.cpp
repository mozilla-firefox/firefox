/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "StorageActivityService.h"

#include "mozilla/BasePrincipal.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/BackgroundUtils.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "nsCOMPtr.h"
#include "nsComponentManagerUtils.h"
#include "nsIMutableArray.h"
#include "nsIObserverService.h"
#include "nsIPrincipal.h"
#include "nsIUserIdleService.h"
#include "nsSupportsPrimitives.h"
#include "nsXPCOM.h"

// This const is used to know when origin activities should be purged because
// too old. This value should be in sync with what the UI needs.
#define TIME_MAX_SECS 86400 /* 24 hours */

namespace mozilla::dom {

static StaticRefPtr<StorageActivityService> gStorageActivityService;
static bool gStorageActivityShutdown = false;

/* static */
void StorageActivityService::SendActivity(nsIPrincipal* aPrincipal) {
  if (!aPrincipal || BasePrincipal::Cast(aPrincipal)->Kind() !=
                         BasePrincipal::eContentPrincipal) {
    // Only content principals.
    return;
  }

  if (!NS_IsMainThread()) {
    SchedulerGroup::Dispatch(NS_NewRunnableFunction(
        "StorageActivityService::SendActivity",
        [principal = nsCOMPtr{aPrincipal}]() { SendActivity(principal); }));
    return;
  }

  RefPtr<StorageActivityService> service = GetOrCreate();
  if (NS_WARN_IF(!service)) {
    return;
  }

  service->SendActivityInternal(aPrincipal);
}

/* static */
void StorageActivityService::SendActivity(
    const mozilla::ipc::PrincipalInfo& aPrincipalInfo) {
  if (aPrincipalInfo.type() !=
      mozilla::ipc::PrincipalInfo::TContentPrincipalInfo) {
    // only content principal.
    return;
  }

  auto principalOrErr = mozilla::ipc::PrincipalInfoToPrincipal(aPrincipalInfo);
  if (NS_WARN_IF(principalOrErr.isErr())) {
    return;
  }

  nsCOMPtr<nsIPrincipal> principal = principalOrErr.unwrap();
  SendActivity(principal);
}

/* static */
void StorageActivityService::SendActivity(const nsACString& aOrigin) {
  MOZ_ASSERT(XRE_IsParentProcess());

  nsCString origin;
  origin.Assign(aOrigin);

  RefPtr<Runnable> r = NS_NewRunnableFunction(
      "StorageActivityService::SendActivity", [origin]() {
        MOZ_ASSERT(NS_IsMainThread());

        RefPtr<StorageActivityService> service = GetOrCreate();
        if (NS_WARN_IF(!service)) {
          return;
        }

        service->SendActivityInternal(origin);
      });

  if (NS_IsMainThread()) {
    (void)r->Run();
  } else {
    NS_DispatchToMainThread(r.forget());
  }
}

/* static */
already_AddRefed<StorageActivityService> StorageActivityService::GetOrCreate() {
  MOZ_ASSERT(NS_IsMainThread());

  if (!gStorageActivityService && !gStorageActivityShutdown) {
    RefPtr<StorageActivityService> service = new StorageActivityService();

    nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
    if (NS_WARN_IF(!obs)) {
      return nullptr;
    }

    nsresult rv =
        obs->AddObserver(service, NS_XPCOM_SHUTDOWN_OBSERVER_ID, true);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return nullptr;
    }

    gStorageActivityService = service;
  }

  RefPtr<StorageActivityService> service = gStorageActivityService;
  return service.forget();
}

StorageActivityService::StorageActivityService() {
  MOZ_ASSERT(NS_IsMainThread());
}

StorageActivityService::~StorageActivityService() {
  MOZ_ASSERT(NS_IsMainThread());
}

void StorageActivityService::SendActivityInternal(nsIPrincipal* aPrincipal) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(BasePrincipal::Cast(aPrincipal)->Kind() ==
             BasePrincipal::eContentPrincipal);

  if (!XRE_IsParentProcess()) {
    SendActivityToParent(aPrincipal);
    return;
  }

  nsAutoCString origin;
  nsresult rv = aPrincipal->GetOrigin(origin);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  SendActivityInternal(origin);
}

void StorageActivityService::SendActivityInternal(const nsACString& aOrigin) {
  MOZ_ASSERT(XRE_IsParentProcess());

  bool shouldAddObserver = mActivities.Count() == 0;
  mActivities.InsertOrUpdate(aOrigin, PR_Now());

  if (shouldAddObserver) {
    nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
    if (NS_WARN_IF(!obs)) {
      return;
    }

    obs->AddObserver(this, OBSERVER_TOPIC_IDLE_DAILY, true);
  }
}

void StorageActivityService::SendActivityToParent(nsIPrincipal* aPrincipal) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!XRE_IsParentProcess());

  ::mozilla::ipc::PBackgroundChild* actor =
      ::mozilla::ipc::BackgroundChild::GetOrCreateForCurrentThread();
  if (NS_WARN_IF(!actor)) {
    return;
  }

  if (!::mozilla::ipc::BackgroundChild::ValidatePrincipal(aPrincipal, {})) {
    MOZ_ASSERT_UNREACHABLE("ValidatePrincipal failure in SendActivityToParent");
    return;
  }

  actor->SendStorageActivity(WrapNotNull(aPrincipal));
}

NS_IMETHODIMP
StorageActivityService::Observe(nsISupports* aSubject, const char* aTopic,
                                const char16_t* aData) {
  MOZ_ASSERT(NS_IsMainThread());

  if (!strcmp(aTopic, OBSERVER_TOPIC_IDLE_DAILY)) {
    CleanUp();
    return NS_OK;
  }

  MOZ_ASSERT(!strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID));

  gStorageActivityShutdown = true;
  gStorageActivityService = nullptr;
  return NS_OK;
}

void StorageActivityService::CleanUp() {
  MOZ_ASSERT(NS_IsMainThread());

  uint64_t now = PR_Now();

  for (auto iter = mActivities.Iter(); !iter.Done(); iter.Next()) {
    if ((now - iter.UserData()) / PR_USEC_PER_SEC > TIME_MAX_SECS) {
      iter.Remove();
    }
  }

  // If no activities, remove the observer.
  if (mActivities.Count() == 0) {
    nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
    if (obs) {
      obs->RemoveObserver(this, OBSERVER_TOPIC_IDLE_DAILY);
    }
  }
}

NS_IMETHODIMP
StorageActivityService::GetActiveOrigins(PRTime aFrom, PRTime aTo,
                                         nsIArray** aRetval) {
  uint64_t now = PR_Now();
  if (((now - aFrom) / PR_USEC_PER_SEC) > TIME_MAX_SECS || aFrom >= aTo) {
    return NS_ERROR_INVALID_ARG;
  }

  // Remove expired entries first.
  CleanUp();

  nsresult rv = NS_OK;
  nsCOMPtr<nsIMutableArray> devices =
      do_CreateInstance(NS_ARRAY_CONTRACTID, &rv);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  for (const auto& activityEntry : mActivities) {
    if (activityEntry.GetData() >= aFrom && activityEntry.GetData() <= aTo) {
      RefPtr<BasePrincipal> principal =
          BasePrincipal::CreateContentPrincipal(activityEntry.GetKey());
      MOZ_ASSERT(principal);

      rv = devices->AppendElement(principal);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
    }
  }

  devices.forget(aRetval);
  return NS_OK;
}

NS_IMETHODIMP
StorageActivityService::MoveOriginInTime(nsIPrincipal* aPrincipal,
                                         PRTime aWhen) {
  if (!XRE_IsParentProcess()) {
    return NS_ERROR_FAILURE;
  }

  nsAutoCString origin;
  nsresult rv = aPrincipal->GetOrigin(origin);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  mActivities.InsertOrUpdate(origin, aWhen);
  return NS_OK;
}

NS_IMETHODIMP
StorageActivityService::TestOnlyReset() {
  const bool shouldRemoveObserver = mActivities.Count() > 0;
  mActivities.Clear();
  if (shouldRemoveObserver) {
    nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
    if (obs) {
      obs->RemoveObserver(this, OBSERVER_TOPIC_IDLE_DAILY);
    }
  }
  return NS_OK;
}

NS_INTERFACE_MAP_BEGIN(StorageActivityService)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIStorageActivityService)
  NS_INTERFACE_MAP_ENTRY(nsIStorageActivityService)
  NS_INTERFACE_MAP_ENTRY(nsIObserver)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
NS_INTERFACE_MAP_END

NS_IMPL_ADDREF(StorageActivityService)
NS_IMPL_RELEASE(StorageActivityService)

}  // namespace mozilla::dom
