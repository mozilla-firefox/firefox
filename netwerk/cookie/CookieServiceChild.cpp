/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/net/CookieServiceChild.h"

#include "Cookie.h"
#include "CookieCommons.h"
#include "CookieLogging.h"
#include "CookieNotification.h"
#include "CookieParser.h"
#include "CookieService.h"
#include "ErrorList.h"
#include "ThirdPartyUtil.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/ConsoleReportCollector.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/StoragePrincipalHelper.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/ContentChild.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/WindowGlobalChild.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "mozilla/ipc/URIUtils.h"
#include "mozilla/net/HttpChannelChild.h"
#include "mozilla/net/NeckoChannelParams.h"
#include "mozilla/net/NeckoChild.h"
#include "nsIChannel.h"
#include "nsIClassifiedChannel.h"
#include "nsIConsoleReportCollector.h"
#include "nsICookieJarSettings.h"
#include "nsIEffectiveTLDService.h"
#include "nsIHttpChannel.h"
#include "nsIPrefBranch.h"
#include "nsIScriptSecurityManager.h"
#include "nsIURI.h"
#include "nsIWebProgressListener.h"
#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsQueryObject.h"
#include "nsServiceManagerUtils.h"

using namespace mozilla::ipc;

namespace mozilla {
namespace net {

static StaticRefPtr<CookieServiceChild> gCookieChildService;

already_AddRefed<CookieServiceChild> CookieServiceChild::GetSingleton() {
  if (!gCookieChildService) {
    gCookieChildService = new CookieServiceChild();
    gCookieChildService->Init();
    ClearOnShutdown(&gCookieChildService);
  }

  return do_AddRef(gCookieChildService);
}

NS_IMPL_ISUPPORTS(CookieServiceChild, nsICookieService,
                  nsISupportsWeakReference)

CookieServiceChild::CookieServiceChild() { NeckoChild::InitNeckoChild(); }

CookieServiceChild::~CookieServiceChild() { gCookieChildService = nullptr; }

void CookieServiceChild::Init() {
  auto* cc = mozilla::ipc::ActorCast<mozilla::dom::ContentChild>(
      gNeckoChild->Manager());
  if (cc->IsShuttingDown()) {
    return;
  }

  // This corresponds to Release() in DeallocPCookieService.
  NS_ADDREF_THIS();

  // Create a child PCookieService actor. Don't do this in the constructor
  // since it could release 'this' on failure
  gNeckoChild->SendPCookieServiceConstructor(this);

  mThirdPartyUtil = ThirdPartyUtil::GetInstance();
  NS_ASSERTION(mThirdPartyUtil, "couldn't get ThirdPartyUtil service");

  mTLDService = do_GetService(NS_EFFECTIVETLDSERVICE_CONTRACTID);
  NS_ASSERTION(mTLDService, "couldn't get TLDService");
}

RefPtr<GenericPromise> CookieServiceChild::TrackCookieLoad(
    nsIChannel* aChannel) {
  if (!CanSend()) {
    return GenericPromise::CreateAndReject(NS_ERROR_NOT_AVAILABLE, __func__);
  }

  uint32_t rejectedReason = 0;
  ThirdPartyAnalysisResult result = mThirdPartyUtil->AnalyzeChannel(
      aChannel, true, nullptr, RequireThirdPartyCheck, &rejectedReason);

  nsCOMPtr<nsIURI> uri;
  aChannel->GetURI(getter_AddRefs(uri));
  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();

  OriginAttributes storageOriginAttributes = loadInfo->GetOriginAttributes();
  StoragePrincipalHelper::PrepareEffectiveStoragePrincipalOriginAttributes(
      aChannel, storageOriginAttributes);

  bool isSafeTopLevelNav = CookieCommons::IsSafeTopLevelNav(aChannel);
  bool hadCrossSiteRedirects = false;
  bool isSameSiteForeign =
      CookieCommons::IsSameSiteForeign(aChannel, uri, &hadCrossSiteRedirects);

  RefPtr<CookieServiceChild> self(this);

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
    // Add the partitioned principal to principals.
    OriginAttributes partitionedOriginAttributes;
    StoragePrincipalHelper::GetOriginAttributes(
        aChannel, partitionedOriginAttributes,
        StoragePrincipalHelper::ePartitionedPrincipal);
    originAttributesList.AppendElement(partitionedOriginAttributes);
    // Only append the partitioned originAttributes if the partitionKey is set.
    // The partitionKey could be empty for partitionKey in partitioned
    // originAttributes if the channel is for privilege request, such as
    // extension's requests.
    if (!partitionedOriginAttributes.mPartitionKey.IsEmpty()) {
      originAttributesList.AppendElement(partitionedOriginAttributes);
    }
  }

