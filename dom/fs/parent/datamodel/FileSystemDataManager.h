/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_FS_PARENT_DATAMODEL_FILESYSTEMDATAMANAGER_H_
#define DOM_FS_PARENT_DATAMODEL_FILESYSTEMDATAMANAGER_H_

#include "FileSystemParentTypes.h"
#include "ResultConnection.h"
#include "mozilla/NotNull.h"
#include "mozilla/TaskQueue.h"
#include "mozilla/ThreadBound.h"
#include "mozilla/dom/FileSystemHelpers.h"
#include "mozilla/dom/FileSystemTypes.h"
#include "mozilla/dom/quota/CheckedUnsafePtr.h"
#include "mozilla/dom/quota/ClientDirectoryLockHandle.h"
#include "mozilla/dom/quota/CommonMetadata.h"
#include "mozilla/dom/quota/ForwardDecls.h"
#include "nsCOMPtr.h"
#include "nsISupportsUtils.h"
#include "nsString.h"
#include "nsTHashSet.h"

namespace mozilla {

template <typename V, typename E>
class Result;

namespace dom {

class FileSystemAccessHandle;
class FileSystemManagerParent;

namespace fs {
struct FileId;
class FileSystemChildMetadata;
}  // namespace fs

namespace quota {
class ClientDirectoryLock;
class QuotaManager;
}  // namespace quota

namespace fs::data {

class FileSystemDatabaseManager;

Result<EntryId, QMResult> GetRootHandle(const Origin& origin);

Result<EntryId, QMResult> GetEntryHandle(
    const FileSystemChildMetadata& aHandle);

Result<ResultConnection, QMResult> GetStorageConnection(
    const quota::OriginMetadata& aOriginMetadata,
    const int64_t aDirectoryLockId);

// The assertion type must be the same as the assertion type used for defining
// FileSystemDataManagerHashKey in FileSystemDataManager.cpp!
class FileSystemDataManager
    : public SupportsCheckedUnsafePtr<CheckIf<ReleaseAssertEnabled>> {
 public:
  enum struct State : uint8_t { Initial = 0, Opening, Open, Closing, Closed };

  FileSystemDataManager(const quota::OriginMetadata& aOriginMetadata,
                        RefPtr<quota::QuotaManager> aQuotaManager,
                        MovingNotNull<nsCOMPtr<nsIEventTarget>> aIOTarget,
                        MovingNotNull<RefPtr<TaskQueue>> aIOTaskQueue);

  // IsExclusive is true because we want to allow the move operations. There's
  // always just one consumer anyway.
  using CreatePromise = MozPromise<Registered<FileSystemDataManager>, nsresult,
                                   /* IsExclusive */ true>;
  static RefPtr<CreatePromise> GetOrCreateFileSystemDataManager(
      const quota::OriginMetadata& aOriginMetadata);

  static void AbortOperationsForLocks(
      const quota::Client::DirectoryLockIdTable& aDirectoryLockIds);

  static void InitiateShutdown();

  static bool IsShutdownCompleted();

  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(FileSystemDataManager)

  void AssertIsOnIOTarget() const;

  const quota::OriginMetadata& OriginMetadataRef() const {
    return mOriginMetadata;
  }

  nsISerialEventTarget* MutableBackgroundTargetPtr() const {
    return mBackgroundTarget.get();
  }

  nsIEventTarget* MutableIOTargetPtr() const { return mIOTarget.get(); }

  nsISerialEventTarget* MutableIOTaskQueuePtr() const {
    return mIOTaskQueue.get();
  }

  Maybe<quota::ClientDirectoryLock&> MaybeDirectoryLockRef() const {
    return ToMaybeRef(mDirectoryLockHandle.get());
  }

  FileSystemDatabaseManager* MutableDatabaseManagerPtr() const {
    MOZ_ASSERT(mDatabaseManager);

    return mDatabaseManager.get();
  }

  void Register();

  void Unregister();

  void RegisterActor(NotNull<FileSystemManagerParent*> aActor);

  void UnregisterActor(NotNull<FileSystemManagerParent*> aActor);

  void RegisterAccessHandle(NotNull<FileSystemAccessHandle*> aAccessHandle);

  void UnregisterAccessHandle(NotNull<FileSystemAccessHandle*> aAccessHandle);

  bool IsOpen() const { return mState == State::Open; }

  RefPtr<BoolPromise> OnOpen();

  RefPtr<BoolPromise> OnClose();

  Result<bool, QMResult> IsLocked(const FileId& aFileId) const;

  Result<bool, QMResult> IsLocked(const EntryId& aEntryId) const;

  Result<FileId, QMResult> LockExclusive(const EntryId& aEntryId);

  void UnlockExclusive(const EntryId& aEntryId);

  Result<FileId, QMResult> LockShared(const EntryId& aEntryId);

  void UnlockShared(const EntryId& aEntryId, const FileId& aFileId,
                    bool aAbort);

  void DeprecateSharedLocks(const EntryId& aEntryId, const FileId& aFileId);

  bool IsLockedWithDeprecatedSharedLock(const EntryId& aEntryId,
                                        const FileId& aFileId) const;

  FileMode GetMode(bool aKeepData) const;

 protected:
  virtual ~FileSystemDataManager();

  bool IsInactive() const;

  bool IsOpening() const { return mState == State::Opening; }

  bool IsClosing() const { return mState == State::Closing; }

  void RequestAllowToClose();

  RefPtr<BoolPromise> BeginOpen();

  RefPtr<BoolPromise> BeginClose();

  // Things touched on background thread only.
  struct BackgroundThreadAccessible {
    nsTHashSet<FileSystemManagerParent*> mActors;
    nsTHashSet<FileSystemAccessHandle*> mAccessHandles;
  };
  ThreadBound<BackgroundThreadAccessible> mBackgroundThreadAccessible;

  const quota::OriginMetadata mOriginMetadata;
  nsTHashSet<EntryId> mExclusiveLocks;
  nsTHashMap<EntryId, uint32_t> mSharedLocks;
  nsTHashMap<EntryId, nsTArray<FileId>> mDeprecatedLocks;
  NS_DECL_OWNINGEVENTTARGET
  const RefPtr<quota::QuotaManager> mQuotaManager;
  const NotNull<nsCOMPtr<nsISerialEventTarget>> mBackgroundTarget;
  const NotNull<nsCOMPtr<nsIEventTarget>> mIOTarget;
  const NotNull<RefPtr<TaskQueue>> mIOTaskQueue;
  quota::ClientDirectoryLockHandle mDirectoryLockHandle;
  UniquePtr<FileSystemDatabaseManager> mDatabaseManager;
  MozPromiseHolder<BoolPromise> mOpenPromiseHolder;
  MozPromiseHolder<BoolPromise> mClosePromiseHolder;
  int64_t mDirectoryLockId;
  uint32_t mRegCount;
  DatabaseVersion mVersion;
  State mState;
};

}  // namespace fs::data
}  // namespace dom
}  // namespace mozilla

#endif  // DOM_FS_PARENT_DATAMODEL_FILESYSTEMDATAMANAGER_H_
