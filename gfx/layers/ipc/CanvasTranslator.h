/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef mozilla_layers_CanvasTranslator_h
#define mozilla_layers_CanvasTranslator_h

#include <deque>
#include <unordered_map>
#include <vector>

#include "mozilla/dom/ipc/IdType.h"
#include "mozilla/gfx/InlineTranslator.h"
#include "mozilla/gfx/RecordedEvent.h"
#include "CanvasChild.h"
#include "mozilla/ipc/SharedMemoryHandle.h"
#include "mozilla/layers/CanvasDrawEventRecorder.h"
#include "mozilla/layers/LayersSurfaces.h"
#include "mozilla/layers/PCanvasParent.h"
#include "mozilla/layers/RemoteTextureMap.h"
#include "mozilla/ipc/CrossProcessSemaphore.h"
#include "mozilla/ipc/SharedMemoryMapping.h"
#include "mozilla/Monitor.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/Variant.h"

namespace mozilla {

using EventType = gfx::RecordedEvent::EventType;
class TaskQueue;

namespace gfx {
class DataSourceSurfaceWrapper;
class DrawTargetWebgl;
class SharedContextWebgl;
}  // namespace gfx

namespace layers {

class SharedSurfacesHolder;
class TextureData;
class TextureHost;
class VideoProcessorD3D11;

class CanvasTranslator final : public gfx::InlineTranslator,
                               public PCanvasParent {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(CanvasTranslator)

  friend class PProtocolParent;

  CanvasTranslator(layers::SharedSurfacesHolder* aSharedSurfacesHolder,
                   const dom::ContentParentId& aContentId, uint32_t aManagerId);

  const dom::ContentParentId& GetContentId() const { return mContentId; }

  uint32_t GetManagerId() const { return mManagerId; }

  /**
   * Dispatches a runnable to the preferred task queue or thread.
   *
   * @param aRunnable the runnable to dispatch
   */
  void DispatchToTaskQueue(already_AddRefed<nsIRunnable> aRunnable);

  /**
   * @returns true if running in the preferred task queue or thread for
   * translation.
   */
  bool IsInTaskQueue() const;

  /**
   * Initialize the canvas translator for a particular TextureType and
   * CanvasEventRingBuffer.
   *
   * @param aTextureType the TextureType the translator will create
   * @param aWebglTextureType the TextureType of any WebGL buffers
   * @param aBackendType the BackendType for texture data
   * @param aHeaderHandle handle for the control header
   * @param aBufferHandles handles for the initial buffers for translation
   * @param aBufferSize size of buffers and the default size
   * @param aReaderSem reading blocked semaphore for the CanvasEventRingBuffer
   * @param aWriterSem writing blocked semaphore for the CanvasEventRingBuffer
   */
  ipc::IPCResult RecvInitTranslator(
      TextureType aTextureType, TextureType aWebglTextureType,
      gfx::BackendType aBackendType,
      ipc::MutableSharedMemoryHandle&& aReadHandle,
      nsTArray<ipc::ReadOnlySharedMemoryHandle>&& aBufferHandles,
      CrossProcessSemaphoreHandle&& aReaderSem,
      CrossProcessSemaphoreHandle&& aWriterSem);

  /**
   * Restart the translation from a Stopped state.
   */
  ipc::IPCResult RecvRestartTranslation();

  /**
   * Adds a new buffer to be translated. The current buffer will be recycled if
   * it is of the default size. The translation will then be restarted.
   */
  ipc::IPCResult RecvAddBuffer(ipc::ReadOnlySharedMemoryHandle&& aBufferHandle);

  /**
   * Sets the shared memory to be used for readback.
   */
  ipc::IPCResult RecvSetDataSurfaceBuffer(
      ipc::MutableSharedMemoryHandle&& aBufferHandle);

  ipc::IPCResult RecvClearCachedResources();

  ipc::IPCResult RecvDropFreeBuffersWhenDormant();

  void ActorDestroy(ActorDestroyReason why) final;

  void CheckAndSignalWriter();

  /**
   * Translates events until no more are available or the end of a transaction
   * If this returns false the caller of this is responsible for re-calling
   * this function.
   * @returns true if next HandleCanvasTranslatorEvents() needs to call
   * TranslateRecording().
   */
  bool TranslateRecording();

  /**
   * Marks the beginning of rendering for a transaction. While in a transaction
   * the translator will wait for a short time for events before returning.
   * When not in a transaction the translator will only translate one event at a
   * time.
   */
  void BeginTransaction();

  /**
   * Marks the end of a transaction.
   */
  void EndTransaction();

  /**
   * Flushes canvas drawing, for example to a device.
   */
  void Flush();

  /**
   * Marks that device change processing in the writing process has finished.
   */
  void DeviceChangeAcknowledged();

  /**
   * Marks that device reset processing in the writing process has finished.
   */
  void DeviceResetAcknowledged();

  /**
   * Used during playback of events to create DrawTargets. For the
   * CanvasTranslator this means creating TextureDatas and getting the
   * DrawTargets from those.
   *
   * @param aRefPtr the key to store the created DrawTarget against
   * @param aTextureOwnerId texture owner ID for this DrawTarget
   * @param aSize the size of the DrawTarget
   * @param aFormat the surface format for the DrawTarget
   * @returns the new DrawTarget
   */
  already_AddRefed<gfx::DrawTarget> CreateDrawTarget(
      gfx::ReferencePtr aRefPtr, RemoteTextureOwnerId aTextureOwnerId,
      const gfx::IntSize& aSize, gfx::SurfaceFormat aFormat);

  already_AddRefed<gfx::DrawTarget> CreateDrawTarget(
      gfx::ReferencePtr aRefPtr, const gfx::IntSize& aSize,
      gfx::SurfaceFormat aFormat) final;

  already_AddRefed<gfx::GradientStops> GetOrCreateGradientStops(
      gfx::DrawTarget* aDrawTarget, gfx::GradientStop* aRawStops,
      uint32_t aNumStops, gfx::ExtendMode aExtendMode) final;

  void CheckpointReached();

  void PauseTranslation();

  /**
   * Wait for a given sync-id to be encountered before resume translation.
   */
  void AwaitTranslationSync(uint64_t aSyncId);

  /**
   * Signal that translation should resume if waiting on the given sync-id.
   */
  void SyncTranslation(uint64_t aSyncId);

  /**
   * Snapshot an external canvas and label it for later lookup under a sync-id.
   */
  mozilla::ipc::IPCResult RecvSnapshotExternalCanvas(uint64_t aSyncId,
                                                     uint32_t aManagerId,
                                                     int32_t aCanvasId);

  /**
   * Resolves the given sync-id from the recording stream to a snapshot from
   * an external canvas that was received from an IPDL message.
   */
  already_AddRefed<gfx::SourceSurface> LookupExternalSnapshot(uint64_t aSyncId);

  /**
   * Removes the texture and other objects associated with a texture ID.
   *
   * @param aTextureOwnerId the texture ID to remove
   */
  void RemoveTexture(const RemoteTextureOwnerId aTextureOwnerId,
                     RemoteTextureTxnType aTxnType = 0,
                     RemoteTextureTxnId aTxnId = 0);

  bool LockTexture(const RemoteTextureOwnerId aTextureOwnerId, OpenMode aMode,
                   bool aInvalidContents = false);
  bool UnlockTexture(const RemoteTextureOwnerId aTextureOwnerId);

  bool PresentTexture(const RemoteTextureOwnerId aTextureOwnerId,
                      RemoteTextureId aId);

  bool PushRemoteTexture(const RemoteTextureOwnerId aTextureOwnerId,
                         TextureData* aData, RemoteTextureId aId,
                         RemoteTextureOwnerId aOwnerId);

  /**
   * Overriden to remove any DataSourceSurfaces associated with the RefPtr.
   *
   * @param aRefPtr the key to the surface
   * @param aSurface the surface to store
   */
  void AddSourceSurface(gfx::ReferencePtr aRefPtr,
                        gfx::SourceSurface* aSurface) final {
    if (mMappedSurface == aRefPtr) {
      mPreparedMap = nullptr;
      mMappedSurface = nullptr;
    }
    RemoveDataSurface(aRefPtr);
    InlineTranslator::AddSourceSurface(aRefPtr, aSurface);
  }

  /**
   * Removes the SourceSurface and other objects associated with a SourceSurface
   * from another process.
   *
   * @param aRefPtr the key to the objects to remove
   */
  void RemoveSourceSurface(gfx::ReferencePtr aRefPtr) final {
    if (mMappedSurface == aRefPtr) {
      mPreparedMap = nullptr;
      mMappedSurface = nullptr;
    }
    RemoveDataSurface(aRefPtr);
    InlineTranslator::RemoveSourceSurface(aRefPtr);
  }

  already_AddRefed<gfx::SourceSurface> LookupExternalSurface(
      uint64_t aKey) final;

  already_AddRefed<gfx::SourceSurface> LookupSourceSurfaceFromSurfaceDescriptor(
      const SurfaceDescriptor& aDesc) final;

  /**
   * Gets the cached DataSourceSurface, if it exists, associated with a
   * SourceSurface from another process.
   *
   * @param aRefPtr the key used to find the DataSourceSurface
   * @returns the DataSourceSurface or nullptr if not found
   */
  gfx::DataSourceSurface* LookupDataSurface(gfx::ReferencePtr aRefPtr);

  /**
   * Used to cache the DataSourceSurface from a SourceSurface associated with a
   * SourceSurface from another process. This is to improve performance if we
   * require the data for that SourceSurface.
   *
   * @param aRefPtr the key used to store the DataSourceSurface
   * @param aSurface the DataSourceSurface to store
   */
  void AddDataSurface(gfx::ReferencePtr aRefPtr,
                      RefPtr<gfx::DataSourceSurface>&& aSurface);

  /**
   * Gets the cached DataSourceSurface, if it exists, associated with a
   * SourceSurface from another process.
   *
   * @param aRefPtr the key used to find the DataSourceSurface
   * @returns the DataSourceSurface or nullptr if not found
   */
  void RemoveDataSurface(gfx::ReferencePtr aRefPtr);

  /**
   * Sets a ScopedMap, to be used in a later event.
   *
   * @param aSurface the associated surface in the other process
   * @param aMap the ScopedMap to store
   */
  void SetPreparedMap(gfx::ReferencePtr aSurface,
                      UniquePtr<gfx::DataSourceSurface::ScopedMap> aMap);

  /**
   * Gets the ScopedMap stored using SetPreparedMap.
   *
   * @param aSurface must match the surface from the SetPreparedMap call
   * @returns the ScopedMap if aSurface matches otherwise nullptr
   */
  UniquePtr<gfx::DataSourceSurface::ScopedMap> GetPreparedMap(
      gfx::ReferencePtr aSurface);

  void PrepareShmem(const RemoteTextureOwnerId aTextureOwnerId);

  void RecycleBuffer();

  void NextBuffer();

  void GetDataSurface(uint64_t aSurfaceRef);

  /**
   * Wait for a canvas to produce the designated surface. If necessary,
   * this may flush out canvas commands to ensure the surface is created.
   * This should only be called from within the canvas task queue thread
   * so that it can force event processing to occur if necessary.
   */
  already_AddRefed<gfx::DataSourceSurface> WaitForSurface(uintptr_t aId);

  static void Shutdown();

 private:
  ~CanvasTranslator();

  class CanvasTranslatorEvent {
   public:
    enum class Tag {
      TranslateRecording,
      AddBuffer,
      SetDataSurfaceBuffer,
      ClearCachedResources,
      DropFreeBuffersWhenDormant,
    };
    const Tag mTag;

   private:
    Variant<ipc::ReadOnlySharedMemoryHandle, ipc::MutableSharedMemoryHandle>
        mBufferHandle;

   public:
    explicit CanvasTranslatorEvent(const Tag aTag)
        : mTag(aTag), mBufferHandle(ipc::ReadOnlySharedMemoryHandle()) {
      MOZ_ASSERT(mTag == Tag::TranslateRecording ||
                 mTag == Tag::ClearCachedResources ||
                 mTag == Tag::DropFreeBuffersWhenDormant);
    }
    CanvasTranslatorEvent(const Tag aTag,
                          ipc::ReadOnlySharedMemoryHandle&& aBufferHandle)
        : mTag(aTag), mBufferHandle(std::move(aBufferHandle)) {
      MOZ_ASSERT(mTag == Tag::AddBuffer);
    }
    CanvasTranslatorEvent(const Tag aTag,
                          ipc::MutableSharedMemoryHandle&& aBufferHandle)
        : mTag(aTag), mBufferHandle(std::move(aBufferHandle)) {
      MOZ_ASSERT(mTag == Tag::SetDataSurfaceBuffer);
    }

    static UniquePtr<CanvasTranslatorEvent> TranslateRecording() {
      return MakeUnique<CanvasTranslatorEvent>(Tag::TranslateRecording);
    }

    static UniquePtr<CanvasTranslatorEvent> AddBuffer(
        ipc::ReadOnlySharedMemoryHandle&& aBufferHandle) {
      return MakeUnique<CanvasTranslatorEvent>(Tag::AddBuffer,
                                               std::move(aBufferHandle));
    }

    static UniquePtr<CanvasTranslatorEvent> SetDataSurfaceBuffer(
        ipc::MutableSharedMemoryHandle&& aBufferHandle) {
      return MakeUnique<CanvasTranslatorEvent>(Tag::SetDataSurfaceBuffer,
                                               std::move(aBufferHandle));
    }

    static UniquePtr<CanvasTranslatorEvent> ClearCachedResources() {
      return MakeUnique<CanvasTranslatorEvent>(Tag::ClearCachedResources);
    }

    static UniquePtr<CanvasTranslatorEvent> DropFreeBuffersWhenDormant() {
      return MakeUnique<CanvasTranslatorEvent>(Tag::DropFreeBuffersWhenDormant);
    }

    ipc::ReadOnlySharedMemoryHandle TakeBufferHandle() {
      if (mTag == Tag::AddBuffer) {
        return std::move(mBufferHandle).as<ipc::ReadOnlySharedMemoryHandle>();
      }
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      return nullptr;
    }

    ipc::MutableSharedMemoryHandle TakeDataSurfaceBufferHandle() {
      if (mTag == Tag::SetDataSurfaceBuffer) {
        return std::move(mBufferHandle).as<ipc::MutableSharedMemoryHandle>();
      }
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      return nullptr;
    }
  };

  /*
   * @returns true if next HandleCanvasTranslatorEvents() needs to call
   * TranslateRecording().
   */
  bool AddBuffer(ipc::ReadOnlySharedMemoryHandle&& aBufferHandle);

  /*
   * @returns true if next HandleCanvasTranslatorEvents() needs to call
   * TranslateRecording().
   */
  bool SetDataSurfaceBuffer(ipc::MutableSharedMemoryHandle&& aBufferHandle);

  bool ReadNextEvent(EventType& aEventType);

  bool HasPendingEvent();

  bool ReadPendingEvent(EventType& aEventType);

  bool CheckDeactivated();

  void Deactivate();

  bool TryDrawTargetWebglFallback(const RemoteTextureOwnerId aTextureOwnerId,
                                  gfx::DrawTargetWebgl* aWebgl);
  void ForceDrawTargetWebglFallback();

  void BlockCanvas();

  UniquePtr<TextureData> CreateTextureData(const gfx::IntSize& aSize,
                                           gfx::SurfaceFormat aFormat,
                                           bool aClear);

  void EnsureRemoteTextureOwner(
      RemoteTextureOwnerId aOwnerId = RemoteTextureOwnerId());

  UniquePtr<TextureData> CreateOrRecycleTextureData(const gfx::IntSize& aSize,
                                                    gfx::SurfaceFormat aFormat);

  already_AddRefed<gfx::DrawTarget> CreateFallbackDrawTarget(
      gfx::ReferencePtr aRefPtr, RemoteTextureOwnerId aTextureOwnerId,
      const gfx::IntSize& aSize, gfx::SurfaceFormat aFormat);

  void ClearTextureInfo();

  bool HandleExtensionEvent(int32_t aType);

  bool CreateReferenceTexture();
  bool CheckForFreshCanvasDevice(int aLineNumber);
  void NotifyDeviceChanged();

  void NotifyDeviceReset(const RemoteTextureOwnerIdSet& aIds);
  bool EnsureSharedContextWebgl();
  gfx::DrawTargetWebgl* GetDrawTargetWebgl(
      const RemoteTextureOwnerId aTextureOwnerId,
      bool aCheckForFallback = true) const;
  void NotifyRequiresRefresh(const RemoteTextureOwnerId aTextureOwnerId,
                             bool aDispatch = true);
  void CacheSnapshotShmem(const RemoteTextureOwnerId aTextureOwnerId,
                          bool aDispatch = true);

  void CacheDataSnapshots();

  void ClearCachedResources();

  void DropFreeBuffersWhenDormant();

  already_AddRefed<gfx::DataSourceSurface>
  MaybeRecycleDataSurfaceForSurfaceDescriptor(
      TextureHost* aTextureHost,
      const SurfaceDescriptorRemoteDecoder& aSurfaceDescriptor);

  bool UsePendingCanvasTranslatorEvents();
  void PostCanvasTranslatorEvents(const MutexAutoLock& aProofOfLock);
  void HandleCanvasTranslatorEvents();

  void NotifyTextureDestruction(const RemoteTextureOwnerId aTextureOwnerId);

  const RefPtr<TaskQueue> mTranslationTaskQueue;
  const RefPtr<SharedSurfacesHolder> mSharedSurfacesHolder;
#if defined(XP_WIN)
  RefPtr<ID3D11Device> mDevice;
  DataMutex<RefPtr<VideoProcessorD3D11>> mVideoProcessorD3D11;
#endif
  static StaticRefPtr<gfx::SharedContextWebgl> sSharedContext;
  RefPtr<gfx::SharedContextWebgl> mSharedContext;
  RefPtr<RemoteTextureOwnerClient> mRemoteTextureOwner;

  size_t mDefaultBufferSize = 0;
  uint32_t mMaxSpinCount;
  TimeDuration mNextEventTimeout;

  using State = CanvasDrawEventRecorder::State;
  using Header = CanvasDrawEventRecorder::Header;

  ipc::SharedMemoryMapping mHeaderShmem;
  Header* mHeader = nullptr;
  // Limit event processing to stop at the designated checkpoint, rather than
  // proceed beyond it. This also forces processing to continue, even when it
  // would normally have been interrupted, so long as not error is produced and
  // so long as the checkpoint has not yet been reached.
  int64_t mFlushCheckpoint = 0;

  // The sync-id that the translator is awaiting and must be encountered before
  // it is ready to resume translation.
  uint64_t mAwaitSyncId = 0;
  // The last sync-id that was actually encountered.
  uint64_t mLastSyncId = 0;
  // A table of external canvas snapshots associated with a given sync-id.
  nsRefPtrHashtable<nsUint64HashKey, gfx::SourceSurface> mExternalSnapshots;

  // Signal that translation should pause because it is still awaiting a sync-id
  // that has not been encountered yet.
  bool PauseUntilSync() const { return mAwaitSyncId > mLastSyncId; }

  struct CanvasShmem {
    ipc::ReadOnlySharedMemoryMapping shmem;
    bool IsValid() const { return shmem.IsValid(); }
    auto Size() { return shmem ? shmem.Size() : 0; }
    gfx::MemReader CreateMemReader() {
      if (!shmem) {
        return {nullptr, 0};
      }
      return {shmem.DataAs<char>(), Size()};
    }
  };
  std::queue<CanvasShmem> mCanvasShmems;
  CanvasShmem mCurrentShmem;
  gfx::MemReader mCurrentMemReader{0, 0};
  ipc::SharedMemoryMapping mDataSurfaceShmem;
  UniquePtr<CrossProcessSemaphore> mWriterSemaphore;
  UniquePtr<CrossProcessSemaphore> mReaderSemaphore;
  TextureType mTextureType = TextureType::Unknown;
  TextureType mWebglTextureType = TextureType::Unknown;
  UniquePtr<TextureData> mReferenceTextureData;
  dom::ContentParentId mContentId;
  uint32_t mManagerId;
  // Sometimes during device reset our reference DrawTarget can be null, so we
  // hold the BackendType separately.
  gfx::BackendType mBackendType = gfx::BackendType::NONE;
  base::ProcessId mOtherPid = base::kInvalidProcessId;
  struct TextureInfo {
    gfx::ReferencePtr mRefPtr;
    UniquePtr<TextureData> mTextureData;
    RefPtr<gfx::DrawTarget> mDrawTarget;
    bool mNotifiedRequiresRefresh = false;
    // Ref-count of how active uses of the DT. Avoids deletion when locked.
    int32_t mLocked = 1;
    OpenMode mTextureLockMode = OpenMode::OPEN_NONE;

    gfx::DrawTargetWebgl* GetDrawTargetWebgl(
        bool aCheckForFallback = true) const;
  };
  std::unordered_map<RemoteTextureOwnerId, TextureInfo,
                     RemoteTextureOwnerId::HashFn>
      mTextureInfo;
  nsRefPtrHashtable<nsPtrHashKey<void>, gfx::DataSourceSurface> mDataSurfaces;
  gfx::ReferencePtr mMappedSurface;
  UniquePtr<gfx::DataSourceSurface::ScopedMap> mPreparedMap;
  Atomic<bool> mDeactivated{false};
  Atomic<bool> mBlocked{false};
  Atomic<bool> mIPDLClosed{false};
  bool mIsInTransaction = false;
  bool mDeviceResetInProgress = false;

  RefPtr<gfx::DataSourceSurface> mUsedDataSurfaceForSurfaceDescriptor;
  RefPtr<gfx::DataSourceSurfaceWrapper> mUsedWrapperForSurfaceDescriptor;
  Maybe<SurfaceDescriptorRemoteDecoder>
      mUsedSurfaceDescriptorForSurfaceDescriptor;

  Mutex mCanvasTranslatorEventsLock;
  RefPtr<nsIRunnable> mCanvasTranslatorEventsRunnable;
  std::deque<UniquePtr<CanvasTranslatorEvent>> mPendingCanvasTranslatorEvents;
};

}  // namespace layers
}  // namespace mozilla

#endif  // mozilla_layers_CanvasTranslator_h
