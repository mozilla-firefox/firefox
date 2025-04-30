/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_SharedSubResourceCache_h__
#define mozilla_SharedSubResourceCache_h__

// A cache that allows us to share subresources across documents. In order to
// use it you need to provide some types, mainly:
//
// * Loader, which implements LoaderPrincipal() and allows you to key per
//   principal. The idea is that this would be the
//   {CSS,Script,Image}Loader object.
//
// * Key (self explanatory). We might want to introduce a common key to
//   share the cache partitioning logic.
//
// * Value, which represents the final cached value. This is expected to
//   be a StyleSheet / Stencil / imgRequestProxy.
//
// * LoadingValue, which must inherit from
//   SharedSubResourceCacheLoadingValueBase (which contains the linked
//   list and the state that the cache manages). It also must provide a
//   ValueForCache() and ExpirationTime() members. For style, this is the
//   SheetLoadData.

#include "mozilla/PrincipalHashKey.h"
#include "mozilla/RefPtr.h"
#include "mozilla/WeakPtr.h"
#include "nsTHashMap.h"
#include "nsIMemoryReporter.h"
#include "nsRefPtrHashtable.h"
#include "mozilla/MemoryReporting.h"
#include "mozilla/StoragePrincipalHelper.h"
#include "mozilla/dom/CacheExpirationTime.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/dom/Document.h"
#include "nsContentUtils.h"
#include "nsHttpResponseHead.h"
#include "nsISupportsImpl.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/dom/CacheablePerformanceTimingData.h"

namespace mozilla {

// A struct to hold the network-related metadata associated with the cache.
//
// When inserting a cache, the consumer should create this from the request and
// make it available via
// SharedSubResourceCacheLoadingValueBase::GetNetworkMetadata.
//
// When using a cache, the consumer can retrieve this from
// SharedSubResourceCache::Result::mNetworkMetadata and use it for notifying
// the observers once the necessary data becomes ready.
// This struct is ref-counted in order to allow this usage.
class SubResourceNetworkMetadataHolder {
 public:
  SubResourceNetworkMetadataHolder() = delete;

  explicit SubResourceNetworkMetadataHolder(nsIRequest* aRequest);

  const dom::CacheablePerformanceTimingData* GetPerfData() const {
    return mPerfData.ptrOr(nullptr);
  }

  const net::nsHttpResponseHead* GetResponseHead() const {
    return mResponseHead.get();
  }

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(SubResourceNetworkMetadataHolder)

 private:
  ~SubResourceNetworkMetadataHolder() = default;

  mozilla::Maybe<dom::CacheablePerformanceTimingData> mPerfData;
  mozilla::UniquePtr<net::nsHttpResponseHead> mResponseHead;
};

enum class CachedSubResourceState {
  Miss,
  Loading,
  Pending,
  Complete,
};

template <typename Derived>
struct SharedSubResourceCacheLoadingValueBase {
  // Whether we're in the "loading" hash table.
  RefPtr<Derived> mNext;

  virtual bool IsLoading() const = 0;
  virtual bool IsCancelled() const = 0;
  virtual bool IsSyncLoad() const = 0;

  virtual SubResourceNetworkMetadataHolder* GetNetworkMetadata() const = 0;

  virtual void StartLoading() = 0;
  virtual void SetLoadCompleted() = 0;
  virtual void OnCoalescedTo(const Derived& aExistingLoad) = 0;
  virtual void Cancel() = 0;

  // Return the next sub-resource which has the same key.
  Derived* GetNextSubResource() { return mNext; }

  ~SharedSubResourceCacheLoadingValueBase() {
    // Do this iteratively to avoid blowing up the stack.
    RefPtr<Derived> next = std::move(mNext);
    while (next) {
      next = std::move(next->mNext);
    }
  }
};

namespace SharedSubResourceCacheUtils {

void AddPerformanceEntryForCache(
    const nsString& aEntryName, const nsString& aInitiatorType,
    const SubResourceNetworkMetadataHolder* aNetworkMetadata,
    TimeStamp aStartTime, TimeStamp aEndTime, dom::Document* aDocument);

bool ShouldClearEntry(nsIURI* aEntryURI, nsIPrincipal* aEntryLoaderPrincipal,
                      nsIPrincipal* aEntryPartitionPrincipal,
                      const Maybe<bool>& aChrome,
                      const Maybe<nsCOMPtr<nsIPrincipal>>& aPrincipal,
                      const Maybe<nsCString>& aSchemelessSite,
                      const Maybe<OriginAttributesPattern>& aPattern,
                      const Maybe<nsCString>& aURL);

}  // namespace SharedSubResourceCacheUtils

template <typename Traits, typename Derived>
class SharedSubResourceCache {
 private:
  using Loader = typename Traits::Loader;
  using Key = typename Traits::Key;
  using Value = typename Traits::Value;
  using LoadingValue = typename Traits::LoadingValue;
  static Key KeyFromLoadingValue(const LoadingValue& aValue) {
    return Traits::KeyFromLoadingValue(aValue);
  }

