/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef CacheStorageService_h_
#define CacheStorageService_h_

#include "mozilla/AtomicBitfields.h"
#include "mozilla/Atomics.h"
#include "mozilla/LinkedList.h"
#include "mozilla/Monitor.h"
#include "mozilla/Mutex.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/TimeStamp.h"
#include "nsClassHashtable.h"
#include "nsICacheStorageService.h"
#include "nsICacheTesting.h"
#include "nsIMemoryReporter.h"
#include "nsINamed.h"
#include "nsITimer.h"
#include "nsProxyRelease.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsTHashMap.h"
#include "nsThreadUtils.h"

class nsICacheEntry;
class nsIURI;
class nsICacheEntryDoomCallback;
class nsICacheStorageVisitor;
class nsIRunnable;
class nsIThread;
class nsIEventTarget;

namespace mozilla {

class OriginAttributes;

namespace net {

class CacheStorageService;
class CacheStorage;
class CacheEntry;
class CacheEntryHandle;
class CacheEntryTable;

class CacheMemoryConsumer {
 public:
  CacheMemoryConsumer() = delete;

 private:
  friend class CacheStorageService;
  // clang-format off
  MOZ_ATOMIC_BITFIELDS(mAtomicBitfields, 32, (
    (uint32_t, ReportedMemoryConsumption, 30),
    (uint32_t, Flags, 2)
  ))
  // clang-format on

 protected:
  enum {
    // No special treatment, reports always to the disk-entries pool.
    NORMAL = 0,
    // This consumer is belonging to a memory-only cache entry, used to decide
    // which of the two disk and memory pools count this consumption at.
    MEMORY_ONLY = 1 << 0,
    // Prevent reports of this consumer at all, used for disk data chunks since
    // we throw them away as soon as the entry is not used by any consumer and
    // don't want to make them wipe the whole pool out during their short life.
    DONT_REPORT = 1 << 1
  };

  explicit CacheMemoryConsumer(uint32_t aFlags);
  ~CacheMemoryConsumer() { DoMemoryReport(0); }
  void DoMemoryReport(uint32_t aCurrentSize);
};

using GlobalEntryTables = nsClassHashtable<nsCStringHashKey, CacheEntryTable>;
class WalkMemoryCacheRunnable;

namespace CacheStorageServiceInternal {
class WalkMemoryCacheRunnable;
class WalkDiskCacheRunnable;
}  // namespace CacheStorageServiceInternal

class CacheStorageService final : public nsICacheStorageService,
                                  public nsIMemoryReporter,
                                  public nsITimerCallback,
                                  public nsICacheTesting,
                                  public nsINamed {
  friend class CacheStorageServiceInternal::WalkMemoryCacheRunnable;
  friend class CacheStorageServiceInternal::WalkDiskCacheRunnable;

 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSICACHESTORAGESERVICE
  NS_DECL_NSIMEMORYREPORTER
  NS_DECL_NSITIMERCALLBACK
  NS_DECL_NSICACHETESTING
  NS_DECL_NSINAMED

  CacheStorageService();

  void Shutdown();
  void DropPrivateBrowsingEntries();

  static CacheStorageService* Self() { return sSelf; }
  static nsISupports* SelfISupports() {
    return static_cast<nsICacheStorageService*>(Self());
  }
  nsresult Dispatch(nsIRunnable* aEvent);
  static bool IsRunning() { return sSelf && !sSelf->mShutdown; }
  static bool IsOnManagementThread();
  already_AddRefed<nsIEventTarget> Thread() const;
  StaticMutex& Lock() { return sLock; }

  // Tracks entries that may be forced valid in a pruned hashtable.
  struct ForcedValidData {
    // The timestamp is computed when the entry gets inserted into the map.
    // It should never be null for an entry in the map.
    TimeStamp validUntil;
    // viewed gets set to true by a call to MarkForcedValidEntryUse()
    bool viewed = false;
  };
  nsTHashMap<nsCStringHashKey, ForcedValidData> mForcedValidEntries;
  void ForcedValidEntriesPrune(TimeStamp& now);

  // Helper thread-safe interface to pass entry info, only difference from
  // nsICacheStorageVisitor is that instead of nsIURI only the uri spec is
  // passed.
  class EntryInfoCallback {
   public:
    virtual void OnEntryInfo(const nsACString& aURISpec,
                             const nsACString& aIdEnhance, int64_t aDataSize,
                             int64_t aAltDataSize, uint32_t aFetchCount,
                             uint32_t aLastModifiedTime,
                             uint32_t aExpirationTime, bool aPinned,
                             nsILoadContextInfo* aInfo) = 0;
  };

  // Invokes OnEntryInfo for the given aEntry, synchronously.
  static void GetCacheEntryInfo(CacheEntry* aEntry,
                                EntryInfoCallback* aCallback);

  nsresult GetCacheIndexEntryAttrs(CacheStorage const* aStorage, nsIURI* aURI,
                                   const nsACString& aIdExtension,
                                   bool* aHasAltData, uint32_t* aFileSizeKb);

  static uint32_t CacheQueueSize(bool highPriority);

  // Memory reporting
  size_t SizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf) const;
  size_t SizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf) const;
  MOZ_DEFINE_MALLOC_SIZE_OF(MallocSizeOf)

