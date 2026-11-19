/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PKCS11Module.h"

#include "PKCS11ModuleDB.h"
#include "PKCS11Slot.h"
#include "nsComponentManagerUtils.h"
#include "nsNSSCertHelper.h"
#include "nsNSSComponent.h"

using namespace mozilla::psm;

NS_IMPL_ISUPPORTS(PKCS11Module, nsIPKCS11Module)

PKCS11Module::PKCS11Module(SECMODModule* module) {
  MOZ_ASSERT(module);
  mModule.reset(SECMOD_ReferenceModule(module));
}

// Convert the UTF8 internal name of the module to how it should appear to the
// user. In most cases this involves simply passing back the module's name.
// However, the builtin roots module has a non-localized name internally that we
// must map to the localized version when we display it to the user.
static nsresult NormalizeModuleNameOut(const char* moduleNameIn,
                                       nsACString& moduleNameOut) {
  // Easy case: this isn't the builtin roots module.
  if (nsDependentCString(moduleNameIn) != kRootModuleName) {
    moduleNameOut.Assign(moduleNameIn);
    return NS_OK;
  }

  nsAutoString localizedRootModuleName;
  nsresult rv =
      GetPIPNSSBundleString("RootCertModuleName", localizedRootModuleName);
  if (NS_FAILED(rv)) {
    return rv;
  }
  moduleNameOut.Assign(NS_ConvertUTF16toUTF8(localizedRootModuleName));
  return NS_OK;
}

NS_IMETHODIMP
PKCS11Module::GetName(/*out*/ nsACString& name) {
  return NormalizeModuleNameOut(mModule->commonName, name);
}

NS_IMETHODIMP
PKCS11Module::GetLibName(/*out*/ nsACString& libName) {
  if (mModule->dllName) {
    libName = mModule->dllName;
  } else {
    libName.SetIsVoid(true);
  }
  return NS_OK;
}

NS_IMETHODIMP
PKCS11Module::GetSlots(nsTArray<RefPtr<nsIPKCS11Slot>>& slots) {
  nsresult rv = CheckForSmartCardChanges();
  if (NS_FAILED(rv)) {
    return rv;
  }

  slots.Clear();

  // Access to mModule->slots must be protected by acquiring the secmod list
  // read lock.
  mozilla::AutoSECMODListReadLock lock;
  for (int i = 0; i < mModule->slotCount; i++) {
    if (mModule->slots[i]) {
      slots.AppendElement(new PKCS11Slot(mModule->slots[i]));
    }
  }

  return NS_OK;
}

#if !defined(MOZ_NO_SMART_CARDS)
nsresult PKCS11Module::GetModuleInfo(ModuleInfo& moduleInfo) {
  nsresult rv = CheckForSmartCardChanges();
  if (NS_FAILED(rv)) {
    return rv;
  }

  rv = GetName(moduleInfo.name());
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = GetLibName(moduleInfo.libName());
  if (NS_FAILED(rv)) {
    return rv;
  }

  moduleInfo.slots().Clear();
  // Access to mModule->slots must be protected by acquiring the secmod list
  // read lock.
  mozilla::AutoSECMODListReadLock lock;
  for (int i = 0; i < mModule->slotCount; i++) {
    if (mModule->slots[i]) {
      RefPtr slot = mozilla::MakeRefPtr<PKCS11Slot>(mModule->slots[i]);
      SlotInfo slotInfo;
      rv = slot->GetSlotInfo(slotInfo);
      if (NS_FAILED(rv)) {
        return rv;
      }
      moduleInfo.slots().AppendElement(std::move(slotInfo));
    }
  }

  return NS_OK;
}

NS_IMPL_ISUPPORTS(RemotePKCS11Module, nsIPKCS11Module)

RemotePKCS11Module::RemotePKCS11Module(const ModuleInfo& moduleInfo)
    : mModuleInfo(moduleInfo) {}

NS_IMETHODIMP
RemotePKCS11Module::GetName(/*out*/ nsACString& name) {
  name.Assign(mModuleInfo.name());
  return NS_OK;
}

NS_IMETHODIMP
RemotePKCS11Module::GetLibName(/*out*/ nsACString& libName) {
  libName.Assign(mModuleInfo.libName());
  return NS_OK;
}

NS_IMETHODIMP
RemotePKCS11Module::GetSlots(nsTArray<RefPtr<nsIPKCS11Slot>>& slots) {
  slots.Clear();
  for (auto& slot : mModuleInfo.slots()) {
    slots.AppendElement(new RemotePKCS11Slot(slot));
  }
  return NS_OK;
}
#endif  // !MOZ_NO_SMART_CARDS
