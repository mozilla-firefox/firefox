/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GLCONTEXT_H_
#define GLCONTEXT_H_

#include <stdint.h>

#include <bitset>
#include <stack>
#include <vector>

#ifdef DEBUG
#  include <string.h>
#endif

#ifdef GetClassName
#  undef GetClassName
#endif

// Define MOZ_GL_DEBUG_BUILD unconditionally to enable GL debugging in opt
// builds.
#ifdef DEBUG
#  define MOZ_GL_DEBUG_BUILD 1
#endif

#include "GLConsts.h"
#include "GLContextSymbols.h"
#include "GLContextTypes.h"
#include "GLDefs.h"
#include "GLTypes.h"
#include "MozFramebuffer.h"
#include "base/platform_thread.h"  // for PlatformThreadId
#include "mozilla/GenericRefCounted.h"
#include "mozilla/IntegerRange.h"
#include "mozilla/RefPtr.h"
#include "mozilla/ThreadLocal.h"
#include "mozilla/UniquePtr.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/gfx/Point.h"
#include "nsRegionFwd.h"
#include "nsString.h"
#include "nsTArray.h"

template <class ElemT, class... More>
constexpr inline std::array<ElemT, 1 + sizeof...(More)> make_array(
    ElemT&& arg1, More&&... more) {
  return {std::forward<ElemT>(arg1), std::forward<ElemT>(more)...};
}

#ifdef MOZ_WIDGET_ANDROID
#  include "mozilla/ProfilerLabels.h"
#endif

namespace mozilla {

namespace gl {
class GLBlitHelper;
class GLLibraryEGL;
class GLReadTexImageHelper;
class SharedSurface;
class SymbolLoader;
struct SymLoadStruct;
}  // namespace gl

namespace layers {
class ColorTextureLayerProgram;
}  // namespace layers

namespace widget {
class CompositorWidget;
}  // namespace widget
}  // namespace mozilla

namespace mozilla {
namespace gl {

enum class GLFeature {
  bind_buffer_offset,
  blend_minmax,
  clear_buffers,
  clip_control,
  copy_buffer,
  copy_image,
  debug,
  depth_clamp,
  depth_texture,
  draw_buffers,
  draw_buffers_indexed,
  draw_instanced,
  element_index_uint,
  ES2_compatibility,
  ES3_compatibility,
  EXT_color_buffer_float,
  frag_color_float,
  frag_depth,
  framebuffer_blit,
  framebuffer_multisample,
  framebuffer_object,
  framebuffer_object_EXT_OES,
  get_integer_indexed,
  get_integer64_indexed,
  get_query_object_i64v,
  get_query_object_iv,
  gpu_shader4,
  instanced_arrays,
  instanced_non_arrays,
  internalformat_query,
  invalidate_framebuffer,
  map_buffer_range,
  multiview,
  occlusion_query,
  occlusion_query_boolean,
  occlusion_query2,
  packed_depth_stencil,
  polygon_offset_clamp,
  prim_restart,
  prim_restart_fixed,
  provoking_vertex,
  query_counter,
  query_objects,
  query_time_elapsed,
  read_buffer,
  renderbuffer_color_float,
  renderbuffer_color_half_float,
  robust_buffer_access_behavior,
  robustness,
  sRGB,
  sampler_objects,
  seamless_cube_map_opt_in,
  shader_texture_lod,
  split_framebuffer,
  standard_derivatives,
  sync,
  texture_3D,
  texture_3D_compressed,
  texture_3D_copy,
  texture_compression_bptc,
  texture_compression_rgtc,
  texture_float,
  texture_float_linear,
  texture_half_float,
  texture_half_float_linear,
  texture_mirror_clamp_to_edge,
  texture_non_power_of_two,
  texture_norm16,
  texture_rg,
  texture_storage,
  texture_swizzle,
  transform_feedback2,
  uniform_buffer_object,
  uniform_matrix_nonsquare,
  vertex_array_object,
  EnumMax
};

enum class ContextProfile : uint8_t {
  Unknown = 0,
  OpenGLCore,
  OpenGLCompatibility,
  OpenGLES
};

enum class GLRenderer {
  Adreno200,
  Adreno205,
  AdrenoTM200,
  AdrenoTM205,
  AdrenoTM305,
  AdrenoTM320,
  AdrenoTM330,
  AdrenoTM420,
  Mali400MP,
  Mali450MP,
  MaliT,
  SGX530,
  SGX540,
  SGX544MP,
  Tegra,
  AndroidEmulator,
  GalliumLlvmpipe,
  MicrosoftBasicRenderDriver,
  SamsungXclipse,
  Other
};

class GLContext : public GenericAtomicRefCounted, public SupportsWeakPtr {
 public:
  static MOZ_THREAD_LOCAL(const GLContext*) sCurrentContext;

  static void InvalidateCurrentContext();

  const GLContextDesc mDesc;

  bool mImplicitMakeCurrent = false;
  bool mUseTLSIsCurrent;

  static void ResetTLSCurrentContext();

  class TlsScope final {
    const WeakPtr<GLContext> mGL;
    const bool mWasTlsOk;

   public:
    explicit TlsScope(GLContext* const gl, bool invalidate = false)
        : mGL(gl), mWasTlsOk(gl && gl->mUseTLSIsCurrent) {
      if (mGL) {
        if (invalidate) {
          InvalidateCurrentContext();
        }
        mGL->mUseTLSIsCurrent = true;
      }
    }

    ~TlsScope() {
      if (mGL) {
        mGL->mUseTLSIsCurrent = mWasTlsOk;
      }
    }
  };

  // -----------------------------------------------------------------------------
  // basic getters
 public:
  /**
   * Returns true if the context is using ANGLE. This should only be overridden
   * for an ANGLE implementation.
   */
  virtual bool IsANGLE() const { return false; }

  /**
   * Returns true if the context is using WARP. This should only be overridden
   * for an ANGLE implementation.
   */
  virtual bool IsWARP() const { return false; }

  /**
   * Returns true if the context is using ANGLE's D3D backend. This should only
   * be overridden for an ANGLE implementation.
   */
  virtual bool IsD3DANGLE() const { return false; }

  /**
   * Returns true if the context is using ANGLE's Metal backend. This should
   * only be overridden for an ANGLE implementation.
   */
  virtual bool IsMetalANGLE() const { return false; }

  virtual void GetWSIInfo(nsCString* const out) const = 0;

  /**
   * Return true if we are running on a OpenGL core profile context
   */
  inline bool IsCoreProfile() const {
    MOZ_ASSERT(mProfile != ContextProfile::Unknown, "unknown context profile");

    return mProfile == ContextProfile::OpenGLCore;
  }

  /**
   * Return true if we are running on a OpenGL compatibility profile context
   * (legacy profile 2.1 on Max OS X)
   */
  inline bool IsCompatibilityProfile() const {
    MOZ_ASSERT(mProfile != ContextProfile::Unknown, "unknown context profile");

    return mProfile == ContextProfile::OpenGLCompatibility;
  }

  inline bool IsGLES() const {
    MOZ_ASSERT(mProfile != ContextProfile::Unknown, "unknown context profile");

    return mProfile == ContextProfile::OpenGLES;
  }

  inline bool IsAtLeast(ContextProfile profile, unsigned int version) const {
    MOZ_ASSERT(profile != ContextProfile::Unknown,
               "IsAtLeast: bad <profile> parameter");
    MOZ_ASSERT(mProfile != ContextProfile::Unknown, "unknown context profile");
    MOZ_ASSERT(mVersion != 0, "unknown context version");

    if (version > mVersion) {
      return false;
    }

    return profile == mProfile;
  }

  /**
   * Return the version of the context.
   * Example :
   *   If this a OpenGL 2.1, that will return 210
   */
  inline uint32_t Version() const { return mVersion; }

  inline uint32_t ShadingLanguageVersion() const {
    return mShadingLanguageVersion;
  }

  GLVendor Vendor() const { return mVendor; }
  GLRenderer Renderer() const { return mRenderer; }
  bool IsMesa() const { return mIsMesa; }

  const nsCString& VendorString() const { return mVendorString; }
  const nsCString& RendererString() const { return mRendererString; }
  const nsCString& VersionString() const { return mVersionString; }
  const nsTArray<nsCString>& ExtensionStrings() const {
    return mExtensionStrings;
  }

  bool IsContextLost() const { return mContextLost; }

  bool CheckContextLost() const {
    mTopError = GetError();
    return IsContextLost();
  }

  bool HasPBOState() const { return (!IsGLES() || Version() >= 300); }
  bool HasTexParamMipmapLevel() const { return HasPBOState(); }

  /**
   * If this context is double-buffered, returns TRUE.
   */
  virtual bool IsDoubleBuffered() const { return false; }

  virtual GLContextType GetContextType() const = 0;

  virtual bool IsCurrentImpl() const = 0;
  virtual bool MakeCurrentImpl() const = 0;

  bool IsCurrent() const {
    if (mImplicitMakeCurrent) return MakeCurrent();

    return IsCurrentImpl();
  }

  bool MakeCurrent(bool aForce = false) const;

  /**
   * Get the default framebuffer for this context.
   */
  UniquePtr<MozFramebuffer> mOffscreenDefaultFb;

  bool CreateOffscreenDefaultFb(const gfx::IntSize& size);

  virtual GLuint GetDefaultFramebuffer() {
    if (mOffscreenDefaultFb) {
      return mOffscreenDefaultFb->mFB;
    }
    return 0;
  }

  /**
   * mVersion store the OpenGL's version, multiplied by 100. For example, if
   * the context is an OpenGL 2.1 context, mVersion value will be 210.
   */
  uint32_t mVersion = 0;
  ContextProfile mProfile = ContextProfile::Unknown;

  uint32_t mShadingLanguageVersion = 0;

  GLVendor mVendor = GLVendor::Other;
  GLRenderer mRenderer = GLRenderer::Other;
  bool mIsMesa = false;

  nsCString mVendorString;
  nsCString mRendererString;
  nsCString mVersionString;
  nsTArray<nsCString> mExtensionStrings;

  // -----------------------------------------------------------------------------
  // Extensions management
  /**
   * This mechanism is designed to know if an extension is supported. In the
   * long term, we would like to only use the extension group queries XXX_* to
   * have full compatibility with context version and profiles (especialy the
   * core that officialy don't bring any extensions).
   */

  /**
   * Known GL extensions that can be queried by
   * IsExtensionSupported.  The results of this are cached, and as
   * such it's safe to use this even in performance critical code.
   * If you add to this array, remember to add to the string names
   * in GLContext.cpp.
   */
  enum GLExtensions {
    Extension_None = 0,
    AMD_compressed_ATC_texture,
    ANGLE_depth_texture,
    ANGLE_framebuffer_blit,
    ANGLE_framebuffer_multisample,
    ANGLE_instanced_arrays,
    ANGLE_multiview,
    ANGLE_provoking_vertex,
    ANGLE_texture_compression_dxt3,
    ANGLE_texture_compression_dxt5,
    ANGLE_timer_query,
    APPLE_client_storage,
    APPLE_fence,
    APPLE_framebuffer_multisample,
    APPLE_sync,
    APPLE_texture_range,
    APPLE_vertex_array_object,
    ARB_ES2_compatibility,
    ARB_ES3_compatibility,
    ARB_clip_control,
    ARB_color_buffer_float,
    ARB_compatibility,
    ARB_copy_buffer,
    ARB_copy_image,
    ARB_depth_clamp,
    ARB_depth_texture,
    ARB_draw_buffers,
    ARB_draw_instanced,
    ARB_framebuffer_object,
    ARB_framebuffer_sRGB,
    ARB_geometry_shader4,
    ARB_half_float_pixel,
    ARB_instanced_arrays,
    ARB_internalformat_query,
    ARB_invalidate_subdata,
    ARB_map_buffer_range,
    ARB_occlusion_query2,
    ARB_pixel_buffer_object,
    ARB_polygon_offset_clamp,
    ARB_provoking_vertex,
    ARB_robust_buffer_access_behavior,
    ARB_robustness,
    ARB_sampler_objects,
    ARB_seamless_cube_map,
    ARB_shader_texture_lod,
    ARB_sync,
    ARB_texture_compression,
    ARB_texture_compression_bptc,
    ARB_texture_compression_rgtc,
    ARB_texture_float,
    ARB_texture_mirror_clamp_to_edge,
    ARB_texture_non_power_of_two,
    ARB_texture_rectangle,
    ARB_texture_rg,
    ARB_texture_storage,
    ARB_texture_swizzle,
    ARB_timer_query,
    ARB_transform_feedback2,
    ARB_uniform_buffer_object,
    ARB_vertex_array_object,
    CHROMIUM_color_buffer_float_rgb,
    CHROMIUM_color_buffer_float_rgba,
    EXT_bgra,
    EXT_blend_minmax,
    EXT_clip_control,
    EXT_color_buffer_float,
    EXT_color_buffer_half_float,
    EXT_copy_texture,
    EXT_depth_clamp,
    EXT_disjoint_timer_query,
    EXT_draw_buffers,
    EXT_draw_buffers2,
    EXT_draw_instanced,
    EXT_float_blend,
    EXT_frag_depth,
    EXT_framebuffer_blit,
    EXT_framebuffer_multisample,
    EXT_framebuffer_object,
    EXT_framebuffer_sRGB,
    EXT_gpu_shader4,
    EXT_map_buffer_range,
    EXT_multisampled_render_to_texture,
    EXT_occlusion_query_boolean,
    EXT_packed_depth_stencil,
    EXT_polygon_offset_clamp,
    EXT_provoking_vertex,
    EXT_read_format_bgra,
    EXT_robustness,
    EXT_sRGB,
    EXT_sRGB_write_control,
    EXT_shader_texture_lod,
    EXT_texture_compression_bptc,
    EXT_texture_compression_dxt1,
    EXT_texture_compression_rgtc,
    EXT_texture_compression_s3tc,
    EXT_texture_compression_s3tc_srgb,
    EXT_texture_filter_anisotropic,
    EXT_texture_format_BGRA8888,
    EXT_texture_mirror_clamp_to_edge,
    EXT_texture_norm16,
    EXT_texture_sRGB,
    EXT_texture_storage,
    EXT_timer_query,
    EXT_transform_feedback,
    EXT_unpack_subimage,
    EXT_semaphore,
    EXT_semaphore_fd,
    EXT_memory_object,
    EXT_memory_object_fd,
    IMG_read_format,
    IMG_texture_compression_pvrtc,
    IMG_texture_npot,
    KHR_debug,
    KHR_parallel_shader_compile,
    KHR_robust_buffer_access_behavior,
    KHR_robustness,
    KHR_texture_compression_astc_hdr,
    KHR_texture_compression_astc_ldr,
    NV_draw_instanced,
    NV_fence,
    NV_framebuffer_blit,
    NV_geometry_program4,
    NV_half_float,
    NV_instanced_arrays,
    NV_primitive_restart,
    NV_texture_barrier,
    NV_transform_feedback,
    NV_transform_feedback2,
    OES_EGL_image,
    OES_EGL_image_external,
    OES_EGL_sync,
    OES_compressed_ETC1_RGB8_texture,
    OES_depth24,
    OES_depth32,
    OES_depth_texture,
    OES_draw_buffers_indexed,
    OES_element_index_uint,
    OES_fbo_render_mipmap,
    OES_framebuffer_object,
    OES_packed_depth_stencil,
    OES_rgb8_rgba8,
    OES_standard_derivatives,
    OES_stencil8,
    OES_texture_3D,
    OES_texture_float,
    OES_texture_float_linear,
    OES_texture_half_float,
    OES_texture_half_float_linear,
    OES_texture_npot,
    OES_vertex_array_object,
    OVR_multiview2,
    Extensions_Max,
    Extensions_End
  };

  bool IsExtensionSupported(GLExtensions aKnownExtension) const {
    return mAvailableExtensions[aKnownExtension];
  }

 protected:
  void MarkExtensionUnsupported(GLExtensions aKnownExtension) {
    mAvailableExtensions[aKnownExtension] = false;
  }

  void MarkExtensionSupported(GLExtensions aKnownExtension) {
    mAvailableExtensions[aKnownExtension] = true;
  }

  std::bitset<Extensions_Max> mAvailableExtensions;

  // -----------------------------------------------------------------------------
  // Feature queries
  /*
   * This mecahnism introduces a new way to check if a OpenGL feature is
   * supported, regardless of whether it is supported by an extension or
   * natively by the context version/profile
   */
 public:
  bool IsSupported(GLFeature feature) const {
    return mAvailableFeatures[size_t(feature)];
  }

  static const char* GetFeatureName(GLFeature feature);

 private:
  std::bitset<size_t(GLFeature::EnumMax)> mAvailableFeatures;

  /**
   * Init features regarding OpenGL extension and context version and profile
   */
  void InitFeatures();

  /**
   * Mark the feature and associated extensions as unsupported
   */
  void MarkUnsupported(GLFeature feature);

  /**
   * Is this feature supported using the core (unsuffixed) symbols?
   */
  bool IsFeatureProvidedByCoreSymbols(GLFeature feature);

  // -----------------------------------------------------------------------------
  // Error handling

