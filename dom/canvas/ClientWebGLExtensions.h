/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef CLIENTWEBGLEXTENSIONS_H_
#define CLIENTWEBGLEXTENSIONS_H_

#include "ClientWebGLContext.h"
#include "WebGLExtensions.h"

namespace mozilla {

class ClientWebGLExtensionBase : public nsWrapperCache {
  friend ClientWebGLContext;

 protected:
  WeakPtr<ClientWebGLContext> mContext;

 public:
  NS_INLINE_DECL_CYCLE_COLLECTING_NATIVE_REFCOUNTING(ClientWebGLExtensionBase)
  NS_DECL_CYCLE_COLLECTION_NATIVE_WRAPPERCACHE_CLASS(ClientWebGLExtensionBase)

 protected:
  explicit ClientWebGLExtensionBase(ClientWebGLContext& context)
      : mContext(&context) {}
  virtual ~ClientWebGLExtensionBase() = default;

 public:
  ClientWebGLContext* GetParentObject() const { return mContext.get(); }
};

// -

// To be used for implementations of ClientWebGLExtensionBase
#define DEFINE_WEBGL_EXTENSION_GOOP(_WebGLBindingType, _Extension)             \
  JSObject* Client##_Extension::WrapObject(JSContext* cx,                      \
                                           JS::Handle<JSObject*> givenProto) { \
    return dom::_WebGLBindingType##_Binding::Wrap(cx, this, givenProto);       \
  }                                                                            \
  Client##_Extension::Client##_Extension(ClientWebGLContext& aClient)          \
      : ClientWebGLExtensionBase(aClient) {}

// Many extensions have no methods.  This is a shorthand for declaring client
// versions of such classes.
#define DECLARE_SIMPLE_WEBGL_EXTENSION(_Extension)                           \
  class Client##_Extension : public ClientWebGLExtensionBase {               \
   public:                                                                   \
    virtual JSObject* WrapObject(JSContext* cx,                              \
                                 JS::Handle<JSObject*> givenProto) override; \
    explicit Client##_Extension(ClientWebGLContext&);                        \
  };

////

class ClientWebGLExtensionCompressedTextureASTC
    : public ClientWebGLExtensionBase {
 public:
  virtual JSObject* WrapObject(JSContext* cx,
                               JS::Handle<JSObject*> givenProto) override;
  explicit ClientWebGLExtensionCompressedTextureASTC(ClientWebGLContext&);

  void GetSupportedProfiles(dom::Nullable<nsTArray<nsString>>& retval) const {
    if (!mContext) [[unlikely]] {
      AutoJsWarning("getSupportedProfiles: Extension is `invalidated`.");
      return;
    }
    mContext->GetSupportedProfilesASTC(retval);
  }
};

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionFloatBlend)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionCompressedTextureBPTC)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionCompressedTextureES3)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionCompressedTextureETC1)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionCompressedTexturePVRTC)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionCompressedTextureRGTC)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionFBORenderMipmap)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionCompressedTextureS3TC)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionCompressedTextureS3TC_SRGB)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionDebugRendererInfo)

class ClientWebGLExtensionDebugShaders : public ClientWebGLExtensionBase {
 public:
  virtual JSObject* WrapObject(JSContext* cx,
                               JS::Handle<JSObject*> givenProto) override;
  explicit ClientWebGLExtensionDebugShaders(ClientWebGLContext&);

  void GetTranslatedShaderSource(const WebGLShaderJS& shader,
                                 nsAString& retval) const {
    if (!mContext) [[unlikely]] {
      AutoJsWarning("getTranslatedShaderSource: Extension is `invalidated`.");
      return;
    }
    mContext->GetTranslatedShaderSource(shader, retval);
  }
};

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionDepthClamp)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionDepthTexture)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionElementIndexUint)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionEXTColorBufferFloat)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionFragDepth)

class ClientWebGLExtensionLoseContext : public ClientWebGLExtensionBase {
 public:
  virtual JSObject* WrapObject(JSContext* cx,
                               JS::Handle<JSObject*> givenProto) override;
  explicit ClientWebGLExtensionLoseContext(ClientWebGLContext&);

