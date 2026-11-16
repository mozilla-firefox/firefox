/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLExtensions.h"

#include "GLContext.h"
#include "WebGLContext.h"
#include "WebGLFormats.h"
#include "mozilla/Casting.h"
#include "mozilla/ScopeExit.h"
#include "mozilla/StaticPrefs_webgl.h"
#include "mozilla/dom/WebGLRenderingContextBinding.h"

namespace mozilla {

static bool TestShaderCompile(gl::GLContext* const gl, const GLenum type,
                              const std::string& source) {
  const auto shader = gl->fCreateShader(type);
  const auto cleanup = MakeScopeExit([&]() { gl->fDeleteShader(shader); });

  const std::array<const char*, 1> parts = {source.c_str()};
  gl->fShaderSource(shader, parts.size(), parts.data(), nullptr);
  gl->fCompileShader(shader);

  GLint status = 0;
  gl->fGetShaderiv(shader, LOCAL_GL_COMPILE_STATUS, &status);

  if (status == LOCAL_GL_TRUE) return true;

  std::vector<char> chars;
  chars.resize(1000);
  gl->fGetShaderInfoLog(shader, LazyAssertedCast(chars.size() - 1), nullptr,
                        chars.data());
  printf_stderr("GetShaderInfoLog() ->\n%s\n", chars.data());

#if defined(MOZ_WIDGET_ANDROID) && (defined(_M_IX86) || defined(__i386__) || \
                                    defined(__x86_64__) || defined(__amd64__))
  // Flakey gl on x86 android emulators in CI sometimes crash on the first
  // GetShaderSource call.
  printf_stderr("source ->\n%s\n", source.c_str());
#else
  gl->fGetShaderSource(shader, LazyAssertedCast(chars.size() - 1), nullptr,
                       chars.data());
  printf_stderr("GetShaderSource() ->\n%s\n", chars.data());
#endif

  return false;
}

// -

WebGLExtensionBlendMinMax::WebGLExtensionBlendMinMax(WebGLContext* webgl)
    : WebGLExtensionBase(webgl) {
  MOZ_ASSERT(IsSupported(webgl), "Don't construct extension if unsupported.");
}

bool WebGLExtensionBlendMinMax::IsSupported(const WebGLContext* webgl) {
  if (webgl->IsWebGL2()) return false;

  return webgl->GL()->IsSupported(gl::GLFeature::blend_minmax);
}

// -

WebGLExtensionColorBufferFloat::WebGLExtensionColorBufferFloat(
    WebGLContext* webgl)
    : WebGLExtensionBase(webgl) {
  MOZ_ASSERT(IsSupported(webgl), "Don't construct extension if unsupported.");
  SetRenderable(webgl::FormatRenderableState::Implicit(
      WebGLExtensionID::WEBGL_color_buffer_float));
}

void WebGLExtensionColorBufferFloat::SetRenderable(
    const webgl::FormatRenderableState& state) {
  auto& fua = mContext->mFormatUsage;

  auto fnUpdateUsage = [&](GLenum sizedFormat,
                           webgl::EffectiveFormat effFormat) {
    auto usage = fua->EditUsage(effFormat);
    usage->SetRenderable(state);
    fua->AllowRBFormat(sizedFormat, usage);
  };

#define FOO(x) fnUpdateUsage(LOCAL_GL_##x, webgl::EffectiveFormat::x)

  // The extension doesn't actually add RGB32F; only RGBA32F.
  FOO(RGBA32F);

#undef FOO
}

void WebGLExtensionColorBufferFloat::OnSetExplicit() {
  SetRenderable(webgl::FormatRenderableState::Explicit());
}

bool WebGLExtensionColorBufferFloat::IsSupported(const WebGLContext* webgl) {
  if (webgl->IsWebGL2()) return false;

  const auto& gl = webgl->gl;
  return gl->IsSupported(gl::GLFeature::renderbuffer_color_float) &&
         gl->IsSupported(gl::GLFeature::frag_color_float);
}

// -

WebGLExtensionColorBufferHalfFloat::WebGLExtensionColorBufferHalfFloat(
    WebGLContext* webgl)
    : WebGLExtensionBase(webgl) {
  MOZ_ASSERT(IsSupported(webgl), "Don't construct extension if unsupported.");
  SetRenderable(webgl::FormatRenderableState::Implicit(
      WebGLExtensionID::EXT_color_buffer_half_float));
}

void WebGLExtensionColorBufferHalfFloat::SetRenderable(
    const webgl::FormatRenderableState& state) {
  auto& fua = mContext->mFormatUsage;

  auto fnUpdateUsage = [&](GLenum sizedFormat, webgl::EffectiveFormat effFormat,
                           const bool renderable) {
    auto usage = fua->EditUsage(effFormat);
    if (renderable) {
      usage->SetRenderable(state);
    }
    fua->AllowRBFormat(sizedFormat, usage, renderable);
  };

#define FOO(x, y) fnUpdateUsage(LOCAL_GL_##x, webgl::EffectiveFormat::x, y)

  FOO(RGBA16F, true);
  FOO(RGB16F, false);  // It's not required, thus not portable. (Also there's a
                       // wicked driver bug on Mac+Intel)

#undef FOO
}

void WebGLExtensionColorBufferHalfFloat::OnSetExplicit() {
  SetRenderable(webgl::FormatRenderableState::Explicit());
}

bool WebGLExtensionColorBufferHalfFloat::IsSupported(
    const WebGLContext* webgl) {
  if (webgl->IsWebGL2()) return false;

  const auto& gl = webgl->gl;
  return gl->IsSupported(gl::GLFeature::renderbuffer_color_half_float) &&
         gl->IsSupported(gl::GLFeature::frag_color_float);
}

// -

