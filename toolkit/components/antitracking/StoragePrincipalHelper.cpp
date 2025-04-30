/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "StoragePrincipalHelper.h"

#include "mozilla/ExpandedPrincipal.h"
#include "mozilla/ipc/PBackgroundSharedTypes.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/extensions/WebExtensionPolicy.h"
#include "mozilla/net/CookieJarSettings.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_privacy.h"
#include "mozilla/StorageAccess.h"
#include "nsContentUtils.h"
#include "nsICookieJarSettings.h"
#include "nsICookieService.h"
#include "nsIDocShell.h"
#include "nsIEffectiveTLDService.h"
#include "nsIPrivateBrowsingChannel.h"
#include "AntiTrackingUtils.h"

namespace mozilla {

namespace {

bool ShouldPartitionChannel(nsIChannel* aChannel,
                            nsICookieJarSettings* aCookieJarSettings) {
  MOZ_ASSERT(aChannel);

  nsCOMPtr<nsIURI> uri;
  nsresult rv = aChannel->GetURI(getter_AddRefs(uri));
  if (NS_FAILED(rv)) {
    return false;
  }

  uint32_t rejectedReason = 0;
  if (ShouldAllowAccessFor(aChannel, uri, &rejectedReason)) {
    return false;
  }

  // Let's use the storage principal only if we need to partition the cookie
  // jar.  We use the lower-level ContentBlocking API here to ensure this
  // check doesn't send notifications.
  if (!ShouldPartitionStorage(rejectedReason) ||
      !StoragePartitioningEnabled(rejectedReason, aCookieJarSettings)) {
    return false;
  }

  return true;
}

bool ChooseOriginAttributes(nsIChannel* aChannel, OriginAttributes& aAttrs,
                            bool aForcePartitionedPrincipal) {
  MOZ_ASSERT(aChannel);

  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  nsCOMPtr<nsICookieJarSettings> cjs;
  Unused << loadInfo->GetCookieJarSettings(getter_AddRefs(cjs));

  if (!aForcePartitionedPrincipal && !ShouldPartitionChannel(aChannel, cjs)) {
    return false;
  }

  nsAutoString partitionKey;
  Unused << cjs->GetPartitionKey(partitionKey);

  if (!partitionKey.IsEmpty()) {
    aAttrs.SetPartitionKey(partitionKey);
    return true;
  }

  // Fallback to get first-party domain from top-level principal when we can't
  // get it from CookieJarSetting. This might happen when a channel is not
  // opened via http, for example, about page.
  nsCOMPtr<nsIPrincipal> toplevelPrincipal = loadInfo->GetTopLevelPrincipal();
  if (!toplevelPrincipal) {
    return false;
  }
  // Cast to BasePrincipal to continue to get acess to GetUri()
  auto* basePrin = BasePrincipal::Cast(toplevelPrincipal);
  nsCOMPtr<nsIURI> principalURI;

  nsresult rv = basePrin->GetURI(getter_AddRefs(principalURI));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }
  bool foreignByAncestorContext =
      AntiTrackingUtils::IsThirdPartyChannel(aChannel) &&
      !loadInfo->GetIsThirdPartyContextToTopWindow();
  aAttrs.SetPartitionKey(principalURI, foreignByAncestorContext);
  return true;
}

bool VerifyValidPartitionedPrincipalInfoForPrincipalInfoInternal(
    const ipc::PrincipalInfo& aPartitionedPrincipalInfo,
    const ipc::PrincipalInfo& aPrincipalInfo,
    bool aIgnoreSpecForContentPrincipal,
    bool aIgnoreDomainForContentPrincipal) {
  if (aPartitionedPrincipalInfo.type() != aPrincipalInfo.type()) {
    return false;
  }

  if (aPartitionedPrincipalInfo.type() ==
      mozilla::ipc::PrincipalInfo::TContentPrincipalInfo) {
    const mozilla::ipc::ContentPrincipalInfo& spInfo =
        aPartitionedPrincipalInfo.get_ContentPrincipalInfo();
    const mozilla::ipc::ContentPrincipalInfo& pInfo =
        aPrincipalInfo.get_ContentPrincipalInfo();

    return spInfo.attrs().EqualsIgnoringPartitionKey(pInfo.attrs()) &&
           spInfo.originNoSuffix() == pInfo.originNoSuffix() &&
           (aIgnoreSpecForContentPrincipal || spInfo.spec() == pInfo.spec()) &&
           (aIgnoreDomainForContentPrincipal ||
            spInfo.domain() == pInfo.domain()) &&
           spInfo.baseDomain() == pInfo.baseDomain();
  }

  if (aPartitionedPrincipalInfo.type() ==
      mozilla::ipc::PrincipalInfo::TSystemPrincipalInfo) {
    // Nothing to check here.
    return true;
  }

  if (aPartitionedPrincipalInfo.type() ==
      mozilla::ipc::PrincipalInfo::TNullPrincipalInfo) {
    const mozilla::ipc::NullPrincipalInfo& spInfo =
        aPartitionedPrincipalInfo.get_NullPrincipalInfo();
    const mozilla::ipc::NullPrincipalInfo& pInfo =
        aPrincipalInfo.get_NullPrincipalInfo();

    return spInfo.spec() == pInfo.spec() &&
           spInfo.attrs().EqualsIgnoringPartitionKey(pInfo.attrs());
  }

  if (aPartitionedPrincipalInfo.type() ==
      mozilla::ipc::PrincipalInfo::TExpandedPrincipalInfo) {
    const mozilla::ipc::ExpandedPrincipalInfo& spInfo =
        aPartitionedPrincipalInfo.get_ExpandedPrincipalInfo();
    const mozilla::ipc::ExpandedPrincipalInfo& pInfo =
        aPrincipalInfo.get_ExpandedPrincipalInfo();

    if (!spInfo.attrs().EqualsIgnoringPartitionKey(pInfo.attrs())) {
      return false;
    }

    if (spInfo.allowlist().Length() != pInfo.allowlist().Length()) {
      return false;
    }

    for (uint32_t i = 0; i < spInfo.allowlist().Length(); ++i) {
      if (!VerifyValidPartitionedPrincipalInfoForPrincipalInfoInternal(
              spInfo.allowlist()[i], pInfo.allowlist()[i],
              aIgnoreSpecForContentPrincipal,
              aIgnoreDomainForContentPrincipal)) {
        return false;
      }
    }

    return true;
  }

  MOZ_CRASH("Invalid principalInfo type");
  return false;
}

}  // namespace

