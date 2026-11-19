/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef PKCS11Slot_h
#define PKCS11Slot_h

#include "ScopedNSSTypes.h"
#include "nsIPKCS11Slot.h"
#include "nsISupports.h"
#include "nsString.h"

#if !defined(MOZ_NO_SMART_CARDS)
#  include "mozilla/psm/PPKCS11Module.h"
#endif  // !MOZ_NO_SMART_CARDS

class PKCS11Slot : public nsIPKCS11Slot {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIPKCS11SLOT

  explicit PKCS11Slot(PK11SlotInfo* slot);

#if !defined(MOZ_NO_SMART_CARDS)
  nsresult GetSlotInfo(mozilla::psm::SlotInfo& slotInfo);
#endif  // !MOZ_NO_SMART_CARDS

 protected:
  virtual ~PKCS11Slot() = default;

 private:
  mozilla::UniquePK11SlotInfo mSlot;
  // True if this is the "PKCS#11 slot" that provides cryptographic functions.
  bool mIsInternalCryptoSlot;
  // True if this is the "PKCS#11 slot" where private keys are stored.
  bool mIsInternalKeySlot;
  nsCString mSlotDesc;
  nsCString mSlotManufacturerID;
  nsCString mSlotHWVersion;
  nsCString mSlotFWVersion;
  int mSeries;

  nsresult refreshSlotInfo();
  nsresult GetAttributeHelper(const nsACString& attribute,
                              /*out*/ nsACString& xpcomOutParam);
};

#if !defined(MOZ_NO_SMART_CARDS)
class RemotePKCS11Slot : public nsIPKCS11Slot {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIPKCS11SLOT

  explicit RemotePKCS11Slot(const mozilla::psm::SlotInfo& slotInfo);

 protected:
  virtual ~RemotePKCS11Slot() = default;

 private:
  mozilla::psm::SlotInfo mSlotInfo;
};
#endif  // !MOZ_NO_SMART_CARDS

#endif  // PKCS11Slot_h
