/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CacheLog.h"
#include "CacheStorageService.h"
#include "CacheFileIOManager.h"
#include "CacheObserver.h"
#include "CacheIndex.h"
#include "CacheIndexIterator.h"
#include "CacheStorage.h"
#include "CacheEntry.h"
#include "CacheFileUtils.h"

#include "ErrorList.h"
#include "nsICacheStorageVisitor.h"
#include "nsIObserverService.h"
#include "nsIFile.h"
#include "nsIURI.h"
#include "nsCOMPtr.h"
#include "nsContentUtils.h"
#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsServiceManagerUtils.h"
#include "nsXULAppAPI.h"
#include "mozilla/AtomicBitfields.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/DebugOnly.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "mozilla/Services.h"
#include "mozilla/StoragePrincipalHelper.h"
#include "mozilla/IntegerPrintfMacros.h"
#include "mozilla/glean/NetwerkCache2Metrics.h"
#include "mozilla/StaticPrefs_network.h"

namespace mozilla::net {

// static
GlobalEntryTables* CacheStorageService::sGlobalEntryTables = nullptr;
StaticMutex CacheStorageService::sLock;

namespace {

void AppendMemoryStorageTag(nsAutoCString& key) {
  // Using DEL as the very last ascii-7 character we can use in the list of
  // attributes
  key.Append('\x7f');
  key.Append(',');
}

}  // namespace

CacheMemoryConsumer::CacheMemoryConsumer(uint32_t aFlags) {
  StoreFlags(aFlags);
}

void CacheMemoryConsumer::DoMemoryReport(uint32_t aCurrentSize) {
  if (!(LoadFlags() & DONT_REPORT) && CacheStorageService::Self()) {
    CacheStorageService::Self()->OnMemoryConsumptionChange(this, aCurrentSize);
  }
}

CacheStorageService::MemoryPool::MemoryPool(EType aType) : mType(aType) {}

CacheStorageService::MemoryPool::~MemoryPool() {
  if (mMemorySize != 0) {
    NS_ERROR(
        "Network cache reported memory consumption is not at 0, probably "
        "leaking?");
  }
}

uint32_t CacheStorageService::MemoryPool::Limit() const {
  uint32_t limit = 0;

  switch (mType) {
    case DISK:
      limit = CacheObserver::MetadataMemoryLimit();
      break;
    case MEMORY:
      limit = CacheObserver::MemoryCacheCapacity();
      break;
    default:
      MOZ_CRASH("Bad pool type");
  }

  static const uint32_t kMaxLimit = 0x3FFFFF;
  if (limit > kMaxLimit) {
    LOG(("  a memory limit (%u) is unexpectedly high, clipping to %u", limit,
         kMaxLimit));
    limit = kMaxLimit;
  }

  return limit << 10;
}

NS_IMPL_ISUPPORTS(CacheStorageService, nsICacheStorageService,
                  nsIMemoryReporter, nsITimerCallback, nsICacheTesting,
                  nsINamed)

CacheStorageService* CacheStorageService::sSelf = nullptr;

CacheStorageService::CacheStorageService() {
  CacheFileIOManager::Init();

  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(!sSelf);

  sSelf = this;
  sGlobalEntryTables = new GlobalEntryTables();

  RegisterStrongMemoryReporter(do_AddRef(this));
}

CacheStorageService::~CacheStorageService() {
  LOG(("CacheStorageService::~CacheStorageService"));
  sSelf = nullptr;
}

void CacheStorageService::Shutdown() {
  StaticMutexAutoLock lock(sLock);

  if (mShutdown) return;

  LOG(("CacheStorageService::Shutdown - start"));

  mShutdown = true;

  nsCOMPtr<nsIRunnable> event =
      NewRunnableMethod("net::CacheStorageService::ShutdownBackground", this,
                        &CacheStorageService::ShutdownBackground);
  Dispatch(event);

#ifdef NS_FREE_PERMANENT_DATA
  // Clear pending callbacks on all entries before dropping them.
  // Each pending Callback holds a RefPtr<CacheEntry> back to its owning
  // entry, forming a prevent-release cycle that the destructor alone
  // cannot break.
  for (const auto& table : sGlobalEntryTables->Values()) {
    for (const auto& entry : table->Values()) {
      entry->ClearCallbacks();
    }
  }
  sGlobalEntryTables->Clear();
  delete sGlobalEntryTables;
#endif
  sGlobalEntryTables = nullptr;

  LOG(("CacheStorageService::Shutdown - done"));
}

void CacheStorageService::ShutdownBackground() {
  LOG(("CacheStorageService::ShutdownBackground - start"));

  MOZ_ASSERT(IsOnManagementThread());

  {
    StaticMutexAutoLock lock(sLock);

    // Cancel purge timer to avoid leaking.
    if (mPurgeTimer) {
      LOG(("  freeing the timer"));
      mPurgeTimer->Cancel();
    }
  }

#ifdef NS_FREE_PERMANENT_DATA
  // Properly unregister entries before clearing the lists to maintain
  // the invariant that IsRegistered() reflects actual list membership.
  // This prevents crashes if pending UNREGISTER operations run after shutdown.
  RefPtr<CacheEntry> entry;
  while ((entry = Pool(MemoryPool::EType::DISK).mManagedEntries.popFirst())) {
    entry->SetRegistered(false);
  }
  while ((entry = Pool(MemoryPool::EType::MEMORY).mManagedEntries.popFirst())) {
    entry->SetRegistered(false);
  }
#endif

  LOG(("CacheStorageService::ShutdownBackground - done"));
}

// Internal management methods

namespace CacheStorageServiceInternal {

// WalkCacheRunnable
// Base class for particular storage entries visiting
class WalkCacheRunnable : public Runnable,
                          public CacheStorageService::EntryInfoCallback {
 protected:
  WalkCacheRunnable(nsICacheStorageVisitor* aVisitor, bool aVisitEntries)
      : Runnable("net::WalkCacheRunnable"),
        mService(CacheStorageService::Self()),
        mCallback(aVisitor) {
    MOZ_ASSERT(NS_IsMainThread());
    StoreNotifyStorage(true);
    StoreVisitEntries(aVisitEntries);
  }

  virtual ~WalkCacheRunnable() {
    if (mCallback) {
      ProxyReleaseMainThread("WalkCacheRunnable::mCallback", mCallback);
    }
  }

  RefPtr<CacheStorageService> mService;
  nsCOMPtr<nsICacheStorageVisitor> mCallback;

  uint64_t mSize{0};

  // clang-format off
  MOZ_ATOMIC_BITFIELDS(mAtomicBitfields, 8, (
    (bool, NotifyStorage, 1),
    (bool, VisitEntries, 1)
  ))
  // clang-format on

  Atomic<bool> mCancel{false};
};

// WalkMemoryCacheRunnable
// Responsible to visit memory storage and walk
// all entries on it asynchronously.
class WalkMemoryCacheRunnable : public WalkCacheRunnable {
 public:
  WalkMemoryCacheRunnable(nsILoadContextInfo* aLoadInfo, bool aVisitEntries,
                          nsICacheStorageVisitor* aVisitor)
      : WalkCacheRunnable(aVisitor, aVisitEntries) {
    CacheFileUtils::AppendKeyPrefix(aLoadInfo, mContextKey);
    MOZ_ASSERT(NS_IsMainThread());
  }

  nsresult Walk() { return mService->Dispatch(this); }

 private:
  NS_IMETHOD Run() override {
    if (CacheStorageService::IsOnManagementThread()) {
      LOG(("WalkMemoryCacheRunnable::Run - collecting [this=%p]", this));
      // First, walk, count and grab all entries from the storage

      StaticMutexAutoLock lock(CacheStorageService::sLock);

      if (!CacheStorageService::IsRunning()) return NS_ERROR_NOT_INITIALIZED;

      // Count the entries to allocate the array memory all at once.
      size_t numEntries = 0;
      for (const auto& entries :
           CacheStorageService::sGlobalEntryTables->Values()) {
        if (entries->Type() != CacheEntryTable::MEMORY_ONLY) {
          continue;
        }
        numEntries += entries->Values().Count();
      }
      mEntryArray.SetCapacity(numEntries);

      // Collect the entries.
      for (const auto& entries :
           CacheStorageService::sGlobalEntryTables->Values()) {
        if (entries->Type() != CacheEntryTable::MEMORY_ONLY) {
          continue;
        }

        for (CacheEntry* entry : entries->Values()) {
          MOZ_ASSERT(!entry->IsUsingDisk());

          mSize += entry->GetMetadataMemoryConsumption();

          int64_t size;
          if (NS_SUCCEEDED(entry->GetDataSize(&size))) {
            mSize += size;
          }
          mEntryArray.AppendElement(entry);
        }
      }

      // Next, we dispatch to the main thread
    } else if (NS_IsMainThread()) {
      LOG(("WalkMemoryCacheRunnable::Run - notifying [this=%p]", this));

      if (LoadNotifyStorage()) {
        LOG(("  storage"));

        uint64_t capacity = CacheObserver::MemoryCacheCapacity();
        capacity <<= 10;  // kilobytes to bytes

        // Second, notify overall storage info
        mCallback->OnCacheStorageInfo(mEntryArray.Length(), mSize, capacity,
                                      nullptr);
        if (!LoadVisitEntries()) return NS_OK;  // done

        StoreNotifyStorage(false);

      } else {
        LOG(("  entry [left=%zu, canceled=%d]", mEntryArray.Length(),
             (bool)mCancel));

        // Third, notify each entry until depleted or canceled.
        if (mNextEntryIdx >= mEntryArray.Length() || mCancel) {
          mCallback->OnCacheEntryVisitCompleted();
          return NS_OK;  // done
        }

        // Grab the next entry.
        RefPtr<CacheEntry> entry = std::move(mEntryArray[mNextEntryIdx++]);

        // Invokes this->OnEntryInfo, that calls the callback with all
        // information of the entry.
        CacheStorageService::GetCacheEntryInfo(entry, this);
      }
    } else {
      MOZ_CRASH("Bad thread");
      return NS_ERROR_FAILURE;
    }

    NS_DispatchToMainThread(this);
    return NS_OK;
  }

  virtual ~WalkMemoryCacheRunnable() {
    if (mCallback) {
      ProxyReleaseMainThread("WalkMemoryCacheRunnable::mCallback", mCallback);
    }
  }

  virtual void OnEntryInfo(const nsACString& aURISpec,
                           const nsACString& aIdEnhance, int64_t aDataSize,
                           int64_t aAltDataSize, uint32_t aFetchCount,
                           uint32_t aLastModifiedTime, uint32_t aExpirationTime,
                           bool aPinned, nsILoadContextInfo* aInfo) override {
    nsresult rv;

    nsCOMPtr<nsIURI> uri;
    rv = NS_NewURI(getter_AddRefs(uri), aURISpec);
    if (NS_FAILED(rv)) {
      return;
    }

    rv = mCallback->OnCacheEntryInfo(uri, aIdEnhance, aDataSize, aAltDataSize,
                                     aFetchCount, aLastModifiedTime,
                                     aExpirationTime, aPinned, aInfo);
    if (NS_FAILED(rv)) {
      LOG(("  callback failed, canceling the walk"));
      mCancel = true;
    }
  }

