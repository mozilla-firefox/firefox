/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef CacheEntry_h_
#define CacheEntry_h_

#include "CacheFile.h"
#include "Dictionary.h"
#include "mozilla/LinkedList.h"
#include "mozilla/Mutex.h"
#include "mozilla/TimeStamp.h"
#include "nsCOMArray.h"
#include "nsCOMPtr.h"
#include "nsHashKeys.h"
#include "nsICacheEntry.h"
#include "nsICacheEntryDoomCallback.h"
#include "nsICacheEntryOpenCallback.h"
#include "nsIOutputStream.h"
#include "nsIRunnable.h"
#include "nsITransportSecurityInfo.h"
#include "nsIURI.h"
#include "nsRefPtrHashtable.h"
#include "nsString.h"
#include "nsThreadUtils.h"

static inline uint32_t PRTimeToSeconds(PRTime t_usec) {
  return uint32_t(t_usec / PR_USEC_PER_SEC);
}

#define NowInSeconds() PRTimeToSeconds(PR_Now())

class nsIOutputStream;
class nsIURI;
class nsIThread;

namespace mozilla {
namespace net {

class CacheStorageService;
class CacheStorage;
class CacheOutputCloseListener;
class CacheEntryHandle;

class CacheEntry final : public nsIRunnable,
                         public CacheFileListener,
                         // Used by CacheStorageService::MemoryPool
                         public LinkedListElement<RefPtr<CacheEntry>> {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIRUNNABLE

  static uint64_t GetNextId();

  CacheEntry(const nsACString& aStorageID, nsIURI* aURI,
             const nsACString& aEnhanceID, bool aUseDisk, bool aSkipSizeCheck,
             bool aPin);

  void AsyncOpen(nsICacheEntryOpenCallback* aCallback, uint32_t aFlags);
#ifdef NS_FREE_PERMANENT_DATA
  void ClearCallbacks();
#endif

  already_AddRefed<CacheEntryHandle> NewHandle();
  // For a new and recreated entry w/o a callback, we need to wrap it
  // with a handle to detect writing consumer is gone.
  already_AddRefed<CacheEntryHandle> NewWriteHandle();

  // Forwarded to from CacheEntryHandle : nsICacheEntry
  nsresult GetKey(nsACString& aKey);
  nsresult GetCacheEntryId(uint64_t* aCacheEntryId);
  nsresult GetPersistent(bool* aPersistToDisk);
  nsresult GetFetchCount(uint32_t* aFetchCount);
  nsresult GetLastFetched(uint32_t* aLastFetched);
  nsresult GetLastModified(uint32_t* aLastModified);
  nsresult GetExpirationTime(uint32_t* aExpirationTime);
  nsresult SetExpirationTime(uint32_t expirationTime);
  nsresult GetOnStartTime(uint64_t* aTime);
  nsresult GetOnStopTime(uint64_t* aTime);
  nsresult GetReadyOrRevalidating(bool* aReady);
  nsresult SetNetworkTimes(uint64_t onStartTime, uint64_t onStopTime);
  nsresult SetContentType(uint8_t aContentType);
  nsresult ForceValidFor(uint32_t aSecondsToTheFuture);
  nsresult GetIsForcedValid(bool* aIsForcedValid);
  nsresult MarkForcedValidUse();
  nsresult OpenInputStream(int64_t offset, nsIInputStream** _retval);
  nsresult OpenOutputStream(int64_t offset, int64_t predictedSize,
                            nsIOutputStream** _retval);
  nsresult GetSecurityInfo(nsITransportSecurityInfo** aSecurityInfo);
  nsresult SetSecurityInfo(nsITransportSecurityInfo* aSecurityInfo);
  nsresult GetStorageDataSize(uint32_t* aStorageDataSize);
  nsresult AsyncDoom(nsICacheEntryDoomCallback* aCallback);
  nsresult GetMetaDataElement(const char* key, char** aRetval);
  nsresult SetMetaDataElement(const char* key, const char* value);
  nsresult GetIsEmpty(bool* aEmpty);
  nsresult VisitMetaData(nsICacheEntryMetaDataVisitor* visitor);
  nsresult MetaDataReady(void);
  nsresult SetValid(void);
  nsresult GetDiskStorageSizeInKB(uint32_t* aDiskStorageSizeInKB);
  nsresult Recreate(bool aMemoryOnly, nsICacheEntry** _retval);
  nsresult GetDataSize(int64_t* aDataSize);
  nsresult GetAltDataSize(int64_t* aDataSize);
  nsresult GetAltDataType(nsACString& aAltDataType);
  nsresult OpenAlternativeOutputStream(const nsACString& type,
                                       int64_t predictedSize,
                                       nsIAsyncOutputStream** _retval);
  nsresult OpenAlternativeInputStream(const nsACString& type,
                                      nsIInputStream** _retval);
  nsresult GetLoadContextInfo(nsILoadContextInfo** aInfo);

  nsresult SetDictionary(DictionaryCacheEntry* aDict);

 public:
  uint32_t GetMetadataMemoryConsumption();
  nsCString const& GetStorageID() const { return mStorageID; }
  nsCString const& GetEnhanceID() const { return mEnhanceID; }
  nsIURI* GetURI() const { return mURI; }
  // Accessible at any time
  bool IsUsingDisk() const { return mUseDisk; }
  bool IsReferenced() const MOZ_NO_THREAD_SAFETY_ANALYSIS;
  bool IsFileDoomed();
  bool IsDoomed() const { return mIsDoomed; }
  bool IsPinned() const { return mPinned; }

  // Mark entry to allow bypassing writer lock for new listeners
  void SetBypassWriterLock(bool aBypass);
  bool ShouldBypassWriterLock() const MOZ_REQUIRES(mLock) {
    return mBypassWriterLock;
  }

  // Methods for entry management (eviction from memory),
  // called only on the management thread.

  // TODO make these inline
  double GetFrecency() const;
  uint32_t GetExpirationTime() const;
  uint32_t UseCount() const { return mUseCount; }

  bool IsRegistered() const;
  bool CanRegister() const;
  void SetRegistered(bool aRegistered);

  TimeStamp const& LoadStart() const { return mLoadStart; }

  enum EPurge {
    PURGE_DATA_ONLY_DISK_BACKED,
    PURGE_WHOLE_ONLY_DISK_BACKED,
    PURGE_WHOLE,
  };

  bool DeferOrBypassRemovalOnPinStatus(bool aPinned);
  bool Purge(uint32_t aWhat);
  void PurgeAndDoom();
  void DoomAlreadyRemoved();

  nsresult HashingKeyWithStorage(nsACString& aResult) const;
  nsresult HashingKey(nsACString& aResult) const;

  void NoteNoVarySearchEntry(nsIURI* aURI);

  static nsresult HashingKey(const nsACString& aStorageID,
                             const nsACString& aEnhanceID, nsIURI* aURI,
                             nsACString& aResult);

  static nsresult HashingKey(const nsACString& aStorageID,
                             const nsACString& aEnhanceID,
                             const nsACString& aURISpec, nsACString& aResult);

  // Accessed only on the service management thread
  double mFrecency{0};
  ::mozilla::Atomic<uint32_t, ::mozilla::Relaxed> mSortingExpirationTime{
      uint32_t(-1)};

  // Memory reporting
  size_t SizeOfExcludingThis(mozilla::MallocSizeOf mallocSizeOf);
  size_t SizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf);

