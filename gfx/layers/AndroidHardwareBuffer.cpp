/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AndroidHardwareBuffer.h"

#include <android/sync.h>
#include <sys/socket.h>

#include "mozilla/TimeStamp.h"
#include "mozilla/UniquePtrExtensions.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/ImageBridgeChild.h"
#include "mozilla/layers/TextureClientSharedSurface.h"

namespace mozilla {
namespace layers {

static uint32_t ToAHardwareBuffer_Format(gfx::SurfaceFormat aFormat) {
  switch (aFormat) {
    case gfx::SurfaceFormat::R8G8B8A8:
    case gfx::SurfaceFormat::B8G8R8A8:
      return AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;

    case gfx::SurfaceFormat::R8G8B8X8:
    case gfx::SurfaceFormat::B8G8R8X8:
      return AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM;

    case gfx::SurfaceFormat::R5G6B5_UINT16:
      return AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM;

    default:
      MOZ_ASSERT_UNREACHABLE("Unsupported SurfaceFormat");
      return AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
  }
}

static Maybe<gfx::SurfaceFormat> ToSurfaceFormat(uint32_t aFormat) {
  switch (aFormat) {
    case AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM:
      return Some(gfx::SurfaceFormat::R8G8B8A8);
    case AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM:
      return Some(gfx::SurfaceFormat::R8G8B8X8);
    case AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM:
      return Some(gfx::SurfaceFormat::R5G6B5_UINT16);
    default:
      return Nothing();
  }
}

/* static */
uint64_t AndroidHardwareBuffer::GetNextId() {
  static std::atomic<uint64_t> sNextId = 0;
  uint64_t id = ++sNextId;
  return id;
}

/* static */
already_AddRefed<AndroidHardwareBuffer> AndroidHardwareBuffer::Create(
    gfx::IntSize aSize, gfx::SurfaceFormat aFormat) {
  if (aFormat != gfx::SurfaceFormat::R8G8B8A8 &&
      aFormat != gfx::SurfaceFormat::R8G8B8X8 &&
      aFormat != gfx::SurfaceFormat::B8G8R8A8 &&
      aFormat != gfx::SurfaceFormat::B8G8R8X8 &&
      aFormat != gfx::SurfaceFormat::R5G6B5_UINT16) {
    return nullptr;
  }

  AHardwareBuffer_Desc desc = {};
  desc.width = aSize.width;
  desc.height = aSize.height;
  desc.layers = 1;  // number of images
  desc.usage = AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
               AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN |
               AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
               AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT;
  desc.format = ToAHardwareBuffer_Format(aFormat);

  AHardwareBuffer* nativeBuffer = nullptr;
  AHardwareBuffer_allocate(&desc, &nativeBuffer);
  if (!nativeBuffer) {
    return nullptr;
  }

  AHardwareBuffer_Desc bufferInfo = {};
  AHardwareBuffer_describe(nativeBuffer, &bufferInfo);

  RefPtr<AndroidHardwareBuffer> buffer = new AndroidHardwareBuffer(
      nativeBuffer, aSize, bufferInfo.stride, aFormat);
  if (auto* manager = AndroidHardwareBufferManager::Get()) {
    manager->Register(buffer);
  }
  return buffer.forget();
}

AndroidHardwareBuffer::AndroidHardwareBuffer(AHardwareBuffer* aNativeBuffer,
                                             gfx::IntSize aSize,
                                             uint32_t aStride,
                                             gfx::SurfaceFormat aFormat)
    : mSize(aSize),
      mStride(aStride),
      mFormat(aFormat),
      mId(GetNextId()),
      mNativeBuffer(aNativeBuffer),
      mIsRegistered(false) {
  MOZ_ASSERT(mNativeBuffer);
#ifdef DEBUG
  AHardwareBuffer_Desc bufferInfo = {};
  AHardwareBuffer_describe(mNativeBuffer, &bufferInfo);
  MOZ_ASSERT(mSize.width == (int32_t)bufferInfo.width);
  MOZ_ASSERT(mSize.height == (int32_t)bufferInfo.height);
  MOZ_ASSERT(mStride == bufferInfo.stride);
  MOZ_ASSERT(ToAHardwareBuffer_Format(mFormat) == bufferInfo.format);
#endif
}

AndroidHardwareBuffer::~AndroidHardwareBuffer() {
  if (mIsRegistered) {
    AndroidHardwareBufferManager::Get()->Unregister(this);
  }
  AHardwareBuffer_release(mNativeBuffer);
}

UniqueFileHandle AndroidHardwareBuffer::SerializeToFileDescriptor() const {
  int fd[2];
  if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, fd) != 0) {
    gfxCriticalNote << "AndroidHardwareBuffer::SerializeToFileDescriptor: "
                       "Failed to create Unix socket";
    return UniqueFileHandle();
  }

