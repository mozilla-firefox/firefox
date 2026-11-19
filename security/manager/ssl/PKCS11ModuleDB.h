/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef PKCS11ModuleDB_h
#define PKCS11ModuleDB_h

#include "mozilla/MozPromise.h"
#include "nsIPKCS11Module.h"
#include "nsIPKCS11ModuleDB.h"
#include "nsTLiteralString.h"

#if !defined(MOZ_NO_SMART_CARDS)
#  include "mozilla/ipc/UtilityProcessManager.h"
#endif  // !MOZ_NO_SMART_CARDS

namespace mozilla {
namespace psm {

// Opens an informational dialog indicating that a protected authentication
// attempt is in progress if there's an active window to host it. In headless
// contexts (xpcshell, very early startup) there's no WindowCreator registered
// and OpenWindow would assert/fail.
MOZ_CAN_RUN_SCRIPT
void ShowProtectedAuthDialog(const nsCString& tokenName,
                             const nsString& promptId);

#define NS_PKCS11MODULEDB_CID \
  {0xff9fbcd7, 0x9517, 0x4334, {0xb9, 0x7a, 0xce, 0xed, 0x78, 0x90, 0x99, 0x74}}

class PKCS11ModuleDB : public nsIPKCS11ModuleDB {
 public:
  PKCS11ModuleDB();

  NS_DECL_ISUPPORTS
  NS_DECL_NSIPKCS11MODULEDB

  static already_AddRefed<PKCS11ModuleDB> GetSingleton();
  static bool UtilityProcessEnabled();

  static nsresult DoDeleteModule(const nsCString& moduleName);
  static nsresult DoAddModule(const nsCString& moduleName,
                              const nsCString& libraryPath,
                              uint32_t mechanismFlags, uint32_t cipherFlags);

#if !defined(MOZ_NO_SMART_CARDS)
  static nsresult DoListModules(nsTArray<mozilla::psm::ModuleInfo>& modules);
#endif  // !MOZ_NO_SMART_CARDS

 protected:
  virtual ~PKCS11ModuleDB() = default;

 private:
  using ListModulesPromise =
      MozPromise<nsTArray<RefPtr<nsIPKCS11Module>>, nsresult, true>;
  RefPtr<ListModulesPromise> ListMainProcessModules();

#if !defined(MOZ_NO_SMART_CARDS)
  using PKCS11ModuleProcessPromise =
      MozPromise<RefPtr<PKCS11ModuleParent>, nsresult, false>;
  RefPtr<PKCS11ModuleProcessPromise> mPKCS11ModuleProcessPromise;

  RefPtr<ListModulesPromise> ListRemoteProcessModules();
  static RefPtr<ListModulesPromise> ListRemoteProcessModulesGivenParent(
      const RefPtr<PKCS11ModuleParent>& parent);

 public:
  typedef MozPromise<mozilla::psm::TokenInfo, nsresult, true> TokenInfoPromise;
  RefPtr<TokenInfoPromise> ResetToken(SECMODModuleID moduleID,
                                      CK_SLOT_ID slotID);
  RefPtr<TokenInfoPromise> LoginToken(SECMODModuleID moduleID,
                                      CK_SLOT_ID slotID);
  RefPtr<TokenInfoPromise> LogoutToken(SECMODModuleID moduleID,
                                       CK_SLOT_ID slotID);
  RefPtr<TokenInfoPromise> ChangeTokenPassword(SECMODModuleID moduleID,
                                               CK_SLOT_ID slotID,
                                               const nsCString& oldPassword,
                                               const nsCString& newPassword);

  typedef MozPromise<nsTArray<CryptokiObject>, nsresult, true>
      FindObjectsPromise;
  RefPtr<FindObjectsPromise> FindObjects(SearchingFor searchingFor);

  typedef MozPromise<nsTArray<uint8_t>, nsresult, true> SignPromise;
  RefPtr<SignPromise> Sign(nsTArray<uint8_t> certificate,
                           nsTArray<uint8_t> data, nsTArray<uint8_t> params);

 private:
  static RefPtr<TokenInfoPromise> ResetTokenGivenParent(
      const RefPtr<PKCS11ModuleParent>& parent, SECMODModuleID moduleID,
      CK_SLOT_ID slotID);
  static RefPtr<TokenInfoPromise> LoginTokenGivenParent(
      const RefPtr<PKCS11ModuleParent>& parent, SECMODModuleID moduleID,
      CK_SLOT_ID slotID);
  static RefPtr<TokenInfoPromise> LogoutTokenGivenParent(
      const RefPtr<PKCS11ModuleParent>& parent, SECMODModuleID moduleID,
      CK_SLOT_ID slotID);

  static RefPtr<TokenInfoPromise> ChangeTokenPasswordGivenParent(
      const RefPtr<PKCS11ModuleParent>& parent, SECMODModuleID moduleID,
      CK_SLOT_ID slotID, const nsCString& oldPassword,
      const nsCString& newPassword);

  static RefPtr<FindObjectsPromise> FindObjectsGivenParent(
      SearchingFor searchingFor, const RefPtr<PKCS11ModuleParent>& parent);

  static RefPtr<SignPromise> SignGivenParent(
      Span<uint8_t> certificate, Span<uint8_t> data, Span<uint8_t> params,
      const RefPtr<PKCS11ModuleParent>& parent);
#endif  // !MOZ_NO_SMART_CARDS
};

const nsLiteralCString kNSSInternalModuleName("NSS Internal PKCS #11 Module");
const nsLiteralCString kOSClientCertsModuleName("OS Client Cert Module");
const nsLiteralCString kRemoteCertsModuleName("Remote Certs Module");
const nsLiteralCString kRootModuleName("Builtin Roots Module");

void CollectThirdPartyPKCS11ModuleTelemetry(bool aIsInitialization = false);

}  // namespace psm
}  // namespace mozilla

#endif  // PKCS11ModuleDB_h
