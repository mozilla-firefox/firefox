/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_SharedStyleSheetCache_h_
#define mozilla_SharedStyleSheetCache_h_

// The shared style sheet cache is a cache that allows us to share sheets across
// documents.

#include "mozilla/MemoryReporting.h"
#include "mozilla/SharedSubResourceCache.h"
#include "mozilla/StyleSheet.h"
#include "mozilla/css/Loader.h"
#include "nsIMemoryReporter.h"
#include "nsIObserver.h"

namespace mozilla {

struct SharedStyleSheetCacheTraits {
  using Loader = css::Loader;
  using Key = SheetLoadDataHashKey;
  using Value = StyleSheet;
  using LoadingValue = css::SheetLoadData;

  static SheetLoadDataHashKey KeyFromLoadingValue(const LoadingValue& aValue) {
    return SheetLoadDataHashKey(aValue);
  }
};

class SharedStyleSheetCache final
    : public SharedSubResourceCache<SharedStyleSheetCacheTraits,
                                    SharedStyleSheetCache>,
      public nsIMemoryReporter,
      public nsIObserver {
 public:
  using Base = SharedSubResourceCache<SharedStyleSheetCacheTraits,
                                      SharedStyleSheetCache>;

  NS_DECL_ISUPPORTS
  NS_DECL_NSIMEMORYREPORTER

  SharedStyleSheetCache();
  void Init();

  NS_IMETHOD Observe(nsISupports* aSubject, const char* aTopic,
                     const char16_t* aData) override {
    return Base::DoObserve(aSubject, aTopic, aData);
  }

  void ClearAllProcessesForSafeBrowsingUpdate() override {
    Clear(/* aChrome = */ Some(false));
  }

  // This has to be static because it's also called for loaders that don't have
  // a sheet cache (loaders that are not owned by a document).
  static void LoadCompleted(SharedStyleSheetCache*, css::SheetLoadData&,
                            nsresult, css::Loader::TriggeringPendingLoad);
  using Base::LoadCompleted;
  static void LoadCompletedInternal(SharedStyleSheetCache*, css::SheetLoadData&,
                                    nsTArray<RefPtr<css::SheetLoadData>>&);
  static void Clear(const Maybe<bool>& aChrome = Nothing(),
                    const Maybe<nsCOMPtr<nsIPrincipal>>& aPrincipal = Nothing(),
                    const Maybe<nsCString>& aSchemelessSite = Nothing(),
                    const Maybe<OriginAttributesPattern>& aPattern = Nothing(),
                    const Maybe<nsCString>& aURL = Nothing());

  void EvictPrincipal(nsIPrincipal* aPrincipal) {
    Base::EvictPrincipal(aPrincipal);
    mInlineSheets.Remove(aPrincipal);
  }

  void ClearInProcess(const Maybe<bool>& aChrome,
                      const Maybe<nsCOMPtr<nsIPrincipal>>& aPrincipal,
                      const Maybe<nsCString>& aSchemelessSite,
                      const Maybe<OriginAttributesPattern>& aPattern,
                      const Maybe<nsCString>& aURL);

  size_t SizeOfIncludingThis(MallocSizeOf) const;

  struct InlineSheetEntry {
    RefPtr<StyleSheet> mSheet;
    bool mWasLoadedAsImage = false;
  };
  using InlineSheetCandidates = nsTArray<InlineSheetEntry>;

  template <class F>
  void WithInlineEntryHandle(nsIPrincipal* aPrincipal,
                             const nsACString& aBuffer, F&& aFunc) {
    auto& principalMap = mInlineSheets.LookupOrInsert(aPrincipal);
    return principalMap.WithEntryHandle(aBuffer, std::forward<F>(aFunc));
  }

 protected:
  void InsertIfNeeded(css::SheetLoadData&);
  bool ShouldIgnoreMemoryPressure() override { return false; }
  void DoScheduleGC();
  void GC();

  nsTHashMap<PrincipalHashKey,
             nsTHashMap<nsCStringHashKey, InlineSheetCandidates>>
      mInlineSheets;
  nsCOMPtr<nsITimer> mGCTimer;
  bool mGCScheduled : 1 = false;

  ~SharedStyleSheetCache();

 public:
  static void ScheduleGC() {
    if (!sSingleton || sSingleton->mGCScheduled) {
      return;
    }
    sSingleton->DoScheduleGC();
  }
};

}  // namespace mozilla

#endif