// static
nsresult StoragePrincipalHelper::Create(nsIChannel* aChannel,
                                        nsIPrincipal* aPrincipal,
                                        bool aForceIsolation,
                                        nsIPrincipal** aStoragePrincipal) {
  MOZ_ASSERT(aChannel);
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aStoragePrincipal);

  auto scopeExit = MakeScopeExit([&] {
    nsCOMPtr<nsIPrincipal> storagePrincipal = aPrincipal;
    storagePrincipal.forget(aStoragePrincipal);
  });

  OriginAttributes attrs = aPrincipal->OriginAttributesRef();
  if (!ChooseOriginAttributes(aChannel, attrs, aForceIsolation)) {
    return NS_OK;
  }

  scopeExit.release();

  nsCOMPtr<nsIPrincipal> storagePrincipal =
      BasePrincipal::Cast(aPrincipal)->CloneForcingOriginAttributes(attrs);

  // If aPrincipal is not a ContentPrincipal, e.g. a NullPrincipal, the clone
  // call will return a nullptr.
  NS_ENSURE_TRUE(storagePrincipal, NS_ERROR_FAILURE);

  storagePrincipal.forget(aStoragePrincipal);
  return NS_OK;
}

// static
nsresult StoragePrincipalHelper::CreatePartitionedPrincipalForServiceWorker(
    nsIPrincipal* aPrincipal, nsICookieJarSettings* aCookieJarSettings,
    nsIPrincipal** aPartitionedPrincipal) {
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aPartitionedPrincipal);

  OriginAttributes attrs = aPrincipal->OriginAttributesRef();

  nsAutoString partitionKey;
  Unused << aCookieJarSettings->GetPartitionKey(partitionKey);

  if (!partitionKey.IsEmpty()) {
    attrs.SetPartitionKey(partitionKey);
  }

  nsCOMPtr<nsIPrincipal> partitionedPrincipal =
      BasePrincipal::Cast(aPrincipal)->CloneForcingOriginAttributes(attrs);

  // If aPrincipal is not a ContentPrincipal, e.g. a NullPrincipal, the clone
  // call will return a nullptr.
  NS_ENSURE_TRUE(partitionedPrincipal, NS_ERROR_FAILURE);

  partitionedPrincipal.forget(aPartitionedPrincipal);
  return NS_OK;
}

