/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Regression test for Bug 2031968: deadlock when DNS completion callbacks are
// invoked while nsHostResolver's mDBLock write lock is held.

#include "GetAddrInfo.h"
#include "gtest/gtest.h"
#include "mozilla/CondVar.h"
#include "mozilla/Maybe.h"
#include "mozilla/Mutex.h"
#include "mozilla/Variant.h"
#include "mozilla/gtest/MozAssertions.h"
#include "mozilla/net/DNSByTypeRecord.h"
#include "mozilla/net/DNSPacket.h"
#include "nsHostRecord.h"
#include "nsHostResolver.h"
#include "nsINativeDNSResolverOverride.h"
#include "nsServiceManagerUtils.h"
#include "prthread.h"

using namespace mozilla;
using namespace mozilla::net;

namespace {

// A callback that, on its first invocation, immediately calls ResolveHost
// again on the same resolver.
class ReentrantCallback final : public nsResolveHostCallback {
 public:
  NS_DECL_ISUPPORTS

  ReentrantCallback(nsHostResolver* aResolver, bool aShouldReenter,
                    Mutex& aMutex, CondVar& aCondVar, bool& aCompleted)
      : mResolver(aResolver),
        mShouldReenter(aShouldReenter),
        mMutex(aMutex),
        mCondVar(aCondVar),
        mCompleted(aCompleted) {}

  void OnResolveHostComplete(nsHostResolver* aResolver, nsHostRecord* aRecord,
                             nsresult aStatus, bool aFromStaleCache) override {
    if (mShouldReenter) {
      RefPtr<ReentrantCallback> inner = new ReentrantCallback(
          mResolver, /* aShouldReenter */ false, mMutex, mCondVar, mCompleted);
      mResolver->ResolveHost(
          "localhost"_ns, ""_ns, -1, nsIDNSService::RESOLVE_TYPE_DEFAULT,
          OriginAttributes(), nsIDNSService::RESOLVE_DEFAULT_FLAGS,
          PR_AF_UNSPEC, inner);
    }
    MutexAutoLock lock(mMutex);
    mCompleted = true;
    mCondVar.Notify();
  }

  bool EqualsAsyncListener(nsIDNSListener* aListener) override { return false; }

  size_t SizeOfIncludingThis(
      mozilla::MallocSizeOf aMallocSizeOf) const override {
    return aMallocSizeOf(this);
  }

 private:
  ~ReentrantCallback() = default;

  RefPtr<nsHostResolver> mResolver;
  const bool mShouldReenter;
  Mutex& mMutex;
  CondVar& mCondVar;
  bool& mCompleted;
};

NS_IMPL_ISUPPORTS0(ReentrantCallback)

struct WorkerArgs {
  RefPtr<nsHostResolver> resolver;
  Mutex* mutex;
  CondVar* condVar;
  bool* completed;
};

static void WorkerThread(void* aArg) {
  WorkerArgs* args = static_cast<WorkerArgs*>(aArg);
  RefPtr<ReentrantCallback> callback =
      new ReentrantCallback(args->resolver, /* aShouldReenter */ true,
                            *args->mutex, *args->condVar, *args->completed);
  args->resolver->ResolveHost(
      "localhost"_ns, ""_ns, -1, nsIDNSService::RESOLVE_TYPE_DEFAULT,
      OriginAttributes(), nsIDNSService::RESOLVE_DEFAULT_FLAGS, PR_AF_UNSPEC,
      callback);
}

}  // namespace

// Verify that a DNS completion callback can safely call back into ResolveHost.
TEST(TestDNS, ResolveHostCallbackCanReenterResolveHost)
{
  RefPtr<nsHostResolver> resolver;
  nsresult rv = nsHostResolver::Create(getter_AddRefs(resolver));
  ASSERT_NS_SUCCEEDED(rv);

  Mutex mutex MOZ_UNANNOTATED("TestDNS.mutex");
  CondVar condVar(mutex, "TestDNS.condVar");
  bool completed = false;

  WorkerArgs args{resolver, &mutex, &condVar, &completed};

  // Run on a worker thread so the main thread can apply a timeout.
  PRThread* thread =
      PR_CreateThread(PR_USER_THREAD, WorkerThread, &args, PR_PRIORITY_NORMAL,
                      PR_GLOBAL_THREAD, PR_JOINABLE_THREAD, 0);
  ASSERT_TRUE(thread);

  bool wasCompleted;
  {
    MutexAutoLock lock(mutex);
    // Wait up to 10 seconds for completion; a timeout indicates deadlock.
    condVar.Wait(TimeDuration::FromSeconds(10));
    wasCompleted = completed;
    EXPECT_TRUE(wasCompleted)
        << "Deadlock detected (Bug 2031968): OnResolveHostComplete was invoked "
           "while mDBLock write was held; the callback could not re-enter "
           "ResolveHost.";
  }

  // If deadlocked, the thread is abandoned rather than blocking forever.
  if (wasCompleted) {
    PR_JoinThread(thread);
    resolver->Shutdown();
  }
}

