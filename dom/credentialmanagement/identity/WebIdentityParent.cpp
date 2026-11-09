/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/WebIdentityParent.h"

#include "mozilla/Components.h"
#include "mozilla/IdentityCredentialRequestManager.h"
#include "mozilla/dom/IdentityNetworkHelpers.h"
#include "mozilla/dom/NavigatorLogin.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "nsIEffectiveTLDService.h"
#include "nsIIdentityCredentialPromptService.h"
#include "nsIIdentityCredentialStorageService.h"
#include "nsIXPConnect.h"
#include "nsScriptSecurityManager.h"
#include "nsURLHelper.h"

namespace mozilla::dom {

void WebIdentityParent::ActorDestroy(ActorDestroyReason aWhy) {
  MOZ_ASSERT(NS_IsMainThread());
}

mozilla::ipc::IPCResult WebIdentityParent::RecvRequestCancel() {
  MOZ_ASSERT(NS_IsMainThread());
  return IPC_OK();
}

mozilla::ipc::IPCResult WebIdentityParent::RecvGetIdentityCredential(
    IdentityCredentialRequestOptions&& aOptions,
    const CredentialMediationRequirement& aMediationRequirement,
    bool aHasUserActivation, const GetIdentityCredentialResolver& aResolver) {
  WindowGlobalParent* manager =
      mozilla::ipc::ActorCast<WindowGlobalParent>(Manager());
  if (!manager) {
    aResolver(NS_ERROR_FAILURE);
    return IPC_OK();
  }
  identity::GetCredentialInMainProcess(
      manager->DocumentPrincipal(), this, std::move(aOptions),
      aMediationRequirement, aHasUserActivation)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aResolver](const IPCIdentityCredential& aResult) {
            return aResolver(aResult);
          },
          [aResolver](nsresult aErr) { aResolver(aErr); });
  return IPC_OK();
}

mozilla::ipc::IPCResult WebIdentityParent::RecvDisconnectIdentityCredential(
    const IdentityCredentialDisconnectOptions& aOptions,
    const DisconnectIdentityCredentialResolver& aResolver) {
  WindowGlobalParent* manager =
      mozilla::ipc::ActorCast<WindowGlobalParent>(Manager());
  if (!manager) {
    aResolver(NS_ERROR_FAILURE);
    return IPC_OK();
  }
  identity::DisconnectInMainProcess(manager->DocumentPrincipal(), aOptions)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aResolver](const bool& aResult) { aResolver(NS_OK); },
          [aResolver](nsresult aErr) { aResolver(aErr); });
  return IPC_OK();
}

mozilla::ipc::IPCResult WebIdentityParent::RecvPreventSilentAccess(
    const PreventSilentAccessResolver& aResolver) {
  WindowGlobalParent* manager =
      mozilla::ipc::ActorCast<WindowGlobalParent>(Manager());
  if (!manager) {
    aResolver(NS_ERROR_FAILURE);
    return IPC_OK();
  }
  nsIPrincipal* principal = manager->DocumentPrincipal();
  if (principal) {
    nsCOMPtr<nsIPermissionManager> permissionManager =
        components::PermissionManager::Service();
    if (permissionManager) {
      permissionManager->RemoveFromPrincipal(
          principal, "credential-allow-silent-access"_ns);
      aResolver(NS_OK);
      return IPC_OK();
    }
  }

  aResolver(NS_ERROR_NOT_AVAILABLE);
  return IPC_OK();
}

mozilla::ipc::IPCResult WebIdentityParent::RecvSetLoginStatus(
    LoginStatus aStatus, const SetLoginStatusResolver& aResolver) {
  WindowGlobalParent* manager =
      mozilla::ipc::ActorCast<WindowGlobalParent>(Manager());
  if (!manager) {
    aResolver(NS_ERROR_FAILURE);
    return IPC_OK();
  }
  nsIPrincipal* principal = manager->DocumentPrincipal();
  if (!principal) {
    aResolver(NS_ERROR_DOM_NOT_ALLOWED_ERR);
    return IPC_OK();
  }
  nsresult rv = NavigatorLogin::SetLoginStatus(principal, aStatus);
  aResolver(rv);
  return IPC_OK();
}

mozilla::ipc::IPCResult WebIdentityParent::RecvResolveContinuationWindow(
    nsCString&& aToken, IdentityResolveOptions&& aOptions,
    const ResolveContinuationWindowResolver& aResolver) {
  // Pass the resolution on to the ICRM to handle it.
  // Faithfully convey its error in resolution.
  IdentityCredentialRequestManager* requestManager =
      IdentityCredentialRequestManager::GetInstance();
  if (!requestManager) {
    aResolver(NS_ERROR_NOT_AVAILABLE);
    return IPC_OK();
  }
  nsresult rv = requestManager->MaybeResolvePopup(this, aToken, aOptions);
  aResolver(rv);
  return IPC_OK();
}

mozilla::ipc::IPCResult WebIdentityParent::RecvIsActiveContinuationWindow(
    const IsActiveContinuationWindowResolver& aResolver) {
  IdentityCredentialRequestManager* requestManager =
      IdentityCredentialRequestManager::GetInstance();
  if (!requestManager) {
    aResolver(false);
    return IPC_OK();
  }
  aResolver(requestManager->IsActivePopup(this));
  return IPC_OK();
}

namespace identity {

nsresult CanSilentlyCollect(nsIPrincipal* aPrincipal,
                            nsIPrincipal* aIDPPrincipal, bool* aResult) {
  NS_ENSURE_ARG_POINTER(aPrincipal);
  NS_ENSURE_ARG_POINTER(aIDPPrincipal);
  nsCString origin;
  nsresult rv = aIDPPrincipal->GetOrigin(origin);
  NS_ENSURE_SUCCESS(rv, rv);

  uint32_t permit = nsIPermissionManager::UNKNOWN_ACTION;
  nsCOMPtr<nsIPermissionManager> permissionManager =
      components::PermissionManager::Service();
  if (!permissionManager) {
    return NS_ERROR_SERVICE_NOT_AVAILABLE;
  }

  rv = permissionManager->TestPermissionFromPrincipal(
      aPrincipal, "credential-allow-silent-access^"_ns + origin, &permit);
  NS_ENSURE_SUCCESS(rv, rv);
  *aResult = (permit == nsIPermissionManager::ALLOW_ACTION);
  if (!*aResult) {
    return NS_OK;
  }
  rv = permissionManager->TestPermissionFromPrincipal(
      aPrincipal, "credential-allow-silent-access"_ns, &permit);
  NS_ENSURE_SUCCESS(rv, rv);
  *aResult = permit == nsIPermissionManager::ALLOW_ACTION;
  return NS_OK;
}

// static
RefPtr<GetIPCIdentityCredentialPromise> GetCredentialInMainProcess(
    nsIPrincipal* aPrincipal, WebIdentityParent* aRelyingParty,
    IdentityCredentialRequestOptions&& aOptions,
    const CredentialMediationRequirement& aMediationRequirement,
    bool aHasUserActivation) {
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aRelyingParty);

