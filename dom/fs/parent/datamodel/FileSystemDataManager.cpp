/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FileSystemDataManager.h"

#include "ErrorList.h"
#include "FileSystemDatabaseManager.h"
#include "FileSystemDatabaseManagerVersion001.h"
#include "FileSystemDatabaseManagerVersion002.h"
#include "FileSystemFileManager.h"
#include "FileSystemHashSource.h"
#include "FileSystemParentTypes.h"
#include "NotifyUtils.h"
#include "ResultStatement.h"
#include "SchemaVersion001.h"
#include "SchemaVersion002.h"
#include "fs/FileSystemConstants.h"
#include "mozIStorageService.h"
#include "mozStorageCID.h"
#include "mozilla/Result.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/dom/FileSystemLog.h"
#include "mozilla/dom/FileSystemManagerParent.h"
#include "mozilla/dom/QMResult.h"
#include "mozilla/dom/quota/ClientDirectoryLock.h"
#include "mozilla/dom/quota/ClientImpl.h"
#include "mozilla/dom/quota/HashKeys.h"
#include "mozilla/dom/quota/QuotaCommon.h"
#include "mozilla/dom/quota/QuotaManager.h"
#include "mozilla/dom/quota/ResultExtensions.h"
#include "mozilla/dom/quota/ThreadUtils.h"
#include "mozilla/dom/quota/UsageInfo.h"
#include "mozilla/ipc/BackgroundParent.h"
#include "nsBaseHashtable.h"
#include "nsCOMPtr.h"
#include "nsHashKeys.h"
#include "nsIFile.h"
#include "nsIFileURL.h"
#include "nsNetCID.h"
#include "nsServiceManagerUtils.h"
#include "nsString.h"
#include "nsThreadUtils.h"

namespace mozilla::dom::fs::data {

namespace {

// When CheckedUnsafePtr's checking is enabled, it's necessary to ensure that
// the hashtable uses the copy constructor instead of memmove for moving entries
// since memmove will break CheckedUnsafePtr in a memory-corrupting way.

// The assertion type must be the same as the assertion type used for defining
// the base class for FileSystemDataManager in FileSystemDataManager.h!
using FileSystemDataManagerHashKey =
    std::conditional<ReleaseAssertEnabled::value,
                     quota::nsCStringHashKeyWithDisabledMemmove,
                     nsCStringHashKey>::type;

// Raw (but checked when the diagnostic assert is enabled) references as we
// don't want to keep FileSystemDataManager objects alive forever. When a
// FileSystemDataManager is destroyed it calls RemoveFileSystemDataManager
// to clear itself.
using FileSystemDataManagerHashtable =
    nsBaseHashtable<FileSystemDataManagerHashKey,
                    NotNull<CheckedUnsafePtr<FileSystemDataManager>>,
                    MovingNotNull<CheckedUnsafePtr<FileSystemDataManager>>>;

// This hashtable isn't protected by any mutex but it is only ever touched on
// the PBackground thread.
StaticAutoPtr<FileSystemDataManagerHashtable> gDataManagers;

RefPtr<FileSystemDataManager> GetFileSystemDataManager(const Origin& aOrigin) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  if (gDataManagers) {
    auto maybeDataManager = gDataManagers->MaybeGet(aOrigin);
    if (maybeDataManager) {
      RefPtr<FileSystemDataManager> result(
          std::move(*maybeDataManager).unwrapBasePtr());
      return result;
    }
  }

  return nullptr;
}

void AddFileSystemDataManager(
    const Origin& aOrigin, const RefPtr<FileSystemDataManager>& aDataManager) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();
  MOZ_ASSERT(!quota::QuotaManager::IsShuttingDown());

  if (!gDataManagers) {
    gDataManagers = new FileSystemDataManagerHashtable();
  }

  MOZ_ASSERT(!gDataManagers->Contains(aOrigin));
  gDataManagers->InsertOrUpdate(aOrigin,
                                WrapMovingNotNullUnchecked(aDataManager));
}

void RemoveFileSystemDataManager(const Origin& aOrigin) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  MOZ_ASSERT(gDataManagers);
  const DebugOnly<bool> removed = gDataManagers->Remove(aOrigin);
  MOZ_ASSERT(removed);

  if (!gDataManagers->Count()) {
    gDataManagers = nullptr;
  }
}

}  // namespace

