/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Dictionary.h"

#include <stdlib.h>

#include "CacheFileUtils.h"
#include "LoadContextInfo.h"
#include "ReferrerInfo.h"
#include "SerializedLoadContext.h"
#include "mozilla/ClearOnShutdown.h"
#include "mozilla/Components.h"
#include "mozilla/FlowMarkers.h"
#include "mozilla/Logging.h"
#include "mozilla/OriginAttributes.h"
#include "mozilla/Preferences.h"
#include "mozilla/SchedulerGroup.h"
#include "mozilla/Services.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/dom/ContentParent.h"
#include "mozilla/dom/Document.h"
#include "mozilla/dom/InternalRequest.h"
#include "mozilla/glean/GleanPings.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "mozilla/ipc/URIUtils.h"
#include "mozilla/net/NeckoChild.h"
#include "mozilla/net/NeckoCommon.h"
#include "mozilla/net/NeckoParent.h"
#include "mozilla/net/URLPatternGlue.h"
#include "mozilla/net/urlpattern_glue.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsAttrValue.h"
#include "nsContentPolicyUtils.h"
#include "nsContentUtils.h"
#include "nsHTTPCompressConv.h"
#include "nsIAsyncInputStream.h"
#include "nsICacheEntry.h"
#include "nsICacheStorage.h"
#include "nsICacheStorageService.h"
#include "nsICachingChannel.h"
#include "nsICancelable.h"
#include "nsIChannel.h"
#include "nsIEffectiveTLDService.h"
#include "nsIFile.h"
#include "nsIInputStream.h"
#include "nsILoadContext.h"
#include "nsILoadContextInfo.h"
#include "nsILoadGroup.h"
#include "nsIOService.h"
#include "nsIObserverService.h"
#include "nsIURI.h"
#include "nsIURIMutator.h"
#include "nsInputStreamPump.h"
#include "nsNetCID.h"
#include "nsNetUtil.h"
#include "nsServiceManagerUtils.h"
#include "nsSimpleURI.h"
#include "nsStreamUtils.h"
#include "nsString.h"
#include "nsThreadUtils.h"

using namespace mozilla;

namespace mozilla {
namespace net {

// Access to all these classes is from MainThread unless otherwise specified

LazyLogModule gDictionaryLog("CompressionDictionaries");

#define DICTIONARY_LOG(args) \
  MOZ_LOG(gDictionaryLog, mozilla::LogLevel::Debug, args)

/**
 * Reference to the DictionaryCache singleton. May be null.
 */
StaticRefPtr<DictionaryCache> gDictionaryCache;
Atomic<bool, Relaxed> DictionaryCache::sShutdown{false};

// about:cache gets upset about entries that don't fit URL specs, so we need
// to add the trailing '/' to GetPrePath()
static nsresult GetDictPath(nsIURI* aURI, nsACString& aPrePath) {
  if (NS_FAILED(aURI->GetPrePath(aPrePath))) {
    return NS_ERROR_FAILURE;
  }
  aPrePath += '/';
  return NS_OK;
}

// static
void DictionaryCache::MakeCacheKey(const nsACString& aPrePath,
                                   nsILoadContextInfo* aLoadContextInfo,
                                   nsACString& aKey) {
  aKey.Truncate();
  if (aLoadContextInfo) {
    if (aLoadContextInfo->IsAnonymous()) {
      aKey.AppendLiteral("A");
    }
    aLoadContextInfo->OriginAttributesPtr()->CreateSuffix(aKey);
  }
  aKey.Append(aPrePath);
}

// static
already_AddRefed<nsICacheStorage> DictionaryCache::GetCacheStorage(
    nsILoadContextInfo* aLoadContextInfo) {
  nsCOMPtr<nsICacheStorageService> cacheStorageService(
      components::CacheStorage::Service());
  if (!cacheStorageService) {
    return nullptr;
  }
  nsCOMPtr<nsICacheStorage> storage;
  if (NS_FAILED(cacheStorageService->DiskCacheStorage(
          aLoadContextInfo, getter_AddRefs(storage)))) {
    return nullptr;
  }
  return storage.forget();
}

DictionaryCacheEntry::DictionaryCacheEntry(const char* aKey) {
  mURI = aKey;
  DICTIONARY_LOG(("Created DictionaryCacheEntry %p, uri=%s", this, aKey));
}

DictionaryCacheEntry::~DictionaryCacheEntry() {
  MOZ_ASSERT(mUsers == 0);
  DICTIONARY_LOG(
      ("Destroyed DictionaryCacheEntry %p, uri=%s, pattern=%s, id=%s", this,
       mURI.get(), mPattern.get(), mId.get()));
  if (mCachedPattern.isSome()) {
    if (NS_IsMainThread()) {
      urlpattern_pattern_free(mCachedPattern.ref());
    } else {
      UrlPatternGlue pattern = mCachedPattern.ref();
      NS_DispatchToMainThread(NS_NewRunnableFunction(
          "DictionaryCacheEntry::FreeCachedPattern",
          [pattern]() { urlpattern_pattern_free(pattern); }));
    }
  }
}

DictionaryCacheEntry::DictionaryCacheEntry(const nsACString& aURI,
                                           const nsACString& aPattern,
                                           nsTArray<nsCString>& aMatchDest,
                                           const nsACString& aId,
                                           uint32_t aExpiration,
                                           const Maybe<nsCString>& aHash)
    : mURI(aURI), mExpiration(aExpiration), mPattern(aPattern), mId(aId) {
  ConvertMatchDestToEnumArray(aMatchDest, mMatchDest);
  DICTIONARY_LOG(
      ("Created DictionaryCacheEntry %p, uri=%s, pattern=%s, id=%s, "
       "expiration=%u",
       this, PromiseFlatCString(aURI).get(), PromiseFlatCString(aPattern).get(),
       PromiseFlatCString(aId).get(), aExpiration));
  if (aHash) {
    mHash = aHash.value();
  }
}

NS_IMPL_ISUPPORTS(DictionaryCacheEntry, nsICacheEntryOpenCallback,
                  nsIStreamListener)

// Convert string MatchDest array to enum array
// static
void DictionaryCacheEntry::ConvertMatchDestToEnumArray(
    const nsTArray<nsCString>& aMatchDest,
    nsTArray<dom::RequestDestination>& aMatchEnums) {
  AutoTArray<dom::RequestDestination, 3> temp;
  for (auto& string : aMatchDest) {
    dom::RequestDestination dest =
        dom::StringToEnum<dom::RequestDestination>(string).valueOr(
            dom::RequestDestination::_empty);
    if (dest != dom::RequestDestination::_empty) {
      temp.AppendElement(dest);
    }
  }
  aMatchEnums.SwapElements(temp);
}

// Returns true if the pattern for the dictionary matches the path given.
// Note: we need to verify that this entry has not expired due to 2.2.1 of
// https://datatracker.ietf.org/doc/draft-ietf-httpbis-compression-dictionary/
bool DictionaryCacheEntry::Match(const nsACString& aFilePath,
                                 ExtContentPolicyType aType, uint32_t aNow,
                                 uint32_t& aLongest) {
  MOZ_ASSERT(NS_IsMainThread());
  if (mHash.IsEmpty()) {
    // We don't have the file yet
    return false;
  }
  if (mNotCached) {
    // Not actually in the cache
    // May not actually be necessary, but good safety valve.
    return false;
  }
  // Not worth checking if we wouldn't use it
  if ((mExpiration == 0 || aNow < mExpiration) &&
      mPattern.Length() > aLongest) {
    // Need to match using match-dest, if it exists
    if (mMatchDest.IsEmpty() ||
        mMatchDest.IndexOf(
            dom::InternalRequest::MapContentPolicyTypeToRequestDestination(
                aType)) != mMatchDest.NoIndex) {
      // Check if we have a cached pattern, otherwise parse and cache it
      if (mCachedPattern.isNothing()) {
        UrlPatternGlue pattern;
        UrlPatternOptions options{};
        const nsCString base(mURI);
        if (!urlpattern_parse_pattern_from_string(&mPattern, &base, options,
                                                  &pattern)) {
          DICTIONARY_LOG(
              ("Failed to parse dictionary pattern %s", mPattern.get()));
          return false;
        }
        mCachedPattern.emplace(pattern);
      }

      UrlPatternInput input = net::CreateUrlPatternInput(aFilePath);
      const nsCString base(mURI);
      bool result =
          net::UrlPatternTest(mCachedPattern.ref(), input, Some(base));
      if (result) {
        aLongest = mPattern.Length();
        DICTIONARY_LOG(("Match: %p   %s to %s, %s (now=%u, expiration=%u)",
                        this, PromiseFlatCString(aFilePath).get(),
                        mPattern.get(), NS_CP_ContentTypeName(aType), aNow,
                        mExpiration));
        DICTIONARY_LOG(("Match: %s (longest %u)", mURI.get(), aLongest));
      }
      return result;
    }  // else failed on match-dest
  }  // else failed on expiration
  return false;
}

void DictionaryCacheEntry::InUse() {
  mUsers++;
  DICTIONARY_LOG(("Dictionary users for %s -- %u Users", mURI.get(), mUsers));
}

void DictionaryCacheEntry::UseCompleted() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mUsers > 0);
  mUsers--;
  // Purge mDictionaryData
  if (mUsers == 0) {  // XXX perhaps we should hold it for a bit longer?
    DICTIONARY_LOG(("Clearing Dictionary data for %s", mURI.get()));
    mDictionaryData.clear();
    mDictionaryDataComplete = false;
  } else {
    DICTIONARY_LOG(("Not clearing Dictionary data for %s -- %u Users",
                    mURI.get(), mUsers));
  }
}

nsCString DictionaryCacheEntry::GetHash() const {
  MOZ_ASSERT(NS_IsMainThread());
  return mHash;
}

bool DictionaryCacheEntry::HasHash() {
  MOZ_ASSERT(NS_IsMainThread());
  return !mHash.IsEmpty();
}

void DictionaryCacheEntry::SetHash(const nsACString& aHash) {
  MOZ_ASSERT(NS_IsMainThread());
  mHash = aHash;
}

bool DictionaryCacheEntry::IsReading() const {
  MOZ_ASSERT(NS_IsMainThread());
  return mUsers > 0 && !mWaitingPrefetch.empty();
}

