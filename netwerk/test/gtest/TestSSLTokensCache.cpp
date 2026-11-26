#include "CertVerifier.h"
#include "CommonSocketControl.h"
#include "SSLTokensCache.h"
#include "ScopedNSSTypes.h"
#include "TransportSecurityInfo.h"
#include "gtest/gtest.h"
#include "mozIStorageConnection.h"
#include "mozIStorageService.h"
#include "mozIStorageStatement.h"
#include "mozStorageCID.h"
#include "mozilla/Preferences.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "mozilla/gtest/MozAssertions.h"
#include "nsDirectoryServiceUtils.h"
#include "nsIFile.h"
#include "nsITransportSecurityInfo.h"
#include "nsIWebProgressListener.h"
#include "nsIX509Cert.h"
#include "nsIX509CertDB.h"
#include "nsNSSCertificate.h"
#include "nsServiceManagerUtils.h"
#include "nsXPCOM.h"
#include "prtime.h"
#include "sslproto.h"

static already_AddRefed<CommonSocketControl> createDummySocketControl() {
  nsCOMPtr<nsIX509CertDB> certDB(do_GetService(NS_X509CERTDB_CONTRACTID));
  EXPECT_TRUE(certDB);
  nsLiteralCString base64(
      "MIIBbjCCARWgAwIBAgIUOyCxVVqw03yUxKSfSojsMF8K/"
      "ikwCgYIKoZIzj0EAwIwHTEbMBkGA1UEAwwScm9vdF9zZWNwMjU2azFfMjU2MCIYDzIwMjAxM"
      "TI3MDAwMDAwWhgPMjAyMzAyMDUwMDAwMDBaMC8xLTArBgNVBAMMJGludF9zZWNwMjU2cjFfM"
      "jU2LXJvb3Rfc2VjcDI1NmsxXzI1NjBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABE+/"
      "u7th4Pj5saYKWayHBOLsBQtCPjz3LpI/"
      "LE95S0VcKmnSM0VsNsQRnQcG4A7tyNGTkNeZG3stB6ME6qBKpsCjHTAbMAwGA1UdEwQFMAMB"
      "Af8wCwYDVR0PBAQDAgEGMAoGCCqGSM49BAMCA0cAMEQCIFuwodUwyOUnIR4KN5ZCSrU7y4iz"
      "4/1EWRdHm5kWKi8dAiB6Ixn9sw3uBVbyxnQKYqGnOwM+qLOkJK0W8XkIE3n5sg==");
  nsCOMPtr<nsIX509Cert> cert;
  EXPECT_TRUE(NS_SUCCEEDED(
      certDB->ConstructX509FromBase64(base64, getter_AddRefs(cert))));
  EXPECT_TRUE(cert);
  nsTArray<nsTArray<uint8_t>> succeededCertChain;
  for (size_t i = 0; i < 3; i++) {
    nsTArray<uint8_t> certDER;
    EXPECT_TRUE(NS_SUCCEEDED(cert->GetRawDER(certDER)));
    succeededCertChain.AppendElement(std::move(certDER));
  }
  nsTArray<nsTArray<uint8_t>> handshakeCerts;
  handshakeCerts.AppendElement(succeededCertChain[0].Clone());
  RefPtr<CommonSocketControl> socketControl(
      new CommonSocketControl(nsLiteralCString("example.com"), 433, 0));
  socketControl->SetServerCert(cert, mozilla::psm::EVStatus::NotEV);
  socketControl->SetSucceededCertChain(std::move(succeededCertChain));
  socketControl->SetHandshakeCertificates(std::move(handshakeCerts));
  return socketControl.forget();
}

static auto MakeTestData(const size_t aDataSize) {
  auto data = nsTArray<uint8_t>();
  data.SetLength(aDataSize);
  // LCG pseudo-random fill: near-incompressible, so the compressed record
  // size stays close to key.len() + 4 + tokenSize + cert overhead.
  uint32_t state = 0xDEADBEEFu;
  for (auto& b : data) {
    state = state * 1664525u + 1013904223u;
    b = static_cast<uint8_t>(state >> 24);
  }
  return data;
}

static PRTime OneHourFromNow() {
  return PR_Now() + PRTime(3600) * PR_USEC_PER_SEC;
}

static void putTokenWithExpiry(
    const nsACString& aKey, uint32_t aSize, PRTime aExpiry,
    bool aPlaintextSecret = true,
    nsITransportSecurityInfo::OverridableErrorCategory aError =
        nsITransportSecurityInfo::OverridableErrorCategory::ERROR_UNSET) {
  RefPtr<CommonSocketControl> socketControl = createDummySocketControl();
  if (aError !=
      nsITransportSecurityInfo::OverridableErrorCategory::ERROR_UNSET) {
    socketControl->SetStatusErrorBits(aError);
  }
  nsTArray<uint8_t> token = MakeTestData(aSize);
  nsresult rv = mozilla::net::SSLTokensCache::Put(
      aKey, token.Elements(), aSize, socketControl, aExpiry, aPlaintextSecret);
  ASSERT_EQ(rv, NS_OK);
}

static void putToken(const nsACString& aKey, uint32_t aSize) {
  putTokenWithExpiry(aKey, aSize, PR_Now() + PRTime(aSize) * PR_USEC_PER_SEC);
}

static void ClearAll() { mozilla::net::SSLTokensCache::Clear(); }

[[nodiscard]] static auto setCapacity(int32_t aKB) {
  constexpr auto kPref = "network.ssl_tokens_cache_capacity";
  const bool hadUserValue = mozilla::Preferences::HasUserValue(kPref);
  const int32_t oldValue = mozilla::Preferences::GetInt(kPref);
  EXPECT_NS_SUCCEEDED(mozilla::Preferences::SetInt(kPref, aKB));
  return mozilla::MakeScopeExit([=] {
    if (hadUserValue) {
      EXPECT_NS_SUCCEEDED(mozilla::Preferences::SetInt(kPref, oldValue));
    } else {
      EXPECT_NS_SUCCEEDED(mozilla::Preferences::ClearUser(kPref));
    }
  });
}

static void getAndCheckResult(const nsACString& aKey, uint32_t aExpectedSize) {
  nsTArray<uint8_t> result;
  mozilla::net::SessionCacheInfo unused;
  nsresult rv = mozilla::net::SSLTokensCache::Get(aKey, result, unused);
  ASSERT_EQ(rv, NS_OK);
  ASSERT_EQ(result.Length(), (size_t)aExpectedSize);
}

TEST(TestTokensCache, SinglePut)
{
  mozilla::net::SSLTokensCache::Clear();
  mozilla::Preferences::SetInt("network.ssl_tokens_cache_records_per_entry", 1);

  putToken("anon:www.example.com:443"_ns, 100);
  nsTArray<uint8_t> result;
  mozilla::net::SessionCacheInfo unused;
  nsresult rv = mozilla::net::SSLTokensCache::Get("anon:www.example.com:443"_ns,
                                                  result, unused);
  ASSERT_EQ(rv, NS_OK);
  rv = mozilla::net::SSLTokensCache::Get("anon:www.example.com:443"_ns, result,
                                         unused);
  ASSERT_EQ(rv, NS_ERROR_NOT_AVAILABLE);
}

TEST(TestTokensCache, MultiplePut)
{
  mozilla::net::SSLTokensCache::Clear();
  mozilla::Preferences::SetInt("network.ssl_tokens_cache_records_per_entry", 3);

  putToken("anon:www.example1.com:443"_ns, 300);
  // This record will be removed because
  // "network.ssl_tokens_cache_records_per_entry" is 3.
  putToken("anon:www.example1.com:443"_ns, 100);
  putToken("anon:www.example1.com:443"_ns, 200);
  putToken("anon:www.example1.com:443"_ns, 400);

  // Test if records are ordered by the expiration time
  getAndCheckResult("anon:www.example1.com:443"_ns, 200);
  getAndCheckResult("anon:www.example1.com:443"_ns, 300);
  getAndCheckResult("anon:www.example1.com:443"_ns, 400);
}

TEST(TestTokensCache, RemoveAll)
{
  mozilla::net::SSLTokensCache::Clear();
  mozilla::Preferences::SetInt("network.ssl_tokens_cache_records_per_entry", 3);

  putToken("anon:www.example1.com:443"_ns, 100);
  putToken("anon:www.example1.com:443"_ns, 200);
  putToken("anon:www.example1.com:443"_ns, 300);

  putToken("anon:www.example2.com:443"_ns, 100);
  putToken("anon:www.example2.com:443"_ns, 200);
  putToken("anon:www.example2.com:443"_ns, 300);

  nsTArray<uint8_t> result;
  mozilla::net::SessionCacheInfo unused;
  nsresult rv = mozilla::net::SSLTokensCache::Get(
      "anon:www.example1.com:443"_ns, result, unused);
  ASSERT_EQ(rv, NS_OK);
  ASSERT_EQ(result.Length(), (size_t)100);

  rv = mozilla::net::SSLTokensCache::RemoveAll("anon:www.example1.com:443"_ns);
  ASSERT_EQ(rv, NS_OK);

  rv = mozilla::net::SSLTokensCache::Get("anon:www.example1.com:443"_ns, result,
                                         unused);
  ASSERT_EQ(rv, NS_ERROR_NOT_AVAILABLE);

  rv = mozilla::net::SSLTokensCache::Get("anon:www.example2.com:443"_ns, result,
                                         unused);
  ASSERT_EQ(rv, NS_OK);
  ASSERT_EQ(result.Length(), (size_t)100);
}

