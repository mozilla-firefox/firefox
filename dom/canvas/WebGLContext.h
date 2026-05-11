/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBGLCONTEXT_H_
#define WEBGLCONTEXT_H_

#include <bitset>
#include <memory>

#include "Colorspaces.h"
#include "GLContextTypes.h"
#include "GLDefs.h"
#include "GLScreenBuffer.h"
#include "ScopedGLHelpers.h"
#include "SurfaceTypes.h"
#include "TexUnpackBlob.h"
#include "js/ScalarType.h"  // js::Scalar::Type
#include "mozilla/Atomics.h"
#include "mozilla/EnumeratedArray.h"
#include "mozilla/Mutex.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/StaticPrefs_webgl.h"
#include "mozilla/WeakPtr.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/HTMLCanvasElement.h"
#include "mozilla/dom/Nullable.h"
#include "mozilla/dom/TypedArray.h"
#include "mozilla/dom/ipc/IdType.h"
#include "mozilla/gfx/2D.h"
#include "nsICanvasRenderingContextInternal.h"
#include "nsTArray.h"

// Local
#include "CacheInvalidator.h"
#include "WebGLContextLossHandler.h"
#include "WebGLExtensions.h"
#include "WebGLObjectModel.h"
#include "WebGLStrongTypes.h"
#include "WebGLTypes.h"

// Generated
#include <list>

#include "mozilla/dom/WebGL2RenderingContextBinding.h"
#include "mozilla/dom/WebGLRenderingContextBinding.h"

class nsIDocShell;

// WebGL-only GLenums
// clang-format off
#define LOCAL_GL_BROWSER_DEFAULT_WEBGL              0x9244
#define LOCAL_GL_CONTEXT_LOST_WEBGL                 0x9242
#define LOCAL_GL_MAX_CLIENT_WAIT_TIMEOUT_WEBGL      0x9247
#define LOCAL_GL_UNPACK_COLORSPACE_CONVERSION_WEBGL 0x9243
#define LOCAL_GL_UNPACK_FLIP_Y_WEBGL                0x9240
#define LOCAL_GL_UNPACK_PREMULTIPLY_ALPHA_WEBGL     0x9241
// clang-format on

namespace mozilla {
class HostWebGLContext;
class ScopedCopyTexImageSource;
class ScopedDrawCallWrapper;
class ScopedResolveTexturesForDraw;
class WebGLBuffer;
class WebGLExtensionBase;
class WebGLFramebuffer;
class WebGLProgram;
class WebGLQuery;
class WebGLRenderbuffer;
class WebGLSampler;
class WebGLShader;
class WebGLSync;
class WebGLTexture;
class WebGLTransformFeedback;
class WebGLVertexArray;
class WebGL2Context;

namespace dom {
class Document;
class Element;
class ImageData;
class OwningHTMLCanvasElementOrOffscreenCanvas;
struct WebGLContextAttributes;
}  // namespace dom

namespace gfx {
class SourceSurface;
class VRLayerChild;
}  // namespace gfx

namespace gl {
class GLScreenBuffer;
class MozFramebuffer;
class SharedSurface;
class Sampler;
class Texture;
}  // namespace gl

namespace layers {
class CompositableHost;
class RemoteTextureOwnerClient;
class SharedSurfacesHolder;
class SurfaceDescriptor;
}  // namespace layers

namespace webgl {
class AvailabilityRunnable;
struct CachedDrawFetchLimits;
struct FbAttachInfo;
struct FormatInfo;
class FormatUsageAuthority;
struct FormatUsageInfo;
struct ImageInfo;
struct LinkedProgramInfo;
struct SamplerUniformInfo;
struct SamplingState;
class ScopedPrepForResourceClear;
class ShaderValidator;
class TexUnpackBlob;
struct UniformInfo;
struct UniformBlockInfo;
struct VertAttribPointerDesc;
}  // namespace webgl

struct WebGLTexImageData {
  TexImageTarget mTarget;
  int32_t mRowLength;
  uint32_t mWidth;
  uint32_t mHeight;
  uint32_t mDepth;
  gfxAlphaType mSrcAlphaType;
};

struct WebGLTexPboOffset {
  TexImageTarget mTarget;
  uint32_t mWidth;
  uint32_t mHeight;
  uint32_t mDepth;
  WebGLsizeiptr mPboOffset;
  bool mHasExpectedImageSize;
  GLsizei mExpectedImageSize;
};

WebGLTexelFormat GetWebGLTexelFormat(TexInternalFormat format);

void AssertUintParamCorrect(gl::GLContext* gl, GLenum pname, GLuint shadow);

// From WebGLContextUtils
TexTarget TexImageTargetToTexTarget(TexImageTarget texImageTarget);

struct WebGLIntOrFloat {
  const enum { Int, Float, Uint } mType;

  union {
    GLint i;
    GLfloat f;
    GLuint u;
  } mValue;

  explicit WebGLIntOrFloat(GLint i) : mType(Int) { mValue.i = i; }
  explicit WebGLIntOrFloat(GLfloat f) : mType(Float) { mValue.f = f; }

  GLint AsInt() const {
    return (mType == Int) ? mValue.i : NS_lroundf(mValue.f);
  }
  GLfloat AsFloat() const {
    return (mType == Float) ? mValue.f : GLfloat(mValue.i);
  }
};

////////////////////////////////////

namespace webgl {

class AvailabilityRunnable final : public DiscardableRunnable {
 public:
  const WeakPtr<const ClientWebGLContext> mWebGL;
  std::vector<WeakPtr<WebGLQueryJS>> mQueries;
  std::vector<WeakPtr<WebGLSyncJS>> mSyncs;

  explicit AvailabilityRunnable(const ClientWebGLContext* webgl);
  ~AvailabilityRunnable();

  NS_IMETHOD Run() override;
};

}  // namespace webgl

////////////////////////////////////////////////////////////////////////////////

class WebGLContext : public VRefCounted, public SupportsWeakPtr {
  friend class ScopedDrawCallWrapper;
  friend class ScopedDrawWithTransformFeedback;
  friend class ScopedFakeVertexAttrib0;
  friend class ScopedFBRebinder;
  friend class WebGL2Context;
  friend class WebGLContextUserData;
  friend class WebGLExtensionCompressedTextureASTC;
  friend class WebGLExtensionCompressedTextureBPTC;
  friend class WebGLExtensionCompressedTextureES3;
  friend class WebGLExtensionCompressedTextureETC1;
  friend class WebGLExtensionCompressedTexturePVRTC;
  friend class WebGLExtensionCompressedTextureRGTC;
  friend class WebGLExtensionCompressedTextureS3TC;
  friend class WebGLExtensionCompressedTextureS3TC_SRGB;
  friend class WebGLExtensionDepthClamp;
  friend class WebGLExtensionDepthTexture;
  friend class WebGLExtensionDisjointTimerQuery;
  friend class WebGLExtensionDrawBuffers;
  friend class WebGLExtensionFragDepth;
  friend class WebGLExtensionLoseContext;
  friend class WebGLExtensionMOZDebug;
  friend class WebGLExtensionShaderTextureLod;
  friend class WebGLExtensionVertexArray;
  friend class WebGLMemoryTracker;
  friend class webgl::AvailabilityRunnable;
  friend struct webgl::LinkedProgramInfo;
  friend struct webgl::SamplerUniformInfo;
  friend class webgl::ScopedPrepForResourceClear;
  friend struct webgl::UniformBlockInfo;

