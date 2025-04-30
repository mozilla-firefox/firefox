/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/dom/quota/Client.h"
#include "mozilla/dom/quota/ClientDirectoryLock.h"
#include "mozilla/dom/quota/ClientDirectoryLockHandle.h"
#include "mozilla/dom/quota/CommonMetadata.h"
#include "mozilla/dom/quota/FileStreams.h"
#include "mozilla/dom/quota/QuotaManager.h"
#include "mozilla/gtest/MozAssertions.h"
#include "nsIPrefBranch.h"
#include "nsIPrefService.h"
#include "QuotaManagerDependencyFixture.h"

namespace mozilla::dom::quota::test {

quota::OriginMetadata GetOutputStreamTestOriginMetadata() {
  return quota::OriginMetadata{""_ns,
                               "example.com"_ns,
                               "http://example.com"_ns,
                               "http://example.com"_ns,
                               /* aIsPrivate */ false,
                               quota::PERSISTENCE_TYPE_DEFAULT};
}

class TestFileOutputStream : public QuotaManagerDependencyFixture {
 public:
  static void SetUpTestCase() {
    ASSERT_NO_FATAL_FAILURE(InitializeFixture());

    nsCOMPtr<nsIPrefBranch> prefs = do_GetService(NS_PREFSERVICE_CONTRACTID);

    prefs->SetIntPref("dom.quotaManager.temporaryStorage.fixedLimit",
                      mQuotaLimit);
  }

  static void TearDownTestCase() {
    nsCOMPtr<nsIPrefBranch> prefs = do_GetService(NS_PREFSERVICE_CONTRACTID);

    prefs->ClearUserPref("dom.quotaManager.temporaryStorage.fixedLimit");

    EXPECT_NO_FATAL_FAILURE(
        ClearStoragesForOrigin(GetOutputStreamTestOriginMetadata()));

    ASSERT_NO_FATAL_FAILURE(ShutdownFixture());
  }

