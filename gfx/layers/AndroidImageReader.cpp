/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AndroidImageReader.h"

#include <android/hardware_buffer.h>
#include <android/native_window_jni.h>
#include <media/NdkImage.h>
#include <media/NdkImageReader.h>

#include <mutex>

#include "GLContext.h"
#include "GLContextEGL.h"
#include "GLContextProvider.h"
#include "GLLibraryEGL.h"
#include "GLReadTexImageHelper.h"
#include "OGLShaderConfig.h"
#include "ScopedGLHelpers.h"
#include "mozilla/DataMutex.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/RemoteMediaManagerParent.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/TimeStamp.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/java/RemoteSurfaceAllocatorNatives.h"
#include "mozilla/java/RemoteSurfaceAllocatorWrappers.h"
#include "mozilla/java/SurfaceAllocatorNatives.h"
#include "mozilla/java/SurfaceAllocatorWrappers.h"
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

namespace {
// Codec-side callbacks are grouped by reader so flush only visits that reader.
using FrameReleaseCallback = SurfaceTextureImage::SetCurrentCallback;
using FrameCallbackMap =
    std::unordered_map<uint64_t, UniquePtr<FrameReleaseCallback>>;
using ReaderFrameMap = std::unordered_map<uint64_t, FrameCallbackMap>;
MOZ_RUNINIT static StaticDataMutex<ReaderFrameMap> sReaderFrames(
    "ImageReaderClientFrames");

/**
 * Owns codec-side frame callbacks for each ImageReader client. Release consumes
 * a callback exactly once. Flush drops callbacks after the decoder has released
 * its codec buffers. Invoke or destroy callbacks outside the registry lock:
 * they can release decoder resources and reenter the client.
 */
class ImageReaderFrameRegistry final {
 public:
  static void RegisterReader(uint64_t aReaderId) {
    auto readers = sReaderFrames.Lock();
    [[maybe_unused]] const bool inserted =
        readers->try_emplace(aReaderId).second;
    MOZ_ASSERT(inserted);
  }

  // Allocator loss is terminal. Removing the entry prevents new callbacks
  // from being registered while the client is still alive.
  static void UnregisterReader(uint64_t aReaderId) {
    FrameCallbackMap discarded;
    {
      auto readers = sReaderFrames.Lock();
      auto it = readers->find(aReaderId);
      if (it == readers->end()) {
        return;
      }
      discarded.swap(it->second);
      readers->erase(it);
    }
  }

  static void RegisterFrame(uint64_t aReaderId, uint64_t aFrameId,
                            UniquePtr<FrameReleaseCallback> aCallback) {
    auto readers = sReaderFrames.Lock();
    auto it = readers->find(aReaderId);
    if (it == readers->end()) {
      return;
    }
    // try_emplace leaves aCallback untouched on a duplicate. Any rejected
    // callback is destroyed after the lock guard above leaves scope.
    [[maybe_unused]] const bool inserted =
        it->second.try_emplace(aFrameId, std::move(aCallback)).second;
    MOZ_ASSERT(inserted);
  }

  static bool ReleaseFrame(uint64_t aReaderId, uint64_t aFrameId,
                           bool aRender) {
    UniquePtr<FrameReleaseCallback> callback;
    {
      auto readers = sReaderFrames.Lock();
      auto reader = readers->find(aReaderId);
      if (reader == readers->end()) {
        return false;
      }
      auto frame = reader->second.find(aFrameId);
      if (frame == reader->second.end()) {
        return false;
      }
      callback = std::move(frame->second);
      reader->second.erase(frame);
    }
    return (*callback)(aRender);
  }

