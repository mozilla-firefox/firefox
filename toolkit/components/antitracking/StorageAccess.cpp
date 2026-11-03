/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "StorageAccess.h"

#include "mozilla/BasePrincipal.h"
#include "mozilla/Components.h"
#include "mozilla/dom/Document.h"
#include "mozilla/net/CookieJarSettings.h"
#include "mozilla/PermissionManager.h"
#include "mozilla/StaticPrefs_browser.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/StaticPrefs_privacy.h"
#include "mozilla/StorageAccess.h"
#include "nsAboutProtocolUtils.h"
#include "nsContentUtils.h"
#include "nsGlobalWindowInner.h"
#include "nsICookiePermission.h"
#include "nsICookieService.h"
#include "nsICookieJarSettings.h"
#include "nsIHttpChannel.h"
#include "nsIPermission.h"
#include "nsIWebProgressListener.h"
#include "nsNetUtil.h"
#include "nsScriptSecurityManager.h"
#include "nsSandboxFlags.h"
#include "AntiTrackingUtils.h"
#include "AntiTrackingLog.h"
#include "ContentBlockingAllowList.h"
#include "mozIThirdPartyUtil.h"

using namespace mozilla;
using namespace mozilla::dom;
using mozilla::net::CookieJarSettings;

// This internal method returns ACCESS_DENY if the access is denied,
// ACCESS_DEFAULT if unknown, some other access code if granted.
uint32_t mozilla::detail::CheckCookiePermissionForPrincipal(
    nsICookieJarSettings* aCookieJarSettings, nsIPrincipal* aPrincipal) {
  MOZ_ASSERT(aCookieJarSettings);
  MOZ_ASSERT(aPrincipal);

  uint32_t cookiePermission = nsICookiePermission::ACCESS_DEFAULT;
  if (!aPrincipal->GetIsContentPrincipal()) {
    return cookiePermission;
  }

  nsresult rv =
      aCookieJarSettings->CookiePermission(aPrincipal, &cookiePermission);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nsICookiePermission::ACCESS_DEFAULT;
  }

  // If we have a custom cookie permission, let's use it.
  return cookiePermission;
}

/*
 * Checks if storage for a given principal is permitted by the user's
 * preferences.
 *
 * Ways this function is used:
 * - aPrincipal, aWindow, optional aURI, others don't care: does this principal
 * have storage access, testing this window's sandboxing and if it is
 * third-party. If aURI is provided, we use that for the window's third party
 * comparisons.
 * - aPrincipal, aChannel, aWindow=nullptr, others don't care: does this
 * principal have storage access, testing if this channel is third-party. Note
 * that this ignores aURI.
 * - aPrincipal, optional aCookieJarSettings, aWindow=nullptr, aChannel=nullptr,
 * aURI don't care: does this principal have storage access (assuming it is in a
 * first-party context and not sandboxed). If we aren't given a
 * cookieJarSettings, we build one with the principal.
 *
 * In all of these cases, we test:
 * - if aPrincipal is a NullPrincipal, denying
 * - if this is for an about URI, allowing (maybe with private browsing
 * constraints) We test the aWindow's extant doc's URI's, aURI's, and
 * aPrincipal's scheme to be "about".
 *
 * We also send a decision to the  ContentBlockingNotifier if we have aWindow or
 * aChannel and didn't stop at the NullPrincipal or about: checks.
 *
 * Used in the implementation of StorageAllowedForWindow,
 * StorageAllowedForDocument, StorageAllowedForChannel and
 * StorageAllowedForServiceWorker.
 */
