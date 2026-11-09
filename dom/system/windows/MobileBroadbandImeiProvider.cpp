/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "MobileBroadbandImeiProvider.h"

#include <mbnapi.h>
#include <windows.h>

#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Logging.h"
#include "mozilla/RefPtr.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/mscom/EnsureMTA.h"
#include "nsCOMPtr.h"
#include "nsProxyRelease.h"
#include "nsThreadUtils.h"

namespace mozilla::dom {

static LazyLogModule gMobileBroadbandImeiLog("MobileBroadbandImei");

#define MBNI_LOG(level, ...) \
  MOZ_LOG(gMobileBroadbandImeiLog, mozilla::LogLevel::level, (__VA_ARGS__))

static StaticRefPtr<MobileBroadbandImeiProvider> sInstance;

MobileBroadbandImeiProvider::MobileBroadbandImeiProvider()
    : mManagerFactory(CreateManager) {}

MobileBroadbandImeiProvider::MobileBroadbandImeiProvider(
    ManagerFactory aManagerFactory)
    : mManagerFactory(std::move(aManagerFactory)) {}

/* static */
already_AddRefed<MobileBroadbandImeiProvider>
MobileBroadbandImeiProvider::GetInstance() {
  if (!sInstance) {
    sInstance = new MobileBroadbandImeiProvider();
    ClearOnShutdown(&sInstance);
  }

  RefPtr<MobileBroadbandImeiProvider> service = sInstance.get();
  return service.forget();
}

NS_IMPL_ISUPPORTS(MobileBroadbandImeiProvider, nsIImeiProvider)

/* static */
already_AddRefed<IMbnInterfaceManager>
MobileBroadbandImeiProvider::CreateManager() {
  RefPtr<IMbnInterfaceManager> manager;
  HRESULT hr =
      CoCreateInstance(__uuidof(MbnInterfaceManager), nullptr, CLSCTX_ALL,
                       __uuidof(IMbnInterfaceManager), getter_AddRefs(manager));
  if (FAILED(hr)) {
    MBNI_LOG(Error, "CoCreateInstance() failed: %08lx", hr);
    return nullptr;
  }
  return manager.forget();
}

/* static */
nsresult MobileBroadbandImeiProvider::GetImeiFromManager(
    IMbnInterfaceManager* aManager, nsAString& aImei) {
  SAFEARRAY* interfaces = nullptr;
  HRESULT hr = aManager->GetInterfaces(&interfaces);
  if (FAILED(hr)) {
    MBNI_LOG(Error, "GetInterfaces() failed: %08lx", hr);
    return NS_ERROR_FAILURE;
  }
  if (!interfaces) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }
  auto destroyInterfaces = MakeScopeExit([&] { SafeArrayDestroy(interfaces); });

  LONG lowerBound = 0;
  LONG upperBound = -1;
  if (FAILED(SafeArrayGetLBound(interfaces, 1, &lowerBound)) ||
      FAILED(SafeArrayGetUBound(interfaces, 1, &upperBound))) {
    return NS_ERROR_FAILURE;
  }

  for (LONG i = lowerBound; i <= upperBound; i++) {
    IMbnInterface* rawInterface = nullptr;
    hr = SafeArrayGetElement(interfaces, &i, &rawInterface);
    RefPtr<IMbnInterface> mbnInterface = dont_AddRef(rawInterface);
    if (FAILED(hr) || !mbnInterface) {
      MBNI_LOG(Error, "SafeArrayGetElement(%ld) failed: %08lx", i, hr);
      continue;
    }

    MBN_INTERFACE_CAPS caps{};
    hr = mbnInterface->GetInterfaceCapability(&caps);
    if (FAILED(hr)) {
      MBNI_LOG(Error, "GetInterfaceCapability(%ld) failed: %08lx", i, hr);
      continue;
    }
    auto freeCaps = MakeScopeExit([&] {
      SysFreeString(caps.customDataClass);
      SysFreeString(caps.customBandClass);
      SysFreeString(caps.deviceID);
      SysFreeString(caps.manufacturer);
      SysFreeString(caps.model);
      SysFreeString(caps.firmwareInfo);
    });

    UINT length = SysStringLen(caps.deviceID);
    if (!length) {
      continue;
    }

    aImei.Assign(caps.deviceID, length);
    return NS_OK;
  }

  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
MobileBroadbandImeiProvider::GetImei(JSContext* aCx, Promise** aResult) {
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

  auto promiseHolder =
      MakeRefPtr<nsMainThreadPtrHolder<Promise>>("GetImei promise", promise);

  nsresult rv = NS_DispatchBackgroundTask(
      NS_NewRunnableFunction(
          "MobileBroadbandImeiProvider::GetImei",
          [promiseHolder, managerFactory = mManagerFactory] {
            nsString imei;
            nsresult rv = NS_ERROR_FAILURE;
            mscom::EnsureMTA([&] {
              RefPtr<IMbnInterfaceManager> manager = managerFactory();
              if (!manager) {
                return;
              }
              rv = GetImeiFromManager(manager, imei);
            });

            NS_DispatchToMainThread(NS_NewRunnableFunction(
                "GetImei callback", [rv, promiseHolder, imei] {
                  Promise* promise = promiseHolder.get()->get();
                  if (NS_SUCCEEDED(rv)) {
                    promise->MaybeResolve(imei);
                  } else {
                    promise->MaybeReject(rv);
                  }
                }));
          }),
      NS_DISPATCH_EVENT_MAY_BLOCK);
  if (NS_FAILED(rv)) {
    promise->MaybeReject(rv);
  }

  promise.forget(aResult);
  return NS_OK;
}

}  // namespace mozilla::dom
