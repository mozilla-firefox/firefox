#include "CertVerifier.h"
#include "CommonSocketControl.h"
#include "SSLTokensCache.h"
#include "TransportSecurityInfo.h"
#include "gtest/gtest.h"
#include "mozilla/Preferences.h"
#include "mozilla/glean/NetwerkMetrics.h"
#include "mozilla/gtest/MozAssertions.h"
#include "nsDirectoryServiceUtils.h"
#include "nsIFile.h"
#include "nsITransportSecurityInfo.h"
#include "nsIWebProgressListener.h"
#include "nsIX509Cert.h"
#include "nsIX509CertDB.h"
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
  RefPtr<CommonSocketControl> socketControl(
      new CommonSocketControl(nsLiteralCString("example.com"), 433, 0));
  socketControl->SetServerCert(cert, mozilla::psm::EVStatus::NotEV);
  socketControl->SetSucceededCertChain(std::move(succeededCertChain));
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

static void putTokenWithExpiry(const nsACString& aKey, uint32_t aSize,
                               PRTime aExpiry) {
  RefPtr<CommonSocketControl> socketControl = createDummySocketControl();
  nsTArray<uint8_t> token = MakeTestData(aSize);
  nsresult rv = mozilla::net::SSLTokensCache::Put(aKey, token.Elements(), aSize,
                                                  socketControl, aExpiry);
  ASSERT_EQ(rv, NS_OK);
}

static void putToken(const nsACString& aKey, uint32_t aSize) {
  putTokenWithExpiry(aKey, aSize, PR_Now() + PRTime(aSize) * PR_USEC_PER_SEC);
}

static void ClearAll() { mozilla::net::SSLTokensCache::Clear(); }

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

  mozilla::Preferences::SetInt("network.ssl_tokens_cache_capacity", 5);

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
  mozilla::Preferences::SetInt("network.ssl_tokens_cache_capacity", 3);

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

  PRTime now = PR_Now();
  RefPtr<CommonSocketControl> sc = createDummySocketControl();
  nsTArray<uint8_t> expiredToken = MakeTestData(100);
  nsTArray<uint8_t> validToken = MakeTestData(200);

  ASSERT_EQ(mozilla::net::SSLTokensCache::Put(
                "anon:example.com:443"_ns, expiredToken.Elements(), 100, sc,
                now - PRTime(1) * PR_USEC_PER_SEC),
            NS_OK);
  ASSERT_EQ(mozilla::net::SSLTokensCache::Put(
                "anon:example.com:443"_ns, validToken.Elements(), 200, sc,
                now + PRTime(3600) * PR_USEC_PER_SEC),
            NS_OK);

  mozilla::net::SSLTokensCache::TriggerWriteForTest(path);
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
  // createDummySocketControl() stores the same cert DER as the server cert
  // and three times in the succeeded chain.  The compressor sees all four
  // copies in one payload and encodes them once, so the stored record must
  // be well under 2x the raw single-cert size.
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

static void corruptPayload(mozilla::net::SSLTokensCacheRecordInfo& aRec) {
  aRec.compressedPayload = {0xFF, 0xFF, 0xFF, 0xFF, 0x00};
}

TEST(TestTokensCache, GetSkipsUndecodableRecord)
{
  ClearAll();
  mozilla::Preferences::SetInt("network.ssl_tokens_cache_records_per_entry", 3);
  putToken("anon:a.example.com:443"_ns, 100);
  putToken("anon:a.example.com:443"_ns, 200);

  nsTArray<mozilla::net::SSLTokensCacheRecordInfo> records;
  mozilla::net::SSLTokensCache::GetAllRecords(records);
  ASSERT_EQ(records.Length(), 2u);
  auto& first = records[0].expirationTime < records[1].expirationTime
                    ? records[0]
                    : records[1];
  corruptPayload(first);
  mozilla::net::SSLTokensCache::ReplaceAllRecordsForTest(std::move(records));

  getAndCheckResult("anon:a.example.com:443"_ns, 200);
  ASSERT_EQ(mozilla::net::SSLTokensCache::CountForTest(), 0u);
}

TEST(TestTokensCache, GetWithOnlyUndecodableRecordsIsMiss)
{
  ClearAll();
  mozilla::Preferences::SetInt("network.ssl_tokens_cache_records_per_entry", 3);
  putToken("anon:a.example.com:443"_ns, 100);
  putToken("anon:a.example.com:443"_ns, 200);

  nsTArray<mozilla::net::SSLTokensCacheRecordInfo> records;
  mozilla::net::SSLTokensCache::GetAllRecords(records);
  for (auto& rec : records) {
    corruptPayload(rec);
  }
  mozilla::net::SSLTokensCache::ReplaceAllRecordsForTest(std::move(records));

  nsTArray<uint8_t> token;
  mozilla::net::SessionCacheInfo info;
  ASSERT_EQ(mozilla::net::SSLTokensCache::Get("anon:a.example.com:443"_ns,
                                              token, info),
            NS_ERROR_NOT_AVAILABLE);
  ASSERT_EQ(mozilla::net::SSLTokensCache::CountForTest(), 0u);
  ASSERT_EQ(mozilla::net::SSLTokensCache::CacheSizeForTest(), 0u);
}
