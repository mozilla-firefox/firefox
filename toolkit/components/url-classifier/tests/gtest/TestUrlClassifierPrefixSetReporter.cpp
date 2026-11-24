/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Common.h"
#include "VariableLengthPrefixSet.h"
#include "mozilla/Atomics.h"
#include "mozilla/gtest/MozAssertions.h"
#include "mozilla/SpinEventLoopUntil.h"
#include "nsIMemoryReporter.h"
#include "nsNetCID.h"
#include "nsPrintfCString.h"
#include "nsServiceManagerUtils.h"

using namespace mozilla;
using namespace mozilla::safebrowsing;

static RefPtr<VariableLengthPrefixSet> CreatePrefixSet(
    const nsACString& aName) {
  auto pset = MakeRefPtr<VariableLengthPrefixSet>();
  nsresult rv = pset->Init(aName);
  EXPECT_NS_SUCCEEDED(rv);
  return pset;
}

class TestHandleReport final : public nsIHandleReportCallback {
 public:
  NS_DECL_ISUPPORTS

  TestHandleReport(nsTArray<nsCString>& aPaths, const nsACString& aPrefix)
      : mPaths(aPaths), mPrefix(aPrefix) {}

  NS_IMETHOD Callback(const nsACString& aProcess, const nsACString& aPath,
                      int32_t aKind, int32_t aUnits, int64_t aAmount,
                      const nsACString& aDescription,
                      nsISupports* aData) override {
    if (StringBeginsWith(aPath, mPrefix)) {
      mPaths.AppendElement(nsCString(aPath));
    }
    return NS_OK;
  }

 private:
  ~TestHandleReport() = default;
  nsTArray<nsCString>& mPaths;
  const nsCString mPrefix;
};

NS_IMPL_ISUPPORTS(TestHandleReport, nsIHandleReportCallback)

class TestFinishReporting final : public nsIFinishReportingCallback {
 public:
  NS_DECL_ISUPPORTS

  explicit TestFinishReporting(Atomic<bool>& aDone) : mDone(aDone) {}

  NS_IMETHOD Callback(nsISupports* aData) override {
    mDone = true;
    return NS_OK;
  }

 private:
  ~TestFinishReporting() = default;
  Atomic<bool>& mDone;
};

NS_IMPL_ISUPPORTS(TestFinishReporting, nsIFinishReportingCallback)

static nsTArray<nsCString> CollectReportPaths(const nsACString& aPathPrefix) {
  nsCOMPtr<nsIMemoryReporterManager> mgr =
      do_GetService("@mozilla.org/memory-reporter-manager;1");
  EXPECT_TRUE(mgr);

  nsTArray<nsCString> paths;
  Atomic<bool> done{false};

  auto handleReport = MakeRefPtr<TestHandleReport>(paths, aPathPrefix);
  auto finishReporting = MakeRefPtr<TestFinishReporting>(done);

  mgr->GetReports(handleReport, nullptr, finishReporting, nullptr, false);

  SpinEventLoopUntil("TestPrefixSetMemoryReporter"_ns,
                     [&]() { return done.load(); });

  return paths;
}

// Verify the singleton reporter registers and reports correctly.
TEST(UrlClassifierPrefixSetReporter, BasicLifecycle)
{
  {
    // Keep prefix sets alive while we verify they appear in memory reports.
    RefPtr<VariableLengthPrefixSet> pset1 = CreatePrefixSet("test-table-a"_ns);
    RefPtr<VariableLengthPrefixSet> pset2 = CreatePrefixSet("test-table-b"_ns);
    (void)pset1;
    (void)pset2;

    nsTArray<nsCString> paths =
        CollectReportPaths("explicit/storage/prefix-set/"_ns);
    bool foundA = false, foundB = false;
    for (const auto& path : paths) {
      if (path.EqualsLiteral("explicit/storage/prefix-set/test-table-a")) {
        foundA = true;
      }
      if (path.EqualsLiteral("explicit/storage/prefix-set/test-table-b")) {
        foundB = true;
      }
    }
    EXPECT_TRUE(foundA);
    EXPECT_TRUE(foundB);
  }

  // After both prefix sets are destroyed, their reports should be gone.
  nsTArray<nsCString> paths =
      CollectReportPaths("explicit/storage/prefix-set/test-table-"_ns);
  EXPECT_TRUE(paths.IsEmpty());
}

// Destroy VariableLengthPrefixSet instances on a background thread while
// collecting memory reports on the main thread. This exercises the race
// that bug 2049342 fixed.
TEST(UrlClassifierPrefixSetReporter, ConcurrentDestroyAndReport)
{
  static const uint32_t kNumSets = 20;
  static const uint32_t kReportRounds = 10;

  nsTArray<RefPtr<VariableLengthPrefixSet>> sets;
  for (uint32_t i = 0; i < kNumSets; i++) {
    nsPrintfCString name("concurrent-test-%u", i);
    sets.AppendElement(CreatePrefixSet(name));
  }

  // Verify all are reported before we start destroying.
  nsTArray<nsCString> paths =
      CollectReportPaths("explicit/storage/prefix-set/concurrent-test-"_ns);
  EXPECT_EQ(paths.Length(), kNumSets);

  // Move sets to a background thread that will release them one at a time,
  // while the main thread repeatedly collects memory reports.
  Atomic<uint32_t> destroyed{0};
  nsTArray<RefPtr<VariableLengthPrefixSet>> bgSets = std::move(sets);

  nsCOMPtr<nsIRunnable> destroyTask =
      NS_NewRunnableFunction("DestroyPrefixSets", [&bgSets, &destroyed]() {
        for (uint32_t i = 0; i < bgSets.Length(); i++) {
          bgSets[i] = nullptr;
          destroyed++;
        }
      });
  nsCOMPtr<nsIEventTarget> pool =
      do_GetService(NS_STREAMTRANSPORTSERVICE_CONTRACTID);
  ASSERT_TRUE(pool);
  nsresult rv = pool->Dispatch(destroyTask, NS_DISPATCH_NORMAL);
  ASSERT_NS_SUCCEEDED(rv);

  // Main thread collects reports repeatedly while destruction proceeds.
  for (uint32_t round = 0; round < kReportRounds; round++) {
    CollectReportPaths("explicit/storage/prefix-set/concurrent-test-"_ns);
  }

  SpinEventLoopUntil("WaitForDestroy"_ns,
                     [&]() { return destroyed == kNumSets; });
  EXPECT_EQ(destroyed.load(), kNumSets);

  // All should be gone now.
  paths = CollectReportPaths("explicit/storage/prefix-set/concurrent-test-"_ns);
  EXPECT_TRUE(paths.IsEmpty());
}

// Verify that creating and immediately destroying a VariableLengthPrefixSet
// (the Init-then-fail path in Classifier::GetLookupCache) doesn't leave
// stale entries in the reporter.
TEST(UrlClassifierPrefixSetReporter, CreateAndImmediatelyDestroy)
{
  for (uint32_t i = 0; i < 50; i++) {
    MOZ_ALWAYS_TRUE(CreatePrefixSet("ephemeral-test"_ns));
  }

  nsTArray<nsCString> paths =
      CollectReportPaths("explicit/storage/prefix-set/ephemeral-test"_ns);
  EXPECT_TRUE(paths.IsEmpty());
}
