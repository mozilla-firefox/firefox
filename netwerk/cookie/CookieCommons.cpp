/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CookieCommons.h"

#include "Cookie.h"
#include "CookieLogging.h"
#include "CookieParser.h"
#include "CookieService.h"
#include "ThirdPartyUtil.h"
#include "mozIThirdPartyUtil.h"
#include "mozilla/ContentBlockingNotifier.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/StorageAccess.h"
#include "mozilla/dom/CanonicalBrowsingContext.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "mozilla/dom/WorkerCommon.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/nsMixedContentBlocker.h"
#include "mozilla/net/CookieJarSettings.h"
#include "nsContentUtils.h"
#include "nsICookiePermission.h"
#include "nsICookieService.h"
#include "nsIEffectiveTLDService.h"
#include "nsIGlobalObject.h"
#include "nsIHttpChannel.h"
#include "nsIRedirectHistoryEntry.h"
#include "nsIWebProgressListener.h"
#include "nsNetUtil.h"
#include "nsReadableUtils.h"
#include "nsSandboxFlags.h"
#include "nsScriptSecurityManager.h"

namespace mozilla {

using dom::Document;

namespace net {

// static
bool CookieCommons::DomainMatches(Cookie* aCookie, const nsACString& aHost) {
  // first, check for an exact host or domain cookie match, e.g. "google.com"
  // or ".google.com"; second a subdomain match, e.g.
  // host = "mail.google.com", cookie domain = ".google.com".
  return aCookie->RawHost() == aHost ||
         (aCookie->IsDomain() && StringEndsWith(aHost, aCookie->Host()));
}

// static
bool CookieCommons::PathMatches(Cookie* aCookie, const nsACString& aPath) {
  return PathMatches(aCookie->Path(), aPath);
}

// static
bool CookieCommons::PathMatches(const nsACString& aCookiePath,
                                const nsACString& aPath) {
  // if our cookie path is empty we can't really perform our prefix check, and
  // also we can't check the last character of the cookie path, so we would
  // never return a successful match.
  if (aCookiePath.IsEmpty()) {
    return false;
  }

  // if the cookie path and the request path are identical, they match.
  if (aCookiePath.Equals(aPath)) {
    return true;
  }

  // if the cookie path is a prefix of the request path, and the last character
  // of the cookie path is %x2F ("/"), they match.
  bool isPrefix = StringBeginsWith(aPath, aCookiePath);
  if (isPrefix && aCookiePath.Last() == '/') {
    return true;
  }

  // if the cookie path is a prefix of the request path, and the first character
  // of the request path that is not included in the cookie path is a %x2F ("/")
  // character, they match.
  uint32_t cookiePathLen = aCookiePath.Length();
  return isPrefix && aPath[cookiePathLen] == '/';
}

// Get the base domain for aHostURI; e.g. for "www.bbc.co.uk", this would be
// "bbc.co.uk". Only properly-formed URI's are tolerated, though a trailing
// dot may be present. If aHostURI is an IP address, an alias such as
// 'localhost', an eTLD such as 'co.uk', or the empty string, aBaseDomain will
// be the exact host, and aRequireHostMatch will be true to indicate that
// substring matches should not be performed.
nsresult CookieCommons::GetBaseDomain(nsIEffectiveTLDService* aTLDService,
                                      nsIURI* aHostURI, nsACString& aBaseDomain,
                                      bool& aRequireHostMatch) {
  // get the base domain. this will fail if the host contains a leading dot,
  // more than one trailing dot, or is otherwise malformed.
  nsresult rv = aTLDService->GetBaseDomain(aHostURI, 0, aBaseDomain);
  aRequireHostMatch = rv == NS_ERROR_HOST_IS_IP_ADDRESS ||
                      rv == NS_ERROR_INSUFFICIENT_DOMAIN_LEVELS;
  if (aRequireHostMatch) {
    // aHostURI is either an IP address, an alias such as 'localhost', an eTLD
    // such as 'co.uk', or the empty string. use the host as a key in such
    // cases.
    rv = nsContentUtils::GetHostOrIPv6WithBrackets(aHostURI, aBaseDomain);
  }
  NS_ENSURE_SUCCESS(rv, rv);

  // aHost (and thus aBaseDomain) may be the string '.'. If so, fail.
  if (aBaseDomain.Length() == 1 && aBaseDomain.Last() == '.') {
    return NS_ERROR_INVALID_ARG;
  }

  // block any URIs without a host that aren't file:// URIs.
  if (aBaseDomain.IsEmpty() && !aHostURI->SchemeIs("file")) {
    return NS_ERROR_INVALID_ARG;
  }

  return NS_OK;
}

nsresult CookieCommons::GetBaseDomain(nsIPrincipal* aPrincipal,
                                      nsACString& aBaseDomain) {
  MOZ_ASSERT(aPrincipal);

  // for historical reasons we use ascii host for file:// URLs.
  if (aPrincipal->SchemeIs("file")) {
    return nsContentUtils::GetHostOrIPv6WithBrackets(aPrincipal, aBaseDomain);
  }

  nsresult rv = aPrincipal->GetBaseDomain(aBaseDomain);
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsContentUtils::MaybeFixIPv6Host(aBaseDomain);
  return NS_OK;
}

// Get the base domain for aHost; e.g. for "www.bbc.co.uk", this would be
// "bbc.co.uk". This is done differently than GetBaseDomain(mTLDService, ): it
// is assumed that aHost is already normalized, and it may contain a leading dot
// (indicating that it represents a domain). A trailing dot may be present.
// If aHost is an IP address, an alias such as 'localhost', an eTLD such as
// 'co.uk', or the empty string, aBaseDomain will be the exact host, and a
// leading dot will be treated as an error.
nsresult CookieCommons::GetBaseDomainFromHost(
    nsIEffectiveTLDService* aTLDService, const nsACString& aHost,
    nsCString& aBaseDomain) {
  // aHost must not be the string '.'.
  if (aHost.Length() == 1 && aHost.Last() == '.') {
    return NS_ERROR_INVALID_ARG;
  }

  // aHost may contain a leading dot; if so, strip it now.
  bool domain = !aHost.IsEmpty() && aHost.First() == '.';

  // get the base domain. this will fail if the host contains a leading dot,
  // more than one trailing dot, or is otherwise malformed.
  nsresult rv = aTLDService->GetBaseDomainFromHost(Substring(aHost, domain), 0,
                                                   aBaseDomain);
  if (rv == NS_ERROR_HOST_IS_IP_ADDRESS ||
      rv == NS_ERROR_INSUFFICIENT_DOMAIN_LEVELS) {
    // aHost is either an IP address, an alias such as 'localhost', an eTLD
    // such as 'co.uk', or the empty string. use the host as a key in such
    // cases; however, we reject any such hosts with a leading dot, since it
    // doesn't make sense for them to be domain cookies.
    if (domain) {
      return NS_ERROR_INVALID_ARG;
    }

    aBaseDomain = aHost;
    return NS_OK;
  }
  return rv;
}

/* static */ bool CookieCommons::IsIPv6BaseDomain(
    const nsACString& aBaseDomain) {
  return aBaseDomain.Contains(':');
}

namespace {

void NotifyRejectionToObservers(nsIURI* aHostURI, CookieOperation aOperation) {
  if (aOperation == OPERATION_WRITE) {
    nsCOMPtr<nsIObserverService> os = services::GetObserverService();
    if (os) {
      os->NotifyObservers(aHostURI, "cookie-rejected", nullptr);
    }
  } else {
    MOZ_ASSERT(aOperation == OPERATION_READ);
  }
}

}  // namespace

// Notify observers that a cookie was rejected due to the users' prefs.
void CookieCommons::NotifyRejected(nsIURI* aHostURI, nsIChannel* aChannel,
                                   uint32_t aRejectedReason,
                                   CookieOperation aOperation) {
  NotifyRejectionToObservers(aHostURI, aOperation);

  ContentBlockingNotifier::OnDecision(
      aChannel, ContentBlockingNotifier::BlockingDecision::eBlock,
      aRejectedReason);
}

// static
bool CookieCommons::CheckCookiePermission(nsIChannel* aChannel,
                                          CookieStruct& aCookieData) {
  if (!aChannel) {
    // No channel, let's assume this is a system-principal request.
    return true;
  }

  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  nsCOMPtr<nsICookieJarSettings> cookieJarSettings;
  nsresult rv =
      loadInfo->GetCookieJarSettings(getter_AddRefs(cookieJarSettings));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return true;
  }

