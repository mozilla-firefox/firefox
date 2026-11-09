/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RenderEGLImageTextureHost.h"

#include "GLContextEGL.h"
#include "GLLibraryEGL.h"
#include "GLReadTexImageHelper.h"
#include "OGLShaderConfig.h"
#include "mozilla/gfx/Logging.h"

namespace mozilla {
namespace wr {

RenderEGLImageTextureHost::RenderEGLImageTextureHost(EGLImage aImage,
                                                     EGLSync aSync,
                                                     gfx::IntSize aSize,
                                                     gfx::SurfaceFormat aFormat)
    : mImage(aImage),
      mSync(aSync),
      mSize(aSize),
      mFormat(aFormat),
      mTextureTarget(LOCAL_GL_TEXTURE_2D),
      mTextureHandle(0) {
  MOZ_COUNT_CTOR_INHERITED(RenderEGLImageTextureHost, RenderTextureHost);
}

RenderEGLImageTextureHost::~RenderEGLImageTextureHost() {
  MOZ_COUNT_DTOR_INHERITED(RenderEGLImageTextureHost, RenderTextureHost);
  DeleteTextureHandle();
}

wr::WrExternalImage RenderEGLImageTextureHost::Lock(uint8_t aChannelIndex,
                                                    gl::GLContext* aGL) {
  MOZ_ASSERT(aChannelIndex == 0);

  if (mGL.get() != aGL) {
    if (mGL) {
      // This should not happen. On android, SingletonGL is used.
      MOZ_ASSERT_UNREACHABLE("Unexpected GL context");
      return InvalidToWrExternalImage();
    }
    mGL = aGL;
  }

  if (!mImage || !mGL || !mGL->MakeCurrent()) {
    return InvalidToWrExternalImage();
  }

  if (!WaitSync() || !CreateTextureHandle()) {
    return InvalidToWrExternalImage();
  }

  return NativeTextureToWrExternalImage(mTextureHandle, 0.0, 0.0,
                                        static_cast<float>(mSize.width),
                                        static_cast<float>(mSize.height));
}

void RenderEGLImageTextureHost::Unlock() {}

RefPtr<layers::TextureSource> RenderEGLImageTextureHost::CreateTextureSource(
    layers::TextureSourceProvider* aProvider) {
  gl::GLContext* gl = aProvider->GetGLContext();
  if (mGL.get() != gl) {
    if (mGL) {
      // This should not happen. On android, SingletonGL is used.
      MOZ_ASSERT_UNREACHABLE("Unexpected GL context");
      return nullptr;
    }
    mGL = gl;
  }

  if (!gl->IsExtensionSupported(gl::GLContext::OES_EGL_image)) {
    gfxCriticalNoteOnce << "EGLImage texture source without OES_EGL_image";
    return nullptr;
  }

  if (!WaitSync()) {
    return nullptr;
  }

  return new layers::EGLImageTextureSource(
      aProvider, mImage, mFormat, gl->GetPreferredEGLImageTextureTarget(),
      LOCAL_GL_CLAMP_TO_EDGE, mSize);
}

gfx::SurfaceFormat RenderEGLImageTextureHost::GetFormat() const {
  MOZ_ASSERT(mFormat == gfx::SurfaceFormat::R8G8B8A8 ||
             mFormat == gfx::SurfaceFormat::R8G8B8X8);
  // SWGL does not support RGBA/RGBX so we must provide data in BGRA/BGRX
  // format. ReadTexImage() called by MapPlane() will ensure that data gets
  // converted correctly.
  if (mFormat == gfx::SurfaceFormat::R8G8B8A8) {
    return gfx::SurfaceFormat::B8G8R8A8;
  }

  if (mFormat == gfx::SurfaceFormat::R8G8B8X8) {
    return gfx::SurfaceFormat::B8G8R8X8;
  }

  gfxCriticalNoteOnce << "Unexpected color format of RenderEGLImageTextureHost";

  return gfx::SurfaceFormat::UNKNOWN;
}

bool RenderEGLImageTextureHost::MapPlane(RenderCompositor* aCompositor,
                                         uint8_t aChannelIndex,
                                         PlaneInfo& aPlaneInfo) {
  RefPtr<gfx::DataSourceSurface> readback = ReadTexImage();
  if (!readback) {
    return false;
  }

  gfx::DataSourceSurface::MappedSurface map;
  if (!readback->Map(gfx::DataSourceSurface::MapType::READ, &map)) {
    return false;
  }

  mReadback = readback;
  aPlaneInfo.mSize = mSize;
  aPlaneInfo.mStride = map.mStride;
  aPlaneInfo.mData = map.mData;
  return true;
}

void RenderEGLImageTextureHost::UnmapPlanes() {
  if (mReadback) {
    mReadback->Unmap();
    mReadback = nullptr;
  }
}

bool RenderEGLImageTextureHost::CreateTextureHandle() {
  if (mTextureHandle) {
    return true;
  }

  if (!mGL->IsExtensionSupported(gl::GLContext::OES_EGL_image)) {
    gfxCriticalNoteOnce << "EGLImage texture handle without OES_EGL_image";
    return false;
  }

  mTextureTarget = mGL->GetPreferredEGLImageTextureTarget();
  MOZ_ASSERT(mTextureTarget == LOCAL_GL_TEXTURE_2D ||
             mTextureTarget == LOCAL_GL_TEXTURE_EXTERNAL);

  mGL->fGenTextures(1, &mTextureHandle);
  ActivateBindAndTexParameteri(mGL, LOCAL_GL_TEXTURE0, mTextureTarget,
                               mTextureHandle);
  mGL->fEGLImageTargetTexture2D(mTextureTarget, mImage);
  return true;
}

void RenderEGLImageTextureHost::DeleteTextureHandle() {
  if (mTextureHandle) {
    if (mGL && mGL->MakeCurrent()) {
      // XXX recycle gl texture, since SharedSurface_EGLImage and
      // RenderEGLImageTextureHost is not recycled.
      mGL->fDeleteTextures(1, &mTextureHandle);
    }
    mTextureHandle = 0;
  }
}

bool RenderEGLImageTextureHost::WaitSync() {
  bool syncSucceeded = true;
  if (mSync) {
    const auto& gle = gl::GLContextEGL::Cast(mGL);
    const auto& egl = gle->mEgl;
    MOZ_ASSERT(egl->IsExtensionSupported(gl::EGLExtension::KHR_fence_sync));
    if (egl->IsExtensionSupported(gl::EGLExtension::KHR_wait_sync)) {
      syncSucceeded = egl->fWaitSync(mSync, 0) == LOCAL_EGL_TRUE;
    } else {
      syncSucceeded = egl->fClientWaitSync(mSync, 0, LOCAL_EGL_FOREVER) ==
                      LOCAL_EGL_CONDITION_SATISFIED;
    }
    // We do not need to delete sync here. It is deleted by
    // SharedSurface_EGLImage.
    mSync = nullptr;
  }

  MOZ_ASSERT(
      syncSucceeded,
      "(Client)WaitSync generated an error. Has mSync already been destroyed?");
  return syncSucceeded;
}

already_AddRefed<gfx::DataSourceSurface>
RenderEGLImageTextureHost::ReadTexImage() {
  if (!mGL) {
    mGL = RenderThread::Get()->SingletonGL();
    if (!mGL) {
      return nullptr;
    }
  }

  if (!WaitSync() || !CreateTextureHandle()) {
    return nullptr;
  }

  // Allocate resulting image surface.
  // Use GetFormat() rather than mFormat for the DataSourceSurface. eg BGRA
  // rather than RGBA, as the latter is not supported by swgl.
  // ReadTexImageHelper will take care of converting the data for us.
  const gfx::SurfaceFormat surfFormat = GetFormat();
  int32_t stride = mSize.width * BytesPerPixel(surfFormat);
  RefPtr<gfx::DataSourceSurface> surf =
      gfx::Factory::CreateDataSourceSurfaceWithStride(mSize, surfFormat,
                                                      stride);
  if (!surf) {
    return nullptr;
  }

  layers::ShaderConfigOGL config =
      layers::ShaderConfigFromTargetAndFormat(mTextureTarget, mFormat);
  int shaderConfig = config.mFeatures;

  bool ret = mGL->ReadTexImageHelper()->ReadTexImage(
      surf, mTextureHandle, mTextureTarget, mSize, gfx::Matrix4x4(),
      shaderConfig, /* aYInvert */ false);
  if (!ret) {
    return nullptr;
  }

  return surf.forget();
}

}  // namespace wr
}  // namespace mozilla
