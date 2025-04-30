/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/AbstractThread.h"
#include "mozilla/AppShutdown.h"
#ifdef MOZ_BACKGROUNDTASKS
#  include "mozilla/BackgroundTasks.h"
#endif
#include "mozilla/BasePrincipal.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Components.h"
#include "mozilla/ContentPrincipal.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/ExpandedPrincipal.h"
#include "mozilla/net/NeckoMessageUtils.h"
#include "mozilla/Permission.h"
#include "mozilla/PermissionManager.h"
#include "mozilla/Preferences.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_permissions.h"
#include "mozilla/glean/ExtensionsPermissionsMetrics.h"

#include "mozIStorageService.h"
#include "mozIStorageConnection.h"
#include "mozIStorageStatement.h"
#include "mozStorageCID.h"

#include "nsAppDirectoryServiceDefs.h"
#include "nsComponentManagerUtils.h"
#include "nsContentUtils.h"
#include "nsCRT.h"
#include "nsDebug.h"
#include "nsIConsoleService.h"
#include "nsIEffectiveTLDService.h"
#include "nsIUserIdleService.h"
#include "nsIInputStream.h"
#include "nsINavHistoryService.h"
#include "nsIObserverService.h"
#include "nsIPrefBranch.h"
#include "nsIPrincipal.h"
#include "nsIURIMutator.h"
#include "nsIWritablePropertyBag2.h"
#include "nsReadLine.h"
#include "nsStringFwd.h"
#include "nsTHashSet.h"
#include "nsToolkitCompsCID.h"

using namespace mozilla::dom;