 private:
  mutable bool mContextLost = false;
  mutable GLenum mTopError = 0;

 protected:
  void OnContextLostError() const;

 public:
  static std::string GLErrorToString(GLenum aError);

  static bool IsBadCallError(const GLenum err) {
    return !(err == 0 || err == LOCAL_GL_CONTEXT_LOST);
  }

  class LocalErrorScope;

 private:
  mutable std::stack<const LocalErrorScope*> mLocalErrorScopeStack;
  mutable UniquePtr<LocalErrorScope> mDebugErrorScope;

  ////////////////////////////////////
  // Use this safer option.

 public:
  class LocalErrorScope {
    const GLContext& mGL;
    GLenum mOldTop;
    bool mHasBeenChecked;

   public:
    explicit LocalErrorScope(const GLContext& gl)
        : mGL(gl), mHasBeenChecked(false) {
      mGL.mLocalErrorScopeStack.push(this);
      mOldTop = mGL.GetError();
    }

    /// Never returns CONTEXT_LOST.
    GLenum GetError() {
      MOZ_ASSERT(!mHasBeenChecked);
      mHasBeenChecked = true;

      const auto ret = mGL.GetError();
      if (ret == LOCAL_GL_CONTEXT_LOST) return 0;
      return ret;
    }

    ~LocalErrorScope() {
      MOZ_ASSERT(mHasBeenChecked);

      MOZ_ASSERT(!IsBadCallError(mGL.GetError()));

      MOZ_ASSERT(mGL.mLocalErrorScopeStack.top() == this);
      mGL.mLocalErrorScopeStack.pop();

      mGL.mTopError = mOldTop;
    }
  };

  // -

  bool GetPotentialInteger(GLenum pname, GLint* param) {
    LocalErrorScope localError(*this);

    fGetIntegerv(pname, param);

    GLenum err = localError.GetError();
    MOZ_ASSERT_IF(err != LOCAL_GL_NO_ERROR, err == LOCAL_GL_INVALID_ENUM);
    return err == LOCAL_GL_NO_ERROR;
  }

  void DebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity,
                     GLsizei length, const GLchar* message);

 private:
  static void GLAPIENTRY StaticDebugCallback(GLenum source, GLenum type,
                                             GLuint id, GLenum severity,
                                             GLsizei length,
                                             const GLchar* message,
                                             const GLvoid* userParam);

  // -----------------------------------------------------------------------------
  // Debugging implementation
 private:
#ifndef MOZ_FUNCTION_NAME
#  ifdef __GNUC__
#    define MOZ_FUNCTION_NAME __PRETTY_FUNCTION__
#  elif defined(_MSC_VER)
#    define MOZ_FUNCTION_NAME __FUNCTION__
#  else
#    define MOZ_FUNCTION_NAME \
      __func__  // defined in C99, supported in various C++ compilers. Just raw
                // function name.
#  endif
#endif

#ifdef MOZ_WIDGET_ANDROID
// Record the name of the GL call for better hang stacks on Android.
#  define ANDROID_ONLY_PROFILER_LABEL AUTO_PROFILER_LABEL(__func__, GRAPHICS);
#else
#  define ANDROID_ONLY_PROFILER_LABEL
#endif

#define BEFORE_GL_CALL                               \
  ANDROID_ONLY_PROFILER_LABEL                        \
  if (MOZ_LIKELY(BeforeGLCall(MOZ_FUNCTION_NAME))) { \
    do {                                             \
  } while (0)

#define AFTER_GL_CALL             \
  AfterGLCall(MOZ_FUNCTION_NAME); \
  }                               \
  do {                            \
  } while (0)

  void BeforeGLCall_Debug(const char* funcName) const;
  void AfterGLCall_Debug(const char* funcName) const;
  static void OnImplicitMakeCurrentFailure(const char* funcName);

  bool BeforeGLCall(const char* const funcName) const {
    if (mImplicitMakeCurrent) {
      if (MOZ_UNLIKELY(!MakeCurrent())) {
        if (!mContextLost) {
          OnImplicitMakeCurrentFailure(funcName);
        }
        return false;
      }
    }
    MOZ_GL_ASSERT(this, IsCurrentImpl());

    if (MOZ_UNLIKELY(mDebugFlags)) {
      BeforeGLCall_Debug(funcName);
    }
    return true;
  }

  void AfterGLCall(const char* const funcName) const {
    if (MOZ_UNLIKELY(mDebugFlags)) {
      AfterGLCall_Debug(funcName);
    }
  }

  GLContext* TrackingContext() {
    GLContext* tip = this;
    while (tip->mSharedContext) tip = tip->mSharedContext;
    return tip;
  }

  static void AssertNotPassingStackBufferToTheGL(const void* ptr);

#ifdef MOZ_GL_DEBUG_BUILD

#  define TRACKING_CONTEXT(a) \
    do {                      \
      TrackingContext()->a;   \
    } while (0)

#  define ASSERT_NOT_PASSING_STACK_BUFFER_TO_GL(ptr) \
    AssertNotPassingStackBufferToTheGL(ptr)

#  define ASSERT_SYMBOL_PRESENT(func)                                    \
    do {                                                                 \
      MOZ_ASSERT(strstr(MOZ_FUNCTION_NAME, #func) != nullptr,            \
                 "Mismatched symbol check.");                            \
      if (MOZ_UNLIKELY(!mSymbols.func)) {                                \
        printf_stderr("RUNTIME ASSERT: Uninitialized GL function: %s\n", \
                      #func);                                            \
        MOZ_CRASH("GFX: Uninitialized GL function");                     \
      }                                                                  \
    } while (0)

#else  // ifdef MOZ_GL_DEBUG_BUILD

#  define TRACKING_CONTEXT(a) \
    do {                      \
    } while (0)
#  define ASSERT_NOT_PASSING_STACK_BUFFER_TO_GL(ptr) \
    do {                                             \
    } while (0)
#  define ASSERT_SYMBOL_PRESENT(func) \
    do {                              \
    } while (0)

#endif  // ifdef MOZ_GL_DEBUG_BUILD

  // Do whatever setup is necessary to draw to our offscreen FBO, if it's
  // bound.
  void BeforeGLDrawCall() {}

  // Do whatever tear-down is necessary after drawing to our offscreen FBO,
  // if it's bound.
  void AfterGLDrawCall() { mHeavyGLCallsSinceLastFlush = true; }

  // Do whatever setup is necessary to read from our offscreen FBO, if it's
  // bound.
  void BeforeGLReadCall() {}

  // Do whatever tear-down is necessary after reading from our offscreen FBO,
  // if it's bound.
  void AfterGLReadCall() {}

 public:
  void OnSyncCall() const { mSyncGLCallCount++; }

  uint64_t GetSyncCallCount() const { return mSyncGLCallCount; }

  void ResetSyncCallCount(const char* resetReason) const;

  // -----------------------------------------------------------------------------
  // GL official entry points
 public:
  // We smash all errors together, so you never have to loop on this. We
  // guarantee that immediately after this call, there are no errors left.
  // Always returns the top-most error, except if followed by CONTEXT_LOST, then
  // return that instead.
  GLenum GetError() const;

  GLenum fGetError() { return GetError(); }

  GLenum fGetGraphicsResetStatus() const;

  // -

  void fActiveTexture(GLenum texture) {
    BEFORE_GL_CALL;
    mSymbols.fActiveTexture(texture);
    AFTER_GL_CALL;
  }

  void fAttachShader(GLuint program, GLuint shader) {
    BEFORE_GL_CALL;
    mSymbols.fAttachShader(program, shader);
    AFTER_GL_CALL;
  }

  void fBeginQuery(GLenum target, GLuint id) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fBeginQuery);
    mSymbols.fBeginQuery(target, id);
    AFTER_GL_CALL;
  }

  void fBindAttribLocation(GLuint program, GLuint index, const GLchar* name) {
    BEFORE_GL_CALL;
    mSymbols.fBindAttribLocation(program, index, name);
    AFTER_GL_CALL;
  }

  void fBindBuffer(GLenum target, GLuint buffer) {
    BEFORE_GL_CALL;
    mSymbols.fBindBuffer(target, buffer);
    AFTER_GL_CALL;
  }

  void InvalidateFramebuffer(GLenum target) {
#ifdef XP_IOS
    // LOCAL_GL_DEPTH_STENCIL_ATTACHMENT cannot be invalidated on iOS.
    constexpr auto ATTACHMENTS = make_array(GLenum{LOCAL_GL_COLOR_ATTACHMENT0});
#else
    constexpr auto ATTACHMENTS = make_array(GLenum{LOCAL_GL_COLOR_ATTACHMENT0},
                                            LOCAL_GL_DEPTH_STENCIL_ATTACHMENT);
#endif

    fInvalidateFramebuffer(target, ATTACHMENTS.size(), ATTACHMENTS.data());
  }

  void fInvalidateFramebuffer(GLenum target, GLsizei numAttachments,
                              const GLenum* attachments) {
    if (!mSymbols.fInvalidateFramebuffer) return;
    BeforeGLDrawCall();
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fInvalidateFramebuffer);
    mSymbols.fInvalidateFramebuffer(target, numAttachments, attachments);
    AFTER_GL_CALL;
    AfterGLDrawCall();
  }

