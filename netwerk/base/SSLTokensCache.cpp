/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SSLTokensCache.h"

#include "CertVerifier.h"
#include "CommonSocketControl.h"
#include "TransportSecurityInfo.h"
#include "brotli/decode.h"
#include "brotli/encode.h"
#include "mozIStorageConnection.h"
#include "mozIStorageService.h"
#include "mozIStorageStatement.h"
#include "mozStorageCID.h"
#include "mozStorageHelper.h"
#include "mozilla/ArrayAlgorithm.h"
#include "mozilla/Components.h"
#include "mozilla/EndianUtils.h"
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
#include "nsThreadUtils.h"
#include "nss.h"
#include "prtime.h"
#include "ssl.h"
#include "sslexp.h"
#include "xpcpublic.h"

#define SSL_TOKENS_CACHE_DB_FILE "ssl_tokens_cache.sqlite"

constexpr int32_t kSSLTokensCacheSchemaVersion = 1;

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
//   [4: server_cert_len][server_cert bytes]
//   [1: has_succeeded_chain] if 1: [1: count] then for each [4: len][bytes]
//   [1: has_handshake_certs] if 1: [1: count] then for each [4: len][bytes]
//
// Stored compressed with an LE original-size prefix.
static constexpr size_t kOriginalLenSize = sizeof(uint32_t);

// Sanity bounds on the length fields above.
static constexpr uint32_t kMaxPayloadSize = 256 * 1024;
static constexpr uint32_t kMaxTokenSize = 256 * 1024;
static constexpr uint32_t kMaxCertSize = 0xffff;

template <typename T>
static void AppendLE(nsTArray<uint8_t>& aBuf, T aVal) {
  T le = mozilla::NativeEndian::swapToLittleEndian(aVal);
  aBuf.AppendElements(reinterpret_cast<const uint8_t*>(&le), sizeof(T));
}

