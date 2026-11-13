/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_LAYERS_ANDROID_HARDWARE_BUFFER
#define MOZILLA_LAYERS_ANDROID_HARDWARE_BUFFER

#include <android/hardware_buffer.h>

#include <unordered_map>

#include "mozilla/Monitor.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/ThreadSafeWeakPtr.h"
#include "mozilla/UniquePtrExtensions.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/layers/AndroidHardwareBuffer.h"
#include "mozilla/layers/TextureClient.h"

namespace mozilla {
namespace layers {

/**
 * AndroidHardwareBuffer is a wrapper of AHardwareBuffer. AHardwareBuffer wraps
 * android GraphicBuffer. It is supported since Android O(APIVersion 26).
 * The manager is mainly used for release fences delivery from
 * host side to client side.
 */
class AndroidHardwareBuffer
    : public SupportsThreadSafeWeakPtr<AndroidHardwareBuffer> {
 public:
  MOZ_DECLARE_REFCOUNTED_TYPENAME(AndroidHardwareBuffer)

  static UniqueFileHandle MergeFences(UniqueFileHandle&& aFence1,
                                      UniqueFileHandle&& aFence2);

  static constexpr uint64_t kDefaultUsage =
      AHARDWAREBUFFER_USAGE_CPU_READ_OFTEN |
      AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN |
      AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE |
      AHARDWAREBUFFER_USAGE_GPU_COLOR_OUTPUT;

  static already_AddRefed<AndroidHardwareBuffer> Create(
      gfx::IntSize aSize, gfx::SurfaceFormat aFormat,
      uint64_t aUsage = kDefaultUsage);

  virtual ~AndroidHardwareBuffer();

  // Serializes the AndroidHardwareBuffer to a file descriptor that can, if
  // desired, be shared to another process, and deserialized with
  // DeserializeFromFileDescriptor(). Note that while an AndroidHardwareBuffer
  // deserialized from the returned file descriptor will refer to the same
  // underlying system buffer, it will be a different instance of this class.
  // Fences will therefore not be propagated between the instances.
  UniqueFileHandle SerializeToFileDescriptor() const;

  // Creates an AndroidHardwareBuffer from a file descriptor that was previously
  // obtained from ToFileDescriptor(). The file descriptor may have been shared
  // from another process. Note that while the returned AndroidHardwareBuffer
  // refers to the same underlying system buffer as the one that was originally
  // serialized, it will be a different instance of this class. Fences will
  // therefore not be propagated between the instances.
  static already_AddRefed<AndroidHardwareBuffer> DeserializeFromFileDescriptor(
      UniqueFileHandle&& aFd);

  int Lock(uint64_t aUsage, const ARect* aRect, void** aOutVirtualAddress);
  int Unlock();

  AHardwareBuffer* GetNativeBuffer() const { return mNativeBuffer; }

  void SetAcquireFence(UniqueFileHandle&& aFenceFd);

  void SetReleaseFence(UniqueFileHandle&& aFenceFd);

  UniqueFileHandle GetAndResetReleaseFence();

  UniqueFileHandle GetAndResetAcquireFence();

  UniqueFileHandle GetAndResetAllFencesMerged();

  UniqueFileHandle GetAcquireFence() const;

  const gfx::IntSize mSize;
  const uint32_t mStride;
  const gfx::SurfaceFormat mFormat;
  const uint64_t mId;

 protected:
  AndroidHardwareBuffer(AHardwareBuffer* aNativeBuffer, gfx::IntSize aSize,
                        uint32_t aStride, gfx::SurfaceFormat aFormat);

  AHardwareBuffer* mNativeBuffer;

  // When true, AndroidHardwareBuffer is registered to
  // AndroidHardwareBufferManager.
  bool mIsRegistered;

  mutable Monitor mMonitor{"AndroidHardwareBuffer::mMonitor"};

  // FileDescriptor of release fence.
  // Release fence is a fence that is used for waiting until usage/composite of
  // AHardwareBuffer is ended. The fence is delivered via ImageBridge.
  UniqueFileHandle mReleaseFenceFd MOZ_GUARDED_BY(mMonitor);

  // FileDescriptor of acquire fence.
  // Acquire fence is a fence that is used for waiting until rendering to
  // its AHardwareBuffer is completed.
  UniqueFileHandle mAcquireFenceFd MOZ_GUARDED_BY(mMonitor);

  static uint64_t GetNextId();

  friend class AndroidHardwareBufferManager;
};

/**
 * AndroidHardwareBufferManager manages AndroidHardwareBuffers that is
 * allocated by client side.
 * Host side only uses mMonitor for thread safety of AndroidHardwareBuffer.
 */
class AndroidHardwareBufferManager {
 public:
  static void Init();
  static void Shutdown();

  static AndroidHardwareBufferManager* Get() { return sInstance; }

  void Register(RefPtr<AndroidHardwareBuffer> aBuffer);

  void Unregister(AndroidHardwareBuffer* aBuffer);

  already_AddRefed<AndroidHardwareBuffer> GetBuffer(uint64_t aBufferId) const;

 private:
  AndroidHardwareBufferManager() = default;

  mutable Monitor mMonitor{"AndroidHardwareBufferManager::mMonitor"};
  std::unordered_map<uint64_t, ThreadSafeWeakPtr<AndroidHardwareBuffer>>
      mBuffers MOZ_GUARDED_BY(mMonitor);

  static StaticAutoPtr<AndroidHardwareBufferManager> sInstance;
};

}  // namespace layers
}  // namespace mozilla

#endif
