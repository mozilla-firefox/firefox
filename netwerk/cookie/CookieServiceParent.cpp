/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CookieServiceParent.h"

#include "CookieCommons.h"
#include "CookieLogging.h"
#include "mozIThirdPartyUtil.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/StoragePrincipalHelper.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/ipc/URIUtils.h"
#include "mozilla/net/CookieService.h"
#include "mozilla/net/CookieServiceParent.h"
#include "mozilla/net/PNeckoParent.h"
#include "nsArrayUtils.h"
#include "nsIChannel.h"
#include "nsIEffectiveTLDService.h"
#include "nsMixedContentBlocker.h"
#include "nsNetCID.h"

using namespace mozilla::ipc;

namespace mozilla {
namespace net {

CookieServiceParent::CookieServiceParent(dom::ContentParent* aContentParent) {
  MOZ_ASSERT(aContentParent);

  // Instantiate the cookieservice via the service manager, so it sticks around
  // until shutdown.
  nsCOMPtr<nsICookieService> cs = do_GetService(NS_COOKIESERVICE_CONTRACTID);

  // Get the CookieService instance directly, so we can call internal methods.
  mCookieService = CookieService::GetSingleton();
  NS_ASSERTION(mCookieService, "couldn't get nsICookieService");

  mTLDService = do_GetService(NS_EFFECTIVETLDSERVICE_CONTRACTID);
  MOZ_ALWAYS_TRUE(mTLDService);

  mProcessingCookie = false;

  nsTArray<nsCOMPtr<nsIPrincipal>> list;
  aContentParent->TakeCookieInProcessCache(list);

  for (nsIPrincipal* principal : list) {
    nsCOMPtr<nsIURI> uri = principal->GetURI();
    UpdateCookieInContentList(uri, principal->OriginAttributesRef());
  }
}

void CookieServiceParent::RemoveBatchDeletedCookies(nsIArray* aCookieList) {
  uint32_t len = 0;
  aCookieList->GetLength(&len);
  OriginAttributes attrs;
  CookieStruct cookieStruct;
  nsTArray<CookieStruct> cookieStructList;
  nsTArray<OriginAttributes> attrsList;
  for (uint32_t i = 0; i < len; i++) {
    nsCOMPtr<nsICookie> xpcCookie = do_QueryElementAt(aCookieList, i);
    const auto& cookie = xpcCookie->AsCookie();
    attrs = cookie.OriginAttributesRef();
    cookieStruct = cookie.ToIPC();

    // Child only needs to know HttpOnly cookies exists, not its value
    // Same for Secure cookies going to a process for an insecure site.
    if (cookie.IsHttpOnly() || !InsecureCookieOrSecureOrigin(cookie)) {
      cookieStruct.value() = "";
    }
    cookieStructList.AppendElement(cookieStruct);
    attrsList.AppendElement(attrs);
  }
  (void)SendRemoveBatchDeletedCookies(cookieStructList, attrsList);
}

void CookieServiceParent::RemoveAll() { (void)SendRemoveAll(); }

void CookieServiceParent::RemoveCookie(const Cookie& cookie,
                                       const nsID* aOperationID) {
  const OriginAttributes& attrs = cookie.OriginAttributesRef();
  CookieStruct cookieStruct = cookie.ToIPC();

  // Child only needs to know HttpOnly cookies exists, not its value
  // Same for Secure cookies going to a process for an insecure site.
  if (cookie.IsHttpOnly() || !InsecureCookieOrSecureOrigin(cookie)) {
    cookieStruct.value() = "";
  }
  (void)SendRemoveCookie(cookieStruct, attrs,
                         aOperationID ? Some(*aOperationID) : Nothing());
}

void CookieServiceParent::AddCookie(const Cookie& cookie,
                                    const nsID* aOperationID) {
  const OriginAttributes& attrs = cookie.OriginAttributesRef();
  CookieStruct cookieStruct = cookie.ToIPC();

  // Child only needs to know HttpOnly cookies exists, not its value
  // Same for Secure cookies going to a process for an insecure site.
  if (cookie.IsHttpOnly() || !InsecureCookieOrSecureOrigin(cookie)) {
    cookieStruct.value() = "";
  }
  (void)SendAddCookie(cookieStruct, attrs,
                      aOperationID ? Some(*aOperationID) : Nothing());
}

bool CookieServiceParent::ContentProcessHasCookie(const Cookie& cookie) {
  return ContentProcessHasCookie(cookie.Host(), cookie.OriginAttributesRef());
}

bool CookieServiceParent::ContentProcessHasCookie(
    const nsACString& aBaseDomain, const OriginAttributes& aOriginAttributes) {
  nsCString baseDomain;
  if (NS_WARN_IF(NS_FAILED(CookieCommons::GetBaseDomainFromHost(
          mTLDService, aBaseDomain, baseDomain)))) {
    return false;
  }

  CookieKey cookieKey(baseDomain, aOriginAttributes);
  return mCookieKeysInContent.MaybeGet(cookieKey).isSome();
}

bool CookieServiceParent::InsecureCookieOrSecureOrigin(const Cookie& cookie) {
  nsCString baseDomain;
  if (NS_FAILED(CookieCommons::GetBaseDomainFromHost(mTLDService, cookie.Host(),
                                                     baseDomain))) {
    MOZ_ASSERT(false,
               "CookieServiceParent::InsecureCookieOrSecureOrigin - "
               "GetBaseDomainFromHost shouldn't fail");
    return false;
  }

  // cookie is insecure or cookie is associated with a secure-origin process
  CookieKey cookieKey(baseDomain, cookie.OriginAttributesRef());
  if (Maybe<bool> allowSecure = mCookieKeysInContent.MaybeGet(cookieKey)) {
    return (!cookie.IsSecure() || *allowSecure);
  }
  return false;
}

void CookieServiceParent::TrackCookieLoad(nsIChannel* aChannel) {
  nsCOMPtr<nsIURI> uri;
  aChannel->GetURI(getter_AddRefs(uri));

  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  bool isSafeTopLevelNav = CookieCommons::IsSafeTopLevelNav(aChannel);
  bool hadCrossSiteRedirects = false;
  bool isSameSiteForeign =
      CookieCommons::IsSameSiteForeign(aChannel, uri, &hadCrossSiteRedirects);

  nsCOMPtr<mozIThirdPartyUtil> thirdPartyUtil;
  thirdPartyUtil = do_GetService(THIRDPARTYUTIL_CONTRACTID);

  uint32_t rejectedReason = 0;
  ThirdPartyAnalysisResult result = thirdPartyUtil->AnalyzeChannel(
      aChannel, false, nullptr, nullptr, &rejectedReason);

  OriginAttributes storageOriginAttributes = loadInfo->GetOriginAttributes();
  StoragePrincipalHelper::PrepareEffectiveStoragePrincipalOriginAttributes(
      aChannel, storageOriginAttributes);

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

  for (auto& originAttributes : originAttributesList) {
    UpdateCookieInContentList(uri, originAttributes);
  }

  // Send matching cookies to Child.
  nsTArray<RefPtr<Cookie>> foundCookieList;
  mCookieService->GetCookiesForURI(
      uri, aChannel, result.contains(ThirdPartyAnalysis::IsForeign),
      result.contains(ThirdPartyAnalysis::IsThirdPartyTrackingResource),
      result.contains(ThirdPartyAnalysis::IsThirdPartySocialTrackingResource),
      result.contains(ThirdPartyAnalysis::IsStorageAccessPermissionGranted),
      rejectedReason, isSafeTopLevelNav, isSameSiteForeign,
      hadCrossSiteRedirects, false, true, originAttributesList,
      foundCookieList);
  nsTArray<CookieStructTable> matchingCookiesListTable;
  SerializeCookieListTable(foundCookieList, matchingCookiesListTable, uri);
  (void)SendTrackCookiesLoad(matchingCookiesListTable);
}

// we append outgoing cookie info into a list here so the ContentParent can
// filter cookies passing to unnecessary ContentProcesses
void CookieServiceParent::UpdateCookieInContentList(
    nsIURI* uri, const OriginAttributes& originAttrs) {
  nsCString baseDomain;
  bool requireAHostMatch = false;

  // prevent malformed urls from being added to the cookie list
  if (NS_WARN_IF(NS_FAILED(CookieCommons::GetBaseDomain(
          mTLDService, uri, baseDomain, requireAHostMatch)))) {
    return;
  }

  CookieKey cookieKey(baseDomain, originAttrs);
  bool& allowSecure = mCookieKeysInContent.LookupOrInsert(cookieKey, false);
  allowSecure =
      allowSecure || nsMixedContentBlocker::IsPotentiallyTrustworthyOrigin(uri);
}

// static
void CookieServiceParent::SerializeCookieListTable(
    const nsTArray<RefPtr<Cookie>>& aFoundCookieList,
    nsTArray<CookieStructTable>& aCookiesListTable, nsIURI* aHostURI) {
  // Stores the index in aCookiesListTable by origin attributes suffix.
  nsTHashMap<nsCStringHashKey, size_t> cookieListTable;

  for (Cookie* cookie : aFoundCookieList) {
    nsAutoCString attrsSuffix;
    cookie->OriginAttributesRef().CreateSuffix(attrsSuffix);
    size_t tableIndex = cookieListTable.LookupOrInsertWith(attrsSuffix, [&] {
      size_t index = aCookiesListTable.Length();
      CookieStructTable* newTable = aCookiesListTable.AppendElement();
      newTable->attrs() = cookie->OriginAttributesRef();
      return index;
    });

    CookieStruct* cookieStruct =
        aCookiesListTable[tableIndex].cookies().AppendElement();
    *cookieStruct = cookie->ToIPC();

    // clear http-only cookie values
    if (cookie->IsHttpOnly()) {
      // Value only needs to exist if an HttpOnly cookie exists.
      cookieStruct->value() = "";
    }

    // clear secure cookie values in insecure context
    bool potentiallyTurstworthy =
        nsMixedContentBlocker::IsPotentiallyTrustworthyOrigin(aHostURI);
    if (cookie->IsSecure() && !potentiallyTurstworthy) {
      cookieStruct->value() = "";
    }
  }
}

IPCResult CookieServiceParent::RecvGetCookieList(
    nsIURI* aHost, const bool& aIsForeign,
    const bool& aIsThirdPartyTrackingResource,
    const bool& aIsThirdPartySocialTrackingResource,
    const bool& aStorageAccessPermissionGranted,
    const uint32_t& aRejectedReason, const bool& aIsSafeTopLevelNav,
    const bool& aIsSameSiteForeign, const bool& aHadCrossSiteRedirects,
    nsTArray<OriginAttributes>&& aAttrsList, GetCookieListResolver&& aResolve) {
  // Send matching cookies to Child.
  if (!aHost) {
    return IPC_FAIL(this, "aHost must not be null");
  }

  auto* contentParent = static_cast<dom::ContentParent*>(Manager()->Manager());
  if (!contentParent) {
    return IPC_FAIL(this, "Missing ContentParent in GetCookieList");
  }

  // Reads are non-mutating, and GetCookieList requests can race content
  // process teardown, so a failed authorization fails closed (no cookies are
  // served or registered for that principal) rather than killing the process.
  nsTArray<OriginAttributes> authorizedAttrsList;
  for (const auto& attrs : aAttrsList) {
    nsCOMPtr<nsIPrincipal> principal =
        BasePrincipal::CreateContentPrincipal(aHost, attrs);
    if (!contentParent->ValidatePrincipal(principal)) {
      continue;
    }

    // Register the key so the parent can filter proactive cookie pushes to
    // this content process.
    UpdateCookieInContentList(aHost, attrs);
    authorizedAttrsList.AppendElement(attrs);
  }

  nsTArray<RefPtr<Cookie>> foundCookieList;
  // Note: passing nullptr as aChannel to GetCookiesForURI() here is fine since
  // this argument is only used for proper reporting of cookie loads, but the
  // child process already does the necessary reporting in this case for us.
  mCookieService->GetCookiesForURI(
      aHost, nullptr, aIsForeign, aIsThirdPartyTrackingResource,
      aIsThirdPartySocialTrackingResource, aStorageAccessPermissionGranted,
      aRejectedReason, aIsSafeTopLevelNav, aIsSameSiteForeign,
      aHadCrossSiteRedirects, false, true, authorizedAttrsList,
      foundCookieList);

  nsTArray<CookieStructTable> matchingCookiesListTable;
  SerializeCookieListTable(foundCookieList, matchingCookiesListTable, aHost);

  aResolve(matchingCookiesListTable);

  return IPC_OK();
}

void CookieServiceParent::ActorDestroy(ActorDestroyReason aWhy) {
  // Nothing needed here. Called right before destructor since this is a
  // non-refcounted class.
}

IPCResult CookieServiceParent::RecvSetCookies(
    const nsCString& aBaseDomain, const OriginAttributes& aOriginAttributes,
    nsIURI* aHost, bool aIsThirdParty, const nsTArray<CookieStruct>& aCookies) {
  if (!aHost) {
    return IPC_FAIL(this, "aHost must not be null");
  }

  // Authorize the write if the process has already loaded this cookie key, or
  // could legitimately load this principal. The latter covers documents with no
  // channel registration (e.g. file://), which never populate the key set.
  auto* contentParent = static_cast<dom::ContentParent*>(Manager()->Manager());
  nsCOMPtr<nsIPrincipal> principal =
      BasePrincipal::CreateContentPrincipal(aHost, aOriginAttributes);
  if (!ContentProcessHasCookie(aBaseDomain, aOriginAttributes) &&
      !(contentParent && contentParent->ValidatePrincipal(principal))) {
    return IPC_FAIL(this, "Invalid set-cookie request from content process");
  }

  return SetCookies(aBaseDomain, aOriginAttributes, aHost, aIsThirdParty,
                    aCookies);
}

IPCResult CookieServiceParent::SetCookies(
    const nsCString& aBaseDomain, const OriginAttributes& aOriginAttributes,
    nsIURI* aHost, bool aIsThirdParty, const nsTArray<CookieStruct>& aCookies,
    dom::BrowsingContext* aBrowsingContext) {
  if (!mCookieService) {
    return IPC_OK();
  }

  // Deserialize URI. Having a host URI is mandatory and should always be
  // provided by the child; thus we consider failure fatal.
  if (!aHost) {
    return IPC_FAIL(this, "aHost must not be null");
  }

  // We set the cookie processing flag to true while processing this cookie
  // update, to make sure we don't send it back to the same content process.
  CookieProcessingGuard guard(this);

  nsICookieValidation::ValidationError error =
      mCookieService->SetCookiesFromIPC(aBaseDomain, aOriginAttributes, aHost,
                                        aIsThirdParty, aCookies,
                                        aBrowsingContext);
  MOZ_DIAGNOSTIC_ASSERT(error == nsICookieValidation::eOK);

  if (error != nsICookieValidation::eOK) {
    MOZ_LOG(gCookieLog, LogLevel::Warning,
            ("Invalid cookie submission from the content process: %d", error));
  }

  return IPC_OK();
}

}  // namespace net
}  // namespace mozilla
