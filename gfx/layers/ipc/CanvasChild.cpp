/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CanvasChild.h"

#include "MainThreadUtils.h"
#include "mozilla/dom/WorkerPrivate.h"
#include "mozilla/dom/WorkerRef.h"
#include "mozilla/dom/WorkerRunnable.h"
#include "mozilla/gfx/CanvasManagerChild.h"
#include "mozilla/gfx/CanvasShutdownManager.h"
#include "mozilla/gfx/DrawTargetRecording.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/gfx/Tools.h"
#include "mozilla/gfx/Rect.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/ipc/ProcessChild.h"
#include "mozilla/ipc/SharedMemoryHandle.h"
#include "mozilla/layers/CanvasDrawEventRecorder.h"
#include "mozilla/layers/ImageDataSerializer.h"
#include "mozilla/layers/SourceSurfaceSharedData.h"
#include "mozilla/AppShutdown.h"
#include "mozilla/Maybe.h"
#include "mozilla/Mutex.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "nsIObserverService.h"
#include "nsICanvasRenderingContextInternal.h"
#include "RecordedCanvasEventImpl.h"

namespace mozilla {
namespace layers {

class RecorderHelpers final : public CanvasDrawEventRecorder::Helpers {
 public:
  NS_DECL_OWNINGTHREAD

  explicit RecorderHelpers(const RefPtr<CanvasChild>& aCanvasChild)
      : mCanvasChild(aCanvasChild) {}

  ~RecorderHelpers() override = default;

  bool InitTranslator(
      TextureType aTextureType, TextureType aWebglTextureType,
      gfx::BackendType aBackendType,
      ipc::MutableSharedMemoryHandle&& aReadHandle,
      nsTArray<ipc::ReadOnlySharedMemoryHandle>&& aBufferHandles,
      CrossProcessSemaphoreHandle&& aReaderSem,
      CrossProcessSemaphoreHandle&& aWriterSem) override {
    NS_ASSERT_OWNINGTHREAD(RecorderHelpers);
    if (NS_WARN_IF(!mCanvasChild)) {
      return false;
    }
    return mCanvasChild->SendInitTranslator(
        aTextureType, aWebglTextureType, aBackendType, std::move(aReadHandle),
        std::move(aBufferHandles), std::move(aReaderSem),
        std::move(aWriterSem));
  }

  bool AddBuffer(ipc::ReadOnlySharedMemoryHandle&& aBufferHandle) override {
    NS_ASSERT_OWNINGTHREAD(RecorderHelpers);
    if (!mCanvasChild) {
      return false;
    }
    return mCanvasChild->SendAddBuffer(std::move(aBufferHandle));
  }

  bool ReaderClosed() override {
    NS_ASSERT_OWNINGTHREAD(RecorderHelpers);
    if (!mCanvasChild) {
      return false;
    }
    return !mCanvasChild->CanSend() || AppShutdown::IsShutdownImpending();
  }

  bool RestartReader() override {
    NS_ASSERT_OWNINGTHREAD(RecorderHelpers);
    if (!mCanvasChild) {
      return false;
    }
    return mCanvasChild->SendRestartTranslation();
  }

  already_AddRefed<CanvasChild> GetCanvasChild() const override {
    RefPtr<CanvasChild> canvasChild(mCanvasChild);
    return canvasChild.forget();
  }

 private:
  const WeakPtr<CanvasChild> mCanvasChild;
};

class SourceSurfaceCanvasRecording final : public gfx::SourceSurface {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(SourceSurfaceCanvasRecording, final)

  SourceSurfaceCanvasRecording(
      const RemoteTextureOwnerId aTextureOwnerId,
      const RefPtr<gfx::SourceSurface>& aRecordedSuface,
      CanvasChild* aCanvasChild,
      const RefPtr<CanvasDrawEventRecorder>& aRecorder)
      : mTextureOwnerId(aTextureOwnerId),
        mRecordedSurface(aRecordedSuface),
        mCanvasChild(aCanvasChild),
        mRecorder(aRecorder) {
    // It's important that AddStoredObject is called first because that will
    // run any pending processing required by recorded objects that have been
    // deleted off the main thread.
    mRecorder->AddStoredObject(this);
    mRecorder->RecordEvent(RecordedAddSurfaceAlias(this, aRecordedSuface));
  }