static StorageAccess InternalStorageAllowedCheck(
    nsIPrincipal* aPrincipal, nsPIDOMWindowInner* aWindow, nsIURI* aURI,
    nsIChannel* aChannel, nsICookieJarSettings* aCookieJarSettings,
    uint32_t& aRejectedReason) {
  MOZ_ASSERT(aPrincipal);

  aRejectedReason = 0;

  StorageAccess access = StorageAccess::eAllow;

  // We don't allow storage on the null principal, in general. Even if the
  // calling context is chrome.
  if (aPrincipal->GetIsNullPrincipal()) {
    return StorageAccess::eDeny;
  }

  nsCOMPtr<nsIURI> documentURI;
  if (aWindow) {
    // If the document is sandboxed, then it is not permitted to use storage
    Document* document = aWindow->GetExtantDoc();
    if (document && document->GetSandboxFlags() & SANDBOXED_ORIGIN) {
      return StorageAccess::eDeny;
    }

    // Check if we are in private browsing, and record that fact
    if (document && document->IsInPrivateBrowsing()) {
      access = StorageAccess::ePrivateBrowsing;
    }

    // Get the document URI for the below about: URI check.
    documentURI = document ? document->GetDocumentURI() : nullptr;
  }

  // About URIs are allowed to access storage, even if they don't have chrome
  // privileges. If this is not desired, than the consumer will have to
  // implement their own restriction functionality.
  //
  // This is due to backwards-compatibility and the state of storage access
  // before the introducton of InternalStorageAllowedCheck:
  //
  // BEFORE:
  // localStorage, caches: allowed in 3rd-party iframes always
  // IndexedDB: allowed in 3rd-party iframes only if 3rd party URI is an about:
  //   URI within a specific allowlist
  //
  // AFTER:
  // localStorage, caches: allowed in 3rd-party iframes by default. Preference
  //   can be set to disable in 3rd-party, which will not disallow in about:
  //   URIs.
  // IndexedDB: allowed in 3rd-party iframes by default. Preference can be set
  //   to disable in 3rd-party, which will disallow in about: URIs, unless they
  //   are within a specific allowlist.
  //
  // This means that behavior for storage with internal about: URIs should not
  // be affected, which is desireable due to the lack of automated testing for
  // about: URIs with these preferences set, and the importance of the correct
  // functioning of these URIs even with custom preferences.
  //
  // We need to check the aURI or the document URI here instead of only checking
  // the URI from the principal. Because the principal might not have a URI if
  // it is a system principal.
  if ((aURI && aURI->SchemeIs("about") &&
       !NS_IsContentAccessibleAboutURI(aURI)) ||
      (documentURI && documentURI->SchemeIs("about") &&
       !NS_IsContentAccessibleAboutURI(documentURI)) ||
      aPrincipal->SchemeIs("about")) {
    return access;
  }

  bool disabled = true;
  if (aWindow) {
    nsIURI* documentURI = aURI ? aURI : aWindow->GetDocumentURI();
    disabled = !documentURI || !ShouldAllowAccessFor(aWindow, documentURI, true,
                                                     &aRejectedReason);

    // If the window is a third-party tracker, we should set the rejected reason
    // to partitioned tracker.
    uint32_t rejectedReason = aRejectedReason;
    if (aRejectedReason ==
            static_cast<uint32_t>(
                nsIWebProgressListener::STATE_COOKIES_PARTITIONED_FOREIGN) &&
        nsContentUtils::IsThirdPartyTrackingResourceWindow(aWindow)) {
      rejectedReason =
          nsIWebProgressListener::STATE_COOKIES_PARTITIONED_TRACKER;
    }

    ContentBlockingNotifier::OnDecision(
        aWindow,
        disabled ? ContentBlockingNotifier::BlockingDecision::eBlock
                 : ContentBlockingNotifier::BlockingDecision::eAllow,
        rejectedReason);
  } else if (aChannel) {
    disabled = false;
    nsCOMPtr<nsIURI> uri;
    nsresult rv = aChannel->GetURI(getter_AddRefs(uri));
    if (!NS_WARN_IF(NS_FAILED(rv))) {
      disabled = !ShouldAllowAccessFor(aChannel, uri, &aRejectedReason);
    }

    // If the channel is a third-party tracker, we should set the rejected
    // reason to partitioned tracker.
    uint32_t rejectedReason = aRejectedReason;
    if (AntiTrackingUtils::IsThirdPartyTrackingChannel(aChannel) &&
        aRejectedReason ==
            static_cast<uint32_t>(
                nsIWebProgressListener::STATE_COOKIES_PARTITIONED_FOREIGN)) {
      rejectedReason =
          nsIWebProgressListener::STATE_COOKIES_PARTITIONED_TRACKER;
    }

    ContentBlockingNotifier::OnDecision(
        aChannel,
        disabled ? ContentBlockingNotifier::BlockingDecision::eBlock
                 : ContentBlockingNotifier::BlockingDecision::eAllow,
        rejectedReason);
  } else {
    MOZ_ASSERT(aPrincipal);
    nsCOMPtr<nsICookieJarSettings> cookieJarSettings = aCookieJarSettings;
    if (!cookieJarSettings) {
      cookieJarSettings = net::CookieJarSettings::Create(aPrincipal);
    }
    disabled = !ShouldAllowAccessFor(aPrincipal, aCookieJarSettings);
  }

  if (!disabled) {
    return access;
  }

  // We want to have a partitioned storage only for trackers.
  // XXX: We should probably remove the check here because this was added for
  //      partitioned tracker only. This was never shipped.
  if (aRejectedReason ==
          static_cast<uint32_t>(
              nsIWebProgressListener::STATE_COOKIES_BLOCKED_TRACKER) ||
      aRejectedReason ==
          static_cast<uint32_t>(
              nsIWebProgressListener::STATE_COOKIES_BLOCKED_SOCIALTRACKER)) {
    return StorageAccess::ePartitionTrackersOrDeny;
  }

  // We want to have a partitioned storage for all third parties.
  if (aRejectedReason ==
      static_cast<uint32_t>(
          nsIWebProgressListener::STATE_COOKIES_PARTITIONED_FOREIGN)) {
    return StorageAccess::ePartitionForeignOrDeny;
  }

  return StorageAccess::eDeny;
}

