/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AntiTrackingLog.h"
#include "AntiTrackingUtils.h"
#include "AntiTrackingRedirectHeuristic.h"
#include "ContentBlockingAllowList.h"
#include "ContentBlockingUserInteraction.h"
#include "StorageAccessAPIHelper.h"

#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/Document.h"
#include "mozilla/net/CookieJarSettings.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/glean/AntitrackingMetrics.h"
#include "nsContentUtils.h"
#include "nsIChannel.h"
#include "nsICookieService.h"
#include "nsIHttpChannel.h"
#include "nsIRedirectHistoryEntry.h"
#include "nsIScriptError.h"
#include "nsIURI.h"
#include "nsNetUtil.h"
#include "nsPIDOMWindow.h"
#include "nsScriptSecurityManager.h"

namespace mozilla {

namespace {

// The helper function to check if we need to check the redirect heuristic for
// ETP later when we know the classification flags of the new channel. This
// check is from the perspective of the old channel. We don't check for the new
// channel because the classification flags are not ready yet when we call this
// function.
bool ShouldCheckRedirectHeuristicETP(nsIChannel* aOldChannel, nsIURI* aOldURI,
                                     nsIPrincipal* aOldPrincipal) {
  nsCOMPtr<nsILoadInfo> oldLoadInfo = aOldChannel->LoadInfo();
  MOZ_ASSERT(oldLoadInfo);

  bool allowedByPreviousRedirect =
      oldLoadInfo->GetAllowListFutureDocumentsCreatedFromThisRedirectChain();

  // We will skip this check if we have granted storage access before so that we
  // can grant the storage access to the rest of the chain.
  if (!AntiTrackingUtils::IsFirstPartyTrackingChannel(aOldChannel) &&
      !allowedByPreviousRedirect) {
    // This is not a tracking -> non-tracking redirect.
    LOG_SPEC(("Ignoring the redirect from %s because it's not tracking to "
              "non-tracking.",
              _spec),
             aOldURI);
    return false;
  }

  if (!ContentBlockingUserInteraction::Exists(aOldPrincipal)) {
    LOG_SPEC(("Ignoring redirect for %s because no user-interaction on "
              "tracker",
              _spec),
             aOldURI);
    return false;
  }

  return true;
}

// The helper function to decide and set the storage access after we know the
// classification flags of the new channel.
bool ShouldRedirectHeuristicApplyETP(nsIChannel* aNewChannel, nsIURI* aNewURI) {
  // We're looking at the first-party classification flags because we're
  // interested in first-party redirects.
  if (AntiTrackingUtils::IsFirstPartyTrackingChannel(aNewChannel)) {
    // This is not a tracking -> non-tracking redirect.
    LOG_SPEC(("Ignoring the redirect to %s because it's not tracking to "
              "non-tracking.",
              _spec),
             aNewURI);
    return false;
  }

  return true;
}

bool ShouldRedirectHeuristicApply(nsIChannel* aNewChannel, nsIURI* aNewURI) {
  nsCOMPtr<nsILoadInfo> newLoadInfo = aNewChannel->LoadInfo();
  MOZ_ASSERT(newLoadInfo);

  nsCOMPtr<nsICookieJarSettings> cookieJarSettings;

  nsresult rv =
      newLoadInfo->GetCookieJarSettings(getter_AddRefs(cookieJarSettings));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    LOG(("Can't obtain the cookieJarSetting from the channel"));
    return false;
  }

  uint32_t cookieBehavior = cookieJarSettings->GetCookieBehavior();
  if (cookieBehavior == nsICookieService::BEHAVIOR_REJECT_TRACKER ||
      cookieBehavior == nsICookieService::BEHAVIOR_PARTITION_FOREIGN) {
    return ShouldRedirectHeuristicApplyETP(aNewChannel, aNewURI);
  }

  LOG((
      "Heuristic doesn't apply because the cookieBehavior doesn't require it"));
  return false;
}

bool ShouldCheckRedirectHeuristic(nsIChannel* aOldChannel, nsIURI* aOldURI,
                                  nsIPrincipal* aOldPrincipal) {
  nsCOMPtr<nsILoadInfo> oldLoadInfo = aOldChannel->LoadInfo();
  MOZ_ASSERT(oldLoadInfo);

  nsCOMPtr<nsICookieJarSettings> cookieJarSettings;

  nsresult rv =
      oldLoadInfo->GetCookieJarSettings(getter_AddRefs(cookieJarSettings));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    LOG(("Can't obtain the cookieJarSettings from the old channel"));
    return false;
  }

  uint32_t cookieBehavior = cookieJarSettings->GetCookieBehavior();
  if (cookieBehavior == nsICookieService::BEHAVIOR_REJECT_TRACKER ||
      cookieBehavior == nsICookieService::BEHAVIOR_PARTITION_FOREIGN) {
    return ShouldCheckRedirectHeuristicETP(aOldChannel, aOldURI, aOldPrincipal);
  }