Result<ResultConnection, QMResult> GetStorageConnection(
    const quota::OriginMetadata& aOriginMetadata,
    const int64_t aDirectoryLockId) {
  MOZ_ASSERT(aDirectoryLockId >= -1);

  // Ensure that storage is initialized and file system folder exists!
  QM_TRY_INSPECT(const auto& dbFileUrl,
                 GetDatabaseFileURL(aOriginMetadata, aDirectoryLockId));

  QM_TRY_INSPECT(
      const auto& storageService,
      QM_TO_RESULT_TRANSFORM(MOZ_TO_RESULT_GET_TYPED(
          nsCOMPtr<mozIStorageService>, MOZ_SELECT_OVERLOAD(do_GetService),
          MOZ_STORAGE_SERVICE_CONTRACTID)));

  QM_TRY_UNWRAP(auto connection,
                QM_TO_RESULT_TRANSFORM(MOZ_TO_RESULT_INVOKE_MEMBER_TYPED(
                    nsCOMPtr<mozIStorageConnection>, storageService,
                    OpenDatabaseWithFileURL, dbFileUrl, ""_ns,
                    mozIStorageService::CONNECTION_DEFAULT)));

  ResultConnection result(connection);

  return result;
}

Result<EntryId, QMResult> GetRootHandle(const Origin& origin) {
  MOZ_ASSERT(!origin.IsEmpty());

  return FileSystemHashSource::GenerateHash(origin, kRootString);
}

Result<EntryId, QMResult> GetEntryHandle(
    const FileSystemChildMetadata& aHandle) {
  MOZ_ASSERT(!aHandle.parentId().IsEmpty());

  return FileSystemHashSource::GenerateHash(aHandle.parentId(),
                                            aHandle.childName());
}

FileSystemDataManager::FileSystemDataManager(
    const quota::OriginMetadata& aOriginMetadata,
    RefPtr<quota::QuotaManager> aQuotaManager,
    MovingNotNull<nsCOMPtr<nsIEventTarget>> aIOTarget,
    MovingNotNull<RefPtr<TaskQueue>> aIOTaskQueue)
    : mOriginMetadata(aOriginMetadata),
      mQuotaManager(std::move(aQuotaManager)),
      mBackgroundTarget(WrapNotNull(GetCurrentSerialEventTarget())),
      mIOTarget(std::move(aIOTarget)),
      mIOTaskQueue(std::move(aIOTaskQueue)),
      mDirectoryLockId(-1),
      mRegCount(0),
      mVersion(0),
      mState(State::Initial) {}

FileSystemDataManager::~FileSystemDataManager() {
  NS_ASSERT_OWNINGTHREAD(FileSystemDataManager);
  MOZ_ASSERT(mState == State::Closed);
  MOZ_ASSERT(!mDatabaseManager);
}