TEST(TestTokensCache, Eviction)
{
  ClearAll();

  // Use a high per-entry limit so global-capacity eviction is the only
  // mechanism under test here (per-entry eviction is already covered by
  // MultiplePut).
  mozilla::Preferences::SetInt("network.ssl_tokens_cache_records_per_entry",
                               10);

  // Token sizes dominate cert overhead, making record sizes predictable.
  // Capacity of 5 KB holds new alone but not old+new+trigger together.
  putToken("anon:evict-old.com:443"_ns, 2000);
  putToken("anon:evict-new.com:443"_ns, 4000);

  auto restoreCapacity = setCapacity(5);

  // Trigger has the earliest expiry, so EvictIfNecessary removes it first,
  // then evict-old.com, until the cache drops below 5 KB.
  putToken("anon:evict-trigger.com:443"_ns, 10);

  nsTArray<uint8_t> result;
  mozilla::net::SessionCacheInfo unused;
  ASSERT_EQ(mozilla::net::SSLTokensCache::Get("anon:evict-old.com:443"_ns,
                                              result, unused),
            NS_ERROR_NOT_AVAILABLE)
      << "evict-old.com should have been evicted (second-oldest after trigger)";
  ASSERT_EQ(mozilla::net::SSLTokensCache::Get("anon:evict-new.com:443"_ns,
                                              result, unused),
            NS_OK)
      << "evict-new.com should survive: it fits within the 5 KB capacity";
}

// Verify that ssl_token_cache_evictions only counts evictions of still-valid
// tokens. Already-expired tokens removed under capacity pressure must not
// be counted (they are tracked by ssl_token_cache_expired instead).
TEST(TestTokensCache, EvictionCountsOnlyValidTokens)
{
  ClearAll();

  mozilla::Preferences::SetInt("network.ssl_tokens_cache_records_per_entry",
                               10);

  // 2 KB tokens dominate cert overhead, so capacity 3 KB holds one record
  // but not two — each insertion past the first triggers exactly one eviction.
  auto restoreCapacity = setCapacity(3);

  auto evictionCount = []() {
    return mozilla::glean::network::ssl_token_cache_evictions.TestGetValue()
        .unwrap()
        .valueOr(0);
  };

  PRTime now = PR_Now();
  int32_t before = evictionCount();

  // Step 1: expired token fits; no eviction yet.
  putTokenWithExpiry("anon:evict-expired.com:443"_ns, 2000,
                     now - PRTime(PR_USEC_PER_SEC));

  // Step 2: exceeds capacity → expired record evicted first (smallest expiry).
  // Must NOT be counted.
  putTokenWithExpiry("anon:evict-valid1.com:443"_ns, 2000,
                     now + PRTime(2000) * PR_USEC_PER_SEC);

  // Step 3: exceeds capacity again → valid_1 evicted (next smallest expiry).
  // MUST be counted.
  putTokenWithExpiry("anon:evict-valid2.com:443"_ns, 2000,
                     now + PRTime(4000) * PR_USEC_PER_SEC);

  // One expired eviction (not counted) + one valid eviction (counted) = 1.
  ASSERT_EQ(evictionCount() - before, 1);
}

static nsCString GetTempCachePath(const char* aName) {
  nsCOMPtr<nsIFile> tmpDir;
  NS_GetSpecialDirectory("TmpD", getter_AddRefs(tmpDir));
  tmpDir->AppendNative(nsDependentCString(aName));
  (void)tmpDir->Remove(false);
  nsAutoString widePath;
  tmpDir->GetPath(widePath);
  return NS_ConvertUTF16toUTF8(widePath);
}

static void CorruptFileAt(const nsCString& aPath, size_t aOffset,
                          uint8_t aVal) {
  FILE* f = fopen(aPath.get(), "r+b");
  if (!f) return;
  fseek(f, static_cast<long>(aOffset), SEEK_SET);
  fputc(aVal, f);
  fclose(f);
}

static already_AddRefed<mozIStorageConnection> OpenCacheDB(
    const nsCString& aPath) {
  nsCOMPtr<mozIStorageService> storage =
      do_GetService(MOZ_STORAGE_SERVICE_CONTRACTID);
  nsCOMPtr<nsIFile> file;
  nsCOMPtr<mozIStorageConnection> conn;
  if (storage &&
      NS_SUCCEEDED(NS_NewNativeLocalFile(aPath, getter_AddRefs(file)))) {
    (void)storage->OpenUnsharedDatabase(
        file, mozIStorageService::CONNECTION_DEFAULT, getter_AddRefs(conn));
  }
  return conn.forget();
}

// Row count of the cache file at aPath, or -1 if it can't be read.
static int64_t CountCacheRows(const nsCString& aPath) {
  nsCOMPtr<mozIStorageConnection> conn = OpenCacheDB(aPath);
  if (!conn) {
    return -1;
  }
  int64_t count = -1;
  nsCOMPtr<mozIStorageStatement> stmt;
  bool hasRow = false;
  if (NS_SUCCEEDED(conn->CreateStatement("SELECT COUNT(*) FROM ssl_tokens"_ns,
                                         getter_AddRefs(stmt))) &&
      NS_SUCCEEDED(stmt->ExecuteStep(&hasRow)) && hasRow) {
    count = stmt->AsInt64(0);
  }
  if (stmt) {
    stmt->Finalize();
  }
  conn->Close();
  return count;
}

TEST(TestTokensCache, ExpiredTokens)
{
  mozilla::net::SSLTokensCache::Clear();
  mozilla::Preferences::SetInt("network.ssl_tokens_cache_records_per_entry", 3);

  PRTime now = PR_Now();
  RefPtr<CommonSocketControl> socketControl = createDummySocketControl();

  nsTArray<uint8_t> expiredToken1 = MakeTestData(100);
  nsTArray<uint8_t> expiredToken2 = MakeTestData(200);
  nsTArray<uint8_t> validToken = MakeTestData(300);

  nsresult rv = mozilla::net::SSLTokensCache::Put(
      "anon:www.example.com:443"_ns, expiredToken1.Elements(), 100,
      socketControl, now - (PRTime(100) * PRTime(PR_USEC_PER_SEC)));
  ASSERT_EQ(rv, NS_OK);

  rv = mozilla::net::SSLTokensCache::Put(
      "anon:www.example.com:443"_ns, expiredToken2.Elements(), 200,
      socketControl, now - (PRTime(50) * PRTime(PR_USEC_PER_SEC)));
  ASSERT_EQ(rv, NS_OK);

  rv = mozilla::net::SSLTokensCache::Put(
      "anon:www.example.com:443"_ns, validToken.Elements(), 300, socketControl,
      now + (PRTime(3600) * PRTime(PR_USEC_PER_SEC)));
  ASSERT_EQ(rv, NS_OK);

  nsTArray<uint8_t> result;
  mozilla::net::SessionCacheInfo unused;
  rv = mozilla::net::SSLTokensCache::Get("anon:www.example.com:443"_ns, result,
                                         unused);
  ASSERT_EQ(rv, NS_OK);
  ASSERT_EQ(result.Length(), (size_t)300);

  rv = mozilla::net::SSLTokensCache::Get("anon:www.example.com:443"_ns, result,
                                         unused);
  ASSERT_EQ(rv, NS_ERROR_NOT_AVAILABLE);
}

// Verifies that QUIC resumption tokens (used as address validation tokens) are
// partitioned by first-party context. A token stored under one partition key
// must not be retrievable using a different partition key, preventing
// cross-origin tracking across first-party sites.
TEST(TestTokensCache, QuicTokenPartitioning)
{
  mozilla::net::SSLTokensCache::Clear();
  mozilla::Preferences::SetInt("network.ssl_tokens_cache_records_per_entry", 3);

  // Simulate two first-party contexts embedding the same third-party QUIC
  // server. The peerId format includes the OriginAttributes suffix which
  // contains the partitionKey.
  const nsLiteralCString kServerPartitionedUnderA(
      "quic.example.com:443^partitionKey=%28https%2Ca.example.com%29");
  const nsLiteralCString kServerPartitionedUnderB(
      "quic.example.com:443^partitionKey=%28https%2Cb.example.com%29");

  // Store a token in the context of first-party A.
  putToken(kServerPartitionedUnderA, 100);

  // The token must be retrievable using the same partition key.
  nsTArray<uint8_t> result;
  mozilla::net::SessionCacheInfo unused;
  nsresult rv = mozilla::net::SSLTokensCache::Get(kServerPartitionedUnderA,
                                                  result, unused);
  ASSERT_EQ(rv, NS_OK);
  ASSERT_EQ(result.Length(), (size_t)100);

  // Re-insert the token so it can be tested from the B context below.
  putToken(kServerPartitionedUnderA, 100);

  // The token must NOT be accessible under a different first-party (B).
  rv = mozilla::net::SSLTokensCache::Get(kServerPartitionedUnderB, result,
                                         unused);
  ASSERT_EQ(rv, NS_ERROR_NOT_AVAILABLE);

  // A separate token stored under first-party B must also be isolated.
  putToken(kServerPartitionedUnderB, 200);

  rv = mozilla::net::SSLTokensCache::Get(kServerPartitionedUnderB, result,
                                         unused);
  ASSERT_EQ(rv, NS_OK);
  ASSERT_EQ(result.Length(), (size_t)200);

  // The B token must not bleed into the A partition.
  rv = mozilla::net::SSLTokensCache::Get(kServerPartitionedUnderA, result,
                                         unused);
  ASSERT_EQ(rv, NS_OK);
  ASSERT_EQ(result.Length(), (size_t)100);

  rv = mozilla::net::SSLTokensCache::Get(kServerPartitionedUnderA, result,
                                         unused);
  ASSERT_EQ(rv, NS_ERROR_NOT_AVAILABLE);
}

TEST(TestTokensCache, PersistenceRoundTrip)
{
  ClearAll();
  mozilla::Preferences::SetInt("network.ssl_tokens_cache_records_per_entry", 3);
  nsCString path = GetTempCachePath("test_tls_tc_rt.sqlite");

  putToken("anon:a.example.com:443"_ns, 100);
  putToken("anon:a.example.com:443"_ns, 200);
  putToken("anon:b.example.com:443"_ns, 150);

  mozilla::net::SSLTokensCache::TriggerWriteForTest(path);

  ClearAll();
  mozilla::net::SSLTokensCache::LoadForTest(path);

  getAndCheckResult("anon:a.example.com:443"_ns, 100);
  getAndCheckResult("anon:a.example.com:443"_ns, 200);
  getAndCheckResult("anon:b.example.com:443"_ns, 150);
}

