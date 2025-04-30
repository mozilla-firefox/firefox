/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef CLIENTWEBGLCONTEXT_H_
#define CLIENTWEBGLCONTEXT_H_

#include "GLConsts.h"
#include "js/GCAPI.h"
#include "mozilla/dom/ImageData.h"
#include "mozilla/Range.h"
#include "mozilla/RefCounted.h"
#include "mozilla/dom/TypedArray.h"
#include "nsICanvasRenderingContextInternal.h"
#include "nsWrapperCache.h"
#include "mozilla/dom/WebGLRenderingContextBinding.h"
#include "mozilla/dom/WebGL2RenderingContextBinding.h"
#include "mozilla/layers/LayersSurfaces.h"
#include "mozilla/StaticPrefs_webgl.h"
#include "WebGLStrongTypes.h"
#include "WebGLTypes.h"

#include "mozilla/Logging.h"
#include "WebGLCommandQueue.h"

#include <memory>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mozilla {

class ClientWebGLExtensionBase;
class HostWebGLContext;

template <typename MethodT, MethodT Method>
size_t IdByMethod();

namespace dom {
class OwningHTMLCanvasElementOrOffscreenCanvas;
class WebGLChild;
}  // namespace dom

namespace webgl {
class AvailabilityRunnable;
class TexUnpackBlob;
class TexUnpackBytes;
}  // namespace webgl

////////////////////////////////////

class WebGLActiveInfoJS final : public RefCounted<WebGLActiveInfoJS> {
 public:
  MOZ_DECLARE_REFCOUNTED_TYPENAME(WebGLActiveInfoJS)

  const webgl::ActiveInfo mInfo;

  explicit WebGLActiveInfoJS(const webgl::ActiveInfo& info) : mInfo(info) {}

  virtual ~WebGLActiveInfoJS() = default;

  // -
  // WebIDL attributes

  GLint Size() const { return static_cast<GLint>(mInfo.elemCount); }
  GLenum Type() const { return mInfo.elemType; }

  void GetName(nsString& retval) const { CopyUTF8toUTF16(mInfo.name, retval); }

  bool WrapObject(JSContext*, JS::Handle<JSObject*>,
                  JS::MutableHandle<JSObject*>);
};

class WebGLShaderPrecisionFormatJS final
    : public RefCounted<WebGLShaderPrecisionFormatJS> {
 public:
  MOZ_DECLARE_REFCOUNTED_TYPENAME(WebGLShaderPrecisionFormatJS)

  const webgl::ShaderPrecisionFormat mInfo;

  explicit WebGLShaderPrecisionFormatJS(
      const webgl::ShaderPrecisionFormat& info)
      : mInfo(info) {}

  virtual ~WebGLShaderPrecisionFormatJS() = default;

  GLint RangeMin() const { return mInfo.rangeMin; }
  GLint RangeMax() const { return mInfo.rangeMax; }
  GLint Precision() const { return mInfo.precision; }

  bool WrapObject(JSContext*, JS::Handle<JSObject*>,
                  JS::MutableHandle<JSObject*>);
};

// -----------------------

class ClientWebGLContext;
class WebGLBufferJS;
class WebGLFramebufferJS;
class WebGLProgramJS;
class WebGLQueryJS;
class WebGLRenderbufferJS;
class WebGLSamplerJS;
class WebGLShaderJS;
class WebGLTextureJS;
class WebGLTransformFeedbackJS;
class WebGLVertexArrayJS;

namespace webgl {

struct LinkResult;

class ProgramKeepAlive final {
  friend class mozilla::WebGLProgramJS;

  WebGLProgramJS* mParent;

 public:
  explicit ProgramKeepAlive(WebGLProgramJS& parent) : mParent(&parent) {}
  ~ProgramKeepAlive();
};

class ShaderKeepAlive final {
  friend class mozilla::WebGLShaderJS;

  const WebGLShaderJS* mParent;

 public:
  explicit ShaderKeepAlive(const WebGLShaderJS& parent) : mParent(&parent) {}
  ~ShaderKeepAlive();
};

class ContextGenerationInfo final {
 public:
  webgl::ExtensionBits mEnabledExtensions;
  RefPtr<WebGLProgramJS> mCurrentProgram;
  std::shared_ptr<webgl::ProgramKeepAlive> mProgramKeepAlive;
  mutable std::shared_ptr<webgl::LinkResult> mActiveLinkResult;

  RefPtr<WebGLTransformFeedbackJS> mDefaultTfo;
  RefPtr<WebGLVertexArrayJS> mDefaultVao;

  std::unordered_map<GLenum, RefPtr<WebGLBufferJS>> mBoundBufferByTarget;
  std::vector<RefPtr<WebGLBufferJS>> mBoundUbos;
  RefPtr<WebGLFramebufferJS> mBoundDrawFb;
  RefPtr<WebGLFramebufferJS> mBoundReadFb;
  RefPtr<WebGLRenderbufferJS> mBoundRb;
  RefPtr<WebGLTransformFeedbackJS> mBoundTfo;
  RefPtr<WebGLVertexArrayJS> mBoundVao;
  std::unordered_map<GLenum, RefPtr<WebGLQueryJS>> mCurrentQueryByTarget;

  struct TexUnit final {
    RefPtr<WebGLSamplerJS> sampler;
    std::unordered_map<GLenum, RefPtr<WebGLTextureJS>> texByTarget;
  };
  uint32_t mActiveTexUnit = 0;
  std::vector<TexUnit> mTexUnits;

  bool mTfActiveAndNotPaused = false;

  std::vector<TypedQuad> mGenericVertexAttribs;

  std::array<int32_t, 4> mScissor = {};
  std::array<int32_t, 4> mViewport = {};
  std::array<float, 4> mClearColor = {{0, 0, 0, 0}};
  std::array<float, 4> mBlendColor = {{0, 0, 0, 0}};
  std::array<float, 2> mDepthRange = {{0, 1}};
  webgl::PixelPackingState mPixelPackState;
  webgl::PixelUnpackStateWebgl mPixelUnpackState;

  std::vector<GLenum> mCompressedTextureFormats;

  Maybe<uvec2> mDrawingBufferSize;

  webgl::ProvokingVertex mProvokingVertex = webgl::ProvokingVertex::LastVertex;

  mutable std::unordered_map<GLenum, bool> mIsEnabledMap;
};

// -

// In the cross process case, the WebGL actor's ownership relationship looks
// like this:
// ---------------------------------------------------------------------
// | ClientWebGLContext -> WebGLChild -> WebGLParent -> HostWebGLContext
// ---------------------------------------------------------------------
//
// where 'A -> B' means "A owns B"

struct NotLostData final {
  ClientWebGLContext& context;
  webgl::InitContextResult info;

  RefPtr<mozilla::dom::WebGLChild> outOfProcess;
  std::unique_ptr<HostWebGLContext> inProcess;

  webgl::ContextGenerationInfo state;
  std::array<RefPtr<ClientWebGLExtensionBase>,
             UnderlyingValue(WebGLExtensionID::Max)>
      extensions;

  RefPtr<layers::CanvasRenderer> mCanvasRenderer;

  explicit NotLostData(ClientWebGLContext& context);
  ~NotLostData();
};

// -

class ObjectJS {
  friend ClientWebGLContext;

 public:
  const std::weak_ptr<NotLostData> mGeneration;
  const ObjectId mId;

 protected:
  bool mDeleteRequested = false;

  explicit ObjectJS(const ClientWebGLContext*);
  virtual ~ObjectJS() = default;

 public:
  ClientWebGLContext* Context() const {
    const auto locked = mGeneration.lock();
    if (!locked) return nullptr;
    return &(locked->context);
  }

  ClientWebGLContext* GetParentObject() const { return Context(); }

  // A la carte:
  bool IsForContext(const ClientWebGLContext&) const;
  virtual bool IsDeleted() const { return mDeleteRequested; }

  bool IsUsable(const ClientWebGLContext& context) const {
    return IsForContext(context) && !IsDeleted();
  }

  // The workhorse:
  bool ValidateUsable(const ClientWebGLContext& context,
                      const char* const argName) const {
    if (MOZ_LIKELY(IsUsable(context))) return true;
    WarnInvalidUse(context, argName);
    return false;
  }

  // Use by DeleteFoo:
  bool ValidateForContext(const ClientWebGLContext& context,
                          const char* const argName) const;

 private:
  void WarnInvalidUse(const ClientWebGLContext&, const char* argName) const;

  // The enum is INVALID_VALUE for Program/Shader :(
  virtual GLenum ErrorOnDeleted() const { return LOCAL_GL_INVALID_OPERATION; }
};

}  // namespace webgl

// -------------------------

class WebGLBufferJS final : public nsWrapperCache, public webgl::ObjectJS {
  friend class ClientWebGLContext;

  webgl::BufferKind mKind =
      webgl::BufferKind::Undefined;  // !IsBuffer until Bind

 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(WebGLBufferJS)
  NS_DECL_CYCLE_COLLECTION_NATIVE_WRAPPERCACHE_CLASS(WebGLBufferJS)

  explicit WebGLBufferJS(const ClientWebGLContext& webgl)
      : webgl::ObjectJS(&webgl) {}

 private:
  ~WebGLBufferJS();

 public:
  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;
};

// -

class WebGLFramebufferJS final : public nsWrapperCache, public webgl::ObjectJS {
  friend class ClientWebGLContext;

 public:
  struct Attachment final {
    RefPtr<WebGLRenderbufferJS> rb;
    RefPtr<WebGLTextureJS> tex;
  };

 private:
  bool mHasBeenBound = false;  // !IsFramebuffer until Bind
  std::unordered_map<GLenum, Attachment> mAttachments;

 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(WebGLFramebufferJS)
  NS_DECL_CYCLE_COLLECTION_NATIVE_WRAPPERCACHE_CLASS(WebGLFramebufferJS)

  explicit WebGLFramebufferJS(const ClientWebGLContext&, bool opaque = false);

  const bool mOpaque;
  bool mInOpaqueRAF = false;

 private:
  ~WebGLFramebufferJS();

  void EnsureColorAttachments();

 public:
  Attachment* GetAttachment(const GLenum slotEnum) {
    auto ret = MaybeFind(mAttachments, slotEnum);
    if (!ret) {
      EnsureColorAttachments();
      ret = MaybeFind(mAttachments, slotEnum);
    }
    return ret;
  }

  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;
};

// -

class WebGLProgramJS final : public nsWrapperCache, public webgl::ObjectJS {
  friend class ClientWebGLContext;

 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(WebGLProgramJS)
  NS_DECL_CYCLE_COLLECTION_NATIVE_WRAPPERCACHE_CLASS(WebGLProgramJS)
  // Must come first!
  // If the REFCOUNTING macro isn't declared first, the AddRef at
  // mInnerRef->js will panic when REFCOUNTING's "owning thread" var is still
  // uninitialized.

  struct Attachment final {
    RefPtr<WebGLShaderJS> shader;
    std::shared_ptr<webgl::ShaderKeepAlive> keepAlive;
  };

 private:
  std::shared_ptr<webgl::ProgramKeepAlive> mKeepAlive;
  const std::weak_ptr<webgl::ProgramKeepAlive> mKeepAliveWeak;

  std::unordered_map<GLenum, Attachment> mNextLink_Shaders;
  bool mLastValidate = false;
  mutable std::shared_ptr<webgl::LinkResult>
      mResult;  // Never null, often defaulted.