  static void DiscardFrames(uint64_t aReaderId) {
    FrameCallbackMap discarded;
    {
      auto readers = sReaderFrames.Lock();
      auto it = readers->find(aReaderId);
      if (it != readers->end()) {
        discarded.swap(it->second);
      }
    }
  }
};

// Only remote RDD decoding uses JNI; local GPU readers call the registry
// directly.
class NativeImageReaderFrames final
    : public java::SurfaceAllocator::Natives<NativeImageReaderFrames> {
 public:
  static bool ReleaseImageReaderFrame(int64_t aReaderId, int64_t aFrameId,
                                      bool aRender) {
    return ImageReaderFrameRegistry::ReleaseFrame(uint64_t(aReaderId),
                                                  uint64_t(aFrameId), aRender);
  }

  static void DiscardImageReaderFrames(int64_t aReaderId) {
    ImageReaderFrameRegistry::UnregisterReader(uint64_t(aReaderId));
  }
};

using RemoteReaders = std::unordered_map<int64_t, RefPtr<AndroidImageReader>>;
struct RemoteReaderRegistry {
  // Start closed; Init opens allocation after the compositor map is ready.
  bool mShuttingDown = true;
  RemoteReaders mReaders;
};
MOZ_RUNINIT static StaticDataMutex<RemoteReaderRegistry> sRemoteReaders(
    "RemoteImageReaders");

/**
 * Compositing-process (GPU or parent) JNI implementation of the ImageReader
 * operations exposed by RemoteSurfaceAllocator. Creates and retains the actual
 * AndroidImageReaders, returning their output Surfaces to the RDD process.
 * Allocation handles identify readers for first-frame rendering and disconnect
 * when a remote Surface is released. Rendering holds a strong reader reference
 * and runs without the registry lock so a failed codec release can be reported.
 */
class NativeImageReaderAllocator final
    : public java::RemoteSurfaceAllocator::Natives<NativeImageReaderAllocator> {
 public:
  static jni::Object::LocalRef CreateImageReaderSurface(int64_t aHandle) {
    // Keep the allocation gate locked through creation and registration. This
    // also prevents Shutdown from destroying the compositor map during Create.
    auto readers = sRemoteReaders.Lock();
    if (readers->mShuttingDown) {
      return nullptr;
    }
    if ((!XRE_IsGPUProcess() && !XRE_IsParentProcess()) ||
        !wr::RenderThread::Get()) {
      return nullptr;
    }
    // Tag the allocation handle as remote. GetNext() enforces that local IDs
    // never use this bit. Keep the Java encoding in sync with kRemoteIdBit.
    // See RemoteSurfaceAllocator.REMOTE_IMAGE_READER_ID_BIT.
    RefPtr<AndroidImageReader> reader =
        AndroidImageReader::Create(GpuProcessAndroidImageReaderId{
            uint64_t(aHandle) | GpuProcessAndroidImageReaderId::kRemoteIdBit});
    if (!reader) {
      return nullptr;
    }
    JNIEnv* env = jni::GetEnvForThread();
    auto surface = jni::Object::LocalRef::Adopt(
        env, ANativeWindow_toSurface(env, reader->GetANativeWindow()));
    if (!surface) {
      return nullptr;
    }
    reader->InitRemote(aHandle);
    readers->mReaders.emplace(aHandle, reader);
    return surface;
  }

  static void MaybeRenderImageReaderFirstFrameNative(int64_t aHandle,
                                                     int64_t aFrameId) {
    if (RefPtr<AndroidImageReader> reader = GetReader(aHandle)) {
      reader->MaybeRenderFirstFrame(
          AndroidMediaCodecFrameId{uint64_t(aFrameId)});
    }
  }

  static void NotifyImageReaderFrameReleaseFailedNative(int64_t aHandle,
                                                        int64_t aFrameId) {
    if (RefPtr<AndroidImageReader> reader = GetReader(aHandle)) {
      reader->NotifyRemoteFrameReleaseFailed(
          AndroidMediaCodecFrameId{uint64_t(aFrameId)});
    }
  }

  static void DisposeImageReaderSurface(int64_t aHandle) {
    RefPtr<AndroidImageReader> reader;
    {
      auto readers = sRemoteReaders.Lock();
      auto it = readers->mReaders.find(aHandle);
      if (it == readers->mReaders.end()) {
        return;
      }
      reader = std::move(it->second);
      readers->mReaders.erase(it);
    }
    reader->DisconnectRemote();
  }

 private:
  static RefPtr<AndroidImageReader> GetReader(int64_t aHandle) {
    auto readers = sRemoteReaders.Lock();
    auto it = readers->mReaders.find(aHandle);
    return it != readers->mReaders.end() ? it->second : nullptr;
  }
};

class LocalAndroidImageReaderClient final : public AndroidImageReaderClient {
 public:
  static already_AddRefed<AndroidImageReaderClient> Create() {
    RefPtr<AndroidImageReader> reader = AndroidImageReader::Create();
    if (!reader) {
      return nullptr;
    }
    JNIEnv* env = jni::GetEnvForThread();
    auto surface = java::sdk::Surface::LocalRef::Adopt(
        env, ANativeWindow_toSurface(env, reader->GetANativeWindow()));
    if (!surface) {
      return nullptr;
    }
    RefPtr<AndroidImageReaderClient> client =
        new LocalAndroidImageReaderClient(reader, surface);
    return client.forget();
  }

