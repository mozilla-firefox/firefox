/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CookieStoreParent.h"

#include "CookieStoreNotificationWatcher.h"
#include "CookieStoreSubscriptionService.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/Components.h"
#include "mozilla/Maybe.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/dom/ProcessIsolation.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "mozilla/ipc/BackgroundUtils.h"
#include "mozilla/ipc/URIUtils.h"  // for ParamTraits<nsIURI*>
#include "mozilla/net/Cookie.h"
#include "mozilla/net/CookieCommons.h"
#include "mozilla/net/CookieParser.h"
#include "mozilla/net/CookiePrefixes.h"
#include "mozilla/net/CookieServiceParent.h"
#include "mozilla/net/CookieValidation.h"
#include "mozilla/net/NeckoParent.h"
#include "nsICookieManager.h"
#include "nsICookieService.h"
#include "nsIEffectiveTLDService.h"
#include "nsNetUtil.h"
#include "nsProxyRelease.h"

using namespace mozilla::ipc;
using namespace mozilla::net;

namespace mozilla::dom {

namespace {

CookieServiceParent* GetCookieServiceParent(ContentParent* aContentParent) {
  AssertIsOnMainThread();

  PNeckoParent* neckoParent =
      LoneManagedOrNullAsserts(aContentParent->ManagedPNeckoParent());
  if (!neckoParent) {
    return nullptr;
  }

  return static_cast<CookieServiceParent*>(
      LoneManagedOrNullAsserts(neckoParent->ManagedPCookieServiceParent()));
}

bool CheckContentProcessSecurity(ThreadsafeContentParentHandle* aParent,
                                 const nsACString& aDomain,
                                 const RefPtr<nsIURI> aCookieURI,
                                 const OriginAttributes& aOriginAttributes) {
  AssertIsOnMainThread();

  // ContentParent is null if we are dealing with the same process.
  if (!aParent) {
    return true;
  }

  RefPtr<ContentParent> contentParent = aParent->GetContentParent();
  if (!contentParent) {
    return false;
  }

  if (CookieServiceParent* cs = GetCookieServiceParent(contentParent)) {
    return cs->ContentProcessHasCookie(aDomain, aOriginAttributes);
  }

  // No cookie service, so no key set to consult: a process hosting only a
  // service worker. Check the principal instead.
  nsCOMPtr<nsIPrincipal> principal =
      BasePrincipal::CreateContentPrincipal(aCookieURI, aOriginAttributes);
  return contentParent->ValidatePrincipal(principal);
}

// A process with no cookie service never receives cookie-changed, so it must
// not be asked to wait for it.
bool ContentProcessCanBeNotified(ThreadsafeContentParentHandle* aParent) {
  AssertIsOnMainThread();

  if (!aParent) {
    return true;
  }

  RefPtr<ContentParent> contentParent = aParent->GetContentParent();
  return contentParent && GetCookieServiceParent(contentParent);
}

bool SubscriptionPrincipalMatchesScope(nsIPrincipal* aPrincipal,
                                       const nsACString& aScopeURL) {
  nsCOMPtr<nsIURI> scopeURI;
  if (NS_WARN_IF(NS_FAILED(NS_NewURI(getter_AddRefs(scopeURI), aScopeURL)))) {
    return false;
  }

  return aPrincipal->IsSameOrigin(scopeURI);
}

}  // namespace

CookieStoreParent::CookieStoreParent() { AssertIsOnBackgroundThread(); }

CookieStoreParent::~CookieStoreParent() {
  CookieStoreNotificationWatcher::ReleaseOnMainThread(
      mNotificationWatcherOnMainThread.forget());
}

mozilla::ipc::IPCResult CookieStoreParent::RecvGetRequest(
    NotNull<RefPtr<nsIURI>> aCookieURI,
    const OriginAttributes& aOriginAttributes,
    const Maybe<OriginAttributes>& aPartitionedOriginAttributes,
    const bool& aThirdPartyContext, const bool& aPartitionForeign,
    const bool& aUsingStorageAccess, const bool& aIsOn3PCBExceptionList,
    const bool& aMatchName, const nsString& aName, const nsCString& aPath,
    const bool& aOnlyFirstMatch, GetRequestResolver&& aResolver) {
  AssertIsOnBackgroundThread();

  RefPtr<ThreadsafeContentParentHandle> parent =
      BackgroundParent::GetContentParentHandle(Manager());

  InvokeAsync(
      GetMainThreadSerialEventTarget(), __func__,
      [self = RefPtr(this), parent = RefPtr(parent), uri = aCookieURI.get(),
       aOriginAttributes, aPartitionedOriginAttributes, aThirdPartyContext,
       aPartitionForeign, aUsingStorageAccess, aIsOn3PCBExceptionList,
       aMatchName, aName, aPath, aOnlyFirstMatch]() {
        CopyableTArray<CookieStoreGetItem> results;
        self->GetRequestOnMainThread(
            parent, uri, aOriginAttributes, aPartitionedOriginAttributes,
            aThirdPartyContext, aPartitionForeign, aUsingStorageAccess,
            aIsOn3PCBExceptionList, aMatchName, aName, aPath, aOnlyFirstMatch,
            results);
        return GetRequestPromise::CreateAndResolve(std::move(results),
                                                   __func__);
      })
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [aResolver = std::move(aResolver)](
                 const GetRequestPromise::ResolveOrRejectValue& aResult) {
               MOZ_ASSERT(aResult.IsResolve());
               aResolver(aResult.ResolveValue());
             });

