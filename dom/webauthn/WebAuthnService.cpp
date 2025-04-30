/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_security.h"
#include "nsIObserverService.h"
#include "nsTextFormatter.h"
#include "nsThreadUtils.h"
#include "WebAuthnEnumStrings.h"
#include "WebAuthnService.h"
#include "WebAuthnTransportIdentifiers.h"

namespace mozilla::dom {

already_AddRefed<nsIWebAuthnService> NewWebAuthnService() {
  nsCOMPtr<nsIWebAuthnService> webauthnService(new WebAuthnService());
  return webauthnService.forget();
}

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
            StaticPrefs::
                security_webauth_webauthn_testing_allow_direct_attestation());
      }));
#else
  nsCOMPtr<nsIRunnable> runnable(NS_NewRunnableFunction(
      __func__, [self, aOrigin, aTransactionId, aBrowsingContextId]() {
        if (StaticPrefs::
                security_webauth_webauthn_testing_allow_direct_attestation()) {
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
  MOZ_ASSERT(aArgs);
  MOZ_ASSERT(aPromise);

  auto guard = mTransactionState.Lock();
  ResetLocked(guard);
  *guard = Some(TransactionState{.service = DefaultService(),
                                 .transactionId = aTransactionId,
                                 .parentRegisterPromise = Some(aPromise)});

  // We may need to show an attestation consent prompt before we return a
  // credential to WebAuthnTransactionParent, so we insert a new promise that
  // chains to `aPromise` here.

  nsString attestation;
  Unused << aArgs->GetAttestationConveyancePreference(attestation);
  bool attestationRequested = !attestation.EqualsLiteral(
      MOZ_WEBAUTHN_ATTESTATION_CONVEYANCE_PREFERENCE_NONE);

  nsString origin;
  Unused << aArgs->GetOrigin(origin);

  RefPtr<WebAuthnRegisterPromiseHolder> promiseHolder =
      new WebAuthnRegisterPromiseHolder(GetCurrentSerialEventTarget());

  RefPtr<WebAuthnService> self = this;
  RefPtr<WebAuthnRegisterPromise> promise = promiseHolder->Ensure();
  promise
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self, origin, aTransactionId, aBrowsingContextId,
           attestationRequested](
              const WebAuthnRegisterPromise::ResolveOrRejectValue& aValue) {
            auto guard = self->mTransactionState.Lock();
            if (guard->isNothing()) {
              return;
            }
            MOZ_ASSERT(guard->ref().parentRegisterPromise.isSome());
            MOZ_ASSERT(guard->ref().registerResult.isNothing());
            MOZ_ASSERT(guard->ref().childRegisterRequest.Exists());

            guard->ref().childRegisterRequest.Complete();

            if (aValue.IsReject()) {
              guard->ref().parentRegisterPromise.ref()->Reject(
                  aValue.RejectValue());
              guard->reset();
              return;
            }

            nsIWebAuthnRegisterResult* result = aValue.ResolveValue();
            // If the RP requested attestation, we need to show a consent prompt
            // before returning any identifying information. The platform may
            // have already done this for us, so we need to inspect the
            // attestation object at this point.
            bool resultIsIdentifying = true;
            Unused << result->HasIdentifyingAttestation(&resultIsIdentifying);
            if (attestationRequested && resultIsIdentifying) {
              guard->ref().registerResult = Some(result);
              self->ShowAttestationConsentPrompt(origin, aTransactionId,
                                                 aBrowsingContextId);
              return;
            }
            result->Anonymize();
            guard->ref().parentRegisterPromise.ref()->Resolve(result);
            guard->reset();
          })
      ->Track(guard->ref().childRegisterRequest);

  nsresult rv = guard->ref().service->MakeCredential(
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
  MOZ_ASSERT(aArgs);
  MOZ_ASSERT(aPromise);

  auto guard = mTransactionState.Lock();
  ResetLocked(guard);
  *guard = Some(TransactionState{.service = DefaultService(),
                                 .transactionId = aTransactionId});
  nsresult rv;

#if defined(XP_MACOSX)
  if (__builtin_available(macos 14.5, *)) {
    // This branch is intentionally empty; using `!__builtin_available(...)`
    // results in a compiler warning.
  } else {
    // On macOS < 14.5 the security key API doesn't handle the AppID extension.
    // So we'll use authenticator-rs if it's likely that the request requires
    // AppID. We consider it likely if 1) the AppID extension is present, 2)
    // the allow list is non-empty, and 3) none of the allowed credentials use
    // the "internal" or "hybrid" transport.
    nsString appId;
    rv = aArgs->GetAppId(appId);
    if (rv == NS_OK) {  // AppID is set
      uint8_t transportSet = 0;
      nsTArray<uint8_t> allowListTransports;
      Unused << aArgs->GetAllowListTransports(allowListTransports);
      for (const uint8_t& transport : allowListTransports) {
        transportSet |= transport;
      }
      uint8_t passkeyTransportMask =
          MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_INTERNAL |
          MOZ_WEBAUTHN_AUTHENTICATOR_TRANSPORT_ID_HYBRID;
      if (allowListTransports.Length() > 0 &&
          (transportSet & passkeyTransportMask) == 0) {
        guard->ref().service = AuthrsService();
      }
    }
  }
#endif

  rv = guard->ref().service->GetAssertion(aTransactionId, aBrowsingContextId,
                                          aArgs, aPromise);
  if (NS_FAILED(rv)) {
    return rv;
  }

  // If this is a conditionally mediated request, notify observers that there
  // is a pending transaction. This is mainly useful in tests.
  bool conditionallyMediated;
  Unused << aArgs->GetConditionallyMediated(&conditionallyMediated);
  if (conditionallyMediated) {
    nsCOMPtr<nsIRunnable> runnable(NS_NewRunnableFunction(__func__, []() {
      nsCOMPtr<nsIObserverService> os = mozilla::services::GetObserverService();
      if (os) {
        os->NotifyObservers(nullptr, "webauthn:conditional-get-pending",
                            nullptr);
      }
    }));
    NS_DispatchToMainThread(runnable.forget());
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
  return SelectedService()->HasPendingConditionalGet(aBrowsingContextId,
                                                     aOrigin, aRv);
}

NS_IMETHODIMP
WebAuthnService::GetAutoFillEntries(
    uint64_t aTransactionId, nsTArray<RefPtr<nsIWebAuthnAutoFillEntry>>& aRv) {
  return SelectedService()->GetAutoFillEntries(aTransactionId, aRv);
}

NS_IMETHODIMP
WebAuthnService::SelectAutoFillEntry(uint64_t aTransactionId,
                                     const nsTArray<uint8_t>& aCredentialId) {
  return SelectedService()->SelectAutoFillEntry(aTransactionId, aCredentialId);
}

NS_IMETHODIMP
WebAuthnService::ResumeConditionalGet(uint64_t aTransactionId) {
  return SelectedService()->ResumeConditionalGet(aTransactionId);
}

void WebAuthnService::ResetLocked(
    const TransactionStateMutex::AutoLock& aGuard) {
  if (aGuard->isSome()) {
    aGuard->ref().childRegisterRequest.DisconnectIfExists();
    if (aGuard->ref().parentRegisterPromise.isSome()) {
      aGuard->ref().parentRegisterPromise.ref()->Reject(NS_ERROR_DOM_ABORT_ERR);
    }
    aGuard->ref().service->Reset();
  }
  aGuard->reset();
}

NS_IMETHODIMP
WebAuthnService::Reset() {
  auto guard = mTransactionState.Lock();
  ResetLocked(guard);
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnService::Cancel(uint64_t aTransactionId) {
  return SelectedService()->Cancel(aTransactionId);
}

NS_IMETHODIMP
WebAuthnService::PinCallback(uint64_t aTransactionId, const nsACString& aPin) {
  return SelectedService()->PinCallback(aTransactionId, aPin);
}

NS_IMETHODIMP
WebAuthnService::SetHasAttestationConsent(uint64_t aTransactionId,
                                          bool aHasConsent) {
  auto guard = this->mTransactionState.Lock();
  if (guard->isNothing() || guard->ref().transactionId != aTransactionId) {
    // This could happen if the transaction was reset just when the prompt was
    // receiving user input.
    return NS_OK;
  }

  MOZ_ASSERT(guard->ref().parentRegisterPromise.isSome());
  MOZ_ASSERT(guard->ref().registerResult.isSome());
  MOZ_ASSERT(!guard->ref().childRegisterRequest.Exists());

  if (!aHasConsent) {
    guard->ref().registerResult.ref()->Anonymize();
  }
  guard->ref().parentRegisterPromise.ref()->Resolve(
      guard->ref().registerResult.ref());

  guard->reset();
  return NS_OK;
}

NS_IMETHODIMP
WebAuthnService::SelectionCallback(uint64_t aTransactionId, uint64_t aIndex) {
  return SelectedService()->SelectionCallback(aTransactionId, aIndex);
}

NS_IMETHODIMP
WebAuthnService::AddVirtualAuthenticator(
    const nsACString& protocol, const nsACString& transport,
    bool hasResidentKey, bool hasUserVerification, bool isUserConsenting,
    bool isUserVerified, uint64_t* retval) {
  return SelectedService()->AddVirtualAuthenticator(
      protocol, transport, hasResidentKey, hasUserVerification,
      isUserConsenting, isUserVerified, retval);
}

NS_IMETHODIMP
WebAuthnService::RemoveVirtualAuthenticator(uint64_t authenticatorId) {
  return SelectedService()->RemoveVirtualAuthenticator(authenticatorId);
}

NS_IMETHODIMP
WebAuthnService::AddCredential(uint64_t authenticatorId,
                               const nsACString& credentialId,
                               bool isResidentCredential,
                               const nsACString& rpId,
                               const nsACString& privateKey,
                               const nsACString& userHandle,
                               uint32_t signCount) {
  return SelectedService()->AddCredential(authenticatorId, credentialId,
                                          isResidentCredential, rpId,
                                          privateKey, userHandle, signCount);
}

NS_IMETHODIMP
WebAuthnService::GetCredentials(
    uint64_t authenticatorId,
    nsTArray<RefPtr<nsICredentialParameters>>& retval) {
  return SelectedService()->GetCredentials(authenticatorId, retval);
}

NS_IMETHODIMP
WebAuthnService::RemoveCredential(uint64_t authenticatorId,
                                  const nsACString& credentialId) {
  return SelectedService()->RemoveCredential(authenticatorId, credentialId);
}

NS_IMETHODIMP
WebAuthnService::RemoveAllCredentials(uint64_t authenticatorId) {
  return SelectedService()->RemoveAllCredentials(authenticatorId);
}

NS_IMETHODIMP
WebAuthnService::SetUserVerified(uint64_t authenticatorId,
                                 bool isUserVerified) {
  return SelectedService()->SetUserVerified(authenticatorId, isUserVerified);
}

NS_IMETHODIMP
WebAuthnService::Listen() { return SelectedService()->Listen(); }

NS_IMETHODIMP
WebAuthnService::RunCommand(const nsACString& cmd) {
  return SelectedService()->RunCommand(cmd);
}

}  // namespace mozilla::dom