  void fInvalidateSubFramebuffer(GLenum target, GLsizei numAttachments,
                                 const GLenum* attachments, GLint x, GLint y,
                                 GLsizei width, GLsizei height) {
    if (!mSymbols.fInvalidateSubFramebuffer) return;
    BeforeGLDrawCall();
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fInvalidateSubFramebuffer);
    mSymbols.fInvalidateSubFramebuffer(target, numAttachments, attachments, x,
                                       y, width, height);
    AFTER_GL_CALL;
    AfterGLDrawCall();
  }

  void fBindTexture(GLenum target, GLuint texture) {
    BEFORE_GL_CALL;
    mSymbols.fBindTexture(target, texture);
    AFTER_GL_CALL;
  }

  void BindSamplerTexture(GLuint texUnitId, GLuint samplerHandle,
                          GLenum texTarget, GLuint texHandle) {
    fBindSampler(texUnitId, samplerHandle);
    fActiveTexture(LOCAL_GL_TEXTURE0 + texUnitId);
    fBindTexture(texTarget, texHandle);
  }

  void fBlendColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    BEFORE_GL_CALL;
    mSymbols.fBlendColor(red, green, blue, alpha);
    AFTER_GL_CALL;
  }

  void fBlendEquation(GLenum mode) {
    BEFORE_GL_CALL;
    mSymbols.fBlendEquation(mode);
    AFTER_GL_CALL;
  }

  void fBlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha) {
    BEFORE_GL_CALL;
    mSymbols.fBlendEquationSeparate(modeRGB, modeAlpha);
    AFTER_GL_CALL;
  }

  void fBlendFunc(GLenum sfactor, GLenum dfactor) {
    BEFORE_GL_CALL;
    mSymbols.fBlendFunc(sfactor, dfactor);
    AFTER_GL_CALL;
  }

  void fBlendFuncSeparate(GLenum sfactorRGB, GLenum dfactorRGB,
                          GLenum sfactorAlpha, GLenum dfactorAlpha) {
    BEFORE_GL_CALL;
    mSymbols.fBlendFuncSeparate(sfactorRGB, dfactorRGB, sfactorAlpha,
                                dfactorAlpha);
    AFTER_GL_CALL;
  }

 private:
  void raw_fBufferData(GLenum target, GLsizeiptr size, const GLvoid* data,
                       GLenum usage) {
    ASSERT_NOT_PASSING_STACK_BUFFER_TO_GL(data);
    BEFORE_GL_CALL;
    mSymbols.fBufferData(target, size, data, usage);
    OnSyncCall();
    AFTER_GL_CALL;
    mHeavyGLCallsSinceLastFlush = true;
  }

 public:
  void fBufferData(GLenum target, GLsizeiptr size, const GLvoid* data,
                   GLenum usage) {
    if (WorkAroundDriverBugs() && target == LOCAL_GL_ARRAY_BUFFER &&
        mVertexBufferExtraPadding) {
      // Some drivers require extra padding at the end of array buffers.
      // See bug 1983036.
      raw_fBufferData(target, size + *mVertexBufferExtraPadding, nullptr,
                      usage);
      if (data) {
        fBufferSubData(target, 0, size, data);
      }
    } else {
      raw_fBufferData(target, size, data, usage);
    }

    // bug 744888
    if (WorkAroundDriverBugs() && !data && Vendor() == GLVendor::NVIDIA) {
      auto buf = std::make_unique<char[]>(1);
      buf[0] = 0;
      fBufferSubData(target, size - 1, 1, buf.get());
    }
  }

  void fBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size,
                      const GLvoid* data) {
    ASSERT_NOT_PASSING_STACK_BUFFER_TO_GL(data);
    BEFORE_GL_CALL;
    mSymbols.fBufferSubData(target, offset, size, data);
    AFTER_GL_CALL;
    mHeavyGLCallsSinceLastFlush = true;
  }

 private:
  void raw_fClear(GLbitfield mask) {
    BEFORE_GL_CALL;
    mSymbols.fClear(mask);
    AFTER_GL_CALL;
  }

 public:
  void fClear(GLbitfield mask) {
    BeforeGLDrawCall();
    raw_fClear(mask);
    AfterGLDrawCall();
  }

  void fClearBufferfi(GLenum buffer, GLint drawbuffer, GLfloat depth,
                      GLint stencil) {
    BeforeGLDrawCall();
    BEFORE_GL_CALL;
    mSymbols.fClearBufferfi(buffer, drawbuffer, depth, stencil);
    AFTER_GL_CALL;
    AfterGLDrawCall();
  }

  void fClearBufferfv(GLenum buffer, GLint drawbuffer, const GLfloat* value) {
    BeforeGLDrawCall();
    BEFORE_GL_CALL;
    mSymbols.fClearBufferfv(buffer, drawbuffer, value);
    AFTER_GL_CALL;
    AfterGLDrawCall();
  }

  void fClearBufferiv(GLenum buffer, GLint drawbuffer, const GLint* value) {
    BeforeGLDrawCall();
    BEFORE_GL_CALL;
    mSymbols.fClearBufferiv(buffer, drawbuffer, value);
    AFTER_GL_CALL;
    AfterGLDrawCall();
  }

  void fClearBufferuiv(GLenum buffer, GLint drawbuffer, const GLuint* value) {
    BeforeGLDrawCall();
    BEFORE_GL_CALL;
    mSymbols.fClearBufferuiv(buffer, drawbuffer, value);
    AFTER_GL_CALL;
    AfterGLDrawCall();
  }

  void fClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    BEFORE_GL_CALL;
    mSymbols.fClearColor(r, g, b, a);
    AFTER_GL_CALL;
  }

  void fClearStencil(GLint s) {
    BEFORE_GL_CALL;
    mSymbols.fClearStencil(s);
    AFTER_GL_CALL;
  }

  void fClientActiveTexture(GLenum texture) {
    BEFORE_GL_CALL;
    mSymbols.fClientActiveTexture(texture);
    AFTER_GL_CALL;
  }

  void fColorMask(realGLboolean red, realGLboolean green, realGLboolean blue,
                  realGLboolean alpha) {
    BEFORE_GL_CALL;
    mSymbols.fColorMask(red, green, blue, alpha);
    AFTER_GL_CALL;
  }

  void fCompressedTexImage2D(GLenum target, GLint level, GLenum internalformat,
                             GLsizei width, GLsizei height, GLint border,
                             GLsizei imageSize, const GLvoid* pixels) {
    ASSERT_NOT_PASSING_STACK_BUFFER_TO_GL(pixels);
    BEFORE_GL_CALL;
    mSymbols.fCompressedTexImage2D(target, level, internalformat, width, height,
                                   border, imageSize, pixels);
    AFTER_GL_CALL;
    mHeavyGLCallsSinceLastFlush = true;
  }

  void fCompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                                GLint yoffset, GLsizei width, GLsizei height,
                                GLenum format, GLsizei imageSize,
                                const GLvoid* pixels) {
    ASSERT_NOT_PASSING_STACK_BUFFER_TO_GL(pixels);
    BEFORE_GL_CALL;
    mSymbols.fCompressedTexSubImage2D(target, level, xoffset, yoffset, width,
                                      height, format, imageSize, pixels);
    AFTER_GL_CALL;
    mHeavyGLCallsSinceLastFlush = true;
  }

  void fCopyTexImage2D(GLenum target, GLint level, GLenum internalformat,
                       GLint x, GLint y, GLsizei width, GLsizei height,
                       GLint border);

  void fCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                          GLint yoffset, GLint x, GLint y, GLsizei width,
                          GLsizei height) {
    BeforeGLReadCall();
    raw_fCopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width,
                           height);
    AfterGLReadCall();
  }

  void fCullFace(GLenum mode) {
    BEFORE_GL_CALL;
    mSymbols.fCullFace(mode);
    AFTER_GL_CALL;
  }

  void fDebugMessageCallback(GLDEBUGPROC callback, const GLvoid* userParam) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fDebugMessageCallback);
    mSymbols.fDebugMessageCallback(callback, userParam);
    AFTER_GL_CALL;
  }

  void fDebugMessageControl(GLenum source, GLenum type, GLenum severity,
                            GLsizei count, const GLuint* ids,
                            realGLboolean enabled) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fDebugMessageControl);
    mSymbols.fDebugMessageControl(source, type, severity, count, ids, enabled);
    AFTER_GL_CALL;
  }

  void fDebugMessageInsert(GLenum source, GLenum type, GLuint id,
                           GLenum severity, GLsizei length, const GLchar* buf) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fDebugMessageInsert);
    mSymbols.fDebugMessageInsert(source, type, id, severity, length, buf);
    AFTER_GL_CALL;
  }

  void fDetachShader(GLuint program, GLuint shader) {
    BEFORE_GL_CALL;
    mSymbols.fDetachShader(program, shader);
    AFTER_GL_CALL;
  }

  void fDepthFunc(GLenum func) {
    BEFORE_GL_CALL;
    mSymbols.fDepthFunc(func);
    AFTER_GL_CALL;
  }

  void fDepthMask(realGLboolean flag) {
    BEFORE_GL_CALL;
    mSymbols.fDepthMask(flag);
    AFTER_GL_CALL;
  }

  void fDisable(GLenum capability) {
    BEFORE_GL_CALL;
    mSymbols.fDisable(capability);
    AFTER_GL_CALL;
  }

  void fDisableClientState(GLenum capability) {
    BEFORE_GL_CALL;
    mSymbols.fDisableClientState(capability);
    AFTER_GL_CALL;
  }

  void fDisableVertexAttribArray(GLuint index) {
    BEFORE_GL_CALL;
    mSymbols.fDisableVertexAttribArray(index);
    AFTER_GL_CALL;
  }

  void fDrawBuffer(GLenum mode) {
    BEFORE_GL_CALL;
    mSymbols.fDrawBuffer(mode);
    AFTER_GL_CALL;
  }

 private:
  void raw_fDrawArrays(GLenum mode, GLint first, GLsizei count) {
    BEFORE_GL_CALL;
    mSymbols.fDrawArrays(mode, first, count);
    AFTER_GL_CALL;
  }

  void raw_fDrawElements(GLenum mode, GLsizei count, GLenum type,
                         const GLvoid* indices) {
    BEFORE_GL_CALL;
    mSymbols.fDrawElements(mode, count, type, indices);
    AFTER_GL_CALL;
  }

 public:
  void fDrawArrays(GLenum mode, GLint first, GLsizei count) {
    BeforeGLDrawCall();
    raw_fDrawArrays(mode, first, count);
    AfterGLDrawCall();
  }

  void fDrawElements(GLenum mode, GLsizei count, GLenum type,
                     const GLvoid* indices) {
    BeforeGLDrawCall();
    raw_fDrawElements(mode, count, type, indices);
    AfterGLDrawCall();
  }

  void fEnable(GLenum capability) {
    BEFORE_GL_CALL;
    mSymbols.fEnable(capability);
    AFTER_GL_CALL;
  }

  void fEnableClientState(GLenum capability) {
    BEFORE_GL_CALL;
    mSymbols.fEnableClientState(capability);
    AFTER_GL_CALL;
  }

  void fEnableVertexAttribArray(GLuint index) {
    BEFORE_GL_CALL;
    mSymbols.fEnableVertexAttribArray(index);
    AFTER_GL_CALL;
  }

  void fEndQuery(GLenum target) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fEndQuery);
    mSymbols.fEndQuery(target);
    AFTER_GL_CALL;
  }

  void fFinish() {
    BEFORE_GL_CALL;
    mSymbols.fFinish();
    OnSyncCall();
    AFTER_GL_CALL;
    mHeavyGLCallsSinceLastFlush = false;
  }

  void fFlush() {
    BEFORE_GL_CALL;
    mSymbols.fFlush();
    AFTER_GL_CALL;
    mHeavyGLCallsSinceLastFlush = false;
  }

  void fFrontFace(GLenum face) {
    BEFORE_GL_CALL;
    mSymbols.fFrontFace(face);
    AFTER_GL_CALL;
  }

  void fGetActiveAttrib(GLuint program, GLuint index, GLsizei maxLength,
                        GLsizei* length, GLint* size, GLenum* type,
                        GLchar* name) {
    BEFORE_GL_CALL;
    mSymbols.fGetActiveAttrib(program, index, maxLength, length, size, type,
                              name);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fGetActiveUniform(GLuint program, GLuint index, GLsizei maxLength,
                         GLsizei* length, GLint* size, GLenum* type,
                         GLchar* name) {
    BEFORE_GL_CALL;
    mSymbols.fGetActiveUniform(program, index, maxLength, length, size, type,
                               name);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fGetAttachedShaders(GLuint program, GLsizei maxCount, GLsizei* count,
                           GLuint* shaders) {
    BEFORE_GL_CALL;
    mSymbols.fGetAttachedShaders(program, maxCount, count, shaders);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  GLint fGetAttribLocation(GLuint program, const GLchar* name) {
    GLint retval = 0;
    BEFORE_GL_CALL;
    retval = mSymbols.fGetAttribLocation(program, name);
    OnSyncCall();
    AFTER_GL_CALL;
    return retval;
  }

 private:
  void raw_fGetIntegerv(GLenum pname, GLint* params) const {
    BEFORE_GL_CALL;
    mSymbols.fGetIntegerv(pname, params);
    OnSyncCall();
    AFTER_GL_CALL;
  }

 public:
  void fGetIntegerv(GLenum pname, GLint* params) const;

  template <typename T>
  void GetInt(const GLenum pname, T* const params) const {
    static_assert(sizeof(T) == sizeof(GLint), "Invalid T.");
    fGetIntegerv(pname, reinterpret_cast<GLint*>(params));
  }

  void GetUIntegerv(GLenum pname, GLuint* params) const {
    GetInt(pname, params);
  }

  template <typename T>
  T GetIntAs(GLenum pname) const {
    static_assert(sizeof(T) == sizeof(GLint), "Invalid T.");
    T ret = 0;
    fGetIntegerv(pname, (GLint*)&ret);
    return ret;
  }

  void fGetFloatv(GLenum pname, GLfloat* params) const {
    BEFORE_GL_CALL;
    mSymbols.fGetFloatv(pname, params);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fGetBooleanv(GLenum pname, realGLboolean* params) const {
    BEFORE_GL_CALL;
    mSymbols.fGetBooleanv(pname, params);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fGetBufferParameteriv(GLenum target, GLenum pname, GLint* params) {
    BEFORE_GL_CALL;
    mSymbols.fGetBufferParameteriv(target, pname, params);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  GLuint fGetDebugMessageLog(GLuint count, GLsizei bufsize, GLenum* sources,
                             GLenum* types, GLuint* ids, GLenum* severities,
                             GLsizei* lengths, GLchar* messageLog) {
    GLuint ret = 0;
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fGetDebugMessageLog);
    ret = mSymbols.fGetDebugMessageLog(count, bufsize, sources, types, ids,
                                       severities, lengths, messageLog);
    OnSyncCall();
    AFTER_GL_CALL;
    return ret;
  }

  void fGetPointerv(GLenum pname, GLvoid** params) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fGetPointerv);
    mSymbols.fGetPointerv(pname, params);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fGetObjectLabel(GLenum identifier, GLuint name, GLsizei bufSize,
                       GLsizei* length, GLchar* label) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fGetObjectLabel);
    mSymbols.fGetObjectLabel(identifier, name, bufSize, length, label);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fGetObjectPtrLabel(const GLvoid* ptr, GLsizei bufSize, GLsizei* length,
                          GLchar* label) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fGetObjectPtrLabel);
    mSymbols.fGetObjectPtrLabel(ptr, bufSize, length, label);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fGenerateMipmap(GLenum target) {
    BEFORE_GL_CALL;
    mSymbols.fGenerateMipmap(target);
    AFTER_GL_CALL;
  }

  void fGetProgramiv(GLuint program, GLenum pname, GLint* param) {
    BEFORE_GL_CALL;
    mSymbols.fGetProgramiv(program, pname, param);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fGetProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei* length,
                          GLchar* infoLog) {
    BEFORE_GL_CALL;
    mSymbols.fGetProgramInfoLog(program, bufSize, length, infoLog);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fTexParameteri(GLenum target, GLenum pname, GLint param) {
    BEFORE_GL_CALL;
    mSymbols.fTexParameteri(target, pname, param);
    AFTER_GL_CALL;
  }

  void fTexParameteriv(GLenum target, GLenum pname, const GLint* params) {
    BEFORE_GL_CALL;
    mSymbols.fTexParameteriv(target, pname, params);
    AFTER_GL_CALL;
  }

  void fTexParameterf(GLenum target, GLenum pname, GLfloat param) {
    BEFORE_GL_CALL;
    mSymbols.fTexParameterf(target, pname, param);
    AFTER_GL_CALL;
  }

  const GLubyte* fGetString(GLenum name) {
    const GLubyte* result = nullptr;
    BEFORE_GL_CALL;
    result = mSymbols.fGetString(name);
    OnSyncCall();
    AFTER_GL_CALL;
    return result;
  }

  void fGetTexImage(GLenum target, GLint level, GLenum format, GLenum type,
                    GLvoid* img) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fGetTexImage);
    mSymbols.fGetTexImage(target, level, format, type, img);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname,
                               GLint* params) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fGetTexLevelParameteriv);
    mSymbols.fGetTexLevelParameteriv(target, level, pname, params);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fGetTexParameterfv(GLenum target, GLenum pname, GLfloat* params) {
    BEFORE_GL_CALL;
    mSymbols.fGetTexParameterfv(target, pname, params);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fGetTexParameteriv(GLenum target, GLenum pname, GLint* params) {
    BEFORE_GL_CALL;
    mSymbols.fGetTexParameteriv(target, pname, params);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fGetUniformfv(GLuint program, GLint location, GLfloat* params) {
    BEFORE_GL_CALL;
    mSymbols.fGetUniformfv(program, location, params);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fGetUniformiv(GLuint program, GLint location, GLint* params) {
    BEFORE_GL_CALL;
    mSymbols.fGetUniformiv(program, location, params);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fGetUniformuiv(GLuint program, GLint location, GLuint* params) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fGetUniformuiv);
    mSymbols.fGetUniformuiv(program, location, params);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  GLint fGetUniformLocation(GLuint programObj, const GLchar* name) {
    GLint retval = -1;
    BEFORE_GL_CALL;
    retval = mSymbols.fGetUniformLocation(programObj, name);
    OnSyncCall();
    AFTER_GL_CALL;
    return retval;
  }

  void fGetVertexAttribfv(GLuint index, GLenum pname, GLfloat* retval) {
    BEFORE_GL_CALL;
    mSymbols.fGetVertexAttribfv(index, pname, retval);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fGetVertexAttribiv(GLuint index, GLenum pname, GLint* retval) {
    BEFORE_GL_CALL;
    mSymbols.fGetVertexAttribiv(index, pname, retval);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fGetVertexAttribPointerv(GLuint index, GLenum pname, GLvoid** retval) {
    BEFORE_GL_CALL;
    mSymbols.fGetVertexAttribPointerv(index, pname, retval);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fHint(GLenum target, GLenum mode) {
    BEFORE_GL_CALL;
    mSymbols.fHint(target, mode);
    AFTER_GL_CALL;
  }

  realGLboolean fIsBuffer(GLuint buffer) {
    realGLboolean retval = false;
    BEFORE_GL_CALL;
    retval = mSymbols.fIsBuffer(buffer);
    OnSyncCall();
    AFTER_GL_CALL;
    return retval;
  }

  realGLboolean fIsEnabled(GLenum capability) {
    realGLboolean retval = false;
    BEFORE_GL_CALL;
    retval = mSymbols.fIsEnabled(capability);
    AFTER_GL_CALL;
    return retval;
  }

  void SetEnabled(const GLenum cap, const bool val) {
    if (val) {
      fEnable(cap);
    } else {
      fDisable(cap);
    }
  }

  bool PushEnabled(const GLenum cap, const bool newVal) {
    const bool oldVal = fIsEnabled(cap);
    if (oldVal != newVal) {
      SetEnabled(cap, newVal);
    }
    return oldVal;
  }

  realGLboolean fIsProgram(GLuint program) {
    realGLboolean retval = false;
    BEFORE_GL_CALL;
    retval = mSymbols.fIsProgram(program);
    AFTER_GL_CALL;
    return retval;
  }

  realGLboolean fIsShader(GLuint shader) {
    realGLboolean retval = false;
    BEFORE_GL_CALL;
    retval = mSymbols.fIsShader(shader);
    AFTER_GL_CALL;
    return retval;
  }

  realGLboolean fIsTexture(GLuint texture) {
    realGLboolean retval = false;
    BEFORE_GL_CALL;
    retval = mSymbols.fIsTexture(texture);
    AFTER_GL_CALL;
    return retval;
  }

  void fLineWidth(GLfloat width) {
    BEFORE_GL_CALL;
    mSymbols.fLineWidth(width);
    AFTER_GL_CALL;
  }

  void fLinkProgram(GLuint program) {
    BEFORE_GL_CALL;
    mSymbols.fLinkProgram(program);
    AFTER_GL_CALL;
  }

  void fObjectLabel(GLenum identifier, GLuint name, GLsizei length,
                    const GLchar* label) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fObjectLabel);
    mSymbols.fObjectLabel(identifier, name, length, label);
    AFTER_GL_CALL;
  }

  void fObjectPtrLabel(const GLvoid* ptr, GLsizei length, const GLchar* label) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fObjectPtrLabel);
    mSymbols.fObjectPtrLabel(ptr, length, label);
    AFTER_GL_CALL;
  }

  void fLoadIdentity() {
    BEFORE_GL_CALL;
    mSymbols.fLoadIdentity();
    AFTER_GL_CALL;
  }

  void fLoadMatrixf(const GLfloat* matrix) {
    BEFORE_GL_CALL;
    mSymbols.fLoadMatrixf(matrix);
    AFTER_GL_CALL;
  }

  void fMatrixMode(GLenum mode) {
    BEFORE_GL_CALL;
    mSymbols.fMatrixMode(mode);
    AFTER_GL_CALL;
  }

  void fPixelStorei(GLenum pname, GLint param) {
    BEFORE_GL_CALL;
    mSymbols.fPixelStorei(pname, param);
    AFTER_GL_CALL;
  }

  void fTextureRangeAPPLE(GLenum target, GLsizei length, GLvoid* pointer) {
    ASSERT_NOT_PASSING_STACK_BUFFER_TO_GL(pointer);
    BEFORE_GL_CALL;
    mSymbols.fTextureRangeAPPLE(target, length, pointer);
    AFTER_GL_CALL;
  }

  void fPointParameterf(GLenum pname, GLfloat param) {
    BEFORE_GL_CALL;
    mSymbols.fPointParameterf(pname, param);
    AFTER_GL_CALL;
  }

  void fPolygonMode(GLenum face, GLenum mode) {
    BEFORE_GL_CALL;
    mSymbols.fPolygonMode(face, mode);
    AFTER_GL_CALL;
  }

  void fPolygonOffset(GLfloat factor, GLfloat bias) {
    BEFORE_GL_CALL;
    mSymbols.fPolygonOffset(factor, bias);
    AFTER_GL_CALL;
  }

  void fPolygonOffsetClamp(GLfloat factor, GLfloat units, GLfloat clamp) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fPolygonOffsetClamp);
    mSymbols.fPolygonOffsetClamp(factor, units, clamp);
    AFTER_GL_CALL;
  }

  void fClipControl(GLenum origin, GLenum depth) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fClipControl);
    mSymbols.fClipControl(origin, depth);
    AFTER_GL_CALL;
  }

  void fPopDebugGroup() {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fPopDebugGroup);
    mSymbols.fPopDebugGroup();
    AFTER_GL_CALL;
  }

  void fPushDebugGroup(GLenum source, GLuint id, GLsizei length,
                       const GLchar* message) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fPushDebugGroup);
    mSymbols.fPushDebugGroup(source, id, length, message);
    AFTER_GL_CALL;
  }

  void fReadBuffer(GLenum mode) {
    BEFORE_GL_CALL;
    mSymbols.fReadBuffer(mode);
    AFTER_GL_CALL;
  }

  void raw_fReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                       GLenum format, GLenum type, GLvoid* pixels) {
    BEFORE_GL_CALL;
    mSymbols.fReadPixels(x, y, width, height, format, type, pixels);
    OnSyncCall();
    AFTER_GL_CALL;
    mHeavyGLCallsSinceLastFlush = true;
  }

  void fReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                   GLenum format, GLenum type, GLvoid* pixels);

 public:
  void fSampleCoverage(GLclampf value, realGLboolean invert) {
    BEFORE_GL_CALL;
    mSymbols.fSampleCoverage(value, invert);
    AFTER_GL_CALL;
  }

  void fScissor(GLint x, GLint y, GLsizei width, GLsizei height) {
    if (mScissorRect[0] == x && mScissorRect[1] == y &&
        mScissorRect[2] == width && mScissorRect[3] == height) {
      return;
    }
    mScissorRect[0] = x;
    mScissorRect[1] = y;
    mScissorRect[2] = width;
    mScissorRect[3] = height;
    BEFORE_GL_CALL;
    mSymbols.fScissor(x, y, width, height);
    AFTER_GL_CALL;
  }

  void fStencilFunc(GLenum func, GLint reference, GLuint mask) {
    BEFORE_GL_CALL;
    mSymbols.fStencilFunc(func, reference, mask);
    AFTER_GL_CALL;
  }

  void fStencilFuncSeparate(GLenum frontfunc, GLenum backfunc, GLint reference,
                            GLuint mask) {
    BEFORE_GL_CALL;
    mSymbols.fStencilFuncSeparate(frontfunc, backfunc, reference, mask);
    AFTER_GL_CALL;
  }

  void fStencilMask(GLuint mask) {
    BEFORE_GL_CALL;
    mSymbols.fStencilMask(mask);
    AFTER_GL_CALL;
  }

  void fStencilMaskSeparate(GLenum face, GLuint mask) {
    BEFORE_GL_CALL;
    mSymbols.fStencilMaskSeparate(face, mask);
    AFTER_GL_CALL;
  }

  void fStencilOp(GLenum fail, GLenum zfail, GLenum zpass) {
    BEFORE_GL_CALL;
    mSymbols.fStencilOp(fail, zfail, zpass);
    AFTER_GL_CALL;
  }

  void fStencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail,
                          GLenum dppass) {
    BEFORE_GL_CALL;
    mSymbols.fStencilOpSeparate(face, sfail, dpfail, dppass);
    AFTER_GL_CALL;
  }

  void fTexGeni(GLenum coord, GLenum pname, GLint param) {
    BEFORE_GL_CALL;
    mSymbols.fTexGeni(coord, pname, param);
    AFTER_GL_CALL;
  }

  void fTexGenf(GLenum coord, GLenum pname, GLfloat param) {
    BEFORE_GL_CALL;
    mSymbols.fTexGenf(coord, pname, param);
    AFTER_GL_CALL;
  }

  void fTexGenfv(GLenum coord, GLenum pname, const GLfloat* params) {
    BEFORE_GL_CALL;
    mSymbols.fTexGenfv(coord, pname, params);
    AFTER_GL_CALL;
  }

 private:
  void raw_fTexImage2D(GLenum target, GLint level, GLint internalformat,
                       GLsizei width, GLsizei height, GLint border,
                       GLenum format, GLenum type, const GLvoid* pixels) {
    ASSERT_NOT_PASSING_STACK_BUFFER_TO_GL(pixels);
    BEFORE_GL_CALL;
    mSymbols.fTexImage2D(target, level, internalformat, width, height, border,
                         format, type, pixels);
    AFTER_GL_CALL;
    mHeavyGLCallsSinceLastFlush = true;
  }

 public:
  void fTexImage2D(GLenum target, GLint level, GLint internalformat,
                   GLsizei width, GLsizei height, GLint border, GLenum format,
                   GLenum type, const GLvoid* pixels);

  void fTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                      GLsizei width, GLsizei height, GLenum format, GLenum type,
                      const GLvoid* pixels) {
    ASSERT_NOT_PASSING_STACK_BUFFER_TO_GL(pixels);
    BEFORE_GL_CALL;
    mSymbols.fTexSubImage2D(target, level, xoffset, yoffset, width, height,
                            format, type, pixels);
    AFTER_GL_CALL;
    mHeavyGLCallsSinceLastFlush = true;
  }

  void fUniform1f(GLint location, GLfloat v0) {
    BEFORE_GL_CALL;
    mSymbols.fUniform1f(location, v0);
    AFTER_GL_CALL;
  }

  void fUniform1fv(GLint location, GLsizei count, const GLfloat* value) {
    BEFORE_GL_CALL;
    mSymbols.fUniform1fv(location, count, value);
    AFTER_GL_CALL;
  }

  void fUniform1i(GLint location, GLint v0) {
    BEFORE_GL_CALL;
    mSymbols.fUniform1i(location, v0);
    AFTER_GL_CALL;
  }

  void fUniform1iv(GLint location, GLsizei count, const GLint* value) {
    BEFORE_GL_CALL;
    mSymbols.fUniform1iv(location, count, value);
    AFTER_GL_CALL;
  }

  void fUniform2f(GLint location, GLfloat v0, GLfloat v1) {
    BEFORE_GL_CALL;
    mSymbols.fUniform2f(location, v0, v1);
    AFTER_GL_CALL;
  }

  void fUniform2fv(GLint location, GLsizei count, const GLfloat* value) {
    BEFORE_GL_CALL;
    mSymbols.fUniform2fv(location, count, value);
    AFTER_GL_CALL;
  }

  void fUniform2i(GLint location, GLint v0, GLint v1) {
    BEFORE_GL_CALL;
    mSymbols.fUniform2i(location, v0, v1);
    AFTER_GL_CALL;
  }

  void fUniform2iv(GLint location, GLsizei count, const GLint* value) {
    BEFORE_GL_CALL;
    mSymbols.fUniform2iv(location, count, value);
    AFTER_GL_CALL;
  }

  void fUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) {
    BEFORE_GL_CALL;
    mSymbols.fUniform3f(location, v0, v1, v2);
    AFTER_GL_CALL;
  }

  void fUniform3fv(GLint location, GLsizei count, const GLfloat* value) {
    BEFORE_GL_CALL;
    mSymbols.fUniform3fv(location, count, value);
    AFTER_GL_CALL;
  }

  void fUniform3i(GLint location, GLint v0, GLint v1, GLint v2) {
    BEFORE_GL_CALL;
    mSymbols.fUniform3i(location, v0, v1, v2);
    AFTER_GL_CALL;
  }

  void fUniform3iv(GLint location, GLsizei count, const GLint* value) {
    BEFORE_GL_CALL;
    mSymbols.fUniform3iv(location, count, value);
    AFTER_GL_CALL;
  }

  void fUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2,
                  GLfloat v3) {
    BEFORE_GL_CALL;
    mSymbols.fUniform4f(location, v0, v1, v2, v3);
    AFTER_GL_CALL;
  }

  void fUniform4fv(GLint location, GLsizei count, const GLfloat* value) {
    BEFORE_GL_CALL;
    mSymbols.fUniform4fv(location, count, value);
    AFTER_GL_CALL;
  }

  void fUniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3) {
    BEFORE_GL_CALL;
    mSymbols.fUniform4i(location, v0, v1, v2, v3);
    AFTER_GL_CALL;
  }

  void fUniform4iv(GLint location, GLsizei count, const GLint* value) {
    BEFORE_GL_CALL;
    mSymbols.fUniform4iv(location, count, value);
    AFTER_GL_CALL;
  }

  void fUniformMatrix2fv(GLint location, GLsizei count, realGLboolean transpose,
                         const GLfloat* value) {
    BEFORE_GL_CALL;
    mSymbols.fUniformMatrix2fv(location, count, transpose, value);
    AFTER_GL_CALL;
  }

  void fUniformMatrix2x3fv(GLint location, GLsizei count,
                           realGLboolean transpose, const GLfloat* value) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fUniformMatrix2x3fv);
    mSymbols.fUniformMatrix2x3fv(location, count, transpose, value);
    AFTER_GL_CALL;
  }

  void fUniformMatrix2x4fv(GLint location, GLsizei count,
                           realGLboolean transpose, const GLfloat* value) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fUniformMatrix2x4fv);
    mSymbols.fUniformMatrix2x4fv(location, count, transpose, value);
    AFTER_GL_CALL;
  }

  void fUniformMatrix3fv(GLint location, GLsizei count, realGLboolean transpose,
                         const GLfloat* value) {
    BEFORE_GL_CALL;
    mSymbols.fUniformMatrix3fv(location, count, transpose, value);
    AFTER_GL_CALL;
  }

  void fUniformMatrix3x2fv(GLint location, GLsizei count,
                           realGLboolean transpose, const GLfloat* value) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fUniformMatrix3x2fv);
    mSymbols.fUniformMatrix3x2fv(location, count, transpose, value);
    AFTER_GL_CALL;
  }

  void fUniformMatrix3x4fv(GLint location, GLsizei count,
                           realGLboolean transpose, const GLfloat* value) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fUniformMatrix3x4fv);
    mSymbols.fUniformMatrix3x4fv(location, count, transpose, value);
    AFTER_GL_CALL;
  }

  void fUniformMatrix4fv(GLint location, GLsizei count, realGLboolean transpose,
                         const GLfloat* value) {
    BEFORE_GL_CALL;
    mSymbols.fUniformMatrix4fv(location, count, transpose, value);
    AFTER_GL_CALL;
  }

  void fUniformMatrix4x2fv(GLint location, GLsizei count,
                           realGLboolean transpose, const GLfloat* value) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fUniformMatrix4x2fv);
    mSymbols.fUniformMatrix4x2fv(location, count, transpose, value);
    AFTER_GL_CALL;
  }

  void fUniformMatrix4x3fv(GLint location, GLsizei count,
                           realGLboolean transpose, const GLfloat* value) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fUniformMatrix4x3fv);
    mSymbols.fUniformMatrix4x3fv(location, count, transpose, value);
    AFTER_GL_CALL;
  }

  void fUseProgram(GLuint program) {
    BEFORE_GL_CALL;
    mSymbols.fUseProgram(program);
    AFTER_GL_CALL;
  }

  void fValidateProgram(GLuint program) {
    BEFORE_GL_CALL;
    mSymbols.fValidateProgram(program);
    AFTER_GL_CALL;
  }

  void fVertexAttribPointer(GLuint index, GLint size, GLenum type,
                            realGLboolean normalized, GLsizei stride,
                            const GLvoid* pointer) {
    BEFORE_GL_CALL;
    mSymbols.fVertexAttribPointer(index, size, type, normalized, stride,
                                  pointer);
    AFTER_GL_CALL;
  }

  void fVertexAttrib1f(GLuint index, GLfloat x) {
    BEFORE_GL_CALL;
    mSymbols.fVertexAttrib1f(index, x);
    AFTER_GL_CALL;
  }

  void fVertexAttrib2f(GLuint index, GLfloat x, GLfloat y) {
    BEFORE_GL_CALL;
    mSymbols.fVertexAttrib2f(index, x, y);
    AFTER_GL_CALL;
  }

  void fVertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z) {
    BEFORE_GL_CALL;
    mSymbols.fVertexAttrib3f(index, x, y, z);
    AFTER_GL_CALL;
  }

  void fVertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z,
                       GLfloat w) {
    BEFORE_GL_CALL;
    mSymbols.fVertexAttrib4f(index, x, y, z, w);
    AFTER_GL_CALL;
  }

  void fVertexAttrib1fv(GLuint index, const GLfloat* v) {
    BEFORE_GL_CALL;
    mSymbols.fVertexAttrib1fv(index, v);
    AFTER_GL_CALL;
  }

  void fVertexAttrib2fv(GLuint index, const GLfloat* v) {
    BEFORE_GL_CALL;
    mSymbols.fVertexAttrib2fv(index, v);
    AFTER_GL_CALL;
  }

  void fVertexAttrib3fv(GLuint index, const GLfloat* v) {
    BEFORE_GL_CALL;
    mSymbols.fVertexAttrib3fv(index, v);
    AFTER_GL_CALL;
  }

  void fVertexAttrib4fv(GLuint index, const GLfloat* v) {
    BEFORE_GL_CALL;
    mSymbols.fVertexAttrib4fv(index, v);
    AFTER_GL_CALL;
  }

  void fVertexPointer(GLint size, GLenum type, GLsizei stride,
                      const GLvoid* pointer) {
    BEFORE_GL_CALL;
    mSymbols.fVertexPointer(size, type, stride, pointer);
    AFTER_GL_CALL;
  }

  void fViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    if (mViewportRect[0] == x && mViewportRect[1] == y &&
        mViewportRect[2] == width && mViewportRect[3] == height) {
      return;
    }
    mViewportRect[0] = x;
    mViewportRect[1] = y;
    mViewportRect[2] = width;
    mViewportRect[3] = height;
    BEFORE_GL_CALL;
    mSymbols.fViewport(x, y, width, height);
    AFTER_GL_CALL;
  }

  void fCompileShader(GLuint shader) {
    BEFORE_GL_CALL;
    mSymbols.fCompileShader(shader);
    AFTER_GL_CALL;
  }

 private:
  friend class SharedSurface_IOSurface;

  void raw_fCopyTexImage2D(GLenum target, GLint level, GLenum internalformat,
                           GLint x, GLint y, GLsizei width, GLsizei height,
                           GLint border) {
    BEFORE_GL_CALL;
    mSymbols.fCopyTexImage2D(target, level, internalformat, x, y, width, height,
                             border);
    AFTER_GL_CALL;
  }

  void raw_fCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                              GLint yoffset, GLint x, GLint y, GLsizei width,
                              GLsizei height) {
    BEFORE_GL_CALL;
    mSymbols.fCopyTexSubImage2D(target, level, xoffset, yoffset, x, y, width,
                                height);
    AFTER_GL_CALL;
  }

 public:
  void fGetShaderiv(GLuint shader, GLenum pname, GLint* param) {
    BEFORE_GL_CALL;
    mSymbols.fGetShaderiv(shader, pname, param);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei* length,
                         GLchar* infoLog) {
    BEFORE_GL_CALL;
    mSymbols.fGetShaderInfoLog(shader, bufSize, length, infoLog);
    OnSyncCall();
    AFTER_GL_CALL;
  }

 private:
  void raw_fGetShaderPrecisionFormat(GLenum shadertype, GLenum precisiontype,
                                     GLint* range, GLint* precision) {
    MOZ_ASSERT(IsGLES());

    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fGetShaderPrecisionFormat);
    mSymbols.fGetShaderPrecisionFormat(shadertype, precisiontype, range,
                                       precision);
    OnSyncCall();
    AFTER_GL_CALL;
  }

 public:
  void fGetShaderPrecisionFormat(GLenum shadertype, GLenum precisiontype,
                                 GLint* range, GLint* precision) {
    if (IsGLES()) {
      raw_fGetShaderPrecisionFormat(shadertype, precisiontype, range,
                                    precision);
    } else {
      // Fall back to automatic values because almost all desktop hardware
      // supports the OpenGL standard precisions.
      GetShaderPrecisionFormatNonES2(shadertype, precisiontype, range,
                                     precision);
    }
  }

  void fGetShaderSource(GLint obj, GLsizei maxLength, GLsizei* length,
                        GLchar* source) {
    BEFORE_GL_CALL;
    mSymbols.fGetShaderSource(obj, maxLength, length, source);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fShaderSource(GLuint shader, GLsizei count, const GLchar* const* strings,
                     const GLint* lengths) {
    BEFORE_GL_CALL;
    mSymbols.fShaderSource(shader, count, strings, lengths);
    AFTER_GL_CALL;
  }

 private:
  mutable GLuint mCachedDrawFb = 0;
  mutable GLuint mCachedReadFb = 0;

 public:
  bool mElideDuplicateBindFramebuffers = false;

  // If e.g. GL_DRAW_FRAMEBUFFER isn't supported, will bind GL_FRAMEBUFFER.
  void fBindFramebuffer(GLenum target, const GLuint fb) const {
    if (!IsSupported(gl::GLFeature::framebuffer_blit)) {
      target = LOCAL_GL_FRAMEBUFFER;
    }
    if (mElideDuplicateBindFramebuffers) {
      MOZ_ASSERT(mCachedDrawFb ==
                 GetIntAs<GLuint>(LOCAL_GL_DRAW_FRAMEBUFFER_BINDING));
      MOZ_ASSERT(mCachedReadFb ==
                 GetIntAs<GLuint>(LOCAL_GL_READ_FRAMEBUFFER_BINDING));

      switch (target) {
        case LOCAL_GL_FRAMEBUFFER:
          if (mCachedDrawFb == fb && mCachedReadFb == fb) return;
          break;
        case LOCAL_GL_DRAW_FRAMEBUFFER:
          if (mCachedDrawFb == fb) return;
          break;
        case LOCAL_GL_READ_FRAMEBUFFER:
          if (mCachedReadFb == fb) return;
          break;
      }
    }

    BEFORE_GL_CALL;
    mSymbols.fBindFramebuffer(target, fb);
    AFTER_GL_CALL;

    switch (target) {
      case LOCAL_GL_FRAMEBUFFER:
        mCachedDrawFb = fb;
        mCachedReadFb = fb;
        break;
      case LOCAL_GL_DRAW_FRAMEBUFFER:
        mCachedDrawFb = fb;
        break;
      case LOCAL_GL_READ_FRAMEBUFFER:
        mCachedReadFb = fb;
        break;
    }
  }

  void fBindRenderbuffer(GLenum target, GLuint renderbuffer) {
    BEFORE_GL_CALL;
    mSymbols.fBindRenderbuffer(target, renderbuffer);
    AFTER_GL_CALL;
  }

  GLenum fCheckFramebufferStatus(GLenum target) {
    GLenum retval = 0;
    BEFORE_GL_CALL;
    retval = mSymbols.fCheckFramebufferStatus(target);
    OnSyncCall();
    AFTER_GL_CALL;
    return retval;
  }

  void fFramebufferRenderbuffer(GLenum target, GLenum attachmentPoint,
                                GLenum renderbufferTarget,
                                GLuint renderbuffer) {
    BEFORE_GL_CALL;
    mSymbols.fFramebufferRenderbuffer(target, attachmentPoint,
                                      renderbufferTarget, renderbuffer);
    AFTER_GL_CALL;
  }

  void fFramebufferTexture2D(GLenum target, GLenum attachmentPoint,
                             GLenum textureTarget, GLuint texture,
                             GLint level) {
    BEFORE_GL_CALL;
    mSymbols.fFramebufferTexture2D(target, attachmentPoint, textureTarget,
                                   texture, level);
    AFTER_GL_CALL;
  }

  void fFramebufferTextureLayer(GLenum target, GLenum attachment,
                                GLuint texture, GLint level, GLint layer) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fFramebufferTextureLayer);
    mSymbols.fFramebufferTextureLayer(target, attachment, texture, level,
                                      layer);
    AFTER_GL_CALL;
  }

  void fGetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment,
                                            GLenum pname, GLint* value) {
    BEFORE_GL_CALL;
    mSymbols.fGetFramebufferAttachmentParameteriv(target, attachment, pname,
                                                  value);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fGetRenderbufferParameteriv(GLenum target, GLenum pname, GLint* value) {
    BEFORE_GL_CALL;
    mSymbols.fGetRenderbufferParameteriv(target, pname, value);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  realGLboolean fIsFramebuffer(GLuint framebuffer) {
    realGLboolean retval = false;
    BEFORE_GL_CALL;
    retval = mSymbols.fIsFramebuffer(framebuffer);
    OnSyncCall();
    AFTER_GL_CALL;
    return retval;
  }

 public:
  realGLboolean fIsRenderbuffer(GLuint renderbuffer) {
    realGLboolean retval = false;
    BEFORE_GL_CALL;
    retval = mSymbols.fIsRenderbuffer(renderbuffer);
    OnSyncCall();
    AFTER_GL_CALL;
    return retval;
  }

  void fRenderbufferStorage(GLenum target, GLenum internalFormat, GLsizei width,
                            GLsizei height) {
    BEFORE_GL_CALL;
    mSymbols.fRenderbufferStorage(target, internalFormat, width, height);
    AFTER_GL_CALL;
  }

 private:
  void raw_fDepthRange(GLclampf a, GLclampf b) {
    MOZ_ASSERT(!IsGLES());

    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fDepthRange);
    mSymbols.fDepthRange(a, b);
    AFTER_GL_CALL;
  }

  void raw_fDepthRangef(GLclampf a, GLclampf b) {
    MOZ_ASSERT(IsGLES());

    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fDepthRangef);
    mSymbols.fDepthRangef(a, b);
    AFTER_GL_CALL;
  }

  void raw_fClearDepth(GLclampf v) {
    MOZ_ASSERT(!IsGLES());

    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fClearDepth);
    mSymbols.fClearDepth(v);
    AFTER_GL_CALL;
  }

  void raw_fClearDepthf(GLclampf v) {
    MOZ_ASSERT(IsGLES());

    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fClearDepthf);
    mSymbols.fClearDepthf(v);
    AFTER_GL_CALL;
  }

 public:
  void fDepthRange(GLclampf a, GLclampf b) {
    if (IsGLES()) {
      raw_fDepthRangef(a, b);
    } else {
      raw_fDepthRange(a, b);
    }
  }

  void fClearDepth(GLclampf v) {
    if (IsGLES()) {
      raw_fClearDepthf(v);
    } else {
      raw_fClearDepth(v);
    }
  }

  void* fMapBuffer(GLenum target, GLenum access) {
    void* ret = nullptr;
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fMapBuffer);
    ret = mSymbols.fMapBuffer(target, access);
    OnSyncCall();
    AFTER_GL_CALL;
    return ret;
  }

  realGLboolean fUnmapBuffer(GLenum target) {
    realGLboolean ret = false;
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fUnmapBuffer);
    ret = mSymbols.fUnmapBuffer(target);
    AFTER_GL_CALL;
    return ret;
  }

 private:
  GLuint raw_fCreateProgram() {
    GLuint ret = 0;
    BEFORE_GL_CALL;
    ret = mSymbols.fCreateProgram();
    AFTER_GL_CALL;
    return ret;
  }

  GLuint raw_fCreateShader(GLenum t) {
    GLuint ret = 0;
    BEFORE_GL_CALL;
    ret = mSymbols.fCreateShader(t);
    AFTER_GL_CALL;
    return ret;
  }

  void raw_fGenBuffers(GLsizei n, GLuint* names) {
    BEFORE_GL_CALL;
    mSymbols.fGenBuffers(n, names);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void raw_fGenFramebuffers(GLsizei n, GLuint* names) {
    BEFORE_GL_CALL;
    mSymbols.fGenFramebuffers(n, names);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void raw_fGenRenderbuffers(GLsizei n, GLuint* names) {
    BEFORE_GL_CALL;
    mSymbols.fGenRenderbuffers(n, names);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void raw_fGenTextures(GLsizei n, GLuint* names) {
    BEFORE_GL_CALL;
    mSymbols.fGenTextures(n, names);
    OnSyncCall();
    AFTER_GL_CALL;
  }

 public:
  GLuint fCreateProgram() {
    GLuint ret = raw_fCreateProgram();
    TRACKING_CONTEXT(CreatedProgram(this, ret));
    return ret;
  }

  GLuint fCreateShader(GLenum t) {
    GLuint ret = raw_fCreateShader(t);
    TRACKING_CONTEXT(CreatedShader(this, ret));
    return ret;
  }

  void fGenBuffers(GLsizei n, GLuint* names) {
    raw_fGenBuffers(n, names);
    TRACKING_CONTEXT(CreatedBuffers(this, n, names));
  }

  void fGenFramebuffers(GLsizei n, GLuint* names) {
    raw_fGenFramebuffers(n, names);
    TRACKING_CONTEXT(CreatedFramebuffers(this, n, names));
  }

  void fGenRenderbuffers(GLsizei n, GLuint* names) {
    raw_fGenRenderbuffers(n, names);
    TRACKING_CONTEXT(CreatedRenderbuffers(this, n, names));
  }

  void fGenTextures(GLsizei n, GLuint* names) {
    raw_fGenTextures(n, names);
    TRACKING_CONTEXT(CreatedTextures(this, n, names));
  }

 private:
  void raw_fDeleteProgram(GLuint program) {
    BEFORE_GL_CALL;
    mSymbols.fDeleteProgram(program);
    AFTER_GL_CALL;
  }

  void raw_fDeleteShader(GLuint shader) {
    BEFORE_GL_CALL;
    mSymbols.fDeleteShader(shader);
    AFTER_GL_CALL;
  }

  void raw_fDeleteBuffers(GLsizei n, const GLuint* names) {
    BEFORE_GL_CALL;
    mSymbols.fDeleteBuffers(n, names);
    AFTER_GL_CALL;
  }

  void raw_fDeleteFramebuffers(GLsizei n, const GLuint* names) {
    BEFORE_GL_CALL;
    mSymbols.fDeleteFramebuffers(n, names);
    AFTER_GL_CALL;

    for (const auto i : IntegerRange(n)) {
      const auto fb = names[i];
      if (mCachedDrawFb == fb) {
        mCachedDrawFb = 0;
      }
      if (mCachedReadFb == fb) {
        mCachedReadFb = 0;
      }
    }
  }

  void raw_fDeleteRenderbuffers(GLsizei n, const GLuint* names) {
    BEFORE_GL_CALL;
    mSymbols.fDeleteRenderbuffers(n, names);
    AFTER_GL_CALL;
  }

  void raw_fDeleteTextures(GLsizei n, const GLuint* names) {
    BEFORE_GL_CALL;
    mSymbols.fDeleteTextures(n, names);
    AFTER_GL_CALL;
  }

 public:
  void fDeleteProgram(GLuint program) {
    raw_fDeleteProgram(program);
    TRACKING_CONTEXT(DeletedProgram(this, program));
  }

  void fDeleteShader(GLuint shader) {
    raw_fDeleteShader(shader);
    TRACKING_CONTEXT(DeletedShader(this, shader));
  }

  void fDeleteBuffers(GLsizei n, const GLuint* names) {
    raw_fDeleteBuffers(n, names);
    TRACKING_CONTEXT(DeletedBuffers(this, n, names));
  }

  void fDeleteFramebuffers(GLsizei n, const GLuint* names);

  void fDeleteRenderbuffers(GLsizei n, const GLuint* names) {
    raw_fDeleteRenderbuffers(n, names);
    TRACKING_CONTEXT(DeletedRenderbuffers(this, n, names));
  }

  void fDeleteTextures(GLsizei n, const GLuint* names) {
#ifdef XP_MACOSX
    // On the Mac the call to fDeleteTextures() triggers a flush. But it
    // happens at the wrong time, which can lead to crashes. To work around
    // this we call fFlush() explicitly ourselves, before the call to
    // fDeleteTextures(). This fixes bug 1666293.
    fFlush();
#endif
    raw_fDeleteTextures(n, names);
    TRACKING_CONTEXT(DeletedTextures(this, n, names));
  }

  // -----------------------------------------------------------------------------
  // Extension ARB_sync (GL)
 public:
  GLsync fFenceSync(GLenum condition, GLbitfield flags) {
    GLsync ret = nullptr;
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fFenceSync);
    ret = mSymbols.fFenceSync(condition, flags);
    OnSyncCall();
    AFTER_GL_CALL;
    return ret;
  }

  realGLboolean fIsSync(GLsync sync) {
    realGLboolean ret = false;
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fIsSync);
    ret = mSymbols.fIsSync(sync);
    OnSyncCall();
    AFTER_GL_CALL;
    return ret;
  }

  void fDeleteSync(GLsync sync) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fDeleteSync);
    mSymbols.fDeleteSync(sync);
    AFTER_GL_CALL;
  }

  GLenum fClientWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
    GLenum ret = 0;
    MOZ_ASSERT(sync);
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fClientWaitSync);
    ret = mSymbols.fClientWaitSync(sync, flags, timeout);
    OnSyncCall();
    AFTER_GL_CALL;
    return ret;
  }

  void fWaitSync(GLsync sync, GLbitfield flags, GLuint64 timeout) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fWaitSync);
    mSymbols.fWaitSync(sync, flags, timeout);
    AFTER_GL_CALL;
  }

  void fGetInteger64v(GLenum pname, GLint64* params) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fGetInteger64v);
    mSymbols.fGetInteger64v(pname, params);
    AFTER_GL_CALL;
  }

  void fGetSynciv(GLsync sync, GLenum pname, GLsizei bufSize, GLsizei* length,
                  GLint* values) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fGetSynciv);
    mSymbols.fGetSynciv(sync, pname, bufSize, length, values);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  // -----------------------------------------------------------------------------
  // Extension OES_EGL_image (GLES)
 public:
  void fEGLImageTargetTexture2D(GLenum target, GLeglImage image) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fEGLImageTargetTexture2D);
    mSymbols.fEGLImageTargetTexture2D(target, image);
    AFTER_GL_CALL;
    mHeavyGLCallsSinceLastFlush = true;
  }

  void fEGLImageTargetRenderbufferStorage(GLenum target, GLeglImage image) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fEGLImageTargetRenderbufferStorage);
    mSymbols.fEGLImageTargetRenderbufferStorage(target, image);
    AFTER_GL_CALL;
  }

  // -----------------------------------------------------------------------------
  // Package XXX_bind_buffer_offset
 public:
  void fBindBufferOffset(GLenum target, GLuint index, GLuint buffer,
                         GLintptr offset) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fBindBufferOffset);
    mSymbols.fBindBufferOffset(target, index, buffer, offset);
    AFTER_GL_CALL;
  }

  // -----------------------------------------------------------------------------
  // Package XXX_draw_buffers
 public:
  void fDrawBuffers(GLsizei n, const GLenum* bufs) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fDrawBuffers);
    mSymbols.fDrawBuffers(n, bufs);
    AFTER_GL_CALL;
  }

  // -----------------------------------------------------------------------------
  // Package XXX_draw_instanced
 public:
  void fDrawArraysInstanced(GLenum mode, GLint first, GLsizei count,
                            GLsizei primcount) {
    BeforeGLDrawCall();
    raw_fDrawArraysInstanced(mode, first, count, primcount);
    AfterGLDrawCall();
  }

  void fDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type,
                              const GLvoid* indices, GLsizei primcount) {
    BeforeGLDrawCall();
    raw_fDrawElementsInstanced(mode, count, type, indices, primcount);
    AfterGLDrawCall();
  }

 private:
  void raw_fDrawArraysInstanced(GLenum mode, GLint first, GLsizei count,
                                GLsizei primcount) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fDrawArraysInstanced);
    mSymbols.fDrawArraysInstanced(mode, first, count, primcount);
    AFTER_GL_CALL;
  }

  void raw_fDrawElementsInstanced(GLenum mode, GLsizei count, GLenum type,
                                  const GLvoid* indices, GLsizei primcount) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fDrawElementsInstanced);
    mSymbols.fDrawElementsInstanced(mode, count, type, indices, primcount);
    AFTER_GL_CALL;
  }

  // -----------------------------------------------------------------------------
  // Package XXX_framebuffer_blit
 public:
  // Draw/Read
  void fBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                        GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                        GLbitfield mask, GLenum filter) {
    BeforeGLDrawCall();
    BeforeGLReadCall();
    raw_fBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1, dstY1,
                         mask, filter);
    AfterGLReadCall();
    AfterGLDrawCall();
  }

 private:
  void raw_fBlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                            GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                            GLbitfield mask, GLenum filter) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fBlitFramebuffer);
    mSymbols.fBlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0, dstY0, dstX1,
                              dstY1, mask, filter);
    AFTER_GL_CALL;
  }

  // -----------------------------------------------------------------------------
  // Package XXX_framebuffer_multisample
 public:
  void fRenderbufferStorageMultisample(GLenum target, GLsizei samples,
                                       GLenum internalFormat, GLsizei width,
                                       GLsizei height) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fRenderbufferStorageMultisample);
    mSymbols.fRenderbufferStorageMultisample(target, samples, internalFormat,
                                             width, height);
    AFTER_GL_CALL;
  }

  // -----------------------------------------------------------------------------
  //  GL 3.0, GL ES 3.0 & EXT_gpu_shader4
 public:
  void fGetVertexAttribIiv(GLuint index, GLenum pname, GLint* params) {
    ASSERT_SYMBOL_PRESENT(fGetVertexAttribIiv);
    BEFORE_GL_CALL;
    mSymbols.fGetVertexAttribIiv(index, pname, params);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fGetVertexAttribIuiv(GLuint index, GLenum pname, GLuint* params) {
    ASSERT_SYMBOL_PRESENT(fGetVertexAttribIuiv);
    BEFORE_GL_CALL;
    mSymbols.fGetVertexAttribIuiv(index, pname, params);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fVertexAttribI4i(GLuint index, GLint x, GLint y, GLint z, GLint w) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fVertexAttribI4i);
    mSymbols.fVertexAttribI4i(index, x, y, z, w);
    AFTER_GL_CALL;
  }

  void fVertexAttribI4iv(GLuint index, const GLint* v) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fVertexAttribI4iv);
    mSymbols.fVertexAttribI4iv(index, v);
    AFTER_GL_CALL;
  }

  void fVertexAttribI4ui(GLuint index, GLuint x, GLuint y, GLuint z, GLuint w) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fVertexAttribI4ui);
    mSymbols.fVertexAttribI4ui(index, x, y, z, w);
    AFTER_GL_CALL;
  }

  void fVertexAttribI4uiv(GLuint index, const GLuint* v) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fVertexAttribI4uiv);
    mSymbols.fVertexAttribI4uiv(index, v);
    AFTER_GL_CALL;
  }

  void fVertexAttribIPointer(GLuint index, GLint size, GLenum type,
                             GLsizei stride, const GLvoid* offset) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fVertexAttribIPointer);
    mSymbols.fVertexAttribIPointer(index, size, type, stride, offset);
    AFTER_GL_CALL;
  }

  void fUniform1ui(GLint location, GLuint v0) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fUniform1ui);
    mSymbols.fUniform1ui(location, v0);
    AFTER_GL_CALL;
  }

  void fUniform2ui(GLint location, GLuint v0, GLuint v1) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fUniform2ui);
    mSymbols.fUniform2ui(location, v0, v1);
    AFTER_GL_CALL;
  }

  void fUniform3ui(GLint location, GLuint v0, GLuint v1, GLuint v2) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fUniform3ui);
    mSymbols.fUniform3ui(location, v0, v1, v2);
    AFTER_GL_CALL;
  }

  void fUniform4ui(GLint location, GLuint v0, GLuint v1, GLuint v2, GLuint v3) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fUniform4ui);
    mSymbols.fUniform4ui(location, v0, v1, v2, v3);
    AFTER_GL_CALL;
  }

  void fUniform1uiv(GLint location, GLsizei count, const GLuint* value) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fUniform1uiv);
    mSymbols.fUniform1uiv(location, count, value);
    AFTER_GL_CALL;
  }

  void fUniform2uiv(GLint location, GLsizei count, const GLuint* value) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fUniform2uiv);
    mSymbols.fUniform2uiv(location, count, value);
    AFTER_GL_CALL;
  }

  void fUniform3uiv(GLint location, GLsizei count, const GLuint* value) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fUniform3uiv);
    mSymbols.fUniform3uiv(location, count, value);
    AFTER_GL_CALL;
  }

  void fUniform4uiv(GLint location, GLsizei count, const GLuint* value) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fUniform4uiv);
    mSymbols.fUniform4uiv(location, count, value);
    AFTER_GL_CALL;
  }

  GLint fGetFragDataLocation(GLuint program, const GLchar* name) {
    GLint result = 0;
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fGetFragDataLocation);
    result = mSymbols.fGetFragDataLocation(program, name);
    OnSyncCall();
    AFTER_GL_CALL;
    return result;
  }

  // -----------------------------------------------------------------------------
  // Package XXX_instanced_arrays
 public:
  void fVertexAttribDivisor(GLuint index, GLuint divisor) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fVertexAttribDivisor);
    mSymbols.fVertexAttribDivisor(index, divisor);
    AFTER_GL_CALL;
  }

  // -----------------------------------------------------------------------------
  // Feature internalformat_query
 public:
  void fGetInternalformativ(GLenum target, GLenum internalformat, GLenum pname,
                            GLsizei bufSize, GLint* params) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fGetInternalformativ);
    mSymbols.fGetInternalformativ(target, internalformat, pname, bufSize,
                                  params);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  // -----------------------------------------------------------------------------
  // Package XXX_query_counter
  /**
   * XXX_query_counter:
   *  - depends on XXX_query_objects
   *  - provide all followed entry points
   *  - provide GL_TIMESTAMP
   */
 public:
  void fQueryCounter(GLuint id, GLenum target) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fQueryCounter);
    mSymbols.fQueryCounter(id, target);
    AFTER_GL_CALL;
  }

  // -----------------------------------------------------------------------------
  // Package XXX_query_objects
  /**
   * XXX_query_objects:
   *  - provide all followed entry points
   *
   * XXX_occlusion_query2:
   *  - depends on XXX_query_objects
   *  - provide ANY_SAMPLES_PASSED
   *
   * XXX_occlusion_query_boolean:
   *  - depends on XXX_occlusion_query2
   *  - provide ANY_SAMPLES_PASSED_CONSERVATIVE
   */
 public:
  void fDeleteQueries(GLsizei n, const GLuint* names) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fDeleteQueries);
    mSymbols.fDeleteQueries(n, names);
    AFTER_GL_CALL;
    TRACKING_CONTEXT(DeletedQueries(this, n, names));
  }

  void fGenQueries(GLsizei n, GLuint* names) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fGenQueries);
    mSymbols.fGenQueries(n, names);
    AFTER_GL_CALL;
    TRACKING_CONTEXT(CreatedQueries(this, n, names));
  }

  void fGetQueryiv(GLenum target, GLenum pname, GLint* params) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fGetQueryiv);
    mSymbols.fGetQueryiv(target, pname, params);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fGetQueryObjectuiv(GLuint id, GLenum pname, GLuint* params) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fGetQueryObjectuiv);
    mSymbols.fGetQueryObjectuiv(id, pname, params);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  realGLboolean fIsQuery(GLuint query) {
    realGLboolean retval = false;
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fIsQuery);
    retval = mSymbols.fIsQuery(query);
    OnSyncCall();
    AFTER_GL_CALL;
    return retval;
  }

  // -----------------------------------------------------------------------------
  // Package XXX_get_query_object_i64v
  /**
   * XXX_get_query_object_i64v:
   *  - depends on XXX_query_objects
   *  - provide the followed entry point
   */
 public:
  void fGetQueryObjecti64v(GLuint id, GLenum pname, GLint64* params) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fGetQueryObjecti64v);
    mSymbols.fGetQueryObjecti64v(id, pname, params);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fGetQueryObjectui64v(GLuint id, GLenum pname, GLuint64* params) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fGetQueryObjectui64v);
    mSymbols.fGetQueryObjectui64v(id, pname, params);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  // -----------------------------------------------------------------------------
  // Package XXX_get_query_object_iv
  /**
   * XXX_get_query_object_iv:
   *  - depends on XXX_query_objects
   *  - provide the followed entry point
   *
   * XXX_occlusion_query:
   *  - depends on XXX_get_query_object_iv
   *  - provide LOCAL_GL_SAMPLES_PASSED
   */
 public:
  void fGetQueryObjectiv(GLuint id, GLenum pname, GLint* params) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fGetQueryObjectiv);
    mSymbols.fGetQueryObjectiv(id, pname, params);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  // -----------------------------------------------------------------------------
  // GL 4.0, GL ES 3.0, ARB_transform_feedback2, NV_transform_feedback2
 public:
  void fBindBufferBase(GLenum target, GLuint index, GLuint buffer) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fBindBufferBase);
    mSymbols.fBindBufferBase(target, index, buffer);
    AFTER_GL_CALL;
  }

  void fBindBufferRange(GLenum target, GLuint index, GLuint buffer,
                        GLintptr offset, GLsizeiptr size) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fBindBufferRange);
    mSymbols.fBindBufferRange(target, index, buffer, offset, size);
    AFTER_GL_CALL;
  }

  void fGenTransformFeedbacks(GLsizei n, GLuint* ids) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fGenTransformFeedbacks);
    mSymbols.fGenTransformFeedbacks(n, ids);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fDeleteTransformFeedbacks(GLsizei n, const GLuint* ids) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fDeleteTransformFeedbacks);
    mSymbols.fDeleteTransformFeedbacks(n, ids);
    AFTER_GL_CALL;
  }

  realGLboolean fIsTransformFeedback(GLuint id) {
    realGLboolean result = false;
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fIsTransformFeedback);
    result = mSymbols.fIsTransformFeedback(id);
    OnSyncCall();
    AFTER_GL_CALL;
    return result;
  }

  void fBindTransformFeedback(GLenum target, GLuint id) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fBindTransformFeedback);
    mSymbols.fBindTransformFeedback(target, id);
    AFTER_GL_CALL;
  }

  void fBeginTransformFeedback(GLenum primitiveMode) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fBeginTransformFeedback);
    mSymbols.fBeginTransformFeedback(primitiveMode);
    AFTER_GL_CALL;
  }

  void fEndTransformFeedback() {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fEndTransformFeedback);
    mSymbols.fEndTransformFeedback();
    AFTER_GL_CALL;
  }

  void fTransformFeedbackVaryings(GLuint program, GLsizei count,
                                  const GLchar* const* varyings,
                                  GLenum bufferMode) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fTransformFeedbackVaryings);
    mSymbols.fTransformFeedbackVaryings(program, count, varyings, bufferMode);
    AFTER_GL_CALL;
  }

  void fGetTransformFeedbackVarying(GLuint program, GLuint index,
                                    GLsizei bufSize, GLsizei* length,
                                    GLsizei* size, GLenum* type, GLchar* name) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fGetTransformFeedbackVarying);
    mSymbols.fGetTransformFeedbackVarying(program, index, bufSize, length, size,
                                          type, name);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fPauseTransformFeedback() {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fPauseTransformFeedback);
    mSymbols.fPauseTransformFeedback();
    AFTER_GL_CALL;
  }

  void fResumeTransformFeedback() {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fResumeTransformFeedback);
    mSymbols.fResumeTransformFeedback();
    AFTER_GL_CALL;
  }

  void fGetIntegeri_v(GLenum param, GLuint index, GLint* values) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fGetIntegeri_v);
    mSymbols.fGetIntegeri_v(param, index, values);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fGetInteger64i_v(GLenum target, GLuint index, GLint64* data) {
    ASSERT_SYMBOL_PRESENT(fGetInteger64i_v);
    BEFORE_GL_CALL;
    mSymbols.fGetInteger64i_v(target, index, data);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  // -----------------------------------------------------------------------------
  // Package XXX_vertex_array_object
 public:
  void fBindVertexArray(GLuint array) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fBindVertexArray);
    mSymbols.fBindVertexArray(array);
    AFTER_GL_CALL;
  }

  void fDeleteVertexArrays(GLsizei n, const GLuint* arrays) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fDeleteVertexArrays);
    mSymbols.fDeleteVertexArrays(n, arrays);
    AFTER_GL_CALL;
  }

  void fGenVertexArrays(GLsizei n, GLuint* arrays) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fGenVertexArrays);
    mSymbols.fGenVertexArrays(n, arrays);
    AFTER_GL_CALL;
  }

  realGLboolean fIsVertexArray(GLuint array) {
    realGLboolean ret = false;
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fIsVertexArray);
    ret = mSymbols.fIsVertexArray(array);
    OnSyncCall();
    AFTER_GL_CALL;
    return ret;
  }

  // -----------------------------------------------------------------------------
  // Extension NV_fence
 public:
  void fGenFences(GLsizei n, GLuint* fences) {
    ASSERT_SYMBOL_PRESENT(fGenFences);
    BEFORE_GL_CALL;
    mSymbols.fGenFences(n, fences);
    AFTER_GL_CALL;
  }

  void fDeleteFences(GLsizei n, const GLuint* fences) {
    ASSERT_SYMBOL_PRESENT(fDeleteFences);
    BEFORE_GL_CALL;
    mSymbols.fDeleteFences(n, fences);
    AFTER_GL_CALL;
  }

  void fSetFence(GLuint fence, GLenum condition) {
    ASSERT_SYMBOL_PRESENT(fSetFence);
    BEFORE_GL_CALL;
    mSymbols.fSetFence(fence, condition);
    AFTER_GL_CALL;
  }

  realGLboolean fTestFence(GLuint fence) {
    realGLboolean ret = false;
    ASSERT_SYMBOL_PRESENT(fTestFence);
    BEFORE_GL_CALL;
    ret = mSymbols.fTestFence(fence);
    OnSyncCall();
    AFTER_GL_CALL;
    return ret;
  }

  void fFinishFence(GLuint fence) {
    ASSERT_SYMBOL_PRESENT(fFinishFence);
    BEFORE_GL_CALL;
    mSymbols.fFinishFence(fence);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  realGLboolean fIsFence(GLuint fence) {
    realGLboolean ret = false;
    ASSERT_SYMBOL_PRESENT(fIsFence);
    BEFORE_GL_CALL;
    ret = mSymbols.fIsFence(fence);
    OnSyncCall();
    AFTER_GL_CALL;
    return ret;
  }

  void fGetFenceiv(GLuint fence, GLenum pname, GLint* params) {
    ASSERT_SYMBOL_PRESENT(fGetFenceiv);
    BEFORE_GL_CALL;
    mSymbols.fGetFenceiv(fence, pname, params);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  // -----------------------------------------------------------------------------
  // Extension NV_texture_barrier
 public:
  void fTextureBarrier() {
    ASSERT_SYMBOL_PRESENT(fTextureBarrier);
    BEFORE_GL_CALL;
    mSymbols.fTextureBarrier();
    AFTER_GL_CALL;
  }

  // Core GL & Extension ARB_copy_buffer
 public:
  void fCopyBufferSubData(GLenum readtarget, GLenum writetarget,
                          GLintptr readoffset, GLintptr writeoffset,
                          GLsizeiptr size) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fCopyBufferSubData);
    mSymbols.fCopyBufferSubData(readtarget, writetarget, readoffset,
                                writeoffset, size);
    AFTER_GL_CALL;
  }

  // Core GL & Extension ARB_copy_image
 public:
  void fCopyImageSubData(GLuint srcName, GLenum srcTarget, GLint srcLevel,
                         GLint srcX, GLint srcY, GLint srcZ, GLuint dstName,
                         GLenum dstTarget, GLint dstLevel, GLint dstX,
                         GLint dstY, GLint dstZ, GLsizei srcWidth,
                         GLsizei srcHeight, GLsizei srcDepth) {
    BEFORE_GL_CALL;
    mSymbols.fCopyImageSubData(srcName, srcTarget, srcLevel, srcX, srcY, srcZ,
                               dstName, dstTarget, dstLevel, dstX, dstY, dstZ,
                               srcWidth, srcHeight, srcDepth);
    AFTER_GL_CALL;
  }

  // -----------------------------------------------------------------------------
  // Core GL & Extension ARB_map_buffer_range
 public:
  void* fMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length,
                        GLbitfield access) {
    void* data = nullptr;
    ASSERT_SYMBOL_PRESENT(fMapBufferRange);
    BEFORE_GL_CALL;
    data = mSymbols.fMapBufferRange(target, offset, length, access);
    OnSyncCall();
    AFTER_GL_CALL;
    return data;
  }

  void fFlushMappedBufferRange(GLenum target, GLintptr offset,
                               GLsizeiptr length) {
    ASSERT_SYMBOL_PRESENT(fFlushMappedBufferRange);
    BEFORE_GL_CALL;
    mSymbols.fFlushMappedBufferRange(target, offset, length);
    AFTER_GL_CALL;
  }

  // -----------------------------------------------------------------------------
  // Core GL & Extension ARB_sampler_objects
 public:
  void fGenSamplers(GLsizei count, GLuint* samplers) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fGenSamplers);
    mSymbols.fGenSamplers(count, samplers);
    AFTER_GL_CALL;
  }

  void fDeleteSamplers(GLsizei count, const GLuint* samplers) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fDeleteSamplers);
    mSymbols.fDeleteSamplers(count, samplers);
    AFTER_GL_CALL;
  }

  realGLboolean fIsSampler(GLuint sampler) {
    realGLboolean result = false;
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fIsSampler);
    result = mSymbols.fIsSampler(sampler);
    OnSyncCall();
    AFTER_GL_CALL;
    return result;
  }

  void fBindSampler(GLuint unit, GLuint sampler) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fBindSampler);
    mSymbols.fBindSampler(unit, sampler);
    AFTER_GL_CALL;
  }

  void fSamplerParameteri(GLuint sampler, GLenum pname, GLint param) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fSamplerParameteri);
    mSymbols.fSamplerParameteri(sampler, pname, param);
    AFTER_GL_CALL;
  }

  void fSamplerParameteriv(GLuint sampler, GLenum pname, const GLint* param) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fSamplerParameteriv);
    mSymbols.fSamplerParameteriv(sampler, pname, param);
    AFTER_GL_CALL;
  }

  void fSamplerParameterf(GLuint sampler, GLenum pname, GLfloat param) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fSamplerParameterf);
    mSymbols.fSamplerParameterf(sampler, pname, param);
    AFTER_GL_CALL;
  }

  void fSamplerParameterfv(GLuint sampler, GLenum pname, const GLfloat* param) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fSamplerParameterfv);
    mSymbols.fSamplerParameterfv(sampler, pname, param);
    AFTER_GL_CALL;
  }

  void fGetSamplerParameteriv(GLuint sampler, GLenum pname, GLint* params) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fGetSamplerParameteriv);
    mSymbols.fGetSamplerParameteriv(sampler, pname, params);
    AFTER_GL_CALL;
  }

  void fGetSamplerParameterfv(GLuint sampler, GLenum pname, GLfloat* params) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fGetSamplerParameterfv);
    mSymbols.fGetSamplerParameterfv(sampler, pname, params);
    AFTER_GL_CALL;
  }

  // -----------------------------------------------------------------------------
  // Core GL & Extension ARB_uniform_buffer_object
 public:
  void fGetUniformIndices(GLuint program, GLsizei uniformCount,
                          const GLchar* const* uniformNames,
                          GLuint* uniformIndices) {
    ASSERT_SYMBOL_PRESENT(fGetUniformIndices);
    BEFORE_GL_CALL;
    mSymbols.fGetUniformIndices(program, uniformCount, uniformNames,
                                uniformIndices);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fGetActiveUniformsiv(GLuint program, GLsizei uniformCount,
                            const GLuint* uniformIndices, GLenum pname,
                            GLint* params) {
    ASSERT_SYMBOL_PRESENT(fGetActiveUniformsiv);
    BEFORE_GL_CALL;
    mSymbols.fGetActiveUniformsiv(program, uniformCount, uniformIndices, pname,
                                  params);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  GLuint fGetUniformBlockIndex(GLuint program, const GLchar* uniformBlockName) {
    GLuint result = 0;
    ASSERT_SYMBOL_PRESENT(fGetUniformBlockIndex);
    BEFORE_GL_CALL;
    result = mSymbols.fGetUniformBlockIndex(program, uniformBlockName);
    OnSyncCall();
    AFTER_GL_CALL;
    return result;
  }

  void fGetActiveUniformBlockiv(GLuint program, GLuint uniformBlockIndex,
                                GLenum pname, GLint* params) {
    ASSERT_SYMBOL_PRESENT(fGetActiveUniformBlockiv);
    BEFORE_GL_CALL;
    mSymbols.fGetActiveUniformBlockiv(program, uniformBlockIndex, pname,
                                      params);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fGetActiveUniformBlockName(GLuint program, GLuint uniformBlockIndex,
                                  GLsizei bufSize, GLsizei* length,
                                  GLchar* uniformBlockName) {
    ASSERT_SYMBOL_PRESENT(fGetActiveUniformBlockName);
    BEFORE_GL_CALL;
    mSymbols.fGetActiveUniformBlockName(program, uniformBlockIndex, bufSize,
                                        length, uniformBlockName);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fUniformBlockBinding(GLuint program, GLuint uniformBlockIndex,
                            GLuint uniformBlockBinding) {
    ASSERT_SYMBOL_PRESENT(fUniformBlockBinding);
    BEFORE_GL_CALL;
    mSymbols.fUniformBlockBinding(program, uniformBlockIndex,
                                  uniformBlockBinding);
    AFTER_GL_CALL;
  }

  // -----------------------------------------------------------------------------
  // Core GL 4.2, GL ES 3.0 & Extension ARB_texture_storage/EXT_texture_storage
  void fTexStorage2D(GLenum target, GLsizei levels, GLenum internalformat,
                     GLsizei width, GLsizei height) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fTexStorage2D);
    mSymbols.fTexStorage2D(target, levels, internalformat, width, height);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fTexStorage3D(GLenum target, GLsizei levels, GLenum internalformat,
                     GLsizei width, GLsizei height, GLsizei depth) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fTexStorage3D);
    mSymbols.fTexStorage3D(target, levels, internalformat, width, height,
                           depth);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  // -----------------------------------------------------------------------------
  // 3D Textures
  void fTexImage3D(GLenum target, GLint level, GLint internalFormat,
                   GLsizei width, GLsizei height, GLsizei depth, GLint border,
                   GLenum format, GLenum type, const GLvoid* data) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fTexImage3D);
    mSymbols.fTexImage3D(target, level, internalFormat, width, height, depth,
                         border, format, type, data);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                      GLint zoffset, GLsizei width, GLsizei height,
                      GLsizei depth, GLenum format, GLenum type,
                      const GLvoid* pixels) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fTexSubImage3D);
    mSymbols.fTexSubImage3D(target, level, xoffset, yoffset, zoffset, width,
                            height, depth, format, type, pixels);
    OnSyncCall();
    AFTER_GL_CALL;
  }

  void fCopyTexSubImage3D(GLenum target, GLint level, GLint xoffset,
                          GLint yoffset, GLint zoffset, GLint x, GLint y,
                          GLsizei width, GLsizei height) {
    BeforeGLReadCall();
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fCopyTexSubImage3D);
    mSymbols.fCopyTexSubImage3D(target, level, xoffset, yoffset, zoffset, x, y,
                                width, height);
    AFTER_GL_CALL;
    AfterGLReadCall();
  }

  void fCompressedTexImage3D(GLenum target, GLint level, GLenum internalformat,
                             GLsizei width, GLsizei height, GLsizei depth,
                             GLint border, GLsizei imageSize,
                             const GLvoid* data) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fCompressedTexImage3D);
    mSymbols.fCompressedTexImage3D(target, level, internalformat, width, height,
                                   depth, border, imageSize, data);
    AFTER_GL_CALL;
  }

  void fCompressedTexSubImage3D(GLenum target, GLint level, GLint xoffset,
                                GLint yoffset, GLint zoffset, GLsizei width,
                                GLsizei height, GLsizei depth, GLenum format,
                                GLsizei imageSize, const GLvoid* data) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fCompressedTexSubImage3D);
    mSymbols.fCompressedTexSubImage3D(target, level, xoffset, yoffset, zoffset,
                                      width, height, depth, format, imageSize,
                                      data);
    AFTER_GL_CALL;
  }

  // -----------------------------------------------------------------------------
  // GL3+, ES3+

  const GLubyte* fGetStringi(GLenum name, GLuint index) {
    const GLubyte* ret = nullptr;
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fGetStringi);
    ret = mSymbols.fGetStringi(name, index);
    OnSyncCall();
    AFTER_GL_CALL;
    return ret;
  }

  // -----------------------------------------------------------------------------
  // APPLE_framebuffer_multisample

  void fResolveMultisampleFramebufferAPPLE() {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fResolveMultisampleFramebufferAPPLE);
    mSymbols.fResolveMultisampleFramebufferAPPLE();
    AFTER_GL_CALL;
  }

  // -----------------------------------------------------------------------------
  // APPLE_fence

  void fFinishObjectAPPLE(GLenum object, GLint name) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fFinishObjectAPPLE);
    mSymbols.fFinishObjectAPPLE(object, name);
    AFTER_GL_CALL;
  }

  realGLboolean fTestObjectAPPLE(GLenum object, GLint name) {
    realGLboolean ret = false;
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fTestObjectAPPLE);
    ret = mSymbols.fTestObjectAPPLE(object, name);
    AFTER_GL_CALL;
    return ret;
  }

  // -----------------------------------------------------------------------------
  // prim_restart

  void fPrimitiveRestartIndex(GLuint index) {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fPrimitiveRestartIndex);
    mSymbols.fPrimitiveRestartIndex(index);
    AFTER_GL_CALL;
  }

  // -----------------------------------------------------------------------------
  // multiview

  void fFramebufferTextureMultiview(GLenum target, GLenum attachment,
                                    GLuint texture, GLint level,
                                    GLint baseViewIndex,
                                    GLsizei numViews) const {
    BEFORE_GL_CALL;
    ASSERT_SYMBOL_PRESENT(fFramebufferTextureMultiview);
    mSymbols.fFramebufferTextureMultiview(target, attachment, texture, level,
                                          baseViewIndex, numViews);
    AFTER_GL_CALL;
  }

  // -
  // draw_buffers_indexed

  void fBlendEquationSeparatei(GLuint i, GLenum modeRGB,
                               GLenum modeAlpha) const {
    BEFORE_GL_CALL;
    mSymbols.fBlendEquationSeparatei(i, modeRGB, modeAlpha);
    AFTER_GL_CALL;
  }

  void fBlendFuncSeparatei(GLuint i, GLenum sfactorRGB, GLenum dfactorRGB,
                           GLenum sfactorAlpha, GLenum dfactorAlpha) const {
    BEFORE_GL_CALL;
    mSymbols.fBlendFuncSeparatei(i, sfactorRGB, dfactorRGB, sfactorAlpha,
                                 dfactorAlpha);
    AFTER_GL_CALL;
  }

  void fColorMaski(GLuint i, realGLboolean red, realGLboolean green,
                   realGLboolean blue, realGLboolean alpha) const {
    BEFORE_GL_CALL;
    mSymbols.fColorMaski(i, red, green, blue, alpha);
    AFTER_GL_CALL;
  }

  void fDisablei(GLenum capability, GLuint i) const {
    BEFORE_GL_CALL;
    mSymbols.fDisablei(capability, i);
    AFTER_GL_CALL;
  }

  void fEnablei(GLenum capability, GLuint i) const {
    BEFORE_GL_CALL;
    mSymbols.fEnablei(capability, i);
    AFTER_GL_CALL;
  }

  // -

  void fProvokingVertex(GLenum mode) const {
    BEFORE_GL_CALL;
    mSymbols.fProvokingVertex(mode);
    AFTER_GL_CALL;
  }

  // -----------------------------------------------------------------------------
  // GL_EXT_semaphore
  void fDeleteSemaphoresEXT(GLsizei n, const GLuint* semaphores) {
    BEFORE_GL_CALL;
    mSymbols.fDeleteSemaphoresEXT(n, semaphores);
    AFTER_GL_CALL;
  }

  void fGenSemaphoresEXT(GLsizei n, GLuint* semaphores) {
    BEFORE_GL_CALL;
    mSymbols.fGenSemaphoresEXT(n, semaphores);
    AFTER_GL_CALL;
  }

  void fGetSemaphoreParameterui64vEXT(GLuint semaphore, GLenum pname,
                                      GLuint64* params) {
    BEFORE_GL_CALL;
    mSymbols.fGetSemaphoreParameterui64vEXT(semaphore, pname, params);
    AFTER_GL_CALL;
  }

  realGLboolean fIsSemaphoreEXT(GLuint semaphore) {
    realGLboolean ret = false;
    BEFORE_GL_CALL;
    ret = mSymbols.fIsSemaphoreEXT(semaphore);
    AFTER_GL_CALL;
    return ret;
  }

  void fSemaphoreParameterui64vEXT(GLuint semaphore, GLenum pname,
                                   const GLuint64* params) {
    BEFORE_GL_CALL;
    mSymbols.fSemaphoreParameterui64vEXT(semaphore, pname, params);
    AFTER_GL_CALL;
  }

  void fSignalSemaphoreEXT(GLuint semaphore, GLuint numBufferBarriers,
                           const GLuint* buffers, GLuint numTextureBarriers,
                           const GLuint* textures, const GLenum* dstLayouts) {
    BEFORE_GL_CALL;
    mSymbols.fSignalSemaphoreEXT(semaphore, numBufferBarriers, buffers,
                                 numTextureBarriers, textures, dstLayouts);
    AFTER_GL_CALL;
  }

  void fWaitSemaphoreEXT(GLuint semaphore, GLuint numBufferBarriers,
                         const GLuint* buffers, GLuint numTextureBarriers,
                         const GLuint* textures, const GLenum* srcLayouts) {
    BEFORE_GL_CALL;
    mSymbols.fWaitSemaphoreEXT(semaphore, numBufferBarriers, buffers,
                               numTextureBarriers, textures, srcLayouts);
    AFTER_GL_CALL;
  }

  // -----------------------------------------------------------------------------
  // GL_EXT_semaphore_fd
  void fImportSemaphoreFdEXT(GLuint semaphore, GLenum handleType, GLint fd) {
    BEFORE_GL_CALL;
    mSymbols.fImportSemaphoreFdEXT(semaphore, handleType, fd);
    AFTER_GL_CALL;
  }

  // -----------------------------------------------------------------------------
  // Extension EXT_memory_object
  void fGetUnsignedBytevEXT(GLenum pname, GLubyte* data) {
    BEFORE_GL_CALL;
    mSymbols.fGetUnsignedBytevEXT(pname, data);
    AFTER_GL_CALL;
  }

  void fGetUnsignedBytei_vEXT(GLenum target, GLuint index, GLubyte* data) {
    BEFORE_GL_CALL;
    mSymbols.fGetUnsignedBytei_vEXT(target, index, data);
    AFTER_GL_CALL;
  }

  void fDeleteMemoryObjectsEXT(GLsizei n, const GLuint* memoryObjects) {
    BEFORE_GL_CALL;
    mSymbols.fDeleteMemoryObjectsEXT(n, memoryObjects);
    AFTER_GL_CALL;
  }

  void fIsMemoryObjectEXT(GLuint memoryObject) {
    BEFORE_GL_CALL;
    mSymbols.fIsMemoryObjectEXT(memoryObject);
    AFTER_GL_CALL;
  }

  void fCreateMemoryObjectsEXT(GLsizei n, GLuint* memoryObjects) {
    BEFORE_GL_CALL;
    mSymbols.fCreateMemoryObjectsEXT(n, memoryObjects);
    AFTER_GL_CALL;
  }

  void fMemoryObjectParameterivEXT(GLuint memoryObject, GLenum pname,
                                   const GLint* params) {
    BEFORE_GL_CALL;
    mSymbols.fMemoryObjectParameterivEXT(memoryObject, pname, params);
    AFTER_GL_CALL;
  }

  void fGetMemoryObjectParameterivEXT(GLuint memoryObject, GLenum pname,
                                      GLint* params) {
    BEFORE_GL_CALL;
    mSymbols.fGetMemoryObjectParameterivEXT(memoryObject, pname, params);
    AFTER_GL_CALL;
  }

  void fTexStorageMem2DEXT(GLenum target, GLsizei levels, GLenum internalFormat,
                           GLsizei width, GLsizei height, GLuint memory,
                           GLuint64 offset) {
    BEFORE_GL_CALL;
    mSymbols.fTexStorageMem2DEXT(target, levels, internalFormat, width, height,
                                 memory, offset);
    AFTER_GL_CALL;
  }

  void fTexStorageMem2DMultisampleEXT(GLenum target, GLsizei samples,
                                      GLenum internalFormat, GLsizei width,
                                      GLsizei height,
                                      realGLboolean fixedSampleLocations,
                                      GLuint memory, GLuint64 offset) {
    BEFORE_GL_CALL;
    mSymbols.fTexStorageMem2DMultisampleEXT(target, samples, internalFormat,
                                            width, height, fixedSampleLocations,
                                            memory, offset);
    AFTER_GL_CALL;
  }

  void fTexStorageMem3DEXT(GLenum target, GLsizei levels, GLenum internalFormat,
                           GLsizei width, GLsizei height, GLsizei depth,
                           GLuint memory, GLuint64 offset) {
    BEFORE_GL_CALL;
    mSymbols.fTexStorageMem3DEXT(target, levels, internalFormat, width, height,
                                 depth, memory, offset);
    AFTER_GL_CALL;
  }

  void fTexStorageMem3DMultisampleEXT(GLenum target, GLsizei samples,
                                      GLenum internalFormat, GLsizei width,
                                      GLsizei height, GLsizei depth,
                                      realGLboolean fixedSampleLocations,
                                      GLuint memory, GLuint64 offset) {
    BEFORE_GL_CALL;
    mSymbols.fTexStorageMem3DMultisampleEXT(
        target, samples, internalFormat, width, height, depth,
        fixedSampleLocations, memory, offset);
    AFTER_GL_CALL;
  }

  void fBufferStorageMemEXT(GLenum target, GLsizeiptr size, GLuint memory,
                            GLuint64 offset) {
    BEFORE_GL_CALL;
    mSymbols.fBufferStorageMemEXT(target, size, memory, offset);
    AFTER_GL_CALL;
  }

  void fTextureStorageMem2DEXT(GLuint texture, GLsizei levels,
                               GLenum internalFormat, GLsizei width,
                               GLsizei height, GLuint memory, GLuint64 offset) {
    BEFORE_GL_CALL;
    mSymbols.fTextureStorageMem2DEXT(texture, levels, internalFormat, width,
                                     height, memory, offset);
    AFTER_GL_CALL;
  }

  void fTextureStorageMem2DMultisampleEXT(GLuint texture, GLsizei samples,
                                          GLenum internalFormat, GLsizei width,
                                          GLsizei height,
                                          realGLboolean fixedSampleLocations,
                                          GLuint memory, GLuint64 offset) {
    BEFORE_GL_CALL;
    mSymbols.fTextureStorageMem2DMultisampleEXT(
        texture, samples, internalFormat, width, height, fixedSampleLocations,
        memory, offset);
    AFTER_GL_CALL;
  }

  void fTextureStorageMem3DEXT(GLuint texture, GLsizei levels,
                               GLenum internalFormat, GLsizei width,
                               GLsizei height, GLsizei depth, GLuint memory,
                               GLuint64 offset) {
    BEFORE_GL_CALL;
    mSymbols.fTextureStorageMem3DEXT(texture, levels, internalFormat, width,
                                     height, depth, memory, offset);
    AFTER_GL_CALL;
  }

  void fTextureStorageMem3DMultisampleEXT(GLuint texture, GLsizei samples,
                                          GLenum internalFormat, GLsizei width,
                                          GLsizei height, GLsizei depth,
                                          realGLboolean fixedSampleLocations,
                                          GLuint memory, GLuint64 offset) {
    BEFORE_GL_CALL;
    mSymbols.fTextureStorageMem3DMultisampleEXT(
        texture, samples, internalFormat, width, height, depth,
        fixedSampleLocations, memory, offset);
    AFTER_GL_CALL;
  }

  void fNamedBufferStorageMemEXT(GLuint buffer, GLsizeiptr size, GLuint memory,
                                 GLuint64 offset) {
    BEFORE_GL_CALL;
    mSymbols.fNamedBufferStorageMemEXT(buffer, size, memory, offset);
    AFTER_GL_CALL;
  }

  // -----------------------------------------------------------------------------
  // Extension EXT_memory_object_fd

  void fImportMemoryFdEXT(GLuint memory, GLuint64 size, GLenum handleType,
                          GLint fd) {
    BEFORE_GL_CALL;
    mSymbols.fImportMemoryFdEXT(memory, size, handleType, fd);
    AFTER_GL_CALL;
  }

  // -

