/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "MutableBlobStorage.h"

#include "EmptyBlobImpl.h"
#include "File.h"
#include "MemoryBlobImpl.h"
#include "mozilla/CheckedInt.h"
#include "mozilla/Preferences.h"
#include "mozilla/TaskQueue.h"
#include "mozilla/dom/TemporaryIPCBlobChild.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/ipc/BackgroundChild.h"
#include "mozilla/ipc/PBackgroundChild.h"
#include "nsAnonymousTemporaryFile.h"
#include "nsNetCID.h"
#include "nsProxyRelease.h"

#define BLOB_MEMORY_TEMPORARY_FILE 1048576

namespace mozilla::dom {

namespace {

// This class uses the callback to inform when the Blob is created or when the
// error must be propagated.
class BlobCreationDoneRunnable final : public Runnable {
 public:
  BlobCreationDoneRunnable(MutableBlobStorage* aBlobStorage,
                           MutableBlobStorageCallback* aCallback,
                           BlobImpl* aBlobImpl, nsresult aRv)
      : Runnable("dom::BlobCreationDoneRunnable"),
        mBlobStorage(aBlobStorage),
        mCallback(aCallback),
        mBlobImpl(aBlobImpl),
        mRv(aRv) {
    MOZ_ASSERT(aBlobStorage);
    MOZ_ASSERT(aCallback);
    MOZ_ASSERT((NS_FAILED(aRv) && !aBlobImpl) ||
               (NS_SUCCEEDED(aRv) && aBlobImpl));
  }

  NS_IMETHOD
  Run() override {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(mBlobStorage);
    mCallback->BlobStoreCompleted(mBlobStorage, mBlobImpl, mRv);
    mCallback = nullptr;
    mBlobImpl = nullptr;
    return NS_OK;
  }

 private:
  ~BlobCreationDoneRunnable() override {
    MOZ_ASSERT(mBlobStorage);
    // If something when wrong, we still have to release these objects in the
    // correct thread.
    NS_ProxyRelease("BlobCreationDoneRunnable::mCallback",
                    mBlobStorage->EventTarget(), mCallback.forget());
  }

  RefPtr<MutableBlobStorage> mBlobStorage;
  RefPtr<MutableBlobStorageCallback> mCallback;
  RefPtr<BlobImpl> mBlobImpl;
  nsresult mRv;
};

// Simple runnable to propagate the error to the BlobStorage.
class ErrorPropagationRunnable final : public Runnable {
 public:
  ErrorPropagationRunnable(MutableBlobStorage* aBlobStorage, nsresult aRv)
      : Runnable("dom::ErrorPropagationRunnable"),
        mBlobStorage(aBlobStorage),
        mRv(aRv) {}

  NS_IMETHOD
  Run() override {
    mBlobStorage->ErrorPropagated(mRv);
    return NS_OK;
  }

 private:
  RefPtr<MutableBlobStorage> mBlobStorage;
  nsresult mRv;
};

// This runnable moves a buffer to the IO thread and there, it writes it into
// the temporary file, if its File Descriptor has not been already closed.
class WriteRunnable final : public Runnable {
 public:
  static WriteRunnable* CopyBuffer(MutableBlobStorage* aBlobStorage,
                                   const void* aData, uint32_t aLength) {
    MOZ_ASSERT(aBlobStorage);
    MOZ_ASSERT(aData);

    // We have to take a copy of this buffer.
    void* data = malloc(aLength);
    if (!data) {
      return nullptr;
    }

    memcpy((char*)data, aData, aLength);
    return new WriteRunnable(aBlobStorage, data, aLength);
  }

  static WriteRunnable* AdoptBuffer(MutableBlobStorage* aBlobStorage,
                                    void* aData, uint32_t aLength) {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(aBlobStorage);
    MOZ_ASSERT(aData);

    return new WriteRunnable(aBlobStorage, aData, aLength);
  }