static nsTArray<uint8_t> SerializeRecord(Span<const uint8_t> aToken,
                                         const SessionCacheInfo& aInfo) {
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
  AppendLE(buf, AssertedCast<uint32_t>(aInfo.mServerCertBytes.Length()));
  buf.AppendElements(aInfo.mServerCertBytes.Elements(),
                     aInfo.mServerCertBytes.Length());
  auto appendChain = [&](const Maybe<nsTArray<nsTArray<uint8_t>>>& aChain) {
    if (aChain.isNothing()) {
      buf.AppendElement(0);
      return;
    }
    buf.AppendElement(1);
    MOZ_RELEASE_ASSERT(aChain->Length() <= 0xFF);
    buf.AppendElement(static_cast<uint8_t>(aChain->Length()));
    for (const auto& cert : *aChain) {
      AppendLE(buf, AssertedCast<uint32_t>(cert.Length()));
      buf.AppendElements(cert.Elements(), cert.Length());
    }
  };
  appendChain(aInfo.mSucceededCertChainBytes);
  appendChain(aInfo.mHandshakeCertificatesBytes);
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

// Returns empty array on failure.
static nsTArray<uint8_t> DecompressRecord(Span<const uint8_t> aCompressed) {
  if (aCompressed.Length() < kOriginalLenSize) {
    return {};
  }
  uint32_t originalLen = LittleEndian::readUint32(aCompressed.Elements());
  if (originalLen > kMaxPayloadSize) {
    LOG(("SSLTokensCache: implausible payload originalLen %" PRIu32,
         originalLen));
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

static bool DeserializeRecord(Span<const uint8_t> aBuf,
                              nsTArray<uint8_t>& aToken,
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

  uint32_t serverCertLen;
  if (!r.Read(serverCertLen)) return false;
  if (serverCertLen > kMaxCertSize) return false;
  if (!r.Bytes(aInfo.mServerCertBytes, serverCertLen)) return false;

  auto readChain = [&](Maybe<nsTArray<nsTArray<uint8_t>>>& aChain) -> bool {
    uint8_t present;
    if (!r.Read(present)) return false;
    if (!present) {
      aChain = Nothing();
      return true;
    }
    uint8_t count;
    if (!r.Read(count)) return false;
    nsTArray<nsTArray<uint8_t>> chain;
    for (uint8_t i = 0; i < count; i++) {
      uint32_t certLen;
      if (!r.Read(certLen)) return false;
      if (certLen > kMaxCertSize) return false;
      nsTArray<uint8_t> cert;
      if (!r.Bytes(cert, certLen)) return false;
      chain.AppendElement(std::move(cert));
    }
    aChain = Some(std::move(chain));
    return true;
  };

  if (!readChain(aInfo.mSucceededCertChainBytes)) return false;
  if (!readChain(aInfo.mHandshakeCertificatesBytes)) return false;
  return r.AtEnd();
}

StaticRefPtr<SSLTokensCache> SSLTokensCache::gInstance;
StaticMutex SSLTokensCache::sLock;
uint64_t SSLTokensCache::sRecordId = 0;

SSLTokensCache::TokenCacheRecord::~TokenCacheRecord() {
  if (!gInstance) {
    return;
  }
  gInstance->OnRecordDestroyed(this);
}

uint32_t SSLTokensCache::TokenCacheRecord::Size() const {
  return mKey.Length() + mCompressedPayload.Length();
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

void SSLTokensCache::ClearCacheLocked() {
  sLock.AssertCurrentThreadOwns();
  mLoadGeneration++;
  mExpirationArray.Clear();
  mTokenCacheRecords.Clear();
  mCacheSize = 0;
}

void SSLTokensCache::CollectRecordInfosLocked(
    nsTArray<SSLTokensCacheRecordInfo>& aOut,
    bool aFilterForPersistence) const {
  sLock.AssertCurrentThreadOwns();
  for (const auto& entry : mTokenCacheRecords.Values()) {
    for (const auto& rec : entry->Records()) {
      if (aFilterForPersistence &&
          !ShouldPersistKey(rec->mKey, rec->mOverridableError)) {
        continue;
      }
      auto info = aOut.AppendElement();
      info->key = rec->mKey;
      info->expirationTime = static_cast<int64_t>(rec->mExpirationTime);
      info->overridableError = rec->mOverridableError;
      info->restored = rec->mRestored;
      info->id = rec->mId;
      info->compressedPayload = rec->mCompressedPayload.Clone();
    }
  }
}

// static
void SSLTokensCache::GetAllRecords(nsTArray<SSLTokensCacheRecordInfo>& aOut) {
  StaticMutexAutoLock lock(sLock);
  if (!gInstance) {
    return;
  }
  gInstance->CollectRecordInfosLocked(aOut, /* aFilterForPersistence */ false);
}

// static
bool SSLTokensCache::DecodeCompressedPayload(Span<const uint8_t> aCompressed,
                                             nsTArray<uint8_t>& aToken,
                                             SessionCacheInfo& aInfo,
                                             uint32_t* aDecompressedLength) {
  nsTArray<uint8_t> payload = DecompressRecord(aCompressed);
  if (payload.IsEmpty()) {
    return false;
  }
  if (aDecompressedLength) {
    *aDecompressedLength = payload.Length();
  }
  return DeserializeRecord(payload, aToken, aInfo);
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
  return aConn->ExecuteSimpleSQL(
      nsLiteralCString("CREATE TABLE ssl_tokens ("
                       "key TEXT NOT NULL,"
                       "expiration_time INTEGER NOT NULL,"
                       "overridable_error INTEGER NOT NULL,"
                       "payload BLOB NOT NULL)"));
}

// static
nsresult SSLTokensCache::EnsureSchema(mozIStorageConnection* aConn) {
  bool tableExists = false;
  nsresult rv = aConn->TableExists("ssl_tokens"_ns, &tableExists);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!tableExists) {
    return CreateSchemaOn(aConn);
  }

  // An unreadable version is as good as a wrong one: recreate.
  int32_t dbSchemaVersion = 0;
  rv = aConn->GetSchemaVersion(&dbSchemaVersion);
  if (NS_SUCCEEDED(rv) && dbSchemaVersion == kSSLTokensCacheSchemaVersion) {
    return NS_OK;
  }

  mozStorageTransaction transaction(aConn, false);
  rv = transaction.Start();
  NS_ENSURE_SUCCESS(rv, rv);
  rv = aConn->ExecuteSimpleSQL("DROP TABLE ssl_tokens"_ns);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = CreateSchemaOn(aConn);
  NS_ENSURE_SUCCESS(rv, rv);
  return transaction.Commit();
}

// static
nsresult SSLTokensCache::PrepareInsertStatement(
    mozIStorageConnection* aConn, nsCOMPtr<mozIStorageStatement>& aStmtInsert) {
  return aConn->CreateStatement(
      nsLiteralCString("INSERT INTO ssl_tokens (key, expiration_time, "
                       "overridable_error, payload) VALUES (:key, "
                       ":expiration_time, :overridable_error, :payload)"),
      getter_AddRefs(aStmtInsert));
}

// static
nsresult SSLTokensCache::WriteSnapshotTo(
    mozIStorageConnection* aConn, mozIStorageStatement* aStmtInsert,
    const nsTArray<SSLTokensCacheRecordInfo>& aSnapshot) {
  mozStorageTransaction transaction(aConn, false);
  nsresult rv = transaction.Start();
  NS_ENSURE_SUCCESS(rv, rv);

  rv = aConn->ExecuteSimpleSQL("DELETE FROM ssl_tokens"_ns);
  NS_ENSURE_SUCCESS(rv, rv);

  for (const auto& rec : aSnapshot) {
    mozStorageStatementScoper scoper(aStmtInsert);
    rv = aStmtInsert->BindUTF8StringByName("key"_ns, rec.key);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = aStmtInsert->BindInt64ByName("expiration_time"_ns, rec.expirationTime);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = aStmtInsert->BindInt32ByName("overridable_error"_ns,
                                      rec.overridableError);
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

// static
uint32_t SSLTokensCache::LoadValidRecordsFrom(mozIStorageConnection* aConn,
                                              uint32_t aLoadGen,
                                              bool* aCorrupted) {
  nsCOMPtr<mozIStorageStatement> stmt;
  nsresult rv = aConn->CreateStatement(
      nsLiteralCString("SELECT key, expiration_time, overridable_error, "
                       "payload FROM ssl_tokens WHERE expiration_time > "
                       ":now"),
      getter_AddRefs(stmt));
  NS_ENSURE_SUCCESS(rv, 0);

  rv = stmt->BindInt64ByName("now"_ns, PR_Now());
  NS_ENSURE_SUCCESS(rv, 0);

  uint32_t count = 0;
  bool hasResult = false;
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
    uint32_t len = 0;
    const uint8_t* blob = nullptr;
    if (NS_FAILED(stmt->GetUTF8String(0, rec.key)) ||
        NS_FAILED(stmt->GetSharedBlob(3, &len, &blob))) {
      // Corruption the open check can't see; don't pass off a partial load.
      if (aCorrupted) {
        *aCorrupted = true;
      }
      continue;
    }
    rec.expirationTime = stmt->AsInt64(1);
    rec.overridableError = static_cast<uint8_t>(stmt->AsInt32(2));
    rec.compressedPayload.AppendElements(blob, len);
    // Loaded from disk, so restored by definition.
    if (PutFromPersisted(rec, aLoadGen, /* aRestored */ true)) {
      count++;
    }
  }
  return count;
}

// static
nsresult SSLTokensCache::Put(const nsACString& aKey, const uint8_t* aToken,
                             uint32_t aTokenLen,
                             CommonSocketControl* aSocketControl) {
  PRTime expirationTime;
  SSLResumptionTokenInfo tokenInfo;
  if (SSL_GetResumptionTokenInfoWithFlags(
          aToken, aTokenLen, &tokenInfo, sizeof(tokenInfo),
          SSL_RESUMPTION_TOKEN_INFO_NO_PEER_CERT) != SECSuccess) {
    LOG(("  cannot get expiration time from the token, NSS error %d",
         PORT_GetError()));
    return NS_ERROR_FAILURE;
  }

  expirationTime = tokenInfo.expirationTime;
  SSL_DestroyResumptionTokenInfo(&tokenInfo);

  return Put(aKey, aToken, aTokenLen, aSocketControl, expirationTime);
}

// static
nsresult SSLTokensCache::Put(const nsACString& aKey, const uint8_t* aToken,
                             uint32_t aTokenLen,
                             CommonSocketControl* aSocketControl,
                             PRTime aExpirationTime) {
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

  SessionCacheInfo info;
  info.mEVStatus = isEV ? psm::EVStatus::EV : psm::EVStatus::NotEV;
  info.mCertificateTransparencyStatus = certificateTransparencyStatus;
  info.mOverridableErrorCategory = overridableErrorCategory;
  info.mIsBuiltCertChainRootBuiltInRoot = isBuiltCertChainRootBuiltInRoot;
  info.mServerCertBytes = std::move(certBytes);
  info.mSucceededCertChainBytes = std::move(succeededCertChainBytes);
  info.mHandshakeCertificatesBytes = std::move(handshakeCertificatesBytes);

  nsTArray<uint8_t> payload = SerializeRecord({aToken, aTokenLen}, info);
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

    auto makeRecord = [&]() MOZ_REQUIRES(sLock) {
      auto rec = std::make_unique<TokenCacheRecord>();
      rec->mKey = aKey;
      rec->mExpirationTime = aExpirationTime;
      rec->mOverridableError = static_cast<uint8_t>(overridableErrorCategory);
      rec->mCompressedPayload = std::move(compressed);
      return rec;
    };

    gInstance->InsertRecordLocked(makeRecord());
    gInstance->LogStats();

  }  // sLock released

  return NS_OK;
}

// static
nsresult SSLTokensCache::Get(const nsACString& aKey, nsTArray<uint8_t>& aToken,
                             SessionCacheInfo& aResult, uint64_t* aTokenId) {
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
    if (!payload.IsEmpty() && DeserializeRecord(payload, aToken, aResult)) {
      mozilla::glean::network::ssl_token_cache_hits.Get("hit"_ns).Add(1);
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
    n += rec->mCompressedPayload.ShallowSizeOfExcludingThis(mallocSizeOf);
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
    if (gInstance->mDBActive) {
      queue = gInstance->mDBQueue;
    }
  }

  if (queue) {
    // The serial queue guarantees this runs after any pending write.
    queue->Dispatch(
        NS_NewRunnableFunction("SSLTokensCache::ClearDB", [] { ClearDB(); }),
        NS_DISPATCH_EVENT_MAY_BLOCK);
  }
}

void SSLTokensCache::DoWrite(bool aSynchronous) {
  nsCOMPtr<nsISerialEventTarget> queue;
  {
    StaticMutexAutoLock lock(sLock);
    if (!gInstance) {
      return;
    }
    if (mDBActive) {
      queue = mDBQueue;
    }
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

  queue->Dispatch(
      NS_NewRunnableFunction("SSLTokensCache::DoWriteAsync",
                             [] { CollectAndWriteSnapshotIfLoaded(); }),
      NS_DISPATCH_EVENT_MAY_BLOCK);
}

// static
void SSLTokensCache::CollectAndWriteSnapshotIfLoaded() {
  MOZ_ASSERT(!NS_IsMainThread());
  // The serial queue guarantees this runs after any queued OpenAndLoad.
  nsTArray<SSLTokensCacheRecordInfo> snapshot;
  {
    StaticMutexAutoLock lock(sLock);
    if (!gInstance || !gInstance->mLoadComplete) {
      return;
    }
    gInstance->CollectRecordInfosLocked(snapshot,
                                        /* aFilterForPersistence */ true);
  }
  WriteSnapshotToDB(std::move(snapshot));
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
  queue->Dispatch(NS_NewRunnableFunction("SSLTokensCache::OpenAndLoad",
                                         [aLoadGen]() {
                                           uint32_t count =
                                               LoadFromDB(aLoadGen);
                                           OnLoadCompleteNotify(count);
                                         }),
                  NS_DISPATCH_EVENT_MAY_BLOCK);
}

// static
void SSLTokensCache::OnLoadCompleteNotify(uint32_t aCount) {
  mozilla::glean::network::ssl_token_cache_persistence_records_loaded.Add(
      AssertedCast<int32_t>(aCount));

  TimeDuration elapsed;
  {
    StaticMutexAutoLock lock(sLock);
    if (!gInstance) {
      return;
    }
    gInstance->mLoadComplete = true;
    elapsed = TimeStamp::Now() - gInstance->mLoadStartTime;
  }
  mozilla::glean::network::ssl_token_cache_load_time.AccumulateRawDuration(
      elapsed);
  LOG(("SSLTokensCache::OnLoadCompleteNotify [records=%u, time=%.1fms]", aCount,
       elapsed.ToMilliseconds()));
}

// static
UniquePtr<SSLTokensCache::TokenCacheRecord> SSLTokensCache::MakeRecord(
    const nsACString& aKey, PRTime aExpirationTime, uint8_t aOverridableError,
    bool aRestored, nsTArray<uint8_t>&& aCompressedPayload) {
  auto rec = std::make_unique<TokenCacheRecord>();
  rec->mKey = aKey;
  rec->mExpirationTime = aExpirationTime;
  rec->mOverridableError = aOverridableError;
  rec->mRestored = aRestored;
  rec->mCompressedPayload = std::move(aCompressedPayload);
  return rec;
}

// static
bool SSLTokensCache::PutFromPersisted(const SSLTokensCacheRecordInfo& aRec,
                                      uint32_t aExpectedGen, bool aRestored) {
  StaticMutexAutoLock lock(sLock);
  if (!gInstance || gInstance->mLoadGeneration != aExpectedGen) {
    return false;
  }
  auto rec = MakeRecord(aRec.key, static_cast<PRTime>(aRec.expirationTime),
                        aRec.overridableError, aRestored,
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
void SSLTokensCache::WriteSnapshotToDB(
    nsTArray<SSLTokensCacheRecordInfo>&& aSnapshot) {
  nsCOMPtr<mozIStorageConnection> conn = OpenActiveDB();
  if (!conn) {
    return;
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
}

// static
void SSLTokensCache::ClearDB() {
  nsCOMPtr<mozIStorageConnection> conn = OpenActiveDB();
  if (!conn) {
    return;
  }
  (void)NS_WARN_IF(
      NS_FAILED(conn->ExecuteSimpleSQL("DELETE FROM ssl_tokens"_ns)));
  conn->Close();
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
bool SSLTokensCache::ShouldPersistKey(const nsACString& aKey,
                                      uint8_t aOverridableError) {
  return aOverridableError == 0 && OAFromPeerId(aKey).mPrivateBrowsingId == 0;
}

// static
void SSLTokensCache::RemoveByMatchAndOAPattern(
    const nsACString& aValue, const nsACString& aSeparatedValue,
    const mozilla::OriginAttributesPattern& aPattern) {
  StaticMutexAutoLock lock(sLock);
  if (!gInstance) {
    return;
  }
  gInstance->RemoveMatchingLocked(
      [&aValue, &aSeparatedValue, &aPattern](const nsACString& aKey) {
        nsDependentCSubstring host = HostFromBasePart(BasePartFromKey(aKey));
        return !host.IsEmpty() &&
               (host.Equals(aValue) || StringEndsWith(host, aSeparatedValue)) &&
               aPattern.Matches(OAFromPeerId(aKey));
      });
}

// static
void SSLTokensCache::RemoveByHostAndOAPattern(
    const nsACString& aHost, const mozilla::OriginAttributesPattern& aPattern) {
  LOG(("SSLTokensCache::RemoveByHostAndOAPattern"));
  RemoveByMatchAndOAPattern(aHost, ":"_ns + aHost, aPattern);
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
  nsAutoCString dotSite("."_ns + aSite);
  nsAutoCString colonSite(":"_ns + aSite);
  StaticMutexAutoLock lock(sLock);
  if (!gInstance) {
    return;
  }
  gInstance->RemoveMatchingLocked(
      [&aSite, &dotSite, &colonSite, &aPattern](const nsACString& aKey) {
        nsDependentCSubstring host = HostFromBasePart(BasePartFromKey(aKey));
        return !host.IsEmpty() &&
               (host.Equals(aSite) || StringEndsWith(host, dotSite) ||
                StringEndsWith(host, colonSite)) &&
               aPattern.Matches(OAFromPeerId(aKey));
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
  nsTArray<SSLTokensCacheRecordInfo> snapshot;
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
void SSLTokensCache::LoadForTest(const nsACString& aPath) {
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
    LoadValidRecordsFrom(conn, loadGen);
  }
  conn->Close();
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
  nsTArray<uint8_t> payload =
      SerializeRecord({dummyToken, sizeof(dummyToken)}, info);
  nsTArray<uint8_t> compressed = CompressRecord(payload);
  if (compressed.IsEmpty()) {
    return;
  }
  SSLTokensCacheRecordInfo rec{};
  rec.key = aKey;
  rec.expirationTime = PR_Now() + 3600LL * PR_USEC_PER_SEC;
  rec.compressedPayload = std::move(compressed);
  PutFromPersisted(rec, gen, /* aRestored */ false);
}

// static
void SSLTokensCache::ReplaceAllRecordsForTest(
    nsTArray<SSLTokensCacheRecordInfo>&& aRecords) {
  StaticMutexAutoLock lock(sLock);
  if (!gInstance) {
    return;
  }
  gInstance->ClearCacheLocked();
  for (auto& info : aRecords) {
    auto rec = MakeRecord(info.key, static_cast<PRTime>(info.expirationTime),
                          info.overridableError, info.restored,
                          std::move(info.compressedPayload));
    gInstance->InsertRecordLocked(std::move(rec));
  }
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
      queueForRemoval->Dispatch(
          NS_NewRunnableFunction("SSLTokensCache::RemoveDB",
                                 []() { RemoveDBFileSync(); }),
          NS_DISPATCH_EVENT_MAY_BLOCK);
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
  nsCOMPtr<nsISerialEventTarget> queue;
  {
    StaticMutexAutoLock lock(sLock);
    if (mDBActive) {
      queue = mDBQueue;
    }
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
#endif  // ENABLE_TESTS

}  // namespace net
}  // namespace mozilla
