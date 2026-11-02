/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CookieService.h"

#include "CookieCommons.h"
#include "CookieDummyStorage.h"
#include "CookieLogging.h"
#include "CookieParser.h"
#include "CookieValidation.h"
#include "ThirdPartyUtil.h"
#include "mozIThirdPartyUtil.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Components.h"
#include "mozilla/ConsoleReportCollector.h"
#include "mozilla/ContentBlockingNotifier.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/StoragePrincipalHelper.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/Promise-inl.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/dom/nsMixedContentBlocker.h"
#include "mozilla/net/CookieJarSettings.h"
#include "mozilla/net/CookiePersistentStorage.h"
#include "mozilla/net/CookiePrivateStorage.h"
#include "mozilla/net/CookieService.h"
#include "mozilla/net/CookieServiceChild.h"
#include "mozilla/net/HttpBaseChannel.h"
#include "mozilla/net/NeckoCommon.h"
#include "nsIConsoleReportCollector.h"
#include "nsICookiePermission.h"
#include "nsIEffectiveTLDService.h"
#include "nsIScriptError.h"
#include "nsIScriptSecurityManager.h"
#include "nsIURI.h"
#include "nsIWebProgressListener.h"
#include "nsNetUtil.h"
#include "xpcpublic.h"

using namespace mozilla::dom;

namespace {

uint32_t MakeCookieBehavior(uint32_t aCookieBehavior) {
  bool isFirstPartyIsolated = OriginAttributes::IsFirstPartyEnabled();

  if (isFirstPartyIsolated &&
      aCookieBehavior == nsICookieService::BEHAVIOR_PARTITION_FOREIGN) {
    return nsICookieService::BEHAVIOR_REJECT_TRACKER;
  }
  return aCookieBehavior;
}

/*
 Enables sanitizeOnShutdown cleaning prefs and disables the
 network.cookie.lifetimePolicy
*/
void MigrateCookieLifetimePrefs() {
  // Former network.cookie.lifetimePolicy values ACCEPT_SESSION/ACCEPT_NORMALLY
  // are not available anymore 2 = ACCEPT_SESSION
  if (mozilla::Preferences::GetInt("network.cookie.lifetimePolicy") != 2) {
    return;
  }
  if (!mozilla::Preferences::GetBool("privacy.sanitize.sanitizeOnShutdown")) {
    mozilla::Preferences::SetBool("privacy.sanitize.sanitizeOnShutdown", true);
    // To avoid clearing categories that the user did not intend to clear
    mozilla::Preferences::SetBool("privacy.clearOnShutdown.history", false);
    mozilla::Preferences::SetBool("privacy.clearOnShutdown.formdata", false);
    mozilla::Preferences::SetBool("privacy.clearOnShutdown.downloads", false);
    mozilla::Preferences::SetBool("privacy.clearOnShutdown.sessions", false);
    mozilla::Preferences::SetBool("privacy.clearOnShutdown.siteSettings",
                                  false);

    // We will migrate the new clear on shutdown prefs to align both sets of
    // prefs incase the user has not migrated yet. We don't have a new sessions
    // prefs, as it was merged into cookiesAndStorage as part of the effort for
    // the clear data revamp Bug 1853996
    mozilla::Preferences::SetBool(
        "privacy.clearOnShutdown_v2.browsingHistoryAndDownloads", false);
    mozilla::Preferences::SetBool("privacy.clearOnShutdown_v2.siteSettings",
                                  false);
  }
  mozilla::Preferences::SetBool("privacy.clearOnShutdown.cookies", true);
  mozilla::Preferences::SetBool("privacy.clearOnShutdown.cache", true);
  mozilla::Preferences::SetBool("privacy.clearOnShutdown.offlineApps", true);

  // Migrate the new clear on shutdown prefs
  mozilla::Preferences::SetBool("privacy.clearOnShutdown_v2.cookiesAndStorage",
                                true);
  mozilla::Preferences::SetBool("privacy.clearOnShutdown_v2.cache", true);
  mozilla::Preferences::ClearUser("network.cookie.lifetimePolicy");
}

}  // anonymous namespace

// static
uint32_t nsICookieManager::GetCookieBehavior(bool aIsPrivate) {
  if (aIsPrivate) {
    // To sync the cookieBehavior pref between regular and private mode in ETP
    // custom mode, we will return the regular cookieBehavior pref for private
    // mode when
    //   1. The regular cookieBehavior pref has a non-default value.
    //   2. And the private cookieBehavior pref has a default value.
    // Also, this can cover the migration case where the user has a non-default
    // cookieBehavior before the private cookieBehavior was introduced. The
    // getter here will directly return the regular cookieBehavior, so that the
    // cookieBehavior for private mode is consistent.
    if (mozilla::Preferences::HasUserValue(
            "network.cookie.cookieBehavior.pbmode")) {
      return MakeCookieBehavior(
          mozilla::StaticPrefs::network_cookie_cookieBehavior_pbmode());
    }

    if (mozilla::Preferences::HasUserValue("network.cookie.cookieBehavior")) {
      return MakeCookieBehavior(
          mozilla::StaticPrefs::network_cookie_cookieBehavior());
    }

    return MakeCookieBehavior(
        mozilla::StaticPrefs::network_cookie_cookieBehavior_pbmode());
  }
  return MakeCookieBehavior(
      mozilla::StaticPrefs::network_cookie_cookieBehavior());
}

