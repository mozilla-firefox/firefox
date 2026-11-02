/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_CookieStorage_h
#define mozilla_net_CookieStorage_h

#include <functional>

#include "CookieCommons.h"
#include "CookieKey.h"
#include "mozilla/net/Cookie.h"
#include "nsICookieNotification.h"
#include "nsIObserver.h"
#include "nsTHashtable.h"
#include "nsWeakReference.h"

class nsIArray;
class nsICookie;
class nsIPrefBranch;

namespace mozilla {
namespace net {

class CookieParser;

// Inherit from CookieKey so this can be stored in nsTHashTable
// TODO: why aren't we using nsClassHashTable<CookieKey, ArrayType>?
class CookieEntry : public CookieKey {
 public:
  // Hash methods
  using ArrayType = nsTArray<RefPtr<Cookie>>;
  using IndexType = ArrayType::index_type;

  explicit CookieEntry(KeyTypePointer aKey) : CookieKey(aKey) {}

  CookieEntry(const CookieEntry& toCopy) {
    // if we end up here, things will break. nsTHashtable shouldn't
    // allow this, since we set ALLOW_MEMMOVE to true.
    MOZ_ASSERT_UNREACHABLE("CookieEntry copy constructor is forbidden!");
  }

  ~CookieEntry() = default;

  inline ArrayType& GetCookies() { return mCookies; }
  inline const ArrayType& GetCookies() const { return mCookies; }

  size_t SizeOfExcludingThis(MallocSizeOf aMallocSizeOf) const;

  bool IsPartitioned() const;

 private:
  ArrayType mCookies;
};

class CookieStorage : public nsIObserver, public nsSupportsWeakReference {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIOBSERVER

  size_t SizeOfIncludingThis(MallocSizeOf aMallocSizeOf) const;

  enum class ExpiredCookies { Exclude, Include };

  void GetSessionCookies(nsTArray<RefPtr<nsICookie>>& aCookies) const;

  already_AddRefed<Cookie> FindCookie(const nsACString& aBaseDomain,
                                      const OriginAttributes& aOriginAttributes,
                                      const nsACString& aHost,
                                      const nsACString& aName,
                                      const nsACString& aPath);

  uint32_t CountCookieBytesNotMatchingCookie(const Cookie& cookie,
                                             const nsACString& baseDomain);

  void GetAll(nsTArray<RefPtr<nsICookie>>& aResult,
              ExpiredCookies aExpired = ExpiredCookies::Exclude) const;

  void GetCookiesFromHost(const nsACString& aBaseDomain,
                          const OriginAttributes& aOriginAttributes,
                          nsTArray<RefPtr<Cookie>>& aCookies);

  bool HasCookies(const nsACString& aBaseDomain,
                  const OriginAttributes& aOriginAttributes);

  bool HasCookies(const nsACString& aBaseDomain,
                  const OriginAttributesPattern& aPattern);

  void GetCookiesWithOriginAttributes(const OriginAttributesPattern& aPattern,
                                      const nsACString& aBaseDomain,
                                      bool aSorted,
                                      nsTArray<RefPtr<nsICookie>>& aResult);

  void RemoveCookie(const nsACString& aBaseDomain,
                    const OriginAttributes& aOriginAttributes,
                    const nsACString& aHost, const nsACString& aName,
                    const nsACString& aPath, bool aFromHttp,
                    const nsID* aOperationID);

  virtual void RemoveCookiesWithOriginAttributes(
      const OriginAttributesPattern& aPattern, const nsACString& aBaseDomain);

  virtual void RemoveCookiesFromExactHost(
      const nsACString& aHost, const nsACString& aBaseDomain,
      const OriginAttributesPattern& aPattern);

  void RemoveAll();

  void NotifyChanged(nsISupports* aSubject,
                     nsICookieNotification::Action aAction,
                     const nsACString& aBaseDomain, bool aIsThirdParty = false,
                     dom::BrowsingContext* aBrowsingContext = nullptr,
                     bool aOldCookieIsSession = false,
                     const nsID* aOperationID = nullptr);

  void AddCookie(CookieParser* aCookieParser, const nsACString& aBaseDomain,
                 const OriginAttributes& aOriginAttributes, Cookie* aCookie,
                 int64_t aCurrentTimeInUsec, nsIURI* aHostURI,
                 const nsACString& aCookieHeader, bool aFromHttp,
                 bool aIsThirdParty, dom::BrowsingContext* aBrowsingContext,
                 const nsID* aOperationID = nullptr);

  uint32_t RemoveOldestCookies(CookieEntry* aEntry, bool aSecure,
                               uint32_t aBytesToRemove,
                               nsCOMPtr<nsIArray>& aPurgedList);

  void RemoveOlderCookiesByBytes(CookieEntry* aEntry, uint32_t removeBytes,
                                 nsCOMPtr<nsIArray>& aPurgedList);

  // tracks how far over the hard and soft CHIPS limits
  // we use the hard and soft limit to prevent excessive purging.
  // the soft limit (aka quota) is derived directly from partitionLimitCapacity
  // pref while the hard limit is the softLimit * a factor
  // (kChipsHardLimitFactor). the hard limit is used to trigger purging and when
  // we do purge, we purge down to the soft limit (quota)
  struct ChipsLimitExcess {
    uint32_t hard;
    uint32_t soft;  // aka quota
  };