TEST(TestTokensCache, PersistenceExpiryFiltering)
{
  ClearAll();
  nsCString path = GetTempCachePath("test_tls_tc_expiry.sqlite");

  putTokenWithExpiry("anon:example.com:443"_ns, 100,
                     PR_Now() - PRTime(1) * PR_USEC_PER_SEC);
  putTokenWithExpiry("anon:example.com:443"_ns, 200, OneHourFromNow());

  mozilla::net::SSLTokensCache::TriggerWriteForTest(path);
  ASSERT_EQ(CountCacheRows(path), 1);
  ClearAll();
  mozilla::net::SSLTokensCache::LoadForTest(path);

  nsTArray<uint8_t> result;
  mozilla::net::SessionCacheInfo unused;
  ASSERT_EQ(mozilla::net::SSLTokensCache::Get("anon:example.com:443"_ns, result,
                                              unused),
            NS_OK);
  ASSERT_EQ(result.Length(), (size_t)200);
  ASSERT_EQ(mozilla::net::SSLTokensCache::Get("anon:example.com:443"_ns, result,
                                              unused),
            NS_ERROR_NOT_AVAILABLE);
}

TEST(TestTokensCache, PersistenceSkipsWrappedTokens)
{
  ClearAll();
  nsCString path = GetTempCachePath("test_tls_tc_wrapped.sqlite");
  PRTime expiry = OneHourFromNow();

  putTokenWithExpiry("anon:wrapped.example.com:443"_ns, 100, expiry,
                     /* aPlaintextSecret */ false);
  putTokenWithExpiry("anon:plain.example.com:443"_ns, 200, expiry);
  mozilla::net::SSLTokensCache::TriggerWriteForTest(path);
  ASSERT_EQ(CountCacheRows(path), 1);

  ClearAll();
  mozilla::net::SSLTokensCache::LoadForTest(path);
  nsTArray<uint8_t> result;
  mozilla::net::SessionCacheInfo unused;
  ASSERT_EQ(mozilla::net::SSLTokensCache::Get("anon:wrapped.example.com:443"_ns,
                                              result, unused),
            NS_ERROR_NOT_AVAILABLE);
  getAndCheckResult("anon:plain.example.com:443"_ns, 200);
}

TEST(TestTokensCache, PersistenceSkipsPrivateAndOverriddenTokens)
{
  ClearAll();
  nsCString path = GetTempCachePath("test_tls_tc_skipped.sqlite");
  PRTime expiry = OneHourFromNow();

  putTokenWithExpiry("anon:pbm.example.com:443^privateBrowsingId=1"_ns, 100,
                     expiry);
  putTokenWithExpiry(
      "anon:override.example.com:443"_ns, 100, expiry,
      /* aPlaintextSecret */ true,
      nsITransportSecurityInfo::OverridableErrorCategory::ERROR_DOMAIN);
  mozilla::net::SSLTokensCache::TriggerWriteForTest(path);

  ASSERT_EQ(CountCacheRows(path), 0);
}

TEST(TestTokensCache, PersistenceDropsOldSchema)
{
  ClearAll();
  nsCString path = GetTempCachePath("test_tls_tc_v1.sqlite");
  {
    nsCOMPtr<mozIStorageConnection> conn = OpenCacheDB(path);
    ASSERT_TRUE(conn);
    ASSERT_NS_SUCCEEDED(conn->SetSchemaVersion(1));
    ASSERT_NS_SUCCEEDED(conn->ExecuteSimpleSQL(
        "CREATE TABLE ssl_tokens (key TEXT NOT NULL, expiration_time INTEGER "
        "NOT NULL, overridable_error INTEGER NOT NULL, payload BLOB NOT NULL);"
        "INSERT INTO ssl_tokens VALUES ('anon:old.example.com:443', "
        "9223372036854775807, 0, x'00')"_ns));
    conn->Close();
  }

  mozilla::net::SSLTokensCache::TriggerWriteForTest(path);

  ASSERT_EQ(CountCacheRows(path), 0);
  nsCOMPtr<mozIStorageConnection> conn = OpenCacheDB(path);
  ASSERT_TRUE(conn);
  int32_t version = 0;
  ASSERT_NS_SUCCEEDED(conn->GetSchemaVersion(&version));
  ASSERT_EQ(version, 3);
  bool indexExists = false;
  ASSERT_NS_SUCCEEDED(conn->IndexExists("ssl_tokens_key"_ns, &indexExists));
  ASSERT_TRUE(indexExists);
  conn->Close();
}

// A corrupt header makes OpenUnsharedDatabase fail with
// NS_ERROR_FILE_CORRUPTED, which must be handled gracefully.
TEST(TestTokensCache, PersistenceCorruption)
{
  ClearAll();
  nsCString path = GetTempCachePath("test_tls_tc_corrupt.sqlite");

  putToken("anon:example.com:443"_ns, 100);
  mozilla::net::SSLTokensCache::TriggerWriteForTest(path);
  // SQLite validates this magic on open, so clobbering it is reliable.
  CorruptFileAt(path, 0, 'X');

  ClearAll();
  mozilla::net::SSLTokensCache::LoadForTest(path);  // must not crash

  nsTArray<uint8_t> result;
  mozilla::net::SessionCacheInfo unused;
  ASSERT_EQ(mozilla::net::SSLTokensCache::Get("anon:example.com:443"_ns, result,
                                              unused),
            NS_ERROR_NOT_AVAILABLE);
}

// A too-small-to-be-a-database file is a distinct failure mode.
TEST(TestTokensCache, PersistenceTruncated)
{
  ClearAll();
  nsCString path = GetTempCachePath("test_tls_tc_trunc.sqlite");

  putToken("anon:example.com:443"_ns, 100);
  mozilla::net::SSLTokensCache::TriggerWriteForTest(path);

  FILE* f = fopen(path.get(), "wb");
  if (f) {
    fwrite("SQLite", 1, 6, f);
    fclose(f);
  }

  ClearAll();
  mozilla::net::SSLTokensCache::LoadForTest(path);  // must not crash

  nsTArray<uint8_t> result;
  mozilla::net::SessionCacheInfo unused;
  ASSERT_EQ(mozilla::net::SSLTokensCache::Get("anon:example.com:443"_ns, result,
                                              unused),
            NS_ERROR_NOT_AVAILABLE);
}

// Corruption found while reading rows, rather than at open time:
// LoadValidRecordsFrom must not report a partial load as success. Which
// failure mode a deep byte-flip triggers varies, so accept either.
TEST(TestTokensCache, PersistenceMidReadCorruption)
{
  ClearAll();
  nsCString path = GetTempCachePath("test_tls_tc_midcorrupt.sqlite");

  putToken("anon:example.com:443"_ns, 500);
  mozilla::net::SSLTokensCache::TriggerWriteForTest(path);

  nsCOMPtr<nsIFile> file;
  ASSERT_NS_SUCCEEDED(NS_NewNativeLocalFile(path, getter_AddRefs(file)));
  int64_t fileSize = 0;
  ASSERT_NS_SUCCEEDED(file->GetFileSize(&fileSize));
  ASSERT_GT(fileSize, (int64_t)200)
      << "test fixture too small to have data past the header";
  CorruptFileAt(path, static_cast<size_t>(fileSize) - 20, 0xFF);

  ClearAll();
  mozilla::net::SSLTokensCache::LoadForTest(path);  // must not crash

  nsTArray<uint8_t> result;
  mozilla::net::SessionCacheInfo unused;
  nsresult rv = mozilla::net::SSLTokensCache::Get("anon:example.com:443"_ns,
                                                  result, unused);
  if (rv == NS_OK) {
    ASSERT_EQ(result.Length(), (size_t)500);
  } else {
    ASSERT_TRUE(rv == NS_ERROR_NOT_AVAILABLE || rv == NS_ERROR_FAILURE)
    << "unexpected error code: " << static_cast<uint32_t>(rv);
  }
}

TEST(TestTokensCache, PersistenceEmpty)
{
  ClearAll();
  nsCString path = GetTempCachePath("test_tls_tc_empty.sqlite");

  mozilla::net::SSLTokensCache::TriggerWriteForTest(path);
  mozilla::net::SSLTokensCache::LoadForTest(path);

  nsTArray<uint8_t> result;
  mozilla::net::SessionCacheInfo unused;
  ASSERT_EQ(mozilla::net::SSLTokensCache::Get("anon:example.com:443"_ns, result,
                                              unused),
            NS_ERROR_NOT_AVAILABLE);
}

TEST(TestTokensCache, PersistenceConsumedRecordsAbsent)
{
  ClearAll();
  mozilla::Preferences::SetInt("network.ssl_tokens_cache_records_per_entry", 3);
  nsCString path = GetTempCachePath("test_tls_tc_consumed.sqlite");

  putToken("anon:example.com:443"_ns, 100);
  putToken("anon:example.com:443"_ns, 200);

  // Consume the first token (Get is destructive)
  nsTArray<uint8_t> result;
  mozilla::net::SessionCacheInfo unused;
  ASSERT_EQ(mozilla::net::SSLTokensCache::Get("anon:example.com:443"_ns, result,
                                              unused),
            NS_OK);

  mozilla::net::SSLTokensCache::TriggerWriteForTest(path);

  ClearAll();
  mozilla::net::SSLTokensCache::LoadForTest(path);

  // Only the unconsumed record (size 200) should be present
  ASSERT_EQ(mozilla::net::SSLTokensCache::Get("anon:example.com:443"_ns, result,
                                              unused),
            NS_OK);
  ASSERT_EQ(result.Length(), (size_t)200);
  ASSERT_EQ(mozilla::net::SSLTokensCache::Get("anon:example.com:443"_ns, result,
                                              unused),
            NS_ERROR_NOT_AVAILABLE);
}

TEST(TestTokensCache, PersistenceClear)
{
  ClearAll();
  nsCString path = GetTempCachePath("test_tls_tc_clear.sqlite");

  putToken("anon:example.com:443"_ns, 100);
  mozilla::net::SSLTokensCache::TriggerWriteForTest(path);

  nsCOMPtr<nsIFile> file;
  ASSERT_NS_SUCCEEDED(NS_NewNativeLocalFile(path, getter_AddRefs(file)));
  bool exists = false;
  file->Exists(&exists);
  ASSERT_TRUE(exists);

  // Clear the in-memory cache, then reload from the DB file.
  ClearAll();
  mozilla::net::SSLTokensCache::LoadForTest(path);

  nsTArray<uint8_t> result;
  mozilla::net::SessionCacheInfo unused;
  ASSERT_EQ(mozilla::net::SSLTokensCache::Get("anon:example.com:443"_ns, result,
                                              unused),
            NS_OK);
  ASSERT_EQ(result.Length(), (size_t)100);
}

