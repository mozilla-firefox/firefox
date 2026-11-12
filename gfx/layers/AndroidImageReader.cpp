/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AndroidImageReader.h"

#include <android/hardware_buffer.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>

#include "GLContext.h"
#include "GLContextEGL.h"
#include "GLContextProvider.h"
#include "GLLibraryEGL.h"
#include "GLReadTexImageHelper.h"
#include "OGLShaderConfig.h"
#include "ScopedGLHelpers.h"
#include "mozilla/RemoteMediaManagerParent.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/layers/AndroidHardwareBuffer.h"
#include "mozilla/layers/AndroidImageConsumer.h"
#include "mozilla/webrender/RenderThread.h"
#include "nsProxyRelease.h"
#include "nsThreadUtils.h"

namespace mozilla {
namespace layers {

static StaticMutex sAImageReadbackMutex;
// Serialize readback and shutdown, including EGL context handoff between
// workers.
static bool sAImageReadbackShuttingDown MOZ_GUARDED_BY(sAImageReadbackMutex) =
    false;
constinit static RefPtr<gl::GLContext> sAImageSnapshotContext
    MOZ_GUARDED_BY(sAImageReadbackMutex);

static void DestroyAImageSnapshotContext() MOZ_REQUIRES(sAImageReadbackMutex) {
  if (!sAImageSnapshotContext) {
    return;
  }

  // Destruction can make the context current to delete GL resources. Keep the
  // EGL display alive and unbind afterwards, including for surfaceless
  // contexts.
  const auto egl = gl::GLContextEGL::Cast(sAImageSnapshotContext)->mEgl;
  sAImageSnapshotContext = nullptr;
  if (!egl->fMakeCurrent(EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
    gfxCriticalNote
        << "Failed to release destroyed ImageReader snapshot context";
  }
  gl::GLContext::ResetTLSCurrentContext();
}

static void ReleaseAImageSnapshotContext() MOZ_REQUIRES(sAImageReadbackMutex) {
  if (!sAImageSnapshotContext) {
    return;
  }

  const auto egl = gl::GLContextEGL::Cast(sAImageSnapshotContext)->mEgl;
  if (!egl->fMakeCurrent(EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT)) {
    // Never reuse a context which may still be current on another pool worker.
    gfxCriticalNote << "Failed to release ImageReader snapshot context";
    DestroyAImageSnapshotContext();
  }
  gl::GLContext::ResetTLSCurrentContext();
}

AndroidImageReaderImage::AndroidImageReaderImage(
    const GpuProcessAndroidImageReaderId aImageReaderId,
    const gfx::IntSize& aSize, const bool aHasAlpha)
    : Image(nullptr, ImageFormat::ANDROID_IMAGE_READER),
      mImageReaderId(aImageReaderId),
      mFrameId(AndroidMediaCodecFrameId::GetNext()),
      mSize(aSize),
      mHasAlpha(aHasAlpha) {
  MOZ_ASSERT(XRE_IsGPUProcess());
}

AndroidImageReaderImage::~AndroidImageReaderImage() {
  auto* imageReaderMap = layers::GpuProcessAndroidImageReaderMap::Get();
  if (imageReaderMap) {
    imageReaderMap->MaybeReleaseFrameToCodec(mImageReaderId, mFrameId,
                                             /* aRender */ false);
  }
}

Maybe<SurfaceDescriptor> AndroidImageReaderImage::GetDesc() {
  return Nothing();
}

void AndroidImageReaderImage::OnSetCurrent() {
  auto* imageReaderMap = layers::GpuProcessAndroidImageReaderMap::Get();
  if (!imageReaderMap) {
    return;
  }

  RefPtr<AndroidImageReader> imageReader =
      imageReaderMap->GetImageReader(mImageReaderId);
  if (!imageReader) {
    return;
  }

  imageReader->MaybeRenderFirstFrame(mFrameId);
}

bool AndroidImageReaderImage::MaybeReleaseFrameToCodec(
    const MonitorAutoLock& aProofOfLock, bool aRender) {
  if (!mSetCurrentCallback) {
    return false;
  }

  bool ret = (*mSetCurrentCallback)(aRender);
  mSetCurrentCallback.reset();
  return ret;
}

nsresult AndroidImageReaderImage::BuildSurfaceDescriptorBuffer(
    SurfaceDescriptorBuffer& aSdBuffer, BuildSdbFlags aFlags,
    const std::function<MemoryOrShmem(uint32_t)>& aAllocate) {
  MOZ_ASSERT(RemoteMediaManagerParent::OnManagerThread());

  StaticMutexAutoLock lock(sAImageReadbackMutex);
  if (sAImageReadbackShuttingDown) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  if (!sAImageSnapshotContext) {
    nsCString discardFailureId;
    sAImageSnapshotContext =
        gl::GLContextProvider::CreateHeadless({}, &discardFailureId);
    if (!sAImageSnapshotContext) {
      NS_WARNING("Failed to create snapshot GLContext");
      return NS_ERROR_FAILURE;
    }
    // The manager TaskQueue serializes calls but can run on different pool
    // workers. The mutex and EGL unbind enforce exclusive context ownership.
    sAImageSnapshotContext->mOwningThreadId = Nothing();
  }

  // CreateHeadless also makes the context current. Unbind on every exit, after
  // UpdateTexImageWithReadback has destroyed all of its scoped GL resources.
  auto releaseContext = MakeScopeExit([]() MOZ_REQUIRES(sAImageReadbackMutex) {
    ReleaseAImageSnapshotContext();
  });

  auto* imageReaderMap = layers::GpuProcessAndroidImageReaderMap::Get();
  if (!imageReaderMap) {
    return NS_ERROR_FAILURE;
  }

  RefPtr<AndroidImageReader> imageReader =
      imageReaderMap->GetImageReader(mImageReaderId);
  if (!imageReader) {
    return NS_ERROR_FAILURE;
  }

  gfx::IntSize size = GetSize();
  // auto format = gfx::SurfaceFormat::R8G8B8X8
  auto format = gfx::SurfaceFormat::B8G8R8A8;

  uint8_t* buffer = nullptr;
  int32_t stride = 0;
  nsresult rv = Image::AllocateSurfaceDescriptorBufferRgb(
      size, format, buffer, aSdBuffer, stride, aAllocate);
  if (NS_FAILED(rv)) {
    return rv;
  }

  RefPtr<gfx::DataSourceSurface> surface;
  surface = gfx::Factory::CreateWrappingDataSourceSurface(buffer, stride, size,
                                                          format);
  if (!surface) {
    return NS_ERROR_FAILURE;
  }

  if (!imageReader->UpdateTexImageWithReadback(mFrameId, sAImageSnapshotContext,
                                               surface, size, format)) {
    return NS_ERROR_FAILURE;
  }

  return NS_OK;
}

AndroidImageWrapper::AndroidImageWrapper(
    AndroidImageReader* aImageReader, AImage* aImage,
    AHardwareBuffer* aHardwareBuffer, const gfx::IntSize aSize,
    const gfx::SurfaceFormat aFormat, mozilla::UniqueFileHandle&& aWriteFenceFd)
    : mHardwareBuffer(aHardwareBuffer),
      mSize(aSize),
      mFormat(aFormat),
      mMutex("AndroidImageWrapper::mMutex"),
      mImageReader(aImageReader),
      mImage(aImage),
      mWriteFenceFd(std::move(aWriteFenceFd)) {
  MOZ_ASSERT(mImageReader);
  mImageReader->mAcquiredImageCount++;
}

AndroidImageWrapper::~AndroidImageWrapper() {
  MutexAutoLock lock(mMutex);

  if (mReadFenceFd) {
    AImage_deleteAsync(mImage, mReadFenceFd.get());
    mReadFenceFd.release();
  } else {
    AImage_delete(mImage);
  }
  mImageReader->mAcquiredImageCount--;
}

mozilla::UniqueFileHandle AndroidImageWrapper::CloneWriteFenceFd() {
  MutexAutoLock lock(mMutex);

  auto writeFenceFd = ipc::FileDescriptor(mWriteFenceFd.get());
  return writeFenceFd.TakePlatformHandle();
}

void AndroidImageWrapper::SetReadFenceFd(UniqueFileHandle&& aFenceFd) {
  MutexAutoLock lock(mMutex);

  mReadFenceFd = AndroidHardwareBuffer::MergeFences(std::move(mReadFenceFd),
                                                    std::move(aFenceFd));
}

/* static */
already_AddRefed<AndroidImageReader> AndroidImageReader::Create() {
  if (!XRE_IsGPUProcess()) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return nullptr;
  }

  auto* imageReaderMap = layers::GpuProcessAndroidImageReaderMap::Get();
  if (!imageReaderMap) {
    return nullptr;
  }

  RefPtr<AndroidImageReader> imageReader = new AndroidImageReader();
  bool ret = imageReader->Init();
  if (!ret) {
    return nullptr;
  }

  imageReaderMap->Register(imageReader);
  imageReader->mRegistered = true;

  return imageReader.forget();
}

AndroidImageReader::AndroidImageReader()
    : mImageReaderId(GpuProcessAndroidImageReaderId::GetNext()),
      mMonitor("mozilla.layers.AndroidImageReader.mMonitor") {}

AndroidImageReader::~AndroidImageReader() {
  auto* imageReaderMap = layers::GpuProcessAndroidImageReaderMap::Get();
  if (imageReaderMap && mRegistered) {
    imageReaderMap->Unregister(mImageReaderId);
  }
  ReleaseResources();
}

bool AndroidImageReader::Init() {
  MonitorAutoLock lock(mMonitor);

  MOZ_ASSERT(!mInited);

  // Set the width, height and format to some default value. This parameters
  // are/maybe overriden by the producer sending buffers to this imageReader's
  // Surface.
  const int32_t width = 1, height = 1;
  uint64_t usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE;

  // XXX set if video could be used for overlay.
  // usage |= AHARDWAREBUFFER_USAGE_COMPOSER_OVERLAY;

  media_status_t result;
  AImageReader* reader = nullptr;
  result = AImageReader_newWithUsage(width, height, AIMAGE_FORMAT_PRIVATE,
                                     usage, mMaxImageCount, &reader);
  if (result != AMEDIA_OK) {
    gfxCriticalNoteOnce << "AImageReader_newWithUsage failed"
                        << static_cast<int32_t>(result);
    return false;
  }

  mAImageReader = reader;

  ANativeWindow* window = nullptr;
  result = AImageReader_getWindow(reader, &window);
  if (result != AMEDIA_OK) {
    gfxCriticalNoteOnce << "AImageReader_getWindow failed"
                        << static_cast<int32_t>(result);
    return false;
  }
  mNativeWindow = window;

  auto listener = std::make_unique<AImageReader_ImageListener>();
  listener->context = reinterpret_cast<void*>(this);
  listener->onImageAvailable = &AndroidImageReader::OnFrameAvailable;

  result = AImageReader_setImageListener(reader, listener.get());
  if (result != AMEDIA_OK) {
    gfxCriticalNoteOnce << "setImageListener failed"
                        << static_cast<int32_t>(result);
    return false;
  }

  MOZ_ASSERT(mAImageReader);
  MOZ_ASSERT(mNativeWindow);
  mInited = true;

  return true;
}

void AndroidImageReader::ReleaseResources() {
  AImageReader* reader = nullptr;
  {
    MonitorAutoLock lock(mMonitor);
    reader = mAImageReader;
    mAImageReader = nullptr;
    // The native window is owned by AImageReader.
    mNativeWindow = nullptr;
  }
  if (!reader) {
    return;
  }

  // Deletion stops the callback thread. Do not hold the monitor that an
  // in-flight OnFrameAvailable callback needs to finish.
  AImageReader_setImageListener(reader, nullptr);
  AImageReader_delete(reader);
}

/* static */
void AndroidImageReader::OnFrameAvailable(void* aContext,
                                          AImageReader* aReader) {
  auto* reader = static_cast<AndroidImageReader*>(aContext);

  reader->NotifyFrameAvailable();
}

void AndroidImageReader::NotifyFrameAvailable() {
  MonitorAutoLock lock(mMonitor);
  mWaitingFrameAvailable = false;
  mMonitor.NotifyAll();
}

bool AndroidImageReader::UpdateTexImage(const AndroidMediaCodecFrameId aFrameId,
                                        gl::GLContext* aGL, GLuint aTexture,
                                        AndroidImageWrapper** aImage) {
  MOZ_ASSERT(aGL);
  MOZ_ASSERT(aImage);

  MonitorAutoLock lock(mMonitor);

  DebugOnly<bool> ret = DoUpdateTexImage(lock, aFrameId);
  if (!mCurrentImage || *aImage == mCurrentImage) {
    MOZ_ASSERT(!ret);
    return false;
  }

  MOZ_ASSERT(mCurrentImage);

  // XXX add fence handling

  const auto& gle = gl::GLContextEGL::Cast(aGL);
  const auto& egl = gle->mEgl;

  const EGLint attrs[] = {
      LOCAL_EGL_IMAGE_PRESERVED,
      LOCAL_EGL_TRUE,
      LOCAL_EGL_NONE,
  };

  auto* nativeBuffer = mCurrentImage->mHardwareBuffer;

  EGLClientBuffer clientBuffer =
      egl->mLib->fGetNativeClientBufferANDROID(nativeBuffer);
  EGLImage eglImage = egl->fCreateImage(
      EGL_NO_CONTEXT, LOCAL_EGL_NATIVE_BUFFER_ANDROID, clientBuffer, attrs);

  MOZ_ASSERT(eglImage);
  if (!eglImage) {
    return false;
  }

  aGL->fBindTexture(LOCAL_GL_TEXTURE_EXTERNAL, aTexture);
  aGL->fTexParameteri(LOCAL_GL_TEXTURE_EXTERNAL, LOCAL_GL_TEXTURE_WRAP_T,
                      LOCAL_GL_CLAMP_TO_EDGE);
  aGL->fTexParameteri(LOCAL_GL_TEXTURE_EXTERNAL, LOCAL_GL_TEXTURE_WRAP_S,
                      LOCAL_GL_CLAMP_TO_EDGE);
  aGL->fEGLImageTargetTexture2D(LOCAL_GL_TEXTURE_EXTERNAL, eglImage);
  egl->fDestroyImage(eglImage);

  *aImage = mCurrentImage;
  NS_ADDREF(*aImage);

  return true;
}

bool AndroidImageReader::UpdateTexImageWithReadback(
    AndroidMediaCodecFrameId aFrameId, gl::GLContext* aGL,
    gfx::DataSourceSurface* aSurface, const gfx::IntSize aSize,
    const gfx::SurfaceFormat aFormat) {
  MOZ_ASSERT(aGL);
  MOZ_ASSERT(aSurface);

  MonitorAutoLock lock(mMonitor);

  DoUpdateTexImage(lock, aFrameId);
  // A timeout is recoverable. Do not read back a previous frame as aFrameId.
  if (!mCurrentImage || mCurrentFrameId != aFrameId) {
    return false;
  }

  if (!aGL->MakeCurrent()) {
    return false;
  }
  gl::ScopedTexture scopedTex(aGL);

  const auto& gle = gl::GLContextEGL::Cast(aGL);
  const auto& egl = gle->mEgl;

  auto writeFenceFd = mCurrentImage->CloneWriteFenceFd();
  if (writeFenceFd) {
    if (!egl->IsExtensionSupported(
            gl::EGLExtension::ANDROID_native_fence_sync) ||
        !egl->IsExtensionSupported(gl::EGLExtension::KHR_fence_sync)) {
      return false;
    }

    // EGL takes ownership of the fd even if creating the sync fails.
    const EGLint fenceAttrs[] = {LOCAL_EGL_SYNC_NATIVE_FENCE_FD_ANDROID,
                                 writeFenceFd.release(), LOCAL_EGL_NONE};
    EGLSync sync =
        egl->fCreateSyncKHR(LOCAL_EGL_SYNC_NATIVE_FENCE_ANDROID, fenceAttrs);
    if (!sync) {
      gfxCriticalNote << "Failed to create ImageReader readback acquire sync";
      return false;
    }

    // Order sampling after the decoder's writes on this GL context. Readback
    // subsequently waits for the GL work to complete.
    const bool waited =
        egl->IsExtensionSupported(gl::EGLExtension::KHR_wait_sync)
            ? egl->fWaitSync(sync, 0) == LOCAL_EGL_TRUE
            : egl->fClientWaitSync(sync, 0, LOCAL_EGL_FOREVER) ==
                  LOCAL_EGL_CONDITION_SATISFIED;
    egl->fDestroySync(sync);
    if (!waited) {
      gfxCriticalNote
          << "Failed to wait for ImageReader readback acquire fence";
      return false;
    }
  }

  const EGLint attrs[] = {
      LOCAL_EGL_IMAGE_PRESERVED,
      LOCAL_EGL_TRUE,
      LOCAL_EGL_NONE,
  };

  auto* nativeBuffer = mCurrentImage->mHardwareBuffer;

  EGLClientBuffer clientBuffer =
      egl->mLib->fGetNativeClientBufferANDROID(nativeBuffer);
  EGLImage eglImage = egl->fCreateImage(
      EGL_NO_CONTEXT, LOCAL_EGL_NATIVE_BUFFER_ANDROID, clientBuffer, attrs);

  MOZ_ASSERT(eglImage);

  if (!eglImage) {
    return false;
  }

  aGL->fBindTexture(LOCAL_GL_TEXTURE_EXTERNAL, scopedTex.Texture());
  aGL->fTexParameteri(LOCAL_GL_TEXTURE_EXTERNAL, LOCAL_GL_TEXTURE_WRAP_T,
                      LOCAL_GL_CLAMP_TO_EDGE);
  aGL->fTexParameteri(LOCAL_GL_TEXTURE_EXTERNAL, LOCAL_GL_TEXTURE_WRAP_S,
                      LOCAL_GL_CLAMP_TO_EDGE);
  aGL->fEGLImageTargetTexture2D(LOCAL_GL_TEXTURE_EXTERNAL, eglImage);
  egl->fDestroyImage(eglImage);

  ShaderConfigOGL config =
      ShaderConfigFromTargetAndFormat(LOCAL_GL_TEXTURE_EXTERNAL, aFormat);
  int shaderConfig = config.mFeatures;

  bool ret = aGL->ReadTexImageHelper()->ReadTexImage(
      aSurface, scopedTex.Texture(), LOCAL_GL_TEXTURE_EXTERNAL, aSize,
      gfx::Matrix4x4(), shaderConfig, /* aYInvert */ false);
  if (!ret) {
    return false;
  }

  return true;
}

void AndroidImageReader::MaybeRenderFirstFrame(
    AndroidMediaCodecFrameId aFrameId) {
  MonitorAutoLock lock(mMonitor);
  if (mCurrentImage) {
    return;
  }
  DoUpdateTexImage(lock, aFrameId);
}

bool AndroidImageReader::DoUpdateTexImage(const MonitorAutoLock& aProofOfLock,
                                          AndroidMediaCodecFrameId aFrameId) {
  MOZ_ASSERT(static_cast<int32_t>(mAcquiredImageCount) <= mMaxImageCount);

  if (mCurrentFrameId == aFrameId) {
    return false;
  }

  // Limit as only one thread could do DoUpdateTexImage()
  const TimeDuration pendingTimeout = TimeDuration::FromMilliseconds(20);
  while (mIsPendingNextImage) {
    CVStatus status = mMonitor.Wait(pendingTimeout);
    if (status == CVStatus::Timeout) {
      gfxCriticalNoteOnce << "Pending next image wait timeout";
      return false;
    }
  }
  MOZ_ASSERT(!mIsPendingNextImage);

  auto scopeExit = MakeScopeExit([&]() MOZ_REQUIRES(mMonitor) {
    mIsPendingNextImage = false;
    mMonitor.NotifyAll();
  });

  mIsPendingNextImage = true;

  // Bound the total notification wait in this call, including an older frame
  // left outstanding by a previous timeout.
  const TimeStamp deadline =
      TimeStamp::Now() + TimeDuration::FromMilliseconds(20);
  bool updated = false;

  while (mCurrentFrameId != aFrameId) {
    if (mPendingFrameId.isNothing()) {
      MOZ_ASSERT(!mWaitingFrameAvailable);
      mPendingFrameId = Some(aFrameId);
      mWaitingFrameAvailable = true;
      if (!MaybeReleaseFrameToCodec(aProofOfLock, aFrameId,
                                    /* aRender */ true)) {
        mPendingFrameId.reset();
        mWaitingFrameAvailable = false;
        return updated;
      }
    }

    while (mWaitingFrameAvailable) {
      const TimeDuration remaining = deadline - TimeStamp::Now();
      if (remaining <= TimeDuration()) {
        gfxCriticalNoteOnce << "UpdateTexImage wait timeout";
        // The image may already be queued even if its notification is late.
        // Try acquiring it before giving up on this update.
        break;
      }
      mMonitor.Wait(remaining);
    }

    // OnFrameAvailable does not carry a frame ID. Consume the outstanding
    // frame before releasing another, even when aFrameId has since changed.
    if (!AcquirePendingImage()) {
      // Keep the pending frame ID and notification state if no image was
      // acquired. Do not release another codec buffer until it is consumed.
      return updated;
    }
    updated = true;
  }

  return updated;
}

bool AndroidImageReader::AcquirePendingImage() {
  MOZ_ASSERT(mPendingFrameId.isSome());
  // Acquisition is also allowed before OnFrameAvailable after a timeout.

  AImage* image = nullptr;
  media_status_t ret = AMEDIA_OK;
  UniqueFileHandle fence;
  ret = AImageReader_acquireNextImageAsync(mAImageReader, &image,
                                           getter_Transfers(fence));
  // XXX Add AImageReader_acquireLatestImageAsync() usage

  switch (ret) {
    case AMEDIA_ERROR_INVALID_PARAMETER:
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      return false;
    case AMEDIA_IMGREADER_MAX_IMAGES_ACQUIRED:
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      return false;
    case AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE:
      return false;
    case AMEDIA_ERROR_UNKNOWN:
      return false;
    case AMEDIA_OK:
      // Call succeeded.
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      return false;
  }

  if (!image) {
    return false;
  }

  // This buffer has been consumed even if importing it subsequently fails.
  const AndroidMediaCodecFrameId frameId = mPendingFrameId.value();
  mPendingFrameId.reset();
  mWaitingFrameAvailable = false;

  AHardwareBuffer* nativeBuffer = nullptr;
  media_status_t result = AImage_getHardwareBuffer(image, &nativeBuffer);
  if (!nativeBuffer) {
    gfxCriticalNoteOnce << "AImage_getHardwareBuffer failed"
                        << static_cast<int32_t>(result);
    if (fence) {
      AImage_deleteAsync(image, fence.release());
    } else {
      AImage_delete(image);
    }
    return false;
  }

  AHardwareBuffer_Desc bufferInfo = {};
  AHardwareBuffer_describe(nativeBuffer, &bufferInfo);

  const gfx::IntSize size = gfx::IntSize(bufferInfo.width, bufferInfo.height);
  // XXX
  const gfx::SurfaceFormat format = gfx::SurfaceFormat::R8G8B8X8;

  // XXX add crop handling

  mCurrentImage = new AndroidImageWrapper(this, image, nativeBuffer, size,
                                          format, std::move(fence));
  mCurrentFrameId = frameId;

  MOZ_ASSERT(static_cast<int32_t>(mAcquiredImageCount) <= mMaxImageCount);

  return true;
}

ANativeWindow* AndroidImageReader::GetANativeWindow() {
  MonitorAutoLock lock(mMonitor);
  return mNativeWindow;
}

RefPtr<AndroidImageWrapper> AndroidImageReader::GetCurrentImage() {
  MonitorAutoLock lock(mMonitor);
  return mCurrentImage;
}

void AndroidImageReader::RegisterReaderImage(
    AndroidImageReaderImage* aReaderImage) {
  if (!aReaderImage) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }
  MonitorAutoLock lock(mMonitor);