  nsIScriptSecurityManager* ssm =
      nsScriptSecurityManager::GetScriptSecurityManager();
  MOZ_ASSERT(ssm);

  nsCOMPtr<nsIPrincipal> channelPrincipal;
  rv = ssm->GetChannelURIPrincipal(aChannel, getter_AddRefs(channelPrincipal));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }

  return CheckCookiePermission(channelPrincipal, cookieJarSettings,
                               aCookieData);
}

// static
bool CookieCommons::CheckCookiePermission(
    nsIPrincipal* aPrincipal, nsICookieJarSettings* aCookieJarSettings,
    CookieStruct& aCookieData) {
  MOZ_ASSERT(aPrincipal);
  MOZ_ASSERT(aCookieJarSettings);

  if (!aPrincipal->GetIsContentPrincipal()) {
    return true;
  }

  uint32_t cookiePermission = nsICookiePermission::ACCESS_DEFAULT;
  nsresult rv =
      aCookieJarSettings->CookiePermission(aPrincipal, &cookiePermission);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return true;
  }

  if (cookiePermission == nsICookiePermission::ACCESS_ALLOW) {
    return true;
  }

  if (cookiePermission == nsICookiePermission::ACCESS_SESSION) {
    aCookieData.isSession() = true;
    return true;
  }

  if (cookiePermission == nsICookiePermission::ACCESS_DENY) {
    return false;
  }

  return true;
}

