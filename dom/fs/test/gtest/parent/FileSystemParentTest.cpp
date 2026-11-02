/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FileSystemParentTest.h"

#include "FileSystemParentTestHelpers.h"
#include "FileSystemParentTypes.h"
#include "TestHelpers.h"
#include "datamodel/FileSystemDataManager.h"
#include "datamodel/FileSystemDatabaseManager.h"
#include "gtest/gtest.h"
#include "mozilla/dom/PFileSystemManager.h"
#include "nsIFile.h"

namespace mozilla::dom::fs::test {

FileSystemParentTest::FileSystemParentTest() = default;

FileSystemParentTest::~FileSystemParentTest() = default;

// static
void FileSystemParentTest::SetUpTestCase() {
  ASSERT_NO_FATAL_FAILURE(InitializeFixture());
}

// static
void FileSystemParentTest::TearDownTestCase() {
  ASSERT_NO_FATAL_FAILURE(ShutdownFixture());
}

void FileSystemParentTest::TearDown() {
  ReleaseDataManager();

  ASSERT_NO_FATAL_FAILURE(ClearStoragesForOrigin(GetTestOriginMetadata()));
}

// static
void FileSystemParentTest::InitializeTemporaryOrigin(
    bool aCreateIfNonExistent) {
  ASSERT_NO_FATAL_FAILURE(
      QuotaManagerDependencyFixture::InitializeTemporaryOrigin(
          GetTestOriginMetadata(), aCreateIfNonExistent));
}

//  static
void FileSystemParentTest::GetOriginUsage(quota::UsageInfo& aResult) {
  ASSERT_NO_FATAL_FAILURE(QuotaManagerDependencyFixture::GetOriginUsage(
      GetTestOriginMetadata(), &aResult));
}

//  static
void FileSystemParentTest::GetCachedOriginUsage(quota::UsageInfo& aResult) {
  ASSERT_NO_FATAL_FAILURE(QuotaManagerDependencyFixture::GetCachedOriginUsage(
      GetTestOriginMetadata(), &aResult));
}

// static
void FileSystemParentTest::InitializeTemporaryClient() {
  ASSERT_NO_FATAL_FAILURE(
      QuotaManagerDependencyFixture::InitializeTemporaryClient(
          GetTestClientMetadata(), /* aCreateIfNonExistent */ true));
}

// static
void FileSystemParentTest::GetStaticDatabaseUsage(
    quota::UsageInfo& aDatabaseUsage) {
  quota::QuotaManager* quotaManager = quota::QuotaManager::Get();
  ASSERT_TRUE(quotaManager);

  TEST_TRY_UNWRAP(
      auto databaseUsage,
      PerformOnThread(
          quotaManager->IOThread(), []() -> Result<quota::UsageInfo, QMResult> {
            QM_TRY_INSPECT(
                const ResultConnection& conn,
                data::GetStorageConnection(GetTestOriginMetadata(),
                                           /* aDirectoryLockId */ -1,
                                           /* aMaybeCipherKey */ Nothing()));

            return data::FileSystemDatabaseManager::GetUsage(
                conn, GetTestOriginMetadata());
          }));

  aDatabaseUsage = std::move(databaseUsage);
}

void FileSystemParentTest::EnsureDataManager() {
  PerformOnBackgroundThread([this]() {
    ASSERT_NO_FATAL_FAILURE(test::CreateRegisteredDataManager(
        GetTestOriginMetadata(), mDataManager));
  });
}

void FileSystemParentTest::ReleaseDataManager() {
  PerformOnBackgroundThread([this]() { mDataManager = nullptr; });
}

void FileSystemParentTest::LockExclusive(const EntryId& aEntryId) {
  ASSERT_TRUE(mDataManager);

  TEST_TRY_UNWRAP(FileId fileId, PerformOnBackgroundThread([this, &aEntryId]() {
                    return mDataManager->LockExclusive(aEntryId);
                  }));
}

void FileSystemParentTest::UnlockExclusive(const EntryId& aEntryId) {
  ASSERT_TRUE(mDataManager);

  PerformOnBackgroundThread(
      [this, &aEntryId]() { mDataManager->UnlockExclusive(aEntryId); });
}

void FileSystemParentTest::LockShared(const EntryId& aEntryId,
                                      FileId& aFileId) {
  ASSERT_TRUE(mDataManager);

  TEST_TRY_UNWRAP(FileId fileId, PerformOnBackgroundThread([this, &aEntryId]() {
                    return mDataManager->LockShared(aEntryId);
                  }));

  aFileId = fileId;
}

void FileSystemParentTest::GetFileObject(const EntryId& aEntryId,
                                         const FileId& aFileId,
                                         nsCOMPtr<nsIFile>& aFile) {
  ASSERT_TRUE(mDataManager);

  TEST_TRY(PerformOnThread(
      mDataManager->MutableIOTaskQueuePtr(),
      [this, &aEntryId, &aFileId, &aFile]() -> Result<Ok, QMResult> {
        ContentType type;
        TimeStamp lastModifiedMs = 0;
        Path path;
        QM_TRY(QM_TO_RESULT(mDataManager->MutableDatabaseManagerPtr()->GetFile(
            aEntryId, aFileId, FileMode::SHARED_FROM_EMPTY, type,
            lastModifiedMs, path, aFile)));

        return Ok{};
      }));
}

void FileSystemParentTest::CreateNewEmptyFile(EntryId& aEntryId) {
  ASSERT_TRUE(mDataManager);

  TEST_TRY_UNWRAP(
      EntryId testFileId,
      PerformOnThread(
          mDataManager->MutableIOTaskQueuePtr(),
          [this]() -> Result<EntryId, QMResult> {
            data::FileSystemDatabaseManager* databaseManager =
                mDataManager->MutableDatabaseManagerPtr();

            QM_TRY_UNWRAP(const EntryId rootId,
                          data::GetRootHandle(GetTestOrigin()));
            FileSystemChildMetadata fileData(rootId, GetTestFileName());

            EntryId testFileId;
            ENSURE_NO_FATAL_FAILURE(
                test::CreateNewEmptyFile(databaseManager, fileData, testFileId),
                Err(QMResult(NS_ERROR_FAILURE)));

            return testFileId;
          }));

  aEntryId = testFileId;
}

void FileSystemParentTest::WriteDataToFile(EntryId& aEntryId,
                                           const nsCString& aData) {
  ASSERT_TRUE(mDataManager);

  TEST_TRY(PerformOnThread(mDataManager->MutableIOTaskQueuePtr(),
                           [this, &aEntryId, &aData]() -> Result<Ok, QMResult> {
                             data::FileSystemDatabaseManager* databaseManager =
                                 mDataManager->MutableDatabaseManagerPtr();

                             ENSURE_NO_FATAL_FAILURE(
                                 test::WriteDataToFile(GetTestOriginMetadata(),
                                                       databaseManager,
                                                       aEntryId, aData),
                                 Err(QMResult(NS_ERROR_FAILURE)));

                             return Ok{};
                           }));
}

void FileSystemParentTest::RemoveFile(bool& aWasRemoved) {
  ASSERT_TRUE(mDataManager);

  TEST_TRY_UNWRAP(
      bool wasRemoved,
      PerformOnThread(mDataManager->MutableIOTaskQueuePtr(),
                      [this]() -> Result<bool, QMResult> {
                        data::FileSystemDatabaseManager* databaseManager =
                            mDataManager->MutableDatabaseManagerPtr();

                        QM_TRY_UNWRAP(const EntryId rootId,
                                      data::GetRootHandle(GetTestOrigin()));

                        QM_TRY_RETURN(databaseManager->RemoveFile(
                            {rootId, GetTestFileName()}));
                      }));

  aWasRemoved = wasRemoved;
}

void FileSystemParentTest::GetDatabaseUsage(quota::UsageInfo& aDatabaseUsage) {
  ASSERT_TRUE(mDataManager);

  TEST_TRY_UNWRAP(
      auto databaseUsage,
      PerformOnThread(mDataManager->MutableIOTaskQueuePtr(), [this]() {
        data::FileSystemDatabaseManager* databaseManager =
            mDataManager->MutableDatabaseManagerPtr();

        QM_TRY_RETURN(databaseManager->GetUsage());
      }));

  aDatabaseUsage = std::move(databaseUsage);
}

void FileSystemParentTest::UpdateDatabaseUsage(const FileId& aFileId) {
  ASSERT_TRUE(mDataManager);

  TEST_TRY(PerformOnThread(
      mDataManager->MutableIOTaskQueuePtr(), [this, &aFileId]() {
        data::FileSystemDatabaseManager* databaseManager =
            mDataManager->MutableDatabaseManagerPtr();

        QM_TRY_RETURN(MOZ_TO_RESULT(databaseManager->UpdateUsage(aFileId)));
      }));
}

}  // namespace mozilla::dom::fs::test