  NS_IMETHOD
  Run() override {
    MOZ_ASSERT(!NS_IsMainThread());
    MOZ_ASSERT(mBlobStorage);

    PRFileDesc* fd = mBlobStorage->GetFD();
    if (!fd) {
      // The file descriptor has been closed in the meantime.
      return NS_OK;
    }

    int32_t written = mLength <= INT32_MAX ? PR_Write(fd, mData, mLength) : -1;
    if (NS_WARN_IF(written < 0 || uint32_t(written) != mLength)) {
      mBlobStorage->CloseFD();
      return mBlobStorage->EventTarget()->Dispatch(
          new ErrorPropagationRunnable(mBlobStorage, NS_ERROR_FAILURE),
          NS_DISPATCH_NORMAL);
    }

    return NS_OK;
  }

 private:
  WriteRunnable(MutableBlobStorage* aBlobStorage, void* aData, uint32_t aLength)
      : Runnable("dom::WriteRunnable"),
        mBlobStorage(aBlobStorage),
        mData(aData),
        mLength(aLength) {
    MOZ_ASSERT(mBlobStorage);
    MOZ_ASSERT(aData);
  }

  ~WriteRunnable() override { free(mData); }

  RefPtr<MutableBlobStorage> mBlobStorage;
  void* mData;
  uint32_t mLength;
};

// This runnable closes the FD in case something goes wrong or the temporary
// file is not needed anymore.
class CloseFileRunnable final : public Runnable {
 public:
  explicit CloseFileRunnable(PRFileDesc* aFD)
      : Runnable("dom::CloseFileRunnable"), mFD(aFD) {}

  NS_IMETHOD
  Run() override {
    MOZ_ASSERT(!NS_IsMainThread());
    PR_Close(mFD);
    mFD = nullptr;
    return NS_OK;
  }

 private:
  ~CloseFileRunnable() override {
    if (mFD) {
      PR_Close(mFD);
    }
  }

  PRFileDesc* mFD;
};

// This runnable is dispatched to the main-thread from the IO thread and its
// task is to create the blob and inform the callback.
class CreateBlobRunnable final : public Runnable,
                                 public TemporaryIPCBlobChildCallback {
 public:
  // We need to always declare refcounting because
  // TemporaryIPCBlobChildCallback has pure-virtual refcounting.
  NS_DECL_ISUPPORTS_INHERITED

  CreateBlobRunnable(MutableBlobStorage* aBlobStorage,
                     const nsACString& aContentType,
                     already_AddRefed<MutableBlobStorageCallback> aCallback)
      : Runnable("dom::CreateBlobRunnable"),
        mBlobStorage(aBlobStorage),
        mContentType(aContentType),
        mCallback(aCallback) {
    MOZ_ASSERT(!NS_IsMainThread());
    MOZ_ASSERT(aBlobStorage);
  }

  NS_IMETHOD
  Run() override {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(mBlobStorage);
    mBlobStorage->AskForBlob(this, mContentType);
    return NS_OK;
  }

  void OperationSucceeded(BlobImpl* aBlobImpl) override {
    RefPtr<MutableBlobStorageCallback> callback(std::move(mCallback));
    callback->BlobStoreCompleted(mBlobStorage, aBlobImpl, NS_OK);
  }

  void OperationFailed(nsresult aRv) override {
    RefPtr<MutableBlobStorageCallback> callback(std::move(mCallback));
    callback->BlobStoreCompleted(mBlobStorage, nullptr, aRv);
  }

 private:
  ~CreateBlobRunnable() override {
    MOZ_ASSERT(mBlobStorage);
    // If something when wrong, we still have to release data in the correct
    // thread.
    NS_ProxyRelease("CreateBlobRunnable::mCallback",
                    mBlobStorage->EventTarget(), mCallback.forget());
  }

  RefPtr<MutableBlobStorage> mBlobStorage;
  nsCString mContentType;
  RefPtr<MutableBlobStorageCallback> mCallback;
};

NS_IMPL_ISUPPORTS_INHERITED0(CreateBlobRunnable, Runnable)

// This task is used to know when the writing is completed. From the IO thread
// it dispatches a CreateBlobRunnable to the main-thread.
class LastRunnable final : public Runnable {
 public:
  LastRunnable(MutableBlobStorage* aBlobStorage, const nsACString& aContentType,
               MutableBlobStorageCallback* aCallback)
      : Runnable("dom::LastRunnable"),
        mBlobStorage(aBlobStorage),
        mContentType(aContentType),
        mCallback(aCallback) {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(mBlobStorage);
    MOZ_ASSERT(aCallback);
  }