  if (aOptions.mMode == IdentityCredentialRequestOptionsMode::Active) {
    // If the site is operating in "Active Mode" we need user activation  to
    // proceed.
    if (!aHasUserActivation) {
      return GetIPCIdentityCredentialPromise::CreateAndReject(
          NS_ERROR_DOM_NETWORK_ERR, __func__);
    }
  } else {
    // Otherwise we are in "Passive Mode" and since this doesn't require user
    // activation we constrain the credentials that are allowed to be be shown
    // to the user so they don't get annoyed.
    // Specifically, they need to have this credential registered for use on
    // this website.
    nsresult rv;
    nsCOMPtr<nsIIdentityCredentialStorageService> icStorageService =
        mozilla::components::IdentityCredentialStorageService::Service(&rv);
    if (NS_WARN_IF(!icStorageService)) {
      return GetIPCIdentityCredentialPromise::CreateAndReject(rv, __func__);
    }
    aOptions.mProviders.RemoveElementsBy(
        [icStorageService,
         aPrincipal](const IdentityProviderRequestOptions& provider) {
          nsCString configLocation = provider.mConfigURL;
          nsCOMPtr<nsIURI> configURI;
          nsresult rv = NS_NewURI(getter_AddRefs(configURI), configLocation);
          if (NS_FAILED(rv)) {
            return true;
          }
          bool thirdParty = true;
          rv = aPrincipal->IsThirdPartyURI(configURI, &thirdParty);
          if (!thirdParty) {
            return false;
          }
          nsCOMPtr<nsIPrincipal> idpPrincipal =
              BasePrincipal::CreateContentPrincipal(
                  configURI, aPrincipal->OriginAttributesRef());
          bool connected = false;
          rv =
              icStorageService->Connected(aPrincipal, idpPrincipal, &connected);
          if (NS_FAILED(rv)) {
            return true;
          }
          return !connected;
        });
  }

  if (aOptions.mProviders.IsEmpty()) {
    return GetIPCIdentityCredentialPromise::CreateAndReject(
        NS_ERROR_NOT_AVAILABLE, __func__);
  }

  RefPtr<GetIPCIdentityCredentialPromise::Private> result =
      new GetIPCIdentityCredentialPromise::Private(__func__);
  DiscoverFromExternalSourceInMainProcess(aPrincipal, aRelyingParty, aOptions,
                                          aMediationRequirement)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [result](const IPCIdentityCredential& credential) {
            result->Resolve(credential, __func__);
          },
          [result](nsresult rv) { result->Reject(rv, __func__); });
  return result.forget();
}

// static
RefPtr<GetIPCIdentityCredentialPromise> DiscoverFromExternalSourceInMainProcess(
    nsIPrincipal* aPrincipal, WebIdentityParent* aRelyingParty,
    const IdentityCredentialRequestOptions& aOptions,
    const CredentialMediationRequirement& aMediationRequirement) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aRelyingParty);

  // Make sure we have providers.
  if (aOptions.mProviders.Length() < 1) {
    return GetIPCIdentityCredentialPromise::CreateAndReject(
        NS_ERROR_DOM_NOT_ALLOWED_ERR, __func__);
  }

  nsCOMPtr<nsIPrincipal> principal(aPrincipal);
  RefPtr<CanonicalBrowsingContext> browsingContext =
      aRelyingParty->MaybeBrowsingContext();
  if (!browsingContext) {
    return GetIPCIdentityCredentialPromise::CreateAndReject(
        NS_ERROR_DOM_NOT_ALLOWED_ERR, __func__);
  }
  RefPtr<WebIdentityParent> relyingParty = aRelyingParty;

  RefPtr<GetIPCIdentityCredentialPromise::Private> result =
      new GetIPCIdentityCredentialPromise::Private(__func__);

  RefPtr<nsITimer> timeout;
  if (StaticPrefs::
          dom_security_credentialmanagement_identity_reject_delay_enabled()) {
    nsresult rv = NS_NewTimerWithCallback(
        getter_AddRefs(timeout),
        [=](auto) {
          result->Reject(NS_ERROR_DOM_NETWORK_ERR, __func__);
          CloseUserInterface(browsingContext);
        },
        StaticPrefs::
            dom_security_credentialmanagement_identity_reject_delay_duration_ms(),
        nsITimer::TYPE_ONE_SHOT, "IdentityCredentialTimeoutCallback"_ns);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      result->Reject(NS_ERROR_FAILURE, __func__);
      return result.forget();
    }
  }

  // Construct an array of requests to fetch manifests for every provider.
  // We need this to show their branding information
  nsTArray<RefPtr<GetManifestPromise>> manifestPromises;
  for (const IdentityProviderRequestOptions& provider : aOptions.mProviders) {
    RefPtr<GetManifestPromise> manifest = FetchManifest(principal, provider);
    manifestPromises.AppendElement(manifest);
  }

  // We use AllSettled here so that failures will be included- we use default
  // values there.
  GetManifestPromise::AllSettled(GetCurrentSerialEventTarget(),
                                 manifestPromises)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [browsingContext, aOptions](
              const GetManifestPromise::AllSettledPromiseType::ResolveValueType&
                  aResults) {
            // Convert the
            // GetManifestPromise::AllSettledPromiseType::ResolveValueType to a
            // Sequence<MozPromise>
            CopyableTArray<MozPromise<IdentityProviderAPIConfig, nsresult,
                                      true>::ResolveOrRejectValue>
                results = aResults;
            const Sequence<MozPromise<IdentityProviderAPIConfig, nsresult,
                                      true>::ResolveOrRejectValue>
                resultsSequence(std::move(results));

            // If we can skip the provider check, because there is only one
            // option and it is already linked, do so!
            Maybe<IdentityProviderRequestOptionsWithManifest>
                autoSelectedIdentityProvider =
                    SkipAccountChooser(aOptions.mProviders, resultsSequence);
            if (autoSelectedIdentityProvider.isSome()) {
              return GetIdentityProviderRequestOptionsWithManifestPromise::
                  CreateAndResolve(autoSelectedIdentityProvider.extract(),
                                   __func__);
            }

            // The user picks from the providers
            return PromptUserToSelectProvider(
                browsingContext, aOptions.mProviders, resultsSequence);
          },
          [](bool error) {
            return GetIdentityProviderRequestOptionsWithManifestPromise::
                CreateAndReject(NS_ERROR_FAILURE, __func__);
          })
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aMediationRequirement, principal,
           relyingParty](const IdentityProviderRequestOptionsWithManifest&
                             providerAndManifest) {
            IdentityProviderAPIConfig manifest;
            IdentityProviderRequestOptions provider;
            std::tie(provider, manifest) = providerAndManifest;
            return CreateCredentialDuringDiscovery(principal, relyingParty,
                                                   provider, manifest,
                                                   aMediationRequirement);
          },
          [](nsresult error) {
            return GetIPCIdentityCredentialPromise::CreateAndReject(error,
                                                                    __func__);
          })
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [result, timeout = std::move(timeout)](
              const GetIPCIdentityCredentialPromise::ResolveOrRejectValue&&
                  value) {
            // Resolve the result
            result->ResolveOrReject(value, __func__);

            // Cancel the timer (if it is still pending) and
            // release the hold on the variables leaked into the timer.
            if (timeout &&
                StaticPrefs::
                    dom_security_credentialmanagement_identity_reject_delay_enabled()) {
              timeout->Cancel();
            }
          });

  return result;
}

