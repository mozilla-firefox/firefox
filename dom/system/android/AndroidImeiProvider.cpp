/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "AndroidImeiProvider.h"

#include "mozilla/ClearOnShutdown.h"
#include "mozilla/dom/Promise.h"
#include "nsIGlobalObject.h"

namespace mozilla::dom {

static StaticRefPtr<AndroidImeiProvider> sInstance;

/* static */
already_AddRefed<AndroidImeiProvider> AndroidImeiProvider::GetInstance() {
  if (!sInstance) {
    sInstance = new AndroidImeiProvider();
    ClearOnShutdown(&sInstance);
  }

  RefPtr<AndroidImeiProvider> service = sInstance.get();
  return service.forget();
}

NS_IMPL_ISUPPORTS(AndroidImeiProvider, nsIImeiProvider)

NS_IMETHODIMP
AndroidImeiProvider::GetImei(JSContext* aCx, Promise** aResult) {
  NS_ENSURE_ARG_POINTER(aResult);
  *aResult = nullptr;

  if (!XRE_IsParentProcess()) {
    return NS_ERROR_FAILURE;
  }

  nsIGlobalObject* global = xpc::CurrentNativeGlobal(aCx);
  if (NS_WARN_IF(!global)) {
    return NS_ERROR_FAILURE;
  }

  ErrorResult erv;
  RefPtr<Promise> promise = Promise::Create(global, erv);
  if (NS_WARN_IF(erv.Failed())) {
    return erv.StealNSResult();
  }

  promise->MaybeReject(NS_ERROR_NOT_IMPLEMENTED);

  promise.forget(aResult);
  return NS_OK;
}

}  // namespace mozilla::dom