 private:
  nsCString mContextKey;
  nsTArray<RefPtr<CacheEntry>> mEntryArray;
  size_t mNextEntryIdx{0};
};

// WalkDiskCacheRunnable
// Using the cache index information to get the list of files per context.
class WalkDiskCacheRunnable : public WalkCacheRunnable {
 public:
  WalkDiskCacheRunnable(nsILoadContextInfo* aLoadInfo, bool aVisitEntries,
                        nsICacheStorageVisitor* aVisitor)
      : WalkCacheRunnable(aVisitor, aVisitEntries),
        mLoadInfo(aLoadInfo),
        mPass(COLLECT_STATS),
        mCount(0) {}

  nsresult Walk() {
    // TODO, bug 998693
    // Initial index build should be forced here so that about:cache soon
    // after startup gives some meaningfull results.

    // Dispatch to the INDEX level in hope that very recent cache entries
    // information gets to the index list before we grab the index iterator
    // for the first time.  This tries to avoid miss of entries that has
    // been created right before the visit is required.
    RefPtr<CacheIOThread> thread = CacheFileIOManager::IOThread();
    NS_ENSURE_TRUE(thread, NS_ERROR_NOT_INITIALIZED);

    return thread->Dispatch(this, CacheIOThread::INDEX);
  }

 private:
  // Invokes OnCacheEntryInfo callback for each single found entry.
  // There is one instance of this class per one entry.
  class OnCacheEntryInfoRunnable : public Runnable {
   public:
    explicit OnCacheEntryInfoRunnable(WalkDiskCacheRunnable* aWalker)
        : Runnable("net::WalkDiskCacheRunnable::OnCacheEntryInfoRunnable"),
          mWalker(aWalker) {}

    NS_IMETHOD Run() override {
      MOZ_ASSERT(NS_IsMainThread());

      nsresult rv;

      nsCOMPtr<nsIURI> uri;
      rv = NS_NewURI(getter_AddRefs(uri), mURISpec);
      if (NS_FAILED(rv)) {
        return NS_OK;
      }

      rv = mWalker->mCallback->OnCacheEntryInfo(
          uri, mIdEnhance, mDataSize, mAltDataSize, mFetchCount,
          mLastModifiedTime, mExpirationTime, mPinned, mInfo);
      if (NS_FAILED(rv)) {
        mWalker->mCancel = true;
      }

      return NS_OK;
    }

    RefPtr<WalkDiskCacheRunnable> mWalker;

    nsCString mURISpec;
    nsCString mIdEnhance;
    int64_t mDataSize{0};
    int64_t mAltDataSize{0};
    uint32_t mFetchCount{0};
    uint32_t mLastModifiedTime{0};
    uint32_t mExpirationTime{0};
    bool mPinned{false};
    nsCOMPtr<nsILoadContextInfo> mInfo;
  };

  NS_IMETHOD Run() override {
    // The main loop
    nsresult rv;

    if (CacheStorageService::IsOnManagementThread()) {
      switch (mPass) {
        case COLLECT_STATS:
          // Get quickly the cache stats.
          uint32_t size;
          rv = CacheIndex::GetCacheStats(mLoadInfo, &size, &mCount);
          if (NS_FAILED(rv)) {
            if (LoadVisitEntries()) {
              // both onStorageInfo and onCompleted are expected
              NS_DispatchToMainThread(this);
            }
            return NS_DispatchToMainThread(this);
          }

          mSize = static_cast<uint64_t>(size) << 10;

          // Invoke onCacheStorageInfo with valid information.
          NS_DispatchToMainThread(this);

          if (!LoadVisitEntries()) {
            return NS_OK;  // done
          }

          mPass = ITERATE_METADATA;
          [[fallthrough]];

        case ITERATE_METADATA:
          // Now grab the context iterator.
          if (!mIter) {
            rv =
                CacheIndex::GetIterator(mLoadInfo, true, getter_AddRefs(mIter));
            if (NS_FAILED(rv)) {
              // Invoke onCacheEntryVisitCompleted now
              return NS_DispatchToMainThread(this);
            }
          }

          while (!mCancel && !CacheObserver::ShuttingDown()) {
            if (CacheIOThread::YieldAndRerun()) return NS_OK;

            SHA1Sum::Hash hash;
            rv = mIter->GetNextHash(&hash);
            if (NS_FAILED(rv)) break;  // done (or error?)

            // This synchronously invokes OnEntryInfo on this class where we
            // redispatch to the main thread for the consumer callback.
            CacheFileIOManager::GetEntryInfo(&hash, this);
          }

          // Invoke onCacheEntryVisitCompleted on the main thread
          NS_DispatchToMainThread(this);
      }
    } else if (NS_IsMainThread()) {
      if (LoadNotifyStorage()) {
        nsCOMPtr<nsIFile> dir;
        CacheFileIOManager::GetCacheDirectory(getter_AddRefs(dir));
        uint64_t capacity = CacheObserver::DiskCacheCapacity();
        capacity <<= 10;  // kilobytes to bytes
        mCallback->OnCacheStorageInfo(mCount, mSize, capacity, dir);
        StoreNotifyStorage(false);
      } else {
        mCallback->OnCacheEntryVisitCompleted();
      }
    } else {
      MOZ_CRASH("Bad thread");
      return NS_ERROR_FAILURE;
    }

    return NS_OK;
  }

  virtual void OnEntryInfo(const nsACString& aURISpec,
                           const nsACString& aIdEnhance, int64_t aDataSize,
                           int64_t aAltDataSize, uint32_t aFetchCount,
                           uint32_t aLastModifiedTime, uint32_t aExpirationTime,
                           bool aPinned, nsILoadContextInfo* aInfo) override {
    // Called directly from CacheFileIOManager::GetEntryInfo.

    // Invoke onCacheEntryInfo on the main thread for this entry.
    RefPtr<OnCacheEntryInfoRunnable> info = new OnCacheEntryInfoRunnable(this);
    info->mURISpec = aURISpec;
    info->mIdEnhance = aIdEnhance;
    info->mDataSize = aDataSize;
    info->mAltDataSize = aAltDataSize;
    info->mFetchCount = aFetchCount;
    info->mLastModifiedTime = aLastModifiedTime;
    info->mExpirationTime = aExpirationTime;
    info->mPinned = aPinned;
    info->mInfo = aInfo;

    NS_DispatchToMainThread(info);
  }

  RefPtr<nsILoadContextInfo> mLoadInfo;
  enum {
    // First, we collect stats for the load context.
    COLLECT_STATS,

    // Second, if demanded, we iterate over the entries gethered
    // from the iterator and call CacheFileIOManager::GetEntryInfo
    // for each found entry.
    ITERATE_METADATA,
  } mPass;