 private:
  virtual ~CacheEntry();

  // CacheFileListener
  NS_IMETHOD OnFileReady(nsresult aResult, bool aIsNew) override;
  NS_IMETHOD OnFileDoomed(nsresult aResult) override;

  // Keep the service alive during life-time of an entry
  RefPtr<CacheStorageService> mService;

  // We must monitor when a cache entry whose consumer is responsible
  // for writing it the first time gets released.  We must then invoke
  // waiting callbacks to not break the chain.
  class Callback {
   public:
    Callback(CacheEntry* aEntry, nsICacheEntryOpenCallback* aCallback,
             bool aReadOnly, bool aReadAlways, bool aCheckOnAnyThread,
             bool aSecret);
    // Special constructor for Callback objects added to the chain
    // just to ensure proper defer dooming (recreation) of this entry.
    Callback(CacheEntry* aEntry, bool aDoomWhenFoundInPinStatus);
    Callback(Callback const& aThat);
    ~Callback();

    // Called when this callback record changes it's owning entry,
    // mainly during recreation.
    void ExchangeEntry(CacheEntry* aEntry) MOZ_REQUIRES(aEntry->mLock);

    // Returns true when an entry is about to be "defer" doomed and this is
    // a "defer" callback.  The caller must hold a lock (this entry is in the
    // caller's mCallback array)
    bool DeferDoom(bool* aDoom) const;