  NS_IMETHOD
  Run() override {
    MOZ_ASSERT(!NS_IsMainThread());

    RefPtr<Runnable> runnable =
        new CreateBlobRunnable(mBlobStorage, mContentType, mCallback.forget());
    return mBlobStorage->EventTarget()->Dispatch(runnable, NS_DISPATCH_NORMAL);
  }

 private:
  ~LastRunnable() override {
    MOZ_ASSERT(mBlobStorage);
    // If something when wrong, we still have to release data in the correct
    // thread.
    NS_ProxyRelease("LastRunnable::mCallback", mBlobStorage->EventTarget(),
                    mCallback.forget());
  }

  RefPtr<MutableBlobStorage> mBlobStorage;
  nsCString mContentType;
  RefPtr<MutableBlobStorageCallback> mCallback;
};

}  // anonymous namespace

MutableBlobStorage::MutableBlobStorage(MutableBlobStorageType aType,
                                       nsIEventTarget* aEventTarget,
                                       uint32_t aMaxMemory)
    : mMutex("MutableBlobStorage::mMutex"),
      mData(nullptr),
      mDataLen(0),
      mDataBufferLen(0),
      mStorageState(aType == eOnlyInMemory ? eKeepInMemory : eInMemory),
      mFD(nullptr),
      mErrorResult(NS_OK),
      mEventTarget(aEventTarget),
      mMaxMemory(aMaxMemory) {
  MOZ_ASSERT(NS_IsMainThread());

  if (!mEventTarget) {
    mEventTarget = GetMainThreadSerialEventTarget();
  }

  if (aMaxMemory == 0 && aType == eCouldBeInTemporaryFile) {
    mMaxMemory = Preferences::GetUint("dom.blob.memoryToTemporaryFile",
                                      BLOB_MEMORY_TEMPORARY_FILE);
  }

  MOZ_ASSERT(mEventTarget);
}

MutableBlobStorage::~MutableBlobStorage() {
  free(mData);

  if (mFD) {
    RefPtr<Runnable> runnable = new CloseFileRunnable(mFD);
    (void)DispatchToIOThread(runnable.forget());
  }

  if (mTaskQueue) {
    mTaskQueue->BeginShutdown();
  }

  if (mActor) {
    NS_ProxyRelease("MutableBlobStorage::mActor", EventTarget(),
                    mActor.forget());
  }
}

void MutableBlobStorage::GetBlobImplWhenReady(
    const nsACString& aContentType, MutableBlobStorageCallback* aCallback) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aCallback);

  MutexAutoLock lock(mMutex);

  // GetBlob can be called just once.
  if (mStorageState == eClosed) {
    MOZ_ASSERT(false);
    return;
  }
  StorageState previousState = mStorageState;
  mStorageState = eClosed;

  if (previousState == eInTemporaryFile) {
    if (NS_FAILED(mErrorResult)) {
      MOZ_ASSERT(!mActor);

      RefPtr<Runnable> runnable =
          new BlobCreationDoneRunnable(this, aCallback, nullptr, mErrorResult);
      EventTarget()->Dispatch(runnable.forget(), NS_DISPATCH_NORMAL);
      return;
    }

    // We want to wait until all the WriteRunnable are completed. The way we do
    // this is to go to the I/O thread and then we come back: the runnables are
    // executed in order and this LastRunnable will be... the last one.
    // This Runnable will also close the FD on the I/O thread.
    RefPtr<Runnable> runnable = new LastRunnable(this, aContentType, aCallback);

    // If the dispatching fails, we are shutting down and it's fine to do not
    // run the callback.
    (void)DispatchToIOThread(runnable.forget());
    return;
  }

  // If we are waiting for the temporary file, it's better to wait...
  if (previousState == eWaitingForTemporaryFile) {
    mPendingContentType = aContentType;
    mPendingCallback = aCallback;
    return;
  }

  RefPtr<BlobImpl> blobImpl;

  if (mData) {
    blobImpl = new MemoryBlobImpl(mData, mDataLen,
                                  NS_ConvertUTF8toUTF16(aContentType));

    mData = nullptr;  // The MemoryBlobImpl takes ownership of the buffer
    mDataLen = 0;
    mDataBufferLen = 0;
  } else {
    blobImpl = new EmptyBlobImpl(NS_ConvertUTF8toUTF16(aContentType));
  }

  RefPtr<BlobCreationDoneRunnable> runnable =
      new BlobCreationDoneRunnable(this, aCallback, blobImpl, NS_OK);

  nsresult error =
      EventTarget()->Dispatch(runnable.forget(), NS_DISPATCH_NORMAL);
  if (NS_WARN_IF(NS_FAILED(error))) {
    return;
  }
}

