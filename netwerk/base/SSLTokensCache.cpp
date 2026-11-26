/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SSLTokensCache.h"

#include <algorithm>

#include "CertVerifier.h"
#include "CommonSocketControl.h"
#include "ScopedNSSTypes.h"
#include "TransportSecurityInfo.h"
#include "brotli/decode.h"
#include "brotli/encode.h"
#include "mozIStorageConnection.h"
#include "mozIStorageService.h"
#include "mozIStorageStatement.h"
#include "mozStorageCID.h"
#include "mozStorageHelper.h"
#include "mozilla/ArrayAlgorithm.h"
#include "mozilla/Atomics.h"
#include "mozilla/Components.h"
#include "mozilla/EndianUtils.h"
#include "mozilla/FunctionRef.h"
#include "mozilla/Logging.h"
#include "mozilla/Maybe.h"
#include "mozilla/OriginAttributes.h"
#include "mozilla/Preferences.h"
#include "mozilla/Services.h"
#include "mozilla/Span.h"
#include "mozilla/StaticPrefs_privacy.h"
#include "mozilla/dom/Promise.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "nsAppDirectoryServiceDefs.h"
#include "nsDirectoryServiceUtils.h"
#include "nsIEventTarget.h"
#include "nsIGlobalObject.h"
#include "nsIOService.h"
#include "nsIObserverService.h"
#include "nsITimer.h"
#include "nsThreadUtils.h"
#include "nss.h"
#include "prtime.h"
#include "ssl.h"
#include "sslexp.h"
#include "xpcpublic.h"

#ifdef ENABLE_TESTS
#  include "mozilla/Atomics.h"
#  include "mozilla/StaticMonitor.h"
#endif

#define SSL_TOKENS_CACHE_DB_FILE "ssl_tokens_cache.sqlite"

constexpr int32_t kSSLTokensCacheSchemaVersion = 3;

// Matches network.cookie.db.flushIntervalMs.
constexpr uint32_t kDBDeleteFlushDelayMs = 500;

namespace mozilla {
namespace net {

static LazyLogModule gSSLTokensCacheLog("SSLTokensCache");

// Main thread only: <profile>/ssl_tokens_cache.sqlite, or null if no profile.
static already_AddRefed<nsIFile> ProfileDBFile() {
  MOZ_ASSERT(NS_IsMainThread());
  nsCOMPtr<nsIFile> file;
  if (NS_FAILED(NS_GetSpecialDirectory(NS_APP_USER_PROFILE_50_DIR,
                                       getter_AddRefs(file)))) {
    return nullptr;
  }
  file->AppendNative(nsLiteralCString(SSL_TOKENS_CACHE_DB_FILE));
  return file.forget();
}

#undef LOG
#define LOG(args) MOZ_LOG(gSSLTokensCacheLog, mozilla::LogLevel::Debug, args)
#undef LOG5_ENABLED
#define LOG5_ENABLED() \
  MOZ_LOG_TEST(mozilla::net::gSSLTokensCacheLog, mozilla::LogLevel::Verbose)

class ExpirationComparator {
 public:
  bool Equals(SSLTokensCache::TokenCacheRecord* a,
              SSLTokensCache::TokenCacheRecord* b) const {
    return a->mExpirationTime == b->mExpirationTime;
  }
  bool LessThan(SSLTokensCache::TokenCacheRecord* a,
                SSLTokensCache::TokenCacheRecord* b) const {
    return a->mExpirationTime < b->mExpirationTime;
  }
};

static nsTArray<nsTArray<uint8_t>> CloneCertChain(
    const nsTArray<nsTArray<uint8_t>>& aSrc) {
  return TransformIntoNewArray(aSrc, [](const auto& c) { return c.Clone(); });
}

SessionCacheInfo SessionCacheInfo::Clone() const {
  SessionCacheInfo result;
  result.mEVStatus = mEVStatus;
  result.mCertificateTransparencyStatus = mCertificateTransparencyStatus;
  result.mServerCertBytes = mServerCertBytes.Clone();
  result.mSucceededCertChainBytes =
      mSucceededCertChainBytes.map(CloneCertChain);
  result.mIsBuiltCertChainRootBuiltInRoot = mIsBuiltCertChainRootBuiltInRoot;
  result.mOverridableErrorCategory = mOverridableErrorCategory;
  result.mHandshakeCertificatesBytes =
      mHandshakeCertificatesBytes.map(CloneCertChain);
  return result;
}

// Serialized payload layout (all integers little-endian):
//   [4: token_len][token bytes]
//   [1: ev_status]
//   [2: ct_status]
//   [1: overridable_error]
//   [1: builtin_root] (0=Nothing, 1=Some(false), 2=Some(true))
//
// Stored compressed with an LE original-size prefix.
//
// cert_hashes DB column layout:
//   [1: has_succeeded_chain] if 1: [1: count] then count x [32: SHA-256]
//   [1: has_handshake_certs] if 1: [1: count] then count x [32: SHA-256]
//
// The server cert is the first handshake cert.
static constexpr size_t kOriginalLenSize = sizeof(uint32_t);
static constexpr size_t kCertHashLen = 32;  // SHA-256

// Sanity bounds on the length fields above.
static constexpr uint32_t kMaxPayloadSize = 256 * 1024;
static constexpr uint32_t kMaxTokenSize = 256 * 1024;
static constexpr uint32_t kMaxCertSize = 0xffff;
// The cert count of a chain is stored in one byte.
static constexpr size_t kMaxCertsPerChain = 0xFF;
// Two chains, each [1: present][1: count] then up to 255 hashes.
static constexpr uint32_t kMaxCertHashesSize =
    2 * (2 + kMaxCertsPerChain * kCertHashLen);

// The presence marker distinguishes an absent chain from an empty one.
static constexpr uint8_t kChainAbsent = 0;
static constexpr uint8_t kChainPresent = 1;

#ifdef ENABLE_TESTS
static Atomic<uint32_t, Relaxed> sCertRowsLoadedForTest;
#endif

template <typename T>
static void AppendLE(nsTArray<uint8_t>& aBuf, T aVal) {
  T le = mozilla::NativeEndian::swapToLittleEndian(aVal);
  aBuf.AppendElements(reinterpret_cast<const uint8_t*>(&le), sizeof(T));
}

static nsDependentCSubstring AsHashKey(Span<const uint8_t> aHash) {
  return nsDependentCSubstring(reinterpret_cast<const char*>(aHash.Elements()),
                               aHash.Length());
}

// Calls aFn on each hash of both chains; stops and returns false as soon as aFn
// returns false.
template <typename F>
static bool ForEachCertHash(const SSLTokensCache::CertHashChain& aSucceeded,
                            const SSLTokensCache::CertHashChain& aHandshake,
                            F&& aFn) MOZ_NO_THREAD_SAFETY_ANALYSIS {
  for (const auto* chain : {&aSucceeded, &aHandshake}) {
    if (chain->isSome()) {
      for (const auto& hash : **chain) {
        if (!aFn(hash)) {
          return false;
        }
      }
    }
  }
  return true;
}

static void AppendChain(nsTArray<uint8_t>& aBuf,
                        const SSLTokensCache::CertHashChain& aChain) {
  if (aChain.isNothing()) {
    aBuf.AppendElement(kChainAbsent);
    return;
  }

  const auto length = aChain->Length();
  MOZ_RELEASE_ASSERT(length <= kMaxCertsPerChain);

  aBuf.AppendElement(kChainPresent);
  aBuf.AppendElement(static_cast<uint8_t>(length));
  for (const auto& hash : *aChain) {
    MOZ_ASSERT(hash.Length() == kCertHashLen);
    aBuf.AppendElements(hash);
  }
}

static nsTArray<uint8_t> SerializePayload(
    Span<const uint8_t> aToken, const SessionCacheInfo& aInfo,
    SSLTokensCache::CertHashChain& aSucceeded,
    SSLTokensCache::CertHashChain& aHandshake,
    nsTArray<const nsTArray<uint8_t>*>& aCerts) {
  nsTArray<uint8_t> buf;
  AppendLE(buf, AssertedCast<uint32_t>(aToken.Length()));
  buf.AppendElements(aToken.Elements(), aToken.Length());
  buf.AppendElement(aInfo.mEVStatus == psm::EVStatus::EV ? 1 : 0);
  AppendLE(buf, aInfo.mCertificateTransparencyStatus);
  buf.AppendElement(static_cast<uint8_t>(aInfo.mOverridableErrorCategory));
  if (aInfo.mIsBuiltCertChainRootBuiltInRoot.isNothing()) {
    buf.AppendElement(0);
  } else {
    buf.AppendElement(*aInfo.mIsBuiltCertChainRootBuiltInRoot ? 2 : 1);
  }

  const auto hashChain = [&aCerts](
                             const Maybe<nsTArray<nsTArray<uint8_t>>>& aChain,
                             SSLTokensCache::CertHashChain& aOut) {
    if (aChain.isNothing()) {
      return true;
    }

    if (aChain->Length() > kMaxCertsPerChain) {
      return false;
    }

    aOut.emplace();
    for (const auto& der : *aChain) {
      if (der.Length() > kMaxCertSize) {
        return false;
      }

      nsTArray<uint8_t> digest;
      if (NS_FAILED(Digest::DigestBuf(SEC_OID_SHA256, der, digest)) ||
          digest.Length() != kCertHashLen) {
        return false;
      }

      aOut->AppendElement(std::move(digest));
      aCerts.AppendElement(&der);
    }
    return true;
  };

  if (!hashChain(aInfo.mSucceededCertChainBytes, aSucceeded) ||
      !hashChain(aInfo.mHandshakeCertificatesBytes, aHandshake)) {
    return {};
  }

  return buf;
}

// Records are compressed on the socket thread during the handshake. Quality 1
// encodes ~3.5x faster than 5 for ~5% larger output, and the cache is bounded
// in bytes, so that costs cached sessions rather than memory. Quality 2 to 4
// compress this data worse than 1 for no speed gain.
static constexpr int kRecordCompressionQuality = 1;

// Returns empty array on failure.
static nsTArray<uint8_t> CompressRecord(Span<const uint8_t> aPayload) {
  nsTArray<uint8_t> encoded;
  if (!encoded.SetLength(BrotliEncoderMaxCompressedSize(aPayload.Length()),
                         fallible)) {
    return {};
  }
  size_t encodedSize = encoded.Length();
  if (!BrotliEncoderCompress(kRecordCompressionQuality, BROTLI_DEFAULT_WINDOW,
                             BROTLI_MODE_GENERIC, aPayload.Length(),
                             aPayload.Elements(), &encodedSize,
                             encoded.Elements())) {
    return {};
  }

  nsTArray<uint8_t> result;
  if (!result.SetCapacity(kOriginalLenSize + encodedSize, fallible)) {
    return {};
  }
  AppendLE(result, AssertedCast<uint32_t>(aPayload.Length()));
  result.AppendElements(encoded.Elements(), encodedSize);
  return result;
}

static bool ReadOriginalLen(Span<const uint8_t> aCompressed,
                            uint32_t& aOriginalLen) {
  if (aCompressed.Length() < kOriginalLenSize) {
    return false;
  }
  aOriginalLen = LittleEndian::readUint32(aCompressed.Elements());
  if (aOriginalLen > kMaxPayloadSize) {
    LOG(("SSLTokensCache: implausible payload originalLen %" PRIu32,
         aOriginalLen));
    return false;
  }
  return true;
}

// Returns empty array on failure.
static nsTArray<uint8_t> DecompressRecord(Span<const uint8_t> aCompressed) {
  uint32_t originalLen = 0;
  if (!ReadOriginalLen(aCompressed, originalLen)) {
    return {};
  }
  nsTArray<uint8_t> result;
  if (!result.SetLength(originalLen, fallible)) {
    return {};
  }
  auto encoded = aCompressed.From(kOriginalLenSize);
  size_t decodedSize = originalLen;
  BrotliDecoderResult r = BrotliDecoderDecompress(
      encoded.Length(), encoded.Elements(), &decodedSize, result.Elements());
  if (r != BROTLI_DECODER_RESULT_SUCCESS || decodedSize != originalLen) {
    return {};
  }
  return result;
}

struct PayloadReader {
  Span<const uint8_t> buf;
  size_t pos = 0;