namespace {

CookieStatus CookieStatusForWindow(nsPIDOMWindowInner* aWindow,
                                   nsIURI* aDocumentURI) {
  MOZ_ASSERT(aWindow);
  MOZ_ASSERT(aDocumentURI);

  ThirdPartyUtil* thirdPartyUtil = ThirdPartyUtil::GetInstance();
  if (thirdPartyUtil) {
    bool isThirdParty = true;

    nsresult rv = thirdPartyUtil->IsThirdPartyWindow(
        aWindow->GetOuterWindow(), aDocumentURI, &isThirdParty);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "Third-party window check failed.");

    if (NS_SUCCEEDED(rv) && !isThirdParty) {
      return STATUS_ACCEPTED;
    }
  }

  return STATUS_ACCEPTED;
}

bool CheckCookieStringFromDocument(const nsACString& aCookieString) {
  // If the set-cookie-string contains a %x00-08 / %x0A-1F / %x7F character (CTL
  // characters excluding HTAB): Abort these steps and ignore the
  // set-cookie-string entirely.
  const char illegalCharacters[] = {
      0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x0A, 0x0B, 0x0C,
      0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
      0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x7F, 0x00};

  const auto* start = aCookieString.BeginReading();
  const auto* end = aCookieString.EndReading();

  auto charFilter = [&](unsigned char c) {
    if (StaticPrefs::network_cookie_blockUnicode() && c >= 0x80) {
      return true;
    }
    return std::find(std::begin(illegalCharacters), std::end(illegalCharacters),
                     c) != std::end(illegalCharacters);
  };

  return std::find_if(start, end, charFilter) == end;
}

}  // namespace

// static
already_AddRefed<Cookie> CookieCommons::CreateCookieFromDocument(
    CookieParser& aCookieParser, Document* aDocument,
    const nsACString& aCookieString, int64_t currentTimeInUsec,
    nsIEffectiveTLDService* aTLDService, mozIThirdPartyUtil* aThirdPartyUtil,
    nsACString& aBaseDomain, OriginAttributes& aAttrs) {
  if (!CookieCommons::IsSchemeSupported(aCookieParser.HostURI())) {
    return nullptr;
  }

  if (!CheckCookieStringFromDocument(aCookieString)) {
    return nullptr;
  }

  nsAutoCString baseDomain;
  bool requireHostMatch = false;
  nsresult rv = CookieCommons::GetBaseDomain(
      aTLDService, aCookieParser.HostURI(), baseDomain, requireHostMatch);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nullptr;
  }

  nsPIDOMWindowInner* innerWindow = aDocument->GetInnerWindow();
  if (NS_WARN_IF(!innerWindow)) {
    return nullptr;
  }

  bool isForeignAndNotAddon = false;
  if (!BasePrincipal::Cast(aDocument->NodePrincipal())->AddonPolicy()) {
    rv = aThirdPartyUtil->IsThirdPartyWindow(innerWindow->GetOuterWindow(),
                                             aCookieParser.HostURI(),
                                             &isForeignAndNotAddon);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      isForeignAndNotAddon = true;
    }
  }

  bool mustBePartitioned =
      isForeignAndNotAddon &&
      aDocument->CookieJarSettings()->GetCookieBehavior() ==
          nsICookieService::BEHAVIOR_PARTITION_FOREIGN &&
      !aDocument->UsingStorageAccess();

  // If we are here, we have been already accepted by the anti-tracking.
  // We just need to check if we have to be in session-only mode.
  CookieStatus cookieStatus =
      CookieStatusForWindow(innerWindow, aCookieParser.HostURI());
  MOZ_ASSERT(cookieStatus == STATUS_ACCEPTED ||
             cookieStatus == STATUS_ACCEPT_SESSION);

  nsCString cookieString(aCookieString);

  nsCOMPtr<nsILoadInfo> loadInfo =
      aDocument->GetChannel() ? aDocument->GetChannel()->LoadInfo() : nullptr;
  const bool on3pcbException = loadInfo && loadInfo->GetIsOn3PCBExceptionList();

  aCookieParser.Parse(baseDomain, requireHostMatch, cookieStatus, cookieString,
                      EmptyCString(), false, isForeignAndNotAddon,
                      mustBePartitioned, aDocument->IsInPrivateBrowsing(),
                      on3pcbException, PR_Now());

  if (!aCookieParser.ContainsCookie()) {
    return nullptr;
  }

  // check permissions from site permission list.
  if (!CookieCommons::CheckCookiePermission(aDocument->NodePrincipal(),
                                            aDocument->CookieJarSettings(),
                                            aCookieParser.CookieData())) {
    NotifyRejectionToObservers(aCookieParser.HostURI(), OPERATION_WRITE);
    ContentBlockingNotifier::OnDecision(
        innerWindow, ContentBlockingNotifier::BlockingDecision::eBlock,
        nsIWebProgressListener::STATE_COOKIES_BLOCKED_BY_PERMISSION);
    return nullptr;
  }

  // CHIPS - If the partitioned attribute is set, store cookie in partitioned
  // cookie jar independent of context. If the cookies are stored in the
  // partitioned cookie jar anyway no special treatment of CHIPS cookies
  // necessary.
  bool needPartitioned = StaticPrefs::network_cookie_CHIPS_enabled() &&
                         aCookieParser.CookieData().isPartitioned();
  nsCOMPtr<nsIPrincipal> cookiePrincipal =
      needPartitioned ? aDocument->PartitionedPrincipal()
                      : aDocument->EffectiveCookiePrincipal();
  MOZ_ASSERT(cookiePrincipal);

  nsCOMPtr<nsICookieService> service =
      do_GetService(NS_COOKIESERVICE_CONTRACTID);
  if (!service) {
    return nullptr;
  }

  // Check if limit-foreign is required.
  uint32_t dummyRejectedReason = 0;
  if (aDocument->CookieJarSettings()->GetLimitForeignContexts() &&
      !service->HasExistingCookies(baseDomain,
                                   cookiePrincipal->OriginAttributesRef()) &&
      !ShouldAllowAccessFor(innerWindow, aCookieParser.HostURI(), true,
                            &dummyRejectedReason)) {
    return nullptr;
  }

  RefPtr<Cookie> cookie = Cookie::Create(
      aCookieParser.CookieData(), cookiePrincipal->OriginAttributesRef());
  MOZ_ASSERT(cookie);

  cookie->SetLastAccessedInUSec(currentTimeInUsec);
  cookie->SetCreationTimeInUSec(
      Cookie::GenerateUniqueCreationTimeInUSec(currentTimeInUsec));
  cookie->SetUpdateTimeInUSec(cookie->CreationTimeInUSec());

  aBaseDomain = baseDomain;
  aAttrs = cookiePrincipal->OriginAttributesRef();

  return cookie.forget();
}