WebGLExtensionCompressedTextureASTC::WebGLExtensionCompressedTextureASTC(
    WebGLContext* webgl)
    : WebGLExtensionBase(webgl) {
  MOZ_ASSERT(IsSupported(webgl), "Don't construct extension if unsupported.");

  RefPtr<WebGLContext> webgl_ = webgl;  // Bug 1201275
  const auto fnAdd = [&webgl_](GLenum sizedFormat,
                               webgl::EffectiveFormat effFormat) {
    auto& fua = webgl_->mFormatUsage;

    auto usage = fua->EditUsage(effFormat);
    usage->isFilterable = true;
    fua->AllowSizedTexFormat(sizedFormat, usage);
  };

#define FOO(x) LOCAL_GL_##x, webgl::EffectiveFormat::x

  fnAdd(FOO(COMPRESSED_RGBA_ASTC_4x4_KHR));
  fnAdd(FOO(COMPRESSED_RGBA_ASTC_5x4_KHR));
  fnAdd(FOO(COMPRESSED_RGBA_ASTC_5x5_KHR));
  fnAdd(FOO(COMPRESSED_RGBA_ASTC_6x5_KHR));
  fnAdd(FOO(COMPRESSED_RGBA_ASTC_6x6_KHR));
  fnAdd(FOO(COMPRESSED_RGBA_ASTC_8x5_KHR));
  fnAdd(FOO(COMPRESSED_RGBA_ASTC_8x6_KHR));
  fnAdd(FOO(COMPRESSED_RGBA_ASTC_8x8_KHR));
  fnAdd(FOO(COMPRESSED_RGBA_ASTC_10x5_KHR));
  fnAdd(FOO(COMPRESSED_RGBA_ASTC_10x6_KHR));
  fnAdd(FOO(COMPRESSED_RGBA_ASTC_10x8_KHR));
  fnAdd(FOO(COMPRESSED_RGBA_ASTC_10x10_KHR));
  fnAdd(FOO(COMPRESSED_RGBA_ASTC_12x10_KHR));
  fnAdd(FOO(COMPRESSED_RGBA_ASTC_12x12_KHR));

  fnAdd(FOO(COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR));
  fnAdd(FOO(COMPRESSED_SRGB8_ALPHA8_ASTC_5x4_KHR));
  fnAdd(FOO(COMPRESSED_SRGB8_ALPHA8_ASTC_5x5_KHR));
  fnAdd(FOO(COMPRESSED_SRGB8_ALPHA8_ASTC_6x5_KHR));
  fnAdd(FOO(COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR));
  fnAdd(FOO(COMPRESSED_SRGB8_ALPHA8_ASTC_8x5_KHR));
  fnAdd(FOO(COMPRESSED_SRGB8_ALPHA8_ASTC_8x6_KHR));
  fnAdd(FOO(COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR));
  fnAdd(FOO(COMPRESSED_SRGB8_ALPHA8_ASTC_10x5_KHR));
  fnAdd(FOO(COMPRESSED_SRGB8_ALPHA8_ASTC_10x6_KHR));
  fnAdd(FOO(COMPRESSED_SRGB8_ALPHA8_ASTC_10x8_KHR));
  fnAdd(FOO(COMPRESSED_SRGB8_ALPHA8_ASTC_10x10_KHR));
  fnAdd(FOO(COMPRESSED_SRGB8_ALPHA8_ASTC_12x10_KHR));
  fnAdd(FOO(COMPRESSED_SRGB8_ALPHA8_ASTC_12x12_KHR));

#undef FOO
}

bool WebGLExtensionCompressedTextureASTC::IsSupported(
    const WebGLContext* webgl) {
  gl::GLContext* gl = webgl->GL();
  return gl->IsExtensionSupported(
      gl::GLContext::KHR_texture_compression_astc_ldr);
}

// -

WebGLExtensionCompressedTextureBPTC::WebGLExtensionCompressedTextureBPTC(
    WebGLContext* const webgl)
    : WebGLExtensionBase(webgl) {
  MOZ_ASSERT(IsSupported(webgl), "Don't construct extension if unsupported.");

  auto& fua = webgl->mFormatUsage;

  const auto fnAdd = [&](const GLenum sizedFormat,
                         const webgl::EffectiveFormat effFormat) {
    auto usage = fua->EditUsage(effFormat);
    usage->isFilterable = true;
    fua->AllowSizedTexFormat(sizedFormat, usage);
  };

#define _(X) LOCAL_GL_##X, webgl::EffectiveFormat::X

  fnAdd(_(COMPRESSED_RGBA_BPTC_UNORM));
  fnAdd(_(COMPRESSED_SRGB_ALPHA_BPTC_UNORM));
  fnAdd(_(COMPRESSED_RGB_BPTC_SIGNED_FLOAT));
  fnAdd(_(COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT));

#undef _
}

bool WebGLExtensionCompressedTextureBPTC::IsSupported(
    const WebGLContext* const webgl) {
  return webgl->gl->IsSupported(gl::GLFeature::texture_compression_bptc);
}

// -

WebGLExtensionCompressedTextureES3::WebGLExtensionCompressedTextureES3(
    WebGLContext* webgl)
    : WebGLExtensionBase(webgl) {
  // GLES 3.0.4, p147, table 3.19
  // GLES 3.0.4, p286+, $C.1 "ETC Compressed Texture Image Formats"
  // Note that all compressed texture formats are filterable:
  // GLES 3.0.4 p161:
  // "[A] texture is complete unless any of the following conditions hold true:
  //  [...]
  //  * The effective internal format specified for the texture arrays is a
  //    sized internal color format that is not texture-filterable (see table
  //    3.13) and [the mag filter requires filtering]."
  // Compressed formats are not sized internal color formats, and indeed they
  // are not listed in table 3.13.

  RefPtr<WebGLContext> webgl_ = webgl;  // Bug 1201275
  const auto fnAdd = [&webgl_](GLenum sizedFormat,
                               webgl::EffectiveFormat effFormat) {
    auto& fua = webgl_->mFormatUsage;

    auto usage = fua->EditUsage(effFormat);
    usage->isFilterable = true;
    fua->AllowSizedTexFormat(sizedFormat, usage);
  };

#define FOO(x) LOCAL_GL_##x, webgl::EffectiveFormat::x

  fnAdd(FOO(COMPRESSED_R11_EAC));
  fnAdd(FOO(COMPRESSED_SIGNED_R11_EAC));
  fnAdd(FOO(COMPRESSED_RG11_EAC));
  fnAdd(FOO(COMPRESSED_SIGNED_RG11_EAC));
  fnAdd(FOO(COMPRESSED_RGB8_ETC2));
  fnAdd(FOO(COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2));
  fnAdd(FOO(COMPRESSED_RGBA8_ETC2_EAC));

  // sRGB support is manadatory in GL 4.3 and GL ES 3.0, which are the only
  // versions to support ETC2.
  fnAdd(FOO(COMPRESSED_SRGB8_ALPHA8_ETC2_EAC));
  fnAdd(FOO(COMPRESSED_SRGB8_ETC2));
  fnAdd(FOO(COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2));

#undef FOO
}