  UniqueFileHandle readerFd(fd[0]);
  UniqueFileHandle writerFd(fd[1]);
  const int ret =
      AHardwareBuffer_sendHandleToUnixSocket(mNativeBuffer, writerFd.get());
  if (ret < 0) {
    gfxCriticalNote << "AndroidHardwareBuffer::SerializeToFileDescriptor: "
                       "sendHandleToUnixSocket failed";
    return UniqueFileHandle();
  }

  return readerFd;
}

/* static */
already_AddRefed<AndroidHardwareBuffer>
AndroidHardwareBuffer::DeserializeFromFileDescriptor(UniqueFileHandle&& aFd) {
  if (!aFd) {
    gfxCriticalNote << "AndroidHardwareBuffer::DeserializeFromFileDescriptor: "
                       "Invalid FileDescriptor";
    return nullptr;
  }

  AHardwareBuffer* nativeBuffer = nullptr;
  int ret = AHardwareBuffer_recvHandleFromUnixSocket(aFd.get(), &nativeBuffer);
  if (ret < 0) {
    gfxCriticalNote << "AndroidHardwareBuffer::DeserializeFromFileDescriptor: "
                       "recvHandleFromUnixSocket failed";
    return nullptr;
  }

  AHardwareBuffer_Desc desc = {};
  AHardwareBuffer_describe(nativeBuffer, &desc);

  const auto format = ToSurfaceFormat(desc.format);
  if (!format) {
    gfxCriticalNote << "AndroidHardwareBuffer::DeserializeFromFileDescriptor: "
                       "Unrecognized AHARDWAREBUFFER_FORMAT";
    AHardwareBuffer_release(nativeBuffer);
    return nullptr;
  }

  RefPtr<AndroidHardwareBuffer> buffer = new AndroidHardwareBuffer(
      nativeBuffer, gfx::IntSize(desc.width, desc.height), desc.stride,
      *format);

  return buffer.forget();
}

int AndroidHardwareBuffer::Lock(uint64_t aUsage, const ARect* aRect,
                                void** aOutVirtualAddress) {
  UniqueFileHandle fd = GetAndResetReleaseFence();
  return AHardwareBuffer_lock(mNativeBuffer, aUsage, fd.release(), aRect,
                              aOutVirtualAddress);
}

int AndroidHardwareBuffer::Unlock() {
  int rawFd = -1;
  // XXX All tested recent Android devices did not return valid fence.
  int ret = AHardwareBuffer_unlock(mNativeBuffer, &rawFd);
  if (ret != 0) {
    return ret;
  }

  SetAcquireFence(UniqueFileHandle(rawFd));
  return 0;
}

