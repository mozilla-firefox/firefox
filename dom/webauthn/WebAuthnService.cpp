/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebAuthnService.h"

#include "WebAuthnEnumStrings.h"
#include "WebAuthnTransportIdentifiers.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_security.h"
#include "nsIObserverService.h"
#include "nsTextFormatter.h"
#include "nsThreadUtils.h"

namespace mozilla::dom {

already_AddRefed<nsIWebAuthnService> NewWebAuthnService() {
  nsCOMPtr<nsIWebAuthnService> webauthnService(new WebAuthnService());
  return webauthnService.forget();
}

#if defined(XP_MACOSX)
namespace {

// True when the request wants the PRF extension and is likely to be
// served by a security key rather than a platform authenticator. Used
// only to decide whether to route around a macOS version that cannot
// evaluate PRF for security keys; the cost of a false positive is a
// slower path, of a false negative a silently missing PRF output.
bool PrfRequestedForSecurityKey(nsIWebAuthnRegisterArgs* aArgs) {
  bool prf = false;
  if (NS_FAILED(aArgs->GetPrf(&prf)) || !prf) {
    return false;
  }
  // An explicit "platform" request is one we must not redirect: authrs
  // has no platform authenticator to offer.
  nsAutoString attachment;
  if (NS_SUCCEEDED(aArgs->GetAuthenticatorAttachment(attachment)) &&
      attachment.EqualsLiteral(
          MOZ_WEBAUTHN_AUTHENTICATOR_ATTACHMENT_PLATFORM)) {
    return false;
  }
  return true;
}

bool PrfRequestedForSecurityKey(nsIWebAuthnSignArgs* aArgs) {
  bool prf = false;
  if (NS_FAILED(aArgs->GetPrf(&prf)) || !prf) {
    return false;
  }
  // Mirrors the AppID heuristic below: a non-empty allow list with no
  // "internal" or "hybrid" credential is a security-key request.
  nsTArray<uint8_t> allowListTransports;
  (void)aArgs->GetAllowListTransports(allowListTransports);
  if (allowListTransports.IsEmpty()) {
    return false;
  }
  uint8_t transportSet = 0;
  for (const uint8_t& transport : allowListTransports) {
    transportSet |= transport;
  }
  uint8_t passkeyTransportMask =
      MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_INTERNAL |
      MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_HYBRID;
  return (transportSet & passkeyTransportMask) == 0;
}

}  // namespace
#endif

NS_IMPL_ISUPPORTS(WebAuthnService, nsIWebAuthnService)

void WebAuthnService::ShowAttestationConsentPrompt(
    const nsString& aOrigin, uint64_t aTransactionId,
    uint64_t aBrowsingContextId) {
  RefPtr<WebAuthnService> self = this;
#ifdef MOZ_WIDGET_ANDROID
  // We don't have a way to prompt the user for consent on Android, so just
  // assume consent not granted.
  nsCOMPtr<nsIRunnable> runnable(
      NS_NewRunnableFunction(__func__, [self, aTransactionId]() {
        self->SetHasAttestationConsent(
            aTransactionId,
            StaticPrefs::security_webauthn_always_allow_direct_attestation());
      }));
#else
  nsCOMPtr<nsIRunnable> runnable(NS_NewRunnableFunction(
      __func__, [self, aOrigin, aTransactionId, aBrowsingContextId]() {
        if (StaticPrefs::security_webauthn_always_allow_direct_attestation()) {
          self->SetHasAttestationConsent(aTransactionId, true);
          return;
        }
        nsCOMPtr<nsIObserverService> os = services::GetObserverService();
        if (!os) {
          return;
        }
        const nsLiteralString jsonFmt =
            u"{\"prompt\": {\"type\":\"attestation-consent\"},"_ns
            u"\"origin\": \"%S\","_ns
            u"\"tid\": %llu, \"browsingContextId\": %llu}"_ns;
        nsString json;
        nsTextFormatter::ssprintf(json, jsonFmt.get(), aOrigin.get(),
                                  aTransactionId, aBrowsingContextId);
        MOZ_ALWAYS_SUCCEEDS(
            os->NotifyObservers(nullptr, "webauthn-prompt", json.get()));
      }));
#endif
  NS_DispatchToMainThread(runnable.forget());
}

NS_IMETHODIMP
WebAuthnService::MakeCredential(uint64_t aTransactionId,
                                uint64_t aBrowsingContextId,
                                nsIWebAuthnRegisterArgs* aArgs,
                                nsIWebAuthnRegisterPromise* aPromise) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aArgs);
  MOZ_ASSERT(aPromise);

  ResetActiveTransaction();
  mActiveTransaction =
      Some(TransactionState{.service = DefaultService(),
                            .transactionId = aTransactionId,
                            .parentRegisterPromise = Some(aPromise)});

