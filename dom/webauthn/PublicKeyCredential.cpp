/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Base64.h"
#include "mozilla/HoldDropJSObjects.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_security.h"
#include "mozilla/dom/AuthenticatorResponse.h"
#include "mozilla/dom/CredentialsContainer.h"
#include "mozilla/dom/ChromeUtils.h"
#include "mozilla/dom/Navigator.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/PublicKeyCredential.h"
#include "mozilla/dom/WebAuthenticationBinding.h"
#include "mozilla/dom/WebAuthnHandler.h"
#include "nsCycleCollectionParticipant.h"

#ifdef MOZ_WIDGET_ANDROID
#  include "mozilla/MozPromise.h"
#  include "mozilla/java/GeckoResultNatives.h"
#  include "mozilla/java/WebAuthnTokenManagerWrappers.h"
#endif

namespace mozilla::dom {

NS_IMPL_CYCLE_COLLECTION_CLASS(PublicKeyCredential)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(PublicKeyCredential, Credential)
  tmp->mRawIdCachedObj = nullptr;
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

NS_IMPL_CYCLE_COLLECTION_TRACE_BEGIN_INHERITED(PublicKeyCredential, Credential)
  NS_IMPL_CYCLE_COLLECTION_TRACE_PRESERVED_WRAPPER
  NS_IMPL_CYCLE_COLLECTION_TRACE_JS_MEMBER_CALLBACK(mRawIdCachedObj)
NS_IMPL_CYCLE_COLLECTION_TRACE_END

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(PublicKeyCredential,
                                                  Credential)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mAttestationResponse)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE(mAssertionResponse)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_ADDREF_INHERITED(PublicKeyCredential, Credential)
NS_IMPL_RELEASE_INHERITED(PublicKeyCredential, Credential)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(PublicKeyCredential)
NS_INTERFACE_MAP_END_INHERITING(Credential)

PublicKeyCredential::PublicKeyCredential(nsPIDOMWindowInner* aParent)
    : Credential(aParent), mRawIdCachedObj(nullptr) {
  mozilla::HoldJSObjects(this);
}

PublicKeyCredential::~PublicKeyCredential() { mozilla::DropJSObjects(this); }

JSObject* PublicKeyCredential::WrapObject(JSContext* aCx,
                                          JS::Handle<JSObject*> aGivenProto) {
  return PublicKeyCredential_Binding::Wrap(aCx, this, aGivenProto);
}

void PublicKeyCredential::GetRawId(JSContext* aCx,
                                   JS::MutableHandle<JSObject*> aValue,
                                   ErrorResult& aRv) {
  if (!mRawIdCachedObj) {
    mRawIdCachedObj = ArrayBuffer::Create(aCx, mRawId, aRv);
    if (aRv.Failed()) {
      return;
    }
  }
  aValue.set(mRawIdCachedObj);
}

void PublicKeyCredential::GetAuthenticatorAttachment(
    DOMString& aAuthenticatorAttachment) {
  if (mAuthenticatorAttachment.isSome()) {
    aAuthenticatorAttachment.SetKnownLiveString(mAuthenticatorAttachment.ref());
  } else {
    aAuthenticatorAttachment.SetNull();
  }
}

already_AddRefed<AuthenticatorResponse> PublicKeyCredential::Response() const {
  if (mAttestationResponse) {
    return do_AddRef(mAttestationResponse);
  }
  if (mAssertionResponse) {
    return do_AddRef(mAssertionResponse);
  }
  return nullptr;
}

void PublicKeyCredential::SetRawId(const nsTArray<uint8_t>& aBuffer) {
  mRawId.Assign(aBuffer);
}

void PublicKeyCredential::SetAuthenticatorAttachment(
    const Maybe<nsString>& aAuthenticatorAttachment) {
  mAuthenticatorAttachment = aAuthenticatorAttachment;
}

void PublicKeyCredential::SetAttestationResponse(
    const RefPtr<AuthenticatorAttestationResponse>& aAttestationResponse) {
  mAttestationResponse = aAttestationResponse;
}

void PublicKeyCredential::SetAssertionResponse(
    const RefPtr<AuthenticatorAssertionResponse>& aAssertionResponse) {
  mAssertionResponse = aAssertionResponse;
}