  const Derived& AsDerived() const {
    return *static_cast<const Derived*>(this);
  }
  Derived& AsDerived() { return *static_cast<Derived*>(this); }

 public:
  SharedSubResourceCache(const SharedSubResourceCache&) = delete;
  SharedSubResourceCache(SharedSubResourceCache&&) = delete;
  SharedSubResourceCache() = default;

  static Derived* Get() {
    static_assert(
        std::is_base_of_v<SharedSubResourceCacheLoadingValueBase<LoadingValue>,
                          LoadingValue>);

    if (sSingleton) {
      return sSingleton.get();
    }
    MOZ_DIAGNOSTIC_ASSERT(!sSingleton);
    sSingleton = new Derived();
    sSingleton->Init();
    return sSingleton.get();
  }

  static void DeleteSingleton() { sSingleton = nullptr; }

 protected:
  struct CompleteSubResource {
    RefPtr<Value> mResource;
    RefPtr<SubResourceNetworkMetadataHolder> mNetworkMetadata;
    CacheExpirationTime mExpirationTime = CacheExpirationTime::Never();
    bool mWasSyncLoad = false;

    explicit CompleteSubResource(LoadingValue& aValue)
        : mResource(aValue.ValueForCache()),
          mNetworkMetadata(aValue.GetNetworkMetadata()),
          mExpirationTime(aValue.ExpirationTime()),
          mWasSyncLoad(aValue.IsSyncLoad()) {}

    inline bool Expired() const;
  };

 public:
  struct Result {
    Value* mCompleteValue = nullptr;
    RefPtr<SubResourceNetworkMetadataHolder> mNetworkMetadata;

    LoadingValue* mLoadingOrPendingValue = nullptr;
    CachedSubResourceState mState = CachedSubResourceState::Miss;

    constexpr Result() = default;

    explicit constexpr Result(const CompleteSubResource& aCompleteSubResource)
        : mCompleteValue(aCompleteSubResource.mResource.get()),
          mNetworkMetadata(aCompleteSubResource.mNetworkMetadata),
          mLoadingOrPendingValue(nullptr),
          mState(CachedSubResourceState::Complete) {}

    constexpr Result(LoadingValue* aLoadingOrPendingValue,
                     CachedSubResourceState aState)
        : mLoadingOrPendingValue(aLoadingOrPendingValue), mState(aState) {}
  };

  Result Lookup(Loader&, const Key&, bool aSyncLoad);

  // Tries to coalesce with an already existing load. The sheet state must be
  // the one that Lookup returned, if it returned a sheet.
  //
  // TODO(emilio): Maybe try to merge this with the lookup? Most consumers could
  // have a data there already.
  [[nodiscard]] bool CoalesceLoad(const Key&, LoadingValue& aNewLoad,
                                  CachedSubResourceState aExistingLoadState);

  size_t SizeOfExcludingThis(MallocSizeOf) const;

  // Puts the load into the "loading" set.
  void LoadStarted(const Key&, LoadingValue&);

  // Removes the load from the "loading" set if there.
  void LoadCompleted(LoadingValue&);

  // Inserts a value into the cache.
  void Insert(LoadingValue&);

  // Evict the specific cache.
  void Evict(const Key&);

  // Puts a load into the "pending" set.
  void DeferLoad(const Key&, LoadingValue&);

  template <typename Callback>
  void StartPendingLoadsForLoader(Loader&, const Callback& aShouldStartLoad);
  void CancelLoadsForLoader(Loader&);

  // Register a loader into the cache. This has the effect of keeping alive all
  // subresources for the origin of the loader's document until UnregisterLoader
  // is called.
  void RegisterLoader(Loader&);

  // Unregister a loader from the cache.
  //
  // If this is the loader for the last document of a given origin, then all the
  // subresources for that document will be removed from the cache. This needs
  // to be called when the document goes away, or when its principal changes.
  void UnregisterLoader(Loader&);

