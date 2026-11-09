/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_net_Dictionary_h
#define mozilla_net_Dictionary_h

#include <vector>

#include "mozilla/RefPtr.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/Vector.h"
#include "mozilla/dom/RequestBinding.h"
#include "mozilla/net/urlpattern_glue.h"
#include "nsCOMPtr.h"
#include "nsHashKeys.h"
#include "nsICacheEntry.h"
#include "nsICacheEntryOpenCallback.h"
#include "nsICacheStorageService.h"
#include "nsICacheStorageVisitor.h"
#include "nsICryptoHash.h"
#include "nsIInterfaceRequestor.h"
#include "nsILoadContextInfo.h"
#include "nsIObserver.h"
#include "nsIStreamListener.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsTHashMap.h"

class nsICacheStorage;
class nsIIOService;
class nsILoadContextInfo;

// Version of metadata entries we expect
static const uint32_t METADATA_DICTIONARY_VERSION = 1;
#define META_DICTIONARY_PREFIX "dict:"_ns

namespace mozilla {
namespace net {

class nsHttpChannel;
class DictionaryOrigin;

// Outstanding requests that offer this dictionary will hold a reference to it.
// If it's replaced (or removed) during the request, we would a) read the data
// into memory* b) unlink this from the origin in the memory cache.
//
// * or we wait for read-into-memory to finish, if we start reading entries
//   when we send the request.
//
// When creating an entry from incoming data, we'll create it with no hash
// initially until the full data has arrived, then update the Hash.
class DictionaryCacheEntry final : public nsICacheEntryOpenCallback,
                                   public nsIStreamListener {
  friend class DictionaryOrigin;

 private:
  ~DictionaryCacheEntry();

 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSICACHEENTRYOPENCALLBACK
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER

  explicit DictionaryCacheEntry(const char* aKey);
  DictionaryCacheEntry(const nsACString& aKey, const nsACString& aPattern,
                       nsTArray<nsCString>& aMatchDest, const nsACString& aId,
                       uint32_t aExpiration = 0,
                       const Maybe<nsCString>& aHash = Nothing());

  static void ConvertMatchDestToEnumArray(
      const nsTArray<nsCString>& aMatchDest,
      nsTArray<dom::RequestDestination>& aMatchEnums);

  // returns true if the pattern for the dictionary matches the path given
  bool Match(const nsACString& aFilePath, ExtContentPolicyType aType,
             uint32_t aNow, uint32_t& aLongest);

  // This will fail if the cache entry is no longer available.
  // Start reading the cache entry into memory and call completion
  // function when done
  nsresult Prefetch(nsILoadContextInfo* aLoadContextInfo, bool& aShouldSuspend,
                    const std::function<void(nsresult)>& aFunc);

  void SetLoadContextInfo(nsILoadContextInfo* aLoadContextInfo) {
    mLoadContextInfo = aLoadContextInfo;
  }
  nsILoadContextInfo* GetLoadContextInfo() const { return mLoadContextInfo; }

  nsCString GetHash() const;
  bool HasHash();
  void SetHash(const nsACString& aHash);

  void WriteOnHash();

  // Defined out of line since DictionaryOrigin isn't complete yet here.
  void SetOrigin(DictionaryOrigin* aOrigin);

  const nsCString& GetId() const { return mId; }

  // keep track of requests that may need the data
  void InUse();
  void UseCompleted();
  bool IsReading() const;

  void SetReplacement(DictionaryCacheEntry* aEntry, DictionaryOrigin* aOrigin) {
    mReplacement = aEntry;
    mOrigin = aOrigin;
    if (mReplacement) {
      mReplacement->mShouldSuspend = true;
      mReplacement->mBlocked = true;
    }
  }

  bool ShouldSuspendUntilCacheRead() const { return mShouldSuspend; }

  // aFunc is called when we have finished reading a dictionary from the
  // cache, or we have no users waiting for cache data (cancelled, etc)
  void CallbackOnCacheRead(const std::function<void(nsresult)>& aFunc);

  const nsACString& GetURI() const { return mURI; }

  const Vector<uint8_t>& GetDictionary() const;

  // Clear dictionary data for testing (forces reload from cache on next
  // prefetch)
  void ClearDataForTesting();

  // Accumulate a hash while saving a file being received to the cache
  void AccumulateHash(const char* aBuf, int32_t aCount);
  void FinishHash();
  void FinishHashOnMainThread(const nsACString& aHash);

  // return a pointer to the data and length
  uint8_t* DictionaryData(size_t* aLength) const;

  bool DictionaryReady() const;

  size_t SizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf) const {
    // XXX
    return mallocSizeOf(this);
  }

