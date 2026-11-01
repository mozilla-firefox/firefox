/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef HOSTWEBGLCONTEXT_H_
#define HOSTWEBGLCONTEXT_H_

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ClientWebGLContext.h"
#include "GLContext.h"
#include "WebGL2Context.h"
#include "WebGLCommandQueue.h"
#include "WebGLContext.h"
#include "WebGLFramebuffer.h"
#include "WebGLTypes.h"
#include "mozilla/GfxMessageUtils.h"
#include "mozilla/Maybe.h"
#include "mozilla/Mutex.h"
#include "mozilla/NotNull.h"
#include "mozilla/dom/BindingUtils.h"

namespace mozilla {

namespace dom {
class WebGLParent;
}
namespace layers {
class CompositableHost;
}

struct LockedOutstandingContexts final {
 private:
  // StaticMutexAutoLock lock; // We can't use it directly (STACK_CLASS), but
  // this is effectively what we hold via RAII.

 public:
  const std::unordered_set<HostWebGLContext*>& contexts;

  LockedOutstandingContexts();
  ~LockedOutstandingContexts();
};

/**
 * Host endpoint of a WebGLContext.  HostWebGLContext owns a WebGLContext
 * that it uses to execute commands sent from its ClientWebGLContext.
 *
 * A HostWebGLContext continuously issues a Task to the Compositor thread that
 * causes it to drain its queue of commands.  It also maintains a map of WebGL
 * objects (e.g. ObjectIdMap<WebGLShader>) that it uses associate them with
 * their cross-process IDs.
 *
 * This class is not an implementation of the
 * nsICanvasRenderingContextInternal DOM class.  That is the
 * ClientWebGLContext.
 */
class HostWebGLContext final : public SupportsWeakPtr {
  friend class WebGLContext;
  friend class WebGLMemoryTracker;
  friend class dom::WebGLParent;

  using ObjectId = webgl::ObjectId;

  static std::unique_ptr<LockedOutstandingContexts> OutstandingContexts() {
    return std::make_unique<LockedOutstandingContexts>();
  }

 public:
  static std::unique_ptr<HostWebGLContext> Create(
      dom::WebGLParent*, const webgl::InitContextDesc&,
      webgl::InitContextResult* out);

 private:
  explicit HostWebGLContext(dom::WebGLParent*);

 public:
  virtual ~HostWebGLContext();

  WebGLContext* GetWebGLContext() const { return mContext; }

 public:
  dom::WebGLParent* const mOwner;

 private:
  RefPtr<WebGLContext> mContext;

#define _(X) std::unordered_map<ObjectId, NotNull<RefPtr<WebGL##X>>> m##X##Map;

  _(Buffer)
  _(Framebuffer)
  _(Program)
  _(Query)
  _(Renderbuffer)
  _(Sampler)
  _(Shader)
  _(Sync)
  _(Texture)
  _(TransformFeedback)
  _(VertexArray)

#undef _

  class AutoResolveT final {
    friend class HostWebGLContext;

    const HostWebGLContext& mParent;
    const ObjectId mId;

   public:
    AutoResolveT(const HostWebGLContext& parent, const ObjectId id)
        : mParent(parent), mId(id) {}

#define _(X)                                              \
  WebGL##X* As(WebGL##X*) const {                         \
    const auto maybe = MaybeFind(mParent.m##X##Map, mId); \
    if (!maybe) return nullptr;                           \
    return maybe->get();                                  \
  }

    _(Buffer)
    _(Framebuffer)
    _(Program)
    _(Query)
    _(Renderbuffer)
    _(Sampler)
    _(Shader)
    _(Sync)
    _(Texture)
    _(TransformFeedback)
    _(VertexArray)

#undef _
    template <typename T>
    MOZ_IMPLICIT operator T*() const {
      T* coercer = nullptr;
      return As(coercer);
    }

    template <typename T>
    MOZ_IMPLICIT operator const T*() const {
      T* coercer = nullptr;
      return As(coercer);
    }
  };

  AutoResolveT AutoResolve(const ObjectId id) const { return {*this, id}; }
  template <typename T>
  T* ById(const ObjectId id) const {
    T* coercer = nullptr;
    return AutoResolve(id).As(coercer);
  }

