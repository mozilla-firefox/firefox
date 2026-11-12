/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_LAYERS_AndroidImageReader
#define MOZILLA_LAYERS_AndroidImageReader

#include <unordered_map>

#include "GLImages.h"
#include "ImageContainer.h"
#include "mozilla/Atomics.h"
#include "mozilla/Maybe.h"
#include "mozilla/Monitor.h"
#include "mozilla/RefPtr.h"
#include "mozilla/StaticPtr.h"
#include "mozilla/ThreadSafeWeakPtr.h"
#include "mozilla/UniquePtrExtensions.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/layers/LayersSurfaces.h"
#include "mozilla/layers/LayersTypes.h"
#include "nsISupportsImpl.h"

struct AHardwareBuffer;
struct AImage;
struct AImageReader;
struct AImageReader_ImageListener;
struct ANativeWindow;

namespace mozilla {
namespace layers {

class AndroidImageConsumer;
class AndroidImageReader;

/**
 * A class wraps information of AndroidImageReader and its frame.
 */
class AndroidImageReaderImage final : public Image {
 public:
  AndroidImageReaderImage(const GpuProcessAndroidImageReaderId aImageReaderId,
                          const gfx::IntSize& aSize, const bool aHasAlpha);

  gfx::IntSize GetSize() const override { return mSize; }

  gl::OriginPos GetOriginPos() const { return gl::OriginPos::TopLeft; }

  already_AddRefed<gfx::SourceSurface> GetAsSourceSurface() override {
    return nullptr;
  }

  nsresult BuildSurfaceDescriptorBuffer(
      SurfaceDescriptorBuffer& aSdBuffer, BuildSdbFlags aFlags,
      const std::function<MemoryOrShmem(uint32_t)>& aAllocate) override;

  AndroidImageReaderImage* AsAndroidImageReaderImage() override { return this; }

  Maybe<SurfaceDescriptor> GetDesc() override;

  void OnSetCurrent() override;

  bool MaybeReleaseFrameToCodec(const MonitorAutoLock& aProofOfLock,
                                bool aRender);

  void RegisterSetCurrentCallback(
      UniquePtr<SurfaceTextureImage::SetCurrentCallback> aCallback) {
    mSetCurrentCallback = std::move(aCallback);
  }

  const GpuProcessAndroidImageReaderId mImageReaderId;
  const AndroidMediaCodecFrameId mFrameId;
  const gfx::IntSize mSize;
  const bool mHasAlpha;

 private:
  virtual ~AndroidImageReaderImage();

  UniquePtr<SurfaceTextureImage::SetCurrentCallback> mSetCurrentCallback;
};

/**
 * A class wraps AHardwareBuffer and AImage. The class is used by
 * AndroidImageReader user to access them.
 */
class AndroidImageWrapper {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(AndroidImageWrapper);

  explicit AndroidImageWrapper(AndroidImageReader* aImageReader, AImage* aImage,
                               AHardwareBuffer* aHardwareBuffer,
                               const gfx::IntSize aSize,
                               const gfx::SurfaceFormat aFormat,
                               mozilla::UniqueFileHandle&& aWriteFenceFd);

  mozilla::UniqueFileHandle CloneWriteFenceFd();

  void SetReadFenceFd(UniqueFileHandle&& aFenceFd);

  const AHardwareBuffer* mHardwareBuffer;
  const gfx::IntSize mSize;
  const gfx::SurfaceFormat mFormat;

 protected:
  ~AndroidImageWrapper();

  Mutex mMutex;