// static
already_AddRefed<nsICookieJarSettings> CookieCommons::GetCookieJarSettings(
    nsIChannel* aChannel) {
  nsCOMPtr<nsICookieJarSettings> cookieJarSettings;
  bool shouldResistFingerprinting = nsContentUtils::ShouldResistFingerprinting(
      aChannel, RFPTarget::IsAlwaysEnabledForPrecompute);
  if (aChannel) {
    nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
    nsresult rv =
        loadInfo->GetCookieJarSettings(getter_AddRefs(cookieJarSettings));
    if (NS_WARN_IF(NS_FAILED(rv))) {
      cookieJarSettings =
          CookieJarSettings::GetBlockingAll(shouldResistFingerprinting);
    }
  } else {
    cookieJarSettings = CookieJarSettings::Create(CookieJarSettings::eRegular,
                                                  shouldResistFingerprinting);
  }

  MOZ_ASSERT(cookieJarSettings);
  return cookieJarSettings.forget();
}

// static
bool CookieCommons::ShouldIncludeCrossSiteCookie(
    Cookie* aCookie, nsIURI* aHostURI, bool aPartitionForeign,
    bool aInPrivateBrowsing, bool aUsingStorageAccess, bool aOn3pcbException) {
  MOZ_ASSERT(aCookie);

  int32_t sameSiteAttr = 0;
  aCookie->GetSameSite(&sameSiteAttr);

  return ShouldIncludeCrossSiteCookie(
      aHostURI, sameSiteAttr,
      aCookie->IsPartitioned() && aCookie->RawIsPartitioned(),
      aPartitionForeign, aInPrivateBrowsing, aUsingStorageAccess,
      aOn3pcbException);
}

// static
bool CookieCommons::ShouldIncludeCrossSiteCookie(
    nsIURI* aHostURI, int32_t aSameSiteAttr, bool aCookiePartitioned,
    bool aPartitionForeign, bool aInPrivateBrowsing, bool aUsingStorageAccess,
    bool aOn3pcbException) {
  if (aSameSiteAttr == nsICookie::SAMESITE_UNSET) {
    bool laxByDefault =
        StaticPrefs::network_cookie_sameSite_laxByDefault() &&
        !nsContentUtils::IsURIInPrefList(
            aHostURI, "network.cookie.sameSite.laxByDefault.disabledHosts");
    aSameSiteAttr =
        laxByDefault ? nsICookie::SAMESITE_LAX : nsICookie::SAMESITE_NONE;
  }

  // CHIPS - If a third-party has storage access it can access both it's
  // partitioned and unpartitioned cookie jars, else its cookies are blocked.
  //
  // Note that we will only include partitioned cookies that have "partitioned"
  // attribution if we enable opt-in partitioning.
  if (aPartitionForeign &&
      (StaticPrefs::network_cookie_cookieBehavior_optInPartitioning() ||
       (aInPrivateBrowsing &&
        StaticPrefs::
            network_cookie_cookieBehavior_optInPartitioning_pbmode())) &&
      !aCookiePartitioned && !aUsingStorageAccess && !aOn3pcbException) {
    return false;
  }

  return aSameSiteAttr == nsICookie::SAMESITE_NONE;
}

