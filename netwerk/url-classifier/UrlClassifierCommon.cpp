/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/net/UrlClassifierCommon.h"

#include "mozilla/BasePrincipal.h"
#include "mozilla/Components.h"
#include "mozilla/StaticPrefs_privacy.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "nsIChannel.h"
#include "nsIClassifiedChannel.h"
#include "nsIHttpChannelInternal.h"
#include "nsIParentChannel.h"
#include "nsNetUtil.h"
#include "nsReadableUtils.h"

namespace mozilla {
namespace net {

const nsCString::size_type UrlClassifierCommon::sMaxSpecLength = 128;

// MOZ_LOG=nsChannelClassifier:5
LazyLogModule gChannelClassifierLog("nsChannelClassifier");
LazyLogModule gChannelClassifierLogLeak("nsChannelClassifierLeak");

/* static */
bool UrlClassifierCommon::AddonMayLoad(nsIChannel* aChannel, nsIURI* aURI) {
  nsCOMPtr<nsILoadInfo> channelLoadInfo = aChannel->LoadInfo();
  // loadingPrincipal is used here to ensure we are loading into an
  // addon principal.  This allows an addon, with explicit permission, to
  // call out to API endpoints that may otherwise get blocked.
  nsIPrincipal* loadingPrincipal = channelLoadInfo->GetLoadingPrincipal();
  if (!loadingPrincipal) {
    return false;
  }

  return BasePrincipal::Cast(loadingPrincipal)->AddonAllowsLoad(aURI, true);
}

/* static */
bool UrlClassifierCommon::ShouldEnableProtectionForChannel(
    nsIChannel* aChannel) {
  MOZ_ASSERT(aChannel);

  nsCOMPtr<nsIURI> chanURI;
  nsresult rv = aChannel->GetURI(getter_AddRefs(chanURI));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }

  if (UrlClassifierCommon::AddonMayLoad(aChannel, chanURI)) {
    return false;
  }

  nsCOMPtr<nsIURI> topWinURI;
  nsCOMPtr<nsIHttpChannelInternal> channel = do_QueryInterface(aChannel);
  if (NS_WARN_IF(!channel)) {
    return false;
  }

  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  MOZ_ASSERT(loadInfo);

  auto policyType = loadInfo->GetExternalContentPolicyType();
  if (policyType == ExtContentPolicy::TYPE_DOCUMENT) {
    UC_LOG(
        ("UrlClassifierCommon::ShouldEnableProtectionForChannel - "
         "skipping top-level load for channel %p",
         aChannel));
    return false;
  }

  // Tracking protection will be enabled so return without updating
  // the security state. If any channels are subsequently cancelled
  // (page elements blocked) the state will be then updated.

  return true;
}

/* static */
nsresult UrlClassifierCommon::GetTopWindowURI(nsIChannel* aChannel,
                                              nsIURI** aURI) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(aChannel);

  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  MOZ_ASSERT(loadInfo);

  RefPtr<dom::BrowsingContext> browsingContext;
  nsresult rv =
      loadInfo->GetTargetBrowsingContext(getter_AddRefs(browsingContext));
  if (NS_WARN_IF(NS_FAILED(rv)) || !browsingContext) {
    return NS_ERROR_FAILURE;
  }

  dom::CanonicalBrowsingContext* top = browsingContext->Canonical()->Top();
  dom::WindowGlobalParent* wgp = top->GetCurrentWindowGlobal();
  if (!wgp) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<nsIURI> uri = wgp->GetDocumentURI();
  if (!uri) {
    return NS_ERROR_FAILURE;
  }

  if (!NS_IsAboutBlank(uri)) {
    uri.forget(aURI);
    return NS_OK;
  }

  // An about:blank document URI doesn't identify a site, which happens for
  // freshly opened popups. Fall back to the document principal, which such a
  // document inherits from its creator. This intentionally fails for null
  // principals, e.g. sandboxed popups, which have an opaque origin.
  nsIPrincipal* principal = wgp->DocumentPrincipal();
  if (!principal || !principal->GetIsContentPrincipal()) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIURI> principalURI = principal->GetURI();
  if (!principalURI) {
    return NS_ERROR_FAILURE;
  }

  principalURI.forget(aURI);
  return NS_OK;
}