// -

WebGLExtensionCompressedTextureETC1::WebGLExtensionCompressedTextureETC1(
    WebGLContext* webgl)
    : WebGLExtensionBase(webgl) {
  RefPtr<WebGLContext> webgl_ = webgl;  // Bug 1201275
  const auto fnAdd = [&webgl_](GLenum sizedFormat,
                               webgl::EffectiveFormat effFormat) {
    auto& fua = webgl_->mFormatUsage;

    auto usage = fua->EditUsage(effFormat);
    usage->isFilterable = true;
    fua->AllowSizedTexFormat(sizedFormat, usage);
  };

#define FOO(x) LOCAL_GL_##x, webgl::EffectiveFormat::x

  fnAdd(FOO(ETC1_RGB8_OES));

#undef FOO
}

// -

WebGLExtensionCompressedTexturePVRTC::WebGLExtensionCompressedTexturePVRTC(
    WebGLContext* webgl)
    : WebGLExtensionBase(webgl) {
  RefPtr<WebGLContext> webgl_ = webgl;  // Bug 1201275
  const auto fnAdd = [&webgl_](GLenum sizedFormat,
                               webgl::EffectiveFormat effFormat) {
    auto& fua = webgl_->mFormatUsage;

    auto usage = fua->EditUsage(effFormat);
    usage->isFilterable = true;
    fua->AllowSizedTexFormat(sizedFormat, usage);
  };

#define FOO(x) LOCAL_GL_##x, webgl::EffectiveFormat::x

  fnAdd(FOO(COMPRESSED_RGB_PVRTC_4BPPV1));
  fnAdd(FOO(COMPRESSED_RGB_PVRTC_2BPPV1));
  fnAdd(FOO(COMPRESSED_RGBA_PVRTC_4BPPV1));
  fnAdd(FOO(COMPRESSED_RGBA_PVRTC_2BPPV1));

#undef FOO
}

// -

WebGLExtensionCompressedTextureRGTC::WebGLExtensionCompressedTextureRGTC(
    WebGLContext* const webgl)
    : WebGLExtensionBase(webgl) {
  MOZ_ASSERT(IsSupported(webgl), "Don't construct extension if unsupported.");

  auto& fua = webgl->mFormatUsage;

  const auto fnAdd = [&](const GLenum sizedFormat,
                         const webgl::EffectiveFormat effFormat) {
    auto usage = fua->EditUsage(effFormat);
    usage->isFilterable = true;
    fua->AllowSizedTexFormat(sizedFormat, usage);
  };

#define _(X) LOCAL_GL_##X, webgl::EffectiveFormat::X

  fnAdd(_(COMPRESSED_RED_RGTC1));
  fnAdd(_(COMPRESSED_SIGNED_RED_RGTC1));
  fnAdd(_(COMPRESSED_RG_RGTC2));
  fnAdd(_(COMPRESSED_SIGNED_RG_RGTC2));

#undef _
}

bool WebGLExtensionCompressedTextureRGTC::IsSupported(
    const WebGLContext* const webgl) {
  return webgl->gl->IsSupported(gl::GLFeature::texture_compression_rgtc);
}

// -

WebGLExtensionCompressedTextureS3TC::WebGLExtensionCompressedTextureS3TC(
    WebGLContext* webgl)
    : WebGLExtensionBase(webgl) {
  RefPtr<WebGLContext> webgl_ = webgl;  // Bug 1201275
  const auto fnAdd = [&webgl_](GLenum sizedFormat,
                               webgl::EffectiveFormat effFormat) {
    auto& fua = webgl_->mFormatUsage;

    auto usage = fua->EditUsage(effFormat);
    usage->isFilterable = true;
    fua->AllowSizedTexFormat(sizedFormat, usage);
  };

#define FOO(x) LOCAL_GL_##x, webgl::EffectiveFormat::x

  fnAdd(FOO(COMPRESSED_RGB_S3TC_DXT1_EXT));
  fnAdd(FOO(COMPRESSED_RGBA_S3TC_DXT1_EXT));
  fnAdd(FOO(COMPRESSED_RGBA_S3TC_DXT3_EXT));
  fnAdd(FOO(COMPRESSED_RGBA_S3TC_DXT5_EXT));

#undef FOO
}

bool WebGLExtensionCompressedTextureS3TC::IsSupported(
    const WebGLContext* webgl) {
  gl::GLContext* gl = webgl->GL();
  if (gl->IsExtensionSupported(gl::GLContext::EXT_texture_compression_s3tc))
    return true;

  return gl->IsExtensionSupported(
             gl::GLContext::EXT_texture_compression_dxt1) &&
         gl->IsExtensionSupported(
             gl::GLContext::ANGLE_texture_compression_dxt3) &&
         gl->IsExtensionSupported(
             gl::GLContext::ANGLE_texture_compression_dxt5);
}

// -