  const RefPtr<AndroidImageReader> mImageReader;
  AImage* mImage;
  const mozilla::UniqueFileHandle mWriteFenceFd MOZ_GUARDED_BY(mMutex);
  mozilla::UniqueFileHandle mReadFenceFd MOZ_GUARDED_BY(mMutex);
};

/**
 * AndroidImageReader is a wrapper of AImageReader. It gets AImage, an opaque
 * type that allows direct application access to image data rendered into a
 * ANativeWindow. And it allows to get AHardwareBuffer from AImage.
 * AHardwareBuffer wraps android GraphicBuffer.
 */
class AndroidImageReader final
    : public SupportsThreadSafeWeakPtr<AndroidImageReader> {
 public:
  MOZ_DECLARE_REFCOUNTED_TYPENAME(AndroidImageReader)

  virtual ~AndroidImageReader();

  static already_AddRefed<AndroidImageReader> Create();

  explicit AndroidImageReader();

  bool UpdateTexImage(AndroidMediaCodecFrameId aFrameId, gl::GLContext* aGL,
                      GLuint aTexture, AndroidImageWrapper** aImage);

  bool UpdateTexImageWithReadback(AndroidMediaCodecFrameId aFrameId,
                                  gl::GLContext* aGL,
                                  gfx::DataSourceSurface* aSurface,
                                  const gfx::IntSize aSize,
                                  const gfx::SurfaceFormat aFormat);

  void MaybeRenderFirstFrame(AndroidMediaCodecFrameId aFrameId);

  bool DoUpdateTexImage(const MonitorAutoLock& aProofOfLock,
                        AndroidMediaCodecFrameId aFrameId)
      MOZ_REQUIRES(mMonitor);

  ANativeWindow* GetANativeWindow();

  RefPtr<AndroidImageWrapper> GetCurrentImage();

  void RegisterReaderImage(AndroidImageReaderImage* aReaderImage);

  bool MaybeReleaseFrameToCodec(const AndroidMediaCodecFrameId aFrameId,
                                const bool aRender);

  bool MaybeReleaseFrameToCodec(const MonitorAutoLock& aProofOfLock,
                                const AndroidMediaCodecFrameId aFrameId,
                                const bool aRender) MOZ_REQUIRES(mMonitor);

  void NotifyFrameAvailable();

  // AndroidImageConsumer::UpdateTexImage() requests at least 2 concurrently
  // acquired AImages.
  const int32_t mMaxImageCount = 2;
  const GpuProcessAndroidImageReaderId mImageReaderId;

 protected:
  friend class AndroidImageWrapper;

  bool AcquirePendingImage() MOZ_REQUIRES(mMonitor);

  bool Init();
  void ReleaseResources();

  static void OnFrameAvailable(void* aContext, AImageReader* aReader);

  Monitor mMonitor;

  bool mRegistered = false;
  bool mInited MOZ_GUARDED_BY(mMonitor) = false;
  AImageReader* mAImageReader MOZ_GUARDED_BY(mMonitor) = nullptr;
  ANativeWindow* mNativeWindow MOZ_GUARDED_BY(mMonitor) = nullptr;
  AndroidMediaCodecFrameId mCurrentFrameId MOZ_GUARDED_BY(mMonitor);
  RefPtr<AndroidImageWrapper> mCurrentImage MOZ_GUARDED_BY(mMonitor);
  // A rendered codec buffer remains outstanding across notification timeouts.
  // Keep its identity until its AImage has been acquired.
  Maybe<AndroidMediaCodecFrameId> mPendingFrameId MOZ_GUARDED_BY(mMonitor);
  bool mWaitingFrameAvailable MOZ_GUARDED_BY(mMonitor) = false;
  bool mIsPendingNextImage MOZ_GUARDED_BY(mMonitor) = false;

  mozilla::Atomic<int32_t> mAcquiredImageCount{0};

  // mPendingFrames does not own AndroidImageReaderImage and stores only its
  // pointer.
  // AndroidImageReaderImage destructor ensures it is removed from the map.
  std::unordered_map<AndroidMediaCodecFrameId, AndroidImageReaderImage*,
                     AndroidMediaCodecFrameId::HashFn>
      mPendingFrames MOZ_GUARDED_BY(mMonitor);
};

/**
 * A class to manage AndroidImageReader and AndroidImageConsumer shared in GPU
 * process by using GpuProcessAndroidImageReaderId.
 */
class GpuProcessAndroidImageReaderMap {
 public:
  static void Init();
  static void Shutdown();

  GpuProcessAndroidImageReaderMap();

  static GpuProcessAndroidImageReaderMap* Get() { return sInstance; }

  void Register(AndroidImageReader* aImageReader);
  void Unregister(GpuProcessAndroidImageReaderId aImageReaderId);

  RefPtr<AndroidImageReader> GetImageReader(
      GpuProcessAndroidImageReaderId aImageReaderId);
  bool MaybeReleaseFrameToCodec(
      const GpuProcessAndroidImageReaderId aImageReaderId,
      const AndroidMediaCodecFrameId aFrameId, const bool aRender);

  RefPtr<AndroidImageConsumer> GetImageConsumer(
      const GpuProcessAndroidImageReaderId aImageReaderId, gl::GLContext* aGL);

  void UnregisterImageConsumer(GpuProcessAndroidImageReaderId aImageReaderId);

 private:
  struct ImageReaderHolder {
    explicit ImageReaderHolder(AndroidImageReader* aImageReader);
    ~ImageReaderHolder();

    ThreadSafeWeakPtr<AndroidImageReader> mImageReader;
    AndroidImageConsumer* mImageConsumer = nullptr;
  };

  Monitor mMonitor;
  std::unordered_map<GpuProcessAndroidImageReaderId,
                     UniquePtr<ImageReaderHolder>,
                     GpuProcessAndroidImageReaderId::HashFn>
      mImageReaders MOZ_GUARDED_BY(mMonitor);

  static StaticAutoPtr<GpuProcessAndroidImageReaderMap> sInstance;
};

}  // namespace layers
}  // namespace mozilla

#endif