RefPtr<FileSystemDataManager::CreatePromise>
FileSystemDataManager::GetOrCreateFileSystemDataManager(
    const quota::OriginMetadata& aOriginMetadata) {
  if (quota::QuotaManager::IsShuttingDown()) {
    return CreatePromise::CreateAndReject(NS_ERROR_FAILURE, __func__);
  }

  if (RefPtr<FileSystemDataManager> dataManager =
          GetFileSystemDataManager(aOriginMetadata.mOrigin)) {
    if (dataManager->IsOpening()) {
      // We have to wait for the open to be finished before resolving the
      // promise. The manager can't close itself in the meantime because we
      // add a new registration in the lambda capture list.
      return dataManager->OnOpen()->Then(
          GetCurrentSerialEventTarget(), __func__,
          [dataManager = Registered<FileSystemDataManager>(dataManager)](
              const BoolPromise::ResolveOrRejectValue& aValue) {
            if (aValue.IsReject()) {
              return CreatePromise::CreateAndReject(aValue.RejectValue(),
                                                    __func__);
            }
            return CreatePromise::CreateAndResolve(dataManager, __func__);
          });
    }

    if (dataManager->IsClosing()) {
      // First, we need to wait for the close to be finished. After that the
      // manager is closed and it can't be opened again. The only option is
      // to create a new manager and open it. However, all this stuff is
      // asynchronous, so it can happen that something else called
      // `GetOrCreateFileSystemManager` in the meantime. For that reason, we
      // shouldn't try to create a new manager and open it here, a "recursive"
      // call to `GetOrCreateFileSystemManager` will handle any new situation.
      return dataManager->OnClose()->Then(
          GetCurrentSerialEventTarget(), __func__,
          [aOriginMetadata](const BoolPromise::ResolveOrRejectValue&) {
            return GetOrCreateFileSystemDataManager(aOriginMetadata);
          });
    }

    return CreatePromise::CreateAndResolve(
        Registered<FileSystemDataManager>(std::move(dataManager)), __func__);
  }

  RefPtr<quota::QuotaManager> quotaManager = quota::QuotaManager::Get();
  MOZ_ASSERT(quotaManager);

  QM_TRY_UNWRAP(auto streamTransportService,
                MOZ_TO_RESULT_GET_TYPED(nsCOMPtr<nsIEventTarget>,
                                        MOZ_SELECT_OVERLOAD(do_GetService),
                                        NS_STREAMTRANSPORTSERVICE_CONTRACTID),
                CreatePromise::CreateAndReject(NS_ERROR_FAILURE, __func__));

  nsCString taskQueueName("OPFS "_ns + aOriginMetadata.mOrigin);

  RefPtr<TaskQueue> ioTaskQueue =
      TaskQueue::Create(do_AddRef(streamTransportService), taskQueueName.get());

  auto dataManager = MakeRefPtr<FileSystemDataManager>(
      aOriginMetadata, std::move(quotaManager),
      WrapMovingNotNull(streamTransportService),
      WrapMovingNotNull(ioTaskQueue));

  AddFileSystemDataManager(aOriginMetadata.mOrigin, dataManager);

  return dataManager->BeginOpen()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [dataManager = Registered<FileSystemDataManager>(dataManager)](
          const BoolPromise::ResolveOrRejectValue& aValue) {
        if (aValue.IsReject()) {
          return CreatePromise::CreateAndReject(aValue.RejectValue(), __func__);
        }

        return CreatePromise::CreateAndResolve(dataManager, __func__);
      });
}

// static
void FileSystemDataManager::AbortOperationsForLocks(
    const quota::Client::DirectoryLockIdTable& aDirectoryLockIds) {
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  // XXX Share the iteration code with `InitiateShutdown`, for example by
  // creating a helper function which would take a predicate function.

  if (!gDataManagers) {
    return;
  }

  for (const auto& dataManager : gDataManagers->Values()) {
    // Check if the Manager holds an acquired DirectoryLock. Origin clearing
    // can't be blocked by this Manager if there is no acquired DirectoryLock.
    // If there is an acquired DirectoryLock, check if the table contains the
    // lock for the Manager.
    if (quota::Client::IsLockForObjectAcquiredAndContainedInLockTable(
            *dataManager, aDirectoryLockIds)) {
      dataManager->RequestAllowToClose();
    }
  }
}

// static
void FileSystemDataManager::InitiateShutdown() {
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  if (!gDataManagers) {
    return;
  }

  for (const auto& dataManager : gDataManagers->Values()) {
    dataManager->RequestAllowToClose();
  }
}

// static
bool FileSystemDataManager::IsShutdownCompleted() {
  ::mozilla::ipc::AssertIsOnBackgroundThread();

  return !gDataManagers;
}

void FileSystemDataManager::AssertIsOnIOTarget() const {
  DebugOnly<bool> current = false;
  MOZ_ASSERT(NS_SUCCEEDED(mIOTarget->IsOnCurrentThread(&current)));
  MOZ_ASSERT(current);
}

void FileSystemDataManager::Register() { mRegCount++; }

void FileSystemDataManager::Unregister() {
  MOZ_ASSERT(mRegCount > 0);

  mRegCount--;

  if (IsInactive()) {
    BeginClose();
  }
}