/**
 * Wrapper around InternalStorageAllowedCheck which caches the check result on
 * the inner window to improve performance. nsGlobalWindowInner is responsible
 * for invalidating the cache state if storage access changes during window
 * lifetime.
 */
static StorageAccess InternalStorageAllowedCheckCached(
    nsIPrincipal* aPrincipal, nsPIDOMWindowInner* aWindow, nsIURI* aURI,
    nsIChannel* aChannel, nsICookieJarSettings* aCookieJarSettings,
    uint32_t& aRejectedReason) {
  // If enabled, check if we have already computed the storage access field
  // for this window. This avoids repeated calls to
  // InternalStorageAllowedCheck.
  nsGlobalWindowInner* win = nullptr;
  if (aWindow) {
    win = nsGlobalWindowInner::Cast(aWindow);

    Maybe<StorageAccess> storageAccess =
        win->GetStorageAllowedCache(aRejectedReason);
    if (storageAccess.isSome()) {
      return storageAccess.value();
    }
  }

  StorageAccess result = InternalStorageAllowedCheck(
      aPrincipal, aWindow, aURI, aChannel, aCookieJarSettings, aRejectedReason);
  if (win) {
    // Remember check result for the lifetime of the window. It's the windows
    // responsibility to invalidate this field if storage access changes
    // because a storage access permission is granted.
    win->SetStorageAllowedCache(result, aRejectedReason);
  }

  return result;
}

namespace mozilla {

StorageAccess StorageAllowedForWindow(nsPIDOMWindowInner* aWindow,
                                      uint32_t* aRejectedReason) {
  uint32_t rejectedReason;
  if (!aRejectedReason) {
    aRejectedReason = &rejectedReason;
  }

  *aRejectedReason = 0;

  if (Document* document = aWindow->GetExtantDoc()) {
    nsCOMPtr<nsIPrincipal> principal = document->NodePrincipal();
    // Note that GetChannel() below may return null, but that's OK, since the
    // callee is able to deal with a null channel argument, and if passed
    // null, will only fail to notify the UI in case storage gets blocked.
    nsIChannel* channel = document->GetChannel();
    return InternalStorageAllowedCheckCached(
        principal, aWindow, nullptr, channel, document->CookieJarSettings(),
        *aRejectedReason);
  }

  // No document? Try checking Private Browsing Mode without document
  if (const nsCOMPtr<nsIGlobalObject> global = aWindow->AsGlobal()) {
    if (const nsCOMPtr<nsIPrincipal> principal = global->PrincipalOrNull()) {
      if (principal->GetIsInPrivateBrowsing()) {
        return StorageAccess::ePrivateBrowsing;
      }
    }
  }

  // Everything failed? Let's return a generic rejected reason.
  return StorageAccess::eDeny;
}

StorageAccess StorageAllowedForDocument(const Document* aDoc) {
  StorageAccess cookieAllowed = CookieAllowedForDocument(aDoc);
  if (!aDoc->IsTopLevelContentDocument() &&
      cookieAllowed > StorageAccess::eDeny) {
    return StorageAccess::ePartitionForeignOrDeny;
  }
  return cookieAllowed;
}

StorageAccess CookieAllowedForDocument(const Document* aDoc) {
  MOZ_ASSERT(aDoc);

  if (nsPIDOMWindowInner* inner = aDoc->GetInnerWindow()) {
    nsCOMPtr<nsIPrincipal> principal = aDoc->NodePrincipal();
    // Note that GetChannel() below may return null, but that's OK, since the
    // callee is able to deal with a null channel argument, and if passed
    // null, will only fail to notify the UI in case storage gets blocked.
    nsIChannel* channel = aDoc->GetChannel();

    uint32_t rejectedReason = 0;
    return InternalStorageAllowedCheckCached(
        principal, inner, nullptr, channel,
        const_cast<Document*>(aDoc)->CookieJarSettings(), rejectedReason);
  }

  return StorageAccess::eDeny;
}

StorageAccess StorageAllowedForNewWindow(nsIPrincipal* aPrincipal, nsIURI* aURI,
                                         nsPIDOMWindowInner* aParent) {
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aURI);
  // parent may be nullptr

