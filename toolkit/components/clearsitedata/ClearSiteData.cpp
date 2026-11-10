/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ClearSiteData.h"

#include "mozilla/net/HttpBaseChannel.h"
#include "mozilla/OriginAttributes.h"
#include "mozilla/Preferences.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/dom/WindowGlobalParent.h"
#include "nsASCIIMask.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsContentSecurityManager.h"
#include "nsContentUtils.h"
#include "nsIClearDataService.h"
#include "nsIHttpChannel.h"
#include "nsIHttpProtocolHandler.h"
#include "nsIObserverService.h"
#include "nsIPrincipal.h"
#include "nsIScriptError.h"
#include "nsIScriptSecurityManager.h"
#include "nsNetUtil.h"
#include "mozilla/Logging.h"

using namespace mozilla;

LazyLogModule gClearSiteDataLog("ClearSiteData");

#define LOG(args) MOZ_LOG(gClearSiteDataLog, mozilla::LogLevel::Debug, args)
#define CLEAR_SITE_DATA_TOPIC "clear-site-data"

namespace {

StaticRefPtr<ClearSiteData> gClearSiteData;

}  // namespace

// This object is used to suspend/resume the channel.
class ClearSiteData::PendingCleanupHolder final : public nsIClearDataCallback {
 public:
  NS_DECL_ISUPPORTS

  explicit PendingCleanupHolder(nsIHttpChannel* aChannel)
      : mChannel(aChannel), mNumPendingClear(0) {
    MOZ_ASSERT(aChannel);
  }

  nsresult Start(uint32_t aNumPendingClear) {
    MOZ_ASSERT(aNumPendingClear > 0);
    MOZ_ASSERT(mNumPendingClear == 0);
    nsresult rv = mChannel->Suspend();
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }
    mNumPendingClear = aNumPendingClear;

    return NS_OK;
  }

  // nsIClearDataCallback interface

  NS_IMETHOD
  OnDataDeleted(uint32_t aFailedFlags) override {
    MOZ_ASSERT(mNumPendingClear != 0);
    mNumPendingClear -= 1;

    if (mNumPendingClear == 0) {
      MOZ_ASSERT(mChannel);
      mChannel->Resume();
      mChannel = nullptr;
    }

    return NS_OK;
  }

 private:
  ~PendingCleanupHolder() {
    if (mNumPendingClear != 0) {
      mChannel->Resume();
    }
  }

  nsCOMPtr<nsIHttpChannel> mChannel;
  uint32_t mNumPendingClear;
};

NS_INTERFACE_MAP_BEGIN(ClearSiteData::PendingCleanupHolder)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIClearDataCallback)
  NS_INTERFACE_MAP_ENTRY(nsIClearDataCallback)
NS_INTERFACE_MAP_END

NS_IMPL_ADDREF(ClearSiteData::PendingCleanupHolder)
NS_IMPL_RELEASE(ClearSiteData::PendingCleanupHolder)

/* static */
void ClearSiteData::Initialize() {
  MOZ_ASSERT(!gClearSiteData);
  MOZ_ASSERT(NS_IsMainThread());

  if (!XRE_IsParentProcess()) {
    return;
  }

  RefPtr<ClearSiteData> service = new ClearSiteData();

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (NS_WARN_IF(!obs)) {
    return;
  }

  obs->AddObserver(service, CLEAR_SITE_DATA_TOPIC, false);
  obs->AddObserver(service, NS_XPCOM_SHUTDOWN_OBSERVER_ID, false);
  gClearSiteData = service;
}

/* static */
void ClearSiteData::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());

  if (!gClearSiteData) {
    return;
  }

  RefPtr<ClearSiteData> service = gClearSiteData;
  gClearSiteData = nullptr;

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (NS_WARN_IF(!obs)) {
    return;
  }

  obs->RemoveObserver(service, CLEAR_SITE_DATA_TOPIC);
  obs->RemoveObserver(service, NS_XPCOM_SHUTDOWN_OBSERVER_ID);
}

ClearSiteData::ClearSiteData() = default;

NS_IMETHODIMP
ClearSiteData::Observe(nsISupports* aSubject, const char* aTopic,
                       const char16_t* aData) {
  if (!strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID)) {
    Shutdown();
    return NS_OK;
  }

  MOZ_ASSERT(!strcmp(aTopic, CLEAR_SITE_DATA_TOPIC));

  nsCOMPtr<nsIHttpChannel> channel = do_QueryInterface(aSubject);
  if (NS_WARN_IF(!channel)) {
    return NS_OK;
  }

  ClearDataFromChannel(channel);
  return NS_OK;
}

