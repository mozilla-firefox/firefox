/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FileSystemParentTest.h"
#include "FileSystemParentTestHelpers.h"
#include "FileSystemParentTypes.h"
#include "TestHelpers.h"
#include "gtest/gtest.h"
#include "nsIFile.h"

namespace mozilla::dom::fs::test {

class TestFileSystemOrphanCleanup : public FileSystemParentTest {};

// A writable stream's temporary file survives a crash because nothing unlocks
// it. Opening the database again must remove it since nothing references it.
TEST_F(TestFileSystemOrphanCleanup, TemporaryFileIsRemovedOnReopen) {
  ASSERT_NO_FATAL_FAILURE(EnsureDataManager());

  EntryId entryId;
  ASSERT_NO_FATAL_FAILURE(CreateNewEmptyFile(entryId));

  ASSERT_NO_FATAL_FAILURE(LockExclusive(entryId));
  ASSERT_NO_FATAL_FAILURE(WriteDataToFile(entryId, GetTestData()));
  ASSERT_NO_FATAL_FAILURE(UnlockExclusive(entryId));

  FileId temporaryFileId(""_ns);
  ASSERT_NO_FATAL_FAILURE(LockShared(entryId, temporaryFileId));

  nsCOMPtr<nsIFile> temporaryFile;
  ASSERT_NO_FATAL_FAILURE(
      GetFileObject(entryId, temporaryFileId, temporaryFile));

  bool exists = false;
  ASSERT_NSEQ(NS_OK, temporaryFile->Exists(&exists));
  ASSERT_TRUE(exists);

  // Simulate a crash: the shared lock is never released.
  ASSERT_NO_FATAL_FAILURE(ReleaseDataManager());

  ASSERT_NO_FATAL_FAILURE(EnsureDataManager());

  ASSERT_NSEQ(NS_OK, temporaryFile->Exists(&exists));
  ASSERT_FALSE(exists);

  // The main file is untouched.
  bool wasRemoved = false;
  ASSERT_NO_FATAL_FAILURE(RemoveFile(wasRemoved));
  ASSERT_TRUE(wasRemoved);
}

}  // namespace mozilla::dom::fs::test
