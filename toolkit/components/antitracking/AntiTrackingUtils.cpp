/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AntiTrackingUtils.h"

#include "AntiTrackingLog.h"
#include "ContentBlockingAllowList.h"
#include "HttpBaseChannel.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/Components.h"
#include "mozilla/dom/BrowsingContext.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/net/CookieJarSettings.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/dom/WindowContext.h"
#include "mozilla/net/NeckoChannelParams.h"
#include "mozilla/PermissionManager.h"
#include "mozIThirdPartyUtil.h"
#include "nsGlobalWindowInner.h"
#include "nsHttpChannel.h"
#include "nsIChannel.h"
#include "nsICookieService.h"
#include "nsIEffectiveTLDService.h"
#include "nsIHttpChannel.h"
#include "nsIPermission.h"
#include "nsIURI.h"
#include "nsNetUtil.h"
#include "nsMixedContentBlocker.h"
#include "nsPIDOMWindow.h"
#include "nsPIDOMWindowInlines.h"
#include "nsQueryObject.h"
#include "nsRFPService.h"
#include "nsSandboxFlags.h"
#include "nsScriptSecurityManager.h"
#include "PartitioningExceptionList.h"

#define ANTITRACKING_PERM_KEY "3rdPartyStorage"
#define ANTITRACKING_FRAME_PERM_KEY "3rdPartyFrameStorage"

using namespace mozilla;
using namespace mozilla::dom;

/* static */ already_AddRefed<nsPIDOMWindowInner>
AntiTrackingUtils::GetInnerWindow(BrowsingContext* aBrowsingContext) {
  MOZ_ASSERT(aBrowsingContext);

  nsCOMPtr<nsPIDOMWindowOuter> outer = aBrowsingContext->GetDOMWindow();
  if (!outer) {
    return nullptr;
  }

  nsCOMPtr<nsPIDOMWindowInner> inner = outer->GetCurrentInnerWindow();
  return inner.forget();
}

/* static */ already_AddRefed<nsPIDOMWindowOuter>
AntiTrackingUtils::GetTopWindow(nsPIDOMWindowInner* aWindow) {
  Document* document = aWindow->GetExtantDoc();
  if (!document) {
    return nullptr;
  }

  nsIChannel* channel = document->GetChannel();
  if (!channel) {
    return nullptr;
  }

  nsCOMPtr<nsPIDOMWindowOuter> pwin =
      aWindow->GetBrowsingContext()->Top()->GetDOMWindow();

  if (!pwin) {
    return nullptr;
  }

  return pwin.forget();
}

/* static */
already_AddRefed<nsIURI> AntiTrackingUtils::MaybeGetDocumentURIBeingLoaded(
    nsIChannel* aChannel) {
  nsCOMPtr<nsIURI> uriBeingLoaded;
  nsLoadFlags loadFlags = 0;
  nsresult rv = aChannel->GetLoadFlags(&loadFlags);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }
  if (loadFlags & nsIChannel::LOAD_DOCUMENT_URI) {
    // If the channel being loaded is a document channel, this call may be
    // coming from an OnStopRequest notification, which might mean that our
    // document may still be in the loading process, so we may need to pass in
    // the uriBeingLoaded argument explicitly.
    rv = NS_GetFinalChannelURI(aChannel, getter_AddRefs(uriBeingLoaded));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return nullptr;
    }
  }
  return uriBeingLoaded.forget();
}

// static
void AntiTrackingUtils::CreateStoragePermissionKey(
    const nsACString& aTrackingOrigin, nsACString& aPermissionKey) {
  MOZ_ASSERT(aPermissionKey.IsEmpty());

  static const nsLiteralCString prefix =
      nsLiteralCString(ANTITRACKING_PERM_KEY "^");

  aPermissionKey.SetCapacity(prefix.Length() + aTrackingOrigin.Length());
  aPermissionKey.Append(prefix);
  aPermissionKey.Append(aTrackingOrigin);
}

// static
bool AntiTrackingUtils::CreateStoragePermissionKey(nsIPrincipal* aPrincipal,
                                                   nsACString& aKey) {
  if (!aPrincipal) {
    return false;
  }

  nsAutoCString origin;
  nsresult rv = aPrincipal->GetOriginNoSuffix(origin);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }

  CreateStoragePermissionKey(origin, aKey);
  return true;
}

// static
void AntiTrackingUtils::CreateStorageFramePermissionKey(
    const nsACString& aTrackingSite, nsACString& aPermissionKey) {
  MOZ_ASSERT(aPermissionKey.IsEmpty());

  static const nsLiteralCString prefix =
      nsLiteralCString(ANTITRACKING_FRAME_PERM_KEY "^");

  aPermissionKey.SetCapacity(prefix.Length() + aTrackingSite.Length());
  aPermissionKey.Append(prefix);
  aPermissionKey.Append(aTrackingSite);
}

// static
bool AntiTrackingUtils::CreateStorageFramePermissionKey(
    nsIPrincipal* aPrincipal, nsACString& aKey) {
  MOZ_ASSERT(aPrincipal);

  nsAutoCString site;
  nsresult rv = aPrincipal->GetSiteOriginNoSuffix(site);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }

  CreateStorageFramePermissionKey(site, aKey);
  return true;
}

// static
bool AntiTrackingUtils::IsStorageAccessPermission(nsIPermission* aPermission,
                                                  nsIPrincipal* aPrincipal) {
  MOZ_ASSERT(aPermission);
  MOZ_ASSERT(aPrincipal);

  // The permission key may belong either to a tracking origin on the same
  // origin as the granted origin, or on another origin as the granted origin
  // (for example when a tracker in a third-party context uses window.open to
  // open another origin where that second origin would be the granted origin.)
  // But even in the second case, the type of the permission would still be
  // formed by concatenating the granted origin to the end of the type name
  // (see CreatePermissionKey).  Therefore, we pass in the same argument to
  // both tracking origin and granted origin here in order to compute the
  // shorter permission key and will then do a prefix match on the type of the
  // input permission to see if it is a storage access permission or not.
  nsAutoCString permissionKey;
  bool result = CreateStoragePermissionKey(aPrincipal, permissionKey);
  if (NS_WARN_IF(!result)) {
    return false;
  }

  nsAutoCString type;
  nsresult rv = aPermission->GetType(type);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }

  return StringBeginsWith(type, permissionKey);
}