  MOZ_ASSERT(mPendingFrames.find(aReaderImage->mFrameId) ==
             mPendingFrames.end());
  mPendingFrames.emplace(aReaderImage->mFrameId, aReaderImage);
}

bool AndroidImageReader::MaybeReleaseFrameToCodec(
    const AndroidMediaCodecFrameId aFrameId, const bool aRender) {
  MonitorAutoLock lock(mMonitor);
  return MaybeReleaseFrameToCodec(lock, aFrameId, aRender);
}
bool AndroidImageReader::MaybeReleaseFrameToCodec(
    const MonitorAutoLock& aProofOfLock,
    const AndroidMediaCodecFrameId aFrameId, const bool aRender) {
  auto it = mPendingFrames.find(aFrameId);
  if (it == mPendingFrames.end()) {
    return false;
  }

  bool ret = it->second->MaybeReleaseFrameToCodec(aProofOfLock, aRender);
  mPendingFrames.erase(it);

  return ret;
}

StaticAutoPtr<GpuProcessAndroidImageReaderMap>
    GpuProcessAndroidImageReaderMap::sInstance;

/* static */
void GpuProcessAndroidImageReaderMap::Init() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(XRE_IsGPUProcess());

  StaticMutexAutoLock lock(sAImageReadbackMutex);
  MOZ_ASSERT(!sInstance);
  MOZ_ASSERT(!sAImageSnapshotContext);
  sAImageReadbackShuttingDown = false;
  sInstance = new GpuProcessAndroidImageReaderMap();
}