  return IPC_OK();
}

mozilla::ipc::IPCResult CookieStoreParent::RecvSetRequest(
    NotNull<RefPtr<nsIURI>> aCookieURI,
    const OriginAttributes& aOriginAttributes, const bool& aThirdPartyContext,
    const bool& aPartitionForeign, const bool& aUsingStorageAccess,
    const bool& aIsOn3PCBExceptionList, const nsString& aName,
    const nsString& aValue, const bool& aSession, const int64_t& aExpires,
    const nsString& aDomain, const nsString& aPath, const int32_t& aSameSite,
    const bool& aPartitioned, const nsID& aOperationID,
    SetRequestResolver&& aResolver) {
  AssertIsOnBackgroundThread();

  RefPtr<ThreadsafeContentParentHandle> parent =
      BackgroundParent::GetContentParentHandle(Manager());

  InvokeAsync(
      GetMainThreadSerialEventTarget(), __func__,
      [self = RefPtr(this), parent = RefPtr(parent), uri = aCookieURI.get(),
       aDomain, aOriginAttributes, aThirdPartyContext, aPartitionForeign,
       aUsingStorageAccess, aIsOn3PCBExceptionList, aName, aValue, aSession,
       aExpires, aPath, aSameSite, aPartitioned, aOperationID]() {
        bool waitForNotification = false;
        SetReturnType ret = self->SetRequestOnMainThread(
            parent, uri, aDomain, aOriginAttributes, aThirdPartyContext,
            aPartitionForeign, aUsingStorageAccess, aIsOn3PCBExceptionList,
            aName, aValue, aSession, aExpires, aPath, aSameSite, aPartitioned,
            aOperationID, waitForNotification);

        switch (ret) {
          case eFailure:
            return SetDeleteRequestPromise::CreateAndReject(false, __func__);

          case eSuccess:
            return SetDeleteRequestPromise::CreateAndResolve(
                waitForNotification, __func__);

          case eSilentFailure:
          default:
            return SetDeleteRequestPromise::CreateAndResolve(false, __func__);
        }
      })
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [aResolver = std::move(aResolver)](
                 const SetDeleteRequestPromise::ResolveOrRejectValue& aResult) {
               if (aResult.IsResolve()) {
                 aResolver(CookieStoreResult(true, aResult.ResolveValue()));
                 return;
               }

               aResolver(CookieStoreResult(false, false));
             });

  return IPC_OK();
}