  void LoseContext() {
    if (!mContext) [[unlikely]] {
      AutoJsWarning("loseContext: Extension is `invalidated`.");
      return;
    }
    mContext->EmulateLoseContext();
  }
  void RestoreContext() {
    if (!mContext) [[unlikely]] {
      AutoJsWarning("restoreContext: Extension is `invalidated`.");
      return;
    }
    mContext->RestoreContext(webgl::LossStatus::LostManually);
  }
};

class ClientWebGLExtensionProvokingVertex : public ClientWebGLExtensionBase {
 public:
  virtual JSObject* WrapObject(JSContext* cx,
                               JS::Handle<JSObject*> givenProto) override;
  explicit ClientWebGLExtensionProvokingVertex(ClientWebGLContext&);

  void ProvokingVertexWEBGL(const GLenum mode) const {
    if (!mContext) [[unlikely]] {
      AutoJsWarning("provokingVertexWEBGL: Extension is `invalidated`.");
      return;
    }
    mContext->ProvokingVertex(mode);
  }
};

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionSRGB)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionStandardDerivatives)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionShaderTextureLod)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionTextureFilterAnisotropic)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionTextureFloat)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionTextureFloatLinear)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionTextureHalfFloat)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionTextureHalfFloatLinear)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionTextureNorm16)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionColorBufferFloat)

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionColorBufferHalfFloat)

class ClientWebGLExtensionDrawBuffers : public ClientWebGLExtensionBase {
 public:
  virtual JSObject* WrapObject(JSContext* cx,
                               JS::Handle<JSObject*> givenProto) override;
  explicit ClientWebGLExtensionDrawBuffers(ClientWebGLContext&);

  void DrawBuffersWEBGL(const dom::Sequence<GLenum>& buffers) {
    if (!mContext) [[unlikely]] {
      AutoJsWarning("drawBuffersWEBGL: Extension is `invalidated`.");
      return;
    }
    mContext->DrawBuffers(buffers);
  }
};

class ClientWebGLExtensionVertexArray : public ClientWebGLExtensionBase {
 public:
  virtual JSObject* WrapObject(JSContext* cx,
                               JS::Handle<JSObject*> givenProto) override;
  explicit ClientWebGLExtensionVertexArray(ClientWebGLContext&);

  already_AddRefed<WebGLVertexArrayJS> CreateVertexArrayOES() {
    RefPtr<WebGLVertexArrayJS> ret;
    if (!mContext) [[unlikely]] {
      AutoJsWarning("createVertexArrayOES: Extension is `invalidated`.");
      ret = new WebGLVertexArrayJS(nullptr);
    } else {
      ret = mContext->CreateVertexArray();
    }
    return ret.forget();
  }
  void DeleteVertexArrayOES(WebGLVertexArrayJS* array) {
    if (!mContext) [[unlikely]] {
      AutoJsWarning("deleteVertexArrayOES: Extension is `invalidated`.");
      return;
    }
    mContext->DeleteVertexArray(array);
  }
  bool IsVertexArrayOES(const WebGLVertexArrayJS* array) {
    if (!mContext) [[unlikely]] {
      AutoJsWarning("isVertexArrayOES: Extension is `invalidated`.");
      return false;
    }
    return mContext->IsVertexArray(array);
  }
  void BindVertexArrayOES(WebGLVertexArrayJS* array) {
    if (!mContext) [[unlikely]] {
      AutoJsWarning("bindVertexArrayOES: Extension is `invalidated`.");
      return;
    }
    mContext->BindVertexArray(array);
  }
};

class ClientWebGLExtensionInstancedArrays : public ClientWebGLExtensionBase {
 public:
  virtual JSObject* WrapObject(JSContext* cx,
                               JS::Handle<JSObject*> givenProto) override;
  explicit ClientWebGLExtensionInstancedArrays(ClientWebGLContext&);

