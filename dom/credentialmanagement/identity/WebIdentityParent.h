/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_WebIdentityParent_h
#define mozilla_dom_WebIdentityParent_h

#include "mozilla/dom/PWebIdentity.h"
#include "mozilla/dom/PWebIdentityParent.h"
#include "mozilla/dom/WindowGlobalParent.h"

namespace mozilla::dom {

class WebIdentityParent final : public PWebIdentityParent {
  NS_INLINE_DECL_REFCOUNTING(WebIdentityParent, override);

 public:
  WebIdentityParent() = default;
  virtual void ActorDestroy(ActorDestroyReason aWhy) override;

  CanonicalBrowsingContext* MaybeBrowsingContext() {
    WindowGlobalParent* manager =
        mozilla::ipc::ActorCast<WindowGlobalParent>(Manager());
    if (!manager) {
      return nullptr;
    }
    return manager->BrowsingContext();
  }

  mozilla::ipc::IPCResult RecvGetIdentityCredential(
      IdentityCredentialRequestOptions&& aOptions,
      const CredentialMediationRequirement& aMediationRequirement,
      bool aHasUserActivation, const GetIdentityCredentialResolver& aResolver);

  mozilla::ipc::IPCResult RecvRequestCancel();

  mozilla::ipc::IPCResult RecvDisconnectIdentityCredential(
      const IdentityCredentialDisconnectOptions& aOptions,
      const DisconnectIdentityCredentialResolver& aResolver);

  mozilla::ipc::IPCResult RecvSetLoginStatus(
      LoginStatus aStatus, const SetLoginStatusResolver& aResolver);

  mozilla::ipc::IPCResult RecvPreventSilentAccess(
      const PreventSilentAccessResolver& aResolver);

  mozilla::ipc::IPCResult RecvResolveContinuationWindow(
      nsCString&& aToken, IdentityResolveOptions&& aOptions,
      const ResolveContinuationWindowResolver& aResolver);

  mozilla::ipc::IPCResult RecvIsActiveContinuationWindow(
      const IsActiveContinuationWindowResolver& aResolver);