/* static */
already_AddRefed<Promise>
PublicKeyCredential::IsUserVerifyingPlatformAuthenticatorAvailable(
    GlobalObject& aGlobal, ErrorResult& aError) {
  nsCOMPtr<nsPIDOMWindowInner> window =
      do_QueryInterface(aGlobal.GetAsSupports());
  if (!window) {
    aError.Throw(NS_ERROR_FAILURE);
    return nullptr;
  }

  RefPtr<WebAuthnHandler> handler =
      window->Navigator()->Credentials()->GetWebAuthnHandler();
  return handler->IsUVPAA(aGlobal, aError);
}

/* static */
already_AddRefed<Promise> PublicKeyCredential::GetClientCapabilities(
    GlobalObject& aGlobal, ErrorResult& aError) {
  RefPtr<Promise> promise =
      Promise::Create(xpc::CurrentNativeGlobal(aGlobal.Context()), aError);
  if (aError.Failed()) {
    return nullptr;
  }

  // From https://w3c.github.io/webauthn/#sctn-getClientCapabilities:
  //    Keys in PublicKeyCredentialClientCapabilities MUST be sorted in
  //    ascending lexicographical order. The set of keys SHOULD contain the set
  //    of enumeration values of ClientCapability
  //    (https://w3c.github.io/webauthn/#enumdef-clientcapability) but the
  //    client MAY omit keys as it deems necessary. [...] The set of keys SHOULD
  //    also contain a key for each extension implemented by the client, where
  //    the key is formed by prefixing the string 'extension:' to the extension
  //    identifier. The associated value for each implemented extension SHOULD
  //    be true.
  //
  Record<nsString, bool> capabilities;

  auto entry = capabilities.Entries().AppendElement();
  entry->mKey = u"conditionalCreate"_ns;
  entry->mValue = false;

  entry = capabilities.Entries().AppendElement();
  entry->mKey = u"conditionalGet"_ns;
#if defined(MOZ_WIDGET_ANDROID)
  entry->mValue = false;
#else
  entry->mValue = StaticPrefs::security_webauthn_enable_conditional_mediation();
#endif

  entry = capabilities.Entries().AppendElement();
  entry->mKey = u"extension:appid"_ns;
  entry->mValue = true;

  // Bug 1570429: support the appidExclude extension.
  // entry = capabilities.Entries().AppendElement();
  // entry->mKey = u"extension:appidExclude"_ns;
  // entry->mValue = true;

  // Bug 1844448: support the credBlob extension.
  // entry = capabilities.Entries().AppendElement();
  // entry->mKey = u"extension:credBlob"_ns;
  // entry->mValue = true;

  entry = capabilities.Entries().AppendElement();
  entry->mKey = u"extension:credProps"_ns;
  entry->mValue = true;

  entry = capabilities.Entries().AppendElement();
  entry->mKey = u"extension:credentialProtectionPolicy"_ns;
#if defined(XP_MACOSX) || defined(MOZ_WIDGET_ANDROID)
  entry->mValue = false;
#else
  entry->mValue = true;
#endif

  entry = capabilities.Entries().AppendElement();
  entry->mKey = u"extension:enforceCredentialProtectionPolicy"_ns;
#if defined(XP_MACOSX) || defined(MOZ_WIDGET_ANDROID)
  entry->mValue = false;
#else
  entry->mValue = true;
#endif

  // Bug 1844448: support the credBlob extension.
  // entry = capabilities.Entries().AppendElement();
  // entry->mKey = u"extension:getCredBlob"_ns;
  // entry->mValue = true;

  entry = capabilities.Entries().AppendElement();
  entry->mKey = u"extension:hmacCreateSecret"_ns;
  entry->mValue = true;

  entry = capabilities.Entries().AppendElement();
  entry->mKey = u"extension:largeBlob"_ns;
#if defined(XP_MACOSX) || defined(XP_WIN)
  entry->mValue = true;
#else
  entry->mValue = false;
#endif

  entry = capabilities.Entries().AppendElement();
  entry->mKey = u"extension:minPinLength"_ns;
  entry->mValue = true;

  entry = capabilities.Entries().AppendElement();
  entry->mKey = u"extension:prf"_ns;
  entry->mValue = true;

  entry = capabilities.Entries().AppendElement();
  entry->mKey = u"hybridTransport"_ns;
#if defined(XP_MACOSX) || defined(XP_WIN) || defined(MOZ_WIDGET_ANDROID)
  entry->mValue = true;
#else
  entry->mValue = false;
#endif

  entry = capabilities.Entries().AppendElement();
  entry->mKey = u"passkeyPlatformAuthenticator"_ns;
#if defined(XP_MACOSX) || defined(XP_WIN) || defined(MOZ_WIDGET_ANDROID)
  entry->mValue = true;
#else
  entry->mValue = false;
#endif

  entry = capabilities.Entries().AppendElement();
  entry->mKey = u"relatedOrigins"_ns;
  entry->mValue = false;

  entry = capabilities.Entries().AppendElement();
  entry->mKey = u"signalAllAcceptedCredentials"_ns;
  entry->mValue = false;

  entry = capabilities.Entries().AppendElement();
  entry->mKey = u"signalCurrentUserDetails"_ns;
  entry->mValue = false;

  entry = capabilities.Entries().AppendElement();
  entry->mKey = u"signalUnknownCredential"_ns;
  entry->mValue = false;

  entry = capabilities.Entries().AppendElement();
  entry->mKey = u"userVerifyingPlatformAuthenticator"_ns;
#if defined(XP_MACOSX) || defined(XP_WIN) || defined(MOZ_WIDGET_ANDROID)
  entry->mValue = true;
#else
  entry->mValue = false;
#endif

  promise->MaybeResolve(capabilities);
  return promise.forget();
}

