/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedSurfaceEGL.h"

#include "GLBlitHelper.h"
#include "GLContextEGL.h"
#include "GLContextProvider.h"
#include "GLLibraryEGL.h"
#include "GLReadTexImageHelper.h"
#include "MozFramebuffer.h"
#include "SharedSurface.h"
#include "mozilla/layers/LayersSurfaces.h"  // for SurfaceDescriptor, etc

namespace mozilla {
namespace gl {

static bool HasEglImageExtensions(const GLContextEGL& gl) {
  const auto& egl = *(gl.mEgl);
  return egl.HasKHRImageBase() &&
         egl.IsExtensionSupported(EGLExtension::KHR_gl_texture_2D_image) &&
         gl.IsExtensionSupported(GLContext::OES_EGL_image);
}

/*static*/
UniquePtr<SurfaceFactory_EGLImage> SurfaceFactory_EGLImage::Create(
    GLContext& gl_) {
  auto& gl = *GLContextEGL::Cast(&gl_);
  if (!HasEglImageExtensions(gl)) return nullptr;

  const auto partialDesc = PartialSharedSurfaceDesc{
      &gl,
      SharedSurfaceType::EGLImageShare,
      layers::TextureType::EGLImage,
      false,  // Can't recycle, as mSync changes never update TextureHost.
  };
  return AsUnique(new SurfaceFactory_EGLImage(partialDesc));
}

// -

/*static*/
UniquePtr<SharedSurface_EGLImage> SharedSurface_EGLImage::Create(
    const SharedSurfaceDesc& desc) {
  const auto& gle = GLContextEGL::Cast(desc.gl);
  const auto& context = gle->mContext;
  const auto& egl = *(gle->mEgl);

  auto fb = MozFramebuffer::Create(desc.gl, desc.size, 0, false, false);
  if (!fb) return nullptr;

  const auto buffer = reinterpret_cast<EGLClientBuffer>(fb->ColorTex());
  const auto image =
      egl.fCreateImage(context, LOCAL_EGL_GL_TEXTURE_2D, buffer, nullptr);
  if (!image) return nullptr;

  return AsUnique(new SharedSurface_EGLImage(desc, std::move(fb), image));
}

SharedSurface_EGLImage::SharedSurface_EGLImage(const SharedSurfaceDesc& desc,
                                               UniquePtr<MozFramebuffer>&& fb,
                                               const EGLImage image)
    : SharedSurface(desc, std::move(fb)),
      mMutex("SharedSurface_EGLImage mutex"),
      mEglDisplay(GLContextEGL::Cast(desc.gl)->mEgl),
      mImage(image) {}

SharedSurface_EGLImage::~SharedSurface_EGLImage() {
  if (auto display = mEglDisplay.lock()) {
    display->fDestroyImage(mImage);

    if (mSync) {
      // We can't call this unless we have the ext, but we will always have
      // the ext if we have something to destroy.
      display->fDestroySync(mSync);
    }
  }
}

void SharedSurface_EGLImage::ProducerReleaseImpl() {
  const auto& gl = GLContextEGL::Cast(mDesc.gl);
  const auto& egl = gl->mEgl;

  MutexAutoLock lock(mMutex);
  gl->MakeCurrent();

  if (egl->IsExtensionSupported(EGLExtension::KHR_fence_sync) &&
      gl->IsExtensionSupported(GLContext::OES_EGL_sync)) {
    if (mSync) {
      MOZ_ALWAYS_TRUE(egl->fDestroySync(mSync));
      mSync = nullptr;
    }

    mSync = egl->fCreateSyncKHR(LOCAL_EGL_SYNC_FENCE, nullptr);
    if (mSync) {
      gl->fFlush();
      return;
    }
  }

  MOZ_ASSERT(!mSync);
  gl->fFinish();
}

void SharedSurface_EGLImage::ProducerReadAcquireImpl() {
  const auto& gle = GLContextEGL::Cast(mDesc.gl);
  const auto& egl = gle->mEgl;
  // Wait on the fence, because presumably we're going to want to read this
  // surface
  if (mSync) {
    egl->fClientWaitSync(mSync, 0, LOCAL_EGL_FOREVER);
  }
}

Maybe<layers::SurfaceDescriptor> SharedSurface_EGLImage::ToSurfaceDescriptor() {
  return Some(layers::EGLImageDescriptor((uintptr_t)mImage, (uintptr_t)mSync,
                                         mDesc.size, true));
}

}  // namespace gl

} /* namespace mozilla */