  static nsresult ReadCacheData(nsIInputStream* aInStream, void* aClosure,
                                const char* aFromSegment, uint32_t aToOffset,
                                uint32_t aCount, uint32_t* aWriteCount);

  void CleanupOnCacheData(nsresult result);

  void MakeMetadataEntry(nsCString& aNewValue);

  nsresult Write(nsICacheEntry* aEntry);

  nsresult RemoveEntry(nsICacheEntry* aCacheEntry);

  // Parse metadata from DictionaryOrigin
  bool ParseMetadata(const char* aSrc);

  void CopyFrom(DictionaryCacheEntry* aOther) {
    mURI = aOther->mURI;
    mPattern = aOther->mPattern;
    mId = aOther->mId;
    mMatchDest = aOther->mMatchDest;
    // XXX mType = aOther->mType;
  }

  void UnblockAddEntry(DictionaryOrigin* aOrigin);

  const nsCString& GetPattern() const { return mPattern; }
  void AppendMatchDest(nsACString& aDest) const;

 private:
  // URI (without ref) for the dictionary
  nsCString mURI;
  // Expiration time, or 0 for none (default)
  uint32_t mExpiration{0};

  nsCString mPattern;
  nsCString mId;  // max length 1024
  CopyableTArray<dom::RequestDestination> mMatchDest;
  // dcb and dcz use type 'raw'.  We're allowed to ignore types we don't
  // understand, so we can fail to record a dictionary with type != 'raw'
  //  nsCString mType;

  // Cached parsed URLPattern for performance
  Maybe<UrlPatternGlue> mCachedPattern;

  // SHA-256 hash value - only written/read on MainThread (after
  // pending->active) Written by FinishHashOnMainThread(), immutable after
  // that
  nsCString mHash;

  uint32_t mUsers{0};  // active requests using this entry

  // In-memory copy - only accessed on MainThread after validation
  // Populated on MainThread after hash validation succeeds
  Vector<uint8_t> mDictionaryData;

  // Publishes mDictionaryData to reader threads. ReleaseAcquire so that
  // the non-atomic mDictionaryData write is visible before readers see true.
  Atomic<bool, ReleaseAcquire> mDictionaryDataComplete{false};

  // Temporary buffer for accumulating dictionary data during cache reads
  // Only accessed by cache I/O thread during stream callbacks (serialized)
  Vector<uint8_t> mPendingDictionaryData;

  // for accumulating SHA-256 hash values for dictionaries
  // Only used sequentially from the cache write path (AccumulateHash/
  // FinishHash), on whichever thread delivers the data; cleared by FinishHash
  nsCOMPtr<nsICryptoHash> mCrypto;

  // Structure to track prefetch callbacks with their private browsing status
  struct PrefetchRequest {
    std::function<void(nsresult)> callback;
    bool isPrivateBrowsing;
  };

  // Callbacks when prefetch is complete - only accessed on MainThread
  // std::vector instead of TArray because it has a std::function ptr in it
  std::vector<PrefetchRequest> mWaitingPrefetch;

  // If we need to Write() an entry before we know the hash, remember the origin
  // here (creates a temporary cycle). Clear on StopRequest
  RefPtr<DictionaryOrigin> mOrigin;

  // Simple state flags accessed from multiple threads
  Atomic<bool, Relaxed> mStopReceived{false};
  Atomic<bool, Relaxed> mNotCached{false};

  // If set, a new entry wants to replace us, and we have active decoding users.
  // When we finish reading data into this entry for decoding, do 2 things:
  // Remove our entry from origin->mEntries (so no future requests find this,
  // and un-Suspend the new channel so it can start saving data into the cache.
  RefPtr<DictionaryCacheEntry> mReplacement;

  // We should suspend until the ond entry has been read
  bool mShouldSuspend{false};

  // We're blocked from taking over for the old entry for now
  bool mBlocked{false};

  // Set during Prefetch in OnCacheEntryAvailable if the stored response
  // headers still contain Content-Encoding. Non-empty means data on disk
  // is likely still compressed (decompressor wasn't applied before save).
  nsCString mStoredContentEncoding;