// static
bool CookieCommons::IsFirstPartyPartitionedCookieWithoutCHIPS(
    Cookie* aCookie, const nsACString& aBaseDomain,
    const OriginAttributes& aOriginAttributes) {
  MOZ_ASSERT(aCookie);

  // The cookie is set with partitioned attribute. This is a CHIPS cookies.
  if (aCookie->RawIsPartitioned()) {
    return false;
  }

  // The originAttributes is not partitioned. This is not a partitioned cookie.
  if (aOriginAttributes.mPartitionKey.IsEmpty()) {
    return false;
  }

  nsAutoString scheme;
  nsAutoString baseDomain;
  int32_t port;
  bool foreignByAncestorContext;
  // Bail out early if the partition key is not valid.
  if (!OriginAttributes::ParsePartitionKey(aOriginAttributes.mPartitionKey,
                                           scheme, baseDomain, port,
                                           foreignByAncestorContext)) {
    return false;
  }

  // Check whether the base domain of the cookie match the base domain in the
  // partitionKey and it is not an ABA context
  return aBaseDomain.Equals(NS_ConvertUTF16toUTF8(baseDomain)) &&
         !foreignByAncestorContext;
}

// static
bool CookieCommons::ShouldEnforceSessionForOriginAttributes(
    const OriginAttributes& aOriginAttributes) {
  // We don't need to enforce session for unpartitioned OAs.
  if (aOriginAttributes.mPartitionKey.IsEmpty()) {
    return false;
  }

  // We enforce session cookies if the partitionKey is from a null principal.
  // This ensures that we don't create dangling cookies that cannot be deleted.
  // A partitionKey from a null principal ends with ".mozilla".
  if (StringEndsWith(aOriginAttributes.mPartitionKey, u".mozilla"_ns)) {
    return true;
  }

  return false;
}

bool CookieCommons::IsSafeTopLevelNav(nsIChannel* aChannel) {
  if (!aChannel) {
    return false;
  }
  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  nsCOMPtr<nsIInterceptionInfo> interceptionInfo = loadInfo->InterceptionInfo();
  if ((loadInfo->GetExternalContentPolicyType() !=
           ExtContentPolicy::TYPE_DOCUMENT &&
       loadInfo->GetExternalContentPolicyType() !=
           ExtContentPolicy::TYPE_SAVEAS_DOWNLOAD) &&
      !interceptionInfo) {
    return false;
  }

  if (interceptionInfo &&
      interceptionInfo->GetExtContentPolicyType() !=
          ExtContentPolicy::TYPE_DOCUMENT &&
      interceptionInfo->GetExtContentPolicyType() !=
          ExtContentPolicy::TYPE_SAVEAS_DOWNLOAD &&
      interceptionInfo->GetExtContentPolicyType() !=
          ExtContentPolicy::TYPE_INVALID) {
    return false;
  }

  return NS_IsSafeMethodNav(aChannel);
}

// This function determines if two schemes are equal in the context of
// "Schemeful SameSite cookies".
//
// Two schemes are considered equal:
//   - if the "network.cookie.sameSite.schemeful" pref is set to false.
// OR
//   - if one of the schemes is not http or https.
// OR
//   - if both schemes are equal AND both are either http or https.
bool IsSameSiteSchemeEqual(const nsACString& aFirstScheme,
                           const nsACString& aSecondScheme) {
  if (!StaticPrefs::network_cookie_sameSite_schemeful()) {
    return true;
  }

  auto isSchemeHttpOrHttps = [](const nsACString& scheme) -> bool {
    return scheme.EqualsLiteral("http") || scheme.EqualsLiteral("https");
  };

  if (!isSchemeHttpOrHttps(aFirstScheme) ||
      !isSchemeHttpOrHttps(aSecondScheme)) {
    return true;
  }

  return aFirstScheme.Equals(aSecondScheme);
}