namespace {

void AppendU16(nsCString& aBuf, uint16_t aVal) {
  aBuf += static_cast<char>((aVal >> 8) & 0xff);
  aBuf += static_cast<char>(aVal & 0xff);
}

void AppendHTTPSHeader(nsCString& aBuf) {
  AppendU16(aBuf, 0);       // id
  AppendU16(aBuf, 0x8000);  // flags (QR set)
  AppendU16(aBuf, 0);       // qdcount
  AppendU16(aBuf, 1);       // ancount
  AppendU16(aBuf, 0);       // nscount
  AppendU16(aBuf, 0);       // arcount
}

void AppendHTTPSAnswerHeader(nsCString& aBuf, const nsACString& aName) {
  MOZ_ALWAYS_SUCCEEDS(DNSPacket::EncodeHost(aBuf, aName));
  AppendU16(aBuf, 65);  // TYPE HTTPS
  AppendU16(aBuf, 1);   // CLASS IN
  AppendU16(aBuf, 0);   // TTL high
  AppendU16(aBuf, 55);  // TTL low
}

// A single HTTPS AliasMode (SvcPriority 0) answer for |aOrigin| whose
// TargetName is |aTarget|.
nsCString BuildHTTPSAliasPacket(const nsACString& aOrigin,
                                const nsACString& aTarget) {
  nsCString buf;
  AppendHTTPSHeader(buf);
  AppendHTTPSAnswerHeader(buf, aOrigin);
  nsCString rdata;
  AppendU16(rdata, 0);  // SvcPriority 0 => AliasMode
  MOZ_ALWAYS_SUCCEEDS(DNSPacket::EncodeHost(rdata, aTarget));
  AppendU16(buf, rdata.Length());  // RDLENGTH
  buf.Append(rdata);
  return buf;
}

// A NOERROR response with an empty answer section (NODATA): the name exists
// but has no HTTPS record.
nsCString BuildHTTPSNoDataPacket() {
  nsCString buf;
  AppendU16(buf, 0);       // id
  AppendU16(buf, 0x8000);  // flags (QR set)
  AppendU16(buf, 0);       // qdcount
  AppendU16(buf, 0);       // ancount
  AppendU16(buf, 0);       // nscount
  AppendU16(buf, 0);       // arcount
  return buf;
}

// A single HTTPS ServiceMode (SvcPriority 1) answer for |aOrigin|.
nsCString BuildHTTPSServicePacket(const nsACString& aOrigin) {
  nsCString buf;
  AppendHTTPSHeader(buf);
  AppendHTTPSAnswerHeader(buf, aOrigin);
  nsCString rdata;
  AppendU16(rdata, 1);             // SvcPriority 1 => ServiceMode
  rdata += '\0';                   // TargetName "." (owner name)
  AppendU16(buf, rdata.Length());  // RDLENGTH
  buf.Append(rdata);
  return buf;
}

void AddHTTPSOverride(nsINativeDNSResolverOverride* aOverride,
                      const nsACString& aHost, const nsCString& aPacket) {
  aOverride->AddHTTPSRecordOverride(
      aHost, reinterpret_cast<const uint8_t*>(aPacket.BeginReading()),
      aPacket.Length());
}

}  // namespace

// An HTTPS AliasMode record whose TargetName differs from the owner name only
// by case is a self-reference and must not be followed. The decoder lowercases
// the TargetName, so detecting this requires a case-insensitive comparison
// (RFC 4343). Regression test for D312868 review feedback on bug 1869075.
TEST(TestDNS, HTTPSAliasSelfReferenceIsCaseInsensitive)
{
  nsCOMPtr<nsINativeDNSResolverOverride> override =
      do_GetService("@mozilla.org/network/native-dns-override;1");
  ASSERT_TRUE(override);

  constexpr auto kMixed = "Ci-Self.example"_ns;
  constexpr auto kLower = "ci-self.example"_ns;

  // The mixed-case origin aliases to itself; the decoder returns the lowercased
  // TargetName, so the alias equals the owner name only case-insensitively.
  AddHTTPSOverride(override, kMixed, BuildHTTPSAliasPacket(kMixed, kMixed));

  // A valid ServiceMode record cached under the lowercased name. If the
  // self-reference were not detected (case-sensitive compare), following the
  // alias would resolve to this record and wrongly report success.
  AddHTTPSOverride(override, kLower, BuildHTTPSServicePacket(kLower));

  TypeRecordResultType result = AsVariant(Nothing());
  uint32_t ttl = 0;
  nsresult rv = ResolveHTTPSRecord(kMixed, nsIDNSService::RESOLVE_DEFAULT_FLAGS,
                                   result, ttl);

  EXPECT_TRUE(NS_FAILED(rv))
      << "a case-only self-referencing HTTPS alias must not be followed";
  EXPECT_TRUE(result.is<TypeRecordEmpty>())
      << "no record should be surfaced for a self-referencing alias";

  override->ClearOverrides();
}