  ~SourceSurfaceCanvasRecording() {
    ReferencePtr surfaceAlias = this;
    if (NS_IsMainThread()) {
      ReleaseOnMainThread(std::move(mRecorder), surfaceAlias,
                          std::move(mRecordedSurface), std::move(mCanvasChild));
      return;
    }

    mRecorder->AddPendingDeletion(
        [recorder = std::move(mRecorder), surfaceAlias,
         aliasedSurface = std::move(mRecordedSurface),
         canvasChild = std::move(mCanvasChild)]() mutable -> void {
          ReleaseOnMainThread(std::move(recorder), surfaceAlias,
                              std::move(aliasedSurface),
                              std::move(canvasChild));
        });
  }

  gfx::SurfaceType GetType() const final { return mRecordedSurface->GetType(); }

  gfx::IntSize GetSize() const final { return mRecordedSurface->GetSize(); }

  gfx::SurfaceFormat GetFormat() const final {
    return mRecordedSurface->GetFormat();
  }

  already_AddRefed<gfx::DataSourceSurface> GetDataSurface() final {
    EnsureDataSurfaceOnMainThread();
    return do_AddRef(mDataSourceSurface);
  }

  void AttachSurface() { mDetached = false; }
  void DetachSurface() { mDetached = true; }

  void InvalidateDataSurface() {
    if (mDataSourceSurface && mMayInvalidate) {
      // This must be the only reference to the data left.
      MOZ_ASSERT(mDataSourceSurface->hasOneRef());
      mDataSourceSurface =
          gfx::Factory::CopyDataSourceSurface(mDataSourceSurface);
      mMayInvalidate = false;
    }
  }

  already_AddRefed<gfx::SourceSurface> ExtractSubrect(
      const gfx::IntRect& aRect) final {
    return mRecordedSurface->ExtractSubrect(aRect);
  }

  bool GetSurfaceDescriptor(SurfaceDescriptor& aDesc) const final {
    aDesc = SurfaceDescriptorCanvasSurface(
        static_cast<gfx::CanvasManagerChild*>(mCanvasChild->Manager())->Id(),
        mCanvasChild->Id(), uintptr_t(gfx::ReferencePtr(this)));
    return true;
  }

 private:
  void EnsureDataSurfaceOnMainThread() {
    // The data can only be retrieved on the main thread.
    if (!mDataSourceSurface && NS_IsMainThread()) {
      mDataSourceSurface = mCanvasChild->GetDataSurface(
          mTextureOwnerId, mRecordedSurface, mDetached, mMayInvalidate);
    }
  }

  // Used to ensure that clean-up that requires it is done on the main thread.
  static void ReleaseOnMainThread(RefPtr<CanvasDrawEventRecorder> aRecorder,
                                  ReferencePtr aSurfaceAlias,
                                  RefPtr<gfx::SourceSurface> aAliasedSurface,
                                  RefPtr<CanvasChild> aCanvasChild) {
    MOZ_ASSERT(NS_IsMainThread());

    aRecorder->RemoveStoredObject(aSurfaceAlias);
    aRecorder->RecordEvent(RecordedRemoveSurfaceAlias(aSurfaceAlias));
    aAliasedSurface = nullptr;
    aCanvasChild = nullptr;
    aRecorder = nullptr;
  }

  const RemoteTextureOwnerId mTextureOwnerId;
  RefPtr<gfx::SourceSurface> mRecordedSurface;
  RefPtr<CanvasChild> mCanvasChild;
  RefPtr<CanvasDrawEventRecorder> mRecorder;
  RefPtr<gfx::DataSourceSurface> mDataSourceSurface;
  bool mDetached = false;
  bool mMayInvalidate = false;
};

class CanvasDataShmemHolder {
 public:
  CanvasDataShmemHolder(
      const std::shared_ptr<ipc::ReadOnlySharedMemoryMapping>& aShmem,
      CanvasChild* aCanvasChild)
      : mMutex("CanvasChild::DataShmemHolder::mMutex"),
        mShmem(aShmem),
        mCanvasChild(aCanvasChild) {}