/* static */
already_AddRefed<Promise> PublicKeyCredential::IsConditionalMediationAvailable(
    GlobalObject& aGlobal, ErrorResult& aError) {
  RefPtr<Promise> promise =
      Promise::Create(xpc::CurrentNativeGlobal(aGlobal.Context()), aError);
  if (aError.Failed()) {
    return nullptr;
  }
#if defined(MOZ_WIDGET_ANDROID)
  promise->MaybeResolve(false);
#else
  promise->MaybeResolve(
      StaticPrefs::security_webauthn_enable_conditional_mediation());
#endif
  return promise.forget();
}

void PublicKeyCredential::GetClientExtensionResults(
    JSContext* cx, AuthenticationExtensionsClientOutputs& aResult) const {
  if (mClientExtensionOutputs.mAppid.WasPassed()) {
    aResult.mAppid.Construct(mClientExtensionOutputs.mAppid.Value());
  }

  if (mClientExtensionOutputs.mCredProps.WasPassed()) {
    aResult.mCredProps.Construct(mClientExtensionOutputs.mCredProps.Value());
  }

  if (mClientExtensionOutputs.mHmacCreateSecret.WasPassed()) {
    aResult.mHmacCreateSecret.Construct(
        mClientExtensionOutputs.mHmacCreateSecret.Value());
  }

  if (mClientExtensionOutputs.mLargeBlob.WasPassed()) {
    const AuthenticationExtensionsLargeBlobOutputs& src =
        mClientExtensionOutputs.mLargeBlob.Value();
    AuthenticationExtensionsLargeBlobOutputs& dest =
        aResult.mLargeBlob.Construct();

    if (src.mSupported.WasPassed()) {
      dest.mSupported.Construct(src.mSupported.Value());
    }

    if (src.mWritten.WasPassed()) {
      dest.mWritten.Construct(src.mWritten.Value());
    }

    if (mLargeBlobValue.isSome()) {
      dest.mBlob.Construct().Init(
          TypedArrayCreator<ArrayBuffer>(mLargeBlobValue.ref()).Create(cx));
    }
  }

  if (mClientExtensionOutputs.mPrf.WasPassed()) {
    AuthenticationExtensionsPRFOutputs& dest = aResult.mPrf.Construct();

    if (mClientExtensionOutputs.mPrf.Value().mEnabled.WasPassed()) {
      dest.mEnabled.Construct(
          mClientExtensionOutputs.mPrf.Value().mEnabled.Value());
    }

    if (mPrfResultsFirst.isSome()) {
      AuthenticationExtensionsPRFValues& destResults =
          dest.mResults.Construct();

      destResults.mFirst.SetAsArrayBuffer().Init(
          TypedArrayCreator<ArrayBuffer>(mPrfResultsFirst.ref()).Create(cx));

      if (mPrfResultsSecond.isSome()) {
        destResults.mSecond.Construct().SetAsArrayBuffer().Init(
            TypedArrayCreator<ArrayBuffer>(mPrfResultsSecond.ref()).Create(cx));
      }
    }
  }
}

