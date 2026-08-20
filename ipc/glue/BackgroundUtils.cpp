/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BackgroundUtils.h"

#include "MainThreadUtils.h"
#include "mozilla/Assertions.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/ContentPrincipal.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/SystemPrincipal.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "mozilla/ipc/URIUtils.h"
#include "mozilla/net/CookieJarSettings.h"
#include "mozilla/net/InterceptionInfo.h"
#include "mozilla/net/NeckoChannelParams.h"
#include "ExpandedPrincipal.h"
#include "nsIContentPolicy.h"
#include "nsIScriptSecurityManager.h"
#include "nsIURI.h"
#include "nsNetUtil.h"
#include "mozilla/LoadInfo.h"
#include "nsContentUtils.h"
#include "nsString.h"
#include "nsTArray.h"
#include "mozilla/nsRedirectHistoryEntry.h"
#include "mozilla/dom/nsCSPUtils.h"
#include "mozilla/dom/nsCSPContext.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/PolicyContainer.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/LoadInfo.h"

using namespace mozilla::dom;
using namespace mozilla::net;

namespace mozilla {

namespace ipc {

Result<nsCOMPtr<nsIPrincipal>, nsresult> PrincipalInfoToPrincipal(
    const PrincipalInfo& aPrincipalInfo) {
  MOZ_ASSERT(aPrincipalInfo.type() != PrincipalInfo::T__None);

  nsCOMPtr<nsIPrincipal> principal;
  nsresult rv;

  switch (aPrincipalInfo.type()) {
    case PrincipalInfo::TSystemPrincipalInfo: {
      principal = SystemPrincipal::Get();
      if (NS_WARN_IF(!principal)) {
        return Err(NS_ERROR_NOT_INITIALIZED);
      }

      return principal;
    }

    case PrincipalInfo::TNullPrincipalInfo: {
      const NullPrincipalInfo& info = aPrincipalInfo.get_NullPrincipalInfo();

      nsCOMPtr<nsIURI> uri;
      rv = NS_NewURI(getter_AddRefs(uri), info.spec());
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return Err(rv);
      }

      if (!uri->SchemeIs(NS_NULLPRINCIPAL_SCHEME)) {
        return Err(NS_ERROR_ILLEGAL_VALUE);
      }

      principal = NullPrincipal::Create(info.attrs(), uri);
      return principal;
    }

    case PrincipalInfo::TContentPrincipalInfo: {
      const ContentPrincipalInfo& info =
          aPrincipalInfo.get_ContentPrincipalInfo();

      nsCOMPtr<nsIURI> uri;
      rv = NS_NewURI(getter_AddRefs(uri), info.spec());
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return Err(rv);
      }

      nsCOMPtr<nsIURI> domain;
      if (info.domain()) {
        rv = NS_NewURI(getter_AddRefs(domain), *info.domain());
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return Err(rv);
        }
      }

      principal =
          BasePrincipal::CreateContentPrincipal(uri, info.attrs(), domain);
      if (NS_WARN_IF(!principal)) {
        return Err(NS_ERROR_NULL_POINTER);
      }

      // Origin must match what the_new_principal.getOrigin returns.
      nsAutoCString originNoSuffix;
      rv = principal->GetOriginNoSuffix(originNoSuffix);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return Err(rv);
      }

      if (NS_WARN_IF(!info.originNoSuffix().Equals(originNoSuffix))) {
        return Err(NS_ERROR_FAILURE);
      }

      if (!info.baseDomain().IsVoid()) {
        nsAutoCString baseDomain;
        rv = principal->GetBaseDomain(baseDomain);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return Err(rv);
        }

        if (NS_WARN_IF(!info.baseDomain().Equals(baseDomain))) {
          return Err(NS_ERROR_FAILURE);
        }
      }
      return principal;
    }

    case PrincipalInfo::TExpandedPrincipalInfo: {
      const ExpandedPrincipalInfo& info =
          aPrincipalInfo.get_ExpandedPrincipalInfo();

      nsTArray<nsCOMPtr<nsIPrincipal>> allowlist;
      nsCOMPtr<nsIPrincipal> alPrincipal;

      for (uint32_t i = 0; i < info.allowlist().Length(); i++) {
        auto principalOrErr = PrincipalInfoToPrincipal(info.allowlist()[i]);
        if (NS_WARN_IF(principalOrErr.isErr())) {
          nsresult ret = principalOrErr.unwrapErr();
          return Err(ret);
        }
        // append that principal to the allowlist
        allowlist.AppendElement(principalOrErr.unwrap());
      }

      RefPtr<ExpandedPrincipal> expandedPrincipal =
          ExpandedPrincipal::Create(allowlist, info.attrs());
      if (!expandedPrincipal) {
        return Err(NS_ERROR_FAILURE);
      }

      principal = expandedPrincipal;
      return principal;
    }

    default:
      return Err(NS_ERROR_FAILURE);
  }
}

bool StorageKeysEqual(const PrincipalInfo& aLeft, const PrincipalInfo& aRight) {
  MOZ_RELEASE_ASSERT(aLeft.type() == PrincipalInfo::TContentPrincipalInfo ||
                     aLeft.type() == PrincipalInfo::TSystemPrincipalInfo);
  MOZ_RELEASE_ASSERT(aRight.type() == PrincipalInfo::TContentPrincipalInfo ||
                     aRight.type() == PrincipalInfo::TSystemPrincipalInfo);

  if (aLeft.type() != aRight.type()) {
    return false;
  }

  if (aLeft.type() == PrincipalInfo::TContentPrincipalInfo) {
    const ContentPrincipalInfo& leftContent = aLeft.get_ContentPrincipalInfo();
    const ContentPrincipalInfo& rightContent =
        aRight.get_ContentPrincipalInfo();

    return leftContent.attrs() == rightContent.attrs() &&
           leftContent.originNoSuffix() == rightContent.originNoSuffix();
  }

  // Storage keys for the System principal always equal.
  return true;
}

