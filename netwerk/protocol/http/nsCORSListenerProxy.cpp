/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "nsCORSListenerProxy.h"

#include <algorithm>

#include "HttpChannelChild.h"
#include "mozilla/Assertions.h"
#include "mozilla/BasePrincipal.h"
#include "mozilla/Components.h"
#include "mozilla/ExpandedPrincipal.h"
#include "mozilla/LinkedList.h"
#include "mozilla/LoadInfo.h"
#include "mozilla/NullPrincipal.h"
#include "mozilla/Preferences.h"
#include "mozilla/StaticPrefs_content.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/StoragePrincipalHelper.h"
#include "mozilla/dom/ReferrerInfo.h"
#include "mozilla/dom/RequestBinding.h"
#include "mozilla/dom/nsHTTPSOnlyUtils.h"
#include "mozilla/glean/NetwerkProtocolHttpMetrics.h"
#include "nsAsyncRedirectVerifyHelper.h"
#include "nsCharSeparatedTokenizer.h"
#include "nsClassHashtable.h"
#include "nsComponentManagerUtils.h"
#include "nsContentUtils.h"
#include "nsError.h"
#include "nsHashKeys.h"
#include "nsHttpChannel.h"
#include "nsIAsyncVerifyRedirectCallback.h"
#include "nsICORSPreflightCache.h"
#include "nsIChannel.h"
#include "nsIChannelEventSink.h"
#include "nsIConsoleService.h"
#include "nsICorsPreflightCallback.h"
#include "nsIDNSRecord.h"
#include "nsIDNSService.h"
#include "nsIHttpChannel.h"
#include "nsIHttpChannelInternal.h"
#include "nsIHttpHeaderVisitor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsILoadContext.h"
#include "nsILoadGroup.h"
#include "nsINetworkInterceptController.h"
#include "nsIScriptError.h"
#include "nsISupportsImpl.h"
#include "nsIThreadRetargetableStreamListener.h"
#include "nsMimeTypes.h"
#include "nsNetUtil.h"
#include "nsQueryObject.h"
#include "nsServiceManagerUtils.h"
#include "nsStreamUtils.h"
#include "nsString.h"
#include "nsStringStream.h"
#include "nsWhitespaceTokenizer.h"

using namespace mozilla;
using namespace mozilla::net;

struct CORSCacheEntry;

#define PREFLIGHT_CACHE_SIZE 100
// 5 seconds is chosen to be compatible with Chromium.
#define PREFLIGHT_DEFAULT_EXPIRY_SECONDS 5

static inline nsAutoString GetStatusCodeAsString(nsIHttpChannel* aHttp) {
  nsAutoString result;
  uint32_t code;
  if (NS_SUCCEEDED(aHttp->GetResponseStatus(&code))) {
    result.AppendInt(code);
  }
  return result;
}

static void LogBlockedRequest(nsIRequest* aRequest, const char* aProperty,
                              const char16_t* aParam, uint32_t aBlockingReason,
                              nsIHttpChannel* aCreatingChannel,
                              bool aIsWarning = false) {
  nsresult rv = NS_OK;

  nsCOMPtr<nsIChannel> channel = do_QueryInterface(aRequest);

  if (!aIsWarning) {
    NS_SetRequestBlockingReason(channel, aBlockingReason);
  }

  nsCOMPtr<nsIURI> aUri;
  channel->GetURI(getter_AddRefs(aUri));
  nsAutoCString spec;
  if (aUri) {
    spec = aUri->GetSpecOrDefault();
  }

  // Generate the error message
  nsAutoString blockedMessage;
  AutoTArray<nsString, 2> params;
  CopyUTF8toUTF16(spec, *params.AppendElement());
  if (aParam) {
    params.AppendElement(aParam);
  }
  NS_ConvertUTF8toUTF16 specUTF16(spec);
  rv = nsContentUtils::FormatLocalizedString(
      PropertiesFile::SECURITY_PROPERTIES, aProperty, params, blockedMessage);

  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to log blocked cross-site request (no formalizedStr");
    return;
  }

  nsAutoString msg(blockedMessage.get());
  nsDependentCString category(aProperty);

  if (XRE_IsParentProcess()) {
    if (aCreatingChannel) {
      rv = aCreatingChannel->LogBlockedCORSRequest(msg, category, aIsWarning);
      if (NS_SUCCEEDED(rv)) {
        return;
      }
    }
    NS_WARNING(
        "Failed to log blocked cross-site request to web console from "
        "parent->child, falling back to browser console");
  }

  bool privateBrowsing = false;
  if (aRequest) {
    nsCOMPtr<nsILoadGroup> loadGroup;
    rv = aRequest->GetLoadGroup(getter_AddRefs(loadGroup));
    NS_ENSURE_SUCCESS_VOID(rv);
    privateBrowsing = nsContentUtils::IsInPrivateBrowsing(loadGroup);
  }

  bool fromChromeContext = false;
  if (channel) {
    nsCOMPtr<nsILoadInfo> loadInfo = channel->LoadInfo();
    fromChromeContext = loadInfo->TriggeringPrincipal()->IsSystemPrincipal();
  }

  // we are passing aProperty as the category so we can link to the
  // appropriate MDN docs depending on the specific error.
  uint64_t innerWindowID = nsContentUtils::GetInnerWindowID(aRequest);
  // The |innerWindowID| could be 0 if this request is created from script.
  // We can always try top level content window id in this case,
  // since the window id can lead to current top level window's web console.
  if (!innerWindowID) {
    if (nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aRequest)) {
      (void)httpChannel->GetTopLevelContentWindowId(&innerWindowID);
    }
  }
  nsCORSListenerProxy::LogBlockedCORSRequest(innerWindowID, privateBrowsing,
                                             fromChromeContext, msg, category,
                                             aIsWarning);
}

//////////////////////////////////////////////////////////////////////////
// Preflight cache

class nsPreflightCache : public nsICORSPreflightCache {
 public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSICORSPREFLIGHTCACHE

  struct TokenTime {
    nsCString token;
    TimeStamp expirationTime;
  };

  already_AddRefed<CORSCacheEntry> GetEntry(
      nsIURI* aURI, nsIPrincipal* aPrincipal, bool aWithCredentials,
      const OriginAttributes& aOriginAttributes, bool aCreate);
  void RemoveEntries(nsIURI* aURI, nsIPrincipal* aPrincipal,
                     const OriginAttributes& aOriginAttributes);
  void PurgePrivateBrowsingEntries();

  void Clear();

 protected:
  virtual ~nsPreflightCache() { Clear(); }

 private:
  nsRefPtrHashtable<nsCStringHashKey, CORSCacheEntry> mTable;
  LinkedList<CORSCacheEntry> mList;
};

struct CORSCacheEntry : public LinkedListElement<CORSCacheEntry>,
                        public nsICORSPreflightCacheEntry {
  NS_DECL_ISUPPORTS
  NS_DECL_NSICORSPREFLIGHTCACHEENTRY
  explicit CORSCacheEntry(nsIURI* aUri,
                          const OriginAttributes& aOriginAttributes,
                          nsIPrincipal* aPrincipal, bool withCredentials,
                          nsCString& aKey)
      : mURI(aUri),
        mOA(aOriginAttributes),
        mPrincipal(aPrincipal),
        mWithCredentials(withCredentials),
        mKey(aKey) {}

  void PurgeExpired(TimeStamp now);
  bool CheckRequest(const nsCString& aMethod,
                    const nsTArray<nsCString>& aHeaders);

  nsCOMPtr<nsIURI> mURI;
  const OriginAttributes mOA;
  nsCOMPtr<nsIPrincipal> mPrincipal;
  bool mWithCredentials;
  nsCString mKey;  // serialized key
  const TimeStamp mCreationTime{TimeStamp::NowLoRes()};
  bool mDoomed{false};
  bool mIsProxyUsed{false};

  nsTArray<nsPreflightCache::TokenTime> mMethods;
  nsTArray<nsPreflightCache::TokenTime> mHeaders;

 private:
  virtual ~CORSCacheEntry() = default;

  bool CheckDNSCache();
};

NS_IMPL_ISUPPORTS(nsPreflightCache, nsICORSPreflightCache)