#undef BEFORE_GL_CALL
#undef AFTER_GL_CALL
#undef ASSERT_SYMBOL_PRESENT
// #undef TRACKING_CONTEXT // Needed in GLContext.cpp
#undef ASSERT_NOT_PASSING_STACK_BUFFER_TO_GL

  // -----------------------------------------------------------------------------
  // Constructor
 protected:
  explicit GLContext(const GLContextDesc&, GLContext* sharedContext = nullptr,
                     bool canUseTLSIsCurrent = false);

  // -----------------------------------------------------------------------------
  // Destructor
 public:
  virtual ~GLContext();

  // Mark this context as destroyed.  This will nullptr out all
  // the GL function pointers!
  void MarkDestroyed();

 protected:
  virtual void OnMarkDestroyed() {}

  // -----------------------------------------------------------------------------
  // Everything that isn't standard GL APIs
 protected:
  typedef gfx::SurfaceFormat SurfaceFormat;

 public:
  virtual void ReleaseSurface() {}

  bool IsDestroyed() const {
    // MarkDestroyed will mark all these as null.
    return mContextLost && mSymbols.fUseProgram == nullptr;
  }

  GLContext* GetSharedContext() { return mSharedContext; }

  /**
   * Returns true if the thread on which this context was created is the
   * currently executing thread.
   */
  bool IsValidOwningThread() const;

  static void PlatformStartup();

 public:
  /**
   * If this context wraps a double-buffered target, swap the back
   * and front buffers.  It should be assumed that after a swap, the
   * contents of the new back buffer are undefined.
   */
  virtual bool SwapBuffers() { return false; }

  /**
   * Stores a damage region (in origin bottom left coordinates), which
   * makes the next SwapBuffers call do eglSwapBuffersWithDamage if supported.
   *
   * Note that even if only part of the context is damaged, the entire buffer
   * needs to be filled with up-to-date contents. This region is only a hint
   * telling the system compositor which parts of the buffer were updated.
   */
  virtual void SetDamage(const nsIntRegion& aDamageRegion) {}

  /**
   * Get the buffer age. If it returns 0, that indicates the buffer state is
   * unknown and the entire frame should be redrawn.
   */
  virtual GLint GetBufferAge() const { return 0; }

  /**
   * Defines a two-dimensional texture image for context target surface
   */
  virtual bool BindTexImage() { return false; }
  /*
   * Releases a color buffer that is being used as a texture
   */
  virtual bool ReleaseTexImage() { return false; }

  virtual Maybe<SymbolLoader> GetSymbolLoader() const = 0;

  void BindFB(GLuint fb) {
    fBindFramebuffer(LOCAL_GL_FRAMEBUFFER, fb);
    MOZ_GL_ASSERT(this, !fb || fIsFramebuffer(fb));
  }

  void BindDrawFB(GLuint fb) {
    fBindFramebuffer(LOCAL_GL_DRAW_FRAMEBUFFER_EXT, fb);
  }

  void BindReadFB(GLuint fb) {
    fBindFramebuffer(LOCAL_GL_READ_FRAMEBUFFER_EXT, fb);
  }

  GLuint GetDrawFB() const {
    return GetIntAs<GLuint>(LOCAL_GL_DRAW_FRAMEBUFFER_BINDING_EXT);
  }

  GLuint GetReadFB() const {
    auto bindEnum = LOCAL_GL_READ_FRAMEBUFFER_BINDING_EXT;
    if (!IsSupported(GLFeature::split_framebuffer)) {
      bindEnum = LOCAL_GL_FRAMEBUFFER_BINDING;
    }
    return GetIntAs<GLuint>(bindEnum);
  }

  GLuint GetFB() const {
    const auto ret = GetDrawFB();
    MOZ_ASSERT(ret == GetReadFB());
    return ret;
  }

 private:
  void GetShaderPrecisionFormatNonES2(GLenum shadertype, GLenum precisiontype,
                                      GLint* range, GLint* precision) {
    switch (precisiontype) {
      case LOCAL_GL_LOW_FLOAT:
      case LOCAL_GL_MEDIUM_FLOAT:
      case LOCAL_GL_HIGH_FLOAT:
        // Assume IEEE 754 precision
        range[0] = 127;
        range[1] = 127;
        *precision = 23;
        break;
      case LOCAL_GL_LOW_INT:
      case LOCAL_GL_MEDIUM_INT:
      case LOCAL_GL_HIGH_INT:
        // Some (most) hardware only supports single-precision floating-point
        // numbers, which can accurately represent integers up to +/-16777216
        range[0] = 24;
        range[1] = 24;
        *precision = 0;
        break;
    }
  }

 public:
  virtual GLenum GetPreferredARGB32Format() const { return LOCAL_GL_RGBA; }

  virtual GLenum GetPreferredEGLImageTextureTarget() const {
#ifdef MOZ_WIDGET_GTK
    return LOCAL_GL_TEXTURE_2D;
#else
    if (IsExtensionSupported(OES_EGL_image_external) &&
        mRenderer != GLRenderer::AndroidEmulator) {
      return LOCAL_GL_TEXTURE_EXTERNAL;
    }
    return LOCAL_GL_TEXTURE_2D;
#endif
  }

  // Returns the texture target Mac IOSurfaces should be bound to
  virtual GLenum GetPreferredMacIOSurfaceTextureTarget() const {
    MOZ_CRASH("unimplemented");
  }

  virtual bool RenewSurface(widget::CompositorWidget* aWidget) { return false; }

  // Shared code for GL extensions and GLX extensions.
  static bool ListHasExtension(const GLubyte* extensions,
                               const char* extension);

 public:
  enum {
    DebugFlagEnabled = 1 << 0,
    DebugFlagTrace = 1 << 1,
    DebugFlagAbortOnError = 1 << 2
  };

  const uint8_t mDebugFlags;
  static uint8_t ChooseDebugFlags(CreateContextFlags createFlags);

 protected:
  RefPtr<GLContext> mSharedContext;

 public:
  // The thread id which this context was created.
  Maybe<PlatformThreadId> mOwningThreadId;

 protected:
  GLContextSymbols mSymbols = {};

  UniquePtr<GLBlitHelper> mBlitHelper;
  UniquePtr<GLReadTexImageHelper> mReadTexImageHelper;

 public:
  GLBlitHelper* BlitHelper();
  GLReadTexImageHelper* ReadTexImageHelper();

  // Assumes shares are created by all sharing with the same global context.
  bool SharesWith(const GLContext* other) const {
    MOZ_ASSERT(!this->mSharedContext || !this->mSharedContext->mSharedContext);
    MOZ_ASSERT(!other->mSharedContext ||
               !other->mSharedContext->mSharedContext);
    MOZ_ASSERT(!this->mSharedContext || !other->mSharedContext ||
               this->mSharedContext == other->mSharedContext);

    const GLContext* thisShared =
        this->mSharedContext ? this->mSharedContext : this;
    const GLContext* otherShared =
        other->mSharedContext ? other->mSharedContext : other;

    return thisShared == otherShared;
  }

  bool IsFramebufferComplete(GLuint fb, GLenum* status = nullptr);

  // Does not check completeness.
  void AttachBuffersToFB(GLuint colorTex, GLuint colorRB, GLuint depthRB,
                         GLuint stencilRB, GLuint fb,
                         GLenum target = LOCAL_GL_TEXTURE_2D);

  // Passing null is fine if the value you'd get is 0.
  bool AssembleOffscreenFBs(const GLuint colorMSRB, const GLuint depthRB,
                            const GLuint stencilRB, const GLuint texture,
                            GLuint* drawFB, GLuint* readFB);

 protected:
  SharedSurface* mLockedSurface = nullptr;

 public:
  void LockSurface(SharedSurface* surf) { mLockedSurface = surf; }

  void UnlockSurface(SharedSurface* surf) {
    MOZ_ASSERT(mLockedSurface == surf);
    mLockedSurface = nullptr;
  }

  SharedSurface* GetLockedSurface() const { return mLockedSurface; }

  bool IsOffscreen() const { return mDesc.isOffscreen; }

  bool WorkAroundDriverBugs() const { return mWorkAroundDriverBugs; }

  bool IsOffscreenSizeAllowed(const gfx::IntSize& aSize) const;

  virtual bool Init();

 private:
  bool InitImpl();
  void LoadMoreSymbols(const SymbolLoader& loader);
  bool LoadExtSymbols(const SymbolLoader& loader, const SymLoadStruct* list,
                      GLExtensions ext);
  bool LoadFeatureSymbols(const SymbolLoader& loader, const SymLoadStruct* list,
                          GLFeature feature);

 protected:
  void InitExtensions();

  GLint mViewportRect[4] = {};
  GLint mScissorRect[4] = {};

  uint32_t mMaxTexOrRbSize = 0;
  GLint mMaxTextureSize = 0;
  GLint mMaxCubeMapTextureSize = 0;
  GLint mMaxRenderbufferSize = 0;
  GLint mMaxViewportDims[2] = {};
  GLsizei mMaxSamples = 0;
  bool mNeedsTextureSizeChecks = false;
  bool mNeedsFlushBeforeDeleteFB = false;
  bool mTextureAllocCrashesOnMapFailure = false;
  // Amount of additional padding bytes that must be allocated for
  // GL_ARRAY_BUFFER buffers to work around driver bugs. See bug 1983036.
  Maybe<GLint> mVertexBufferExtraPadding;
  const bool mWorkAroundDriverBugs;
  mutable uint64_t mSyncGLCallCount = 0;

  bool IsTextureSizeSafeToPassToDriver(GLenum target, GLsizei width,
                                       GLsizei height) const {
    if (mNeedsTextureSizeChecks) {
      // some drivers incorrectly handle some large texture sizes that are below
      // the max texture size that they report. So we check ourselves against
      // our own values (mMax[CubeMap]TextureSize). see bug 737182 for Mac Intel
      // 2D textures see bug 684882 for Mac Intel cube map textures see bug
      // 814716 for Mesa Nouveau
      GLsizei maxSize =
          target == LOCAL_GL_TEXTURE_CUBE_MAP ||
                  (target >= LOCAL_GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
                   target <= LOCAL_GL_TEXTURE_CUBE_MAP_NEGATIVE_Z)
              ? mMaxCubeMapTextureSize
              : mMaxTextureSize;
      return width <= maxSize && height <= maxSize;
    }
    return true;
  }

 public:
  auto MaxSamples() const { return uint32_t(mMaxSamples); }
  auto MaxTextureSize() const { return uint32_t(mMaxTextureSize); }
  auto MaxRenderbufferSize() const { return uint32_t(mMaxRenderbufferSize); }
  auto MaxTexOrRbSize() const { return mMaxTexOrRbSize; }

