/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GLContext.h"
#include "GLScreenBuffer.h"
#include "MozFramebuffer.h"
#include "WebGLBuffer.h"
#include "WebGLContext.h"
#include "WebGLFormats.h"
#include "WebGLFramebuffer.h"
#include "WebGLProgram.h"
#include "WebGLRenderbuffer.h"
#include "WebGLShader.h"
#include "WebGLTexture.h"
#include "WebGLVertexArray.h"
#include "mozilla/Maybe.h"
#include "mozilla/Preferences.h"
#include "nsString.h"

namespace mozilla {

void WebGLContext::SetEnabled(const GLenum cap, const Maybe<GLuint> i,
                              const bool enabled) {
  const FuncScope funcScope(*this, "enable(i)/disable(i)");
  if (IsContextLost()) return;

  if (!mIsEnabledMapKeys.count(cap)) {
    MOZ_ASSERT(false, "Bad cap.");
    return;
  }

  if (cap == LOCAL_GL_BLEND) {
    if (i) {
      const auto limit = MaxValidDrawBuffers();
      if (*i >= limit) {
        ErrorInvalidValue("`index` (%u) must be < %s (%u)", *i,
                          "MAX_DRAW_BUFFERS", limit);
        return;
      }
      mBlendEnabled[*i] = enabled;
    } else {
      if (enabled) {
        mBlendEnabled.set();
      } else {
        mBlendEnabled.reset();
      }
    }
  } else {
    if (i) {
      MOZ_ASSERT(false, "i");
      return;
    }
    const auto slot = GetStateTrackingSlot(cap);
    if (slot) {
      *slot = enabled;
    }
  }

  switch (cap) {
    case LOCAL_GL_DEPTH_TEST:
    case LOCAL_GL_STENCIL_TEST:
      break;  // Lazily applied, so don't tell GL yet or we will desync.

    default:
      // Non-lazy caps.
      if (i) {
        if (enabled) {
          gl->fEnablei(cap, *i);
        } else {
          gl->fDisablei(cap, *i);
        }
      } else {
        gl->SetEnabled(cap, enabled);
      }
      break;
  }
}

bool WebGLContext::GetStencilBits(GLint* const out_stencilBits) const {
  *out_stencilBits = 0;
  if (mBoundDrawFramebuffer) {
    if (!mBoundDrawFramebuffer->IsCheckFramebufferStatusComplete()) {
      // Error, we don't know which stencil buffer's bits to use
      ErrorInvalidFramebufferOperation(
          "getParameter: framebuffer has two stencil buffers bound");
      return false;
    }

    if (mBoundDrawFramebuffer->StencilAttachment().HasAttachment() ||
        mBoundDrawFramebuffer->DepthStencilAttachment().HasAttachment()) {
      *out_stencilBits = 8;
    }
  } else if (mOptions.stencil) {
    *out_stencilBits = 8;
  }

  return true;
}

Maybe<double> WebGLContext::GetParameter(const GLenum pname) {
  const FuncScope funcScope(*this, "getParameter");
  if (IsContextLost()) return {};

  if (IsWebGL2() || IsExtensionEnabled(WebGLExtensionID::WEBGL_draw_buffers)) {
    if (pname == LOCAL_GL_MAX_COLOR_ATTACHMENTS) {
      return Some(MaxValidDrawBuffers());

    } else if (pname == LOCAL_GL_MAX_DRAW_BUFFERS) {
      return Some(MaxValidDrawBuffers());

    } else if (pname >= LOCAL_GL_DRAW_BUFFER0 &&
               pname < GLenum(LOCAL_GL_DRAW_BUFFER0 + MaxValidDrawBuffers())) {
      const auto slotId = pname - LOCAL_GL_DRAW_BUFFER0;
      GLenum ret = LOCAL_GL_NONE;
      if (!mBoundDrawFramebuffer) {
        if (slotId == 0) {
          ret = mDefaultFB_DrawBuffer0;
        }
      } else {
        const auto& fb = *mBoundDrawFramebuffer;
        const auto& bs = fb.DrawBufferEnabled();
        if (bs[slotId]) {
          ret = LOCAL_GL_COLOR_ATTACHMENT0 + slotId;
        }
      }
      return Some(ret);
    }
  }

  if (IsExtensionEnabled(WebGLExtensionID::EXT_disjoint_timer_query)) {
    switch (pname) {
      case LOCAL_GL_TIMESTAMP_EXT: {
        uint64_t val = 0;
        if (Has64BitTimestamps()) {
          gl->fGetInteger64v(pname, (GLint64*)&val);
        } else {
          GLint val32 = 0;
          gl->fGetIntegerv(pname, &val32);
          val = val32;
        }
        // TODO: JS doesn't support 64-bit integers. Be lossy and
        // cast to double (53 bits)
        return Some(val);
      }

      case LOCAL_GL_GPU_DISJOINT_EXT: {
        realGLboolean val = false;  // Not disjoint by default.
        if (gl->IsExtensionSupported(gl::GLContext::EXT_disjoint_timer_query)) {
          gl->fGetBooleanv(pname, &val);
        }
        return Some(bool(val));
      }

      default:
        break;
    }
  }

  if (IsWebGL2() ||
      IsExtensionEnabled(WebGLExtensionID::OES_standard_derivatives)) {
    if (pname == LOCAL_GL_FRAGMENT_SHADER_DERIVATIVE_HINT) {
      GLint i = 0;
      gl->fGetIntegerv(pname, &i);
      return Some(i);
    }
  }

  if (IsExtensionEnabled(WebGLExtensionID::EXT_clip_control)) {
    if (pname == LOCAL_GL_CLIP_ORIGIN || pname == LOCAL_GL_CLIP_DEPTH_MODE) {
      GLint value = 0;
      gl->fGetIntegerv(pname, &value);
      return Some(value);
    }
  }

  if (IsExtensionEnabled(WebGLExtensionID::EXT_polygon_offset_clamp)) {
    if (pname == LOCAL_GL_POLYGON_OFFSET_CLAMP_EXT) {
      GLfloat f = 0.f;
      gl->fGetFloatv(pname, &f);
      return Some(f);
    }
  }

  if (IsExtensionEnabled(WebGLExtensionID::EXT_texture_filter_anisotropic)) {
    if (pname == LOCAL_GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT) {
      GLfloat f = 0.f;
      gl->fGetFloatv(pname, &f);
      return Some(f);
    }
  }

  if (IsExtensionEnabled(WebGLExtensionID::MOZ_debug)) {
    if (pname == dom::MOZ_debug_Binding::DOES_INDEX_VALIDATION) {
      return Some(mNeedsIndexValidation);
    }
  }

  switch (pname) {
    ////////////////////////////////
    // Single-value params

    // unsigned int
    case LOCAL_GL_CULL_FACE_MODE:
    case LOCAL_GL_FRONT_FACE:
    case LOCAL_GL_ACTIVE_TEXTURE:
    case LOCAL_GL_STENCIL_FUNC:
    case LOCAL_GL_STENCIL_FAIL:
    case LOCAL_GL_STENCIL_PASS_DEPTH_FAIL:
    case LOCAL_GL_STENCIL_PASS_DEPTH_PASS:
    case LOCAL_GL_STENCIL_BACK_FUNC:
    case LOCAL_GL_STENCIL_BACK_FAIL:
    case LOCAL_GL_STENCIL_BACK_PASS_DEPTH_FAIL:
    case LOCAL_GL_STENCIL_BACK_PASS_DEPTH_PASS:
    case LOCAL_GL_DEPTH_FUNC:
    case LOCAL_GL_BLEND_SRC_RGB:
    case LOCAL_GL_BLEND_SRC_ALPHA:
    case LOCAL_GL_BLEND_DST_RGB:
    case LOCAL_GL_BLEND_DST_ALPHA:
    case LOCAL_GL_BLEND_EQUATION_RGB:
    case LOCAL_GL_BLEND_EQUATION_ALPHA: {
      GLint i = 0;
      gl->fGetIntegerv(pname, &i);
      return Some(i);
    }

    case LOCAL_GL_GENERATE_MIPMAP_HINT:
      return Some(mGenerateMipmapHint);

    case LOCAL_GL_IMPLEMENTATION_COLOR_READ_FORMAT:
    case LOCAL_GL_IMPLEMENTATION_COLOR_READ_TYPE: {
      const webgl::FormatUsageInfo* usage;
      uint32_t width, height;
      if (!BindCurFBForColorRead(&usage, &width, &height,
                                 LOCAL_GL_INVALID_OPERATION))
        return Nothing();

      const auto implPI = ValidImplementationColorReadPI(usage);

      GLenum ret;
      if (pname == LOCAL_GL_IMPLEMENTATION_COLOR_READ_FORMAT) {
        ret = implPI.format;
      } else {
        ret = implPI.type;
      }
      return Some(ret);
    }

    // int
    case LOCAL_GL_STENCIL_REF:
    case LOCAL_GL_STENCIL_BACK_REF: {
      GLint stencilBits = 0;
      if (!GetStencilBits(&stencilBits)) return Nothing();

      // Assuming stencils have 8 bits
      const GLint stencilMask = (1 << stencilBits) - 1;

      GLint refValue = 0;
      gl->fGetIntegerv(pname, &refValue);

      return Some(refValue & stencilMask);
    }

    case LOCAL_GL_SAMPLE_BUFFERS:
    case LOCAL_GL_SAMPLES: {
      const auto& fb = mBoundDrawFramebuffer;
      auto samples = [&]() -> Maybe<uint32_t> {
        if (!fb) {
          if (!EnsureDefaultFB()) return Nothing();
          return Some(mDefaultFB->mSamples);
        }

        if (!fb->IsCheckFramebufferStatusComplete()) return Some(0);

        DoBindFB(fb, LOCAL_GL_FRAMEBUFFER);
        return Some(gl->GetIntAs<uint32_t>(LOCAL_GL_SAMPLES));
      }();
      if (samples && pname == LOCAL_GL_SAMPLE_BUFFERS) {
        samples = Some(uint32_t(bool(samples.value())));
      }
      if (!samples) return Nothing();
      return Some(samples.value());
    }

    case LOCAL_GL_STENCIL_CLEAR_VALUE:
    case LOCAL_GL_SUBPIXEL_BITS: {
      GLint i = 0;
      gl->fGetIntegerv(pname, &i);
      return Some(i);
    }

    case LOCAL_GL_RED_BITS:
    case LOCAL_GL_GREEN_BITS:
    case LOCAL_GL_BLUE_BITS:
    case LOCAL_GL_ALPHA_BITS:
    case LOCAL_GL_DEPTH_BITS:
    case LOCAL_GL_STENCIL_BITS: {
      const auto format = [&]() -> const webgl::FormatInfo* {
        const auto& fb = mBoundDrawFramebuffer;
        if (fb) {
          if (!fb->IsCheckFramebufferStatusComplete()) return nullptr;

          const auto& attachment = [&]() -> const auto& {
            switch (pname) {
              case LOCAL_GL_DEPTH_BITS:
                if (fb->DepthStencilAttachment().HasAttachment())
                  return fb->DepthStencilAttachment();
                return fb->DepthAttachment();

              case LOCAL_GL_STENCIL_BITS:
                if (fb->DepthStencilAttachment().HasAttachment())
                  return fb->DepthStencilAttachment();
                return fb->StencilAttachment();

              default:
                return fb->ColorAttachment0();
            }
          }();

          const auto imageInfo = attachment.GetImageInfo();
          if (!imageInfo) return nullptr;
          return imageInfo->mFormat->format;
        }

        auto effFormat = webgl::EffectiveFormat::RGB8;
        switch (pname) {
          case LOCAL_GL_DEPTH_BITS:
            if (mOptions.depth) {
              effFormat = webgl::EffectiveFormat::DEPTH24_STENCIL8;
            }
            break;

          case LOCAL_GL_STENCIL_BITS:
            if (mOptions.stencil) {
              effFormat = webgl::EffectiveFormat::DEPTH24_STENCIL8;
            }
            break;

          default:
            if (mOptions.alpha) {
              effFormat = webgl::EffectiveFormat::RGBA8;
            }
            break;
        }
        return webgl::GetFormat(effFormat);
      }();
      int32_t ret = 0;
      if (format) {
        switch (pname) {
          case LOCAL_GL_RED_BITS:
            ret = format->r;
            break;
          case LOCAL_GL_GREEN_BITS:
            ret = format->g;
            break;
          case LOCAL_GL_BLUE_BITS:
            ret = format->b;
            break;
          case LOCAL_GL_ALPHA_BITS:
            ret = format->a;
            break;
          case LOCAL_GL_DEPTH_BITS:
            ret = format->d;
            break;
          case LOCAL_GL_STENCIL_BITS:
            ret = format->s;
            break;
        }
      }
      return Some(ret);
    }

    case LOCAL_GL_MAX_RENDERBUFFER_SIZE:
      return Some(mGLMaxRenderbufferSize);

    case LOCAL_GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS:
      return Some(mGLMaxVertexTextureImageUnits);

    case LOCAL_GL_MAX_TEXTURE_IMAGE_UNITS:
      return Some(mGLMaxFragmentTextureImageUnits);

    case LOCAL_GL_MAX_VERTEX_UNIFORM_VECTORS:
      return Some(mGLMaxVertexUniformVectors);

    case LOCAL_GL_MAX_FRAGMENT_UNIFORM_VECTORS:
      return Some(mGLMaxFragmentUniformVectors);

    case LOCAL_GL_MAX_VARYING_VECTORS:
      return Some(mGLMaxFragmentInputVectors);

    // unsigned int. here we may have to return very large values like 2^32-1
    // that can't be represented as javascript integer values. We just return
    // them as doubles and javascript doesn't care.
    case LOCAL_GL_STENCIL_BACK_VALUE_MASK:
      return Some(mStencilValueMaskBack);
      // pass as FP value to allow large values such as 2^32-1.

    case LOCAL_GL_STENCIL_BACK_WRITEMASK:
      return Some(mStencilWriteMaskBack);

    case LOCAL_GL_STENCIL_VALUE_MASK:
      return Some(mStencilValueMaskFront);

    case LOCAL_GL_STENCIL_WRITEMASK:
      return Some(mStencilWriteMaskFront);

    case LOCAL_GL_COLOR_WRITEMASK:
      return Some(mColorWriteMasks[0]);

    // float
    case LOCAL_GL_LINE_WIDTH:
      return Some((double)mLineWidth);

    case LOCAL_GL_DEPTH_CLEAR_VALUE:
    case LOCAL_GL_POLYGON_OFFSET_FACTOR:
    case LOCAL_GL_POLYGON_OFFSET_UNITS:
    case LOCAL_GL_SAMPLE_COVERAGE_VALUE: {
      GLfloat f = 0.f;
      gl->fGetFloatv(pname, &f);
      return Some(f);
    }

    // bool
    case LOCAL_GL_DEPTH_TEST:
      return Some((bool)mDepthTestEnabled);
    case LOCAL_GL_STENCIL_TEST:
      return Some((bool)mStencilTestEnabled);

    case LOCAL_GL_BLEND:
    case LOCAL_GL_CULL_FACE:
    case LOCAL_GL_DITHER:
    case LOCAL_GL_POLYGON_OFFSET_FILL:
    case LOCAL_GL_SCISSOR_TEST:
    case LOCAL_GL_SAMPLE_COVERAGE_INVERT:
    case LOCAL_GL_SAMPLE_ALPHA_TO_COVERAGE:
    case LOCAL_GL_SAMPLE_COVERAGE:
    case LOCAL_GL_DEPTH_WRITEMASK: {
      realGLboolean b = 0;
      gl->fGetBooleanv(pname, &b);
      return Some(bool(b));
    }

    default:
      break;
  }

  ErrorInvalidEnumInfo("pname", pname);
  return Nothing();
}

bool* WebGLContext::GetStateTrackingSlot(GLenum cap) {
  switch (cap) {
    case LOCAL_GL_DEPTH_TEST:
      return &mDepthTestEnabled;
    case LOCAL_GL_DITHER:
      return &mDitherEnabled;
    case LOCAL_GL_RASTERIZER_DISCARD:
      return &mRasterizerDiscardEnabled;
    case LOCAL_GL_SCISSOR_TEST:
      return &mScissorTestEnabled;
    case LOCAL_GL_STENCIL_TEST:
      return &mStencilTestEnabled;
  }

  return nullptr;
}

}  // namespace mozilla
