/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "PKCS11Token.h"

#include "PKCS11ModuleDB.h"
#include "ScopedNSSTypes.h"
#include "mozilla/Casting.h"
#include "mozilla/ErrorResult.h"
#include "mozilla/Logging.h"
#include "mozilla/dom/Promise.h"
#include "nsISupports.h"
#include "nsNSSCertHelper.h"
#include "nsNSSComponent.h"
#include "nsPromiseFlatString.h"
#include "nsReadableUtils.h"
#include "nsServiceManagerUtils.h"
#include "prerror.h"
#include "secerr.h"

using namespace mozilla;
using namespace mozilla::psm;
using mozilla::ErrorResult;
using mozilla::dom::Promise;

extern mozilla::LazyLogModule gPIPNSSLog;

NS_IMPL_ISUPPORTS(PKCS11Token, nsIPKCS11Token)

nsresult PKCS11Token::Init() {
  static NS_DEFINE_CID(kNSSComponentCID, NS_NSSCOMPONENT_CID);
  nsCOMPtr<nsINSSComponent> nss(do_GetService(kNSSComponentCID));
  if (!nss) {
    return NS_ERROR_FAILURE;
  }
  mSlot.reset(PK11_GetInternalKeySlot());
  mIsInternalCryptoToken = false;
  mIsInternalKeyToken = true;
  mSeries = PK11_GetSlotSeries(mSlot.get());
  return refreshTokenInfo();
}

PKCS11Token::PKCS11Token(PK11SlotInfo* slot) {
  MOZ_ASSERT(slot);
  mSlot.reset(PK11_ReferenceSlot(slot));
  mIsInternalCryptoToken =
      PK11_IsInternal(mSlot.get()) && !PK11_IsInternalKeySlot(mSlot.get());
  mIsInternalKeyToken = PK11_IsInternalKeySlot(mSlot.get());
  mSeries = PK11_GetSlotSeries(slot);
  (void)refreshTokenInfo();
}

nsresult PKCS11Token::refreshTokenInfo() {
  if (mIsInternalCryptoToken) {
    nsresult rv;
    if (PK11_IsFIPS()) {
      rv = GetPIPNSSBundleString("Fips140TokenDescription", mTokenName);
    } else {
      rv = GetPIPNSSBundleString("TokenDescription", mTokenName);
    }
    if (NS_FAILED(rv)) {
      return rv;
    }
  } else if (mIsInternalKeyToken) {
    nsresult rv = GetPIPNSSBundleString("PrivateTokenDescription", mTokenName);
    if (NS_FAILED(rv)) {
      return rv;
    }
  } else {
    mTokenName.Assign(PK11_GetTokenName(mSlot.get()));
  }

  CK_TOKEN_INFO tokInfo;
  nsresult rv = mozilla::MapSECStatus(PK11_GetTokenInfo(mSlot.get(), &tokInfo));
  if (NS_FAILED(rv)) {
    return rv;
  }

  // Set the Manufacturer field
  if (mIsInternalCryptoToken || mIsInternalKeyToken) {
    rv = GetPIPNSSBundleString("ManufacturerID", mTokenManufacturerID);
    if (NS_FAILED(rv)) {
      return rv;
    }
  } else {
    const char* ccManID =
        mozilla::BitwiseCast<char*, CK_UTF8CHAR*>(tokInfo.manufacturerID);
    mTokenManufacturerID.Assign(
        ccManID, strnlen(ccManID, sizeof(tokInfo.manufacturerID)));
    mTokenManufacturerID.Trim(" ", false, true);
  }

  // Set the Hardware Version field
  mTokenHWVersion.Truncate();
  mTokenHWVersion.AppendInt(tokInfo.hardwareVersion.major);
  mTokenHWVersion.Append('.');
  mTokenHWVersion.AppendInt(tokInfo.hardwareVersion.minor);

  // Set the Firmware Version field
  mTokenFWVersion.Truncate();
  mTokenFWVersion.AppendInt(tokInfo.firmwareVersion.major);
  mTokenFWVersion.Append('.');
  mTokenFWVersion.AppendInt(tokInfo.firmwareVersion.minor);

  // Set the Serial Number field
  const char* ccSerial =
      mozilla::BitwiseCast<char*, CK_CHAR*>(tokInfo.serialNumber);
  mTokenSerialNum.Assign(ccSerial,
                         strnlen(ccSerial, sizeof(tokInfo.serialNumber)));
  mTokenSerialNum.Trim(" ", false, true);

  return NS_OK;
}