// static
nsresult
StoragePrincipalHelper::PrepareEffectiveStoragePrincipalOriginAttributes(
    nsIChannel* aChannel, OriginAttributes& aOriginAttributes) {
  MOZ_ASSERT(aChannel);

  ChooseOriginAttributes(aChannel, aOriginAttributes, false);
  return NS_OK;
}

// static
bool StoragePrincipalHelper::VerifyValidStoragePrincipalInfoForPrincipalInfo(
    const mozilla::ipc::PrincipalInfo& aStoragePrincipalInfo,
    const mozilla::ipc::PrincipalInfo& aPrincipalInfo) {
  return VerifyValidPartitionedPrincipalInfoForPrincipalInfoInternal(
      aStoragePrincipalInfo, aPrincipalInfo, false, false);
}

// static
bool StoragePrincipalHelper::VerifyValidClientPrincipalInfoForPrincipalInfo(
    const mozilla::ipc::PrincipalInfo& aClientPrincipalInfo,
    const mozilla::ipc::PrincipalInfo& aPrincipalInfo) {
  return VerifyValidPartitionedPrincipalInfoForPrincipalInfoInternal(
      aClientPrincipalInfo, aPrincipalInfo, true, true);
}

// static
nsresult StoragePrincipalHelper::GetPrincipal(nsIChannel* aChannel,
                                              PrincipalType aPrincipalType,
                                              nsIPrincipal** aPrincipal) {
  MOZ_ASSERT(aChannel);
  MOZ_ASSERT(aPrincipal);

  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  nsCOMPtr<nsICookieJarSettings> cjs;
  Unused << loadInfo->GetCookieJarSettings(getter_AddRefs(cjs));

  nsIScriptSecurityManager* ssm = nsContentUtils::GetSecurityManager();
  MOZ_DIAGNOSTIC_ASSERT(ssm);

  nsCOMPtr<nsIPrincipal> principal;
  nsCOMPtr<nsIPrincipal> partitionedPrincipal;

  nsresult rv =
      ssm->GetChannelResultPrincipals(aChannel, getter_AddRefs(principal),
                                      getter_AddRefs(partitionedPrincipal));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  // The aChannel might not be opened in some cases, e.g. getting principal
  // for the new channel during a redirect. So, the value
  // `IsThirdPartyToTopWindow` is incorrect in this case because this value is
  // calculated during opening a channel. And we need to know the value in order
  // to get the correct principal. To fix this, we compute the value here even
  // the channel hasn't been opened yet.
  //
  // Note that we don't need to compute the value if there is no browsing
  // context ID assigned. This could happen in a GTest or XPCShell.
  //
  // ToDo: The AntiTrackingUtils::ComputeIsThirdPartyToTopWindow() is only
  //       available in the parent process. So, this can only work in the parent
  //       process. It's fine for now, but we should change this to also work in
  //       content processes. Bug 1736452 will address this.
  //
  if (XRE_IsParentProcess() && loadInfo->GetBrowsingContextID() != 0) {
    AntiTrackingUtils::ComputeIsThirdPartyToTopWindow(aChannel);
  }

  nsCOMPtr<nsIPrincipal> outPrincipal = principal;

  switch (aPrincipalType) {
    case eRegularPrincipal:
      break;

    case eStorageAccessPrincipal:
      if (ShouldPartitionChannel(aChannel, cjs)) {
        outPrincipal = partitionedPrincipal;
      }
      break;

    case ePartitionedPrincipal:
      outPrincipal = partitionedPrincipal;
      break;

    case eForeignPartitionedPrincipal:
      // We only support foreign partitioned principal when dFPI is enabled.
      if (cjs->GetCookieBehavior() ==
              nsICookieService::BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN &&
          AntiTrackingUtils::IsThirdPartyChannel(aChannel)) {
        outPrincipal = partitionedPrincipal;
      }
      break;
  }

  outPrincipal.forget(aPrincipal);
  return NS_OK;
}

