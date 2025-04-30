/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AuthrsBridge_ffi.h"
#include "WebAuthnResult.h"
#include "nsIWebAuthnAttObj.h"
#include "nsCOMPtr.h"
#include "nsString.h"

#ifdef MOZ_WIDGET_ANDROID
namespace mozilla::jni {

template <>
RefPtr<dom::WebAuthnRegisterResult> Java2Native(
    mozilla::jni::Object::Param aData, JNIEnv* aEnv) {
  MOZ_ASSERT(aData.IsInstanceOf<java::WebAuthnUtils::MakeCredentialResponse>());
  java::WebAuthnUtils::MakeCredentialResponse::LocalRef response(aData);
  RefPtr<dom::WebAuthnRegisterResult> result =
      new dom::WebAuthnRegisterResult(response);
  return result;
}

template <>
RefPtr<dom::WebAuthnSignResult> Java2Native(mozilla::jni::Object::Param aData,
                                            JNIEnv* aEnv) {
  MOZ_ASSERT(aData.IsInstanceOf<java::WebAuthnUtils::GetAssertionResponse>());
  java::WebAuthnUtils::GetAssertionResponse::LocalRef response(aData);
  RefPtr<dom::WebAuthnSignResult> result =
      new dom::WebAuthnSignResult(response);
  return result;
}

}  // namespace mozilla::jni
#endif