  void Shutdown() override { mReader->Shutdown(); }

 private:
  LocalAndroidImageReaderClient(AndroidImageReader* aReader,
                                java::sdk::Surface::LocalRef aSurface)
      : AndroidImageReaderClient(aReader->mImageReaderId, aSurface),
        mReader(aReader) {}

  ~LocalAndroidImageReaderClient() override { UnregisterReader(); }

  void RenderFirstFrame(AndroidMediaCodecFrameId aFrameId) override {
    mReader->MaybeRenderFirstFrame(aFrameId);
  }

  const RefPtr<AndroidImageReader> mReader;
};

class RemoteAndroidImageReaderClient final : public AndroidImageReaderClient {
 public:
  static already_AddRefed<AndroidImageReaderClient> Create() {
    // Register JNI callbacks once before requesting a compositor-owned Surface.
    static std::once_flag sNativeMethodsInitialized;
    std::call_once(sNativeMethodsInitialized,
                   [] { NativeImageReaderFrames::Init(); });
    java::GeckoSurface::GlobalRef surface = java::GeckoSurface::LocalRef(
        java::SurfaceAllocator::AcquireImageReaderSurface());
    if (!surface) {
      return nullptr;
    }
    RefPtr<AndroidImageReaderClient> client =
        new RemoteAndroidImageReaderClient(surface);
    return client.forget();
  }

  bool IsValid() const override {
    return AndroidImageReaderClient::IsValid() && !mAllocation->IsReleased();
  }

  void Shutdown() override {
    // Unregister first so no new callbacks can be registered while disposing
    // the Surface allocation disconnects the compositor reader.
    UnregisterReader();
    java::SurfaceAllocator::DisposeSurface(mAllocation);
  }

 private:
  explicit RemoteAndroidImageReaderClient(
      java::GeckoSurface::GlobalRef aSurface)
      : AndroidImageReaderClient(GpuProcessAndroidImageReaderId{uint64_t(
                                     aSurface->GetImageReaderId())},
                                 aSurface->GetSurface()),
        mAllocation(aSurface) {
    // The base constructor registers the reader. If the allocator loss
    // callback already ran, the released Java Surface lets us detect that
    // and undo the registration.
    if (!IsValid()) {
      UnregisterReader();
    }
  }

  ~RemoteAndroidImageReaderClient() override { Shutdown(); }

  void RenderFirstFrame(AndroidMediaCodecFrameId aFrameId) override {
    AUTO_PROFILER_MARKER_TEXT("RDD ImageReader first frame IPC", MEDIA_PLAYBACK,
                              {}, "Wait for compositor ImageReader"_ns);
    java::SurfaceAllocator::MaybeRenderImageReaderFirstFrame(
        mAllocation->GetHandle(), int64_t(aFrameId.mId));
  }

  const java::GeckoSurface::GlobalRef mAllocation;
};

}  // namespace

AndroidImageReaderClient::AndroidImageReaderClient(
    GpuProcessAndroidImageReaderId aId, java::sdk::Surface::LocalRef aSurface)
    : mImageReaderId(aId), mOutputSurface(aSurface) {
  ImageReaderFrameRegistry::RegisterReader(mImageReaderId.mId);
}

void AndroidImageReaderClient::UnregisterReader() {
  ImageReaderFrameRegistry::UnregisterReader(mImageReaderId.mId);
}

bool AndroidImageReaderClient::IsValid() const { return bool(mOutputSurface); }

already_AddRefed<AndroidImageReaderClient> AndroidImageReaderClient::Create() {
  if (XRE_IsGPUProcess()) {
    return LocalAndroidImageReaderClient::Create();
  }
  if (XRE_IsRDDProcess()) {
    return RemoteAndroidImageReaderClient::Create();
  }
  return nullptr;
}

java::sdk::Surface::LocalRef AndroidImageReaderClient::GetSurface() const {
  return java::sdk::Surface::LocalRef(mOutputSurface);
}