  template <typename T>
  bool Read(T& out) {
    if (buf.Length() - pos < sizeof(T)) return false;
    if constexpr (sizeof(T) == 1) {
      out = static_cast<T>(buf[pos]);
    } else {
      T le;
      memcpy(&le, buf.Elements() + pos, sizeof(T));
      out = mozilla::NativeEndian::swapFromLittleEndian(le);
    }
    pos += sizeof(T);
    return true;
  }
  bool Bytes(nsTArray<uint8_t>& out, uint32_t len) {
    if (buf.Length() - pos < len) return false;
    if (!out.SetLength(len, fallible)) return false;
    memcpy(out.Elements(), buf.Elements() + pos, len);
    pos += len;
    return true;
  }
  bool AtEnd() const { return pos == buf.Length(); }
};

static bool ReadChain(PayloadReader& aReader,
                      SSLTokensCache::CertHashChain& aChain) {
  uint8_t presence;
  if (!aReader.Read(presence)) {
    return false;
  }

  if (presence == kChainAbsent) {
    aChain = Nothing();
    return true;
  }
  if (presence != kChainPresent) {
    return false;
  }

  uint8_t count;
  if (!aReader.Read(count)) {
    return false;
  }

  CopyableTArray<SSLTokensCacheCertHash> chain;
  chain.SetCapacity(count);
  for (size_t i = 0; i < count; ++i) {
    SSLTokensCacheCertHash hash;
    if (!aReader.Bytes(hash, kCertHashLen)) {
      return false;
    }
    chain.AppendElement(std::move(hash));
  }

  aChain = Some(std::move(chain));
  return true;
}

static nsTArray<uint8_t> EncodeCertHashes(
    const SSLTokensCache::CertHashChain& aSucceeded,
    const SSLTokensCache::CertHashChain& aHandshake) {
  nsTArray<uint8_t> buf;
  if (aHandshake.isNothing()) {
    return buf;
  }

  AppendChain(buf, aSucceeded);
  AppendChain(buf, aHandshake);
  return buf;
}

static bool DecodeCertHashes(Span<const uint8_t> aBuf,
                             SSLTokensCache::CertHashChain& aSucceeded,
                             SSLTokensCache::CertHashChain& aHandshake) {
  if (aBuf.IsEmpty()) {
    return true;
  }

  PayloadReader r{aBuf};
  // The hash-based format requires a present handshake chain.
  return ReadChain(r, aSucceeded) && ReadChain(r, aHandshake) && r.AtEnd() &&
         aHandshake.isSome();
}

static size_t DecodedRecordSize(
    size_t aPayloadLen, const nsTArray<const nsTArray<uint8_t>*>& aCerts) {
  size_t size = aPayloadLen;
  for (const auto* der : aCerts) {
    size += der->Length();
  }
  return size;
}

// Returns the DER for a hash, or nullptr if it's unknown.
using CertLookup =
    FunctionRef<const nsTArray<uint8_t>*(const SSLTokensCacheCertHash&)>;

static bool DeserializeRecord(
    Span<const uint8_t> aBuf,
    const SSLTokensCache::CertHashChain& aSucceededHashes,
    const SSLTokensCache::CertHashChain& aHandshakeHashes,
    CertLookup aLookupCert, nsTArray<uint8_t>& aToken,
    SessionCacheInfo& aInfo) {
  PayloadReader r{aBuf};

  uint32_t tokenLen;
  if (!r.Read(tokenLen)) return false;
  if (tokenLen > kMaxTokenSize) return false;
  if (!r.Bytes(aToken, tokenLen)) return false;

  uint8_t evStatus;
  if (!r.Read(evStatus)) return false;
  aInfo.mEVStatus = evStatus ? psm::EVStatus::EV : psm::EVStatus::NotEV;

  uint16_t ctStatus;
  if (!r.Read(ctStatus)) return false;
  aInfo.mCertificateTransparencyStatus = ctStatus;

  uint8_t overridableError;
  if (!r.Read(overridableError)) return false;
  aInfo.mOverridableErrorCategory =
      static_cast<nsITransportSecurityInfo::OverridableErrorCategory>(
          overridableError);

  uint8_t builtinRoot;
  if (!r.Read(builtinRoot)) return false;
  if (builtinRoot == 0) {
    aInfo.mIsBuiltCertChainRootBuiltInRoot = Nothing();
  } else if (builtinRoot == 1) {
    aInfo.mIsBuiltCertChainRootBuiltInRoot = Some(false);
  } else if (builtinRoot == 2) {
    aInfo.mIsBuiltCertChainRootBuiltInRoot = Some(true);
  } else {
    return false;
  }

  if (aHandshakeHashes.isNothing()) {
    return false;
  }

  MOZ_ASSERT(aBuf.Length() <= kMaxPayloadSize);
  const auto resolveChain = [&aLookupCert](
                                const SSLTokensCache::CertHashChain& aHashes,
                                Maybe<nsTArray<nsTArray<uint8_t>>>& aChain) {
    if (aHashes.isNothing()) {
      aChain = Nothing();
      return true;
    }
    aChain = Some(nsTArray<nsTArray<uint8_t>>());
    for (const auto& hash : *aHashes) {
      const auto* der = aLookupCert(hash);
      if (!der) {
        return false;
      }
      MOZ_ASSERT(der->Length() <= kMaxCertSize);
      aChain->AppendElement(der->Clone());
    }
    return true;
  };

  if (!resolveChain(aSucceededHashes, aInfo.mSucceededCertChainBytes) ||
      !resolveChain(aHandshakeHashes, aInfo.mHandshakeCertificatesBytes) ||
      aInfo.mHandshakeCertificatesBytes->IsEmpty()) {
    return false;
  }
  // The first handshake certificate is the server certificate.
  aInfo.mServerCertBytes = (*aInfo.mHandshakeCertificatesBytes)[0].Clone();

  return r.AtEnd();
}

StaticRefPtr<SSLTokensCache> SSLTokensCache::gInstance;
StaticMutex SSLTokensCache::sLock;
uint64_t SSLTokensCache::sRecordId = 0;

#ifdef ENABLE_TESTS
static constexpr uint32_t kLoadNotPausedForTest = UINT32_MAX;
static StaticMonitor sLoadPauseMonitor;
// Row count at which the startup load blocks; written under sLoadPauseMonitor.
static Atomic<uint32_t> sLoadPausedAtRowsForTest(kLoadNotPausedForTest);
// Set by the hooks below; gates the load-complete notification.
static Atomic<bool> sLoadHookArmedForTest(false);
// Number of upcoming OpenActiveDB calls that fail.
static Atomic<uint32_t> sFailDBOpensForTest(0);
// Set by FailDBOpensForTest; gates the DB-removed notification.
static Atomic<bool> sFailDBOpensArmedForTest(false);

static void NotifyObserversForTest(const char* aTopic) {
  NS_DispatchToMainThread(NS_NewRunnableFunction(
      "SSLTokensCache::NotifyObserversForTest", [aTopic] {
        if (nsCOMPtr<nsIObserverService> obs =
                mozilla::services::GetObserverService()) {
          obs->NotifyObservers(nullptr, aTopic, nullptr);
        }
      }));
}

static void SetLoadPausedAtRowsForTest(uint32_t aRows) {
  StaticMonitorAutoLock lock(sLoadPauseMonitor);
  sLoadPausedAtRowsForTest = aRows;
  lock.NotifyAll();
}

// Called on mDBQueue after aRows rows of the startup load.
static void WaitWhileLoadPausedForTest(uint32_t aRows) {
  if (sLoadPausedAtRowsForTest != aRows) {
    return;
  }
  MOZ_ASSERT(!NS_IsMainThread());
  StaticMonitorAutoLock lock(sLoadPauseMonitor);
  while (sLoadPausedAtRowsForTest == aRows) {
    lock.Wait();
  }
}
#endif

SSLTokensCache::TokenCacheRecord::~TokenCacheRecord() {
  if (!gInstance) {
    return;
  }
  gInstance->OnRecordDestroyed(this);
}

uint32_t SSLTokensCache::TokenCacheRecord::Size() const {
  const uint32_t size = mKey.Length() + mCompressedPayload.Length();
  if (mHandshakeHashes.isNothing()) {
    return size;
  }

  // The certs of a shared record are in mCertStore; it holds only their hashes.
  const size_t hashCount = mHandshakeHashes->Length() +
                           (mSucceededHashes ? mSucceededHashes->Length() : 0);
  return size + hashCount * kCertHashLen;
}

uint32_t SSLTokensCache::CertStoreEntry::Size() const {
  return kCertHashLen + mDER.Length();
}

uint32_t SSLTokensCache::TokenCacheEntry::Size() const {
  uint32_t size = 0;
  for (const auto& rec : mRecords) {
    size += rec->Size();
  }
  return size;
}

void SSLTokensCache::TokenCacheEntry::AddRecord(
    UniquePtr<SSLTokensCache::TokenCacheRecord>&& aRecord,
    nsTArray<TokenCacheRecord*>& aExpirationArray) {
  if (mRecords.Length() ==
      StaticPrefs::network_ssl_tokens_cache_records_per_entry()) {
    aExpirationArray.RemoveElement(mRecords[0].get());
    mRecords.RemoveElementAt(0);
  }

  aExpirationArray.AppendElement(aRecord.get());
  for (int32_t i = mRecords.Length() - 1; i >= 0; --i) {
    if (aRecord->mExpirationTime > mRecords[i]->mExpirationTime) {
      mRecords.InsertElementAt(i + 1, std::move(aRecord));
      return;
    }
  }
  mRecords.InsertElementAt(0, std::move(aRecord));
}

UniquePtr<SSLTokensCache::TokenCacheRecord>
SSLTokensCache::TokenCacheEntry::RemoveWithId(uint64_t aId) {
  for (int32_t i = mRecords.Length() - 1; i >= 0; --i) {
    if (mRecords[i]->mId == aId) {
      UniquePtr<TokenCacheRecord> record = std::move(mRecords[i]);
      mRecords.RemoveElementAt(i);
      return record;
    }
  }
  return nullptr;
}

const UniquePtr<SSLTokensCache::TokenCacheRecord>&
SSLTokensCache::TokenCacheEntry::Get() {
  return mRecords[0];
}

NS_IMPL_ISUPPORTS(SSLTokensCache, nsIMemoryReporter, nsIObserver,
                  nsIAsyncShutdownBlocker)

template <typename Pred>
void SSLTokensCache::RemoveMatchingLocked(Pred&& aPredicate) {
  sLock.AssertCurrentThreadOwns();
  AutoTArray<nsCString, 4> keysToRemove;
  for (const auto& entry : mTokenCacheRecords) {
    if (aPredicate(entry.GetKey())) {
      keysToRemove.AppendElement(entry.GetKey());
    }
  }
  for (const auto& key : keysToRemove) {
    (void)RemoveAllLocked(key);
  }
}

nsISerialEventTarget* SSLTokensCache::ActiveDBQueueLocked() const {
  sLock.AssertCurrentThreadOwns();
  return mDBActive ? mDBQueue.get() : nullptr;
}

// static
template <typename F>
void SSLTokensCache::DispatchToDBQueue(nsISerialEventTarget* aQueue,
                                       const char* aName, F&& aFunction) {
  aQueue->Dispatch(NS_NewRunnableFunction(aName, std::forward<F>(aFunction)),
                   NS_DISPATCH_EVENT_MAY_BLOCK);
}

// static
void SSLTokensCache::RemoveMatchingAndQueueDeletes(
    StringPredicate&& aPredicate) {
  MOZ_ASSERT(NS_IsMainThread());
  StaticMutexAutoLock lock(sLock);
  if (!gInstance) {
    return;
  }
  gInstance->RemoveMatchingLocked(aPredicate);
  if (!gInstance->mDBActive) {
    return;
  }
  gInstance->mPendingDBRemovals.AppendElement(aPredicate);
  if (!gInstance->mLoadComplete) {
    // OnLoadCompleteNotify flushes mPendingDBRemovals.
    gInstance->mRemovalsBeforeLoad.AppendElement(std::move(aPredicate));
    return;
  }
  if (!gInstance->mDBDeleteTimer) {
    NS_NewTimerWithCallback(
        getter_AddRefs(gInstance->mDBDeleteTimer),
        [](nsITimer*) { FlushPendingDBDeletes(); }, kDBDeleteFlushDelayMs,
        nsITimer::TYPE_ONE_SHOT, "SSLTokensCache::FlushPendingDBDeletes"_ns);
  }
}

// static
void SSLTokensCache::FlushPendingDBDeletes() {
  MOZ_ASSERT(NS_IsMainThread());
  nsTArray<StringPredicate> removals;
  nsCOMPtr<nsISerialEventTarget> queue;
  {
    StaticMutexAutoLock lock(sLock);
    if (!gInstance) {
      return;
    }
    gInstance->mDBDeleteTimer = nullptr;
    queue = gInstance->ActiveDBQueueLocked();
    if (queue) {
      removals = std::move(gInstance->mPendingDBRemovals);
    }
  }
  if (removals.IsEmpty()) {
    return;
  }
  DispatchToDBQueue(queue, "SSLTokensCache::DeleteMatchesFromDB",
                    [removals = std::move(removals)]() mutable {
                      DeleteMatchesFromDB(std::move(removals));
                    });
}

// static
void SSLTokensCache::RemoveDBFileAfterFailure() {
  RemoveDBFileSync();
#ifdef ENABLE_TESTS
  if (sFailDBOpensArmedForTest) {
    NotifyObserversForTest("ssl-tokens-cache-db-removed-for-test");
  }
#endif
}

void SSLTokensCache::ClearCacheLocked() {
  sLock.AssertCurrentThreadOwns();
  mLoadGeneration++;
  mExpirationArray.Clear();
  mTokenCacheRecords.Clear();
  MOZ_ASSERT(mCertStore.IsEmpty(), "Records released all their certs");
  mCertStore.Clear();
  mCacheSize = 0;
  mPendingDBRemovals.Clear();
  mRemovalsBeforeLoad.Clear();
}

void SSLTokensCache::StoreOrAddRefCertLocked(
    const SSLTokensCacheCertHash& aHash, Span<const uint8_t> aDER) {
  sLock.AssertCurrentThreadOwns();

  if (auto entry = mCertStore.Lookup(AsHashKey(aHash))) {
    ++entry->mRefCount;
    return;
  }

  CertStoreEntry newEntry;
  newEntry.mDER.AppendElements(aDER);
  newEntry.mRefCount = 1;
  mCacheSize += newEntry.Size();
  mCertStore.InsertOrUpdate(AsHashKey(aHash), std::move(newEntry));
}

bool SSLTokensCache::StoreOrAddRefCertsLocked(
    const CertHashChain& aSucceededHashes,
    const CertHashChain& aHandshakeHashes, const CertDERByFingerprint& aCerts) {
  sLock.AssertCurrentThreadOwns();

  // Check that aCerts contains each hash from aSucceededHashes and
  // aHandshakeHashes.
  if (!ForEachCertHash(aSucceededHashes, aHandshakeHashes,
                       [&aCerts](const SSLTokensCacheCertHash& aHash) {
                         return aCerts.Contains(AsHashKey(aHash));
                       })) {
    return false;
  }

  // Acquire one reference per hash occurrence, including duplicates.
  ForEachCertHash(
      aSucceededHashes, aHandshakeHashes,
      [this, &aCerts](const SSLTokensCacheCertHash& aHash) MOZ_REQUIRES(sLock) {
        StoreOrAddRefCertLocked(aHash, aCerts.Lookup(AsHashKey(aHash)).Data());
        return true;
      });
  return true;
}

void SSLTokensCache::ReleaseCertHashesLocked(
    const CertHashChain& aSucceededHashes,
    const CertHashChain& aHandshakeHashes) {
  sLock.AssertCurrentThreadOwns();

  ForEachCertHash(
      aSucceededHashes, aHandshakeHashes,
      [this](const SSLTokensCacheCertHash& aHash) MOZ_REQUIRES(sLock) {
        auto entry = mCertStore.Lookup(AsHashKey(aHash));
        MOZ_ASSERT(entry, "Record refers to a cert missing from the store");
        if (entry) {
          MOZ_ASSERT(entry->mRefCount > 0);
          if (--entry->mRefCount == 0) {
            mCacheSize -= entry->Size();
            entry.Remove();
          }
        }
        return true;
      });
}

void SSLTokensCache::CollectRecordInfosLocked(
    SSLTokensCacheSnapshot& aOut, bool aFilterForPersistence) const {
  sLock.AssertCurrentThreadOwns();
  PRTime now = PR_Now();
  for (const auto& entry : mTokenCacheRecords.Values()) {
    for (const auto& rec : entry->Records()) {
      if (aFilterForPersistence && !ShouldPersist(*rec, now)) {
        continue;
      }
      auto info = aOut.records.AppendElement();
      info->key = rec->mKey;
      info->expirationTime = static_cast<int64_t>(rec->mExpirationTime);
      info->overridableError = rec->mOverridableError;
      info->restored = rec->mRestored;
      info->id = rec->mId;
      info->succeededHashes = rec->mSucceededHashes;
      info->handshakeHashes = rec->mHandshakeHashes;
      info->compressedPayload = rec->mCompressedPayload.Clone();

      // A shared record holds only hashes. After the lock is released, the
      // snapshot's users can't read mCertStore, so the snapshot carries the
      // DER. Certs are collected per record rather than copied from mCertStore,
      // so only certs of records that pass aFilterForPersistence are included.
      ForEachCertHash(
          rec->mSucceededHashes, rec->mHandshakeHashes,
          [this, &aOut](const SSLTokensCacheCertHash& aHash)
              MOZ_REQUIRES(sLock) {
                const auto key = AsHashKey(aHash);
                if (aOut.certs.Contains(key)) {
                  return true;
                }

                auto storeEntry = mCertStore.Lookup(key);
                MOZ_ASSERT(storeEntry,
                           "Record refers to a cert missing from the store");
                if (storeEntry) {
                  aOut.certs.InsertOrUpdate(key, storeEntry->mDER.Clone());
                }
                return true;
              });
    }
  }
}

// static
void SSLTokensCache::GetAllRecords(SSLTokensCacheSnapshot& aOut) {
  StaticMutexAutoLock lock(sLock);
  if (!gInstance) {
    return;
  }
  gInstance->CollectRecordInfosLocked(aOut, /* aFilterForPersistence */ false);
}

// static
bool SSLTokensCache::DecodeCompressedPayload(
    const SSLTokensCacheRecordInfo& aRec, const CertDERByFingerprint& aCerts,
    nsTArray<uint8_t>& aToken, SessionCacheInfo& aInfo,
    uint32_t* aDecompressedLength) {
  nsTArray<uint8_t> payload = DecompressRecord(aRec.compressedPayload);
  if (payload.IsEmpty()) {
    return false;
  }
  if (aDecompressedLength) {
    *aDecompressedLength = payload.Length();
  }

  auto lookupCert =
      [&aCerts](
          const SSLTokensCacheCertHash& aHash) -> const nsTArray<uint8_t>* {
    auto entry = aCerts.Lookup(AsHashKey(aHash));
    return entry ? &entry.Data() : nullptr;
  };

  return DeserializeRecord(payload, aRec.succeededHashes, aRec.handshakeHashes,
                           lookupCert, aToken, aInfo);
}

// static
nsDependentCSubstring SSLTokensCache::BasePartFromKey(const nsACString& aKey) {
  int32_t caretPos = aKey.FindChar('^');
  return nsDependentCSubstring(
      aKey, 0, caretPos == kNotFound ? aKey.Length() : caretPos);
}

// static
// Extracts the host from a key's base part ("host:port" or "prefix:host:port").
// Returns an empty string if no colon is found.
nsDependentCSubstring SSLTokensCache::HostFromBasePart(
    const nsDependentCSubstring& aBasePart) {
  int32_t lastColon = aBasePart.RFindChar(':');
  if (lastColon == kNotFound) {
    return nsDependentCSubstring();
  }
  return nsDependentCSubstring(aBasePart, 0, lastColon);
}

// static
OriginAttributes SSLTokensCache::OAFromPeerId(const nsACString& aPeerId) {
  OriginAttributes oa;
  int32_t caretPos = aPeerId.FindChar('^');
  if (caretPos != kNotFound) {
    // PopulateFromSuffix expects the full OA suffix including the leading '^'.
    nsAutoCString suffix(Substring(aPeerId, caretPos));
    (void)oa.PopulateFromSuffix(suffix);
  }
  return oa;
}

// static
bool SSLTokensCache::SetupPersistenceLocked(uint32_t& aLoadGen,
                                            bool aStorageServiceAvailable) {
  sLock.AssertCurrentThreadOwns();
  MOZ_ASSERT(gInstance);
  MOZ_ASSERT(!gInstance->mDBActive);

  if (!aStorageServiceAvailable) {
    return false;
  }

  if (!gInstance->mDBFile) {
    nsCOMPtr<nsIFile> dbFile = ProfileDBFile();
    if (!dbFile) {
      return false;
    }
    gInstance->mDBFile = dbFile;
  }

  if (!gInstance->mDBQueue) {
    nsCOMPtr<nsISerialEventTarget> queue;
    NS_CreateBackgroundTaskQueue("SSLTokensDB", getter_AddRefs(queue));
    if (!queue) {
      return false;
    }
    gInstance->mDBQueue = queue;
  }

  gInstance->mDBActive = true;
  gInstance->mLoadStartTime = TimeStamp::Now();
  aLoadGen = gInstance->mLoadGeneration;
  return true;
}

// Best-effort removal of the pre-mozStorage backing files.
static void RemoveLegacyBackingFilesSync(nsIFile* aDbFile) {
  nsCOMPtr<nsIFile> legacy;
  if (NS_FAILED(aDbFile->Clone(getter_AddRefs(legacy)))) {
    return;
  }
  legacy->SetLeafName(u"ssl_tokens_cache.bin"_ns);
  (void)legacy->Remove(false);
  legacy->SetLeafName(u"ssl_tokens_cache.tmp"_ns);
  (void)legacy->Remove(false);
}

// static
nsresult SSLTokensCache::Init() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(XRE_IsParentProcess());

