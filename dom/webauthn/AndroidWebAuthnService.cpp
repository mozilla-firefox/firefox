/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AndroidWebAuthnService.h"

#include "JavaBuiltins.h"
#include "JavaExceptions.h"
#include "WebAuthnEnumStrings.h"
#include "WebAuthnPromiseHolder.h"
#include "WebAuthnResult.h"
#include "mozilla/StaticPrefs_security.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "mozilla/java/WebAuthnTokenManagerWrappers.h"
#include "mozilla/jni/Conversions.h"
#include "mozilla/jni/GeckoBundleUtils.h"

namespace mozilla {
namespace jni {
template <>
dom::AndroidWebAuthnError Java2Native(mozilla::jni::Object::Param aData,
                                      JNIEnv* aEnv) {
  MOZ_ASSERT(aData.IsInstanceOf<jni::Throwable>());
  java::sdk::Throwable::LocalRef throwable(aData);
  return dom::AndroidWebAuthnError(throwable->GetMessage()->ToString());
}
}  // namespace jni

namespace dom {

NS_IMPL_ISUPPORTS(AndroidWebAuthnService, nsIWebAuthnService)

NS_IMETHODIMP
AndroidWebAuthnService::GetIsUVPAA(bool* aAvailable) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
AndroidWebAuthnService::MakeCredential(uint64_t aTransactionId,
                                       uint64_t aBrowsingContextId,
                                       nsIWebAuthnRegisterArgs* aArgs,
                                       nsIWebAuthnRegisterPromise* aPromise) {
  MOZ_ASSERT(NS_IsMainThread());

  GECKOBUNDLE_START(credentialBundle);
  GECKOBUNDLE_PUT(credentialBundle, "isWebAuthn",
                  java::sdk::Integer::ValueOf(1));

  {
    GECKOBUNDLE_START(rpBundle);

    nsString rpId;
    (void)aArgs->GetRpId(rpId);
    GECKOBUNDLE_PUT(rpBundle, "id", jni::StringParam(rpId));

    nsString rpName;
    (void)aArgs->GetRpName(rpName);
    GECKOBUNDLE_PUT(rpBundle, "name", jni::StringParam(rpName));

    GECKOBUNDLE_FINISH(rpBundle);
    GECKOBUNDLE_PUT(credentialBundle, "rp", rpBundle);
  }

  {
    GECKOBUNDLE_START(userBundle);

    nsString userName;
    (void)aArgs->GetUserName(userName);
    GECKOBUNDLE_PUT(userBundle, "name", jni::StringParam(userName));

    nsString userDisplayName;
    (void)aArgs->GetUserDisplayName(userDisplayName);
    GECKOBUNDLE_PUT(userBundle, "displayName",
                    jni::StringParam(userDisplayName));

    GECKOBUNDLE_FINISH(userBundle);
    GECKOBUNDLE_PUT(credentialBundle, "user", userBundle);
  }

  nsString origin;
  (void)aArgs->GetOrigin(origin);
  GECKOBUNDLE_PUT(credentialBundle, "origin", jni::StringParam(origin));

  uint32_t timeout;
  (void)aArgs->GetTimeoutMS(&timeout);
  GECKOBUNDLE_PUT(credentialBundle, "timeout", java::sdk::Double::New(timeout));

  // Add UI support to consent to attestation, bug 1550164
  GECKOBUNDLE_PUT(credentialBundle, "attestation",
                  jni::StringParam(u"none"_ns));

  GECKOBUNDLE_FINISH(credentialBundle);

  nsTArray<uint8_t> userId;
  (void)aArgs->GetUserId(userId);
  jni::ByteBuffer::LocalRef uid = jni::ByteBuffer::New(
      const_cast<void*>(static_cast<const void*>(userId.Elements())),
      userId.Length());

  nsTArray<uint8_t> challBuf;
  (void)aArgs->GetChallenge(challBuf);
  jni::ByteBuffer::LocalRef challenge = jni::ByteBuffer::New(
      const_cast<void*>(static_cast<const void*>(challBuf.Elements())),
      challBuf.Length());

  nsTArray<nsTArray<uint8_t>> excludeList;
  (void)aArgs->GetExcludeList(excludeList);
  jni::ObjectArray::LocalRef idList =
      jni::ObjectArray::New(excludeList.Length());
  int ix = 0;
  for (const nsTArray<uint8_t>& credId : excludeList) {
    jni::ByteBuffer::LocalRef id = jni::ByteBuffer::New(
        const_cast<void*>(static_cast<const void*>(credId.Elements())),
        credId.Length());

    idList->SetElement(ix, id);

    ix += 1;
  }

  nsTArray<uint8_t> transportBuf;
  (void)aArgs->GetExcludeListTransports(transportBuf);
  jni::ByteBuffer::LocalRef transportList = jni::ByteBuffer::New(
      const_cast<void*>(static_cast<const void*>(transportBuf.Elements())),
      transportBuf.Length());

  nsTArray<uint8_t> clientDataHash;
  (void)aArgs->GetClientDataHash(clientDataHash);
  jni::ByteBuffer::LocalRef hash = jni::ByteBuffer::New(
      const_cast<void*>(static_cast<const void*>(clientDataHash.Elements())),
      clientDataHash.Length());

  nsTArray<int32_t> coseAlgs;
  (void)aArgs->GetCoseAlgs(coseAlgs);
  jni::IntArray::LocalRef algs =
      jni::IntArray::New(coseAlgs.Elements(), coseAlgs.Length());

  GECKOBUNDLE_START(authSelBundle);

  nsString residentKey;
  (void)aArgs->GetResidentKey(residentKey);

  // Get extensions
  bool requestedCredProps;
  (void)aArgs->GetCredProps(&requestedCredProps);

  // Unfortunately, GMS's FIDO2 API has no option for Passkey. If using
  // residentKey, credential will be synced with Passkey via Google
  // account or credential provider service. So this is experimental.
  if (requestedCredProps &&
      StaticPrefs::
          security_webauthn_webauthn_enable_android_fido2_residentkey()) {
    GECKOBUNDLE_PUT(authSelBundle, "residentKey",
                    jni::StringParam(residentKey));
  }

  nsString userVerification;
  (void)aArgs->GetUserVerification(userVerification);
  if (userVerification.EqualsLiteral(
          MOZ_WEBAUTHN_USER_VERIFICATION_REQUIREMENT_REQUIRED) ||
      userVerification.EqualsLiteral(
          MOZ_WEBAUTHN_USER_VERIFICATION_REQUIREMENT_PREFERRED) ||
      userVerification.EqualsLiteral(
          MOZ_WEBAUTHN_USER_VERIFICATION_REQUIREMENT_DISCOURAGED)) {
    GECKOBUNDLE_PUT(authSelBundle, "userVerification",
                    jni::StringParam(userVerification));
  }

  nsString authenticatorAttachment;
  nsresult rv = aArgs->GetAuthenticatorAttachment(authenticatorAttachment);
  if (rv != NS_ERROR_NOT_AVAILABLE) {
    if (NS_FAILED(rv)) {
      aPromise->Reject(rv);
      return NS_OK;
    }
    if (authenticatorAttachment.EqualsLiteral(
            MOZ_WEBAUTHN_AUTHENTICATOR_ATTACHMENT_PLATFORM) ||
        authenticatorAttachment.EqualsLiteral(
            MOZ_WEBAUTHN_AUTHENTICATOR_ATTACHMENT_CROSS_PLATFORM)) {
      GECKOBUNDLE_PUT(authSelBundle, "authenticatorAttachment",
                      jni::StringParam(authenticatorAttachment));
    }
  }
  GECKOBUNDLE_FINISH(authSelBundle);

  GECKOBUNDLE_START(extensionsBundle);
  GECKOBUNDLE_PUT(extensionsBundle, "credProps",
                  requestedCredProps ? java::sdk::Boolean::TRUE()
                                     : java::sdk::Boolean::FALSE());
  GECKOBUNDLE_FINISH(extensionsBundle);

  nsString json;
  (void)aArgs->GetJson(json);

  auto result = java::WebAuthnTokenManager::WebAuthnMakeCredential(
      credentialBundle, uid, challenge, idList, transportList, authSelBundle,
      extensionsBundle, algs, hash, json);

  auto geckoResult = java::GeckoResult::LocalRef(std::move(result));

  MozPromise<RefPtr<WebAuthnRegisterResult>, AndroidWebAuthnError,
             true>::FromGeckoResult(geckoResult)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aPromise =
               RefPtr{aPromise}](RefPtr<WebAuthnRegisterResult>&& aValue) {
            aPromise->Resolve(aValue);
          },
          [aPromise = RefPtr{aPromise}](AndroidWebAuthnError&& aValue) {
            aPromise->Reject(aValue.GetError());
          });