// static
Maybe<size_t> AntiTrackingUtils::CountSitesAllowStorageAccess(
    nsIPrincipal* aPrincipal) {
  RefPtr<PermissionManager> permManager = PermissionManager::GetInstance();
  if (NS_WARN_IF(!permManager)) {
    return Nothing();
  }

  nsAutoCString prefix;
  AntiTrackingUtils::CreateStoragePermissionKey(aPrincipal, prefix);
  nsAutoCString framePrefix;
  AntiTrackingUtils::CreateStorageFramePermissionKey(aPrincipal, framePrefix);

  using Permissions = nsTArray<RefPtr<nsIPermission>>;
  Permissions perms;
  nsresult rv = permManager->GetAllWithTypePrefix(prefix, perms);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return Nothing();
  }
  Permissions framePermissions;
  rv = permManager->GetAllWithTypePrefix(framePrefix, framePermissions);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return Nothing();
  }
  if (!perms.AppendElements(framePermissions, fallible)) {
    return Nothing();
  }

  // Create a set of unique origins
  using Sites = nsTArray<nsCString>;
  Sites sites;

  // Iterate over all permissions that have a prefix equal to the expected
  // permission we are looking for. This includes two things we need to remove:
  // duplicates and origin strings that have a prefix of aPrincipal's origin
  // string, e.g. https://example.company when aPrincipal is
  // https://example.com.
  for (const auto& perm : perms) {
    nsAutoCString type;
    rv = perm->GetType(type);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return Nothing();
    }

    // Let's make sure that we're not looking at a permission for
    // https://exampletracker.company when we mean to look for the
    // permission for https://exampletracker.com!
    if (type != prefix && type != framePrefix) {
      continue;
    }

    nsCOMPtr<nsIPrincipal> principal;
    rv = perm->GetPrincipal(getter_AddRefs(principal));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return Nothing();
    }

    nsAutoCString site;
    rv = principal->GetSiteOrigin(site);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return Nothing();
    }

    ToLowerCase(site);

    // Append if it would not be a duplicate
    if (sites.IndexOf(site) == Sites::NoIndex) {
      sites.AppendElement(site);
    }
  }

  return Some(sites.Length());
}

// static
bool AntiTrackingUtils::CheckStoragePermission(nsIPrincipal* aPrincipal,
                                               const nsAutoCString& aType,
                                               bool aIsInPrivateBrowsing) {
  RefPtr<PermissionManager> permManager = PermissionManager::GetInstance();
  if (NS_WARN_IF(!permManager)) {
    LOG(("Failed to obtain the permission manager"));
    return false;
  }

  uint32_t result = 0;
  if (aIsInPrivateBrowsing) {
    LOG_PRIN(("Querying the permissions for private modei looking for a "
              "permission of type %s for %s",
              aType.get(), _spec),
             aPrincipal);
    if (!permManager->PermissionAvailable(aPrincipal, aType)) {
      LOG(
          ("Permission isn't available for this principal in the current "
           "process"));
      return false;
    }
    nsTArray<RefPtr<nsIPermission>> permissions;
    nsresult rv = permManager->GetAllForPrincipal(aPrincipal, permissions);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      LOG(("Failed to get the list of permissions"));
      return false;
    }

    bool found = false;
    for (const auto& permission : permissions) {
      if (!permission) {
        LOG(("Couldn't get the permission for unknown reasons"));
        continue;
      }

      nsAutoCString permissionType;
      if (NS_SUCCEEDED(permission->GetType(permissionType)) &&
          permissionType != aType) {
        LOG(("Non-matching permission type: %s", aType.get()));
        continue;
      }

      uint32_t capability = 0;
      if (NS_SUCCEEDED(permission->GetCapability(&capability)) &&
          capability != nsIPermissionManager::ALLOW_ACTION) {
        LOG(("Non-matching permission capability: %d", capability));
        continue;
      }

      uint32_t expirationType = 0;
      if (NS_SUCCEEDED(permission->GetExpireType(&expirationType)) &&
          expirationType != nsIPermissionManager ::EXPIRE_SESSION) {
        LOG(("Non-matching permission expiration type: %d", expirationType));
        continue;
      }

      int64_t expirationTime = 0;
      if (NS_SUCCEEDED(permission->GetExpireTime(&expirationTime)) &&
          expirationTime != 0) {
        LOG(("Non-matching permission expiration time: %" PRId64,
             expirationTime));
        continue;
      }

      LOG(("Found a matching permission"));
      found = true;
      break;
    }

    if (!found) {
      return false;
    }
  } else {
    nsresult rv = permManager->TestPermissionWithoutDefaultsFromPrincipal(
        aPrincipal, aType, &result);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      LOG(("Failed to test the permission"));
      return false;
    }

    LOG_PRIN(
        ("Testing permission type %s for %s resulted in %d (%s)", aType.get(),
         _spec, int(result),
         result == nsIPermissionManager::ALLOW_ACTION ? "success" : "failure"),
        aPrincipal);

    if (result != nsIPermissionManager::ALLOW_ACTION) {
      return false;
    }
  }

  return true;
}

/* static */
nsresult AntiTrackingUtils::TestStoragePermissionInParent(
    nsIPrincipal* aTopPrincipal, nsIPrincipal* aPrincipal, uint32_t* aResult) {
  NS_ENSURE_ARG(aResult);
  *aResult = nsIPermissionManager::UNKNOWN_ACTION;
  NS_ENSURE_ARG(aTopPrincipal);
  NS_ENSURE_ARG(aPrincipal);

  nsCOMPtr<nsIPermissionManager> permMgr =
      components::PermissionManager::Service();
  NS_ENSURE_TRUE(permMgr, NS_ERROR_FAILURE);

  // Build the permission keys
  nsAutoCString requestPermissionKey;
  bool success = AntiTrackingUtils::CreateStoragePermissionKey(
      aPrincipal, requestPermissionKey);
  NS_ENSURE_TRUE(success, NS_ERROR_FAILURE);

  nsAutoCString requestFramePermissionKey;
  success = AntiTrackingUtils::CreateStorageFramePermissionKey(
      aPrincipal, requestFramePermissionKey);
  NS_ENSURE_TRUE(success, NS_ERROR_FAILURE);

  // Test the permission
  uint32_t access = nsIPermissionManager::UNKNOWN_ACTION;
  nsresult rv = permMgr->TestPermissionFromPrincipal(
      aTopPrincipal, requestPermissionKey, &access);
  NS_ENSURE_SUCCESS(rv, rv);

  if (access != nsIPermissionManager::UNKNOWN_ACTION) {
    *aResult = access;
    return NS_OK;
  }

  uint32_t frameAccess = nsIPermissionManager::UNKNOWN_ACTION;
  rv = permMgr->TestPermissionFromPrincipal(
      aTopPrincipal, requestFramePermissionKey, &frameAccess);
  NS_ENSURE_SUCCESS(rv, rv);

  *aResult = frameAccess;
  return NS_OK;
}