// static
Maybe<IdentityProviderRequestOptionsWithManifest> SkipAccountChooser(
    const Sequence<IdentityProviderRequestOptions>& aProviders,
    const Sequence<GetManifestPromise::ResolveOrRejectValue>& aManifests) {
  if (aProviders.Length() != 1) {
    return Nothing();
  }
  if (aManifests.Length() != 1) {
    return Nothing();
  }
  if (!aManifests.ElementAt(0).IsResolve()) {
    return Nothing();
  }
  const IdentityProviderRequestOptions& resolvedProvider =
      aProviders.ElementAt(0);
  const IdentityProviderAPIConfig& resolvedManifest =
      aManifests.ElementAt(0).ResolveValue();
  return Some(std::make_tuple(resolvedProvider, resolvedManifest));
}

// static
Maybe<IdentityProviderAccount> FindAccountToReauthenticate(
    const IdentityProviderRequestOptions& aProvider, nsIPrincipal* aRPPrincipal,
    const IdentityProviderAccountList& aAccountList) {
  if (!aAccountList.mAccounts.WasPassed()) {
    return Nothing();
  }

  nsresult rv;
  nsCOMPtr<nsIIdentityCredentialStorageService> icStorageService =
      mozilla::components::IdentityCredentialStorageService::Service(&rv);
  if (NS_WARN_IF(!icStorageService)) {
    return Nothing();
  }

  Maybe<IdentityProviderAccount> result = Nothing();
  for (const IdentityProviderAccount& account :
       aAccountList.mAccounts.Value()) {
    // Don't reauthenticate accounts that have an approved clients list but no
    // matching clientID from navigator.credentials.get's argument
    if (account.mApproved_clients.WasPassed()) {
      if (!account.mApproved_clients.Value().Contains(
              NS_ConvertUTF8toUTF16(aProvider.mClientId))) {
        continue;
      }
    }

    RefPtr<nsIURI> configURI;
    nsresult rv = NS_NewURI(getter_AddRefs(configURI), aProvider.mConfigURL);
    if (NS_FAILED(rv)) {
      continue;
    }
    nsCOMPtr<nsIPrincipal> idpPrincipal = BasePrincipal::CreateContentPrincipal(
        configURI, aRPPrincipal->OriginAttributesRef());

    // Don't reauthenticate unconnected accounts
    bool connected = false;
    rv = icStorageService->Connected(aRPPrincipal, idpPrincipal, &connected);
    if (NS_WARN_IF(NS_FAILED(rv)) || !connected) {
      continue;
    }

    // Don't reauthenticate if silent access is disabled
    bool silentAllowed = false;
    rv = CanSilentlyCollect(aRPPrincipal, idpPrincipal, &silentAllowed);
    if (!NS_WARN_IF(NS_FAILED(rv)) && !silentAllowed) {
      continue;
    }

    // We only auto-reauthenticate if we have one candidate.
    if (result.isSome()) {
      return Nothing();
    }

    // Remember our first candidate so we can return it after
    // this loop, or return nothing if we find another!
    result = Some(account);
  }

  return result;
}