  return NS_OK;
}

NS_IMETHODIMP
AndroidWebAuthnService::GetAssertion(uint64_t aTransactionId,
                                     uint64_t aBrowsingContextId,
                                     nsIWebAuthnSignArgs* aArgs,
                                     nsIWebAuthnSignPromise* aPromise) {
  MOZ_ASSERT(NS_IsMainThread());

  nsTArray<uint8_t> challBuf;
  (void)aArgs->GetChallenge(challBuf);
  jni::ByteBuffer::LocalRef challenge = jni::ByteBuffer::New(
      const_cast<void*>(static_cast<const void*>(challBuf.Elements())),
      challBuf.Length());

  nsTArray<nsTArray<uint8_t>> allowList;
  (void)aArgs->GetAllowList(allowList);
  jni::ObjectArray::LocalRef idList = jni::ObjectArray::New(allowList.Length());
  int ix = 0;
  for (const nsTArray<uint8_t>& credId : allowList) {
    jni::ByteBuffer::LocalRef id = jni::ByteBuffer::New(
        const_cast<void*>(static_cast<const void*>(credId.Elements())),
        credId.Length());

    idList->SetElement(ix, id);

    ix += 1;
  }

  nsTArray<uint8_t> clientDataHash;
  (void)aArgs->GetClientDataHash(clientDataHash);
  jni::ByteBuffer::LocalRef hash = jni::ByteBuffer::New(
      const_cast<void*>(static_cast<const void*>(clientDataHash.Elements())),
      clientDataHash.Length());

  nsTArray<uint8_t> transportBuf;
  (void)aArgs->GetAllowListTransports(transportBuf);
  jni::ByteBuffer::LocalRef transportList = jni::ByteBuffer::New(
      const_cast<void*>(static_cast<const void*>(transportBuf.Elements())),
      transportBuf.Length());

  GECKOBUNDLE_START(assertionBundle);

  GECKOBUNDLE_PUT(assertionBundle, "isWebAuthn",
                  java::sdk::Integer::ValueOf(1));

  nsString rpId;
  (void)aArgs->GetRpId(rpId);
  GECKOBUNDLE_PUT(assertionBundle, "rpId", jni::StringParam(rpId));

  nsString origin;
  (void)aArgs->GetOrigin(origin);
  GECKOBUNDLE_PUT(assertionBundle, "origin", jni::StringParam(origin));

  uint32_t timeout;
  (void)aArgs->GetTimeoutMS(&timeout);
  GECKOBUNDLE_PUT(assertionBundle, "timeout", java::sdk::Double::New(timeout));

  nsString userVerification;
  (void)aArgs->GetUserVerification(userVerification);
  GECKOBUNDLE_PUT(assertionBundle, "userVerification",
                  jni::StringParam(userVerification));

  GECKOBUNDLE_FINISH(assertionBundle);

  GECKOBUNDLE_START(extensionsBundle);

  nsString appId;
  nsresult rv = aArgs->GetAppId(appId);
  if (rv != NS_ERROR_NOT_AVAILABLE) {
    if (NS_FAILED(rv)) {
      aPromise->Reject(NS_ERROR_DOM_NOT_ALLOWED_ERR);
      return NS_OK;
    }
    GECKOBUNDLE_PUT(extensionsBundle, "fidoAppId", jni::StringParam(appId));
  }

  GECKOBUNDLE_FINISH(extensionsBundle);

  nsString json;
  (void)aArgs->GetJson(json);

  auto result = java::WebAuthnTokenManager::WebAuthnGetAssertion(
      challenge, idList, transportList, assertionBundle, extensionsBundle, hash,
      json);
  auto geckoResult = java::GeckoResult::LocalRef(std::move(result));
  MozPromise<RefPtr<WebAuthnSignResult>, AndroidWebAuthnError,
             true>::FromGeckoResult(geckoResult)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aPromise = RefPtr{aPromise}](RefPtr<WebAuthnSignResult>&& aValue) {
            aPromise->Resolve(aValue);
          },
          [aPromise = RefPtr{aPromise}](AndroidWebAuthnError&& aValue) {
            aPromise->Reject(aValue.GetError());
          });

  return NS_OK;
}