    // We are raising reference count here to take into account the pending
    // callback (that virtually holds a ref to this entry before it gets
    // it's pointer).
    RefPtr<CacheEntry> mEntry;
    nsCOMPtr<nsICacheEntryOpenCallback> mCallback;
    nsCOMPtr<nsIEventTarget> mTarget;
    bool mReadOnly : 1;
    bool mReadAlways : 1;  // used for dictionary reads
    bool mRevalidating : 1;
    bool mCheckOnAnyThread : 1;
    bool mRecheckAfterWrite : 1;
    bool mNotWanted : 1;
    bool mSecret : 1;

    // These are set only for the defer-doomer Callback instance inserted
    // to the callback chain.  When any of these is set and also any of
    // the corressponding flags on the entry is set, this callback will
    // indicate (via DeferDoom()) the entry have to be recreated/doomed.
    bool mDoomWhenFoundPinned : 1;
    bool mDoomWhenFoundNonPinned : 1;

    nsresult OnCheckThread(bool* aOnCheckThread) const;
    nsresult OnAvailThread(bool* aOnAvailThread) const;
  };

  // Since OnCacheEntryAvailable must be invoked on the main thread
  // we need a runnable for it...
  class AvailableCallbackRunnable : public Runnable {
   public:
    AvailableCallbackRunnable(CacheEntry* aEntry, Callback const& aCallback)
        : Runnable("CacheEntry::AvailableCallbackRunnable"),
          mEntry(aEntry),
          mCallback(aCallback) {}

   private:
    NS_IMETHOD Run() override {
      mEntry->InvokeAvailableCallback(mCallback);
      return NS_OK;
    }

    RefPtr<CacheEntry> mEntry;
    Callback mCallback;
  };

  // Since OnCacheEntryDoomed must be invoked on the main thread
  // we need a runnable for it...
  class DoomCallbackRunnable : public Runnable {
   public:
    DoomCallbackRunnable(CacheEntry* aEntry, nsresult aRv)
        : Runnable("net::CacheEntry::DoomCallbackRunnable"),
          mEntry(aEntry),
          mRv(aRv) {}

   private:
    NS_IMETHOD Run() override {
      nsCOMPtr<nsICacheEntryDoomCallback> callback;
      {
        mozilla::MutexAutoLock lock(mEntry->mLock);
        mEntry->mDoomCallback.swap(callback);
      }

      if (callback) callback->OnCacheEntryDoomed(mRv);
      return NS_OK;
    }

    RefPtr<CacheEntry> mEntry;
    nsresult mRv;
  };

  // Starts the load or just invokes the callback, bypasses (when required)
  // if busy.  Returns true on job done, false on bypass.
  bool Open(Callback& aCallback, bool aTruncate, bool aPriority,
            bool aBypassIfBusy);
  // Loads from disk asynchronously
  bool Load(bool aTruncate, bool aPriority);

  void RememberCallback(Callback& aCallback) MOZ_REQUIRES(mLock);
  void InvokeCallbacksLock();
  void InvokeCallbacks();
  bool InvokeCallbacks(bool aReadOnly);
  bool InvokeCallback(Callback& aCallback);
  void InvokeAvailableCallback(Callback const& aCallback);
  void OnFetched(Callback const& aCallback);

  nsresult OpenOutputStreamInternal(int64_t offset, nsIOutputStream** _retval);
  nsresult OpenInputStreamInternal(int64_t offset, const char* aAltDataType,
                                   nsIInputStream** _retval);

  void OnHandleClosed(CacheEntryHandle const* aHandle);

 private:
  friend class CacheEntryHandle;
  // Increment/decrements the number of handles keeping this entry.
  void AddHandleRef() MOZ_REQUIRES(mLock) { ++mHandlesCount; }
  void ReleaseHandleRef() MOZ_REQUIRES(mLock) { --mHandlesCount; }
  // Current number of handles keeping this entry.
  uint32_t HandlesCount() const MOZ_REQUIRES(mLock) { return mHandlesCount; }

 private:
  friend class CacheOutputCloseListener;
  void OnOutputClosed();

