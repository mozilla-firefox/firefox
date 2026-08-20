/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/ProcInfo.h"

#include "mozilla/UniquePtr.h"
#include "nsNetCID.h"
#include "nsServiceManagerUtils.h"
#include "nsThreadUtils.h"

namespace mozilla {

RefPtr<ProcInfoPromise> GetProcInfo(nsTArray<ProcInfoRequest>&& aRequests) {
  auto holder = MakeUnique<MozPromiseHolder<ProcInfoPromise>>();
  RefPtr<ProcInfoPromise> promise = holder->Ensure(__func__);

  nsresult rv = NS_OK;
  nsCOMPtr<nsIEventTarget> target =
      do_GetService(NS_STREAMTRANSPORTSERVICE_CONTRACTID, &rv);
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to get stream transport service");
    holder->Reject(rv, __func__);
    return promise;
  }

  RefPtr<nsIRunnable> r = NS_NewRunnableFunction(
      __func__,
      [holder = std::move(holder),
       requests = std::move(aRequests)]() mutable -> void {
        holder->ResolveOrReject(GetProcInfoSync(std::move(requests)), __func__);
      });

  rv = target->Dispatch(r.forget(), NS_DISPATCH_NORMAL);
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to dispatch the LoadDataRunnable.");
  }

  return promise;
}

nsCString GetUtilityActorName(const UtilityActorName aActorName) {
  switch (aActorName) {
    case UtilityActorName::Unknown:
      return "unknown"_ns;
    case UtilityActorName::AudioDecoder_Generic:
      return "audio-decoder-generic"_ns;
    case UtilityActorName::AudioDecoder_AppleMedia:
      return "audio-decoder-applemedia"_ns;
    case UtilityActorName::AudioDecoder_WMF:
      return "audio-decoder-wmf"_ns;
    case UtilityActorName::MfMediaEngineCDM:
      return "mf-media-engine"_ns;
    case UtilityActorName::JSOracle:
      return "js-oracle"_ns;
    case UtilityActorName::WindowsUtils:
      return "windows-utils"_ns;
    case UtilityActorName::WindowsFileDialog:
      return "windows-file-dialog"_ns;
    case UtilityActorName::Pkcs11Module:
      return "pkcs11-module"_ns;
    case UtilityActorName::HwInference:
      return "hw-inference"_ns;
    default:
      return "unknown"_ns;
  }
}

}  // namespace mozilla