/* static */
nsILoadInfo::StoragePermissionState
AntiTrackingUtils::GetStoragePermissionStateInParent(nsIChannel* aChannel) {
  MOZ_ASSERT(aChannel);
  MOZ_DIAGNOSTIC_ASSERT(XRE_IsParentProcess());

  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  nsCOMPtr<nsICookieJarSettings> cookieJarSettings;

  auto policyType = loadInfo->GetExternalContentPolicyType();

  // The channel is for the document load of the top-level window. The top-level
  // window should always has 'hasStoragePermission' flag as false. So, we can
  // return here directly.
  if (policyType == ExtContentPolicy::TYPE_DOCUMENT) {
    return nsILoadInfo::NoStoragePermission;
  }

  nsresult rv =
      loadInfo->GetCookieJarSettings(getter_AddRefs(cookieJarSettings));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nsILoadInfo::NoStoragePermission;
  }

  int32_t cookieBehavior = cookieJarSettings->GetCookieBehavior();

  // We only need to check the storage permission if the cookie behavior is
  // BEHAVIOR_REJECT_TRACKER, BEHAVIOR_PARTITION_FOREIGN or
  // BEHAVIOR_REJECT_FOREIGN with exceptions. Because ContentBlocking wouldn't
  // update or check the storage permission if the cookie behavior is not
  // belongs to these three.
  if (!net::CookieJarSettings::IsRejectThirdPartyContexts(cookieBehavior)) {
    return nsILoadInfo::NoStoragePermission;
  }

  RefPtr<BrowsingContext> bc;
  rv = loadInfo->GetTargetBrowsingContext(getter_AddRefs(bc));
  if (NS_WARN_IF(NS_FAILED(rv)) || !bc) {
    return nsILoadInfo::NoStoragePermission;
  }

  uint64_t targetWindowId = GetTopLevelAntiTrackingWindowId(bc);
  nsCOMPtr<nsIPrincipal> targetPrincipal;

  if (targetWindowId) {
    RefPtr<WindowGlobalParent> wgp =
        WindowGlobalParent::GetByInnerWindowId(targetWindowId);

    if (NS_WARN_IF(!wgp)) {
      return nsILoadInfo::NoStoragePermission;
    }

    targetPrincipal = wgp->DocumentPrincipal();
  } else {
    // We try to use the loading principal if there is no AntiTrackingWindowId.
    targetPrincipal = loadInfo->GetLoadingPrincipal();
  }

  if (!targetPrincipal) {
    nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aChannel);

    if (httpChannel) {
      // We don't have a loading principal, let's see if this is a document
      // channel which belongs to a top-level window.
      bool isDocument = false;
      rv = httpChannel->GetIsMainDocumentChannel(&isDocument);
      if (NS_SUCCEEDED(rv) && isDocument) {
        nsIScriptSecurityManager* ssm =
            nsScriptSecurityManager::GetScriptSecurityManager();
        (void)ssm->GetChannelResultPrincipal(aChannel,
                                             getter_AddRefs(targetPrincipal));
      }
    }
  }

  // Let's use the triggering principal if we still have nothing on the hand.
  if (!targetPrincipal) {
    targetPrincipal = loadInfo->TriggeringPrincipal();
  }

  // Cannot get the target principal, bail out.
  if (NS_WARN_IF(!targetPrincipal)) {
    return nsILoadInfo::NoStoragePermission;
  }

  if (targetPrincipal->IsSystemPrincipal()) {
    return nsILoadInfo::HasStoragePermission;
  }

  nsCOMPtr<nsIURI> trackingURI;
  rv = aChannel->GetURI(getter_AddRefs(trackingURI));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nsILoadInfo::NoStoragePermission;
  }

  nsCOMPtr<nsIPrincipal> trackingPrincipal =
      BasePrincipal::CreateContentPrincipal(trackingURI,
                                            loadInfo->GetOriginAttributes());

  bool isThirdParty = IsThirdPartyChannel(aChannel);
  if (isThirdParty) {
    nsAutoCString targetOrigin;
    nsAutoCString trackingOrigin;
    if (NS_FAILED(targetPrincipal->GetOriginNoSuffix(targetOrigin)) ||
        NS_FAILED(trackingPrincipal->GetOriginNoSuffix(trackingOrigin))) {
      return nsILoadInfo::NoStoragePermission;
    }

    // Check whether the third-party channel is on any allow lists. We check
    // the partitioning exception list and the content blocking allow list.
    if (PartitioningExceptionList::Check(targetOrigin, trackingOrigin)) {
      return nsILoadInfo::StoragePermissionAllowListed;
    }

    nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aChannel);
    if (httpChannel && ContentBlockingAllowList::Check(httpChannel)) {
      return nsILoadInfo::StoragePermissionAllowListed;
    }
  }

  nsAutoCString type;
  AntiTrackingUtils::CreateStoragePermissionKey(trackingPrincipal, type);

  if (AntiTrackingUtils::CheckStoragePermission(
          targetPrincipal, type, NS_UsePrivateBrowsing(aChannel))) {
    return nsILoadInfo::HasStoragePermission;
  }

  WindowContext* wc = bc->GetCurrentWindowContext();
  if (!wc) {
    return nsILoadInfo::NoStoragePermission;
  }
  WindowGlobalParent* wgp = wc->Canonical();
  if (!wgp) {
    return nsILoadInfo::NoStoragePermission;
  }
  nsIPrincipal* framePrincipal = wgp->DocumentPrincipal();
  if (!framePrincipal) {
    return nsILoadInfo::NoStoragePermission;
  }

  RefPtr<net::HttpBaseChannel> httpBaseChannel = do_QueryObject(aChannel);
  if (httpBaseChannel && httpBaseChannel->HasRedirectTaintedOrigin()) {
    return nsILoadInfo::NoStoragePermission;
  }

  if (policyType == ExtContentPolicy::TYPE_SUBDOCUMENT) {
    // For loads of framed documents, we only use storage access
    // if the load is the result of a same-origin, same-site-initiated
    // navigation of the frame.
    uint64_t triggeringWindowId;
    rv = loadInfo->GetTriggeringWindowId(&triggeringWindowId);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return nsILoadInfo::NoStoragePermission;
    }

    nsIScriptSecurityManager* ssm = nsContentUtils::GetSecurityManager();
    RefPtr<nsIPrincipal> channelResultPrincipal;
    rv = ssm->GetChannelResultPrincipal(aChannel,
                                        getter_AddRefs(channelResultPrincipal));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return nsILoadInfo::NoStoragePermission;
    }
    RefPtr<net::HttpBaseChannel> httpChannel = do_QueryObject(aChannel);
    bool crossSiteInitiated = false;
    bool triggeringWindowHasStorageAccess = false;
    if (bc && bc->GetParent()->GetCurrentWindowContext()) {
      RefPtr<WindowGlobalParent> triggeringWGP =
          WindowGlobalParent::GetByInnerWindowId(triggeringWindowId);
      if (triggeringWGP && triggeringWGP->DocumentPrincipal()) {
        rv = triggeringWGP->DocumentPrincipal()->IsThirdPartyPrincipal(
            channelResultPrincipal, &crossSiteInitiated);
        if (NS_FAILED(rv)) {
          crossSiteInitiated = false;
        }
        triggeringWindowHasStorageAccess =
            triggeringWGP->GetUsingStorageAccess();
      }
    }

    if (!crossSiteInitiated && triggeringWindowHasStorageAccess &&
        trackingPrincipal->Equals(framePrincipal) && httpChannel &&
        !httpChannel->HasRedirectTaintedOrigin()) {
      return nsILoadInfo::HasStoragePermission;
    }
  } else if (!bc->IsTop()) {
    // For subframe resources, check if the document has storage access
    // and that the resource being loaded is same-site to the page.
    bool isThirdParty = true;
    nsresult rv = framePrincipal->IsThirdPartyURI(trackingURI, &isThirdParty);
    if (NS_SUCCEEDED(rv) && wc->GetUsingStorageAccess() && !isThirdParty) {
      return nsILoadInfo::HasStoragePermission;
    }
  }

  // The remaining part of the function is for determining whether storage
  // access could be granted using Storage-Access-Headers. And granting it
  // if instructed by the server via the "Activate-Storage-Access"-header.
  // Storage-Access headers are only sent in secure context
  if (!nsMixedContentBlocker::IsPotentiallyTrustworthyOrigin(trackingURI)) {
    return nsILoadInfo::NoStoragePermission;
  }

  // In case Storage-Access was granted to the origin prior with the
  // Storage-Access-API and the permission still exists, the website can
  // activate Storage-Access with Storage-Access-Headers.
  uint32_t result = 0;
  rv = AntiTrackingUtils::TestStoragePermissionInParent(
      targetPrincipal, trackingPrincipal, &result);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nsILoadInfo::NoStoragePermission;
  }
  if (result == nsIPermissionManager::ALLOW_ACTION) {
    if (RefPtr<net::nsHttpChannel> httpChannel = do_QueryObject(aChannel)) {
      if (httpChannel->StorageAccessReloadedChannel()) {
        return nsILoadInfo::HasStoragePermission;
      } else {
        return nsILoadInfo::InactiveStoragePermission;
      }
    }
  }

  // In the ABA-case, A can also get storage-access automatically via
  // Storage-Access-Headers.
  if (isThirdParty) {
    if (RefPtr<net::nsHttpChannel> httpChannel = do_QueryObject(aChannel)) {
      // Determine whether we are in ABA or AB case, erroring on AB side
      bool isAB = true;
      rv = targetPrincipal->IsThirdPartyURI(trackingURI, &isAB);
      if (NS_FAILED(rv)) {
        return nsILoadInfo::NoStoragePermission;
      }
      if (isAB) {
        // Third party resource, that could potentially have storage access
        // Sending "Sec-Fetch-Storage-Access: none"
        return nsILoadInfo::DisabledStoragePermission;
      } else {
        if (httpChannel->StorageAccessReloadedChannel()) {
          // The server sent a retry response after we sent the "inactive"
          // activate storage-access
          // Sending "Sec-Fetch-Storage-Access: active"
          return nsILoadInfo::HasStoragePermission;
        } else {
          // The site could has implicit storage-access due to ABA scenario.
          // Let the server know that replying with "retry" will lead to us
          // reloading the channel with storage-access enabled.
          // Sending "Sec-Fetch-Storage-Access: inactive"
          return nsILoadInfo::InactiveStoragePermission;
        }
      }
    }
  }

  return nsILoadInfo::NoStoragePermission;
}

