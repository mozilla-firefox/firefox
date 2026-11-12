/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "FileSystemAccessHandle.h"

#include "FileSystemDatabaseManager.h"
#include "FileSystemEncryptedFileStreams.h"
#include "FileSystemParentTypes.h"
#include "mozilla/dom/FileSystemCipherKeyManager.h"
#include "mozilla/dom/FileSystemDataManager.h"
#include "mozilla/dom/FileSystemHelpers.h"
#include "mozilla/dom/FileSystemLog.h"
#include "mozilla/dom/quota/FileStreams.h"
#include "mozilla/dom/quota/PersistenceType.h"
#include "mozilla/dom/quota/QuotaCommon.h"
#include "mozilla/dom/quota/RemoteQuotaObjectParent.h"
#include "mozilla/dom/quota/ResultExtensions.h"
#include "mozilla/ipc/RandomAccessStreamParams.h"
#include "mozilla/ipc/RandomAccessStreamUtils.h"
#include "nsIFileStreams.h"

namespace mozilla::dom {

FileSystemAccessHandle::FileSystemAccessHandle(
    RefPtr<fs::data::FileSystemDataManager> aDataManager,
    const fs::EntryId& aEntryId, MovingNotNull<RefPtr<TaskQueue>> aIOTaskQueue)
    : mEntryId(aEntryId),
      mDataManager(std::move(aDataManager)),
      mIOTaskQueue(std::move(aIOTaskQueue)),
      mActor(nullptr),
      mControlActor(nullptr),
      mRegCount(0),
      mLocked(false),
      mRegistered(false),
      mClosed(false) {}

FileSystemAccessHandle::~FileSystemAccessHandle() {
  MOZ_DIAGNOSTIC_ASSERT(mClosed);
}

// static
RefPtr<FileSystemAccessHandle::CreatePromise> FileSystemAccessHandle::Create(
    RefPtr<fs::data::FileSystemDataManager> aDataManager,
    const fs::EntryId& aEntryId) {
  MOZ_ASSERT(aDataManager);
  aDataManager->AssertIsOnIOTarget();

  RefPtr<TaskQueue> ioTaskQueue = TaskQueue::Create(
      do_AddRef(aDataManager->MutableIOTargetPtr()), "FileSystemAccessHandle");

  RefPtr<FileSystemAccessHandle> accessHandle = new FileSystemAccessHandle(
      std::move(aDataManager), aEntryId, WrapMovingNotNull(ioTaskQueue));

  return accessHandle->BeginInit()->Then(
      GetCurrentSerialEventTarget(), __func__,
      [accessHandle = fs::Registered<FileSystemAccessHandle>(accessHandle)](
          InitPromise::ResolveOrRejectValue&& value) mutable {
        if (value.IsReject()) {
          return CreatePromise::CreateAndReject(value.RejectValue(), __func__);
        }

        mozilla::ipc::RandomAccessStreamParams streamParams =
            std::move(value.ResolveValue());

        return CreatePromise::CreateAndResolve(
            std::pair(std::move(accessHandle), std::move(streamParams)),
            __func__);
      });
}

NS_IMPL_ISUPPORTS_INHERITED0(FileSystemAccessHandle, FileSystemStreamCallbacks)

void FileSystemAccessHandle::Register() { ++mRegCount; }

void FileSystemAccessHandle::Unregister() {
  MOZ_ASSERT(mRegCount > 0);

  --mRegCount;

  if (IsInactive() && IsOpen()) {
    BeginClose();
  }
}

void FileSystemAccessHandle::RegisterActor(
    NotNull<FileSystemAccessHandleParent*> aActor) {
  MOZ_ASSERT(!mActor);

  mActor = aActor;
}

void FileSystemAccessHandle::UnregisterActor(
    NotNull<FileSystemAccessHandleParent*> aActor) {
  MOZ_ASSERT(mActor);
  MOZ_ASSERT(mActor == aActor);

  mActor = nullptr;

  if (IsInactive() && IsOpen()) {
    BeginClose();
  }
}

void FileSystemAccessHandle::RegisterControlActor(
    NotNull<FileSystemAccessHandleControlParent*> aControlActor) {
  MOZ_ASSERT(!mControlActor);

  mControlActor = aControlActor;
}

void FileSystemAccessHandle::UnregisterControlActor(
    NotNull<FileSystemAccessHandleControlParent*> aControlActor) {
  MOZ_ASSERT(mControlActor);
  MOZ_ASSERT(mControlActor == aControlActor);

  mControlActor = nullptr;

  if (IsInactive() && IsOpen()) {
    BeginClose();
  }
}

bool FileSystemAccessHandle::IsOpen() const { return !mClosed; }

RefPtr<BoolPromise> FileSystemAccessHandle::BeginClose() {
  if (mClosed) {
    return BoolPromise::CreateAndResolve(true, __func__);
  }
  MOZ_ASSERT(IsOpen());

  LOG(("Closing AccessHandle"));

  mClosed = true;

  return InvokeAsync(mIOTaskQueue.get(), __func__,
                     [self = RefPtr(this)]() {
                       if (self->mRemoteQuotaObjectParent) {
                         self->mRemoteQuotaObjectParent->Close();
                       }

                       return BoolPromise::CreateAndResolve(true, __func__);
                     })
      ->Then(GetCurrentSerialEventTarget(), __func__,
             [self = RefPtr(this)](const BoolPromise::ResolveOrRejectValue&) {
               return self->mIOTaskQueue->BeginShutdown();
             })
      ->Then(
          GetCurrentSerialEventTarget(), __func__,
          [self = RefPtr(this)](const ShutdownPromise::ResolveOrRejectValue&) {
            if (self->mLocked) {
              self->mDataManager->UnlockExclusive(self->mEntryId);
            }

            return BoolPromise::CreateAndResolve(true, __func__);
          })
      ->Then(mDataManager->MutableBackgroundTargetPtr(), __func__,
             [self = RefPtr(this)](const BoolPromise::ResolveOrRejectValue&) {
               if (self->mRegistered) {
                 self->mDataManager->UnregisterAccessHandle(WrapNotNull(self));
               }

               self->mDataManager = nullptr;

               return BoolPromise::CreateAndResolve(true, __func__);
             });
}

bool FileSystemAccessHandle::IsInactive() const {
  return !mRegCount && !mActor && !mControlActor;
}

RefPtr<FileSystemAccessHandle::InitPromise>
FileSystemAccessHandle::BeginInit() {
  QM_TRY_UNWRAP(fs::FileId fileId, mDataManager->LockExclusive(mEntryId),
                [](const auto& aRv) {
                  return InitPromise::CreateAndReject(ToNSResult(aRv),
                                                      __func__);
                });

  mLocked = true;

  auto CreateAndRejectInitPromise = [](StaticString aFunc, nsresult aRv) {
    return CreateAndRejectMozPromise<InitPromise>(aFunc, aRv);
  };

  fs::ContentType type;
  fs::TimeStamp lastModifiedMilliSeconds;
  fs::Path path;
  nsCOMPtr<nsIFile> file;
  QM_TRY(MOZ_TO_RESULT(mDataManager->MutableDatabaseManagerPtr()->GetFile(
             mEntryId, fileId, fs::FileMode::EXCLUSIVE, type,
             lastModifiedMilliSeconds, path, file)),
         CreateAndRejectInitPromise);

  if (LOG_ENABLED()) {
    nsAutoString path;
    if (NS_SUCCEEDED(file->GetPath(path))) {
      LOG(("Opening SyncAccessHandle %s", NS_ConvertUTF16toUTF8(path).get()));
    }
  }

  return InvokeAsync(
             mDataManager->MutableBackgroundTargetPtr(), __func__,
             [self = RefPtr(this)]() {
               self->mDataManager->RegisterAccessHandle(WrapNotNull(self));

               self->mRegistered = true;

               return BoolPromise::CreateAndResolve(true, __func__);
             })
      ->Then(mIOTaskQueue.get(), __func__,
             [self = RefPtr(this), CreateAndRejectInitPromise,
              fileId = std::move(fileId), file = std::move(file)](
                 const BoolPromise::ResolveOrRejectValue& value) {
               if (value.IsReject()) {
                 return InitPromise::CreateAndReject(value.RejectValue(),
                                                     __func__);
               }

               const auto& originMetadata =
                   self->mDataManager->OriginMetadataRef();

               const RefPtr<fs::FileSystemCipherKeyManager> cipherKeyManager =
                   self->mDataManager->MaybeCipherKeyManager();
               MOZ_ASSERT(!originMetadata.mIsPrivate || cipherKeyManager);

               QM_TRY_UNWRAP(
                   nsCOMPtr<nsIRandomAccessStream> stream,
                   originMetadata.mIsPrivate
                       ? fs::CreateEncryptedFileRandomAccessStream(
                             originMetadata, *cipherKeyManager, fileId, file)
                       : CreateFileRandomAccessStream(
                             originMetadata.mPersistenceType, originMetadata,
                             quota::Client::FILESYSTEM, file, -1, -1,
                             nsIFileRandomAccessStream::DEFER_OPEN),
                   CreateAndRejectInitPromise);

               mozilla::ipc::RandomAccessStreamParams streamParams =
                   mozilla::ipc::SerializeRandomAccessStream(
                       WrapMovingNotNullUnchecked(std::move(stream)), self);

               return InitPromise::CreateAndResolve(std::move(streamParams),
                                                    __func__);
             });
}

}  // namespace mozilla::dom
