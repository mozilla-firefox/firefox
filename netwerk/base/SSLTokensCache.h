/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef SSLTokensCache_h_
#define SSLTokensCache_h_

#include "CertVerifier.h"  // For EVStatus
#include "mozilla/Maybe.h"
#include "mozilla/OriginAttributes.h"
#include "mozilla/Span.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPrefs_network.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/net/DashboardTypes.h"
#include "nsClassHashtable.h"
#include "nsIAsyncShutdown.h"
#include "nsIFile.h"
#include "nsIMemoryReporter.h"
#include "nsIObserver.h"
#include "nsISSLTokensCache.h"
#include "nsISerialEventTarget.h"
#include "nsISupportsImpl.h"
#include "nsITransportSecurityInfo.h"
#include "nsTArray.h"
#include "nsTHashMap.h"
#include "nsXULAppAPI.h"

#ifdef ENABLE_TESTS
#  include "nsISSLTokensCacheTest.h"
#endif

class CommonSocketControl;
class mozIStorageConnection;
class mozIStorageService;
class mozIStorageStatement;

namespace mozilla {
namespace net {

struct SessionCacheInfo {
  SessionCacheInfo Clone() const;

  psm::EVStatus mEVStatus = psm::EVStatus::NotEV;
  uint16_t mCertificateTransparencyStatus =
      nsITransportSecurityInfo::CERTIFICATE_TRANSPARENCY_NOT_APPLICABLE;
  nsTArray<uint8_t> mServerCertBytes;
  Maybe<nsTArray<nsTArray<uint8_t>>> mSucceededCertChainBytes;
  Maybe<bool> mIsBuiltCertChainRootBuiltInRoot;
  nsITransportSecurityInfo::OverridableErrorCategory mOverridableErrorCategory =
      nsITransportSecurityInfo::OverridableErrorCategory::ERROR_UNSET;
  Maybe<nsTArray<nsTArray<uint8_t>>> mHandshakeCertificatesBytes;
};

class SSLTokensCache : public nsIMemoryReporter,
                       public nsIObserver,
                       public nsIAsyncShutdownBlocker {
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSIMEMORYREPORTER
  NS_DECL_NSIOBSERVER
  NS_DECL_NSIASYNCSHUTDOWNBLOCKER

  friend class ExpirationComparator;

  static nsresult Init();
  static nsresult Shutdown();

  static nsresult Put(const nsACString& aKey, const uint8_t* aToken,
                      uint32_t aTokenLen, CommonSocketControl* aSocketControl);
  static nsresult Put(const nsACString& aKey, const uint8_t* aToken,
                      uint32_t aTokenLen, CommonSocketControl* aSocketControl,
                      PRTime aExpirationTime);
  static nsresult Get(const nsACString& aKey, nsTArray<uint8_t>& aToken,
                      SessionCacheInfo& aResult, uint64_t* aTokenId = nullptr);
  static nsresult Remove(const nsACString& aKey, uint64_t aId);
  static nsresult RemoveAll(const nsACString& aKey);
  static void Clear();
  // Clears the NSS in-memory client session cache (if NSS is initialized)
  // and all resumption tokens. This is the entry point for "clear the TLS
  // session cache" from anywhere in the tree; PSM no longer has its own copy
  // of this logic.
  static void ClearSessionCacheAndTokens();
  // Remove only private-browsing entries (privateBrowsingId != 0).
  static void ClearPrivateBrowsing();
  static void RemoveByHostAndOAPattern(
      const nsACString& aHost, const mozilla::OriginAttributesPattern& aPattern)
      MOZ_EXCLUDES(sLock);
  // Removes aHost's matching resumption tokens and clears the NSS in-memory
  // client session cache.
  static void ClearSessionCacheAndTokensForHost(
      const nsACString& aHost, const mozilla::OriginAttributesPattern& aPattern)
      MOZ_EXCLUDES(sLock);
  static void RemoveBySiteAndOAPattern(
      const nsACString& aSite, const mozilla::OriginAttributesPattern& aPattern)
      MOZ_EXCLUDES(sLock);

  // Non-consuming snapshot of all current records, for about:networking.
  static void GetAllRecords(nsTArray<SSLTokensCacheRecordInfo>& aOut);

  // aDecompressedLength, if non-null, receives the size of the decompressed
  // payload (token + serialized SessionCacheInfo combined), for reporting
  // the compression ratio against the compressed size.
  static bool DecodeCompressedPayload(mozilla::Span<const uint8_t> aCompressed,
                                      nsTArray<uint8_t>& aToken,
                                      SessionCacheInfo& aInfo,
                                      uint32_t* aDecompressedLength = nullptr);

#ifdef ENABLE_TESTS
  // Test-only helpers.
  static void TriggerWriteForTest(const nsACString& aPath);
  static void LoadForTest(const nsACString& aPath);
  static uint32_t CountForTest();
  static void PutForTest(const nsACString& aKey);
  static uint32_t CacheSizeForTest();
  static void ReplaceAllRecordsForTest(
      nsTArray<SSLTokensCacheRecordInfo>&& aRecords);
#endif

 private:
  class TokenCacheRecord;  // defined below

  SSLTokensCache();
  virtual ~SSLTokensCache();

  nsresult RemoveLocked(const nsACString& aKey, uint64_t aId)
      MOZ_REQUIRES(sLock);
  nsresult RemoveAllLocked(const nsACString& aKey) MOZ_REQUIRES(sLock);
  // Extracts the first valid (non-expired) record for aKey, updating
  // mCacheSize and mExpirationArray.  Sets *aTokenId if non-null.
  // Returns the owned record on hit, nullptr on miss.
  UniquePtr<TokenCacheRecord> GetRecordLocked(const nsACString& aKey,
                                              uint64_t* aTokenId)
      MOZ_REQUIRES(sLock);

  void EvictIfNecessary() MOZ_REQUIRES(sLock);
  void LogStats() MOZ_REQUIRES(sLock);
  void ClearCacheLocked() MOZ_REQUIRES(sLock);
  // Returns true if a token for aKey with aOverridableError should be
  // persisted to disk (not PBM and no cert-error override).
  static bool ShouldPersistKey(const nsACString& aKey,
                               uint8_t aOverridableError);

  // Reconcile persistence infrastructure with the current pref value.
  // The signature matches PrefChangedFunc so it can be passed directly to
  // Preferences::RegisterCallback; the two parameters are ignored.
  static void ReconcilePersistence(const char* = nullptr, void* = nullptr);

  size_t SizeOfIncludingThis(mozilla::MallocSizeOf mallocSizeOf) const
      MOZ_REQUIRES(sLock);

  static mozilla::StaticRefPtr<SSLTokensCache> gInstance MOZ_GUARDED_BY(sLock);
  static StaticMutex sLock;
  static uint64_t sRecordId MOZ_GUARDED_BY(sLock);

  uint32_t mCacheSize MOZ_GUARDED_BY(sLock){0};

  // Persistence state (parent process only)
  bool mPrefCallbackRegistered{false};  // main-thread-only
  bool mWriteObserversRegistered MOZ_GUARDED_BY(sLock){false};
  // Created lazily and kept alive across a pref-disable, so every DB
  // operation serializes on the same target. Needs no explicit shutdown.
  nsCOMPtr<nsISerialEventTarget> mDBQueue MOZ_GUARDED_BY(sLock);
  // Resolved once on the main thread, since NS_GetSpecialDirectory asserts
  // NS_IsMainThread(); read-only afterwards via GetCachedDBFile().
  nsCOMPtr<nsIFile> mDBFile MOZ_GUARDED_BY(sLock);
  // False once persistence is off, even though mDBQueue stays alive.
  bool mDBActive MOZ_GUARDED_BY(sLock){false};
  bool mLoadComplete MOZ_GUARDED_BY(sLock){false};
  TimeStamp mLoadStartTime MOZ_GUARDED_BY(sLock);
  // Bumped by Clear() to invalidate in-flight background loads.
  uint32_t mLoadGeneration MOZ_GUARDED_BY(sLock){0};
  void DoWrite(bool aSynchronous) MOZ_EXCLUDES(sLock);
  void RegisterShutdownBlocker() MOZ_EXCLUDES(sLock);
  void RemoveShutdownBlocker() MOZ_EXCLUDES(sLock);
  // Unregisters aInstance from the memory reporter, observers and pref
  // callbacks. Called exactly once, from whichever teardown path runs.
  static void UnregisterFromServices(SSLTokensCache* aInstance)
      MOZ_EXCLUDES(sLock);
  nsCOMPtr<nsIAsyncShutdownClient> mShutdownBarrier MOZ_GUARDED_BY(sLock);

  // Creates mDBQueue if needed and marks persistence active. The caller must
  // check storage service availability before taking sLock, since
  // do_GetService must not run under it.
  static bool SetupPersistenceLocked(uint32_t& aLoadGen,
                                     bool aStorageServiceAvailable)
      MOZ_REQUIRES(sLock);
  // Dispatches open+migrate+load to mDBQueue.
  static void DispatchOpenAndLoad(uint32_t aLoadGen);
  static void OnLoadCompleteNotify(uint32_t aCount);
  // Builds a TokenCacheRecord from its raw persisted fields.
  static UniquePtr<TokenCacheRecord> MakeRecord(
      const nsACString& aKey, PRTime aExpirationTime, uint8_t aOverridableError,
      bool aRestored, nsTArray<uint8_t>&& aCompressedPayload);

  // Skips insertion, returning false, if Clear() bumped mLoadGeneration past
  // aExpectedGen since the load started.
  static bool PutFromPersisted(const SSLTokensCacheRecordInfo& aRec,
                               uint32_t aExpectedGen, bool aRestored);

  // False once persistence is off, so a task queued earlier can't recreate
  // the DB file.
  static bool IsPersistenceStillActive();

  // Null if persistence was never activated. Safe on the DB task queue.
  static already_AddRefed<nsIFile> GetCachedDBFile();

  // The following run on the DB task queue only, each opening and closing
  // its own connection.
  static bool OpenDB(nsCOMPtr<mozIStorageConnection>& aConn);
  // IsPersistenceStillActive + OpenDB; caller must Close() the result.
  static already_AddRefed<mozIStorageConnection> OpenActiveDB();
  static uint32_t LoadFromDB(uint32_t aLoadGen);
  static void WriteSnapshotToDB(nsTArray<SSLTokensCacheRecordInfo>&& aSnapshot);
  // Writes a snapshot, but only once the startup load has completed.
  static void CollectAndWriteSnapshotIfLoaded();
  static void ClearDB();
  static void RemoveDBFileSync();
  // Best-effort removal so a fresh DB is created on next open. Unlike
  // Places, an ephemeral cache gains nothing from keeping a corrupt backup.
  static bool RemoveDBFile(nsIFile* aDbFile);
#ifdef DEBUG
  // Stronger than !NS_IsMainThread(): asserts we're specifically on mDBQueue.
  static void AssertOnDBThread();
#endif

  // Schema helpers, shared with the test-only hooks below. EnsureSchema
  // creates or recreates the ssl_tokens table as needed.
  static nsresult EnsureSchema(mozIStorageConnection* aConn);
  static nsresult CreateSchemaOn(mozIStorageConnection* aConn);
  static nsresult PrepareInsertStatement(
      mozIStorageConnection* aConn,
      nsCOMPtr<mozIStorageStatement>& aStmtInsert);
  static nsresult WriteSnapshotTo(
      mozIStorageConnection* aConn, mozIStorageStatement* aStmtInsert,
      const nsTArray<SSLTokensCacheRecordInfo>& aSnapshot);
  // aCorrupted is set if a row fails to read, i.e. corruption found after a
  // successful open; OpenDB already handles corruption at open time.
  static uint32_t LoadValidRecordsFrom(mozIStorageConnection* aConn,
                                       uint32_t aLoadGen,
                                       bool* aCorrupted = nullptr);

  static nsDependentCSubstring BasePartFromKey(const nsACString& aKey);
  static nsDependentCSubstring HostFromBasePart(
      const nsDependentCSubstring& aBasePart);
  static OriginAttributes OAFromPeerId(const nsACString& aPeerId);
  static void RemoveByMatchAndOAPattern(
      const nsACString& aValue, const nsACString& aSeparatedValue,
      const mozilla::OriginAttributesPattern& aPattern) MOZ_EXCLUDES(sLock);

  // Snapshot owning its key and payload bytes, so it can cross threads and
  // processes. aFilterForPersistence applies ShouldPersistKey.
  void CollectRecordInfosLocked(nsTArray<SSLTokensCacheRecordInfo>& aOut,
                                bool aFilterForPersistence) const
      MOZ_REQUIRES(sLock);
  // Removes entries matching aPredicate.
  template <typename Pred>
  void RemoveMatchingLocked(Pred&& aPredicate) MOZ_REQUIRES(sLock);

  class TokenCacheRecord {
   public:
    ~TokenCacheRecord();

    uint32_t Size() const;

    nsCString mKey;
    PRTime mExpirationTime = 0;
    // Compressed (token || serialized SessionCacheInfo). Storing them together
    // lets the compressor find redundancies across the NSS token and the cert
    // chain fields (both carry the same cert DER).
    nsTArray<uint8_t> mCompressedPayload;
    // Cached separately to allow ShouldPersistKey() filtering without
    // decompressing the payload.
    uint8_t mOverridableError = 0;
    uint64_t mId = 0;
    // Not part of the on-storage format; in-memory only, for
    // about:networking.
    bool mRestored = false;
  };

  class TokenCacheEntry {
   public:
    uint32_t Size() const;
    // Add a record into |mRecords|. To make sure |mRecords| is sorted, we
    // iterate |mRecords| everytime to find a right place to insert the new
    // record.
    void AddRecord(UniquePtr<TokenCacheRecord>&& aRecord,
                   nsTArray<TokenCacheRecord*>& aExpirationArray);
    // This function returns the first record in |mRecords|.
    const UniquePtr<TokenCacheRecord>& Get();
    UniquePtr<TokenCacheRecord> RemoveWithId(uint64_t aId);
    uint32_t RecordCount() const { return mRecords.Length(); }
    const nsTArray<UniquePtr<TokenCacheRecord>>& Records() const {
      return mRecords;
    }

   private:
    // The records in this array are ordered by the expiration time.
    nsTArray<UniquePtr<TokenCacheRecord>> mRecords;
  };

  void OnRecordDestroyed(TokenCacheRecord* aRec) MOZ_REQUIRES(sLock);
  uint64_t InsertRecordLocked(UniquePtr<TokenCacheRecord> aRec)
      MOZ_REQUIRES(sLock);

  nsClassHashtable<nsCStringHashKey, TokenCacheEntry> mTokenCacheRecords
      MOZ_GUARDED_BY(sLock);
  nsTArray<TokenCacheRecord*> mExpirationArray MOZ_GUARDED_BY(sLock);
};

// Scriptable entry point (@mozilla.org/network/ssl-tokens-cache;1) for
// SSLTokensCache. Stateless: all state lives in the SSLTokensCache singleton
// reached via its static methods.
class SSLTokensCacheService final : public nsISSLTokensCache
#ifdef ENABLE_TESTS
    ,
                                    public nsISSLTokensCacheTest
#endif
{
 public:
  NS_DECL_THREADSAFE_ISUPPORTS
  NS_DECL_NSISSLTOKENSCACHE
#ifdef ENABLE_TESTS
  NS_DECL_NSISSLTOKENSCACHETEST
#endif

  SSLTokensCacheService() = default;

 private:
  ~SSLTokensCacheService() = default;
};

}  // namespace net
}  // namespace mozilla

#endif  // SSLTokensCache_h_
