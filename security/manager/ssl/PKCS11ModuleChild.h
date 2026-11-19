/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_psm_PKCS11ModuleChild_h
#define mozilla_psm_PKCS11ModuleChild_h

#if defined(MOZ_NO_SMART_CARDS)
#  error This file should only be used when MOZ_NO_SMART_CARDS is not defined.
#endif  // MOZ_NO_SMART_CARDS

#include "mozilla/psm/PPKCS11ModuleChild.h"
#include "nsIObserver.h"
#include "nsISupports.h"

namespace mozilla::psm {

class PKCS11ModuleChild final : public PPKCS11ModuleChild {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(PKCS11ModuleChild, override);

  PKCS11ModuleChild() = default;

  nsresult Start(Endpoint<PPKCS11ModuleChild>&& aEndpoint,
                 nsCString&& aProfilePath);

  ipc::IPCResult RecvAddModule(nsCString&& aModuleName,
                               nsCString&& aLibraryPath,
                               uint32_t aMechanismFlags, uint32_t aCipherFlags,
                               AddModuleResolver&& aResolver);

  ipc::IPCResult RecvDeleteModule(nsCString&& aModuleName,
                                  DeleteModuleResolver&& aResolver);

  ipc::IPCResult RecvListModules(ListModulesResolver&& aResolver);

  ipc::IPCResult RecvResetToken(SECMODModuleID aModuleID, CK_SLOT_ID aSlotID,
                                ResetTokenResolver&& aResolver);

  ipc::IPCResult RecvLoginToken(SECMODModuleID aModuleID, CK_SLOT_ID aSlotID,
                                LoginTokenResolver&& aResolver);

  ipc::IPCResult RecvLogoutToken(SECMODModuleID aModuleID, CK_SLOT_ID aSlotID,
                                 LogoutTokenResolver&& aResolver);

  ipc::IPCResult RecvChangeTokenPassword(SECMODModuleID aModuleID,
                                         CK_SLOT_ID aSlotID,
                                         const nsCString& aOldPassword,
                                         const nsCString& aNewPassword,
                                         ResetTokenResolver&& aResolver);

  ipc::IPCResult RecvCancelProtectedAuth(uint64_t uuid);

  ipc::IPCResult RecvFindObjects(SearchingFor aSearchingFor,
                                 FindObjectsResolver&& aResolver);

  ipc::IPCResult RecvSign(nsTArray<uint8_t> aCertificate,
                          nsTArray<uint8_t> aData, nsTArray<uint8_t> aParams,
                          SignResolver&& aResolver);

  // Called by RemotePKCS11PasswordPrompt to prompt for a password in the
  // parent and then return it to NSS.
  char* PromptForPassword(PK11SlotInfo* slot);

  // Called by RemotePKCS11PasswordPrompt to initiate a protected
  // authentication attempt and indicate to the parent process that one is in
  // progress.
  char* InitiateProtectedAuth(PK11SlotInfo* slot);

 private:
  // Task queue for handling incoming and outgoing IPC calls.
  nsCOMPtr<nsISerialEventTarget> mTaskQueue;

  // Task queue for doing anything that may result in an authentication prompt
  // from NSS (e.g. logging in to a token, searching for certificates, or
  // signing data).
  // If NSS does prompt for authentication, this thread will block until the
  // prompt has been handled or cancelled.
  nsCOMPtr<nsISerialEventTarget> mAuthTaskQueue;

  // mAuthPromptMonitor notifies a pending authentication request that the
  // prompt for a password in the main process has completed.
  mozilla::Monitor mAuthPromptMonitor{"PKCS11ModuleChild::mAuthPromptMonitor"};

  // If set, this will be the result of prompting for a password in the main
  // process.
  mozilla::Maybe<std::tuple<nsresult, nsCString>> mMaybePasswordForPrompt
      MOZ_GUARDED_BY(mAuthPromptMonitor);

  enum class ProtectedAuthState {
    InProgress,
    Cancelled,
    Succeeded,
    DoRetry,
  };

  // If set, there is an in-progress protected auth attempt. The second value
  // of the pair identifies the prompt so that, for example, a late cancel
  // doesn't affect an upcoming, unrelated protected auth attempt.
  mozilla::Maybe<std::pair<ProtectedAuthState, uint64_t>>
      mMaybeProtectedAuthPrompt MOZ_GUARDED_BY(mAuthPromptMonitor);

  ~PKCS11ModuleChild() = default;
};

}  // namespace mozilla::psm

#endif  // mozilla_psm_PKCS11ModuleChild_h
