/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "BufferSurface.h"

#include <gbm.h>

#ifdef MOZ_LOGGING
#  include "gfxUtils.h"
#  include "mozilla/gfx/Logging.h"
#endif

#ifdef MOZ_WAYLAND
#  include "nsWaylandDisplay.h"
#endif
#include "GLContextEGL.h"
#include "GLContextProvider.h"
#include "ScopedGLHelpers.h"
#include "mozilla/widget/DMABufDevice.h"

using namespace mozilla;
using namespace mozilla::widget;
using namespace mozilla::gfx;

#undef LOGDMABUF
#undef LOGDMABUFS
#ifdef MOZ_LOGGING
#  include "mozilla/Logging.h"

extern mozilla::LazyLogModule gDmabufLog;
#  define LOGDMABUF(str, ...)                     \
    MOZ_LOG(gDmabufLog, mozilla::LogLevel::Debug, \
            ("%s: " str, GetDebugTag().get(), ##__VA_ARGS__))
#  define LOGDMABUFS(str, ...) \
    MOZ_LOG(gDmabufLog, mozilla::LogLevel::Debug, (str, ##__VA_ARGS__))
#else
#  define LOGDMABUF(str, ...)
#  define LOGDMABUFS(str, ...)
#endif /* MOZ_LOGGING */

BufferSurface::~BufferSurface() = default;

nsAutoCString BufferSurface::GetDebugTag() const {
  nsAutoCString tag;
  tag.AppendPrintf("[%p]", this);
  return tag;
}

bool BufferSurface::CreateTextures(gl::GLContext* aGLContext) {
  for (int plane = 0; plane < GetTextureCount(); plane++) {
    if (!CreateTexture(aGLContext, plane)) {
      LOGDMABUF("BufferSurface::CreateTextures() failed at plane %d", plane);
      return false;
    }
  }
  mTextureIsDirty = false;
  return true;
}

/* static */
void BufferSurface::SetTextureFilters(gl::GLContext* aGL, GLuint aTexture,
                                      GLenum aTarget) {
  const gl::ScopedBindTexture savedTex(aGL, aTexture, aTarget);
  aGL->fTexParameteri(aTarget, LOCAL_GL_TEXTURE_WRAP_S, LOCAL_GL_CLAMP_TO_EDGE);
  aGL->fTexParameteri(aTarget, LOCAL_GL_TEXTURE_WRAP_T, LOCAL_GL_CLAMP_TO_EDGE);
  aGL->fTexParameteri(aTarget, LOCAL_GL_TEXTURE_MAG_FILTER, LOCAL_GL_LINEAR);
  aGL->fTexParameteri(aTarget, LOCAL_GL_TEXTURE_MIN_FILTER, LOCAL_GL_LINEAR);
}

/* static */
void BufferSurface::SetTextureFilters(gl::GLContext* aGL, GLuint aTexture) {
  SetTextureFilters(aGL, aTexture, LOCAL_GL_TEXTURE_2D);
}

bool BufferSurface::HoldsTexture() const {
  for (int i = 0; i < BUFFER_SURFACE_PLANES; i++) {
    if (mTexture[i] || mEGLImage[i]) {
      return true;
    }
  }
  return false;
}

void BufferSurface::ReleaseTextures() {
  LOGDMABUF("BufferSurface::ReleaseTextures() UID %d", mUID);

  if (!HoldsTexture()) {
    return;
  }

  if (!mGL) {
#ifdef NIGHTLY_BUILD
    MOZ_DIAGNOSTIC_ASSERT(mGL, "Missing GL context!");
#else
    NS_WARNING(
        "BufferSurface::ReleaseTextures(): Missing GL context! We're "
        "leaking textures!");
    return;
#endif
  }

  if (mGL->MakeCurrent()) {
    mGL->fDeleteTextures(BUFFER_SURFACE_PLANES, mTexture);
    std::fill(std::begin(mTexture), std::end(mTexture), 0);
  } else {
    NS_WARNING(
        "BufferSurface::ReleaseTextures(): MakeCurrent failed. We're "
        "leaking textures!");
  }

  const auto& gle = gl::GLContextEGL::Cast(mGL);
  const auto& egl = gle->mEgl;
  for (auto& eglImage : mEGLImage) {
    if (eglImage) {
      egl->fDestroyImage(eglImage);
      eglImage = nullptr;
    }
  }

  mGL = nullptr;
}

bool BufferSurface::HasAlpha() const {
  return mFOURCCFormat == GBM_FORMAT_ARGB8888 ||
         mFOURCCFormat == GBM_FORMAT_ABGR8888 ||
         mFOURCCFormat == GBM_FORMAT_RGBA8888 ||
         mFOURCCFormat == GBM_FORMAT_BGRA8888 ||
         mFOURCCFormat == GBM_FORMAT_ABGR2101010;
}

BufferSurface::SurfaceType BufferSurface::GetSurfaceType() const {
  switch (mFOURCCFormat) {
    case GBM_FORMAT_ARGB8888:
    case GBM_FORMAT_ABGR8888:
    case GBM_FORMAT_BGRA8888:
    case GBM_FORMAT_RGBA8888:
    case GBM_FORMAT_XRGB8888:
    case GBM_FORMAT_XBGR8888:
    case GBM_FORMAT_BGRX8888:
    case GBM_FORMAT_RGBX8888:
    case GBM_FORMAT_ABGR2101010:
      return SURFACE_RGBA;
    case VA_FOURCC_P010:
    case VA_FOURCC_P016:
    case VA_FOURCC_NV12:
    case VA_FOURCC_YV12:
    case VA_FOURCC_I420:
      return SURFACE_YUV;
    default:
      gfxCriticalNoteOnce << "BufferSurface::GetSurfaceType(): unknown format: "
                          << mFOURCCFormat;
      return SURFACE_RGBA;
  }
}

mozilla::gfx::SurfaceFormat BufferSurface::GetFormat() const {
  switch (mFOURCCFormat) {
    case GBM_FORMAT_ARGB8888:
      return gfx::SurfaceFormat::B8G8R8A8;
    case GBM_FORMAT_ABGR8888:
      return gfx::SurfaceFormat::R8G8B8A8;
    case GBM_FORMAT_BGRA8888:
      return gfx::SurfaceFormat::A8R8G8B8;
    case GBM_FORMAT_RGBA8888:
      gfxCriticalError() << "DMABufSurfaceRGBA::GetFormat(): Unsupported "
                            "format GBM_FORMAT_RGBA8888";
      return gfx::SurfaceFormat::UNKNOWN;
    case GBM_FORMAT_XRGB8888:
      return gfx::SurfaceFormat::B8G8R8X8;
    case GBM_FORMAT_XBGR8888:
      return gfx::SurfaceFormat::R8G8B8X8;
    case GBM_FORMAT_BGRX8888:
      return gfx::SurfaceFormat::X8R8G8B8;
    case GBM_FORMAT_RGBX8888:
      gfxCriticalError() << "DMABufSurfaceRGBA::GetFormat(): Unsupported "
                            "format GBM_FORMAT_RGBX8888";
      return gfx::SurfaceFormat::UNKNOWN;
    case VA_FOURCC_P010:
      return gfx::SurfaceFormat::P010;
    case VA_FOURCC_P016:
      return gfx::SurfaceFormat::P016;
    case VA_FOURCC_NV12:
      return gfx::SurfaceFormat::NV12;
    case VA_FOURCC_YV12:
    case VA_FOURCC_I420:
      return (GetColorDepth() == gfx::ColorDepth::COLOR_10)
                 ? gfx::SurfaceFormat::YUV420P10
                 : gfx::SurfaceFormat::YUV420;
    case GBM_FORMAT_ABGR2101010:
      return gfx::SurfaceFormat::R10G10B10A2_UINT32;
    default:
      gfxCriticalNoteOnce << "DMABufSurfaceYUV::GetFormat() unknown format: "
                          << mFOURCCFormat;
      return gfx::SurfaceFormat::UNKNOWN;
  }
}

void BufferSurface::SetFormat(mozilla::gfx::SurfaceFormat aFormat) {
  mFOURCCFormat = [aFormat]() {
    switch (aFormat) {
      case gfx::SurfaceFormat::B8G8R8A8:
        return GBM_FORMAT_ARGB8888;
      case gfx::SurfaceFormat::R8G8B8A8:
        return GBM_FORMAT_ABGR8888;
      case gfx::SurfaceFormat::A8R8G8B8:
        return GBM_FORMAT_BGRA8888;
      case gfx::SurfaceFormat::B8G8R8X8:
        return GBM_FORMAT_XRGB8888;
      case gfx::SurfaceFormat::R8G8B8X8:
        return GBM_FORMAT_XBGR8888;
      case gfx::SurfaceFormat::X8R8G8B8:
        return GBM_FORMAT_BGRX8888;
      case gfx::SurfaceFormat::R10G10B10A2_UINT32:
        return GBM_FORMAT_ABGR2101010;
      default:
        gfxCriticalNoteOnce << "BufferSurface::SetFormat() unsupported format: "
                            << aFormat;
        MOZ_DIAGNOSTIC_CRASH("unsupported format");
        return (uint32_t)0;
    }
  }();
}

int BufferSurface::GetFormatBPP() const {
  switch (mFOURCCFormat) {
    case GBM_FORMAT_ARGB8888:
    case GBM_FORMAT_ABGR8888:
    case GBM_FORMAT_BGRA8888:
    case GBM_FORMAT_RGBA8888:
    case GBM_FORMAT_XRGB8888:
    case GBM_FORMAT_XBGR8888:
    case GBM_FORMAT_BGRX8888:
    case GBM_FORMAT_RGBX8888:
    case GBM_FORMAT_ABGR2101010:
      return 4;
    default:
      gfxCriticalNoteOnce
          << "BufferSurface::GetFormatBPP() unsupported format: "
          << mFOURCCFormat;
      MOZ_DIAGNOSTIC_CRASH("unsupported format");
      return 0;
  }
}

gfx::ColorDepth BufferSurface::GetColorDepth() const {
  if (mColorDepth) {
    return mColorDepth.value();
  }
  switch (mFOURCCFormat) {
    case GBM_FORMAT_ABGR2101010:
    case VA_FOURCC_P010:
    case VA_FOURCC_P016:
      return gfx::ColorDepth::COLOR_10;
    default:
      return gfx::ColorDepth::COLOR_8;
  }
}

#ifdef MOZ_WAYLAND
int BufferSurface::GetWLFormat() const {
  switch (mFOURCCFormat) {
    case GBM_FORMAT_ARGB8888:
      return WL_SHM_FORMAT_ARGB8888;
    case GBM_FORMAT_XRGB8888:
      return WL_SHM_FORMAT_XRGB8888;
    case GBM_FORMAT_ABGR8888:
    case GBM_FORMAT_BGRA8888:
    case GBM_FORMAT_XBGR8888:
    case GBM_FORMAT_BGRX8888:
    case GBM_FORMAT_ABGR2101010:
    case VA_FOURCC_P010:
    case VA_FOURCC_P016:
    case VA_FOURCC_NV12:
    case VA_FOURCC_YV12:
    case VA_FOURCC_I420:
      return mFOURCCFormat;
    default:
      gfxCriticalNoteOnce << "BufferSurface::GetWLFormat() unknown format: "
                          << mFOURCCFormat;
      return 0;
  }
}

int BufferSurface::GetWLColorCoeficients() {
  if (GetSurfaceType() == SURFACE_RGBA) {
    return WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_IDENTITY;
  }
  switch (mColorSpace) {
    case gfx::YUVColorSpace::BT601:
      return WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT601;
    case gfx::YUVColorSpace::BT709:
      return WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT709;
    case gfx::YUVColorSpace::BT2020:
      return WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT2020;
    default:
      MOZ_DIAGNOSTIC_CRASH("Unsupported YUV color space!");
      return 0;
  }
}

#  ifdef MOZ_LOGGING
const char* BufferSurface::GetWLColorCoeficientsName(int aWLColorCoeficients) {
  switch (aWLColorCoeficients) {
    case WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_IDENTITY:
      return "WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_IDENTITY";
    case WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT601:
      return "WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT601";
    case WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT709:
      return "WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT709";
    case WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT2020:
      return "WP_COLOR_REPRESENTATION_SURFACE_V1_COEFFICIENTS_BT2020";
    default:
      return "Unknow";
  }
}
#  endif
#endif

size_t BufferSurface::GetUsedMemory(int aWidth, int aHeight) const {
  switch (mFOURCCFormat) {
    case VA_FOURCC_P010:
    case VA_FOURCC_P016:
      // one plane 16b + two planes 16b (half sized).
      return aWidth * aHeight * 2 + (aWidth >> 1) * (aHeight >> 1) * 4;
    case VA_FOURCC_NV12:
    case VA_FOURCC_YV12:
    case VA_FOURCC_I420:
      // one plane 8b + two planes 8b (half sized).
      return aWidth * aHeight + (aWidth >> 1) * (aHeight >> 1) * 2;
    default:
      return aWidth * aHeight * GetFormatBPP();
  }
}

#ifdef MOZ_LOGGING
void BufferSurface::DumpToFile(const char* aFileName) {
  RefPtr<gfx::DataSourceSurface> surf = GetAsSourceSurface();
  gfxUtils::WriteAsPNG(surf, aFileName);
}

#  if 0
// A direct mapping version without GL.
// Does not work on AMD/NVIDIA
void DMABufSurfaceRGBA::DumpToFile(const char* pFileName) {
  uint32_t stride;

  if (!MapReadOnly(&stride)) {
    return;
  }
  cairo_surface_t* surface = nullptr;

  auto unmap = MakeScopeExit([&] {
    if (surface) {
      cairo_surface_destroy(surface);
    }
    Unmap();
  });

  surface = cairo_image_surface_create_for_data(
      (unsigned char*)mMappedRegion[0], CAIRO_FORMAT_ARGB32, mWidth, mHeight,
      stride);
  if (cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS) {
    cairo_surface_write_to_png(surface, pFileName);
  }
}
#  endif
#endif