  if (nsCOMPtr<nsIFile> dbFile = ProfileDBFile()) {
    NS_DispatchBackgroundTask(NS_NewRunnableFunction(
        "SSLTokensCache::RemoveLegacyBackingFiles",
        [dbFile]() { RemoveLegacyBackingFilesSync(dbFile); }));
  }

  StaticMutexAutoLock lock(sLock);

  MOZ_ASSERT(!gInstance);

  gInstance = new SSLTokensCache();

  RegisterWeakMemoryReporter(gInstance);

  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (obs) {
    obs->AddObserver(gInstance, "profile-after-change", false);
    obs->AddObserver(gInstance, "last-pb-context-exited", false);
  }

  if (!StaticPrefs::network_ssl_tokens_cache_persistence()) {
    return NS_OK;
  }

  if (obs) {
    obs->AddObserver(gInstance, "application-background", false);
    obs->AddObserver(gInstance, "idle-daily", false);
    gInstance->mWriteObserversRegistered = true;
  }

  return NS_OK;
}

// static
nsresult SSLTokensCache::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());
#ifdef ENABLE_TESTS
  SetLoadPausedAtRowsForTest(kLoadNotPausedForTest);
#endif
  RefPtr<SSLTokensCache> instance;
  bool blockerRegistered = false;
  {
    StaticMutexAutoLock lock(sLock);

    if (!gInstance) {
      return NS_ERROR_UNEXPECTED;
    }

    instance = gInstance;
    blockerRegistered = gInstance->mShutdownBarrier != nullptr;
  }

  // With no blocker, BlockShutdown will never fire: write synchronously
  // (test environments only), unregister, and clear gInstance now.
  if (!blockerRegistered) {
#ifdef ENABLE_TESTS
    instance->DoWrite(true);
#endif
    UnregisterFromServices(instance);
    StaticMutexAutoLock lock(sLock);
    gInstance = nullptr;
  }
  return NS_OK;
}

// static
void SSLTokensCache::UnregisterFromServices(SSLTokensCache* aInstance) {
  MOZ_ASSERT(NS_IsMainThread());
  UnregisterWeakMemoryReporter(aInstance);

  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (!obs) {
    return;
  }

  bool hadWriteObservers;
  {
    StaticMutexAutoLock lock(sLock);
    hadWriteObservers = aInstance->mWriteObserversRegistered;
  }
  if (hadWriteObservers) {
    obs->RemoveObserver(aInstance, "application-background");
    obs->RemoveObserver(aInstance, "idle-daily");
  }
  Preferences::UnregisterCallback(&SSLTokensCache::ReconcilePersistence,
                                  "network.ssl_tokens_cache_persistence");
  obs->RemoveObserver(aInstance, "profile-after-change");
  obs->RemoveObserver(aInstance, "last-pb-context-exited");
}

SSLTokensCache::SSLTokensCache() { LOG(("SSLTokensCache::SSLTokensCache")); }

SSLTokensCache::~SSLTokensCache() { LOG(("SSLTokensCache::~SSLTokensCache")); }

// static
nsresult SSLTokensCache::CreateSchemaOn(mozIStorageConnection* aConn) {
  nsresult rv = aConn->SetSchemaVersion(kSSLTokensCacheSchemaVersion);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aConn->ExecuteSimpleSQL(
      nsLiteralCString("CREATE TABLE ssl_tokens ("
                       "key TEXT NOT NULL,"
                       "expiration_time INTEGER NOT NULL,"
                       "overridable_error INTEGER NOT NULL,"
                       "cert_hashes BLOB,"
                       "payload BLOB NOT NULL);"
                       "CREATE INDEX ssl_tokens_key ON ssl_tokens (key)"));
  NS_ENSURE_SUCCESS(rv, rv);

  // Store DER bytes once per hash so token records can share certificates.
  return aConn->ExecuteSimpleSQL(
      nsLiteralCString("CREATE TABLE ssl_certs ("
                       "hash BLOB PRIMARY KEY NOT NULL,"
                       "der BLOB NOT NULL)"));
}