void FileSystemDataManager::RegisterActor(
    NotNull<FileSystemManagerParent*> aActor) {
  MOZ_ASSERT(!mBackgroundThreadAccessible.Access()->mActors.Contains(aActor));
  MOZ_ASSERT(mState == State::Open);
  MOZ_ASSERT(mDirectoryLockHandle);

  mBackgroundThreadAccessible.Access()->mActors.Insert(aActor);

  aActor->SetRegistered(true);

  // It can happen that FileSystemDataManager::AbortOperationsForLocks is
  // called during async CreateFileSystemManagerParent operation when the actor
  // is not yet registered. FileSystemDataManager::RequestAllowToClose is not
  // able to propagate the RequestAllowToClose notification to the actor in
  // that case. However, one a new actor is registered, we can check the
  // directory lock if it has been invalidated and eventually notify the actor
  // about the abort.

  if (mDirectoryLockHandle->Invalidated()) {
    aActor->RequestAllowToClose();
  }
}

void FileSystemDataManager::UnregisterActor(
    NotNull<FileSystemManagerParent*> aActor) {
  MOZ_ASSERT(mBackgroundThreadAccessible.Access()->mActors.Contains(aActor));
  MOZ_ASSERT(mState == State::Open);
  MOZ_ASSERT(mDirectoryLockHandle);

  mBackgroundThreadAccessible.Access()->mActors.Remove(aActor);

  aActor->SetRegistered(false);

  if (IsInactive()) {
    BeginClose();
  }
}

void FileSystemDataManager::RegisterAccessHandle(
    NotNull<FileSystemAccessHandle*> aAccessHandle) {
  MOZ_ASSERT(!mBackgroundThreadAccessible.Access()->mAccessHandles.Contains(
      aAccessHandle));

  mBackgroundThreadAccessible.Access()->mAccessHandles.Insert(aAccessHandle);
}

void FileSystemDataManager::UnregisterAccessHandle(
    NotNull<FileSystemAccessHandle*> aAccessHandle) {
  MOZ_ASSERT(mBackgroundThreadAccessible.Access()->mAccessHandles.Contains(
      aAccessHandle));

  mBackgroundThreadAccessible.Access()->mAccessHandles.Remove(aAccessHandle);

  if (IsInactive()) {
    BeginClose();
  }
}

RefPtr<BoolPromise> FileSystemDataManager::OnOpen() {
  MOZ_ASSERT(mState == State::Opening);

  return mOpenPromiseHolder.Ensure(__func__);
}

RefPtr<BoolPromise> FileSystemDataManager::OnClose() {
  MOZ_ASSERT(mState == State::Closing);

  return mClosePromiseHolder.Ensure(__func__);
}

// Note: Input can be temporary or main file id
Result<bool, QMResult> FileSystemDataManager::IsLocked(
    const FileId& aFileId) const {
  auto checkIfEntryIdIsLocked = [this, &aFileId]() -> Result<bool, QMResult> {
    QM_TRY_INSPECT(const EntryId& entryId,
                   mDatabaseManager->GetEntryId(aFileId));

    return IsLocked(entryId);
  };

  auto valueToSome = [](auto aValue) { return Some(std::move(aValue)); };

  QM_TRY_UNWRAP(Maybe<bool> maybeLocked,
                QM_OR_ELSE_LOG_VERBOSE_IF(
                    // Expression.
                    (checkIfEntryIdIsLocked().map(valueToSome)),
                    // Predicate.
                    IsSpecificError<NS_ERROR_DOM_NOT_FOUND_ERR>,
                    // Fallback.
                    ([](const auto&) -> Result<Maybe<bool>, QMResult> {
                      return Some(false);  // Non-existent files are not locked.
                    })));

  if (!maybeLocked) {
    // If the metadata is inaccessible, we block modifications.
    return true;
  }

  return *maybeLocked;
}

Result<bool, QMResult> FileSystemDataManager::IsLocked(
    const EntryId& aEntryId) const {
  return mExclusiveLocks.Contains(aEntryId) || mSharedLocks.Contains(aEntryId);
}