bool CookieCommons::IsSameSiteForeign(nsIChannel* aChannel, nsIURI* aHostURI,
                                      bool* aHadCrossSiteRedirects) {
  *aHadCrossSiteRedirects = false;

  if (!aChannel) {
    return false;
  }
  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  // Do not treat loads triggered by web extensions as foreign
  nsCOMPtr<nsIURI> channelURI;
  NS_GetFinalChannelURI(aChannel, getter_AddRefs(channelURI));

  nsCOMPtr<nsIInterceptionInfo> interceptionInfo = loadInfo->InterceptionInfo();

  RefPtr<BasePrincipal> triggeringPrincipal;
  ExtContentPolicy contentPolicyType;
  if (interceptionInfo && interceptionInfo->TriggeringPrincipal()) {
    triggeringPrincipal =
        BasePrincipal::Cast(interceptionInfo->TriggeringPrincipal());
    contentPolicyType = interceptionInfo->GetExtContentPolicyType();
  } else {
    triggeringPrincipal = BasePrincipal::Cast(loadInfo->TriggeringPrincipal());
    contentPolicyType = loadInfo->GetExternalContentPolicyType();

    if (triggeringPrincipal->AddonPolicy() &&
        triggeringPrincipal->AddonAllowsLoad(channelURI)) {
      return false;
    }
  }
  const nsTArray<nsCOMPtr<nsIRedirectHistoryEntry>>& redirectChain(
      interceptionInfo && interceptionInfo->TriggeringPrincipal()
          ? interceptionInfo->RedirectChain()
          : loadInfo->RedirectChain());

  nsAutoCString hostScheme, otherScheme;
  aHostURI->GetScheme(hostScheme);

  bool isForeign = true;
  nsresult rv;
  if (contentPolicyType == ExtContentPolicy::TYPE_DOCUMENT ||
      contentPolicyType == ExtContentPolicy::TYPE_SAVEAS_DOWNLOAD) {
    // for loads of TYPE_DOCUMENT we query the hostURI from the
    // triggeringPrincipal which returns the URI of the document that caused the
    // navigation.
    rv = triggeringPrincipal->IsThirdPartyChannel(aChannel, &isForeign);

    triggeringPrincipal->GetScheme(otherScheme);
  } else {
    // If the load is caused by FetchEvent.request or NavigationPreload request,
    // check the original InterceptedHttpChannel is a third-party channel or
    // not.
    if (interceptionInfo && interceptionInfo->TriggeringPrincipal()) {
      isForeign = interceptionInfo->FromThirdParty();
      if (isForeign) {
        return true;
      }
    }
    nsCOMPtr<mozIThirdPartyUtil> thirdPartyUtil =
        do_GetService(THIRDPARTYUTIL_CONTRACTID);
    if (!thirdPartyUtil) {
      return true;
    }
    rv = thirdPartyUtil->IsThirdPartyChannel(aChannel, aHostURI, &isForeign);

    channelURI->GetScheme(otherScheme);
  }
  // if we are dealing with a cross origin request, we can return here
  // because we already know the request is 'foreign'.
  if (NS_FAILED(rv) || isForeign) {
    return true;
  }

  if (!IsSameSiteSchemeEqual(otherScheme, hostScheme)) {
    // If the two schemes are not of the same http(s) scheme then we
    // consider the request as foreign.
    return true;
  }

  // for loads of TYPE_SUBDOCUMENT we have to perform an additional test,
  // because a cross-origin iframe might perform a navigation to a same-origin
  // iframe which would send same-site cookies. Hence, if the iframe navigation
  // was triggered by a cross-origin triggeringPrincipal, we treat the load as
  // foreign.
  if (contentPolicyType == ExtContentPolicy::TYPE_SUBDOCUMENT) {
    rv = triggeringPrincipal->IsThirdPartyChannel(aChannel, &isForeign);
    if (NS_FAILED(rv) || isForeign) {
      return true;
    }
  }

  // for the purpose of same-site cookies we have to treat any cross-origin
  // redirects as foreign. E.g. cross-site to same-site redirect is a problem
  // with regards to CSRF.

  nsCOMPtr<nsIPrincipal> redirectPrincipal;
  for (nsIRedirectHistoryEntry* entry : redirectChain) {
    entry->GetPrincipal(getter_AddRefs(redirectPrincipal));
    if (redirectPrincipal) {
      rv = redirectPrincipal->IsThirdPartyChannel(aChannel, &isForeign);
      // if at any point we encounter a cross-origin redirect we can return.
      if (NS_FAILED(rv) || isForeign) {
        *aHadCrossSiteRedirects = isForeign;
        return true;
      }

      nsAutoCString redirectScheme;
      redirectPrincipal->GetScheme(redirectScheme);
      if (!IsSameSiteSchemeEqual(redirectScheme, hostScheme)) {
        // If the two schemes are not of the same http(s) scheme then we
        // consider the request as foreign.
        *aHadCrossSiteRedirects = true;
        return true;
      }
    }
  }
  return isForeign;
}

// static
nsICookie::schemeType CookieCommons::URIToSchemeType(nsIURI* aURI) {
  MOZ_ASSERT(aURI);

  nsAutoCString scheme;
  nsresult rv = aURI->GetScheme(scheme);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nsICookie::SCHEME_UNSET;
  }

  return SchemeToSchemeType(scheme);
}

// static
nsICookie::schemeType CookieCommons::PrincipalToSchemeType(
    nsIPrincipal* aPrincipal) {
  MOZ_ASSERT(aPrincipal);

  nsAutoCString scheme;
  nsresult rv = aPrincipal->GetScheme(scheme);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return nsICookie::SCHEME_UNSET;
  }

  return SchemeToSchemeType(scheme);
}

// static
nsICookie::schemeType CookieCommons::SchemeToSchemeType(
    const nsACString& aScheme) {
  MOZ_ASSERT(IsSchemeSupported(aScheme));

  if (aScheme.Equals("https")) {
    return nsICookie::SCHEME_HTTPS;
  }

  if (aScheme.Equals("http")) {
    return nsICookie::SCHEME_HTTP;
  }

  if (aScheme.Equals("file")) {
    return nsICookie::SCHEME_FILE;
  }

  MOZ_CRASH("Unsupported scheme type");
}

// static
bool CookieCommons::IsSchemeSupported(nsIPrincipal* aPrincipal) {
  MOZ_ASSERT(aPrincipal);

  nsAutoCString scheme;
  nsresult rv = aPrincipal->GetScheme(scheme);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }

  return IsSchemeSupported(scheme);
}