/* static */
void GpuProcessAndroidImageReaderMap::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());

  // Wait for any readback to release the context before destroying it here.
  StaticMutexAutoLock lock(sAImageReadbackMutex);
  sAImageReadbackShuttingDown = true;
  DestroyAImageSnapshotContext();
  sInstance = nullptr;
}

GpuProcessAndroidImageReaderMap::GpuProcessAndroidImageReaderMap()
    : mMonitor("GpuProcessAndroidImageReaderMap.mMonitor") {}

void GpuProcessAndroidImageReaderMap::Register(
    AndroidImageReader* aImageReader) {
  MOZ_ASSERT(aImageReader);

  if (!aImageReader) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }

  MonitorAutoLock lock(mMonitor);

  auto it = mImageReaders.find(aImageReader->mImageReaderId);
  if (it != mImageReaders.end()) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }

  mImageReaders.emplace(aImageReader->mImageReaderId,
                        std::make_unique<ImageReaderHolder>(aImageReader));
}

void GpuProcessAndroidImageReaderMap::Unregister(
    GpuProcessAndroidImageReaderId aImageReaderId) {
  MonitorAutoLock lock(mMonitor);

  const auto it = mImageReaders.find(aImageReaderId);
  MOZ_ASSERT(it != mImageReaders.end());
  if (it == mImageReaders.end()) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return;
  }
  mImageReaders.erase(it);
}