#ifdef MOZ_GL_DEBUG_BUILD
  void CreatedProgram(GLContext* aOrigin, GLuint aName);
  void CreatedShader(GLContext* aOrigin, GLuint aName);
  void CreatedBuffers(GLContext* aOrigin, GLsizei aCount, GLuint* aNames);
  void CreatedQueries(GLContext* aOrigin, GLsizei aCount, GLuint* aNames);
  void CreatedTextures(GLContext* aOrigin, GLsizei aCount, GLuint* aNames);
  void CreatedFramebuffers(GLContext* aOrigin, GLsizei aCount, GLuint* aNames);
  void CreatedRenderbuffers(GLContext* aOrigin, GLsizei aCount, GLuint* aNames);
  void DeletedProgram(GLContext* aOrigin, GLuint aName);
  void DeletedShader(GLContext* aOrigin, GLuint aName);
  void DeletedBuffers(GLContext* aOrigin, GLsizei aCount, const GLuint* aNames);
  void DeletedQueries(GLContext* aOrigin, GLsizei aCount, const GLuint* aNames);
  void DeletedTextures(GLContext* aOrigin, GLsizei aCount,
                       const GLuint* aNames);
  void DeletedFramebuffers(GLContext* aOrigin, GLsizei aCount,
                           const GLuint* aNames);
  void DeletedRenderbuffers(GLContext* aOrigin, GLsizei aCount,
                            const GLuint* aNames);

  void SharedContextDestroyed(GLContext* aChild);
  void ReportOutstandingNames();

  struct NamedResource {
    NamedResource() : origin(nullptr), name(0), originDeleted(false) {}

    NamedResource(GLContext* aOrigin, GLuint aName)
        : origin(aOrigin), name(aName), originDeleted(false) {}

    GLContext* origin;
    GLuint name;
    bool originDeleted;

    // for sorting
    bool operator<(const NamedResource& aOther) const {
      if (intptr_t(origin) < intptr_t(aOther.origin)) return true;
      if (name < aOther.name) return true;
      return false;
    }
    bool operator==(const NamedResource& aOther) const = default;
  };

  nsTArray<NamedResource> mTrackedPrograms;
  nsTArray<NamedResource> mTrackedShaders;
  nsTArray<NamedResource> mTrackedTextures;
  nsTArray<NamedResource> mTrackedFramebuffers;
  nsTArray<NamedResource> mTrackedRenderbuffers;
  nsTArray<NamedResource> mTrackedBuffers;
  nsTArray<NamedResource> mTrackedQueries;