// static
RefPtr<GetIPCIdentityCredentialPromise> CreateCredentialDuringDiscovery(
    nsIPrincipal* aPrincipal, WebIdentityParent* aRelyingParty,
    const IdentityProviderRequestOptions& aProvider,
    const IdentityProviderAPIConfig& aManifest,
    const CredentialMediationRequirement& aMediationRequirement) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aRelyingParty);

  nsCOMPtr<nsIPrincipal> argumentPrincipal = aPrincipal;
  RefPtr<WebIdentityParent> relyingParty(aRelyingParty);
  RefPtr<CanonicalBrowsingContext> browsingContext(
      aRelyingParty->MaybeBrowsingContext());
  if (!browsingContext) {
    return GetIPCIdentityCredentialPromise::CreateAndReject(NS_ERROR_FAILURE,
                                                            __func__);
  }

  return FetchAccountList(argumentPrincipal, aProvider, aManifest)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [argumentPrincipal, browsingContext, aManifest, aMediationRequirement,
           aProvider](
              const std::tuple<IdentityProviderAPIConfig,
                               IdentityProviderAccountList>& promiseResult) {
            IdentityProviderAPIConfig currentManifest;
            IdentityProviderAccountList accountList;
            std::tie(currentManifest, accountList) = promiseResult;
            if (!accountList.mAccounts.WasPassed() ||
                accountList.mAccounts.Value().Length() == 0) {
              return GetAccountPromise::CreateAndReject(NS_ERROR_FAILURE,
                                                        __func__);
            }

            // Remove accounts without a matching login hint if one was provided
            // in the JS call
            if (aProvider.mLoginHint.WasPassed()) {
              const nsCString& loginHint = aProvider.mLoginHint.Value();
              accountList.mAccounts.Value().RemoveElementsBy(
                  [loginHint](const IdentityProviderAccount& account) {
                    if (!account.mLogin_hints.WasPassed() ||
                        account.mLogin_hints.Value().Length() == 0) {
                      return true;
                    }
                    if (account.mLogin_hints.Value().Contains(loginHint)) {
                      return false;
                    }
                    return true;
                  });
            }

            // Remove accounts without a matching domain hint if one was
            // provided in the JS call
            if (aProvider.mDomainHint.WasPassed()) {
              const nsCString& domainHint = aProvider.mDomainHint.Value();
              accountList.mAccounts.Value().RemoveElementsBy(
                  [domainHint](const IdentityProviderAccount& account) {
                    if (!account.mDomain_hints.WasPassed() ||
                        account.mDomain_hints.Value().Length() == 0) {
                      return true;
                    }
                    // The domain hint "any" matches any hint.
                    if (domainHint.Equals("any")) {
                      return false;
                    }
                    if (account.mDomain_hints.Value().Contains(domainHint)) {
                      return false;
                    }
                    return true;
                  });
            }

            // Remove accounts without a matching account hint if a label was
            // provided in the IDP config
            if (currentManifest.mAccount_label.WasPassed()) {
              const nsCString& accountHint =
                  currentManifest.mAccount_label.Value();
              accountList.mAccounts.Value().RemoveElementsBy(
                  [accountHint](const IdentityProviderAccount& account) {
                    if (!account.mLabel_hints.WasPassed() ||
                        account.mLabel_hints.Value().Length() == 0) {
                      return true;
                    }
                    if (account.mLabel_hints.Value().Contains(accountHint)) {
                      return false;
                    }
                    return true;
                  });
            }

            // If we can skip showing the user any UI by just doing a silent
            // renewal, do so.
            if (aMediationRequirement !=
                CredentialMediationRequirement::Required) {
              Maybe<IdentityProviderAccount> reauthenticatingAccount =
                  FindAccountToReauthenticate(aProvider, argumentPrincipal,
                                              accountList);
              if (reauthenticatingAccount.isSome()) {
                return GetAccountPromise::CreateAndResolve(
                    std::make_tuple(aManifest,
                                    reauthenticatingAccount.extract(), true),
                    __func__);
              }
            }

            if (accountList.mAccounts.Value().Length() < 1) {
              return GetAccountPromise::CreateAndReject(
                  NS_ERROR_DOM_NETWORK_ERR, __func__);
            }

            return PromptUserToSelectAccount(browsingContext, accountList,
                                             aProvider, currentManifest);
          },
          [](nsresult error) {
            return GetAccountPromise::CreateAndReject(error, __func__);
          })
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [argumentPrincipal, aProvider,
           relyingParty](const std::tuple<IdentityProviderAPIConfig,
                                          IdentityProviderAccount, const bool>&
                             promiseResult) {
            IdentityProviderAPIConfig currentManifest;
            IdentityProviderAccount account;
            bool isAutoSelected;
            std::tie(currentManifest, account, isAutoSelected) = promiseResult;
            return FetchToken(argumentPrincipal, relyingParty, aProvider,
                              currentManifest, account, isAutoSelected);
          },
          [](nsresult error) {
            return GetTokenPromise::CreateAndReject(error, __func__);
          })
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [argumentPrincipal,
           aProvider](const std::tuple<nsCString, nsCString, const bool>&
                          promiseResult) {
            nsCString token;
            nsCString accountId;
            bool isAutoSelected;
            std::tie(token, accountId, isAutoSelected) = promiseResult;
            IPCIdentityCredential credential;
            credential.token() = Some(token);
            credential.id() = NS_ConvertUTF8toUTF16(accountId);
            credential.isAutoSelected() = isAutoSelected;
            credential.configURL() = aProvider.mConfigURL;
            // We always make sure accounts are linked after we successfully
            // fetch a token
            nsresult rv = LinkAccount(argumentPrincipal, accountId, aProvider);
            if (NS_FAILED(rv)) {
              return GetIPCIdentityCredentialPromise::CreateAndReject(rv,
                                                                      __func__);
            }
            return GetIPCIdentityCredentialPromise::CreateAndResolve(credential,
                                                                     __func__);
          },
          [browsingContext](nsresult error) {
            CloseUserInterface(browsingContext);
            return GetIPCIdentityCredentialPromise::CreateAndReject(error,
                                                                    __func__);
          });
}

// static
RefPtr<GetRootManifestPromise> FetchRootManifest(
    nsIPrincipal* aPrincipal, const IdentityProviderConfig& aProvider) {
  MOZ_ASSERT(XRE_IsParentProcess());
  if (StaticPrefs::
          dom_security_credentialmanagement_identity_test_ignore_well_known()) {
    return GetRootManifestPromise::CreateAndResolve(Nothing(), __func__);
  }

  // Build the URL
  nsCString configLocation = aProvider.mConfigURL;
  nsCOMPtr<nsIURI> configURI;
  nsresult rv = NS_NewURI(getter_AddRefs(configURI), configLocation);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return GetRootManifestPromise::CreateAndReject(rv, __func__);
  }
  RefPtr<nsIEffectiveTLDService> etld =
      mozilla::components::EffectiveTLD::Service();
  if (!etld) {
    return GetRootManifestPromise::CreateAndReject(
        NS_ERROR_SERVICE_NOT_AVAILABLE, __func__);
  }
  nsCString manifestURIString;
  rv = etld->GetSite(configURI, manifestURIString);
  if (NS_FAILED(rv)) {
    return GetRootManifestPromise::CreateAndReject(NS_ERROR_INVALID_ARG,
                                                   __func__);
  }
  nsAutoCString wellKnownPathForTesting;
  rv = Preferences::GetCString(
      "dom.security.credentialmanagement.identity.test_well_known_path",
      wellKnownPathForTesting);
  if (NS_SUCCEEDED(rv) && !wellKnownPathForTesting.IsVoid() &&
      !wellKnownPathForTesting.IsEmpty()) {
    manifestURIString.Append(wellKnownPathForTesting);
  } else {
    manifestURIString.AppendLiteral("/.well-known/web-identity");
  }
  nsCOMPtr<nsIURI> manifestURI;
  rv = NS_NewURI(getter_AddRefs(manifestURI), manifestURIString, nullptr);
  if (NS_FAILED(rv)) {
    return GetRootManifestPromise::CreateAndReject(NS_ERROR_INVALID_ARG,
                                                   __func__);
  }

  // We actually don't need to do any of this well-known stuff if the
  // requesting principal is same-site to the manifest URI. There is no
  // privacy risk in that case, because the requests could be sent with
  // their unpartitioned cookies anyway.
  if (!aPrincipal->GetIsNullPrincipal()) {
    bool thirdParty = true;
    rv = aPrincipal->IsThirdPartyURI(manifestURI, &thirdParty);
    if (NS_SUCCEEDED(rv) && !thirdParty) {
      return GetRootManifestPromise::CreateAndResolve(Nothing(), __func__);
    }
  }

  return IdentityNetworkHelpers::FetchWellKnownHelper(manifestURI, aPrincipal)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aProvider](const IdentityProviderWellKnown& manifest) {
            // Resolve whether or not the argument URL is found in
            // the well-known
            if (manifest.mProvider_urls.Contains(aProvider.mConfigURL)) {
              return GetRootManifestPromise::CreateAndResolve(Some(manifest),
                                                              __func__);
            }
            return GetRootManifestPromise::CreateAndReject(NS_ERROR_FAILURE,
                                                           __func__);
          },
          [](nsresult error) {
            return GetRootManifestPromise::CreateAndReject(error, __func__);
          });
}