  uint32_t rejectedReason = 0;
  nsCOMPtr<nsICookieJarSettings> cjs;
  if (aParent && aParent->GetExtantDoc()) {
    cjs = aParent->GetExtantDoc()->CookieJarSettings();
  } else {
    cjs = net::CookieJarSettings::Create(aPrincipal);
  }
  return InternalStorageAllowedCheck(aPrincipal, aParent, aURI, nullptr, cjs,
                                     rejectedReason);
}

StorageAccess StorageAllowedForChannel(nsIChannel* aChannel) {
  MOZ_DIAGNOSTIC_ASSERT(nsContentUtils::GetSecurityManager());
  MOZ_DIAGNOSTIC_ASSERT(aChannel);

  nsCOMPtr<nsIPrincipal> principal;
  (void)nsContentUtils::GetSecurityManager()->GetChannelResultPrincipal(
      aChannel, getter_AddRefs(principal));
  NS_ENSURE_TRUE(principal, StorageAccess::eDeny);

  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  nsCOMPtr<nsICookieJarSettings> cookieJarSettings;
  nsresult rv =
      loadInfo->GetCookieJarSettings(getter_AddRefs(cookieJarSettings));
  NS_ENSURE_SUCCESS(rv, StorageAccess::eDeny);

  uint32_t rejectedReason = 0;
  StorageAccess result = InternalStorageAllowedCheck(
      principal, nullptr, nullptr, aChannel, cookieJarSettings, rejectedReason);

  return result;
}

StorageAccess StorageAllowedForServiceWorker(
    nsIPrincipal* aPrincipal, nsICookieJarSettings* aCookieJarSettings) {
  uint32_t rejectedReason = 0;
  return InternalStorageAllowedCheck(aPrincipal, nullptr, nullptr, nullptr,
                                     aCookieJarSettings, rejectedReason);
}

bool ShouldPartitionStorage(StorageAccess aAccess) {
  return aAccess == StorageAccess::ePartitionTrackersOrDeny ||
         aAccess == StorageAccess::ePartitionForeignOrDeny;
}

bool ShouldPartitionStorage(uint32_t aRejectedReason) {
  return aRejectedReason ==
             static_cast<uint32_t>(
                 nsIWebProgressListener::STATE_COOKIES_BLOCKED_TRACKER) ||
         aRejectedReason ==
             static_cast<uint32_t>(
                 nsIWebProgressListener::STATE_COOKIES_BLOCKED_SOCIALTRACKER) ||
         aRejectedReason ==
             static_cast<uint32_t>(
                 nsIWebProgressListener::STATE_COOKIES_PARTITIONED_FOREIGN);
}

bool StoragePartitioningEnabled(StorageAccess aAccess,
                                nsICookieJarSettings* aCookieJarSettings) {
  return aAccess == StorageAccess::ePartitionForeignOrDeny &&
         aCookieJarSettings->GetCookieBehavior() ==
             nsICookieService::BEHAVIOR_PARTITION_FOREIGN;
}

bool StoragePartitioningEnabled(uint32_t aRejectedReason,
                                nsICookieJarSettings* aCookieJarSettings) {
  return aRejectedReason ==
             static_cast<uint32_t>(
                 nsIWebProgressListener::STATE_COOKIES_PARTITIONED_FOREIGN) &&
         aCookieJarSettings->GetCookieBehavior() ==
             nsICookieService::BEHAVIOR_PARTITION_FOREIGN;
}

