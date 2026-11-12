/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ClientValidation.h"

#include "mozilla/StaticPrefs_security.h"
#include "mozilla/dom/ClientIPCTypes.h"
#include "mozilla/dom/LoadedOriginSet.h"
#include "mozilla/dom/ProcessIsolation.h"
#include "mozilla/dom/RemoteType.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "mozilla/net/MozURL.h"

namespace mozilla::dom {

using mozilla::ipc::ContentPrincipalInfo;
using mozilla::ipc::PrincipalInfo;
using mozilla::net::MozURL;

Maybe<IPCClientInfo&> ValidateClientInfo(Tainted<IPCClientInfo>& aClientInfo,
                                         LoadedOriginSet* aLoadedOrigins) {
  return aClientInfo.ValidateRef([&](const IPCClientInfo& aInfo) {
    return ClientIsValidPrincipalInfo(aInfo.principalInfo(), aLoadedOrigins);
  });
}

bool ClientIsValidPrincipalInfo(const PrincipalInfo& aPrincipalInfo,
                                LoadedOriginSet* aLoadedOrigins) {
  auto result = mozilla::ipc::PrincipalInfoToPrincipal(aPrincipalInfo);
  if (NS_WARN_IF(result.isErr())) {
    return false;
  }
  nsCOMPtr<nsIPrincipal> principal = result.unwrap();

  // FIXME: Remove the system allowance once for non-inference processes once we
  // can load documents with the system principal into content.
  if (aLoadedOrigins &&
      NS_WARN_IF(!aLoadedOrigins->ValidatePrincipal(
          principal, {ValidatePrincipalOptions::AllowSystemIfLoaded}))) {
    return false;
  }

  // Windows and workers should not have expanded principals, etc.
  return principal->IsSystemPrincipal() || principal->GetIsNullPrincipal() ||
         principal->GetIsContentPrincipal();
}

bool IsValidClientOpConstructorArgs(const ClientOpConstructorArgs& aArgs,
                                    LoadedOriginSet* aLoadedOrigins) {
  switch (aArgs.type()) {
    case ClientOpConstructorArgs::TClientControlledArgs:
      return ClientIsValidPrincipalInfo(
          aArgs.get_ClientControlledArgs().serviceWorker().principalInfo(),
          aLoadedOrigins);

    case ClientOpConstructorArgs::TClientNavigateArgs: {
      const ClientNavigateArgs& args = aArgs.get_ClientNavigateArgs();
      return ClientIsValidPrincipalInfo(args.target().principalInfo(),
                                        aLoadedOrigins) &&
             ClientIsValidPrincipalInfo(args.serviceWorker().principalInfo(),
                                        aLoadedOrigins);
    }

    case ClientOpConstructorArgs::TClientPostMessageArgs:
      return ClientIsValidPrincipalInfo(
          aArgs.get_ClientPostMessageArgs().serviceWorker().principalInfo(),
          aLoadedOrigins);

    case ClientOpConstructorArgs::TClientMatchAllArgs:
      return ClientIsValidPrincipalInfo(
          aArgs.get_ClientMatchAllArgs().serviceWorker().principalInfo(),
          aLoadedOrigins);

    case ClientOpConstructorArgs::TClientClaimArgs:
      return ClientIsValidPrincipalInfo(
          aArgs.get_ClientClaimArgs().serviceWorker().principalInfo(),
          aLoadedOrigins);

    case ClientOpConstructorArgs::TClientGetInfoAndStateArgs:
      return ClientIsValidPrincipalInfo(
          aArgs.get_ClientGetInfoAndStateArgs().principalInfo(),
          aLoadedOrigins);

    case ClientOpConstructorArgs::TClientOpenWindowArgs:
      return ClientIsValidPrincipalInfo(
          aArgs.get_ClientOpenWindowArgs().principalInfo(), aLoadedOrigins);

    case ClientOpConstructorArgs::TClientFocusArgs:
    case ClientOpConstructorArgs::TClientEvictBFCacheArgs:
      // No principals.
      return true;

    case ClientOpConstructorArgs::T__None:
      break;
  }

  MOZ_CRASH("Unhandled ClientOpConstructorArgs");
}

bool ClientIsValidCreationURL(const PrincipalInfo& aPrincipalInfo,
                              const nsACString& aURL) {
  RefPtr<MozURL> url;
  nsresult rv = MozURL::Init(getter_AddRefs(url), aURL);
  NS_ENSURE_SUCCESS(rv, false);

  switch (aPrincipalInfo.type()) {
    case PrincipalInfo::TContentPrincipalInfo: {
      // Any origin can create an about:blank or about:srcdoc Client.
      if (aURL.LowerCaseEqualsLiteral("about:blank") ||
          aURL.LowerCaseEqualsLiteral("about:srcdoc")) {
        return true;
      }

      const ContentPrincipalInfo& content =
          aPrincipalInfo.get_ContentPrincipalInfo();

      // Parse the principal origin URL as well.  This ensures any MozURL
      // parser issues effect both URLs equally.
      RefPtr<MozURL> principalURL;
      rv = MozURL::Init(getter_AddRefs(principalURL), content.originNoSuffix());
      NS_ENSURE_SUCCESS(rv, false);

      nsAutoCString origin;
      url->Origin(origin);

      nsAutoCString principalOrigin;
      principalURL->Origin(principalOrigin);

      // The vast majority of sites should simply result in the same principal
      // and URL origin.
      if (principalOrigin == origin) {
        return true;
      }

      nsDependentCSubstring scheme = url->Scheme();

      // Generally any origin can also open javascript: windows and workers.
      if (scheme.LowerCaseEqualsLiteral("javascript")) {
        return true;
      }

      // Otherwise don't support this URL type in the clients sub-system for
      // now.  This will exclude a variety of internal browser clients, but
      // currently we don't need to support those.  This function can be
      // expanded to handle more cases as necessary.
      return false;
    }
    case PrincipalInfo::TSystemPrincipalInfo: {
      nsDependentCSubstring scheme = url->Scheme();

      // While many types of documents can be created with a system principal,
      // there are only a few that can reasonably become windows.  We attempt
      // to validate the list of known cases here with a simple scheme check.
      return scheme.LowerCaseEqualsLiteral("about") ||
             scheme.LowerCaseEqualsLiteral("chrome") ||
             scheme.LowerCaseEqualsLiteral("resource") ||
             scheme.LowerCaseEqualsLiteral("blob") ||
             scheme.LowerCaseEqualsLiteral("javascript") ||
             scheme.LowerCaseEqualsLiteral("view-source");
    }
    case PrincipalInfo::TNullPrincipalInfo: {
      // A wide variety of clients can have a null principal.  For example,
      // sandboxed iframes can have a normal content URL.  For now allow
      // any parsable URL for null principals.  This is relatively safe since
      // null principals have unique origins and won't most ClientManagerService
      // queries anyway.
      return true;
    }
    default: {
      break;
    }
  }

  // Clients (windows/workers) should never have an expanded principal type.
  return false;
}

}  // namespace mozilla::dom
