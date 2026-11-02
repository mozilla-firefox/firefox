/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "StorageAccessPermissionRequest.h"

#include <cstdlib>

#include "mozilla/StaticPrefs_dom.h"
#include "nsGlobalWindowInner.h"

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_INHERITED(StorageAccessPermissionRequest,
                                   ContentPermissionRequestBase)

NS_IMPL_ISUPPORTS_CYCLE_COLLECTION_INHERITED_0(StorageAccessPermissionRequest,
                                               ContentPermissionRequestBase)

StorageAccessPermissionRequest::StorageAccessPermissionRequest(
    nsPIDOMWindowInner* aWindow, nsIPrincipal* aNodePrincipal, bool aFrameOnly,
    AllowCallback&& aAllowCallback, CancelCallback&& aCancelCallback)
    : ContentPermissionRequestBase(aNodePrincipal, aWindow,
                                   "dom.storage_access"_ns,
                                   "storage-access"_ns),
      mAllowCallback(std::move(aAllowCallback)),
      mCancelCallback(std::move(aCancelCallback)),
      mCallbackCalled(false) {
  mOptions.SetLength(2);
  // Location 0 is no longer sent
  if (aFrameOnly) {
    mOptions.ElementAt(1) = u"1"_ns;
  }
  mPermissionRequests.AppendElement(PermissionRequest(mType, mOptions));
}

NS_IMETHODIMP
StorageAccessPermissionRequest::Cancel() {
  if (!mCallbackCalled) {
    mCallbackCalled = true;
    mCancelCallback();
  }
  return NS_OK;
}

NS_IMETHODIMP
StorageAccessPermissionRequest::Allow(JS::Handle<JS::Value> aChoices) {
  nsTArray<PermissionChoice> choices;
  nsresult rv = TranslateChoices(aChoices, mPermissionRequests, choices);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // There is no support to allow grants automatically from the prompting code
  // path.

  if (!mCallbackCalled) {
    mCallbackCalled = true;
    if (choices.Length() == 1 && choices[0].choice().EqualsLiteral("allow")) {
      mAllowCallback();
    }
  }
  return NS_OK;
}

void StorageAccessPermissionRequest::GetPermissionOptions(
    nsTArray<nsString>& aOptions) const {
  aOptions.AppendElements(mOptions);
}

RefPtr<StorageAccessPermissionRequest::AutoGrantDelayPromise>
StorageAccessPermissionRequest::MaybeDelayAutomaticGrants() {
  RefPtr<AutoGrantDelayPromise::Private> p =
      new AutoGrantDelayPromise::Private(__func__);

  unsigned simulatedDelay = CalculateSimulatedDelay();
  if (simulatedDelay) {
    nsCOMPtr<nsITimer> timer;
    RefPtr<AutoGrantDelayPromise::Private> promise = p;
    nsresult rv = NS_NewTimerWithFuncCallback(
        getter_AddRefs(timer),
        [](nsITimer* aTimer, void* aClosure) -> void {
          auto* promise =
              static_cast<AutoGrantDelayPromise::Private*>(aClosure);
          promise->Resolve(true, __func__);
          NS_RELEASE(aTimer);
          NS_RELEASE(promise);
        },
        promise, simulatedDelay, nsITimer::TYPE_ONE_SHOT,
        "DelayedAllowAutoGrantCallback"_ns);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      p->Reject(false, __func__);
    } else {
      // Leak the references here! We'll release them inside the callback.
      timer.forget().leak();
      promise.forget().leak();
    }
  } else {
    p->Resolve(false, __func__);
  }
  return p;
}

already_AddRefed<StorageAccessPermissionRequest>
StorageAccessPermissionRequest::Create(nsPIDOMWindowInner* aWindow,
                                       AllowCallback&& aAllowCallback,
                                       CancelCallback&& aCancelCallback) {
  if (!aWindow) {
    return nullptr;
  }
  nsGlobalWindowInner* win = nsGlobalWindowInner::Cast(aWindow);

  return Create(aWindow, win->GetPrincipal(), std::move(aAllowCallback),
                std::move(aCancelCallback));
}

already_AddRefed<StorageAccessPermissionRequest>
StorageAccessPermissionRequest::Create(nsPIDOMWindowInner* aWindow,
                                       nsIPrincipal* aPrincipal,
                                       AllowCallback&& aAllowCallback,
                                       CancelCallback&& aCancelCallback) {
  return Create(aWindow, aPrincipal, true, std::move(aAllowCallback),
                std::move(aCancelCallback));
}

already_AddRefed<StorageAccessPermissionRequest>
StorageAccessPermissionRequest::Create(nsPIDOMWindowInner* aWindow,
                                       nsIPrincipal* aPrincipal,
                                       bool aFrameOnly,
                                       AllowCallback&& aAllowCallback,
                                       CancelCallback&& aCancelCallback) {
  if (!aWindow) {
    return nullptr;
  }

  if (!aPrincipal) {
    return nullptr;
  }

  RefPtr<StorageAccessPermissionRequest> request =
      new StorageAccessPermissionRequest(aWindow, aPrincipal, aFrameOnly,
                                         std::move(aAllowCallback),
                                         std::move(aCancelCallback));
  return request.forget();
}

unsigned StorageAccessPermissionRequest::CalculateSimulatedDelay() {
  if (!StaticPrefs::dom_storage_access_auto_grants_delayed()) {
    return 0;
  }

  // Generate a random time value that is at least 0 and and most 3 seconds.
  std::srand(static_cast<unsigned>(PR_Now()));

  const unsigned kMin = 0;
  const unsigned kMax = 3000;
  const unsigned random = std::abs(std::rand());

  return kMin + random % (kMax - kMin);
}

}  // namespace mozilla::dom
