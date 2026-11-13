/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLContext.h"
#include "WebGL2Context.h"

#include "WebGLContextUtils.h"
#include "WebGLBuffer.h"
#include "WebGLShader.h"
#include "WebGLProgram.h"
#include "WebGLFormats.h"
#include "WebGLFramebuffer.h"
#include "WebGLQuery.h"
#include "WebGLRenderbuffer.h"
#include "WebGLTexture.h"
#include "WebGLVertexArray.h"

#include "nsDebug.h"
#include "nsReadableUtils.h"
#include "nsString.h"

#include "gfxContext.h"
#include "gfxPlatform.h"
#include "GLContext.h"

#include "nsContentUtils.h"
#include "nsError.h"
#include "nsLayoutUtils.h"

#include "CanvasUtils.h"
#include "gfxUtils.h"
#include "MozFramebuffer.h"

#include "jsfriendapi.h"

#include "WebGLTexelConversions.h"
#include "WebGLValidateStrings.h"
#include <algorithm>

// needed to check if current OS is lower than 10.7
#if defined(MOZ_WIDGET_COCOA)
#  include "nsCocoaFeatures.h"
#endif

#include "mozilla/DebugOnly.h"
#include "mozilla/dom/BindingUtils.h"
#include "mozilla/dom/ImageData.h"
#include "mozilla/dom/WebGLRenderingContextBinding.h"
#include "mozilla/EndianUtils.h"
#include "mozilla/RefPtr.h"
#include "mozilla/UniquePtrExtensions.h"
#include "mozilla/StaticPrefs_webgl.h"

