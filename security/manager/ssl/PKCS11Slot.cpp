/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PKCS11Slot.h"

#include <string.h>

#include "PKCS11Token.h"
#include "mozilla/RefPtr.h"
#include "nsComponentManagerUtils.h"
#include "nsNSSCertHelper.h"

using namespace mozilla;
using namespace mozilla::psm;

NS_IMPL_ISUPPORTS(PKCS11Slot, nsIPKCS11Slot)

PKCS11Slot::PKCS11Slot(PK11SlotInfo* slot) {
  MOZ_ASSERT(slot);
  mSlot.reset(PK11_ReferenceSlot(slot));
  mIsInternalCryptoSlot =
      PK11_IsInternal(mSlot.get()) && !PK11_IsInternalKeySlot(mSlot.get());
  mIsInternalKeySlot = PK11_IsInternalKeySlot(mSlot.get());
  mSeries = PK11_GetSlotSeries(slot);
  (void)refreshSlotInfo();
}

nsresult PKCS11Slot::refreshSlotInfo() {
  CK_SLOT_INFO slotInfo;
  nsresult rv = mozilla::MapSECStatus(PK11_GetSlotInfo(mSlot.get(), &slotInfo));
  if (NS_FAILED(rv)) {
    return rv;
  }

  // Set the Description field
  if (mIsInternalCryptoSlot) {
    nsresult rv;
    if (PK11_IsFIPS()) {
      rv = GetPIPNSSBundleString("Fips140SlotDescription", mSlotDesc);
    } else {
      rv = GetPIPNSSBundleString("SlotDescription", mSlotDesc);
    }
    if (NS_FAILED(rv)) {
      return rv;
    }
  } else if (mIsInternalKeySlot) {
    rv = GetPIPNSSBundleString("PrivateSlotDescription", mSlotDesc);
    if (NS_FAILED(rv)) {
      return rv;
    }
  } else {
    const char* ccDesc =
        mozilla::BitwiseCast<char*, CK_UTF8CHAR*>(slotInfo.slotDescription);
    mSlotDesc.Assign(ccDesc, strnlen(ccDesc, sizeof(slotInfo.slotDescription)));
    mSlotDesc.Trim(" ", false, true);
  }

  // Set the Manufacturer field
  if (mIsInternalCryptoSlot || mIsInternalKeySlot) {
    rv = GetPIPNSSBundleString("ManufacturerID", mSlotManufacturerID);
    if (NS_FAILED(rv)) {
      return rv;
    }
  } else {
    const char* ccManID =
        mozilla::BitwiseCast<char*, CK_UTF8CHAR*>(slotInfo.manufacturerID);
    mSlotManufacturerID.Assign(
        ccManID, strnlen(ccManID, sizeof(slotInfo.manufacturerID)));
    mSlotManufacturerID.Trim(" ", false, true);
  }

  // Set the Hardware Version field
  mSlotHWVersion.Truncate();
  mSlotHWVersion.AppendInt(slotInfo.hardwareVersion.major);
  mSlotHWVersion.Append('.');
  mSlotHWVersion.AppendInt(slotInfo.hardwareVersion.minor);

  // Set the Firmware Version field
  mSlotFWVersion.Truncate();
  mSlotFWVersion.AppendInt(slotInfo.firmwareVersion.major);
  mSlotFWVersion.Append('.');
  mSlotFWVersion.AppendInt(slotInfo.firmwareVersion.minor);

  return NS_OK;
}