// static
RefPtr<GetManifestPromise> FetchManifest(
    nsIPrincipal* aPrincipal, const IdentityProviderConfig& aProvider) {
  MOZ_ASSERT(XRE_IsParentProcess());

  nsCOMPtr<nsIPrincipal> requestingPrincipal(aPrincipal);
  return FetchRootManifest(aPrincipal, aProvider)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aProvider,
           requestingPrincipal](Maybe<IdentityProviderWellKnown> rootManifest) {
            // Build the URL
            nsCString configLocation = aProvider.mConfigURL;
            nsCOMPtr<nsIURI> manifestURI;
            nsresult rv =
                NS_NewURI(getter_AddRefs(manifestURI), configLocation, nullptr);
            if (NS_FAILED(rv)) {
              return MozPromise<std::tuple<Maybe<IdentityProviderWellKnown>,
                                           IdentityProviderAPIConfig>,
                                nsresult,
                                true>::CreateAndReject(NS_ERROR_INVALID_ARG,
                                                       __func__);
            }
            return IdentityNetworkHelpers::FetchConfigHelper(
                manifestURI, requestingPrincipal, rootManifest);
          },
          [](nsresult error) {
            return MozPromise<std::tuple<Maybe<IdentityProviderWellKnown>,
                                         IdentityProviderAPIConfig>,
                              nsresult, true>::CreateAndReject(error, __func__);
          })
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aProvider](std::tuple<Maybe<IdentityProviderWellKnown>,
                                 IdentityProviderAPIConfig>
                          manifests) {
            IdentityProviderAPIConfig currentManifest;
            Maybe<IdentityProviderWellKnown> fetchedWellKnown;
            std::tie(fetchedWellKnown, currentManifest) = manifests;
            // If we have more than one provider URL, we need to make sure that
            // the accounts endpoint matches
            nsCString configLocation = aProvider.mConfigURL;
            if (fetchedWellKnown.isSome()) {
              IdentityProviderWellKnown wellKnown(fetchedWellKnown.extract());
              if (wellKnown.mProvider_urls.Length() == 1) {
                if (!wellKnown.mProvider_urls.Contains(configLocation)) {
                  return GetManifestPromise::CreateAndReject(NS_ERROR_FAILURE,
                                                             __func__);
                }
              } else if (!wellKnown.mProvider_urls.Contains(configLocation) ||
                         !wellKnown.mAccounts_endpoint.WasPassed() ||
                         !wellKnown.mAccounts_endpoint.Value().Equals(
                             currentManifest.mAccounts_endpoint)) {
                return GetManifestPromise::CreateAndReject(NS_ERROR_FAILURE,
                                                           __func__);
              }
            }
            return GetManifestPromise::CreateAndResolve<
                mozilla::dom::IdentityProviderAPIConfig>(
                IdentityProviderAPIConfig(currentManifest), __func__);
          },
          [](nsresult error) {
            return GetManifestPromise::CreateAndReject(error, __func__);
          });
}

// static
RefPtr<GetAccountListPromise> FetchAccountList(
    nsIPrincipal* aPrincipal, const IdentityProviderRequestOptions& aProvider,
    const IdentityProviderAPIConfig& aManifest) {
  MOZ_ASSERT(XRE_IsParentProcess());
  // Build the URL
  nsCOMPtr<nsIURI> baseURI;
  nsresult rv = NS_NewURI(getter_AddRefs(baseURI), aProvider.mConfigURL);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return GetAccountListPromise::CreateAndReject(rv, __func__);
  }
  nsCOMPtr<nsIURI> idpURI;
  rv = NS_NewURI(getter_AddRefs(idpURI), aManifest.mAccounts_endpoint, nullptr,
                 baseURI);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return GetAccountListPromise::CreateAndReject(rv, __func__);
  }
  nsCOMPtr<nsIPrincipal> idpPrincipal = BasePrincipal::CreateContentPrincipal(
      idpURI, aPrincipal->OriginAttributesRef());

  return IdentityNetworkHelpers::FetchAccountsHelper(idpURI, idpPrincipal)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aManifest](const IdentityProviderAccountList& accountList) {
            return GetAccountListPromise::CreateAndResolve(
                std::make_tuple(aManifest, accountList), __func__);
          },
          [](nsresult error) {
            return GetAccountListPromise::CreateAndReject(error, __func__);
          });
}