  // Only accessed on MainThread.
  nsCOMPtr<nsILoadContextInfo> mLoadContextInfo;
};

// XXX Do we want to pre-read dictionaries into RAM at startup (lazily)?
// If we have all dictionaries stored in the cache, we don't need to do
// lookups to find if an origin has dictionaries or not, and we don't need to
// store empty entries (and LRU them).  Downside would be if there are a LOT of
// origins with dictionaries, which may eventually happen, it would use more
// memory for rarely used origins.  We could have a limit for dictionaries, and
// above that switch to partial caching and empty entries for origins without.

class DictionaryCache;

class DictionaryOriginReader final : public nsICacheEntryOpenCallback,
                                     public nsIStreamListener {
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSICACHEENTRYOPENCALLBACK
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER

  DictionaryOriginReader() = default;

  void Start(
      bool aCreate, DictionaryOrigin* aOrigin, nsACString& aKey, nsIURI* aURI,
      ExtContentPolicyType aType, nsILoadContextInfo* aLoadContextInfo,
      DictionaryCache* aCache,
      const std::function<nsresult(bool, DictionaryCacheEntry*)>& aCallback);
  void FinishMatch();

 private:
  ~DictionaryOriginReader() = default;

  RefPtr<DictionaryOrigin> mOrigin;
  nsCOMPtr<nsIURI> mURI;
  ExtContentPolicyType mType = ExtContentPolicyType::TYPE_INVALID;
  std::function<nsresult(bool, DictionaryCacheEntry*)> mCallback;
  RefPtr<DictionaryCache> mCache;
};

// using DictCacheList = AutoCleanLinkedList<RefPtr<DictionaryCacheEntry>>;
using DictCacheList = nsTArray<RefPtr<DictionaryCacheEntry>>;

// XXX if we want to have a parallel LRU list for pushing origins out of memory,
// add this: public LinkedListElement<RefPtr<DictionaryOrigin>>,
class DictionaryOrigin : public nsICacheEntryMetaDataVisitor {
  friend class DictionaryCache;
  friend class DictionaryOriginReader;

 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSICACHEENTRYMETADATAVISITOR

  DictionaryOrigin(const nsACString& aOrigin, nsICacheEntry* aEntry,
                   nsILoadContextInfo* aLoadContextInfo)
      : mOrigin(aOrigin), mEntry(aEntry), mLoadContextInfo(aLoadContextInfo) {}

  nsILoadContextInfo* GetLoadContextInfo() const { return mLoadContextInfo; }

  void SetMapKey(const nsACString& aMapKey) { mMapKey = aMapKey; }
  const nsCString& GetMapKey() const { return mMapKey; }

  void SetCacheEntry(nsICacheEntry* aEntry);
  nsresult Write(DictionaryCacheEntry* aDictEntry);
  already_AddRefed<DictionaryCacheEntry> AddEntry(
      DictionaryCacheEntry* aDictEntry, bool aNewEntry);
  nsresult RemoveEntry(const nsACString& aKey);
  void RemoveEntry(DictionaryCacheEntry* aEntry);
  DictionaryCacheEntry* Match(const nsACString& path,
                              ExtContentPolicyType aType);
  void FinishAddEntry(DictionaryCacheEntry* aEntry);
  void DumpEntries();
  void Clear();
  bool IsEmpty() const {
    return mEntries.IsEmpty() && mPendingEntries.IsEmpty() &&
           mPendingRemove.IsEmpty() && mWaitingCacheRead.IsEmpty();
  }

 private:
  virtual ~DictionaryOrigin() = default;

  nsCString mOrigin;
  nsCOMPtr<nsICacheEntry> mEntry;
  nsCOMPtr<nsILoadContextInfo> mLoadContextInfo;
  // Key this origin is stored under in DictionaryCache::mDictionaryCache.
  nsCString mMapKey;
  DictCacheList mEntries;
  // Dictionaries currently being received.  Once these get a Hash, move to
  // mEntries
  DictCacheList mPendingEntries;
  // Dictionaries removed from mEntries but waiting to be removed from the
  // Cache metadata
  DictCacheList mPendingRemove;
  // Write out all entries once we have a cacheentry
  bool mDeferredWrites{false};

