/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebGLFormats.h"

#include "GLContext.h"
#include "GLDefs.h"
#include "mozilla/StaticMutex.h"
#include "mozilla/gfx/Logging.h"

namespace mozilla::webgl {

const char* ToString(const ComponentType type) {
  switch (type) {
    case ComponentType::Int:
      return "Int";
    case ComponentType::UInt:
      return "UInt";
    case ComponentType::NormInt:
      return "NormInt";
    case ComponentType::NormUInt:
      return "NormUInt";
    case ComponentType::Float:
      return "Float";
  }
  MOZ_CRASH("pacify gcc6 warning");
}

static constexpr TextureBaseType ToBaseType(const ComponentType type) {
  switch (type) {
    case ComponentType::Int:
      return TextureBaseType::Int;
    case ComponentType::UInt:
      return TextureBaseType::UInt;
    case ComponentType::NormInt:
    case ComponentType::NormUInt:
    case ComponentType::Float:
      // case ComponentType::Depth:
      return TextureBaseType::Float;
  }
  MOZ_CRASH("pacify gcc6 warning");
}

const char* ToString(const TextureBaseType x) {
  switch (x) {
    case webgl::TextureBaseType::Float:
      return "FLOAT";
    case webgl::TextureBaseType::Int:
      return "INT";
    case webgl::TextureBaseType::UInt:
      return "UINT";
  }
  MOZ_CRASH("pacify gcc6 warning");
}

// -

template <typename K, typename V, typename K2, typename V2>
static inline void AlwaysInsert(std::map<K, V>& dest, const K2& key,
                                const V2& val) {
  auto res = dest.insert({key, val});
  bool didInsert = res.second;
  MOZ_ALWAYS_TRUE(didInsert);
}

template <typename K, typename V, typename K2>
static inline V* FindOrNull(const std::map<K, V*>& dest, const K2& key) {
  auto itr = dest.find(key);
  if (itr == dest.end()) return nullptr;

  return itr->second;
}

// Returns a pointer to the in-place value for `key`.
template <typename C, typename K2>
static inline auto FindPtrOrNull(C& container, const K2& key) {
  auto itr = container.find(key);
  using R = decltype(&(itr->second));
  if (itr == container.end()) return R{nullptr};

  return &(itr->second);
}

//////////////////////////////////////////////////////////////////////////////////////////

MOZ_RUNINIT std::map<EffectiveFormat, const FormatInfo*> gFormatInfoMap;
MOZ_RUNINIT
std::map<std::pair<EffectiveFormat, UnsizedFormat>, const FormatInfo*>
    gCopyDecayFormatsMap;

static inline const FormatInfo* GetFormatInfo_NoLock(EffectiveFormat format) {
  MOZ_ASSERT(!gFormatInfoMap.empty());
  return FindOrNull(gFormatInfoMap, format);
}

//////////////////////////////////////////////////////////////////////////////////////////

static constexpr CompressedFormatInfo MakeCompressedFormatInfo(
    EffectiveFormat format, uint16_t bitsPerBlock, uint8_t blockWidth,
    uint8_t blockHeight, CompressionFamily family) {
  MOZ_RELEASE_ASSERT(bitsPerBlock % 8 == 0);
  uint16_t bytesPerBlock = bitsPerBlock / 8;  // The specs always state these in
                                              // bits, but it's only ever useful
                                              // to us as bytes.
  MOZ_RELEASE_ASSERT(bytesPerBlock <= 255);

  return {format, uint8_t(bytesPerBlock), blockWidth, blockHeight, family};
}

//////////////////////////////////////////////////////////////////////////////////////////

static constexpr FormatInfo MakeFormatInfo(
    EffectiveFormat format, const char* name, GLenum sizedFormat,
    uint8_t bytesPerPixel, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
    uint8_t d, uint8_t s, UnsizedFormat unsizedFormat, bool isSRGB,
    ComponentType componentType,
    const CompressedFormatInfo* compression = nullptr) {
  switch (unsizedFormat) {
    case UnsizedFormat::R:
      MOZ_ASSERT(r && !g && !b && !a && !d && !s);
      break;

    case UnsizedFormat::RG:
      MOZ_ASSERT(r && g && !b && !a && !d && !s);
      break;

    case UnsizedFormat::RGB:
      MOZ_ASSERT(r && g && b && !a && !d && !s);
      break;

    case UnsizedFormat::RGBA:
      MOZ_ASSERT(r && g && b && a && !d && !s);
      break;

    case UnsizedFormat::L:
      MOZ_ASSERT(r && !g && !b && !a && !d && !s);
      break;

    case UnsizedFormat::A:
      MOZ_ASSERT(!r && !g && !b && a && !d && !s);
      break;

    case UnsizedFormat::LA:
      MOZ_ASSERT(r && !g && !b && a && !d && !s);
      break;

    case UnsizedFormat::D:
      MOZ_ASSERT(!r && !g && !b && !a && d && !s);
      break;

    case UnsizedFormat::S:
      MOZ_ASSERT(!r && !g && !b && !a && !d && s);
      break;

    case UnsizedFormat::DEPTH_STENCIL:
      MOZ_ASSERT(!r && !g && !b && !a && d && s);
      break;
  }

  MOZ_ASSERT(!bytesPerPixel == bool(compression));

#ifdef DEBUG
  uint8_t totalBits = r + g + b + a + d + s;
  if (format == EffectiveFormat::RGB9_E5) {
    totalBits = 9 + 9 + 9 + 5;
  }

  if (compression) {
    MOZ_ASSERT(totalBits);
    MOZ_ASSERT(!bytesPerPixel);
  } else {
    MOZ_ASSERT(totalBits == bytesPerPixel * 8);
  }
#endif

  return {format,
          name,
          sizedFormat,
          unsizedFormat,
          componentType,
          ToBaseType(componentType),
          isSRGB,
          compression,
          bytesPerPixel,
          r,
          g,
          b,
          a,
          d,
          s};
}

static inline void AddFormatInfo(const FormatInfo* info) {
  AlwaysInsert(gFormatInfoMap, info->effectiveFormat, info);
}

static void InitFormatInfo() {
  // This function is full of expressive formatting, so:
  // clang-format off

    #define FORMAT_INFO(name, bytesPerPixel, r, g, b, a, unsizedFormat, isSRGB, componentType, ...) \
        static constexpr FormatInfo __FORMAT_INFO_##name = \
            MakeFormatInfo(EffectiveFormat::name, #name, LOCAL_GL_##name, bytesPerPixel, r, g, b, a, 0, 0, \
                           UnsizedFormat::unsizedFormat, isSRGB, ComponentType::componentType, ##__VA_ARGS__); \
        AddFormatInfo(&__FORMAT_INFO_##name)

    #define DEPTH_STENCIL_FORMAT_INFO(name, bytesPerPixel, d, s, unsizedFormat, componentType, ...) \
        static constexpr FormatInfo __FORMAT_INFO_##name = \
            MakeFormatInfo(EffectiveFormat::name, #name, LOCAL_GL_##name, bytesPerPixel, 0, 0, 0, 0, d, s, \
                           UnsizedFormat::unsizedFormat, false, ComponentType::componentType, ##__VA_ARGS__); \
        AddFormatInfo(&__FORMAT_INFO_##name)

    #define COMPRESSED_FORMAT_INFO(name, bitsPerBlock, blockWidth, blockHeight, family, ...) \
        static constexpr CompressedFormatInfo __COMPRESSED_FORMAT_INFO_##name = \
            MakeCompressedFormatInfo(EffectiveFormat::name, bitsPerBlock, blockWidth, blockHeight, CompressionFamily::family); \
        FORMAT_INFO(name, 0, ##__VA_ARGS__, &__COMPRESSED_FORMAT_INFO_##name)

    // 'Virtual' effective formats have no sizedFormat.
    #define VIRTUAL_FORMAT_INFO(name, bytesPerPixel, r, g, b, a, unsizedFormat, isSRGB, componentType, ...) \
        static constexpr FormatInfo __FORMAT_INFO_##name = \
            MakeFormatInfo(EffectiveFormat::name, #name, 0, bytesPerPixel, r, g, b, a, 0, 0, \
                           UnsizedFormat::unsizedFormat, isSRGB, ComponentType::componentType, ##__VA_ARGS__); \
        AddFormatInfo(&__FORMAT_INFO_##name)

    // GLES 3.0.4, p130-132, table 3.13
    //           |     format        | renderable | filterable |
    FORMAT_INFO(R8            ,  1,  8, 0, 0, 0,  R   , false, NormUInt);
    FORMAT_INFO(R8_SNORM      ,  1,  8, 0, 0, 0,  R   , false, NormInt );
    FORMAT_INFO(RG8           ,  2,  8, 8, 0, 0,  RG  , false, NormUInt);
    FORMAT_INFO(RG8_SNORM     ,  2,  8, 8, 0, 0,  RG  , false, NormInt );
    FORMAT_INFO(RGB8          ,  3,  8, 8, 8, 0,  RGB , false, NormUInt);
    FORMAT_INFO(RGB8_SNORM    ,  3,  8, 8, 8, 0,  RGB , false, NormInt );
    FORMAT_INFO(RGB565        ,  2,  5, 6, 5, 0,  RGB , false, NormUInt);
    FORMAT_INFO(RGBA4         ,  2,  4, 4, 4, 4,  RGBA, false, NormUInt);
    FORMAT_INFO(RGB5_A1       ,  2,  5, 5, 5, 1,  RGBA, false, NormUInt);
    FORMAT_INFO(RGBA8         ,  4,  8, 8, 8, 8,  RGBA, false, NormUInt);
    FORMAT_INFO(RGBA8_SNORM   ,  4,  8, 8, 8, 8,  RGBA, false, NormInt );
    FORMAT_INFO(RGB10_A2      ,  4, 10,10,10, 2,  RGBA, false, NormUInt);
    FORMAT_INFO(RGB10_A2UI    ,  4, 10,10,10, 2,  RGBA, false, UInt    );

    FORMAT_INFO(SRGB8         ,  3,  8, 8, 8, 0,  RGB , true , NormUInt);
    FORMAT_INFO(SRGB8_ALPHA8  ,  4,  8, 8, 8, 8,  RGBA, true , NormUInt);

    FORMAT_INFO(R16F          ,  2, 16, 0, 0, 0,  R   , false, Float   );
    FORMAT_INFO(RG16F         ,  4, 16,16, 0, 0,  RG  , false, Float   );
    FORMAT_INFO(RGB16F        ,  6, 16,16,16, 0,  RGB , false, Float   );
    FORMAT_INFO(RGBA16F       ,  8, 16,16,16,16,  RGBA, false, Float   );
    FORMAT_INFO(R32F          ,  4, 32, 0, 0, 0,  R   , false, Float   );
    FORMAT_INFO(RG32F         ,  8, 32,32, 0, 0,  RG  , false, Float   );
    FORMAT_INFO(RGB32F        , 12, 32,32,32, 0,  RGB , false, Float   );
    FORMAT_INFO(RGBA32F       , 16, 32,32,32,32,  RGBA, false, Float   );

    FORMAT_INFO(R11F_G11F_B10F,  4, 11,11,10, 0,  RGB , false, Float   );
    FORMAT_INFO(RGB9_E5       ,  4, 14,14,14, 0,  RGB , false, Float   );

    FORMAT_INFO(R8I           ,  1,  8, 0, 0, 0,  R   , false, Int     );
    FORMAT_INFO(R8UI          ,  1,  8, 0, 0, 0,  R   , false, UInt    );
    FORMAT_INFO(R16I          ,  2, 16, 0, 0, 0,  R   , false, Int     );
    FORMAT_INFO(R16UI         ,  2, 16, 0, 0, 0,  R   , false, UInt    );
    FORMAT_INFO(R32I          ,  4, 32, 0, 0, 0,  R   , false, Int     );
    FORMAT_INFO(R32UI         ,  4, 32, 0, 0, 0,  R   , false, UInt    );

    FORMAT_INFO(RG8I          ,  2,  8, 8, 0, 0,  RG  , false, Int     );
    FORMAT_INFO(RG8UI         ,  2,  8, 8, 0, 0,  RG  , false, UInt    );
    FORMAT_INFO(RG16I         ,  4, 16,16, 0, 0,  RG  , false, Int     );
    FORMAT_INFO(RG16UI        ,  4, 16,16, 0, 0,  RG  , false, UInt    );
    FORMAT_INFO(RG32I         ,  8, 32,32, 0, 0,  RG  , false, Int     );
    FORMAT_INFO(RG32UI        ,  8, 32,32, 0, 0,  RG  , false, UInt    );

    FORMAT_INFO(RGB8I         ,  3,  8, 8, 8, 0,  RGB , false, Int     );
    FORMAT_INFO(RGB8UI        ,  3,  8, 8, 8, 0,  RGB , false, UInt    );
    FORMAT_INFO(RGB16I        ,  6, 16,16,16, 0,  RGB , false, Int     );
    FORMAT_INFO(RGB16UI       ,  6, 16,16,16, 0,  RGB , false, UInt    );
    FORMAT_INFO(RGB32I        , 12, 32,32,32, 0,  RGB , false, Int     );
    FORMAT_INFO(RGB32UI       , 12, 32,32,32, 0,  RGB , false, UInt    );

    FORMAT_INFO(RGBA8I        ,  4,  8, 8, 8, 8,  RGBA, false, Int     );
    FORMAT_INFO(RGBA8UI       ,  4,  8, 8, 8, 8,  RGBA, false, UInt    );
    FORMAT_INFO(RGBA16I       ,  8, 16,16,16,16,  RGBA, false, Int     );
    FORMAT_INFO(RGBA16UI      ,  8, 16,16,16,16,  RGBA, false, UInt    );
    FORMAT_INFO(RGBA32I       , 16, 32,32,32,32,  RGBA, false, Int     );
    FORMAT_INFO(RGBA32UI      , 16, 32,32,32,32,  RGBA, false, UInt    );

    // GLES 3.0.4, p133, table 3.14
    DEPTH_STENCIL_FORMAT_INFO(DEPTH_COMPONENT16 , 2, 16,0, D, NormUInt);
    DEPTH_STENCIL_FORMAT_INFO(DEPTH_COMPONENT24 , 3, 24,0, D, NormUInt);
    DEPTH_STENCIL_FORMAT_INFO(DEPTH_COMPONENT32F, 4, 32,0, D, Float);
    // DEPTH_STENCIL types are sampled as their depth component.
    DEPTH_STENCIL_FORMAT_INFO(DEPTH24_STENCIL8  , 4, 24,8, DEPTH_STENCIL, NormUInt);
    DEPTH_STENCIL_FORMAT_INFO(DEPTH32F_STENCIL8 , 5, 32,8, DEPTH_STENCIL, Float);

    // GLES 3.0.4, p205-206, "Required Renderbuffer Formats"
    DEPTH_STENCIL_FORMAT_INFO(STENCIL_INDEX8, 1, 0,8, S, Int);

    // GLES 3.0.4, p147, table 3.19
    // GLES 3.0.4  p286+  $C.1 "ETC Compressed Texture Image Formats"
    COMPRESSED_FORMAT_INFO(COMPRESSED_RGB8_ETC2                     ,  64,4,4,ES3, 1,1,1,0, RGB , false, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_SRGB8_ETC2                    ,  64,4,4,ES3, 1,1,1,0, RGB , true , NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_RGBA8_ETC2_EAC                , 128,4,4,ES3, 1,1,1,1, RGBA, false, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_SRGB8_ALPHA8_ETC2_EAC         , 128,4,4,ES3, 1,1,1,1, RGBA, true , NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_R11_EAC                       ,  64,4,4,ES3, 1,0,0,0, R   , false, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_RG11_EAC                      , 128,4,4,ES3, 1,1,0,0, RG  , false, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_SIGNED_R11_EAC                ,  64,4,4,ES3, 1,0,0,0, R   , false, NormInt );
    COMPRESSED_FORMAT_INFO(COMPRESSED_SIGNED_RG11_EAC               , 128,4,4,ES3, 1,1,0,0, RG  , false, NormInt );
    COMPRESSED_FORMAT_INFO(COMPRESSED_RGB8_PUNCHTHROUGH_ALPHA1_ETC2 ,  64,4,4,ES3, 1,1,1,1, RGBA, false, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_SRGB8_PUNCHTHROUGH_ALPHA1_ETC2,  64,4,4,ES3, 1,1,1,1, RGBA, true , NormUInt);

    // EXT_texture_compression_bptc
    COMPRESSED_FORMAT_INFO(COMPRESSED_RGBA_BPTC_UNORM        , 16*8,4,4,BPTC, 1,1,1,1, RGBA, false, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_SRGB_ALPHA_BPTC_UNORM  , 16*8,4,4,BPTC, 1,1,1,1, RGBA, true , NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_RGB_BPTC_SIGNED_FLOAT  , 16*8,4,4,BPTC, 1,1,1,0, RGB , false, Float   );
    COMPRESSED_FORMAT_INFO(COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT, 16*8,4,4,BPTC, 1,1,1,0, RGB , false, Float   );

    // EXT_texture_compression_rgtc
    COMPRESSED_FORMAT_INFO(COMPRESSED_RED_RGTC1       ,  8*8,4,4,RGTC, 1,0,0,0, R , false, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_SIGNED_RED_RGTC1,  8*8,4,4,RGTC, 1,0,0,0, R , false, NormInt );
    COMPRESSED_FORMAT_INFO(COMPRESSED_RG_RGTC2        , 16*8,4,4,RGTC, 1,1,0,0, RG, false, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_SIGNED_RG_RGTC2 , 16*8,4,4,RGTC, 1,1,0,0, RG, false, NormInt );

    // EXT_texture_compression_s3tc
    COMPRESSED_FORMAT_INFO(COMPRESSED_RGB_S3TC_DXT1_EXT ,  64,4,4,S3TC, 1,1,1,0, RGB , false, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_RGBA_S3TC_DXT1_EXT,  64,4,4,S3TC, 1,1,1,1, RGBA, false, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_RGBA_S3TC_DXT3_EXT, 128,4,4,S3TC, 1,1,1,1, RGBA, false, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_RGBA_S3TC_DXT5_EXT, 128,4,4,S3TC, 1,1,1,1, RGBA, false, NormUInt);

    // EXT_texture_compression_s3tc_srgb
    COMPRESSED_FORMAT_INFO(COMPRESSED_SRGB_S3TC_DXT1_EXT      ,  64,4,4,S3TC, 1,1,1,0, RGB , true, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT,  64,4,4,S3TC, 1,1,1,1, RGBA, true, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT, 128,4,4,S3TC, 1,1,1,1, RGBA, true, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT, 128,4,4,S3TC, 1,1,1,1, RGBA, true, NormUInt);

    // KHR_texture_compression_astc_ldr
    COMPRESSED_FORMAT_INFO(COMPRESSED_RGBA_ASTC_4x4_KHR          , 128, 4, 4,ASTC, 1,1,1,1, RGBA, false, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_RGBA_ASTC_5x4_KHR          , 128, 5, 4,ASTC, 1,1,1,1, RGBA, false, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_RGBA_ASTC_5x5_KHR          , 128, 5, 5,ASTC, 1,1,1,1, RGBA, false, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_RGBA_ASTC_6x5_KHR          , 128, 6, 5,ASTC, 1,1,1,1, RGBA, false, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_RGBA_ASTC_6x6_KHR          , 128, 6, 6,ASTC, 1,1,1,1, RGBA, false, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_RGBA_ASTC_8x5_KHR          , 128, 8, 5,ASTC, 1,1,1,1, RGBA, false, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_RGBA_ASTC_8x6_KHR          , 128, 8, 6,ASTC, 1,1,1,1, RGBA, false, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_RGBA_ASTC_8x8_KHR          , 128, 8, 8,ASTC, 1,1,1,1, RGBA, false, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_RGBA_ASTC_10x5_KHR         , 128,10, 5,ASTC, 1,1,1,1, RGBA, false, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_RGBA_ASTC_10x6_KHR         , 128,10, 6,ASTC, 1,1,1,1, RGBA, false, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_RGBA_ASTC_10x8_KHR         , 128,10, 8,ASTC, 1,1,1,1, RGBA, false, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_RGBA_ASTC_10x10_KHR        , 128,10,10,ASTC, 1,1,1,1, RGBA, false, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_RGBA_ASTC_12x10_KHR        , 128,12,10,ASTC, 1,1,1,1, RGBA, false, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_RGBA_ASTC_12x12_KHR        , 128,12,12,ASTC, 1,1,1,1, RGBA, false, NormUInt);

    COMPRESSED_FORMAT_INFO(COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR  , 128, 4, 4,ASTC, 1,1,1,1, RGBA, true , NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_SRGB8_ALPHA8_ASTC_5x4_KHR  , 128, 5, 4,ASTC, 1,1,1,1, RGBA, true , NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_SRGB8_ALPHA8_ASTC_5x5_KHR  , 128, 5, 5,ASTC, 1,1,1,1, RGBA, true , NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_SRGB8_ALPHA8_ASTC_6x5_KHR  , 128, 6, 5,ASTC, 1,1,1,1, RGBA, true , NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR  , 128, 6, 6,ASTC, 1,1,1,1, RGBA, true , NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_SRGB8_ALPHA8_ASTC_8x5_KHR  , 128, 8, 5,ASTC, 1,1,1,1, RGBA, true , NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_SRGB8_ALPHA8_ASTC_8x6_KHR  , 128, 8, 6,ASTC, 1,1,1,1, RGBA, true , NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR  , 128, 8, 8,ASTC, 1,1,1,1, RGBA, true , NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_SRGB8_ALPHA8_ASTC_10x5_KHR , 128,10, 5,ASTC, 1,1,1,1, RGBA, true , NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_SRGB8_ALPHA8_ASTC_10x6_KHR , 128,10, 6,ASTC, 1,1,1,1, RGBA, true , NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_SRGB8_ALPHA8_ASTC_10x8_KHR , 128,10, 8,ASTC, 1,1,1,1, RGBA, true , NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_SRGB8_ALPHA8_ASTC_10x10_KHR, 128,10,10,ASTC, 1,1,1,1, RGBA, true , NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_SRGB8_ALPHA8_ASTC_12x10_KHR, 128,12,10,ASTC, 1,1,1,1, RGBA, true , NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_SRGB8_ALPHA8_ASTC_12x12_KHR, 128,12,12,ASTC, 1,1,1,1, RGBA, true , NormUInt);

    // IMG_texture_compression_pvrtc
    COMPRESSED_FORMAT_INFO(COMPRESSED_RGB_PVRTC_4BPPV1 , 256, 8,8,PVRTC, 1,1,1,0, RGB , false, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_RGBA_PVRTC_4BPPV1, 256, 8,8,PVRTC, 1,1,1,1, RGBA, false, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_RGB_PVRTC_2BPPV1 , 256,16,8,PVRTC, 1,1,1,0, RGB , false, NormUInt);
    COMPRESSED_FORMAT_INFO(COMPRESSED_RGBA_PVRTC_2BPPV1, 256,16,8,PVRTC, 1,1,1,1, RGBA, false, NormUInt);

    // OES_compressed_ETC1_RGB8_texture
    COMPRESSED_FORMAT_INFO(ETC1_RGB8_OES, 64,4,4,ETC1, 1,1,1,0, RGB, false, NormUInt);

    // EXT_texture_norm16
    FORMAT_INFO(R16   , 2, 16, 0, 0, 0, R   , false, NormUInt);
    FORMAT_INFO(RG16  , 4, 16,16, 0, 0, RG  , false, NormUInt);
    FORMAT_INFO(RGB16 , 6, 16,16,16, 0, RGB , false, NormUInt);
    FORMAT_INFO(RGBA16, 8, 16,16,16,16, RGBA, false, NormUInt);

    FORMAT_INFO(R16_SNORM   , 2, 16, 0, 0, 0, R   , false, NormInt);
    FORMAT_INFO(RG16_SNORM  , 4, 16,16, 0, 0, RG  , false, NormInt);
    FORMAT_INFO(RGB16_SNORM , 6, 16,16,16, 0, RGB , false, NormInt);
    FORMAT_INFO(RGBA16_SNORM, 8, 16,16,16,16, RGBA, false, NormInt);

    // GLES 3.0.4, p128, table 3.12.
    VIRTUAL_FORMAT_INFO(Luminance8Alpha8, 2, 8,0,0,8, LA, false, NormUInt);
    VIRTUAL_FORMAT_INFO(Luminance8      , 1, 8,0,0,0, L , false, NormUInt);
    VIRTUAL_FORMAT_INFO(Alpha8          , 1, 0,0,0,8, A , false, NormUInt);

    // OES_texture_float
    VIRTUAL_FORMAT_INFO(Luminance32FAlpha32F, 8, 32,0,0,32, LA, false, Float);
    VIRTUAL_FORMAT_INFO(Luminance32F        , 4, 32,0,0, 0, L , false, Float);
    VIRTUAL_FORMAT_INFO(Alpha32F            , 4,  0,0,0,32, A , false, Float);

    // OES_texture_half_float
    VIRTUAL_FORMAT_INFO(Luminance16FAlpha16F, 4, 16,0,0,16, LA, false, Float);
    VIRTUAL_FORMAT_INFO(Luminance16F        , 2, 16,0,0, 0, L , false, Float);
    VIRTUAL_FORMAT_INFO(Alpha16F            , 2,  0,0,0,16, A , false, Float);

    #undef FORMAT_INFO
    #undef DEPTH_STENCIL_FORMAT_INFO
    #undef COMPRESSED_FORMAT_INFO
    #undef VIRTUAL_FORMAT_INFO

    ////////////////////////////////////////////////////////////////////////////

    const auto fnSetCopyDecay = [](EffectiveFormat src, EffectiveFormat asR,
                                   EffectiveFormat asRG, EffectiveFormat asRGB,
                                   EffectiveFormat asRGBA, EffectiveFormat asL,
                                   EffectiveFormat asA, EffectiveFormat asLA)
    {
        const auto fnSet = [src](UnsizedFormat uf, EffectiveFormat ef) {
            if (ef == EffectiveFormat::MAX)
                return;

            const auto* format = GetFormatInfo_NoLock(ef);
            MOZ_ASSERT(format->unsizedFormat == uf);
            AlwaysInsert(gCopyDecayFormatsMap, std::make_pair(src, uf), format);
        };

        fnSet(UnsizedFormat::R   , asR);
        fnSet(UnsizedFormat::RG  , asRG);
        fnSet(UnsizedFormat::RGB , asRGB);
        fnSet(UnsizedFormat::RGBA, asRGBA);
        fnSet(UnsizedFormat::L   , asL);
        fnSet(UnsizedFormat::A   , asA);
        fnSet(UnsizedFormat::LA  , asLA);
    };

#define SET_COPY_DECAY(src,asR,asRG,asRGB,asRGBA,asL,asA,asLA) \
    fnSetCopyDecay(EffectiveFormat::src, EffectiveFormat::asR, EffectiveFormat::asRG,     \
                   EffectiveFormat::asRGB, EffectiveFormat::asRGBA, EffectiveFormat::asL, \
                   EffectiveFormat::asA, EffectiveFormat::asLA);

    //////

#define SET_BY_SUFFIX(X) \
        SET_COPY_DECAY(   R##X, R##X,   MAX,    MAX,     MAX, Luminance##X,      MAX,                    MAX) \
        SET_COPY_DECAY(  RG##X, R##X, RG##X,    MAX,     MAX, Luminance##X,      MAX,                    MAX) \
        SET_COPY_DECAY( RGB##X, R##X, RG##X, RGB##X,     MAX, Luminance##X,      MAX,                    MAX) \
        SET_COPY_DECAY(RGBA##X, R##X, RG##X, RGB##X, RGBA##X, Luminance##X, Alpha##X, Luminance##X##Alpha##X)

    SET_BY_SUFFIX(8)   // WebGL decided that RGB8 should be guaranteed renderable.
    SET_BY_SUFFIX(16F) // RGB16F is renderable in EXT_color_buffer_half_float, though not
                       // EXT_color_buffer_float.
    SET_BY_SUFFIX(32F) // Technically RGB32F is never renderable, but no harm here.

#undef SET_BY_SUFFIX


    //////

#define SET_BY_SUFFIX(X) \
        SET_COPY_DECAY(   R##X, R##X,   MAX,    MAX,     MAX, MAX, MAX, MAX) \
        SET_COPY_DECAY(  RG##X, R##X, RG##X,    MAX,     MAX, MAX, MAX, MAX) \
        SET_COPY_DECAY(RGBA##X, R##X, RG##X, RGB##X, RGBA##X, MAX, MAX, MAX)

    SET_BY_SUFFIX(8I)
    SET_BY_SUFFIX(8UI)

    SET_BY_SUFFIX(16)
    SET_BY_SUFFIX(16I)
    SET_BY_SUFFIX(16UI)

    SET_BY_SUFFIX(32I)
    SET_BY_SUFFIX(32UI)

#undef SET_BY_SUFFIX

    //////

    SET_COPY_DECAY(    RGB565, R8, RG8, RGB565,      MAX, Luminance8,    MAX,              MAX)
    SET_COPY_DECAY(     RGBA4, R8, RG8, RGB565,    RGBA4, Luminance8, Alpha8, Luminance8Alpha8)
    SET_COPY_DECAY(   RGB5_A1, R8, RG8, RGB565,  RGB5_A1, Luminance8, Alpha8, Luminance8Alpha8)
    SET_COPY_DECAY(  RGB10_A2, R8, RG8,   RGB8, RGB10_A2, Luminance8, Alpha8,              MAX)

    SET_COPY_DECAY(RGB10_A2UI, R8UI, RG8UI, RGB8UI, RGB10_A2UI, MAX, MAX, MAX)

    SET_COPY_DECAY(SRGB8_ALPHA8, MAX, MAX, MAX, SRGB8_ALPHA8, MAX, Alpha8, MAX)

    SET_COPY_DECAY(R11F_G11F_B10F, R16F, RG16F, R11F_G11F_B10F, MAX, Luminance16F, MAX, MAX)

#undef SET_COPY_DECAY

  // clang-format on
}

//////////////////////////////////////////////////////////////////////////////////////////

bool gAreFormatTablesInitialized = false;

static void EnsureInitFormatTables(
    const StaticMutexAutoLock&)  // Prove that you locked it!
{
  if (gAreFormatTablesInitialized) [[likely]] {
    return;
  }

  gAreFormatTablesInitialized = true;

  InitFormatInfo();
}

//////////////////////////////////////////////////////////////////////////////////////////
// Public funcs

StaticMutex gFormatMapMutex;

const FormatInfo* GetFormat(EffectiveFormat format) {
  StaticMutexAutoLock lock(gFormatMapMutex);
  EnsureInitFormatTables(lock);

  return GetFormatInfo_NoLock(format);
}

//////////////////////////////////////////////////////////////////////////////////////////

const FormatInfo* FormatInfo::GetCopyDecayFormat(UnsizedFormat uf) const {
  StaticMutexAutoLock lock(gFormatMapMutex);
  EnsureInitFormatTables(lock);

  return FindOrNull(gCopyDecayFormatsMap, std::make_pair(effectiveFormat, uf));
}

Maybe<PackingInfoInfo> PackingInfoInfo::For(const PackingInfo& pi) {
  PackingInfoInfo ret{};

  switch (pi.type) {
    case LOCAL_GL_UNSIGNED_SHORT_4_4_4_4:
    case LOCAL_GL_UNSIGNED_SHORT_5_5_5_1:
    case LOCAL_GL_UNSIGNED_SHORT_5_6_5:
      ret = {2, 1, true};
      break;

    case LOCAL_GL_UNSIGNED_INT_10F_11F_11F_REV:
    case LOCAL_GL_UNSIGNED_INT_2_10_10_10_REV:
    case LOCAL_GL_UNSIGNED_INT_24_8:
    case LOCAL_GL_UNSIGNED_INT_5_9_9_9_REV:
      ret = {4, 1, true};
      break;

    case LOCAL_GL_FLOAT_32_UNSIGNED_INT_24_8_REV:
      ret = {8, 1, true};
      break;

      // Alright, that's all the fixed-size unpackTypes.

    case LOCAL_GL_BYTE:
    case LOCAL_GL_UNSIGNED_BYTE:
      ret = {1, 0, false};
      break;

    case LOCAL_GL_SHORT:
    case LOCAL_GL_UNSIGNED_SHORT:
    case LOCAL_GL_HALF_FLOAT:
    case LOCAL_GL_HALF_FLOAT_OES:
      ret = {2, 0, false};
      break;

    case LOCAL_GL_INT:
    case LOCAL_GL_UNSIGNED_INT:
    case LOCAL_GL_FLOAT:
      ret = {4, 0, false};
      break;

    default:
      return {};
  }

  if (!ret.isPacked) {
    switch (pi.format) {
      case LOCAL_GL_RED:
      case LOCAL_GL_RED_INTEGER:
      case LOCAL_GL_LUMINANCE:
      case LOCAL_GL_ALPHA:
      case LOCAL_GL_DEPTH_COMPONENT:
        ret.elementsPerPixel = 1;
        break;

      case LOCAL_GL_RG:
      case LOCAL_GL_RG_INTEGER:
      case LOCAL_GL_LUMINANCE_ALPHA:
        ret.elementsPerPixel = 2;
        break;

      case LOCAL_GL_RGB:
      case LOCAL_GL_RGB_INTEGER:
      case LOCAL_GL_SRGB:
        ret.elementsPerPixel = 3;
        break;

      case LOCAL_GL_BGRA:
      case LOCAL_GL_RGBA:
      case LOCAL_GL_RGBA_INTEGER:
      case LOCAL_GL_SRGB_ALPHA:
        ret.elementsPerPixel = 4;
        break;

      default:
        return {};
    }
  }

  return Some(ret);
}

//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////
// FormatUsageAuthority

bool FormatUsageInfo::IsUnpackValid(
    const PackingInfo& key, const DriverUnpackInfo** const out_value) const {
  auto itr = validUnpacks.find(key);
  if (itr == validUnpacks.end()) return false;

  *out_value = &(itr->second);
  return true;
}

void FormatUsageInfo::ResolveMaxSamples(gl::GLContext& gl) const {
  MOZ_ASSERT(gl.IsCurrent());
  MOZ_ASSERT(!this->maxSamplesKnown);
  MOZ_ASSERT(!this->maxSamples);
  this->maxSamplesKnown = true;

  const GLenum internalFormat = this->format->sizedFormat;
  if (!internalFormat) return;
  if (!gl.IsSupported(gl::GLFeature::internalformat_query)) return;

  // GL_SAMPLES returns a list in descending order, so ask for just one elem to
  // get the max.
  gl.fGetInternalformativ(LOCAL_GL_RENDERBUFFER, internalFormat,
                          LOCAL_GL_SAMPLES, 1,
                          reinterpret_cast<GLint*>(&this->maxSamples));
}

////////////////////////////////////////

static void AddSimpleUnsized(FormatUsageAuthority* fua, GLenum unpackFormat,
                             GLenum unpackType, EffectiveFormat effFormat) {
  auto usage = fua->EditUsage(effFormat);
  usage->isFilterable = true;

  const PackingInfo pi = {unpackFormat, unpackType};
  const DriverUnpackInfo dui = {unpackFormat, unpackFormat, unpackType};
  fua->AddTexUnpack(usage, pi, dui);

  fua->AllowUnsizedTexFormat(pi, usage);
};

/*static*/ const GLint FormatUsageInfo::kLuminanceSwizzleRGBA[4] = {
    LOCAL_GL_RED, LOCAL_GL_RED, LOCAL_GL_RED, LOCAL_GL_ONE};
/*static*/ const GLint FormatUsageInfo::kAlphaSwizzleRGBA[4] = {
    LOCAL_GL_ZERO, LOCAL_GL_ZERO, LOCAL_GL_ZERO, LOCAL_GL_RED};
/*static*/ const GLint FormatUsageInfo::kLumAlphaSwizzleRGBA[4] = {
    LOCAL_GL_RED, LOCAL_GL_RED, LOCAL_GL_RED, LOCAL_GL_GREEN};

static bool AddLegacyFormats_LA8(FormatUsageAuthority* fua, gl::GLContext* gl) {
  if (gl->IsCoreProfile()) {
    if (!gl->IsSupported(gl::GLFeature::texture_swizzle)) return false;

    PackingInfo pi;
    DriverUnpackInfo dui;

    const auto fnAdd = [fua, &pi, &dui](EffectiveFormat effFormat,
                                        const GLint* swizzle) {
      auto usage = fua->EditUsage(effFormat);
      usage->isFilterable = true;
      usage->textureSwizzleRGBA = swizzle;

      fua->AddTexUnpack(usage, pi, dui);

      fua->AllowUnsizedTexFormat(pi, usage);
    };

    pi = {LOCAL_GL_LUMINANCE, LOCAL_GL_UNSIGNED_BYTE};
    dui = {LOCAL_GL_R8, LOCAL_GL_RED, LOCAL_GL_UNSIGNED_BYTE};
    fnAdd(EffectiveFormat::Luminance8, FormatUsageInfo::kLuminanceSwizzleRGBA);

    pi = {LOCAL_GL_ALPHA, LOCAL_GL_UNSIGNED_BYTE};
    dui = {LOCAL_GL_R8, LOCAL_GL_RED, LOCAL_GL_UNSIGNED_BYTE};
    fnAdd(EffectiveFormat::Alpha8, FormatUsageInfo::kAlphaSwizzleRGBA);

    pi = {LOCAL_GL_LUMINANCE_ALPHA, LOCAL_GL_UNSIGNED_BYTE};
    dui = {LOCAL_GL_RG8, LOCAL_GL_RG, LOCAL_GL_UNSIGNED_BYTE};
    fnAdd(EffectiveFormat::Luminance8Alpha8,
          FormatUsageInfo::kLumAlphaSwizzleRGBA);
  } else {
    // clang-format off
        AddSimpleUnsized(fua, LOCAL_GL_LUMINANCE      , LOCAL_GL_UNSIGNED_BYTE, EffectiveFormat::Luminance8      );
        AddSimpleUnsized(fua, LOCAL_GL_ALPHA          , LOCAL_GL_UNSIGNED_BYTE, EffectiveFormat::Alpha8          );
        AddSimpleUnsized(fua, LOCAL_GL_LUMINANCE_ALPHA, LOCAL_GL_UNSIGNED_BYTE, EffectiveFormat::Luminance8Alpha8);
    // clang-format on
  }

  return true;
}

static bool AddUnsizedFormats(FormatUsageAuthority* fua, gl::GLContext* gl) {
  // clang-format off

    // GLES 2.0.25, p63, Table 3.4
    AddSimpleUnsized(fua, LOCAL_GL_RGBA, LOCAL_GL_UNSIGNED_BYTE         , EffectiveFormat::RGBA8  );
    AddSimpleUnsized(fua, LOCAL_GL_RGBA, LOCAL_GL_UNSIGNED_SHORT_4_4_4_4, EffectiveFormat::RGBA4  );
    AddSimpleUnsized(fua, LOCAL_GL_RGBA, LOCAL_GL_UNSIGNED_SHORT_5_5_5_1, EffectiveFormat::RGB5_A1);
    AddSimpleUnsized(fua, LOCAL_GL_RGB , LOCAL_GL_UNSIGNED_BYTE         , EffectiveFormat::RGB8   );
    AddSimpleUnsized(fua, LOCAL_GL_RGB , LOCAL_GL_UNSIGNED_SHORT_5_6_5  , EffectiveFormat::RGB565 );

    // L, A, LA
    return AddLegacyFormats_LA8(fua, gl);

  // clang-format on
}

void FormatUsageInfo::SetRenderable(const FormatRenderableState& state) {
  if (!renderableState.IsExplicit()) {
    renderableState = state;
  }

#ifdef DEBUG
  const FormatInfo* format = this->format;
  if (format->IsColorFormat()) {
    const FormatInfo* copyDecay =
        format->GetCopyDecayFormat(format->unsizedFormat);
    MOZ_ASSERT(bool(copyDecay),
               "Renderable formats must be in copyDecayFormats.");
    MOZ_ASSERT(copyDecay == format);
  }
#endif
}

std::unique_ptr<FormatUsageAuthority> FormatUsageAuthority::CreateForWebGL1(
    gl::GLContext* gl) {
  std::unique_ptr<FormatUsageAuthority> ret(new FormatUsageAuthority);
  const auto ptr = ret.get();

  ////////////////////////////////////////////////////////////////////////////
  // Usages

  const auto fnSet = [ptr](EffectiveFormat effFormat, bool isRenderable,
                           bool isFilterable) {
    MOZ_ASSERT(!ptr->GetUsage(effFormat));

    auto usage = ptr->EditUsage(effFormat);
    usage->isFilterable = isFilterable;

    if (isRenderable) {
      usage->SetRenderable();
    }
  };

  // Largely from GLES 2.0.25, p117, Table 4.5, but also:
  // * RGBA8: Made renderable in WebGL 1.0, "Framebuffer Object Attachments".
  // * RGB8: Not guaranteed by ES2 to be renderable, but we should allow it for
  //   web-compat. Min-capability mode should mark this as non-renderable.

  constexpr bool ALWAYS = true;  // For better contrast with `false` in tables.

  // clang-format off
  //   |              format              | renderable | filterable |
  fnSet(EffectiveFormat::RGBA8            , ALWAYS     , ALWAYS     );
  fnSet(EffectiveFormat::RGBA4            , ALWAYS     , ALWAYS     );
  fnSet(EffectiveFormat::RGB5_A1          , ALWAYS     , ALWAYS     );
  fnSet(EffectiveFormat::RGB565           , ALWAYS     , ALWAYS     );
  fnSet(EffectiveFormat::RGB8             , ALWAYS     , ALWAYS     );
  // "Legacy" formats
  fnSet(EffectiveFormat::Luminance8Alpha8 , false      , ALWAYS     );
  fnSet(EffectiveFormat::Luminance8       , false      , ALWAYS     );
  fnSet(EffectiveFormat::Alpha8           , false      , ALWAYS     );
  // Depth/stencil
  fnSet(EffectiveFormat::DEPTH_COMPONENT16, ALWAYS     , ALWAYS     );
  fnSet(EffectiveFormat::DEPTH_COMPONENT24, ALWAYS     , ALWAYS     );
  fnSet(EffectiveFormat::STENCIL_INDEX8   , ALWAYS     , false      );
  // Added in WebGL 1.0 spec:
  fnSet(EffectiveFormat::DEPTH24_STENCIL8 , ALWAYS     , ALWAYS     );
  //   |              format              | renderable | filterable |
  // clang-format on

  ////////////////////////////////////
  // RB formats

#define FOO(x) \
  ptr->AllowRBFormat(LOCAL_GL_##x, ptr->GetUsage(EffectiveFormat::x))

  FOO(RGBA4);
  FOO(RGB5_A1);
  FOO(RGB565);
  FOO(DEPTH_COMPONENT16);
  FOO(STENCIL_INDEX8);
  // FOO(DEPTH24_STENCIL8 ); // WebGL 1 uses DEPTH_STENCIL instead of
  // DEPTH24_STENCIL8.

#undef FOO

  ptr->AllowRBFormat(LOCAL_GL_DEPTH_STENCIL,
                     ptr->GetUsage(EffectiveFormat::DEPTH24_STENCIL8));

  ////////////////////////////////////////////////////////////////////////////

  if (!AddUnsizedFormats(ptr, gl)) return nullptr;

  return ret;
}

std::unique_ptr<FormatUsageAuthority> FormatUsageAuthority::CreateForWebGL2(
    gl::GLContext* gl) {
  std::unique_ptr<FormatUsageAuthority> ret(new FormatUsageAuthority);
  const auto ptr = ret.get();

  ////////////////////////////////////////////////////////////////////////////
  // GLES 3.0.4 p111-113

  const auto fnAddSizedUnpack = [ptr](EffectiveFormat effFormat,
                                      GLenum internalFormat,
                                      GLenum unpackFormat, GLenum unpackType) {
    auto usage = ptr->EditUsage(effFormat);

    const PackingInfo pi = {unpackFormat, unpackType};
    const DriverUnpackInfo dui = {internalFormat, unpackFormat, unpackType};
    ptr->AddTexUnpack(usage, pi, dui);
  };

  // clang-format off
#define FOO(x) EffectiveFormat::x, LOCAL_GL_ ## x

    // RGBA
    fnAddSizedUnpack(FOO(RGBA8       ), LOCAL_GL_RGBA, LOCAL_GL_UNSIGNED_BYTE              );
    fnAddSizedUnpack(FOO(RGBA4       ), LOCAL_GL_RGBA, LOCAL_GL_UNSIGNED_SHORT_4_4_4_4     );
    fnAddSizedUnpack(FOO(RGBA4       ), LOCAL_GL_RGBA, LOCAL_GL_UNSIGNED_BYTE              );
    fnAddSizedUnpack(FOO(RGB5_A1     ), LOCAL_GL_RGBA, LOCAL_GL_UNSIGNED_SHORT_5_5_5_1     );
    fnAddSizedUnpack(FOO(RGB5_A1     ), LOCAL_GL_RGBA, LOCAL_GL_UNSIGNED_BYTE              );
    fnAddSizedUnpack(FOO(RGB5_A1     ), LOCAL_GL_RGBA, LOCAL_GL_UNSIGNED_INT_2_10_10_10_REV);
    fnAddSizedUnpack(FOO(SRGB8_ALPHA8), LOCAL_GL_RGBA, LOCAL_GL_UNSIGNED_BYTE              );
    fnAddSizedUnpack(FOO(RGBA8_SNORM ), LOCAL_GL_RGBA, LOCAL_GL_BYTE                       );
    fnAddSizedUnpack(FOO(RGB10_A2    ), LOCAL_GL_RGBA, LOCAL_GL_UNSIGNED_INT_2_10_10_10_REV);
    fnAddSizedUnpack(FOO(RGBA16F     ), LOCAL_GL_RGBA, LOCAL_GL_HALF_FLOAT                 );
    fnAddSizedUnpack(FOO(RGBA16F     ), LOCAL_GL_RGBA, LOCAL_GL_FLOAT                      );
    fnAddSizedUnpack(FOO(RGBA32F     ), LOCAL_GL_RGBA, LOCAL_GL_FLOAT                      );

    // RGBA_INTEGER
    fnAddSizedUnpack(FOO(RGBA8UI   ), LOCAL_GL_RGBA_INTEGER, LOCAL_GL_UNSIGNED_BYTE              );
    fnAddSizedUnpack(FOO(RGBA8I    ), LOCAL_GL_RGBA_INTEGER, LOCAL_GL_BYTE                       );
    fnAddSizedUnpack(FOO(RGBA16UI  ), LOCAL_GL_RGBA_INTEGER, LOCAL_GL_UNSIGNED_SHORT             );
    fnAddSizedUnpack(FOO(RGBA16I   ), LOCAL_GL_RGBA_INTEGER, LOCAL_GL_SHORT                      );
    fnAddSizedUnpack(FOO(RGBA32UI  ), LOCAL_GL_RGBA_INTEGER, LOCAL_GL_UNSIGNED_INT               );
    fnAddSizedUnpack(FOO(RGBA32I   ), LOCAL_GL_RGBA_INTEGER, LOCAL_GL_INT                        );
    fnAddSizedUnpack(FOO(RGB10_A2UI), LOCAL_GL_RGBA_INTEGER, LOCAL_GL_UNSIGNED_INT_2_10_10_10_REV);

    // RGB
    fnAddSizedUnpack(FOO(RGB8          ), LOCAL_GL_RGB, LOCAL_GL_UNSIGNED_BYTE               );
    fnAddSizedUnpack(FOO(SRGB8         ), LOCAL_GL_RGB, LOCAL_GL_UNSIGNED_BYTE               );
    fnAddSizedUnpack(FOO(RGB565        ), LOCAL_GL_RGB, LOCAL_GL_UNSIGNED_SHORT_5_6_5        );
    fnAddSizedUnpack(FOO(RGB565        ), LOCAL_GL_RGB, LOCAL_GL_UNSIGNED_BYTE               );
    fnAddSizedUnpack(FOO(RGB8_SNORM    ), LOCAL_GL_RGB, LOCAL_GL_BYTE                        );
    fnAddSizedUnpack(FOO(R11F_G11F_B10F), LOCAL_GL_RGB, LOCAL_GL_UNSIGNED_INT_10F_11F_11F_REV);
    fnAddSizedUnpack(FOO(R11F_G11F_B10F), LOCAL_GL_RGB, LOCAL_GL_HALF_FLOAT                  );
    fnAddSizedUnpack(FOO(R11F_G11F_B10F), LOCAL_GL_RGB, LOCAL_GL_FLOAT                       );
    fnAddSizedUnpack(FOO(RGB16F        ), LOCAL_GL_RGB, LOCAL_GL_HALF_FLOAT                  );
    fnAddSizedUnpack(FOO(RGB16F        ), LOCAL_GL_RGB, LOCAL_GL_FLOAT                       );
    fnAddSizedUnpack(FOO(RGB9_E5       ), LOCAL_GL_RGB, LOCAL_GL_UNSIGNED_INT_5_9_9_9_REV    );
    fnAddSizedUnpack(FOO(RGB9_E5       ), LOCAL_GL_RGB, LOCAL_GL_HALF_FLOAT                  );
    fnAddSizedUnpack(FOO(RGB9_E5       ), LOCAL_GL_RGB, LOCAL_GL_FLOAT                       );
    fnAddSizedUnpack(FOO(RGB32F        ), LOCAL_GL_RGB, LOCAL_GL_FLOAT                       );

    // RGB_INTEGER
    fnAddSizedUnpack(FOO(RGB8UI ), LOCAL_GL_RGB_INTEGER, LOCAL_GL_UNSIGNED_BYTE );
    fnAddSizedUnpack(FOO(RGB8I  ), LOCAL_GL_RGB_INTEGER, LOCAL_GL_BYTE          );
    fnAddSizedUnpack(FOO(RGB16UI), LOCAL_GL_RGB_INTEGER, LOCAL_GL_UNSIGNED_SHORT);
    fnAddSizedUnpack(FOO(RGB16I ), LOCAL_GL_RGB_INTEGER, LOCAL_GL_SHORT         );
    fnAddSizedUnpack(FOO(RGB32UI), LOCAL_GL_RGB_INTEGER, LOCAL_GL_UNSIGNED_INT  );
    fnAddSizedUnpack(FOO(RGB32I ), LOCAL_GL_RGB_INTEGER, LOCAL_GL_INT           );

    // RG
    fnAddSizedUnpack(FOO(RG8      ), LOCAL_GL_RG, LOCAL_GL_UNSIGNED_BYTE);
    fnAddSizedUnpack(FOO(RG8_SNORM), LOCAL_GL_RG, LOCAL_GL_BYTE         );
    fnAddSizedUnpack(FOO(RG16F    ), LOCAL_GL_RG, LOCAL_GL_HALF_FLOAT   );
    fnAddSizedUnpack(FOO(RG16F    ), LOCAL_GL_RG, LOCAL_GL_FLOAT        );
    fnAddSizedUnpack(FOO(RG32F    ), LOCAL_GL_RG, LOCAL_GL_FLOAT        );

    // RG_INTEGER
    fnAddSizedUnpack(FOO(RG8UI ), LOCAL_GL_RG_INTEGER, LOCAL_GL_UNSIGNED_BYTE );
    fnAddSizedUnpack(FOO(RG8I  ), LOCAL_GL_RG_INTEGER, LOCAL_GL_BYTE          );
    fnAddSizedUnpack(FOO(RG16UI), LOCAL_GL_RG_INTEGER, LOCAL_GL_UNSIGNED_SHORT);
    fnAddSizedUnpack(FOO(RG16I ), LOCAL_GL_RG_INTEGER, LOCAL_GL_SHORT         );
    fnAddSizedUnpack(FOO(RG32UI), LOCAL_GL_RG_INTEGER, LOCAL_GL_UNSIGNED_INT  );
    fnAddSizedUnpack(FOO(RG32I ), LOCAL_GL_RG_INTEGER, LOCAL_GL_INT           );

    // RED
    fnAddSizedUnpack(FOO(R8      ), LOCAL_GL_RED, LOCAL_GL_UNSIGNED_BYTE);
    fnAddSizedUnpack(FOO(R8_SNORM), LOCAL_GL_RED, LOCAL_GL_BYTE         );
    fnAddSizedUnpack(FOO(R16F    ), LOCAL_GL_RED, LOCAL_GL_HALF_FLOAT   );
    fnAddSizedUnpack(FOO(R16F    ), LOCAL_GL_RED, LOCAL_GL_FLOAT        );
    fnAddSizedUnpack(FOO(R32F    ), LOCAL_GL_RED, LOCAL_GL_FLOAT        );

    // RED_INTEGER
    fnAddSizedUnpack(FOO(R8UI ), LOCAL_GL_RED_INTEGER, LOCAL_GL_UNSIGNED_BYTE );
    fnAddSizedUnpack(FOO(R8I  ), LOCAL_GL_RED_INTEGER, LOCAL_GL_BYTE          );
    fnAddSizedUnpack(FOO(R16UI), LOCAL_GL_RED_INTEGER, LOCAL_GL_UNSIGNED_SHORT);
    fnAddSizedUnpack(FOO(R16I ), LOCAL_GL_RED_INTEGER, LOCAL_GL_SHORT         );
    fnAddSizedUnpack(FOO(R32UI), LOCAL_GL_RED_INTEGER, LOCAL_GL_UNSIGNED_INT  );
    fnAddSizedUnpack(FOO(R32I ), LOCAL_GL_RED_INTEGER, LOCAL_GL_INT           );

    // DEPTH_COMPONENT
    fnAddSizedUnpack(FOO(DEPTH_COMPONENT16 ), LOCAL_GL_DEPTH_COMPONENT, LOCAL_GL_UNSIGNED_SHORT);
    fnAddSizedUnpack(FOO(DEPTH_COMPONENT16 ), LOCAL_GL_DEPTH_COMPONENT, LOCAL_GL_UNSIGNED_INT  );
    fnAddSizedUnpack(FOO(DEPTH_COMPONENT24 ), LOCAL_GL_DEPTH_COMPONENT, LOCAL_GL_UNSIGNED_INT  );
    fnAddSizedUnpack(FOO(DEPTH_COMPONENT32F), LOCAL_GL_DEPTH_COMPONENT, LOCAL_GL_FLOAT         );

    // DEPTH_STENCIL
    fnAddSizedUnpack(FOO(DEPTH24_STENCIL8 ), LOCAL_GL_DEPTH_STENCIL, LOCAL_GL_UNSIGNED_INT_24_8             );
    fnAddSizedUnpack(FOO(DEPTH32F_STENCIL8), LOCAL_GL_DEPTH_STENCIL, LOCAL_GL_FLOAT_32_UNSIGNED_INT_24_8_REV);

#undef FOO
  // clang-format on

  ////////////////////////////////////////////////////////////////////////////

  // For renderable, see GLES 3.0.4, p212 "Framebuffer Completeness"
  // For filterable, see GLES 3.0.4, p161 "...a texture is complete unless..."

  const auto fnAllowES3TexFormat = [ptr](GLenum sizedFormat,
                                         EffectiveFormat effFormat,
                                         bool isRenderable, bool isFilterable) {
    auto usage = ptr->EditUsage(effFormat);
    usage->isFilterable = isFilterable;

    if (isRenderable) {
      usage->SetRenderable();
    }

    ptr->AllowSizedTexFormat(sizedFormat, usage);

    if (isRenderable) {
      ptr->AllowRBFormat(sizedFormat, usage);
    }
  };

  constexpr bool ALWAYS = true;  // For better contrast with `false` in tables.

  // clang-format off
#define _(x)  LOCAL_GL_##x, EffectiveFormat::x

  // GLES 3.0.4, p128-129 "Required Texture Formats"
  // GLES 3.0.4, p130-132, table 3.13
  //                 |     format          | renderable | filterable |
  fnAllowES3TexFormat(_(R8)                , ALWAYS     , ALWAYS     );
  fnAllowES3TexFormat(_(R8_SNORM)          , false      , ALWAYS     );
  fnAllowES3TexFormat(_(RG8)               , ALWAYS     , ALWAYS     );
  fnAllowES3TexFormat(_(RG8_SNORM)         , false      , ALWAYS     );
  fnAllowES3TexFormat(_(RGB8)              , ALWAYS     , ALWAYS     );
  fnAllowES3TexFormat(_(RGB8_SNORM)        , false      , ALWAYS     );
  fnAllowES3TexFormat(_(RGB565)            , ALWAYS     , ALWAYS     );
  fnAllowES3TexFormat(_(RGBA4)             , ALWAYS     , ALWAYS     );
  fnAllowES3TexFormat(_(RGB5_A1)           , ALWAYS     , ALWAYS     );
  fnAllowES3TexFormat(_(RGBA8)             , ALWAYS     , ALWAYS     );
  fnAllowES3TexFormat(_(RGBA8_SNORM)       , false      , ALWAYS     );
  fnAllowES3TexFormat(_(RGB10_A2)          , ALWAYS     , ALWAYS     );
  fnAllowES3TexFormat(_(RGB10_A2UI)        , ALWAYS     , false      );

  fnAllowES3TexFormat(_(SRGB8)             , false      , ALWAYS     );
  fnAllowES3TexFormat(_(SRGB8_ALPHA8)      , ALWAYS     , ALWAYS     );
  //                 |     format          | renderable | filterable |
  fnAllowES3TexFormat(_(R16F)              , false      , ALWAYS     );
  fnAllowES3TexFormat(_(RG16F)             , false      , ALWAYS     );
  fnAllowES3TexFormat(_(RGB16F)            , false      , ALWAYS     );
  fnAllowES3TexFormat(_(RGBA16F)           , false      , ALWAYS     );

  fnAllowES3TexFormat(_(R32F)              , false      , false      );
  fnAllowES3TexFormat(_(RG32F)             , false      , false      );
  fnAllowES3TexFormat(_(RGB32F)            , false      , false      );
  fnAllowES3TexFormat(_(RGBA32F)           , false      , false      );

  fnAllowES3TexFormat(_(R11F_G11F_B10F)    , false      , ALWAYS     );
  fnAllowES3TexFormat(_(RGB9_E5)           , false      , ALWAYS     );
  //                 |     format          | renderable | filterable |
  fnAllowES3TexFormat(_(R8I)               , ALWAYS     , false      );
  fnAllowES3TexFormat(_(R8UI)              , ALWAYS     , false      );
  fnAllowES3TexFormat(_(R16I)              , ALWAYS     , false      );
  fnAllowES3TexFormat(_(R16UI)             , ALWAYS     , false      );
  fnAllowES3TexFormat(_(R32I)              , ALWAYS     , false      );
  fnAllowES3TexFormat(_(R32UI)             , ALWAYS     , false      );

  fnAllowES3TexFormat(_(RG8I)              , ALWAYS     , false      );
  fnAllowES3TexFormat(_(RG8UI)             , ALWAYS     , false      );
  fnAllowES3TexFormat(_(RG16I)             , ALWAYS     , false      );
  fnAllowES3TexFormat(_(RG16UI)            , ALWAYS     , false      );
  fnAllowES3TexFormat(_(RG32I)             , ALWAYS     , false      );
  fnAllowES3TexFormat(_(RG32UI)            , ALWAYS     , false      );
  //                 |     format          | renderable | filterable |
  fnAllowES3TexFormat(_(RGB8I)             , false      , false      );
  fnAllowES3TexFormat(_(RGB8UI)            , false      , false      );
  fnAllowES3TexFormat(_(RGB16I)            , false      , false      );
  fnAllowES3TexFormat(_(RGB16UI)           , false      , false      );
  fnAllowES3TexFormat(_(RGB32I)            , false      , false      );
  fnAllowES3TexFormat(_(RGB32UI)           , false      , false      );

  fnAllowES3TexFormat(_(RGBA8I)            , ALWAYS     , false      );
  fnAllowES3TexFormat(_(RGBA8UI)           , ALWAYS     , false      );
  fnAllowES3TexFormat(_(RGBA16I)           , ALWAYS     , false      );
  fnAllowES3TexFormat(_(RGBA16UI)          , ALWAYS     , false      );
  fnAllowES3TexFormat(_(RGBA32I)           , ALWAYS     , false      );
  fnAllowES3TexFormat(_(RGBA32UI)          , ALWAYS     , false      );
  //                 |     format          | renderable | filterable |
  fnAllowES3TexFormat(_(DEPTH_COMPONENT16) , ALWAYS     , false      ); // [1]
  fnAllowES3TexFormat(_(DEPTH_COMPONENT24) , ALWAYS     , false      ); // [1]
  fnAllowES3TexFormat(_(DEPTH_COMPONENT32F), ALWAYS     , false      ); // [1]
  fnAllowES3TexFormat(_(DEPTH24_STENCIL8)  , ALWAYS     , false      ); // [1]
  fnAllowES3TexFormat(_(DEPTH32F_STENCIL8) , ALWAYS     , false      ); // [1]

  // [1]: Sized depth or depth-stencil formats are not filterable
  //      per GLES 3.0.6 p161.
  //      Specifically, they're texture-incomplete if depth-compare:none and
  //      not NEAREST.

#undef _
  // clang-format on

  // GLES 3.0.4, p206, "Required Renderbuffer Formats":
  // "Implementations are also required to support STENCIL_INDEX8. Requesting
  // this internal format for a renderbuffer will allocate at least 8 stencil
  // bit planes."

  auto usage = ptr->EditUsage(EffectiveFormat::STENCIL_INDEX8);
  usage->SetRenderable();
  ptr->AllowRBFormat(LOCAL_GL_STENCIL_INDEX8, usage);

  ////////////////
  // Legacy formats

  if (!AddUnsizedFormats(ptr, gl)) return nullptr;

  ptr->AllowRBFormat(LOCAL_GL_DEPTH_STENCIL,
                     ptr->GetUsage(EffectiveFormat::DEPTH24_STENCIL8));

  ////////////////////////////////////

  return ret;
}

//////////////////////////////////////////////////////////////////////////////////////////

void FormatUsageAuthority::AddTexUnpack(FormatUsageInfo* usage,
                                        const PackingInfo& pi,
                                        const DriverUnpackInfo& dui) {
  // Don't AlwaysInsert here, since we'll see duplicates from sized and unsized
  // formats.
  auto res = usage->validUnpacks.insert({pi, dui});
  auto itr = res.first;

  if (!usage->idealUnpack) {
    // First one!
    usage->idealUnpack = &(itr->second);
  }

  mValidTexUnpackFormats.insert(pi.format);
  mValidTexUnpackTypes.insert(pi.type);
}

static bool Contains(const std::set<GLenum>& set, GLenum key) {
  return set.find(key) != set.end();
}

bool FormatUsageAuthority::IsInternalFormatEnumValid(
    GLenum internalFormat) const {
  return Contains(mValidTexInternalFormats, internalFormat);
}

bool FormatUsageAuthority::AreUnpackEnumsValid(GLenum unpackFormat,
                                               GLenum unpackType) const {
  return (Contains(mValidTexUnpackFormats, unpackFormat) &&
          Contains(mValidTexUnpackTypes, unpackType));
}

////////////////////

void FormatUsageAuthority::AllowRBFormat(GLenum sizedFormat,
                                         const FormatUsageInfo* usage,
                                         const bool expectRenderable) {
  MOZ_ASSERT(!usage->format->compression);
  MOZ_ASSERT(usage->format->sizedFormat);
  MOZ_ASSERT(usage->IsRenderable() || !expectRenderable);

  const auto& found = mRBFormatMap.find(sizedFormat);
  if (found != mRBFormatMap.end()) {
    MOZ_ASSERT(found->second == usage);
    return;
  }
  AlwaysInsert(mRBFormatMap, sizedFormat, usage);
}

void FormatUsageAuthority::AllowSizedTexFormat(GLenum sizedFormat,
                                               const FormatUsageInfo* usage) {
  if (usage->format->compression) {
    MOZ_ASSERT(usage->isFilterable, "Compressed formats should be filterable.");
  } else {
    MOZ_ASSERT(!usage->validUnpacks.empty() && usage->idealUnpack,
               "AddTexUnpack() first.");
  }

  AlwaysInsert(mSizedTexFormatMap, sizedFormat, usage);

  mValidTexInternalFormats.insert(sizedFormat);
}

void FormatUsageAuthority::AllowUnsizedTexFormat(const PackingInfo& pi,
                                                 const FormatUsageInfo* usage) {
  MOZ_ASSERT(!usage->format->compression);
  MOZ_ASSERT(!usage->validUnpacks.empty() && usage->idealUnpack,
             "AddTexUnpack() first.");

  AlwaysInsert(mUnsizedTexFormatMap, pi, usage);

  mValidTexInternalFormats.insert(pi.format);
  mValidTexUnpackFormats.insert(pi.format);
  mValidTexUnpackTypes.insert(pi.type);
}

const FormatUsageInfo* FormatUsageAuthority::GetRBUsage(
    GLenum sizedFormat) const {
  return FindOrNull(mRBFormatMap, sizedFormat);
}

const FormatUsageInfo* FormatUsageAuthority::GetSizedTexUsage(
    GLenum sizedFormat) const {
  return FindOrNull(mSizedTexFormatMap, sizedFormat);
}

const FormatUsageInfo* FormatUsageAuthority::GetUnsizedTexUsage(
    const PackingInfo& pi) const {
  return FindOrNull(mUnsizedTexFormatMap, pi);
}

FormatUsageInfo* FormatUsageAuthority::EditUsage(EffectiveFormat format) {
  auto itr = mUsageMap.find(format);

  if (itr == mUsageMap.end()) {
    const FormatInfo* formatInfo = GetFormat(format);
    MOZ_RELEASE_ASSERT(formatInfo, "GFX: no format info set.");

    FormatUsageInfo usage(formatInfo);

    auto res = mUsageMap.insert({format, usage});
    DebugOnly<bool> didInsert = res.second;
    MOZ_ASSERT(didInsert);

    itr = res.first;
  }

  return &(itr->second);
}

const FormatUsageInfo* FormatUsageAuthority::GetUsage(
    EffectiveFormat format) const {
  auto itr = mUsageMap.find(format);
  if (itr == mUsageMap.end()) return nullptr;

  return &(itr->second);
}

////////////////////////////////////////////////////////////////////////////////

}  // namespace mozilla::webgl