  friend const webgl::CachedDrawFetchLimits* ValidateDraw(WebGLContext*, GLenum,
                                                          uint32_t);
  friend RefPtr<const webgl::LinkedProgramInfo> QueryProgramInfo(
      WebGLProgram* prog, gl::GLContext* gl);

  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(WebGLContext, override)

  enum {
    UNPACK_FLIP_Y_WEBGL = 0x9240,
    UNPACK_PREMULTIPLY_ALPHA_WEBGL = 0x9241,
    // We throw InvalidOperation in TexImage if we fail to use GPU fast-path
    // for texture copy when it is set to true, only for debug purpose.
    UNPACK_REQUIRE_FASTPATH = 0x10001,
    CONTEXT_LOST_WEBGL = 0x9242,
    UNPACK_COLORSPACE_CONVERSION_WEBGL = 0x9243,
    BROWSER_DEFAULT_WEBGL = 0x9244,
    UNMASKED_VENDOR_WEBGL = 0x9245,
    UNMASKED_RENDERER_WEBGL = 0x9246
  };

 private:
  class LruPosition final {
    std::list<WebGLContext*>::iterator mItr;

   public:
    void AssignLocked(WebGLContext& aContext) MOZ_REQUIRES(sLruMutex);
    void Reset();
    void ResetLocked() MOZ_REQUIRES(sLruMutex);
    bool IsInsertedLocked() const MOZ_REQUIRES(sLruMutex);

    LruPosition();
    explicit LruPosition(WebGLContext&);

    ~LruPosition() { Reset(); }

    LruPosition(const LruPosition&) = delete;
    LruPosition(LruPosition&&) = delete;
    LruPosition& operator=(const LruPosition&) = delete;
    LruPosition& operator=(LruPosition&&) = delete;
  };

  mutable LruPosition mLruPosition MOZ_GUARDED_BY(sLruMutex);

  void BumpLruLocked() MOZ_REQUIRES(sLruMutex);

 public:
  void BumpLru();
  void LoseLruContextIfLimitExceeded();

  // -

  // We've had issues in the past with nulling `gl` without actually releasing
  // all of our resources. This construction ensures that we are aware that we
  // should only null `gl` in DestroyResourcesAndContext.
  RefPtr<gl::GLContext> mGL_OnlyClearInDestroyResourcesAndContext;

 public:
  // Grab a const reference so we can see changes, but can't make changes.
  const decltype(mGL_OnlyClearInDestroyResourcesAndContext)& gl;

 public:
  void CheckForInactivity();

 protected:
  const WeakPtr<HostWebGLContext> mHost;
  const bool mResistFingerprinting;
  WebGLContextOptions mOptions;
  const uint32_t mPrincipalKey;
  Maybe<webgl::Limits> mLimits;
  webgl::EnumMask<layers::SurfaceDescriptor::Type> mUploadableSdTypes;
  const uint32_t mMaxVertIdsPerDraw =
      StaticPrefs::webgl_max_vert_ids_per_draw();

  bool mIsContextLost = false;
  Atomic<bool> mPendingContextLoss = Atomic<bool>{false};
  webgl::ContextLossReason mPendingContextLossReason =
      webgl::ContextLossReason::None;
  const uint32_t mMaxPerfWarnings = StaticPrefs::webgl_perf_max_warnings();
  mutable uint64_t mNumPerfWarnings = 0;
  const uint32_t mMaxAcceptableFBStatusInvals =
      StaticPrefs::webgl_perf_max_acceptable_fb_status_invals();
  bool mWarnOnce_DepthTexCompareFilterable = true;

  mutable std::optional<bool> mIsSupportedCache_DrawBuffers;
  mutable std::optional<bool> mIsSupportedCache_FragDepth;
  mutable std::optional<bool> mIsSupportedCache_ShaderTextureLod;

  // -

  uint64_t mNextFenceId = 1;
  uint64_t mCompletedFenceId = 0;

  mutable std::list<WeakPtr<WebGLSync>> mPendingSyncs;
  mutable RefPtr<nsIRunnable> mPollPendingSyncs_Pending;
  static constexpr uint32_t kPollPendingSyncs_DelayMs =
      4;  // Four times a frame.
 public:
  void EnsurePollPendingSyncs_Pending() const;
  void PollPendingSyncs() const;

 protected:
  std::unique_ptr<gl::Texture> mIncompleteTexOverride;

 public:
  class FuncScope;

 private:
  mutable FuncScope* mFuncScope = nullptr;

 public:
  static RefPtr<WebGLContext> Create(HostWebGLContext*,
                                     const webgl::InitContextDesc&,
                                     webgl::InitContextResult* out);

 private:
  webgl::OptionalRenderableFormatBits mOptionalRenderableFormatBits =
      webgl::OptionalRenderableFormatBits{0};
  void FinishInit();
  void InitUploadableSdTypes();

 protected:
  WebGLContext(HostWebGLContext*, const webgl::InitContextDesc&);
  virtual ~WebGLContext();

  RefPtr<layers::CompositableHost> mCompositableHost;

  layers::LayersBackend mBackend = layers::LayersBackend::LAYERS_NONE;

 public:
  void Resize(uvec2 size);

  void SetCompositableHost(RefPtr<layers::CompositableHost>& aCompositableHost);

  /**
   * An abstract base class to be implemented by callers wanting to be notified
   * that a refresh has occurred. Callers must ensure an observer is removed
   * before it is destroyed.
   */
  virtual void DidRefresh();

  void OnMemoryPressure();

  // -

  /*

  Here are the bind calls that are supposed to be fully-validated client side,
  so that client's binding state doesn't diverge:
  * AttachShader
  * DetachShader
  * BindFramebuffer
  * FramebufferAttach
  * BindBuffer
  * BindBufferRange
  * BindTexture
  * UseProgram
  * BindSampler
  * BindTransformFeedback
  * BindVertexArray
  * BeginQuery
  * EndQuery
  * ActiveTexture

  */

  const auto& CurFuncScope() const { return *mFuncScope; }
  const char* FuncName() const;

  class FuncScope final {
   public:
    const WebGLContext& mWebGL;
    const char* const mFuncName;
    bool mBindFailureGuard = false;

   public:
    FuncScope(const WebGLContext& webgl, const char* funcName);
    ~FuncScope();
  };

  void GenerateErrorImpl(const GLenum err, const nsACString& text) const {
    GenerateErrorImpl(err, std::string(text.View()));
  }
  void GenerateErrorImpl(const GLenum err, const std::string& text) const;

  void GenerateError(const webgl::ErrorInfo& err) {
    GenerateError(err.type, "%s", err.info.c_str());
  }

  template <typename... Args>
  void GenerateError(const GLenum err, const char* const fmt,
                     const Args&... args) const {
    MOZ_ASSERT(FuncName());

    nsCString text;
    text.AppendPrintf("WebGL warning: %s: ", FuncName());

#ifdef __clang__
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Wformat-security"
#elif defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wformat-security"
#endif
    text.AppendPrintf(fmt, args...);
#ifdef __clang__
#  pragma clang diagnostic pop
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

    GenerateErrorImpl(err, text);
  }