namespace mozilla {

#define PERMISSIONS_FILE_NAME "permissions.sqlite"
#define HOSTS_SCHEMA_VERSION 12

// Default permissions are read from a URL - this is the preference we read
// to find that URL. If not set, don't use any default permissions.
constexpr char kDefaultsUrlPrefName[] = "permissions.manager.defaultsUrl";

constexpr char kPermissionChangeNotification[] = PERM_CHANGE_NOTIFICATION;

// A special value for a permission ID that indicates the ID was loaded as
// a default value.  These will never be written to the database, but may
// be overridden with an explicit permission (including UNKNOWN_ACTION)
constexpr int64_t cIDPermissionIsDefault = -1;

#define ENSURE_NOT_CHILD_PROCESS_(onError)                 \
  PR_BEGIN_MACRO                                           \
  if (IsChildProcess()) {                                  \
    NS_ERROR("Cannot perform action in content process!"); \
    onError                                                \
  }                                                        \
  PR_END_MACRO

#define ENSURE_NOT_CHILD_PROCESS \
  ENSURE_NOT_CHILD_PROCESS_({ return NS_ERROR_NOT_AVAILABLE; })

#define ENSURE_NOT_CHILD_PROCESS_NORET ENSURE_NOT_CHILD_PROCESS_(;)

#define EXPIRY_NOW PR_Now() / 1000

////////////////////////////////////////////////////////////////////////////////

namespace {

inline bool IsChildProcess() { return XRE_IsContentProcess(); }

void LogToConsole(const nsAString& aMsg) {
  nsCOMPtr<nsIConsoleService> console(
      do_GetService("@mozilla.org/consoleservice;1"));
  if (!console) {
    NS_WARNING("Failed to log message to console.");
    return;
  }

  nsAutoString msg(aMsg);
  console->LogStringMessage(msg.get());
}

// NOTE: an empty string can be passed as aType - if it is this function will
// return "false" unconditionally.
bool HasDefaultPref(const nsACString& aType) {
  // A list of permissions that can have a fallback default permission
  // set under the permissions.default.* pref.
  static const nsLiteralCString kPermissionsWithDefaults[] = {
      "camera"_ns,    "microphone"_ns,      "geo"_ns, "desktop-notification"_ns,
      "shortcuts"_ns, "screen-wake-lock"_ns};

  if (!aType.IsEmpty()) {
    for (const auto& perm : kPermissionsWithDefaults) {
      if (perm.Equals(aType)) {
        return true;
      }
    }
  }

  return false;
}

// These permissions are special permissions which must be transmitted to the
// content process before documents with their principals have loaded within
// that process.
//
// Permissions which are in this list are considered to have a "" permission
// key, even if their principal would not normally have that key.
static const nsLiteralCString kPreloadPermissions[] = {
    // This permission is preloaded to support properly blocking service worker
    // interception when a user has disabled storage for a specific site.  Once
    // service worker interception moves to the parent process this should be
    // removed.  See bug 1428130.
    "cookie"_ns, "https-only-load-insecure"_ns};

// NOTE: nullptr can be passed as aType - if it is this function will return
// "false" unconditionally.
bool IsPreloadPermission(const nsACString& aType) {
  if (!aType.IsEmpty()) {
    for (const auto& perm : kPreloadPermissions) {
      if (perm.Equals(aType)) {
        return true;
      }
    }
  }

  return false;
}

// Array of permission types which should not be isolated by origin attributes,
// for user context and private browsing.
// Keep this array in sync with 'STRIPPED_PERMS' in
// 'test_permmanager_oa_strip.js'
// Currently only preloaded permissions are supported.
// This is because perms are sent to the content process in bulk by perm key.
// Non-preloaded, but OA stripped permissions would not be accessible by sites
// in private browsing / non-default user context.
static constexpr std::array<nsLiteralCString, 2> kStripOAPermissions = {
    {"cookie"_ns, "https-only-load-insecure"_ns}};

bool IsOAForceStripPermission(const nsACString& aType) {
  if (aType.IsEmpty()) {
    return false;
  }
  for (const auto& perm : kStripOAPermissions) {
    if (perm.Equals(aType)) {
      return true;
    }
  }
  return false;
}

// Array of permission prefixes which should be isolated only by site.
// These site-scoped permissions are stored under their site's principal.
// GetAllForPrincipal also needs to look for these especially.
static constexpr std::array<nsLiteralCString, 3> kSiteScopedPermissions = {
    {"3rdPartyStorage^"_ns, "AllowStorageAccessRequest^"_ns,
     "3rdPartyFrameStorage^"_ns}};

bool IsSiteScopedPermission(const nsACString& aType) {
  if (aType.IsEmpty()) {
    return false;
  }
  for (const auto& perm : kSiteScopedPermissions) {
    if (aType.Length() >= perm.Length() &&
        Substring(aType, 0, perm.Length()) == perm) {
      return true;
    }
  }
  return false;
}

// Array of permission type prefixes which have a secondary key encoded in the
// permission type. These permissions will not be stored in-process with the
// secondary key, but updates to them will cause "perm-changed" notifications on
// processes for that key.
static constexpr std::array<nsLiteralCString, 3> kSecondaryKeyedPermissions = {
    {"3rdPartyStorage^"_ns, "AllowStorageAccessRequest^"_ns,
     "3rdPartyFrameStorage^"_ns}};

bool GetSecondaryKey(const nsACString& aType, nsACString& aSecondaryKey) {
  aSecondaryKey.Truncate();
  if (aType.IsEmpty()) {
    return false;
  }
  for (const auto& perm : kSecondaryKeyedPermissions) {
    if (aType.Length() > perm.Length() &&
        Substring(aType, 0, perm.Length()) == perm) {
      aSecondaryKey = Substring(aType, perm.Length());
      return true;
    }
  }
  return false;
}

void OriginAppendOASuffix(OriginAttributes aOriginAttributes,
                          bool aForceStripOA, nsACString& aOrigin) {
  PermissionManager::MaybeStripOriginAttributes(aForceStripOA,
                                                aOriginAttributes);

  nsAutoCString oaSuffix;
  aOriginAttributes.CreateSuffix(oaSuffix);
  aOrigin.Append(oaSuffix);
}

nsresult GetOriginFromPrincipal(nsIPrincipal* aPrincipal, bool aForceStripOA,
                                nsACString& aOrigin) {
  nsresult rv = aPrincipal->GetOriginNoSuffix(aOrigin);
  // The principal may belong to the about:blank content viewer, so this can be
  // expected to fail.
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsAutoCString suffix;
  rv = aPrincipal->GetOriginSuffix(suffix);
  NS_ENSURE_SUCCESS(rv, rv);

  OriginAttributes attrs;
  NS_ENSURE_TRUE(attrs.PopulateFromSuffix(suffix), NS_ERROR_FAILURE);

  OriginAppendOASuffix(attrs, aForceStripOA, aOrigin);

  return NS_OK;
}

// Returns the site of the principal, including OA, given a principal.
nsresult GetSiteFromPrincipal(nsIPrincipal* aPrincipal, bool aForceStripOA,
                              nsACString& aSite) {
  nsCOMPtr<nsIURI> uri = aPrincipal->GetURI();
  nsCOMPtr<nsIEffectiveTLDService> etld =
      mozilla::components::EffectiveTLD::Service();
  NS_ENSURE_TRUE(etld, NS_ERROR_FAILURE);
  NS_ENSURE_TRUE(uri, NS_ERROR_FAILURE);
  nsresult rv = etld->GetSite(uri, aSite);

  // The principal may belong to the about:blank content viewer, so this can be
  // expected to fail.
  if (NS_FAILED(rv)) {
    rv = aPrincipal->GetOrigin(aSite);
    NS_ENSURE_SUCCESS(rv, rv);
    return NS_OK;
  }

  nsAutoCString suffix;
  rv = aPrincipal->GetOriginSuffix(suffix);
  NS_ENSURE_SUCCESS(rv, rv);

  OriginAttributes attrs;
  NS_ENSURE_TRUE(attrs.PopulateFromSuffix(suffix), NS_ERROR_FAILURE);

  OriginAppendOASuffix(attrs, aForceStripOA, aSite);

  return NS_OK;
}

nsresult GetOriginFromURIAndOA(nsIURI* aURI,
                               const OriginAttributes* aOriginAttributes,
                               bool aForceStripOA, nsACString& aOrigin) {
  nsAutoCString origin(aOrigin);
  nsresult rv = ContentPrincipal::GenerateOriginNoSuffixFromURI(aURI, origin);
  NS_ENSURE_SUCCESS(rv, rv);

  OriginAppendOASuffix(*aOriginAttributes, aForceStripOA, origin);

  aOrigin = origin;

  return NS_OK;
}

nsresult GetPrincipalFromOrigin(const nsACString& aOrigin, bool aForceStripOA,
                                nsIPrincipal** aPrincipal) {
  nsAutoCString originNoSuffix;
  OriginAttributes attrs;
  if (!attrs.PopulateFromOrigin(aOrigin, originNoSuffix)) {
    return NS_ERROR_FAILURE;
  }

  PermissionManager::MaybeStripOriginAttributes(aForceStripOA, attrs);

  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURI(getter_AddRefs(uri), originNoSuffix);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIPrincipal> principal =
      BasePrincipal::CreateContentPrincipal(uri, attrs);
  principal.forget(aPrincipal);
  return NS_OK;
}

nsresult GetPrincipal(nsIURI* aURI, nsIPrincipal** aPrincipal) {
  OriginAttributes attrs;
  nsCOMPtr<nsIPrincipal> principal =
      BasePrincipal::CreateContentPrincipal(aURI, attrs);
  NS_ENSURE_TRUE(principal, NS_ERROR_FAILURE);

  principal.forget(aPrincipal);
  return NS_OK;
}

nsCString GetNextSubDomainForHost(const nsACString& aHost) {
  nsCString subDomain;
  nsCOMPtr<nsIEffectiveTLDService> etld =
      mozilla::components::EffectiveTLD::Service();
  nsresult rv = etld->GetNextSubDomain(aHost, subDomain);
  // We can fail if there is no more subdomain or if the host can't have a
  // subdomain.
  if (NS_FAILED(rv)) {
    return ""_ns;
  }

  return subDomain;
}

// This function produces a nsIURI which is identical to the current
// nsIURI, except that it has one less subdomain segment. It returns
// `nullptr` if there are no more segments to remove.
already_AddRefed<nsIURI> GetNextSubDomainURI(nsIURI* aURI) {
  nsAutoCString host;
  nsresult rv = aURI->GetHost(host);
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  nsCString domain = GetNextSubDomainForHost(host);
  if (domain.IsEmpty()) {
    return nullptr;
  }

  nsCOMPtr<nsIURI> uri;
  rv = NS_MutateURI(aURI).SetHost(domain).Finalize(uri);
  if (NS_FAILED(rv) || !uri) {
    return nullptr;
  }

  return uri.forget();
}

nsresult UpgradeHostToOriginAndInsert(
    const nsACString& aHost, const nsCString& aType, uint32_t aPermission,
    uint32_t aExpireType, int64_t aExpireTime, int64_t aModificationTime,
    std::function<nsresult(const nsACString& aOrigin, const nsCString& aType,
                           uint32_t aPermission, uint32_t aExpireType,
                           int64_t aExpireTime, int64_t aModificationTime)>&&
        aCallback) {
  if (aHost.EqualsLiteral("<file>")) {
    // We no longer support the magic host <file>
    NS_WARNING(
        "The magic host <file> is no longer supported. "
        "It is being removed from the permissions database.");
    return NS_OK;
  }

  // First, we check to see if the host is a valid URI. If it is, it can be
  // imported directly
  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_NewURI(getter_AddRefs(uri), aHost);
  if (NS_SUCCEEDED(rv)) {
    // It was previously possible to insert useless entries to your permissions
    // database for URIs which have a null principal. This acts as a cleanup,
    // getting rid of these useless database entries
    if (uri->SchemeIs("moz-nullprincipal")) {
      NS_WARNING("A moz-nullprincipal: permission is being discarded.");
      return NS_OK;
    }

    nsCOMPtr<nsIPrincipal> principal;
    rv = GetPrincipal(uri, getter_AddRefs(principal));
    NS_ENSURE_SUCCESS(rv, rv);

    nsAutoCString origin;
    rv = GetOriginFromPrincipal(principal, IsOAForceStripPermission(aType),
                                origin);
    NS_ENSURE_SUCCESS(rv, rv);

    aCallback(origin, aType, aPermission, aExpireType, aExpireTime,
              aModificationTime);
    return NS_OK;
  }

  // The user may use this host at non-standard ports or protocols, we can use
  // their history to guess what ports and protocols we want to add permissions
  // for. We find every URI which they have visited with this host (or a
  // subdomain of this host), and try to add it as a principal.
  bool foundHistory = false;

  nsCOMPtr<nsINavHistoryService> histSrv = nullptr;
  if (NS_IsMainThread()) {
    histSrv = do_GetService(NS_NAVHISTORYSERVICE_CONTRACTID);
  }

  if (histSrv) {
    nsCOMPtr<nsINavHistoryQuery> histQuery;
    rv = histSrv->GetNewQuery(getter_AddRefs(histQuery));
    NS_ENSURE_SUCCESS(rv, rv);

    // Get the eTLD+1 of the domain
    nsAutoCString eTLD1;
    nsCOMPtr<nsIEffectiveTLDService> etld =
        mozilla::components::EffectiveTLD::Service();
    rv = etld->GetBaseDomainFromHost(aHost, 0, eTLD1);

    if (NS_FAILED(rv)) {
      // If the lookup on the tldService for the base domain for the host
      // failed, that means that we just want to directly use the host as the
      // host name for the lookup.
      eTLD1 = aHost;
    }

    // We want to only find history items for this particular eTLD+1, and
    // subdomains
    rv = histQuery->SetDomain(eTLD1);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = histQuery->SetDomainIsHost(false);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsINavHistoryQueryOptions> histQueryOpts;
    rv = histSrv->GetNewQueryOptions(getter_AddRefs(histQueryOpts));
    NS_ENSURE_SUCCESS(rv, rv);

    // We want to get the URIs for every item in the user's history with the
    // given host
    rv =
        histQueryOpts->SetResultType(nsINavHistoryQueryOptions::RESULTS_AS_URI);
    NS_ENSURE_SUCCESS(rv, rv);

    // We only search history, because searching both bookmarks and history
    // is not supported, and history tends to be more comprehensive.
    rv = histQueryOpts->SetQueryType(
        nsINavHistoryQueryOptions::QUERY_TYPE_HISTORY);
    NS_ENSURE_SUCCESS(rv, rv);

    // We include hidden URIs (such as those visited via iFrames) as they may
    // have permissions too
    rv = histQueryOpts->SetIncludeHidden(true);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsINavHistoryResult> histResult;
    rv = histSrv->ExecuteQuery(histQuery, histQueryOpts,
                               getter_AddRefs(histResult));
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<nsINavHistoryContainerResultNode> histResultContainer;
    rv = histResult->GetRoot(getter_AddRefs(histResultContainer));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = histResultContainer->SetContainerOpen(true);
    NS_ENSURE_SUCCESS(rv, rv);

    uint32_t childCount = 0;
    rv = histResultContainer->GetChildCount(&childCount);
    NS_ENSURE_SUCCESS(rv, rv);

    nsTHashSet<nsCString> insertedOrigins;
    for (uint32_t i = 0; i < childCount; i++) {
      nsCOMPtr<nsINavHistoryResultNode> child;
      histResultContainer->GetChild(i, getter_AddRefs(child));
      if (NS_WARN_IF(NS_FAILED(rv))) continue;

      uint32_t type;
      rv = child->GetType(&type);
      if (NS_WARN_IF(NS_FAILED(rv)) ||
          type != nsINavHistoryResultNode::RESULT_TYPE_URI) {
        NS_WARNING(
            "Unexpected non-RESULT_TYPE_URI node in "
            "UpgradeHostToOriginAndInsert()");
        continue;
      }

      nsAutoCString uriSpec;
      rv = child->GetUri(uriSpec);
      if (NS_WARN_IF(NS_FAILED(rv))) continue;

      nsCOMPtr<nsIURI> uri;
      rv = NS_NewURI(getter_AddRefs(uri), uriSpec);
      if (NS_WARN_IF(NS_FAILED(rv))) continue;

      // Use the provided host - this URI may be for a subdomain, rather than
      // the host we care about.
      rv = NS_MutateURI(uri).SetHost(aHost).Finalize(uri);
      if (NS_WARN_IF(NS_FAILED(rv))) continue;

      // We now have a URI which we can make a nsIPrincipal out of
      nsCOMPtr<nsIPrincipal> principal;
      rv = GetPrincipal(uri, getter_AddRefs(principal));
      if (NS_WARN_IF(NS_FAILED(rv))) continue;

      nsAutoCString origin;
      rv = GetOriginFromPrincipal(principal, IsOAForceStripPermission(aType),
                                  origin);
      if (NS_WARN_IF(NS_FAILED(rv))) continue;

      // Ensure that we don't insert the same origin repeatedly
      if (insertedOrigins.Contains(origin)) {
        continue;
      }

      foundHistory = true;
      rv = aCallback(origin, aType, aPermission, aExpireType, aExpireTime,
                     aModificationTime);
      NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "Insert failed");
      insertedOrigins.Insert(origin);
    }

    rv = histResultContainer->SetContainerOpen(false);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // If we didn't find any origins for this host in the poermissions database,
  // we can insert the default http:// and https:// permissions into the
  // database. This has a relatively high likelihood of applying the permission
  // to the correct origin.
  if (!foundHistory) {
    nsAutoCString hostSegment;
    nsCOMPtr<nsIPrincipal> principal;
    nsAutoCString origin;

    // If this is an ipv6 URI, we need to surround it in '[', ']' before trying
    // to parse it as a URI.
    if (aHost.FindChar(':') != -1) {
      hostSegment.AssignLiteral("[");
      hostSegment.Append(aHost);
      hostSegment.AppendLiteral("]");
    } else {
      hostSegment.Assign(aHost);
    }

    // http:// URI default
    rv = NS_NewURI(getter_AddRefs(uri), "http://"_ns + hostSegment);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = GetPrincipal(uri, getter_AddRefs(principal));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = GetOriginFromPrincipal(principal, IsOAForceStripPermission(aType),
                                origin);
    NS_ENSURE_SUCCESS(rv, rv);

    aCallback(origin, aType, aPermission, aExpireType, aExpireTime,
              aModificationTime);

    // https:// URI default
    rv = NS_NewURI(getter_AddRefs(uri), "https://"_ns + hostSegment);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = GetPrincipal(uri, getter_AddRefs(principal));
    NS_ENSURE_SUCCESS(rv, rv);

    rv = GetOriginFromPrincipal(principal, IsOAForceStripPermission(aType),
                                origin);
    NS_ENSURE_SUCCESS(rv, rv);

    aCallback(origin, aType, aPermission, aExpireType, aExpireTime,
              aModificationTime);
  }

  return NS_OK;
}

bool IsExpandedPrincipal(nsIPrincipal* aPrincipal) {
  nsCOMPtr<nsIExpandedPrincipal> ep = do_QueryInterface(aPrincipal);
  return !!ep;
}

// We only want to persist permissions which don't have session or policy
// expiration.
bool IsPersistentExpire(uint32_t aExpire, const nsACString& aType) {
  bool res = (aExpire != nsIPermissionManager::EXPIRE_SESSION &&
              aExpire != nsIPermissionManager::EXPIRE_POLICY);
  return res;
}

nsresult NotifySecondaryKeyPermissionUpdateInContentProcess(
    const nsACString& aType, uint32_t aPermission,
    const nsACString& aSecondaryKey, nsIPrincipal* aTopPrincipal) {
  NS_ENSURE_ARG_POINTER(aTopPrincipal);
  MOZ_ASSERT(XRE_IsParentProcess());
  AutoTArray<RefPtr<BrowsingContextGroup>, 5> bcGroups;
  BrowsingContextGroup::GetAllGroups(bcGroups);
  for (const auto& bcGroup : bcGroups) {
    for (const auto& topBC : bcGroup->Toplevels()) {
      CanonicalBrowsingContext* topCBC = topBC->Canonical();
      RefPtr<nsIURI> topURI = topCBC->GetCurrentURI();
      if (!topURI) {
        continue;
      }
      bool thirdParty;
      nsresult rv = aTopPrincipal->IsThirdPartyURI(topURI, &thirdParty);
      if (NS_FAILED(rv)) {
        continue;
      }
      if (!thirdParty) {
        AutoTArray<RefPtr<BrowsingContext>, 5> bcs;
        topBC->GetAllBrowsingContextsInSubtree(bcs);
        for (const auto& bc : bcs) {
          CanonicalBrowsingContext* cbc = bc->Canonical();
          ContentParent* cp = cbc->GetContentParent();
          if (!cp) {
            continue;
          }
          if (cp->NeedsSecondaryKeyPermissionsUpdate(aSecondaryKey)) {
            WindowGlobalParent* wgp = cbc->GetCurrentWindowGlobal();
            if (!wgp) {
              continue;
            }
            bool success = wgp->SendNotifyPermissionChange(aType, aPermission);
            Unused << NS_WARN_IF(!success);
          }
        }
      }
    }
  }
  return NS_OK;
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////

PermissionManager::PermissionKey*
PermissionManager::PermissionKey::CreateFromPrincipal(nsIPrincipal* aPrincipal,
                                                      bool aForceStripOA,
                                                      bool aScopeToSite,
                                                      nsresult& aResult) {
  nsAutoCString keyString;
  if (aScopeToSite) {
    aResult = GetSiteFromPrincipal(aPrincipal, aForceStripOA, keyString);
  } else {
    aResult = GetOriginFromPrincipal(aPrincipal, aForceStripOA, keyString);
  }
  if (NS_WARN_IF(NS_FAILED(aResult))) {
    return nullptr;
  }
  return new PermissionKey(keyString);
}

PermissionManager::PermissionKey*
PermissionManager::PermissionKey::CreateFromURIAndOriginAttributes(
    nsIURI* aURI, const OriginAttributes* aOriginAttributes, bool aForceStripOA,
    nsresult& aResult) {
  nsAutoCString origin;
  aResult =
      GetOriginFromURIAndOA(aURI, aOriginAttributes, aForceStripOA, origin);
  if (NS_WARN_IF(NS_FAILED(aResult))) {
    return nullptr;
  }

  return new PermissionKey(origin);
}

PermissionManager::PermissionKey*
PermissionManager::PermissionKey::CreateFromURI(nsIURI* aURI,
                                                nsresult& aResult) {
  nsAutoCString origin;
  aResult = ContentPrincipal::GenerateOriginNoSuffixFromURI(aURI, origin);
  if (NS_WARN_IF(NS_FAILED(aResult))) {
    return nullptr;
  }

  return new PermissionKey(origin);
}

////////////////////////////////////////////////////////////////////////////////
// PermissionManager Implementation

NS_IMPL_ISUPPORTS(PermissionManager, nsIPermissionManager, nsIObserver,
                  nsISupportsWeakReference, nsIAsyncShutdownBlocker)

PermissionManager::PermissionManager()
    : mMonitor("PermissionManager::mMonitor"),
      mState(eInitializing),
      mMemoryOnlyDB(false),
      mLargestID(0) {}

PermissionManager::~PermissionManager() {
  MonitorAutoLock lock{mMonitor};

  // NOTE: Make sure to reject each of the promises in mPermissionKeyPromiseMap
  // before destroying.
  for (const auto& promise : mPermissionKeyPromiseMap.Values()) {
    if (promise) {
      promise->Reject(NS_ERROR_FAILURE, __func__);
    }
  }
  mPermissionKeyPromiseMap.Clear();

  if (mThread) {
    mThread->Shutdown();
    mThread = nullptr;
  }
}

/* static */
StaticMutex PermissionManager::sCreationMutex;
StaticRefPtr<PermissionManager> PermissionManager::sInstanceHolder;
bool PermissionManager::sInstanceDead(false);

// static
already_AddRefed<nsIPermissionManager> PermissionManager::GetXPCOMSingleton() {
  return GetInstance();
}

// static
already_AddRefed<PermissionManager> PermissionManager::GetInstance() {
  // The lazy initialization could race.
  StaticMutexAutoLock lock(sCreationMutex);

  if (sInstanceDead) {
    return nullptr;
  }

  if (sInstanceHolder) {
    RefPtr<PermissionManager> ret(sInstanceHolder);
    return ret.forget();
  }

  auto permManager = MakeRefPtr<PermissionManager>();
  if (NS_SUCCEEDED(permManager->Init())) {
    // Note that this does an extra AddRef on purpose to keep us alive
    // until shutdown.
    sInstanceHolder = permManager.get();
    return permManager.forget();
  }

  sInstanceDead = true;
  return nullptr;
}

nsresult PermissionManager::Init() {
  // If we are already shutting down, do not permit a creation.
  // This must match the phase in GetAsyncShutdownBarrier.
  if (AppShutdown::IsInOrBeyond(ShutdownPhase::XPCOMWillShutdown)) {
    return NS_ERROR_ILLEGAL_DURING_SHUTDOWN;
  }

  MOZ_ASSERT(NS_IsMainThread());

  MonitorAutoLock lock{mMonitor};
  MOZ_ASSERT(mState == eInitializing);

  // If the 'permissions.memory_only' pref is set to true, then don't write any
  // permission settings to disk, but keep them in a memory-only database.
  mMemoryOnlyDB = Preferences::GetBool("permissions.memory_only", false);

  nsresult rv;
  nsCOMPtr<nsIPrefService> prefService =
      do_GetService(NS_PREFSERVICE_CONTRACTID, &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = prefService->GetBranch("permissions.default.",
                              getter_AddRefs(mDefaultPrefBranch));
  NS_ENSURE_SUCCESS(rv, rv);

  if (IsChildProcess()) {
    // Stop here; we don't need the DB in the child process. Instead we will be
    // sent permissions as we need them by our parent process.
    mState = eReady;

    // We use ClearOnShutdown on the content process only because on the parent
    // process we need to block the shutdown for the final closeDB() call.
    ClearOnShutdown(&sInstanceHolder);
    return NS_OK;
  }

  nsCOMPtr<nsIObserverService> observerService = services::GetObserverService();
  if (observerService) {
    observerService->AddObserver(this, "profile-do-change", true);
    observerService->AddObserver(this, "testonly-reload-permissions-from-disk",
                                 true);
    observerService->AddObserver(this, "last-pb-context-exited", true);
  }

  if (XRE_IsParentProcess()) {
    nsCOMPtr<nsIAsyncShutdownClient> asc = GetAsyncShutdownBarrier();
    if (!asc) {
      return NS_ERROR_NOT_AVAILABLE;
    }
    nsAutoString blockerName;
    MOZ_ALWAYS_SUCCEEDS(GetName(blockerName));

    nsresult rv = asc->AddBlocker(
        this, NS_LITERAL_STRING_FROM_CSTRING(__FILE__), __LINE__, blockerName);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  AddIdleDailyMaintenanceJob();

  MOZ_ASSERT(!mThread);
  NS_ENSURE_SUCCESS(NS_NewNamedThread("Permission", getter_AddRefs(mThread)),
                    NS_ERROR_FAILURE);

  PRThread* prThread;
  MOZ_ALWAYS_SUCCEEDS(mThread->GetPRThread(&prThread));
  MOZ_ASSERT(prThread);

  mThreadBoundData.Transfer(prThread);

  InitDB(false);

  return NS_OK;
}

nsresult PermissionManager::OpenDatabase(nsIFile* aPermissionsFile) {
  MOZ_ASSERT(!NS_IsMainThread());
  auto data = mThreadBoundData.Access();

  nsresult rv;
  nsCOMPtr<mozIStorageService> storage =
      do_GetService(MOZ_STORAGE_SERVICE_CONTRACTID);
  if (!storage) {
    return NS_ERROR_UNEXPECTED;
  }
  // cache a connection to the hosts database
  if (mMemoryOnlyDB) {
    rv = storage->OpenSpecialDatabase(
        kMozStorageMemoryStorageKey, VoidCString(),
        mozIStorageService::CONNECTION_DEFAULT, getter_AddRefs(data->mDBConn));
  } else {
    rv = storage->OpenDatabase(aPermissionsFile,
                               mozIStorageService::CONNECTION_DEFAULT,
                               getter_AddRefs(data->mDBConn));
  }
  return rv;
}

void PermissionManager::InitDB(bool aRemoveFile) {
  mState = eInitializing;
  MOZ_ASSERT(NS_IsMainThread());

  mReadEntries.Clear();

  auto readyIfFailed = MakeScopeExit([&]() {
    // ignore failure here, since it's non-fatal (we can run fine without
    // persistent storage - e.g. if there's no profile).
    // XXX should we tell the user about this?
    mState = eReady;
  });

  if (!mPermissionsFile) {
    nsresult rv = NS_GetSpecialDirectory(NS_APP_PERMISSION_PARENT_DIR,
                                         getter_AddRefs(mPermissionsFile));
    if (NS_FAILED(rv)) {
      rv = NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                                  getter_AddRefs(mPermissionsFile));
      if (NS_FAILED(rv)) {
        return;
      }
    }

    rv =
        mPermissionsFile->AppendNative(nsLiteralCString(PERMISSIONS_FILE_NAME));
    NS_ENSURE_SUCCESS_VOID(rv);
  }

  nsCOMPtr<nsIInputStream> defaultsInputStream = GetDefaultsInputStream();

  RefPtr<PermissionManager> self = this;
  mThread->Dispatch(NS_NewRunnableFunction(
      "PermissionManager::InitDB", [self, aRemoveFile, defaultsInputStream] {
        MonitorAutoLock lock(self->mMonitor);

        nsresult rv = self->TryInitDB(aRemoveFile, defaultsInputStream, lock);
        Unused << NS_WARN_IF(NS_FAILED(rv));

        // This extra runnable calls EnsureReadCompleted to finialize the
        // initialization. If there is something blocked by the monitor, it will
        // be NOP.
        NS_DispatchToMainThread(NS_NewRunnableFunction(
            "PermissionManager::InitDB-MainThread", [self] {
              MonitorAutoLock lock{self->mMonitor};
              self->EnsureReadCompleted();
            }));

        self->mMonitor.Notify();
      }));

  readyIfFailed.release();
}

nsresult PermissionManager::TryInitDB(bool aRemoveFile,
                                      nsIInputStream* aDefaultsInputStream,
                                      const MonitorAutoLock& aProofOfLock) {
  MOZ_ASSERT(!NS_IsMainThread());

  auto raii = MakeScopeExit([&]() {
    if (aDefaultsInputStream) {
      aDefaultsInputStream->Close();
    }

    mState = eDBInitialized;
  });

  auto data = mThreadBoundData.Access();

  auto raiiFailure = MakeScopeExit([&]() {
    if (data->mDBConn) {
      DebugOnly<nsresult> rv = data->mDBConn->Close();
      MOZ_ASSERT(NS_SUCCEEDED(rv));
      data->mDBConn = nullptr;
    }
  });

  nsresult rv;

  if (aRemoveFile) {
    bool exists = false;
    rv = mPermissionsFile->Exists(&exists);
    NS_ENSURE_SUCCESS(rv, rv);
    if (exists) {
      rv = mPermissionsFile->Remove(false);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  rv = OpenDatabase(mPermissionsFile);
  if (rv == NS_ERROR_FILE_CORRUPTED) {
    LogToConsole(u"permissions.sqlite is corrupted! Try again!"_ns);

    // Add telemetry probe
    glean::permissions::sql_corrupted.Add(1);

    // delete corrupted permissions.sqlite and try again
    rv = mPermissionsFile->Remove(false);
    NS_ENSURE_SUCCESS(rv, rv);
    LogToConsole(u"Corrupted permissions.sqlite has been removed."_ns);

    rv = OpenDatabase(mPermissionsFile);
    NS_ENSURE_SUCCESS(rv, rv);
    LogToConsole(u"OpenDatabase to permissions.sqlite is successful!"_ns);
  }

  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  bool ready;
  data->mDBConn->GetConnectionReady(&ready);
  if (!ready) {
    LogToConsole(nsLiteralString(
        u"Fail to get connection to permissions.sqlite! Try again!"));

    // delete and try again
    rv = mPermissionsFile->Remove(false);
    NS_ENSURE_SUCCESS(rv, rv);
    LogToConsole(u"Defective permissions.sqlite has been removed."_ns);

    // Add telemetry probe
    glean::permissions::defective_sql_removed.Add(1);

    rv = OpenDatabase(mPermissionsFile);
    NS_ENSURE_SUCCESS(rv, rv);
    LogToConsole(u"OpenDatabase to permissions.sqlite is successful!"_ns);

    data->mDBConn->GetConnectionReady(&ready);
    if (!ready) return NS_ERROR_UNEXPECTED;
  }

  bool tableExists = false;
  data->mDBConn->TableExists("moz_perms"_ns, &tableExists);
  if (!tableExists) {
    data->mDBConn->TableExists("moz_hosts"_ns, &tableExists);
  }
  if (!tableExists) {
    rv = CreateTable();
    NS_ENSURE_SUCCESS(rv, rv);
  } else {
    // table already exists; check the schema version before reading
    int32_t dbSchemaVersion;
    rv = data->mDBConn->GetSchemaVersion(&dbSchemaVersion);
    NS_ENSURE_SUCCESS(rv, rv);

    switch (dbSchemaVersion) {
        // upgrading.
        // every time you increment the database schema, you need to
        // implement the upgrading code from the previous version to the
        // new one. fall through to current version

      case 1: {
        // previous non-expiry version of database.  Upgrade it by adding
        // the expiration columns
        rv = data->mDBConn->ExecuteSimpleSQL(
            "ALTER TABLE moz_hosts ADD expireType INTEGER"_ns);
        NS_ENSURE_SUCCESS(rv, rv);

        rv = data->mDBConn->ExecuteSimpleSQL(
            "ALTER TABLE moz_hosts ADD expireTime INTEGER"_ns);
        NS_ENSURE_SUCCESS(rv, rv);
      }

        // fall through to the next upgrade
        [[fallthrough]];

      // TODO: we want to make default version as version 2 in order to
      // fix bug 784875.
      case 0:
      case 2: {
        // Add appId/isInBrowserElement fields.
        rv = data->mDBConn->ExecuteSimpleSQL(
            "ALTER TABLE moz_hosts ADD appId INTEGER"_ns);
        NS_ENSURE_SUCCESS(rv, rv);

        rv = data->mDBConn->ExecuteSimpleSQL(nsLiteralCString(
            "ALTER TABLE moz_hosts ADD isInBrowserElement INTEGER"));
        NS_ENSURE_SUCCESS(rv, rv);

        rv = data->mDBConn->SetSchemaVersion(3);
        NS_ENSURE_SUCCESS(rv, rv);
      }

        // fall through to the next upgrade
        [[fallthrough]];

      // Version 3->4 is the creation of the modificationTime field.
      case 3: {
        rv = data->mDBConn->ExecuteSimpleSQL(nsLiteralCString(
            "ALTER TABLE moz_hosts ADD modificationTime INTEGER"));
        NS_ENSURE_SUCCESS(rv, rv);

        // We leave the modificationTime at zero for all existing records;
        // using now() would mean, eg, that doing "remove all from the
        // last hour" within the first hour after migration would remove
        // all permissions.

        rv = data->mDBConn->SetSchemaVersion(4);
        NS_ENSURE_SUCCESS(rv, rv);
      }

        // fall through to the next upgrade
        [[fallthrough]];

      // In version 5, host appId, and isInBrowserElement were merged into
      // a single origin entry
      //
      // In version 6, the tables were renamed for backwards compatability
      // reasons with version 4 and earlier.
      //
      // In version 7, a bug in the migration used for version 4->5 was
      // discovered which could have triggered data-loss. Because of that,
      // all users with a version 4, 5, or 6 database will be re-migrated
      // from the backup database. (bug 1186034). This migration bug is
      // not present after bug 1185340, and the re-migration ensures that
      // all users have the fix.
      case 5:
        // This branch could also be reached via dbSchemaVersion == 3, in
        // which case we want to fall through to the dbSchemaVersion == 4
        // case. The easiest way to do that is to perform this extra check
        // here to make sure that we didn't get here via a fallthrough
        // from v3
        if (dbSchemaVersion == 5) {
          // In version 5, the backup database is named moz_hosts_v4. We
          // perform the version 5->6 migration to get the tables to have
          // consistent naming conventions.

          // Version 5->6 is the renaming of moz_hosts to moz_perms, and
          // moz_hosts_v4 to moz_hosts (bug 1185343)
          //
          // In version 5, we performed the modifications to the
          // permissions database in place, this meant that if you
          // upgraded to a version which used V5, and then downgraded to a
          // version which used v4 or earlier, the fallback path would
          // drop the table, and your permissions data would be lost. This
          // migration undoes that mistake, by restoring the old moz_hosts
          // table (if it was present), and instead using the new table
          // moz_perms for the new permissions schema.
          //
          // NOTE: If you downgrade, store new permissions, and then
          // upgrade again, these new permissions won't be migrated or
          // reflected in the updated database. This migration only occurs
          // once, as if moz_perms exists, it will skip creating it. In
          // addition, permissions added after the migration will not be
          // visible in previous versions of firefox.

          bool permsTableExists = false;
          data->mDBConn->TableExists("moz_perms"_ns, &permsTableExists);
          if (!permsTableExists) {
            // Move the upgraded database to moz_perms
            rv = data->mDBConn->ExecuteSimpleSQL(
                "ALTER TABLE moz_hosts RENAME TO moz_perms"_ns);
            NS_ENSURE_SUCCESS(rv, rv);
          } else {
            NS_WARNING(
                "moz_hosts was not renamed to moz_perms, "
                "as a moz_perms table already exists");

            // In the situation where a moz_perms table already exists,
            // but the schema is lower than 6, a migration has already
            // previously occured to V6, but a downgrade has caused the
            // moz_hosts table to be dropped. This should only occur in
            // the case of a downgrade to a V5 database, which was only
            // present in a few day's nightlies. As that version was
            // likely used only on a temporary basis, we assume that the
            // database from the previous V6 has the permissions which the
            // user actually wants to use. We have to get rid of moz_hosts
            // such that moz_hosts_v4 can be moved into its place if it
            // exists.
            rv = data->mDBConn->ExecuteSimpleSQL("DROP TABLE moz_hosts"_ns);
            NS_ENSURE_SUCCESS(rv, rv);
          }

#ifdef DEBUG
          // The moz_hosts table shouldn't exist anymore
          bool hostsTableExists = false;
          data->mDBConn->TableExists("moz_hosts"_ns, &hostsTableExists);
          MOZ_ASSERT(!hostsTableExists);
#endif

          // Rename moz_hosts_v4 back to it's original location, if it
          // exists
          bool v4TableExists = false;
          data->mDBConn->TableExists("moz_hosts_v4"_ns, &v4TableExists);
          if (v4TableExists) {
            rv = data->mDBConn->ExecuteSimpleSQL(nsLiteralCString(
                "ALTER TABLE moz_hosts_v4 RENAME TO moz_hosts"));
            NS_ENSURE_SUCCESS(rv, rv);
          }

          rv = data->mDBConn->SetSchemaVersion(6);
          NS_ENSURE_SUCCESS(rv, rv);
        }

        // fall through to the next upgrade
        [[fallthrough]];

      // At this point, the version 5 table has been migrated to a version
      // 6 table We are guaranteed to have at least one of moz_hosts and
      // moz_perms. If we have moz_hosts, we will migrate moz_hosts into
      // moz_perms (even if we already have a moz_perms, as we need a
      // re-migration due to bug 1186034).
      //
      // After this migration, we are guaranteed to have both a moz_hosts
      // (for backwards compatability), and a moz_perms table. The
      // moz_hosts table will have a v4 schema, and the moz_perms table
      // will have a v6 schema.
      case 4:
      case 6: {
        bool hostsTableExists = false;
        data->mDBConn->TableExists("moz_hosts"_ns, &hostsTableExists);
        if (hostsTableExists) {
          // Both versions 4 and 6 have a version 4 formatted hosts table
          // named moz_hosts. We can migrate this table to our version 7
          // table moz_perms. If moz_perms is present, then we can use it
          // as a basis for comparison.

          rv = data->mDBConn->BeginTransaction();
          NS_ENSURE_SUCCESS(rv, rv);

          bool tableExists = false;
          data->mDBConn->TableExists("moz_hosts_new"_ns, &tableExists);
          if (tableExists) {
            NS_WARNING(
                "The temporary database moz_hosts_new already exists, "
                "dropping "
                "it.");
            rv = data->mDBConn->ExecuteSimpleSQL("DROP TABLE moz_hosts_new"_ns);
            NS_ENSURE_SUCCESS(rv, rv);
          }
          rv = data->mDBConn->ExecuteSimpleSQL(
              nsLiteralCString("CREATE TABLE moz_hosts_new ("
                               " id INTEGER PRIMARY KEY"
                               ",origin TEXT"
                               ",type TEXT"
                               ",permission INTEGER"
                               ",expireType INTEGER"
                               ",expireTime INTEGER"
                               ",modificationTime INTEGER"
                               ")"));
          NS_ENSURE_SUCCESS(rv, rv);

          nsCOMPtr<mozIStorageStatement> stmt;
          rv = data->mDBConn->CreateStatement(
              nsLiteralCString(
                  "SELECT host, type, permission, expireType, "
                  "expireTime, "
                  "modificationTime, isInBrowserElement FROM moz_hosts"),
              getter_AddRefs(stmt));
          NS_ENSURE_SUCCESS(rv, rv);

          int64_t id = 0;
          bool hasResult;

          while (NS_SUCCEEDED(stmt->ExecuteStep(&hasResult)) && hasResult) {
            MigrationEntry entry;

            // Read in the old row
            rv = stmt->GetUTF8String(0, entry.mHost);
            if (NS_WARN_IF(NS_FAILED(rv))) {
              continue;
            }
            rv = stmt->GetUTF8String(1, entry.mType);
            if (NS_WARN_IF(NS_FAILED(rv))) {
              continue;
            }

            entry.mId = id++;
            entry.mPermission = stmt->AsInt32(2);
            entry.mExpireType = stmt->AsInt32(3);
            entry.mExpireTime = stmt->AsInt64(4);
            entry.mModificationTime = stmt->AsInt64(5);

            mMigrationEntries.AppendElement(entry);
          }

          // We don't drop the moz_hosts table such that it is available
          // for backwards-compatability and for future migrations in case
          // of migration errors in the current code. Create a marker
          // empty table which will indicate that the moz_hosts table is
          // intended to act as a backup. If this table is not present,
          // then the moz_hosts table was created as a random empty table.
          rv = data->mDBConn->ExecuteSimpleSQL(
              nsLiteralCString("CREATE TABLE moz_hosts_is_backup (dummy "
                               "INTEGER PRIMARY KEY)"));
          NS_ENSURE_SUCCESS(rv, rv);

          bool permsTableExists = false;
          data->mDBConn->TableExists("moz_perms"_ns, &permsTableExists);
          if (permsTableExists) {
            // The user already had a moz_perms table, and we are
            // performing a re-migration. We count the rows in the old
            // table for telemetry, and then back up their old database as
            // moz_perms_v6

            nsCOMPtr<mozIStorageStatement> countStmt;
            rv = data->mDBConn->CreateStatement(
                "SELECT COUNT(*) FROM moz_perms"_ns, getter_AddRefs(countStmt));
            bool hasResult = false;
            if (NS_FAILED(rv) ||
                NS_FAILED(countStmt->ExecuteStep(&hasResult)) || !hasResult) {
              NS_WARNING("Could not count the rows in moz_perms");
            }

            // Back up the old moz_perms database as moz_perms_v6 before
            // we move the new table into its position
            rv = data->mDBConn->ExecuteSimpleSQL(nsLiteralCString(
                "ALTER TABLE moz_perms RENAME TO moz_perms_v6"));
            NS_ENSURE_SUCCESS(rv, rv);
          }

          rv = data->mDBConn->ExecuteSimpleSQL(nsLiteralCString(
              "ALTER TABLE moz_hosts_new RENAME TO moz_perms"));
          NS_ENSURE_SUCCESS(rv, rv);

          rv = data->mDBConn->CommitTransaction();
          NS_ENSURE_SUCCESS(rv, rv);
        } else {
          // We don't have a moz_hosts table, so we create one for
          // downgrading purposes. This table is empty.
          rv = data->mDBConn->ExecuteSimpleSQL(
              nsLiteralCString("CREATE TABLE moz_hosts ("
                               " id INTEGER PRIMARY KEY"
                               ",host TEXT"
                               ",type TEXT"
                               ",permission INTEGER"
                               ",expireType INTEGER"
                               ",expireTime INTEGER"
                               ",modificationTime INTEGER"
                               ",appId INTEGER"
                               ",isInBrowserElement INTEGER"
                               ")"));
          NS_ENSURE_SUCCESS(rv, rv);

          // We are guaranteed to have a moz_perms table at this point.
        }

#ifdef DEBUG
        {
          // At this point, both the moz_hosts and moz_perms tables should
          // exist
          bool hostsTableExists = false;
          bool permsTableExists = false;
          data->mDBConn->TableExists("moz_hosts"_ns, &hostsTableExists);
          data->mDBConn->TableExists("moz_perms"_ns, &permsTableExists);
          MOZ_ASSERT(hostsTableExists && permsTableExists);
        }
#endif

        rv = data->mDBConn->SetSchemaVersion(7);
        NS_ENSURE_SUCCESS(rv, rv);
      }

        // fall through to the next upgrade
        [[fallthrough]];

      // The version 7-8 migration is the re-migration of localhost and
      // ip-address entries due to errors in the previous version 7
      // migration which caused localhost and ip-address entries to be
      // incorrectly discarded. The version 7 migration logic has been
      // corrected, and thus this logic only needs to execute if the user
      // is currently on version 7.
      case 7: {
        // This migration will be relatively expensive as we need to
        // perform database lookups for each origin which we want to
        // insert. Fortunately, it shouldn't be too expensive as we only
        // want to insert a small number of entries created for localhost
        // or IP addresses.

        // We only want to perform the re-migration if moz_hosts is a
        // backup
        bool hostsIsBackupExists = false;
        data->mDBConn->TableExists("moz_hosts_is_backup"_ns,
                                   &hostsIsBackupExists);

        // Only perform this migration if the original schema version was
        // 7, and the moz_hosts table is a backup.
        if (dbSchemaVersion == 7 && hostsIsBackupExists) {
          nsCOMPtr<mozIStorageStatement> stmt;
          rv = data->mDBConn->CreateStatement(
              nsLiteralCString(
                  "SELECT host, type, permission, expireType, "
                  "expireTime, "
                  "modificationTime, isInBrowserElement FROM moz_hosts"),
              getter_AddRefs(stmt));
          NS_ENSURE_SUCCESS(rv, rv);

          nsCOMPtr<mozIStorageStatement> idStmt;
          rv = data->mDBConn->CreateStatement(
              "SELECT MAX(id) FROM moz_hosts"_ns, getter_AddRefs(idStmt));

          int64_t id = 0;
          bool hasResult = false;
          if (NS_SUCCEEDED(rv) &&
              NS_SUCCEEDED(idStmt->ExecuteStep(&hasResult)) && hasResult) {
            id = idStmt->AsInt32(0) + 1;
          }

          nsCOMPtr<nsIEffectiveTLDService> etld =
              mozilla::components::EffectiveTLD::Service();
          while (NS_SUCCEEDED(stmt->ExecuteStep(&hasResult)) && hasResult) {
            MigrationEntry entry;

            // Read in the old row
            rv = stmt->GetUTF8String(0, entry.mHost);
            if (NS_WARN_IF(NS_FAILED(rv))) {
              continue;
            }

            nsAutoCString eTLD1;
            rv = etld->GetBaseDomainFromHost(entry.mHost, 0, eTLD1);
            if (NS_SUCCEEDED(rv)) {
              // We only care about entries which the tldService can't
              // handle
              continue;
            }

            rv = stmt->GetUTF8String(1, entry.mType);
            if (NS_WARN_IF(NS_FAILED(rv))) {
              continue;
            }

            entry.mId = id++;
            entry.mPermission = stmt->AsInt32(2);
            entry.mExpireType = stmt->AsInt32(3);
            entry.mExpireTime = stmt->AsInt64(4);
            entry.mModificationTime = stmt->AsInt64(5);

            mMigrationEntries.AppendElement(entry);
          }
        }

        // Even if we didn't perform the migration, we want to bump the
        // schema version to 8.
        rv = data->mDBConn->SetSchemaVersion(8);
        NS_ENSURE_SUCCESS(rv, rv);
      }

        // fall through to the next upgrade
        [[fallthrough]];

      // The version 8-9 migration removes the unnecessary backup
      // moz-hosts database contents. as the data no longer needs to be
      // migrated
      case 8: {
        // We only want to clear out the old table if it is a backup. If
        // it isn't a backup, we don't need to touch it.
        bool hostsIsBackupExists = false;
        data->mDBConn->TableExists("moz_hosts_is_backup"_ns,
                                   &hostsIsBackupExists);
        if (hostsIsBackupExists) {
          // Delete everything from the backup, we want to keep around the
          // table so that you can still downgrade and not break things,
          // but we don't need to keep the rows around.
          rv = data->mDBConn->ExecuteSimpleSQL("DELETE FROM moz_hosts"_ns);
          NS_ENSURE_SUCCESS(rv, rv);

          // The table is no longer a backup, so get rid of it.
          rv = data->mDBConn->ExecuteSimpleSQL(
              "DROP TABLE moz_hosts_is_backup"_ns);
          NS_ENSURE_SUCCESS(rv, rv);
        }

        rv = data->mDBConn->SetSchemaVersion(9);
        NS_ENSURE_SUCCESS(rv, rv);
      }

        // fall through to the next upgrade
        [[fallthrough]];

      case 9: {
        rv = data->mDBConn->SetSchemaVersion(10);
        NS_ENSURE_SUCCESS(rv, rv);
      }

        // fall through to the next upgrade
        [[fallthrough]];

      case 10: {
        // Filter out the rows with storage access API permissions with a
        // granted origin, and remove the granted origin part from the
        // permission type.
        rv = data->mDBConn->ExecuteSimpleSQL(nsLiteralCString(
            "UPDATE moz_perms "
            "SET type=SUBSTR(type, 0, INSTR(SUBSTR(type, INSTR(type, "
            "'^') + "
            "1), '^') + INSTR(type, '^')) "
            "WHERE INSTR(SUBSTR(type, INSTR(type, '^') + 1), '^') AND "
            "SUBSTR(type, 0, 18) == \"storageAccessAPI^\";"));
        NS_ENSURE_SUCCESS(rv, rv);

        rv = data->mDBConn->SetSchemaVersion(11);
        NS_ENSURE_SUCCESS(rv, rv);
      }

        // fall through to the next upgrade
        [[fallthrough]];

      case 11: {
        // Migrate 3rdPartyStorage keys to a site scope
        rv = data->mDBConn->BeginTransaction();
        NS_ENSURE_SUCCESS(rv, rv);
        nsCOMPtr<mozIStorageStatement> updateStmt;
        rv = data->mDBConn->CreateStatement(
            nsLiteralCString("UPDATE moz_perms SET origin = ?2 WHERE id = ?1"),
            getter_AddRefs(updateStmt));
        NS_ENSURE_SUCCESS(rv, rv);

        nsCOMPtr<mozIStorageStatement> deleteStmt;
        rv = data->mDBConn->CreateStatement(
            nsLiteralCString("DELETE FROM moz_perms WHERE id = ?1"),
            getter_AddRefs(deleteStmt));
        NS_ENSURE_SUCCESS(rv, rv);

        nsCOMPtr<mozIStorageStatement> selectStmt;
        rv = data->mDBConn->CreateStatement(
            nsLiteralCString("SELECT id, origin, type FROM moz_perms WHERE "
                             " SUBSTR(type, 0, 17) == \"3rdPartyStorage^\""),
            getter_AddRefs(selectStmt));
        NS_ENSURE_SUCCESS(rv, rv);

        nsTHashSet<nsCStringHashKey> deduplicationSet;
        bool hasResult;
        nsCOMPtr<nsIEffectiveTLDService> etld =
            mozilla::components::EffectiveTLD::Service();
        while (NS_SUCCEEDED(selectStmt->ExecuteStep(&hasResult)) && hasResult) {
          int64_t id;
          rv = selectStmt->GetInt64(0, &id);
          NS_ENSURE_SUCCESS(rv, rv);

          nsCString origin;
          rv = selectStmt->GetUTF8String(1, origin);
          NS_ENSURE_SUCCESS(rv, rv);

          nsCString type;
          rv = selectStmt->GetUTF8String(2, type);
          NS_ENSURE_SUCCESS(rv, rv);

          nsCOMPtr<nsIURI> uri;
          rv = NS_NewURI(getter_AddRefs(uri), origin);
          if (NS_FAILED(rv)) {
            continue;
          }
          nsCString site;
          rv = etld->GetSite(uri, site);
          if (NS_WARN_IF(NS_FAILED(rv))) {
            continue;
          }

          nsCString deduplicationKey =
              nsPrintfCString("%s,%s", site.get(), type.get());
          if (deduplicationSet.Contains(deduplicationKey)) {
            rv = deleteStmt->BindInt64ByIndex(0, id);
            NS_ENSURE_SUCCESS(rv, rv);

            rv = deleteStmt->Execute();
            NS_ENSURE_SUCCESS(rv, rv);
          } else {
            deduplicationSet.Insert(deduplicationKey);
            rv = updateStmt->BindInt64ByIndex(0, id);
            NS_ENSURE_SUCCESS(rv, rv);
            rv = updateStmt->BindUTF8StringByIndex(1, site);
            NS_ENSURE_SUCCESS(rv, rv);

            rv = updateStmt->Execute();
            NS_ENSURE_SUCCESS(rv, rv);
          }
        }
        rv = data->mDBConn->CommitTransaction();
        NS_ENSURE_SUCCESS(rv, rv);

        rv = data->mDBConn->SetSchemaVersion(HOSTS_SCHEMA_VERSION);
        NS_ENSURE_SUCCESS(rv, rv);
      }

        // fall through to the next upgrade
        [[fallthrough]];

      // current version.
      case HOSTS_SCHEMA_VERSION:
        break;

      // downgrading.
      // if columns have been added to the table, we can still use the
      // ones we understand safely. if columns have been deleted or
      // altered, just blow away the table and start from scratch! if you
      // change the way a column is interpreted, make sure you also change
      // its name so this check will catch it.
      default: {
        // check if all the expected columns exist
        nsCOMPtr<mozIStorageStatement> stmt;
        rv = data->mDBConn->CreateStatement(
            nsLiteralCString("SELECT origin, type, permission, "
                             "expireType, expireTime, "
                             "modificationTime FROM moz_perms"),
            getter_AddRefs(stmt));
        if (NS_SUCCEEDED(rv)) break;

        // our columns aren't there - drop the table!
        rv = data->mDBConn->ExecuteSimpleSQL("DROP TABLE moz_perms"_ns);
        NS_ENSURE_SUCCESS(rv, rv);

        rv = CreateTable();
        NS_ENSURE_SUCCESS(rv, rv);
      } break;
    }
  }

  // cache frequently used statements (for insertion, deletion, and
  // updating)
  rv = data->mDBConn->CreateStatement(
      nsLiteralCString("INSERT INTO moz_perms "
                       "(id, origin, type, permission, expireType, "
                       "expireTime, modificationTime) "
                       "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)"),
      getter_AddRefs(data->mStmtInsert));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = data->mDBConn->CreateStatement(nsLiteralCString("DELETE FROM moz_perms "
                                                       "WHERE id = ?1"),
                                      getter_AddRefs(data->mStmtDelete));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = data->mDBConn->CreateStatement(
      nsLiteralCString("UPDATE moz_perms "
                       "SET permission = ?2, expireType= ?3, expireTime = "
                       "?4, modificationTime = ?5 WHERE id = ?1"),
      getter_AddRefs(data->mStmtUpdate));
  NS_ENSURE_SUCCESS(rv, rv);

  // Always import default permissions.
  ConsumeDefaultsInputStream(aDefaultsInputStream, aProofOfLock);

  // check whether to import or just read in the db
  if (tableExists) {
    rv = Read(aProofOfLock);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  raiiFailure.release();

  return NS_OK;
}

void PermissionManager::AddIdleDailyMaintenanceJob() {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIObserverService> observerService = services::GetObserverService();
  NS_ENSURE_TRUE_VOID(observerService);

  nsresult rv =
      observerService->AddObserver(this, OBSERVER_TOPIC_IDLE_DAILY, false);
  NS_ENSURE_SUCCESS_VOID(rv);
}

void PermissionManager::RemoveIdleDailyMaintenanceJob() {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIObserverService> observerService = services::GetObserverService();
  NS_ENSURE_TRUE_VOID(observerService);

  nsresult rv =
      observerService->RemoveObserver(this, OBSERVER_TOPIC_IDLE_DAILY);
  NS_ENSURE_SUCCESS_VOID(rv);
}

void PermissionManager::PerformIdleDailyMaintenance() {
  MOZ_ASSERT(NS_IsMainThread());

  RefPtr<PermissionManager> self = this;
  mThread->Dispatch(NS_NewRunnableFunction(
      "PermissionManager::PerformIdleDailyMaintenance", [self] {
        auto data = self->mThreadBoundData.Access();

        if (self->mState == eClosed || !data->mDBConn) {
          return;
        }

        nsCOMPtr<mozIStorageStatement> stmtDeleteExpired;
        nsresult rv = data->mDBConn->CreateStatement(
            nsLiteralCString("DELETE FROM moz_perms WHERE expireType = "
                             "?1 AND expireTime <= ?2"),
            getter_AddRefs(stmtDeleteExpired));
        NS_ENSURE_SUCCESS_VOID(rv);

        rv = stmtDeleteExpired->BindInt32ByIndex(
            0, nsIPermissionManager::EXPIRE_TIME);
        NS_ENSURE_SUCCESS_VOID(rv);

        rv = stmtDeleteExpired->BindInt64ByIndex(1, EXPIRY_NOW);
        NS_ENSURE_SUCCESS_VOID(rv);

        rv = stmtDeleteExpired->Execute();
        NS_ENSURE_SUCCESS_VOID(rv);
      }));
}

// sets the schema version and creates the moz_perms table.
nsresult PermissionManager::CreateTable() {
  MOZ_ASSERT(!NS_IsMainThread());
  auto data = mThreadBoundData.Access();

  // set the schema version, before creating the table
  nsresult rv = data->mDBConn->SetSchemaVersion(HOSTS_SCHEMA_VERSION);
  if (NS_FAILED(rv)) return rv;

  // create the table
  // SQL also lives in automation.py.in. If you change this SQL change that
  // one too
  rv = data->mDBConn->ExecuteSimpleSQL(
      nsLiteralCString("CREATE TABLE moz_perms ("
                       " id INTEGER PRIMARY KEY"
                       ",origin TEXT"
                       ",type TEXT"
                       ",permission INTEGER"
                       ",expireType INTEGER"
                       ",expireTime INTEGER"
                       ",modificationTime INTEGER"
                       ")"));
  if (NS_FAILED(rv)) return rv;

  // We also create a legacy V4 table, for backwards compatability,
  // and to ensure that downgrades don't trigger a schema version change.
  return data->mDBConn->ExecuteSimpleSQL(
      nsLiteralCString("CREATE TABLE moz_hosts ("
                       " id INTEGER PRIMARY KEY"
                       ",host TEXT"
                       ",type TEXT"
                       ",permission INTEGER"
                       ",expireType INTEGER"
                       ",expireTime INTEGER"
                       ",modificationTime INTEGER"
                       ",isInBrowserElement INTEGER"
                       ")"));
}

// Returns whether the given combination of expire type and expire time are
// expired. Note that EXPIRE_SESSION only honors expireTime if it is nonzero.
bool PermissionManager::HasExpired(uint32_t aExpireType, int64_t aExpireTime) {
  return (aExpireType == nsIPermissionManager::EXPIRE_TIME ||
          (aExpireType == nsIPermissionManager::EXPIRE_SESSION &&
           aExpireTime != 0)) &&
         aExpireTime <= EXPIRY_NOW;
}

NS_IMETHODIMP
PermissionManager::AddFromPrincipalAndPersistInPrivateBrowsing(
    nsIPrincipal* aPrincipal, const nsACString& aType, uint32_t aPermission) {
  ENSURE_NOT_CHILD_PROCESS;

  bool isValidPermissionPrincipal = false;
  nsresult rv = ShouldHandlePrincipalForPermission(aPrincipal,
                                                   isValidPermissionPrincipal);

  NS_ENSURE_SUCCESS(rv, rv);
  if (!isValidPermissionPrincipal) {
    // return early if the principal is invalid for permissions
    return rv;
  }

  // A modificationTime of zero will cause AddInternal to use now().
  int64_t modificationTime = 0;

  MonitorAutoLock lock{mMonitor};

  return AddInternal(aPrincipal, aType, aPermission, 0,
                     nsIPermissionManager::EXPIRE_NEVER,
                     /* aExpireTime */ 0, modificationTime, eNotify, eWriteToDB,
                     /* aOriginString*/ nullptr,
                     /* aAllowPersistInPrivateBrowsing */ true);
}

NS_IMETHODIMP
PermissionManager::AddDefaultFromPrincipal(nsIPrincipal* aPrincipal,
                                           const nsACString& aType,
                                           uint32_t aPermission) {
  ENSURE_NOT_CHILD_PROCESS;

  bool isValidPermissionPrincipal = false;
  nsresult rv = ShouldHandlePrincipalForPermission(aPrincipal,
                                                   isValidPermissionPrincipal);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!isValidPermissionPrincipal) {
    // return early if the principal is invalid for permissions
    return rv;
  }

  nsCString origin;
  rv = GetOriginFromPrincipal(aPrincipal, IsOAForceStripPermission(aType),
                              origin);
  NS_ENSURE_SUCCESS(rv, rv);

  MonitorAutoLock lock(mMonitor);

  DefaultEntry entry;
  {
    // Try to update existing entry in mDefaultEntriesForImport, which will
    // later be used to restore the default permissions when permissions are
    // cleared
    bool updatedExistingEntry = false;
    nsTArray<DefaultEntry>::iterator defaultEntry =
        mDefaultEntriesForImport.begin();
    while (defaultEntry != mDefaultEntriesForImport.end()) {
      if (defaultEntry->mType == aType && defaultEntry->mOrigin == origin) {
        defaultEntry->mPermission = aPermission;
        entry = *defaultEntry;
        if (aPermission == nsIPermissionManager::UNKNOWN_ACTION) {
          mDefaultEntriesForImport.RemoveElementAt(defaultEntry);
        }
        updatedExistingEntry = true;
        break;
      }
      ++defaultEntry;
    }

    // Or add a new entry if there wasn't already one and we aren't deleting the
    // default permission
    if (!updatedExistingEntry) {
      entry.mOrigin = origin;
      entry.mPermission = aPermission;
      entry.mType = aType;
      if (aPermission != nsIPermissionManager::UNKNOWN_ACTION) {
        mDefaultEntriesForImport.AppendElement(entry);
      }
    }
  }

  // So far, we have only updated mDefaultEntriesForImport for later recovery.
  // Now, we actually need to import this change into the permission manager.
  return ImportDefaultEntry(entry);
}

NS_IMETHODIMP
PermissionManager::AddFromPrincipal(nsIPrincipal* aPrincipal,
                                    const nsACString& aType,
                                    uint32_t aPermission, uint32_t aExpireType,
                                    int64_t aExpireTime) {
  ENSURE_NOT_CHILD_PROCESS;
  NS_ENSURE_TRUE(aExpireType == nsIPermissionManager::EXPIRE_NEVER ||
                     aExpireType == nsIPermissionManager::EXPIRE_TIME ||
                     aExpireType == nsIPermissionManager::EXPIRE_SESSION ||
                     aExpireType == nsIPermissionManager::EXPIRE_POLICY,
                 NS_ERROR_INVALID_ARG);

  // Skip addition if the permission is already expired.
  if (HasExpired(aExpireType, aExpireTime)) {
    return NS_OK;
  }

  bool isValidPermissionPrincipal = false;
  nsresult rv = ShouldHandlePrincipalForPermission(aPrincipal,
                                                   isValidPermissionPrincipal);

  NS_ENSURE_SUCCESS(rv, rv);
  if (!isValidPermissionPrincipal) {
    // return early if the principal is invalid for permissions
    return rv;
  }

  // A modificationTime of zero will cause AddInternal to use now().
  int64_t modificationTime = 0;
  MonitorAutoLock lock{mMonitor};
  return AddInternal(aPrincipal, aType, aPermission, 0, aExpireType,
                     aExpireTime, modificationTime, eNotify, eWriteToDB);
}

NS_IMETHODIMP
PermissionManager::TestAddFromPrincipalByTime(nsIPrincipal* aPrincipal,
                                              const nsACString& aType,
                                              uint32_t aPermission,
                                              int64_t aModificationTime) {
  ENSURE_NOT_CHILD_PROCESS;

  bool isValidPermissionPrincipal = false;
  nsresult rv = ShouldHandlePrincipalForPermission(aPrincipal,
                                                   isValidPermissionPrincipal);

  NS_ENSURE_SUCCESS(rv, rv);
  if (!isValidPermissionPrincipal) {
    // return early if the principal is invalid for permissions
    return rv;
  }

  MonitorAutoLock lock{mMonitor};
  return AddInternal(aPrincipal, aType, aPermission, 0,
                     nsIPermissionManager::EXPIRE_NEVER, 0, aModificationTime,
                     eNotify, eWriteToDB);
}

nsresult PermissionManager::Add(nsIPrincipal* aPrincipal,
                                const nsACString& aType, uint32_t aPermission,
                                int64_t aID, uint32_t aExpireType,
                                int64_t aExpireTime, int64_t aModificationTime,
                                NotifyOperationType aNotifyOperation,
                                DBOperationType aDBOperation,
                                const nsACString* aOriginString,
                                const bool aAllowPersistInPrivateBrowsing) {
  MOZ_ASSERT(IsChildProcess());

  MonitorAutoLock lock{mMonitor};
  return AddInternal(aPrincipal, aType, aPermission, aID, aExpireType,
                     aExpireTime, aModificationTime, aNotifyOperation,
                     aDBOperation, aOriginString,
                     aAllowPersistInPrivateBrowsing);
}

nsresult PermissionManager::AddInternal(
    nsIPrincipal* aPrincipal, const nsACString& aType, uint32_t aPermission,
    int64_t aID, uint32_t aExpireType, int64_t aExpireTime,
    int64_t aModificationTime, NotifyOperationType aNotifyOperation,
    DBOperationType aDBOperation, const nsACString* aOriginString,
    const bool aAllowPersistInPrivateBrowsing) {
  MOZ_ASSERT_IF(!IsChildProcess(), NS_IsMainThread());

  // If this is a default permission, no changes should not be written to disk.
  MOZ_ASSERT((aID != cIDPermissionIsDefault) || (aDBOperation != eWriteToDB));

  EnsureReadCompleted();

  nsresult rv = NS_OK;
  nsAutoCString origin;
  // Only attempt to compute the origin string when it is going to be needed
  // later on in the function.
  if (!IsChildProcess() ||
      (aDBOperation == eWriteToDB && IsPersistentExpire(aExpireType, aType))) {
    if (aOriginString) {
      // Use the origin string provided by the caller.
      origin = *aOriginString;
    } else {
      if (IsSiteScopedPermission(aType)) {
        rv = GetSiteFromPrincipal(aPrincipal, IsOAForceStripPermission(aType),
                                  origin);
      } else {
        // Compute it from the principal provided.
        rv = GetOriginFromPrincipal(aPrincipal, IsOAForceStripPermission(aType),
                                    origin);
      }
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  // Unless the caller sets aAllowPersistInPrivateBrowsing, only store
  // permissions for the session in Private Browsing. Except for default
  // permissions which are stored in-memory only and imported each startup. We
  // also allow setting persistent UKNOWN_ACTION, to support removing default
  // private browsing permissions.
  if (!aAllowPersistInPrivateBrowsing && aID != cIDPermissionIsDefault &&
      aPermission != UNKNOWN_ACTION && aExpireType != EXPIRE_SESSION) {
    uint32_t privateBrowsingId =
        nsScriptSecurityManager::DEFAULT_PRIVATE_BROWSING_ID;
    nsresult rv = aPrincipal->GetPrivateBrowsingId(&privateBrowsingId);
    if (NS_SUCCEEDED(rv) &&
        privateBrowsingId !=
            nsScriptSecurityManager::DEFAULT_PRIVATE_BROWSING_ID) {
      aExpireType = EXPIRE_SESSION;
    }
  }

  // Let's send the new permission to the content process only if it has to be
  // notified.
  if (!IsChildProcess() && aNotifyOperation == eNotify) {
    IPC::Permission permission(origin, aType, aPermission, aExpireType,
                               aExpireTime);

    nsAutoCString permissionKey;
    GetKeyForPermission(aPrincipal, aType, permissionKey);
    bool isSecondaryKeyed;
    nsAutoCString secondaryKey;
    isSecondaryKeyed = GetSecondaryKey(aType, secondaryKey);
    if (isSecondaryKeyed) {
      NotifySecondaryKeyPermissionUpdateInContentProcess(
          aType, aPermission, secondaryKey, aPrincipal);
    }

    nsTArray<ContentParent*> cplist;
    ContentParent::GetAll(cplist);
    for (uint32_t i = 0; i < cplist.Length(); ++i) {
      ContentParent* cp = cplist[i];
      if (cp->NeedsPermissionsUpdate(permissionKey)) {
        Unused << cp->SendAddPermission(permission);
      }
    }
  }

  MOZ_ASSERT(PermissionAvailableInternal(aPrincipal, aType));

  // look up the type index
  int32_t typeIndex = GetTypeIndex(aType, true);
  NS_ENSURE_TRUE(typeIndex != -1, NS_ERROR_OUT_OF_MEMORY);

  // When an entry already exists, PutEntry will return that, instead
  // of adding a new one
  RefPtr<PermissionKey> key = PermissionKey::CreateFromPrincipal(
      aPrincipal, IsOAForceStripPermission(aType),
      IsSiteScopedPermission(aType), rv);
  if (!key) {
    MOZ_ASSERT(NS_FAILED(rv));
    return rv;
  }

  PermissionHashKey* entry = mPermissionTable.PutEntry(key);
  if (!entry) return NS_ERROR_FAILURE;
  if (!entry->GetKey()) {
    mPermissionTable.RemoveEntry(entry);
    return NS_ERROR_OUT_OF_MEMORY;
  }

  // figure out the transaction type, and get any existing permission value
  OperationType op;
  int32_t index = entry->GetPermissionIndex(typeIndex);
  if (index == -1) {
    if (aPermission == nsIPermissionManager::UNKNOWN_ACTION) {
      op = eOperationNone;
    } else {
      op = eOperationAdding;
    }

  } else {
    PermissionEntry oldPermissionEntry = entry->GetPermissions()[index];

    // remove the permission if the permission is UNKNOWN, update the
    // permission if its value or expire type have changed OR if the time has
    // changed and the expire type is time, otherwise, don't modify.  There's
    // no need to modify a permission that doesn't expire with time when the
    // only thing changed is the expire time.
    if (aPermission == oldPermissionEntry.mPermission &&
        aExpireType == oldPermissionEntry.mExpireType &&
        (aExpireType == nsIPermissionManager::EXPIRE_NEVER ||
         aExpireTime == oldPermissionEntry.mExpireTime)) {
      op = eOperationNone;
    } else if (oldPermissionEntry.mID == cIDPermissionIsDefault &&
               aID != cIDPermissionIsDefault) {
      // An existing default permission already exists, but the new permission
      // isn't a default permission. This case requires some special handing.
      op = eOperationReplacingDefault;
    } else if (oldPermissionEntry.mID != cIDPermissionIsDefault &&
               aID == cIDPermissionIsDefault) {
      // We are adding a default permission but a "real" permission already
      // exists. This means we don't have to do anything here.
      op = eOperationNone;
    } else if (aPermission == nsIPermissionManager::UNKNOWN_ACTION) {
      // At this point, both the old and new permission are either both default
      // permissions, or both not default permissions. Now we only need to check
      // wether to change or remove the old permission.
      op = eOperationRemoving;
    } else {
      op = eOperationChanging;
    }
  }

  // child processes should *always* be passed a modificationTime of zero.
  MOZ_ASSERT(!IsChildProcess() || aModificationTime == 0);

  // do the work for adding, deleting, or changing a permission:
  // update the in-memory list, write to the db, and notify consumers.
  int64_t id;
  if (aModificationTime == 0) {
    aModificationTime = EXPIRY_NOW;
  }

  switch (op) {
    case eOperationNone: {
      // nothing to do
      return NS_OK;
    }

    case eOperationAdding: {
      if (aDBOperation == eWriteToDB) {
        // we'll be writing to the database - generate a known unique id
        id = ++mLargestID;
      } else {
        // we're reading from the database - use the id already assigned
        id = aID;
      }

      entry->GetPermissions().AppendElement(
          PermissionEntry(id, typeIndex, aPermission, aExpireType, aExpireTime,
                          aModificationTime));

      if (aDBOperation == eWriteToDB &&
          IsPersistentExpire(aExpireType, aType)) {
        UpdateDB(op, id, origin, aType, aPermission, aExpireType, aExpireTime,
                 aModificationTime);
      }

      if (aNotifyOperation == eNotify) {
        NotifyObserversWithPermission(aPrincipal, mTypeArray[typeIndex],
                                      aPermission, aExpireType, aExpireTime,
                                      aModificationTime, u"added"_ns);
      }

      break;
    }

    case eOperationRemoving: {
      PermissionEntry oldPermissionEntry = entry->GetPermissions()[index];
      id = oldPermissionEntry.mID;

      // If the type we want to remove is EXPIRE_POLICY, we need to reject
      // attempts to change the permission.
      if (entry->GetPermissions()[index].mExpireType == EXPIRE_POLICY) {
        NS_WARNING("Attempting to remove EXPIRE_POLICY permission");
        break;
      }

      entry->GetPermissions().RemoveElementAt(index);

      if (aDBOperation == eWriteToDB) {
        // We care only about the id here so we pass dummy values for all other
        // parameters.
        UpdateDB(op, id, ""_ns, ""_ns, 0, nsIPermissionManager::EXPIRE_NEVER, 0,
                 0);
      }

      if (aNotifyOperation == eNotify) {
        NotifyObserversWithPermission(
            aPrincipal, mTypeArray[typeIndex], oldPermissionEntry.mPermission,
            oldPermissionEntry.mExpireType, oldPermissionEntry.mExpireTime,
            oldPermissionEntry.mModificationTime, u"deleted"_ns);
      }

      // If there are no more permissions stored for that entry, clear it.
      if (entry->GetPermissions().IsEmpty()) {
        mPermissionTable.RemoveEntry(entry);
      }

      // If the entry we are removing is not a default, restore the potential
      // default entry in-memory
      if (oldPermissionEntry.mID != cIDPermissionIsDefault) {
        for (const DefaultEntry& defaultEntry : mDefaultEntriesForImport) {
          if (defaultEntry.mType == aType && defaultEntry.mOrigin == origin &&
              defaultEntry.mPermission !=
                  nsIPermissionManager::UNKNOWN_ACTION) {
            rv = ImportDefaultEntry(defaultEntry);
            NS_ENSURE_SUCCESS(rv, rv);
            break;
          }
        }
      }

      break;
    }

    case eOperationChanging: {
      id = entry->GetPermissions()[index].mID;

      // If the existing type is EXPIRE_POLICY, we need to reject attempts to
      // change the permission.
      if (entry->GetPermissions()[index].mExpireType == EXPIRE_POLICY) {
        NS_WARNING("Attempting to modify EXPIRE_POLICY permission");
        break;
      }

      PermissionEntry oldPermissionEntry = entry->GetPermissions()[index];

      // If the new expireType is EXPIRE_SESSION, then we have to keep a
      // copy of the previous permission/expireType values. This cached value
      // will be used when restoring the permissions of an app.
      if (entry->GetPermissions()[index].mExpireType !=
              nsIPermissionManager::EXPIRE_SESSION &&
          aExpireType == nsIPermissionManager::EXPIRE_SESSION) {
        entry->GetPermissions()[index].mNonSessionPermission =
            entry->GetPermissions()[index].mPermission;
        entry->GetPermissions()[index].mNonSessionExpireType =
            entry->GetPermissions()[index].mExpireType;
        entry->GetPermissions()[index].mNonSessionExpireTime =
            entry->GetPermissions()[index].mExpireTime;
      } else if (aExpireType != nsIPermissionManager::EXPIRE_SESSION) {
        entry->GetPermissions()[index].mNonSessionPermission = aPermission;
        entry->GetPermissions()[index].mNonSessionExpireType = aExpireType;
        entry->GetPermissions()[index].mNonSessionExpireTime = aExpireTime;
      }

      entry->GetPermissions()[index].mPermission = aPermission;
      entry->GetPermissions()[index].mExpireType = aExpireType;
      entry->GetPermissions()[index].mExpireTime = aExpireTime;
      entry->GetPermissions()[index].mModificationTime = aModificationTime;

      if (aDBOperation == eWriteToDB) {
        bool newIsPersistentExpire = IsPersistentExpire(aExpireType, aType);
        bool oldIsPersistentExpire =
            IsPersistentExpire(oldPermissionEntry.mExpireType, aType);

        if (!newIsPersistentExpire && oldIsPersistentExpire) {
          // Maybe we have to remove the previous permission if that was
          // persistent.
          UpdateDB(eOperationRemoving, id, ""_ns, ""_ns, 0,
                   nsIPermissionManager::EXPIRE_NEVER, 0, 0);
        } else if (newIsPersistentExpire && !oldIsPersistentExpire) {
          // It could also be that the previous permission was session-only but
          // this needs to be written into the DB. In this case, we have to run
          // an Adding operation.
          UpdateDB(eOperationAdding, id, origin, aType, aPermission,
                   aExpireType, aExpireTime, aModificationTime);
        } else if (newIsPersistentExpire) {
          // This is the a simple update.  We care only about the id, the
          // permission and expireType/expireTime/modificationTime here. We pass
          // dummy values for all other parameters.
          UpdateDB(op, id, ""_ns, ""_ns, aPermission, aExpireType, aExpireTime,
                   aModificationTime);
        }
      }

      if (aNotifyOperation == eNotify) {
        NotifyObserversWithPermission(aPrincipal, mTypeArray[typeIndex],
                                      aPermission, aExpireType, aExpireTime,
                                      aModificationTime, u"changed"_ns);
      }

      break;
    }
    case eOperationReplacingDefault: {
      // this is handling the case when we have an existing permission
      // entry that was created as a "default" (and thus isn't in the DB) with
      // an explicit permission (that may include UNKNOWN_ACTION.)
      // Note we will *not* get here if we are replacing an already replaced
      // default value - that is handled as eOperationChanging.

      // So this is a hybrid of eOperationAdding (as we are writing a new entry
      // to the DB) and eOperationChanging (as we are replacing the in-memory
      // repr and sending a "changed" notification).

      // We want a new ID even if not writing to the DB, so the modified entry
      // in memory doesn't have the magic cIDPermissionIsDefault value.
      id = ++mLargestID;

      // The default permission being replaced can't have session expiry or
      // policy expiry.
      NS_ENSURE_TRUE(entry->GetPermissions()[index].mExpireType !=
                         nsIPermissionManager::EXPIRE_SESSION,
                     NS_ERROR_UNEXPECTED);
      NS_ENSURE_TRUE(entry->GetPermissions()[index].mExpireType !=
                         nsIPermissionManager::EXPIRE_POLICY,
                     NS_ERROR_UNEXPECTED);
      // We don't support the new entry having any expiry - supporting that
      // would make things far more complex and none of the permissions we set
      // as a default support that.
      NS_ENSURE_TRUE(aExpireType == EXPIRE_NEVER, NS_ERROR_UNEXPECTED);

      // update the existing entry in memory.
      entry->GetPermissions()[index].mID = id;
      entry->GetPermissions()[index].mPermission = aPermission;
      entry->GetPermissions()[index].mExpireType = aExpireType;
      entry->GetPermissions()[index].mExpireTime = aExpireTime;
      entry->GetPermissions()[index].mModificationTime = aModificationTime;

      // If requested, create the entry in the DB.
      if (aDBOperation == eWriteToDB &&
          IsPersistentExpire(aExpireType, aType)) {
        UpdateDB(eOperationAdding, id, origin, aType, aPermission, aExpireType,
                 aExpireTime, aModificationTime);
      }

      if (aNotifyOperation == eNotify) {
        NotifyObserversWithPermission(aPrincipal, mTypeArray[typeIndex],
                                      aPermission, aExpireType, aExpireTime,
                                      aModificationTime, u"changed"_ns);
      }

    } break;
  }

  return NS_OK;
}

NS_IMETHODIMP
PermissionManager::RemoveFromPrincipal(nsIPrincipal* aPrincipal,
                                       const nsACString& aType) {
  ENSURE_NOT_CHILD_PROCESS;

  MonitorAutoLock lock{mMonitor};
  return RemoveFromPrincipalInternal(aPrincipal, aType);
}

nsresult PermissionManager::RemoveFromPrincipalInternal(
    nsIPrincipal* aPrincipal, const nsACString& aType) {
  ENSURE_NOT_CHILD_PROCESS;
  NS_ENSURE_ARG_POINTER(aPrincipal);

  // System principals are never added to the database, no need to remove them.
  if (aPrincipal->IsSystemPrincipal()) {
    return NS_OK;
  }

  // Permissions may not be added to expanded principals.
  if (IsExpandedPrincipal(aPrincipal)) {
    return NS_ERROR_INVALID_ARG;
  }

  // AddInternal() handles removal, just let it do the work
  return AddInternal(aPrincipal, aType, nsIPermissionManager::UNKNOWN_ACTION, 0,
                     nsIPermissionManager::EXPIRE_NEVER, 0, 0, eNotify,
                     eWriteToDB);
}

NS_IMETHODIMP
PermissionManager::RemovePermission(nsIPermission* aPerm) {
  if (!aPerm) {
    return NS_OK;
  }
  nsCOMPtr<nsIPrincipal> principal;
  nsresult rv = aPerm->GetPrincipal(getter_AddRefs(principal));
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString type;
  rv = aPerm->GetType(type);
  NS_ENSURE_SUCCESS(rv, rv);

  MonitorAutoLock lock{mMonitor};

  // Permissions are uniquely identified by their principal and type.
  // We remove the permission using these two pieces of data.
  return RemoveFromPrincipalInternal(principal, type);
}

NS_IMETHODIMP
PermissionManager::RemoveAll() {
  ENSURE_NOT_CHILD_PROCESS;

  MonitorAutoLock lock{mMonitor};
  return RemoveAllInternal(true);
}

NS_IMETHODIMP
PermissionManager::RemoveAllSince(int64_t aSince) {
  ENSURE_NOT_CHILD_PROCESS;
  MonitorAutoLock lock{mMonitor};
  return RemoveAllModifiedSince(aSince);
}

NS_IMETHODIMP
PermissionManager::RemoveAllExceptTypes(
    const nsTArray<nsCString>& aTypeExceptions) {
  ENSURE_NOT_CHILD_PROCESS;

  MonitorAutoLock lock{mMonitor};
  // Need to make sure read is done before we get the type index. Type indexes
  // are populated from DB.
  EnsureReadCompleted();

  if (aTypeExceptions.IsEmpty()) {
    return RemoveAllInternal(true);
  }

  return RemovePermissionEntries(
      [&](const PermissionEntry& aPermEntry) MOZ_REQUIRES(mMonitor) {
        return !aTypeExceptions.Contains(mTypeArray[aPermEntry.mType]);
      });
}

nsresult PermissionManager::RemovePermissionEntries(
    const std::function<bool(const PermissionEntry& aPermEntry,
                             const nsCOMPtr<nsIPrincipal>& aPrincipal)>&
        aCondition,
    bool aComputePrincipalForCondition) {
  EnsureReadCompleted();

  Vector<std::tuple<nsCOMPtr<nsIPrincipal>, nsCString, nsCString>, 10> array;
  for (const PermissionHashKey& entry : mPermissionTable) {
    for (const auto& permEntry : entry.GetPermissions()) {
      // Depending on whether the principal is needed in the condition check, we
      // may already check the condition here, to avoid needing to compute the
      // principal if the condition is true.
      if (!aComputePrincipalForCondition && !aCondition(permEntry, nullptr)) {
        continue;
      }

      nsCOMPtr<nsIPrincipal> principal;
      nsresult rv = GetPrincipalFromOrigin(
          entry.GetKey()->mOrigin,
          IsOAForceStripPermission(mTypeArray[permEntry.mType]),
          getter_AddRefs(principal));
      if (NS_FAILED(rv)) {
        continue;
      }

      if (aComputePrincipalForCondition && !aCondition(permEntry, principal)) {
        continue;
      }

      if (!array.emplaceBack(principal, mTypeArray[permEntry.mType],
                             entry.GetKey()->mOrigin)) {
        continue;
      }
    }
  }

  for (auto& i : array) {
    // AddInternal handles removal, so let it do the work...
    AddInternal(
        std::get<0>(i), std::get<1>(i), nsIPermissionManager::UNKNOWN_ACTION, 0,
        nsIPermissionManager::EXPIRE_NEVER, 0, 0, PermissionManager::eNotify,
        PermissionManager::eWriteToDB, &std::get<2>(i));
  }

  return NS_OK;
}

nsresult PermissionManager::RemovePermissionEntries(
    const std::function<bool(const PermissionEntry& aPermEntry)>& aCondition) {
  return RemovePermissionEntries(
      [&](const PermissionEntry& aPermEntry,
          const nsCOMPtr<nsIPrincipal>& aPrincipal) {
        return aCondition(aPermEntry);
      },
      false);
}

NS_IMETHODIMP
PermissionManager::RemoveByType(const nsACString& aType) {
  ENSURE_NOT_CHILD_PROCESS;

  MonitorAutoLock lock{mMonitor};

  // Need to make sure read is done before we get the type index. Type indexes
  // are populated from DB.
  EnsureReadCompleted();

  int32_t typeIndex = GetTypeIndex(aType, false);
  // If type == -1, the type isn't known,
  // so just return NS_OK
  if (typeIndex == -1) {
    return NS_OK;
  }

  return RemovePermissionEntries(
      [typeIndex](const PermissionEntry& aPermEntry) {
        return static_cast<uint32_t>(typeIndex) == aPermEntry.mType;
      });
}

NS_IMETHODIMP
PermissionManager::RemoveByTypeSince(const nsACString& aType,
                                     int64_t aModificationTime) {
  ENSURE_NOT_CHILD_PROCESS;

  MonitorAutoLock lock{mMonitor};

  // Need to make sure read is done before we get the type index. Type indexes
  // are populated from DB.
  EnsureReadCompleted();

  int32_t typeIndex = GetTypeIndex(aType, false);
  // If type == -1, the type isn't known,
  // so just return NS_OK
  if (typeIndex == -1) {
    return NS_OK;
  }

  return RemovePermissionEntries(
      [typeIndex, aModificationTime](const PermissionEntry& aPermEntry) {
        return uint32_t(typeIndex) == aPermEntry.mType &&
               aModificationTime <= aPermEntry.mModificationTime;
      });
}

NS_IMETHODIMP
PermissionManager::RemoveAllSinceWithTypeExceptions(
    int64_t aModificationTime, const nsTArray<nsCString>& aTypeExceptions) {
  ENSURE_NOT_CHILD_PROCESS;

  MonitorAutoLock lock{mMonitor};

  // Need to make sure read is done before we get the type index. Type indexes
  // are populated from DB.
  EnsureReadCompleted();

  return RemovePermissionEntries(
      [&](const PermissionEntry& aPermEntry) MOZ_REQUIRES(mMonitor) {
        return !aTypeExceptions.Contains(mTypeArray[aPermEntry.mType]) &&
               aModificationTime <= aPermEntry.mModificationTime;
      });
}

void PermissionManager::CloseDB(CloseDBNextOp aNextOp) {
  EnsureReadCompleted();

  mState = eClosed;

  nsCOMPtr<nsIInputStream> defaultsInputStream;
  if (aNextOp == eRebuldOnSuccess) {
    defaultsInputStream = GetDefaultsInputStream();
  }

  RefPtr<PermissionManager> self = this;
  mThread->Dispatch(NS_NewRunnableFunction(
      "PermissionManager::CloseDB", [self, aNextOp, defaultsInputStream] {
        auto data = self->mThreadBoundData.Access();
        // Null the statements, this will finalize them.
        data->mStmtInsert = nullptr;
        data->mStmtDelete = nullptr;
        data->mStmtUpdate = nullptr;
        if (data->mDBConn) {
          DebugOnly<nsresult> rv = data->mDBConn->Close();
          MOZ_ASSERT(NS_SUCCEEDED(rv));
          data->mDBConn = nullptr;

          if (aNextOp == eRebuldOnSuccess) {
            MonitorAutoLock lock{self->mMonitor};
            self->TryInitDB(true, defaultsInputStream, lock);
          }
        }

        if (aNextOp == eShutdown) {
          NS_DispatchToMainThread(
              NS_NewRunnableFunction("PermissionManager::FinishAsyncShutdown",
                                     [self] { self->FinishAsyncShutdown(); }));
        }
      }));
}

nsresult PermissionManager::RemoveAllFromIPC() {
  MOZ_ASSERT(IsChildProcess());

  MonitorAutoLock lock{mMonitor};

  // Remove from memory and notify immediately. Since the in-memory
  // database is authoritative, we do not need confirmation from the
  // on-disk database to notify observers.
  RemoveAllFromMemory();

  return NS_OK;
}

nsresult PermissionManager::RemoveAllInternal(bool aNotifyObservers) {
  ENSURE_NOT_CHILD_PROCESS;

  EnsureReadCompleted();

  // Let's broadcast the removeAll() to any content process.
  nsTArray<ContentParent*> parents;
  ContentParent::GetAll(parents);
  for (ContentParent* parent : parents) {
    Unused << parent->SendRemoveAllPermissions();
  }

  // Remove from memory and notify immediately. Since the in-memory
  // database is authoritative, we do not need confirmation from the
  // on-disk database to notify observers.
  RemoveAllFromMemory();

  // Re-import the defaults
  ImportLatestDefaults();

  if (aNotifyObservers) {
    NotifyObservers(nullptr, u"cleared"_ns);
  }

  RefPtr<PermissionManager> self = this;
  mThread->Dispatch(
      NS_NewRunnableFunction("PermissionManager::RemoveAllInternal", [self] {
        auto data = self->mThreadBoundData.Access();

        if (self->mState == eClosed || !data->mDBConn) {
          return;
        }

        // clear the db
        nsresult rv =
            data->mDBConn->ExecuteSimpleSQL("DELETE FROM moz_perms"_ns);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          NS_DispatchToMainThread(NS_NewRunnableFunction(
              "PermissionManager::RemoveAllInternal-Failure", [self] {
                MonitorAutoLock lock{self->mMonitor};
                self->CloseDB(eRebuldOnSuccess);
              }));
        }
      }));

  return NS_OK;
}

NS_IMETHODIMP
PermissionManager::TestExactPermissionFromPrincipal(nsIPrincipal* aPrincipal,
                                                    const nsACString& aType,
                                                    uint32_t* aPermission) {
  MonitorAutoLock lock{mMonitor};
  return CommonTestPermission(aPrincipal, -1, aType, aPermission,
                              nsIPermissionManager::UNKNOWN_ACTION, false, true,
                              true);
}

NS_IMETHODIMP
PermissionManager::TestExactPermanentPermission(nsIPrincipal* aPrincipal,
                                                const nsACString& aType,
                                                uint32_t* aPermission) {
  MonitorAutoLock lock{mMonitor};
  return CommonTestPermission(aPrincipal, -1, aType, aPermission,
                              nsIPermissionManager::UNKNOWN_ACTION, false, true,
                              false);
}

NS_IMETHODIMP
PermissionManager::TestPermissionFromPrincipal(nsIPrincipal* aPrincipal,
                                               const nsACString& aType,
                                               uint32_t* aPermission) {
  MonitorAutoLock lock{mMonitor};
  return CommonTestPermission(aPrincipal, -1, aType, aPermission,
                              nsIPermissionManager::UNKNOWN_ACTION, false,
                              false, true);
}

NS_IMETHODIMP
PermissionManager::GetPermissionObject(nsIPrincipal* aPrincipal,
                                       const nsACString& aType,
                                       bool aExactHostMatch,
                                       nsIPermission** aResult) {
  NS_ENSURE_ARG_POINTER(aPrincipal);
  *aResult = nullptr;

  MonitorAutoLock lock{mMonitor};

  EnsureReadCompleted();

  if (aPrincipal->IsSystemPrincipal()) {
    return NS_OK;
  }

  // Querying the permission object of an nsEP is non-sensical.
  if (IsExpandedPrincipal(aPrincipal)) {
    return NS_ERROR_INVALID_ARG;
  }

  MOZ_ASSERT(PermissionAvailableInternal(aPrincipal, aType));

  int32_t typeIndex = GetTypeIndex(aType, false);
  // If type == -1, the type isn't known,
  // so just return NS_OK
  if (typeIndex == -1) return NS_OK;

  PermissionHashKey* entry =
      GetPermissionHashKey(aPrincipal, typeIndex, aExactHostMatch);
  if (!entry) {
    return NS_OK;
  }

  // We don't call GetPermission(typeIndex) because that returns a fake
  // UNKNOWN_ACTION entry if there is no match.
  int32_t idx = entry->GetPermissionIndex(typeIndex);
  if (-1 == idx) {
    return NS_OK;
  }

  nsCOMPtr<nsIPrincipal> principal;
  nsresult rv = GetPrincipalFromOrigin(entry->GetKey()->mOrigin,
                                       IsOAForceStripPermission(aType),
                                       getter_AddRefs(principal));
  NS_ENSURE_SUCCESS(rv, rv);

  PermissionEntry& perm = entry->GetPermissions()[idx];
  nsCOMPtr<nsIPermission> r = Permission::Create(
      principal, mTypeArray[perm.mType], perm.mPermission, perm.mExpireType,
      perm.mExpireTime, perm.mModificationTime);
  if (NS_WARN_IF(!r)) {
    return NS_ERROR_FAILURE;
  }
  r.forget(aResult);
  return NS_OK;
}

nsresult PermissionManager::CommonTestPermissionInternal(
    nsIPrincipal* aPrincipal, nsIURI* aURI,
    const OriginAttributes* aOriginAttributes, int32_t aTypeIndex,
    const nsACString& aType, uint32_t* aPermission, bool aExactHostMatch,
    bool aIncludingSession) {
  MOZ_ASSERT(aPrincipal || aURI);
  NS_ENSURE_ARG_POINTER(aPrincipal || aURI);
  MOZ_ASSERT_IF(aPrincipal, !aURI && !aOriginAttributes);
  MOZ_ASSERT_IF(aURI || aOriginAttributes, !aPrincipal);

  EnsureReadCompleted();

#ifdef DEBUG
  {
    nsCOMPtr<nsIPrincipal> prin = aPrincipal;
    if (!prin) {
      if (aURI) {
        prin = BasePrincipal::CreateContentPrincipal(aURI, OriginAttributes());
      }
    }
    MOZ_ASSERT(prin);
    MOZ_ASSERT(PermissionAvailableInternal(prin, aType));
  }
#endif

  PermissionHashKey* entry =
      aPrincipal ? GetPermissionHashKey(aPrincipal, aTypeIndex, aExactHostMatch)
                 : GetPermissionHashKey(aURI, aOriginAttributes, aTypeIndex,
                                        aExactHostMatch);
  if (!entry || (!aIncludingSession &&
                 entry->GetPermission(aTypeIndex).mNonSessionExpireType ==
                     nsIPermissionManager::EXPIRE_SESSION)) {
    return NS_OK;
  }

  *aPermission = aIncludingSession
                     ? entry->GetPermission(aTypeIndex).mPermission
                     : entry->GetPermission(aTypeIndex).mNonSessionPermission;
  return NS_OK;
}

nsresult PermissionManager::GetPermissionEntries(
    const std::function<bool(const PermissionEntry& aPermEntry)>& aCondition,
    nsTArray<RefPtr<nsIPermission>>& aResult) {
  aResult.Clear();
  if (XRE_IsContentProcess()) {
    NS_WARNING(
        "Iterating over all permissions is not available in the "
        "content process, as not all permissions may be available.");
    return NS_ERROR_NOT_AVAILABLE;
  }

  EnsureReadCompleted();

  for (const PermissionHashKey& entry : mPermissionTable) {
    for (const auto& permEntry : entry.GetPermissions()) {
      // Given how "default" permissions work and the possibility of them being
      // overridden with UNKNOWN_ACTION, we might see this value here - but we
      // do *not* want to return them via the enumerator.
      if (permEntry.mPermission == nsIPermissionManager::UNKNOWN_ACTION) {
        continue;
      }

      // If the permission is expired, skip it. We're not deleting it here
      // because we're iterating over a lot of permissions.
      // It will be removed as part of the daily maintenance later.
      if (HasExpired(permEntry.mExpireType, permEntry.mExpireTime)) {
        continue;
      }

      if (!aCondition(permEntry)) {
        continue;
      }

      nsCOMPtr<nsIPrincipal> principal;
      nsresult rv = GetPrincipalFromOrigin(
          entry.GetKey()->mOrigin,
          IsOAForceStripPermission(mTypeArray[permEntry.mType]),
          getter_AddRefs(principal));
      if (NS_FAILED(rv)) {
        continue;
      }

      RefPtr<nsIPermission> permission = Permission::Create(
          principal, mTypeArray[permEntry.mType], permEntry.mPermission,
          permEntry.mExpireType, permEntry.mExpireTime,
          permEntry.mModificationTime);
      if (NS_WARN_IF(!permission)) {
        continue;
      }
      aResult.AppendElement(std::move(permission));
    }
  }

  return NS_OK;
}

NS_IMETHODIMP PermissionManager::GetAll(
    nsTArray<RefPtr<nsIPermission>>& aResult) {
  MonitorAutoLock lock{mMonitor};
  return GetPermissionEntries(
      [](const PermissionEntry& aPermEntry) { return true; }, aResult);
}

NS_IMETHODIMP PermissionManager::GetAllByTypeSince(
    const nsACString& aPrefix, int64_t aSince,
    nsTArray<RefPtr<nsIPermission>>& aResult) {
  // Check that aSince is a reasonable point in time, not in the future
  if (aSince > (PR_Now() / PR_USEC_PER_MSEC)) {
    return NS_ERROR_INVALID_ARG;
  }

  MonitorAutoLock lock{mMonitor};
  return GetPermissionEntries(
      [&](const PermissionEntry& aPermEntry) MOZ_REQUIRES(mMonitor) {
        return mTypeArray[aPermEntry.mType].Equals(aPrefix) &&
               aSince <= aPermEntry.mModificationTime;
      },
      aResult);
}

NS_IMETHODIMP PermissionManager::GetAllWithTypePrefix(
    const nsACString& aPrefix, nsTArray<RefPtr<nsIPermission>>& aResult) {
  MonitorAutoLock lock{mMonitor};
  return GetPermissionEntries(
      [&](const PermissionEntry& aPermEntry) MOZ_REQUIRES(mMonitor) {
        return StringBeginsWith(mTypeArray[aPermEntry.mType], aPrefix);
      },
      aResult);
}

NS_IMETHODIMP PermissionManager::GetAllByTypes(
    const nsTArray<nsCString>& aTypes,
    nsTArray<RefPtr<nsIPermission>>& aResult) {
  if (aTypes.IsEmpty()) {
    return NS_OK;
  }

  MonitorAutoLock lock{mMonitor};
  return GetPermissionEntries(
      [&](const PermissionEntry& aPermEntry) MOZ_REQUIRES(mMonitor) {
        return aTypes.Contains(mTypeArray[aPermEntry.mType]);
      },
      aResult);
}

nsresult PermissionManager::ShouldHandlePrincipalForPermission(
    nsIPrincipal* aPrincipal, bool& aIsPermissionPrincipalValid) {
  NS_ENSURE_ARG_POINTER(aPrincipal);
  // We don't add the system principal because it actually has no URI and we
  // always allow action for them.
  if (aPrincipal->IsSystemPrincipal()) {
    aIsPermissionPrincipalValid = false;
    return NS_OK;
  }

  // Null principals can't meaningfully have persisted permissions attached to
  // them, so we don't allow adding permissions for them.
  if (aPrincipal->GetIsNullPrincipal()) {
    aIsPermissionPrincipalValid = false;
    return NS_OK;
  }

  // Permissions may not be added to expanded principals.
  if (IsExpandedPrincipal(aPrincipal)) {
    aIsPermissionPrincipalValid = false;
    return NS_ERROR_INVALID_ARG;
  }

  // Permission principal is valid
  aIsPermissionPrincipalValid = true;
  return NS_OK;
}

nsresult PermissionManager::GetAllForPrincipalHelper(
    nsIPrincipal* aPrincipal, bool aSiteScopePermissions,
    nsTArray<RefPtr<nsIPermission>>& aResult) {
  nsresult rv;
  RefPtr<PermissionKey> key = PermissionKey::CreateFromPrincipal(
      aPrincipal, false, aSiteScopePermissions, rv);
  if (!key) {
    MOZ_ASSERT(NS_FAILED(rv));
    return rv;
  }
  PermissionHashKey* entry = mPermissionTable.GetEntry(key);

  nsTArray<PermissionEntry> strippedPerms;
  rv = GetStripPermsForPrincipal(aPrincipal, aSiteScopePermissions,
                                 strippedPerms);
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (entry) {
    for (const auto& permEntry : entry->GetPermissions()) {
      // Only return custom permissions
      if (permEntry.mPermission == nsIPermissionManager::UNKNOWN_ACTION) {
        continue;
      }

      // If the permission is expired, skip it. We're not deleting it here
      // because we're iterating over a lot of permissions.
      // It will be removed as part of the daily maintenance later.
      if (HasExpired(permEntry.mExpireType, permEntry.mExpireTime)) {
        continue;
      }

      // Make sure that we only get site scoped permissions if this
      // helper is being invoked for that purpose.
      if (aSiteScopePermissions !=
          IsSiteScopedPermission(mTypeArray[permEntry.mType])) {
        continue;
      }

      // Stripped principal permissions overwrite regular ones
      // For each permission check if there is a stripped permission we should
      // use instead
      PermissionEntry perm = permEntry;
      nsTArray<PermissionEntry>::index_type index = 0;
      for (const auto& strippedPerm : strippedPerms) {
        if (strippedPerm.mType == permEntry.mType) {
          perm = strippedPerm;
          strippedPerms.RemoveElementAt(index);
          break;
        }
        index++;
      }

      RefPtr<nsIPermission> permission = Permission::Create(
          aPrincipal, mTypeArray[perm.mType], perm.mPermission,
          perm.mExpireType, perm.mExpireTime, perm.mModificationTime);
      if (NS_WARN_IF(!permission)) {
        continue;
      }
      aResult.AppendElement(permission);
    }
  }

  for (const auto& perm : strippedPerms) {
    RefPtr<nsIPermission> permission = Permission::Create(
        aPrincipal, mTypeArray[perm.mType], perm.mPermission, perm.mExpireType,
        perm.mExpireTime, perm.mModificationTime);
    if (NS_WARN_IF(!permission)) {
      continue;
    }
    aResult.AppendElement(permission);
  }

  return NS_OK;
}

NS_IMETHODIMP
PermissionManager::GetAllForPrincipal(
    nsIPrincipal* aPrincipal, nsTArray<RefPtr<nsIPermission>>& aResult) {
  nsresult rv;
  aResult.Clear();

  MonitorAutoLock lock{mMonitor};
  EnsureReadCompleted();

  MOZ_ASSERT(PermissionAvailableInternal(aPrincipal, ""_ns));

  // First, append the non-site-scoped permissions.
  rv = GetAllForPrincipalHelper(aPrincipal, false, aResult);
  NS_ENSURE_SUCCESS(rv, rv);

  // Second, append the site-scoped permissions.
  return GetAllForPrincipalHelper(aPrincipal, true, aResult);
}

NS_IMETHODIMP PermissionManager::Observe(nsISupports* aSubject,
                                         const char* aTopic,
                                         const char16_t* someData) {
  ENSURE_NOT_CHILD_PROCESS;

  MonitorAutoLock lock{mMonitor};

  if (!nsCRT::strcmp(aTopic, "profile-do-change") && !mPermissionsFile) {
    // profile startup is complete, and we didn't have the permissions file
    // before; init the db from the new location
    InitDB(false);
  } else if (!nsCRT::strcmp(aTopic, "testonly-reload-permissions-from-disk")) {
    // Testing mechanism to reload all permissions from disk. Because the
    // permission manager automatically initializes itself at startup, tests
    // that directly manipulate the permissions database need some way to reload
    // the database for their changes to have any effect. This mechanism was
    // introduced when moving the permissions manager from on-demand startup to
    // always being initialized. This is not guarded by a pref because it's not
    // dangerous to reload permissions from disk, just bad for performance.
    RemoveAllFromMemory();
    CloseDB(eNone);
    InitDB(false);
  } else if (!nsCRT::strcmp(aTopic, OBSERVER_TOPIC_IDLE_DAILY)) {
    PerformIdleDailyMaintenance();
  } else if (!nsCRT::strcmp(aTopic, "last-pb-context-exited")) {
    DebugOnly<nsresult> rv = RemoveAllForPrivateBrowsing();
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv),
                         "Failed to clear private browsing permissions");
  }

  return NS_OK;
}

nsresult PermissionManager::RemoveAllModifiedSince(int64_t aModificationTime) {
  ENSURE_NOT_CHILD_PROCESS;
  // Skip remove calls for default permissions to avoid
  // creating UNKNOWN_ACTION overrides in AddInternal
  return RemovePermissionEntries(
      [aModificationTime](const PermissionEntry& aPermEntry) {
        return aModificationTime <= aPermEntry.mModificationTime &&
               aPermEntry.mID != cIDPermissionIsDefault;
      });
}

nsresult PermissionManager::RemoveAllForPrivateBrowsing() {
  ENSURE_NOT_CHILD_PROCESS;
  return RemovePermissionEntries([](const PermissionEntry& aPermEntry,
                                    const nsCOMPtr<nsIPrincipal>& aPrincipal) {
    return aPrincipal->GetIsInPrivateBrowsing() &&
           aPermEntry.mID != cIDPermissionIsDefault;
  });
}

NS_IMETHODIMP
PermissionManager::RemovePermissionsWithAttributes(
    const nsAString& aPattern, const nsTArray<nsCString>& aTypeInclusions,
    const nsTArray<nsCString>& aTypeExceptions) {
  ENSURE_NOT_CHILD_PROCESS;

  OriginAttributesPattern pattern;
  if (!pattern.Init(aPattern)) {
    return NS_ERROR_INVALID_ARG;
  }

  MonitorAutoLock lock{mMonitor};
  return RemovePermissionsWithAttributes(pattern, aTypeInclusions,
                                         aTypeExceptions);
}

nsresult PermissionManager::RemovePermissionsWithAttributes(
    OriginAttributesPattern& aPattern,
    const nsTArray<nsCString>& aTypeInclusions,
    const nsTArray<nsCString>& aTypeExceptions) {
  EnsureReadCompleted();

  Vector<std::tuple<nsCOMPtr<nsIPrincipal>, nsCString, nsCString>, 10>
      permissions;
  for (const PermissionHashKey& entry : mPermissionTable) {
    nsCOMPtr<nsIPrincipal> principal;
    nsresult rv = GetPrincipalFromOrigin(entry.GetKey()->mOrigin, false,
                                         getter_AddRefs(principal));
    if (NS_FAILED(rv)) {
      continue;
    }

    if (!aPattern.Matches(principal->OriginAttributesRef())) {
      continue;
    }

    for (const auto& permEntry : entry.GetPermissions()) {
      if (aTypeExceptions.Contains(mTypeArray[permEntry.mType])) {
        continue;
      }
      if (!aTypeInclusions.IsEmpty() &&
          !aTypeInclusions.Contains(mTypeArray[permEntry.mType])) {
        continue;
      }
      if (!permissions.emplaceBack(principal, mTypeArray[permEntry.mType],
                                   entry.GetKey()->mOrigin)) {
        continue;
      }
    }
  }

  for (auto& i : permissions) {
    AddInternal(
        std::get<0>(i), std::get<1>(i), nsIPermissionManager::UNKNOWN_ACTION, 0,
        nsIPermissionManager::EXPIRE_NEVER, 0, 0, PermissionManager::eNotify,
        PermissionManager::eWriteToDB, &std::get<2>(i));
  }

  return NS_OK;
}

nsresult PermissionManager::GetStripPermsForPrincipal(
    nsIPrincipal* aPrincipal, bool aSiteScopePermissions,
    nsTArray<PermissionEntry>& aResult) {
  aResult.Clear();
  aResult.SetCapacity(kStripOAPermissions.size());

#ifdef __clang__
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wunreachable-code-return"
#endif
  // No special strip permissions
  if (kStripOAPermissions.empty()) {
    return NS_OK;
  }
#ifdef __clang__
#  pragma clang diagnostic pop
#endif

  nsresult rv;
  // Create a key for the principal, but strip any origin attributes.
  // The key must be created aware of whether or not we are scoping to site.
  RefPtr<PermissionKey> key = PermissionKey::CreateFromPrincipal(
      aPrincipal, true, aSiteScopePermissions, rv);
  if (!key) {
    MOZ_ASSERT(NS_FAILED(rv));
    return rv;
  }

  PermissionHashKey* hashKey = mPermissionTable.GetEntry(key);
  if (!hashKey) {
    return NS_OK;
  }

  for (const auto& permType : kStripOAPermissions) {
    // if the permission type's site scoping does not match this function call,
    // we don't care about it, so continue.
    // As of time of writing, this never happens when aSiteScopePermissions
    // is true because there is no common permission between kStripOAPermissions
    // and kSiteScopedPermissions
    if (aSiteScopePermissions != IsSiteScopedPermission(permType)) {
      continue;
    }
    int32_t index = GetTypeIndex(permType, false);
    if (index == -1) {
      continue;
    }
    PermissionEntry perm = hashKey->GetPermission(index);
    if (perm.mPermission == nsIPermissionManager::UNKNOWN_ACTION) {
      continue;
    }
    aResult.AppendElement(perm);
  }

  return NS_OK;
}

int32_t PermissionManager::GetTypeIndex(const nsACString& aType, bool aAdd) {
  for (uint32_t i = 0; i < mTypeArray.length(); ++i) {
    if (mTypeArray[i].Equals(aType)) {
      return i;
    }
  }

  if (!aAdd) {
    // Not found, but that is ok - we were just looking.
    return -1;
  }

  // This type was not registered before.
  // append it to the array, without copy-constructing the string
  if (!mTypeArray.emplaceBack(aType)) {
    return -1;
  }

  return mTypeArray.length() - 1;
}

PermissionManager::PermissionHashKey* PermissionManager::GetPermissionHashKey(
    nsIPrincipal* aPrincipal, uint32_t aType, bool aExactHostMatch) {
  EnsureReadCompleted();

  MOZ_ASSERT(PermissionAvailableInternal(aPrincipal, mTypeArray[aType]));

  nsresult rv;
  RefPtr<PermissionKey> key = PermissionKey::CreateFromPrincipal(
      aPrincipal, IsOAForceStripPermission(mTypeArray[aType]),
      IsSiteScopedPermission(mTypeArray[aType]), rv);
  if (!key) {
    return nullptr;
  }

  PermissionHashKey* entry = mPermissionTable.GetEntry(key);

  if (entry) {
    PermissionEntry permEntry = entry->GetPermission(aType);

    // if the entry is expired, remove and keep looking for others.
    if (HasExpired(permEntry.mExpireType, permEntry.mExpireTime)) {
      entry = nullptr;
      RemoveFromPrincipalInternal(aPrincipal, mTypeArray[aType]);
    } else if (permEntry.mPermission == nsIPermissionManager::UNKNOWN_ACTION) {
      entry = nullptr;
    }
  }

  if (entry) {
    return entry;
  }

  // If aExactHostMatch wasn't true, we can check if the base domain has a
  // permission entry.
  if (!aExactHostMatch) {
    nsCOMPtr<nsIPrincipal> principal = aPrincipal->GetNextSubDomainPrincipal();
    if (principal) {
      return GetPermissionHashKey(principal, aType, aExactHostMatch);
    }
  }

  // No entry, really...
  return nullptr;
}

PermissionManager::PermissionHashKey* PermissionManager::GetPermissionHashKey(
    nsIURI* aURI, const OriginAttributes* aOriginAttributes, uint32_t aType,
    bool aExactHostMatch) {
  MOZ_ASSERT(aURI);

#ifdef DEBUG
  {
    nsCOMPtr<nsIPrincipal> principal;
    nsresult rv = NS_OK;
    if (aURI) {
      rv = GetPrincipal(aURI, getter_AddRefs(principal));
    }
    MOZ_ASSERT_IF(NS_SUCCEEDED(rv),
                  PermissionAvailableInternal(principal, mTypeArray[aType]));
  }
#endif

  nsresult rv;
  RefPtr<PermissionKey> key;

  if (aOriginAttributes) {
    key = PermissionKey::CreateFromURIAndOriginAttributes(
        aURI, aOriginAttributes, IsOAForceStripPermission(mTypeArray[aType]),
        rv);
  } else {
    key = PermissionKey::CreateFromURI(aURI, rv);
  }

  if (!key) {
    return nullptr;
  }

  PermissionHashKey* entry = mPermissionTable.GetEntry(key);

  if (entry) {
    PermissionEntry permEntry = entry->GetPermission(aType);

    // if the entry is expired, remove and keep looking for others.
    if (HasExpired(permEntry.mExpireType, permEntry.mExpireTime)) {
      entry = nullptr;
      // If we need to remove a permission we mint a principal.  This is a bit
      // inefficient, but hopefully this code path isn't super common.
      nsCOMPtr<nsIPrincipal> principal;
      if (aURI) {
        nsresult rv = GetPrincipal(aURI, getter_AddRefs(principal));
        if (NS_WARN_IF(NS_FAILED(rv))) {
          return nullptr;
        }
      }
      RemoveFromPrincipal(principal, mTypeArray[aType]);
    } else if (permEntry.mPermission == nsIPermissionManager::UNKNOWN_ACTION) {
      entry = nullptr;
    }
  }

  if (entry) {
    return entry;
  }

  // If aExactHostMatch wasn't true, we can check if the base domain has a
  // permission entry.
  if (!aExactHostMatch) {
    nsCOMPtr<nsIURI> uri;
    if (aURI) {
      uri = GetNextSubDomainURI(aURI);
    }
    if (uri) {
      return GetPermissionHashKey(uri, aOriginAttributes, aType,
                                  aExactHostMatch);
    }
  }

  // No entry, really...
  return nullptr;
}

nsresult PermissionManager::RemoveAllFromMemory() {
  mLargestID = 0;
  mTypeArray.clear();
  mPermissionTable.Clear();

  return NS_OK;
}

// wrapper function for mangling (host,type,perm,expireType,expireTime)
// set into an nsIPermission.
void PermissionManager::NotifyObserversWithPermission(
    nsIPrincipal* aPrincipal, const nsACString& aType, uint32_t aPermission,
    uint32_t aExpireType, int64_t aExpireTime, int64_t aModificationTime,
    const nsString& aData) {
  nsCOMPtr<nsIPermission> permission =
      Permission::Create(aPrincipal, aType, aPermission, aExpireType,
                         aExpireTime, aModificationTime);
  if (permission) {
    NotifyObservers(permission, aData);
  }
}

// notify observers that the permission list changed. there are four possible
// values for aData:
// "deleted" means a permission was deleted. aPermission is the deleted
// permission. "added"   means a permission was added. aPermission is the added
// permission. "changed" means a permission was altered. aPermission is the new
// permission. "cleared" means the entire permission list was cleared.
// aPermission is null.
void PermissionManager::NotifyObservers(
    const nsCOMPtr<nsIPermission>& aPermission, const nsString& aData) {
  nsCOMPtr<nsIObserverService> observerService = services::GetObserverService();
  if (observerService) {
    // we need to release the monitor here because the observers for the below
    // notification can call back in to permission manager and try to lock the
    // monitor again.
    MonitorAutoUnlock unlock{mMonitor};
    observerService->NotifyObservers(aPermission, kPermissionChangeNotification,
                                     aData.Data());
  }
}

nsresult PermissionManager::Read(const MonitorAutoLock& aProofOfLock) {
  ENSURE_NOT_CHILD_PROCESS;

  MOZ_ASSERT(!NS_IsMainThread());
  auto data = mThreadBoundData.Access();

  nsresult rv;
  bool hasResult;
  nsCOMPtr<mozIStorageStatement> stmt;

  // Let's retrieve the last used ID.
  rv = data->mDBConn->CreateStatement(
      nsLiteralCString("SELECT MAX(id) FROM moz_perms"), getter_AddRefs(stmt));
  NS_ENSURE_SUCCESS(rv, rv);

  while (NS_SUCCEEDED(stmt->ExecuteStep(&hasResult)) && hasResult) {
    int64_t id = stmt->AsInt64(0);
    mLargestID = id;
  }

  rv = data->mDBConn->CreateStatement(
      nsLiteralCString(
          "SELECT id, origin, type, permission, expireType, "
          "expireTime, modificationTime "
          "FROM moz_perms WHERE expireType != ?1 OR expireTime > ?2"),
      getter_AddRefs(stmt));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = stmt->BindInt32ByIndex(0, nsIPermissionManager::EXPIRE_TIME);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = stmt->BindInt64ByIndex(1, EXPIRY_NOW);
  NS_ENSURE_SUCCESS(rv, rv);

  bool readError = false;

  while (NS_SUCCEEDED(stmt->ExecuteStep(&hasResult)) && hasResult) {
    ReadEntry entry;

    // explicitly set our entry id counter for use in AddInternal(),
    // and keep track of the largest id so we know where to pick up.
    entry.mId = stmt->AsInt64(0);
    MOZ_ASSERT(entry.mId <= mLargestID);

    rv = stmt->GetUTF8String(1, entry.mOrigin);
    if (NS_FAILED(rv)) {
      readError = true;
      continue;
    }

    rv = stmt->GetUTF8String(2, entry.mType);
    if (NS_FAILED(rv)) {
      readError = true;
      continue;
    }

    entry.mPermission = stmt->AsInt32(3);
    entry.mExpireType = stmt->AsInt32(4);

    // convert into int64_t values (milliseconds)
    entry.mExpireTime = stmt->AsInt64(5);
    entry.mModificationTime = stmt->AsInt64(6);

    entry.mFromMigration = false;

    mReadEntries.AppendElement(entry);
  }

  if (readError) {
    NS_ERROR("Error occured while reading the permissions database!");
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

void PermissionManager::CompleteMigrations() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mState == eReady);

  nsresult rv;

  nsTArray<MigrationEntry> entries = std::move(mMigrationEntries);

  for (const MigrationEntry& entry : entries) {
    rv = UpgradeHostToOriginAndInsert(
        entry.mHost, entry.mType, entry.mPermission, entry.mExpireType,
        entry.mExpireTime, entry.mModificationTime,
        [&](const nsACString& aOrigin, const nsCString& aType,
            uint32_t aPermission, uint32_t aExpireType, int64_t aExpireTime,
            int64_t aModificationTime) MOZ_REQUIRES(mMonitor) {
          MaybeAddReadEntryFromMigration(aOrigin, aType, aPermission,
                                         aExpireType, aExpireTime,
                                         aModificationTime, entry.mId);
          return NS_OK;
        });
    Unused << NS_WARN_IF(NS_FAILED(rv));
  }
}

void PermissionManager::CompleteRead() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mState == eReady);

  nsresult rv;

  nsTArray<ReadEntry> entries = std::move(mReadEntries);

  for (const ReadEntry& entry : entries) {
    nsCOMPtr<nsIPrincipal> principal;
    rv = GetPrincipalFromOrigin(entry.mOrigin,
                                IsOAForceStripPermission(entry.mType),
                                getter_AddRefs(principal));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      continue;
    }

    DBOperationType op = entry.mFromMigration ? eWriteToDB : eNoDBOperation;

    rv = AddInternal(principal, entry.mType, entry.mPermission, entry.mId,
                     entry.mExpireType, entry.mExpireTime,
                     entry.mModificationTime, eDontNotify, op, &entry.mOrigin);
    Unused << NS_WARN_IF(NS_FAILED(rv));
  }
}

void PermissionManager::MaybeAddReadEntryFromMigration(
    const nsACString& aOrigin, const nsCString& aType, uint32_t aPermission,
    uint32_t aExpireType, int64_t aExpireTime, int64_t aModificationTime,
    int64_t aId) {
  // We convert a migration to a ReadEntry only if we don't have an existing
  // ReadEntry for the same origin + type.
  for (const ReadEntry& entry : mReadEntries) {
    if (entry.mOrigin == aOrigin && entry.mType == aType) {
      return;
    }
  }

  ReadEntry entry;
  entry.mId = aId;
  entry.mOrigin = aOrigin;
  entry.mType = aType;
  entry.mPermission = aPermission;
  entry.mExpireType = aExpireType;
  entry.mExpireTime = aExpireTime;
  entry.mModificationTime = aModificationTime;
  entry.mFromMigration = true;

  mReadEntries.AppendElement(entry);
}

void PermissionManager::UpdateDB(OperationType aOp, int64_t aID,
                                 const nsACString& aOrigin,
                                 const nsACString& aType, uint32_t aPermission,
                                 uint32_t aExpireType, int64_t aExpireTime,
                                 int64_t aModificationTime) {
  ENSURE_NOT_CHILD_PROCESS_NORET;

  MOZ_ASSERT(NS_IsMainThread());
  EnsureReadCompleted();

  nsCString origin(aOrigin);
  nsCString type(aType);

  RefPtr<PermissionManager> self = this;
  mThread->Dispatch(NS_NewRunnableFunction(
      "PermissionManager::UpdateDB",
      [self, aOp, aID, origin, type, aPermission, aExpireType, aExpireTime,
       aModificationTime] {
        nsresult rv;

        auto data = self->mThreadBoundData.Access();

        if (self->mState == eClosed || !data->mDBConn) {
          // no statement is ok - just means we don't have a profile
          return;
        }

        mozIStorageStatement* stmt = nullptr;
        switch (aOp) {
          case eOperationAdding: {
            stmt = data->mStmtInsert;

            rv = stmt->BindInt64ByIndex(0, aID);
            if (NS_FAILED(rv)) break;

            rv = stmt->BindUTF8StringByIndex(1, origin);
            if (NS_FAILED(rv)) break;

            rv = stmt->BindUTF8StringByIndex(2, type);
            if (NS_FAILED(rv)) break;

            rv = stmt->BindInt32ByIndex(3, aPermission);
            if (NS_FAILED(rv)) break;

            rv = stmt->BindInt32ByIndex(4, aExpireType);
            if (NS_FAILED(rv)) break;

            rv = stmt->BindInt64ByIndex(5, aExpireTime);
            if (NS_FAILED(rv)) break;

            rv = stmt->BindInt64ByIndex(6, aModificationTime);
            break;
          }

          case eOperationRemoving: {
            stmt = data->mStmtDelete;
            rv = stmt->BindInt64ByIndex(0, aID);
            break;
          }

          case eOperationChanging: {
            stmt = data->mStmtUpdate;

            rv = stmt->BindInt64ByIndex(0, aID);
            if (NS_FAILED(rv)) break;

            rv = stmt->BindInt32ByIndex(1, aPermission);
            if (NS_FAILED(rv)) break;

            rv = stmt->BindInt32ByIndex(2, aExpireType);
            if (NS_FAILED(rv)) break;

            rv = stmt->BindInt64ByIndex(3, aExpireTime);
            if (NS_FAILED(rv)) break;

            rv = stmt->BindInt64ByIndex(4, aModificationTime);
            break;
          }

          default: {
            MOZ_ASSERT_UNREACHABLE("need a valid operation in UpdateDB()!");
            rv = NS_ERROR_UNEXPECTED;
            break;
          }
        }

        if (NS_FAILED(rv)) {
          NS_WARNING("db change failed!");
          return;
        }

        rv = stmt->Execute();
        MOZ_ASSERT(NS_SUCCEEDED(rv));
      }));
}

bool PermissionManager::GetPermissionsFromOriginOrKey(
    const nsACString& aOrigin, const nsACString& aKey,
    nsTArray<IPC::Permission>& aPerms) {
  MonitorAutoLock lock{mMonitor};

  EnsureReadCompleted();

  aPerms.Clear();
  if (NS_WARN_IF(XRE_IsContentProcess())) {
    return false;
  }

  for (const PermissionHashKey& entry : mPermissionTable) {
    nsAutoCString permissionKey;
    if (aOrigin.IsEmpty()) {
      // We can't check for individual OA strip perms here.
      // Don't force strip origin attributes.
      GetKeyForOrigin(entry.GetKey()->mOrigin, false, false, permissionKey);

      // If the keys don't match, and we aren't getting the default "" key, then
      // we can exit early. We have to keep looking if we're getting the default
      // key, as we may see a preload permission which should be transmitted.
      if (aKey != permissionKey && !aKey.IsEmpty()) {
        continue;
      }
    } else if (aOrigin != entry.GetKey()->mOrigin) {
      // If the origins don't match, then we can exit early. We have to keep
      // looking if we're getting the default origin, as we may see a preload
      // permission which should be transmitted.
      continue;
    }

    for (const auto& permEntry : entry.GetPermissions()) {
      // Given how "default" permissions work and the possibility of them
      // being overridden with UNKNOWN_ACTION, we might see this value here -
      // but we do not want to send it to the content process.
      if (permEntry.mPermission == nsIPermissionManager::UNKNOWN_ACTION) {
        continue;
      }

      bool isPreload = IsPreloadPermission(mTypeArray[permEntry.mType]);
      bool shouldAppend;
      if (aOrigin.IsEmpty()) {
        shouldAppend = (isPreload && aKey.IsEmpty()) ||
                       (!isPreload && aKey == permissionKey);
      } else {
        shouldAppend = (!isPreload && aOrigin == entry.GetKey()->mOrigin);
      }
      if (shouldAppend) {
        aPerms.AppendElement(
            IPC::Permission(entry.GetKey()->mOrigin,
                            mTypeArray[permEntry.mType], permEntry.mPermission,
                            permEntry.mExpireType, permEntry.mExpireTime));
      }
    }
  }

  return true;
}

void PermissionManager::SetPermissionsWithKey(
    const nsACString& aPermissionKey, nsTArray<IPC::Permission>& aPerms) {
  if (NS_WARN_IF(XRE_IsParentProcess())) {
    return;
  }

  MonitorAutoLock lock{mMonitor};

  RefPtr<GenericNonExclusivePromise::Private> promise;
  bool foundKey =
      mPermissionKeyPromiseMap.Get(aPermissionKey, getter_AddRefs(promise));
  if (promise) {
    MOZ_ASSERT(foundKey);
    // NOTE: This will resolve asynchronously, so we can mark it as resolved
    // now, and be confident that we will have filled in the database before any
    // callbacks run.
    promise->Resolve(true, __func__);
  } else if (foundKey) {
    // NOTE: We shouldn't be sent two InitializePermissionsWithKey for the same
    // key, but it's possible.
    return;
  }
  mPermissionKeyPromiseMap.InsertOrUpdate(
      aPermissionKey, RefPtr<GenericNonExclusivePromise::Private>{});

  // Add the permissions locally to our process
  for (IPC::Permission& perm : aPerms) {
    nsCOMPtr<nsIPrincipal> principal;
    nsresult rv =
        GetPrincipalFromOrigin(perm.origin, IsOAForceStripPermission(perm.type),
                               getter_AddRefs(principal));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      continue;
    }

#ifdef DEBUG
    nsAutoCString permissionKey;
    GetKeyForPermission(principal, perm.type, permissionKey);
    MOZ_ASSERT(permissionKey == aPermissionKey,
               "The permission keys which were sent over should match!");
#endif

    // The child process doesn't care about modification times - it neither
    // reads nor writes, nor removes them based on the date - so 0 (which
    // will end up as now()) is fine.
    uint64_t modificationTime = 0;
    AddInternal(principal, perm.type, perm.capability, 0, perm.expireType,
                perm.expireTime, modificationTime, eDontNotify, eNoDBOperation);
  }
}

/* static */
nsresult PermissionManager::GetKeyForOrigin(const nsACString& aOrigin,
                                            bool aForceStripOA,
                                            bool aSiteScopePermissions,
                                            nsACString& aKey) {
  aKey.Truncate();

  // We only key origins for http, https URIs. All origins begin with
  // the URL which they apply to, which means that they should begin with their
  // scheme in the case where they are one of these interesting URIs. We don't
  // want to actually parse the URL here however, because this can be called on
  // hot paths.
  if (!StringBeginsWith(aOrigin, "http:"_ns) &&
      !StringBeginsWith(aOrigin, "https:"_ns)) {
    return NS_OK;
  }

  // We need to look at the originAttributes if they are present, to make sure
  // to remove any which we don't want. We put the rest of the origin, not
  // including the attributes, into the key.
  OriginAttributes attrs;
  if (!attrs.PopulateFromOrigin(aOrigin, aKey)) {
    aKey.Truncate();
    return NS_OK;
  }

  MaybeStripOriginAttributes(aForceStripOA, attrs);

#ifdef DEBUG
  // Parse the origin string into a principal, and extract some useful
  // information from it for assertions.
  nsCOMPtr<nsIPrincipal> dbgPrincipal;
  MOZ_ALWAYS_SUCCEEDS(GetPrincipalFromOrigin(aOrigin, aForceStripOA,
                                             getter_AddRefs(dbgPrincipal)));
  MOZ_ASSERT(dbgPrincipal->SchemeIs("http") || dbgPrincipal->SchemeIs("https"));
  MOZ_ASSERT(dbgPrincipal->OriginAttributesRef() == attrs);
#endif

  // If it is needed, turn the origin into its site-origin
  if (aSiteScopePermissions) {
    nsCOMPtr<nsIURI> uri;
    nsresult rv = NS_NewURI(getter_AddRefs(uri), aKey);
    if (!NS_WARN_IF(NS_FAILED(rv))) {
      nsCString site;
      nsCOMPtr<nsIEffectiveTLDService> etld =
          mozilla::components::EffectiveTLD::Service();
      rv = etld->GetSite(uri, site);
      if (!NS_WARN_IF(NS_FAILED(rv))) {
        aKey = site;
      }
    }
  }

  // Append the stripped suffix to the output origin key.
  nsAutoCString suffix;
  attrs.CreateSuffix(suffix);
  aKey.Append(suffix);

  return NS_OK;
}

/* static */
nsresult PermissionManager::GetKeyForPrincipal(nsIPrincipal* aPrincipal,
                                               bool aForceStripOA,
                                               bool aSiteScopePermissions,
                                               nsACString& aKey) {
  nsAutoCString origin;
  nsresult rv = aPrincipal->GetOrigin(origin);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    aKey.Truncate();
    return rv;
  }
  return GetKeyForOrigin(origin, aForceStripOA, aSiteScopePermissions, aKey);
}