  // -------------------------------------------------------------------------
  // Host-side methods.  Calls in the client are forwarded to the host.
  // -------------------------------------------------------------------------

 public:
  // ------------------------- Composition -------------------------

  void SetCompositableHost(RefPtr<layers::CompositableHost>& compositableHost) {
    mContext->SetCompositableHost(compositableHost);
  }

  void Present(const ObjectId xrFb, const layers::TextureType t,
               const bool webvr, const webgl::SwapChainOptions& options) const {
    return (void)mContext->Present(AutoResolve(xrFb), t, webvr, options);
  }
  void CopyToSwapChain(const ObjectId fb, const layers::TextureType t,
                       const webgl::SwapChainOptions& options) const {
    return (void)mContext->CopyToSwapChain(AutoResolve(fb), t, options);
  }
  void WaitForTxn(const layers::RemoteTextureOwnerId aOwnerId,
                  const layers::RemoteTextureTxnType txnType,
                  const layers::RemoteTextureTxnId txnId) {
    mContext->WaitForTxn(aOwnerId, txnType, txnId);
  }
  void EndOfFrame() const { return (void)mContext->EndOfFrame(); }
  Maybe<layers::SurfaceDescriptor> GetFrontBuffer(ObjectId xrFb,
                                                  const bool webvr) const;

  // -

  Maybe<uvec2> FrontBufferSnapshotInto(
      Maybe<mozilla::Range<uint8_t>> dest) const {
    return mContext->FrontBufferSnapshotInto(dest);
  }

  Maybe<uvec2> FrontBufferSnapshotInto(
      std::shared_ptr<gl::SharedSurface>& front,
      Maybe<mozilla::Range<uint8_t>> dest) const {
    return mContext->FrontBufferSnapshotInto(front, dest);
  }

  void ClearVRSwapChain() const { mContext->ClearVRSwapChain(); }

  // -

  void SetDrawingBufferColorSpace(const dom::PredefinedColorSpace val) const {
    mContext->SetDrawingBufferColorSpace(val);
  }
  void SetUnpackColorSpace(const dom::PredefinedColorSpace val) const {
    mContext->SetUnpackColorSpace(val);
  }
  void Resize(const uvec2& size) { return mContext->Resize(size); }

  uvec2 DrawingBufferSize() { return mContext->DrawingBufferSize(); }

  void OnMemoryPressure() { return mContext->OnMemoryPressure(); }

  void DidRefresh() { mContext->DidRefresh(); }

  void GenerateError(const GLenum error, const std::string& text) const {
    mContext->GenerateErrorImpl(error, text);
  }

  void OnContextLoss(webgl::ContextLossReason);

  void RequestExtension(const WebGLExtensionID ext) {
    mContext->RequestExtension(ext);
  }

  // -
  // Child-ward

  void JsWarning(const std::string&) const;

  // -
  // Creation and destruction

  void CreateBuffer(ObjectId);
  void CreateFramebuffer(ObjectId);
  bool CreateOpaqueFramebuffer(ObjectId,
                               const webgl::OpaqueFramebufferOptions& options);
  void CreateProgram(ObjectId);
  void CreateQuery(ObjectId);
  void CreateRenderbuffer(ObjectId);
  void CreateSampler(ObjectId);
  void CreateShader(ObjectId, GLenum type);
  void CreateSync(ObjectId);
  void CreateTexture(ObjectId);
  void CreateTransformFeedback(ObjectId);
  void CreateVertexArray(ObjectId);

  void DeleteBuffer(ObjectId);
  void DeleteFramebuffer(ObjectId);
  void DeleteProgram(ObjectId);
  void DeleteQuery(ObjectId);
  void DeleteRenderbuffer(ObjectId);
  void DeleteSampler(ObjectId);
  void DeleteShader(ObjectId);
  void DeleteSync(ObjectId);
  void DeleteTexture(ObjectId);
  void DeleteTransformFeedback(ObjectId);
  void DeleteVertexArray(ObjectId);

  // ------------------------- GL State -------------------------
  bool IsContextLost() const { return mContext->IsContextLost(); }