  return SendGetCookieList(
             uri, result.contains(ThirdPartyAnalysis::IsForeign),
             result.contains(ThirdPartyAnalysis::IsThirdPartyTrackingResource),
             result.contains(
                 ThirdPartyAnalysis::IsThirdPartySocialTrackingResource),
             result.contains(
                 ThirdPartyAnalysis::IsStorageAccessPermissionGranted),
             rejectedReason, isSafeTopLevelNav, isSameSiteForeign,
             hadCrossSiteRedirects, originAttributesList)
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self, uri](const nsTArray<CookieStructTable>& aCookiesListTable) {
            for (auto& entry : aCookiesListTable) {
              auto& cookies = entry.cookies();
              for (auto& cookieEntry : cookies) {
                RefPtr<Cookie> cookie =
                    Cookie::Create(cookieEntry, entry.attrs());
                cookie->SetIsHttpOnly(false);
                self->RecordDocumentCookie(cookie, entry.attrs());
              }
            }
            return GenericPromise::CreateAndResolve(true, __func__);
          },
          [](const mozilla::ipc::ResponseRejectReason) {
            return GenericPromise::CreateAndReject(NS_ERROR_FAILURE, __func__);
          });
}

IPCResult CookieServiceChild::RecvRemoveAll() {
  mCookiesMap.Clear();

  nsCOMPtr<nsIObserverService> obsService = services::GetObserverService();
  if (obsService) {
    obsService->NotifyObservers(nullptr, "content-removed-all-cookies",
                                nullptr);
  }
  return IPC_OK();
}

IPCResult CookieServiceChild::RecvRemoveCookie(
    const CookieStruct& aCookie, const OriginAttributes& aAttrs,
    const Maybe<nsID>& aOperationID) {
  RemoveSingleCookie(aCookie, aAttrs, aOperationID);

  nsCOMPtr<nsIObserverService> obsService = services::GetObserverService();
  if (obsService) {
    obsService->NotifyObservers(nullptr, "content-removed-cookie", nullptr);
  }
  return IPC_OK();
}

void CookieServiceChild::RemoveSingleCookie(const CookieStruct& aCookie,
                                            const OriginAttributes& aAttrs,
                                            const Maybe<nsID>& aOperationID) {
  nsCString baseDomain;
  if (NS_FAILED(CookieCommons::GetBaseDomainFromHost(
          mTLDService, aCookie.host(), baseDomain))) {
    MOZ_ASSERT(false,
               "CookieServiceChild::RemoveSingleCookie - GetBaseDomainFromHost "
               "shouldn't fail");
    return;
  }
  CookieKey key(baseDomain, aAttrs);
  CookiesList* cookiesList = nullptr;
  mCookiesMap.Get(key, &cookiesList);

  if (!cookiesList) {
    return;
  }

  uint32_t targetHash =
      Cookie::ComputeKeyHash(aCookie.name(), aCookie.host(), aCookie.path());
  for (uint32_t i = 0; i < cookiesList->Length(); i++) {
    RefPtr<Cookie> cookie = cookiesList->ElementAt(i);
    // bug 1858366: In the case that we are updating a stale cookie
    // from the content process: the parent process will signal
    // a batch deletion for the old cookie.
    // When received by the content process we should not remove
    // the new cookie since we have already updated the content
    // process cookies. So we also check the expiry here.
    if (cookie->KeyHash() == targetHash &&
        cookie->Name().Equals(aCookie.name()) &&
        cookie->Host().Equals(aCookie.host()) &&
        cookie->Path().Equals(aCookie.path()) &&
        cookie->ExpiryInMSec() <= aCookie.expiryInMSec()) {
      cookiesList->RemoveElementAt(i);
      NotifyObservers(cookie, aAttrs, CookieNotificationAction::CookieDeleted,
                      aOperationID);
      break;
    }
  }
}