void DictionaryCacheEntry::CallbackOnCacheRead(
    const std::function<void(nsresult)>& aFunc) {
  MOZ_ASSERT(NS_IsMainThread());
  // CallbackOnCacheRead is used for dictionary saving, not validation,
  // so private browsing doesn't apply here
  mWaitingPrefetch.push_back(PrefetchRequest{aFunc, false});
}

const Vector<uint8_t>& DictionaryCacheEntry::GetDictionary() const
    MOZ_NO_THREAD_SAFETY_ANALYSIS {
  // May be called off main thread when the channel has been retargeted.
  // Safe because mDictionaryData is immutable after mDictionaryDataComplete.
  MOZ_ASSERT(mDictionaryDataComplete);
  return mDictionaryData;
}

void DictionaryCacheEntry::ClearDataForTesting() {
  MOZ_ASSERT(NS_IsMainThread());
  mDictionaryData.clear();
  mDictionaryDataComplete = false;
}

uint8_t* DictionaryCacheEntry::DictionaryData(size_t* aLength) const {
  MOZ_ASSERT(mDictionaryDataComplete);
  *aLength = mDictionaryData.length();
  return (uint8_t*)mDictionaryData.begin();
}

bool DictionaryCacheEntry::DictionaryReady() const {
  return mDictionaryDataComplete;
}

// returns aShouldSuspend=true if we should suspend to wait for the prefetch
nsresult DictionaryCacheEntry::Prefetch(
    nsILoadContextInfo* aLoadContextInfo, bool& aShouldSuspend,
    const std::function<void(nsresult)>& aFunc) {
  MOZ_ASSERT(NS_IsMainThread());

  DICTIONARY_LOG(("Prefetch for %s", mURI.get()));

  // Refuse to serve dictionary bytes to a request from a different
  // partition than the one this entry was added under.
  if (mLoadContextInfo && aLoadContextInfo &&
      !mLoadContextInfo->Equals(aLoadContextInfo)) {
    DICTIONARY_LOG(
        ("Prefetch for %s - refusing cross-partition access", mURI.get()));
    aShouldSuspend = false;
    return NS_ERROR_FAILURE;
  }

  // Determine private browsing status for this request
  bool isPrivateBrowsing = aLoadContextInfo && aLoadContextInfo->IsPrivate();

  // Start reading the cache entry into memory and call completion
  // function when done
  if (!mWaitingPrefetch.empty()) {
    DICTIONARY_LOG(("Prefetch for %s - already waiting", mURI.get()));
    mWaitingPrefetch.push_back(PrefetchRequest{aFunc, isPrivateBrowsing});
    aShouldSuspend = true;
    return NS_OK;
  }

  // Note that if the cache entry has been cleared, and we still have active
  // users of it, we'll hold onto that data since we have outstanding requests
  // for it.  Probably we shouldn't allow new requests to use this data (and
  // the WPTs assume we shouldn't).
  if (mDictionaryDataComplete) {
    DICTIONARY_LOG(("Prefetch for %s - already have data in memory (%u users)",
                    mURI.get(), mUsers));
    aShouldSuspend = false;
    return NS_OK;
  }

  // We haven't requested it yet from the Cache and don't have it in memory
  // already. Add to waiting list.
  mWaitingPrefetch.push_back(PrefetchRequest{aFunc, isPrivateBrowsing});

  nsCOMPtr<nsICacheStorage> cacheStorage =
      DictionaryCache::GetCacheStorage(aLoadContextInfo);
  if (!cacheStorage) {
    mWaitingPrefetch.clear();
    aShouldSuspend = false;
    return NS_ERROR_FAILURE;
  }
  // If the file isn't available in the cache, AsyncOpenURIString()
  // will synchronously make a callback to OnCacheEntryAvailable() with
  // nullptr.  We can key off that to fail Prefetch(), and also to
  // remove ourselves from the origin.
  // Use OPEN_ALWAYS to ensure we don't get stalled if this is for some
  // reason a revalidation
  if (NS_FAILED(cacheStorage->AsyncOpenURIString(
          mURI, ""_ns,
          nsICacheStorage::OPEN_READONLY | nsICacheStorage::OPEN_COMPLETE_ONLY |
              nsICacheStorage::OPEN_ALWAYS |
              nsICacheStorage::CHECK_MULTITHREADED,
          this)) ||
      mNotCached) {
    DICTIONARY_LOG(("AsyncOpenURIString failed for %s", mURI.get()));
    // For some reason the cache no longer has this entry; fail Prefetch
    // and also remove this from our origin
    mWaitingPrefetch.clear();
    aShouldSuspend = false;
    // Remove from origin
    if (mOrigin) {
      mOrigin->RemoveEntry(this);
      mOrigin = nullptr;
    }
    return NS_ERROR_FAILURE;
  }
  DICTIONARY_LOG(("Started Prefetch for %s, anonymous=%d", mURI.get(),
                  aLoadContextInfo->IsAnonymous()));
  aShouldSuspend = true;
  return NS_OK;
}

void DictionaryCacheEntry::AccumulateHash(const char* aBuf, int32_t aCount) {
  // Called from CacheFileOutputStream::Write, which may run on a non-main
  // thread when the channel's delivery has been retargeted (e.g. by
  // FetchDriver). Safe because during the save phase, mCrypto, mHash, and
  // mDictionaryData are only accessed sequentially from the write path.
  // No concurrent access occurs: mHash is empty (not yet computed),
  // mDictionaryData is empty (not yet loaded), and mCrypto is only used
  // by AccumulateHash/FinishHash which are called sequentially.
  if (!mHash.IsEmpty()) {
    if (!mDictionaryData.empty()) {
      // We have data from the cache.... but if we change the hash there will
      // be problems
      // XXX dragons here
      MOZ_DIAGNOSTIC_ASSERT(
          false,
          "Accumulate Dictionary hash when we already have a hash and data");
      return;
    }
    // accumulating a new hash when we have an existing?
    // XXX probably kill the hash when we get an overwrite; tricky, need to
    // handle loading the old one into ram to decompress the new one.  Also,
    // what if the old one is being used for multiple requests, one of which
    // is an overwrite?   This is an edge case not discussed in the spec - we
    // could separate out a structure for in-flight requests where the data
    // would be used from, so the Entry could be overwritten as needed
    MOZ_DIAGNOSTIC_ASSERT(
        false, "Accumulate Dictionary hash when we already have a hash");
    return;  // XXX
  }
  size_t dataLen = mDictionaryData.length();
  if (!mCrypto) {
    DICTIONARY_LOG(("Calculating new hash for %s", mURI.get()));
    // If mCrypto is null, and mDictionaryData is set, we've already got the
    // data for this dictionary.
    nsresult rv;
    mCrypto = do_CreateInstance(NS_CRYPTO_HASH_CONTRACTID, &rv);
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return;
    }
    rv = mCrypto->Init(nsICryptoHash::SHA256);
    NS_WARNING_ASSERTION(NS_SUCCEEDED(rv), "Cache InitCrypto failed");
  }
  // Detect if we're hashing compressed data — the cache should store
  // decompressed dictionary bytes.
  if (MOZ_UNLIKELY(MOZ_LOG_TEST(gDictionaryLog, mozilla::LogLevel::Debug))) {
    if (dataLen == 0 && aCount >= 2) {
      const uint8_t* bytes = reinterpret_cast<const uint8_t*>(aBuf);
      if (bytes[0] == GZIP_MAGIC_0 && bytes[1] == GZIP_MAGIC_1) {
        DICTIONARY_LOG((
            "**** WARNING: AccumulateHash for %s starts with gzip magic bytes! "
            "Data may be stored compressed instead of decompressed.",
            mURI.get()));
      } else if (bytes[0] == BROTLI_BYTE_0 && bytes[1] == BROTLI_BYTE_1) {
        // brotli has no fixed magic, but 0xceb2 is common for shared brotli
        // dict
        DICTIONARY_LOG(
            ("*** NOTE: AccumulateHash likely brotli for %s", mURI.get()));
      }
    }
  }
  mCrypto->Update(reinterpret_cast<const uint8_t*>(aBuf), aCount);
  DICTIONARY_LOG(
      ("Accumulate Hash %p: %d bytes, total %zu", this, aCount, dataLen));
}

void DictionaryCacheEntry::FinishHash() {
  // May be called off main thread when channel delivery is retargeted.
  // Hash computation is safe off-thread; mHash and origin operations are
  // only touched on the main thread.
  if (mCrypto) {
    nsCString hash;
    mCrypto->Finish(true, hash);
    mCrypto = nullptr;
    DICTIONARY_LOG(("Hash for %p (%s) is %s", this, mURI.get(), hash.get()));
    if (NS_IsMainThread()) {
      FinishHashOnMainThread(hash);
    } else {
      NS_DispatchToMainThread(NS_NewRunnableFunction(
          "DictionaryCacheEntry::FinishHashOnMainThread",
          [self = RefPtr{this}, hash = std::move(hash)]() {
            self->FinishHashOnMainThread(hash);
          }));
    }
  }
}

void DictionaryCacheEntry::FinishHashOnMainThread(const nsACString& aHash) {
  MOZ_ASSERT(NS_IsMainThread());
  mHash = aHash;
  RefPtr<DictionaryOrigin> origin = std::move(mOrigin);
  if (!origin) {
    return;
  }
  DICTIONARY_LOG(("Write on hash"));
  if (NS_FAILED(origin->Write(this))) {
    origin->RemoveEntry(this);
    return;
  }
  if (!mBlocked) {
    origin->FinishAddEntry(this);
  }
}

// Version of metadata entries we expect
static const uint32_t METADATA_VERSION = 1;

// Metadata format:
// |version|hash|pattern|[matchdest|]*||id|expiration|type
//
// * Entries:
// ** CString: URI -- the key, not in the entry
// ** CString: Version (1)
// ** CString: Hash
// ** CString: Pattern
// ** match-dest CString list, terminated by empty string
// *** CString: Match-dest
// ** CString: Id
// ** uint32 as a CString: expiration. If missing, 0 (none)
// ** CString: type -- defaults to 'raw' if missing
// We store strings with a delimiter, and use escapes for delimiters or escape
// characters in the source strings.
//