RefPtr<AndroidImageReader> GpuProcessAndroidImageReaderMap::GetImageReader(
    GpuProcessAndroidImageReaderId aImageReaderId) {
  MonitorAutoLock lock(mMonitor);

  const auto it = mImageReaders.find(aImageReaderId);
  if (it == mImageReaders.end()) {
    return nullptr;
  }
  return RefPtr<AndroidImageReader>(it->second->mImageReader);
}

bool GpuProcessAndroidImageReaderMap::MaybeReleaseFrameToCodec(
    const GpuProcessAndroidImageReaderId aImageReaderId,
    const AndroidMediaCodecFrameId aFrameId, const bool aRender) {
  RefPtr<AndroidImageReader> reader = GetImageReader(aImageReaderId);
  if (!reader) {
    return false;
  }
  return reader->MaybeReleaseFrameToCodec(aFrameId, aRender);
}

RefPtr<AndroidImageConsumer> GpuProcessAndroidImageReaderMap::GetImageConsumer(
    const GpuProcessAndroidImageReaderId aImageReaderId, gl::GLContext* aGL) {
  MOZ_ASSERT(wr::RenderThread::IsInRenderThread());
  MOZ_ASSERT(aGL);

  MonitorAutoLock lock(mMonitor);

  const auto it = mImageReaders.find(aImageReaderId);
  if (it == mImageReaders.end()) {
    return nullptr;
  }

  auto* holder = it->second.get();

  if (holder->mImageConsumer) {
    if (holder->mImageConsumer->mGL == aGL) {
      return holder->mImageConsumer;
    }
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return nullptr;
  }

  RefPtr<AndroidImageReader> imageReader(holder->mImageReader);
  if (!imageReader) {
    return nullptr;
  }

  RefPtr<AndroidImageConsumer> imageConsumer;
  imageConsumer = AndroidImageConsumer::Create(imageReader, aGL);
  if (!imageConsumer) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return nullptr;
  }

  holder->mImageConsumer = imageConsumer;

  return imageConsumer;
}

void GpuProcessAndroidImageReaderMap::UnregisterImageConsumer(
    GpuProcessAndroidImageReaderId aImageReaderId) {
  MOZ_ASSERT(wr::RenderThread::IsInRenderThread());

  MonitorAutoLock lock(mMonitor);

  const auto it = mImageReaders.find(aImageReaderId);
  if (it == mImageReaders.end()) {
    return;
  }

  auto* holder = it->second.get();

  MOZ_ASSERT(holder->mImageConsumer);
  holder->mImageConsumer = nullptr;
}

GpuProcessAndroidImageReaderMap::ImageReaderHolder::ImageReaderHolder(
    AndroidImageReader* aImageReader)
    : mImageReader(aImageReader) {}

GpuProcessAndroidImageReaderMap::ImageReaderHolder::~ImageReaderHolder() {}

}  // namespace layers
}  // namespace mozilla