WebGLExtensionCompressedTextureS3TC_SRGB::
    WebGLExtensionCompressedTextureS3TC_SRGB(WebGLContext* webgl)
    : WebGLExtensionBase(webgl) {
  RefPtr<WebGLContext> webgl_ = webgl;  // Bug 1201275
  const auto fnAdd = [&webgl_](GLenum sizedFormat,
                               webgl::EffectiveFormat effFormat) {
    auto& fua = webgl_->mFormatUsage;

    auto usage = fua->EditUsage(effFormat);
    usage->isFilterable = true;
    fua->AllowSizedTexFormat(sizedFormat, usage);
  };

#define FOO(x) LOCAL_GL_##x, webgl::EffectiveFormat::x

  fnAdd(FOO(COMPRESSED_SRGB_S3TC_DXT1_EXT));
  fnAdd(FOO(COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT));
  fnAdd(FOO(COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT));
  fnAdd(FOO(COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT));

#undef FOO
}

bool WebGLExtensionCompressedTextureS3TC_SRGB::IsSupported(
    const WebGLContext* webgl) {
  gl::GLContext* gl = webgl->GL();
  if (gl->IsGLES())
    return gl->IsExtensionSupported(
        gl::GLContext::EXT_texture_compression_s3tc_srgb);

  // Desktop GL is more complicated: It's EXT_texture_sRGB, when
  // EXT_texture_compression_s3tc is supported, that enables srgb+s3tc.
  return gl->IsExtensionSupported(gl::GLContext::EXT_texture_sRGB) &&
         gl->IsExtensionSupported(gl::GLContext::EXT_texture_compression_s3tc);
}

// -

WebGLExtensionDepthClamp::WebGLExtensionDepthClamp(WebGLContext* const webgl)
    : WebGLExtensionBase(webgl) {
  webgl->mIsEnabledMapKeys.insert(LOCAL_GL_DEPTH_CLAMP);
}

// -

WebGLExtensionDepthTexture::WebGLExtensionDepthTexture(
    WebGLContext* const webgl)
    : WebGLExtensionBase(webgl) {
  auto& fua = webgl->mFormatUsage;

  const auto fnAdd = [&fua](webgl::EffectiveFormat effFormat,
                            GLenum unpackFormat, GLenum unpackType) {
    auto usage = fua->EditUsage(effFormat);
    MOZ_ASSERT(usage->isFilterable);
    MOZ_ASSERT(usage->IsRenderable());

    const webgl::PackingInfo pi = {unpackFormat, unpackType};
    const webgl::DriverUnpackInfo dui = {unpackFormat, unpackFormat,
                                         unpackType};
    fua->AddTexUnpack(usage, pi, dui);
    fua->AllowUnsizedTexFormat(pi, usage);
  };

  fnAdd(webgl::EffectiveFormat::DEPTH_COMPONENT16, LOCAL_GL_DEPTH_COMPONENT,
        LOCAL_GL_UNSIGNED_SHORT);
  fnAdd(webgl::EffectiveFormat::DEPTH_COMPONENT24, LOCAL_GL_DEPTH_COMPONENT,
        LOCAL_GL_UNSIGNED_INT);
  fnAdd(webgl::EffectiveFormat::DEPTH24_STENCIL8, LOCAL_GL_DEPTH_STENCIL,
        LOCAL_GL_UNSIGNED_INT_24_8);
}

bool WebGLExtensionDepthTexture::IsSupported(const WebGLContext* const webgl) {
  if (webgl->IsWebGL2()) return false;

  // WEBGL_depth_texture supports DEPTH_STENCIL textures
  const auto& gl = webgl->gl;
  if (!gl->IsSupported(gl::GLFeature::packed_depth_stencil)) return false;

  return gl->IsSupported(gl::GLFeature::depth_texture) ||
         gl->IsExtensionSupported(gl::GLContext::ANGLE_depth_texture);
}

// -

WebGLExtensionDisjointTimerQuery::WebGLExtensionDisjointTimerQuery(
    WebGLContext* webgl)
    : WebGLExtensionBase(webgl) {
  MOZ_ASSERT(IsSupported(webgl), "Don't construct extension if unsupported.");
}

bool WebGLExtensionDisjointTimerQuery::IsSupported(
    const WebGLContext* const webgl) {
  if (!StaticPrefs::webgl_enable_privileged_extensions()) return false;

  gl::GLContext* gl = webgl->GL();
  return gl->IsSupported(gl::GLFeature::query_objects) &&
         gl->IsSupported(gl::GLFeature::get_query_object_i64v) &&
         gl->IsSupported(
             gl::GLFeature::query_counter);  // provides GL_TIMESTAMP
}

// -

WebGLExtensionDrawBuffers::WebGLExtensionDrawBuffers(WebGLContext* webgl)
    : WebGLExtensionBase(webgl) {
  MOZ_ASSERT(IsSupported(webgl), "Don't construct extension if unsupported.");
}

bool WebGLExtensionDrawBuffers::IsSupported(const WebGLContext* webgl) {
  if (!webgl->mIsSupportedCache_DrawBuffers) {
    webgl->mIsSupportedCache_DrawBuffers = [&]() {
      gl::GLContext* const gl = webgl->GL();

      if (webgl->IsWebGL2()) return false;
      if (!gl->IsSupported(gl::GLFeature::draw_buffers)) return false;

      if (gl->IsGLES() && gl->Version() >= 300) {
        // ANGLE's shader translator can't translate ESSL1 exts to ESSL3. (bug
        // 1524804)
        // The spec (and some implementations of ES3) don't require support for
        // any extensions in ESSL 100, but an implementation can choose to
        // support them anyway. So let's check!
        const bool ok = TestShaderCompile(gl, LOCAL_GL_FRAGMENT_SHADER, R"(
#extension GL_EXT_draw_buffers: require
void main() {}
)");
        if (!ok) return false;
      }

      return true;
    }();
  }

  return *webgl->mIsSupportedCache_DrawBuffers;
}

// -

WebGLExtensionExplicitPresent::WebGLExtensionExplicitPresent(
    WebGLContext* const webgl)
    : WebGLExtensionBase(webgl) {
  if (!IsSupported(webgl)) {
    NS_WARNING(
        "Constructing WebGLExtensionExplicitPresent but IsSupported() is "
        "false!");
    // This was previously an assert, but it seems like we get races against
    // StaticPrefs changes/initialization?
  }
}