  struct UniformLocInfo final {
    const uint32_t location;
    const GLenum elemType;
  };

  mutable Maybe<std::unordered_map<std::string, UniformLocInfo>>
      mUniformLocByName;
  mutable std::vector<uint32_t> mUniformBlockBindings;

  std::unordered_set<const WebGLTransformFeedbackJS*> mActiveTfos;

  explicit WebGLProgramJS(const ClientWebGLContext&);

  ~WebGLProgramJS() {
    mKeepAlive = nullptr;  // Try to delete.

    const auto& maybe = mKeepAliveWeak.lock();
    if (maybe) {
      maybe->mParent = nullptr;
    }
  }

 public:
  bool IsDeleted() const override { return !mKeepAliveWeak.lock(); }
  GLenum ErrorOnDeleted() const override { return LOCAL_GL_INVALID_VALUE; }

  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;
};

// -

class WebGLQueryJS final : public nsWrapperCache,
                           public webgl::ObjectJS,
                           public SupportsWeakPtr {
  friend class ClientWebGLContext;
  friend class webgl::AvailabilityRunnable;

  GLenum mTarget = 0;  // !IsQuery until Bind
  bool mCanBeAvailable = false;

 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(WebGLQueryJS)
  NS_DECL_CYCLE_COLLECTION_NATIVE_WRAPPERCACHE_CLASS(WebGLQueryJS)

  explicit WebGLQueryJS(const ClientWebGLContext* const webgl)
      : webgl::ObjectJS(webgl) {}

 private:
  ~WebGLQueryJS();

 public:
  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;
};

// -

class WebGLRenderbufferJS final : public nsWrapperCache,
                                  public webgl::ObjectJS {
  friend class ClientWebGLContext;

 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(WebGLRenderbufferJS)
  NS_DECL_CYCLE_COLLECTION_NATIVE_WRAPPERCACHE_CLASS(WebGLRenderbufferJS)

 private:
  bool mHasBeenBound = false;  // !IsRenderbuffer until Bind

  explicit WebGLRenderbufferJS(const ClientWebGLContext& webgl)
      : webgl::ObjectJS(&webgl) {}
  ~WebGLRenderbufferJS();

 public:
  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;
};

// -

class WebGLSamplerJS final : public nsWrapperCache, public webgl::ObjectJS {
  // IsSampler without Bind
 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(WebGLSamplerJS)
  NS_DECL_CYCLE_COLLECTION_NATIVE_WRAPPERCACHE_CLASS(WebGLSamplerJS)

  explicit WebGLSamplerJS(const ClientWebGLContext& webgl)
      : webgl::ObjectJS(&webgl) {}

 private:
  ~WebGLSamplerJS();

 public:
  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;
};

// -

class WebGLShaderJS final : public nsWrapperCache, public webgl::ObjectJS {
  friend class ClientWebGLContext;

 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(WebGLShaderJS)
  NS_DECL_CYCLE_COLLECTION_NATIVE_WRAPPERCACHE_CLASS(WebGLShaderJS)

 private:
  const GLenum mType;
  std::string mSource;
  std::shared_ptr<webgl::ShaderKeepAlive> mKeepAlive;
  const std::weak_ptr<webgl::ShaderKeepAlive> mKeepAliveWeak;

  mutable webgl::CompileResult mResult;

  WebGLShaderJS(const ClientWebGLContext&, GLenum type);

  ~WebGLShaderJS() {
    mKeepAlive = nullptr;  // Try to delete.

    const auto& maybe = mKeepAliveWeak.lock();
    if (maybe) {
      maybe->mParent = nullptr;
    }
  }

 public:
  bool IsDeleted() const override { return !mKeepAliveWeak.lock(); }
  GLenum ErrorOnDeleted() const override { return LOCAL_GL_INVALID_VALUE; }

  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;
};

// -

class WebGLSyncJS final : public nsWrapperCache,
                          public webgl::ObjectJS,
                          public SupportsWeakPtr {
  friend class ClientWebGLContext;
  friend class webgl::AvailabilityRunnable;

  bool mCanBeAvailable = false;
  uint8_t mNumQueriesBeforeFirstFrameBoundary = 0;
  uint8_t mNumQueriesWithoutFlushCommandsBit = 0;

 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(WebGLSyncJS)
  NS_DECL_CYCLE_COLLECTION_NATIVE_WRAPPERCACHE_CLASS(WebGLSyncJS)

  explicit WebGLSyncJS(const ClientWebGLContext& webgl)
      : webgl::ObjectJS(&webgl) {}

 private:
  ~WebGLSyncJS();

 public:
  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;
};

// -

class WebGLTextureJS final : public nsWrapperCache, public webgl::ObjectJS {
  friend class ClientWebGLContext;

  GLenum mTarget = 0;  // !IsTexture until Bind

 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(WebGLTextureJS)
  NS_DECL_CYCLE_COLLECTION_NATIVE_WRAPPERCACHE_CLASS(WebGLTextureJS)

  explicit WebGLTextureJS(const ClientWebGLContext& webgl)
      : webgl::ObjectJS(&webgl) {}

 private:
  ~WebGLTextureJS();

 public:
  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;
};

// -

class WebGLTransformFeedbackJS final : public nsWrapperCache,
                                       public webgl::ObjectJS {
  friend class ClientWebGLContext;

  bool mHasBeenBound = false;  // !IsTransformFeedback until Bind
  bool mActiveOrPaused = false;
  std::vector<RefPtr<WebGLBufferJS>> mAttribBuffers;
  RefPtr<WebGLProgramJS> mActiveProgram;
  std::shared_ptr<webgl::ProgramKeepAlive> mActiveProgramKeepAlive;

 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(WebGLTransformFeedbackJS)
  NS_DECL_CYCLE_COLLECTION_NATIVE_WRAPPERCACHE_CLASS(WebGLTransformFeedbackJS)

  explicit WebGLTransformFeedbackJS(const ClientWebGLContext&);

 private:
  ~WebGLTransformFeedbackJS();

 public:
  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;
};

// -

std::array<uint16_t, 3> ValidUploadElemTypes(GLenum);

class WebGLUniformLocationJS final : public nsWrapperCache,
                                     public webgl::ObjectJS {
  friend class ClientWebGLContext;

  const std::weak_ptr<webgl::LinkResult> mParent;
  const uint32_t mLocation;
  const std::array<uint16_t, 3> mValidUploadElemTypes;

 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(WebGLUniformLocationJS)
  NS_DECL_CYCLE_COLLECTION_NATIVE_WRAPPERCACHE_CLASS(WebGLUniformLocationJS)

  WebGLUniformLocationJS(const ClientWebGLContext& webgl,
                         std::weak_ptr<webgl::LinkResult> parent, uint32_t loc,
                         GLenum elemType)
      : webgl::ObjectJS(&webgl),
        mParent(parent),
        mLocation(loc),
        mValidUploadElemTypes(ValidUploadElemTypes(elemType)) {}

 private:
  ~WebGLUniformLocationJS() = default;

 public:
  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;
};

// -

class WebGLVertexArrayJS final : public nsWrapperCache, public webgl::ObjectJS {
  friend class ClientWebGLContext;

  bool mHasBeenBound = false;  // !IsVertexArray until Bind
  RefPtr<WebGLBufferJS> mIndexBuffer;
  std::vector<RefPtr<WebGLBufferJS>> mAttribBuffers;

 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(WebGLVertexArrayJS)
  NS_DECL_CYCLE_COLLECTION_NATIVE_WRAPPERCACHE_CLASS(WebGLVertexArrayJS)

  explicit WebGLVertexArrayJS(const ClientWebGLContext*);

 private:
  ~WebGLVertexArrayJS();

 public:
  JSObject* WrapObject(JSContext*, JS::Handle<JSObject*>) override;
};

////////////////////////////////////

using Float32ListU = dom::MaybeSharedFloat32ArrayOrUnrestrictedFloatSequence;
using Int32ListU = dom::MaybeSharedInt32ArrayOrLongSequence;
using Uint32ListU = dom::MaybeSharedUint32ArrayOrUnsignedLongSequence;

template <typename Converter, typename T>
inline bool ConvertSequence(const dom::Sequence<T>& sequence,
                            Converter&& converter) {
  // It's ok to GC here, but we need a common parameter list for
  // Converter with the typed array version.
  JS::AutoCheckCannotGC nogc;
  nogc.reset();
  return std::forward<Converter>(converter)(Span(sequence), std::move(nogc));
}

template <typename Converter>
inline bool Convert(const Float32ListU& list, Converter&& converter) {
  if (list.IsFloat32Array()) {
    return list.GetAsFloat32Array().ProcessData(
        std::forward<Converter>(converter));
  }

  return ConvertSequence(list.GetAsUnrestrictedFloatSequence(),
                         std::forward<Converter>(converter));
}

template <typename Converter>
inline bool Convert(const Int32ListU& list, Converter&& converter) {
  if (list.IsInt32Array()) {
    return list.GetAsInt32Array().ProcessData(
        std::forward<Converter>(converter));
  }

  return ConvertSequence(list.GetAsLongSequence(),
                         std::forward<Converter>(converter));
}

template <typename Converter>
inline bool Convert(const Uint32ListU& list, Converter&& converter) {
  if (list.IsUint32Array()) {
    return list.GetAsUint32Array().ProcessData(
        std::forward<Converter>(converter));
  }

  return ConvertSequence(list.GetAsUnsignedLongSequence(),
                         std::forward<Converter>(converter));
}

template <typename T>
inline Range<const uint8_t> MakeByteRange(const T& x) {
  const auto typed = MakeRange(x);
  return Range<const uint8_t>(
      reinterpret_cast<const uint8_t*>(typed.begin().get()),
      typed.length() * sizeof(typed[0]));
}

template <typename T>
inline Range<const uint8_t> MakeByteRange(const Span<T>& x) {
  return AsBytes(x);
}

// -

struct TexImageSourceAdapter final : public TexImageSource {
  TexImageSourceAdapter(const dom::Nullable<dom::ArrayBufferView>* maybeView,
                        ErrorResult*) {
    if (!maybeView->IsNull()) {
      mView = &(maybeView->Value());
    }
  }

  TexImageSourceAdapter(const dom::Nullable<dom::ArrayBufferView>* maybeView,
                        GLuint viewElemOffset) {
    if (!maybeView->IsNull()) {
      mView = &(maybeView->Value());
    }
    mViewElemOffset = viewElemOffset;
  }

  TexImageSourceAdapter(const dom::ArrayBufferView* view, ErrorResult*) {
    mView = view;
  }

  TexImageSourceAdapter(const dom::ArrayBufferView* view, GLuint viewElemOffset,
                        GLuint viewElemLengthOverride = 0) {
    mView = view;
    mViewElemOffset = viewElemOffset;
    mViewElemLengthOverride = viewElemLengthOverride;
  }

  explicit TexImageSourceAdapter(const WebGLintptr* pboOffset,
                                 GLuint ignored1 = 0, GLuint ignored2 = 0) {
    mPboOffset = pboOffset;
  }

  TexImageSourceAdapter(const WebGLintptr* pboOffset, ErrorResult* ignored) {
    mPboOffset = pboOffset;
  }

  TexImageSourceAdapter(const dom::ImageBitmap* imageBitmap,
                        ErrorResult* out_error) {
    mImageBitmap = imageBitmap;
    mOut_error = out_error;
  }