Result<FileId, QMResult> FileSystemDataManager::LockExclusive(
    const EntryId& aEntryId) {
  QM_TRY_UNWRAP(const bool isLocked, IsLocked(aEntryId));
  if (isLocked) {
    return Err(QMResult(NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR));
  }

  QM_TRY_INSPECT(const FileId& fileId,
                 mDatabaseManager->EnsureFileId(aEntryId));

  // If the file has been removed, we should get a file not found error.
  // Otherwise, if usage tracking cannot be started because file size is not
  // known and attempts to read it are failing, lock is denied to freeze the
  // quota usage until the (external) blocker is gone or the file is removed.
  QM_TRY(QM_TO_RESULT(mDatabaseManager->BeginUsageTracking(fileId)));

  LOG_VERBOSE(("ExclusiveLock"));
  mExclusiveLocks.Insert(aEntryId);

  return fileId;
}

// TODO: Improve reporting of failures, see bug 1840811.
void FileSystemDataManager::UnlockExclusive(const EntryId& aEntryId) {
  MOZ_ASSERT(mExclusiveLocks.Contains(aEntryId));

  LOG_VERBOSE(("ExclusiveUnlock"));
  mExclusiveLocks.Remove(aEntryId);

  QM_TRY_INSPECT(const FileId& fileId, mDatabaseManager->GetFileId(aEntryId),
                 QM_VOID);

  // On error, usage tracking remains on to prevent writes until usage is
  // updated successfully.
  QM_TRY(MOZ_TO_RESULT(mDatabaseManager->UpdateUsage(fileId)), QM_VOID);
  QM_TRY(MOZ_TO_RESULT(mDatabaseManager->EndUsageTracking(fileId)), QM_VOID);
}

Result<FileId, QMResult> FileSystemDataManager::LockShared(
    const EntryId& aEntryId) {
  if (mExclusiveLocks.Contains(aEntryId)) {
    return Err(QMResult(NS_ERROR_DOM_NO_MODIFICATION_ALLOWED_ERR));
  }

  auto& count = mSharedLocks.LookupOrInsert(aEntryId);
  if (!(1u + CheckedUint32(count)).isValid()) {  // don't make the count invalid
    return Err(QMResult(NS_ERROR_UNEXPECTED));
  }

  QM_TRY_INSPECT(const FileId& fileId,
                 mDatabaseManager->EnsureTemporaryFileId(aEntryId));

  // If the file has been removed, we should get a file not found error.
  // Otherwise, if usage tracking cannot be started because file size is not
  // known and attempts to read it are failing, lock is denied to freeze the
  // quota usage until the (external) blocker is gone or the file is removed.
  QM_TRY(QM_TO_RESULT(mDatabaseManager->BeginUsageTracking(fileId)));

  ++count;
  LOG_VERBOSE(("SharedLock %u", count));

  return fileId;
}

// TODO: Improve reporting of failures, see bug 1840811.
void FileSystemDataManager::UnlockShared(const EntryId& aEntryId,
                                         const FileId& aFileId, bool aAbort) {
  const bool wasDeprecated = [&]() {
    // Someone recreated the file and put an exclusive lock on it
    if (mExclusiveLocks.Contains(aEntryId)) {
      aAbort = true;
    }

    auto entry = mDeprecatedLocks.Lookup(aEntryId);
    if (!entry) {
      return false;
    }

    auto& fileIdData = entry.Data();
    auto fileIdIt = fileIdData.IndexOf(aFileId);
    if (nsTArray<FileId>::NoIndex == fileIdIt) {
      return false;
    }

    fileIdData.UnorderedRemoveElementAt(fileIdIt);

    if (fileIdData.IsEmpty()) {
      entry.Remove();
    }

    return true;
  }();

  // Deprecated locks are per file. A file cannot be
  // both in active use with a shared lock and deprecated,
  // while entries can.
  if (!wasDeprecated) {
    MOZ_ASSERT(!mExclusiveLocks.Contains(aEntryId));

    auto entry = mSharedLocks.Lookup(aEntryId);
    if (!entry) {
      return;
    }

    MOZ_ASSERT(entry.Data() > 0);
    --entry.Data();

    LOG_VERBOSE(("SharedUnlock %u", *entry));

    if (0u == entry.Data()) {
      entry.Remove();
    }
  }

  // If underlying file does not exist but should close, abort instead.
  if (!aAbort) {
    QM_WARNONLY_TRY_UNWRAP(const Maybe<bool> doesFileExist,
                           mDatabaseManager->DoesFileExist(aEntryId));
    const bool exists = doesFileExist.isSome() && doesFileExist.ref();
    if (!exists) {
      aAbort = true;
    }
  }

  // On error, usage tracking remains on to prevent writes until usage is
  // updated successfully.
  QM_TRY(MOZ_TO_RESULT(mDatabaseManager->UpdateUsage(aFileId)), QM_VOID);
  QM_TRY(MOZ_TO_RESULT(mDatabaseManager->EndUsageTracking(aFileId)), QM_VOID);
  QM_TRY(
      MOZ_TO_RESULT(mDatabaseManager->MergeFileId(aEntryId, aFileId, aAbort)),
      QM_VOID);
}

