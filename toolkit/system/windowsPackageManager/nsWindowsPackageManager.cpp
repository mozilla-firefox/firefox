/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsWindowsPackageManager.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/Logging.h"
#include "mozilla/WinHeaderOnlyUtils.h"
#include <wrl.h>
#include <windows.management.deployment.h>
#ifndef __MINGW32__
#  include "nsProxyRelease.h"
#  include <comutil.h>
#  include <windows.applicationmodel.store.h>
#  include <windows.services.store.h>
#endif  // __MINGW32__
#include "nsError.h"
#include "nsString.h"
#include "nsISupportsPrimitives.h"
#include "nsCOMPtr.h"
#include "nsComponentManagerUtils.h"
#include "nsXPCOMCID.h"
#include "json/json.h"

using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Management;
#ifndef __MINGW32__  // MinGW lacks windows.services.store.h
using namespace ABI::Windows::Services::Store;
#endif

// Campaign IDs are stored in a JSON data structure under this key
// for installs done without the user being signed in to the Microsoft
// store.
#define CAMPAIGN_ID_JSON_FIELD_NAME "customPolicyField1"

namespace mozilla {
namespace toolkit {
namespace system {

NS_IMPL_ISUPPORTS(nsWindowsPackageManager, nsIWindowsPackageManager)

NS_IMETHODIMP
nsWindowsPackageManager::FindUserInstalledPackages(
    const nsTArray<nsString>& aNamePrefixes, nsTArray<nsString>& aPackages) {
  ComPtr<IInspectable> pmInspectable;
  ComPtr<Deployment::IPackageManager> pm;
  HRESULT hr = RoActivateInstance(
      HStringReference(
          RuntimeClass_Windows_Management_Deployment_PackageManager)
          .Get(),
      &pmInspectable);
  if (!SUCCEEDED(hr)) {
    return NS_ERROR_FAILURE;
  }
  hr = pmInspectable.As(&pm);
  if (!SUCCEEDED(hr)) {
    return NS_ERROR_FAILURE;
  }

  ComPtr<Collections::IIterable<ApplicationModel::Package*> > pkgs;
  hr = pm->FindPackagesByUserSecurityId(NULL, &pkgs);
  if (!SUCCEEDED(hr)) {
    return NS_ERROR_FAILURE;
  }
  ComPtr<Collections::IIterator<ApplicationModel::Package*> > iterator;
  hr = pkgs->First(&iterator);
  if (!SUCCEEDED(hr)) {
    return NS_ERROR_FAILURE;
  }
  boolean hasCurrent;
  hr = iterator->get_HasCurrent(&hasCurrent);
  while (SUCCEEDED(hr) && hasCurrent) {
    ComPtr<ApplicationModel::IPackage> package;
    hr = iterator->get_Current(&package);
    if (!SUCCEEDED(hr)) {
      return NS_ERROR_FAILURE;
    }
    ComPtr<ApplicationModel::IPackageId> packageId;
    hr = package->get_Id(&packageId);
    if (!SUCCEEDED(hr)) {
      return NS_ERROR_FAILURE;
    }
    HString rawName;
    hr = packageId->get_FamilyName(rawName.GetAddressOf());
    if (!SUCCEEDED(hr)) {
      return NS_ERROR_FAILURE;
    }
    unsigned int tmp;
    nsString name(rawName.GetRawBuffer(&tmp));
    for (uint32_t i = 0; i < aNamePrefixes.Length(); i++) {
      if (name.Find(aNamePrefixes.ElementAt(i)) != kNotFound) {
        aPackages.AppendElement(name);
        break;
      }
    }
    hr = iterator->MoveNext(&hasCurrent);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsWindowsPackageManager::GetInstalledDate(uint64_t* ts) {
  ComPtr<ApplicationModel::IPackageStatics> pkgStatics;
  HRESULT hr = RoGetActivationFactory(
      HStringReference(RuntimeClass_Windows_ApplicationModel_Package).Get(),
      IID_PPV_ARGS(&pkgStatics));
  if (!SUCCEEDED(hr)) {
    return NS_ERROR_FAILURE;
  }

  ComPtr<ApplicationModel::IPackage> package;
  hr = pkgStatics->get_Current(&package);
  if (!SUCCEEDED(hr)) {
    return NS_ERROR_FAILURE;
  }

  ComPtr<ApplicationModel::IPackage3> package3;
  hr = package.As(&package3);
  if (!SUCCEEDED(hr)) {
    return NS_ERROR_FAILURE;
  }

  DateTime installedDate;
  hr = package3->get_InstalledDate(&installedDate);
  if (!SUCCEEDED(hr)) {
    return NS_ERROR_FAILURE;
  }

  *ts = installedDate.UniversalTime;
  return NS_OK;
}

#ifndef __MINGW32__
static HRESULT RejectOnMainThread(
    const char* aName, nsMainThreadPtrHandle<dom::Promise> promiseHolder,
    nsresult result) {
  DebugOnly<nsresult> rv = NS_DispatchToMainThread(NS_NewRunnableFunction(
      aName, [promiseHolder = std::move(promiseHolder), result]() {
        promiseHolder.get()->MaybeReject(result);
      }));
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "NS_DispatchToMainThread failed");

  return S_OK;
}

// forward declarations
static void GetCampaignIdFromStoreProductOnBackgroundThread(
    ComPtr<IAsyncOperation<StoreProductResult*> > asyncSpr,
    ComPtr<IStoreContext> storeContext,
    nsMainThreadPtrHandle<dom::Promise> promiseHolder);
static void GetCampaignIdFromLicenseOnBackgroundThread(
    ComPtr<IAsyncOperation<StoreAppLicense*> > asyncSal,
    nsMainThreadPtrHandle<dom::Promise> promiseHolder,
    nsAutoString aCampaignId);

static std::tuple<nsMainThreadPtrHolder<dom::Promise>*, nsresult>
InitializePromise(JSContext* aCx, dom::Promise** aPromise) {
  *aPromise = nullptr;

  nsIGlobalObject* global = xpc::CurrentNativeGlobal(aCx);
  if (NS_WARN_IF(!global)) {
    return std::make_tuple(nullptr, NS_ERROR_FAILURE);
  }

  ErrorResult erv;
  RefPtr<dom::Promise> outer = dom::Promise::Create(global, erv);
  if (NS_WARN_IF(erv.Failed())) {
    return std::make_tuple(nullptr, erv.StealNSResult());
  }

  auto promiseHolder = new nsMainThreadPtrHolder<dom::Promise>(
      "nsWindowsPackageManager::CampaignId Promise", outer);

  outer.forget(aPromise);

  return std::make_tuple(promiseHolder, NS_OK);
}
#endif  // __MINGW32__

NS_IMETHODIMP
nsWindowsPackageManager::CampaignId(JSContext* aCx, dom::Promise** aPromise) {
  NS_ENSURE_ARG_POINTER(aPromise);

#ifdef __MINGW32__
  return NS_ERROR_NOT_IMPLEMENTED;
#else

  // This is only relevant for MSIX packaged builds.
  if (!mozilla::HasPackageIdentity()) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  auto [unwrappedPromiseHolder, result] = InitializePromise(aCx, aPromise);

  NS_ENSURE_SUCCESS(result, result);

  nsMainThreadPtrHandle<dom::Promise> promiseHolder(unwrappedPromiseHolder);

  ComPtr<IStoreContextStatics> scStatics = nullptr;
  HRESULT hr = RoGetActivationFactory(
      HStringReference(RuntimeClass_Windows_Services_Store_StoreContext).Get(),
      IID_PPV_ARGS(&scStatics));
  /* Per
   * https://docs.microsoft.com/en-us/windows/uwp/publish/create-a-custom-app-promotion-campaign#programmatically-retrieve-the-custom-campaign-id-for-an-app
   * there are three ways to find a campaign ID.
   * 1) If the user was logged into the Microsoft Store when they installed
   *    it will be available in a SKU.
   * 2) If they were not logged in, it will be available through the
   *    StoreAppLicense
   *
   * There's also a third way, in theory, to retrieve it on very old versions of
   * Windows 10 (1511 or earlier). However, these versions don't appear to be
   * able to use the Windows Store at all anymore - so there's no point in
   * supporting that scenario.
   *
   */
  if (!SUCCEEDED(hr) || scStatics == nullptr) {
    promiseHolder.get()->MaybeReject(NS_ERROR_FAILURE);
    return NS_OK;
  }

  ComPtr<IStoreContext> storeContext = nullptr;
  hr = scStatics->GetDefault(&storeContext);
  if (!SUCCEEDED(hr) || storeContext == nullptr) {
    promiseHolder.get()->MaybeReject(NS_ERROR_FAILURE);
    return NS_OK;
  }

  /* Despite the documentation not saying otherwise, these don't work
   * consistently when called from the main thread. I tried the two scenarios
   * described above multiple times, and couldn't consistently get the campaign
   * id when running this code async on the main thread. So instead, this
   * dispatches to a background task to do the work, and then dispatches to the
   * main thread to call back into the Javascript asynchronously
   *
   */
  result = NS_DispatchBackgroundTask(
      NS_NewRunnableFunction(
          __func__,
          [storeContext = std::move(storeContext),
           promiseHolder = std::move(promiseHolder)]() -> void {
            ComPtr<IAsyncOperation<StoreProductResult*> > asyncSpr = nullptr;

            auto hr =
                storeContext->GetStoreProductForCurrentAppAsync(&asyncSpr);
            if (!SUCCEEDED(hr) || asyncSpr == nullptr) {
              RejectOnMainThread(__func__, std::move(promiseHolder),
                                 NS_ERROR_FAILURE);
              return;
            }

            auto callback =
                Callback<IAsyncOperationCompletedHandler<StoreProductResult*> >(
                    [promiseHolder, asyncSpr,
                     storeContext = std::move(storeContext)](
                        IAsyncOperation<StoreProductResult*>* asyncInfo,
                        AsyncStatus status) -> HRESULT {
                      bool asyncOpSucceeded = status == AsyncStatus::Completed;
                      if (!asyncOpSucceeded) {
                        return RejectOnMainThread(__func__,
                                                  std::move(promiseHolder),
                                                  NS_ERROR_FAILURE);
                      }

                      GetCampaignIdFromStoreProductOnBackgroundThread(
                          std::move(asyncSpr), std::move(storeContext),
                          std::move(promiseHolder));

                      return S_OK;
                    });

            hr = asyncSpr->put_Completed(callback.Get());
            if (!SUCCEEDED(hr)) {
              RejectOnMainThread(__func__, std::move(promiseHolder),
                                 NS_ERROR_FAILURE);
              return;
            }
          }),
      NS_DISPATCH_EVENT_MAY_BLOCK);

  if (NS_FAILED(result)) {
    promiseHolder.get()->MaybeReject(NS_ERROR_FAILURE);
    return NS_OK;
  }

  return NS_OK;
#endif  // __MINGW32__
}

#ifndef __MINGW32__
static void GetCampaignIdFromStoreProductOnBackgroundThread(
    ComPtr<IAsyncOperation<StoreProductResult*> > asyncSpr,
    ComPtr<IStoreContext> storeContext,
    nsMainThreadPtrHandle<dom::Promise> promiseHolder) {
  ComPtr<IStoreProductResult> productResult = nullptr;

  auto hr = asyncSpr->GetResults(&productResult);
  if (!SUCCEEDED(hr) || productResult == nullptr) {
    RejectOnMainThread(__func__, std::move(promiseHolder), NS_ERROR_FAILURE);
    return;
  }

  nsAutoString campaignId;

  ComPtr<IStoreProduct> product = nullptr;
  hr = productResult->get_Product(&product);
  if (SUCCEEDED(hr) && (product != nullptr)) {
    ComPtr<Collections::IVectorView<StoreSku*> > skus = nullptr;
    hr = product->get_Skus(&skus);
    if (!SUCCEEDED(hr) || skus == nullptr) {
      RejectOnMainThread(__func__, std::move(promiseHolder), NS_ERROR_FAILURE);
      return;
    }

    unsigned int size;
    hr = skus->get_Size(&size);
    if (!SUCCEEDED(hr)) {
      RejectOnMainThread(__func__, std::move(promiseHolder), NS_ERROR_FAILURE);
      return;
    }

    for (unsigned int i = 0; i < size; i++) {
      ComPtr<IStoreSku> sku = nullptr;
      hr = skus->GetAt(i, &sku);
      if (!SUCCEEDED(hr) || sku == nullptr) {
        RejectOnMainThread(__func__, std::move(promiseHolder),
                           NS_ERROR_FAILURE);
        return;
      }

      boolean isInUserCollection = false;
      hr = sku->get_IsInUserCollection(&isInUserCollection);
      if (!SUCCEEDED(hr) || !isInUserCollection) continue;

      ComPtr<IStoreCollectionData> scd = nullptr;
      hr = sku->get_CollectionData(&scd);
      if (!SUCCEEDED(hr) || scd == nullptr) continue;

      HString msCampaignId;
      hr = scd->get_CampaignId(msCampaignId.GetAddressOf());
      if (!SUCCEEDED(hr)) continue;

      unsigned int tmp;
      campaignId.Assign(msCampaignId.GetRawBuffer(&tmp));
      if (campaignId.Length() > 0) {
        campaignId.AppendLiteral("&msstoresignedin=true");
      }
    }
  }

  if (!campaignId.IsEmpty()) {
    // If we got here, it means that campaignId has been processed and can be
    // sent back via the promise
    DebugOnly<nsresult> rv = NS_DispatchToMainThread(NS_NewRunnableFunction(
        __func__, [promiseHolder = std::move(promiseHolder),
                   campaignId = std::move(campaignId)]() {
          promiseHolder.get()->MaybeResolve(campaignId);
        }));

    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "NS_DispatchToMainThread failed");

    return;
  }

  // There's various points above that could exit without a failure.
  // If we get here without a campaignId we may as well just check
  // the AppStoreLicense.
  ComPtr<IAsyncOperation<StoreAppLicense*> > asyncSal = nullptr;

  hr = storeContext->GetAppLicenseAsync(&asyncSal);
  if (!SUCCEEDED(hr) || asyncSal == nullptr) {
    RejectOnMainThread(__func__, std::move(promiseHolder), NS_ERROR_FAILURE);
    return;
  }

  auto callback = Callback<IAsyncOperationCompletedHandler<StoreAppLicense*> >(
      [asyncSal, promiseHolder, campaignId = std::move(campaignId)](
          IAsyncOperation<StoreAppLicense*>* asyncInfo,
          AsyncStatus status) -> HRESULT {
        bool asyncOpSucceeded = status == AsyncStatus::Completed;
        if (!asyncOpSucceeded) {
          return RejectOnMainThread(__func__, std::move(promiseHolder),
                                    NS_ERROR_FAILURE);
        }

        GetCampaignIdFromLicenseOnBackgroundThread(std::move(asyncSal),
                                                   std::move(promiseHolder),
                                                   std::move(campaignId));

        return S_OK;
      });

  hr = asyncSal->put_Completed(callback.Get());
  if (!SUCCEEDED(hr)) {
    RejectOnMainThread(__func__, std::move(promiseHolder), NS_ERROR_FAILURE);
    return;
  }
}

static void GetCampaignIdFromLicenseOnBackgroundThread(
    ComPtr<IAsyncOperation<StoreAppLicense*> > asyncSal,
    nsMainThreadPtrHandle<dom::Promise> promiseHolder,
    nsAutoString aCampaignId) {
  ComPtr<IStoreAppLicense> license = nullptr;
  auto hr = asyncSal->GetResults(&license);
  if (!SUCCEEDED(hr) || license == nullptr) {
    RejectOnMainThread(__func__, std::move(promiseHolder), NS_ERROR_FAILURE);
    return;
  }

  HString extendedData;
  hr = license->get_ExtendedJsonData(extendedData.GetAddressOf());
  if (!SUCCEEDED(hr)) {
    RejectOnMainThread(__func__, std::move(promiseHolder), NS_ERROR_FAILURE);
    return;
  }

  Json::Value jsonData;
  Json::Reader jsonReader;

  unsigned int tmp;
  nsAutoString key(extendedData.GetRawBuffer(&tmp));
  if (!jsonReader.parse(NS_ConvertUTF16toUTF8(key).get(), jsonData, false)) {
    RejectOnMainThread(__func__, std::move(promiseHolder), NS_ERROR_FAILURE);
    return;
  }

  if (jsonData.isMember(CAMPAIGN_ID_JSON_FIELD_NAME) &&
      jsonData[CAMPAIGN_ID_JSON_FIELD_NAME].isString()) {
    aCampaignId.Assign(
        NS_ConvertUTF8toUTF16(
            jsonData[CAMPAIGN_ID_JSON_FIELD_NAME].asString().c_str())
            .get());
    if (aCampaignId.Length() > 0) {
      aCampaignId.AppendLiteral("&msstoresignedin=false");
    }
  }

  DebugOnly<nsresult> rv = NS_DispatchToMainThread(NS_NewRunnableFunction(
      __func__, [promiseHolder = std::move(promiseHolder),
                 aCampaignId = std::move(aCampaignId)]() {
        promiseHolder.get()->MaybeResolve(aCampaignId);
      }));
  NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "NS_DispatchToMainThread failed");
}
#endif  // __MINGW32__

}  // namespace system
}  // namespace toolkit
}  // namespace mozilla