void AndroidImageReaderClient::MaybeRenderFirstFrame(
    AndroidMediaCodecFrameId aFrameId) {
  if (!IsValid()) {
    return;
  }
  RenderFirstFrame(aFrameId);
}

// Drop this reader's callbacks without invoking them. On flush, the decoder
// releases codec buffers through its frame map before discarding these
// callbacks.
void AndroidImageReaderClient::DiscardFrames() {
  ImageReaderFrameRegistry::DiscardFrames(mImageReaderId.mId);
}

void AndroidImageReaderClient::RegisterFrame(
    AndroidMediaCodecFrameId aFrameId,
    UniquePtr<SurfaceTextureImage::SetCurrentCallback> aCallback) {
  if (!IsValid()) {
    return;
  }
  ImageReaderFrameRegistry::RegisterFrame(mImageReaderId.mId, aFrameId.mId,
                                          std::move(aCallback));
}

void AndroidImageReaderClient::ForgetFrame(AndroidMediaCodecFrameId aFrameId) {
  ImageReaderFrameRegistry::ReleaseFrame(mImageReaderId.mId, aFrameId.mId,
                                         false);
}

void AndroidImageReader::InitRemote(int64_t aHandle) {
  MonitorAutoLock lock(mMonitor);
  MOZ_ASSERT(mRemoteState.isNothing());
  mRemoteState.emplace(aHandle);
}

void AndroidImageReader::ClearPendingFrame() {
  mPendingFrameId.reset();
  mWaitingFrameAvailable = false;
}

void AndroidImageReader::NotifyRemoteFrameReleaseFailed(
    AndroidMediaCodecFrameId aFrameId) {
  MonitorAutoLock lock(mMonitor);
  // A failure notification can arrive after the pending request has changed.
  // Clear the request and wake waiters only if the frame ID still matches.
  if (!IsPendingFrame(aFrameId)) {
    return;
  }
  ClearPendingFrame();
  mMonitor.NotifyAll();
}

void AndroidImageReader::DisconnectRemote() {
  // Drop the cached image reference after releasing mMonitor.
  RefPtr<AndroidImageWrapper> image;
  {
    MonitorAutoLock lock(mMonitor);
    image = DisconnectRemoteLocked();
  }
}

RefPtr<AndroidImageWrapper> AndroidImageReader::DisconnectRemoteLocked() {
  MOZ_ASSERT(mRemoteState.isSome());
  mRemoteState->mDisconnected = true;
  ClearPendingFrame();
  mMonitor.NotifyAll();
  return std::move(mCurrentImage);
}

void AndroidImageReader::ExpireRemoteFrame() {
  // Disconnect the reader so a late image cannot be mistaken for a later
  // frame. Images carry no frame ID.
  gfxCriticalNoteOnce << "Remote ImageReader frame timed out";
  RefPtr<AndroidImageWrapper> image = DisconnectRemoteLocked();
  MonitorAutoUnlock unlock(mMonitor);
  image = nullptr;
}

void AndroidImageReader::StartRemoteFrameRequest() {
  MOZ_ASSERT(mRemoteState.isSome());
  // Unlike the notification wait, this deadline spans multiple updates.
  const TimeDuration kRemoteFrameTimeout = TimeDuration::FromSeconds(5);
  mRemoteState->mFrameDeadline = TimeStamp::Now() + kRemoteFrameTimeout;
  mRemoteState->mFrameTimeoutScheduled = false;
}

bool AndroidImageReader::HandleRemoteFrameNoBuffer() {
  MOZ_ASSERT(mRemoteState.isSome());
  if (TimeStamp::Now() >= mRemoteState->mFrameDeadline) {
    ExpireRemoteFrame();
    return true;
  }
  // Keep recovery armed even if the compositor stops requesting frames.
  ScheduleRemoteFrameTimeout();
  return false;
}