nsresult MutableBlobStorage::Append(const void* aData, uint32_t aLength) {
  // This method can be called on any thread.

  MutexAutoLock lock(mMutex);
  if (mStorageState == eClosed) {
    MOZ_ASSERT(false);
    return NS_ERROR_NOT_AVAILABLE;
  }
  NS_ENSURE_ARG_POINTER(aData);

  if (!aLength) {
    return NS_OK;
  }

  // If eInMemory is the current Storage state, we could maybe migrate to
  // a temporary file.
  if (mStorageState == eInMemory && ShouldBeTemporaryStorage(lock, aLength) &&
      !MaybeCreateTemporaryFile(lock)) {
    return NS_ERROR_FAILURE;
  }

  // If we are already in the temporaryFile mode, we have to dispatch a
  // runnable.
  if (mStorageState == eInTemporaryFile) {
    // If a previous operation failed, let's return that error now.
    if (NS_FAILED(mErrorResult)) {
      return mErrorResult;
    }

    RefPtr<WriteRunnable> runnable =
        WriteRunnable::CopyBuffer(this, aData, aLength);
    if (NS_WARN_IF(!runnable)) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    nsresult rv = DispatchToIOThread(runnable.forget());
    if (NS_WARN_IF(NS_FAILED(rv))) {
      return rv;
    }

    mDataLen += aLength;
    return NS_OK;
  }

  // By default, we store in memory.

  uint64_t offset = mDataLen;

  if (!ExpandBufferSize(lock, aLength)) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  memcpy((char*)mData + offset, aData, aLength);
  return NS_OK;
}

bool MutableBlobStorage::ExpandBufferSize(const MutexAutoLock& aProofOfLock,
                                          uint64_t aSize) {
  MOZ_ASSERT(mStorageState < eInTemporaryFile);

  if (mDataBufferLen >= mDataLen + aSize) {
    mDataLen += aSize;
    return true;
  }

  // Start at 1 or we'll loop forever.
  CheckedUint32 bufferLen =
      std::max<uint32_t>(static_cast<uint32_t>(mDataBufferLen), 1);
  while (bufferLen.isValid() && bufferLen.value() < mDataLen + aSize) {
    bufferLen *= 2;
  }

  if (!bufferLen.isValid()) {
    return false;
  }

  void* data = realloc(mData, bufferLen.value());
  if (!data) {
    return false;
  }

  mData = data;
  mDataBufferLen = bufferLen.value();
  mDataLen += aSize;
  return true;
}

