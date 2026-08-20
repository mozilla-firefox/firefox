/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "CanvasTranslator.h"

#include "GLContext.h"
#include "HostWebGLContext.h"
#include "RecordedCanvasEventImpl.h"
#include "SharedSurface.h"
#include "WebGLParent.h"
#include "gfxGradientCache.h"
#include "mozilla/DataMutex.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/SyncRunnable.h"
#include "mozilla/TaskQueue.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/CanvasManagerParent.h"
#include "mozilla/gfx/CanvasRenderThread.h"
#include "mozilla/gfx/DataSourceSurfaceWrapper.h"
#include "mozilla/gfx/DrawTargetWebgl.h"
#include "mozilla/gfx/GPUParent.h"
#include "mozilla/gfx/GPUProcessManager.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/Swizzle.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/ipc/Endpoint.h"
#include "mozilla/ipc/SharedMemoryHandle.h"
#include "mozilla/layers/BufferTexture.h"
#include "mozilla/layers/CanvasTranslator.h"
#include "mozilla/layers/ImageDataSerializer.h"
#include "mozilla/layers/SharedSurfacesParent.h"
#include "mozilla/layers/TextureClient.h"
#include "mozilla/layers/VideoBridgeParent.h"

#if defined(XP_WIN)
#  include "mozilla/gfx/DeviceManagerDx.h"
#  include "mozilla/layers/TextureD3D11.h"
#endif