NS_IMETHODIMP
AndroidWebAuthnService::Reset() {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_OK;
}

NS_IMETHODIMP
AndroidWebAuthnService::Cancel(uint64_t aTransactionId) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
AndroidWebAuthnService::HasPendingConditionalGet(uint64_t aBrowsingContextId,
                                                 const nsAString& aOrigin,
                                                 uint64_t* aRv) {
  MOZ_ASSERT(NS_IsMainThread());
  // Signal that there is no pending conditional get request, so the caller
  // will not attempt to call GetAutoFillEntries, SelectAutoFillEntry, or
  // ResumeConditionalGet (as these are not implemented).
  *aRv = 0;
  return NS_OK;
}

NS_IMETHODIMP
AndroidWebAuthnService::GetAutoFillEntries(
    uint64_t aTransactionId, nsIWebAuthnAutoFillEntriesCallback* aCallback) {
  MOZ_ASSERT(NS_IsMainThread());
  aCallback->Reject(NS_ERROR_NOT_IMPLEMENTED);
  return NS_OK;
}

NS_IMETHODIMP
AndroidWebAuthnService::GetAutoFillEntriesForRpId(
    const nsAString& aRpId, const nsTArray<nsTArray<uint8_t>>& aAllowList,
    nsIWebAuthnAutoFillEntriesCallback* aCallback) {
  MOZ_ASSERT(NS_IsMainThread());
  aCallback->Reject(NS_ERROR_NOT_IMPLEMENTED);
  return NS_OK;
}