  void NoteNoVarySearchEntry(const nsACString& aContextKey,
                             const nsACString& aBasePath,
                             const nsACString& aFullKey);
  void NoteNoVarySearchEntry(nsICacheEntry* aEntry, nsIURI* aURI);

 private:
  virtual ~CacheStorageService();
  void ShutdownBackground();

  /**
   * Keeps tables of entries.  There is one entries table for each distinct load
   * context type.  The distinction is based on following load context info
   * states: <isPrivate|isAnon|inIsolatedMozBrowser> which builds a mapping
   * key.
   *
   * Thread-safe to access, protected by the service mutex.
   */
  static GlobalEntryTables* sGlobalEntryTables MOZ_GUARDED_BY(sLock);

 private:
  // The following methods may only be called on the management
  // thread.
  friend class CacheEntry;

  /**
   * Registers the entry into the associated MemoryPool.
   * Holds a strong reference until it is unregistered.
   */
  void RegisterEntry(CacheEntry* aEntry);

  /**
   * Deregisters the entry from the associated MemoryPool.
   */
  void UnregisterEntry(CacheEntry* aEntry);

  /**
   * Removes the entry from the related entry hash table, if still present.
   */
  bool RemoveEntry(CacheEntry* aEntry, bool aOnlyUnreferenced = false);

  /**
   * Tells the storage service whether this entry is only to be stored in
   * memory.
   */
  void RecordMemoryOnlyEntry(CacheEntry* aEntry, bool aOnlyInMemory,
                             bool aOverwrite);

  /**
   * Sets a cache entry valid (overrides the default loading behavior by loading
   * directly from cache) for the given number of seconds
   * See nsICacheEntry.idl for more details
   */
  void ForceEntryValidFor(nsACString const& aContextKey,
                          nsACString const& aEntryKey,
                          uint32_t aSecondsToTheFuture);

  /**
   * Remove the validity info
   */
  void RemoveEntryForceValid(nsACString const& aContextKey,
                             nsACString const& aEntryKey);

  /**
   * Retrieves the status of the cache entry to see if it has been forced valid
   * (so it will loaded directly from cache without further validation)
   */
  bool IsForcedValidEntry(nsACString const& aContextKey,
                          nsACString const& aEntryKey);

  // Marks the entry as used, so we may properly report when it gets evicted
  // if the prefetched resource was used or not.
  void MarkForcedValidEntryUse(nsACString const& aContextKey,
                               nsACString const& aEntryKey);

 private:
  friend class CacheIndex;

  /**
   * CacheIndex uses this to prevent a cache entry from being prememptively
   * thrown away when forced valid
   * See nsICacheEntry.idl for more details
   */
  bool IsForcedValidEntry(nsACString const& aContextEntryKey);

 private:
  // These are helpers for telemetry monitoring of the memory pools.
  void TelemetryPrune(TimeStamp& now);
  void TelemetryRecordEntryCreation(CacheEntry const* entry);
  void TelemetryRecordEntryRemoval(CacheEntry* entry);