  void SetEnabled(GLenum cap, Maybe<GLuint> i, bool val) const {
    mContext->SetEnabled(cap, i, val);
  }

  Maybe<double> GetNumber(GLenum pname) const {
    return mContext->GetParameter(pname);
  }

  Maybe<std::string> GetString(GLenum pname) const {
    return mContext->GetString(pname);
  }

  void AttachShader(ObjectId prog, ObjectId shader) const {
    const auto pProg = ById<WebGLProgram>(prog);
    const auto pShader = ById<WebGLShader>(shader);
    if (!pProg || !pShader) return;
    mContext->AttachShader(*pProg, *pShader);
  }

  void BindAttribLocation(ObjectId id, GLuint location,
                          const std::string& name) const {
    const auto obj = ById<WebGLProgram>(id);
    if (!obj) return;
    mContext->BindAttribLocation(*obj, location, name);
  }

  void BindFramebuffer(GLenum target, ObjectId id) const {
    mContext->BindFramebuffer(target, AutoResolve(id));
  }

  void BlendColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) const {
    mContext->BlendColor(r, g, b, a);
  }

  void BlendEquationSeparate(Maybe<GLuint> i, GLenum modeRGB,
                             GLenum modeAlpha) const {
    mContext->BlendEquationSeparate(i, modeRGB, modeAlpha);
  }

  void BlendFuncSeparate(Maybe<GLuint> i, GLenum srcRGB, GLenum dstRGB,
                         GLenum srcAlpha, GLenum dstAlpha) const {
    mContext->BlendFuncSeparate(i, srcRGB, dstRGB, srcAlpha, dstAlpha);
  }

  GLenum CheckFramebufferStatus(GLenum target) const {
    return mContext->CheckFramebufferStatus(target);
  }

  void Clear(GLbitfield mask) const { mContext->Clear(mask); }