namespace mozilla {

using namespace mozilla::dom;
using namespace mozilla::gfx;
using namespace mozilla::gl;

//
//  WebGL API
//

void WebGLContext::ActiveTexture(uint32_t texUnit) {
  FuncScope funcScope(*this, "activeTexture");
  if (IsContextLost()) return;
  funcScope.mBindFailureGuard = true;

  if (texUnit >= Limits().maxTexUnits) {
    return ErrorInvalidEnum("Texture unit %u out of range (%u).", texUnit,
                            Limits().maxTexUnits);
  }

  mActiveTexture = texUnit;
  gl->fActiveTexture(LOCAL_GL_TEXTURE0 + texUnit);

  funcScope.mBindFailureGuard = false;
}

void WebGLContext::AttachShader(WebGLProgram& prog, WebGLShader& shader) {
  FuncScope funcScope(*this, "attachShader");
  if (IsContextLost()) return;
  funcScope.mBindFailureGuard = true;

  prog.AttachShader(shader);

  funcScope.mBindFailureGuard = false;
}

void WebGLContext::BindAttribLocation(WebGLProgram& prog, GLuint location,
                                      const std::string& name) const {
  const FuncScope funcScope(*this, "bindAttribLocation");
  if (IsContextLost()) return;

  prog.BindAttribLocation(location, name);
}

void WebGLContext::BindFramebuffer(GLenum target, WebGLFramebuffer* wfb) {
  FuncScope funcScope(*this, "bindFramebuffer");
  if (IsContextLost()) return;
  funcScope.mBindFailureGuard = true;

  if (!ValidateFramebufferTarget(target)) return;

  if (!wfb) {
    gl->fBindFramebuffer(target, 0);
  } else {
    GLuint framebuffername = wfb->mGLName;
    gl->fBindFramebuffer(target, framebuffername);
    wfb->mHasBeenBound = true;
  }

  switch (target) {
    case LOCAL_GL_FRAMEBUFFER:
      mBoundDrawFramebuffer = wfb;
      mBoundReadFramebuffer = wfb;
      break;
    case LOCAL_GL_DRAW_FRAMEBUFFER:
      mBoundDrawFramebuffer = wfb;
      break;
    case LOCAL_GL_READ_FRAMEBUFFER:
      mBoundReadFramebuffer = wfb;
      break;
    default:
      return;
  }
  funcScope.mBindFailureGuard = false;
}

void WebGLContext::BlendEquationSeparate(Maybe<GLuint> i, GLenum modeRGB,
                                         GLenum modeAlpha) {
  const FuncScope funcScope(*this, "blendEquationSeparate");
  if (IsContextLost()) return;

  if (!ValidateBlendEquationEnum(modeRGB, "modeRGB") ||
      !ValidateBlendEquationEnum(modeAlpha, "modeAlpha")) {
    return;
  }

  if (i) {
    MOZ_RELEASE_ASSERT(
        IsExtensionEnabled(WebGLExtensionID::OES_draw_buffers_indexed));
    const auto limit = MaxValidDrawBuffers();
    if (*i >= limit) {
      ErrorInvalidValue("`index` (%u) must be < %s (%u)", *i,
                        "MAX_DRAW_BUFFERS", limit);
      return;
    }

    gl->fBlendEquationSeparatei(*i, modeRGB, modeAlpha);
  } else {
    gl->fBlendEquationSeparate(modeRGB, modeAlpha);
  }
}

static bool ValidateBlendFuncEnum(WebGLContext* webgl, GLenum factor,
                                  const char* varName) {
  switch (factor) {
    case LOCAL_GL_ZERO:
    case LOCAL_GL_ONE:
    case LOCAL_GL_SRC_COLOR:
    case LOCAL_GL_ONE_MINUS_SRC_COLOR:
    case LOCAL_GL_DST_COLOR:
    case LOCAL_GL_ONE_MINUS_DST_COLOR:
    case LOCAL_GL_SRC_ALPHA:
    case LOCAL_GL_ONE_MINUS_SRC_ALPHA:
    case LOCAL_GL_DST_ALPHA:
    case LOCAL_GL_ONE_MINUS_DST_ALPHA:
    case LOCAL_GL_CONSTANT_COLOR:
    case LOCAL_GL_ONE_MINUS_CONSTANT_COLOR:
    case LOCAL_GL_CONSTANT_ALPHA:
    case LOCAL_GL_ONE_MINUS_CONSTANT_ALPHA:
    case LOCAL_GL_SRC_ALPHA_SATURATE:
      return true;

    default:
      webgl->ErrorInvalidEnumInfo(varName, factor);
      return false;
  }
}

static bool ValidateBlendFuncEnums(WebGLContext* webgl, GLenum srcRGB,
                                   GLenum srcAlpha, GLenum dstRGB,
                                   GLenum dstAlpha) {
  if (!webgl->IsWebGL2()) {
    if (dstRGB == LOCAL_GL_SRC_ALPHA_SATURATE ||
        dstAlpha == LOCAL_GL_SRC_ALPHA_SATURATE) {
      webgl->ErrorInvalidEnum(
          "LOCAL_GL_SRC_ALPHA_SATURATE as a destination"
          " blend function is disallowed in WebGL 1 (dstRGB ="
          " 0x%04x, dstAlpha = 0x%04x).",
          dstRGB, dstAlpha);
      return false;
    }
  }

  if (!ValidateBlendFuncEnum(webgl, srcRGB, "srcRGB") ||
      !ValidateBlendFuncEnum(webgl, srcAlpha, "srcAlpha") ||
      !ValidateBlendFuncEnum(webgl, dstRGB, "dstRGB") ||
      !ValidateBlendFuncEnum(webgl, dstAlpha, "dstAlpha")) {
    return false;
  }

  return true;
}

void WebGLContext::BlendFuncSeparate(Maybe<GLuint> i, GLenum srcRGB,
                                     GLenum dstRGB, GLenum srcAlpha,
                                     GLenum dstAlpha) {
  const FuncScope funcScope(*this, "blendFuncSeparate");
  if (IsContextLost()) return;

  if (!ValidateBlendFuncEnums(this, srcRGB, srcAlpha, dstRGB, dstAlpha)) return;

  // note that we only check compatibity for the RGB enums, no need to for the
  // Alpha enums, see "Section 6.8 forgetting to mention alpha factors?" thread
  // on the public_webgl mailing list
  if (!ValidateBlendFuncEnumsCompatibility(srcRGB, dstRGB, "srcRGB and dstRGB"))
    return;

  if (i) {
    MOZ_RELEASE_ASSERT(
        IsExtensionEnabled(WebGLExtensionID::OES_draw_buffers_indexed));
    const auto limit = MaxValidDrawBuffers();
    if (*i >= limit) {
      ErrorInvalidValue("`index` (%u) must be < %s (%u)", *i,
                        "MAX_DRAW_BUFFERS", limit);
      return;
    }

    gl->fBlendFuncSeparatei(*i, srcRGB, dstRGB, srcAlpha, dstAlpha);
  } else {
    gl->fBlendFuncSeparate(srcRGB, dstRGB, srcAlpha, dstAlpha);
  }
}

GLenum WebGLContext::CheckFramebufferStatus(GLenum target) {
  const FuncScope funcScope(*this, "checkFramebufferStatus");
  if (IsContextLost()) return LOCAL_GL_FRAMEBUFFER_UNSUPPORTED;

  if (!ValidateFramebufferTarget(target)) return 0;

  WebGLFramebuffer* fb;
  switch (target) {
    case LOCAL_GL_FRAMEBUFFER:
    case LOCAL_GL_DRAW_FRAMEBUFFER:
      fb = mBoundDrawFramebuffer;
      break;

    case LOCAL_GL_READ_FRAMEBUFFER:
      fb = mBoundReadFramebuffer;
      break;

    default:
      MOZ_CRASH("GFX: Bad target.");
  }

  if (!fb) return LOCAL_GL_FRAMEBUFFER_COMPLETE;

  return fb->CheckFramebufferStatus().get();
}

RefPtr<WebGLProgram> WebGLContext::CreateProgram() {
  const FuncScope funcScope(*this, "createProgram");
  if (IsContextLost()) return nullptr;

  return new WebGLProgram(this);
}

RefPtr<WebGLShader> WebGLContext::CreateShader(GLenum type) {
  const FuncScope funcScope(*this, "createShader");
  if (IsContextLost()) return nullptr;

  if (type != LOCAL_GL_VERTEX_SHADER && type != LOCAL_GL_FRAGMENT_SHADER) {
    ErrorInvalidEnumInfo("type", type);
    return nullptr;
  }

  return new WebGLShader(this, type);
}

void WebGLContext::CullFace(GLenum face) {
  const FuncScope funcScope(*this, "cullFace");
  if (IsContextLost()) return;

  if (!ValidateFaceEnum(face)) return;

  gl->fCullFace(face);
}

void WebGLContext::DetachShader(WebGLProgram& prog, const WebGLShader& shader) {
  FuncScope funcScope(*this, "detachShader");
  if (IsContextLost()) return;
  funcScope.mBindFailureGuard = true;

  prog.DetachShader(shader);

  funcScope.mBindFailureGuard = false;
}

static bool ValidateComparisonEnum(WebGLContext& webgl, const GLenum func) {
  switch (func) {
    case LOCAL_GL_NEVER:
    case LOCAL_GL_LESS:
    case LOCAL_GL_LEQUAL:
    case LOCAL_GL_GREATER:
    case LOCAL_GL_GEQUAL:
    case LOCAL_GL_EQUAL:
    case LOCAL_GL_NOTEQUAL:
    case LOCAL_GL_ALWAYS:
      return true;

    default:
      webgl.ErrorInvalidEnumInfo("func", func);
      return false;
  }
}

void WebGLContext::DepthFunc(GLenum func) {
  const FuncScope funcScope(*this, "depthFunc");
  if (IsContextLost()) return;

  if (!ValidateComparisonEnum(*this, func)) return;

  gl->fDepthFunc(func);
}

void WebGLContext::DepthRange(GLfloat zNear, GLfloat zFar) {
  const FuncScope funcScope(*this, "depthRange");
  if (IsContextLost()) return;

  if (zNear > zFar)
    return ErrorInvalidOperation(
        "the near value is greater than the far value!");

  gl->fDepthRange(zNear, zFar);
}

// -

void WebGLContext::FramebufferAttach(const GLenum target,
                                     const GLenum attachSlot,
                                     const GLenum bindImageTarget,
                                     const webgl::FbAttachInfo& toAttach) {
  FuncScope funcScope(*this, "framebufferAttach");
  funcScope.mBindFailureGuard = true;
  const auto& limits = *mLimits;

  if (!ValidateFramebufferTarget(target)) return;

  auto fb = mBoundDrawFramebuffer;
  if (target == LOCAL_GL_READ_FRAMEBUFFER) {
    fb = mBoundReadFramebuffer;
  }
  if (!fb) return;

  // `rb` needs no validation.

  // `tex`
  const auto& tex = toAttach.tex;
  if (tex) {
    const auto err = CheckFramebufferAttach(bindImageTarget, tex->mTarget.get(),
                                            toAttach.mipLevel, toAttach.zLayer,
                                            toAttach.zLayerCount, limits);
    if (err) return;
  }

  auto safeToAttach = toAttach;
  if (!toAttach.rb && !toAttach.tex) {
    safeToAttach = {};
  }
  if (!IsWebGL2() &&
      !IsExtensionEnabled(WebGLExtensionID::OES_fbo_render_mipmap)) {
    safeToAttach.mipLevel = 0;
  }
  if (!IsExtensionEnabled(WebGLExtensionID::OVR_multiview2)) {
    safeToAttach.isMultiview = false;
  }

  if (!fb->FramebufferAttach(attachSlot, safeToAttach)) return;

  funcScope.mBindFailureGuard = false;
}

// -

void WebGLContext::FrontFace(GLenum mode) {
  const FuncScope funcScope(*this, "frontFace");
  if (IsContextLost()) return;

  switch (mode) {
    case LOCAL_GL_CW:
    case LOCAL_GL_CCW:
      break;
    default:
      return ErrorInvalidEnumInfo("mode", mode);
  }

  gl->fFrontFace(mode);
}

Maybe<double> WebGLContext::GetBufferParameter(GLenum target, GLenum pname) {
  const FuncScope funcScope(*this, "getBufferParameter");
  if (IsContextLost()) return Nothing();

  const auto& slot = ValidateBufferSlot(target);
  if (!slot) return Nothing();
  const auto& buffer = *slot;

  if (!buffer) {
    ErrorInvalidOperation("Buffer for `target` is null.");
    return Nothing();
  }

  switch (pname) {
    case LOCAL_GL_BUFFER_SIZE:
      return Some(buffer->ByteLength());

    case LOCAL_GL_BUFFER_USAGE:
      return Some(buffer->Usage());

    default:
      ErrorInvalidEnumInfo("pname", pname);
      return Nothing();
  }
}

Maybe<double> WebGLContext::GetFramebufferAttachmentParameter(
    WebGLFramebuffer* const fb, GLenum attachment, GLenum pname) const {
  const FuncScope funcScope(*this, "getFramebufferAttachmentParameter");
  if (IsContextLost()) return Nothing();

  if (fb) return fb->GetAttachmentParameter(attachment, pname);

  ////////////////////////////////////

  if (!IsWebGL2()) {
    ErrorInvalidOperation(
        "Querying against the default framebuffer is not"
        " allowed in WebGL 1.");
    return Nothing();
  }

  switch (attachment) {
    case LOCAL_GL_BACK:
    case LOCAL_GL_DEPTH:
    case LOCAL_GL_STENCIL:
      break;

    default:
      ErrorInvalidEnum(
          "For the default framebuffer, can only query COLOR, DEPTH,"
          " or STENCIL.");
      return Nothing();
  }

  switch (pname) {
    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE:
      switch (attachment) {
        case LOCAL_GL_BACK:
          break;
        case LOCAL_GL_DEPTH:
          if (!mOptions.depth) {
            return Some(LOCAL_GL_NONE);
          }
          break;
        case LOCAL_GL_STENCIL:
          if (!mOptions.stencil) {
            return Some(LOCAL_GL_NONE);
          }
          break;
        default:
          ErrorInvalidEnum(
              "With the default framebuffer, can only query COLOR, DEPTH,"
              " or STENCIL for GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE");
          return Nothing();
      }
      return Some(LOCAL_GL_FRAMEBUFFER_DEFAULT);

      ////////////////

    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE:
    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE:
    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE:
      if (attachment == LOCAL_GL_BACK) return Some(8);
      return Some(0);

    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE:
      if (attachment == LOCAL_GL_BACK) {
        if (mOptions.alpha) {
          return Some(8);
        }
        ErrorInvalidOperation(
            "The default framebuffer doesn't contain an alpha buffer");
        return Nothing();
      }
      return Some(0);

    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE:
      if (attachment == LOCAL_GL_DEPTH) {
        if (mOptions.depth) {
          return Some(24);
        }
        ErrorInvalidOperation(
            "The default framebuffer doesn't contain an depth buffer");
        return Nothing();
      }
      return Some(0);

    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE:
      if (attachment == LOCAL_GL_STENCIL) {
        if (mOptions.stencil) {
          return Some(8);
        }
        ErrorInvalidOperation(
            "The default framebuffer doesn't contain an stencil buffer");
        return Nothing();
      }
      return Some(0);

    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE:
      if (attachment == LOCAL_GL_STENCIL) {
        if (mOptions.stencil) {
          return Some(LOCAL_GL_UNSIGNED_INT);
        }
        ErrorInvalidOperation(
            "The default framebuffer doesn't contain an stencil buffer");
      } else if (attachment == LOCAL_GL_DEPTH) {
        if (mOptions.depth) {
          return Some(LOCAL_GL_UNSIGNED_NORMALIZED);
        }
        ErrorInvalidOperation(
            "The default framebuffer doesn't contain an depth buffer");
      } else {  // LOCAL_GL_BACK
        return Some(LOCAL_GL_UNSIGNED_NORMALIZED);
      }
      return Nothing();

    case LOCAL_GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING:
      if (attachment == LOCAL_GL_STENCIL) {
        if (!mOptions.stencil) {
          ErrorInvalidOperation(
              "The default framebuffer doesn't contain an stencil buffer");
          return Nothing();
        }
      } else if (attachment == LOCAL_GL_DEPTH) {
        if (!mOptions.depth) {
          ErrorInvalidOperation(
              "The default framebuffer doesn't contain an depth buffer");
          return Nothing();
        }
      }
      return Some(LOCAL_GL_LINEAR);
  }

  ErrorInvalidEnumInfo("pname", pname);
  return Nothing();
}

Maybe<double> WebGLContext::GetRenderbufferParameter(
    const WebGLRenderbuffer& rb, GLenum pname) const {
  const FuncScope funcScope(*this, "getRenderbufferParameter");
  if (IsContextLost()) return Nothing();

  switch (pname) {
    case LOCAL_GL_RENDERBUFFER_SAMPLES:
      if (!IsWebGL2()) break;
      [[fallthrough]];

    case LOCAL_GL_RENDERBUFFER_WIDTH:
    case LOCAL_GL_RENDERBUFFER_HEIGHT:
    case LOCAL_GL_RENDERBUFFER_RED_SIZE:
    case LOCAL_GL_RENDERBUFFER_GREEN_SIZE:
    case LOCAL_GL_RENDERBUFFER_BLUE_SIZE:
    case LOCAL_GL_RENDERBUFFER_ALPHA_SIZE:
    case LOCAL_GL_RENDERBUFFER_DEPTH_SIZE:
    case LOCAL_GL_RENDERBUFFER_STENCIL_SIZE:
    case LOCAL_GL_RENDERBUFFER_INTERNAL_FORMAT: {
      // RB emulation means we have to ask the RB itself.
      GLint i = rb.GetRenderbufferParameter(pname);
      return Some(i);
    }

    default:
      break;
  }

  ErrorInvalidEnumInfo("pname", pname);
  return Nothing();
}

RefPtr<WebGLTexture> WebGLContext::CreateTexture() {
  const FuncScope funcScope(*this, "createTexture");
  if (IsContextLost()) return nullptr;

  GLuint tex = 0;
  gl->fGenTextures(1, &tex);

  return new WebGLTexture(this, tex);
}

GLenum WebGLContext::GetError() {
  const FuncScope funcScope(*this, "getError");

  /* WebGL 1.0: Section 5.14.3: Setting and getting state:
   *   If the context's webgl context lost flag is set, returns
   *   CONTEXT_LOST_WEBGL the first time this method is called.
   *   Afterward, returns NO_ERROR until the context has been
   *   restored.
   *
   * WEBGL_lose_context:
   *   [When this extension is enabled: ] loseContext and
   *   restoreContext are allowed to generate INVALID_OPERATION errors
   *   even when the context is lost.
   */

  auto err = mWebGLError;
  mWebGLError = 0;
  if (IsContextLost() || err)  // Must check IsContextLost in all flow paths.
    return err;

  // Either no WebGL-side error, or it's already been cleared.
  // UnderlyingGL-side errors, now.
  err = gl->fGetError();
  if (gl->IsContextLost()) {
    CheckForContextLoss();
    return GetError();
  }
  MOZ_ASSERT(err != LOCAL_GL_CONTEXT_LOST);

  if (err) {
    GenerateWarning("Driver error unexpected by WebGL: 0x%04x", err);
    // This might be:
    // - INVALID_OPERATION from ANGLE due to incomplete RBAB implementation for
    // DrawElements
    //   with DYNAMIC_DRAW index buffer.
  }
  return err;
}

webgl::GetUniformData WebGLContext::GetUniform(const WebGLProgram& prog,
                                               const uint32_t loc) const {
  const FuncScope funcScope(*this, "getUniform");
  webgl::GetUniformData ret;
  [&]() {
    if (IsContextLost()) return;

    const auto& info = prog.LinkInfo();
    if (!info) return;

    const auto locInfo = MaybeFind(info->locationMap, loc);
    if (!locInfo) return;

    ret.type = locInfo->info.info.elemType;
    switch (ret.type) {
      case LOCAL_GL_FLOAT:
      case LOCAL_GL_FLOAT_VEC2:
      case LOCAL_GL_FLOAT_VEC3:
      case LOCAL_GL_FLOAT_VEC4:
      case LOCAL_GL_FLOAT_MAT2:
      case LOCAL_GL_FLOAT_MAT3:
      case LOCAL_GL_FLOAT_MAT4:
      case LOCAL_GL_FLOAT_MAT2x3:
      case LOCAL_GL_FLOAT_MAT2x4:
      case LOCAL_GL_FLOAT_MAT3x2:
      case LOCAL_GL_FLOAT_MAT3x4:
      case LOCAL_GL_FLOAT_MAT4x2:
      case LOCAL_GL_FLOAT_MAT4x3:
        gl->fGetUniformfv(prog.mGLName, loc,
                          reinterpret_cast<float*>(ret.data));
        break;

      case LOCAL_GL_INT:
      case LOCAL_GL_INT_VEC2:
      case LOCAL_GL_INT_VEC3:
      case LOCAL_GL_INT_VEC4:
      case LOCAL_GL_SAMPLER_2D:
      case LOCAL_GL_SAMPLER_3D:
      case LOCAL_GL_SAMPLER_CUBE:
      case LOCAL_GL_SAMPLER_2D_SHADOW:
      case LOCAL_GL_SAMPLER_2D_ARRAY:
      case LOCAL_GL_SAMPLER_2D_ARRAY_SHADOW:
      case LOCAL_GL_SAMPLER_CUBE_SHADOW:
      case LOCAL_GL_INT_SAMPLER_2D:
      case LOCAL_GL_INT_SAMPLER_3D:
      case LOCAL_GL_INT_SAMPLER_CUBE:
      case LOCAL_GL_INT_SAMPLER_2D_ARRAY:
      case LOCAL_GL_UNSIGNED_INT_SAMPLER_2D:
      case LOCAL_GL_UNSIGNED_INT_SAMPLER_3D:
      case LOCAL_GL_UNSIGNED_INT_SAMPLER_CUBE:
      case LOCAL_GL_UNSIGNED_INT_SAMPLER_2D_ARRAY:
      case LOCAL_GL_BOOL:
      case LOCAL_GL_BOOL_VEC2:
      case LOCAL_GL_BOOL_VEC3:
      case LOCAL_GL_BOOL_VEC4:
        gl->fGetUniformiv(prog.mGLName, loc,
                          reinterpret_cast<int32_t*>(ret.data));
        break;

      case LOCAL_GL_UNSIGNED_INT:
      case LOCAL_GL_UNSIGNED_INT_VEC2:
      case LOCAL_GL_UNSIGNED_INT_VEC3:
      case LOCAL_GL_UNSIGNED_INT_VEC4:
        gl->fGetUniformuiv(prog.mGLName, loc,
                           reinterpret_cast<uint32_t*>(ret.data));
        break;

      default:
        MOZ_CRASH("GFX: Invalid elemType.");
    }
  }();
  return ret;
}

void WebGLContext::Hint(GLenum target, GLenum mode) {
  const FuncScope funcScope(*this, "hint");
  if (IsContextLost()) return;

  switch (mode) {
    case LOCAL_GL_FASTEST:
    case LOCAL_GL_NICEST:
    case LOCAL_GL_DONT_CARE:
      break;
    default:
      return ErrorInvalidEnumArg("mode", mode);
  }

  // -

  bool isValid = false;

  switch (target) {
    case LOCAL_GL_GENERATE_MIPMAP_HINT:
      mGenerateMipmapHint = mode;
      isValid = true;

      // Deprecated and removed in desktop GL Core profiles.
      if (gl->IsCoreProfile()) return;

      break;

    case LOCAL_GL_FRAGMENT_SHADER_DERIVATIVE_HINT:
      if (IsWebGL2() ||
          IsExtensionEnabled(WebGLExtensionID::OES_standard_derivatives)) {
        isValid = true;
      }
      break;
  }
  if (!isValid) return ErrorInvalidEnumInfo("target", target);

  // -

  gl->fHint(target, mode);
}

// -

void WebGLContext::LinkProgram(WebGLProgram& prog) {
  const FuncScope funcScope(*this, "linkProgram");
  if (IsContextLost()) return;

  prog.LinkProgram();

  if (&prog == mCurrentProgram) {
    if (!prog.IsLinked()) {
      // We use to simply early-out here, and preserve the GL behavior that
      // failed relink doesn't invalidate the current active program link info.
      // The new behavior was changed for WebGL here:
      // https://github.com/KhronosGroup/WebGL/pull/3371
      mActiveProgramLinkInfo = nullptr;
      gl->fUseProgram(0);  // Shouldn't be needed, but let's be safe.
      return;
    }
    mActiveProgramLinkInfo = prog.LinkInfo();
    gl->fUseProgram(prog.mGLName);  // Uncontionally re-use.
    // Previously, we needed this re-use on nvidia as a driver workaround,
    // but we might as well do it unconditionally.
  }
}

Maybe<webgl::ErrorInfo> SetPixelUnpack(
    const bool isWebgl2, webgl::PixelUnpackStateWebgl* const unpacking,
    const GLenum pname, const GLint param) {
  if (isWebgl2) {
    uint32_t* pValueSlot = nullptr;
    switch (pname) {
      case LOCAL_GL_UNPACK_IMAGE_HEIGHT:
        pValueSlot = &unpacking->imageHeight;
        break;

      case LOCAL_GL_UNPACK_SKIP_IMAGES:
        pValueSlot = &unpacking->skipImages;
        break;

      case LOCAL_GL_UNPACK_ROW_LENGTH:
        pValueSlot = &unpacking->rowLength;
        break;

      case LOCAL_GL_UNPACK_SKIP_ROWS:
        pValueSlot = &unpacking->skipRows;
        break;

      case LOCAL_GL_UNPACK_SKIP_PIXELS:
        pValueSlot = &unpacking->skipPixels;
        break;
    }

    if (pValueSlot) {
      *pValueSlot = static_cast<uint32_t>(param);
      return {};
    }
  }

  switch (pname) {
    case dom::WebGLRenderingContext_Binding::UNPACK_FLIP_Y_WEBGL:
      unpacking->flipY = bool(param);
      return {};

    case dom::WebGLRenderingContext_Binding::UNPACK_PREMULTIPLY_ALPHA_WEBGL:
      unpacking->premultiplyAlpha = bool(param);
      return {};

    case dom::WebGLRenderingContext_Binding::UNPACK_COLORSPACE_CONVERSION_WEBGL:
      switch (param) {
        case LOCAL_GL_NONE:
        case dom::WebGLRenderingContext_Binding::BROWSER_DEFAULT_WEBGL:
          break;

        default: {
          const nsPrintfCString text("Bad UNPACK_COLORSPACE_CONVERSION: %s",
                                     EnumString(param).c_str());
          return Some(webgl::ErrorInfo{LOCAL_GL_INVALID_VALUE, ToString(text)});
        }
      }
      unpacking->colorspaceConversion = param;
      return {};

    case dom::MOZ_debug_Binding::UNPACK_REQUIRE_FASTPATH:
      unpacking->requireFastPath = bool(param);
      return {};

    case LOCAL_GL_UNPACK_ALIGNMENT:
      switch (param) {
        case 1:
        case 2:
        case 4:
        case 8:
          break;

        default: {
          const nsPrintfCString text(
              "UNPACK_ALIGNMENT must be [1,2,4,8], was %i", param);
          return Some(webgl::ErrorInfo{LOCAL_GL_INVALID_VALUE, ToString(text)});
        }
      }
      unpacking->alignmentInTypeElems = param;
      return {};

    default:
      break;
  }
  const nsPrintfCString text("Bad `pname`: %s", EnumString(pname).c_str());
  return Some(webgl::ErrorInfo{LOCAL_GL_INVALID_ENUM, ToString(text)});
}

bool WebGLContext::DoReadPixelsAndConvert(
    const webgl::FormatInfo* const srcFormat, const webgl::ReadPixelsDesc& desc,
    const uintptr_t dest, const uint64_t destSize, const uint32_t rowStride) {
  const auto& x = desc.srcOffset.x;
  const auto& y = desc.srcOffset.y;
  const auto size = *ivec2::From(desc.size);
  const auto& pi = desc.pi;

  // On at least Win+NV, we'll get PBO errors if we don't have at least
  // `rowStride * height` bytes available to read into.
  const auto naiveBytesNeeded = CheckedInt<uint64_t>(rowStride) * size.y;
  const bool isDangerCloseToEdge =
      (!naiveBytesNeeded.isValid() || naiveBytesNeeded.value() > destSize);
  const bool useParanoidHandling =
      (gl->WorkAroundDriverBugs() && isDangerCloseToEdge &&
       mBoundPixelPackBuffer);
  if (!useParanoidHandling) {
    gl->fReadPixels(x, y, size.x, size.y, pi.format, pi.type,
                    reinterpret_cast<void*>(dest));
    return true;
  }

  // Read everything but the last row.
  const auto bodyHeight = size.y - 1;
  if (bodyHeight) {
    gl->fReadPixels(x, y, size.x, bodyHeight, pi.format, pi.type,
                    reinterpret_cast<void*>(dest));
  }

  // Now read the last row.
  gl->fPixelStorei(LOCAL_GL_PACK_ALIGNMENT, 1);
  gl->fPixelStorei(LOCAL_GL_PACK_ROW_LENGTH, 0);
  gl->fPixelStorei(LOCAL_GL_PACK_SKIP_ROWS, 0);

  const auto tailRowOffset =
      reinterpret_cast<uint8_t*>(dest) + rowStride * bodyHeight;
  gl->fReadPixels(x, y + bodyHeight, size.x, 1, pi.format, pi.type,
                  tailRowOffset);

  return true;
}

webgl::ReadPixelsResult WebGLContext::ReadPixelsInto(
    const webgl::ReadPixelsDesc& desc, const Range<uint8_t>& dest) {
  const FuncScope funcScope(*this, "readPixels");
  if (IsContextLost()) return {};

  if (mBoundPixelPackBuffer) {
    ErrorInvalidOperation("PIXEL_PACK_BUFFER must be null.");
    return {};
  }

  return ReadPixelsImpl(desc, reinterpret_cast<uintptr_t>(dest.begin().get()),
                        dest.length());
}

void WebGLContext::ReadPixelsPbo(const webgl::ReadPixelsDesc& desc,
                                 const uint64_t offset) {
  const FuncScope funcScope(*this, "readPixels");
  if (IsContextLost()) return;

  const auto& buffer = ValidateBufferSelection(LOCAL_GL_PIXEL_PACK_BUFFER);
  if (!buffer) return;

  //////

  {
    const auto pii = webgl::PackingInfoInfo::For(desc.pi);
    if (!pii) {
      GLenum err = LOCAL_GL_INVALID_OPERATION;
      if (!desc.pi.format || !desc.pi.type) {
        err = LOCAL_GL_INVALID_ENUM;
      }
      GenerateError(err, "`format` (%s) and/or `type` (%s) not acceptable.",
                    EnumString(desc.pi.format).c_str(),
                    EnumString(desc.pi.type).c_str());
      return;
    }

    if (offset % pii->bytesPerElement != 0) {
      ErrorInvalidOperation(
          "`offset` must be divisible by the size of `type`"
          " in bytes.");
      return;
    }
  }

  //////

  auto bytesAvailable = buffer->ByteLength();
  if (offset > bytesAvailable) {
    ErrorInvalidOperation("`offset` too large for bound PIXEL_PACK_BUFFER.");
    return;
  }
  bytesAvailable -= offset;

  // -

  const ScopedLazyBind lazyBind(gl, LOCAL_GL_PIXEL_PACK_BUFFER, buffer);

  ReadPixelsImpl(desc, offset, bytesAvailable);

  buffer->ResetLastUpdateFenceId();
}

static webgl::PackingInfo DefaultReadPixelPI(
    const webgl::FormatUsageInfo* usage) {
  MOZ_ASSERT(usage->IsRenderable());
  const auto& format = *usage->format;
  switch (format.componentType) {
    case webgl::ComponentType::NormUInt:
      if (format.r == 16) {
        return {LOCAL_GL_RGBA, LOCAL_GL_UNSIGNED_SHORT};
      }
      return {LOCAL_GL_RGBA, LOCAL_GL_UNSIGNED_BYTE};

    case webgl::ComponentType::Int:
      return {LOCAL_GL_RGBA_INTEGER, LOCAL_GL_INT};

    case webgl::ComponentType::UInt:
      return {LOCAL_GL_RGBA_INTEGER, LOCAL_GL_UNSIGNED_INT};

    case webgl::ComponentType::Float:
      return {LOCAL_GL_RGBA, LOCAL_GL_FLOAT};

    default:
      MOZ_CRASH();
  }
}

static bool ArePossiblePackEnums(const WebGLContext* webgl,
                                 const webgl::PackingInfo& pi) {
  // OpenGL ES 2.0 $4.3.1 - IMPLEMENTATION_COLOR_READ_{TYPE/FORMAT} is a valid
  // combination for glReadPixels()...

  // Only valid when pulled from:
  // * GLES 2.0.25 p105:
  //   "table 3.4, excluding formats LUMINANCE and LUMINANCE_ALPHA."
  // * GLES 3.0.4 p193:
  //   "table 3.2, excluding formats DEPTH_COMPONENT and DEPTH_STENCIL."
  switch (pi.format) {
    case LOCAL_GL_LUMINANCE:
    case LOCAL_GL_LUMINANCE_ALPHA:
    case LOCAL_GL_DEPTH_COMPONENT:
    case LOCAL_GL_DEPTH_STENCIL:
      return false;
  }

  if (pi.type == LOCAL_GL_UNSIGNED_INT_24_8) return false;

  const auto pii = webgl::PackingInfoInfo::For(pi);
  if (!pii) return false;

  return true;
}

webgl::PackingInfo WebGLContext::ValidImplementationColorReadPI(
    const webgl::FormatUsageInfo* usage) const {
  const auto defaultPI = DefaultReadPixelPI(usage);

  // ES2_compatibility always returns RGBA/UNSIGNED_BYTE, so branch on actual
  // IsGLES(). Also OSX+NV generates an error here.
  if (!gl->IsGLES()) return defaultPI;

  webgl::PackingInfo implPI;
  gl->fGetIntegerv(LOCAL_GL_IMPLEMENTATION_COLOR_READ_FORMAT,
                   (GLint*)&implPI.format);
  gl->fGetIntegerv(LOCAL_GL_IMPLEMENTATION_COLOR_READ_TYPE,
                   (GLint*)&implPI.type);

  if (!ArePossiblePackEnums(this, implPI)) return defaultPI;

  return implPI;
}

static bool ValidateReadPixelsFormatAndType(
    const webgl::FormatUsageInfo* srcUsage, const webgl::PackingInfo& pi,
    gl::GLContext* gl, WebGLContext* webgl) {
  if (!ArePossiblePackEnums(webgl, pi)) {
    webgl->ErrorInvalidEnum("Unexpected format or type.");
    return false;
  }

  const auto defaultPI = DefaultReadPixelPI(srcUsage);
  if (pi == defaultPI) return true;

  ////

  // OpenGL ES 3.0.4 p194 - When the internal format of the rendering surface is
  // RGB10_A2, a third combination of format RGBA and type
  // UNSIGNED_INT_2_10_10_10_REV is accepted.

  if (webgl->IsWebGL2() &&
      srcUsage->format->effectiveFormat == webgl::EffectiveFormat::RGB10_A2 &&
      pi.format == LOCAL_GL_RGBA &&
      pi.type == LOCAL_GL_UNSIGNED_INT_2_10_10_10_REV) {
    return true;
  }

  ////

  MOZ_ASSERT(gl->IsCurrent());
  const auto implPI = webgl->ValidImplementationColorReadPI(srcUsage);
  if (pi == implPI) return true;

  ////

  // clang-format off
  webgl->ErrorInvalidOperation(
      "Format and type %s/%s incompatible with this %s attachment."
      " This framebuffer requires either %s/%s or"
      " getParameter(IMPLEMENTATION_COLOR_READ_FORMAT/_TYPE) %s/%s.",
      EnumString(pi.format).c_str(), EnumString(pi.type).c_str(),
      srcUsage->format->name,
      EnumString(defaultPI.format).c_str(), EnumString(defaultPI.type).c_str(),
      EnumString(implPI.format).c_str(), EnumString(implPI.type).c_str());
  // clang-format on

  return false;
}

webgl::ReadPixelsResult WebGLContext::ReadPixelsImpl(
    const webgl::ReadPixelsDesc& desc, const uintptr_t dest,
    const uint64_t availBytes) {
  const webgl::FormatUsageInfo* srcFormat;
  uint32_t srcWidth;
  uint32_t srcHeight;
  if (!BindCurFBForColorRead(&srcFormat, &srcWidth, &srcHeight)) return {};

  //////

  if (!ValidateReadPixelsFormatAndType(srcFormat, desc.pi, gl, this)) return {};

  //////

  // Reject invalid pack alignment.
  switch (desc.packState.alignmentInTypeElems) {
    case 1:
    case 2:
    case 4:
    case 8:
      break;  // all good
    default:
      ErrorInvalidValue("pack alignment must be 1, 2, 4, or 8.");
      return {};
  }

  const auto& srcOffset = desc.srcOffset;
  const auto& size = desc.size;

  if (!ivec2::From(size)) {
    ErrorInvalidValue("width and height must be non-negative.");
    return {};
  }

  const auto& packing = desc.packState;
  const auto explicitPackingRes = webgl::ExplicitPixelPackingState::ForUseWith(
      packing, LOCAL_GL_TEXTURE_2D, {size.x, size.y, 1}, desc.pi, {});
  if (!explicitPackingRes.isOk()) {
    ErrorInvalidOperation("%s", explicitPackingRes.inspectErr().c_str());
    return {};
  }
  const auto& explicitPacking = explicitPackingRes.inspect();
  const auto& rowStride = explicitPacking.metrics.bytesPerRowStride;
  const auto& bytesNeeded = explicitPacking.metrics.totalBytesUsed;
  if (bytesNeeded > availBytes) {
    ErrorInvalidOperation("buffer too small");
    return {};
  }

  ////

  int32_t readX, readY;
  int32_t writeX, writeY;
  int32_t rwWidth, rwHeight;
  if (!Intersect(srcWidth, srcOffset.x, size.x, &readX, &writeX, &rwWidth) ||
      !Intersect(srcHeight, srcOffset.y, size.y, &readY, &writeY, &rwHeight)) {
    ErrorOutOfMemory("Bad subrect selection.");
    return {};
  }

  ////////////////
  // Now that the errors are out of the way, on to actually reading!

  gl->fPixelStorei(LOCAL_GL_PACK_ALIGNMENT, packing.alignmentInTypeElems);
  if (IsWebGL2()) {
    gl->fPixelStorei(LOCAL_GL_PACK_ROW_LENGTH, packing.rowLength);
    gl->fPixelStorei(LOCAL_GL_PACK_SKIP_PIXELS, packing.skipPixels);
    gl->fPixelStorei(LOCAL_GL_PACK_SKIP_ROWS, packing.skipRows);
  }

  if (!rwWidth || !rwHeight) {
    // Disjoint rects, so we're done already.
    DummyReadFramebufferOperation();
    return {};
  }
  const auto rwSize = *uvec2::From(rwWidth, rwHeight);

  const auto res = webgl::ReadPixelsResult{
      {{writeX, writeY}, {rwSize.x, rwSize.y}}, rowStride};

  if (rwSize == size) {
    DoReadPixelsAndConvert(srcFormat->format, desc, dest, bytesNeeded,
                           rowStride);
    return res;
  }

  // Read request contains out-of-bounds pixels. Unfortunately:
  // GLES 3.0.4 p194 "Obtaining Pixels from the Framebuffer":
  // "If any of these pixels lies outside of the window allocated to the current
  // GL context, or outside of the image attached to the currently bound
  // framebuffer object, then the values obtained for those pixels are
  // undefined."

  // This is a slow-path, so warn people away!
  GenerateWarning(
      "Out-of-bounds reads with readPixels are deprecated, and"
      " may be slow.");

  ////////////////////////////////////
  // Read only the in-bounds pixels.

  if (IsWebGL2()) {
    if (!packing.rowLength) {
      gl->fPixelStorei(LOCAL_GL_PACK_ROW_LENGTH, packing.skipPixels + size.x);
    }
    gl->fPixelStorei(LOCAL_GL_PACK_SKIP_PIXELS, packing.skipPixels + writeX);
    gl->fPixelStorei(LOCAL_GL_PACK_SKIP_ROWS, packing.skipRows + writeY);

    auto desc2 = desc;
    desc2.srcOffset = {readX, readY};
    desc2.size = rwSize;

    DoReadPixelsAndConvert(srcFormat->format, desc2, dest, bytesNeeded,
                           rowStride);
  } else {
    // I *did* say "hilariously slow".

    auto desc2 = desc;
    desc2.srcOffset = {readX, readY};
    desc2.size = {rwSize.x, 1};

    const auto skipBytes = writeX * explicitPacking.metrics.bytesPerPixel;
    const auto usedRowBytes = rwSize.x * explicitPacking.metrics.bytesPerPixel;
    for (const auto j : IntegerRange(rwSize.y)) {
      desc2.srcOffset.y = readY + j;
      const auto destWriteBegin = dest + skipBytes + (writeY + j) * rowStride;
      MOZ_RELEASE_ASSERT(dest <= destWriteBegin);
      MOZ_RELEASE_ASSERT(destWriteBegin <= dest + availBytes);

      const auto destWriteEnd = destWriteBegin + usedRowBytes;
      MOZ_RELEASE_ASSERT(dest <= destWriteEnd);
      MOZ_RELEASE_ASSERT(destWriteEnd <= dest + availBytes);

      DoReadPixelsAndConvert(srcFormat->format, desc2, destWriteBegin,
                             destWriteEnd - destWriteBegin, rowStride);
    }
  }

  return res;
}

void WebGLContext::RenderbufferStorageMultisample(WebGLRenderbuffer& rb,
                                                  uint32_t samples,
                                                  GLenum internalFormat,
                                                  uint32_t width,
                                                  uint32_t height) const {
  const FuncScope funcScope(*this, "renderbufferStorage(Multisample)?");
  if (IsContextLost()) return;

  rb.RenderbufferStorage(samples, internalFormat, width, height);
}

void WebGLContext::Scissor(GLint x, GLint y, GLsizei width, GLsizei height) {
  const FuncScope funcScope(*this, "scissor");
  if (IsContextLost()) return;

  if (!ValidateNonNegative("width", width) ||
      !ValidateNonNegative("height", height)) {
    return;
  }

  mScissorRect = {x, y, width, height};
  mScissorRect.Apply(*gl);
}

void WebGLContext::StencilFuncSeparate(GLenum face, GLenum func, GLint ref,
                                       GLuint mask) {
  const FuncScope funcScope(*this, "stencilFuncSeparate");
  if (IsContextLost()) return;

  if (!ValidateFaceEnum(face) || !ValidateComparisonEnum(*this, func)) {
    return;
  }

  switch (face) {
    case LOCAL_GL_FRONT_AND_BACK:
      mStencilRefFront = ref;
      mStencilRefBack = ref;
      mStencilValueMaskFront = mask;
      mStencilValueMaskBack = mask;
      break;
    case LOCAL_GL_FRONT:
      mStencilRefFront = ref;
      mStencilValueMaskFront = mask;
      break;
    case LOCAL_GL_BACK:
      mStencilRefBack = ref;
      mStencilValueMaskBack = mask;
      break;
  }

  gl->fStencilFuncSeparate(face, func, ref, mask);
}

void WebGLContext::StencilOpSeparate(GLenum face, GLenum sfail, GLenum dpfail,
                                     GLenum dppass) {
  const FuncScope funcScope(*this, "stencilOpSeparate");
  if (IsContextLost()) return;

  if (!ValidateFaceEnum(face) || !ValidateStencilOpEnum(sfail, "sfail") ||
      !ValidateStencilOpEnum(dpfail, "dpfail") ||
      !ValidateStencilOpEnum(dppass, "dppass"))
    return;

  gl->fStencilOpSeparate(face, sfail, dpfail, dppass);
}

////////////////////////////////////////////////////////////////////////////////
// Uniform setters.

void WebGLContext::UniformData(
    const uint32_t loc, const bool transpose,
    const Range<const webgl::UniformDataVal>& data) const {
  const FuncScope funcScope(*this, "uniform setter");

  if (!IsWebGL2() && transpose) {
    GenerateError(LOCAL_GL_INVALID_VALUE, "`transpose`:true requires WebGL 2.");
    return;
  }

  // -

  const auto& link = mActiveProgramLinkInfo;
  if (!link) return;

  const auto locInfo = MaybeFind(link->locationMap, loc);
  if (!locInfo) {
    // Null WebGLUniformLocations become -1, which will end up here.
    return;
  }

  const auto& validationInfo = locInfo->info;
  const auto& activeInfo = validationInfo.info;
  const auto& channels = validationInfo.channelsPerElem;
  const auto& pfn = validationInfo.pfn;

  // -

  const auto lengthInType = data.length();
  const size_t availElemCount = lengthInType / channels;
  if (availElemCount > 1 && !validationInfo.isArray) {
    GenerateError(
        LOCAL_GL_INVALID_OPERATION,
        "(uniform %s) `values` length (%u) must exactly match size of %s.",
        activeInfo.name.c_str(), lengthInType,
        EnumString(activeInfo.elemType).c_str());
    return;
  }
  const size_t elemCount =
      validationInfo.isArray
          ? std::min(availElemCount, size_t(activeInfo.elemCount))
          : availElemCount;

  // -

  const auto& samplerInfo = locInfo->samplerInfo;
  if (samplerInfo) {
    const auto idata = reinterpret_cast<const uint32_t*>(data.begin().get());
    const auto maxTexUnits = GLMaxTextureUnits();
    for (const auto& val : Range<const uint32_t>(idata, elemCount)) {
      if (val >= maxTexUnits) {
        ErrorInvalidValue(
            "This uniform location is a sampler, but %d"
            " is not a valid texture unit.",
            val);
        return;
      }
    }
  }

  // -

  // This is a little galaxy-brain, sorry!
  const auto ptr = static_cast<const void*>(data.begin().get());
  (*pfn)(*gl, static_cast<GLint>(loc), elemCount, transpose, ptr);

  // -

  if (samplerInfo) {
    auto& texUnits = samplerInfo->texUnits;

    const auto srcBegin = reinterpret_cast<const uint32_t*>(data.begin().get());
    auto destIndex = locInfo->indexIntoUniform;
    if (destIndex < texUnits.length()) {
      // Only sample as many indexes as available tex units allow.
      const auto destCount = std::min(elemCount, texUnits.length() - destIndex);
      for (const auto& val : Range<const uint32_t>(srcBegin, destCount)) {
        texUnits[destIndex] = AssertedCast<uint8_t>(val);
        destIndex += 1;
      }
    }
  }
}

////////////////////////////////////////////////////////////////////////////////

void WebGLContext::UseProgram(WebGLProgram* prog) {
  FuncScope funcScope(*this, "useProgram");
  if (IsContextLost()) return;
  funcScope.mBindFailureGuard = true;

  if (!prog) {
    mCurrentProgram = nullptr;
    mActiveProgramLinkInfo = nullptr;
    funcScope.mBindFailureGuard = false;
    return;
  }

  if (!ValidateObject("prog", *prog)) return;

  if (!prog->UseProgram()) return;

  mCurrentProgram = prog;
  mActiveProgramLinkInfo = mCurrentProgram->LinkInfo();

  funcScope.mBindFailureGuard = false;
}

bool WebGLContext::ValidateProgram(const WebGLProgram& prog) const {
  const FuncScope funcScope(*this, "validateProgram");
  if (IsContextLost()) return false;

  return prog.ValidateProgram();
}

RefPtr<WebGLFramebuffer> WebGLContext::CreateFramebuffer() {
  const FuncScope funcScope(*this, "createFramebuffer");
  if (IsContextLost()) return nullptr;

  GLuint fbo = 0;
  gl->fGenFramebuffers(1, &fbo);

  return new WebGLFramebuffer(this, fbo);
}

RefPtr<WebGLFramebuffer> WebGLContext::CreateOpaqueFramebuffer(
    const webgl::OpaqueFramebufferOptions& options) {
  const FuncScope funcScope(*this, "createOpaqueFramebuffer");
  if (IsContextLost()) return nullptr;

  uint32_t samples = options.antialias ? StaticPrefs::webgl_msaa_samples() : 0;
  samples = std::min(samples, gl->MaxSamples());
  const gfx::IntSize size = {options.width, options.height};

  auto fbo =
      gl::MozFramebuffer::Create(gl, size, samples, options.depthStencil);
  if (!fbo) {
    return nullptr;
  }

  return new WebGLFramebuffer(this, std::move(fbo));
}

RefPtr<WebGLRenderbuffer> WebGLContext::CreateRenderbuffer() {
  const FuncScope funcScope(*this, "createRenderbuffer");
  if (IsContextLost()) return nullptr;

  return new WebGLRenderbuffer(this);
}

void WebGLContext::Viewport(GLint x, GLint y, GLsizei width, GLsizei height) {
  const FuncScope funcScope(*this, "viewport");
  if (IsContextLost()) return;

  if (!ValidateNonNegative("width", width) ||
      !ValidateNonNegative("height", height)) {
    return;
  }

  const auto& limits = Limits();
  width = std::min(width, static_cast<GLsizei>(limits.maxViewportDim));
  height = std::min(height, static_cast<GLsizei>(limits.maxViewportDim));

  gl->fViewport(x, y, width, height);

  mViewportX = x;
  mViewportY = y;
  mViewportWidth = width;
  mViewportHeight = height;
}

void WebGLContext::CompileShader(WebGLShader& shader) {
  const FuncScope funcScope(*this, "compileShader");
  if (IsContextLost()) return;

  if (!ValidateObject("shader", shader)) return;

  shader.CompileShader();
}

Maybe<webgl::ShaderPrecisionFormat> WebGLContext::GetShaderPrecisionFormat(
    GLenum shadertype, GLenum precisiontype) const {
  const FuncScope funcScope(*this, "getShaderPrecisionFormat");
  if (IsContextLost()) return Nothing();

  switch (shadertype) {
    case LOCAL_GL_FRAGMENT_SHADER:
    case LOCAL_GL_VERTEX_SHADER:
      break;
    default:
      ErrorInvalidEnumInfo("shadertype", shadertype);
      return Nothing();
  }

  switch (precisiontype) {
    case LOCAL_GL_LOW_FLOAT:
    case LOCAL_GL_MEDIUM_FLOAT:
    case LOCAL_GL_HIGH_FLOAT:
    case LOCAL_GL_LOW_INT:
    case LOCAL_GL_MEDIUM_INT:
    case LOCAL_GL_HIGH_INT:
      break;
    default:
      ErrorInvalidEnumInfo("precisiontype", precisiontype);
      return Nothing();
  }

  GLint range[2], precision;

  if (mDisableFragHighP && shadertype == LOCAL_GL_FRAGMENT_SHADER &&
      (precisiontype == LOCAL_GL_HIGH_FLOAT ||
       precisiontype == LOCAL_GL_HIGH_INT)) {
    precision = 0;
    range[0] = 0;
    range[1] = 0;
  } else {
    gl->fGetShaderPrecisionFormat(shadertype, precisiontype, range, &precision);
  }

  return Some(webgl::ShaderPrecisionFormat{range[0], range[1], precision});
}

void WebGLContext::ShaderSource(WebGLShader& shader,
                                const std::string& source) const {
  const FuncScope funcScope(*this, "shaderSource");
  if (IsContextLost()) return;

  shader.ShaderSource(source);
}

void WebGLContext::BlendColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
  const FuncScope funcScope(*this, "blendColor");
  if (IsContextLost()) return;

