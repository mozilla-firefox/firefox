/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Base64.h"
#include "mozilla/JSONStringWriteFuncs.h"
#include "mozilla/JSONWriter.h"
#include "mozilla/StaticPrefs_security.h"
#include "mozilla/dom/PWindowGlobalParent.h"
#include "mozilla/dom/WebAuthnTransactionParent.h"
#include "mozilla/dom/WindowGlobalParent.h"

#include "nsThreadUtils.h"
#include "WebAuthnArgs.h"
#include "WebAuthnUtil.h"

namespace mozilla::dom {

nsresult AssembleClientData(WindowGlobalParent* aManager,
                            const nsACString& aType,
                            const nsTArray<uint8_t>& aChallenge,
                            /* out */ nsACString& aJsonOut) {
  nsAutoCString challengeBase64;
  nsresult rv =
      Base64URLEncode(aChallenge.Length(), aChallenge.Elements(),
                      Base64URLEncodePaddingPolicy::Omit, challengeBase64);
  if (NS_FAILED(rv)) {
    return NS_ERROR_FAILURE;
  }

  nsIPrincipal* principal = aManager->DocumentPrincipal();
  nsIPrincipal* topPrincipal =
      aManager->TopWindowContext()->DocumentPrincipal();

  nsCString origin;
  rv = principal->GetWebExposedOriginSerialization(origin);
  if (NS_FAILED(rv)) {
    return NS_ERROR_FAILURE;
  }

  bool crossOrigin = !principal->Equals(topPrincipal);

  // Serialize the collected client data using the algorithm from
  // https://www.w3.org/TR/webauthn-3/#clientdatajson-serialization.
  // Please update the definition of CollectedClientData in
  // dom/webidl/WebAuthentication.webidl when changes are made here.
  JSONStringRefWriteFunc f(aJsonOut);
  JSONWriter w(f, JSONWriter::CollectionStyle::SingleLineStyle);
  w.Start();
  // Steps 2 and 3
  w.StringProperty("type", aType);
  // Steps 4 and 5
  w.StringProperty("challenge", challengeBase64);
  // Steps 6 and 7
  w.StringProperty("origin", origin);
  // Steps 8 - 10
  w.BoolProperty("crossOrigin", crossOrigin);
  // Step 11. The description of the algorithm says "If topOrigin is present",
  // but the definition of topOrigin says that topOrigin "is set only if [...]
  // crossOrigin is true." so we use the latter condition instead.
  if (crossOrigin) {
    nsCString topOrigin;
    rv = topPrincipal->GetWebExposedOriginSerialization(topOrigin);
    if (NS_FAILED(rv)) {
      return NS_ERROR_FAILURE;
    }
    w.StringProperty("topOrigin", topOrigin);
  }
  w.End();

  return NS_OK;
}

bool GetAssertionRequestIncludesLargeBlobRead(
    const WebAuthnGetAssertionInfo& aInfo) {
  for (const WebAuthnExtension& ext : aInfo.Extensions()) {
    if (ext.type() == WebAuthnExtension::TWebAuthnExtensionLargeBlob) {
      if (ext.get_WebAuthnExtensionLargeBlob().flag().isSome()) {
        return ext.get_WebAuthnExtensionLargeBlob().flag().ref();
      }
    }
  }
  return false;
}

bool MakeCredentialRequestIncludesPrfExtension(
    const WebAuthnMakeCredentialInfo& aInfo) {
  for (const WebAuthnExtension& ext : aInfo.Extensions()) {
    if (ext.type() == WebAuthnExtension::TWebAuthnExtensionPrf) {
      return true;
    }
  }
  return false;
}

void WebAuthnTransactionParent::CompleteTransaction() {
  if (mTransactionId.isSome()) {
    if (mRegisterPromiseRequest.Exists()) {
      mRegisterPromiseRequest.Complete();
    }
    if (mSignPromiseRequest.Exists()) {
      mSignPromiseRequest.Complete();
    }
    if (mWebAuthnService) {
      // We have to do this to work around Bug 1864526.
      mWebAuthnService->Cancel(mTransactionId.ref());
    }
    mTransactionId.reset();
  }
}

void WebAuthnTransactionParent::DisconnectTransaction() {
  mTransactionId.reset();
  mRegisterPromiseRequest.DisconnectIfExists();
  mSignPromiseRequest.DisconnectIfExists();
  if (mWebAuthnService) {
    mWebAuthnService->Reset();
  }
}

mozilla::ipc::IPCResult WebAuthnTransactionParent::RecvRequestRegister(
    const WebAuthnMakeCredentialInfo& aTransactionInfo,
    RequestRegisterResolver&& aResolver) {
  MOZ_ASSERT(NS_IsMainThread());

  if (!mWebAuthnService) {
    mWebAuthnService = do_GetService("@mozilla.org/webauthn/service;1");
    if (!mWebAuthnService) {
      aResolver(NS_ERROR_NOT_AVAILABLE);
      return IPC_OK();
    }
  }

  // If there's an ongoing transaction, abort it.
  if (mTransactionId.isSome()) {
    DisconnectTransaction();
  }
  uint64_t aTransactionId = NextId();
  mTransactionId = Some(aTransactionId);

  WindowGlobalParent* manager = static_cast<WindowGlobalParent*>(Manager());
  nsIPrincipal* principal = manager->DocumentPrincipal();

  if (!IsWebAuthnAllowedForPrincipal(principal)) {
    aResolver(NS_ERROR_DOM_SECURITY_ERR);
    return IPC_OK();
  }

  if (!IsValidRpId(principal, aTransactionInfo.RpId())) {
    aResolver(NS_ERROR_DOM_SECURITY_ERR);
    return IPC_OK();
  }

  nsCString origin;
  nsresult rv = principal->GetWebExposedOriginSerialization(origin);
  if (NS_FAILED(rv)) {
    aResolver(NS_ERROR_FAILURE);
    return IPC_OK();
  }

  nsCString clientDataJSON;
  rv = AssembleClientData(manager, "webauthn.create"_ns,
                          aTransactionInfo.Challenge(), clientDataJSON);
  if (NS_FAILED(rv)) {
    aResolver(NS_ERROR_FAILURE);
    return IPC_OK();
  }

  bool requestIncludesPrfExtension =
      MakeCredentialRequestIncludesPrfExtension(aTransactionInfo);

  RefPtr<WebAuthnRegisterPromiseHolder> promiseHolder =
      new WebAuthnRegisterPromiseHolder(GetCurrentSerialEventTarget());

  RefPtr<WebAuthnRegisterPromise> promise = promiseHolder->Ensure();
  promise
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self = RefPtr{this}, inputClientData = clientDataJSON,
           requestIncludesPrfExtension, resolver = std::move(aResolver)](
              const WebAuthnRegisterPromise::ResolveOrRejectValue& aValue) {
            self->CompleteTransaction();

            if (aValue.IsReject()) {
              resolver(aValue.RejectValue());
              return;
            }

            auto rejectWithNotAllowed = MakeScopeExit(
                [&]() { resolver(NS_ERROR_DOM_NOT_ALLOWED_ERR); });

            RefPtr<nsIWebAuthnRegisterResult> registerResult =
                aValue.ResolveValue();

            nsCString clientData;
            nsresult rv = registerResult->GetClientDataJSON(clientData);
            if (rv == NS_ERROR_NOT_AVAILABLE) {
              clientData = inputClientData;
            } else if (NS_FAILED(rv)) {
              return;
            }

            nsTArray<uint8_t> attObj;
            rv = registerResult->GetAttestationObject(attObj);
            if (NS_WARN_IF(NS_FAILED(rv))) {
              return;
            }

            nsTArray<uint8_t> credentialId;
            rv = registerResult->GetCredentialId(credentialId);
            if (NS_WARN_IF(NS_FAILED(rv))) {
              return;
            }

            nsTArray<nsString> transports;
            rv = registerResult->GetTransports(transports);
            if (NS_WARN_IF(NS_FAILED(rv))) {
              return;
            }

            Maybe<nsString> authenticatorAttachment;
            nsString maybeAuthenticatorAttachment;
            rv = registerResult->GetAuthenticatorAttachment(
                maybeAuthenticatorAttachment);
            if (rv != NS_ERROR_NOT_AVAILABLE) {
              if (NS_WARN_IF(NS_FAILED(rv))) {
                return;
              }
              authenticatorAttachment = Some(maybeAuthenticatorAttachment);
            }

            nsTArray<WebAuthnExtensionResult> extensions;
            bool credPropsRk;
            rv = registerResult->GetCredPropsRk(&credPropsRk);
            if (rv != NS_ERROR_NOT_AVAILABLE) {
              if (NS_WARN_IF(NS_FAILED(rv))) {
                return;
              }
              extensions.AppendElement(
                  WebAuthnExtensionResultCredProps(credPropsRk));
            }

            bool hmacCreateSecret;
            rv = registerResult->GetHmacCreateSecret(&hmacCreateSecret);
            if (rv != NS_ERROR_NOT_AVAILABLE) {
              if (NS_WARN_IF(NS_FAILED(rv))) {
                return;
              }
              extensions.AppendElement(
                  WebAuthnExtensionResultHmacSecret(hmacCreateSecret));
            }

            bool largeBlobSupported;
            rv = registerResult->GetLargeBlobSupported(&largeBlobSupported);
            if (rv != NS_ERROR_NOT_AVAILABLE) {
              if (NS_FAILED(rv)) {
                return;
              }
              nsTArray<uint8_t> blob;  // unused
              extensions.AppendElement(WebAuthnExtensionResultLargeBlob(
                  largeBlobSupported, blob, false));
            }

            if (requestIncludesPrfExtension) {
              Maybe<WebAuthnExtensionPrfValues> prfResults = Nothing();

              bool prfEnabled = false;
              rv = registerResult->GetPrfEnabled(&prfEnabled);
              if (rv != NS_ERROR_NOT_AVAILABLE && NS_FAILED(rv)) {
                return;
              }

              nsTArray<uint8_t> prfResultsFirst;
              rv = registerResult->GetPrfResultsFirst(prfResultsFirst);
              if (rv != NS_ERROR_NOT_AVAILABLE) {
                if (NS_WARN_IF(NS_FAILED(rv))) {
                  return;
                }

                bool prfResultsSecondMaybe = false;
                nsTArray<uint8_t> prfResultsSecond;
                rv = registerResult->GetPrfResultsSecond(prfResultsSecond);
                if (rv != NS_ERROR_NOT_AVAILABLE) {
                  if (NS_WARN_IF(NS_FAILED(rv))) {
                    return;
                  }
                  prfResultsSecondMaybe = true;
                }

                prfResults = Some(WebAuthnExtensionPrfValues(
                    prfResultsFirst, prfResultsSecondMaybe, prfResultsSecond));
              }

              extensions.AppendElement(
                  WebAuthnExtensionResultPrf(Some(prfEnabled), prfResults));
            }

            WebAuthnMakeCredentialResult result(
                clientData, attObj, credentialId, transports, extensions,
                authenticatorAttachment);

            rejectWithNotAllowed.release();
            resolver(result);
          })
      ->Track(mRegisterPromiseRequest);

  uint64_t browsingContextId = manager->GetBrowsingContext()->Top()->Id();
  bool privateBrowsing = principal->GetIsInPrivateBrowsing();
  auto args = MakeRefPtr<WebAuthnRegisterArgs>(
      origin, clientDataJSON, privateBrowsing, aTransactionInfo);
  rv = mWebAuthnService->MakeCredential(aTransactionId, browsingContextId, args,
                                        promiseHolder);
  if (NS_FAILED(rv)) {
    promiseHolder->Reject(NS_ERROR_DOM_NOT_ALLOWED_ERR);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult WebAuthnTransactionParent::RecvRequestSign(
    const WebAuthnGetAssertionInfo& aTransactionInfo,
    RequestSignResolver&& aResolver) {
  MOZ_ASSERT(NS_IsMainThread());

  if (!mWebAuthnService) {
    mWebAuthnService = do_GetService("@mozilla.org/webauthn/service;1");
    if (!mWebAuthnService) {
      aResolver(NS_ERROR_NOT_AVAILABLE);
      return IPC_OK();
    }
  }

  if (mTransactionId.isSome()) {
    DisconnectTransaction();
  }
  uint64_t transactionId = NextId();
  mTransactionId = Some(transactionId);

  WindowGlobalParent* manager = static_cast<WindowGlobalParent*>(Manager());
  nsIPrincipal* principal = manager->DocumentPrincipal();

  if (!IsWebAuthnAllowedForPrincipal(principal)) {
    aResolver(NS_ERROR_DOM_SECURITY_ERR);
    return IPC_OK();
  }

  if (!IsValidRpId(principal, aTransactionInfo.RpId())) {
    aResolver(NS_ERROR_DOM_SECURITY_ERR);
    return IPC_OK();
  }

  if (aTransactionInfo.AppId().isSome() &&
      !IsValidAppId(principal, aTransactionInfo.AppId().ref())) {
    aResolver(NS_ERROR_DOM_SECURITY_ERR);
    return IPC_OK();
  }

  nsCString origin;
  nsresult rv = principal->GetWebExposedOriginSerialization(origin);
  if (NS_FAILED(rv)) {
    aResolver(NS_ERROR_FAILURE);
    return IPC_OK();
  }

  nsCString clientDataJSON;
  rv = AssembleClientData(manager, "webauthn.get"_ns,
                          aTransactionInfo.Challenge(), clientDataJSON);
  if (NS_FAILED(rv)) {
    aResolver(NS_ERROR_FAILURE);
    return IPC_OK();
  }

  bool requestIncludesAppId = aTransactionInfo.AppId().isSome();

  bool requestIncludesLargeBlobRead =
      GetAssertionRequestIncludesLargeBlobRead(aTransactionInfo);

  RefPtr<WebAuthnSignPromiseHolder> promiseHolder =
      new WebAuthnSignPromiseHolder(GetCurrentSerialEventTarget());

  RefPtr<WebAuthnSignPromise> promise = promiseHolder->Ensure();
  promise
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self = RefPtr{this}, inputClientData = clientDataJSON,
           requestIncludesAppId, requestIncludesLargeBlobRead,
           resolver = std::move(aResolver)](
              const WebAuthnSignPromise::ResolveOrRejectValue& aValue) {
            self->CompleteTransaction();

            if (aValue.IsReject()) {
              resolver(aValue.RejectValue());
              return;
            }

            auto rejectWithNotAllowed = MakeScopeExit(
                [&]() { resolver(NS_ERROR_DOM_NOT_ALLOWED_ERR); });

            RefPtr<nsIWebAuthnSignResult> signResult = aValue.ResolveValue();

            nsCString clientData;
            nsresult rv = signResult->GetClientDataJSON(clientData);
            if (rv == NS_ERROR_NOT_AVAILABLE) {
              clientData = inputClientData;
            } else if (NS_FAILED(rv)) {
              return;
            }

            nsTArray<uint8_t> credentialId;
            rv = signResult->GetCredentialId(credentialId);
            if (NS_WARN_IF(NS_FAILED(rv))) {
              return;
            }

            nsTArray<uint8_t> signature;
            rv = signResult->GetSignature(signature);
            if (NS_WARN_IF(NS_FAILED(rv))) {
              return;
            }

            nsTArray<uint8_t> authenticatorData;
            rv = signResult->GetAuthenticatorData(authenticatorData);
            if (NS_WARN_IF(NS_FAILED(rv))) {
              return;
            }

            nsTArray<uint8_t> userHandle;
            Unused << signResult->GetUserHandle(userHandle);  // optional

            Maybe<nsString> authenticatorAttachment;
            nsString maybeAuthenticatorAttachment;
            rv = signResult->GetAuthenticatorAttachment(
                maybeAuthenticatorAttachment);
            if (rv != NS_ERROR_NOT_AVAILABLE) {
              if (NS_WARN_IF(NS_FAILED(rv))) {
                return;
              }
              authenticatorAttachment = Some(maybeAuthenticatorAttachment);
            }

            nsTArray<WebAuthnExtensionResult> extensions;
            if (requestIncludesAppId) {
              bool usedAppId = false;
              rv = signResult->GetUsedAppId(&usedAppId);
              if (rv != NS_ERROR_NOT_AVAILABLE && NS_FAILED(rv)) {
                return;
              }
              extensions.AppendElement(WebAuthnExtensionResultAppId(usedAppId));
            }

            nsTArray<uint8_t> largeBlobValue;
            rv = signResult->GetLargeBlobValue(largeBlobValue);
            if (rv != NS_ERROR_NOT_AVAILABLE) {
              if (NS_FAILED(rv)) {
                return;
              }
              extensions.AppendElement(WebAuthnExtensionResultLargeBlob(
                  true, largeBlobValue, false));
            } else if (requestIncludesLargeBlobRead) {
              // Signal a read error by setting both flags.
              extensions.AppendElement(
                  WebAuthnExtensionResultLargeBlob(true, largeBlobValue, true));
            } else {
              // Read and write operations are mutually exclusive, so we only
              // check for a write result if the read result is not available.
              bool largeBlobWritten;
              rv = signResult->GetLargeBlobWritten(&largeBlobWritten);
              if (rv != NS_ERROR_NOT_AVAILABLE) {
                if (NS_FAILED(rv)) {
                  return;
                }
                extensions.AppendElement(WebAuthnExtensionResultLargeBlob(
                    false, largeBlobValue, largeBlobWritten));
              }
            }

            {
              Maybe<WebAuthnExtensionPrfValues> prfResults;
              bool prfMaybe = false;
              rv = signResult->GetPrfMaybe(&prfMaybe);
              if (rv == NS_OK && prfMaybe) {
                nsTArray<uint8_t> prfResultsFirst;
                rv = signResult->GetPrfResultsFirst(prfResultsFirst);
                if (rv != NS_ERROR_NOT_AVAILABLE) {
                  if (NS_WARN_IF(NS_FAILED(rv))) {
                    return;
                  }

                  bool prfResultsSecondMaybe = false;
                  nsTArray<uint8_t> prfResultsSecond;
                  rv = signResult->GetPrfResultsSecond(prfResultsSecond);
                  if (rv != NS_ERROR_NOT_AVAILABLE) {
                    if (NS_WARN_IF(NS_FAILED(rv))) {
                      return;
                    }
                    prfResultsSecondMaybe = true;
                  }

                  prfResults = Some(WebAuthnExtensionPrfValues(
                      prfResultsFirst, prfResultsSecondMaybe,
                      prfResultsSecond));
                } else {
                  prfResults = Nothing();
                }

                extensions.AppendElement(
                    WebAuthnExtensionResultPrf(Nothing(), prfResults));
              }
            }

            WebAuthnGetAssertionResult result(
                clientData, credentialId, signature, authenticatorData,
                extensions, userHandle, authenticatorAttachment);

            rejectWithNotAllowed.release();
            resolver(result);
          })
      ->Track(mSignPromiseRequest);

  uint64_t browsingContextId = manager->GetBrowsingContext()->Top()->Id();
  bool privateBrowsing = principal->GetIsInPrivateBrowsing();
  auto args = MakeRefPtr<WebAuthnSignArgs>(origin, clientDataJSON,
                                           privateBrowsing, aTransactionInfo);
  rv = mWebAuthnService->GetAssertion(transactionId, browsingContextId, args,
                                      promiseHolder);
  if (NS_FAILED(rv)) {
    promiseHolder->Reject(NS_ERROR_DOM_NOT_ALLOWED_ERR);
  }

  return IPC_OK();
}