void PublicKeyCredential::ToJSON(JSContext* aCx,
                                 JS::MutableHandle<JSObject*> aRetval,
                                 ErrorResult& aError) {
  JS::Rooted<JS::Value> value(aCx);
  if (mAttestationResponse) {
    RegistrationResponseJSON json;
    GetId(json.mId);
    GetId(json.mRawId);
    mAttestationResponse->ToJSON(json.mResponse, aError);
    if (aError.Failed()) {
      return;
    }
    if (mAuthenticatorAttachment.isSome()) {
      json.mAuthenticatorAttachment.Construct();
      json.mAuthenticatorAttachment.Value() = mAuthenticatorAttachment.ref();
    }
    if (mClientExtensionOutputs.mCredProps.WasPassed()) {
      json.mClientExtensionResults.mCredProps.Construct(
          mClientExtensionOutputs.mCredProps.Value());
    }
    if (mClientExtensionOutputs.mHmacCreateSecret.WasPassed()) {
      json.mClientExtensionResults.mHmacCreateSecret.Construct(
          mClientExtensionOutputs.mHmacCreateSecret.Value());
    }
    if (mClientExtensionOutputs.mPrf.WasPassed()) {
      json.mClientExtensionResults.mPrf.Construct();
      if (mClientExtensionOutputs.mPrf.Value().mEnabled.WasPassed()) {
        json.mClientExtensionResults.mPrf.Value().mEnabled.Construct(
            mClientExtensionOutputs.mPrf.Value().mEnabled.Value());
      }
    }
    if (mClientExtensionOutputs.mLargeBlob.WasPassed()) {
      const AuthenticationExtensionsLargeBlobOutputs& src =
          mClientExtensionOutputs.mLargeBlob.Value();
      AuthenticationExtensionsLargeBlobOutputsJSON& dest =
          json.mClientExtensionResults.mLargeBlob.Construct();
      if (src.mSupported.WasPassed()) {
        dest.mSupported.Construct(src.mSupported.Value());
      }
    }
    json.mType.Assign(u"public-key"_ns);
    if (!ToJSValue(aCx, json, &value)) {
      aError.StealExceptionFromJSContext(aCx);
      return;
    }
  } else if (mAssertionResponse) {
    AuthenticationResponseJSON json;
    GetId(json.mId);
    GetId(json.mRawId);
    mAssertionResponse->ToJSON(json.mResponse, aError);
    if (aError.Failed()) {
      return;
    }
    if (mAuthenticatorAttachment.isSome()) {
      json.mAuthenticatorAttachment.Construct();
      json.mAuthenticatorAttachment.Value() = mAuthenticatorAttachment.ref();
    }
    if (mClientExtensionOutputs.mAppid.WasPassed()) {
      json.mClientExtensionResults.mAppid.Construct(
          mClientExtensionOutputs.mAppid.Value());
    }
    if (mClientExtensionOutputs.mPrf.WasPassed()) {
      json.mClientExtensionResults.mPrf.Construct();
    }
    if (mClientExtensionOutputs.mPrf.WasPassed() && mPrfResultsFirst.isSome()) {
      AuthenticationExtensionsPRFValuesJSON& dest =
          json.mClientExtensionResults.mPrf.Value().mResults.Construct();
      nsCString prfFirst;
      nsresult rv = mozilla::Base64URLEncode(
          mPrfResultsFirst->Length(), mPrfResultsFirst->Elements(),
          Base64URLEncodePaddingPolicy::Omit, prfFirst);
      if (NS_FAILED(rv)) {
        aError.ThrowEncodingError(
            "could not encode first prf output as urlsafe base64");
        return;
      }
      dest.mFirst.Assign(NS_ConvertUTF8toUTF16(prfFirst));
      if (mPrfResultsSecond.isSome()) {
        nsCString prfSecond;
        nsresult rv = mozilla::Base64URLEncode(
            mPrfResultsSecond->Length(), mPrfResultsSecond->Elements(),
            Base64URLEncodePaddingPolicy::Omit, prfSecond);
        if (NS_FAILED(rv)) {
          aError.ThrowEncodingError(
              "could not encode second prf output as urlsafe base64");
          return;
        }
        dest.mSecond.Construct(NS_ConvertUTF8toUTF16(prfSecond));
      }
    }
    if (mClientExtensionOutputs.mLargeBlob.WasPassed()) {
      const AuthenticationExtensionsLargeBlobOutputs& src =
          mClientExtensionOutputs.mLargeBlob.Value();
      AuthenticationExtensionsLargeBlobOutputsJSON& dest =
          json.mClientExtensionResults.mLargeBlob.Construct();
      if (src.mWritten.WasPassed()) {
        dest.mWritten.Construct(src.mWritten.Value());
      }
      if (mLargeBlobValue.isSome()) {
        nsCString largeBlobB64;
        nsresult rv = mozilla::Base64URLEncode(
            mLargeBlobValue->Length(), mLargeBlobValue->Elements(),
            Base64URLEncodePaddingPolicy::Omit, largeBlobB64);
        if (NS_FAILED(rv)) {
          aError.ThrowEncodingError(
              "could not encode large blob data as urlsafe base64");
          return;
        }
        dest.mBlob.Construct(NS_ConvertUTF8toUTF16(largeBlobB64));
      }
    }
    json.mType.Assign(u"public-key"_ns);
    if (!ToJSValue(aCx, json, &value)) {
      aError.StealExceptionFromJSContext(aCx);
      return;
    }
  } else {
    MOZ_ASSERT_UNREACHABLE(
        "either mAttestationResponse or mAssertionResponse should be set");
  }
  JS::Rooted<JSObject*> result(aCx, &value.toObject());
  aRetval.set(result);
}

