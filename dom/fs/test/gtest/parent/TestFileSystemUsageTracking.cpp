/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FileSystemParentTest.h"
#include "FileSystemParentTestHelpers.h"
#include "FileSystemParentTypes.h"
#include "gtest/gtest.h"
#include "mozilla/StaticPrefs_security.h"
#include "mozilla/dom/quota/UsageInfo.h"

// This file is intended for integration tests which verify usage tracking
// without any restart in between.

namespace mozilla::dom::fs::test {

class TestFileSystemUsageTracking : public FileSystemParentTest {};

TEST_F(TestFileSystemUsageTracking, CheckUsageBeforeAnyFilesOnDisk) {
  // For uninitialized database, origin usage is nothing
  quota::UsageInfo usageNow;
  ASSERT_NO_FATAL_FAILURE(GetOriginUsage(usageNow));
  ASSERT_NO_FATAL_FAILURE(CheckUsageIsNothing(usageNow));

  // Initialize database
  ASSERT_NO_FATAL_FAILURE(EnsureDataManager());

  // After initialization,
  // * database usage is not zero
  // * GetDatabaseUsage and GetOriginUsage should agree
  ASSERT_NO_FATAL_FAILURE(GetDatabaseUsage(usageNow));
  ASSERT_NO_FATAL_FAILURE(CheckUsageGreaterThan(usageNow, 0u));

  uint64_t initialDbUsage;
  ASSERT_NO_FATAL_FAILURE(GetUsageValue(usageNow, initialDbUsage));

  ASSERT_NO_FATAL_FAILURE(GetOriginUsage(usageNow));
  ASSERT_NO_FATAL_FAILURE(CheckUsageEqualTo(usageNow, initialDbUsage));

  // Create a new empty file
  EntryId testFileId;
  ASSERT_NO_FATAL_FAILURE(CreateNewEmptyFile(testFileId));

  // After a new file has been created (only in the database),
  // * database usage has increased
  // * GetDatabaseUsage and GetOriginUsage should agree
  const auto increasedDbUsage = initialDbUsage + 2 * GetPageSize();

  ASSERT_NO_FATAL_FAILURE(GetDatabaseUsage(usageNow));
  ASSERT_NO_FATAL_FAILURE(CheckUsageEqualTo(usageNow, increasedDbUsage));

  ASSERT_NO_FATAL_FAILURE(GetOriginUsage(usageNow));
  ASSERT_NO_FATAL_FAILURE(CheckUsageEqualTo(usageNow, increasedDbUsage));
}

TEST_F(TestFileSystemUsageTracking, WritesToFilesShouldIncreaseUsage) {
  // Initialize database
  ASSERT_NO_FATAL_FAILURE(EnsureDataManager());

  // Create a new empty file
  EntryId testFileId;
  ASSERT_NO_FATAL_FAILURE(CreateNewEmptyFile(testFileId));

  quota::UsageInfo usageNow;
  ASSERT_NO_FATAL_FAILURE(GetDatabaseUsage(usageNow));
  ASSERT_NO_FATAL_FAILURE(CheckUsageGreaterThan(usageNow, 0u));

  uint64_t initialDbUsage;
  ASSERT_NO_FATAL_FAILURE(GetUsageValue(usageNow, initialDbUsage));

  ASSERT_NO_FATAL_FAILURE(GetOriginUsage(usageNow));
  ASSERT_NO_FATAL_FAILURE(CheckUsageEqualTo(usageNow, initialDbUsage));

  // Fill the file with some content
  ASSERT_NO_FATAL_FAILURE(LockExclusive(testFileId));

  const nsCString& testData = GetTestData();

  ASSERT_NO_FATAL_FAILURE(WriteDataToFile(testFileId, testData));

  // After the content has been written to the file,
  // * database usage is the same (the usage is updated later during file
  //   unlocking)
  // * origin usage has increased
  ASSERT_NO_FATAL_FAILURE(GetDatabaseUsage(usageNow));
  ASSERT_NO_FATAL_FAILURE(CheckUsageEqualTo(usageNow, initialDbUsage));

  const auto increasedDbUsage = initialDbUsage + testData.Length();

  ASSERT_NO_FATAL_FAILURE(GetOriginUsage(usageNow));
  ASSERT_NO_FATAL_FAILURE(CheckUsageEqualTo(usageNow, increasedDbUsage));

  ASSERT_NO_FATAL_FAILURE(UnlockExclusive(testFileId));

  // After the file has been unlocked,
  // * database usage has increased
  // * GetDatabaseUsage and GetOriginUsage should now agree
  ASSERT_NO_FATAL_FAILURE(GetDatabaseUsage(usageNow));
  ASSERT_NO_FATAL_FAILURE(CheckUsageEqualTo(usageNow, increasedDbUsage));

  ASSERT_NO_FATAL_FAILURE(GetOriginUsage(usageNow));
  ASSERT_NO_FATAL_FAILURE(CheckUsageEqualTo(usageNow, increasedDbUsage));
}

TEST_F(TestFileSystemUsageTracking, RemovingFileShouldDecreaseUsage) {
  if (StaticPrefs::security_storage_encryption_sqlite_enabled()) {
    // obfsvfs forces an 8192-byte page size with 32 reserved bytes per page,
    // so the on-disk database usage no longer matches the byte counts this
    // test computes assuming the unencrypted page layout.
    GTEST_SKIP()
        << "QM usage accounting differs under SQLite at-rest encryption";
  }
  // Initialize database
  ASSERT_NO_FATAL_FAILURE(EnsureDataManager());

  // Create a new empty file
  EntryId testFileId;
  ASSERT_NO_FATAL_FAILURE(CreateNewEmptyFile(testFileId));

  quota::UsageInfo usageNow;
  ASSERT_NO_FATAL_FAILURE(GetDatabaseUsage(usageNow));
  ASSERT_NO_FATAL_FAILURE(CheckUsageGreaterThan(usageNow, 0u));

  uint64_t initialDbUsage;
  ASSERT_NO_FATAL_FAILURE(GetUsageValue(usageNow, initialDbUsage));

  ASSERT_NO_FATAL_FAILURE(GetOriginUsage(usageNow));
  ASSERT_NO_FATAL_FAILURE(CheckUsageEqualTo(usageNow, initialDbUsage));

  // Fill the file with some content
  ASSERT_NO_FATAL_FAILURE(LockExclusive(testFileId));

  const nsCString& testData = GetTestData();

  ASSERT_NO_FATAL_FAILURE(WriteDataToFile(testFileId, testData));

  ASSERT_NO_FATAL_FAILURE(UnlockExclusive(testFileId));

  // After the file has been unlocked,
  // * database usage has increased
  // * GetDatabaseUsage and GetOriginUsage should now agree
  const auto increasedDbUsage = initialDbUsage + testData.Length();

  ASSERT_NO_FATAL_FAILURE(GetDatabaseUsage(usageNow));
  ASSERT_NO_FATAL_FAILURE(CheckUsageEqualTo(usageNow, increasedDbUsage));

  ASSERT_NO_FATAL_FAILURE(GetOriginUsage(usageNow));
  ASSERT_NO_FATAL_FAILURE(CheckUsageEqualTo(usageNow, increasedDbUsage));

  // Remove the file
  bool wasRemoved;
  ASSERT_NO_FATAL_FAILURE(RemoveFile(wasRemoved));
  ASSERT_TRUE(wasRemoved);

  // After the file has been removed,
  // * database usage has decreased (to the initial value)
  // * GetDatabaseUsage and GetOriginUsage should agree
  ASSERT_NO_FATAL_FAILURE(GetDatabaseUsage(usageNow));
  ASSERT_NO_FATAL_FAILURE(CheckUsageEqualTo(usageNow, initialDbUsage));

  ASSERT_NO_FATAL_FAILURE(GetOriginUsage(usageNow));
  ASSERT_NO_FATAL_FAILURE(CheckUsageEqualTo(usageNow, initialDbUsage));
}

TEST_F(TestFileSystemUsageTracking, LockAfterUncleanCloseShouldRecoverUsage) {
  ASSERT_NO_FATAL_FAILURE(EnsureDataManager());

  EntryId testFileId;
  ASSERT_NO_FATAL_FAILURE(CreateNewEmptyFile(testFileId));

  quota::UsageInfo usageNow;
  ASSERT_NO_FATAL_FAILURE(GetDatabaseUsage(usageNow));

  uint64_t initialDbUsage;
  ASSERT_NO_FATAL_FAILURE(GetUsageValue(usageNow, initialDbUsage));

  ASSERT_NO_FATAL_FAILURE(LockExclusive(testFileId));

  const nsCString& testData = GetTestData();

  ASSERT_NO_FATAL_FAILURE(WriteDataToFile(testFileId, testData));

  // Releasing the data manager while the file is locked leaves its usage
  // tracked, as when the browser is killed with a sync access handle open, and
  // no origin initialization rescans it before the next lock.
  ASSERT_NO_FATAL_FAILURE(ReleaseDataManager());
  ASSERT_NO_FATAL_FAILURE(EnsureDataManager());

  ASSERT_NO_FATAL_FAILURE(GetDatabaseUsage(usageNow));
  ASSERT_NO_FATAL_FAILURE(CheckUsageEqualTo(usageNow, initialDbUsage));

  ASSERT_NO_FATAL_FAILURE(LockExclusive(testFileId));

  ASSERT_NO_FATAL_FAILURE(GetDatabaseUsage(usageNow));
  ASSERT_NO_FATAL_FAILURE(
      CheckUsageEqualTo(usageNow, initialDbUsage + testData.Length()));

  ASSERT_NO_FATAL_FAILURE(UnlockExclusive(testFileId));
}

}  // namespace mozilla::dom::fs::test