  bool Init(dom::ThreadSafeWorkerRef* aWorkerRef) {
    if (!aWorkerRef) {
      return true;
    }

    RefPtr<dom::StrongWorkerRef> workerRef = dom::StrongWorkerRef::Create(
        aWorkerRef->Private(), "CanvasChild::DataShmemHolder",
        [this]() { DestroyWorker(); });
    if (NS_WARN_IF(!workerRef)) {
      return false;
    }

    MutexAutoLock lock(mMutex);
    mWorkerRef = new dom::ThreadSafeWorkerRef(workerRef);
    return true;
  }

  void Destroy() {
    class DestroyRunnable final : public dom::WorkerThreadRunnable {
     public:
      explicit DestroyRunnable(CanvasDataShmemHolder* aShmemHolder)
          : dom::WorkerThreadRunnable("CanvasDataShmemHolder::Destroy"),
            mShmemHolder(aShmemHolder) {}

      bool WorkerRun(JSContext* aCx,
                     dom::WorkerPrivate* aWorkerPrivate) override {
        mShmemHolder->Destroy();
        return true;
      }

      void PostRun(JSContext* aCx, dom::WorkerPrivate* aWorkerPrivate,
                   bool aRunResult) override {}

      bool PreDispatch(dom::WorkerPrivate* aWorkerPrivate) override {
        return true;
      }

      void PostDispatch(dom::WorkerPrivate* aWorkerPrivate,
                        bool aDispatchResult) override {}

     private:
      CanvasDataShmemHolder* mShmemHolder;
    };

    mMutex.Lock();

    if (mCanvasChild) {
      if (mWorkerRef) {
        if (!mWorkerRef->Private()->IsOnCurrentThread()) {
          auto task = MakeRefPtr<DestroyRunnable>(this);
          dom::WorkerPrivate* worker = mWorkerRef->Private();
          mMutex.Unlock();
          task->Dispatch(worker);
          return;
        }
      } else if (!NS_IsMainThread()) {
        mMutex.Unlock();
        NS_DispatchToMainThread(NS_NewRunnableFunction(
            "CanvasDataShmemHolder::Destroy", [this]() { Destroy(); }));
        return;
      }

      mCanvasChild->ReturnDataSurfaceShmem(std::move(mShmem));
      mCanvasChild = nullptr;
      mWorkerRef = nullptr;
    }

    mMutex.Unlock();
    delete this;
  }

  void DestroyWorker() {
    MutexAutoLock lock(mMutex);
    mCanvasChild = nullptr;
    mWorkerRef = nullptr;
  }

