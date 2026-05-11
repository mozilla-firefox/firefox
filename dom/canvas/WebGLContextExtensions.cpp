/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ClientWebGLExtensions.h"
#include "GLContext.h"
#include "WebGLContext.h"
#include "WebGLContextUtils.h"
#include "WebGLExtensions.h"
#include "mozilla/EnumeratedRange.h"
#include "mozilla/StaticPrefs_webgl.h"
#include "mozilla/dom/BindingDeclarations.h"
#include "mozilla/dom/ToJSValue.h"
#include "nsString.h"

namespace mozilla {

const char* GetExtensionName(const WebGLExtensionID ext) {
  switch (ext) {
#define WEBGL_EXTENSION_IDENTIFIER(x) \
  case WebGLExtensionID::x:           \
    return #x;

    WEBGL_EXTENSION_IDENTIFIER(ANGLE_instanced_arrays)
    WEBGL_EXTENSION_IDENTIFIER(EXT_blend_minmax)
    WEBGL_EXTENSION_IDENTIFIER(EXT_color_buffer_float)
    WEBGL_EXTENSION_IDENTIFIER(EXT_color_buffer_half_float)
    WEBGL_EXTENSION_IDENTIFIER(EXT_depth_clamp)
    WEBGL_EXTENSION_IDENTIFIER(EXT_disjoint_timer_query)
    WEBGL_EXTENSION_IDENTIFIER(EXT_float_blend)
    WEBGL_EXTENSION_IDENTIFIER(EXT_frag_depth)
    WEBGL_EXTENSION_IDENTIFIER(EXT_shader_texture_lod)
    WEBGL_EXTENSION_IDENTIFIER(EXT_sRGB)
    WEBGL_EXTENSION_IDENTIFIER(EXT_texture_compression_bptc)
    WEBGL_EXTENSION_IDENTIFIER(EXT_texture_compression_rgtc)
    WEBGL_EXTENSION_IDENTIFIER(EXT_texture_filter_anisotropic)
    WEBGL_EXTENSION_IDENTIFIER(EXT_texture_norm16)
    WEBGL_EXTENSION_IDENTIFIER(MOZ_debug)
    WEBGL_EXTENSION_IDENTIFIER(OES_draw_buffers_indexed)
    WEBGL_EXTENSION_IDENTIFIER(OES_element_index_uint)
    WEBGL_EXTENSION_IDENTIFIER(OES_fbo_render_mipmap)
    WEBGL_EXTENSION_IDENTIFIER(OES_standard_derivatives)
    WEBGL_EXTENSION_IDENTIFIER(OES_texture_float)
    WEBGL_EXTENSION_IDENTIFIER(OES_texture_float_linear)
    WEBGL_EXTENSION_IDENTIFIER(OES_texture_half_float)
    WEBGL_EXTENSION_IDENTIFIER(OES_texture_half_float_linear)
    WEBGL_EXTENSION_IDENTIFIER(OES_vertex_array_object)
    WEBGL_EXTENSION_IDENTIFIER(OVR_multiview2)
    WEBGL_EXTENSION_IDENTIFIER(WEBGL_color_buffer_float)
    WEBGL_EXTENSION_IDENTIFIER(WEBGL_compressed_texture_astc)
    WEBGL_EXTENSION_IDENTIFIER(WEBGL_compressed_texture_etc)
    WEBGL_EXTENSION_IDENTIFIER(WEBGL_compressed_texture_etc1)
    WEBGL_EXTENSION_IDENTIFIER(WEBGL_compressed_texture_pvrtc)
    WEBGL_EXTENSION_IDENTIFIER(WEBGL_compressed_texture_s3tc)
    WEBGL_EXTENSION_IDENTIFIER(WEBGL_compressed_texture_s3tc_srgb)
    WEBGL_EXTENSION_IDENTIFIER(WEBGL_debug_renderer_info)
    WEBGL_EXTENSION_IDENTIFIER(WEBGL_debug_shaders)
    WEBGL_EXTENSION_IDENTIFIER(WEBGL_depth_texture)
    WEBGL_EXTENSION_IDENTIFIER(WEBGL_draw_buffers)
    WEBGL_EXTENSION_IDENTIFIER(WEBGL_explicit_present)
    WEBGL_EXTENSION_IDENTIFIER(WEBGL_lose_context)
    WEBGL_EXTENSION_IDENTIFIER(WEBGL_provoking_vertex)

#undef WEBGL_EXTENSION_IDENTIFIER

    case WebGLExtensionID::Max:
      break;
  }
  MOZ_CRASH("bad WebGLExtensionID");
}

// ----------------------------
// ClientWebGLContext

void ClientWebGLContext::GetExtension(JSContext* cx, const nsAString& wideName,
                                      JS::MutableHandle<JSObject*> retval,
                                      dom::CallerType callerType,
                                      ErrorResult& rv) {
  retval.set(nullptr);
  const FuncScope funcScope(*this, "getExtension");
  if (IsContextLost()) return;

  const auto name = NS_ConvertUTF16toUTF8(wideName);

  auto ext = WebGLExtensionID::Max;

  // step 1: figure what extension is wanted
  for (const auto extension : MakeEnumeratedRange(WebGLExtensionID::Max)) {
    const auto& curName = GetExtensionName(extension);
    if (name.Equals(curName, nsCaseInsensitiveCStringComparator)) {
      ext = extension;
      break;
    }
  }

  if (ext == WebGLExtensionID::Max) return;

  RefPtr<ClientWebGLExtensionBase> extObj;
  if (ext == WebGLExtensionID::WEBGL_lose_context) {
    extObj = mExtLoseContext;
  } else {
    extObj = GetExtension(ext, callerType);
  }
  if (!extObj) return;

  // Ugh, this would be easier returning `any` than `object`.
  JS::Rooted<JS::Value> v(cx);
  MOZ_ALWAYS_TRUE(dom::ToJSValue(cx, extObj, &v));
  if (v.isObject()) {
    retval.set(&v.toObject());
  }
}

RefPtr<ClientWebGLExtensionBase> ClientWebGLContext::GetExtension(
    const WebGLExtensionID ext, const dom::CallerType callerType) {
  if (ext == WebGLExtensionID::WEBGL_lose_context) {
    // Always the same.
    return mExtLoseContext;
  }

  if (!mNotLost) return nullptr;

  if (!IsSupported(ext, callerType)) return nullptr;

  auto& extSlot = mNotLost->extensions[UnderlyingValue(ext)];
  if (!extSlot) [[unlikely]] {
    extSlot = [&]() -> RefPtr<ClientWebGLExtensionBase> {
      switch (ext) {
        // ANGLE_
        case WebGLExtensionID::ANGLE_instanced_arrays:
          return new ClientWebGLExtensionInstancedArrays(*this);

        // EXT_
        case WebGLExtensionID::EXT_blend_minmax:
          return new ClientWebGLExtensionBlendMinMax(*this);
        case WebGLExtensionID::EXT_color_buffer_float:
          return new ClientWebGLExtensionEXTColorBufferFloat(*this);
        case WebGLExtensionID::EXT_color_buffer_half_float:
          return new ClientWebGLExtensionColorBufferHalfFloat(*this);
        case WebGLExtensionID::EXT_depth_clamp:
          return new ClientWebGLExtensionDepthClamp(*this);
        case WebGLExtensionID::EXT_disjoint_timer_query:
          return new ClientWebGLExtensionDisjointTimerQuery(*this);
        case WebGLExtensionID::EXT_float_blend:
          return new ClientWebGLExtensionFloatBlend(*this);
        case WebGLExtensionID::EXT_frag_depth:
          return new ClientWebGLExtensionFragDepth(*this);
        case WebGLExtensionID::EXT_shader_texture_lod:
          return new ClientWebGLExtensionShaderTextureLod(*this);
        case WebGLExtensionID::EXT_sRGB:
          return new ClientWebGLExtensionSRGB(*this);
        case WebGLExtensionID::EXT_texture_compression_bptc:
          return new ClientWebGLExtensionCompressedTextureBPTC(*this);
        case WebGLExtensionID::EXT_texture_compression_rgtc:
          return new ClientWebGLExtensionCompressedTextureRGTC(*this);
        case WebGLExtensionID::EXT_texture_filter_anisotropic:
          return new ClientWebGLExtensionTextureFilterAnisotropic(*this);
        case WebGLExtensionID::EXT_texture_norm16:
          return new ClientWebGLExtensionTextureNorm16(*this);

        // MOZ_
        case WebGLExtensionID::MOZ_debug:
          return new ClientWebGLExtensionMOZDebug(*this);

        // OES_
        case WebGLExtensionID::OES_draw_buffers_indexed:
          return new ClientWebGLExtensionDrawBuffersIndexed(*this);
        case WebGLExtensionID::OES_element_index_uint:
          return new ClientWebGLExtensionElementIndexUint(*this);
        case WebGLExtensionID::OES_fbo_render_mipmap:
          return new ClientWebGLExtensionFBORenderMipmap(*this);
        case WebGLExtensionID::OES_standard_derivatives:
          return new ClientWebGLExtensionStandardDerivatives(*this);
        case WebGLExtensionID::OES_texture_float:
          return new ClientWebGLExtensionTextureFloat(*this);
        case WebGLExtensionID::OES_texture_float_linear:
          return new ClientWebGLExtensionTextureFloatLinear(*this);
        case WebGLExtensionID::OES_texture_half_float:
          return new ClientWebGLExtensionTextureHalfFloat(*this);
        case WebGLExtensionID::OES_texture_half_float_linear:
          return new ClientWebGLExtensionTextureHalfFloatLinear(*this);
        case WebGLExtensionID::OES_vertex_array_object:
          return new ClientWebGLExtensionVertexArray(*this);

        // OVR_
        case WebGLExtensionID::OVR_multiview2:
          return new ClientWebGLExtensionMultiview(*this);

        // WEBGL_
        case WebGLExtensionID::WEBGL_color_buffer_float:
          return new ClientWebGLExtensionColorBufferFloat(*this);
        case WebGLExtensionID::WEBGL_compressed_texture_astc:
          return new ClientWebGLExtensionCompressedTextureASTC(*this);
        case WebGLExtensionID::WEBGL_compressed_texture_etc:
          return new ClientWebGLExtensionCompressedTextureES3(*this);
        case WebGLExtensionID::WEBGL_compressed_texture_etc1:
          return new ClientWebGLExtensionCompressedTextureETC1(*this);
        case WebGLExtensionID::WEBGL_compressed_texture_pvrtc:
          return new ClientWebGLExtensionCompressedTexturePVRTC(*this);
        case WebGLExtensionID::WEBGL_compressed_texture_s3tc:
          return new ClientWebGLExtensionCompressedTextureS3TC(*this);
        case WebGLExtensionID::WEBGL_compressed_texture_s3tc_srgb:
          return new ClientWebGLExtensionCompressedTextureS3TC_SRGB(*this);
        case WebGLExtensionID::WEBGL_debug_renderer_info: {
          if (callerType != dom::CallerType::System) {
            JsWarning(
                "WEBGL_debug_renderer_info is deprecated in Firefox and will "
                "be removed. Please use RENDERER.");
          }
          return new ClientWebGLExtensionDebugRendererInfo(*this);
        }
        case WebGLExtensionID::WEBGL_debug_shaders:
          return new ClientWebGLExtensionDebugShaders(*this);
        case WebGLExtensionID::WEBGL_depth_texture:
          return new ClientWebGLExtensionDepthTexture(*this);
        case WebGLExtensionID::WEBGL_draw_buffers:
          return new ClientWebGLExtensionDrawBuffers(*this);
        case WebGLExtensionID::WEBGL_explicit_present:
          return new ClientWebGLExtensionExplicitPresent(*this);
        case WebGLExtensionID::WEBGL_provoking_vertex:
          return new ClientWebGLExtensionProvokingVertex(*this);

        case WebGLExtensionID::WEBGL_lose_context:
        case WebGLExtensionID::Max:
          break;
      }
      MOZ_CRASH("illegal extension enum");
    }();
    MOZ_ASSERT(extSlot);
    RequestExtension(ext);
  }

  return extSlot;
}

// ----------------------------
// WebGLContext

bool WebGLContext::IsExtensionSupported(WebGLExtensionID ext) const {
  switch (ext) {
    case WebGLExtensionID::MOZ_debug:
    case WebGLExtensionID::WEBGL_debug_renderer_info:
    case WebGLExtensionID::WEBGL_debug_shaders:
    case WebGLExtensionID::WEBGL_lose_context:
      // Always supported.
      return true;

    // In alphabetical order
    // ANGLE_
    case WebGLExtensionID::ANGLE_instanced_arrays:
      return WebGLExtensionInstancedArrays::IsSupported(this);

    // EXT_
    case WebGLExtensionID::EXT_blend_minmax:
      return WebGLExtensionBlendMinMax::IsSupported(this);

    case WebGLExtensionID::EXT_color_buffer_float:
      return WebGLExtensionEXTColorBufferFloat::IsSupported(this);

    case WebGLExtensionID::EXT_color_buffer_half_float:
      return WebGLExtensionColorBufferHalfFloat::IsSupported(this);

    case WebGLExtensionID::EXT_depth_clamp:
      return gl->IsSupported(gl::GLFeature::depth_clamp);

    case WebGLExtensionID::EXT_disjoint_timer_query:
      return WebGLExtensionDisjointTimerQuery::IsSupported(this);

    case WebGLExtensionID::EXT_float_blend:
      return WebGLExtensionFloatBlend::IsSupported(this);

    case WebGLExtensionID::EXT_frag_depth:
      return WebGLExtensionFragDepth::IsSupported(this);

    case WebGLExtensionID::EXT_shader_texture_lod:
      return WebGLExtensionShaderTextureLod::IsSupported(this);

    case WebGLExtensionID::EXT_sRGB:
      return WebGLExtensionSRGB::IsSupported(this);

    case WebGLExtensionID::EXT_texture_compression_bptc:
      return WebGLExtensionCompressedTextureBPTC::IsSupported(this);

    case WebGLExtensionID::EXT_texture_compression_rgtc:
      return WebGLExtensionCompressedTextureRGTC::IsSupported(this);

    case WebGLExtensionID::EXT_texture_filter_anisotropic:
      return gl->IsExtensionSupported(
          gl::GLContext::EXT_texture_filter_anisotropic);

    case WebGLExtensionID::EXT_texture_norm16:
      return WebGLExtensionTextureNorm16::IsSupported(this);

    // OES_
    case WebGLExtensionID::OES_draw_buffers_indexed:
      if (!IsWebGL2()) return false;
      return gl->IsSupported(gl::GLFeature::draw_buffers_indexed) &&
             gl->IsSupported(gl::GLFeature::get_integer_indexed);

    case WebGLExtensionID::OES_element_index_uint:
      if (IsWebGL2()) return false;
      return gl->IsSupported(gl::GLFeature::element_index_uint);

    case WebGLExtensionID::OES_fbo_render_mipmap:
      return WebGLExtensionFBORenderMipmap::IsSupported(this);

    case WebGLExtensionID::OES_standard_derivatives:
      if (IsWebGL2()) return false;
      return gl->IsSupported(gl::GLFeature::standard_derivatives);

    case WebGLExtensionID::OES_texture_float:
      return WebGLExtensionTextureFloat::IsSupported(this);

    case WebGLExtensionID::OES_texture_float_linear:
      return gl->IsSupported(gl::GLFeature::texture_float_linear);

    case WebGLExtensionID::OES_texture_half_float:
      return WebGLExtensionTextureHalfFloat::IsSupported(this);

    case WebGLExtensionID::OES_texture_half_float_linear:
      if (IsWebGL2()) return false;
      return gl->IsSupported(gl::GLFeature::texture_half_float_linear);

    case WebGLExtensionID::OES_vertex_array_object:
      return !IsWebGL2();  // Always supported in webgl1.

    // OVR_
    case WebGLExtensionID::OVR_multiview2:
      return WebGLExtensionMultiview::IsSupported(this);

    // WEBGL_
    case WebGLExtensionID::WEBGL_color_buffer_float:
      return WebGLExtensionColorBufferFloat::IsSupported(this);

    case WebGLExtensionID::WEBGL_compressed_texture_astc:
      return WebGLExtensionCompressedTextureASTC::IsSupported(this);

    case WebGLExtensionID::WEBGL_compressed_texture_etc:
      return gl->IsSupported(gl::GLFeature::ES3_compatibility) &&
             !gl->IsANGLE();

    case WebGLExtensionID::WEBGL_compressed_texture_etc1:
      return gl->IsExtensionSupported(
                 gl::GLContext::OES_compressed_ETC1_RGB8_texture) &&
             !gl->IsANGLE();

    case WebGLExtensionID::WEBGL_compressed_texture_pvrtc:
      return gl->IsExtensionSupported(
          gl::GLContext::IMG_texture_compression_pvrtc);

    case WebGLExtensionID::WEBGL_compressed_texture_s3tc:
      return WebGLExtensionCompressedTextureS3TC::IsSupported(this);

    case WebGLExtensionID::WEBGL_compressed_texture_s3tc_srgb:
      return WebGLExtensionCompressedTextureS3TC_SRGB::IsSupported(this);

    case WebGLExtensionID::WEBGL_depth_texture:
      return WebGLExtensionDepthTexture::IsSupported(this);

    case WebGLExtensionID::WEBGL_draw_buffers:
      return WebGLExtensionDrawBuffers::IsSupported(this);

    case WebGLExtensionID::WEBGL_explicit_present:
      return WebGLExtensionExplicitPresent::IsSupported(this);

    case WebGLExtensionID::WEBGL_provoking_vertex:
      if (!gl->IsSupported(gl::GLFeature::provoking_vertex)) return false;

      // > Implementations SHOULD only expose this extension when
      // > FIRST_VERTEX_CONVENTION is more efficient than the default behavior
      // > of LAST_VERTEX_CONVENTION.
      if (gl->IsANGLE()) return true;  // Better on D3D.
      if (kIsMacOS) {
        // Better on Metal, so probably Mac in general.
        return true;
      }
      return false;  // Probably not better for Win+GL, Linux, or Android.

    case WebGLExtensionID::Max:
      break;
  }

  MOZ_CRASH();
}

bool WebGLContext::IsExtensionExplicit(const WebGLExtensionID ext) const {
  return mExtensions[ext] && mExtensions[ext]->IsExplicit();
}

void WebGLContext::WarnIfImplicit(const WebGLExtensionID ext) const {
  const auto& extension = mExtensions[ext];
  if (!extension || extension->IsExplicit()) return;

  GenerateWarning(
      "Using format enabled by implicitly enabled extension: %s. "
      "For maximal portability enable it explicitly.",
      GetExtensionName(ext));
}

void WebGLContext::RequestExtension(const WebGLExtensionID ext,
                                    const bool explicitly) {
  const auto& limits = Limits();
  if (!limits.supportedExtensions[ext]) return;

  auto& slot = mExtensions[ext];
  switch (ext) {
    // ANGLE_
    case WebGLExtensionID::ANGLE_instanced_arrays:
      slot.reset(new WebGLExtensionInstancedArrays(this));
      break;

    // EXT_
    case WebGLExtensionID::EXT_blend_minmax:
      slot.reset(new WebGLExtensionBlendMinMax(this));
      break;
    case WebGLExtensionID::EXT_color_buffer_float:
      slot.reset(new WebGLExtensionEXTColorBufferFloat(this));
      break;
    case WebGLExtensionID::EXT_color_buffer_half_float:
      slot.reset(new WebGLExtensionColorBufferHalfFloat(this));
      break;
    case WebGLExtensionID::EXT_depth_clamp:
      slot.reset(new WebGLExtensionDepthClamp(this));
      break;
    case WebGLExtensionID::EXT_disjoint_timer_query:
      slot.reset(new WebGLExtensionDisjointTimerQuery(this));
      break;
    case WebGLExtensionID::EXT_float_blend:
      slot.reset(new WebGLExtensionFloatBlend(this));
      break;
    case WebGLExtensionID::EXT_frag_depth:
      slot.reset(new WebGLExtensionFragDepth(this));
      break;
    case WebGLExtensionID::EXT_shader_texture_lod:
      slot.reset(new WebGLExtensionShaderTextureLod(this));
      break;
    case WebGLExtensionID::EXT_sRGB:
      slot.reset(new WebGLExtensionSRGB(this));
      break;
    case WebGLExtensionID::EXT_texture_compression_bptc:
      slot.reset(new WebGLExtensionCompressedTextureBPTC(this));
      break;
    case WebGLExtensionID::EXT_texture_compression_rgtc:
      slot.reset(new WebGLExtensionCompressedTextureRGTC(this));
      break;
    case WebGLExtensionID::EXT_texture_filter_anisotropic:
      slot.reset(new WebGLExtensionTextureFilterAnisotropic(this));
      break;
    case WebGLExtensionID::EXT_texture_norm16:
      slot.reset(new WebGLExtensionTextureNorm16(this));
      break;

    // MOZ_
    case WebGLExtensionID::MOZ_debug:
      slot.reset(new WebGLExtensionMOZDebug(this));
      break;

    // OES_
    case WebGLExtensionID::OES_draw_buffers_indexed:
      slot.reset(new WebGLExtensionDrawBuffersIndexed(this));
      break;
    case WebGLExtensionID::OES_element_index_uint:
      slot.reset(new WebGLExtensionElementIndexUint(this));
      break;
    case WebGLExtensionID::OES_fbo_render_mipmap:
      slot.reset(new WebGLExtensionFBORenderMipmap(this));
      break;
    case WebGLExtensionID::OES_standard_derivatives:
      slot.reset(new WebGLExtensionStandardDerivatives(this));
      break;
    case WebGLExtensionID::OES_texture_float:
      slot.reset(new WebGLExtensionTextureFloat(this));
      break;
    case WebGLExtensionID::OES_texture_float_linear:
      slot.reset(new WebGLExtensionTextureFloatLinear(this));
      break;
    case WebGLExtensionID::OES_texture_half_float:
      slot.reset(new WebGLExtensionTextureHalfFloat(this));
      break;
    case WebGLExtensionID::OES_texture_half_float_linear:
      slot.reset(new WebGLExtensionTextureHalfFloatLinear(this));
      break;
    case WebGLExtensionID::OES_vertex_array_object:
      slot.reset(new WebGLExtensionVertexArray(this));
      break;

    // WEBGL_
    case WebGLExtensionID::OVR_multiview2:
      slot.reset(new WebGLExtensionMultiview(this));
      break;

    // WEBGL_
    case WebGLExtensionID::WEBGL_color_buffer_float:
      slot.reset(new WebGLExtensionColorBufferFloat(this));
      break;
    case WebGLExtensionID::WEBGL_compressed_texture_astc:
      slot.reset(new WebGLExtensionCompressedTextureASTC(this));
      break;
    case WebGLExtensionID::WEBGL_compressed_texture_etc:
      slot.reset(new WebGLExtensionCompressedTextureES3(this));
      break;
    case WebGLExtensionID::WEBGL_compressed_texture_etc1:
      slot.reset(new WebGLExtensionCompressedTextureETC1(this));
      break;
    case WebGLExtensionID::WEBGL_compressed_texture_pvrtc:
      slot.reset(new WebGLExtensionCompressedTexturePVRTC(this));
      break;
    case WebGLExtensionID::WEBGL_compressed_texture_s3tc:
      slot.reset(new WebGLExtensionCompressedTextureS3TC(this));
      break;
    case WebGLExtensionID::WEBGL_compressed_texture_s3tc_srgb:
      slot.reset(new WebGLExtensionCompressedTextureS3TC_SRGB(this));
      break;
    case WebGLExtensionID::WEBGL_debug_renderer_info:
      slot.reset(new WebGLExtensionDebugRendererInfo(this));
      break;
    case WebGLExtensionID::WEBGL_debug_shaders:
      slot.reset(new WebGLExtensionDebugShaders(this));
      break;
    case WebGLExtensionID::WEBGL_depth_texture:
      slot.reset(new WebGLExtensionDepthTexture(this));
      break;
    case WebGLExtensionID::WEBGL_draw_buffers:
      slot.reset(new WebGLExtensionDrawBuffers(this));
      break;
    case WebGLExtensionID::WEBGL_explicit_present:
      slot.reset(new WebGLExtensionExplicitPresent(this));
      break;
    case WebGLExtensionID::WEBGL_lose_context:
      slot.reset(new WebGLExtensionLoseContext(this));
      break;
    case WebGLExtensionID::WEBGL_provoking_vertex:
      slot.reset(new WebGLExtensionProvokingVertex(this));
      break;

    case WebGLExtensionID::Max:
      MOZ_CRASH();
  }
  MOZ_ASSERT(slot);
  const auto& obj = slot;

  if (explicitly && !obj->IsExplicit()) {
    obj->SetExplicit();
  }

  // Also enable implied extensions.
  switch (ext) {
    case WebGLExtensionID::EXT_color_buffer_float:
      RequestExtension(WebGLExtensionID::EXT_float_blend, false);
      break;

    case WebGLExtensionID::OES_texture_float:
      RequestExtension(WebGLExtensionID::EXT_float_blend, false);
      RequestExtension(WebGLExtensionID::WEBGL_color_buffer_float, false);
      break;

    case WebGLExtensionID::OES_texture_half_float:
      RequestExtension(WebGLExtensionID::EXT_color_buffer_half_float, false);
      break;

    case WebGLExtensionID::WEBGL_color_buffer_float:
      RequestExtension(WebGLExtensionID::EXT_float_blend, false);
      break;

    default:
      break;
  }
}

}  // namespace mozilla