bool MutableBlobStorage::ShouldBeTemporaryStorage(
    const MutexAutoLock& aProofOfLock, uint64_t aSize) const {
  MOZ_ASSERT(mStorageState == eInMemory);

  CheckedUint32 bufferSize = mDataLen;
  bufferSize += aSize;

  if (!bufferSize.isValid()) {
    return false;
  }

  return bufferSize.value() >= mMaxMemory;
}

bool MutableBlobStorage::MaybeCreateTemporaryFile(
    const MutexAutoLock& aProofOfLock) {
  mStorageState = eWaitingForTemporaryFile;

  if (!NS_IsMainThread()) {
    RefPtr<MutableBlobStorage> self = this;
    nsCOMPtr<nsIRunnable> r = NS_NewRunnableFunction(
        "MutableBlobStorage::MaybeCreateTemporaryFile", [self]() {
          MutexAutoLock lock(self->mMutex);
          self->MaybeCreateTemporaryFileOnMainThread(lock);
          if (!self->mActor) {
            self->ErrorPropagated(NS_ERROR_FAILURE);
          }
        });
    EventTarget()->Dispatch(r.forget(), NS_DISPATCH_NORMAL);
    return true;
  }

  MaybeCreateTemporaryFileOnMainThread(aProofOfLock);
  return !!mActor;
}

void MutableBlobStorage::MaybeCreateTemporaryFileOnMainThread(
    const MutexAutoLock& aProofOfLock) {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!mActor);

  mozilla::ipc::PBackgroundChild* actorChild =
      mozilla::ipc::BackgroundChild::GetOrCreateForCurrentThread();
  if (NS_WARN_IF(!actorChild)) {
    return;
  }

  auto actor = MakeRefPtr<TemporaryIPCBlobChild>(this);

  // We need to manually increase the reference for this actor because the
  // IPC allocator method is not triggered. The Release() is called by IPDL
  // in DeallocPTemporaryIPCBlobChild on both normal teardown and synchronous
  // constructor failure, so this must happen BEFORE Send.
  // Here, actor.get() makes the programmer intention to manually increase the
  // reference count explicit, and also avoids a trap which blocks adding such
  // code accidentally.
  actor.get()->AddRef();

  if (!actorChild->SendPTemporaryIPCBlobConstructor(actor)) {
    // Constructor failed synchronously (e.g. PBackground link already dead).
    // IPDL has already called DeallocPTemporaryIPCBlobChild and released the
    // manual ref; our local RefPtr holds the last reference and will clean up.
    return;
  }

  mActor = std::move(actor);

  // The actor will call us when the FileDescriptor is received.
}

void MutableBlobStorage::TemporaryFileCreated(PRFileDesc* aFD) {
  MOZ_ASSERT(NS_IsMainThread());

  MutexAutoLock lock(mMutex);
  MOZ_ASSERT(mStorageState == eWaitingForTemporaryFile ||
             mStorageState == eClosed);
  MOZ_ASSERT_IF(mPendingCallback, mStorageState == eClosed);
  MOZ_ASSERT(mActor);
  MOZ_ASSERT(aFD);

  // If the object has been already closed and we don't need to execute a
  // callback, we need just to close the file descriptor in the correct thread.
  if (mStorageState == eClosed && !mPendingCallback) {
    RefPtr<Runnable> runnable = new CloseFileRunnable(aFD);

    // If this dispatching fails, CloseFileRunnable will close the FD in the
    // DTOR on the current thread.
    (void)DispatchToIOThread(runnable.forget());

    // Let's inform the parent that we have nothing else to do.
    mActor->SendOperationFailed();
    mActor = nullptr;
    return;
  }

  // If we still receiving data, we can proceed in temporary-file mode.
  if (mStorageState == eWaitingForTemporaryFile) {
    mStorageState = eInTemporaryFile;
  }

  mFD = aFD;
  MOZ_ASSERT(NS_SUCCEEDED(mErrorResult));

  // This runnable takes the ownership of mData and it will write this buffer
  // into the temporary file.
  RefPtr<WriteRunnable> runnable =
      WriteRunnable::AdoptBuffer(this, mData, mDataLen);
  MOZ_ASSERT(runnable);

  mData = nullptr;
  mDataBufferLen = 0;
  mDataLen = 0;

  nsresult rv = DispatchToIOThread(runnable.forget());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    // Shutting down, we cannot continue.
    return;
  }

  // If we are closed, it means that GetBlobImplWhenReady() has been called when
  // we were already waiting for a temporary file-descriptor. Finally we are
  // here, AdoptBuffer runnable is going to write the current buffer into this
  // file. After that, there is nothing else to write, and we dispatch
  // LastRunnable which ends up calling mPendingCallback via CreateBlobRunnable.
  if (mStorageState == eClosed) {
    MOZ_ASSERT(mPendingCallback);

    RefPtr<Runnable> runnable =
        new LastRunnable(this, mPendingContentType, mPendingCallback);
    (void)DispatchToIOThread(runnable.forget());

    mPendingCallback = nullptr;
  }
}