int32_t CookiesBehavior(Document* a3rdPartyDocument) {
  MOZ_ASSERT(a3rdPartyDocument);

  // WebExtensions principals always get BEHAVIOR_ACCEPT as cookieBehavior
  // (See Bug 1406675 and Bug 1525917 for rationale).
  if (BasePrincipal::Cast(a3rdPartyDocument->NodePrincipal())->AddonPolicy()) {
    return nsICookieService::BEHAVIOR_ACCEPT;
  }

  return a3rdPartyDocument->CookieJarSettings()->GetCookieBehavior();
}

bool CookiesBehaviorRejectsThirdPartyContexts(Document* aDocument) {
  MOZ_ASSERT(aDocument);

  // WebExtensions principals always get BEHAVIOR_ACCEPT as cookieBehavior
  // (See Bug 1406675 and Bug 1525917 for rationale).
  if (BasePrincipal::Cast(aDocument->NodePrincipal())->AddonPolicy()) {
    return false;
  }

  return aDocument->CookieJarSettings()->GetRejectThirdPartyContexts();
}

int32_t CookiesBehavior(nsILoadInfo* aLoadInfo, nsIURI* a3rdPartyURI) {
  MOZ_ASSERT(aLoadInfo);
  MOZ_ASSERT(a3rdPartyURI);

  // WebExtensions 3rd party URI always get BEHAVIOR_ACCEPT as cookieBehavior,
  // this is semantically equivalent to the principal having a AddonPolicy().
  if (a3rdPartyURI->SchemeIs("moz-extension")) {
    return nsICookieService::BEHAVIOR_ACCEPT;
  }

  nsCOMPtr<nsICookieJarSettings> cookieJarSettings;
  nsresult rv =
      aLoadInfo->GetCookieJarSettings(getter_AddRefs(cookieJarSettings));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nsICookieService::BEHAVIOR_REJECT;
  }

  return cookieJarSettings->GetCookieBehavior();
}

int32_t CookiesBehavior(nsIPrincipal* aPrincipal,
                        nsICookieJarSettings* aCookieJarSettings) {
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aCookieJarSettings);

  // WebExtensions principals always get BEHAVIOR_ACCEPT as cookieBehavior
  // (See Bug 1406675 for rationale).
  if (BasePrincipal::Cast(aPrincipal)->AddonPolicy()) {
    return nsICookieService::BEHAVIOR_ACCEPT;
  }

  return aCookieJarSettings->GetCookieBehavior();
}