TEST(TestTokensCache, PersistenceServerCertRoundTrip)
{
  // Server cert bytes and succeeded cert
  // chain must survive a persist/reload cycle so that PSK-resumed TLS 1.3
  // connections — which receive no Certificate message — can reconstruct
  // full security info via RebuildCertificateInfoFromSSLTokenCache().
  ClearAll();
  nsCString path = GetTempCachePath("test_tls_tc_cert.sqlite");

  putToken("anon:example.com:443"_ns, 100);
  mozilla::net::SSLTokensCache::TriggerWriteForTest(path);

  ClearAll();
  mozilla::net::SSLTokensCache::LoadForTest(path);

  nsTArray<uint8_t> result;
  mozilla::net::SessionCacheInfo info;
  ASSERT_EQ(mozilla::net::SSLTokensCache::Get("anon:example.com:443"_ns, result,
                                              info),
            NS_OK);
  ASSERT_FALSE(info.mServerCertBytes.IsEmpty())
  << "Server cert bytes must survive persistence (bug 2033907)";
  ASSERT_TRUE(info.mSucceededCertChainBytes.isSome())
  << "Succeeded cert chain must survive persistence (needed for "
     "connection coalescing)";
  ASSERT_FALSE(info.mSucceededCertChainBytes->IsEmpty())
  << "Succeeded cert chain must not be empty after persistence";
}

// Simulates a PSK-resumed TLS 1.3 connection using a token from the cache:
// retrieves the token, creates a fresh CommonSocketControl, calls
// SetSessionCacheInfo() + RebuildCertificateInfoFromSSLTokenCache() —
// replicating the SetResumptionTokenFromExternalCache + HandshakeCallback path.
static RefPtr<CommonSocketControl> SimulateResumedConnection(
    const nsACString& aKey) {
  nsTArray<uint8_t> token;
  mozilla::net::SessionCacheInfo info;
  if (mozilla::net::SSLTokensCache::Get(aKey, token, info) != NS_OK) {
    return nullptr;
  }
  RefPtr<CommonSocketControl> sc(
      new CommonSocketControl(nsLiteralCString("example.com"), 443, 0));
  sc->SetSessionCacheInfo(std::move(info));
  sc->RebuildCertificateInfoFromSSLTokenCache();
  return sc;
}

TEST(TestTokensCache, ResumedConnectionHasValidCertAfterReload)
{
  // Verifies the bug 2033907 hypothesis: when a token loaded from disk is
  // used for a PSK-resumed TLS 1.3 connection (no Certificate message from
  // the server), RebuildCertificateInfoFromSSLTokenCache() must produce a
  // server cert with valid DER bytes on the socket.
  ClearAll();
  nsCString path = GetTempCachePath("test_tls_tc_resumed_cert.sqlite");
  putToken("anon:example.com:443"_ns, 100);
  mozilla::net::SSLTokensCache::TriggerWriteForTest(path);
  ClearAll();
  mozilla::net::SSLTokensCache::LoadForTest(path);

  RefPtr<CommonSocketControl> sc =
      SimulateResumedConnection("anon:example.com:443"_ns);
  ASSERT_TRUE(sc);

  // The reconstructed cert must have non-empty DER bytes — HasServerCert()
  // alone is not sufficient since nsNSSCertificate(empty_DER) is non-null.
  nsCOMPtr<nsIX509Cert> serverCert = sc->GetServerCert();
  ASSERT_TRUE(serverCert)
  << "Resumed connection must have a server cert object after "
     "RebuildCertificateInfoFromSSLTokenCache()";
  nsTArray<uint8_t> certDER;
  ASSERT_NS_SUCCEEDED(serverCert->GetRawDER(certDER));
  ASSERT_FALSE(certDER.IsEmpty())
  << "Cert DER must be non-empty after reconstruction from persisted "
     "token (bug 2033907)";
}

TEST(TestTokensCache, ResumedConnectionEnablesCoalescing)
{
  // Verifies that HTTP/2 connection coalescing is enabled after a PSK
  // resumption using a token loaded from disk. IsAcceptableForHost() returns
  // false when mSucceededCertChain is empty.
  ClearAll();
  nsCString path = GetTempCachePath("test_tls_tc_coalesce.sqlite");
  putToken("anon:example.com:443"_ns, 100);
  mozilla::net::SSLTokensCache::TriggerWriteForTest(path);
  ClearAll();
  mozilla::net::SSLTokensCache::LoadForTest(path);

  RefPtr<CommonSocketControl> sc =
      SimulateResumedConnection("anon:example.com:443"_ns);
  ASSERT_TRUE(sc);

  // The succeeded cert chain must be populated so IsAcceptableForHost() passes.
  nsTArray<RefPtr<nsIX509Cert>> chain;
  nsCOMPtr<nsITransportSecurityInfo> secInfo;
  ASSERT_NS_SUCCEEDED(sc->GetSecurityInfo(getter_AddRefs(secInfo)));
  ASSERT_NS_SUCCEEDED(secInfo->GetSucceededCertChain(chain));
  ASSERT_FALSE(chain.IsEmpty())
  << "Succeeded cert chain must be non-empty after reload so that "
     "HTTP/2 connection coalescing is not disabled";
}

TEST(TestTokensCache, PersistenceWriteAfterLoad)
{
  // Verify that tokens loaded from disk survive a subsequent flush —
  // i.e. loaded records are re-registered, so a later write includes both.
  ClearAll();
  mozilla::Preferences::SetInt("network.ssl_tokens_cache_records_per_entry", 3);
  nsCString path = GetTempCachePath("test_tls_tc_wal.sqlite");

  putToken("anon:a.example.com:443"_ns, 100);
  putToken("anon:b.example.com:443"_ns, 200);

  // Initial write to disk.
  mozilla::net::SSLTokensCache::TriggerWriteForTest(path);

  // Simulate restart: clear all state, reload from disk.
  ClearAll();
  mozilla::net::SSLTokensCache::LoadForTest(path);

  // Add one more token in the new session and flush again.
  putToken("anon:c.example.com:443"_ns, 300);
  mozilla::net::SSLTokensCache::TriggerWriteForTest(path);

  // Simulate a second restart: clear and reload.
  ClearAll();
  mozilla::net::SSLTokensCache::LoadForTest(path);

  // All three tokens must survive — the two from the first session and the one
  // added in the second session.
  nsTArray<uint8_t> result;
  mozilla::net::SessionCacheInfo unused;
  ASSERT_EQ(mozilla::net::SSLTokensCache::Get("anon:a.example.com:443"_ns,
                                              result, unused),
            NS_OK);
  ASSERT_EQ(result.Length(), (size_t)100);
  ASSERT_EQ(mozilla::net::SSLTokensCache::Get("anon:b.example.com:443"_ns,
                                              result, unused),
            NS_OK);
  ASSERT_EQ(result.Length(), (size_t)200);
  ASSERT_EQ(mozilla::net::SSLTokensCache::Get("anon:c.example.com:443"_ns,
                                              result, unused),
            NS_OK);
  ASSERT_EQ(result.Length(), (size_t)300);
}

TEST(TestTokensCache, CertBytesRoundTrip)
{
  // Cert bytes must round-trip through Put/Get with byte-perfect fidelity.
  mozilla::net::SSLTokensCache::Clear();

  putToken("anon:roundtrip.example.com:443"_ns, 100);

  nsTArray<uint8_t> token;
  mozilla::net::SessionCacheInfo info;
  ASSERT_EQ(mozilla::net::SSLTokensCache::Get(
                "anon:roundtrip.example.com:443"_ns, token, info),
            NS_OK);

  ASSERT_FALSE(info.mServerCertBytes.IsEmpty())
  << "Server cert bytes must survive Put/Get round-trip";
  ASSERT_TRUE(info.mSucceededCertChainBytes.isSome())
  << "Succeeded cert chain must survive Put/Get round-trip";
  ASSERT_EQ(info.mSucceededCertChainBytes->Length(), (size_t)3)
      << "Succeeded cert chain length must be preserved";

  // createDummySocketControl() repeats the same cert 3 times in the succeeded
  // chain; each must equal mServerCertBytes after decompression.
  for (const auto& chainCert : *info.mSucceededCertChainBytes) {
    ASSERT_EQ(chainCert, info.mServerCertBytes)
        << "Each cert in the succeeded chain must equal mServerCertBytes";
  }
}

TEST(TestTokensCache, WithinRecordCertDedup)
{
  // createDummySocketControl() stores the same cert DER as the server cert,
  // three times in the succeeded chain and once in the handshake certs.  The
  // compressor sees all five copies in one payload and encodes them once, so
  // the stored record must be well under 2x the raw single-cert size.
  mozilla::net::SSLTokensCache::Clear();

  putToken("anon:dedup.example.com:443"_ns, 10);

  // Get the token to measure the raw DER size of the fixture cert.
  nsTArray<uint8_t> token;
  mozilla::net::SessionCacheInfo info;
  ASSERT_EQ(mozilla::net::SSLTokensCache::Get("anon:dedup.example.com:443"_ns,
                                              token, info),
            NS_OK);
  uint32_t rawCertSize = info.mServerCertBytes.Length();
  ASSERT_GT(rawCertSize, (uint32_t)0);

  // Re-insert so there is a live record to measure.
  putToken("anon:dedup.example.com:443"_ns, 10);
  uint32_t cacheSize = mozilla::net::SSLTokensCache::CacheSizeForTest();

  // Four identical cert blobs in one compressed payload must store to well
  // under 2x the raw single-cert size.
  ASSERT_LT(cacheSize, rawCertSize * 2)
      << "4x identical cert blobs must compress to ~1x; "
         "rawCertSize="
      << rawCertSize << " cacheSize=" << cacheSize;
}