 private:
  ~WebIdentityParent() = default;
};

namespace identity {
// These are promise types, all used to support the async implementation of
// this API. All are of the form MozPromise<RefPtr<T>, nsresult>.
// Tuples are included to shuffle additional values along, so that the
// intermediate state is entirely in the promise chain and we don't have to
// capture an early step's result into a callback for a subsequent promise.
using GetIdentityCredentialPromise =
    MozPromise<RefPtr<IdentityCredential>, nsresult, true>;
using GetIdentityCredentialsPromise =
    MozPromise<nsTArray<RefPtr<IdentityCredential>>, nsresult, true>;
using GetIPCIdentityCredentialPromise =
    MozPromise<IPCIdentityCredential, nsresult, true>;
using GetIPCIdentityCredentialsPromise =
    MozPromise<CopyableTArray<IPCIdentityCredential>, nsresult, true>;
using GetIdentityProviderRequestOptionsPromise =
    MozPromise<IdentityProviderRequestOptions, nsresult, true>;
using ValidationPromise = MozPromise<bool, nsresult, true>;
using GetRootManifestPromise =
    MozPromise<Maybe<IdentityProviderWellKnown>, nsresult, true>;
using GetManifestPromise =
    MozPromise<IdentityProviderAPIConfig, nsresult, true>;
using IdentityProviderRequestOptionsWithManifest =
    std::tuple<IdentityProviderRequestOptions, IdentityProviderAPIConfig>;
using GetIdentityProviderRequestOptionsWithManifestPromise =
    MozPromise<IdentityProviderRequestOptionsWithManifest, nsresult, true>;
using GetAccountListPromise = MozPromise<
    std::tuple<IdentityProviderAPIConfig, IdentityProviderAccountList>,
    nsresult, true>;
using GetIdentityAssertionPromise =
    MozPromise<std::tuple<IdentityAssertionResponse, IdentityProviderAccount>,
               nsresult, true>;
using GetTokenPromise =
    MozPromise<std::tuple<nsCString, nsCString, const bool>, nsresult, true>;
using GetAccountPromise = MozPromise<
    std::tuple<IdentityProviderAPIConfig, IdentityProviderAccount, const bool>,
    nsresult, true>;
using GetMetadataPromise =
    MozPromise<IdentityProviderClientMetadata, nsresult, true>;

RefPtr<GetIPCIdentityCredentialPromise> GetCredentialInMainProcess(
    nsIPrincipal* aPrincipal, WebIdentityParent* aRelyingParty,
    IdentityCredentialRequestOptions&& aOptions,
    const CredentialMediationRequirement& aMediationRequirement,
    bool aHasUserActivation);

nsresult CanSilentlyCollect(nsIPrincipal* aPrincipal,
                            nsIPrincipal* aIDPPrincipal, bool* aResult);

Maybe<IdentityProviderAccount> FindAccountToReauthenticate(
    const IdentityProviderRequestOptions& aProvider, nsIPrincipal* aRPPrincipal,
    const IdentityProviderAccountList& aAccountList);

Maybe<IdentityProviderRequestOptionsWithManifest> SkipAccountChooser(
    const Sequence<IdentityProviderRequestOptions>& aProviders,
    const Sequence<GetManifestPromise::ResolveOrRejectValue>& aManifests);

// Start the FedCM flow. This will start the timeout timer, fire initial
// network requests, prompt the user, and call into CreateCredential.
//
//   Arguments:
//     aPrincipal: the caller of navigator.credentials.get()'s principal
//     aBrowsingContext: the BC of the caller of navigator.credentials.get()
//     aOptions: argument passed to navigator.credentials.get()
//  Return value:
//    a promise resolving to an IPC credential with type "identity", id
//    constructed to identify it, and token corresponding to the token
//    fetched in FetchToken. This promise may reject with nsresult errors.
//  Side effects:
//    Will send network requests to the IDP. The details of which are in the
//    other methods here.
RefPtr<GetIPCIdentityCredentialPromise> DiscoverFromExternalSourceInMainProcess(
    nsIPrincipal* aPrincipal, WebIdentityParent* aRelyingParty,
    const IdentityCredentialRequestOptions& aOptions,
    const CredentialMediationRequirement& aMediationRequirement);

// Create an IPC credential that can be passed back to the content process.
// This calls a lot of helpers to do the logic of going from a single provider
// to a bearer token for an account at that provider.
//
//  Arguments:
//    aPrincipal: the caller of navigator.credentials.get()'s principal
//    aBrowsingContext: the BC of the caller of navigator.credentials.get()
//    aProvider: the provider to validate the root manifest of
//    aManifest: the internal manifest of the identity provider
//  Return value:
//    a promise resolving to an IPC credential with type "identity", id
//    constructed to identify it, and token corresponding to the token
//    fetched in FetchToken. This promise may reject with nsresult errors.
//  Side effects:
//    Will send network requests to the IDP. The details of which are in the
//    other methods here.
RefPtr<GetIPCIdentityCredentialPromise> CreateCredentialDuringDiscovery(
    nsIPrincipal* aPrincipal, WebIdentityParent* aRelyingParty,
    const IdentityProviderRequestOptions& aProvider,
    const IdentityProviderAPIConfig& aManifest,
    const CredentialMediationRequirement& aMediationRequirement);

// Performs a Fetch for the root manifest of the provided identity provider
// if needed and validates its structure. The returned promise resolves
// if a regular manifest fetch can proceed, with a root manifest value if
// one was fetched
//
//  Arguments:
//    aPrincipal: the caller of navigator.credentials.get()'s principal
//    aProvider: the provider to validate the root manifest of
//  Return value:
//    promise that resolves to a root manifest if one is fetched. Will reject
//    when there are network or other errors.
//  Side effects:
//    Network request to the IDP's well-known if it is needed
//
RefPtr<GetRootManifestPromise> FetchRootManifest(
    nsIPrincipal* aPrincipal, const IdentityProviderConfig& aProvider);

// Performs a Fetch for the internal manifest of the provided identity
// provider. The returned promise resolves with the manifest retrieved.
//
//  Arguments:
//    aPrincipal: the caller of navigator.credentials.get()'s principal
//    aProvider: the provider to fetch the root manifest
//  Return value:
//    promise that resolves to the internal manifest. Will reject
//    when there are network or other errors.
//  Side effects:
//    Network request to the URL in aProvider as the manifest from inside a
//    NullPrincipal sandbox
//
RefPtr<GetManifestPromise> FetchManifest(
    nsIPrincipal* aPrincipal, const IdentityProviderConfig& aProvider);

// Performs a Fetch for the account list from the provided identity
// provider. The returned promise resolves with the manifest and the fetched
// account list in a tuple of objects. We put the argument manifest in the
// tuple to facilitate clean promise chaining.
//
//  Arguments:
//    aPrincipal: the caller of navigator.credentials.get()'s principal
//    aProvider: the provider to get account lists from
//    aManifest: the provider's internal manifest
//  Return value:
//    promise that resolves to a Tuple of the passed manifest and the fetched
//    account list. Will reject when there are network or other errors.
//  Side effects:
//    Network request to the provider supplied account endpoint with
//    credentials but without any indication of aPrincipal.
//
RefPtr<GetAccountListPromise> FetchAccountList(
    nsIPrincipal* aPrincipal, const IdentityProviderRequestOptions& aProvider,
    const IdentityProviderAPIConfig& aManifest);

// Performs a Fetch for a bearer token to the provided identity
// provider for a given account. The returned promise resolves with the
// account argument and the fetched token in a tuple of objects.
// We put the argument account in the
// tuple to facilitate clean promise chaining.
//
//  Arguments:
//    aPrincipal: the caller of navigator.credentials.get()'s principal
//    aProvider: the provider to get account lists from
//    aManifest: the provider's internal manifest
//    aAccount: the account to request
//    aIsAutoSelected: whether the account was auto-selected during the user
//    account selection process
//  Return value:
//    promise that resolves to a Tuple of the passed account and the fetched
//    token. Will reject when there are network or other errors.
//  Side effects:
//    Network request to the provider supplied token endpoint with
//    credentials and including information about the requesting principal.
//
RefPtr<GetTokenPromise> FetchToken(
    nsIPrincipal* aPrincipal, WebIdentityParent* aRelyingParty,
    const IdentityProviderRequestOptions& aProvider,
    const IdentityProviderAPIConfig& aManifest,
    const IdentityProviderAccount& aAccount, const bool aIsAutoSelected);

// Show the user a dialog to select what identity provider they would like
// to try to log in with.
//
//   Arguments:
//    aBrowsingContext: the BC of the caller of navigator.credentials.get()
//    aProviders: the providers to let the user select from
//    aManifests: the manifests
//  Return value:
//    a promise resolving to an identity provider that the user took action
//    to select. This promise may reject with nsresult errors.
//  Side effects:
//    Will show a dialog to the user.
RefPtr<GetIdentityProviderRequestOptionsWithManifestPromise>
PromptUserToSelectProvider(
    BrowsingContext* aBrowsingContext,
    const Sequence<IdentityProviderRequestOptions>& aProviders,
    const Sequence<GetManifestPromise::ResolveOrRejectValue>& aManifests);

// Show the user a dialog to select what account they would like
// to try to log in with.
//
//   Arguments:
//    aBrowsingContext: the BC of the caller of navigator.credentials.get()
//    aAccounts: the accounts to let the user select from
//    aProvider: the provider that was chosen
//    aManifest: the identity provider that was chosen's manifest
//  Return value:
//    a promise resolving to an account that the user took action
//    to select (and aManifest). This promise may reject with nsresult errors.
//  Side effects:
//    Will show a dialog to the user.
RefPtr<GetAccountPromise> PromptUserToSelectAccount(
    BrowsingContext* aBrowsingContext,
    const IdentityProviderAccountList& aAccounts,
    const IdentityProviderRequestOptions& aProvider,
    const IdentityProviderAPIConfig& aManifest);

// Make a connection between the identity provider, relying party, and the
// account ID in the persistent database.
//
//   Arguments:
//    aPrincipal: the principal of the callor of navigator.credentials.get()
//    aAccountId: the account to be linked
//    aProvider: the identity provider's requested options
//  Return value:
//    Success or failure, as nsresult
//  Side effects:
//    Modifies the IdentityCredentialStorageService state for this account.
nsresult LinkAccount(nsIPrincipal* aPrincipal, const nsCString& aAccountId,
                     const IdentityProviderRequestOptions& aProvider);

// Close all dialogs associated with IdentityCredential generation on the
// provided browsing context
//
//   Arguments:
//    aBrowsingContext: the BC of the caller of navigator.credentials.get()
//  Side effects:
//    Will close a dialog shown to the user.
void CloseUserInterface(BrowsingContext* aBrowsingContext);

RefPtr<MozPromise<bool, nsresult, true>> DisconnectInMainProcess(
    nsIPrincipal* aDocumentPrincipal,
    const IdentityCredentialDisconnectOptions& aOptions);

RefPtr<GetTokenPromise> AuthorizationPopupForToken(
    nsIURI* aContinueURI, WebIdentityParent* aRelyingParty,
    const IdentityProviderAccount& aAccount, const bool isAutoSelected);

}  // namespace identity

}  // namespace mozilla::dom

#endif  // mozilla_dom_WebIdentityParent_h
