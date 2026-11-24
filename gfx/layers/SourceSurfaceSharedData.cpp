/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SourceSurfaceSharedData.h"

#include "mozilla/Likely.h"
#include "mozilla/StaticPrefs_image.h"
#include "mozilla/image/SurfaceCache.h"
#include "mozilla/ipc/SharedMemoryMapping.h"
#include "mozilla/layers/SharedSurfacesChild.h"
#include "mozilla/layers/SharedSurfacesParent.h"
#include "nsDebug.h"  // for NS_ABORT_OOM
#include "nsXULAppAPI.h"

#ifdef DEBUG
/**
 * If defined, this makes SourceSurfaceSharedData::Finalize memory protect the
 * underlying shared buffer in the producing process (the content or UI
 * process). Given flushing the page table is expensive, and its utility is
 * predominantly diagnostic (in case of overrun), turn it off by default.
 */
#  define SHARED_SURFACE_PROTECT_FINALIZED
#endif

using namespace mozilla::layers;

namespace mozilla {
namespace gfx {

void SourceSurfaceSharedDataWrapper::Init(
    const IntSize& aSize, int32_t aStride, SurfaceFormat aFormat,
    ipc::ReadOnlySharedMemoryHandle aHandle, GeckoChildID aCreatorChildID) {
  MOZ_ASSERT(!mBuf);
  mSize = aSize;
  mStride = aStride;
  mFormat = aFormat;
  mCreatorChildID = aCreatorChildID;

  size_t len = GetAlignedDataLength();
  mBufHandle = std::move(aHandle);
  if (!mBufHandle) {
    MOZ_CRASH("Invalid shared memory handle!");
  }

  bool mapped = EnsureMapped();
  if ((sizeof(uintptr_t) <= 4 ||
       StaticPrefs::image_mem_shared_unmap_force_enabled_AtStartup()) &&
      len / 1024 >
          StaticPrefs::image_mem_shared_unmap_min_threshold_kb_AtStartup()) {
    mHandleLock.emplace("SourceSurfaceSharedDataWrapper::mHandleLock");

    if (mapped) {
      // Tracking at the initial mapping, and not just after the first use of
      // the surface means we might get unmapped again before the next frame
      // gets rendered if a low virtual memory condition persists.
      SharedSurfacesParent::AddTracking(this);
    }
  } else if (!mapped) {
    // We don't support unmapping for this surface, and we failed to map it.
    NS_ABORT_OOM(len);
  } else {
    mBufHandle = nullptr;
  }
}

void SourceSurfaceSharedDataWrapper::Init(SourceSurfaceSharedData* aSurface) {
  MOZ_ASSERT(!mBuf);
  MOZ_ASSERT(aSurface);
  mSize = aSurface->mSize;
  mStride = aSurface->mStride;
  mFormat = aSurface->mFormat;
  mCreatorChildID = XRE_GetChildID();
  mBuf = aSurface->mBuf;
}

bool SourceSurfaceSharedDataWrapper::EnsureMapped() {
  MOZ_ASSERT(!GetData());

  auto computedStride =
      CheckedInt<int32_t>(mSize.width) * BytesPerPixel(mFormat);
  auto computedLength = CheckedInt<int32_t>(mSize.height) * mStride;
  if (mSize.width < 0 || mSize.height < 0 || mStride < 0 ||
      !computedStride.isValid() || computedStride.value() <= 0 ||
      mStride < computedStride.value() || !computedLength.isValid() ||
      computedLength.value() <= 0 || !image::SurfaceCache::IsLegalSize(mSize) ||
      mBufHandle.Size() < GetAlignedDataLength()) {
    return false;
  }

  auto mapping = mBufHandle.Map();
  while (!mapping) {
    nsTArray<RefPtr<SourceSurfaceSharedDataWrapper>> expired;
    if (!SharedSurfacesParent::AgeOneGeneration(expired)) {
      return false;
    }
    MOZ_ASSERT(!expired.Contains(this));
    SharedSurfacesParent::ExpireMap(expired);
    mapping = mBufHandle.Map();
  }

  mBuf = std::make_shared<ipc::MutableOrReadOnlySharedMemoryMapping>(
      std::move(mapping));

  return true;
}

bool SourceSurfaceSharedDataWrapper::Map(MapType aMapType,
                                         MappedSurface* aMappedSurface) {
  uint8_t* dataPtr;

  if (aMapType != MapType::READ) {
    // The data may be write-protected
    return false;
  }

  if (mHandleLock) {
    MutexAutoLock lock(*mHandleLock);
    dataPtr = GetData();
    if (mMapCount == 0) {
      if (mConsumers > 0) {
        SharedSurfacesParent::RemoveTracking(this);
      }
      if (!dataPtr) {
        if (!EnsureMapped()) {
          NS_ABORT_OOM(GetAlignedDataLength());
        }
        dataPtr = GetData();
      }
    }
    ++mMapCount;
  } else {
    dataPtr = GetData();
    ++mMapCount;
  }

  MOZ_ASSERT(dataPtr);
  aMappedSurface->mData = dataPtr;
  aMappedSurface->mStride = mStride;
  return true;
}

void SourceSurfaceSharedDataWrapper::Unmap() {
  if (mHandleLock) {
    MutexAutoLock lock(*mHandleLock);
    if (--mMapCount == 0 && mConsumers > 0) {
      SharedSurfacesParent::AddTracking(this);
    }
  } else {
    --mMapCount;
  }
  MOZ_ASSERT(mMapCount >= 0);
}

void SourceSurfaceSharedDataWrapper::ExpireMap() {
  MutexAutoLock lock(*mHandleLock);
  if (mMapCount == 0) {
    // This unmaps the stored memory mapping.
    *mBuf = nullptr;
  }
}

bool SourceSurfaceSharedData::Init(const IntSize& aSize, int32_t aStride,
                                   SurfaceFormat aFormat,
                                   bool aShare /* = true */) {
  mSize = aSize;
  mStride = aStride;
  mFormat = aFormat;

  size_t len = GetAlignedDataLength();
  mBufHandle = ipc::shared_memory::Create(len);
  mBuf = std::make_shared<ipc::MutableOrReadOnlySharedMemoryMapping>(
      mBufHandle.Map());
  if (NS_WARN_IF(!mBufHandle) || NS_WARN_IF(!mBuf || !*mBuf)) {
    return false;
  }

  if (aShare) {
    layers::SharedSurfacesChild::Share(this);
  }

  return true;
}

void SourceSurfaceSharedData::SizeOfExcludingThis(MallocSizeOf aMallocSizeOf,
                                                  SizeOfInfo& aInfo) const {
  MutexAutoLock lock(mMutex);
  aInfo.AddType(SurfaceType::DATA_SHARED);
  if (mBuf) {
    aInfo.mNonHeapBytes = GetAlignedDataLength();
  }
  if (!mClosed) {
    aInfo.mExternalHandles = 1;
  }
  Maybe<wr::ExternalImageId> extId = SharedSurfacesChild::GetExternalId(this);
  if (extId) {
    aInfo.mExternalId = wr::AsUint64(extId.ref());
  }
}

uint8_t* SourceSurfaceSharedData::GetDataInternal() const {
  mMutex.AssertCurrentThreadOwns();

  // This class's mappings are always mutable, so we can safely cast away the
  // const in the values returned here (see the comment above the `mBuf`
  // declaration).

  // If we have an old buffer lingering, it is because we get reallocated to
  // get a new handle to share, but there were still active mappings.
  if (MOZ_UNLIKELY(mOldBuf)) {
    MOZ_ASSERT(mMapCount > 0);
    MOZ_ASSERT(mFinalized);
    return const_cast<uint8_t*>(mOldBuf->DataAs<uint8_t>());
  }
  // Const cast to match `GetData()`.
  return const_cast<uint8_t*>(mBuf->DataAs<uint8_t>());
}

nsresult SourceSurfaceSharedData::CloneHandle(
    ipc::ReadOnlySharedMemoryHandle& aHandle) {
  MutexAutoLock lock(mMutex);
  MOZ_ASSERT(mHandleCount > 0);

  if (mClosed) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  aHandle = mBufHandle.Clone().ToReadOnly();
  if (MOZ_UNLIKELY(!aHandle)) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

void SourceSurfaceSharedData::CloseHandleInternal() {
  mMutex.AssertCurrentThreadOwns();

  if (mClosed) {
    MOZ_ASSERT(mHandleCount == 0);
    MOZ_ASSERT(mShared);
    return;
  }

  if (mShared) {
    mBufHandle = nullptr;
    mClosed = true;
  }
}

bool SourceSurfaceSharedData::ReallocHandle() {
  MutexAutoLock lock(mMutex);
  MOZ_ASSERT(mHandleCount > 0);
  MOZ_ASSERT(mClosed);

  if (NS_WARN_IF(!mFinalized)) {
    // We haven't finished populating the surface data yet, which means we are
    // out of luck, as we have no means of synchronizing with the producer to
    // write new data to a new buffer. This should be fairly rare, caused by a
    // crash in the GPU process, while we were decoding an image.
    return false;
  }

  size_t len = GetAlignedDataLength();
  auto handle = ipc::shared_memory::Create(len);
  auto mapping = handle.Map();
  if (NS_WARN_IF(!handle) || NS_WARN_IF(!mapping)) {
    return false;
  }

  size_t copyLen = GetDataLength();
  memcpy(mapping.Address(), mBuf->Address(), copyLen);
#ifdef SHARED_SURFACE_PROTECT_FINALIZED
  ipc::shared_memory::LocalProtect(mapping.DataAs<char>(), len,
                                   ipc::shared_memory::AccessRead);
#endif

  if (mMapCount > 0 && !mOldBuf) {
    mOldBuf = std::move(mBuf);
  }
  mBufHandle = std::move(handle);
  mBuf = std::make_shared<ipc::MutableOrReadOnlySharedMemoryMapping>(
      std::move(mapping));
  mClosed = false;
  mShared = false;
  return true;
}

void SourceSurfaceSharedData::Finalize() {
  MutexAutoLock lock(mMutex);
  MOZ_ASSERT(!mFinalized);

#ifdef SHARED_SURFACE_PROTECT_FINALIZED
  size_t len = GetAlignedDataLength();
  // This class's mappings are always mutable, so we can safely cast away the
  // const (see the comment above the `mBuf` declaration).
  ipc::shared_memory::LocalProtect(const_cast<char*>(mBuf->DataAs<char>()), len,
                                   ipc::shared_memory::AccessRead);
#endif

  mFinalized = true;
}

}  // namespace gfx
}  // namespace mozilla