namespace mozilla {
namespace net {

/******************************************************************************
 * CookieService impl:
 * useful types & constants
 ******************************************************************************/

static StaticRefPtr<CookieService> gCookieService;

namespace {

// Return false if the cookie should be ignored for the current channel.
bool ProcessSameSiteCookieForForeignRequest(nsIChannel* aChannel,
                                            Cookie* aCookie,
                                            bool aIsSafeTopLevelNav,
                                            bool aHadCrossSiteRedirects,
                                            bool aLaxByDefault) {
  // If it's a cross-site request and the cookie is same site only (strict)
  // don't send it.
  if (aCookie->SameSite() == nsICookie::SAMESITE_STRICT) {
    return false;
  }

  // Explicit SameSite=None cookies are always processed. When laxByDefault
  // is OFF then so are default cookies.
  if (aCookie->SameSite() == nsICookie::SAMESITE_NONE ||
      (!aLaxByDefault && aCookie->SameSite() == nsICookie::SAMESITE_UNSET)) {
    return true;
  }

  // Lax-by-default cookies are processed even with an intermediate
  // cross-site redirect (they are treated like aIsSameSiteForeign = false).
  if (aLaxByDefault && aCookie->SameSite() == nsICookie::SAMESITE_UNSET &&
      aHadCrossSiteRedirects &&
      StaticPrefs::
          network_cookie_sameSite_laxByDefault_allowBoomerangRedirect()) {
    return true;
  }

  int64_t currentTimeInUsec = PR_Now();

  // 2 minutes of tolerance for 'SameSite=Lax by default' for cookies set
  // without a SameSite value when used for unsafe http methods.
  if (aLaxByDefault && aCookie->SameSite() == nsICookie::SAMESITE_UNSET &&
      StaticPrefs::network_cookie_sameSite_laxPlusPOST_timeout() > 0 &&
      currentTimeInUsec - aCookie->UpdateTimeInUSec() <=
          (StaticPrefs::network_cookie_sameSite_laxPlusPOST_timeout() *
           PR_USEC_PER_SEC) &&
      !NS_IsSafeMethodNav(aChannel)) {
    return true;
  }

  MOZ_ASSERT(
      (aLaxByDefault && aCookie->SameSite() == nsICookie::SAMESITE_UNSET) ||
      aCookie->SameSite() == nsICookie::SAMESITE_LAX);
  // We only have SameSite=Lax or lax-by-default cookies at this point.  These
  // are processed only if it's a top-level navigation
  return aIsSafeTopLevelNav;
}

}  // namespace

/******************************************************************************
 * CookieService impl:
 * singleton instance ctor/dtor methods
 ******************************************************************************/

already_AddRefed<nsICookieService> CookieService::GetXPCOMSingleton() {
  if (IsNeckoChild()) {
    return CookieServiceChild::GetSingleton();
  }

  return GetSingleton();
}

already_AddRefed<CookieService> CookieService::GetSingleton() {
  NS_ASSERTION(!IsNeckoChild(), "not a parent process");

  if (gCookieService) {
    return do_AddRef(gCookieService);
  }

  // Create a new singleton CookieService.
  // We AddRef only once since XPCOM has rules about the ordering of module
  // teardowns - by the time our module destructor is called, it's too late to
  // Release our members (e.g. nsIObserverService and nsIPrefBranch), since GC
  // cycles have already been completed and would result in serious leaks.
  // See bug 209571.
  // TODO: Verify what is the earliest point in time during shutdown where
  // we can deny the creation of the CookieService as a whole.
  gCookieService = new CookieService();
  if (gCookieService) {
    if (NS_SUCCEEDED(gCookieService->Init())) {
      ClearOnShutdown(&gCookieService);
    } else {
      gCookieService = nullptr;
    }
  }

  return do_AddRef(gCookieService);
}

/******************************************************************************
 * CookieService impl:
 * public methods
 ******************************************************************************/

NS_IMPL_ISUPPORTS(CookieService, nsICookieService, nsICookieManager,
                  nsIObserver, nsISupportsWeakReference, nsIMemoryReporter)

CookieService::CookieService() = default;

nsresult CookieService::Init() {
  nsresult rv;
  mTLDService = mozilla::components::EffectiveTLD::Service(&rv);
  NS_ENSURE_SUCCESS(rv, rv);

  mThirdPartyUtil = mozilla::components::ThirdPartyUtil::Service();
  NS_ENSURE_SUCCESS(rv, rv);

  // Init our default, and possibly private CookieStorages.
  InitCookieStorages();

  // Migrate network.cookie.lifetimePolicy pref to sanitizeOnShutdown prefs
  MigrateCookieLifetimePrefs();

  RegisterWeakMemoryReporter(this);

  nsCOMPtr<nsIObserverService> os = services::GetObserverService();
  NS_ENSURE_STATE(os);
  os->AddObserver(this, "last-pb-context-exited", true);
  os->AddObserver(this, "browser-delayed-startup-finished", true);

  RunOnShutdown(
      [self = RefPtr{this}] { self->RetirePersistentStorageForShutdown(); },
      ShutdownPhase::AppShutdown);

  return NS_OK;
}

void CookieService::InitCookieStorages() {
  NS_ASSERTION(!mPersistentStorage, "already have a default CookieStorage");
  NS_ASSERTION(!mPrivateStorage, "already have a private CookieStorage");

  if (MOZ_UNLIKELY(AppShutdown::IsInOrBeyond(ShutdownPhase::AppShutdown))) {
    mPersistentStorage = new CookieDummyStorage();
  } else if (MOZ_UNLIKELY(StaticPrefs::network_cookie_noPersistentStorage())) {
    mPersistentStorage = CookiePrivateStorage::Create();
  } else {
    mPersistentStorage = CookiePersistentStorage::Create();
  }

  mPrivateStorage = CookiePrivateStorage::Create();
}

void CookieService::RetirePersistentStorageForShutdown() {
  MOZ_ASSERT(NS_IsMainThread());
  if (mPersistentStorage) {
    mRetiredStorage = std::move(mPersistentStorage);
    mPersistentStorage = new CookieDummyStorage();
  }
}

void CookieService::CloseCookieStorages() {
  // return if we already closed
  if (!mPersistentStorage) {
    return;
  }

  // Let's nullify both storages before calling Close().
  RefPtr<CookieStorage> privateStorage;
  privateStorage.swap(mPrivateStorage);

  RefPtr<CookieStorage> persistentStorage;
  persistentStorage.swap(mPersistentStorage);

  RefPtr<CookieStorage> retiredStorage;
  retiredStorage.swap(mRetiredStorage);

  privateStorage->Close();
  persistentStorage->Close();
  if (retiredStorage) {
    retiredStorage->Close();
  }
}

CookieService::~CookieService() {
  CloseCookieStorages();

  UnregisterWeakMemoryReporter(this);

  gCookieService = nullptr;
}

NS_IMETHODIMP
CookieService::Observe(nsISupports* /*aSubject*/, const char* aTopic,
                       const char16_t* /*aData*/) {
  if (!strcmp(aTopic, "browser-delayed-startup-finished")) {
    mThirdPartyCookieBlockingExceptions.Initialize();

    RunOnShutdown([self = RefPtr{this}] {
      self->mThirdPartyCookieBlockingExceptions.Shutdown();
    });
  } else if (!strcmp(aTopic, "last-pb-context-exited")) {
    // Flush all the cookies stored by private browsing contexts
    OriginAttributesPattern pattern;
    pattern.mPrivateBrowsingId.Construct(1);
    RemoveCookiesWithOriginAttributes(pattern, ""_ns);
    mPrivateStorage = CookiePrivateStorage::Create();
  }

  return NS_OK;
}

NS_IMETHODIMP
CookieService::TestCloseCookieDB() {
  if (!xpc::IsInAutomation()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  CloseCookieStorages();
  return NS_OK;
}

NS_IMETHODIMP
CookieService::TestOpenCookieDB() {
  MOZ_ASSERT(!mPersistentStorage, "shouldn't have a default CookieStorage");
  MOZ_ASSERT(!mPrivateStorage, "shouldn't have a private CookieStorage");
  InitCookieStorages();
  return NS_OK;
}

NS_IMETHODIMP
CookieService::GetCookieBehavior(bool aIsPrivate, uint32_t* aCookieBehavior) {
  NS_ENSURE_ARG_POINTER(aCookieBehavior);
  *aCookieBehavior = nsICookieManager::GetCookieBehavior(aIsPrivate);
  return NS_OK;
}

NS_IMETHODIMP
CookieService::GetCookieStringFromHttp(nsIURI* aHostURI, nsIChannel* aChannel,
                                       nsACString& aCookieString) {
  NS_ENSURE_ARG(aHostURI);
  NS_ENSURE_ARG(aChannel);

  aCookieString.Truncate();

  if (!CookieCommons::IsSchemeSupported(aHostURI)) {
    return NS_OK;
  }

  uint32_t rejectedReason = 0;
  ThirdPartyAnalysisResult result = mThirdPartyUtil->AnalyzeChannel(
      aChannel, false, aHostURI, nullptr, &rejectedReason);

  bool isSafeTopLevelNav = CookieCommons::IsSafeTopLevelNav(aChannel);
  bool hadCrossSiteRedirects = false;
  bool isSameSiteForeign = CookieCommons::IsSameSiteForeign(
      aChannel, aHostURI, &hadCrossSiteRedirects);

  OriginAttributes storageOriginAttributes;
  StoragePrincipalHelper::GetOriginAttributes(
      aChannel, storageOriginAttributes,
      StoragePrincipalHelper::eStorageAccessPrincipal);

  nsTArray<OriginAttributes> originAttributesList;
  originAttributesList.AppendElement(storageOriginAttributes);

  // CHIPS - If CHIPS is enabled the partitioned cookie jar is always available
  // (and therefore the partitioned OriginAttributes), the unpartitioned cookie
  // jar is only available in first-party or third-party with storageAccess
  // contexts.
  nsCOMPtr<nsICookieJarSettings> cookieJarSettings =
      CookieCommons::GetCookieJarSettings(aChannel);
  bool isCHIPS = StaticPrefs::network_cookie_CHIPS_enabled() &&
                 !cookieJarSettings->GetBlockingAllContexts();
  bool isUnpartitioned =
      !result.contains(ThirdPartyAnalysis::IsForeign) ||
      result.contains(ThirdPartyAnalysis::IsStorageAccessPermissionGranted);
  if (isCHIPS && isUnpartitioned) {
    // Assert that the storage originAttributes is empty. In other words,
    // it's unpartitioned.
    MOZ_ASSERT(storageOriginAttributes.mPartitionKey.IsEmpty());
    // Add the partitioned principal to principals
    OriginAttributes partitionedOriginAttributes;
    StoragePrincipalHelper::GetOriginAttributes(
        aChannel, partitionedOriginAttributes,
        StoragePrincipalHelper::ePartitionedPrincipal);
    // Only append the partitioned originAttributes if the partitionKey is set.
    // The partitionKey could be empty for partitionKey in partitioned
    // originAttributes if the channel is for privilege request, such as
    // extension's requests.
    if (!partitionedOriginAttributes.mPartitionKey.IsEmpty()) {
      originAttributesList.AppendElement(partitionedOriginAttributes);
    }
  }

  AutoTArray<RefPtr<Cookie>, 8> foundCookieList;
  GetCookiesForURI(
      aHostURI, aChannel, result.contains(ThirdPartyAnalysis::IsForeign),
      result.contains(ThirdPartyAnalysis::IsThirdPartyTrackingResource),
      result.contains(ThirdPartyAnalysis::IsThirdPartySocialTrackingResource),
      result.contains(ThirdPartyAnalysis::IsStorageAccessPermissionGranted),
      rejectedReason, isSafeTopLevelNav, isSameSiteForeign,
      hadCrossSiteRedirects, true, false, originAttributesList,
      foundCookieList);

  CookieCommons::ComposeCookieString(foundCookieList, aCookieString);

  if (!aCookieString.IsEmpty()) {
    COOKIE_LOGSUCCESS(GET_COOKIE, aHostURI, aCookieString, nullptr, false);
  }
  return NS_OK;
}

NS_IMETHODIMP
CookieService::SetCookieStringFromHttp(nsIURI* aHostURI,
                                       const nsACString& aCookieHeader,
                                       nsIChannel* aChannel) {
  NS_ENSURE_ARG(aHostURI);
  NS_ENSURE_ARG(aChannel);

  if (!IsInitialized()) {
    return NS_OK;
  }

  if (!CookieCommons::IsSchemeSupported(aHostURI)) {
    return NS_OK;
  }

  uint32_t rejectedReason = 0;
  ThirdPartyAnalysisResult result = mThirdPartyUtil->AnalyzeChannel(
      aChannel, false, aHostURI, nullptr, &rejectedReason);

  OriginAttributes storagePrincipalOriginAttributes;
  StoragePrincipalHelper::GetOriginAttributes(
      aChannel, storagePrincipalOriginAttributes,
      StoragePrincipalHelper::eStorageAccessPrincipal);

  // get the base domain for the host URI.
  // e.g. for "www.bbc.co.uk", this would be "bbc.co.uk".
  // file:// URI's (i.e. with an empty host) are allowed, but any other
  // scheme must have a non-empty host. A trailing dot in the host
  // is acceptable.
  bool requireHostMatch;
  nsAutoCString baseDomain;
  nsresult rv = CookieCommons::GetBaseDomain(mTLDService, aHostURI, baseDomain,
                                             requireHostMatch);
  if (NS_FAILED(rv)) {
    COOKIE_LOGFAILURE(SET_COOKIE, aHostURI, aCookieHeader,
                      "couldn't get base domain from URI");
    return NS_OK;
  }

  nsCOMPtr<nsICookieJarSettings> cookieJarSettings =
      CookieCommons::GetCookieJarSettings(aChannel);

  nsAutoCString hostFromURI;
  nsContentUtils::GetHostOrIPv6WithBrackets(aHostURI, hostFromURI);

  nsAutoCString baseDomainFromURI;
  rv = CookieCommons::GetBaseDomainFromHost(mTLDService, hostFromURI,
                                            baseDomainFromURI);
  NS_ENSURE_SUCCESS(rv, NS_OK);

  CookieStorage* storage = PickStorage(storagePrincipalOriginAttributes);

  // check default prefs
  bool hasExistingCookies =
      HasExistingCookies(baseDomainFromURI, storagePrincipalOriginAttributes);

  nsCOMPtr<nsIConsoleReportCollector> crc = do_QueryInterface(aChannel);

  CookieStatus cookieStatus = CheckPrefs(
      crc, cookieJarSettings, aHostURI,
      result.contains(ThirdPartyAnalysis::IsForeign),
      result.contains(ThirdPartyAnalysis::IsThirdPartyTrackingResource),
      result.contains(ThirdPartyAnalysis::IsThirdPartySocialTrackingResource),
      result.contains(ThirdPartyAnalysis::IsStorageAccessPermissionGranted),
      aCookieHeader, hasExistingCookies, storagePrincipalOriginAttributes,
      &rejectedReason);

  MOZ_ASSERT_IF(
      rejectedReason &&
          rejectedReason !=
              nsIWebProgressListener::STATE_COOKIES_PARTITIONED_TRACKER,
      cookieStatus == STATUS_REJECTED);

  // fire a notification if third party or if cookie was rejected
  // (but not if there was an error)
  switch (cookieStatus) {
    case STATUS_REJECTED:
      CookieCommons::NotifyRejected(aHostURI, aChannel, rejectedReason,
                                    OPERATION_WRITE);
      return NS_OK;  // Stop here
    case STATUS_REJECTED_WITH_ERROR:
      CookieCommons::NotifyRejected(aHostURI, aChannel, rejectedReason,
                                    OPERATION_WRITE);
      return NS_OK;
    case STATUS_ACCEPTED:  // Fallthrough
    case STATUS_ACCEPT_SESSION:
      NotifyAccepted(aChannel, rejectedReason);

      // Notify the content blocking event if tracker cookies are partitioned.
      if (rejectedReason ==
          nsIWebProgressListener::STATE_COOKIES_PARTITIONED_TRACKER) {
        ContentBlockingNotifier::OnDecision(
            aChannel, ContentBlockingNotifier::BlockingDecision::eBlock,
            rejectedReason);
      }
      break;
    default:
      break;
  }

  bool addonAllowsLoad = false;
  nsCOMPtr<nsIURI> channelURI;
  NS_GetFinalChannelURI(aChannel, getter_AddRefs(channelURI));
  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  addonAllowsLoad = BasePrincipal::Cast(loadInfo->TriggeringPrincipal())
                        ->AddonAllowsLoad(channelURI);

  bool isForeignAndNotAddon = false;
  if (!addonAllowsLoad) {
    mThirdPartyUtil->IsThirdPartyChannel(aChannel, aHostURI,
                                         &isForeignAndNotAddon);

    // include sub-document navigations from cross-site to same-site
    // wrt top-level in our check for thirdparty-ness
    if (StaticPrefs::network_cookie_sameSite_crossSiteIframeSetCheck() &&
        !isForeignAndNotAddon &&
        loadInfo->GetExternalContentPolicyType() ==
            ExtContentPolicy::TYPE_SUBDOCUMENT) {
      bool triggeringPrincipalIsThirdParty = false;
      BasePrincipal::Cast(loadInfo->TriggeringPrincipal())
          ->IsThirdPartyURI(channelURI, &triggeringPrincipalIsThirdParty);
      isForeignAndNotAddon |= triggeringPrincipalIsThirdParty;
    }
  }

  bool mustBePartitioned =
      isForeignAndNotAddon &&
      cookieJarSettings->GetCookieBehavior() ==
          nsICookieService::BEHAVIOR_PARTITION_FOREIGN &&
      !result.contains(ThirdPartyAnalysis::IsStorageAccessPermissionGranted);

  nsCString cookieHeader(aCookieHeader);

  // CHIPS - The partitioned cookie jar is always available and it is always
  // possible to store cookies in it using the "Partitioned" attribute.
  // Prepare the partitioned principals OAs to enable possible partitioned
  // cookie storing from first-party or with StorageAccess.
  // Similar behavior to CookieServiceChild::SetCookieStringFromHttp().
  OriginAttributes partitionedPrincipalOriginAttributes;
  bool isPartitionedPrincipal =
      !storagePrincipalOriginAttributes.mPartitionKey.IsEmpty();
  bool isCHIPS = StaticPrefs::network_cookie_CHIPS_enabled() &&
                 !cookieJarSettings->GetBlockingAllContexts();
  // Only need to get OAs if we don't already use the partitioned principal.
  if (isCHIPS && !isPartitionedPrincipal) {
    StoragePrincipalHelper::GetOriginAttributes(
        aChannel, partitionedPrincipalOriginAttributes,
        StoragePrincipalHelper::ePartitionedPrincipal);
  }

  nsAutoCString dateHeader;
  CookieCommons::GetServerDateHeader(aChannel, dateHeader);

  int64_t currentTimeInUsec =
      CookieCommons::GetCurrentTimeInUSecFromChannel(aChannel);

  // process each cookie in the header
  CookieParser cookieParser(crc, aHostURI);

  cookieParser.Parse(baseDomain, requireHostMatch, cookieStatus, cookieHeader,
                     dateHeader, true, isForeignAndNotAddon, mustBePartitioned,
                     storagePrincipalOriginAttributes.IsPrivateBrowsing(),
                     loadInfo->GetIsOn3PCBExceptionList(), currentTimeInUsec);

  if (!cookieParser.ContainsCookie()) {
    return NS_OK;
  }

  // check permissions from site permission list.
  if (!CookieCommons::CheckCookiePermission(aChannel,
                                            cookieParser.CookieData())) {
    COOKIE_LOGFAILURE(SET_COOKIE, aHostURI, aCookieHeader,
                      "cookie rejected by permission manager");
    CookieCommons::NotifyRejected(
        aHostURI, aChannel,
        nsIWebProgressListener::STATE_COOKIES_BLOCKED_BY_PERMISSION,
        OPERATION_WRITE);
    cookieParser.RejectCookie(CookieParser::RejectedByPermissionManager);
    return NS_OK;
  }

  // CHIPS - If the partitioned attribute is set, store cookie in partitioned
  // cookie jar independent of context. If the cookies are stored in the
  // partitioned cookie jar anyway no special treatment of CHIPS cookies
  // necessary.
  bool needPartitioned = isCHIPS && cookieParser.CookieData().isPartitioned() &&
                         !isPartitionedPrincipal;
  OriginAttributes& cookieOriginAttributes =
      needPartitioned ? partitionedPrincipalOriginAttributes
                      : storagePrincipalOriginAttributes;
  // Assert that partitionedPrincipalOriginAttributes are initialized if used.
  MOZ_ASSERT_IF(needPartitioned,
                !partitionedPrincipalOriginAttributes.mPartitionKey.IsEmpty());

  // create a new Cookie
  RefPtr<Cookie> cookie =
      Cookie::Create(cookieParser.CookieData(), cookieOriginAttributes);
  MOZ_ASSERT(cookie);

  cookie->SetLastAccessedInUSec(currentTimeInUsec);
  cookie->SetCreationTimeInUSec(
      Cookie::GenerateUniqueCreationTimeInUSec(currentTimeInUsec));
  cookie->SetUpdateTimeInUSec(cookie->CreationTimeInUSec());

  // Use TargetBrowsingContext to also take frame loads into account.
  RefPtr<BrowsingContext> bc = loadInfo->GetTargetBrowsingContext();

  // add the cookie to the list. AddCookie() takes care of logging.
  storage->AddCookie(&cookieParser, baseDomain, cookieOriginAttributes, cookie,
                     currentTimeInUsec, aHostURI, aCookieHeader, true,
                     isForeignAndNotAddon, bc);

  return NS_OK;
}

void CookieService::NotifyAccepted(nsIChannel* aChannel,
                                   uint32_t aRejectedReason) {
  ContentBlockingNotifier::OnDecision(
      aChannel, ContentBlockingNotifier::BlockingDecision::eAllow,
      aRejectedReason);
}

/******************************************************************************
 * nsICookieManager impl:
 * nsICookieManager
 ******************************************************************************/

NS_IMETHODIMP
CookieService::RemoveAll() {
  if (!IsInitialized()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  mPersistentStorage->EnsureInitialized();
  mPersistentStorage->RemoveAll();
  return NS_OK;
}

NS_IMETHODIMP
CookieService::GetCookies(nsTArray<RefPtr<nsICookie>>& aCookies) {
  if (!IsInitialized()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  mPersistentStorage->EnsureInitialized();

  // We expose only non-private cookies.
  mPersistentStorage->GetAll(aCookies);

  return NS_OK;
}

NS_IMETHODIMP
CookieService::GetSessionCookies(nsTArray<RefPtr<nsICookie>>& aCookies) {
  if (!IsInitialized()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  mPersistentStorage->EnsureInitialized();

  // We expose only non-private cookies.
  mPersistentStorage->GetSessionCookies(aCookies);

  return NS_OK;
}

NS_IMETHODIMP
CookieService::Add(const nsACString& aHost, const nsACString& aPath,
                   const nsACString& aName, const nsACString& aValue,
                   bool aIsSecure, bool aIsHttpOnly, bool aIsSession,
                   int64_t aExpiry, JS::Handle<JS::Value> aOriginAttributes,
                   int32_t aSameSite, nsICookie::schemeType aSchemeMap,
                   bool aIsPartitioned, JSContext* aCx,
                   nsICookieValidation** aValidation) {
  NS_ENSURE_ARG_POINTER(aCx);
  NS_ENSURE_ARG_POINTER(aValidation);

  OriginAttributes attrs;

  if (!aOriginAttributes.isObject() || !attrs.Init(aCx, aOriginAttributes)) {
    return NS_ERROR_INVALID_ARG;
  }

  nsCOMPtr<nsICookieValidation> validation;
  nsresult rv = AddInternal(nullptr, aHost, aPath, aName, aValue, aIsSecure,
                            aIsHttpOnly, aIsSession, aExpiry, &attrs, aSameSite,
                            aSchemeMap, aIsPartitioned, /* from-http: */ true,
                            nullptr, getter_AddRefs(validation));
  if (rv != NS_ERROR_ILLEGAL_VALUE || !validation ||
      CookieValidation::Cast(validation)->Result() ==
          nsICookieValidation::eOK) {
    validation.forget(aValidation);
    return rv;
  }

  validation.forget(aValidation);
  return NS_OK;
}

NS_IMETHODIMP_(nsresult)
CookieService::AddNative(nsIURI* aCookieURI, const nsACString& aHost,
                         const nsACString& aPath, const nsACString& aName,
                         const nsACString& aValue, bool aIsSecure,
                         bool aIsHttpOnly, bool aIsSession, int64_t aExpiry,
                         OriginAttributes* aOriginAttributes, int32_t aSameSite,
                         nsICookie::schemeType aSchemeMap, bool aIsPartitioned,
                         bool aFromHttp, const nsID* aOperationID,
                         nsICookieValidation** aValidation) {
  return AddInternal(aCookieURI, aHost, aPath, aName, aValue, aIsSecure,
                     aIsHttpOnly, aIsSession, aExpiry, aOriginAttributes,
                     aSameSite, aSchemeMap, aIsPartitioned, aFromHttp,
                     aOperationID, aValidation);
}

nsresult CookieService::AddInternal(
    nsIURI* aCookieURI, const nsACString& aHost, const nsACString& aPath,
    const nsACString& aName, const nsACString& aValue, bool aIsSecure,
    bool aIsHttpOnly, bool aIsSession, int64_t aExpiry,
    OriginAttributes* aOriginAttributes, int32_t aSameSite,
    nsICookie::schemeType aSchemeMap, bool aIsPartitioned, bool aFromHttp,
    const nsID* aOperationID, nsICookieValidation** aValidation) {
  NS_ENSURE_ARG_POINTER(aValidation);

  if (NS_WARN_IF(!aOriginAttributes)) {
    return NS_ERROR_FAILURE;
  }

  if (!IsInitialized()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  // first, normalize the hostname, and fail if it contains illegal characters.
  nsAutoCString host(aHost);
  nsresult rv = NormalizeHost(host);
  NS_ENSURE_SUCCESS(rv, rv);

  // get the base domain for the host URI.
  // e.g. for "www.bbc.co.uk", this would be "bbc.co.uk".
  nsAutoCString baseDomain;
  rv = CookieCommons::GetBaseDomainFromHost(mTLDService, host, baseDomain);
  NS_ENSURE_SUCCESS(rv, rv);

  int64_t currentTimeInUsec = PR_Now();
  int64_t uniqueCreationTimeInUSec =
      Cookie::GenerateUniqueCreationTimeInUSec(currentTimeInUsec);

  CookieStruct cookieData(nsCString(aName), nsCString(aValue), host,
                          nsCString(aPath), aExpiry, currentTimeInUsec,
                          uniqueCreationTimeInUSec, uniqueCreationTimeInUSec,
                          aIsHttpOnly, aIsSession, aIsSecure, aIsPartitioned,
                          aSameSite, aSchemeMap);

  RefPtr<CookieValidation> cv = CookieValidation::Validate(cookieData);

  if (cv->Result() != nsICookieValidation::eOK) {
    cv.forget(aValidation);
    return NS_ERROR_ILLEGAL_VALUE;
  }

  RefPtr<Cookie> cookie = Cookie::Create(cookieData, *aOriginAttributes);
  MOZ_ASSERT(cookie);

  CookieStorage* storage = PickStorage(*aOriginAttributes);
  storage->AddCookie(nullptr, baseDomain, *aOriginAttributes, cookie,
                     currentTimeInUsec, aCookieURI, VoidCString(), aFromHttp,
                     !aOriginAttributes->mPartitionKey.IsEmpty(), nullptr,
                     aOperationID);

  cv.forget(aValidation);
  return NS_OK;
}

nsresult CookieService::Remove(const nsACString& aHost,
                               const OriginAttributes& aAttrs,
                               const nsACString& aName, const nsACString& aPath,
                               bool aFromHttp, const nsID* aOperationID) {
  // first, normalize the hostname, and fail if it contains illegal characters.
  nsAutoCString host(aHost);
  nsresult rv = NormalizeHost(host);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString baseDomain;
  if (!host.IsEmpty()) {
    rv = CookieCommons::GetBaseDomainFromHost(mTLDService, host, baseDomain);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (!IsInitialized()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  CookieStorage* storage = PickStorage(aAttrs);
  storage->RemoveCookie(baseDomain, aAttrs, host, PromiseFlatCString(aName),
                        PromiseFlatCString(aPath), aFromHttp, aOperationID);

  return NS_OK;
}

NS_IMETHODIMP
CookieService::Remove(const nsACString& aHost, const nsACString& aName,
                      const nsACString& aPath,
                      JS::Handle<JS::Value> aOriginAttributes, JSContext* aCx) {
  OriginAttributes attrs;

  if (!aOriginAttributes.isObject() || !attrs.Init(aCx, aOriginAttributes)) {
    return NS_ERROR_INVALID_ARG;
  }

  return RemoveNative(aHost, aName, aPath, &attrs, /* from http: */ true,
                      nullptr);
}

NS_IMETHODIMP_(nsresult)
CookieService::RemoveNative(const nsACString& aHost, const nsACString& aName,
                            const nsACString& aPath,
                            OriginAttributes* aOriginAttributes, bool aFromHttp,
                            const nsID* aOperationID) {
  if (NS_WARN_IF(!aOriginAttributes)) {
    return NS_ERROR_FAILURE;
  }

  nsresult rv =
      Remove(aHost, *aOriginAttributes, aName, aPath, aFromHttp, aOperationID);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

void CookieService::GetCookiesForURI(
    nsIURI* aHostURI, nsIChannel* aChannel, bool aIsForeign,
    bool aIsThirdPartyTrackingResource,
    bool aIsThirdPartySocialTrackingResource,
    bool aStorageAccessPermissionGranted, uint32_t aRejectedReason,
    bool aIsSafeTopLevelNav, bool aIsSameSiteForeign,
    bool aHadCrossSiteRedirects, bool aHttpBound,
    bool aAllowSecureCookiesToInsecureOrigin,
    const nsTArray<OriginAttributes>& aOriginAttrsList,
    nsTArray<RefPtr<Cookie>>& aCookieList) {
  NS_ASSERTION(aHostURI, "null host!");

  uint32_t acceptedReason = 0;

  if (!CookieCommons::IsSchemeSupported(aHostURI)) {
    return;
  }

  if (!IsInitialized()) {
    return;
  }

  nsCOMPtr<nsICookieJarSettings> cookieJarSettings =
      CookieCommons::GetCookieJarSettings(aChannel);

  nsCOMPtr<nsIConsoleReportCollector> crc = do_QueryInterface(aChannel);

  nsCOMPtr<nsILoadInfo> loadInfo = aChannel ? aChannel->LoadInfo() : nullptr;
  const bool on3pcdException = loadInfo && loadInfo->GetIsOn3PCBExceptionList();

  // When both partitioned and unpartitioned jars are queried (CHIPS enabled
  // in an unpartitioned context), only CHIPS cookies (those with the
  // Partitioned attribute) should be returned from the partitioned jar.
  bool hasBothPartitionedAndUnpartitioned = false;
  if (aOriginAttrsList.Length() > 1) {
    bool hasUnpartitioned = false;
    bool hasPartitioned = false;
    for (const auto& a : aOriginAttrsList) {
      if (a.mPartitionKey.IsEmpty()) {
        hasUnpartitioned = true;
      } else {
        hasPartitioned = true;
      }
    }
    hasBothPartitionedAndUnpartitioned = hasUnpartitioned && hasPartitioned;
  }

  for (const auto& attrs : aOriginAttrsList) {
    CookieStorage* storage = PickStorage(attrs);

    // get the base domain, host, and path from the URI.
    // e.g. for "www.bbc.co.uk", the base domain would be "bbc.co.uk".
    // file:// URI's (i.e. with an empty host) are allowed, but any other
    // scheme must have a non-empty host. A trailing dot in the host
    // is acceptable.
    bool requireHostMatch;
    nsAutoCString baseDomain;
    nsAutoCString hostFromURI;
    nsAutoCString pathFromURI;
    nsresult rv = CookieCommons::GetBaseDomain(mTLDService, aHostURI,
                                               baseDomain, requireHostMatch);
    if (NS_SUCCEEDED(rv)) {
      rv = nsContentUtils::GetHostOrIPv6WithBrackets(aHostURI, hostFromURI);
    }
    if (NS_SUCCEEDED(rv)) {
      rv = aHostURI->GetFilePath(pathFromURI);
    }
    if (NS_FAILED(rv)) {
      COOKIE_LOGFAILURE(GET_COOKIE, aHostURI, VoidCString(),
                        "invalid host/path from URI");
      return;
    }

    nsAutoCString normalizedHostFromURI(hostFromURI);
    rv = NormalizeHost(normalizedHostFromURI);
    NS_ENSURE_SUCCESS_VOID(rv);

    nsAutoCString baseDomainFromURI;
    rv = CookieCommons::GetBaseDomainFromHost(
        mTLDService, normalizedHostFromURI, baseDomainFromURI);
    NS_ENSURE_SUCCESS_VOID(rv);

    // check default prefs
    uint32_t rejectedReason = aRejectedReason;
    bool hasExistingCookies = HasExistingCookies(baseDomainFromURI, attrs);

    CookieStatus cookieStatus = CheckPrefs(
        crc, cookieJarSettings, aHostURI, aIsForeign,
        aIsThirdPartyTrackingResource, aIsThirdPartySocialTrackingResource,
        aStorageAccessPermissionGranted, VoidCString(), hasExistingCookies,
        attrs, &rejectedReason);

    MOZ_ASSERT_IF(
        rejectedReason &&
            rejectedReason !=
                nsIWebProgressListener::STATE_COOKIES_PARTITIONED_TRACKER,
        cookieStatus == STATUS_REJECTED);

    // for GetCookie(), we only fire acceptance/rejection notifications
    // (but not if there was an error)
    switch (cookieStatus) {
      case STATUS_REJECTED:
        // If we don't have any cookies from this host, fail silently.
        if (hasExistingCookies) {
          CookieCommons::NotifyRejected(aHostURI, aChannel, rejectedReason,
                                        OPERATION_READ);
        }
        return;
      default:
        break;
    }
    acceptedReason = rejectedReason;

    // Note: The following permissions logic is mirrored in
    // extensions::MatchPattern::MatchesCookie.
    // If it changes, please update that function, or file a bug for someone
    // else to do so.

    // check if aHostURI is using an https secure protocol.
    // if it isn't, then we can't send a secure cookie over the connection.
    // if SchemeIs fails, assume an insecure connection, to be on the safe side
    bool potentiallyTrustworthy =
        nsMixedContentBlocker::IsPotentiallyTrustworthyOrigin(aHostURI);

    int64_t currentTimeInUsec = PR_Now();
    bool stale = false;

    nsTArray<RefPtr<Cookie>> cookies;
    storage->GetCookiesFromHost(baseDomain, attrs, cookies);
    if (cookies.IsEmpty()) {
      continue;
    }

    bool laxByDefault =
        StaticPrefs::network_cookie_sameSite_laxByDefault() &&
        !nsContentUtils::IsURIInPrefList(
            aHostURI, "network.cookie.sameSite.laxByDefault.disabledHosts");

    // iterate the cookies!
    for (Cookie* cookie : cookies) {
      // check the host, since the base domain lookup is conservative.
      if (!CookieCommons::DomainMatches(cookie, hostFromURI)) {
        continue;
      }

      // if the cookie is secure and the host scheme isn't, we avoid sending
      // cookie if possible. But for process synchronization purposes, we may
      // want the content process to know about the cookie (without it's value).
      // In which case we will wipe the value before sending
      if (cookie->IsSecure() && !potentiallyTrustworthy &&
          !aAllowSecureCookiesToInsecureOrigin) {
        continue;
      }

      // if the cookie is httpOnly and it's not going directly to the HTTP
      // connection, don't send it
      if (cookie->IsHttpOnly() && !aHttpBound) {
        continue;
      }

      // if the nsIURI path doesn't match the cookie path, don't send it back
      if (!CookieCommons::PathMatches(cookie, pathFromURI)) {
        continue;
      }

      // When both jars are queried, filter out non-CHIPS cookies from the
      // partitioned jar.
      if (!StaticPrefs::network_cookie_CHIPS_affectsTCP() &&
          hasBothPartitionedAndUnpartitioned &&
          !attrs.mPartitionKey.IsEmpty() && !cookie->RawIsPartitioned()) {
        continue;
      }

      // Check if we need to block the cookie because of opt-in partitioning.
      // We will only allow partitioned cookies with "partitioned" attribution
      // if opt-in partitioning is enabled.
      //
      // Note: If the cookie is on the 3pcd exception list, we will include
      // the cookie.
      if (aIsForeign && cookieJarSettings->GetPartitionForeign() &&
          (StaticPrefs::network_cookie_cookieBehavior_optInPartitioning() ||
           (attrs.IsPrivateBrowsing() &&
            StaticPrefs::
                network_cookie_cookieBehavior_optInPartitioning_pbmode())) &&
          !(cookie->IsPartitioned() && cookie->RawIsPartitioned()) &&
          !aStorageAccessPermissionGranted && !on3pcdException) {
        continue;
      }

      if (aHttpBound && aIsSameSiteForeign) {
        bool blockCookie = !ProcessSameSiteCookieForForeignRequest(
            aChannel, cookie, aIsSafeTopLevelNav, aHadCrossSiteRedirects,
            laxByDefault);

        if (blockCookie) {
          if (aHadCrossSiteRedirects) {
            CookieLogging::LogMessageToConsole(
                crc, aHostURI, nsIScriptError::warningFlag,
                CONSOLE_REJECTION_CATEGORY, "CookieBlockedCrossSiteRedirect"_ns,
                AutoTArray<nsString, 1>{
                    NS_ConvertUTF8toUTF16(cookie->Name()),
                });
          }
          continue;
        }
      }

      // all checks passed - add to list and check if lastAccessedInUSec stamp
      // needs updating
      aCookieList.AppendElement(cookie);
      if (cookie->IsStale()) {
        stale = true;
      }
    }

    if (aCookieList.IsEmpty()) {
      continue;
    }

    // update lastAccessedInUSec timestamps. we only do this if the timestamp is
    // stale by a certain amount, to avoid thrashing the db during pageload.
    if (stale) {
      storage->StaleCookies(aCookieList, currentTimeInUsec);
    }
  }

  if (aCookieList.IsEmpty()) {
    return;
  }

  // Send a notification about the acceptance of the cookies now that we found
  // some.
  NotifyAccepted(aChannel, acceptedReason);

  // return cookies in order of path length; longest to shortest.
  // this is required per RFC2109.  if cookies match in length,
  // then sort by creation time (see bug 236772).
  aCookieList.Sort(CompareCookiesForSending());
}

/******************************************************************************
 * CookieService impl:
 * private domain & permission compliance enforcement functions
 ******************************************************************************/

nsresult CookieService::NormalizeHost(nsCString& aHost) {
  nsAutoCString host;
  if (!CookieCommons::IsIPv6BaseDomain(aHost)) {
    nsresult rv = NS_DomainToASCII(aHost, host);
    if (NS_FAILED(rv)) {
      return rv;
    }
    aHost = std::move(host);
  }

  return NS_OK;
}

CookieStatus CookieService::CheckPrefs(
    nsIConsoleReportCollector* aCRC, nsICookieJarSettings* aCookieJarSettings,
    nsIURI* aHostURI, bool aIsForeign, bool aIsThirdPartyTrackingResource,
    bool aIsThirdPartySocialTrackingResource,
    bool aStorageAccessPermissionGranted, const nsACString& aCookieHeader,
    bool aHasExistingCookies, const OriginAttributes& aOriginAttrs,
    uint32_t* aRejectedReason) {
  nsresult rv;

  MOZ_ASSERT(aRejectedReason);

  *aRejectedReason = 0;

  // don't let unsupported scheme sites get/set cookies (could be a security
  // issue)
  if (!CookieCommons::IsSchemeSupported(aHostURI)) {
    COOKIE_LOGFAILURE(!aCookieHeader.IsVoid(), aHostURI, aCookieHeader,
                      "non http/https sites cannot read cookies");
    return STATUS_REJECTED_WITH_ERROR;
  }

  nsCOMPtr<nsIPrincipal> principal =
      BasePrincipal::CreateContentPrincipal(aHostURI, aOriginAttrs);

  if (!principal) {
    COOKIE_LOGFAILURE(!aCookieHeader.IsVoid(), aHostURI, aCookieHeader,
                      "non-content principals cannot get/set cookies");
    return STATUS_REJECTED_WITH_ERROR;
  }

  // check the permission list first; if we find an entry, it overrides
  // default prefs. see bug 184059.
  uint32_t cookiePermission = nsICookiePermission::ACCESS_DEFAULT;
  rv = aCookieJarSettings->CookiePermission(principal, &cookiePermission);
  if (NS_SUCCEEDED(rv)) {
    switch (cookiePermission) {
      case nsICookiePermission::ACCESS_DENY:
        COOKIE_LOGFAILURE(!aCookieHeader.IsVoid(), aHostURI, aCookieHeader,
                          "cookies are blocked for this site");
        CookieLogging::LogMessageToConsole(
            aCRC, aHostURI, nsIScriptError::warningFlag,
            CONSOLE_REJECTION_CATEGORY, "CookieRejectedByPermissionManager"_ns,
            AutoTArray<nsString, 1>{
                NS_ConvertUTF8toUTF16(aCookieHeader),
            });

        *aRejectedReason =
            nsIWebProgressListener::STATE_COOKIES_BLOCKED_BY_PERMISSION;
        return STATUS_REJECTED;

      case nsICookiePermission::ACCESS_ALLOW:
        return STATUS_ACCEPTED;
      default:
        break;
    }
  }

  // No cookies allowed if this request comes from a resource in a 3rd party
  // context, when anti-tracking protection is enabled and when we don't have
  // access to the first-party cookie jar.
  if (aIsForeign && aIsThirdPartyTrackingResource &&
      !aStorageAccessPermissionGranted &&
      aCookieJarSettings->GetRejectThirdPartyContexts()) {
    if (aCookieJarSettings->GetPartitionForeign()) {
      MOZ_ASSERT(!aOriginAttrs.mPartitionKey.IsEmpty(),
                 "We must have a StoragePrincipal here!");
      *aRejectedReason =
          nsIWebProgressListener::STATE_COOKIES_PARTITIONED_TRACKER;
      return STATUS_ACCEPTED;
    }

    COOKIE_LOGFAILURE(!aCookieHeader.IsVoid(), aHostURI, aCookieHeader,
                      "cookies are disabled in trackers");
    if (aIsThirdPartySocialTrackingResource) {
      *aRejectedReason =
          nsIWebProgressListener::STATE_COOKIES_BLOCKED_SOCIALTRACKER;
    } else {
      *aRejectedReason = nsIWebProgressListener::STATE_COOKIES_BLOCKED_TRACKER;
    }
    return STATUS_REJECTED;
  }

  // check default prefs.
  // Check aStorageAccessPermissionGranted when checking aCookieBehavior
  // so that we take things such as the content blocking allow list into
  // account.
  if (aCookieJarSettings->GetCookieBehavior() ==
          nsICookieService::BEHAVIOR_REJECT &&
      !aStorageAccessPermissionGranted) {
    COOKIE_LOGFAILURE(!aCookieHeader.IsVoid(), aHostURI, aCookieHeader,
                      "cookies are disabled");
    *aRejectedReason = nsIWebProgressListener::STATE_COOKIES_BLOCKED_ALL;
    return STATUS_REJECTED;
  }

  // check if cookie is foreign
  if (aIsForeign) {
    if (aCookieJarSettings->GetCookieBehavior() ==
            nsICookieService::BEHAVIOR_REJECT_FOREIGN &&
        !aStorageAccessPermissionGranted) {
      COOKIE_LOGFAILURE(!aCookieHeader.IsVoid(), aHostURI, aCookieHeader,
                        "context is third party");
      CookieLogging::LogMessageToConsole(
          aCRC, aHostURI, nsIScriptError::warningFlag,
          CONSOLE_REJECTION_CATEGORY, "CookieRejectedThirdParty"_ns,
          AutoTArray<nsString, 1>{
              NS_ConvertUTF8toUTF16(aCookieHeader),
          });
      *aRejectedReason = nsIWebProgressListener::STATE_COOKIES_BLOCKED_FOREIGN;
      return STATUS_REJECTED;
    }

    if (aCookieJarSettings->GetLimitForeignContexts() &&
        !aStorageAccessPermissionGranted && !aHasExistingCookies) {
      COOKIE_LOGFAILURE(!aCookieHeader.IsVoid(), aHostURI, aCookieHeader,
                        "context is third party");
      CookieLogging::LogMessageToConsole(
          aCRC, aHostURI, nsIScriptError::warningFlag,
          CONSOLE_REJECTION_CATEGORY, "CookieRejectedThirdParty"_ns,
          AutoTArray<nsString, 1>{
              NS_ConvertUTF8toUTF16(aCookieHeader),
          });
      *aRejectedReason = nsIWebProgressListener::STATE_COOKIES_BLOCKED_FOREIGN;
      return STATUS_REJECTED;
    }
  }

  // if nothing has complained, accept cookie
  return STATUS_ACCEPTED;
}

/******************************************************************************
 * CookieService impl:
 * private cookielist management functions
 ******************************************************************************/

// find whether a given cookie has been previously set. this is provided by the
// nsICookieManager interface.
NS_IMETHODIMP
CookieService::CookieExists(const nsACString& aHost, const nsACString& aPath,
                            const nsACString& aName,
                            JS::Handle<JS::Value> aOriginAttributes,
                            JSContext* aCx, bool* aFoundCookie) {
  NS_ENSURE_ARG_POINTER(aCx);
  NS_ENSURE_ARG_POINTER(aFoundCookie);

  OriginAttributes attrs;
  if (!aOriginAttributes.isObject() || !attrs.Init(aCx, aOriginAttributes)) {
    return NS_ERROR_INVALID_ARG;
  }
  return CookieExistsNative(aHost, aPath, aName, &attrs, aFoundCookie);
}

NS_IMETHODIMP_(nsresult)
CookieService::CookieExistsNative(const nsACString& aHost,
                                  const nsACString& aPath,
                                  const nsACString& aName,
                                  OriginAttributes* aOriginAttributes,
                                  bool* aFoundCookie) {
  nsCOMPtr<nsICookie> cookie;
  nsresult rv = GetCookieNative(aHost, aPath, aName, aOriginAttributes,
                                getter_AddRefs(cookie));
  NS_ENSURE_SUCCESS(rv, rv);

  *aFoundCookie = cookie != nullptr;

  return NS_OK;
}

NS_IMETHODIMP_(nsresult)
CookieService::GetCookieNative(const nsACString& aHost, const nsACString& aPath,
                               const nsACString& aName,
                               OriginAttributes* aOriginAttributes,
                               nsICookie** aCookie) {
  NS_ENSURE_ARG_POINTER(aOriginAttributes);
  NS_ENSURE_ARG_POINTER(aCookie);

  if (!IsInitialized()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsAutoCString baseDomain;
  nsresult rv =
      CookieCommons::GetBaseDomainFromHost(mTLDService, aHost, baseDomain);
  NS_ENSURE_SUCCESS(rv, rv);

  CookieStorage* storage = PickStorage(*aOriginAttributes);

  RefPtr<Cookie> cookie =
      storage->FindCookie(baseDomain, *aOriginAttributes, aHost, aName, aPath);
  cookie.forget(aCookie);

  return NS_OK;
}

NS_IMETHODIMP
CookieService::HasCookiesForSite(const nsACString& aHost,
                                 const nsAString& aPattern, bool* aResult) {
  NS_ENSURE_ARG_POINTER(aResult);
  *aResult = false;

  OriginAttributesPattern pattern;
  if (!pattern.Init(aPattern)) {
    return NS_ERROR_INVALID_ARG;
  }

  nsAutoCString host(aHost);
  nsresult rv = NormalizeHost(host);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString baseDomain;
  rv = CookieCommons::GetBaseDomainFromHost(mTLDService, host, baseDomain);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!IsInitialized()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  CookieStorage* storage = PickStorage(pattern);
  storage->EnsureInitialized();

  *aResult = storage->HasCookies(baseDomain, pattern);
  return NS_OK;
}

// get an enumerator of cookies stored by a particular host. this is provided by
// the nsICookieManager interface.
NS_IMETHODIMP
CookieService::GetCookiesFromHost(const nsACString& aHost,
                                  JS::Handle<JS::Value> aOriginAttributes,
                                  bool aSorted, JSContext* aCx,
                                  nsTArray<RefPtr<nsICookie>>& aResult) {
  OriginAttributes attrs;
  if (!aOriginAttributes.isObject() || !attrs.Init(aCx, aOriginAttributes)) {
    return NS_ERROR_INVALID_ARG;
  }

  return GetCookiesFromHostNative(aHost, &attrs, aSorted, aResult);
}

NS_IMETHODIMP
CookieService::GetCookiesFromHostNative(const nsACString& aHost,
                                        OriginAttributes* aAttrs, bool aSorted,
                                        nsTArray<RefPtr<nsICookie>>& aResult) {
  // first, normalize the hostname, and fail if it contains illegal characters.
  nsAutoCString host(aHost);
  nsresult rv = NormalizeHost(host);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString baseDomain;
  rv = CookieCommons::GetBaseDomainFromHost(mTLDService, host, baseDomain);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!IsInitialized()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  CookieStorage* storage = PickStorage(*aAttrs);

  nsTArray<RefPtr<Cookie>> cookies;
  storage->GetCookiesFromHost(baseDomain, *aAttrs, cookies);

  if (cookies.IsEmpty()) {
    return NS_OK;
  }

  aResult.SetCapacity(cookies.Length());
  for (Cookie* cookie : cookies) {
    aResult.AppendElement(cookie);
  }

  if (aSorted) {
    aResult.Sort(CompareCookiesForSending());
  }

  return NS_OK;
}

NS_IMETHODIMP
CookieService::GetCookiesWithOriginAttributes(
    const nsAString& aPattern, const nsACString& aHost, const bool aSorted,
    nsTArray<RefPtr<nsICookie>>& aResult) {
  OriginAttributesPattern pattern;
  if (!pattern.Init(aPattern)) {
    return NS_ERROR_INVALID_ARG;
  }

  nsAutoCString host(aHost);
  nsresult rv = NormalizeHost(host);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString baseDomain;
  rv = CookieCommons::GetBaseDomainFromHost(mTLDService, host, baseDomain);
  NS_ENSURE_SUCCESS(rv, rv);

  return GetCookiesWithOriginAttributes(pattern, baseDomain, aSorted, aResult);
}

nsresult CookieService::GetCookiesWithOriginAttributes(
    const OriginAttributesPattern& aPattern, const nsCString& aBaseDomain,
    bool aSorted, nsTArray<RefPtr<nsICookie>>& aResult) {
  CookieStorage* storage = PickStorage(aPattern);
  storage->GetCookiesWithOriginAttributes(aPattern, aBaseDomain, aSorted,
                                          aResult);

  return NS_OK;
}

NS_IMETHODIMP
CookieService::RemoveCookiesWithOriginAttributes(const nsAString& aPattern,
                                                 const nsACString& aHost) {
  MOZ_ASSERT(XRE_IsParentProcess());

  OriginAttributesPattern pattern;
  if (!pattern.Init(aPattern)) {
    return NS_ERROR_INVALID_ARG;
  }

  nsAutoCString host(aHost);
  nsresult rv = NormalizeHost(host);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString baseDomain;
  rv = CookieCommons::GetBaseDomainFromHost(mTLDService, host, baseDomain);
  NS_ENSURE_SUCCESS(rv, rv);

  return RemoveCookiesWithOriginAttributes(pattern, baseDomain);
}

nsresult CookieService::RemoveCookiesWithOriginAttributes(
    const OriginAttributesPattern& aPattern, const nsCString& aBaseDomain) {
  if (!IsInitialized()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  CookieStorage* storage = PickStorage(aPattern);
  storage->RemoveCookiesWithOriginAttributes(aPattern, aBaseDomain);

  return NS_OK;
}

NS_IMETHODIMP
CookieService::RemoveCookiesFromExactHost(const nsACString& aHost,
                                          const nsAString& aPattern) {
  MOZ_ASSERT(XRE_IsParentProcess());

  OriginAttributesPattern pattern;
  if (!pattern.Init(aPattern)) {
    return NS_ERROR_INVALID_ARG;
  }

  return RemoveCookiesFromExactHost(aHost, pattern);
}

nsresult CookieService::RemoveCookiesFromExactHost(
    const nsACString& aHost, const OriginAttributesPattern& aPattern) {
  nsAutoCString host(aHost);
  nsresult rv = NormalizeHost(host);
  NS_ENSURE_SUCCESS(rv, rv);

  nsAutoCString baseDomain;
  rv = CookieCommons::GetBaseDomainFromHost(mTLDService, host, baseDomain);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!IsInitialized()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  CookieStorage* storage = PickStorage(aPattern);
  storage->RemoveCookiesFromExactHost(aHost, baseDomain, aPattern);

  return NS_OK;
}

namespace {

class RemoveAllSinceRunnable : public Runnable {
 public:
  using CookieArray = nsTArray<RefPtr<nsICookie>>;
  RemoveAllSinceRunnable(Promise* aPromise, CookieService* aSelf,
                         CookieArray&& aCookieArray, int64_t aSinceWhen)
      : Runnable("RemoveAllSinceRunnable"),
        mPromise(aPromise),
        mSelf(aSelf),
        mList(std::move(aCookieArray)),
        mIndex(0),
        mSinceWhen(aSinceWhen) {}

  NS_IMETHODIMP Run() override {
    RemoveSome();

    if (mIndex < mList.Length()) {
      return NS_DispatchToCurrentThread(this);
    }
    mPromise->MaybeResolveWithUndefined();

    return NS_OK;
  }

 private:
  void RemoveSome() {
    for (CookieArray::size_type iter = 0;
         iter < kYieldPeriod && mIndex < mList.Length(); ++mIndex, ++iter) {
      auto* cookie = static_cast<Cookie*>(mList[mIndex].get());
      if (cookie->CreationTimeInUSec() > mSinceWhen &&
          NS_FAILED(mSelf->Remove(cookie->Host(), cookie->OriginAttributesRef(),
                                  cookie->Name(), cookie->Path(),
                                  /* from http: */ true, nullptr))) {
        continue;
      }
    }
  }

 private:
  RefPtr<Promise> mPromise;
  RefPtr<CookieService> mSelf;
  CookieArray mList;
  CookieArray::size_type mIndex;
  int64_t mSinceWhen;
  static const CookieArray::size_type kYieldPeriod = 10;
};

}  // namespace

NS_IMETHODIMP
CookieService::RemoveAllSince(int64_t aSinceWhen, JSContext* aCx,
                              Promise** aRetVal) {
  nsIGlobalObject* globalObject = xpc::CurrentNativeGlobal(aCx);
  if (NS_WARN_IF(!globalObject)) {
    return NS_ERROR_UNEXPECTED;
  }

  ErrorResult result;
  RefPtr<Promise> promise = Promise::Create(globalObject, result);
  if (NS_WARN_IF(result.Failed())) {
    return result.StealNSResult();
  }

  mPersistentStorage->EnsureInitialized();

  nsTArray<RefPtr<nsICookie>> cookieList;

  // We delete only non-private cookies.
  mPersistentStorage->GetAll(cookieList,
                             CookieStorage::ExpiredCookies::Include);

  RefPtr<RemoveAllSinceRunnable> runMe = new RemoveAllSinceRunnable(
      promise, this, std::move(cookieList), aSinceWhen);

  promise.forget(aRetVal);

  return runMe->Run();
}

namespace {

class CompareCookiesCreationTime {
 public:
  static bool Equals(const nsICookie* aCookie1, const nsICookie* aCookie2) {
    return static_cast<const Cookie*>(aCookie1)->CreationTimeInUSec() ==
           static_cast<const Cookie*>(aCookie2)->CreationTimeInUSec();
  }

  static bool LessThan(const nsICookie* aCookie1, const nsICookie* aCookie2) {
    return static_cast<const Cookie*>(aCookie1)->CreationTimeInUSec() <
           static_cast<const Cookie*>(aCookie2)->CreationTimeInUSec();
  }
};

}  // namespace

NS_IMETHODIMP
CookieService::GetCookiesSince(int64_t aSinceWhen,
                               nsTArray<RefPtr<nsICookie>>& aResult) {
  if (!IsInitialized()) {
    return NS_OK;
  }

  mPersistentStorage->EnsureInitialized();

  // We expose only non-private cookies.
  nsTArray<RefPtr<nsICookie>> cookieList;
  mPersistentStorage->GetAll(cookieList);

  for (RefPtr<nsICookie>& cookie : cookieList) {
    if (static_cast<Cookie*>(cookie.get())->CreationTimeInUSec() >=
        aSinceWhen) {
      aResult.AppendElement(cookie);
    }
  }

  aResult.Sort(CompareCookiesCreationTime());
  return NS_OK;
}

size_t CookieService::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const {
  size_t n = aMallocSizeOf(this);

  if (mPersistentStorage) {
    n += mPersistentStorage->SizeOfIncludingThis(aMallocSizeOf);
  }
  if (mPrivateStorage) {
    n += mPrivateStorage->SizeOfIncludingThis(aMallocSizeOf);
  }

  return n;
}

MOZ_DEFINE_MALLOC_SIZE_OF(CookieServiceMallocSizeOf)

NS_IMETHODIMP
CookieService::CollectReports(nsIHandleReportCallback* aHandleReport,
                              nsISupports* aData, bool /*aAnonymize*/) {
  MOZ_COLLECT_REPORT("explicit/cookie-service", KIND_HEAP, UNITS_BYTES,
                     SizeOfIncludingThis(CookieServiceMallocSizeOf),
                     "Memory used by the cookie service.");

  return NS_OK;
}

bool CookieService::IsInitialized() const {
  if (!mPersistentStorage) {
    NS_WARNING("No CookieStorage! Profile already close?");
    return false;
  }

  MOZ_ASSERT(mPrivateStorage);
  return true;
}

CookieStorage* CookieService::PickStorage(const OriginAttributes& aAttrs) {
  MOZ_ASSERT(IsInitialized());

  if (aAttrs.IsPrivateBrowsing()) {
    return mPrivateStorage;
  }

  mPersistentStorage->EnsureInitialized();
  return mPersistentStorage;
}

CookieStorage* CookieService::PickStorage(
    const OriginAttributesPattern& aAttrs) {
  MOZ_ASSERT(IsInitialized());

  if (aAttrs.mPrivateBrowsingId.WasPassed() &&
      aAttrs.mPrivateBrowsingId.Value() > 0) {
    return mPrivateStorage;
  }

  mPersistentStorage->EnsureInitialized();
  return mPersistentStorage;
}

nsICookieValidation::ValidationError CookieService::SetCookiesFromIPC(
    const nsACString& aBaseDomain, const OriginAttributes& aAttrs,
    nsIURI* aHostURI, bool aIsThirdParty,
    const nsTArray<CookieStruct>& aCookies, BrowsingContext* aBrowsingContext) {
  if (!IsInitialized()) {
    // If we are probably shutting down, we can ignore this cookie.
    return nsICookieValidation::eOK;
  }

  CookieStorage* storage = PickStorage(aAttrs);
  int64_t currentTimeInUsec = PR_Now();

  for (const CookieStruct& cookieData : aCookies) {
    RefPtr<CookieValidation> validation = CookieValidation::ValidateForHost(
        cookieData, aHostURI, aBaseDomain, false, false);
    MOZ_ASSERT(validation);

    if (validation->Result() != nsICookieValidation::eOK) {
      return validation->Result();
    }

    // create a new Cookie and copy attributes
    RefPtr<Cookie> cookie = Cookie::Create(cookieData, aAttrs);
    if (!cookie) {
      continue;
    }

    cookie->SetLastAccessedInUSec(currentTimeInUsec);
    cookie->SetCreationTimeInUSec(
        Cookie::GenerateUniqueCreationTimeInUSec(currentTimeInUsec));
    cookie->SetUpdateTimeInUSec(cookie->CreationTimeInUSec());

    storage->AddCookie(nullptr, aBaseDomain, aAttrs, cookie, currentTimeInUsec,
                       aHostURI, ""_ns, false, aIsThirdParty, aBrowsingContext);
  }

  return nsICookieValidation::eOK;
}

void CookieService::GetCookiesFromHost(
    const nsACString& aBaseDomain, const OriginAttributes& aOriginAttributes,
    nsTArray<RefPtr<Cookie>>& aCookies) {
  if (!IsInitialized()) {
    return;
  }

  CookieStorage* storage = PickStorage(aOriginAttributes);
  storage->GetCookiesFromHost(aBaseDomain, aOriginAttributes, aCookies);
}

void CookieService::StaleCookies(const nsTArray<RefPtr<Cookie>>& aCookies,
                                 int64_t aCurrentTimeInUsec) {
  if (!IsInitialized()) {
    return;
  }

  if (aCookies.IsEmpty()) {
    return;
  }

  OriginAttributes originAttributes = aCookies[0]->OriginAttributesRef();
#ifdef MOZ_DEBUG
  for (Cookie* cookie : aCookies) {
    MOZ_ASSERT(originAttributes == cookie->OriginAttributesRef());
  }
#endif

  CookieStorage* storage = PickStorage(originAttributes);
  storage->StaleCookies(aCookies, aCurrentTimeInUsec);
}

bool CookieService::HasExistingCookies(
    const nsACString& aBaseDomain, const OriginAttributes& aOriginAttributes) {
  if (!IsInitialized()) {
    return false;
  }

  CookieStorage* storage = PickStorage(aOriginAttributes);
  return storage->HasCookies(aBaseDomain, aOriginAttributes);
}

void CookieService::AddCookieFromDocument(
    CookieParser& aCookieParser, const nsACString& aBaseDomain,
    const OriginAttributes& aOriginAttributes, Cookie& aCookie,
    int64_t aCurrentTimeInUsec, nsIURI* aDocumentURI, bool aThirdParty,
    Document* aDocument) {
  MOZ_ASSERT(aDocumentURI);
  MOZ_ASSERT(aDocument);

  if (!IsInitialized()) {
    return;
  }

  nsAutoCString cookieString;
  aCookieParser.GetCookieString(cookieString);

  PickStorage(aOriginAttributes)
      ->AddCookie(&aCookieParser, aBaseDomain, aOriginAttributes, &aCookie,
                  aCurrentTimeInUsec, aDocumentURI, cookieString, false,
                  aThirdParty, aDocument->GetBrowsingContext());
}

/* static */
void CookieService::Update3PCBExceptionInfo(nsIChannel* aChannel) {
  MOZ_ASSERT(aChannel);
  MOZ_ASSERT(XRE_IsParentProcess());

  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  RefPtr<CookieService> csSingleton = CookieService::GetSingleton();

  // Bail out if the channel is a top-level loading. The exception is only
  // applicable to third-party loading.
  if (loadInfo->GetExternalContentPolicyType() ==
      ExtContentPolicy::TYPE_DOCUMENT) {
    return;
  }

  // Bail out earlier if the 3PCB exception service is not initialized.
  if (!csSingleton->mThirdPartyCookieBlockingExceptions.IsInitialized()) {
    return;
  }

  // If the channel is triggered by a system principal, we don't need to do
  // anything because the channel is for loading system resources.
  if (loadInfo->TriggeringPrincipal()->IsSystemPrincipal()) {
    return;
  }

  bool isInExceptionList =
      csSingleton->mThirdPartyCookieBlockingExceptions.CheckExceptionForChannel(
          aChannel);

  (void)loadInfo->SetIsOn3PCBExceptionList(isInExceptionList);
}

NS_IMETHODIMP
CookieService::AddThirdPartyCookieBlockingExceptions(
    const nsTArray<RefPtr<nsIThirdPartyCookieExceptionEntry>>& aExceptions) {
  for (const auto& ex : aExceptions) {
    nsAutoCString exception;
    MOZ_ALWAYS_SUCCEEDS(ex->Serialize(exception));
    mThirdPartyCookieBlockingExceptions.Insert(exception);
  }

  return NS_OK;
}

NS_IMETHODIMP
CookieService::RemoveThirdPartyCookieBlockingExceptions(
    const nsTArray<RefPtr<nsIThirdPartyCookieExceptionEntry>>& aExceptions) {
  for (const auto& ex : aExceptions) {
    nsAutoCString exception;
    MOZ_ALWAYS_SUCCEEDS(ex->Serialize(exception));
    mThirdPartyCookieBlockingExceptions.Remove(exception);
  }

  return NS_OK;
}

NS_IMETHODIMP
CookieService::TestGet3PCBExceptions(nsTArray<nsCString>& aExceptions) {
  aExceptions.Clear();

  mThirdPartyCookieBlockingExceptions.GetExceptions(aExceptions);

  return NS_OK;
}

NS_IMETHODIMP
CookieService::MaybeCapExpiry(int64_t aExpiryInMSec, int64_t* aResult) {
  NS_ENSURE_ARG_POINTER(aResult);
  *aResult =
      CookieCommons::MaybeCapExpiry(PR_Now() / PR_USEC_PER_MSEC, aExpiryInMSec);
  return NS_OK;
}

}  // namespace net
}  // namespace mozilla