mozilla::ipc::IPCResult CookieStoreParent::RecvDeleteRequest(
    NotNull<RefPtr<nsIURI>> aCookieURI,
    const OriginAttributes& aOriginAttributes, const bool& aThirdPartyContext,
    const bool& aPartitionForeign, const bool& aUsingStorageAccess,
    const bool& aIsOn3PCBExceptionList, const nsString& aName,
    const nsString& aDomain, const nsString& aPath, const bool& aPartitioned,
    const nsID& aOperationID, DeleteRequestResolver&& aResolver) {
  AssertIsOnBackgroundThread();

  RefPtr<ThreadsafeContentParentHandle> parent =
      BackgroundParent::GetContentParentHandle(Manager());

  InvokeAsync(
      GetMainThreadSerialEventTarget(), __func__,
      [self = RefPtr(this), parent = RefPtr(parent), uri = aCookieURI.get(),
       aDomain, aOriginAttributes, aThirdPartyContext, aPartitionForeign,
       aUsingStorageAccess, aIsOn3PCBExceptionList, aName, aPath, aPartitioned,
       aOperationID]() {
        bool waitForNotification = self->DeleteRequestOnMainThread(
            parent, uri, aDomain, aOriginAttributes, aThirdPartyContext,
            aPartitionForeign, aUsingStorageAccess, aIsOn3PCBExceptionList,
            aName, aPath, aPartitioned, aOperationID);
        return SetDeleteRequestPromise::CreateAndResolve(waitForNotification,
                                                         __func__);
      })
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [aResolver = std::move(aResolver)](
                 const SetDeleteRequestPromise::ResolveOrRejectValue& aResult) {
               MOZ_ASSERT(aResult.IsResolve());
               aResolver(aResult.ResolveValue());
             });
  return IPC_OK();
}

mozilla::ipc::IPCResult CookieStoreParent::RecvGetSubscriptionsRequest(
    const PrincipalInfo& aPrincipalInfo, const nsCString& aScopeURL,
    GetSubscriptionsRequestResolver&& aResolver) {
  AssertIsOnBackgroundThread();

  auto principalOrErr = PrincipalInfoToPrincipal(aPrincipalInfo);
  if (principalOrErr.isErr()) {
    return IPC_FAIL(this, "invalid PrincipalInfo");
  }
  nsCOMPtr<nsIPrincipal> principal = principalOrErr.unwrap();

  RefPtr<ThreadsafeContentParentHandle> parent =
      BackgroundParent::GetContentParentHandle(Manager());
  if (parent && !parent->ValidatePrincipal(principal)) {
    return IPC_FAIL(this, "principal not allowed for remote type");
  }

  if (!SubscriptionPrincipalMatchesScope(principal, aScopeURL)) {
    return IPC_FAIL(this, "principal not same-origin with scope");
  }

  InvokeAsync(GetMainThreadSerialEventTarget(), __func__,
              [self = RefPtr(this), aPrincipalInfo, aScopeURL]() {
                CookieStoreSubscriptionService* service =
                    CookieStoreSubscriptionService::Instance();
                if (!service) {
                  return GetSubscriptionsRequestPromise::CreateAndReject(
                      NS_ERROR_FAILURE, __func__);
                }

                nsTArray<CookieSubscription> subscriptions;
                service->GetSubscriptions(aPrincipalInfo, aScopeURL,
                                          subscriptions);

                return GetSubscriptionsRequestPromise::CreateAndResolve(
                    std::move(subscriptions), __func__);
              })
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [aResolver = std::move(aResolver)](
                 const GetSubscriptionsRequestPromise::ResolveOrRejectValue&
                     aResult) {
               if (aResult.IsResolve()) {
                 aResolver(aResult.ResolveValue());
                 return;
               }

               aResolver(nsTArray<CookieSubscription>());
             });

  return IPC_OK();
}

