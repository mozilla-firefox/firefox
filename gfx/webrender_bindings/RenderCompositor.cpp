/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RenderCompositor.h"

#include "GLContext.h"
#include "gfxConfig.h"
#include "gfxPlatform.h"
#include "mozilla/StaticPrefs_gfx.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/gfx/gfxVars.h"
#include "mozilla/layers/SyncObject.h"
#include "mozilla/webrender/RenderCompositorLayerNative.h"
#include "mozilla/webrender/RenderCompositorLayersSWGL.h"
#include "mozilla/webrender/RenderCompositorOGL.h"
#include "mozilla/webrender/RenderCompositorSWGL.h"
#include "mozilla/widget/CompositorWidget.h"

#ifdef MOZ_WIDGET_ANDROID
#  include "GLContextEGL.h"
#  include "ScopedGLHelpers.h"
#  include "mozilla/ScopeExit.h"
#  include "mozilla/gfx/2D.h"
#  include "mozilla/layers/AndroidHardwareBuffer.h"
#endif

#ifdef XP_WIN
#  include "mozilla/webrender/RenderCompositorANGLE.h"
#  include "mozilla/widget/WinCompositorWidget.h"
#endif

#if defined(MOZ_WIDGET_ANDROID) || defined(MOZ_WIDGET_GTK)
#  include "mozilla/webrender/RenderCompositorEGL.h"
#endif

#ifdef MOZ_WAYLAND
#  include "mozilla/webrender/RenderCompositorNative.h"
#endif

#ifdef XP_DARWIN
#  include "mozilla/webrender/RenderCompositorNative.h"
#endif

namespace mozilla::wr {

void wr_compositor_add_surface(void* aCompositor, wr::NativeSurfaceId aId,
                               const wr::CompositorSurfaceTransform* aTransform,
                               wr::DeviceIntRect aClipRect,
                               wr::ImageRendering aImageRendering,
                               wr::DeviceIntRect aRoundedClipRect,
                               wr::ClipRadius aRoundedClipRadius) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->AddSurface(aId, *aTransform, aClipRect, aImageRendering,
                         aRoundedClipRect, aRoundedClipRadius);
}

void wr_compositor_begin_frame(void* aCompositor) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->CompositorBeginFrame();
}

void wr_compositor_bind(void* aCompositor, wr::NativeTileId aId,
                        wr::DeviceIntPoint* aOffset, uint64_t* aSurfaceHandle,
                        wr::DeviceIntRect aDirtyRect,
                        wr::DeviceIntRect aValidRect) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->Bind(aId, aOffset, aSurfaceHandle, aDirtyRect, aValidRect);
}

void wr_compositor_create_surface(void* aCompositor, wr::NativeSurfaceId aId,
                                  wr::DeviceIntSize aTileSize, bool aIsOpaque) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->CreateSurface(aId, aTileSize, aIsOpaque);
}

void wr_compositor_create_external_surface(void* aCompositor,
                                           wr::NativeSurfaceId aId,
                                           bool aIsOpaque) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->CreateExternalSurface(aId, aIsOpaque);
}

void wr_compositor_create_swapchain_surface(void* aCompositor,
                                            wr::NativeSurfaceId aId,
                                            wr::DeviceIntSize aSize,
                                            bool aIsOpaque,
                                            bool aNeedsSyncDcompCommit) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->CreateSwapChainSurface(aId, aSize, aIsOpaque,
                                     aNeedsSyncDcompCommit);
}

void wr_compositor_resize_swapchain(void* aCompositor, wr::NativeSurfaceId aId,
                                    wr::DeviceIntSize aSize) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->ResizeSwapChainSurface(aId, aSize);
}

void wr_compositor_create_backdrop_surface(void* aCompositor,
                                           wr::NativeSurfaceId aId,
                                           wr::ColorF aColor) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->CreateBackdropSurface(aId, aColor);
}

void wr_compositor_create_tile(void* aCompositor, wr::NativeSurfaceId aId,
                               int32_t aX, int32_t aY) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->CreateTile(aId, aX, aY);
}

void wr_compositor_destroy_tile(void* aCompositor, wr::NativeSurfaceId aId,
                                int32_t aX, int32_t aY) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->DestroyTile(aId, aX, aY);
}

void wr_compositor_bind_swapchain(void* aCompositor, wr::NativeSurfaceId aId,
                                  const wr::DeviceIntRect* aDirtyRects,
                                  size_t aNumDirtyRects) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->BindSwapChain(aId, aDirtyRects, aNumDirtyRects);
}