#if defined(XP_MACOSX)
  if (__builtin_available(macos 26.4, *)) {
    // This branch is intentionally empty; using `!__builtin_available(...)`
    // results in a compiler warning.
  } else {
    // Below macOS 26.4 the security key API cannot evaluate the PRF
    // extension -- `ASAuthorizationSecurityKeyPublicKeyCredentialRegistration`
    // grew a `prf` property only then, and platform passkeys only got one
    // in 15.0. A PRF request that the platform silently drops leaves the
    // caller with a credential it cannot derive from, so route it to
    // authenticator-rs instead. Only do this when the request is not
    // asking for a platform authenticator, which we cannot serve.
    if (PrfRequestedForSecurityKey(aArgs)) {
      mActiveTransaction.ref().service = AuthrsService();
    }
  }
#endif

  // We may need to show an attestation consent prompt before we return a
  // credential to WebAuthnTransactionParent, so we insert a new promise that
  // chains to `aPromise` here.

  nsString attestation;
  (void)aArgs->GetAttestationConveyancePreference(attestation);
  bool attestationRequested = !attestation.EqualsLiteral(
      MOZ_WEBAUTHN_ATTESTATION_CONVEYANCE_PREFERENCE_NONE);

  nsString origin;
  (void)aArgs->GetOrigin(origin);

  RefPtr<WebAuthnRegisterPromiseHolder> promiseHolder =
      new WebAuthnRegisterPromiseHolder(GetCurrentSerialEventTarget());

  RefPtr<WebAuthnService> self = this;
  RefPtr<WebAuthnRegisterPromise> promise = promiseHolder->Ensure();
  promise
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self, origin = std::move(origin), aTransactionId, aBrowsingContextId,
           attestationRequested](
              const WebAuthnRegisterPromise::ResolveOrRejectValue& aValue) {
            MOZ_ASSERT(NS_IsMainThread());
            if (self->mActiveTransaction.isNothing()) {
              return;
            }
            MOZ_ASSERT(
                self->mActiveTransaction.ref().parentRegisterPromise.isSome());
            MOZ_ASSERT(
                self->mActiveTransaction.ref().registerResult.isNothing());
            MOZ_ASSERT(
                self->mActiveTransaction.ref().childRegisterRequest.Exists());

            self->mActiveTransaction.ref().childRegisterRequest.Complete();

            if (aValue.IsReject()) {
              self->mActiveTransaction.ref()
                  .parentRegisterPromise.ref()
                  ->Reject(aValue.RejectValue());
              self->mActiveTransaction.reset();
              return;
            }

            nsIWebAuthnRegisterResult* result = aValue.ResolveValue();
            // We can return whatever result we have if the authenticator
            // handled attestation consent for us.
            bool attestationConsentPromptShown = false;
            (void)result->GetAttestationConsentPromptShown(
                &attestationConsentPromptShown);
            if (attestationConsentPromptShown) {
              self->mActiveTransaction.ref()
                  .parentRegisterPromise.ref()
                  ->Resolve(result);
              self->mActiveTransaction.reset();
              return;
            }
            // If the RP requested attestation and the response contains
            // identifying information, then we need to show a consent
            // prompt.
            bool resultIsIdentifying = true;
            (void)result->HasIdentifyingAttestation(&resultIsIdentifying);
            if (attestationRequested && resultIsIdentifying) {
              self->mActiveTransaction.ref().registerResult = Some(result);
              self->ShowAttestationConsentPrompt(origin, aTransactionId,
                                                 aBrowsingContextId);
              return;
            }
            // In all other cases we strip out identifying information.
            result->Anonymize();
            self->mActiveTransaction.ref().parentRegisterPromise.ref()->Resolve(
                result);
            self->mActiveTransaction.reset();
          })
      ->Track(mActiveTransaction.ref().childRegisterRequest);

  nsresult rv = ActiveService()->MakeCredential(
      aTransactionId, aBrowsingContextId, aArgs, promiseHolder);
  if (NS_FAILED(rv)) {
    promiseHolder->Reject(NS_ERROR_DOM_NOT_ALLOWED_ERR);
  }
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnService::GetAssertion(uint64_t aTransactionId,
                              uint64_t aBrowsingContextId,
                              nsIWebAuthnSignArgs* aArgs,
                              nsIWebAuthnSignPromise* aPromise) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aArgs);
  MOZ_ASSERT(aPromise);

  bool conditionallyMediated;
  (void)aArgs->GetConditionallyMediated(&conditionallyMediated);

  if (conditionallyMediated) {
    // Conditional gets coexist with transactions from other browsing
    // contexts. We hold them here until they are promoted to a modal flow via
    // SelectAutoFillEntry or ResumeConditionalGet, or cancelled.
    mConditionalGets.RemoveElementsBy(
        [aBrowsingContextId](ConditionalGet& aEntry) {
          if (aEntry.browsingContextId != aBrowsingContextId) {
            return false;
          }
          aEntry.signPromise->Reject(NS_ERROR_DOM_NOT_ALLOWED_ERR);
          return true;
        });
    mConditionalGets.AppendElement(ConditionalGet{
        .transactionId = aTransactionId,
        .browsingContextId = aBrowsingContextId,
        .signArgs = aArgs,
        .signPromise = aPromise,
    });

    // Defer the observer notification to the next event-loop turn so it fires
    // after this call has fully returned to the caller.
    nsCOMPtr<nsIRunnable> runnable(NS_NewRunnableFunction(__func__, []() {
      nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
      if (os) {
        os->NotifyObservers(nullptr, "webauthn:conditional-get-pending",
                            nullptr);
      }
    }));
    NS_DispatchToMainThread(runnable.forget());
    return NS_OK;
  }

  ResetActiveTransaction();
  mActiveTransaction = Some(TransactionState{.service = DefaultService(),
                                             .transactionId = aTransactionId});

