/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#if !defined(NIGHTLY_BUILD) || defined(MOZ_NO_SMART_CARDS)
#  error This file should only be used under NIGHTLY_BUILD and when MOZ_NO_SMART_CARDS is not defined.
#endif  // !NIGHTLY_BUILD || MOZ_NO_SMART_CARDS

#include "mozilla/psm/PKCS11ModuleParent.h"

#include "PKCS11ModuleDB.h"
#include "nsIPrompt.h"
#include "nsNSSCertHelper.h"
#include "nsNSSComponent.h"

namespace mozilla::psm {

NS_IMPL_ISUPPORTS(PKCS11ModuleParent, nsIObserver)

nsresult PKCS11ModuleParent::BindToUtilityProcess(
    const RefPtr<ipc::UtilityProcessParent>& aUtilityParent) {
  if (!aUtilityParent) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  Endpoint<PPKCS11ModuleParent> parentEnd;
  Endpoint<PPKCS11ModuleChild> childEnd;
  nsresult rv = PPKCS11Module::CreateEndpoints(
      ipc::EndpointProcInfo::Current(), aUtilityParent->OtherEndpointProcInfo(),
      &parentEnd, &childEnd);

  if (NS_FAILED(rv)) {
    MOZ_ASSERT_UNREACHABLE("Protocol endpoints failure");
    return NS_ERROR_FAILURE;
  }

  nsAutoCString profilePath;
  rv = GetNSSProfilePath(profilePath);
  if (NS_FAILED(rv)) {
    profilePath.Truncate();
  }

  if (!aUtilityParent->SendStartPKCS11ModuleService(std::move(childEnd),
                                                    profilePath)) {
    MOZ_ASSERT_UNREACHABLE("StartPKCS11Module service failure");
    return NS_ERROR_FAILURE;
  }

  if (!parentEnd.Bind(this)) {
    MOZ_ASSERT_UNREACHABLE("StartPKCS11Module service failure");
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

nsresult PromptForPassword(const nsCString& tokenName, nsCString& passwordOut) {
  nsAutoString promptString;
  AutoTArray<nsString, 1> formatStrings = {
      NS_ConvertUTF8toUTF16(tokenName),
  };
  nsresult rv = PIPBundleFormatStringFromName(
      "CertSecurityDeviceAuthenticationPrompt", formatStrings, promptString);
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsString password;
  bool userClickedOK = false;
  nsCOMPtr<nsIPrompt> prompt;
  rv = nsNSSComponent::GetNewPrompter(getter_AddRefs(prompt));
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = prompt->PromptPassword(nullptr, promptString.get(),
                              getter_Copies(password), &userClickedOK);
  if (NS_FAILED(rv)) {
    return rv;
  }
  if (!userClickedOK) {
    // Indicate that the operation causing the password prompt should be
    // cancelled.
    return NS_ERROR_ABORT;
  }
  passwordOut.Assign(NS_ConvertUTF16toUTF8(password));
  return NS_OK;
}

ipc::IPCResult PKCS11ModuleParent::RecvPromptPassword(
    nsCString&& aTokenName, PromptPasswordResolver&& aResolver) {
  nsCOMPtr<nsISerialEventTarget> currentThread(GetCurrentSerialEventTarget());
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      __func__,
      [tokenName(std::move(aTokenName)), eventTarget(std::move(currentThread)),
       resolver(std::move(aResolver))] {
        nsCString password;
        nsresult rv = PromptForPassword(tokenName, password);
        eventTarget->Dispatch(
            NS_NewRunnableFunction(__func__, [rv, password(std::move(password)),
                                              resolver(std::move(resolver))]() {
              resolver(std::make_tuple(rv, std::move(password)));
            }));
      }));
  return IPC_OK();
}

NS_IMETHODIMP
PKCS11ModuleParent::Observe(nsISupports*, const char* aTopic,
                            const char16_t* aData) {
  if (strcmp("pk11-protected-auth-cancel", aTopic) != 0) {
    return NS_ERROR_UNEXPECTED;
  }

  // Only react to the cancel notification carrying our unique id; another
  // protected-auth dialog open at the same time uses a different id.
  if (aData && mMaybePromptId.isSome() &&
      IntToString(*mMaybePromptId) == nsDependentString(aData)) {
    Maybe<uint64_t> maybePromptId(mMaybePromptId.take());
    (void)SendCancelProtectedAuth(*maybePromptId);
    nsCOMPtr<nsIObserverService> obsService =
        mozilla::services::GetObserverService();
    if (obsService) {
      obsService->RemoveObserver(this, "pk11-protected-auth-cancel");
    }
  }
  return NS_OK;
}

ipc::IPCResult PKCS11ModuleParent::RecvShowProtectedAuthPrompt(
    nsCString&& aTokenName, uint64_t id) {
  mMaybePromptId = Some(id);

  // Add this as an observer for if the dialog gets cancelled.
  nsCOMPtr<nsIObserverService> obsService =
      mozilla::services::GetObserverService();
  if (obsService) {
    (void)obsService->AddObserver(this, "pk11-protected-auth-cancel", false);
  }

  nsAutoString promptId;
  promptId.AppendInt(id);
  ShowProtectedAuthDialog(aTokenName, promptId);
  return IPC_OK();
}

ipc::IPCResult PKCS11ModuleParent::RecvDismissProtectedAuthPrompt(uint64_t id) {
  nsCOMPtr<nsIObserverService> obsService =
      mozilla::services::GetObserverService();
  if (obsService && mMaybePromptId.isSome() && *mMaybePromptId == id) {
    mMaybePromptId.reset();
    nsAutoString promptId(IntToString(id));
    obsService->NotifyObservers(nullptr, "pk11-protected-auth-complete",
                                promptId.get());
    obsService->RemoveObserver(this, "pk11-protected-auth-cancel");
  }
  return IPC_OK();
}

}  // namespace mozilla::psm