// RFC 9460: an AliasMode TargetName must be used as the connection target even
// when it has no HTTPS record of its own, so that Happy Eyeballs can resolve
// its A/AAAA records. Mirrors the ex2.example.com case from bug 2072300.
TEST(TestDNS, HTTPSAliasTargetWithoutHTTPSRecord)
{
  nsCOMPtr<nsINativeDNSResolverOverride> override =
      do_GetService("@mozilla.org/network/native-dns-override;1");
  ASSERT_TRUE(override);

  constexpr auto kOrigin = "blog.alias-nodata.example"_ns;
  constexpr auto kTarget = "alias-nodata.example"_ns;

  AddHTTPSOverride(override, kOrigin, BuildHTTPSAliasPacket(kOrigin, kTarget));
  AddHTTPSOverride(override, kTarget, BuildHTTPSNoDataPacket());

  TypeRecordResultType result = AsVariant(Nothing());
  uint32_t ttl = 0;
  nsresult rv = ResolveHTTPSRecord(
      kOrigin, nsIDNSService::RESOLVE_DEFAULT_FLAGS, result, ttl);

  EXPECT_NS_SUCCEEDED(rv);
  ASSERT_TRUE(result.is<TypeRecordHTTPSSVC>());
  const auto& records = result.as<TypeRecordHTTPSSVC>();
  ASSERT_EQ(records.Length(), 1u);
  EXPECT_EQ(records[0].mSvcFieldPriority, 0);
  EXPECT_TRUE(records[0].mSvcDomainName.Equals(kTarget));

  override->ClearOverrides();
}

// For a port-prefixed query, a ServiceMode TargetName of "." must resolve to
// the origin host rather than the prefixed name, like the TRR path does.
TEST(TestDNS, HTTPSPortPrefixedServiceModeUsesOriginHost)
{
  nsCOMPtr<nsINativeDNSResolverOverride> override =
      do_GetService("@mozilla.org/network/native-dns-override;1");
  ASSERT_TRUE(override);

  constexpr auto kPrefixed = "_8443._https.port-prefix.example"_ns;
  constexpr auto kOrigin = "port-prefix.example"_ns;

  AddHTTPSOverride(override, kPrefixed, BuildHTTPSServicePacket(kPrefixed));

  TypeRecordResultType result = AsVariant(Nothing());
  uint32_t ttl = 0;
  nsresult rv =
      ResolveHTTPSRecord(kPrefixed, nsIDNSService::RESOLVE_DEFAULT_FLAGS,
                         result, ttl, Some(nsCString(kOrigin)));

  EXPECT_NS_SUCCEEDED(rv);
  ASSERT_TRUE(result.is<TypeRecordHTTPSSVC>());
  const auto& records = result.as<TypeRecordHTTPSSVC>();
  ASSERT_EQ(records.Length(), 1u);
  EXPECT_TRUE(records[0].mSvcDomainName.Equals(kOrigin))
      << records[0].mSvcDomainName.get();

  override->ClearOverrides();
}

// Once an AliasMode record of a port-prefixed name is followed, a ServiceMode
// TargetName of "." refers to the alias target (RFC 9460 section 2.5.2), not
// to the origin host.
TEST(TestDNS, HTTPSPortPrefixedAliasTargetUsesOwnerName)
{
  nsCOMPtr<nsINativeDNSResolverOverride> override =
      do_GetService("@mozilla.org/network/native-dns-override;1");
  ASSERT_TRUE(override);

  constexpr auto kPrefixed = "_8443._https.port-alias.example"_ns;
  constexpr auto kOrigin = "port-alias.example"_ns;
  constexpr auto kTarget = "svc.cdn-port-alias.example"_ns;

  AddHTTPSOverride(override, kPrefixed,
                   BuildHTTPSAliasPacket(kPrefixed, kTarget));
  AddHTTPSOverride(override, kTarget, BuildHTTPSServicePacket(kTarget));

  TypeRecordResultType result = AsVariant(Nothing());
  uint32_t ttl = 0;
  nsresult rv =
      ResolveHTTPSRecord(kPrefixed, nsIDNSService::RESOLVE_DEFAULT_FLAGS,
                         result, ttl, Some(nsCString(kOrigin)));

  EXPECT_NS_SUCCEEDED(rv);
  ASSERT_TRUE(result.is<TypeRecordHTTPSSVC>());
  const auto& records = result.as<TypeRecordHTTPSSVC>();
  ASSERT_EQ(records.Length(), 1u);
  EXPECT_EQ(records[0].mSvcFieldPriority, 1);
  EXPECT_TRUE(records[0].mSvcDomainName.Equals(kTarget))
      << records[0].mSvcDomainName.get();

  override->ClearOverrides();
}