  template <typename... Args>
  void ErrorInvalidEnum(const char* const fmt, const Args&... args) const {
    GenerateError(LOCAL_GL_INVALID_ENUM, fmt, args...);
  }
  template <typename... Args>
  void ErrorInvalidOperation(const char* const fmt, const Args&... args) const {
    GenerateError(LOCAL_GL_INVALID_OPERATION, fmt, args...);
  }
  template <typename... Args>
  void ErrorInvalidValue(const char* const fmt, const Args&... args) const {
    GenerateError(LOCAL_GL_INVALID_VALUE, fmt, args...);
  }
  template <typename... Args>
  void ErrorInvalidFramebufferOperation(const char* const fmt,
                                        const Args&... args) const {
    GenerateError(LOCAL_GL_INVALID_FRAMEBUFFER_OPERATION, fmt, args...);
  }
  template <typename... Args>
  void ErrorOutOfMemory(const char* const fmt, const Args&... args) const {
    GenerateError(LOCAL_GL_OUT_OF_MEMORY, fmt, args...);
  }

  template <typename... Args>
  void ErrorImplementationBug(const char* const fmt,
                              const Args&... args) const {
    const nsPrintfCString newFmt(
        "Implementation bug, please file at %s! %s",
        "https://bugzilla.mozilla.org/"
        "enter_bug.cgi?product=Core&component=Canvas%3A+WebGL",
        fmt);
    GenerateError(LOCAL_GL_OUT_OF_MEMORY, newFmt.get(), args...);
    MOZ_ASSERT(false, "WebGLContext::ErrorImplementationBug");
    NS_ERROR("WebGLContext::ErrorImplementationBug");
  }

  void ErrorInvalidEnumInfo(const char* info, GLenum enumValue) const;
  void ErrorInvalidEnumArg(const char* argName, GLenum val) const;

  static const char* ErrorName(GLenum error);

  void JsWarning(const std::string& text) const;

  /**
   * Return displayable name for GLenum.
   * This version is like gl::GLenumToStr but with out the GL_ prefix to
   * keep consistency with how errors are reported from WebGL.
   * Returns hex formatted version of glenum if glenum is unknown.
   */
  static void EnumName(GLenum val, nsCString* out_name);

  void DummyReadFramebufferOperation();

  layers::SharedSurfacesHolder* GetSharedSurfacesHolder() const;

  dom::ContentParentId GetContentId() const;

  WebGLTexture* GetActiveTex(const GLenum texTarget) const;

  gl::GLContext* GL() const { return gl; }

  bool IsPremultAlpha() const { return mOptions.premultipliedAlpha; }

  bool IsPreservingDrawingBuffer() const {
    return mOptions.preserveDrawingBuffer;
  }

  // Present to compositor
 private:
  bool PresentInto(gl::SwapChain& swapChain);
  bool PresentIntoXR(gl::SwapChain& swapChain, const gl::MozFramebuffer& xrFb);

 public:
  // Present swaps the front and back buffers of the swap chain for compositing.
  // This assumes the framebuffer may directly alias with the back buffer,
  // dependent on remoting state or other concerns. Framebuffer and swap chain
  // surface formats are assumed to be similar to enable this aliasing. As such,
  // the back buffer may be invalidated by this swap with the front buffer,
  // unless overriden by explicitly setting the preserveDrawingBuffer option,
  // which may incur a further copy to preserve the back buffer.
  void Present(
      WebGLFramebuffer*, layers::TextureType, const bool webvr,
      const webgl::SwapChainOptions& options = webgl::SwapChainOptions());
  // CopyToSwapChain forces a copy from the supplied framebuffer into the back
  // buffer before swapping the front and back buffers of the swap chain for
  // compositing. The formats of the framebuffer and the swap chain buffers
  // may differ subject to available format conversion options. Since this
  // operation uses an explicit copy, it inherently preserves the framebuffer
  // without need to set the preserveDrawingBuffer option.
  bool CopyToSwapChain(
      WebGLFramebuffer*, layers::TextureType,
      const webgl::SwapChainOptions& options = webgl::SwapChainOptions(),
      layers::RemoteTextureOwnerClient* ownerClient = nullptr);
  // In use cases where a framebuffer is used as an offscreen framebuffer and
  // does not need to be committed to the swap chain, it may still be useful
  // for the implementation to delineate distinct frames, such as when sharing
  // a single WebGLContext amongst many distinct users. EndOfFrame signals that
  // frame rendering is complete so that any implementation side-effects such
  // as resetting internal profile counters or resource queues may be handled
  // appropriately.
  void EndOfFrame();
  RefPtr<gfx::DataSourceSurface> GetFrontBufferSnapshot();
  Maybe<uvec2> FrontBufferSnapshotInto(
      const Maybe<Range<uint8_t>> dest,
      const Maybe<size_t> destStride = Nothing());
  Maybe<uvec2> FrontBufferSnapshotInto(
      const std::shared_ptr<gl::SharedSurface>& front,
      const Maybe<Range<uint8_t>> dest,
      const Maybe<size_t> destStride = Nothing());
  Maybe<uvec2> SnapshotInto(GLuint srcFb, const gfx::IntSize& size,
                            const Range<uint8_t>& dest,
                            const Maybe<size_t> destStride = Nothing());
  already_AddRefed<gfx::SourceSurface> GetBackBufferSnapshot(
      const bool requireAlphaPremult);
  std::shared_ptr<gl::SharedSurface> GetBackBufferSnapshotSharedSurface(
      layers::TextureType texType, bool bgra = false, bool yFlip = false,
      bool requireAlphaPremult = false);
  void RecycleSnapshotSharedSurface(std::shared_ptr<gl::SharedSurface>);
  gl::SwapChain* GetSwapChain(WebGLFramebuffer*, const bool webvr);
  Maybe<layers::SurfaceDescriptor> GetFrontBuffer(WebGLFramebuffer*,
                                                  const bool webvr);

  std::optional<dom::PredefinedColorSpace> mDrawingBufferColorSpace;
  std::optional<dom::PredefinedColorSpace> mUnpackColorSpace;
  std::optional<color::ColorProfileDesc> mDisplayProfile;

  void SetDrawingBufferColorSpace(const dom::PredefinedColorSpace val) {
    mDrawingBufferColorSpace = val;
  }

  void SetUnpackColorSpace(const dom::PredefinedColorSpace val) {
    mUnpackColorSpace = val;
  }

  void ClearVRSwapChain();

  void RunContextLossTimer();
  void CheckForContextLoss();
  void HandlePendingContextLoss();

  bool TryToRestoreContext();

  void AssertCachedBindings() const;
  void AssertCachedGlobalState() const;

  // WebIDL WebGLRenderingContext API
  void Commit();

  uvec2 DrawingBufferSize();

 public:
  void GetContextAttributes(dom::Nullable<dom::WebGLContextAttributes>& retval);

  // This is the entrypoint. Don't test against it directly.
  bool IsContextLost() const {
    auto* self = const_cast<WebGLContext*>(this);
    if (self->mPendingContextLoss.exchange(false)) {
      self->HandlePendingContextLoss();
    }
    return mIsContextLost;
  }

  // -