#if defined(XP_MACOSX)
  if (__builtin_available(macos 14.5, *)) {
    // This branch is intentionally empty; using `!__builtin_available(...)`
    // results in a compiler warning.
  } else {
    // On macOS < 14.5 the security key API doesn't handle the AppID
    // extension. So we'll use authenticator-rs if it's likely that the
    // request requires AppID. We consider it likely if 1) the AppID
    // extension is present, 2) the allow list is non-empty, and 3) none of
    // the allowed credentials use the "internal" or "hybrid" transport.
    nsString appId;
    nsresult rv = aArgs->GetAppId(appId);
    if (rv == NS_OK) {  // AppID is set
      uint8_t transportSet = 0;
      nsTArray<uint8_t> allowListTransports;
      (void)aArgs->GetAllowListTransports(allowListTransports);
      for (const uint8_t& transport : allowListTransports) {
        transportSet |= transport;
      }
      uint8_t passkeyTransportMask =
          MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_INTERNAL |
          MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_HYBRID;
      if (allowListTransports.Length() > 0 &&
          (transportSet & passkeyTransportMask) == 0) {
        mActiveTransaction.ref().service = AuthrsService();
      }
    }
  }

  if (__builtin_available(macos 26.4, *)) {
    // This branch is intentionally empty; using `!__builtin_available(...)`
    // results in a compiler warning.
  } else {
    // Same reasoning as in MakeCredential: below macOS 26.4 the security
    // key API cannot evaluate PRF, and a dropped PRF request is worse
    // than a slower one, so hand it to authenticator-rs.
    if (PrfRequestedForSecurityKey(aArgs)) {
      mActiveTransaction.ref().service = AuthrsService();
    }
  }
