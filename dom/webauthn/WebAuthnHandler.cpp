/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "hasht.h"
#include "nsHTMLDocument.h"
#include "nsIURIMutator.h"
#include "nsThreadUtils.h"
#include "WebAuthnCoseIdentifiers.h"
#include "WebAuthnEnumStrings.h"
#include "WebAuthnTransportIdentifiers.h"
#include "mozilla/Base64.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/BounceTrackingProtection.h"
#include "mozilla/glean/DomWebauthnMetrics.h"
#include "mozilla/dom/AuthenticatorAssertionResponse.h"
#include "mozilla/dom/AuthenticatorAttestationResponse.h"
#include "mozilla/dom/PublicKeyCredential.h"
#include "mozilla/dom/PWebAuthnTransaction.h"
#include "mozilla/dom/WebAuthnHandler.h"
#include "mozilla/dom/WebAuthnTransactionChild.h"
#include "mozilla/dom/WebAuthnUtil.h"
#include "mozilla/dom/WindowGlobalChild.h"

#ifdef XP_WIN
#  include "WinWebAuthnService.h"
#endif

using namespace mozilla::ipc;

namespace mozilla::dom {

/***********************************************************************
 * Statics
 **********************************************************************/

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(WebAuthnHandler)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTION(WebAuthnHandler, mWindow, mTransaction)

NS_IMPL_CYCLE_COLLECTING_ADDREF(WebAuthnHandler)
NS_IMPL_CYCLE_COLLECTING_RELEASE(WebAuthnHandler)

/***********************************************************************
 * Utility Functions
 **********************************************************************/

static uint8_t SerializeTransports(
    const mozilla::dom::Sequence<nsString>& aTransports) {
  uint8_t transports = 0;

  // We ignore unknown transports for forward-compatibility, but this
  // needs to be reviewed if values are added to the
  // AuthenticatorTransport enum.
  static_assert(MOZ_WEBAUTHN_ENUM_STRINGS_VERSION == 3);
  for (const nsAString& str : aTransports) {
    if (str.EqualsLiteral(MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_USB)) {
      transports |= MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_USB;
    } else if (str.EqualsLiteral(MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_NFC)) {
      transports |= MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_NFC;
    } else if (str.EqualsLiteral(MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_BLE)) {
      transports |= MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_BLE;
    } else if (str.EqualsLiteral(
                   MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_INTERNAL)) {
      transports |= MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_INTERNAL;
    } else if (str.EqualsLiteral(MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_HYBRID)) {
      transports |= MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_HYBRID;
    }
  }
  return transports;
}

/***********************************************************************
 * WebAuthnHandler Implementation
 **********************************************************************/

WebAuthnHandler::~WebAuthnHandler() {
  MOZ_ASSERT(NS_IsMainThread());
  if (mActor) {
    if (mTransaction.isSome()) {
      CancelTransaction(NS_ERROR_DOM_ABORT_ERR);
    }
    mActor->SetHandler(nullptr);
  }
}

bool WebAuthnHandler::MaybeCreateActor() {
  MOZ_ASSERT(NS_IsMainThread());

  if (mActor) {
    return true;
  }

  RefPtr<WebAuthnTransactionChild> actor = new WebAuthnTransactionChild();

  WindowGlobalChild* windowGlobalChild = mWindow->GetWindowGlobalChild();
  if (!windowGlobalChild ||
      !windowGlobalChild->SendPWebAuthnTransactionConstructor(actor)) {
    return false;
  }

  mActor = actor;
  mActor->SetHandler(this);

  return true;
}

void WebAuthnHandler::ActorDestroyed() {
  MOZ_ASSERT(NS_IsMainThread());
  mActor = nullptr;
}

already_AddRefed<Promise> WebAuthnHandler::MakeCredential(
    const PublicKeyCredentialCreationOptions& aOptions,
    const Optional<OwningNonNull<AbortSignal>>& aSignal, ErrorResult& aError) {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(mWindow);

  RefPtr<Promise> promise = Promise::Create(global, aError);
  if (aError.Failed()) {
    return nullptr;
  }

  if (mTransaction.isSome()) {
    // abort the old transaction and take over control from here.
    CancelTransaction(NS_ERROR_DOM_ABORT_ERR);
  }

  if (!MaybeCreateActor()) {
    promise->MaybeReject(NS_ERROR_DOM_OPERATION_ERR);
    return promise.forget();
  }

  nsCOMPtr<Document> doc = mWindow->GetDoc();
  if (!IsWebAuthnAllowedInDocument(doc)) {
    promise->MaybeReject(NS_ERROR_DOM_SECURITY_ERR);
    return promise.forget();
  }

  nsCOMPtr<nsIPrincipal> principal = doc->NodePrincipal();
  if (!IsWebAuthnAllowedForPrincipal(principal)) {
    promise->MaybeReject(NS_ERROR_DOM_SECURITY_ERR);
    return promise.forget();
  }

  nsCString rpId;
  if (aOptions.mRp.mId.WasPassed()) {
    rpId = NS_ConvertUTF16toUTF8(aOptions.mRp.mId.Value());
  } else {
    nsresult rv = DefaultRpId(principal, rpId);
    if (NS_FAILED(rv)) {
      promise->MaybeReject(NS_ERROR_FAILURE);
      return promise.forget();
    }
  }
  if (!IsValidRpId(principal, rpId)) {
    promise->MaybeReject(NS_ERROR_DOM_SECURITY_ERR);
    return promise.forget();
  }

  // Enforce 5.4.3 User Account Parameters for Credential Generation
  // When we add UX, we'll want to do more with this value, but for now
  // we just have to verify its correctness.
  CryptoBuffer userId;
  userId.Assign(aOptions.mUser.mId);
  if (userId.Length() > 64) {
    promise->MaybeRejectWithTypeError("user.id is too long");
    return promise.forget();
  }

  // If timeoutSeconds was specified, check if its value lies within a
  // reasonable range as defined by the platform and if not, correct it to the
  // closest value lying within that range.

  uint32_t adjustedTimeout = 30000;
  if (aOptions.mTimeout.WasPassed()) {
    adjustedTimeout = aOptions.mTimeout.Value();
    adjustedTimeout = std::max(15000u, adjustedTimeout);
    adjustedTimeout = std::min(120000u, adjustedTimeout);
  }

  // <https://w3c.github.io/webauthn/#sctn-appid-extension>
  if (aOptions.mExtensions.mAppid.WasPassed()) {
    promise->MaybeReject(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
    return promise.forget();
  }

  // Process each element of mPubKeyCredParams using the following steps, to
  // produce a new sequence of coseAlgos.
  nsTArray<CoseAlg> coseAlgos;
  // If pubKeyCredParams is empty, append ES256 and RS256
  if (aOptions.mPubKeyCredParams.IsEmpty()) {
    coseAlgos.AppendElement(static_cast<long>(CoseAlgorithmIdentifier::ES256));
    coseAlgos.AppendElement(static_cast<long>(CoseAlgorithmIdentifier::RS256));
  } else {
    for (size_t a = 0; a < aOptions.mPubKeyCredParams.Length(); ++a) {
      // If current.type does not contain a PublicKeyCredentialType
      // supported by this implementation, then stop processing current and move
      // on to the next element in mPubKeyCredParams.
      if (!aOptions.mPubKeyCredParams[a].mType.EqualsLiteral(
              MOZ_WEBAUTHN_PUBLIC_KEY_CREDENTIAL_TYPE_PUBLIC_KEY)) {
        continue;
      }

      coseAlgos.AppendElement(aOptions.mPubKeyCredParams[a].mAlg);
    }
  }

  // If there are algorithms specified, but none are Public_key algorithms,
  // reject the promise.
  if (coseAlgos.IsEmpty() && !aOptions.mPubKeyCredParams.IsEmpty()) {
    promise->MaybeReject(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
    return promise.forget();
  }

  // If excludeList is undefined, set it to the empty list.
  //
  // If extensions was specified, process any extensions supported by this
  // client platform, to produce the extension data that needs to be sent to the
  // authenticator. If an error is encountered while processing an extension,
  // skip that extension and do not produce any extension data for it. Call the
  // result of this processing clientExtensions.
  //
  // Currently no extensions are supported

  CryptoBuffer challenge;
  if (!challenge.Assign(aOptions.mChallenge)) {
    promise->MaybeReject(NS_ERROR_DOM_SECURITY_ERR);
    return promise.forget();
  }

  nsTArray<WebAuthnScopedCredential> excludeList;
  for (const auto& s : aOptions.mExcludeCredentials) {
    WebAuthnScopedCredential c;
    CryptoBuffer cb;
    cb.Assign(s.mId);
    c.id() = cb;
    if (s.mTransports.WasPassed()) {
      c.transports() = SerializeTransports(s.mTransports.Value());
    }
    excludeList.AppendElement(c);
  }

  // TODO: Add extension list building
  nsTArray<WebAuthnExtension> extensions;

  // <https://fidoalliance.org/specs/fido-v2.0-ps-20190130/fido-client-to-authenticator-protocol-v2.0-ps-20190130.html#sctn-hmac-secret-extension>
  if (aOptions.mExtensions.mHmacCreateSecret.WasPassed()) {
    bool hmacCreateSecret = aOptions.mExtensions.mHmacCreateSecret.Value();
    if (hmacCreateSecret) {
      extensions.AppendElement(WebAuthnExtensionHmacSecret(hmacCreateSecret));
    }
  }

  if (aOptions.mExtensions.mCredentialProtectionPolicy.WasPassed()) {
    bool enforceCredProtect = false;
    if (aOptions.mExtensions.mEnforceCredentialProtectionPolicy.WasPassed()) {
      enforceCredProtect =
          aOptions.mExtensions.mEnforceCredentialProtectionPolicy.Value();
    }
    extensions.AppendElement(WebAuthnExtensionCredProtect(
        aOptions.mExtensions.mCredentialProtectionPolicy.Value(),
        enforceCredProtect));
  }

  if (aOptions.mExtensions.mCredProps.WasPassed()) {
    bool credProps = aOptions.mExtensions.mCredProps.Value();
    if (credProps) {
      extensions.AppendElement(WebAuthnExtensionCredProps(credProps));
    }
  }

  if (aOptions.mExtensions.mMinPinLength.WasPassed()) {
    bool minPinLength = aOptions.mExtensions.mMinPinLength.Value();
    if (minPinLength) {
      extensions.AppendElement(WebAuthnExtensionMinPinLength(minPinLength));
    }
  }

  // <https://w3c.github.io/webauthn/#sctn-large-blob-extension>
  if (aOptions.mExtensions.mLargeBlob.WasPassed()) {
    if (aOptions.mExtensions.mLargeBlob.Value().mRead.WasPassed() ||
        aOptions.mExtensions.mLargeBlob.Value().mWrite.WasPassed()) {
      promise->MaybeReject(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
      return promise.forget();
    }
    Maybe<bool> supportRequired;
    const Optional<nsString>& largeBlobSupport =
        aOptions.mExtensions.mLargeBlob.Value().mSupport;
    if (largeBlobSupport.WasPassed()) {
      supportRequired.emplace(largeBlobSupport.Value().Equals(u"required"_ns));
    }
    nsTArray<uint8_t> write;  // unused
    extensions.AppendElement(
        WebAuthnExtensionLargeBlob(supportRequired, write));
  }

  // <https://w3c.github.io/webauthn/#prf-extension>
  if (aOptions.mExtensions.mPrf.WasPassed()) {
    const AuthenticationExtensionsPRFInputs& prf =
        aOptions.mExtensions.mPrf.Value();

    Maybe<WebAuthnExtensionPrfValues> eval = Nothing();
    if (prf.mEval.WasPassed()) {
      CryptoBuffer first;
      first.Assign(prf.mEval.Value().mFirst);
      const bool secondMaybe = prf.mEval.Value().mSecond.WasPassed();
      CryptoBuffer second;
      if (secondMaybe) {
        second.Assign(prf.mEval.Value().mSecond.Value());
      }
      eval = Some(WebAuthnExtensionPrfValues(first, secondMaybe, second));
    }

    const bool evalByCredentialMaybe = prf.mEvalByCredential.WasPassed();
    nsTArray<WebAuthnExtensionPrfEvalByCredentialEntry> evalByCredential;
    if (evalByCredentialMaybe) {
      // evalByCredential is only allowed in GetAssertion.
      // https://w3c.github.io/webauthn/#prf-extension
      promise->MaybeReject(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
      return promise.forget();
    }

    extensions.AppendElement(
        WebAuthnExtensionPrf(eval, evalByCredentialMaybe, evalByCredential));
  }

  const auto& selection = aOptions.mAuthenticatorSelection;
  const auto& attachment = selection.mAuthenticatorAttachment;
  const nsString& attestation = aOptions.mAttestation;

  // Attachment
  Maybe<nsString> authenticatorAttachment;
  if (attachment.WasPassed()) {
    authenticatorAttachment.emplace(attachment.Value());
  }

  // The residentKey field was added in WebAuthn level 2. It takes precedent
  // over the requireResidentKey field if and only if it is present and it is a
  // member of the ResidentKeyRequirement enum.
  static_assert(MOZ_WEBAUTHN_ENUM_STRINGS_VERSION == 3);
  bool useResidentKeyValue =
      selection.mResidentKey.WasPassed() &&
      (selection.mResidentKey.Value().EqualsLiteral(
           MOZ_WEBAUTHN_RESIDENT_KEY_REQUIREMENT_REQUIRED) ||
       selection.mResidentKey.Value().EqualsLiteral(
           MOZ_WEBAUTHN_RESIDENT_KEY_REQUIREMENT_PREFERRED) ||
       selection.mResidentKey.Value().EqualsLiteral(
           MOZ_WEBAUTHN_RESIDENT_KEY_REQUIREMENT_DISCOURAGED));

  nsString residentKey;
  if (useResidentKeyValue) {
    residentKey = selection.mResidentKey.Value();
  } else {
    // "If no value is given then the effective value is required if
    // requireResidentKey is true or discouraged if it is false or absent."
    if (selection.mRequireResidentKey) {
      residentKey.AssignLiteral(MOZ_WEBAUTHN_RESIDENT_KEY_REQUIREMENT_REQUIRED);
    } else {
      residentKey.AssignLiteral(
          MOZ_WEBAUTHN_RESIDENT_KEY_REQUIREMENT_DISCOURAGED);
    }
  }

  // Create and forward authenticator selection criteria.
  WebAuthnAuthenticatorSelection authSelection(
      residentKey, selection.mUserVerification, authenticatorAttachment);

  WebAuthnMakeCredentialRpInfo rpInfo(aOptions.mRp.mName);

  WebAuthnMakeCredentialUserInfo userInfo(userId, aOptions.mUser.mName,
                                          aOptions.mUser.mDisplayName);

  // Abort the request if aborted flag is already set.
  if (aSignal.WasPassed() && aSignal.Value().Aborted()) {
    AutoJSAPI jsapi;
    if (!jsapi.Init(global)) {
      promise->MaybeReject(NS_ERROR_DOM_ABORT_ERR);
      return promise.forget();
    }
    JSContext* cx = jsapi.cx();
    JS::Rooted<JS::Value> reason(cx);
    aSignal.Value().GetReason(cx, &reason);
    promise->MaybeReject(reason);
    return promise.forget();
  }

  WebAuthnMakeCredentialInfo info(rpId, challenge, adjustedTimeout, excludeList,
                                  rpInfo, userInfo, coseAlgos, extensions,
                                  authSelection, attestation);

  // Set up the transaction state. Fallible operations should not be performed
  // below this line, as we must not leave the transaction state partially
  // initialized. Once the transaction state is initialized the only valid ways
  // to end the transaction are CancelTransaction, RejectTransaction, and
  // FinishMakeCredential.
  AbortSignal* signal = nullptr;
  if (aSignal.WasPassed()) {
    signal = &aSignal.Value();
    Follow(signal);
  }

  MOZ_ASSERT(mTransaction.isNothing());
  mTransaction =
      Some(WebAuthnTransaction(promise, WebAuthnTransactionType::Create));
  mActor->SendRequestRegister(info)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self = RefPtr{this}](
              const PWebAuthnTransactionChild::RequestRegisterPromise::
                  ResolveOrRejectValue& aValue) {
            self->mTransaction.ref().mRegisterHolder.Complete();
            if (aValue.IsResolve() && aValue.ResolveValue().type() ==
                                          WebAuthnMakeCredentialResponse::Type::
                                              TWebAuthnMakeCredentialResult) {
              self->FinishMakeCredential(aValue.ResolveValue());
            } else if (aValue.IsResolve()) {
              self->RejectTransaction(aValue.ResolveValue());
            } else {
              self->RejectTransaction(NS_ERROR_DOM_NOT_ALLOWED_ERR);
            }
          })
      ->Track(mTransaction.ref().mRegisterHolder);

  return promise.forget();
}

const size_t MAX_ALLOWED_CREDENTIALS = 20;

already_AddRefed<Promise> WebAuthnHandler::GetAssertion(
    const PublicKeyCredentialRequestOptions& aOptions,
    const bool aConditionallyMediated,
    const Optional<OwningNonNull<AbortSignal>>& aSignal, ErrorResult& aError) {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(mWindow);

  RefPtr<Promise> promise = Promise::Create(global, aError);
  if (aError.Failed()) {
    return nullptr;
  }

  if (mTransaction.isSome()) {
    // abort the old transaction and take over control from here.
    CancelTransaction(NS_ERROR_DOM_ABORT_ERR);
  }

  if (!MaybeCreateActor()) {
    promise->MaybeReject(NS_ERROR_DOM_OPERATION_ERR);
    return promise.forget();
  }

  nsCOMPtr<Document> doc = mWindow->GetDoc();
  if (!IsWebAuthnAllowedInDocument(doc)) {
    promise->MaybeReject(NS_ERROR_DOM_SECURITY_ERR);
    return promise.forget();
  }

  nsCOMPtr<nsIPrincipal> principal = doc->NodePrincipal();
  if (!IsWebAuthnAllowedForPrincipal(principal)) {
    promise->MaybeReject(NS_ERROR_DOM_SECURITY_ERR);
    return promise.forget();
  }

  nsCString rpId;
  if (aOptions.mRpId.WasPassed()) {
    rpId = NS_ConvertUTF16toUTF8(aOptions.mRpId.Value());
  } else {
    nsresult rv = DefaultRpId(principal, rpId);
    if (NS_FAILED(rv)) {
      promise->MaybeReject(NS_ERROR_FAILURE);
      return promise.forget();
    }
  }
  if (!IsValidRpId(principal, rpId)) {
    promise->MaybeReject(NS_ERROR_DOM_SECURITY_ERR);
    return promise.forget();
  }

  // If timeoutSeconds was specified, check if its value lies within a
  // reasonable range as defined by the platform and if not, correct it to the
  // closest value lying within that range.

  uint32_t adjustedTimeout = 30000;
  if (aOptions.mTimeout.WasPassed()) {
    adjustedTimeout = aOptions.mTimeout.Value();
    adjustedTimeout = std::max(15000u, adjustedTimeout);
    adjustedTimeout = std::min(120000u, adjustedTimeout);
  }

  // Abort the request if the allowCredentials set is too large
  if (aOptions.mAllowCredentials.Length() > MAX_ALLOWED_CREDENTIALS) {
    promise->MaybeReject(NS_ERROR_DOM_SECURITY_ERR);
    return promise.forget();
  }

  CryptoBuffer challenge;
  if (!challenge.Assign(aOptions.mChallenge)) {
    promise->MaybeReject(NS_ERROR_DOM_SECURITY_ERR);
    return promise.forget();
  }

  nsTArray<WebAuthnScopedCredential> allowList;
  for (const auto& s : aOptions.mAllowCredentials) {
    if (s.mType.EqualsLiteral(
            MOZ_WEBAUTHN_PUBLIC_KEY_CREDENTIAL_TYPE_PUBLIC_KEY)) {
      WebAuthnScopedCredential c;
      CryptoBuffer cb;
      cb.Assign(s.mId);
      c.id() = cb;
      if (s.mTransports.WasPassed()) {
        c.transports() = SerializeTransports(s.mTransports.Value());
      }
      allowList.AppendElement(c);
    }
  }
  if (allowList.Length() == 0 && aOptions.mAllowCredentials.Length() != 0) {
    promise->MaybeReject(NS_ERROR_DOM_NOT_ALLOWED_ERR);
    return promise.forget();
  }

  // If extensions were specified, process any extensions supported by this
  // client platform, to produce the extension data that needs to be sent to the
  // authenticator. If an error is encountered while processing an extension,
  // skip that extension and do not produce any extension data for it. Call the
  // result of this processing clientExtensions.
  nsTArray<WebAuthnExtension> extensions;

  // credProps is only supported in MakeCredentials
  if (aOptions.mExtensions.mCredProps.WasPassed()) {
    promise->MaybeReject(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
    return promise.forget();
  }

  // minPinLength is only supported in MakeCredentials
  if (aOptions.mExtensions.mMinPinLength.WasPassed()) {
    promise->MaybeReject(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
    return promise.forget();
  }

  // <https://w3c.github.io/webauthn/#sctn-appid-extension>
  Maybe<nsCString> maybeAppId;
  if (aOptions.mExtensions.mAppid.WasPassed()) {
    nsCString appId(NS_ConvertUTF16toUTF8(aOptions.mExtensions.mAppid.Value()));

    // Step 2 of Algorithm 3.1.2 of
    // https://fidoalliance.org/specs/fido-v2.0-id-20180227/fido-appid-and-facets-v2.0-id-20180227.html#determining-if-a-caller-s-facetid-is-authorized-for-an-appid
    if (appId.IsEmpty() || appId.EqualsLiteral("null")) {
      auto* basePrin = BasePrincipal::Cast(principal);
      nsresult rv = basePrin->GetWebExposedOriginSerialization(appId);
      if (NS_FAILED(rv)) {
        promise->MaybeReject(NS_ERROR_DOM_SECURITY_ERR);
        return promise.forget();
      }
    }

    maybeAppId.emplace(std::move(appId));
  }

  // <https://w3c.github.io/webauthn/#sctn-large-blob-extension>
  if (aOptions.mExtensions.mLargeBlob.WasPassed()) {
    const AuthenticationExtensionsLargeBlobInputs& extLargeBlob =
        aOptions.mExtensions.mLargeBlob.Value();
    if (extLargeBlob.mSupport.WasPassed() ||
        (extLargeBlob.mRead.WasPassed() && extLargeBlob.mWrite.WasPassed()) ||
        (extLargeBlob.mWrite.WasPassed() &&
         aOptions.mAllowCredentials.Length() != 1)) {
      promise->MaybeReject(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
      return promise.forget();
    }
    Maybe<bool> read = Nothing();
    if (extLargeBlob.mRead.WasPassed() && extLargeBlob.mRead.Value()) {
      read.emplace(true);
    }

    CryptoBuffer write;
    if (extLargeBlob.mWrite.WasPassed()) {
      read.emplace(false);
      write.Assign(extLargeBlob.mWrite.Value());
    }
    extensions.AppendElement(WebAuthnExtensionLargeBlob(read, write));
  }

  // <https://w3c.github.io/webauthn/#prf-extension>
  if (aOptions.mExtensions.mPrf.WasPassed()) {
    const AuthenticationExtensionsPRFInputs& prf =
        aOptions.mExtensions.mPrf.Value();

    Maybe<WebAuthnExtensionPrfValues> eval = Nothing();
    if (prf.mEval.WasPassed()) {
      CryptoBuffer first;
      first.Assign(prf.mEval.Value().mFirst);
      const bool secondMaybe = prf.mEval.Value().mSecond.WasPassed();
      CryptoBuffer second;
      if (secondMaybe) {
        second.Assign(prf.mEval.Value().mSecond.Value());
      }
      eval = Some(WebAuthnExtensionPrfValues(first, secondMaybe, second));
    }

    const bool evalByCredentialMaybe = prf.mEvalByCredential.WasPassed();
    nsTArray<WebAuthnExtensionPrfEvalByCredentialEntry> evalByCredential;
    if (evalByCredentialMaybe) {
      if (allowList.Length() == 0) {
        promise->MaybeReject(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
        return promise.forget();
      }

      for (const auto& entry : prf.mEvalByCredential.Value().Entries()) {
        FallibleTArray<uint8_t> evalByCredentialEntryId;
        nsresult rv = Base64URLDecode(NS_ConvertUTF16toUTF8(entry.mKey),
                                      Base64URLDecodePaddingPolicy::Ignore,
                                      evalByCredentialEntryId);
        if (NS_FAILED(rv)) {
          promise->MaybeReject(NS_ERROR_DOM_SYNTAX_ERR);
          return promise.forget();
        }

        bool foundMatchingAllowListEntry = false;
        for (const auto& cred : allowList) {
          if (evalByCredentialEntryId == cred.id()) {
            foundMatchingAllowListEntry = true;
          }
        }
        if (!foundMatchingAllowListEntry) {
          promise->MaybeReject(NS_ERROR_DOM_SYNTAX_ERR);
          return promise.forget();
        }

        CryptoBuffer first;
        first.Assign(entry.mValue.mFirst);
        const bool secondMaybe = entry.mValue.mSecond.WasPassed();
        CryptoBuffer second;
        if (secondMaybe) {
          second.Assign(entry.mValue.mSecond.Value());
        }
        evalByCredential.AppendElement(
            WebAuthnExtensionPrfEvalByCredentialEntry(
                evalByCredentialEntryId,
                WebAuthnExtensionPrfValues(first, secondMaybe, second)));
      }
    }

    extensions.AppendElement(
        WebAuthnExtensionPrf(eval, evalByCredentialMaybe, evalByCredential));
  }

  // Abort the request if aborted flag is already set.
  if (aSignal.WasPassed() && aSignal.Value().Aborted()) {
    AutoJSAPI jsapi;
    if (!jsapi.Init(global)) {
      promise->MaybeReject(NS_ERROR_DOM_ABORT_ERR);
      return promise.forget();
    }
    JSContext* cx = jsapi.cx();
    JS::Rooted<JS::Value> reason(cx);
    aSignal.Value().GetReason(cx, &reason);
    promise->MaybeReject(reason);
    return promise.forget();
  }

  WebAuthnGetAssertionInfo info(
      rpId, maybeAppId, challenge, adjustedTimeout, allowList, extensions,
      aOptions.mUserVerification, aConditionallyMediated);

  // Set up the transaction state. Fallible operations should not be performed
  // below this line, as we must not leave the transaction state partially
  // initialized. Once the transaction state is initialized the only valid ways
  // to end the transaction are CancelTransaction, RejectTransaction, and
  // FinishGetAssertion.
  AbortSignal* signal = nullptr;
  if (aSignal.WasPassed()) {
    signal = &aSignal.Value();
    Follow(signal);
  }

  MOZ_ASSERT(mTransaction.isNothing());
  mTransaction =
      Some(WebAuthnTransaction(promise, WebAuthnTransactionType::Get));
  mActor->SendRequestSign(info)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self = RefPtr{this}](
              const PWebAuthnTransactionChild::RequestSignPromise::
                  ResolveOrRejectValue& aValue) {
            self->mTransaction.ref().mSignHolder.Complete();
            if (aValue.IsResolve() && aValue.ResolveValue().type() ==
                                          WebAuthnGetAssertionResponse::Type::
                                              TWebAuthnGetAssertionResult) {
              self->FinishGetAssertion(aValue.ResolveValue());
            } else if (aValue.IsResolve()) {
              self->RejectTransaction(aValue.ResolveValue());
            } else {
              self->RejectTransaction(NS_ERROR_DOM_NOT_ALLOWED_ERR);
            }
          })
      ->Track(mTransaction.ref().mSignHolder);

  return promise.forget();
}

already_AddRefed<Promise> WebAuthnHandler::Store(const Credential& aCredential,
                                                 ErrorResult& aError) {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(mWindow);

  RefPtr<Promise> promise = Promise::Create(global, aError);
  if (aError.Failed()) {
    return nullptr;
  }

  if (mTransaction.isSome()) {
    // abort the old transaction and take over control from here.
    CancelTransaction(NS_ERROR_DOM_ABORT_ERR);
  }

  promise->MaybeReject(NS_ERROR_DOM_NOT_SUPPORTED_ERR);
  return promise.forget();
}

already_AddRefed<Promise> WebAuthnHandler::IsUVPAA(GlobalObject& aGlobal,
                                                   ErrorResult& aError) {
  RefPtr<Promise> promise =
      Promise::Create(xpc::CurrentNativeGlobal(aGlobal.Context()), aError);
  if (aError.Failed()) {
    return nullptr;
  }

  if (!MaybeCreateActor()) {
    promise->MaybeReject(NS_ERROR_DOM_OPERATION_ERR);
    return promise.forget();
  }

  mActor->SendRequestIsUVPAA()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [promise](const PWebAuthnTransactionChild::RequestIsUVPAAPromise::
                    ResolveOrRejectValue& aValue) {
        if (aValue.IsResolve()) {
          promise->MaybeResolve(aValue.ResolveValue());
        } else {
          promise->MaybeReject(NS_ERROR_DOM_NOT_ALLOWED_ERR);
        }
      });
  return promise.forget();
}

void WebAuthnHandler::FinishMakeCredential(
    const WebAuthnMakeCredentialResult& aResult) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mTransaction.isSome());

  nsAutoCString keyHandleBase64Url;
  nsresult rv = Base64URLEncode(
      aResult.KeyHandle().Length(), aResult.KeyHandle().Elements(),
      Base64URLEncodePaddingPolicy::Omit, keyHandleBase64Url);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    RejectTransaction(rv);
    return;
  }

  // Create a new PublicKeyCredential object and populate its fields with the
  // values returned from the authenticator as well as the clientDataJSON
  // computed earlier.
  RefPtr<AuthenticatorAttestationResponse> attestation =
      new AuthenticatorAttestationResponse(mWindow);
  attestation->SetClientDataJSON(aResult.ClientDataJSON());
  attestation->SetAttestationObject(aResult.AttestationObject());
  attestation->SetTransports(aResult.Transports());

  RefPtr<PublicKeyCredential> credential = new PublicKeyCredential(mWindow);
  credential->SetId(NS_ConvertASCIItoUTF16(keyHandleBase64Url));
  credential->SetType(u"public-key"_ns);
  credential->SetRawId(aResult.KeyHandle());
  credential->SetAttestationResponse(attestation);

  if (aResult.AuthenticatorAttachment().isSome()) {
    credential->SetAuthenticatorAttachment(aResult.AuthenticatorAttachment());

    mozilla::glean::webauthn_create::authenticator_attachment
        .Get(NS_ConvertUTF16toUTF8(aResult.AuthenticatorAttachment().ref()))
        .Add(1);
  } else {
    mozilla::glean::webauthn_get::authenticator_attachment.Get("unknown"_ns)
        .Add(1);
  }

  // Forward client extension results.
  for (const auto& ext : aResult.Extensions()) {
    if (ext.type() ==
        WebAuthnExtensionResult::TWebAuthnExtensionResultCredProps) {
      bool credPropsRk = ext.get_WebAuthnExtensionResultCredProps().rk();
      credential->SetClientExtensionResultCredPropsRk(credPropsRk);
      if (credPropsRk) {
        mozilla::glean::webauthn_create::passkey.Add(1);
      }
    }
    if (ext.type() ==
        WebAuthnExtensionResult::TWebAuthnExtensionResultHmacSecret) {
      bool hmacCreateSecret =
          ext.get_WebAuthnExtensionResultHmacSecret().hmacCreateSecret();
      credential->SetClientExtensionResultHmacSecret(hmacCreateSecret);
    }
    if (ext.type() ==
        WebAuthnExtensionResult::TWebAuthnExtensionResultLargeBlob) {
      credential->InitClientExtensionResultLargeBlob();
      credential->SetClientExtensionResultLargeBlobSupported(
          ext.get_WebAuthnExtensionResultLargeBlob().flag());
    }
    if (ext.type() == WebAuthnExtensionResult::TWebAuthnExtensionResultPrf) {
      credential->InitClientExtensionResultPrf();
      const Maybe<bool> prfEnabled =
          ext.get_WebAuthnExtensionResultPrf().enabled();
      if (prfEnabled.isSome()) {
        credential->SetClientExtensionResultPrfEnabled(prfEnabled.value());
      }
      const Maybe<WebAuthnExtensionPrfValues> prfValues =
          ext.get_WebAuthnExtensionResultPrf().results();
      if (prfValues.isSome()) {
        credential->SetClientExtensionResultPrfResultsFirst(
            prfValues.value().first());
        if (prfValues.value().secondMaybe()) {
          credential->SetClientExtensionResultPrfResultsSecond(
              prfValues.value().second());
        }
      }
    }
  }

  ResolveTransaction(credential);
}

void WebAuthnHandler::FinishGetAssertion(
    const WebAuthnGetAssertionResult& aResult) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mTransaction.isSome());

  nsAutoCString keyHandleBase64Url;
  nsresult rv = Base64URLEncode(
      aResult.KeyHandle().Length(), aResult.KeyHandle().Elements(),
      Base64URLEncodePaddingPolicy::Omit, keyHandleBase64Url);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    RejectTransaction(rv);
    return;
  }

  // Create a new PublicKeyCredential object named value and populate its fields
  // with the values returned from the authenticator as well as the
  // clientDataJSON computed earlier.
  RefPtr<AuthenticatorAssertionResponse> assertion =
      new AuthenticatorAssertionResponse(mWindow);
  assertion->SetClientDataJSON(aResult.ClientDataJSON());
  assertion->SetAuthenticatorData(aResult.AuthenticatorData());
  assertion->SetSignature(aResult.Signature());
  assertion->SetUserHandle(aResult.UserHandle());  // may be empty

  RefPtr<PublicKeyCredential> credential = new PublicKeyCredential(mWindow);
  credential->SetId(NS_ConvertASCIItoUTF16(keyHandleBase64Url));
  credential->SetType(u"public-key"_ns);
  credential->SetRawId(aResult.KeyHandle());
  credential->SetAssertionResponse(assertion);

  if (aResult.AuthenticatorAttachment().isSome()) {
    credential->SetAuthenticatorAttachment(aResult.AuthenticatorAttachment());

    mozilla::glean::webauthn_get::authenticator_attachment
        .Get(NS_ConvertUTF16toUTF8(aResult.AuthenticatorAttachment().ref()))
        .Add(1);
  } else {
    mozilla::glean::webauthn_get::authenticator_attachment.Get("unknown"_ns)
        .Add(1);
  }

  // Forward client extension results.
  for (const auto& ext : aResult.Extensions()) {
    if (ext.type() == WebAuthnExtensionResult::TWebAuthnExtensionResultAppId) {
      bool appid = ext.get_WebAuthnExtensionResultAppId().AppId();
      credential->SetClientExtensionResultAppId(appid);
    }
    if (ext.type() ==
        WebAuthnExtensionResult::TWebAuthnExtensionResultLargeBlob) {
      if (ext.get_WebAuthnExtensionResultLargeBlob().flag() &&
          ext.get_WebAuthnExtensionResultLargeBlob().written()) {
        // Signal a read failure by including an empty largeBlob extension.
        credential->InitClientExtensionResultLargeBlob();
      } else if (ext.get_WebAuthnExtensionResultLargeBlob().flag()) {
        const nsTArray<uint8_t>& largeBlobValue =
            ext.get_WebAuthnExtensionResultLargeBlob().blob();
        credential->InitClientExtensionResultLargeBlob();
        credential->SetClientExtensionResultLargeBlobValue(largeBlobValue);
      } else {
        bool largeBlobWritten =
            ext.get_WebAuthnExtensionResultLargeBlob().written();
        credential->InitClientExtensionResultLargeBlob();
        credential->SetClientExtensionResultLargeBlobWritten(largeBlobWritten);
      }
    }
    if (ext.type() == WebAuthnExtensionResult::TWebAuthnExtensionResultPrf) {
      credential->InitClientExtensionResultPrf();
      Maybe<WebAuthnExtensionPrfValues> prfResults =
          ext.get_WebAuthnExtensionResultPrf().results();
      if (prfResults.isSome()) {
        credential->SetClientExtensionResultPrfResultsFirst(
            prfResults.value().first());
        if (prfResults.value().secondMaybe()) {
          credential->SetClientExtensionResultPrfResultsSecond(
              prfResults.value().second());
        }
      }
    }
  }

  // Treat successful assertion as user activation for BounceTrackingProtection.
  nsIGlobalObject* global = mTransaction.ref().mPromise->GetGlobalObject();
  if (global) {
    nsPIDOMWindowInner* window = global->GetAsInnerWindow();
    if (window) {
      Unused << BounceTrackingProtection::RecordUserActivation(
          window->GetWindowContext());
    }
  }

  ResolveTransaction(credential);
}