already_AddRefed<nsIContentSecurityPolicy> CSPInfoToCSP(
    const CSPInfo& aCSPInfo, Document* aRequestingDoc,
    nsresult* aOptionalResult) {
  MOZ_ASSERT(NS_IsMainThread());

  nsresult stackResult;
  nsresult& rv = aOptionalResult ? *aOptionalResult : stackResult;

  RefPtr csp = MakeRefPtr<nsCSPContext>();

  if (aRequestingDoc) {
    rv = csp->SetRequestContextWithDocument(aRequestingDoc);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return nullptr;
    }
  } else {
    auto principalOrErr =
        PrincipalInfoToPrincipal(aCSPInfo.requestPrincipalInfo());
    if (NS_WARN_IF(principalOrErr.isErr())) {
      return nullptr;
    }

    nsCOMPtr<nsIURI> selfURI;
    if (!aCSPInfo.selfURISpec().IsEmpty()) {
      rv = NS_NewURI(getter_AddRefs(selfURI), aCSPInfo.selfURISpec());
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return nullptr;
      }
    }

    nsCOMPtr<nsIPrincipal> principal = principalOrErr.unwrap();

    rv = csp->SetRequestContextWithPrincipal(
        principal, selfURI, aCSPInfo.referrer(), aCSPInfo.innerWindowID());
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return nullptr;
    }
  }
  csp->SetSkipAllowInlineStyleCheck(aCSPInfo.skipAllowInlineStyleCheck());

  for (uint32_t i = 0; i < aCSPInfo.policyInfos().Length(); i++) {
    csp->AddIPCPolicy(aCSPInfo.policyInfos()[i]);
  }
  return csp.forget();
}

nsresult CSPToCSPInfo(nsIContentSecurityPolicy* aCSP, CSPInfo* aCSPInfo) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aCSP);
  MOZ_ASSERT(aCSPInfo);

  if (!aCSP || !aCSPInfo) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIPrincipal> requestPrincipal = aCSP->GetRequestPrincipal();

  PrincipalInfo requestingPrincipalInfo;
  nsresult rv =
      PrincipalToPrincipalInfo(requestPrincipal, &requestingPrincipalInfo);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsCOMPtr<nsIURI> selfURI = aCSP->GetSelfURI();
  nsAutoCString selfURISpec;
  if (selfURI) {
    selfURI->GetSpec(selfURISpec);
  }

  nsAutoCString referrer;
  aCSP->GetReferrer(referrer);

  uint64_t windowID = aCSP->GetInnerWindowID();
  bool skipAllowInlineStyleCheck = aCSP->GetSkipAllowInlineStyleCheck();

  RequireTrustedTypesForDirectiveState requireTrustedTypesForDirectiveState =
      aCSP->GetRequireTrustedTypesForDirectiveState();

  nsTArray<ContentSecurityPolicy> policies;
  static_cast<nsCSPContext*>(aCSP)->SerializePolicies(policies);

  *aCSPInfo = CSPInfo(std::move(policies), requestingPrincipalInfo, selfURISpec,
                      referrer, windowID, requireTrustedTypesForDirectiveState,
                      skipAllowInlineStyleCheck);
  return NS_OK;
}

nsresult PrincipalToPrincipalInfo(nsIPrincipal* aPrincipal,
                                  PrincipalInfo* aPrincipalInfo,
                                  bool aSkipBaseDomain) {
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aPrincipalInfo);

  nsresult rv;
  if (aPrincipal->GetIsNullPrincipal()) {
    nsAutoCString spec;
    rv = aPrincipal->GetAsciiSpec(spec);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    *aPrincipalInfo =
        NullPrincipalInfo(aPrincipal->OriginAttributesRef(), spec);
    return NS_OK;
  }

  if (aPrincipal->IsSystemPrincipal()) {
    *aPrincipalInfo = SystemPrincipalInfo();
    return NS_OK;
  }

  // might be an expanded principal
  auto* basePrin = BasePrincipal::Cast(aPrincipal);
  if (basePrin->Is<ExpandedPrincipal>()) {
    auto* expanded = basePrin->As<ExpandedPrincipal>();

    nsTArray<PrincipalInfo> allowlistInfo;
    PrincipalInfo info;

    for (auto& prin : expanded->AllowList()) {
      rv = PrincipalToPrincipalInfo(prin, &info, aSkipBaseDomain);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return rv;
      }
      // append that spec to the allowlist
      allowlistInfo.AppendElement(info);
    }

    *aPrincipalInfo = ExpandedPrincipalInfo(aPrincipal->OriginAttributesRef(),
                                            std::move(allowlistInfo));
    return NS_OK;
  }

  nsAutoCString spec;
  rv = aPrincipal->GetAsciiSpec(spec);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsCString originNoSuffix;
  rv = aPrincipal->GetOriginNoSuffix(originNoSuffix);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  nsCOMPtr<nsIURI> domainUri;
  rv = aPrincipal->GetDomain(getter_AddRefs(domainUri));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  Maybe<nsCString> domain;
  if (domainUri) {
    domain.emplace();
    rv = domainUri->GetSpec(domain.ref());
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
  }

  // This attribute is not crucial.
  nsCString baseDomain;
  if (aSkipBaseDomain) {
    baseDomain.SetIsVoid(true);
  } else {
    if (NS_FAILED(aPrincipal->GetBaseDomain(baseDomain))) {
      // No warning here. Some principal URLs do not have a base-domain.
      baseDomain.SetIsVoid(true);
    }
  }

  *aPrincipalInfo =
      ContentPrincipalInfo(aPrincipal->OriginAttributesRef(), originNoSuffix,
                           spec, domain, baseDomain);
  return NS_OK;
}

bool IsPrincipalInfoPrivate(const PrincipalInfo& aPrincipalInfo) {
  if (aPrincipalInfo.type() != ipc::PrincipalInfo::TContentPrincipalInfo) {
    return false;
  }

  const ContentPrincipalInfo& info = aPrincipalInfo.get_ContentPrincipalInfo();
  return info.attrs().IsPrivateBrowsing();
}

already_AddRefed<nsIRedirectHistoryEntry> RHEntryInfoToRHEntry(
    const RedirectHistoryEntryInfo& aRHEntryInfo) {
  auto principalOrErr = PrincipalInfoToPrincipal(aRHEntryInfo.principalInfo());
  if (NS_WARN_IF(principalOrErr.isErr())) {
    return nullptr;
  }

  nsCOMPtr<nsIPrincipal> principal = principalOrErr.unwrap();
  nsCOMPtr<nsIURI> referrerUri = DeserializeURI(aRHEntryInfo.referrerUri());

  nsCOMPtr<nsIRedirectHistoryEntry> entry = new nsRedirectHistoryEntry(
      principal, referrerUri, aRHEntryInfo.remoteAddress());

  return entry.forget();
}

nsresult RHEntryToRHEntryInfo(nsIRedirectHistoryEntry* aRHEntry,
                              RedirectHistoryEntryInfo* aRHEntryInfo) {
  MOZ_ASSERT(aRHEntry);
  MOZ_ASSERT(aRHEntryInfo);

  nsresult rv;
  aRHEntry->GetRemoteAddress(aRHEntryInfo->remoteAddress());

  nsCOMPtr<nsIURI> referrerUri;
  rv = aRHEntry->GetReferrerURI(getter_AddRefs(referrerUri));
  NS_ENSURE_SUCCESS(rv, rv);
  SerializeURI(referrerUri, aRHEntryInfo->referrerUri());

  nsCOMPtr<nsIPrincipal> principal;
  rv = aRHEntry->GetPrincipal(getter_AddRefs(principal));
  NS_ENSURE_SUCCESS(rv, rv);

  return PrincipalToPrincipalInfo(principal, &aRHEntryInfo->principalInfo());
}