mozilla::ipc::IPCResult CookieStoreParent::RecvSubscribeOrUnsubscribeRequest(
    const PrincipalInfo& aPrincipalInfo, const nsCString& aScopeURL,
    const CopyableTArray<CookieSubscription>& aSubscriptions,
    bool aSubscription, SubscribeOrUnsubscribeRequestResolver&& aResolver) {
  AssertIsOnBackgroundThread();

  auto principalOrErr = PrincipalInfoToPrincipal(aPrincipalInfo);
  if (principalOrErr.isErr()) {
    return IPC_FAIL(this, "invalid PrincipalInfo");
  }
  nsCOMPtr<nsIPrincipal> principal = principalOrErr.unwrap();

  RefPtr<ThreadsafeContentParentHandle> parent =
      BackgroundParent::GetContentParentHandle(Manager());
  if (parent && !parent->ValidatePrincipal(principal)) {
    return IPC_FAIL(this, "principal not allowed for remote type");
  }

  if (!SubscriptionPrincipalMatchesScope(principal, aScopeURL)) {
    return IPC_FAIL(this, "principal not same-origin with scope");
  }

  InvokeAsync(GetMainThreadSerialEventTarget(), __func__,
              [self = RefPtr(this), aPrincipalInfo, aScopeURL, aSubscriptions,
               aSubscription]() {
                CookieStoreSubscriptionService* service =
                    CookieStoreSubscriptionService::Instance();
                if (!service) {
                  return SubscribeOrUnsubscribeRequestPromise::CreateAndReject(
                      NS_ERROR_FAILURE, __func__);
                }

                if (aSubscription) {
                  service->Subscribe(aPrincipalInfo, aScopeURL, aSubscriptions);
                } else {
                  service->Unsubscribe(aPrincipalInfo, aScopeURL,
                                       aSubscriptions);
                }

                return SubscribeOrUnsubscribeRequestPromise::CreateAndResolve(
                    true, __func__);
              })
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aResolver = std::move(aResolver)](
              const SubscribeOrUnsubscribeRequestPromise::ResolveOrRejectValue&
                  aResult) { aResolver(aResult.IsResolve()); });

  return IPC_OK();
}

mozilla::ipc::IPCResult CookieStoreParent::RecvClose() {
  AssertIsOnBackgroundThread();

  (void)Send__delete__(this);
  return IPC_OK();
}