/* static */
nsresult UrlClassifierCommon::CreatePairwiseEntityListURI(nsIChannel* aChannel,
                                                          nsIURI** aURI) {
  MOZ_ASSERT(aChannel);
  MOZ_ASSERT(aURI);

  nsresult rv;
  nsCOMPtr<nsIHttpChannelInternal> chan = do_QueryInterface(aChannel, &rv);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!chan) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIURI> topWinURI;
  rv =
      UrlClassifierCommon::GetTopWindowURI(aChannel, getter_AddRefs(topWinURI));
  if (NS_FAILED(rv) || !topWinURI) {
    // SharedWorker and ServiceWorker don't have an associated window, use
    // client's URI instead.
    nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
    MOZ_ASSERT(loadInfo);

    Maybe<dom::ClientInfo> clientInfo = loadInfo->GetClientInfo();
    if (clientInfo.isSome()) {
      if ((clientInfo->Type() == dom::ClientType::Sharedworker) ||
          (clientInfo->Type() == dom::ClientType::Serviceworker)) {
        UC_LOG(
            ("UrlClassifierCommon::CreatePairwiseEntityListURI - "
             "channel %p initiated by worker, get uri from client",
             aChannel));

        auto clientPrincipalOrErr = clientInfo->GetPrincipal();
        if (clientPrincipalOrErr.isOk()) {
          nsCOMPtr<nsIPrincipal> principal = clientPrincipalOrErr.unwrap();
          if (principal) {
            auto* basePrin = BasePrincipal::Cast(principal);
            rv = basePrin->GetURI(getter_AddRefs(topWinURI));
            (void)NS_WARN_IF(NS_FAILED(rv));
          }
        }
      }
    }

    if (!topWinURI) {
      UC_LOG(
          ("UrlClassifierCommon::CreatePairwiseEntityListURI - "
           "no top-level window associated with channel %p, "
           "get uri from loading principal",
           aChannel));

      nsCOMPtr<nsIPrincipal> principal = loadInfo->GetLoadingPrincipal();
      if (principal) {
        auto* basePrin = BasePrincipal::Cast(principal);
        rv = basePrin->GetURI(getter_AddRefs(topWinURI));
        (void)NS_WARN_IF(NS_FAILED(rv));
      }
    }
  }

  if (!topWinURI) {
    UC_LOG(
        ("UrlClassifierCommon::CreatePairwiseEntityListURI - "
         "fail to get top-level window uri for channel %p",
         aChannel));

    // Return success because we want to continue to look up even without
    // whitelist.
    return NS_OK;
  }

  nsCOMPtr<nsIScriptSecurityManager> securityManager;
  securityManager = mozilla::components::ScriptSecurityManager::Service(&rv);
  NS_ENSURE_SUCCESS(rv, rv);
  nsCOMPtr<nsIPrincipal> chanPrincipal;
  rv = securityManager->GetChannelURIPrincipal(aChannel,
                                               getter_AddRefs(chanPrincipal));
  NS_ENSURE_SUCCESS(rv, rv);

  // Craft a entitylist URL like "toplevel.page/?resource=third.party.domain"
  nsAutoCString pageHostname, resourceDomain;
  rv = topWinURI->GetHost(pageHostname);
  if (NS_FAILED(rv)) {
    // When the top-level page doesn't support GetHost, for example, about:home,
    // we don't return an error here; instead, we return success to make sure
    // that the lookup process calling this API continues to run.
    if (UC_LOG_ENABLED()) {
      nsCString topWinSpec =
          topWinURI ? topWinURI->GetSpecOrDefault() : "(null)"_ns;
      topWinSpec.Truncate(
          std::min(topWinSpec.Length(), UrlClassifierCommon::sMaxSpecLength));
      UC_LOG(
          ("UrlClassifierCommon::CreatePairwiseEntityListURI - "
           "cannot get host from the top-level uri %s of channel %p",
           topWinSpec.get(), aChannel));
    }
    return NS_OK;
  }

  rv = chanPrincipal->GetBaseDomain(resourceDomain);
  NS_ENSURE_SUCCESS(rv, rv);
  nsAutoCString entitylistEntry =
      "http://"_ns + pageHostname + "/?resource="_ns + resourceDomain;
  UC_LOG(
      ("UrlClassifierCommon::CreatePairwiseEntityListURI - looking for %s in "
       "the entitylist on channel %p",
       entitylistEntry.get(), aChannel));

  nsCOMPtr<nsIURI> entitylistURI;
  rv = NS_NewURI(getter_AddRefs(entitylistURI), entitylistEntry);
  if (NS_FAILED(rv)) {
    return rv;
  }

  entitylistURI.forget(aURI);
  return NS_OK;
}