void WebAuthnHandler::RunAbortAlgorithm() {
  if (NS_WARN_IF(mTransaction.isNothing())) {
    return;
  }

  nsCOMPtr<nsIGlobalObject> global = do_QueryInterface(mWindow);

  AutoJSAPI jsapi;
  if (!jsapi.Init(global)) {
    CancelTransaction(NS_ERROR_DOM_ABORT_ERR);
    return;
  }
  JSContext* cx = jsapi.cx();
  JS::Rooted<JS::Value> reason(cx);
  Signal()->GetReason(cx, &reason);
  CancelTransaction(reason);
}

void WebAuthnHandler::ResolveTransaction(
    const RefPtr<PublicKeyCredential>& aCredential) {
  MOZ_ASSERT(mTransaction.isSome());

  switch (mTransaction.ref().mType) {
    case WebAuthnTransactionType::Create:
      mozilla::glean::webauthn_create::success.Add(1);
      break;
    case WebAuthnTransactionType::Get:
      mozilla::glean::webauthn_get::success.Add(1);
      break;
  }

  mTransaction.ref().mPromise->MaybeResolve(aCredential);
  mTransaction.reset();
  Unfollow();
}

template <typename T>
void WebAuthnHandler::RejectTransaction(const T& aReason) {
  MOZ_ASSERT(mTransaction.isSome());

  switch (mTransaction.ref().mType) {
    case WebAuthnTransactionType::Create:
      mozilla::glean::webauthn_create::failure.Add(1);
      break;
    case WebAuthnTransactionType::Get:
      mozilla::glean::webauthn_get::failure.Add(1);
      break;
  }

  mTransaction.ref().mPromise->MaybeReject(aReason);
  mTransaction.reset();
  Unfollow();
}

}  // namespace mozilla::dom