void ClearSiteData::ClearDataFromChannel(nsIHttpChannel* aChannel) {
  MOZ_ASSERT(aChannel);

  nsresult rv;
  nsCOMPtr<nsIURI> uri;

  nsIScriptSecurityManager* ssm = nsContentUtils::GetSecurityManager();
  if (NS_WARN_IF(!ssm)) {
    return;
  }

  nsCOMPtr<nsIPrincipal> storagePrincipal;
  rv = ssm->GetChannelResultStoragePrincipal(aChannel,
                                             getter_AddRefs(storagePrincipal));
  if (NS_WARN_IF(NS_FAILED(rv) || !storagePrincipal)) {
    return;
  }

  nsCOMPtr<nsIPrincipal> nodePrincipal;
  nsCOMPtr<nsIPrincipal> partitionedPrincipal;
  rv = ssm->GetChannelResultPrincipals(aChannel, getter_AddRefs(nodePrincipal),
                                       getter_AddRefs(partitionedPrincipal));
  (void)nodePrincipal;
  if (NS_WARN_IF(NS_FAILED(rv) || !partitionedPrincipal)) {
    return;
  }

  bool secure = storagePrincipal->GetIsOriginPotentiallyTrustworthy();
  if (NS_WARN_IF(NS_FAILED(rv)) || !secure) {
    return;
  }

  // We want to use the final URI to check if Clear-Site-Data should be allowed
  // or not.
  rv = aChannel->GetURI(getter_AddRefs(uri));
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  uint32_t flags = ParseHeader(aChannel, uri);
  if (flags == 0) {
    // Nothing to do.
    return;
  }

  int32_t cleanFlags = 0;
  // collect flags separately for network cache cleaning due to network cache
  // forcing partitionKey to be not empty in top-level context. However other
  // storage such as cookies use empty partitionKey. Therefore, we need to pass
  // in a different principal.
  int32_t cleanNetworkFlags = 0;

  LOG(("ClearSiteData: %s, %x", uri->GetSpecOrDefault().get(), flags));

  if (StaticPrefs::privacy_clearSiteDataHeader_cache_enabled() &&
      (flags & eCache)) {
    LogOpToConsole(aChannel, uri, eCache);
    cleanNetworkFlags |= nsIClearDataService::CLEAR_ALL_CACHES;
  }

  if (StaticPrefs::dom_speculation_rules_enabled() &&
      (flags & ePrefetchCache)) {
    LogOpToConsole(aChannel, uri, ePrefetchCache);
    dom::WindowGlobalParent::ClearPrefetchCache(partitionedPrincipal);
  }

  if (flags & eCookies) {
    LogOpToConsole(aChannel, uri, eCookies);
    cleanFlags |= nsIClearDataService::CLEAR_COOKIES |
                  nsIClearDataService::CLEAR_FINGERPRINTING_PROTECTION_STATE;
  }

  if (flags & eStorage) {
    LogOpToConsole(aChannel, uri, eStorage);
    cleanFlags |= nsIClearDataService::CLEAR_DOM_STORAGES |
                  nsIClearDataService::CLEAR_FINGERPRINTING_PROTECTION_STATE;
  }

  LOG(("ClearSiteData: cleanFlags=%x, cleanNetworkFlags=%x", cleanFlags,
       cleanNetworkFlags));
  // for each `DeleteDataFromPrincipal` we need to wait for one callback.
  // cleanFlags elicits once callback.
  uint32_t numClearCalls = (cleanFlags != 0) + (cleanNetworkFlags != 0);

  if (numClearCalls > 0) {
    nsCOMPtr<nsIClearDataService> cds =
        do_GetService("@mozilla.org/clear-data-service;1");
    MOZ_ASSERT(cds);

    RefPtr<PendingCleanupHolder> holder = new PendingCleanupHolder(aChannel);
    rv = holder->Start(numClearCalls);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return;
    }

    if (cleanFlags != 0) {
      rv = cds->DeleteDataFromPrincipal(
          storagePrincipal, false /* user request */, cleanFlags, holder);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        // the channel gets resumed when the holder is no longer in scope.
        // Therefore returning without calling OnDataDeleted twice doesn't
        // stall the load indefinitly and no further cleanup from us is
        // necessary.
        return;
      }
    }

    if (cleanNetworkFlags != 0) {
      rv = cds->DeleteDataFromPrincipal(partitionedPrincipal,
                                        false /* user request */,
                                        cleanNetworkFlags, holder);
      if (NS_WARN_IF(NS_FAILED(rv))) {
        return;
      }
    }
  }
}