/* static */
nsresult PermissionManager::GetKeyForPermission(nsIPrincipal* aPrincipal,
                                                const nsACString& aType,
                                                nsACString& aKey) {
  // Preload permissions have the "" key.
  if (IsPreloadPermission(aType)) {
    aKey.Truncate();
    return NS_OK;
  }

  return GetKeyForPrincipal(aPrincipal, IsOAForceStripPermission(aType),
                            IsSiteScopedPermission(aType), aKey);
}

/* static */
nsTArray<std::pair<nsCString, nsCString>>
PermissionManager::GetAllKeysForPrincipal(nsIPrincipal* aPrincipal) {
  MOZ_ASSERT(aPrincipal);

  nsTArray<std::pair<nsCString, nsCString>> pairs;
  nsCOMPtr<nsIPrincipal> prin = aPrincipal;

  while (prin) {
    // Add the pair to the list
    std::pair<nsCString, nsCString>* pair =
        pairs.AppendElement(std::make_pair(""_ns, ""_ns));
    // We can't check for individual OA strip perms here.
    // Don't force strip origin attributes.
    GetKeyForPrincipal(prin, false, false, pair->first);

    // On origins with a derived key set to an empty string
    // (basically any non-web URI scheme), we want to make sure
    // to return earlier, and leave [("", "")] as the resulting
    // pairs (but still run the same debug assertions near the
    // end of this method).
    if (pair->first.IsEmpty()) {
      break;
    }

    Unused << GetOriginFromPrincipal(prin, false, pair->second);
    prin = prin->GetNextSubDomainPrincipal();
    // Get the next subdomain principal and loop back around.
  }

  MOZ_ASSERT(pairs.Length() >= 1,
             "Every principal should have at least one pair item.");
  return pairs;
}