bool WebGLExtensionExplicitPresent::IsSupported(
    const WebGLContext* const webgl) {
  return StaticPrefs::webgl_enable_draft_extensions();
}

// -

WebGLExtensionEXTColorBufferFloat::WebGLExtensionEXTColorBufferFloat(
    WebGLContext* webgl)
    : WebGLExtensionBase(webgl) {
  MOZ_ASSERT(IsSupported(webgl), "Don't construct extension if unsupported.");

  auto& fua = webgl->mFormatUsage;

  auto fnUpdateUsage = [&fua](GLenum sizedFormat,
                              webgl::EffectiveFormat effFormat) {
    auto usage = fua->EditUsage(effFormat);
    usage->SetRenderable();
    fua->AllowRBFormat(sizedFormat, usage);
  };

#define FOO(x) fnUpdateUsage(LOCAL_GL_##x, webgl::EffectiveFormat::x)

  FOO(R16F);
  FOO(RG16F);
  FOO(RGBA16F);

  FOO(R32F);
  FOO(RG32F);
  FOO(RGBA32F);

  FOO(R11F_G11F_B10F);

#undef FOO
}

/*static*/
bool WebGLExtensionEXTColorBufferFloat::IsSupported(const WebGLContext* webgl) {
  if (!webgl->IsWebGL2()) return false;

  const gl::GLContext* gl = webgl->GL();
  return gl->IsSupported(gl::GLFeature::EXT_color_buffer_float);
}

// -

WebGLExtensionFBORenderMipmap::WebGLExtensionFBORenderMipmap(
    WebGLContext* const webgl)
    : WebGLExtensionBase(webgl) {
  MOZ_ASSERT(IsSupported(webgl), "Don't construct extension if unsupported.");
}

bool WebGLExtensionFBORenderMipmap::IsSupported(
    const WebGLContext* const webgl) {
  if (webgl->IsWebGL2()) return false;

  const auto& gl = webgl->gl;
  if (!gl->IsGLES()) return true;
  if (gl->Version() >= 300) return true;
  return gl->IsExtensionSupported(gl::GLContext::OES_fbo_render_mipmap);
}

// -

WebGLExtensionFloatBlend::WebGLExtensionFloatBlend(WebGLContext* const webgl)
    : WebGLExtensionBase(webgl) {
  MOZ_ASSERT(IsSupported(webgl), "Don't construct extension if unsupported.");
}

bool WebGLExtensionFloatBlend::IsSupported(const WebGLContext* const webgl) {
  if (!WebGLExtensionColorBufferFloat::IsSupported(webgl) &&
      !WebGLExtensionEXTColorBufferFloat::IsSupported(webgl))
    return false;

  const auto& gl = webgl->gl;
  if (!gl->IsGLES() && gl->Version() >= 300) return true;
  if (gl->IsGLES() && gl->Version() >= 320) return true;
  return gl->IsExtensionSupported(gl::GLContext::EXT_float_blend);
}

// -

WebGLExtensionFragDepth::WebGLExtensionFragDepth(WebGLContext* webgl)
    : WebGLExtensionBase(webgl) {
  MOZ_ASSERT(IsSupported(webgl), "Don't construct extension if unsupported.");
}

bool WebGLExtensionFragDepth::IsSupported(const WebGLContext* const webgl) {
  if (!webgl->mIsSupportedCache_FragDepth) {
    webgl->mIsSupportedCache_FragDepth = [&]() {
      gl::GLContext* const gl = webgl->GL();

      if (webgl->IsWebGL2()) return false;
      if (!gl->IsSupported(gl::GLFeature::frag_depth)) return false;

      if (gl->IsGLES() && gl->Version() >= 300) {
        // ANGLE's shader translator can't translate ESSL1 exts to ESSL3. (bug
        // 1524804)
        // The spec (and some implementations of ES3) don't require support for
        // any extensions in ESSL 100, but an implementation can choose to
        // support them anyway. So let's check!
        const bool ok = TestShaderCompile(gl, LOCAL_GL_FRAGMENT_SHADER, R"(
#extension GL_EXT_frag_depth: require
void main() {}
)");
        if (!ok) return false;
      }

      return true;
    }();
  }

  return *webgl->mIsSupportedCache_FragDepth;
}

// -

WebGLExtensionPolygonOffsetClamp::WebGLExtensionPolygonOffsetClamp(
    WebGLContext* webgl)
    : WebGLExtensionBase(webgl) {
  MOZ_ASSERT(IsSupported(webgl), "Don't construct extension if unsupported.");
}

bool WebGLExtensionPolygonOffsetClamp::IsSupported(
    const WebGLContext* const webgl) {
  return webgl->GL()->IsSupported(gl::GLFeature::polygon_offset_clamp);
}

WebGLExtensionClipControl::WebGLExtensionClipControl(WebGLContext* webgl)
    : WebGLExtensionBase(webgl) {
  MOZ_ASSERT(IsSupported(webgl), "Don't construct extension if unsupported.");
}

bool WebGLExtensionClipControl::IsSupported(const WebGLContext* const webgl) {
  return webgl->GL()->IsSupported(gl::GLFeature::clip_control);
}

// -

WebGLExtensionInstancedArrays::WebGLExtensionInstancedArrays(
    WebGLContext* webgl)
    : WebGLExtensionBase(webgl) {
  MOZ_ASSERT(IsSupported(webgl), "Don't construct extension if unsupported.");
}

bool WebGLExtensionInstancedArrays::IsSupported(const WebGLContext* webgl) {
  if (webgl->IsWebGL2()) return false;

  gl::GLContext* gl = webgl->GL();
  return gl->IsSupported(gl::GLFeature::draw_instanced) &&
         gl->IsSupported(gl::GLFeature::instanced_arrays);
}

// -