void AndroidImageReader::ScheduleRemoteFrameTimeout() {
  MOZ_ASSERT(mRemoteState.isSome());
  if (mRemoteState->mFrameTimeoutScheduled) {
    return;
  }
  auto* target = GetMainThreadSerialEventTarget();
  if (!target) {
    return;
  }

  const double remainingMs =
      (mRemoteState->mFrameDeadline - TimeStamp::Now()).ToMilliseconds();
  // DelayedDispatch requires a positive delay. Round up to the next ms.
  const auto delayMs = remainingMs > 0 ? uint32_t(remainingMs) + 1 : 1u;
  mRemoteState->mFrameTimeoutScheduled = NS_SUCCEEDED(target->DelayedDispatch(
      NS_NewRunnableFunction(
          "AndroidImageReader::OnRemoteFrameTimeout",
          [weakReader = ThreadSafeWeakPtr<AndroidImageReader>(this),
           frameId = mPendingFrameId.value(),
           deadline = mRemoteState->mFrameDeadline] {
            if (auto reader =
                    static_cast<RefPtr<AndroidImageReader>>(weakReader)) {
              reader->OnRemoteFrameTimeout(frameId, deadline);
            }
          }),
      delayMs));
}

void AndroidImageReader::OnRemoteFrameTimeout(AndroidMediaCodecFrameId aFrameId,
                                              TimeStamp aDeadline) {
  MonitorAutoLock lock(mMonitor);
  // Delayed tasks can outlive the request that scheduled them. Check both
  // the frame ID and the deadline to match the original request.
  if (mRemoteState.isNothing() || IsStopped() || !IsPendingFrame(aFrameId) ||
      mRemoteState->mFrameDeadline != aDeadline) {
    return;
  }
  // An update in progress will attempt acquisition and check the deadline.
  if (mIsPendingNextImage) {
    return;
  }
  // Try acquiring once more before expiring the reader. An acquired image
  // consumes the pending frame even if import fails, so expire only when
  // the queue has no buffer available.
  if (AcquirePendingImage() == AcquireImageResult::NoBufferAvailable) {
    ExpireRemoteFrame();
  }
}

AndroidImageReaderImage::AndroidImageReaderImage(
    AndroidImageReaderClient* aClient, const gfx::IntSize& aSize,
    bool aHasAlpha)
    : Image(nullptr, ImageFormat::ANDROID_IMAGE_READER),
      mImageReaderId(aClient->mImageReaderId),
      mFrameId(AndroidMediaCodecFrameId::GetNext()),
      mSize(aSize),
      mHasAlpha(aHasAlpha),
      mClient(aClient) {}

AndroidImageReaderImage::~AndroidImageReaderImage() {
  mClient->ForgetFrame(mFrameId);
}

Maybe<SurfaceDescriptor> AndroidImageReaderImage::GetDesc() {
  return Nothing();
}

void AndroidImageReaderImage::OnSetCurrent() {
  mClient->MaybeRenderFirstFrame(mFrameId);
}

nsresult AndroidImageReaderImage::BuildSurfaceDescriptorBuffer(
    SurfaceDescriptorBuffer& aSdBuffer, BuildSdbFlags aFlags,
    const std::function<MemoryOrShmem(uint32_t)>& aAllocate) {
  // RDD only owns a proxy and must never read pixels, including through Binder.
  if (XRE_IsRDDProcess()) {
    return NS_ERROR_NOT_AVAILABLE;
  }
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

  auto imageReaderMap = layers::GpuProcessAndroidImageReaderMap::Get();
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
  return ipc::FileDescriptor(mWriteFenceFd.get()).TakePlatformHandle();
}

bool AndroidImageWrapper::WaitForWriteFence(gl::GLContext* aGL) {
  UniqueFileHandle writeFenceFd;
  {
    MutexAutoLock lock(mMutex);
    if (!mWriteFenceFd) {
      return true;
    }
    writeFenceFd =
        ipc::FileDescriptor(mWriteFenceFd.get()).TakePlatformHandle();
  }

  const auto& egl = gl::GLContextEGL::Cast(aGL)->mEgl;
  if (!writeFenceFd ||
      !egl->IsExtensionSupported(gl::EGLExtension::ANDROID_native_fence_sync) ||
      !egl->IsExtensionSupported(gl::EGLExtension::KHR_fence_sync)) {
    gfxCriticalNoteOnce << "Cannot import AndroidImageReader acquire fence";
    return false;
  }

  // EGL takes ownership of the duplicated fence FD.
  const EGLint attrs[] = {LOCAL_EGL_SYNC_NATIVE_FENCE_FD_ANDROID,
                          writeFenceFd.release(), LOCAL_EGL_NONE};
  EGLSync sync =
      egl->fCreateSyncKHR(LOCAL_EGL_SYNC_NATIVE_FENCE_ANDROID, attrs);
  if (!sync) {
    gfxCriticalNoteOnce << "Failed to create AndroidImageReader acquire sync";
    return false;
  }

  const bool waited = egl->IsExtensionSupported(gl::EGLExtension::KHR_wait_sync)
                          ? egl->fWaitSync(sync, 0) == LOCAL_EGL_TRUE
                          : egl->fClientWaitSync(sync, 0, LOCAL_EGL_FOREVER) ==
                                LOCAL_EGL_CONDITION_SATISFIED;
  egl->fDestroySync(sync);
  if (!waited) {
    gfxCriticalNoteOnce
        << "Failed to wait for AndroidImageReader acquire fence";
  }
  return waited;
}