static mozilla::net::SSLTokensCacheSnapshot getCacheSnapshot() {
  mozilla::net::SSLTokensCacheSnapshot snapshot;
  mozilla::net::SSLTokensCache::GetAllRecords(snapshot);
  return snapshot;
}

static void corruptPayload(mozilla::net::SSLTokensCacheRecordInfo& aRec) {
  aRec.compressedPayload = {0xFF, 0xFF, 0xFF, 0xFF, 0x00};
}

TEST(TestTokensCache, GetSkipsUndecodableRecord)
{
  ClearAll();
  mozilla::Preferences::SetInt("network.ssl_tokens_cache_records_per_entry", 3);
  putToken("anon:a.example.com:443"_ns, 100);
  putToken("anon:a.example.com:443"_ns, 200);

  mozilla::net::SSLTokensCacheSnapshot snapshot = getCacheSnapshot();
  auto& records = snapshot.records;
  ASSERT_EQ(records.Length(), 2u);
  auto& first = records[0].expirationTime < records[1].expirationTime
                    ? records[0]
                    : records[1];
  corruptPayload(first);
  mozilla::net::SSLTokensCache::ReplaceAllRecordsForTest(std::move(snapshot));

  getAndCheckResult("anon:a.example.com:443"_ns, 200);
  ASSERT_EQ(mozilla::net::SSLTokensCache::CountForTest(), 0u);
}

TEST(TestTokensCache, GetWithOnlyUndecodableRecordsIsMiss)
{
  ClearAll();
  mozilla::Preferences::SetInt("network.ssl_tokens_cache_records_per_entry", 3);
  putToken("anon:a.example.com:443"_ns, 100);
  putToken("anon:a.example.com:443"_ns, 200);

  mozilla::net::SSLTokensCacheSnapshot snapshot = getCacheSnapshot();
  for (auto& rec : snapshot.records) {
    corruptPayload(rec);
  }
  mozilla::net::SSLTokensCache::ReplaceAllRecordsForTest(std::move(snapshot));

  nsTArray<uint8_t> token;
  mozilla::net::SessionCacheInfo info;
  ASSERT_EQ(mozilla::net::SSLTokensCache::Get("anon:a.example.com:443"_ns,
                                              token, info),
            NS_ERROR_NOT_AVAILABLE);
  ASSERT_EQ(mozilla::net::SSLTokensCache::CountForTest(), 0u);
  ASSERT_EQ(mozilla::net::SSLTokensCache::CacheSizeForTest(), 0u);
}

static uint32_t serverCertSize() {
  RefPtr<CommonSocketControl> sc = createDummySocketControl();
  nsCOMPtr<nsIX509Cert> cert = sc->GetServerCert();
  nsTArray<uint8_t> der;
  EXPECT_NS_SUCCEEDED(cert->GetRawDER(der));
  return der.Length();
}

TEST(TestTokensCache, CertStoreSharedAcrossRecords)
{
  // Records with the same cert, for the same or different hosts, share one
  // copy of it; only the first record pays for the cert bytes.
  ClearAll();
  mozilla::Preferences::SetInt("network.ssl_tokens_cache_records_per_entry", 3);
  uint32_t certSize = serverCertSize();

  putToken("anon:a.example.com:443"_ns, 10);
  uint32_t one = mozilla::net::SSLTokensCache::CacheSizeForTest();
  ASSERT_GT(one, certSize);

  putToken("anon:a.example.com:443"_ns, 10);
  uint32_t two = mozilla::net::SSLTokensCache::CacheSizeForTest();
  putToken("anon:b.example.com:443"_ns, 10);
  uint32_t three = mozilla::net::SSLTokensCache::CacheSizeForTest();

  ASSERT_LT(two - one, certSize) << "same host must not store the cert again";
  ASSERT_LT(three - two, certSize)
      << "other host must not store the cert again";
}

TEST(TestTokensCache, CertStoreReleasedOnRemoveAndEviction)
{
  ClearAll();
  mozilla::Preferences::SetInt("network.ssl_tokens_cache_records_per_entry", 1);

  putToken("anon:a.example.com:443"_ns, 10);
  // Replaces the first record through the records-per-entry limit.
  putToken("anon:a.example.com:443"_ns, 20);
  ASSERT_EQ(
      mozilla::net::SSLTokensCache::RemoveAll("anon:a.example.com:443"_ns),
      NS_OK);
  ASSERT_EQ(mozilla::net::SSLTokensCache::CacheSizeForTest(), 0u);

  putToken("anon:b.example.com:443"_ns, 10);
  {
    auto restoreCapacity = setCapacity(0);
    putToken("anon:c.example.com:443"_ns, 10);
  }
  ASSERT_EQ(mozilla::net::SSLTokensCache::CacheSizeForTest(), 0u)
      << "evicting every record must empty the cert store";
}

static already_AddRefed<mozIStorageConnection> openTestDB(
    const nsCString& aPath) {
  nsCOMPtr<nsIFile> file;
  if (NS_FAILED(NS_NewNativeLocalFile(aPath, getter_AddRefs(file)))) {
    return nullptr;
  }
  nsCOMPtr<mozIStorageService> storage =
      do_GetService(MOZ_STORAGE_SERVICE_CONTRACTID);
  if (!storage) {
    return nullptr;
  }
  nsCOMPtr<mozIStorageConnection> conn;
  if (NS_FAILED(storage->OpenUnsharedDatabase(
          file, mozIStorageService::CONNECTION_DEFAULT,
          getter_AddRefs(conn)))) {
    return nullptr;
  }
  return conn.forget();
}

TEST(TestTokensCache, PersistenceStoresEachCertOnce)
{
  ClearAll();
  mozilla::Preferences::SetInt("network.ssl_tokens_cache_records_per_entry", 3);
  nsCString path = GetTempCachePath("test_tls_tc_certs_once.sqlite");

  putToken("anon:a.example.com:443"_ns, 100);
  putToken("anon:a.example.com:443"_ns, 200);
  putToken("anon:b.example.com:443"_ns, 150);
  uint32_t sizeBefore = mozilla::net::SSLTokensCache::CacheSizeForTest();
  mozilla::net::SSLTokensCache::TriggerWriteForTest(path);

  nsCOMPtr<mozIStorageConnection> conn = openTestDB(path);
  ASSERT_TRUE(conn);
  nsCOMPtr<mozIStorageStatement> stmt;
  ASSERT_NS_SUCCEEDED(conn->CreateStatement("SELECT COUNT(*) FROM ssl_certs"_ns,
                                            getter_AddRefs(stmt)));
  bool hasResult = false;
  ASSERT_NS_SUCCEEDED(stmt->ExecuteStep(&hasResult));
  ASSERT_TRUE(hasResult);
  ASSERT_EQ(stmt->AsInt32(0), 1) << "one distinct cert across three records";
  stmt->Finalize();
  conn->Close();

  ClearAll();
  mozilla::net::SSLTokensCache::LoadForTest(path);
  ASSERT_EQ(mozilla::net::SSLTokensCache::CacheSizeForTest(), sizeBefore)
      << "reload must rebuild the same store and records";

  RefPtr<CommonSocketControl> sc =
      SimulateResumedConnection("anon:b.example.com:443"_ns);
  ASSERT_TRUE(sc);
  nsCOMPtr<nsIX509Cert> serverCert = sc->GetServerCert();
  ASSERT_TRUE(serverCert);
  nsTArray<uint8_t> der;
  ASSERT_NS_SUCCEEDED(serverCert->GetRawDER(der));
  ASSERT_EQ(der.Length(), serverCertSize());
}

TEST(TestTokensCache, CertsStoredInSharedStore)
{
  // Both records refer to the same cert, which the snapshot holds once.
  ClearAll();
  mozilla::Preferences::SetInt("network.ssl_tokens_cache_records_per_entry", 3);
  uint32_t certSize = serverCertSize();

  putToken("anon:a.example.com:443"_ns, 10);
  putToken("anon:b.example.com:443"_ns, 10);

  mozilla::net::SSLTokensCacheSnapshot snapshot = getCacheSnapshot();
  ASSERT_EQ(snapshot.records.Length(), 2u);
  ASSERT_EQ(snapshot.certs.Count(), 1u);

  nsTArray<uint8_t> token;
  mozilla::net::SessionCacheInfo info;
  ASSERT_EQ(mozilla::net::SSLTokensCache::Get("anon:a.example.com:443"_ns,
                                              token, info),
            NS_OK);
  ASSERT_EQ(info.mServerCertBytes.Length(), certSize);
  ASSERT_TRUE(info.mSucceededCertChainBytes.isSome());
  ASSERT_EQ(info.mSucceededCertChainBytes->Length(), 3u);
  ASSERT_TRUE(info.mHandshakeCertificatesBytes.isSome());
  ASSERT_EQ(info.mHandshakeCertificatesBytes->Length(), 1u);
}

static constexpr uint32_t kFakeCertLen = 400;

// The cache never parses cert DER.
static nsTArray<uint8_t> fakeCert(uint8_t aTag, uint32_t aLen = kFakeCertLen) {
  nsTArray<uint8_t> der;
  der.SetLength(aLen);
  uint32_t state = 0x9E3779B9u * (aTag + 1);
  for (auto& b : der) {
    state = state * 1664525u + 1013904223u;
    b = static_cast<uint8_t>(state >> 24);
  }
  return der;
}

static nsTArray<nsTArray<uint8_t>> fakeChain(
    std::initializer_list<uint8_t> aTags) {
  nsTArray<nsTArray<uint8_t>> chain;
  for (uint8_t tag : aTags) {
    chain.AppendElement(fakeCert(tag));
  }
  return chain;
}