  void ClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) const {
    mContext->ClearColor(r, g, b, a);
  }

  void ClearDepth(GLclampf v) const { mContext->ClearDepth(v); }

  void ClearStencil(GLint v) const { mContext->ClearStencil(v); }

  void ColorMask(Maybe<GLuint> i, uint8_t mask) const {
    mContext->ColorMask(i, mask);
  }

  void CompileShader(const ObjectId id) const {
    const auto obj = ById<WebGLShader>(id);
    if (!obj) return;
    mContext->CompileShader(*obj);
  }

  void CullFace(GLenum face) const { mContext->CullFace(face); }

  void DepthFunc(GLenum func) const { mContext->DepthFunc(func); }

  void DepthMask(WebGLboolean b) const { mContext->DepthMask(b); }

  void DepthRange(GLclampf zNear, GLclampf zFar) const {
    mContext->DepthRange(zNear, zFar);
  }

  void DetachShader(const ObjectId prog, const ObjectId shader) const {
    const auto pProg = ById<WebGLProgram>(prog);
    const auto pShader = ById<WebGLShader>(shader);
    if (!pProg || !pShader) return;
    mContext->DetachShader(*pProg, *pShader);
  }

  void Flush() const { mContext->Flush(); }

  void Finish() const { mContext->Finish(); }

  void FramebufferAttach(const GLenum target, const GLenum attachSlot,
                         const GLenum bindImageTarget, const ObjectId id,
                         const GLint mipLevel, const GLint zLayerBase,
                         const GLsizei numViewLayers) const {
    webgl::FbAttachInfo toAttach;
    toAttach.rb = AutoResolve(id);
    toAttach.tex = AutoResolve(id);
    toAttach.mipLevel = mipLevel;
    toAttach.zLayer = zLayerBase;
    if (numViewLayers) {
      toAttach.zLayerCount = numViewLayers;
      toAttach.isMultiview = true;
    }

    mContext->FramebufferAttach(target, attachSlot, bindImageTarget, toAttach);
  }

  void FrontFace(GLenum mode) const { mContext->FrontFace(mode); }

  Maybe<double> GetBufferParameter(GLenum target, GLenum pname) const {
    return mContext->GetBufferParameter(target, pname);
  }

  webgl::CompileResult GetCompileResult(ObjectId id) const {
    const auto obj = ById<WebGLShader>(id);
    if (!obj) return {};
    return mContext->GetCompileResult(*obj);
  }

  GLenum GetError() const { return mContext->GetError(); }

  GLint GetFragDataLocation(ObjectId id, const std::string& name) const {
    const auto obj = ById<WebGLProgram>(id);
    if (!obj) return -1;
    return mContext->GetFragDataLocation(*obj, name);
  }

  Maybe<double> GetFramebufferAttachmentParameter(ObjectId id,
                                                  GLenum attachment,
                                                  GLenum pname) const {
    return mContext->GetFramebufferAttachmentParameter(AutoResolve(id),
                                                       attachment, pname);
  }

  webgl::LinkResult GetLinkResult(ObjectId id) const {
    const auto obj = ById<WebGLProgram>(id);
    if (!obj) return {};
    return mContext->GetLinkResult(*obj);
  }

  Maybe<double> GetRenderbufferParameter(ObjectId id, GLenum pname) const {
    const auto obj = ById<WebGLRenderbuffer>(id);
    if (!obj) return {};
    return mContext->GetRenderbufferParameter(*obj, pname);
  }

  webgl::GetUniformData GetUniform(ObjectId id, uint32_t loc) const {
    const auto obj = ById<WebGLProgram>(id);
    if (!obj) return {};
    return mContext->GetUniform(*obj, loc);
  }

  void Hint(GLenum target, GLenum mode) const { mContext->Hint(target, mode); }

  void LineWidth(GLfloat width) const { mContext->LineWidth(width); }

  void LinkProgram(const ObjectId id) const {
    const auto obj = ById<WebGLProgram>(id);
    if (!obj) return;
    mContext->LinkProgram(*obj);
  }

  void PolygonOffset(GLfloat factor, GLfloat units) const {
    mContext->PolygonOffset(factor, units);
  }

  void PolygonOffsetClampEXT(GLfloat factor, GLfloat units,
                             GLfloat clamp) const {
    mContext->PolygonOffsetClampEXT(factor, units, clamp);
  }

  void ClipControlEXT(GLenum origin, GLenum depth) const {
    mContext->ClipControlEXT(origin, depth);
  }

  void SampleCoverage(GLclampf value, bool invert) const {
    mContext->SampleCoverage(value, invert);
  }

  void Scissor(GLint x, GLint y, GLsizei width, GLsizei height) const {
    mContext->Scissor(x, y, width, height);
  }

  // TODO: s/nsAString/std::string/
  void ShaderSource(const ObjectId id, const std::string& source) const {
    const auto obj = ById<WebGLShader>(id);
    if (!obj) return;
    mContext->ShaderSource(*obj, source);
  }

  void StencilFuncSeparate(GLenum face, GLenum func, GLint ref,
                           GLuint mask) const {
    mContext->StencilFuncSeparate(face, func, ref, mask);
  }
  void StencilMaskSeparate(GLenum face, GLuint mask) const {
    mContext->StencilMaskSeparate(face, mask);
  }
  void StencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail,
                         GLenum dppass) const {
    mContext->StencilOpSeparate(face, sfail, dpfail, dppass);
  }

  void Viewport(GLint x, GLint y, GLsizei width, GLsizei height) const {
    mContext->Viewport(x, y, width, height);
  }

  // ------------------------- Buffer Objects -------------------------
  void BindBuffer(GLenum target, const ObjectId id) const {
    mContext->BindBuffer(target, AutoResolve(id));
  }

  void BindBufferRange(GLenum target, GLuint index, const ObjectId id,
                       uint64_t offset, uint64_t size) const {
    GetWebGL2Context()->BindBufferRange(target, index, AutoResolve(id), offset,
                                        size);
  }

  void CopyBufferSubData(GLenum readTarget, GLenum writeTarget,
                         uint64_t readOffset, uint64_t writeOffset,
                         uint64_t size) const {
    GetWebGL2Context()->CopyBufferSubData(readTarget, writeTarget, readOffset,
                                          writeOffset, size);
  }

  bool GetBufferSubData(GLenum target, uint64_t srcByteOffset,
                        const mozilla::Range<uint8_t>& dest) const {
    return GetWebGL2Context()->GetBufferSubData(target, srcByteOffset, dest);
  }

  void BufferData(GLenum target, const Span<const uint8_t>& srcData,
                  GLenum usage) const {
    mContext->BufferData(target, srcData.size(), srcData.data(), usage);
  }

  void BufferData_SizeOnly(GLenum target, size_t byteSize, GLenum usage) const {
    mContext->BufferData(target, byteSize, nullptr, usage);
  }

  void BufferSubData(GLenum target, uint64_t dstByteOffset,
                     const Span<const uint8_t>& srcData,
                     bool unsynchronized = false) const {
    mContext->BufferSubData(target, dstByteOffset, srcData.size(),
                            srcData.data(), unsynchronized);
  }

  // -------------------------- Framebuffer Objects --------------------------
  void BlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                       GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                       GLbitfield mask, GLenum filter) const {
    GetWebGL2Context()->BlitFramebuffer(srcX0, srcY0, srcX1, srcY1, dstX0,
                                        dstY0, dstX1, dstY1, mask, filter);
  }

  void InvalidateFramebuffer(GLenum target,
                             const Span<const GLenum>& attachments) const {
    GetWebGL2Context()->InvalidateFramebuffer(target, attachments);
  }

  void InvalidateSubFramebuffer(GLenum target,
                                const Span<const GLenum>& attachments, GLint x,
                                GLint y, GLsizei width, GLsizei height) const {
    GetWebGL2Context()->InvalidateSubFramebuffer(target, attachments, x, y,
                                                 width, height);
  }

  void ReadBuffer(GLenum mode) const { GetWebGL2Context()->ReadBuffer(mode); }

  // ----------------------- Renderbuffer objects -----------------------
  Maybe<std::vector<int32_t>> GetInternalformatParameter(GLenum target,
                                                         GLenum internalformat,
                                                         GLenum pname) const {
    return GetWebGL2Context()->GetInternalformatParameter(
        target, internalformat, pname);
  }

  void RenderbufferStorageMultisample(ObjectId id, uint32_t samples,
                                      GLenum internalFormat, uint32_t width,
                                      uint32_t height) const {
    const auto obj = ById<WebGLRenderbuffer>(id);
    if (!obj) return;
    mContext->RenderbufferStorageMultisample(*obj, samples, internalFormat,
                                             width, height);
  }

  // --------------------------- Texture objects ---------------------------
  void ActiveTexture(uint32_t texUnit) const {
    mContext->ActiveTexture(texUnit);
  }

  void BindTexture(GLenum texTarget, const ObjectId id) const {
    mContext->BindTexture(texTarget, AutoResolve(id));
  }

  void GenerateMipmap(GLenum texTarget) const {
    mContext->GenerateMipmap(texTarget);
  }

  // CompressedTexSubImage if `sub`
  void CompressedTexImage(bool sub, GLenum imageTarget, uint32_t level,
                          GLenum format, const uvec3& offset, const uvec3& size,
                          const Span<const uint8_t>& src,
                          const uint32_t pboImageSize,
                          const Maybe<uint64_t>& pboOffset) const {
    mContext->CompressedTexImage(sub, imageTarget, level, format, offset, size,
                                 src, pboImageSize, pboOffset);
  }

  // CopyTexSubImage if `!respecFormat`
  void CopyTexImage(GLenum imageTarget, uint32_t level, GLenum respecFormat,
                    const uvec3& dstOffset, const ivec2& srcOffset,
                    const uvec2& size) const {
    mContext->CopyTexImage(imageTarget, level, respecFormat, dstOffset,
                           srcOffset, size);
  }

  // TexSubImage if `!respecFormat`
  void TexImage(uint32_t level, GLenum respecFormat, const uvec3& offset,
                const webgl::PackingInfo& pi,
                const webgl::TexUnpackBlobDesc& src) const {
    mContext->TexImage(level, respecFormat, offset, pi, src);
  }

  void TexStorage(GLenum texTarget, uint32_t levels, GLenum internalFormat,
                  const uvec3& size) const {
    GetWebGL2Context()->TexStorage(texTarget, levels, internalFormat, size);
  }

  Maybe<double> GetTexParameter(ObjectId id, GLenum pname) const {
    const auto obj = ById<WebGLTexture>(id);
    if (!obj) return {};
    return mContext->GetTexParameter(*obj, pname);
  }

  void TexParameter_base(GLenum texTarget, GLenum pname,
                         const FloatOrInt& param) const {
    mContext->TexParameter_base(texTarget, pname, param);
  }

  // ------------------- Programs and shaders --------------------------------
  void UseProgram(ObjectId id) const { mContext->UseProgram(AutoResolve(id)); }

  bool ValidateProgram(ObjectId id) const {
    const auto obj = ById<WebGLProgram>(id);
    if (!obj) return false;
    return mContext->ValidateProgram(*obj);
  }

  // ------------------------ Uniforms and attributes ------------------------

  void UniformData(uint32_t loc, bool transpose,
                   const Span<const webgl::UniformDataVal>& data) const {
    mContext->UniformData(loc, transpose, data);
  }

  void VertexAttrib4T(GLuint index, const webgl::TypedQuad& data) const {
    mContext->VertexAttrib4T(index, data);
  }

  void VertexAttribDivisor(GLuint index, GLuint divisor) const {
    mContext->VertexAttribDivisor(index, divisor);
  }

  Maybe<double> GetIndexedParameter(GLenum target, GLuint index) const {
    return GetWebGL2Context()->GetIndexedParameter(target, index);
  }

  void UniformBlockBinding(const ObjectId id, GLuint uniformBlockIndex,
                           GLuint uniformBlockBinding) const {
    const auto obj = ById<WebGLProgram>(id);
    if (!obj) return;
    GetWebGL2Context()->UniformBlockBinding(*obj, uniformBlockIndex,
                                            uniformBlockBinding);
  }

  void EnableVertexAttribArray(GLuint index) const {
    mContext->EnableVertexAttribArray(index);
  }

  void DisableVertexAttribArray(GLuint index) const {
    mContext->DisableVertexAttribArray(index);
  }

  Maybe<double> GetVertexAttrib(GLuint index, GLenum pname) const {
    return mContext->GetVertexAttrib(index, pname);
  }

  void VertexAttribPointer(GLuint index,
                           const webgl::VertAttribPointerDesc& desc) const {
    mContext->VertexAttribPointer(index, desc);
  }

  // --------------------------- Buffer Operations --------------------------
  void ClearBufferTv(GLenum buffer, GLint drawBuffer,
                     const webgl::TypedQuad& data) const {
    GetWebGL2Context()->ClearBufferTv(buffer, drawBuffer, data);
  }

  void ClearBufferfi(GLenum buffer, GLint drawBuffer, GLfloat depth,
                     GLint stencil) const {
    GetWebGL2Context()->ClearBufferfi(buffer, drawBuffer, depth, stencil);
  }

  // ------------------------------ Readback -------------------------------
  void ReadPixelsPbo(const webgl::ReadPixelsDesc& desc,
                     const uint64_t offset) const {
    mContext->ReadPixelsPbo(desc, offset);
  }

  webgl::ReadPixelsResult ReadPixelsInto(
      const webgl::ReadPixelsDesc& desc,
      const mozilla::Range<uint8_t>& dest) const {
    return mContext->ReadPixelsInto(desc, dest);
  }

  // ----------------------------- Sampler -----------------------------------

  void BindSampler(GLuint unit, ObjectId id) const {
    GetWebGL2Context()->BindSampler(unit, AutoResolve(id));
  }

  void SamplerParameteri(ObjectId id, GLenum pname, GLint param) const {
    const auto obj = ById<WebGLSampler>(id);
    if (!obj) return;
    GetWebGL2Context()->SamplerParameteri(*obj, pname, param);
  }

  void SamplerParameterf(ObjectId id, GLenum pname, GLfloat param) const {
    const auto obj = ById<WebGLSampler>(id);
    if (!obj) return;
    GetWebGL2Context()->SamplerParameterf(*obj, pname, param);
  }

  Maybe<double> GetSamplerParameter(ObjectId id, GLenum pname) const {
    const auto obj = ById<WebGLSampler>(id);
    if (!obj) return {};
    return GetWebGL2Context()->GetSamplerParameter(*obj, pname);
  }

  // ------------------------------- GL Sync ---------------------------------

  GLenum ClientWaitSync(ObjectId id, GLbitfield flags, GLuint64 timeout) const {
    const auto obj = ById<WebGLSync>(id);
    if (!obj) return LOCAL_GL_WAIT_FAILED;
    return GetWebGL2Context()->ClientWaitSync(*obj, flags, timeout);
  }

  // -------------------------- Transform Feedback ---------------------------
  void BindTransformFeedback(ObjectId id) const {
    GetWebGL2Context()->BindTransformFeedback(AutoResolve(id));
  }

  void BeginTransformFeedback(GLenum primitiveMode) const {
    GetWebGL2Context()->BeginTransformFeedback(primitiveMode);
  }

  void EndTransformFeedback() const {
    GetWebGL2Context()->EndTransformFeedback();
  }

  void PauseTransformFeedback() const {
    GetWebGL2Context()->PauseTransformFeedback();
  }

  void ResumeTransformFeedback() const {
    GetWebGL2Context()->ResumeTransformFeedback();
  }

  void TransformFeedbackVaryings(ObjectId id,
                                 const std::vector<std::string>& varyings,
                                 GLenum bufferMode) const {
    const auto obj = ById<WebGLProgram>(id);
    if (!obj) return;
    GetWebGL2Context()->TransformFeedbackVaryings(*obj, varyings, bufferMode);
  }

  // -------------------------- Opaque Framebuffers ---------------------------
  void SetFramebufferIsInOpaqueRAF(ObjectId id, bool value) {
    WebGLFramebuffer* fb = AutoResolve(id);
    if (fb) {
      fb->mInOpaqueRAF = value;
    }
  }

  // -------------------------------------------------------------------------
  // Host-side extension methods.  Calls in the client are forwarded to the
  // host. Some extension methods are also available in WebGL2 Contexts.  For
  // them, the final parameter is a boolean indicating if the call originated
  // from an extension.
  // -------------------------------------------------------------------------

  // Misc. Extensions
  void DrawBuffers(const std::vector<GLenum>& buffers) const {
    mContext->DrawBuffers(buffers);
  }

  // VertexArrayObjectEXT
  void BindVertexArray(ObjectId id) const {
    mContext->BindVertexArray(AutoResolve(id));
  }

  // InstancedElementsEXT
  void DrawArraysInstanced(GLenum mode, GLint first, GLsizei vertCount,
                           GLsizei primCount) const {
    mContext->DrawArraysInstanced(mode, first, vertCount, primCount);
  }

  void DrawElementsInstanced(GLenum mode, GLsizei vertCount, GLenum type,
                             WebGLintptr offset, GLsizei primCount) const {
    mContext->DrawElementsInstanced(mode, vertCount, type, offset, primCount);
  }

  // GLQueryEXT
  void BeginQuery(GLenum target, ObjectId id) const {
    const auto obj = ById<WebGLQuery>(id);
    if (!obj) return;
    mContext->BeginQuery(target, *obj);
  }

  void EndQuery(GLenum target) const { mContext->EndQuery(target); }

  void QueryCounter(ObjectId id) const {
    const auto obj = ById<WebGLQuery>(id);
    if (!obj) return;
    mContext->QueryCounter(*obj);
  }

  Maybe<double> GetQueryParameter(ObjectId id, GLenum pname) const {
    const auto obj = ById<WebGLQuery>(id);
    if (!obj) return {};
    return mContext->GetQueryParameter(*obj, pname);
  }

  // WEBGL_provoking_vertex
  void ProvokingVertex(const webgl::ProvokingVertex mode) const {
    mContext->ProvokingVertex(mode);
  }

  // -------------------------------------------------------------------------
  // Client-side methods.  Calls in the Host are forwarded to the client.
  // -------------------------------------------------------------------------
 public:
  void OnLostContext();
  void OnRestoredContext();

 protected:
  WebGL2Context* GetWebGL2Context() const {
    MOZ_RELEASE_ASSERT(mContext->IsWebGL2(), "Requires WebGL2 context");
    return static_cast<WebGL2Context*>(mContext.get());
  }
};

}  // namespace mozilla

#endif  // HOSTWEBGLCONTEXT_H_