void AndroidImageWrapper::SetReadFenceFd(UniqueFileHandle&& aFenceFd) {
  if (!aFenceFd) {
    return;
  }

  UniqueFileHandle fence = std::move(aFenceFd);
  MutexAutoLock lock(mMutex);
  if (!mReadFenceFd) {
    mReadFenceFd = std::move(fence);
    return;
  }

  auto merged = AndroidHardwareBuffer::TryMergeFences(mReadFenceFd, fence);
  if (!merged) {
    // Keep the registered fence and close the incoming FD without waiting.
    gfxCriticalNoteOnce << "Failed to merge AndroidImageReader read fences";
    return;
  }
  mReadFenceFd = std::move(merged);
}

/* static */
already_AddRefed<AndroidImageReader> AndroidImageReader::Create(
    GpuProcessAndroidImageReaderId aId) {
  if ((!XRE_IsGPUProcess() && !XRE_IsParentProcess()) ||
      !wr::RenderThread::Get()) {
    MOZ_ASSERT_UNREACHABLE("unexpected to be called");
    return nullptr;
  }

  auto imageReaderMap = layers::GpuProcessAndroidImageReaderMap::Get();
  if (!imageReaderMap) {
    return nullptr;
  }

  RefPtr<AndroidImageReader> imageReader = new AndroidImageReader(aId);
  bool ret = imageReader->Init();
  if (!ret) {
    return nullptr;
  }

  imageReaderMap->Register(imageReader);
  imageReader->mRegistered = true;

  return imageReader.forget();
}

AndroidImageReader::AndroidImageReader(GpuProcessAndroidImageReaderId aId)
    : mImageReaderId(aId.IsValid() ? aId
                                   : GpuProcessAndroidImageReaderId::GetNext()),
      mMonitor("mozilla.layers.AndroidImageReader.mMonitor") {}

AndroidImageReader::~AndroidImageReader() {
  auto imageReaderMap = layers::GpuProcessAndroidImageReaderMap::Get();
  if (imageReaderMap && mRegistered) {
    imageReaderMap->Unregister(mImageReaderId);
  }
  ReleaseResources();
}