// static
nsresult SSLTokensCache::EnsureSchema(mozIStorageConnection* aConn) {
  bool tokensExist = false;
  nsresult rv = aConn->TableExists("ssl_tokens"_ns, &tokensExist);
  NS_ENSURE_SUCCESS(rv, rv);

  bool certsExist = false;
  rv = aConn->TableExists("ssl_certs"_ns, &certsExist);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!tokensExist && !certsExist) {
    return CreateSchemaOn(aConn);
  }

  // An unreadable version is as good as a wrong one: recreate.
  int32_t dbSchemaVersion = 0;
  rv = aConn->GetSchemaVersion(&dbSchemaVersion);
  if (tokensExist && certsExist && NS_SUCCEEDED(rv) &&
      dbSchemaVersion == kSSLTokensCacheSchemaVersion) {
    return NS_OK;
  }

  mozStorageTransaction transaction(aConn, false);
  rv = transaction.Start();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aConn->ExecuteSimpleSQL("DROP TABLE IF EXISTS ssl_tokens"_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aConn->ExecuteSimpleSQL("DROP TABLE IF EXISTS ssl_certs"_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = CreateSchemaOn(aConn);
  NS_ENSURE_SUCCESS(rv, rv);

  return transaction.Commit();
}

// static
nsresult SSLTokensCache::PrepareInsertStatement(
    mozIStorageConnection* aConn, nsCOMPtr<mozIStorageStatement>& aStmtInsert) {
  return aConn->CreateStatement(
      nsLiteralCString(
          "INSERT INTO ssl_tokens "
          "(key, expiration_time, overridable_error, cert_hashes, payload) "
          "VALUES "
          "(:key, :expiration_time, :overridable_error, :cert_hashes, "
          ":payload)"),
      getter_AddRefs(aStmtInsert));
}

// static
nsresult SSLTokensCache::DeleteAllRows(mozIStorageConnection* aConn) {
  nsresult rv = aConn->ExecuteSimpleSQL("DELETE FROM ssl_tokens"_ns);
  NS_ENSURE_SUCCESS(rv, rv);
  return aConn->ExecuteSimpleSQL("DELETE FROM ssl_certs"_ns);
}

// static
nsresult SSLTokensCache::WriteSnapshotTo(
    mozIStorageConnection* aConn, mozIStorageStatement* aStmtInsert,
    const SSLTokensCacheSnapshot& aSnapshot) {
  // Replace both tables atomically so records and certificates stay consistent.
  mozStorageTransaction transaction(aConn, false);
  nsresult rv = transaction.Start();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = DeleteAllRows(aConn);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<mozIStorageStatement> insertCert;
  rv = aConn->CreateStatement(
      "INSERT INTO ssl_certs (hash, der) VALUES (:hash, :der)"_ns,
      getter_AddRefs(insertCert));
  NS_ENSURE_SUCCESS(rv, rv);

  // aSnapshot.certs has each cert once, so the hash PRIMARY KEY can't conflict.
  for (const auto& cert : aSnapshot.certs) {
    mozStorageStatementScoper scoper(insertCert);

    rv = insertCert->BindUTF8StringAsBlobByName("hash"_ns, cert.GetKey());
    NS_ENSURE_SUCCESS(rv, rv);

    rv = insertCert->BindBlobArrayByName("der"_ns, cert.GetData());
    NS_ENSURE_SUCCESS(rv, rv);

    rv = insertCert->Execute();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  for (const auto& rec : aSnapshot.records) {
    mozStorageStatementScoper scoper(aStmtInsert);
    rv = aStmtInsert->BindUTF8StringByName("key"_ns, rec.key);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = aStmtInsert->BindInt64ByName("expiration_time"_ns, rec.expirationTime);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = aStmtInsert->BindInt32ByName("overridable_error"_ns,
                                      rec.overridableError);
    NS_ENSURE_SUCCESS(rv, rv);
    const auto certHashes =
        EncodeCertHashes(rec.succeededHashes, rec.handshakeHashes);
    rv = aStmtInsert->BindBlobArrayByName("cert_hashes"_ns, certHashes);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = aStmtInsert->BindBlobByName("payload"_ns,
                                     rec.compressedPayload.Elements(),
                                     rec.compressedPayload.Length());
    NS_ENSURE_SUCCESS(rv, rv);
    rv = aStmtInsert->Execute();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return transaction.Commit();
}

// Reads the cert with aHash from ssl_certs and checks its SHA-256.
static bool ReadCertFromDB(mozIStorageStatement* aCertStmt,
                           const SSLTokensCacheCertHash& aHash,
                           nsTArray<uint8_t>& aOut, bool* aCorrupted) {
  mozStorageStatementScoper scoper(aCertStmt);
  if (NS_FAILED(aCertStmt->BindBlobArrayByName("hash"_ns, aHash))) {
    return false;
  }

  bool hasResult = false;
  // missing cert
  if (NS_FAILED(aCertStmt->ExecuteStep(&hasResult)) || !hasResult) {
    if (aCorrupted) {
      *aCorrupted = true;
    }
    return false;
  }

  uint32_t derLen = 0;
  const uint8_t* der = nullptr;
  if (NS_FAILED(aCertStmt->GetSharedBlob(0, &derLen, &der))) {
    if (aCorrupted) {
      *aCorrupted = true;
    }
    return false;
  }
  if (derLen > kMaxCertSize) {
    return false;
  }

#ifdef ENABLE_TESTS
  sCertRowsLoadedForTest++;
#endif

  nsTArray<uint8_t> digest;
  if (NS_FAILED(Digest::DigestBuf(SEC_OID_SHA256, der, derLen, digest))) {
    return false;
  }
  if (digest != aHash) {
    if (aCorrupted) {
      *aCorrupted = true;
    }
    return false;
  }

  // Copy the borrowed blob before the statement is reset.
  aOut.AppendElements(der, derLen);
  return true;
}

bool SSLTokensCache::LoadRecordCerts(mozIStorageStatement* aCertStmt,
                                     uint32_t aPayloadLen,
                                     const CertHashChain& aSucceededHashes,
                                     const CertHashChain& aHandshakeHashes,
                                     CertDERByFingerprint& aOut,
                                     bool* aCorrupted) {
  size_t totalSize = aPayloadLen;
  return ForEachCertHash(
      aSucceededHashes, aHandshakeHashes,
      [&](const SSLTokensCacheCertHash& aHash) {
        const auto key = AsHashKey(aHash);
        if (!aOut.Contains(key)) {
          nsTArray<uint8_t> der;
          if (!ReadCertFromDB(aCertStmt, aHash, der, aCorrupted)) {
            return false;
          }
          aOut.InsertOrUpdate(key, std::move(der));
        }

        // Count every occurrence: decoding copies DER bytes into each chain.
        const size_t length = aOut.Lookup(key).Data().Length();
        if (length > kMaxPayloadSize - totalSize) {
          return false;
        }
        totalSize += length;
        return true;
      });
}

// static
uint32_t SSLTokensCache::LoadValidRecordsFrom(mozIStorageConnection* aConn,
                                              uint32_t aLoadGen,
                                              bool* aCorrupted) {
  nsCOMPtr<mozIStorageStatement> certStmt;
  nsresult rv =
      aConn->CreateStatement("SELECT der FROM ssl_certs WHERE hash = :hash"_ns,
                             getter_AddRefs(certStmt));
  NS_ENSURE_SUCCESS(rv, 0);

  nsCOMPtr<mozIStorageStatement> stmt;
  rv = aConn->CreateStatement(
      nsLiteralCString("SELECT key, expiration_time, overridable_error, "
                       "cert_hashes, payload FROM ssl_tokens WHERE "
                       "expiration_time > :now"),
      getter_AddRefs(stmt));
  NS_ENSURE_SUCCESS(rv, 0);

  rv = stmt->BindInt64ByName("now"_ns, PR_Now());
  NS_ENSURE_SUCCESS(rv, 0);

  uint32_t count = 0;
#ifdef ENABLE_TESTS
  uint32_t rows = 0;
#endif
  bool hasResult = false;
  CertDERByFingerprint certs;
  for (;;) {
    rv = stmt->ExecuteStep(&hasResult);
    if (NS_FAILED(rv)) {
      LOG(("SSLTokensCache::LoadValidRecordsFrom: ExecuteStep failed"));
      if (aCorrupted) {
        *aCorrupted = true;
      }
      break;
    }
    if (!hasResult) {
      break;
    }
    SSLTokensCacheRecordInfo rec{};
    uint32_t hashesLen = 0;
    const uint8_t* hashes = nullptr;
    uint32_t len = 0;
    const uint8_t* blob = nullptr;
    if (NS_FAILED(stmt->GetUTF8String(0, rec.key)) ||
        NS_FAILED(stmt->GetSharedBlob(3, &hashesLen, &hashes)) ||
        NS_FAILED(stmt->GetSharedBlob(4, &len, &blob))) {
      // Corruption the open check can't see; don't pass off a partial load.
      if (aCorrupted) {
        *aCorrupted = true;
      }
      continue;
    }
    if (hashesLen > kMaxCertHashesSize ||
        !DecodeCertHashes(Span(hashes, hashesLen), rec.succeededHashes,
                          rec.handshakeHashes)) {
      if (aCorrupted) {
        *aCorrupted = true;
      }
      continue;
    }
    rec.expirationTime = stmt->AsInt64(1);
    rec.overridableError = static_cast<uint8_t>(stmt->AsInt32(2));
    rec.compressedPayload.AppendElements(blob, len);
    uint32_t payloadLen = 0;
    if (!ReadOriginalLen(rec.compressedPayload, payloadLen)) {
      continue;
    }
    if (!LoadRecordCerts(certStmt, payloadLen, rec.succeededHashes,
                         rec.handshakeHashes, certs, aCorrupted)) {
      continue;
    }
    // Loaded from disk, so restored by definition.
    if (PutFromPersisted(rec, certs, aLoadGen, /* aRestored */ true)) {
      count++;
    }
#ifdef ENABLE_TESTS
    WaitWhileLoadPausedForTest(++rows);
#endif
  }
  return count;
}

// static
nsresult SSLTokensCache::Put(const nsACString& aKey, const uint8_t* aToken,
                             uint32_t aTokenLen,
                             CommonSocketControl* aSocketControl) {
  SSLResumptionTokenInfo tokenInfo;
  if (SSL_GetResumptionTokenInfoWithFlags(
          aToken, aTokenLen, &tokenInfo, sizeof(tokenInfo),
          SSL_RESUMPTION_TOKEN_INFO_NO_PEER_CERT) != SECSuccess) {
    LOG(("  cannot get expiration time from the token, NSS error %d",
         PORT_GetError()));
    return NS_ERROR_FAILURE;
  }

  PRTime expirationTime = tokenInfo.expirationTime;
  bool plaintextSecret = tokenInfo.plaintextSecret;
  SSL_DestroyResumptionTokenInfo(&tokenInfo);

  return Put(aKey, aToken, aTokenLen, aSocketControl, expirationTime,
             plaintextSecret);
}

// static
nsresult SSLTokensCache::Put(const nsACString& aKey, const uint8_t* aToken,
                             uint32_t aTokenLen,
                             CommonSocketControl* aSocketControl,
                             PRTime aExpirationTime, bool aPlaintextSecret) {
  LOG(("SSLTokensCache::Put [key=%s, tokenLen=%u]",
       PromiseFlatCString(aKey).get(), aTokenLen));

  if (!aSocketControl) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsITransportSecurityInfo> securityInfo;
  nsresult rv = aSocketControl->GetSecurityInfo(getter_AddRefs(securityInfo));
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsCOMPtr<nsIX509Cert> cert;
  securityInfo->GetServerCert(getter_AddRefs(cert));
  if (!cert) {
    return NS_ERROR_FAILURE;
  }

  nsTArray<uint8_t> certBytes;
  rv = cert->GetRawDER(certBytes);
  if (NS_FAILED(rv)) {
    return rv;
  }

  Maybe<nsTArray<nsTArray<uint8_t>>> succeededCertChainBytes;
  nsTArray<RefPtr<nsIX509Cert>> succeededCertArray;
  rv = securityInfo->GetSucceededCertChain(succeededCertArray);
  if (NS_FAILED(rv)) {
    return rv;
  }

  auto getRawDerAll = [](nsTArray<RefPtr<nsIX509Cert>>& aCerts)
      -> Result<nsTArray<nsTArray<uint8_t>>, nsresult> {
    return TransformIntoNewArrayAbortOnErr(
        aCerts,
        [](const RefPtr<nsIX509Cert>& aCert)
            -> Result<nsTArray<uint8_t>, nsresult> {
          nsTArray<uint8_t> raw;
          MOZ_TRY(aCert->GetRawDER(raw));
          return std::move(raw);
        },
        fallible);
  };

  Maybe<bool> isBuiltCertChainRootBuiltInRoot;
  if (!succeededCertArray.IsEmpty()) {
    auto result = getRawDerAll(succeededCertArray);
    if (result.isErr()) return result.unwrapErr();
    succeededCertChainBytes.emplace(result.unwrap());

    bool builtInRoot = false;
    rv = securityInfo->GetIsBuiltCertChainRootBuiltInRoot(&builtInRoot);
    if (NS_FAILED(rv)) {
      return rv;
    }
    isBuiltCertChainRootBuiltInRoot.emplace(builtInRoot);
  }

  bool isEV;
  rv = securityInfo->GetIsExtendedValidation(&isEV);
  if (NS_FAILED(rv)) {
    return rv;
  }

  uint16_t certificateTransparencyStatus;
  rv = securityInfo->GetCertificateTransparencyStatus(
      &certificateTransparencyStatus);
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsITransportSecurityInfo::OverridableErrorCategory overridableErrorCategory;
  rv = securityInfo->GetOverridableErrorCategory(&overridableErrorCategory);
  if (NS_FAILED(rv)) {
    return rv;
  }

  Maybe<nsTArray<nsTArray<uint8_t>>> handshakeCertificatesBytes;
  nsTArray<RefPtr<nsIX509Cert>> handshakeCertificates;
  rv = securityInfo->GetHandshakeCertificates(handshakeCertificates);
  if (NS_FAILED(rv)) {
    return rv;
  }
  if (!handshakeCertificates.IsEmpty()) {
    auto result = getRawDerAll(handshakeCertificates);
    if (result.isErr()) return result.unwrapErr();
    handshakeCertificatesBytes.emplace(result.unwrap());
  }
  if (!handshakeCertificatesBytes ||
      (*handshakeCertificatesBytes)[0] != certBytes) {
    LOG(("SSLTokensCache::Put: server cert isn't the first handshake cert"));
    return NS_ERROR_FAILURE;
  }

  SessionCacheInfo info;
  info.mEVStatus = isEV ? psm::EVStatus::EV : psm::EVStatus::NotEV;
  info.mCertificateTransparencyStatus = certificateTransparencyStatus;
  info.mOverridableErrorCategory = overridableErrorCategory;
  info.mIsBuiltCertChainRootBuiltInRoot = isBuiltCertChainRootBuiltInRoot;
  info.mServerCertBytes = std::move(certBytes);
  info.mSucceededCertChainBytes = std::move(succeededCertChainBytes);
  info.mHandshakeCertificatesBytes = std::move(handshakeCertificatesBytes);

  CertHashChain succeededHashes;
  CertHashChain handshakeHashes;
  nsTArray<const nsTArray<uint8_t>*> certs;
  nsTArray<uint8_t> payload = SerializePayload(
      {aToken, aTokenLen}, info, succeededHashes, handshakeHashes, certs);

  // Apply the size limit to the decoded record, including shared certificate
  // bytes.
  if (payload.IsEmpty() ||
      DecodedRecordSize(payload.Length(), certs) > kMaxPayloadSize) {
    LOG(("SSLTokensCache::Put: serialization failed or record too large"));
    return NS_ERROR_FAILURE;
  }
  nsTArray<uint8_t> compressed = CompressRecord(payload);
  if (compressed.IsEmpty()) {
    LOG(("SSLTokensCache::Put: compression failed"));
    return NS_ERROR_FAILURE;
  }

  {
    StaticMutexAutoLock lock(sLock);

    if (!gInstance) {
      LOG(("  service not initialized"));
      return NS_ERROR_NOT_INITIALIZED;
    }

    // certs[i] is the DER of the i-th hash, in ForEachCertHash order.
    size_t i = 0;
    ForEachCertHash(
        succeededHashes, handshakeHashes,
        [&](const SSLTokensCacheCertHash& aHash) MOZ_REQUIRES(sLock) {
          gInstance->StoreOrAddRefCertLocked(aHash, *certs[i++]);
          return true;
        });

    auto rec = MakeRecord(
        aKey, aExpirationTime, static_cast<uint8_t>(overridableErrorCategory),
        /* aRestored */ false, aPlaintextSecret, std::move(succeededHashes),
        std::move(handshakeHashes), std::move(compressed));
    if (StaticPrefs::network_ssl_tokens_cache_persistence() &&
        !aPlaintextSecret && ShouldPersistIgnoringPlaintext(*rec, PR_Now())) {
      mozilla::glean::network::ssl_token_cache_wrapped_not_persisted.Add(1);
    }
    gInstance->InsertRecordLocked(std::move(rec));
    gInstance->LogStats();

  }  // sLock released

  return NS_OK;
}

// static
nsresult SSLTokensCache::Get(const nsACString& aKey, nsTArray<uint8_t>& aToken,
                             SessionCacheInfo& aResult, uint64_t* aTokenId,
                             bool* aRestored) {
  LOG(("SSLTokensCache::Get [key=%s]", PromiseFlatCString(aKey).get()));

  StaticMutexAutoLock lock(sLock);
  if (!gInstance) {
    LOG(("  service not initialized"));
    return NS_ERROR_NOT_INITIALIZED;
  }
  if (!gInstance->mLoadComplete && gInstance->mDBActive) {
    LOG(("SSLTokensCache::Get: connection before load complete"));
    mozilla::glean::network::ssl_token_cache_early_connections.Add(1);
  }
  while (UniquePtr<TokenCacheRecord> owned =
             gInstance->GetRecordLocked(aKey, aTokenId)) {
    nsTArray<uint8_t> payload = DecompressRecord(owned->mCompressedPayload);
    // mCertStore keeps these certs until owned is destroyed.
    auto lookupCert =
        [](const SSLTokensCacheCertHash& aHash) -> const nsTArray<uint8_t>* {
      // Called synchronously by DeserializeRecord while sLock is held.
      MOZ_PUSH_IGNORE_THREAD_SAFETY
      auto entry = gInstance->mCertStore.Lookup(AsHashKey(aHash));
      MOZ_POP_THREAD_SAFETY
      return entry ? &entry->mDER : nullptr;
    };
    if (!payload.IsEmpty() &&
        DeserializeRecord(payload, owned->mSucceededHashes,
                          owned->mHandshakeHashes, lookupCert, aToken,
                          aResult)) {
      mozilla::glean::network::ssl_token_cache_hits.Get("hit"_ns).Add(1);
      if (aRestored) {
        *aRestored = owned->mRestored;
      }
      return NS_OK;
    }
    LOG(("  discarding undecodable record [id=%" PRIu64 "]", owned->mId));
  }
  mozilla::glean::network::ssl_token_cache_hits.Get("miss"_ns).Add(1);
  return NS_ERROR_NOT_AVAILABLE;
}

UniquePtr<SSLTokensCache::TokenCacheRecord> SSLTokensCache::GetRecordLocked(
    const nsACString& aKey, uint64_t* aTokenId) {
  sLock.AssertCurrentThreadOwns();

  TokenCacheEntry* cacheEntry = nullptr;

  if (mTokenCacheRecords.Get(aKey, &cacheEntry)) {
    if (cacheEntry->RecordCount() == 0) {
      MOZ_ASSERT(false, "Found a cacheEntry with no records");
      mTokenCacheRecords.Remove(aKey);
      return nullptr;
    }

    PRTime now = PR_Now();

    while (cacheEntry->RecordCount() > 0) {
      const UniquePtr<TokenCacheRecord>& rec = cacheEntry->Get();

      if (rec->mExpirationTime > now) {
        uint64_t id = rec->mId;
        uint32_t size = rec->Size();
        UniquePtr<TokenCacheRecord> owned = cacheEntry->RemoveWithId(id);
        if (aTokenId) {
          *aTokenId = id;
        }
        mCacheSize -= size;
        if (cacheEntry->RecordCount() == 0) {
          mTokenCacheRecords.Remove(aKey);
        }
        LOG(("SSLTokensCache::GetRecordLocked: hit [key=%s, load_complete=%s]",
             PromiseFlatCString(aKey).get(), mLoadComplete ? "yes" : "no"));
        return owned;
      }

      LOG(("  skipping expired token [expirationTime=%" PRId64 ", now=%" PRId64
           "]",
           rec->mExpirationTime, now));
      mozilla::glean::network::ssl_token_cache_expired.Add(1);
      uint64_t expiredId = rec->mId;
      mCacheSize -= rec->Size();
      cacheEntry->RemoveWithId(expiredId);
    }

    mTokenCacheRecords.Remove(aKey);
  }

  LOG(("  token not found"));
  return nullptr;
}

// static
nsresult SSLTokensCache::Remove(const nsACString& aKey, uint64_t aId) {
  StaticMutexAutoLock lock(sLock);

  LOG(("SSLTokensCache::Remove [key=%s]", PromiseFlatCString(aKey).get()));

  if (!gInstance) {
    LOG(("  service not initialized"));
    return NS_ERROR_NOT_INITIALIZED;
  }

  return gInstance->RemoveLocked(aKey, aId);
}

nsresult SSLTokensCache::RemoveLocked(const nsACString& aKey, uint64_t aId) {
  sLock.AssertCurrentThreadOwns();

  LOG(("SSLTokensCache::RemoveLocked [key=%s, id=%" PRIu64 "]",
       PromiseFlatCString(aKey).get(), aId));

  TokenCacheEntry* cacheEntry;
  if (!mTokenCacheRecords.Get(aKey, &cacheEntry)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  UniquePtr<TokenCacheRecord> rec = cacheEntry->RemoveWithId(aId);
  if (!rec) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  mCacheSize -= rec->Size();
  if (cacheEntry->RecordCount() == 0) {
    mTokenCacheRecords.Remove(aKey);
  }

  // Release the record immediately, so mExpirationArray can be also updated.
  rec = nullptr;

  LogStats();

  return NS_OK;
}

// static
nsresult SSLTokensCache::RemoveAll(const nsACString& aKey) {
  StaticMutexAutoLock lock(sLock);

  LOG(("SSLTokensCache::RemoveAll [key=%s]", PromiseFlatCString(aKey).get()));

  if (!gInstance) {
    LOG(("  service not initialized"));
    return NS_ERROR_NOT_INITIALIZED;
  }

  return gInstance->RemoveAllLocked(aKey);
}

nsresult SSLTokensCache::RemoveAllLocked(const nsACString& aKey) {
  sLock.AssertCurrentThreadOwns();

  LOG(("SSLTokensCache::RemoveAllLocked [key=%s]",
       PromiseFlatCString(aKey).get()));

  UniquePtr<TokenCacheEntry> cacheEntry;
  if (!mTokenCacheRecords.Remove(aKey, &cacheEntry)) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  mCacheSize -= cacheEntry->Size();
  cacheEntry = nullptr;

  LogStats();

  return NS_OK;
}

void SSLTokensCache::OnRecordDestroyed(TokenCacheRecord* aRec) {
  // Always called from destructors of map entries while sLock is held.
  sLock.AssertCurrentThreadOwns();
  mExpirationArray.RemoveElement(aRec);
  ReleaseCertHashesLocked(aRec->mSucceededHashes, aRec->mHandshakeHashes);
}

void SSLTokensCache::EvictIfNecessary() {
  sLock.AssertCurrentThreadOwns();
  // kilobytes to bytes
  uint32_t capacity = StaticPrefs::network_ssl_tokens_cache_capacity() << 10;
  if (mCacheSize <= capacity) {
    return;
  }

  LOG(("SSLTokensCache::EvictIfNecessary: evicting"));

  mExpirationArray.Sort(ExpirationComparator());

  PRTime now = PR_Now();
  while (mCacheSize > capacity && mExpirationArray.Length() > 0) {
    auto* rec = mExpirationArray[0];
    // Only count evictions of tokens that are still valid; already-expired
    // tokens would be discarded on lookup anyway and are tracked separately
    // by ssl_token_cache_expired.
    if (rec->mExpirationTime > now) {
      mozilla::glean::network::ssl_token_cache_evictions.Add(1);
    }
    DebugOnly<nsresult> rv = RemoveLocked(rec->mKey, rec->mId);
    MOZ_ASSERT(NS_SUCCEEDED(rv),
               "mExpirationArray and mTokenCacheRecords are out of sync!");
  }
}

void SSLTokensCache::LogStats() {
  sLock.AssertCurrentThreadOwns();
  if (!LOG5_ENABLED()) {
    return;
  }
  LOG(("SSLTokensCache::LogStats [count=%zu, cacheSize=%u]",
       mExpirationArray.Length(), mCacheSize));
  for (const auto& ent : mTokenCacheRecords.Values()) {
    const UniquePtr<TokenCacheRecord>& rec = ent->Get();
    LOG(("  [key=%s, count=%d]", rec->mKey.get(), ent->RecordCount()));
  }
}

size_t SSLTokensCache::SizeOfIncludingThis(
    mozilla::MallocSizeOf mallocSizeOf) const {
  size_t n = mallocSizeOf(this);

  n += mTokenCacheRecords.ShallowSizeOfExcludingThis(mallocSizeOf);
  n += mExpirationArray.ShallowSizeOfExcludingThis(mallocSizeOf);

  for (const auto* rec : mExpirationArray) {
    n += mallocSizeOf(rec);
    n += rec->mKey.SizeOfExcludingThisIfUnshared(mallocSizeOf);
    for (const auto* chain : {&rec->mSucceededHashes, &rec->mHandshakeHashes}) {
      if (chain->isNothing()) {
        continue;
      }
      n += (*chain)->ShallowSizeOfExcludingThis(mallocSizeOf);
      for (const auto& hash : **chain) {
        n += hash.ShallowSizeOfExcludingThis(mallocSizeOf);
      }
    }
    n += rec->mCompressedPayload.ShallowSizeOfExcludingThis(mallocSizeOf);
  }

  n += mCertStore.ShallowSizeOfExcludingThis(mallocSizeOf);
  for (const auto& entry : mCertStore) {
    n += entry.GetKey().SizeOfExcludingThisIfUnshared(mallocSizeOf);
    n += entry.GetData().mDER.ShallowSizeOfExcludingThis(mallocSizeOf);
  }

  return n;
}

MOZ_DEFINE_MALLOC_SIZE_OF(SSLTokensCacheMallocSizeOf)

NS_IMETHODIMP
SSLTokensCache::CollectReports(nsIHandleReportCallback* aHandleReport,
                               nsISupports* aData, bool aAnonymize) {
  StaticMutexAutoLock lock(sLock);

  MOZ_COLLECT_REPORT("explicit/network/ssl-tokens-cache", KIND_HEAP,
                     UNITS_BYTES,
                     SizeOfIncludingThis(SSLTokensCacheMallocSizeOf),
                     "Memory used for the SSL tokens cache.");

  return NS_OK;
}

// static
void SSLTokensCache::ClearPrivateBrowsing() {
  LOG(("SSLTokensCache::ClearPrivateBrowsing"));
  StaticMutexAutoLock lock(sLock);
  if (!gInstance) {
    return;
  }
  gInstance->RemoveMatchingLocked([](const nsACString& aKey) {
    // Non-PBM keys have no '^' OA suffix at all — skip full deserialization.
    if (!aKey.Contains('^')) return false;
    return OAFromPeerId(aKey).mPrivateBrowsingId != 0;
  });
}

static void MaybeClearNSSSessionCache() {
  if (NSS_IsInitialized()) {
    SSL_ClearSessionCache();
  }
}

// static
void SSLTokensCache::ClearSessionCacheAndTokens() {
  MaybeClearNSSSessionCache();
  Clear();
}

// static
void SSLTokensCache::Clear() {
  LOG(("SSLTokensCache::Clear"));

  nsCOMPtr<nsISerialEventTarget> queue;
  {
    StaticMutexAutoLock lock(sLock);
    if (!gInstance) {
      LOG(("  service not initialized"));
      return;
    }

    gInstance->ClearCacheLocked();
    queue = gInstance->ActiveDBQueueLocked();
  }

  if (queue) {
    // The serial queue guarantees this runs after any pending write.
    DispatchToDBQueue(queue, "SSLTokensCache::ClearDB", [] { ClearDB(); });
  }
}

void SSLTokensCache::DoWrite(bool aSynchronous) {
  nsCOMPtr<nsISerialEventTarget> queue;
  {
    StaticMutexAutoLock lock(sLock);
    if (!gInstance) {
      return;
    }
    queue = ActiveDBQueueLocked();
  }

  if (!queue) {
    return;
  }

  if (aSynchronous) {
    // Test-only synchronous path; block until write completes on the queue.
    nsCOMPtr<nsIRunnable> event =
        NS_NewRunnableFunction("SSLTokensCache::DoWriteSync",
                               [] { CollectAndWriteSnapshotIfLoaded(); });
    NS_DispatchAndSpinEventLoopUntilComplete(
        "SSLTokensCache::DoWrite synchronous test-only write"_ns, queue.get(),
        event.forget());
    return;
  }

  DispatchToDBQueue(queue, "SSLTokensCache::DoWriteAsync",
                    [] { CollectAndWriteSnapshotIfLoaded(); });
}

// static
void SSLTokensCache::CollectAndWriteSnapshotIfLoaded() {
  MOZ_ASSERT(!NS_IsMainThread());
  // The serial queue guarantees this runs after any queued OpenAndLoad.
  SSLTokensCacheSnapshot snapshot;
  bool hadPendingRemovals = false;
  {
    StaticMutexAutoLock lock(sLock);
    if (!gInstance || !gInstance->mLoadComplete) {
      return;
    }
    gInstance->CollectRecordInfosLocked(snapshot,
                                        /* aFilterForPersistence */ true);
    hadPendingRemovals = !gInstance->mPendingDBRemovals.IsEmpty();
    gInstance->mPendingDBRemovals.Clear();  // The snapshot supersedes them.
  }
  if (!WriteSnapshotToDB(std::move(snapshot)) && hadPendingRemovals) {
    RemoveDBFileAfterFailure();
  }
}

// static
void SSLTokensCache::DispatchOpenAndLoad(uint32_t aLoadGen) {
  nsCOMPtr<nsISerialEventTarget> queue;
  {
    StaticMutexAutoLock lock(sLock);
    if (!gInstance || !gInstance->mDBActive) {
      return;
    }
    queue = gInstance->mDBQueue;
  }
  DispatchToDBQueue(queue, "SSLTokensCache::OpenAndLoad", [aLoadGen]() {
#ifdef ENABLE_TESTS
    WaitWhileLoadPausedForTest(0);
#endif
    uint32_t count = LoadFromDB(aLoadGen);
    OnLoadCompleteNotify(count);
  });
}

// static
void SSLTokensCache::OnLoadCompleteNotify(uint32_t aCount) {
  mozilla::glean::network::ssl_token_cache_persistence_records_loaded.Add(
      AssertedCast<int32_t>(aCount));

  TimeDuration elapsed;
  nsTArray<StringPredicate> removals;
  {
    StaticMutexAutoLock lock(sLock);
    if (!gInstance) {
      return;
    }
    gInstance->mLoadComplete = true;
    gInstance->mRemovalsBeforeLoad.Clear();
    removals = std::move(gInstance->mPendingDBRemovals);
    elapsed = TimeStamp::Now() - gInstance->mLoadStartTime;
  }
  if (!removals.IsEmpty()) {
    DeleteMatchesFromDB(std::move(removals));  // Already on mDBQueue.
  }
  mozilla::glean::network::ssl_token_cache_load_time.AccumulateRawDuration(
      elapsed);
  LOG(("SSLTokensCache::OnLoadCompleteNotify [records=%u, time=%.1fms]", aCount,
       elapsed.ToMilliseconds()));
#ifdef ENABLE_TESTS
  if (sLoadHookArmedForTest) {
    NotifyObserversForTest("ssl-tokens-cache-load-complete-for-test");
  }
#endif
}

// static
UniquePtr<SSLTokensCache::TokenCacheRecord> SSLTokensCache::MakeRecord(
    const nsACString& aKey, PRTime aExpirationTime, uint8_t aOverridableError,
    bool aRestored, bool aPlaintextSecret, CertHashChain&& aSucceededHashes,
    CertHashChain&& aHandshakeHashes, nsTArray<uint8_t>&& aCompressedPayload) {
  auto rec = std::make_unique<TokenCacheRecord>();
  rec->mKey = aKey;
  rec->mExpirationTime = aExpirationTime;
  rec->mOverridableError = aOverridableError;
  rec->mRestored = aRestored;
  rec->mPlaintextSecret = aPlaintextSecret;
  rec->mSucceededHashes = std::move(aSucceededHashes);
  rec->mHandshakeHashes = std::move(aHandshakeHashes);
  rec->mCompressedPayload = std::move(aCompressedPayload);
  return rec;
}

// static
bool SSLTokensCache::PutFromPersisted(const SSLTokensCacheRecordInfo& aRec,
                                      const CertDERByFingerprint& aCerts,
                                      uint32_t aExpectedGen, bool aRestored) {
  StaticMutexAutoLock lock(sLock);
  if (!gInstance || gInstance->mLoadGeneration != aExpectedGen) {
    return false;
  }
  if (MatchesAny(gInstance->mRemovalsBeforeLoad, aRec.key)) {
    return false;
  }
  if (!gInstance->StoreOrAddRefCertsLocked(aRec.succeededHashes,
                                           aRec.handshakeHashes, aCerts)) {
    return false;
  }
  auto rec = MakeRecord(
      aRec.key, static_cast<PRTime>(aRec.expirationTime), aRec.overridableError,
      aRestored, /* aPlaintextSecret */ true,
      CertHashChain(aRec.succeededHashes), CertHashChain(aRec.handshakeHashes),
      aRec.compressedPayload.Clone());
  gInstance->InsertRecordLocked(std::move(rec));
  return true;
}

#ifdef DEBUG
// static
void SSLTokensCache::AssertOnDBThread() {
  MOZ_ASSERT(!NS_IsMainThread());
  nsCOMPtr<nsISerialEventTarget> queue;
  {
    StaticMutexAutoLock lock(sLock);
    if (gInstance) {
      queue = gInstance->mDBQueue;
    }
  }
  MOZ_ASSERT(!queue || queue->IsOnCurrentThread());
}
#endif

// static
bool SSLTokensCache::IsPersistenceStillActive() {
  // Re-checked so a task queued before the pref flipped off can't write.
  if (!StaticPrefs::network_ssl_tokens_cache_persistence()) {
    return false;
  }
  StaticMutexAutoLock lock(sLock);
  return gInstance && gInstance->mDBActive;
}

// static
already_AddRefed<nsIFile> SSLTokensCache::GetCachedDBFile() {
  StaticMutexAutoLock lock(sLock);
  if (!gInstance || !gInstance->mDBFile) {
    return nullptr;
  }
  nsCOMPtr<nsIFile> dbFile = gInstance->mDBFile;
  return dbFile.forget();
}

// static
bool SSLTokensCache::RemoveDBFile(nsIFile* aDbFile) {
  MOZ_ASSERT(!NS_IsMainThread());
  return NS_SUCCEEDED(aDbFile->Remove(false));
}

// True if the file itself is unusable, so removal is the only recovery.
static bool DBFileIsUnusable(nsresult aRv) {
  switch (aRv) {
    case NS_ERROR_STORAGE_BUSY:
    case NS_ERROR_FILE_IS_LOCKED:
    case NS_ERROR_FILE_ACCESS_DENIED:
    case NS_ERROR_FILE_READ_ONLY:
    case NS_ERROR_OUT_OF_MEMORY:
    case NS_ERROR_NOT_INITIALIZED:
      return false;
    default:
      return true;
  }
}

// Shared by OpenDB and the test-only connection helper below.
static nsresult OpenUnsharedDatabaseAt(nsIFile* aFile,
                                       mozIStorageConnection** aConn) {
  nsCOMPtr<mozIStorageService> storageService =
      do_GetService(MOZ_STORAGE_SERVICE_CONTRACTID);
  if (!storageService) {
    return NS_ERROR_FAILURE;
  }
  return storageService->OpenUnsharedDatabase(
      aFile, mozIStorageService::CONNECTION_DEFAULT, aConn);
}

// static
bool SSLTokensCache::OpenDB(nsCOMPtr<mozIStorageConnection>& aConn) {
  MOZ_ASSERT(!NS_IsMainThread());
#ifdef DEBUG
  AssertOnDBThread();
#endif
  MOZ_ASSERT(!aConn);

  nsCOMPtr<nsIFile> dbFile = GetCachedDBFile();
  if (!dbFile) {
    return false;
  }

  nsresult rv = OpenUnsharedDatabaseAt(dbFile, getter_AddRefs(aConn));
  // Corruption or a missing encryption key never opens again; retry once.
  if (NS_FAILED(rv) && DBFileIsUnusable(rv)) {
    LOG(("SSLTokensCache::OpenDB: unopenable (0x%" PRIx32 "), removing",
         static_cast<uint32_t>(rv)));
    mozilla::glean::network::ssl_token_cache_db_errors.Get("open"_ns).Add(1);
    aConn = nullptr;
    RemoveDBFile(dbFile);
    rv = OpenUnsharedDatabaseAt(dbFile, getter_AddRefs(aConn));
  }
  if (NS_FAILED(rv)) {
    aConn = nullptr;
    return false;
  }

  // Disposable cache, so skip fsync entirely.
  (void)NS_WARN_IF(
      NS_FAILED(aConn->ExecuteSimpleSQL("PRAGMA synchronous = OFF"_ns)));

  // Tolerate brief contention; matches Places' DATABASE_BUSY_TIMEOUT_MS.
  (void)NS_WARN_IF(
      NS_FAILED(aConn->ExecuteSimpleSQL("PRAGMA busy_timeout = 100"_ns)));

  if (NS_FAILED(EnsureSchema(aConn))) {
    aConn->Close();
    aConn = nullptr;
    // Opened but unusable; drop it so the next open starts clean.
    mozilla::glean::network::ssl_token_cache_db_errors.Get("schema"_ns).Add(1);
    (void)RemoveDBFile(dbFile);
    return false;
  }

  return true;
}

// static
already_AddRefed<mozIStorageConnection> SSLTokensCache::OpenActiveDB() {
  MOZ_ASSERT(!NS_IsMainThread());
  if (!IsPersistenceStillActive()) {
    return nullptr;
  }
#ifdef ENABLE_TESTS
  uint32_t failOpens = sFailDBOpensForTest;
  if (failOpens &&
      sFailDBOpensForTest.compareExchange(failOpens, failOpens - 1)) {
    return nullptr;
  }
#endif
  nsCOMPtr<mozIStorageConnection> conn;
  if (!OpenDB(conn)) {
    return nullptr;
  }
  return conn.forget();
}

// static
uint32_t SSLTokensCache::LoadFromDB(uint32_t aLoadGen) {
  nsCOMPtr<mozIStorageConnection> conn = OpenActiveDB();
  if (!conn) {
    return 0;
  }
  bool corrupted = false;
  uint32_t count = LoadValidRecordsFrom(conn, aLoadGen, &corrupted);
  conn->Close();
  if (corrupted) {
    mozilla::glean::network::ssl_token_cache_db_errors.Get("read"_ns).Add(1);
    nsCOMPtr<nsIFile> dbFile = GetCachedDBFile();
    if (dbFile) {
      RemoveDBFile(dbFile);
    }
  }
  return count;
}

// static
bool SSLTokensCache::WriteSnapshotToDB(SSLTokensCacheSnapshot&& aSnapshot) {
  nsCOMPtr<mozIStorageConnection> conn = OpenActiveDB();
  if (!conn) {
    return false;
  }
  nsCOMPtr<mozIStorageStatement> stmtInsert;
  nsresult rv = PrepareInsertStatement(conn, stmtInsert);
  if (NS_SUCCEEDED(rv)) {
    rv = WriteSnapshotTo(conn, stmtInsert, aSnapshot);
    stmtInsert = nullptr;
  }
  if (NS_WARN_IF(NS_FAILED(rv))) {
    mozilla::glean::network::ssl_token_cache_db_errors.Get("write"_ns).Add(1);
  }
  conn->Close();
  return NS_SUCCEEDED(rv);
}

// static
void SSLTokensCache::ClearDB() {
  nsCOMPtr<mozIStorageConnection> conn = OpenActiveDB();
  if (!conn) {
    RemoveDBFileAfterFailure();
    return;
  }
  nsresult rv;
  {
    mozStorageTransaction transaction(conn, false);
    rv = transaction.Start();
    if (NS_SUCCEEDED(rv)) {
      rv = DeleteAllRows(conn);
    }
    if (NS_SUCCEEDED(rv)) {
      rv = transaction.Commit();
    }
  }
  conn->Close();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    RemoveDBFileAfterFailure();
  }
}

// static
void SSLTokensCache::DeleteMatchesFromDB(
    nsTArray<StringPredicate>&& aRemovals) {
  nsCOMPtr<mozIStorageConnection> conn = OpenActiveDB();
  if (!conn) {
    RemoveDBFileAfterFailure();
    return;
  }
  nsresult rv = DeleteMatchesFrom(conn, aRemovals);
  conn->Close();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    mozilla::glean::network::ssl_token_cache_db_errors.Get("delete"_ns).Add(1);
    RemoveDBFileAfterFailure();
  }
}

// static
nsresult SSLTokensCache::DeleteMatchesFrom(
    mozIStorageConnection* aConn, const nsTArray<StringPredicate>& aRemovals) {
  nsCOMPtr<mozIStorageStatement> select;
  nsresult rv = aConn->CreateStatement("SELECT DISTINCT key FROM ssl_tokens"_ns,
                                       getter_AddRefs(select));
  NS_ENSURE_SUCCESS(rv, rv);
  mozStorageTransaction transaction(aConn, false);
  rv = transaction.Start();
  NS_ENSURE_SUCCESS(rv, rv);
  nsTArray<nsCString> keys;
  {
    mozStorageStatementScoper scoper(select);
    bool hasResult = false;
    while (NS_SUCCEEDED(rv = select->ExecuteStep(&hasResult)) && hasResult) {
      nsAutoCString key;
      rv = select->GetUTF8String(0, key);
      NS_ENSURE_SUCCESS(rv, rv);
      if (MatchesAny(aRemovals, key)) {
        keys.AppendElement(key);
      }
    }
    NS_ENSURE_SUCCESS(rv, rv);
  }
  if (keys.IsEmpty()) {
    return NS_OK;
  }
  nsCOMPtr<mozIStorageStatement> stmt;
  rv = aConn->CreateStatement(
      "DELETE FROM ssl_tokens WHERE key IN carray(:keys)"_ns,
      getter_AddRefs(stmt));
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->BindArrayOfUTF8StringsByName("keys"_ns, keys);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = stmt->Execute();
  NS_ENSURE_SUCCESS(rv, rv);
  return transaction.Commit();
}

// static
void SSLTokensCache::RemoveDBFileSync() {
  nsCOMPtr<nsIFile> dbFile = GetCachedDBFile();
  if (dbFile) {
    (void)RemoveDBFile(dbFile);
  }
}

uint64_t SSLTokensCache::InsertRecordLocked(UniquePtr<TokenCacheRecord> aRec) {
  sLock.AssertCurrentThreadOwns();
  const uint64_t id = ++sRecordId;
  aRec->mId = id;

  // aRec->mKey must be read before AddRecord() moves aRec.
  TokenCacheEntry* cacheEntry = mTokenCacheRecords.GetOrInsertNew(aRec->mKey);
  if (cacheEntry->RecordCount() > 0) {
    mCacheSize -= cacheEntry->Size();
  }
  cacheEntry->AddRecord(std::move(aRec), mExpirationArray);
  mCacheSize += cacheEntry->Size();
  EvictIfNecessary();
  return id;
}

// static
bool SSLTokensCache::MatchesAny(const nsTArray<StringPredicate>& aPredicates,
                                const nsACString& aKey) {
  return std::any_of(
      aPredicates.begin(), aPredicates.end(),
      [&](const StringPredicate& aPredicate) { return aPredicate(aKey); });
}

// static
bool SSLTokensCache::ShouldPersist(const TokenCacheRecord& aRec, PRTime aNow) {
  return aRec.mPlaintextSecret && ShouldPersistIgnoringPlaintext(aRec, aNow);
}

// static
bool SSLTokensCache::ShouldPersistIgnoringPlaintext(
    const TokenCacheRecord& aRec, PRTime aNow) {
  return aRec.mExpirationTime > aNow && aRec.mOverridableError == 0 &&
         OAFromPeerId(aRec.mKey).mPrivateBrowsingId == 0;
}

// static
void SSLTokensCache::RemoveByHostMatchAndOAPattern(
    const mozilla::OriginAttributesPattern& aPattern,
    StringPredicate&& aHostMatches) {
  RemoveMatchingAndQueueDeletes(
      [pattern = aPattern,
       hostMatches = std::move(aHostMatches)](const nsACString& aKey) {
        nsDependentCSubstring host = HostFromBasePart(BasePartFromKey(aKey));
        return !host.IsEmpty() && hostMatches(host) &&
               pattern.Matches(OAFromPeerId(aKey));
      });
}

// static
void SSLTokensCache::RemoveByHostAndOAPattern(
    const nsACString& aHost, const mozilla::OriginAttributesPattern& aPattern) {
  LOG(("SSLTokensCache::RemoveByHostAndOAPattern"));
  RemoveByHostMatchAndOAPattern(
      aPattern,
      [host = nsCString(aHost),
       colonHost = nsCString(":"_ns + aHost)](const nsACString& aKeyHost) {
        return aKeyHost.Equals(host) || StringEndsWith(aKeyHost, colonHost);
      });
}

// static
void SSLTokensCache::ClearSessionCacheAndTokensForHost(
    const nsACString& aHost, const mozilla::OriginAttributesPattern& aPattern) {
  LOG(("SSLTokensCache::ClearSessionCacheAndTokensForHost"));
  MaybeClearNSSSessionCache();
  RemoveByHostAndOAPattern(aHost, aPattern);
}

// static
void SSLTokensCache::RemoveBySiteAndOAPattern(
    const nsACString& aSite, const mozilla::OriginAttributesPattern& aPattern) {
  LOG(("SSLTokensCache::RemoveBySiteAndOAPattern"));

  // Three cases for the host part of a key (after HostFromBasePart):
  //   "example.com"       — exact, no prefix        → Equals(aSite)
  //   "sub.example.com"   — subdomain                → ends with "."_ns + aSite
  //   "anon:example.com"  — connection-type prefix   → ends with ":"_ns + aSite
  // (Prefixed subdomains like "anon:sub.example.com" are caught by dotSite.)
  RemoveByHostMatchAndOAPattern(
      aPattern,
      [site = nsCString(aSite), dotSite = nsCString("."_ns + aSite),
       colonSite = nsCString(":"_ns + aSite)](const nsACString& aHost) {
        return aHost.Equals(site) || StringEndsWith(aHost, dotSite) ||
               StringEndsWith(aHost, colonSite);
      });
}

#ifdef ENABLE_TESTS

// Throwaway connection at aPath, independent of mDBQueue/OpenDB, so tests
// can exercise persistence without the pref or observers.
static already_AddRefed<mozIStorageConnection> OpenTestConnection(
    const nsACString& aPath) {
  nsCOMPtr<nsIFile> file;
  if (NS_FAILED(NS_NewNativeLocalFile(aPath, getter_AddRefs(file)))) {
    return nullptr;
  }
  nsCOMPtr<mozIStorageConnection> conn;
  if (NS_FAILED(OpenUnsharedDatabaseAt(file, getter_AddRefs(conn)))) {
    return nullptr;
  }
  return conn.forget();
}

// static
void SSLTokensCache::TriggerWriteForTest(const nsACString& aPath) {
  SSLTokensCacheSnapshot snapshot;
  {
    StaticMutexAutoLock lock(sLock);
    if (gInstance) {
      gInstance->CollectRecordInfosLocked(snapshot,
                                          /* aFilterForPersistence */ true);
    }
  }

  nsCOMPtr<mozIStorageConnection> conn = OpenTestConnection(aPath);
  if (!conn) {
    return;
  }
  if (NS_FAILED(EnsureSchema(conn))) {
    conn->Close();
    return;
  }

  nsCOMPtr<mozIStorageStatement> stmt;
  if (NS_SUCCEEDED(PrepareInsertStatement(conn, stmt))) {
    (void)NS_WARN_IF(NS_FAILED(WriteSnapshotTo(conn, stmt, snapshot)));
    stmt = nullptr;
  }
  conn->Close();
}

// static
void SSLTokensCache::LoadForTest(const nsACString& aPath, bool* aCorrupted) {
  uint32_t loadGen = 0;
  {
    StaticMutexAutoLock lock(sLock);
    if (gInstance) {
      loadGen = gInstance->mLoadGeneration;
    }
  }

  nsCOMPtr<mozIStorageConnection> conn = OpenTestConnection(aPath);
  if (!conn) {
    return;
  }
  bool tableExists = false;
  if (NS_SUCCEEDED(conn->TableExists("ssl_tokens"_ns, &tableExists)) &&
      tableExists) {
    LoadValidRecordsFrom(conn, loadGen, aCorrupted);
  }
  conn->Close();
}

// static
uint32_t SSLTokensCache::CertRowsLoadedForTest() {
  return sCertRowsLoadedForTest;
}

// static
uint32_t SSLTokensCache::CountForTest() {
  StaticMutexAutoLock lock(sLock);
  if (!gInstance) {
    return 0;
  }
  return gInstance->mTokenCacheRecords.Count();
}

// static
uint32_t SSLTokensCache::CacheSizeForTest() {
  StaticMutexAutoLock lock(sLock);
  return gInstance ? gInstance->mCacheSize : 0;
}

// static
void SSLTokensCache::PutForTest(const nsACString& aKey) {
  uint32_t gen = 0;
  {
    StaticMutexAutoLock lock(sLock);
    if (gInstance) {
      gen = gInstance->mLoadGeneration;
    }
  }
  uint8_t dummyToken[] = {0xDE, 0xAD, 0xBE, 0xEF};
  SessionCacheInfo info;
  // The cache treats certificate bytes as opaque.
  info.mServerCertBytes = {0x01, 0x02, 0x03};
  info.mHandshakeCertificatesBytes.emplace();
  info.mHandshakeCertificatesBytes->AppendElement(
      info.mServerCertBytes.Clone());

  CertHashChain succeededHashes;
  CertHashChain handshakeHashes;
  nsTArray<const nsTArray<uint8_t>*> certs;
  nsTArray<uint8_t> payload =
      SerializePayload({dummyToken, sizeof(dummyToken)}, info, succeededHashes,
                       handshakeHashes, certs);
  if (payload.IsEmpty()) {
    return;
  }
  nsTArray<uint8_t> compressed = CompressRecord(payload);
  if (compressed.IsEmpty()) {
    return;
  }

  CertDERByFingerprint certsByFingerprint;
  if (handshakeHashes.isSome()) {
    certsByFingerprint.InsertOrUpdate(AsHashKey((*handshakeHashes)[0]),
                                      std::move(info.mServerCertBytes));
  }

  SSLTokensCacheRecordInfo rec{};
  rec.key = aKey;
  rec.expirationTime = PR_Now() + 3600LL * PR_USEC_PER_SEC;
  rec.handshakeHashes = std::move(handshakeHashes);
  rec.compressedPayload = std::move(compressed);
  PutFromPersisted(rec, certsByFingerprint, gen, /* aRestored */ false);
}

// static
void SSLTokensCache::ReplaceAllRecordsForTest(
    SSLTokensCacheSnapshot&& aSnapshot) {
  StaticMutexAutoLock lock(sLock);
  if (!gInstance) {
    return;
  }
  gInstance->ClearCacheLocked();
  for (auto& info : aSnapshot.records) {
    if (!gInstance->StoreOrAddRefCertsLocked(
            info.succeededHashes, info.handshakeHashes, aSnapshot.certs)) {
      continue;
    }
    auto rec = MakeRecord(
        info.key, static_cast<PRTime>(info.expirationTime),
        info.overridableError, info.restored, /* aPlaintextSecret */ true,
        std::move(info.succeededHashes), std::move(info.handshakeHashes),
        std::move(info.compressedPayload));
    gInstance->InsertRecordLocked(std::move(rec));
  }
}

// static
void SSLTokensCache::HoldLoadForTest(bool aHold) {
  PauseLoadAfterRowsForTest(aHold ? 0 : kLoadNotPausedForTest);
}

// static
void SSLTokensCache::PauseLoadAfterRowsForTest(uint32_t aRows) {
  sLoadHookArmedForTest = true;
  SetLoadPausedAtRowsForTest(aRows);
}

// static
void SSLTokensCache::FailDBOpensForTest(uint32_t aCount) {
  sFailDBOpensArmedForTest = true;
  sFailDBOpensForTest = aCount;
}

#endif  // ENABLE_TESTS

// static
void SSLTokensCache::ReconcilePersistence(const char*, void*) {
  MOZ_ASSERT(XRE_IsParentProcess());
  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (!obs) {
    return;
  }

  bool wantPersistence = StaticPrefs::network_ssl_tokens_cache_persistence();
  // Checked before sLock; see SetupPersistenceLocked's doc comment.
  bool storageServiceAvailable = false;
  if (wantPersistence) {
    nsCOMPtr<mozIStorageService> storageService =
        do_GetService(MOZ_STORAGE_SERVICE_CONTRACTID);
    storageServiceAvailable = storageService != nullptr;
  }

  bool addObservers = false;
  bool removeObservers = false;
  bool loadOnActivation = false;
  uint32_t loadGen = 0;
  RefPtr<SSLTokensCache> instance;
  nsCOMPtr<nsISerialEventTarget> queueForRemoval;
  {
    StaticMutexAutoLock lock(sLock);
    instance = gInstance;
    if (!instance) {
      return;
    }
    bool wasRegistered = instance->mWriteObserversRegistered;
    instance->mWriteObserversRegistered = wantPersistence;
    addObservers = wantPersistence && !wasRegistered;
    removeObservers = !wantPersistence && wasRegistered;
    if (wantPersistence && !instance->mDBActive &&
        SetupPersistenceLocked(loadGen, storageServiceAvailable)) {
      // Only the startup activation loads from disk; a later re-enable
      // resumes writing without reloading a possibly-stale cache.
      loadOnActivation = !instance->mPrefCallbackRegistered;
      instance->mLoadComplete = !loadOnActivation;
    }
    if (!wantPersistence && instance->mDBActive) {
      instance->mDBActive = false;
      // mDBQueue stays alive, only the DB file is removed.
      queueForRemoval = instance->mDBQueue;
      instance->ClearCacheLocked();
    }
  }
  if (!wantPersistence) {
    // Drop any DB left behind by a session that had persistence on.
    if (queueForRemoval) {
      // Ordered after any pending write.
      DispatchToDBQueue(queueForRemoval, "SSLTokensCache::RemoveDB",
                        [] { RemoveDBFileSync(); });
    } else if (nsCOMPtr<nsIFile> dbFile = ProfileDBFile()) {
      NS_DispatchBackgroundTask(
          NS_NewRunnableFunction("SSLTokensCache::RemoveDB",
                                 [dbFile]() { (void)RemoveDBFile(dbFile); }));
    }
  }
  if (addObservers) {
    obs->AddObserver(instance, "application-background", false);
    obs->AddObserver(instance, "idle-daily", false);
  } else if (removeObservers) {
    obs->RemoveObserver(instance, "application-background");
    obs->RemoveObserver(instance, "idle-daily");
  }
  if (wantPersistence) {
    if (loadOnActivation) {
      DispatchOpenAndLoad(loadGen);
    }
    instance->RegisterShutdownBlocker();
  }
}

NS_IMETHODIMP
SSLTokensCache::Observe(nsISupports* aSubject, const char* aTopic,
                        const char16_t* aData) {
  if (!strcmp(aTopic, "application-background") ||
      !strcmp(aTopic, "idle-daily")) {
    LOG(("SSLTokensCache::Observe [topic=%s]", aTopic));
    DoWrite(false);
  } else if (!strcmp(aTopic, "profile-after-change")) {
    MOZ_ASSERT(XRE_IsParentProcess());
    LOG(("SSLTokensCache::Observe [topic=profile-after-change]"));
    ReconcilePersistence();
    if (!mPrefCallbackRegistered) {
      // Register for live pref changes now that the JS environment is ready.
      // Registering earlier (in Init()) would fire the callback during startup
      // pref-loading before the JS module loader is initialised.
      Preferences::RegisterCallback(&SSLTokensCache::ReconcilePersistence,
                                    "network.ssl_tokens_cache_persistence");
      mPrefCallbackRegistered = true;
    }
  } else if (!strcmp(aTopic, "last-pb-context-exited")) {
    MOZ_ASSERT(XRE_IsParentProcess());
    LOG(("SSLTokensCache::Observe [topic=last-pb-context-exited]"));
    // Remove only PBM entries from this process's token cache.
    ClearPrivateBrowsing();
  }
  return NS_OK;
}

// nsIAsyncShutdownBlocker

NS_IMETHODIMP
SSLTokensCache::BlockShutdown(nsIAsyncShutdownClient* /* aClient */) {
  LOG(("SSLTokensCache::BlockShutdown"));

  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(XRE_IsParentProcess());
#ifdef ENABLE_TESTS
  SetLoadPausedAtRowsForTest(kLoadNotPausedForTest);
#endif
  nsCOMPtr<nsISerialEventTarget> queue;
  {
    StaticMutexAutoLock lock(sLock);
    queue = ActiveDBQueueLocked();
  }
  if (!queue) {
    RemoveShutdownBlocker();
    return NS_OK;
  }

  // Write the final snapshot. The blocker keeps the main thread alive until
  // that completes.
  RefPtr<SSLTokensCache> self = this;
  InvokeAsync(queue.get(), __func__,
              [] {
                CollectAndWriteSnapshotIfLoaded();
                return GenericPromise::CreateAndResolve(true, __func__);
              })
      ->Then(
          GetMainThreadSerialEventTarget(), __func__,
          [self](bool) { self->RemoveShutdownBlocker(); },
          [self](nsresult) { self->RemoveShutdownBlocker(); });
  return NS_OK;
}

NS_IMETHODIMP
SSLTokensCache::GetName(nsAString& aName) {
  aName.AssignLiteral("SSLTokensCache: writing cache to disk");
  return NS_OK;
}

NS_IMETHODIMP
SSLTokensCache::GetState(nsIPropertyBag** aState) {
  *aState = nullptr;
  return NS_OK;
}

void SSLTokensCache::RegisterShutdownBlocker() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(XRE_IsParentProcess());
  {
    StaticMutexAutoLock lock(sLock);
    if (!gInstance || !gInstance->mDBQueue) {
      return;
    }
    if (gInstance->mShutdownBarrier) {
      return;
    }
  }
  // sLock is intentionally released before the AsyncShutdown calls below:
  // Service() and AddBlocker may run JS, which mustn't happen under sLock.
  nsCOMPtr<nsIAsyncShutdownService> svc = components::AsyncShutdown::Service();
  if (!svc) {
    return;
  }
  nsCOMPtr<nsIAsyncShutdownClient> client;
  svc->GetProfileBeforeChange(getter_AddRefs(client));
  if (!client) {
    return;
  }
  LOG(("SSLTokensCache::RegisterShutdownBlocker"));
  nsresult rv = client->AddBlocker(
      this, NS_LITERAL_STRING_FROM_CSTRING(__FILE__), __LINE__, u""_ns);
  if (NS_WARN_IF(NS_FAILED(rv))) {
    // Too late to add a blocker, so don't record one: Shutdown() would
    // assume it is pending and skip its own cleanup.
    return;
  }
  {
    StaticMutexAutoLock lock(sLock);
    mShutdownBarrier = client;
  }
}

void SSLTokensCache::RemoveShutdownBlocker() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(XRE_IsParentProcess());
  // Shutdown() left gInstance set so BlockShutdown could write, so
  // unregister here instead.
  UnregisterFromServices(this);

  nsCOMPtr<nsIAsyncShutdownClient> barrier;
  {
    StaticMutexAutoLock lock(sLock);
    barrier = std::move(mShutdownBarrier);
    gInstance = nullptr;
  }
  if (barrier) {
    barrier->RemoveBlocker(this);
  }
}