  TexImageSourceAdapter(const dom::ImageData* imageData, ErrorResult*) {
    mImageData = imageData;
  }

  TexImageSourceAdapter(const dom::OffscreenCanvas* offscreenCanvas,
                        ErrorResult* const out_error) {
    mOffscreenCanvas = offscreenCanvas;
    mOut_error = out_error;
  }

  TexImageSourceAdapter(const dom::VideoFrame* videoFrame,
                        ErrorResult* const out_error) {
    mVideoFrame = videoFrame;
    mOut_error = out_error;
  }

  TexImageSourceAdapter(const dom::Element* domElem,
                        ErrorResult* const out_error) {
    mDomElem = domElem;
    mOut_error = out_error;
  }
};

/**
 * Base class for all IDL implementations of WebGLContext
 */
class ClientWebGLContext final : public nsICanvasRenderingContextInternal,
                                 public nsWrapperCache {
  friend class webgl::AvailabilityRunnable;
  friend class webgl::ObjectJS;
  friend class webgl::ProgramKeepAlive;
  friend class webgl::ShaderKeepAlive;

  // ----------------------------- Lifetime and DOM ---------------------------
 public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_WRAPPERCACHE_CLASS(ClientWebGLContext)

  JSObject* WrapObject(JSContext* cx,
                       JS::Handle<JSObject*> givenProto) override {
    if (mIsWebGL2) {
      return dom::WebGL2RenderingContext_Binding::Wrap(cx, this, givenProto);
    }
    return dom::WebGLRenderingContext_Binding::Wrap(cx, this, givenProto);
  }

  // -

 public:
  const bool mIsWebGL2;

 private:
  bool mIsCanvasDirty = false;
  uvec2 mRequestedSize = {};

 public:
  explicit ClientWebGLContext(bool webgl2);

 private:
  virtual ~ClientWebGLContext();

  const RefPtr<ClientWebGLExtensionLoseContext> mExtLoseContext;

  mutable std::shared_ptr<webgl::NotLostData> mNotLost;
  mutable GLenum mNextError = 0;
  mutable webgl::LossStatus mLossStatus = webgl::LossStatus::Ready;
  mutable bool mAwaitingRestore = false;
  mutable webgl::ObjectId mLastId = 0;

 public:
  webgl::ObjectId NextId() const { return mLastId += 1; }

  // Holds Some Id if async present is used
  mutable Maybe<layers::RemoteTextureId> mLastRemoteTextureId;
  mutable Maybe<layers::RemoteTextureOwnerId> mRemoteTextureOwnerId;
  mutable RefPtr<layers::FwdTransactionTracker> mFwdTransactionTracker;

  // -

 public:
  const auto& Limits() const { return mNotLost->info.limits; }
  const auto& Vendor() const { return mNotLost->info.vendor; }
  // https://www.khronos.org/registry/webgl/specs/latest/1.0/#actual-context-parameters
  const WebGLContextOptions& ActualContextParameters() const {
    MOZ_ASSERT(mNotLost != nullptr);
    return mNotLost->info.options;
  }

  auto& State() { return mNotLost->state; }
  const auto& State() const {
    return const_cast<ClientWebGLContext*>(this)->State();
  }

  // -

 private:
  mutable RefPtr<webgl::AvailabilityRunnable> mAvailabilityRunnable;

 public:
  webgl::AvailabilityRunnable& EnsureAvailabilityRunnable() const;

  // -

 public:
  void EmulateLoseContext() const;
  void OnContextLoss(webgl::ContextLossReason) const;
  void RestoreContext(webgl::LossStatus requiredStatus) const;

 private:
  bool DispatchEvent(const nsAString&) const;
  void Event_webglcontextlost() const;
  void Event_webglcontextrestored() const;

  bool CreateHostContext(const uvec2& requestedSize);
  void ThrowEvent_WebGLContextCreationError(const std::string&) const;

  void UpdateCanvasParameters();

 public:
  void MarkCanvasDirty();

  void MarkContextClean() override {}

  void OnBeforePaintTransaction() override;

  mozilla::dom::WebGLChild* GetChild() const {
    if (!mNotLost) return nullptr;
    if (!mNotLost->outOfProcess) return nullptr;
    return mNotLost->outOfProcess.get();
  }

  // -------------------------------------------------------------------------
  // Client WebGL API call tracking and error message reporting
  // -------------------------------------------------------------------------
 public:
  // Remembers the WebGL function that is lowest on the stack for client-side
  // error generation.
  class FuncScope final {
   public:
    const ClientWebGLContext& mWebGL;
    const std::shared_ptr<webgl::NotLostData> mKeepNotLostOrNull;
    const char* const mFuncName;

    FuncScope(const ClientWebGLContext& webgl, const char* funcName)
        : mWebGL(webgl),
          mKeepNotLostOrNull(webgl.mNotLost),
          mFuncName(funcName) {
      // Only set if an "outer" scope hasn't already been set.
      if (!mWebGL.mFuncScope) {
        mWebGL.mFuncScope = this;
      }
    }

    ~FuncScope() {
      if (this == mWebGL.mFuncScope) {
        mWebGL.mFuncScope = nullptr;
      }
    }

    FuncScope(const FuncScope&) = delete;
    FuncScope(FuncScope&&) = delete;
  };

 protected:
  // The scope of the function at the top of the current WebGL function call
  // stack
  mutable FuncScope* mFuncScope = nullptr;

  const char* FuncName() const {
    return mFuncScope ? mFuncScope->mFuncName : nullptr;
  }

 public:
  template <typename... Args>
  void EnqueueError(const GLenum error, const char* const format,
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
    text.AppendPrintf(format, args...);
#ifdef __clang__
#  pragma clang diagnostic pop
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

    EnqueueErrorImpl(error, text);
  }

  void EnqueueError(const webgl::ErrorInfo& info) const {
    EnqueueError(info.type, "%s", info.info.c_str());
  }

  template <typename... Args>
  void EnqueueWarning(const char* const format, const Args&... args) const {
    EnqueueError(0, format, args...);
  }

  template <typename... Args>
  void EnqueuePerfWarning(const char* const format, const Args&... args) const {
    EnqueueError(webgl::kErrorPerfWarning, format, args...);
  }

  void EnqueueError_ArgEnum(const char* argName,
                            GLenum val) const;  // Cold code.

 private:
  void EnqueueErrorImpl(GLenum errorOrZero, const nsACString&) const;

 public:
  Maybe<Span<uint8_t>> ValidateArrayBufferView(const Span<uint8_t>& bytes,
                                               size_t elemSize,
                                               GLuint elemOffset,
                                               GLuint elemCountOverride,
                                               const GLenum errorEnum) const;

 protected:
  template <typename T>
  bool ValidateNonNull(const char* const argName,
                       const dom::Nullable<T>& maybe) const {
    if (maybe.IsNull()) {
      EnqueueError(LOCAL_GL_INVALID_VALUE, "%s: Cannot be null.", argName);
      return false;
    }
    return true;
  }

  bool ValidateNonNegative(const char* argName, int64_t val) const {
    if (MOZ_UNLIKELY(val < 0)) {
      EnqueueError(LOCAL_GL_INVALID_VALUE, "`%s` must be non-negative.",
                   argName);
      return false;
    }
    return true;
  }

  bool ValidateViewType(GLenum unpackType, const TexImageSource& src) const;

  Maybe<uvec3> ValidateExtents(GLsizei width, GLsizei height, GLsizei depth,
                               GLint border) const;

  // -------------------------------------------------------------------------
  // nsICanvasRenderingContextInternal / nsAPostRefreshObserver
  // -------------------------------------------------------------------------
 public:
  bool InitializeCanvasRenderer(nsDisplayListBuilder* aBuilder,
                                layers::CanvasRenderer* aRenderer) override;

  void MarkContextCleanForFrameCapture() override {
    mFrameCaptureState = FrameCaptureState::CLEAN;
  }
  // Note that 'clean' here refers to its invalidation state, not the
  // contents of the buffer.
  Watchable<FrameCaptureState>* GetFrameCaptureState() override {
    return &mFrameCaptureState;
  }

  void OnMemoryPressure() override;
  void SetContextOptions(const WebGLContextOptions& aOptions) {
    mInitialOptions.emplace(aOptions);
  }
  const WebGLContextOptions& GetContextOptions() const {
    return mInitialOptions.ref();
  }
  NS_IMETHOD
  SetContextOptions(JSContext* cx, JS::Handle<JS::Value> options,
                    ErrorResult& aRvForDictionaryInit) override;
  NS_IMETHOD
  SetDimensions(int32_t width, int32_t height) override;
  bool UpdateWebRenderCanvasData(
      nsDisplayListBuilder* aBuilder,
      layers::WebRenderCanvasData* aCanvasData) override;

  // ------

  int32_t GetWidth() override { return AutoAssertCast(DrawingBufferSize().x); }
  int32_t GetHeight() override { return AutoAssertCast(DrawingBufferSize().y); }

  NS_IMETHOD InitializeWithDrawTarget(nsIDocShell*,
                                      NotNull<gfx::DrawTarget*>) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  void ResetBitmap() override;

  UniquePtr<uint8_t[]> GetImageBuffer(int32_t* out_format,
                                      gfx::IntSize* out_imageSize) override;
  NS_IMETHOD GetInputStream(const char* mimeType,
                            const nsAString& encoderOptions,
                            nsIInputStream** out_stream) override;

  already_AddRefed<mozilla::gfx::SourceSurface> GetSurfaceSnapshot(
      gfxAlphaType* out_alphaType) override;

  mozilla::ipc::IProtocol* SupportsSnapshotExternalCanvas() const override;

  void SetOpaqueValueFromOpaqueAttr(bool) override {};
  bool GetIsOpaque() override { return !mInitialOptions->alpha; }

  /**
   * An abstract base class to be implemented by callers wanting to be notified
   * that a refresh has occurred. Callers must ensure an observer is removed
   * before it is destroyed.
   */
  void DidRefresh() override;

  NS_IMETHOD Redraw(const gfxRect&) override {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  // ------

 protected:
  layers::LayersBackend GetCompositorBackendType() const;

  Watchable<FrameCaptureState> mFrameCaptureState = {
      FrameCaptureState::CLEAN, "ClientWebGLContext::mFrameCaptureState"};

  // -------------------------------------------------------------------------
  // WebGLRenderingContext Basic Properties and Methods
  // -------------------------------------------------------------------------
 public:
  dom::HTMLCanvasElement* GetCanvas() const { return mCanvasElement; }
  void Commit();
  void GetCanvas(
      dom::Nullable<dom::OwningHTMLCanvasElementOrOffscreenCanvas>& retval);

  GLsizei DrawingBufferWidth() {
    const FuncScope funcScope(*this, "drawingBufferWidth");
    return AutoAssertCast(DrawingBufferSize().x);
  }
  GLsizei DrawingBufferHeight() {
    const FuncScope funcScope(*this, "drawingBufferHeight");
    return AutoAssertCast(DrawingBufferSize().y);
  }

  // -

 private:
  std::optional<dom::PredefinedColorSpace> mDrawingBufferColorSpace;
  std::optional<dom::PredefinedColorSpace> mUnpackColorSpace;

 public:
  auto DrawingBufferColorSpace() const {
    return mDrawingBufferColorSpace ? *mDrawingBufferColorSpace
                                    : dom::PredefinedColorSpace::Srgb;
  }
  void SetDrawingBufferColorSpace(dom::PredefinedColorSpace);

  auto UnpackColorSpace() const {
    return mUnpackColorSpace ? *mUnpackColorSpace
                             : dom::PredefinedColorSpace::Srgb;
  }
  void SetUnpackColorSpace(dom::PredefinedColorSpace);

  // -

  void GetContextAttributes(dom::Nullable<dom::WebGLContextAttributes>& retval);

 private:
  webgl::SwapChainOptions PrepareAsyncSwapChainOptions(
      WebGLFramebufferJS* fb, bool webvr,
      const webgl::SwapChainOptions& options = webgl::SwapChainOptions());

 public:
  layers::TextureType GetTexTypeForSwapChain() const;
  void Present(
      WebGLFramebufferJS*, const bool webvr = false,
      const webgl::SwapChainOptions& options = webgl::SwapChainOptions());
  void Present(
      WebGLFramebufferJS*, layers::TextureType, const bool webvr = false,
      const webgl::SwapChainOptions& options = webgl::SwapChainOptions());
  void CopyToSwapChain(
      WebGLFramebufferJS*,
      const webgl::SwapChainOptions& options = webgl::SwapChainOptions());
  void EndOfFrame();
  Maybe<layers::SurfaceDescriptor> GetFrontBuffer(
      WebGLFramebufferJS*, const bool webvr = false) override;
  Maybe<layers::SurfaceDescriptor> PresentFrontBuffer(
      WebGLFramebufferJS*, const bool webvr = false) override;
  RefPtr<gfx::SourceSurface> GetFrontBufferSnapshot(
      bool requireAlphaPremult = true) override;
  already_AddRefed<layers::FwdTransactionTracker> UseCompositableForwarder(
      layers::CompositableForwarder* aForwarder) override;
  void OnDestroyChild(dom::WebGLChild* aChild);

  void ClearVRSwapChain();

 private:
  RefPtr<gfx::DataSourceSurface> BackBufferSnapshot();
  [[nodiscard]] bool DoReadPixels(const webgl::ReadPixelsDesc&,
                                  Span<uint8_t>) const;
  uvec2 DrawingBufferSize();

  // -

  mutable bool mAutoFlushPending = false;

  void AutoEnqueueFlush() const {
    if (MOZ_LIKELY(mAutoFlushPending)) return;
    mAutoFlushPending = true;

    const auto DeferredFlush = [weak =
                                    WeakPtr<const ClientWebGLContext>(this)]() {
      const auto strong = RefPtr<const ClientWebGLContext>(weak);
      if (!strong) return;
      if (!strong->mAutoFlushPending) return;
      strong->mAutoFlushPending = false;

      if (!StaticPrefs::webgl_auto_flush()) return;
      const bool flushGl = StaticPrefs::webgl_auto_flush_gl();
      strong->Flush(flushGl);
    };

    already_AddRefed<mozilla::CancelableRunnable> runnable =
        NS_NewCancelableRunnableFunction("ClientWebGLContext::DeferredFlush",
                                         DeferredFlush);
    NS_DispatchToCurrentThread(std::move(runnable));
  }

  void CancelAutoFlush() const { mAutoFlushPending = false; }

  // -

  void AfterDrawCall() {
    if (!mNotLost) return;
    const auto& state = State();
    if (!state.mBoundDrawFb) {
      MarkCanvasDirty();
    }

    AutoEnqueueFlush();
  }

  // -------------------------------------------------------------------------
  // Client-side helper methods.  Dispatch to a Host method.
  // -------------------------------------------------------------------------

  // ------------------------- GL State -------------------------
 public:
  bool IsContextLost() const { return !mNotLost; }

  void Disable(GLenum cap) const { SetEnabledI(cap, {}, false); }
  void Enable(GLenum cap) const { SetEnabledI(cap, {}, true); }
  void SetEnabledI(GLenum cap, Maybe<GLuint> i, bool val) const;

  bool IsEnabled(GLenum cap) const;

 private:
  Maybe<double> GetNumber(GLenum pname);
  Maybe<std::string> GetString(GLenum pname);

 public:
  void GetParameter(JSContext* cx, GLenum pname,
                    JS::MutableHandle<JS::Value> retval, ErrorResult& rv,
                    bool debug = false);

  void GetBufferParameter(JSContext* cx, GLenum target, GLenum pname,
                          JS::MutableHandle<JS::Value> retval) const;

  void GetFramebufferAttachmentParameter(JSContext* cx, GLenum target,
                                         GLenum attachment, GLenum pname,
                                         JS::MutableHandle<JS::Value> retval,
                                         ErrorResult& rv) const;

  void GetRenderbufferParameter(JSContext* cx, GLenum target, GLenum pname,
                                JS::MutableHandle<JS::Value> retval) const;

  void GetIndexedParameter(JSContext* cx, GLenum target, GLuint index,
                           JS::MutableHandle<JS::Value> retval,
                           ErrorResult& rv) const;

  already_AddRefed<WebGLShaderPrecisionFormatJS> GetShaderPrecisionFormat(
      GLenum shadertype, GLenum precisiontype);

  void UseProgram(WebGLProgramJS*);
  void ValidateProgram(WebGLProgramJS&) const;

  // -

  already_AddRefed<WebGLBufferJS> CreateBuffer() const;
  already_AddRefed<WebGLFramebufferJS> CreateFramebuffer() const;
  already_AddRefed<WebGLFramebufferJS> CreateOpaqueFramebuffer(
      const webgl::OpaqueFramebufferOptions&) const;
  already_AddRefed<WebGLProgramJS> CreateProgram() const;
  already_AddRefed<WebGLQueryJS> CreateQuery() const;
  already_AddRefed<WebGLRenderbufferJS> CreateRenderbuffer() const;
  already_AddRefed<WebGLSamplerJS> CreateSampler() const;
  already_AddRefed<WebGLShaderJS> CreateShader(GLenum type) const;
  already_AddRefed<WebGLSyncJS> FenceSync(GLenum condition,
                                          GLbitfield flags) const;
  already_AddRefed<WebGLTextureJS> CreateTexture() const;
  already_AddRefed<WebGLTransformFeedbackJS> CreateTransformFeedback() const;
  already_AddRefed<WebGLVertexArrayJS> CreateVertexArray() const;

  void DeleteBuffer(WebGLBufferJS*);
  void DeleteFramebuffer(WebGLFramebufferJS*, bool canDeleteOpaque = false);
  void DeleteProgram(WebGLProgramJS*) const;
  void DeleteQuery(WebGLQueryJS*);
  void DeleteRenderbuffer(WebGLRenderbufferJS*);
  void DeleteSampler(WebGLSamplerJS*);
  void DeleteShader(WebGLShaderJS*) const;
  void DeleteSync(WebGLSyncJS*) const;
  void DeleteTexture(WebGLTextureJS*);
  void DeleteTransformFeedback(WebGLTransformFeedbackJS*);
  void DeleteVertexArray(WebGLVertexArrayJS*);

 private:
  void DoDeleteProgram(WebGLProgramJS&) const;
  void DoDeleteShader(const WebGLShaderJS&) const;

 public:
  // -

  bool IsBuffer(const WebGLBufferJS*) const;
  bool IsFramebuffer(const WebGLFramebufferJS*) const;
  bool IsProgram(const WebGLProgramJS*) const;
  bool IsQuery(const WebGLQueryJS*) const;
  bool IsRenderbuffer(const WebGLRenderbufferJS*) const;
  bool IsSampler(const WebGLSamplerJS*) const;
  bool IsShader(const WebGLShaderJS*) const;
  bool IsSync(const WebGLSyncJS*) const;
  bool IsTexture(const WebGLTextureJS*) const;
  bool IsTransformFeedback(const WebGLTransformFeedbackJS*) const;
  bool IsVertexArray(const WebGLVertexArrayJS*) const;

  // -
  // WebGLProgramJS

 private:
  const webgl::LinkResult& GetLinkResult(const WebGLProgramJS&) const;

 public:
  void AttachShader(WebGLProgramJS&, WebGLShaderJS&) const;
  void BindAttribLocation(WebGLProgramJS&, GLuint location,
                          const nsAString& name) const;
  void DetachShader(WebGLProgramJS&, const WebGLShaderJS&) const;
  void GetAttachedShaders(
      const WebGLProgramJS&,
      dom::Nullable<nsTArray<RefPtr<WebGLShaderJS>>>& retval) const;
  void LinkProgram(WebGLProgramJS&) const;
  void TransformFeedbackVaryings(WebGLProgramJS&,
                                 const dom::Sequence<nsString>& varyings,
                                 GLenum bufferMode) const;
  void UniformBlockBinding(WebGLProgramJS&, GLuint blockIndex,
                           GLuint blockBinding) const;

  // Link result reflection
  already_AddRefed<WebGLActiveInfoJS> GetActiveAttrib(const WebGLProgramJS&,
                                                      GLuint index);
  already_AddRefed<WebGLActiveInfoJS> GetActiveUniform(const WebGLProgramJS&,
                                                       GLuint index);
  void GetActiveUniformBlockName(const WebGLProgramJS&,
                                 GLuint uniformBlockIndex,
                                 nsAString& retval) const;
  void GetActiveUniformBlockParameter(JSContext* cx, const WebGLProgramJS&,
                                      GLuint uniformBlockIndex, GLenum pname,
                                      JS::MutableHandle<JS::Value> retval,
                                      ErrorResult& rv);
  void GetActiveUniforms(JSContext*, const WebGLProgramJS&,
                         const dom::Sequence<GLuint>& uniformIndices,
                         GLenum pname,
                         JS::MutableHandle<JS::Value> retval) const;
  GLint GetAttribLocation(const WebGLProgramJS&, const nsAString& name) const;
  GLint GetFragDataLocation(const WebGLProgramJS&, const nsAString& name) const;
  void GetProgramInfoLog(const WebGLProgramJS& prog, nsAString& retval) const;
  void GetProgramParameter(JSContext*, const WebGLProgramJS&, GLenum pname,
                           JS::MutableHandle<JS::Value> retval) const;
  already_AddRefed<WebGLActiveInfoJS> GetTransformFeedbackVarying(
      const WebGLProgramJS&, GLuint index);
  GLuint GetUniformBlockIndex(const WebGLProgramJS&,
                              const nsAString& uniformBlockName) const;
  void GetUniformIndices(const WebGLProgramJS&,
                         const dom::Sequence<nsString>& uniformNames,
                         dom::Nullable<nsTArray<GLuint>>& retval) const;

  // WebGLUniformLocationJS
  already_AddRefed<WebGLUniformLocationJS> GetUniformLocation(
      const WebGLProgramJS&, const nsAString& name) const;
  void GetUniform(JSContext*, const WebGLProgramJS&,
                  const WebGLUniformLocationJS&,
                  JS::MutableHandle<JS::Value> retval);

  // -
  // WebGLShaderJS

 private:
  const webgl::CompileResult& GetCompileResult(const WebGLShaderJS&) const;

 public:
  void CompileShader(WebGLShaderJS&) const;
  void GetShaderInfoLog(const WebGLShaderJS&, nsAString& retval) const;
  void GetShaderParameter(JSContext*, const WebGLShaderJS&, GLenum pname,
                          JS::MutableHandle<JS::Value> retval) const;
  void GetShaderSource(const WebGLShaderJS&, nsAString& retval) const;
  void GetTranslatedShaderSource(const WebGLShaderJS& shader,
                                 nsAString& retval) const;
  void ShaderSource(WebGLShaderJS&, const nsAString&) const;

  // -

  void BindFramebuffer(GLenum target, WebGLFramebufferJS*);

  void BlendColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a);

  // -

  void BlendEquation(GLenum mode) { BlendEquationSeparate(mode, mode); }
  void BlendFunc(GLenum sfactor, GLenum dfactor) {
    BlendFuncSeparate(sfactor, dfactor, sfactor, dfactor);
  }

  void BlendEquationSeparate(GLenum modeRGB, GLenum modeAlpha) {
    BlendEquationSeparateI({}, modeRGB, modeAlpha);
  }
  void BlendFuncSeparate(GLenum srcRGB, GLenum dstRGB, GLenum srcAlpha,
                         GLenum dstAlpha) {
    BlendFuncSeparateI({}, srcRGB, dstRGB, srcAlpha, dstAlpha);
  }

  void BlendEquationSeparateI(Maybe<GLuint> buf, GLenum modeRGB,
                              GLenum modeAlpha);
  void BlendFuncSeparateI(Maybe<GLuint> buf, GLenum srcRGB, GLenum dstRGB,
                          GLenum srcAlpha, GLenum dstAlpha);

  // -

  GLenum CheckFramebufferStatus(GLenum target);

  void Clear(GLbitfield mask);

  // -

 private:
  void ClearBufferTv(const GLenum buffer, const GLint drawBuffer,
                     const webgl::AttribBaseType type,
                     JS::AutoCheckCannotGC&& nogc,
                     const Span<const uint8_t>& view,
                     const GLuint srcElemOffset);

 public:
  void ClearBufferfv(GLenum buffer, GLint drawBuffer, const Float32ListU& list,
                     GLuint srcElemOffset) {
    const FuncScope funcScope(*this, "clearBufferfv");
    if (!Convert(list, [&](const Span<const float>& aData,
                           JS::AutoCheckCannotGC&& nogc) {
          ClearBufferTv(buffer, drawBuffer, webgl::AttribBaseType::Float,
                        std::move(nogc), AsBytes(aData), srcElemOffset);
          return true;
        })) {
      EnqueueError(LOCAL_GL_INVALID_VALUE, "`values` too small.");
    }
  }
  void ClearBufferiv(GLenum buffer, GLint drawBuffer, const Int32ListU& list,
                     GLuint srcElemOffset) {
    const FuncScope funcScope(*this, "clearBufferiv");
    if (!Convert(list, [&](const Span<const int32_t>& aData,
                           JS::AutoCheckCannotGC&& nogc) {
          ClearBufferTv(buffer, drawBuffer, webgl::AttribBaseType::Int,
                        std::move(nogc), AsBytes(aData), srcElemOffset);
          return true;
        })) {
      EnqueueError(LOCAL_GL_INVALID_VALUE, "`values` too small.");
    }
  }
  void ClearBufferuiv(GLenum buffer, GLint drawBuffer, const Uint32ListU& list,
                      GLuint srcElemOffset) {
    const FuncScope funcScope(*this, "clearBufferuiv");
    if (!Convert(list, [&](const Span<const uint32_t>& aData,
                           JS::AutoCheckCannotGC&& nogc) {
          ClearBufferTv(buffer, drawBuffer, webgl::AttribBaseType::Uint,
                        std::move(nogc), AsBytes(aData), srcElemOffset);
          return true;
        })) {
      EnqueueError(LOCAL_GL_INVALID_VALUE, "`values` too small.");
    }
  }

  // -

  void ClearBufferfi(GLenum buffer, GLint drawBuffer, GLfloat depth,
                     GLint stencil);

  void ClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a);

  void ClearDepth(GLclampf v);

  void ClearStencil(GLint v);

  void ColorMask(bool r, bool g, bool b, bool a) const {
    ColorMaskI({}, r, g, b, a);
  }
  void ColorMaskI(Maybe<GLuint> buf, bool r, bool g, bool b, bool a) const;

  void CullFace(GLenum face);

  void DepthFunc(GLenum func);

  void DepthMask(WebGLboolean b);

  void DepthRange(GLclampf zNear, GLclampf zFar);

  void Flush(bool flushGl = true) const;

  void SyncSnapshot() override { Flush(); }

  void Finish();

  void FrontFace(GLenum mode);

  GLenum GetError();

  void Hint(GLenum target, GLenum mode);

  void LineWidth(GLfloat width);

  void PixelStorei(GLenum pname, GLint param);

  void PolygonOffset(GLfloat factor, GLfloat units);

  void SampleCoverage(GLclampf value, WebGLboolean invert);

  void Scissor(GLint x, GLint y, GLsizei width, GLsizei height);

  // -

  void StencilFunc(GLenum func, GLint ref, GLuint mask) {
    StencilFuncSeparate(LOCAL_GL_FRONT_AND_BACK, func, ref, mask);
  }
  void StencilMask(GLuint mask) {
    StencilMaskSeparate(LOCAL_GL_FRONT_AND_BACK, mask);
  }
  void StencilOp(GLenum sfail, GLenum dpfail, GLenum dppass) {
    StencilOpSeparate(LOCAL_GL_FRONT_AND_BACK, sfail, dpfail, dppass);
  }

  void StencilFuncSeparate(GLenum face, GLenum func, GLint ref, GLuint mask);
  void StencilMaskSeparate(GLenum face, GLuint mask);
  void StencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail,
                         GLenum dppass);

  // -

  void Viewport(GLint x, GLint y, GLsizei width, GLsizei height);

  // ------------------------- Buffer Objects -------------------------
 public:
  void BindBuffer(GLenum target, WebGLBufferJS*);

  // -

 private:
  void BindBufferRangeImpl(const GLenum target, const GLuint index,
                           WebGLBufferJS* const buffer, const uint64_t offset,
                           const uint64_t size);

 public:
  void BindBufferBase(const GLenum target, const GLuint index,
                      WebGLBufferJS* const buffer) {
    const FuncScope funcScope(*this, "bindBufferBase");
    if (IsContextLost()) return;

    BindBufferRangeImpl(target, index, buffer, 0, 0);
  }

  void BindBufferRange(const GLenum target, const GLuint index,
                       WebGLBufferJS* const buffer, const WebGLintptr offset,
                       const WebGLsizeiptr size) {
    const FuncScope funcScope(*this, "bindBufferRange");
    if (IsContextLost()) return;

    if (buffer) {
      if (!ValidateNonNegative("offset", offset)) return;

      if (size < 1) {
        EnqueueError(LOCAL_GL_INVALID_VALUE,
                     "`size` must be positive for non-null `buffer`.");
        return;
      }
    }

    BindBufferRangeImpl(target, index, buffer, static_cast<uint64_t>(offset),
                        static_cast<uint64_t>(size));
  }

  // -

  void CopyBufferSubData(GLenum readTarget, GLenum writeTarget,
                         GLintptr readOffset, GLintptr writeOffset,
                         GLsizeiptr size);

  void BufferData(GLenum target, WebGLsizeiptr size, GLenum usage);
  void BufferData(GLenum target,
                  const dom::Nullable<dom::ArrayBuffer>& maybeSrc,
                  GLenum usage);
  void BufferData(GLenum target, const dom::ArrayBufferView& srcData,
                  GLenum usage, GLuint srcElemOffset = 0,
                  GLuint srcElemCountOverride = 0);

  void BufferSubData(GLenum target, WebGLsizeiptr dstByteOffset,
                     const dom::ArrayBufferView& src, GLuint srcElemOffset = 0,
                     GLuint srcElemCountOverride = 0);
  void BufferSubData(GLenum target, WebGLsizeiptr dstByteOffset,
                     const dom::ArrayBuffer& src);

  void GetBufferSubData(GLenum target, GLintptr srcByteOffset,
                        const dom::ArrayBufferView& dstData,
                        GLuint dstElemOffset, GLuint dstElemCountOverride);

  // -------------------------- Framebuffer Objects --------------------------

  void BlitFramebuffer(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                       GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                       GLbitfield mask, GLenum filter);

  // -

 private:
  // `bindTarget` if non-zero allows initializing the rb/tex with that target.
  void FramebufferAttach(GLenum target, GLenum attachEnum, GLenum bindTarget,
                         WebGLRenderbufferJS*, WebGLTextureJS*,
                         uint32_t mipLevel, uint32_t zLayer,
                         uint32_t numViewLayers) const;

 public:
  void FramebufferRenderbuffer(GLenum target, GLenum attachSlot,
                               GLenum rbTarget, WebGLRenderbufferJS* rb) const {
    const FuncScope funcScope(*this, "framebufferRenderbuffer");
    if (IsContextLost()) return;
    if (rbTarget != LOCAL_GL_RENDERBUFFER) {
      EnqueueError_ArgEnum("rbTarget", rbTarget);
      return;
    }
    FramebufferAttach(target, attachSlot, rbTarget, rb, nullptr, 0, 0, 0);
  }

  void FramebufferTexture2D(GLenum target, GLenum attachSlot,
                            GLenum texImageTarget, WebGLTextureJS*,
                            GLint mipLevel) const;

  void FramebufferTextureLayer(GLenum target, GLenum attachSlot,
                               WebGLTextureJS* tex, GLint mipLevel,
                               GLint zLayer) const {
    const FuncScope funcScope(*this, "framebufferTextureLayer");
    if (IsContextLost()) return;
    FramebufferAttach(target, attachSlot, 0, nullptr, tex,
                      static_cast<uint32_t>(mipLevel),
                      static_cast<uint32_t>(zLayer), 0);
  }

  void FramebufferTextureMultiview(GLenum target, GLenum attachSlot,
                                   WebGLTextureJS* tex, GLint mipLevel,
                                   GLint zLayerBase,
                                   GLsizei numViewLayers) const {
    const FuncScope funcScope(*this, "framebufferTextureMultiview");
    if (IsContextLost()) return;
    if (tex && numViewLayers < 1) {
      EnqueueError(LOCAL_GL_INVALID_VALUE, "`numViewLayers` must be >=1.");
      return;
    }
    FramebufferAttach(target, attachSlot, 0, nullptr, tex,
                      static_cast<uint32_t>(mipLevel),
                      static_cast<uint32_t>(zLayerBase),
                      static_cast<uint32_t>(numViewLayers));
  }

  // -

  void InvalidateFramebuffer(GLenum target,
                             const dom::Sequence<GLenum>& attachments,
                             ErrorResult& unused);
  void InvalidateSubFramebuffer(GLenum target,
                                const dom::Sequence<GLenum>& attachments,
                                GLint x, GLint y, GLsizei width, GLsizei height,
                                ErrorResult& unused);

  void ReadBuffer(GLenum mode);

  // ----------------------- Renderbuffer objects -----------------------
  void GetInternalformatParameter(JSContext* cx, GLenum target,
                                  GLenum internalformat, GLenum pname,
                                  JS::MutableHandle<JS::Value> retval,
                                  ErrorResult& rv);

  void BindRenderbuffer(GLenum target, WebGLRenderbufferJS*);

  void RenderbufferStorage(GLenum target, GLenum internalFormat, GLsizei width,
                           GLsizei height) const {
    RenderbufferStorageMultisample(target, 0, internalFormat, width, height);
  }

  void RenderbufferStorageMultisample(GLenum target, GLsizei samples,
                                      GLenum internalFormat, GLsizei width,
                                      GLsizei height) const;

  // --------------------------- Texture objects ---------------------------

  void ActiveTexture(GLenum texUnit);

  void BindTexture(GLenum texTarget, WebGLTextureJS*);

  void GenerateMipmap(GLenum texTarget) const;

  void GetTexParameter(JSContext* cx, GLenum texTarget, GLenum pname,
                       JS::MutableHandle<JS::Value> retval) const;

  void TexParameterf(GLenum texTarget, GLenum pname, GLfloat param);
  void TexParameteri(GLenum texTarget, GLenum pname, GLint param);

  // -

 private:
  void TexStorage(uint8_t funcDims, GLenum target, GLsizei levels,
                  GLenum internalFormat, const ivec3& size) const;

  // Primitive tex upload functions
  void TexImage(uint8_t funcDims, GLenum target, GLint level,
                GLenum respecFormat, const ivec3& offset,
                const Maybe<ivec3>& size, GLint border,
                const webgl::PackingInfo& pi, const TexImageSource& src) const;
  void CompressedTexImage(bool sub, uint8_t funcDims, GLenum target,
                          GLint level, GLenum format, const ivec3& offset,
                          const ivec3& size, GLint border,
                          const TexImageSource& src,
                          GLsizei pboImageSize) const;
  void CopyTexImage(uint8_t funcDims, GLenum target, GLint level,
                    GLenum respecFormat, const ivec3& dstOffset,
                    const ivec2& srcOffset, const ivec2& size,
                    GLint border) const;

 public:
  void TexStorage2D(GLenum target, GLsizei levels, GLenum internalFormat,
                    GLsizei width, GLsizei height) const {
    TexStorage(2, target, levels, internalFormat, {width, height, 1});
  }

  void TexStorage3D(GLenum target, GLsizei levels, GLenum internalFormat,
                    GLsizei width, GLsizei height, GLsizei depth) const {
    TexStorage(3, target, levels, internalFormat, {width, height, depth});
  }

  ////////////////////////////////////

  template <typename T>  // TexImageSource or WebGLintptr
  void TexImage2D(GLenum target, GLint level, GLenum internalFormat,
                  GLsizei width, GLsizei height, GLint border,
                  GLenum unpackFormat, GLenum unpackType, const T& anySrc,
                  ErrorResult& out_error) const {
    const TexImageSourceAdapter src(&anySrc, &out_error);
    TexImage(2, target, level, internalFormat, {0, 0, 0},
             Some(ivec3{width, height, 1}), border, {unpackFormat, unpackType},
             src);
  }

  void TexImage2D(GLenum target, GLint level, GLenum internalFormat,
                  GLsizei width, GLsizei height, GLint border,
                  GLenum unpackFormat, GLenum unpackType,
                  const dom::ArrayBufferView& view, GLuint viewElemOffset,
                  ErrorResult&) const {
    const TexImageSourceAdapter src(&view, viewElemOffset);
    TexImage(2, target, level, internalFormat, {0, 0, 0},
             Some(ivec3{width, height, 1}), border, {unpackFormat, unpackType},
             src);
  }

  // -

  template <typename T>  // TexImageSource or WebGLintptr
  void TexSubImage2D(GLenum target, GLint level, GLint xOffset, GLint yOffset,
                     GLsizei width, GLsizei height, GLenum unpackFormat,
                     GLenum unpackType, const T& anySrc,
                     ErrorResult& out_error) const {
    const TexImageSourceAdapter src(&anySrc, &out_error);
    TexImage(2, target, level, 0, {xOffset, yOffset, 0},
             Some(ivec3{width, height, 1}), 0, {unpackFormat, unpackType}, src);
  }

  void TexSubImage2D(GLenum target, GLint level, GLint xOffset, GLint yOffset,
                     GLsizei width, GLsizei height, GLenum unpackFormat,
                     GLenum unpackType, const dom::ArrayBufferView& view,
                     GLuint viewElemOffset, ErrorResult&) const {
    const TexImageSourceAdapter src(&view, viewElemOffset);
    TexImage(2, target, level, 0, {xOffset, yOffset, 0},
             Some(ivec3{width, height, 1}), 0, {unpackFormat, unpackType}, src);
  }

  // -

  template <typename T>  // TexImageSource or WebGLintptr
  void TexImage3D(GLenum target, GLint level, GLenum internalFormat,
                  GLsizei width, GLsizei height, GLsizei depth, GLint border,
                  GLenum unpackFormat, GLenum unpackType, const T& anySrc,
                  ErrorResult& out_error) const {
    const TexImageSourceAdapter src(&anySrc, &out_error);
    TexImage(3, target, level, internalFormat, {0, 0, 0},
             Some(ivec3{width, height, depth}), border,
             {unpackFormat, unpackType}, src);
  }

  void TexImage3D(GLenum target, GLint level, GLenum internalFormat,
                  GLsizei width, GLsizei height, GLsizei depth, GLint border,
                  GLenum unpackFormat, GLenum unpackType,
                  const dom::ArrayBufferView& view, GLuint viewElemOffset,
                  ErrorResult&) const {
    const TexImageSourceAdapter src(&view, viewElemOffset);
    TexImage(3, target, level, internalFormat, {0, 0, 0},
             Some(ivec3{width, height, depth}), border,
             {unpackFormat, unpackType}, src);
  }

  // -

  template <typename T>  // TexImageSource or WebGLintptr
  void TexSubImage3D(GLenum target, GLint level, GLint xOffset, GLint yOffset,
                     GLint zOffset, GLsizei width, GLsizei height,
                     GLsizei depth, GLenum unpackFormat, GLenum unpackType,
                     const T& anySrc, ErrorResult& out_error) const {
    const TexImageSourceAdapter src(&anySrc, &out_error);
    TexImage(3, target, level, 0, {xOffset, yOffset, zOffset},
             Some(ivec3{width, height, depth}), 0, {unpackFormat, unpackType},
             src);
  }

  void TexSubImage3D(GLenum target, GLint level, GLint xOffset, GLint yOffset,
                     GLint zOffset, GLsizei width, GLsizei height,
                     GLsizei depth, GLenum unpackFormat, GLenum unpackType,
                     const dom::Nullable<dom::ArrayBufferView>& maybeSrcView,
                     GLuint srcElemOffset, ErrorResult&) const {
    const TexImageSourceAdapter src(&maybeSrcView, srcElemOffset);
    TexImage(3, target, level, 0, {xOffset, yOffset, zOffset},
             Some(ivec3{width, height, depth}), 0, {unpackFormat, unpackType},
             src);
  }

  ////////////////////////////////////

 public:
  void CompressedTexImage2D(GLenum target, GLint level, GLenum internalFormat,
                            GLsizei width, GLsizei height, GLint border,
                            GLsizei imageSize, WebGLintptr offset) const {
    const TexImageSourceAdapter src(&offset);
    CompressedTexImage(false, 2, target, level, internalFormat, {0, 0, 0},
                       {width, height, 1}, border, src, imageSize);
  }

  void CompressedTexImage2D(GLenum target, GLint level, GLenum internalFormat,
                            GLsizei width, GLsizei height, GLint border,
                            const dom::ArrayBufferView& view,
                            GLuint viewElemOffset = 0,
                            GLuint viewElemLengthOverride = 0) const {
    const TexImageSourceAdapter src(&view, viewElemOffset,
                                    viewElemLengthOverride);
    CompressedTexImage(false, 2, target, level, internalFormat, {0, 0, 0},
                       {width, height, 1}, border, src, 0);
  }

  // -

  void CompressedTexSubImage2D(GLenum target, GLint level, GLint xOffset,
                               GLint yOffset, GLsizei width, GLsizei height,
                               GLenum unpackFormat, GLsizei imageSize,
                               WebGLintptr offset) const {
    const TexImageSourceAdapter src(&offset);
    CompressedTexImage(true, 2, target, level, unpackFormat,
                       {xOffset, yOffset, 0}, {width, height, 1}, 0, src,
                       imageSize);
  }

  void CompressedTexSubImage2D(GLenum target, GLint level, GLint xOffset,
                               GLint yOffset, GLsizei width, GLsizei height,
                               GLenum unpackFormat,
                               const dom::ArrayBufferView& view,
                               GLuint viewElemOffset = 0,
                               GLuint viewElemLengthOverride = 0) const {
    const TexImageSourceAdapter src(&view, viewElemOffset,
                                    viewElemLengthOverride);
    CompressedTexImage(true, 2, target, level, unpackFormat,
                       {xOffset, yOffset, 0}, {width, height, 1}, 0, src, 0);
  }

  // -

  void CompressedTexImage3D(GLenum target, GLint level, GLenum internalFormat,
                            GLsizei width, GLsizei height, GLsizei depth,
                            GLint border, GLsizei imageSize,
                            WebGLintptr offset) const {
    const TexImageSourceAdapter src(&offset);
    CompressedTexImage(false, 3, target, level, internalFormat, {0, 0, 0},
                       {width, height, depth}, border, src, imageSize);
  }

  void CompressedTexImage3D(GLenum target, GLint level, GLenum internalFormat,
                            GLsizei width, GLsizei height, GLsizei depth,
                            GLint border, const dom::ArrayBufferView& view,
                            GLuint viewElemOffset = 0,
                            GLuint viewElemLengthOverride = 0) const {
    const TexImageSourceAdapter src(&view, viewElemOffset,
                                    viewElemLengthOverride);
    CompressedTexImage(false, 3, target, level, internalFormat, {0, 0, 0},
                       {width, height, depth}, border, src, 0);
  }

  // -

  void CompressedTexSubImage3D(GLenum target, GLint level, GLint xOffset,
                               GLint yOffset, GLint zOffset, GLsizei width,
                               GLsizei height, GLsizei depth,
                               GLenum unpackFormat, GLsizei imageSize,
                               WebGLintptr offset) const {
    const TexImageSourceAdapter src(&offset);
    CompressedTexImage(true, 3, target, level, unpackFormat,
                       {xOffset, yOffset, zOffset}, {width, height, depth}, 0,
                       src, imageSize);
  }

  void CompressedTexSubImage3D(GLenum target, GLint level, GLint xOffset,
                               GLint yOffset, GLint zOffset, GLsizei width,
                               GLsizei height, GLsizei depth,
                               GLenum unpackFormat,
                               const dom::ArrayBufferView& view,
                               GLuint viewElemOffset = 0,
                               GLuint viewElemLengthOverride = 0) const {
    const TexImageSourceAdapter src(&view, viewElemOffset,
                                    viewElemLengthOverride);
    CompressedTexImage(true, 3, target, level, unpackFormat,
                       {xOffset, yOffset, zOffset}, {width, height, depth}, 0,
                       src, 0);
  }

  // --------------------

  void CopyTexImage2D(GLenum target, GLint level, GLenum internalFormat,
                      GLint x, GLint y, GLsizei width, GLsizei height,
                      GLint border) const {
    CopyTexImage(2, target, level, internalFormat, {0, 0, 0}, {x, y},
                 {width, height}, border);
  }

  void CopyTexSubImage2D(GLenum target, GLint level, GLint xOffset,
                         GLint yOffset, GLint x, GLint y, GLsizei width,
                         GLsizei height) const {
    CopyTexImage(2, target, level, 0, {xOffset, yOffset, 0}, {x, y},
                 {width, height}, 0);
  }

  void CopyTexSubImage3D(GLenum target, GLint level, GLint xOffset,
                         GLint yOffset, GLint zOffset, GLint x, GLint y,
                         GLsizei width, GLsizei height) const {
    CopyTexImage(3, target, level, 0, {xOffset, yOffset, zOffset}, {x, y},
                 {width, height}, 0);
  }

  // -------------------
  // legacy TexImageSource uploads without width/height.
  // The width/height params are webgl2 only, and let you do subrect
  // selection with e.g. width < UNPACK_ROW_LENGTH.

  template <typename TexImageSourceT>
  void TexImage2D(GLenum target, GLint level, GLenum internalFormat,
                  GLenum unpackFormat, GLenum unpackType,
                  const TexImageSourceT& anySrc, ErrorResult& out_error) const {
    const TexImageSourceAdapter src(&anySrc, &out_error);
    TexImage(2, target, level, internalFormat, {}, {}, 0,
             {unpackFormat, unpackType}, src);
  }

  template <typename TexImageSourceT>
  void TexSubImage2D(GLenum target, GLint level, GLint xOffset, GLint yOffset,
                     GLenum unpackFormat, GLenum unpackType,
                     const TexImageSourceT& anySrc,
                     ErrorResult& out_error) const {
    const TexImageSourceAdapter src(&anySrc, &out_error);
    TexImage(2, target, level, 0, {xOffset, yOffset, 0}, {}, 0,
             {unpackFormat, unpackType}, src);
  }

  // ------------------------ Uniforms and attributes ------------------------

 private:
  Maybe<double> GetVertexAttribPriv(GLuint index, GLenum pname);

 public:
  void GetVertexAttrib(JSContext* cx, GLuint index, GLenum pname,
                       JS::MutableHandle<JS::Value> retval, ErrorResult& rv);

 private:
  const webgl::LinkResult* GetActiveLinkResult() const {
    const auto& state = State();
    if (state.mCurrentProgram) {
      (void)GetLinkResult(*state.mCurrentProgram);
    }
    return state.mActiveLinkResult.get();
  }

  // Used by callers that have a `nogc` token that ensures that no GC will
  // happen while `bytes` is alive. Internally, the nogc range will be ended
  // after `bytes` is used (either successfully but where the data are possibly
  // sent over IPC which has a tendency to GC, or unsuccesfully in which case
  // error handling can GC.)
  void UniformData(GLenum funcElemType, const WebGLUniformLocationJS* const loc,
                   bool transpose, const Range<const uint8_t>& bytes,
                   JS::AutoCheckCannotGC&& nogc, GLuint elemOffset = 0,
                   GLuint elemCountOverride = 0) const;

  // Used by callers that are not passing `bytes` that might be GC-controlled.
  // This will create an artificial and unnecessary nogc region that should
  // get optimized away to nothing.
  void UniformData(GLenum funcElemType, const WebGLUniformLocationJS* const loc,
                   bool transpose, const Range<const uint8_t>& bytes,
                   GLuint elemOffset = 0, GLuint elemCountOverride = 0) const {
    JS::AutoCheckCannotGC nogc;
    UniformData(funcElemType, loc, transpose, bytes, std::move(nogc),
                elemOffset, elemCountOverride);
  }

  // -

  template <typename T>
  Maybe<Range<T>> ValidateSubrange(const Range<T>& data, size_t elemOffset,
                                   size_t elemLengthOverride = 0) const {
    auto ret = data;
    if (elemOffset > ret.length()) {
      EnqueueError(LOCAL_GL_INVALID_VALUE,
                   "`elemOffset` too large for `data`.");
      return {};
    }
    ret = {ret.begin() + elemOffset, ret.end()};
    if (elemLengthOverride) {
      if (elemLengthOverride > ret.length()) {
        EnqueueError(
            LOCAL_GL_INVALID_VALUE,
            "`elemLengthOverride` too large for `data` and `elemOffset`.");
        return {};
      }
      ret = {ret.begin().get(), elemLengthOverride};
    }
    return Some(ret);
  }

 public:
