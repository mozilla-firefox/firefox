/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DynamicFpiNavigationHeuristic.h"

#include "AntiTrackingUtils.h"
#include "StorageAccessAPIHelper.h"
#include "mozIThirdPartyUtil.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/BounceTrackingRecord.h"
#include "mozilla/BounceTrackingState.h"
#include "mozilla/Components.h"
#include "mozilla/glean/AntitrackingMetrics.h"
#include "nsIChannel.h"
#include "nsNetUtil.h"
#include "nsISHistory.h"
#include "nsIURIClassifier.h"

namespace mozilla {

// static
void DynamicFpiNavigationHeuristic::MaybeGrantStorageAccess(
    dom::CanonicalBrowsingContext* aBrowsingContext, nsIChannel* aChannel) {
  // Make sure we only fire the heuristic when it is enabled.
  if (!StaticPrefs::privacy_antitracking_enableWebcompat() ||
      !StaticPrefs::privacy_restrict3rdpartystorage_heuristic_navigation()) {
    return;
  }

  // Validate our args and make sure we have a bounce tracking state.
  NS_ENSURE_TRUE_VOID(aBrowsingContext);
  NS_ENSURE_FALSE_VOID(aBrowsingContext->IsSubframe());
  RefPtr<BounceTrackingState> bounceTrackingState =
      aBrowsingContext->GetBounceTrackingState();
  NS_ENSURE_TRUE_VOID(bounceTrackingState);
  NS_ENSURE_TRUE_VOID(aChannel);

  // Don't trigger navigation heuristic for first-party trackers if the pref
  // says so.
  if (StaticPrefs::
          privacy_restrict3rdpartystorage_heuristic_exclude_third_party_trackers() &&
      AntiTrackingUtils::IsFirstPartyTrackingChannel(aChannel)) {
    return;
  }

  nsCOMPtr<nsIPrincipal> resultPrincipal;
  nsresult rv = nsContentUtils::GetSecurityManager()->GetChannelResultPrincipal(
      aChannel, getter_AddRefs(resultPrincipal));
  if (NS_FAILED(rv) || !resultPrincipal ||
      !resultPrincipal->GetIsContentPrincipal()) {
    return;
  }

  BounceTrackingRecord* record = bounceTrackingState->GetBounceTrackingRecord();
  if (!record) {
    return;
  }

  // Get the session history and the current index (of the opening document)
  nsCOMPtr<nsISHistory> shistory = aBrowsingContext->GetSessionHistory();
  if (!shistory) {
    return;
  }
  int32_t index = -1;
  rv = shistory->GetIndex(&index);
  if (NS_FAILED(rv)) {
    return;
  }

  // Loop backward in the session history, looking for the initial visit to
  // the same site host as the opening document, and building a set of site
  // hosts we interacted with along the way.
  bool foundResultSiteInHistory = false;
  nsTArray<RefPtr<nsIURI>> candidateURIs;
  RefPtr<nsISHEntry> entry;
  for (int32_t i = 0; i <= index; i++) {
    shistory->GetEntryAtIndex(index - i, getter_AddRefs(entry));
    if (!entry) {
      continue;
    }
    RefPtr<nsIURI> entryURI = entry->GetResultPrincipalURI();

    // Fall back to an unmodified entry's URI.
    // Warning: you should not copy-paste this code elsewhere, nor should you
    // use GetURI in security-critical contexts where you really want something
    // like the resultPrincipalURI. We are only doing that here because we do
    // not have a OriginalURI set, are giving a permission based on a heuristic,
    // and constrain ourselves to http(s) URIs.
    if (!entryURI) {
      entryURI = entry->GetURI();
      if (!entryURI) {
        continue;
      }
    }
    nsAutoCString scheme;
    nsresult rv = entryURI->GetScheme(scheme);
    if (NS_FAILED(rv) ||
        (!scheme.EqualsLiteral("http") && !scheme.EqualsLiteral("https"))) {
      continue;
    }

    bool isThirdPartyEntry = false;
    rv = resultPrincipal->IsThirdPartyURI(entryURI, &isThirdPartyEntry);
    if (NS_SUCCEEDED(rv) && !isThirdPartyEntry) {
      nsAutoCString entryScheme;
      rv = entryURI->GetScheme(entryScheme);
      if (NS_SUCCEEDED(rv) && resultPrincipal->SchemeIs(entryScheme.get())) {
        foundResultSiteInHistory = true;
        break;
      }
    }

    nsAutoCString entrySiteHost;
    nsCOMPtr<mozIThirdPartyUtil> thirdPartyUtil =
        components::ThirdPartyUtil::Service();
    if (!thirdPartyUtil) {
      continue;
    }
    rv = thirdPartyUtil->GetBaseDomain(entryURI, entrySiteHost);
    if (NS_FAILED(rv)) {
      continue;
    }
    if (record->GetUserActivationHosts().Contains(entrySiteHost)) {
      candidateURIs.AppendElement(entryURI);
    }
  }

  // Fire the heuristic for all interacted-with hosts of the current extended
  // navigation
  if (foundResultSiteInHistory) {
    for (nsIURI* uri : candidateURIs) {
      // Construct the right principal, using the opening document's OAs
      RefPtr<nsIPrincipal> embedeePrincipal =
          BasePrincipal::CreateContentPrincipal(
              uri, resultPrincipal->OriginAttributesRef());

      (void)StorageAccessAPIHelper::SaveAccessForOriginOnParentProcess(
          resultPrincipal, embedeePrincipal,
          StorageAccessAPIHelper::StorageAccessPromptChoices::eAllow, false,
          StaticPrefs::privacy_restrict3rdpartystorage_expiration_visited());

      glean::contentblocking::storage_access_granted_count
          .EnumGet(glean::contentblocking::StorageAccessGrantedCountLabel::
                       eStoragegranted)
          .Add();
      glean::contentblocking::storage_access_granted_count
          .EnumGet(glean::contentblocking::StorageAccessGrantedCountLabel::
                       eNavigation)
          .Add();

      StorageAccessGrantTelemetryClassification::MaybeReportTracker(
          static_cast<uint16_t>(
              glean::contentblocking::StorageAccessGrantedCountLabel::
                  eNavigationCt),
          uri);
    }
  }
}
}  // namespace mozilla