void MutableBlobStorage::AskForBlob(TemporaryIPCBlobChildCallback* aCallback,
                                    const nsACString& aContentType) {
  MOZ_ASSERT(NS_IsMainThread());

  MutexAutoLock lock(mMutex);
  MOZ_ASSERT(mStorageState == eClosed);
  MOZ_ASSERT(aCallback);

  // If something went wrong in one of the previous WriteRunnable runnables,
  // it's time to report the error.
  if (NS_FAILED(mErrorResult)) {
    MOZ_ASSERT(!mFD);
    MOZ_ASSERT(!mActor);
    aCallback->OperationFailed(mErrorResult);
    return;
  }

  MOZ_ASSERT(mFD);
  MOZ_ASSERT(mActor);

  // Let's pass the FileDescriptor to the parent actor in order to keep the file
  // locked on windows.
  mActor->AskForBlob(aCallback, aContentType, mFD);

  // The previous operation has duplicated the file descriptor. Now we can close
  // mFD. The parent will take care of closing the duplicated file descriptor on
  // its side.
  RefPtr<Runnable> runnable = new CloseFileRunnable(mFD);
  (void)DispatchToIOThread(runnable.forget());

  mFD = nullptr;
  mActor = nullptr;
}

void MutableBlobStorage::ErrorPropagated(nsresult aRv) {
  MOZ_ASSERT(NS_IsMainThread());

  MutexAutoLock lock(mMutex);
  mErrorResult = aRv;

  if (mActor) {
    mActor->SendOperationFailed();
    mActor = nullptr;
  }
}

nsresult MutableBlobStorage::DispatchToIOThread(
    already_AddRefed<nsIRunnable> aRunnable) {
  if (!mTaskQueue) {
    nsCOMPtr<nsIEventTarget> target =
        do_GetService(NS_STREAMTRANSPORTSERVICE_CONTRACTID);
    MOZ_ASSERT(target);

    mTaskQueue = TaskQueue::Create(target.forget(), "BlobStorage");
  }

  nsCOMPtr<nsIRunnable> runnable(aRunnable);
  nsresult rv = mTaskQueue->Dispatch(runnable.forget());
  if (NS_WARN_IF(NS_FAILED(rv))) {
    return rv;
  }

  return NS_OK;
}

size_t MutableBlobStorage::SizeOfCurrentMemoryBuffer() {
  MOZ_ASSERT(NS_IsMainThread());
  MutexAutoLock lock(mMutex);
  return mStorageState < eInTemporaryFile ? mDataLen : 0;
}

PRFileDesc* MutableBlobStorage::GetFD() {
  MOZ_ASSERT(!NS_IsMainThread());
  MutexAutoLock lock(mMutex);
  return mFD;
}

void MutableBlobStorage::CloseFD() {
  MOZ_ASSERT(!NS_IsMainThread());
  MutexAutoLock lock(mMutex);
  MOZ_ASSERT(mFD);

  PR_Close(mFD);
  mFD = nullptr;
}

}  // namespace mozilla::dom