void CookieStoreParent::GetRequestOnMainThread(
    ThreadsafeContentParentHandle* aParent, const RefPtr<nsIURI> aCookieURI,
    const OriginAttributes& aOriginAttributes,
    const Maybe<OriginAttributes>& aPartitionedOriginAttributes,
    bool aThirdPartyContext, bool aPartitionForeign, bool aUsingStorageAccess,
    bool aIsOn3PCBExceptionList, bool aMatchName, const nsAString& aName,
    const nsACString& aPath, bool aOnlyFirstMatch,
    nsTArray<CookieStoreGetItem>& aResults) {
  nsresult rv;
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsICookieService> service =
      do_GetService(NS_COOKIESERVICE_CONTRACTID);
  if (!service) {
    return;
  }

  nsAutoCString baseDomain;
  nsCOMPtr<nsIEffectiveTLDService> etld =
      mozilla::components::EffectiveTLD::Service();
  bool requireMatch = false;
  rv = CookieCommons::GetBaseDomain(etld, aCookieURI, baseDomain, requireMatch);
  if (NS_FAILED(rv)) {
    return;
  }

  if (!CheckContentProcessSecurity(aParent, baseDomain, aCookieURI,
                                   aOriginAttributes)) {
    return;
  }

  nsAutoCString hostName;
  rv = nsContentUtils::GetHostOrIPv6WithBrackets(aCookieURI, hostName);
  if (NS_FAILED(rv)) {
    return;
  }

  NS_ConvertUTF16toUTF8 matchName(aName);

  nsTArray<OriginAttributes> attrsList;
  attrsList.AppendElement(aOriginAttributes);

  if (aPartitionedOriginAttributes) {
    attrsList.AppendElement(aPartitionedOriginAttributes.value());
  }

  nsTArray<CookieStoreGetItem> list;

  bool hasBothPartitionedAndUnpartitioned =
      aPartitionedOriginAttributes.isSome();

  [[maybe_unused]] int64_t currentTimeInMSec = PR_Now() / PR_USEC_PER_MSEC;

  for (const OriginAttributes& attrs : attrsList) {
    nsTArray<RefPtr<Cookie>> cookies;
    service->GetCookiesFromHost(baseDomain, attrs, cookies);
    list.SetCapacity(list.Length() + cookies.Length());

    for (Cookie* cookie : cookies) {
      if (!CookieCommons::DomainMatches(cookie, hostName)) {
        continue;
      }
      if (cookie->IsHttpOnly()) {
        continue;
      }
      MOZ_DIAGNOSTIC_ASSERT(!cookie->IsExpired(currentTimeInMSec));

      if (aThirdPartyContext &&
          !CookieCommons::ShouldIncludeCrossSiteCookie(
              cookie, aCookieURI, aPartitionForeign, attrs.IsPrivateBrowsing(),
              aUsingStorageAccess, aIsOn3PCBExceptionList)) {
        continue;
      }

      if (aMatchName && !matchName.Equals(cookie->Name())) {
        continue;
      }

      if (!net::CookieCommons::PathMatches(cookie->Path(), aPath)) {
        continue;
      }

      // Skipping sending TCP cookies when the page has StorageAccess if
      // configured so that CHIPS doesn't affect TCP.
      if (!StaticPrefs::network_cookie_CHIPS_affectsTCP() &&
          hasBothPartitionedAndUnpartitioned &&
          !attrs.mPartitionKey.IsEmpty() && !cookie->RawIsPartitioned()) {
        continue;
      }

      list.AppendElement(CookieStoreGetItem(cookie->Name(), cookie->Value()));

      if (aOnlyFirstMatch) {
        break;
      }
    }

    if (!list.IsEmpty() && aOnlyFirstMatch) {
      break;
    }
  }

  aResults.SwapElements(list);
}