// static
RefPtr<GetTokenPromise> FetchToken(
    nsIPrincipal* aPrincipal, WebIdentityParent* aRelyingParty,
    const IdentityProviderRequestOptions& aProvider,
    const IdentityProviderAPIConfig& aManifest,
    const IdentityProviderAccount& aAccount, const bool isAutoSelected) {
  MOZ_ASSERT(XRE_IsParentProcess());
  // Build the URL
  nsCOMPtr<nsIURI> baseURI;
  nsCString baseURIString = aProvider.mConfigURL;
  nsresult rv = NS_NewURI(getter_AddRefs(baseURI), baseURIString);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return GetTokenPromise::CreateAndReject(rv, __func__);
  }
  nsCOMPtr<nsIURI> idpURI;
  nsCString tokenSpec = aManifest.mId_assertion_endpoint;
  rv = NS_NewURI(getter_AddRefs(idpURI), tokenSpec.get(), baseURI);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return GetTokenPromise::CreateAndReject(rv, __func__);
  }
  nsCString tokenLocation;
  rv = idpURI->GetSpec(tokenLocation);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return GetTokenPromise::CreateAndReject(rv, __func__);
  }

  // Create a new request
  URLParams bodyValue;
  bodyValue.Set("account_id"_ns, NS_ConvertUTF16toUTF8(aAccount.mId));
  bodyValue.Set("client_id"_ns, aProvider.mClientId);
  if (aProvider.mNonce.WasPassed()) {
    bodyValue.Set("nonce"_ns, aProvider.mNonce.Value());
  }
  bodyValue.Set("disclosure_text_shown"_ns, "false"_ns);
  nsCString serializedIsAutoSelected = isAutoSelected ? "true"_ns : "false"_ns;
  bodyValue.Set("is_auto_selected"_ns, serializedIsAutoSelected);
  nsAutoCString bodyCString;
  bodyValue.Serialize(bodyCString, true);

  RefPtr<WebIdentityParent> relyingParty(aRelyingParty);
  return IdentityNetworkHelpers::FetchTokenHelper(idpURI, bodyCString,
                                                  aPrincipal)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aAccount, idpURI, relyingParty,
           isAutoSelected](const IdentityAssertionResponse& response) {
            // If we were provided a token, resolve with it.
            if (response.mToken.WasPassed()) {
              return GetTokenPromise::CreateAndResolve(
                  std::make_tuple(response.mToken.Value(),
                                  NS_ConvertUTF16toUTF8(aAccount.mId),
                                  isAutoSelected),
                  __func__);
            }
            // If we don't have a continuation window to open at this stage,
            // reject with the appropriate error.
            if (!response.mContinue_on.WasPassed()) {
              return GetTokenPromise::CreateAndReject(NS_ERROR_DOM_NETWORK_ERR,
                                                      __func__);
            }
            // Construct the URI we are going to open
            nsCOMPtr<nsIURI> continueURI;
            nsCString continueSpec = response.mContinue_on.Value();
            nsresult rv =
                NS_NewURI(getter_AddRefs(continueURI), continueSpec.get());
            if (NS_FAILED(rv)) {
              return GetTokenPromise::CreateAndReject(NS_ERROR_DOM_NETWORK_ERR,
                                                      __func__);
            }
            // It must be same-origin to the URL used to fetch to identity
            // assertion
            if (!nsScriptSecurityManager::SecurityCompareURIs(continueURI,
                                                              idpURI)) {
              return GetTokenPromise::CreateAndReject(NS_ERROR_DOM_NETWORK_ERR,
                                                      __func__);
            }
            // Open the popup, and return the result of its interaction
            return AuthorizationPopupForToken(continueURI, relyingParty,
                                              aAccount, isAutoSelected);
          },
          [](nsresult error) {
            return GetTokenPromise::CreateAndReject(error, __func__);
          });
}

RefPtr<GetTokenPromise> AuthorizationPopupForToken(
    nsIURI* aContinueURI, WebIdentityParent* aRelyingParty,
    const IdentityProviderAccount& aAccount, const bool isAutoSelected) {
  MOZ_ASSERT(aContinueURI);
  IdentityCredentialRequestManager* requestManager =
      IdentityCredentialRequestManager::GetInstance();
  if (!requestManager) {
    return GetTokenPromise::CreateAndReject(NS_ERROR_DOM_NETWORK_ERR, __func__);
  }
  // Start the process of getting a token for the popup.
  // The request manager opens the popup and holds a ref to the promise
  // returned. This then gets settles depending on the popup page's behavior (or
  // rejects on close).
  return requestManager->GetTokenFromPopup(aRelyingParty, aContinueURI)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aAccount, isAutoSelected](
              const std::tuple<nsCString, Maybe<nsCString>>& promiseResult) {
            // We will resolve either way with our token from here, but
            // We may have an account ID to override the user's selection in the
            // account chooser.
            nsCString token;
            Maybe<nsCString> overridingAccountId;
            std::tie(token, overridingAccountId) = promiseResult;
            if (overridingAccountId.isSome()) {
              return GetTokenPromise::CreateAndResolve(
                  std::make_tuple(token, overridingAccountId.value(),
                                  isAutoSelected),
                  __func__);
            }
            return GetTokenPromise::CreateAndResolve(
                std::make_tuple(token, NS_ConvertUTF16toUTF8(aAccount.mId),
                                isAutoSelected),
                __func__);
          },
          [](nsresult rv) {
            return GetTokenPromise::CreateAndReject(NS_ERROR_DOM_NETWORK_ERR,
                                                    __func__);
          });
}