// static
bool CookieCommons::IsSchemeSupported(nsIURI* aURI) {
  MOZ_ASSERT(aURI);

  nsAutoCString scheme;
  nsresult rv = aURI->GetScheme(scheme);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }

  return IsSchemeSupported(scheme);
}

// static
bool CookieCommons::IsSchemeSupported(const nsACString& aScheme) {
  return aScheme.Equals("https") || aScheme.Equals("http") ||
         (aScheme.Equals("file") &&
          !StaticPrefs::network_cookie_blockFileScheme());
}

// static
bool CookieCommons::ChipsLimitEnabledAndChipsCookie(
    const Cookie& cookie, dom::BrowsingContext* aBrowsingContext) {
  bool tcpEnabled = false;
  if (aBrowsingContext) {
    dom::CanonicalBrowsingContext* canonBC = aBrowsingContext->Canonical();
    if (canonBC) {
      dom::WindowGlobalParent* windowParent = canonBC->GetCurrentWindowGlobal();
      if (windowParent) {
        nsCOMPtr<nsICookieJarSettings> cjs = windowParent->CookieJarSettings();
        tcpEnabled = cjs->GetPartitionForeign();
      }
    }
  } else {
    // calls coming from addNative have no document, channel or browsingContext
    // to determine if TCP is enabled, so we just create a cookieJarSettings to
    // check the pref.
    nsCOMPtr<nsICookieJarSettings> cjs;
    cjs = CookieJarSettings::Create(CookieJarSettings::eRegular, false);
    tcpEnabled = cjs->GetPartitionForeign();
  }

  return StaticPrefs::network_cookie_CHIPS_enabled() &&
         StaticPrefs::network_cookie_chips_partitionLimitEnabled() &&
         cookie.IsPartitioned() && cookie.RawIsPartitioned() && tcpEnabled;
}

void CookieCommons::ComposeCookieString(nsTArray<RefPtr<Cookie>>& aCookieList,
                                        nsACString& aCookieString) {
  for (Cookie* cookie : aCookieList) {
    // check if we have anything to write
    if (!cookie->Name().IsEmpty() || !cookie->Value().IsEmpty()) {
      // if we've already added a cookie to the return list, append a "; " so
      // that subsequent cookies are delimited in the final list.
      if (!aCookieString.IsEmpty()) {
        aCookieString.AppendLiteral("; ");
      }

      if (!cookie->Name().IsEmpty()) {
        // we have a name and value - write both
        aCookieString += cookie->Name() + "="_ns + cookie->Value();
      } else {
        // just write value
        aCookieString += cookie->Value();
      }
    }
  }
}