/* static */
nsresult UrlClassifierCommon::SetTrackingInfo(
    nsIChannel* aChannel, const nsTArray<nsCString>& aLists,
    const nsTArray<nsCString>& aFullHashes) {
  NS_ENSURE_ARG(!aLists.IsEmpty());

  // Can be called in EITHER the parent or child process.
  nsresult rv;
  nsCOMPtr<nsIClassifiedChannel> classifiedChannel =
      do_QueryInterface(aChannel, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  if (classifiedChannel) {
    classifiedChannel->SetMatchedTrackingInfo(aLists, aFullHashes);
  }

  nsCOMPtr<nsIParentChannel> parentChannel;
  NS_QueryNotificationCallbacks(aChannel, parentChannel);
  if (parentChannel) {
    // This channel is a parent-process proxy for a child process request.
    // Tell the child process channel to do this as well.
    // TODO: We can remove the code sending the IPC to content to update
    //       tracking info once we move the ContentBlockingLog into the parent.
    //       This would be done in Bug 1599046.
    nsAutoCString strLists, strHashes;
    UrlClassifierCommon::TablesToString(aLists, strLists);
    UrlClassifierCommon::TablesToString(aFullHashes, strHashes);

    parentChannel->SetClassifierMatchedTrackingInfo(strLists, strHashes);
  }

  return NS_OK;
}

void UrlClassifierCommon::TablesToString(const nsTArray<nsCString>& aList,
                                         nsACString& aString) {
  // Truncate and append rather than assigning because that's more efficient if
  // aString is an nsAutoCString.
  aString.Truncate();
  StringJoinAppend(aString, ","_ns, aList);
}

uint32_t UrlClassifierCommon::TablesToClassificationFlags(
    const nsTArray<nsCString>& aList,
    const std::vector<ClassificationData>& aData, uint32_t aDefaultFlag) {
  uint32_t flags = 0;
  for (const nsCString& table : aList) {
    flags |= TableToClassificationFlag(table, aData);
  }

  if (flags == 0) {
    flags |= aDefaultFlag;
  }

  return flags;
}

uint32_t UrlClassifierCommon::TableToClassificationFlag(
    const nsACString& aTable, const std::vector<ClassificationData>& aData) {
  for (const ClassificationData& data : aData) {
    if (StringBeginsWith(aTable, data.mPrefix)) {
      return data.mFlag;
    }
  }

  return 0;
}

/* static */
bool UrlClassifierCommon::ShouldProcessWithProtectionFeature(
    nsIChannel* aChannel) {
  MOZ_ASSERT(aChannel);

  bool shouldProcess = true;
  bool isPrivateMode = NS_UsePrivateBrowsing(aChannel);

  nsCOMPtr<nsIClassifiedChannel> classifiedChannel =
      do_QueryInterface(aChannel);

  if (classifiedChannel) {
    if (classifiedChannel->GetClassificationFlags() &
        nsIClassifiedChannel::ClassificationFlags::CLASSIFIED_CONSENTMANAGER) {
      // Channel is classified as consent manager
      if (StaticPrefs::
              privacy_trackingprotection_consentmanager_skip_enabled() ||
          (StaticPrefs::
               privacy_trackingprotection_consentmanager_skip_pbmode_enabled() &&
           isPrivateMode)) {
        // Don't process channel
        shouldProcess = false;

        UC_LOG(
            ("UrlClassifierCommon::ShouldProcessWithProtectionFeature - "
             "Skipping channel %p because annotated as a consent manager",
             aChannel));
      }
    }

    if (classifiedChannel->GetClassificationFlags() &
        nsIClassifiedChannel::ClassificationFlags::CLASSIFIED_ANTIFRAUD) {
      // Channel is classified as anti-fraud
      if (StaticPrefs::privacy_trackingprotection_antifraud_skip_enabled() ||
          (StaticPrefs::
               privacy_trackingprotection_antifraud_skip_pbmode_enabled() &&
           isPrivateMode)) {
        // Don't process channel
        shouldProcess = false;

        UC_LOG(
            ("UrlClassifierCommon::ShouldProcessWithProtectionFeature - "
             "Skipping channel %p because it is annotated as anti-fraud",
             aChannel));
      }
    }
  }

  return shouldProcess;
}

}  // namespace net
}  // namespace mozilla