  void DrawArraysInstancedANGLE(GLenum mode, GLint first, GLsizei count,
                                GLsizei primcount) {
    if (!mContext) [[unlikely]] {
      AutoJsWarning("drawArraysInstancedANGLE: Extension is `invalidated`.");
      return;
    }
    mContext->DrawArraysInstanced(mode, first, count, primcount);
  }
  void DrawElementsInstancedANGLE(GLenum mode, GLsizei count, GLenum type,
                                  WebGLintptr offset, GLsizei primcount) {
    if (!mContext) [[unlikely]] {
      AutoJsWarning("drawElementsInstancedANGLE: Extension is `invalidated`.");
      return;
    }
    mContext->DrawElementsInstanced(mode, count, type, offset, primcount);
  }
  void VertexAttribDivisorANGLE(GLuint index, GLuint divisor) {
    if (!mContext) [[unlikely]] {
      AutoJsWarning("vertexAttribDivisorANGLE: Extension is `invalidated`.");
      return;
    }
    mContext->VertexAttribDivisor(index, divisor);
  }
};

DECLARE_SIMPLE_WEBGL_EXTENSION(WebGLExtensionBlendMinMax)

class ClientWebGLExtensionDisjointTimerQuery : public ClientWebGLExtensionBase {
 public:
  virtual JSObject* WrapObject(JSContext* cx,
                               JS::Handle<JSObject*> givenProto) override;
  explicit ClientWebGLExtensionDisjointTimerQuery(ClientWebGLContext&);

  already_AddRefed<WebGLQueryJS> CreateQueryEXT() const {
    RefPtr<WebGLQueryJS> ret;
    if (!mContext) [[unlikely]] {
      AutoJsWarning("createQueryEXT: Extension is `invalidated`.");
      ret = new WebGLQueryJS(nullptr);
    } else {
      ret = mContext->CreateQuery();
    }
    return ret.forget();
  }
  void DeleteQueryEXT(WebGLQueryJS* query) const {
    if (!mContext) [[unlikely]] {
      AutoJsWarning("deleteQueryEXT: Extension is `invalidated`.");
      return;
    }
    mContext->DeleteQuery(query);
  }
  bool IsQueryEXT(const WebGLQueryJS* query) const {
    if (!mContext) [[unlikely]] {
      AutoJsWarning("isQueryEXT: Extension is `invalidated`.");
      return false;
    }
    return mContext->IsQuery(query);
  }
  void BeginQueryEXT(GLenum target, WebGLQueryJS& query) const {
    if (!mContext) [[unlikely]] {
      AutoJsWarning("beginQueryEXT: Extension is `invalidated`.");
      return;
    }
    mContext->BeginQuery(target, query);
  }
  void EndQueryEXT(GLenum target) const {
    if (!mContext) [[unlikely]] {
      AutoJsWarning("endQueryEXT: Extension is `invalidated`.");
      return;
    }
    mContext->EndQuery(target);
  }
  void QueryCounterEXT(WebGLQueryJS& query, GLenum target) const {
    if (!mContext) [[unlikely]] {
      AutoJsWarning("queryCounterEXT: Extension is `invalidated`.");
      return;
    }
    mContext->QueryCounter(query, target);
  }
  void GetQueryEXT(JSContext* cx, GLenum target, GLenum pname,
                   JS::MutableHandle<JS::Value> retval) const {
    if (!mContext) [[unlikely]] {
      AutoJsWarning("getQueryEXT: Extension is `invalidated`.");
      return;
    }
    mContext->GetQuery(cx, target, pname, retval);
  }
  void GetQueryObjectEXT(JSContext* cx, WebGLQueryJS& query, GLenum pname,
                         JS::MutableHandle<JS::Value> retval) const {
    if (!mContext) [[unlikely]] {
      AutoJsWarning("getQueryObjectEXT: Extension is `invalidated`.");
      return;
    }
    mContext->GetQueryParameter(cx, query, pname, retval);
  }
};

class ClientWebGLExtensionExplicitPresent : public ClientWebGLExtensionBase {
 public:
  virtual JSObject* WrapObject(JSContext* cx,
                               JS::Handle<JSObject*> givenProto) override;
  explicit ClientWebGLExtensionExplicitPresent(ClientWebGLContext&);