  // readers that are waiting for this origin's metadata to be read
  nsTArray<RefPtr<DictionaryOriginReader>> mWaitingCacheRead;
};

// singleton class
class DictionaryCache final : public nsIObserver {
 private:
  DictionaryCache() {
    nsresult rv = Init();
    (void)rv;
    MOZ_DIAGNOSTIC_ASSERT(NS_SUCCEEDED(rv));
  }
  ~DictionaryCache() = default;

  friend class DictionaryOriginReader;
  friend class DictionaryCacheEntry;

 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIOBSERVER

  static already_AddRefed<DictionaryCache> GetInstance();

  nsresult Init();
  static void Shutdown();

  // Test-only: undo Shutdown()'s permanent disable so the cache can be used
  // again after a simulated restart (see
  // nsICacheTesting::startupCacheForTesting). The instance is recreated lazily
  // by GetInstance().
  static void ResetShutdownForTesting() { sShutdown = false; }

  nsresult AddEntry(nsIURI* aURI, const nsACString& aKey,
                    const nsACString& aPattern, nsTArray<nsCString>& aMatchDest,
                    const nsACString& aId, const Maybe<nsCString>& aHash,
                    bool aNewEntry, uint32_t aExpiration,
                    nsILoadContextInfo* aLoadContextInfo,
                    DictionaryCacheEntry** aDictEntry);

  already_AddRefed<DictionaryCacheEntry> AddEntry(
      nsIURI* aURI, bool aNewEntry, nsILoadContextInfo* aLoadContextInfo,
      DictionaryCacheEntry* aDictEntry);

  static void RemoveDictionaryOMT(const nsACString& aKey,
                                  nsILoadContextInfo* aLoadContextInfo);
  // remove the entire origin (should be empty!)
  static void RemoveOriginFor(const nsACString& aKey,
                              nsILoadContextInfo* aLoadContextInfo);

  // Remove a dictionary if it exists for the key given
  static void RemoveDictionary(const nsACString& aKey,
                               nsILoadContextInfo* aLoadContextInfo);
  // Remove an origin for the map key given (see DictionaryOrigin::mMapKey)
  void RemoveOrigin(const nsACString& aMapKey);

  nsresult RemoveEntry(nsIURI* aURI, const nsACString& aKey,
                       nsILoadContextInfo* aLoadContextInfo);

  static void RemoveDictionariesForOrigin(nsIURI* aURI);
  static void RemoveAllDictionaries();

  // Clears all ports at host
  void Clear();

  // Corrupt the hash of a dictionary entry for testing
  void CorruptHashForTesting(const nsACString& aURI,
                             nsILoadContextInfo* aLoadContextInfo);

  // Clear the dictionary data while keeping the entry (for testing)
  void ClearDictionaryDataForTesting(const nsACString& aURI,
                                     nsILoadContextInfo* aLoadContextInfo);

  // return an entry
  void GetDictionaryFor(
      nsIURI* aURI, ExtContentPolicyType aType,
      nsILoadContextInfo* aLoadContextInfo, nsHttpChannel* aChan,
      void (*aSuspend)(nsHttpChannel*),
      const std::function<nsresult(bool, DictionaryCacheEntry*)>& aCallback);

  size_t SizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf) const {
    // XXX
    return mallocSizeOf(this);
  }

 private:
  void RemoveOriginForInternal(const nsACString& aKey,
                               nsILoadContextInfo* aLoadContextInfo);

  static already_AddRefed<nsICacheStorage> GetCacheStorage(
      nsILoadContextInfo* aLoadContextInfo);

  // Builds the key used in mDictionaryCache: aPrePath plus a suffix derived
  // from aLoadContextInfo's OriginAttributes and anonymous flag.
  static void MakeCacheKey(const nsACString& aPrePath,
                           nsILoadContextInfo* aLoadContextInfo,
                           nsACString& aKey);

  static Atomic<bool, Relaxed> sShutdown;

  // In-memory cache of dictionary entries.  HashMap, keyed by origin, of
  // Linked list (LRU order) of valid dictionaries for the origin.
  // We keep empty entries in there to avoid hitting the disk cache to find out
  // if there are dictionaries for an origin.
  // Static assertions fire if we try to have a LinkedList directly in an
  // nsTHashMap
  nsTHashMap<nsCStringHashKey, RefPtr<DictionaryOrigin>> mDictionaryCache;
};

}  // namespace net
}  // namespace mozilla

#endif  // mozilla_net_Dictionary_h