 private:
  // Schedules a background operation on the management thread.
  // When executed on the management thread directly, the operation(s)
  // is (are) executed immediately.
  void BackgroundOp(uint32_t aOperation, bool aForceAsync = false);
  void StoreFrecency(double aFrecency);

  // Called only from DoomAlreadyRemoved()
  void DoomFile() MOZ_REQUIRES(mLock);
  // When this entry is doomed the first time, this method removes
  // any force-valid timing info for this entry.
  void RemoveForcedValidity();

  already_AddRefed<CacheEntryHandle> ReopenTruncated(
      bool aMemoryOnly, nsICacheEntryOpenCallback* aCallback);
  void TransferCallbacks(CacheEntry& aFromEntry);

  mozilla::Mutex mLock{"CacheEntry"};

  // Reflects the number of existing handles for this entry
  ::mozilla::ThreadSafeAutoRefCnt mHandlesCount MOZ_GUARDED_BY(mLock);

  nsTArray<Callback> mCallbacks MOZ_GUARDED_BY(mLock);
  nsCOMPtr<nsICacheEntryDoomCallback> mDoomCallback MOZ_GUARDED_BY(mLock);

  // Set in CacheEntry::Load(), only - shouldn't need to be under lock
  // XXX FIX?  is this correct?
  RefPtr<CacheFile> mFile;

  // Using ReleaseAcquire since we only control access to mFile with this.
  // When mFileStatus is read and found success it is ensured there is mFile and
  // that it is after a successful call to Init().
  Atomic<nsresult, ReleaseAcquire> mFileStatus{NS_ERROR_NOT_INITIALIZED};
  // Set in constructor. nsIURI is immutable and thread-safe, so this can be
  // read from any thread without a lock.
  nsCOMPtr<nsIURI> const mURI;
  nsCString const mEnhanceID;
  nsCString const mStorageID;

  // mUseDisk, mSkipSizeCheck, mIsDoomed are plain "bool", not "bool:1",
  // so as to avoid bitfield races with the byte containing
  // mSecurityInfoLoaded et al.  See bug 1278524.
  //
  // Whether it's allowed to persist the data to disk
  bool const mUseDisk;
  // Whether it should skip max size check.
  bool const mSkipSizeCheck;
  // Set when entry is doomed with AsyncDoom() or DoomAlreadyRemoved().
  Atomic<bool, Relaxed> mIsDoomed{false};
  // The indication of pinning this entry was open with
  Atomic<bool, Relaxed> mPinned;

  // Following flags are all synchronized with the cache entry lock.

  // Whether security info has already been looked up in metadata.
  bool mSecurityInfoLoaded : 1 MOZ_GUARDED_BY(mLock);
  // Prevents any callback invocation
  bool mPreventCallbacks : 1 MOZ_GUARDED_BY(mLock);
  // true: after load and an existing file, or after output stream has been
  //       opened.
  //       note - when opening an input stream, and this flag is false, output
  //       stream is open along ; this makes input streams on new entries
  //       behave correctly when EOF is reached (WOULD_BLOCK is returned).
  // false: after load and a new file, or dropped to back to false when a
  //        writer fails to open an output stream.
  bool mHasData : 1 MOZ_GUARDED_BY(mLock);
  // Whether the pinning state of the entry is known (equals to the actual state
  // of the cache file)
  bool mPinningKnown : 1 MOZ_GUARDED_BY(mLock);
  // Whether to bypass writer lock for new listeners (when writer is suspended)
  bool mBypassWriterLock : 1 MOZ_GUARDED_BY(mLock);

  static char const* StateString(uint32_t aState);

  enum EState {       // transiting to:
    NOTLOADED = 0,    // -> LOADING | EMPTY
    LOADING = 1,      // -> EMPTY | READY
    EMPTY = 2,        // -> WRITING
    WRITING = 3,      // -> EMPTY | READY
    READY = 4,        // -> REVALIDATING
    REVALIDATING = 5  // -> READY
  };

  // State of this entry.
  EState mState MOZ_GUARDED_BY(mLock){NOTLOADED};

  enum ERegistration {
    NEVERREGISTERED = 0,  // The entry has never been registered
    REGISTERED = 1,       // The entry is stored in the memory pool index
    DEREGISTERED = 2      // The entry has been removed from the pool
  };

  // Accessed only on the management thread.  Records the state of registration
  // this entry in the memory pool intermediate cache.
  ERegistration mRegistration{NEVERREGISTERED};