void PublicKeyCredential::SetClientExtensionResultAppId(bool aResult) {
  mClientExtensionOutputs.mAppid.Construct();
  mClientExtensionOutputs.mAppid.Value() = aResult;
}

void PublicKeyCredential::SetClientExtensionResultCredPropsRk(bool aResult) {
  mClientExtensionOutputs.mCredProps.Construct();
  mClientExtensionOutputs.mCredProps.Value().mRk.Construct();
  mClientExtensionOutputs.mCredProps.Value().mRk.Value() = aResult;
}

void PublicKeyCredential::SetClientExtensionResultHmacSecret(
    bool aHmacCreateSecret) {
  mClientExtensionOutputs.mHmacCreateSecret.Construct();
  mClientExtensionOutputs.mHmacCreateSecret.Value() = aHmacCreateSecret;
}

void PublicKeyCredential::InitClientExtensionResultLargeBlob() {
  mClientExtensionOutputs.mLargeBlob.Construct();
}

void PublicKeyCredential::SetClientExtensionResultLargeBlobSupported(
    bool aLargeBlobSupported) {
  mClientExtensionOutputs.mLargeBlob.Value().mSupported.Construct(
      aLargeBlobSupported);
}

void PublicKeyCredential::SetClientExtensionResultLargeBlobValue(
    const nsTArray<uint8_t>& aLargeBlobValue) {
  mLargeBlobValue.emplace(aLargeBlobValue.Length());
  mLargeBlobValue->Assign(aLargeBlobValue);
}

void PublicKeyCredential::SetClientExtensionResultLargeBlobWritten(
    bool aLargeBlobWritten) {
  mClientExtensionOutputs.mLargeBlob.Value().mWritten.Construct(
      aLargeBlobWritten);
}

void PublicKeyCredential::InitClientExtensionResultPrf() {
  mClientExtensionOutputs.mPrf.Construct();
}

void PublicKeyCredential::SetClientExtensionResultPrfEnabled(bool aPrfEnabled) {
  mClientExtensionOutputs.mPrf.Value().mEnabled.Construct(aPrfEnabled);
}

void PublicKeyCredential::SetClientExtensionResultPrfResultsFirst(
    const nsTArray<uint8_t>& aPrfResultsFirst) {
  mPrfResultsFirst.emplace(32);
  mPrfResultsFirst->Assign(aPrfResultsFirst);
}

void PublicKeyCredential::SetClientExtensionResultPrfResultsSecond(
    const nsTArray<uint8_t>& aPrfResultsSecond) {
  mPrfResultsSecond.emplace(32);
  mPrfResultsSecond->Assign(aPrfResultsSecond);
}