  void PrepareForShutdown();

  void ClearInProcess(const Maybe<bool>& aChrome,
                      const Maybe<nsCOMPtr<nsIPrincipal>>& aPrincipal,
                      const Maybe<nsCString>& aSchemelessSite,
                      const Maybe<OriginAttributesPattern>& aPattern,
                      const Maybe<nsCString>& aURL);

 protected:
  void CancelPendingLoadsForLoader(Loader&);

  void WillStartPendingLoad(LoadingValue&);

  void EvictPrincipal(nsIPrincipal*);

  nsTHashMap<Key, CompleteSubResource> mComplete;
  nsRefPtrHashtable<Key, LoadingValue> mPending;
  // The SheetLoadData pointers in mLoadingDatas below are weak references that
  // get cleaned up when StreamLoader::OnStopRequest gets called.
  //
  // Note that we hold on to all sheet loads, even if in the end they happen not
  // to be cacheable.
  nsTHashMap<Key, WeakPtr<LoadingValue>> mLoading;

  // An origin-to-number-of-registered-documents count, in order to manage cache
  // eviction as described in RegisterLoader / UnregisterLoader.
  nsTHashMap<PrincipalHashKey, uint32_t> mLoaderPrincipalRefCnt;

 protected:
  // Lazily created in the first Get() call.
  // The singleton should be deleted by DeleteSingleton() during shutdown.
  inline static MOZ_GLOBINIT StaticRefPtr<Derived> sSingleton;
};

template <typename Traits, typename Derived>
void SharedSubResourceCache<Traits, Derived>::ClearInProcess(
    const Maybe<bool>& aChrome, const Maybe<nsCOMPtr<nsIPrincipal>>& aPrincipal,
    const Maybe<nsCString>& aSchemelessSite,
    const Maybe<OriginAttributesPattern>& aPattern,
    const Maybe<nsCString>& aURL) {
  MOZ_ASSERT(aSchemelessSite.isSome() == aPattern.isSome(),
             "Must pass both site and OA pattern.");

  if (!aChrome && !aPrincipal && !aSchemelessSite && !aURL) {
    mComplete.Clear();
    return;
  }

  for (auto iter = mComplete.Iter(); !iter.Done(); iter.Next()) {
    if (SharedSubResourceCacheUtils::ShouldClearEntry(
            iter.Key().URI(), iter.Key().LoaderPrincipal(),
            iter.Key().PartitionPrincipal(), aChrome, aPrincipal,
            aSchemelessSite, aPattern, aURL)) {
      iter.Remove();
    }
  }
}

template <typename Traits, typename Derived>
void SharedSubResourceCache<Traits, Derived>::RegisterLoader(Loader& aLoader) {
  mLoaderPrincipalRefCnt.LookupOrInsert(aLoader.LoaderPrincipal(), 0) += 1;
}

template <typename Traits, typename Derived>
void SharedSubResourceCache<Traits, Derived>::UnregisterLoader(
    Loader& aLoader) {
  nsIPrincipal* prin = aLoader.LoaderPrincipal();
  auto lookup = mLoaderPrincipalRefCnt.Lookup(prin);
  MOZ_RELEASE_ASSERT(lookup);
  MOZ_RELEASE_ASSERT(lookup.Data());
  if (!--lookup.Data()) {
    lookup.Remove();
    // TODO(emilio): Do this off a timer or something maybe, though in practice
    // BFCache is good enough at keeping things alive.
    AsDerived().EvictPrincipal(prin);
  }
}

template <typename Traits, typename Derived>
void SharedSubResourceCache<Traits, Derived>::EvictPrincipal(
    nsIPrincipal* aPrincipal) {
  for (auto iter = mComplete.Iter(); !iter.Done(); iter.Next()) {
    if (iter.Key().LoaderPrincipal()->Equals(aPrincipal)) {
      iter.Remove();
    }
  }
}

template <typename Traits, typename Derived>
void SharedSubResourceCache<Traits, Derived>::CancelPendingLoadsForLoader(
    Loader& aLoader) {
  AutoTArray<RefPtr<LoadingValue>, 10> arr;

  for (auto iter = mPending.Iter(); !iter.Done(); iter.Next()) {
    RefPtr<LoadingValue>& first = iter.Data();
    LoadingValue* prev = nullptr;
    LoadingValue* current = iter.Data();
    do {
      if (&current->Loader() != &aLoader) {
        prev = current;
        current = current->mNext;
        continue;
      }
      // Detach the load from the list, mark it as cancelled, and then below
      // call SheetComplete on it.
      RefPtr<LoadingValue> strong =
          prev ? std::move(prev->mNext) : std::move(first);
      MOZ_ASSERT(strong == current);
      if (prev) {
        prev->mNext = std::move(strong->mNext);
        current = prev->mNext;
      } else {
        first = std::move(strong->mNext);
        current = first;
      }
      arr.AppendElement(std::move(strong));
    } while (current);

    if (!first) {
      iter.Remove();
    }
  }

  for (auto& loading : arr) {
    loading->DidCancelLoad();
  }
}

template <typename Traits, typename Derived>
void SharedSubResourceCache<Traits, Derived>::WillStartPendingLoad(
    LoadingValue& aData) {
  LoadingValue* curr = &aData;
  do {
    curr->Loader().WillStartPendingLoad();
  } while ((curr = curr->mNext));
}

template <typename Traits, typename Derived>
void SharedSubResourceCache<Traits, Derived>::CancelLoadsForLoader(
    Loader& aLoader) {
  CancelPendingLoadsForLoader(aLoader);

  // We can't stop in-progress loads because some other loader may care about
  // them.
  for (LoadingValue* data : mLoading.Values()) {
    MOZ_DIAGNOSTIC_ASSERT(data,
                          "We weren't properly notified and the load was "
                          "incorrectly dropped on the floor");
    for (; data; data = data->mNext) {
      if (&data->Loader() == &aLoader) {
        data->Cancel();
        MOZ_ASSERT(data->IsCancelled());
      }
    }
  }
}

template <typename Traits, typename Derived>
void SharedSubResourceCache<Traits, Derived>::DeferLoad(const Key& aKey,
                                                        LoadingValue& aValue) {
  MOZ_ASSERT(KeyFromLoadingValue(aValue).KeyEquals(aKey));
  MOZ_DIAGNOSTIC_ASSERT(!aValue.mNext, "Should only defer loads once");

  mPending.InsertOrUpdate(aKey, RefPtr{&aValue});
}

template <typename Traits, typename Derived>
template <typename Callback>
void SharedSubResourceCache<Traits, Derived>::StartPendingLoadsForLoader(
    Loader& aLoader, const Callback& aShouldStartLoad) {
  AutoTArray<RefPtr<LoadingValue>, 10> arr;

  for (auto iter = mPending.Iter(); !iter.Done(); iter.Next()) {
    bool startIt = false;
    {
      LoadingValue* data = iter.Data();
      do {
        if (&data->Loader() == &aLoader) {
          if (aShouldStartLoad(*data)) {
            startIt = true;
            break;
          }
        }
      } while ((data = data->mNext));
    }
    if (startIt) {
      arr.AppendElement(std::move(iter.Data()));
      iter.Remove();
    }
  }
  for (auto& data : arr) {
    WillStartPendingLoad(*data);
    data->StartPendingLoad();
  }
}

template <typename Traits, typename Derived>
void SharedSubResourceCache<Traits, Derived>::Insert(LoadingValue& aValue) {
  auto key = KeyFromLoadingValue(aValue);
#ifdef DEBUG
  // We only expect a complete entry to be overriding when:
  //  * It's expired.
  //  * We're explicitly bypassing the cache.
  //  * Our entry is a sync load that was completed after aValue started loading
  //    async.
  for (const auto& entry : mComplete) {
    if (key.KeyEquals(entry.GetKey())) {
      MOZ_ASSERT(entry.GetData().Expired() ||
                     aValue.Loader().ShouldBypassCache() ||
                     (entry.GetData().mWasSyncLoad && !aValue.IsSyncLoad()),
                 "Overriding existing complete entry?");
    }
  }
#endif

  mComplete.InsertOrUpdate(key, CompleteSubResource(aValue));
}

template <typename Traits, typename Derived>
void SharedSubResourceCache<Traits, Derived>::Evict(const Key& aKey) {
  (void)mComplete.Remove(aKey);
}

template <typename Traits, typename Derived>
bool SharedSubResourceCache<Traits, Derived>::CoalesceLoad(
    const Key& aKey, LoadingValue& aNewLoad,
    CachedSubResourceState aExistingLoadState) {
  MOZ_ASSERT(KeyFromLoadingValue(aNewLoad).KeyEquals(aKey));
  // TODO(emilio): If aExistingLoadState is inconvenient, we could get rid of it
  // by paying two hash lookups...
  LoadingValue* existingLoad = nullptr;
  if (aExistingLoadState == CachedSubResourceState::Loading) {
    existingLoad = mLoading.Get(aKey);
    MOZ_ASSERT(existingLoad, "Caller lied about the state");
  } else if (aExistingLoadState == CachedSubResourceState::Pending) {
    existingLoad = mPending.GetWeak(aKey);
    MOZ_ASSERT(existingLoad, "Caller lied about the state");
  }

  if (!existingLoad) {
    return false;
  }

  if (aExistingLoadState == CachedSubResourceState::Pending &&
      !aNewLoad.ShouldDefer()) {
    // Kick the load off; someone cares about it right away
    RefPtr<LoadingValue> removedLoad;
    mPending.Remove(aKey, getter_AddRefs(removedLoad));
    MOZ_ASSERT(removedLoad == existingLoad, "Bad loading table");

    WillStartPendingLoad(*removedLoad);

    // We insert to the front instead of the back, to keep the invariant that
    // the front sheet always is the one that triggers the load.
    aNewLoad.mNext = std::move(removedLoad);
    return false;
  }

  LoadingValue* data = existingLoad;
  while (data->mNext) {
    data = data->mNext;
  }
  data->mNext = &aNewLoad;

  aNewLoad.OnCoalescedTo(*existingLoad);
  return true;
}

template <typename Traits, typename Derived>
auto SharedSubResourceCache<Traits, Derived>::Lookup(Loader& aLoader,
                                                     const Key& aKey,
                                                     bool aSyncLoad) -> Result {
  // Now complete sheets.
  if (auto lookup = mComplete.Lookup(aKey)) {
    const CompleteSubResource& completeSubResource = lookup.Data();
    if ((!aLoader.ShouldBypassCache() && !completeSubResource.Expired()) ||
        aLoader.HasLoaded(aKey)) {
      return Result(completeSubResource);
    }
  }

  if (aSyncLoad) {
    return Result();
  }

  if (LoadingValue* data = mLoading.Get(aKey)) {
    return Result(data, CachedSubResourceState::Loading);
  }

  if (LoadingValue* data = mPending.GetWeak(aKey)) {
    return Result(data, CachedSubResourceState::Pending);
  }

  return {};
}

template <typename Traits, typename Derived>
size_t SharedSubResourceCache<Traits, Derived>::SizeOfExcludingThis(
    MallocSizeOf aMallocSizeOf) const {
  size_t n = mComplete.ShallowSizeOfExcludingThis(aMallocSizeOf);
  for (const auto& data : mComplete.Values()) {
    n += data.mResource->SizeOfIncludingThis(aMallocSizeOf);
  }

  return n;
}

template <typename Traits, typename Derived>
void SharedSubResourceCache<Traits, Derived>::LoadStarted(
    const Key& aKey, LoadingValue& aValue) {
  MOZ_DIAGNOSTIC_ASSERT(!aValue.IsLoading(), "Already loading? How?");
  MOZ_DIAGNOSTIC_ASSERT(KeyFromLoadingValue(aValue).KeyEquals(aKey));
  MOZ_DIAGNOSTIC_ASSERT(!mLoading.Contains(aKey), "Load not coalesced?");
  aValue.StartLoading();
  MOZ_ASSERT(aValue.IsLoading(), "Check that StartLoading is effectful.");
  mLoading.InsertOrUpdate(aKey, &aValue);
}

template <typename Traits, typename Derived>
bool SharedSubResourceCache<Traits, Derived>::CompleteSubResource::Expired()
    const {
  return mExpirationTime.IsExpired();
}

template <typename Traits, typename Derived>
void SharedSubResourceCache<Traits, Derived>::LoadCompleted(
    LoadingValue& aValue) {
  if (!aValue.IsLoading()) {
    return;
  }
  auto key = KeyFromLoadingValue(aValue);
  Maybe<LoadingValue*> value = mLoading.Extract(key);
  MOZ_DIAGNOSTIC_ASSERT(value);
  MOZ_DIAGNOSTIC_ASSERT(value.value() == &aValue);
  Unused << value;
  aValue.SetLoadCompleted();
  MOZ_ASSERT(!aValue.IsLoading(), "Check that SetLoadCompleted is effectful.");
}

}  // namespace mozilla

#endif