IPCResult CookieServiceChild::RecvAddCookie(const CookieStruct& aCookie,
                                            const OriginAttributes& aAttrs,
                                            const Maybe<nsID>& aOperationID) {
  RefPtr<Cookie> cookie = Cookie::Create(aCookie, aAttrs);

  CookieNotificationAction action = RecordDocumentCookie(cookie, aAttrs);
  NotifyObservers(cookie, aAttrs, action, aOperationID);

  // signal test code to check their cookie list
  nsCOMPtr<nsIObserverService> obsService = services::GetObserverService();
  if (obsService) {
    obsService->NotifyObservers(nullptr, "content-added-cookie", nullptr);
  }

  return IPC_OK();
}

IPCResult CookieServiceChild::RecvRemoveBatchDeletedCookies(
    nsTArray<CookieStruct>&& aCookiesList,
    nsTArray<OriginAttributes>&& aAttrsList) {
  MOZ_ASSERT(aCookiesList.Length() == aAttrsList.Length());
  for (uint32_t i = 0; i < aCookiesList.Length(); i++) {
    const CookieStruct& cookieStruct = aCookiesList.ElementAt(i);
    RemoveSingleCookie(cookieStruct, aAttrsList.ElementAt(i), Nothing());
  }

  nsCOMPtr<nsIObserverService> obsService = services::GetObserverService();
  if (obsService) {
    obsService->NotifyObservers(nullptr, "content-batch-deleted-cookies",
                                nullptr);
  }
  return IPC_OK();
}

IPCResult CookieServiceChild::RecvTrackCookiesLoad(
    nsTArray<CookieStructTable>&& aCookiesListTable) {
  for (auto& entry : aCookiesListTable) {
    for (auto& cookieEntry : entry.cookies()) {
      RefPtr<Cookie> cookie = Cookie::Create(cookieEntry, entry.attrs());
      cookie->SetIsHttpOnly(false);
      RecordDocumentCookie(cookie, entry.attrs());
    }
  }

  nsCOMPtr<nsIObserverService> obsService = services::GetObserverService();
  if (obsService) {
    obsService->NotifyObservers(nullptr, "content-track-cookies-loaded",
                                nullptr);
  }

  return IPC_OK();
}

/* static */ bool CookieServiceChild::RequireThirdPartyCheck(
    nsILoadInfo* aLoadInfo) {
  if (!aLoadInfo) {
    return false;
  }

  nsCOMPtr<nsICookieJarSettings> cookieJarSettings;
  nsresult rv =
      aLoadInfo->GetCookieJarSettings(getter_AddRefs(cookieJarSettings));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return false;
  }

  uint32_t cookieBehavior = cookieJarSettings->GetCookieBehavior();
  return cookieBehavior == nsICookieService::BEHAVIOR_REJECT_FOREIGN ||
         cookieBehavior == nsICookieService::BEHAVIOR_LIMIT_FOREIGN ||
         cookieBehavior == nsICookieService::BEHAVIOR_REJECT_TRACKER ||
         cookieBehavior == nsICookieService::BEHAVIOR_PARTITION_FOREIGN;
}