#endif

 protected:
  bool mHeavyGLCallsSinceLastFlush = false;

 public:
  void FlushIfHeavyGLCallsSinceLastFlush();
  static bool ShouldSpew();
  static bool ShouldDumpExts();

  // --

  void TexParams_SetClampNoMips(GLenum target) {
    fTexParameteri(target, LOCAL_GL_TEXTURE_WRAP_S, LOCAL_GL_CLAMP_TO_EDGE);
    fTexParameteri(target, LOCAL_GL_TEXTURE_WRAP_T, LOCAL_GL_CLAMP_TO_EDGE);
    fTexParameteri(target, LOCAL_GL_TEXTURE_MAG_FILTER, LOCAL_GL_NEAREST);
    fTexParameteri(target, LOCAL_GL_TEXTURE_MIN_FILTER, LOCAL_GL_NEAREST);
  }

  // --

  GLuint CreateFramebuffer() {
    GLuint x = 0;
    fGenFramebuffers(1, &x);
    return x;
  }
  GLuint CreateRenderbuffer() {
    GLuint x = 0;
    fGenRenderbuffers(1, &x);
    return x;
  }
  GLuint CreateTexture() {
    GLuint x = 0;
    fGenTextures(1, &x);
    return x;
  }

  void DeleteFramebuffer(const GLuint x) { fDeleteFramebuffers(1, &x); }
  void DeleteRenderbuffer(const GLuint x) { fDeleteRenderbuffers(1, &x); }
  void DeleteTexture(const GLuint x) { fDeleteTextures(1, &x); }
};