  RefPtr<WebGLBuffer> CreateBuffer();
  RefPtr<WebGLFramebuffer> CreateFramebuffer();
  RefPtr<WebGLFramebuffer> CreateOpaqueFramebuffer(
      const webgl::OpaqueFramebufferOptions& options);
  RefPtr<WebGLProgram> CreateProgram();
  RefPtr<WebGLQuery> CreateQuery();
  RefPtr<WebGLRenderbuffer> CreateRenderbuffer();
  RefPtr<WebGLShader> CreateShader(GLenum type);
  RefPtr<WebGLTexture> CreateTexture();
  RefPtr<WebGLVertexArray> CreateVertexArray();

  // -

  void AttachShader(WebGLProgram& prog, WebGLShader& shader);
  void BindAttribLocation(WebGLProgram& prog, GLuint location,
                          const std::string& name) const;
  void BindFramebuffer(GLenum target, WebGLFramebuffer* fb);
  void BindRenderbuffer(GLenum target, WebGLRenderbuffer* fb);
  void BindVertexArray(WebGLVertexArray* vao);
  void BlendColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a);
  void BlendEquationSeparate(Maybe<GLuint> i, GLenum modeRGB, GLenum modeAlpha);
  void BlendFuncSeparate(Maybe<GLuint> i, GLenum srcRGB, GLenum dstRGB,
                         GLenum srcAlpha, GLenum dstAlpha);
  GLenum CheckFramebufferStatus(GLenum target);
  void Clear(GLbitfield mask);
  void ClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a);
  void ClearDepth(GLclampf v);
  void ClearStencil(GLint v);
  void ColorMask(Maybe<GLuint> i, uint8_t mask);
  void CompileShader(WebGLShader& shader);

 private:
  void CompileShaderANGLE(WebGLShader* shader);
  void CompileShaderBypass(WebGLShader* shader, const nsCString& shaderSource);

 public:
  void CullFace(GLenum face);
  void DepthFunc(GLenum func);
  void DepthMask(WebGLboolean b);
  void DepthRange(GLclampf zNear, GLclampf zFar);
  void DetachShader(WebGLProgram& prog, const WebGLShader& shader);
  void DrawBuffers(const std::vector<GLenum>& buffers);
  void Flush();
  void Finish();

  void FramebufferAttach(GLenum target, GLenum attachSlot,
                         GLenum bindImageTarget,
                         const webgl::FbAttachInfo& toAttach);

  void FrontFace(GLenum mode);

  Maybe<double> GetBufferParameter(GLenum target, GLenum pname);
  webgl::CompileResult GetCompileResult(const WebGLShader&) const;
  GLenum GetError();
  GLint GetFragDataLocation(const WebGLProgram&, const std::string& name) const;

  Maybe<double> GetFramebufferAttachmentParameter(WebGLFramebuffer*,
                                                  GLenum attachment,
                                                  GLenum pname) const;

  Maybe<double> GetRenderbufferParameter(const WebGLRenderbuffer&,
                                         GLenum pname) const;
  webgl::LinkResult GetLinkResult(const WebGLProgram&) const;

  webgl::GetUniformData GetUniform(const WebGLProgram&, uint32_t loc) const;

  void Hint(GLenum target, GLenum mode);

  void LineWidth(GLfloat width);
  void LinkProgram(WebGLProgram& prog);
  void PolygonOffset(GLfloat factor, GLfloat units);
  void ProvokingVertex(webgl::ProvokingVertex) const;

  ////

  webgl::PackingInfo ValidImplementationColorReadPI(
      const webgl::FormatUsageInfo* usage) const;

 protected:
  webgl::ReadPixelsResult ReadPixelsImpl(const webgl::ReadPixelsDesc&,
                                         uintptr_t dest, uint64_t availBytes);
  bool DoReadPixelsAndConvert(const webgl::FormatInfo* srcFormat,
                              const webgl::ReadPixelsDesc&, uintptr_t dest,
                              uint64_t dataLen, uint32_t rowStride);

 public:
  void ReadPixelsPbo(const webgl::ReadPixelsDesc&, uint64_t offset);
  webgl::ReadPixelsResult ReadPixelsInto(const webgl::ReadPixelsDesc&,
                                         const Range<uint8_t>& dest);

  ////

  void RenderbufferStorageMultisample(WebGLRenderbuffer&, uint32_t samples,
                                      GLenum internalformat, uint32_t width,
                                      uint32_t height) const;

 public:
  void SampleCoverage(GLclampf value, WebGLboolean invert);
  void Scissor(GLint x, GLint y, GLsizei width, GLsizei height);
  void ShaderSource(WebGLShader& shader, const std::string& source) const;
  void StencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask);
  void StencilMaskSeparate(GLenum face, GLuint mask);
  void StencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail,
                         GLenum dppass);

  //////////////////////////

  void UniformData(uint32_t loc, bool transpose,
                   const Span<const webgl::UniformDataVal>& data) const;

  ////////////////////////////////////

  void UseProgram(WebGLProgram* prog);

  bool ValidateAttribArraySetter(uint32_t count, uint32_t arrayLength);
  bool ValidateProgram(const WebGLProgram& prog) const;
  void Viewport(GLint x, GLint y, GLsizei width, GLsizei height);

  // -----------------------------------------------------------------------------
  // Buffer Objects (WebGLContextBuffers.cpp)
  void BindBuffer(GLenum target, WebGLBuffer* buffer);
  void BindBufferRange(GLenum target, GLuint index, WebGLBuffer* buf,
                       uint64_t offset, uint64_t size);

  void BufferData(GLenum target, uint64_t dataLen, const uint8_t* data,
                  GLenum usage) const;
  void UninitializedBufferData_SizeOnly(GLenum target, uint64_t dataLen,
                                        GLenum usage) const;
  // The unsynchronized flag may allow for better performance when
  // interleaving buffer updates with draw calls. However, care must
  // be taken. This has similar semantics to glMapBufferRange's
  // GL_MAP_UNSYNCHRONIZED_BIT: the results of any pending operations
  // that reference the region of the buffer being updated are
  // undefined.
  void BufferSubData(GLenum target, uint64_t dstByteOffset, uint64_t srcDataLen,
                     const uint8_t* srcData, bool unsynchronized = false) const;

 protected:
  // bound buffer state
  RefPtr<WebGLBuffer> mBoundArrayBuffer;
  RefPtr<WebGLBuffer> mBoundCopyReadBuffer;
  RefPtr<WebGLBuffer> mBoundCopyWriteBuffer;
  RefPtr<WebGLBuffer> mBoundPixelPackBuffer;
  RefPtr<WebGLBuffer> mBoundPixelUnpackBuffer;
  RefPtr<WebGLBuffer> mBoundTransformFeedbackBuffer;
  RefPtr<WebGLBuffer> mBoundUniformBuffer;

  std::vector<IndexedBufferBinding> mIndexedUniformBufferBindings;

  RefPtr<WebGLBuffer>& GetBufferSlotByTarget(GLenum target);
  RefPtr<WebGLBuffer>& GetBufferSlotByTargetIndexed(GLenum target,
                                                    GLuint index);

  // -

  void GenErrorIllegalUse(GLenum useTarget, uint32_t useId, GLenum boundTarget,
                          uint32_t boundId) const;

  bool ValidateBufferForNonTf(const WebGLBuffer&, GLenum nonTfTarget,
                              uint32_t nonTfId) const;

  bool ValidateBufferForNonTf(const WebGLBuffer* const nonTfBuffer,
                              const GLenum nonTfTarget,
                              const uint32_t nonTfId = -1) const {
    if (!nonTfBuffer) return true;
    return ValidateBufferForNonTf(*nonTfBuffer, nonTfTarget, nonTfId);
  }

  bool ValidateBuffersForTf(const WebGLTransformFeedback&,
                            const webgl::LinkedProgramInfo&) const;
  bool ValidateBuffersForTf(
      const std::vector<webgl::BufferAndIndex>& tfBuffers) const;

  // -----------------------------------------------------------------------------
  // Queries (WebGL2ContextQueries.cpp)
 protected:
  RefPtr<WebGLQuery> mQuerySlot_SamplesPassed;
  RefPtr<WebGLQuery> mQuerySlot_TFPrimsWritten;
  RefPtr<WebGLQuery> mQuerySlot_TimeElapsed;

  RefPtr<WebGLQuery>* ValidateQuerySlotByTarget(GLenum target);

 public:
  void BeginQuery(GLenum target, WebGLQuery& query);
  void EndQuery(GLenum target);
  Maybe<double> GetQueryParameter(const WebGLQuery& query, GLenum pname) const;
  void QueryCounter(WebGLQuery&) const;

  // -----------------------------------------------------------------------------
  // State and State Requests (WebGLContextState.cpp)
  void SetEnabled(GLenum cap, Maybe<GLuint> i, bool enabled);
  bool GetStencilBits(GLint* const out_stencilBits) const;

  virtual Maybe<double> GetParameter(GLenum pname);
  Maybe<std::string> GetString(GLenum pname) const;

 private:
  static StaticMutex sLruMutex;
  static std::list<WebGLContext*> sLru MOZ_GUARDED_BY(sLruMutex);

  // State tracking slots
  bool mDitherEnabled = true;
  bool mRasterizerDiscardEnabled = false;
  bool mScissorTestEnabled = false;
  bool mDepthTestEnabled = false;
  bool mStencilTestEnabled = false;
  GLenum mGenerateMipmapHint = LOCAL_GL_DONT_CARE;

  struct ScissorRect final {
    GLint x;
    GLint y;
    GLsizei w;
    GLsizei h;

    void Apply(gl::GLContext&) const;
  };
  ScissorRect mScissorRect = {};

  bool* GetStateTrackingSlot(GLenum cap);

  // Allocation debugging variables
  mutable uint64_t mDataAllocGLCallCount = 0;

  void OnDataAllocCall() const { mDataAllocGLCallCount++; }

  uint64_t GetNumGLDataAllocCalls() const { return mDataAllocGLCallCount; }

  void OnEndOfFrame();

  // -----------------------------------------------------------------------------
  // Texture funcions (WebGLContextTextures.cpp)
 public:
  void ActiveTexture(uint32_t texUnit);
  void BindTexture(GLenum texTarget, WebGLTexture* tex);
  void GenerateMipmap(GLenum texTarget);

  Maybe<double> GetTexParameter(const WebGLTexture&, GLenum pname) const;
  void TexParameter_base(GLenum texTarget, GLenum pname,
                         const FloatOrInt& param);

  virtual bool IsTexParamValid(GLenum pname) const;

  ////////////////////////////////////
  // Uploads

  // CompressedTexSubImage if `sub`
  void CompressedTexImage(bool sub, GLenum imageTarget, uint32_t level,
                          GLenum format, uvec3 offset, uvec3 size,
                          const Range<const uint8_t>& src,
                          const uint32_t pboImageSize,
                          const Maybe<uint64_t>& pboOffset) const;

  // CopyTexSubImage if `!respectFormat`
  void CopyTexImage(GLenum imageTarget, uint32_t level, GLenum respecFormat,
                    uvec3 dstOffset, const ivec2& srcOffset,
                    const uvec2& size) const;

  // TexSubImage if `!respectFormat`
  void TexImage(uint32_t level, GLenum respecFormat, uvec3 offset,
                const webgl::PackingInfo& pi,
                const webgl::TexUnpackBlobDesc&) const;

  void TexStorage(GLenum texTarget, uint32_t levels, GLenum sizedFormat,
                  uvec3 size) const;

  std::unique_ptr<webgl::TexUnpackBlob> ToTexUnpackBytes(
      const WebGLTexImageData& imageData);

  std::unique_ptr<webgl::TexUnpackBytes> ToTexUnpackBytes(
      WebGLTexPboOffset& aPbo);

  ////////////////////////////////////
  // WebGLTextureUpload.cpp
 protected:
  bool ValidateTexImageSpecification(uint8_t funcDims, GLenum texImageTarget,
                                     GLint level, GLsizei width, GLsizei height,
                                     GLsizei depth, GLint border,
                                     TexImageTarget* const out_target,
                                     WebGLTexture** const out_texture,
                                     webgl::ImageInfo** const out_imageInfo);
  bool ValidateTexImageSelection(uint8_t funcDims, GLenum texImageTarget,
                                 GLint level, GLint xOffset, GLint yOffset,
                                 GLint zOffset, GLsizei width, GLsizei height,
                                 GLsizei depth,
                                 TexImageTarget* const out_target,
                                 WebGLTexture** const out_texture,
                                 webgl::ImageInfo** const out_imageInfo);
  bool ValidateUnpackInfo(bool usePBOs, GLenum format, GLenum type,
                          webgl::PackingInfo* const out);

  // -----------------------------------------------------------------------------
  // Vertices Feature (WebGLContextVertices.cpp)
  GLenum mPrimRestartTypeBytes = 0;

 public:
  void DrawArraysInstanced(GLenum mode, GLint first, GLsizei vertexCount,
                           GLsizei instanceCount);
  void DrawElementsInstanced(GLenum mode, GLsizei vertexCount, GLenum type,
                             WebGLintptr byteOffset, GLsizei instanceCount);

  void EnableVertexAttribArray(GLuint index);
  void DisableVertexAttribArray(GLuint index);

  Maybe<double> GetVertexAttrib(GLuint index, GLenum pname);

  ////

  void VertexAttrib4T(GLuint index, const webgl::TypedQuad&);

  ////

  void VertexAttribPointer(uint32_t index, const webgl::VertAttribPointerDesc&);

  void VertexAttribDivisor(GLuint index, GLuint divisor);

 private:
  WebGLBuffer* DrawElements_check(GLsizei indexCount, GLenum type,
                                  WebGLintptr byteOffset,
                                  GLsizei instanceCount);
  void Draw_cleanup();

  void VertexAttrib1fv_base(GLuint index, uint32_t arrayLength,
                            const GLfloat* ptr);
  void VertexAttrib2fv_base(GLuint index, uint32_t arrayLength,
                            const GLfloat* ptr);
  void VertexAttrib3fv_base(GLuint index, uint32_t arrayLength,
                            const GLfloat* ptr);
  void VertexAttrib4fv_base(GLuint index, uint32_t arrayLength,
                            const GLfloat* ptr);

  bool BindArrayAttribToLocation0(WebGLProgram* prog);

  // -----------------------------------------------------------------------------
  // PROTECTED
 protected:
  WebGLVertexAttrib0Status WhatDoesVertexAttrib0Need() const;
  bool DoFakeVertexAttrib0(uint64_t fakeVertexCount,
                           WebGLVertexAttrib0Status whatDoesAttrib0Need);

  bool mResetLayer = true;
  bool mOptionsFrozen = false;
  bool mIsMesa = false;
  bool mLoseContextOnMemoryPressure = false;
  bool mCanLoseContextInForeground = true;
  bool mShouldPresent = false;
  bool mDisableFragHighP = false;
  bool mForceResizeOnPresent = false;
  bool mVRReady = false;

  template <typename WebGLObjectType>
  void DeleteWebGLObjectsArray(nsTArray<WebGLObjectType>& array);

  GLuint mActiveTexture = 0;
  GLenum mDefaultFB_DrawBuffer0 = LOCAL_GL_BACK;
  GLenum mDefaultFB_ReadBuffer = LOCAL_GL_BACK;

  mutable GLenum mWebGLError = 0;

  std::unique_ptr<webgl::ShaderValidator> CreateShaderValidator(
      GLenum shaderType) const;

  // some GL constants
  uint32_t mGLMaxFragmentUniformVectors = 0;
  uint32_t mGLMaxVertexUniformVectors = 0;
  uint32_t mGLMaxVertexOutputVectors = 0;
  uint32_t mGLMaxFragmentInputVectors = 0;

  uint32_t mGLMaxVertexTextureImageUnits = 0;
  uint32_t mGLMaxFragmentTextureImageUnits = 0;
  uint32_t mGLMaxCombinedTextureImageUnits = 0;

  // ES3:
  uint32_t mGLMinProgramTexelOffset = 0;
  uint32_t mGLMaxProgramTexelOffset = 0;

 public:
  auto GLMaxDrawBuffers() const { return mLimits->maxColorDrawBuffers; }

  uint32_t MaxValidDrawBuffers() const {
    if (IsWebGL2() ||
        IsExtensionEnabled(WebGLExtensionID::WEBGL_draw_buffers)) {
      return GLMaxDrawBuffers();
    }
    return 1;
  }

  GLenum LastColorAttachmentEnum() const {
    return LOCAL_GL_COLOR_ATTACHMENT0 + MaxValidDrawBuffers() - 1;
  }

  const auto& Options() const { return mOptions; }

 protected:
  uint32_t mGLMaxRenderbufferSize = 0;

 public:
  const auto& Limits() const { return *mLimits; }
  auto MaxVertexAttribs() const { return mLimits->maxVertexAttribs; }
  auto GLMaxTextureUnits() const { return mLimits->maxTexUnits; }

  bool IsFormatValidForFB(TexInternalFormat format) const;

  bool IsUploadableSdType(const layers::SurfaceDescriptor& sd) const;

 protected:
  // -------------------------------------------------------------------------
  // WebGL extensions (implemented in WebGLContextExtensions.cpp)

  EnumeratedArray<WebGLExtensionID, std::unique_ptr<WebGLExtensionBase>,
                  size_t(WebGLExtensionID::Max)>
      mExtensions;

 public:
  void RequestExtension(WebGLExtensionID, bool explicitly = true);

  // returns true if the extension has been enabled by calling getExtension.
  bool IsExtensionEnabled(const WebGLExtensionID id) const {
    return bool(mExtensions[id]);
  }

  bool IsExtensionExplicit(WebGLExtensionID) const;
  void WarnIfImplicit(WebGLExtensionID) const;

  bool IsExtensionSupported(WebGLExtensionID) const;

  // -------------------------------------------------------------------------
  // WebGL 2 specifics (implemented in WebGL2Context.cpp)
 public:
  virtual bool IsWebGL2() const { return false; }

  virtual WebGL2Context* AsWebGL2() { return nullptr; }

  struct FailureReason {
    nsCString key;  // For reporting.
    nsCString info;

    FailureReason() = default;

    template <typename A, typename B>
    FailureReason(const A& _key, const B& _info)
        : key(nsCString(_key)), info(nsCString(_info)) {}
  };

 protected:
  bool InitWebGL2(FailureReason* const out_failReason);

  bool CreateAndInitGL(bool forceEnabled,
                       std::vector<FailureReason>* const out_failReasons);

  // -------------------------------------------------------------------------
  // Validation functions (implemented in WebGLContextValidate.cpp)
  bool InitAndValidateGL(FailureReason* const out_failReason);

  bool ValidateBlendEquationEnum(GLenum cap, const char* info);
  bool ValidateBlendFuncEnumsCompatibility(GLenum sfactor, GLenum dfactor,
                                           const char* info);
  bool ValidateStencilOpEnum(GLenum action, const char* info);
  bool ValidateFaceEnum(GLenum face);
  bool ValidateTexInputData(GLenum type, js::Scalar::Type jsArrayType,
                            WebGLTexImageFunc func, WebGLTexDimensions dims);
  bool ValidateAttribPointer(bool integerMode, GLuint index, GLint size,
                             GLenum type, WebGLboolean normalized,
                             GLsizei stride, WebGLintptr byteOffset,
                             const char* info);
  bool ValidateStencilParamsForDrawCall() const;

  bool ValidateCopyTexImage(TexInternalFormat srcFormat,
                            TexInternalFormat dstformat, WebGLTexImageFunc func,
                            WebGLTexDimensions dims);

  bool ValidateTexImage(TexImageTarget texImageTarget, GLint level,
                        GLenum internalFormat, GLint xoffset, GLint yoffset,
                        GLint zoffset, GLint width, GLint height, GLint depth,
                        GLint border, GLenum format, GLenum type,
                        WebGLTexImageFunc func, WebGLTexDimensions dims);
  bool ValidateTexImageFormat(GLenum internalFormat, WebGLTexImageFunc func,
                              WebGLTexDimensions dims);
  bool ValidateTexImageType(GLenum type, WebGLTexImageFunc func,
                            WebGLTexDimensions dims);
  bool ValidateTexImageFormatAndType(GLenum format, GLenum type,
                                     WebGLTexImageFunc func,
                                     WebGLTexDimensions dims);
  bool ValidateCompTexImageInternalFormat(GLenum format, WebGLTexImageFunc func,
                                          WebGLTexDimensions dims);
  bool ValidateCopyTexImageInternalFormat(GLenum format, WebGLTexImageFunc func,
                                          WebGLTexDimensions dims);
  bool ValidateTexImageSize(TexImageTarget texImageTarget, GLint level,
                            GLint width, GLint height, GLint depth,
                            WebGLTexImageFunc func, WebGLTexDimensions dims);
  bool ValidateTexSubImageSize(GLint x, GLint y, GLint z, GLsizei width,
                               GLsizei height, GLsizei depth, GLsizei baseWidth,
                               GLsizei baseHeight, GLsizei baseDepth,
                               WebGLTexImageFunc func, WebGLTexDimensions dims);
  bool ValidateCompTexImageSize(GLint level, GLenum internalFormat,
                                GLint xoffset, GLint yoffset, GLsizei width,
                                GLsizei height, GLsizei levelWidth,
                                GLsizei levelHeight, WebGLTexImageFunc func,
                                WebGLTexDimensions dims);
  bool ValidateCompTexImageDataSize(GLint level, GLenum internalFormat,
                                    GLsizei width, GLsizei height,
                                    uint32_t byteLength, WebGLTexImageFunc func,
                                    WebGLTexDimensions dims);

  bool HasDrawBuffers() const {
    return IsWebGL2() ||
           IsExtensionEnabled(WebGLExtensionID::WEBGL_draw_buffers);
  }

  RefPtr<WebGLBuffer>* ValidateBufferSlot(GLenum target);

 public:
  WebGLBuffer* ValidateBufferSelection(GLenum target) const;

 protected:
  IndexedBufferBinding* ValidateIndexedBufferSlot(GLenum target, GLuint index);

  bool ValidateIndexedBufferBinding(
      GLenum target, GLuint index,
      RefPtr<WebGLBuffer>** const out_genericBinding,
      IndexedBufferBinding** const out_indexedBinding);

 public:
  bool ValidateNonNegative(const char* argName, int64_t val) const {
    if (val < 0) [[unlikely]] {
      ErrorInvalidValue("`%s` must be non-negative.", argName);
      return false;
    }
    return true;
  }

  template <typename T>
  bool ValidateNonNull(const char* const argName,
                       const dom::Nullable<T>& maybe) const {
    if (maybe.IsNull()) {
      ErrorInvalidValue("%s: Cannot be null.", argName);
      return false;
    }
    return true;
  }

  ////

 protected:
  void DestroyResourcesAndContext();

  // helpers

  bool ConvertImage(size_t width, size_t height, size_t srcStride,
                    size_t dstStride, const uint8_t* src, uint8_t* dst,
                    WebGLTexelFormat srcFormat, bool srcPremultiplied,
                    WebGLTexelFormat dstFormat, bool dstPremultiplied,
                    size_t dstTexelSize);

  //////
 public:
  template <typename T>
  bool ValidateObject(const char* const argName, const T& object) const {
    // Todo: Remove all callers.
    return true;
  }

  template <typename T>
  bool ValidateObject(const char* const argName, const T* const object) const {
    // Todo: Remove most (all?) callers.
    if (!object) {
      ErrorInvalidOperation(
          "%s: Object argument cannot have been marked for"
          " deletion.",
          argName);
      return false;
    }
    return true;
  }

  ////

 private:
  void LoseContextLruLocked(webgl::ContextLossReason reason)
      MOZ_REQUIRES(sLruMutex);

 public:
  void LoseContext(
      webgl::ContextLossReason reason = webgl::ContextLossReason::None);

 protected:
  nsTArray<RefPtr<WebGLTexture>> mBound2DTextures;
  nsTArray<RefPtr<WebGLTexture>> mBoundCubeMapTextures;
  nsTArray<RefPtr<WebGLTexture>> mBound3DTextures;
  nsTArray<RefPtr<WebGLTexture>> mBound2DArrayTextures;
  nsTArray<RefPtr<WebGLSampler>> mBoundSamplers;

  mutable std::unique_ptr<gl::Sampler> mSamplerLinear;

  GLuint SamplerLinear() const;

  void ResolveTexturesForDraw() const;

  RefPtr<WebGLProgram> mCurrentProgram;
  RefPtr<const webgl::LinkedProgramInfo> mActiveProgramLinkInfo;

  bool ValidateFramebufferTarget(GLenum target) const;
  bool ValidateInvalidateFramebuffer(GLenum target,
                                     const Span<const GLenum>& attachments,
                                     std::vector<GLenum>* const scopedVector,
                                     GLsizei* const out_glNumAttachments,
                                     const GLenum** const out_glAttachments);

  RefPtr<WebGLFramebuffer> mBoundDrawFramebuffer;
  RefPtr<WebGLFramebuffer> mBoundReadFramebuffer;
  RefPtr<WebGLTransformFeedback> mBoundTransformFeedback;
  RefPtr<WebGLVertexArray> mBoundVertexArray;

 public:
  const auto& BoundReadFb() const { return mBoundReadFramebuffer; }

 protected:
  RefPtr<WebGLTransformFeedback> mDefaultTransformFeedback;
  RefPtr<WebGLVertexArray> mDefaultVertexArray;

  ////////////////////////////////////

 protected:
  GLuint mEmptyTFO = 0;

  // Generic Vertex Attributes
  // Though CURRENT_VERTEX_ATTRIB is listed under "Vertex Shader State" in the
  // spec state tables, this isn't vertex shader /object/ state. This array is
  // merely state useful to vertex shaders, but is global state.
  std::vector<webgl::AttribBaseType> mGenericVertexAttribTypes;
  CacheInvalidator mGenericVertexAttribTypeInvalidator;

  GLuint mFakeVertexAttrib0BufferObject = 0;
  intptr_t mFakeVertexAttrib0BufferAllocSize = 0;
  intptr_t mFakeVertexAttrib0BufferInitializedSize = 0;
  alignas(alignof(float)) uint8_t
      mGenericVertexAttrib0Data[sizeof(float) * 4] = {};
  alignas(alignof(float)) uint8_t
      mFakeVertexAttrib0Data[sizeof(float) * 4] = {};

  GLint mStencilRefFront = 0;
  GLint mStencilRefBack = 0;
  GLuint mStencilValueMaskFront = 0;
  GLuint mStencilValueMaskBack = 0;
  GLuint mStencilWriteMaskFront = 0;
  GLuint mStencilWriteMaskBack = 0;
  uint8_t mColorWriteMask0 = 0xf;  // bitmask
  mutable uint8_t mDriverColorMask0 = 0xf;
  bool mDepthWriteMask = true;
  GLfloat mColorClearValue[4] = {0, 0, 0, 0};
  GLint mStencilClearValue = 0;
  GLfloat mDepthClearValue = 1.0f;

  std::bitset<webgl::kMaxDrawBuffers> mColorWriteMaskNonzero = -1;
  std::bitset<webgl::kMaxDrawBuffers> mBlendEnabled = 0;

  std::unordered_set<GLenum> mIsEnabledMapKeys;

  GLint mViewportX = 0;
  GLint mViewportY = 0;
  GLsizei mViewportWidth = 0;
  GLsizei mViewportHeight = 0;
  bool mAlreadyWarnedAboutViewportLargerThanDest = false;

  GLfloat mLineWidth = 1.0;

  WebGLContextLossHandler mContextLossHandler;

  // Used for some hardware (particularly Tegra 2 and 4) that likes to
  // be Flushed while doing hundreds of draw calls.
  mutable uint64_t mDrawCallsSinceLastFlush = 0;

  mutable uint64_t mWarningCount = 0;
  const uint64_t mMaxWarnings = StaticPrefs::webgl_max_warnings_per_context();
  bool mAlreadyWarnedAboutFakeVertexAttrib0 = false;

  bool ShouldGenerateWarnings() const { return mWarningCount < mMaxWarnings; }

  bool ShouldGeneratePerfWarnings() const {
    return mNumPerfWarnings < mMaxPerfWarnings;
  }

  bool mNeedsFakeNoAlpha = false;
  bool mNeedsFakeNoDepth = false;
  bool mNeedsFakeNoStencil = false;

  bool mDriverDepthTest = false;
  bool mDriverStencilTest = false;

  bool mNeedsLegacyVertexAttrib0Handling = false;
  bool mMaybeNeedsLegacyVertexAttrib0Handling = false;
  bool mNeedsIndexValidation = false;
  bool mBug_DrawArraysInstancedUserAttribFetchAffectedByFirst = false;

  const bool mAllowFBInvalidation = StaticPrefs::webgl_allow_fb_invalidation();

  bool Has64BitTimestamps() const;

  // --

  const uint8_t mMsaaSamples =
      static_cast<uint8_t>(StaticPrefs::webgl_msaa_samples());
  mutable uvec2 mRequestedSize;
  mutable UniquePtr<gl::MozFramebuffer> mDefaultFB;
  mutable bool mDefaultFB_IsInvalid = false;
  mutable UniquePtr<gl::MozFramebuffer> mResolvedDefaultFB;

  mutable std::unordered_map<std::tuple<gfx::ColorSpace2, gfx::ColorSpace2>,
                             std::shared_ptr<gl::Texture>>
      mLutTexByColorMapping;

  gl::SwapChain mSwapChain;
  gl::SwapChain mWebVRSwapChain;
  gl::SwapChain mSnapshotSwapChain;

  RefPtr<layers::RemoteTextureOwnerClient> mRemoteTextureOwner;

  bool PushRemoteTexture(
      WebGLFramebuffer*, gl::SwapChain&, std::shared_ptr<gl::SharedSurface>,
      const webgl::SwapChainOptions& options,
      layers::RemoteTextureOwnerClient* ownerClient = nullptr);

  void EnsureContextLostRemoteTextureOwner(
      const webgl::SwapChainOptions& options);

 public:
  void WaitForTxn(layers::RemoteTextureOwnerId ownerId,
                  layers::RemoteTextureTxnType txnType,
                  layers::RemoteTextureTxnId txnId);

  // --

 protected:
  bool EnsureDefaultFB();
  bool ValidateAndInitFB(
      const WebGLFramebuffer* fb,
      GLenum incompleteFbError = LOCAL_GL_INVALID_FRAMEBUFFER_OPERATION);
  void DoBindFB(const WebGLFramebuffer* fb,
                GLenum target = LOCAL_GL_FRAMEBUFFER) const;

  bool BindCurFBForDraw();
  bool BindCurFBForColorRead(
      const webgl::FormatUsageInfo** out_format, uint32_t* out_width,
      uint32_t* out_height,
      GLenum incompleteFbError = LOCAL_GL_INVALID_FRAMEBUFFER_OPERATION);
  void DoColorMask(Maybe<GLuint> i, uint8_t bitmask) const;
  void BlitBackbufferToCurDriverFB(
      WebGLFramebuffer* const srcAsWebglFb = nullptr,
      const gl::MozFramebuffer* const srcAsMozFb = nullptr,
      bool srcIsBGRA = false, bool yFlip = false,
      Maybe<gfxAlphaType> convertAlpha = {}) const;

  struct GetDefaultFBForReadDesc {
    bool endOfFrame = false;
  };
  const gl::MozFramebuffer* GetDefaultFBForRead(const GetDefaultFBForReadDesc&);
  const gl::MozFramebuffer* GetDefaultFBForRead() {
    return GetDefaultFBForRead({});
  }

  bool BindDefaultFBForRead();

  // --

 public:
  // console logging helpers
  template <typename... Args>
  void GenerateWarning(const char* const fmt, const Args&... args) const {
    GenerateError(0, fmt, args...);
  }

  template <typename... Args>
  void GeneratePerfWarning(const char* const fmt, const Args&... args) const {
    if (ShouldGeneratePerfWarnings()) {
      GenerateError(webgl::kErrorPerfWarning, fmt, args...);
    }
  }

 public:
  std::unique_ptr<webgl::FormatUsageAuthority> mFormatUsage;

  virtual std::unique_ptr<webgl::FormatUsageAuthority> CreateFormatUsage(
      gl::GLContext* gl) const;

  const decltype(mBound2DTextures)* TexListForElemType(GLenum elemType) const;

  // Friend list
  friend class ScopedCopyTexImageSource;
  friend class ScopedResolveTexturesForDraw;
  friend class webgl::TexUnpackBlob;
  friend class webgl::TexUnpackBytes;
  friend class webgl::TexUnpackSurface;
  friend struct webgl::UniformInfo;
  friend class WebGLTexture;
  friend class WebGLFBAttachPoint;
  friend class WebGLFramebuffer;
  friend class WebGLRenderbuffer;
  friend class WebGLProgram;
  friend class WebGLQuery;
  friend class WebGLBuffer;
  friend class WebGLSampler;
  friend class WebGLShader;
  friend class WebGLSync;
  friend class WebGLTransformFeedback;
  friend class WebGLVertexArray;
  friend class WebGLVertexArrayFake;
  friend class WebGLVertexArrayGL;
};