  static const int32_t mQuotaLimit = 8192;
};

TEST_F(TestFileOutputStream, extendFileStreamWithSetEOF) {
  auto backgroundTask = []() {
    auto ioTask = []() {
      quota::QuotaManager* quotaManager = quota::QuotaManager::Get();

      auto originMetadata = GetOutputStreamTestOriginMetadata();

      const int64_t groupLimit =
          static_cast<int64_t>(quotaManager->GetGroupLimit());
      ASSERT_TRUE(mQuotaLimit * 1024LL == groupLimit);

      // We don't use the tested stream itself to check the file size as it
      // may report values which have not been written to disk.
      RefPtr<quota::FileOutputStream> check =
          MakeRefPtr<quota::FileOutputStream>(quota::PERSISTENCE_TYPE_DEFAULT,
                                              originMetadata,
                                              quota::Client::Type::SDB);

      RefPtr<quota::FileOutputStream> stream =
          MakeRefPtr<quota::FileOutputStream>(quota::PERSISTENCE_TYPE_DEFAULT,
                                              originMetadata,
                                              quota::Client::Type::SDB);

      {
        auto testPathRes =
            quotaManager->GetOrCreateTemporaryOriginDirectory(originMetadata);

        ASSERT_TRUE(testPathRes.isOk());

        nsCOMPtr<nsIFile> testPath = testPathRes.unwrap();

        ASSERT_NS_SUCCEEDED(testPath->AppendRelativePath(u"sdb"_ns));

        ASSERT_NS_SUCCEEDED(
            testPath->AppendRelativePath(u"tTestFileOutputStream.txt"_ns));

        bool exists = true;
        ASSERT_NS_SUCCEEDED(testPath->Exists(&exists));

        if (exists) {
          ASSERT_NS_SUCCEEDED(testPath->Remove(/* recursive */ false));
        }

        ASSERT_NS_SUCCEEDED(testPath->Exists(&exists));
        ASSERT_FALSE(exists);

        ASSERT_NS_SUCCEEDED(testPath->Create(nsIFile::NORMAL_FILE_TYPE, 0666));

        ASSERT_NS_SUCCEEDED(testPath->Exists(&exists));
        ASSERT_TRUE(exists);

        nsCOMPtr<nsIFile> checkPath;
        ASSERT_NS_SUCCEEDED(testPath->Clone(getter_AddRefs(checkPath)));

        const int32_t IOFlags = -1;
        const int32_t perm = -1;
        const int32_t behaviorFlags = 0;
        ASSERT_NS_SUCCEEDED(
            stream->Init(testPath, IOFlags, perm, behaviorFlags));

        ASSERT_NS_SUCCEEDED(
            check->Init(testPath, IOFlags, perm, behaviorFlags));
      }

      // Check that we start with an empty file
      int64_t avail = 42;
      ASSERT_NS_SUCCEEDED(check->GetSize(&avail));

      ASSERT_TRUE(0 == avail);

      // Enlarge the file
      const int64_t toSize = groupLimit;
      ASSERT_NS_SUCCEEDED(stream->Seek(nsISeekableStream::NS_SEEK_SET, toSize));

      ASSERT_NS_SUCCEEDED(check->GetSize(&avail));

      ASSERT_TRUE(0 == avail);

      ASSERT_NS_SUCCEEDED(stream->SetEOF());

      ASSERT_NS_SUCCEEDED(check->GetSize(&avail));

      ASSERT_TRUE(toSize == avail);

      // Try to enlarge the file past the limit
      const int64_t overGroupLimit = groupLimit + 1;

      // Seeking is allowed
      ASSERT_NS_SUCCEEDED(
          stream->Seek(nsISeekableStream::NS_SEEK_SET, overGroupLimit));

      ASSERT_NS_SUCCEEDED(check->GetSize(&avail));

      ASSERT_TRUE(toSize == avail);

      // Setting file size to exceed quota should yield no device space error
      ASSERT_TRUE(NS_ERROR_FILE_NO_DEVICE_SPACE == stream->SetEOF());

      ASSERT_NS_SUCCEEDED(check->GetSize(&avail));

      ASSERT_TRUE(toSize == avail);

      // Shrink the file
      const int64_t toHalfSize = toSize / 2;
      ASSERT_NS_SUCCEEDED(
          stream->Seek(nsISeekableStream::NS_SEEK_SET, toHalfSize));

      ASSERT_NS_SUCCEEDED(check->GetSize(&avail));

      ASSERT_TRUE(toSize == avail);

      ASSERT_NS_SUCCEEDED(stream->SetEOF());

      ASSERT_NS_SUCCEEDED(check->GetSize(&avail));

      ASSERT_TRUE(toHalfSize == avail);

      // Shrink the file back to nothing
      ASSERT_NS_SUCCEEDED(stream->Seek(nsISeekableStream::NS_SEEK_SET, 0));

      ASSERT_NS_SUCCEEDED(check->GetSize(&avail));

      ASSERT_TRUE(toHalfSize == avail);

      ASSERT_NS_SUCCEEDED(stream->SetEOF());

      ASSERT_NS_SUCCEEDED(check->GetSize(&avail));

      ASSERT_TRUE(0 == avail);
    };

    ClientDirectoryLockHandle directoryLockHandle;

    QuotaManager* quotaManager = QuotaManager::Get();
    ASSERT_TRUE(quotaManager);

    bool done = false;

    quotaManager
        ->OpenClientDirectory(
            {GetOutputStreamTestOriginMetadata(), Client::SDB})
        ->Then(
            GetCurrentSerialEventTarget(), __func__,
            [&directoryLockHandle,
             &done](ClientDirectoryLockHandle&& aResolveValue) {
              directoryLockHandle = std::move(aResolveValue);

              done = true;
            },
            [&done](const nsresult aRejectValue) {
              ASSERT_TRUE(false);

              done = true;
            });

    SpinEventLoopUntil("Promise is fulfilled"_ns, [&done]() { return done; });

    ASSERT_TRUE(directoryLockHandle);

    PerformOnIOThread(std::move(ioTask));

    {
      auto destroyingDirectoryLockHandle = std::move(directoryLockHandle);
    }
  };

  PerformOnBackgroundThread(std::move(backgroundTask));
}

}  // namespace mozilla::dom::quota::test