bool PermissionManager::PermissionAvailable(nsIPrincipal* aPrincipal,
                                            const nsACString& aType) {
  MonitorAutoLock lock{mMonitor};
  return PermissionAvailableInternal(aPrincipal, aType);
}

bool PermissionManager::PermissionAvailableInternal(nsIPrincipal* aPrincipal,
                                                    const nsACString& aType) {
  EnsureReadCompleted();

  if (XRE_IsContentProcess()) {
    nsAutoCString permissionKey;
    // NOTE: GetKeyForPermission accepts a null aType.
    GetKeyForPermission(aPrincipal, aType, permissionKey);

    // If we have a pending promise for the permission key in question, we don't
    // have the permission available, so report a warning and return false.
    RefPtr<GenericNonExclusivePromise::Private> promise;
    if (!mPermissionKeyPromiseMap.Get(permissionKey, getter_AddRefs(promise)) ||
        promise) {
      // Emit a useful diagnostic warning with the permissionKey for the process
      // which hasn't received permissions yet.
      NS_WARNING(nsPrintfCString("This content process hasn't received the "
                                 "permissions for %s yet",
                                 permissionKey.get())
                     .get());
      return false;
    }
  }
  return true;
}

void PermissionManager::WhenPermissionsAvailable(nsIPrincipal* aPrincipal,
                                                 nsIRunnable* aRunnable) {
  MOZ_ASSERT(aRunnable);

  if (!XRE_IsContentProcess()) {
    aRunnable->Run();
    return;
  }

  MonitorAutoLock lock{mMonitor};

  nsTArray<RefPtr<GenericNonExclusivePromise>> promises;
  for (auto& pair : GetAllKeysForPrincipal(aPrincipal)) {
    RefPtr<GenericNonExclusivePromise::Private> promise;
    if (!mPermissionKeyPromiseMap.Get(pair.first, getter_AddRefs(promise))) {
      // In this case we have found a permission which isn't available in the
      // content process and hasn't been requested yet. We need to create a new
      // promise, and send the request to the parent (if we have not already
      // done so).
      promise = new GenericNonExclusivePromise::Private(__func__);
      mPermissionKeyPromiseMap.InsertOrUpdate(pair.first, RefPtr{promise});
    }

    if (promise) {
      promises.AppendElement(std::move(promise));
    }
  }

  // If all of our permissions are available, immediately run the runnable. This
  // avoids any extra overhead during fetch interception which is performance
  // sensitive.
  if (promises.IsEmpty()) {
    aRunnable->Run();
    return;
  }

  auto* thread = AbstractThread::MainThread();

  RefPtr<nsIRunnable> runnable = aRunnable;
  GenericNonExclusivePromise::All(thread, promises)
      ->Then(
          thread, __func__, [runnable]() { runnable->Run(); },
          []() {
            NS_WARNING(
                "PermissionManager permission promise rejected. We're "
                "probably shutting down.");
          });
}