// Escape the string and append to aOutput
static void EscapeMetadataString(const nsACString& aInput, nsCString& aOutput) {
  // First calculate how much we'll need to append.  Means we'll walk the source
  // twice, but avoids any potential multiple reallocations
  size_t len = 1;  // for initial |
  for (size_t i = 0; i < aInput.Length(); ++i) {
    if (aInput[i] == '|' || aInput[i] == '\\') {
      len += 2;
    } else {
      len++;
    }
  }
  aOutput.SetCapacity(aOutput.Length() + len);

  aOutput.AppendLiteral("|");
  for (size_t i = 0; i < aInput.Length(); ++i) {
    if (aInput[i] == '|' || aInput[i] == '\\') {
      aOutput.Append('\\');
    }
    aOutput.Append(aInput[i]);
  }
}

void DictionaryCacheEntry::MakeMetadataEntry(nsCString& aNewValue) {
  MOZ_ASSERT(NS_IsMainThread());
  aNewValue.AppendLiteral("|"), aNewValue.AppendInt(METADATA_VERSION),
      EscapeMetadataString(mHash, aNewValue);
  EscapeMetadataString(mPattern, aNewValue);
  EscapeMetadataString(mId, aNewValue);
  for (auto& dest : mMatchDest) {
    EscapeMetadataString(dom::GetEnumString(dest), aNewValue);
  }
  // List of match-dest values is terminated by an empty string
  EscapeMetadataString(""_ns, aNewValue);
  // Expiration time, as a CString
  nsAutoCStringN<12> expiration;
  expiration = nsPrintfCString("%u", mExpiration);
  EscapeMetadataString(expiration, aNewValue);
  // We don't store type, since we only support type 'raw'  We can support
  // type in the future by considering missing type as raw without changing the
  // format
}

nsresult DictionaryCacheEntry::Write(nsICacheEntry* aCacheEntry) {
  nsAutoCStringN<2048> metadata;
  MakeMetadataEntry(metadata);
  DICTIONARY_LOG(
      ("DictionaryCacheEntry::Write %s %s", mURI.get(), metadata.get()));
  nsresult rv = aCacheEntry->SetMetaDataElement(mURI.get(), metadata.get());
  if (NS_FAILED(rv)) {
    return rv;
  }
  return aCacheEntry->MetaDataReady();
}

nsresult DictionaryCacheEntry::RemoveEntry(nsICacheEntry* aCacheEntry) {
  DICTIONARY_LOG(("RemoveEntry from metadata for %s", mURI.get()));
  nsresult rv = aCacheEntry->SetMetaDataElement(mURI.get(), nullptr);
  if (NS_FAILED(rv)) {
    return rv;
  }
  return aCacheEntry->MetaDataReady();
}

// Parse - | for field seperator; \ for escape of | or \ .
static const char* GetEncodedString(const char* aSrc, nsACString& aOutput) {
  // scan the input string and build the output, handling escapes
  aOutput.Truncate();
  MOZ_ASSERT(*aSrc == '|' || *aSrc == 0);
  if (!aSrc || *aSrc != '|') {
    return aSrc;
  }
  aSrc++;
  while (*aSrc) {
    if (*aSrc == '|') {
      break;
    }
    if (*aSrc == '\\') {
      aSrc++;
    }
    aOutput.Append(*aSrc++);
  }
  return aSrc;
}

// Parse metadata from DictionaryOrigin
bool DictionaryCacheEntry::ParseMetadata(const char* aSrc) {
  MOZ_ASSERT(NS_IsMainThread());
  // Using mHash as a temp for version
  aSrc = GetEncodedString(aSrc, mHash);
  const char* tmp = mHash.get();
  uint32_t version = atoi(tmp);
  if (version != METADATA_VERSION) {
    return false;
  }
  aSrc = GetEncodedString(aSrc, mHash);
  aSrc = GetEncodedString(aSrc, mPattern);
  aSrc = GetEncodedString(aSrc, mId);
  nsAutoCString temp;
  // get match-dest values (list ended with empty string)
  do {
    aSrc = GetEncodedString(aSrc, temp);
    if (!temp.IsEmpty()) {
      dom::RequestDestination dest =
          dom::StringToEnum<dom::RequestDestination>(temp).valueOr(
              dom::RequestDestination::_empty);
      if (dest != dom::RequestDestination::_empty) {
        mMatchDest.AppendElement(dest);
      }
    }
  } while (!temp.IsEmpty());
  if (*aSrc == '|') {
    char* newSrc;
    mExpiration = strtoul(++aSrc, &newSrc, 10);
    aSrc = newSrc;
  }  // else leave default of 0
  // XXX type - we assume and only support 'raw', may be missing
  aSrc = GetEncodedString(aSrc, temp);

  DICTIONARY_LOG(
      ("Parse entry %s: |%s| %s match-dest[0]=%s id=%s", mURI.get(),
       mHash.get(), mPattern.get(),
       mMatchDest.Length() > 0 ? dom::GetEnumString(mMatchDest[0]).get() : "",
       mId.get()));
  return true;
}

void DictionaryCacheEntry::AppendMatchDest(nsACString& aDest) const {
  for (auto& dest : mMatchDest) {
    aDest.Append(dom::GetEnumString(dest));
    aDest.Append(" ");
  }
}

//-----------------------------------------------------------------------------
// nsIStreamListener implementation
//-----------------------------------------------------------------------------

NS_IMETHODIMP
DictionaryCacheEntry::OnStartRequest(nsIRequest* request) {
  DICTIONARY_LOG(("DictionaryCacheEntry %s OnStartRequest", mURI.get()));
  return NS_OK;
}

NS_IMETHODIMP
DictionaryCacheEntry::OnDataAvailable(nsIRequest* request,
                                      nsIInputStream* aInputStream,
                                      uint64_t aOffset, uint32_t aCount) {
  uint32_t n;
  DICTIONARY_LOG(
      ("DictionaryCacheEntry %s OnDataAvailable %u", mURI.get(), aCount));
  return aInputStream->ReadSegments(&DictionaryCacheEntry::ReadCacheData, this,
                                    aCount, &n);
}

/* static */
nsresult DictionaryCacheEntry::ReadCacheData(
    nsIInputStream* aInStream, void* aClosure, const char* aFromSegment,
    uint32_t aToOffset, uint32_t aCount, uint32_t* aWriteCount) {
  DictionaryCacheEntry* self = static_cast<DictionaryCacheEntry*>(aClosure);

  (void)self->mPendingDictionaryData.append(aFromSegment, aCount);
  DICTIONARY_LOG(("Accumulate %p (%s): %d bytes, total %zu", self,
                  self->mURI.get(), aCount,
                  self->mPendingDictionaryData.length()));
  *aWriteCount = aCount;
  return NS_OK;
}

void DictionaryCacheEntry::CleanupOnCacheData(nsresult result) {
  MOZ_ASSERT(NS_IsMainThread());

  DICTIONARY_LOG(("Unsuspending %zu channels", mWaitingPrefetch.size()));

  // if we suspended, un-suspend the channel(s)
  std::vector<PrefetchRequest> callbacks = std::move(mWaitingPrefetch);

  for (auto& request : callbacks) {
    (request.callback)(result);
  }

  // If we have a replacement entry waiting, unsuspend its channels too
  if (mReplacement) {
    DICTIONARY_LOG(("Unsuspending %zu replacement channels",
                    mReplacement->mWaitingPrefetch.size()));
    std::vector<PrefetchRequest> replacementCallbacks =
        std::move(mReplacement->mWaitingPrefetch);

    for (auto& request : replacementCallbacks) {
      (request.callback)(result);
    }
  }

  // If we're being replaced by a new entry, swap now
  RefPtr<DictionaryCacheEntry> self;
  if (mReplacement) {
    DICTIONARY_LOG(("*** Replacing entry %p with %p for %s", this,
                    mReplacement.get(), mURI.get()));
    // Make sure we don't destroy ourselves
    self = this;
    mReplacement->mShouldSuspend = false;
    mOrigin->RemoveEntry(this);
    // When mReplacement gets all it's data, it will be added to mEntries
    mReplacement->UnblockAddEntry(mOrigin);
    mOrigin = nullptr;
  }
}