// static
nsresult AntiTrackingUtils::ActivateStoragePermissionStateInParent(
    nsIChannel* aChannel) {
  NS_ENSURE_ARG_POINTER(aChannel);
  MOZ_DIAGNOSTIC_ASSERT(XRE_IsParentProcess());

  if (GetStoragePermissionStateInParent(aChannel) !=
      nsILoadInfo::InactiveStoragePermission) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  if (NS_WARN_IF(!loadInfo)) {
    return NS_ERROR_FAILURE;
  }

#ifdef DEBUG
  // We are only allowed to transition from "Inactive" to "Has". Parent function
  // should check this condition, but check here again to make extra sure.
  nsILoadInfo::StoragePermissionState currentStorageAccess =
      loadInfo->GetStoragePermission();
  MOZ_ASSERT(currentStorageAccess == nsILoadInfo::InactiveStoragePermission);
#endif

  // Allow accessing unpartitioned cookies
  MOZ_TRY(loadInfo->SetStoragePermission(nsILoadInfo::HasStoragePermission));
  return NS_OK;
}

bool AntiTrackingUtils::ProcessStorageAccessHeadersShouldRetry(
    nsIChannel* aChannel) {
  bool ShouldRetry = false;
  nsresult rv = ProcessStorageAccessHeaders(aChannel, &ShouldRetry);
  return NS_SUCCEEDED(rv) && ShouldRetry;
}