bool ShouldAllowAccessFor(nsPIDOMWindowInner* aWindow, nsIURI* aURI,
                          bool aCookies, uint32_t* aRejectedReason) {
  MOZ_ASSERT(aWindow);
  MOZ_ASSERT(aURI);

  // Let's avoid a null check on aRejectedReason everywhere else.
  uint32_t rejectedReason = 0;
  if (!aRejectedReason) {
    aRejectedReason = &rejectedReason;
  }

  LOG_SPEC(("Computing whether window %p has access to URI %s", aWindow, _spec),
           aURI);

  nsGlobalWindowInner* innerWindow = nsGlobalWindowInner::Cast(aWindow);
  Document* document = innerWindow->GetExtantDoc();
  if (!document) {
    LOG(("Our window has no document"));
    return false;
  }

  uint32_t cookiePermission = detail::CheckCookiePermissionForPrincipal(
      document->CookieJarSettings(), document->NodePrincipal());
  if (cookiePermission != nsICookiePermission::ACCESS_DEFAULT) {
    LOG(
        ("CheckCookiePermissionForPrincipal() returned a non-default access "
         "code (%d) for window's principal, returning %s",
         int(cookiePermission),
         cookiePermission != nsICookiePermission::ACCESS_DENY ? "success"
                                                              : "failure"));
    if (cookiePermission != nsICookiePermission::ACCESS_DENY) {
      return true;
    }

    *aRejectedReason =
        nsIWebProgressListener::STATE_COOKIES_BLOCKED_BY_PERMISSION;
    return false;
  }

  int32_t behavior = CookiesBehavior(document);
  if (behavior == nsICookieService::BEHAVIOR_ACCEPT) {
    LOG(("The cookie behavior pref mandates accepting all cookies!"));
    return true;
  }

  if (ContentBlockingAllowList::Check(aWindow)) {
    return true;
  }

  if (behavior == nsICookieService::BEHAVIOR_REJECT) {
    LOG(("The cookie behavior pref mandates rejecting all cookies!"));
    *aRejectedReason = nsIWebProgressListener::STATE_COOKIES_BLOCKED_ALL;
    return false;
  }

  // As a performance optimization, we only perform this check for
  // BEHAVIOR_REJECT_FOREIGN and BEHAVIOR_LIMIT_FOREIGN.  For
  // BEHAVIOR_REJECT_TRACKER and
  // BEHAVIOR_PARTITION_FOREIGN, third-partiness is
  // implicily checked later below.
  if (behavior != nsICookieService::BEHAVIOR_REJECT_TRACKER &&
      behavior != nsICookieService::BEHAVIOR_PARTITION_FOREIGN) {
    // Let's check if this is a 3rd party context.
    if (!AntiTrackingUtils::IsThirdPartyWindow(aWindow, aURI)) {
      LOG(("Our window isn't a third-party window"));
      return true;
    }
  }

  if (behavior == nsICookieService::BEHAVIOR_REJECT_FOREIGN ||
      behavior == nsICookieService::BEHAVIOR_LIMIT_FOREIGN) {
    // XXX For non-cookie forms of storage, we handle BEHAVIOR_LIMIT_FOREIGN
    // by simply rejecting the request to use the storage. In the future, if
    // we change the meaning of BEHAVIOR_LIMIT_FOREIGN to be one which makes
    // sense for non-cookie storage types, this may change.
    LOG(("Nothing more to do due to the behavior code %d", int(behavior)));
    *aRejectedReason = nsIWebProgressListener::STATE_COOKIES_BLOCKED_FOREIGN;
    return false;
  }

  // The document has been allowlisted. We can return from here directly.
  if (document->HasStorageAccessPermissionGrantedByAllowList()) {
    return true;
  }

  MOZ_ASSERT(behavior == nsICookieService::BEHAVIOR_REJECT_TRACKER ||
             behavior == nsICookieService::BEHAVIOR_PARTITION_FOREIGN);

  uint32_t blockedReason =
      nsIWebProgressListener::STATE_COOKIES_BLOCKED_TRACKER;

  if (behavior == nsICookieService::BEHAVIOR_REJECT_TRACKER) {
    if (!nsContentUtils::IsThirdPartyTrackingResourceWindow(aWindow)) {
      LOG(("Our window isn't a third-party tracking window"));
      return true;
    }

    if (AntiTrackingUtils::IsThirdPartySocialTrackingChannel(
            document->GetChannel())) {
      blockedReason =
          nsIWebProgressListener::STATE_COOKIES_BLOCKED_SOCIALTRACKER;
    }
  } else if (behavior == nsICookieService::BEHAVIOR_PARTITION_FOREIGN) {
    if (nsContentUtils::IsThirdPartyTrackingResourceWindow(aWindow) ||
        AntiTrackingUtils::IsThirdPartyWindow(aWindow, aURI)) {
      LOG(("We're in the third-party context, storage should be partitioned"));
      // fall through, but remember that we're partitioning.
      blockedReason = nsIWebProgressListener::STATE_COOKIES_PARTITIONED_FOREIGN;
    } else {
      LOG(("Our window isn't a third-party window, storage is allowed"));
      return true;
    }
  } else {
    MOZ_ASSERT_UNREACHABLE(
        "This should be an exhaustive list of cookie behaviors possible "
        "here.");
  }

  Document* doc = aWindow->GetExtantDoc();
  // Make sure storage access isn't disabled
  if (doc && (doc->StorageAccessSandboxed())) {
    LOG(("Our document is sandboxed"));
    *aRejectedReason = blockedReason;
    return false;
  }

  // "Storage access granted" only affects cookie access for third party
  // documents. So if we are looking if we should allow access for cookies,
  // then test if that permission is enabled on this document.
  // Document::UsingStorageAccess first checks if storage access granted is
  // cached in the inner window, if no, it then checks the storage permission
  // flag in the channel's loadinfo
  bool allowed = aCookies && document->UsingStorageAccess();

  if (!allowed) {
    *aRejectedReason = blockedReason;
  } else {
    if (MOZ_LOG_TEST(gAntiTrackingLog, mozilla::LogLevel::Debug) &&
        aWindow->UsingStorageAccess()) {
      LOG(("Permission stored in the window. All good."));
    }
  }

  return allowed;
}