void FileSystemDataManager::DeprecateSharedLocks(const EntryId& aEntryId,
                                                 const FileId& aFileId) {
  auto oldEntry = mSharedLocks.Lookup(aEntryId);
  if (!oldEntry) {
    return;
  }

  auto& deprecatedEntries = mDeprecatedLocks.LookupOrInsert(aEntryId);
  MOZ_ASSERT(!deprecatedEntries.Contains(aFileId));
  deprecatedEntries.AppendElement(aFileId);

  MOZ_ASSERT(oldEntry.Data() >= 1);
  if (oldEntry.Data() == 1) {
    oldEntry.Remove();
  } else {
    --oldEntry.Data();
  }
}

bool FileSystemDataManager::IsLockedWithDeprecatedSharedLock(
    const EntryId& aEntryId, const FileId& aFileId) const {
  MOZ_ASSERT(!aEntryId.IsEmpty());
  MOZ_ASSERT(!aFileId.IsEmpty());

  auto entry = mDeprecatedLocks.Lookup(aEntryId);
  if (!entry) {
    return false;
  }

  return nsTArray<FileId>::NoIndex != entry.Data().IndexOf(aFileId);
}

FileMode FileSystemDataManager::GetMode(bool aKeepData) const {
  if (1 == mVersion) {
    return FileMode::EXCLUSIVE;
  }

  return aKeepData ? FileMode::SHARED_FROM_COPY : FileMode::SHARED_FROM_EMPTY;
}

bool FileSystemDataManager::IsInactive() const {
  auto data = mBackgroundThreadAccessible.Access();
  return !mRegCount && !data->mActors.Count() && !data->mAccessHandles.Count();
}

void FileSystemDataManager::RequestAllowToClose() {
  for (const auto& actor : mBackgroundThreadAccessible.Access()->mActors) {
    actor->RequestAllowToClose();
  }
}