nsresult PKCS11Token::GetAttributeHelper(const nsACString& attribute,
                                         /*out*/ nsACString& xpcomOutParam) {
  // Handle removals/insertions.
  if (PK11_GetSlotSeries(mSlot.get()) != mSeries) {
    nsresult rv = refreshTokenInfo();
    if (NS_FAILED(rv)) {
      return rv;
    }
  }

  xpcomOutParam = attribute;
  return NS_OK;
}

NS_IMETHODIMP
PKCS11Token::GetTokenName(/*out*/ nsACString& tokenName) {
  return GetAttributeHelper(mTokenName, tokenName);
}

NS_IMETHODIMP
PKCS11Token::GetIsInternalKeyToken(/*out*/ bool* _retval) {
  NS_ENSURE_ARG_POINTER(_retval);
  *_retval = mIsInternalKeyToken;
  return NS_OK;
}

NS_IMETHODIMP
PKCS11Token::GetTokenManID(/*out*/ nsACString& tokenManufacturerID) {
  return GetAttributeHelper(mTokenManufacturerID, tokenManufacturerID);
}

NS_IMETHODIMP
PKCS11Token::GetTokenHWVersion(/*out*/ nsACString& tokenHWVersion) {
  return GetAttributeHelper(mTokenHWVersion, tokenHWVersion);
}

NS_IMETHODIMP
PKCS11Token::GetTokenFWVersion(/*out*/ nsACString& tokenFWVersion) {
  return GetAttributeHelper(mTokenFWVersion, tokenFWVersion);
}

NS_IMETHODIMP
PKCS11Token::GetTokenSerialNumber(/*out*/ nsACString& tokenSerialNum) {
  return GetAttributeHelper(mTokenSerialNum, tokenSerialNum);
}

NS_IMETHODIMP
PKCS11Token::GetIsLoggedIn(bool* isLoggedIn) {
  *isLoggedIn = PK11_IsLoggedIn(mSlot.get(), nullptr);
  return NS_OK;
}

NS_IMETHODIMP
PKCS11Token::Login(JSContext* aCx, Promise** aPromise) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!NS_IsMainThread()) {
    return NS_ERROR_NOT_SAME_THREAD;
  }

  ErrorResult result;
  RefPtr<Promise> promise =
      Promise::Create(xpc::CurrentNativeGlobal(aCx), result);
  if (result.Failed()) {
    return result.StealNSResult();
  }
  auto promiseHolder =
      MakeRefPtr<nsMainThreadPtrHolder<Promise>>("Login promise", promise);

  nsCOMPtr<nsIRunnable> runnable(NS_NewRunnableFunction(
      "Login runnable",
      [promiseHolder,
       slot = UniquePK11SlotInfo(PK11_ReferenceSlot(mSlot.get()))]() {
        nsresult rv =
            mozilla::MapSECStatus(PK11_Authenticate(slot.get(), true, nullptr));
        NS_DispatchToMainThread(
            NS_NewRunnableFunction("Login callback", [rv, promiseHolder] {
              if (NS_SUCCEEDED(rv)) {
                promiseHolder->get()->MaybeResolveWithUndefined();
              } else {
                promiseHolder->get()->MaybeReject(rv);
              }
            }));
      }));

  promise.forget(aPromise);
  return NS_DispatchBackgroundTask(runnable.forget());
}

NS_IMETHODIMP
PKCS11Token::Logout(JSContext* aCx, Promise** aPromise) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!NS_IsMainThread()) {
    return NS_ERROR_NOT_SAME_THREAD;
  }

  ErrorResult result;
  RefPtr<Promise> promise =
      Promise::Create(xpc::CurrentNativeGlobal(aCx), result);
  if (result.Failed()) {
    return result.StealNSResult();
  }
  auto promiseHolder =
      MakeRefPtr<nsMainThreadPtrHolder<Promise>>("Logout promise", promise);

  nsCOMPtr<nsIRunnable> runnable(NS_NewRunnableFunction(
      "Logout runnable",
      [promiseHolder,
       slot = UniquePK11SlotInfo(PK11_ReferenceSlot(mSlot.get()))]() {
        // PK11_Logout() can fail if the user wasn't logged in beforehand. We
        // want this method to succeed even in this case, so we ignore the
        // return value.
        (void)PK11_Logout(slot.get());
        NS_DispatchToMainThread(
            NS_NewRunnableFunction("Logout callback", [promiseHolder] {
              promiseHolder->get()->MaybeResolveWithUndefined();
            }));
      }));

  promise.forget(aPromise);
  return NS_DispatchBackgroundTask(runnable.forget());
}