/* static */
UniqueFileHandle AndroidHardwareBuffer::MergeFences(
    UniqueFileHandle&& aFence1, UniqueFileHandle&& aFence2) {
  if (!aFence1) {
    return aFence2;
  }

  if (!aFence2) {
    return aFence1;
  }

  UniqueFileHandle fence1 = std::move(aFence1);
  UniqueFileHandle fence2 = std::move(aFence2);

  int fd = sync_merge("MergeFences", fence1.get(), fence2.get());
  if (fd < 0) {
    gfxCriticalNote << "AndroidHardwareBuffer::MergeFences: "
                       "failed to merge fences";
    return UniqueFileHandle();
  }

  return UniqueFileHandle(fd);
}

void AndroidHardwareBuffer::SetReleaseFence(UniqueFileHandle&& aFenceFd) {
  MonitorAutoLock lock(mMonitor);
  mReleaseFenceFd =
      MergeFences(std::move(mReleaseFenceFd), std::move(aFenceFd));
}

void AndroidHardwareBuffer::SetAcquireFence(UniqueFileHandle&& aFenceFd) {
  MonitorAutoLock lock(mMonitor);

  // Preserve an unconsumed acquire fence rather than silently dropping it.
  mAcquireFenceFd =
      MergeFences(std::move(mAcquireFenceFd), std::move(aFenceFd));
}

UniqueFileHandle AndroidHardwareBuffer::GetAndResetReleaseFence() {
  MonitorAutoLock lock(mMonitor);
  return std::move(mReleaseFenceFd);
}

UniqueFileHandle AndroidHardwareBuffer::GetAndResetAcquireFence() {
  MonitorAutoLock lock(mMonitor);
  return std::move(mAcquireFenceFd);
}

UniqueFileHandle AndroidHardwareBuffer::GetAndResetAllFencesMerged() {
  MonitorAutoLock lock(mMonitor);
  return MergeFences(std::move(mAcquireFenceFd), std::move(mReleaseFenceFd));
}

UniqueFileHandle AndroidHardwareBuffer::GetAcquireFence() const {
  MonitorAutoLock lock(mMonitor);
  if (!mAcquireFenceFd) {
    return UniqueFileHandle();
  }

  return DuplicateFileHandle(mAcquireFenceFd);
}

StaticAutoPtr<AndroidHardwareBufferManager>
    AndroidHardwareBufferManager::sInstance;

/* static */
void AndroidHardwareBufferManager::Init() {
  MOZ_ASSERT(XRE_IsGPUProcess());

  sInstance = new AndroidHardwareBufferManager();
}

/* static */
void AndroidHardwareBufferManager::Shutdown() { sInstance = nullptr; }

void AndroidHardwareBufferManager::Register(
    RefPtr<AndroidHardwareBuffer> aBuffer) {
  MonitorAutoLock lock(mMonitor);

  aBuffer->mIsRegistered = true;
  ThreadSafeWeakPtr<AndroidHardwareBuffer> weak(aBuffer);

#ifdef DEBUG
  const auto it = mBuffers.find(aBuffer->mId);
  MOZ_ASSERT(it == mBuffers.end());
#endif
  mBuffers.emplace(aBuffer->mId, weak);
}

void AndroidHardwareBufferManager::Unregister(AndroidHardwareBuffer* aBuffer) {
  MonitorAutoLock lock(mMonitor);

  const auto it = mBuffers.find(aBuffer->mId);
  MOZ_ASSERT(it != mBuffers.end());
  if (it == mBuffers.end()) {
    gfxCriticalNote << "AndroidHardwareBuffer id mismatch happened";
    return;
  }
  mBuffers.erase(it);
}

already_AddRefed<AndroidHardwareBuffer> AndroidHardwareBufferManager::GetBuffer(
    uint64_t aBufferId) const {
  MonitorAutoLock lock(mMonitor);

  const auto it = mBuffers.find(aBufferId);
  if (it == mBuffers.end()) {
    return nullptr;
  }
  auto buffer = RefPtr<AndroidHardwareBuffer>(it->second);
  return buffer.forget();
}

}  // namespace layers
}  // namespace mozilla