// static
RefPtr<MozPromise<bool, nsresult, true>> DisconnectInMainProcess(
    nsIPrincipal* aDocumentPrincipal,
    const IdentityCredentialDisconnectOptions& aOptions) {
  MOZ_ASSERT(XRE_IsParentProcess());
  nsresult rv;
  nsCOMPtr<nsIIdentityCredentialStorageService> icStorageService =
      mozilla::components::IdentityCredentialStorageService::Service(&rv);
  if (NS_WARN_IF(!icStorageService)) {
    return MozPromise<bool, nsresult, true>::CreateAndReject(rv, __func__);
  }

  RefPtr<MozPromise<bool, nsresult, true>::Private> resultPromise =
      new MozPromise<bool, nsresult, true>::Private(__func__);

  RefPtr<nsIURI> configURI;
  rv = NS_NewURI(getter_AddRefs(configURI), aOptions.mConfigURL);
  if (NS_FAILED(rv)) {
    resultPromise->Reject(NS_ERROR_DOM_MALFORMED_URI, __func__);
    return resultPromise;
  }

  nsCOMPtr<nsIPrincipal> principal(aDocumentPrincipal);
  nsCOMPtr<nsIPrincipal> idpPrincipal = BasePrincipal::CreateContentPrincipal(
      configURI, principal->OriginAttributesRef());

  FetchManifest(principal, aOptions)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [resultPromise, aOptions, icStorageService, configURI, idpPrincipal,
           principal](const IdentityProviderAPIConfig& aConfig) {
            if (!aConfig.mDisconnect_endpoint.WasPassed()) {
              resultPromise->Reject(NS_ERROR_DOM_NETWORK_ERR, __func__);
              return MozPromise<DisconnectedAccount, nsresult,
                                true>::CreateAndReject(NS_OK, __func__);
            }
            RefPtr<nsIURI> disconnectURI;
            nsCString disconnectArgument = aConfig.mDisconnect_endpoint.Value();
            nsresult rv = NS_NewURI(getter_AddRefs(disconnectURI),
                                    disconnectArgument, nullptr, configURI);
            if (NS_FAILED(rv)) {
              resultPromise->Reject(NS_ERROR_DOM_NETWORK_ERR, __func__);
              return MozPromise<DisconnectedAccount, nsresult,
                                true>::CreateAndReject(NS_OK, __func__);
            }

            bool connected = false;
            rv = icStorageService->Connected(principal, idpPrincipal,
                                             &connected);
            if (NS_WARN_IF(NS_FAILED(rv)) || !connected) {
              resultPromise->Reject(NS_ERROR_DOM_NETWORK_ERR, __func__);
              return MozPromise<DisconnectedAccount, nsresult,
                                true>::CreateAndReject(NS_OK, __func__);
            }

            // Create a new request
            URLParams bodyValue;
            bodyValue.Set("client_id"_ns, aOptions.mClientId);
            bodyValue.Set("account_hint"_ns, aOptions.mAccountHint);
            nsAutoCString bodyCString;
            bodyValue.Serialize(bodyCString, true);
            return IdentityNetworkHelpers::FetchDisconnectHelper(
                disconnectURI, bodyCString, principal);
          },
          [resultPromise](nsresult aError) {
            resultPromise->Reject(aError, __func__);
            // We reject with NS_OK, so that we don't disconnect accounts in the
            // reject callback here.
            return MozPromise<DisconnectedAccount, nsresult,
                              true>::CreateAndReject(NS_OK, __func__);
          })
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [icStorageService, principal, idpPrincipal,
           resultPromise](const DisconnectedAccount& token) {
            bool registered = false, notUsed = false;
            nsresult rv = icStorageService->GetState(principal, idpPrincipal,
                                                     token.mAccount_id,
                                                     &registered, &notUsed);
            if (NS_WARN_IF(NS_FAILED(rv))) {
              resultPromise->Reject(NS_ERROR_UNEXPECTED, __func__);
              return;
            }
            if (registered) {
              nsresult rv = icStorageService->Delete(principal, idpPrincipal,
                                                     token.mAccount_id);
              if (NS_WARN_IF(NS_FAILED(rv))) {
                resultPromise->Reject(NS_ERROR_UNEXPECTED, __func__);
                return;
              }
              resultPromise->Resolve(true, __func__);
            } else {
              nsresult rv =
                  icStorageService->Disconnect(principal, idpPrincipal);
              if (NS_WARN_IF(NS_FAILED(rv))) {
                resultPromise->Reject(NS_ERROR_UNEXPECTED, __func__);
                return;
              }
              resultPromise->Resolve(true, __func__);
            }
            return;
          },
          [icStorageService, principal, idpPrincipal,
           resultPromise](nsresult error) {
            // Bail out if we already rejected the result above.
            if (error == NS_OK) {
              return;
            }

            // If we issued the request and it failed, fall back
            // to clearing all.
            nsresult rv = icStorageService->Disconnect(principal, idpPrincipal);
            if (NS_WARN_IF(NS_FAILED(rv))) {
              resultPromise->Reject(NS_ERROR_UNEXPECTED, __func__);
              return;
            }
            resultPromise->Resolve(true, __func__);
            return;
          });

  return resultPromise;
}

// static
RefPtr<GetIdentityProviderRequestOptionsWithManifestPromise>
PromptUserToSelectProvider(
    BrowsingContext* aBrowsingContext,
    const Sequence<IdentityProviderRequestOptions>& aProviders,
    const Sequence<GetManifestPromise::ResolveOrRejectValue>& aManifests) {
  MOZ_ASSERT(aBrowsingContext);
  RefPtr<GetIdentityProviderRequestOptionsWithManifestPromise::Private>
      resultPromise =
          new GetIdentityProviderRequestOptionsWithManifestPromise::Private(
              __func__);

  if (NS_WARN_IF(!aBrowsingContext)) {
    resultPromise->Reject(NS_ERROR_FAILURE, __func__);
    return resultPromise;
  }

  nsresult error;
  nsCOMPtr<nsIIdentityCredentialPromptService> icPromptService =
      mozilla::components::IdentityCredentialPromptService::Service(&error);
  if (NS_WARN_IF(!icPromptService)) {
    resultPromise->Reject(error, __func__);
    return resultPromise;
  }

  nsCOMPtr<nsIXPConnectWrappedJS> wrapped = do_QueryInterface(icPromptService);
  AutoJSAPI jsapi;
  if (NS_WARN_IF(!jsapi.Init(wrapped->GetJSObjectGlobal()))) {
    resultPromise->Reject(NS_ERROR_FAILURE, __func__);
    return resultPromise;
  }

  JS::Rooted<JS::Value> providersJS(jsapi.cx());
  bool success = ToJSValue(jsapi.cx(), aProviders, &providersJS);
  if (NS_WARN_IF(!success)) {
    resultPromise->Reject(NS_ERROR_FAILURE, __func__);
    return resultPromise;
  }

  // Convert each settled MozPromise into a Nullable<ResolveValue>
  Sequence<Nullable<IdentityProviderAPIConfig>> manifests;
  for (GetManifestPromise::ResolveOrRejectValue manifest : aManifests) {
    if (manifest.IsResolve()) {
      if (NS_WARN_IF(
              !manifests.AppendElement(manifest.ResolveValue(), fallible))) {
        resultPromise->Reject(NS_ERROR_FAILURE, __func__);
        return resultPromise;
      }
    } else {
      if (NS_WARN_IF(!manifests.AppendElement(
              Nullable<IdentityProviderAPIConfig>(), fallible))) {
        resultPromise->Reject(NS_ERROR_FAILURE, __func__);
        return resultPromise;
      }
    }
  }
  JS::Rooted<JS::Value> manifestsJS(jsapi.cx());
  success = ToJSValue(jsapi.cx(), manifests, &manifestsJS);
  if (NS_WARN_IF(!success)) {
    resultPromise->Reject(NS_ERROR_FAILURE, __func__);
    return resultPromise;
  }

  RefPtr<Promise> showPromptPromise;
  icPromptService->ShowProviderPrompt(aBrowsingContext, providersJS,
                                      manifestsJS,
                                      getter_AddRefs(showPromptPromise));

  showPromptPromise->AddCallbacksWithCycleCollectedArgs(
      [aProviders, aManifests, resultPromise](
          JSContext*, JS::Handle<JS::Value> aValue, ErrorResult&) {
        int32_t result = aValue.toInt32();
        if (result < 0 || (uint32_t)result > aProviders.Length() ||
            (uint32_t)result > aManifests.Length()) {
          resultPromise->Reject(NS_ERROR_FAILURE, __func__);
          return;
        }
        const IdentityProviderRequestOptions& resolvedProvider =
            aProviders.ElementAt(result);
        if (!aManifests.ElementAt(result).IsResolve()) {
          resultPromise->Reject(NS_ERROR_FAILURE, __func__);
          return;
        }
        const IdentityProviderAPIConfig& resolvedManifest =
            aManifests.ElementAt(result).ResolveValue();
        resultPromise->Resolve(
            std::make_tuple(resolvedProvider, resolvedManifest), __func__);
      },
      [resultPromise](JSContext*, JS::Handle<JS::Value> aValue, ErrorResult&) {
        resultPromise->Reject(
            Promise::TryExtractNSResultFromRejectionValue(aValue), __func__);
      });
  // Working around https://gcc.gnu.org/bugzilla/show_bug.cgi?id=85883
  showPromptPromise->AppendNativeHandler(
      new MozPromiseRejectOnDestruction{resultPromise, __func__});

  return resultPromise;
}

