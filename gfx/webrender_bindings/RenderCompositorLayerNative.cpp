/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RenderCompositorLayerNative.h"

#include "GLContext.h"
#include "GLContextEGL.h"
#include "GLContextProvider.h"
#include "RenderCompositorRecordedFrame.h"
#include "mozilla/ProfilerLabels.h"
#include "mozilla/ProfilerMarkers.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/CompositionRecorder.h"
#include "mozilla/layers/NativeLayer.h"
#include "mozilla/layers/ProfilerScreenshots.h"
#include "mozilla/layers/SurfacePool.h"
#include "mozilla/webrender/RenderTextureHost.h"
#include "mozilla/webrender/RenderThread.h"
#include "mozilla/widget/CompositorWidget.h"

#if defined(MOZ_WAYLAND)
#  include "mozilla/layers/NativeLayerWayland.h"
#endif

namespace mozilla::wr {

extern LazyLogModule gRenderThreadLog;
#define LOG(...) MOZ_LOG(gRenderThreadLog, LogLevel::Debug, (__VA_ARGS__))

RenderCompositorLayerNative::RenderCompositorLayerNative(
    const RefPtr<widget::CompositorWidget>& aWidget, gl::GLContext* aGL)
    : RenderCompositor(aWidget),
      mNativeLayerRoot(GetWidget()->GetNativeLayerRoot()) {
  LOG("RenderCompositorLayerNative::RenderCompositorLayerNative()");

  MOZ_ASSERT(mNativeLayerRoot);
#if defined(MOZ_WAYLAND)
  if (auto* rootWayland = mNativeLayerRoot->AsNativeLayerRootWayland()) {
    rootWayland->SetGLContext(aGL);
  }
#endif

#if defined(XP_DARWIN) || defined(MOZ_WAYLAND)
  auto pool = RenderThread::Get()->SharedSurfacePool();
  if (pool) {
    mSurfacePoolHandle = pool->GetHandleForGL(aGL);
  }
#endif
  MOZ_RELEASE_ASSERT(mSurfacePoolHandle);
}

RenderCompositorLayerNative::~RenderCompositorLayerNative() {
  LOG("RRenderCompositorLayerNative::~RenderCompositorLayerNative()");

  mProfilerScreenshotGrabber.Destroy();
  mNativeLayerRoot->SetLayers({});
  mNativeLayerRootSnapshotter = nullptr;
  mNativeLayerRoot = nullptr;
}

bool RenderCompositorLayerNative::BeginFrame() {
  if (!MakeCurrent()) {
    gfxCriticalNote << "Failed to make render context current, can't draw.";
    return false;
  }

  if (!InitDefaultFramebuffer()) {
    return false;
  }

  return true;
}

RenderedFrameId RenderCompositorLayerNative::EndFrame(
    const nsTArray<DeviceIntRect>& aDirtyRects) {
  RenderedFrameId frameId = GetNextRenderFrameId();

  DoSwap();

  MOZ_ASSERT(mPendingGpuFences.empty());

  return frameId;
}

void RenderCompositorLayerNative::Pause() {}

bool RenderCompositorLayerNative::Resume() { return true; }

inline layers::WebRenderCompositor RenderCompositorLayerNative::CompositorType()
    const {
#if defined(XP_DARWIN)
  return layers::WebRenderCompositor::CORE_ANIMATION;
#elif defined(MOZ_WAYLAND)
  return layers::WebRenderCompositor::WAYLAND;
#else
  MOZ_ASSERT_UNREACHABLE("Unexpected to be called!");
  return layers::WebRenderCompositor::Unknown;
#endif
}

LayoutDeviceIntSize RenderCompositorLayerNative::GetBufferSize() {
  return mWidget->GetClientSize();
}

bool RenderCompositorLayerNative::ShouldUseNativeCompositor() { return false; }

bool RenderCompositorLayerNative::ShouldUseLayerCompositor() const {
  return UseLayerCompositor();
}

bool RenderCompositorLayerNative::UseLayerCompositor() const { return true; }

bool RenderCompositorLayerNative::EnableAsyncScreenshot() {
#if defined(XP_DARWIN)
  // On macOS, NativeLayerRootSnapshotterCA supports to take snapshot with
  // multiple layers.
  return true;
#else
  // Request WebRender to use only one layer for content rendering during taking
  // snapshot. In addition to the content layer, one debug layer could exist.
  mAsyncScreenshotLastFrameUsed = mCurrentFrame;
  if (!mEnableAsyncScreenshot) {
    mEnableAsyncScreenshotInNextFrame = true;
    return false;
  }
  return true;
#endif
}

void RenderCompositorLayerNative::GetCompositorCapabilities(
    CompositorCapabilities* aCaps) {
  RenderCompositor::GetCompositorCapabilities(aCaps);
}

uint32_t RenderCompositorLayerNative::GetMaxPartialPresentRects() {
  return gfx::gfxVars::WebRenderMaxPartialPresentRects();
}

void RenderCompositorLayerNative::GetWindowProperties(
    WindowProperties* aProperties) {
  // XXX
  aProperties->is_opaque = false;
  aProperties->enable_screenshot = mEnableAsyncScreenshot;
}

RenderCompositorLayerNative::Surface::~Surface() = default;

RenderCompositorLayerNative::Surface::Surface(wr::DeviceIntSize aSize,
                                              bool aIsOpaque)
    : mSize(aSize), mIsOpaque(aIsOpaque) {}

bool RenderCompositorLayerNative::MaybeReadback(
    const gfx::IntSize& aReadbackSize, const wr::ImageFormat& aReadbackFormat,
    const Range<uint8_t>& aReadbackBuffer, bool* aNeedsYFlip) {
  MOZ_RELEASE_ASSERT(aReadbackFormat == wr::ImageFormat::BGRA8);

  if (!mNativeLayerRootSnapshotter) {
    mNativeLayerRootSnapshotter = mNativeLayerRoot->CreateSnapshotter();

    if (!mNativeLayerRootSnapshotter) {
      return false;
    }
  }
  bool success = mNativeLayerRootSnapshotter->ReadbackPixels(
      aReadbackSize, gfx::SurfaceFormat::B8G8R8A8, aReadbackBuffer);

  // ReadbackPixels might have changed the current context. Make sure GL is
  // current again.
  MakeCurrent();

  if (aNeedsYFlip) {
    *aNeedsYFlip = true;
  }

  return success;
}

bool RenderCompositorLayerNative::MaybeRecordFrame(
    layers::CompositionRecorder& aRecorder) {
  if (!mNativeLayerRootSnapshotter) {
    mNativeLayerRootSnapshotter = mNativeLayerRoot->CreateSnapshotter();
  }

  if (!mNativeLayerRootSnapshotter) {
    return true;
  }

  gfx::IntSize size = GetBufferSize().ToUnknownSize();
  RefPtr<layers::profiler_screenshots::RenderSource> snapshot =
      mNativeLayerRootSnapshotter->GetWindowContents(size);
  if (!snapshot) {
    return true;
  }

  RefPtr<layers::profiler_screenshots::AsyncReadbackBuffer> buffer =
      mNativeLayerRootSnapshotter->CreateAsyncReadbackBuffer(size);
  buffer->CopyFrom(snapshot);

  RefPtr<layers::RecordedFrame> frame =
      new RenderCompositorRecordedFrame(TimeStamp::Now(), std::move(buffer));
  aRecorder.RecordFrame(frame);

  // GetWindowContents might have changed the current context. Make sure our
  // context is current again.
  MakeCurrent();
  return true;
}

bool RenderCompositorLayerNative::MaybeGrabScreenshot(
    const gfx::IntSize& aWindowSize) {
  if (!mozilla::layers::ProfilerScreenshots::IsEnabled()) {
    return false;
  }

  if (!mNativeLayerRootSnapshotter) {
    mNativeLayerRootSnapshotter = mNativeLayerRoot->CreateSnapshotter();
  }

  if (mNativeLayerRootSnapshotter) {
    mProfilerScreenshotGrabber.MaybeGrabScreenshot(*mNativeLayerRootSnapshotter,
                                                   aWindowSize);

    // MaybeGrabScreenshot might have changed the current context. Make sure our
    // context is current again.
    MakeCurrent();
  }

  return true;
}

bool RenderCompositorLayerNative::MaybeProcessScreenshotQueue() {
  mProfilerScreenshotGrabber.MaybeProcessQueue();

  // MaybeProcessQueue might have changed the current context. Make sure our
  // context is current again.
  MakeCurrent();

  return true;
}

void RenderCompositorLayerNative::WaitUntilPresentationFlushed() {
  mNativeLayerRoot->WaitUntilCommitToScreenHasBeenProcessed();
}

void RenderCompositorLayerNative::CompositorBeginFrame() {
  mAddedLayers.Clear();
  mBeginFrameTimeStamp = TimeStamp::Now();
  mSurfacePoolHandle->OnBeginFrame();
  mNativeLayerRoot->PrepareForCommit();
  mCurrentFrame++;
  if (mEnableAsyncScreenshotInNextFrame) {
    mEnableAsyncScreenshot = true;
    mEnableAsyncScreenshotInNextFrame = false;
  }
}

void RenderCompositorLayerNative::CompositorEndFrame() {
#if defined(XP_DARWIN)
  // MacOS fails rendering without the flush here.
  DoFlush();
#endif

  mNativeLayerRoot->SetLayers(mAddedLayers);
  mNativeLayerRoot->CommitToScreen();
  mSurfacePoolHandle->OnEndFrame();
  if (mEnableAsyncScreenshot &&
      (mCurrentFrame - mAsyncScreenshotLastFrameUsed) > 1) {
    mEnableAsyncScreenshot = false;
  }
}

void RenderCompositorLayerNative::BindNativeLayer(wr::NativeSurfaceId aId) {
  MOZ_RELEASE_ASSERT(!mCurrentlyBoundNativeLayer);

  auto surfaceCursor = mSurfaces.find(aId);
  MOZ_RELEASE_ASSERT(surfaceCursor != mSurfaces.end());
  Surface& surface = surfaceCursor->second;

  mCurrentlyBoundNativeLayer = surface.mNativeLayer;
}

void RenderCompositorLayerNative::UnbindNativeLayer() {
  MOZ_RELEASE_ASSERT(mCurrentlyBoundNativeLayer);

  mCurrentlyBoundNativeLayer->NotifySurfaceReady();
  mCurrentlyBoundNativeLayer = nullptr;
}

void RenderCompositorLayerNative::CreateExternalSurface(wr::NativeSurfaceId aId,
                                                        bool aIsOpaque) {
  MOZ_RELEASE_ASSERT(mSurfaces.find(aId) == mSurfaces.end());

  RefPtr<layers::NativeLayer> layer =
      mNativeLayerRoot->CreateLayerForExternalTexture(aIsOpaque);

  Surface surface{DeviceIntSize{}, aIsOpaque};
  surface.mIsExternal = true;
  surface.mNativeLayer = layer;

  mSurfaces.insert({aId, std::move(surface)});
}

void RenderCompositorLayerNative::CreateSwapChainSurface(
    wr::NativeSurfaceId aId, wr::DeviceIntSize aSize, bool aIsOpaque,
    bool aNeedsSyncDcompCommit) {
  MOZ_RELEASE_ASSERT(mSurfaces.find(aId) == mSurfaces.end());

  Surface surface{aSize, aIsOpaque};
  surface.mNativeLayer = mNativeLayerRoot->CreateLayer(
      surface.Size(), aIsOpaque, mSurfacePoolHandle);

  mSurfaces.insert({aId, std::move(surface)});
}

void RenderCompositorLayerNative::ResizeSwapChainSurface(
    wr::NativeSurfaceId aId, wr::DeviceIntSize aSize) {
  auto surfaceCursor = mSurfaces.find(aId);
  MOZ_RELEASE_ASSERT(surfaceCursor != mSurfaces.end());
  Surface& surface = surfaceCursor->second;

  MOZ_ASSERT(!surface.mIsExternal);

  if (aSize == surface.mSize) {
    return;
  }

  surface.mSize = aSize;
  surface.mNativeLayer = mNativeLayerRoot->CreateLayer(
      surface.Size(), surface.mIsOpaque, mSurfacePoolHandle);
}

void RenderCompositorLayerNative::AttachExternalImage(
    wr::NativeSurfaceId aId, wr::ExternalImageId aExternalImage) {
  RenderTextureHost* image =
      RenderThread::Get()->GetRenderTexture(aExternalImage);
  MOZ_RELEASE_ASSERT(image);

  auto surfaceCursor = mSurfaces.find(aId);
  MOZ_RELEASE_ASSERT(surfaceCursor != mSurfaces.end());

  Surface& surface = surfaceCursor->second;
  MOZ_RELEASE_ASSERT(surface.mNativeLayer);
  MOZ_RELEASE_ASSERT(surface.mIsExternal);
  surface.mNativeLayer->AttachExternalImage(image);
}

void RenderCompositorLayerNativeOGL::AttachExternalImage(
    wr::NativeSurfaceId aId, wr::ExternalImageId aExternalImage) {
  RenderTextureHost* image =
      RenderThread::Get()->GetRenderTexture(aExternalImage);

  // image->Lock only uses the channel index to populate the returned
  // `WrExternalImage`. Since we don't use that, it doesn't matter
  // what channel index we pass.
  // A DMABUF buffer is attached to the wl_surface as-is and is never sampled
  // here, so locking it without a GL context skips a per-frame EGLImage import
  // and BO mapping. Every other backend still requires a context (e.g.
  // RenderMacIOSurfaceTextureHost).
  image->Lock(0, image->AsRenderDMABUFTextureHost() ? nullptr : mGL.get());

  RenderCompositorLayerNative::AttachExternalImage(aId, aExternalImage);
}

void RenderCompositorLayerNative::DestroySurface(NativeSurfaceId aId) {
  auto surfaceCursor = mSurfaces.find(aId);
  MOZ_RELEASE_ASSERT(surfaceCursor != mSurfaces.end());

  mSurfaces.erase(surfaceCursor);
}

/* static */
gfx::SamplingFilter RenderCompositorLayerNative::ToSamplingFilter(
    wr::ImageRendering aImageRendering) {
  if (aImageRendering == wr::ImageRendering::Auto) {
    return gfx::SamplingFilter::LINEAR;
  }
  return gfx::SamplingFilter::POINT;
}

void RenderCompositorLayerNative::AddSurface(
    wr::NativeSurfaceId aId, const wr::CompositorSurfaceTransform& aTransform,
    wr::DeviceIntRect aClipRect, wr::ImageRendering aImageRendering,
    wr::DeviceIntRect aRoundedClipRect, wr::ClipRadius aClipRadius) {
  MOZ_RELEASE_ASSERT(!mCurrentlyBoundNativeLayer);

  auto surfaceCursor = mSurfaces.find(aId);
  MOZ_RELEASE_ASSERT(surfaceCursor != mSurfaces.end());
  const Surface& surface = surfaceCursor->second;

  float sx = aTransform.scale.x;
  float sy = aTransform.scale.y;
  float tx = aTransform.offset.x;
  float ty = aTransform.offset.y;
  gfx::Matrix4x4 transform(sx, 0.0, 0.0, 0.0, 0.0, sy, 0.0, 0.0, 0.0, 0.0, 1.0,
                           0.0, tx, ty, 0.0, 1.0);

  RefPtr<layers::NativeLayer> layer = surface.mNativeLayer;
  gfx::IntPoint layerPosition(0, 0);
  layer->SetPosition(layerPosition);
  gfx::IntRect clipRect(aClipRect.min.x, aClipRect.min.y, aClipRect.width(),
                        aClipRect.height());
  layer->SetClipRect(Some(clipRect));
  gfx::Rect roundedClipRect(aRoundedClipRect.min.x, aRoundedClipRect.min.y,
                            aRoundedClipRect.width(),
                            aRoundedClipRect.height());
  gfx::RectCornerRadii clipRadius(aClipRadius.top_left, aClipRadius.top_right,
                                  aClipRadius.bottom_right,
                                  aClipRadius.bottom_left);
  gfx::RoundedRect roundedClip(roundedClipRect, clipRadius);
  layer->SetRoundedClipRect(Some(roundedClip));
  layer->SetTransform(transform);
  layer->SetSamplingFilter(ToSamplingFilter(aImageRendering));
  mAddedLayers.AppendElement(layer);

  if (surface.mIsExternal) {
    RefPtr<layers::GpuFence> fence = layer->GetGpuFence();
    if (fence && BackendType() == layers::WebRenderBackend::HARDWARE) {
      mPendingGpuFences.emplace_back(fence);
    }
  }
}

/* static */
UniquePtr<RenderCompositor> RenderCompositorLayerNativeOGL::Create(
    const RefPtr<widget::CompositorWidget>& aWidget, nsACString& aError) {
  RefPtr<gl::GLContext> gl = RenderThread::Get()->SingletonGL();
  if (!gl) {
    gl = gl::GLContextProvider::CreateForCompositorWidget(
        aWidget, /* aHardwareWebRender */ true, /* aForceAccelerated */ true);
    RenderThread::MaybeEnableGLDebugMessage(gl);
  }
  if (!gl || !gl->MakeCurrent()) {
    gfxCriticalNote << "Failed GL context creation for WebRender: "
                    << gfx::hexa(gl.get());
    return nullptr;
  }
  return std::make_unique<RenderCompositorLayerNativeOGL>(aWidget,
                                                          std::move(gl));
}

RenderCompositorLayerNativeOGL::RenderCompositorLayerNativeOGL(
    const RefPtr<widget::CompositorWidget>& aWidget,
    RefPtr<gl::GLContext>&& aGL)
    : RenderCompositorLayerNative(aWidget, aGL), mGL(aGL) {
  MOZ_ASSERT(mGL);
}

RenderCompositorLayerNativeOGL::~RenderCompositorLayerNativeOGL() {
  if (!mGL->MakeCurrent()) {
    gfxCriticalNote
        << "Failed to make render context current during destroying.";
    // Leak resources!
    mPreviousFrameDoneFences = nullptr;
    mThisFrameDoneFences = nullptr;
    return;
  }

  if (mPreviousFrameDoneFences && mPreviousFrameDoneFences->mSync) {
    mGL->fDeleteSync(mPreviousFrameDoneFences->mSync);
  }
  if (mThisFrameDoneFences && mThisFrameDoneFences->mSync) {
    mGL->fDeleteSync(mThisFrameDoneFences->mSync);
  }
}

bool RenderCompositorLayerNativeOGL::InitDefaultFramebuffer() {
  mGL->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, mGL->GetDefaultFramebuffer());
  return true;
}