nsresult AntiTrackingUtils::ProcessStorageAccessHeaders(nsIChannel* aChannel,
                                                        bool* aOutRetry) {
  NS_ENSURE_ARG_POINTER(aChannel);
  NS_ENSURE_ARG_POINTER(aOutRetry);
  *aOutRetry = false;
  if (!StaticPrefs::dom_storage_access_enabled() ||
      !StaticPrefs::dom_storage_access_headers_enabled()) {
    return NS_OK;
  }

  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aChannel);
  NS_ENSURE_TRUE(httpChannel, NS_ERROR_FAILURE);

  // only continue processing if we got an Activate-Storage-Access response
  // header
  nsAutoCString activate;
  MOZ_TRY(
      httpChannel->GetResponseHeader("Activate-Storage-Access"_ns, activate));

  nsCOMPtr<nsILoadInfo> loadInfo;
  MOZ_TRY(aChannel->GetLoadInfo(getter_AddRefs(loadInfo)));

  // 1. If request's credentials mode is not "include", return failure.
  uint32_t cookiePolicy = 0;
  MOZ_TRY(loadInfo->GetCookiePolicy(&cookiePolicy));
  if (cookiePolicy != nsILoadInfo::SEC_COOKIES_INCLUDE) {
    return NS_ERROR_FAILURE;
  }

  // 2. If request's `eligible for storage-access` is "eligible", return
  // failure. When storage-access is already granted, no need to process further
  nsILoadInfo::StoragePermissionState storageAccess =
      AntiTrackingUtils::GetStoragePermissionStateInParent(aChannel);
  if (storageAccess != nsILoadInfo::InactiveStoragePermission) {
    return NS_ERROR_FAILURE;
  }

  // 3. Let storageAccessStatus be request's storage access status.
  nsAutoCString storageAccessStatus;

  // 4. If storageAccessStatus is not "inactive", return failure.
  MOZ_TRY(httpChannel->GetRequestHeader("Sec-Fetch-Storage-Access"_ns,
                                        storageAccessStatus));
  if (!storageAccessStatus.EqualsLiteral("inactive")) {
    return NS_ERROR_FAILURE;
  }
  net::ActivateStorageAccess asa =
      MOZ_TRY(net::ParseActivateStorageAccess(activate));

  switch (asa.variant) {
    case net::ActivateStorageAccessVariant::Load: {
      // TODO: only do on document channels / load?
      auto policyType = loadInfo->GetExternalContentPolicyType();
      if (policyType != ExtContentPolicy::TYPE_SUBDOCUMENT) {
        return NS_ERROR_FAILURE;
      }
      MOZ_TRY(
          AntiTrackingUtils::ActivateStoragePermissionStateInParent(aChannel));
      return NS_OK;
    }
    case net::ActivateStorageAccessVariant::RetryOrigin: {
      // check whether specified allowedOrigin allows this retry
      // parse origin
      nsCOMPtr<nsIURI> allowedOrigin;
      MOZ_TRY(NS_NewURI(getter_AddRefs(allowedOrigin), asa.origin.get()));

      nsIPrincipal* loadingPrincipal = loadInfo->GetLoadingPrincipal();
      if (!loadingPrincipal) {
        return NS_ERROR_FAILURE;
      }
      if (!loadingPrincipal->IsSameOrigin(allowedOrigin)) {
        return NS_ERROR_FAILURE;
      }
      *aOutRetry = true;
      return NS_OK;
    }
    case net::ActivateStorageAccessVariant::RetryAny:
      // no checks on origin necessary
      *aOutRetry = true;
      return NS_OK;
  }
  MOZ_ASSERT(false, "Invalid enum variant");
  return NS_ERROR_FAILURE;
}

uint64_t AntiTrackingUtils::GetTopLevelAntiTrackingWindowId(
    BrowsingContext* aBrowsingContext) {
  MOZ_ASSERT(aBrowsingContext);

  RefPtr<WindowContext> winContext =
      aBrowsingContext->GetCurrentWindowContext();
  if (!winContext || winContext->GetCookieBehavior().isNothing()) {
    return 0;
  }

  // Do not check BEHAVIOR_PARTITION_FOREIGN her because when
  // a third-party subresource is inside the main frame, we need to return the
  // top-level window id to partition its cookies correctly.
  uint32_t behavior = *winContext->GetCookieBehavior();
  if (behavior == nsICookieService::BEHAVIOR_REJECT_TRACKER &&
      aBrowsingContext->IsTop()) {
    return 0;
  }

  return aBrowsingContext->Top()->GetCurrentInnerWindowId();
}

uint64_t AntiTrackingUtils::GetTopLevelStorageAreaWindowId(
    BrowsingContext* aBrowsingContext) {
  MOZ_ASSERT(aBrowsingContext);

  if (Document::StorageAccessSandboxed(aBrowsingContext->GetSandboxFlags())) {
    return 0;
  }

  BrowsingContext* parentBC = aBrowsingContext->GetParent();
  if (!parentBC) {
    // No parent browsing context available!
    return 0;
  }

  if (!parentBC->IsTop()) {
    return 0;
  }

  return parentBC->GetCurrentInnerWindowId();
}