void PermissionManager::EnsureReadCompleted() {
  if (mState == eInitializing) {
    while (mState == eInitializing) {
      mMonitor.AssertCurrentThreadOwns();
      mMonitor.Wait();
    }
  }

  switch (mState) {
    case eInitializing:
      MOZ_CRASH("This state is impossible!");

    case eDBInitialized:
      // child processes transitions from eInitializing -> eReady
      ENSURE_NOT_CHILD_PROCESS_NORET;

      mState = eReady;

      CompleteMigrations();
      ImportLatestDefaults();
      CompleteRead();

      [[fallthrough]];

    case eReady:
      [[fallthrough]];

    case eClosed:
      break;

    default:
      MOZ_CRASH("Invalid state");
  }
}

already_AddRefed<nsIInputStream> PermissionManager::GetDefaultsInputStream() {
  MOZ_ASSERT(NS_IsMainThread());

  nsAutoCString defaultsURL;
  Preferences::GetCString(kDefaultsUrlPrefName, defaultsURL);
  if (defaultsURL.IsEmpty()) {  // == Don't use built-in permissions.
    return nullptr;
  }

  nsCOMPtr<nsIURI> defaultsURI;
  nsresult rv = NS_NewURI(getter_AddRefs(defaultsURI), defaultsURL);
  NS_ENSURE_SUCCESS(rv, nullptr);

  nsCOMPtr<nsIChannel> channel;
  rv = NS_NewChannel(getter_AddRefs(channel), defaultsURI,
                     nsContentUtils::GetSystemPrincipal(),
                     nsILoadInfo::SEC_ALLOW_CROSS_ORIGIN_SEC_CONTEXT_IS_NULL,
                     nsIContentPolicy::TYPE_OTHER);
  NS_ENSURE_SUCCESS(rv, nullptr);

  nsCOMPtr<nsIInputStream> inputStream;
  rv = channel->Open(getter_AddRefs(inputStream));
  NS_ENSURE_SUCCESS(rv, nullptr);

  return inputStream.forget();
}