bool Base64DecodeToArrayBuffer(GlobalObject& aGlobal, const nsAString& aString,
                               ArrayBuffer& aArrayBuffer, ErrorResult& aRv) {
  JSContext* cx = aGlobal.Context();
  JS::Rooted<JSObject*> result(cx);
  Base64URLDecodeOptions options;
  options.mPadding = Base64URLDecodePadding::Ignore;
  mozilla::dom::ChromeUtils::Base64URLDecode(
      aGlobal, NS_ConvertUTF16toUTF8(aString), options, &result, aRv);
  if (aRv.Failed()) {
    return false;
  }
  return aArrayBuffer.Init(result);
}

bool DecodeAuthenticationExtensionsPRFValuesJSON(
    GlobalObject& aGlobal,
    const AuthenticationExtensionsPRFValuesJSON& aBase64Values,
    AuthenticationExtensionsPRFValues& aValues, ErrorResult& aRv) {
  if (!Base64DecodeToArrayBuffer(aGlobal, aBase64Values.mFirst,
                                 aValues.mFirst.SetAsArrayBuffer(), aRv)) {
    return false;
  }
  if (aBase64Values.mSecond.WasPassed()) {
    if (!Base64DecodeToArrayBuffer(
            aGlobal, aBase64Values.mSecond.Value(),
            aValues.mSecond.Construct().SetAsArrayBuffer(), aRv)) {
      return false;
    }
  }
  return true;
}

bool DecodeAuthenticationExtensionsPRFInputsJSON(
    GlobalObject& aGlobal,
    const AuthenticationExtensionsPRFInputsJSON& aInputsJSON,
    AuthenticationExtensionsPRFInputs& aInputs, ErrorResult& aRv) {
  if (aInputsJSON.mEval.WasPassed()) {
    if (!DecodeAuthenticationExtensionsPRFValuesJSON(
            aGlobal, aInputsJSON.mEval.Value(), aInputs.mEval.Construct(),
            aRv)) {
      return false;
    }
  }
  if (aInputsJSON.mEvalByCredential.WasPassed()) {
    const Record<nsString, AuthenticationExtensionsPRFValuesJSON>& recordsJSON =
        aInputsJSON.mEvalByCredential.Value();
    Record<nsString, AuthenticationExtensionsPRFValues>& records =
        aInputs.mEvalByCredential.Construct();
    if (!records.Entries().SetCapacity(recordsJSON.Entries().Length(),
                                       fallible)) {
      return false;
    }
    for (auto& entryJSON : recordsJSON.Entries()) {
      auto entry = records.Entries().AppendElement();
      entry->mKey = entryJSON.mKey;
      if (!DecodeAuthenticationExtensionsPRFValuesJSON(
              aGlobal, entryJSON.mValue, entry->mValue, aRv)) {
        return false;
      }
    }
  }
  return true;
}

bool DecodeAuthenticationExtensionsLargeBlobInputsJSON(
    GlobalObject& aGlobal,
    const AuthenticationExtensionsLargeBlobInputsJSON& aInputsJSON,
    AuthenticationExtensionsLargeBlobInputs& aInputs, ErrorResult& aRv) {
  if (aInputsJSON.mSupport.WasPassed()) {
    aInputs.mSupport.Construct(aInputsJSON.mSupport.Value());
  }
  if (aInputsJSON.mRead.WasPassed()) {
    aInputs.mRead.Construct(aInputsJSON.mRead.Value());
  }
  if (aInputsJSON.mWrite.WasPassed()) {
    if (!Base64DecodeToArrayBuffer(
            aGlobal, aInputsJSON.mWrite.Value(),
            aInputs.mWrite.Construct().SetAsArrayBuffer(), aRv)) {
      return false;
    }
  }
  return true;
}

