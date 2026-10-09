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
#include "mozilla/layers/LayersSurfaces.h"  // for SurfaceDescriptor, etc
#include "SharedSurface.h"

#if defined(MOZ_WIDGET_ANDROID)
#  include "AndroidNativeWindow.h"
#  include "mozilla/java/SurfaceAllocatorWrappers.h"
#  include "mozilla/java/GeckoSurfaceTextureWrappers.h"
#endif  // defined(MOZ_WIDGET_ANDROID)

namespace mozilla {
namespace gl {

static bool HasEglImageExtensions(const GLContextEGL& gl) {
  const auto& egl = *(gl.mEgl);
  return egl.HasKHRImageBase() &&
         egl.IsExtensionSupported(EGLExtension::KHR_gl_texture_2D_image) &&
         (gl.IsExtensionSupported(GLContext::OES_EGL_image_external) ||
          gl.IsExtensionSupported(GLContext::OES_EGL_image));
}

/*static*/
UniquePtr<SurfaceFactory_EGLImage> SurfaceFactory_EGLImage::Create(
    GLContext& gl_) {
  auto& gl = *GLContextEGL::Cast(&gl_);
  if (!HasEglImageExtensions(gl)) return nullptr;

  const auto partialDesc = PartialSharedSurfaceDesc{
      &gl, SharedSurfaceType::EGLImageShare, layers::TextureType::EGLImage,
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

  auto fb = MozFramebuffer::Create(desc.gl, desc.size, 0, false);
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

    mSync = egl->fCreateSync(LOCAL_EGL_SYNC_FENCE, nullptr);
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

////////////////////////////////////////////////////////////////////////

#ifdef MOZ_WIDGET_ANDROID

/*static*/
UniquePtr<SharedSurface_SurfaceTexture> SharedSurface_SurfaceTexture::Create(
    const SharedSurfaceDesc& desc) {
  const auto& size = desc.size;

  jni::Object::LocalRef surfaceObj;
  const bool useSingleBuffer =
      desc.gl->Renderer() != GLRenderer::AndroidEmulator;

  if (useSingleBuffer) {
    surfaceObj =
        java::SurfaceAllocator::AcquireSurface(size.width, size.height, true);
  }

  if (!surfaceObj) {
    // Try multi-buffer mode
    surfaceObj =
        java::SurfaceAllocator::AcquireSurface(size.width, size.height, false);
  }

  if (!surfaceObj) {
    // Give up
    NS_WARNING("Failed to allocate SurfaceTexture!");
    return nullptr;
  }
  const auto surface = java::GeckoSurface::Ref::From(surfaceObj);

  AndroidNativeWindow window(surface);
  const auto& gle = GLContextEGL::Cast(desc.gl);
  MOZ_ASSERT(gle);
  const auto eglSurface = gle->CreateCompatibleSurface(window.NativeWindow());
  if (!eglSurface) return nullptr;

  return AsUnique(new SharedSurface_SurfaceTexture(desc, surface, eglSurface));
}

SharedSurface_SurfaceTexture::SharedSurface_SurfaceTexture(
    const SharedSurfaceDesc& desc, java::GeckoSurface::Param surface,
    const EGLSurface eglSurface)
    : SharedSurface(desc, nullptr),
      mSurface(surface),
      mEglSurface(eglSurface),
      mEglDisplay(GLContextEGL::Cast(desc.gl)->mEgl) {}

SharedSurface_SurfaceTexture::~SharedSurface_SurfaceTexture() {
  if (mOrigEglSurface) {
    // We are about to destroy mEglSurface.
    // Make sure gl->SetEGLSurfaceOverride() doesn't keep a reference
    // to the surface.
    UnlockProd();
  }

  std::shared_ptr<EglDisplay> display = mEglDisplay.lock();
  if (display) {
    display->fDestroySurface(mEglSurface);
  }
  java::SurfaceAllocator::DisposeSurface(mSurface);
}

void SharedSurface_SurfaceTexture::LockProdImpl() {
  MOZ_RELEASE_ASSERT(mSurface->GetAvailable());

  GLContextEGL* gl = GLContextEGL::Cast(mDesc.gl);
  mOrigEglSurface = gl->GetEGLSurfaceOverride();
  gl->SetEGLSurfaceOverride(mEglSurface);
}

void SharedSurface_SurfaceTexture::UnlockProdImpl() {
  MOZ_RELEASE_ASSERT(mSurface->GetAvailable());

  GLContextEGL* gl = GLContextEGL::Cast(mDesc.gl);
  MOZ_ASSERT(gl->GetEGLSurfaceOverride() == mEglSurface);

  gl->SetEGLSurfaceOverride(mOrigEglSurface);
  mOrigEglSurface = nullptr;
}

void SharedSurface_SurfaceTexture::ProducerReadReleaseImpl() {
  // This GeckoSurfaceTexture is not SurfaceTexture of this class's GeckoSurface
  // when current process is content process. In this case, SurfaceTexture of
  // this class's GeckoSurface does not exist in this process. It exists in
  // compositor's process. Then GeckoSurfaceTexture in this process is a sync
  // surface that copies back the SurfaceTextrure from compositor's process. It
  // was added by Bug 1486659. Then SurfaceTexture::UpdateTexImage() becomes
  // very heavy weight, since it does copy back the SurfaceTextrure from
  // compositor's process.
  java::GeckoSurfaceTexture::LocalRef surfaceTexture =
      java::GeckoSurfaceTexture::Lookup(mSurface->GetHandle());
  if (!surfaceTexture) {
    NS_ERROR("Didn't find GeckoSurfaceTexture in ProducerReadReleaseImpl");
    return;
  }
  surfaceTexture->UpdateTexImage();
  // Non single buffer mode Surface does not need ReleaseTexImage() call.
  // When SurfaceTexture is sync Surface, it might not be single buffer mode.
  if (surfaceTexture->IsSingleBuffer()) {
    surfaceTexture->ReleaseTexImage();
  }
}

void SharedSurface_SurfaceTexture::Commit() {
  MOZ_RELEASE_ASSERT(mSurface->GetAvailable());

  LockProdImpl();
  mDesc.gl->SwapBuffers();
  UnlockProdImpl();
  mSurface->SetAvailable(false);
}

void SharedSurface_SurfaceTexture::WaitForBufferOwnership() {
  mSurface->SetAvailable(true);
}

bool SharedSurface_SurfaceTexture::IsBufferAvailable() const {
  return mSurface->GetAvailable();
}

bool SharedSurface_SurfaceTexture::IsValid() const {
  return SharedSurface::IsValid() && !mSurface->IsReleased();
}

Maybe<layers::SurfaceDescriptor>
SharedSurface_SurfaceTexture::ToSurfaceDescriptor() {
  return Some(layers::SurfaceTextureDescriptor(
      mSurface->GetHandle(), mDesc.size, gfx::SurfaceFormat::R8G8B8A8,
      false /* Do NOT override colorspace */, false /* NOT continuous */,
      Nothing() /* Do not override transform */));
}

SurfaceFactory_SurfaceTexture::SurfaceFactory_SurfaceTexture(GLContext& gl)
    : SurfaceFactory({&gl, SharedSurfaceType::AndroidSurfaceTexture,
                      layers::TextureType::AndroidNativeWindow, true}) {}

#endif  // MOZ_WIDGET_ANDROID

}  // namespace gl

} /* namespace mozilla */