mozilla::ipc::IPCResult WebAuthnTransactionParent::RecvRequestCancel() {
  MOZ_ASSERT(NS_IsMainThread());

  if (mTransactionId.isNothing()) {
    return IPC_OK();
  }

  DisconnectTransaction();
  return IPC_OK();
}

mozilla::ipc::IPCResult WebAuthnTransactionParent::RecvRequestIsUVPAA(
    RequestIsUVPAAResolver&& aResolver) {
  MOZ_ASSERT(NS_IsMainThread());

#ifdef MOZ_WIDGET_ANDROID
  // Try the nsIWebAuthnService. If we're configured for tests we
  // will get a result. Otherwise we expect NS_ERROR_NOT_IMPLEMENTED.
  nsCOMPtr<nsIWebAuthnService> service(
      do_GetService("@mozilla.org/webauthn/service;1"));
  bool available;
  nsresult rv = service->GetIsUVPAA(&available);
  if (NS_SUCCEEDED(rv)) {
    aResolver(available);
    return IPC_OK();
  }

  // Don't consult the platform API if resident key support is disabled.
  if (!StaticPrefs::
          security_webauthn_webauthn_enable_android_fido2_residentkey()) {
    aResolver(false);
    return IPC_OK();
  }

  // The GeckoView implementation of
  // isUserVerifiyingPlatformAuthenticatorAvailable dispatches the work to a
  // background thread and returns a MozPromise which we can ->Then to call
  // aResolver on the current thread.
  nsCOMPtr<nsISerialEventTarget> target = GetCurrentSerialEventTarget();
  auto result = java::WebAuthnTokenManager::
      WebAuthnIsUserVerifyingPlatformAuthenticatorAvailable();
  auto geckoResult = java::GeckoResult::LocalRef(std::move(result));
  MozPromise<bool, bool, false>::FromGeckoResult(geckoResult)
      ->Then(target, __func__,
             [resolver = std::move(aResolver)](
                 const MozPromise<bool, bool, false>::ResolveOrRejectValue&
                     aValue) {
               if (aValue.IsResolve()) {
                 resolver(aValue.ResolveValue());
               } else {
                 resolver(false);
               }
             });
  return IPC_OK();

#else

  nsCOMPtr<nsISerialEventTarget> target = GetCurrentSerialEventTarget();
  nsCOMPtr<nsIRunnable> runnable(NS_NewRunnableFunction(
      __func__, [target, resolver = std::move(aResolver)]() {
        bool available;
        nsCOMPtr<nsIWebAuthnService> service(
            do_GetService("@mozilla.org/webauthn/service;1"));
        nsresult rv = service->GetIsUVPAA(&available);
        if (NS_FAILED(rv)) {
          available = false;
        }
        BoolPromise::CreateAndResolve(available, __func__)
            ->Then(target, __func__,
                   [resolver](const BoolPromise::ResolveOrRejectValue& value) {
                     if (value.IsResolve()) {
                       resolver(value.ResolveValue());
                     } else {
                       resolver(false);
                     }
                   });
      }));
  NS_DispatchBackgroundTask(runnable.forget(), NS_DISPATCH_EVENT_MAY_BLOCK);
  return IPC_OK();
#endif
}

void WebAuthnTransactionParent::ActorDestroy(ActorDestroyReason aWhy) {
  // Called either by Send__delete__() in RecvDestroyMe() above, or when
  // the channel disconnects. Ensure the token manager forgets about us.
  MOZ_ASSERT(NS_IsMainThread());

  if (mTransactionId.isSome()) {
    DisconnectTransaction();
  }
}

}  // namespace mozilla::dom