void PublicKeyCredential::ParseCreationOptionsFromJSON(
    GlobalObject& aGlobal,
    const PublicKeyCredentialCreationOptionsJSON& aOptions,
    PublicKeyCredentialCreationOptions& aResult, ErrorResult& aRv) {
  if (aOptions.mRp.mId.WasPassed()) {
    aResult.mRp.mId.Construct(aOptions.mRp.mId.Value());
  }
  aResult.mRp.mName.Assign(aOptions.mRp.mName);

  aResult.mUser.mName.Assign(aOptions.mUser.mName);
  if (!Base64DecodeToArrayBuffer(aGlobal, aOptions.mUser.mId,
                                 aResult.mUser.mId.SetAsArrayBuffer(), aRv)) {
    aRv.ThrowEncodingError("could not decode user ID as urlsafe base64");
    return;
  }
  aResult.mUser.mDisplayName.Assign(aOptions.mUser.mDisplayName);

  if (!Base64DecodeToArrayBuffer(aGlobal, aOptions.mChallenge,
                                 aResult.mChallenge.SetAsArrayBuffer(), aRv)) {
    aRv.ThrowEncodingError("could not decode challenge as urlsafe base64");
    return;
  }

  aResult.mPubKeyCredParams = aOptions.mPubKeyCredParams;

  if (aOptions.mTimeout.WasPassed()) {
    aResult.mTimeout.Construct(aOptions.mTimeout.Value());
  }

  for (const auto& excludeCredentialJSON : aOptions.mExcludeCredentials) {
    PublicKeyCredentialDescriptor* excludeCredential =
        aResult.mExcludeCredentials.AppendElement(fallible);
    if (!excludeCredential) {
      aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
      return;
    }
    excludeCredential->mType = excludeCredentialJSON.mType;
    if (!Base64DecodeToArrayBuffer(aGlobal, excludeCredentialJSON.mId,
                                   excludeCredential->mId.SetAsArrayBuffer(),
                                   aRv)) {
      aRv.ThrowEncodingError(
          "could not decode excluded credential ID as urlsafe base64");
      return;
    }
    if (excludeCredentialJSON.mTransports.WasPassed()) {
      excludeCredential->mTransports.Construct(
          excludeCredentialJSON.mTransports.Value());
    }
  }

  if (aOptions.mAuthenticatorSelection.WasPassed()) {
    aResult.mAuthenticatorSelection = aOptions.mAuthenticatorSelection.Value();
  }

  aResult.mAttestation = aOptions.mAttestation;

  if (aOptions.mExtensions.WasPassed()) {
    const AuthenticationExtensionsClientInputsJSON& extInputsJSON =
        aOptions.mExtensions.Value();
    AuthenticationExtensionsClientInputs& extInputs = aResult.mExtensions;
    if (extInputsJSON.mAppid.WasPassed()) {
      extInputs.mAppid.Construct(extInputsJSON.mAppid.Value());
    }
    if (extInputsJSON.mCredentialProtectionPolicy.WasPassed()) {
      extInputs.mCredentialProtectionPolicy.Construct(
          extInputsJSON.mCredentialProtectionPolicy.Value());
    }
    if (extInputsJSON.mEnforceCredentialProtectionPolicy.WasPassed()) {
      extInputs.mEnforceCredentialProtectionPolicy.Construct(
          extInputsJSON.mEnforceCredentialProtectionPolicy.Value());
    }
    if (extInputsJSON.mCredProps.WasPassed()) {
      extInputs.mCredProps.Construct(extInputsJSON.mCredProps.Value());
    }
    if (extInputsJSON.mHmacCreateSecret.WasPassed()) {
      extInputs.mHmacCreateSecret.Construct(
          extInputsJSON.mHmacCreateSecret.Value());
    }
    if (extInputsJSON.mMinPinLength.WasPassed()) {
      extInputs.mMinPinLength.Construct(extInputsJSON.mMinPinLength.Value());
    }
    if (extInputsJSON.mLargeBlob.WasPassed()) {
      const AuthenticationExtensionsLargeBlobInputsJSON& largeBlobInputsJSON =
          extInputsJSON.mLargeBlob.Value();
      AuthenticationExtensionsLargeBlobInputs& largeBlobInputs =
          aResult.mExtensions.mLargeBlob.Construct();
      if (!DecodeAuthenticationExtensionsLargeBlobInputsJSON(
              aGlobal, largeBlobInputsJSON, largeBlobInputs, aRv)) {
        aRv.ThrowEncodingError(
            "could not decode large blob inputs as urlsafe base64");
        return;
      }
    }
    if (extInputsJSON.mPrf.WasPassed()) {
      const AuthenticationExtensionsPRFInputsJSON& prfInputsJSON =
          extInputsJSON.mPrf.Value();
      AuthenticationExtensionsPRFInputs& prfInputs = extInputs.mPrf.Construct();
      if (!DecodeAuthenticationExtensionsPRFInputsJSON(aGlobal, prfInputsJSON,
                                                       prfInputs, aRv)) {
        aRv.ThrowEncodingError("could not decode prf inputs as urlsafe base64");
        return;
      }
    }
  }
}