 private:
  // Following methods are thread safe to call.
  friend class CacheStorage;

  /**
   * Get, or create when not existing and demanded, an entry for the storage
   * and uri+id extension.
   */
  nsresult AddStorageEntry(CacheStorage const* aStorage, nsIURI* aURI,
                           const nsACString& aIdExtension, uint32_t aFlags,
                           CacheEntryHandle** aResult);

  /**
   * Check existance of an entry.  This may throw NS_ERROR_NOT_AVAILABLE
   * when the information cannot be obtained synchronously w/o blocking.
   */
  nsresult CheckStorageEntry(CacheStorage const* aStorage, nsIURI* aURI,
                             const nsACString& aIdExtension, bool* aResult);

  /**
   * Removes the entry from the related entry hash table, if still present
   * and returns it.
   */
  nsresult DoomStorageEntry(CacheStorage const* aStorage, nsIURI* aURI,
                            const nsACString& aIdExtension,
                            nsICacheEntryDoomCallback* aCallback);

  /**
   * Removes and returns entry table for the storage.
   */
  nsresult DoomStorageEntries(CacheStorage const* aStorage,
                              nsICacheEntryDoomCallback* aCallback);

  /**
   * Walk all entiries beloging to the storage.
   */
  nsresult WalkStorageEntries(CacheStorage const* aStorage, bool aVisitEntries,
                              nsICacheStorageVisitor* aVisitor);

 private:
  friend class CacheFileIOManager;

  /**
   * CacheFileIOManager uses this method to notify CacheStorageService that
   * an active entry was removed. This method is called even if the entry
   * removal was originated by CacheStorageService.  This also removes the entry
   * from the DictionaryCache.
   */
  void CacheFileDoomed(const nsACString& aKey,
                       nsILoadContextInfo* aLoadContextInfo,
                       const nsACString& aIdExtension,
                       const nsACString& aURISpec);

  /**
   * Tries to find an existing entry in the hashtables and synchronously call
   * OnCacheEntryInfo of the aVisitor callback when found.
   * @retuns
   *   true, when the entry has been found that also implies the callbacks has
   *        beem invoked
   *   false, when an entry has not been found
   */
  bool GetCacheEntryInfo(nsILoadContextInfo* aLoadContextInfo,
                         const nsACString& aIdExtension,
                         const nsACString& aURISpec,
                         EntryInfoCallback* aCallback);

 private:
  friend class CacheMemoryConsumer;

  /**
   * When memory consumption of this entry radically changes, this method
   * is called to reflect the size of allocated memory.  This call may purge
   * unspecified number of entries from memory (but not from disk).
   */
  void OnMemoryConsumptionChange(CacheMemoryConsumer* aConsumer,
                                 uint32_t aCurrentMemoryConsumption);

  /**
   * If not already pending, it schedules mPurgeTimer that fires after 1 second
   * and dispatches PurgeOverMemoryLimit().
   */
  void SchedulePurgeOverMemoryLimit();

  /**
   * Called on the management thread, removes all expired and then least used
   * entries from the memory, first from the disk pool and then from the memory
   * pool.
   */
  void PurgeExpiredOrOverMemoryLimit();

 private:
  nsresult DoomStorageEntries(const nsACString& aContextKey,
                              nsILoadContextInfo* aContext, bool aDiskStorage,
                              bool aPin, nsICacheEntryDoomCallback* aCallback);
  nsresult AddStorageEntry(const nsACString& aContextKey, nsIURI* aURI,
                           const nsACString& aIdExtension, bool aWriteToDisk,
                           bool aSkipSizeCheck, bool aPin, uint32_t aFlags,
                           CacheEntryHandle** aResult);

  nsresult ClearOriginInternal(
      const nsAString& aOrigin,
      const mozilla::OriginAttributes& aOriginAttributes, bool aAnonymous);

  static CacheStorageService* sSelf;

  static StaticMutex sLock;
  mozilla::Mutex mForcedValidEntriesLock{
      "CacheStorageService.mForcedValidEntriesLock"};

  Atomic<bool, Relaxed> mShutdown{false};