CookieStoreParent::SetReturnType CookieStoreParent::SetRequestOnMainThread(
    ThreadsafeContentParentHandle* aParent, const RefPtr<nsIURI> aCookieURI,
    const nsAString& aDomain, const OriginAttributes& aOriginAttributes,
    bool aThirdPartyContext, bool aPartitionForeign, bool aUsingStorageAccess,
    bool aIsOn3PCBExceptionList, const nsAString& aName,
    const nsAString& aValue, bool aSession, int64_t aExpires,
    const nsAString& aPath, int32_t aSameSite, bool aPartitioned,
    const nsID& aOperationID, bool& aWaitForNotification) {
  AssertIsOnMainThread();
  nsresult rv;

  // By default, no notification should be expected.
  aWaitForNotification = false;

  nsAutoCString domain = NS_ConvertUTF16toUTF8(aDomain);
  nsAutoCString domainWithDot;

  if (CookiePrefixes::Has(CookiePrefixes::eHttp, aName) ||
      CookiePrefixes::Has(CookiePrefixes::eHostHttp, aName)) {
    MOZ_DIAGNOSTIC_CRASH("This should not be allowed by CookieStore");
    return eSilentFailure;
  }

  if (CookiePrefixes::Has(CookiePrefixes::eHost, aName) && !domain.IsEmpty()) {
    MOZ_DIAGNOSTIC_CRASH("This should not be allowed by CookieStore");
    return eSilentFailure;
  }

  // Determine whether aCookieURI is hosted on something that requires an
  // exact host match (IP literal, single-label host such as `localhost`, or
  // a public suffix). The cookie service cannot store a domain cookie for
  // those, so we must not prepend a leading dot.
  nsCOMPtr<nsIEffectiveTLDService> etld =
      mozilla::components::EffectiveTLD::Service();
  nsAutoCString baseDomain;
  bool requireHostMatch = false;
  rv = CookieCommons::GetBaseDomain(etld, aCookieURI, baseDomain,
                                    requireHostMatch);
  if (NS_FAILED(rv)) {
    return eSilentFailure;
  }

  // If aDomain is `domain.com` then domainWithDot will be `.domain.com`.
  // When aDomain is empty, domain and domainWithDot both fall back to the
  // host of aCookieURI (a host-only cookie).
  if (domain.IsEmpty()) {
    rv = nsContentUtils::GetHostOrIPv6WithBrackets(aCookieURI, domain);
    if (NS_FAILED(rv)) {
      return eSilentFailure;
    }
  } else if (!requireHostMatch) {
    domainWithDot.Insert('.', 0);
  }
  domainWithDot.Append(domain);

  if (!CheckContentProcessSecurity(aParent, domain, aCookieURI,
                                   aOriginAttributes)) {
    return eSilentFailure;
  }

  if (aThirdPartyContext &&
      !CookieCommons::ShouldIncludeCrossSiteCookie(
          aCookieURI, aSameSite,
          aPartitioned && !aOriginAttributes.mPartitionKey.IsEmpty(),
          aPartitionForeign, aOriginAttributes.IsPrivateBrowsing(),
          aUsingStorageAccess, aIsOn3PCBExceptionList)) {
    return eSilentFailure;
  }

  nsCOMPtr<nsICookieManager> service =
      do_GetService(NS_COOKIEMANAGER_CONTRACTID);
  if (!service) {
    return eSilentFailure;
  }

  bool notified = false;
  auto notificationCb = [&]() { notified = true; };

  CookieStoreNotificationWatcher* notificationWatcher =
      GetOrCreateNotificationWatcherOnMainThread(aOriginAttributes);
  if (!notificationWatcher) {
    return eSilentFailure;
  }

  notificationWatcher->CallbackWhenNotified(aOperationID, notificationCb);

  auto cleanupNotificationWatcher = MakeScopeExit(
      [&]() { notificationWatcher->ForgetOperationID(aOperationID); });

  OriginAttributes attrs(aOriginAttributes);

  nsCOMPtr<nsICookieValidation> validation;
  rv = service->AddNative(
      aCookieURI, domainWithDot, NS_ConvertUTF16toUTF8(aPath),
      NS_ConvertUTF16toUTF8(aName), NS_ConvertUTF16toUTF8(aValue),
      /* secure: */ true,
      /* http-only: */ false, aSession, aSession ? INT64_MAX : aExpires, &attrs,
      aSameSite, nsICookie::SCHEME_HTTPS, aPartitioned, /* from http: */ false,
      &aOperationID, getter_AddRefs(validation));

  if (NS_WARN_IF(NS_FAILED(rv))) {
    if (rv == NS_ERROR_ILLEGAL_VALUE && validation &&
        CookieValidation::Cast(validation)->Result() !=
            nsICookieValidation::eOK) {
      return eFailure;
    }

    return eSilentFailure;
  }

  aWaitForNotification = notified && ContentProcessCanBeNotified(aParent);
  return eSuccess;
}