  ChipsLimitExcess PartitionLimitExceededBytes(Cookie* aCookie,
                                               const nsACString& aBaseDomain);

  static void CreateOrUpdatePurgeList(nsCOMPtr<nsIArray>& aPurgedList,
                                      nsICookie* aCookie);

  virtual void StaleCookies(const nsTArray<RefPtr<Cookie>>& aCookieList,
                            int64_t aCurrentTimeInUsec) = 0;

  virtual void Close() = 0;

  virtual void EnsureInitialized() = 0;

 protected:
  // stores the CookieEntry entryclass and an index into the cookie array within
  // that entryclass, for purposes of storing an iteration state that points to
  // a certain cookie.
  struct MOZ_STACK_CLASS CookieListIter {
    // default (non-initializing) constructor.
    CookieListIter() = default;

    // explicit constructor to a given iterator state with entryclass 'aEntry'
    // and index 'aIndex'.
    explicit CookieListIter(CookieEntry* aEntry, CookieEntry::IndexType aIndex)
        : entry(aEntry), index(aIndex) {}

    // get the Cookie * the iterator currently points to.
    mozilla::net::Cookie* Cookie() const { return entry->GetCookies()[index]; }

    CookieEntry* entry;
    CookieEntry::IndexType index;
  };

  // comparator class for lastaccessed times of cookies.
  class CompareCookiesByAge;

  // Cookie comparator for the priority queue used in FindStaleCookies.
  // Note that the expired cookie has the highest priority.
  // Other non-expired cookies are sorted by their age.
  class CookieIterComparator;

  // comparator class for sorting cookies by entry and index.
  class CompareCookiesByIndex;

  CookieStorage() = default;
  virtual ~CookieStorage() = default;

  void Init();

  bool FindCookieIncludingExpired(const nsACString& aBaseDomain,
                                  const OriginAttributes& aOriginAttributes,
                                  const nsACString& aHost,
                                  const nsACString& aName,
                                  const nsACString& aPath,
                                  CookieListIter& aIter);

  void AddCookieToList(const nsACString& aBaseDomain,
                       const OriginAttributes& aOriginAttributes,
                       Cookie* aCookie);

  virtual void StoreCookie(const nsACString& aBaseDomain,
                           const OriginAttributes& aOriginAttributes,
                           Cookie* aCookie) = 0;

  virtual const char* NotificationTopic() const = 0;

  virtual void NotifyChangedInternal(nsICookieNotification* aSubject,
                                     bool aOldCookieIsSession) = 0;

  virtual void RemoveAllInternal() = 0;

  // This method calls RemoveCookieFromDB + RemoveCookieFromListInternal.
  void RemoveCookieFromList(const CookieListIter& aIter);

  void RemoveCookieFromListInternal(const CookieListIter& aIter);

  virtual void RemoveCookieFromDB(Cookie* aCookie) = 0;

  already_AddRefed<nsIArray> PurgeCookiesWithCallbacks(
      int64_t aCurrentTimeInUsec, uint16_t aMaxNumberOfCookies,
      int64_t aCookiePurgeAge,
      std::function<void(const CookieListIter&)>&& aRemoveCookieCallback,
      std::function<void()>&& aFinalizeCallback);

  nsTHashtable<CookieEntry> mHostTable;

  uint32_t mCookieCount{0};

 private:
  void PrefChanged(nsIPrefBranch* aPrefBranch);

  bool FindSecureCookie(const nsACString& aBaseDomain,
                        const OriginAttributes& aOriginAttributes,
                        Cookie* aCookie);

  static void FindStaleCookies(CookieEntry* aEntry, int64_t aCurrentTimeInMSec,
                               bool aIsSecure,
                               nsTArray<CookieListIter>& aOutput,
                               uint32_t aLimit);

  void UpdateCookieOldestTime(Cookie* aCookie);

  void MergeCookieSchemeMap(Cookie* aOldCookie, Cookie* aNewCookie);

  static already_AddRefed<nsIArray> CreatePurgeList(nsICookie* aCookie);

  virtual already_AddRefed<nsIArray> PurgeCookies(int64_t aCurrentTimeInUsec,
                                                  uint16_t aMaxNumberOfCookies,
                                                  int64_t aCookiePurgeAge) = 0;

  void PurgeExpiredCookies();

  void RemoveCookiesFromBack(nsTArray<CookieListIter>& aCookieIters,
                             nsCOMPtr<nsIArray>& aPurgedList);

  // Serialize aBaseDomain e.g. apply "zero abbreveation" (::), use single
  // zeros and remove brackets to match principal base domain representation.
  static bool SerializeIPv6BaseDomain(nsACString& aBaseDomain);

  virtual void CollectCookieJarSizeData() = 0;

  int64_t mCookieOldestTime{INT64_MAX};

  uint16_t mMaxNumberOfCookies{kMaxNumberOfCookies};
  uint16_t mMaxCookiesPerHost{kMaxCookiesPerHost};
  uint16_t mCookieQuotaPerHost{kCookieQuotaPerHost};
  int64_t mCookiePurgeAge{kCookiePurgeAge};
};

}  // namespace net
}  // namespace mozilla

#endif  // mozilla_net_CookieStorage_h