 private:
  Mutex mMutex;
  std::shared_ptr<ipc::ReadOnlySharedMemoryMapping> mShmem;
  RefPtr<CanvasChild> mCanvasChild MOZ_GUARDED_BY(mMutex);
  RefPtr<dom::ThreadSafeWorkerRef> mWorkerRef MOZ_GUARDED_BY(mMutex);
};

CanvasChild::CanvasChild(dom::ThreadSafeWorkerRef* aWorkerRef)
    : mWorkerRef(aWorkerRef) {}

CanvasChild::~CanvasChild() { MOZ_ASSERT(!mWorkerRef); }

static void NotifyCanvasDeviceChanged() {
  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (obs) {
    obs->NotifyObservers(nullptr, "canvas-device-reset", nullptr);
  }
}

ipc::IPCResult CanvasChild::RecvNotifyDeviceChanged() {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  NotifyCanvasDeviceChanged();
  mRecorder->RecordEvent(RecordedDeviceChangeAcknowledged());
  return IPC_OK();
}

ipc::IPCResult CanvasChild::RecvNotifyDeviceReset(
    const nsTArray<RemoteTextureOwnerId>& aOwnerIds) {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  if (auto* manager = gfx::CanvasShutdownManager::MaybeGet()) {
    manager->OnRemoteCanvasReset(aOwnerIds);
  }

  mRecorder->RecordEvent(RecordedDeviceResetAcknowledged());
  return IPC_OK();
}

/* static */ bool CanvasChild::mDeactivated = false;

ipc::IPCResult CanvasChild::RecvDeactivate() {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  RefPtr<CanvasChild> self(this);
  mDeactivated = true;
  if (auto* cm = gfx::CanvasManagerChild::Get()) {
    cm->DeactivateCanvas();
  }
  NotifyCanvasDeviceChanged();
  return IPC_OK();
}

ipc::IPCResult CanvasChild::RecvBlockCanvas() {
  mBlocked = true;
  if (auto* cm = gfx::CanvasManagerChild::Get()) {
    cm->BlockCanvas();
  }
  return IPC_OK();
}

bool CanvasChild::EnsureRecorder(gfx::IntSize aSize, gfx::SurfaceFormat aFormat,
                                 TextureType aTextureType,
                                 TextureType aWebglTextureType) {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  if (!mRecorder) {
    gfx::BackendType backendType =
        gfxPlatform::GetPlatform()->GetPreferredCanvasBackend();
    auto recorder = MakeRefPtr<CanvasDrawEventRecorder>(mWorkerRef);
    if (!recorder->Init(aTextureType, aWebglTextureType, backendType,
                        MakeUnique<RecorderHelpers>(this))) {
      return false;
    }

    mRecorder = recorder.forget();
  }

  if (NS_WARN_IF(mRecorder->GetTextureType() != aTextureType)) {
    // The recorder has already been initialized with a different type. This can
    // happen if there is a device reset or fallback that causes a switch to a
    // different unaccelerated texture type (i.e. unknown). In that case, just
    // fall back to non-remote rendering.
    return false;
  }

  EnsureDataSurfaceShmem(aSize, aFormat);

  return true;
}

void CanvasChild::ActorDestroy(ActorDestroyReason aWhy) {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  if (mRecorder) {
    mRecorder->DetachResources();
  }
  mTextureInfo.clear();
}

void CanvasChild::Destroy() {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  if (CanSend()) {
    Send__delete__(this);
  }

  mWorkerRef = nullptr;
}

bool CanvasChild::EnsureBeginTransaction() {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  if (!mIsInTransaction) {
    RecordEvent(RecordedCanvasBeginTransaction());
    mIsInTransaction = true;
  }

  return true;
}

void CanvasChild::EndTransaction() {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  if (mIsInTransaction) {
    RecordEvent(RecordedCanvasEndTransaction());
    mIsInTransaction = false;
    mDormant = false;
  } else if (mRecorder) {
    // Schedule to drop free buffers if we have no non-empty transactions.
    if (!mDormant) {
      mDormant = true;
      NS_DelayedDispatchToCurrentThread(
          NewRunnableMethod("CanvasChild::DropFreeBuffersWhenDormant", this,
                            &CanvasChild::DropFreeBuffersWhenDormant),
          StaticPrefs::gfx_canvas_remote_drop_buffer_milliseconds());
    }
  }

  // If we are continuously drawing/recording, then we need to periodically
  // flush our external surface/image references, to ensure they actually get
  // freed on a timely basis.
  if (mRecorder) {
    mRecorder->ClearProcessedExternalSurfaces();
    mRecorder->ClearProcessedExternalImages();
  }

  ++mTransactionsSinceGetDataSurface;
}

void CanvasChild::DropFreeBuffersWhenDormant() {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);
  // Drop any free buffers if we have not had any non-empty transactions.
  if (mDormant && mRecorder) {
    mRecorder->DropFreeBuffers();
    // Notify CanvasTranslator it is dormant.
    SendDropFreeBuffersWhenDormant();
  }
}

void CanvasChild::ClearCachedResources() {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);
  if (mRecorder) {
    mRecorder->DropFreeBuffers();
    // Notify CanvasTranslator it is about to be minimized.
    SendClearCachedResources();
  }
}

bool CanvasChild::ShouldBeCleanedUp() const {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  // Always return true if we've been deactivated.
  if (Deactivated()) {
    return true;
  }

  // We can only be cleaned up if nothing else references our recorder.
  return !mRecorder || (mRecorder->hasOneRef() && mTextureInfo.empty());
}