NS_IMETHODIMP
AndroidWebAuthnService::SelectAutoFillEntry(
    uint64_t aTransactionId, const nsTArray<uint8_t>& aCredentialId) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
AndroidWebAuthnService::ResumeConditionalGet(uint64_t aTransactionId) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
AndroidWebAuthnService::PinCallback(uint64_t aTransactionId,
                                    const nsACString& aPin) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
AndroidWebAuthnService::SetHasAttestationConsent(uint64_t aTransactionId,
                                                 bool aHasConsent) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
AndroidWebAuthnService::SelectionCallback(uint64_t aTransactionId,
                                          uint64_t aIndex) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
AndroidWebAuthnService::AddVirtualAuthenticator(
    const nsACString& aProtocol, const nsACString& aTransport,
    bool aHasResidentKey, bool aHasUserVerification, bool aIsUserConsenting,
    bool aIsUserVerified, nsACString& aRetval) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
AndroidWebAuthnService::HasVirtualAuthenticator(
    const nsACString& aAuthenticatorId, bool* aRetval) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
AndroidWebAuthnService::RemoveVirtualAuthenticator(
    const nsACString& aAuthenticatorId) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
AndroidWebAuthnService::AddCredential(const nsACString& aAuthenticatorId,
                                      const nsACString& aCredentialId,
                                      bool aIsResidentCredential,
                                      const nsACString& aRpId,
                                      const nsACString& aPrivateKey,
                                      const nsACString& aUserHandle,
                                      uint32_t aSignCount) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
AndroidWebAuthnService::GetCredentials(
    const nsACString& aAuthenticatorId,
    nsTArray<RefPtr<nsICredentialParameters>>& _aRetval) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
AndroidWebAuthnService::RemoveCredential(const nsACString& aAuthenticatorId,
                                         const nsACString& aCredentialId) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
AndroidWebAuthnService::RemoveAllCredentials(
    const nsACString& aAuthenticatorId) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
AndroidWebAuthnService::SetUserVerified(const nsACString& aAuthenticatorId,
                                        bool aIsUserVerified) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
AndroidWebAuthnService::Listen() {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
AndroidWebAuthnService::RunCommand(const nsACString& aCmd) {
  MOZ_ASSERT(NS_IsMainThread());
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
AndroidWebAuthnService::StartLogging() { return NS_ERROR_NOT_IMPLEMENTED; }

NS_IMETHODIMP
AndroidWebAuthnService::StopLogging() { return NS_ERROR_NOT_IMPLEMENTED; }

}  // namespace dom
}  // namespace mozilla