  // If a new (empty) entry is requested to open an input stream before
  // output stream has been opened, we must open output stream internally
  // on CacheFile and hold until writer releases the entry or opens the output
  // stream for read (then we trade him mOutputStream).
  nsCOMPtr<nsIOutputStream> mOutputStream MOZ_GUARDED_BY(mLock);

  // Weak reference to the current writter.  There can be more then one
  // writer at a time and OnHandleClosed() must be processed only for the
  // current one.
  CacheEntryHandle* mWriter MOZ_GUARDED_BY(mLock){nullptr};

  // Background thread scheduled operation.  Set (under the lock) one
  // of this flags to tell the background thread what to do.
  class Ops {
   public:
    static uint32_t const REGISTER = 1 << 0;
    static uint32_t const FRECENCYUPDATE = 1 << 1;
    static uint32_t const CALLBACKS = 1 << 2;
    static uint32_t const UNREGISTER = 1 << 3;

    Ops() = default;
    uint32_t Grab() {
      uint32_t flags = mFlags;
      mFlags = 0;
      return flags;
    }
    bool Set(uint32_t aFlags) {
      bool needsDispatch = !mFlags;
      mFlags |= aFlags;
      return needsDispatch;
    }

   private:
    uint32_t mFlags{0};
  } mBackgroundOperations;

  nsCOMPtr<nsITransportSecurityInfo> mSecurityInfo;
  mozilla::TimeStamp mLoadStart;
  uint32_t mUseCount{0};

  RefPtr<DictionaryCacheEntry> mDict;

  const uint64_t mCacheEntryId;
};

class CacheEntryHandle final : public nsICacheEntry {
 public:
  explicit CacheEntryHandle(CacheEntry* aEntry);
  CacheEntry* Entry() const { return mEntry; }

  NS_DECL_THREADSAFE_ISUPPORTS

