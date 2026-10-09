/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RenderTextureHostSWGL.h"

#include "mozilla/gfx/Logging.h"
#include "mozilla/layers/TextureHost.h"
#include "RenderThread.h"

namespace mozilla {
namespace wr {

bool RenderTextureHostSWGL::UpdatePlanes(RenderCompositor* aCompositor) {
  wr_swgl_make_current(mContext);
  size_t planeCount = GetPlaneCount();
  bool texInit = false;
  if (mPlanes.size() < planeCount) {
    mPlanes.reserve(planeCount);
    while (mPlanes.size() < planeCount) {
      mPlanes.push_back(PlaneInfo(wr_swgl_gen_texture(mContext)));
    }
    texInit = true;
  }
  gfx::SurfaceFormat format = GetFormat();
  gfx::ColorDepth colorDepth = GetColorDepth();
  for (size_t i = 0; i < planeCount; i++) {
    PlaneInfo& plane = mPlanes[i];
    if (!MapPlane(aCompositor, i, plane)) {
      if (i > 0) {
        UnmapPlanes();
      }
      return false;
    }
    GLenum internalFormat = 0;
    switch (format) {
      case gfx::SurfaceFormat::B8G8R8A8:
      case gfx::SurfaceFormat::B8G8R8X8:
        MOZ_ASSERT(colorDepth == gfx::ColorDepth::COLOR_8);
        internalFormat = LOCAL_GL_RGBA8;
        break;
      case gfx::SurfaceFormat::YUV420:
        switch (colorDepth) {
          case gfx::ColorDepth::COLOR_8:
            internalFormat = LOCAL_GL_R8;
            break;
          case gfx::ColorDepth::COLOR_10:
          case gfx::ColorDepth::COLOR_12:
          case gfx::ColorDepth::COLOR_16:
            internalFormat = LOCAL_GL_R16;
            break;
        }
        break;
      case gfx::SurfaceFormat::NV12:
        switch (colorDepth) {
          case gfx::ColorDepth::COLOR_8:
            internalFormat = i > 0 ? LOCAL_GL_RG8 : LOCAL_GL_R8;
            break;
          case gfx::ColorDepth::COLOR_10:
          case gfx::ColorDepth::COLOR_12:
          case gfx::ColorDepth::COLOR_16:
            internalFormat = i > 0 ? LOCAL_GL_RG16 : LOCAL_GL_R16;
            break;
        }
        break;
      case gfx::SurfaceFormat::P010:
        MOZ_ASSERT(colorDepth == gfx::ColorDepth::COLOR_10);
        internalFormat = i > 0 ? LOCAL_GL_RG16 : LOCAL_GL_R16;
        break;
      case gfx::SurfaceFormat::P016:
        MOZ_ASSERT(colorDepth == gfx::ColorDepth::COLOR_16);
        internalFormat = i > 0 ? LOCAL_GL_RG16 : LOCAL_GL_R16;
        break;
      case gfx::SurfaceFormat::YUY2:
        MOZ_ASSERT(colorDepth == gfx::ColorDepth::COLOR_8);
        internalFormat = LOCAL_GL_RGB_RAW_422_APPLE;
        break;
      default:
        MOZ_RELEASE_ASSERT(false, "Unhandled external image format");
        break;
    }
    wr_swgl_set_texture_buffer(mContext, plane.mTexture, internalFormat,
                               plane.mSize.width, plane.mSize.height,
                               plane.mStride, plane.mData, 0, 0);
  }
  if (texInit) {
    // Initialize the mip filters to linear by default.
    for (const auto& plane : mPlanes) {
      wr_swgl_set_texture_parameter(mContext, plane.mTexture,
                                    LOCAL_GL_TEXTURE_MIN_FILTER,
                                    LOCAL_GL_LINEAR);
      wr_swgl_set_texture_parameter(mContext, plane.mTexture,
                                    LOCAL_GL_TEXTURE_MAG_FILTER,
                                    LOCAL_GL_LINEAR);
    }
  }
  return true;
}

bool RenderTextureHostSWGL::SetContext(void* aContext) {
  if (mContext != aContext) {
    CleanupPlanes();
    mContext = aContext;
    wr_swgl_reference_context(mContext);
  }
  return mContext != nullptr;
}

wr::WrExternalImage RenderTextureHostSWGL::LockSWGL(
    uint8_t aChannelIndex, void* aContext, RenderCompositor* aCompositor) {
  if (!SetContext(aContext)) {
    return InvalidToWrExternalImage();
  }
  if (!HasLockedSWGL()) {
    if (!UpdatePlanes(aCompositor)) {
      return InvalidToWrExternalImage();
    }
    mLockedSWGL = true;
  } else if (!mLockedSWGL) {
    return InvalidToWrExternalImage();
  }
  if (aChannelIndex >= mPlanes.size()) {
    return InvalidToWrExternalImage();
  }
  const PlaneInfo& plane = mPlanes[aChannelIndex];

  // Prefer native textures, unless our backend forbids it.
  layers::TextureHost::NativeTexturePolicy policy =
      layers::TextureHost::BackendNativeTexturePolicy(
          layers::WebRenderBackend::SOFTWARE, plane.mSize);
  return policy == layers::TextureHost::NativeTexturePolicy::FORBID
             ? RawDataToWrExternalImage((uint8_t*)plane.mData,
                                        plane.mStride * plane.mSize.height)
             : NativeTextureToWrExternalImage(
                   plane.mTexture, 0.0, 0.0,
                   static_cast<float>(plane.mSize.width),
                   static_cast<float>(plane.mSize.height));
}

void RenderTextureHostSWGL::UnlockSWGL() {
  if (mLockedSWGL) {
    mLockedSWGL = false;
    UnmapPlanes();
  }
}

void RenderTextureHostSWGL::CleanupPlanes() {
  if (!mContext) {
    return;
  }
  if (!mPlanes.empty()) {
    wr_swgl_make_current(mContext);
    for (const auto& plane : mPlanes) {
      wr_swgl_delete_texture(mContext, plane.mTexture);
    }
    mPlanes.clear();
  }
  wr_swgl_destroy_context(mContext);
  mContext = nullptr;
}

RenderTextureHostSWGL::~RenderTextureHostSWGL() { CleanupPlanes(); }

bool RenderTextureHostSWGL::LockSWGLCompositeSurface(
    void* aContext, wr::SWGLCompositeSurfaceInfo* aInfo) {
  if (!SetContext(aContext)) {
    return false;
  }
  if (!HasLockedSWGL()) {
    if (!UpdatePlanes(nullptr)) {
      return false;
    }
    mLockedSWGLCompositeSurface = true;
  } else if (!mLockedSWGLCompositeSurface) {
    return false;
  }
  MOZ_ASSERT(mPlanes.size() <= 3);
  for (size_t i = 0; i < mPlanes.size(); i++) {
    aInfo->textures[i] = mPlanes[i].mTexture;
  }
  switch (GetFormat()) {
    case gfx::SurfaceFormat::YUV420:
    case gfx::SurfaceFormat::NV12:
    case gfx::SurfaceFormat::P010:
    case gfx::SurfaceFormat::P016:
    case gfx::SurfaceFormat::YUY2: {
      aInfo->yuv_planes = mPlanes.size();
      auto colorSpace = GetYUVColorSpace();
      aInfo->color_space = ToWrYuvRangedColorSpace(colorSpace);
      auto colorDepth = GetColorDepth();
      aInfo->color_depth = ToWrColorDepth(colorDepth);
      break;
    }
    case gfx::SurfaceFormat::B8G8R8A8:
    case gfx::SurfaceFormat::B8G8R8X8:
      break;
    default:
      gfxCriticalNote << "Unhandled external image format: " << GetFormat();
      MOZ_RELEASE_ASSERT(false, "Unhandled external image format");
      break;
  }
  aInfo->size.width = mPlanes[0].mSize.width;
  aInfo->size.height = mPlanes[0].mSize.height;
  return true;
}

void RenderTextureHostSWGL::UnlockSWGLCompositeSurface() {
  if (mLockedSWGLCompositeSurface) {
    mLockedSWGLCompositeSurface = false;
    UnmapPlanes();
  }
}

bool wr_swgl_lock_composite_surface(void* aContext, wr::ExternalImageId aId,
                                    wr::SWGLCompositeSurfaceInfo* aInfo) {
  RenderTextureHost* texture = RenderThread::Get()->GetRenderTexture(aId);
  if (!texture) {
    return false;
  }
  RenderTextureHostSWGL* swglTex = texture->AsRenderTextureHostSWGL();
  if (!swglTex) {
    return false;
  }
  return swglTex->LockSWGLCompositeSurface(aContext, aInfo);
}

void wr_swgl_unlock_composite_surface(void* aContext, wr::ExternalImageId aId) {
  RenderTextureHost* texture = RenderThread::Get()->GetRenderTexture(aId);
  if (!texture) {
    return;
  }
  RenderTextureHostSWGL* swglTex = texture->AsRenderTextureHostSWGL();
  if (!swglTex) {
    return;
  }
  swglTex->UnlockSWGLCompositeSurface();
}

}  // namespace wr
}  // namespace mozilla