void PermissionManager::ConsumeDefaultsInputStream(
    nsIInputStream* aInputStream, const MonitorAutoLock& aProofOfLock) {
  MOZ_ASSERT(!NS_IsMainThread());

  constexpr char kMatchTypeHost[] = "host";
  constexpr char kMatchTypeOrigin[] = "origin";

  mDefaultEntriesForImport.Clear();

  if (!aInputStream) {
    return;
  }

  nsresult rv;

  /* format is:
   * matchtype \t type \t permission \t host
   * Only "host" is supported for matchtype
   * type is a string that identifies the type of permission (e.g. "cookie")
   * permission is an integer between 1 and 15
   */

  // Ideally we'd do this with nsILineInputString, but this is called with an
  // nsIInputStream that comes from a resource:// URI, which doesn't support
  // that interface.  So NS_ReadLine to the rescue...
  nsLineBuffer<char> lineBuffer;
  nsCString line;
  bool isMore = true;
  do {
    rv = NS_ReadLine(aInputStream, &lineBuffer, line, &isMore);
    NS_ENSURE_SUCCESS_VOID(rv);

    if (line.IsEmpty() || line.First() == '#') {
      continue;
    }

    nsTArray<nsCString> lineArray;

    // Split the line at tabs
    ParseString(line, '\t', lineArray);

    if (lineArray.Length() != 4) {
      continue;
    }

    nsresult error = NS_OK;
    uint32_t permission = lineArray[2].ToInteger(&error);
    if (NS_FAILED(error)) {
      continue;
    }

    const nsCString& hostOrOrigin = lineArray[3];
    const nsCString& type = lineArray[1];

    if (lineArray[0].EqualsLiteral(kMatchTypeHost)) {
      UpgradeHostToOriginAndInsert(
          hostOrOrigin, type, permission, nsIPermissionManager::EXPIRE_NEVER, 0,
          0,
          [&](const nsACString& aOrigin, const nsCString& aType,
              uint32_t aPermission, uint32_t aExpireType, int64_t aExpireTime,
              int64_t aModificationTime) MOZ_REQUIRES(mMonitor) {
            AddDefaultEntryForImport(aOrigin, aType, aPermission, aProofOfLock);
            return NS_OK;
          });
    } else if (lineArray[0].EqualsLiteral(kMatchTypeOrigin)) {
      AddDefaultEntryForImport(hostOrOrigin, type, permission, aProofOfLock);
    } else {
      continue;
    }

  } while (isMore);
}

