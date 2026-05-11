/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef WEBGL_FORMATS_H_
#define WEBGL_FORMATS_H_

#include <map>
#include <memory>
#include <set>

#include "WebGLTypes.h"

namespace mozilla::webgl {

using EffectiveFormatValueT = uint8_t;

enum class EffectiveFormat : EffectiveFormatValueT {
  // GLES 3.0.4, p128-129, "Required Texture Formats"
  // "Texture and renderbuffer color formats"
  RGBA32I,
  RGBA32UI,
  RGBA16I,
  RGBA16UI,
  RGBA8,
  RGBA8I,
  RGBA8UI,
  SRGB8_ALPHA8,
  RGB10_A2,
  RGB10_A2UI,
  RGBA4,
  RGB5_A1,

  RGB8,
  RGB565,

  RG32I,
  RG32UI,
  RG16I,
  RG16UI,
  RG8,
  RG8I,
  RG8UI,

  R32I,
  R32UI,
  R16I,
  R16UI,
  R8,
  R8I,
  R8UI,

  // "Texture-only color formats"
  RGBA32F,
  RGBA16F,
  RGBA8_SNORM,

  RGB32F,
  RGB32I,
  RGB32UI,

  RGB16F,
  RGB16I,
  RGB16UI,

  RGB8_SNORM,
  RGB8I,
  RGB8UI,
  SRGB8,

  R11F_G11F_B10F,
  RGB9_E5,

  RG32F,
  RG16F,
  RG8_SNORM,

  R32F,
  R16F,
  R8_SNORM,

  // "Depth formats"
  DEPTH_COMPONENT32F,
  DEPTH_COMPONENT24,
  DEPTH_COMPONENT16,

  // "Combined depth+stencil formats"
  DEPTH32F_STENCIL8,
  DEPTH24_STENCIL8,

  // GLES 3.0.4, p205-206, "Required Renderbuffer Formats"
  STENCIL_INDEX8,

  ////////////////////////////////////

  // GLES 3.0.4, p147, table 3.19
  // GLES 3.0.4, p286+, $C.1 "ETC Compressed Texture Image Formats"
  COMPRESSED_R11_EAC,
  COMPRESSED_SIGNED_R11_EAC,
  COMPRESSED_RG11_EAC,
  COMPRESSED_SIGNED_RG11_EAC,
  COMPRESSED_RGB8_ETC2,
  COMPRESSED_SRGB8_ETC2,
  COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2,
  COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2,
  COMPRESSED_RGBA8_ETC2_EAC,
  COMPRESSED_SRGB8_ALPHA8_ETC2_EAC,

  // EXT_texture_compression_bptc
  COMPRESSED_RGBA_BPTC_UNORM,
  COMPRESSED_SRGB_ALPHA_BPTC_UNORM,
  COMPRESSED_RGB_BPTC_SIGNED_FLOAT,
  COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT,

  // EXT_texture_compression_rgtc
  COMPRESSED_RED_RGTC1,
  COMPRESSED_SIGNED_RED_RGTC1,
  COMPRESSED_RG_RGTC2,
  COMPRESSED_SIGNED_RG_RGTC2,

  // EXT_texture_compression_s3tc
  COMPRESSED_RGB_S3TC_DXT1_EXT,
  COMPRESSED_RGBA_S3TC_DXT1_EXT,
  COMPRESSED_RGBA_S3TC_DXT3_EXT,
  COMPRESSED_RGBA_S3TC_DXT5_EXT,

  // EXT_texture_sRGB
  COMPRESSED_SRGB_S3TC_DXT1_EXT,
  COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT,
  COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT,
  COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT,

  // KHR_texture_compression_astc_ldr
  COMPRESSED_RGBA_ASTC_4x4_KHR,
  COMPRESSED_RGBA_ASTC_5x4_KHR,
  COMPRESSED_RGBA_ASTC_5x5_KHR,
  COMPRESSED_RGBA_ASTC_6x5_KHR,
  COMPRESSED_RGBA_ASTC_6x6_KHR,
  COMPRESSED_RGBA_ASTC_8x5_KHR,
  COMPRESSED_RGBA_ASTC_8x6_KHR,
  COMPRESSED_RGBA_ASTC_8x8_KHR,
  COMPRESSED_RGBA_ASTC_10x5_KHR,
  COMPRESSED_RGBA_ASTC_10x6_KHR,
  COMPRESSED_RGBA_ASTC_10x8_KHR,
  COMPRESSED_RGBA_ASTC_10x10_KHR,
  COMPRESSED_RGBA_ASTC_12x10_KHR,
  COMPRESSED_RGBA_ASTC_12x12_KHR,

  COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR,
  COMPRESSED_SRGB8_ALPHA8_ASTC_5x4_KHR,
  COMPRESSED_SRGB8_ALPHA8_ASTC_5x5_KHR,
  COMPRESSED_SRGB8_ALPHA8_ASTC_6x5_KHR,
  COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR,
  COMPRESSED_SRGB8_ALPHA8_ASTC_8x5_KHR,
  COMPRESSED_SRGB8_ALPHA8_ASTC_8x6_KHR,
  COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR,
  COMPRESSED_SRGB8_ALPHA8_ASTC_10x5_KHR,
  COMPRESSED_SRGB8_ALPHA8_ASTC_10x6_KHR,
  COMPRESSED_SRGB8_ALPHA8_ASTC_10x8_KHR,
  COMPRESSED_SRGB8_ALPHA8_ASTC_10x10_KHR,
  COMPRESSED_SRGB8_ALPHA8_ASTC_12x10_KHR,
  COMPRESSED_SRGB8_ALPHA8_ASTC_12x12_KHR,

  // IMG_texture_compression_pvrtc
  COMPRESSED_RGB_PVRTC_4BPPV1,
  COMPRESSED_RGBA_PVRTC_4BPPV1,
  COMPRESSED_RGB_PVRTC_2BPPV1,
  COMPRESSED_RGBA_PVRTC_2BPPV1,

  // OES_compressed_ETC1_RGB8_texture
  ETC1_RGB8_OES,

  ////////////////////////////////////

  // GLES 3.0.4, p128, table 3.12.
  Luminance8Alpha8,
  Luminance8,
  Alpha8,

  // OES_texture_float
  Luminance32FAlpha32F,
  Luminance32F,
  Alpha32F,

  // OES_texture_half_float
  Luminance16FAlpha16F,
  Luminance16F,
  Alpha16F,

  // EXT_texture_norm16
  R16,
  RG16,
  RGB16,
  RGBA16,
  R16_SNORM,
  RG16_SNORM,
  RGB16_SNORM,
  RGBA16_SNORM,

  MAX,
};

enum class UnsizedFormat : uint8_t {
  R,
  RG,
  RGB,
  RGBA,
  LA,
  L,
  A,
  D,
  S,
  DEPTH_STENCIL,  // `DS` is a macro on Solaris. (regset.h)
};

// GLES 3.0.4 p114 Table 3.4, p240
enum class ComponentType : uint8_t {
  Int,       // RGBA32I
  UInt,      // RGBA32UI
  NormInt,   // RGBA8_SNORM
  NormUInt,  // RGBA8
  Float,     // RGBA32F
};
const char* ToString(ComponentType);

enum class TextureBaseType : uint8_t {
  Int = uint8_t(ComponentType::Int),
  UInt = uint8_t(ComponentType::UInt),
  Float = uint8_t(ComponentType::Float),  // Also includes NormU?Int and Depth
};

const char* ToString(TextureBaseType);

enum class CompressionFamily : uint8_t {
  ASTC,
  BPTC,
  ES3,  // ETC2 or EAC
  ETC1,
  PVRTC,
  RGTC,
  S3TC,
};

////////////////////////////////////////////////////////////////////////////////

struct CompressedFormatInfo {
  const EffectiveFormat effectiveFormat;
  const uint8_t bytesPerBlock;
  const uint8_t blockWidth;
  const uint8_t blockHeight;
  const CompressionFamily family;
};

struct FormatInfo {
  const EffectiveFormat effectiveFormat;
  const char* const name;
  const GLenum sizedFormat;
  const UnsizedFormat unsizedFormat;
  const ComponentType componentType;
  const TextureBaseType baseType;
  const bool isSRGB;

  const CompressedFormatInfo* const compression;

  const uint8_t estimatedBytesPerPixel;  // 0 iff bool(compression).

  // In bits. Iff bool(compression), active channels are 1.
  const uint8_t r;
  const uint8_t g;
  const uint8_t b;
  const uint8_t a;
  const uint8_t d;
  const uint8_t s;

  //////

  const FormatInfo* GetCopyDecayFormat(UnsizedFormat) const;

  bool IsColorFormat() const {
    // Alpha is a 'color format' since it's 'color-attachable'.
    return bool(compression) || bool(r | g | b | a);
  }
};

//////////////////////////////////////////////////////////////////////////////////////////

struct PackingInfoInfo final {
  uint8_t bytesPerElement = 0;
  uint8_t elementsPerPixel = 0;  // E.g. 1 for LOCAL_GL_UNSIGNED_SHORT_4_4_4_4
  bool isPacked = false;

  static Maybe<PackingInfoInfo> For(const PackingInfo&);