nsresult DoReset(const UniquePK11SlotInfo& slot, bool isInternalKeyToken) {
  SECStatus rv = PK11_ResetToken(slot.get(), nullptr);
  if (rv != SECSuccess) {
    return mozilla::MapSECStatus(rv);
  }
  // If this is the internal key token, set an empty password to enable the SQL
  // DB to work properly.
  if (isInternalKeyToken) {
    rv = PK11_InitPin(slot.get(), nullptr, nullptr);
    if (rv != SECSuccess) {
      return mozilla::MapSECStatus(rv);
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
PKCS11Token::Reset(JSContext* aCx, Promise** aPromise) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!NS_IsMainThread()) {
    return NS_ERROR_NOT_SAME_THREAD;
  }

  ErrorResult result;
  RefPtr<Promise> promise =
      Promise::Create(xpc::CurrentNativeGlobal(aCx), result);
  if (result.Failed()) {
    return result.StealNSResult();
  }
  auto promiseHolder =
      MakeRefPtr<nsMainThreadPtrHolder<Promise>>("Reset promise", promise);

  nsCOMPtr<nsIRunnable> runnable(NS_NewRunnableFunction(
      "Reset runnable",
      [promiseHolder,
       slot = UniquePK11SlotInfo(PK11_ReferenceSlot(mSlot.get())),
       isInternalKeyToken(mIsInternalKeyToken)]() {
        nsresult rv = DoReset(slot, isInternalKeyToken);
        NS_DispatchToMainThread(
            NS_NewRunnableFunction("Reset callback", [rv, promiseHolder] {
              if (NS_SUCCEEDED(rv)) {
                promiseHolder->get()->MaybeResolveWithUndefined();
              } else {
                promiseHolder->get()->MaybeReject(rv);
              }
            }));
      }));

  promise.forget(aPromise);
  return NS_DispatchBackgroundTask(runnable.forget());
}

nsresult DoChangePassword(const UniquePK11SlotInfo& slot,
                          const nsACString& oldPassword,
                          const nsACString& newPassword) {
  if (oldPassword.IsEmpty() && PK11_NeedUserInit(slot.get())) {
    return mozilla::MapSECStatus(
        PK11_InitPin(slot.get(), "", PromiseFlatCString(newPassword).get()));
  }
  SECStatus rv =
      PK11_CheckUserPassword(slot.get(), PromiseFlatCString(oldPassword).get());
  if (rv != SECSuccess) {
    return mozilla::MapSECStatus(rv);
  }
  return mozilla::MapSECStatus(
      PK11_ChangePW(slot.get(), PromiseFlatCString(oldPassword).get(),
                    PromiseFlatCString(newPassword).get()));
}

NS_IMETHODIMP
PKCS11Token::ChangePassword(const nsACString& oldPassword,
                            const nsACString& newPassword, JSContext* aCx,
                            Promise** aPromise) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!NS_IsMainThread()) {
    return NS_ERROR_NOT_SAME_THREAD;
  }

  ErrorResult result;
  RefPtr<Promise> promise =
      Promise::Create(xpc::CurrentNativeGlobal(aCx), result);
  if (result.Failed()) {
    return result.StealNSResult();
  }
  auto promiseHolder = MakeRefPtr<nsMainThreadPtrHolder<Promise>>(
      "ChangePassword promise", promise);

  nsCOMPtr<nsIRunnable> runnable(NS_NewRunnableFunction(
      "ChangePassword runnable",
      [promiseHolder, oldPassword = nsCString(oldPassword),
       newPassword = nsCString(newPassword),
       slot = UniquePK11SlotInfo(PK11_ReferenceSlot(mSlot.get()))]() {
        nsresult rv = DoChangePassword(slot, oldPassword, newPassword);
        NS_DispatchToMainThread(NS_NewRunnableFunction(
            "ChangePassword callback", [rv, promiseHolder] {
              if (NS_SUCCEEDED(rv)) {
                promiseHolder->get()->MaybeResolveWithUndefined();
              } else {
                promiseHolder->get()->MaybeReject(rv);
              }
            }));
      }));

  promise.forget(aPromise);
  return NS_DispatchBackgroundTask(runnable.forget());
}

// Two PKCS#11 flags are relevant here: CKF_LOGIN_REQUIRED and
// CKF_USER_PIN_INITIALIZED.
// CKF_LOGIN_REQUIRED is set if there are some cryptographic operations on the
// token that require logging in to perform. If this flag is not set, no
// password is required.
// CKF_USER_PIN_INITIALIZED is set if the token's user pin (password) has been
// set.
// CKF_LOGIN_REQUIRED is obtained by calling PK11_NeedLogin.
// CKF_USER_PIN_INITIALIZED is obtained by negating the result of calling
// PK11_NeedUserInit.