already_AddRefed<gfx::DrawTargetRecording> CanvasChild::CreateDrawTarget(
    const RemoteTextureOwnerId& aTextureOwnerId, gfx::IntSize aSize,
    gfx::SurfaceFormat aFormat) {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);
  MOZ_ASSERT(mTextureInfo.find(aTextureOwnerId) == mTextureInfo.end());

  if (!mRecorder) {
    return nullptr;
  }

  RefPtr<gfx::DrawTarget> dummyDt = gfx::Factory::CreateDrawTarget(
      gfx::BackendType::SKIA, gfx::IntSize(1, 1), aFormat);
  RefPtr<gfx::DrawTargetRecording> dt = MakeAndAddRef<gfx::DrawTargetRecording>(
      mRecorder, aTextureOwnerId, dummyDt, aSize);
  dt->SetOptimizeTransform(true);

  mTextureInfo.insert({aTextureOwnerId, {}});

  return dt.forget();
}

bool CanvasChild::EnsureDataSurfaceShmem(gfx::IntSize aSize,
                                         gfx::SurfaceFormat aFormat) {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  if (!mRecorder) {
    return false;
  }

  size_t sizeRequired =
      ImageDataSerializer::ComputeRGBBufferSize(aSize, aFormat);
  if (!sizeRequired) {
    return false;
  }
  sizeRequired = ipc::shared_memory::PageAlignedSize(sizeRequired);

  if (!mDataSurfaceShmemAvailable || mDataSurfaceShmem->Size() < sizeRequired) {
    RecordEvent(RecordedPauseTranslation());
    auto shmemHandle = ipc::shared_memory::Create(sizeRequired);
    if (!shmemHandle) {
      return false;
    }

    auto roMapping = shmemHandle.AsReadOnly().Map();
    if (!roMapping) {
      return false;
    }

    if (!SendSetDataSurfaceBuffer(std::move(shmemHandle))) {
      return false;
    }

    mDataSurfaceShmem = std::make_shared<ipc::ReadOnlySharedMemoryMapping>(
        std::move(roMapping));
    mDataSurfaceShmemAvailable = true;
  }

  MOZ_ASSERT(mDataSurfaceShmemAvailable);
  return true;
}

void CanvasChild::RecordEvent(const gfx::RecordedEvent& aEvent) {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  // We drop mRecorder in ActorDestroy to break the reference cycle.
  if (!mRecorder) {
    return;
  }

  mRecorder->RecordEvent(aEvent);
}

int64_t CanvasChild::CreateCheckpoint() {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);
  return mRecorder->CreateCheckpoint();
}