  LOG(
      ("Heuristic doesn't be needed because the cookieBehavior doesn't require "
       "it"));
  return false;
}

}  // namespace

void PrepareForAntiTrackingRedirectHeuristic(nsIChannel* aOldChannel,
                                             nsIURI* aOldURI,
                                             nsIChannel* aNewChannel,
                                             nsIURI* aNewURI) {
  MOZ_ASSERT(aOldChannel);
  MOZ_ASSERT(aOldURI);
  MOZ_ASSERT(aNewChannel);
  MOZ_ASSERT(aNewURI);

  // This heuristic works only on the parent process.
  if (!XRE_IsParentProcess()) {
    return;
  }

  if (!StaticPrefs::privacy_antitracking_enableWebcompat() ||
      !StaticPrefs::privacy_restrict3rdpartystorage_heuristic_redirect()) {
    return;
  }

  nsCOMPtr<nsIHttpChannel> oldChannel = do_QueryInterface(aOldChannel);
  if (!oldChannel) {
    return;
  }

  nsCOMPtr<nsIHttpChannel> newChannel = do_QueryInterface(aNewChannel);
  if (!newChannel) {
    return;
  }

  LOG_SPEC2(("Preparing redirect-heuristic for the redirect %s -> %s", _spec1,
             _spec2),
            aOldURI, aNewURI);

  nsCOMPtr<nsILoadInfo> oldLoadInfo = aOldChannel->LoadInfo();
  MOZ_ASSERT(oldLoadInfo);

  nsCOMPtr<nsILoadInfo> newLoadInfo = aNewChannel->LoadInfo();
  MOZ_ASSERT(newLoadInfo);

  // We need to clear the flag first because the new loadInfo was cloned from
  // the old loadInfo.
  newLoadInfo->SetNeedForCheckingAntiTrackingHeuristic(false);

  nsCOMPtr<nsICookieJarSettings> cookieJarSettings;
  nsresult rv =
      oldLoadInfo->GetCookieJarSettings(getter_AddRefs(cookieJarSettings));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    LOG(("Can't get the cookieJarSettings"));
    return;
  }

  int32_t behavior = cookieJarSettings->GetCookieBehavior();

  if (!cookieJarSettings->GetRejectThirdPartyContexts()) {
    LOG(
        ("Disabled by network.cookie.cookieBehavior pref (%d), bailing out "
         "early",
         behavior));
    return;
  }

  MOZ_ASSERT(behavior == nsICookieService::BEHAVIOR_REJECT_TRACKER ||
             behavior == nsICookieService::BEHAVIOR_PARTITION_FOREIGN);

  ExtContentPolicyType contentType =
      oldLoadInfo->GetExternalContentPolicyType();
  if (contentType != ExtContentPolicy::TYPE_DOCUMENT ||
      !aOldChannel->IsDocument()) {
    LOG_SPEC(("Ignoring redirect for %s because it's not a document", _spec),
             aOldURI);
    // We care about document redirects only.
    return;
  }

  if (ContentBlockingAllowList::Check(newChannel)) {
    return;
  }

  nsIScriptSecurityManager* ssm =
      nsScriptSecurityManager::GetScriptSecurityManager();
  MOZ_ASSERT(ssm);

  nsCOMPtr<nsIPrincipal> oldPrincipal;

  const nsTArray<nsCOMPtr<nsIRedirectHistoryEntry>>& chain =
      oldLoadInfo->RedirectChain();

  if (oldLoadInfo->GetAllowListFutureDocumentsCreatedFromThisRedirectChain() &&
      !chain.IsEmpty()) {
    rv = chain[0]->GetPrincipal(getter_AddRefs(oldPrincipal));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      LOG(("Can't obtain the principal from the redirect chain"));
      return;
    }
  } else {
    rv = ssm->GetChannelResultPrincipal(aOldChannel,
                                        getter_AddRefs(oldPrincipal));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      LOG(("Can't obtain the principal from the previous channel"));
      return;
    }
  }

  newLoadInfo->SetNeedForCheckingAntiTrackingHeuristic(
      ShouldCheckRedirectHeuristic(aOldChannel, aOldURI, oldPrincipal));
}