// static
nsresult StoragePrincipalHelper::GetPrincipal(nsPIDOMWindowInner* aWindow,
                                              PrincipalType aPrincipalType,
                                              nsIPrincipal** aPrincipal) {
  MOZ_ASSERT(aWindow);
  MOZ_ASSERT(aPrincipal);

  nsCOMPtr<dom::Document> doc = aWindow->GetExtantDoc();
  NS_ENSURE_STATE(doc);

  nsCOMPtr<nsIPrincipal> outPrincipal;

  switch (aPrincipalType) {
    case eRegularPrincipal:
      outPrincipal = doc->NodePrincipal();
      break;

    case eStorageAccessPrincipal:
      outPrincipal = doc->EffectiveStoragePrincipal();
      break;

    case ePartitionedPrincipal:
      outPrincipal = doc->PartitionedPrincipal();
      break;

    case eForeignPartitionedPrincipal:
      // We only support foreign partitioned principal when dFPI is enabled.
      if (doc->CookieJarSettings()->GetCookieBehavior() ==
              nsICookieService::BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN &&
          AntiTrackingUtils::IsThirdPartyWindow(aWindow, nullptr)) {
        outPrincipal = doc->PartitionedPrincipal();
      } else {
        outPrincipal = doc->NodePrincipal();
      }
      break;
  }

  outPrincipal.forget(aPrincipal);
  return NS_OK;
}

// static
bool StoragePrincipalHelper::ShouldUsePartitionPrincipalForServiceWorker(
    nsIDocShell* aDocShell) {
  MOZ_ASSERT(aDocShell);

  // We don't use the partitioned principal for service workers if it's
  // disabled.
  if (!StaticPrefs::privacy_partition_serviceWorkers()) {
    return false;
  }

  RefPtr<dom::Document> document = aDocShell->GetExtantDocument();

  // If we cannot get the document from the docShell, we turn to get its
  // parent's document.
  if (!document) {
    nsCOMPtr<nsIDocShellTreeItem> parentItem;
    aDocShell->GetInProcessSameTypeParent(getter_AddRefs(parentItem));

    if (parentItem) {
      document = parentItem->GetDocument();
    }
  }

  nsCOMPtr<nsICookieJarSettings> cookieJarSettings;

  if (document) {
    cookieJarSettings = document->CookieJarSettings();
  } else {
    // If there was no document, we create one cookieJarSettings here in order
    // to get the cookieBehavior.  We don't need a real value for RFP because
    // we are only using this object to check default cookie behavior.
    cookieJarSettings =
        net::CookieJarSettings::Create(net::CookieJarSettings::eRegular,
                                       /* shouldResistFingerpreinting */ false);
  }

  // We only support partitioned service workers when dFPI is enabled.
  if (cookieJarSettings->GetCookieBehavior() !=
      nsICookieService::BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN) {
    return false;
  }

  // Only the third-party context will need to use the partitioned principal. A
  // first-party context is still using the regular principal for the service
  // worker.
  return AntiTrackingUtils::IsThirdPartyContext(
      document ? document->GetBrowsingContext()
               : aDocShell->GetBrowsingContext());
}

// static
bool StoragePrincipalHelper::ShouldUsePartitionPrincipalForServiceWorker(
    dom::WorkerPrivate* aWorkerPrivate) {
  MOZ_ASSERT(aWorkerPrivate);

  // We don't use the partitioned principal for service workers if it's
  // disabled.
  if (!StaticPrefs::privacy_partition_serviceWorkers()) {
    return false;
  }

  nsCOMPtr<nsICookieJarSettings> cookieJarSettings =
      aWorkerPrivate->CookieJarSettings();

  // We only support partitioned service workers when dFPI is enabled.
  if (cookieJarSettings->GetCookieBehavior() !=
      nsICookieService::BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN) {
    return false;
  }

  return aWorkerPrivate->IsThirdPartyContext();
}

// static
bool StoragePrincipalHelper::GetOriginAttributes(
    nsIChannel* aChannel, mozilla::OriginAttributes& aAttributes,
    StoragePrincipalHelper::PrincipalType aPrincipalType) {
  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  loadInfo->GetOriginAttributes(&aAttributes);

  bool isPrivate = aAttributes.IsPrivateBrowsing();
  nsCOMPtr<nsIPrivateBrowsingChannel> pbChannel = do_QueryInterface(aChannel);
  if (pbChannel) {
    nsresult rv = pbChannel->GetIsChannelPrivate(&isPrivate);
    NS_ENSURE_SUCCESS(rv, false);
  } else {
    // Some channels may not implement nsIPrivateBrowsingChannel
    nsCOMPtr<nsILoadContext> loadContext;
    NS_QueryNotificationCallbacks(aChannel, loadContext);
    if (loadContext) {
      isPrivate = loadContext->UsePrivateBrowsing();
    }
  }
  aAttributes.SyncAttributesWithPrivateBrowsing(isPrivate);

  nsCOMPtr<nsICookieJarSettings> cjs;

  switch (aPrincipalType) {
    case eRegularPrincipal:
      break;

    case eStorageAccessPrincipal:
      PrepareEffectiveStoragePrincipalOriginAttributes(aChannel, aAttributes);
      break;

    case ePartitionedPrincipal:
      ChooseOriginAttributes(aChannel, aAttributes, true);
      break;

    case eForeignPartitionedPrincipal:
      Unused << loadInfo->GetCookieJarSettings(getter_AddRefs(cjs));

      // We only support foreign partitioned principal when dFPI is enabled.
      // Otherwise, we will use the regular principal.
      if (cjs->GetCookieBehavior() ==
              nsICookieService::BEHAVIOR_REJECT_TRACKER_AND_PARTITION_FOREIGN &&
          AntiTrackingUtils::IsThirdPartyChannel(aChannel)) {
        ChooseOriginAttributes(aChannel, aAttributes, true);
      }
      break;
  }

  return true;
}