  gl->fBlendColor(r, g, b, a);
}

void WebGLContext::Flush() {
  const FuncScope funcScope(*this, "flush");
  if (IsContextLost()) return;

  gl->fFlush();
}

void WebGLContext::Finish() {
  const FuncScope funcScope(*this, "finish");
  if (IsContextLost()) return;

  gl->fFinish();

  mCompletedFenceId = mNextFenceId;
  mNextFenceId += 1;
}

void WebGLContext::LineWidth(GLfloat width) {
  const FuncScope funcScope(*this, "lineWidth");
  if (IsContextLost()) return;

  // Doing it this way instead of `if (width <= 0.0)` handles NaNs.
  const bool isValid = width > 0.0;
  if (!isValid) {
    ErrorInvalidValue("`width` must be positive and non-zero.");
    return;
  }

  mLineWidth = width;

  if (gl->IsCoreProfile() && width > 1.0) {
    width = 1.0;
  }

  gl->fLineWidth(width);
}

void WebGLContext::PolygonOffset(GLfloat factor, GLfloat units) {
  const FuncScope funcScope(*this, "polygonOffset");
  if (IsContextLost()) return;

  gl->fPolygonOffset(factor, units);
}

void WebGLContext::ProvokingVertex(const webgl::ProvokingVertex mode) const {
  const FuncScope funcScope(*this, "provokingVertex");
  if (IsContextLost()) return;
  MOZ_RELEASE_ASSERT(
      IsExtensionEnabled(WebGLExtensionID::WEBGL_provoking_vertex));

  gl->fProvokingVertex(UnderlyingValue(mode));
}

void WebGLContext::SampleCoverage(GLclampf value, WebGLboolean invert) {
  const FuncScope funcScope(*this, "sampleCoverage");
  if (IsContextLost()) return;

  gl->fSampleCoverage(value, invert);
}

}  // namespace mozilla