void FinishAntiTrackingRedirectHeuristic(nsIChannel* aNewChannel,
                                         nsIURI* aNewURI) {
  MOZ_ASSERT(aNewChannel);
  MOZ_ASSERT(aNewURI);

  // This heuristic works only on the parent process.
  if (!XRE_IsParentProcess()) {
    return;
  }

  if (!StaticPrefs::privacy_antitracking_enableWebcompat() ||
      !StaticPrefs::privacy_restrict3rdpartystorage_heuristic_redirect()) {
    return;
  }

  nsCOMPtr<nsIHttpChannel> newChannel = do_QueryInterface(aNewChannel);
  if (!newChannel) {
    return;
  }

  LOG_SPEC(("Finishing redirect-heuristic for the redirect to %s", _spec),
           aNewURI);

  nsCOMPtr<nsILoadInfo> newLoadInfo = newChannel->LoadInfo();
  MOZ_ASSERT(newLoadInfo);

  // Bailing out early if there is no need to do the heuristic.
  if (!newLoadInfo->GetNeedForCheckingAntiTrackingHeuristic()) {
    return;
  }

  if (!ShouldRedirectHeuristicApply(aNewChannel, aNewURI)) {
    return;
  }

  nsIScriptSecurityManager* ssm =
      nsScriptSecurityManager::GetScriptSecurityManager();
  MOZ_ASSERT(ssm);

  const nsTArray<nsCOMPtr<nsIRedirectHistoryEntry>>& chain =
      newLoadInfo->RedirectChain();

  if (chain.IsEmpty()) {
    LOG(("Can't obtain the redirect chain"));
    return;
  }

  nsCOMPtr<nsIPrincipal> oldPrincipal;
  uint32_t idx =
      newLoadInfo->GetAllowListFutureDocumentsCreatedFromThisRedirectChain()
          ? 0
          : chain.Length() - 1;

  nsresult rv = chain[idx]->GetPrincipal(getter_AddRefs(oldPrincipal));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    LOG(("Can't obtain the principal from the redirect chain"));
    return;
  }

  nsCOMPtr<nsIPrincipal> newPrincipal;
  rv =
      ssm->GetChannelResultPrincipal(aNewChannel, getter_AddRefs(newPrincipal));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    LOG(("Can't obtain the principal from the new channel"));
    return;
  }

  if (oldPrincipal->Equals(newPrincipal)) {
    LOG(("No permission needed for same principals."));
    return;
  }

  nsAutoCString oldOrigin;
  rv = oldPrincipal->GetOriginNoSuffix(oldOrigin);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    LOG(("Can't get the origin from the old Principal"));
    return;
  }

  nsAutoCString newOrigin;
  rv = newPrincipal->GetOriginNoSuffix(newOrigin);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    LOG(("Can't get the origin from the new Principal"));
    return;
  }

  LOG(("Adding a first-party storage exception for %s...", newOrigin.get()));

  LOG(("Saving the permission: oldOrigin=%s, grantedOrigin=%s", oldOrigin.get(),
       newOrigin.get()));

  // Any new redirect from this loadInfo must be considered as granted.
  newLoadInfo->SetAllowListFutureDocumentsCreatedFromThisRedirectChain(true);

  uint64_t innerWindowID;
  (void)newChannel->GetTopLevelContentWindowId(&innerWindowID);

  nsAutoString errorText;
  AutoTArray<nsString, 2> params = {NS_ConvertUTF8toUTF16(newOrigin),
                                    NS_ConvertUTF8toUTF16(oldOrigin)};
  rv = nsContentUtils::FormatLocalizedString(
      PropertiesFile::NECKO_PROPERTIES, "CookieAllowedForOriginByHeuristic",
      params, errorText);
  if (NS_SUCCEEDED(rv)) {
    nsContentUtils::ReportToConsoleByWindowID(
        errorText, nsIScriptError::warningFlag, ANTITRACKING_CONSOLE_CATEGORY,
        innerWindowID);
  }

  glean::contentblocking::storage_access_granted_count
      .EnumGet(glean::contentblocking::StorageAccessGrantedCountLabel::
                   eStoragegranted)
      .Add();
  glean::contentblocking::storage_access_granted_count
      .EnumGet(glean::contentblocking::StorageAccessGrantedCountLabel::
                   eRedirecttracker)
      .Add();

  // We don't need to test if it is a tracker, since we already know it is one!
  glean::contentblocking::storage_access_granted_count
      .EnumGet(glean::contentblocking::StorageAccessGrantedCountLabel::
                   eStoragegrantedCt)
      .Add();
  glean::contentblocking::storage_access_granted_count
      .EnumGet(glean::contentblocking::StorageAccessGrantedCountLabel::
                   eRedirecttrackerCt)
      .Add();

  // We don't care about this promise because the operation is actually sync.
  RefPtr<StorageAccessAPIHelper::ParentAccessGrantPromise> promise =
      StorageAccessAPIHelper::SaveAccessForOriginOnParentProcess(
          newPrincipal, oldPrincipal,
          StorageAccessAPIHelper::StorageAccessPromptChoices::eAllow, false,
          StaticPrefs::privacy_restrict3rdpartystorage_expiration_redirect());
  (void)promise;
}

}  // namespace mozilla
