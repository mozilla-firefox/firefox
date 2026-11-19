/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_psm_PKCS11ModuleParent_h
#define mozilla_psm_PKCS11ModuleParent_h

#if defined(MOZ_NO_SMART_CARDS)
#  error This file should only be used when MOZ_NO_SMART_CARDS is not defined.
#endif  // MOZ_NO_SMART_CARDS

#include "mozilla/Maybe.h"
#include "mozilla/ProcInfo.h"
#include "mozilla/ipc/UtilityProcessParent.h"
#include "mozilla/psm/PPKCS11ModuleParent.h"
#include "nsIObserverService.h"

namespace mozilla::psm {

class PKCS11ModuleParent final : public PPKCS11ModuleParent,
                                 public nsIObserver {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIOBSERVER

  explicit PKCS11ModuleParent() = default;

  UtilityActorName GetActorName() { return UtilityActorName::Pkcs11Module; }

  nsresult BindToUtilityProcess(
      const RefPtr<ipc::UtilityProcessParent>& aUtilityParent);

  ipc::IPCResult RecvPromptPassword(nsCString&& aTokenName,
                                    PromptPasswordResolver&& aResolver);
  MOZ_CAN_RUN_SCRIPT_BOUNDARY
  ipc::IPCResult RecvShowProtectedAuthPrompt(nsCString&& aTokenName,
                                             uint64_t id);
  ipc::IPCResult RecvDismissProtectedAuthPrompt(uint64_t id);

 private:
  ~PKCS11ModuleParent() = default;

  mozilla::Maybe<uint64_t> mMaybePromptId;
};

}  // namespace mozilla::psm

#endif  // mozilla_psm_PKCS11ModuleParent_h