void PermissionManager::AddDefaultEntryForImport(
    const nsACString& aOrigin, const nsCString& aType, uint32_t aPermission,
    const MonitorAutoLock& aProofOfLock) {
  DefaultEntry* entry = mDefaultEntriesForImport.AppendElement();
  MOZ_ASSERT(entry);

  entry->mPermission = aPermission;
  entry->mOrigin = aOrigin;
  entry->mType = aType;
}

nsresult PermissionManager::ImportDefaultEntry(
    const DefaultEntry& aDefaultEntry) {
  nsCOMPtr<nsIPrincipal> principal;
  nsresult rv = GetPrincipalFromOrigin(
      aDefaultEntry.mOrigin, IsOAForceStripPermission(aDefaultEntry.mType),
      getter_AddRefs(principal));
  if (NS_FAILED(rv)) {
    NS_WARNING("Couldn't import an origin permission - malformed origin");
    return rv;
  }

  // the import file format doesn't handle modification times, so we use
  // 0, which AddInternal will convert to now()
  int64_t modificationTime = 0;

  rv = AddInternal(principal, aDefaultEntry.mType, aDefaultEntry.mPermission,
                   cIDPermissionIsDefault, nsIPermissionManager::EXPIRE_NEVER,
                   0, modificationTime, eDontNotify, eNoDBOperation);
  if (NS_FAILED(rv)) {
    NS_WARNING("There was a problem importing an origin permission");
    return rv;
  }

  if (StaticPrefs::permissions_isolateBy_privateBrowsing() &&
      !IsOAForceStripPermission(aDefaultEntry.mType)) {
    // Also import the permission for private browsing.
    OriginAttributes attrs = OriginAttributes(principal->OriginAttributesRef());
    attrs.mPrivateBrowsingId = 1;
    nsCOMPtr<nsIPrincipal> pbPrincipal =
        BasePrincipal::Cast(principal)->CloneForcingOriginAttributes(attrs);
    // May return nullptr if clone fails.
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    rv =
        AddInternal(pbPrincipal, aDefaultEntry.mType, aDefaultEntry.mPermission,
                    cIDPermissionIsDefault, nsIPermissionManager::EXPIRE_NEVER,
                    0, modificationTime, eDontNotify, eNoDBOperation);
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "There was a problem importing an origin permission for private "
          "browsing");
      return rv;
    }
  }

  return NS_OK;
}

