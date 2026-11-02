/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CookieStorage.h"

#include <limits>

#include "Cookie.h"
#include "CookieCommons.h"
#include "CookieLogging.h"
#include "CookieNotification.h"
#include "CookieParser.h"
#include "CookieService.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/dom/nsMixedContentBlocker.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "mozilla/net/MozURL_ffi.h"
#include "nsCOMPtr.h"
#include "nsComponentManagerUtils.h"
#include "nsICookieNotification.h"
#include "nsIMutableArray.h"
#include "nsIPrefService.h"
#include "nsIScriptError.h"
#include "nsIUserIdleService.h"
#include "nsServiceManagerUtils.h"
#include "nsTPriorityQueue.h"
#include "prprf.h"

#undef ADD_TEN_PERCENT
#define ADD_TEN_PERCENT(i) static_cast<uint32_t>((i) + (i) / 10)

#undef LIMIT
#define LIMIT(x, low, high, default) \
  ((x) >= (low) && (x) <= (high) ? (x) : (default))

// in order to keep our metrics consistent
// we only send metrics when the pref hasn't been manipulated from the default
static const uint32_t kChipsPartitionByteCapacityDefault = 10240;
static const double kChipsHardLimitFactor = 1.2;

namespace mozilla {
namespace net {

// comparator class for lastaccessed times of cookies.
class CookieStorage::CompareCookiesByAge {
 public:
  static bool Equals(const CookieListIter& a, const CookieListIter& b) {
    return a.Cookie()->LastAccessedInUSec() ==
               b.Cookie()->LastAccessedInUSec() &&
           a.Cookie()->CreationTimeInUSec() == b.Cookie()->CreationTimeInUSec();
  }

  static bool LessThan(const CookieListIter& a, const CookieListIter& b) {
    // compare by lastAccessedInUSec time, and tiebreak by creationTimeInUSec.
    int64_t result =
        a.Cookie()->LastAccessedInUSec() - b.Cookie()->LastAccessedInUSec();
    if (result != 0) {
      return result < 0;
    }

    return a.Cookie()->CreationTimeInUSec() < b.Cookie()->CreationTimeInUSec();
  }
};

// Cookie comparator for the priority queue used in FindStaleCookies.
// Note that the expired cookie has the highest priority.
// Other non-expired cookies are sorted by their age.
class CookieStorage::CookieIterComparator {
 private:
  int64_t mCurrentTimeInMSec;

 public:
  explicit CookieIterComparator(int64_t aTimeInMSec)
      : mCurrentTimeInMSec(aTimeInMSec) {}

  bool LessThan(const CookieListIter& lhs, const CookieListIter& rhs) {
    bool lExpired = lhs.Cookie()->IsExpired(mCurrentTimeInMSec);
    bool rExpired = rhs.Cookie()->IsExpired(mCurrentTimeInMSec);
    if (lExpired && !rExpired) {
      return true;
    }

    if (!lExpired && rExpired) {
      return false;
    }

    return CompareCookiesByAge::LessThan(lhs, rhs);
  }
};

// comparator class for sorting cookies by entry and index.
class CookieStorage::CompareCookiesByIndex {
 public:
  static bool Equals(const CookieListIter& a, const CookieListIter& b) {
    NS_ASSERTION(a.entry != b.entry || a.index != b.index,
                 "cookie indexes should never be equal");
    return false;
  }