NS_IMETHODIMP
PKCS11Token::GetCanHavePassword(bool* canHavePassword) {
  // A token is considered able to have a password if CKF_LOGIN_REQUIRED is set
  // or if CKF_USER_PIN_INITIALIZED is set.
  *canHavePassword =
      PK11_NeedLogin(mSlot.get()) || !PK11_NeedUserInit(mSlot.get());
  ;
  return NS_OK;
}

NS_IMETHODIMP
PKCS11Token::GetHasPassword(bool* hasPassword) {
  // A token has a password if CKF_LOGIN_REQUIRED is set and
  // CKF_USER_PIN_INITIALIZED is set.
  *hasPassword = PK11_NeedLogin(mSlot.get()) && !PK11_NeedUserInit(mSlot.get());
  return NS_OK;
}

#if !defined(MOZ_NO_SMART_CARDS)
nsresult PKCS11Token::GetTokenInfo(TokenInfo& tokenInfo) {
  tokenInfo.moduleID() = PK11_GetModuleID(mSlot.get());
  tokenInfo.slotID() = PK11_GetSlotID(mSlot.get());
  nsresult rv = GetTokenName(tokenInfo.name());
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = GetTokenManID(tokenInfo.manID());
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = GetTokenHWVersion(tokenInfo.hwVersion());
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = GetTokenFWVersion(tokenInfo.fwVersion());
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = GetTokenSerialNumber(tokenInfo.serialNumber());
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = GetIsLoggedIn(&tokenInfo.isLoggedIn());
  if (NS_FAILED(rv)) {
    return rv;
  }
  rv = GetCanHavePassword(&tokenInfo.canHavePassword());
  if (NS_FAILED(rv)) {
    return rv;
  }
  return GetHasPassword(&tokenInfo.hasPassword());
}

NS_IMPL_ISUPPORTS(RemotePKCS11Token, nsIPKCS11Token)

NS_IMETHODIMP
RemotePKCS11Token::GetTokenName(nsACString& tokenName) {
  tokenName.Assign(mTokenInfo.name());
  return NS_OK;
}

NS_IMETHODIMP
RemotePKCS11Token::GetIsInternalKeyToken(bool* isInternalKeyToken) {
  *isInternalKeyToken = false;
  return NS_OK;
}

NS_IMETHODIMP
RemotePKCS11Token::GetTokenManID(nsACString& tokenManID) {
  tokenManID.Assign(mTokenInfo.manID());
  return NS_OK;
}

NS_IMETHODIMP
RemotePKCS11Token::GetTokenHWVersion(nsACString& tokenHWVersion) {
  tokenHWVersion.Assign(mTokenInfo.hwVersion());
  return NS_OK;
}

NS_IMETHODIMP
RemotePKCS11Token::GetTokenFWVersion(nsACString& tokenFWVersion) {
  tokenFWVersion.Assign(mTokenInfo.fwVersion());
  return NS_OK;
}

NS_IMETHODIMP
RemotePKCS11Token::GetTokenSerialNumber(nsACString& tokenSerialNumber) {
  tokenSerialNumber.Assign(mTokenInfo.serialNumber());
  return NS_OK;
}

NS_IMETHODIMP
RemotePKCS11Token::GetIsLoggedIn(bool* isLoggedIn) {
  *isLoggedIn = mTokenInfo.isLoggedIn();
  return NS_OK;
}

NS_IMETHODIMP
RemotePKCS11Token::Login(JSContext* aCx, Promise** aPromise) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!NS_IsMainThread()) {
    return NS_ERROR_NOT_SAME_THREAD;
  }

  RefPtr<PKCS11ModuleDB> pkcs11ModuleDB(PKCS11ModuleDB::GetSingleton());
  if (!pkcs11ModuleDB) {
    return NS_ERROR_FAILURE;
  }

  ErrorResult result;
  RefPtr<Promise> promise =
      Promise::Create(xpc::CurrentNativeGlobal(aCx), result);
  if (result.Failed()) {
    return result.StealNSResult();
  }

  pkcs11ModuleDB->LoginToken(mTokenInfo.moduleID(), mTokenInfo.slotID())
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [self = RefPtr{this}, promise](
                 const PKCS11ModuleDB::TokenInfoPromise::ResolveOrRejectValue&
                     aValue) {
               if (aValue.IsResolve()) {
                 self->mTokenInfo = std::move(aValue.ResolveValue());
                 promise->MaybeResolveWithUndefined();
               } else {
                 promise->MaybeReject(aValue.RejectValue());
               }
             });

  promise.forget(aPromise);
  return NS_OK;
}