NS_IMETHODIMP
DictionaryCacheEntry::OnStopRequest(nsIRequest* request, nsresult result) {
  DICTIONARY_LOG(("DictionaryCacheEntry %s OnStopRequest", mURI.get()));

  Vector<uint8_t> pendingData;
  nsCString computedHash;

  if (NS_SUCCEEDED(result)) {
    // Move pending data for validation
    pendingData = std::move(mPendingDictionaryData);
  }

  // Check if loaded data looks compressed (gzip/zstd magic bytes)
  if (MOZ_UNLIKELY(MOZ_LOG_TEST(gDictionaryLog, mozilla::LogLevel::Debug))) {
    if (NS_SUCCEEDED(result) && pendingData.length() >= 4) {
      const uint8_t* b = pendingData.begin();
      if (b[0] == GZIP_MAGIC_0 && b[1] == GZIP_MAGIC_1) {
        DICTIONARY_LOG(
            ("WARNING: Prefetched dictionary data for %s starts with gzip "
             "magic! length=%zu. Cache stored compressed data.",
             mURI.get(), pendingData.length()));
      } else if (b[0] == ZSTD_MAGIC_0 && b[1] == ZSTD_MAGIC_1 &&
                 b[2] == ZSTD_MAGIC_2 && b[3] == ZSTD_MAGIC_3) {
        DICTIONARY_LOG(
            ("WARNING: Prefetched dictionary data for %s starts with zstd "
             "magic! length=%zu. Cache stored compressed data.",
             mURI.get(), pendingData.length()));
      }
    }
  }

  // Calculate hash of loaded data (can be done off-thread)
  if (NS_SUCCEEDED(result) && !pendingData.empty()) {
    nsCOMPtr<nsICryptoHash> hasher =
        do_CreateInstance(NS_CRYPTO_HASH_CONTRACTID);
    if (hasher) {
      hasher->Init(nsICryptoHash::SHA256);
      hasher->Update(pendingData.begin(),
                     static_cast<uint32_t>(pendingData.length()));
      MOZ_ALWAYS_SUCCEEDS(hasher->Finish(true, computedHash));
    }
  }

  // Dispatch to main thread to compare hash and install validated data
  nsCOMPtr<nsIRunnable> runnable = NS_NewRunnableFunction(
      "DictionaryCacheEntry::OnStopRequest",
      [self = RefPtr{this}, result, computedHash = std::move(computedHash),
       pendingData = std::move(pendingData)]() mutable {
        nsresult finalResult = result;
        bool shouldRemoveDictionary = false;

        // Compare computed hash with stored hash on MainThread
        if (NS_SUCCEEDED(finalResult) && !pendingData.empty()) {
          if (!self->mHash.IsEmpty() && !computedHash.Equals(self->mHash)) {
            DICTIONARY_LOG(("Hash mismatch for %s: expected %s, computed %s",
                            self->mURI.get(), self->mHash.get(),
                            computedHash.get()));
            // Log stored Content-Encoding from cache metadata — if non-empty,
            // the dictionary was stored compressed (decompressor not applied)
            if (MOZ_UNLIKELY(
                    MOZ_LOG_TEST(gDictionaryLog, mozilla::LogLevel::Debug))) {
              if (!self->mStoredContentEncoding.IsEmpty()) {
                DICTIONARY_LOG(
                    ("Hash mismatch: stored Content-Encoding was '%s' — "
                     "data is compressed!",
                     self->mStoredContentEncoding.get()));
              }
              // Log first bytes to detect compression format
              if (pendingData.length() >= 4) {
                const uint8_t* b = pendingData.begin();
                DICTIONARY_LOG(
                    ("Hash mismatch data: first bytes 0x%02x 0x%02x 0x%02x "
                     "0x%02x, length %zu %s",
                     b[0], b[1], b[2], b[3], pendingData.length(),
                     (b[0] == 0x1f && b[1] == 0x8b) ? "(GZIP!)"
                     : (b[0] == 0x28 && b[1] == 0xb5 && b[2] == 0x2f &&
                        b[3] == 0xfd)
                         ? "(ZSTD!)"
                         : ""));
              }
            }
            // Report to OHTTP ping with dictionary URI's ETLD+1
            // Only send if at least one waiting request is NOT in private
            // browsing
            bool hasNonPrivateRequest = false;
            for (const auto& request : self->mWaitingPrefetch) {
              if (!request.isPrivateBrowsing) {
                hasNonPrivateRequest = true;
                break;
              }
            }

            if (hasNonPrivateRequest) {
              nsAutoCString site;
              nsCOMPtr<nsIURI> uri;
              if (NS_SUCCEEDED(NS_NewURI(getter_AddRefs(uri), self->mURI))) {
                nsCOMPtr<nsIEffectiveTLDService> eTLDService =
                    do_GetService(NS_EFFECTIVETLDSERVICE_CONTRACTID);
                if (eTLDService) {
                  (void)eTLDService->GetBaseDomain(uri, 0, site);
                }
              }
              if (site.IsEmpty()) {
                site.AssignLiteral("unknown");
              }
              mozilla::glean::network::ContentDecodingErrorReportExtra extra = {
                  .errorType = Some(nsCString("dict_hash_mismatch"_ns)),
                  .topLevelSite = Some(site)};
              glean::network::content_decoding_error_report.Record(Some(extra));
            }

            finalResult = NS_ERROR_CORRUPTED_CONTENT;
            pendingData.clear();
            shouldRemoveDictionary = true;
          } else {
            // Hash matches or no hash to check - install the data
            self->mDictionaryData = std::move(pendingData);
            self->mDictionaryDataComplete = true;
          }
        }

        if (NS_SUCCEEDED(finalResult) && !self->mDictionaryDataComplete) {
          DICTIONARY_LOG(("Zero-byte cache entry for %s", self->mURI.get()));
          finalResult = NS_ERROR_FAILURE;
          shouldRemoveDictionary = true;
        }

        self->CleanupOnCacheData(finalResult);
        self->mStopReceived = true;
        if (shouldRemoveDictionary) {
          // Already on MainThread
          DictionaryCache::RemoveDictionary(self->mURI,
                                            self->GetLoadContextInfo());
        }
      });
  NS_DispatchToMainThread(runnable);

  return result;
}

void DictionaryCacheEntry::UnblockAddEntry(DictionaryOrigin* aOrigin) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!mHash.IsEmpty()) {
    // Already done, we can move to mEntries now
    aOrigin->FinishAddEntry(this);
  }
  mBlocked = false;
}

void DictionaryCacheEntry::SetOrigin(DictionaryOrigin* aOrigin) {
  mOrigin = aOrigin;
  mLoadContextInfo = aOrigin ? aOrigin->GetLoadContextInfo() : nullptr;
}

void DictionaryCacheEntry::WriteOnHash() {
  MOZ_ASSERT(NS_IsMainThread());
  if (!mHash.IsEmpty() && mOrigin) {
    DICTIONARY_LOG(("Write already hashed"));
    mOrigin->Write(this);
  }
}

//-----------------------------------------------------------------------------
// nsICacheEntryOpenCallback implementation
//-----------------------------------------------------------------------------
// Note: we don't care if the entry is stale since we're not loading it; we're
// just saying with have this specific set of bits with this hash available
// to use as a dictionary.

// This may be called on a random thread due to
// nsICacheStorage::CHECK_MULTITHREADED
NS_IMETHODIMP
DictionaryCacheEntry::OnCacheEntryCheck(nsICacheEntry* aEntry,
                                        uint32_t* result) {
  DICTIONARY_LOG(("OnCacheEntryCheck %p", this));
  *result = nsICacheEntryOpenCallback::ENTRY_WANTED;
  return NS_OK;
}

// This may be called on a random thread due to
// nsICacheStorage::CHECK_MULTITHREADED
NS_IMETHODIMP
DictionaryCacheEntry::OnCacheEntryAvailable(nsICacheEntry* entry, bool isNew,
                                            nsresult status) {
  DICTIONARY_LOG(("OnCacheEntryAvailable %p, result %u, entry %p", this,
                  (uint32_t)status, entry));
  if (entry) {
    if (MOZ_UNLIKELY(MOZ_LOG_TEST(gDictionaryLog, mozilla::LogLevel::Debug))) {
      // Check if the cache entry still has Content-Encoding in its stored
      // response headers. If so, the data on disk is likely still compressed
      // (the decompressor wasn't applied before saving).
      nsCString responseHead;
      if (NS_SUCCEEDED(entry->GetMetaDataElement(
              "response-head", getter_Copies(responseHead)))) {
        // Extract Content-Encoding value from stored response headers
        auto ceStart = responseHead.Find("Content-Encoding:"_ns);
        if (ceStart != kNotFound) {
          auto valueStart = ceStart + 17;  // strlen("Content-Encoding:")
          auto lineEnd = responseHead.FindChar('\n', valueStart);
          if (lineEnd == kNotFound) {
            lineEnd = responseHead.Length();
          }
          mStoredContentEncoding =
              Substring(responseHead, valueStart, lineEnd - valueStart);
          mStoredContentEncoding.Trim(" \t\r");
          DICTIONARY_LOG(
              ("WARNING: Cache entry for dictionary %s has stored "
               "Content-Encoding: '%s'. Data is likely compressed!",
               mURI.get(), mStoredContentEncoding.get()));
        }
      }
    }

    nsCOMPtr<nsIInputStream> stream;
    entry->OpenInputStream(0, getter_AddRefs(stream));
    if (!stream) {
      DICTIONARY_LOG(("OpenInputStream failed for %s", mURI.get()));
      nsCOMPtr<nsIRunnable> runnable =
          NS_NewRunnableFunction("DictionaryCacheEntry::OnCacheEntryAvailable",
                                 [self = RefPtr{this}]() {
                                   self->CleanupOnCacheData(NS_ERROR_FAILURE);
                                   DictionaryCache::RemoveDictionary(
                                       self->mURI, self->GetLoadContextInfo());
                                 });
      NS_DispatchToMainThread(runnable);
      return NS_OK;
    }

    // nsInputStreamPump supports off-main-thread use: when constructed
    // off-thread it sets mOffMainThread and targets the calling thread's
    // serial event target, so callbacks fire on whatever thread calls
    // AsyncRead.
    RefPtr<nsInputStreamPump> pump;
    nsresult rv = nsInputStreamPump::Create(getter_AddRefs(pump), stream);
    if (NS_FAILED(rv)) {
      DICTIONARY_LOG(("nsInputStreamPump::Create failed for %s", mURI.get()));
      NS_DispatchToMainThread(
          NS_NewRunnableFunction("DictionaryCacheEntry::OnCacheEntryAvailable",
                                 [self = RefPtr{this}]() {
                                   self->CleanupOnCacheData(NS_ERROR_FAILURE);
                                   DictionaryCache::RemoveDictionary(
                                       self->mURI, self->GetLoadContextInfo());
                                 }));
      return NS_OK;
    }
    rv = pump->AsyncRead(this);
    if (NS_FAILED(rv)) {
      DICTIONARY_LOG(("AsyncRead failed for %s", mURI.get()));
      NS_DispatchToMainThread(
          NS_NewRunnableFunction("DictionaryCacheEntry::OnCacheEntryAvailable",
                                 [self = RefPtr{this}]() {
                                   self->CleanupOnCacheData(NS_ERROR_FAILURE);
                                   DictionaryCache::RemoveDictionary(
                                       self->mURI, self->GetLoadContextInfo());
                                 }));
      return NS_OK;
    }
    DICTIONARY_LOG(("Waiting for data"));
  } else {
    // Error out any channels waiting on this cache entry.  Also,
    // remove the dictionary entry from the origin.
    mNotCached = true;  // For Prefetch()
    DICTIONARY_LOG(("Prefetched cache entry not available!!!"));

    // Dispatch cleanup to main thread
    nsCString uriCopy = mURI;
    nsCOMPtr<nsIRunnable> runnable = NS_NewRunnableFunction(
        "DictionaryCacheEntry::OnCacheEntryAvailable",
        [self = RefPtr{this}, uriCopy]() {
          self->CleanupOnCacheData(NS_ERROR_CORRUPTED_CONTENT);
          DictionaryCache::RemoveDictionary(self->mURI,
                                            self->GetLoadContextInfo());
        });
    NS_DispatchToMainThread(runnable);
  }

  return NS_OK;
}

//----------------------------------------------------------------------------------