nsresult LoadInfoToLoadInfoArgs(nsILoadInfo* aLoadInfo,
                                LoadInfoArgs* outLoadInfoArgs) {
  nsresult rv = NS_OK;
  Maybe<PrincipalInfo> loadingPrincipalInfo;
  if (nsIPrincipal* loadingPrin = aLoadInfo->GetLoadingPrincipal()) {
    loadingPrincipalInfo.emplace();
    rv = PrincipalToPrincipalInfo(loadingPrin, loadingPrincipalInfo.ptr());
    NS_ENSURE_SUCCESS(rv, rv);
  }

  PrincipalInfo triggeringPrincipalInfo;
  rv = PrincipalToPrincipalInfo(aLoadInfo->TriggeringPrincipal(),
                                &triggeringPrincipalInfo);
  NS_ENSURE_SUCCESS(rv, rv);

  Maybe<PrincipalInfo> principalToInheritInfo;
  if (nsIPrincipal* principalToInherit = aLoadInfo->PrincipalToInherit()) {
    principalToInheritInfo.emplace();
    rv = PrincipalToPrincipalInfo(principalToInherit,
                                  principalToInheritInfo.ptr());
    NS_ENSURE_SUCCESS(rv, rv);
  }

  Maybe<PrincipalInfo> topLevelPrincipalInfo;
  if (nsIPrincipal* topLevenPrin = aLoadInfo->GetTopLevelPrincipal()) {
    topLevelPrincipalInfo.emplace();
    rv = PrincipalToPrincipalInfo(topLevenPrin, topLevelPrincipalInfo.ptr());
    NS_ENSURE_SUCCESS(rv, rv);
  }

  Maybe<URIParams> optionalResultPrincipalURI;
  nsCOMPtr<nsIURI> resultPrincipalURI;
  (void)aLoadInfo->GetResultPrincipalURI(getter_AddRefs(resultPrincipalURI));
  if (resultPrincipalURI) {
    SerializeURI(resultPrincipalURI, optionalResultPrincipalURI);
  }

  nsCString triggeringRemoteType;
  rv = aLoadInfo->GetTriggeringRemoteType(triggeringRemoteType);
  NS_ENSURE_SUCCESS(rv, rv);

  nsTArray<RedirectHistoryEntryInfo> redirectChainIncludingInternalRedirects;
  for (const nsCOMPtr<nsIRedirectHistoryEntry>& redirectEntry :
       aLoadInfo->RedirectChainIncludingInternalRedirects()) {
    RedirectHistoryEntryInfo* entry =
        redirectChainIncludingInternalRedirects.AppendElement();
    rv = RHEntryToRHEntryInfo(redirectEntry, entry);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  nsTArray<RedirectHistoryEntryInfo> redirectChain;
  for (const nsCOMPtr<nsIRedirectHistoryEntry>& redirectEntry :
       aLoadInfo->RedirectChain()) {
    RedirectHistoryEntryInfo* entry = redirectChain.AppendElement();
    rv = RHEntryToRHEntryInfo(redirectEntry, entry);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  Maybe<IPCClientInfo> ipcClientInfo;
  const Maybe<ClientInfo>& clientInfo = aLoadInfo->GetClientInfo();
  if (clientInfo.isSome()) {
    ipcClientInfo.emplace(clientInfo.ref().ToIPC());
  }

  Maybe<IPCClientInfo> ipcReservedClientInfo;
  const Maybe<ClientInfo>& reservedClientInfo =
      aLoadInfo->GetReservedClientInfo();
  if (reservedClientInfo.isSome()) {
    ipcReservedClientInfo.emplace(reservedClientInfo.ref().ToIPC());
  }

  Maybe<IPCClientInfo> ipcInitialClientInfo;
  const Maybe<ClientInfo>& initialClientInfo =
      aLoadInfo->GetInitialClientInfo();
  if (initialClientInfo.isSome()) {
    ipcInitialClientInfo.emplace(initialClientInfo.ref().ToIPC());
  }

  Maybe<IPCServiceWorkerDescriptor> ipcController;
  const Maybe<ServiceWorkerDescriptor>& controller = aLoadInfo->GetController();
  if (controller.isSome()) {
    ipcController.emplace(controller.ref().ToIPC());
  }

  nsAutoString cspNonce;
  (void)NS_WARN_IF(NS_FAILED(aLoadInfo->GetCspNonce(cspNonce)));

  nsAutoString integrityMetadata;
  (void)NS_WARN_IF(
      NS_FAILED(aLoadInfo->GetIntegrityMetadata(integrityMetadata)));

  nsCOMPtr<nsICookieJarSettings> cookieJarSettings;
  rv = aLoadInfo->GetCookieJarSettings(getter_AddRefs(cookieJarSettings));
  NS_ENSURE_SUCCESS(rv, rv);

  CookieJarSettingsArgs cookieJarSettingsArgs;
  static_cast<CookieJarSettings*>(cookieJarSettings.get())
      ->Serialize(cookieJarSettingsArgs);

  nsCOMPtr<nsIURI> unstrippedURI;
  (void)aLoadInfo->GetUnstrippedURI(getter_AddRefs(unstrippedURI));

  Maybe<bool> isThirdPartyContextToTopWindow;
  if (static_cast<LoadInfo*>(aLoadInfo)
          ->HasIsThirdPartyContextToTopWindowSet()) {
    isThirdPartyContextToTopWindow.emplace(
        aLoadInfo->GetIsThirdPartyContextToTopWindow());
  }

  Maybe<InterceptionInfoArg> interceptionInfoArg;
  nsIInterceptionInfo* interceptionInfo = aLoadInfo->InterceptionInfo();
  if (interceptionInfo) {
    Maybe<PrincipalInfo> triggeringPrincipalInfo;
    if (interceptionInfo->TriggeringPrincipal()) {
      triggeringPrincipalInfo.emplace();
      rv = PrincipalToPrincipalInfo(interceptionInfo->TriggeringPrincipal(),
                                    triggeringPrincipalInfo.ptr());
    }

    nsTArray<RedirectHistoryEntryInfo> redirectChain;
    for (const nsCOMPtr<nsIRedirectHistoryEntry>& redirectEntry :
         interceptionInfo->RedirectChain()) {
      RedirectHistoryEntryInfo* entry = redirectChain.AppendElement();
      rv = RHEntryToRHEntryInfo(redirectEntry, entry);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    interceptionInfoArg = Some(InterceptionInfoArg(
        triggeringPrincipalInfo, interceptionInfo->ContentPolicyType(),
        redirectChain, interceptionInfo->FromThirdParty()));
  }

  Maybe<RFPTargetSet> overriddenFingerprintingSettingsArg;
  Maybe<RFPTargetSet> overriddenFingerprintingSettings =
      aLoadInfo->GetOverriddenFingerprintingSettings();

  if (overriddenFingerprintingSettings) {
    overriddenFingerprintingSettingsArg =
        Some(overriddenFingerprintingSettings.ref());
  }

  Maybe<RequestMode> requestMode;
  aLoadInfo->GetRequestMode(&requestMode);

  Maybe<PolicyContainerArgs> maybePolicyContainerToInherit;
  nsCOMPtr<nsIPolicyContainer> policyContainerToInherit =
      aLoadInfo->GetPolicyContainerToInherit();
  if (policyContainerToInherit) {
    PolicyContainerArgs args;
    PolicyContainer::ToArgs(PolicyContainer::Cast(policyContainerToInherit),
                            args);
    maybePolicyContainerToInherit.emplace(args);
  }

  *outLoadInfoArgs = LoadInfoArgs(
      loadingPrincipalInfo, triggeringPrincipalInfo, principalToInheritInfo,
      topLevelPrincipalInfo, optionalResultPrincipalURI, triggeringRemoteType,
      aLoadInfo->GetSandboxedNullPrincipalID(), aLoadInfo->GetSecurityFlags(),
      aLoadInfo->GetSandboxFlags(), aLoadInfo->GetTriggeringSandboxFlags(),
      aLoadInfo->GetTriggeringWindowId(),
      aLoadInfo->GetTriggeringStorageAccess(),
      aLoadInfo->GetTriggeringFirstPartyClassificationFlags(),
      aLoadInfo->GetTriggeringThirdPartyClassificationFlags(),
      aLoadInfo->InternalContentPolicyType(),
      static_cast<uint32_t>(aLoadInfo->GetTainting()),
      aLoadInfo->GetBlockAllMixedContent(),
      aLoadInfo->GetUpgradeInsecureRequests(),
      aLoadInfo->GetBrowserUpgradeInsecureRequests(),
      aLoadInfo->GetBrowserDidUpgradeInsecureRequests(),
      aLoadInfo->GetBrowserWouldUpgradeInsecureRequests(),
      aLoadInfo->GetForceAllowDataURI(),
      aLoadInfo->GetAllowInsecureRedirectToDataURI(),
      aLoadInfo->GetForceMediaDocument(),
      aLoadInfo->GetSkipContentPolicyCheckForWebRequest(),
      aLoadInfo->GetOriginalFrameSrcLoad(),
      aLoadInfo->GetForceInheritPrincipalDropped(),
      aLoadInfo->GetInnerWindowID(), aLoadInfo->GetBrowsingContextID(),
      aLoadInfo->GetAssociatedBrowsingContextID(),
      aLoadInfo->GetFrameBrowsingContextID(),
      aLoadInfo->GetInitialSecurityCheckDone(),
      aLoadInfo->GetIsInThirdPartyContext(), isThirdPartyContextToTopWindow,
      aLoadInfo->GetIsOn3PCBExceptionList(), aLoadInfo->GetIsFormSubmission(),
      aLoadInfo->GetIsGETRequest(), aLoadInfo->GetSendCSPViolationEvents(),
      aLoadInfo->GetOriginAttributes(), redirectChainIncludingInternalRedirects,
      redirectChain, aLoadInfo->GetSchemelessInput(),
      aLoadInfo->GetHttpsUpgradeTelemetry(), ipcClientInfo,
      ipcReservedClientInfo, ipcInitialClientInfo, ipcController,
      aLoadInfo->CorsUnsafeHeaders(), aLoadInfo->GetForcePreflight(),
      aLoadInfo->GetIsPreflight(), aLoadInfo->GetLoadTriggeredFromExternal(),
      aLoadInfo->GetServiceWorkerTaintingSynthesized(),
      aLoadInfo->GetDocumentHasUserInteracted(),
      aLoadInfo->GetAllowListFutureDocumentsCreatedFromThisRedirectChain(),
      aLoadInfo->GetNeedForCheckingAntiTrackingHeuristic(), cspNonce,
      integrityMetadata, aLoadInfo->GetSkipContentSniffing(),
      aLoadInfo->GetHttpsOnlyStatus(), aLoadInfo->GetHstsStatus(),
      aLoadInfo->GetHasValidUserGestureActivation(),
      aLoadInfo->GetTextDirectiveUserActivation(),
      aLoadInfo->GetAllowDeprecatedSystemRequests(),
      aLoadInfo->GetIsInDevToolsContext(), aLoadInfo->GetParserCreatedScript(),
      requestMode, aLoadInfo->GetIsFromProcessingFrameAttributes(),
      aLoadInfo->GetIsMediaRequest(), aLoadInfo->GetIsFromObjectOrEmbed(),
      cookieJarSettingsArgs, aLoadInfo->GetRequestBlockingReason(),
      maybePolicyContainerToInherit, aLoadInfo->GetStoragePermission(),
      aLoadInfo->GetParentIpAddressSpace(), aLoadInfo->GetIpAddressSpace(),
      overriddenFingerprintingSettingsArg, aLoadInfo->GetIsMetaRefresh(),
      aLoadInfo->GetActivatedFromNavigationalPrefetch(),
      aLoadInfo->GetLoadingEmbedderPolicy(),
      aLoadInfo->GetIsOriginTrialCoepCredentiallessEnabledForTopLevel(),
      unstrippedURI, interceptionInfoArg, aLoadInfo->GetIsNewWindowTarget(),
      aLoadInfo->GetUserNavigationInvolvement(),
      aLoadInfo->GetContainerFeaturePolicyInfo(), {});

  return NS_OK;
}

nsresult LoadInfoArgsToLoadInfo(const LoadInfoArgs& aLoadInfoArgs,
                                const nsACString& aOriginRemoteType,
                                nsILoadInfo** outLoadInfo) {
  return LoadInfoArgsToLoadInfo(aLoadInfoArgs, aOriginRemoteType, nullptr,
                                outLoadInfo);
}
nsresult LoadInfoArgsToLoadInfo(const LoadInfoArgs& aLoadInfoArgs,
                                const nsACString& aOriginRemoteType,
                                nsINode* aCspToInheritLoadingContext,
                                nsILoadInfo** outLoadInfo) {
  RefPtr<LoadInfo> loadInfo;
  nsresult rv = LoadInfoArgsToLoadInfo(aLoadInfoArgs, aOriginRemoteType,
                                       aCspToInheritLoadingContext,
                                       getter_AddRefs(loadInfo));
  NS_ENSURE_SUCCESS(rv, rv);

  loadInfo.forget(outLoadInfo);
  return NS_OK;
}

nsresult LoadInfoArgsToLoadInfo(const LoadInfoArgs& aLoadInfoArgs,
                                const nsACString& aOriginRemoteType,
                                LoadInfo** outLoadInfo) {
  return LoadInfoArgsToLoadInfo(aLoadInfoArgs, aOriginRemoteType, nullptr,
                                outLoadInfo);
}
nsresult LoadInfoArgsToLoadInfo(const LoadInfoArgs& loadInfoArgs,
                                const nsACString& aOriginRemoteType,
                                nsINode* aCspToInheritLoadingContext,
                                LoadInfo** outLoadInfo) {
  nsCOMPtr<nsIPrincipal> loadingPrincipal;
  if (loadInfoArgs.requestingPrincipalInfo().isSome()) {
    auto loadingPrincipalOrErr =
        PrincipalInfoToPrincipal(loadInfoArgs.requestingPrincipalInfo().ref());
    if (NS_WARN_IF(loadingPrincipalOrErr.isErr())) {
      return loadingPrincipalOrErr.unwrapErr();
    }
    loadingPrincipal = loadingPrincipalOrErr.unwrap();
  }

  auto triggeringPrincipalOrErr =
      PrincipalInfoToPrincipal(loadInfoArgs.triggeringPrincipalInfo());
  if (NS_WARN_IF(triggeringPrincipalOrErr.isErr())) {
    return triggeringPrincipalOrErr.unwrapErr();
  }
  nsCOMPtr<nsIPrincipal> triggeringPrincipal =
      triggeringPrincipalOrErr.unwrap();

  nsCOMPtr<nsIPrincipal> principalToInherit;
  nsCOMPtr<nsIPrincipal> flattenedPrincipalToInherit;
  if (loadInfoArgs.principalToInheritInfo().isSome()) {
    auto principalToInheritOrErr =
        PrincipalInfoToPrincipal(loadInfoArgs.principalToInheritInfo().ref());
    if (NS_WARN_IF(principalToInheritOrErr.isErr())) {
      return principalToInheritOrErr.unwrapErr();
    }
    flattenedPrincipalToInherit = principalToInheritOrErr.unwrap();
  }

  if (XRE_IsContentProcess()) {
    auto targetBrowsingContextId = loadInfoArgs.frameBrowsingContextID()
                                       ? loadInfoArgs.frameBrowsingContextID()
                                       : loadInfoArgs.browsingContextID();
    if (RefPtr<BrowsingContext> bc =
            BrowsingContext::Get(targetBrowsingContextId)) {
      auto [originalTriggeringPrincipal, originalPrincipalToInherit] =
          bc->GetTriggeringAndInheritPrincipalsForCurrentLoad();

      if (originalTriggeringPrincipal &&
          originalTriggeringPrincipal->Equals(triggeringPrincipal)) {
        triggeringPrincipal = originalTriggeringPrincipal;
      }
      if (originalPrincipalToInherit &&
          (loadInfoArgs.securityFlags() &
           nsILoadInfo::SEC_FORCE_INHERIT_PRINCIPAL) &&
          originalPrincipalToInherit->Equals(flattenedPrincipalToInherit)) {
        principalToInherit = originalPrincipalToInherit;
      }
    }
  }
  if (!principalToInherit && loadInfoArgs.principalToInheritInfo().isSome()) {
    principalToInherit = flattenedPrincipalToInherit;
  }

  nsCOMPtr<nsIPrincipal> topLevelPrincipal;
  if (loadInfoArgs.topLevelPrincipalInfo().isSome()) {
    auto topLevelPrincipalOrErr =
        PrincipalInfoToPrincipal(loadInfoArgs.topLevelPrincipalInfo().ref());
    if (NS_WARN_IF(topLevelPrincipalOrErr.isErr())) {
      return topLevelPrincipalOrErr.unwrapErr();
    }
    topLevelPrincipal = topLevelPrincipalOrErr.unwrap();
  }

  nsCOMPtr<nsIURI> resultPrincipalURI;
  if (loadInfoArgs.resultPrincipalURI().isSome()) {
    resultPrincipalURI = DeserializeURI(loadInfoArgs.resultPrincipalURI());
    NS_ENSURE_TRUE(resultPrincipalURI, NS_ERROR_UNEXPECTED);
  }

  // If we received this message from a content process, reset
  // triggeringRemoteType to the process which sent us the message. If the
  // parent sent us the message, we trust it to provide the correct triggering
  // remote type.
  //
  // This means that the triggering remote type will be reset if a LoadInfo is
  // bounced through a content process, as the LoadInfo can no longer be
  // validated to be coming from the originally specified remote type.
  nsCString triggeringRemoteType = loadInfoArgs.triggeringRemoteType();
  if (aOriginRemoteType != NOT_REMOTE_TYPE &&
      aOriginRemoteType != triggeringRemoteType) {
    triggeringRemoteType = aOriginRemoteType;
  }

  RedirectHistoryArray redirectChainIncludingInternalRedirects;
  for (const RedirectHistoryEntryInfo& entryInfo :
       loadInfoArgs.redirectChainIncludingInternalRedirects()) {
    nsCOMPtr<nsIRedirectHistoryEntry> redirectHistoryEntry =
        RHEntryInfoToRHEntry(entryInfo);
    NS_ENSURE_TRUE(redirectHistoryEntry, NS_ERROR_UNEXPECTED);
    redirectChainIncludingInternalRedirects.AppendElement(
        redirectHistoryEntry.forget());
  }

  RedirectHistoryArray redirectChain;
  for (const RedirectHistoryEntryInfo& entryInfo :
       loadInfoArgs.redirectChain()) {
    nsCOMPtr<nsIRedirectHistoryEntry> redirectHistoryEntry =
        RHEntryInfoToRHEntry(entryInfo);
    NS_ENSURE_TRUE(redirectHistoryEntry, NS_ERROR_UNEXPECTED);
    redirectChain.AppendElement(redirectHistoryEntry.forget());
  }
  nsTArray<nsCOMPtr<nsIPrincipal>> ancestorPrincipals;
  nsTArray<uint64_t> ancestorBrowsingContextIDs;
  if (XRE_IsParentProcess() &&
      (nsContentUtils::InternalContentPolicyTypeToExternal(
           loadInfoArgs.contentPolicyType()) !=
       ExtContentPolicy::TYPE_DOCUMENT)) {
    // Only fill out ancestor principals and browsing context IDs when we
    // are deserializing LoadInfoArgs to be LoadInfo for a subresource
    RefPtr<BrowsingContext> parentBC =
        BrowsingContext::Get(loadInfoArgs.browsingContextID());
    if (parentBC) {
      LoadInfo::ComputeAncestors(parentBC->Canonical(), ancestorPrincipals,
                                 ancestorBrowsingContextIDs);
    }
  } else {
    // Fill out (possibly redacted) ancestor principals for
    // Location.ancestorOrigins
    for (const auto& principalInfo : loadInfoArgs.ancestorOrigins()) {
      if (principalInfo.isNothing()) {
        ancestorPrincipals.AppendElement(nullptr);
      } else {
        auto principal = PrincipalInfoToPrincipal(principalInfo.value());
        // If this operation fail, we censor the origin.
        ancestorPrincipals.AppendElement(principal.unwrapOr(nullptr));
      }
    }
  }

  Maybe<ClientInfo> clientInfo;
  if (loadInfoArgs.clientInfo().isSome()) {
    clientInfo.emplace(ClientInfo(loadInfoArgs.clientInfo().ref()));
  }

  Maybe<ClientInfo> reservedClientInfo;
  if (loadInfoArgs.reservedClientInfo().isSome()) {
    reservedClientInfo.emplace(
        ClientInfo(loadInfoArgs.reservedClientInfo().ref()));
  }

  Maybe<ClientInfo> initialClientInfo;
  if (loadInfoArgs.initialClientInfo().isSome()) {
    initialClientInfo.emplace(
        ClientInfo(loadInfoArgs.initialClientInfo().ref()));
  }

  // We can have an initial client info or a reserved client info, but not both.
  MOZ_DIAGNOSTIC_ASSERT(reservedClientInfo.isNothing() ||
                        initialClientInfo.isNothing());
  NS_ENSURE_TRUE(
      reservedClientInfo.isNothing() || initialClientInfo.isNothing(),
      NS_ERROR_UNEXPECTED);

  Maybe<ServiceWorkerDescriptor> controller;
  if (loadInfoArgs.controller().isSome()) {
    controller.emplace(
        ServiceWorkerDescriptor(loadInfoArgs.controller().ref()));
  }

  nsCOMPtr<nsICookieJarSettings> cookieJarSettings;
  CookieJarSettings::Deserialize(loadInfoArgs.cookieJarSettings(),
                                 getter_AddRefs(cookieJarSettings));

  Maybe<RFPTargetSet> overriddenFingerprintingSettings;
  if (loadInfoArgs.overriddenFingerprintingSettings().isSome()) {
    overriddenFingerprintingSettings.emplace(
        loadInfoArgs.overriddenFingerprintingSettings().ref());
  }

  // Restore the loadingContext for frames using the BrowsingContext's
  // embedder element. Note that this only works if the embedder is
  // same-process, so won't be fission compatible.
  nsCOMPtr<nsINode> loadingContext;
  RefPtr<BrowsingContext> frameBrowsingContext =
      BrowsingContext::Get(loadInfoArgs.frameBrowsingContextID());
  if (frameBrowsingContext) {
    loadingContext = frameBrowsingContext->GetEmbedderElement();
  }

  Maybe<bool> isThirdPartyContextToTopWindow;
  if (loadInfoArgs.isThirdPartyContextToTopWindow().isSome()) {
    isThirdPartyContextToTopWindow.emplace(
        loadInfoArgs.isThirdPartyContextToTopWindow().ref());
  }

  nsCOMPtr<nsIInterceptionInfo> interceptionInfo;
  if (loadInfoArgs.interceptionInfo().isSome()) {
    const InterceptionInfoArg& interceptionInfoArg =
        loadInfoArgs.interceptionInfo().ref();
    nsCOMPtr<nsIPrincipal> triggeringPrincipal;
    if (interceptionInfoArg.triggeringPrincipalInfo().isSome()) {
      auto triggeringPrincipalOrErr = PrincipalInfoToPrincipal(
          interceptionInfoArg.triggeringPrincipalInfo().ref());
      if (NS_WARN_IF(triggeringPrincipalOrErr.isErr())) {
        return triggeringPrincipalOrErr.unwrapErr();
      }
      triggeringPrincipal = triggeringPrincipalOrErr.unwrap();
    }

    RedirectHistoryArray redirectChain;
    for (const RedirectHistoryEntryInfo& entryInfo :
         interceptionInfoArg.redirectChain()) {
      nsCOMPtr<nsIRedirectHistoryEntry> redirectHistoryEntry =
          RHEntryInfoToRHEntry(entryInfo);
      NS_ENSURE_TRUE(redirectHistoryEntry, NS_ERROR_UNEXPECTED);
      redirectChain.AppendElement(redirectHistoryEntry.forget());
    }

    interceptionInfo = new InterceptionInfo(
        triggeringPrincipal, interceptionInfoArg.contentPolicyType(),
        redirectChain, interceptionInfoArg.fromThirdParty());
  }

  RefPtr<PolicyContainer> policyContainerToInherit;
  const auto& policyContainerToInheritArgs =
      loadInfoArgs.policyContainerToInherit();
  if (policyContainerToInheritArgs.isSome()) {
    nsCOMPtr<Document> doc = do_QueryInterface(aCspToInheritLoadingContext);
    PolicyContainer::FromArgs(policyContainerToInheritArgs.ref(), doc,
                              getter_AddRefs(policyContainerToInherit));
  }

  RefPtr<mozilla::net::LoadInfo> loadInfo = new mozilla::net::LoadInfo(
      loadingPrincipal, triggeringPrincipal, principalToInherit,
      topLevelPrincipal, resultPrincipalURI, cookieJarSettings,
      policyContainerToInherit, loadInfoArgs.containerFeaturePolicyInfo(),
      triggeringRemoteType, loadInfoArgs.sandboxedNullPrincipalID(), clientInfo,
      reservedClientInfo, initialClientInfo, controller,
      loadInfoArgs.securityFlags(), loadInfoArgs.sandboxFlags(),
      loadInfoArgs.contentPolicyType(),
      loadInfoArgs.serviceWorkerTaintingSynthesized(),
      static_cast<LoadTainting>(loadInfoArgs.tainting()),

#define DEFINE_ARGUMENT(_t, _n, name, _d) loadInfoArgs.name(),
      LOADINFO_FOR_EACH_FIELD(DEFINE_ARGUMENT, LOADINFO_DUMMY_SETTER)
#undef DEFINE_ARGUMENT

          loadInfoArgs.initialSecurityCheckDone(),
      loadInfoArgs.isInThirdPartyContext(), isThirdPartyContextToTopWindow,
      loadInfoArgs.originAttributes(),
      std::move(redirectChainIncludingInternalRedirects),
      std::move(redirectChain), std::move(ancestorPrincipals),
      ancestorBrowsingContextIDs, loadInfoArgs.corsUnsafeHeaders(),
      loadInfoArgs.loadTriggeredFromExternal(), loadInfoArgs.cspNonce(),
      loadInfoArgs.integrityMetadata(),
      // This function is only called for moving LoadInfo across processes.
      // Same-document navigation won't cross process boundaries.
      /* aIsSameDocumentNavigation */ false, overriddenFingerprintingSettings,
      loadingContext, loadInfoArgs.unstrippedURI(), interceptionInfo,
      loadInfoArgs.schemelessInput(), loadInfoArgs.userNavigationInvolvement());

  loadInfo.forget(outLoadInfo);
  return NS_OK;
}

void LoadInfoToParentLoadInfoForwarder(
    nsILoadInfo* aLoadInfo, ParentLoadInfoForwarderArgs* aForwarderArgsOut) {
  Maybe<IPCServiceWorkerDescriptor> ipcController;
  Maybe<ServiceWorkerDescriptor> controller(aLoadInfo->GetController());
  if (controller.isSome()) {
    ipcController.emplace(controller.ref().ToIPC());
  }

  uint32_t tainting = nsILoadInfo::TAINTING_BASIC;
  (void)aLoadInfo->GetTainting(&tainting);

  Maybe<CookieJarSettingsArgs> cookieJarSettingsArgs;

  nsCOMPtr<nsICookieJarSettings> cookieJarSettings;
  nsresult rv =
      aLoadInfo->GetCookieJarSettings(getter_AddRefs(cookieJarSettings));
  CookieJarSettings* cs =
      static_cast<CookieJarSettings*>(cookieJarSettings.get());
  if (NS_SUCCEEDED(rv) && cookieJarSettings && cs->HasBeenChanged()) {
    CookieJarSettingsArgs args;
    cs->Serialize(args);
    cookieJarSettingsArgs = Some(args);
  }

  nsCOMPtr<nsIURI> unstrippedURI;
  (void)aLoadInfo->GetUnstrippedURI(getter_AddRefs(unstrippedURI));

  Maybe<bool> isThirdPartyContextToTopWindow;
  if (static_cast<LoadInfo*>(aLoadInfo)
          ->HasIsThirdPartyContextToTopWindowSet()) {
    isThirdPartyContextToTopWindow.emplace(
        aLoadInfo->GetIsThirdPartyContextToTopWindow());
  }

  Maybe<RFPTargetSet> overriddenFingerprintingSettingsArg;
  Maybe<RFPTargetSet> overriddenFingerprintingSettings =
      aLoadInfo->GetOverriddenFingerprintingSettings();

  if (overriddenFingerprintingSettings) {
    overriddenFingerprintingSettingsArg =
        Some(overriddenFingerprintingSettings.ref());
  }

  Maybe<RequestMode> requestMode;
  aLoadInfo->GetRequestMode(&requestMode);

  *aForwarderArgsOut = ParentLoadInfoForwarderArgs(
      aLoadInfo->GetAllowInsecureRedirectToDataURI(), ipcController, tainting,
      aLoadInfo->GetSkipContentSniffing(), aLoadInfo->GetHttpsOnlyStatus(),
      aLoadInfo->GetSchemelessInput(), aLoadInfo->GetHttpsUpgradeTelemetry(),
      aLoadInfo->GetHstsStatus(), aLoadInfo->GetHasValidUserGestureActivation(),
      aLoadInfo->GetTextDirectiveUserActivation(),
      aLoadInfo->GetAllowDeprecatedSystemRequests(),
      aLoadInfo->GetIsInDevToolsContext(), aLoadInfo->GetParserCreatedScript(),
      requestMode, aLoadInfo->GetTriggeringSandboxFlags(),
      aLoadInfo->GetTriggeringWindowId(),
      aLoadInfo->GetTriggeringStorageAccess(),
      aLoadInfo->GetServiceWorkerTaintingSynthesized(),
      aLoadInfo->GetDocumentHasUserInteracted(),
      aLoadInfo->GetAllowListFutureDocumentsCreatedFromThisRedirectChain(),
      cookieJarSettingsArgs, aLoadInfo->GetContainerFeaturePolicyInfo(),
      aLoadInfo->GetRequestBlockingReason(), aLoadInfo->GetStoragePermission(),
      overriddenFingerprintingSettingsArg, aLoadInfo->GetIsMetaRefresh(),
      isThirdPartyContextToTopWindow, aLoadInfo->GetIsInThirdPartyContext(),
      aLoadInfo->GetIsOn3PCBExceptionList(), unstrippedURI,
      aLoadInfo->GetUserNavigationInvolvement());
}

nsresult MergeParentLoadInfoForwarder(
    ParentLoadInfoForwarderArgs const& aForwarderArgs, nsILoadInfo* aLoadInfo) {
  nsresult rv;

  rv = aLoadInfo->SetAllowInsecureRedirectToDataURI(
      aForwarderArgs.allowInsecureRedirectToDataURI());
  NS_ENSURE_SUCCESS(rv, rv);

  aLoadInfo->ClearController();
  auto& controller = aForwarderArgs.controller();
  if (controller.isSome()) {
    aLoadInfo->SetController(ServiceWorkerDescriptor(controller.ref()));
  }

  if (aForwarderArgs.serviceWorkerTaintingSynthesized()) {
    aLoadInfo->SynthesizeServiceWorkerTainting(
        static_cast<LoadTainting>(aForwarderArgs.tainting()));
  } else {
    aLoadInfo->MaybeIncreaseTainting(aForwarderArgs.tainting());
  }
  rv = aLoadInfo->SetTextDirectiveUserActivation(
      aForwarderArgs.textDirectiveUserActivation());
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aLoadInfo->SetSkipContentSniffing(aForwarderArgs.skipContentSniffing());
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aLoadInfo->SetHttpsOnlyStatus(aForwarderArgs.httpsOnlyStatus());
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aLoadInfo->SetSchemelessInput(aForwarderArgs.schemelessInput());
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aLoadInfo->SetHttpsUpgradeTelemetry(
      aForwarderArgs.httpsUpgradeTelemetry());
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aLoadInfo->SetHstsStatus(aForwarderArgs.hstsStatus());
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aLoadInfo->SetTriggeringSandboxFlags(
      aForwarderArgs.triggeringSandboxFlags());
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aLoadInfo->SetTriggeringWindowId(aForwarderArgs.triggeringWindowId());
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aLoadInfo->SetTriggeringStorageAccess(
      aForwarderArgs.triggeringStorageAccess());
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aLoadInfo->SetHasValidUserGestureActivation(
      aForwarderArgs.hasValidUserGestureActivation());
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aLoadInfo->SetAllowDeprecatedSystemRequests(
      aForwarderArgs.allowDeprecatedSystemRequests());
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aLoadInfo->SetIsInDevToolsContext(aForwarderArgs.isInDevToolsContext());
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aLoadInfo->SetParserCreatedScript(aForwarderArgs.parserCreatedScript());
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aLoadInfo->SetRequestMode(aForwarderArgs.requestMode());
  NS_ENSURE_SUCCESS(rv, rv);

  MOZ_ALWAYS_SUCCEEDS(aLoadInfo->SetDocumentHasUserInteracted(
      aForwarderArgs.documentHasUserInteracted()));
  MOZ_ALWAYS_SUCCEEDS(
      aLoadInfo->SetAllowListFutureDocumentsCreatedFromThisRedirectChain(
          aForwarderArgs
              .allowListFutureDocumentsCreatedFromThisRedirectChain()));
  MOZ_ALWAYS_SUCCEEDS(aLoadInfo->SetRequestBlockingReason(
      aForwarderArgs.requestBlockingReason()));

  const Maybe<CookieJarSettingsArgs>& cookieJarSettingsArgs =
      aForwarderArgs.cookieJarSettings();
  if (cookieJarSettingsArgs.isSome()) {
    nsCOMPtr<nsICookieJarSettings> cookieJarSettings;
    nsresult rv =
        aLoadInfo->GetCookieJarSettings(getter_AddRefs(cookieJarSettings));
    if (NS_SUCCEEDED(rv) && cookieJarSettings) {
      nsCOMPtr<nsICookieJarSettings> mergedCookieJarSettings;
      mergedCookieJarSettings =
          static_cast<CookieJarSettings*>(cookieJarSettings.get())
              ->Merge(cookieJarSettingsArgs.ref());
      aLoadInfo->SetCookieJarSettings(mergedCookieJarSettings);
    }
  }

  rv = aLoadInfo->SetStoragePermission(aForwarderArgs.storagePermission());
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aLoadInfo->SetIsMetaRefresh(aForwarderArgs.isMetaRefresh());
  NS_ENSURE_SUCCESS(rv, rv);

  const Maybe<RFPTargetSet>& overriddenFingerprintingSettings =
      aForwarderArgs.overriddenFingerprintingSettings();
  if (overriddenFingerprintingSettings.isSome()) {
    aLoadInfo->SetOverriddenFingerprintingSettings(
        overriddenFingerprintingSettings.ref());
  }

  static_cast<LoadInfo*>(aLoadInfo)->ClearIsThirdPartyContextToTopWindow();
  if (aForwarderArgs.isThirdPartyContextToTopWindow().isSome()) {
    rv = aLoadInfo->SetIsThirdPartyContextToTopWindow(
        aForwarderArgs.isThirdPartyContextToTopWindow().ref());
  }
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aLoadInfo->SetIsInThirdPartyContext(
      aForwarderArgs.isInThirdPartyContext());
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aLoadInfo->SetIsOn3PCBExceptionList(
      aForwarderArgs.isOn3PCBExceptionList());
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aLoadInfo->SetUnstrippedURI(aForwarderArgs.unstrippedURI());
  NS_ENSURE_SUCCESS(rv, rv);

  if (aForwarderArgs.containerFeaturePolicyInfo()) {
    aLoadInfo->SetContainerFeaturePolicyInfo(
        *aForwarderArgs.containerFeaturePolicyInfo());
  }

  aLoadInfo->SetUserNavigationInvolvement(
      uint8_t(aForwarderArgs.userNavigationInvolvement()));

  return NS_OK;
}

void LoadInfoToChildLoadInfoForwarder(
    nsILoadInfo* aLoadInfo, ChildLoadInfoForwarderArgs* aForwarderArgsOut) {
  Maybe<IPCClientInfo> ipcReserved;
  Maybe<ClientInfo> reserved(aLoadInfo->GetReservedClientInfo());
  if (reserved.isSome()) {
    ipcReserved.emplace(reserved.ref().ToIPC());
  }

  Maybe<IPCClientInfo> ipcInitial;
  Maybe<ClientInfo> initial(aLoadInfo->GetInitialClientInfo());
  if (initial.isSome()) {
    ipcInitial.emplace(initial.ref().ToIPC());
  }

  Maybe<IPCServiceWorkerDescriptor> ipcController;
  Maybe<ServiceWorkerDescriptor> controller(aLoadInfo->GetController());
  if (controller.isSome()) {
    ipcController.emplace(controller.ref().ToIPC());
  }

  *aForwarderArgsOut =
      ChildLoadInfoForwarderArgs(ipcReserved, ipcInitial, ipcController,
                                 aLoadInfo->GetRequestBlockingReason());
}

nsresult MergeChildLoadInfoForwarder(
    const ChildLoadInfoForwarderArgs& aForwarderArgs, nsILoadInfo* aLoadInfo) {
  Maybe<ClientInfo> reservedClientInfo;
  auto& ipcReserved = aForwarderArgs.reservedClientInfo();
  if (ipcReserved.isSome()) {
    reservedClientInfo.emplace(ClientInfo(ipcReserved.ref()));
  }

  Maybe<ClientInfo> initialClientInfo;
  auto& ipcInitial = aForwarderArgs.initialClientInfo();
  if (ipcInitial.isSome()) {
    initialClientInfo.emplace(ClientInfo(ipcInitial.ref()));
  }

  // There should only be at most one reserved or initial ClientInfo.
  if (NS_WARN_IF(reservedClientInfo.isSome() && initialClientInfo.isSome())) {
    return NS_ERROR_FAILURE;
  }

  // If we received no reserved or initial ClientInfo, then we must not
  // already have one set.  There are no use cases where this should
  // happen and we don't have a way to clear the current value.
  if (NS_WARN_IF(reservedClientInfo.isNothing() &&
                 initialClientInfo.isNothing() &&
                 (aLoadInfo->GetReservedClientInfo().isSome() ||
                  aLoadInfo->GetInitialClientInfo().isSome()))) {
    return NS_ERROR_FAILURE;
  }

  if (reservedClientInfo.isSome()) {
    // We need to override here instead of simply set the value.  This
    // allows us to change the reserved client.  This is necessary when
    // the ClientChannelHelper created a new reserved client in the
    // child-side of the redirect.
    aLoadInfo->OverrideReservedClientInfoInParent(reservedClientInfo.ref());
  } else if (initialClientInfo.isSome()) {
    aLoadInfo->SetInitialClientInfo(initialClientInfo.ref());
  }

  aLoadInfo->ClearController();
  auto& controller = aForwarderArgs.controller();
  if (controller.isSome()) {
    aLoadInfo->SetController(ServiceWorkerDescriptor(controller.ref()));
  }

  uint32_t blockingReason = aForwarderArgs.requestBlockingReason();
  if (blockingReason) {
    // We only want to override when non-null, so that any earlier set non-null
    // value is not reverted to 0.
    aLoadInfo->SetRequestBlockingReason(blockingReason);
  }

  return NS_OK;
}

}  // namespace ipc
}  // namespace mozilla