NS_IMETHODIMP
RemotePKCS11Token::Logout(JSContext* aCx, Promise** aPromise) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!NS_IsMainThread()) {
    return NS_ERROR_NOT_SAME_THREAD;
  }

  RefPtr<PKCS11ModuleDB> pkcs11ModuleDB(PKCS11ModuleDB::GetSingleton());
  if (!pkcs11ModuleDB) {
    return NS_ERROR_FAILURE;
  }

  ErrorResult result;
  RefPtr<Promise> promise =
      Promise::Create(xpc::CurrentNativeGlobal(aCx), result);
  if (result.Failed()) {
    return result.StealNSResult();
  }

  pkcs11ModuleDB->LogoutToken(mTokenInfo.moduleID(), mTokenInfo.slotID())
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [self = RefPtr{this}, promise](
                 const PKCS11ModuleDB::TokenInfoPromise::ResolveOrRejectValue&
                     aValue) {
               if (aValue.IsResolve()) {
                 self->mTokenInfo = std::move(aValue.ResolveValue());
                 promise->MaybeResolveWithUndefined();
               } else {
                 promise->MaybeReject(aValue.RejectValue());
               }
             });

  promise.forget(aPromise);
  return NS_OK;
}

NS_IMETHODIMP
RemotePKCS11Token::Reset(JSContext* aCx, Promise** aPromise) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!NS_IsMainThread()) {
    return NS_ERROR_NOT_SAME_THREAD;
  }

  RefPtr<PKCS11ModuleDB> pkcs11ModuleDB(PKCS11ModuleDB::GetSingleton());
  if (!pkcs11ModuleDB) {
    return NS_ERROR_FAILURE;
  }

  ErrorResult result;
  RefPtr<Promise> promise =
      Promise::Create(xpc::CurrentNativeGlobal(aCx), result);
  if (result.Failed()) {
    return result.StealNSResult();
  }

  pkcs11ModuleDB->ResetToken(mTokenInfo.moduleID(), mTokenInfo.slotID())
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [self = RefPtr{this}, promise](
                 const PKCS11ModuleDB::TokenInfoPromise::ResolveOrRejectValue&
                     aValue) {
               if (aValue.IsResolve()) {
                 self->mTokenInfo = std::move(aValue.ResolveValue());
                 promise->MaybeResolveWithUndefined();
               } else {
                 promise->MaybeReject(aValue.RejectValue());
               }
             });

  promise.forget(aPromise);
  return NS_OK;
}

NS_IMETHODIMP
RemotePKCS11Token::ChangePassword(const nsACString& oldPassword,
                                  const nsACString& newPassword, JSContext* aCx,
                                  Promise** aPromise) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!NS_IsMainThread()) {
    return NS_ERROR_NOT_SAME_THREAD;
  }

  RefPtr<PKCS11ModuleDB> pkcs11ModuleDB(PKCS11ModuleDB::GetSingleton());
  if (!pkcs11ModuleDB) {
    return NS_ERROR_FAILURE;
  }

  ErrorResult result;
  RefPtr<Promise> promise =
      Promise::Create(xpc::CurrentNativeGlobal(aCx), result);
  if (result.Failed()) {
    return result.StealNSResult();
  }

  pkcs11ModuleDB
      ->ChangeTokenPassword(mTokenInfo.moduleID(), mTokenInfo.slotID(),
                            PromiseFlatCString(oldPassword),
                            PromiseFlatCString(newPassword))
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [self = RefPtr{this}, promise](
                 const PKCS11ModuleDB::TokenInfoPromise::ResolveOrRejectValue&
                     aValue) {
               if (aValue.IsResolve()) {
                 self->mTokenInfo = std::move(aValue.ResolveValue());
                 promise->MaybeResolveWithUndefined();
               } else {
                 promise->MaybeReject(aValue.RejectValue());
               }
             });

  promise.forget(aPromise);
  return NS_OK;
}

NS_IMETHODIMP
RemotePKCS11Token::GetCanHavePassword(bool* canHavePassword) {
  *canHavePassword = mTokenInfo.canHavePassword();
  return NS_OK;
}

NS_IMETHODIMP
RemotePKCS11Token::GetHasPassword(bool* hasPassword) {
  *hasPassword = mTokenInfo.hasPassword();
  return NS_OK;
}
#endif  // !MOZ_NO_SMART_CARDS