/* static */
already_AddRefed<nsIPrincipal> AntiTrackingUtils::GetPrincipal(
    BrowsingContext* aBrowsingContext) {
  MOZ_ASSERT(aBrowsingContext);

  nsCOMPtr<nsIPrincipal> principal;

  if (XRE_IsContentProcess()) {
    // Passing an out-of-process browsing context in child processes to
    // this API won't get any result, so just assert.
    MOZ_ASSERT(aBrowsingContext->IsInProcess());

    nsPIDOMWindowOuter* outer = aBrowsingContext->GetDOMWindow();
    if (NS_WARN_IF(!outer)) {
      return nullptr;
    }

    nsPIDOMWindowInner* inner = outer->GetCurrentInnerWindow();
    if (NS_WARN_IF(!inner)) {
      return nullptr;
    }

    principal = nsGlobalWindowInner::Cast(inner)->GetPrincipal();
  } else {
    WindowGlobalParent* wgp =
        aBrowsingContext->Canonical()->GetCurrentWindowGlobal();
    if (NS_WARN_IF(!wgp)) {
      return nullptr;
    }

    principal = wgp->DocumentPrincipal();
  }
  return principal.forget();
}

/* static */
bool AntiTrackingUtils::GetPrincipalAndTrackingOrigin(
    BrowsingContext* aBrowsingContext, nsIPrincipal** aPrincipal,
    nsACString& aTrackingOrigin) {
  MOZ_ASSERT(aBrowsingContext);

  // Passing an out-of-process browsing context in child processes to
  // this API won't get any result, so just assert.
  MOZ_ASSERT_IF(XRE_IsContentProcess(), aBrowsingContext->IsInProcess());

  // Let's take the principal and the origin of the tracker.
  nsCOMPtr<nsIPrincipal> principal =
      AntiTrackingUtils::GetPrincipal(aBrowsingContext);
  if (NS_WARN_IF(!principal)) {
    return false;
  }

  nsresult rv = principal->GetOriginNoSuffix(aTrackingOrigin);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }

  if (aPrincipal) {
    principal.forget(aPrincipal);
  }

  return true;
};

/* static */
uint32_t AntiTrackingUtils::GetCookieBehavior(
    BrowsingContext* aBrowsingContext) {
  MOZ_ASSERT(aBrowsingContext);

  RefPtr<dom::WindowContext> win = aBrowsingContext->GetCurrentWindowContext();
  if (!win || win->GetCookieBehavior().isNothing()) {
    return nsICookieService::BEHAVIOR_REJECT;
  }

  return *win->GetCookieBehavior();
}

/* static */
already_AddRefed<WindowGlobalParent>
AntiTrackingUtils::GetTopWindowExcludingExtensionAccessibleContentFrames(
    CanonicalBrowsingContext* aBrowsingContext, nsIURI* aURIBeingLoaded) {
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(aBrowsingContext);

  CanonicalBrowsingContext* bc = aBrowsingContext;
  RefPtr<WindowGlobalParent> prev;
  while (RefPtr<WindowGlobalParent> parent = bc->GetParentWindowContext()) {
    CanonicalBrowsingContext* parentBC = parent->BrowsingContext();

    nsIPrincipal* parentPrincipal = parent->DocumentPrincipal();
    nsIURI* uri = prev ? prev->GetDocumentURI() : aURIBeingLoaded;

    // If the new parent has permission to load the current page, we're
    // at a moz-extension:// frame which has a host permission that allows
    // it to load the document that we've loaded.  In that case, stop at
    // this frame and consider it the top-level frame.
    if (uri &&
        BasePrincipal::Cast(parentPrincipal)->AddonAllowsLoad(uri, true)) {
      break;
    }

    bc = parentBC;
    prev = parent;
  }
  if (!prev) {
    prev = bc->GetCurrentWindowGlobal();
  }
  return prev.forget();
}

/* static */
nsresult AntiTrackingUtils::IsThirdPartyToPartitionKeySite(
    nsIChannel* aChannel, const nsCOMPtr<nsIURI>& aURI, bool* aIsThirdParty) {
  MOZ_ASSERT(XRE_IsParentProcess());
  NS_ENSURE_ARG(aChannel);
  NS_ENSURE_ARG(aURI);
  NS_ENSURE_ARG(aIsThirdParty);
  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  nsCOMPtr<nsICookieJarSettings> cjs;
  nsresult rv = loadInfo->GetCookieJarSettings(getter_AddRefs(cjs));
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoString partitionKey;
  rv = cjs->GetPartitionKey(partitionKey);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoString scheme, host;
  int32_t _unused1;
  bool _unused2;
  if (!OriginAttributes::ParsePartitionKey(partitionKey, scheme, host, _unused1,
                                           _unused2)) {
    return NS_ERROR_FAILURE;
  }
  if (host.IsEmpty()) {
    return NS_ERROR_FAILURE;
  }
  nsAutoString partitionKeySite = scheme + u"://"_ns + host;
  nsCOMPtr<nsIURI> partitionKeySiteURI;
  rv = NS_NewURI(getter_AddRefs(partitionKeySiteURI), partitionKeySite);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<mozIThirdPartyUtil> thirdPartyUtil =
      components::ThirdPartyUtil::Service();
  return thirdPartyUtil->IsThirdPartyURI(aURI, partitionKeySiteURI,
                                         aIsThirdParty);
}