NS_IMETHODIMP
nsPreflightCache::GetEntries(
    nsIPrincipal* aPrincipal,
    nsTArray<RefPtr<nsICORSPreflightCacheEntry>>& aEntries) {
  for (auto iter = mTable.Iter(); !iter.Done(); iter.Next()) {
    RefPtr<nsIPrincipal> iterPrincipal;
    iter.Data()->GetPrincipal(getter_AddRefs(iterPrincipal));
    if (iterPrincipal->Equals(aPrincipal)) {
      auto* entry = iter.UserData();
      aEntries.AppendElement(entry);
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
nsPreflightCache::ClearEntry(nsICORSPreflightCacheEntry* entry) {
  nsCOMPtr<nsIURI> uri;
  nsresult rv = entry->GetURI(getter_AddRefs(uri));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIPrincipal> principal;
  rv = entry->GetPrincipal(getter_AddRefs(principal));
  NS_ENSURE_SUCCESS(rv, rv);

  const OriginAttributes oa = entry->OriginAttributesRef();

  RemoveEntries(uri, principal, oa);
  return NS_OK;
}

// Will be initialized in EnsurePreflightCache.
static StaticRefPtr<nsPreflightCache> sPreflightCache;

static bool EnsurePreflightCache() {
  if (sPreflightCache) return true;

  RefPtr<nsPreflightCache> newCache(new nsPreflightCache());
  sPreflightCache = newCache;
  return true;
}

void nsPreflightCache::PurgePrivateBrowsingEntries() {
  for (auto iter = mTable.Iter(); !iter.Done(); iter.Next()) {
    auto* entry = iter.UserData();
    if (entry->mOA.IsPrivateBrowsing()) {
      // last private browsing window closed, remove preflight cache entries
      entry->removeFrom(sPreflightCache->mList);
      iter.Remove();
    }
  }
}

NS_IMPL_ISUPPORTS(CORSCacheEntry, nsICORSPreflightCacheEntry)

NS_IMETHODIMP
CORSCacheEntry::GetKey(nsACString& aKey) {
  aKey = mKey;
  return NS_OK;
}

NS_IMETHODIMP
CORSCacheEntry::GetURI(nsIURI** aURI) {
  *aURI = do_AddRef(mURI).take();
  return NS_OK;
}

NS_IMETHODIMP
CORSCacheEntry::GetOriginAttributes(JSContext* aCx,
                                    JS::MutableHandle<JS::Value> aVal) {
  if (NS_WARN_IF(!ToJSValue(aCx, mOA, aVal))) {
    return NS_ERROR_FAILURE;
  }
  return NS_OK;
}

const OriginAttributes& CORSCacheEntry::OriginAttributesRef() { return mOA; }

NS_IMETHODIMP
CORSCacheEntry::GetPrincipal(nsIPrincipal** aPrincipal) {
  *aPrincipal = do_AddRef(mPrincipal).take();
  return NS_OK;
}

NS_IMETHODIMP
CORSCacheEntry::GetPrivateBrowsing(bool* aPrivateBrowsing) {
  *aPrivateBrowsing = mOA.IsPrivateBrowsing();
  return NS_OK;
}

NS_IMETHODIMP
CORSCacheEntry::GetWithCredentials(bool* aWithCredentials) {
  *aWithCredentials = mWithCredentials;
  return NS_OK;
}

void CORSCacheEntry::PurgeExpired(TimeStamp now) {
  auto isExpired = [now](const nsPreflightCache::TokenTime& aEntry) {
    return now >= aEntry.expirationTime;
  };
  mMethods.RemoveElementsBy(isExpired);
  mHeaders.RemoveElementsBy(isExpired);
}

bool CORSCacheEntry::CheckDNSCache() {
  // When proxy is used, the DNS lookup is done by proxy, so we skip this check.
  if (mIsProxyUsed) {
    return true;
  }

  nsCOMPtr<nsIDNSService> dns;
  dns = mozilla::components::DNS::Service();
  if (!dns) {
    return false;
  }

  nsAutoCString host;
  if (NS_FAILED(mURI->GetAsciiHost(host))) {
    return false;
  }

  // Happy Eyeballs resolves each address family separately, so the host may be
  // cached only as per-family (AF_INET/AF_INET6) records with no AF_UNSPEC
  // entry. Check each family: the cached preflight is still valid as long as a
  // matching DNS entry exists and none was updated after the preflight was
  // created.
  const nsIDNSService::DNSFlags familyFlags[] = {
      nsIDNSService::RESOLVE_DEFAULT_FLAGS,  // AF_UNSPEC
      nsIDNSService::RESOLVE_DISABLE_IPV6,   // AF_INET
      nsIDNSService::RESOLVE_DISABLE_IPV4,   // AF_INET6
  };

  bool foundRecord = false;
  for (const auto& flags : familyFlags) {
    nsCOMPtr<nsIDNSRecord> record;
    nsresult rv =
        dns->ResolveNative(host, nsIDNSService::RESOLVE_OFFLINE | flags, mOA,
                           getter_AddRefs(record));
    nsCOMPtr<nsIDNSAddrRecord> addrRec = do_QueryInterface(record);
    if (NS_FAILED(rv) || !addrRec) {
      continue;
    }

    foundRecord = true;
    TimeStamp lastUpdate;
    (void)addrRec->GetLastUpdate(&lastUpdate);
    if (lastUpdate > mCreationTime) {
      // The DNS result was re-resolved after the preflight was cached.
      return false;
    }
  }

  return foundRecord;
}

bool CORSCacheEntry::CheckRequest(const nsCString& aMethod,
                                  const nsTArray<nsCString>& aHeaders) {
  PurgeExpired(TimeStamp::NowLoRes());

  if (!CheckDNSCache()) {
    mMethods.Clear();
    mHeaders.Clear();
    mDoomed = true;
    return false;
  }

  if (!aMethod.EqualsLiteral("GET") && !aMethod.EqualsLiteral("POST")) {
    struct CheckToken {
      bool Equals(const nsPreflightCache::TokenTime& e,
                  const nsCString& method) const {
        return e.token.Equals(method);
      }
    };

    if (!mMethods.Contains(aMethod, CheckToken())) {
      return false;
    }
  }

  struct CheckHeaderToken {
    bool Equals(const nsPreflightCache::TokenTime& e,
                const nsCString& header) const {
      return e.token.Equals(header, nsCaseInsensitiveCStringComparator);
    }
  } checker;
  for (uint32_t i = 0; i < aHeaders.Length(); ++i) {
    if (!mHeaders.Contains(aHeaders[i], checker)) {
      return false;
    }
  }

  return true;
}

already_AddRefed<CORSCacheEntry> nsPreflightCache::GetEntry(
    nsIURI* aURI, nsIPrincipal* aPrincipal, bool aWithCredentials,
    const OriginAttributes& aOriginAttributes, bool aCreate) {
  nsAutoCString key;
  if (NS_FAILED(aPrincipal->GetPrefLightCacheKey(aURI, aWithCredentials,
                                                 aOriginAttributes, key))) {
    NS_WARNING("Invalid cache key!");
    return nullptr;
  }

  RefPtr<CORSCacheEntry> existingEntry = nullptr;
  if ((existingEntry = mTable.Get(key))) {
    if (existingEntry->mDoomed) {
      existingEntry->removeFrom(mList);
      mTable.Remove(key);
    } else {
      // Entry already existed so just return it. Also update the LRU list.
      // Move to the head of the list.
      existingEntry->removeFrom(mList);
      mList.insertFront(existingEntry);
      return existingEntry.forget();
    }
  }

  if (!aCreate) {
    return nullptr;
  }

  // This is a new entry, allocate and insert into the table now so that any
  // failures don't cause items to be removed from a full cache.
  RefPtr<CORSCacheEntry> newEntry = new CORSCacheEntry(
      aURI, aOriginAttributes, aPrincipal, aWithCredentials, key);

  NS_ASSERTION(mTable.Count() <= PREFLIGHT_CACHE_SIZE,
               "Something is borked, too many entries in the cache!");

  // Now enforce the max count.
  if (mTable.Count() == PREFLIGHT_CACHE_SIZE) {
    // Try to kick out all the expired entries.
    TimeStamp now = TimeStamp::NowLoRes();
    for (auto iter = mTable.Iter(); !iter.Done(); iter.Next()) {
      auto* entry = iter.UserData();
      entry->PurgeExpired(now);

      if (entry->mHeaders.IsEmpty() && entry->mMethods.IsEmpty()) {
        // Expired, remove from the list as well as the hash table.
        entry->removeFrom(sPreflightCache->mList);
        iter.Remove();
      }
    }

    // If that didn't remove anything then kick out the least recently used
    // entry.
    if (mTable.Count() == PREFLIGHT_CACHE_SIZE) {
      CORSCacheEntry* lruEntry = static_cast<CORSCacheEntry*>(mList.popLast());
      MOZ_ASSERT(lruEntry);

      // This will delete 'lruEntry'.
      nsAutoCString lruKey;
      lruEntry->GetKey(lruKey);
      mTable.Remove(lruKey);

      NS_ASSERTION(mTable.Count() == PREFLIGHT_CACHE_SIZE - 1,
                   "Somehow tried to remove an entry that was never added!");
    }
  }
  CORSCacheEntry* newEntryWeak = newEntry.get();
  mTable.InsertOrUpdate(key, newEntry);
  mList.insertFront(newEntryWeak);
  return newEntry.forget();
}

void nsPreflightCache::RemoveEntries(
    nsIURI* aURI, nsIPrincipal* aPrincipal,
    const OriginAttributes& aOriginAttributes) {
  RefPtr<CORSCacheEntry> entry;
  nsCString key;
  if (NS_SUCCEEDED(aPrincipal->GetPrefLightCacheKey(aURI, true,
                                                    aOriginAttributes, key))) {
    if ((entry = mTable.Get(key))) {
      entry->removeFrom(mList);
      mTable.Remove(key);
    }
  }

  if (NS_SUCCEEDED(aPrincipal->GetPrefLightCacheKey(aURI, false,
                                                    aOriginAttributes, key))) {
    if ((entry = mTable.Get(key))) {
      entry->removeFrom(mList);
      mTable.Remove(key);
    }
  }
}

void nsPreflightCache::Clear() {
  mList.clear();
  mTable.Clear();
}

//////////////////////////////////////////////////////////////////////////
// nsCORSListenerProxy

NS_IMPL_ISUPPORTS(nsCORSListenerProxy, nsIStreamListener, nsIRequestObserver,
                  nsIChannelEventSink, nsIInterfaceRequestor,
                  nsIThreadRetargetableStreamListener)

/* static */
already_AddRefed<nsICORSPreflightCache>
nsCORSListenerProxy::GetCORSPreflightSingleton() {
  NS_ASSERTION(!IsNeckoChild(), "not a parent process");

  if (!EnsurePreflightCache()) {
    NS_ASSERTION(false, "Failed to get the preflightCache");
  }
  return do_AddRef(sPreflightCache);
}

/* static */
void nsCORSListenerProxy::Shutdown() { sPreflightCache = nullptr; }

/* static */
void nsCORSListenerProxy::ClearCache() {
  if (!sPreflightCache) {
    return;
  }
  sPreflightCache->Clear();
}

// static
void nsCORSListenerProxy::ClearPrivateBrowsingCache() {
  if (!sPreflightCache) {
    return;
  }
  sPreflightCache->PurgePrivateBrowsingEntries();
}

// Usually, when using an expanded principal, there's no particularly good
// origin to do the request with. However if the expanded principal only wraps
// one principal, we can use that one instead.
//
// This is needed so that DevTools can still do CORS-enabled requests (since
// DevTools uses a triggering principal expanding the node principal to bypass
// CSP checks, see Element::CreateDevToolsPrincipal(), bug 1604562, and bug
// 1391994).
static nsIPrincipal* GetOriginHeaderPrincipal(nsIPrincipal* aPrincipal) {
  while (aPrincipal && aPrincipal->GetIsExpandedPrincipal()) {
    auto* ep = BasePrincipal::Cast(aPrincipal)->As<ExpandedPrincipal>();
    if (ep->AllowList().Length() != 1) {
      break;
    }
    aPrincipal = ep->AllowList()[0];
  }
  return aPrincipal;
}

nsCORSListenerProxy::nsCORSListenerProxy(nsIStreamListener* aOuter,
                                         nsIPrincipal* aRequestingPrincipal,
                                         bool aWithCredentials)
    : mOuterListener(aOuter),
      mRequestingPrincipal(aRequestingPrincipal),
      mOriginHeaderPrincipal(GetOriginHeaderPrincipal(aRequestingPrincipal)),
      mWithCredentials(aWithCredentials),
      mRequestApproved(false),
      mHasBeenCrossSite(false),
#ifdef DEBUG
      mInited(false),
#endif
      mMutex("nsCORSListenerProxy") {
}

nsresult nsCORSListenerProxy::Init(nsIChannel* aChannel,
                                   DataURIHandling aAllowDataURI) {
  aChannel->GetNotificationCallbacks(
      getter_AddRefs(mOuterNotificationCallbacks));
  aChannel->SetNotificationCallbacks(this);

  nsresult rv =
      UpdateChannel(aChannel, aAllowDataURI, UpdateType::Default, false);
  if (NS_FAILED(rv)) {
    {
      MutexAutoLock lock(mMutex);
      mOuterListener = nullptr;
    }
    mRequestingPrincipal = nullptr;
    mOriginHeaderPrincipal = nullptr;
    mOuterNotificationCallbacks = nullptr;
    mHttpChannel = nullptr;
  }
#ifdef DEBUG
  mInited = true;
#endif
  return rv;
}

NS_IMETHODIMP
nsCORSListenerProxy::OnStartRequest(nsIRequest* aRequest) {
  MOZ_ASSERT(mInited, "nsCORSListenerProxy has not been initialized properly");
  nsresult rv = CheckRequestApproved(aRequest);
  mRequestApproved = NS_SUCCEEDED(rv);
  if (!mRequestApproved) {
    nsCOMPtr<nsIChannel> channel = do_QueryInterface(aRequest);
    if (channel) {
      nsCOMPtr<nsIURI> uri;
      NS_GetFinalChannelURI(channel, getter_AddRefs(uri));
      if (uri) {
        OriginAttributes attrs;
        StoragePrincipalHelper::GetOriginAttributesForNetworkState(channel,
                                                                   attrs);

        if (sPreflightCache) {
          // OK to use mRequestingPrincipal since preflights never get
          // redirected.
          sPreflightCache->RemoveEntries(uri, mRequestingPrincipal, attrs);
        } else {
          nsCOMPtr<nsIHttpChannelChild> httpChannelChild =
              do_QueryInterface(channel);
          if (httpChannelChild) {
            rv = httpChannelChild->RemoveCorsPreflightCacheEntry(
                uri, mRequestingPrincipal, attrs);
            if (NS_FAILED(rv)) {
              // Only warn here to ensure we fall through the request Cancel()
              // and outer listener OnStartRequest() calls.
              NS_WARNING("Failed to remove CORS preflight cache entry!");
            }
          }
        }
      }
    }

    aRequest->Cancel(NS_ERROR_DOM_BAD_URI);
    nsCOMPtr<nsIStreamListener> listener;
    {
      MutexAutoLock lock(mMutex);
      listener = mOuterListener;
    }
    listener->OnStartRequest(aRequest);

    // Reason for NS_ERROR_DOM_BAD_URI already logged in CheckRequestApproved()
    return NS_ERROR_DOM_BAD_URI;
  }

  nsCOMPtr<nsIStreamListener> listener;
  {
    MutexAutoLock lock(mMutex);
    listener = mOuterListener;
  }
  return listener->OnStartRequest(aRequest);
}

namespace {
class CheckOriginHeader final : public nsIHttpHeaderVisitor {
 public:
  NS_DECL_ISUPPORTS

  CheckOriginHeader() = default;

  NS_IMETHOD
  VisitHeader(const nsACString& aHeader, const nsACString& aValue) override {
    if (aHeader.EqualsLiteral("Access-Control-Allow-Origin")) {
      mHeaderCount++;
    }

    if (mHeaderCount > 1) {
      return NS_ERROR_DOM_BAD_URI;
    }
    return NS_OK;
  }

 private:
  uint32_t mHeaderCount{0};

  ~CheckOriginHeader() = default;
};

NS_IMPL_ISUPPORTS(CheckOriginHeader, nsIHttpHeaderVisitor)
}  // namespace

nsresult nsCORSListenerProxy::CheckRequestApproved(nsIRequest* aRequest) {
  // Check if this was actually a cross domain request
  if (!mHasBeenCrossSite) {
    return NS_OK;
  }
  nsCOMPtr<nsIHttpChannel> topChannel;
  topChannel.swap(mHttpChannel);

  if (StaticPrefs::content_cors_disable()) {
    LogBlockedRequest(aRequest, "CORSDisabled", nullptr,
                      nsILoadInfo::BLOCKING_REASON_CORSDISABLED, topChannel);
    return NS_ERROR_DOM_BAD_URI;
  }

  // Check if the request failed
  nsresult status;
  nsresult rv = aRequest->GetStatus(&status);
  if (NS_FAILED(rv)) {
    LogBlockedRequest(aRequest, "CORSDidNotSucceed2", nullptr,
                      nsILoadInfo::BLOCKING_REASON_CORSDIDNOTSUCCEED,
                      topChannel);
    return rv;
  }

  if (NS_FAILED(status)) {
    if (NS_BINDING_ABORTED != status) {
      // Don't want to log mere cancellation as an error.
      LogBlockedRequest(aRequest, "CORSDidNotSucceed2", nullptr,
                        nsILoadInfo::BLOCKING_REASON_CORSDIDNOTSUCCEED,
                        topChannel);
    }
    return status;
  }

  // Test that things worked on a HTTP level
  nsCOMPtr<nsIHttpChannel> http = do_QueryInterface(aRequest);
  if (!http) {
    nsCOMPtr<nsIChannel> channel = do_QueryInterface(aRequest);
    nsCOMPtr<nsIURI> uri;
    NS_GetFinalChannelURI(channel, getter_AddRefs(uri));
    if (uri && uri->SchemeIs("moz-extension")) {
      // moz-extension:-URLs do not support CORS, but can universally be read
      // if an extension lists the resource in web_accessible_resources.
      // Access will be checked in UpdateChannel.
      return NS_OK;
    }
    LogBlockedRequest(aRequest, "CORSRequestNotHttp", nullptr,
                      nsILoadInfo::BLOCKING_REASON_CORSREQUESTNOTHTTP,
                      topChannel);
    return NS_ERROR_DOM_BAD_URI;
  }

  nsCOMPtr<nsILoadInfo> loadInfo = http->LoadInfo();
  if (loadInfo->GetServiceWorkerTaintingSynthesized()) {
    // For synthesized responses, we don't need to perform any checks.
    // Note: This would be unsafe if we ever changed our behavior to allow
    // service workers to intercept CORS preflights.
    return NS_OK;
  }

  // Check the Access-Control-Allow-Origin header
  RefPtr<CheckOriginHeader> visitor = new CheckOriginHeader();
  nsAutoCString allowedOriginHeader;

  // check for duplicate headers
  rv = http->VisitOriginalResponseHeaders(visitor);
  if (NS_FAILED(rv)) {
    LogBlockedRequest(
        aRequest, "CORSMultipleAllowOriginNotAllowed", nullptr,
        nsILoadInfo::BLOCKING_REASON_CORSMULTIPLEALLOWORIGINNOTALLOWED,
        topChannel);
    return rv;
  }

  rv = http->GetResponseHeader("Access-Control-Allow-Origin"_ns,
                               allowedOriginHeader);
  if (NS_FAILED(rv)) {
    auto statusCode = GetStatusCodeAsString(http);
    LogBlockedRequest(aRequest, "CORSMissingAllowOrigin2", statusCode.get(),
                      nsILoadInfo::BLOCKING_REASON_CORSMISSINGALLOWORIGIN,
                      topChannel);
    return rv;
  }

  // Bug 1210985 - Explicitly point out the error that the credential is
  // not supported if the allowing origin is '*'. Note that this check
  // has to be done before the condition
  //
  // >> if (mWithCredentials || !allowedOriginHeader.EqualsLiteral("*"))
  //
  // below since "if (A && B)" is included in "if (A || !B)".
  //
  if (mWithCredentials && allowedOriginHeader.EqualsLiteral("*")) {
    LogBlockedRequest(aRequest, "CORSNotSupportingCredentials", nullptr,
                      nsILoadInfo::BLOCKING_REASON_CORSNOTSUPPORTINGCREDENTIALS,
                      topChannel);
    return NS_ERROR_DOM_BAD_URI;
  }

  if (mWithCredentials || !allowedOriginHeader.EqualsLiteral("*")) {
    MOZ_ASSERT(!mOriginHeaderPrincipal->GetIsExpandedPrincipal());
    nsAutoCString origin;
    mOriginHeaderPrincipal->GetWebExposedOriginSerialization(origin);

    if (!allowedOriginHeader.Equals(origin)) {
      LogBlockedRequest(
          aRequest, "CORSAllowOriginNotMatchingOrigin",
          NS_ConvertUTF8toUTF16(allowedOriginHeader).get(),
          nsILoadInfo::BLOCKING_REASON_CORSALLOWORIGINNOTMATCHINGORIGIN,
          topChannel);
      return NS_ERROR_DOM_BAD_URI;
    }
  }

  // Check Access-Control-Allow-Credentials header
  if (mWithCredentials) {
    nsAutoCString allowCredentialsHeader;
    rv = http->GetResponseHeader("Access-Control-Allow-Credentials"_ns,
                                 allowCredentialsHeader);

    if (!allowCredentialsHeader.EqualsLiteral("true")) {
      LogBlockedRequest(
          aRequest, "CORSMissingAllowCredentials", nullptr,
          nsILoadInfo::BLOCKING_REASON_CORSMISSINGALLOWCREDENTIALS, topChannel);
      return NS_ERROR_DOM_BAD_URI;
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
nsCORSListenerProxy::OnStopRequest(nsIRequest* aRequest, nsresult aStatusCode) {
  MOZ_ASSERT(mInited, "nsCORSListenerProxy has not been initialized properly");
  nsCOMPtr<nsIStreamListener> listener;
  {
    MutexAutoLock lock(mMutex);
    listener = std::move(mOuterListener);
  }
  nsresult rv = listener->OnStopRequest(aRequest, aStatusCode);
  mOuterNotificationCallbacks = nullptr;
  mHttpChannel = nullptr;
  return rv;
}

NS_IMETHODIMP
nsCORSListenerProxy::OnDataAvailable(nsIRequest* aRequest,
                                     nsIInputStream* aInputStream,
                                     uint64_t aOffset, uint32_t aCount) {
  // NB: This can be called on any thread!  But we're guaranteed that it is
  // called between OnStartRequest and OnStopRequest, so we don't need to worry
  // about races.

  MOZ_ASSERT(mInited, "nsCORSListenerProxy has not been initialized properly");
  if (!mRequestApproved) {
    // Reason for NS_ERROR_DOM_BAD_URI already logged in CheckRequestApproved()
    return NS_ERROR_DOM_BAD_URI;
  }
  nsCOMPtr<nsIStreamListener> listener;
  {
    MutexAutoLock lock(mMutex);
    listener = mOuterListener;
  }
  return listener->OnDataAvailable(aRequest, aInputStream, aOffset, aCount);
}

NS_IMETHODIMP
nsCORSListenerProxy::OnDataFinished(nsresult aStatus) {
  nsCOMPtr<nsIStreamListener> listener;
  {
    MutexAutoLock lock(mMutex);
    listener = mOuterListener;
  }
  if (!listener) {
    return NS_ERROR_FAILURE;
  }
  nsCOMPtr<nsIThreadRetargetableStreamListener> retargetableListener =
      do_QueryInterface(listener);
  if (retargetableListener) {
    return retargetableListener->OnDataFinished(aStatus);
  }

  return NS_OK;
}

void nsCORSListenerProxy::SetInterceptController(
    nsINetworkInterceptController* aInterceptController) {
  mInterceptController = aInterceptController;
}

NS_IMETHODIMP
nsCORSListenerProxy::GetInterface(const nsIID& aIID, void** aResult) {
  if (aIID.Equals(NS_GET_IID(nsIChannelEventSink))) {
    *aResult = static_cast<nsIChannelEventSink*>(this);
    NS_ADDREF_THIS();

    return NS_OK;
  }

  if (aIID.Equals(NS_GET_IID(nsINetworkInterceptController)) &&
      mInterceptController) {
    nsCOMPtr<nsINetworkInterceptController> copy(mInterceptController);
    *aResult = copy.forget().take();

    return NS_OK;
  }

  return mOuterNotificationCallbacks
             ? mOuterNotificationCallbacks->GetInterface(aIID, aResult)
             : NS_ERROR_NO_INTERFACE;
}

NS_IMETHODIMP
nsCORSListenerProxy::AsyncOnChannelRedirect(
    nsIChannel* aOldChannel, nsIChannel* aNewChannel, uint32_t aFlags,
    nsIAsyncVerifyRedirectCallback* aCb) {
  nsresult rv;
  if (NS_IsInternalSameURIRedirect(aOldChannel, aNewChannel, aFlags) ||
      NS_IsHSTSUpgradeRedirect(aOldChannel, aNewChannel, aFlags)) {
    // Internal redirects still need to be updated in order to maintain
    // the correct headers.  We use DataURIHandling::Allow, since unallowed
    // data URIs should have been blocked before we got to the internal
    // redirect.
    rv = UpdateChannel(aNewChannel, DataURIHandling::Allow,
                       UpdateType::InternalOrHSTSRedirect, false);
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "nsCORSListenerProxy::AsyncOnChannelRedirect: "
          "internal redirect UpdateChannel() returned failure");
      aOldChannel->Cancel(rv);
      return rv;
    }
  } else {
    mIsRedirect = true;
    // A real, external redirect.  Perform CORS checking on new URL.
    rv = CheckRequestApproved(aOldChannel);
    if (NS_FAILED(rv)) {
      nsCOMPtr<nsIURI> oldURI;
      NS_GetFinalChannelURI(aOldChannel, getter_AddRefs(oldURI));
      if (oldURI) {
        OriginAttributes attrs;
        StoragePrincipalHelper::GetOriginAttributesForNetworkState(aOldChannel,
                                                                   attrs);
        if (sPreflightCache) {
          // OK to use mRequestingPrincipal since preflights never get
          // redirected.
          sPreflightCache->RemoveEntries(oldURI, mRequestingPrincipal, attrs);
        } else {
          nsCOMPtr<nsIHttpChannelChild> httpChannelChild =
              do_QueryInterface(aOldChannel);
          if (httpChannelChild) {
            rv = httpChannelChild->RemoveCorsPreflightCacheEntry(
                oldURI, mRequestingPrincipal, attrs);
            if (NS_FAILED(rv)) {
              // Only warn here to ensure we call the channel Cancel() below
              NS_WARNING("Failed to remove CORS preflight cache entry!");
            }
          }
        }
      }
      aOldChannel->Cancel(NS_ERROR_DOM_BAD_URI);
      // Reason for NS_ERROR_DOM_BAD_URI already logged in
      // CheckRequestApproved()
      return NS_ERROR_DOM_BAD_URI;
    }

    if (mHasBeenCrossSite) {
      // Once we've been cross-site, cross-origin redirects reset our source
      // origin. Note that we need to call GetChannelURIPrincipal() because
      // we are looking for the principal that is actually being loaded and not
      // the principal that initiated the load.
      nsCOMPtr<nsIPrincipal> oldChannelPrincipal;
      nsContentUtils::GetSecurityManager()->GetChannelURIPrincipal(
          aOldChannel, getter_AddRefs(oldChannelPrincipal));
      nsCOMPtr<nsIPrincipal> newChannelPrincipal;
      nsContentUtils::GetSecurityManager()->GetChannelURIPrincipal(
          aNewChannel, getter_AddRefs(newChannelPrincipal));
      if (!oldChannelPrincipal || !newChannelPrincipal) {
        rv = NS_ERROR_OUT_OF_MEMORY;
      }

      if (NS_FAILED(rv)) {
        aOldChannel->Cancel(rv);
        return rv;
      }

      if (!oldChannelPrincipal->Equals(newChannelPrincipal)) {
        // Spec says to set our source origin to a unique origin.
        mOriginHeaderPrincipal =
            NullPrincipal::CreateWithInheritedAttributes(oldChannelPrincipal);
      }
    }

    bool rewriteToGET = false;
    // We need to strip auth header from preflight request for
    // cross-origin redirects.
    // See Bug 1874132
    bool stripAuthHeader =
        NS_ShouldRemoveAuthHeaderOnRedirect(aOldChannel, aNewChannel, aFlags);

    nsCOMPtr<nsIHttpChannel> oldHttpChannel = do_QueryInterface(aOldChannel);
    if (oldHttpChannel) {
      nsAutoCString method;
      (void)oldHttpChannel->GetRequestMethod(method);
      (void)oldHttpChannel->ShouldStripRequestBodyHeader(method, &rewriteToGET);
    }

    rv = UpdateChannel(
        aNewChannel, DataURIHandling::Disallow,
        rewriteToGET ? UpdateType::StripRequestBodyHeader : UpdateType::Default,
        stripAuthHeader);
    if (NS_FAILED(rv)) {
      NS_WARNING(
          "nsCORSListenerProxy::AsyncOnChannelRedirect: "
          "UpdateChannel() returned failure");
      aOldChannel->Cancel(rv);
      return rv;
    }
  }

  nsCOMPtr<nsIChannelEventSink> outer =
      do_GetInterface(mOuterNotificationCallbacks);
  if (outer) {
    return outer->AsyncOnChannelRedirect(aOldChannel, aNewChannel, aFlags, aCb);
  }

  aCb->OnRedirectVerifyCallback(NS_OK);

  return NS_OK;
}

NS_IMETHODIMP
nsCORSListenerProxy::CheckListenerChain() {
  MOZ_ASSERT(NS_IsMainThread());

  nsCOMPtr<nsIThreadRetargetableStreamListener> retargetableListener;
  {
    MutexAutoLock lock(mMutex);
    retargetableListener = do_QueryInterface(mOuterListener);
  }
  if (!retargetableListener) {
    return NS_ERROR_NO_INTERFACE;
  }

  return retargetableListener->CheckListenerChain();
}

// Please note that the CSP directive 'upgrade-insecure-requests' and the
// HTTPS-Only Mode are relying on the promise that channels get updated from
// http: to https: before the channel fetches any data from the netwerk. Such
// channels should not be blocked by CORS and marked as cross origin requests.
// E.g.: toplevel page: https://www.example.com loads
//                 xhr: http://www.example.com/foo which gets updated to
//                      https://www.example.com/foo
// In such a case we should bail out of CORS and rely on the promise that
// nsHttpChannel::Connect() upgrades the request from http to https.
bool CheckInsecureUpgradePreventsCORS(nsIPrincipal* aRequestingPrincipal,
                                      nsIChannel* aChannel) {
  nsCOMPtr<nsIURI> channelURI;
  nsresult rv = NS_GetFinalChannelURI(aChannel, getter_AddRefs(channelURI));
  NS_ENSURE_SUCCESS(rv, false);

  // upgrade insecure requests is only applicable to http requests
  if (!channelURI->SchemeIs("http")) {
    return false;
  }

  nsCOMPtr<nsIURI> originalURI;
  rv = aChannel->GetOriginalURI(getter_AddRefs(originalURI));
  NS_ENSURE_SUCCESS(rv, false);

  nsAutoCString principalHost, channelHost, origChannelHost;

  // if we can not query a host from the uri, there is nothing to do
  if (NS_FAILED(aRequestingPrincipal->GetAsciiHost(principalHost)) ||
      NS_FAILED(channelURI->GetAsciiHost(channelHost)) ||
      NS_FAILED(originalURI->GetAsciiHost(origChannelHost))) {
    return false;
  }

  // if the hosts do not match, there is nothing to do
  if (!principalHost.EqualsIgnoreCase(channelHost.get())) {
    return false;
  }

  // also check that uri matches the one of the originalURI
  if (!channelHost.EqualsIgnoreCase(origChannelHost.get())) {
    return false;
  }

  return true;
}

nsresult nsCORSListenerProxy::UpdateChannel(nsIChannel* aChannel,
                                            DataURIHandling aAllowDataURI,
                                            UpdateType aUpdateType,
                                            bool aStripAuthHeader) {
  MOZ_ASSERT_IF(aUpdateType == UpdateType::InternalOrHSTSRedirect,
                !aStripAuthHeader);
  nsCOMPtr<nsIURI> uri, originalURI;
  nsresult rv = NS_GetFinalChannelURI(aChannel, getter_AddRefs(uri));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = aChannel->GetOriginalURI(getter_AddRefs(originalURI));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();

  // Introduced for DevTools in order to allow overriding some requests
  // with the content of data: URIs.
  if (loadInfo->GetAllowInsecureRedirectToDataURI() && uri->SchemeIs("data")) {
    return NS_OK;
  }

  // exempt data URIs from the same origin check.
  if (aAllowDataURI == DataURIHandling::Allow && originalURI == uri) {
    if (uri->SchemeIs("data")) {
      return NS_OK;
    }
    if (loadInfo->GetAboutBlankInherits() && NS_IsAboutBlank(uri)) {
      return NS_OK;
    }
  }

  // Set CORS attributes on channel so that intercepted requests get correct
  // values. We have to do this here because the CheckMayLoad checks may lead
  // to early return. We can't be sure this is an http channel though, so we
  // can't return early on failure.
  nsCOMPtr<nsIHttpChannelInternal> internal = do_QueryInterface(aChannel);
  if (internal) {
    rv = internal->SetRequestMode(dom::RequestMode::Cors);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = internal->SetCorsIncludeCredentials(mWithCredentials);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // TODO: Bug 1353683
  // consider calling SetBlockedRequest in nsCORSListenerProxy::UpdateChannel
  //
  // Check that the uri is ok to load
  uint32_t flags = loadInfo->CheckLoadURIFlags();
  rv = nsContentUtils::GetSecurityManager()->CheckLoadURIWithPrincipal(
      mRequestingPrincipal, uri, flags, loadInfo->GetInnerWindowID());
  NS_ENSURE_SUCCESS(rv, rv);

  if (originalURI != uri) {
    rv = nsContentUtils::GetSecurityManager()->CheckLoadURIWithPrincipal(
        mRequestingPrincipal, originalURI, flags, loadInfo->GetInnerWindowID());
    NS_ENSURE_SUCCESS(rv, rv);
  }

  if (uri->SchemeIs("moz-extension")) {
    // moz-extension:-URLs do not support CORS, but can universally be read
    // if an extension lists the resource in web_accessible_resources.
    // This is enforced via the CheckLoadURIWithPrincipal call above:
    // moz-extension resources have the URI_DANGEROUS_TO_LOAD flag, unless
    // listed in web_accessible_resources.
    return NS_OK;
  }

  if (!mHasBeenCrossSite &&
      NS_SUCCEEDED(mRequestingPrincipal->CheckMayLoad(uri, false)) &&
      (originalURI == uri ||
       NS_SUCCEEDED(mRequestingPrincipal->CheckMayLoad(originalURI, false)))) {
    return NS_OK;
  }

  // If the CSP directive 'upgrade-insecure-requests' is used or the HTTPS-Only
  // Mode is enabled then we should not incorrectly require CORS if the only
  // difference of a subresource request and the main page is the scheme. e.g.
  // toplevel page: https://www.example.com loads
  //           xhr: http://www.example.com/somefoo,
  // then the xhr request will be upgraded to https before it fetches any data
  // from the netwerk, hence we shouldn't require CORS in that specific case.
  if (CheckInsecureUpgradePreventsCORS(mRequestingPrincipal, aChannel)) {
    // Check if https-only mode upgrades this later anyway
    nsCOMPtr<nsILoadInfo> loadinfo = aChannel->LoadInfo();
    if (nsHTTPSOnlyUtils::IsSafeToAcceptCORSOrMixedContent(loadinfo)) {
      return NS_OK;
    }
    // Check if 'upgrade-insecure-requests' is used
    if (loadInfo->GetUpgradeInsecureRequests() ||
        loadInfo->GetBrowserUpgradeInsecureRequests()) {
      return NS_OK;
    }
  }

  // Check if we need to do a preflight, and if so set one up. This must be
  // called once we know that the request is going, or has gone, cross-origin.
  rv = CheckPreflightNeeded(aChannel, aUpdateType, aStripAuthHeader);
  NS_ENSURE_SUCCESS(rv, rv);

  // It's a cross site load
  mHasBeenCrossSite = true;

  if (mIsRedirect) {
    // https://fetch.spec.whatwg.org/#http-redirect-fetch
    // Step 9. If request’s mode is "cors", locationURL includes credentials,
    // and request’s origin is not same origin with locationURL’s origin,
    // then return a network error.

    nsAutoCString userpass;
    uri->GetUserPass(userpass);
    NS_ENSURE_TRUE(userpass.IsEmpty(), NS_ERROR_DOM_BAD_URI);
  }

  // If we have an expanded principal here, we'll reject the CORS request,
  // because we can't send a useful Origin header which is required for CORS.
  if (nsContentUtils::IsExpandedPrincipal(mOriginHeaderPrincipal)) {
    nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aChannel);
    LogBlockedRequest(aChannel, "CORSOriginHeaderNotAdded", nullptr,
                      nsILoadInfo::BLOCKING_REASON_CORSORIGINHEADERNOTADDED,
                      httpChannel);
    return NS_ERROR_DOM_BAD_URI;
  }

  // Add the Origin header
  nsAutoCString origin;
  rv = mOriginHeaderPrincipal->GetWebExposedOriginSerialization(origin);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIHttpChannel> http = do_QueryInterface(aChannel);
  NS_ENSURE_TRUE(http, NS_ERROR_FAILURE);

  // hide the Origin header when requesting from .onion and requesting CORS
  if (StaticPrefs::network_http_referer_hideOnionSource()) {
    if (mOriginHeaderPrincipal->GetIsOnion()) {
      origin.AssignLiteral("null");
    }
  }

  rv = http->SetRequestHeader(net::nsHttp::Origin.val(), origin, false);
  NS_ENSURE_SUCCESS(rv, rv);

  // Make cookie-less if needed. We don't need to do anything here if the
  // channel was opened with AsyncOpen, since then AsyncOpen will take
  // care of the cookie policy for us.
  if (!mWithCredentials) {
    nsLoadFlags flags;
    rv = http->GetLoadFlags(&flags);
    NS_ENSURE_SUCCESS(rv, rv);

    flags |= nsIRequest::LOAD_ANONYMOUS;
    if (StaticPrefs::network_cors_preflight_allow_client_cert()) {
      flags |= nsIRequest::LOAD_ANONYMOUS_ALLOW_CLIENT_CERT;
    }
    rv = http->SetLoadFlags(flags);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  mHttpChannel = std::move(http);

  return NS_OK;
}

nsresult nsCORSListenerProxy::CheckPreflightNeeded(nsIChannel* aChannel,
                                                   UpdateType aUpdateType,
                                                   bool aStripAuthHeader) {
  // If this caller isn't using AsyncOpen, or if this *is* a preflight channel,
  // then we shouldn't initiate preflight for this channel.
  nsCOMPtr<nsILoadInfo> loadInfo = aChannel->LoadInfo();
  if (loadInfo->GetSecurityMode() !=
          nsILoadInfo::SEC_REQUIRE_CORS_INHERITS_SEC_CONTEXT ||
      loadInfo->GetIsPreflight()) {
    return NS_OK;
  }

  bool doPreflight = loadInfo->GetForcePreflight();

  nsCOMPtr<nsIHttpChannel> http = do_QueryInterface(aChannel);
  if (!http) {
    // Note: A preflight is not needed for moz-extension:-requests either, but
    // there is already a check for that in the caller of CheckPreflightNeeded,
    // in UpdateChannel.
    LogBlockedRequest(aChannel, "CORSRequestNotHttp", nullptr,
                      nsILoadInfo::BLOCKING_REASON_CORSREQUESTNOTHTTP,
                      mHttpChannel);
    return NS_ERROR_DOM_BAD_URI;
  }

  nsAutoCString method;
  (void)http->GetRequestMethod(method);
  if (!method.LowerCaseEqualsLiteral("get") &&
      !method.LowerCaseEqualsLiteral("post") &&
      !method.LowerCaseEqualsLiteral("head")) {
    doPreflight = true;
  }

  // Avoid copying the array here
  const nsTArray<nsCString>& loadInfoHeaders = loadInfo->CorsUnsafeHeaders();
  if (!loadInfoHeaders.IsEmpty()) {
    doPreflight = true;
  }

  // Add Content-Type header if needed
  nsTArray<nsCString> headers;
  nsAutoCString contentTypeHeader;
  nsresult rv = http->GetRequestHeader("Content-Type"_ns, contentTypeHeader);
  // GetRequestHeader return an error if the header is not set. Don't add
  // "content-type" to the list if that's the case.
  if (NS_SUCCEEDED(rv) &&
      !nsContentUtils::IsAllowedNonCorsContentType(contentTypeHeader) &&
      !loadInfoHeaders.Contains("content-type"_ns,
                                nsCaseInsensitiveCStringArrayComparator())) {
    headers.AppendElements(loadInfoHeaders);
    headers.AppendElement("content-type"_ns);
    doPreflight = true;
  }

  if (!doPreflight) {
    return NS_OK;
  }

  nsCOMPtr<nsIHttpChannelInternal> internal = do_QueryInterface(http);
  if (!internal) {
    auto statusCode = GetStatusCodeAsString(http);
    LogBlockedRequest(aChannel, "CORSDidNotSucceed2", statusCode.get(),
                      nsILoadInfo::BLOCKING_REASON_CORSDIDNOTSUCCEED,
                      mHttpChannel);
    return NS_ERROR_DOM_BAD_URI;
  }

  internal->SetCorsPreflightParameters(
      headers.IsEmpty() ? loadInfoHeaders : headers,
      aUpdateType == UpdateType::StripRequestBodyHeader, aStripAuthHeader);

  return NS_OK;
}

//////////////////////////////////////////////////////////////////////////
// Preflight proxy

// Class used as streamlistener and notification callback when
// doing the initial OPTIONS request for a CORS check
class nsCORSPreflightListener final : public nsIStreamListener,
                                      public nsIInterfaceRequestor,
                                      public nsIChannelEventSink {
 public:
  nsCORSPreflightListener(nsIPrincipal* aReferrerPrincipal,
                          nsICorsPreflightCallback* aCallback,
                          nsILoadContext* aLoadContext, bool aWithCredentials,
                          const nsCString& aPreflightMethod,
                          const nsTArray<nsCString>& aPreflightHeaders)
      : mPreflightMethod(aPreflightMethod),
        mPreflightHeaders(aPreflightHeaders.Clone()),
        mReferrerPrincipal(aReferrerPrincipal),
        mCallback(aCallback),
        mLoadContext(aLoadContext),
        mWithCredentials(aWithCredentials) {}

  NS_DECL_ISUPPORTS
  NS_DECL_NSISTREAMLISTENER
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSIINTERFACEREQUESTOR
  NS_DECL_NSICHANNELEVENTSINK

  nsresult CheckPreflightRequestApproved(nsIRequest* aRequest);

 private:
  ~nsCORSPreflightListener() = default;

  void AddResultToCache(nsIRequest* aRequest);

  nsCString mPreflightMethod;
  nsTArray<nsCString> mPreflightHeaders;
  nsCOMPtr<nsIPrincipal> mReferrerPrincipal;
  nsCOMPtr<nsICorsPreflightCallback> mCallback;
  nsCOMPtr<nsILoadContext> mLoadContext;
  bool mWithCredentials;
};

NS_IMPL_ISUPPORTS(nsCORSPreflightListener, nsIStreamListener,
                  nsIRequestObserver, nsIInterfaceRequestor,
                  nsIChannelEventSink)

void nsCORSPreflightListener::AddResultToCache(nsIRequest* aRequest) {
  nsCOMPtr<nsIHttpChannel> http = do_QueryInterface(aRequest);
  NS_ASSERTION(http, "Request was not http");

  // The "Access-Control-Max-Age" header should return an age in seconds.
  nsAutoCString headerVal;
  uint32_t age = 0;
  (void)http->GetResponseHeader("Access-Control-Max-Age"_ns, headerVal);
  if (headerVal.IsEmpty()) {
    age = PREFLIGHT_DEFAULT_EXPIRY_SECONDS;
  } else {
    // Sanitize the string. We only allow 'delta-seconds' as specified by
    // http://dev.w3.org/2006/waf/access-control (digits 0-9 with no leading or
    // trailing non-whitespace characters).
    nsACString::const_char_iterator iter, end;
    headerVal.BeginReading(iter);
    headerVal.EndReading(end);
    while (iter != end) {
      if (*iter < '0' || *iter > '9') {
        return;
      }
      age = age * 10 + (*iter - '0');
      // Cap at 24 hours. This also avoids overflow
      age = std::min(age, 86400U);
      ++iter;
    }
  }

  if (!age || !EnsurePreflightCache()) {
    return;
  }

  // String seems fine, go ahead and cache.
  // Note that we have already checked that these headers follow the correct
  // syntax.

  nsCOMPtr<nsIURI> uri;
  NS_GetFinalChannelURI(http, getter_AddRefs(uri));

  TimeStamp expirationTime =
      TimeStamp::NowLoRes() + TimeDuration::FromSeconds(age);

  OriginAttributes attrs;
  StoragePrincipalHelper::GetOriginAttributesForNetworkState(http, attrs);

  RefPtr<CORSCacheEntry> entry = sPreflightCache->GetEntry(
      uri, mReferrerPrincipal, mWithCredentials, attrs, true);
  if (!entry) {
    return;
  }

  nsCOMPtr<nsIHttpChannelInternal> httpChannelInternal(
      do_QueryInterface(aRequest));
  if (httpChannelInternal) {
    (void)httpChannelInternal->GetIsProxyUsed(&entry->mIsProxyUsed);
  }

  // The "Access-Control-Allow-Methods" header contains a comma separated
  // list of method names.
  (void)http->GetResponseHeader("Access-Control-Allow-Methods"_ns, headerVal);

  for (const nsACString& method :
       nsCCharSeparatedTokenizer(headerVal, ',').ToRange()) {
    if (method.IsEmpty()) {
      continue;
    }
    uint32_t i;
    for (i = 0; i < entry->mMethods.Length(); ++i) {
      if (entry->mMethods[i].token.Equals(method)) {
        entry->mMethods[i].expirationTime = expirationTime;
        break;
      }
    }
    if (i == entry->mMethods.Length()) {
      nsPreflightCache::TokenTime* newMethod = entry->mMethods.AppendElement();
      if (!newMethod) {
        return;
      }

      newMethod->token = method;
      newMethod->expirationTime = expirationTime;
    }
  }

  // The "Access-Control-Allow-Headers" header contains a comma separated
  // list of method names.
  (void)http->GetResponseHeader("Access-Control-Allow-Headers"_ns, headerVal);

  for (const nsACString& header :
       nsCCharSeparatedTokenizer(headerVal, ',').ToRange()) {
    if (header.IsEmpty()) {
      continue;
    }
    uint32_t i;
    for (i = 0; i < entry->mHeaders.Length(); ++i) {
      if (entry->mHeaders[i].token.Equals(header)) {
        entry->mHeaders[i].expirationTime = expirationTime;
        break;
      }
    }
    if (i == entry->mHeaders.Length()) {
      nsPreflightCache::TokenTime* newHeader = entry->mHeaders.AppendElement();
      if (!newHeader) {
        return;
      }

      newHeader->token = header;
      newHeader->expirationTime = expirationTime;
    }
  }
}

NS_IMETHODIMP
nsCORSPreflightListener::OnStartRequest(nsIRequest* aRequest) {
#ifdef DEBUG
  {
    nsCOMPtr<nsIChannel> channel = do_QueryInterface(aRequest);
    nsCOMPtr<nsILoadInfo> loadInfo = channel ? channel->LoadInfo() : nullptr;
    MOZ_ASSERT(!loadInfo || !loadInfo->GetServiceWorkerTaintingSynthesized());
  }
#endif

  nsresult rv = CheckPreflightRequestApproved(aRequest);

  if (NS_SUCCEEDED(rv)) {
    // Everything worked, try to cache and then fire off the actual request.
    AddResultToCache(aRequest);

    mCallback->OnPreflightSucceeded();
  } else {
    mCallback->OnPreflightFailed(rv);
  }

  return rv;
}

NS_IMETHODIMP
nsCORSPreflightListener::OnStopRequest(nsIRequest* aRequest, nsresult aStatus) {
  mCallback = nullptr;
  return NS_OK;
}

/** nsIStreamListener methods **/

NS_IMETHODIMP
nsCORSPreflightListener::OnDataAvailable(nsIRequest* aRequest,
                                         nsIInputStream* inStr,
                                         uint64_t sourceOffset,
                                         uint32_t count) {
  uint32_t totalRead;
  return inStr->ReadSegments(NS_DiscardSegment, nullptr, count, &totalRead);
}

NS_IMETHODIMP
nsCORSPreflightListener::AsyncOnChannelRedirect(
    nsIChannel* aOldChannel, nsIChannel* aNewChannel, uint32_t aFlags,
    nsIAsyncVerifyRedirectCallback* callback) {
  // Only internal redirects allowed for now.
  if (!NS_IsInternalSameURIRedirect(aOldChannel, aNewChannel, aFlags) &&
      !NS_IsHSTSUpgradeRedirect(aOldChannel, aNewChannel, aFlags)) {
    nsCOMPtr<nsIHttpChannel> httpChannel = do_QueryInterface(aOldChannel);
    LogBlockedRequest(
        aOldChannel, "CORSExternalRedirectNotAllowed", nullptr,
        nsILoadInfo::BLOCKING_REASON_CORSEXTERNALREDIRECTNOTALLOWED,
        httpChannel);
    return NS_ERROR_DOM_BAD_URI;
  }

  callback->OnRedirectVerifyCallback(NS_OK);
  return NS_OK;
}

nsresult nsCORSPreflightListener::CheckPreflightRequestApproved(
    nsIRequest* aRequest) {
  nsresult status;
  nsresult rv = aRequest->GetStatus(&status);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_SUCCESS(status, status);

  // Test that things worked on a HTTP level
  nsCOMPtr<nsIHttpChannel> http = do_QueryInterface(aRequest);
  nsCOMPtr<nsIHttpChannelInternal> internal = do_QueryInterface(aRequest);
  NS_ENSURE_STATE(internal);
  nsCOMPtr<nsIHttpChannel> parentHttpChannel = do_QueryInterface(mCallback);

  bool succeedded;
  rv = http->GetRequestSucceeded(&succeedded);
  if (NS_FAILED(rv) || !succeedded) {
    auto statusCode = GetStatusCodeAsString(http);
    LogBlockedRequest(aRequest, "CORSPreflightDidNotSucceed3", statusCode.get(),
                      nsILoadInfo::BLOCKING_REASON_CORSPREFLIGHTDIDNOTSUCCEED,
                      parentHttpChannel);
    return NS_ERROR_DOM_BAD_URI;
  }

  nsAutoCString headerVal;
  // The "Access-Control-Allow-Methods" header contains a comma separated
  // list of method names.
  (void)http->GetResponseHeader("Access-Control-Allow-Methods"_ns, headerVal);
  bool foundMethod = mPreflightMethod.EqualsLiteral("GET") ||
                     mPreflightMethod.EqualsLiteral("HEAD") ||
                     mPreflightMethod.EqualsLiteral("POST");
  for (const nsACString& method :
       nsCCharSeparatedTokenizer(headerVal, ',').ToRange()) {
    if (method.IsEmpty()) {
      continue;
    }
    if (!NS_IsValidHTTPToken(method)) {
      LogBlockedRequest(aRequest, "CORSInvalidAllowMethod",
                        NS_ConvertUTF8toUTF16(method).get(),
                        nsILoadInfo::BLOCKING_REASON_CORSINVALIDALLOWMETHOD,
                        parentHttpChannel);
      return NS_ERROR_DOM_BAD_URI;
    }

    if (method.EqualsLiteral("*") && !mWithCredentials) {
      foundMethod = true;
    } else {
      foundMethod |= mPreflightMethod.Equals(method);
    }
  }
  if (!foundMethod) {
    LogBlockedRequest(aRequest, "CORSMethodNotFound", nullptr,
                      nsILoadInfo::BLOCKING_REASON_CORSMETHODNOTFOUND,
                      parentHttpChannel);
    return NS_ERROR_DOM_BAD_URI;
  }

  // The "Access-Control-Allow-Headers" header contains a comma separated
  // list of header names.
  (void)http->GetResponseHeader("Access-Control-Allow-Headers"_ns, headerVal);
  nsTArray<nsCString> headers;
  bool wildcard = false;
  bool hasAuthorizationHeader = false;
  for (const nsACString& header :
       nsCCharSeparatedTokenizer(headerVal, ',').ToRange()) {
    if (header.IsEmpty()) {
      continue;
    }
    if (!NS_IsValidHTTPToken(header)) {
      LogBlockedRequest(aRequest, "CORSInvalidAllowHeader",
                        NS_ConvertUTF8toUTF16(header).get(),
                        nsILoadInfo::BLOCKING_REASON_CORSINVALIDALLOWHEADER,
                        parentHttpChannel);
      return NS_ERROR_DOM_BAD_URI;
    }
    if (header.EqualsLiteral("*") && !mWithCredentials) {
      wildcard = true;
    } else {
      headers.AppendElement(header);
    }

    if (header.LowerCaseEqualsASCII("authorization")) {
      hasAuthorizationHeader = true;
    }
  }

  bool authorizationInPreflightHeaders = false;
  bool authorizationCoveredByWildcard = false;
  for (uint32_t i = 0; i < mPreflightHeaders.Length(); ++i) {
    // Cache the result of the authorization header.
    bool isAuthorization =
        mPreflightHeaders[i].LowerCaseEqualsASCII("authorization");
    if (wildcard) {
      if (!isAuthorization) {
        continue;
      } else {
        authorizationInPreflightHeaders = true;
        if (StaticPrefs::
                network_cors_preflight_authorization_covered_by_wildcard() &&
            !hasAuthorizationHeader) {
          // When `Access-Control-Allow-Headers` is `*` and there is no
          // `Authorization` header listed, we send a deprecation warning to the
          // console.
          LogBlockedRequest(aRequest, "CORSAllowHeaderFromPreflightDeprecation",
                            nullptr, 0, parentHttpChannel, true);
          glean::network::cors_authorization_header
              .Get("covered_by_wildcard"_ns)
              .Add(1);
          authorizationCoveredByWildcard = true;
          continue;
        }
      }
    }

    const auto& comparator = nsCaseInsensitiveCStringArrayComparator();
    if (!headers.Contains(mPreflightHeaders[i], comparator)) {
      LogBlockedRequest(
          aRequest, "CORSMissingAllowHeaderFromPreflight2",
          NS_ConvertUTF8toUTF16(mPreflightHeaders[i]).get(),
          nsILoadInfo::BLOCKING_REASON_CORSMISSINGALLOWHEADERFROMPREFLIGHT,
          parentHttpChannel);
      if (isAuthorization) {
        glean::network::cors_authorization_header.Get("disallowed"_ns).Add(1);
      }
      return NS_ERROR_DOM_BAD_URI;
    }
  }

  if (authorizationInPreflightHeaders && !authorizationCoveredByWildcard) {
    glean::network::cors_authorization_header.Get("allowed"_ns).Add(1);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsCORSPreflightListener::GetInterface(const nsIID& aIID, void** aResult) {
  if (aIID.Equals(NS_GET_IID(nsILoadContext)) && mLoadContext) {
    nsCOMPtr<nsILoadContext> copy = mLoadContext;
    copy.forget(aResult);
    return NS_OK;
  }

  return QueryInterface(aIID, aResult);
}

void nsCORSListenerProxy::RemoveFromCorsPreflightCache(
    nsIURI* aURI, nsIPrincipal* aRequestingPrincipal,
    const OriginAttributes& aOriginAttributes) {
  MOZ_ASSERT(XRE_IsParentProcess());
  if (sPreflightCache) {
    sPreflightCache->RemoveEntries(aURI, aRequestingPrincipal,
                                   aOriginAttributes);
  }
}

// static
nsresult nsCORSListenerProxy::StartCORSPreflight(
    nsIChannel* aRequestChannel, nsICorsPreflightCallback* aCallback,
    nsTArray<nsCString>& aUnsafeHeaders, nsIChannel** aPreflightChannel) {
  *aPreflightChannel = nullptr;

  if (StaticPrefs::content_cors_disable()) {
    nsCOMPtr<nsIHttpChannel> http = do_QueryInterface(aRequestChannel);
    LogBlockedRequest(aRequestChannel, "CORSDisabled", nullptr,
                      nsILoadInfo::BLOCKING_REASON_CORSDISABLED, http);
    return NS_ERROR_DOM_BAD_URI;
  }

  nsAutoCString method;
  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(aRequestChannel));
  NS_ENSURE_TRUE(httpChannel, NS_ERROR_UNEXPECTED);
  (void)httpChannel->GetRequestMethod(method);

  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_GetFinalChannelURI(aRequestChannel, getter_AddRefs(uri));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsILoadInfo> originalLoadInfo = aRequestChannel->LoadInfo();
  MOZ_ASSERT(originalLoadInfo->GetSecurityMode() ==
                 nsILoadInfo::SEC_REQUIRE_CORS_INHERITS_SEC_CONTEXT,
             "how did we end up here?");

  nsCOMPtr<nsIPrincipal> principal = originalLoadInfo->GetLoadingPrincipal();
  MOZ_ASSERT(principal && originalLoadInfo->GetExternalContentPolicyType() !=
                              ExtContentPolicy::TYPE_DOCUMENT,
             "Should not do CORS loads for top-level loads, so a "
             "loadingPrincipal should always exist.");
  bool withCredentials =
      originalLoadInfo->GetCookiePolicy() == nsILoadInfo::SEC_COOKIES_INCLUDE;

  RefPtr<CORSCacheEntry> entry;

  nsLoadFlags loadFlags;
  rv = aRequestChannel->GetLoadFlags(&loadFlags);
  NS_ENSURE_SUCCESS(rv, rv);

  // Disable preflight cache if devtools says so or on other force reloads
  bool disableCache = (loadFlags & nsIRequest::LOAD_BYPASS_CACHE);

  if (sPreflightCache && !disableCache) {
    OriginAttributes attrs;
    StoragePrincipalHelper::GetOriginAttributesForNetworkState(aRequestChannel,
                                                               attrs);
    entry = sPreflightCache->GetEntry(uri, principal, withCredentials, attrs,
                                      false);
  }

  if (entry && entry->CheckRequest(method, aUnsafeHeaders)) {
    aCallback->OnPreflightSucceeded();
    return NS_OK;
  }

  // Either it wasn't cached or the cached result has expired. Build a
  // channel for the OPTIONS request.

  nsCOMPtr<nsILoadInfo> loadInfo =
      static_cast<mozilla::net::LoadInfo*>(originalLoadInfo.get())
          ->CloneForNewRequest();
  static_cast<mozilla::net::LoadInfo*>(loadInfo.get())->SetIsPreflight();

  nsCOMPtr<nsILoadGroup> loadGroup;
  rv = aRequestChannel->GetLoadGroup(getter_AddRefs(loadGroup));
  NS_ENSURE_SUCCESS(rv, rv);

  // We want to give the preflight channel's notification callbacks the same
  // load context as the original channel's notification callbacks had.  We
  // don't worry about a load context provided via the loadgroup here, since
  // they have the same loadgroup.
  nsCOMPtr<nsIInterfaceRequestor> callbacks;
  rv = aRequestChannel->GetNotificationCallbacks(getter_AddRefs(callbacks));
  NS_ENSURE_SUCCESS(rv, rv);
  nsCOMPtr<nsILoadContext> loadContext = do_GetInterface(callbacks);

  // Preflight requests should never be intercepted by service workers and
  // are always anonymous.
  // NOTE: We ignore CORS checks on synthesized responses (see the CORS
  // preflights, then we need to extend the GetResponseSynthesized() check in
  // nsCORSListenerProxy::CheckRequestApproved()). If we change our behavior
  // here and allow service workers to intercept CORS preflights, then that
  // check won't be safe any more.
  loadFlags |=
      nsIChannel::LOAD_BYPASS_SERVICE_WORKER | nsIRequest::LOAD_ANONYMOUS;

  if (StaticPrefs::network_cors_preflight_allow_client_cert()) {
    loadFlags |= nsIRequest::LOAD_ANONYMOUS_ALLOW_CLIENT_CERT;
  }

  nsCOMPtr<nsIChannel> preflightChannel;
  rv = NS_NewChannelInternal(getter_AddRefs(preflightChannel), uri, loadInfo,
                             nullptr,  // PerformanceStorage
                             loadGroup,
                             nullptr,  // aCallbacks
                             loadFlags);
  NS_ENSURE_SUCCESS(rv, rv);

  // Set method and headers
  nsCOMPtr<nsIHttpChannel> preHttp = do_QueryInterface(preflightChannel);
  NS_ASSERTION(preHttp, "Failed to QI to nsIHttpChannel!");

  rv = preHttp->SetRequestMethod("OPTIONS"_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = preHttp->SetRequestHeader("Access-Control-Request-Method"_ns, method,
                                 false);
  NS_ENSURE_SUCCESS(rv, rv);

  // Set the CORS preflight channel's warning reporter to be the same as the
  // requesting channel so that all log messages are able to be reported through
  // the warning reporter.
  RefPtr<nsHttpChannel> reqCh = do_QueryObject(aRequestChannel);
  RefPtr<nsHttpChannel> preCh = do_QueryObject(preHttp);
  if (preCh && reqCh) {  // there are other implementers of nsIHttpChannel
    preCh->SetWarningReporter(reqCh->GetWarningReporter());
  }

  nsTArray<nsCString> preflightHeaders;
  if (!aUnsafeHeaders.IsEmpty()) {
    for (uint32_t i = 0; i < aUnsafeHeaders.Length(); ++i) {
      preflightHeaders.AppendElement();
      ToLowerCase(aUnsafeHeaders[i], preflightHeaders[i]);
    }
    preflightHeaders.Sort();
    nsAutoCString headers;
    for (uint32_t i = 0; i < preflightHeaders.Length(); ++i) {
      if (i != 0) {
        headers += ',';
      }
      headers += preflightHeaders[i];
    }
    rv = preHttp->SetRequestHeader("Access-Control-Request-Headers"_ns, headers,
                                   false);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  // Set up listener which will start the original channel
  RefPtr<nsCORSPreflightListener> preflightListener =
      new nsCORSPreflightListener(principal, aCallback, loadContext,
                                  withCredentials, method, preflightHeaders);

  rv = preflightChannel->SetNotificationCallbacks(preflightListener);
  NS_ENSURE_SUCCESS(rv, rv);

  if (preCh && reqCh) {
    // Per https://fetch.spec.whatwg.org/#cors-preflight-fetch step 1, the
    // request's referrer and referrer policy should match the original request.
    nsCOMPtr<nsIReferrerInfo> referrerInfo;
    rv = reqCh->GetReferrerInfo(getter_AddRefs(referrerInfo));
    NS_ENSURE_SUCCESS(rv, rv);
    if (referrerInfo) {
      nsCOMPtr<nsIReferrerInfo> newReferrerInfo =
          static_cast<dom::ReferrerInfo*>(referrerInfo.get())->Clone();
      rv = preCh->SetReferrerInfo(newReferrerInfo);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  // Start preflight
  rv = preflightChannel->AsyncOpen(preflightListener);
  NS_ENSURE_SUCCESS(rv, rv);

  // Return newly created preflight channel
  preflightChannel.forget(aPreflightChannel);

  return NS_OK;
}

// static
void nsCORSListenerProxy::LogBlockedCORSRequest(
    uint64_t aInnerWindowID, bool aPrivateBrowsing, bool aFromChromeContext,
    const nsAString& aMessage, const nsACString& aCategory, bool aIsWarning) {
  nsresult rv = NS_OK;

  // Build the error object and log it to the console
  nsCOMPtr<nsIConsoleService> console(
      mozilla::components::Console::Service(&rv));
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to log blocked cross-site request (no console)");
    return;
  }

  nsCOMPtr<nsIScriptError> scriptError =
      do_CreateInstance(NS_SCRIPTERROR_CONTRACTID, &rv);
  if (NS_FAILED(rv)) {
    NS_WARNING("Failed to log blocked cross-site request (no scriptError)");
    return;
  }

  uint32_t errorFlag =
      aIsWarning ? nsIScriptError::warningFlag : nsIScriptError::errorFlag;

  // query innerWindowID and log to web console, otherwise log to
  // the error to the browser console.
  if (aInnerWindowID > 0) {
    rv = scriptError->InitWithSanitizedSource(aMessage,
                                              ""_ns,  // sourceName
                                              0,      // lineNumber
                                              0,      // columnNumber
                                              errorFlag, aCategory,
                                              aInnerWindowID);
  } else {
    rv = scriptError->Init(aMessage,
                           ""_ns,  // sourceName
                           0,      // lineNumber
                           0,      // columnNumber
                           errorFlag, aCategory, aPrivateBrowsing,
                           aFromChromeContext);  // From chrome context
  }
  if (NS_FAILED(rv)) {
    NS_WARNING(
        "Failed to log blocked cross-site request (scriptError init failed)");
    return;
  }
  console->LogMessage(scriptError);
}