WebGLExtensionMultiview::WebGLExtensionMultiview(WebGLContext* const webgl)
    : WebGLExtensionBase(webgl) {
  MOZ_ASSERT(IsSupported(webgl), "Don't construct extension if unsupported.");
}

bool WebGLExtensionMultiview::IsSupported(const WebGLContext* const webgl) {
  if (!webgl->IsWebGL2()) return false;

  const auto& gl = webgl->gl;
  return gl->IsSupported(gl::GLFeature::multiview);
}

// -

WebGLExtensionShaderTextureLod::WebGLExtensionShaderTextureLod(
    WebGLContext* webgl)
    : WebGLExtensionBase(webgl) {
  MOZ_ASSERT(IsSupported(webgl), "Don't construct extension if unsupported.");
}

bool WebGLExtensionShaderTextureLod::IsSupported(const WebGLContext* webgl) {
  if (!webgl->mIsSupportedCache_ShaderTextureLod) {
    webgl->mIsSupportedCache_ShaderTextureLod = [&]() {
      gl::GLContext* const gl = webgl->GL();

      if (webgl->IsWebGL2()) return false;
      if (!gl->IsSupported(gl::GLFeature::shader_texture_lod)) return false;

      if (gl->IsGLES() && gl->Version() >= 300) {
        // ANGLE's shader translator doesn't yet translate
        // WebGL1+EXT_shader_texture_lod to ES3. (Bug 1491221)
        // The spec (and some implementations of ES3) don't require support for
        // any extensions in ESSL 100, but an implementation can choose to
        // support them anyway. So let's check!
        const bool ok = TestShaderCompile(gl, LOCAL_GL_FRAGMENT_SHADER, R"(
#extension GL_EXT_shader_texture_lod: require
void main() {}
)");
        if (!ok) return false;
      }

      return true;
    }();
  }
  return *webgl->mIsSupportedCache_ShaderTextureLod;
}

// -

WebGLExtensionSRGB::WebGLExtensionSRGB(WebGLContext* webgl)
    : WebGLExtensionBase(webgl) {
  MOZ_ASSERT(IsSupported(webgl), "Don't construct extension if unsupported.");

  gl::GLContext* gl = webgl->GL();
  if (!gl->IsGLES()) {
    // Desktop OpenGL requires the following to be enabled in order to
    // support sRGB operations on framebuffers.
    gl->fEnable(LOCAL_GL_FRAMEBUFFER_SRGB_EXT);
  }

  auto& fua = webgl->mFormatUsage;

  RefPtr<gl::GLContext> gl_ = gl;  // Bug 1201275
  const auto fnAdd = [&fua, &gl_](webgl::EffectiveFormat effFormat,
                                  GLenum format, GLenum desktopUnpackFormat) {
    auto usage = fua->EditUsage(effFormat);
    usage->isFilterable = true;

    webgl::DriverUnpackInfo dui = {format, format, LOCAL_GL_UNSIGNED_BYTE};
    const auto pi = dui.ToPacking();

    if (!gl_->IsGLES()) dui.unpackFormat = desktopUnpackFormat;

    fua->AddTexUnpack(usage, pi, dui);

    fua->AllowUnsizedTexFormat(pi, usage);
  };

  fnAdd(webgl::EffectiveFormat::SRGB8, LOCAL_GL_SRGB, LOCAL_GL_RGB);
  fnAdd(webgl::EffectiveFormat::SRGB8_ALPHA8, LOCAL_GL_SRGB_ALPHA,
        LOCAL_GL_RGBA);

  auto usage = fua->EditUsage(webgl::EffectiveFormat::SRGB8_ALPHA8);
  usage->SetRenderable();
  fua->AllowRBFormat(LOCAL_GL_SRGB8_ALPHA8, usage);
}

bool WebGLExtensionSRGB::IsSupported(const WebGLContext* const webgl) {
  if (webgl->IsWebGL2()) return false;

  return webgl->gl->IsSupported(gl::GLFeature::sRGB);
}

// -

WebGLExtensionTextureFloat::WebGLExtensionTextureFloat(WebGLContext* webgl)
    : WebGLExtensionBase(webgl) {
  MOZ_ASSERT(IsSupported(webgl));

  auto& fua = webgl->mFormatUsage;
  gl::GLContext* gl = webgl->GL();

  webgl::PackingInfo pi;
  webgl::DriverUnpackInfo dui;
  const GLint* swizzle = nullptr;

  const auto fnAdd = [&](webgl::EffectiveFormat effFormat) {
    MOZ_ASSERT_IF(swizzle, gl->IsSupported(gl::GLFeature::texture_swizzle));

    auto usage = fua->EditUsage(effFormat);
    usage->textureSwizzleRGBA = swizzle;
    fua->AddTexUnpack(usage, pi, dui);

    fua->AllowUnsizedTexFormat(pi, usage);
  };

  bool useSizedFormats = true;
  const bool hasSizedLegacyFormats = gl->IsCompatibilityProfile();
  if (gl->IsGLES() && gl->Version() < 300) {
    useSizedFormats = false;
  }

  ////////////////

  pi = {LOCAL_GL_RGBA, LOCAL_GL_FLOAT};
  dui = {pi.format, pi.format, pi.type};
  swizzle = nullptr;
  if (useSizedFormats || gl->IsExtensionSupported(
                             gl::GLContext::CHROMIUM_color_buffer_float_rgba)) {
    // ANGLE only exposes renderable RGBA32F via
    // CHROMIUM_color_buffer_float_rgba, which uses sized formats.
    dui.internalFormat = LOCAL_GL_RGBA32F;
  }
  fnAdd(webgl::EffectiveFormat::RGBA32F);

  //////

  pi = {LOCAL_GL_RGB, LOCAL_GL_FLOAT};
  dui = {pi.format, pi.format, pi.type};
  swizzle = nullptr;
  if (useSizedFormats) {
    dui.internalFormat = LOCAL_GL_RGB32F;
  }
  fnAdd(webgl::EffectiveFormat::RGB32F);

  //////

  pi = {LOCAL_GL_LUMINANCE, LOCAL_GL_FLOAT};
  dui = {pi.format, pi.format, pi.type};
  swizzle = nullptr;
  if (useSizedFormats) {
    if (hasSizedLegacyFormats) {
      dui.internalFormat = LOCAL_GL_LUMINANCE32F_ARB;
    } else {
      dui.internalFormat = LOCAL_GL_R32F;
      dui.unpackFormat = LOCAL_GL_RED;
      swizzle = webgl::FormatUsageInfo::kLuminanceSwizzleRGBA;
    }
  }
  fnAdd(webgl::EffectiveFormat::Luminance32F);

  //////

  pi = {LOCAL_GL_ALPHA, LOCAL_GL_FLOAT};
  dui = {pi.format, pi.format, pi.type};
  swizzle = nullptr;
  if (useSizedFormats) {
    if (hasSizedLegacyFormats) {
      dui.internalFormat = LOCAL_GL_ALPHA32F_ARB;
    } else {
      dui.internalFormat = LOCAL_GL_R32F;
      dui.unpackFormat = LOCAL_GL_RED;
      swizzle = webgl::FormatUsageInfo::kAlphaSwizzleRGBA;
    }
  }
  fnAdd(webgl::EffectiveFormat::Alpha32F);

  //////

  pi = {LOCAL_GL_LUMINANCE_ALPHA, LOCAL_GL_FLOAT};
  dui = {pi.format, pi.format, pi.type};
  swizzle = nullptr;
  if (useSizedFormats) {
    if (hasSizedLegacyFormats) {
      dui.internalFormat = LOCAL_GL_LUMINANCE_ALPHA32F_ARB;
    } else {
      dui.internalFormat = LOCAL_GL_RG32F;
      dui.unpackFormat = LOCAL_GL_RG;
      swizzle = webgl::FormatUsageInfo::kLumAlphaSwizzleRGBA;
    }
  }
  fnAdd(webgl::EffectiveFormat::Luminance32FAlpha32F);
}