/* static */
void AntiTrackingUtils::ComputeIsThirdPartyToTopWindow(nsIChannel* aChannel) {
  MOZ_ASSERT(aChannel);
  MOZ_ASSERT(XRE_IsParentProcess());

  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();

  // When a top-level load is opened by window.open, BrowsingContext from
  // LoadInfo is its opener, which may make the third-party caculation code
  // below returns an incorrect result. So we use TYPE_DOCUMENT to
  // ensure a top-level load is not considered 3rd-party.
  auto policyType = loadInfo->GetExternalContentPolicyType();
  if (policyType == ExtContentPolicy::TYPE_DOCUMENT) {
    loadInfo->SetIsThirdPartyContextToTopWindow(false);
    return;
  }

  RefPtr<BrowsingContext> bc;
  loadInfo->GetBrowsingContext(getter_AddRefs(bc));
  if (!bc) {
    bc = loadInfo->GetAssociatedBrowsingContext();
  }

  nsCOMPtr<nsIURI> uri;
  (void)aChannel->GetURI(getter_AddRefs(uri));

  // In some cases we don't have a browsingContext. For example, in xpcshell
  // tests, channels that are used to download images and channels for loading
  // worker script.
  if (!bc) {
    // If the flag was set before, we don't need to compute again. This could
    // happen for the channels used to load worker scripts.
    //
    // Note that we cannot stop computing the flag in general even it has set
    // before because sometimes we need to get the up-to-date flag, e.g.
    // redirects.
    if (static_cast<net::LoadInfo*>(loadInfo.get())
            ->HasIsThirdPartyContextToTopWindowSet()) {
      return;
    }

    // We turn to check the top-level principal if there is no browsing context.
    auto* principal = BasePrincipal::Cast(loadInfo->GetTopLevelPrincipal());

    // If we don't have the top level principal, we try to compare directly to
    // the scheme in the partition key. Failing that, we fall back to
    // comparing to the loading principal. But we can only do this if this
    // channel has a URI. If we have neither a channel URI or the browsing
    // context, we can do nothing.
    if (uri) {
      if (!principal) {
        bool isThirdParty = true;
        nsresult rv =
            IsThirdPartyToPartitionKeySite(aChannel, uri, &isThirdParty);
        if (NS_SUCCEEDED(rv)) {
          loadInfo->SetIsThirdPartyContextToTopWindow(isThirdParty);
          return;
        }
      }
      principal = BasePrincipal::Cast(loadInfo->GetLoadingPrincipal());
    }
    if (principal) {
      bool isThirdParty = true;
      nsresult rv = principal->IsThirdPartyURI(uri, &isThirdParty);

      if (NS_SUCCEEDED(rv)) {
        loadInfo->SetIsThirdPartyContextToTopWindow(isThirdParty);
      }
    }
    return;
  }

  RefPtr<WindowGlobalParent> topWindow =
      GetTopWindowExcludingExtensionAccessibleContentFrames(bc->Canonical(),
                                                            uri);

  if (NS_WARN_IF(!topWindow)) {
    return;
  }

  nsCOMPtr<nsIPrincipal> topWindowPrincipal = topWindow->DocumentPrincipal();
  if (topWindowPrincipal && !topWindowPrincipal->GetIsNullPrincipal()) {
    auto* basePrin = BasePrincipal::Cast(topWindowPrincipal);
    bool isThirdParty = true;

    // For about:blank and about:srcdoc, we can't just compare uri to determine
    // whether the page is third-party, so we use channel result principal
    // instead. By doing this, an the resource inherits the principal from
    // its parent is considered not a third-party.
    if (NS_IsAboutBlank(uri) || NS_IsAboutSrcdoc(uri) ||
        uri->SchemeIs("blob")) {
      nsIScriptSecurityManager* ssm = nsContentUtils::GetSecurityManager();
      if (NS_WARN_IF(!ssm)) {
        return;
      }

      nsCOMPtr<nsIPrincipal> principal;
      nsresult rv =
          ssm->GetChannelResultPrincipal(aChannel, getter_AddRefs(principal));
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return;
      }

      basePrin->IsThirdPartyPrincipal(principal, &isThirdParty);
    } else {
      basePrin->IsThirdPartyURI(uri, &isThirdParty);
    }

    loadInfo->SetIsThirdPartyContextToTopWindow(isThirdParty);
  }
}

/* static */
bool AntiTrackingUtils::IsThirdPartyChannel(nsIChannel* aChannel) {
  MOZ_ASSERT(aChannel);

  nsCOMPtr<mozIThirdPartyUtil> tpuService =
      mozilla::components::ThirdPartyUtil::Service();
  if (!tpuService) {
    return true;
  }
  bool thirdParty = true;
  nsresult rv = tpuService->IsThirdPartyChannel(aChannel, nullptr, &thirdParty);
  if (NS_FAILED(rv)) {
    return true;
  }
  return thirdParty;
}

/* static */
bool AntiTrackingUtils::IsThirdPartyWindow(nsPIDOMWindowInner* aWindow,
                                           nsIURI* aURI) {
  MOZ_ASSERT(aWindow);

  // We assume that the window is foreign to the URI by default.
  bool thirdParty = true;

  // We will skip checking URIs for about:blank and about:srcdoc because they
  // have no domain. So, comparing them will always fail.
  if (aURI && !NS_IsAboutBlank(aURI) && !NS_IsAboutSrcdoc(aURI)) {
    nsCOMPtr<nsIScriptObjectPrincipal> scriptObjPrin =
        do_QueryInterface(aWindow);
    if (!scriptObjPrin) {
      return thirdParty;
    }

    nsCOMPtr<nsIPrincipal> prin = scriptObjPrin->GetPrincipal();
    if (!prin) {
      return thirdParty;
    }

    // Determine whether aURI is foreign with respect to the current principal.
    nsresult rv = prin->IsThirdPartyURI(aURI, &thirdParty);
    if (NS_FAILED(rv)) {
      return thirdParty;
    }

    if (thirdParty) {
      return thirdParty;
    }
  }

  RefPtr<Document> doc = aWindow->GetDoc();
  if (!doc) {
    // If we can't get the document from the window, ex, about:blank, fallback
    // to use IsThirdPartyWindow check that examine the whole hierarchy.
    nsCOMPtr<mozIThirdPartyUtil> thirdPartyUtil =
        components::ThirdPartyUtil::Service();
    (void)thirdPartyUtil->IsThirdPartyWindow(aWindow->GetOuterWindow(), nullptr,
                                             &thirdParty);
    return thirdParty;
  }

  return IsThirdPartyDocument(doc);
}

/* static */
bool AntiTrackingUtils::IsThirdPartyDocument(Document* aDocument) {
  MOZ_ASSERT(aDocument);

  if (aDocument->IsTopLevelContentDocument()) {
    return false;
  }

  nsCOMPtr<mozIThirdPartyUtil> tpuService =
      mozilla::components::ThirdPartyUtil::Service();
  if (!tpuService) {
    return true;
  }
  bool thirdParty = true;

  if (aDocument->GetSandboxFlags() & SANDBOXED_ORIGIN) {
    return true;
  }

  if (!aDocument->GetChannel()) {
    // If we can't get the channel from the document, i.e. initial about:blank
    // page, we first check if we should inherit from the parent document. If
    // the principal of the document is the same as the principal of the parent
    // document, we inherit the third-party status from the parent document.
    // Otherwise, we use the browsingContext of the document to check if it's
    // in the third-party context. If the browsing context is still not
    // available, we will treat the window as third-party.
    //
    // Note that we cannot directly use the browsingContext here. The
    // browsingContext of a mixed but same baseDomain context with top level
    // will be considered as third-party because they are not in the same
    // content process. However, it should be considered as first-party.
    RefPtr<Document> parentDoc = aDocument->GetInProcessParentDocument();
    if (parentDoc &&
        aDocument->NodePrincipal()->Equals(parentDoc->NodePrincipal())) {
      return IsThirdPartyDocument(parentDoc);
    }

    RefPtr<BrowsingContext> bc = aDocument->GetBrowsingContext();
    if (bc && bc->IsInProcess()) {
      return IsThirdPartyContext(bc);
    }
    return true;
  }

  nsresult rv = tpuService->IsThirdPartyChannel(aDocument->GetChannel(),
                                                nullptr, &thirdParty);
  if (NS_FAILED(rv)) {
    return true;
  }
  return thirdParty;
}

