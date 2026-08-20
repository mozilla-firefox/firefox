/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef EffectiveTLDService_h
#define EffectiveTLDService_h

#include "MainThreadUtils.h"
#include "mozilla/Dafsa.h"
#include "mozilla/MruCache.h"
#include "mozilla/StaticPtr.h"
#include "nsHashKeys.h"
#include "nsIEffectiveTLDService.h"
#include "nsString.h"

class nsIIDNService;

class nsEffectiveTLDService final : public nsIEffectiveTLDService {
 public:
  // The `nsEffectiveTLDService` is allocated in a static singleton.
  // We'll use a fake reference count for the type.
  NS_IMETHOD_(MozExternalRefCountType) AddRef() override { return 2; }
  NS_IMETHOD_(MozExternalRefCountType) Release() override { return 1; }
  NS_IMETHOD QueryInterface(REFNSIID aIID, void** aInstancePtr) override;
  using HasThreadSafeRefCnt = std::true_type;

  NS_DECL_NSIEFFECTIVETLDSERVICE

  static already_AddRefed<nsIEffectiveTLDService> GetXPCOMSingleton();

 private:
  constexpr explicit nsEffectiveTLDService(mozilla::Dafsa::Graph aGraph)
      : mGraph(aGraph) {}

  nsresult GetBaseDomainInternal(nsCString& aHostname, int32_t aAdditionalParts,
                                 bool aOnlyKnownPublicSuffix,
                                 nsACString& aBaseDomain);

  static nsEffectiveTLDService sSingleton;

  // The DAFSA provides a compact encoding of the rather large eTLD list.
  const mozilla::Dafsa mGraph;

  // Note that the cache entries here can record entries that were cached
  // successfully or unsuccessfully.  mResult must be checked before using an
  // entry.  If it's a success error code, the cache entry is valid and can be
  // used.
  struct TLDCacheEntry {
    nsCString mHost;
    nsCString mBaseDomain;
    nsresult mResult;
  };

  // We use a small most recently used cache to compensate for DAFSA lookups
  // being slightly slower than a binary search on a larger table of strings.
  //
  // We first check the cache for a matching result and avoid a DAFSA lookup
  // if a match is found. Otherwise we lookup the domain in the DAFSA and then
  // cache the result. During standard browsing the same domains are repeatedly
  // fed into |GetBaseDomainInternal| so this ends up being an effective
  // mitigation getting about a 99% hit rate with four tabs open.
  struct TldCache
      : public mozilla::MruCache<nsACString, TLDCacheEntry, TldCache> {
    static mozilla::HashNumber Hash(const nsACString& aKey) {
      return mozilla::HashString(aKey);
    }
    static bool Match(const nsACString& aKey, const TLDCacheEntry& aVal) {
      return aKey == aVal.mHost;
    }
  };

  // Only accessed on the main thread.
  mozilla::StaticAutoPtr<TldCache> mMruTable
      MOZ_GUARDED_BY(mozilla::sMainThreadCapability);
};

#endif  // EffectiveTLDService_h