// static
bool StoragePrincipalHelper::GetRegularPrincipalOriginAttributes(
    dom::Document* aDocument, OriginAttributes& aAttributes) {
  aAttributes = mozilla::OriginAttributes();
  if (!aDocument) {
    return false;
  }

  nsCOMPtr<nsILoadGroup> loadGroup = aDocument->GetDocumentLoadGroup();
  if (loadGroup) {
    return GetRegularPrincipalOriginAttributes(loadGroup, aAttributes);
  }

  nsCOMPtr<nsIChannel> channel = aDocument->GetChannel();
  if (!channel) {
    return false;
  }

  return GetOriginAttributes(channel, aAttributes, eRegularPrincipal);
}

// static
bool StoragePrincipalHelper::GetRegularPrincipalOriginAttributes(
    nsILoadGroup* aLoadGroup, OriginAttributes& aAttributes) {
  aAttributes = mozilla::OriginAttributes();
  if (!aLoadGroup) {
    return false;
  }

  nsCOMPtr<nsIInterfaceRequestor> callbacks;
  aLoadGroup->GetNotificationCallbacks(getter_AddRefs(callbacks));
  if (!callbacks) {
    return false;
  }

  nsCOMPtr<nsILoadContext> loadContext = do_GetInterface(callbacks);
  if (!loadContext) {
    return false;
  }

  loadContext->GetOriginAttributes(aAttributes);
  return true;
}

// static
bool StoragePrincipalHelper::GetOriginAttributesForNetworkState(
    nsIChannel* aChannel, OriginAttributes& aAttributes) {
  return StoragePrincipalHelper::GetOriginAttributes(
      aChannel, aAttributes,
      StaticPrefs::privacy_partition_network_state() ? ePartitionedPrincipal
                                                     : eRegularPrincipal);
}

// static
void StoragePrincipalHelper::GetOriginAttributesForNetworkState(
    dom::Document* aDocument, OriginAttributes& aAttributes) {
  aAttributes = aDocument->NodePrincipal()->OriginAttributesRef();

  if (!StaticPrefs::privacy_partition_network_state()) {
    return;
  }

  aAttributes = aDocument->PartitionedPrincipal()->OriginAttributesRef();
}

// static
void StoragePrincipalHelper::UpdateOriginAttributesForNetworkState(
    nsIURI* aFirstPartyURI, OriginAttributes& aAttributes) {
  if (!StaticPrefs::privacy_partition_network_state()) {
    return;
  }

  aAttributes.SetPartitionKey(aFirstPartyURI, false);
}

enum SupportedScheme { HTTP, HTTPS };

static bool GetOriginAttributesWithScheme(nsIChannel* aChannel,
                                          OriginAttributes& aAttributes,
                                          SupportedScheme aScheme) {
  const nsString targetScheme = aScheme == HTTP ? u"http"_ns : u"https"_ns;
  if (!StoragePrincipalHelper::GetOriginAttributesForNetworkState(
          aChannel, aAttributes)) {
    return false;
  }

  if (aAttributes.mPartitionKey.IsEmpty() ||
      aAttributes.mPartitionKey[0] != '(') {
    return true;
  }

  nsAString::const_iterator start, end;
  aAttributes.mPartitionKey.BeginReading(start);
  aAttributes.mPartitionKey.EndReading(end);

  MOZ_DIAGNOSTIC_ASSERT(*start == '(');
  start++;

  nsAString::const_iterator iter(start);
  bool ok = FindCharInReadable(',', iter, end);
  MOZ_DIAGNOSTIC_ASSERT(ok);

  if (!ok) {
    return false;
  }

  nsAutoString scheme;
  scheme.Assign(Substring(start, iter));

  if (scheme.Equals(targetScheme)) {
    return true;
  }

  nsAutoString key;
  key += u"("_ns;
  key += targetScheme;
  key.Append(Substring(iter, end));
  aAttributes.SetPartitionKey(key);

  return true;
}