// static
CookieCommons::SecurityChecksResult
CookieCommons::CheckGlobalAndRetrieveCookiePrincipals(
    Document* aDocument, nsIPrincipal** aCookiePrincipal,
    nsIPrincipal** aCookiePartitionedPrincipal) {
  MOZ_ASSERT(aCookiePrincipal);

  nsCOMPtr<nsIPrincipal> cookiePrincipal;
  nsCOMPtr<nsIPrincipal> cookiePartitionedPrincipal;

  if (!NS_IsMainThread()) {
    MOZ_ASSERT(!aDocument);

    dom::WorkerPrivate* workerPrivate = dom::GetCurrentThreadWorkerPrivate();
    MOZ_ASSERT(workerPrivate);

    StorageAccess storageAccess = workerPrivate->StorageAccess();
    if (storageAccess == StorageAccess::eDeny) {
      return SecurityChecksResult::eDoNotContinue;
    }

    cookiePrincipal = workerPrivate->GetPrincipal();
    if (NS_WARN_IF(!cookiePrincipal) || cookiePrincipal->GetIsNullPrincipal()) {
      return SecurityChecksResult::eSecurityError;
    }

    // CHIPS - If CHIPS is enabled the partitioned cookie jar is always
    // available (and therefore the partitioned principal), the unpartitioned
    // cookie jar is only available in first-party or third-party with
    // storageAccess contexts. In both cases, the Worker will have storage
    // access.
    bool isCHIPS =
        StaticPrefs::network_cookie_CHIPS_enabled() &&
        !workerPrivate->CookieJarSettings()->GetBlockingAllContexts();
    bool workerHasStorageAccess =
        workerPrivate->StorageAccess() == StorageAccess::eAllow;

    if (isCHIPS && workerHasStorageAccess) {
      // Only retrieve the partitioned originAttributes if the partitionKey is
      // set. The partitionKey could be empty for partitionKey in partitioned
      // originAttributes if the aWorker is for privilege context, such as the
      // extension's background page.
      nsCOMPtr<nsIPrincipal> partitionedPrincipal =
          workerPrivate->GetPartitionedPrincipal();
      if (partitionedPrincipal && !partitionedPrincipal->OriginAttributesRef()
                                       .mPartitionKey.IsEmpty()) {
        cookiePartitionedPrincipal = std::move(partitionedPrincipal);
      }
    }
  } else {
    if (!aDocument) {
      return SecurityChecksResult::eDoNotContinue;
    }

    // If the document's sandboxed origin flag is set, then reading cookies
    // is prohibited.
    if (aDocument->GetSandboxFlags() & SANDBOXED_ORIGIN) {
      return SecurityChecksResult::eSandboxedError;
    }

    cookiePrincipal = aDocument->EffectiveCookiePrincipal();
    if (NS_WARN_IF(!cookiePrincipal) || cookiePrincipal->GetIsNullPrincipal()) {
      return SecurityChecksResult::eSecurityError;
    }

    if (aDocument->CookieAccessDisabled()) {
      return SecurityChecksResult::eDoNotContinue;
    }

    // GTests do not create an inner window and because of these a few security
    // checks will block this method.
    if (!StaticPrefs::dom_cookie_testing_enabled()) {
      StorageAccess storageAccess = CookieAllowedForDocument(aDocument);
      if (storageAccess == StorageAccess::eDeny) {
        return SecurityChecksResult::eDoNotContinue;
      }

      if (ShouldPartitionStorage(storageAccess) &&
          !StoragePartitioningEnabled(storageAccess,
                                      aDocument->CookieJarSettings())) {
        return SecurityChecksResult::eDoNotContinue;
      }

      // If the document is a cookie-averse Document... return the empty string.
      if (aDocument->IsCookieAverse()) {
        return SecurityChecksResult::eDoNotContinue;
      }
    }

    // CHIPS - If CHIPS is enabled the partitioned cookie jar is always
    // available (and therefore the partitioned principal), the unpartitioned
    // cookie jar is only available in first-party or third-party with
    // storageAccess contexts. In both cases, the aDocument will have storage
    // access.
    bool isCHIPS = StaticPrefs::network_cookie_CHIPS_enabled() &&
                   !aDocument->CookieJarSettings()->GetBlockingAllContexts();
    bool documentHasStorageAccess = false;
    nsresult rv = aDocument->HasStorageAccessSync(documentHasStorageAccess);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return SecurityChecksResult::eDoNotContinue;
    }

    if (isCHIPS && documentHasStorageAccess) {
      // Only append the partitioned originAttributes if the partitionKey is
      // set. The partitionKey could be empty for partitionKey in partitioned
      // originAttributes if the aDocument is for privilege context, such as the
      // extension's background page.
      if (!aDocument->PartitionedPrincipal()
               ->OriginAttributesRef()
               .mPartitionKey.IsEmpty()) {
        cookiePartitionedPrincipal = aDocument->PartitionedPrincipal();
      }
    }
  }

  if (!IsSchemeSupported(cookiePrincipal)) {
    return SecurityChecksResult::eDoNotContinue;
  }

  cookiePrincipal.forget(aCookiePrincipal);

  if (aCookiePartitionedPrincipal) {
    cookiePartitionedPrincipal.forget(aCookiePartitionedPrincipal);
  }

  return SecurityChecksResult::eContinue;
}
// static
void CookieCommons::GetServerDateHeader(nsIChannel* aChannel,
                                        nsACString& aServerDateHeader) {
  if (!aChannel) {
    return;
  }

  nsCOMPtr<nsIHttpChannel> channel = do_QueryInterface(aChannel);
  if (NS_WARN_IF(!channel)) {
    return;
  }

  (void)channel->GetResponseHeader("Date"_ns, aServerDateHeader);
}

// static
int64_t CookieCommons::MaybeCapExpiry(int64_t aCurrentTimeInMSec,
                                      int64_t aExpiryInMSec) {
  const int64_t maxageCap = StaticPrefs::network_cookie_maxageCap();

  if (maxageCap) {
    aExpiryInMSec =
        std::min(aExpiryInMSec, aCurrentTimeInMSec + maxageCap * 1000);
  }

  return aExpiryInMSec;
}

int64_t CookieCommons::MaybeCapMaxAge(int64_t aCurrentTimeInMSec,
                                      int64_t aMaxAgeInSec) {
  const int64_t maxageCap = StaticPrefs::network_cookie_maxageCap();
  CheckedInt<int64_t> value(aCurrentTimeInMSec);

  value +=
      (maxageCap ? std::min(aMaxAgeInSec, maxageCap) : aMaxAgeInSec) * 1000;
  return value.isValid() ? value.value() : INT64_MAX;
}

// static
bool CookieCommons::IsSubdomainOf(const nsACString& a, const nsACString& b) {
  if (a == b) {
    return true;
  }
  if (a.Length() > b.Length()) {
    return a[a.Length() - b.Length() - 1] == '.' && StringEndsWith(a, b);
  }
  return false;
}

// static
int64_t CookieCommons::GetCurrentTimeInUSecFromChannel(nsIChannel* aChannel) {
  nsCOMPtr<nsITimedChannel> timedChannel = do_QueryInterface(aChannel);
  if (timedChannel) {
    PRTime currentTimeInUSec = 0;
    nsresult rv = timedChannel->GetResponseStartTime(&currentTimeInUSec);
    if (NS_SUCCEEDED(rv) && currentTimeInUSec) {
      return currentTimeInUSec;
    }
  }

  return PR_Now();
}

}  // namespace net
}  // namespace mozilla