void RenderCompositorLayerNativeOGL::DoSwap() { InsertFrameDoneSync(); }

void RenderCompositorLayerNativeOGL::DoFlush() {
  if (mGL->GetContextType() == gl::GLContextType::EGL) {
    const auto* gle = gl::GLContextEGL::Cast(mGL);
    const auto& egl = gle->mEgl;

    // When setting a Metal-rendered IOSurface as a CALayer's contents, pending
    // Metal commands must be explicitly scheduled to ensure they are committed
    // with the next transaction.
    // https://groups.google.com/g/angleproject/c/6UeZshVzt28/m/pRCjGEfmEwAJ
    if (egl->IsExtensionSupported(
            gl::EGLExtension::ANGLE_wait_until_work_scheduled)) {
      egl->fWaitUntilWorkScheduledANGLE();
    }
  }
  mGL->fFlush();
}

void RenderCompositorLayerNativeOGL::InsertFrameDoneSync() {
#ifdef XP_DARWIN
  // Only do this on macOS.
  // On other platforms, SwapBuffers automatically applies back-pressure.
  if (mThisFrameDoneFences && mThisFrameDoneFences->mSync) {
    mGL->fDeleteSync(mThisFrameDoneFences->mSync);
  }
  mThisFrameDoneFences =
      std::make_unique<BackPressureFences>(std::move(mPendingGpuFences));
  mThisFrameDoneFences->mSync =
      mGL->fFenceSync(LOCAL_GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
#endif
}

bool RenderCompositorLayerNativeOGL::WaitForGPU() {
  if (mPreviousFrameDoneFences) {
    for (const auto& fence : mPreviousFrameDoneFences->mGpuFences) {
      fence->ClientWait(TimeDuration::Forever());
    }

    if (mPreviousFrameDoneFences->mSync) {
      AUTO_PROFILER_LABEL("Waiting for GPU to finish previous frame", GRAPHICS);
      mGL->fClientWaitSync(mPreviousFrameDoneFences->mSync,
                           LOCAL_GL_SYNC_FLUSH_COMMANDS_BIT,
                           LOCAL_GL_TIMEOUT_IGNORED);
      mGL->fDeleteSync(mPreviousFrameDoneFences->mSync);
    }
  }
  mPreviousFrameDoneFences = std::move(mThisFrameDoneFences);
  MOZ_ASSERT(!mThisFrameDoneFences);

  return true;
}

void RenderCompositorLayerNativeOGL::BindSwapChain(
    wr::NativeSurfaceId aId, const wr::DeviceIntRect* aDirtyRects,
    size_t aNumDirtyRects) {
  BindNativeLayer(aId);
  MOZ_ASSERT(mCurrentlyBoundNativeLayer);

  gfx::IntSize size = mCurrentlyBoundNativeLayer->GetSize();
  gfx::IntRect validRect = mCurrentlyBoundNativeLayer->GetRect();

  const auto dirtyRect = [&]() {
    if (aNumDirtyRects > 0) {
      MOZ_RELEASE_ASSERT(aNumDirtyRects == 1);

      const auto& rect = aDirtyRects[0];

      // Clip rect to bufferSize
      int left = std::clamp((int)rect.min.x, 0, size.width);
      int top = std::clamp((int)rect.min.y, 0, size.height);
      int right = std::clamp((int)rect.max.x, 0, size.width);
      int bottom = std::clamp((int)rect.max.y, 0, size.height);

      int width = right - left;
      int height = bottom - top;
      MOZ_RELEASE_ASSERT(width >= 0);
      MOZ_RELEASE_ASSERT(height >= 0);

      return gfx::IntRect(left, top, width, height);
    }

    return gfx::IntRect(0, 0, size.width, size.height);
  }();

  Maybe<GLuint> fbo = mCurrentlyBoundNativeLayer->NextSurfaceAsFramebuffer(
      validRect, dirtyRect, true);
  if (!fbo) {
    // XXX
    return;
  }
  mGL->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, *fbo);
}

void RenderCompositorLayerNativeOGL::PresentSwapChain(
    wr::NativeSurfaceId aId, const wr::DeviceIntRect* aDirtyRects,
    size_t aNumDirtyRects) {
  mGL->fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, 0);

  UnbindNativeLayer();
}

}  // namespace mozilla::wr