void AndroidImageReader::Shutdown() {
  RefPtr<AndroidImageWrapper> image;
  {
    MonitorAutoLock lock(mMonitor);
    mShuttingDown = true;
    // AndroidImageWrapper holds a strong reference to this reader. Break the
    // cycle while the decoder still owns the reader; consumers may retain
    // images.
    image.swap(mCurrentImage);
    mMonitor.NotifyAll();
  }
  // Release the cached image outside the monitor, since its destructor may
  // call AImage_delete() or AImage_deleteAsync().
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
  if (IsRemoteDisconnected()) {
    return;
  }
  // The notification wakes the waiter. Acquisition uses the pending frame
  // ID to associate the buffer with the codec frame.
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

  // The caller must wait on the acquire fence in the GL context that
  // will sample this image.

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

  if (!mCurrentImage->WaitForWriteFence(aGL)) {
    return false;
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

  if (IsStopped() || mCurrentFrameId == aFrameId) {
    return false;
  }

  // Only one thread can update the reader at a time.
  const TimeDuration concurrentUpdateTimeout =
      TimeDuration::FromMilliseconds(20);
  while (mIsPendingNextImage && !IsStopped()) {
    CVStatus status = mMonitor.Wait(concurrentUpdateTimeout);
    if (status == CVStatus::Timeout) {
      gfxCriticalNoteOnce << "Pending next image wait timeout";
      return false;
    }
  }
  if (IsStopped()) {
    return false;
  }
  MOZ_ASSERT(!mIsPendingNextImage);

  auto scopeExit = MakeScopeExit([&]() MOZ_REQUIRES(mMonitor) {
    mIsPendingNextImage = false;
    mMonitor.NotifyAll();
  });

  mIsPendingNextImage = true;

  // Bound the total notification wait in this call, including an older frame
  // left outstanding by a previous timeout.
  const TimeStamp notificationDeadline =
      TimeStamp::Now() + TimeDuration::FromMilliseconds(20);
  // Acquiring an older outstanding frame counts as an update, even if the
  // requested frame is still pending.
  bool acquiredAnyImage = false;

  while (mCurrentFrameId != aFrameId) {
    if (mPendingFrameId.isNothing()) {
      MOZ_ASSERT(!mWaitingFrameAvailable);
      mPendingFrameId = Some(aFrameId);
      mWaitingFrameAvailable = true;
      if (mRemoteState.isSome()) {
        StartRemoteFrameRequest();
      }
      const bool released = MaybeReleaseFrameToCodec(aProofOfLock, aFrameId,
                                                     /* aRender */ true);
      // Releasing the codec buffer drops the monitor. Recheck whether the
      // reader stopped and whether the same frame is still pending before
      // handling the release result.
      if (IsStopped()) {
        return false;
      }
      if (!IsPendingFrame(aFrameId)) {
        return acquiredAnyImage;
      }
      if (!released) {
        ClearPendingFrame();
        return acquiredAnyImage;
      }
    }

    WaitForFrameNotification(notificationDeadline);

    // The wait releases the monitor. Recheck whether the reader stopped or
    // the pending request was cleared before acquiring an image.
    if (IsStopped()) {
      return false;
    }

    if (mPendingFrameId.isNothing()) {
      // RDD reported that the codec did not release the pending frame.
      return acquiredAnyImage;
    }

    // OnFrameAvailable does not carry a frame ID. Consume the outstanding
    // frame before releasing another, even when aFrameId has since changed.
    const auto result = AcquirePendingImage();
    if (result == AcquireImageResult::Success) {
      acquiredAnyImage = true;
      continue;
    }

    if (mRemoteState.isSome() &&
        result == AcquireImageResult::NoBufferAvailable &&
        HandleRemoteFrameNoBuffer()) {
      return false;
    }
    return acquiredAnyImage;
  }

  return acquiredAnyImage;
}

void AndroidImageReader::WaitForFrameNotification(TimeStamp aDeadline) {
  while (mWaitingFrameAvailable && !IsStopped()) {
    const TimeDuration remaining = aDeadline - TimeStamp::Now();
    if (remaining <= TimeDuration()) {
      gfxCriticalNoteOnce << "UpdateTexImage wait timeout";
      // The caller still tries to acquire the image in case it was queued
      // before the notification arrived.
      return;
    }
    mMonitor.Wait(remaining);
  }
}

AndroidImageReader::AcquireImageResult
AndroidImageReader::AcquirePendingImage() {
  if (IsStopped()) {
    return AcquireImageResult::Failure;
  }
  MOZ_ASSERT(mPendingFrameId.isSome());
  // Acquisition is also allowed before OnFrameAvailable after a timeout.

  AImage* image = nullptr;
  UniqueFileHandle fence;
  const media_status_t ret = AImageReader_acquireNextImageAsync(
      mAImageReader, &image, getter_Transfers(fence));
  // XXX Add AImageReader_acquireLatestImageAsync() usage

  switch (ret) {
    case AMEDIA_ERROR_INVALID_PARAMETER:
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      return AcquireImageResult::Failure;
    case AMEDIA_IMGREADER_MAX_IMAGES_ACQUIRED:
      // Consumers can still hold the acquired images. Keep the pending frame
      // so the next update can acquire it after an image is released.
      return AcquireImageResult::Failure;
    case AMEDIA_IMGREADER_NO_BUFFER_AVAILABLE:
      return AcquireImageResult::NoBufferAvailable;
    case AMEDIA_ERROR_UNKNOWN:
      return AcquireImageResult::Failure;
    case AMEDIA_OK:
      break;
    default:
      MOZ_ASSERT_UNREACHABLE("unexpected to be called");
      return AcquireImageResult::Failure;
  }

  if (!image) {
    return AcquireImageResult::Failure;
  }

  // This buffer has been consumed even if importing it subsequently fails.
  const AndroidMediaCodecFrameId frameId = mPendingFrameId.value();
  ClearPendingFrame();

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
    return AcquireImageResult::Failure;
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

  return AcquireImageResult::Success;
}

ANativeWindow* AndroidImageReader::GetANativeWindow() {
  MonitorAutoLock lock(mMonitor);
  return mNativeWindow;
}

RefPtr<AndroidImageWrapper> AndroidImageReader::GetCurrentImage() {
  MonitorAutoLock lock(mMonitor);
  return mCurrentImage;
}

bool AndroidImageReader::MaybeReleaseFrameToCodec(
    const AndroidMediaCodecFrameId aFrameId, const bool aRender) {
  MonitorAutoLock lock(mMonitor);
  return MaybeReleaseFrameToCodec(lock, aFrameId, aRender);
}
bool AndroidImageReader::MaybeReleaseFrameToCodec(
    const MonitorAutoLock& aProofOfLock,
    const AndroidMediaCodecFrameId aFrameId, const bool aRender) {
  aProofOfLock.AssertOwns(mMonitor);
  if (IsStopped()) {
    return false;
  }
  if (mRemoteState.isSome()) {
    // Copy the handle under mMonitor, then unlock before calling Binder so
    // frame notifications and release-failure callbacks can update the reader.
    const int64_t handle = mRemoteState->mHandle;
    MonitorAutoUnlock unlock(mMonitor);
    // Binder reports whether it accepted the request. RDD reports render
    // failures asynchronously.
    return java::RemoteSurfaceAllocator::ReleaseImageReaderFrame(
        handle, int64_t(mImageReaderId.mId), int64_t(aFrameId.mId), aRender);
  }

  // Local callbacks can reenter the reader and return the codec release result.
  MonitorAutoUnlock unlock(mMonitor);
  return ImageReaderFrameRegistry::ReleaseFrame(mImageReaderId.mId,
                                                aFrameId.mId, aRender);
}

static StaticMutex sImageReaderMapMutex;
static StaticRefPtr<GpuProcessAndroidImageReaderMap> sImageReaderMap
    MOZ_GUARDED_BY(sImageReaderMapMutex);

/* static */
RefPtr<GpuProcessAndroidImageReaderMap> GpuProcessAndroidImageReaderMap::Get() {
  StaticMutexAutoLock lock(sImageReaderMapMutex);
  return sImageReaderMap.get();
}

/* static */
void GpuProcessAndroidImageReaderMap::Init() {
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(XRE_IsGPUProcess() || XRE_IsParentProcess());
  NativeImageReaderAllocator::Init();

  {
    StaticMutexAutoLock lock(sAImageReadbackMutex);
    MOZ_ASSERT(!sAImageSnapshotContext);
    sAImageReadbackShuttingDown = false;
  }
  {
    StaticMutexAutoLock lock(sImageReaderMapMutex);
    MOZ_ASSERT(!sImageReaderMap);
    sImageReaderMap = new GpuProcessAndroidImageReaderMap();
  }
  {
    auto readers = sRemoteReaders.Lock();
    readers->mShuttingDown = false;
  }
}

/* static */
void GpuProcessAndroidImageReaderMap::Shutdown() {
  MOZ_ASSERT(NS_IsMainThread());

  RemoteReaders readers;
  {
    auto remoteReaders = sRemoteReaders.Lock();
    remoteReaders->mShuttingDown = true;
    readers.swap(remoteReaders->mReaders);
  }
  for (auto& [handle, reader] : readers) {
    reader->DisconnectRemote();
  }
  readers.clear();

  // Detach the singleton under the same lock used to acquire references.
  // In-flight callers can finish, and the last reference is released unlocked.
  RefPtr<GpuProcessAndroidImageReaderMap> map;
  {
    StaticMutexAutoLock lock(sImageReaderMapMutex);
    map = sImageReaderMap.forget();
  }

  // Wait for any readback to release the context before destroying it here.
  StaticMutexAutoLock lock(sAImageReadbackMutex);
  sAImageReadbackShuttingDown = true;
  DestroyAImageSnapshotContext();
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