  RefPtr<CacheIndexIterator> mIter;
  uint32_t mCount;
};

}  // namespace CacheStorageServiceInternal

void CacheStorageService::DropPrivateBrowsingEntries() {
  StaticMutexAutoLock lock(sLock);

  if (mShutdown) return;

  nsTArray<nsCString> keys;
  for (const nsACString& key : sGlobalEntryTables->Keys()) {
    nsCOMPtr<nsILoadContextInfo> info = CacheFileUtils::ParseKey(key);
    if (info && info->IsPrivate()) {
      keys.AppendElement(key);
    }
  }

  for (uint32_t i = 0; i < keys.Length(); ++i) {
    DoomStorageEntries(keys[i], nullptr, true, false, nullptr);
  }
}

// Helper methods

// static
bool CacheStorageService::IsOnManagementThread() {
  RefPtr<CacheStorageService> service = Self();
  if (!service) return false;

  nsCOMPtr<nsIEventTarget> target = service->Thread();
  if (!target) return false;

  bool currentThread;
  nsresult rv = target->IsOnCurrentThread(&currentThread);
  return NS_SUCCEEDED(rv) && currentThread;
}

already_AddRefed<nsIEventTarget> CacheStorageService::Thread() const {
  return CacheFileIOManager::IOTarget();
}

nsresult CacheStorageService::Dispatch(nsIRunnable* aEvent) {
  RefPtr<CacheIOThread> cacheIOThread = CacheFileIOManager::IOThread();
  if (!cacheIOThread) return NS_ERROR_NOT_AVAILABLE;

  return cacheIOThread->Dispatch(aEvent, CacheIOThread::MANAGEMENT);
}

namespace CacheStorageEvictHelper {

nsresult ClearStorage(bool const aPrivate, bool const aAnonymous,
                      OriginAttributes& aOa) {
  nsresult rv;

  aOa.SyncAttributesWithPrivateBrowsing(aPrivate);
  RefPtr<LoadContextInfo> info = GetLoadContextInfo(aAnonymous, aOa);

  nsCOMPtr<nsICacheStorage> storage;
  RefPtr<CacheStorageService> service = CacheStorageService::Self();
  NS_ENSURE_TRUE(service, NS_ERROR_FAILURE);

  // Clear disk storage
  rv = service->DiskCacheStorage(info, getter_AddRefs(storage));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = storage->AsyncEvictStorage(nullptr);
  NS_ENSURE_SUCCESS(rv, rv);

  // Clear memory storage
  rv = service->MemoryCacheStorage(info, getter_AddRefs(storage));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = storage->AsyncEvictStorage(nullptr);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

nsresult Run(OriginAttributes& aOa) {
  nsresult rv;

  // Clear all [private X anonymous] combinations
  rv = ClearStorage(false, false, aOa);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = ClearStorage(false, true, aOa);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = ClearStorage(true, false, aOa);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = ClearStorage(true, true, aOa);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

}  // namespace CacheStorageEvictHelper

// nsICacheStorageService

NS_IMETHODIMP CacheStorageService::MemoryCacheStorage(
    nsILoadContextInfo* aLoadContextInfo, nsICacheStorage** _retval) {
  NS_ENSURE_ARG(_retval);

  nsCOMPtr<nsICacheStorage> storage =
      new CacheStorage(aLoadContextInfo, false, false, false);
  storage.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP CacheStorageService::DiskCacheStorage(
    nsILoadContextInfo* aLoadContextInfo, nsICacheStorage** _retval) {
  NS_ENSURE_ARG(_retval);

  // TODO save some heap granularity - cache commonly used storages.

  // When disk cache is disabled, still provide a storage, but just keep stuff
  // in memory.
  bool useDisk = CacheObserver::UseDiskCache();

  nsCOMPtr<nsICacheStorage> storage = new CacheStorage(
      aLoadContextInfo, useDisk, false /* size limit */, false /* don't pin */);
  storage.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP CacheStorageService::PinningCacheStorage(
    nsILoadContextInfo* aLoadContextInfo, nsICacheStorage** _retval) {
  NS_ENSURE_ARG(aLoadContextInfo);
  NS_ENSURE_ARG(_retval);

  // When disk cache is disabled don't pretend we cache.
  if (!CacheObserver::UseDiskCache()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsCOMPtr<nsICacheStorage> storage =
      new CacheStorage(aLoadContextInfo, true /* use disk */,
                       true /* ignore size checks */, true /* pin */);
  storage.forget(_retval);
  return NS_OK;
}

NS_IMETHODIMP CacheStorageService::Clear() {
  nsresult rv;

  // Tell the index to block notification to AsyncGetDiskConsumption.
  // Will be allowed again from CacheFileContextEvictor::EvictEntries()
  // when all the context have been removed from disk.
  CacheIndex::OnAsyncEviction(true);

  StaticMutexAutoLock lock(sLock);

  {
    mozilla::MutexAutoLock forcedValidEntriesLock(mForcedValidEntriesLock);
    mForcedValidEntries.Clear();
  }

  NS_ENSURE_TRUE(!mShutdown, NS_ERROR_NOT_INITIALIZED);

  const auto keys = ToTArray<nsTArray<nsCString>>(sGlobalEntryTables->Keys());
  for (const auto& key : keys) {
    DoomStorageEntries(key, nullptr, true, false, nullptr);
  }

  // Passing null as a load info means to evict all contexts.
  // EvictByContext() respects the entry pinning.  EvictAll() does not.
  rv = CacheFileIOManager::EvictByContext(nullptr, false, u""_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP CacheStorageService::ClearOriginsByPrincipal(
    nsIPrincipal* aPrincipal) {
  nsresult rv;

  if (NS_WARN_IF(!aPrincipal)) {
    return NS_ERROR_FAILURE;
  }

  nsAutoString origin;
  rv = nsContentUtils::GetWebExposedOriginSerialization(aPrincipal, origin);
  NS_ENSURE_SUCCESS(rv, rv);
  LOG(("CacheStorageService::ClearOriginsByPrincipal %s",
       NS_ConvertUTF16toUTF8(origin).get()));

  rv = ClearOriginInternal(origin, aPrincipal->OriginAttributesRef(), true);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = ClearOriginInternal(origin, aPrincipal->OriginAttributesRef(), false);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP CacheStorageService::ClearOriginsByOriginAttributes(
    const nsAString& aOriginAttributes) {
  nsresult rv;
  LOG(("CacheStorageService::ClearOriginsByOriginAttributes %s",
       NS_ConvertUTF16toUTF8(aOriginAttributes).get()));

  if (NS_WARN_IF(aOriginAttributes.IsEmpty())) {
    return NS_ERROR_FAILURE;
  }

  OriginAttributes oa;
  if (!oa.Init(aOriginAttributes)) {
    NS_ERROR("Could not parse the argument for OriginAttributes");
    return NS_ERROR_FAILURE;
  }

  rv = CacheStorageEvictHelper::Run(oa);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

static bool RemoveExactEntry(CacheEntryTable* aEntries, nsACString const& aKey,
                             CacheEntry* aEntry, bool aOverwrite) {
  RefPtr<CacheEntry> existingEntry;
  if (!aEntries->Get(aKey, getter_AddRefs(existingEntry))) {
    LOG(("RemoveExactEntry [entry=%p already gone]", aEntry));
    return false;  // Already removed...
  }

  if (!aOverwrite && existingEntry != aEntry) {
    LOG(("RemoveExactEntry [entry=%p already replaced]", aEntry));
    return false;  // Already replaced...
  }

  LOG(("RemoveExactEntry [entry=%p removed]", aEntry));
  aEntries->Remove(aKey);
  return true;
}

NS_IMETHODIMP CacheStorageService::ClearBaseDomain(
    const nsAString& aBaseDomain) {
  LOG(("CacheStorageService::ClearBaseDomain %s",
       NS_ConvertUTF16toUTF8(aBaseDomain).get()));
  StaticMutexAutoLock lock(sLock);
  if (sGlobalEntryTables) {
    if (mShutdown) return NS_ERROR_NOT_AVAILABLE;

    nsCString cBaseDomain = NS_ConvertUTF16toUTF8(aBaseDomain);

    nsTArray<nsCString> keys;
    for (const auto& globalEntry : *sGlobalEntryTables) {
      // Match by partitionKey base domain. This should cover most cache entries
      // because we statically partition the cache. Most first party cache
      // entries will also have a partitionKey set where the partitionKey base
      // domain will match the entry URI base domain.
      const nsACString& key = globalEntry.GetKey();
      nsCOMPtr<nsILoadContextInfo> info =
          CacheFileUtils::ParseKey(globalEntry.GetKey());

      if (info &&
          StoragePrincipalHelper::PartitionKeyHasBaseDomain(
              info->OriginAttributesPtr()->mPartitionKey, aBaseDomain)) {
        keys.AppendElement(key);
        continue;
      }

      // If we didn't get a partitionKey match, try to match by entry URI. This
      // requires us to iterate over all entries.
      CacheEntryTable* table = globalEntry.GetWeak();
      MOZ_ASSERT(table);

      nsTArray<RefPtr<CacheEntry>> entriesToDelete;

      for (CacheEntry* entry : table->Values()) {
        nsCOMPtr<nsIURI> uri;
        nsresult rv = NS_NewURI(getter_AddRefs(uri), entry->GetURI());
        if (NS_WARN_IF(NS_FAILED(rv))) {
          continue;
        }

        nsAutoCString host;
        rv = uri->GetHost(host);
        // Some entries may not have valid hosts. We can skip them.
        if (NS_FAILED(rv) || host.IsEmpty()) {
          continue;
        }

        bool hasRootDomain = false;
        rv = HasRootDomain(host, cBaseDomain, &hasRootDomain);
        if (NS_WARN_IF(NS_FAILED(rv))) {
          continue;
        }
        if (hasRootDomain) {
          entriesToDelete.AppendElement(entry);
        }
      }

      // Clear individual matched entries.
      for (RefPtr<CacheEntry>& entry : entriesToDelete) {
        nsAutoCString entryKey;
        nsresult rv = entry->HashingKey(entryKey);
        if (NS_FAILED(rv)) {
          NS_ERROR("aEntry->HashingKey() failed?");
          return rv;
        }

        RemoveExactEntry(table, entryKey, entry, false /* don't overwrite */);
      }
    }

    // Clear matched keys.
    for (uint32_t i = 0; i < keys.Length(); ++i) {
      LOG(("CacheStorageService::ClearBaseDomain Dooming %s", keys[i].get()));
      DoomStorageEntries(keys[i], nullptr, true, false, nullptr);
    }
  }

  return CacheFileIOManager::EvictByContext(nullptr, false /* pinned */, u""_ns,
                                            aBaseDomain);
}

nsresult CacheStorageService::ClearOriginInternal(
    const nsAString& aOrigin, const OriginAttributes& aOriginAttributes,
    bool aAnonymous) {
  nsresult rv;

  RefPtr<LoadContextInfo> info =
      GetLoadContextInfo(aAnonymous, aOriginAttributes);
  if (NS_WARN_IF(!info)) {
    return NS_ERROR_FAILURE;
  }

  StaticMutexAutoLock lock(sLock);

  if (sGlobalEntryTables) {
    for (const auto& globalEntry : *sGlobalEntryTables) {
      bool matches = false;
      rv = CacheFileUtils::KeyMatchesLoadContextInfo(globalEntry.GetKey(), info,
                                                     &matches);
      NS_ENSURE_SUCCESS(rv, rv);
      if (!matches) {
        continue;
      }

      CacheEntryTable* table = globalEntry.GetWeak();
      MOZ_ASSERT(table);

      nsTArray<RefPtr<CacheEntry>> entriesToDelete;

      for (CacheEntry* entry : table->Values()) {
        nsCOMPtr<nsIURI> uri;
        rv = NS_NewURI(getter_AddRefs(uri), entry->GetURI());
        NS_ENSURE_SUCCESS(rv, rv);

        nsAutoString origin;
        rv = nsContentUtils::GetWebExposedOriginSerialization(uri, origin);
        NS_ENSURE_SUCCESS(rv, rv);

        if (origin != aOrigin) {
          continue;
        }

        entriesToDelete.AppendElement(entry);
      }

      for (RefPtr<CacheEntry>& entry : entriesToDelete) {
        nsAutoCString entryKey;
        rv = entry->HashingKey(entryKey);
        if (NS_FAILED(rv)) {
          NS_ERROR("aEntry->HashingKey() failed?");
          return rv;
        }
        MOZ_ASSERT_IF(info->IsPrivate(), !entry->IsUsingDisk());
        RemoveExactEntry(table, entryKey, entry, false /* don't overwrite */);
      }
    }
  }

  if (!info->IsPrivate()) {
    rv = CacheFileIOManager::EvictByContext(info, false /* pinned */, aOrigin);
  }
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP CacheStorageService::ClearOriginDictionary(nsIURI* aURI) {
  LOG(("CacheStorageService::ClearOriginDictionary"));
  // Note: due to cookie samesite rules, we need to clean for all ports
  DictionaryCache::RemoveDictionariesForOrigin(aURI);
  return NS_OK;
}

NS_IMETHODIMP CacheStorageService::ClearAllOriginDictionaries() {
  LOG(("CacheStorageService::ClearAllOriginDictionaries"));
  DictionaryCache::RemoveAllDictionaries();
  return NS_OK;
}

NS_IMETHODIMP CacheStorageService::PurgeFromMemory(uint32_t aWhat) {
  uint32_t what;

  switch (aWhat) {
    case PURGE_DISK_DATA_ONLY:
      what = CacheEntry::PURGE_DATA_ONLY_DISK_BACKED;
      break;

    case PURGE_DISK_ALL:
      what = CacheEntry::PURGE_WHOLE_ONLY_DISK_BACKED;
      break;

    case PURGE_EVERYTHING:
      what = CacheEntry::PURGE_WHOLE;
      break;

    default:
      return NS_ERROR_INVALID_ARG;
  }

  nsCOMPtr<nsIRunnable> event = new PurgeFromMemoryRunnable(this, what);

  return Dispatch(event);
}

NS_IMETHODIMP CacheStorageService::PurgeFromMemoryRunnable::Run() {
  if (NS_IsMainThread()) {
    nsCOMPtr<nsIObserverService> observerService =
        mozilla::services::GetObserverService();
    if (observerService) {
      observerService->NotifyObservers(
          nullptr, "cacheservice:purge-memory-pools", nullptr);
    }

    return NS_OK;
  }

  if (mService) {
    // Note that we seem to come here only in the case of "memory-pressure"
    // being notified (or in case of tests), so we start from purging in-memory
    // entries first and ignore minprogress for disk entries.
    // TODO not all flags apply to both pools.
    mService->Pool(MemoryPool::EType::MEMORY)
        .PurgeAll(mWhat, StaticPrefs::network_cache_purge_minprogress_memory());
    mService->Pool(MemoryPool::EType::DISK).PurgeAll(mWhat, 0);
    mService = nullptr;
  }

  NS_DispatchToMainThread(this);
  return NS_OK;
}

NS_IMETHODIMP CacheStorageService::AsyncGetDiskConsumption(
    nsICacheStorageConsumptionObserver* aObserver) {
  NS_ENSURE_ARG(aObserver);

  nsresult rv;

  rv = CacheIndex::AsyncGetDiskConsumption(aObserver);
  NS_ENSURE_SUCCESS(rv, rv);

  return NS_OK;
}

NS_IMETHODIMP CacheStorageService::GetIoTarget(nsIEventTarget** aEventTarget) {
  NS_ENSURE_ARG(aEventTarget);

  nsCOMPtr<nsIEventTarget> ioTarget = CacheFileIOManager::IOTarget();
  ioTarget.forget(aEventTarget);

  return NS_OK;
}

NS_IMETHODIMP CacheStorageService::AsyncVisitAllStorages(
    nsICacheStorageVisitor* aVisitor, bool aVisitEntries) {
  LOG(("CacheStorageService::AsyncVisitAllStorages [cb=%p]", aVisitor));
  NS_ENSURE_FALSE(mShutdown, NS_ERROR_NOT_INITIALIZED);

  // Walking the disk cache also walks the memory cache.
  RefPtr<CacheStorageServiceInternal::WalkDiskCacheRunnable> event =
      new CacheStorageServiceInternal::WalkDiskCacheRunnable(
          nullptr, aVisitEntries, aVisitor);
  return event->Walk();
}

// Methods used by CacheEntry for management of in-memory structures.

void CacheStorageService::RegisterEntry(CacheEntry* aEntry) {
  MOZ_ASSERT(IsOnManagementThread());

  if (mShutdown || !aEntry->CanRegister()) return;

  TelemetryRecordEntryCreation(aEntry);

  LOG(("CacheStorageService::RegisterEntry [entry=%p]", aEntry));

  MemoryPool& pool = Pool(aEntry->IsUsingDisk());
  pool.mManagedEntries.insertBack(aEntry);

  aEntry->SetRegistered(true);
}

void CacheStorageService::UnregisterEntry(CacheEntry* aEntry) {
  MOZ_ASSERT(IsOnManagementThread());

  if (!aEntry->IsRegistered()) return;

  TelemetryRecordEntryRemoval(aEntry);

  LOG(("CacheStorageService::UnregisterEntry [entry=%p]", aEntry));

  MemoryPool& pool = Pool(aEntry->IsUsingDisk());
  aEntry->removeFrom(pool.mManagedEntries);

  // Note: aEntry->CanRegister() since now returns false
  aEntry->SetRegistered(false);
}

static bool AddExactEntry(CacheEntryTable* aEntries, nsACString const& aKey,
                          CacheEntry* aEntry, bool aOverwrite) {
  RefPtr<CacheEntry> existingEntry;
  if (!aOverwrite && aEntries->Get(aKey, getter_AddRefs(existingEntry))) {
    bool equals = existingEntry == aEntry;
    LOG(("AddExactEntry [entry=%p equals=%d]", aEntry, equals));
    return equals;  // Already there...
  }

  LOG(("AddExactEntry [entry=%p put]", aEntry));
  aEntries->InsertOrUpdate(aKey, RefPtr{aEntry});
  return true;
}

bool CacheStorageService::RemoveEntry(CacheEntry* aEntry,
                                      bool aOnlyUnreferenced) {
  LOG(("CacheStorageService::RemoveEntry [entry=%p]", aEntry));

  nsAutoCString entryKey;
  nsresult rv = aEntry->HashingKey(entryKey);
  if (NS_FAILED(rv)) {
    NS_ERROR("aEntry->HashingKey() failed?");
    return false;
  }

  StaticMutexAutoLock lock(sLock);

  if (mShutdown) {
    LOG(("  after shutdown"));
    return false;
  }

  if (aOnlyUnreferenced) {
    if (aEntry->IsReferenced()) {
      LOG(("  still referenced, not removing"));
      return false;
    }

    if (!aEntry->IsUsingDisk() &&
        IsForcedValidEntry(aEntry->GetStorageID(), entryKey)) {
      LOG(("  forced valid, not removing"));
      return false;
    }
  }

  CacheEntryTable* entries;
  if (sGlobalEntryTables->Get(aEntry->GetStorageID(), &entries)) {
    RemoveExactEntry(entries, entryKey, aEntry, false /* don't overwrite */);
  }

  nsAutoCString memoryStorageID(aEntry->GetStorageID());
  AppendMemoryStorageTag(memoryStorageID);

  if (sGlobalEntryTables->Get(memoryStorageID, &entries)) {
    RemoveExactEntry(entries, entryKey, aEntry, false /* don't overwrite */);
  }

  return true;
}

void CacheStorageService::RecordMemoryOnlyEntry(CacheEntry* aEntry,
                                                bool aOnlyInMemory,
                                                bool aOverwrite) {
  LOG(
      ("CacheStorageService::RecordMemoryOnlyEntry [entry=%p, memory=%d, "
       "overwrite=%d]",
       aEntry, aOnlyInMemory, aOverwrite));
  // This method is responsible to put this entry to a special record hashtable
  // that contains only entries that are stored in memory.
  // Keep in mind that every entry, regardless of whether is in-memory-only or
  // not is always recorded in the storage master hash table, the one identified
  // by CacheEntry.StorageID().

  sLock.AssertCurrentThreadOwns();

  if (mShutdown) {
    LOG(("  after shutdown"));
    return;
  }

  nsresult rv;

  nsAutoCString entryKey;
  rv = aEntry->HashingKey(entryKey);
  if (NS_FAILED(rv)) {
    NS_ERROR("aEntry->HashingKey() failed?");
    return;
  }

  CacheEntryTable* entries = nullptr;
  nsAutoCString memoryStorageID(aEntry->GetStorageID());
  AppendMemoryStorageTag(memoryStorageID);

  if (!sGlobalEntryTables->Get(memoryStorageID, &entries)) {
    if (!aOnlyInMemory) {
      LOG(("  not recorded as memory only"));
      return;
    }

    entries = sGlobalEntryTables
                  ->InsertOrUpdate(
                      memoryStorageID,
                      MakeUnique<CacheEntryTable>(CacheEntryTable::MEMORY_ONLY))
                  .get();
    LOG(("  new memory-only storage table for %s", memoryStorageID.get()));
  }

  if (aOnlyInMemory) {
    AddExactEntry(entries, entryKey, aEntry, aOverwrite);
  } else {
    RemoveExactEntry(entries, entryKey, aEntry, aOverwrite);
  }
}

// Checks if a cache entry is forced valid (will be loaded directly from cache
// without further validation) - see nsICacheEntry.idl for further details
bool CacheStorageService::IsForcedValidEntry(nsACString const& aContextKey,
                                             nsACString const& aEntryKey) {
  return IsForcedValidEntry(aContextKey + aEntryKey);
}

bool CacheStorageService::IsForcedValidEntry(
    nsACString const& aContextEntryKey) {
  mozilla::MutexAutoLock lock(mForcedValidEntriesLock);

  ForcedValidData data;

  if (!mForcedValidEntries.Get(aContextEntryKey, &data)) {
    return false;
  }

  if (data.validUntil.IsNull()) {
    MOZ_ASSERT_UNREACHABLE("the timeStamp should never be null");
    return false;
  }

  // Entry timeout not reached yet
  if (TimeStamp::NowLoRes() <= data.validUntil) {
    return true;
  }

  // Entry timeout has been reached
  mForcedValidEntries.Remove(aContextEntryKey);

  return false;
}

void CacheStorageService::MarkForcedValidEntryUse(nsACString const& aContextKey,
                                                  nsACString const& aEntryKey) {
  mozilla::MutexAutoLock lock(mForcedValidEntriesLock);

  ForcedValidData data;

  if (!mForcedValidEntries.Get(aContextKey + aEntryKey, &data)) {
    return;
  }

  data.viewed = true;
  mForcedValidEntries.InsertOrUpdate(aContextKey + aEntryKey, data);
}

// Allows a cache entry to be loaded directly from cache without further
// validation - see nsICacheEntry.idl for further details
void CacheStorageService::ForceEntryValidFor(nsACString const& aContextKey,
                                             nsACString const& aEntryKey,
                                             uint32_t aSecondsToTheFuture) {
  mozilla::MutexAutoLock lock(mForcedValidEntriesLock);

  TimeStamp now = TimeStamp::NowLoRes();
  ForcedValidEntriesPrune(now);

  ForcedValidData data;
  data.validUntil = now + TimeDuration::FromSeconds(aSecondsToTheFuture);
  data.viewed = false;

  mForcedValidEntries.InsertOrUpdate(aContextKey + aEntryKey, data);
}

void CacheStorageService::RemoveEntryForceValid(nsACString const& aContextKey,
                                                nsACString const& aEntryKey) {
  mozilla::MutexAutoLock lock(mForcedValidEntriesLock);

  LOG(("CacheStorageService::RemoveEntryForceValid context='%s' entryKey=%s",
       PromiseFlatCString(aContextKey).get(),
       PromiseFlatCString(aEntryKey).get()));
  mForcedValidEntries.Remove(aContextKey + aEntryKey);
}

// Cleans out the old entries in mForcedValidEntries
void CacheStorageService::ForcedValidEntriesPrune(TimeStamp& now) {
  static TimeDuration const oneMinute = TimeDuration::FromSeconds(60);
  static TimeStamp dontPruneUntil = now + oneMinute;
  if (now < dontPruneUntil) return;

  for (auto iter = mForcedValidEntries.Iter(); !iter.Done(); iter.Next()) {
    if (iter.Data().validUntil < now) {
      iter.Remove();
    }
  }
  dontPruneUntil = now + oneMinute;
}

void CacheStorageService::OnMemoryConsumptionChange(
    CacheMemoryConsumer* aConsumer, uint32_t aCurrentMemoryConsumption) {
  LOG(("CacheStorageService::OnMemoryConsumptionChange [consumer=%p, size=%u]",
       aConsumer, aCurrentMemoryConsumption));

  uint32_t savedMemorySize = aConsumer->LoadReportedMemoryConsumption();
  if (savedMemorySize == aCurrentMemoryConsumption) return;

  // Exchange saved size with current one.
  aConsumer->StoreReportedMemoryConsumption(aCurrentMemoryConsumption);

  bool usingDisk = !(aConsumer->LoadFlags() & CacheMemoryConsumer::MEMORY_ONLY);
  bool overLimit = Pool(usingDisk).OnMemoryConsumptionChange(
      savedMemorySize, aCurrentMemoryConsumption);

  if (!overLimit) return;

  // It's likely the timer has already been set when we get here,
  // check outside the lock to save resources.
#ifdef MOZ_TSAN
  if (mPurgeTimerActive) {
#else
  if (mPurgeTimer) {
#endif
    return;
  }

  // We don't know if this is called under the service lock or not,
  // hence rather dispatch.
  RefPtr<nsIEventTarget> cacheIOTarget = Thread();
  if (!cacheIOTarget) return;

  // Dispatch as a priority task, we want to set the purge timer
  // ASAP to prevent vain redispatch of this event.
  nsCOMPtr<nsIRunnable> event = NewRunnableMethod(
      "net::CacheStorageService::SchedulePurgeOverMemoryLimit", this,
      &CacheStorageService::SchedulePurgeOverMemoryLimit);
  cacheIOTarget->Dispatch(event, nsIEventTarget::DISPATCH_NORMAL);
}

bool CacheStorageService::MemoryPool::OnMemoryConsumptionChange(
    uint32_t aSavedMemorySize, uint32_t aCurrentMemoryConsumption) {
  mMemorySize -= aSavedMemorySize;
  mMemorySize += aCurrentMemoryConsumption;

  LOG(("  mMemorySize=%u (+%u,-%u)", uint32_t(mMemorySize),
       aCurrentMemoryConsumption, aSavedMemorySize));

  // Bypass purging when memory has not grew up significantly
  if (aCurrentMemoryConsumption <= aSavedMemorySize) return false;

  return mMemorySize > Limit();
}

void CacheStorageService::SchedulePurgeOverMemoryLimit() {
  LOG(("CacheStorageService::SchedulePurgeOverMemoryLimit"));

  StaticMutexAutoLock lock(sLock);

  if (mShutdown) {
    LOG(("  past shutdown"));
    return;
  }

  if (mPurgeTimer) {
    LOG(("  timer already up"));
    return;
  }

  mPurgeTimer = NS_NewTimer();
  if (mPurgeTimer) {
#ifdef MOZ_TSAN
    mPurgeTimerActive = true;
#endif
    nsresult rv;
    rv = mPurgeTimer->InitWithCallback(this, 1000, nsITimer::TYPE_ONE_SHOT);
    LOG(("  timer init rv=0x%08" PRIx32, static_cast<uint32_t>(rv)));
  }
}

NS_IMETHODIMP
CacheStorageService::Notify(nsITimer* aTimer) {
  LOG(("CacheStorageService::Notify"));

  StaticMutexAutoLock lock(sLock);

  if (aTimer == mPurgeTimer) {
#ifdef MOZ_TSAN
    mPurgeTimerActive = false;
#endif
    mPurgeTimer = nullptr;

    if (!mShutdown) {
      nsCOMPtr<nsIRunnable> event = NewRunnableMethod(
          "net::CacheStorageService::PurgeExpiredOrOverMemoryLimit", this,
          &CacheStorageService::PurgeExpiredOrOverMemoryLimit);
      Dispatch(event);
    }
  }

  return NS_OK;
}

NS_IMETHODIMP
CacheStorageService::GetName(nsACString& aName) {
  aName.AssignLiteral("CacheStorageService");
  return NS_OK;
}

void CacheStorageService::PurgeExpiredOrOverMemoryLimit() {
  MOZ_ASSERT(IsOnManagementThread());

  LOG(("CacheStorageService::PurgeExpiredOrOverMemoryLimit"));

  if (mShutdown) return;

  static TimeDuration const kFourSeconds = TimeDuration::FromSeconds(4);
  TimeStamp now = TimeStamp::NowLoRes();

  if (!mLastPurgeTime.IsNull() && now - mLastPurgeTime < kFourSeconds) {
    LOG(("  bypassed, too soon"));
    return;
  }

  mLastPurgeTime = now;

  // We start purging memory entries first as we care more about RAM over
  // disk space beeing freed in case we are interrupted.
  Pool(MemoryPool::EType::MEMORY).PurgeExpiredOrOverMemoryLimit();
  Pool(MemoryPool::EType::DISK).PurgeExpiredOrOverMemoryLimit();
}

void CacheStorageService::MemoryPool::PurgeExpiredOrOverMemoryLimit() {
  if (StaticPrefs::network_cache_purge_disable()) {
    return;
  }
  TimeStamp start(TimeStamp::Now());

  uint32_t const memoryLimit = Limit();
  size_t minprogress =
      (mType == EType::DISK)
          ? StaticPrefs::network_cache_purge_minprogress_disk()
          : StaticPrefs::network_cache_purge_minprogress_memory();

  // We always purge expired entries, even if under our limit.
  size_t numExpired = PurgeExpired(minprogress);
  if (numExpired > 0) {
    LOG(("  found and purged %zu expired entries", numExpired));
  }
  minprogress = (minprogress > numExpired) ? minprogress - numExpired : 0;

  // If we are still under pressure, purge LFU entries until we aren't.
  if (mMemorySize > memoryLimit) {
    // Do not enter PurgeByFrecency if we reached the minimum and are asked to
    // deliver entries.
    if (minprogress == 0 && CacheIOThread::YieldAndRerun()) {
      return;
    }

    auto r = PurgeByFrecency(minprogress);
    if (MOZ_LIKELY(r.isOk())) {
      size_t numPurged = r.unwrap();
      LOG((
          "  memory data consumption over the limit, abandoned %zu LFU entries",
          numPurged));
    } else {
      // If we hit an error (OOM), do an emergency PurgeAll.
      size_t numPurged = PurgeAll(CacheEntry::PURGE_WHOLE, minprogress);
      LOG(
          ("  memory data consumption over the limit, emergency purged all %zu "
           "entries",
           numPurged));
    }
  }

  LOG(("  purging took %1.2fms", (TimeStamp::Now() - start).ToMilliseconds()));
}

// This function purges ALL expired entries.
size_t CacheStorageService::MemoryPool::PurgeExpired(size_t minprogress) {
  MOZ_ASSERT(IsOnManagementThread());

  uint32_t now = NowInSeconds();

  size_t numPurged = 0;
  // Scan for items to purge. mManagedEntries is not sorted but comparing just
  // one integer should be faster than anything else, so go scan.
  RefPtr<CacheEntry> entry = mManagedEntries.getFirst();
  while (entry) {
    // Get the next entry before we may be removed from our list.
    RefPtr<CacheEntry> nextEntry = entry->getNext();

    if (entry->GetExpirationTime() <= now) {
      // Purge will modify our mManagedEntries list but we are prepared for it.
      if (entry->Purge(CacheEntry::PURGE_WHOLE)) {
        numPurged++;
        LOG(("  purged expired, entry=%p, exptime=%u (now=%u)", entry.get(),
             entry->GetExpirationTime(), now));
      }
    }

    entry = std::move(nextEntry);

    // To have some progress even under load, we do the check only after
    // purging at least minprogress items if under pressure.
    if ((numPurged >= minprogress || mMemorySize <= Limit()) &&
        CacheIOThread::YieldAndRerun()) {
      break;
    }
  }

  return numPurged;
}

Result<size_t, nsresult> CacheStorageService::MemoryPool::PurgeByFrecency(
    size_t minprogress) {
  MOZ_ASSERT(IsOnManagementThread());

  // Pretend the limit is 10% lower so that we get rid of more entries at one
  // shot and save the sorting below.
  uint32_t const memoryLimit = (uint32_t)(Limit() * 0.9);
  if (mMemorySize <= memoryLimit) {
    return 0;
  }

  LOG(("MemoryPool::PurgeByFrecency, len=%zu", mManagedEntries.length()));

  // We want to have an array snapshot for sorting and iterating.
  struct mayPurgeEntry {
    RefPtr<CacheEntry> mEntry;
    double mFrecency;

    explicit mayPurgeEntry(CacheEntry* aEntry) {
      mEntry = aEntry;
      mFrecency = aEntry->GetFrecency();
    }

    bool operator<(const mayPurgeEntry& aOther) const {
      return mFrecency < aOther.mFrecency;
    }
  };

  nsTArray<mayPurgeEntry> mayPurgeSorted;
  if (!mayPurgeSorted.SetCapacity(mManagedEntries.length(),
                                  mozilla::fallible)) {
    return Err(NS_ERROR_OUT_OF_MEMORY);
  }
  {
    StaticMutexAutoLock lock(CacheStorageService::Self()->Lock());

    for (const auto& entry : mManagedEntries) {
      // Referenced items cannot be purged and we deliberately want to not
      // look at '0' frecency entries, these are new entries and can be
      // ignored.  Also, any dict: (CompressionDictionary) entries for an
      // origin should not be purged unless empty - they will empty out as
      // the cache entries referenced by them are purged until they are empty.
      if (!entry->IsReferenced() && entry->GetFrecency() > 0.0 &&
          (!entry->GetEnhanceID().EqualsLiteral("dict:") ||
           entry->GetMetadataMemoryConsumption() == 0)) {
        mayPurgeEntry copy(entry);
        mayPurgeSorted.AppendElement(std::move(copy));
      } else {
        if (entry->GetEnhanceID().EqualsLiteral("dict:")) {
          LOG(
              ("*** Ignored Entry is a dictionary origin, metadata size %d, "
               "referenced %d, Frecency %f",
               entry->GetMetadataMemoryConsumption(), entry->IsReferenced(),
               entry->GetFrecency()));
        }
      }
    }
  }
  if (mayPurgeSorted.Length() == 0) {
    return 0;
  }
  mayPurgeSorted.Sort();

  size_t numPurged = 0;

  for (auto& checkPurge : mayPurgeSorted) {
    if (mMemorySize <= memoryLimit) {
      break;
    }

    // Ensure it's deleted immediately if purged so we can record the
    // mMemorySize savings
    RefPtr<CacheEntry> entry = std::move(checkPurge.mEntry);

    if (entry->Purge(CacheEntry::PURGE_WHOLE)) {
      numPurged++;
      LOG(("  abandoned (%d), entry=%p, frecency=%1.10f",
           CacheEntry::PURGE_WHOLE, entry.get(), entry->GetFrecency()));
    }

    if (numPurged >= minprogress && CacheIOThread::YieldAndRerun()) {
      LOG(("MemoryPool::PurgeByFrecency interrupted"));
      return numPurged;
    }
  }

  LOG(
      ("MemoryPool::PurgeByFrecency done, purged %zu - mMemorySize %u, "
       "memoryLimit %u",
       numPurged, (uint32_t)mMemorySize, memoryLimit));

  return numPurged;
}

size_t CacheStorageService::MemoryPool::PurgeAll(uint32_t aWhat,
                                                 size_t minprogress) {
  LOG(("CacheStorageService::MemoryPool::PurgeAll aWhat=%d", aWhat));
  MOZ_ASSERT(IsOnManagementThread());

  size_t numPurged = 0;

  RefPtr<CacheEntry> entry = mManagedEntries.getFirst();
  while (entry) {
    if (numPurged >= minprogress && CacheIOThread::YieldAndRerun()) break;

    // Get the next entry before we may be removed from our list.
    RefPtr<CacheEntry> nextEntry = entry->getNext();

    if (entry->Purge(aWhat)) {
      numPurged++;
      LOG(("  abandoned entry=%p", entry.get()));
    }

    entry = std::move(nextEntry);
  }

  return numPurged;
}

// Methods exposed to and used by CacheStorage.

nsresult CacheStorageService::AddStorageEntry(CacheStorage const* aStorage,
                                              const nsACString& aURI,
                                              const nsACString& aIdExtension,
                                              uint32_t aFlags,
                                              CacheEntryHandle** aResult) {
  NS_ENSURE_FALSE(mShutdown, NS_ERROR_NOT_INITIALIZED);

  NS_ENSURE_ARG(aStorage);

  nsAutoCString contextKey;
  CacheFileUtils::AppendKeyPrefix(aStorage->LoadInfo(), contextKey);

  return AddStorageEntry(contextKey, aURI, aIdExtension,
                         aStorage->WriteToDisk(), aStorage->SkipSizeCheck(),
                         aStorage->Pinning(), aFlags, aResult);
}

nsresult CacheStorageService::AddStorageEntry(
    const nsACString& aContextKey, const nsACString& aURI,
    const nsACString& aIdExtension, bool aWriteToDisk, bool aSkipSizeCheck,
    bool aPin, uint32_t aFlags, CacheEntryHandle** aResult) {
  nsresult rv;

  nsAutoCString entryKey;
  rv = CacheEntry::HashingKey(""_ns, aIdExtension, aURI, entryKey);
  NS_ENSURE_SUCCESS(rv, rv);

  LOG(("CacheStorageService::AddStorageEntry [entryKey=%s, contextKey=%s]",
       entryKey.get(), PromiseFlatCString(aContextKey).get()));

  RefPtr<CacheEntry> entry;
  RefPtr<CacheEntryHandle> handle;

  {
    StaticMutexAutoLock lock(sLock);

    NS_ENSURE_FALSE(mShutdown, NS_ERROR_NOT_INITIALIZED);

    // Ensure storage table
    CacheEntryTable* const entries =
        sGlobalEntryTables
            ->LookupOrInsertWith(
                aContextKey,
                [&aContextKey] {
                  LOG(("  new storage entries table for context '%s'",
                       PromiseFlatCString(aContextKey).get()));
                  return MakeUnique<CacheEntryTable>(
                      CacheEntryTable::ALL_ENTRIES);
                })
            .get();

    bool entryExists = entries->Get(entryKey, getter_AddRefs(entry));
    if (!entryExists && (aFlags & nsICacheStorage::OPEN_READONLY) &&
        (aFlags & nsICacheStorage::OPEN_SECRETLY) &&
        StaticPrefs::network_cache_bug1708673()) {
      return NS_ERROR_CACHE_KEY_NOT_FOUND;
    }
    if (entryExists && (aFlags & nsICacheStorage::OPEN_COMPLETE_ONLY)) {
      bool ready = false;
      // We're looking for complete files, even if they're being revalidated
      // (for dictionaries)
      entry->GetReadyOrRevalidating(&ready);
      if (!ready) {
        return NS_ERROR_CACHE_KEY_NOT_FOUND;
      }
    }

    bool replace = aFlags & nsICacheStorage::OPEN_TRUNCATE;

    if (entryExists && !replace) {
      // check whether we want to turn this entry to a memory-only.
      if (MOZ_UNLIKELY(!aWriteToDisk) && MOZ_LIKELY(entry->IsUsingDisk())) {
        LOG(("  entry is persistent but we want mem-only, replacing it"));
        replace = true;
      }
    }

    // If truncate is demanded, delete and doom the current entry
    if (entryExists && replace) {
      entries->Remove(entryKey);

      LOG(("  dooming entry %p for %s because of OPEN_TRUNCATE", entry.get(),
           entryKey.get()));
      // On purpose called under the lock to prevent races of doom and open on
      // I/O thread No need to remove from both memory-only and all-entries
      // tables.  The new entry will overwrite the shadow entry in its ctor.
      entry->DoomAlreadyRemoved();

      entry = nullptr;
      entryExists = false;

      // Would only lead to deleting force-valid timestamp again.  We don't need
      // the replace information anymore after this point anyway.
      replace = false;
    }

    // Ensure entry for the particular URL
    if (!entryExists) {
      // When replacing with a new entry, always remove the current force-valid
      // timestamp, this is the only place to do it.
      if (replace) {
        RemoveEntryForceValid(aContextKey, entryKey);
      }

      // Entry is not in the hashtable or has just been truncated...
      entry = new CacheEntry(aContextKey, aURI, aIdExtension, aWriteToDisk,
                             aSkipSizeCheck, aPin);
      entries->InsertOrUpdate(entryKey, RefPtr{entry});
      LOG(("  new entry %p for %s", entry.get(), entryKey.get()));
    }

    if (entry) {
      // Here, if this entry was not for a long time referenced by any consumer,
      // gets again first 'handles count' reference.
      handle = entry->NewHandle();
    }
  }

  handle.forget(aResult);
  return NS_OK;
}

nsresult CacheStorageService::CheckStorageEntry(CacheStorage const* aStorage,
                                                const nsACString& aURI,
                                                const nsACString& aIdExtension,
                                                bool* aResult) {
  nsresult rv;

  nsAutoCString contextKey;
  CacheFileUtils::AppendKeyPrefix(aStorage->LoadInfo(), contextKey);

  if (!aStorage->WriteToDisk()) {
    AppendMemoryStorageTag(contextKey);
  }

  LOG(("CacheStorageService::CheckStorageEntry [uri=%s, eid=%s, contextKey=%s]",
       PromiseFlatCString(aURI).get(), PromiseFlatCString(aIdExtension).get(),
       contextKey.get()));

  {
    StaticMutexAutoLock lock(sLock);

    NS_ENSURE_FALSE(mShutdown, NS_ERROR_NOT_INITIALIZED);

    nsAutoCString entryKey;
    rv = CacheEntry::HashingKey(""_ns, aIdExtension, aURI, entryKey);
    NS_ENSURE_SUCCESS(rv, rv);

    CacheEntryTable* entries;
    if ((*aResult = sGlobalEntryTables->Get(contextKey, &entries)) &&
        entries->GetWeak(entryKey, aResult)) {
      LOG(("  found in hash tables"));
      return NS_OK;
    }
  }

  if (!aStorage->WriteToDisk()) {
    // Memory entry, nothing more to do.
    LOG(("  not found in hash tables"));
    return NS_OK;
  }

  // Disk entry, not found in the hashtable, check the index.
  nsAutoCString fileKey;
  rv = CacheEntry::HashingKey(contextKey, aIdExtension, aURI, fileKey);

  CacheIndex::EntryStatus status;
  rv = CacheIndex::HasEntry(fileKey, &status);
  if (NS_FAILED(rv) || status == CacheIndex::DO_NOT_KNOW) {
    LOG(("  index doesn't know, rv=0x%08" PRIx32, static_cast<uint32_t>(rv)));
    return NS_ERROR_NOT_AVAILABLE;
  }

  *aResult = status == CacheIndex::EXISTS;
  LOG(("  %sfound in index", *aResult ? "" : "not "));
  return NS_OK;
}

nsresult CacheStorageService::GetCacheIndexEntryAttrs(
    CacheStorage const* aStorage, const nsACString& aURI,
    const nsACString& aIdExtension, bool* aHasAltData, uint32_t* aFileSizeKb) {
  nsresult rv;

  nsAutoCString contextKey;
  CacheFileUtils::AppendKeyPrefix(aStorage->LoadInfo(), contextKey);

  LOG(
      ("CacheStorageService::GetCacheIndexEntryAttrs [uri=%s, eid=%s, "
       "contextKey=%s]",
       PromiseFlatCString(aURI).get(), PromiseFlatCString(aIdExtension).get(),
       contextKey.get()));

  nsAutoCString fileKey;
  rv = CacheEntry::HashingKey(contextKey, aIdExtension, aURI, fileKey);
  if (NS_FAILED(rv)) {
    return rv;
  }

  *aHasAltData = false;
  *aFileSizeKb = 0;
  auto closure = [&aHasAltData, &aFileSizeKb](const CacheIndexEntry* entry) {
    *aHasAltData = entry->GetHasAltData();
    *aFileSizeKb = entry->GetFileSize();
  };

  CacheIndex::EntryStatus status;
  rv = CacheIndex::HasEntry(fileKey, &status, closure);
  if (NS_FAILED(rv)) {
    return rv;
  }

  if (status != CacheIndex::EXISTS) {
    return NS_ERROR_CACHE_KEY_NOT_FOUND;
  }

  return NS_OK;
}

namespace {

class CacheEntryDoomByKeyCallback : public CacheFileIOListener,
                                    public nsIRunnable {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIRUNNABLE

  explicit CacheEntryDoomByKeyCallback(nsICacheEntryDoomCallback* aCallback)
      : mCallback(aCallback), mResult(NS_ERROR_NOT_INITIALIZED) {}

 private:
  virtual ~CacheEntryDoomByKeyCallback();

  NS_IMETHOD OnFileOpened(CacheFileHandle* aHandle, nsresult aResult) override {
    return NS_OK;
  }
  NS_IMETHOD OnDataWritten(CacheFileHandle* aHandle, const char* aBuf,
                           nsresult aResult) override {
    return NS_OK;
  }
  NS_IMETHOD OnDataRead(CacheFileHandle* aHandle, char* aBuf,
                        nsresult aResult) override {
    return NS_OK;
  }
  NS_IMETHOD OnFileDoomed(CacheFileHandle* aHandle, nsresult aResult) override;
  NS_IMETHOD OnEOFSet(CacheFileHandle* aHandle, nsresult aResult) override {
    return NS_OK;
  }
  NS_IMETHOD OnFileRenamed(CacheFileHandle* aHandle,
                           nsresult aResult) override {
    return NS_OK;
  }

  nsCOMPtr<nsICacheEntryDoomCallback> mCallback;
  nsresult mResult;
};

CacheEntryDoomByKeyCallback::~CacheEntryDoomByKeyCallback() {
  if (mCallback) {
    ProxyReleaseMainThread("CacheEntryDoomByKeyCallback::mCallback", mCallback);
  }
}

NS_IMETHODIMP CacheEntryDoomByKeyCallback::OnFileDoomed(
    CacheFileHandle* aHandle, nsresult aResult) {
  if (!mCallback) return NS_OK;

  mResult = aResult;
  if (NS_IsMainThread()) {
    Run();
  } else {
    NS_DispatchToMainThread(this);
  }

  return NS_OK;
}

NS_IMETHODIMP CacheEntryDoomByKeyCallback::Run() {
  mCallback->OnCacheEntryDoomed(mResult);
  return NS_OK;
}

NS_IMPL_ISUPPORTS(CacheEntryDoomByKeyCallback, CacheFileIOListener,
                  nsIRunnable);

}  // namespace

nsresult CacheStorageService::DoomStorageEntry(
    CacheStorage const* aStorage, const nsACString& aURI,
    const nsACString& aIdExtension, nsICacheEntryDoomCallback* aCallback) {
  LOG(("CacheStorageService::DoomStorageEntry %s",
       PromiseFlatCString(aURI).get()));

  NS_ENSURE_ARG(aStorage);

  nsAutoCString contextKey;
  CacheFileUtils::AppendKeyPrefix(aStorage->LoadInfo(), contextKey);

  nsAutoCString entryKey;
  nsresult rv = CacheEntry::HashingKey(""_ns, aIdExtension, aURI, entryKey);
  NS_ENSURE_SUCCESS(rv, rv);

  RefPtr<CacheEntry> entry;
  {
    StaticMutexAutoLock lock(sLock);

    NS_ENSURE_FALSE(mShutdown, NS_ERROR_NOT_INITIALIZED);

    CacheEntryTable* entries;
    if (sGlobalEntryTables->Get(contextKey, &entries)) {
      if (entries->Get(entryKey, getter_AddRefs(entry))) {
        if (aStorage->WriteToDisk() || !entry->IsUsingDisk()) {
          // When evicting from disk storage, purge
          // When evicting from memory storage and the entry is memory-only,
          // purge
          LOG(
              ("  purging entry %p for %s [storage use disk=%d, entry use "
               "disk=%d]",
               entry.get(), entryKey.get(), aStorage->WriteToDisk(),
               entry->IsUsingDisk()));
          entries->Remove(entryKey);
        } else {
          // Otherwise, leave it
          LOG(
              ("  leaving entry %p for %s [storage use disk=%d, entry use "
               "disk=%d]",
               entry.get(), entryKey.get(), aStorage->WriteToDisk(),
               entry->IsUsingDisk()));
          entry = nullptr;
        }
      }
    }

    if (!entry) {
      RemoveEntryForceValid(contextKey, entryKey);
    }
  }

  if (entry) {
    LOG(("  dooming entry %p for %s", entry.get(), entryKey.get()));
    return entry->AsyncDoom(aCallback);
  }

  LOG(("  no entry loaded for %s", entryKey.get()));

  if (aStorage->WriteToDisk()) {
    nsAutoCString contextKey;
    CacheFileUtils::AppendKeyPrefix(aStorage->LoadInfo(), contextKey);

    rv = CacheEntry::HashingKey(contextKey, aIdExtension, aURI, entryKey);
    NS_ENSURE_SUCCESS(rv, rv);

    LOG(("  dooming file only for %s", entryKey.get()));

    RefPtr<CacheEntryDoomByKeyCallback> callback(
        new CacheEntryDoomByKeyCallback(aCallback));
    rv = CacheFileIOManager::DoomFileByKey(entryKey, callback);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

  class Callback : public Runnable {
   public:
    explicit Callback(nsICacheEntryDoomCallback* aCallback)
        : mozilla::Runnable("Callback"), mCallback(aCallback) {}
    NS_IMETHOD Run() override {
      mCallback->OnCacheEntryDoomed(NS_ERROR_NOT_AVAILABLE);
      return NS_OK;
    }
    nsCOMPtr<nsICacheEntryDoomCallback> mCallback;
  };

  if (aCallback) {
    RefPtr<Runnable> callback = new Callback(aCallback);
    return NS_DispatchToMainThread(callback);
  }

  return NS_OK;
}

nsresult CacheStorageService::DoomStorageEntries(
    CacheStorage const* aStorage, nsICacheEntryDoomCallback* aCallback) {
  LOG(("CacheStorageService::DoomStorageEntries"));

  NS_ENSURE_FALSE(mShutdown, NS_ERROR_NOT_INITIALIZED);
  NS_ENSURE_ARG(aStorage);

  nsAutoCString contextKey;
  CacheFileUtils::AppendKeyPrefix(aStorage->LoadInfo(), contextKey);

  StaticMutexAutoLock lock(sLock);

  return DoomStorageEntries(contextKey, aStorage->LoadInfo(),
                            aStorage->WriteToDisk(), aStorage->Pinning(),
                            aCallback);
}

nsresult CacheStorageService::DoomStorageEntries(
    const nsACString& aContextKey, nsILoadContextInfo* aContext,
    bool aDiskStorage, bool aPinned, nsICacheEntryDoomCallback* aCallback) {
  LOG(("CacheStorageService::DoomStorageEntries [context=%s]",
       PromiseFlatCString(aContextKey).get()));

  sLock.AssertCurrentThreadOwns();

  NS_ENSURE_TRUE(!mShutdown, NS_ERROR_NOT_INITIALIZED);

  nsAutoCString memoryStorageID(aContextKey);
  AppendMemoryStorageTag(memoryStorageID);

  if (aDiskStorage) {
    LOG(("  dooming disk+memory storage of %s",
         PromiseFlatCString(aContextKey).get()));

    // Walk one by one and remove entries according their pin status
    CacheEntryTable *diskEntries, *memoryEntries;
    if (sGlobalEntryTables->Get(aContextKey, &diskEntries)) {
      sGlobalEntryTables->Get(memoryStorageID, &memoryEntries);

      for (auto iter = diskEntries->Iter(); !iter.Done(); iter.Next()) {
        auto entry = iter.Data();
        if (entry->DeferOrBypassRemovalOnPinStatus(aPinned)) {
          continue;
        }

        if (memoryEntries) {
          RemoveExactEntry(memoryEntries, iter.Key(), entry, false);
        }
        iter.Remove();
      }
    }

    if (aContext && !aContext->IsPrivate()) {
      LOG(("  dooming disk entries"));
      CacheFileIOManager::EvictByContext(aContext, aPinned, u""_ns);
    }
  } else {
    LOG(("  dooming memory-only storage of %s",
         PromiseFlatCString(aContextKey).get()));

    // Remove the memory entries table from the global tables.
    // Since we store memory entries also in the disk entries table
    // we need to remove the memory entries from the disk table one
    // by one manually.
    mozilla::UniquePtr<CacheEntryTable> memoryEntries;
    sGlobalEntryTables->Remove(memoryStorageID, &memoryEntries);

    CacheEntryTable* diskEntries;
    if (memoryEntries && sGlobalEntryTables->Get(aContextKey, &diskEntries)) {
      for (const auto& memoryEntry : *memoryEntries) {
        const auto& entry = memoryEntry.GetData();
        RemoveExactEntry(diskEntries, memoryEntry.GetKey(), entry, false);
      }
    }
  }

  {
    mozilla::MutexAutoLock lock(mForcedValidEntriesLock);

    if (aContext) {
      for (auto iter = mForcedValidEntries.Iter(); !iter.Done(); iter.Next()) {
        bool matches;
        DebugOnly<nsresult> rv = CacheFileUtils::KeyMatchesLoadContextInfo(
            iter.Key(), aContext, &matches);
        MOZ_ASSERT(NS_SUCCEEDED(rv));

        if (matches) {
          iter.Remove();
        }
      }
    } else {
      mForcedValidEntries.Clear();
    }
  }

  // An artificial callback.  This is a candidate for removal tho.  In the new
  // cache any 'doom' or 'evict' function ensures that the entry or entries
  // being doomed is/are not accessible after the function returns.  So there is
  // probably no need for a callback - has no meaning.  But for compatibility
  // with the old cache that is still in the tree we keep the API similar to be
  // able to make tests as well as other consumers work for now.
  class Callback : public Runnable {
   public:
    explicit Callback(nsICacheEntryDoomCallback* aCallback)
        : mozilla::Runnable("Callback"), mCallback(aCallback) {}
    NS_IMETHOD Run() override {
      mCallback->OnCacheEntryDoomed(NS_OK);
      return NS_OK;
    }
    nsCOMPtr<nsICacheEntryDoomCallback> mCallback;
  };

  if (aCallback) {
    RefPtr<Runnable> callback = new Callback(aCallback);
    return NS_DispatchToMainThread(callback);
  }

  return NS_OK;
}

nsresult CacheStorageService::WalkStorageEntries(
    CacheStorage const* aStorage, bool aVisitEntries,
    nsICacheStorageVisitor* aVisitor) {
  LOG(("CacheStorageService::WalkStorageEntries [cb=%p, visitentries=%d]",
       aVisitor, aVisitEntries));
  NS_ENSURE_FALSE(mShutdown, NS_ERROR_NOT_INITIALIZED);

  NS_ENSURE_ARG(aStorage);

  if (aStorage->WriteToDisk()) {
    RefPtr<CacheStorageServiceInternal::WalkDiskCacheRunnable> event =
        new CacheStorageServiceInternal::WalkDiskCacheRunnable(
            aStorage->LoadInfo(), aVisitEntries, aVisitor);
    return event->Walk();
  }

  RefPtr<CacheStorageServiceInternal::WalkMemoryCacheRunnable> event =
      new CacheStorageServiceInternal::WalkMemoryCacheRunnable(
          aStorage->LoadInfo(), aVisitEntries, aVisitor);
  return event->Walk();
}

void CacheStorageService::CacheFileDoomed(const nsACString& aKey,
                                          nsILoadContextInfo* aLoadContextInfo,
                                          const nsACString& aIdExtension,
                                          const nsACString& aURISpec) {
  nsAutoCString contextKey;
  CacheFileUtils::AppendKeyPrefix(aLoadContextInfo, contextKey);

  nsAutoCString entryKey;
  CacheEntry::HashingKey(""_ns, aIdExtension, aURISpec, entryKey);

  StaticMutexAutoLock lock(sLock);

  if (mShutdown) {
    return;
  }

  CacheEntryTable* entries;
  RefPtr<CacheEntry> entry;

  if (sGlobalEntryTables->Get(contextKey, &entries) &&
      entries->Get(entryKey, getter_AddRefs(entry))) {
    if (entry->IsFileDoomed()) {
      // Need to remove under the lock to avoid possible race leading
      // to duplication of the entry per its key.
      RemoveExactEntry(entries, entryKey, entry, false);
      entry->DoomAlreadyRemoved();
    }

    // Entry found, but it's not the entry that has been found doomed
    // by the lower eviction layer.  Just leave everything unchanged.
    return;
  }

  RemoveEntryForceValid(contextKey, entryKey);
}

bool CacheStorageService::GetCacheEntryInfo(
    nsILoadContextInfo* aLoadContextInfo, const nsACString& aIdExtension,
    const nsACString& aURISpec, EntryInfoCallback* aCallback) {
  nsAutoCString contextKey;
  CacheFileUtils::AppendKeyPrefix(aLoadContextInfo, contextKey);

  nsAutoCString entryKey;
  CacheEntry::HashingKey(""_ns, aIdExtension, aURISpec, entryKey);

  RefPtr<CacheEntry> entry;
  {
    StaticMutexAutoLock lock(sLock);

    if (mShutdown) {
      return false;
    }

    CacheEntryTable* entries;
    if (!sGlobalEntryTables->Get(contextKey, &entries)) {
      return false;
    }

    if (!entries->Get(entryKey, getter_AddRefs(entry))) {
      return false;
    }
  }

  GetCacheEntryInfo(entry, aCallback);
  return true;
}

// static
void CacheStorageService::GetCacheEntryInfo(CacheEntry* aEntry,
                                            EntryInfoCallback* aCallback) {
  nsCString const uriSpec = aEntry->GetURI();
  nsCString const enhanceId = aEntry->GetEnhanceID();

  nsAutoCString entryKey;
  aEntry->HashingKeyWithStorage(entryKey);

  nsCOMPtr<nsILoadContextInfo> info = CacheFileUtils::ParseKey(entryKey);

  uint32_t dataSize;
  if (NS_FAILED(aEntry->GetStorageDataSize(&dataSize))) {
    dataSize = 0;
  }
  int64_t altDataSize;
  if (NS_FAILED(aEntry->GetAltDataSize(&altDataSize))) {
    altDataSize = 0;
  }
  uint32_t fetchCount;
  if (NS_FAILED(aEntry->GetFetchCount(&fetchCount))) {
    fetchCount = 0;
  }
  uint32_t lastModified;
  if (NS_FAILED(aEntry->GetLastModified(&lastModified))) {
    lastModified = 0;
  }
  uint32_t expirationTime;
  if (NS_FAILED(aEntry->GetExpirationTime(&expirationTime))) {
    expirationTime = 0;
  }

  aCallback->OnEntryInfo(uriSpec, enhanceId, dataSize, altDataSize, fetchCount,
                         lastModified, expirationTime, aEntry->IsPinned(),
                         info);
}

// static
uint32_t CacheStorageService::CacheQueueSize(bool highPriority) {
  RefPtr<CacheIOThread> thread = CacheFileIOManager::IOThread();
  // The thread will be null at shutdown.
  if (!thread) {
    return 0;
  }
  return thread->QueueSize(highPriority);
}

// Telemetry collection

namespace {

bool TelemetryEntryKey(CacheEntry const* entry, nsAutoCString& key) {
  nsAutoCString entryKey;
  nsresult rv = entry->HashingKey(entryKey);
  if (NS_FAILED(rv)) return false;

  if (entry->GetStorageID().IsEmpty()) {
    // Hopefully this will be const-copied, saves some memory
    key = entryKey;
  } else {
    key.Assign(entry->GetStorageID());
    key.Append(':');
    key.Append(entryKey);
  }

  return true;
}

}  // namespace

void CacheStorageService::TelemetryPrune(TimeStamp& now) {
  static TimeDuration const oneMinute = TimeDuration::FromSeconds(60);
  static TimeStamp dontPruneUntil = now + oneMinute;
  if (now < dontPruneUntil) return;

  static TimeDuration const fifteenMinutes = TimeDuration::FromSeconds(900);
  for (auto iter = mPurgeTimeStamps.Iter(); !iter.Done(); iter.Next()) {
    if (now - iter.Data() > fifteenMinutes) {
      // We are not interested in resurrection of entries after 15 minutes
      // of time.  This is also the limit for the telemetry.
      iter.Remove();
    }
  }
  dontPruneUntil = now + oneMinute;
}

void CacheStorageService::TelemetryRecordEntryCreation(
    CacheEntry const* entry) {
  MOZ_ASSERT(CacheStorageService::IsOnManagementThread());

  nsAutoCString key;
  if (!TelemetryEntryKey(entry, key)) return;

  TimeStamp now = TimeStamp::NowLoRes();
  TelemetryPrune(now);

  // When an entry is craeted (registered actually) we check if there is
  // a timestamp marked when this very same cache entry has been removed
  // (deregistered) because of over-memory-limit purging.  If there is such
  // a timestamp found accumulate telemetry on how long the entry was away.
  TimeStamp timeStamp;
  if (!mPurgeTimeStamps.Get(key, &timeStamp)) return;

  mPurgeTimeStamps.Remove(key);

  glean::network::http_cache_entry_reload_time.AccumulateRawDuration(
      TimeStamp::NowLoRes() - timeStamp);
}

void CacheStorageService::TelemetryRecordEntryRemoval(CacheEntry* entry) {
  MOZ_ASSERT(CacheStorageService::IsOnManagementThread());

  // Doomed entries must not be considered, we are only interested in purged
  // entries.  Note that the mIsDoomed flag is always set before deregistration
  // happens.
  if (entry->IsDoomed()) return;

  nsAutoCString key;
  if (!TelemetryEntryKey(entry, key)) return;

  // When an entry is removed (deregistered actually) we put a timestamp for
  // this entry to the hashtable so that when the entry is created (registered)
  // again we know how long it was away.  Also accumulate number of AsyncOpen
  // calls on the entry, this tells us how efficiently the pool actually works.

  TimeStamp now = TimeStamp::NowLoRes();
  TelemetryPrune(now);
  mPurgeTimeStamps.InsertOrUpdate(key, now);

  glean::network::http_cache_entry_reuse_count.AccumulateSingleSample(
      entry->UseCount());
  if (Telemetry::CanRecordPrereleaseData()) {
    glean::network::http_cache_entry_alive_time.AccumulateRawDuration(
        TimeStamp::NowLoRes() - entry->LoadStart());
  }
}

// nsIMemoryReporter

size_t CacheStorageService::SizeOfExcludingThis(
    mozilla::MallocSizeOf mallocSizeOf) const {
  sLock.AssertCurrentThreadOwns();

  size_t n = 0;
  // The elemets are referenced by sGlobalEntryTables and are reported from
  // there.

  // Entries reported manually in CacheStorageService::CollectReports callback
  if (sGlobalEntryTables) {
    n += sGlobalEntryTables->ShallowSizeOfIncludingThis(mallocSizeOf);
  }
  n += mPurgeTimeStamps.SizeOfExcludingThis(mallocSizeOf);

  return n;
}

size_t CacheStorageService::SizeOfIncludingThis(
    mozilla::MallocSizeOf mallocSizeOf) const {
  return mallocSizeOf(this) + SizeOfExcludingThis(mallocSizeOf);
}

NS_IMETHODIMP
CacheStorageService::CollectReports(nsIHandleReportCallback* aHandleReport,
                                    nsISupports* aData, bool aAnonymize) {
  StaticMutexAutoLock lock(sLock);
  MOZ_COLLECT_REPORT("explicit/network/cache2/io", KIND_HEAP, UNITS_BYTES,
                     CacheFileIOManager::SizeOfIncludingThis(MallocSizeOf),
                     "Memory used by the cache IO manager.");

  MOZ_COLLECT_REPORT("explicit/network/cache2/index", KIND_HEAP, UNITS_BYTES,
                     CacheIndex::SizeOfIncludingThis(MallocSizeOf),
                     "Memory used by the cache index.");

  // Report the service instance, this doesn't report entries, done lower
  MOZ_COLLECT_REPORT("explicit/network/cache2/service", KIND_HEAP, UNITS_BYTES,
                     SizeOfIncludingThis(MallocSizeOf),
                     "Memory used by the cache storage service.");

  // Report all entries, each storage separately (by the context key)
  //
  // References are:
  // sGlobalEntryTables to N CacheEntryTable
  // CacheEntryTable to N CacheEntry
  // CacheEntry to 1 CacheFile
  // CacheFile to
  //   N CacheFileChunk (keeping the actual data)
  //   1 CacheFileMetadata (keeping http headers etc.)
  //   1 CacheFileOutputStream
  //   N CacheFileInputStream
  if (sGlobalEntryTables) {
    for (const auto& globalEntry : *sGlobalEntryTables) {
      CacheStorageService::Self()->Lock().AssertCurrentThreadOwns();

      CacheEntryTable* table = globalEntry.GetWeak();

      size_t size = 0;
      mozilla::MallocSizeOf mallocSizeOf = CacheStorageService::MallocSizeOf;

      size += table->ShallowSizeOfIncludingThis(mallocSizeOf);
      for (const auto& tableEntry : *table) {
        size += tableEntry.GetKey().SizeOfExcludingThisIfUnshared(mallocSizeOf);

        // Bypass memory-only entries, those will be reported when iterating the
        // memory only table. Memory-only entries are stored in both ALL_ENTRIES
        // and MEMORY_ONLY hashtables.
        RefPtr<mozilla::net::CacheEntry> const& entry = tableEntry.GetData();
        if (table->Type() == CacheEntryTable::MEMORY_ONLY ||
            entry->IsUsingDisk()) {
          size += entry->SizeOfIncludingThis(mallocSizeOf);
        }
      }

      aHandleReport->Callback(
          ""_ns,
          nsPrintfCString(
              "explicit/network/cache2/%s-storage(%s)",
              table->Type() == CacheEntryTable::MEMORY_ONLY ? "memory" : "disk",
              aAnonymize ? "<anonymized>"
                         : PromiseFlatCString(globalEntry.GetKey()).get()),
          nsIMemoryReporter::KIND_HEAP, nsIMemoryReporter::UNITS_BYTES, size,
          "Memory used by the cache storage."_ns, aData);
    }
  }

  return NS_OK;
}

// nsICacheTesting

NS_IMETHODIMP
CacheStorageService::IOThreadSuspender::Run() {
  MonitorAutoLock mon(mMon);
  while (!mSignaled) {
    mon.Wait();
  }
  return NS_OK;
}

void CacheStorageService::IOThreadSuspender::Notify() {
  MonitorAutoLock mon(mMon);
  mSignaled = true;
  mon.Notify();
}

NS_IMETHODIMP
CacheStorageService::SuspendCacheIOThread(uint32_t aLevel) {
  RefPtr<CacheIOThread> thread = CacheFileIOManager::IOThread();
  if (!thread) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  MOZ_ASSERT(!mActiveIOSuspender);
  mActiveIOSuspender = new IOThreadSuspender();
  return thread->Dispatch(mActiveIOSuspender, aLevel);
}

NS_IMETHODIMP
CacheStorageService::ResumeCacheIOThread() {
  MOZ_ASSERT(mActiveIOSuspender);

  RefPtr<IOThreadSuspender> suspender;
  suspender.swap(mActiveIOSuspender);
  suspender->Notify();
  return NS_OK;
}

NS_IMETHODIMP
CacheStorageService::Flush(nsIObserver* aObserver) {
  RefPtr<CacheIOThread> thread = CacheFileIOManager::IOThread();
  if (!thread) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  nsCOMPtr<nsIObserverService> observerService =
      mozilla::services::GetObserverService();
  if (!observerService) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  // Adding as weak, the consumer is responsible to keep the reference
  // until notified.
  observerService->AddObserver(aObserver, "cacheservice:purge-memory-pools",
                               false);

  // This runnable will do the purging and when done, notifies the above
  // observer. We dispatch it to the CLOSE level, so all data writes scheduled
  // up to this time will be done before this purging happens.
  RefPtr<CacheStorageService::PurgeFromMemoryRunnable> r =
      new CacheStorageService::PurgeFromMemoryRunnable(this,
                                                       CacheEntry::PURGE_WHOLE);

  return thread->Dispatch(r, CacheIOThread::WRITE);
}

NS_IMETHODIMP
CacheStorageService::ClearDictionaryCacheMemory() {
  LOG(("CacheStorageService::ClearDictionaryCacheMemory"));
  RefPtr<DictionaryCache> cache = DictionaryCache::GetInstance();
  if (cache) {
    cache->Clear();
  }
  return NS_OK;
}

NS_IMETHODIMP
CacheStorageService::CorruptDictionaryHash(const nsACString& aURI) {
  LOG(("CacheStorageService::CorruptDictionaryHash [uri=%s]",
       PromiseFlatCString(aURI).get()));
  RefPtr<DictionaryCache> cache = DictionaryCache::GetInstance();
  if (cache) {
    cache->CorruptHashForTesting(aURI);
  }
  return NS_OK;
}

NS_IMETHODIMP
CacheStorageService::ClearDictionaryDataForTesting(const nsACString& aURI) {
  LOG(("CacheStorageService::ClearDictionaryDataForTesting [uri=%s]",
       PromiseFlatCString(aURI).get()));
  RefPtr<DictionaryCache> cache = DictionaryCache::GetInstance();
  if (cache) {
    cache->ClearDictionaryDataForTesting(aURI);
  }
  return NS_OK;
}

}  // namespace mozilla::net