  static bool LessThan(const CookieListIter& a, const CookieListIter& b) {
    // compare by entryclass pointer, then by index.
    if (a.entry != b.entry) {
      return a.entry < b.entry;
    }

    return a.index < b.index;
  }
};

// ---------------------------------------------------------------------------
// CookieEntry

size_t CookieEntry::SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const {
  size_t amount = CookieKey::SizeOfExcludingThis(aMallocSizeOf);

  amount += mCookies.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (uint32_t i = 0; i < mCookies.Length(); ++i) {
    amount += mCookies[i]->SizeOfIncludingThis(aMallocSizeOf);
  }

  return amount;
}

bool CookieEntry::IsPartitioned() const {
  return !mOriginAttributes.mPartitionKey.IsEmpty();
}

// ---------------------------------------------------------------------------
// CookieStorage

NS_IMPL_ISUPPORTS(CookieStorage, nsIObserver, nsISupportsWeakReference)

void CookieStorage::Init() {
  // init our pref and observer
  nsCOMPtr<nsIPrefBranch> prefBranch = do_GetService(NS_PREFSERVICE_CONTRACTID);
  if (prefBranch) {
    prefBranch->AddObserver(kPrefMaxNumberOfCookies, this, true);
    prefBranch->AddObserver(kPrefMaxCookiesPerHost, this, true);
    prefBranch->AddObserver(kPrefCookiePurgeAge, this, true);
    PrefChanged(prefBranch);
  }

  nsCOMPtr<nsIObserverService> observerService = services::GetObserverService();
  NS_ENSURE_TRUE_VOID(observerService);

  nsresult rv =
      observerService->AddObserver(this, OBSERVER_TOPIC_IDLE_DAILY, true);
  NS_ENSURE_SUCCESS_VOID(rv);
}

size_t CookieStorage::SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const {
  size_t amount = 0;

  amount += aMallocSizeOf(this);
  amount += mHostTable.SizeOfExcludingThis(aMallocSizeOf);

  return amount;
}

void CookieStorage::GetSessionCookies(
    nsTArray<RefPtr<nsICookie>>& aCookies) const {
  int64_t currentTimeInMSec = PR_Now() / PR_USEC_PER_MSEC;

  aCookies.SetCapacity(mCookieCount);
  for (const auto& entry : mHostTable) {
    const CookieEntry::ArrayType& cookies = entry.GetCookies();
    for (CookieEntry::IndexType i = 0; i < cookies.Length(); ++i) {
      Cookie* cookie = cookies[i];
      // Filter out non-session and expired cookies.
      if (cookie->IsSession() && !cookie->IsExpired(currentTimeInMSec)) {
        aCookies.AppendElement(cookie);
      }
    }
  }
}

// find an exact cookie specified by host, name, and path that hasn't expired.
already_AddRefed<Cookie> CookieStorage::FindCookie(
    const nsACString& aBaseDomain, const OriginAttributes& aOriginAttributes,
    const nsACString& aHost, const nsACString& aName, const nsACString& aPath) {
  CookieListIter iter{};

  if (!FindCookieIncludingExpired(aBaseDomain, aOriginAttributes, aHost, aName,
                                  aPath, iter)) {
    return nullptr;
  }

  RefPtr<Cookie> cookie = iter.Cookie();
  if (cookie->IsExpired()) {
    return nullptr;
  }

  return cookie.forget();
}

// find an exact cookie specified by host, name, and path, even if expired.
bool CookieStorage::FindCookieIncludingExpired(
    const nsACString& aBaseDomain, const OriginAttributes& aOriginAttributes,
    const nsACString& aHost, const nsACString& aName, const nsACString& aPath,
    CookieListIter& aIter) {
  CookieEntry* entry =
      mHostTable.GetEntry(CookieKey(aBaseDomain, aOriginAttributes));
  if (!entry) {
    return false;
  }

  const CookieEntry::ArrayType& cookies = entry->GetCookies();
  uint32_t targetHash = Cookie::ComputeKeyHash(aName, aHost, aPath);
  for (CookieEntry::IndexType i = 0; i < cookies.Length(); ++i) {
    Cookie* cookie = cookies[i];

    if (cookie->KeyHash() == targetHash && aHost.Equals(cookie->Host()) &&
        aPath.Equals(cookie->Path()) && aName.Equals(cookie->Name())) {
      aIter = CookieListIter(entry, i);
      return true;
    }
  }

  return false;
}

// find an secure cookie specified by host and name
bool CookieStorage::FindSecureCookie(const nsACString& aBaseDomain,
                                     const OriginAttributes& aOriginAttributes,
                                     Cookie* aCookie) {
  CookieEntry* entry =
      mHostTable.GetEntry(CookieKey(aBaseDomain, aOriginAttributes));
  if (!entry) {
    return false;
  }

  const CookieEntry::ArrayType& cookies = entry->GetCookies();
  for (CookieEntry::IndexType i = 0; i < cookies.Length(); ++i) {
    Cookie* cookie = cookies[i];
    // isn't a match if insecure or a different name
    if (!cookie->IsSecure() || !aCookie->Name().Equals(cookie->Name())) {
      continue;
    }

    // The host must "domain-match" an existing cookie or vice-versa
    if (CookieCommons::DomainMatches(cookie, aCookie->Host()) ||
        CookieCommons::DomainMatches(aCookie, cookie->Host())) {
      // If the path of new cookie and the path of existing cookie
      // aren't "/", then this situation needs to compare paths to
      // ensure only that a newly-created non-secure cookie does not
      // overlay an existing secure cookie.
      if (CookieCommons::PathMatches(cookie, aCookie->Path())) {
        return true;
      }
    }
  }

  return false;
}

bool CookieStorage::HasCookies(const nsACString& aBaseDomain,
                               const OriginAttributesPattern& aPattern) {
  int64_t currentTimeInMSec = PR_Now() / PR_USEC_PER_MSEC;

  for (auto iter = mHostTable.Iter(); !iter.Done(); iter.Next()) {
    CookieEntry* entry = iter.Get();

    if (!aBaseDomain.Equals(entry->mBaseDomain)) {
      continue;
    }

    if (!aPattern.Matches(entry->mOriginAttributes)) {
      continue;
    }

    for (Cookie* cookie : entry->GetCookies()) {
      if (!cookie->IsExpired(currentTimeInMSec)) {
        return true;
      }
    }
  }

  return false;
}

uint32_t CookieStorage::CountCookieBytesNotMatchingCookie(
    const Cookie& cookie, const nsACString& baseDomain) {
  CookieEntry* entry =
      mHostTable.GetEntry(CookieKey(baseDomain, cookie.OriginAttributesRef()));
  if (!entry) {
    return 0;
  }

  // count cookies with different name to the cookie being added
  uint32_t cookieBytes = 0;
  for (Cookie* c : entry->GetCookies()) {
    if (!cookie.Name().Equals(c->Name())) {
      cookieBytes += c->NameAndValueBytes();
    }
  }
  return cookieBytes;
}

void CookieStorage::GetAll(nsTArray<RefPtr<nsICookie>>& aResult,
                           ExpiredCookies aExpired) const {
  int64_t currentTimeInMSec = PR_Now() / PR_USEC_PER_MSEC;

  aResult.SetCapacity(mCookieCount);

  for (const auto& entry : mHostTable) {
    const CookieEntry::ArrayType& cookies = entry.GetCookies();
    for (CookieEntry::IndexType i = 0; i < cookies.Length(); ++i) {
      if (aExpired == ExpiredCookies::Exclude &&
          cookies[i]->IsExpired(currentTimeInMSec)) {
        continue;
      }

      aResult.AppendElement(cookies[i]);
    }
  }
}

void CookieStorage::GetCookiesFromHost(
    const nsACString& aBaseDomain, const OriginAttributes& aOriginAttributes,
    nsTArray<RefPtr<Cookie>>& aCookies) {
  CookieEntry* entry =
      mHostTable.GetEntry(CookieKey(aBaseDomain, aOriginAttributes));
  if (!entry) {
    return;
  }

  aCookies = entry->GetCookies().Clone();

  int64_t currentTimeInMSec = PR_Now() / PR_USEC_PER_MSEC;
  aCookies.RemoveElementsBy([currentTimeInMSec](const RefPtr<Cookie>& aCookie) {
    return aCookie->IsExpired(currentTimeInMSec);
  });
}

bool CookieStorage::HasCookies(const nsACString& aBaseDomain,
                               const OriginAttributes& aOriginAttributes) {
  CookieEntry* entry =
      mHostTable.GetEntry(CookieKey(aBaseDomain, aOriginAttributes));
  if (!entry) {
    return false;
  }

  int64_t currentTimeInMSec = PR_Now() / PR_USEC_PER_MSEC;

  for (Cookie* cookie : entry->GetCookies()) {
    if (!cookie->IsExpired(currentTimeInMSec)) {
      return true;
    }
  }

  return false;
}

void CookieStorage::GetCookiesWithOriginAttributes(
    const OriginAttributesPattern& aPattern, const nsACString& aBaseDomain,
    bool aSorted, nsTArray<RefPtr<nsICookie>>& aResult) {
  int64_t currentTimeInMSec = PR_Now() / PR_USEC_PER_MSEC;

  for (auto iter = mHostTable.Iter(); !iter.Done(); iter.Next()) {
    CookieEntry* entry = iter.Get();

    if (!aBaseDomain.IsEmpty() && !aBaseDomain.Equals(entry->mBaseDomain)) {
      continue;
    }

    if (!aPattern.Matches(entry->mOriginAttributes)) {
      continue;
    }

    const CookieEntry::ArrayType& entryCookies = entry->GetCookies();

    for (CookieEntry::IndexType i = 0; i < entryCookies.Length(); ++i) {
      if (entryCookies[i]->IsExpired(currentTimeInMSec)) {
        continue;
      }

      aResult.AppendElement(entryCookies[i]);
    }
  }

  if (aSorted) {
    aResult.Sort(CompareCookiesForSending());
  }
}

void CookieStorage::RemoveCookie(const nsACString& aBaseDomain,
                                 const OriginAttributes& aOriginAttributes,
                                 const nsACString& aHost,
                                 const nsACString& aName,
                                 const nsACString& aPath, bool aFromHttp,
                                 const nsID* aOperationID) {
  CookieListIter matchIter{};
  RefPtr<Cookie> cookie;
  if (FindCookieIncludingExpired(aBaseDomain, aOriginAttributes, aHost, aName,
                                 aPath, matchIter)) {
    cookie = matchIter.Cookie();

    // If the old cookie is httponly, make sure we're not coming from script.
    if (!aFromHttp && cookie->IsHttpOnly()) {
      return;
    }

    RemoveCookieFromList(matchIter);
  }

  if (cookie) {
    // Everything's done. Notify observers.
    NotifyChanged(cookie, nsICookieNotification::COOKIE_DELETED, aBaseDomain,
                  aOperationID);
  }
}

void CookieStorage::RemoveCookiesWithOriginAttributes(
    const OriginAttributesPattern& aPattern, const nsACString& aBaseDomain) {
  // Iterate the hash table of CookieEntry.
  for (auto iter = mHostTable.Iter(); !iter.Done(); iter.Next()) {
    CookieEntry* entry = iter.Get();

    if (!aBaseDomain.IsEmpty() && !aBaseDomain.Equals(entry->mBaseDomain)) {
      continue;
    }

    if (!aPattern.Matches(entry->mOriginAttributes)) {
      continue;
    }

    // Pattern matches. Delete all cookies within this CookieEntry.
    uint32_t cookiesCount = entry->GetCookies().Length();

    for (CookieEntry::IndexType i = 0; i < cookiesCount; ++i) {
      // Remove the first cookie from the list.
      CookieListIter iter(entry, 0);
      RefPtr<Cookie> cookie = iter.Cookie();

      // Remove the cookie.
      RemoveCookieFromList(iter);

      if (cookie) {
        NotifyChanged(cookie, nsICookieNotification::COOKIE_DELETED,
                      aBaseDomain);
      }
    }
  }
}

/* static */ bool CookieStorage::SerializeIPv6BaseDomain(
    nsACString& aBaseDomain) {
  bool hasStartBracket = aBaseDomain.First() == '[';
  bool hasEndBracket = aBaseDomain.Last() == ']';

  // If only start or end bracket exists host is malformed.
  if (hasStartBracket != hasEndBracket) {
    return false;
  }

  // If the base domain is not in URL format (e.g. [::1]) add brackets so we
  // can use rusturl_parse_ipv6addr().
  if (!hasStartBracket) {
    aBaseDomain.Insert('[', 0);
    aBaseDomain.Append(']');
  }

  // Serialize base domain to "zero abbreviation" and lower-case hex
  // representation.
  nsAutoCString baseDomain;
  nsresult rv = (nsresult)rusturl_parse_ipv6addr(&aBaseDomain, &baseDomain);
  NS_ENSURE_SUCCESS(rv, false);

  // Strip brackets to match principal representation.
  aBaseDomain = Substring(baseDomain, 1, baseDomain.Length() - 2);

  return true;
}

void CookieStorage::RemoveCookiesFromExactHost(
    const nsACString& aHost, const nsACString& aBaseDomain,
    const OriginAttributesPattern& aPattern) {
  // Intermediate fix until Bug 1882259 is resolved.
  // Bug 1860033 - Cookies do not serialize IPv6 host / base domain in contrast
  // to principals. To allow deletion by principal serialize before comparison.
  // We check the base domain since it is used as the CookieList key and equals
  // the normalized (ASCII) host for IP addresses
  // (it is equal to the CookieService::NormalizeHost() output).
  nsAutoCString removeBaseDomain;
  bool isIPv6 = CookieCommons::IsIPv6BaseDomain(aBaseDomain);
  if (isIPv6) {
    MOZ_ASSERT(!aBaseDomain.IsEmpty());
    // Copy base domain since argument is immutable.
    removeBaseDomain = aBaseDomain;
    if (NS_WARN_IF(!SerializeIPv6BaseDomain(removeBaseDomain))) {
      // Return on malformed base domains.
      return;
    }
  }

  // Iterate the hash table of CookieEntry.
  for (auto iter = mHostTable.Iter(); !iter.Done(); iter.Next()) {
    CookieEntry* entry = iter.Get();

    // IPv6 host / base domain cookies
    if (isIPv6) {
      // If we look for a IPv6 cookie skip non-IPv6 cookie entries.
      if (!CookieCommons::IsIPv6BaseDomain(entry->mBaseDomain)) {
        continue;
      }
      // Serialize IPv6 base domains before comparison.
      // Copy base domain since argument is immutable.
      nsAutoCString entryBaseDomain;
      entryBaseDomain = entry->mBaseDomain;
      if (NS_WARN_IF(!SerializeIPv6BaseDomain(entryBaseDomain))) {
        continue;
      }
      if (!removeBaseDomain.Equals(entryBaseDomain)) {
        continue;
      }
      // Non-IPv6 cookies
    } else if (!aBaseDomain.Equals(entry->mBaseDomain)) {
      continue;
    }

    if (!aPattern.Matches(entry->mOriginAttributes)) {
      continue;
    }

    uint32_t cookiesCount = entry->GetCookies().Length();
    for (CookieEntry::IndexType i = cookiesCount; i != 0; --i) {
      CookieListIter iter(entry, i - 1);
      RefPtr<Cookie> cookie = iter.Cookie();

      // For IP addresses (ASCII normalized) host == baseDomain, we checked
      // equality already.
      if (!isIPv6 && !aHost.Equals(cookie->RawHost())) {
        continue;
      }

      // Remove the cookie.
      RemoveCookieFromList(iter);

      if (cookie) {
        NotifyChanged(cookie, nsICookieNotification::COOKIE_DELETED,
                      aBaseDomain);
      }
    }
  }
}

void CookieStorage::RemoveAll() {
  // clearing the hashtable will call each CookieEntry's dtor,
  // which releases all their respective children.
  mHostTable.Clear();
  mCookieCount = 0;
  mCookieOldestTime = INT64_MAX;

  RemoveAllInternal();

  NotifyChanged(nullptr, nsICookieNotification::ALL_COOKIES_CLEARED, ""_ns);
}

// notify observers that the cookie list changed.
void CookieStorage::NotifyChanged(nsISupports* aSubject,
                                  nsICookieNotification::Action aAction,
                                  const nsACString& aBaseDomain,
                                  bool aIsThirdParty,
                                  dom::BrowsingContext* aBrowsingContext,
                                  bool aOldCookieIsSession,
                                  const nsID* aOperationID) {
  nsCOMPtr<nsIObserverService> os = services::GetObserverService();
  if (!os) {
    return;
  }

  nsCOMPtr<nsICookie> cookie;
  nsCOMPtr<nsIArray> batchDeletedCookies;

  if (aAction == nsICookieNotification::COOKIES_BATCH_DELETED) {
    batchDeletedCookies = do_QueryInterface(aSubject);
  } else {
    cookie = do_QueryInterface(aSubject);
  }

  uint64_t browsingContextId = 0;
  if (aBrowsingContext) {
    browsingContextId = aBrowsingContext->Id();
  }

  nsCOMPtr<nsICookieNotification> notification = new CookieNotification(
      aAction, cookie, aBaseDomain, aIsThirdParty, batchDeletedCookies,
      browsingContextId, aOperationID);
  // Notify for topic "private-cookie-changed" or "cookie-changed"
  os->NotifyObservers(notification, NotificationTopic(), u"");

  NotifyChangedInternal(notification, aOldCookieIsSession);
}

void CookieStorage::RemoveCookiesFromBack(
    nsTArray<CookieListIter>& aCookieIters, nsCOMPtr<nsIArray>& aPurgedList) {
  for (auto it = aCookieIters.rbegin(); it != aCookieIters.rend(); ++it) {
    RefPtr<Cookie> cookie = (*it).Cookie();
    MOZ_ASSERT(cookie);
    COOKIE_LOGEVICTED(cookie, "Too many cookie bytes for this partition");
    RemoveCookieFromList(*it);
    CreateOrUpdatePurgeList(aPurgedList, cookie);

    // if a sole cookie is ever removed, we would remove the entire entry
    // but practically speaking,
    // we should never be in a scenario when we remove the final cookie
    // unless a single cookie puts us over the limit
    // for that to occur a user would have to adjust the CHIPS limit to be < 4
    // KB
    MOZ_ASSERT((*it).entry);
  }
}

uint32_t CookieStorage::RemoveOldestCookies(CookieEntry* aEntry, bool aSecure,
                                            uint32_t aBytesToRemove,
                                            nsCOMPtr<nsIArray>& aPurgedList) {
  const CookieEntry::ArrayType& cookies = aEntry->GetCookies();
  using MaybePurgeList = nsTArray<CookieListIter>;

  // note that because the maybePurgeList is populated exclusively by
  // pre-existing cookie list, we will never remove the cookie that is currently
  // being added
  MaybePurgeList maybePurgeList(aEntry->GetCookies().Length());
  for (CookieEntry::IndexType i = 0; i < cookies.Length(); ++i) {
    CookieListIter iter(aEntry, i);
    // assumes that secure cookie removal will always happen after insecure
    // cookie removal
    // Ie. this function will will only be called with aSecure == true
    // after it has already been called with aSecure == false
    if (aSecure || !iter.Cookie()->IsSecure()) {
      maybePurgeList.AppendElement(iter);
    }
  }

  // sort by age to prep oldest cookies first
  // since the underlying cookie list doesn't guarantee age-order because age
  // is primarily determined by lastAccessed, not creation time
  // todo: write test to assert oldest first
  maybePurgeList.Sort(CompareCookiesByAge());

  // truncate the list if we don't need to remove all cookies
  uint32_t bytesRemoved = 0;
  uint32_t count = 0;
  for (auto iter : maybePurgeList) {
    bytesRemoved += iter.Cookie()->NameAndValueBytes();
    count++;
    if (bytesRemoved >= static_cast<uint32_t>(aBytesToRemove)) {
      maybePurgeList.SetLength(count);
      break;
    }
  }
  // sort for safe, orderly removal (by index)
  // because CookieIters effectively are just pointers to the underlying cookie
  // list we must remove them from the back (largest index first)
  maybePurgeList.Sort(CompareCookiesByIndex());
  RemoveCookiesFromBack(maybePurgeList, aPurgedList);
  return bytesRemoved;
}

void CookieStorage::RemoveOlderCookiesByBytes(CookieEntry* aEntry,
                                              uint32_t removeBytes,
                                              nsCOMPtr<nsIArray>& aPurgedList) {
  MOZ_ASSERT(aEntry);
  CookieKey key(aEntry->mBaseDomain, aEntry->mOriginAttributes);

  // remove insecure older cookies until we are within the byte limit
  // (CHIPS cookies will not be detected here since they must be secure)
  uint32_t bytesRemoved =
      RemoveOldestCookies(aEntry, false, removeBytes, aPurgedList);

  // remove secure cookies if we still have cookies to remove
  if (bytesRemoved <= removeBytes) {
    // Re-lookup: aEntry may have been freed if pass 1 emptied it.
    CookieEntry* entry = mHostTable.GetEntry(key);
    if (!entry) {
      return;
    }
    // remove secure older cookies until we are within the byte limit
    MOZ_LOG(gCookieLog, LogLevel::Debug,
            ("Still too many cookies for partition, purging secure\n"));
    uint32_t bytesStillToRemove = removeBytes - bytesRemoved;
    RemoveOldestCookies(entry, true, bytesStillToRemove, aPurgedList);
  }
}

CookieStorage::ChipsLimitExcess CookieStorage::PartitionLimitExceededBytes(
    Cookie* aCookie, const nsACString& aBaseDomain) {
  uint32_t newByteCount =
      CountCookieBytesNotMatchingCookie(*aCookie, aBaseDomain) +
      aCookie->NameAndValueBytes();
  ChipsLimitExcess res{.hard = 0, .soft = 0};
  uint32_t softLimit =
      StaticPrefs::network_cookie_chips_partitionLimitByteCapacity();
  // shouldn't expect more the 4000 cookies * 4000 bytes/cookie -> 16MB
  uint32_t hardLimit = static_cast<uint32_t>(softLimit * kChipsHardLimitFactor);
  if (newByteCount > hardLimit) {
    res.hard = newByteCount - hardLimit;
    res.soft = newByteCount - softLimit;
  }
  return res;
}

// this is a backend function for adding a cookie to the list, via SetCookie.
// also used in the cookie manager, for profile migration from IE.  it either
// replaces an existing cookie; or adds the cookie to the hashtable, and
// deletes a cookie (if maximum number of cookies has been reached). also
// performs list maintenance by removing expired cookies.
void CookieStorage::AddCookie(CookieParser* aCookieParser,
                              const nsACString& aBaseDomain,
                              const OriginAttributes& aOriginAttributes,
                              Cookie* aCookie, int64_t aCurrentTimeInUsec,
                              nsIURI* aHostURI, const nsACString& aCookieHeader,
                              bool aFromHttp, bool aIsThirdParty,
                              dom::BrowsingContext* aBrowsingContext,
                              const nsID* aOperationID) {
  if (CookieCommons::IsFirstPartyPartitionedCookieWithoutCHIPS(
          aCookie, aBaseDomain, aOriginAttributes)) {
    COOKIE_LOGFAILURE(SET_COOKIE, aHostURI, aCookieHeader,
                      "Invalid first-party partitioned cookie without "
                      "partitioned cookie attribution.");
    MOZ_ASSERT(false);
    return;
  }

  int64_t currentTimeInMSec = aCurrentTimeInUsec / PR_USEC_PER_MSEC;

  CookieListIter exactIter{};
  bool foundCookie = false;
  foundCookie = FindCookieIncludingExpired(aBaseDomain, aOriginAttributes,
                                           aCookie->Host(), aCookie->Name(),
                                           aCookie->Path(), exactIter);
  bool foundSecureExact = foundCookie && exactIter.Cookie()->IsSecure();
  bool potentiallyTrustworthy = true;
  if (aHostURI) {
    potentiallyTrustworthy =
        nsMixedContentBlocker::IsPotentiallyTrustworthyOrigin(aHostURI);
  }
  bool oldCookieIsSession = false;
  // Step1, call FindSecureCookie(). FindSecureCookie() would
  // find the existing cookie with the security flag and has
  // the same name, host and path of the new cookie, if there is any.
  // Step2, Confirm new cookie's security setting. If any targeted
  // cookie had been found in Step1, then confirm whether the
  // new cookie could modify it. If the new created cookie’s
  // "secure-only-flag" is not set, and the "scheme" component
  // of the "request-uri" does not denote a "secure" protocol,
  // then ignore the new cookie.
  // (draft-ietf-httpbis-cookie-alone section 3.2)
  if (!aCookie->IsSecure() &&
      (foundSecureExact ||
       FindSecureCookie(aBaseDomain, aOriginAttributes, aCookie)) &&
      !potentiallyTrustworthy) {
    COOKIE_LOGFAILURE(SET_COOKIE, aHostURI, aCookieHeader,
                      "cookie can't save because older cookie is secure "
                      "cookie but newer cookie is non-secure cookie");
    if (aCookieParser) {
      aCookieParser->RejectCookie(CookieParser::RejectedNonsecureOverSecure);
    }
    return;
  }

  RefPtr<Cookie> oldCookie;
  nsCOMPtr<nsIArray> purgedList;
  if (foundCookie) {
    oldCookie = exactIter.Cookie();
    oldCookieIsSession = oldCookie->IsSession();

    // Check if the old cookie is stale (i.e. has already expired). If so, we
    // need to be careful about the semantics of removing it and adding the new
    // cookie: we want the behavior wrt adding the new cookie to be the same as
    // if it didn't exist, but we still want to fire a removal notification.
    if (oldCookie->IsExpired(currentTimeInMSec)) {
      if (aCookie->IsExpired(currentTimeInMSec)) {
        // The new cookie has expired and the old one is stale. Nothing to do.
        COOKIE_LOGFAILURE(SET_COOKIE, aHostURI, aCookieHeader,
                          "cookie has already expired");
        return;
      }

      // Remove the stale cookie. We save notification for later, once all list
      // modifications are complete.
      RemoveCookieFromList(exactIter);
      COOKIE_LOGFAILURE(SET_COOKIE, aHostURI, aCookieHeader,
                        "stale cookie was purged");
      purgedList = CreatePurgeList(oldCookie);

      // We've done all we need to wrt removing and notifying the stale cookie.
      // From here on out, we pretend pretend it didn't exist, so that we
      // preserve expected notification semantics when adding the new cookie.
      foundCookie = false;

    } else {
      // If the old cookie is httponly, make sure we're not coming from script.
      if (!aFromHttp && oldCookie->IsHttpOnly()) {
        COOKIE_LOGFAILURE(
            SET_COOKIE, aHostURI, aCookieHeader,
            "previously stored cookie is httponly; coming from script");
        if (aCookieParser) {
          aCookieParser->RejectCookie(
              CookieParser::RejectedHttpOnlyButFromScript);
        }
        return;
      }

      // If the new cookie has the same value, expiry date, isSecure, isSession,
      // isHttpOnly and SameSite flags then we can just keep the old one.
      // Only if any of these differ we would want to override the cookie.
      if (oldCookie->Value().Equals(aCookie->Value()) &&
          oldCookie->ExpiryInMSec() == aCookie->ExpiryInMSec() &&
          oldCookie->IsSecure() == aCookie->IsSecure() &&
          oldCookie->IsSession() == aCookie->IsSession() &&
          oldCookie->IsHttpOnly() == aCookie->IsHttpOnly() &&
          oldCookie->SameSite() == aCookie->SameSite() &&
          oldCookie->SchemeMap() == aCookie->SchemeMap() &&
          // We don't want to perform this optimization if the cookie is
          // considered stale, since in this case we would need to update the
          // database.
          !oldCookie->IsStale()) {
        // Update the last access time on the old cookie.
        oldCookie->SetLastAccessedInUSec(aCookie->LastAccessedInUSec());
        UpdateCookieOldestTime(oldCookie);
        return;
      }

      // Merge the scheme map in case the old cookie and the new cookie are
      // used with different schemes.
      MergeCookieSchemeMap(oldCookie, aCookie);

      // Remove the old cookie.
      RemoveCookieFromList(exactIter);

      // If the new cookie has expired -- i.e. the intent was simply to delete
      // the old cookie -- then we're done.
      if (aCookie->IsExpired(currentTimeInMSec)) {
        COOKIE_LOGFAILURE(SET_COOKIE, aHostURI, aCookieHeader,
                          "previously stored cookie was deleted");
        NotifyChanged(oldCookie, nsICookieNotification::COOKIE_DELETED,
                      aBaseDomain, false, aBrowsingContext, oldCookieIsSession,
                      aOperationID);
        return;
      }

      // Preserve creation time of cookie for ordering purposes.
      aCookie->SetCreationTimeInUSec(oldCookie->CreationTimeInUSec());
    }

    // check for CHIPS-partitioned exceeding byte limit
    // when we overwrite a cookie with a cookie)
    if (CookieCommons::ChipsLimitEnabledAndChipsCookie(*aCookie,
                                                       aBrowsingContext)) {
      CookieEntry* entry =
          mHostTable.GetEntry(CookieKey(aBaseDomain, aOriginAttributes));
      if (entry) {
        ChipsLimitExcess exceededBytes =
            PartitionLimitExceededBytes(aCookie, aBaseDomain);
        if (exceededBytes.hard > 0) {
          MOZ_LOG(gCookieLog, LogLevel::Debug,
                  ("Partition byte limit exceeded on cookie overwrite\n"));
          if (!StaticPrefs::network_cookie_chips_partitionLimitDryRun()) {
            RemoveOlderCookiesByBytes(entry, exceededBytes.soft, purgedList);
          }
          if (StaticPrefs::network_cookie_chips_partitionLimitByteCapacity() ==
              kChipsPartitionByteCapacityDefault) {
            mozilla::glean::networking::cookie_chips_partition_limit_overflow
                .AccumulateSingleSample(exceededBytes.hard);
          }
        }
      }
    }
  } else {
    // check if cookie has already expired
    if (aCookie->IsExpired(currentTimeInMSec)) {
      COOKIE_LOGFAILURE(SET_COOKIE, aHostURI, aCookieHeader,
                        "cookie has already expired");
      return;
    }

    // check if we have to delete an old cookie.
    CookieEntry* entry =
        mHostTable.GetEntry(CookieKey(aBaseDomain, aOriginAttributes));
    ChipsLimitExcess partitionLimitExceededBytes{};
    // we haven't yet added the new cookie so we compare cookie list with >=
    if (entry && entry->GetCookies().Length() >= mMaxCookiesPerHost) {
      nsTArray<CookieListIter> removedIterList;
      // +1 to account for the cookie that we are adding,
      // to ensure that we end up with mCookieQuotaPerHost cookies.
      // "excess" should only be > 1 when prefs have been manipulated
      uint32_t excess = entry->GetCookies().Length() - mMaxCookiesPerHost + 1;
      uint32_t limit = mMaxCookiesPerHost - mCookieQuotaPerHost + excess;
      // Prioritize evicting insecure cookies.
      // (draft-ietf-httpbis-cookie-alone section 3.3)
      FindStaleCookies(entry, currentTimeInMSec, false, removedIterList, limit);
      if (removedIterList.Length() == 0) {
        if (aCookie->IsSecure()) {
          // It's valid to evict a secure cookie for another secure cookie.
          FindStaleCookies(entry, currentTimeInMSec, true, removedIterList,
                           limit);
        } else {
          COOKIE_LOGEVICTED(aCookie,
                            "Too many cookies for this domain and the new "
                            "cookie is not a secure cookie");
          return;
        }
      }

      MOZ_ASSERT(!removedIterList.IsEmpty());
      // Sort |removedIterList| by index again, since we have to remove the
      // cookie in the reverse order.
      removedIterList.Sort(CompareCookiesByIndex());
      for (auto it = removedIterList.rbegin(); it != removedIterList.rend();
           it++) {
        RefPtr<Cookie> evictedCookie = (*it).Cookie();
        COOKIE_LOGEVICTED(evictedCookie, "Too many cookies for this domain");
        RemoveCookieFromList(*it);
        CreateOrUpdatePurgeList(purgedList, evictedCookie);
        MOZ_ASSERT((*it).entry);
      }
      uint32_t purgedLength = 0;
      purgedList->GetLength(&purgedLength);
      mozilla::glean::networking::cookie_purge_entry_max.AccumulateSingleSample(
          purgedLength);

    } else if (CookieCommons::ChipsLimitEnabledAndChipsCookie(
                   *aCookie, aBrowsingContext) &&
               entry &&
               (partitionLimitExceededBytes =
                    PartitionLimitExceededBytes(aCookie, aBaseDomain))
                       .hard > 0) {
      MOZ_LOG(gCookieLog, LogLevel::Debug,
              ("Partition byte limit exceeded on cookie add\n"));

      if (!StaticPrefs::network_cookie_chips_partitionLimitDryRun()) {
        RemoveOlderCookiesByBytes(entry, partitionLimitExceededBytes.soft,
                                  purgedList);
      }
      if (StaticPrefs::network_cookie_chips_partitionLimitByteCapacity() ==
          kChipsPartitionByteCapacityDefault) {
        mozilla::glean::networking::cookie_chips_partition_limit_overflow
            .AccumulateSingleSample(partitionLimitExceededBytes.hard);
      }
    } else if (mCookieCount >= ADD_TEN_PERCENT(mMaxNumberOfCookies)) {
      int64_t maxAge = aCurrentTimeInUsec - mCookieOldestTime;
      int64_t purgeAge = ADD_TEN_PERCENT(mCookiePurgeAge);
      if (maxAge >= purgeAge) {
        // we're over both size and age limits by 10%; time to purge the table!
        // do this by:
        // 1) removing expired cookies;
        // 2) evicting the balance of old cookies until we reach the size limit.
        // note that the mCookieOldestTime indicator can be pessimistic - if
        // it's older than the actual oldest cookie, we'll just purge more
        // eagerly.
        purgedList = PurgeCookies(aCurrentTimeInUsec, mMaxNumberOfCookies,
                                  mCookiePurgeAge);
        uint32_t purgedLength = 0;
        if (purgedList) {
          purgedList->GetLength(&purgedLength);
        }
        mozilla::glean::networking::cookie_purge_max.AccumulateSingleSample(
            purgedLength);
      }
    }
  }

  // Add the cookie to the db. We do not supply a params array for batching
  // because this might result in removals and additions being out of order.
  AddCookieToList(aBaseDomain, aOriginAttributes, aCookie);
  StoreCookie(aBaseDomain, aOriginAttributes, aCookie);

  COOKIE_LOGSUCCESS(SET_COOKIE, aHostURI, aCookieHeader, aCookie, foundCookie);

  // Now that list mutations are complete, notify observers. We do it here
  // because observers may themselves attempt to mutate the list.
  if (purgedList) {
    NotifyChanged(purgedList, nsICookieNotification::COOKIES_BATCH_DELETED,
                  ""_ns, false, nullptr, false, aOperationID);
  }

  // Notify for topic "private-cookie-changed" or "cookie-changed"
  NotifyChanged(aCookie,
                foundCookie ? nsICookieNotification::COOKIE_CHANGED
                            : nsICookieNotification::COOKIE_ADDED,
                aBaseDomain, aIsThirdParty, aBrowsingContext,
                oldCookieIsSession, aOperationID);
}

void CookieStorage::UpdateCookieOldestTime(Cookie* aCookie) {
  if (aCookie->LastAccessedInUSec() < mCookieOldestTime) {
    mCookieOldestTime = aCookie->LastAccessedInUSec();
  }
}

void CookieStorage::MergeCookieSchemeMap(Cookie* aOldCookie,
                                         Cookie* aNewCookie) {
  aNewCookie->SetSchemeMap(aOldCookie->SchemeMap() | aNewCookie->SchemeMap());
}

void CookieStorage::AddCookieToList(const nsACString& aBaseDomain,
                                    const OriginAttributes& aOriginAttributes,
                                    Cookie* aCookie) {
  if (!aCookie) {
    NS_WARNING("Attempting to AddCookieToList with null cookie");
    return;
  }

  CookieKey key(aBaseDomain, aOriginAttributes);

  CookieEntry* entry = mHostTable.PutEntry(key);
  NS_ASSERTION(entry, "can't insert element into a null entry!");

  entry->GetCookies().AppendElement(aCookie);
  ++mCookieCount;

  // keep track of the oldest cookie, for when it comes time to purge
  UpdateCookieOldestTime(aCookie);
}

// static
already_AddRefed<nsIArray> CookieStorage::CreatePurgeList(nsICookie* aCookie) {
  nsCOMPtr<nsIMutableArray> removedList =
      do_CreateInstance(NS_ARRAY_CONTRACTID);
  removedList->AppendElement(aCookie);
  return removedList.forget();
}

// Given the output iter array and the count limit, find cookies
// sort by expiry and lastAccessedInUSec time.
// static
void CookieStorage::FindStaleCookies(CookieEntry* aEntry,
                                     int64_t aCurrentTimeInMSec, bool aIsSecure,
                                     nsTArray<CookieListIter>& aOutput,
                                     uint32_t aLimit) {
  MOZ_ASSERT(aLimit);

  const CookieEntry::ArrayType& cookies = aEntry->GetCookies();
  aOutput.Clear();

  CookieIterComparator comp(aCurrentTimeInMSec);
  nsTPriorityQueue<CookieListIter, CookieIterComparator> queue(comp);

  for (CookieEntry::IndexType i = 0; i < cookies.Length(); ++i) {
    Cookie* cookie = cookies[i];

    if (cookie->IsExpired(aCurrentTimeInMSec)) {
      queue.Push(CookieListIter(aEntry, i));
      continue;
    }

    if (!aIsSecure) {
      // We want to look for the non-secure cookie first time through,
      // then find the secure cookie the second time this function is called.
      if (cookie->IsSecure()) {
        continue;
      }
    }

    queue.Push(CookieListIter(aEntry, i));
  }

  uint32_t count = 0;
  while (!queue.IsEmpty() && count < aLimit) {
    aOutput.AppendElement(queue.Pop());
    count++;
  }
}

// static
void CookieStorage::CreateOrUpdatePurgeList(nsCOMPtr<nsIArray>& aPurgedList,
                                            nsICookie* aCookie) {
  if (!aPurgedList) {
    COOKIE_LOGSTRING(LogLevel::Debug, ("Creating new purge list"));
    aPurgedList = CreatePurgeList(aCookie);
    return;
  }

  nsCOMPtr<nsIMutableArray> purgedList = do_QueryInterface(aPurgedList);
  if (purgedList) {
    COOKIE_LOGSTRING(LogLevel::Debug, ("Updating existing purge list"));
    purgedList->AppendElement(aCookie);
  } else {
    COOKIE_LOGSTRING(LogLevel::Debug, ("Could not QI aPurgedList!"));
  }
}

void CookieStorage::PurgeExpiredCookies() {
  if (!mCookieCount) {
    return;
  }

  // Passing the largest possible cookie limit keeps the eviction pass of
  // PurgeCookies() a no-op, so only expired cookies are dropped here.
  nsCOMPtr<nsIArray> purgedList = PurgeCookies(
      PR_Now(), std::numeric_limits<uint16_t>::max(), mCookiePurgeAge);
  if (!purgedList) {
    return;
  }

  uint32_t purgedLength = 0;
  purgedList->GetLength(&purgedLength);
  if (!purgedLength) {
    return;
  }

  NotifyChanged(purgedList, nsICookieNotification::COOKIES_BATCH_DELETED,
                ""_ns);
}

// purges expired and old cookies in a batch operation.
already_AddRefed<nsIArray> CookieStorage::PurgeCookiesWithCallbacks(
    int64_t aCurrentTimeInUsec, uint16_t aMaxNumberOfCookies,
    int64_t aCookiePurgeAge,
    std::function<void(const CookieListIter&)>&& aRemoveCookieCallback,
    std::function<void()>&& aFinalizeCallback) {
  NS_ASSERTION(mHostTable.Count() > 0, "table is empty");

  uint32_t initialCookieCount = mCookieCount;
  COOKIE_LOGSTRING(LogLevel::Debug,
                   ("PurgeCookies(): beginning purge with %" PRIu32
                    " cookies and %" PRId64 " oldest age",
                    mCookieCount, aCurrentTimeInUsec - mCookieOldestTime));

  using PurgeList = nsTArray<CookieListIter>;
  PurgeList purgeList(kMaxNumberOfCookies);

  nsCOMPtr<nsIMutableArray> removedList =
      do_CreateInstance(NS_ARRAY_CONTRACTID);

  int64_t currentTimeInMSec = aCurrentTimeInUsec / PR_USEC_PER_MSEC;
  int64_t purgeTime = aCurrentTimeInUsec - aCookiePurgeAge;
  int64_t oldestTime = INT64_MAX;

  for (auto iter = mHostTable.Iter(); !iter.Done(); iter.Next()) {
    CookieEntry* entry = iter.Get();

    const CookieEntry::ArrayType& cookies = entry->GetCookies();
    auto length = cookies.Length();
    for (CookieEntry::IndexType i = 0; i < length;) {
      CookieListIter iter(entry, i);
      Cookie* cookie = cookies[i];

      // check if the cookie has expired
      if (cookie->IsExpired(currentTimeInMSec)) {
        removedList->AppendElement(cookie);
        COOKIE_LOGEVICTED(cookie, "Cookie expired");

        // remove from list; do not increment our iterator, but stop if we're
        // done already.
        aRemoveCookieCallback(iter);
        if (i == --length) {
          break;
        }
      } else {
        // check if the cookie is over the age limit
        if (cookie->LastAccessedInUSec() <= purgeTime) {
          purgeList.AppendElement(iter);

        } else if (cookie->LastAccessedInUSec() < oldestTime) {
          // reset our indicator
          oldestTime = cookie->LastAccessedInUSec();
        }

        ++i;
      }
      MOZ_ASSERT(length == cookies.Length());
    }
  }

  uint32_t postExpiryCookieCount = mCookieCount;

  // now we have a list of iterators for cookies over the age limit.
  // sort them by age, and then we'll see how many to remove...
  purgeList.Sort(CompareCookiesByAge());

  // only remove old cookies until we reach the max cookie limit, no more.
  uint32_t excess = mCookieCount > aMaxNumberOfCookies
                        ? mCookieCount - aMaxNumberOfCookies
                        : 0;
  if (purgeList.Length() > excess) {
    // We're not purging everything in the list, so update our indicator.
    oldestTime = purgeList[excess].Cookie()->LastAccessedInUSec();

    purgeList.SetLength(excess);
  }

  // sort the list again, this time grouping cookies with a common entryclass
  // together, and with ascending index. this allows us to iterate backwards
  // over the list removing cookies, without having to adjust indexes as we go.
  purgeList.Sort(CompareCookiesByIndex());
  for (PurgeList::index_type i = purgeList.Length(); i--;) {
    Cookie* cookie = purgeList[i].Cookie();
    removedList->AppendElement(cookie);
    COOKIE_LOGEVICTED(cookie, "Cookie too old");

    aRemoveCookieCallback(purgeList[i]);
  }

  // Update the database if we have entries to purge.
  if (aFinalizeCallback) {
    aFinalizeCallback();
  }

  // reset the oldest time indicator
  mCookieOldestTime = oldestTime;

  COOKIE_LOGSTRING(LogLevel::Debug,
                   ("PurgeCookies(): %" PRIu32 " expired; %" PRIu32
                    " purged; %" PRIu32 " remain; %" PRId64 " oldest age",
                    initialCookieCount - postExpiryCookieCount,
                    postExpiryCookieCount - mCookieCount, mCookieCount,
                    aCurrentTimeInUsec - mCookieOldestTime));

  return removedList.forget();
}

// remove a cookie from the hashtable, and update the iterator state.
void CookieStorage::RemoveCookieFromList(const CookieListIter& aIter) {
  RemoveCookieFromDB(aIter.Cookie());
  RemoveCookieFromListInternal(aIter);
}

void CookieStorage::RemoveCookieFromListInternal(const CookieListIter& aIter) {
  if (aIter.entry->GetCookies().Length() == 1) {
    // we're removing the last element in the array - so just remove the entry
    // from the hash. note that the entryclass' dtor will take care of
    // releasing this last element for us!
    mHostTable.RawRemoveEntry(aIter.entry);

  } else {
    // just remove the element from the list
    aIter.entry->GetCookies().RemoveElementAt(aIter.index);
  }

  --mCookieCount;
}

void CookieStorage::PrefChanged(nsIPrefBranch* aPrefBranch) {
  int32_t val;
  if (NS_SUCCEEDED(aPrefBranch->GetIntPref(kPrefMaxNumberOfCookies, &val))) {
    mMaxNumberOfCookies =
        static_cast<uint16_t> LIMIT(val, 1, 0xFFFF, kMaxNumberOfCookies);
  }

  if (NS_SUCCEEDED(aPrefBranch->GetIntPref(kPrefCookieQuotaPerHost, &val))) {
    mCookieQuotaPerHost = static_cast<uint16_t> LIMIT(
        val, 1, mMaxCookiesPerHost, kCookieQuotaPerHost);
  }

  if (NS_SUCCEEDED(aPrefBranch->GetIntPref(kPrefMaxCookiesPerHost, &val))) {
    mMaxCookiesPerHost = static_cast<uint16_t> LIMIT(
        val, mCookieQuotaPerHost, 0xFFFF, kMaxCookiesPerHost);
  }

  if (NS_SUCCEEDED(aPrefBranch->GetIntPref(kPrefCookiePurgeAge, &val))) {
    mCookiePurgeAge =
        int64_t(LIMIT(val, 0, INT32_MAX, INT32_MAX)) * PR_USEC_PER_SEC;
  }
}

NS_IMETHODIMP
CookieStorage::Observe(nsISupports* aSubject, const char* aTopic,
                       const char16_t* /*aData*/) {
  if (!strcmp(aTopic, NS_PREFBRANCH_PREFCHANGE_TOPIC_ID)) {
    nsCOMPtr<nsIPrefBranch> prefBranch = do_QueryInterface(aSubject);
    if (prefBranch) {
      PrefChanged(prefBranch);
    }
  } else if (!strcmp(aTopic, OBSERVER_TOPIC_IDLE_DAILY)) {
    CollectCookieJarSizeData();
    PurgeExpiredCookies();
  }

  return NS_OK;
}

}  // namespace net
}  // namespace mozilla