bool DoesStringMatch(const char* aString, const char* aWantedString);

void SplitByChar(const nsACString& str, const char delim,
                 std::vector<nsCString>* const out);

template <size_t N>
bool MarkBitfieldByString(const nsACString& str,
                          const char* const (&markStrList)[N],
                          std::bitset<N>* const out_markList) {
  for (size_t i = 0; i < N; i++) {
    if (str.Equals(markStrList[i])) {
      (*out_markList)[i] = 1;
      return true;
    }
  }
  return false;
}

template <size_t N>
void MarkBitfieldByStrings(Span<const nsCString> strList, bool dumpStrings,
                           const char* const (&markStrList)[N],
                           std::bitset<N>* const out_markList) {
  for (auto itr = strList.begin(); itr != strList.end(); ++itr) {
    const nsACString& str = *itr;
    const bool wasMarked = MarkBitfieldByString(str, markStrList, out_markList);
    if (dumpStrings)
      printf_stderr("  %s%s\n", PromiseFlatCString(str).get(),
                    wasMarked ? "(*)" : "");
  }
}

// -

class Renderbuffer final {
 public:
  const WeakPtr<GLContext> weakGl;
  const GLuint name;

 private:
  static GLuint Create(GLContext& gl) {
    GLuint ret = 0;
    gl.fGenRenderbuffers(1, &ret);
    return ret;
  }