void wr_compositor_present_swapchain(void* aCompositor, wr::NativeSurfaceId aId,
                                     const wr::DeviceIntRect* aDirtyRects,
                                     size_t aNumDirtyRects) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->PresentSwapChain(aId, aDirtyRects, aNumDirtyRects);
}

void wr_compositor_destroy_surface(void* aCompositor, NativeSurfaceId aId) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->DestroySurface(aId);
}

void wr_compositor_attach_external_image(void* aCompositor,
                                         wr::NativeSurfaceId aId,
                                         wr::ExternalImageId aExternalImage) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->AttachExternalImage(aId, aExternalImage);
}

void wr_compositor_start_compositing(void* aCompositor, wr::ColorF aClearColor,
                                     const wr::DeviceIntRect* aDirtyRects,
                                     size_t aNumDirtyRects,
                                     const wr::DeviceIntRect* aOpaqueRects,
                                     size_t aNumOpaqueRects) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->StartCompositing(aClearColor, aDirtyRects, aNumDirtyRects,
                               aOpaqueRects, aNumOpaqueRects);
}

void wr_compositor_end_frame(void* aCompositor) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->CompositorEndFrame();
}

void wr_compositor_get_capabilities(void* aCompositor,
                                    CompositorCapabilities* aCaps) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->GetCompositorCapabilities(aCaps);
}

void wr_compositor_get_window_visibility(void* aCompositor,
                                         WindowVisibility* aVisibility) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->GetWindowVisibility(aVisibility);
}

void wr_compositor_get_window_properties(void* aCompositor,
                                         WindowProperties* aProperties) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->GetWindowProperties(aProperties);
}

void wr_compositor_get_swapchain_mode(void* aCompositor, bool* aUsesOsLayers,
                                      size_t* aMaxPartialPresentRects) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  *aUsesOsLayers = compositor->UseLayerCompositor();
  *aMaxPartialPresentRects = compositor->GetMaxPartialPresentRects();
}

void wr_compositor_unbind(void* aCompositor) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->Unbind();
}

void wr_compositor_deinit(void* aCompositor) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->DeInit();
}

void wr_compositor_map_tile(void* aCompositor, wr::NativeTileId aId,
                            wr::DeviceIntRect aDirtyRect,
                            wr::DeviceIntRect aValidRect, void** aData,
                            int32_t* aStride) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->MapTile(aId, aDirtyRect, aValidRect, aData, aStride);
}

void wr_compositor_unmap_tile(void* aCompositor) {
  RenderCompositor* compositor = static_cast<RenderCompositor*>(aCompositor);
  compositor->UnmapTile();
}

/* static */
UniquePtr<RenderCompositor> RenderCompositor::Create(
    const RefPtr<widget::CompositorWidget>& aWidget, nsACString& aError) {
  if (aWidget->AsDelegate() &&
      aWidget->AsDelegate()->AsHeadlessCompositorWidget()) {
    MOZ_ASSERT(aWidget->GetCompositorOptions().UseSoftwareWebRender());
    return RenderCompositorSWGL::Create(aWidget, aError);
  }

  if (aWidget->GetCompositorOptions().UseSoftwareWebRender()) {
#ifdef XP_DARWIN
    // Mac uses NativeLayerCA
    if (!gfxPlatform::IsHeadless()) {
      return RenderCompositorNativeSWGL::Create(aWidget, aError);
    }
#elif defined(MOZ_WAYLAND)
    // Some widgets on Wayland (D&D popups for instance) can't use native
    // compositor due to system limitations.
    if (gfx::gfxVars::UseWebRenderCompositor() &&
        aWidget->GetCompositorOptions().AllowNativeCompositor()) {
      return RenderCompositorNativeSWGL::Create(aWidget, aError);
    }
#endif
    UniquePtr<RenderCompositor> comp =
        RenderCompositorLayersSWGL::Create(aWidget, aError);
    if (comp) {
      return comp;
    }
#if defined(MOZ_WIDGET_ANDROID)
    // On Android, we do not want to fallback from RenderCompositorOGLSWGL to
    // RenderCompositorSWGL.
    if (aWidget->GetCompositorOptions().AllowSoftwareWebRenderOGL()) {
      return nullptr;
    }
#endif
    return RenderCompositorSWGL::Create(aWidget, aError);
  }

#ifdef XP_WIN
  if (gfx::gfxVars::UseWebRenderANGLE()) {
    return RenderCompositorANGLE::Create(aWidget, aError);
  }
#endif

#if defined(MOZ_WAYLAND)
  if (gfx::gfxVars::UseWebRenderCompositor() &&
      aWidget->GetCompositorOptions().AllowNativeCompositor()) {
    if (StaticPrefs::gfx_webrender_layer_compositor()) {
      return RenderCompositorLayerNativeOGL::Create(aWidget, aError);
    } else {
      return RenderCompositorNativeOGL::Create(aWidget, aError);
    }
  }
#endif

#if defined(MOZ_WIDGET_ANDROID) || defined(MOZ_WIDGET_GTK)
  UniquePtr<RenderCompositor> eglCompositor =
      RenderCompositorEGL::Create(aWidget, aError);
  if (eglCompositor) {
    return eglCompositor;
  }
#endif

#if defined(MOZ_WIDGET_ANDROID)
  // RenderCompositorOGL is not used on android
  return nullptr;
#elif defined(XP_DARWIN)
  if (gfx::gfxVars::UseWebRenderCompositor() &&
      aWidget->GetCompositorOptions().AllowNativeCompositor()) {
    if (StaticPrefs::gfx_webrender_layer_compositor()) {
      UniquePtr<RenderCompositor> compositor =
          RenderCompositorLayerNativeOGL::Create(aWidget, aError);
      if (compositor) {
        return compositor;
      }
    }
  }
  // Mac uses NativeLayerCA
  return RenderCompositorNativeOGL::Create(aWidget, aError);
#else
  return RenderCompositorOGL::Create(aWidget, aError);
#endif
}