// Read the metadata for an Origin and parse it, creating DictionaryCacheEntrys
// as needed. If aType is TYPE_OTHER, there is no Match() to do
void DictionaryOriginReader::Start(
    bool aCreate, DictionaryOrigin* aOrigin, nsACString& aKey, nsIURI* aURI,
    ExtContentPolicyType aType, nsILoadContextInfo* aLoadContextInfo,
    DictionaryCache* aCache,
    const std::function<nsresult(bool, DictionaryCacheEntry*)>& aCallback) {
  mOrigin = aOrigin;
  mURI = aURI;
  mType = aType;
  mCallback = aCallback;
  mCache = aCache;

  AUTO_PROFILER_FLOW_MARKER("DictionaryOriginReader::Start", NETWORK,
                            Flow::FromPointer(this));

  // The cache entry is for originattribute extension of
  // META_DICTIONARY_PREFIX, plus key of prepath

  // This also keeps this alive until we get the callback.  We must do this
  // BEFORE we call AsyncOpenURIString, or we may get a callback to
  // OnCacheEntryAvailable before we can do this
  mOrigin->mWaitingCacheRead.AppendElement(this);
  if (mOrigin->mWaitingCacheRead.Length() == 1) {  // was empty
    DICTIONARY_LOG(("DictionaryOriginReader::Start(%s): %p",
                    PromiseFlatCString(aKey).get(), this));
    nsCOMPtr<nsICacheStorage> storage =
        DictionaryCache::GetCacheStorage(aLoadContextInfo);
    if (!storage) {
      mOrigin->mWaitingCacheRead.Clear();
      (aCallback)(true, nullptr);
      return;
    }
    storage->AsyncOpenURIString(aKey, META_DICTIONARY_PREFIX,
                                aCreate
                                    ? nsICacheStorage::OPEN_NORMALLY |
                                          nsICacheStorage::CHECK_MULTITHREADED
                                    : nsICacheStorage::OPEN_READONLY |
                                          nsICacheStorage::OPEN_SECRETLY |
                                          nsICacheStorage::CHECK_MULTITHREADED,
                                this);
    // This one will get the direct callback to do Match()
  }
  // Else we already have a read for this cache entry pending, just wait
  // for that
}

void DictionaryOriginReader::FinishMatch() {
  RefPtr<DictionaryCacheEntry> result;
  // Don't Match if this was a call from AddEntry()
  if (mType != ExtContentPolicy::TYPE_OTHER) {
    nsCString path;
    mURI->GetPathQueryRef(path);
    result = mOrigin->Match(path, mType);
  }
  DICTIONARY_LOG(("Done with reading origin for %p", mOrigin.get()));
  (mCallback)(true, result);
}

NS_IMPL_ISUPPORTS(DictionaryOriginReader, nsICacheEntryOpenCallback,
                  nsIStreamListener)

//-----------------------------------------------------------------------------
// nsICacheEntryOpenCallback implementation
//-----------------------------------------------------------------------------

// This may be called on a random thread due to
// nsICacheStorage::CHECK_MULTITHREADED
NS_IMETHODIMP DictionaryOriginReader::OnCacheEntryCheck(nsICacheEntry* entry,
                                                        uint32_t* result) {
  *result = nsICacheEntryOpenCallback::ENTRY_WANTED;
  DICTIONARY_LOG(
      ("DictionaryOriginReader::OnCacheEntryCheck this=%p for entry %p", this,
       entry));
  return NS_OK;
}

NS_IMETHODIMP DictionaryOriginReader::OnCacheEntryAvailable(
    nsICacheEntry* aCacheEntry, bool isNew, nsresult result) {
  MOZ_ASSERT(NS_IsMainThread(), "Got cache entry off main thread!");
  DICTIONARY_LOG(
      ("DictionaryOriginReader::OnCacheEntryAvailable this=%p for entry %p",
       this, aCacheEntry));

  if (!aCacheEntry) {
    // Didn't have any dictionaries for this origin, and must have been readonly
    for (auto& reader : mOrigin->mWaitingCacheRead) {
      (reader->mCallback)(true, nullptr);
    }
    mOrigin->mWaitingCacheRead.Clear();
    AUTO_PROFILER_TERMINATING_FLOW_MARKER(
        "DictionaryOriginReader::OnCacheEntryAvailable", NETWORK,
        Flow::FromPointer(this));
    return NS_OK;
  }

  mOrigin->SetCacheEntry(aCacheEntry);
  AUTO_PROFILER_FLOW_MARKER("DictionaryOriginReader::VisitMetaData", NETWORK,
                            Flow::FromPointer(this));
  bool empty = false;
  aCacheEntry->GetIsEmpty(&empty);
  if (!empty) {
    // There's no data in the cache entry, just metadata
    nsCOMPtr<nsICacheEntryMetaDataVisitor> metadata(mOrigin);
    aCacheEntry->VisitMetaData(metadata);
  }  // else new cache entry

  // This list is the only thing keeping us alive
  RefPtr<DictionaryOriginReader> safety(this);
  for (auto& reader : mOrigin->mWaitingCacheRead) {
    reader->FinishMatch();
  }
  mOrigin->mWaitingCacheRead.Clear();
  AUTO_PROFILER_TERMINATING_FLOW_MARKER(
      "DictionaryOriginReader::OnCacheEntryAvailable", NETWORK,
      Flow::FromPointer(this));
  return NS_OK;
}

//-----------------------------------------------------------------------------
// nsIStreamListener implementation
//-----------------------------------------------------------------------------

NS_IMETHODIMP
DictionaryOriginReader::OnStartRequest(nsIRequest* request) {
  DICTIONARY_LOG(("DictionaryOriginReader %p OnStartRequest", this));
  return NS_OK;
}

NS_IMETHODIMP
DictionaryOriginReader::OnDataAvailable(nsIRequest* request,
                                        nsIInputStream* aInputStream,
                                        uint64_t aOffset, uint32_t aCount) {
  DICTIONARY_LOG(
      ("DictionaryOriginReader %p OnDataAvailable %u", this, aCount));
  return NS_OK;
}

NS_IMETHODIMP
DictionaryOriginReader::OnStopRequest(nsIRequest* request, nsresult result) {
  DICTIONARY_LOG(("DictionaryOriginReader %p OnStopRequest", this));
  return NS_OK;
}

// static
already_AddRefed<DictionaryCache> DictionaryCache::GetInstance() {
  MOZ_ASSERT(NS_IsMainThread());
  // Return nullptr if shutdown has occurred to prevent resurrection
  if (sShutdown) {
    return nullptr;
  }
  if (!gDictionaryCache) {
    gDictionaryCache = new DictionaryCache();
  }
  return do_AddRef(gDictionaryCache);
}

nsresult DictionaryCache::Init() {
  if (XRE_IsParentProcess()) {
    nsCOMPtr<nsICacheStorageService> cacheStorageService(
        components::CacheStorage::Service());
    if (!cacheStorageService) {
      return NS_ERROR_FAILURE;
    }

    nsCOMPtr<nsIObserverService> obsService =
        mozilla::services::GetObserverService();
    if (obsService) {
      obsService->AddObserver(this, "idle-daily", false);
    }
  }
  DICTIONARY_LOG(("Inited DictionaryCache"));
  return NS_OK;
}

void DictionaryCache::Shutdown() {
  DICTIONARY_LOG(("DictionaryCache::Shutdown"));
  sShutdown = true;
  if (XRE_IsParentProcess()) {
    nsCOMPtr<nsIObserverService> obsService =
        mozilla::services::GetObserverService();
    if (obsService && gDictionaryCache) {
      obsService->RemoveObserver(gDictionaryCache.get(), "idle-daily");
    }
  }
  gDictionaryCache = nullptr;
}

NS_IMETHODIMP
DictionaryCache::Observe(nsISupports* subject, const char* topic,
                         const char16_t* data) {
  MOZ_ASSERT(NS_IsMainThread());
  if (!strcmp(topic, "idle-daily")) {
    // Submit the content decoding error ping once per day
    glean_pings::ContentDecodingError.Submit();
  }
  return NS_OK;
}

//-----------------------------------------------------------------------------
// DictionaryCache::nsISupports
//-----------------------------------------------------------------------------

NS_IMPL_ISUPPORTS(DictionaryCache, nsIObserver)

nsresult DictionaryCache::AddEntry(
    nsIURI* aURI, const nsACString& aKey, const nsACString& aPattern,
    nsTArray<nsCString>& aMatchDest, const nsACString& aId,
    const Maybe<nsCString>& aHash, bool aNewEntry, uint32_t aExpiration,
    nsILoadContextInfo* aLoadContextInfo, DictionaryCacheEntry** aDictEntry) {
  // Note that normally we're getting an entry in and until all the data
  // has been received, we can't use it.  The Hash being null is a flag
  // that it's not yet valid.
  DICTIONARY_LOG(("AddEntry for %s, pattern %s, id %s, expiration %u",
                  PromiseFlatCString(aKey).get(),
                  PromiseFlatCString(aPattern).get(),
                  PromiseFlatCString(aId).get(), aExpiration));
  // Note that we don't know if there's an entry for this key in the origin
  RefPtr<DictionaryCacheEntry> dict = new DictionaryCacheEntry(
      aKey, aPattern, aMatchDest, aId, aExpiration, aHash);
  dict = AddEntry(aURI, aNewEntry, aLoadContextInfo, dict);
  if (dict) {
    *aDictEntry = do_AddRef(dict).take();
    return NS_OK;
  }
  DICTIONARY_LOG(
      ("Failed adding entry for %s", PromiseFlatCString(aKey).get()));
  *aDictEntry = nullptr;
  return NS_ERROR_FAILURE;
}