 public:
  explicit Renderbuffer(GLContext& gl) : weakGl(&gl), name(Create(gl)) {}

  ~Renderbuffer() {
    const RefPtr<GLContext> gl = weakGl.get();
    if (!gl || !gl->MakeCurrent()) return;
    gl->fDeleteRenderbuffers(1, &name);
  }
};

// -

class Texture final {
 public:
  const WeakPtr<GLContext> weakGl;
  const GLuint name;

 private:
  static GLuint Create(GLContext& gl) {
    GLuint ret = 0;
    gl.fGenTextures(1, &ret);
    return ret;
  }

 public:
  explicit Texture(GLContext& gl) : weakGl(&gl), name(Create(gl)) {}

  ~Texture() {
    const RefPtr<GLContext> gl = weakGl.get();
    if (!gl || !gl->MakeCurrent()) return;
    gl->fDeleteTextures(1, &name);
  }
};

// -

class Sampler final {
 public:
  const WeakPtr<GLContext> weakGl;
  const GLuint name;

 private:
  static GLuint Create(GLContext& gl) {
    GLuint ret = 0;
    gl.fGenSamplers(1, &ret);
    return ret;
  }

 public:
  explicit Sampler(GLContext& gl) : weakGl(&gl), name(Create(gl)) {}

  ~Sampler() {
    const RefPtr<GLContext> gl = weakGl.get();
    if (!gl || !gl->MakeCurrent()) return;
    gl->fDeleteSamplers(1, &name);
  }
};

/**
 * Helper function that creates a 2D texture aSize.width x aSize.height with
 * storage type specified by aFormats. Returns GL texture object id.
 *
 * See mozilla::gl::CreateTexture.
 */
UniquePtr<Texture> CreateTexture(GLContext&, const gfx::IntSize& size);

/**
 * Helper function that calculates the number of bytes required per
 * texel for a texture from its format and type.
 */
uint32_t GetBytesPerTexel(GLenum format, GLenum type);

void MesaMemoryLeakWorkaround();

} /* namespace gl */
} /* namespace mozilla */

#endif /* GLCONTEXT_H_ */