static nsresult putTokenWithServerCert(
    const nsACString& aKey, uint32_t aSize, nsTArray<uint8_t>&& aServerCert,
    nsTArray<nsTArray<uint8_t>>&& aSucceeded,
    nsTArray<nsTArray<uint8_t>>&& aHandshake,
    PRTime aExpiry = PR_Now() + PRTime(3600) * PR_USEC_PER_SEC,
    nsITransportSecurityInfo::OverridableErrorCategory aError =
        nsITransportSecurityInfo::OverridableErrorCategory::ERROR_UNSET) {
  RefPtr<CommonSocketControl> sc(
      new CommonSocketControl(nsLiteralCString("example.com"), 433, 0));
  nsCOMPtr<nsIX509Cert> serverCert =
      new nsNSSCertificate(std::move(aServerCert));
  sc->SetServerCert(serverCert, mozilla::psm::EVStatus::NotEV);
  if (aError !=
      nsITransportSecurityInfo::OverridableErrorCategory::ERROR_UNSET) {
    sc->SetStatusErrorBits(aError);
  }
  sc->SetSucceededCertChain(std::move(aSucceeded));
  sc->SetHandshakeCertificates(std::move(aHandshake));
  nsTArray<uint8_t> token = MakeTestData(aSize);
  return mozilla::net::SSLTokensCache::Put(
      aKey, token.Elements(), aSize, sc, aExpiry, /* aPlaintextSecret */ true);
}

static nsresult putTokenWithChains(
    const nsACString& aKey, uint32_t aSize,
    nsTArray<nsTArray<uint8_t>>&& aSucceeded,
    nsTArray<nsTArray<uint8_t>>&& aHandshake,
    PRTime aExpiry = PR_Now() + PRTime(3600) * PR_USEC_PER_SEC,
    nsITransportSecurityInfo::OverridableErrorCategory aError =
        nsITransportSecurityInfo::OverridableErrorCategory::ERROR_UNSET) {
  nsTArray<uint8_t> serverCert = aHandshake[0].Clone();
  return putTokenWithServerCert(aKey, aSize, std::move(serverCert),
                                std::move(aSucceeded), std::move(aHandshake),
                                aExpiry, aError);
}

static void putTokenWithCerts(
    const nsACString& aKey, uint32_t aSize,
    std::initializer_list<uint8_t> aSucceeded,
    std::initializer_list<uint8_t> aHandshake,
    PRTime aExpiry = PR_Now() + PRTime(3600) * PR_USEC_PER_SEC,
    nsITransportSecurityInfo::OverridableErrorCategory aError =
        nsITransportSecurityInfo::OverridableErrorCategory::ERROR_UNSET) {
  ASSERT_EQ(putTokenWithChains(aKey, aSize, fakeChain(aSucceeded),
                               fakeChain(aHandshake), aExpiry, aError),
            NS_OK);
}

static void getAndCheckCerts(const nsACString& aKey, uint32_t aTokenSize,
                             std::initializer_list<uint8_t> aSucceeded,
                             std::initializer_list<uint8_t> aHandshake) {
  ASSERT_GT(aHandshake.size(), 0u);
  nsTArray<uint8_t> token;
  mozilla::net::SessionCacheInfo info;
  ASSERT_EQ(mozilla::net::SSLTokensCache::Get(aKey, token, info), NS_OK);
  ASSERT_EQ(token.Length(), aTokenSize);
  ASSERT_EQ(info.mServerCertBytes, fakeCert(*aHandshake.begin()));
  if (aSucceeded.size() == 0) {
    ASSERT_TRUE(info.mSucceededCertChainBytes.isNothing());
  } else {
    ASSERT_TRUE(info.mSucceededCertChainBytes.isSome());
    ASSERT_EQ(*info.mSucceededCertChainBytes, fakeChain(aSucceeded));
  }
  ASSERT_TRUE(info.mHandshakeCertificatesBytes.isSome());
  ASSERT_EQ(*info.mHandshakeCertificatesBytes, fakeChain(aHandshake));
}

TEST(TestTokensCache, SnapshotMissingCertDropsRecord)
{
  // A record whose cert isn't in the snapshot or the store is skipped rather
  // than inserted with a dangling reference.
  ClearAll();
  putTokenWithCerts("anon:a.example.com:443"_ns, 100, {1, 2, 3}, {1, 2});

  mozilla::net::SSLTokensCacheSnapshot snapshot = getCacheSnapshot();
  ASSERT_EQ(snapshot.records.Length(), 1u);
  ASSERT_TRUE(snapshot.records[0].succeededHashes.isSome());
  // Certs 1 and 2 still resolve; only cert 3 is missing.
  const auto& lastHash = snapshot.records[0].succeededHashes->LastElement();
  ASSERT_TRUE(snapshot.certs.Remove(nsDependentCSubstring(
      reinterpret_cast<const char*>(lastHash.Elements()), lastHash.Length())));

  mozilla::net::SSLTokensCache::ReplaceAllRecordsForTest(std::move(snapshot));
  ASSERT_EQ(mozilla::net::SSLTokensCache::CountForTest(), 0u);
  ASSERT_EQ(mozilla::net::SSLTokensCache::CacheSizeForTest(), 0u);
}

TEST(TestTokensCache, DistinctCertsRoundTrip)
{
  // Leaf 1, intermediate 2, root 3; the server sent the leaf and intermediate,
  // or the leaf and a cert 4 that isn't in the built chain.
  nsCString path = GetTempCachePath("test_tls_tc_distinct_certs.sqlite");
  ClearAll();

  putTokenWithCerts("anon:a.example.com:443"_ns, 100, {1, 2, 3}, {1, 2});
  getAndCheckCerts("anon:a.example.com:443"_ns, 100, {1, 2, 3}, {1, 2});

  putTokenWithCerts("anon:a.example.com:443"_ns, 100, {1, 2, 3}, {1, 4});
  getAndCheckCerts("anon:a.example.com:443"_ns, 100, {1, 2, 3}, {1, 4});

  putTokenWithCerts("anon:a.example.com:443"_ns, 100, {1, 2, 3}, {1, 2});
  mozilla::net::SSLTokensCache::TriggerWriteForTest(path);
  ClearAll();
  mozilla::net::SSLTokensCache::LoadForTest(path);
  getAndCheckCerts("anon:a.example.com:443"_ns, 100, {1, 2, 3}, {1, 2});

  putTokenWithCerts("anon:a.example.com:443"_ns, 100, {1, 2, 3}, {1, 2});
  mozilla::net::SSLTokensCache::ReplaceAllRecordsForTest(getCacheSnapshot());
  getAndCheckCerts("anon:a.example.com:443"_ns, 100, {1, 2, 3}, {1, 2});
}

TEST(TestTokensCache, MalformedCertHashesDropRecord)
{
  using Corrupt = void (*)(nsTArray<uint8_t>&);
  for (Corrupt corrupt : std::initializer_list<Corrupt>{
           [](nsTArray<uint8_t>& aHashes) { aHashes.RemoveLastElement(); },
           [](nsTArray<uint8_t>& aHashes) { aHashes.AppendElement(0); },
           // Byte 1 is the succeeded chain's count.
           [](nsTArray<uint8_t>& aHashes) { aHashes[1]++; }}) {
    ClearAll();
    nsCString path = GetTempCachePath("test_tls_tc_malformed_hashes.sqlite");
    putTokenWithCerts("anon:a.example.com:443"_ns, 100, {1, 2, 3}, {1, 2});
    mozilla::net::SSLTokensCache::TriggerWriteForTest(path);

    nsCOMPtr<mozIStorageConnection> conn = openTestDB(path);
    ASSERT_TRUE(conn);
    nsTArray<uint8_t> hashes;
    {
      nsCOMPtr<mozIStorageStatement> select;
      ASSERT_NS_SUCCEEDED(conn->CreateStatement(
          "SELECT cert_hashes FROM ssl_tokens"_ns, getter_AddRefs(select)));
      bool hasResult = false;
      ASSERT_NS_SUCCEEDED(select->ExecuteStep(&hasResult));
      ASSERT_TRUE(hasResult);
      uint32_t len = 0;
      const uint8_t* data = nullptr;
      ASSERT_NS_SUCCEEDED(select->GetSharedBlob(0, &len, &data));
      hashes.AppendElements(data, len);
      select->Finalize();
    }
    corrupt(hashes);
    nsCOMPtr<mozIStorageStatement> update;
    ASSERT_NS_SUCCEEDED(
        conn->CreateStatement("UPDATE ssl_tokens SET cert_hashes = :hashes"_ns,
                              getter_AddRefs(update)));
    ASSERT_NS_SUCCEEDED(update->BindBlobByName("hashes"_ns, hashes.Elements(),
                                               hashes.Length()));
    ASSERT_NS_SUCCEEDED(update->Execute());
    update->Finalize();
    conn->Close();

    ClearAll();
    bool corrupted = false;
    mozilla::net::SSLTokensCache::LoadForTest(path, &corrupted);
    EXPECT_TRUE(corrupted);
    ASSERT_EQ(mozilla::net::SSLTokensCache::CountForTest(), 0u);
    ASSERT_EQ(mozilla::net::SSLTokensCache::CacheSizeForTest(), 0u);
  }
}

static nsTArray<uint8_t> certHash(const nsTArray<uint8_t>& aDER) {
  nsTArray<uint8_t> hash;
  EXPECT_NS_SUCCEEDED(mozilla::Digest::DigestBuf(SEC_OID_SHA256, aDER, hash));
  return hash;
}

TEST(TestTokensCache, CertExceptionRoundTrip)
{
  ClearAll();
  putTokenWithCerts(
      "anon:a.example.com:443"_ns, 100, {}, {1, 2},
      PR_Now() + PRTime(3600) * PR_USEC_PER_SEC,
      nsITransportSecurityInfo::OverridableErrorCategory::ERROR_TRUST);

  nsCString path = GetTempCachePath("test_tls_tc_cert_exception.sqlite");
  mozilla::net::SSLTokensCache::TriggerWriteForTest(path);

  nsTArray<uint8_t> token;
  mozilla::net::SessionCacheInfo info;
  ASSERT_EQ(mozilla::net::SSLTokensCache::Get("anon:a.example.com:443"_ns,
                                              token, info),
            NS_OK);
  ASSERT_EQ(info.mOverridableErrorCategory,
            nsITransportSecurityInfo::OverridableErrorCategory::ERROR_TRUST);
  ASSERT_EQ(info.mServerCertBytes, fakeCert(1));
  ASSERT_TRUE(info.mSucceededCertChainBytes.isNothing());
  ASSERT_TRUE(info.mHandshakeCertificatesBytes.isSome());
  ASSERT_EQ(*info.mHandshakeCertificatesBytes, fakeChain({1, 2}));

  ClearAll();
  mozilla::net::SSLTokensCache::LoadForTest(path);
  ASSERT_EQ(mozilla::net::SSLTokensCache::CountForTest(), 0u)
      << "cert exceptions must not be persisted";
}