  // Accessible only on the service thread
  class MemoryPool {
   public:
    enum EType {
      DISK,
      MEMORY,
    } mType;

    explicit MemoryPool(EType aType);
    ~MemoryPool();

    MemoryPool() = delete;

    // We want to have constant O(1) for removal from this list.
    LinkedList<RefPtr<CacheEntry>> mManagedEntries;
    Atomic<uint32_t, Relaxed> mMemorySize{0};

    bool OnMemoryConsumptionChange(uint32_t aSavedMemorySize,
                                   uint32_t aCurrentMemoryConsumption);
    /**
     * Purges entries from memory based on the frecency ordered array.
     */
    void PurgeExpiredOrOverMemoryLimit();
    size_t PurgeExpired(size_t minprogress);
    Result<size_t, nsresult> PurgeByFrecency(size_t minprogress);
    size_t PurgeAll(uint32_t aWhat, size_t minprogress);

   private:
    uint32_t Limit() const;
  };

  MemoryPool mDiskPool{MemoryPool::DISK};
  MemoryPool mMemoryPool{MemoryPool::MEMORY};
  TimeStamp mLastPurgeTime;
  MemoryPool& Pool(bool aUsingDisk) {
    return aUsingDisk ? mDiskPool : mMemoryPool;
  }
  MemoryPool const& Pool(bool aUsingDisk) const {
    return aUsingDisk ? mDiskPool : mMemoryPool;
  }

  nsCOMPtr<nsITimer> mPurgeTimer;
#ifdef MOZ_TSAN
  // In OnMemoryConsumptionChange() we check whether the timer exists, but we
  // cannot grab the lock there (see comment 6 in bug 1614637) and TSan reports
  // a data race. This data race is harmless, so we use this atomic flag only in
  // TSan build to suppress it.
  Atomic<bool, Relaxed> mPurgeTimerActive{false};
#endif

  class PurgeFromMemoryRunnable : public Runnable {
   public:
    PurgeFromMemoryRunnable(CacheStorageService* aService, uint32_t aWhat)
        : Runnable("net::CacheStorageService::PurgeFromMemoryRunnable"),
          mService(aService),
          mWhat(aWhat) {}

   private:
    virtual ~PurgeFromMemoryRunnable() = default;

    NS_IMETHOD Run() override;

    RefPtr<CacheStorageService> mService;
    uint32_t mWhat;
  };

  // Used just for telemetry purposes, accessed only on the management thread.
  // Note: not included in the memory reporter, this is not expected to be huge
  // and also would be complicated to report since reporting happens on the main
  // thread but this table is manipulated on the management thread.
  nsTHashMap<nsCStringHashKey, mozilla::TimeStamp> mPurgeTimeStamps;

  // nsICacheTesting
  class IOThreadSuspender : public Runnable {
   public:
    IOThreadSuspender()
        : Runnable("net::CacheStorageService::IOThreadSuspender"),
          mMon("IOThreadSuspender") {}
    void Notify();

   private:
    virtual ~IOThreadSuspender() = default;
    NS_IMETHOD Run() override;

    Monitor mMon;
    bool mSignaled MOZ_GUARDED_BY(mMon){false};
  };

  RefPtr<IOThreadSuspender> mActiveIOSuspender;
};

template <class T>
void ProxyRelease(const char* aName, nsCOMPtr<T>& object,
                  nsIEventTarget* target) {
  NS_ProxyRelease(aName, target, object.forget());
}

template <class T>
void ProxyReleaseMainThread(const char* aName, nsCOMPtr<T>& object) {
  ProxyRelease(aName, object, GetMainThreadSerialEventTarget());
}

}  // namespace net
}  // namespace mozilla

#define NS_CACHE_STORAGE_SERVICE_CID \
  {0xea70b098, 0x5014, 0x4e21, {0xae, 0xe1, 0x75, 0xe6, 0xb2, 0xc4, 0xb8, 0xe0}}

#define NS_CACHE_STORAGE_SERVICE_CONTRACTID \
  "@mozilla.org/netwerk/cache-storage-service;1"

#define NS_CACHE_STORAGE_SERVICE_CONTRACTID2 \
  "@mozilla.org/network/cache-storage-service;1"

#endif