#ifdef ENABLE_TESTS
NS_IMPL_ISUPPORTS(SSLTokensCacheService, nsISSLTokensCache,
                  nsISSLTokensCacheTest)
#else
NS_IMPL_ISUPPORTS(SSLTokensCacheService, nsISSLTokensCache)
#endif

// Most of nsISSLTokensCache is parent-process-only.
static nsresult EnsureParentProcess() {
  MOZ_ASSERT(XRE_IsParentProcess());
  return XRE_IsParentProcess() ? NS_OK : NS_ERROR_NOT_AVAILABLE;
}

NS_IMETHODIMP
SSLTokensCacheService::ClearSSLExternalAndInternalSessionCache() {
  nsresult rv = EnsureParentProcess();
  if (NS_FAILED(rv)) {
    return rv;
  }
  SSLTokensCache::ClearSessionCacheAndTokens();
  return NS_OK;
}

NS_IMETHODIMP
SSLTokensCacheService::AsyncClearSSLExternalAndInternalSessionCache(
    JSContext* aCx, mozilla::dom::Promise** aPromise) {
  nsresult rv = EnsureParentProcess();
  if (NS_FAILED(rv)) {
    return rv;
  }

  nsIGlobalObject* globalObject = xpc::CurrentNativeGlobal(aCx);
  if (NS_WARN_IF(!globalObject)) {
    return NS_ERROR_FAILURE;
  }

  ErrorResult result;
  RefPtr<mozilla::dom::Promise> promise =
      mozilla::dom::Promise::Create(globalObject, result);
  if (NS_WARN_IF(result.Failed())) {
    return result.StealNSResult();
  }

  promise->MaybeResolveWithUndefined();
  SSLTokensCache::ClearSessionCacheAndTokens();
  promise.forget(aPromise);
  return NS_OK;
}