// ImportLatestDefaults will import the latest default permissions read during
// the last DB initialization.
nsresult PermissionManager::ImportLatestDefaults() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mState == eReady);

  for (const DefaultEntry& entry : mDefaultEntriesForImport) {
    Unused << ImportDefaultEntry(entry);
  }

  return NS_OK;
}

/**
 * Perform the early steps of a permission check and determine whether we need
 * to call CommonTestPermissionInternal() for the actual permission check.
 *
 * @param aPrincipal optional principal argument to check the permission for,
 *                   can be nullptr if we aren't performing a principal-based
 *                   check.
 * @param aTypeIndex if the caller isn't sure what the index of the permission
 *                   type to check for is in the mTypeArray member variable,
 *                   it should pass -1, otherwise this would be the index of
 *                   the type inside mTypeArray.  This would only be something
 *                   other than -1 in recursive invocations of this function.
 * @param aType      the permission type to test.
 * @param aPermission out argument which will be a permission type that we
 *                    will return from this function once the function is
 *                    done.
 * @param aDefaultPermission the default permission to be used if we can't
 *                           determine the result of the permission check.
 * @param aDefaultPermissionIsValid whether the previous argument contains a
 *                                  valid value.
 * @param aExactHostMatch whether to look for the exact host name or also for
 *                        subdomains that can have the same permission.
 * @param aIncludingSession whether to include session permissions when
 *                          testing for the permission.
 */
PermissionManager::TestPreparationResult
PermissionManager::CommonPrepareToTestPermission(
    nsIPrincipal* aPrincipal, int32_t aTypeIndex, const nsACString& aType,
    uint32_t* aPermission, uint32_t aDefaultPermission,
    bool aDefaultPermissionIsValid, bool aExactHostMatch,
    bool aIncludingSession) {
  auto* basePrin = BasePrincipal::Cast(aPrincipal);
  if (basePrin && basePrin->IsSystemPrincipal()) {
    *aPermission = ALLOW_ACTION;
    return AsVariant(NS_OK);
  }

  EnsureReadCompleted();

  // For some permissions, query the default from a pref. We want to avoid
  // doing this for all permissions so that permissions can opt into having
  // the pref lookup overhead on each call.
  int32_t defaultPermission =
      aDefaultPermissionIsValid ? aDefaultPermission : UNKNOWN_ACTION;
  if (!aDefaultPermissionIsValid && HasDefaultPref(aType)) {
    Unused << mDefaultPrefBranch->GetIntPref(PromiseFlatCString(aType).get(),
                                             &defaultPermission);
    if (defaultPermission < 0 ||
        defaultPermission > nsIPermissionManager::MAX_VALID_ACTION) {
      defaultPermission = nsIPermissionManager::UNKNOWN_ACTION;
    }
  }

  // Set the default.
  *aPermission = defaultPermission;

  int32_t typeIndex =
      aTypeIndex == -1 ? GetTypeIndex(aType, false) : aTypeIndex;

  // For expanded principals, we want to iterate over the allowlist and see
  // if the permission is granted for any of them.
  if (basePrin && basePrin->Is<ExpandedPrincipal>()) {
    auto* ep = basePrin->As<ExpandedPrincipal>();
    for (const auto& prin : ep->AllowList()) {
      uint32_t perm;
      nsresult rv =
          CommonTestPermission(prin, typeIndex, aType, &perm, defaultPermission,
                               true, aExactHostMatch, aIncludingSession);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return AsVariant(rv);
      }

      if (perm == nsIPermissionManager::ALLOW_ACTION) {
        *aPermission = perm;
        return AsVariant(NS_OK);
      }
      if (perm == nsIPermissionManager::PROMPT_ACTION) {
        // Store it, but keep going to see if we can do better.
        *aPermission = perm;
      }
    }

    return AsVariant(NS_OK);
  }

  // If type == -1, the type isn't known, just signal that we are done.
  if (typeIndex == -1) {
    return AsVariant(NS_OK);
  }

  return AsVariant(typeIndex);
}

// If aTypeIndex is passed -1, we try to inder the type index from aType.
nsresult PermissionManager::CommonTestPermission(
    nsIPrincipal* aPrincipal, int32_t aTypeIndex, const nsACString& aType,
    uint32_t* aPermission, uint32_t aDefaultPermission,
    bool aDefaultPermissionIsValid, bool aExactHostMatch,
    bool aIncludingSession) {
  auto preparationResult = CommonPrepareToTestPermission(
      aPrincipal, aTypeIndex, aType, aPermission, aDefaultPermission,
      aDefaultPermissionIsValid, aExactHostMatch, aIncludingSession);
  if (preparationResult.is<nsresult>()) {
    return preparationResult.as<nsresult>();
  }

  return CommonTestPermissionInternal(
      aPrincipal, nullptr, nullptr, preparationResult.as<int32_t>(), aType,
      aPermission, aExactHostMatch, aIncludingSession);
}

// If aTypeIndex is passed -1, we try to inder the type index from aType.
nsresult PermissionManager::CommonTestPermission(
    nsIURI* aURI, int32_t aTypeIndex, const nsACString& aType,
    uint32_t* aPermission, uint32_t aDefaultPermission,
    bool aDefaultPermissionIsValid, bool aExactHostMatch,
    bool aIncludingSession) {
  auto preparationResult = CommonPrepareToTestPermission(
      nullptr, aTypeIndex, aType, aPermission, aDefaultPermission,
      aDefaultPermissionIsValid, aExactHostMatch, aIncludingSession);
  if (preparationResult.is<nsresult>()) {
    return preparationResult.as<nsresult>();
  }

  return CommonTestPermissionInternal(
      nullptr, aURI, nullptr, preparationResult.as<int32_t>(), aType,
      aPermission, aExactHostMatch, aIncludingSession);
}

nsresult PermissionManager::CommonTestPermission(
    nsIURI* aURI, const OriginAttributes* aOriginAttributes, int32_t aTypeIndex,
    const nsACString& aType, uint32_t* aPermission, uint32_t aDefaultPermission,
    bool aDefaultPermissionIsValid, bool aExactHostMatch,
    bool aIncludingSession) {
  auto preparationResult = CommonPrepareToTestPermission(
      nullptr, aTypeIndex, aType, aPermission, aDefaultPermission,
      aDefaultPermissionIsValid, aExactHostMatch, aIncludingSession);
  if (preparationResult.is<nsresult>()) {
    return preparationResult.as<nsresult>();
  }

  return CommonTestPermissionInternal(
      nullptr, aURI, aOriginAttributes, preparationResult.as<int32_t>(), aType,
      aPermission, aExactHostMatch, aIncludingSession);
}

nsresult PermissionManager::TestPermissionWithoutDefaultsFromPrincipal(
    nsIPrincipal* aPrincipal, const nsACString& aType, uint32_t* aPermission) {
  MOZ_ASSERT(!HasDefaultPref(aType));

  MonitorAutoLock lock{mMonitor};
  return CommonTestPermission(aPrincipal, -1, aType, aPermission,
                              nsIPermissionManager::UNKNOWN_ACTION, true, false,
                              true);
}

void PermissionManager::FinishAsyncShutdown() {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIAsyncShutdownClient> asc = GetAsyncShutdownBarrier();
  MOZ_ASSERT(asc);

  DebugOnly<nsresult> rv = asc->RemoveBlocker(this);
  MOZ_ASSERT(NS_SUCCEEDED(rv));

  // Now we can safely release our holder.
  StaticMutexAutoLock lock(sCreationMutex);
  MOZ_ASSERT(sInstanceDead);
  if (sInstanceHolder) {
    sInstanceHolder = nullptr;
  }
}

// Async shutdown blocker methods

NS_IMETHODIMP PermissionManager::GetName(nsAString& aName) {
  aName = u"PermissionManager: Flushing data"_ns;
  return NS_OK;
}

NS_IMETHODIMP PermissionManager::BlockShutdown(
    nsIAsyncShutdownClient* aClient) {
  {
    // From now on we do not allow to capture new references to our singleton.
    StaticMutexAutoLock lock(sCreationMutex);
    sInstanceDead = true;
  }

  MonitorAutoLock lock{mMonitor};

  RemoveIdleDailyMaintenanceJob();
  RemoveAllFromMemory();
  // CloseDB does async work and will call FinishAsyncShutdown once done.
  CloseDB(eShutdown);
  return NS_OK;
}

NS_IMETHODIMP
PermissionManager::GetState(nsIPropertyBag** aBagOut) {
  nsCOMPtr<nsIWritablePropertyBag2> propertyBag =
      do_CreateInstance("@mozilla.org/hash-property-bag;1");

  nsresult rv = propertyBag->SetPropertyAsInt32(u"state"_ns, mState);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  propertyBag.forget(aBagOut);

  return NS_OK;
}

nsCOMPtr<nsIAsyncShutdownClient> PermissionManager::GetAsyncShutdownBarrier()
    const {
  nsresult rv;
  nsCOMPtr<nsIAsyncShutdownService> svc =
      do_GetService("@mozilla.org/async-shutdown-service;1", &rv);
  if (NS_FAILED(rv)) {
    return nullptr;
  }

  nsCOMPtr<nsIAsyncShutdownClient> client;
  // This feels very late but there seem to be other services that rely on
  // us later than "profile-before-change".
  rv = svc->GetXpcomWillShutdown(getter_AddRefs(client));
  MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));

  return client;
}

void PermissionManager::MaybeStripOriginAttributes(
    bool aForceStrip, OriginAttributes& aOriginAttributes) {
  uint32_t flags = 0;

  if (aForceStrip || !StaticPrefs::permissions_isolateBy_privateBrowsing()) {
    flags |= OriginAttributes::STRIP_PRIVATE_BROWSING_ID;
  }

  if (aForceStrip || !StaticPrefs::permissions_isolateBy_userContext()) {
    flags |= OriginAttributes::STRIP_USER_CONTEXT_ID;
  }

  if (flags != 0) {
    aOriginAttributes.StripAttributes(flags);
  }
}

}  // namespace mozilla