bool WebGLExtensionTextureFloat::IsSupported(const WebGLContext* webgl) {
  if (webgl->IsWebGL2()) return false;

  gl::GLContext* gl = webgl->GL();
  if (!gl->IsSupported(gl::GLFeature::texture_float)) return false;

  const bool needsSwizzle = gl->IsCoreProfile();
  const bool hasSwizzle = gl->IsSupported(gl::GLFeature::texture_swizzle);
  if (needsSwizzle && !hasSwizzle) return false;

  return true;
}

// -

WebGLExtensionTextureFloatLinear::WebGLExtensionTextureFloatLinear(
    WebGLContext* webgl)
    : WebGLExtensionBase(webgl) {
  auto& fua = webgl->mFormatUsage;

  fua->EditUsage(webgl::EffectiveFormat::RGBA32F)->isFilterable = true;
  fua->EditUsage(webgl::EffectiveFormat::RGB32F)->isFilterable = true;

  if (webgl->IsWebGL2()) {
    fua->EditUsage(webgl::EffectiveFormat::R32F)->isFilterable = true;
    fua->EditUsage(webgl::EffectiveFormat::RG32F)->isFilterable = true;
  } else {
    fua->EditUsage(webgl::EffectiveFormat::Luminance32FAlpha32F)->isFilterable =
        true;
    fua->EditUsage(webgl::EffectiveFormat::Luminance32F)->isFilterable = true;
    fua->EditUsage(webgl::EffectiveFormat::Alpha32F)->isFilterable = true;
  }
}

// -

WebGLExtensionTextureHalfFloat::WebGLExtensionTextureHalfFloat(
    WebGLContext* webgl)
    : WebGLExtensionBase(webgl) {
  auto& fua = webgl->mFormatUsage;
  gl::GLContext* gl = webgl->GL();

  webgl::PackingInfo pi;
  webgl::DriverUnpackInfo dui;
  const GLint* swizzle = nullptr;

  const auto fnAdd = [&](webgl::EffectiveFormat effFormat) {
    MOZ_ASSERT_IF(swizzle, gl->IsSupported(gl::GLFeature::texture_swizzle));

    auto usage = fua->EditUsage(effFormat);
    usage->textureSwizzleRGBA = swizzle;
    fua->AddTexUnpack(usage, pi, dui);

    fua->AllowUnsizedTexFormat(pi, usage);
  };

  bool useSizedFormats = true;
  const bool hasSizedLegacyFormats = gl->IsCompatibilityProfile();
  if (gl->IsGLES() && gl->Version() < 300) {
    useSizedFormats = false;
  }

  GLenum driverUnpackType = LOCAL_GL_HALF_FLOAT;
  if (!gl->IsSupported(gl::GLFeature::texture_half_float)) {
    MOZ_ASSERT(gl->IsExtensionSupported(gl::GLContext::OES_texture_half_float));
    driverUnpackType = LOCAL_GL_HALF_FLOAT_OES;
  }

  ////////////////

  pi = {LOCAL_GL_RGBA, LOCAL_GL_HALF_FLOAT_OES};
  dui = {pi.format, pi.format, driverUnpackType};
  swizzle = nullptr;
  if (useSizedFormats) {
    dui.internalFormat = LOCAL_GL_RGBA16F;
  }
  fnAdd(webgl::EffectiveFormat::RGBA16F);

  //////

  pi = {LOCAL_GL_RGB, LOCAL_GL_HALF_FLOAT_OES};
  dui = {pi.format, pi.format, driverUnpackType};
  swizzle = nullptr;
  if (useSizedFormats) {
    dui.internalFormat = LOCAL_GL_RGB16F;
  }
  fnAdd(webgl::EffectiveFormat::RGB16F);

  //////

  pi = {LOCAL_GL_LUMINANCE, LOCAL_GL_HALF_FLOAT_OES};
  dui = {pi.format, pi.format, driverUnpackType};
  swizzle = nullptr;
  if (useSizedFormats) {
    if (hasSizedLegacyFormats) {
      dui.internalFormat = LOCAL_GL_LUMINANCE16F_ARB;
    } else {
      dui.internalFormat = LOCAL_GL_R16F;
      dui.unpackFormat = LOCAL_GL_RED;
      swizzle = webgl::FormatUsageInfo::kLuminanceSwizzleRGBA;
    }
  }
  fnAdd(webgl::EffectiveFormat::Luminance16F);

  //////

  pi = {LOCAL_GL_ALPHA, LOCAL_GL_HALF_FLOAT_OES};
  dui = {pi.format, pi.format, driverUnpackType};
  swizzle = nullptr;
  if (useSizedFormats) {
    if (hasSizedLegacyFormats) {
      dui.internalFormat = LOCAL_GL_ALPHA16F_ARB;
    } else {
      dui.internalFormat = LOCAL_GL_R16F;
      dui.unpackFormat = LOCAL_GL_RED;
      swizzle = webgl::FormatUsageInfo::kAlphaSwizzleRGBA;
    }
  }
  fnAdd(webgl::EffectiveFormat::Alpha16F);

  //////

  pi = {LOCAL_GL_LUMINANCE_ALPHA, LOCAL_GL_HALF_FLOAT_OES};
  dui = {pi.format, pi.format, driverUnpackType};
  swizzle = nullptr;
  if (useSizedFormats) {
    if (hasSizedLegacyFormats) {
      dui.internalFormat = LOCAL_GL_LUMINANCE_ALPHA16F_ARB;
    } else {
      dui.internalFormat = LOCAL_GL_RG16F;
      dui.unpackFormat = LOCAL_GL_RG;
      swizzle = webgl::FormatUsageInfo::kLumAlphaSwizzleRGBA;
    }
  }
  fnAdd(webgl::EffectiveFormat::Luminance16FAlpha16F);
}

