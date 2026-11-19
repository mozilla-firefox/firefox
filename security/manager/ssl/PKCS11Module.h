/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef PKCS11Module_h
#define PKCS11Module_h

#include "ScopedNSSTypes.h"
#include "nsIPKCS11Module.h"
#include "nsISupports.h"
#include "nsString.h"

#if !defined(MOZ_NO_SMART_CARDS)
#  include "mozilla/psm/PPKCS11Module.h"
#endif  // !MOZ_NO_SMART_CARDS

class PKCS11Module : public nsIPKCS11Module {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIPKCS11MODULE

  explicit PKCS11Module(SECMODModule* module);

#if !defined(MOZ_NO_SMART_CARDS)
  nsresult GetModuleInfo(mozilla::psm::ModuleInfo& moduleInfo);
#endif  // !MOZ_NO_SMART_CARDS

 protected:
  virtual ~PKCS11Module() = default;

 private:
  mozilla::UniqueSECMODModule mModule;
};

#if !defined(MOZ_NO_SMART_CARDS)
class RemotePKCS11Module : public nsIPKCS11Module {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIPKCS11MODULE

  explicit RemotePKCS11Module(const mozilla::psm::ModuleInfo& moduleInfo);

 protected:
  virtual ~RemotePKCS11Module() = default;

 private:
  mozilla::psm::ModuleInfo mModuleInfo;
};
#endif  // !MOZ_NO_SMART_CARDS

#endif  // PKCS11Module_h