// static
bool StoragePrincipalHelper::GetOriginAttributesForHSTS(
    nsIChannel* aChannel, OriginAttributes& aAttributes) {
  return GetOriginAttributesWithScheme(aChannel, aAttributes, HTTP);
}

// static
bool StoragePrincipalHelper::GetOriginAttributesForHTTPSRR(
    nsIChannel* aChannel, OriginAttributes& aAttributes) {
  return GetOriginAttributesWithScheme(aChannel, aAttributes, HTTPS);
}

// static
bool StoragePrincipalHelper::GetOriginAttributes(
    const mozilla::ipc::PrincipalInfo& aPrincipalInfo,
    OriginAttributes& aAttributes) {
  aAttributes = mozilla::OriginAttributes();

  using Type = ipc::PrincipalInfo;
  switch (aPrincipalInfo.type()) {
    case Type::TContentPrincipalInfo:
      aAttributes = aPrincipalInfo.get_ContentPrincipalInfo().attrs();
      break;
    case Type::TNullPrincipalInfo:
      aAttributes = aPrincipalInfo.get_NullPrincipalInfo().attrs();
      break;
    case Type::TExpandedPrincipalInfo:
      aAttributes = aPrincipalInfo.get_ExpandedPrincipalInfo().attrs();
      break;
    case Type::TSystemPrincipalInfo:
      break;
    default:
      return false;
  }

  return true;
}

bool StoragePrincipalHelper::PartitionKeyHasBaseDomain(
    const nsAString& aPartitionKey, const nsACString& aBaseDomain) {
  return PartitionKeyHasBaseDomain(aPartitionKey,
                                   NS_ConvertUTF8toUTF16(aBaseDomain));
}

// static
bool StoragePrincipalHelper::PartitionKeyHasBaseDomain(
    const nsAString& aPartitionKey, const nsAString& aBaseDomain) {
  if (aPartitionKey.IsEmpty() || aBaseDomain.IsEmpty()) {
    return false;
  }

  nsString scheme;
  nsString pkBaseDomain;
  int32_t port;
  bool foreign;
  bool success = OriginAttributes::ParsePartitionKey(
      aPartitionKey, scheme, pkBaseDomain, port, foreign);

  if (!success) {
    return false;
  }

  return aBaseDomain.Equals(pkBaseDomain);
}

// static
void StoragePrincipalHelper::UpdatePartitionKeyWithForeignAncestorBit(
    nsAString& aKey, bool aForeignByAncestorContext) {
  bool site = 0 == aKey.Find(u"(");
  if (!site) {
    return;
  }
  if (aForeignByAncestorContext) {
    int32_t index = aKey.Find(u",f)");
    if (index == -1) {
      uint32_t cutStart = aKey.Length() - 1;
      aKey.ReplaceLiteral(cutStart, 1, u",f)");
    }
  } else {
    int32_t index = aKey.Find(u",f)");
    if (index != -1) {
      uint32_t cutLength = aKey.Length() - index;
      aKey.ReplaceLiteral(index, cutLength, u")");
    }
  }
}

// static
nsString StoragePrincipalHelper::PartitionKeyForExpandedPrincipal(
    nsIPrincipal* aExpandedPrincipal) {
  MOZ_ASSERT(nsContentUtils::IsExpandedPrincipal(aExpandedPrincipal));

  OriginAttributes attrs;

  for (const auto& principal : BasePrincipal::Cast(aExpandedPrincipal)
                                   ->As<ExpandedPrincipal>()
                                   ->AllowList()) {
    MOZ_ASSERT(principal);

    nsCOMPtr<nsIURI> uri;
    nsresult rv = BasePrincipal::Cast(principal)->GetURI(getter_AddRefs(uri));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      continue;
    }

    nsAutoCString scheme;
    rv = uri->GetScheme(scheme);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      continue;
    }

    if (!scheme.Equals("moz-extension")) {
      continue;
    }

    attrs.SetFirstPartyDomain(true, uri, true);
    MOZ_ASSERT(attrs.mPartitionKey.IsEmpty());
    break;
  }

  return attrs.mPartitionKey;
}

}  // namespace mozilla