#define _(T, type_t, TYPE)                                                    \
  void Uniform1##T(const WebGLUniformLocationJS* const loc, type_t x) const { \
    const type_t arr[] = {x};                                                 \
    UniformData(TYPE, loc, false, MakeByteRange(arr));                        \
  }                                                                           \
  void Uniform2##T(const WebGLUniformLocationJS* const loc, type_t x,         \
                   type_t y) const {                                          \
    const type_t arr[] = {x, y};                                              \
    UniformData(TYPE##_VEC2, loc, false, MakeByteRange(arr));                 \
  }                                                                           \
  void Uniform3##T(const WebGLUniformLocationJS* const loc, type_t x,         \
                   type_t y, type_t z) const {                                \
    const type_t arr[] = {x, y, z};                                           \
    UniformData(TYPE##_VEC3, loc, false, MakeByteRange(arr));                 \
  }                                                                           \
  void Uniform4##T(const WebGLUniformLocationJS* const loc, type_t x,         \
                   type_t y, type_t z, type_t w) const {                      \
    const type_t arr[] = {x, y, z, w};                                        \
    UniformData(TYPE##_VEC4, loc, false, MakeByteRange(arr));                 \
  }

  _(f, float, LOCAL_GL_FLOAT)
  _(i, int32_t, LOCAL_GL_INT)
  _(ui, uint32_t, LOCAL_GL_UNSIGNED_INT)

#undef _

  // -

#define _(NT, TypeListU, TYPE)                                             \
  void Uniform##NT##v(const WebGLUniformLocationJS* const loc,             \
                      const TypeListU& list, GLuint elemOffset = 0,        \
                      GLuint elemCountOverride = 0) const {                \
    Convert(list, [&](const auto& aData, JS::AutoCheckCannotGC&& nogc) {   \
      UniformData(TYPE, loc, false, MakeByteRange(aData), std::move(nogc), \
                  elemOffset, elemCountOverride);                          \
      return true;                                                         \
    });                                                                    \
  }

  _(1f, Float32ListU, LOCAL_GL_FLOAT)
  _(2f, Float32ListU, LOCAL_GL_FLOAT_VEC2)
  _(3f, Float32ListU, LOCAL_GL_FLOAT_VEC3)
  _(4f, Float32ListU, LOCAL_GL_FLOAT_VEC4)
  _(1i, Int32ListU, LOCAL_GL_INT)
  _(2i, Int32ListU, LOCAL_GL_INT_VEC2)
  _(3i, Int32ListU, LOCAL_GL_INT_VEC3)
  _(4i, Int32ListU, LOCAL_GL_INT_VEC4)
  _(1ui, Uint32ListU, LOCAL_GL_UNSIGNED_INT)
  _(2ui, Uint32ListU, LOCAL_GL_UNSIGNED_INT_VEC2)
  _(3ui, Uint32ListU, LOCAL_GL_UNSIGNED_INT_VEC3)
  _(4ui, Uint32ListU, LOCAL_GL_UNSIGNED_INT_VEC4)