RenderCompositor::RenderCompositor(
    const RefPtr<widget::CompositorWidget>& aWidget)
    : mWidget(aWidget) {}

RenderCompositor::~RenderCompositor() = default;

bool RenderCompositor::MakeCurrent() { return gl()->MakeCurrent(); }

void RenderCompositor::GetCompositorCapabilities(
    CompositorCapabilities* aCaps) {
  if (StaticPrefs::gfx_webrender_compositor_max_update_rects_AtStartup() > 0) {
    aCaps->max_update_rects = 1;
  } else {
    aCaps->max_update_rects = 0;
  }
}

void RenderCompositor::GetWindowVisibility(WindowVisibility* aVisibility) {
#ifdef XP_WIN
  auto* widget = mWidget->AsWindows();
  if (!widget) {
    return;
  }
  aVisibility->is_fully_occluded = widget->GetWindowIsFullyOccluded();
#endif
}

void RenderCompositor::GetWindowProperties(WindowProperties* aProperties) {
  aProperties->is_opaque = true;
}

gfx::DeviceResetReason RenderCompositor::IsContextLost(bool aForce) {
  auto* glc = gl();
  // GetGraphicsResetStatus may trigger an implicit MakeCurrent if robustness
  // is not supported, so unless we are forcing, pass on the check.
  if (!glc || (!aForce && !glc->IsSupported(gl::GLFeature::robustness))) {
    return gfx::DeviceResetReason::OK;
  }
  auto resetStatus = glc->fGetGraphicsResetStatus();
  switch (resetStatus) {
    case LOCAL_GL_NO_ERROR:
      return gfx::DeviceResetReason::OK;
    case LOCAL_GL_INNOCENT_CONTEXT_RESET_ARB:
      return gfx::DeviceResetReason::DRIVER_ERROR;
    case LOCAL_GL_PURGED_CONTEXT_RESET_NV:
      return gfx::DeviceResetReason::NVIDIA_VIDEO;
    case LOCAL_GL_GUILTY_CONTEXT_RESET_ARB:
      return gfx::DeviceResetReason::RESET;
    case LOCAL_GL_UNKNOWN_CONTEXT_RESET_ARB:
      return gfx::DeviceResetReason::UNKNOWN;
    default:
      gfxCriticalError() << "Device reset with WR context unexpected status: "
                         << gfx::hexa(resetStatus);
      break;
  }
  return gfx::DeviceResetReason::OTHER;
}