// static
RefPtr<GetAccountPromise> PromptUserToSelectAccount(
    BrowsingContext* aBrowsingContext,
    const IdentityProviderAccountList& aAccounts,
    const IdentityProviderRequestOptions& aProvider,
    const IdentityProviderAPIConfig& aManifest) {
  MOZ_ASSERT(aBrowsingContext);
  RefPtr<GetAccountPromise::Private> resultPromise =
      new GetAccountPromise::Private(__func__);

  if (NS_WARN_IF(!aBrowsingContext)) {
    resultPromise->Reject(NS_ERROR_FAILURE, __func__);
    return resultPromise;
  }

  nsresult error;
  nsCOMPtr<nsIIdentityCredentialPromptService> icPromptService =
      mozilla::components::IdentityCredentialPromptService::Service(&error);
  if (NS_WARN_IF(!icPromptService)) {
    resultPromise->Reject(error, __func__);
    return resultPromise;
  }

  nsCOMPtr<nsIXPConnectWrappedJS> wrapped = do_QueryInterface(icPromptService);
  AutoJSAPI jsapi;
  if (NS_WARN_IF(!jsapi.Init(wrapped->GetJSObjectGlobal()))) {
    resultPromise->Reject(NS_ERROR_FAILURE, __func__);
    return resultPromise;
  }

  JS::Rooted<JS::Value> accountsJS(jsapi.cx());
  bool success = ToJSValue(jsapi.cx(), aAccounts, &accountsJS);
  if (NS_WARN_IF(!success)) {
    resultPromise->Reject(NS_ERROR_FAILURE, __func__);
    return resultPromise;
  }

  JS::Rooted<JS::Value> providerJS(jsapi.cx());
  success = ToJSValue(jsapi.cx(), aProvider, &providerJS);
  if (NS_WARN_IF(!success)) {
    resultPromise->Reject(NS_ERROR_FAILURE, __func__);
    return resultPromise;
  }

  JS::Rooted<JS::Value> manifestJS(jsapi.cx());
  success = ToJSValue(jsapi.cx(), aManifest, &manifestJS);
  if (NS_WARN_IF(!success)) {
    resultPromise->Reject(NS_ERROR_FAILURE, __func__);
    return resultPromise;
  }

  RefPtr<Promise> showPromptPromise;
  icPromptService->ShowAccountListPrompt(aBrowsingContext, accountsJS,
                                         providerJS, manifestJS,
                                         getter_AddRefs(showPromptPromise));

  showPromptPromise->AddCallbacksWithCycleCollectedArgs(
      [aAccounts, resultPromise, aManifest](
          JSContext*, JS::Handle<JS::Value> aValue, ErrorResult&) {
        int32_t result = aValue.toInt32();
        if (!aAccounts.mAccounts.WasPassed() || result < 0 ||
            (uint32_t)result > aAccounts.mAccounts.Value().Length()) {
          resultPromise->Reject(NS_ERROR_FAILURE, __func__);
          return;
        }
        const IdentityProviderAccount& resolved =
            aAccounts.mAccounts.Value().ElementAt(result);
        resultPromise->Resolve(std::make_tuple(aManifest, resolved, false),
                               __func__);
      },
      [resultPromise](JSContext*, JS::Handle<JS::Value> aValue, ErrorResult&) {
        resultPromise->Reject(
            Promise::TryExtractNSResultFromRejectionValue(aValue), __func__);
      });
  // Working around https://gcc.gnu.org/bugzilla/show_bug.cgi?id=85883
  showPromptPromise->AppendNativeHandler(
      new MozPromiseRejectOnDestruction{resultPromise, __func__});

  return resultPromise;
}

// static
nsresult LinkAccount(nsIPrincipal* aPrincipal, const nsCString& aAccountId,
                     const IdentityProviderRequestOptions& aProvider) {
  MOZ_ASSERT(aPrincipal);

  nsresult error;
  nsCOMPtr<nsIIdentityCredentialStorageService> icStorageService =
      mozilla::components::IdentityCredentialStorageService::Service(&error);
  if (NS_WARN_IF(!icStorageService)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  // Check the storage bit
  nsCString configLocation = aProvider.mConfigURL;
  nsCOMPtr<nsIURI> idpURI;
  error = NS_NewURI(getter_AddRefs(idpURI), configLocation);
  if (NS_WARN_IF(NS_FAILED(error))) {
    return error;
  }
  nsCOMPtr<nsIPrincipal> idpPrincipal = BasePrincipal::CreateContentPrincipal(
      idpURI, aPrincipal->OriginAttributesRef());

  // Mark as logged in and return
  icStorageService->SetState(aPrincipal, idpPrincipal, aAccountId, true, true);

  nsCOMPtr<nsIPermissionManager> permissionManager =
      components::PermissionManager::Service();

  if (NS_WARN_IF(!permissionManager)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsCString idpOrigin;
  error = idpPrincipal->GetOrigin(idpOrigin);
  NS_ENSURE_SUCCESS(error, error);

  permissionManager->AddFromPrincipal(
      aPrincipal, "credential-allow-silent-access^"_ns + idpOrigin,
      nsIPermissionManager::ALLOW_ACTION, nsIPermissionManager::EXPIRE_SESSION,
      0);
  permissionManager->AddFromPrincipal(aPrincipal,
                                      "credential-allow-silent-access"_ns,
                                      nsIPermissionManager::ALLOW_ACTION,
                                      nsIPermissionManager::EXPIRE_SESSION, 0);
  return NS_OK;
}

// static
void CloseUserInterface(BrowsingContext* aBrowsingContext) {
  nsresult error;
  nsCOMPtr<nsIIdentityCredentialPromptService> icPromptService =
      mozilla::components::IdentityCredentialPromptService::Service(&error);
  if (NS_WARN_IF(!icPromptService)) {
    return;
  }
  icPromptService->Close(aBrowsingContext);
}

}  // namespace identity

}  // namespace mozilla::dom