already_AddRefed<gfx::DataSourceSurface> CanvasChild::GetDataSurface(
    const RemoteTextureOwnerId aTextureOwnerId,
    const gfx::SourceSurface* aSurface, bool aDetached, bool& aMayInvalidate) {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);
  MOZ_ASSERT(aSurface);

  // mTransactionsSinceGetDataSurface is used to determine if we want to prepare
  // a DataSourceSurface in the GPU process up front at the end of the
  // transaction, but that only makes sense if the canvas JS is requesting data
  // in between transactions.
  if (!mIsInTransaction) {
    mTransactionsSinceGetDataSurface = 0;
  }

  if (!EnsureBeginTransaction()) {
    return nullptr;
  }

  gfx::IntSize ssSize = aSurface->GetSize();
  gfx::SurfaceFormat ssFormat = aSurface->GetFormat();
  auto stride = ImageDataSerializer::ComputeRGBStride(ssFormat, ssSize.width);

  // Shmem is only valid if the surface is the latest snapshot (not detached).
  if (!aDetached) {
    // If there is a shmem associated with this snapshot id, then we want to try
    // use that directly without having to allocate a new shmem for retrieval.
    auto it = mTextureInfo.find(aTextureOwnerId);
    if (it != mTextureInfo.end() && it->second.mSnapshotShmem) {
      const auto* shmemPtr = it->second.mSnapshotShmem->DataAs<uint8_t>();
      MOZ_ASSERT(shmemPtr);
      mRecorder->RecordEvent(RecordedPrepareShmem(aTextureOwnerId));
      auto checkpoint = CreateCheckpoint();
      if (NS_WARN_IF(!mRecorder->WaitForCheckpoint(checkpoint))) {
        return nullptr;
      }
      auto* closure =
          new CanvasDataShmemHolder(it->second.mSnapshotShmem, this);
      if (NS_WARN_IF(!closure->Init(mWorkerRef))) {
        delete closure;
        return nullptr;
      }
      // We can cast away the const of `shmemPtr` to match the call because the
      // DataSourceSurface will not be written to.
      RefPtr<gfx::DataSourceSurface> dataSurface =
          gfx::Factory::CreateWrappingDataSourceSurface(
              const_cast<uint8_t*>(shmemPtr), stride, ssSize, ssFormat,
              ReleaseDataShmemHolder, closure);
      aMayInvalidate = true;
      return dataSurface.forget();
    }
  }

  RecordEvent(RecordedPrepareDataForSurface(aSurface));

  if (!EnsureDataSurfaceShmem(ssSize, ssFormat)) {
    return nullptr;
  }

  RecordEvent(RecordedGetDataForSurface(aSurface));
  auto checkpoint = CreateCheckpoint();
  if (NS_WARN_IF(!mRecorder->WaitForCheckpoint(checkpoint))) {
    return nullptr;
  }

  auto* closure = new CanvasDataShmemHolder(mDataSurfaceShmem, this);
  if (NS_WARN_IF(!closure->Init(mWorkerRef))) {
    delete closure;
    return nullptr;
  }

  mDataSurfaceShmemAvailable = false;

  const auto* data = mDataSurfaceShmem->DataAs<uint8_t>();

  // We can cast away the const of `data` to match the call because the
  // DataSourceSurface will not be written to.
  RefPtr<gfx::DataSourceSurface> dataSurface =
      gfx::Factory::CreateWrappingDataSourceSurface(
          const_cast<uint8_t*>(data), stride, ssSize, ssFormat,
          ReleaseDataShmemHolder, closure);
  aMayInvalidate = false;
  return dataSurface.forget();
}

/* static */ void CanvasChild::ReleaseDataShmemHolder(void* aClosure) {
  auto* shmemHolder = static_cast<CanvasDataShmemHolder*>(aClosure);
  shmemHolder->Destroy();
}

already_AddRefed<gfx::SourceSurface> CanvasChild::WrapSurface(
    const RefPtr<gfx::SourceSurface>& aSurface,
    const RemoteTextureOwnerId aTextureOwnerId) {
  NS_ASSERT_OWNINGTHREAD(CanvasChild);

  if (!aSurface) {
    return nullptr;
  }

  return MakeAndAddRef<SourceSurfaceCanvasRecording>(aTextureOwnerId, aSurface,
                                                     this, mRecorder);
}

void CanvasChild::ReturnDataSurfaceShmem(
    std::shared_ptr<ipc::ReadOnlySharedMemoryMapping>&& aDataSurfaceShmem) {
  // We can only reuse the latest data surface shmem.
  if (aDataSurfaceShmem == mDataSurfaceShmem) {
    MOZ_ASSERT(!mDataSurfaceShmemAvailable);
    mDataSurfaceShmemAvailable = true;
  }
}

void CanvasChild::AttachSurface(const RefPtr<gfx::SourceSurface>& aSurface) {
  if (auto* surface =
          static_cast<SourceSurfaceCanvasRecording*>(aSurface.get())) {
    surface->AttachSurface();
  }
}

void CanvasChild::DetachSurface(const RefPtr<gfx::SourceSurface>& aSurface,
                                bool aInvalidate) {
  if (auto* surface =
          static_cast<SourceSurfaceCanvasRecording*>(aSurface.get())) {
    surface->DetachSurface();
    if (aInvalidate) {
      surface->InvalidateDataSurface();
    }
  }
}