TEST(TestTokensCache, PutRejectsOversizedCert)
{
  auto restoreCapacity = setCapacity(1024);
  ClearAll();
  for (uint32_t certLen : {0xffffu, 0x10000u}) {
    nsTArray<nsTArray<uint8_t>> handshake;
    handshake.AppendElement(fakeCert(1, certLen));
    nsresult rv = putTokenWithChains("anon:a.example.com:443"_ns, 100, {},
                                     std::move(handshake));
    ASSERT_EQ(rv, certLen <= 0xffff ? NS_OK : NS_ERROR_FAILURE);
  }
  ASSERT_EQ(mozilla::net::SSLTokensCache::CountForTest(), 1u);
  nsTArray<uint8_t> token;
  mozilla::net::SessionCacheInfo info;
  ASSERT_EQ(mozilla::net::SSLTokensCache::Get("anon:a.example.com:443"_ns,
                                              token, info),
            NS_OK);
  ASSERT_EQ(info.mServerCertBytes, fakeCert(1, 0xffff));
}

TEST(TestTokensCache, PutRejectsRecordAboveDecodedLimit)
{
  auto restoreCapacity = setCapacity(1024);
  ClearAll();
  // 100 token bytes + 9 metadata bytes + 3 x 0xffff reaches 256 KiB with 65430.
  for (uint32_t lastCertLen : {65430u, 65431u}) {
    nsTArray<nsTArray<uint8_t>> succeeded;
    succeeded.AppendElement(fakeCert(1, 0xffff));
    succeeded.AppendElement(fakeCert(2, 0xffff));
    succeeded.AppendElement(fakeCert(3, lastCertLen));
    nsTArray<nsTArray<uint8_t>> handshake;
    handshake.AppendElement(fakeCert(1, 0xffff));
    nsresult rv =
        putTokenWithChains("anon:a.example.com:443"_ns, 100,
                           std::move(succeeded), std::move(handshake));
    ASSERT_EQ(rv, lastCertLen == 65430u ? NS_OK : NS_ERROR_FAILURE);
  }
  ASSERT_EQ(mozilla::net::SSLTokensCache::CountForTest(), 1u);

  nsTArray<uint8_t> token;
  mozilla::net::SessionCacheInfo info;
  ASSERT_EQ(mozilla::net::SSLTokensCache::Get("anon:a.example.com:443"_ns,
                                              token, info),
            NS_OK);
  ASSERT_EQ(info.mServerCertBytes, fakeCert(1, 0xffff));
  ASSERT_EQ(mozilla::net::SSLTokensCache::CacheSizeForTest(), 0u);
}

TEST(TestTokensCache, PersistenceRejectsInvalidCerts)
{
  ClearAll();
  putTokenWithCerts("anon:b.example.com:443"_ns, 100, {4, 5, 6}, {4, 5});
  const uint32_t oneRecordSize =
      mozilla::net::SSLTokensCache::CacheSizeForTest();
  struct Case {
    nsLiteralCString sql;
    bool expectCorrupted;
  };
  for (const auto& test : {
           Case{"DELETE FROM ssl_certs"_ns, true},
           Case{"UPDATE ssl_certs SET der = zeroblob(400)"_ns, true},
           Case{"UPDATE ssl_certs SET hash = X'00'"_ns, true},
           Case{"UPDATE ssl_certs SET der = zeroblob(65536)"_ns, false},
       }) {
    ClearAll();
    nsCString path = GetTempCachePath("test_tls_tc_invalid_certs.sqlite");
    putTokenWithCerts("anon:a.example.com:443"_ns, 100, {1, 2, 3}, {1, 2});
    putTokenWithCerts("anon:b.example.com:443"_ns, 100, {4, 5, 6}, {4, 5});
    mozilla::net::SSLTokensCache::TriggerWriteForTest(path);

    nsCOMPtr<mozIStorageConnection> conn = openTestDB(path);
    ASSERT_TRUE(conn);
    ASSERT_NS_SUCCEEDED(conn->ExecuteSimpleSQL(
        test.sql + " WHERE rowid = (SELECT MIN(rowid) FROM ssl_certs)"_ns));
    conn->Close();

    ClearAll();
    bool corrupted = false;
    mozilla::net::SSLTokensCache::LoadForTest(path, &corrupted);
    EXPECT_EQ(corrupted, test.expectCorrupted) << test.sql.get();
    ASSERT_EQ(mozilla::net::SSLTokensCache::CountForTest(), 1u)
        << test.sql.get();
    ASSERT_EQ(mozilla::net::SSLTokensCache::CacheSizeForTest(), oneRecordSize)
        << test.sql.get();
  }
}

TEST(TestTokensCache, PersistenceRecreatesIncompleteSchema)
{
  struct Case {
    int32_t version;
    nsLiteralCString sql;
  };
  for (const auto& c : {
           Case{3,
                "CREATE TABLE ssl_tokens (key TEXT NOT NULL, expiration_time "
                "INTEGER NOT NULL, overridable_error INTEGER NOT NULL, "
                "cert_hashes BLOB, payload BLOB NOT NULL)"_ns},
           Case{3,
                "CREATE TABLE ssl_certs (hash BLOB PRIMARY KEY NOT NULL, der "
                "BLOB NOT NULL)"_ns},
           Case{1,
                "CREATE TABLE ssl_tokens (key TEXT NOT NULL, expiration_time "
                "INTEGER NOT NULL, overridable_error INTEGER NOT NULL, "
                "payload BLOB NOT NULL)"_ns},
       }) {
    ClearAll();
    nsCString path = GetTempCachePath("test_tls_tc_incomplete_schema.sqlite");
    nsCOMPtr<mozIStorageConnection> conn = openTestDB(path);
    ASSERT_TRUE(conn);
    ASSERT_NS_SUCCEEDED(conn->SetSchemaVersion(c.version));
    ASSERT_NS_SUCCEEDED(conn->ExecuteSimpleSQL(c.sql));
    conn->Close();

    putTokenWithCerts("anon:a.example.com:443"_ns, 100, {1, 2, 3}, {1, 2});
    mozilla::net::SSLTokensCache::TriggerWriteForTest(path);
    ClearAll();
    mozilla::net::SSLTokensCache::LoadForTest(path);
    getAndCheckCerts("anon:a.example.com:443"_ns, 100, {1, 2, 3}, {1, 2});
  }
}

TEST(TestTokensCache, RecordsDecodeAfterReplace)
{
  // Records sharing certs decode through the snapshot, replacement and a
  // persistence round trip.
  ClearAll();
  nsCString path = GetTempCachePath("test_tls_tc_mixed_records.sqlite");
  putTokenWithCerts("anon:a.example.com:443"_ns, 100, {1, 2, 3}, {1, 2});
  putTokenWithCerts("anon:b.example.com:443"_ns, 100, {4, 2, 3}, {4, 2});

  mozilla::net::SSLTokensCacheSnapshot snapshot;
  mozilla::net::SSLTokensCache::GetAllRecords(snapshot);
  ASSERT_EQ(snapshot.records.Length(), 2u);
  for (const auto& rec : snapshot.records) {
    nsTArray<uint8_t> token;
    mozilla::net::SessionCacheInfo info;
    ASSERT_TRUE(mozilla::net::SSLTokensCache::DecodeCompressedPayload(
        rec, snapshot.certs, token, info));
    ASSERT_EQ(token.Length(), 100u);
  }

  mozilla::net::SSLTokensCache::ReplaceAllRecordsForTest(std::move(snapshot));
  mozilla::net::SSLTokensCache::TriggerWriteForTest(path);
  ClearAll();
  mozilla::net::SSLTokensCache::LoadForTest(path);
  getAndCheckCerts("anon:a.example.com:443"_ns, 100, {1, 2, 3}, {1, 2});
  getAndCheckCerts("anon:b.example.com:443"_ns, 100, {4, 2, 3}, {4, 2});
  ASSERT_EQ(mozilla::net::SSLTokensCache::CacheSizeForTest(), 0u);
}

TEST(TestTokensCache, CertStoreReleasedByOtherRemovalRoutes)
{
  // a and b share intermediate 2 and root 3, but not their leaves 1 and 4, so
  // removing a, by any route, must free only its leaf.
  const uint32_t certEntrySize = 32 + kFakeCertLen;
  const PRTime valid = PR_Now() + PRTime(3600) * PR_USEC_PER_SEC;
  const PRTime expired = PR_Now() - PR_USEC_PER_SEC;

  auto check = [&](const nsACString& aKeyA, PRTime aExpiryA, auto aRemoveA) {
    ClearAll();
    putTokenWithCerts(aKeyA, 100, {1, 2, 3}, {1, 2}, aExpiryA);
    uint32_t sizeA = mozilla::net::SSLTokensCache::CacheSizeForTest();
    putTokenWithCerts("anon:b.example.com:443"_ns, 100, {4, 2, 3}, {4, 2});
    uint32_t sizeAB = mozilla::net::SSLTokensCache::CacheSizeForTest();

    aRemoveA();
    ASSERT_EQ(mozilla::net::SSLTokensCache::CacheSizeForTest(),
              sizeAB - sizeA + 2 * certEntrySize);
    getAndCheckCerts("anon:b.example.com:443"_ns, 100, {4, 2, 3}, {4, 2});
    ASSERT_EQ(mozilla::net::SSLTokensCache::CacheSizeForTest(), 0u);
  };

  check("anon:a.example.com:443"_ns, valid, [] {
    ASSERT_EQ(
        mozilla::net::SSLTokensCache::RemoveAll("anon:a.example.com:443"_ns),
        NS_OK);
  });
  check("anon:a.example.com:443"_ns, valid, [] {
    mozilla::net::SSLTokensCache::RemoveByHostAndOAPattern(
        "a.example.com"_ns, mozilla::OriginAttributesPattern());
  });
  check("a.example.com:443^privateBrowsingId=1"_ns, valid,
        [] { mozilla::net::SSLTokensCache::ClearPrivateBrowsing(); });
  check("anon:a.example.com:443"_ns, expired, [] {
    nsTArray<uint8_t> token;
    mozilla::net::SessionCacheInfo info;
    ASSERT_EQ(mozilla::net::SSLTokensCache::Get("anon:a.example.com:443"_ns,
                                                token, info),
              NS_ERROR_NOT_AVAILABLE);
  });
}