CookieServiceChild::CookieNotificationAction
CookieServiceChild::RecordDocumentCookie(Cookie* aCookie,
                                         const OriginAttributes& aAttrs) {
  nsAutoCString baseDomain;
  if (NS_FAILED(CookieCommons::GetBaseDomainFromHost(
          mTLDService, aCookie->Host(), baseDomain))) {
    MOZ_ASSERT(false,
               "CookieServiceChild::RecordDocumentCookie - "
               "GetBaseDomainFromHost shouldn't fail");
    return CookieNotificationAction::NoActionNeeded;
  }

  if (CookieCommons::IsFirstPartyPartitionedCookieWithoutCHIPS(
          aCookie, baseDomain, aAttrs)) {
    COOKIE_LOGSTRING(LogLevel::Error,
                     ("Invalid first-party partitioned cookie without "
                      "partitioned cookie attribution from the document."));
    MOZ_ASSERT(false);
    return CookieNotificationAction::NoActionNeeded;
  }

  CookieKey key(baseDomain, aAttrs);
  CookiesList* cookiesList = nullptr;
  mCookiesMap.Get(key, &cookiesList);

  if (!cookiesList) {
    cookiesList = mCookiesMap.GetOrInsertNew(key);
  }

  bool cookieFound = false;

  for (uint32_t i = 0; i < cookiesList->Length(); i++) {
    Cookie* cookie = cookiesList->ElementAt(i);
    if (cookie->KeyHash() == aCookie->KeyHash() &&
        cookie->Name().Equals(aCookie->Name()) &&
        cookie->Host().Equals(aCookie->Host()) &&
        cookie->Path().Equals(aCookie->Path())) {
      if (cookie->Value().Equals(aCookie->Value()) &&
          cookie->ExpiryInMSec() == aCookie->ExpiryInMSec() &&
          cookie->IsSecure() == aCookie->IsSecure() &&
          cookie->SameSite() == aCookie->SameSite() &&
          cookie->IsSession() == aCookie->IsSession() &&
          cookie->IsHttpOnly() == aCookie->IsHttpOnly()) {
        cookie->SetLastAccessedInUSec(aCookie->LastAccessedInUSec());
        return CookieNotificationAction::NoActionNeeded;
      }
      cookiesList->RemoveElementAt(i);
      cookieFound = true;
      break;
    }
  }

  int64_t currentTimeInMSec = PR_Now() / PR_USEC_PER_MSEC;
  if (aCookie->ExpiryInMSec() <= currentTimeInMSec) {
    return cookieFound ? CookieNotificationAction::CookieDeleted
                       : CookieNotificationAction::NoActionNeeded;
  }

  cookiesList->AppendElement(aCookie);
  return cookieFound ? CookieNotificationAction::CookieChanged
                     : CookieNotificationAction::CookieAdded;
}