bool ShouldAllowAccessFor(nsIChannel* aChannel, nsIURI* aURI,
                          uint32_t* aRejectedReason) {
  MOZ_ASSERT(aURI);
  MOZ_ASSERT(aChannel);

  // Let's avoid a null check on aRejectedReason everywhere else.
  uint32_t rejectedReason = 0;
  if (!aRejectedReason) {
    aRejectedReason = &rejectedReason;
  }

  nsIScriptSecurityManager* ssm =
      nsScriptSecurityManager::GetScriptSecurityManager();
  MOZ_ASSERT(ssm);

  nsCOMPtr<nsIURI> channelURI;
  nsresult rv = NS_GetFinalChannelURI(aChannel, getter_AddRefs(channelURI));
  if (NS_FAILED(rv)) {
    LOG(("Failed to get the channel final URI, bail out early"));
    return true;
  }
  LOG_SPEC(
      ("Computing whether channel %p has access to URI %s", aChannel, _spec),
      channelURI);

  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  nsCOMPtr<nsICookieJarSettings> cookieJarSettings;
  rv = loadInfo->GetCookieJarSettings(getter_AddRefs(cookieJarSettings));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    LOG(
        ("Failed to get the cookie jar settings from the loadinfo, bail out "
         "early"));
    return true;
  }

  nsCOMPtr<nsIPrincipal> channelPrincipal;
  rv = ssm->GetChannelURIPrincipal(aChannel, getter_AddRefs(channelPrincipal));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    LOG(("No channel principal, bail out early"));
    return false;
  }

  uint32_t cookiePermission = detail::CheckCookiePermissionForPrincipal(
      cookieJarSettings, channelPrincipal);
  if (cookiePermission != nsICookiePermission::ACCESS_DEFAULT) {
    LOG(
        ("CheckCookiePermissionForPrincipal() returned a non-default access "
         "code (%d) for channel's principal, returning %s",
         int(cookiePermission),
         cookiePermission != nsICookiePermission::ACCESS_DENY ? "success"
                                                              : "failure"));
    if (cookiePermission != nsICookiePermission::ACCESS_DENY) {
      return true;
    }

    *aRejectedReason =
        nsIWebProgressListener::STATE_COOKIES_BLOCKED_BY_PERMISSION;
    return false;
  }

  if (!channelURI) {
    LOG(("No channel uri, bail out early"));
    return false;
  }

  int32_t behavior = CookiesBehavior(loadInfo, channelURI);
  if (behavior == nsICookieService::BEHAVIOR_ACCEPT) {
    LOG(("The cookie behavior pref mandates accepting all cookies!"));
    return true;
  }

  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aChannel);

  if (httpChannel && ContentBlockingAllowList::Check(httpChannel)) {
    return true;
  }

  if (behavior == nsICookieService::BEHAVIOR_REJECT) {
    LOG(("The cookie behavior pref mandates rejecting all cookies!"));
    *aRejectedReason = nsIWebProgressListener::STATE_COOKIES_BLOCKED_ALL;
    return false;
  }

  nsCOMPtr<mozIThirdPartyUtil> thirdPartyUtil =
      components::ThirdPartyUtil::Service();
  if (!thirdPartyUtil) {
    LOG(("No thirdPartyUtil, bail out early"));
    return true;
  }

  bool thirdParty = false;
  rv = thirdPartyUtil->IsThirdPartyChannel(aChannel, aURI, &thirdParty);
  // Grant if it's not a 3rd party.
  // Be careful to check the return value of IsThirdPartyChannel, since
  // IsThirdPartyChannel() will fail if the channel's loading principal is the
  // system principal...
  if (NS_SUCCEEDED(rv) && !thirdParty) {
    LOG(("Our channel isn't a third-party channel"));
    return true;
  }

  if (behavior == nsICookieService::BEHAVIOR_REJECT_FOREIGN ||
      behavior == nsICookieService::BEHAVIOR_LIMIT_FOREIGN) {
    // XXX For non-cookie forms of storage, we handle BEHAVIOR_LIMIT_FOREIGN
    // by simply rejecting the request to use the storage. In the future, if
    // we change the meaning of BEHAVIOR_LIMIT_FOREIGN to be one which makes
    // sense for non-cookie storage types, this may change.
    LOG(("Nothing more to do due to the behavior code %d", int(behavior)));
    *aRejectedReason = nsIWebProgressListener::STATE_COOKIES_BLOCKED_FOREIGN;
    return false;
  }

  // The channel has been allowlisted. We can return from here.
  if (loadInfo->GetStoragePermission() ==
      nsILoadInfo::StoragePermissionAllowListed) {
    return true;
  }

  MOZ_ASSERT(behavior == nsICookieService::BEHAVIOR_REJECT_TRACKER ||
             behavior == nsICookieService::BEHAVIOR_PARTITION_FOREIGN);

  uint32_t blockedReason =
      nsIWebProgressListener::STATE_COOKIES_BLOCKED_TRACKER;

  if (behavior == nsICookieService::BEHAVIOR_REJECT_TRACKER) {
    if (!AntiTrackingUtils::IsThirdPartyTrackingChannel(aChannel)) {
      LOG(("Our channel isn't a third-party tracking channel"));
      return true;
    }

    if (AntiTrackingUtils::IsThirdPartySocialTrackingChannel(aChannel)) {
      blockedReason =
          nsIWebProgressListener::STATE_COOKIES_BLOCKED_SOCIALTRACKER;
    }
  } else if (behavior == nsICookieService::BEHAVIOR_PARTITION_FOREIGN) {
    if (AntiTrackingUtils::IsThirdPartyTrackingChannel(aChannel) ||
        AntiTrackingUtils::IsThirdPartyChannel(aChannel)) {
      LOG(("We're in the third-party context, storage should be partitioned"));
      // fall through but remember that we're partitioning.
      blockedReason = nsIWebProgressListener::STATE_COOKIES_PARTITIONED_FOREIGN;
    } else {
      LOG(("Our channel isn't a third-party channel, storage is allowed"));
      return true;
    }
  } else {
    MOZ_ASSERT_UNREACHABLE(
        "This should be an exhaustive list of cookie behaviors possible "
        "here.");
  }

  RefPtr<BrowsingContext> targetBC;
  rv = loadInfo->GetTargetBrowsingContext(getter_AddRefs(targetBC));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    LOG(("Failed to get the channel's target browsing context"));
    return false;
  }

  // If we cannot get the target browsing context from the loadInfo, the
  // channel could be a fetch request from a worker scope. In this case, we
  // get the target browsing context from the worker associated browsing
  // context instead.
  if (!targetBC) {
    rv = loadInfo->GetAssociatedBrowsingContext(getter_AddRefs(targetBC));
  }

  // We could have no target BC for the channel if it's for loading the script
  // for remote workers, i.e. shared workers and service workers. In this
  // case, we also don't have document, so we can skip the sandbox and the
  // document check.
  if (!targetBC) {
    LOG(("No browsing context is available for the channel."));
  }

  if (targetBC &&
      Document::StorageAccessSandboxed(targetBC->GetSandboxFlags())) {
    LOG(("Our document is sandboxed"));
    *aRejectedReason = blockedReason;
    return false;
  }

  // Let's see if we have to grant the access for this particular channel.

  // UsingStorageAccess only applies to channels that load
  // documents, for sub-resources loads, just returns the result from
  // loadInfo.
  bool isDocument = false;
  aChannel->GetIsDocument(&isDocument);

  if (targetBC && isDocument) {
    nsCOMPtr<nsPIDOMWindowInner> inner =
        AntiTrackingUtils::GetInnerWindow(targetBC);
    if (inner && inner->UsingStorageAccess()) {
      LOG(("Permission stored in the window. All good."));
      return true;
    }
  }

  nsILoadInfo::StoragePermissionState storageAccess =
      loadInfo->GetStoragePermission();
  bool allowed = storageAccess == nsILoadInfo::HasStoragePermission ||
                 storageAccess == nsILoadInfo::StoragePermissionAllowListed;
  if (!allowed) {
    *aRejectedReason = blockedReason;
  }

  return allowed;
}

bool ShouldAllowAccessFor(nsIPrincipal* aPrincipal,
                          nsICookieJarSettings* aCookieJarSettings) {
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aCookieJarSettings);

  uint32_t access =
      detail::CheckCookiePermissionForPrincipal(aCookieJarSettings, aPrincipal);

  if (access != nsICookiePermission::ACCESS_DEFAULT) {
    return access != nsICookiePermission::ACCESS_DENY;
  }

  int32_t behavior = CookiesBehavior(aPrincipal, aCookieJarSettings);
  return behavior != nsICookieService::BEHAVIOR_REJECT;
}

}  // namespace mozilla