namespace mozilla::dom {

NS_IMPL_ISUPPORTS(WebAuthnRegisterResult, nsIWebAuthnRegisterResult)

NS_IMETHODIMP
WebAuthnRegisterResult::GetClientDataJSON(nsACString& aClientDataJSON) {
  if (mClientDataJSON.isSome()) {
    aClientDataJSON = *mClientDataJSON;
    return NS_OK;
  }
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
WebAuthnRegisterResult::GetAttestationObject(
    nsTArray<uint8_t>& aAttestationObject) {
  aAttestationObject.Assign(mAttestationObject);
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnRegisterResult::GetCredentialId(nsTArray<uint8_t>& aCredentialId) {
  aCredentialId.Assign(mCredentialId);
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnRegisterResult::GetTransports(nsTArray<nsString>& aTransports) {
  aTransports.Assign(mTransports);
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnRegisterResult::GetHmacCreateSecret(bool* aHmacCreateSecret) {
  if (mHmacCreateSecret.isSome()) {
    *aHmacCreateSecret = mHmacCreateSecret.ref();
    return NS_OK;
  }
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
WebAuthnRegisterResult::GetCredPropsRk(bool* aCredPropsRk) {
  if (mCredPropsRk.isSome()) {
    *aCredPropsRk = mCredPropsRk.ref();
    return NS_OK;
  }
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
WebAuthnRegisterResult::SetCredPropsRk(bool aCredPropsRk) {
  mCredPropsRk = Some(aCredPropsRk);
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnRegisterResult::GetLargeBlobSupported(bool* aLargeBlobSupported) {
  if (mLargeBlobSupported.isSome()) {
    *aLargeBlobSupported = mLargeBlobSupported.ref();
    return NS_OK;
  }
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
WebAuthnRegisterResult::GetPrfEnabled(bool* aPrfEnabled) {
  if (mPrfSupported.isSome()) {
    *aPrfEnabled = mPrfSupported.ref();
    return NS_OK;
  }
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
WebAuthnRegisterResult::GetPrfResultsFirst(
    nsTArray<uint8_t>& aPrfResultsFirst) {
  if (mPrfFirst.isSome()) {
    aPrfResultsFirst.Assign(mPrfFirst.ref());
    return NS_OK;
  }
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
WebAuthnRegisterResult::GetPrfResultsSecond(
    nsTArray<uint8_t>& aPrfResultsSecond) {
  if (mPrfSecond.isSome()) {
    aPrfResultsSecond.Assign(mPrfSecond.ref());
    return NS_OK;
  }
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
WebAuthnRegisterResult::GetAuthenticatorAttachment(
    nsAString& aAuthenticatorAttachment) {
  if (mAuthenticatorAttachment.isSome()) {
    aAuthenticatorAttachment = mAuthenticatorAttachment.ref();
    return NS_OK;
  }
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
WebAuthnRegisterResult::HasIdentifyingAttestation(
    bool* aHasIdentifyingAttestation) {
  // Assume the attestation statement is identifying in case the constructor or
  // the getter below fail.
  bool isIdentifying = true;

  nsCOMPtr<nsIWebAuthnAttObj> attObj;
  nsresult rv = authrs_webauthn_att_obj_constructor(mAttestationObject,
                                                    /* anonymize */ false,
                                                    getter_AddRefs(attObj));
  if (NS_SUCCEEDED(rv)) {
    Unused << attObj->IsIdentifying(&isIdentifying);
  }

  *aHasIdentifyingAttestation = isIdentifying;
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnRegisterResult::Anonymize() {
  // The anonymize flag in the nsIWebAuthnAttObj constructor causes the
  // attestation statement to be removed during deserialization. It also
  // causes the AAGUID to be zeroed out. If we can't deserialize the
  // existing attestation, then we can't ensure that it is anonymized, so we
  // act as though the user denied consent and we return NotAllowed.
  nsCOMPtr<nsIWebAuthnAttObj> anonymizedAttObj;
  nsresult rv = authrs_webauthn_att_obj_constructor(
      mAttestationObject,
      /* anonymize */ true, getter_AddRefs(anonymizedAttObj));
  if (NS_FAILED(rv)) {
    return rv;
  }
  mAttestationObject.Clear();
  rv = anonymizedAttObj->GetAttestationObject(mAttestationObject);
  if (NS_FAILED(rv)) {
    return rv;
  }
  return NS_OK;
}

NS_IMPL_ISUPPORTS(WebAuthnSignResult, nsIWebAuthnSignResult)

NS_IMETHODIMP
WebAuthnSignResult::GetClientDataJSON(nsACString& aClientDataJSON) {
  if (mClientDataJSON.isSome()) {
    aClientDataJSON = *mClientDataJSON;
    return NS_OK;
  }
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
WebAuthnSignResult::GetAuthenticatorData(
    nsTArray<uint8_t>& aAuthenticatorData) {
  aAuthenticatorData.Assign(mAuthenticatorData);
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnSignResult::GetCredentialId(nsTArray<uint8_t>& aCredentialId) {
  aCredentialId.Assign(mCredentialId);
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnSignResult::GetSignature(nsTArray<uint8_t>& aSignature) {
  aSignature.Assign(mSignature);
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnSignResult::GetUserHandle(nsTArray<uint8_t>& aUserHandle) {
  aUserHandle.Assign(mUserHandle);
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnSignResult::GetUserName(nsACString& aUserName) {
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
WebAuthnSignResult::GetUsedAppId(bool* aUsedAppId) {
  if (mUsedAppId.isNothing()) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  *aUsedAppId = mUsedAppId.ref();
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnSignResult::SetUsedAppId(bool aUsedAppId) {
  mUsedAppId = Some(aUsedAppId);
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnSignResult::GetLargeBlobValue(nsTArray<uint8_t>& aLargeBlobValue) {
  if (mLargeBlobValue.isSome()) {
    aLargeBlobValue.Assign(*mLargeBlobValue);
    return NS_OK;
  }
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
WebAuthnSignResult::GetLargeBlobWritten(bool* aLargeBlobWritten) {
  if (mLargeBlobWritten.isSome()) {
    *aLargeBlobWritten = mLargeBlobWritten.ref();
    return NS_OK;
  }
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
WebAuthnSignResult::GetPrfMaybe(bool* aPrfMaybe) {
  *aPrfMaybe = mPrfFirst.isSome();
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnSignResult::GetPrfResultsFirst(nsTArray<uint8_t>& aPrfResultsFirst) {
  if (mPrfFirst.isNothing()) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  aPrfResultsFirst.Assign(*mPrfFirst);
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnSignResult::GetPrfResultsSecond(nsTArray<uint8_t>& aPrfResultsSecond) {
  if (mPrfSecond.isNothing()) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  aPrfResultsSecond.Assign(*mPrfSecond);
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnSignResult::GetAuthenticatorAttachment(
    nsAString& aAuthenticatorAttachment) {
  if (mAuthenticatorAttachment.isSome()) {
    aAuthenticatorAttachment = mAuthenticatorAttachment.ref();
    return NS_OK;
  }
  return NS_ERROR_NOT_AVAILABLE;
}

}  // namespace mozilla::dom