#ifdef MOZ_WIDGET_ANDROID
/* static */
bool RenderCompositor::CaptureScreenPixelsFromGL(
    gl::GLContextEGL* aGL, const gfx::IntRect& aSourceRect,
    const LayoutDeviceIntSize& aFramebufferSize,
    gl::OriginPos aFramebufferOrigin,
    layers::AndroidHardwareBuffer* aHardwareBuffer) {
  const auto& egl = aGL->mEgl;
  const bool needsYFlip = aFramebufferOrigin != gl::OriginPos::TopLeft;
  gl::ScopedBindFramebuffer scopedBind(aGL);

  const bool canBlit =
      aGL->IsSupported(gl::GLFeature::framebuffer_blit) &&
      aGL->IsExtensionSupported(gl::GLContext::OES_EGL_image) &&
      egl->IsExtensionSupported(gl::EGLExtension::KHR_fence_sync) &&
      egl->IsExtensionSupported(gl::EGLExtension::ANDROID_native_fence_sync);

  if (!canBlit) {
    // Fallback path for devices which cannot blit directly to the hardware
    // buffer. Use glReadPixels to read the source rect into a buffer, then draw
    // into the CPU-mapped hardware buffer, flipping and scaling as required.
    const int bpp = gfx::BytesPerPixel(aHardwareBuffer->mFormat);
    const RefPtr<gfx::DataSourceSurface> surface =
        gfx::Factory::CreateDataSourceSurfaceWithStride(
            aSourceRect.Size(), gfx::SurfaceFormat::R8G8B8A8,
            aSourceRect.width * bpp);
    if (!surface) {
      return false;
    }
    {
      const gfx::DataSourceSurface::ScopedMap map(
          surface, gfx::DataSourceSurface::WRITE);
      if (!map.IsMapped()) {
        return false;
      }
      aGL->BindReadFB(0);
      const int srcY = needsYFlip ? aFramebufferSize.height - aSourceRect.y -
                                        aSourceRect.height
                                  : aSourceRect.y;
      aGL->fReadPixels(aSourceRect.x, srcY, aSourceRect.width,
                       aSourceRect.height, LOCAL_GL_RGBA,
                       LOCAL_GL_UNSIGNED_BYTE, map.GetData());
    }

    uint8_t* destBuf = nullptr;
    if (aHardwareBuffer->Lock(AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, nullptr,
                              reinterpret_cast<void**>(&destBuf))) {
      return false;
    }
    const auto hardwareBufferUnlock =
        MakeScopeExit([&]() { aHardwareBuffer->Unlock(); });
    const RefPtr<gfx::DrawTarget> dt = gfx::Factory::CreateDrawTargetForData(
        gfx::BackendType::SKIA, destBuf, aHardwareBuffer->mSize,
        aHardwareBuffer->mStride * bpp, aHardwareBuffer->mFormat);
    if (!dt) {
      return false;
    }
    if (needsYFlip) {
      dt->SetTransform(
          gfx::Matrix::Scaling(1.0, -1.0) *
          gfx::Matrix::Translation(0, aHardwareBuffer->mSize.height));
    }
    dt->DrawSurface(surface, gfx::Rect({}, gfx::Size(aHardwareBuffer->mSize)),
                    gfx::Rect({}, gfx::Size(aSourceRect.Size())),
                    gfx::DrawSurfaceOptions(gfx::SamplingFilter::LINEAR),
                    gfx::DrawOptions(1.0f, gfx::CompositionOp::OP_SOURCE));
    return true;
  }

  // Attach the hardware buffer to a framebuffer, then blit the source rect to
  // it from the main framebuffer, flipping and scaling as required.
  gl::ScopedEGLImageForAndroidHardwareBuffer eglImage(aGL, aHardwareBuffer);
  if (eglImage.Image() == EGL_NO_IMAGE) {
    return false;
  }
  gl::ScopedRenderbuffer rb(aGL);
  aGL->fBindRenderbuffer(LOCAL_GL_RENDERBUFFER, rb);
  aGL->fEGLImageTargetRenderbufferStorage(LOCAL_GL_RENDERBUFFER, eglImage);
  gl::ScopedFramebufferForRenderbuffer fb(aGL, rb);

  const auto srcRect =
      needsYFlip
          ? gfx::IntRect(aSourceRect.x, aFramebufferSize.height - aSourceRect.y,
                         aSourceRect.width, -aSourceRect.height)
          : aSourceRect;
  const auto destRect = gfx::IntRect({}, aHardwareBuffer->mSize);
  aGL->BindReadFB(0);
  aGL->BindDrawFB(fb.FB());
  aGL->fBlitFramebuffer(srcRect.x, srcRect.y, srcRect.XMost(), srcRect.YMost(),
                        destRect.x, destRect.y, destRect.XMost(),
                        destRect.YMost(), LOCAL_GL_COLOR_BUFFER_BIT,
                        LOCAL_GL_LINEAR);

  if (EGLSync sync =
          egl->fCreateSyncKHR(LOCAL_EGL_SYNC_NATIVE_FENCE_ANDROID, nullptr)) {
    auto fence = UniqueFileHandle(egl->fDupNativeFenceFDANDROID(sync));
    if (fence) {
      aHardwareBuffer->SetAcquireFence(std::move(fence));
    }
    egl->fDestroySync(sync);
  }
  return true;
}
#endif

}  // namespace mozilla::wr