TEST(TestTokensCache, LoadIgnoresOrphanCerts)
{
  ClearAll();
  nsCString path = GetTempCachePath("test_tls_tc_orphan_certs.sqlite");
  putTokenWithCerts("anon:a.example.com:443"_ns, 100, {1, 2, 3}, {1, 2});
  putTokenWithCerts("anon:b.example.com:443"_ns, 100, {4, 2, 3}, {4, 2});
  uint32_t sizeBefore = mozilla::net::SSLTokensCache::CacheSizeForTest();
  mozilla::net::SSLTokensCache::TriggerWriteForTest(path);

  nsCOMPtr<mozIStorageConnection> conn = openTestDB(path);
  ASSERT_TRUE(conn);
  ASSERT_NS_SUCCEEDED(conn->ExecuteSimpleSQL(
      "WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n "
      "WHERE i < 100) INSERT INTO ssl_certs SELECT randomblob(32), "
      "zeroblob(65535) FROM n"_ns));
  conn->Close();

  ClearAll();
  uint32_t rowsBefore = mozilla::net::SSLTokensCache::CertRowsLoadedForTest();
  mozilla::net::SSLTokensCache::LoadForTest(path);
  ASSERT_EQ(mozilla::net::SSLTokensCache::CertRowsLoadedForTest() - rowsBefore,
            4u);
  ASSERT_EQ(mozilla::net::SSLTokensCache::CountForTest(), 2u);
  ASSERT_EQ(mozilla::net::SSLTokensCache::CacheSizeForTest(), sizeBefore);
  getAndCheckCerts("anon:a.example.com:443"_ns, 100, {1, 2, 3}, {1, 2});
}

TEST(TestTokensCache, LoadRejectsRecordOverCertBudget)
{
  // A handshake chain of five 0xffff-byte certs with valid fingerprints, all
  // distinct or one cert five times; both exceed the decoded-size limit.
  for (const auto& tags : std::initializer_list<std::initializer_list<uint8_t>>{
           {10, 11, 12, 13, 14}, {10, 10, 10, 10, 10}}) {
    ClearAll();
    nsCString path = GetTempCachePath("test_tls_tc_cert_budget.sqlite");
    putTokenWithCerts("anon:a.example.com:443"_ns, 100, {1, 2, 3}, {1, 2});
    mozilla::net::SSLTokensCache::TriggerWriteForTest(path);

    nsCOMPtr<mozIStorageConnection> conn = openTestDB(path);
    ASSERT_TRUE(conn);
    nsCOMPtr<mozIStorageStatement> insert;
    ASSERT_NS_SUCCEEDED(conn->CreateStatement(
        "INSERT OR IGNORE INTO ssl_certs (hash, der) VALUES (:hash, :der)"_ns,
        getter_AddRefs(insert)));
    nsTArray<uint8_t> hashes = {0, 1, 5};
    for (uint8_t tag : tags) {
      nsTArray<uint8_t> der = fakeCert(tag, 0xffff);
      nsTArray<uint8_t> hash = certHash(der);
      hashes.AppendElements(hash);
      ASSERT_NS_SUCCEEDED(
          insert->BindBlobByName("hash"_ns, hash.Elements(), hash.Length()));
      ASSERT_NS_SUCCEEDED(
          insert->BindBlobByName("der"_ns, der.Elements(), der.Length()));
      ASSERT_NS_SUCCEEDED(insert->Execute());
    }
    insert->Finalize();
    nsCOMPtr<mozIStorageStatement> update;
    ASSERT_NS_SUCCEEDED(
        conn->CreateStatement("UPDATE ssl_tokens SET cert_hashes = :hashes"_ns,
                              getter_AddRefs(update)));
    ASSERT_NS_SUCCEEDED(update->BindBlobByName("hashes"_ns, hashes.Elements(),
                                               hashes.Length()));
    ASSERT_NS_SUCCEEDED(update->Execute());
    update->Finalize();
    conn->Close();

    ClearAll();
    bool corrupted = false;
    mozilla::net::SSLTokensCache::LoadForTest(path, &corrupted);
    EXPECT_FALSE(corrupted);
    ASSERT_EQ(mozilla::net::SSLTokensCache::CountForTest(), 0u);
    ASSERT_EQ(mozilla::net::SSLTokensCache::CacheSizeForTest(), 0u);
  }
}

TEST(TestTokensCache, PutRejectsServerCertNotFirstHandshakeCert)
{
  ClearAll();
  putTokenWithCerts("anon:a.example.com:443"_ns, 100, {1, 2, 3}, {1, 2});
  uint32_t sizeBefore = mozilla::net::SSLTokensCache::CacheSizeForTest();
  // Without handshake certs, or with a server cert that isn't the first one.
  ASSERT_EQ(putTokenWithServerCert("anon:a.example.com:443"_ns, 100,
                                   fakeCert(1), {}, {}),
            NS_ERROR_FAILURE);
  ASSERT_EQ(putTokenWithServerCert("anon:a.example.com:443"_ns, 100,
                                   fakeCert(9), {}, fakeChain({1, 2})),
            NS_ERROR_FAILURE);
  ASSERT_EQ(mozilla::net::SSLTokensCache::CountForTest(), 1u);
  ASSERT_EQ(mozilla::net::SSLTokensCache::CacheSizeForTest(), sizeBefore);
  getAndCheckCerts("anon:a.example.com:443"_ns, 100, {1, 2, 3}, {1, 2});
}

TEST(TestTokensCache, PutForTestRoundTrip)
{
  const auto key = "anon:put-for-test.example.com:443"_ns;
  const nsTArray<uint8_t> expectedToken = {0xDE, 0xAD, 0xBE, 0xEF};
  ClearAll();
  mozilla::net::SSLTokensCache::PutForTest(key);

  nsTArray<uint8_t> token;
  mozilla::net::SessionCacheInfo info;
  ASSERT_EQ(mozilla::net::SSLTokensCache::Get(key, token, info), NS_OK);
  EXPECT_EQ(token, expectedToken);
  ASSERT_TRUE(info.mHandshakeCertificatesBytes.isSome());
  ASSERT_EQ(info.mHandshakeCertificatesBytes->Length(), 1u);
  EXPECT_FALSE(info.mServerCertBytes.IsEmpty());
  EXPECT_EQ(info.mServerCertBytes, (*info.mHandshakeCertificatesBytes)[0]);
}

TEST(TestTokensCache, PutAfterResumedConnection)
{
  ClearAll();
  putTokenWithCerts("anon:a.example.com:443"_ns, 100, {1, 2, 3}, {1, 2});
  RefPtr<CommonSocketControl> sc =
      SimulateResumedConnection("anon:a.example.com:443"_ns);
  ASSERT_TRUE(sc);
  nsTArray<uint8_t> token = MakeTestData(200);
  ASSERT_EQ(mozilla::net::SSLTokensCache::Put(
                "anon:a.example.com:443"_ns, token.Elements(), 200, sc,
                PR_Now() + PRTime(3600) * PR_USEC_PER_SEC),
            NS_OK);
  getAndCheckCerts("anon:a.example.com:443"_ns, 200, {1, 2, 3}, {1, 2});
}

TEST(TestTokensCache, PersistenceSkipsCertsOfFilteredRecords)
{
  ClearAll();
  nsCString path = GetTempCachePath("test_tls_tc_filtered_certs.sqlite");
  putTokenWithCerts("anon:a.example.com:443"_ns, 100, {1, 2, 3}, {1, 2});
  putTokenWithCerts("b.example.com:443^privateBrowsingId=1"_ns, 100, {4, 2, 3},
                    {4, 2});
  putTokenWithCerts(
      "anon:c.example.com:443"_ns, 100, {}, {5, 2},
      PR_Now() + PRTime(3600) * PR_USEC_PER_SEC,
      nsITransportSecurityInfo::OverridableErrorCategory::ERROR_TRUST);
  mozilla::net::SSLTokensCache::TriggerWriteForTest(path);

  nsCOMPtr<mozIStorageConnection> conn = openTestDB(path);
  ASSERT_TRUE(conn);
  nsCOMPtr<mozIStorageStatement> stmt;
  ASSERT_NS_SUCCEEDED(conn->CreateStatement(
      "SELECT COUNT(*) FROM ssl_tokens"_ns, getter_AddRefs(stmt)));
  bool hasResult = false;
  ASSERT_NS_SUCCEEDED(stmt->ExecuteStep(&hasResult));
  ASSERT_TRUE(hasResult);
  ASSERT_EQ(stmt->AsInt32(0), 1);
  stmt->Finalize();

  ASSERT_NS_SUCCEEDED(conn->CreateStatement("SELECT hash FROM ssl_certs"_ns,
                                            getter_AddRefs(stmt)));
  nsTArray<nsTArray<uint8_t>> hashes;
  for (;;) {
    ASSERT_NS_SUCCEEDED(stmt->ExecuteStep(&hasResult));
    if (!hasResult) {
      break;
    }
    uint32_t len = 0;
    const uint8_t* data = nullptr;
    ASSERT_NS_SUCCEEDED(stmt->GetSharedBlob(0, &len, &data));
    hashes.AppendElement(nsTArray<uint8_t>(data, len));
  }
  stmt->Finalize();
  conn->Close();

  ASSERT_EQ(hashes.Length(), 3u);
  for (uint8_t tag : {1, 2, 3}) {
    ASSERT_TRUE(hashes.Contains(certHash(fakeCert(tag))))
    << "cert " << int(tag) << " must be persisted";
  }
}