template <typename F>
static nsresult WithParsedOAPattern(const nsAString& aPatternJson, F&& aFunc) {
  nsresult rv = EnsureParentProcess();
  if (NS_FAILED(rv)) {
    return rv;
  }
  mozilla::OriginAttributesPattern pattern;
  if (!pattern.Init(aPatternJson)) {
    return NS_ERROR_INVALID_ARG;
  }
  aFunc(pattern);
  return NS_OK;
}

NS_IMETHODIMP
SSLTokensCacheService::RemoveSSLTokensByHostAndOriginAttributesPattern(
    const nsACString& aHost, const nsAString& aPattern) {
  return WithParsedOAPattern(aPattern, [&aHost](const auto& pattern) {
    SSLTokensCache::RemoveByHostAndOAPattern(aHost, pattern);
  });
}

NS_IMETHODIMP
SSLTokensCacheService::RemoveSSLTokensBySiteAndOriginAttributesPattern(
    const nsACString& aSite, const nsAString& aPattern) {
  return WithParsedOAPattern(aPattern, [&aSite](const auto& pattern) {
    SSLTokensCache::RemoveBySiteAndOAPattern(aSite, pattern);
  });
}

#ifdef ENABLE_TESTS
NS_IMETHODIMP
SSLTokensCacheService::CountSSLTokens(uint32_t* aCount) {
  *aCount = SSLTokensCache::CountForTest();
  return NS_OK;
}

NS_IMETHODIMP
SSLTokensCacheService::PutSSLTokenForTest(const nsACString& aKey) {
  SSLTokensCache::PutForTest(aKey);
  return NS_OK;
}

NS_IMETHODIMP
SSLTokensCacheService::HoldLoadForTest(bool aHold) {
  SSLTokensCache::HoldLoadForTest(aHold);
  return NS_OK;
}

NS_IMETHODIMP
SSLTokensCacheService::PauseLoadAfterRowsForTest(uint32_t aRows) {
  SSLTokensCache::PauseLoadAfterRowsForTest(aRows);
  return NS_OK;
}

NS_IMETHODIMP
SSLTokensCacheService::FailDBOpensForTest(uint32_t aCount) {
  SSLTokensCache::FailDBOpensForTest(aCount);
  return NS_OK;
}
#endif  // ENABLE_TESTS

}  // namespace net
}  // namespace mozilla