already_AddRefed<DictionaryCacheEntry> DictionaryCache::AddEntry(
    nsIURI* aURI, bool aNewEntry, nsILoadContextInfo* aLoadContextInfo,
    DictionaryCacheEntry* aDictEntry) {
  // Note that normally we're getting an entry in and until all the data
  // has been received, we can't use it.  The Hash being null is a flag
  // that it's not yet valid.
  nsCString prepath;
  if (NS_FAILED(GetDictPath(aURI, prepath))) {
    return nullptr;
  }
  nsCString mapKey;
  MakeCacheKey(prepath, aLoadContextInfo, mapKey);
  DICTIONARY_LOG(("AddEntry: %s, %d, %p", mapKey.get(), aNewEntry, aDictEntry));
  // SetOrigin() below only runs when the origin is newly created.
  aDictEntry->SetLoadContextInfo(aLoadContextInfo);
  // create for the origin if it doesn't exist
  RefPtr<DictionaryCacheEntry> newEntry;
  (void)mDictionaryCache.WithEntryHandle(mapKey, [&](auto&& entry) {
    auto& origin = entry.OrInsertWith([&] {
      RefPtr<DictionaryOrigin> origin =
          new DictionaryOrigin(prepath, nullptr, aLoadContextInfo);
      origin->SetMapKey(mapKey);
      // Create a cache entry for this if it doesn't exist.  Note
      // that the entry we're adding will need to be saved later once
      // we have the cache entry

      // This creates a cycle until the dictionary is removed from the cache
      aDictEntry->SetOrigin(origin);
      DICTIONARY_LOG(("Creating cache entry for origin %s", mapKey.get()));

      // Open (and parse metadata) or create
      RefPtr<DictionaryOriginReader> reader = new DictionaryOriginReader();
      // the type is irrelevant; we won't be calling Match()
      reader->Start(
          true, origin, prepath, aURI, ExtContentPolicy::TYPE_OTHER,
          aLoadContextInfo, this,
          [entry = RefPtr(aDictEntry)](
              bool, DictionaryCacheEntry* aDict) {  // XXX avoid so many lambdas
                                                    // which cause allocations
            // Write the dirty entry we couldn't write before once
            // we get the hash
            entry->WriteOnHash();
            return NS_OK;
          });
      // Since this is read asynchronously, we need to either add the entry
      // async once the read is done and it's populated, or we have to handle
      // collisions on the read
      return origin;
    });

    newEntry = origin->AddEntry(aDictEntry, aNewEntry);
    DICTIONARY_LOG(("AddEntry: added %s", mapKey.get()));
    return NS_OK;
  });
  return newEntry.forget();
}

nsresult DictionaryCache::RemoveEntry(nsIURI* aURI, const nsACString& aKey,
                                      nsILoadContextInfo* aLoadContextInfo) {
  nsCString prepath;
  if (NS_FAILED(GetDictPath(aURI, prepath))) {
    return NS_ERROR_FAILURE;
  }
  nsCString mapKey;
  MakeCacheKey(prepath, aLoadContextInfo, mapKey);
  DICTIONARY_LOG(("DictionaryCache::RemoveEntry for %s : %s", mapKey.get(),
                  PromiseFlatCString(aKey).get()));
  if (auto origin = mDictionaryCache.Lookup(mapKey)) {
    return origin.Data()->RemoveEntry(aKey);
  }
  return NS_ERROR_FAILURE;
}

void DictionaryCache::Clear() {
  // There may be active Prefetch()es running, note, and active fetches
  // using dictionaries.  They will stay alive until the channels using
  // them go away.  However, no new requests will see them.

  // This can be used to make the cache return to the state it has at
  // startup, especially useful for tests.
  mDictionaryCache.Clear();
}

void DictionaryCache::CorruptHashForTesting(
    const nsACString& aURI, nsILoadContextInfo* aLoadContextInfo) {
  DICTIONARY_LOG(("DictionaryCache::CorruptHashForTesting for %s",
                  PromiseFlatCString(aURI).get()));
  nsCOMPtr<nsIURI> uri;
  if (NS_FAILED(NS_NewURI(getter_AddRefs(uri), aURI))) {
    return;
  }
  nsAutoCString prepath;
  if (NS_FAILED(GetDictPath(uri, prepath))) {
    return;
  }
  nsCString mapKey;
  MakeCacheKey(prepath, aLoadContextInfo, mapKey);
  if (auto origin = mDictionaryCache.Lookup(mapKey)) {
    for (auto& entry : origin.Data()->mEntries) {
      if (entry->GetURI().Equals(aURI)) {
        DICTIONARY_LOG(("Corrupting hash for %s",
                        PromiseFlatCString(entry->GetURI()).get()));
        entry->SetHash("CORRUPTED_FOR_TESTING"_ns);
        return;
      }
    }
    for (auto& entry : origin.Data()->mPendingEntries) {
      if (entry->GetURI().Equals(aURI)) {
        DICTIONARY_LOG(("Corrupting hash for %s (pending)",
                        PromiseFlatCString(entry->GetURI()).get()));
        entry->SetHash("CORRUPTED_FOR_TESTING"_ns);
        return;
      }
    }
  }
}

void DictionaryCache::ClearDictionaryDataForTesting(
    const nsACString& aURI, nsILoadContextInfo* aLoadContextInfo) {
  DICTIONARY_LOG(("DictionaryCache::ClearDictionaryDataForTesting for %s",
                  PromiseFlatCString(aURI).get()));
  nsCOMPtr<nsIURI> uri;
  if (NS_FAILED(NS_NewURI(getter_AddRefs(uri), aURI))) {
    return;
  }
  nsAutoCString prepath;
  if (NS_FAILED(GetDictPath(uri, prepath))) {
    return;
  }
  nsCString mapKey;
  MakeCacheKey(prepath, aLoadContextInfo, mapKey);
  if (auto origin = mDictionaryCache.Lookup(mapKey)) {
    for (auto& entry : origin.Data()->mEntries) {
      if (entry->GetURI().Equals(aURI)) {
        DICTIONARY_LOG(("Clearing data for %s",
                        PromiseFlatCString(entry->GetURI()).get()));
        entry->ClearDataForTesting();
        return;
      }
    }
  }
}

// Remove a dictionary if it exists for the key given
// static
void DictionaryCache::RemoveDictionaryOMT(
    const nsACString& aKey, nsILoadContextInfo* aLoadContextInfo) {
  DICTIONARY_LOG(
      ("Removing dictionary for %s", PromiseFlatCString(aKey).get()));
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "DictionaryCache::RemoveDictionaryOMT",
      [key = nsCString(aKey),
       lci = nsCOMPtr<nsILoadContextInfo>(aLoadContextInfo)]() {
        DictionaryCache::RemoveDictionary(key, lci);
      }));
}

// Remove a dictionary if it exists for the key given
// static
void DictionaryCache::RemoveDictionary(const nsACString& aKey,
                                       nsILoadContextInfo* aLoadContextInfo) {
  MOZ_ASSERT(NS_IsMainThread());
  DICTIONARY_LOG(
      ("Removing dictionary for %s", PromiseFlatCString(aKey).get()));

  RefPtr<DictionaryCache> cache = GetInstance();
  if (!cache) {
    // Shutdown has occurred, cannot remove dictionary
    return;
  }
  nsCOMPtr<nsIURI> uri;
  if (NS_FAILED(NS_NewURI(getter_AddRefs(uri), aKey))) {
    return;
  }
  nsAutoCString prepath;
  if (NS_SUCCEEDED(GetDictPath(uri, prepath))) {
    nsCString mapKey;
    MakeCacheKey(prepath, aLoadContextInfo, mapKey);
    if (auto origin = cache->mDictionaryCache.Lookup(mapKey)) {
      origin.Data()->RemoveEntry(aKey);
    }
  }
}

// static
void DictionaryCache::RemoveOriginFor(const nsACString& aKey,
                                      nsILoadContextInfo* aLoadContextInfo) {
  // Called off the main thread when entries are doomed, but gDictionaryCache
  // must only be accessed on the main thread.
  if (sShutdown) {
    return;
  }
  DICTIONARY_LOG(
      ("Removing dictionary origin %s", PromiseFlatCString(aKey).get()));
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "DictionaryCache::RemoveOriginFor",
      [key = nsCString(aKey),
       lci = nsCOMPtr<nsILoadContextInfo>(aLoadContextInfo)]() {
        // Don't create the cache just to look for an origin in it.
        RefPtr<DictionaryCache> cache = gDictionaryCache.get();
        if (cache && !sShutdown) {
          cache->RemoveOriginForInternal(key, lci);
        }
      }));
}

// Remove a dictionary if it exists for the key given, if it's empty
void DictionaryCache::RemoveOriginForInternal(
    const nsACString& aKey, nsILoadContextInfo* aLoadContextInfo) {
  nsCOMPtr<nsIURI> uri;
  if (NS_FAILED(NS_NewURI(getter_AddRefs(uri), aKey))) {
    return;
  }
  nsAutoCString prepath;
  if (NS_SUCCEEDED(GetDictPath(uri, prepath))) {
    nsCString mapKey;
    MakeCacheKey(prepath, aLoadContextInfo, mapKey);
    if (auto origin = mDictionaryCache.Lookup(mapKey)) {
      if (MOZ_UNLIKELY(origin.Data()->IsEmpty())) {
        DICTIONARY_LOG(
            ("Removing origin for %s", PromiseFlatCString(aKey).get()));
        // This removes it from the global list and also dooms the entry
        origin.Data()->Clear();
      } else {
        DICTIONARY_LOG(
            ("Origin not empty: %s", PromiseFlatCString(aKey).get()));
      }
    }
  }
}

// Remove the entry for the map key given (see DictionaryOrigin::mMapKey)
void DictionaryCache::RemoveOrigin(const nsACString& aMapKey) {
  mDictionaryCache.Remove(aMapKey);
}

// Remove a dictionary if it exists for the key given.  Mainthread only.
// Note: due to cookie samesite rules, we need to clean for all ports!
// static
void DictionaryCache::RemoveDictionariesForOrigin(nsIURI* aURI) {
  // There's no PrePathNoPort()
  nsAutoCString temp;
  aURI->GetScheme(temp);
  nsCString origin(temp);
  aURI->GetUserPass(temp);
  origin += "://"_ns + temp;
  aURI->GetHost(temp);
  origin += temp;

  DICTIONARY_LOG(("Removing all dictionaries for origin of %s (%zu)",
                  PromiseFlatCString(origin).get(), origin.Length()));
  RefPtr<DictionaryCache> cache = GetInstance();
  if (!cache) {
    // Shutdown has occurred, cannot remove dictionaries
    return;
  }
  // We can't just use Remove here; the ClearSiteData service strips the
  // port.  We need to clear all that match the host with any port or none.

  // Keep an array of origins to clear since that will want to modify the
  // hash table we're iterating
  AutoTArray<RefPtr<DictionaryOrigin>, 1> toClear;
  for (auto& entry : cache->mDictionaryCache) {
    // We need to drop any port from entry (and origin).  Assuming they're
    // the same up to the / or : in mOrigin, we want to limit the host
    // there.  We also know that entry is https://.
    // Verify that:
    // a) they're equal to that point
    // b) that the next character of mOrigin is '/' or ':', which avoids
    //    issues like matching https://foo.bar to (mOrigin)
    //    https://foo.barsoom.com:666/
    DICTIONARY_LOG(
        ("Possibly removing dictionary origin for %s (vs %s), %zu vs %zu",
         entry.GetData()->mOrigin.get(), PromiseFlatCString(origin).get(),
         entry.GetData()->mOrigin.Length(), origin.Length()));
    if (entry.GetData()->mOrigin.Length() > origin.Length() &&
        (entry.GetData()->mOrigin[origin.Length()] == '/' ||   // no port
         entry.GetData()->mOrigin[origin.Length()] == ':')) {  // port
      // no strncmp() for nsCStrings...
      nsDependentCSubstring host =
          Substring(entry.GetData()->mOrigin, 0,
                    origin.Length());  // not including '/' or ':'
      DICTIONARY_LOG(("Compare %s vs %s", entry.GetData()->mOrigin.get(),
                      PromiseFlatCString(host).get()));
      if (origin.Equals(host)) {
        DICTIONARY_LOG(
            ("RemoveDictionaries: Removing dictionary origin %p for %s",
             entry.GetData().get(), entry.GetData()->mOrigin.get()));
        toClear.AppendElement(entry.GetData());
      }
    }
  }
  // Now clear and doom all the entries
  for (auto& entry : toClear) {
    entry->Clear();
  }
}

