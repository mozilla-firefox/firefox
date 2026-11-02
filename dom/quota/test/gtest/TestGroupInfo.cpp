/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <memory>  // For std::make_unique

#include "GroupInfo.h"
#include "GroupInfoPair.h"
#include "OriginInfo.h"
#include "QuotaManagerDependencyFixture.h"
#include "mozilla/dom/quota/Client.h"
#include "mozilla/dom/quota/QuotaManager.h"

namespace mozilla::dom::quota::test {

class TestGroupInfo : public QuotaManagerDependencyFixture {
 public:
  static void SetUpTestCase() { ASSERT_NO_FATAL_FAILURE(InitializeFixture()); }

  static void TearDownTestCase() { ASSERT_NO_FATAL_FAILURE(ShutdownFixture()); }
};

// Verify that LockedAddOriginInfo nulls mGroupInfo on a replaced OriginInfo
// and adjusts usage counters correctly.
TEST_F(TestGroupInfo, ReplacementNullsMGroupInfo) {
  ASSERT_NO_FATAL_FAILURE(InitializeStorage());
  ASSERT_NO_FATAL_FAILURE(InitializeTemporaryStorage());

  PerformOnIOThread([]() {
    QuotaManager* quotaManager = QuotaManager::Get();
    ASSERT_TRUE(quotaManager);

    MutexAutoLock lock(quotaManager->mQuotaMutex);

    const int64_t savedTemporaryStorageUsage =
        quotaManager->mTemporaryStorageUsage;

    auto groupInfoPair =
        std::make_unique<GroupInfoPair>(""_ns, "example.com"_ns);
    auto groupInfo =
        MakeRefPtr<GroupInfo>(groupInfoPair.get(), PERSISTENCE_TYPE_DEFAULT);

    ClientUsageArray clientUsagesA;
    clientUsagesA[Client::IDB] = Some(int64_t(1000));

    auto originInfoA = MakeRefPtr<OriginInfo>(
        groupInfo, "http://example.com"_ns, "http://example.com"_ns,
        /* aIsPrivate */ false, /* aIsEncrypted */ false, clientUsagesA,
        /* aUsage */ 1000, /* aAccessTime */ 0, /* aMaintenanceDate */ 0,
        /* aPersisted */ false, /* aDirectoryExists */ true);

    RefPtr<OriginInfo> keptRefA = originInfoA;

    groupInfo->LockedAddOriginInfo(
        WrapNotNullUnchecked(std::move(originInfoA)));

    ASSERT_NE(keptRefA->GetGroupInfo(), nullptr);
    ASSERT_EQ(groupInfo->mUsage, 1000);

    ClientUsageArray clientUsagesB;
    clientUsagesB[Client::IDB] = Some(int64_t(2000));

    auto originInfoB = MakeRefPtr<OriginInfo>(
        groupInfo, "http://example.com"_ns, "http://example.com"_ns,
        /* aIsPrivate */ false, /* aIsEncrypted */ false, clientUsagesB,
        /* aUsage */ 2000, /* aAccessTime */ 0, /* aMaintenanceDate */ 0,
        /* aPersisted */ false, /* aDirectoryExists */ true);

    RefPtr<OriginInfo> keptRefB = originInfoB;

    groupInfo->LockedAddOriginInfo(
        WrapNotNullUnchecked(std::move(originInfoB)));

    EXPECT_EQ(keptRefA->GetGroupInfo(), nullptr);
    EXPECT_EQ(keptRefB->GetGroupInfo(), groupInfo.get());
    EXPECT_EQ(groupInfo->mUsage, 2000);
    EXPECT_EQ(groupInfo->mOriginInfos.Length(), 1u);

    // Clean up: remove surviving OriginInfos (nulls their mGroupInfo) and
    // restore the global counter.
    groupInfo->LockedRemoveOriginInfos();
    quotaManager->mTemporaryStorageUsage = savedTemporaryStorageUsage;
  });

  ASSERT_NO_FATAL_FAILURE(ShutdownStorage());
}

}  // namespace mozilla::dom::quota::test