RefPtr<BoolPromise> FileSystemDataManager::BeginOpen() {
  MOZ_ASSERT(mQuotaManager);
  MOZ_ASSERT(mState == State::Initial);

  mState = State::Opening;

  mQuotaManager
      ->OpenClientDirectory(
          {mOriginMetadata, mozilla::dom::quota::Client::FILESYSTEM})
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [self = RefPtr<FileSystemDataManager>(this)](
                 quota::QuotaManager::ClientDirectoryLockHandlePromise::
                     ResolveOrRejectValue&& value) {
               if (value.IsReject()) {
                 return BoolPromise::CreateAndReject(value.RejectValue(),
                                                     __func__);
               }

               self->mDirectoryLockHandle = std::move(value.ResolveValue());

               MOZ_ASSERT(self->mDirectoryLockHandle->Id() >= 0);
               self->mDirectoryLockId = self->mDirectoryLockHandle->Id();

               if (self->mDirectoryLockHandle->Invalidated()) {
                 return BoolPromise::CreateAndReject(NS_ERROR_ABORT, __func__);
               }

               NotifyDatabaseWorkStarted();

               return BoolPromise::CreateAndResolve(true, __func__);
             })
      ->Then(
          mQuotaManager->IOThread(), __func__,
          [self = RefPtr<FileSystemDataManager>(this)](
              const BoolPromise::ResolveOrRejectValue& value) {
            if (value.IsReject()) {
              return BoolPromise::CreateAndReject(value.RejectValue(),
                                                  __func__);
            }

            QM_TRY(
                MOZ_TO_RESULT(EnsureFileSystemDirectory(self->mOriginMetadata)),
                CreateAndRejectBoolPromise);

            quota::SleepIfEnabled(
                StaticPrefs::dom_fs_databaseInitialization_pauseOnIOThreadMs());

            return BoolPromise::CreateAndResolve(true, __func__);
          })
      ->Then(
          MutableIOTaskQueuePtr(), __func__,
          [self = RefPtr<FileSystemDataManager>(this)](
              const BoolPromise::ResolveOrRejectValue& value) {
            if (value.IsReject()) {
              return BoolPromise::CreateAndReject(value.RejectValue(),
                                                  __func__);
            }

            QM_TRY_UNWRAP(auto connection,
                          GetStorageConnection(self->mOriginMetadata,
                                               self->mDirectoryLockId),
                          CreateAndRejectBoolPromiseFromQMResult);

            QM_TRY_UNWRAP(UniquePtr<FileSystemFileManager> fmPtr,
                          FileSystemFileManager::CreateFileSystemFileManager(
                              self->mOriginMetadata),
                          CreateAndRejectBoolPromiseFromQMResult);

            QM_TRY_UNWRAP(
                self->mVersion,
                QM_OR_ELSE_WARN_IF(
                    // Expression.
                    SchemaVersion002::InitializeConnection(
                        connection, *fmPtr, self->mOriginMetadata.mOrigin),
                    // Predicate.
                    ([](const auto&) { return true; }),
                    // Fallback.
                    ([&self, &connection](const auto&) {
                      QM_TRY_RETURN(SchemaVersion001::InitializeConnection(
                          connection, self->mOriginMetadata.mOrigin));
                    })),
                CreateAndRejectBoolPromiseFromQMResult);

            QM_TRY_UNWRAP(
                EntryId rootId,
                fs::data::GetRootHandle(self->mOriginMetadata.mOrigin),
                CreateAndRejectBoolPromiseFromQMResult);

            switch (self->mVersion) {
              case 1: {
                self->mDatabaseManager =
                    MakeUnique<FileSystemDatabaseManagerVersion001>(
                        self, std::move(connection), std::move(fmPtr), rootId);
                break;
              }

              case 2: {
                self->mDatabaseManager =
                    MakeUnique<FileSystemDatabaseManagerVersion002>(
                        self, std::move(connection), std::move(fmPtr), rootId);
                break;
              }

              default:
                break;
            }

            return BoolPromise::CreateAndResolve(true, __func__);
          })
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [self = RefPtr<FileSystemDataManager>(this)](
                 const BoolPromise::ResolveOrRejectValue& value) {
               if (value.IsReject()) {
                 self->mState = State::Initial;

                 self->mOpenPromiseHolder.RejectIfExists(value.RejectValue(),
                                                         __func__);

               } else {
                 self->mState = State::Open;

                 self->mOpenPromiseHolder.ResolveIfExists(true, __func__);
               }
             });

  return OnOpen();
}

RefPtr<BoolPromise> FileSystemDataManager::BeginClose() {
  MOZ_ASSERT(mState != State::Closing && mState != State::Closed);
  MOZ_ASSERT(IsInactive());

  mState = State::Closing;

  InvokeAsync(MutableIOTaskQueuePtr(), __func__,
              [self = RefPtr<FileSystemDataManager>(this)]() {
                if (self->mDatabaseManager) {
                  self->mDatabaseManager->Close();
                  self->mDatabaseManager = nullptr;
                }

                return BoolPromise::CreateAndResolve(true, __func__);
              })
      ->Then(MutableBackgroundTargetPtr(), __func__,
             [self = RefPtr<FileSystemDataManager>(this)](
                 const BoolPromise::ResolveOrRejectValue&) {
               return self->mIOTaskQueue->BeginShutdown();
             })
      ->Then(MutableBackgroundTargetPtr(), __func__,
             [self = RefPtr<FileSystemDataManager>(this)](
                 const ShutdownPromise::ResolveOrRejectValue&) {
               {
                 auto destroyingDirectoryLockHandle =
                     std::move(self->mDirectoryLockHandle);
               }

               RemoveFileSystemDataManager(self->mOriginMetadata.mOrigin);

               self->mState = State::Closed;

               self->mClosePromiseHolder.ResolveIfExists(true, __func__);
             });

  return OnClose();
}

}  // namespace mozilla::dom::fs::data