/* static */
bool AntiTrackingUtils::IsThirdPartyContext(BrowsingContext* aBrowsingContext) {
  MOZ_ASSERT(aBrowsingContext);
  MOZ_ASSERT(aBrowsingContext->IsInProcess());

  // iframes with SANDBOX_ORIGIN are always third-party contexts
  // because they are a unique origin
  nsIDocShell* docShell = aBrowsingContext->GetDocShell();
  if (!docShell) {
    return true;
  }
  Document* doc = docShell->GetExtantDocument();
  if (!doc || doc->GetSandboxFlags() & SANDBOXED_ORIGIN) {
    return true;
  }
  nsIPrincipal* principal = doc->NodePrincipal();

  BrowsingContext* traversingParent = aBrowsingContext->GetParent();
  while (traversingParent) {
    // If the parent browsing context is not in the same process, it's
    // cross-origin.
    if (!traversingParent->IsInProcess()) {
      return true;
    }

    nsIDocShell* parentDocShell = traversingParent->GetDocShell();
    if (!parentDocShell) {
      return true;
    }
    Document* parentDoc = parentDocShell->GetExtantDocument();
    if (!parentDoc || parentDoc->GetSandboxFlags() & SANDBOXED_ORIGIN) {
      return true;
    }
    nsIPrincipal* parentPrincipal = parentDoc->NodePrincipal();

    auto* parentBasePrin = BasePrincipal::Cast(parentPrincipal);
    bool isThirdParty = true;

    parentBasePrin->IsThirdPartyPrincipal(principal, &isThirdParty);
    if (isThirdParty) {
      return true;
    }

    traversingParent = traversingParent->GetParent();
  }
  return false;
}

/* static */
nsCString AntiTrackingUtils::GrantedReasonToString(
    ContentBlockingNotifier::StorageAccessPermissionGrantedReason aReason) {
  switch (aReason) {
    case ContentBlockingNotifier::eOpener:
      return "opener"_ns;
    case ContentBlockingNotifier::eOpenerAfterUserInteraction:
      return "user interaction"_ns;
    default:
      return "stroage access API"_ns;
  }
}

/* static */
void AntiTrackingUtils::UpdateAntiTrackingInfoForChannel(nsIChannel* aChannel) {
  MOZ_ASSERT(aChannel);

  if (!XRE_IsParentProcess()) {
    return;
  }

  MOZ_DIAGNOSTIC_ASSERT(XRE_IsParentProcess());

  AntiTrackingUtils::ComputeIsThirdPartyToTopWindow(aChannel);

  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();

  (void)loadInfo->SetStoragePermission(
      AntiTrackingUtils::GetStoragePermissionStateInParent(aChannel));

  // Note that we need to put this after computing the IsThirdPartyToTopWindow
  // flag because it will be used when getting the granular fingerprinting
  // protections.
  Maybe<RFPTargetSet> overriddenFingerprintingSettings =
      nsRFPService::GetOverriddenFingerprintingSettingsForChannel(aChannel);

  if (overriddenFingerprintingSettings) {
    loadInfo->SetOverriddenFingerprintingSettings(
        overriddenFingerprintingSettings.ref());
  }
#ifdef DEBUG
  static_cast<mozilla::net::LoadInfo*>(loadInfo.get())
      ->MarkOverriddenFingerprintingSettingsAsSet();
#endif

  nsCOMPtr<nsICookieJarSettings> cookieJarSettings;
  (void)loadInfo->GetCookieJarSettings(getter_AddRefs(cookieJarSettings));
  // Subresources (including subdocuments) may have a different partition key,
  // particularly one without or with the same-site bit. We have to update that
  // here.
  net::CookieJarSettings::Cast(cookieJarSettings)
      ->UpdatePartitionKeyForDocumentLoadedByChannel(aChannel);

  // We only update the IsOnContentBlockingAllowList flag and the partition key
  // for the top-level http channel.
  //
  // The IsOnContentBlockingAllowList is only for http. For other types of
  // channels, such as 'file:', there will be no interface to modify this. So,
  // we only update it in http channels.
  //
  // The partition key is computed based on the site, so it's no point to set it
  // for channels other than http channels.
  ExtContentPolicyType contentType = loadInfo->GetExternalContentPolicyType();
  nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aChannel);
  if (!httpChannel || contentType != ExtContentPolicy::TYPE_DOCUMENT) {
    return;
  }

  // Update the IsOnContentBlockingAllowList flag in the CookieJarSettings
  // if this is a top level loading. For sub-document loading, this flag
  // would inherit from the parent.
  net::CookieJarSettings::Cast(cookieJarSettings)
      ->UpdateIsOnContentBlockingAllowList(aChannel);

  // We only need to set FPD for top-level loads. FPD will automatically be
  // propagated to non-top level loads via CookieJarSetting.
  nsCOMPtr<nsIURI> uri;
  (void)aChannel->GetURI(getter_AddRefs(uri));
  net::CookieJarSettings::Cast(cookieJarSettings)->SetPartitionKey(uri);

  // Generate the fingerprinting randomization key for top-level loads. The key
  // will automatically be propagated to sub loads.
  auto RFPRandomKey = nsRFPService::GenerateKey(aChannel);
  if (RFPRandomKey) {
    net::CookieJarSettings::Cast(cookieJarSettings)
        ->SetFingerprintingRandomizationKey(RFPRandomKey.ref());
  }
}