  inline uint8_t BytesPerPixel() const {
    return bytesPerElement * elementsPerPixel;
  }
};

const FormatInfo* GetFormat(EffectiveFormat format);

inline uint8_t BytesPerPixel(const PackingInfo& packing) {
  const auto pii = PackingInfoInfo::For(packing);
  if (pii) [[likely]] {
    return pii->BytesPerPixel();
  }

  gfxCriticalError() << "Bad BytesPerPixel(" << packing << ")";
  MOZ_CRASH("Bad `packing`.");
}

/*
GLint ComponentSize(const FormatInfo* format, GLenum component);
GLenum ComponentType(const FormatInfo* format);
*/
////////////////////////////////////////

struct FormatRenderableState final {
 private:
  enum class RenderableState {
    Disabled,
    Implicit,
    Explicit,
  };

 public:
  RenderableState state = RenderableState::Disabled;
  WebGLExtensionID extid = WebGLExtensionID::Max;

  static FormatRenderableState Explicit() {
    return {RenderableState::Explicit};
  }

  static FormatRenderableState Implicit(WebGLExtensionID extid) {
    return {RenderableState::Implicit, extid};
  }

  bool IsRenderable() const { return state != RenderableState::Disabled; }
  bool IsExplicit() const { return state == RenderableState::Explicit; }
};

struct FormatUsageInfo {
  const FormatInfo* const format;

 private:
  FormatRenderableState renderableState;

 public:
  bool isFilterable = false;

  std::map<PackingInfo, DriverUnpackInfo> validUnpacks;
  const DriverUnpackInfo* idealUnpack = nullptr;

  // LOCAL_GL_IMPLEMENTATION_COLOR_READ_FORMAT/_TYPE:
  mutable std::optional<PackingInfo> implReadPiCache;

  const GLint* textureSwizzleRGBA = nullptr;

 private:
  mutable bool maxSamplesKnown = false;
  mutable uint32_t maxSamples = 0;

 public:
  static const GLint kLuminanceSwizzleRGBA[4];
  static const GLint kAlphaSwizzleRGBA[4];
  static const GLint kLumAlphaSwizzleRGBA[4];

  explicit FormatUsageInfo(const FormatInfo* const _format) : format(_format) {
    if (format->IsColorFormat() && format->baseType != TextureBaseType::Float) {
      maxSamplesKnown = true;
    }
  }

  bool IsRenderable() const { return renderableState.IsRenderable(); }
  void SetRenderable(
      const FormatRenderableState& state = FormatRenderableState::Explicit());
  bool IsExplicitlyRenderable() const { return renderableState.IsExplicit(); }
  WebGLExtensionID GetExtensionID() const {
    MOZ_ASSERT(renderableState.extid != WebGLExtensionID::Max);
    return renderableState.extid;
  }

  bool IsUnpackValid(const PackingInfo& key,
                     const DriverUnpackInfo** const out_value) const;

 private:
  void ResolveMaxSamples(gl::GLContext& gl) const;

 public:
  uint32_t MaxSamples(gl::GLContext& gl) const {
    if (!maxSamplesKnown) {
      ResolveMaxSamples(gl);
    }
    return maxSamples;
  }
};

class FormatUsageAuthority {
  std::map<EffectiveFormat, FormatUsageInfo> mUsageMap;

  std::map<GLenum, const FormatUsageInfo*> mRBFormatMap;
  std::map<GLenum, const FormatUsageInfo*> mSizedTexFormatMap;
  std::map<PackingInfo, const FormatUsageInfo*> mUnsizedTexFormatMap;

  std::set<GLenum> mValidTexInternalFormats;
  std::set<GLenum> mValidTexUnpackFormats;
  std::set<GLenum> mValidTexUnpackTypes;

 public:
  static std::unique_ptr<FormatUsageAuthority> CreateForWebGL1(
      gl::GLContext* gl);
  static std::unique_ptr<FormatUsageAuthority> CreateForWebGL2(
      gl::GLContext* gl);

 private:
  FormatUsageAuthority() = default;

 public:
  FormatUsageInfo* EditUsage(EffectiveFormat format);
  const FormatUsageInfo* GetUsage(EffectiveFormat format) const;

  void AddTexUnpack(FormatUsageInfo* usage, const PackingInfo& pi,
                    const DriverUnpackInfo& dui);

  bool IsInternalFormatEnumValid(GLenum internalFormat) const;
  bool AreUnpackEnumsValid(GLenum unpackFormat, GLenum unpackType) const;

  void AllowRBFormat(GLenum sizedFormat, const FormatUsageInfo* usage,
                     bool expectRenderable = true);
  void AllowSizedTexFormat(GLenum sizedFormat, const FormatUsageInfo* usage);
  void AllowUnsizedTexFormat(const PackingInfo& pi,
                             const FormatUsageInfo* usage);

  const FormatUsageInfo* GetRBUsage(GLenum sizedFormat) const;
  const FormatUsageInfo* GetSizedTexUsage(GLenum sizedFormat) const;
  const FormatUsageInfo* GetUnsizedTexUsage(const PackingInfo& pi) const;
};

}  // namespace mozilla::webgl

#endif  // WEBGL_FORMATS_H_