// Returns `value` rounded to the next highest multiple of `multiple`.
// AKA PadToAlignment, StrideForAlignment.
template <typename V, typename M>
V RoundUpToMultipleOf(const V& value, const M& multiple) {
  return ((value + multiple - 1) / multiple) * multiple;
}

class ScopedFBRebinder final {
 private:
  const WebGLContext* const mWebGL;

 public:
  explicit ScopedFBRebinder(const WebGLContext* const webgl) : mWebGL(webgl) {}
  ~ScopedFBRebinder();
};

// -

constexpr inline bool IsBufferTargetLazilyBound(const GLenum target) {
  return target != LOCAL_GL_ELEMENT_ARRAY_BUFFER;
}

void DoBindBuffer(gl::GLContext&, GLenum target, const WebGLBuffer*);

class ScopedLazyBind final {
 private:
  gl::GLContext& mGL;
  const GLenum mTarget;

 public:
  ScopedLazyBind(gl::GLContext* const gl, const GLenum target,
                 const WebGLBuffer* const buf)
      : mGL(*gl), mTarget(IsBufferTargetLazilyBound(target) ? target : 0) {
    if (mTarget) {
      DoBindBuffer(mGL, mTarget, buf);
    }
  }

  ~ScopedLazyBind() {
    if (mTarget) {
      DoBindBuffer(mGL, mTarget, nullptr);
    }
  }
};

////

bool Intersect(int32_t srcSize, int32_t read0, int32_t readSize,
               int32_t* out_intRead0, int32_t* out_intWrite0,
               int32_t* out_intSize);

uint64_t AvailGroups(uint64_t totalAvailItems, uint64_t firstItemOffset,
                     uint32_t groupSize, uint32_t groupStride);

////

class ScopedDrawCallWrapper final {
 public:
  WebGLContext& mWebGL;

  explicit ScopedDrawCallWrapper(WebGLContext& webgl);
  ~ScopedDrawCallWrapper();
};

namespace webgl {

class ScopedPrepForResourceClear final {
  const WebGLContext& webgl;

 public:
  explicit ScopedPrepForResourceClear(const WebGLContext&);
  ~ScopedPrepForResourceClear();
};

struct IndexedName final {
  std::string name;
  uint64_t index;
};
Maybe<IndexedName> ParseIndexed(const std::string& str);

}  // namespace webgl

webgl::LinkActiveInfo GetLinkActiveInfo(
    gl::GLContext& gl, const GLuint prog, const bool webgl2,
    const std::unordered_map<std::string, std::string>& nameUnmap);

}  // namespace mozilla

#endif