NS_IMETHODIMP
CookieServiceChild::GetCookieStringFromHttp(nsIURI* /*aHostURI*/,
                                            nsIChannel* /*aChannel*/,
                                            nsACString& /*aCookieString*/) {
  MOZ_CRASH("This method should not be called");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
CookieServiceChild::SetCookieStringFromHttp(nsIURI* aHostURI,
                                            const nsACString& aCookieString,
                                            nsIChannel* aChannel) {
  MOZ_CRASH("This method should not be called");
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
CookieServiceChild::RunInTransaction(
    nsICookieTransactionCallback* /*aCallback*/) {
  return NS_ERROR_NOT_IMPLEMENTED;
}

void CookieServiceChild::GetCookiesFromHost(
    const nsACString& aBaseDomain, const OriginAttributes& aOriginAttributes,
    nsTArray<RefPtr<Cookie>>& aCookies) {
  CookieKey key(aBaseDomain, aOriginAttributes);

  CookiesList* cookiesList = nullptr;
  mCookiesMap.Get(key, &cookiesList);

  if (cookiesList) {
    aCookies.AppendElements(*cookiesList);
  }
}

void CookieServiceChild::StaleCookies(const nsTArray<RefPtr<Cookie>>& aCookies,
                                      int64_t aCurrentTimeInUsec) {
  // Nothing to do here.
}

bool CookieServiceChild::HasExistingCookies(
    const nsACString& aBaseDomain, const OriginAttributes& aOriginAttributes) {
  CookiesList* cookiesList = nullptr;

  CookieKey key(aBaseDomain, aOriginAttributes);
  mCookiesMap.Get(key, &cookiesList);

  return cookiesList ? cookiesList->Length() : false;
}

void CookieServiceChild::AddCookieFromDocument(
    CookieParser& aCookieParser, const nsACString& aBaseDomain,
    const OriginAttributes& aOriginAttributes, Cookie& aCookie,
    int64_t aCurrentTimeInUsec, nsIURI* aDocumentURI, bool aThirdParty,
    dom::Document* aDocument) {
  MOZ_ASSERT(aDocumentURI);
  MOZ_ASSERT(aDocument);

  CookieKey key(aBaseDomain, aOriginAttributes);
  CookiesList* cookies = mCookiesMap.Get(key);

  if (cookies) {
    // We need to see if the cookie we're setting would overwrite an httponly
    // or a secure one. This would not affect anything we send over the net
    // (those come from the parent, which already checks this),
    // but script could see an inconsistent view of things.

    // CHIPS - If the cookie has the "Partitioned" attribute set it will be
    // stored in the partitioned cookie jar.
    bool needPartitioned = StaticPrefs::network_cookie_CHIPS_enabled() &&
                           aCookie.RawIsPartitioned();
    nsCOMPtr<nsIPrincipal> principal =
        needPartitioned ? aDocument->PartitionedPrincipal()
                        : aDocument->EffectiveCookiePrincipal();
    bool isPotentiallyTrustworthy =
        principal->GetIsOriginPotentiallyTrustworthy();

    for (uint32_t i = 0; i < cookies->Length(); ++i) {
      RefPtr<Cookie> existingCookie = cookies->ElementAt(i);
      if (existingCookie->KeyHash() == aCookie.KeyHash() &&
          existingCookie->Name().Equals(aCookie.Name()) &&
          existingCookie->Host().Equals(aCookie.Host()) &&
          existingCookie->Path().Equals(aCookie.Path())) {
        // Can't overwrite an httponly cookie from a script context.
        if (existingCookie->IsHttpOnly()) {
          return;
        }

        // prevent insecure cookie from overwriting a secure one in insecure
        // context.
        if (existingCookie->IsSecure() && !isPotentiallyTrustworthy) {
          return;
        }
      }
    }
  }

  CookieNotificationAction action =
      RecordDocumentCookie(&aCookie, aOriginAttributes);
  NotifyObservers(&aCookie, aOriginAttributes, action);

  if (CanSend()) {
    nsTArray<CookieStruct> cookiesToSend;
    cookiesToSend.AppendElement(aCookie.ToIPC());

    // Asynchronously call the parent.
    dom::WindowGlobalChild* windowGlobalChild =
        aDocument->GetWindowGlobalChild();

    // If there is no WindowGlobalChild fall back to PCookieService SetCookies.
    if (NS_WARN_IF(!windowGlobalChild)) {
      SendSetCookies(aBaseDomain, aOriginAttributes, aDocumentURI, aThirdParty,
                     cookiesToSend);
      return;
    }

    windowGlobalChild->SendSetCookies(aBaseDomain, aOriginAttributes,
                                      aDocumentURI, aThirdParty, cookiesToSend);
  }
}

void CookieServiceChild::NotifyObservers(Cookie* aCookie,
                                         const OriginAttributes& aAttrs,
                                         CookieNotificationAction aAction,
                                         const Maybe<nsID>& aOperationID) {
  nsICookieNotification::Action notificationAction;
  switch (aAction) {
    case CookieNotificationAction::NoActionNeeded:
      return;

    case CookieNotificationAction::CookieAdded:
      notificationAction = nsICookieNotification::COOKIE_ADDED;
      break;

    case CookieNotificationAction::CookieChanged:
      notificationAction = nsICookieNotification::COOKIE_CHANGED;
      break;

    case CookieNotificationAction::CookieDeleted:
      notificationAction = nsICookieNotification::COOKIE_DELETED;
      break;
  }

  nsCOMPtr<nsIObserverService> os = services::GetObserverService();
  if (!os) {
    return;
  }

  nsAutoCString baseDomain;
  if (NS_FAILED(CookieCommons::GetBaseDomainFromHost(
          mTLDService, aCookie->Host(), baseDomain))) {
    MOZ_ASSERT(false,
               "CookieServiceChild::NotifyObservers - GetBaseDomainFromHost "
               "shouldn't fail");
    return;
  }

  nsCOMPtr<nsICookieNotification> notification =
      new CookieNotification(notificationAction, aCookie, baseDomain, false,
                             nullptr, 0, aOperationID.ptrOr(nullptr));

  os->NotifyObservers(
      notification,
      aAttrs.IsPrivateBrowsing() ? "private-cookie-changed" : "cookie-changed",
      u"");
}

}  // namespace net
}  // namespace mozilla