void PublicKeyCredential::ParseRequestOptionsFromJSON(
    GlobalObject& aGlobal,
    const PublicKeyCredentialRequestOptionsJSON& aOptions,
    PublicKeyCredentialRequestOptions& aResult, ErrorResult& aRv) {
  if (!Base64DecodeToArrayBuffer(aGlobal, aOptions.mChallenge,
                                 aResult.mChallenge.SetAsArrayBuffer(), aRv)) {
    aRv.ThrowEncodingError("could not decode challenge as urlsafe base64");
    return;
  }

  if (aOptions.mTimeout.WasPassed()) {
    aResult.mTimeout.Construct(aOptions.mTimeout.Value());
  }

  if (aOptions.mRpId.WasPassed()) {
    aResult.mRpId.Construct(aOptions.mRpId.Value());
  }

  for (const auto& allowCredentialJSON : aOptions.mAllowCredentials) {
    PublicKeyCredentialDescriptor* allowCredential =
        aResult.mAllowCredentials.AppendElement(fallible);
    if (!allowCredential) {
      aRv.Throw(NS_ERROR_OUT_OF_MEMORY);
      return;
    }
    allowCredential->mType = allowCredentialJSON.mType;
    if (!Base64DecodeToArrayBuffer(aGlobal, allowCredentialJSON.mId,
                                   allowCredential->mId.SetAsArrayBuffer(),
                                   aRv)) {
      aRv.ThrowEncodingError(
          "could not decode allowed credential ID as urlsafe base64");
      return;
    }
    if (allowCredentialJSON.mTransports.WasPassed()) {
      allowCredential->mTransports.Construct(
          allowCredentialJSON.mTransports.Value());
    }
  }

  aResult.mUserVerification = aOptions.mUserVerification;

  if (aOptions.mExtensions.WasPassed()) {
    if (aOptions.mExtensions.Value().mAppid.WasPassed()) {
      aResult.mExtensions.mAppid.Construct(
          aOptions.mExtensions.Value().mAppid.Value());
    }
    if (aOptions.mExtensions.Value().mCredProps.WasPassed()) {
      aResult.mExtensions.mCredProps.Construct(
          aOptions.mExtensions.Value().mCredProps.Value());
    }
    if (aOptions.mExtensions.Value().mHmacCreateSecret.WasPassed()) {
      aResult.mExtensions.mHmacCreateSecret.Construct(
          aOptions.mExtensions.Value().mHmacCreateSecret.Value());
    }
    if (aOptions.mExtensions.Value().mLargeBlob.WasPassed()) {
      const AuthenticationExtensionsLargeBlobInputsJSON& largeBlobInputsJSON =
          aOptions.mExtensions.Value().mLargeBlob.Value();
      AuthenticationExtensionsLargeBlobInputs& largeBlobInputs =
          aResult.mExtensions.mLargeBlob.Construct();
      if (!DecodeAuthenticationExtensionsLargeBlobInputsJSON(
              aGlobal, largeBlobInputsJSON, largeBlobInputs, aRv)) {
        aRv.ThrowEncodingError(
            "could not decode large blob inputs as urlsafe base64");
        return;
      }
    }
    if (aOptions.mExtensions.Value().mMinPinLength.WasPassed()) {
      aResult.mExtensions.mMinPinLength.Construct(
          aOptions.mExtensions.Value().mMinPinLength.Value());
    }
    if (aOptions.mExtensions.Value().mPrf.WasPassed()) {
      const AuthenticationExtensionsPRFInputsJSON& prfInputsJSON =
          aOptions.mExtensions.Value().mPrf.Value();
      AuthenticationExtensionsPRFInputs& prfInputs =
          aResult.mExtensions.mPrf.Construct();
      if (!DecodeAuthenticationExtensionsPRFInputsJSON(aGlobal, prfInputsJSON,
                                                       prfInputs, aRv)) {
        aRv.ThrowEncodingError("could not decode prf inputs as urlsafe base64");
        return;
      }
    }
  }
}

}  // namespace mozilla::dom