uint32_t ClearSiteData::ParseHeader(nsIHttpChannel* aChannel,
                                    nsIURI* aURI) const {
  MOZ_ASSERT(aChannel);

  nsAutoCString headerValue;
  nsresult rv = aChannel->GetResponseHeader("Clear-Site-Data"_ns, headerValue);
  if (NS_FAILED(rv)) {
    return 0;
  }

  uint32_t flags = 0;

  for (auto value : nsCCharSeparatedTokenizer(headerValue, ',').ToRange()) {
    // XXX This seems unnecessary, since the tokenizer already strips whitespace
    // around tokens.
    value.StripTaggedASCII(mozilla::ASCIIMask::MaskWhitespace());

    if (StaticPrefs::privacy_clearSiteDataHeader_cache_enabled()) {
      if (value.EqualsLiteral("\"cache\"")) {
        flags |= eCache | ePrefetchCache;
        continue;
      }
    }

    if (value.EqualsLiteral("\"prefetchCache\"")) {
      flags |= ePrefetchCache;
      continue;
    }

    if (value.EqualsLiteral("\"cookies\"")) {
      flags |= eCookies;
      continue;
    }

    if (value.EqualsLiteral("\"storage\"")) {
      flags |= eStorage;
      continue;
    }

    if (value.EqualsLiteral("\"*\"")) {
      flags = eCookies | eStorage | ePrefetchCache;
      if (StaticPrefs::privacy_clearSiteDataHeader_cache_enabled()) {
        flags |= eCache;
      }
      break;
    }

    LogErrorToConsole(aChannel, aURI, value);
  }

  return flags;
}

void ClearSiteData::LogOpToConsole(nsIHttpChannel* aChannel, nsIURI* aURI,
                                   Type aType) const {
  nsAutoString type;
  TypeToString(aType, type);

  nsTArray<nsString> params;
  params.AppendElement(type);

  LogToConsoleInternal(aChannel, aURI, "RunningClearSiteDataValue", params);
}

void ClearSiteData::LogErrorToConsole(nsIHttpChannel* aChannel, nsIURI* aURI,
                                      const nsACString& aUnknownType) const {
  nsTArray<nsString> params;
  params.AppendElement(NS_ConvertUTF8toUTF16(aUnknownType));

  LogToConsoleInternal(aChannel, aURI, "UnknownClearSiteDataValue", params);
}

void ClearSiteData::LogToConsoleInternal(
    nsIHttpChannel* aChannel, nsIURI* aURI, const char* aMsg,
    const nsTArray<nsString>& aParams) const {
  MOZ_ASSERT(aChannel);

  nsCOMPtr<net::HttpBaseChannel> httpChannel = do_QueryInterface(aChannel);
  if (!httpChannel) {
    return;
  }

  nsAutoCString uri;
  nsresult rv = aURI->GetSpec(uri);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return;
  }

  httpChannel->AddConsoleReport(nsIScriptError::infoFlag, "Clear-Site-Data"_ns,
                                PropertiesFile::SECURITY_PROPERTIES, uri, 0, 0,
                                nsDependentCString(aMsg), aParams);
}

void ClearSiteData::TypeToString(Type aType, nsAString& aStr) const {
  switch (aType) {
    case eCache:
      aStr.AssignLiteral("cache");
      break;

    case eCookies:
      aStr.AssignLiteral("cookies");
      break;

    case eStorage:
      aStr.AssignLiteral("storage");
      break;

    case ePrefetchCache:
      aStr.AssignLiteral("prefetchCache");
      break;

    default:
      MOZ_CRASH("Unknown type.");
  }
}

NS_INTERFACE_MAP_BEGIN(ClearSiteData)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIObserver)
  NS_INTERFACE_MAP_ENTRY(nsIObserver)
NS_INTERFACE_MAP_END

NS_IMPL_ADDREF(ClearSiteData)
NS_IMPL_RELEASE(ClearSiteData)