// Remove a dictionary if it exists for the key given.  Mainthread only
// static
void DictionaryCache::RemoveAllDictionaries() {
  RefPtr<DictionaryCache> cache = GetInstance();
  if (!cache) {
    // Shutdown has occurred, cannot remove dictionaries
    return;
  }

  DICTIONARY_LOG(("Removing all dictionaries"));
  // Clear contents of all origins without calling DictionaryOrigin::Clear()
  // which would try to modify the hashtable during iteration (causing
  // reentrancy assertion in PLDHashTable).
  // Note: This duplicates logic from DictionaryOrigin::Clear() but avoids
  // the RemoveOrigin() call during iteration. For selective removal, see
  // RemoveDictionariesForOrigin() which uses a collect-then-clear pattern.
  AutoTArray<nsCOMPtr<nsICacheEntry>, 8> entriesToDoom;
  for (auto& origin : cache->mDictionaryCache) {
    DictionaryOrigin* originPtr = origin.GetData();
    DICTIONARY_LOG(("*** Clearing origin %s", originPtr->mOrigin.get()));
    originPtr->mEntries.Clear();
    originPtr->mPendingEntries.Clear();
    originPtr->mPendingRemove.Clear();
    if (originPtr->mEntry) {
      entriesToDoom.AppendElement(originPtr->mEntry);
    }
  }
  // Doom all cache entries asynchronously in one task
  if (!entriesToDoom.IsEmpty()) {
    NS_DispatchBackgroundTask(NS_NewRunnableFunction(
        "DictionaryOrigin::ClearAll", [entries = std::move(entriesToDoom)]() {
          DICTIONARY_LOG(("*** Dooming %zu entries", entries.Length()));
          for (auto& entry : entries) {
            entry->AsyncDoom(nullptr);
          }
        }));
  }
  // Now clear the hashtable after iteration is complete
  cache->mDictionaryCache.Clear();
}

// Return an entry via a callback (async).
// If we don't have the origin in-memory, ask the cache for the origin, and
// when we get it, parse the metadata to build a DictionaryOrigin.
// Once we have a DictionaryOrigin (in-memory or parsed), scan it for matches.
// If it's not in the cache, return nullptr via callback.
void DictionaryCache::GetDictionaryFor(
    nsIURI* aURI, ExtContentPolicyType aType,
    nsILoadContextInfo* aLoadContextInfo, nsHttpChannel* aChan,
    void (*aSuspend)(nsHttpChannel*),
    const std::function<nsresult(bool, DictionaryCacheEntry*)>& aCallback) {
  // Note: IETF 2.2.3 Multiple Matching Directories
  // We need to return match-dest matches first
  // If no match-dest, then the longest match
  nsCString prepath;
  if (NS_FAILED(GetDictPath(aURI, prepath))) {
    (aCallback)(false, nullptr);
    return;
  }
  nsCString mapKey;
  MakeCacheKey(prepath, aLoadContextInfo, mapKey);
  // Match immediately if we've already created the origin and read any
  // metadata
  if (auto existing = mDictionaryCache.Lookup(mapKey)) {
    if (existing.Data()->mWaitingCacheRead.IsEmpty()) {
      // Find the longest match
      nsCString path;
      RefPtr<DictionaryCacheEntry> result;

      aURI->GetSpec(path);
      DICTIONARY_LOG(("GetDictionaryFor(%s %s)", mapKey.get(), path.get()));

      result = existing.Data()->Match(path, aType);
      (aCallback)(false, result);
    } else {
      DICTIONARY_LOG(
          ("GetDictionaryFor(%s): Waiting for metadata read to match",
           mapKey.get()));
      // Wait for the metadata read to complete
      RefPtr<DictionaryOriginReader> reader = new DictionaryOriginReader();
      // Must do this before calling start, which can run the callbacks and call
      // Resume
      DICTIONARY_LOG(("Suspending to get Dictionary headers"));
      aSuspend(aChan);
      reader->Start(false, existing.Data(), prepath, aURI, aType,
                    aLoadContextInfo, this, aCallback);
    }
    return;
  }
  // We don't have an entry at all.  We need to check if there's an entry
  // on disk for <origin>, unless we know we have all entries in the memory
  // cache.

  // Handle unknown origins by checking the disk cache
  nsCOMPtr<nsICacheStorage> storage = GetCacheStorage(aLoadContextInfo);
  if (!storage) {
    (aCallback)(false, nullptr);  // in case we have no disk storage
    return;
  }

  // Sync check to see if the entry exists
  bool exists;
  nsCOMPtr<nsIURI> prepathURI;

  if (NS_SUCCEEDED(NS_NewURI(getter_AddRefs(prepathURI), prepath)) &&
      NS_SUCCEEDED(
          storage->Exists(prepathURI, META_DICTIONARY_PREFIX, &exists)) &&
      exists) {
    // To keep track of the callback, we need a new object to get the
    // OnCacheEntryAvailable can resolve the callback.
    DICTIONARY_LOG(("Reading %s for dictionary entries", mapKey.get()));
    RefPtr<DictionaryOrigin> origin =
        new DictionaryOrigin(prepath, nullptr, aLoadContextInfo);
    origin->SetMapKey(mapKey);
    // Add the origin to the list; we'll immediately start a reader which
    // will set mWaitingCacheRead, so future GetDictionaryFor() calls
    // will wait for the metadata to be read before doing Match()
    mDictionaryCache.InsertOrUpdate(mapKey, origin);

    RefPtr<DictionaryOriginReader> reader = new DictionaryOriginReader();
    // After Start(), if we drop this ref reader will kill itself on
    // completion; it holds a self-ref
    DICTIONARY_LOG(("Suspending to get Dictionary headers"));
    aSuspend(aChan);
    reader->Start(false, origin, prepath, aURI, aType, aLoadContextInfo, this,
                  aCallback);
    return;
  }
  // No dictionaries for origin
  (aCallback)(false, nullptr);
}

//-----------------------------------------------------------------------------
// DictionaryOrigin
//-----------------------------------------------------------------------------
NS_IMPL_ISUPPORTS(DictionaryOrigin, nsICacheEntryMetaDataVisitor)

nsresult DictionaryOrigin::Write(DictionaryCacheEntry* aDictEntry) {
  DICTIONARY_LOG(("DictionaryOrigin::Write %s %p", mOrigin.get(), aDictEntry));
  if (mEntry) {
    return aDictEntry->Write(mEntry);
  }
  // Write it once DictionaryOriginReader creates the entry
  mDeferredWrites = true;
  return NS_OK;
}

void DictionaryOrigin::SetCacheEntry(nsICacheEntry* aEntry) {
  mEntry = aEntry;
  mEntry->SetContentType(nsICacheEntry::CONTENT_TYPE_DICTIONARY);
  if (mDeferredWrites) {
    // RemoveEntry() mutates mEntries, so defer removals.
    DictCacheList remove;
    for (auto& entry : mEntries) {
      if (NS_FAILED(Write(entry))) {
        remove.AppendElement(entry);
      }
    }
    for (auto& entry : remove) {
      RemoveEntry(entry);
    }
  }
  mDeferredWrites = false;
  // Handle removes that were pending
  for (auto& remove : mPendingRemove) {
    DICTIONARY_LOG(("Pending RemoveEntry for %s", remove->mURI.get()));
    remove->RemoveEntry(mEntry);
  }
  mPendingRemove.Clear();
}