bool CookieStoreParent::DeleteRequestOnMainThread(
    ThreadsafeContentParentHandle* aParent, const RefPtr<nsIURI> aCookieURI,
    const nsAString& aDomain, const OriginAttributes& aOriginAttributes,
    bool aThirdPartyContext, bool aPartitionForeign, bool aUsingStorageAccess,
    bool aIsOn3PCBExceptionList, const nsAString& aName, const nsAString& aPath,
    bool aPartitioned, const nsID& aOperationID) {
  MOZ_ASSERT(NS_IsMainThread());
  nsresult rv;

  nsAutoCString baseDomain;
  nsCOMPtr<nsIEffectiveTLDService> etld =
      mozilla::components::EffectiveTLD::Service();
  bool requireMatch = false;
  rv = CookieCommons::GetBaseDomain(etld, aCookieURI, baseDomain, requireMatch);
  if (NS_FAILED(rv)) {
    return false;
  }

  nsAutoCString hostName;
  nsContentUtils::GetHostOrIPv6WithBrackets(aCookieURI, hostName);

  // A cookie set without a domain attribute is host-only; one set with a domain
  // attribute never is, even when the attribute equals the host.
  const bool hostOnly = aDomain.IsEmpty();

  nsAutoCString cookiesForDomain;
  if (hostOnly) {
    cookiesForDomain = std::move(hostName);
  } else {
    cookiesForDomain = NS_ConvertUTF16toUTF8(aDomain);
  }

  if (!CheckContentProcessSecurity(aParent, cookiesForDomain, aCookieURI,
                                   aOriginAttributes)) {
    return false;
  }

  nsCOMPtr<nsICookieService> service =
      do_GetService(NS_COOKIESERVICE_CONTRACTID);
  if (!service) {
    return false;
  }
  nsCOMPtr<nsICookieManager> cookieManager = do_QueryInterface(service);

  NS_ConvertUTF16toUTF8 matchName(aName);
  NS_ConvertUTF16toUTF8 matchPath(aPath);

  [[maybe_unused]] int64_t currentTimeInMSec = PR_Now() / PR_USEC_PER_MSEC;

  nsTArray<RefPtr<Cookie>> cookies;
  OriginAttributes attrs(aOriginAttributes);
  service->GetCookiesFromHost(baseDomain, attrs, cookies);

  for (Cookie* cookie : cookies) {
    MOZ_ASSERT(cookie);
    MOZ_DIAGNOSTIC_ASSERT(!cookie->IsExpired(currentTimeInMSec));
    if (!matchName.Equals(cookie->Name())) {
      continue;
    }
    // "Delete a cookie" is defined as "set a cookie" with a max-age of 0, so
    // the target is the single cookie the storage model identifies by name,
    // domain, host-only-flag and path.
    const bool cookieIsHostOnly = !cookie->IsDomain();
    if (cookieIsHostOnly != hostOnly ||
        !cookie->RawHost().Equals(cookiesForDomain)) {
      continue;
    }

    if (!matchPath.IsEmpty() && !matchPath.Equals(cookie->Path())) {
      continue;
    }

    if (cookie->IsPartitioned() != aPartitioned) continue;

    if (aThirdPartyContext) {
      int32_t sameSiteAttr = cookie->SameSite();

      if (!CookieCommons::ShouldIncludeCrossSiteCookie(
              aCookieURI, sameSiteAttr,
              aPartitioned && !aOriginAttributes.mPartitionKey.IsEmpty(),
              aPartitionForeign, attrs.IsPrivateBrowsing(), aUsingStorageAccess,
              aIsOn3PCBExceptionList)) {
        return false;
      }
    }

    bool notified = false;
    auto notificationCb = [&]() { notified = true; };

    CookieStoreNotificationWatcher* notificationWatcher =
        GetOrCreateNotificationWatcherOnMainThread(aOriginAttributes);
    if (!notificationWatcher) {
      return false;
    }

    notificationWatcher->CallbackWhenNotified(aOperationID, notificationCb);

    auto cleanupNotificationWatcher = MakeScopeExit(
        [&]() { notificationWatcher->ForgetOperationID(aOperationID); });

    rv = cookieManager->RemoveNative(cookie->Host(), matchName, cookie->Path(),
                                     &attrs, /* from http: */ false,
                                     &aOperationID);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return false;
    }

    return notified && ContentProcessCanBeNotified(aParent);
  }

  return false;
}

CookieStoreNotificationWatcher*
CookieStoreParent::GetOrCreateNotificationWatcherOnMainThread(
    const OriginAttributes& aOriginAttributes) {
  MOZ_ASSERT(NS_IsMainThread());

  if (!mNotificationWatcherOnMainThread) {
    mNotificationWatcherOnMainThread = CookieStoreNotificationWatcher::Create(
        aOriginAttributes.IsPrivateBrowsing());
  }

  return mNotificationWatcherOnMainThread;
}

}  // namespace mozilla::dom