namespace mozilla {
namespace layers {

UniquePtr<TextureData> CanvasTranslator::CreateTextureData(
    const gfx::IntSize& aSize, gfx::SurfaceFormat aFormat, bool aClear) {
  TextureData* textureData = nullptr;
  TextureAllocationFlags allocFlags =
      aClear ? ALLOC_CLEAR_BUFFER : ALLOC_DEFAULT;
  switch (mTextureType) {
    case TextureType::Unknown:
      textureData = BufferTextureData::Create(
          aSize, aFormat, gfx::ColorSpace2::SRGB, gfx::TransferFunction::SRGB,
          gfx::BackendType::SKIA, LayersBackend::LAYERS_WR,
          TextureFlags::DEALLOCATE_CLIENT | TextureFlags::REMOTE_TEXTURE,
          allocFlags, nullptr);
      break;
    default:
      textureData = TextureData::Create(mTextureType, aFormat, aSize,
                                        allocFlags, mBackendType);
      break;
  }

  return WrapUnique(textureData);
}

CanvasTranslator::CanvasTranslator(
    layers::SharedSurfacesHolder* aSharedSurfacesHolder,
    const dom::ContentParentId& aContentId, uint32_t aManagerId)
    : mSharedSurfacesHolder(aSharedSurfacesHolder),
      mMaxSpinCount(StaticPrefs::gfx_canvas_remote_max_spin_count()),
      mContentId(aContentId),
      mManagerId(aManagerId),
      mCanvasTranslatorEventsLock(
          "CanvasTranslator::mCanvasTranslatorEventsLock") {
  mNextEventTimeout = TimeDuration::FromMilliseconds(
      StaticPrefs::gfx_canvas_remote_event_timeout_ms());
}

CanvasTranslator::~CanvasTranslator() = default;

void CanvasTranslator::DispatchToTaskQueue(
    already_AddRefed<nsIRunnable> aRunnable) {
  gfx::CanvasRenderThread::Dispatch(std::move(aRunnable));
}

bool CanvasTranslator::IsInTaskQueue() const {
  return gfx::CanvasRenderThread::IsInCanvasRenderThread();
}

StaticRefPtr<gfx::SharedContextWebgl> CanvasTranslator::sSharedContext;

bool CanvasTranslator::EnsureSharedContextWebgl() {
  if (!mSharedContext || mSharedContext->IsContextLost()) {
    if (mSharedContext) {
      ForceDrawTargetWebglFallback();
      if (mRemoteTextureOwner) {
        // Ensure any shared surfaces referring to the old context go away.
        mRemoteTextureOwner->ClearRecycledTextures();
      }
    }
    // Check if the global shared context is still valid. If not, instantiate
    // a new one before we try to use it.
    if (!sSharedContext || sSharedContext->IsContextLost()) {
      sSharedContext = gfx::SharedContextWebgl::Create();
    }
    mSharedContext = sSharedContext;
    // If we can't get a new context, then the only thing left to do is block
    // new canvases.
    if (!mSharedContext || mSharedContext->IsContextLost()) {
      mSharedContext = nullptr;
      BlockCanvas();
      return false;
    }
  }
  return true;
}

void CanvasTranslator::Shutdown() {
  if (sSharedContext) {
    gfx::CanvasRenderThread::Dispatch(NS_NewRunnableFunction(
        "CanvasTranslator::Shutdown", []() { sSharedContext = nullptr; }));
  }
}

mozilla::ipc::IPCResult CanvasTranslator::RecvInitTranslator(
    TextureType aTextureType, TextureType aWebglTextureType,
    gfx::BackendType aBackendType, ipc::MutableSharedMemoryHandle&& aReadHandle,
    nsTArray<ipc::ReadOnlySharedMemoryHandle>&& aBufferHandles,
    CrossProcessSemaphoreHandle&& aReaderSem,
    CrossProcessSemaphoreHandle&& aWriterSem) {
  if (mHeaderShmem) {
    return IPC_FAIL(this, "RecvInitTranslator called twice.");
  }

  mTextureType = aTextureType;
  mWebglTextureType = aWebglTextureType;
  mBackendType = aBackendType;
  mOtherPid = OtherPid();

  mHeaderShmem = aReadHandle.Map();
  if (!mHeaderShmem) {
    Deactivate();
    return IPC_FAIL(this, "Failed to map canvas header shared memory.");
  }

  mHeader = mHeaderShmem.DataAs<Header>();

  mWriterSemaphore.reset(CrossProcessSemaphore::Create(std::move(aWriterSem)));
  mWriterSemaphore->CloseHandle();

  mReaderSemaphore.reset(CrossProcessSemaphore::Create(std::move(aReaderSem)));
  mReaderSemaphore->CloseHandle();

  if (!CreateReferenceTexture()) {
    gfxCriticalNote << "GFX: CanvasTranslator failed to get device";
    Deactivate();
    return IPC_OK();
  }

  if (gfx::gfxVars::UseAcceleratedCanvas2D() && !EnsureSharedContextWebgl()) {
    gfxCriticalNote
        << "GFX: CanvasTranslator failed creating WebGL shared context";
  }

  // Use the first buffer as our current buffer.
  if (aBufferHandles.IsEmpty()) {
    Deactivate();
    return IPC_FAIL(this, "No canvas buffer shared memory supplied.");
  }
  mDefaultBufferSize = aBufferHandles[0].Size();
  auto handleIter = aBufferHandles.begin();
  mCurrentShmem.shmem = std::move(*handleIter).Map();
  if (!mCurrentShmem.shmem) {
    Deactivate();
    return IPC_FAIL(this, "Failed to map canvas buffer shared memory.");
  }
  mCurrentMemReader = mCurrentShmem.CreateMemReader();

  // Add all other buffers to our recycled CanvasShmems.
  for (handleIter++; handleIter < aBufferHandles.end(); handleIter++) {
    CanvasShmem newShmem;
    newShmem.shmem = std::move(*handleIter).Map();
    if (!newShmem.shmem) {
      Deactivate();
      return IPC_FAIL(this, "Failed to map canvas buffer shared memory.");
    }
    mCanvasShmems.emplace(std::move(newShmem));
  }

  if (UsePendingCanvasTranslatorEvents()) {
    MutexAutoLock lock(mCanvasTranslatorEventsLock);
    mPendingCanvasTranslatorEvents.push_back(
        CanvasTranslatorEvent::TranslateRecording());
    PostCanvasTranslatorEvents(lock);
  } else {
    DispatchToTaskQueue(
        NewRunnableMethod("CanvasTranslator::TranslateRecording", this,
                          &CanvasTranslator::TranslateRecording));
  }
  return IPC_OK();
}

ipc::IPCResult CanvasTranslator::RecvRestartTranslation() {
  if (mDeactivated) {
    // The other side might have sent a message before we deactivated.
    return IPC_OK();
  }

  if (UsePendingCanvasTranslatorEvents()) {
    MutexAutoLock lock(mCanvasTranslatorEventsLock);
    mPendingCanvasTranslatorEvents.push_back(
        CanvasTranslatorEvent::TranslateRecording());
    PostCanvasTranslatorEvents(lock);
  } else {
    DispatchToTaskQueue(
        NewRunnableMethod("CanvasTranslator::TranslateRecording", this,
                          &CanvasTranslator::TranslateRecording));
  }

  return IPC_OK();
}

ipc::IPCResult CanvasTranslator::RecvAddBuffer(
    ipc::ReadOnlySharedMemoryHandle&& aBufferHandle) {
  if (mDeactivated) {
    // The other side might have sent a resume message before we deactivated.
    return IPC_OK();
  }

  if (UsePendingCanvasTranslatorEvents()) {
    MutexAutoLock lock(mCanvasTranslatorEventsLock);
    mPendingCanvasTranslatorEvents.push_back(
        CanvasTranslatorEvent::AddBuffer(std::move(aBufferHandle)));
    PostCanvasTranslatorEvents(lock);
  } else {
    DispatchToTaskQueue(NewRunnableMethod<ipc::ReadOnlySharedMemoryHandle&&>(
        "CanvasTranslator::AddBuffer", this, &CanvasTranslator::AddBuffer,
        std::move(aBufferHandle)));
  }

  return IPC_OK();
}

bool CanvasTranslator::AddBuffer(
    ipc::ReadOnlySharedMemoryHandle&& aBufferHandle) {
  MOZ_ASSERT(IsInTaskQueue());
  if (mHeader->readerState == State::Failed) {
    // We failed before we got to the pause event.
    return false;
  }

  if (mHeader->readerState != State::Paused) {
    gfxCriticalNote << "CanvasTranslator::AddBuffer bad state "
                    << uint32_t(State(mHeader->readerState));
#ifndef FUZZING_SNAPSHOT
    MOZ_DIAGNOSTIC_CRASH("mHeader->readerState == State::Paused");
#endif
    Deactivate();
    return false;
  }

  MOZ_ASSERT(mDefaultBufferSize != 0);

  // Check and signal the writer when we finish with a buffer, because it
  // might have hit the buffer count limit and be waiting to use our old one.
  CheckAndSignalWriter();

  // Default sized buffers will have been queued for recycling.
  if (mCurrentShmem.IsValid() && mCurrentShmem.Size() == mDefaultBufferSize) {
    mCanvasShmems.emplace(std::move(mCurrentShmem));
  }

  CanvasShmem newShmem;
  newShmem.shmem = aBufferHandle.Map();
  if (!newShmem.shmem) {
    return false;
  }

  mCurrentShmem = std::move(newShmem);
  mCurrentMemReader = mCurrentShmem.CreateMemReader();

  return TranslateRecording();
}

ipc::IPCResult CanvasTranslator::RecvSetDataSurfaceBuffer(
    uint32_t aId, ipc::MutableSharedMemoryHandle&& aBufferHandle) {
  if (mDeactivated) {
    // The other side might have sent a resume message before we deactivated.
    return IPC_OK();
  }

  if (UsePendingCanvasTranslatorEvents()) {
    MutexAutoLock lock(mCanvasTranslatorEventsLock);
    mPendingCanvasTranslatorEvents.push_back(
        CanvasTranslatorEvent::SetDataSurfaceBuffer(aId,
                                                    std::move(aBufferHandle)));
    PostCanvasTranslatorEvents(lock);
  } else {
    DispatchToTaskQueue(
        NewRunnableMethod<uint32_t, ipc::MutableSharedMemoryHandle&&>(
            "CanvasTranslator::SetDataSurfaceBuffer", this,
            &CanvasTranslator::SetDataSurfaceBuffer, aId,
            std::move(aBufferHandle)));
  }

  return IPC_OK();
}

void CanvasTranslator::UnlinkDataSurfaceShmemOwner(
    const RefPtr<gfx::DataSourceSurface>& aSurface) {
  if (!aSurface) {
    return;
  }
  // Remove the associated id key
  aSurface->RemoveUserData(&mDataSurfaceShmemIdKey);
  // Force copy-on-write of contained shmem if applicable
  gfx::DataSourceSurface::ScopedMap map(
      aSurface, gfx::DataSourceSurface::MapType::READ_WRITE);
}

void CanvasTranslator::DataSurfaceBufferWillChange(uint32_t aId,
                                                   bool aKeepAlive,
                                                   size_t aLimit) {
  if (aId) {
    // If a specific id is changing, then attempt to copy it.
    auto it = mDataSurfaceShmems.find(aId);
    if (it != mDataSurfaceShmems.end()) {
      RefPtr<gfx::DataSourceSurface> owner(it->second.mOwner);
      if (owner) {
        UnlinkDataSurfaceShmemOwner(owner);
        it->second.mOwner = nullptr;
      }
      // Erase the shmem if it's not the last used id.
      if (!aKeepAlive || aId != mLastDataSurfaceShmemId) {
        mDataSurfaceShmems.erase(it);
      }
    }
  } else {
    // If no limit is requested, clear everything.
    if (!aLimit) {
      aLimit = mDataSurfaceShmems.size();
    }
    // Ensure all shmems still alive are copied.
    DataSurfaceShmem lastShmem;
    auto it = mDataSurfaceShmems.begin();
    for (; aLimit > 0 && it != mDataSurfaceShmems.end(); ++it, --aLimit) {
      RefPtr<gfx::DataSourceSurface> owner(it->second.mOwner);
      if (owner) {
        UnlinkDataSurfaceShmemOwner(owner);
        it->second.mOwner = nullptr;
      }
      // If the last shmem id needs to be kept alive, move it before clearing
      // the hashtable.
      if (aKeepAlive && it->first == mLastDataSurfaceShmemId) {
        lastShmem = std::move(it->second);
      }
    }
    // Clear all iterated shmem mappings.
    if (it == mDataSurfaceShmems.end()) {
      mDataSurfaceShmems.clear();
    } else if (it != mDataSurfaceShmems.begin()) {
      mDataSurfaceShmems.erase(mDataSurfaceShmems.begin(), it);
    }
    // Restore the last shmem id if necessary.
    if (lastShmem.mShmem.IsValid()) {
      mDataSurfaceShmems[mLastDataSurfaceShmemId] = std::move(lastShmem);
    }
  }
}

bool CanvasTranslator::SetDataSurfaceBuffer(
    uint32_t aId, ipc::MutableSharedMemoryHandle&& aBufferHandle) {
  MOZ_ASSERT(IsInTaskQueue());
  if (mHeader->readerState == State::Failed) {
    // We failed before we got to the pause event.
    return false;
  }

  if (mHeader->readerState != State::Paused) {
    gfxCriticalNote << "CanvasTranslator::SetDataSurfaceBuffer bad state "
                    << uint32_t(State(mHeader->readerState));
#ifndef FUZZING_SNAPSHOT
    MOZ_DIAGNOSTIC_CRASH("mHeader->readerState == State::Paused");
#endif
    Deactivate();
    return false;
  }

  if (!aId) {
    return false;
  }

  if (aId < mLastDataSurfaceShmemId) {
    // The id space has overflowed, so clear out all existing mapping.
    DataSurfaceBufferWillChange(0, false);
  } else if (mLastDataSurfaceShmemId != aId) {
    // The last shmem id will be kept alive, even if there is no owner. The last
    // shmem id is about to change, so if the current last id has no owner, it
    // needs to be erased.
    auto it = mDataSurfaceShmems.find(mLastDataSurfaceShmemId);
    if (it != mDataSurfaceShmems.end() && it->second.mOwner.IsDead()) {
      mDataSurfaceShmems.erase(it);
    }
    // Ensure the current shmems don't exceed the maximum allowed.
    size_t maxShmems = StaticPrefs::gfx_canvas_accelerated_max_data_shmems();
    if (maxShmems > 0 && mDataSurfaceShmems.size() >= maxShmems) {
      DataSurfaceBufferWillChange(0, false,
                                  (mDataSurfaceShmems.size() - maxShmems) + 1);
    }
  }
  mLastDataSurfaceShmemId = aId;

  // If the id is being reused, then ensure the old contents is copied before
  // the buffer is changed.
  DataSurfaceBufferWillChange(aId);

  // Finally, change the shmem mapping.
  mDataSurfaceShmems[aId].mShmem = aBufferHandle.Map();
  if (!mDataSurfaceShmems[aId].mShmem) {
    // Try clearing out old mappings to see if resource limits were reached.
    DataSurfaceBufferWillChange(0, false);
    // Try mapping one last time.
    mDataSurfaceShmems[aId].mShmem = aBufferHandle.Map();
    if (!mDataSurfaceShmems[aId].mShmem) {
      return false;
    }
  }

  return TranslateRecording();
}

void CanvasTranslator::GetDataSurface(uint32_t aId, uint64_t aSurfaceRef) {
  MOZ_ASSERT(IsInTaskQueue());

  ReferencePtr surfaceRef = reinterpret_cast<void*>(aSurfaceRef);
  RefPtr<gfx::DataSourceSurface> dataSurface = LookupDataSurface(surfaceRef);
  if (!dataSurface) {
    gfx::SourceSurface* surface = LookupSourceSurface(surfaceRef);
    if (!surface) {
      return;
    }
    dataSurface = surface->GetDataSurface();
    if (!dataSurface) {
      return;
    }
  }
  auto dstSize = dataSurface->GetSize();
  gfx::SurfaceFormat format = dataSurface->GetFormat();
  auto dstStride = ImageDataSerializer::ComputeRGBStride(format, dstSize.width);
  Maybe<uint32_t> requiredSize =
      ImageDataSerializer::ComputeRGBBufferSize(dstSize, format);
  if (dstStride.isNothing() || requiredSize.isNothing()) {
    return;
  }

  // Ensure any old references to the shmem are copied before modification.
  DataSurfaceBufferWillChange(aId);

  // Ensure a shmem exists with the given id.
  auto it = mDataSurfaceShmems.find(aId);
  if (it == mDataSurfaceShmems.end()) {
    return;
  }

  // Try directly reading the data surface into shmem to avoid further copies.
  if (size_t(requiredSize.value()) > it->second.mShmem.Size()) {
    return;
  }

  uint8_t* dst = it->second.mShmem.DataAs<uint8_t>();
  if (dataSurface->ReadDataInto(dst, dstStride.value())) {
    // If reading directly into the shmem, then mark the data surface as the
    // shmem's owner.
    it->second.mOwner = dataSurface;
    dataSurface->AddUserData(&mDataSurfaceShmemIdKey,
                             reinterpret_cast<void*>(aId), nullptr);
    return;
  }

  // Otherwise, map the data surface and do an explicit copy.
  gfx::DataSourceSurface::ScopedMap map(dataSurface,
                                        gfx::DataSourceSurface::MapType::READ);
  if (!map.IsMapped()) {
    return;
  }

  gfx::SwizzleData(map.GetData(), map.GetStride(), format, dst,
                   dstStride.value(), format, dstSize);
}

already_AddRefed<gfx::SourceSurface> CanvasTranslator::WaitForSurface(
    uintptr_t aId, Maybe<layers::SurfaceDescriptor>* aDesc) {
  // If it's not safe to flush the event queue, then don't try to wait.
  if (!gfx::gfxVars::UseAcceleratedCanvas2D() ||
      !UsePendingCanvasTranslatorEvents() || !IsInTaskQueue()) {
    return nullptr;
  }
  ReferencePtr idRef(aId);
  ExportSurface* surf = LookupExportSurface(idRef);
  if (!surf || !surf->mData) {
    if (!HasPendingEvent()) {
      return nullptr;
    }

    // If the surface doesn't exist yet, that may be because the events
    // that produce it still need to be processed. Flush out any events
    // currently in the queue, that by now should have been placed in
    // the queue but for which processing has not yet occurred..
    mFlushCheckpoint = mHeader->eventCount;
    HandleCanvasTranslatorEvents();
    mFlushCheckpoint = 0;
    // If there is still no surface, then it is unlikely to be produced
    // now, so give up.
    surf = LookupExportSurface(idRef);
    if (!surf || !surf->mData) {
      return nullptr;
    }
  }
  // If we need to export a surface descriptor, then ensure we can export
  // to an accelerated type from the WebGL context.
  if (aDesc && mWebglTextureType != TextureType::Unknown && mSharedContext &&
      !mSharedContext->IsContextLost()) {
    surf->mSharedSurface =
        mSharedContext->ExportSharedSurface(mWebglTextureType, surf->mData);
    if (surf->mSharedSurface) {
      surf->mSharedSurface->BeginRead();
      *aDesc = surf->mSharedSurface->ToSurfaceDescriptor();
      if (*aDesc && aDesc->ref().type() ==
                        SurfaceDescriptor::TSurfaceDescriptorMacIOSurface) {
        aDesc->ref().get_SurfaceDescriptorMacIOSurface().gpuFence() =
            surf->mSharedSurface->TakeGpuFence();
      }
      surf->mSharedSurface->EndRead();
    }
  }
  return do_AddRef(surf->mData);
}

void CanvasTranslator::RemoveExportSurface(gfx::ReferencePtr aRefPtr) {
  auto it = mExportSurfaces.find(aRefPtr);
  if (it != mExportSurfaces.end()) {
    mExportSurfaces.erase(it);
  }
}

void CanvasTranslator::RecycleBuffer() {
  if (!mCurrentShmem.IsValid()) {
    return;
  }

  mCanvasShmems.emplace(std::move(mCurrentShmem));
  NextBuffer();
}

void CanvasTranslator::NextBuffer() {
  if (mCanvasShmems.empty()) {
    return;
  }

  // Check and signal the writer when we finish with a buffer, because it
  // might have hit the buffer count limit and be waiting to use our old one.
  CheckAndSignalWriter();

  mCurrentShmem = std::move(mCanvasShmems.front());
  mCanvasShmems.pop();
  mCurrentMemReader = mCurrentShmem.CreateMemReader();
}

void CanvasTranslator::ActorDestroy(ActorDestroyReason why) {
  MOZ_ASSERT(gfx::CanvasRenderThread::IsInCanvasRenderThread());

  // Since we might need to access the actor status off the owning IPDL thread,
  // we need to cache it here.
  mIPDLClosed = true;

  {
    MutexAutoLock lock(mCanvasTranslatorEventsLock);
    mPendingCanvasTranslatorEvents.clear();
  }

  DispatchToTaskQueue(NewRunnableMethod("CanvasTranslator::ClearTextureInfo",
                                        this,
                                        &CanvasTranslator::ClearTextureInfo));
}

void CanvasTranslator::Deactivate() {
  if (mDeactivated) {
    return;
  }
  mDeactivated = true;
  if (mHeader) {
    mHeader->readerState = State::Failed;
  }

  // We need to tell the other side to deactivate. Make sure the stream is
  // marked as bad so that the writing side won't wait for space to write.
  gfx::CanvasRenderThread::Dispatch(
      NewRunnableMethod("CanvasTranslator::SendDeactivate", this,
                        &CanvasTranslator::SendDeactivate));

  // Disable remote canvas for all.
  gfx::CanvasManagerParent::DisableRemoteCanvas();
}

inline gfx::DrawTargetWebgl* CanvasTranslator::TextureInfo::GetDrawTargetWebgl(
    bool aCheckForFallback) const {
  if ((!mTextureData || !aCheckForFallback) && mDrawTarget &&
      mDrawTarget->GetBackendType() == gfx::BackendType::WEBGL) {
    return static_cast<gfx::DrawTargetWebgl*>(mDrawTarget.get());
  }
  return nullptr;
}

bool CanvasTranslator::TryDrawTargetWebglFallback(
    const RemoteTextureOwnerId aTextureOwnerId, gfx::DrawTargetWebgl* aWebgl) {
  NotifyRequiresRefresh(aTextureOwnerId);

  const auto& info = mTextureInfo[aTextureOwnerId];
  if (info.mTextureData) {
    return true;
  }
  if (RefPtr<gfx::DrawTarget> dt =
          CreateFallbackDrawTarget(info.mRefPtr, aTextureOwnerId,
                                   aWebgl->GetSize(), aWebgl->GetFormat())) {
    bool success = aWebgl->CopyToFallback(dt);
    if (info.mRefPtr) {
      AddDrawTarget(info.mRefPtr, dt);
    }
    return success;
  }
  return false;
}

void CanvasTranslator::ForceDrawTargetWebglFallback() {
  // This looks for any DrawTargetWebgls that have a cached data snapshot that
  // can be used to recover a fallback TextureData in the event of a context
  // loss.
  RemoteTextureOwnerIdSet lost;
  for (const auto& entry : mTextureInfo) {
    const auto& ownerId = entry.first;
    const auto& info = entry.second;
    if (gfx::DrawTargetWebgl* webgl = info.GetDrawTargetWebgl()) {
      if (!TryDrawTargetWebglFallback(entry.first, webgl)) {
        // No fallback could be created, so we need to notify the compositor the
        // texture won't be pushed.
        if (mRemoteTextureOwner && mRemoteTextureOwner->IsRegistered(ownerId)) {
          lost.insert(ownerId);
        }
      }
    }
  }
  if (!lost.empty()) {
    NotifyDeviceReset(lost);
  }
}

void CanvasTranslator::BlockCanvas() {
  if (mDeactivated || mBlocked) {
    return;
  }
  mBlocked = true;
  gfx::CanvasRenderThread::Dispatch(
      NewRunnableMethod("CanvasTranslator::SendBlockCanvas", this,
                        &CanvasTranslator::SendBlockCanvas));
}

void CanvasTranslator::CheckAndSignalWriter() {
  do {
    switch (mHeader->writerState) {
      case State::Processing:
      case State::Failed:
        return;
      case State::AboutToWait:
        // The writer is making a decision about whether to wait. So, we must
        // wait until it has decided to avoid races. Check if the writer is
        // closed to avoid hangs.
        if (mIPDLClosed) {
          return;
        }
        continue;
      case State::Waiting:
        if (mHeader->processedCount >= mHeader->writerWaitCount) {
          mHeader->writerState = State::Processing;
          mWriterSemaphore->Signal();
        }
        return;
      default:
        MOZ_ASSERT_UNREACHABLE("Invalid waiting state.");
        return;
    }
  } while (true);
}

bool CanvasTranslator::HasPendingEvent() {
  return mHeader->processedCount < mHeader->eventCount;
}

bool CanvasTranslator::ReadPendingEvent(EventType& aEventType) {
  ReadElementConstrained(mCurrentMemReader, aEventType,
                         EventType::DRAWTARGETCREATION, LAST_CANVAS_EVENT_TYPE);
  if (!mCurrentMemReader.good()) {
    mHeader->readerState = State::Failed;
    return false;
  }

  return true;
}

bool CanvasTranslator::ReadNextEvent(EventType& aEventType) {
  MOZ_DIAGNOSTIC_ASSERT(mHeader->readerState == State::Processing);

  uint32_t spinCount = mMaxSpinCount;
  do {
    if (HasPendingEvent()) {
      return ReadPendingEvent(aEventType);
    }
  } while (--spinCount != 0);

  Flush();
  mHeader->readerState = State::AboutToWait;
  if (HasPendingEvent()) {
    mHeader->readerState = State::Processing;
    return ReadPendingEvent(aEventType);
  }

  if (!mIsInTransaction) {
    mHeader->readerState = State::Stopped;
    return false;
  }

  // When in a transaction we wait for a short time because we're expecting more
  // events from the content process. We don't want to wait for too long in case
  // other content processes are waiting for events to process.
  mHeader->readerState = State::Waiting;

  if (mReaderSemaphore->Wait(Some(mNextEventTimeout))) {
    MOZ_RELEASE_ASSERT(HasPendingEvent());
    MOZ_RELEASE_ASSERT(mHeader->readerState == State::Processing);
    return ReadPendingEvent(aEventType);
  }

  // We have to use compareExchange here because the writer can change our
  // state if we are waiting.
  if (!mHeader->readerState.compareExchange(State::Waiting, State::Stopped)) {
    MOZ_RELEASE_ASSERT(HasPendingEvent());
    MOZ_RELEASE_ASSERT(mHeader->readerState == State::Processing);
    // The writer has just signaled us, so consume it before returning
    MOZ_ALWAYS_TRUE(mReaderSemaphore->Wait());
    return ReadPendingEvent(aEventType);
  }

  return false;
}

bool CanvasTranslator::TranslateRecording() {
  MOZ_ASSERT(IsInTaskQueue());
  MOZ_DIAGNOSTIC_ASSERT_IF(mFlushCheckpoint, HasPendingEvent());

  if (mHeader->readerState == State::Failed) {
    return false;
  }

  if (mSharedContext && EnsureSharedContextWebgl()) {
    mSharedContext->EnterTlsScope();
  }
  auto exitTlsScope = MakeScopeExit([&] {
    if (mSharedContext) {
      mSharedContext->ExitTlsScope();
    }
  });

  auto start = TimeStamp::Now();
  mHeader->readerState = State::Processing;
  EventType eventType = EventType::INVALID;
  while (ReadNextEvent(eventType)) {
    bool success = RecordedEvent::DoWithEventFromReader(
        mCurrentMemReader, eventType,
        [&](RecordedEvent* recordedEvent) -> bool {
          // Make sure that the whole event was read from the stream.
          if (!mCurrentMemReader.good()) {
            if (mIPDLClosed) {
              // The other side has closed only warn about read failure.
              gfxWarning() << "Failed to read event type: "
                           << recordedEvent->GetType();
            } else {
              gfxCriticalNote << "Failed to read event type: "
                              << recordedEvent->GetType();
            }
            return false;
          }

          return recordedEvent->PlayEvent(this);
        });

    // Check the stream is good here or we will log the issue twice.
    if (!mCurrentMemReader.good()) {
      mHeader->readerState = State::Failed;
      return false;
    }

    if (!success && !HandleExtensionEvent(eventType)) {
      gfxCriticalNoteOnce << "Failed to play canvas event type: " << eventType;

      if (!mCurrentMemReader.good()) {
        mHeader->readerState = State::Failed;
        return false;
      }
    }

    mHeader->processedCount++;

    if (mHeader->readerState == State::Paused || PauseUntilSync()) {
      // We're waiting for an IPDL message return false, because we will resume
      // translation after it is received.
      Flush();
      return false;
    }

    if (mFlushCheckpoint) {
      // If we processed past the checkpoint return true to ensure translation
      // after the checkpoint resumes later.
      if (mHeader->processedCount >= mFlushCheckpoint) {
        return true;
      }
    } else {
      if (UsePendingCanvasTranslatorEvents()) {
        const auto maxDurationMs = 100;
        const auto now = TimeStamp::Now();
        const auto waitDurationMs =
            static_cast<uint32_t>((now - start).ToMilliseconds());
        if (waitDurationMs > maxDurationMs) {
          return true;
        }
      }
    }
  }

  return false;
}

bool CanvasTranslator::UsePendingCanvasTranslatorEvents() {
  return StaticPrefs::gfx_canvas_remote_use_canvas_translator_event_AtStartup();
}

void CanvasTranslator::PostCanvasTranslatorEvents(
    const MutexAutoLock& aProofOfLock) {
  if (mIPDLClosed) {
    return;
  }

  // Runnable has already been triggered.
  if (mCanvasTranslatorEventsRunnable) {
    return;
  }

  RefPtr<nsIRunnable> runnable =
      NewRunnableMethod("CanvasTranslator::HandleCanvasTranslatorEvents", this,
                        &CanvasTranslator::HandleCanvasTranslatorEvents);
  mCanvasTranslatorEventsRunnable = runnable;

  // Runnable has not been triggered yet.
  DispatchToTaskQueue(runnable.forget());
}

void CanvasTranslator::HandleCanvasTranslatorEvents() {
  MOZ_ASSERT(IsInTaskQueue());

  UniquePtr<CanvasTranslatorEvent> event;
  {
    MutexAutoLock lock(mCanvasTranslatorEventsLock);
    MOZ_ASSERT_IF(mIPDLClosed, mPendingCanvasTranslatorEvents.empty());
    if (mPendingCanvasTranslatorEvents.empty() || PauseUntilSync()) {
      mCanvasTranslatorEventsRunnable = nullptr;
      return;
    }
    auto& front = mPendingCanvasTranslatorEvents.front();
    event = std::move(front);
    mPendingCanvasTranslatorEvents.pop_front();
  }

  MOZ_RELEASE_ASSERT(event.get());

  bool dispatchTranslate = false;
  while (!dispatchTranslate && event) {
    switch (event->mTag) {
      case CanvasTranslatorEvent::Tag::TranslateRecording:
        dispatchTranslate = TranslateRecording();
        break;
      case CanvasTranslatorEvent::Tag::AddBuffer:
        dispatchTranslate = AddBuffer(event->TakeBufferHandle());
        break;
      case CanvasTranslatorEvent::Tag::SetDataSurfaceBuffer:
        dispatchTranslate = SetDataSurfaceBuffer(
            event->mId, event->TakeDataSurfaceBufferHandle());
        break;
      case CanvasTranslatorEvent::Tag::ClearCachedResources:
        ClearCachedResources();
        break;
      case CanvasTranslatorEvent::Tag::DropFreeBuffersWhenDormant:
        DropFreeBuffersWhenDormant();
        break;
    }

    event.reset(nullptr);

    {
      MutexAutoLock lock(mCanvasTranslatorEventsLock);
      MOZ_ASSERT_IF(mIPDLClosed, mPendingCanvasTranslatorEvents.empty());
      if (mIPDLClosed) {
        return;
      }
      if (PauseUntilSync()) {
        mCanvasTranslatorEventsRunnable = nullptr;
        mPendingCanvasTranslatorEvents.push_front(
            CanvasTranslatorEvent::TranslateRecording());
        return;
      }
      if (!mIPDLClosed && !dispatchTranslate &&
          !mPendingCanvasTranslatorEvents.empty()) {
        auto& front = mPendingCanvasTranslatorEvents.front();
        event = std::move(front);
        mPendingCanvasTranslatorEvents.pop_front();
      }
    }
  }

  MOZ_ASSERT(!event);

  {
    MutexAutoLock lock(mCanvasTranslatorEventsLock);
    mCanvasTranslatorEventsRunnable = nullptr;

    MOZ_ASSERT_IF(mIPDLClosed, mPendingCanvasTranslatorEvents.empty());
    if (mIPDLClosed) {
      return;
    }

    if (dispatchTranslate) {
      // Handle TranslateRecording at first in next
      // HandleCanvasTranslatorEvents().
      mPendingCanvasTranslatorEvents.push_front(
          CanvasTranslatorEvent::TranslateRecording());
    }

    if (!mPendingCanvasTranslatorEvents.empty()) {
      PostCanvasTranslatorEvents(lock);
    }
  }
}

#define READ_AND_PLAY_CANVAS_EVENT_TYPE(_typeenum, _class)             \
  case _typeenum: {                                                    \
    auto e = _class(mCurrentMemReader);                                \
    if (!mCurrentMemReader.good()) {                                   \
      if (mIPDLClosed) {                                               \
        /* The other side has closed only warn about read failure. */  \
        gfxWarning() << "Failed to read event type: " << _typeenum;    \
      } else {                                                         \
        gfxCriticalNote << "Failed to read event type: " << _typeenum; \
      }                                                                \
      return false;                                                    \
    }                                                                  \
    return e.PlayCanvasEvent(this);                                    \
  }

bool CanvasTranslator::HandleExtensionEvent(int32_t aType) {
  // This is where we handle extensions to the Moz2D Recording events to handle
  // canvas specific things.
  switch (aType) {
    FOR_EACH_CANVAS_EVENT(READ_AND_PLAY_CANVAS_EVENT_TYPE)
    default:
      return false;
  }
}

void CanvasTranslator::BeginTransaction() {
  PROFILER_MARKER_TEXT("CanvasTranslator", GRAPHICS, {},
                       "CanvasTranslator::BeginTransaction"_ns);
  mIsInTransaction = true;
}

void CanvasTranslator::Flush() {}

void CanvasTranslator::EndTransaction() {
  Flush();
  mIsInTransaction = false;
}

void CanvasTranslator::DeviceResetAcknowledged() {
  if (mRemoteTextureOwner) {
    mRemoteTextureOwner->NotifyContextRestored();
  }
}

bool CanvasTranslator::CreateReferenceTexture() {
  if (mReferenceTextureData) {
    if (mBaseDT) {
      mReferenceTextureData->ReturnDrawTarget(mBaseDT.forget());
    }
    mReferenceTextureData->Unlock();
  }

  mReferenceTextureData =
      CreateTextureData(gfx::IntSize(1, 1), gfx::SurfaceFormat::B8G8R8A8, true);
  if (!mReferenceTextureData) {
    return false;
  }

  if (NS_WARN_IF(!mReferenceTextureData->Lock(OpenMode::OPEN_READ_WRITE))) {
    gfxCriticalNote << "CanvasTranslator::CreateReferenceTexture lock failed";
    mReferenceTextureData.reset();
    return false;
  }

  mBaseDT = mReferenceTextureData->BorrowDrawTarget();
  if (!mBaseDT) {
    return false;
  }

  return true;
}

void CanvasTranslator::NotifyDeviceReset(const RemoteTextureOwnerIdSet& aIds) {
  if (aIds.empty()) {
    return;
  }
  if (mRemoteTextureOwner) {
    mRemoteTextureOwner->NotifyContextLost(&aIds);
  }
  nsTArray<RemoteTextureOwnerId> idArray(aIds.size());
  for (const auto& id : aIds) {
    idArray.AppendElement(id);
  }
  gfx::CanvasRenderThread::Dispatch(
      NewRunnableMethod<nsTArray<RemoteTextureOwnerId>&&>(
          "CanvasTranslator::SendNotifyDeviceReset", this,
          &CanvasTranslator::SendNotifyDeviceReset, std::move(idArray)));
}

gfx::DrawTargetWebgl* CanvasTranslator::GetDrawTargetWebgl(
    const RemoteTextureOwnerId aTextureOwnerId, bool aCheckForFallback) const {
  auto result = mTextureInfo.find(aTextureOwnerId);
  if (result != mTextureInfo.end()) {
    return result->second.GetDrawTargetWebgl(aCheckForFallback);
  }
  return nullptr;
}

void CanvasTranslator::NotifyRequiresRefresh(
    const RemoteTextureOwnerId aTextureOwnerId, bool aDispatch) {
  if (aDispatch) {
    auto& info = mTextureInfo[aTextureOwnerId];
    if (!info.mNotifiedRequiresRefresh) {
      info.mNotifiedRequiresRefresh = true;
      DispatchToTaskQueue(NewRunnableMethod<RemoteTextureOwnerId, bool>(
          "CanvasTranslator::NotifyRequiresRefresh", this,
          &CanvasTranslator::NotifyRequiresRefresh, aTextureOwnerId, false));
    }
    return;
  }

  if (mTextureInfo.find(aTextureOwnerId) != mTextureInfo.end()) {
    (void)SendNotifyRequiresRefresh(aTextureOwnerId);
  }
}

void CanvasTranslator::CacheSnapshotShmem(
    const RemoteTextureOwnerId aTextureOwnerId, bool aDispatch) {
  if (aDispatch) {
    DispatchToTaskQueue(NewRunnableMethod<RemoteTextureOwnerId, bool>(
        "CanvasTranslator::CacheSnapshotShmem", this,
        &CanvasTranslator::CacheSnapshotShmem, aTextureOwnerId, false));
    return;
  }

  if (gfx::DrawTargetWebgl* webgl = GetDrawTargetWebgl(aTextureOwnerId)) {
    if (auto shmemHandle = webgl->TakeShmemHandle()) {
      // Lock the DT so that it doesn't get removed while shmem is in transit.
      AddTextureKeepAlive(aTextureOwnerId);
      nsCOMPtr<nsIThread> thread =
          gfx::CanvasRenderThread::GetCanvasRenderThread();
      RefPtr<CanvasTranslator> translator = this;
      SendSnapshotShmem(aTextureOwnerId, std::move(shmemHandle))
          ->Then(
              thread, __func__,
              [=](bool) {
                translator->RemoveTextureKeepAlive(aTextureOwnerId);
              },
              [=](ipc::ResponseRejectReason) {
                translator->RemoveTextureKeepAlive(aTextureOwnerId);
              });
    }
  }
}

void CanvasTranslator::PrepareShmem(
    const RemoteTextureOwnerId aTextureOwnerId) {
  if (gfx::DrawTargetWebgl* webgl =
          GetDrawTargetWebgl(aTextureOwnerId, false)) {
    if (RefPtr<gfx::DrawTarget> dt =
            mTextureInfo[aTextureOwnerId].mFallbackDrawTarget) {
      // If there was a fallback, copy the fallback to the software framebuffer
      // shmem for reading.
      if (RefPtr<gfx::SourceSurface> snapshot = dt->Snapshot()) {
        webgl->CopySurface(snapshot, snapshot->GetRect(), gfx::IntPoint(0, 0));
      }
    } else {
      // Otherwise, just ensure the software framebuffer is up to date.
      webgl->PrepareShmem();
    }
  }
}

void CanvasTranslator::CacheDataSnapshots() {
  DataSurfaceBufferWillChange();

  if (mSharedContext) {
    // If there are any DrawTargetWebgls, then try to cache their framebuffers
    // in software surfaces, just in case the GL context is lost. So long as
    // there is a software copy of the framebuffer, it can be copied into a
    // fallback TextureData later even if the GL context goes away.
    for (auto const& entry : mTextureInfo) {
      if (gfx::DrawTargetWebgl* webgl = entry.second.GetDrawTargetWebgl()) {
        webgl->EnsureDataSnapshot();
      }
    }
  }
}

void CanvasTranslator::ClearCachedResources() {
  mUsedDataSurfaceForSurfaceDescriptor = nullptr;
  mUsedWrapperForSurfaceDescriptor = nullptr;
  mUsedSurfaceDescriptorForSurfaceDescriptor = Nothing();

  if (mSharedContext) {
    mSharedContext->OnMemoryPressure();
  }

  CacheDataSnapshots();
}

ipc::IPCResult CanvasTranslator::RecvClearCachedResources() {
  if (mDeactivated) {
    // The other side might have sent a message before we deactivated.
    return IPC_OK();
  }

  if (UsePendingCanvasTranslatorEvents()) {
    MutexAutoLock lock(mCanvasTranslatorEventsLock);
    mPendingCanvasTranslatorEvents.emplace_back(
        CanvasTranslatorEvent::ClearCachedResources());
    PostCanvasTranslatorEvents(lock);
  } else {
    DispatchToTaskQueue(
        NewRunnableMethod("CanvasTranslator::ClearCachedResources", this,
                          &CanvasTranslator::ClearCachedResources));
  }
  return IPC_OK();
}

void CanvasTranslator::DropFreeBuffersWhenDormant() { CacheDataSnapshots(); }

ipc::IPCResult CanvasTranslator::RecvDropFreeBuffersWhenDormant() {
  if (mDeactivated) {
    // The other side might have sent a message before we deactivated.
    return IPC_OK();
  }

  if (UsePendingCanvasTranslatorEvents()) {
    MutexAutoLock lock(mCanvasTranslatorEventsLock);
    mPendingCanvasTranslatorEvents.emplace_back(
        CanvasTranslatorEvent::DropFreeBuffersWhenDormant());
    PostCanvasTranslatorEvents(lock);
  } else {
    DispatchToTaskQueue(
        NewRunnableMethod("CanvasTranslator::DropFreeBuffersWhenDormant", this,
                          &CanvasTranslator::DropFreeBuffersWhenDormant));
  }
  return IPC_OK();
}

static const OpenMode kInitMode = OpenMode::OPEN_READ_WRITE;

already_AddRefed<gfx::DrawTarget> CanvasTranslator::CreateFallbackDrawTarget(
    gfx::ReferencePtr aRefPtr, const RemoteTextureOwnerId aTextureOwnerId,
    const gfx::IntSize& aSize, gfx::SurfaceFormat aFormat) {
  UniquePtr<TextureData> textureData =
      CreateOrRecycleTextureData(aSize, aFormat);
  if (NS_WARN_IF(!textureData)) {
    return nullptr;
  }

  if (NS_WARN_IF(!textureData->Lock(kInitMode))) {
    gfxCriticalNote << "CanvasTranslator::CreateDrawTarget lock failed";
    return nullptr;
  }

  RefPtr<gfx::DrawTarget> dt = textureData->BorrowDrawTarget();
  if (NS_WARN_IF(!dt)) {
    textureData->Unlock();
    return nullptr;
  }
  // Recycled buffer contents may be uninitialized.
  dt->ClearRect(gfx::Rect(dt->GetRect()));

  TextureInfo& info = mTextureInfo[aTextureOwnerId];
  info.mRefPtr = aRefPtr;
  info.mFallbackDrawTarget = dt;
  info.mTextureData = std::move(textureData);
  info.mTextureLockMode = kInitMode;

  return dt.forget();
}

already_AddRefed<gfx::DrawTarget> CanvasTranslator::CreateDrawTarget(
    gfx::ReferencePtr aRefPtr, const RemoteTextureOwnerId aTextureOwnerId,
    const gfx::IntSize& aSize, gfx::SurfaceFormat aFormat) {
  if (!aTextureOwnerId.IsValid()) {
#ifndef FUZZING_SNAPSHOT
    MOZ_DIAGNOSTIC_CRASH("No texture owner set");
#endif
    return nullptr;
  }

  {
    auto result = mTextureInfo.find(aTextureOwnerId);
    if (result != mTextureInfo.end()) {
      const TextureInfo& info = result->second;
      if (info.mTextureData || info.mDrawTarget) {
#ifndef FUZZING_SNAPSHOT
        MOZ_DIAGNOSTIC_CRASH("DrawTarget already exists");
#endif
        return nullptr;
      }
    }
  }

  RefPtr<gfx::DrawTarget> dt;
  if (gfx::gfxVars::UseAcceleratedCanvas2D()) {
    if (EnsureSharedContextWebgl()) {
      mSharedContext->EnterTlsScope();
    }
    if (RefPtr<gfx::DrawTargetWebgl> webgl =
            gfx::DrawTargetWebgl::Create(aSize, aFormat, mSharedContext)) {
      webgl->BeginFrame(true);
      dt = webgl.forget().downcast<gfx::DrawTarget>();
      if (dt) {
        TextureInfo& info = mTextureInfo[aTextureOwnerId];
        info.mRefPtr = aRefPtr;
        info.mDrawTarget = dt;
        info.mTextureLockMode = kInitMode;
        CacheSnapshotShmem(aTextureOwnerId);
      }
    }
    if (!dt) {
      NotifyRequiresRefresh(aTextureOwnerId);
    }
  }

  if (!dt) {
    dt = CreateFallbackDrawTarget(aRefPtr, aTextureOwnerId, aSize, aFormat);
  }

  if (dt && aRefPtr) {
    AddDrawTarget(aRefPtr, dt);
  }
  return dt.forget();
}

already_AddRefed<gfx::DrawTarget> CanvasTranslator::CreateDrawTarget(
    gfx::ReferencePtr aRefPtr, const gfx::IntSize& aSize,
    gfx::SurfaceFormat aFormat) {
#ifndef FUZZING_SNAPSHOT
  MOZ_DIAGNOSTIC_CRASH("Unexpected CreateDrawTarget call!");
#endif
  return nullptr;
}

void CanvasTranslator::NotifyTextureDestruction(
    const RemoteTextureOwnerId aTextureOwnerId) {
  MOZ_ASSERT(gfx::CanvasRenderThread::IsInCanvasRenderThread());

  if (mIPDLClosed) {
    return;
  }
  (void)SendNotifyTextureDestruction(aTextureOwnerId);
}

void CanvasTranslator::AddTextureKeepAlive(const RemoteTextureOwnerId& aId) {
  auto result = mTextureInfo.find(aId);
  if (result == mTextureInfo.end()) {
    return;
  }
  auto& info = result->second;
  ++info.mKeepAlive;
}

void CanvasTranslator::RemoveTextureKeepAlive(const RemoteTextureOwnerId& aId) {
  RemoveTexture(aId, 0, 0, false);
}

void CanvasTranslator::RemoveTexture(const RemoteTextureOwnerId aTextureOwnerId,
                                     RemoteTextureTxnType aTxnType,
                                     RemoteTextureTxnId aTxnId,
                                     bool aFinalize) {
  // Don't erase the texture if still in use
  auto result = mTextureInfo.find(aTextureOwnerId);
  if (result == mTextureInfo.end()) {
    return;
  }
  auto& info = result->second;
  if (mRemoteTextureOwner && aTxnType && aTxnId) {
    mRemoteTextureOwner->WaitForTxn(aTextureOwnerId, aTxnType, aTxnId);
  }
  // Remove the DrawTarget only if this is being called from a recorded event
  // or if there are no remaining keepalives. If this is being called only to
  // remove a keepalive without forcing removal, then the DrawTarget is still
  // being used by the recording.
  if ((aFinalize || info.mKeepAlive <= 1) && info.mRefPtr) {
    RemoveDrawTarget(info.mRefPtr);
    info.mRefPtr = ReferencePtr();
  }
  if (--info.mKeepAlive > 0) {
    return;
  }
  if (info.mTextureData) {
    if (info.mFallbackDrawTarget) {
      info.mTextureData->ReturnDrawTarget(info.mFallbackDrawTarget.forget());
    }
    info.mTextureData->Unlock();
  }
  if (mRemoteTextureOwner) {
    // If this texture id was manually registered as a remote texture owner,
    // unregister it so it does not stick around after the texture id goes away.
    if (aTextureOwnerId.IsValid()) {
      mRemoteTextureOwner->UnregisterTextureOwner(aTextureOwnerId);
    }
  }

  gfx::CanvasRenderThread::Dispatch(NewRunnableMethod<RemoteTextureOwnerId>(
      "CanvasTranslator::NotifyTextureDestruction", this,
      &CanvasTranslator::NotifyTextureDestruction, aTextureOwnerId));

  mTextureInfo.erase(result);
}

bool CanvasTranslator::LockTexture(const RemoteTextureOwnerId aTextureOwnerId,
                                   OpenMode aMode, bool aInvalidContents) {
  if (aMode == OpenMode::OPEN_NONE) {
    return false;
  }
  auto result = mTextureInfo.find(aTextureOwnerId);
  if (result == mTextureInfo.end()) {
    return false;
  }
  auto& info = result->second;
  if (info.mTextureLockMode != OpenMode::OPEN_NONE) {
    return (info.mTextureLockMode & aMode) == aMode;
  }
  if (gfx::DrawTargetWebgl* webgl = info.GetDrawTargetWebgl()) {
    if (aMode & OpenMode::OPEN_WRITE) {
      webgl->BeginFrame(aInvalidContents);
    }
  }
  info.mTextureLockMode = aMode;
  return true;
}

bool CanvasTranslator::UnlockTexture(
    const RemoteTextureOwnerId aTextureOwnerId) {
  auto result = mTextureInfo.find(aTextureOwnerId);
  if (result == mTextureInfo.end()) {
    return false;
  }
  auto& info = result->second;
  if (info.mTextureLockMode == OpenMode::OPEN_NONE) {
    return false;
  }

  if (gfx::DrawTargetWebgl* webgl = info.GetDrawTargetWebgl()) {
    if (info.mTextureLockMode & OpenMode::OPEN_WRITE) {
      webgl->EndFrame();
      if (webgl->RequiresRefresh()) {
        NotifyRequiresRefresh(aTextureOwnerId);
      }
    }
  }
  info.mTextureLockMode = OpenMode::OPEN_NONE;
  return true;
}

bool CanvasTranslator::PresentTexture(
    const RemoteTextureOwnerId aTextureOwnerId, RemoteTextureId aId) {
  AUTO_PROFILER_MARKER_TEXT("CanvasTranslator", GRAPHICS, {},
                            "CanvasTranslator::PresentTexture"_ns);
  auto result = mTextureInfo.find(aTextureOwnerId);
  if (result == mTextureInfo.end()) {
    return false;
  }
  auto& info = result->second;
  if (gfx::DrawTargetWebgl* webgl = info.GetDrawTargetWebgl()) {
    EnsureRemoteTextureOwner(aTextureOwnerId);
    if (webgl->CopyToSwapChain(mWebglTextureType, aId, aTextureOwnerId,
                               mRemoteTextureOwner)) {
      return true;
    }
    if (mSharedContext && mSharedContext->IsContextLost()) {
      // If the context was lost, try to create a fallback to push instead.
      EnsureSharedContextWebgl();
    } else {
      // CopyToSwapChain failed for an unknown reason other than context loss.
      // Try to read into fallback data if possible to recover, otherwise force
      // the loss of the individual texture.
      webgl->EnsureDataSnapshot();
      if (!TryDrawTargetWebglFallback(aTextureOwnerId, webgl)) {
        RemoteTextureOwnerIdSet lost = {aTextureOwnerId};
        NotifyDeviceReset(lost);
      }
    }
  }
  if (TextureData* data = info.mTextureData.get()) {
    PushRemoteTexture(aTextureOwnerId, data, aId, aTextureOwnerId);
  }
  return true;
}

void CanvasTranslator::EnsureRemoteTextureOwner(RemoteTextureOwnerId aOwnerId) {
  if (!mRemoteTextureOwner) {
    mRemoteTextureOwner = new RemoteTextureOwnerClient(mOtherPid);
  }
  if (aOwnerId.IsValid() && !mRemoteTextureOwner->IsRegistered(aOwnerId)) {
    mRemoteTextureOwner->RegisterTextureOwner(aOwnerId,
                                              /* aSharedRecycling */ true);
  }
}

UniquePtr<TextureData> CanvasTranslator::CreateOrRecycleTextureData(
    const gfx::IntSize& aSize, gfx::SurfaceFormat aFormat) {
  if (mRemoteTextureOwner) {
    if (mTextureType == TextureType::Unknown) {
      return mRemoteTextureOwner->CreateOrRecycleBufferTextureData(aSize,
                                                                   aFormat);
    }
    if (UniquePtr<TextureData> data =
            mRemoteTextureOwner->GetRecycledTextureData(aSize, aFormat,
                                                        mTextureType)) {
      return data;
    }
  }
  return CreateTextureData(aSize, aFormat, false);
}

bool CanvasTranslator::PushRemoteTexture(
    const RemoteTextureOwnerId aTextureOwnerId, TextureData* aData,
    RemoteTextureId aId, RemoteTextureOwnerId aOwnerId) {
  EnsureRemoteTextureOwner(aOwnerId);
  TextureData::Info info;
  aData->FillInfo(info);
  UniquePtr<TextureData> dstData =
      CreateOrRecycleTextureData(info.size, info.format);
  bool success = false;
  // Source data is already locked.
  if (dstData) {
    if (dstData->Lock(OpenMode::OPEN_WRITE)) {
      if (RefPtr<gfx::DrawTarget> dstDT = dstData->BorrowDrawTarget()) {
        if (RefPtr<gfx::DrawTarget> srcDT = aData->BorrowDrawTarget()) {
          if (RefPtr<gfx::SourceSurface> snapshot = srcDT->Snapshot()) {
            dstDT->CopySurface(snapshot, snapshot->GetRect(),
                               gfx::IntPoint(0, 0));
            dstDT->Flush();
            success = true;
          }
        }
      }
      dstData->Unlock();
    } else {
      gfxCriticalNote << "CanvasTranslator::PushRemoteTexture dst lock failed";
    }
  }
  if (success) {
    mRemoteTextureOwner->PushTexture(aId, aOwnerId, std::move(dstData));
  } else {
    mRemoteTextureOwner->PushDummyTexture(aId, aOwnerId);
  }
  return success;
}

void CanvasTranslator::ClearTextureInfo() {
  MOZ_ASSERT(mIPDLClosed);

  DataSurfaceBufferWillChange(0, false);

  mUsedDataSurfaceForSurfaceDescriptor = nullptr;
  mUsedWrapperForSurfaceDescriptor = nullptr;
  mUsedSurfaceDescriptorForSurfaceDescriptor = Nothing();

  for (auto& entry : mTextureInfo) {
    auto& info = entry.second;
    if (info.mTextureData) {
      if (info.mFallbackDrawTarget) {
        info.mTextureData->ReturnDrawTarget(info.mFallbackDrawTarget.forget());
      }
      info.mTextureData->Unlock();
    }
  }
  mTextureInfo.clear();
  mDrawTargets.Clear();
  mSharedContext = nullptr;
  // If the global shared context's ref is the last ref left, then clear out
  // any internal caches and textures from the context, but still keep it
  // alive. This saves on startup costs while not contributing significantly
  // to memory usage.
  if (sSharedContext && sSharedContext->hasOneRef()) {
    sSharedContext->ClearCaches();
  }
  if (mReferenceTextureData) {
    if (mBaseDT) {
      mReferenceTextureData->ReturnDrawTarget(mBaseDT.forget());
    }
    mReferenceTextureData->Unlock();
  }
  if (mRemoteTextureOwner) {
    mRemoteTextureOwner->UnregisterAllTextureOwners();
    mRemoteTextureOwner = nullptr;
  }
}

already_AddRefed<gfx::SourceSurface> CanvasTranslator::LookupExternalSurface(
    uint64_t aKey) {
  return mSharedSurfacesHolder->Get(wr::ToExternalImageId(aKey));
}

// Check if the surface descriptor describes a GPUVideo texture for which we
// only have an opaque source/handle from SurfaceDescriptorRemoteDecoder to
// derive the actual texture from.
static bool SDIsSupportedRemoteDecoder(const SurfaceDescriptor& sd) {
  if (sd.type() != SurfaceDescriptor::TSurfaceDescriptorGPUVideo) {
    return false;
  }

  const auto& sdv = sd.get_SurfaceDescriptorGPUVideo();
  const auto& sdvType = sdv.type();
  if (sdvType != SurfaceDescriptorGPUVideo::TSurfaceDescriptorRemoteDecoder) {
    return false;
  }

  const auto& sdrd = sdv.get_SurfaceDescriptorRemoteDecoder();
  const auto& subdesc = sdrd.subdesc();
  const auto& subdescType = subdesc.type();

  if (subdescType == RemoteDecoderVideoSubDescriptor::Tnull_t ||
      subdescType ==
          RemoteDecoderVideoSubDescriptor::TSurfaceDescriptorMacIOSurface ||
      subdescType == RemoteDecoderVideoSubDescriptor::TSurfaceDescriptorD3D10) {
    return true;
  }

  return false;
}

already_AddRefed<gfx::DataSourceSurface>
CanvasTranslator::MaybeRecycleDataSurfaceForSurfaceDescriptor(
    TextureHost* aTextureHost,
    const SurfaceDescriptorRemoteDecoder& aSurfaceDescriptor) {
  if (!StaticPrefs::gfx_canvas_remote_recycle_used_data_surface()) {
    return nullptr;
  }

  auto& usedSurf = mUsedDataSurfaceForSurfaceDescriptor;
  auto& usedWrapper = mUsedWrapperForSurfaceDescriptor;
  auto& usedDescriptor = mUsedSurfaceDescriptorForSurfaceDescriptor;

  if (usedDescriptor.isSome() && usedDescriptor.ref() == aSurfaceDescriptor) {
    MOZ_ASSERT(usedSurf);
    MOZ_ASSERT(usedWrapper);

    auto* bufferTextureHost = aTextureHost->AsBufferTextureHost();
    if (bufferTextureHost) {
      if (usedSurf->GetType() == gfx::SurfaceType::DATA_ALIGNED) {
        // Buffer of DataSourceSurface is owned by DataSourceSurface
        MOZ_ASSERT(aTextureHost->GetSize() == usedSurf->GetSize());
        if (aTextureHost->GetSize() == usedSurf->GetSize()) {
          // Since the data is the same as before, the DataSourceSurfaceWrapper
          // can be reused.
          return do_AddRef(usedWrapper);
        } else {
          mUsedDataSurfaceForSurfaceDescriptor = nullptr;
          mUsedWrapperForSurfaceDescriptor = nullptr;
          mUsedSurfaceDescriptorForSurfaceDescriptor = Nothing();
        }
      } else {
        // Buffer of DataSourceSurface is owned by BufferTextureHost
        if (bufferTextureHost->GetBuffer() &&
            bufferTextureHost->GetBuffer() == usedSurf->GetData() &&
            aTextureHost->GetSize() == usedSurf->GetSize() &&
            aTextureHost->GetFormat() == usedSurf->GetFormat()) {
          // Since the data is the same as before, the DataSourceSurfaceWrapper
          // can be reused.
          return do_AddRef(usedWrapper);
        }
        mUsedDataSurfaceForSurfaceDescriptor = nullptr;
        mUsedWrapperForSurfaceDescriptor = nullptr;
        mUsedSurfaceDescriptorForSurfaceDescriptor = Nothing();
      }
    }
  }

  bool isYuvVideo = false;
  if (aTextureHost->AsMacIOSurfaceTextureHost()) {
    if (aTextureHost->GetFormat() == SurfaceFormat::NV12 ||
        aTextureHost->GetFormat() == SurfaceFormat::YUY2) {
      isYuvVideo = true;
    }
  } else if (aTextureHost->GetFormat() == gfx::SurfaceFormat::YUV420) {
    isYuvVideo = true;
  }

  // Reuse previously used DataSourceSurface if it is not used and same
  // size/format.
  bool reuseSurface = isYuvVideo && usedSurf && usedSurf->refCount() == 1 &&
                      usedSurf->GetFormat() == gfx::SurfaceFormat::B8G8R8X8 &&
                      aTextureHost->GetSize() == usedSurf->GetSize();
  usedSurf =
      aTextureHost->GetAsSurface(reuseSurface ? usedSurf.get() : nullptr);
  if (NS_WARN_IF(!usedSurf)) {
    usedWrapper = nullptr;
    usedDescriptor = Nothing();
    return nullptr;
  }
  // Wrap DataSourceSurface with DataSourceSurfaceWrapper to force upload in
  // DrawTargetWebgl::DrawSurface().
  usedDescriptor = Some(aSurfaceDescriptor);
  usedWrapper = new gfx::DataSourceSurfaceWrapper(usedSurf);
  return do_AddRef(usedWrapper);
}

already_AddRefed<gfx::SourceSurface>
CanvasTranslator::LookupSourceSurfaceFromSurfaceDescriptor(
    const SurfaceDescriptor& aDesc) {
  if (!SDIsSupportedRemoteDecoder(aDesc)) {
    return nullptr;
  }

  const auto& sdrd = aDesc.get_SurfaceDescriptorGPUVideo()
                         .get_SurfaceDescriptorRemoteDecoder();
  const auto& subdesc = sdrd.subdesc();
  const auto& subdescType = subdesc.type();

  RefPtr<VideoBridgeParent> parent =
      VideoBridgeParent::GetSingleton(sdrd.source());
  if (!parent) {
    gfxCriticalNote << "TexUnpackSurface failed to get VideoBridgeParent";
    return nullptr;
  }
  RefPtr<TextureHost> texture =
      parent->LookupTexture(mContentId, sdrd.handle());
  if (!texture) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    gfxCriticalNote << "TexUnpackSurface failed to get TextureHost";
    return nullptr;
  }

#if defined(XP_WIN)
  if (subdescType == RemoteDecoderVideoSubDescriptor::TSurfaceDescriptorD3D10) {
    auto* textureHostD3D11 = texture->AsDXGITextureHostD3D11();
    if (!textureHostD3D11) {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      return nullptr;
    }
    auto& usedSurf = mUsedDataSurfaceForSurfaceDescriptor;
    auto& usedDescriptor = mUsedSurfaceDescriptorForSurfaceDescriptor;

    // TODO reuse DataSourceSurface if no update.

    if (RefPtr<ID3D11Device> device =
            gfx::DeviceManagerDx::Get()->GetCanvasDevice()) {
      usedSurf = textureHostD3D11->GetAsSurfaceWithDevice(device);
    } else {
      usedSurf = nullptr;
    }
    if (!usedSurf) {
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      usedDescriptor = Nothing();
      return nullptr;
    }
    usedDescriptor = Some(sdrd);

    return do_AddRef(usedSurf);
  }
#endif