#undef _

  // -

#define _(X)                                                                   \
  void UniformMatrix##X##fv(const WebGLUniformLocationJS* loc, bool transpose, \
                            const Float32ListU& list, GLuint elemOffset = 0,   \
                            GLuint elemCountOverride = 0) const {              \
    Convert(list, [&](const Span<const float>& aData,                          \
                      JS::AutoCheckCannotGC&& nogc) {                          \
      UniformData(LOCAL_GL_FLOAT_MAT##X, loc, transpose, MakeByteRange(aData), \
                  std::move(nogc), elemOffset, elemCountOverride);             \
      return true;                                                             \
    });                                                                        \
  }

  _(2)
  _(2x3)
  _(2x4)

  _(3x2)
  _(3)
  _(3x4)

  _(4x2)
  _(4x3)
  _(4)

#undef _

  // -

  void EnableVertexAttribArray(GLuint index);

  void DisableVertexAttribArray(GLuint index);

  WebGLsizeiptr GetVertexAttribOffset(GLuint index, GLenum pname);

  // -

 private:
  void VertexAttrib4Tv(GLuint index, webgl::AttribBaseType,
                       const Range<const uint8_t>&);

 public:
  void VertexAttrib1f(GLuint index, GLfloat x) {
    VertexAttrib4f(index, x, 0, 0, 1);
  }
  void VertexAttrib2f(GLuint index, GLfloat x, GLfloat y) {
    VertexAttrib4f(index, x, y, 0, 1);
  }
  void VertexAttrib3f(GLuint index, GLfloat x, GLfloat y, GLfloat z) {
    VertexAttrib4f(index, x, y, z, 1);
  }

  void VertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z,
                      GLfloat w) {
    const float arr[4] = {x, y, z, w};
    VertexAttrib4Tv(index, webgl::AttribBaseType::Float, MakeByteRange(arr));
  }

  // -

  template <typename List, typename T, size_t N>
  bool MakeArrayFromList(const List& list, T (&array)[N]) {
    bool badLength = false;
    if (!Convert(list, [&](const auto& aData, JS::AutoCheckCannotGC&&) {
          static_assert(
              std::is_same_v<std::remove_const_t<
                                 std::remove_reference_t<decltype(aData[0])>>,
                             T>);
          if (N > aData.Length()) {
            badLength = true;
            return false;
          }

          std::copy_n(aData.begin(), N, array);
          return true;
        })) {
      EnqueueError(
          LOCAL_GL_INVALID_VALUE,
          badLength
              ? nsPrintfCString("Length of `list` must be >=%zu.", N).get()
              : "Conversion of `list` failed.");
      return false;
    }
    return true;
  }

  void VertexAttrib1fv(const GLuint index, const Float32ListU& list) {
    const FuncScope funcScope(*this, "vertexAttrib1fv");
    if (IsContextLost()) return;

    float arr[1];
    if (!MakeArrayFromList(list, arr)) {
      return;
    }
    VertexAttrib1f(index, arr[0]);
  }

  void VertexAttrib2fv(const GLuint index, const Float32ListU& list) {
    const FuncScope funcScope(*this, "vertexAttrib1fv");
    if (IsContextLost()) return;

    float arr[2];
    if (!MakeArrayFromList(list, arr)) {
      return;
    }
    VertexAttrib2f(index, arr[0], arr[1]);
  }

  void VertexAttrib3fv(const GLuint index, const Float32ListU& list) {
    const FuncScope funcScope(*this, "vertexAttrib1fv");
    if (IsContextLost()) return;

    float arr[3];
    if (!MakeArrayFromList(list, arr)) {
      return;
    }
    VertexAttrib3f(index, arr[0], arr[1], arr[2]);
  }

  void VertexAttrib4fv(GLuint index, const Float32ListU& list) {
    const FuncScope funcScope(*this, "vertexAttrib4fv");
    float arr[4];
    if (!MakeArrayFromList(list, arr)) {
      return;
    }
    VertexAttrib4Tv(index, webgl::AttribBaseType::Float, MakeByteRange(arr));
  }
  void VertexAttribI4iv(GLuint index, const Int32ListU& list) {
    const FuncScope funcScope(*this, "vertexAttribI4iv");
    int32_t arr[4];
    if (!MakeArrayFromList(list, arr)) {
      return;
    }
    VertexAttrib4Tv(index, webgl::AttribBaseType::Int, MakeByteRange(arr));
  }
  void VertexAttribI4uiv(GLuint index, const Uint32ListU& list) {
    const FuncScope funcScope(*this, "vertexAttribI4uiv");
    uint32_t arr[4];
    if (!MakeArrayFromList(list, arr)) {
      return;
    }
    VertexAttrib4Tv(index, webgl::AttribBaseType::Uint, MakeByteRange(arr));
  }

  void VertexAttribI4i(GLuint index, GLint x, GLint y, GLint z, GLint w) {
    const int32_t arr[4] = {x, y, z, w};
    VertexAttrib4Tv(index, webgl::AttribBaseType::Int, MakeByteRange(arr));
  }
  void VertexAttribI4ui(GLuint index, GLuint x, GLuint y, GLuint z, GLuint w) {
    const uint32_t arr[4] = {x, y, z, w};
    VertexAttrib4Tv(index, webgl::AttribBaseType::Uint, MakeByteRange(arr));
  }

 private:
  void VertexAttribPointerImpl(bool isFuncInt, GLuint index, GLint size,
                               GLenum type, WebGLboolean normalized,
                               GLsizei iStride, WebGLintptr iByteOffset);

 public:
  void VertexAttribIPointer(GLuint index, GLint size, GLenum type,
                            GLsizei stride, WebGLintptr byteOffset) {
    VertexAttribPointerImpl(true, index, size, type, false, stride, byteOffset);
  }

  void VertexAttribPointer(GLuint index, GLint size, GLenum type,
                           WebGLboolean normalized, GLsizei stride,
                           WebGLintptr byteOffset) {
    VertexAttribPointerImpl(false, index, size, type, normalized, stride,
                            byteOffset);
  }

  // -------------------------------- Drawing -------------------------------
 public:
  void DrawArrays(GLenum mode, GLint first, GLsizei count) {
    DrawArraysInstanced(mode, first, count, 1);
  }

  void DrawElements(GLenum mode, GLsizei count, GLenum type,
                    WebGLintptr byteOffset) {
    DrawElementsInstanced(mode, count, type, byteOffset, 1);
  }

  void DrawRangeElements(GLenum mode, GLuint start, GLuint end, GLsizei count,
                         GLenum type, WebGLintptr byteOffset) {
    const FuncScope funcScope(*this, "drawRangeElements");
    if (end < start) {
      EnqueueError(LOCAL_GL_INVALID_VALUE, "end must be >= start.");
      return;
    }
    DrawElementsInstanced(mode, count, type, byteOffset, 1);
  }

  // ------------------------------ Readback -------------------------------
 public:
  void ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type,
                  const dom::Nullable<dom::ArrayBufferView>& maybeView,
                  dom::CallerType aCallerType, ErrorResult& out_error) const {
    const FuncScope funcScope(*this, "readPixels");
    if (!ValidateNonNull("pixels", maybeView)) return;
    ReadPixels(x, y, width, height, format, type, maybeView.Value(), 0,
               aCallerType, out_error);
  }

  void ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type, WebGLsizeiptr offset,
                  dom::CallerType aCallerType, ErrorResult& out_error) const;

  void ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type,
                  const dom::ArrayBufferView& dstData, GLuint dstElemOffset,
                  dom::CallerType aCallerType, ErrorResult& out_error) const;

 protected:
  bool ReadPixels_SharedPrecheck(GLenum* inout_readType,
                                 dom::CallerType aCallerType,
                                 ErrorResult& out_error) const;

  // ------------------------------ Vertex Array ------------------------------
 public:
  void BindVertexArray(WebGLVertexArrayJS*);

  void DrawArraysInstanced(GLenum mode, GLint first, GLsizei count,
                           GLsizei primcount);

  void DrawElementsInstanced(GLenum mode, GLsizei count, GLenum type,
                             WebGLintptr offset, GLsizei primcount);

  void VertexAttribDivisor(GLuint index, GLuint divisor);

  // --------------------------------- GL Query
  // ---------------------------------
 public:
  void GetQuery(JSContext*, GLenum target, GLenum pname,
                JS::MutableHandle<JS::Value> retval) const;
  void GetQueryParameter(JSContext*, WebGLQueryJS&, GLenum pname,
                         JS::MutableHandle<JS::Value> retval) const;
  void BeginQuery(GLenum target, WebGLQueryJS&);
  void EndQuery(GLenum target);
  void QueryCounter(WebGLQueryJS&, GLenum target) const;

  // -------------------------------- Sampler -------------------------------

  void GetSamplerParameter(JSContext*, const WebGLSamplerJS&, GLenum pname,
                           JS::MutableHandle<JS::Value> retval) const;

  void BindSampler(GLuint unit, WebGLSamplerJS*);
  void SamplerParameteri(WebGLSamplerJS&, GLenum pname, GLint param) const;
  void SamplerParameterf(WebGLSamplerJS&, GLenum pname, GLfloat param) const;

  // ------------------------------- GL Sync ---------------------------------

  GLenum ClientWaitSync(WebGLSyncJS&, GLbitfield flags, GLuint64 timeout) const;
  void GetSyncParameter(JSContext*, WebGLSyncJS&, GLenum pname,
                        JS::MutableHandle<JS::Value> retval) const;
  void WaitSync(const WebGLSyncJS&, GLbitfield flags, GLint64 timeout) const;

  mutable webgl::ObjectId mCompletedSyncId = 0;
  void OnSyncComplete(webgl::ObjectId id) const {
    if (mCompletedSyncId < id) {
      mCompletedSyncId = id;
    }
  }

  // -------------------------- Transform Feedback ---------------------------

  void BindTransformFeedback(GLenum target, WebGLTransformFeedbackJS*);
  void BeginTransformFeedback(GLenum primitiveMode);
  void EndTransformFeedback();
  void PauseTransformFeedback();
  void ResumeTransformFeedback();

  // -------------------------- Opaque Framebuffers ---------------------------

  void SetFramebufferIsInOpaqueRAF(WebGLFramebufferJS*, bool);

  // ------------------------------ Extensions ------------------------------
 public:
  void GetSupportedExtensions(dom::Nullable<nsTArray<nsString>>& retval,
                              dom::CallerType callerType) const;

  bool IsSupported(WebGLExtensionID, dom::CallerType callerType =
                                         dom::CallerType::NonSystem) const;

  void GetExtension(JSContext* cx, const nsAString& name,
                    JS::MutableHandle<JSObject*> retval,
                    dom::CallerType callerType, ErrorResult& rv);

 protected:
  bool IsExtensionForbiddenForCaller(const WebGLExtensionID ext,
                                     const dom::CallerType callerType) const;

  RefPtr<ClientWebGLExtensionBase> GetExtension(WebGLExtensionID ext,
                                                dom::CallerType callerType);
  void RequestExtension(WebGLExtensionID) const;

 public:
  bool IsExtensionEnabled(const WebGLExtensionID id) const {
    return bool(mNotLost->extensions[UnderlyingValue(id)]);
  }

  void AddCompressedFormat(GLenum);

  // ---------------------------- Misc Extensions ----------------------------
 public:
  void DrawBuffers(const dom::Sequence<GLenum>& buffers);

  void GetSupportedProfilesASTC(
      dom::Nullable<nsTArray<nsString>>& retval) const;

  void MOZDebugGetParameter(JSContext* cx, GLenum pname,
                            JS::MutableHandle<JS::Value> retval,
                            ErrorResult& rv) {
    GetParameter(cx, pname, retval, rv, true);
  }

  void ProvokingVertex(GLenum rawMode) const;

  // -------------------------------------------------------------------------
  // Client-side methods.  Calls in the Host are forwarded to the client.
  // -------------------------------------------------------------------------
 public:
  void JsWarning(const std::string&) const;

  // -------------------------------------------------------------------------
  // The cross-process communication mechanism
  // -------------------------------------------------------------------------
 protected:
  // If we are running WebGL in this process then call the HostWebGLContext
  // method directly.  Otherwise, dispatch over IPC.
  template <typename MethodType, MethodType method, typename... CallerArgs>
  void Run(const CallerArgs&... args) const {
    const auto id = IdByMethod<MethodType, method>();
    auto noNoGc = std::optional<JS::AutoCheckCannotGC>{};
    Run_WithDestArgTypes_ConstnessHelper(std::move(noNoGc), method, id,
                                         args...);
  }

  // Same as above for use when using potentially GC-controlled data. The scope
  // of `aNoGC` will be ended after the data is no longer needed.
  template <typename MethodType, MethodType method, typename... CallerArgs>
  void RunWithGCData(JS::AutoCheckCannotGC&& aNoGC,
                     const CallerArgs&... aArgs) const {
    const auto id = IdByMethod<MethodType, method>();
    auto noGc = std::optional<JS::AutoCheckCannotGC>{std::move(aNoGC)};
    Run_WithDestArgTypes_ConstnessHelper(std::move(noGc), method, id, aArgs...);
  }

  // Because we're trying to explicitly pull `DestArgs` via `method`, we have
  // one overload for mut-methods and one for const-methods.
  template <typename... DestArgs>
  void Run_WithDestArgTypes_ConstnessHelper(
      std::optional<JS::AutoCheckCannotGC>&& noGc,
      void (HostWebGLContext::*method)(DestArgs...), const size_t id,
      const std::remove_reference_t<std::remove_const_t<DestArgs>>&... args)
      const {
    Run_WithDestArgTypes(std::move(noGc), method, id, args...);
  }
  template <typename... DestArgs>
  void Run_WithDestArgTypes_ConstnessHelper(
      std::optional<JS::AutoCheckCannotGC>&& noGc,
      void (HostWebGLContext::*method)(DestArgs...) const, const size_t id,
      const std::remove_reference_t<std::remove_const_t<DestArgs>>&... args)
      const {
    Run_WithDestArgTypes(std::move(noGc), method, id, args...);
  }

  template <typename MethodT, typename... DestArgs>
  void Run_WithDestArgTypes(std::optional<JS::AutoCheckCannotGC>&&, MethodT,
                            const size_t id, const DestArgs&...) const;

  // -------------------------------------------------------------------------
  // Helpers for DOM operations, composition, actors, etc
  // -------------------------------------------------------------------------

 public:
  // https://immersive-web.github.io/webxr/#xr-compatible
  bool IsXRCompatible() const;
  already_AddRefed<dom::Promise> MakeXRCompatible(ErrorResult& aRv);

 protected:
  uint32_t GetPrincipalHashValue() const;

  // Prepare the context for capture before compositing
  void BeginComposition();

  // Clean up the context after captured for compositing
  void EndComposition();

  mozilla::dom::Document* GetOwnerDoc() const;

  mutable bool mResetLayer = true;
  Maybe<const WebGLContextOptions> mInitialOptions;
  bool mXRCompatible = false;
};

// used by DOM bindings in conjunction with GetParentObject
inline nsISupports* ToSupports(ClientWebGLContext* webgl) {
  return static_cast<nsICanvasRenderingContextInternal*>(webgl);
}

const char* GetExtensionName(WebGLExtensionID);

// -

inline bool webgl::ObjectJS::IsForContext(
    const ClientWebGLContext& context) const {
  const auto& notLost = context.mNotLost;
  if (!notLost) return false;
  if (notLost.get() != mGeneration.lock().get()) return false;
  return true;
}

void AutoJsWarning(const std::string& utf8);

}  // namespace mozilla

#endif  // CLIENTWEBGLCONTEXT_H_
