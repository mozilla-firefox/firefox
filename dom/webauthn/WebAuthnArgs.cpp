/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebAuthnArgs.h"
#include "WebAuthnEnumStrings.h"
#include "WebAuthnUtil.h"
#include "mozilla/dom/PWebAuthnTransactionParent.h"

namespace mozilla::dom {

NS_IMPL_ISUPPORTS(WebAuthnRegisterArgs, nsIWebAuthnRegisterArgs)

NS_IMETHODIMP
WebAuthnRegisterArgs::GetOrigin(nsAString& aOrigin) {
  aOrigin = NS_ConvertUTF8toUTF16(mOrigin);
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnRegisterArgs::GetChallenge(nsTArray<uint8_t>& aChallenge) {
  aChallenge.Assign(mInfo.Challenge());
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnRegisterArgs::GetClientDataJSON(nsACString& aClientDataJSON) {
  aClientDataJSON = mClientDataJSON;
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnRegisterArgs::GetClientDataHash(nsTArray<uint8_t>& aClientDataHash) {
  nsresult rv = HashCString(mClientDataJSON, aClientDataHash);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

NS_IMETHODIMP
WebAuthnRegisterArgs::GetRpId(nsAString& aRpId) {
  aRpId = NS_ConvertUTF8toUTF16(mInfo.RpId());
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnRegisterArgs::GetRpName(nsAString& aRpName) {
  aRpName = mInfo.Rp().Name();
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnRegisterArgs::GetUserId(nsTArray<uint8_t>& aUserId) {
  aUserId.Assign(mInfo.User().Id());
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnRegisterArgs::GetUserName(nsAString& aUserName) {
  aUserName = mInfo.User().Name();
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnRegisterArgs::GetUserDisplayName(nsAString& aUserDisplayName) {
  aUserDisplayName = mInfo.User().DisplayName();
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnRegisterArgs::GetCoseAlgs(nsTArray<int32_t>& aCoseAlgs) {
  aCoseAlgs.Clear();
  for (const CoseAlg& coseAlg : mInfo.coseAlgs()) {
    aCoseAlgs.AppendElement(coseAlg.alg());
  }
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnRegisterArgs::GetExcludeList(
    nsTArray<nsTArray<uint8_t>>& aExcludeList) {
  aExcludeList.Clear();
  for (const WebAuthnScopedCredential& cred : mInfo.ExcludeList()) {
    aExcludeList.AppendElement(cred.id().Clone());
  }
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnRegisterArgs::GetExcludeListTransports(
    nsTArray<uint8_t>& aExcludeListTransports) {
  aExcludeListTransports.Clear();
  for (const WebAuthnScopedCredential& cred : mInfo.ExcludeList()) {
    aExcludeListTransports.AppendElement(cred.transports());
  }
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnRegisterArgs::GetCredentialProtectionPolicy(
    nsACString& aCredentialProtectionPolicy) {
  if (mCredentialProtectionPolicy.isSome()) {
    aCredentialProtectionPolicy =
        GetEnumString(mCredentialProtectionPolicy.ref());
    return NS_OK;
  }
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
WebAuthnRegisterArgs::GetEnforceCredentialProtectionPolicy(
    bool* aEnforceCredentialProtectionPolicy) {
  *aEnforceCredentialProtectionPolicy = mEnforceCredentialProtectionPolicy;

  return NS_OK;
}

NS_IMETHODIMP
WebAuthnRegisterArgs::GetCredProps(bool* aCredProps) {
  *aCredProps = mCredProps;

  return NS_OK;
}

NS_IMETHODIMP
WebAuthnRegisterArgs::GetHmacCreateSecret(bool* aHmacCreateSecret) {
  *aHmacCreateSecret = mHmacCreateSecret;

  return NS_OK;
}

NS_IMETHODIMP
WebAuthnRegisterArgs::GetPrf(bool* aPrf) {
  *aPrf = mPrf;
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnRegisterArgs::GetPrfEvalFirst(nsTArray<uint8_t>& aEvalFirst) {
  for (const WebAuthnExtension& ext : mInfo.Extensions()) {
    if (ext.type() == WebAuthnExtension::TWebAuthnExtensionPrf) {
      Maybe<WebAuthnExtensionPrfValues> eval =
          ext.get_WebAuthnExtensionPrf().eval();
      if (eval.isSome()) {
        aEvalFirst.Assign(eval->first());
        return NS_OK;
      }
      break;
    }
  }

  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
WebAuthnRegisterArgs::GetPrfEvalSecond(nsTArray<uint8_t>& aEvalSecond) {
  for (const WebAuthnExtension& ext : mInfo.Extensions()) {
    if (ext.type() == WebAuthnExtension::TWebAuthnExtensionPrf) {
      Maybe<WebAuthnExtensionPrfValues> eval =
          ext.get_WebAuthnExtensionPrf().eval();
      if (eval.isSome() && eval->secondMaybe()) {
        aEvalSecond.Assign(eval->second());
        return NS_OK;
      }
      break;
    }
  }

  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
WebAuthnRegisterArgs::GetMinPinLength(bool* aMinPinLength) {
  *aMinPinLength = mMinPinLength;

  return NS_OK;
}

NS_IMETHODIMP
WebAuthnRegisterArgs::GetResidentKey(nsAString& aResidentKey) {
  aResidentKey = mInfo.AuthenticatorSelection().residentKey();
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnRegisterArgs::GetUserVerification(
    nsAString& aUserVerificationRequirement) {
  aUserVerificationRequirement =
      mInfo.AuthenticatorSelection().userVerificationRequirement();
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnRegisterArgs::GetAuthenticatorAttachment(
    nsAString& aAuthenticatorAttachment) {
  if (mInfo.AuthenticatorSelection().authenticatorAttachment().isNothing()) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  aAuthenticatorAttachment =
      *mInfo.AuthenticatorSelection().authenticatorAttachment();
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnRegisterArgs::GetTimeoutMS(uint32_t* aTimeoutMS) {
  *aTimeoutMS = mInfo.TimeoutMS();
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnRegisterArgs::GetAttestationConveyancePreference(
    nsAString& aAttestationConveyancePreference) {
  const nsString& attPref = mInfo.attestationConveyancePreference();
  if (attPref.EqualsLiteral(
          MOZ_WEBAUTHN_ATTESTATION_CONVEYANCE_PREFERENCE_INDIRECT) ||
      attPref.EqualsLiteral(
          MOZ_WEBAUTHN_ATTESTATION_CONVEYANCE_PREFERENCE_DIRECT) ||
      attPref.EqualsLiteral(
          MOZ_WEBAUTHN_ATTESTATION_CONVEYANCE_PREFERENCE_ENTERPRISE)) {
    aAttestationConveyancePreference.Assign(attPref);
  } else {
    aAttestationConveyancePreference.AssignLiteral(
        MOZ_WEBAUTHN_ATTESTATION_CONVEYANCE_PREFERENCE_NONE);
  }
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnRegisterArgs::GetPrivateBrowsing(bool* aPrivateBrowsing) {
  *aPrivateBrowsing = mPrivateBrowsing;
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnRegisterArgs::GetLargeBlobSupportRequired(
    bool* aLargeBlobSupportRequired) {
  if (mLargeBlobSupportRequired.isSome()) {
    *aLargeBlobSupportRequired = mLargeBlobSupportRequired.ref();
    return NS_OK;
  }
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMPL_ISUPPORTS(WebAuthnSignArgs, nsIWebAuthnSignArgs)

NS_IMETHODIMP
WebAuthnSignArgs::GetOrigin(nsAString& aOrigin) {
  aOrigin = NS_ConvertUTF8toUTF16(mOrigin);
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnSignArgs::GetRpId(nsAString& aRpId) {
  aRpId = NS_ConvertUTF8toUTF16(mInfo.RpId());
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnSignArgs::GetChallenge(nsTArray<uint8_t>& aChallenge) {
  aChallenge.Assign(mInfo.Challenge());
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnSignArgs::GetClientDataJSON(nsACString& aClientDataJSON) {
  aClientDataJSON = mClientDataJSON;
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnSignArgs::GetClientDataHash(nsTArray<uint8_t>& aClientDataHash) {
  nsresult rv = HashCString(mClientDataJSON, aClientDataHash);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

NS_IMETHODIMP
WebAuthnSignArgs::GetAllowList(nsTArray<nsTArray<uint8_t>>& aAllowList) {
  aAllowList.Clear();
  for (const WebAuthnScopedCredential& cred : mInfo.AllowList()) {
    aAllowList.AppendElement(cred.id().Clone());
  }
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnSignArgs::GetAllowListTransports(
    nsTArray<uint8_t>& aAllowListTransports) {
  aAllowListTransports.Clear();
  for (const WebAuthnScopedCredential& cred : mInfo.AllowList()) {
    aAllowListTransports.AppendElement(cred.transports());
  }
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnSignArgs::GetHmacCreateSecret(bool* aHmacCreateSecret) {
  for (const WebAuthnExtension& ext : mInfo.Extensions()) {
    if (ext.type() == WebAuthnExtension::TWebAuthnExtensionHmacSecret) {
      *aHmacCreateSecret =
          ext.get_WebAuthnExtensionHmacSecret().hmacCreateSecret();
      return NS_OK;
    }
  }

  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
WebAuthnSignArgs::GetAppId(nsAString& aAppId) {
  if (mInfo.AppId().isNothing()) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  aAppId = NS_ConvertUTF8toUTF16(mInfo.AppId().ref());
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnSignArgs::GetPrf(bool* aPrf) {
  *aPrf = mPrf;
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnSignArgs::GetPrfEvalFirst(nsTArray<uint8_t>& aEvalFirst) {
  for (const WebAuthnExtension& ext : mInfo.Extensions()) {
    if (ext.type() == WebAuthnExtension::TWebAuthnExtensionPrf) {
      Maybe<WebAuthnExtensionPrfValues> eval =
          ext.get_WebAuthnExtensionPrf().eval();
      if (eval.isSome()) {
        aEvalFirst.Assign(eval->first());
        return NS_OK;
      }
      break;
    }
  }

  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
WebAuthnSignArgs::GetPrfEvalSecond(nsTArray<uint8_t>& aEvalSecond) {
  for (const WebAuthnExtension& ext : mInfo.Extensions()) {
    if (ext.type() == WebAuthnExtension::TWebAuthnExtensionPrf) {
      Maybe<WebAuthnExtensionPrfValues> eval =
          ext.get_WebAuthnExtensionPrf().eval();
      if (eval.isSome() && eval->secondMaybe()) {
        aEvalSecond.Assign(eval->second());
        return NS_OK;
      }
      break;
    }
  }

  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
WebAuthnSignArgs::GetPrfEvalByCredentialCredentialId(
    nsTArray<nsTArray<uint8_t>>& aCredentialIds) {
  for (const WebAuthnExtension& ext : mInfo.Extensions()) {
    if (ext.type() == WebAuthnExtension::TWebAuthnExtensionPrf) {
      if (ext.get_WebAuthnExtensionPrf().evalByCredentialMaybe()) {
        for (const WebAuthnExtensionPrfEvalByCredentialEntry& entry :
             ext.get_WebAuthnExtensionPrf().evalByCredential()) {
          aCredentialIds.AppendElement(entry.credentialId().Clone());
        }
        return NS_OK;
      }
      break;
    }
  }

  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
WebAuthnSignArgs::GetPrfEvalByCredentialEvalFirst(
    nsTArray<nsTArray<uint8_t>>& aEvalFirsts) {
  for (const WebAuthnExtension& ext : mInfo.Extensions()) {
    if (ext.type() == WebAuthnExtension::TWebAuthnExtensionPrf) {
      if (ext.get_WebAuthnExtensionPrf().evalByCredentialMaybe()) {
        for (const WebAuthnExtensionPrfEvalByCredentialEntry& entry :
             ext.get_WebAuthnExtensionPrf().evalByCredential()) {
          aEvalFirsts.AppendElement(entry.eval().first().Clone());
        }
        return NS_OK;
      }
      break;
    }
  }

  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
WebAuthnSignArgs::GetPrfEvalByCredentialEvalSecondMaybe(
    nsTArray<bool>& aEvalSecondMaybes) {
  for (const WebAuthnExtension& ext : mInfo.Extensions()) {
    if (ext.type() == WebAuthnExtension::TWebAuthnExtensionPrf) {
      if (ext.get_WebAuthnExtensionPrf().evalByCredentialMaybe()) {
        for (const WebAuthnExtensionPrfEvalByCredentialEntry& entry :
             ext.get_WebAuthnExtensionPrf().evalByCredential()) {
          aEvalSecondMaybes.AppendElement(entry.eval().secondMaybe());
        }
        return NS_OK;
      }
      break;
    }
  }

  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
WebAuthnSignArgs::GetPrfEvalByCredentialEvalSecond(
    nsTArray<nsTArray<uint8_t>>& aEvalSeconds) {
  for (const WebAuthnExtension& ext : mInfo.Extensions()) {
    if (ext.type() == WebAuthnExtension::TWebAuthnExtensionPrf) {
      if (ext.get_WebAuthnExtensionPrf().evalByCredentialMaybe()) {
        for (const WebAuthnExtensionPrfEvalByCredentialEntry& entry :
             ext.get_WebAuthnExtensionPrf().evalByCredential()) {
          if (entry.eval().secondMaybe()) {
            aEvalSeconds.AppendElement(entry.eval().second().Clone());
          } else {
            aEvalSeconds.AppendElement(nsTArray<uint8_t>());
          }
        }
        return NS_OK;
      }
      break;
    }
  }

  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
WebAuthnSignArgs::GetUserVerification(nsAString& aUserVerificationRequirement) {
  aUserVerificationRequirement = mInfo.userVerificationRequirement();
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnSignArgs::GetTimeoutMS(uint32_t* aTimeoutMS) {
  *aTimeoutMS = mInfo.TimeoutMS();
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnSignArgs::GetConditionallyMediated(bool* aConditionallyMediated) {
  *aConditionallyMediated = mInfo.ConditionallyMediated();
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnSignArgs::GetPrivateBrowsing(bool* aPrivateBrowsing) {
  *aPrivateBrowsing = mPrivateBrowsing;
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnSignArgs::GetLargeBlobRead(bool* aLargeBlobRead) {
  if (mLargeBlobRead.isSome()) {
    *aLargeBlobRead = mLargeBlobRead.ref();
    return NS_OK;
  }
  return NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
WebAuthnSignArgs::GetLargeBlobWrite(nsTArray<uint8_t>& aLargeBlobWrite) {
  if (mLargeBlobRead.isSome() && mLargeBlobRead.ref() == false) {
    aLargeBlobWrite.Assign(mLargeBlobWrite);
    return NS_OK;
  }
  return NS_ERROR_NOT_AVAILABLE;
}

}  // namespace mozilla::dom