  if (subdescType ==
      RemoteDecoderVideoSubDescriptor::TSurfaceDescriptorMacIOSurface) {
    MOZ_ASSERT(texture->AsMacIOSurfaceTextureHost());

    RefPtr<gfx::DataSourceSurface> surf =
        MaybeRecycleDataSurfaceForSurfaceDescriptor(texture, sdrd);
    return surf.forget();
  }

  if (subdescType == RemoteDecoderVideoSubDescriptor::Tnull_t) {
    RefPtr<gfx::DataSourceSurface> surf =
        MaybeRecycleDataSurfaceForSurfaceDescriptor(texture, sdrd);
    return surf.forget();
  }

  MOZ_ASSERT_UNREACHABLE("unexpected to be called");
  return nullptr;
}

void CanvasTranslator::CheckpointReached() { CheckAndSignalWriter(); }

void CanvasTranslator::PauseTranslation() {
  mHeader->readerState = State::Paused;
}

void CanvasTranslator::AwaitTranslationSync(uint64_t aSyncId) {
  if (NS_WARN_IF(!UsePendingCanvasTranslatorEvents()) ||
      NS_WARN_IF(!IsInTaskQueue()) || NS_WARN_IF(mAwaitSyncId >= aSyncId)) {
    return;
  }

  mAwaitSyncId = aSyncId;
}

void CanvasTranslator::SyncTranslation(uint64_t aSyncId) {
  if (NS_WARN_IF(!IsInTaskQueue()) || NS_WARN_IF(aSyncId <= mLastSyncId)) {
    return;
  }

  bool wasPaused = PauseUntilSync();
  mLastSyncId = aSyncId;
  // If translation was previously paused waiting on a sync-id, check if sync-id
  // encountered requires restarting translation.
  if (wasPaused && !PauseUntilSync()) {
    HandleCanvasTranslatorEvents();
  }
}

mozilla::ipc::IPCResult CanvasTranslator::RecvSnapshotExternalCanvas(
    uint64_t aSyncId, uint32_t aManagerId, ActorId aCanvasId) {
  if (NS_WARN_IF(!IsInTaskQueue())) {
    return IPC_FAIL(this,
                    "RecvSnapshotExternalCanvas used outside of task queue.");
  }

  // Verify that snapshot requests are not received out of order order.
  if (NS_WARN_IF(aSyncId <= mLastSyncId)) {
    return IPC_FAIL(this, "RecvSnapShotExternalCanvas received too late.");
  }

  // Attempt to snapshot an external canvas that is associated with the same
  // content process as this canvas. On success, associate it with the sync-id.
  ExternalSnapshot snapshot;
  if (auto* actor = gfx::CanvasManagerParent::GetCanvasActor(
          mContentId, aManagerId, aCanvasId)) {
    if (dom::WebGLParent* webglActor = ActorDynCast<dom::WebGLParent>(actor)) {
      if (auto* hostContext = webglActor->GetHostWebGLContext()) {
        if (auto* webgl = hostContext->GetWebGLContext()) {
          if (mWebglTextureType != TextureType::Unknown) {
            snapshot.mSharedSurface = webgl->GetBackBufferSnapshotSharedSurface(
                mWebglTextureType, true, true, true);
            if (snapshot.mSharedSurface) {
              snapshot.mWebgl = webgl;
              snapshot.mDescriptor =
                  snapshot.mSharedSurface->ToSurfaceDescriptor();
              if (snapshot.mDescriptor &&
                  snapshot.mDescriptor->type() ==
                      SurfaceDescriptor::TSurfaceDescriptorMacIOSurface) {
                snapshot.mDescriptor->get_SurfaceDescriptorMacIOSurface()
                    .gpuFence() = snapshot.mSharedSurface->TakeGpuFence();
              }
            }
          }
          if (!snapshot.mDescriptor) {
            snapshot.mData = webgl->GetBackBufferSnapshot(true);
          }
        }
      }
    } else {
      MOZ_ASSERT_UNREACHABLE("Unsupported protocol");
    }
  }

  if (!snapshot.mDescriptor && !snapshot.mData) {
    // No available surface, but sync translation so it may resume after
    // attempting snapshot.
    SyncTranslation(aSyncId);
    return IPC_FAIL(this, "SnapshotExternalCanvas failed to get surface.");
  }

  mExternalSnapshots.insert({aSyncId, std::move(snapshot)});

  // Sync translation so it may resume with the snapshot.
  SyncTranslation(aSyncId);
  return IPC_OK();
}

bool CanvasTranslator::ResolveExternalSnapshot(uint64_t aSyncId,
                                               ReferencePtr aRefPtr,
                                               const IntSize& aSize,
                                               SurfaceFormat aFormat,
                                               DrawTarget* aDT) {
  MOZ_ASSERT(IsInTaskQueue());
  uint64_t prevSyncId = mLastSyncId;
  if (NS_WARN_IF(aSyncId > mLastSyncId)) {
    // If arriving here, a previous SnapshotExternalCanvas IPDL message never
    // arrived for some reason. Sync translation here to avoid locking up.
    SyncTranslation(aSyncId);
  }

  // Check if the snapshot was added. This should only ever be called once per
  // snapshot, as it is removed from the table when resolved.
  auto it = mExternalSnapshots.find(aSyncId);
  if (it == mExternalSnapshots.end()) {
    // There was no snapshot available, which can happen if this was called
    // before or without a corresponding SnapshotExternalCanvas, or if called
    // multiple times.
    if (aSyncId > prevSyncId) {
      gfxCriticalNoteOnce
          << "External canvas snapshot resolved before creation.";
    } else {
      gfxCriticalNoteOnce << "Exernal canvas snapshot already resolved.";
    }
    return false;
  }

  ExternalSnapshot snapshot = std::move(it->second);
  mExternalSnapshots.erase(it);

  RefPtr<gfx::SourceSurface> resolved;
  if (snapshot.mSharedSurface) {
    snapshot.mSharedSurface->BeginRead();
  }
  if (snapshot.mDescriptor) {
    if (aDT) {
      resolved =
          aDT->ImportSurfaceDescriptor(*snapshot.mDescriptor, aSize, aFormat);
    }
    if (!resolved && gfx::gfxVars::UseAcceleratedCanvas2D() &&
        EnsureSharedContextWebgl()) {
      // If we can't import the surface using the DT, then try using the global
      // shared context to allow for a readback.
      resolved = mSharedContext->ImportSurfaceDescriptor(*snapshot.mDescriptor,
                                                         aSize, aFormat);
    }
  }
  if (snapshot.mSharedSurface) {
    snapshot.mSharedSurface->EndRead();
    if (snapshot.mWebgl) {
      snapshot.mWebgl->RecycleSnapshotSharedSurface(snapshot.mSharedSurface);
    }
  }
  if (!resolved) {
    // There was no descriptor, but check if there is at least a data surface.
    resolved = snapshot.mData;
  }
  if (resolved) {
    AddSourceSurface(aRefPtr, resolved);
    return true;
  }
  return false;
}

already_AddRefed<gfx::GradientStops> CanvasTranslator::GetOrCreateGradientStops(
    gfx::DrawTarget* aDrawTarget, gfx::GradientStop* aRawStops,
    uint32_t aNumStops, gfx::ExtendMode aExtendMode) {
  MOZ_ASSERT(aDrawTarget);
  nsTArray<gfx::GradientStop> rawStopArray(aRawStops, aNumStops);
  return gfx::gfxGradientCache::GetOrCreateGradientStops(
      aDrawTarget, rawStopArray, aExtendMode);
}

gfx::DataSourceSurface* CanvasTranslator::LookupDataSurface(
    gfx::ReferencePtr aRefPtr) {
  return mDataSurfaces.GetWeak(aRefPtr);
}

void CanvasTranslator::AddDataSurface(
    gfx::ReferencePtr aRefPtr, RefPtr<gfx::DataSourceSurface>&& aSurface) {
  mDataSurfaces.InsertOrUpdate(aRefPtr, std::move(aSurface));
}

void CanvasTranslator::RemoveDataSurface(gfx::ReferencePtr aRefPtr) {
  RefPtr<gfx::DataSourceSurface> surface;
  if (mDataSurfaces.Remove(aRefPtr, getter_AddRefs(surface))) {
    // If the data surface is the assigned owner of a shmem, and if the shmem
    // is not the last shmem id used, then erase the shmem.
    if (auto id = reinterpret_cast<uintptr_t>(
            surface->GetUserData(&mDataSurfaceShmemIdKey))) {
      if (id != mLastDataSurfaceShmemId) {
        auto it = mDataSurfaceShmems.find(id);
        if (it != mDataSurfaceShmems.end()) {
          // If this is not the last reference to the surface, then the shmem
          // contents needs to be copied.
          if (!surface->hasOneRef()) {
            UnlinkDataSurfaceShmemOwner(surface);
          }
          mDataSurfaceShmems.erase(it);
        }
      }
    }
  }
}

}  // namespace layers
}  // namespace mozilla