bool WebGLExtensionTextureHalfFloat::IsSupported(const WebGLContext* webgl) {
  if (webgl->IsWebGL2()) return false;

  gl::GLContext* gl = webgl->GL();
  if (!gl->IsSupported(gl::GLFeature::texture_half_float) &&
      !gl->IsExtensionSupported(gl::GLContext::OES_texture_half_float)) {
    return false;
  }

  const bool needsSwizzle = gl->IsCoreProfile();
  const bool hasSwizzle = gl->IsSupported(gl::GLFeature::texture_swizzle);
  if (needsSwizzle && !hasSwizzle) return false;

  return true;
}

// -

WebGLExtensionTextureHalfFloatLinear::WebGLExtensionTextureHalfFloatLinear(
    WebGLContext* webgl)
    : WebGLExtensionBase(webgl) {
  MOZ_ASSERT(!webgl->IsWebGL2());
  auto& fua = webgl->mFormatUsage;

  fua->EditUsage(webgl::EffectiveFormat::RGBA16F)->isFilterable = true;
  fua->EditUsage(webgl::EffectiveFormat::RGB16F)->isFilterable = true;
  fua->EditUsage(webgl::EffectiveFormat::Luminance16FAlpha16F)->isFilterable =
      true;
  fua->EditUsage(webgl::EffectiveFormat::Luminance16F)->isFilterable = true;
  fua->EditUsage(webgl::EffectiveFormat::Alpha16F)->isFilterable = true;
}

// -

bool WebGLExtensionTextureNorm16::IsSupported(const WebGLContext* const webgl) {
  gl::GLContext* gl = webgl->GL();
  return webgl->IsWebGL2() && gl->IsSupported(gl::GLFeature::texture_norm16);
}

WebGLExtensionTextureNorm16::WebGLExtensionTextureNorm16(WebGLContext* webgl)
    : WebGLExtensionBase(webgl) {
  if (!IsSupported(webgl)) {
    NS_WARNING(
        "Constructing WebGLExtensionTextureNorm16 but IsSupported() is "
        "false!");
    // This was previously an assert, but it seems like we get races against
    // StaticPrefs changes/initialization?
  }

  auto& fua = *webgl->mFormatUsage;

  const auto fnAdd = [&](webgl::EffectiveFormat effFormat,
                         const bool renderable, const webgl::PackingInfo& pi) {
    auto& usage = *fua.EditUsage(effFormat);

    usage.isFilterable = true;

    const auto& format = *usage.format;

    const auto dui =
        webgl::DriverUnpackInfo{format.sizedFormat, pi.format, pi.type};
    fua.AddTexUnpack(&usage, pi, dui);

    fua.AllowSizedTexFormat(format.sizedFormat, &usage);

    if (renderable) {
      usage.SetRenderable();
      fua.AllowRBFormat(format.sizedFormat, &usage);
    }
  };

  fnAdd(webgl::EffectiveFormat::R16, true,
        {LOCAL_GL_RED, LOCAL_GL_UNSIGNED_SHORT});
  fnAdd(webgl::EffectiveFormat::RG16, true,
        {LOCAL_GL_RG, LOCAL_GL_UNSIGNED_SHORT});
  fnAdd(webgl::EffectiveFormat::RGB16, false,
        {LOCAL_GL_RGB, LOCAL_GL_UNSIGNED_SHORT});
  fnAdd(webgl::EffectiveFormat::RGBA16, true,
        {LOCAL_GL_RGBA, LOCAL_GL_UNSIGNED_SHORT});

  fnAdd(webgl::EffectiveFormat::R16_SNORM, false,
        {LOCAL_GL_RED, LOCAL_GL_SHORT});
  fnAdd(webgl::EffectiveFormat::RG16_SNORM, false,
        {LOCAL_GL_RG, LOCAL_GL_SHORT});
  fnAdd(webgl::EffectiveFormat::RGB16_SNORM, false,
        {LOCAL_GL_RGB, LOCAL_GL_SHORT});
  fnAdd(webgl::EffectiveFormat::RGBA16_SNORM, false,
        {LOCAL_GL_RGBA, LOCAL_GL_SHORT});
}

}  // namespace mozilla