already_AddRefed<DictionaryCacheEntry> DictionaryOrigin::AddEntry(
    DictionaryCacheEntry* aDictEntry, bool aNewEntry) {
  // Remove any entry for the same item
  for (size_t i = 0; i < mEntries.Length(); i++) {
    if (mEntries[i]->GetURI().Equals(aDictEntry->GetURI())) {
      DictionaryCacheEntry* oldEntry = mEntries[i];
      if (aNewEntry) {
        // We're overwriting an existing entry, perhaps with a new hash.  It
        // might be different, of course.
        // Until we receive and save the new data, we should use the old data.

        // We need to pause this channel, regardless of how it's encoded,
        // until the entry we're replacing has either no users, or has data
        // read in from the cache.  Then we can un-Suspend and start
        // replacing the data in the cache itself.  If there are no current
        // users, and we start replacing the data, we need to remove the
        // old entry so no one tries to use the old data/hash for a new
        // request.

        // Note that when we start replacing data in the cache we need to
        // also remove it from the origin's entry in the cache, in case we
        // exit or crash before we finish replacing the entry and updating
        // the origin's entry with the new hash.

        // Once we've replaced the entry (which will be after we have
        // hash), new requests will use the new data/hash.  I.e. we'll
        // still allow new requests to use the old cache data/hash until
        // the swap occurs.  Once the swap happens, the channels using the
        // old data/hash will still have an mDictDecoding reference to the
        // DictionaryCacheEntry for the old data/hash.

        // XXX possible edge case: if a second request to replace the
        // entry appears. Is this possible, or would the second request
        // for the same URI get subsumed into the older one still in
        // process? I'm guessing it doesn't, so we may need to deal with this

        DICTIONARY_LOG((
            "Replacing dictionary %p for %s: new will be %p", mEntries[i].get(),
            PromiseFlatCString(oldEntry->GetURI()).get(), oldEntry));
        // May be overkill to check HasHash here
        if (mEntries[i]->IsReading() && !aDictEntry->HasHash()) {
          DICTIONARY_LOG(("Old entry is reading data"));
          // If the old entry doesn't already have the data from the
          // dictionary, we'll need to Suspend this channel, and do a
          // replace later.  Remember this new entry so when the current
          // entry has it's data in memory we can un-Suspend the new
          // channel/entry.  When the new entry finishes saving, it will
          // use the mReplacement link to come back and insert itself
          // into mEntries and drop the old entry.  Use an origin link
          // for that since the old entry could in theory get purged and
          // removed from the origin before we finish.
          mEntries[i]->SetReplacement(aDictEntry, this);
          // SetReplacement will also set aDictEntry->mShouldSuspend
          return do_AddRef(aDictEntry);
        } else {
          DICTIONARY_LOG(("Removing old entry, no users or already read data"));
          // We can just replace, there are no users active for the old data.
          // This stops new requests from trying to use the old data we're in
          // the process of replacing Remove the entry from the Origin and
          // Write().
          if (mEntry) {
            mEntries[i]->RemoveEntry(mEntry);
          }
          mEntries.RemoveElementAt(i);
        }
      } else {
        // We're updating an existing entry (on a 304 Not Modified).   Assume
        // the values may have changed (though likely they haven't).  Check Spec
        // XXX
        DICTIONARY_LOG(
            ("Updating dictionary for %s (%p)", mOrigin.get(), oldEntry));
        oldEntry->CopyFrom(aDictEntry);
        // write the entry back if something changed
        // XXX Check if something changed
        oldEntry->Write(mEntry);

        // We don't need to reference the entry
        return nullptr;
      }
      break;
    }
  }

  DICTIONARY_LOG(("New dictionary %sfor %s: %p",
                  aDictEntry->HasHash() ? "" : "(pending) ", mOrigin.get(),
                  aDictEntry));
  if (aDictEntry->HasHash()) {
    mEntries.AppendElement(aDictEntry);
  } else {
    // Still need to receive the data.  When we have the hash, move to
    // mEntries (and Write) using entry->mOrigin
    mPendingEntries.AppendElement(aDictEntry);
    aDictEntry->SetReplacement(nullptr, this);
  }

  // DictionaryCache/caller is responsible for ensure this gets written if
  // needed
  return do_AddRef(aDictEntry);
}

nsresult DictionaryOrigin::RemoveEntry(const nsACString& aKey) {
  DICTIONARY_LOG(
      ("DictionaryOrigin::RemoveEntry for %s", PromiseFlatCString(aKey).get()));
  RefPtr<DictionaryCacheEntry> hold;
  for (const auto& dict : mEntries) {
    if (dict->GetURI().Equals(aKey)) {
      // Ensure it doesn't disappear on us
      hold = dict;
      DICTIONARY_LOG(("Removing %p", dict.get()));
      mEntries.RemoveElement(dict);
      if (mEntry) {
        hold->RemoveEntry(mEntry);
      } else {
        // We don't have the cache entry yet.  Defer the removal from
        // the entry until we do
        mPendingRemove.AppendElement(hold);
        return NS_OK;
      }
      break;
    }
  }
  if (!hold) {
    DICTIONARY_LOG(("DictionaryOrigin::RemoveEntry (pending) for %s",
                    PromiseFlatCString(aKey).get()));
    for (const auto& dict : mPendingEntries) {
      if (dict->GetURI().Equals(aKey)) {
        // Ensure it doesn't disappear on us
        RefPtr<DictionaryCacheEntry> hold(dict);
        DICTIONARY_LOG(("Removing %p", dict.get()));
        mPendingEntries.RemoveElement(dict);
        break;
      }
    }
  }
  // If this origin has no entries, remove it and doom the entry
  if (IsEmpty()) {
    Clear();
  }
  return hold ? NS_OK : NS_ERROR_FAILURE;
}

void DictionaryOrigin::FinishAddEntry(DictionaryCacheEntry* aEntry) {
  // if aDictEntry is in mPendingEntries, move to mEntries
  if (mPendingEntries.RemoveElement(aEntry)) {
    // We need to give priority to elements fetched most recently if they
    // have an equivalent match length (and dest)
    // XXX insert at the start of entries of this length
    mEntries.InsertElementAt(0, aEntry);
  }
  DICTIONARY_LOG(("FinishAddEntry(%s)", aEntry->mURI.get()));
  if (MOZ_UNLIKELY(MOZ_LOG_TEST(gDictionaryLog, mozilla::LogLevel::Debug))) {
    DumpEntries();
  }
}

void DictionaryOrigin::RemoveEntry(DictionaryCacheEntry* aEntry) {
  DICTIONARY_LOG(("RemoveEntry(%s)", aEntry->mURI.get()));
  if (!mEntries.RemoveElement(aEntry)) {
    mPendingEntries.RemoveElement(aEntry);
  }
  if (MOZ_UNLIKELY(MOZ_LOG_TEST(gDictionaryLog, mozilla::LogLevel::Debug))) {
    DumpEntries();
  }
}

void DictionaryOrigin::DumpEntries() {
  DICTIONARY_LOG(("*** Origin %s ***", mOrigin.get()));
  for (const auto& dict : mEntries) {
    DICTIONARY_LOG(
        ("* %s: pattern %s, id %s, match-dest[0]: %s, hash: %s, expiration: "
         "%u",
         dict->mURI.get(), dict->mPattern.get(), dict->mId.get(),
         dict->mMatchDest.IsEmpty()
             ? ""
             : dom::GetEnumString(dict->mMatchDest[0]).get(),
         dict->GetHash().get(), dict->mExpiration));
  }
  DICTIONARY_LOG(("*** Pending ***"));
  for (const auto& dict : mPendingEntries) {
    DICTIONARY_LOG(
        ("* %s: pattern %s, id %s, match-dest[0]: %s, hash: %s, expiration: "
         "%u",
         dict->mURI.get(), dict->mPattern.get(), dict->mId.get(),
         dict->mMatchDest.IsEmpty()
             ? ""
             : dom::GetEnumString(dict->mMatchDest[0]).get(),
         dict->GetHash().get(), dict->mExpiration));
  }
}

void DictionaryOrigin::Clear() {
  DICTIONARY_LOG(("*** Clearing origin %s", mOrigin.get()));
  mEntries.Clear();
  mPendingEntries.Clear();
  mPendingRemove.Clear();
  // We may be under a lock; doom this asynchronously
  if (mEntry) {
    // This will attempt to delete the DictionaryOrigin, but we'll do
    // that more directly
    NS_DispatchBackgroundTask(NS_NewRunnableFunction(
        "DictionaryOrigin::Clear", [entry = mEntry, origin(mOrigin)]() {
          DICTIONARY_LOG(("*** Dooming origin %s", origin.get()));
          entry->AsyncDoom(nullptr);
        }));
  }
  gDictionaryCache->RemoveOrigin(mMapKey);
}

// caller will throw this into a RefPtr
DictionaryCacheEntry* DictionaryOrigin::Match(const nsACString& aPath,
                                              ExtContentPolicyType aType) {
  uint32_t longest = 0;
  DictionaryCacheEntry* result = nullptr;
  uint32_t now = mozilla::net::NowInSeconds();

  for (const auto& dict : mEntries) {
    if (dict->Match(aPath, aType, now, longest)) {
      result = dict;
    }
  }
  // XXX if we want to LRU the origins so we can push them out of memory based
  // on LRU, do something like this:
  /*
  if (result) {
    removeFrom(dictionarycase->mDictionaryCacheLRU);
    dictionarycase->mDictionaryCacheLRU.insertFront(this);
  }
  */
  return result;
}

//-----------------------------------------------------------------------------
// DictionaryOrigin::nsICacheEntryMetaDataVisitor
//-----------------------------------------------------------------------------
nsresult DictionaryOrigin::OnMetaDataElement(const char* asciiKey,
                                             const char* asciiValue) {
  DICTIONARY_LOG(("DictionaryOrigin::OnMetaDataElement %s %s",
                  asciiKey ? asciiKey : "", asciiValue));

  // We set the content ID to CONTENT_TYPE_DICTIONARY, ensure that's correct
  if (strcmp(asciiKey, "ctid") == 0) {
    MOZ_ASSERT(strcmp(asciiValue, "7") == 0);
    return NS_OK;
  }
  // All other keys should be URLs for dictionaries
  // If we already have an entry for this key (pending or in the list),
  // don't override it
  for (auto& entry : mEntries) {
    if (entry->GetURI().Equals(asciiKey)) {
      return NS_OK;
    }
  }
  for (auto& entry : mPendingEntries) {
    if (entry->GetURI().Equals(asciiKey)) {
      return NS_OK;
    }
  }
  RefPtr<DictionaryCacheEntry> entry = new DictionaryCacheEntry(asciiKey);
  entry->SetLoadContextInfo(mLoadContextInfo);
  if (entry->ParseMetadata(asciiValue)) {
    mEntries.AppendElement(entry);
  }
  return NS_OK;
}

// Overall structure:
// Dictionary:
//     DictionaryCache:
//        OriginHashmap:
//           LinkedList: DictionaryCacheEntry
//              Data from cache (sometimes)
//
// Each origin is in the cache as a dictionary-origin entry.  In that
// entry's metadata, we have an LRU-sorted list of dictionary entries to be able
// to build a DictionaryCacheEntry.
// When we offer a dictionary on a load, we'll start prefetching the data into
// the DictionaryCacheEntry for the item in the cache. When the response comes
// in, we'll either use it to decompress, or indicate we no longer care about
// the data. If no one cares about the data, we'll purge it from memory.
//    Hold refs to the data in requests.  When the only ref is in the
//    DictionaryCacheEntry, purge the data. This could also be done via the
//    InUse counter
//
// XXX be careful about thrashing the cache loading and purging.
// XXX Perhaps allow a little dwell time in ram if not too large?

// When a load comes in, we need to block decompressing it on having the data
// from the cache if it's dcb or dcz.
// XXX If the cache fails for some reason, we probably should consider
// re-fetching the data without Dictionary-Available.

}  // namespace net
}  // namespace mozilla