  void Present() const {
    if (!mContext) [[unlikely]] {
      AutoJsWarning("present: Extension is `invalidated`.");
      return;
    }
    mContext->OnBeforePaintTransaction();
  }
};

class ClientWebGLExtensionMOZDebug : public ClientWebGLExtensionBase {
 public:
  virtual JSObject* WrapObject(JSContext* cx,
                               JS::Handle<JSObject*> givenProto) override;
  explicit ClientWebGLExtensionMOZDebug(ClientWebGLContext&);

  void GetParameter(JSContext* cx, GLenum pname,
                    JS::MutableHandle<JS::Value> retval,
                    ErrorResult& er) const {
    if (!mContext) [[unlikely]] {
      AutoJsWarning("getParameter: Extension is `invalidated`.");
      return;
    }
    mContext->MOZDebugGetParameter(cx, pname, retval, er);
  }
};

class ClientWebGLExtensionMultiview : public ClientWebGLExtensionBase {
 public:
  virtual JSObject* WrapObject(JSContext* cx,
                               JS::Handle<JSObject*> givenProto) override;
  explicit ClientWebGLExtensionMultiview(ClientWebGLContext&);

  void FramebufferTextureMultiviewOVR(const GLenum target,
                                      const GLenum attachment,
                                      WebGLTextureJS* const texture,
                                      const GLint level,
                                      const GLint baseViewIndex,
                                      const GLsizei numViews) const {
    if (!mContext) [[unlikely]] {
      AutoJsWarning(
          "framebufferTextureMultiviewOVR: Extension is `invalidated`.");
      return;
    }
    mContext->FramebufferTextureMultiview(target, attachment, texture, level,
                                          baseViewIndex, numViews);
  }
};

class ClientWebGLExtensionDrawBuffersIndexed : public ClientWebGLExtensionBase {
 public:
  virtual JSObject* WrapObject(JSContext* cx,
                               JS::Handle<JSObject*> givenProto) override;
  explicit ClientWebGLExtensionDrawBuffersIndexed(ClientWebGLContext&);

  void EnableiOES(const GLenum target, const GLuint buf) const {
    if (!mContext) [[unlikely]] {
      AutoJsWarning("enableiOES: Extension is `invalidated`.");
      return;
    }
    mContext->SetEnabledI(target, Some(buf), true);
  }

  void DisableiOES(const GLenum target, const GLuint buf) const {
    if (!mContext) [[unlikely]] {
      AutoJsWarning("disableiOES: Extension is `invalidated`.");
      return;
    }
    mContext->SetEnabledI(target, Some(buf), false);
  }

  void BlendEquationiOES(const GLuint buf, const GLenum mode) const {
    BlendEquationSeparateiOES(buf, mode, mode);
  }

  void BlendEquationSeparateiOES(const GLuint buf, const GLenum modeRgb,
                                 const GLenum modeAlpha) const {
    if (!mContext) [[unlikely]] {
      AutoJsWarning("blendEquationSeparateiOES: Extension is `invalidated`.");
      return;
    }
    mContext->BlendEquationSeparateI(Some(buf), modeRgb, modeAlpha);
  }

  void BlendFunciOES(const GLuint buf, const GLenum src,
                     const GLenum dst) const {
    BlendFuncSeparateiOES(buf, src, dst, src, dst);
  }

  void BlendFuncSeparateiOES(const GLuint buf, const GLenum srcRgb,
                             const GLenum dstRgb, const GLenum srcAlpha,
                             const GLenum dstAlpha) const {
    if (!mContext) [[unlikely]] {
      AutoJsWarning("blendFuncSeparateiOES: Extension is `invalidated`.");
      return;
    }
    mContext->BlendFuncSeparateI(Some(buf), srcRgb, dstRgb, srcAlpha, dstAlpha);
  }

  void ColorMaskiOES(const GLuint buf, const bool r, const bool g, const bool b,
                     const bool a) const {
    if (!mContext) [[unlikely]] {
      AutoJsWarning("colorMaskiOES: Extension is `invalidated`.");
      return;
    }
    mContext->ColorMaskI(Some(buf), r, g, b, a);
  }
};

}  // namespace mozilla

#endif  // CLIENTWEBGLEXTENSIONS_H_
