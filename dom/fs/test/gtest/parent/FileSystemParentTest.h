/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_FS_TEST_GTEST_PARENT_FILESYSTEMPARENTTEST_H_
#define DOM_FS_TEST_GTEST_PARENT_FILESYSTEMPARENTTEST_H_

#include "mozilla/dom/FileSystemHelpers.h"
#include "mozilla/dom/FileSystemTypes.h"
#include "mozilla/dom/quota/test/QuotaManagerDependencyFixture.h"
#include "nsCOMPtr.h"
#include "nsStringFwd.h"

class nsIFile;

namespace mozilla::dom {

namespace quota {

class UsageInfo;

}  // namespace quota

namespace fs {

struct FileId;

namespace data {

class FileSystemDataManager;

}  // namespace data
}  // namespace fs
}  // namespace mozilla::dom

namespace mozilla::dom::fs::test {

class FileSystemParentTest : public quota::test::QuotaManagerDependencyFixture {
 protected:
  FileSystemParentTest();

  ~FileSystemParentTest();

  static void SetUpTestCase();

  static void TearDownTestCase();

  void TearDown() override;

  static void InitializeTemporaryOrigin(bool aCreateIfNonExistent = true);

  static void GetOriginUsage(quota::UsageInfo& aResult);

  static void GetCachedOriginUsage(quota::UsageInfo& aResult);

  static void InitializeTemporaryClient();

  static void GetStaticDatabaseUsage(quota::UsageInfo& aDatabaseUsage);

  void EnsureDataManager();

  void ReleaseDataManager();

  void LockExclusive(const EntryId& aEntryId);

  void UnlockExclusive(const EntryId& aEntryId);

  void LockShared(const EntryId& aEntryId, FileId& aFileId);

  void CreateNewEmptyFile(EntryId& aEntryId);

  void GetFileObject(const EntryId& aEntryId, const FileId& aFileId,
                     nsCOMPtr<nsIFile>& aFile);

  void WriteDataToFile(EntryId& aEntryId, const nsCString& aData);

  void RemoveFile(bool& aWasRemoved);

  void GetDatabaseUsage(quota::UsageInfo& aDatabaseUsage);

  void UpdateDatabaseUsage(const FileId& aFileId);

 private:
  Registered<data::FileSystemDataManager> mDataManager;
};

}  // namespace mozilla::dom::fs::test

#endif  // DOM_FS_TEST_GTEST_PARENT_FILESYSTEMPARENTTEST_H_
