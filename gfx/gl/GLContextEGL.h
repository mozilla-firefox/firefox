/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GLCONTEXTEGL_H_
#define GLCONTEXTEGL_H_

#include <memory>

#include "GLContext.h"
#include "GLLibraryEGL.h"
#include "nsRegion.h"

namespace mozilla {
namespace layers {
class SurfaceTextureImage;
}  // namespace layers
namespace widget {
class CompositorWidget;
}  // namespace widget
namespace gl {

inline std::shared_ptr<EglDisplay> DefaultEglDisplay(
    nsACString* const out_failureId) {
  const auto lib = GLLibraryEGL::Get(out_failureId);
  if (!lib) {
    return nullptr;
  }
  return lib->DefaultDisplay(out_failureId);
}

inline std::shared_ptr<EglDisplay> CreateSoftwareEglDisplay(
    nsACString* const out_failureId) {
  const auto lib = GLLibraryEGL::Get(out_failureId);
  if (!lib) {
    return nullptr;
  }
  return lib->CreateDisplay(EGLCreateDisplayFlags{.mForceSoftware = true},
                            out_failureId);
}

inline std::shared_ptr<EglDisplay> CreateHighPowerEglDisplay(
    nsACString* const out_failureId) {
  const auto lib = GLLibraryEGL::Get(out_failureId);
  if (!lib) {
    return nullptr;
  }
  return lib->CreateDisplay(EGLCreateDisplayFlags{.mPreferHighPower = true},
                            out_failureId);
}

// -

class GLContextEGL final : public GLContext {
 public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(GLContextEGL, override)

  static RefPtr<GLContextEGL> CreateGLContext(
      std::shared_ptr<EglDisplay>, const GLContextDesc&,
      EGLConfig surfaceConfig, EGLSurface surface, const bool useGles,
      EGLConfig contextConfig, nsACString* const out_failureId);

 private:
  GLContextEGL(std::shared_ptr<EglDisplay>, const GLContextDesc&,
               EGLConfig surfaceConfig, EGLSurface surface, EGLContext context);
  ~GLContextEGL();

 public:
  virtual GLContextType GetContextType() const override {
    return GLContextType::EGL;
  }

  static GLContextEGL* Cast(GLContext* gl) {
    MOZ_RELEASE_ASSERT(gl->GetContextType() == GLContextType::EGL);
    return static_cast<GLContextEGL*>(gl);
  }

  bool Init() override;

  virtual bool IsDoubleBuffered() const override { return mIsDoubleBuffered; }

  void SetIsDoubleBuffered(bool aIsDB) { mIsDoubleBuffered = aIsDB; }

  virtual bool IsANGLE() const override { return mEgl->mLib->IsANGLE(); }
  virtual bool IsWARP() const override { return mEgl->mIsWARP; }
  virtual bool IsD3DANGLE() const override {
    return mEgl->IsExtensionSupported(EGLDeviceExtension::ANGLE_device_d3d);
  }
  virtual bool IsMetalANGLE() const override {
    return mEgl->IsExtensionSupported(EGLDeviceExtension::ANGLE_device_metal);
  }

  virtual bool BindTexImage() override;

  virtual bool ReleaseTexImage() override;

  void SetEGLSurfaceOverride(EGLSurface surf);
  EGLSurface GetEGLSurfaceOverride() { return mSurfaceOverride; }

  virtual bool MakeCurrentImpl() const override;

  virtual bool IsCurrentImpl() const override;

  virtual bool RenewSurface(widget::CompositorWidget* aWidget) override;

  virtual void ReleaseSurface() override;

  Maybe<SymbolLoader> GetSymbolLoader() const override;

  virtual bool SwapBuffers() override;

  virtual void SetDamage(const nsIntRegion& aDamageRegion) override;

  GLint GetBufferAge() const override;

  virtual void GetWSIInfo(nsCString* const out) const override;

  EGLSurface GetEGLSurface() const { return mSurface; }

  bool HasExtBufferAge() const;
  bool HasKhrPartialUpdate() const;
  // Returns which EGL texture target (e.g. EGL_TEXTURE_2D) should be used for
  // binding MacIOSurface PBuffers on ANGLE. Requires the extension
  // ANGLE_iosurface_client_buffer.
  EGLint GetBindToTextureTargetANGLE() const;
  // Same as above, but returns the corresponding GL texture target, e.g.
  // GL_TEXTURE_2D.
  GLenum GetPreferredMacIOSurfaceTextureTarget() const override;

  bool BindTex2DOffscreen(GLContext* aOffscreen);
  void UnbindTex2DOffscreen(GLContext* aOffscreen);
  void BindOffscreenFramebuffer();

  void Destroy();

  static RefPtr<GLContextEGL> CreateWithoutSurface(
      std::shared_ptr<EglDisplay>, const GLContextCreateDesc&,
      nsACString* const out_FailureId);
  static RefPtr<GLContextEGL> CreateEGLSurfacelessContext(
      const std::shared_ptr<EglDisplay> display,
      const GLContextCreateDesc& desc, nsACString* const out_failureId);

  EGLSurface CreateEGLSurfaceForCompositorWidget(
      widget::CompositorWidget* aCompositorWidget, const EGLConfig aConfig);

  static void DestroySurface(EglDisplay&, const EGLSurface aSurface);

#ifdef MOZ_X11
  static bool FindVisual(int* const out_visualId);
#endif

 protected:
  friend class GLContextProviderEGL;
  friend class GLContextEGLFactory;

  virtual void OnMarkDestroyed() override;

 public:
  const std::shared_ptr<EglDisplay> mEgl;
  const EGLConfig mSurfaceConfig;
  const EGLContext mContext;

 protected:
  EGLSurface mSurface;
  const EGLSurface mFallbackSurface;

  EGLSurface mSurfaceOverride = EGL_NO_SURFACE;
  bool mBound = false;

  bool mIsPBuffer = false;
  bool mIsDoubleBuffered = false;
  bool mCanBindToTexture = false;
  bool mShareWithEGLImage = false;
  bool mOwnsContext = true;
  mutable Maybe<EGLint> mBindToTextureTargetANGLE;

  nsIntRegion mDamageRegion;

  static EGLSurface CreatePBufferSurfaceTryingPowerOfTwo(
      EglDisplay&, EGLConfig, EGLenum bindToTextureFormat,
      gfx::IntSize& pbsize);

#ifdef MOZ_WAYLAND
  static EGLSurface CreateWaylandOffscreenSurface(EglDisplay&, EGLConfig,
                                                  gfx::IntSize& pbsize);
#endif

 public:
  EGLSurface CreateCompatibleSurface(void* aWindow) const;
};

bool CreateConfig(EglDisplay&, EGLConfig* aConfig, int32_t aDepth,
                  bool aEnableDepthBuffer, bool aUseGles,
                  bool aAllowFallback = true);

}  // namespace gl
}  // namespace mozilla

#endif  // GLCONTEXTEGL_H_