nsresult PKCS11Slot::GetAttributeHelper(const nsACString& attribute,
                                        /*out*/ nsACString& xpcomOutParam) {
  if (PK11_GetSlotSeries(mSlot.get()) != mSeries) {
    nsresult rv = refreshSlotInfo();
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  xpcomOutParam = attribute;
  return NS_OK;
}

NS_IMETHODIMP
PKCS11Slot::GetName(/*out*/ nsACString& name) {
  if (mIsInternalCryptoSlot) {
    if (PK11_IsFIPS()) {
      return GetPIPNSSBundleString("Fips140TokenDescription", name);
    }
    return GetPIPNSSBundleString("TokenDescription", name);
  }
  if (mIsInternalKeySlot) {
    return GetPIPNSSBundleString("PrivateTokenDescription", name);
  }
  name.Assign(PK11_GetSlotName(mSlot.get()));

  return NS_OK;
}

NS_IMETHODIMP
PKCS11Slot::GetDesc(/*out*/ nsACString& desc) {
  return GetAttributeHelper(mSlotDesc, desc);
}

NS_IMETHODIMP
PKCS11Slot::GetManID(/*out*/ nsACString& manufacturerID) {
  return GetAttributeHelper(mSlotManufacturerID, manufacturerID);
}

NS_IMETHODIMP
PKCS11Slot::GetHWVersion(/*out*/ nsACString& hwVersion) {
  return GetAttributeHelper(mSlotHWVersion, hwVersion);
}

NS_IMETHODIMP
PKCS11Slot::GetFWVersion(/*out*/ nsACString& fwVersion) {
  return GetAttributeHelper(mSlotFWVersion, fwVersion);
}

NS_IMETHODIMP
PKCS11Slot::GetToken(nsIPKCS11Token** _retval) {
  NS_ENSURE_ARG_POINTER(_retval);
  if (!PK11_IsPresent(mSlot.get())) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  nsCOMPtr<nsIPKCS11Token> token = new PKCS11Token(mSlot.get());
  token.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP
PKCS11Slot::GetTokenName(/*out*/ nsACString& tokenName) {
  if (!PK11_IsPresent(mSlot.get())) {
    tokenName.SetIsVoid(true);
    return NS_OK;
  }

  if (PK11_GetSlotSeries(mSlot.get()) != mSeries) {
    nsresult rv = refreshSlotInfo();
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  if (mIsInternalCryptoSlot) {
    if (PK11_IsFIPS()) {
      return GetPIPNSSBundleString("Fips140TokenDescription", tokenName);
    }
    return GetPIPNSSBundleString("TokenDescription", tokenName);
  }
  if (mIsInternalKeySlot) {
    return GetPIPNSSBundleString("PrivateTokenDescription", tokenName);
  }

  tokenName.Assign(PK11_GetTokenName(mSlot.get()));
  return NS_OK;
}

NS_IMETHODIMP
PKCS11Slot::GetStatus(uint32_t* _retval) {
  NS_ENSURE_ARG_POINTER(_retval);
  if (PK11_IsDisabled(mSlot.get())) {
    *_retval = SLOT_DISABLED;
  } else if (!PK11_IsPresent(mSlot.get())) {
    *_retval = SLOT_NOT_PRESENT;
  } else if (PK11_NeedLogin(mSlot.get()) && PK11_NeedUserInit(mSlot.get())) {
    *_retval = SLOT_UNINITIALIZED;
  } else if (PK11_NeedLogin(mSlot.get()) &&
             !PK11_IsLoggedIn(mSlot.get(), nullptr)) {
    *_retval = SLOT_NOT_LOGGED_IN;
  } else if (PK11_NeedLogin(mSlot.get())) {
    *_retval = SLOT_LOGGED_IN;
  } else {
    *_retval = SLOT_READY;
  }
  return NS_OK;
}

#if !defined(MOZ_NO_SMART_CARDS)
nsresult PKCS11Slot::GetSlotInfo(SlotInfo& slotInfo) {
  nsresult rv = GetName(slotInfo.name());
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = GetDesc(slotInfo.desc());
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = GetManID(slotInfo.manID());
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = GetHWVersion(slotInfo.hwVersion());
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = GetFWVersion(slotInfo.fwVersion());
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = GetStatus(&slotInfo.status());
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = GetTokenName(slotInfo.tokenName());
  if (NS_FAILED(rv)) {
    return rv;
  }
  if (PK11_IsPresent(mSlot.get())) {
    RefPtr<PKCS11Token> token(MakeAndAddRef<PKCS11Token>(mSlot.get()));
    TokenInfo tokenInfo;
    rv = token->GetTokenInfo(tokenInfo);
    if (NS_FAILED(rv)) {
      return rv;
    }
    slotInfo.tokenInfo().emplace(std::move(tokenInfo));
  }
  return NS_OK;
}

NS_IMPL_ISUPPORTS(RemotePKCS11Slot, nsIPKCS11Slot)

RemotePKCS11Slot::RemotePKCS11Slot(const SlotInfo& slotInfo)
    : mSlotInfo(slotInfo) {}

NS_IMETHODIMP
RemotePKCS11Slot::GetName(/*out*/ nsACString& name) {
  name.Assign(mSlotInfo.name());
  return NS_OK;
}

NS_IMETHODIMP
RemotePKCS11Slot::GetDesc(/*out*/ nsACString& desc) {
  desc.Assign(mSlotInfo.desc());
  return NS_OK;
}

NS_IMETHODIMP
RemotePKCS11Slot::GetManID(/*out*/ nsACString& manufacturerID) {
  manufacturerID.Assign(mSlotInfo.manID());
  return NS_OK;
}

NS_IMETHODIMP
RemotePKCS11Slot::GetHWVersion(/*out*/ nsACString& hwVersion) {
  hwVersion.Assign(mSlotInfo.hwVersion());
  return NS_OK;
}

NS_IMETHODIMP
RemotePKCS11Slot::GetFWVersion(/*out*/ nsACString& fwVersion) {
  fwVersion.Assign(mSlotInfo.fwVersion());
  return NS_OK;
}

NS_IMETHODIMP
RemotePKCS11Slot::GetToken(nsIPKCS11Token** token) {
  if (mSlotInfo.tokenInfo().isNothing()) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  *token = MakeAndAddRef<RemotePKCS11Token>(*mSlotInfo.tokenInfo()).take();
  return NS_OK;
}

NS_IMETHODIMP
RemotePKCS11Slot::GetTokenName(/*out*/ nsACString& tokenName) {
  tokenName.Assign(mSlotInfo.tokenName());
  return NS_OK;
}

NS_IMETHODIMP
RemotePKCS11Slot::GetStatus(uint32_t* status) {
  NS_ENSURE_ARG_POINTER(status);
  *status = mSlotInfo.status();
  return NS_OK;
}
#endif  // !MOZ_NO_SMART_CARDS