ipc::IPCResult CanvasChild::RecvNotifyRequiresRefresh(
    const RemoteTextureOwnerId aTextureOwnerId) {
  auto it = mTextureInfo.find(aTextureOwnerId);
  if (it != mTextureInfo.end()) {
    it->second.mRequiresRefresh = true;
  }
  return IPC_OK();
}

bool CanvasChild::RequiresRefresh(
    const RemoteTextureOwnerId aTextureOwnerId) const {
  if (mBlocked) {
    return true;
  }
  auto it = mTextureInfo.find(aTextureOwnerId);
  if (it != mTextureInfo.end()) {
    return it->second.mRequiresRefresh;
  }
  return false;
}

ipc::IPCResult CanvasChild::RecvSnapshotShmem(
    const RemoteTextureOwnerId aTextureOwnerId,
    ipc::ReadOnlySharedMemoryHandle&& aShmemHandle,
    SnapshotShmemResolver&& aResolve) {
  auto it = mTextureInfo.find(aTextureOwnerId);
  if (it != mTextureInfo.end()) {
    auto shmem = aShmemHandle.Map();
    if (NS_WARN_IF(!shmem)) {
      shmem = nullptr;
    } else {
      it->second.mSnapshotShmem =
          std::make_shared<ipc::ReadOnlySharedMemoryMapping>(std::move(shmem));
    }
    aResolve(true);
  } else {
    aResolve(false);
  }
  return IPC_OK();
}

ipc::IPCResult CanvasChild::RecvNotifyTextureDestruction(
    const RemoteTextureOwnerId aTextureOwnerId) {
  auto it = mTextureInfo.find(aTextureOwnerId);
  if (it == mTextureInfo.end()) {
    MOZ_ASSERT(!CanSend());
    return IPC_OK();
  }

  mTextureInfo.erase(aTextureOwnerId);
  return IPC_OK();
}

already_AddRefed<gfx::SourceSurface> CanvasChild::SnapshotExternalCanvas(
    gfx::DrawTargetRecording* aTarget,
    nsICanvasRenderingContextInternal* aCanvas,
    mozilla::ipc::IProtocol* aActor) {
  // SnapshotExternalCanvas is only valid to use if using Accelerated Canvas2D
  // with the pending events queue enabled. This ensures WebGL and AC2D are
  // running under the same thread, and that events can be paused or resumed
  // while synchronizing between WebGL and AC2D.
  if (!gfx::gfxVars::UseAcceleratedCanvas2D() ||
      !StaticPrefs::gfx_canvas_remote_use_canvas_translator_event_AtStartup()) {
    return nullptr;
  }

  gfx::SurfaceFormat format = aCanvas->GetIsOpaque()
                                  ? gfx::SurfaceFormat::B8G8R8X8
                                  : gfx::SurfaceFormat::B8G8R8A8;
  gfx::IntSize size(aCanvas->GetWidth(), aCanvas->GetHeight());
  // Create a source sourface that will be associated with the snapshot.
  RefPtr<gfx::SourceSurface> surface =
      aTarget->CreateExternalSourceSurface(size, format);
  if (!surface) {
    return nullptr;
  }

  // Pause translation until the sync-id identifying the snapshot is received.
  uint64_t syncId = ++mLastSyncId;
  mRecorder->RecordEvent(RecordedAwaitTranslationSync(syncId));

  // Flush WebGL to cause any IPDL messages to get sent at this sync point.
  aCanvas->SyncSnapshot();

  // Once the IPDL message is sent to generate the snapshot, resolve the sync-id
  // to a surface in the recording stream. The AwaitTranslationSync above will
  // ensure this event is not translated until the snapshot is generated first.
  mRecorder->RecordEvent(
      RecordedResolveExternalSnapshot(syncId, gfx::ReferencePtr(surface)));

  uint32_t managerId = static_cast<gfx::CanvasManagerChild*>(Manager())->Id();
  int32_t canvasId = aActor->Id();

  // Actually send the request via IPDL to snapshot the external WebGL canvas.
  SendSnapshotExternalCanvas(syncId, managerId, canvasId);

  return surface.forget();
}

}  // namespace layers
}  // namespace mozilla