#endif

  nsresult rv = ActiveService()->GetAssertion(
      aTransactionId, aBrowsingContextId, aArgs, aPromise);
  if (NS_FAILED(rv)) {
    mActiveTransaction.reset();
    return rv;
  }
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnService::GetIsUVPAA(bool* aAvailable) {
  return DefaultService()->GetIsUVPAA(aAvailable);
}

NS_IMETHODIMP
WebAuthnService::HasPendingConditionalGet(uint64_t aBrowsingContextId,
                                          const nsAString& aOrigin,
                                          uint64_t* aRv) {
  MOZ_ASSERT(NS_IsMainThread());
  for (const auto& entry : mConditionalGets) {
    if (entry.browsingContextId != aBrowsingContextId) {
      continue;
    }
    nsString entryOrigin;
    (void)entry.signArgs->GetOrigin(entryOrigin);
    if (entryOrigin == aOrigin) {
      *aRv = entry.transactionId;
      return NS_OK;
    }
  }
  *aRv = 0;
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnService::GetAutoFillEntries(
    uint64_t aTransactionId, nsIWebAuthnAutoFillEntriesCallback* aCallback) {
  MOZ_ASSERT(NS_IsMainThread());
  nsCOMPtr<nsIWebAuthnSignArgs> signArgs;
  for (const auto& entry : mConditionalGets) {
    if (entry.transactionId == aTransactionId) {
      signArgs = entry.signArgs;
      break;
    }
  }
  if (!signArgs) {
    aCallback->Reject(NS_ERROR_NOT_AVAILABLE);
    return NS_OK;
  }
  nsString rpId;
  nsTArray<nsTArray<uint8_t>> allowList;
  (void)signArgs->GetRpId(rpId);
  (void)signArgs->GetAllowList(allowList);
  return DefaultService()->GetAutoFillEntriesForRpId(rpId, allowList,
                                                     aCallback);
}

NS_IMETHODIMP
WebAuthnService::GetAutoFillEntriesForRpId(
    const nsAString& aRpId, const nsTArray<nsTArray<uint8_t>>& aAllowList,
    nsIWebAuthnAutoFillEntriesCallback* aCallback) {
  MOZ_ASSERT(NS_IsMainThread());
  return DefaultService()->GetAutoFillEntriesForRpId(aRpId, aAllowList,
                                                     aCallback);
}

Maybe<WebAuthnService::ConditionalGet> WebAuthnService::TakeConditionalByTid(
    uint64_t aTransactionId) {
  MOZ_ASSERT(NS_IsMainThread());
  Maybe<ConditionalGet> found;
  mConditionalGets.RemoveElementsBy([&](ConditionalGet& aEntry) {
    if (aEntry.transactionId != aTransactionId) {
      return false;
    }
    found = Some(std::move(aEntry));
    return true;
  });
  return found;
}

nsresult WebAuthnService::DispatchConditionalGetAssertion(
    const ConditionalGet& aPending, nsIWebAuthnSignArgs* aArgs) {
  MOZ_ASSERT(NS_IsMainThread());
  ResetActiveTransaction();
  mActiveTransaction = Some(TransactionState{
      .service = DefaultService(), .transactionId = aPending.transactionId});
  nsresult rv = ActiveService()->GetAssertion(aPending.transactionId,
                                              aPending.browsingContextId, aArgs,
                                              aPending.signPromise);
  if (NS_FAILED(rv)) {
    aPending.signPromise->Reject(NS_ERROR_DOM_NOT_ALLOWED_ERR);
    mActiveTransaction.reset();
    return rv;
  }
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnService::SelectAutoFillEntry(uint64_t aTransactionId,
                                     const nsTArray<uint8_t>& aCredentialId) {
  MOZ_ASSERT(NS_IsMainThread());
  Maybe<ConditionalGet> found = TakeConditionalByTid(aTransactionId);
  if (found.isNothing()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  // If the original allow list is non-empty, the selected credential must be
  // in it.
  nsTArray<nsTArray<uint8_t>> allowList;
  (void)found.ref().signArgs->GetAllowList(allowList);
  if (!allowList.IsEmpty() && !allowList.Contains(aCredentialId)) {
    found.ref().signPromise->Reject(NS_ERROR_DOM_NOT_ALLOWED_ERR);
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIWebAuthnSignArgs> args;
  nsresult rv = found.ref().signArgs->CloneWithSelectedCredential(
      aCredentialId, getter_AddRefs(args));
  if (NS_FAILED(rv)) {
    found.ref().signPromise->Reject(NS_ERROR_DOM_NOT_ALLOWED_ERR);
    return rv;
  }
  return DispatchConditionalGetAssertion(found.ref(), args);
}

NS_IMETHODIMP
WebAuthnService::ResumeConditionalGet(uint64_t aTransactionId) {
  MOZ_ASSERT(NS_IsMainThread());
  Maybe<ConditionalGet> found = TakeConditionalByTid(aTransactionId);
  if (found.isNothing()) {
    return NS_ERROR_NOT_AVAILABLE;
  }
  return DispatchConditionalGetAssertion(found.ref(), found.ref().signArgs);
}

void WebAuthnService::RejectActiveRegisterPromise() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mActiveTransaction.isSome());
  mActiveTransaction.ref().childRegisterRequest.DisconnectIfExists();
  if (mActiveTransaction.ref().parentRegisterPromise.isSome()) {
    mActiveTransaction.ref().parentRegisterPromise.ref()->Reject(
        NS_ERROR_DOM_NOT_ALLOWED_ERR);
  }
}

void WebAuthnService::ResetActiveTransaction() {
  MOZ_ASSERT(NS_IsMainThread());
  if (mActiveTransaction.isSome()) {
    RejectActiveRegisterPromise();
    ActiveService()->Reset();
    mActiveTransaction.reset();
  }
}

NS_IMETHODIMP
WebAuthnService::Reset() {
  MOZ_ASSERT(NS_IsMainThread());
  ResetActiveTransaction();
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnService::Cancel(uint64_t aTransactionId) {
  MOZ_ASSERT(NS_IsMainThread());
  if (Maybe<ConditionalGet> found = TakeConditionalByTid(aTransactionId);
      found.isSome()) {
    found.ref().signPromise->Reject(NS_ERROR_DOM_NOT_ALLOWED_ERR);
    return NS_OK;
  }

  if (mActiveTransaction.isSome() &&
      mActiveTransaction.ref().transactionId == aTransactionId) {
    RejectActiveRegisterPromise();
    ActiveService()->Cancel(aTransactionId);
    mActiveTransaction.reset();
    return NS_OK;
  }

  // about:webauthn drives an interactive management session through Listen()
  // and RunCommand(), which always run on the authrs service and are not
  // tracked as an mActiveTransaction. Its Cancel() must reach authrs too, or
  // the session's status receiver (a blocking background task) is never torn
  // down and blocks shutdown.
  AuthrsService()->Cancel(aTransactionId);
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnService::SetHasAttestationConsent(uint64_t aTransactionId,
                                          bool aHasConsent) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mActiveTransaction.isNothing() ||
      mActiveTransaction.ref().transactionId != aTransactionId) {
    // This could happen if the transaction was reset just when the prompt was
    // receiving user input.
    return NS_OK;
  }

  MOZ_ASSERT(mActiveTransaction.ref().parentRegisterPromise.isSome());
  MOZ_ASSERT(mActiveTransaction.ref().registerResult.isSome());
  MOZ_ASSERT(!mActiveTransaction.ref().childRegisterRequest.Exists());

  if (!aHasConsent) {
    mActiveTransaction.ref().registerResult.ref()->Anonymize();
  }
  mActiveTransaction.ref().parentRegisterPromise.ref()->Resolve(
      mActiveTransaction.ref().registerResult.ref());

  mActiveTransaction.reset();
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnService::PinCallback(uint64_t aTransactionId, const nsACString& aPin) {
  MOZ_ASSERT(NS_IsMainThread());
  if (aTransactionId == 0) {
    // about:webauthn PIN callback
    return AuthrsService()->PinCallback(aTransactionId, aPin);
  }

  if (mActiveTransaction.isNothing() ||
      mActiveTransaction.ref().transactionId != aTransactionId) {
    return NS_OK;
  }
  return ActiveService()->PinCallback(aTransactionId, aPin);
}

NS_IMETHODIMP
WebAuthnService::SelectionCallback(uint64_t aTransactionId, uint64_t aIndex) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mActiveTransaction.isNothing() ||
      mActiveTransaction.ref().transactionId != aTransactionId) {
    return NS_OK;
  }
  return ActiveService()->SelectionCallback(aTransactionId, aIndex);
}

NS_IMETHODIMP
WebAuthnService::AddVirtualAuthenticator(
    const nsACString& aProtocol, const nsACString& aTransport,
    bool aHasResidentKey, bool aHasUserVerification, bool aIsUserConsenting,
    bool aIsUserVerified, nsACString& aRetval) {
  MOZ_ASSERT(NS_IsMainThread());
  return AuthrsService()->AddVirtualAuthenticator(
      aProtocol, aTransport, aHasResidentKey, aHasUserVerification,
      aIsUserConsenting, aIsUserVerified, aRetval);
}

NS_IMETHODIMP
WebAuthnService::HasVirtualAuthenticator(const nsACString& aAuthenticatorId,
                                         bool* aRetval) {
  MOZ_ASSERT(NS_IsMainThread());
  return AuthrsService()->HasVirtualAuthenticator(aAuthenticatorId, aRetval);
}

NS_IMETHODIMP
WebAuthnService::RemoveVirtualAuthenticator(
    const nsACString& aAuthenticatorId) {
  MOZ_ASSERT(NS_IsMainThread());
  return AuthrsService()->RemoveVirtualAuthenticator(aAuthenticatorId);
}

NS_IMETHODIMP
WebAuthnService::AddCredential(const nsACString& aAuthenticatorId,
                               const nsACString& aCredentialId,
                               bool aIsResidentCredential,
                               const nsACString& aRpId,
                               const nsACString& aPrivateKey,
                               const nsACString& aUserHandle,
                               uint32_t aSignCount) {
  MOZ_ASSERT(NS_IsMainThread());
  return AuthrsService()->AddCredential(aAuthenticatorId, aCredentialId,
                                        aIsResidentCredential, aRpId,
                                        aPrivateKey, aUserHandle, aSignCount);
}

NS_IMETHODIMP
WebAuthnService::GetCredentials(
    const nsACString& aAuthenticatorId,
    nsTArray<RefPtr<nsICredentialParameters>>& aRetval) {
  MOZ_ASSERT(NS_IsMainThread());
  return AuthrsService()->GetCredentials(aAuthenticatorId, aRetval);
}

NS_IMETHODIMP
WebAuthnService::RemoveCredential(const nsACString& aAuthenticatorId,
                                  const nsACString& aCredentialId) {
  MOZ_ASSERT(NS_IsMainThread());
  return AuthrsService()->RemoveCredential(aAuthenticatorId, aCredentialId);
}

NS_IMETHODIMP
WebAuthnService::RemoveAllCredentials(const nsACString& aAuthenticatorId) {
  MOZ_ASSERT(NS_IsMainThread());
  return AuthrsService()->RemoveAllCredentials(aAuthenticatorId);
}

NS_IMETHODIMP
WebAuthnService::SetUserVerified(const nsACString& aAuthenticatorId,
                                 bool aIsUserVerified) {
  MOZ_ASSERT(NS_IsMainThread());
  return AuthrsService()->SetUserVerified(aAuthenticatorId, aIsUserVerified);
}

NS_IMETHODIMP
WebAuthnService::Listen() {
  MOZ_ASSERT(NS_IsMainThread());
  return AuthrsService()->Listen();
}

NS_IMETHODIMP
WebAuthnService::RunCommand(const nsACString& aCmd) {
  MOZ_ASSERT(NS_IsMainThread());
  return AuthrsService()->RunCommand(aCmd);
}

}  // namespace mozilla::dom