  // Default implementation is simply safely forwarded.
  NS_IMETHOD GetKey(nsACString& aKey) override { return mEntry->GetKey(aKey); }
  NS_IMETHOD GetCacheEntryId(uint64_t* aCacheEntryId) override {
    return mEntry->GetCacheEntryId(aCacheEntryId);
  }
  NS_IMETHOD GetPersistent(bool* aPersistent) override {
    return mEntry->GetPersistent(aPersistent);
  }
  NS_IMETHOD GetFetchCount(uint32_t* aFetchCount) override {
    return mEntry->GetFetchCount(aFetchCount);
  }
  NS_IMETHOD GetLastFetched(uint32_t* aLastFetched) override {
    return mEntry->GetLastFetched(aLastFetched);
  }
  NS_IMETHOD GetLastModified(uint32_t* aLastModified) override {
    return mEntry->GetLastModified(aLastModified);
  }
  NS_IMETHOD GetExpirationTime(uint32_t* aExpirationTime) override {
    return mEntry->GetExpirationTime(aExpirationTime);
  }
  NS_IMETHOD SetExpirationTime(uint32_t expirationTime) override {
    return mEntry->SetExpirationTime(expirationTime);
  }
  NS_IMETHOD GetOnStartTime(uint64_t* aOnStartTime) override {
    return mEntry->GetOnStartTime(aOnStartTime);
  }
  NS_IMETHOD GetOnStopTime(uint64_t* aOnStopTime) override {
    return mEntry->GetOnStopTime(aOnStopTime);
  }
  NS_IMETHOD GetReadyOrRevalidating(bool* aReady) override {
    return mEntry->GetReadyOrRevalidating(aReady);
  }
  NS_IMETHOD SetNetworkTimes(uint64_t onStartTime,
                             uint64_t onStopTime) override {
    return mEntry->SetNetworkTimes(onStartTime, onStopTime);
  }
  NS_IMETHOD SetContentType(uint8_t contentType) override {
    return mEntry->SetContentType(contentType);
  }
  NS_IMETHOD ForceValidFor(uint32_t aSecondsToTheFuture) override {
    return mEntry->ForceValidFor(aSecondsToTheFuture);
  }
  NS_IMETHOD GetIsForcedValid(bool* aIsForcedValid) override {
    return mEntry->GetIsForcedValid(aIsForcedValid);
  }
  NS_IMETHOD MarkForcedValidUse() override {
    return mEntry->MarkForcedValidUse();
  }
  NS_IMETHOD OpenInputStream(int64_t offset,
                             nsIInputStream** _retval) override {
    return mEntry->OpenInputStream(offset, _retval);
  }
  NS_IMETHOD OpenOutputStream(int64_t offset, int64_t predictedSize,
                              nsIOutputStream** _retval) override {
    return mEntry->OpenOutputStream(offset, predictedSize, _retval);
  }
  NS_IMETHOD GetSecurityInfo(
      nsITransportSecurityInfo** aSecurityInfo) override {
    return mEntry->GetSecurityInfo(aSecurityInfo);
  }
  NS_IMETHOD SetSecurityInfo(nsITransportSecurityInfo* aSecurityInfo) override {
    return mEntry->SetSecurityInfo(aSecurityInfo);
  }
  NS_IMETHOD GetStorageDataSize(uint32_t* aStorageDataSize) override {
    return mEntry->GetStorageDataSize(aStorageDataSize);
  }
  NS_IMETHOD AsyncDoom(nsICacheEntryDoomCallback* listener) override {
    return mEntry->AsyncDoom(listener);
  }
  NS_IMETHOD GetMetaDataElement(const char* key, char** _retval) override {
    return mEntry->GetMetaDataElement(key, _retval);
  }
  NS_IMETHOD SetMetaDataElement(const char* key, const char* value) override {
    return mEntry->SetMetaDataElement(key, value);
  }
  NS_IMETHOD GetIsEmpty(bool* empty) override {
    return mEntry->GetIsEmpty(empty);
  }
  NS_IMETHOD VisitMetaData(nsICacheEntryMetaDataVisitor* visitor) override {
    return mEntry->VisitMetaData(visitor);
  }
  NS_IMETHOD MetaDataReady(void) override { return mEntry->MetaDataReady(); }
  NS_IMETHOD SetValid(void) override { return mEntry->SetValid(); }
  NS_IMETHOD GetDiskStorageSizeInKB(uint32_t* aDiskStorageSizeInKB) override {
    return mEntry->GetDiskStorageSizeInKB(aDiskStorageSizeInKB);
  }
  NS_IMETHOD Recreate(bool aMemoryOnly, nsICacheEntry** _retval) override {
    return mEntry->Recreate(aMemoryOnly, _retval);
  }
  NS_IMETHOD GetDataSize(int64_t* aDataSize) override {
    return mEntry->GetDataSize(aDataSize);
  }
  NS_IMETHOD GetAltDataSize(int64_t* aAltDataSize) override {
    return mEntry->GetAltDataSize(aAltDataSize);
  }
  NS_IMETHOD GetAltDataType(nsACString& aType) override {
    return mEntry->GetAltDataType(aType);
  }
  NS_IMETHOD OpenAlternativeOutputStream(
      const nsACString& type, int64_t predictedSize,
      nsIAsyncOutputStream** _retval) override {
    return mEntry->OpenAlternativeOutputStream(type, predictedSize, _retval);
  }
  NS_IMETHOD OpenAlternativeInputStream(const nsACString& type,
                                        nsIInputStream** _retval) override {
    return mEntry->OpenAlternativeInputStream(type, _retval);
  }
  NS_IMETHOD GetLoadContextInfo(
      nsILoadContextInfo** aLoadContextInfo) override {
    return mEntry->GetLoadContextInfo(aLoadContextInfo);
  }
  NS_IMETHOD SetDictionary(DictionaryCacheEntry* aDict) override {
    return mEntry->SetDictionary(aDict);
  }
  NS_IMETHOD SetBypassWriterLock(bool aBypass) override {
    mEntry->SetBypassWriterLock(aBypass);
    return NS_OK;
  }

  // Specific implementation:
  NS_IMETHOD Dismiss() override;

 private:
  virtual ~CacheEntryHandle();
  RefPtr<CacheEntry> mEntry;

  // This is |false| until Dismiss() was called and prevents OnHandleClosed
  // being called more than once.
  Atomic<bool, ReleaseAcquire> mClosed{false};
};

class CacheOutputCloseListener final : public Runnable {
 public:
  void OnOutputClosed();

 private:
  friend class CacheEntry;

  virtual ~CacheOutputCloseListener() = default;

  NS_DECL_NSIRUNNABLE
  explicit CacheOutputCloseListener(CacheEntry* aEntry);

 private:
  RefPtr<CacheEntry> mEntry;
};

}  // namespace net
}  // namespace mozilla

#endif
